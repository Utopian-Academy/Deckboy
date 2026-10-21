// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The filesystem library, spelled dbfs:: everywhere in Deckboy.
//
// On a Mac, std::filesystem is not compiled into the app. It lives in the C++
// runtime that ships WITH macOS, and Apple only added it in 10.15 (Catalina).
// The Intel Mac build runs on 10.13 High Sierra, where using it means the app
// will not even launch, so there dbfs is ghc::filesystem: a header-only copy
// of the same API (native/third_party/ghc, MIT, v1.5.16). Every other build,
// the Apple Silicon one included, gets the standard library's own.
//
// DECKBOY_GHC_FILESYSTEM forces the copy on any platform, to compile-test it
// without a Mac.
#if defined(DECKBOY_GHC_FILESYSTEM) || \
    (defined(__APPLE__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) && \
     __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500)
#include "../third_party/ghc/filesystem.hpp"
namespace dbfs = ghc::filesystem;
#else
#include <filesystem>
namespace dbfs = std::filesystem;
#endif
