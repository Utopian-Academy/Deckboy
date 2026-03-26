// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <SDL.h>

namespace deckboy::platform::video {

// Siphon (macOS) / Spout (Windows) sender for interprocess texture sharing
class SiphonSpoutSender {
 public:
  SiphonSpoutSender(const std::string& name);
  ~SiphonSpoutSender();

  // Prevent copying
  SiphonSpoutSender(const SiphonSpoutSender&) = delete;
  SiphonSpoutSender& operator=(const SiphonSpoutSender&) = delete;

  // Lifecycle
  bool init(int width, int height);
  bool isInitialized() const;
  void shutdown();

  // Send frame (called each render pass)
  bool sendFrame(SDL_Texture* texture);

  // Configuration
  bool setName(const std::string& name);
  std::string getName() const;

  // Supported on: macOS (Siphon) and Windows (Spout)
  static bool isSupported();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  friend class Impl;
};

}  // namespace deckboy::platform::video
