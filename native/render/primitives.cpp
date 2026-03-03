/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Playboy_0.01 - Render Primitives Implementation
 * Copyright 2025 James
 */

#include "primitives.hpp"

namespace playboy::render {

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
  if (inner.w > 0 && inner.h > 0) {
    strokeRect(renderer, inner, innerBorder);
  }
}

void Primitives::drawSpeakerGrille(SDL_Renderer* renderer, int x, int y, 
                                    int width, int bars, SDL_Color color) {
  for (int index = 0; index < bars; ++index) {
    SDL_Rect slot{x, y + index * 7, width, 3};
    fillRect(renderer, slot, color);
  }
}

}  // namespace playboy::render
