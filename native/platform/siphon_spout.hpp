// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// siphon_spout.hpp — Interprocess GPU texture sharing (Siphon/Spout sender).
//
// Siphon (macOS) and Spout (Windows) are frameworks for sharing GPU textures
// between applications with zero-copy efficiency. This is used for:
//   - Sending Deckboy's output to VJ/projection mapping software (Resolume, etc.)
//   - Feeding output to streaming tools that accept Siphon/Spout sources
//   - Real-time compositing with other visual applications
//
// The sender takes an SDL_Texture and shares it with any receiver on the
// same machine. Unlike NDI (network-based, requires encode/decode), Siphon/Spout
// operates directly on GPU memory — zero encoding latency.
//
// Not available on Linux (no Siphon/Spout equivalent; use NDI instead).
//
// Implementation: siphon_spout.cpp (pimpl pattern, platform-specific Impl class)
// Used by: app_render_output.ipp (sends composited output to Siphon/Spout receivers).
// ============================================================================

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <SDL.h>

namespace deckboy::platform::video {

// RAII sender for interprocess texture sharing via Siphon (macOS) or Spout (Windows).
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
