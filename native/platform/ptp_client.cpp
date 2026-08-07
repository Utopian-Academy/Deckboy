// ---------------------------------------------------------------------------
// ptp_client.cpp — PTPv2 slave. See ptp_client.hpp for scope and honest limits.
//
// Wire layout used below (IEEE 1588-2008 §13):
//
//   Common header, 34 bytes
//     [0]      transportSpecific<<4 | messageType
//     [1]      reserved<<4 | versionPTP (2)
//     [2..3]   messageLength
//     [4]      domainNumber
//     [6..7]   flagField          (bit 1 of [6] = twoStep)
//     [8..15]  correctionField    (nanoseconds << 16)
//     [20..29] sourcePortIdentity (clockIdentity[8] + portNumber[2])
//     [30..31] sequenceId
//     [32]     controlField
//     [33]     logMessageInterval
//
//   Timestamp, 10 bytes: secondsField[6] + nanosecondsField[4]
//
//   Sync/Follow_Up/Delay_Req: header + timestamp at [34]
//   Delay_Resp:               header + receiveTimestamp[34] + requestingPortIdentity[44]
//   Announce:                 grandmasterIdentity at [53]
// ---------------------------------------------------------------------------
#include "ptp_client.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

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
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

namespace deckboy {
namespace platform {
namespace video {

namespace {

constexpr const char* kPtpMulticastGroup = "224.0.1.129";  // AVB/default PTP group
constexpr int kEventPort = 319;
constexpr int kGeneralPort = 320;
constexpr std::size_t kHeaderBytes = 34;

constexpr int kMsgSync = 0x0;
constexpr int kMsgDelayReq = 0x1;
constexpr int kMsgFollowUp = 0x8;
constexpr int kMsgDelayResp = 0x9;
constexpr int kMsgAnnounce = 0xB;

// Delay requests are rate-limited; once a second is ample for a media clock and
// keeps us from adding traffic to a plant network.
constexpr std::uint64_t kDelayReqIntervalNanos = 1'000'000'000ull;
// An offset this large means we are not really following anything sensible.
constexpr std::int64_t kSaneOffsetNanos = 5'000'000'000ll;   // 5 s
// Consecutive consistent measurements before we admit to being locked.
constexpr int kLockThreshold = 4;

std::uint64_t nowSystemNanos() {
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}

std::uint16_t readBe16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

void writeBe16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>(v >> 8);
  p[1] = static_cast<std::uint8_t>(v & 0xFF);
}

// PTP timestamp -> nanoseconds since the PTP epoch.
std::uint64_t readTimestamp(const std::uint8_t* p) {
  std::uint64_t seconds = 0;
  for (int i = 0; i < 6; ++i) {
    seconds = (seconds << 8) | p[i];
  }
  const std::uint32_t nanos =
    (static_cast<std::uint32_t>(p[6]) << 24) | (static_cast<std::uint32_t>(p[7]) << 16) |
    (static_cast<std::uint32_t>(p[8]) << 8) | static_cast<std::uint32_t>(p[9]);
  return seconds * 1'000'000'000ull + nanos;
}

void writeTimestamp(std::uint8_t* p, std::uint64_t nanos) {
  const std::uint64_t seconds = nanos / 1'000'000'000ull;
  const std::uint32_t rem = static_cast<std::uint32_t>(nanos % 1'000'000'000ull);
  for (int i = 5; i >= 0; --i) {
    p[i] = static_cast<std::uint8_t>((seconds >> ((5 - i) * 8)) & 0xFF);
  }
  p[6] = static_cast<std::uint8_t>((rem >> 24) & 0xFF);
  p[7] = static_cast<std::uint8_t>((rem >> 16) & 0xFF);
  p[8] = static_cast<std::uint8_t>((rem >> 8) & 0xFF);
  p[9] = static_cast<std::uint8_t>(rem & 0xFF);
}

// correctionField is a 64-bit signed value scaled by 2^16 nanoseconds.
std::int64_t readCorrectionNanos(const std::uint8_t* p) {
  std::int64_t raw = 0;
  for (int i = 0; i < 8; ++i) {
    raw = (raw << 8) | p[i];
  }
  return raw >> 16;
}

std::string formatClockIdentity(const std::uint8_t* id) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02X%02X%02X.%02X%02X.%02X%02X%02X",
                id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
  return std::string(buf);
}

bool joinMulticast(socket_t sock, const std::string& interfaceAddress) {
  ip_mreq mreq {};
  if (::inet_pton(AF_INET, kPtpMulticastGroup, &mreq.imr_multiaddr) != 1) {
    return false;
  }
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (!interfaceAddress.empty()) {
    in_addr ifAddr {};
    if (::inet_pton(AF_INET, interfaceAddress.c_str(), &ifAddr) == 1) {
      mreq.imr_interface = ifAddr;
    }
  }
  return ::setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                      reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == 0;
}

socket_t openPtpSocket(int port, const std::string& interfaceAddress) {
  socket_t sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == kInvalidSocket) {
    return kInvalidSocket;
  }
  const int reuse = 1;
  ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));
  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<std::uint16_t>(port));
  if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
    ::closesocket(sock);
#else
    ::close(sock);
#endif
    return kInvalidSocket;
  }
  if (!joinMulticast(sock, interfaceAddress)) {
#ifdef _WIN32
    ::closesocket(sock);
#else
    ::close(sock);
#endif
    return kInvalidSocket;
  }
  return sock;
}

}  // namespace

PtpClient::PtpClient()
  : eventSocket_(static_cast<long long>(kInvalidSocket)),
    generalSocket_(static_cast<long long>(kInvalidSocket)) {
  // A locally-unique clock identity for our Delay_Req source port. Random is
  // fine — we are slave-only and never participate in master election.
  std::random_device rd;
  for (int i = 0; i < 8; ++i) {
    localClockIdentity_[i] = static_cast<std::uint8_t>(rd() & 0xFF);
  }
  localClockIdentity_[3] = 0xFF;  // the conventional EUI-64 padding
  localClockIdentity_[4] = 0xFE;
}

PtpClient::~PtpClient() {
  stop();
}

void PtpClient::setLastError(const std::string& message) {
  std::lock_guard<std::mutex> lock(stateMutex_);
  lastError_ = message;
}

std::string PtpClient::lastError() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return lastError_;
}

std::string PtpClient::grandmasterIdentity() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return grandmasterIdentity_;
}

std::uint64_t PtpClient::ptpNanosNow() const {
  const std::uint64_t local = nowSystemNanos();
  const std::int64_t offset = offsetNanos_.load(std::memory_order_relaxed);
  if (!locked_.load(std::memory_order_relaxed)) {
    return local;  // caller must check locked() before claiming traceability
  }
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(local) + offset);
}

bool PtpClient::start(const PtpConfig& config) {
  stop();
  config_ = config;

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

  socket_t ev = openPtpSocket(kEventPort, config_.interfaceAddress);
  if (ev == kInvalidSocket) {
    // Port 319/320 are commonly already held by a system PTP service; say so
    // rather than failing silently, because that is the usual cause.
    setLastError("could not bind PTP port 319 (in use, or no multicast route)");
    return false;
  }
  socket_t gen = openPtpSocket(kGeneralPort, config_.interfaceAddress);
  if (gen == kInvalidSocket) {
#ifdef _WIN32
    ::closesocket(ev);
#else
    ::close(ev);
#endif
    setLastError("could not bind PTP port 320");
    return false;
  }

  eventSocket_ = static_cast<long long>(ev);
  generalSocket_ = static_cast<long long>(gen);
  stop_.store(false);
  locked_.store(false);
  haveOffset_ = false;
  consecutiveGoodOffsets_ = 0;
  running_.store(true);
  thread_ = std::thread([this] { listenLoop(); });
  return true;
}

void PtpClient::stop() {
  if (thread_.joinable()) {
    stop_.store(true);
    thread_.join();
  }
  stop_.store(false);
  running_.store(false);
  locked_.store(false);
  auto closeSock = [](long long& s) {
    if (s != static_cast<long long>(kInvalidSocket)) {
#ifdef _WIN32
      ::closesocket(static_cast<socket_t>(s));
#else
      ::close(static_cast<socket_t>(s));
#endif
      s = static_cast<long long>(kInvalidSocket);
    }
  };
  closeSock(eventSocket_);
  closeSock(generalSocket_);
#ifdef _WIN32
  if (winsockStarted_) {
    WSACleanup();
    winsockStarted_ = false;
  }
#endif
}

void PtpClient::listenLoop() {
  std::vector<std::uint8_t> buffer(1500);
  while (!stop_.load()) {
    fd_set readSet;
    FD_ZERO(&readSet);
    const socket_t ev = static_cast<socket_t>(eventSocket_);
    const socket_t gen = static_cast<socket_t>(generalSocket_);
    FD_SET(ev, &readSet);
    FD_SET(gen, &readSet);
    timeval tv {};
    tv.tv_sec = 0;
    tv.tv_usec = 200000;  // 200 ms, so stop() is responsive
#ifdef _WIN32
    const int maxFd = 0;
#else
    const int maxFd = static_cast<int>(std::max(ev, gen)) + 1;
#endif
    const int ready = ::select(maxFd, &readSet, nullptr, nullptr, &tv);
    if (ready < 0) {
      break;
    }
    if (ready > 0 && FD_ISSET(ev, &readSet)) {
      const int n = ::recv(ev, reinterpret_cast<char*>(buffer.data()),
                           static_cast<int>(buffer.size()), 0);
      // Timestamp as soon after the read as possible — every instruction
      // between arrival and here is error in T2.
      const std::uint64_t arrival = nowSystemNanos();
      if (n > 0) {
        handleEventPacket(buffer.data(), static_cast<std::size_t>(n), arrival);
      }
    }
    if (ready > 0 && FD_ISSET(gen, &readSet)) {
      const int n = ::recv(gen, reinterpret_cast<char*>(buffer.data()),
                           static_cast<int>(buffer.size()), 0);
      if (n > 0) {
        handleGeneralPacket(buffer.data(), static_cast<std::size_t>(n));
      }
    }

    // Keep the path-delay estimate fresh.
    const std::uint64_t now = nowSystemNanos();
    if (haveOffset_ && !awaitingDelayResp_ &&
        (lastDelayReqAtNanos_ == 0 || now - lastDelayReqAtNanos_ >= kDelayReqIntervalNanos)) {
      sendDelayRequest();
    }
  }
}

void PtpClient::handleEventPacket(const std::uint8_t* data, std::size_t size,
                                  std::uint64_t arrivalNanos) {
  if (size < kHeaderBytes + 10) {
    return;
  }
  if ((data[1] & 0x0F) != 2 || data[4] != static_cast<std::uint8_t>(config_.domain)) {
    return;  // not PTPv2, or a different domain
  }
  const int messageType = data[0] & 0x0F;
  if (messageType != kMsgSync) {
    return;
  }
  const bool twoStep = (data[6] & 0x02) != 0;
  syncArrivalNanos_ = arrivalNanos;                 // T2
  pendingSyncSequence_ = readBe16(data + 30);
  if (twoStep) {
    awaitingFollowUp_ = true;                       // T1 arrives in the Follow_Up
    return;
  }
  // One-step: the origin timestamp is in the Sync itself.
  const std::uint64_t t1 = readTimestamp(data + kHeaderBytes) + readCorrectionNanos(data + 8);
  const std::int64_t rawOffset =
    static_cast<std::int64_t>(syncArrivalNanos_) - static_cast<std::int64_t>(t1);
  const std::int64_t pathDelay = pathDelayNanos_.load(std::memory_order_relaxed);
  const std::int64_t offset = rawOffset - pathDelay;
  if (std::llabs(offset) > kSaneOffsetNanos && locked_.load()) {
    return;
  }
  // The offset we publish is PTP-minus-local, i.e. what to ADD to local time.
  const std::int64_t publish = -offset;
  smoothedOffsetNanos_ = haveOffset_
    ? static_cast<std::int64_t>(smoothedOffsetNanos_ * 0.875 + publish * 0.125)
    : publish;
  haveOffset_ = true;
  offsetNanos_.store(smoothedOffsetNanos_, std::memory_order_relaxed);
  if (++consecutiveGoodOffsets_ >= kLockThreshold) {
    locked_.store(true, std::memory_order_relaxed);
  }
}

void PtpClient::handleGeneralPacket(const std::uint8_t* data, std::size_t size) {
  if (size < kHeaderBytes) {
    return;
  }
  if ((data[1] & 0x0F) != 2 || data[4] != static_cast<std::uint8_t>(config_.domain)) {
    return;
  }
  const int messageType = data[0] & 0x0F;

  if (messageType == kMsgAnnounce && size >= 61) {
    const std::string gm = formatClockIdentity(data + 53);
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (grandmasterIdentity_ != gm) {
      grandmasterIdentity_ = gm;
    }
    return;
  }

  if (messageType == kMsgFollowUp && size >= kHeaderBytes + 10) {
    if (!awaitingFollowUp_ || readBe16(data + 30) != pendingSyncSequence_) {
      return;  // not the Follow_Up for the Sync we are holding
    }
    awaitingFollowUp_ = false;
    const std::uint64_t t1 =
      readTimestamp(data + kHeaderBytes) + readCorrectionNanos(data + 8);
    const std::int64_t rawOffset =
      static_cast<std::int64_t>(syncArrivalNanos_) - static_cast<std::int64_t>(t1);
    const std::int64_t pathDelay = pathDelayNanos_.load(std::memory_order_relaxed);
    const std::int64_t offset = rawOffset - pathDelay;
    if (std::llabs(offset) > kSaneOffsetNanos && locked_.load()) {
      consecutiveGoodOffsets_ = 0;
      locked_.store(false, std::memory_order_relaxed);
      return;
    }
    const std::int64_t publish = -offset;
    // Gentle IIR: a media clock wants to be steady, and a single jittery
    // user-space timestamp should not yank it.
    smoothedOffsetNanos_ = haveOffset_
      ? static_cast<std::int64_t>(smoothedOffsetNanos_ * 0.875 + publish * 0.125)
      : publish;
    haveOffset_ = true;
    offsetNanos_.store(smoothedOffsetNanos_, std::memory_order_relaxed);
    if (++consecutiveGoodOffsets_ >= kLockThreshold) {
      locked_.store(true, std::memory_order_relaxed);
    }
    return;
  }

  if (messageType == kMsgDelayResp && size >= kHeaderBytes + 20) {
    if (!awaitingDelayResp_ || readBe16(data + 30) != delayReqSequence_) {
      return;
    }
    // Only ours: the requesting port identity must match our clock identity.
    if (std::memcmp(data + kHeaderBytes + 10, localClockIdentity_, 8) != 0) {
      return;
    }
    awaitingDelayResp_ = false;
    const std::uint64_t t4 =
      readTimestamp(data + kHeaderBytes) - readCorrectionNanos(data + 8);
    const std::int64_t reverse =
      static_cast<std::int64_t>(t4) - static_cast<std::int64_t>(delayReqSentNanos_);
    // meanPathDelay = ((T2-T1) + (T4-T3)) / 2. (T2-T1) is the current raw
    // forward measurement, recoverable from the published offset.
    const std::int64_t forward = -offsetNanos_.load(std::memory_order_relaxed)
                               + pathDelayNanos_.load(std::memory_order_relaxed);
    std::int64_t delay = (forward + reverse) / 2;
    if (delay < 0) {
      delay = 0;  // asymmetric or jittered measurement; never go negative
    }
    pathDelayNanos_.store(delay, std::memory_order_relaxed);
  }
}

void PtpClient::sendDelayRequest() {
  const socket_t ev = static_cast<socket_t>(eventSocket_);
  if (ev == kInvalidSocket) {
    return;
  }
  std::uint8_t msg[kHeaderBytes + 10] {};
  msg[0] = static_cast<std::uint8_t>(kMsgDelayReq);   // transportSpecific 0
  msg[1] = 0x02;                                      // PTPv2
  writeBe16(msg + 2, static_cast<std::uint16_t>(sizeof(msg)));
  msg[4] = static_cast<std::uint8_t>(config_.domain);
  std::memcpy(msg + 20, localClockIdentity_, 8);
  writeBe16(msg + 28, 1);                             // portNumber
  ++delayReqSequence_;
  writeBe16(msg + 30, delayReqSequence_);
  msg[32] = 0x01;                                     // controlField: Delay_Req
  msg[33] = 0x7F;                                     // logMessageInterval

  sockaddr_in dest {};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(static_cast<std::uint16_t>(kEventPort));
  if (::inet_pton(AF_INET, kPtpMulticastGroup, &dest.sin_addr) != 1) {
    return;
  }
  // T3 is taken as close to the write as possible, for the same reason as T2.
  delayReqSentNanos_ = nowSystemNanos();
  writeTimestamp(msg + kHeaderBytes, delayReqSentNanos_);
  const int sent = ::sendto(ev, reinterpret_cast<const char*>(msg), sizeof(msg), 0,
                            reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
  if (sent > 0) {
    awaitingDelayResp_ = true;
    lastDelayReqAtNanos_ = delayReqSentNanos_;
  }
}

}  // namespace video
}  // namespace platform
}  // namespace deckboy
