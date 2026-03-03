// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Playboy contributors

#include "render/text_renderer.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

namespace playboy::render {

void TextRenderer::drawText(SDL_Renderer* renderer, TTF_Font* font,
                            const std::string& text, const SDL_Color& color,
                            int x, int y) {
  if (!font || text.empty()) {
    return;
  }

  SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
  if (!surface) {
    return;
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (!texture) {
    SDL_FreeSurface(surface);
    return;
  }

  SDL_Rect dst{x, y, surface->w, surface->h};
  SDL_FreeSurface(surface);
  SDL_RenderCopy(renderer, texture, nullptr, &dst);
  SDL_DestroyTexture(texture);
}

void TextRenderer::drawCenteredText(SDL_Renderer* renderer, TTF_Font* font,
                                    const std::string& text,
                                    const SDL_Color& color,
                                    const SDL_Rect& bounds) {
  if (!font || text.empty()) {
    return;
  }

  SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
  if (!surface) {
    return;
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (!texture) {
    SDL_FreeSurface(surface);
    return;
  }

  SDL_Rect dst{
    bounds.x + (bounds.w - surface->w) / 2,
    bounds.y + (bounds.h - surface->h) / 2,
    surface->w,
    surface->h
  };

  SDL_FreeSurface(surface);
  SDL_RenderCopy(renderer, texture, nullptr, &dst);
  SDL_DestroyTexture(texture);
}

void TextRenderer::getTextDimensions(TTF_Font* font, const std::string& text,
                                     int& outWidth, int& outHeight) {
  outWidth = 0;
  outHeight = 0;

  if (!font || text.empty()) {
    return;
  }

  TTF_SizeUTF8(font, text.c_str(), &outWidth, &outHeight);
}

SDL_Texture* TextRenderer::textToTexture(SDL_Renderer* renderer, TTF_Font* font,
                                         const std::string& text,
                                         const SDL_Color& color) {
  if (!font || text.empty()) {
    return nullptr;
  }

  SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
  if (!surface) {
    return nullptr;
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);

  return texture;  // Caller responsible for SDL_DestroyTexture
}

}  // namespace playboy::render
