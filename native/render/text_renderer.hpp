// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Playboy contributors

#ifndef PLAYBOY_RENDER_TEXT_RENDERER_HPP
#define PLAYBOY_RENDER_TEXT_RENDERER_HPP

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

namespace playboy::render {

// TextRenderer provides SDL2-based text rendering operations.
// This stateless utility class consolidates text drawing, font management,
// and text metrics operations, enabling easier refactoring of display code
// and supporting unit testing of text layout.
class TextRenderer {
 public:
  // Draw text at (x, y) position with specified color
  // Font and text must be non-null
  static void drawText(SDL_Renderer* renderer, TTF_Font* font,
                       const std::string& text, const SDL_Color& color,
                       int x, int y);

  // Draw text centered within rect bounds
  // Horizontally and vertically centered
  static void drawCenteredText(SDL_Renderer* renderer, TTF_Font* font,
                               const std::string& text, const SDL_Color& color,
                               const SDL_Rect& bounds);

  // Get text dimensions without rendering
  // Returns width and height in pixels
  static void getTextDimensions(TTF_Font* font, const std::string& text,
                                int& outWidth, int& outHeight);

  // Create a texture from text (caller must destroy with SDL_DestroyTexture)
  // Useful for caching text that doesn't change frequently
  static SDL_Texture* textToTexture(SDL_Renderer* renderer, TTF_Font* font,
                                    const std::string& text,
                                    const SDL_Color& color);
};

}  // namespace playboy::render

#endif  // PLAYBOY_RENDER_TEXT_RENDERER_HPP
