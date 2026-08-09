// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// ltc_api.hpp — Linear Timecode (LTC) decoder via libltc (dynamic load).
//
// LTC is an analog audio signal encoding SMPTE timecode, commonly used in
// broadcast and live events to synchronize playback. This file provides:
//
//   LtcDecodedTimecode:  decoded HH:MM:SS:FF + drop-frame flag
//   LtcFpsEstimator:     heuristic frame rate detection from observed timecodes
//                         (detects 24, 25, 29.97df, 30 fps)
//   decodeLtcFrameBytes: extract timecode from raw LTC frame bytes (BCD decode)
//   LtcApi:              dynamic loader for libltc — loads symbols at runtime
//                         so Deckboy can run without libltc installed
//
// The libltc library is loaded dynamically via DynamicLibrary because:
//   - It's an optional feature (not all users need timecode)
//   - It avoids a hard link dependency on a library that may not be installed
//   - The library path can be overridden via DECKBOY_LTC_LIB env variable
//
// Cross-platform: libltc is loaded dynamically on all platforms.
// Linux: apt install libltc-dev; macOS: brew install libltc; Windows: ltc.dll next to exe.
//
// Used by: main.cpp's LTC receive thread, which captures audio input and
// feeds PCM samples to the decoder to extract timecode for display/sync.
// ============================================================================

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "dynamic_library.hpp"
#include "../core/paths.hpp"   // executablePath() for bundle-relative candidates
#include <filesystem>

// A decoded SMPTE timecode value extracted from an LTC audio frame.
struct LtcDecodedTimecode {
  int hours = 0;       // 0–23
  int minutes = 0;     // 0–59
  int seconds = 0;     // 0–59
  int frames = 0;      // 0–29 (depends on frame rate)
  bool dropFrame = false;  // true for 29.97fps drop-frame timecode
};

// libltc's SMPTETimecode, laid out to match the library ABI exactly. We load
// libltc dynamically and never include its headers, so this mirrors the public
// struct: a 6-byte timezone string followed by the date and time fields as
// unsigned chars. Getting this wrong writes garbage timecode, so it is spelled
// out rather than guessed at a call site.
struct LtcSmpteTimecode {
  char timezone[6] {};   // e.g. "+0100"
  unsigned char years = 0;
  unsigned char months = 0;
  unsigned char days = 0;
  unsigned char hours = 0;
  unsigned char mins = 0;
  unsigned char secs = 0;
  unsigned char frame = 0;
};

// Heuristic frame rate estimator based on observed timecode values.
// Watches the highest frame number seen per second to determine whether
// the source is 24, 25, or 30 fps. Drop-frame timecodes are immediately
// identified as 29.97fps. Updates incrementally — each new timecode
// refines the estimate.
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

// Decode raw LTC frame bytes (8 bytes of BCD-encoded timecode) into a
// LtcDecodedTimecode struct. Each byte pair encodes units and tens digits
// of frames/seconds/minutes/hours. Bit 2 of byte 1 is the drop-frame flag.
// Returns nullopt for out-of-range values (corrupted frames).
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

// Dynamic loader for the libltc shared library.
// Loads all required decoder symbols at runtime so Deckboy can run
// without libltc installed (the LTC feature simply won't be available).
// Thread-safe: ensureLoaded() uses the `attempted` flag to avoid redundant loads.
struct LtcApi {
  static constexpr size_t kFrameExtBytes = 0x170;  // sizeof(LTCFrameExt) in libltc

  deckboy::platform::DynamicLibrary lib_;
  std::atomic<bool> loaded {false};    // true after successful symbol resolution
  std::atomic<bool> attempted {false}; // true after first load attempt (success or failure)
  std::string loadError = "not initialized";
  // Function pointers matching libltc's public API
  void* (*decoderCreateFn)(int, int) = nullptr;              // ltc_decoder_create
  void (*decoderFreeFn)(void*) = nullptr;                    // ltc_decoder_free
  void (*decoderQueueFlushFn)(void*) = nullptr;              // ltc_decoder_queue_flush
  int (*decoderQueueLengthFn)(void*) = nullptr;              // ltc_decoder_queue_length
  int (*decoderReadFn)(void*, void*) = nullptr;              // ltc_decoder_read
  void (*decoderWriteS16Fn)(void*, std::int16_t*, size_t, std::int64_t) = nullptr; // ltc_decoder_write_s16

  // ── Encoder (timecode OUT) ────────────────────────────────────────────────
  // Deckboy could CHASE timecode but never GENERATE it, so it could not be
  // master of a rig. libltc has always shipped these — the DLL exports all 27
  // encoder symbols — they simply were not bound. Optional: a build that
  // resolves only the decoder still works, `encoderAvailable` just stays false.
  void* (*encoderCreateFn)(double, double, int, int) = nullptr;   // ltc_encoder_create(sample_rate, fps, standard, flags)
  void (*encoderFreeFn)(void*) = nullptr;                          // ltc_encoder_free
  void (*encoderSetTimecodeFn)(void*, const void*) = nullptr;      // ltc_encoder_set_timecode(SMPTETimecode*)
  int  (*encoderEncodeFrameFn)(void*) = nullptr;                   // ltc_encoder_encode_frame
  int  (*encoderGetBufferFn)(void*, void*) = nullptr;              // ltc_encoder_get_buffer(ltcsnd_sample_t*)
  void (*encoderBufferFlushFn)(void*) = nullptr;                   // ltc_encoder_buffer_flush
  int  (*encoderIncTimecodeFn)(void*) = nullptr;                   // ltc_encoder_inc_timecode
  int  (*encoderSetVolumeFn)(void*, double) = nullptr;             // ltc_encoder_set_volume (dBFS)
  std::atomic<bool> encoderAvailable {false};

  // Load libltc if not already attempted. Returns true if all symbols resolved.
  bool ensureLoaded() {
    if (attempted) {
      return loaded;
    }
    attempted = true;

    std::vector<std::string> candidates;
    if (const char* env = std::getenv("DECKBOY_LTC_LIB"); env && *env) {
      candidates.emplace_back(env);
    }

    // BUNDLE-RELATIVE candidates FIRST, so a portable build finds the libltc it
    // ships rather than depending on a system install. libltc is loaded by
    // dlopen/LoadLibrary at runtime (never linked), so the packagers' link-walk
    // never sees it and cannot auto-bundle it — the packager copies it in
    // explicitly and we look for it here by absolute path relative to the
    // executable. Without this, the portable builds advertised LTC but it only
    // worked on a machine that happened to have libltc installed.
    std::error_code exeEc;
    std::filesystem::path exeDir =
        deckboy::core::Paths::executablePath().parent_path();
    if (!exeDir.empty()) {
#ifdef _WIN32
      candidates.emplace_back((exeDir / "ltc.dll").string());
      candidates.emplace_back((exeDir / "libltc.dll").string());
#elif defined(__APPLE__)
      // Contents/MacOS/Deckboy -> Contents/Frameworks/libltc.dylib
      candidates.emplace_back((exeDir / ".." / "Frameworks" / "libltc.dylib").lexically_normal().string());
#else
      // bin/Deckboy -> lib/libltc.so*
      candidates.emplace_back((exeDir / ".." / "lib" / "libltc.so.11").lexically_normal().string());
      candidates.emplace_back((exeDir / ".." / "lib" / "libltc.so").lexically_normal().string());
#endif
    }
    (void) exeEc;

#ifdef _WIN32
    candidates.emplace_back("ltc.dll");
    candidates.emplace_back("libltc.dll");
#elif defined(__APPLE__)
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

    // Encoder is OPTIONAL — resolve it, but never fail the load over it. An
    // older libltc that lacks these still gives a working chase.
    encoderCreateFn = lib_.loadSymbol<decltype(encoderCreateFn)>("ltc_encoder_create");
    encoderFreeFn = lib_.loadSymbol<decltype(encoderFreeFn)>("ltc_encoder_free");
    encoderSetTimecodeFn = lib_.loadSymbol<decltype(encoderSetTimecodeFn)>("ltc_encoder_set_timecode");
    encoderEncodeFrameFn = lib_.loadSymbol<decltype(encoderEncodeFrameFn)>("ltc_encoder_encode_frame");
    encoderGetBufferFn = lib_.loadSymbol<decltype(encoderGetBufferFn)>("ltc_encoder_get_buffer");
    encoderBufferFlushFn = lib_.loadSymbol<decltype(encoderBufferFlushFn)>("ltc_encoder_buffer_flush");
    encoderIncTimecodeFn = lib_.loadSymbol<decltype(encoderIncTimecodeFn)>("ltc_encoder_inc_timecode");
    encoderSetVolumeFn = lib_.loadSymbol<decltype(encoderSetVolumeFn)>("ltc_encoder_set_volume");
    encoderAvailable = encoderCreateFn && encoderFreeFn && encoderSetTimecodeFn &&
                       encoderEncodeFrameFn && encoderGetBufferFn && encoderIncTimecodeFn;

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
