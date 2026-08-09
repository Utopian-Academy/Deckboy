// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
//
// syphon_metal.h — plain-C++ facade over the Objective-C++ Syphon Metal server
// (syphon_metal.mm). Keeps siphon_spout.cpp free of any Metal/Objective-C so
// only the .mm is compiled as Objective-C++.
//
// Publishing path (see docs/SYPHON_PLAN.md): the caller already has the finished
// output as a CPU BGRA buffer (the Spout sender produces exactly this), so this
// uploads it into a reused MTLTexture and publishes via SyphonPublisher. No
// SDL-Metal internals required.
//
// Compiled only on macOS with ENABLE_SIPHON (DECKBOY_HAS_SIPHON). Callers must
// guard use accordingly.
#pragma once

#include <cstdint>
#include <string>

namespace deckboy {
namespace platform {
namespace video {

class SyphonPublisher {
 public:
  SyphonPublisher();
  ~SyphonPublisher();

  SyphonPublisher(const SyphonPublisher&) = delete;
  SyphonPublisher& operator=(const SyphonPublisher&) = delete;

  // Create the Metal device + Syphon server advertised under `serverName`.
  // Returns false if Metal is unavailable. Safe to call again after close().
  bool open(const std::string& serverName);

  // Upload one BGRA8 frame (tightly packed, width*4 bytes per row) and publish
  // it. Reallocates the backing texture only when the raster changes.
  bool publishBGRA(const std::uint8_t* bgra, int width, int height);

  void close();
  bool isOpen() const { return impl_ != nullptr; }

 private:
  void* impl_ = nullptr;  // opaque owner of the Objective-C objects
};

}  // namespace video
}  // namespace platform
}  // namespace deckboy
