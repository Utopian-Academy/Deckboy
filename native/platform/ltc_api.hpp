// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#ifndef _WIN32

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "dynamic_library.hpp"

struct LtcDecodedTimecode {
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int frames = 0;
  bool dropFrame = false;
};

struct LtcFpsEstimator {
  double estimate = 30.0;
  int trackedSecondOfDay = -1;
  int maxFrameSeen = -1;

  void observe(const LtcDecodedTimecode& tc) {
    if (tc.dropFrame) {
      estimate = 29.97;
      trackedSecondOfDay = tc.hours * 3600 + tc.minutes * 60 + tc.seconds;
      maxFrameSeen = tc.frames;
      return;
    }

    int secondOfDay = tc.hours * 3600 + tc.minutes * 60 + tc.seconds;
    if (trackedSecondOfDay < 0 || secondOfDay != trackedSecondOfDay) {
      if (maxFrameSeen >= 29) {
        estimate = 30.0;
      } else if (maxFrameSeen >= 24) {
        estimate = 25.0;
      } else if (maxFrameSeen >= 23) {
        estimate = 24.0;
      }
      trackedSecondOfDay = secondOfDay;
      maxFrameSeen = tc.frames;
    } else {
      maxFrameSeen = std::max(maxFrameSeen, tc.frames);
    }

    if (tc.frames >= 29) {
      estimate = 30.0;
    } else if (tc.frames >= 24 && estimate < 25.0) {
      estimate = 25.0;
    } else if (tc.frames >= 23 && estimate < 24.0) {
      estimate = 24.0;
    }
  }

  double current(double fallback) const {
    if (std::isfinite(estimate) && estimate > 1.0) {
      return estimate;
    }
    return std::isfinite(fallback) && fallback > 1.0 ? fallback : 30.0;
  }
};

inline std::optional<LtcDecodedTimecode> decodeLtcFrameBytes(const std::uint8_t* bytes) {
  if (!bytes) {
    return std::nullopt;
  }

  LtcDecodedTimecode tc;
  tc.frames = static_cast<int>(bytes[0] & 0x0F) + static_cast<int>((bytes[1] & 0x03) * 10);
  tc.seconds = static_cast<int>(bytes[2] & 0x0F) + static_cast<int>((bytes[3] & 0x07) * 10);
  tc.minutes = static_cast<int>(bytes[4] & 0x0F) + static_cast<int>((bytes[5] & 0x07) * 10);
  tc.hours = static_cast<int>(bytes[6] & 0x0F) + static_cast<int>((bytes[7] & 0x03) * 10);
  tc.dropFrame = (bytes[1] & 0x04) != 0;
  if (tc.frames < 0 || tc.frames > 29 ||
      tc.seconds < 0 || tc.seconds > 59 ||
      tc.minutes < 0 || tc.minutes > 59 ||
      tc.hours < 0 || tc.hours > 23) {
    return std::nullopt;
  }
  return tc;
}

struct LtcApi {
  static constexpr size_t kFrameExtBytes = 0x170;

  deckboy::platform::DynamicLibrary lib_;
  bool loaded = false;
  bool attempted = false;
  std::string loadError = "not initialized";
  void* (*decoderCreateFn)(int, int) = nullptr;
  void (*decoderFreeFn)(void*) = nullptr;
  void (*decoderQueueFlushFn)(void*) = nullptr;
  int (*decoderQueueLengthFn)(void*) = nullptr;
  int (*decoderReadFn)(void*, void*) = nullptr;
  void (*decoderWriteS16Fn)(void*, std::int16_t*, size_t, std::int64_t) = nullptr;

  bool ensureLoaded() {
    if (attempted) {
      return loaded;
    }
    attempted = true;

    std::vector<std::string> candidates;
    if (const char* env = std::getenv("DECKBOY_LTC_LIB"); env && *env) {
      candidates.emplace_back(env);
    }
#ifdef __APPLE__
    candidates.emplace_back("libltc.dylib");
    candidates.emplace_back("/usr/local/lib/libltc.dylib");
    candidates.emplace_back("/opt/homebrew/lib/libltc.dylib");
#else
    candidates.emplace_back("libltc.so.11");
    candidates.emplace_back("libltc.so");
    candidates.emplace_back("/usr/local/lib/libltc.so.11");
    candidates.emplace_back("/usr/lib/libltc.so.11");
    candidates.emplace_back("/lib/x86_64-linux-gnu/libltc.so.11");
#endif

    lib_ = deckboy::platform::DynamicLibrary(std::move(candidates));
    if (!lib_.load()) {
      loadError = lib_.error().empty() ? "unable to load libltc" : lib_.error();
      return false;
    }

    decoderCreateFn = lib_.loadSymbol<decltype(decoderCreateFn)>("ltc_decoder_create");
    decoderFreeFn = lib_.loadSymbol<decltype(decoderFreeFn)>("ltc_decoder_free");
    decoderQueueFlushFn = lib_.loadSymbol<decltype(decoderQueueFlushFn)>("ltc_decoder_queue_flush");
    decoderQueueLengthFn = lib_.loadSymbol<decltype(decoderQueueLengthFn)>("ltc_decoder_queue_length");
    decoderReadFn = lib_.loadSymbol<decltype(decoderReadFn)>("ltc_decoder_read");
    decoderWriteS16Fn = lib_.loadSymbol<decltype(decoderWriteS16Fn)>("ltc_decoder_write_s16");

    if (!decoderCreateFn || !decoderFreeFn || !decoderQueueFlushFn ||
        !decoderQueueLengthFn || !decoderReadFn || !decoderWriteS16Fn) {
      loadError = "missing libltc decoder symbols";
      lib_.unload();
      return false;
    }

    loaded = true;
    loadError.clear();
    return true;
  }

  void shutdown() {
    lib_.unload();
    loaded = false;
    attempted = false;
    loadError = "not initialized";
    decoderCreateFn = nullptr;
    decoderFreeFn = nullptr;
    decoderQueueFlushFn = nullptr;
    decoderQueueLengthFn = nullptr;
    decoderReadFn = nullptr;
    decoderWriteS16Fn = nullptr;
  }
};

#endif // !_WIN32
