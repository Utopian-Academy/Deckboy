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
#include <iostream>
#include <string>

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

// ---------------------------------------------------------------------------
// deckboyCreateRenderer — a renderer, with the backends we actually test.
//
// SDL_CreateRenderer(window, nullptr) lets SDL walk its own driver list, and on
// Windows that list ends up at the DIRECT3D 9 backend when the ones above it
// fail to create. That backend crashes inside the NVIDIA driver -- 0xC0000005
// in nvd3dumx.dll, under D3D_CreateRenderer -- and D3D11 does fail sometimes,
// under GPU resource pressure with several instances holding devices at once.
//
// The symptom is horrible to chase: recordings that die at random, blamed on
// whatever feature happened to be under test at the time.
//
// So the order is named. Every entry is a backend this app is tested on, and
// D3D9 is not among them. Software is the last resort and always works.
inline SDL_Renderer* deckboyCreateRenderer(SDL_Window* window) {
  if (!window) {
    return nullptr;
  }
// THE PLATFORM'S OWN BACKEND COMES FIRST, and it did not.
//
// This list was written to keep Windows off D3D9 and put the Windows backends
// at the front, which it does. What nobody noticed is that it also puts
// **OpenGL ahead of Metal**, so on macOS the first name that creates wins --
// and OpenGL creates. Deckboy has been running on a deprecated backend on
// every Mac since this list was written, quietly, because it worked.
//
// On macOS 26 it stopped working properly: the interface drew -- panels,
// icons, theme colours, the splash photograph -- with NO TEXT AT ALL
// (issue #6). Measured on a Tahoe machine: the fonts open, rasterise ink and
// measure correctly, and the whole text path is fine under the software
// renderer, so nothing before the GPU is at fault. macOS 14 and 15 still draw
// text through OpenGL, which is why CI never saw it.
//
// Metal is the supported, tested backend on macOS and has been since 2018.
// OpenGL stays in the list, one place lower, as the fallback it should always
// have been.
#if defined(__APPLE__)
  static const char* const kDrivers[] = {
    "metal", "opengl", "gpu", "software",
  };
#elif defined(_WIN32)
  static const char* const kDrivers[] = {
    "direct3d11", "direct3d12", "opengl", "gpu",
  };
#else
  static const char* const kDrivers[] = {
    "opengl", "vulkan", "gpu",
  };
#endif
  for (const char* driver : kDrivers) {
    if (SDL_Renderer* renderer = SDL_CreateRenderer(window, driver)) {
      // SAY WHICH ONE. Which backend a window ended up on decides how it
      // behaves, and until issue #6 there was no way to find out short of
      // reading this function and guessing. One line, once per window.
      std::cerr << "renderer: " << driver << std::endl;
      return renderer;
    }
  }
  SDL_Renderer* fallback = SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
  std::cerr << "renderer: software (every hardware backend refused)" << std::endl;
  return fallback;
}

inline SDL_Texture* deckboyCreateTexture(SDL_Renderer* renderer, Uint32 format,
                                         SDL_TextureAccess access, int w, int h) {
  SDL_Texture* texture = SDL_CreateTexture(renderer, static_cast<SDL_PixelFormat>(format), access, w, h);
  if (texture) {
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
  }
  return texture;
}

// WHY THERE IS A SECOND ATTEMPT.
//
// Issue #6: on macOS 26 the entire interface drew -- panels, icons, theme
// colours, the splash photograph -- with no text anywhere. Measured on a Tahoe
// machine: the fonts open, rasterise ink and measure correctly, and the whole
// text path works under the software renderer. The backend is the variable --
// see deckboyCreateRenderer above, which had been handing macOS a deprecated
// OpenGL context -- and the only step left between a good surface and a
// missing glyph is this call.
//
// SDL_ttf hands back ARGB8888. Images in this app never take this path -- they
// are streaming textures filled with SDL_UpdateTexture -- so a backend that
// refuses a STATIC texture in that format loses every letter and nothing else,
// which is exactly the reported picture. Converting to RGBA32 and trying again
// costs one blit on a path that has already failed, and nothing at all on the
// machines where the first attempt works.
//
// deckboyTextureFailures() is not decoration. Every text draw in the app used
// to end at `if (!texture) return;` with no log, no toast and no counter, so an
// operator looking at a blank interface had nothing to report and we had
// nothing to ask for. Now the app can say it.
inline unsigned& deckboyTextureFailureCount() {
  static unsigned count = 0;
  return count;
}

inline const std::string& deckboyTextureFailureReason() {
  static std::string reason;
  return reason;
}

inline void deckboyNoteTextureFailure(const char* what) {
  ++deckboyTextureFailureCount();
  if (deckboyTextureFailureCount() == 1) {
    const_cast<std::string&>(deckboyTextureFailureReason()) =
      std::string(what) + ": " + (SDL_GetError() ? SDL_GetError() : "(no error)");
    std::cerr << "texture creation failed (" << what << "): "
              << (SDL_GetError() ? SDL_GetError() : "(no error)") << std::endl;
  }
}

inline SDL_Texture* deckboyCreateTextureFromSurface(SDL_Renderer* renderer, SDL_Surface* surface) {
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (!texture && surface && surface->format != SDL_PIXELFORMAT_RGBA32) {
    if (SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32)) {
      texture = SDL_CreateTextureFromSurface(renderer, converted);
      SDL_DestroySurface(converted);
    }
  }
  if (texture) {
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
  } else {
    deckboyNoteTextureFailure("text/surface");
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

// ── The shortcut modifier: Command on macOS, Control everywhere else ───────
// Every shortcut read SDL_KMOD_CTRL directly, which is right on Windows and
// Linux and wrong on macOS, where the platform modifier is Command. Cmd+S,
// Cmd+O, Cmd+Z and Cmd-click did nothing at all on a Mac. The one place that
// DID accept Command -- the inline text editor -- accepted it unconditionally,
// so on Windows the Windows key copied and pasted.
//
// macOS keeps Control working alongside Command, so nothing an operator has in
// their fingers stops working and every documented Ctrl+<key> still applies.
//
// This is for SHORTCUTS only. A guard asking "is any modifier held, so this
// keypress is not text" must keep testing the modifiers it cares about
// directly: folding Command into one of those would be this same bug pointing
// the other way.
inline bool deckboyShortcutHeld(Uint16 mod) {
#ifdef __APPLE__
  return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
#else
  return (mod & SDL_KMOD_CTRL) != 0;
#endif
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
