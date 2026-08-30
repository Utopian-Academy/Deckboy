// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// decklink.hpp — Blackmagic DeckLink SDI/HDMI output interface.
//
// DeckLink cards are professional video I/O devices used in broadcast
// and live events. This file provides:
//   - DeckLinkMode enum: all supported output formats (1080i/p, 720p, 4K)
//   - DeckLinkDeviceInfo: device discovery with capability flags
//   - DeckLinkOutput: RAII class for sending BGRA video + PCM audio frames
//
// Supported modes range from 720p50 to UHD 2160p60. Each mode specifies
// resolution, progressive/interlaced, and frame rate. Helper functions
// provide labels, tokens (for serialization), dimensions, and frame rates.
//
// The implementation (decklink.cpp) uses the Blackmagic DeckLink SDK
// COM interface on Windows and the DeckLink API on Linux/macOS. The
// pimpl pattern keeps SDK headers out of consumer code.
//
// Used by: output_backend.cpp (routes egress frames to the DeckLink device).
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace deckboy::platform::video {

// All supported DeckLink output modes. Token names follow the pattern:
//   Resolution + Scan + FrameRate (e.g. HD1080p5994 = 1920x1080 progressive 59.94fps)
enum class DeckLinkMode {
  HD1080i50,
  HD1080i5994,
  HD1080i60,
  HD1080p2398,
  HD1080p24,
  HD1080p25,
  HD1080p2997,
  HD1080p30,
  HD1080p50,
  HD1080p5994,
  HD1080p60,
  HD720p50,
  HD720p5994,
  HD720p60,
  UHD2160p2398,
  UHD2160p24,
  UHD2160p25,
  UHD2160p2997,
  UHD2160p30,
  UHD2160p50,
  UHD2160p5994,
  UHD2160p60,
};

struct DeckLinkDeviceInfo {
  int id = -1;
  std::string displayName;
  std::string modelName;
  bool supportsInput = false;
  bool supportsOutput = false;
  bool supportsSDI = false;
  bool supportsHDMI = false;
  bool supports10Bit = false;
  bool supports4K = false;
};

// Returns the display label for a mode (e.g. "1080p 59.94")
std::string deckLinkModeLabel(DeckLinkMode mode);

// Parse a mode token from persistence (e.g. "1080p5994")
DeckLinkMode parseDeckLinkMode(const std::string& token);

// Return the mode token for persistence
std::string deckLinkModeToken(DeckLinkMode mode);

// Width/height for a given mode
int deckLinkModeWidth(DeckLinkMode mode);
int deckLinkModeHeight(DeckLinkMode mode);

// Frame rate numerator/denominator for a given mode
void deckLinkModeFrameRate(DeckLinkMode mode, int& numerator, int& denominator);

// Live capture from a DeckLink / UltraStudio input.
//
// The counterpart of DeckLinkOutput, on the same devices and the same modes.
// A card that can do both can run both at once, which is the ordinary
// arrangement: play out of one connector while taking a camera in on another.
//
// MODE DETECTION IS AUTOMATIC where the hardware supports it. An operator
// plugging a camera in does not know, and should not have to know, whether it
// is arriving as 1080i59.94 or 1080p29.97 -- and getting it wrong produces a
// black frame rather than an error. A card without format detection falls back
// to the mode it is asked for.
class DeckLinkInput {
 public:
  // One captured frame. BGRA, top-down, tightly packed unless stride says
  // otherwise. Delivered on the SDK's own thread: the callback must be quick
  // and must not touch the renderer.
  using FrameCallback = std::function<void(const std::uint8_t* bgra, int width,
                                           int height, int stride)>;
  // Interleaved 16-bit PCM at 48kHz, which is the only rate the SDK captures.
  using AudioCallback = std::function<void(const std::int16_t* samples,
                                           int sampleCount, int channels)>;

  DeckLinkInput();
  ~DeckLinkInput();

  DeckLinkInput(const DeckLinkInput&) = delete;
  DeckLinkInput& operator=(const DeckLinkInput&) = delete;

  // Devices that can capture. A subset of listDevices(): plenty of cards are
  // playout only, and offering those as sources is a dead control.
  static std::vector<DeckLinkDeviceInfo> listInputDevices();

  // mode is the starting guess; with detectFormat the card corrects it as soon
  // as it sees signal. audioChannels of 0 captures no audio.
  bool start(int deviceId, DeckLinkMode mode, bool detectFormat = true,
             int audioChannels = 2);
  bool isRunning() const;
  void stop();

  void onFrame(FrameCallback callback);
  void onAudio(AudioCallback callback);

  // What the card is actually receiving, which after format detection is not
  // necessarily what start() was asked for. Zero width means no signal yet.
  int detectedWidth() const;
  int detectedHeight() const;
  double detectedFps() const;
  // True once a frame has arrived. An input with a cable but no source stays
  // false, which is the difference the operator needs to see.
  bool hasSignal() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class DeckLinkOutput {
 public:
  DeckLinkOutput();
  ~DeckLinkOutput();

  DeckLinkOutput(const DeckLinkOutput&) = delete;
  DeckLinkOutput& operator=(const DeckLinkOutput&) = delete;

  static std::vector<DeckLinkDeviceInfo> listDevices();

  bool init(int deviceId, DeckLinkMode mode, bool enable10Bit = true);
  bool isInitialized() const;
  void shutdown();

  // Send a BGRA32 pixel buffer (same format as egress capture pipeline).
  // stride = width * 4. Returns false on failure.
  bool sendFrame(const std::uint8_t* pixels, int width, int height, int stride);

  // Send interleaved 16-bit PCM audio samples.
  bool sendAudio(const std::int16_t* samples, int sampleCount, int sampleRate, int channels);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace deckboy::platform::video
