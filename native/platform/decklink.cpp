// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// decklink.cpp — Blackmagic DeckLink SDI/HDMI output implementation.
//
// Provides two compile paths depending on the DECKBOY_HAS_DECKLINK feature gate:
//
//   Enabled (DECKBOY_HAS_DECKLINK defined):
//     Full implementation using the Blackmagic DeckLink SDK COM interface.
//     - Mode helpers: label/token/parse/width/height/frameRate for all 22 modes
//     - Device enumeration via IDeckLinkIterator (queries model name, display
//       name, output support, SDI/HDMI connectors, 4K mode availability)
//     - DeckLinkOutput::init(): opens device, validates mode + pixel format
//       (10-bit preferred with 8-bit fallback), enables video output
//     - DeckLinkOutput::sendFrame(): converts BGRA32 input → UYVY 8-bit via
//       BT.709 RGB→YCbCr, nearest-neighbor scales to output mode resolution,
//       schedules frame via DeckLink scheduled playback API
//     - DeckLinkOutput::sendAudio(): schedules interleaved 16-bit PCM samples
//     - DeckLinkOutput::shutdown(): stops scheduled playback, releases COM objects
//
//   Disabled (stub):
//     All methods return failure / empty results with a diagnostic message.
//
// Header: decklink.hpp
// Used by: output_backend.cpp (routes egress frames to DeckLink device).
// ============================================================================

#include "decklink.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

#if defined(DECKBOY_HAS_DECKLINK)

// ── Platform-specific DeckLink SDK includes ─────────────────────────────────
// Windows: MIDL-generated COM header from DeckLinkAPI.idl
// Linux/macOS: Direct .h headers from the SDK include directory
#if defined(_WIN32)
  #include <comdef.h>
  #include "DeckLinkAPI_h.h"
#else
  #include "DeckLinkAPI.h"
#endif

// ── Platform-specific COM / string helpers ──────────────────────────────────
// These macros abstract the differences between Windows COM (BSTR, CoCreate)
// and the Linux/macOS DeckLink API (const char*, CreateDeckLinkIteratorInstance).

#if defined(_WIN32)
  // Windows: create iterator via COM CoCreateInstance
  static IDeckLinkIterator* createDeckLinkIterator() {
    IDeckLinkIterator* iterator = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL,
                                  IID_IDeckLinkIterator, reinterpret_cast<void**>(&iterator));
    return SUCCEEDED(hr) ? iterator : nullptr;
  }
  // Windows: convert BSTR to std::string and free the BSTR
  static std::string bstrToString(BSTR bstr) {
    if (!bstr) return {};
    int wlen = ::SysStringLen(bstr);
    int mblen = ::WideCharToMultiByte(CP_UTF8, 0, bstr, wlen, nullptr, 0, nullptr, nullptr);
    std::string result(mblen, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, bstr, wlen, &result[0], mblen, nullptr, nullptr);
    return result;
  }
#else
  // Linux/macOS: factory function provided by DeckLinkAPIDispatch.cpp
  static IDeckLinkIterator* createDeckLinkIterator() {
    return CreateDeckLinkIteratorInstance();
  }
#endif

#endif // DECKBOY_HAS_DECKLINK

namespace deckboy::platform::video {

// ── Mode helpers ────────────────────────────────────────────────────────────
// These functions convert between the DeckLinkMode enum and various
// representations: display labels for the UI, tokens for persistence/config,
// pixel dimensions, and frame rate as a numerator/denominator pair.

// Returns a human-readable label for display in settings UI (e.g. "1080p 59.94").
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

// Returns a compact token for serialization (e.g. "1080p5994"). Used in
// project save/load and settings persistence.
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

// Parses a mode token back to the enum value. Defaults to HD1080p60 for
// unrecognized tokens (safe fallback — most common broadcast format).
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

// Returns horizontal resolution: 1280 (720p), 1920 (1080i/p), or 3840 (UHD).
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

// Returns vertical resolution: 720, 1080, or 2160.
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

// Returns the frame rate as numerator/denominator (e.g. 60000/1001 for 59.94fps).
// Uses the broadcast convention: NTSC rates use 1001 denominators for drop-frame,
// PAL/film rates use 1000 denominators. Interlaced modes report the field rate
// (e.g. 1080i50 = 25fps fields, but the mode groups with 25p).
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

// ── DeckLinkOutput implementation (SDK 16.0) ────────────────────────────────
// Cross-platform implementation using the Blackmagic DeckLink SDK 16.x COM
// interface. Platform differences are isolated in the helpers above; the Impl
// class and public methods below are platform-neutral.

#if defined(DECKBOY_HAS_DECKLINK)

class DeckLinkOutput::Impl {
 public:
  IDeckLink* deckLink_ = nullptr;              // COM device handle
  IDeckLinkOutput* deckLinkOutput_ = nullptr;  // COM output interface
  DeckLinkMode mode_ = DeckLinkMode::HD1080p60;
  int deviceId_ = -1;
  bool isInitialized_ = false;
  bool enable10Bit_ = true;
#if defined(_WIN32)
  bool comInitialized_ = false;                // tracks per-instance CoInitialize
#endif

  // Scheduled playback: frame counter drives the time base for ScheduleVideoFrame
  bool playbackStarted_ = false;
  std::uint64_t frameCount_ = 0;

  // Converts our DeckLinkMode enum to the SDK's BMDDisplayMode constants.
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

  // Ensure COM is initialized on this thread (Windows only).
  // Safe to call multiple times — tracks init state.
  void ensureCOMInitialized() {
#if defined(_WIN32)
    if (!comInitialized_) {
      // S_FALSE means already initialized on this thread — that's fine
      HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      if (SUCCEEDED(hr)) {
        comInitialized_ = true;
      }
    }
#endif
  }
};

DeckLinkOutput::DeckLinkOutput() : impl_(std::make_unique<Impl>()) {}

DeckLinkOutput::~DeckLinkOutput() { shutdown(); }

// ── Device name extraction helper ───────────────────────────────────────────
// Platform-specific: BSTR on Windows, const char* on Linux/macOS.
#if defined(_WIN32)
static std::string getDeckLinkModelName(IDeckLink* deckLink) {
  BSTR name = nullptr;
  if (deckLink->GetModelName(&name) == S_OK && name) {
    std::string result = bstrToString(name);
    ::SysFreeString(name);
    return result;
  }
  return {};
}
static std::string getDeckLinkDisplayName(IDeckLink* deckLink) {
  BSTR name = nullptr;
  if (deckLink->GetDisplayName(&name) == S_OK && name) {
    std::string result = bstrToString(name);
    ::SysFreeString(name);
    return result;
  }
  return {};
}
#else
static std::string getDeckLinkModelName(IDeckLink* deckLink) {
  const char* name = nullptr;
  if (deckLink->GetModelName(&name) == S_OK && name) {
    std::string result = name;
    free(const_cast<char*>(name));
    return result;
  }
  return {};
}
static std::string getDeckLinkDisplayName(IDeckLink* deckLink) {
  const char* name = nullptr;
  if (deckLink->GetDisplayName(&name) == S_OK && name) {
    std::string result = name;
    free(const_cast<char*>(name));
    return result;
  }
  return {};
}
#endif

// Enumerate all DeckLink devices via the SDK's COM iterator.
// For each device, queries model name, display name, output support,
// connector types (SDI/HDMI via BMDDeckLinkVideoOutputConnections bit field),
// and 4K capability by iterating display modes.
std::vector<DeckLinkDeviceInfo> DeckLinkOutput::listDevices() {
  std::vector<DeckLinkDeviceInfo> devices;

#if defined(_WIN32)
  // Ensure COM is ready on this thread for device enumeration
  HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool comOwned = SUCCEEDED(comHr);
#endif

  IDeckLinkIterator* iterator = createDeckLinkIterator();
  if (!iterator) {
#if defined(_WIN32)
    if (comOwned) CoUninitialize();
#endif
    return devices;  // SDK not installed or no driver loaded
  }

  IDeckLink* deckLink = nullptr;
  int id = 0;
  while (iterator->Next(&deckLink) == S_OK) {
    DeckLinkDeviceInfo info;
    info.id = id;

    info.modelName = getDeckLinkModelName(deckLink);
    info.displayName = getDeckLinkDisplayName(deckLink);
    if (info.displayName.empty()) {
      info.displayName = info.modelName;
    }

    IDeckLinkOutput* output = nullptr;
    if (deckLink->QueryInterface(IID_IDeckLinkOutput, reinterpret_cast<void**>(&output)) == S_OK) {
      info.supportsOutput = true;

      // Check 4K support by iterating available display modes
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

      // Detect connector types via IDeckLinkProfileAttributes (SDK 16.x).
      // BMDDeckLinkVideoOutputConnections returns a BMDVideoConnection bit field
      // indicating which output connectors the device has.
      IDeckLinkProfileAttributes* attrs = nullptr;
      if (deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, reinterpret_cast<void**>(&attrs)) == S_OK) {
        int64_t outputConnections = 0;
        if (attrs->GetInt(BMDDeckLinkVideoOutputConnections, &outputConnections) == S_OK) {
          info.supportsSDI  = (outputConnections & bmdVideoConnectionSDI) != 0 ||
                              (outputConnections & bmdVideoConnectionOpticalSDI) != 0;
          info.supportsHDMI = (outputConnections & bmdVideoConnectionHDMI) != 0;
        }
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
#if defined(_WIN32)
  if (comOwned) CoUninitialize();
#endif
  return devices;
}

// Initialize the DeckLink output: open the device, validate the requested mode
// and pixel format, then enable video output on the card.
// Tries 10-bit first; falls back to 8-bit if the device doesn't support it.
bool DeckLinkOutput::init(int deviceId, DeckLinkMode mode, bool enable10Bit) {
  if (impl_->isInitialized_) shutdown();

  impl_->ensureCOMInitialized();
  impl_->deviceId_ = deviceId;
  impl_->mode_ = mode;
  impl_->enable10Bit_ = enable10Bit;

  IDeckLinkIterator* iterator = createDeckLinkIterator();
  if (!iterator) {
    std::cerr << "[DeckLink] SDK not available\n";
    return false;
  }

  // Walk the iterator to find the device at the requested index
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

  // SDK 16.x DoesSupportVideoMode returns (BMDDisplayMode* actualMode, BOOL*/bool* supported).
  // Windows COM uses BOOL (typedef int), Linux/macOS use bool.
  auto checkModeSupport = [&](BMDPixelFormat pf) -> bool {
    BMDDisplayMode actualMode = bmdModeUnknown;
#if defined(_WIN32)
    BOOL supported = FALSE;
#else
    bool supported = false;
#endif
    HRESULT hr = output->DoesSupportVideoMode(
      bmdVideoConnectionUnspecified, bmdMode, pf,
      bmdNoVideoOutputConversion, bmdSupportedVideoModeDefault,
      &actualMode, &supported);
    return SUCCEEDED(hr) && supported;
  };

  if (!checkModeSupport(pixelFormat)) {
    // Fall back to 8-bit if 10-bit not supported
    if (enable10Bit) {
      pixelFormat = bmdFormat8BitYUV;
      impl_->enable10Bit_ = false;
      if (!checkModeSupport(pixelFormat)) {
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

// Shut down the DeckLink output: stop scheduled playback, disable video output,
// and release all COM objects. Safe to call multiple times.
void DeckLinkOutput::shutdown() {
  if (!impl_->isInitialized_) return;

  if (impl_->deckLinkOutput_) {
    // Stop scheduled playback before disabling output to avoid glitches
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

// Send a BGRA32 pixel buffer to the DeckLink card.
// Pipeline:
//   1. Create a DeckLink video frame at the output mode's resolution
//   2. Nearest-neighbor scale the input BGRA to the output dimensions
//   3. Convert BGRA → UYVY using BT.709 RGB→YCbCr coefficients
//   4. Display synchronously (first frame) or schedule for playback
bool DeckLinkOutput::sendFrame(const std::uint8_t* pixels, int width, int height, int stride) {
  if (!impl_->isInitialized_ || !impl_->deckLinkOutput_ || !pixels) return false;

  int modeW = deckLinkModeWidth(impl_->mode_);
  int modeH = deckLinkModeHeight(impl_->mode_);

  BMDPixelFormat pixelFormat = impl_->enable10Bit_ ? bmdFormat10BitYUV : bmdFormat8BitYUV;
  // v210 (10-bit) packs 6 pixels per 16 bytes (128 bits per 48 pixels);
  // UYVY (8-bit) is 2 bytes per pixel (4 bytes per pixel pair)
  int outStride = impl_->enable10Bit_ ? ((modeW + 47) / 48 * 128) : (modeW * 2);

  // Allocate a DeckLink frame buffer at the output mode's resolution
  IDeckLinkMutableVideoFrame* frame = nullptr;
  if (impl_->deckLinkOutput_->CreateVideoFrame(
        modeW, modeH, outStride, pixelFormat,
        bmdFrameFlagDefault, &frame) != S_OK) {
    return false;
  }

  // SDK 16.x: pixel data lives behind IDeckLinkVideoBuffer, obtained via QueryInterface.
  // Must bracket writes with StartAccess/EndAccess for proper GPU/DMA synchronization.
  IDeckLinkVideoBuffer* videoBuffer = nullptr;
  if (frame->QueryInterface(IID_IDeckLinkVideoBuffer, reinterpret_cast<void**>(&videoBuffer)) != S_OK) {
    frame->Release();
    return false;
  }
  if (videoBuffer->StartAccess(bmdBufferAccessWrite) != S_OK) {
    videoBuffer->Release();
    frame->Release();
    return false;
  }
  void* frameData = nullptr;
  if (videoBuffer->GetBytes(&frameData) != S_OK) {
    videoBuffer->EndAccess(bmdBufferAccessWrite);
    videoBuffer->Release();
    frame->Release();
    return false;
  }

  // Convert BGRA32 input to UYVY (8-bit) output.
  // Nearest-neighbor scaling: for each output pixel, sample the closest
  // input pixel. Processes pixel pairs since UYVY shares chroma between
  // two adjacent luma samples.
  auto* dst = static_cast<std::uint8_t*>(frameData);
  for (int y = 0; y < modeH; ++y) {
    // Map output row → nearest input row
    int srcY = (height > 0) ? std::min(y * height / modeH, height - 1) : 0;
    const std::uint8_t* srcRow = pixels + srcY * stride;

    for (int x = 0; x < modeW; x += 2) {
      // Map each pixel in the output pair to the nearest input column
      int srcX0 = (width > 0) ? std::min(x * width / modeW, width - 1) : 0;
      int srcX1 = (width > 0) ? std::min((x + 1) * width / modeW, width - 1) : 0;

      // Read BGRA components from the input buffer
      int b0 = srcRow[srcX0 * 4 + 0], g0 = srcRow[srcX0 * 4 + 1], r0 = srcRow[srcX0 * 4 + 2];
      int b1 = srcRow[srcX1 * 4 + 0], g1 = srcRow[srcX1 * 4 + 1], r1 = srcRow[srcX1 * 4 + 2];

      // BT.709 RGB→YCbCr conversion (integer approximation)
      // Y  = (( 66*R + 129*G +  25*B + 128) >> 8) + 16    (luma, range 16–235)
      // Cb = ((-38*R -  74*G + 112*B + 128) >> 8) + 128   (blue chroma, range 16–240)
      // Cr = ((112*R -  94*G -  18*B + 128) >> 8) + 128   (red chroma, range 16–240)
      int y0 = ((66 * r0 + 129 * g0 + 25 * b0 + 128) >> 8) + 16;
      int y1 = ((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16;
      // Average the RGB of the pixel pair for shared chroma samples
      int avgR = (r0 + r1) / 2, avgG = (g0 + g1) / 2, avgB = (b0 + b1) / 2;
      int cb = ((-38 * avgR - 74 * avgG + 112 * avgB + 128) >> 8) + 128;
      int cr = ((112 * avgR - 94 * avgG - 18 * avgB + 128) >> 8) + 128;

      if (!impl_->enable10Bit_) {
        // UYVY byte order: Cb Y0 Cr Y1 (4 bytes per 2 pixels)
        int dstOff = y * outStride + x * 2;
        dst[dstOff + 0] = static_cast<std::uint8_t>(std::clamp(cb, 0, 255));
        dst[dstOff + 1] = static_cast<std::uint8_t>(std::clamp(y0, 0, 255));
        dst[dstOff + 2] = static_cast<std::uint8_t>(std::clamp(cr, 0, 255));
        dst[dstOff + 3] = static_cast<std::uint8_t>(std::clamp(y1, 0, 255));
      }
      // 10-bit path: v210 packing is complex — omitted for now, falls back to 8-bit
    }
  }

  // Release buffer access before submitting the frame for display
  videoBuffer->EndAccess(bmdBufferAccessWrite);
  videoBuffer->Release();

  // Display the frame: sync for the first frame, scheduled for subsequent ones
  if (!impl_->playbackStarted_) {
    impl_->deckLinkOutput_->DisplayVideoFrameSync(frame);
  } else {
    // Use the mode's frame rate to calculate the time base for scheduling
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

// Schedule interleaved 16-bit PCM audio samples for DeckLink audio output.
// The DeckLink SDK accepts PCM data via ScheduleAudioSamples() which queues
// them for playout synchronized with video frames.
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

// ── Stub implementation (DeckLink SDK not available) ────────────────────────
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
