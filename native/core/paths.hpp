// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// paths.hpp — Portable path resolution for project root, data dir, and fonts.
//
// This abstraction ensures Deckboy can find its assets (fonts, default show
// file, data directory) regardless of where the binary is launched from.
//
// Resolution priority (project root):
//   1. DECKBOY_ROOT environment variable (explicit override)
//   2. Walk up from executable directory, skipping build/bin subdirs
//   3. Current working directory (fallback)
//
// Used by:
//   - main.cpp        → font loading, default project load
//   - media_engine.*  → (indirectly) for resolving relative media paths
//   - app_project_state.ipp → default save/load location
//
// Implementation: paths.cpp (platform-specific exe path discovery).
// ============================================================================

#ifndef DECKBOY_CORE_PATHS_HPP
#define DECKBOY_CORE_PATHS_HPP

#include <filesystem>
#include <string>

namespace deckboy {
namespace core {

/// Portable path resolution. No hardcoded machine paths.
/// Resolution order: env vars first, then executable-relative, then cwd.
struct Paths {
  /// Project root directory. This is the anchor for all relative paths.
  /// Order: DECKBOY_ROOT env → walk up from exe dir (skip bin/build) → cwd.
  static std::filesystem::path projectRoot();

  /// Absolute path to the running executable, or empty if the platform
  /// doesn't support exe path discovery (rare; handled gracefully by callers).
  static std::filesystem::path executablePath();

  /// Data directory (project_root / "data"). Contains fonts, themes, UI sounds
  /// and the other bundled assets. READ-ONLY as far as the app is concerned —
  /// inside a macOS .app this is Contents/Resources/data, which is covered by
  /// the code signature. Write to stateDir() instead.
  static std::filesystem::path dataDir();

  /// Where the app WRITES: the default show, the last-opened pointer, crash
  /// logs. Usually dataDir() itself, which keeps a portable install portable —
  /// but never a directory the app must not modify or cannot write to.
  ///
  /// Order:
  ///   1. DECKBOY_STATE_DIR env (explicit override)
  ///   2. dataDir(), unless we are inside a macOS .app bundle (writing there
  ///      breaks the code signature) or it is not writable (read-only volume,
  ///      install the user has no rights to)
  ///   3. per-user application data (%APPDATA%, ~/Library/Application Support,
  ///      XDG_DATA_HOME)
  static std::filesystem::path stateDir();

  /// Default show file path: state_dir / "default.deckboy".
  /// Loaded automatically at startup if it exists.
  static std::filesystem::path defaultProjectFile();

  /// Create the data and state directories if they don't exist.
  /// Returns false on failure. Called once during app initialization.
  static bool ensureDataDir();

  /// Resolve a font file path for a logical font name.
  /// Search order:
  ///   1. DECKBOY_FONT_<NAME> env var (e.g. DECKBOY_FONT_SANS="/path/to/font.ttf")
  ///   2. data_dir / <candidate filename> (bundled fonts)
  ///   3. XDG_DATA_HOME / deckboy / fonts / <candidate> (Linux user fonts)
  ///   4. Platform system font directories (Windows Fonts, macOS Library, etc.)
  /// Returns the first path that exists, or a reasonable default (caller should
  /// check existence before opening).
  enum class FontName {
    Sans,   // proportional UI font (DejaVu Sans / Segoe UI / Arial)
    Mono,   // monospaced font for timecodes and data (Consolas / Menlo)
    Pixel   // retro pixel font for branding (PressStart2P)
  };
  static std::filesystem::path fontPath(FontName name);

  /// Normalize a project file path for save/load:
  ///   - Empty path → default project file location
  ///   - Missing extension → append ".deckboy"
  static std::filesystem::path normalizeProjectPath(const std::filesystem::path& path);
};

}  // namespace core
}  // namespace deckboy

#endif
