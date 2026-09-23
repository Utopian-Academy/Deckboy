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

// The fill for a two-state control. ON IS THE LIT ONE, AND IT IS LIT IN EVERY
// THEME -- which is harder than it sounds, and has now been got wrong twice in
// opposite directions.
//
// v0.99.339 made the lit fill `pal.light` on the grounds that a switched-on row
// should be the easiest to read. That was right in principle and broken in
// practice: `pal.light` is what panels and unlit tiles are already made of, so
// on half the bundled themes the two states came out the SAME COLOUR. It was
// reverted to ON = `pal.dark`, OFF = `pal.tile`, which is unmistakable -- but
// only on themes where `tile` is recessive. On a terminal theme `tile` IS the
// bright one, so an operator looking at this reads every OFF row as lit and
// every ON row as dark. James, looking at exactly that: "the cue toggles seem
// lit when deactivated and dark when activated, this seems inverted to me."
//
// Both attempts picked a fixed pair of palette roles and hoped they landed the
// right way round. They cannot: the roles mean different brightnesses in
// different themes. So pick by BRIGHTNESS, which is the thing the eye is
// actually reading, and guarantee the gap.
namespace detail {

inline int paletteLuma(SDL_Color c) {
  // Rec. 601, which is close enough for "which of these two looks brighter"
  // and needs no floating point.
  return (299 * c.r + 587 * c.g + 114 * c.b) / 1000;
}

}  // namespace detail

inline SDL_Color paletteToggleFill(bool on) {
  // The brightest and the dimmest fills the palette offers for a small raised
  // control. ON takes the bright end, OFF the dim end, whatever those happen
  // to be called in this theme.
  const SDL_Color candidates[] = {pal.light, pal.tile, pal.dark, pal.deep};
  const SDL_Color* brightest = &candidates[0];
  const SDL_Color* dimmest = &candidates[0];
  for (const SDL_Color& c : candidates) {
    if (detail::paletteLuma(c) > detail::paletteLuma(*brightest)) brightest = &c;
    if (detail::paletteLuma(c) < detail::paletteLuma(*dimmest)) dimmest = &c;
  }
  // AND THE GAP IS GUARANTEED. If a theme's fills are all close together,
  // the unlit one is pushed further from the lit one rather than left a shade
  // apart -- a difference nobody can see is the same bug in a third form.
  if (!on && paletteColorDistance(*brightest, *dimmest) < 90) {
    return paletteMix(*dimmest, detail::paletteLuma(*brightest) > 128
                                  ? SDL_Color {0, 0, 0, 255}
                                  : SDL_Color {255, 255, 255, 255}, 0.35);
  }
  return on ? *brightest : *dimmest;
}

// The ink that belongs on that fill, kept beside it so a caller cannot pair a
// lit fill with the ink for an unlit one. Chosen for contrast against the fill
// rather than assumed, for the same reason the fill is.
inline SDL_Color paletteToggleInk(bool on) {
  const SDL_Color fill = paletteToggleFill(on);
  return detail::paletteLuma(fill) > 128 ? pal.deep : pal.light;
}
