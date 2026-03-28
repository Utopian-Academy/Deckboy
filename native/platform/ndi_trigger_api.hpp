// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#ifndef _WIN32

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "dynamic_library.hpp"

struct NdiTriggerRuntimeSource {
  const char* p_ndi_name = nullptr;
  const char* p_url_address = nullptr;
};

struct NdiTriggerRuntimeMetadataFrame {
  int length = 0;
  char* p_data = nullptr;
  std::int64_t timecode = 0;
};

struct NdiTriggerApi {
  deckboy::platform::DynamicLibrary lib_;
  bool loaded = false;
  bool attempted = false;
  std::string loadError = "not initialized";
  bool (*initializeFn)(void) = nullptr;
  void (*destroyFn)(void) = nullptr;
  void* (*findCreateFn)(const void*) = nullptr;
  void (*findDestroyFn)(void*) = nullptr;
  bool (*findWaitForSourcesFn)(void*, std::uint32_t) = nullptr;
  const NdiTriggerRuntimeSource* (*findGetCurrentSourcesFn)(void*, std::uint32_t*) = nullptr;
  void* (*recvCreateFn)(const void*) = nullptr;
  void (*recvDestroyFn)(void*) = nullptr;
  void (*recvConnectFn)(void*, const NdiTriggerRuntimeSource*) = nullptr;
  int (*recvCaptureFn)(void*, void*, void*, NdiTriggerRuntimeMetadataFrame*, std::uint32_t) = nullptr;
  void (*recvFreeMetadataFn)(void*, const NdiTriggerRuntimeMetadataFrame*) = nullptr;

  bool ensureLoaded() {
    if (attempted) {
      return loaded;
    }
    attempted = true;

    std::vector<std::string> candidates;
    if (const char* env = std::getenv("DECKBOY_NDI_LIB"); env && *env) {
      candidates.emplace_back(env);
    }
#ifdef __APPLE__
    candidates.emplace_back("libndi.dylib");
    candidates.emplace_back("/usr/local/lib/libndi.dylib");
    candidates.emplace_back("/Library/NDI SDK for Apple/lib/macOS/libndi.dylib");
#else
    candidates.emplace_back("libndi.so.6");
    candidates.emplace_back("libndi.so");
    candidates.emplace_back("/usr/local/lib/libndi.so.6");
    candidates.emplace_back("/usr/lib/libndi.so.6");
    candidates.emplace_back("/usr/lib/x86_64-linux-gnu/libndi.so.6");
    candidates.emplace_back("/usr/lib64/libndi.so.6");
#endif

    lib_ = deckboy::platform::DynamicLibrary(std::move(candidates));
    if (!lib_.load()) {
      loadError = lib_.error().empty() ? "unable to load libndi" : lib_.error();
      return false;
    }

    initializeFn = lib_.loadSymbol<decltype(initializeFn)>("NDIlib_initialize");
    destroyFn = lib_.loadSymbol<decltype(destroyFn)>("NDIlib_destroy");
    findCreateFn = lib_.loadSymbol<decltype(findCreateFn)>("NDIlib_find_create_v2");
    findDestroyFn = lib_.loadSymbol<decltype(findDestroyFn)>("NDIlib_find_destroy");
    findWaitForSourcesFn = lib_.loadSymbol<decltype(findWaitForSourcesFn)>("NDIlib_find_wait_for_sources");
    findGetCurrentSourcesFn = lib_.loadSymbol<decltype(findGetCurrentSourcesFn)>("NDIlib_find_get_current_sources");
    recvCreateFn = lib_.loadSymbol<decltype(recvCreateFn)>("NDIlib_recv_create_v3");
    recvDestroyFn = lib_.loadSymbol<decltype(recvDestroyFn)>("NDIlib_recv_destroy");
    recvConnectFn = lib_.loadSymbol<decltype(recvConnectFn)>("NDIlib_recv_connect");
    recvCaptureFn = lib_.loadSymbol<decltype(recvCaptureFn)>("NDIlib_recv_capture_v3");
    recvFreeMetadataFn = lib_.loadSymbol<decltype(recvFreeMetadataFn)>("NDIlib_recv_free_metadata");

    if (!initializeFn || !destroyFn || !findCreateFn || !findDestroyFn ||
        !findWaitForSourcesFn || !findGetCurrentSourcesFn || !recvCreateFn ||
        !recvDestroyFn || !recvConnectFn || !recvCaptureFn || !recvFreeMetadataFn) {
      loadError = "missing NDI receiver symbols";
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
    findCreateFn = nullptr;
    findDestroyFn = nullptr;
    findWaitForSourcesFn = nullptr;
    findGetCurrentSourcesFn = nullptr;
    recvCreateFn = nullptr;
    recvDestroyFn = nullptr;
    recvConnectFn = nullptr;
    recvCaptureFn = nullptr;
    recvFreeMetadataFn = nullptr;
  }
};

#else // _WIN32

#include <string>

// Stub NdiTriggerApi for Windows — NDI trigger runtime not yet implemented
struct NdiTriggerApi {
  bool loaded = false;
  bool attempted = false;
  std::string loadError = "NDI trigger not supported on Windows";

  bool ensureLoaded() { return false; }
  void shutdown() {}
};

#endif // !_WIN32
