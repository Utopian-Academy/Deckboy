// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// texture_helpers.hpp — SDL texture lifecycle utilities.
//
// Provides syncTexture(), a helper that manages the lifecycle of an
// SDL_Texture used to display dynamic pixel data (e.g. video frames,
// browser captures, pattern cues). It handles:
//
//   1. Dimension validation — rejects zero-size or null pixel data
//   2. Lazy recreation — only destroys and recreates the texture when
//      the width or height changes, avoiding GPU allocation churn
//   3. Pixel upload — copies new frame data via SDL_UpdateTexture
//
// The texture is created with SDL_TEXTUREACCESS_STREAMING and
// SDL_PIXELFORMAT_RGBA32 for raw RGBA pixel upload compatibility.
//
// Header-only (inline). No .cpp counterpart.
// Used by: media_engine.cpp (uploadFrame), app_render_output.ipp.
// ============================================================================

#pragma once

#include <SDL.h>

// Synchronize an SDL texture with new pixel data, recreating only when the
// dimensions change. Returns true when the texture holds valid, up-to-date
// pixels; false on invalid input or allocation failure.
//
// Parameters:
//   renderer     — the SDL renderer that owns the texture
//   tex          — [in/out] the texture pointer (created/destroyed as needed)
//   cachedW/H    — [in/out] the dimensions of the current texture
//   newW/H       — the dimensions of the incoming pixel data
//   pixels       — raw RGBA pixel data to upload
//   pitch        — byte stride of one row (typically newW * 4)
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
