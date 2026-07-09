// terrarium_0.42_cloudfix.cpp
// SDL2 ASCII-glyph terrarium with: seasons, wind, water depth (DF-ish), vivid palette,
// biome presets (meadow, wetland, alpine, alien, tropical), clouds + cloud shadows,
// weather (clear/overcast/rain/storm), rain overlay, lightning, big rainbow,
// lots of plant variance, more animals, and multi-tile "big" entities (2x2 deer, 3x3 ancient tree).
//
// Build (Linux):
//   sudo apt install -y g++ libsdl2-dev
//   g++ -O2 -std=c++17 terrarium_0.34.cpp -o terrarium_0.34 `sdl2-config --cflags --libs`
//
// Run examples:
//   ./terrarium_0.34 --biome meadow
//   ./terrarium_0.34 --biome tropical
//   ./terrarium_0.34 --windowed --biome tropical
//   ./terrarium_0.34 --fullscreen --biome meadow
//
// Controls:
//   SPACE pause/unpause
//   .     step (when paused)
//   [ ]   slower/faster
//   r     reseed
//   F11   toggle fullscreen
//   ESC   quit
//
// Notes:
// - ASCII-only (no unicode) for portability and Termux-friendly compilation if needed.
// - Clouds are a low-res field (CW x CH), scrolled by wind; shadows darken the w.
// - Rain is overlay + modest water increase; storms add lightning and stronger wind.
// - Big creatures are "stamped" at render time from anchor entities (simple, robust).

#include "../core/sdl_compat.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "terrarium_core.hpp"

using namespace terra;

static inline void setColor(SDL_Renderer* rr, uint8_t R, uint8_t G, uint8_t B, uint8_t A=255) {
  SDL_SetRenderDrawColor(rr, R, G, B, A);
}

struct Layout { int screenW=0, screenH=0; int hudH=0; int simHpx=0; };

static Layout computeLayout(SDL_Renderer* ren) {
  Layout L;
  SDL_GetCurrentRenderOutputSize(ren, &L.screenW, &L.screenH);
  L.hudH = std::max(40, L.screenH/18);
  L.simHpx = L.screenH - L.hudH;
  return L;
}


struct GlyphCache {
  std::unordered_map<char, SDL_Texture*> tex;

  void destroy() {
    for (auto& kv : tex) SDL_DestroyTexture(kv.second);
    tex.clear();
  }

  SDL_Texture* makeGlyph(SDL_Renderer* ren, char c) {
    SDL_Texture* t = deckboyCreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 8, 8);
    if (!t) return nullptr;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);

    void* pixels=nullptr; int pitch=0;
    if (!SDL_LockTexture(t, nullptr, &pixels, &pitch)) {
      SDL_DestroyTexture(t);
      return nullptr;
    }

    for (int y=0; y<8; ++y) {
      uint32_t* px = (uint32_t*)((uint8_t*)pixels + y*pitch);
      for (int x=0; x<8; ++x) px[x] = 0x00000000;
    }

    const uint8_t* g = glyph8(c);
    for (int y=0; y<8; ++y) {
      uint32_t* px = (uint32_t*)((uint8_t*)pixels + y*pitch);
      uint8_t bits = g[y];
      for (int x=0; x<8; ++x) {
        bool on = (bits & (0x80u >> x)) != 0;
        if (on) px[x] = 0xE0FFFFFF;
      }
    }

    SDL_UnlockTexture(t);
    return t;
  }

  SDL_Texture* get(SDL_Renderer* ren, char c) {
    auto it = tex.find(c);
    if (it != tex.end()) return it->second;
    SDL_Texture* t = makeGlyph(ren, c);
    if (t) tex[c] = t;
    return t;
  }
};


static inline void applyCloudLayer(SDL_Renderer* ren, const SDL_Rect& rc, uint8_t cloudVal) {
  if (cloudVal < 120) return;
  float c = (cloudVal - 120) / 135.f;
  c = std::clamp(c, 0.f, 1.f);
  uint8_t alpha = (uint8_t)(c * 60);
  setColor(ren, 180, 190, 210, alpha);
  SDL_RenderFillRect(ren, &rc);
}

static void render(SDL_Renderer* ren, const Layout& L, World& w, GlyphCache& gc, int tick) {
  Season s = seasonAt(tick);
  float sp = seasonLerp(tick);
        sp *= w.cloudOpacity;

  setColor(ren, 0,0,0);
  SDL_RenderClear(ren);

  for (int y=0; y<H; ++y) {
    int y0 = (y * L.simHpx) / H;
    int y1 = ((y+1) * L.simHpx) / H;
    int hpx = std::max(1, y1 - y0);

    for (int x=0; x<W; ++x) {
      int x0 = (x * L.screenW) / W;
      int x1 = ((x+1) * L.screenW) / W;
      int wpx = std::max(1, x1 - x0);

      SDL_Rect rc{ x0, y0, wpx, hpx };

      RGB bg = baseBgFor(w, x, y, tick, s, sp);
      uint8_t cloud = sampleCloud(w.clouds, x, y);
      // Biome-tune clouds: tropical clouds should be lighter/smaller coverage.
      cloud = (uint8_t)std::min<int>(255, (int)(cloud * w.cloudOpacity));
      if (w.biome==TROPICAL) cloud = (uint8_t)std::max<int>(0, (int)cloud - 35);
      if (w.biome==DESERT)   cloud = (uint8_t)std::max<int>(0, (int)cloud - 25);

      applyCloudShadow(bg, cloud);

      setColor(ren, bg.r, bg.g, bg.b);
      SDL_RenderFillRect(ren, &rc);

      char c = renderCharAt(w, x, y);

      if (c=='.' && w.water[y][x]==0 && w.entities[y][x]==' ' && w.overlay[y][x]==' ') {
        applyCloudLayer(ren, rc, cloud);
        continue;
      }

      if (w.entities[y][x]==' ' && w.overlay[y][x]==' ' && w.water[y][x]==0) {
        uint32_t h = hash3((uint32_t)x, (uint32_t)y, (uint32_t)(tick/6));
        c = terrainGlyphVariant(c, h, s, w.weather);
      }

      SDL_Texture* gt = gc.get(ren, c);
      if (gt) {
        RGB fg = fgForChar(w, c, s, sp, tick, x, y);

        if ((w.terrain[y][x]==',' || w.terrain[y][x]=='"') && w.wind.strength>0) {
          uint32_t h = hash3((uint32_t)x,(uint32_t)y,(uint32_t)(tick/3));
          if (h & 1u) {
            fg.g = clampU8((int)fg.g + 20);
            fg.r = clampU8((int)fg.r + 5);
          }
        }

        if ((c=='/'||c=='\\'||c=='|') && (w.weather.state==STORM)) {
          fg.r = clampU8(fg.r + 30);
          fg.g = clampU8(fg.g + 30);
          fg.b = clampU8(fg.b + 30);
        }

        SDL_SetTextureColorMod(gt, fg.r, fg.g, fg.b);
        SDL_RenderTexture(ren, gt, nullptr, &rc);
      }

      applyCloudLayer(ren, rc, cloud);
    }
  }

  SDL_Rect hud{0, L.simHpx, L.screenW, L.hudH};
  setColor(ren, 8,8,10);
  SDL_RenderFillRect(ren, &hud);

  SDL_RenderPresent(ren);
}

// ---------------- CLI ----------------
static Biome parseBiome(int argc, char** argv) {
  for (int i=1; i<argc; ++i) {
    if (std::strcmp(argv[i], "--biome")==0 && i+1<argc) {
      std::string s = argv[i+1];
      if (s=="meadow") return MEADOW;
      if (s=="wetland") return WETLAND;
      if (s=="alpine") return ALPINE;
      if (s=="alien") return ALIEN;
      if (s=="tropical") return TROPICAL;
    }
  }
  return MEADOW;
}

int main(int argc, char** argv) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  bool startFullscreen = true;
  for (int i=1; i<argc; ++i) {
    if (std::strcmp(argv[i], "--windowed")==0) startFullscreen = false;
    if (std::strcmp(argv[i], "--fullscreen")==0) startFullscreen = true;
  }

  Uint32 wflags = SDL_WINDOW_RESIZABLE;  // SDL3: windows are shown by default
  if (startFullscreen) wflags |= SDL_WINDOW_FULLSCREEN;  // borderless desktop (no explicit mode)

  SDL_Window* win = SDL_CreateWindow(
    "Terrarium",
    1280, 720,
    wflags
  );
  if (!win) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
  if (!ren) ren = SDL_CreateRenderer(win, SDL_SOFTWARE_RENDERER);
  if (!ren) {
    std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }

  // (SDL3: nearest-neighbour scaling applied per texture via deckboyCreateTexture.)

  uint32_t seed = (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
  Rng r(seed);

  Biome biome = parseBiome(argc, argv);

  World world;
  seedWorld(world, r, biome);

  GlyphCache gc;
  Layout layout = computeLayout(ren);

  bool running=true, paused=false;
  int tps=DEFAULT_TPS;
  int tick=0;
  std::string banner="calm";

  auto last = std::chrono::steady_clock::now();

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running=false;
      if (e.type == SDL_EVENT_KEY_DOWN) {
        switch (e.key.key) {
          case SDLK_ESCAPE: running=false; break;
          case SDLK_B: {
            // Cycle biomes with a short fade-out/fade-in.
            world.targetBiome = (Biome)(((int)world.biome + 1) % BIOME_COUNT);
            world.biomeFadeDir = +1;
          } break;
          case SDLK_SPACE: paused=!paused; break;
          case SDLK_PERIOD:
            if (paused) { step(world, r, banner, tick); tick++; }
            break;
          case SDLK_LEFTBRACKET: if (tps>1) tps--; break;
          case SDLK_RIGHTBRACKET: if (tps<30) tps++; break;
          case SDLK_R:
            seed = (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
            r = Rng(seed);
            seedWorld(world, r, biome);
            tick=0; banner="reset";
            break;
          case SDLK_F11: {
            SDL_WindowFlags flags = SDL_GetWindowFlags(win);
            bool fs = (flags & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(win, !fs);
            layout = computeLayout(ren);
          } break;
          default: break;
        }
      }
      if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
          e.type == SDL_EVENT_WINDOW_RESIZED ||
          e.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
        layout = computeLayout(ren);
      }
    }

    auto now = std::chrono::steady_clock::now();
    auto dtMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
    const int msPerTick = 1000 / std::max(1, tps);

    // Biome transitions: animate fade, then actually reseed the world into the new biome.
    // (Without this, hitting 'B' would only change the label/palette, not the terrain rules.)
    if (world.biomeFadeDir != 0) {
      world.biomeFade += 0.02f * (float)world.biomeFadeDir;
      if (world.biomeFade >= 1.0f) {
        world.biomeFade = 0.0f;
        world.biomeFadeDir = 0;
        seedWorld(world, r, world.targetBiome);
      } else if (world.biomeFade <= 0.0f) {
        world.biomeFade = 0.0f;
        world.biomeFadeDir = 0;
      }
    }

    if (!paused && dtMs >= msPerTick) {
      last = now;
      step(world, r, banner, tick);
      tick++;
    }

    Season s = seasonAt(tick);
    std::string title =
      std::string("Terrarium 0.42 (fixed4) | biome ") + biomeName(biome) +
      " | " + std::to_string(W) + "x" + std::to_string(H) +
      " | tick " + std::to_string(tick) +
      " | " + (paused ? "PAUSED" : ("tps " + std::to_string(tps))) +
      " | " + seasonName(s) +
      " | weather " + weatherName(world.weather.state) +
      " (" + std::to_string((int)(world.weather.rainStrength*100)) + "%)" +
      " | wind " + std::to_string(world.wind.strength) +
      " | " + banner +
      " | SPACE pause  . step  [ ] speed  r reset  F11 fullscreen  ESC quit";
    SDL_SetWindowTitle(win, title.c_str());

    render(ren, layout, world, gc, tick);
    SDL_Delay(6);
  }

  gc.destroy();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
