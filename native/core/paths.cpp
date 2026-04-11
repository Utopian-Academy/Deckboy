// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.


#include "paths.hpp"
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace deckboy {
namespace core {

namespace {

fs::path getCwd() {
  std::error_code ec;
  fs::path cwd = fs::current_path(ec);
  if (ec || cwd.empty()) {
    return fs::path(".");
  }
  return cwd;
}

fs::path getExecutablePath() {
#if defined(_WIN32)
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size() - 1) {
      buffer.resize(length);
      std::error_code ec;
      fs::path absolute = fs::absolute(fs::path(buffer), ec);
      return ec ? fs::path(buffer) : absolute;
    }
    buffer.resize(buffer.size() * 2);
  }
#elif defined(__APPLE__)
  uint32_t size = 0;
  (void) _NSGetExecutablePath(nullptr, &size);
  if (size == 0) {
    return {};
  }
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return {};
  }
  fs::path path(buffer.c_str());
  std::error_code ec;
  fs::path canonical = fs::weakly_canonical(path, ec);
  if (!ec && !canonical.empty()) {
    return canonical;
  }
  fs::path absolute = fs::absolute(path, ec);
  return ec ? path : absolute;
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
  std::string buf;
  buf.resize(256);
  for (;;) {
    ssize_t n = readlink("/proc/self/exe", &buf[0], buf.size());
    if (n < 0) return {};
    if (static_cast<size_t>(n) < buf.size()) {
      buf.resize(static_cast<size_t>(n));
      fs::path p(buf);
      std::error_code ec;
      return fs::absolute(p, ec);
    }
    buf.resize(buf.size() * 2);
  }
#else
  return {};
#endif
}

fs::path resolveProjectRoot() {
  const char* env = std::getenv("DECKBOY_ROOT");
  if (env && env[0] != '\0') {
    fs::path p(env);
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    if (!ec && !abs.empty()) return abs;
  }

  fs::path exe = getExecutablePath();
  if (!exe.empty()) {
    // Walk up from executable directory looking for data/ directory.
    // Handles build/native/, build/, bin/, or direct project root.
    fs::path dir = exe.parent_path();
    for (int depth = 0; depth < 6 && !dir.empty(); ++depth) {
      std::string name = dir.filename().string();
      // Case-insensitive comparison to handle CMake's Release/Debug capitalisation on Windows.
      // Skip known build subdirectory names BEFORE checking for data/ — prevents stopping at
      // build/windows/Release/data/ (created by the app) instead of the real project root.
      std::string nameLower = name;
      for (char& c : nameLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (nameLower == "bin" || nameLower == "build" || nameLower == "native" ||
          nameLower == "debug" || nameLower == "release" || nameLower == "windows" ||
          nameLower == "x64" || nameLower == "x86") {
        dir = dir.parent_path();
        continue;
      }
      if (fs::is_directory(dir / "data")) {
        return dir;
      }
      break;
    }
    // Fallback: return immediate parent of executable
    fs::path fallback = exe.parent_path();
    if (!fallback.empty()) return fallback;
  }

  return getCwd();
}

}  // namespace

fs::path Paths::projectRoot() {
  return resolveProjectRoot();
}

fs::path Paths::executablePath() {
  return getExecutablePath();
}

fs::path Paths::dataDir() {
  return projectRoot() / "data";
}

fs::path Paths::defaultProjectFile() {
  return dataDir() / "default.deckboy";
}

bool Paths::ensureDataDir() {
  std::error_code ec;
  fs::create_directories(dataDir(), ec);
  return !ec;
}

fs::path Paths::fontPath(FontName name) {
  fs::path data = dataDir();
  const char* envKey = nullptr;
  std::vector<fs::path> candidates;

  // Optional user data dir (XDG); only used when set.
  fs::path xdgFonts;
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && xdg[0] != '\0') {
    xdgFonts = fs::path(xdg) / "deckboy" / "fonts";
  }

  const char* homeEnv = std::getenv("HOME");
  fs::path homeDir = (homeEnv && homeEnv[0] != '\0') ? fs::path(homeEnv) : fs::path();

#if defined(_WIN32)
  const char* windirEnv = std::getenv("WINDIR");
  fs::path windowsFonts = (windirEnv && windirEnv[0] != '\0')
    ? fs::path(windirEnv) / "Fonts"
    : fs::path("C:/Windows/Fonts");
#endif

  switch (name) {
    case FontName::Sans:
      envKey = "DECKBOY_FONT_SANS";
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
      candidates.insert(candidates.end(), {
        windowsFonts / "segoeui.ttf",
        windowsFonts / "arial.ttf",
      });
#else
      candidates.insert(candidates.end(), {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      });
#endif
      break;
    case FontName::Mono:
      envKey = "DECKBOY_FONT_MONO";
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
      candidates = { data / "PressStart2P.ttf", data / "fonts" / "PressStart2P.ttf" };
      if (!xdgFonts.empty()) candidates.push_back(xdgFonts / "PressStart2P.ttf");
      break;
  }

  if (envKey) {
    const char* env = std::getenv(envKey);
    if (env && env[0] != '\0') {
      fs::path p(env);
      if (fs::exists(p)) return p;
      return p;  // caller may still try to open; return requested path
    }
  }

  for (const auto& p : candidates) {
    if (fs::exists(p)) return p;
  }
  return candidates.empty() ? data / "font" : candidates[0];
}

fs::path Paths::normalizeProjectPath(const fs::path& path) {
  if (path.empty()) return defaultProjectFile();
  fs::path result = path;
  if (result.extension().empty()) result += ".deckboy";
  return result;
}

}  // namespace core
}  // namespace deckboy
