// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "decklink.hpp"

#include <iostream>
#include <vector>

// Note: This implementation is a stub. For production use, integrate Blackmagic DeckLink SDK:
// https://www.blackmagicdesign.com/api/
// Download DeckLink SDK and link against libDeckLinkAPI.so (Linux)

namespace deckboy::platform::video {

class DeckLinkOutput::Impl {
 public:
  int deviceId_ = -1;
  DeckLinkMode mode_ = DeckLinkMode::HD1080p60;
  bool isInitialized_ = false;
  bool supports10Bit_ = true;

  // Placeholder for IDeckLink, IDeckLinkOutput interfaces
  // void* deckLink_ = nullptr;
  // void* deckLinkOutput_ = nullptr;
};

DeckLinkOutput::DeckLinkOutput() : impl_(std::make_unique<Impl>()) {}

DeckLinkOutput::~DeckLinkOutput() {
  shutdown();
}

std::vector<DeckLinkDeviceInfo> DeckLinkOutput::listDevices() {
  std::vector<DeckLinkDeviceInfo> devices;

  // TODO: When DeckLink SDK is integrated:
  // IDeckLinkIterator* deckLinkIterator = CreateDeckLinkIteratorInstance();
  // IDeckLink* deckLink = nullptr;
  // int id = 0;
  // while (deckLinkIterator->Next(&deckLink) == S_OK) {
  //   DLS_BSTR modelName = nullptr;
  //   deckLink->GetModelName(&modelName);
  //   
  //   DeckLinkDeviceInfo info;
  //   info.id = id;
  //   info.modelName = DLS_TO_CSTRING(modelName);
  //   info.displayName = "DeckLink " + info.modelName;
  //   // Check capabilities...
  //   devices.push_back(info);
  //   
  //   deckLink->Release();
  //   id++;
  // }
  // deckLinkIterator->Release();

  return devices;
}

bool DeckLinkOutput::init(int deviceId, DeckLinkMode mode, bool supports10Bit) {
  if (impl_->isInitialized_) {
    shutdown();
  }

  impl_->deviceId_ = deviceId;
  impl_->mode_ = mode;
  impl_->supports10Bit_ = supports10Bit;
  impl_->isInitialized_ = true;

  // TODO: Initialize DeckLink device:
  // IDeckLinkIterator* deckLinkIterator = CreateDeckLinkIteratorInstance();
  // IDeckLink* deckLink = nullptr;
  // for (int i = 0; i <= deviceId; ++i) {
  //   if (deckLinkIterator->Next(&deckLink) != S_OK) {
  //     deckLinkIterator->Release();
  //     std::cerr << "DeckLink device not found: " << deviceId << '\n';
  //     return false;
  //   }
  // }
  // deckLinkIterator->Release();
  //
  // deckLink->QueryInterface(IID_IDeckLinkOutput, (void**)&impl_->deckLinkOutput_);
  // // Configure output mode based on mode_ parameter
  // // Enable 10-bit or 8-bit based on supports10Bit
  // deckLink->Release();

  return true;
}

bool DeckLinkOutput::isInitialized() const {
  return impl_->isInitialized_;
}

void DeckLinkOutput::shutdown() {
  if (!impl_->isInitialized_) {
    return;
  }

  // TODO: Release DeckLink resources:
  // if (impl_->deckLinkOutput_) {
  //   impl_->deckLinkOutput_->Release();
  //   impl_->deckLinkOutput_ = nullptr;
  // }

  impl_->isInitialized_ = false;
}

bool DeckLinkOutput::sendFrame(SDL_Texture* texture, int width, int height) {
  if (!impl_->isInitialized_) {
    return false;
  }

  // TODO: Convert SDL texture to DeckLink frame format:
  // - Read texture pixels from SDL
  // - Convert to 10-bit Y'CbCr (if 10-bit) or 8-bit UYVY
  // - Create IDeckLinkVideoFrame
  // - Send via ScheduleVideoFrame()

  return true;
}

bool DeckLinkOutput::sendAudio(const std::vector<std::int16_t>& audioSamples, int sampleRate, int channels) {
  if (!impl_->isInitialized_) {
    return false;
  }

  // TODO: Send audio via ScheduleAudioSamples():
  // IDeckLinkAudioInputPacket* audioPacket = nullptr;
  // impl_->deckLinkOutput_->CreateAudioInputPacket(&audioPacket, ...);
  // // Fill audio data...
  // impl_->deckLinkOutput_->ScheduleAudioSamples(audioPacket);
  // audioPacket->Release();

  return true;
}

bool DeckLinkOutput::setTimecode(std::uint32_t hours, std::uint32_t minutes, std::uint32_t seconds, std::uint32_t frames, bool dropFrame) {
  if (!impl_->isInitialized_) {
    return false;
  }

  // TODO: Set LTC/VITC timecode:
  // IDeckLinkTimecode* timecode = nullptr;
  // impl_->deckLinkOutput_->CreateTimecode(kBMDTimecodeFormatLTC, hours, minutes, seconds, frames, &timecode);
  // impl_->deckLinkOutput_->SetTimecodeFromComponents(timecode);
  // timecode->Release();

  return true;
}

}  // namespace deckboy::platform::video
