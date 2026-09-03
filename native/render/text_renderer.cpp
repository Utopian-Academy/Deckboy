// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// text_renderer.cpp — text rendering implementation.
//
// All methods follow the same pattern: render UTF-8 text to an SDL_Surface
// via TTF_RenderText_Blended(anti-aliased, alpha-blended, 0), convert to an
// SDL_Texture, blit to the renderer, then clean up the temporary surface.
//
// Note: drawText() and drawCenteredText() create and destroy a texture per
// call. For text that doesn't change every frame, use textToTexture() to
// cache the result and SDL_RenderTexture it directly.
//
// Header: text_renderer.hpp
// ============================================================================

#include "render/text_renderer.hpp"
#include "core/sdl_compat.hpp"
#include <SDL3_ttf/SDL_ttf.h>

namespace deckboy::render {

// Render text at an absolute pixel position. Creates a temporary texture
// from the TTF surface, blits it, and destroys both immediately.
void TextRenderer::drawText(SDL_Renderer* renderer, TTF_Font* font,
                            const std::string& text, const SDL_Color& color,
                            int x, int y) {
  if (!font || text.empty()) {
    return;
  }

  SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), 0, color);
  if (!surface) {
    return;
  }

  SDL_Texture* texture = deckboyCreateTextureFromSurface(renderer, surface);
  if (!texture) {
    SDL_DestroySurface(surface);
    return;
  }

  SDL_Rect dst{x, y, surface->w, surface->h};
  SDL_DestroySurface(surface);
  SDL_RenderTexture(renderer, texture, nullptr, &dst);
  SDL_DestroyTexture(texture);
}

// Render text centered both horizontally and vertically within a bounding rect.
// Calculates the offset from the surface dimensions after rendering.
void TextRenderer::drawCenteredText(SDL_Renderer* renderer, TTF_Font* font,
                                    const std::string& text,
                                    const SDL_Color& color,
                                    const SDL_Rect& bounds) {
  if (!font || text.empty()) {
    return;
  }

  SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), 0, color);
  if (!surface) {
    return;
  }

  SDL_Texture* texture = deckboyCreateTextureFromSurface(renderer, surface);
  if (!texture) {
    SDL_DestroySurface(surface);
    return;
  }

  SDL_Rect dst{
    bounds.x + (bounds.w - surface->w) / 2,
    bounds.y + (bounds.h - surface->h) / 2,
    surface->w,
    surface->h
  };

  SDL_DestroySurface(surface);
  SDL_RenderTexture(renderer, texture, nullptr, &dst);
  SDL_DestroyTexture(texture);
}

// Measure text dimensions without rendering. TTF_GetStringSize calculates
// the bounding box the rendered string would occupy.
void TextRenderer::getTextDimensions(TTF_Font* font, const std::string& text,
                                     int& outWidth, int& outHeight) {
  outWidth = 0;
  outHeight = 0;

  if (!font || text.empty()) {
    return;
  }

  TTF_GetStringSize(font, text.c_str(), 0, &outWidth, &outHeight);
}

// Create a persistent SDL_Texture from text for caching. The caller owns
// the returned texture and must call SDL_DestroyTexture when done.
// Returns nullptr if font is null or text is empty.
SDL_Texture* TextRenderer::textToTexture(SDL_Renderer* renderer, TTF_Font* font,
                                         const std::string& text,
                                         const SDL_Color& color) {
  if (!font || text.empty()) {
    return nullptr;
  }

  SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), 0, color);
  if (!surface) {
    return nullptr;
  }

  SDL_Texture* texture = deckboyCreateTextureFromSurface(renderer, surface);
  SDL_DestroySurface(surface);

  return texture;  // Caller responsible for SDL_DestroyTexture
}

}  // namespace deckboy::render
