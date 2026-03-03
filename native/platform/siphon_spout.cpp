// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Playboy Contributors
// This file is part of Playboy, a cue deck for live events.
// See LICENSE for details.

#include "siphon_spout.hpp"

#include <iostream>

// Note: This implementation is a stub. For production use:
// macOS: Siphon framework (https://github.com/Siphon/Siphon-Framework)
// Windows: Spout SDK (https://github.com/leadedge/Spout2)

namespace playboy::platform::video {

class SiphonSpoutSender::Impl {
 public:
  std::string name_;
  int width_ = 0;
  int height_ = 0;
  bool isInitialized_ = false;

  // Placeholder for platform-specific objects:
  // macOS: SiphonServerDirectory*, SiphonServer*
  // Windows: SpoutSender*
  // void* platformHandle_ = nullptr;
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

  // TODO: Platform-specific initialization:
  // #ifdef __APPLE__
  //   SiphonServer* server = [[SiphonServer alloc] initWithName:impl_->name_];
  //   impl_->platformHandle_ = (__bridge void*)server;
  // #elif _WIN32
  //   SpoutSender* sender = new SpoutSender();
  //   sender->CreateSender(impl_->name_.c_str(), width, height);
  //   impl_->platformHandle_ = sender;
  // #endif

  impl_->isInitialized_ = true;
  return true;
}

bool SiphonSpoutSender::isInitialized() const {
  return impl_->isInitialized_;
}

void SiphonSpoutSender::shutdown() {
  if (!impl_->isInitialized_) {
    return;
  }

  // TODO: Release platform-specific resources:
  // #ifdef __APPLE__
  //   SiphonServer* server = (__bridge SiphonServer*)impl_->platformHandle_;
  //   [server release];
  // #elif _WIN32
  //   SpoutSender* sender = (SpoutSender*)impl_->platformHandle_;
  //   delete sender;
  // #endif

  impl_->isInitialized_ = false;
}

bool SiphonSpoutSender::sendFrame(SDL_Texture* texture) {
  if (!impl_->isInitialized_ || !texture) {
    return false;
  }

  // TODO: Send frame via platform API:
  // #ifdef __APPLE__
  //   SiphonServer* server = (__bridge SiphonServer*)impl_->platformHandle_;
  //   // Get texture ID from SDL, create IOSurface, send via Siphon
  // #elif _WIN32
  //   SpoutSender* sender = (SpoutSender*)impl_->platformHandle_;
  //   // Get texture handle from SDL, send via Spout
  // #endif

  return true;
}

bool SiphonSpoutSender::setName(const std::string& name) {
  impl_->name_ = name;

  // TODO: Update sender name on running sender:
  // #ifdef __APPLE__
  //   // Siphon doesn't support renaming; need to recreate server
  // #elif _WIN32
  //   // Spout supports dynamic renaming
  // #endif

  return true;
}

std::string SiphonSpoutSender::getName() const {
  return impl_->name_;
}

bool SiphonSpoutSender::isSupported() {
#if defined(__APPLE__) || defined(_WIN32)
  return true;
#else
  return false;  // Only macOS and Windows
#endif
}

}  // namespace playboy::platform::video
