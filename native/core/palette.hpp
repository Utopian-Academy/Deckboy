// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// palette.hpp — Runtime color palette (pre-unpacked SDL_Color values).
//
// The theme color constants in constants.hpp are stored as packed uint32
// (0xRRGGBBAAu) for compact definition. This file unpacks them into
// SDL_Color structs at startup for direct use by the rendering code.
//
// The global `pal` instance is populated by calling rebuildPalette() once
// during app initialization (in main.cpp, after SDL_Init). If a runtime
// theme change is ever supported, rebuildPalette() would be called again
// after overwriting the kScreen*/kShell* constants.
//
// Every rendering function (primitives.*, app_render_*.ipp, text_renderer.*)
// reads from `pal` rather than unpacking the constants each frame.
// ============================================================================

#pragma once

#include "core/sdl_compat.hpp"
#include <cstdint>
#include "constants.hpp"

// ---------------------------------------------------------------------------
// Palette — Pre-unpacked SDL_Color versions of the DMG theme constants.
//
// Field names match the semantic role in the UI:
//   shell*:      outer case chrome (toolbar, panel borders)
//   light/mid:   highlight and accent colors (selected items, active buttons)
//   dark:        primary text and row background
//   deep:        deepest background (panel interiors, output preview)
//   inkSoft:     secondary/muted text
//   buttonBezel: standard button border
//   deleteBezel: destructive action button border (red-tinted)
//   scanlineAlpha: opacity for the CRT scanline overlay effect
// ---------------------------------------------------------------------------
struct Palette {
  SDL_Color shellOuter;      // outer case plastic color
  SDL_Color shellInner;      // inner case / toolbar background
  SDL_Color shellShadow;     // shadow/border for case chrome
  SDL_Color light;           // LCD lightest — highlights, active selection
  SDL_Color mid;             // LCD mid — accent, hover states
  SDL_Color dark;            // LCD dark — body text, row backgrounds
  SDL_Color deep;            // LCD deepest — panel backgrounds
  SDL_Color inkSoft;         // soft ink — secondary text, disabled items
  SDL_Color buttonBezel;     // standard button outline/border
  SDL_Color deleteBezel;     // danger (delete/destructive) button outline
  Uint8 scanlineAlpha = 18;  // alpha for the CRT scanline overlay effect
};

// Global palette instance — populated once at startup by rebuildPalette().
// All UI rendering code reads colors from this instance.
inline Palette pal;

// Unpack a single 0xRRGGBBAAu constant into an SDL_Color struct.
// Used internally by rebuildPalette() to convert each theme constant.
inline SDL_Color paletteColorFromRgba(std::uint32_t rgba) {
  return {
    static_cast<Uint8>((rgba >> 24) & 0xFFu),  // red
    static_cast<Uint8>((rgba >> 16) & 0xFFu),  // green
    static_cast<Uint8>((rgba >> 8) & 0xFFu),   // blue
    static_cast<Uint8>(rgba & 0xFFu)            // alpha
  };
}

// Unpack all theme constants into the global `pal` instance.
// Must be called once during app initialization (after SDL_Init, before
// any rendering). Would need to be called again if theme constants are
// modified at runtime.
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
