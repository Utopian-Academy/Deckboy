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

bool SiphonSpoutSender::sendFrame(SDL_Texture* texture) {
  if (!impl_->isInitialized_ || !texture || !impl_->spout_) {
    return false;
  }

  // Query texture dimensions (SDL3: float out-params, format via properties)
  float texWf = 0.0f, texHf = 0.0f;
  if (!SDL_GetTextureSize(texture, &texWf, &texHf)) {
    return false;
  }
  int texW = static_cast<int>(texWf), texH = static_cast<int>(texHf);

  // Resize pixel buffer if texture dimensions changed
  size_t bufSize = static_cast<size_t>(texW) * texH * 4;
  if (impl_->pixelBuf_.size() != bufSize) {
    impl_->pixelBuf_.resize(bufSize);
    impl_->width_ = texW;
    impl_->height_ = texH;
  }

  // Read pixels from SDL_Texture into CPU buffer.
  // SDL_RenderReadPixels reads from the current render target.
  // The caller should have set this texture as the render target before calling.
  // We read as BGRA (SDL_PIXELFORMAT_ARGB8888 = BGRA in memory on little-endian).
  // Read via lock: there is no way to get a renderer back from a texture,
  // which is what the abandoned local here was waiting for.
  void* pixels = nullptr;
  int pitch = 0;
  if (!SDL_LockTexture(texture, nullptr, &pixels, &pitch)) {
    // Texture may not be lockable (render target). Fall back to render read.
    // The caller must ensure the texture is the current render target.
    return false;
  }

  // Copy pixels row by row (pitch may differ from width * 4)
  for (int y = 0; y < texH; ++y) {
    const unsigned char* src = static_cast<const unsigned char*>(pixels) + y * pitch;
    unsigned char* dst = impl_->pixelBuf_.data() + y * texW * 4;
    std::memcpy(dst, src, static_cast<size_t>(texW) * 4);
  }
  SDL_UnlockTexture(texture);

  // Spout SendImage expects GL_RGBA or GL_BGRA pixel format.
  // SDL textures are typically ARGB8888 which is BGRA in memory.
  // GL_BGRA = 0x80E1
  constexpr GLenum GL_BGRA_EXT = 0x80E1;
  bool ok = impl_->spout_->SendImage(
    impl_->pixelBuf_.data(),
    static_cast<unsigned int>(texW),
    static_cast<unsigned int>(texH),
    GL_BGRA_EXT,
    true  // bInvert: flip vertically (Spout expects bottom-up, SDL is top-down)
  );

  return ok;
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

bool SiphonSpoutSender::sendFrame(SDL_Texture* texture) {
  (void)texture;
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
