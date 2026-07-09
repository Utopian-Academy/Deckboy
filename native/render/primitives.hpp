// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// primitives.hpp — Low-level SDL2 drawing primitives for the Deckboy UI.
//
// Stateless utility class providing basic 2D drawing operations used by
// all Deckboy UI components. All methods are static — no state is held.
//
//   fillRect()        — solid color rectangle fill (alpha-blended)
//   strokeRect()      — rectangle outline (1px border)
//   drawFramedPanel() — beveled panel: fill + border + highlight/shadow edges
//   drawSpeakerGrille() — decorative horizontal bar pattern (cosmetic detail)
//
// Implementation: primitives.cpp
// Used by: waveform_renderer.cpp, app_render_*.ipp, main.cpp UI drawing.
// ============================================================================

#pragma once

#include "core/sdl_compat.hpp"

namespace deckboy::render {

class Primitives {
 public:
  // Fill a rectangle with solid color (sets SDL_BLENDMODE_BLEND for alpha)
  static void fillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color);

  // Draw 1px rectangle outline (sets SDL_BLENDMODE_BLEND for alpha)
  static void strokeRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color);

  // Draw a beveled panel: solid body fill, 1px outer border, then inset
  // highlight/shadow edges for a raised or sunken 3D effect. The bevel
  // direction is auto-detected: if innerBorder is brighter than body,
  // the panel appears raised; if darker, it appears inset.
  static void drawFramedPanel(SDL_Renderer* renderer, const SDL_Rect& rect,
                              SDL_Color body, SDL_Color border, SDL_Color innerBorder);

  // Draw a decorative speaker grille pattern: evenly-spaced horizontal bars.
  // Each bar is 3px tall with 4px gaps (7px pitch). Used for cosmetic detail
  // in the deck output display area.
  static void drawSpeakerGrille(SDL_Renderer* renderer, int x, int y,
                                int width, int bars, SDL_Color color);
};

}  // namespace deckboy::render
