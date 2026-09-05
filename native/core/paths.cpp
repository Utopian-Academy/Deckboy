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
#include <fstream>
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

  // Priority 2 (macOS only): inside a .app bundle, resources live in
  // Contents/Resources — that is Apple's layout, and codesign treats
  // Contents/MacOS as a code-only directory, so shipping data/ next to the
  // executable there risks failing signature validation on Apple Silicon.
  //
  // Detected structurally (…/Contents/MacOS/<exe>) rather than by looking for a
  // ".app" suffix, because the bundle can be renamed by whoever downloads it.
  // Falls through to the normal walk-up when not bundled, so a plain
  // build-tree run on macOS behaves exactly as before.
#ifdef __APPLE__
  {
    fs::path exePath = getExecutablePath();
    if (!exePath.empty()) {
      fs::path macosDir = exePath.parent_path();
      fs::path contentsDir = macosDir.parent_path();
      if (macosDir.filename() == "MacOS" && contentsDir.filename() == "Contents") {
        fs::path resources = contentsDir / "Resources";
        if (fs::is_directory(resources / "data")) {
          return resources;
        }
      }
    }
  }
#endif

  // Priority 3: walk up from executable directory
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

  // Priority 4: current working directory
  return getCwd();
}

// True when the executable sits at …/Contents/MacOS/<exe>, i.e. we are running
// from inside a bundle whose Resources are sealed by the code signature.
bool runningFromMacBundle() {
#ifdef __APPLE__
  fs::path exePath = getExecutablePath();
  if (exePath.empty()) {
    return false;
  }
  fs::path macosDir = exePath.parent_path();
  fs::path contentsDir = macosDir.parent_path();
  return macosDir.filename() == "MacOS" && contentsDir.filename() == "Contents";
#else
  return false;
#endif
}

// Per-user application data directory for this platform.
fs::path userApplicationDataDir() {
  const char* home = std::getenv("HOME");
#if defined(_WIN32)
  if (const char* appData = std::getenv("APPDATA"); appData && appData[0] != '\0') {
    return fs::path(appData) / "Deckboy";
  }
  if (const char* profile = std::getenv("USERPROFILE"); profile && profile[0] != '\0') {
    return fs::path(profile) / "AppData" / "Roaming" / "Deckboy";
  }
#elif defined(__APPLE__)
  if (home && home[0] != '\0') {
    return fs::path(home) / "Library" / "Application Support" / "Deckboy";
  }
#else
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && xdg[0] != '\0') {
    return fs::path(xdg) / "deckboy";
  }
  if (home && home[0] != '\0') {
    return fs::path(home) / ".local" / "share" / "deckboy";
  }
#endif
  (void) home;
  return getCwd() / "deckboy-data";  // last resort; better than a path we can't write
}

// Can we create files in this directory? Probes for real rather than trusting
// permission bits: a read-only DMG, a Gatekeeper-translocated bundle and a
// non-admin /Applications install all fail here, and each of those is a machine
// Deckboy is expected to run on.
bool directoryIsWritable(const fs::path& dir) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (!fs::is_directory(dir, ec)) {
    return false;
  }
  fs::path probe = dir / ".deckboy-write-probe";
  {
    std::ofstream out(probe, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out << 'x';
    if (!out) {
      return false;
    }
  }
  fs::remove(probe, ec);
  return true;
}

// Was the state dir FORCED by the environment, rather than worked out?
//
// It matters for one thing only -- see ensureDataDir -- but it matters a lot:
// an explicit override is a caller saying "this run is isolated", and that is
// a promise the migration below can otherwise break.
bool stateDirWasOverridden() {
  const char* env = std::getenv("DECKBOY_STATE_DIR");
  return env && env[0] != '\0';
}

fs::path resolveStateDir() {
  if (const char* env = std::getenv("DECKBOY_STATE_DIR"); env && env[0] != '\0') {
    std::error_code ec;
    fs::path abs = fs::absolute(fs::path(env), ec);
    if (!ec && !abs.empty()) return abs;
  }
  // The data dir is the answer whenever it can be one: that is what keeps a
  // portable install portable, and what DECKBOY_ROOT selects for an isolated
  // test run. Two things disqualify it.
  //
  // Inside a bundle it is Contents/Resources/data, and writing there
  // invalidates the code signature ("a sealed resource is missing or invalid")
  // even where the disk permits it — so the bundle is ruled out before the
  // probe rather than by it. The probe then covers read-only volumes and
  // installs the user has no write access to.
  fs::path data = Paths::dataDir();
  if (!runningFromMacBundle() && directoryIsWritable(data)) {
    return data;
  }
  fs::path userDir = userApplicationDataDir();
  std::error_code ec;
  fs::create_directories(userDir, ec);
  return userDir;
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

fs::path Paths::stateDir() {
  // Resolved once: the writability probe touches the disk, and every caller
  // (crash handler included) expects a cheap accessor. Nothing that feeds the
  // decision changes within a run.
  static const fs::path resolved = resolveStateDir();
  return resolved;
}

fs::path Paths::defaultProjectFile() {
  return stateDir() / "default.deckboy";  // auto-loaded at startup if it exists
}

bool Paths::ensureDataDir() {
  std::error_code dataEc;
  fs::create_directories(dataDir(), dataEc);  // creates all intermediate dirs; no-op if exists
  std::error_code stateEc;
  fs::create_directories(stateDir(), stateEc);

  // Versions before the split wrote the show and the last-opened pointer into
  // data/. When that is no longer where we write, carry them across once so an
  // upgrade in place doesn't look like the operator's show disappeared.
  if (!dataEc && !stateEc && stateDir() != dataDir()) {
    // The show file is COPIED, so carrying it across is harmless: the new
    // state dir gets its own.
    //
    // last_project.txt is NOT a copy of anything -- it is an ABSOLUTE PATH,
    // and usually a path to data/default.deckboy. Migrating it into a state
    // dir that was forced by DECKBOY_STATE_DIR hands an "isolated" run a
    // pointer straight back out to the operator's real show, which it then
    // opens, edits and autosaves over. The isolation looks like it took and
    // does not, which is the worst way for it to fail. That happened, and it
    // cost a live show file.
    //
    // So the pointer travels only when the state dir MOVED ON ITS OWN -- the
    // upgrade-in-place case the migration exists for, where the operator
    // genuinely wants their last show back.
    const bool isolated = stateDirWasOverridden();
    for (const char* name : {"default.deckboy", "last_project.txt"}) {
      if (isolated && std::string(name) == "last_project.txt") {
        continue;
      }
      std::error_code ec;
      fs::path from = dataDir() / name;
      fs::path to = stateDir() / name;
      if (fs::exists(from, ec) && !fs::exists(to, ec)) {
        fs::copy_file(from, to, ec);
      }
    }
  }
  return !dataEc && !stateEc;
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
        // Liberation Sans FIRST, DejaVu after it.
        //
        // The UI's fixed-width chips and button labels were laid out against
        // Segoe UI on Windows. DejaVu Sans is markedly wider, so on Linux the
        // same labels ellipsized where Windows showed them in full — "OUTPUT"
        // became "OUTP...", "deck fader" became "deck fad...", PASTE and RESET
        // lost their tails. Same layout, same window size, different font
        // metrics.
        //
        // Liberation Sans is metric-compatible with Arial and close enough to
        // Segoe UI's width to fit the existing layout. DejaVu remains as the
        // fallback for distros that lack Liberation, where the labels shorten
        // but nothing breaks.
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
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
        // Liberation Mono first, matching the Sans case above: it is the
        // Courier-metric companion and sits closer to Consolas (the Windows
        // pick) than DejaVu Sans Mono does, so timecodes and technical
        // readouts occupy the width the layout expects.
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
        "/usr/share/fonts/liberation-mono/LiberationMono-Regular.ttf",
        "/usr/share/fonts/TTF/LiberationMono-Regular.ttf",
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
