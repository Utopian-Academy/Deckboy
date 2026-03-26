/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Deckboy_0.01 - Render Primitives
 * Copyright 2025 James
 */

#pragma once

#include <SDL2/SDL.h>

namespace deckboy::render {

// Basic drawing primitives for SDL2
class Primitives {
 public:
  // Fill a rectangle with solid color (with alpha blending)
  static void fillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color);

  // Draw rectangle outline
  static void strokeRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color);

  // Draw framed panel (body fill + border + inner border)
  static void drawFramedPanel(SDL_Renderer* renderer, const SDL_Rect& rect, 
                              SDL_Color body, SDL_Color border, SDL_Color innerBorder);

  // Draw speaker grille pattern (horizontal bars)
  static void drawSpeakerGrille(SDL_Renderer* renderer, int x, int y, 
                                int width, int bars, SDL_Color color);
};

}  // namespace deckboy::render
