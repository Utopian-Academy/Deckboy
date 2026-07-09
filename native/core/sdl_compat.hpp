// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026
//
// SDL3 compatibility layer for the SDL2→SDL3 migration (v0.77.0).
//
// Two jobs:
//  1. Int-geometry draw overloads. Deckboy's layout structs are integer
//     SDL_Rect end to end; SDL3's draw calls take SDL_FRect. These C++
//     overloads convert at the draw boundary so the hundreds of int-rect
//     call sites stay as they are (per docs/SDL3_MIGRATION_PLAN.md §3.1).
//  2. SDL2-style display *indices* over SDL3 display IDs. Projects persist
//     outputs by display index; these helpers keep that model working by
//     mapping index ↔ SDL_DisplayID through SDL_GetDisplays() order.
//
// Every include of <SDL.h> was rewritten to include this header instead, so
// it must stay dependency-free apart from SDL3 itself.

#pragma once

#include <SDL3/SDL.h>

#include <cstddef>

// ── Draw-call overloads: int SDL_Rect → SDL_FRect at the boundary ──────────

inline SDL_FRect deckboyToFRect(const SDL_Rect& r) {
  return SDL_FRect{static_cast<float>(r.x), static_cast<float>(r.y),
                   static_cast<float>(r.w), static_cast<float>(r.h)};
}

inline bool SDL_RenderFillRect(SDL_Renderer* renderer, const SDL_Rect* rect) {
  if (!rect) {
    return SDL_RenderFillRect(renderer, static_cast<const SDL_FRect*>(nullptr));
  }
  SDL_FRect fr = deckboyToFRect(*rect);
  return SDL_RenderFillRect(renderer, &fr);
}

inline bool SDL_RenderRect(SDL_Renderer* renderer, const SDL_Rect* rect) {
  if (!rect) {
    return SDL_RenderRect(renderer, static_cast<const SDL_FRect*>(nullptr));
  }
  SDL_FRect fr = deckboyToFRect(*rect);
  return SDL_RenderRect(renderer, &fr);
}

// A literal nullptr is ambiguous between the int-rect overloads above and
// SDL3's float originals — resolve it explicitly.
inline bool SDL_RenderFillRect(SDL_Renderer* renderer, std::nullptr_t) {
  return SDL_RenderFillRect(renderer, static_cast<const SDL_FRect*>(nullptr));
}

inline bool SDL_RenderRect(SDL_Renderer* renderer, std::nullptr_t) {
  return SDL_RenderRect(renderer, static_cast<const SDL_FRect*>(nullptr));
}

inline bool SDL_RenderTexture(SDL_Renderer* renderer, SDL_Texture* texture,
                              const SDL_Rect* src, const SDL_Rect* dst) {
  SDL_FRect fsrc {};
  SDL_FRect fdst {};
  if (src) fsrc = deckboyToFRect(*src);
  if (dst) fdst = deckboyToFRect(*dst);
  return SDL_RenderTexture(renderer, texture,
                           src ? &fsrc : nullptr,
                           dst ? &fdst : nullptr);
}

inline bool SDL_RenderTextureRotated(SDL_Renderer* renderer, SDL_Texture* texture,
                                     const SDL_Rect* src, const SDL_Rect* dst,
                                     double angle, const SDL_Point* center,
                                     SDL_FlipMode flip) {
  SDL_FRect fsrc {};
  SDL_FRect fdst {};
  SDL_FPoint fcenter {};
  if (src) fsrc = deckboyToFRect(*src);
  if (dst) fdst = deckboyToFRect(*dst);
  if (center) {
    fcenter = SDL_FPoint{static_cast<float>(center->x), static_cast<float>(center->y)};
  }
  return SDL_RenderTextureRotated(renderer, texture,
                                  src ? &fsrc : nullptr,
                                  dst ? &fdst : nullptr,
                                  angle,
                                  center ? &fcenter : nullptr,
                                  flip);
}

// ── Texture creation with the legacy global nearest scale mode ─────────────
// SDL2 ran with SDL_HINT_RENDER_SCALE_QUALITY="0" (nearest) for every
// texture; the hint is gone in SDL3 (default linear), so apply nearest at
// creation to preserve the pixel-crisp look and the exact SDL2 behaviour.

// Format is Uint32 because the codebase stores pixel formats in Uint32
// variables (SDL2 convention); SDL3 made SDL_PixelFormat a real enum.
inline SDL_Texture* deckboyCreateTexture(SDL_Renderer* renderer, Uint32 format,
                                         SDL_TextureAccess access, int w, int h) {
  SDL_Texture* texture = SDL_CreateTexture(renderer, static_cast<SDL_PixelFormat>(format), access, w, h);
  if (texture) {
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
  }
  return texture;
}

inline SDL_Texture* deckboyCreateTextureFromSurface(SDL_Renderer* renderer, SDL_Surface* surface) {
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (texture) {
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
  }
  return texture;
}

// ── Audio: SDL2 pause(0/1) semantics over a device-bound SDL_AudioStream ───
// The queue-audio model maps onto SDL3 as one logical playback device + bound
// stream per consumer (SDL_OpenAudioDeviceStream); pausing the stream's
// device pauses only that logical device, so per-deck transport control is
// preserved even when decks share a physical output.

inline void deckboySetAudioPaused(SDL_AudioStream* stream, bool paused) {
  if (!stream) {
    return;
  }
  if (paused) {
    SDL_PauseAudioStreamDevice(stream);
  } else {
    SDL_ResumeAudioStreamDevice(stream);
  }
}

// ── SDL2-style display indices over SDL3 display IDs ───────────────────────
// Index space = position in the SDL_GetDisplays() array, matching what
// SDL2 exposed. Projects persist these indices; the topology-refresh logic
// re-validates them on hot-plug just as before.

inline int deckboyGetNumVideoDisplays() {
  int count = 0;
  if (SDL_DisplayID* ids = SDL_GetDisplays(&count)) {
    SDL_free(ids);
    return count;
  }
  return 0;
}

inline SDL_DisplayID deckboyDisplayIdFromIndex(int index) {
  int count = 0;
  SDL_DisplayID result = 0;
  if (SDL_DisplayID* ids = SDL_GetDisplays(&count)) {
    if (index >= 0 && index < count) {
      result = ids[index];
    }
    SDL_free(ids);
  }
  return result;
}

inline int deckboyDisplayIndexFromId(SDL_DisplayID id) {
  int count = 0;
  int result = -1;
  if (SDL_DisplayID* ids = SDL_GetDisplays(&count)) {
    for (int i = 0; i < count; ++i) {
      if (ids[i] == id) {
        result = i;
        break;
      }
    }
    SDL_free(ids);
  }
  return result;
}

inline const char* deckboyGetDisplayName(int index) {
  return SDL_GetDisplayName(deckboyDisplayIdFromIndex(index));
}

inline bool deckboyGetDisplayBounds(int index, SDL_Rect* rect) {
  return SDL_GetDisplayBounds(deckboyDisplayIdFromIndex(index), rect);
}

inline int deckboyGetWindowDisplayIndex(SDL_Window* window) {
  return deckboyDisplayIndexFromId(SDL_GetDisplayForWindow(window));
}

// SDL2-style out-parameter desktop-mode query (SDL3 returns a pointer).
inline bool deckboyGetDesktopDisplayMode(int index, SDL_DisplayMode* out) {
  const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(deckboyDisplayIdFromIndex(index));
  if (!mode) {
    return false;
  }
  if (out) {
    *out = *mode;
  }
  return true;
}
