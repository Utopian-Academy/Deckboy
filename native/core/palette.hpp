// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <SDL.h>
#include <cstdint>
#include "constants.hpp"

struct Palette {
  SDL_Color shellOuter;
  SDL_Color shellInner;
  SDL_Color shellShadow;
  SDL_Color light;
  SDL_Color mid;
  SDL_Color dark;
  SDL_Color deep;
  SDL_Color inkSoft;
  SDL_Color buttonBezel;
  SDL_Color deleteBezel;
  Uint8 scanlineAlpha = 18;
};

inline Palette pal;

inline SDL_Color paletteColorFromRgba(std::uint32_t rgba) {
  return {
    static_cast<Uint8>((rgba >> 24) & 0xFFu),
    static_cast<Uint8>((rgba >> 16) & 0xFFu),
    static_cast<Uint8>((rgba >> 8) & 0xFFu),
    static_cast<Uint8>(rgba & 0xFFu)
  };
}

inline void rebuildPalette() {
  pal.shellOuter  = paletteColorFromRgba(kShellOuterColor);
  pal.shellInner  = paletteColorFromRgba(kShellInnerColor);
  pal.shellShadow = paletteColorFromRgba(kShellShadowColor);
  pal.light       = paletteColorFromRgba(kScreenLightColor);
  pal.mid         = paletteColorFromRgba(kScreenMidColor);
  pal.dark        = paletteColorFromRgba(kScreenDarkColor);
  pal.deep        = paletteColorFromRgba(kScreenDeepColor);
  pal.inkSoft     = paletteColorFromRgba(kScreenInkSoftColor);
  pal.buttonBezel = paletteColorFromRgba(kButtonBezelColor);
  pal.deleteBezel = paletteColorFromRgba(kDeleteBezelColor);
}
