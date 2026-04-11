// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#if defined(DECKBOY_HAS_NDI_SDK)

#include <Processing.NDI.Lib.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "dynamic_library.hpp"

struct NdiApi {
  deckboy::platform::DynamicLibrary lib_;
  bool loaded = false;
  bool attempted = false;
  std::string loadError = "not initialized";
  bool (*initializeFn)(void) = nullptr;
  void (*destroyFn)(void) = nullptr;
  NDIlib_send_instance_t (*sendCreateFn)(const NDIlib_send_create_t*) = nullptr;
  void (*sendDestroyFn)(NDIlib_send_instance_t) = nullptr;
  void (*sendVideoFn)(NDIlib_send_instance_t, const NDIlib_video_frame_v2_t*) = nullptr;
  void (*sendAudioInterleaved16sFn)(NDIlib_send_instance_t, const NDIlib_audio_frame_interleaved_16s_t*) = nullptr;
  int (*sendConnectionsFn)(NDIlib_send_instance_t, uint32_t) = nullptr;

  bool ensureLoaded() {
    if (attempted) {
      return loaded;
    }
    attempted = true;

    std::vector<std::string> candidates;
    if (const char* env = std::getenv("DECKBOY_NDI_LIB"); env && *env) {
      candidates.emplace_back(env);
    }
#ifdef _WIN32
    candidates.emplace_back("Processing.NDI.Lib.x64.dll");
    // NDI SDK default install locations on Windows
    if (const char* ndiDir = std::getenv("NDI_SDK_DIR"); ndiDir && *ndiDir) {
      candidates.emplace_back(std::string(ndiDir) + "\\Bin\\x64\\Processing.NDI.Lib.x64.dll");
    }
    candidates.emplace_back("C:\\Program Files\\NDI\\NDI 6 Runtime\\v6\\Processing.NDI.Lib.x64.dll");
    candidates.emplace_back("C:\\Program Files\\NDI\\NDI 5 Runtime\\Processing.NDI.Lib.x64.dll");
#elif defined(__APPLE__)
    candidates.emplace_back("libndi.dylib");
#else
    candidates.emplace_back("libndi.so.6");
    candidates.emplace_back("libndi.so");
    candidates.emplace_back("/usr/local/lib/libndi.so.6");
    candidates.emplace_back("/usr/lib/libndi.so.6");
#endif

    lib_ = deckboy::platform::DynamicLibrary(std::move(candidates));
    if (!lib_.load()) {
      loadError = lib_.error().empty() ? "unable to load libndi" : lib_.error();
      return false;
    }

    initializeFn = lib_.loadSymbol<decltype(initializeFn)>("NDIlib_initialize");
    destroyFn = lib_.loadSymbol<decltype(destroyFn)>("NDIlib_destroy");
    sendCreateFn = lib_.loadSymbol<decltype(sendCreateFn)>("NDIlib_send_create");
    sendDestroyFn = lib_.loadSymbol<decltype(sendDestroyFn)>("NDIlib_send_destroy");
    sendVideoFn = lib_.loadSymbol<decltype(sendVideoFn)>("NDIlib_send_send_video_v2");
    sendAudioInterleaved16sFn = lib_.loadSymbol<decltype(sendAudioInterleaved16sFn)>("NDIlib_util_send_send_audio_interleaved_16s");
    sendConnectionsFn = lib_.loadSymbol<decltype(sendConnectionsFn)>("NDIlib_send_get_no_connections");

    if (!initializeFn || !destroyFn || !sendCreateFn || !sendDestroyFn ||
        !sendVideoFn || !sendAudioInterleaved16sFn || !sendConnectionsFn) {
      loadError = "missing NDI symbols in runtime";
      lib_.unload();
      return false;
    }

    if (!initializeFn()) {
      loadError = "NDIlib_initialize failed";
      lib_.unload();
      return false;
    }

    loaded = true;
    loadError.clear();
    return true;
  }

  void shutdown() {
    if (loaded && destroyFn) {
      destroyFn();
    }
    lib_.unload();
    loaded = false;
    attempted = false;
    loadError = "not initialized";
    initializeFn = nullptr;
    destroyFn = nullptr;
    sendCreateFn = nullptr;
    sendDestroyFn = nullptr;
    sendVideoFn = nullptr;
    sendAudioInterleaved16sFn = nullptr;
    sendConnectionsFn = nullptr;
  }
};

#endif // DECKBOY_HAS_NDI_SDK
