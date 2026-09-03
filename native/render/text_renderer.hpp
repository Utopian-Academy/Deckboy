// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// text_renderer.hpp — SDL_ttf text rendering utilities.
//
// Stateless utility class for rendering UTF-8 text via SDL_ttf. All methods
// are static — no font ownership or caching is performed here (fonts are
// owned by the App class in main.cpp).
//
//   drawText()          — render text at absolute (x, y) position
//   drawCenteredText()  — render text centered within a bounding rectangle
//   getTextDimensions() — measure text size without rendering
//   textToTexture()     — create a cached SDL_Texture from text
//
// All text rendering uses TTF_RenderText_Blended for anti-aliased output.
// The text is UTF-8 and the length argument is 0, which means "to the first
// NUL" -- the SDL3 signature takes an explicit length.
// The textToTexture() method is useful for text that doesn't change every
// frame (e.g. labels) — the caller must manage the texture lifetime.
//
// Implementation: text_renderer.cpp
// Used by: waveform_renderer.cpp, app_render_*.ipp, main.cpp UI text.
// ============================================================================

#ifndef DECKBOY_RENDER_TEXT_RENDERER_HPP
#define DECKBOY_RENDER_TEXT_RENDERER_HPP

#include "core/sdl_compat.hpp"
#include <SDL3_ttf/SDL_ttf.h>
#include <string>

namespace deckboy::render {

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

}  // namespace deckboy::render

#endif  // DECKBOY_RENDER_TEXT_RENDERER_HPP
