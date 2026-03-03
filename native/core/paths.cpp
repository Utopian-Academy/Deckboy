// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Playboy Contributors
// This file is part of Playboy, a cue deck for live events.
// See LICENSE for details.


#include "paths.hpp"
#include <cstdlib>
#include <string>
#include <vector>

#if !defined(_WIN32) && (defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__))
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace playboy {
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
#if !defined(_WIN32) && (defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__))
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
  const char* env = std::getenv("PLAYBOY_ROOT");
  if (env && env[0] != '\0') {
    fs::path p(env);
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    if (!ec && !abs.empty()) return abs;
  }

  fs::path exe = getExecutablePath();
  if (!exe.empty()) {
    fs::path dir = exe.parent_path();
    if (!dir.empty()) {
      std::string name = dir.filename().string();
      if (name == "bin" || name == "build" || name == "native") {
        fs::path parent = dir.parent_path();
        if (!parent.empty()) return parent;
      }
      return dir;
    }
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
  return dataDir() / "default.playboy";
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
    xdgFonts = fs::path(xdg) / "playboy" / "fonts";
  }

  switch (name) {
    case FontName::Sans:
      envKey = "PLAYBOY_FONT_SANS";
      candidates = { data / "DejaVuSans.ttf", data / "fonts" / "DejaVuSans.ttf" };
      if (!xdgFonts.empty()) candidates.push_back(xdgFonts / "DejaVuSans.ttf");
      candidates.insert(candidates.end(), {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      });
      break;
    case FontName::Mono:
      envKey = "PLAYBOY_FONT_MONO";
      candidates = { data / "DejaVuSansMono.ttf", data / "fonts" / "DejaVuSansMono.ttf" };
      if (!xdgFonts.empty()) candidates.push_back(xdgFonts / "DejaVuSansMono.ttf");
      candidates.insert(candidates.end(), {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
      });
      break;
    case FontName::Pixel:
      envKey = "PLAYBOY_FONT_PIXEL";
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
  if (result.extension().empty()) result += ".playboy";
  return result;
}

}  // namespace core
}  // namespace playboy
