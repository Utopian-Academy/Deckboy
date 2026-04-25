// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// paths.cpp — Implementation of portable path resolution (see paths.hpp).
//
// Platform-specific executable path discovery:
//   - Windows:  GetModuleFileNameW (with dynamic buffer growth)
//   - macOS:    _NSGetExecutablePath + weakly_canonical
//   - Linux:    readlink("/proc/self/exe") with buffer growth
//
// The project root walk-up algorithm handles common build directory layouts:
//   exe in build/windows/Release/ → walks up 3 levels to find data/
//   exe in bin/                   → walks up 1 level
//   exe in project root/          → finds data/ immediately
// ============================================================================

#include "paths.hpp"
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN    // exclude rarely-used Windows headers (faster compile)
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>       // _NSGetExecutablePath
#include <unistd.h>
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <unistd.h>            // readlink
#endif

namespace fs = std::filesystem;

namespace deckboy {
namespace core {

namespace {

// Return the current working directory, or "." if it can't be determined.
// Used as the last-resort fallback for project root resolution.
fs::path getCwd() {
  std::error_code ec;
  fs::path cwd = fs::current_path(ec);
  if (ec || cwd.empty()) {
    return fs::path(".");
  }
  return cwd;
}

// Platform-specific: discover the absolute path to the running executable.
// Returns empty path on failure (caller must handle the fallback).
fs::path getExecutablePath() {
#if defined(_WIN32)
  // Windows: use GetModuleFileNameW with a growing buffer.
  // The initial buffer is MAX_PATH; if the path is longer (e.g. deep junction),
  // we double the buffer and retry until the full path fits.
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};                 // GetModuleFileNameW failed (shouldn't happen)
    }
    if (length < buffer.size() - 1) {
      buffer.resize(length);     // trim to actual length
      std::error_code ec;
      fs::path absolute = fs::absolute(fs::path(buffer), ec);
      return ec ? fs::path(buffer) : absolute;
    }
    buffer.resize(buffer.size() * 2);  // path was truncated; grow and retry
  }
#elif defined(__APPLE__)
  // macOS: _NSGetExecutablePath gives us the raw path (may contain symlinks).
  // First call with nullptr to get required buffer size, then allocate and call again.
  uint32_t size = 0;
  (void) _NSGetExecutablePath(nullptr, &size);  // fills size with required bytes
  if (size == 0) {
    return {};
  }
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return {};
  }
  // Resolve symlinks and make absolute for a clean canonical path.
  fs::path path(buffer.c_str());
  std::error_code ec;
  fs::path canonical = fs::weakly_canonical(path, ec);
  if (!ec && !canonical.empty()) {
    return canonical;
  }
  fs::path absolute = fs::absolute(path, ec);
  return ec ? path : absolute;
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
  // Linux/BSD: read the /proc/self/exe symlink. Buffer grows if path is long.
  std::string buf;
  buf.resize(256);
  for (;;) {
    ssize_t n = readlink("/proc/self/exe", &buf[0], buf.size());
    if (n < 0) return {};        // readlink failed (e.g. /proc not mounted)
    if (static_cast<size_t>(n) < buf.size()) {
      buf.resize(static_cast<size_t>(n));
      fs::path p(buf);
      std::error_code ec;
      return fs::absolute(p, ec);
    }
    buf.resize(buf.size() * 2);  // path was truncated; grow and retry
  }
#else
  return {};                     // unsupported platform — caller uses cwd fallback
#endif
}

// Walk up from the executable's directory to find the project root.
// The project root is identified by the presence of a "data/" subdirectory.
//
// Known build directory names (bin, build, native, debug, release, windows,
// x64, x86) are skipped automatically — this handles layouts like:
//   project/build/windows/Release/deckboy.exe → finds project/data/
//
// Case-insensitive comparison handles CMake's Release/Debug capitalisation
// on Windows (e.g. "release" vs "Release").
fs::path resolveProjectRoot() {
  // Priority 1: explicit DECKBOY_ROOT environment variable
  const char* env = std::getenv("DECKBOY_ROOT");
  if (env && env[0] != '\0') {
    fs::path p(env);
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    if (!ec && !abs.empty()) return abs;
  }

  // Priority 2: walk up from executable directory
  fs::path exe = getExecutablePath();
  if (!exe.empty()) {
    fs::path dir = exe.parent_path();
    for (int depth = 0; depth < 6 && !dir.empty(); ++depth) {
      std::string name = dir.filename().string();
      // Case-insensitive: lowercase the directory name for comparison.
      std::string nameLower = name;
      for (char& c : nameLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      // Skip known build subdirectory names BEFORE checking for data/.
      // This prevents false-positive matches on build/windows/Release/data/
      // (which the app itself may have created at runtime).
      if (nameLower == "bin" || nameLower == "build" || nameLower == "native" ||
          nameLower == "debug" || nameLower == "release" || nameLower == "windows" ||
          nameLower == "x64" || nameLower == "x86") {
        dir = dir.parent_path();
        continue;
      }
      // Found a non-build directory — check if it has a data/ subdirectory.
      if (fs::is_directory(dir / "data")) {
        return dir;
      }
      break;  // not a build dir and no data/ — stop walking
    }
    // Fallback: return immediate parent of executable
    fs::path fallback = exe.parent_path();
    if (!fallback.empty()) return fallback;
  }

  // Priority 3: current working directory
  return getCwd();
}

}  // anonymous namespace

// -- Public API (delegates to the anonymous-namespace helpers above) ----------

fs::path Paths::projectRoot() {
  return resolveProjectRoot();
}

fs::path Paths::executablePath() {
  return getExecutablePath();
}

fs::path Paths::dataDir() {
  return projectRoot() / "data";  // always a subdirectory of the resolved project root
}

fs::path Paths::defaultProjectFile() {
  return dataDir() / "default.deckboy";  // auto-loaded at startup if it exists
}

bool Paths::ensureDataDir() {
  std::error_code ec;
  fs::create_directories(dataDir(), ec);  // creates all intermediate dirs; no-op if exists
  return !ec;
}

// ---------------------------------------------------------------------------
// fontPath — Resolve a font file for a logical font name.
//
// The search order ensures the bundled font in data/ is preferred, then
// user-installed fonts (XDG on Linux, ~/Library on macOS), then system fonts.
// The DECKBOY_FONT_<NAME> env var overrides everything (for CI or custom setups).
//
// Platform-specific system font paths:
//   Windows: %WINDIR%/Fonts/ (segoeui.ttf, consola.ttf, arial.ttf)
//   macOS:   /System/Library/Fonts/ and /Library/Fonts/
//   Linux:   /usr/share/fonts/truetype/dejavu/ and variants
// ---------------------------------------------------------------------------
fs::path Paths::fontPath(FontName name) {
  fs::path data = dataDir();
  const char* envKey = nullptr;
  std::vector<fs::path> candidates;

  // Optional XDG user data directory (Linux convention); only used when set.
  fs::path xdgFonts;
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && xdg[0] != '\0') {
    xdgFonts = fs::path(xdg) / "deckboy" / "fonts";
  }

  // Home directory for per-user font locations (macOS ~/Library/Fonts/)
  const char* homeEnv = std::getenv("HOME");
  fs::path homeDir = (homeEnv && homeEnv[0] != '\0') ? fs::path(homeEnv) : fs::path();

#if defined(_WIN32)
  // Windows system fonts directory (usually C:\Windows\Fonts)
  const char* windirEnv = std::getenv("WINDIR");
  fs::path windowsFonts = (windirEnv && windirEnv[0] != '\0')
    ? fs::path(windirEnv) / "Fonts"
    : fs::path("C:/Windows/Fonts");
#endif

  // Build the candidate list based on the requested font name.
  // Each case starts with bundled fonts in data/, then platform-specific fallbacks.
  switch (name) {
    case FontName::Sans:
      envKey = "DECKBOY_FONT_SANS";
      // Bundled DejaVu Sans (preferred — guaranteed consistent cross-platform)
      candidates = { data / "DejaVuSans.ttf", data / "fonts" / "DejaVuSans.ttf" };
      if (!xdgFonts.empty()) candidates.push_back(xdgFonts / "DejaVuSans.ttf");
#if defined(__APPLE__)
      if (!homeDir.empty()) candidates.push_back(homeDir / "Library/Fonts/Arial.ttf");
      candidates.insert(candidates.end(), {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
      });
#elif defined(_WIN32)
      // Segoe UI is the Windows UI font; Arial is the universal fallback
      candidates.insert(candidates.end(), {
        windowsFonts / "segoeui.ttf",
        windowsFonts / "arial.ttf",
      });
#else
      // Common Linux font package paths (Debian, Arch, Fedora)
      candidates.insert(candidates.end(), {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      });
#endif
      break;
    case FontName::Mono:
      envKey = "DECKBOY_FONT_MONO";
      // Bundled DejaVu Sans Mono (used for timecodes, technical readouts)
      candidates = { data / "DejaVuSansMono.ttf", data / "fonts" / "DejaVuSansMono.ttf" };
      if (!xdgFonts.empty()) candidates.push_back(xdgFonts / "DejaVuSansMono.ttf");
#if defined(__APPLE__)
      if (!homeDir.empty()) candidates.push_back(homeDir / "Library/Fonts/Menlo.ttc");
      candidates.insert(candidates.end(), {
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/Library/Fonts/Courier New.ttf",
      });
#elif defined(_WIN32)
      // Consolas is the standard Windows monospace font
      candidates.insert(candidates.end(), {
        windowsFonts / "consola.ttf",
        windowsFonts / "cour.ttf",
      });
#else
      candidates.insert(candidates.end(), {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
      });
#endif
      break;
    case FontName::Pixel:
      envKey = "DECKBOY_FONT_PIXEL";
      // PressStart2P: retro pixel font used for the Deckboy branding/logo
      candidates = { data / "PressStart2P.ttf", data / "fonts" / "PressStart2P.ttf" };
      if (!xdgFonts.empty()) candidates.push_back(xdgFonts / "PressStart2P.ttf");
      // No system fallback — pixel font is bundled or user-supplied only
      break;
  }

  // Check for environment variable override (highest priority)
  if (envKey) {
    const char* env = std::getenv(envKey);
    if (env && env[0] != '\0') {
      fs::path p(env);
      if (fs::exists(p)) return p;
      return p;  // return even if missing — caller can report the error
    }
  }

  // Return the first candidate that actually exists on disk
  for (const auto& p : candidates) {
    if (fs::exists(p)) return p;
  }
  // Nothing found — return first candidate path so caller gets a meaningful error
  return candidates.empty() ? data / "font" : candidates[0];
}

// Normalize a project path for save/load. Handles two common cases:
//   1. Empty path → use the default project file location
//   2. Path without extension → append ".deckboy" extension
fs::path Paths::normalizeProjectPath(const fs::path& path) {
  if (path.empty()) return defaultProjectFile();
  fs::path result = path;
  if (result.extension().empty()) result += ".deckboy";
  return result;
}

}  // namespace core
}  // namespace deckboy
