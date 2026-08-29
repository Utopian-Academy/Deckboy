#pragma once

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

// NOTE: deliberately no SDL include here — the sim core is platform-free so
// it can be reused by the plugin build (see plugin/).
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX  // keep std::min/std::max usable
  #endif
  #include <windows.h>
  #include <mmsystem.h>
  #pragma comment(lib, "winmm.lib")
  // windef.h defines legacy near/far as empty macros; the sim uses them as
  // ordinary identifiers.
  #undef near
  #undef far
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

// --- Audio voice count (melodic channels 0..NUM_VOICES-1) ---
static constexpr int NUM_VOICES = 3;

// clamp to [-1, +1]
static inline float clamp11f(float v){ return v<-1.f?-1.f:(v>1.f?1.f:v); }


// World size in cells. Overridable per-target (the pico build uses 200x200).
#ifndef TERRA_W
#define TERRA_W 200
#endif
#ifndef TERRA_H
#define TERRA_H 112
#endif
static constexpr int W = TERRA_W;
static constexpr int H = TERRA_H;

static constexpr int CW = 96;
static constexpr int CH = 54;

static constexpr int DEFAULT_TPS = 6;

static constexpr int SEASON_TICKS = 900; // faster season cycle so it's visible (~30-90s depending on TPS)
static constexpr int DAY_TICKS    = 900;

static constexpr int WIND_CHANGE_TICKS = 220;
static constexpr int MAX_WIND = 5;

enum WeatherState { CLEAR=0, OVERCAST=1, RAIN=2, STORM=3 };

static constexpr float VIVID_SAT = 1.32f;  // rich, life-evoking (bg is black)
static constexpr float VIVID_VAL = 1.10f;  // a touch brighter

static constexpr int BUG_CAP_BASE      = (W * H) / 75;
static constexpr int BIRD_CAP_BASE     = (W * H) / 260;
static constexpr int SCORPION_CAP = (W * H) / 1600;
static constexpr int DRAGONFLY_CAP= (W * H) / 1100;
static constexpr int CRAB_CAP     = (W * H) / 1400;
static constexpr int JELLY_CAP    = (W * H) / 1500;
static constexpr int CRAWLER_CAP  = (W * H) / 1700;
static constexpr int ORB_CAP      = (W * H) / 2600;

static constexpr int FISH_CAP     = (W * H) / 1600;
static constexpr int CRANE_CAP    = (W * H) / 3800;

static constexpr int RABBIT_CAP_BASE   = (W * H) / 420;
static constexpr int SNAKE_CAP_BASE    = (W * H) / 1200;
static constexpr int FIREFLY_CAP_BASE  = (W * H) / 320;
static constexpr int DEER_CAP_BASE     = (W * H) / 2400;
static constexpr int OWL_CAP_BASE      = (W * H) / 2200;
static constexpr int YETI_CAP_BASE     = (W * H) / 3200;

using Grid = std::vector<std::string>;
using Water = std::vector<std::vector<uint8_t>>;

static inline bool inBounds(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }

// Fast xorshift64* RNG. The sim calls this millions of times per second in
// the water/terrain passes; std::mt19937 + per-call distribution objects were
// the single largest cost per tick (and far too slow for a Pi Zero build).
struct Rng {
  uint64_t state;
  Rng(uint32_t seed=0xC0FFEEu) {
    state = (uint64_t)seed * 0x9E3779B97F4A7C15ull + 0xD1B54A32D192ED03ull;
    if (state == 0) state = 0xC0FFEEull;
    u32(); u32();  // scramble away from the seed
  }
  uint32_t u32() {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return (uint32_t)((state * 0x2545F4914F6CDD1Dull) >> 32);
  }
  // Inclusive [a,b] like the old uniform_int_distribution behavior.
  int irange(int a,int b){
    if (b < a) std::swap(a, b);
    uint32_t span = (uint32_t)(b - a) + 1u;
    return a + (int)(((uint64_t)u32() * (uint64_t)span) >> 32);
  }
  float u01(){ return (float)(u32() >> 8) * (1.0f / 16777216.0f); }
  bool oneIn(int n){ return n <= 1 ? true : irange(1, n) == 1; }
  int i(int lo, int hi) { return irange(lo, hi); }
};

enum Mood : uint8_t { MOOD_CALM=0, MOOD_CURIOUS, MOOD_HUNGRY, MOOD_THIRSTY, MOOD_FEARFUL, MOOD_ENRAGED, MOOD_EUPHORIC };
enum Intent : uint8_t { INTENT_WANDER=0, INTENT_FORAGE, INTENT_DRINK, INTENT_FLEE, INTENT_HUNT, INTENT_REST };

struct Agent {
  int id=0;          // stable-ish id (index at spawn)
  int x=0, y=0;
  char glyph='?';

  uint8_t species=0; // abstract species id

  // Vitals (0..1 unless noted)
  float hunger=0.0f;   // 0..1 (1 = starving)
  float thirst=0.0f;   // 0..1 (1 = parched)
  float fatigue=0.0f;  // 0..1 (1 = exhausted)
  float stress=0.0f;   // 0..1
  float health=1.0f;   // 0..1

  Mood mood = MOOD_CALM;
  Intent intent = INTENT_WANDER;

  uint8_t flags=0;     // bit0:panic
};
// Agent flags
static constexpr uint8_t AGF_PANIC   = 1u<<0;
static constexpr uint8_t AGF_LEGEND_A= 1u<<1;
static constexpr uint8_t AGF_LEGEND_B= 1u<<2;

static inline bool isLegendary(const Agent& a){ return (a.flags & (AGF_LEGEND_A|AGF_LEGEND_B))!=0; }


static inline bool isAquatic(char g){ return g=='>'||g=='<'||g=='W'||g=='S'||g=='D'; }
static inline bool isHerbivore(char g){ return g=='r'||g=='D'||g=='K'; }
static inline bool isPredator(char g){ return g=='n'||g=='H'||g=='S'||g=='K'; } // dinos can be predators-ish
static inline bool isBird(char g){ return g=='v'||g=='O'||g=='C'; }

static inline bool isEdiblePlant(char t){
  switch(t){
    case ',': case '"': case ';': case 'f': case 'F': case 'p': case 'y': case 'Y': return true;
    default: return false;
  }
}
static inline char grazed(char t){
  switch(t){
    case '"': return ',';
    case ';': return ',';
    case 'f': return ',';
    case ',': return '.';
    default: return t;
  }
}
static inline uint32_t hash3(uint32_t x, uint32_t y, uint32_t salt) {
  uint32_t h = x * 0x9E3779B1u ^ y * 0x85EBCA6Bu ^ salt * 0xC2B2AE35u;
  h ^= (h >> 16); h *= 0x7FEB352Du; h ^= (h >> 15); h *= 0x846CA68Bu; h ^= (h >> 16);
  return h;
}

static inline uint8_t clampU8(int v){ return (uint8_t)std::clamp(v, 0, 255); }
static inline int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }

inline int countNeighborsChar(const Grid& g, int x, int y, char c) {
  int n = 0;
  for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
    if (dx==0 && dy==0) continue;
    int nx=x+dx, ny=y+dy;
    if (inBounds(nx,ny) && g[ny][nx]==c) n++;
  }
  return n;
}

enum Season { SPRING=0, SUMMER=1, AUTUMN=2, WINTER=3 };

inline constexpr int BIOME_COUNT = 9;

// SKY is the odd one: every other biome looks DOWN at a world, and the sky
// looks UP from underneath it. There is no ground, so the sim's fields are
// repurposed rather than extended — `height` carries cloud body and `water`
// stays empty — and the renderer supplies the whole picture. Appended last:
// saved patches and the voyage stop table both address biomes by index.
enum Biome { MEADOW=0, WETLAND=1, ALPINE=2, ALIEN=3, TROPICAL=4, DESERT=5,
             CITY=6, OCEAN=7, SKY=8 };


// ---- City terrain glyphs ----
// Chosen to be free in the glyph renderer's 8x8 world table AND free of sim
// meaning: 'A'/'b' already had bitmaps and 'n' is the predator sprite, so
// road/mid-rise/neon are R/N/Z. Everything refers to these constants, so
// moving one is a one-line change.
// The city is a biome like any other: the same water, weather and day/night
// run over it, and it grows and decays on its own clock. These live in the
// terrain grid alongside the plants.
inline constexpr char CITY_ROAD    = 'R';  // asphalt
inline constexpr char CITY_WALK    = '_';  // sidewalk
inline constexpr char CITY_LOW     = 'h';  // shops / low-rise
inline constexpr char CITY_MID     = 'N';  // mid-rise block
inline constexpr char CITY_TOWER   = 'k';  // concrete tower
inline constexpr char CITY_GLASS   = 'G';  // glass tower
inline constexpr char CITY_NEON    = 'Z';  // signage
inline constexpr char CITY_BRIDGE  = 'j';  // bridge / expressway deck
inline constexpr char CITY_QUAY    = 'q';  // seawall, dock edge
inline constexpr char CITY_LOT     = 'z';  // vacant lot / construction

// City elevations. `height` doubles as storeys in the city: every cell of a
// building carries the same value, which is how the renderer reads a whole
// facade off the height field. Ground sits above the deepest water the
// harbour can hold (bed 90 + depth 7 * 10 = 160) so the tide stays in it.
inline constexpr int CITY_GROUND  = 170;  // road level
inline constexpr int CITY_WALK_H  = 174;
inline constexpr int CITY_QUAY_H  = 176;
inline constexpr int CITY_PARK_H  = 172;
inline constexpr int CITY_DECK_H  = 190;  // bridges / expressway
inline constexpr int CITY_BASE_H  = 180;  // a 1-storey building
inline constexpr int CITY_STOREY  = 2;    // height units per storey

inline bool isCityBuilding(char c) {
  return c == CITY_LOW || c == CITY_MID || c == CITY_TOWER || c == CITY_GLASS;
}
inline bool isCityPaved(char c) {
  return c == CITY_ROAD || c == CITY_WALK || c == CITY_BRIDGE || c == CITY_QUAY;
}
inline bool isCityGlyph(char c) {
  return isCityBuilding(c) || isCityPaved(c) || c == CITY_NEON || c == CITY_LOT;
}

enum Species : uint8_t {
  SPEC_WANDERER=0,
  SPEC_SHELLBACK=1,
  SPEC_SWARMER=2,
  SPEC_ENGINEER=3,
  SPEC_PARASITE=4,
  SPEC_PACKHUNTER=5,
  SPEC_MYSTIC=6,
  SPEC_TRICKSTER=7,
  SPEC_COUNT=8
};

struct BiomeWeights {
  float pondDensity = 0.0f;
  float stoneChance = 0.0f;
  float reedChance = 0.0f;
  float fernChance = 0.0f;
  float flowerChance = 0.0f;
  float bigFlowerChance = 0.0f;
  float treeChance = 0.0f;
  float mushChance = 0.0f;
  float growRate = 0.0f;
  float bloomRate = 0.0f;
  float fireRate = 0.0f;
  float alienRate = 0.0f;
};

struct Wind {
  int dx = 0;
  int dy = 0;
  int strength = 0;
};

struct Weather {
  WeatherState state = CLEAR;
  float rainStrength = 0.0f;
  int timer = 0;
  bool lastTickWasRaining = false;
};

struct Clouds {
  std::vector<uint8_t> field;
  float offX = 0.0f;
  float offY = 0.0f;

  Clouds() : field(CW * CH, 0) {}
};

struct World {
  uint32_t worldSeed = 0xC0FFEEu;
  // Island mode (world-shaper, works with any biome): radial height
  // falloff into a surrounding ocean ring; map edges act as open sea.
  // Set once before the first seedWorld — persists across reseeds.
  bool island = false;
  // Volcano (1-in-3 volcanic islands): crater vent position, and the tick
  // the current eruption ends (0 = dormant).
  int ventX = -1, ventY = -1;
  int eruptEnd = 0;
  Grid terrain;
  Grid entities;
  Water water;
  std::vector<std::vector<uint8_t>> height;
  Grid overlay;
  Wind wind;
  Weather weather;
  Clouds clouds;
  float cloudOpacity = 1.0f;
  Biome biome = MEADOW;
  Biome targetBiome = MEADOW;
  float biomeFade = 0.0f;
  int biomeFadeDir = 0;
  bool biomeMorphActive = false;
  float biomeMorphT = 0.0f;
  BiomeWeights bwFrom{};
  BiomeWeights bwTo{};
  BiomeWeights bw{};
  std::vector<std::vector<uint8_t>> moist;
  std::vector<std::pair<int,int>> springs;
  std::vector<Agent> agents;
};

// Open water at the map edge: island mode rings the world with sea, and the
// OCEAN biome IS sea. Both want the edges topped up and the offshore cast
// (ships, whale, serpent, seabirds) at work.
inline bool hasOpenSea(const World& w) { return w.island || w.biome == OCEAN; }

inline constexpr char FOAM_GLYPH = '=';
inline constexpr char LILYPAD_GLYPH = 'l';
inline constexpr char KELP_GLYPH = 'u';

// ===== Shared state for new features (zoom/pan, ripples, alea weights) =====
struct Ripple {
  int cx=0, cy=0;
  float t=0.f;
  float amp=3.f;      // displacement amplitude in cells
  float speed=18.f;   // ring speed (cells/sec)
  float width=2.5f;   // ring thickness (cells)
  float chaos=1.f;    // chaos injection multiplier
};
struct AleaWeights {
  float rainChance=1.f;
  float spawnChance=1.f;
  float mutationRate=1.f;
  float drift=1.f;
  float chaos=1.f;
  // music note-off / duration control
  float noteLen=1.f;        // duration multiplier (0.25..2.0 typical)
  float holdChance=0.06f;   // chance to stretch a note longer
};

struct StepEvent {
  int x=0, y=0;
  int dx=0, dy=0;  // movement delta
  char glyph='?';
  float strength=1.f; // 1..2 (for shove etc)
};
inline std::vector<StepEvent> g_stepEvents;

inline int g_zoom = 1;
inline int g_inspectIdx = -1;       // selected agent index
inline bool g_followInspect = false; // camera follows selected agent when true

inline int g_camX = 0;
inline int g_camY = 0;
inline std::vector<Ripple> g_ripples;
inline AleaWeights g_alea;

static constexpr int MOD_N = 70;
inline const char* g_modName[MOD_N] = {
  "water_view", "plants_view", "overlay_view", "agents_view", "agent_speed",
  "stress_mean", "stress_hi", "panic_count", "hunger_mean", "thirst_mean",
  "fatigue_mean", "health_mean", "pred_pressure", "birth_pulse", "death_pulse",
  "ripple_energy", "wind_mag", "season_pos", "cloud_opacity", "raininess",
  "shellback_stress", "swarm_cohesion", "parasite_aura", "engineer_work", "mystic_flux",
  "trickster_mischief", "pack_density", "plant_flux", "water_flux", "stress_flux",
  "hunger_flux", "thirst_flux", "fatigue_flux", "health_flux", "panic_flux",
  "oddity_0", "oddity_1", "oddity_2", "oddity_3", "oddity_4",
  "oddity_5", "oddity_6", "oddity_7", "oddity_8", "oddity_9",
  "oddity_10", "oddity_11", "oddity_12", "oddity_13", "oddity_14",
  // World-clock and live-sky sources (day/night, seasons-of-the-real-world,
  // eruptions, actual weather outside the window).
  "daylight", "golden_hour", "rain_strength", "wave_energy",
  "eruption", "real_temp", "real_wind", "snowing",
  // The city, the sea and the thing that watches. Appended, never inserted:
  // saved patches address slots by index.
  "city_built", "city_skyline", "city_neon", "city_streets", "city_rush",
  "harbour_boats", "open_water", "reef", "apparition", "biolum",
  // The sky. Appended, never inserted.
  "sky_traffic", "sky_wonder"
};
inline float g_modVal[MOD_N] = {0};

// --- Mod Matrix ---
enum ModDest : int {
  DEST_NONE=0,
  DEST_CC11_EXPR=1,
  DEST_CC74_BRIGHT=2,
  DEST_PAN=3,
  DEST_PORTA_V0=4,
  DEST_PORTA_V1=5,
  DEST_PORTA_V2=6,
  DEST_MIDI_CC=7,   // arbitrary CC on the external MIDI port (slot's cc field)
  DEST_COUNT=8
};

static inline const char* modDestName(int d){
  switch(d){
    case DEST_CC11_EXPR: return "CC11 Expr";
    case DEST_CC74_BRIGHT: return "CC74 Bright";
    case DEST_PAN: return "CC10 Pan";
    case DEST_PORTA_V0: return "Porta V0";
    case DEST_PORTA_V1: return "Porta V1";
    case DEST_PORTA_V2: return "Porta V2";
    case DEST_MIDI_CC: return "MIDI CC";
    default: return "None";
  }
}

struct ModMap {
  int src=0;
  int dest=DEST_NONE;
  float amt=0.0f;     // -2..2
  float smooth=0.20f; // 0..0.98
  float state=0.0f;
  bool enabled=false;
  int cc=1;           // DEST_MIDI_CC only: which controller to drive (0..127)
};
static constexpr int MOD_SLOTS=12;
inline ModMap g_modMap[MOD_SLOTS];

// True when any enabled mod-matrix slot targets `dest`. Used to give the
// matrix full authority over a destination: the built-in animated automation
// must yield instead of overwriting the matrix's CC values.
inline bool modMapControls(int dest) {
  for (int i = 0; i < MOD_SLOTS; ++i) {
    if (g_modMap[i].enabled && g_modMap[i].dest == dest) return true;
  }
  return false;
}
inline int g_g_modScroll = 0;
inline int g_g_mmSel = 0;
inline int g_g_mmField = 0;


// Mod-driven targets
inline float g_cc11Expr = 1.0f;
inline float g_cc74Bright = 0.5f;
inline float g_pan01 = 0.5f;
inline float g_porta01[NUM_VOICES] = {0.f, 0.f, 0.f};
// Per-slot outputs for DEST_MIDI_CC (0..1); -1 = slot inactive this pass.
inline float g_modCC01[MOD_SLOTS] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};



// Shared engine functions implemented in terrarium_core.cpp.
void applyModMatrix();
void updateModPool(World& w, int tick, int viewW, int viewH);
Season seasonAt(int tick);
float seasonLerp(int tick);
bool nightish(int tick);

// A kiosk vat runs for years: int tick would overflow (~414 days at 60 tps)
// and sim-clock daylight would go permanently dark. Wrap by a multiple of
// both the day cycle (8*DAY_TICKS = 7200) and the season cycle
// (4*SEASON_TICKS = 3600) so every phase is preserved.
inline int wrapTick(int t) {
  return (t >= 1000000000) ? t - 999993600 : t;  // 999993600 = 7200 * 138888
}

// Day/night cycle. `level` is daylight 0..1 (smoothstep dawn/dusk ramps,
// never a hard flip); `warm` tints the light: +1 = golden hour, 0 = neutral
// noon, negative = cool moonlight.
struct Daylight {
  float level = 1.0f;
  float warm = 0.0f;
};
// 0 = off (always noon), 1 = sim ticks (one day = 8*DAY_TICKS),
// 2 = wall clock (the vat lives in your timezone).
extern int g_daynightMode;
Daylight daylightNow(int tick);

// Season pacing: 0 = sim ticks (a "year" in minutes), 1 = one season per
// real day (four-day year — good kiosk rhythm), 2 = the actual calendar.
extern int g_seasonMode;

// Display brightness 0.05..1.0, live-controlled by the kiosk remote: reads
// ~/.terrarium-brightness (single float) at most once a second. Missing
// file = 1.0. Renderer-only — never affects the sim.
float displayBrightness();

// Ground style for the kiosk: 0 = earth (soft brown soil), 1 = oled (true
// black — the world floats on an off panel). Reads ~/.terrarium-bg
// ("earth"/"oled") at most once a second, like displayBrightness().
int displayBgMode();

// Seconds since the process started (steady clock). The one clock the sim,
// the renderer and the mod matrix all agree on.
float terraSeconds();

// The alien apparition's schedule lives here, not in the renderer, because
// the mod matrix has to know about it too: in the plugin the UI only runs
// when an editor is open, and a modulation source that reads zero whenever
// nobody is looking is not a modulation source. Geometry stays in the
// renderer; this is just "how far out is it, 0..1".
inline constexpr float ALIEN_APPARITION_EPOCH = 900.f;  // a quarter hour
inline constexpr float ALIEN_APPARITION_DWELL = 30.f;
inline constexpr uint32_t ALIEN_APPARITION_ODDS = 11u;  // one epoch in eleven
float alienApparition01(const World& w, float seconds);

// ---- Sky traffic schedule ----
// Lives in core for exactly the reason the apparition's does: in the plugin
// the UI only runs while an editor is open, so anything derived from
// renderer state reads zero whenever nobody is looking, and a modulation
// source that depends on being watched is not a modulation source.
//
// This is the single source of truth for WHETHER a given flyer is currently
// crossing. The renderer calls it too and then supplies only the geometry,
// so the music and the picture can never disagree about what is up there.
struct SkyFlyer {
  float period;   // seconds between chances
  float dwell;    // how long a crossing lasts
  uint32_t s1, s2;
  uint32_t modv;  // shows up when (hash % modv) == modr
  uint32_t modr;
};
// The dragon is the biggest thing in the sky by a wide margin — it spans
// most of the disc — and at one crossing every ~17 minutes it stopped being
// an event and became the weather. `modv` is the rarity dial: the flyer is
// up on 1 epoch in `modv`, so at a 400s period this is now roughly one
// crossing every 87 minutes. Rare enough that seeing it is luck, often
// enough that a long sitting will catch one.
inline constexpr SkyFlyer SKY_DRAGON  = {400.f, 62.f, 0xD2A6047u, 0x11FEu, 13u, 0u};
inline constexpr SkyFlyer SKY_UNICORN = {260.f, 46.f, 0x5EC2E7u, 0x0417u, 3u, 0u};
inline constexpr SkyFlyer SKY_UFO     = {330.f, 52.f, 0x0F0B12u, 0x5A0CEu, 4u, 0u};
inline constexpr SkyFlyer SKY_RIDER   = {290.f, 30.f, 0x21DE2u, 0xC10DDu, 3u, 1u};
inline constexpr SkyFlyer SKY_WITCH   = {355.f, 40.f, 0x17C4Bu, 0xB2003u, 3u, 2u};
// A little plane towing an advertising banner. Low, level and SLOW — that is
// its whole identity next to the airliners, which are high, fast, and take
// any heading they like. Its crossing is long because a banner you cannot
// read the length of is just a smear.
inline constexpr SkyFlyer SKY_BANNER  = {310.f, 54.f, 0xBA22E7u, 0x9F10Du, 3u, 1u};
// A helicopter towing an LED video wall on a chain. The rarest thing in the
// sky and the only one that is itself a light source.
inline constexpr SkyFlyer SKY_CHOPPER = {430.f, 72.f, 0xC40FFEu, 0x3E1A5u, 4u, 1u};
// A fantasy airship: a striped envelope with a little wooden ship's hull
// slung underneath. The SLOWEST thing up here bar the balloons, and the
// longest crossing of anything — it is scenery rather than an event, and it
// should take its time getting across.
inline constexpr SkyFlyer SKY_AIRSHIP = {340.f, 96.f, 0xA125B7u, 0x6D30Cu, 3u, 0u};

// Is this flyer crossing right now? `age` is how far into the crossing it
// is, and `h` the epoch hash the caller uses to pick lanes/colours.
bool skyFlyerUp(const SkyFlyer& f, float seconds, float* age, uint32_t* h);

// 0..1: how much is crossing the sky at this moment. The rarer the flyer,
// the more it is worth musically.
float skyTraffic01(float seconds);

// How visible open-ocean swell is as colour, live via ~/.terrarium-swell
// (0..1, default 0.30). 0 = deep water is texture and glitter only; 1 = full
// rolling bands. The breaking surf where water shoals is NOT affected — that
// always happens. Exists as a knob because the right amount is a matter of
// taste and has to be judged on the panel, not in a screenshot.
float displaySwell();

// Brightness above 1.0, from the same ~/.terrarium-brightness file: a screen
// curve applied at the end of shading (1.0 = off, up to 3.0). displayBrightness
// still only attenuates, so every existing call site is unchanged.
float displayLift();

// How hard the final palette-harmony grade pulls, live via
// ~/.terrarium-harmony (0..1, default 1.0). 0 = the raw authored colours,
// 1 = the full chroma ceiling + split tone. A knob for the same reason
// displaySwell is one: how much is a matter of taste, and taste has to be
// judged on the panel rather than in a screenshot.
float displayHarmony();

// Display contrast 0.5..1.8 around mid-grey, live via ~/.terrarium-contrast
// (same polling pattern as brightness). Missing file = 1.0.
float displayContrast();

// Round-panel geometry, live via ~/.terrarium-panel ("<diameter> <x> <y>",
// diameter in LEDs, offset in output pixels). diameter <= 0 means "use the
// compiled world size". Lets the kiosk be aligned against the real disc
// without a rebuild — see --panel/--calibrate in the pico build.
struct PanelGeom {
  int diameter = 0;
  int offX = 0;
  int offY = 0;
};
PanelGeom displayPanel();

// Live weather: mirror the real local sky. A fetcher writes
// ~/.terrarium-weather (Open-Meteo, every 10 min); the sim polls it and
// overrides the weather state machine when g_weatherMode == 1.
extern int g_weatherMode;  // 0 = simulated weather, 1 = live local weather
struct LiveWeather {
  bool valid = false;
  bool snowing = false;
  int code = 0, cloud = 0, winddir = 0;
  float windspeed = 0.f, temp = 0.f;
  long ts = 0;
};
const LiveWeather& liveWeatherNow();
const char* weatherName(WeatherState state);
const char* seasonName(Season season);
const char* speciesName(uint8_t s);
const char* biomeName(Biome b);
BiomeWeights weightsFor(Biome b);
BiomeWeights lerpBiomeWeights(const BiomeWeights& a, const BiomeWeights& b, float t);
void initClouds(Clouds& c, Rng& r, Biome b);
float clamp01(float v);
char waterFlowGlyph(const World& w, int x, int y, int tick);
bool isWaterVisualGlyph(unsigned char c);
bool isTree(char c);
bool isVeg(char c);
int speciesVariant2(const World& w, int x, int y, int n);
void seedWorld(World& w, Rng& r, Biome biome);
void step(World& w, Rng& r, std::string& banner, int tick);
