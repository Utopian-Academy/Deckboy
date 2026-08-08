// ---------------------------------------------------------------------------
// st2110_output.cpp — ST 2110-20 packetisation and multicast transmission.
//
// Wire format, so the packing below can be checked against the spec:
//
//   RTP header (RFC 3550, 12 bytes)
//     V=2, P=0, X=0, CC=0, M=<last packet of frame>, PT=<dynamic>
//     sequence number, timestamp (90 kHz), SSRC
//
//   ST 2110-20 payload header
//     Extended Sequence Number      16 bits   (high half of a 32-bit seq)
//     then 1..3 Sample Row Data descriptors:
//       SRD Length                  16 bits   (octets of THIS row fragment)
//       F | SRD Row Number          1 | 15    (F=1 => another SRD follows)
//       C | SRD Offset              1 | 15    (C=1 => another SRD follows)
//     The LAST descriptor clears both continuation bits.
//
//   Payload: an integer number of pgroups, in raster order.
//
// This implementation emits one SRD per packet — legal, simple, and what most
// senders do for progressive video. Multi-SRD packets only save header bytes.
// ---------------------------------------------------------------------------
#include "st2110_output.hpp"

#include "ptp_client.hpp"   // grandmaster time for the RTP media clock

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <cmath>
#include <cstring>
#include <random>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

namespace deckboy {
namespace platform {
namespace video {

namespace {

// The SDP origin address when no interface is explicitly configured.
//
// Hardcoding 127.0.0.1 here (the old behaviour) put "o=- 0 0 IN IP4 127.0.0.1"
// in every SDP a plant receives, which describes the session as originating on
// the receiver's own loopback. The connection line `c=` is what a receiver
// actually joins on and was always correct, so this was cosmetic — but it is
// the kind of detail an engineer reads when a stream misbehaves, and a wrong
// answer there costs someone half an hour.
//
// Resolved by asking the routing table which interface reaches the stream's own
// DESTINATION GROUP; no packet is sent. Probing a generic internet address
// instead would answer with the default route, which on a machine running a VPN
// is the tunnel — so the SDP would claim the stream originates on an interface
// that carries no 2110 at all. Asking about the real destination gets the NIC
// the multicast actually leaves by.
//
// MEMOISED per destination: SDPs are rebuilt whenever the NMOS sender snapshot
// is taken, so doing this per call would open sockets at frame rate. Refreshed
// every 30 s so a NIC change is picked up without polling.
std::string defaultOriginAddress(const std::string& destination) {
  struct Entry { std::string address; std::chrono::steady_clock::time_point at; };
  static std::mutex cacheMutex;
  static std::map<std::string, Entry> cache;

  const auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto it = cache.find(destination);
    if (it != cache.end() && !it->second.address.empty() &&
        now - it->second.at < std::chrono::seconds(30)) {
      return it->second.address;
    }
  }

  std::string resolved;
  socket_t probe = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (probe != kInvalidSocket) {
    sockaddr_in target {};
    target.sin_family = AF_INET;
    target.sin_port = htons(53);
    const std::string peer = destination.empty() ? std::string("8.8.8.8") : destination;
    if (::inet_pton(AF_INET, peer.c_str(), &target.sin_addr) == 1 &&
        ::connect(probe, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0) {
      sockaddr_in local {};
#ifdef _WIN32
      int length = sizeof(local);
#else
      socklen_t length = sizeof(local);
#endif
      if (::getsockname(probe, reinterpret_cast<sockaddr*>(&local), &length) == 0) {
        char buffer[INET_ADDRSTRLEN] = {};
        if (::inet_ntop(AF_INET, &local.sin_addr, buffer, sizeof(buffer))) {
          resolved = buffer;
        }
      }
    }
#ifdef _WIN32
    ::closesocket(probe);
#else
    ::close(probe);
#endif
  }
  // Only fall back to loopback if the machine genuinely has no route out.
  if (resolved.empty()) {
    resolved = "127.0.0.1";
  }

  std::lock_guard<std::mutex> lock(cacheMutex);
  // Bounded: one entry per destination group in use, and a show has a handful.
  // Clearing wholesale beats unbounded growth if something ever churns groups.
  if (cache.size() > 16) {
    cache.clear();
  }
  cache[destination] = Entry{resolved, now};
  return resolved;
}

constexpr std::uint32_t kVideoClockRateHz = 90000;  // ST 2110-20 video RTP clock
constexpr std::size_t kRtpHeaderBytes = 12;
// Extended sequence number (2) + one SRD descriptor (6).
constexpr std::size_t kPayloadHeaderBytes = 8;

void writeBe16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
  p[1] = static_cast<std::uint8_t>(v & 0xFF);
}

void writeBe32(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
  p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
  p[2] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
  p[3] = static_cast<std::uint8_t>(v & 0xFF);
}

// Pack a run of 10-bit samples MSB-first into a byte stream. ST 2110-20 packs
// samples contiguously with no padding inside a pgroup, so 4 samples (one
// 4:2:2 pgroup = 2 pixels) occupy exactly 5 octets.
class BitPacker {
 public:
  explicit BitPacker(std::uint8_t* out) : out_(out) {}

  void push(std::uint16_t value, int bits) {
    for (int i = bits - 1; i >= 0; --i) {
      const std::uint32_t bit = (value >> i) & 0x1u;
      accum_ = static_cast<std::uint8_t>((accum_ << 1) | bit);
      if (++bitCount_ == 8) {
        out_[written_++] = accum_;
        accum_ = 0;
        bitCount_ = 0;
      }
    }
  }

  std::size_t written() const { return written_; }

 private:
  std::uint8_t* out_;
  std::size_t written_ = 0;
  std::uint8_t accum_ = 0;
  int bitCount_ = 0;
};

}  // namespace

int st2110PgroupBytes(St2110Sampling sampling) {
  switch (sampling) {
    case St2110Sampling::YCbCr422_10bit: return 5;
    case St2110Sampling::YCbCr422_8bit:  return 4;
  }
  return 5;
}

int st2110PgroupPixels(St2110Sampling sampling) {
  // Both 4:2:2 modes carry 2 pixels (Cb Y Cr Y) per pgroup.
  (void)sampling;
  return 2;
}

const char* st2110SamplingLabel(St2110Sampling sampling) {
  switch (sampling) {
    case St2110Sampling::YCbCr422_10bit: return "YCbCr-4:2:2 10-bit";
    case St2110Sampling::YCbCr422_8bit:  return "YCbCr-4:2:2 8-bit";
  }
  return "YCbCr-4:2:2 10-bit";
}

std::string st2110BuildSdp(const St2110Config& config, const std::string& sessionName,
                           bool ptpLocked, const std::string& ptpGrandmaster,
                           int ptpDomain) {
  const int depth = config.sampling == St2110Sampling::YCbCr422_10bit ? 10 : 8;
  // Frame rate as an exact rational: 59.94 must be advertised 60000/1001, not
  // "59.94", or receivers derive the wrong media clock.
  std::string rateText;
  {
    const double fps = config.frameRate;
    const double nearestDrop = std::round(fps * 1001.0 / 1000.0);
    if (std::abs(fps - (nearestDrop * 1000.0 / 1001.0)) < 0.01 &&
        std::abs(fps - nearestDrop) > 0.001) {
      rateText = std::to_string(static_cast<int>(nearestDrop) * 1000) + "/1001";
    } else {
      rateText = std::to_string(static_cast<int>(std::lround(fps)));
    }
  }

  std::ostringstream sdp;
  sdp << "v=0\r\n"
      << "o=- 0 0 IN IP4 " << (config.interfaceAddress.empty() ? defaultOriginAddress(config.destinationAddress) : config.interfaceAddress) << "\r\n"
      << "s=" << (sessionName.empty() ? "Deckboy ST 2110-20" : sessionName) << "\r\n"
      << "t=0 0\r\n"
      << "m=video " << config.destinationPort << " RTP/AVP " << config.payloadType << "\r\n"
      << "c=IN IP4 " << config.destinationAddress << "/" << config.timeToLive << "\r\n"
      << "a=rtpmap:" << config.payloadType << " raw/" << kVideoClockRateHz << "\r\n"
      << "a=fmtp:" << config.payloadType
      << " sampling=YCbCr-4:2:2; width=" << config.width
      << "; height=" << config.height
      << "; exactframerate=" << rateText
      << "; depth=" << depth
      << "; TCS=SDR; colorimetry=BT709; PM=2110GPM; SSN=\"ST2110-20:2017\"; TP=2110TPW"
      << (config.interlaced ? "; interlace" : "")
      << "\r\n"
      ;
  // Clock advertisement follows the ACTUAL state. Only claim a traceable
  // grandmaster when genuinely locked to one — an optimistic ts-refclk is how a
  // stream silently fails to genlock in a real plant.
  if (ptpLocked && !ptpGrandmaster.empty()) {
    sdp << "a=ts-refclk:ptp=IEEE1588-2008:" << ptpGrandmaster << ":" << ptpDomain << "\r\n"
        << "a=mediaclk:direct=0\r\n";
  } else {
    sdp << "a=ts-refclk:localmac=00-00-00-00-00-00\r\n"
        << "a=mediaclk:sender\r\n";
  }
  return sdp.str();
}

St2110Output::St2110Output() : socket_(static_cast<long long>(kInvalidSocket)) {
  std::random_device rd;
  ssrc_ = static_cast<std::uint32_t>(rd());
  sequenceNumber_ = static_cast<std::uint16_t>(rd() & 0xFFFF);
}

St2110Output::~St2110Output() {
  close();
}

bool St2110Output::open(const St2110Config& config) {
  close();
  config_ = config;
  lastError_.clear();

  if (config_.width <= 0 || config_.height <= 0) {
    setLastError("invalid raster");
    return false;
  }

#ifdef _WIN32
  if (!winsockStarted_) {
    WSADATA wsa {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      setLastError("WSAStartup failed");
      return false;
    }
    winsockStarted_ = true;
  }
#endif

  socket_t sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == kInvalidSocket) {
    setLastError("socket() failed");
    return false;
  }

  // Multicast TTL keeps the stream inside the facility unless deliberately
  // raised — an uncompressed 2110 flow escaping onto a wider network is a
  // multi-gigabit accident.
  const int ttl = std::clamp(config_.timeToLive, 1, 255);
  ::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,
               reinterpret_cast<const char*>(&ttl), sizeof(ttl));

  // Pin the egress NIC when asked. On a media network the interface choice is
  // not incidental — the wrong NIC means the stream never reaches the plant.
  if (!config_.interfaceAddress.empty()) {
    in_addr ifAddr {};
    if (::inet_pton(AF_INET, config_.interfaceAddress.c_str(), &ifAddr) == 1) {
      ::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF,
                   reinterpret_cast<const char*>(&ifAddr), sizeof(ifAddr));
    }
  }

  // Uncompressed video bursts hard; a small kernel buffer drops packets that
  // the receiver then reports as picture corruption.
  const int sendBuf = 8 * 1024 * 1024;
  ::setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));

  socket_ = static_cast<long long>(sock);
  packet_.assign(kRtpHeaderBytes + kPayloadHeaderBytes +
                     static_cast<std::size_t>(std::max(256, config_.maxPayloadBytes)),
                 0);
  rowSamples_.assign(static_cast<std::size_t>(config_.width) * 2u, 0);
  framesSent_.store(0, std::memory_order_relaxed);
  framesDropped_.store(0, std::memory_order_relaxed);
  pacingErrorMicros_.store(0.0, std::memory_order_relaxed);
  senderStop_.store(false);
  hasPending_ = false;
  senderThread_ = std::thread([this] { senderLoop(); });
  return true;
}

void St2110Output::close() {
  // Stop the sender BEFORE the socket goes away, or it can transmit into a
  // closed descriptor.
  if (senderThread_.joinable()) {
    senderStop_.store(true);
    frameCv_.notify_all();
    senderThread_.join();
  }
  senderStop_.store(false);
  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    hasPending_ = false;
    pendingFrame_.clear();
  }
  if (socket_ != static_cast<long long>(kInvalidSocket)) {
#ifdef _WIN32
    ::closesocket(static_cast<socket_t>(socket_));
#else
    ::close(static_cast<socket_t>(socket_));
#endif
    socket_ = static_cast<long long>(kInvalidSocket);
  }
#ifdef _WIN32
  if (winsockStarted_) {
    WSACleanup();
    winsockStarted_ = false;
  }
#endif
}

void St2110Output::convertRowToYCbCr422(const std::uint8_t* srcRowBgra, int width,
                                        std::vector<std::uint16_t>& out) const {
  const bool tenBit = config_.sampling == St2110Sampling::YCbCr422_10bit;
  const int maxCode = tenBit ? 1023 : 255;
  // BT.709 studio swing. Luma 64-940 (10-bit) / 16-235 (8-bit); chroma centred.
  const double lumaLow = tenBit ? 64.0 : 16.0;
  const double lumaRange = tenBit ? 876.0 : 219.0;
  const double chromaCentre = tenBit ? 512.0 : 128.0;
  const double chromaRange = tenBit ? 896.0 : 224.0;

  out.resize(static_cast<std::size_t>(width) * 2u);
  std::size_t outIndex = 0;

  // 4:2:2 subsampling: co-sited chroma averaged across each horizontal pair,
  // emitted in Cb Y Cr Y order as the payload requires.
  for (int x = 0; x < width; x += 2) {
    const std::uint8_t* p0 = srcRowBgra + static_cast<std::size_t>(x) * 4u;
    const int x1 = std::min(x + 1, width - 1);
    const std::uint8_t* p1 = srcRowBgra + static_cast<std::size_t>(x1) * 4u;

    auto toY = [&](const std::uint8_t* p) {
      const double b = p[0] / 255.0, g = p[1] / 255.0, r = p[2] / 255.0;
      const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
      return static_cast<std::uint16_t>(
          std::clamp(std::lround(lumaLow + y * lumaRange), 0L, static_cast<long>(maxCode)));
    };
    auto cbcr = [&](const std::uint8_t* p, double& cb, double& cr) {
      const double b = p[0] / 255.0, g = p[1] / 255.0, r = p[2] / 255.0;
      const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
      cb = (b - y) / 1.8556;
      cr = (r - y) / 1.5748;
    };

    double cb0 = 0, cr0 = 0, cb1 = 0, cr1 = 0;
    cbcr(p0, cb0, cr0);
    cbcr(p1, cb1, cr1);
    const double cb = (cb0 + cb1) * 0.5;
    const double cr = (cr0 + cr1) * 0.5;

    const auto encodeChroma = [&](double v) {
      return static_cast<std::uint16_t>(
          std::clamp(std::lround(chromaCentre + v * chromaRange), 0L,
                     static_cast<long>(maxCode)));
    };

    out[outIndex++] = encodeChroma(cb);
    out[outIndex++] = toY(p0);
    out[outIndex++] = encodeChroma(cr);
    out[outIndex++] = toY(p1);
  }
}

bool St2110Output::sendPacket(const std::uint8_t* data, std::size_t size) {
  sockaddr_in dest {};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(static_cast<std::uint16_t>(config_.destinationPort));
  if (::inet_pton(AF_INET, config_.destinationAddress.c_str(), &dest.sin_addr) != 1) {
    setLastError("invalid destination address");
    return false;
  }
  const int sent = ::sendto(static_cast<socket_t>(socket_),
                            reinterpret_cast<const char*>(data),
                            static_cast<int>(size), 0,
                            reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
  if (sent < 0) {
    setLastError("sendto() failed");
    return false;
  }
  return true;
}

void St2110Output::setLastError(const std::string& message) {
  std::lock_guard<std::mutex> lock(errorMutex_);
  lastError_ = message;
}

std::string St2110Output::lastError() const {
  std::lock_guard<std::mutex> lock(errorMutex_);
  return lastError_;
}

// Hand the frame to the sender thread and get straight back to rendering.
bool St2110Output::sendFrame(const std::uint8_t* srcBgra, int srcStrideBytes) {
  if (!isOpen() || srcBgra == nullptr) {
    return false;
  }
  const std::size_t needed =
    static_cast<std::size_t>(srcStrideBytes) * static_cast<std::size_t>(config_.height);
  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (hasPending_) {
      // Sender is still pacing the previous frame. Overwrite it: a live stream
      // wants the newest picture, and queueing would only add latency.
      framesDropped_.fetch_add(1, std::memory_order_relaxed);
    }
    pendingFrame_.resize(needed);
    std::memcpy(pendingFrame_.data(), srcBgra, needed);
    pendingStride_ = srcStrideBytes;
    hasPending_ = true;
  }
  frameCv_.notify_one();
  return true;
}

void St2110Output::senderLoop() {
  std::vector<std::uint8_t> frame;
  int stride = 0;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(frameMutex_);
      frameCv_.wait(lock, [this] { return hasPending_ || senderStop_.load(); });
      if (senderStop_.load()) {
        return;
      }
      frame.swap(pendingFrame_);
      stride = pendingStride_;
      hasPending_ = false;
    }
    sendFramePaced(frame, stride);
  }
}

void St2110Output::sendFramePaced(const std::vector<std::uint8_t>& bgraFrame,
                                  int srcStrideBytes) {
  const std::uint8_t* srcBgra = bgraFrame.data();
  if (srcBgra == nullptr || bgraFrame.empty()) {
    return;
  }

  const int width = config_.width;
  const int height = config_.height;
  const int pgBytes = st2110PgroupBytes(config_.sampling);
  const int pgPixels = st2110PgroupPixels(config_.sampling);
  const bool tenBit = config_.sampling == St2110Sampling::YCbCr422_10bit;

  // Whole pgroups only, and never more than one row per packet in this
  // single-SRD form.
  const int maxPayload = (config_.maxPayloadBytes / pgBytes) * pgBytes;
  if (maxPayload < pgBytes) {
    setLastError("MTU too small for one pgroup");
    return;
  }
  const int pgroupsPerRow = width / pgPixels;
  const int rowBytes = pgroupsPerRow * pgBytes;

  // RTP timestamp: one value for every packet of a frame (they share a sampling
  // instant). Derived from the monotonic clock — this is the line that becomes
  // a PTP read when ST 2059 lands, and nothing else changes.
  // THE line the PTP client exists for. ST 2110-10 derives the RTP timestamp
  // from the media clock, which in a real plant is the PTP grandmaster — that
  // is what lets independent senders align. When we are genuinely locked, use
  // grandmaster time; otherwise fall back to the local clock and keep saying so
  // in the SDP.
  std::uint64_t clockNanos = 0;
  if (ptp_ != nullptr && ptp_->locked()) {
    clockNanos = ptp_->ptpNanosNow();
  } else {
    clockNanos = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
  }
  // 90 kHz ticks since the epoch, wrapped to 32 bits. Done in 128-bit-safe
  // steps: nanos * 90000 would overflow a 64-bit value outright.
  const std::uint64_t seconds = clockNanos / 1'000'000'000ull;
  const std::uint64_t remainder = clockNanos % 1'000'000'000ull;
  const std::uint64_t ticks =
    seconds * kVideoClockRateHz + (remainder * kVideoClockRateHz) / 1'000'000'000ull;
  const std::uint32_t rtpTimestamp = static_cast<std::uint32_t>(ticks & 0xFFFFFFFFull);

  // ── ST 2110-21 pacing ──────────────────────────────────────────────────────
  // Without this the whole frame goes out as fast as sendto() will take it —
  // ~2.6 Gb/s of instantaneous burst for 1080p59.94. Real receivers under-run
  // or drop on that, and it is flatly non-conformant.
  //
  // Spread the frame's packets evenly across the frame interval instead. This
  // is the WIDE model (TP=2110TPW, which is what the SDP advertises): true
  // narrow needs hardware pacing (Rivermax/DPDK) and is out of reach from a
  // user-space socket on Windows. Wide is a legitimate, declarable profile —
  // and it is enormously better than a burst.
  const int packetsPerRow = (rowBytes + maxPayload - 1) / maxPayload;
  const long long totalPackets =
    static_cast<long long>(packetsPerRow) * static_cast<long long>(height);
  const double frameSeconds = (config_.frameRate > 1.0) ? (1.0 / config_.frameRate) : (1.0 / 60.0);
  // Aim to finish a little early so a late frame never eats into the next one.
  const double packetIntervalNs =
    (totalPackets > 0) ? (frameSeconds * 0.95 * 1e9 / static_cast<double>(totalPackets)) : 0.0;
  const auto frameStart = std::chrono::steady_clock::now();
  long long packetIndex = 0;
  double pacingErrorSumNs = 0.0;

  // Hybrid wait: sleep away the bulk (cheap, coarse) then spin the last ~1.2 ms
  // (accurate, costly). Sleeping alone on Windows overshoots by whole
  // milliseconds, which at these packet rates is hundreds of packets late.
  auto waitUntil = [](std::chrono::steady_clock::time_point deadline) {
    for (;;) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        return;
      }
      const auto remaining = deadline - now;
      if (remaining > std::chrono::microseconds(1200)) {
        std::this_thread::sleep_for(remaining - std::chrono::microseconds(1000));
      } else {
        std::this_thread::yield();
      }
    }
  };

  for (int row = 0; row < height; ++row) {
    if (senderStop_.load(std::memory_order_relaxed)) {
      return;
    }
    const std::uint8_t* srcRow = srcBgra + static_cast<std::size_t>(row) *
                                               static_cast<std::size_t>(srcStrideBytes);
    convertRowToYCbCr422(srcRow, width, rowSamples_);

    int offsetBytes = 0;
    while (offsetBytes < rowBytes) {
      const int chunkBytes = std::min(maxPayload, rowBytes - offsetBytes);
      const int chunkPgroups = chunkBytes / pgBytes;
      const int pixelOffset = (offsetBytes / pgBytes) * pgPixels;
      const bool lastPacketOfFrame =
          (row == height - 1) && (offsetBytes + chunkBytes >= rowBytes);

      std::uint8_t* p = packet_.data();

      // --- RTP header ---
      p[0] = 0x80;  // V=2, no padding/extension, CC=0
      p[1] = static_cast<std::uint8_t>((lastPacketOfFrame ? 0x80 : 0x00) |
                                       (config_.payloadType & 0x7F));
      writeBe16(p + 2, sequenceNumber_);
      writeBe32(p + 4, rtpTimestamp);
      writeBe32(p + 8, ssrc_);

      // --- ST 2110-20 payload header (single SRD) ---
      std::uint8_t* ph = p + kRtpHeaderBytes;
      writeBe16(ph, static_cast<std::uint16_t>(extendedSequenceHigh_ & 0xFFFF));
      writeBe16(ph + 2, static_cast<std::uint16_t>(chunkBytes));
      // F=0: no further SRD. Row number is 15 bits.
      writeBe16(ph + 4, static_cast<std::uint16_t>(row & 0x7FFF));
      // C=0: last SRD in this packet. Offset is in PIXELS, not octets.
      writeBe16(ph + 6, static_cast<std::uint16_t>(pixelOffset & 0x7FFF));

      // --- Payload ---
      std::uint8_t* payload = ph + kPayloadHeaderBytes;
      const int firstSample = (pixelOffset / pgPixels) * 4;  // 4 samples per pgroup
      if (tenBit) {
        BitPacker packer(payload);
        for (int i = 0; i < chunkPgroups * 4; ++i) {
          packer.push(rowSamples_[static_cast<std::size_t>(firstSample + i)], 10);
        }
      } else {
        for (int i = 0; i < chunkPgroups * 4; ++i) {
          payload[i] = static_cast<std::uint8_t>(
              rowSamples_[static_cast<std::size_t>(firstSample + i)] & 0xFF);
        }
      }

      // Hold this packet until its slot in the frame.
      if (packetIntervalNs > 0.0) {
        const auto due = frameStart + std::chrono::nanoseconds(
          static_cast<long long>(packetIntervalNs * static_cast<double>(packetIndex)));
        waitUntil(due);
        const auto actual = std::chrono::steady_clock::now();
        pacingErrorSumNs += std::abs(static_cast<double>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(actual - due).count()));
      }

      if (!sendPacket(packet_.data(),
                      kRtpHeaderBytes + kPayloadHeaderBytes +
                          static_cast<std::size_t>(chunkBytes))) {
        return;
      }

      if (sequenceNumber_ == 0xFFFF) {
        ++extendedSequenceHigh_;  // carry into the extended sequence number
      }
      ++sequenceNumber_;
      ++packetIndex;
      offsetBytes += chunkBytes;
    }
  }

  if (packetIndex > 0) {
    pacingErrorMicros_.store(pacingErrorSumNs / static_cast<double>(packetIndex) / 1000.0,
                             std::memory_order_relaxed);
  }
  framesSent_.fetch_add(1, std::memory_order_relaxed);
}

// ═══════════════════════════════════════════════════════════════════════════
// ST 2110-30 (AES67) audio essence
// ═══════════════════════════════════════════════════════════════════════════

namespace {
constexpr std::uint32_t kAudioClockRateHz = 48000;   // RTP clock == sample rate
constexpr int kAudioFramesPerPacket = 48;            // 1 ms at 48 kHz (level A)
constexpr int kAudioBytesPerSample = 3;              // L24
}  // namespace

std::string st2110BuildAudioSdp(const St2110AudioConfig& config,
                                const std::string& sessionName,
                                bool ptpLocked, const std::string& ptpGrandmaster,
                                int ptpDomain) {
  std::ostringstream sdp;
  sdp << "v=0\r\n"
      << "o=- 0 0 IN IP4 "
      << (config.interfaceAddress.empty() ? defaultOriginAddress(config.destinationAddress) : config.interfaceAddress) << "\r\n"
      << "s=" << (sessionName.empty() ? "Deckboy ST 2110-30" : sessionName) << "\r\n"
      << "t=0 0\r\n"
      << "m=audio " << config.destinationPort << " RTP/AVP " << config.payloadType << "\r\n"
      << "c=IN IP4 " << config.destinationAddress << "/" << config.timeToLive << "\r\n"
      << "a=rtpmap:" << config.payloadType << " L24/" << kAudioClockRateHz << "/"
      << config.channels << "\r\n"
      // 1 ms packets; maxptime must not be below ptime or receivers reject it.
      << "a=ptime:1\r\n"
      << "a=maxptime:1\r\n";
  if (ptpLocked && !ptpGrandmaster.empty()) {
    sdp << "a=ts-refclk:ptp=IEEE1588-2008:" << ptpGrandmaster << ":" << ptpDomain << "\r\n"
        << "a=mediaclk:direct=0\r\n";
  } else {
    sdp << "a=ts-refclk:localmac=00-00-00-00-00-00\r\n"
        << "a=mediaclk:sender\r\n";
  }
  return sdp.str();
}

St2110AudioOutput::St2110AudioOutput()
  : socket_(static_cast<long long>(kInvalidSocket)) {
  std::random_device rd;
  ssrc_ = static_cast<std::uint32_t>(rd());
  sequenceNumber_ = static_cast<std::uint16_t>(rd() & 0xFFFF);
}

St2110AudioOutput::~St2110AudioOutput() {
  close();
}

void St2110AudioOutput::setLastError(const std::string& message) {
  std::lock_guard<std::mutex> lock(errorMutex_);
  lastError_ = message;
}

std::string St2110AudioOutput::lastError() const {
  std::lock_guard<std::mutex> lock(errorMutex_);
  return lastError_;
}

bool St2110AudioOutput::open(const St2110AudioConfig& config) {
  close();
  config_ = config;
  config_.channels = std::clamp(config_.channels, 1, 8);

#ifdef _WIN32
  if (!winsockStarted_) {
    WSADATA wsa {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      setLastError("WSAStartup failed");
      return false;
    }
    winsockStarted_ = true;
  }
#endif

  socket_t sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == kInvalidSocket) {
    setLastError("audio socket() failed");
    return false;
  }
  const int ttl = std::clamp(config_.timeToLive, 1, 255);
  ::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,
               reinterpret_cast<const char*>(&ttl), sizeof(ttl));
  if (!config_.interfaceAddress.empty()) {
    in_addr ifAddr {};
    if (::inet_pton(AF_INET, config_.interfaceAddress.c_str(), &ifAddr) == 1) {
      ::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF,
                   reinterpret_cast<const char*>(&ifAddr), sizeof(ifAddr));
    }
  }
  socket_ = static_cast<long long>(sock);
  packet_.assign(kRtpHeaderBytes +
                   static_cast<std::size_t>(kAudioFramesPerPacket) *
                   static_cast<std::size_t>(config_.channels) * kAudioBytesPerSample, 0);
  pending_.clear();
  timestampPrimed_ = false;
  packetsSent_.store(0, std::memory_order_relaxed);
  return true;
}

void St2110AudioOutput::close() {
  if (socket_ != static_cast<long long>(kInvalidSocket)) {
#ifdef _WIN32
    ::closesocket(static_cast<socket_t>(socket_));
#else
    ::close(static_cast<socket_t>(socket_));
#endif
    socket_ = static_cast<long long>(kInvalidSocket);
  }
#ifdef _WIN32
  if (winsockStarted_) {
    WSACleanup();
    winsockStarted_ = false;
  }
#endif
  pending_.clear();
}

bool St2110AudioOutput::sendPacket(const std::uint8_t* data, std::size_t size) {
  sockaddr_in dest {};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(static_cast<std::uint16_t>(config_.destinationPort));
  if (::inet_pton(AF_INET, config_.destinationAddress.c_str(), &dest.sin_addr) != 1) {
    setLastError("invalid audio destination address");
    return false;
  }
  return ::sendto(static_cast<socket_t>(socket_), reinterpret_cast<const char*>(data),
                  static_cast<int>(size), 0,
                  reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) >= 0;
}

void St2110AudioOutput::pushSamples(const std::int16_t* interleaved, std::size_t frameCount) {
  if (!isOpen() || interleaved == nullptr || frameCount == 0) {
    return;
  }
  const int channels = config_.channels;
  // The engine hands out stereo; if the flow declares more channels we would
  // need a real upmix, so refuse rather than emit silence on phantom channels.
  if (channels != 2) {
    return;
  }

  // Prime the RTP timestamp from the media clock once, then advance it by
  // exactly 48 per packet. Re-deriving it per packet from wall time would
  // introduce jitter into a stream whose whole job is to be evenly clocked.
  if (!timestampPrimed_) {
    std::uint64_t clockNanos = 0;
    if (ptp_ != nullptr && ptp_->locked()) {
      clockNanos = ptp_->ptpNanosNow();
    } else {
      clockNanos = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
    }
    const std::uint64_t seconds = clockNanos / 1'000'000'000ull;
    const std::uint64_t rem = clockNanos % 1'000'000'000ull;
    rtpTimestamp_ = static_cast<std::uint32_t>(
      (seconds * kAudioClockRateHz + (rem * kAudioClockRateHz) / 1'000'000'000ull) & 0xFFFFFFFFull);
    timestampPrimed_ = true;
  }

  pending_.insert(pending_.end(), interleaved,
                  interleaved + frameCount * static_cast<std::size_t>(channels));

  const std::size_t samplesPerPacket =
    static_cast<std::size_t>(kAudioFramesPerPacket) * static_cast<std::size_t>(channels);
  std::size_t offset = 0;
  while (pending_.size() - offset >= samplesPerPacket) {
    std::uint8_t* p = packet_.data();
    p[0] = 0x80;
    // No marker on audio; the RTP timestamp carries the timing.
    p[1] = static_cast<std::uint8_t>(config_.payloadType & 0x7F);
    writeBe16(p + 2, sequenceNumber_);
    writeBe32(p + 4, rtpTimestamp_);
    writeBe32(p + 8, ssrc_);

    std::uint8_t* payload = p + kRtpHeaderBytes;
    for (std::size_t i = 0; i < samplesPerPacket; ++i) {
      // int16 -> L24 big-endian. Shifting left by 8 is the correct widening:
      // it preserves full scale, where a plain cast would drop 8 bits of range.
      const std::int32_t wide = static_cast<std::int32_t>(pending_[offset + i]) << 8;
      payload[i * 3 + 0] = static_cast<std::uint8_t>((wide >> 16) & 0xFF);
      payload[i * 3 + 1] = static_cast<std::uint8_t>((wide >> 8) & 0xFF);
      payload[i * 3 + 2] = static_cast<std::uint8_t>(wide & 0xFF);
    }
    if (!sendPacket(packet_.data(),
                    kRtpHeaderBytes + samplesPerPacket * kAudioBytesPerSample)) {
      break;
    }
    ++sequenceNumber_;
    rtpTimestamp_ += kAudioFramesPerPacket;
    packetsSent_.fetch_add(1, std::memory_order_relaxed);
    offset += samplesPerPacket;
  }
  if (offset > 0) {
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(offset));
  }
}

}  // namespace video
}  // namespace platform
}  // namespace deckboy
