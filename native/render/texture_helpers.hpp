// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// texture_helpers.hpp — SDL texture lifecycle utilities.
//
// Two helpers, by intent:
//
//   syncTexture(...)      — raw bytes + pitch, always RGBA32. Use for
//                           pixel data the engine produced itself (thumbnails,
//                           timeline strips, captured output readbacks).
//
//   syncFrameTexture(...) — takes a DecodedFrame, picks the matching SDL
//                           pixel format from frame.format, and recreates
//                           the texture when format OR dimensions change.
//                           Use for live decoded frames where the format
//                           is allowed to be NV12.
//
// Both lazily recreate the texture only when something actually changed,
// so a steady stream of same-shape frames pays only the upload cost.
//
// Header-only (inline). No .cpp counterpart.
// Used by: media_engine.cpp (uploadFrame), app_render_output.ipp (bridge
// textures), app_project_state.ipp (preview/timeline/thumbnail textures),
// app_update.ipp (control-window preview).
// ============================================================================

#pragma once

#include "core/sdl_compat.hpp"

#include "core/types.hpp"

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
    tex = deckboyCreateTexture(renderer,
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

// Synchronize an SDL texture with a DecodedFrame, honoring its pixel format.
// Recreates the texture when width, height, OR pixel format changes (e.g. a
// cue switch from RGBA→NV12). Returns true on success.
//
// For NV12 frames the Y plane lives at the start of frame.pixels and the
// interleaved UV plane follows after width*height bytes — exactly the
// layout SDL_UpdateNVTexture expects.
//
// Parameters:
//   renderer     — the SDL renderer that owns the texture
//   tex          — [in/out] the texture pointer (created/destroyed as needed)
//   cachedW/H    — [in/out] the dimensions of the current texture
//   cachedFmt    — [in/out] the SDL pixel format of the current texture
//   frame        — the source DecodedFrame; format must match `pixels` layout
inline bool syncFrameTexture(SDL_Renderer* renderer,
                             SDL_Texture*& tex,
                             int& cachedW,
                             int& cachedH,
                             Uint32& cachedFmt,
                             const DecodedFrame& frame) {
  if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) return false;
  const Uint32 wantFmt = sdlPixelFormat(frame.format);
  if (tex && (cachedW != frame.width || cachedH != frame.height || cachedFmt != wantFmt)) {
    SDL_DestroyTexture(tex);
    tex = nullptr;
  }
  if (!tex) {
    tex = deckboyCreateTexture(renderer, wantFmt, SDL_TEXTUREACCESS_STREAMING,
                            frame.width, frame.height);
    if (!tex) return false;
    cachedW = frame.width;
    cachedH = frame.height;
    cachedFmt = wantFmt;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  }
  if (frame.format == FramePixelFormat::NV12) {
    const Uint8* y = frame.pixels.data();
    const Uint8* uv = y + static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
    SDL_UpdateNVTexture(tex, nullptr, y, frame.width, uv, frame.width);
  } else {
    SDL_UpdateTexture(tex, nullptr, frame.pixels.data(), frame.width * 4);
  }
  return true;
}
