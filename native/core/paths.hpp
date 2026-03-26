// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.


#ifndef DECKBOY_CORE_PATHS_HPP
#define DECKBOY_CORE_PATHS_HPP

#include <filesystem>
#include <string>

namespace deckboy {
namespace core {

/// Portable path resolution. No hardcoded machine paths.
/// Resolution order: env vars first, then executable-relative, then cwd.
struct Paths {
  /// Project root. Order: DECKBOY_ROOT env → dir containing executable (or its parent if exe is in bin/build) → cwd.
  static std::filesystem::path projectRoot();

  /// Path to the running executable, or empty if unavailable (e.g. on some platforms).
  static std::filesystem::path executablePath();

  /// Data directory: project_root / "data". Created if missing when calling ensureDataDir().
  static std::filesystem::path dataDir();

  /// Default show file path: data_dir / "default.deckboy".
  static std::filesystem::path defaultProjectFile();

  /// Ensure data directory exists; returns false on failure.
  static bool ensureDataDir();

  /// Font path for a given logical name. Tries, in order:
  /// - DECKBOY_FONT_<NAME> env (e.g. DECKBOY_FONT_SANS)
  /// - data_dir / <candidate> for each candidate
  /// - system paths (e.g. /usr/share/fonts/...)
  /// Returns first path that exists, or a default that may not exist (caller can check).
  enum class FontName { Sans, Mono, Pixel };
  static std::filesystem::path fontPath(FontName name);

  /// Normalize project path: empty -> default; no extension -> append .deckboy.
  static std::filesystem::path normalizeProjectPath(const std::filesystem::path& path);
};

}  // namespace core
}  // namespace deckboy

#endif
