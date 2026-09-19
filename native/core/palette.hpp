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
#include <algorithm>
#include <cmath>
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
  SDL_Color deep;            // LCD deepest — panel backgrounds + dark ink on bright fills
  SDL_Color fg;              // on-body text ink (bright in terminal themes; = deep in light)
  SDL_Color tile;            // interactive tile fill (dark in terminal themes; = light in light)
  SDL_Color fgSoft;          // secondary on-tile ink (bright-muted in terminal; = dark in light)
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
// any rendering). Called again whenever a theme is applied at runtime.
//
// NOTE (v0.78.12): readability is a THEME-DATA contract, not a code fix —
// theme.txt authors must keep ink roles legible on the fills they're drawn
// over (deep/dark ink on shell_inner + screen_light fills; light ink on
// deep). tools/audit_theme_contrast.ps1 checks every theme against the
// pairs the UI draws and lists failures; run it after editing any theme.
inline void rebuildPalette() {
  pal.shellOuter  = paletteColorFromRgba(kShellOuterColor);
  pal.shellInner  = paletteColorFromRgba(kShellInnerColor);
  pal.shellShadow = paletteColorFromRgba(kShellShadowColor);
  pal.light       = paletteColorFromRgba(kScreenLightColor);
  pal.mid         = paletteColorFromRgba(kScreenMidColor);
  pal.dark        = paletteColorFromRgba(kScreenDarkColor);
  pal.deep        = paletteColorFromRgba(kScreenDeepColor);
  pal.fg          = paletteColorFromRgba(kScreenFgColor);
  pal.tile        = paletteColorFromRgba(kScreenTileColor);
  pal.fgSoft      = paletteColorFromRgba(kScreenFgSoftColor);
  pal.inkSoft     = paletteColorFromRgba(kScreenInkSoftColor);
  pal.buttonBezel = paletteColorFromRgba(kButtonBezelColor);
  pal.deleteBezel = paletteColorFromRgba(kDeleteBezelColor);
}

// ── A LIT CONTROL HAS TO LOOK LIT ───────────────────────────────────────────
//
// Toggles draw their ON state with `pal.light` and their OFF state with
// `pal.tile` -- and `screen_tile` DEFAULTS TO `screen_light`, deliberately, so
// that a light theme which never heard of the tile role is unchanged. The two
// roles therefore hold the SAME COLOUR on every such theme, which made every
// toggle in the inspector and the settings modal identical in both states:
// only the word inside it changed.
//
// MEASURED on the default theme: the lit "hold" pill and an unlit "loop" pill
// were both RGB(140,174,15) -- a difference of zero. Reported from a show as
// "the cue state toggles aren't lighting up when enabled" and "I'm clicking
// pause on last frame and there's no visible change".
//
// It arrived with f44bfbc (2026-09-12, v0.99.339), which changed the lit fill
// from `pal.dark` to `pal.light` so that a switched-on row would be the EASIEST
// to read rather than the hardest. That intent is right and is kept; what was
// missing is that the unlit state then has nothing to be brighter THAN. So when
// a theme does not separate the two roles itself, the unlit fill recedes toward
// the panel instead.
inline SDL_Color paletteMix(SDL_Color a, SDL_Color b, double t) {
  const double k = std::clamp(t, 0.0, 1.0);
  auto lerp = [&](Uint8 x, Uint8 y) {
    return static_cast<Uint8>(std::lround(x * (1.0 - k) + y * k));
  };
  return {lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), a.a};
}

inline int paletteColorDistance(SDL_Color a, SDL_Color b) {
  return std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)) +
         std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)) +
         std::abs(static_cast<int>(a.b) - static_cast<int>(b.b));
}

// The fill for a two-state control. ON IS INVERTED, NOT MERELY BRIGHTER.
//
// v0.99.339 changed the lit fill from `pal.dark` to `pal.light` on the grounds
// that a switched-on row should be the easiest to read rather than the hardest.
// That reasoning is fine in isolation and wrong in practice: `pal.light` is
// what the panel and the unlit tiles are already made of, so "brighter" had
// nothing to be brighter than, and on half the bundled themes the two states
// came out the SAME COLOUR. Even once the unlit state was receded to restore a
// difference, James's verdict on looking at it was that the older colours were
// better and that on and off have to be CLEAR. His call, and it is the right
// one: a lit control should be unmistakable across a dark room at a glance,
// not a shade apart from its neighbour.
//
// So ON is the deep fill with light ink, OFF is the tile with its normal ink --
// the two furthest-apart pairs the palette has, in every theme.
inline SDL_Color paletteToggleFill(bool on) {
  return on ? pal.dark : pal.tile;
}

// The ink that belongs on that fill, kept beside it so a caller cannot pair a
// lit fill with the ink for an unlit one.
inline SDL_Color paletteToggleInk(bool on) {
  return on ? pal.light : pal.fg;
}
