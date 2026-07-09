// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// terrarium_core.hpp - The Terrarium ecosystem simulation, extracted from the
// companion exe (terrarium.cpp) so Deckboy can run it NATIVELY as a pattern
// source (pattern://terrarium). SDL-free: world state, simulation step,
// procedural species, the 8x8 glyph font, per-cell glyph/color logic, and a
// packed-RGBA frame renderer. The companion exe includes this header and
// keeps only its windowing/input/SDL-texture layer.
// Everything is static/internal-linkage inside namespace terra - each
// including TU gets its own copy of the world machinery.
// ============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace terra {

static constexpr int W = 200;
static constexpr int H = 112;

static constexpr int CW = 96;
static constexpr int CH = 54;

static constexpr int DEFAULT_TPS = 9;

static constexpr int SEASON_TICKS = 1800;
static constexpr int DAY_TICKS    = 900;

static constexpr int WIND_CHANGE_TICKS = 220;
static constexpr int MAX_WIND = 5;

enum WeatherState { CLEAR=0, OVERCAST=1, RAIN=2, STORM=3 };

static constexpr float VIVID_SAT = 1.18f;  // slightly richer (bg is black)
static constexpr float VIVID_VAL = 1.08f;  // a touch brighter

static constexpr int BUG_CAP_BASE      = (W * H) / 75;
static constexpr int BIRD_CAP_BASE     = (W * H) / 260;
static constexpr int SCORPION_CAP = (W * H) / 1600;
static constexpr int DRAGONFLY_CAP= (W * H) / 1100;
static constexpr int CRAB_CAP     = (W * H) / 1400;
static constexpr int JELLY_CAP    = (W * H) / 1500;
static constexpr int WHALE_CAP = 2;
static constexpr int DINO_CAP  = 3;
static constexpr int CRAWLER_CAP  = (W * H) / 1700;
static constexpr int ORB_CAP      = (W * H) / 2600;

static constexpr int RABBIT_CAP_BASE   = (W * H) / 420;
static constexpr int SNAKE_CAP_BASE    = (W * H) / 1200;
static constexpr int FIREFLY_CAP_BASE  = (W * H) / 320;
static constexpr int DEER_CAP_BASE     = (W * H) / 2400;
static constexpr int OWL_CAP_BASE      = (W * H) / 2200;
static constexpr int YETI_CAP_BASE     = (W * H) / 3200;

using Grid = std::vector<std::string>;
using Water = std::vector<std::vector<uint8_t>>;

static inline bool inBounds(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }

struct Rng {
  std::mt19937 rng;
  explicit Rng(uint32_t seed) : rng(seed) {}
  int i(int a, int b) { std::uniform_int_distribution<int> d(a, b); return d(rng); }
  bool oneIn(int n) { return n > 0 && i(1, n) == 1; }
  float u01() { std::uniform_real_distribution<float> d(0.f, 1.f); return d(rng); }
  uint32_t u32() { return (uint32_t)rng(); }
};

static inline uint32_t hash3(uint32_t x, uint32_t y, uint32_t salt) {
  uint32_t h = x * 0x9E3779B1u ^ y * 0x85EBCA6Bu ^ salt * 0xC2B2AE35u;
  h ^= (h >> 16); h *= 0x7FEB352Du; h ^= (h >> 15); h *= 0x846CA68Bu; h ^= (h >> 16);
  return h;
}

static inline uint8_t clampU8(int v){ return (uint8_t)std::clamp(v, 0, 255); }

static int countNeighborsChar(const Grid& g, int x, int y, char c) {
  int n = 0;
  for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
    if (dx==0 && dy==0) continue;
    int nx=x+dx, ny=y+dy;
    if (inBounds(nx,ny) && g[ny][nx]==c) n++;
  }
  return n;
}

static int countNeighborsWater(const Water& w, int x, int y) {
  int n = 0;
  for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
    if (dx==0 && dy==0) continue;
    int nx=x+dx, ny=y+dy;
    if (inBounds(nx,ny) && w[ny][nx]>0) n++;
  }
  return n;
}

enum Season { SPRING=0, SUMMER=1, AUTUMN=2, WINTER=3 };
static inline Season seasonAt(int tick) { return (Season)((tick / SEASON_TICKS) % 4); }
static inline float seasonLerp(int tick) { return float(tick % SEASON_TICKS) / float(SEASON_TICKS); }
static inline bool nightish(int tick) { return ((tick / (DAY_TICKS/2)) % 2) == 1; }

// ---------------- Biomes ----------------
static constexpr int BIOME_COUNT = 6;

enum Biome { MEADOW=0, WETLAND=1, ALPINE=2, ALIEN=3, TROPICAL=4, DESERT=5 };

static const char* biomeName(Biome b) {
  switch (b) {
    case MEADOW: return "meadow";
    case WETLAND:return "wetland";
    case ALPINE: return "alpine";
    case ALIEN:  return "alien";
    case TROPICAL: return "tropical";
    case DESERT: return "desert";
  }
  return "meadow";
}

struct BiomeWeights {
  float pondDensity;
  float stoneChance;
  float reedChance;
  float fernChance;
  float flowerChance;
  float bigFlowerChance;
  float treeChance;
  float mushChance;

  float growRate;
  float bloomRate;
  float fireRate;
  float alienRate;
};

static BiomeWeights weightsFor(Biome b) {
  switch (b) {
    case MEADOW: return {1.0f, 1.0f, 0.7f, 0.9f, 1.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.2f, 0.9f, 0.7f};
    case WETLAND:return {1.35f,0.7f, 1.4f, 1.0f, 1.15f,1.0f, 0.9f, 1.2f, 1.0f, 1.3f, 0.8f, 0.7f};
    case ALPINE: return {0.75f,1.6f, 0.5f, 0.6f, 0.7f, 0.6f, 0.7f, 0.7f, 0.75f,0.6f, 1.05f,0.8f};
    case ALIEN:  return {1.05f,1.0f, 0.9f, 0.9f, 1.5f, 1.6f, 1.1f, 1.3f, 1.1f, 1.6f, 0.85f,1.6f};
    case TROPICAL:return {1.25f,0.6f, 1.3f, 1.2f, 1.6f, 1.4f, 1.25f,1.15f,1.2f, 1.65f,0.75f,1.0f};
  }
  return weightsFor(MEADOW);
}

// ---------------- Wind / Weather / Clouds ----------------
struct Wind { int dx=0, dy=0; int strength=0; };
// ---------------- Aleatoric flora & fauna (seeded) ----------------
struct RGB { uint8_t r,g,b; };

struct PlantSpec {
  const uint8_t* glyph = nullptr; // 8x8 bitmap pointer
  RGB base{0,0,0};
  RGB accent{0,0,0};
  float moisturePref = 0.5f; // 0 dry .. 1 wet
  float heightPref   = 0.5f; // 0 low .. 1 high
  float bloominess   = 0.3f; // 0..1 accent probability
};

struct AnimalSpec {
  const uint8_t* glyph = nullptr;
  RGB base{0,0,0};
  RGB accent{0,0,0};
  float moisturePref = 0.5f;
  float heightPref   = 0.5f;
  float speed        = 0.5f; // 0 slow .. 1 fast
  float herbivory    = 0.5f; // 0 predator .. 1 herbivore
  float glow         = 0.0f; // 0..1
};

// Dynamic glyph lookup table (filled at seed time).
static const uint8_t* gDynGlyph[256] = {nullptr};
static inline unsigned char uc(char c) { return (unsigned char)c; }

// Reserve byte ranges for procedurally generated species.
static constexpr unsigned char PLANT_BASE = 0x30; // 0x30..0x6F (64)
static constexpr unsigned char ANIM_BASE  = 0x70; // 0x70..0x9F (48)

static inline bool isPlantToken(char c){
  unsigned char u = uc(c);
  if (u >= PLANT_BASE && u < PLANT_BASE + 64) return true;
  return (c=='"' || c=='T' || c=='Y' || c=='P' || c=='F' || c=='f' || c=='*' || c==';' || c=='+' || c=='~');
}
static inline bool isAnimalToken(char c){
  unsigned char u = uc(c);
  if (u >= ANIM_BASE && u < ANIM_BASE + 48) return true;
  return (c=='b' || c=='r' || c=='s' || c=='o' || c=='g' || c=='w' || c=='y' || c=='a' || c=='d');
}
struct Weather {
  WeatherState state = CLEAR;
  float rainStrength = 0.f;
  int timer = 0;
  bool lastTickWasRaining = false;
};
struct Clouds {
  std::vector<uint8_t> field;
  float offX=0.f, offY=0.f;
  Clouds() : field(CW*CH, 0) {}
};

struct World {
  uint32_t worldSeed = 0xC0FFEEu;
  Grid terrain;
  Grid entities;
  Water water;
  std::vector<std::vector<uint8_t>> height;
  Grid overlay;
  Wind wind;
  Weather weather;
  Clouds clouds;
  float cloudOpacity = 1.0f;
  Biome biome;
  Biome targetBiome = MEADOW;
  float biomeFade = 0.0f;
  int biomeFadeDir = 0;
  BiomeWeights bw;
  // Hydrology stability
  std::vector<std::vector<uint8_t>> moist; // 0..255 groundwater/moisture reserve
  std::vector<std::pair<int,int>> springs; // persistent water sources

  // Aleatoric species pools (generated at seed time)
  std::vector<PlantSpec> flora; // 64 plant templates
  std::vector<AnimalSpec> fauna; // 48 animal templates

};

// ---------------- Procedural species variety (visual-only) ----------------
static inline uint32_t biomeSalt(Biome b) {
  switch (b) {
    case MEADOW:   return 0xA17C3u;
    case WETLAND:  return 0x55D1Bu;
    case ALPINE:   return 0xC0FFEu;
    case DESERT:   return 0xD3A5Eu;
    case TROPICAL: return 0x7A0F1u;
    case ALIEN:    return 0xA11E1u;
    default:       return 0xBEEFu;
  }
}

static inline uint32_t speciesSeed2(const World& w, int x, int y) {
  return hash3((uint32_t)x, (uint32_t)y, w.worldSeed ^ biomeSalt(w.biome));
}

// Overload used for aleatoric flora/fauna: fold in season + an extra salt (eg. tick-derived)
static inline uint32_t speciesSeed2(const World& w, int x, int y, Season s, uint32_t salt) {
  // Keep deterministic per-world, but let season/tick influence variant selection slightly.
  // This preserves "identity" while allowing subtle temporal change.
  uint32_t h = speciesSeed2(w, x, y);
  h ^= hash3((uint32_t)s * 0x9E3779B9u, 0u, 0u);
  h ^= hash3(salt, 0xA5A5A5A5u, 0x3C3C3C3Cu);
  return h;
}


static inline int speciesVariant2(const World& w, int x, int y, int n) {
  if (n <= 1) return 0;
  return (int)(speciesSeed2(w,x,y) % (uint32_t)n);
}

// ---------------- Helpers ----------------
static char waterGlyph(uint8_t d) {
  d = (uint8_t)std::min<int>(7, d);
  if (!d) return '.';
  return (char)('0' + d); // '1'..'7'
}


static inline char waterFlowGlyph(const World& w, int x, int y, int tick) {
  uint8_t d0 = w.water[y][x];
  if (!d0) return '.';
  int d = std::min<int>(7, d0);

  int bestDx = 0, bestDy = 0;
  int here = (int)w.height[y][x] + (int)w.water[y][x]*8;
  int bestDrop = 0;
  for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
    if (!dx && !dy) continue;
    int nx=x+dx, ny=y+dy;
    if (!inBounds(nx,ny)) continue;
    int there = (int)w.height[ny][nx] + (int)w.water[ny][nx]*8;
    int drop = here - there;
    if (drop > bestDrop) { bestDrop = drop; bestDx = dx; bestDy = dy; }
  }

  if (bestDrop < 2) return (char)('0' + d);

  int cat = 0; // 0 horiz, 1 vert, 2 diag
  if (bestDx != 0 && bestDy != 0) cat = 2;
  else if (bestDy != 0) cat = 1;

  int base = (cat==0 ? 0x01 : (cat==1 ? 0x08 : 0x0F));
  int phase = ((tick/6) + x + y) & 1;
  if (d >= 4 && phase) d = std::max(1, d-1);
  return (char)(base + (d-1));
}

static inline bool isTree(char c) { return c=='T' || c=='Y' || c=='P'; }
static inline bool isVeg(char c) {
  return (c==','||c=='"'||c=='#'||c=='m'||c=='f'||c=='+'||c=='&'||c==';'||c==':'||c=='$'||c=='!'||isTree(c));
}
static inline bool blocksEntity(char terrain, uint8_t waterDepth) {
  if (waterDepth > 0) return true;
  if (terrain == '*' || terrain == 'B' || terrain == 'M' || terrain == '^' || terrain=='X' || terrain=='c') return true;
  return false;
}

static void initClouds(Clouds& c, Rng& r, Biome b) {
  float base = (b==WETLAND) ? 110.f : (b==ALPINE ? 80.f : 95.f);
  if (b==ALIEN) base = 105.f;
  if (b==TROPICAL) base = 92.f;
  for (int y=0; y<CH; ++y) for (int x=0; x<CW; ++x) {
    float n = r.u01();
    int v = int(base + (n-0.5f)*120.f);
    c.field[y*CW + x] = clampU8(v);
  }
  c.offX = r.u01() * CW;
  c.offY = r.u01() * CH;
}

static void blurClouds(Clouds& c) {
  std::vector<uint8_t> tmp = c.field;
  for (int y=0; y<CH; ++y) for (int x=0; x<CW; ++x) {
    int acc=0, cnt=0;
    for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
      int nx = (x+dx+CW)%CW, ny=(y+dy+CH)%CH;
      acc += tmp[ny*CW+nx];
      cnt++;
    }
    c.field[y*CW+x] = (uint8_t)(acc/cnt);
  }
}

static void evolveClouds(Clouds& c, Rng& r, const Wind& w, const Weather& we, int tick) {
  float speed = 0.010f + 0.008f * (float)w.strength;
  if (we.state == STORM) speed *= 1.6f;
  c.offX += speed * (float)w.dx;
  c.offY += speed * (float)w.dy;
  if (c.offX < 0) c.offX += CW; if (c.offX >= CW) c.offX -= CW;
  if (c.offY < 0) c.offY += CH; if (c.offY >= CH) c.offY -= CH;

  if (tick % 9 == 0) {
    for (int i=0; i< (CW*CH)/18; ++i) {
      int x = r.i(0, CW-1), y = r.i(0, CH-1);
      int idx = y*CW+x;
      int v = (int)c.field[idx] + r.i(-6, 6);
      if (we.state == OVERCAST) v += 3;
      if (we.state == CLEAR) v -= 2;
      if (we.state == RAIN) v += 4;
      if (we.state == STORM) v += 6;
      c.field[idx] = clampU8(v);
    }
  }
  if (tick % 23 == 0) blurClouds(c);
}

static uint8_t sampleCloud(const Clouds& c, int x, int y) {
  // Bilinear sample for smoother/smaller-looking cloud shapes.
  float fx = ((float)x / (float)W) * (float)CW + c.offX;
  float fy = ((float)y / (float)H) * (float)CH + c.offY;
  int x0 = (int)floorf(fx) % CW; if (x0 < 0) x0 += CW;
  int y0 = (int)floorf(fy) % CH; if (y0 < 0) y0 += CH;
  int x1 = (x0 + 1) % CW;
  int y1 = (y0 + 1) % CH;
  float tx = fx - floorf(fx);
  float ty = fy - floorf(fy);
  int a = c.field[y0*CW + x0];
  int b = c.field[y0*CW + x1];
  int d = c.field[y1*CW + x0];
  int e = c.field[y1*CW + x1];
  float top = a + (b - a) * tx;
  float bot = d + (e - d) * tx;
  float v = top + (bot - top) * ty;
  if (v < 0.f) v = 0.f;
  if (v > 255.f) v = 255.f;
  return (uint8_t)(v + 0.5f);
}

static float avgCloud(const Clouds& c) {
  long acc=0;
  for (auto v: c.field) acc += v;
  return (float)acc / (float)(CW*CH);
}

// overlays
static void clearOverlay(World& w) {
  for (int y=0; y<H; ++y) std::fill(w.overlay[y].begin(), w.overlay[y].end(), ' ');
}

static void spawnRainbow(World& w, Rng& r) {
  int cx = W/2 + r.i(-W/10, W/10);
  int cy = H + r.i(H/6, H/3);
  int R  = std::min(W, H) + r.i(10, 60);
  int thick = 3 + r.i(0, 3);
  const char bands[] = {'=', '-', '~', '+', '!'};
  int nb = (int)(sizeof(bands)/sizeof(bands[0]));
  for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
    int dx=x-cx, dy=y-cy;
    int d2=dx*dx + dy*dy;
    int r0=R, r1=R-thick;
    if (d2 <= r0*r0 && d2 >= r1*r1) {
      int band = (x + 2*y) % nb;
      w.overlay[y][x] = bands[band];
    }
  }
}

static void applyRainOverlay(World& w, int tick) {
  if (w.weather.rainStrength <= 0.01f) return;
  int strength = (int)(w.weather.rainStrength * 10.f);
  int drops = (W * H) / std::max(8, 30 - strength*2);

  char streak = '|';
  if (w.wind.strength > 0) {
    if (w.wind.dx > 0) streak = '/';
    else if (w.wind.dx < 0) streak = '\\';
    else streak = '|';
  }

  for (int i=0; i<drops; ++i) {
    int x = (int)(hash3(i, tick, 1337) % W);
    int y = (int)(hash3(i, tick, 7331) % H);
    if (w.overlay[y][x] == ' ') w.overlay[y][x] = streak;
  }
}

static void updateWind(World& w, Rng& r, int tick) {
  if (tick % WIND_CHANGE_TICKS != 0) return;

  int target = 0;
  if (w.weather.state == CLEAR) target = r.i(0, 2);
  if (w.weather.state == OVERCAST) target = r.i(1, 3);
  if (w.weather.state == RAIN) target = r.i(2, 4);
  if (w.weather.state == STORM) target = r.i(3, MAX_WIND);

  if (r.oneIn(8)) { w.wind.strength = 0; w.wind.dx=0; w.wind.dy=0; return; }
  w.wind.strength = std::clamp(w.wind.strength + r.i(-1, 2), 0, MAX_WIND);
  if (w.wind.strength < target && r.oneIn(2)) w.wind.strength++;
  if (w.wind.strength > target && r.oneIn(3)) w.wind.strength--;

  if (w.wind.strength == 0) { w.wind.dx=0; w.wind.dy=0; return; }

  if (r.oneIn(2) || (w.wind.dx==0 && w.wind.dy==0)) {
    int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
    int k = r.i(0,7);
    w.wind.dx = dirs[k][0];
    w.wind.dy = dirs[k][1];
  }
}

static void updateWeather(World& w, Rng& r, int tick) {
  w.weather.timer++;
  Season s = seasonAt(tick);

  float cloudAvg = avgCloud(w.clouds);
  bool cloudy = cloudAvg > 120.f;
  bool veryCloudy = cloudAvg > 150.f;

  auto toState = [&](WeatherState ns){
    w.weather.state = ns;
    w.weather.timer = 0;
  };

  switch (w.weather.state) {
    case CLEAR: {
      w.weather.rainStrength = std::max(0.f, w.weather.rainStrength - 0.02f);
      if (cloudy && r.oneIn(6)) toState(OVERCAST);
      int chance = (s==SUMMER? 220 : s==SPRING? 170 : s==AUTUMN? 190 : 260);
      if (w.biome==TROPICAL) chance = std::max(120, chance-60);
      if (w.biome==DESERT) chance = std::max(4000, chance+2800); // far rarer rain in desert
      if (r.oneIn(chance)) toState(OVERCAST);
    } break;

    case OVERCAST: {
      w.weather.rainStrength = std::min(0.35f, w.weather.rainStrength + 0.01f);
      if (veryCloudy && w.weather.timer > 80 && r.oneIn(3)) toState(RAIN);
      if (!cloudy && w.weather.timer > 120 && r.oneIn(4)) toState(CLEAR);
      if (w.weather.timer > 600 && r.oneIn(3)) toState(CLEAR);
    } break;

    case RAIN: {
      w.weather.rainStrength = std::min(1.f, w.weather.rainStrength + 0.02f);
      int stormChance = (s==SUMMER ? 180 : 420);
      if (w.biome==TROPICAL) stormChance = std::max(120, stormChance-80);
      if (w.biome==DESERT) stormChance = std::max(8000, stormChance+6000); // basically no storms
      if (w.weather.timer > 120 && r.oneIn(stormChance)) toState(STORM);
      if (w.weather.timer > 350 && r.oneIn(6)) toState(OVERCAST);
      if (w.weather.timer > 900) toState(OVERCAST);
    } break;

    case STORM: {
      w.weather.rainStrength = std::min(1.f, w.weather.rainStrength + 0.03f);
      if (w.weather.timer > 160 && r.oneIn(4)) toState(RAIN);
      if (w.weather.timer > 420) toState(RAIN);
    } break;
  }

  bool isRainingNow = (w.weather.state == RAIN || w.weather.state == STORM);
  if (w.weather.lastTickWasRaining && !isRainingNow) {
    float cavg = cloudAvg;
    int chance = (cavg < 120.f) ? 2 : (cavg < 140.f ? 3 : 5);
    if (w.biome==TROPICAL) chance = std::max(1, chance-1);
    if (w.biome==DESERT) chance = std::max(1, chance+12); // desert tends to stay clear
    if (r.oneIn(chance)) spawnRainbow(w, r);
  }
  w.weather.lastTickWasRaining = isRainingNow;
}

// ---------------- Seeding world ----------------

static void genHeight(World& w, uint32_t seed) {
  auto noise = [&](int x,int y,int s)->uint8_t{
    uint32_t h = hash3((uint32_t)x, (uint32_t)y, (uint32_t)(seed + (uint32_t)s*1013u));
    return (uint8_t)(h & 255u);
  };

  for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
    int n1 = noise(x/6,  y/6,  1);
    int n2 = noise(x/18, y/18, 2);
    int n3 = noise(x/44, y/44, 3);
    int v  = (n1*6 + n2*3 + n3*2) / 11;

    int ridge = (int)(120 - std::abs(y - H/2)) / 2;
    v = std::clamp(v + ridge, 0, 255);

    w.height[y][x] = (uint8_t)v;
  }

  for (int pass=0; pass<2; ++pass) {
    auto tmp = w.height;
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
      int acc=0,cnt=0;
      for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
        int nx=x+dx, ny=y+dy;
        if (!inBounds(nx,ny)) continue;
        acc += tmp[ny][nx];
        cnt++;
      }
      w.height[y][x] = (uint8_t)(acc/cnt);
    }
  }
}


// --- Aleatoric pool generation (per-biome, per-seed)
static inline float u01(uint32_t x){ return (float)(x & 0xFFFFu) / 65535.0f; }
static inline float clamp01(float x){ return x<0?0:(x>1?1:x); }

static inline RGB pastelFrom(uint32_t h, float sat=0.55f, float val=0.92f) {
  float H = (float)((h>>8) & 1023u) / 1023.0f;
  float S = sat;
  float V = val;
  float r,g,b;
  float i = std::floor(H*6.0f);
  float f = H*6.0f - i;
  float p = V*(1.0f-S);
  float q = V*(1.0f-f*S);
  float t = V*(1.0f-(1.0f-f)*S);
  switch ((int)i % 6) {
    case 0: r=V; g=t; b=p; break;
    case 1: r=q; g=V; b=p; break;
    case 2: r=p; g=V; b=t; break;
    case 3: r=p; g=q; b=V; break;
    case 4: r=t; g=p; b=V; break;
    default:r=V; g=p; b=q; break;
  }
  return RGB{(uint8_t)(r*255.0f),(uint8_t)(g*255.0f),(uint8_t)(b*255.0f)};
}


// Forward declarations for 8x8 glyph bitmaps used by the aleatoric pools.
extern const uint8_t TGRASS[8];
extern const uint8_t SHRUB[8];
extern const uint8_t FERN[8];
extern const uint8_t FLOW1[8];
extern const uint8_t FLOW2[8];
extern const uint8_t TREE1[8];
extern const uint8_t TREE2[8];
extern const uint8_t BIGF[8];
extern const uint8_t MUSH[8];
extern const uint8_t REED[8];
extern const uint8_t LILY[8];
extern const uint8_t SAND[8];
extern const uint8_t CACT[8];
extern const uint8_t STONE[8];
extern const uint8_t BOUL[8];
extern const uint8_t MUD[8];
extern const uint8_t PALM[8];
extern const uint8_t FRUIT[8];
extern const uint8_t MOUN[8];
extern const uint8_t STAR[8];
extern const uint8_t EX[8];
extern const uint8_t HASH[8];
extern const uint8_t PIPE[8];
extern const uint8_t BUG[8];
extern const uint8_t BIRD[8];
extern const uint8_t RAB[8];
extern const uint8_t OWL[8];
extern const uint8_t DRGN1[8];
extern const uint8_t CRAB1[8];
extern const uint8_t SCOR1[8];
extern const uint8_t SNAKE[8];
extern const uint8_t JELY1[8];
extern const uint8_t YETI[8];
extern const uint8_t CRAW1[8];
extern const uint8_t ORB1[8];
extern const uint8_t AYY[8];

static inline const uint8_t* pickPlantGlyph(Biome b, uint32_t h){
  const uint8_t* meadow[]   = {TGRASS, SHRUB, FERN, FLOW1, FLOW2, TREE1, TREE2, BIGF, MUSH};
  const uint8_t* wetland[]  = {REED, LILY, FERN, FLOW1, FLOW2, SHRUB, TREE1, MUSH};
  const uint8_t* desert[]   = {SAND, CACT, STONE, BOUL, MUD};
  const uint8_t* tropical[] = {PALM, TREE2, FLOW2, BIGF, FERN, REED, LILY, FRUIT};
  const uint8_t* alpine[]   = {TREE1, TREE2, STONE, MOUN, FERN};
  const uint8_t* alien[]    = {STAR, EX, HASH, MUSH, FERN, PIPE};
  const uint8_t** arr = meadow; int n = (int)(sizeof(meadow)/sizeof(meadow[0]));
  switch(b){
    case MEADOW:   arr=meadow;   n=(int)(sizeof(meadow)/sizeof(meadow[0])); break;
    case WETLAND:  arr=wetland;  n=(int)(sizeof(wetland)/sizeof(wetland[0])); break;
    case DESERT:   arr=desert;   n=(int)(sizeof(desert)/sizeof(desert[0])); break;
    case TROPICAL: arr=tropical; n=(int)(sizeof(tropical)/sizeof(tropical[0])); break;
    case ALPINE:   arr=alpine;   n=(int)(sizeof(alpine)/sizeof(alpine[0])); break;
    case ALIEN:    arr=alien;    n=(int)(sizeof(alien)/sizeof(alien[0])); break;
    default: break;
  }
  return arr[h % (uint32_t)n];
}

static inline const uint8_t* pickAnimalGlyph(Biome b, uint32_t h){
  const uint8_t* meadow[]   = {BUG, BIRD, RAB, OWL};
  const uint8_t* wetland[]  = {BUG, BIRD, DRGN1, CRAB1};
  const uint8_t* desert[]   = {SCOR1, SNAKE, BUG};
  const uint8_t* tropical[] = {CRAB1, JELY1, BUG, BIRD};
  const uint8_t* alpine[]   = {OWL, YETI, BIRD};
  const uint8_t* alien[]    = {CRAW1, ORB1, AYY, EX};
  const uint8_t** arr = meadow; int n = (int)(sizeof(meadow)/sizeof(meadow[0]));
  switch(b){
    case MEADOW:   arr=meadow;   n=(int)(sizeof(meadow)/sizeof(meadow[0])); break;
    case WETLAND:  arr=wetland;  n=(int)(sizeof(wetland)/sizeof(wetland[0])); break;
    case DESERT:   arr=desert;   n=(int)(sizeof(desert)/sizeof(desert[0])); break;
    case TROPICAL: arr=tropical; n=(int)(sizeof(tropical)/sizeof(tropical[0])); break;
    case ALPINE:   arr=alpine;   n=(int)(sizeof(alpine)/sizeof(alpine[0])); break;
    case ALIEN:    arr=alien;    n=(int)(sizeof(alien)/sizeof(alien[0])); break;
    default: break;
  }
  return arr[h % (uint32_t)n];
}

static inline void generateSpeciesPools(World& w){
  for (int i=0;i<256;++i) gDynGlyph[i] = nullptr;
  w.flora.clear(); w.fauna.clear();
  for (int i=0;i<64;++i){
    uint32_t h = hash3((uint32_t)i, biomeSalt(w.biome), w.worldSeed ^ 0xA5A5u);
    PlantSpec ps;
    ps.glyph = pickPlantGlyph(w.biome, h);
    ps.moisturePref = u01(h>>1);
    ps.heightPref   = u01(h>>9);
    ps.bloominess   = 0.15f + 0.55f*u01(h>>17);
    float sat = (w.biome==DESERT)?0.30f:0.55f;
    float val = (w.biome==ALIEN)?0.78f:0.92f;
    ps.base   = pastelFrom(h, sat, val);
    ps.accent = pastelFrom(h^0x9E3779B9u, std::min(0.75f,sat+0.15f), std::min(0.98f,val+0.04f));
    w.flora.push_back(ps);
    gDynGlyph[PLANT_BASE + (unsigned char)i] = ps.glyph;
  }
  for (int i=0;i<48;++i){
    uint32_t h = hash3((uint32_t)i, biomeSalt(w.biome)^0x13579u, w.worldSeed ^ 0x5A5Au);
    AnimalSpec as;
    as.glyph = pickAnimalGlyph(w.biome, h);
    as.moisturePref = u01(h>>2);
    as.heightPref   = u01(h>>10);
    as.speed        = 0.15f + 0.85f*u01(h>>18);
    as.herbivory    = u01(h>>6);
    as.glow         = (w.biome==ALIEN || w.biome==WETLAND) ? (0.12f*u01(h>>20)) : (0.05f*u01(h>>20));
    float sat = 0.45f;
    float val = (w.biome==ALIEN)?0.80f:0.88f;
    as.base   = pastelFrom(h^0xCAFEBABEu, sat, val);
    as.accent = pastelFrom(h^0x1234567u, std::min(0.70f,sat+0.18f), std::min(0.96f,val+0.06f));
    w.fauna.push_back(as);
    gDynGlyph[ANIM_BASE + (unsigned char)i] = as.glyph;
  }
}

static inline char floraTokenForCell(const World& w, int x, int y, Season s, int tick){
  if (w.flora.empty()) return 'T';
  float moist = (!w.moist.empty()) ? (float)w.moist[y][x] / 255.0f : 0.0f;
  float water = (!w.water.empty()) ? (float)w.water[y][x] / 7.0f : 0.0f;
  float m = clamp01(0.65f*moist + 0.35f*water);
  float h = (float)w.height[y][x] / 255.0f;
  uint32_t hs = speciesSeed2(w, x, y, s, (uint32_t)(tick*2654435761u));
  int a = (int)(hs % 64u);
  int best=a; float bestScore=1e9f;
  for (int k=0;k<6;++k){
    int id = (a + k*11) & 63;
    const PlantSpec& ps = w.flora[id];
    float dm = ps.moisturePref - m;
    float dh = ps.heightPref - h;
    float score = dm*dm + 0.7f*dh*dh;
    if (score < bestScore){ bestScore=score; best=id; }
  }
  return (char)(PLANT_BASE + (unsigned char)best);
}

static inline char faunaTokenForCell(const World& w, int x, int y, Season s, int tick){
  if (w.fauna.empty()) return 'b';
  float moist = (!w.moist.empty()) ? (float)w.moist[y][x] / 255.0f : 0.0f;
  float water = (!w.water.empty()) ? (float)w.water[y][x] / 7.0f : 0.0f;
  float m = clamp01(0.6f*moist + 0.4f*water);
  float h = (float)w.height[y][x] / 255.0f;
  uint32_t hs = speciesSeed2(w, x, y, s, (uint32_t)(tick^0xBEEFu));
  int a = (int)(hs % 48u);
  int best=a; float bestScore=1e9f;
  for (int k=0;k<6;++k){
    int id = (a + k*7) % 48;
    const AnimalSpec& as = w.fauna[id];
    float dm = as.moisturePref - m;
    float dh = as.heightPref - h;
    float score = dm*dm + 0.7f*dh*dh;
    if (score < bestScore){ bestScore=score; best=id; }
  }
  return (char)(ANIM_BASE + (unsigned char)best);
}

static inline void reskinWorld(World& w, Season s, int tick){
  if (w.flora.empty() || w.fauna.empty()) return;
  for (int y=0;y<H;++y){
    for (int x=0;x<W;++x){
      char& tc = w.terrain[y][x];
      if (isPlantToken(tc) && uc(tc) < PLANT_BASE) tc = floraTokenForCell(w,x,y,s,tick);
      char& ec = w.entities[y][x];
      if (isAnimalToken(ec) && uc(ec) < ANIM_BASE) ec = faunaTokenForCell(w,x,y,s,tick);
    }
  }
}

static void seedWorld(World& w, Rng& r, Biome biome) {
  
  w.worldSeed = r.u32();
w.biome = biome;
  generateSpeciesPools(w);
  w.targetBiome = biome;
  w.biomeFade = 0.0f;
  w.biomeFadeDir = 0;
  w.bw = weightsFor(biome);

  w.terrain.assign(H, std::string(W, '.'));
  w.entities.assign(H, std::string(W, ' '));
  w.overlay.assign(H, std::string(W, ' '));
  w.water.assign(H, std::vector<uint8_t>(W, 0));
  w.height.assign(H, std::vector<uint8_t>(W, 0));
  w.moist.assign(H, std::vector<uint8_t>(W, 0));
  w.springs.clear();

  w.wind = Wind{0,0,0};
  w.weather = Weather{};
  w.clouds = Clouds{};
  initClouds(w.clouds, r, biome);
  w.cloudOpacity = 1.0f;
  if (biome==TROPICAL) w.cloudOpacity = 0.40f;
  if (biome==DESERT)   w.cloudOpacity = 0.35f;
  if (biome==ALIEN)    w.cloudOpacity = 0.75f;
  genHeight(w, (uint32_t)r.i(0, 0x7fffffff));
  // Biome-specific base terrain + sea level shaping
  if (biome == DESERT) {
    // Mostly sand, very sparse vegetation; water only in small oases.
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
      w.terrain[y][x] = 's';
      w.water[y][x] = 0;
    }
    // Carve a few oases: shallow water + reeds + palms
    int oases = 2 + (r.oneIn(2)?1:0);
    for (int o=0;o<oases;++o) {
      int cx=r.i(W/6, W-1-W/6), cy=r.i(H/6, H-1-H/6);
      int rad=r.i(10, 22);
      for (int y=cy-rad; y<=cy+rad; ++y) for (int x=cx-rad; x<=cx+rad; ++x) {
        if (!inBounds(x,y)) continue;
        int dx=x-cx, dy=y-cy;
        if (dx*dx+dy*dy > rad*rad) continue;
        int d2 = dx*dx+dy*dy;
        if (d2 < (rad*rad)/3) w.water[y][x] = (uint8_t)std::max<int>(w.water[y][x], 3);
        else if (d2 < (rad*rad)*2/3) w.water[y][x] = (uint8_t)std::max<int>(w.water[y][x], 2);
        else w.water[y][x] = (uint8_t)std::max<int>(w.water[y][x], 1);
      }
    }
  } else if (biome == TROPICAL) {
    // Ocean-heavy: low altitude becomes sea; high altitude becomes islands.
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
      uint8_t alt = w.height[y][x];
      if (alt < 120) { w.water[y][x] = 5; w.terrain[y][x] = '.'; }
      else if (alt < 150) { w.water[y][x] = 3; w.terrain[y][x] = '.'; }
      else {
        w.water[y][x] = 0;
        w.terrain[y][x] = (alt > 210) ? '^' : '.';
      }
    }
  }


  int basePonds = std::max(4, (W * H) / 9000);
  int ponds = std::max(2, (int)(basePonds * w.bw.pondDensity));
  for (int p=0; p<ponds; ++p) {
    int marginX = std::max(12, W/18);
    int marginY = std::max(8,  H/18);
    int cx = r.i(marginX, W-1-marginX);
    int cy = r.i(marginY, H-1-marginY);
    int rad = r.i(5, 14);

    for (int y=cy-rad; y<=cy+rad; ++y) for (int x=cx-rad; x<=cx+rad; ++x) {
      if (!inBounds(x,y)) continue;
      int dx=x-cx, dy=y-cy;
      int d2 = dx*dx + dy*dy;
      if (d2 <= rad*rad + r.i(-5,5)) {
        uint8_t depth = (uint8_t)std::clamp(7 - (d2 / std::max(1,rad)), 2, 7);
        w.water[y][x] = std::max<uint8_t>(w.water[y][x], depth);
      }
    }
  }

  // height-driven cliffs / boulders / mountain ridges
    // Gentle springs in some ponds/lakes so water doesn't vanish over time.
  // (Helps preserve those nice seed-born ponds even if weather stays dry.)
  {
    int target = std::max(2, ponds);
    int added = 0;
    for (int tries=0; tries<6000 && added<target; ++tries) {
      int x = r.i(0, W-1), y = r.i(0, H-1);
      if (w.water[y][x] >= 5) { w.springs.push_back({x,y}); ++added; }
    }
  }

for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
    if (w.water[y][x] > 0) continue;
    uint8_t alt = w.height[y][x];
    if (alt > 245) { w.terrain[y][x] = 'M'; continue; }
    if (alt > 232 && r.oneIn(2)) w.terrain[y][x] = '^';
    if (alt > 238 && r.oneIn(3)) w.terrain[y][x] = 'B';
  }

  for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
    if (w.water[y][x] > 0) continue;
    uint8_t alt = w.height[y][x];
    // altGrow is currently not used in seedWorld; kept for potential future tuning.
    // float altGrow = 1.0f;
    // if (alt > 220) altGrow *= 0.45f;
    // else if (alt > 200) altGrow *= 0.65f;
    // else if (alt < 80) altGrow *= 1.25f;

    int wet = countNeighborsWater(w.water, x, y);
    if (wet > 0 && r.oneIn(2)) w.terrain[y][x] = ',';
    if (wet > 0 && r.u01() < 0.08f * w.bw.reedChance) w.terrain[y][x] = ':';
  }

  for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
    if (w.water[y][x] > 0) continue;
    if (r.u01() < 0.0016f * w.bw.stoneChance) w.terrain[y][x] = '^';
  }

  // Mud tends to form near water edges (adds earth tones)
  for (int k=0; k< (W*H)/420; ++k) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (w.water[y][x]>0) continue;
    int wet = countNeighborsWater(w.water, x, y);
    if (wet>0 && r.oneIn(2) && w.terrain[y][x]=='.') w.terrain[y][x]='d';
    if (wet>1 && r.oneIn(3) && (w.terrain[y][x]==','||w.terrain[y][x]=='"')) w.terrain[y][x]='d';
  }

  // Extra boulders (earthy accents)
  for (int k=0; k< (W*H)/9000; ++k) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (w.water[y][x]>0) continue;
    if (w.terrain[y][x]=='.' || w.terrain[y][x]=='^') w.terrain[y][x]='B';
  }
  // DESERT cactus scatter
  if (biome == DESERT) {
    for (int k=0; k< (W*H)/180; ++k) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (w.water[y][x]>0) continue;
      if (w.terrain[y][x]=='s' && r.oneIn(3)) w.terrain[y][x]='c';
    }
  }


  int starters = (W * H) / 700;
  for (int k=0; k<starters; ++k) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (w.water[y][x]==0 && w.terrain[y][x]==',' && r.oneIn(3)) w.terrain[y][x] = '#';
  }
  for (int k=0; k< (W*H)/900; ++k) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (w.water[y][x]==0 && (w.terrain[y][x]==','||w.terrain[y][x]=='#') && r.u01() < 0.20f*w.bw.fernChance) w.terrain[y][x] = ';';
  }
  for (int k=0; k< (W*H)/2200; ++k) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (w.water[y][x]==0 && (w.terrain[y][x]=='#' || w.terrain[y][x]==',') && r.u01() < 0.35f*w.bw.treeChance)
      w.terrain[y][x] = (w.biome==TROPICAL ? (r.oneIn(2)?'P':(r.oneIn(2)?'T':'Y')) : (r.oneIn(2) ? 'T' : 'Y'));
  }

  for (int k=0; k< (W*H)/700; ++k) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (w.water[y][x]==0 && (w.terrain[y][x]=='.' || w.terrain[y][x]==',') && countNeighborsWater(w.water, x, y)>0 && r.u01() < 0.40f*w.bw.mushChance)
      w.terrain[y][x] = 'm';
  }
  for (int k=0; k< (W*H)/900; ++k) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (w.water[y][x]==0 && (w.terrain[y][x]==',' || w.terrain[y][x]=='"' || w.terrain[y][x]==';') && r.u01() < 0.30f*w.bw.flowerChance) {
      float t = r.u01();
      if (t < 0.12f*w.bw.bigFlowerChance) w.terrain[y][x] = '&';
      else w.terrain[y][x] = (r.oneIn(2) ? 'f' : '+');
    }
  }

  if (biome != ALPINE) {
    for (int k=0; k< (W*H)/2400; ++k) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (w.water[y][x]==0 && (w.terrain[y][x]=='#' || w.terrain[y][x]==';') && r.oneIn(2)) w.terrain[y][x] = '$';
    }
  }

  for (int k=0; k< (W*H)/9000; ++k) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (w.water[y][x]==0 && (w.terrain[y][x]==',' || w.terrain[y][x]=='"') && r.oneIn(2)) w.terrain[y][x] = '!';
  }

  // ---- Hydrology: initialize groundwater moisture + persistent springs ----
  {
    float moistScale = 1.0f;
    if (biome==WETLAND)  moistScale = 1.35f;
    if (biome==DESERT)   moistScale = 0.35f;
    if (biome==TROPICAL) moistScale = 1.15f;
    if (biome==ALPINE)   moistScale = 0.60f;
    if (biome==ALIEN)    moistScale = 0.90f;

    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
      uint8_t alt = w.height[y][x];
      int base = 120 - (int)alt/2; // lowlands = wetter
      if (base < 0) base = 0;
      int m = (int)(base * moistScale) + r.i(0, 18);
      if (m > 180) m = 180;
      w.moist[y][x] = (uint8_t)m;
    }

    int springCount = 2;
    if (biome==WETLAND) springCount = 3;
    if (biome==DESERT)  springCount = 1;
    if (biome==ALPINE)  springCount = 1;
    if (biome==TROPICAL) springCount = 2;
    if (biome==ALIEN)   springCount = 2;

    auto tooClose = [&](int x,int y){
      for (auto &p : w.springs) {
        int dx = p.first - x, dy = p.second - y;
        if (dx*dx + dy*dy < 70*70) return true;
      }
      return false;
    };

    for (int sidx=0; sidx<springCount; ++sidx) {
      int bestX = W/2, bestY = H/2;
      int bestScore = 1e9;

      for (int tries=0; tries<2600; ++tries) {
        int x = r.i(10, W-11);
        int y = r.i(10, H-11);
        if (tooClose(x,y)) continue;

        uint8_t alt = w.height[y][x];
        // Avoid very high peaks; prefer low basins, but allow midlands for rivers.
        if (biome==ALPINE && alt > 200) continue;
        if (biome!=ALPINE && alt > 210) continue;

        int nmin = 255;
        for (int oy=-1; oy<=1; ++oy) for (int ox=-1; ox<=1; ++ox) {
          if (ox==0 && oy==0) continue;
          uint8_t a2 = w.height[y+oy][x+ox];
          if (a2 < nmin) nmin = a2;
        }

        // Score: low altitude + "basin-ness" bonus.
        int basinBonus = (nmin - (int)alt);
        int score = (int)alt*3 - basinBonus*6 + r.i(0, 30);

        if (biome==DESERT) score += (int)alt; // deserts: prefer the lowest of the low
        if (score < bestScore) { bestScore = score; bestX = x; bestY = y; }
      }

      w.springs.push_back({bestX, bestY});

      // Seed a stable pool at the source + wetter soil around it.
      w.water[bestY][bestX] = (uint8_t)std::max<int>(w.water[bestY][bestX], 6);
      for (int oy=-3; oy<=3; ++oy) for (int ox=-3; ox<=3; ++ox) {
        int x = bestX + ox, y = bestY + oy;
        if (!inBounds(x,y)) continue;
        int dist2 = ox*ox + oy*oy;
        if (dist2 <= 2) w.water[y][x] = (uint8_t)std::max<int>(w.water[y][x], 4);
        int add = (dist2<=4) ? 80 : 35;
        int mm = (int)w.moist[y][x] + add;
        if (mm > 255) mm = 255;
        w.moist[y][x] = (uint8_t)mm;
      }
    }
  }

}

// ---------------- Water flow ----------------
static void stepWater(World& w, Rng& r) {
  Water next = w.water;

  // Persistent springs: keep rivers/lakes alive long-term.
  if (!w.springs.empty()) {
    for (auto &p : w.springs) {
      int sx=p.first, sy=p.second;
      int add = 2;
      if (w.biome==DESERT) add = 1;
      if (w.weather.state==STORM) add = 3;
      if (w.weather.state==RAIN)  add = 2;
      int v = (int)next[sy][sx] + add;
      if (v > 7) v = 7;
      next[sy][sx] = (uint8_t)v;

      // Gentle seep around springs (helps a visible outflow form).
      for (int oy=-1; oy<=1; ++oy) for (int ox=-1; ox<=1; ++ox) {
        if (!ox && !oy) continue;
        int x=sx+ox, y=sy+oy;
        if (!inBounds(x,y)) continue;
        if (r.oneIn(18) && next[y][x] < 5) next[y][x]++;
      }
    }
  }

  // Groundwater recharge: dry tiles with stored moisture can re-wet, especially in basins.
  for (int i=0; i<(W*H)/80; ++i) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (next[y][x] != 0) continue;
    uint8_t m = w.moist[y][x];
    if (m < 20) continue;
    uint8_t alt = w.height[y][x];
    // Basin-ish: lower than neighbors
    int nmin = 255;
    for (int oy=-1; oy<=1; ++oy) for (int ox=-1; ox<=1; ++ox) {
      if (!ox && !oy) continue;
      int nx=x+ox, ny=y+oy;
      if (!inBounds(nx,ny)) continue;
      nmin = std::min<int>(nmin, (int)w.height[ny][nx]);
    }
    bool basin = ((int)alt + 2 <= nmin);
    int denom = basin ? 180 : 420;
    if (w.biome==DESERT) denom *= 2;
    if (w.biome==WETLAND) denom = std::max(80, denom/2);
    if (r.oneIn(denom)) {
      next[y][x] = 1;
      w.moist[y][x] = (uint8_t)std::max<int>(0, (int)m - 18);
    }
  }

  int baseMoves = (W*H)/2;
  int windMoves = (W*H)/8 * w.wind.strength;
  int rainMoves = (w.weather.state==RAIN||w.weather.state==STORM) ? (W*H)/10 : 0;
  int moves = baseMoves + windMoves + rainMoves;

  if (w.weather.rainStrength > 0.05f) {
    int hits = (int)((W*H)/420 * w.weather.rainStrength); // less global flooding
    for (int i=0; i<hits; ++i) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      int wetN = countNeighborsWater(next, x, y);
      if (wetN == 0 && !r.oneIn(7)) continue;
      if (next[y][x] < 7 && r.oneIn(2)) next[y][x]++;
    }
  }

  
  // Moisture slowly decays (prevents infinite buildup).
  for (int i=0; i<(W*H)/60; ++i) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    uint8_t &m = w.moist[y][x];
    if (m==0) continue;
    if (r.oneIn(10)) m--; // very gentle
  }

for (int k=0; k<moves; ++k) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    uint8_t d = next[y][x];
    if (d==0) continue;

    int dirs[6][2] = {{0,1},{-1,1},{1,1},{-1,0},{1,0},{0,-1}};
    int bestNx=x, bestNy=y, bestScore=999999;

    for (int i=0; i<6; ++i) {
      int nx=x+dirs[i][0], ny=y+dirs[i][1];
      if (!inBounds(nx,ny)) continue;
      int score = (int)next[ny][nx]*10 + i;

      if (w.wind.strength>0) {
        int dot = dirs[i][0]*w.wind.dx + dirs[i][1]*w.wind.dy;
        score -= dot * (2 + w.wind.strength);
      }
      score += r.i(0,4);

      if (score < bestScore) { bestScore=score; bestNx=nx; bestNy=ny; }
    }

    if (bestNx==x && bestNy==y) continue;
    uint8_t nd = next[bestNy][bestNx];

    if (nd + 1 < d) { next[y][x]--; next[bestNy][bestNx]++; }
    else if (w.wind.strength>=3 && r.oneIn(7) && nd < d) { next[y][x]--; next[bestNy][bestNx]++; }
    else if (r.oneIn(12) && nd < d) { next[y][x]--; next[bestNy][bestNx]++; }
  }

  for (int y=0;y<H;++y) for (int x=0;x<W;++x) next[y][x] = (uint8_t)std::min<int>(7, next[y][x]);
  w.water.swap(next);
}


static void waterSinks(World& w, Rng& r, Season s) {
  // Prevent long-term "oceanification" by adding sinks.
  float evap = 0.00006f; // evaporation (very gentle so ponds/lakes persist)
  if (s == SUMMER) evap *= 1.6f;
  if (s == WINTER) evap *= 0.55f;

  if (w.weather.state == CLEAR)    evap *= 1.35f;
  if (w.weather.state == OVERCAST) evap *= 0.95f;
  if (w.weather.state == RAIN)     evap *= 0.45f;
  if (w.weather.state == STORM)    evap *= 0.35f;

  evap *= (1.0f + 0.10f * (float)w.wind.strength);

  float infil = 0.00012f; // infiltration (gentle; shallow puddles fade, lakes stay)
  if (w.biome == TROPICAL) infil *= 0.85f;
  if (w.biome == DESERT) infil *= 1.85f;
  if (s == SUMMER) infil *= 1.15f;
  if (s == WINTER) infil *= 0.75f;

  float edgeDrain = 0.0016f; // lower edge drainage
  if (w.biome == WETLAND) edgeDrain *= 0.60f;
  if (w.biome == TROPICAL) edgeDrain *= 0.70f;

  for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
    uint8_t &d = w.water[y][x];
    if (d == 0) continue;


    // Basins/lowlands retain water better.
    uint8_t alt = w.height[y][x];
    int nmin = 255;
    for (int oy=-1; oy<=1; ++oy) for (int ox=-1; ox<=1; ++ox) {
      if (!ox && !oy) continue;
      int nx=x+ox, ny=y+oy;
      if (!inBounds(nx,ny)) continue;
      nmin = std::min<int>(nmin, (int)w.height[ny][nx]);
    }
    bool basin = ((int)alt + 2 <= nmin);
    float retain = 1.0f;
    if (alt < 90) retain *= 0.75f;
    if (basin) retain *= 0.35f;
    if (d >= 4) retain *= 0.55f; // deep water rarely infiltrates/evaps away

    // Infiltration for shallow puddles on porous ground/vegetation.
    if (d <= 2) {
      char t = w.terrain[y][x];
      bool porous =
        (t=='.' || t==',' || t=='"' || t==';' || t=='#' || t==':' || t=='d' ||
         t=='m' || t=='f' || t=='+' || t=='&' || t=='!' || t=='$' || isTree(t));
      if (porous && r.u01() < infil*retain) {
        d--; 
        int mm = (int)w.moist[y][x] + 22;
        if (mm > 255) mm = 255;
        w.moist[y][x] = (uint8_t)mm;
        continue;
      }
    }

    // Evaporation.
    if (r.u01() < evap*retain) {
      d--; 
      int mm = (int)w.moist[y][x] + 6;
      if (mm > 255) mm = 255;
      w.moist[y][x] = (uint8_t)mm;
      continue;
    }

    // Edge drainage.
    bool nearEdge = (x < 2 || y < 2 || x > W-3 || y > H-3);
    if (nearEdge && d <= 3 && r.u01() < edgeDrain*retain) {
      d--; 
      continue;
    }
  }
}



// ---------------- Terrain ecology ----------------
static void stepTerrain(World& w, Rng& r, Season s, int tick) {
  Grid next = w.terrain;

  float springBoost = (s==SPRING) ? 1.35f : 1.0f;
  float autumnMush  = (s==AUTUMN) ? 1.35f : 1.0f;
  float winterSlow  = (s==WINTER) ? 1.55f : 1.0f;

  float rainBoost = (w.weather.state==RAIN || w.weather.state==STORM) ? (1.0f + 0.7f*w.weather.rainStrength) : 1.0f;

  for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
    if (w.water[y][x] > 0) {
      if (w.terrain[y][x] == '*') next[y][x] = 'x';
      continue;
    }

    char c = w.terrain[y][x];

    // altitude drives ecology (mountains sparser, valleys richer)
    uint8_t alt = w.height[y][x];
    float altGrow = 1.0f;
    if (alt > 220) altGrow *= 0.45f;
    else if (alt > 200) altGrow *= 0.65f;
    else if (alt < 80) altGrow *= 1.25f;

    if (c=='*') { next[y][x] = (r.oneIn(3) ? 'x' : '*'); continue; }
    if (c=='x') {
      int wet = countNeighborsWater(w.water, x, y);
      if (wet>0 && r.oneIn(6)) next[y][x]=',';
      else if (r.oneIn((int)(35*winterSlow))) next[y][x]='.';
      continue;
    }

    if (isVeg(c)) {
      bool ignite = false;
      for (int dy=-1; dy<=1 && !ignite; ++dy) for (int dx=-1; dx<=1 && !ignite; ++dx) {
        if (dx==0 && dy==0) continue;
        int nx=x+dx, ny=y+dy;
        if (!inBounds(nx,ny)) continue;
        if (w.terrain[ny][nx]=='*') {
          int dot = dx*w.wind.dx + dy*w.wind.dy;
          int boost = 0;
          if (w.wind.strength>0) boost += std::max(0, dot)*w.wind.strength;
          if (w.weather.state==STORM) boost += 2;
          int denom = std::max(2, (int)(10 - boost));
          // tropical slightly more fire-prone during storms
          if (w.biome==TROPICAL && w.weather.state==STORM) denom = std::max(2, denom-1);
          if (r.oneIn(denom)) ignite = true;
        }
      }
      if (ignite) { next[y][x]='*'; continue; }
    }

    int wet = countNeighborsWater(w.water, x, y);
    int g   = countNeighborsChar(w.terrain, x, y, ',');
    int tg  = countNeighborsChar(w.terrain, x, y, '"');
    int sh  = countNeighborsChar(w.terrain, x, y, '#');
    int tr  = countNeighborsChar(w.terrain, x, y, 'T') + countNeighborsChar(w.terrain, x, y, 'Y') + countNeighborsChar(w.terrain, x, y, 'P');
    int flo = countNeighborsChar(w.terrain, x, y, 'f') + countNeighborsChar(w.terrain, x, y, '+') +
              countNeighborsChar(w.terrain, x, y, '&') + countNeighborsChar(w.terrain, x, y, '!') +
              countNeighborsChar(w.terrain, x, y, '$');
    int fern= countNeighborsChar(w.terrain, x, y, ';');
    int reeds=countNeighborsChar(w.terrain, x, y, ':');

    if (c=='.') {
      int fert = wet*3 + g + tg + flo + fern;
      float p = 0.0032f * fert * w.bw.growRate * springBoost * rainBoost * altGrow / winterSlow;
      if (w.biome==TROPICAL) p *= 1.25f;
      if (fert>0 && r.u01() < p) next[y][x] = ',';
      if (wet>0 && r.u01() < 0.05f * w.bw.reedChance * rainBoost / winterSlow) next[y][x] = ':';
      // (disabled) rockification over time tended to turn the whole world into cliffs.
      continue;
    }

    if (c=='^') {
      if (wet>0 && r.oneIn((int)(220 / (w.bw.growRate*rainBoost)))) next[y][x] = ',';
      continue;
    }

    if (c==':') {
      if (wet==0 && r.oneIn((int)(18*winterSlow))) next[y][x]='.';
      if (wet>0 && r.oneIn(40)) {
        int nx=x+r.i(-1,1), ny=y+r.i(-1,1);
        if (inBounds(nx,ny) && w.water[ny][nx]==0 && w.terrain[ny][nx]=='.') next[ny][nx]=':';
      }
      continue;
    }

    if (c==';') {
      if (wet==0 && r.oneIn((int)(30*winterSlow))) next[y][x]=',';
      if (wet>0 && r.oneIn((int)(55 / (springBoost*rainBoost)))) {
        int nx=x+r.i(-1,1), ny=y+r.i(-1,1);
        if (inBounds(nx,ny) && w.water[ny][nx]==0 && (w.terrain[ny][nx]==',' || w.terrain[ny][nx]=='.')) next[ny][nx]=';';
      }
      continue;
    }

    if (c=='$') {
      if (s==SUMMER && wet>0 && r.oneIn(180)) {
        int nx=x+r.i(-1,1), ny=y+r.i(-1,1);
        if (inBounds(nx,ny) && w.water[ny][nx]==0 && (w.terrain[ny][nx]==',' || w.terrain[ny][nx]==';')) next[ny][nx]='$';
      }
      if (s==WINTER && r.oneIn(80)) next[y][x]='#';
      continue;
    }

    if (c==',') {
      if ((g+tg)>=4 && r.oneIn((int)(90*winterSlow))) next[y][x]='"';
      float flowerScale = (alt > 200) ? 0.35f : 1.0f;
      if (wet>0 && r.u01() < (0.005f * springBoost * rainBoost * w.bw.bloomRate * flowerScale)) {
        float t = r.u01();
        if (t < 0.05f * w.bw.bigFlowerChance) next[y][x] = '!';
        else if (t < 0.16f * w.bw.bigFlowerChance) next[y][x] = '&';
        else next[y][x] = (r.oneIn(2) ? 'f' : '+');
      }
      if (wet>0 && r.oneIn((int)(260 / (springBoost*rainBoost)))) next[y][x]=';';
      if ((wet+g+tg+reeds)==0 && r.oneIn((int)(75*winterSlow))) next[y][x]='.';
      continue;
    }

    if (c=='"') {
      if ((g+tg)>=5 && r.oneIn((int)(140*winterSlow))) next[y][x]='#';
      if (wet>0 && r.u01() < (0.004f * springBoost * rainBoost * w.bw.bloomRate)) {
        float t=r.u01();
        if (t < 0.10f*w.bw.bigFlowerChance) next[y][x]='&';
        else next[y][x] = (r.oneIn(2)?'f':'+');
      }
      if (wet==0 && r.oneIn((int)(120*winterSlow))) next[y][x]=',';
      continue;
    }

    if (c=='#') {
      if ((sh+tr)>=3 && wet>0 && r.oneIn((int)(280*winterSlow)))
        next[y][x]=(w.biome==TROPICAL ? (r.oneIn(2)?'P':(r.oneIn(2)?'T':'Y')) : (r.oneIn(2)?'T':'Y'));
      if (wet==0 && r.oneIn((int)(170*winterSlow))) next[y][x]='"';
      if (wet>0 && r.u01() < 0.006f * autumnMush * rainBoost * w.bw.mushChance) next[y][x]='m';
      if (s==SUMMER && r.oneIn(220) && w.biome!=ALPINE) next[y][x]='$';
      continue;
    }

    if (isTree(c)) {
      if (wet>0 && r.oneIn((int)(230 / (rainBoost)))) {
        int nx=x+r.i(-1,1), ny=y+r.i(-1,1);
        if (inBounds(nx,ny) && w.water[ny][nx]==0 && (w.terrain[ny][nx]=='.'||w.terrain[ny][nx]==',')) {
          int pick = r.i(1, 10);
          if (pick <= 3) next[ny][nx]='m';
          else if (pick <= 5) next[ny][nx]=';';
          else if (pick <= 7) next[ny][nx]='$';
          else if (pick == 8) next[ny][nx]='!';
          else next[ny][nx]=(r.oneIn(2)?'f':'+');
        }
      }
      if (wet==0 && r.oneIn((int)(1400*winterSlow))) next[y][x]='#';
      continue;
    }

    if (c=='m') {
      if (wet==0 && tr==0 && r.oneIn((int)(28*winterSlow))) next[y][x]='.';
      if ((wet+tr)>=2 && r.u01() < 0.02f * autumnMush * rainBoost * w.bw.mushChance) {
        int nx=x+r.i(-1,1), ny=y+r.i(-1,1);
        if (inBounds(nx,ny) && w.water[ny][nx]==0 && (w.terrain[ny][nx]=='.'||w.terrain[ny][nx]==',')) next[ny][nx]='m';
      }
      continue;
    }

    if (c=='f' || c=='+' || c=='&' || c=='!' ) {
      int fade = 170;
      if (s==WINTER) fade = 70;
      if (s==SPRING) fade = 230;
      if (w.weather.state==STORM) fade = std::max(40, fade-30);
      if (w.biome==TROPICAL) fade = (int)(fade * 1.25f);
      if (r.oneIn((int)(fade*winterSlow))) next[y][x] = (r.oneIn(2)?',':'.');

      if (wet>0 && (g+tg+fern)>=3 && r.u01() < 0.0045f * springBoost * rainBoost * w.bw.bloomRate) {
        int nx=x+r.i(-1,1), ny=y+r.i(-1,1);
        if (inBounds(nx,ny) && w.water[ny][nx]==0 && (w.terrain[ny][nx]==','||w.terrain[ny][nx]=='"'||w.terrain[ny][nx]==';')) {
          float t=r.u01();
          if (t < 0.06f*w.bw.bigFlowerChance) next[ny][nx]='!';
          else if (t < 0.18f*w.bw.bigFlowerChance) next[ny][nx]='&';
          else next[ny][nx]=(r.oneIn(2)?'f':'+');
        }
      }
      continue;
    }
  }

  w.terrain.swap(next);
}

// ---------------- Lightning ----------------
static void lightning(World& w, Rng& r, std::string& banner) {
  banner = "STORM: lightning!";
  int strikes = 2 + r.i(0, 4);
  for (int s=0; s<strikes; ++s) {
    int cx=r.i(0,W-1), cy=r.i(0,H-1);
    for (int k=0; k<260; ++k) {
      int x=cx+r.i(-20,20), y=cy+r.i(-12,12);
      if (!inBounds(x,y)) continue;
      if (w.water[y][x] > 0) continue;
      if (isVeg(w.terrain[y][x]) && r.oneIn(2)) w.terrain[y][x]='*';
    }
  }
}

// ---------------- Chaos ----------------
static void chaosAlien(World& w, Rng& r, std::string& banner) {
  banner = "Alien: reality flexes";
  for (int tries=0; tries<800; ++tries) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (w.water[y][x]>0) continue;
    if (w.entities[y][x]==' ') { w.entities[y][x]='A'; break; }
  }
  for (int k=0; k<340; ++k) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (r.oneIn(9)) w.water[y][x] = (uint8_t)r.i(0,7);
    if (w.water[y][x]==0) {
      char &t = w.terrain[y][x];
      if (t=='.' && r.oneIn(3)) t=',';
      else if (t==',' && r.oneIn(3)) t=(r.oneIn(2)?'"':(r.oneIn(2)?';':':'));
      else if (t=='"' && r.oneIn(5)) t='#';
      else if (t=='#' && r.oneIn(6)) t=(r.oneIn(2)?'T':'Y');
      else if (isTree(t) && r.oneIn(18)) t='*';
      else if (t==',' && r.oneIn(20)) t='!';
    }
  }
}

static void maybeChaos(World& w, Rng& r, std::string& banner, Season s) {
  int base = (w.biome==ALIEN) ? 900 : (w.biome==TROPICAL ? 1100 : 1400);
  if (!r.oneIn(base)) { banner = "calm"; return; }

  int roll = r.i(1, 100);
  if (s==SPRING) {
    if (roll <= 55) banner = "spring bloom";
    else if (roll <= 65) chaosAlien(w,r,banner);
    else banner = "fresh wind";
  } else if (s==SUMMER) {
    if (roll <= 45) banner = "summer heat";
    else if (roll <= 70) chaosAlien(w,r,banner);
    else banner = "wild gusts";
  } else if (s==AUTUMN) {
    if (roll <= 35) banner = "spore drift";
    else if (roll <= 55) chaosAlien(w,r,banner);
    else banner = "autumn hush";
  } else {
    if (roll <= 20) chaosAlien(w,r,banner);
    else banner = "winter hush";
  }
}

// ---------------- Entities ----------------
static void stepEntities(World& w, Rng& r, Season s, int tick) {
  auto isHerb = [&](char c)->bool{
    unsigned char U=uc(c);
    if (U>=ANIM_BASE && U<ANIM_BASE+48 && !w.fauna.empty()) return w.fauna[(int)(U-ANIM_BASE)].herbivory > 0.55f;
    return (c=='r' || c=='d');
  };
  auto isPred = [&](char c)->bool{
    unsigned char U=uc(c);
    if (U>=ANIM_BASE && U<ANIM_BASE+48 && !w.fauna.empty()) return w.fauna[(int)(U-ANIM_BASE)].herbivory < 0.35f;
    return (c=='s' || c=='y' || c=='a');
  };


  Grid cur = w.entities;
  Grid out = cur;

  auto countEnt = [&](char c){
    int total=0;
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) if (cur[y][x]==c) total++;
    return total;
  };

  int bugs = countEnt('b');
  int birds = countEnt('v');
  int rabbits = countEnt('r');
  int snakes = countEnt('n');
  int fireflies = countEnt('F');
  int owls = countEnt('O');
  int deer = countEnt('D');

  int him = countEnt((char)0x19);
  int her = countEnt((char)0x1A);
  int scorpions = countEnt((char)0x16);
  int dragonflies = countEnt((char)0x17);
  int crabs = countEnt((char)0x18);
  int jellies = countEnt((char)0x1B);
  int crawlers = countEnt((char)0x1C);
  int orbs = countEnt((char)0x1D);

int whales = countEnt('W');
int dinos  = countEnt('Q');


  // Big / rare creatures
if (w.biome==TROPICAL && whales < WHALE_CAP && r.oneIn(180)) {
  for (int tries=0; tries<60; ++tries) {
    int x = r.i(2, W-3), y = r.i(2, H-3);
    uint8_t wd = w.water[y][x];
    if (wd >= 5 && w.entities[y][x]==' ' && w.terrain[y][x] != '^') { // deep enough, not cliff
      w.entities[y][x] = 'W';
      break;
    }
  }
}
if ((w.biome==MEADOW || w.biome==ALPINE) && dinos < DINO_CAP && r.oneIn(9000)) {
  for (int tries=0; tries<80; ++tries) {
    int x = r.i(2, W-3), y = r.i(2, H-3);
    if (w.water[y][x] != 0) continue;
    if (w.entities[y][x] != ' ') continue;
    char t = w.terrain[y][x];
    if (t=='^' || t=='~') continue;
    // Prefer greener / forested areas
    bool lush = (t=='T' || t=='Y' || t=='F' || t=='P' || t=='G' || t=='f');
    if (!lush && !r.oneIn(5)) continue;
    w.entities[y][x] = 'Q';
    break;
  }
}

auto canSpawnAt = [&](int x,int y)->bool{
    if (!inBounds(x,y)) return false;
    if (out[y][x] != ' ') return false;
    char t = w.terrain[y][x];
    if (t=='*' || t=='^' || t=='B') return false;
    if (w.water[y][x] > 0) return false;
    return true;
  };

  // Legendary wandering couple (ultra rare). They never despawn.
  if (him==0 && her==0 && r.oneIn(520000)) {
    for (int tries=0; tries<6000; ++tries) {
      int x=r.i(2,W-3), y=r.i(2,H-3);
      if (!canSpawnAt(x,y)) continue;
      int nx=x+(r.oneIn(2)?1:-1), ny=y;
      if (!canSpawnAt(nx,ny)) continue;
      out[y][x]=(char)0x19;
      out[ny][nx]=(char)0x1A;
      break;
    }
  }

  // Distinct biome fauna (kept sparse so density stays sane)
  if (w.biome==DESERT && scorpions < SCORPION_CAP && r.oneIn(22)) {
    for (int tries=0; tries<2000; ++tries) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (!canSpawnAt(x,y)) continue;
      if (w.terrain[y][x]=='s' || w.terrain[y][x]=='.') { out[y][x]=(char)0x16; break; }
    }
  }

  if (w.biome==WETLAND && dragonflies < DRAGONFLY_CAP && r.oneIn(14)) {
    for (int tries=0; tries<2200; ++tries) {
      int x=r.i(1,W-2), y=r.i(1,H-2);
      if (!canSpawnAt(x,y)) continue;
      bool nearWater=false;
      for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        if(!dx&&!dy) continue;
        if (w.water[y+dy][x+dx]>0) nearWater=true;
      }
      if (!nearWater) continue;
      out[y][x]=(char)0x17; break;
    }
  }

  if (w.biome==TROPICAL && crabs < CRAB_CAP && r.oneIn(20)) {
    for (int tries=0; tries<2600; ++tries) {
      int x=r.i(1,W-2), y=r.i(1,H-2);
      if (!canSpawnAt(x,y)) continue;
      bool shore=false;
      for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        if(!dx&&!dy) continue;
        if (w.water[y+dy][x+dx]>0) shore=true;
      }
      if (!shore) continue;
      out[y][x]=(char)0x18; break;
    }
  }

  if (w.biome==TROPICAL && jellies < JELLY_CAP && r.oneIn(26)) {
    for (int tries=0; tries<2600; ++tries) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (out[y][x] != ' ') continue;
      if (w.water[y][x] >= 4) { out[y][x]=(char)0x1B; break; }
    }
  }

  if (w.biome==ALIEN && crawlers < CRAWLER_CAP && r.oneIn(16)) {
    for (int tries=0; tries<3200; ++tries) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (!canSpawnAt(x,y)) continue;
      out[y][x]=(char)0x1C; break;
    }
  }

  if (w.biome==ALIEN && orbs < ORB_CAP && r.oneIn(80)) {
    for (int tries=0; tries<4000; ++tries) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (!canSpawnAt(x,y)) continue;
      out[y][x]=(char)0x1D; break;
    }
  }
  int yetis = countEnt('H');

  
  if (bugs < BUG_CAP_BASE && r.oneIn(2)) {
    for (int tries=0; tries<180; ++tries) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (!canSpawnAt(x,y)) continue;
      char t = w.terrain[y][x];
      if (t=='f'||t=='+'||t=='&'||t=='!'||t=='$') { out[y][x]='b'; break; }
      if ((t==','||t=='"'||t==';') &&
          (countNeighborsChar(w.terrain,x,y,'f')+countNeighborsChar(w.terrain,x,y,'+')+
           countNeighborsChar(w.terrain,x,y,'&')+countNeighborsChar(w.terrain,x,y,'!') > 0) && r.oneIn(7)) {
        out[y][x]='b'; break;
      }
    }
  }

  if (w.biome != ALPINE && rabbits < RABBIT_CAP_BASE && r.oneIn(10)) {
    for (int tries=0; tries<220; ++tries) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (!canSpawnAt(x,y)) continue;
      char t=w.terrain[y][x];
      if (t==','||t=='"'||t==';') { out[y][x]='r'; break; }
    }
  }

  if (birds < BIRD_CAP_BASE && bugs > BUG_CAP_BASE/6 && r.oneIn(12)) {
    for (int tries=0; tries<200; ++tries) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (canSpawnAt(x,y)) { out[y][x]='v'; break; }
    }
  }

  if (snakes < SNAKE_CAP_BASE && (rabbits > RABBIT_CAP_BASE/10 || bugs > BUG_CAP_BASE/5) && r.oneIn(60)) {
    for (int tries=0; tries<260; ++tries) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (!canSpawnAt(x,y)) continue;
      if (w.terrain[y][x]==','||w.terrain[y][x]=='"'||w.terrain[y][x]=='#') { out[y][x]='n'; break; }
    }
  }

  if (s==SUMMER && nightish(tick) && fireflies < FIREFLY_CAP_BASE && r.oneIn(5)) {
    for (int tries=0; tries<220; ++tries) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (!canSpawnAt(x,y)) continue;
      char t=w.terrain[y][x];
      if (t==','||t=='"'||t==';'||t=='f'||t=='+') { out[y][x]='F'; break; }
    }
  }

  if (s==WINTER && nightish(tick) && owls < OWL_CAP_BASE && r.oneIn(120)) {
    for (int tries=0; tries<260; ++tries) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (canSpawnAt(x,y)) { out[y][x]='O'; break; }
    }
  }

  if (s==WINTER && yetis < YETI_CAP_BASE && r.oneIn(90)) {
    for (int tries=0; tries<260; ++tries) {
      int x=r.i(0,W-1), y=r.i(0,H-1);
      if (!canSpawnAt(x,y)) continue;
      if (w.height[y][x] < 210) continue;
      char t=w.terrain[y][x];
      if (t=='.'||t=='^' || t=='M') { out[y][x]='H'; break; }
    }
  }

  if ((w.biome==MEADOW || w.biome==WETLAND || w.biome==TROPICAL) && deer < DEER_CAP_BASE && r.oneIn(140)) {
    for (int tries=0; tries<420; ++tries) {
      int x=r.i(0,W-2), y=r.i(0,H-2);
      bool ok=true;
      for (int dy=0; dy<2 && ok; ++dy) for (int dx=0; dx<2 && ok; ++dx) {
        if (!canSpawnAt(x+dx,y+dy)) ok=false;
      }
      if (!ok) continue;
      if (w.terrain[y][x]==',' || w.terrain[y][x]=='"' || w.terrain[y][x]==';') { out[y][x]='D'; break; }
    }
  }

  auto canMoveTo = [&](int nx,int ny){
    if (!inBounds(nx,ny)) return false;
    if (cur[ny][nx] != ' ') return false;
    if (blocksEntity(w.terrain[ny][nx], w.water[ny][nx])) return false;
    return true;
  };
  auto moveTo = [&](int x,int y,int nx,int ny,char e){
    out[ny][nx]=e; out[y][x]=' ';
  };

  for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
    char e = cur[y][x];
    if (e==' ') continue;
    if (out[y][x] != e) continue;

    if (e=='b') {
      if (r.oneIn(420)) { out[y][x]=' '; continue; }
      int bestNx=x,bestNy=y,bestScore=999999;
      for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
        if (dx==0 && dy==0) continue;
        int nx=x+dx, ny=y+dy;
        if (!canMoveTo(nx,ny)) continue;
        int score=r.i(0,18);
        char t=w.terrain[ny][nx];
        if (t=='!') score-=120;
        else if (t=='&') score-=90;
        else if (t=='f'||t=='+') score-=70;
        else if (t=='$') score-=55;
        else if (t==';') score-=25;
        else if (t=='"') score-=18;
        else if (t==',') score-=10;
        else if (t=='.') score+=10;
        if (score<bestScore){bestScore=score;bestNx=nx;bestNy=ny;}
      }
      if (bestNx!=x||bestNy!=y) moveTo(x,y,bestNx,bestNy,'b');
      continue;
    }

    if (e=='r') {
      if (r.oneIn(900)) { out[y][x]=' '; continue; }
      int bestNx=x,bestNy=y,bestScore=999999;
      for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
        if (dx==0&&dy==0) continue;
        int nx=x+dx, ny=y+dy;
        if (!canMoveTo(nx,ny)) continue;
        int score = r.i(0,25);
        char t=w.terrain[ny][nx];
        if (t=='"'||t==',') score-=18;
        if (t=='f'||t=='+'||t=='&'||t=='!') score-=25;
        int danger=0;
        for (int oy=-2; oy<=2; ++oy) for (int ox=-2; ox<=2; ++ox) {
          int ax=nx+ox, ay=ny+oy;
          if (!inBounds(ax,ay)) continue;
          if (cur[ay][ax]=='n' || cur[ay][ax]=='v' || cur[ay][ax]=='O') danger++;
        }
        score += danger*20;
        if (score<bestScore){bestScore=score;bestNx=nx;bestNy=ny;}
      }
      if (bestNx!=x||bestNy!=y) {
        moveTo(x,y,bestNx,bestNy,'r');
        char &t = w.terrain[bestNy][bestNx];
        if ((t=='"'||t==',') && r.oneIn(6)) t='.';
        else if ((t=='f'||t=='+'||t=='&'||t=='!') && r.oneIn(2)) t=',';
      }
      continue;
    }

    if (e=='v') {
      if (r.oneIn(700)) { out[y][x]=' '; continue; }
      bool ate=false;
      for (int dy=-1; dy<=1 && !ate; ++dy) for (int dx=-1; dx<=1 && !ate; ++dx) {
        if (dx==0&&dy==0) continue;
        int nx=x+dx, ny=y+dy;
        if (!inBounds(nx,ny)) continue;
        if (cur[ny][nx]=='b' && !blocksEntity(w.terrain[ny][nx], w.water[ny][nx])) {
          out[ny][nx]='v'; out[y][x]=' '; ate=true;
        }
      }
      if (ate) continue;

      int bestNx=x,bestNy=y,bestScore=999999;
      for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
        if (dx==0&&dy==0) continue;
        int nx=x+dx, ny=y+dy;
        if (!canMoveTo(nx,ny)) continue;
        int bugLocal=0;
        for (int oy=-2; oy<=2; ++oy) for (int ox=-2; ox<=2; ++ox) {
          int ax=nx+ox, ay=ny+oy;
          if (inBounds(ax,ay) && cur[ay][ax]=='b') bugLocal++;
        }
        int score = -bugLocal*22 + r.i(0,22);
        if (score<bestScore){bestScore=score;bestNx=nx;bestNy=ny;}
      }
      if (bestNx!=x||bestNy!=y) moveTo(x,y,bestNx,bestNy,'v');
      continue;
    }

    if (e=='n') {
      if (r.oneIn(1800)) { out[y][x]=' '; continue; }
      bool ate=false;
      for (int dy=-1; dy<=1 && !ate; ++dy) for (int dx=-1; dx<=1 && !ate; ++dx) {
        if (dx==0&&dy==0) continue;
        int nx=x+dx, ny=y+dy;
        if (!inBounds(nx,ny)) continue;
        if ((cur[ny][nx]=='r' || cur[ny][nx]=='b') && !blocksEntity(w.terrain[ny][nx], w.water[ny][nx])) {
          out[ny][nx]='n'; out[y][x]=' '; ate=true;
        }
      }
      if (ate) continue;

      int bestNx=x,bestNy=y,bestScore=999999;
      for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
        if (dx==0&&dy==0) continue;
        int nx=x+dx, ny=y+dy;
        if (!canMoveTo(nx,ny)) continue;
        int prey=0;
        for (int oy=-3; oy<=3; ++oy) for (int ox=-3; ox<=3; ++ox) {
          int ax=nx+ox, ay=ny+oy;
          if (!inBounds(ax,ay)) continue;
          if (cur[ay][ax]=='r') prey += 3;
          else if (cur[ay][ax]=='b') prey += 1;
        }
        int score = -prey*10 + r.i(0,30);
        char t=w.terrain[ny][nx];
        if (t=='#') score-=8;
        if (t==','||t=='"') score-=5;
        if (t=='.') score+=8;
        if (score<bestScore){bestScore=score;bestNx=nx;bestNy=ny;}
      }
      if (bestNx!=x||bestNy!=y) moveTo(x,y,bestNx,bestNy,'n');
      continue;
    }

    if (e=='F') {
      if (!nightish(tick) || r.oneIn(260)) { out[y][x]=' '; continue; }
      int nx=x+r.i(-1,1), ny=y+r.i(-1,1);
      if (canMoveTo(nx,ny)) moveTo(x,y,nx,ny,'F');
      continue;
    }

    if (e=='O') {
      if (!nightish(tick) || s!=WINTER || r.oneIn(1400)) { out[y][x]=' '; continue; }
      bool ate=false;
      for (int dy=-1; dy<=1 && !ate; ++dy) for (int dx=-1; dx<=1 && !ate; ++dx) {
        if (dx==0&&dy==0) continue;
        int nx=x+dx, ny=y+dy;
        if (!inBounds(nx,ny)) continue;
        if ((cur[ny][nx]=='v' || cur[ny][nx]=='b') && !blocksEntity(w.terrain[ny][nx], w.water[ny][nx])) {
          out[ny][nx]='O'; out[y][x]=' '; ate=true;
        }
      }
      if (ate) continue;
      int nx=x+r.i(-1,1), ny=y+r.i(-1,1);
      if (canMoveTo(nx,ny)) moveTo(x,y,nx,ny,'O');
      continue;
    }

    if (e=='H') {
      if (s!=WINTER || r.oneIn(900)) { out[y][x]=' '; continue; }
      if (!r.oneIn(2)) continue;
      int bestNx=x,bestNy=y,bestScore=999999;
      for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
        if (dx==0&&dy==0) continue;
        int nx=x+dx, ny=y+dy;
        if (!canMoveTo(nx,ny)) continue;
        int score=r.i(0,25);
        char t=w.terrain[ny][nx];
        if (t=='.') score-=10;
        if (t=='^') score-=8;
        if (isVeg(t)) score+=25;
        if (score<bestScore){bestScore=score;bestNx=nx;bestNy=ny;}
      }
      if (bestNx!=x||bestNy!=y) moveTo(x,y,bestNx,bestNy,'H');
      continue;
    }

    if (e=='W') { // whale (slow drift in deep water)
  if (r.oneIn(2)) continue; // slower
  int dx = (r.oneIn(2)?0:(r.oneIn(2)?-1:1));
  int dy = (r.oneIn(2)?0:(r.oneIn(2)?-1:1));
  // bias slightly with wind
  if (r.oneIn(3)) dx += (w.wind.dx>0?1:(w.wind.dx<0?-1:0));
  if (r.oneIn(3)) dy += (w.wind.dy>0?1:(w.wind.dy<0?-1:0));
  if (dx==0 && dy==0) continue;
  int nx=x+dx, ny=y+dy;
  if (!inBounds(nx,ny)) continue;
  if (w.entities[ny][nx] != ' ') continue;
  if (w.water[ny][nx] < 4) continue; // keep to water
  out[ny*W+nx]='W';
  out[y*W+x]=' ';
  continue;
}
if (e=='Q') { // dinosaur (big land wanderer)
  // slightly faster than deer
  int dx = r.i(-1,1), dy = r.i(-1,1);
  if (dx==0 && dy==0) { if (r.oneIn(3)) dx = (r.oneIn(2)?-1:1); }
  int nx=x+dx, ny=y+dy;
  if (!inBounds(nx,ny)) continue;
  if (w.entities[ny][nx] != ' ') continue;
  if (w.water[ny][nx] != 0) continue;
  char t = w.terrain[ny][nx];
  if (t=='^' || t=='~') continue;
  out[ny*W+nx]='Q';
  out[y*W+x]=' ';
  // occasionally trample small plants into dirt
  if (r.oneIn(6) && (t=='f' || t=='G' || t=='F')) w.terrain[ny][nx]='.';
  continue;
}

if (e=='D') {
      if (r.oneIn(900)) { out[y][x]=' '; continue; }
      if (!r.oneIn(6)) continue;
      int dirx = r.i(-1,1), diry = r.i(-1,1);
      int nx=x+dirx, ny=y+diry;
      if (nx < 0 || ny < 0 || nx > W-2 || ny > H-2) continue;
      bool ok=true;
      for (int dy=0; dy<2 && ok; ++dy) for (int dx=0; dx<2 && ok; ++dx) {
        if (blocksEntity(w.terrain[ny+dy][nx+dx], w.water[ny+dy][nx+dx])) ok=false;
        if (cur[ny+dy][nx+dx] != ' ' && !(nx+dx==x && ny+dy==y)) ok=false;
      }
      if (!ok) continue;
      out[ny][nx] = 'D';
      out[y][x] = ' ';
      if (w.terrain[y][x]==',' && r.oneIn(4)) w.terrain[y][x]='.';
      continue;
    }

    if (e=='A') {
      if (r.oneIn(1200)) { out[y][x]=' '; continue; }
      int nx=x+r.i(-1,1), ny=y+r.i(-1,1);
      if (canMoveTo(nx,ny)) {
        if (w.water[y][x]==0 && r.oneIn(8)) w.terrain[y][x] = (r.oneIn(2)?'m':'!');
        if (r.oneIn(24)) w.water[y][x] = (uint8_t)r.i(0,7);
        moveTo(x,y,nx,ny,'A');
      }
      continue;
    }
  }


  // DESERT fauna spawns (simple)
  if (w.biome == DESERT) {
    if (r.oneIn(28)) {
      for (int tries=0; tries<2000; ++tries) {
        int x=r.i(0,W-1), y=r.i(0,H-1);
        if (w.water[y][x]>0) continue;
        if (w.terrain[y][x] != '`') continue;
        if (out[y][x] != ' ') continue;
        out[y][x] = (r.oneIn(4) ? 'C' : 'L');
        break;
      }
    }
  }


  // TROPICAL sea creatures
  if (w.biome == TROPICAL) {
    if (r.oneIn(18)) {
      for (int tries=0; tries<2500; ++tries) {
        int x=r.i(0,W-1), y=r.i(0,H-1);
        if (w.water[y][x] < 3) continue;
        if (out[y][x] != ' ') continue;
        int roll = r.i(0, 99);
        out[y][x] = (roll < 70 ? 'D' : (roll < 95 ? 'W' : 'S'));
        break;
      }
    }
  }

  w.entities.swap(out);

  // Simple interrelationships: herbivores graze nearby plants; predators occasionally remove nearby prey.
  for (int y=1;y<H-1;++y){
    for (int x=1;x<W-1;++x){
      char me = w.entities[y][x];
      if (me==' ') continue;
      if (isHerb(me) && r.oneIn(7)) {
        for (int k=0;k<5;++k){
          int dx=r.i(-1,1), dy=r.i(-1,1);
          int nx=x+dx, ny=y+dy;
          if (isPlantToken(w.terrain[ny][nx])) { w.terrain[ny][nx] = ' '; break; }
        }
      }
      if (isPred(me) && r.oneIn(12)) {
        for (int k=0;k<5;++k){
          int dx=r.i(-1,1), dy=r.i(-1,1);
          int nx=x+dx, ny=y+dy;
          char prey = w.entities[ny][nx];
          if (prey!=' ' && !isPred(prey)) { w.entities[ny][nx] = ' '; break; }
        }
      }
    }
  }

}

// big ancient tree anchors 'Q'
static void maybeSpawnAncientTree(World& w, Rng& r) {
  if (!r.oneIn(2200)) return;
  for (int tries=0; tries<500; ++tries) {
    int x=r.i(1, W-2), y=r.i(1, H-2);
    if (w.water[y][x] > 0) continue;
    if (w.entities[y][x] != ' ') continue;
    int trees = countNeighborsChar(w.terrain, x, y, 'T') + countNeighborsChar(w.terrain, x, y, 'Y') + countNeighborsChar(w.terrain, x, y, 'P');
    if (trees < 2) continue;
    w.entities[y][x] = 'Q';
    break;
  }
}

// ---------------- Step ----------------
static void step(World& w, Rng& r, std::string& banner, int tick) {
  clearOverlay(w);

  Season s = seasonAt(tick);

  evolveClouds(w.clouds, r, w.wind, w.weather, tick);
  updateWeather(w, r, tick);
  updateWind(w, r, tick);

  if (w.weather.state == STORM && r.oneIn(35)) lightning(w, r, banner);
  maybeChaos(w, r, banner, s);

  stepWater(w, r);
  waterSinks(w, r, s);
  stepTerrain(w, r, s, tick);
  stepEntities(w, r, s, tick);
  reskinWorld(w, s, tick);
  maybeSpawnAncientTree(w, r);

  applyRainOverlay(w, tick);
}

// ---------------- Rendering ----------------

// tiny 8x8 glyphs
static const uint8_t* glyph8(char c) {
  if (gDynGlyph[uc(c)]) return gDynGlyph[uc(c)];

  static const uint8_t BLANK[8]  = {0,0,0,0,0,0,0,0};

  static const uint8_t COMMA[8]  = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x10};
  static const uint8_t DASH[8]   = {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00};
  static const uint8_t WAVE[8]   = {0x00,0x00,0x52,0x2A,0x15,0x0A,0x00,0x00};
  static const uint8_t EQ[8]     = {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00};
  static const uint8_t HASH[8]   = {0x00,0x24,0x7E,0x24,0x24,0x7E,0x24,0x00};
  static const uint8_t PCT[8]    = {0x00,0x62,0x64,0x08,0x10,0x26,0x46,0x00};
  static const uint8_t AT[8]     = {0x00,0x3C,0x42,0x5A,0x5A,0x40,0x3C,0x00};

  static const uint8_t TGRASS[8] = {0x24,0x24,0x24,0x00,0x00,0x00,0x00,0x00}; // "
  static const uint8_t SHRUB[8]  = {0x00,0x24,0x7E,0x24,0x24,0x7E,0x24,0x00}; // #
  static const uint8_t TREE1[8]  = {0x10,0x38,0x54,0x10,0x10,0x10,0x38,0x00}; // T
  static const uint8_t TREE2[8]  = {0x10,0x38,0x54,0x10,0x10,0x28,0x44,0x00}; // Y
  static const uint8_t PALM[8]   = {0x10,0x54,0x38,0x10,0x10,0x10,0x38,0x00}; // P
  static const uint8_t MUSH[8]   = {0x00,0x3C,0x7E,0x7E,0x18,0x18,0x3C,0x00}; // m
  static const uint8_t FLOW1[8]  = {0x10,0x54,0x38,0x7C,0x38,0x54,0x10,0x00}; // +
  static const uint8_t FLOW2[8]  = {0x00,0x10,0x38,0x7C,0x38,0x10,0x00,0x00}; // f
  static const uint8_t BIGF[8]   = {0x28,0x7C,0xFE,0x7C,0xFE,0x7C,0x28,0x00}; // &
  static const uint8_t SUPERB[8] = {0x10,0x7C,0xFE,0x7C,0xFE,0x7C,0x10,0x00}; // !
  static const uint8_t FERN[8]   = {0x10,0x38,0x10,0x38,0x10,0x28,0x44,0x00}; // ;
  static const uint8_t REED[8]   = {0x10,0x10,0x10,0x10,0x28,0x28,0x00,0x00}; // :
  static const uint8_t STONE[8]  = {0x00,0x18,0x3C,0x7E,0x7E,0x3C,0x18,0x00}; // ^
  static const uint8_t FRUIT[8]  = {0x18,0x24,0x42,0x5A,0x7E,0x24,0x18,0x00}; // $
  static const uint8_t STAR[8]   = {0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00}; // *
  static const uint8_t EX[8]     = {0x00,0x42,0x24,0x18,0x18,0x24,0x42,0x00}; // x
  static const uint8_t MUD[8]    = {0x00,0x00,0x3C,0x66,0x5A,0x66,0x3C,0x00}; // d
  static const uint8_t BOUL[8]   = {0x00,0x3C,0x7E,0xDB,0xFF,0xE7,0x7E,0x3C}; // B
  static const uint8_t MOUN[8]   = {0x10,0x38,0x7C,0xFE,0x7C,0x38,0x10,0x00}; // M
  static const uint8_t LILY[8]   = {0x00,0x38,0x7C,0xFE,0xEE,0x7C,0x38,0x00}; // l
  static const uint8_t SAND[8]   = {0x00,0x00,0x18,0x3C,0x7E,0x3C,0x18,0x00}; // s
  static const uint8_t CACT[8]   = {0x18,0x18,0x5A,0x7E,0x5A,0x18,0x18,0x00}; // c
  static const uint8_t LIZ[8] = {0x00,0x18,0x3C,0x66,0x3C,0x18,0x66,0x00}; // L
  static const uint8_t CAM[8] = {0x00,0x3C,0x66,0x7E,0x5A,0x66,0x24,0x00}; // C
  static const uint8_t DOLP[8] = {0x00,0x1C,0x3E,0x7C,0x3E,0x1C,0x08,0x00}; // D
  static const uint8_t WHAL[8] = {0x00,0x3C,0x7E,0xDB,0xFF,0x7E,0x3C,0x00}; 
static const uint8_t DINO[8*8] = {
  0,0,1,1,0,0,0,0,
  0,1,1,1,1,0,0,0,
  1,1,0,1,1,1,0,0,
  1,1,1,1,1,1,1,0,
  0,1,1,1,1,1,0,0,
  0,0,1,1,0,1,0,0,
  0,0,1,0,0,1,0,0,
  0,0,1,0,0,1,0,0,
};
// W
  static const uint8_t SEAM[8] = {0x18,0x3C,0x7E,0xDB,0x7E,0x3C,0x5A,0x00}; // S
  static const uint8_t MONO[8]   = {0x18,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x18}; // X
  static const uint8_t EYE[8]    = {0x00,0x3C,0x42,0xA5,0x81,0xA5,0x42,0x3C}; // E

// Extra fauna glyphs (distinct archetypes) + legendary wanderers
static const uint8_t SCOR1[8] = {0x00,0x10,0x38,0x54,0x38,0x10,0x28,0x00}; // \x1E scorpion
static const uint8_t DRGN1[8] = {0x00,0x24,0x18,0x7E,0x18,0x24,0x42,0x00}; // \x1F dragonfly
static const uint8_t CRAB1[8] = {0x00,0x24,0x7E,0x3C,0x3C,0x7E,0x24,0x00}; // \x20 crab
static const uint8_t JELY1[8] = {0x00,0x3C,0x7E,0x7E,0x3C,0x24,0x24,0x00}; // \x21 jellyfish
static const uint8_t CRAW1[8] = {0x00,0x3C,0x5A,0x3C,0x5A,0x3C,0x24,0x00}; // \x22 crawler
static const uint8_t ORB1 [8] = {0x00,0x18,0x3C,0x7E,0x7E,0x3C,0x18,0x00}; // \x23 orb
static const uint8_t HIM1 [8] = {0x00,0x18,0x18,0x3C,0x5A,0x18,0x24,0x00}; // \x19
static const uint8_t HER1 [8] = {0x00,0x18,0x18,0x3C,0x7E,0x18,0x3C,0x00}; // \x1A


  // Water depth glyphs (ASCII digits '1'..'7' but drawn as waves)
  static const uint8_t WAT1[8] = {0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00}; // 1 (spark)
  static const uint8_t WAT2[8] = {0x00,0x00,0x10,0x00,0x04,0x00,0x00,0x00}; // 2
  static const uint8_t WAT3[8] = {0x00,0x00,0x28,0x00,0x10,0x00,0x00,0x00}; // 3
  static const uint8_t WAT4[8] = {0x00,0x00,0x28,0x00,0x28,0x00,0x00,0x00}; // 4
  static const uint8_t WAT5[8] = {0x00,0x44,0x28,0x00,0x44,0x28,0x00,0x00}; // 5
  static const uint8_t WAT6[8] = {0x00,0x44,0x28,0x00,0x44,0x28,0x00,0x44}; // 6
  static const uint8_t WAT7[8] = {0x44,0x28,0x00,0x44,0x28,0x00,0x44,0x28}; // 7 (dense waves)


  // Water flow-direction glyphs (non-printable chars so they don't collide with gameplay glyphs)
  // Horizontal (\x01..\x07), Vertical (\x08..\x0E), Diagonal (\x0F..\x15)
  static const uint8_t W1H[8] = {0x00,0x00,0x00,0x38,0x00,0x00,0x00,0x00};
  static const uint8_t W2H[8] = {0x00,0x00,0x38,0x00,0x1C,0x00,0x00,0x00};
  static const uint8_t W3H[8] = {0x00,0x00,0x38,0x00,0x38,0x00,0x00,0x00};
  static const uint8_t W4H[8] = {0x00,0x38,0x00,0x38,0x00,0x38,0x00,0x00};
  static const uint8_t W5H[8] = {0x00,0x7C,0x00,0x38,0x00,0x7C,0x00,0x00};
  static const uint8_t W6H[8] = {0x00,0x7C,0x00,0x7C,0x00,0x7C,0x00,0x00};
  static const uint8_t W7H[8] = {0x7C,0x00,0x7C,0x00,0x7C,0x00,0x7C,0x00};

  static const uint8_t W1V[8] = {0x00,0x00,0x10,0x10,0x10,0x00,0x00,0x00};
  static const uint8_t W2V[8] = {0x00,0x10,0x10,0x00,0x10,0x10,0x00,0x00};
  static const uint8_t W3V[8] = {0x10,0x10,0x00,0x10,0x10,0x00,0x10,0x10};
  static const uint8_t W4V[8] = {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00};
  static const uint8_t W5V[8] = {0x1C,0x1C,0x00,0x1C,0x1C,0x00,0x1C,0x1C};
  static const uint8_t W6V[8] = {0x3C,0x00,0x3C,0x00,0x3C,0x00,0x3C,0x00};
  static const uint8_t W7V[8] = {0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C};

  static const uint8_t W1D[8] = {0x00,0x00,0x40,0x03,0x10,0x00,0x00,0x00};
  static const uint8_t W2D[8] = {0x00,0x40,0x03,0x10,0x08,0x00,0x00,0x00};
  static const uint8_t W3D[8] = {0x40,0x03,0x10,0x08,0x04,0x02,0x00,0x00};
  static const uint8_t W4D[8] = {0x40,0x03,0x10,0x08,0x10,0x03,0x40,0x00};
  static const uint8_t W5D[8] = {0x44,0x05,0x11,0x08,0x11,0x05,0x44,0x00};
  static const uint8_t W6D[8] = {0x66,0x33,0x19,0x0C,0x19,0x33,0x66,0x00};
  static const uint8_t W7D[8] = {0x77,0x3B,0x1D,0x0E,0x1D,0x3B,0x77,0x00};

  static const uint8_t BUG[8]    = {0x00,0x18,0x3C,0x5A,0x3C,0x18,0x00,0x00}; // b
  static const uint8_t BIRD[8]   = {0x00,0x00,0x42,0x24,0x18,0x00,0x00,0x00}; // v
  static const uint8_t RAB[8]    = {0x18,0x3C,0x66,0x42,0x42,0x66,0x24,0x00}; // r
  static const uint8_t SNAKE[8]  = {0x00,0x7E,0x40,0x7E,0x02,0x7E,0x00,0x00}; // s
  static const uint8_t GLOW[8]   = {0x00,0x18,0x3C,0x7E,0x3C,0x18,0x00,0x00}; // F
  static const uint8_t OWL[8]    = {0x3C,0x7E,0xDB,0xFF,0xBD,0xDB,0x7E,0x3C}; // O
  static const uint8_t YETI[8]   = {0x3C,0x7E,0xFF,0xDB,0xFF,0xDB,0x7E,0x3C}; // H
  static const uint8_t AYY[8]    = {0x00,0x18,0x24,0x42,0x7E,0x42,0x42,0x00}; // A

  static const uint8_t SLASH[8]  = {0x02,0x04,0x08,0x10,0x03,0x40,0x80,0x00};
  static const uint8_t BSLASH[8] = {0x80,0x40,0x03,0x10,0x08,0x04,0x02,0x00};
  static const uint8_t PIPE[8]   = {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18};

  switch (c) {
    case ',': return COMMA;
    case '-': return DASH;
    case '~': return WAVE;
    case '=': return EQ;
    case '#': return HASH;
    case '%': return PCT;
    case '@': return AT;

    case '"': return TGRASS;
    case 'T': return TREE1;
    case 'Y': return TREE2;
    case 'P': return PALM;
    case 'm': return MUSH;
    case '+': return FLOW1;
    case 'f': return FLOW2;
    case '&': return BIGF;
    case '!': return SUPERB;
    case ';': return FERN;
    case ':': return REED;
    case '^': return STONE;
    case '$': return FRUIT;
    case '*': return STAR;
    case 'x': return EX;
    case 'd': return MUD;
    case 'B': return BOUL;
    case 'M': return MOUN;
    case 'l': return LILY;
    case '`': return SAND;
    case 'c': return CACT;
    case 'L': return LIZ;
    case 'C': return CAM;
    case 'D': return DOLP;
    case 'Q': return DINO;
    case 'W': return WHAL;
    case 'S': return SEAM;
    case 'X': return MONO;
    case 'E': return EYE;
    case '1': return WAT1;
    case '2': return WAT2;
    case '3': return WAT3;
    case '4': return WAT4;
    case '5': return WAT5;
    case '6': return WAT6;
    case '7': return WAT7;
    case '\x01': return W1H; case '\x02': return W2H; case '\x03': return W3H; case '\x04': return W4H; case '\x05': return W5H; case '\x06': return W6H; case '\x07': return W7H;
    case '\x08': return W1V; case '\x09': return W2V; case '\x0A': return W3V; case '\x0B': return W4V; case '\x0C': return W5V; case '\x0D': return W6V; case '\x0E': return W7V;
    case '\x0F': return W1D; case '\x10': return W2D; case '\x11': return W3D; case '\x12': return W4D; case '\x13': return W5D; case '\x14': return W6D; case '\x15': return W7D;

    case 'b': return BUG;
    case 'v': return BIRD;
    case 'r': return RAB;
    case 'n': return SNAKE;
    case 's': return SAND;
    case 'F': return GLOW;
    case 'O': return OWL;
    case 'H': return YETI;
    case 'A': return AYY;

    case '/': return SLASH;
    case '\\': return BSLASH;
    case '|': return PIPE;

        case '\x19': return HIM1;
    case '\x1A': return HER1;
    case '\x16': return SCOR1;
    case '\x17': return DRGN1;
    case '\x18': return CRAB1;
    case '\x1B': return JELY1;
    case '\x1C': return CRAW1;
    case '\x1D': return ORB1;
    default: return BLANK;
  }
}

static inline void applySeasonTint(uint8_t& r, uint8_t& g, uint8_t& b, Season s, float p) {
  struct Off { int dr,dg,db; };
  auto offsFor = [&](Season ss)->Off{
    switch (ss) {
      case SPRING: return Off{-4, +18, -4};
      case SUMMER: return Off{+8, +10, +0};
      case AUTUMN: return Off{+22, -2, -10};
      case WINTER: return Off{-12, -8, +12};
    }
    return Off{0,0,0};
  };
  Season s2 = (Season)((s+1)%4);
  Off a = offsFor(s), b2 = offsFor(s2);

  float t=p;
  int rr = int(r) + int(a.dr*(1.f-t) + b2.dr*t);
  int gg = int(g) + int(a.dg*(1.f-t) + b2.dg*t);
  int bb = int(b) + int(a.db*(1.f-t) + b2.db*t);

  if (s==WINTER || s2==WINTER) {
    int gray = (rr+gg+bb)/3;
    float wgt = (s==WINTER) ? (0.35f + 0.35f*t) : (0.35f*(1.f-t));
    rr = int(rr*(1.f-wgt) + gray*wgt);
    gg = int(gg*(1.f-wgt) + gray*wgt);
    bb = int(bb*(1.f-wgt) + gray*wgt);
  }

  r = clampU8(rr); g=clampU8(gg); b=clampU8(bb);
}

static inline void vividify(uint8_t& R, uint8_t& G, uint8_t& B) {
  float r = R/255.f, g = G/255.f, b = B/255.f;
  float L = (r+g+b)/3.f;
  r = L + (r-L)*VIVID_SAT;
  g = L + (g-L)*VIVID_SAT;
  b = L + (b-L)*VIVID_SAT;
  r *= VIVID_VAL; g *= VIVID_VAL; b *= VIVID_VAL;
  R = clampU8((int)(r*255.f));
  G = clampU8((int)(g*255.f));
  B = clampU8((int)(b*255.f));
}

static inline void pastelWash(uint8_t& R, uint8_t& G, uint8_t& B, float amt) {
  const int wr = 248, wg = 246, wb = 240;
  R = clampU8((int)(R*(1.f-amt) + wr*amt));
  G = clampU8((int)(G*(1.f-amt) + wg*amt));
  B = clampU8((int)(B*(1.f-amt) + wb*amt));
}

static inline char terrainGlyphVariant(char t, uint32_t h, Season s, const Weather& we) {
  uint32_t k = h & 7u;
  if (t=='f' || t=='+' || t=='&' || t=='!') {
    if ((s==SPRING || we.state==RAIN || we.state==STORM) && k==0) return '!';
    if (k==1) return '&';
    if (k==2) return '+';
    return t;
  }
  return t;
}

static inline char renderCharAt(const World& w, int x, int y) {
  char e = w.entities[y][x];
  if (e != ' ') {
    if (e=='D') return 'r';
    if (e=='Q') return 'T';
    return e;
  }

  for (int ay=y-1; ay<=y; ++ay) for (int ax=x-1; ax<=x; ++ax) {
    if (!inBounds(ax,ay)) continue;
    if (w.entities[ay][ax]=='D') {
      int dx = x-ax, dy = y-ay;
      if (dx>=0 && dx<2 && dy>=0 && dy<2) {
        if ((dx^dy)==0) return 'r';
        return 'v';
      }
    }
  }

  for (int ay=y-1; ay<=y+1; ++ay) for (int ax=x-1; ax<=x+1; ++ax) {
    if (!inBounds(ax,ay)) continue;
    if (w.entities[ay][ax]=='Q') {
      int dx=x-ax, dy=y-ay;
      if (dx>=-1 && dx<=1 && dy>=-1 && dy<=1) {
        return ((dx+dy)&1) ? 'Y' : 'T';
      }
    }
  }

  char o = w.overlay[y][x];
  if (o != ' ') return o;

  uint8_t d = w.water[y][x];
  if (d>0) {
    // shallow-water lily pads (visual only)
    if (d <= 2 && w.entities[y][x]==' ' && w.overlay[y][x]==' ') {
      uint32_t hh = hash3((uint32_t)x,(uint32_t)y, 555u);
      if ((hh % 13u)==0u) return 'l';
      if ((hh % 97u)==0u) return 'f';
    }
    return waterGlyph(d);
  }

  return w.terrain[y][x];
}

static RGB baseBgFor(const World& /*w*/, int /*x*/, int /*y*/, int /*tick*/, Season /*s*/, float /*sp*/) {
  // 0.33: background is always pure black; all color lives in glyphs.
  return RGB{0,0,0};
}

static RGB fgForChar(const World& w, char c, Season s, float sp, int tick, int x, int y) {
  unsigned char U = uc(c);
  if (U >= PLANT_BASE && U < PLANT_BASE + 64 && !w.flora.empty()) {
    const PlantSpec& ps = w.flora[(int)(U-PLANT_BASE)];
    RGB col = ps.base;
    uint32_t hs = speciesSeed2(w, x, y, s, (uint32_t)tick);
    float drift = 0.10f + 0.15f * ((hs>>8)&255)/255.0f;
    uint8_t rr=col.r, gg=col.g, bb=col.b;
    applySeasonTint(rr,gg,bb,s,drift);
    if (((hs>>20)&1023u) < (uint32_t)(ps.bloominess*1023.0f)) {
      rr = (uint8_t)((rr + ps.accent.r)/2);
      gg = (uint8_t)((gg + ps.accent.g)/2);
      bb = (uint8_t)((bb + ps.accent.b)/2);
    }
    return RGB{rr,gg,bb};
  }
  if (U >= ANIM_BASE && U < ANIM_BASE + 48 && !w.fauna.empty()) {
    const AnimalSpec& as = w.fauna[(int)(U-ANIM_BASE)];
    RGB col = as.base;
    uint32_t hs = speciesSeed2(w, x, y, s, (uint32_t)(tick^0xBEEFu));
    float drift = 0.08f + 0.12f * ((hs>>10)&255)/255.0f;
    uint8_t rr=col.r, gg=col.g, bb=col.b;
    applySeasonTint(rr,gg,bb,s,drift);
    if (((hs>>22)&1023u) < 180u) {
      rr = (uint8_t)((rr + as.accent.r)/2);
      gg = (uint8_t)((gg + as.accent.g)/2);
      bb = (uint8_t)((bb + as.accent.b)/2);
    }
    if (as.glow > 0.01f) {
      float pulse = 0.5f + 0.5f*std::sin(0.03f*(float)tick + (float)(x*17+y*11));
      rr = (uint8_t)std::min(255, (int)rr + (int)(as.glow*55.0f*pulse));
      gg = (uint8_t)std::min(255, (int)gg + (int)(as.glow*55.0f*pulse));
      bb = (uint8_t)std::min(255, (int)bb + (int)(as.glow*65.0f*pulse));
    }
    return RGB{rr,gg,bb};
  }


  // 0.33: All colors are on glyphs; background stays black.
  // sp = cloud shadow strength (0..~0.6). We dim fg under shadow.
  uint32_t h = hash3((uint32_t)x, (uint32_t)y, (uint32_t)(tick/12));
  auto pick = [&](const std::initializer_list<RGB>& pal)->RGB {
    size_t n = pal.size();
    size_t idx = (size_t)((h ^ (h>>11) ^ (h>>21)) % (uint32_t)n);
    auto it = pal.begin();
    std::advance(it, idx);
    return *it;
  };
  auto jitter = [&](RGB c0, int amt)->RGB {
    int jr = (int)((h>>8)&7) - 3;
    int jg = (int)((h>>11)&7) - 3;
    int jb = (int)((h>>14)&7) - 3;
    c0.r = clampU8((int)c0.r + jr*amt);
    c0.g = clampU8((int)c0.g + jg*amt);
    c0.b = clampU8((int)c0.b + jb*amt);
    return c0;
  };

  RGB fg{235,235,235};

  // Empty space: invisible on black
  if (c=='.' || c==' ') fg = {0,0,0};

  // Water depth glyphs from waterGlyph(): ASCII-safe digits '1'..'7'
  else if ((unsigned char)c >= 0x01 && (unsigned char)c <= 0x15) {
    unsigned char uc = (unsigned char)c;
    int d = 1;
    if (uc <= 0x07) d = 1 + (uc - 0x01);
    else if (uc <= 0x0E) d = 1 + (uc - 0x08);
    else d = 1 + (uc - 0x0F);
    uint32_t wh = hash3((uint32_t)x, (uint32_t)y, (uint32_t)(tick/5));
    int shimmer = (int)((wh & 3u)) - 1;
// Blue-forward palette (no pea-green water). Deeper -> darker/denser.
// Two-step base so biomes can tint slightly without going wild.
int sr=40, sg=175, sb=215;   // shallow
int dr=10, dg=70,  db=155;   // deep
if (w.biome==TROPICAL) { sr=35; sg=190; sb=220; dr=8;  dg=85;  db=165; }
if (w.biome==DESERT)   { sr=50; sg=165; sb=205; dr=15; dg=75;  db=150; } // mineral/ochre tint
float t = (d<=1)?0.f: (d>=7?1.f: (d-1)/6.f);
int r = (int)(sr + (dr-sr)*t);
int g = (int)(sg + (dg-sg)*t);
int b = (int)(sb + (db-sb)*t);

// Shimmer: subtle, mostly in highlights.
int sh = shimmer;
r += (int)(sh * (1.f - t) * 0.6f);
g += (int)(sh * (1.f - t) * 1.0f);
b += (int)(sh * (1.f - t) * 1.4f);

// Depth darkening + seasonal adjustment.
int dark = d * 9;
if (s==WINTER) dark += 10;
if (s==SUMMER) dark -= 4;
r = std::max(0, r - dark);
g = std::max(0, g - (int)(dark*0.7f));
b = std::max(0, b - (int)(dark*0.45f));
fg = { clampU8(r), clampU8(g), clampU8(b) };
  }
  else if (c>='1' && c<='7') {
    int d = (int)(c - '0'); // 1..7
    int r = 42 - d*2;
    int g = 118 + d*4;
    int b = 180 + d*9;
    fg = { clampU8(r), clampU8(g), clampU8(b) };
  }

  // Plants
  else if (c==',' ) { // short grass
    fg = jitter(pick({ {88,180,110},{68,156,96},{58,140,92},{96,196,132},{78,170,120} }), 6);
  }
  else if (c=='"') { // tall grass
    fg = jitter(pick({ {66,170,98},{52,152,86},{44,136,78},{74,186,122} }), 6);
  }
  else if (c==';') { // shrubs
    fg = jitter(pick({ {52,146,86},{42,126,78},{36,116,72},{62,160,96} }), 5);
  }
  else if (c=='#') { // reeds
    fg = jitter(pick({ {72,190,120},{62,170,112},{82,206,132} }), 5);
  }
  else if (c==':') { // moss terrain
    fg = jitter(pick({ {54,160,100},{44,140,90},{64,178,110} }), 5);
  }
  else if (c=='m') { // mushrooms
    fg = jitter(pick({ {230,210,190},{210,180,220},{255,150,180},{200,245,255},{255,240,170} }), 4);
  }
  else if (c=='d') { // mud/dirt
    fg = jitter(pick({ {128,90,62},{110,74,52},{150,110,80} }), 4);
  }
  else if (c=='B' || c=='^' || c=='M') { // rocks
    if (c=='M') fg = jitter(pick({ {140,160,185},{120,140,170},{165,185,210} }), 3);
    else if (c=='^') fg = jitter(pick({ {160,160,175},{140,140,160},{185,185,205} }), 3);
    else fg = jitter(pick({ {170,170,178},{152,152,160},{190,190,198} }), 3);
  }
  else if (c=='l') { // lily pads
    fg = jitter(pick({ {92,210,140},{72,190,120},{110,230,160} }), 4);
  }
  else if (c=='T' || c=='t') { // bark
    fg = jitter(pick({ {140,98,66},{120,82,56},{165,120,84} }), 3);
  }
  else if (c=='F' || c=='P') { // foliage/fronds
    fg = jitter(pick({ {60,190,120},{46,170,110},{70,210,140} }), 5);
  }
  else if (c=='f' || c=='+' || c=='&' || c=='!') { // flowers
    if (w.biome==TROPICAL) {
      fg = jitter(pick({ {255,80,120},{255,140,60},{255,220,60},{140,220,255},{190,120,255} }), 7);
    } else {
      fg = jitter(pick({ {255,160,190},{255,220,120},{200,170,255},{160,220,255},{255,190,140},{255,120,150},{245,245,245},{210,255,160} }), 6);
    }
    if (c=='+') { fg.r = clampU8((int)fg.r + 12); fg.g = clampU8((int)fg.g + 8); }
    if (c=='&') { fg.b = clampU8((int)fg.b + 14); }
    if (c=='!') { fg.r = clampU8((int)fg.r + 16); fg.b = clampU8((int)fg.b + 10); }
  }

  // Creatures / special effects
  else if (c=='r') fg = {255,245,220};
  else if (c=='b') fg = {220,255,180};
  else if (c=='v') fg = {210,210,255};
  else if (c=='H') fg = {230,245,255};
  else if (c=='A') fg = {170,255,220};
  else if (c=='D') fg = {160, 220, 255}; // dolphin
  else if (c=='Q') { // dinosaur
  // earthy/olive with slight seasonal shift
  int rr=120, gg=150, bb=95;
  if (s==AUTUMN) { rr=160; gg=120; bb=80; }
  if (s==WINTER) { rr=140; gg=140; bb=140; }
  return { (uint8_t)rr, (uint8_t)gg, (uint8_t)bb };
}
if (c=='W') fg = {190, 210, 240}; // whale
  else if (c=='S') fg = {120, 255, 210}; // sea monster
  else if (c=='L') fg = {220, 255, 180}; // lizard
  else if (c=='C') fg = {210, 180, 120}; // camel
  else if (c=='c') fg = {120, 220, 150}; // cactus
  else if (c=='`') fg = {230, 210, 150}; // sand
  else if (c=='X') fg = {180, 120, 255}; // monolith
  else if (c=='E') fg = {255, 110, 140}; // watcher eye

  else if (c=='R') fg = {255,200,120};
  else if (c=='*') fg = {255,255,255};
  else if (c=='o') fg = {255,210,120};
  else if (c=='x') fg = {255,120,120};
  else if (c=='n') fg = {180, 255, 180}; // snake

  // Seasonal tint (subtle)
  if (s == AUTUMN) { fg.r = clampU8((int)fg.r + 10); fg.g = clampU8((int)fg.g - 2); }
  else if (s == WINTER) { fg.r = clampU8((int)fg.r + 6); fg.g = clampU8((int)fg.g + 6); fg.b = clampU8((int)fg.b + 10); }

  // Cloud shadows dim the glyph (bg stays black)
  float dim = std::clamp(1.0f - sp, 0.45f, 1.0f);
  dim *= (1.0f - w.biomeFade);
  fg.r = (uint8_t)(fg.r * dim);
  fg.g = (uint8_t)(fg.g * dim);
  fg.b = (uint8_t)(fg.b * dim);

  vividify(fg.r, fg.g, fg.b);
  return fg;
}
static inline void applyCloudShadow(RGB& bg, uint8_t cloudVal) {
  float c = cloudVal / 255.f;
  float shadow = 1.0f - c * 0.42f;
  bg.r = clampU8((int)(bg.r * shadow));
  bg.g = clampU8((int)(bg.g * shadow));
  bg.b = clampU8((int)(bg.b * shadow));
}

static const char* weatherName(WeatherState st) {
  switch (st) {
    case CLEAR: return "clear";
    case OVERCAST: return "overcast";
    case RAIN: return "rain";
    case STORM: return "storm";
  }
  return "clear";
}
static const char* seasonName(Season s) {
  switch (s) {
    case SPRING: return "spring";
    case SUMMER: return "summer";
    case AUTUMN: return "autumn";
    case WINTER: return "winter";
  }
  return "spring";
}

// ---------------------------------------------------------------------------
// renderWorldRgba — Native pixel renderer for the Deckboy pattern source.
//
// Mirrors the SDL exe's render() cell-for-cell (same glyph variant, color,
// wind highlight, storm-rain brighten, cloud layer), but writes straight
// into a packed RGBA buffer at 1 cell = 8x8 px. No HUD — the pattern is
// pure terrarium. Buffer is kFrameW x kFrameH (1600x896).
// ---------------------------------------------------------------------------
static constexpr int kFrameW = W * 8;
static constexpr int kFrameH = H * 8;

static inline void drawGlyphRgba(std::vector<uint8_t>& px, int cellX, int cellY,
                                 char c, RGB fg) {
  const uint8_t* g = glyph8(c);
  for (int y = 0; y < 8; ++y) {
    uint8_t bits = g[y];
    if (!bits) continue;
    size_t row = (static_cast<size_t>(cellY + y) * kFrameW + cellX) * 4u;
    for (int x = 0; x < 8; ++x) {
      if (bits & (0x80u >> x)) {
        px[row + static_cast<size_t>(x) * 4u + 0] = fg.r;
        px[row + static_cast<size_t>(x) * 4u + 1] = fg.g;
        px[row + static_cast<size_t>(x) * 4u + 2] = fg.b;
      }
    }
  }
}

static void renderWorldRgba(World& w, int tick, std::vector<uint8_t>& out) {
  out.assign(static_cast<size_t>(kFrameW) * kFrameH * 4u, 0);
  for (size_t i = 3; i < out.size(); i += 4) {
    out[i] = 255;  // opaque black background — all color lives in glyphs
  }
  Season s = seasonAt(tick);
  float sp = seasonLerp(tick) * w.cloudOpacity;

  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      uint8_t cloud = sampleCloud(w.clouds, x, y);
      cloud = (uint8_t)std::min<int>(255, (int)(cloud * w.cloudOpacity));
      if (w.biome == TROPICAL) cloud = (uint8_t)std::max<int>(0, (int)cloud - 35);
      if (w.biome == DESERT)   cloud = (uint8_t)std::max<int>(0, (int)cloud - 25);

      char c = renderCharAt(w, x, y);
      bool emptySky = (c == '.' && w.water[y][x] == 0 &&
                       w.entities[y][x] == ' ' && w.overlay[y][x] == ' ');
      if (!emptySky) {
        if (w.entities[y][x] == ' ' && w.overlay[y][x] == ' ' && w.water[y][x] == 0) {
          uint32_t hh = hash3((uint32_t)x, (uint32_t)y, (uint32_t)(tick / 6));
          c = terrainGlyphVariant(c, hh, s, w.weather);
        }
        RGB fg = fgForChar(w, c, s, sp, tick, x, y);
        if ((w.terrain[y][x] == ',' || w.terrain[y][x] == '"') && w.wind.strength > 0) {
          uint32_t hh = hash3((uint32_t)x, (uint32_t)y, (uint32_t)(tick / 3));
          if (hh & 1u) {
            fg.g = clampU8((int)fg.g + 20);
            fg.r = clampU8((int)fg.r + 5);
          }
        }
        if ((c == '/' || c == '\\' || c == '|') && w.weather.state == STORM) {
          fg.r = clampU8((int)fg.r + 30);
          fg.g = clampU8((int)fg.g + 30);
          fg.b = clampU8((int)fg.b + 30);
        }
        drawGlyphRgba(out, x * 8, y * 8, c, fg);
      }

      // Cloud layer over the whole cell (matches applyCloudLayer in the exe).
      if (cloud >= 120) {
        float cf = std::clamp((cloud - 120) / 135.f, 0.f, 1.f);
        float a = cf * 60.f / 255.f;
        for (int py = 0; py < 8; ++py) {
          size_t row = (static_cast<size_t>(y * 8 + py) * kFrameW + x * 8) * 4u;
          for (int pxx = 0; pxx < 8; ++pxx) {
            size_t i = row + static_cast<size_t>(pxx) * 4u;
            out[i + 0] = clampU8((int)(out[i + 0] * (1.f - a) + 180.f * a));
            out[i + 1] = clampU8((int)(out[i + 1] * (1.f - a) + 190.f * a));
            out[i + 2] = clampU8((int)(out[i + 2] * (1.f - a) + 210.f * a));
          }
        }
      }
    }
  }
}


// --- Aleatoric glyph bitmaps (8x8 row bitmasks) ---
const uint8_t TGRASS[8] = {0x00,0x10,0x54,0x10,0x54,0x10,0x00,0x00};
const uint8_t SHRUB[8] = {0x00,0x18,0x3C,0x7E,0x3C,0x18,0x00,0x00};
const uint8_t FERN[8] = {0x00,0x10,0x38,0x54,0x10,0x38,0x00,0x00};
const uint8_t FLOW1[8] = {0x00,0x10,0x38,0x10,0xEE,0x10,0x00,0x00};
const uint8_t FLOW2[8] = {0x00,0x28,0x10,0x7C,0x10,0x28,0x00,0x00};
const uint8_t TREE1[8] = {0x10,0x38,0x7C,0x38,0x10,0x10,0x10,0x00};
const uint8_t TREE2[8] = {0x38,0x7C,0xFE,0x7C,0x38,0x10,0x10,0x00};
const uint8_t BIGF[8] = {0x00,0x6C,0xFE,0x7C,0x38,0x10,0x00,0x00};
const uint8_t MUSH[8] = {0x00,0x7C,0xFE,0x7C,0x10,0x38,0x00,0x00};
const uint8_t REED[8] = {0x10,0x10,0x10,0x54,0x54,0x10,0x10,0x00};
const uint8_t LILY[8] = {0x00,0x38,0x44,0xBA,0x44,0x38,0x00,0x00};
const uint8_t SAND[8] = {0x00,0x00,0x22,0x00,0x08,0x00,0x44,0x00};
const uint8_t CACT[8] = {0x10,0x10,0x54,0x7C,0x54,0x10,0x10,0x00};
const uint8_t STONE[8] = {0x00,0x3C,0x42,0x81,0x81,0x42,0x3C,0x00};
const uint8_t BOUL[8] = {0x3C,0x7E,0xFF,0xE7,0xFF,0x7E,0x3C,0x00};
const uint8_t MUD[8] = {0x00,0x00,0x3C,0x42,0x3C,0x00,0x00,0x00};
const uint8_t PALM[8] = {0x28,0x10,0x7C,0x10,0x10,0x10,0x10,0x00};
const uint8_t FRUIT[8] = {0x00,0x10,0x28,0x10,0x00,0x10,0x00,0x00};
const uint8_t MOUN[8] = {0x10,0x38,0x7C,0xFE,0x7C,0x38,0x10,0x00};
const uint8_t STAR[8] = {0x10,0x92,0x54,0x38,0x54,0x92,0x10,0x00};
const uint8_t EX[8] = {0x82,0x44,0x28,0x10,0x28,0x44,0x82,0x00};
const uint8_t HASH[8] = {0x28,0xFE,0x28,0xFE,0x28,0x00,0x00,0x00};
const uint8_t PIPE[8] = {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00};
const uint8_t BUG[8] = {0x00,0x38,0x7C,0x54,0x7C,0x38,0x00,0x00};
const uint8_t BIRD[8] = {0x00,0x18,0x3C,0x5A,0x18,0x00,0x00,0x00};
const uint8_t RAB[8] = {0x00,0x24,0x7E,0x5A,0x7E,0x24,0x00,0x00};
const uint8_t OWL[8] = {0x00,0x7E,0xA5,0xBD,0xA5,0x7E,0x00,0x00};
const uint8_t YETI[8] = {0x00,0x3C,0x7E,0xDB,0x7E,0x3C,0x00,0x00};
const uint8_t SCOR1[8] = {0x00,0x38,0x6C,0xFE,0x6C,0x38,0x10,0x00};
const uint8_t SNAKE[8] = {0x00,0x7C,0x02,0x7C,0x80,0x7C,0x00,0x00};
const uint8_t DRGN1[8] = {0x00,0x6C,0xFE,0xD6,0xFE,0x6C,0x00,0x00};
const uint8_t CRAB1[8] = {0x00,0x54,0x7C,0xFE,0x7C,0x54,0x00,0x00};
const uint8_t JELY1[8] = {0x00,0x38,0x7C,0xFE,0x10,0x38,0x00,0x00};
const uint8_t CRAW1[8] = {0x00,0xFC,0x82,0xFE,0x82,0xFC,0x00,0x00};
const uint8_t ORB1[8] = {0x00,0x38,0x44,0x82,0x44,0x38,0x00,0x00};
const uint8_t AYY[8] = {0x00,0x3C,0x42,0xA5,0x81,0x7E,0x00,0x00};


} // namespace terra
