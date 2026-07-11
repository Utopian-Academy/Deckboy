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

// ---------------------------------------------------------------------------
// Readability enforcement (v0.78.12). Theme files are free-form and several
// shipped with text-role tones nearly identical to their background-role
// tones (dark-green text on deep-green rows: unreadable). Rather than
// hand-tune 24 themes, enforce minimum WCAG-style contrast between the
// role pairs the UI actually draws, nudging the offending tone toward
// black/white until legible. Hues survive — only lightness moves, and only
// as far as needed, so compliant themes are untouched.
// ---------------------------------------------------------------------------
inline double paletteRelativeLuminance(const SDL_Color& c) {
  auto lin = [](double v) {
    v /= 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

inline double paletteContrastRatio(const SDL_Color& a, const SDL_Color& b) {
  double la = paletteRelativeLuminance(a);
  double lb = paletteRelativeLuminance(b);
  double hi = la > lb ? la : lb;
  double lo = la > lb ? lb : la;
  return (hi + 0.05) / (lo + 0.05);
}

// Move `text` toward white or black (whichever side of `bg` it already
// leans) until it clears `minRatio` against bg. Steps are small so the
// theme's hue character is kept as much as possible.
inline void palettePushApart(SDL_Color& text, const SDL_Color& bg, double minRatio) {
  bool lighten = paletteRelativeLuminance(text) >= paletteRelativeLuminance(bg);
  for (int step = 0; step < 24 && paletteContrastRatio(text, bg) < minRatio; ++step) {
    auto nudge = [&](Uint8 v) {
      int goal = lighten ? 255 : 0;
      return static_cast<Uint8>(v + (goal - static_cast<int>(v)) * 15 / 100);
    };
    text.r = nudge(text.r);
    text.g = nudge(text.g);
    text.b = nudge(text.b);
  }
}

inline void enforcePaletteReadability() {
  // Conservative global pass — only the ink roles move, never chrome fills
  // (warping fills wrecks theme identity; famicom's dialog went grey).
  // Per-draw safety is handled by readableInkOn() below.
  palettePushApart(pal.light, pal.deep, 4.5);
  palettePushApart(pal.dark, pal.light, 3.0);
  palettePushApart(pal.inkSoft, pal.deep, 3.0);
}

// ---------------------------------------------------------------------------
// readableInkOn — Per-draw readability guard. Returns `preferred` when it
// already reads against `bg` (contrast ≥ minRatio); otherwise falls back to
// whichever of the palette's extreme inks contrasts best, and to pure
// black/white if even those fail. Text sites on theme- or cue-colored fills
// (cue rows, dialogs) route their ink through this so NO theme or color tag
// can produce invisible text, while compliant themes render untouched.
// ---------------------------------------------------------------------------
inline SDL_Color readableInkOn(const SDL_Color& bg, const SDL_Color& preferred,
                               double minRatio = 3.0) {
  if (paletteContrastRatio(preferred, bg) >= minRatio) {
    return preferred;
  }
  SDL_Color best = paletteContrastRatio(pal.light, bg) >= paletteContrastRatio(pal.deep, bg)
                     ? pal.light : pal.deep;
  if (paletteContrastRatio(best, bg) >= minRatio) {
    return best;
  }
  return paletteRelativeLuminance(bg) < 0.35 ? SDL_Color {245, 245, 245, preferred.a}
                                             : SDL_Color {12, 12, 12, preferred.a};
}

// Unpack all theme constants into the global `pal` instance.
// Must be called once during app initialization (after SDL_Init, before
// any rendering). Called again whenever a theme is applied at runtime.
// Ends with the readability pass so no theme can ship unreadable text.
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
  enforcePaletteReadability();
}
