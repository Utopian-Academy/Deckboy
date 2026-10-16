// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#pragma once
#include "core/sdl_compat.hpp"
#include "core/paths.hpp"

namespace deckboy::platform {
// Native window decoration, not desk layout. Raster sizes are asset resolutions.
// BMP avoids starting ffmpeg or depending on an optional image decoder at boot.
inline void applyPortableWindowIcon(SDL_Window* window) {
  if (!window) return;
#ifdef __APPLE__
  const auto executable = core::Paths::executablePath();
  if (executable.parent_path().filename() == "MacOS" &&
      executable.parent_path().parent_path().filename() == "Contents") {
    // Cocoa sets the process-wide Dock icon here. A bundle already supplies
    // its ICNS/Assets.car; replacing it would discard the native macOS icon.
    return;
  }
#endif
  const auto directory = core::Paths::dataDir() / "icons";
  SDL_Surface* base = nullptr;
  for (const int pixels : {32, 48, 64, 128}) {
    const auto file = directory / ("deckboy-" + std::to_string(pixels) + ".bmp");
    const auto utf8 = file.u8string();
    SDL_Surface* surface = SDL_LoadBMP(reinterpret_cast<const char*>(utf8.c_str()));
    if (!surface) continue;
    if (!base) {
      base = surface;
    } else {
      SDL_AddSurfaceAlternateImage(base, surface);
      SDL_DestroySurface(surface); // the base retains its own reference
    }
  }
  if (base) {
    SDL_SetWindowIcon(window, base);
    SDL_DestroySurface(base);
  }
}
} // namespace deckboy::platform
