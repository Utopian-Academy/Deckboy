// ---------------------------------------------------------------------------
// st2110_output.hpp — SMPTE ST 2110-20 uncompressed video sender.
//
// WHAT THIS IS, PRECISELY
// -----------------------
// A conformant ST 2110-20 RTP packetiser and multicast sender: correct pgroup
// packing, correct payload headers (extended sequence number + Sample Row Data
// descriptors), correct marker-bit framing, and an SDP that describes what it
// emits. A software receiver (ffmpeg, GStreamer, a 2110 analyser) will decode
// this and see the right picture.
//
// WHAT THIS IS NOT — read before promising anything to a venue
// ------------------------------------------------------------
//   * NOT PTP-LOCKED. RTP timestamps come from the system clock, not from an
//     IEEE 1588 grandmaster via ST 2059. Without a shared clock this stream
//     cannot be genlocked to other 2110 essences, so it will not intercut
//     cleanly with real plant sources.
//   * NOT ST 2110-21 NARROW. Packets are paced per frame, not to the narrow
//     traffic model. Meeting the narrow model needs hardware pacing (Rivermax
//     on a ConnectX-class NIC, or DPDK); user-space sockets on Windows cannot
//     do it. Tolerant receivers cope; strict ones may report non-conformance.
//   * VIDEO ONLY. No -30 audio, no -40 ancillary.
//
// Those three gaps are why the UI labels this EXPERIMENTAL. See
// docs/ST2110_FEASIBILITY.md for the full assessment and the phased path to a
// production-grade sender. Do not relabel it without closing them.
//
// The wire format itself is what it is regardless of those gaps, so this code
// is the durable part: adding PTP later changes where the timestamp comes from,
// not how a frame becomes packets.
// ---------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace deckboy {
namespace platform {
namespace video {

class PtpClient;

// Sampling / depth combinations from ST 2110-20 Table 1. Only the two an event
// deck realistically emits are offered; the pgroup maths below is per-format,
// so adding 4:4:4 or 12-bit means adding a case, not reworking the packetiser.
enum class St2110Sampling {
  YCbCr422_10bit,  // pgroup = 2 pixels / 5 bytes  (broadcast default)
  YCbCr422_8bit,   // pgroup = 2 pixels / 4 bytes  (cheaper, still conformant)
};

struct St2110Config {
  bool enabled = false;
  std::string destinationAddress = "239.20.10.1";  // SSM/ASM multicast group
  std::string interfaceAddress;                    // local NIC to send from ("" = default route)
  int destinationPort = 20000;
  int payloadType = 96;                            // dynamic RTP PT
  int width = 1920;
  int height = 1080;
  double frameRate = 60000.0 / 1001.0;             // 59.94
  bool interlaced = false;
  int timeToLive = 8;                              // multicast TTL: stay inside the plant
  St2110Sampling sampling = St2110Sampling::YCbCr422_10bit;
  // ST 2110-20 packets are sized to fit a standard 1500-byte MTU. Raising this
  // requires jumbo frames end to end; getting it wrong fragments every packet
  // and destroys timing.
  int maxPayloadBytes = 1428;
};

// Bytes and pixels per pgroup for a sampling mode. A pgroup is the smallest
// whole number of pixels that packs into a whole number of octets, and every
// RTP payload must contain an integer number of them.
int st2110PgroupBytes(St2110Sampling sampling);
int st2110PgroupPixels(St2110Sampling sampling);

// Human label for settings/status text.
const char* st2110SamplingLabel(St2110Sampling sampling);

// Generate the SDP an ST 2110-20 receiver needs (RFC 4566 + ST 2110-20 fmtp).
// This is the file an operator hands to the receiving device or registers with
// NMOS IS-05.
//
// ts-refclk is written from the ACTUAL clock state: `ptp=IEEE1588-2008:<gm>:<domain>`
// only when genuinely locked to that grandmaster, otherwise `localmac`.
// Advertising a traceable clock we are not locked to is how a stream silently
// fails to genlock in a plant, so this must never be optimistic.
std::string st2110BuildSdp(const St2110Config& config, const std::string& sessionName,
                           bool ptpLocked = false,
                           const std::string& ptpGrandmaster = {},
                           int ptpDomain = 0);

// ---------------------------------------------------------------------------
// Sender. One instance per output. open() binds the socket and pre-sizes the
// packet buffers; sendFrame() packetises and transmits one progressive frame.
//
// sendFrame() takes tightly-packed BGRA (the compositor readback format used by
// the other egress paths) and does the colour conversion internally, so callers
// stay in the same pixel format they already produce for NDI/stream.
// ---------------------------------------------------------------------------
class St2110Output {
 public:
  St2110Output();
  ~St2110Output();

  St2110Output(const St2110Output&) = delete;
  St2110Output& operator=(const St2110Output&) = delete;

  bool open(const St2110Config& config);
  void close();
  bool isOpen() const { return socket_ >= 0; }

  // Optional PTP reference. When set AND locked, RTP timestamps are derived
  // from grandmaster time instead of the local clock — which is the difference
  // between a stream that decodes and a stream that can be cut to. Not owned;
  // must outlive this sender. Safe to leave null.
  void setPtpClient(const PtpClient* ptp) { ptp_ = ptp; }

  // srcBgra must hold width*height*4 bytes. srcStrideBytes allows a padded
  // readback.
  //
  // NON-BLOCKING: this copies the frame and hands it to the sender thread, then
  // returns. It must never pace on the caller's thread — the caller is the
  // output render loop, and holding it for a frame interval would stall the
  // programme output to pace a stream.
  //
  // Latest-frame-wins: if the sender is still working when a new frame arrives,
  // the older pending frame is DROPPED. For live video a fresh frame is always
  // worth more than a stale one, and an unbounded queue would just grow latency
  // until it fell over.
  bool sendFrame(const std::uint8_t* srcBgra, int srcStrideBytes);

  const St2110Config& config() const { return config_; }
  std::string lastError() const;
  std::uint64_t framesSent() const { return framesSent_.load(std::memory_order_relaxed); }
  std::uint64_t framesDropped() const { return framesDropped_.load(std::memory_order_relaxed); }
  // Mean absolute error between when a packet should have gone out and when it
  // did, in microseconds — the number that says whether pacing is holding.
  double pacingErrorMicros() const { return pacingErrorMicros_.load(std::memory_order_relaxed); }

 private:
  // BT.709 studio-swing conversion of one row of BGRA into the 4:2:2 sample
  // order the payload expects (Cb Y Cr Y ...), at the configured bit depth.
  void convertRowToYCbCr422(const std::uint8_t* srcRowBgra, int width,
                            std::vector<std::uint16_t>& out) const;

  bool sendPacket(const std::uint8_t* data, std::size_t size);

  // Packetise and transmit one frame, paced. Runs ONLY on senderThread_.
  void sendFramePaced(const std::vector<std::uint8_t>& bgra, int strideBytes);
  void senderLoop();
  void setLastError(const std::string& message);

  St2110Config config_;
  const PtpClient* ptp_ = nullptr;    // not owned; null = free-running local clock
  long long socket_ = -1;              // SOCKET on Windows, int fd elsewhere
  std::uint16_t sequenceNumber_ = 0;
  std::uint32_t extendedSequenceHigh_ = 0;
  std::uint32_t ssrc_ = 0;
  std::atomic<std::uint64_t> framesSent_ {0};
  std::atomic<std::uint64_t> framesDropped_ {0};
  std::atomic<double> pacingErrorMicros_ {0.0};

  mutable std::mutex errorMutex_;
  std::string lastError_;

  // -- Sender thread -----------------------------------------------------------
  std::thread senderThread_;
  std::atomic<bool> senderStop_ {false};
  std::mutex frameMutex_;
  std::condition_variable frameCv_;
  std::vector<std::uint8_t> pendingFrame_;   // BGRA, owned by the mutex
  int pendingStride_ = 0;
  bool hasPending_ = false;

  std::vector<std::uint8_t> packet_;      // scratch: one RTP datagram (sender thread)
  std::vector<std::uint16_t> rowSamples_; // scratch: one row of 4:2:2 (sender thread)
  bool winsockStarted_ = false;
};

// ---------------------------------------------------------------------------
// St2110AudioOutput — ST 2110-30 (AES67) PCM audio essence.
//
// A 2110 video flow with no audio is half a feed, and audio is the cheap half:
// 2 channels of 24-bit 48 kHz is ~2.3 Mb/s, against ~1.2 Gb/s for the video.
//
// Conformance level A: 48 kHz, 1 ms packet time (48 frames per packet), up to 8
// channels, L24 payload. The RTP clock is the SAMPLE RATE (48 kHz), not 90 kHz —
// video and audio essences carry different clock rates and are aligned by both
// being derived from the same PTP grandmaster.
//
// Fed from MediaEngine's existing audio tap, which delivers the final,
// post-delay, post-limiter stereo the room actually hears — so the stream
// carries exactly what the PA does.
// ---------------------------------------------------------------------------
struct St2110AudioConfig {
  bool enabled = false;
  std::string destinationAddress = "239.20.10.1";
  std::string interfaceAddress;
  // Convention: audio sits two ports above the video essence of the same flow.
  int destinationPort = 20002;
  int payloadType = 97;
  int channels = 2;
  int timeToLive = 8;
};

class St2110AudioOutput {
 public:
  St2110AudioOutput();
  ~St2110AudioOutput();

  St2110AudioOutput(const St2110AudioOutput&) = delete;
  St2110AudioOutput& operator=(const St2110AudioOutput&) = delete;

  bool open(const St2110AudioConfig& config);
  void close();
  bool isOpen() const { return socket_ >= 0; }
  void setPtpClient(const PtpClient* ptp) { ptp_ = ptp; }

  // Interleaved stereo int16 at 48 kHz — exactly what the engine's audio tap
  // hands out. Called from the AUDIO THREAD: it must not block, so this only
  // buffers whole 1 ms packets and sends them straight out (they are ~150 bytes
  // each; pacing them further would cost more than it saves).
  void pushSamples(const std::int16_t* interleaved, std::size_t frameCount);

  std::uint64_t packetsSent() const { return packetsSent_.load(std::memory_order_relaxed); }
  const St2110AudioConfig& config() const { return config_; }
  std::string lastError() const;

 private:
  bool sendPacket(const std::uint8_t* data, std::size_t size);
  void setLastError(const std::string& message);

  St2110AudioConfig config_;
  const PtpClient* ptp_ = nullptr;
  long long socket_ = -1;
  std::uint16_t sequenceNumber_ = 0;
  std::uint32_t ssrc_ = 0;
  std::uint32_t rtpTimestamp_ = 0;
  bool timestampPrimed_ = false;
  std::atomic<std::uint64_t> packetsSent_ {0};
  std::vector<std::int16_t> pending_;   // partial packet carried between calls
  std::vector<std::uint8_t> packet_;
  mutable std::mutex errorMutex_;
  std::string lastError_;
  bool winsockStarted_ = false;
};

// SDP for the audio essence. Separate from the video SDP because they are
// separate flows with separate ports and clock rates; a receiver joins each.
std::string st2110BuildAudioSdp(const St2110AudioConfig& config,
                                const std::string& sessionName,
                                bool ptpLocked = false,
                                const std::string& ptpGrandmaster = {},
                                int ptpDomain = 0);

}  // namespace video
}  // namespace platform
}  // namespace deckboy
