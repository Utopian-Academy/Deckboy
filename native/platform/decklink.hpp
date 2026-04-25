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
