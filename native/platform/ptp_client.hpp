// ---------------------------------------------------------------------------
// ptp_client.hpp — IEEE 1588-2008 (PTPv2) ordinary-clock SLAVE, for SMPTE
// ST 2059 media clock alignment.
//
// WHY THIS EXISTS
// ---------------
// An ST 2110 sender whose RTP timestamps come from its own free-running clock
// cannot genlock to anything. Every essence in a 2110 plant derives its media
// clock from a shared PTP grandmaster; without that, a stream decodes fine in
// software and is useless on a real switcher. This is the piece that turns
// "correctly formatted packets" into "a source you can cut to".
//
// WHAT IT DOES
// ------------
// Listens on the PTP multicast groups, follows the grandmaster announced on
// the configured domain, and maintains an offset between the local clock and
// PTP time using the standard two-way exchange:
//
//   T1  Sync origin        (from Follow_Up, or Sync itself if one-step)
//   T2  Sync arrival       (local)
//   T3  Delay_Req sent     (local)
//   T4  Delay_Req arrival  (from Delay_Resp)
//
//   meanPathDelay = ((T2 - T1) + (T4 - T3)) / 2
//   offset        = (T2 - T1) - meanPathDelay
//
// HONEST LIMITS — read before trusting this in a plant
// ----------------------------------------------------
//   * SOFTWARE TIMESTAMPING ONLY. T2/T3 are taken in user space when the
//     datagram is read/written, so they carry OS scheduling jitter. Expect tens
//     of microseconds, not the sub-microsecond a hardware-timestamping NIC
//     gives. Good enough to be genuinely locked and to declare a traceable
//     clock; NOT good enough to claim broadcast-grade accuracy.
//   * No BMCA. It follows the first grandmaster it hears on the domain (and
//     re-follows if that changes), rather than running the full Best Master
//     Clock Algorithm. Correct for a slave-only device on a sane network.
//   * Slave only. Deckboy never advertises itself as a master.
//
// The offset is smoothed and only reported as locked once it is consistent, so
// a half-synced clock never quietly claims to be right.
// ---------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace deckboy {
namespace platform {
namespace video {

struct PtpConfig {
  int domain = 0;                  // ST 2059 commonly uses 127; 0 is the PTP default
  std::string interfaceAddress;    // local NIC to join on ("" = default route)
};

class PtpClient {
 public:
  PtpClient();
  ~PtpClient();

  PtpClient(const PtpClient&) = delete;
  PtpClient& operator=(const PtpClient&) = delete;

  bool start(const PtpConfig& config);
  void stop();
  bool running() const { return running_.load(std::memory_order_relaxed); }

  // True once a grandmaster has been followed and the offset has settled.
  // Until then callers MUST fall back to the local clock and must not claim a
  // traceable reference in an SDP.
  bool locked() const { return locked_.load(std::memory_order_relaxed); }

  // PTP (TAI) nanoseconds since the PTP epoch, right now. Falls back to the
  // local system clock when unlocked, so callers always get a usable value —
  // check locked() to know which you got.
  std::uint64_t ptpNanosNow() const;

  std::int64_t offsetNanos() const { return offsetNanos_.load(std::memory_order_relaxed); }
  double meanPathDelayMicros() const {
    return static_cast<double>(pathDelayNanos_.load(std::memory_order_relaxed)) / 1000.0;
  }
  // "001b19.fffe.000000" style, for the SDP's ts-refclk line.
  std::string grandmasterIdentity() const;
  int domain() const { return config_.domain; }
  std::string lastError() const;

 private:
  void listenLoop();
  void handleEventPacket(const std::uint8_t* data, std::size_t size, std::uint64_t arrivalNanos);
  void handleGeneralPacket(const std::uint8_t* data, std::size_t size);
  void sendDelayRequest();
  void setLastError(const std::string& message);

  PtpConfig config_;
  std::thread thread_;
  std::atomic<bool> stop_ {false};
  std::atomic<bool> running_ {false};
  std::atomic<bool> locked_ {false};
  std::atomic<std::int64_t> offsetNanos_ {0};
  std::atomic<std::int64_t> pathDelayNanos_ {0};

  long long eventSocket_ = -1;     // port 319: Sync, Delay_Req
  long long generalSocket_ = -1;   // port 320: Follow_Up, Announce, Delay_Resp
  bool winsockStarted_ = false;

  mutable std::mutex stateMutex_;
  std::string grandmasterIdentity_;
  std::string lastError_;

  // Exchange state, touched only on the listen thread.
  std::uint64_t syncArrivalNanos_ = 0;     // T2
  std::uint16_t pendingSyncSequence_ = 0;
  bool awaitingFollowUp_ = false;
  std::uint64_t delayReqSentNanos_ = 0;    // T3
  std::uint16_t delayReqSequence_ = 0;
  bool awaitingDelayResp_ = false;
  std::uint64_t lastDelayReqAtNanos_ = 0;
  std::uint8_t localClockIdentity_[8] {};
  int consecutiveGoodOffsets_ = 0;
  std::int64_t smoothedOffsetNanos_ = 0;
  bool haveOffset_ = false;
};

}  // namespace video
}  // namespace platform
}  // namespace deckboy
