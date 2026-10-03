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
#include <cstdlib>
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

// Which backend the most recent deckboyCreateRenderer call landed on. Was
// previously only visible via the std::cerr line below, which a packaged
// .app launched from Finder or the Dock has no way to show anyone — so a
// report like issue #6 could describe the symptom but never the backend that
// produced it. Queryable so main.cpp can put it in a file that survives past
// the terminal nobody was watching.
inline std::string& deckboyLastRendererDriver() {
  static std::string name;
  return name;
}

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
// On macOS 26 it stopped working properly on at least one machine: the
// interface drew -- panels, icons, theme colours, the splash photograph --
// with NO TEXT AT ALL (issue #6).
//
// BE CAREFUL WITH WHAT THAT PROVES, because the first version of this comment
// was not. It cited fonts opening, rasterising ink and measuring correctly,
// and the whole interface drawing under the software renderer, as evidence
// that "nothing before the GPU is at fault". Every one of those measurements
// was taken on a Tahoe machine that DRAWS TEXT FINE ON OPENGL. They were
// measurements of a working machine, and they localised nothing.
//
// What the reporter's screenshots do show, and it is the useful part: the
// pixel-art icons and the images render, the rectangles render, and only
// glyphs are missing. Images and primitives hold their textures; until
// v0.99.363 the text path created and destroyed one per label PER FRAME. An
// all-or-nothing loss of exactly the churning path is what texture allocation
// failing under an emulated GL driver looks like. See the text texture cache
// in main.cpp. Metal is still the right backend here either way.
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
  // DECKBOY_RENDERER=software|opengl|metal|... tries that backend first. The
  // A/B an affected machine can run from a terminal without a new build:
  // if the interface draws its text under "software", the fault is in the
  // GPU path; if it does not, it is in the fonts.
  if (const char* forced = std::getenv("DECKBOY_RENDERER"); forced && forced[0] != '\0') {
    if (SDL_Renderer* renderer = SDL_CreateRenderer(window, forced)) {
      std::cerr << "renderer: " << forced << " (DECKBOY_RENDERER)" << std::endl;
      deckboyLastRendererDriver() = std::string(forced) + " (forced)";
      return renderer;
    }
    std::cerr << "renderer: DECKBOY_RENDERER=" << forced << " would not open; using the list" << std::endl;
  }
  for (const char* driver : kDrivers) {
    if (SDL_Renderer* renderer = SDL_CreateRenderer(window, driver)) {
      // SAY WHICH ONE. Which backend a window ended up on decides how it
      // behaves, and until issue #6 there was no way to find out short of
      // reading this function and guessing. One line, once per window.
      std::cerr << "renderer: " << driver << std::endl;
      deckboyLastRendererDriver() = driver;
      return renderer;
    }
  }
  SDL_Renderer* fallback = SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
  std::cerr << "renderer: software (every hardware backend refused)" << std::endl;
  deckboyLastRendererDriver() = "software (every hardware backend refused)";
  return fallback;
}

inline SDL_Texture* deckboyCreateTexture(SDL_Renderer* renderer, Uint32 format,
                                         SDL_TextureAccess access, int w, int h,
                                         SDL_Colorspace colorspace = SDL_COLORSPACE_UNKNOWN) {
  SDL_PropertiesID props = SDL_CreateProperties();
  if (!props) return nullptr;
  SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, format);
  SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, access);
  SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
  SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
  if (colorspace != SDL_COLORSPACE_UNKNOWN)
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, colorspace);
  SDL_Texture* texture = SDL_CreateTextureWithProperties(renderer, props);
  SDL_DestroyProperties(props);
  if (texture) {
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
  }
  return texture;
}

inline SDL_Colorspace deckboyTextureColorspace(SDL_Texture* texture) {
  return texture ? static_cast<SDL_Colorspace>(SDL_GetNumberProperty(
    SDL_GetTextureProperties(texture), SDL_PROP_TEXTURE_COLORSPACE_NUMBER,
    SDL_COLORSPACE_UNKNOWN)) : SDL_COLORSPACE_UNKNOWN;
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

// STREAMING FIRST, not a static texture-from-surface.
//
// Issue #6 came back on a second Tahoe machine (26.6.1, M1 Pro) with both
// prior fixes in place: Metal ordered ahead of OpenGL (v0.99.361), and the
// label cache that stopped text being the one path in the app allocating a
// texture every frame (v0.99.363). Neither is wrong to have done, and
// neither explains a machine that still loses every glyph the first time
// each one is drawn -- the cache does not paper over a failure, it just
// means we now retry every frame instead of once.
//
// What every fix so far left alone: SDL_CreateTextureFromSurface (used here,
// on an SDL_ttf ARGB8888 surface) creates a STATIC-access texture. Every
// image and video frame in this app is built with SDL_CreateTexture(...,
// SDL_TEXTUREACCESS_STREAMING, ...) + SDL_UpdateTexture, and nobody has ever
// reported one of those blank -- on this machine or any other. The v0.99.363
// retry asked the same static call again in a forced pixel format; it never
// asked for the OTHER kind of texture, the kind that already works on every
// machine this bug has been reported from.
//
// So: build it the way a video frame is built, upload with SDL_UpdateTexture,
// and only fall back to the old static path (kept below) if a backend ever
// refuses streaming access instead. Cheap to keep, since it only runs when
// the new path has already failed.
inline SDL_Texture* deckboyCreateTextureFromSurface(SDL_Renderer* renderer, SDL_Surface* surface) {
  if (!surface) {
    deckboyNoteTextureFailure("text/surface");
    return nullptr;
  }

  SDL_Surface* owned = nullptr;  // non-null only when we had to convert format; ours to free
  SDL_Surface* rgba = surface;
  if (surface->format != SDL_PIXELFORMAT_RGBA32) {
    owned = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    if (owned) {
      rgba = owned;
    }
  }
  if (rgba->format == SDL_PIXELFORMAT_RGBA32) {
    if (SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                 SDL_TEXTUREACCESS_STREAMING,
                                                 rgba->w, rgba->h)) {
      SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
      // Streaming textures default to BLENDMODE_NONE (opaque); SDL_ttf's
      // Blended surfaces carry real alpha at every glyph edge, and
      // SDL_CreateTextureFromSurface would have picked BLEND up from the
      // surface automatically. A manually built texture has to be told.
      SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
      if (SDL_UpdateTexture(texture, nullptr, rgba->pixels, rgba->pitch)) {
        if (owned) SDL_DestroySurface(owned);
        return texture;
      }
      SDL_DestroyTexture(texture);  // created, but the pixel upload itself failed
    }
  }
  if (owned) SDL_DestroySurface(owned);

  // FALLBACK: the static path this function used before v0.99.394, for a
  // backend that refuses streaming access instead of static.
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (!texture && surface->format != SDL_PIXELFORMAT_RGBA32) {
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
