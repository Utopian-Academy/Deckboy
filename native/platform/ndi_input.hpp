// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// ndi_input.hpp — receive an NDI source natively.
//
// NDI input used to be decoded through ffmpeg's libndi_newtek input device.
// Upstream removed that in 2021 over the NewTek SDK's licence, so on any
// current ffmpeg the cue was created, named, and stayed blank for ever with
// nothing said. Verified against a real source: `ffmpeg -devices` on the build
// shipped here lists dshow, gdigrab, lavfi, openal and vfwcap, and nothing
// else.
//
// So it is received here instead, the way DeckLinkSource is captured rather
// than decoded -- same shape, same reasons. The NDI runtime is already on the
// machine for output and its receive entry points are already being loaded for
// the NDI trigger, so this adds a capture loop rather than a dependency.
//
// The SDK HEADERS are used for the struct layouts and the FUNCTIONS are loaded
// at runtime. Mirroring the structs by hand -- as ndi_trigger_api.hpp does, to
// stay buildable without the SDK -- is a standing invitation to get a field
// offset wrong and read garbage, and video frames have far more fields to get
// wrong than a metadata frame. Linking the import library instead would make
// the NDI runtime a hard requirement for starting Deckboy at all.
//
// Header-only interface; see ndi_input.cpp.
// Used by: MediaEngine, for cues whose path is ndi://<source name>.
// ============================================================================

#ifndef DECKBOY_PLATFORM_NDI_INPUT_HPP
#define DECKBOY_PLATFORM_NDI_INPUT_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace deckboy::platform::video {

class NdiInput {
 public:
  NdiInput();
  ~NdiInput();

  NdiInput(const NdiInput&) = delete;
  NdiInput& operator=(const NdiInput&) = delete;

  // BGRA, top-down, tightly packed. Called from the capture thread, so the
  // callback must not touch the renderer.
  using FrameCallback =
    std::function<void(const std::uint8_t* bgra, int width, int height, int stride)>;
  void onFrame(FrameCallback cb);

  // `sourceName` is what NDI advertises, e.g. "WINDY (Test Pattern)". Matching
  // is a substring so a show can name the sender without the machine, which is
  // the part an operator does not know until the day.
  bool start(const std::string& sourceName);
  void stop();
  bool running() const { return running_.load(std::memory_order_acquire); }

  // Empty while all is well; otherwise why there is no picture, in words an
  // operator can act on.
  std::string lastError() const;

  // Is the NDI runtime present at all? Cheap after the first call.
  static bool available(std::string* whyNot = nullptr);

  // Every source visible right now. Discovery is progressive, so this waits
  // up to `waitMs` rather than answering with whatever replied first.
  static std::vector<std::string> discoverSources(int waitMs = 1500);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::atomic<bool> running_{false};
};

}  // namespace deckboy::platform::video

#endif  // DECKBOY_PLATFORM_NDI_INPUT_HPP
