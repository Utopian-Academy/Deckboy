// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// asio_audio.hpp — Steinberg ASIO device discovery and capability query.
//
// ASIO is how Windows does professional multichannel audio: the driver is
// supplied by the interface vendor, talks to the hardware directly, and gives
// far lower latency and far higher channel counts than the shared mixer. It is
// also how Dante reaches an application, via Dante Virtual Soundcard's ASIO
// driver.
//
// The SDK is used under the GPL v3 half of its dual licence; see
// native/third_party/asio/README.md.
//
// Everything here degrades to "no devices" when ASIO is not compiled in or no
// driver is installed. A machine with no ASIO interface is the NORMAL case, not
// an error, and must never produce a warning an operator has to dismiss.
// ============================================================================

#ifndef DECKBOY_PLATFORM_ASIO_AUDIO_HPP
#define DECKBOY_PLATFORM_ASIO_AUDIO_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace deckboy {
namespace platform {
namespace audio {

// One installed ASIO driver, as reported by the registry plus (when probed) the
// driver itself. Channel counts and buffer size are only meaningful once
// `probed` is true, because obtaining them requires LOADING the driver, which
// can take hundreds of milliseconds and may pop the vendor's own dialog.
struct AsioDeviceInfo {
  std::string name;             // driver name as the vendor registered it
  int inputChannels = 0;
  int outputChannels = 0;
  int preferredBufferFrames = 0;
  double sampleRate = 0.0;
  int outputLatencyFrames = 0;  // driver-reported output latency
  bool probed = false;          // false = registry entry only, counts unknown
  std::string error;            // why a probe failed, for the operator
};

// True when ASIO support was compiled in at all.
bool asioSupportCompiled();

// Installed drivers, from the registry. Cheap: does NOT load any driver, so it
// is safe to call while a show is running.
std::vector<AsioDeviceInfo> listAsioDevices();

// Load one driver and fill in its capabilities. EXPENSIVE and intrusive: some
// drivers show their own control panel or seize the device on load, so this is
// only called when the operator asks for it, never on a timer or at boot.
bool probeAsioDevice(const std::string& name, AsioDeviceInfo& out);

// ---------------------------------------------------------------------------
// AsioOutput — a running ASIO output stream.
//
// Deckboy's engine produces INTERLEAVED s16 at 48kHz; ASIO wants per-channel,
// non-interleaved buffers in whatever sample type the driver chose, filled from
// a callback on the driver's own real-time thread. This class is the adapter:
// push interleaved s16 with write(), and the callback de-interleaves and
// converts.
//
// The callback runs on a driver thread with hard deadlines. It therefore never
// allocates, never locks, and never blocks -- it drains a ring buffer and, if
// there is not enough, outputs silence and counts the underrun. An audio
// callback that waits is an audio callback that glitches.
//
// ASIO is a process-wide SINGLETON, so exactly one AsioOutput may be open at a
// time; opening a second returns an error rather than corrupting the first.
// ---------------------------------------------------------------------------
class AsioOutput {
 public:
  AsioOutput() = default;
  ~AsioOutput();
  AsioOutput(const AsioOutput&) = delete;
  AsioOutput& operator=(const AsioOutput&) = delete;

  // Open `channels` outputs on `driverName` and start the stream.
  //
  // `sourceRate` is the rate the CALLER produces, not a wish. The driver picks
  // its own rate -- one slaved to external word clock will refuse to change --
  // so when the two differ this class resamples between them rather than
  // refusing to open. Refusing would be safe but wrong: an interface clocked
  // to 44.1 or 96 for the rest of the rig is a normal setup, not an error.
  bool open(const std::string& driverName, int channels, double sourceRate,
            std::string& error);
  void close();

  bool running() const;
  int channels() const;
  double sampleRate() const;
  int bufferFrames() const;

  // Queue interleaved s16 frames (frames = samples per channel). Returns the
  // number of FRAMES accepted; a short return means the ring is full and the
  // caller should try again rather than spin.
  std::size_t write(const std::int16_t* interleaved, std::size_t frames);

  // True when the device runs at a different rate from the source and audio is
  // being converted on the way through.
  bool resampling() const;

  // Frames currently buffered and not yet played, expressed in SOURCE frames
  // so callers pacing against it do not have to know about the conversion.
  // The backpressure signal.
  std::size_t queuedFrames() const;

  // Output latency in FRAMES, as the driver reports it: the delay between
  // handing a sample to the ring and it leaving the socket. Deckboy slaves
  // video to the audio clock, so a switch from SDL to ASIO changes this
  // number and lip sync drifts by the difference unless it is accounted for.
  // Reported rather than silently assumed.
  int outputLatencyFrames() const;
  double outputLatencySeconds() const;

  // Times the callback ran short of data since open(). Non-zero means audible
  // glitching, so it is surfaced rather than hidden.
  std::uint64_t underruns() const;
};

}  // namespace audio
}  // namespace platform
}  // namespace deckboy

#endif  // DECKBOY_PLATFORM_ASIO_AUDIO_HPP
