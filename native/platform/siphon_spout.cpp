// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// siphon_spout.cpp — Interprocess GPU texture sharing (Siphon/Spout sender).
//
// Platform implementations:
//   Windows (DECKBOY_HAS_SPOUT): Uses SpoutLibrary DLL via the SPOUTLIBRARY
//     COM-like interface. Sends BGRA pixel data via SendImage() — SpoutLibrary
//     handles DirectX shared texture creation internally.
//   macOS (DECKBOY_HAS_SIPHON): Stub (Siphon integration is a future task).
//   Linux / no SDK: Stub — all methods return success but do nothing.
//
// The sender takes an SDL_Texture, reads its pixels into a CPU buffer, and
// sends them via the platform API. Spout can also share a GPU texture
// directly, which would skip the readback entirely.
//
// THE REASON GIVEN FOR NOT DOING SO IS OUT OF DATE. It said the SDL2 renderer
// exposed no raw DirectX handle portably, which was true of SDL2 and is not
// true now: SDL3 hands back the ID3D11Texture2D through
// SDL_GetTextureProperties(SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER). A
// zero-copy path is therefore available on the Windows backend.
//
// It stays on the CPU path until somebody can test it against a real Spout
// receiver -- swapping the transport of a live output on an argument alone is
// how a show loses its feed.
//
// Header: siphon_spout.hpp
// Used by: app_render_output.ipp (sends composited output to VJ software).
// ============================================================================

#include "siphon_spout.hpp"

#include <iostream>
#include <vector>

// ── Windows Spout implementation (SpoutLibrary DLL) ─────────────────────────
#if defined(DECKBOY_HAS_SPOUT) && defined(_WIN32)

#include <SpoutLibrary/SpoutLibrary.h>

namespace deckboy::platform::video {

class SiphonSpoutSender::Impl {
 public:
  std::string name_;
  int width_ = 0;
  int height_ = 0;
  bool isInitialized_ = false;
  SPOUTLIBRARY* spout_ = nullptr;         // SpoutLibrary COM-like interface
  std::vector<unsigned char> pixelBuf_;    // Reusable pixel readback buffer
};

SiphonSpoutSender::SiphonSpoutSender(const std::string& name)
  : impl_(std::make_unique<Impl>()) {
  impl_->name_ = name;
}

SiphonSpoutSender::~SiphonSpoutSender() {
  shutdown();
}

bool SiphonSpoutSender::init(int width, int height) {
  if (impl_->isInitialized_) {
    shutdown();
  }

  impl_->width_ = width;
  impl_->height_ = height;

  // Create the SpoutLibrary instance
  impl_->spout_ = GetSpout();
  if (!impl_->spout_) {
    std::cerr << "[Spout] Failed to create SpoutLibrary instance\n";
    return false;
  }

  // Set sender name — SendImage will auto-create the sender on first call
  impl_->spout_->SetSenderName(impl_->name_.c_str());

  // Pre-allocate pixel buffer (BGRA = 4 bytes per pixel)
  impl_->pixelBuf_.resize(static_cast<size_t>(width) * height * 4);

  impl_->isInitialized_ = true;
  std::cerr << "[Spout] Sender initialized: " << impl_->name_
            << " (" << width << "x" << height << ")\n";
  return true;
}

bool SiphonSpoutSender::isInitialized() const {
  return impl_->isInitialized_;
}

void SiphonSpoutSender::shutdown() {
  if (!impl_->isInitialized_) {
    return;
  }

  if (impl_->spout_) {
    impl_->spout_->ReleaseSender();
    impl_->spout_->Release();
    impl_->spout_ = nullptr;
  }

  impl_->pixelBuf_.clear();
  impl_->isInitialized_ = false;
  std::cerr << "[Spout] Sender shut down: " << impl_->name_ << "\n";
}

bool SiphonSpoutSender::sendFrame(const void* pixels, int width, int height,
                                 int stride) {
  if (!impl_->isInitialized_ || !pixels || !impl_->spout_ ||
      width <= 0 || height <= 0) {
    return false;
  }
  impl_->width_ = width;
  impl_->height_ = height;

  // RGBA, PACKED, BUILT IN ONE PASS.
  //
  // The capture buffer is SDL_PIXELFORMAT_BGRA32 -- B,G,R,A in memory -- and
  // this used to hand it over declared as GL_BGRA, which is a true statement
  // that Spout does not act on: the bytes arrive at the receiver unswapped and
  // are read as RGBA. Measured in Resolume against the test card, the bar
  // order came back white/cyan/yellow/green/magenta/blue/red/black instead of
  // white/yellow/cyan/green/magenta/red/blue/black -- left-to-right order
  // intact, yellow swapped with cyan and red with blue, which is exactly an
  // R/B exchange and not a mirror.
  //
  // So the swap is done here and the format declared as what is actually sent.
  // One pass over the frame, which also absorbs a non-tight stride, and still
  // less work than the two full-frame copies this path used to make.
  const std::size_t tight = static_cast<std::size_t>(width) * 4;
  const std::size_t need = tight * static_cast<std::size_t>(height);
  if (impl_->pixelBuf_.size() != need) {
    impl_->pixelBuf_.resize(need);
  }
  const unsigned char* src = static_cast<const unsigned char*>(pixels);
  for (int y = 0; y < height; ++y) {
    const unsigned char* s = src + static_cast<std::size_t>(y) * stride;
    unsigned char* dstRow = impl_->pixelBuf_.data() + static_cast<std::size_t>(y) * tight;
    for (int x = 0; x < width; ++x) {
      dstRow[x * 4 + 0] = s[x * 4 + 2];   // R <- R (third byte of BGRA)
      dstRow[x * 4 + 1] = s[x * 4 + 1];   // G
      dstRow[x * 4 + 2] = s[x * 4 + 0];   // B <- B (first byte of BGRA)
      dstRow[x * 4 + 3] = s[x * 4 + 3];   // A
    }
  }

  // NO FLIP. This passed bInvert=true on the reasoning that Spout is bottom-up
  // and Deckboy is top-down -- true of Spout's OpenGL heritage, and not of the
  // DirectX11 shared texture it uses on Windows, which is top-down like us.
  // The flip was correcting a difference that is not there.
  //
  // Verified from our side rather than the receiver's: DECKBOY_SPOUT_DUMP
  // writes the exact buffer handed over, and it is top-down with the colour
  // bars in the right order.
  return impl_->spout_->SendImage(impl_->pixelBuf_.data(),
                                  static_cast<unsigned int>(width),
                                  static_cast<unsigned int>(height),
                                  GL_RGBA,
                                  false);
}

bool SiphonSpoutSender::setName(const std::string& name) {
  std::string oldName = impl_->name_;
  impl_->name_ = name;

  if (impl_->isInitialized_ && impl_->spout_) {
    // Spout requires recreating the sender for a name change
    impl_->spout_->ReleaseSender();
    impl_->spout_->SetSenderName(name.c_str());
  }

  return true;
}

std::string SiphonSpoutSender::getName() const {
  return impl_->name_;
}

bool SiphonSpoutSender::isSupported() {
  return true;
}

}  // namespace deckboy::platform::video

// ── Stub implementation (no Spout/Siphon SDK available) ─────────────────────
#else

namespace deckboy::platform::video {

class SiphonSpoutSender::Impl {
 public:
  std::string name_;
  int width_ = 0;
  int height_ = 0;
  bool isInitialized_ = false;
};

SiphonSpoutSender::SiphonSpoutSender(const std::string& name)
  : impl_(std::make_unique<Impl>()) {
  impl_->name_ = name;
}

SiphonSpoutSender::~SiphonSpoutSender() {
  shutdown();
}

bool SiphonSpoutSender::init(int width, int height) {
  if (impl_->isInitialized_) {
    shutdown();
  }
  impl_->width_ = width;
  impl_->height_ = height;
  impl_->isInitialized_ = true;
  return true;
}

bool SiphonSpoutSender::isInitialized() const {
  return impl_->isInitialized_;
}

void SiphonSpoutSender::shutdown() {
  impl_->isInitialized_ = false;
}

bool SiphonSpoutSender::sendFrame(const void* pixels, int width, int height,
                                 int stride) {
  (void) pixels; (void) width; (void) height; (void) stride;
  return impl_->isInitialized_;
}

bool SiphonSpoutSender::setName(const std::string& name) {
  impl_->name_ = name;
  return true;
}

std::string SiphonSpoutSender::getName() const {
  return impl_->name_;
}

bool SiphonSpoutSender::isSupported() {
  // Only where a real sender exists. This file contains a Windows Spout
  // implementation and nothing else — there is no Syphon backend yet — so
  // returning true on __APPLE__ (the previous behaviour) advertised a macOS
  // output that silently discarded every frame. A texture-share destination
  // that reports "supported" and shares nothing is worse than one that admits
  // it is missing: the operator wires it into a show and finds out live.
  //
  // When a Syphon backend lands, add its guard here alongside the Spout one.
#if defined(DECKBOY_HAS_SPOUT) && defined(_WIN32)
  return true;
#else
  return false;
#endif
}

}  // namespace deckboy::platform::video

#endif
