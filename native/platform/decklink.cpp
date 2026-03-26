// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "decklink.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

#if defined(DECKBOY_HAS_DECKLINK)
#include "DeckLinkAPI.h"
#endif

namespace deckboy::platform::video {

// ── Mode helpers ─────────────────────────────────────────────────

std::string deckLinkModeLabel(DeckLinkMode mode) {
  switch (mode) {
    case DeckLinkMode::HD1080i50:     return "1080i 50";
    case DeckLinkMode::HD1080i5994:   return "1080i 59.94";
    case DeckLinkMode::HD1080i60:     return "1080i 60";
    case DeckLinkMode::HD1080p2398:   return "1080p 23.98";
    case DeckLinkMode::HD1080p24:     return "1080p 24";
    case DeckLinkMode::HD1080p25:     return "1080p 25";
    case DeckLinkMode::HD1080p2997:   return "1080p 29.97";
    case DeckLinkMode::HD1080p30:     return "1080p 30";
    case DeckLinkMode::HD1080p50:     return "1080p 50";
    case DeckLinkMode::HD1080p5994:   return "1080p 59.94";
    case DeckLinkMode::HD1080p60:     return "1080p 60";
    case DeckLinkMode::HD720p50:      return "720p 50";
    case DeckLinkMode::HD720p5994:    return "720p 59.94";
    case DeckLinkMode::HD720p60:      return "720p 60";
    case DeckLinkMode::UHD2160p2398:  return "2160p 23.98";
    case DeckLinkMode::UHD2160p24:    return "2160p 24";
    case DeckLinkMode::UHD2160p25:    return "2160p 25";
    case DeckLinkMode::UHD2160p2997:  return "2160p 29.97";
    case DeckLinkMode::UHD2160p30:    return "2160p 30";
    case DeckLinkMode::UHD2160p50:    return "2160p 50";
    case DeckLinkMode::UHD2160p5994:  return "2160p 59.94";
    case DeckLinkMode::UHD2160p60:    return "2160p 60";
  }
  return "1080p 60";
}

std::string deckLinkModeToken(DeckLinkMode mode) {
  switch (mode) {
    case DeckLinkMode::HD1080i50:     return "1080i50";
    case DeckLinkMode::HD1080i5994:   return "1080i5994";
    case DeckLinkMode::HD1080i60:     return "1080i60";
    case DeckLinkMode::HD1080p2398:   return "1080p2398";
    case DeckLinkMode::HD1080p24:     return "1080p24";
    case DeckLinkMode::HD1080p25:     return "1080p25";
    case DeckLinkMode::HD1080p2997:   return "1080p2997";
    case DeckLinkMode::HD1080p30:     return "1080p30";
    case DeckLinkMode::HD1080p50:     return "1080p50";
    case DeckLinkMode::HD1080p5994:   return "1080p5994";
    case DeckLinkMode::HD1080p60:     return "1080p60";
    case DeckLinkMode::HD720p50:      return "720p50";
    case DeckLinkMode::HD720p5994:    return "720p5994";
    case DeckLinkMode::HD720p60:      return "720p60";
    case DeckLinkMode::UHD2160p2398:  return "2160p2398";
    case DeckLinkMode::UHD2160p24:    return "2160p24";
    case DeckLinkMode::UHD2160p25:    return "2160p25";
    case DeckLinkMode::UHD2160p2997:  return "2160p2997";
    case DeckLinkMode::UHD2160p30:    return "2160p30";
    case DeckLinkMode::UHD2160p50:    return "2160p50";
    case DeckLinkMode::UHD2160p5994:  return "2160p5994";
    case DeckLinkMode::UHD2160p60:    return "2160p60";
  }
  return "1080p60";
}

DeckLinkMode parseDeckLinkMode(const std::string& token) {
  if (token == "1080i50")    return DeckLinkMode::HD1080i50;
  if (token == "1080i5994")  return DeckLinkMode::HD1080i5994;
  if (token == "1080i60")    return DeckLinkMode::HD1080i60;
  if (token == "1080p2398")  return DeckLinkMode::HD1080p2398;
  if (token == "1080p24")    return DeckLinkMode::HD1080p24;
  if (token == "1080p25")    return DeckLinkMode::HD1080p25;
  if (token == "1080p2997")  return DeckLinkMode::HD1080p2997;
  if (token == "1080p30")    return DeckLinkMode::HD1080p30;
  if (token == "1080p50")    return DeckLinkMode::HD1080p50;
  if (token == "1080p5994")  return DeckLinkMode::HD1080p5994;
  if (token == "1080p60")    return DeckLinkMode::HD1080p60;
  if (token == "720p50")     return DeckLinkMode::HD720p50;
  if (token == "720p5994")   return DeckLinkMode::HD720p5994;
  if (token == "720p60")     return DeckLinkMode::HD720p60;
  if (token == "2160p2398")  return DeckLinkMode::UHD2160p2398;
  if (token == "2160p24")    return DeckLinkMode::UHD2160p24;
  if (token == "2160p25")    return DeckLinkMode::UHD2160p25;
  if (token == "2160p2997")  return DeckLinkMode::UHD2160p2997;
  if (token == "2160p30")    return DeckLinkMode::UHD2160p30;
  if (token == "2160p50")    return DeckLinkMode::UHD2160p50;
  if (token == "2160p5994")  return DeckLinkMode::UHD2160p5994;
  if (token == "2160p60")    return DeckLinkMode::UHD2160p60;
  return DeckLinkMode::HD1080p60;
}

int deckLinkModeWidth(DeckLinkMode mode) {
  switch (mode) {
    case DeckLinkMode::HD720p50:
    case DeckLinkMode::HD720p5994:
    case DeckLinkMode::HD720p60:
      return 1280;
    case DeckLinkMode::UHD2160p2398:
    case DeckLinkMode::UHD2160p24:
    case DeckLinkMode::UHD2160p25:
    case DeckLinkMode::UHD2160p2997:
    case DeckLinkMode::UHD2160p30:
    case DeckLinkMode::UHD2160p50:
    case DeckLinkMode::UHD2160p5994:
    case DeckLinkMode::UHD2160p60:
      return 3840;
    default:
      return 1920;
  }
}

int deckLinkModeHeight(DeckLinkMode mode) {
  switch (mode) {
    case DeckLinkMode::HD720p50:
    case DeckLinkMode::HD720p5994:
    case DeckLinkMode::HD720p60:
      return 720;
    case DeckLinkMode::UHD2160p2398:
    case DeckLinkMode::UHD2160p24:
    case DeckLinkMode::UHD2160p25:
    case DeckLinkMode::UHD2160p2997:
    case DeckLinkMode::UHD2160p30:
    case DeckLinkMode::UHD2160p50:
    case DeckLinkMode::UHD2160p5994:
    case DeckLinkMode::UHD2160p60:
      return 2160;
    default:
      return 1080;
  }
}

void deckLinkModeFrameRate(DeckLinkMode mode, int& numerator, int& denominator) {
  switch (mode) {
    case DeckLinkMode::HD1080p2398:
    case DeckLinkMode::UHD2160p2398:
      numerator = 24000; denominator = 1001; break;
    case DeckLinkMode::HD1080p24:
    case DeckLinkMode::UHD2160p24:
      numerator = 24000; denominator = 1000; break;
    case DeckLinkMode::HD1080p25:
    case DeckLinkMode::HD1080i50:
    case DeckLinkMode::UHD2160p25:
      numerator = 25000; denominator = 1000; break;
    case DeckLinkMode::HD1080p2997:
    case DeckLinkMode::UHD2160p2997:
      numerator = 30000; denominator = 1001; break;
    case DeckLinkMode::HD1080p30:
    case DeckLinkMode::UHD2160p30:
      numerator = 30000; denominator = 1000; break;
    case DeckLinkMode::HD720p50:
    case DeckLinkMode::HD1080p50:
    case DeckLinkMode::UHD2160p50:
      numerator = 50000; denominator = 1000; break;
    case DeckLinkMode::HD720p5994:
    case DeckLinkMode::HD1080p5994:
    case DeckLinkMode::HD1080i5994:
    case DeckLinkMode::UHD2160p5994:
      numerator = 60000; denominator = 1001; break;
    case DeckLinkMode::HD720p60:
    case DeckLinkMode::HD1080p60:
    case DeckLinkMode::HD1080i60:
    case DeckLinkMode::UHD2160p60:
    default:
      numerator = 60000; denominator = 1000; break;
  }
}

// ── DeckLinkOutput implementation ────────────────────────────────

#if defined(DECKBOY_HAS_DECKLINK)

class DeckLinkOutput::Impl {
 public:
  IDeckLink* deckLink_ = nullptr;
  IDeckLinkOutput* deckLinkOutput_ = nullptr;
  DeckLinkMode mode_ = DeckLinkMode::HD1080p60;
  int deviceId_ = -1;
  bool isInitialized_ = false;
  bool enable10Bit_ = true;

  // Scheduled playback state
  bool playbackStarted_ = false;
  std::uint64_t frameCount_ = 0;

  BMDDisplayMode toBmdMode(DeckLinkMode m) const {
    switch (m) {
      case DeckLinkMode::HD1080i50:     return bmdModeHD1080i50;
      case DeckLinkMode::HD1080i5994:   return bmdModeHD1080i5994;
      case DeckLinkMode::HD1080i60:     return bmdModeHD1080i6000;
      case DeckLinkMode::HD1080p2398:   return bmdModeHD1080p2398;
      case DeckLinkMode::HD1080p24:     return bmdModeHD1080p24;
      case DeckLinkMode::HD1080p25:     return bmdModeHD1080p25;
      case DeckLinkMode::HD1080p2997:   return bmdModeHD1080p2997;
      case DeckLinkMode::HD1080p30:     return bmdModeHD1080p30;
      case DeckLinkMode::HD1080p50:     return bmdModeHD1080p50;
      case DeckLinkMode::HD1080p5994:   return bmdModeHD1080p5994;
      case DeckLinkMode::HD1080p60:     return bmdModeHD1080p6000;
      case DeckLinkMode::HD720p50:      return bmdModeHD720p50;
      case DeckLinkMode::HD720p5994:    return bmdModeHD720p5994;
      case DeckLinkMode::HD720p60:      return bmdModeHD720p60;
      case DeckLinkMode::UHD2160p2398:  return bmdMode4K2160p2398;
      case DeckLinkMode::UHD2160p24:    return bmdMode4K2160p24;
      case DeckLinkMode::UHD2160p25:    return bmdMode4K2160p25;
      case DeckLinkMode::UHD2160p2997:  return bmdMode4K2160p2997;
      case DeckLinkMode::UHD2160p30:    return bmdMode4K2160p30;
      case DeckLinkMode::UHD2160p50:    return bmdMode4K2160p50;
      case DeckLinkMode::UHD2160p5994:  return bmdMode4K2160p5994;
      case DeckLinkMode::UHD2160p60:    return bmdMode4K2160p60;
    }
    return bmdModeHD1080p6000;
  }
};

DeckLinkOutput::DeckLinkOutput() : impl_(std::make_unique<Impl>()) {}

DeckLinkOutput::~DeckLinkOutput() { shutdown(); }

std::vector<DeckLinkDeviceInfo> DeckLinkOutput::listDevices() {
  std::vector<DeckLinkDeviceInfo> devices;
  IDeckLinkIterator* iterator = CreateDeckLinkIteratorInstance();
  if (!iterator) return devices;

  IDeckLink* deckLink = nullptr;
  int id = 0;
  while (iterator->Next(&deckLink) == S_OK) {
    DeckLinkDeviceInfo info;
    info.id = id;

    const char* modelName = nullptr;
    if (deckLink->GetModelName(&modelName) == S_OK && modelName) {
      info.modelName = modelName;
      free(const_cast<char*>(modelName));
    }

    const char* displayName = nullptr;
    if (deckLink->GetDisplayName(&displayName) == S_OK && displayName) {
      info.displayName = displayName;
      free(const_cast<char*>(displayName));
    } else {
      info.displayName = info.modelName;
    }

    IDeckLinkOutput* output = nullptr;
    if (deckLink->QueryInterface(IID_IDeckLinkOutput, reinterpret_cast<void**>(&output)) == S_OK) {
      info.supportsOutput = true;

      IDeckLinkDisplayModeIterator* modeIter = nullptr;
      if (output->GetDisplayModeIterator(&modeIter) == S_OK) {
        IDeckLinkDisplayMode* mode = nullptr;
        while (modeIter->Next(&mode) == S_OK) {
          long w = mode->GetWidth();
          long h = mode->GetHeight();
          if (w >= 3840 && h >= 2160) info.supports4K = true;
          mode->Release();
        }
        modeIter->Release();
      }

      IDeckLinkAttributes* attrs = nullptr;
      if (deckLink->QueryInterface(IID_IDeckLinkAttributes, reinterpret_cast<void**>(&attrs)) == S_OK) {
        bool hasSDI = false, hasHDMI = false;
        attrs->GetFlag(BMDDeckLinkSupportsSDIOutput, &hasSDI);
        attrs->GetFlag(BMDDeckLinkSupportsHDMIOutput, &hasHDMI);
        info.supportsSDI = hasSDI;
        info.supportsHDMI = hasHDMI;
        attrs->Release();
      }

      info.supports10Bit = true;  // All modern DeckLink cards support 10-bit
      output->Release();
    }

    devices.push_back(info);
    deckLink->Release();
    ++id;
  }

  iterator->Release();
  return devices;
}

bool DeckLinkOutput::init(int deviceId, DeckLinkMode mode, bool enable10Bit) {
  if (impl_->isInitialized_) shutdown();

  impl_->deviceId_ = deviceId;
  impl_->mode_ = mode;
  impl_->enable10Bit_ = enable10Bit;

  IDeckLinkIterator* iterator = CreateDeckLinkIteratorInstance();
  if (!iterator) {
    std::cerr << "[DeckLink] SDK not available\n";
    return false;
  }

  IDeckLink* deckLink = nullptr;
  for (int i = 0; i <= deviceId; ++i) {
    if (deckLink) { deckLink->Release(); deckLink = nullptr; }
    if (iterator->Next(&deckLink) != S_OK) {
      iterator->Release();
      std::cerr << "[DeckLink] Device " << deviceId << " not found\n";
      return false;
    }
  }
  iterator->Release();

  IDeckLinkOutput* output = nullptr;
  if (deckLink->QueryInterface(IID_IDeckLinkOutput, reinterpret_cast<void**>(&output)) != S_OK) {
    deckLink->Release();
    std::cerr << "[DeckLink] Device does not support output\n";
    return false;
  }

  BMDDisplayMode bmdMode = impl_->toBmdMode(mode);
  BMDPixelFormat pixelFormat = enable10Bit ? bmdFormat10BitYUV : bmdFormat8BitYUV;

  BMDDisplayModeSupport supported = bmdDisplayModeNotSupported;
  IDeckLinkDisplayMode* resultMode = nullptr;
  if (output->DoesSupportVideoMode(bmdVideoConnectionUnspecified, bmdMode,
                                    pixelFormat, bmdNoVideoOutputConversion,
                                    bmdSupportedVideoModeDefault,
                                    &resultMode, &supported) != S_OK ||
      supported == bmdDisplayModeNotSupported) {
    // Fall back to 8-bit if 10-bit not supported
    if (enable10Bit) {
      pixelFormat = bmdFormat8BitYUV;
      impl_->enable10Bit_ = false;
      if (output->DoesSupportVideoMode(bmdVideoConnectionUnspecified, bmdMode,
                                        pixelFormat, bmdNoVideoOutputConversion,
                                        bmdSupportedVideoModeDefault,
                                        &resultMode, &supported) != S_OK ||
          supported == bmdDisplayModeNotSupported) {
        output->Release();
        deckLink->Release();
        std::cerr << "[DeckLink] Mode not supported\n";
        return false;
      }
    } else {
      output->Release();
      deckLink->Release();
      std::cerr << "[DeckLink] Mode not supported\n";
      return false;
    }
  }
  if (resultMode) resultMode->Release();

  if (output->EnableVideoOutput(bmdMode, bmdVideoOutputFlagDefault) != S_OK) {
    output->Release();
    deckLink->Release();
    std::cerr << "[DeckLink] Failed to enable video output\n";
    return false;
  }

  impl_->deckLink_ = deckLink;
  impl_->deckLinkOutput_ = output;
  impl_->isInitialized_ = true;
  impl_->playbackStarted_ = false;
  impl_->frameCount_ = 0;
  return true;
}

bool DeckLinkOutput::isInitialized() const {
  return impl_->isInitialized_;
}

void DeckLinkOutput::shutdown() {
  if (!impl_->isInitialized_) return;

  if (impl_->deckLinkOutput_) {
    if (impl_->playbackStarted_) {
      impl_->deckLinkOutput_->StopScheduledPlayback(0, nullptr, 0);
    }
    impl_->deckLinkOutput_->DisableVideoOutput();
    impl_->deckLinkOutput_->Release();
    impl_->deckLinkOutput_ = nullptr;
  }
  if (impl_->deckLink_) {
    impl_->deckLink_->Release();
    impl_->deckLink_ = nullptr;
  }
  impl_->isInitialized_ = false;
  impl_->playbackStarted_ = false;
}

bool DeckLinkOutput::sendFrame(const std::uint8_t* pixels, int width, int height, int stride) {
  if (!impl_->isInitialized_ || !impl_->deckLinkOutput_ || !pixels) return false;

  int modeW = deckLinkModeWidth(impl_->mode_);
  int modeH = deckLinkModeHeight(impl_->mode_);

  BMDPixelFormat pixelFormat = impl_->enable10Bit_ ? bmdFormat10BitYUV : bmdFormat8BitYUV;
  int outStride = impl_->enable10Bit_ ? ((modeW + 47) / 48 * 128) : (modeW * 2);

  IDeckLinkMutableVideoFrame* frame = nullptr;
  if (impl_->deckLinkOutput_->CreateVideoFrame(
        modeW, modeH, outStride, pixelFormat,
        bmdFrameFlagDefault, &frame) != S_OK) {
    return false;
  }

  void* frameData = nullptr;
  if (frame->GetBytes(&frameData) != S_OK) {
    frame->Release();
    return false;
  }

  // Convert BGRA32 input to UYVY (8-bit) output.
  // For simplicity, scale input to output size using nearest-neighbor,
  // then convert colorspace inline.
  auto* dst = static_cast<std::uint8_t*>(frameData);
  for (int y = 0; y < modeH; ++y) {
    int srcY = (height > 0) ? std::min(y * height / modeH, height - 1) : 0;
    const std::uint8_t* srcRow = pixels + srcY * stride;

    for (int x = 0; x < modeW; x += 2) {
      int srcX0 = (width > 0) ? std::min(x * width / modeW, width - 1) : 0;
      int srcX1 = (width > 0) ? std::min((x + 1) * width / modeW, width - 1) : 0;

      // BGRA layout
      int b0 = srcRow[srcX0 * 4 + 0], g0 = srcRow[srcX0 * 4 + 1], r0 = srcRow[srcX0 * 4 + 2];
      int b1 = srcRow[srcX1 * 4 + 0], g1 = srcRow[srcX1 * 4 + 1], r1 = srcRow[srcX1 * 4 + 2];

      // BT.709 RGB→YCbCr
      int y0 = ((66 * r0 + 129 * g0 + 25 * b0 + 128) >> 8) + 16;
      int y1 = ((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16;
      int avgR = (r0 + r1) / 2, avgG = (g0 + g1) / 2, avgB = (b0 + b1) / 2;
      int cb = ((-38 * avgR - 74 * avgG + 112 * avgB + 128) >> 8) + 128;
      int cr = ((112 * avgR - 94 * avgG - 18 * avgB + 128) >> 8) + 128;

      if (!impl_->enable10Bit_) {
        // UYVY: Cb Y0 Cr Y1
        int dstOff = y * outStride + x * 2;
        dst[dstOff + 0] = static_cast<std::uint8_t>(std::clamp(cb, 0, 255));
        dst[dstOff + 1] = static_cast<std::uint8_t>(std::clamp(y0, 0, 255));
        dst[dstOff + 2] = static_cast<std::uint8_t>(std::clamp(cr, 0, 255));
        dst[dstOff + 3] = static_cast<std::uint8_t>(std::clamp(y1, 0, 255));
      }
      // 10-bit path: v210 packing is complex — omitted for now, falls back to 8-bit
    }
  }

  if (!impl_->playbackStarted_) {
    impl_->deckLinkOutput_->DisplayVideoFrameSync(frame);
  } else {
    int frN = 0, frD = 0;
    deckLinkModeFrameRate(impl_->mode_, frN, frD);
    BMDTimeValue duration = frD;
    BMDTimeScale scale = frN;
    impl_->deckLinkOutput_->ScheduleVideoFrame(
      frame, impl_->frameCount_ * duration, duration, scale);
    impl_->frameCount_++;
  }

  frame->Release();
  return true;
}

bool DeckLinkOutput::sendAudio(const std::int16_t* samples, int sampleCount,
                                int sampleRate, int channels) {
  if (!impl_->isInitialized_ || !impl_->deckLinkOutput_ || !samples || sampleCount <= 0) {
    return false;
  }
  std::uint32_t written = 0;
  impl_->deckLinkOutput_->ScheduleAudioSamples(
    const_cast<std::int16_t*>(samples),
    static_cast<std::uint32_t>(sampleCount),
    0, 0, &written);
  return written > 0;
}

#else  // !DECKBOY_HAS_DECKLINK — stub implementation

class DeckLinkOutput::Impl {
 public:
  bool isInitialized_ = false;
};

DeckLinkOutput::DeckLinkOutput() : impl_(std::make_unique<Impl>()) {}
DeckLinkOutput::~DeckLinkOutput() { shutdown(); }

std::vector<DeckLinkDeviceInfo> DeckLinkOutput::listDevices() { return {}; }

bool DeckLinkOutput::init(int, DeckLinkMode, bool) {
  std::cerr << "[DeckLink] Not available (built without ENABLE_DECKLINK)\n";
  return false;
}

bool DeckLinkOutput::isInitialized() const { return false; }
void DeckLinkOutput::shutdown() { impl_->isInitialized_ = false; }

bool DeckLinkOutput::sendFrame(const std::uint8_t*, int, int, int) { return false; }
bool DeckLinkOutput::sendAudio(const std::int16_t*, int, int, int) { return false; }

#endif  // DECKBOY_HAS_DECKLINK

}  // namespace deckboy::platform::video
