// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// ndi_trigger_api.hpp — NDI receive + find runtime (dynamic library loader).
//
// Complements ndi_api.hpp (which handles SEND). This file loads the NDI SDK
// symbols needed to DISCOVER sources on the network and RECEIVE metadata
// from them. Used for:
//   - NDI source discovery: listing available NDI senders for NdiSource cues
//   - NDI tally: receiving tally/metadata from NDI senders for status display
//   - NDI trigger: receiving metadata triggers from external control systems
//
// The same NDI runtime library is used (Processing.NDI.Lib.x64.dll / libndi.so),
// but different symbols are loaded:
//   NDIlib_find_create_v2/destroy:       discover NDI sources on the network
//   NDIlib_find_wait_for_sources:         block until source list changes
//   NDIlib_find_get_current_sources:      get the current source list
//   NDIlib_recv_create_v3/destroy:        create a receiver for a source
//   NDIlib_recv_connect:                  connect to a specific source
//   NDIlib_recv_capture_v3:               capture video/audio/metadata frames
//   NDIlib_recv_free_metadata:            free a received metadata frame
//
// Runtime structs (NdiTriggerRuntimeSource, NdiTriggerRuntimeMetadataFrame)
// mirror the NDI SDK structs but avoid requiring the NDI SDK headers at
// compile time — this file works without DECKBOY_HAS_NDI_SDK.
//
// Available on all platforms (Windows, macOS, Linux).
//
// Used by: main.cpp NDI discovery thread and tally receive loop.
// ============================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "dynamic_library.hpp"

// Mirror of NDIlib_source_t — avoids requiring NDI SDK headers.
struct NdiTriggerRuntimeSource {
  const char* p_ndi_name = nullptr;     // human-readable source name (e.g. "MACHINE (Source)")
  const char* p_url_address = nullptr;  // URL for direct connection
};

// Mirror of NDIlib_metadata_frame_t — avoids requiring NDI SDK headers.
struct NdiTriggerRuntimeMetadataFrame {
  int length = 0;               // length of p_data in bytes
  char* p_data = nullptr;       // XML metadata string
  std::int64_t timecode = 0;    // timecode of the metadata frame
};

// Dynamic loader for NDI receive/find symbols.
// Same pattern as NdiApi: call ensureLoaded(), use function pointers, call shutdown().
struct NdiTriggerApi {
  deckboy::platform::DynamicLibrary lib_;
  std::atomic<bool> loaded {false};
  std::atomic<bool> attempted {false};
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
#ifdef _WIN32
    candidates.emplace_back("Processing.NDI.Lib.x64.dll");
    if (const char* ndiDir = std::getenv("NDI_SDK_DIR"); ndiDir && *ndiDir) {
      candidates.emplace_back(std::string(ndiDir) + "\\Bin\\x64\\Processing.NDI.Lib.x64.dll");
    }
    candidates.emplace_back("C:\\Program Files\\NDI\\NDI 6 Runtime\\v6\\Processing.NDI.Lib.x64.dll");
    candidates.emplace_back("C:\\Program Files\\NDI\\NDI 5 Runtime\\Processing.NDI.Lib.x64.dll");
#elif defined(__APPLE__)
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
