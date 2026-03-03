// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Playboy Contributors
// This file is part of Playboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

namespace playboy::platform::video {

// DeckLink output modes
enum class DeckLinkMode {
  HD1080p50,
  HD1080p5994,
  HD1080p60,
  HD720p50,
  HD720p5994,
  HD720p60,
  UHD4K50,
  UHD4K5994,
  UHD4K60,
  _12G_SDI_50,
  _12G_SDI_5994,
  _12G_SDI_60,
};

// DeckLink device info
struct DeckLinkDeviceInfo {
  int id = -1;
  std::string displayName;
  std::string modelName;
  bool supportsSDI = false;
  bool supportsHDMI = false;
  bool supports10Bit = false;
};

// DeckLink output driver
class DeckLinkOutput {
 public:
  DeckLinkOutput();
  ~DeckLinkOutput();

  // Prevent copying
  DeckLinkOutput(const DeckLinkOutput&) = delete;
  DeckLinkOutput& operator=(const DeckLinkOutput&) = delete;

  // Device enumeration
  static std::vector<DeckLinkDeviceInfo> listDevices();

  // Lifecycle
  bool init(int deviceId, DeckLinkMode mode, bool supports10Bit = true);
  bool isInitialized() const;
  void shutdown();

  // Frame output (called each render pass)
  bool sendFrame(SDL_Texture* texture, int width, int height);

  // Audio output
  bool sendAudio(const std::vector<std::int16_t>& audioSamples, int sampleRate, int channels);

  // Timecode
  bool setTimecode(std::uint32_t hours, std::uint32_t minutes, std::uint32_t seconds, std::uint32_t frames, bool dropFrame = false);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  friend class Impl;
};

}  // namespace playboy::platform::video
