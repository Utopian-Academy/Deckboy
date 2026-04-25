// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// primitives.cpp — SDL2 drawing primitives implementation.
//
// Implements the stateless Primitives class for basic 2D rendering operations.
// The drawFramedPanel() bevel effect auto-detects raised vs. sunken style
// by comparing the luminance of the body and inner border colors.
//
// Header: primitives.hpp
// Used by: waveform_renderer.cpp, app_render_*.ipp, main.cpp UI drawing.
// ============================================================================

#include "primitives.hpp"

#include <algorithm>

namespace deckboy::render {

// Helper: shrink a rectangle by N pixels on all sides.
static SDL_Rect insetRect(const SDL_Rect& rect, int inset) {
  return SDL_Rect{
    rect.x + inset,
    rect.y + inset,
    std::max(0, rect.w - 2 * inset),
    std::max(0, rect.h - 2 * inset)
  };
}

void Primitives::fillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(renderer, &rect);
}

void Primitives::strokeRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawRect(renderer, &rect);
}

// Draws a panel with a 3D bevel effect:
//   1. Fill the body rectangle with the body color
//   2. Draw a 1px outer border
//   3. Inset by 2px and draw highlight (top/left) + shadow (bottom/right)
// The bevel direction is auto-detected from relative luminance.
void Primitives::drawFramedPanel(SDL_Renderer* renderer, const SDL_Rect& rect,
                                  SDL_Color body, SDL_Color border, SDL_Color innerBorder) {
  fillRect(renderer, rect, body);
  strokeRect(renderer, rect, border);
  SDL_Rect inner = insetRect(rect, 2);
  if (inner.w > 2 && inner.h > 2) {
    // Compare luminance to determine bevel direction:
    // brighter innerBorder → raised panel, darker → sunken panel
    int bodyLuma = body.r + body.g + body.b;
    int innerLuma = innerBorder.r + innerBorder.g + innerBorder.b;
    bool raised = (innerLuma >= bodyLuma);
    SDL_Color hi = raised ? innerBorder : body;  // Highlight (top-left edges)
    SDL_Color lo = {  // Shadow (bottom-right edges, darkened by 1/3)
      static_cast<Uint8>(std::min(255, (raised ? body.r : innerBorder.r) * 2 / 3)),
      static_cast<Uint8>(std::min(255, (raised ? body.g : innerBorder.g) * 2 / 3)),
      static_cast<Uint8>(std::min(255, (raised ? body.b : innerBorder.b) * 2 / 3)),
      (raised ? body : innerBorder).a
    };
    int x1 = inner.x, y1 = inner.y;
    int x2 = inner.x + inner.w - 1, y2 = inner.y + inner.h - 1;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, hi.r, hi.g, hi.b, hi.a);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y1);
    SDL_RenderDrawLine(renderer, x1, y1, x1, y2);
    SDL_SetRenderDrawColor(renderer, lo.r, lo.g, lo.b, lo.a);
    SDL_RenderDrawLine(renderer, x1, y2, x2, y2);
    SDL_RenderDrawLine(renderer, x2, y1, x2, y2);
  }
}

void Primitives::drawSpeakerGrille(SDL_Renderer* renderer, int x, int y, 
                                    int width, int bars, SDL_Color color) {
  for (int index = 0; index < bars; ++index) {
    SDL_Rect slot{x, y + index * 7, width, 3};
    fillRect(renderer, slot, color);
  }
}

}  // namespace deckboy::render
