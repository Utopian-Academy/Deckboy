/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Deckboy_0.01 - Render Primitives Implementation
 * Copyright 2025 James
 */

#include "primitives.hpp"

#include <algorithm>

namespace deckboy::render {

// Inline helper: inset a rectangle by N pixels
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

void Primitives::drawFramedPanel(SDL_Renderer* renderer, const SDL_Rect& rect,
                                  SDL_Color body, SDL_Color border, SDL_Color innerBorder) {
  fillRect(renderer, rect, body);
  strokeRect(renderer, rect, border);
  SDL_Rect inner = insetRect(rect, 2);
  if (inner.w > 2 && inner.h > 2) {
    // Bevel: innerBorder brighter than body → raised, darker → inset
    int bodyLuma = body.r + body.g + body.b;
    int innerLuma = innerBorder.r + innerBorder.g + innerBorder.b;
    bool raised = (innerLuma >= bodyLuma);
    SDL_Color hi = raised ? innerBorder : body;
    SDL_Color lo = {
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
