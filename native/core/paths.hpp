#ifndef PLAYBOY_CORE_PATHS_HPP
#define PLAYBOY_CORE_PATHS_HPP

#include <filesystem>
#include <string>

namespace playboy {
namespace core {

/// Portable path resolution. No hardcoded machine paths.
/// Resolution order: env vars first, then executable-relative, then cwd.
struct Paths {
  /// Project root. Order: PLAYBOY_ROOT env → dir containing executable (or its parent if exe is in bin/build) → cwd.
  static std::filesystem::path projectRoot();

  /// Path to the running executable, or empty if unavailable (e.g. on some platforms).
  static std::filesystem::path executablePath();

  /// Data directory: project_root / "data". Created if missing when calling ensureDataDir().
  static std::filesystem::path dataDir();

  /// Default show file path: data_dir / "default.playboy".
  static std::filesystem::path defaultProjectFile();

  /// Ensure data directory exists; returns false on failure.
  static bool ensureDataDir();

  /// Font path for a given logical name. Tries, in order:
  /// - PLAYBOY_FONT_<NAME> env (e.g. PLAYBOY_FONT_SANS)
  /// - data_dir / <candidate> for each candidate
  /// - system paths (e.g. /usr/share/fonts/...)
  /// Returns first path that exists, or a default that may not exist (caller can check).
  enum class FontName { Sans, Mono, Pixel };
  static std::filesystem::path fontPath(FontName name);

  /// Normalize project path: empty -> default; no extension -> append .playboy.
  static std::filesystem::path normalizeProjectPath(const std::filesystem::path& path);
};

}  // namespace core
}  // namespace playboy

#endif
