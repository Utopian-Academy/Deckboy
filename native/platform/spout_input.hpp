// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// spout_input.hpp — receive a Spout sender natively (Windows).
//
// The capture backend has always reported Spout receive as "scaffold only", so
// a Syphon/Spout source cue took and then showed a striped placeholder for
// ever. This receives one properly, the same shape as ndi_input and for the
// same reason: it is captured rather than decoded, because no ffmpeg can do it.
//
// IT NEEDS AN OPENGL CONTEXT, and that is the whole difficulty. Spout shares
// pixels through a DirectX texture but its library exposes only GL entry
// points to get at them -- ReceiveImage takes a GLenum and a GLuint FBO --
// while Deckboy renders through D3D11. NDI needed nothing of the sort, which
// is why that one took an afternoon and this did not.
//
// So the receive thread makes its OWN hidden window and GL context and keeps
// them to itself. Not SDL's: SDL wants windows created on the main thread, and
// a context that has to be handed between threads to be useful is a worse
// arrangement than a private one that never moves. The window is never shown,
// never pumped, and exists only to own a device context.
//
// Used by: MediaEngine, for Syphon/Spout source cues on Windows.
// ============================================================================

#ifndef DECKBOY_PLATFORM_SPOUT_INPUT_HPP
#define DECKBOY_PLATFORM_SPOUT_INPUT_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace deckboy::platform::video {

class SpoutInput {
 public:
  SpoutInput();
  ~SpoutInput();

  SpoutInput(const SpoutInput&) = delete;
  SpoutInput& operator=(const SpoutInput&) = delete;

  // BGRA, top-down, tightly packed — the same contract NdiInput uses, so the
  // engine's conversion is shared rather than written twice.
  using FrameCallback =
    std::function<void(const std::uint8_t* bgra, int width, int height, int stride)>;
  void onFrame(FrameCallback cb);

  // An empty name takes whatever sender is active, which is what a receiver
  // with nothing configured should do. Otherwise matched exactly: Spout's own
  // names are exact and a receiver that guessed would connect to the wrong
  // machine's feed on a busy network.
  bool start(const std::string& senderName);
  void stop();
  bool running() const { return running_.load(std::memory_order_acquire); }

  // Why there is no picture, in words an operator can act on.
  std::string lastError() const;

  // Is Spout receive possible in this build at all?
  static bool available(std::string* whyNot = nullptr);

  // Senders advertising right now.
  static std::vector<std::string> discoverSenders();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::atomic<bool> running_{false};
};

}  // namespace deckboy::platform::video

#endif  // DECKBOY_PLATFORM_SPOUT_INPUT_HPP
