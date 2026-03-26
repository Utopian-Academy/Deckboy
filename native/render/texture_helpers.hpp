// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <SDL.h>

// Synchronise an SDL texture with new pixel data, recreating only when the
// dimensions change.  Returns true when the texture holds valid, up-to-date
// pixels; false on invalid input or allocation failure.
//
// Usage:
//   syncTexture(renderer, tex, cachedW, cachedH, newW, newH, pixels, pitch);
//
// The function handles:
//   - Early-out when dimensions or pixels are invalid
//   - Destroying and recreating the texture when the size changes
//   - Uploading the pixel data via SDL_UpdateTexture
inline bool syncTexture(SDL_Renderer* renderer,
                        SDL_Texture*& tex,
                        int& cachedW,
                        int& cachedH,
                        int newW,
                        int newH,
                        const void* pixels,
                        int pitch) {
  if (newW <= 0 || newH <= 0 || !pixels) return false;
  if (tex && (cachedW != newW || cachedH != newH)) {
    SDL_DestroyTexture(tex);
    tex = nullptr;
  }
  if (!tex) {
    tex = SDL_CreateTexture(renderer,
                            SDL_PIXELFORMAT_RGBA32,
                            SDL_TEXTUREACCESS_STREAMING,
                            newW, newH);
    if (!tex) return false;
    cachedW = newW;
    cachedH = newH;
  }
  SDL_UpdateTexture(tex, nullptr, pixels, pitch);
  return true;
}
