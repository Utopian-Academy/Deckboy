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
#include <atomic>
#include <mutex>
#include <cstring>
#include <iostream>
#include <cmath>

#if defined(DECKBOY_HAS_DECKLINK)

// ── Platform-specific DeckLink SDK includes ─────────────────────────────────
// Windows: MIDL-generated COM header from DeckLinkAPI.idl
// Linux/macOS: Direct .h headers from the SDK include directory
#if defined(_WIN32)
  #include <comdef.h>
  #include "DeckLinkAPI_h.h"
#else
  #if defined(__APPLE__)
    #include <CoreFoundation/CoreFoundation.h>
  #endif
  #include "DeckLinkAPI.h"
#endif

// GetFlag takes the platform's own boolean: BOOL on Windows, plain bool
// everywhere else. One alias rather than a conditional at every call site.
#if defined(_WIN32)
using DeckLinkFlag = BOOL;
#else
using DeckLinkFlag = bool;
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

#if defined(DECKBOY_HAS_DECKLINK)
// ONE mode table, shared by playout and capture. A second copy would be a
// second thing to update when a card gains a mode.
static BMDDisplayMode deckLinkModeToBmd(DeckLinkMode m) {
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
#endif  // DECKBOY_HAS_DECKLINK


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
    case DeckLinkMode::HD1080i5994:
      numerator = 30000; denominator = 1001; break;
    case DeckLinkMode::HD1080p30:
    case DeckLinkMode::UHD2160p30:
    case DeckLinkMode::HD1080i60:
      numerator = 30000; denominator = 1000; break;
    case DeckLinkMode::HD720p50:
    case DeckLinkMode::HD1080p50:
    case DeckLinkMode::UHD2160p50:
      numerator = 50000; denominator = 1000; break;
    case DeckLinkMode::HD720p5994:
    case DeckLinkMode::HD1080p5994:
    case DeckLinkMode::UHD2160p5994:
      numerator = 60000; denominator = 1001; break;
    case DeckLinkMode::HD720p60:
    case DeckLinkMode::HD1080p60:
    case DeckLinkMode::UHD2160p60:
    default:
      numerator = 60000; denominator = 1000; break;
  }
}

// ── DeckLinkOutput implementation (SDK 16.0) ────────────────────────────────
bool deckLinkConvertBgra(const std::uint8_t* pixels, int width, int height, int stride,
                        std::uint8_t* output, int outputWidth, int outputHeight,
                        int outputStride, bool tenBit) {
  if (!pixels || !output || width <= 0 || height <= 0 || outputWidth <= 0 ||
      outputHeight <= 0 || (outputWidth & 1) || stride < width * 4 ||
      outputStride < (tenBit ? (outputWidth + 47) / 48 * 128 : outputWidth * 2)) return false;
  const double gain = tenBit ? 4.0 : 1.0;
  auto pair = [&](const std::uint8_t* row, int x, std::uint16_t* samples) {
    if (x >= outputWidth) {
      samples[0] = samples[2] = static_cast<std::uint16_t>(128 * gain);
      samples[1] = samples[3] = static_cast<std::uint16_t>(16 * gain);
      return;
    }
    const auto* a = row + (static_cast<std::int64_t>(x) * width / outputWidth) * 4;
    const auto* b = row + (static_cast<std::int64_t>(x + 1) * width / outputWidth) * 4;
    const double lumaA = (0.2126 * a[2] + 0.7152 * a[1] + 0.0722 * a[0]) / 255.0;
    const double lumaB = (0.2126 * b[2] + 0.7152 * b[1] + 0.0722 * b[0]) / 255.0;
    const double luma = (lumaA + lumaB) * 0.5;
    const double red = (a[2] + b[2]) / 510.0;
    const double blue = (a[0] + b[0]) / 510.0;
    samples[0] = static_cast<std::uint16_t>(std::clamp(std::lround(gain *
      (128.0 + 112.0 * (blue - luma) / (1.0 - 0.0722))),
      static_cast<long>(16 * gain), static_cast<long>(240 * gain)));
    samples[1] = static_cast<std::uint16_t>(std::lround(gain * (16.0 + 219.0 * lumaA)));
    samples[2] = static_cast<std::uint16_t>(std::clamp(std::lround(gain *
      (128.0 + 112.0 * (red - luma) / (1.0 - 0.2126))),
      static_cast<long>(16 * gain), static_cast<long>(240 * gain)));
    samples[3] = static_cast<std::uint16_t>(std::lround(gain * (16.0 + 219.0 * lumaB)));
  };
  for (int y = 0; y < outputHeight; ++y) {
    const auto* row = pixels + (static_cast<std::int64_t>(y) * height / outputHeight) * stride;
    auto* dst = output + static_cast<std::size_t>(y) * outputStride;
    std::memset(dst, 0, static_cast<std::size_t>(outputStride));
    if (!tenBit) {
      for (int x = 0; x < outputWidth; x += 2) {
        std::uint16_t samples[4];
        pair(row, x, samples);
        for (int i = 0; i < 4; ++i) dst[x * 2 + i] = static_cast<std::uint8_t>(samples[i]);
      }
    } else {
      for (int x = 0; x < outputWidth; x += 6) {
        std::uint16_t s[12];
        for (int i = 0; i < 3; ++i) pair(row, x + i * 2, s + i * 4);
        const std::uint32_t words[4] = {
          s[0] | (std::uint32_t(s[1]) << 10) | (std::uint32_t(s[2]) << 20),
          s[3] | (std::uint32_t(s[4]) << 10) | (std::uint32_t(s[5]) << 20),
          s[6] | (std::uint32_t(s[7]) << 10) | (std::uint32_t(s[8]) << 20),
          s[9] | (std::uint32_t(s[10]) << 10) | (std::uint32_t(s[11]) << 20)};
        for (int i = 0; i < 4; ++i)
          for (int byte = 0; byte < 4; ++byte)
            dst[(x / 6) * 16 + i * 4 + byte] = static_cast<std::uint8_t>(words[i] >> (byte * 8));
      }
    }
  }
  return true;
}

// Cross-platform implementation using the Blackmagic DeckLink SDK 16.x COM
// interface. Platform differences are isolated in the helpers above; the Impl
// class and public methods below are platform-neutral.

#if defined(DECKBOY_HAS_DECKLINK)

class DeckLinkOutput::Impl final : public IDeckLinkVideoOutputCallback {
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
  std::uint64_t audioFrameCount_ = 0;
  bool audioEnabled_ = false;
  std::atomic<ULONG> refCount_ {1};
  std::atomic<std::uint64_t> lateFrames_ {0};

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
#if defined(_WIN32)
    if (iid == IID_IUnknown || iid == IID_IDeckLinkVideoOutputCallback) {
      *ppv = static_cast<IDeckLinkVideoOutputCallback*>(this);
      AddRef();
      return S_OK;
    }
#else
    (void)iid;
#endif
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
  ULONG STDMETHODCALLTYPE Release() override { return --refCount_; }
  HRESULT STDMETHODCALLTYPE ScheduledFrameCompleted(IDeckLinkVideoFrame*,
      BMDOutputFrameCompletionResult result) override {
    if (result != bmdOutputFrameCompleted) ++lateFrames_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE ScheduledPlaybackHasStopped() override { return S_OK; }

  // Converts our DeckLinkMode enum to the SDK's BMDDisplayMode constants.
  BMDDisplayMode toBmdMode(DeckLinkMode m) const { return deckLinkModeToBmd(m); }

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
#elif defined(__APPLE__)
// macOS is NOT Linux here. The SDK returns CFStringRef on one and a malloc'd
// const char* on the other, so grouping them as "not Windows" is a type error
// -- which went unseen for as long as the DeckLink path was never compiled on
// a Mac.
static std::string cfStringToStd(CFStringRef text) {
  if (!text) return {};
  const CFIndex length = CFStringGetLength(text);
  const CFIndex capacity =
    CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::string out(static_cast<std::size_t>(capacity), '\0');
  if (!CFStringGetCString(text, out.data(), capacity, kCFStringEncodingUTF8)) {
    return {};
  }
  out.resize(std::strlen(out.c_str()));
  return out;
}
static std::string getDeckLinkModelName(IDeckLink* deckLink) {
  CFStringRef name = nullptr;
  if (deckLink->GetModelName(&name) == S_OK && name) {
    std::string result = cfStringToStd(name);
    CFRelease(name);
    return result;
  }
  return {};
}
static std::string getDeckLinkDisplayName(IDeckLink* deckLink) {
  CFStringRef name = nullptr;
  if (deckLink->GetDisplayName(&name) == S_OK && name) {
    std::string result = cfStringToStd(name);
    CFRelease(name);
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

    // Capture capability, asked separately: plenty of cards do one and not the
    // other, and offering a playout-only card as a source is a dead control.
    IDeckLinkInput* inputProbe = nullptr;
    if (deckLink->QueryInterface(IID_IDeckLinkInput,
                                 reinterpret_cast<void**>(&inputProbe)) == S_OK) {
      info.supportsInput = true;
      inputProbe->Release();
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
bool DeckLinkOutput::init(int deviceId, DeckLinkMode mode, bool enable10Bit, bool enableAudio) {
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
  impl_->audioFrameCount_ = 0;
  impl_->audioEnabled_ = enableAudio;
  impl_->lateFrames_.store(0);
  if (output->SetScheduledFrameCompletionCallback(impl_.get()) != S_OK ||
      (enableAudio && (output->EnableAudioOutput(bmdAudioSampleRate48kHz,
        bmdAudioSampleType16bitInteger, 2, bmdAudioOutputStreamTimestamped) != S_OK ||
        output->BeginAudioPreroll() != S_OK))) {
    shutdown();
    return false;
  }
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
    impl_->deckLinkOutput_->SetScheduledFrameCompletionCallback(nullptr);
    if (impl_->audioEnabled_) impl_->deckLinkOutput_->DisableAudioOutput();
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
  if (!impl_->isInitialized_ || !impl_->deckLinkOutput_ || !pixels ||
      width <= 0 || height <= 0 || stride < width * 4) return false;
  unsigned int buffered = 0;
  if (impl_->deckLinkOutput_->GetBufferedVideoFrameCount(&buffered) != S_OK) return false;
  if (buffered >= 4) return true;

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
  const bool converted = deckLinkConvertBgra(pixels, width, height, stride,
    static_cast<std::uint8_t*>(frameData), modeW, modeH, outStride, impl_->enable10Bit_);

  // Release buffer access before submitting the frame for display
  videoBuffer->EndAccess(bmdBufferAccessWrite);
  videoBuffer->Release();

  int frN = 0, frD = 0;
  deckLinkModeFrameRate(impl_->mode_, frN, frD);
  if (impl_->playbackStarted_) {
    BMDTimeValue streamTime = 0;
    double speed = 1.0;
    if (impl_->deckLinkOutput_->GetScheduledStreamTime(frN, &streamTime, &speed) == S_OK)
      impl_->frameCount_ = std::max(impl_->frameCount_,
        static_cast<std::uint64_t>(std::max<BMDTimeValue>(0, streamTime) / frD + 2));
  }
  bool scheduled = converted && impl_->deckLinkOutput_->ScheduleVideoFrame(
    frame, static_cast<BMDTimeValue>(impl_->frameCount_) * frD, frD, frN) == S_OK;
  if (scheduled) ++impl_->frameCount_;
  if (scheduled && !impl_->playbackStarted_ && impl_->frameCount_ >= 3) {
    scheduled = (!impl_->audioEnabled_ || impl_->deckLinkOutput_->EndAudioPreroll() == S_OK) &&
                impl_->deckLinkOutput_->StartScheduledPlayback(0, frN, 1.0) == S_OK;
    impl_->playbackStarted_ = scheduled;
  }
  frame->Release();
  return scheduled;
}

// Schedule interleaved 16-bit PCM audio samples for DeckLink audio output.
// The DeckLink SDK accepts PCM data via ScheduleAudioSamples() which queues
// them for playout synchronized with video frames.
bool DeckLinkOutput::sendAudio(const std::int16_t* samples, int sampleCount,
                                int sampleRate, int channels) {
  if (!impl_->isInitialized_ || !impl_->deckLinkOutput_ || !impl_->audioEnabled_ ||
      !samples || sampleCount <= 0 || sampleRate != 48000 || channels != 2) {
    return false;
  }
  std::uint32_t written = 0;
  if (impl_->playbackStarted_) {
    BMDTimeValue streamTime = 0;
    double speed = 1.0;
    if (impl_->deckLinkOutput_->GetScheduledStreamTime(48000, &streamTime, &speed) == S_OK &&
        streamTime > static_cast<BMDTimeValue>(impl_->audioFrameCount_)) {
      int scale = 0, duration = 0;
      deckLinkModeFrameRate(impl_->mode_, scale, duration);
      const std::uint64_t videoPosition = scale > 0
        ? impl_->frameCount_ * static_cast<std::uint64_t>(duration) * 48000 /
          static_cast<std::uint64_t>(scale) : 0;
      impl_->audioFrameCount_ = std::max(videoPosition,
        static_cast<std::uint64_t>(streamTime) + 48000 / 50);
    }
  }
  const HRESULT result = impl_->deckLinkOutput_->ScheduleAudioSamples(
    const_cast<std::int16_t*>(samples), static_cast<std::uint32_t>(sampleCount),
    static_cast<BMDTimeValue>(impl_->audioFrameCount_), 48000, &written);
  impl_->audioFrameCount_ += written;
  return result == S_OK && written == static_cast<std::uint32_t>(sampleCount);
}

// ── Stub implementation (DeckLink SDK not available) ────────────────────────
// ── DeckLinkInput ───────────────────────────────────────────────────────────
//
// The SDK delivers frames by calling us back on its own thread, so this class
// is a COM callback object plus the state it needs. Everything the callback
// touches is either atomic or guarded, because the thread it runs on is the
// driver's and it will not wait for us.
//
// FORMAT DETECTION is on by default. An operator plugging a camera in does not
// know whether it is arriving as 1080i59.94 or 1080p29.97, and guessing wrong
// gives a black picture rather than an error -- so the card is asked to tell
// us, and the stream is restarted on the mode it reports.
class DeckLinkInput::Impl final : public IDeckLinkInputCallback {
 public:
  Impl() = default;
  ~Impl() = default;

  // ── IUnknown. The SDK holds a reference for as long as it may call us. ──
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
#if defined(_WIN32)
    if (iid == IID_IUnknown || iid == IID_IDeckLinkInputCallback) {
      *ppv = static_cast<IDeckLinkInputCallback*>(this);
      AddRef();
      return S_OK;
    }
#else
    // REFUSED on Linux and macOS, deliberately.
    //
    // The two platforms spell REFIID differently -- macOS as CFUUIDBytes,
    // Linux as a plain 16-byte struct -- and answering properly would mean
    // CoreFoundation on one and a memcmp on the other, in a function the SDK
    // never calls. SetCallback keeps the pointer it is given; it does not go
    // looking for another interface on it. Blackmagic's own Linux samples
    // refuse here for the same reason.
    (void)iid;
#endif
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --refCount_;
    return remaining;   // lifetime is owned by DeckLinkInput, not by refcount
  }

  // ── The card has noticed the incoming format is not what we asked for. ──
  HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
      BMDVideoInputFormatChangedEvents /*events*/,
      IDeckLinkDisplayMode* newMode,
      BMDDetectedVideoInputFormatFlags /*flags*/) override {
    if (!input_ || !newMode) return S_OK;
    // Restart on the mode the card reports. PauseStreams is not enough: the
    // pixel format and the frame size both change with it.
    input_->StopStreams();
    input_->DisableVideoInput();
    const BMDDisplayMode mode = newMode->GetDisplayMode();
    input_->EnableVideoInput(mode, bmdFormat8BitBGRA,
                             detectFormat_ ? bmdVideoInputEnableFormatDetection
                                           : bmdVideoInputFlagDefault);
    detectedWidth_ = static_cast<int>(newMode->GetWidth());
    detectedHeight_ = static_cast<int>(newMode->GetHeight());
    BMDTimeValue frameDuration = 0;
    BMDTimeScale timeScale = 0;
    if (newMode->GetFrameRate(&frameDuration, &timeScale) == S_OK &&
        frameDuration > 0) {
      detectedFps_ = static_cast<double>(timeScale) /
                     static_cast<double>(frameDuration);
    }
    input_->StartStreams();
    return S_OK;
  }

  // ── One captured frame, on the driver's thread. ──
  HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(
      IDeckLinkVideoInputFrame* videoFrame,
      IDeckLinkAudioInputPacket* audioPacket) override {
    if (videoFrame) {
      // NO INPUT SOURCE means a cable with nothing on it. The SDK still
      // delivers frames at the nominal rate, and they are garbage -- reporting
      // them as signal would light the tally on a black input.
      const bool valid =
        (videoFrame->GetFlags() & bmdFrameHasNoInputSource) == 0;
      hasSignal_ = valid;
      if (valid) {
        // PIXELS COME THROUGH IDeckLinkVideoBuffer.
        //
        // SDK 16 moved GetBytes off the frame and onto a buffer interface that
        // has to be opened for reading and closed again -- the frame itself
        // now only describes the picture. StartAccess/EndAccess is what lets
        // the driver hand out memory it may still be filling, so skipping it
        // reads a frame that is not finished.
        IDeckLinkVideoBuffer* buffer = nullptr;
        if (videoFrame->QueryInterface(IID_IDeckLinkVideoBuffer,
                                       reinterpret_cast<void**>(&buffer)) == S_OK &&
            buffer) {
          void* bytes = nullptr;
          if (buffer->StartAccess(bmdBufferAccessRead) == S_OK) {
            if (buffer->GetBytes(&bytes) == S_OK && bytes) {
              const int w = static_cast<int>(videoFrame->GetWidth());
              const int h = static_cast<int>(videoFrame->GetHeight());
              const int stride = static_cast<int>(videoFrame->GetRowBytes());
              detectedWidth_ = w;
              detectedHeight_ = h;
              std::lock_guard<std::mutex> lock(callbackMutex_);
              if (frameCallback_) {
                frameCallback_(static_cast<const std::uint8_t*>(bytes), w, h, stride);
              }
            }
            buffer->EndAccess(bmdBufferAccessRead);
          }
          buffer->Release();
        }
      }
    }
    if (audioPacket) {
      void* bytes = nullptr;
      if (audioPacket->GetBytes(&bytes) == S_OK && bytes) {
        const int frames = static_cast<int>(audioPacket->GetSampleFrameCount());
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (audioCallback_ && frames > 0) {
          audioCallback_(static_cast<const std::int16_t*>(bytes),
                         frames * audioChannels_, audioChannels_);
        }
      }
    }
    return S_OK;
  }

  IDeckLink* device_ = nullptr;
  IDeckLinkInput* input_ = nullptr;
  std::atomic<ULONG> refCount_ {1};
  std::mutex callbackMutex_;
  FrameCallback frameCallback_;
  AudioCallback audioCallback_;
  int audioChannels_ = 2;
  bool detectFormat_ = true;
  std::atomic<int> detectedWidth_ {0};
  std::atomic<int> detectedHeight_ {0};
  std::atomic<double> detectedFps_ {0.0};
  std::atomic<bool> hasSignal_ {false};
  bool running_ = false;
};

DeckLinkInput::DeckLinkInput() : impl_(std::make_unique<Impl>()) {}
DeckLinkInput::~DeckLinkInput() { stop(); }

std::vector<DeckLinkDeviceInfo> DeckLinkInput::listInputDevices() {
  std::vector<DeckLinkDeviceInfo> capable;
  for (const DeckLinkDeviceInfo& info : DeckLinkOutput::listDevices()) {
    if (info.supportsInput) {
      capable.push_back(info);
    }
  }
  return capable;
}

bool DeckLinkInput::start(int deviceId, DeckLinkMode mode, bool detectFormat,
                          int audioChannels) {
  stop();
  IDeckLinkIterator* iterator = createDeckLinkIterator();
  if (!iterator) {
    return false;
  }
  IDeckLink* deckLink = nullptr;
  int index = 0;
  IDeckLink* chosen = nullptr;
  while (iterator->Next(&deckLink) == S_OK) {
    if (index == deviceId) {
      chosen = deckLink;
      break;
    }
    deckLink->Release();
    ++index;
  }
  iterator->Release();
  if (!chosen) {
    return false;
  }

  IDeckLinkInput* input = nullptr;
  if (chosen->QueryInterface(IID_IDeckLinkInput,
                             reinterpret_cast<void**>(&input)) != S_OK) {
    chosen->Release();
    return false;
  }

  // Format detection is a card capability, not a given. Asked for rather than
  // assumed, so an older card starts on the requested mode instead of failing
  // EnableVideoInput outright.
  bool canDetect = false;
  IDeckLinkProfileAttributes* attrs = nullptr;
  if (chosen->QueryInterface(IID_IDeckLinkProfileAttributes,
                             reinterpret_cast<void**>(&attrs)) == S_OK) {
    // The platform's own boolean, which is not the same type everywhere.
    DeckLinkFlag supported = DeckLinkFlag();
    if (attrs->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &supported) == S_OK) {
      canDetect = supported ? true : false;
    }
    attrs->Release();
  }
  const bool useDetection = detectFormat && canDetect;

  impl_->device_ = chosen;
  impl_->input_ = input;
  impl_->detectFormat_ = useDetection;
  impl_->audioChannels_ = audioChannels > 0 ? audioChannels : 2;
  impl_->detectedWidth_ = deckLinkModeWidth(mode);
  impl_->detectedHeight_ = deckLinkModeHeight(mode);
  impl_->hasSignal_ = false;

  input->SetCallback(impl_.get());

  // BGRA in, because that is what the rest of the pipeline speaks. The card
  // converts from whatever is on the wire, which costs nothing on the host.
  const BMDDisplayMode bmdMode = deckLinkModeToBmd(mode);
  if (input->EnableVideoInput(bmdMode, bmdFormat8BitBGRA,
                              useDetection ? bmdVideoInputEnableFormatDetection
                                           : bmdVideoInputFlagDefault) != S_OK) {
    stop();
    return false;
  }
  if (audioChannels > 0) {
    // 48kHz is the only rate the SDK captures, which is also what the engine
    // wants, so nothing resamples.
    input->EnableAudioInput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger,
                            static_cast<uint32_t>(audioChannels));
  }
  if (input->StartStreams() != S_OK) {
    stop();
    return false;
  }
  impl_->running_ = true;
  return true;
}

bool DeckLinkInput::isRunning() const { return impl_ && impl_->running_; }

void DeckLinkInput::stop() {
  if (!impl_ || !impl_->running_) {
    // Still release anything a failed start left behind.
    if (impl_ && impl_->input_) {
      impl_->input_->SetCallback(nullptr);
      impl_->input_->Release();
      impl_->input_ = nullptr;
    }
    if (impl_ && impl_->device_) {
      impl_->device_->Release();
      impl_->device_ = nullptr;
    }
    return;
  }
  if (impl_->input_) {
    impl_->input_->StopStreams();
    impl_->input_->DisableVideoInput();
    impl_->input_->DisableAudioInput();
    // Cleared BEFORE release: the driver may have a call in flight, and
    // dropping the callback first means it finds nothing to call rather than a
    // half-destroyed object.
    impl_->input_->SetCallback(nullptr);
    impl_->input_->Release();
    impl_->input_ = nullptr;
  }
  if (impl_->device_) {
    impl_->device_->Release();
    impl_->device_ = nullptr;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->callbackMutex_);
    impl_->frameCallback_ = nullptr;
    impl_->audioCallback_ = nullptr;
  }
  impl_->running_ = false;
  impl_->hasSignal_ = false;
}

void DeckLinkInput::onFrame(FrameCallback callback) {
  std::lock_guard<std::mutex> lock(impl_->callbackMutex_);
  impl_->frameCallback_ = std::move(callback);
}

void DeckLinkInput::onAudio(AudioCallback callback) {
  std::lock_guard<std::mutex> lock(impl_->callbackMutex_);
  impl_->audioCallback_ = std::move(callback);
}

int DeckLinkInput::detectedWidth() const { return impl_ ? impl_->detectedWidth_.load() : 0; }
int DeckLinkInput::detectedHeight() const { return impl_ ? impl_->detectedHeight_.load() : 0; }
double DeckLinkInput::detectedFps() const { return impl_ ? impl_->detectedFps_.load() : 0.0; }
bool DeckLinkInput::hasSignal() const { return impl_ && impl_->hasSignal_.load(); }

#else  // !DECKBOY_HAS_DECKLINK — stub implementation

class DeckLinkOutput::Impl {
 public:
  bool isInitialized_ = false;
};

DeckLinkOutput::DeckLinkOutput() : impl_(std::make_unique<Impl>()) {}
DeckLinkOutput::~DeckLinkOutput() { shutdown(); }

std::vector<DeckLinkDeviceInfo> DeckLinkOutput::listDevices() { return {}; }

bool DeckLinkOutput::init(int, DeckLinkMode, bool, bool) {
  std::cerr << "[DeckLink] Not available (built without ENABLE_DECKLINK)\n";
  return false;
}

bool DeckLinkOutput::isInitialized() const { return false; }
void DeckLinkOutput::shutdown() { impl_->isInitialized_ = false; }

bool DeckLinkOutput::sendFrame(const std::uint8_t*, int, int, int) { return false; }
bool DeckLinkOutput::sendAudio(const std::int16_t*, int, int, int) { return false; }

// DeckLinkInput without the SDK: reports no devices and refuses to start, so
// the UI can offer the source type and say why it is unavailable rather than
// the whole translation unit vanishing and taking the symbols with it.
class DeckLinkInput::Impl {};

DeckLinkInput::DeckLinkInput() : impl_(nullptr) {}
DeckLinkInput::~DeckLinkInput() = default;

std::vector<DeckLinkDeviceInfo> DeckLinkInput::listInputDevices() { return {}; }

bool DeckLinkInput::start(int, DeckLinkMode, bool, int) { return false; }
bool DeckLinkInput::isRunning() const { return false; }
void DeckLinkInput::stop() {}
void DeckLinkInput::onFrame(FrameCallback) {}
void DeckLinkInput::onAudio(AudioCallback) {}
int DeckLinkInput::detectedWidth() const { return 0; }
int DeckLinkInput::detectedHeight() const { return 0; }
double DeckLinkInput::detectedFps() const { return 0.0; }
bool DeckLinkInput::hasSignal() const { return false; }

#endif  // DECKBOY_HAS_DECKLINK

}  // namespace deckboy::platform::video
