// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// dynamic_library.hpp — Cross-platform dynamic library loader (dlopen/LoadLibrary).
//
// Provides a RAII wrapper for loading shared libraries at runtime. This is the
// foundation for optional external library integrations:
//   - NDI SDK (ndi_api.hpp, ndi_trigger_api.hpp): Processing.NDI.Lib
//   - LTC timecode (ltc_api.hpp): libltc
//   - DeckLink (decklink.hpp): DeckLink SDK
//
// Design:
//   - Constructor takes a list of candidate paths, tried in order (allows
//     fallback from versioned .so to unversioned, env override, etc.)
//   - load() tries each candidate; first success wins
//   - loadSymbol<T>() returns a typed function pointer or nullptr
//   - Destructor calls unload() (RAII)
//   - Move-only (no copying — shared library handles can't be duplicated)
//
// Platform abstraction:
//   POSIX: dlopen(RTLD_NOW | RTLD_LOCAL), dlsym, dlclose, dlerror
//   Windows: LoadLibraryW (UTF-8 → wide string), GetProcAddress, FreeLibrary
// ============================================================================

#pragma once

#include <string>
#include <vector>
#include <type_traits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace deckboy::platform {

// Generic dynamic library loader that abstracts dlopen/dlsym/dlclose on POSIX
// and LoadLibraryW/GetProcAddress/FreeLibrary on Windows.
//
// Usage:
//   DynamicLibrary lib({"libfoo.so.2", "libfoo.so", "/usr/lib/libfoo.so.2"});
//   if (!lib.load()) { /* lib.error() has the reason */ }
//   auto fn = lib.loadSymbol<void(*)(int)>("foo_init");
//   ...
//   lib.unload();
class DynamicLibrary {
public:
  DynamicLibrary() = default;

  explicit DynamicLibrary(std::vector<std::string> candidates)
      : candidates_(std::move(candidates)) {}

  ~DynamicLibrary() { unload(); }

  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;
  DynamicLibrary(DynamicLibrary&& other) noexcept
      : candidates_(std::move(other.candidates_)),
        handle_(other.handle_),
        error_(std::move(other.error_)) {
    other.handle_ = nullptr;
  }
  DynamicLibrary& operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
      unload();
      candidates_ = std::move(other.candidates_);
      handle_ = other.handle_;
      error_ = std::move(other.error_);
      other.handle_ = nullptr;
    }
    return *this;
  }

  // Try each candidate path in order. Returns true on first success.
  bool load() {
    for (const auto& candidate : candidates_) {
      if (candidate.empty()) {
        continue;
      }
#ifdef _WIN32
      // Convert UTF-8 path to wide string for LoadLibraryW.
      int wideLen = MultiByteToWideChar(CP_UTF8, 0, candidate.c_str(), -1, nullptr, 0);
      if (wideLen > 0) {
        std::vector<wchar_t> widePath(static_cast<size_t>(wideLen));
        MultiByteToWideChar(CP_UTF8, 0, candidate.c_str(), -1, widePath.data(), wideLen);
        handle_ = static_cast<void*>(LoadLibraryW(widePath.data()));
      }
#else
      handle_ = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
      if (handle_) {
        error_.clear();
        return true;
      }
    }
    // All candidates failed — capture the last error.
#ifdef _WIN32
    DWORD code = GetLastError();
    error_ = "LoadLibrary failed (error " + std::to_string(code) + ")";
#else
    const char* err = dlerror();
    error_ = err ? err : "dlopen failed";
#endif
    return false;
  }

  // Load a symbol by name and cast it to the requested function-pointer type.
  // Returns nullptr on failure (also sets error()).
  template <typename T>
  T loadSymbol(const char* name) {
    static_assert(std::is_pointer_v<T>, "loadSymbol<T>: T must be a pointer type");
    if (!handle_) {
      error_ = "library not loaded";
      return nullptr;
    }
#ifdef _WIN32
    auto sym = reinterpret_cast<T>(
        reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name)));
#else
    auto sym = reinterpret_cast<T>(dlsym(handle_, name));
#endif
    if (!sym) {
#ifdef _WIN32
      DWORD code = GetLastError();
      error_ = std::string("GetProcAddress failed for ") + name + " (error " + std::to_string(code) + ")";
#else
      const char* err = dlerror();
      error_ = err ? err : (std::string("dlsym failed for ") + name);
#endif
    }
    return sym;
  }

  // Close the library handle.
  void unload() {
    if (handle_) {
#ifdef _WIN32
      FreeLibrary(static_cast<HMODULE>(handle_));
#else
      dlclose(handle_);
#endif
      handle_ = nullptr;
    }
  }

  bool isLoaded() const { return handle_ != nullptr; }
  const std::string& error() const { return error_; }

private:
  std::vector<std::string> candidates_;
  void* handle_ = nullptr;
  std::string error_;
};

} // namespace deckboy::platform
