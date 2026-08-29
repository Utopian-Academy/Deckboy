#include "terrarium_core.hpp"

#include <ctime>

static inline float smooth1(float cur,float tgt,float s){
  float a=std::clamp(s,0.0f,0.98f);
  return cur*a + tgt*(1.0f-a);
}

void applyModMatrix(){
  g_cc11Expr=1.0f; g_cc74Bright=0.5f; g_pan01=0.5f;
  for(int v=0; v<NUM_VOICES; ++v) g_porta01[v]=0.0f;
  for(int i=0;i<MOD_SLOTS;++i) g_modCC01[i]=-1.0f;

  for(int i=0;i<MOD_SLOTS;++i){
    auto& mm=g_modMap[i];
    if(!mm.enabled || mm.dest==DEST_NONE) continue;
    int src=std::clamp(mm.src,0,MOD_N-1);
    float x=g_modVal[src]; // [-1..1]
    float target=x*mm.amt;
    mm.state=smooth1(mm.state,target,mm.smooth);
    float v=mm.state;

    switch(mm.dest){
      case DEST_CC11_EXPR: g_cc11Expr=std::clamp(1.0f+0.7f*v,0.0f,1.0f); break;
      case DEST_CC74_BRIGHT: g_cc74Bright=std::clamp(0.5f+0.5f*v,0.0f,1.0f); break;
      case DEST_PAN: g_pan01=std::clamp(0.5f+0.5f*v,0.0f,1.0f); break;
      case DEST_PORTA_V0: g_porta01[0]=std::clamp(0.5f+0.5f*v,0.0f,1.0f); break;
      case DEST_PORTA_V1: g_porta01[1]=std::clamp(0.5f+0.5f*v,0.0f,1.0f); break;
      case DEST_PORTA_V2: g_porta01[2]=std::clamp(0.5f+0.5f*v,0.0f,1.0f); break;
      case DEST_MIDI_CC:  g_modCC01[i]=std::clamp(0.5f+0.5f*v,0.0f,1.0f); break;
      default: break;
    }
  }
}
static void applyRippleChaos(World& w, Rng& r, int tick);



static int countNeighborsWater(const Water& w, int x, int y) {
  int n = 0;
  for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
    if (dx==0 && dy==0) continue;
    int nx=x+dx, ny=y+dy;
    if (inBounds(nx,ny) && w[ny][nx]>0) n++;
  }
  return n;
}

int g_seasonMode = 0;  // 0 = sim ticks, 1 = one season per real day, 2 = calendar

static void localNow(std::tm& lt) {
  std::time_t now = std::time(nullptr);
#ifdef _WIN32
  localtime_s(&lt, &now);
#else
  localtime_r(&now, &lt);
#endif
}

Season seasonAt(int tick) {
  if (g_seasonMode == 1) {
    // One season per real day: a four-day year, aligned to local midnight.
    return (Season)((std::time(nullptr) / 86400) % 4);
  }
  if (g_seasonMode == 2) {
    std::tm lt{}; localNow(lt);
    int m = lt.tm_mon;  // 0=Jan; northern-hemisphere calendar
    if (m == 11 || m <= 1) return WINTER;
    if (m <= 4) return SPRING;
    if (m <= 7) return SUMMER;
    return AUTUMN;
  }
  return (Season)((tick / SEASON_TICKS) % 4);
}

float seasonLerp(int tick) {
  if (g_seasonMode == 1) {
    std::tm lt{}; localNow(lt);
    return ((float)lt.tm_hour * 3600.f + (float)lt.tm_min * 60.f +
            (float)lt.tm_sec) / 86400.f;
  }
  if (g_seasonMode == 2) {
    std::tm lt{}; localNow(lt);
    int m = lt.tm_mon;
    int intoSeason = (m == 11) ? 0 : ((m + 1) % 3);  // months into the season
    return ((float)intoSeason + (float)(lt.tm_mday - 1) / 31.f) / 3.f;
  }
  return float(tick % SEASON_TICKS) / float(SEASON_TICKS);
}
bool nightish(int tick) { return daylightNow(tick).level < 0.35f; }

int g_daynightMode = 1;

Daylight daylightNow(int tick) {
  float hour;
  if (g_daynightMode == 2) {
    std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    hour = (float)lt.tm_hour + (float)lt.tm_min / 60.0f;
  } else if (g_daynightMode == 1) {
    // One full day = 8*DAY_TICKS (24 minutes at 5 tps). Start mid-morning
    // so a fresh vat opens in daylight.
    const int cycle = DAY_TICKS * 8;
    hour = 9.0f + 24.0f * (float)(tick % cycle) / (float)cycle;
    if (hour >= 24.0f) hour -= 24.0f;
  } else {
    return {1.0f, 0.0f};
  }

  auto smooth = [](float x) {
    x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
    return x * x * (3.f - 2.f * x);
  };

  Daylight d;
  if (hour < 5.f)        d.level = 0.f;
  else if (hour < 8.f)   d.level = smooth((hour - 5.f) / 3.f);   // dawn
  else if (hour < 18.f)  d.level = 1.f;
  else if (hour < 22.f)  d.level = 1.f - smooth((hour - 18.f) / 4.f);  // dusk
  else                   d.level = 0.f;

  if (hour >= 5.f && hour < 8.5f)
    d.warm = 0.8f * (1.f - std::fabs((hour - 6.75f) / 1.75f));   // sunrise gold
  else if (hour >= 17.5f && hour < 22.f)
    d.warm = 1.0f * (1.f - std::fabs((hour - 19.75f) / 2.25f));  // sunset amber
  else if (d.level <= 0.f)
    d.warm = -0.6f;                                              // moonlight
  return d;
}

int displayBgMode() {
  static int cached = 0;
  static std::chrono::steady_clock::time_point lastRead{};
  auto now = std::chrono::steady_clock::now();
  if (now - lastRead < std::chrono::seconds(1)) return cached;
  lastRead = now;
  const char* home = std::getenv("HOME");
  if (!home) return cached;
  std::string path = std::string(home) + "/.terrarium-bg";
  cached = 0;
  if (FILE* f = std::fopen(path.c_str(), "r")) {
    int c = std::fgetc(f);
    if (c == 'o' || c == 'O') cached = 1;
    std::fclose(f);
  }
  return cached;
}

int g_weatherMode = 0;

const LiveWeather& liveWeatherNow() {
  static LiveWeather cached;
  static std::chrono::steady_clock::time_point lastRead{};
  auto now = std::chrono::steady_clock::now();
  if (now - lastRead < std::chrono::seconds(60)) return cached;
  lastRead = now;
  const char* home = std::getenv("HOME");
  if (!home) return cached;
  std::string path = std::string(home) + "/.terrarium-weather";
  FILE* f = std::fopen(path.c_str(), "r");
  if (!f) { cached.valid = false; return cached; }
  LiveWeather lw;
  char key[32]; float val;
  while (std::fscanf(f, "%31[^=]=%f\n", key, &val) == 2) {
    if (!std::strcmp(key, "code")) lw.code = (int)val;
    else if (!std::strcmp(key, "cloud")) lw.cloud = (int)val;
    else if (!std::strcmp(key, "windspeed")) lw.windspeed = val;
    else if (!std::strcmp(key, "winddir")) lw.winddir = (int)val;
    else if (!std::strcmp(key, "temp")) lw.temp = val;
    else if (!std::strcmp(key, "ts")) lw.ts = (long)val;
  }
  std::fclose(f);
  // Stale data (fetcher dead / offline > 2h) falls back to simulated.
  lw.valid = (std::time(nullptr) - lw.ts) < 7200;
  lw.snowing = (lw.code >= 71 && lw.code <= 77) || lw.code == 85 || lw.code == 86;
  cached = lw;
  return cached;
}

float displayContrast() {
  static float cached = 1.0f;
  static std::chrono::steady_clock::time_point lastRead{};
  auto now = std::chrono::steady_clock::now();
  if (now - lastRead < std::chrono::seconds(1)) return cached;
  lastRead = now;
  const char* home = std::getenv("HOME");
  if (!home) return cached;
  std::string path = std::string(home) + "/.terrarium-contrast";
  if (FILE* f = std::fopen(path.c_str(), "r")) {
    float v = 1.0f;
    if (std::fscanf(f, "%f", &v) == 1) cached = std::clamp(v, 0.5f, 1.8f);
    std::fclose(f);
  } else {
    cached = 1.0f;
  }
  return cached;
}

float displaySwell() {
  static float cached = 0.30f;
  static std::chrono::steady_clock::time_point lastRead{};
  auto now = std::chrono::steady_clock::now();
  if (now - lastRead < std::chrono::seconds(1)) return cached;
  lastRead = now;
  const char* home = std::getenv("HOME");
  if (!home) return cached;
  std::string path = std::string(home) + "/.terrarium-swell";
  cached = 0.30f;
  if (FILE* f = std::fopen(path.c_str(), "r")) {
    float v = 0.30f;
    if (std::fscanf(f, "%f", &v) == 1) cached = std::clamp(v, 0.f, 1.f);
    std::fclose(f);
  }
  return cached;
}

bool skyFlyerUp(const SkyFlyer& f, float seconds, float* age, uint32_t* h) {
  uint32_t ep = (uint32_t)(seconds / f.period);
  uint32_t hh = hash3(ep, f.s1, f.s2);
  float a = seconds - (float)ep * f.period;
  if (age) *age = a;
  if (h) *h = hh;
  return ((hh % f.modv) == f.modr) && a < f.dwell;
}

float skyTraffic01(float seconds) {
  // Weighted by rarity: a balloon is scenery, a dragon is an event. Each
  // contribution is shaped by a sine over its crossing so a source ramps in
  // and out with the thing itself rather than stepping on and off.
  struct Entry { const SkyFlyer* f; float weight; };
  static const Entry kAll[] = {
      {&SKY_DRAGON, 1.00f}, {&SKY_UNICORN, 0.85f}, {&SKY_UFO, 0.80f},
      {&SKY_RIDER, 0.55f},  {&SKY_WITCH, 0.65f},   {&SKY_BANNER, 0.45f},
      {&SKY_CHOPPER, 0.95f}, {&SKY_AIRSHIP, 0.70f},
  };
  float sum = 0.f;
  for (const Entry& e : kAll) {
    float age = 0.f;
    if (!skyFlyerUp(*e.f, seconds, &age, nullptr)) continue;
    float p = std::clamp(age / e.f->dwell, 0.f, 1.f);
    sum += e.weight * std::sin(3.14159f * p);
  }
  return clamp01(sum);
}

float displayHarmony() {
  static float cached = 1.00f;
  static std::chrono::steady_clock::time_point lastRead{};
  auto now = std::chrono::steady_clock::now();
  if (now - lastRead < std::chrono::seconds(1)) return cached;
  lastRead = now;
  const char* home = std::getenv("HOME");
  if (!home) return cached;
  std::string path = std::string(home) + "/.terrarium-harmony";
  cached = 1.00f;
  if (FILE* f = std::fopen(path.c_str(), "r")) {
    float v = 1.00f;
    if (std::fscanf(f, "%f", &v) == 1) cached = std::clamp(v, 0.f, 1.f);
    std::fclose(f);
  }
  return cached;
}

float terraSeconds() {
  static auto t0 = std::chrono::steady_clock::now();
  return std::chrono::duration<float>(std::chrono::steady_clock::now() - t0)
      .count();
}

float alienApparition01(const World& w, float seconds) {
  uint32_t ep = (uint32_t)(seconds / ALIEN_APPARITION_EPOCH);
  uint32_t hh = hash3(ep, w.worldSeed, 0x8EAD5u);
  if ((hh % ALIEN_APPARITION_ODDS) != 0u) return 0.f;
  float age = seconds - (float)ep * ALIEN_APPARITION_EPOCH;
  if (age >= ALIEN_APPARITION_DWELL) return 0.f;
  float rise;
  if (age < 5.f)                                  rise = age / 5.f;
  else if (age < ALIEN_APPARITION_DWELL - 6.f)    rise = 1.f;
  else rise = std::max(0.f, (ALIEN_APPARITION_DWELL - age) / 6.f);
  return rise * rise * (3.f - 2.f * rise);
}

// Brightness above 1.0 is a LIFT, not a multiply. The old control only ever
// attenuated: 1.0 meant "don't dim", so max brightness was simply the palette
// as authored — and since the day/night grade caps output at 0.38 of the
// palette at night, max never felt like max on the panel after dark. Values
// above 1.0 now apply a screen curve (1-(1-v)^k), which lifts the darks and
// midtones hard while leaving white at white, so nothing clips.
float displayLift() {
  static float cached = 1.0f;
  static std::chrono::steady_clock::time_point lastRead{};
  auto now = std::chrono::steady_clock::now();
  if (now - lastRead < std::chrono::seconds(1)) return cached;
  lastRead = now;
  const char* home = std::getenv("HOME");
  if (!home) return cached;
  std::string path = std::string(home) + "/.terrarium-brightness";
  cached = 1.0f;
  if (FILE* f = std::fopen(path.c_str(), "r")) {
    float v = 1.0f;
    if (std::fscanf(f, "%f", &v) == 1 && v > 1.0f) cached = std::min(v, 3.0f);
    std::fclose(f);
  }
  return cached;
}

// Panel geometry, live from ~/.terrarium-panel ("<diameter> <x> <y>").
// The round LED disc's true diameter was never on a spec sheet, and the
// output lands at whatever offset the panel's controller crops from — so
// both are dialled in against the live panel instead of compiled in.
// Missing file = defaults (caller's compiled world size, origin 0,0).
PanelGeom displayPanel() {
  static PanelGeom cached;
  static std::chrono::steady_clock::time_point lastRead{};
  auto now = std::chrono::steady_clock::now();
  if (now - lastRead < std::chrono::seconds(1)) return cached;
  lastRead = now;
  const char* home = std::getenv("HOME");
  if (!home) return cached;
  std::string path = std::string(home) + "/.terrarium-panel";
  PanelGeom pg;
  if (FILE* f = std::fopen(path.c_str(), "r")) {
    int d = 0, x = 0, y = 0;
    int n = std::fscanf(f, "%d %d %d", &d, &x, &y);
    if (n >= 1 && d > 0) pg.diameter = std::clamp(d, 8, 4096);
    if (n >= 2) pg.offX = std::clamp(x, -4096, 4096);
    if (n >= 3) pg.offY = std::clamp(y, -4096, 4096);
    std::fclose(f);
  }
  cached = pg;
  return cached;
}

float displayBrightness() {
  static float cached = 1.0f;
  static std::chrono::steady_clock::time_point lastRead{};
  auto now = std::chrono::steady_clock::now();
  if (now - lastRead < std::chrono::seconds(1)) return cached;
  lastRead = now;
  const char* home = std::getenv("HOME");
  if (!home) return cached;
  std::string path = std::string(home) + "/.terrarium-brightness";
  if (FILE* f = std::fopen(path.c_str(), "r")) {
    float v = 1.0f;
    if (std::fscanf(f, "%f", &v) == 1)
      cached = std::clamp(v, 0.05f, 1.0f);
    std::fclose(f);
  } else {
    cached = 1.0f;
  }
  return cached;
}

const char* weatherName(WeatherState state) {
  switch (state) {
    case CLEAR: return "clear";
    case OVERCAST: return "overcast";
    case RAIN: return "rain";
    case STORM: return "storm";
  }
  return "clear";
}

const char* seasonName(Season season) {
  switch (season) {
    case SPRING: return "spring";
    case SUMMER: return "summer";
    case AUTUMN: return "autumn";
    case WINTER: return "winter";
  }
  return "spring";
}


const char* speciesName(uint8_t s){
  switch(s){
    case SPEC_WANDERER:  return "WANDERER";
    case SPEC_SHELLBACK: return "SHELLBACK";
    case SPEC_SWARMER:   return "SWARMER";
    case SPEC_ENGINEER:  return "ENGINEER";
    case SPEC_PARASITE:  return "PARASITE";
    case SPEC_PACKHUNTER:return "PACKHUNTER";
    case SPEC_MYSTIC:    return "MYSTIC";
    case SPEC_TRICKSTER: return "TRICKSTER";
  }
  return "???";
}

static inline uint8_t pickBiomeSpecies(Biome b, Rng& r){
  // Abstract distributions per biome for variety (no new glyphs required).
  // We bias different "personalities" rather than different sprites.
  float u = r.u01();
  switch(b){
    case WETLAND:
      if(u<0.18f) return SPEC_SHELLBACK;
      if(u<0.32f) return SPEC_SWARMER;
      if(u<0.44f) return SPEC_PARASITE;
      if(u<0.56f) return SPEC_ENGINEER;
      if(u<0.70f) return SPEC_MYSTIC;
      if(u<0.82f) return SPEC_TRICKSTER;
      return SPEC_WANDERER;
    case DESERT:
      if(u<0.22f) return SPEC_ENGINEER;
      if(u<0.40f) return SPEC_TRICKSTER;
      if(u<0.54f) return SPEC_PACKHUNTER;
      if(u<0.68f) return SPEC_MYSTIC;
      if(u<0.80f) return SPEC_PARASITE;
      return SPEC_WANDERER;
    case TROPICAL:
      if(u<0.22f) return SPEC_SWARMER;
      if(u<0.40f) return SPEC_PARASITE;
      if(u<0.54f) return SPEC_ENGINEER;
      if(u<0.68f) return SPEC_TRICKSTER;
      if(u<0.80f) return SPEC_MYSTIC;
      return SPEC_WANDERER;
    case ALPINE:
      if(u<0.20f) return SPEC_PACKHUNTER;
      if(u<0.38f) return SPEC_MYSTIC;
      if(u<0.54f) return SPEC_ENGINEER;
      if(u<0.68f) return SPEC_TRICKSTER;
      if(u<0.80f) return SPEC_SWARMER;
      return SPEC_WANDERER;
    case ALIEN:
      if(u<0.25f) return SPEC_MYSTIC;
      if(u<0.45f) return SPEC_TRICKSTER;
      if(u<0.60f) return SPEC_PARASITE;
      if(u<0.74f) return SPEC_ENGINEER;
      if(u<0.86f) return SPEC_SWARMER;
      return SPEC_WANDERER;
    case OCEAN:
      if(u<0.32f) return SPEC_SWARMER;
      if(u<0.50f) return SPEC_MYSTIC;
      if(u<0.64f) return SPEC_PACKHUNTER;
      if(u<0.78f) return SPEC_PARASITE;
      if(u<0.88f) return SPEC_SHELLBACK;
      return SPEC_WANDERER;
    case CITY:  // pigeons, rats, cats, commuters — swarmers and opportunists
      if(u<0.30f) return SPEC_SWARMER;
      if(u<0.50f) return SPEC_TRICKSTER;
      if(u<0.66f) return SPEC_PARASITE;
      if(u<0.80f) return SPEC_ENGINEER;
      if(u<0.90f) return SPEC_PACKHUNTER;
      return SPEC_WANDERER;
    case MEADOW:
    default:
      if(u<0.18f) return SPEC_SWARMER;
      if(u<0.32f) return SPEC_ENGINEER;
      if(u<0.44f) return SPEC_TRICKSTER;
      if(u<0.56f) return SPEC_MYSTIC;
      if(u<0.68f) return SPEC_PACKHUNTER;
      if(u<0.78f) return SPEC_PARASITE;
      return SPEC_WANDERER;
  }
}

// Display glyphs: animated, biome-tinted per abstract species (does not affect behavior glyphs).
static inline char speciesDisplayGlyph(uint8_t spec, Biome /*b*/, int tick, bool legendA, bool legendB){
  int ph = (tick/5)&1; // 2-frame animation
  if(legendA) return (char)(ph? 'Y' : 'y');  // Legendary couple (A)
  if(legendB) return (char)(ph? 'Z' : 'z');  // Legendary couple (B)
  // Use dedicated "sprite glyphs" in the 0x80+ range so agents are always visually distinct from terrain.
  // (Rendered via custom 8x8 bitmaps in glyph8_world().)
  uint8_t s = (uint8_t)(spec % SPEC_COUNT);
  return (char)(0x80u + (uint8_t)(s*2u + (uint8_t)ph));
}

const char* biomeName(Biome b) {
  switch (b) {
    case MEADOW: return "meadow";
    case WETLAND:return "wetland";
    case ALPINE: return "alpine";
    case ALIEN:  return "alien";
    case TROPICAL: return "tropical";
    case DESERT: return "desert";
    case CITY:   return "city";
    case OCEAN:  return "ocean";
    case SKY:    return "sky";
  }
  return "meadow";
}

BiomeWeights lerpBiomeWeights(const BiomeWeights& a, const BiomeWeights& b, float t){
  BiomeWeights o;
  o.pondDensity     = a.pondDensity     + (b.pondDensity     - a.pondDensity)     * t;
  o.stoneChance     = a.stoneChance     + (b.stoneChance     - a.stoneChance)     * t;
  o.reedChance      = a.reedChance      + (b.reedChance      - a.reedChance)      * t;
  o.fernChance      = a.fernChance      + (b.fernChance      - a.fernChance)      * t;
  o.flowerChance    = a.flowerChance    + (b.flowerChance    - a.flowerChance)    * t;
  o.bigFlowerChance = a.bigFlowerChance + (b.bigFlowerChance - a.bigFlowerChance) * t;
  o.treeChance      = a.treeChance      + (b.treeChance      - a.treeChance)      * t;
  o.mushChance      = a.mushChance      + (b.mushChance      - a.mushChance)      * t;
  o.growRate        = a.growRate        + (b.growRate        - a.growRate)        * t;
  o.bloomRate       = a.bloomRate       + (b.bloomRate       - a.bloomRate)       * t;
  o.fireRate        = a.fireRate        + (b.fireRate        - a.fireRate)        * t;
  o.alienRate       = a.alienRate       + (b.alienRate       - a.alienRate)       * t;
  return o;
}

BiomeWeights weightsFor(Biome b) {
  switch (b) {
    // pondDensity, stoneChance, reedChance, fernChance, flowerChance, bigFlowerChance, treeChance, mushChance,
    // growRate, bloomRate, fireRate, alienRate
    case MEADOW:  // drier + fewer "mud faces" + fewer flowers (more distinct from wetland)
      return {0.35f, 1.0f, 0.55f, 0.85f, 0.55f, 0.55f, 1.0f, 0.90f, 1.0f, 0.85f, 0.90f, 0.70f};
    case WETLAND: // wetter + reed-heavy
      return {1.70f, 0.7f, 1.55f, 1.05f, 0.95f, 0.90f, 0.85f, 1.15f, 1.0f, 1.15f, 0.80f, 0.70f};
    case ALPINE:  // much rockier, fewer ponds, fewer flowers
      return {0.18f, 1.85f, 0.35f, 0.45f, 0.35f, 0.35f, 0.60f, 0.55f, 0.70f, 0.55f, 1.05f, 0.80f};
    case ALIEN:
      return {1.05f, 1.0f, 0.90f, 0.90f, 1.25f, 1.35f, 1.05f, 1.15f, 1.05f, 1.45f, 0.85f, 1.60f};
    case TROPICAL:
      return {1.30f, 0.6f, 1.35f, 1.25f, 1.35f, 1.15f, 1.20f, 1.05f, 1.15f, 1.45f, 0.75f, 1.00f};
    case DESERT:
      return {0.05f, 1.10f, 0.05f, 0.10f, 0.10f, 0.10f, 0.20f, 0.10f, 0.35f, 0.15f, 1.45f, 0.40f};
    case CITY:  // what grows here grows in parks and cracks — plus a harbour
      return {0.30f, 0.30f, 0.25f, 0.35f, 0.45f, 0.30f, 0.55f, 0.25f, 0.60f, 0.70f, 0.30f, 0.35f};
    case OCEAN:  // only the atolls have anything growing on them
      return {0.10f, 0.90f, 0.30f, 0.30f, 0.55f, 0.40f, 0.45f, 0.20f, 0.70f, 0.90f, 0.10f, 0.45f};
    case SKY:  // nothing grows in mid-air; the renderer paints all of it
      return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  }
  return weightsFor(MEADOW);
}

// --- Mod pool update (moved here so World/Species are defined) ---
void updateModPool(World& w, int tick, int viewW, int viewH){
  // Sample the current camera viewport to keep modulation "what you see is what you hear".
  int x0=g_camX, y0=g_camY;
  int x1=std::min(W, g_camX+viewW);
  int y1=std::min(H, g_camY+viewH);
  int waterC=0, plantC=0, overC=0;
  int agentsV=0, panicC=0;
  float stressSum=0, hungerSum=0, thirstSum=0, fatSum=0, healthSum=0;
  float speedSum=0;
  int stressHi=0;

  // Species-derived silly counters
  int shellHi=0, shellN=0;
  int swarmN=0;
  int parasiteN=0;
  int engineerN=0;
  int mysticN=0;
  int trickN=0;
  int packN=0;

  // Quick tile sampling (stride for speed)
  int sx = std::max(1, (x1-x0)/64);
  int sy = std::max(1, (y1-y0)/36);
  int samp=0;
  for(int y=y0; y<y1; y+=sy){
    for(int x=x0; x<x1; x+=sx){
      ++samp;
      if (w.water[y][x]>0) ++waterC;
      if (isEdiblePlant(w.terrain[y][x])) ++plantC;
      if (w.overlay[y][x] != ' ') ++overC;
    }
  }
  float waterFrac = (samp>0)? (float)waterC/samp : 0.f;
  float plantFrac = (samp>0)? (float)plantC/samp : 0.f;
  float overFrac  = (samp>0)? (float)overC/samp  : 0.f;

  // Agent stats within view
  for (auto &a: w.agents){
    if (a.x<x0 || a.x>=x1 || a.y<y0 || a.y>=y1) continue;
    agentsV++;
    stressSum += a.stress;
    hungerSum += a.hunger;
    thirstSum += a.thirst;
    fatSum    += a.fatigue;
    healthSum += a.health;
    if (a.stress > 0.75f) stressHi++;
    if (a.flags & 1) panicC++;

    // speed proxy: flee/wander/drink intents influence audible motion
    float sp = 0.0f;
    if (a.intent==INTENT_FLEE) sp = 1.0f;
    else if (a.intent==INTENT_DRINK || a.intent==INTENT_FORAGE || a.intent==INTENT_HUNT) sp = 0.7f;
    else sp = 0.35f;
    sp *= (1.0f - 0.65f*a.fatigue);
    speedSum += sp;

    switch(a.species){
      case SPEC_SHELLBACK: shellN++; if (a.stress>0.70f) shellHi++; break;
      case SPEC_SWARMER: swarmN++; break;
      case SPEC_PARASITE: parasiteN++; break;
      case SPEC_ENGINEER: engineerN++; break;
      case SPEC_MYSTIC: mysticN++; break;
      case SPEC_TRICKSTER: trickN++; break;
      case SPEC_PACKHUNTER: packN++; break;
      default: break;
    }
  }
  float invA = (agentsV>0)? (1.0f/agentsV) : 0.f;
  float stressMean = stressSum*invA;
  float hungerMean = hungerSum*invA;
  float thirstMean = thirstSum*invA;
  float fatMean    = fatSum*invA;
  float healthMean = healthSum*invA;
  float agentSpeed = speedSum*invA;

  // Predator pressure in view
  int preds=0, prey=0;
  for (auto &a: w.agents){
    if (a.x<x0 || a.x>=x1 || a.y<y0 || a.y>=y1) continue;
    if (isPredator(a.glyph)) preds++; else prey++;
  }
  float predPressure = (prey>0)? (float)preds/(float)prey : (preds? 4.f:0.f);

  // Ripple energy near view
  float rippleE=0.f;
  for (auto &rp: g_ripples){
    // approximate: more recent + closer to view center
    float cx=float(rp.cx), cy=float(rp.cy);
    float vx=float((x0+x1)*0.5f), vy=float((y0+y1)*0.5f);
    float dx=cx-vx, dy=cy-vy;
    float dist = std::sqrt(dx*dx+dy*dy);
    rippleE += (rp.amp) * std::exp(-dist/40.0f) * (1.0f - std::min(1.0f, rp.t/3.0f));
  }
  rippleE = std::min(3.0f, rippleE);

  // Flux: compare against previous values (store in tail of g_modVal)
  static float prevWater=0, prevPlant=0, prevStress=0, prevHunger=0, prevThirst=0, prevFat=0, prevHealth=0, prevPanic=0;
  float waterFlux = std::fabs(waterFrac - prevWater);
  float plantFlux = std::fabs(plantFrac - prevPlant);
  float stressFlux= std::fabs(stressMean - prevStress);
  float hungerFlux= std::fabs(hungerMean - prevHunger);
  float thirstFlux= std::fabs(thirstMean - prevThirst);
  float fatFlux   = std::fabs(fatMean - prevFat);
  float healthFlux= std::fabs(healthMean - prevHealth);
  float panicFlux = std::fabs((float)panicC*invA - prevPanic);

  prevWater=waterFrac; prevPlant=plantFrac; prevStress=stressMean; prevHunger=hungerMean; prevThirst=thirstMean; prevFat=fatMean; prevHealth=healthMean; prevPanic=(float)panicC*invA;

  // Fill mod array (0..1-ish)
  auto clamp01f=[](float v){ return v<0.f?0.f:(v>1.f?1.f:v); };
  g_modVal[0]=clamp01f(waterFrac);
  g_modVal[1]=clamp01f(plantFrac);
  g_modVal[2]=clamp01f(overFrac);
  g_modVal[3]=clamp01f((float)agentsV/60.f);
  g_modVal[4]=clamp01f(agentSpeed);

  g_modVal[5]=clamp01f(stressMean);
  g_modVal[6]=clamp01f((float)stressHi/ std::max(1.f,(float)agentsV));
  g_modVal[7]=clamp01f((float)panicC/ std::max(1.f,(float)agentsV));
  g_modVal[8]=clamp01f(hungerMean);
  g_modVal[9]=clamp01f(thirstMean);

  g_modVal[10]=clamp01f(fatMean);
  g_modVal[11]=clamp01f(healthMean);
  g_modVal[12]=clamp01f(std::min(2.0f,predPressure)/2.0f);

  // Birth/death pulses: use existing counters if any, else fake from flux
  g_modVal[13]=clamp01f(plantFlux*3.0f);
  g_modVal[14]=clamp01f(stressFlux*3.0f);

  g_modVal[15]=clamp01f(rippleE/3.0f);
  float windMag = std::sqrt((float)w.wind.dx*w.wind.dx + (float)w.wind.dy*w.wind.dy) * (float)w.wind.strength/8.0f;
  g_modVal[16]=clamp01f(windMag);
  g_modVal[17]=clamp01f(seasonLerp(tick));
  g_modVal[18]=clamp01f(w.cloudOpacity);
  g_modVal[19]=clamp01f((float)w.weather.state/4.0f);

  g_modVal[20]=clamp01f((shellN>0)? (float)shellHi/shellN : 0.f);
  // swarm cohesion: more swarmers + less speed => higher cohesion (cute but useful)
  g_modVal[21]=clamp01f((float)swarmN/50.f * (1.0f - agentSpeed));
  g_modVal[22]=clamp01f((float)parasiteN/40.f * (0.3f + stressMean));
  g_modVal[23]=clamp01f((float)engineerN/40.f * (0.2f + waterFrac));
  g_modVal[24]=clamp01f((float)mysticN/40.f * (0.5f + overFrac));

  g_modVal[25]=clamp01f((float)trickN/40.f * (0.5f + rippleE/3.0f));
  g_modVal[26]=clamp01f((float)packN/40.f * (0.4f + predPressure*0.25f));
  g_modVal[27]=clamp01f(plantFlux*4.0f);
  g_modVal[28]=clamp01f(waterFlux*4.0f);
  g_modVal[29]=clamp01f(stressFlux*4.0f);

  g_modVal[30]=clamp01f(hungerFlux*4.0f);
  g_modVal[31]=clamp01f(thirstFlux*4.0f);
  g_modVal[32]=clamp01f(fatFlux*4.0f);
  g_modVal[33]=clamp01f(healthFlux*4.0f);
  g_modVal[34]=clamp01f(panicFlux*4.0f);

  // Oddities: silly mixtures, designed to wiggle
  float o0 = (g_modVal[20]*g_modVal[15]);                  // shellback stress * ripples
  float o1 = (g_modVal[12]* (1.0f-g_modVal[1]));          // pred pressure * low plants
  float o2 = (g_modVal[8]*g_modVal[9]);                   // hunger * thirst
  float o3 = std::fabs(g_modVal[16]-g_modVal[19]);         // wind vs rain mismatch
  float o4 = g_modVal[21] * (0.3f + g_modVal[2]);          // swarm cohesion * overlay
  float o5 = g_modVal[5] * (0.5f + g_modVal[28]);          // stress * water flux
  float o6 = g_modVal[11] * (1.0f - g_modVal[10]);         // health vs fatigue
  float o7 = g_modVal[24] * (0.2f + g_modVal[17]);         // mystic flux * season
  float o8 = g_modVal[25] * (0.2f + g_modVal[4]);          // trickster mischief * speed
  float o9 = (g_modVal[0]+g_modVal[1])*0.5f;               // wet+green
  float o10= std::fabs(g_modVal[0]-g_modVal[1]);           // wet vs green contrast
  float o11= g_modVal[7] * (0.3f + g_modVal[12]);          // panic * pred pressure
  float o12= g_modVal[18] * (0.2f + g_modVal[2]);          // clouds * overlay
  float o13= g_modVal[13] * (0.2f + g_modVal[29]);         // birthpulse * stress flux
  float o14= (float)((tick/37)%11)/10.0f;                  // weird slow sawtooth

  float odd[15]={o0,o1,o2,o3,o4,o5,o6,o7,o8,o9,o10,o11,o12,o13,o14};

// --- Bipolar + spiky modulation ---
static float prev[MOD_N] = {0};
for(int i=0;i<15;++i) g_modVal[35+i]=clamp01f(odd[i]);

// World-clock and live-sky sources: the vat's day, its volcano, and the
// actual weather outside the window, all playable through the matrix.
{
  Daylight dl = daylightNow(tick);
  g_modVal[50] = clamp01f(dl.level);
  g_modVal[51] = clamp01f(std::max(0.f, dl.warm));
  g_modVal[52] = clamp01f(w.weather.rainStrength);
  g_modVal[53] = clamp01f(0.6f * g_modVal[16] + 0.4f * w.weather.rainStrength);
  g_modVal[54] = (w.ventX >= 0 && w.eruptEnd > tick)
                     ? clamp01f((float)(w.eruptEnd - tick) / 120.f)
                     : 0.f;
  const LiveWeather& lw = liveWeatherNow();
  g_modVal[55] = lw.valid ? clamp01f((lw.temp + 10.f) / 45.f) : 0.5f;
  g_modVal[56] = lw.valid ? clamp01f(lw.windspeed / 40.f) : g_modVal[16];
  g_modVal[57] = (lw.valid && lw.snowing) ? 1.f : 0.f;
}

// The city, the sea, and the thing that watches. One pass over the world,
// computed from sim state only (never from renderer state — in the plugin the
// UI may not be running at all), so a patch behaves the same whether anyone
// is looking or not.
{
  Daylight dl = daylightNow(tick);
  int built = 0, streets = 0, neon = 0, deep = 0, reef = 0, emissive = 0;
  long storeys = 0;
  for (int y = 0; y < H; ++y) {
    const std::string& row = w.terrain[y];
    for (int x = 0; x < W; ++x) {
      char c = row[x];
      if (isCityBuilding(c)) {
        ++built;
        storeys += std::max(0, ((int)w.height[y][x] - CITY_BASE_H) / CITY_STOREY);
      } else if (c == CITY_ROAD || c == CITY_BRIDGE) {
        ++streets;
      } else if (c == CITY_NEON) {
        ++neon;
      } else if (c == 'C') {
        ++reef;
      }
      if (w.water[y][x] >= 4) ++deep;
      // Alien flora that makes its own light.
      if (w.biome == ALIEN &&
          (c == 'f' || c == '+' || c == '&' || c == '!' || c == 'm')) ++emissive;
    }
  }
  const float area = (float)(W * H);

  // 58 city_built   how much of the world is building
  g_modVal[58] = clamp01f((float)built / area * 3.2f);
  // 59 city_skyline mean storeys of the built stock, 0..~30
  g_modVal[59] = built ? clamp01f((float)storeys / (float)built / 26.f) : 0.f;
  // 60 city_neon    signage, and it only counts once it is lit
  g_modVal[60] = clamp01f((float)neon / area * 26.f) *
                 clamp01f(1.f - dl.level / 0.6f);
  // 61 city_streets road and bridge deck
  g_modVal[61] = clamp01f((float)streets / area * 4.5f);
  // 62 city_rush    the commute: peaks either side of the day, quiet at
  //                 3am and quiet at noon
  {
    float d = dl.level;
    float rush = 1.f - std::fabs(d - 0.5f) * 2.f;      // 1 at the twilights
    g_modVal[62] = clamp01f(rush * (streets ? 1.f : 0.f));
  }
  // 63 harbour_boats the ferry is always working; the freighter comes and goes
  {
    float secs = terraSeconds();
    float boats = 0.f;
    if (w.biome == CITY && streets) {
      boats = 0.45f;
      uint32_t ep = (uint32_t)(secs / 200.f);
      uint32_t hh2 = hash3(ep, w.worldSeed, 0x5417u);
      float age = secs - (float)ep * 200.f;
      if ((hh2 % 3u) == 0u && age < 150.f) boats += 0.55f;
    }
    g_modVal[63] = clamp01f(boats);
  }
  // 64 open_water   how much of the world is deep sea
  g_modVal[64] = clamp01f((float)deep / area);
  // 65 reef         coral
  g_modVal[65] = clamp01f((float)reef / area * 30.f);
  // 66 apparition   0..1 as it leans in and withdraws (see alienApparition01)
  g_modVal[66] = (w.biome == ALIEN)
                     ? clamp01f(alienApparition01(w, terraSeconds()))
                     : 0.f;
  // 67 biolum       alien emissive flora, weighted by how dark it has got
  g_modVal[67] = clamp01f((float)emissive / area * 18.f) *
                 clamp01f(0.25f + 0.75f * (1.f - dl.level));
  // 68 sky_traffic  what is crossing the sky right now (SKY only). Read off
  //                 the shared clock rather than the renderer, so it is just
  //                 as true with no editor open — see skyTraffic01.
  g_modVal[68] = (w.biome == SKY) ? clamp01f(skyTraffic01(terraSeconds())) : 0.f;
  // 69 sky_wonder   how OPEN the sky is: clear air and steady wind. The
  //                 counterpart to traffic — the empty sky is a mood too,
  //                 and something has to modulate during the long quiet.
  g_modVal[69] = (w.biome == SKY)
                     ? clamp01f((1.f - w.weather.rainStrength) *
                                (0.35f + 0.65f * dl.level) *
                                (0.55f + 0.45f * clamp01f(
                                     (float)w.wind.strength / 3.f)))
                     : 0.f;
}

for(int i=0;i<MOD_N;++i){
  float v = g_modVal[i]*2.0f - 1.0f;      // 0..1 -> -1..+1
  float dv = v - prev[i];
  prev[i] = v;
  float sp = v + 0.85f*dv + ((float)((tick + i*131) % 97) / 96.0f - 0.5f) * 0.06f;
  g_modVal[i] = clamp11f(sp);
}



}
// --- end mod pool update ---


// ---------------- Procedural species variety (visual-only) ----------------
static inline uint32_t biomeSalt(Biome b) {
  switch (b) {
    case MEADOW:   return 0xA17C3u;
    case WETLAND:  return 0x55D1Bu;
    case ALPINE:   return 0xC0FFEu;
    case DESERT:   return 0xD3A5Eu;
    case TROPICAL: return 0x7A0F1u;
    case ALIEN:    return 0xA11E1u;
    case CITY:     return 0xC17F0u;
    case OCEAN:    return 0x0CEA1u;
    case SKY:      return 0x5C1E5u;
    default:       return 0xBEEFu;
  }
}

static inline uint32_t speciesSeed2(const World& w, int x, int y) {
  return hash3((uint32_t)x, (uint32_t)y, w.worldSeed ^ biomeSalt(w.biome));
}

int speciesVariant2(const World& w, int x, int y, int n) {
  if (n <= 1) return 0;
  return (int)(speciesSeed2(w,x,y) % (uint32_t)n);
}

// ---------------- Helpers ----------------
char waterFlowGlyph(const World& w, int x, int y, int tick) {
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

bool isWaterVisualGlyph(unsigned char c) {
  // depth digits and custom wave/flow bitmaps live in low control-code range.
  if (c >= (unsigned char)'1' && c <= (unsigned char)'7') return true;
  if (c == (unsigned char)'0') return true; // used for d>=? and calm water
  if (c == (unsigned char)FOAM_GLYPH) return true;
  if (c < 0x20u && c != 0u) return true; // custom flow glyphs 0x01..0x15
  return false;
}

bool isTree(char c) { return c=='T' || c=='Y' || c=='P'; }
bool isVeg(char c) {
  return (c==','||c=='"'||c=='#'||c=='m'||c=='f'||c=='+'||c=='&'||c==';'||c==':'||c=='$'||c=='!'||isTree(c));
}
static inline bool blocksEntity(char terrain, uint8_t waterDepth) {
  if (waterDepth > 0) return true;
  if (terrain == '*' || terrain == 'B' || terrain == 'M' || terrain == '^' || terrain=='X' || terrain=='c') return true;
  return false;
}

void initClouds(Clouds& c, Rng& r, Biome b) {
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
  if (c.offX < 0) {
    c.offX += CW;
  }
  if (c.offX >= CW) {
    c.offX -= CW;
  }
  if (c.offY < 0) {
    c.offY += CH;
  }
  if (c.offY >= CH) {
    c.offY -= CH;
  }

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
  // Quadratic in strength: a shower rolling in starts as a few scattered
  // drops and thickens, instead of arriving as a wall of streaks.
  float rs = w.weather.rainStrength;
  int drops = (int)((float)(W * H) * rs * rs / 12.0f);
  if (drops < 1) return;

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

  // Live mode: real wind speed and compass direction drive the vat's wind
  // (and through it the ocean current, cloud drift, and fire spread).
  if (g_weatherMode == 1) {
    const LiveWeather& lw = liveWeatherNow();
    if (lw.valid) {
      w.wind.strength = std::clamp((int)(lw.windspeed / 8.f), 0, MAX_WIND);
      // Meteorological direction = where wind comes FROM; blow-to = +180.
      float th = ((float)lw.winddir + 180.f) * 3.14159f / 180.f;
      float bx = std::sin(th), by = -std::cos(th);  // N = screen up
      w.wind.dx = (bx > 0.38f) ? 1 : (bx < -0.38f ? -1 : 0);
      w.wind.dy = (by > 0.38f) ? 1 : (by < -0.38f ? -1 : 0);
      if (w.wind.strength == 0) { w.wind.dx = 0; w.wind.dy = 0; }
      return;
    }
  }

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

  // Live mode: the real sky decides. WMO weather codes -> vat states,
  // rain strength eases toward its target so showers still roll in/out.
  if (g_weatherMode == 1) {
    const LiveWeather& lw = liveWeatherNow();
    if (lw.valid) {
      WeatherState target = CLEAR;
      float rainT = 0.f;
      int c = lw.code;
      if (c >= 95) { target = STORM; rainT = 0.85f; }
      else if ((c >= 63 && c <= 67) || c == 81 || c == 82) { target = RAIN; rainT = 0.70f; }
      else if ((c >= 51 && c <= 61) || c == 80) { target = RAIN; rainT = 0.35f; }
      else if (lw.snowing) { target = RAIN; rainT = 0.45f; }  // rendered as snow
      else if (c >= 2 || lw.cloud > 60) target = OVERCAST;
      if (w.weather.state != target) { w.weather.state = target; w.weather.timer = 0; }
      float rs = w.weather.rainStrength;
      w.weather.rainStrength = rs + (rainT - rs) * 0.01f;
      w.cloudOpacity = 0.25f + 0.75f * (float)lw.cloud / 100.f;
      bool isRainingNow = (target == RAIN || target == STORM);
      if (w.weather.lastTickWasRaining && !isRainingNow && r.oneIn(3)) spawnRainbow(w, r);
      w.weather.lastTickWasRaining = isRainingNow;
      return;
    }
  }

  Season s = seasonAt(tick);

  // Biome-driven precipitation tendencies (higher => more rain; lower => drier).
  auto biomeRaininess = [&](Biome b)->float{
    switch (b) {
      case WETLAND:  return 1.15f;
      case TROPICAL: return 1.10f;
      case MEADOW:   return 0.45f;
      case ALPINE:   return 0.22f;
      case ALIEN:    return 0.95f;
      case DESERT:   return 0.08f;
      default:       return 0.80f;
    }
  };
  float raininess = biomeRaininess(w.biome);

  float cloudAvg = avgCloud(w.clouds);
  bool cloudy = cloudAvg > 120.f;
  bool veryCloudy = cloudAvg > 150.f;

  auto toState = [&](WeatherState ns){
    w.weather.state = ns;
    w.weather.timer = 0;
  };

  switch (w.weather.state) {
    case CLEAR: {
      w.weather.rainStrength = std::max(0.f, w.weather.rainStrength - 0.008f);
      if (cloudy && r.oneIn(9)) toState(OVERCAST);
      int chance = (s==SUMMER? 300 : s==SPRING? 240 : s==AUTUMN? 260 : 340);
      // Apply biome raininess: lower raininess => higher chance value (rarer transitions)
      chance = (int)std::clamp((float)chance / std::max(0.05f, raininess), 80.0f, 12000.0f);
      if (w.biome==TROPICAL) chance = std::max(120, chance-60);
      if (w.biome==DESERT) chance = std::max(4000, chance+2800); // far rarer rain in desert
      chance = (int)std::clamp((float)chance / std::max(0.10f, g_alea.rainChance * g_alea.chaos), 1.0f, 20000.0f);
      if (r.oneIn(chance)) toState(OVERCAST);
    } break;

    case OVERCAST: {
      // Overcast is grey sky, not drizzle: rain fades out here rather than
      // pre-loading to 0.35 (which made every shower arrive full-strength
      // and left permanent background rain — the "binary and heavy" feel).
      w.weather.rainStrength = std::max(0.f, w.weather.rainStrength - 0.006f);
      if (veryCloudy && w.weather.timer > 110 && r.oneIn(4)) toState(RAIN);
      if (!cloudy && w.weather.timer > 110 && r.oneIn(3)) toState(CLEAR);
      if (w.weather.timer > 500 && r.oneIn(3)) toState(CLEAR);
    } break;

    case RAIN: {
      // Roll in gently: ~2 minutes from first drops to full rain.
      float maxRain = (w.biome==ALPINE? 0.50f : (w.biome==MEADOW? 0.55f : (w.biome==DESERT? 0.18f : 1.0f)));
      w.weather.rainStrength = std::min(maxRain, w.weather.rainStrength + 0.0025f);
      int stormChance = (s==SUMMER ? 180 : 420);
      stormChance = (int)std::clamp((float)stormChance / std::max(0.05f, raininess), 90.0f, 25000.0f);
      if (w.biome==TROPICAL) stormChance = std::max(120, stormChance-80);
      if (w.biome==DESERT) stormChance = std::max(8000, stormChance+6000); // basically no storms
      if (w.weather.timer > 120 && r.oneIn(stormChance)) toState(STORM);
      if (w.weather.timer > 260 && r.oneIn(5)) toState(OVERCAST);
      if (w.weather.timer > 700) toState(OVERCAST);
    } break;

    case STORM: {
      float maxRain = (w.biome==ALPINE? 0.60f : (w.biome==MEADOW? 0.65f : (w.biome==DESERT? 0.22f : 1.0f)));
      w.weather.rainStrength = std::min(maxRain, w.weather.rainStrength + 0.008f);
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
    chance = (int)std::clamp((float)chance / std::max(0.10f, g_alea.spawnChance * g_alea.chaos), 1.0f, 20000.0f);
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

// ---------------- City worldgen ----------------
// Laid down after the base terrain and before the plant passes, so parks,
// street trees and the weeds in the cracks all come from the normal
// vegetation rules. Height doubles as storeys: every cell of one building
// carries the same value, which is what gives the renderer its facades.
static void seedCity(World& w, Rng& r) {
  // A harbour on one edge, and the downtown looking across it — the whole
  // reason a city reads as a city from above is the water and the skyline.
  const int edge = r.i(0, 3);  // 0 = N, 1 = E, 2 = S, 3 = W
  const float bayDepth = 0.20f + 0.12f * r.u01();
  const uint32_t coastSeed = w.worldSeed ^ 0x5EA51DEu;

  auto coastAt = [&](float along) {
    // Four octaves: a bay, then headlands, then inlets, then a ragged edge —
    // a two-term wobble gave the harbour straight diagonal sides.
    float wob = 0.62f
              + 0.34f * std::sin(along * 3.1f + (float)(coastSeed & 63u))
              + 0.20f * std::sin(along * 7.3f + (float)((coastSeed >> 6) & 63u))
              + 0.055f * std::sin(along * 14.3f + (float)((coastSeed >> 12) & 63u))
              + 0.022f * std::sin(along * 27.9f + (float)((coastSeed >> 18) & 63u));
    return bayDepth * wob * 2.0f;
  };

  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
    float fx = (float)x / (float)(W - 1), fy = (float)y / (float)(H - 1);
    float into, along;
    switch (edge) {
      case 0:  into = fy;        along = fx; break;
      case 1:  into = 1.f - fx;  along = fy; break;
      case 2:  into = 1.f - fy;  along = fx; break;
      default: into = fx;        along = fy; break;
    }
    float shore = coastAt(along);
    if (into < shore) {
      float t = (shore - into) / std::max(0.02f, shore);  // 0 at the shore
      w.water[y][x] = (uint8_t)std::clamp(2 + (int)(t * 6.f), 2, 7);
      w.terrain[y][x] = '.';
      // Harbour bed. Water flows to the lowest surface (bed + 10*depth), so
      // the whole city sits above the deepest water it can ever hold —
      // otherwise a spring tide runs up the avenues and never leaves.
      w.height[y][x] = (uint8_t)std::max(0, 90 - (int)(t * 20.f));
    } else {
      w.water[y][x] = 0;
      w.terrain[y][x] = CITY_LOT;
      w.height[y][x] = CITY_GROUND;
    }
  }

  // Downtown: a single core the towers cluster around, set back from the
  // water so the skyline has a waterfront to stand behind.
  float dcx, dcy;
  switch (edge) {
    case 0:  dcx = (0.30f + 0.40f * r.u01()) * W; dcy = (0.34f + 0.22f * r.u01()) * H; break;
    case 1:  dcx = (0.44f + 0.22f * r.u01()) * W; dcy = (0.30f + 0.40f * r.u01()) * H; break;
    case 2:  dcx = (0.30f + 0.40f * r.u01()) * W; dcy = (0.44f + 0.22f * r.u01()) * H; break;
    default: dcx = (0.34f + 0.22f * r.u01()) * W; dcy = (0.30f + 0.40f * r.u01()) * H; break;
  }
  const float coreR = (float)std::min(W, H) * (0.19f + 0.05f * r.u01());

  auto downtown01 = [&](int x, int y) {  // 1 at the core, 0 at the fringe
    float dx = (float)x - dcx, dy = (float)y - dcy;
    float d = std::sqrt(dx * dx + dy * dy) / coreR;
    return std::clamp(1.0f - d, 0.0f, 1.0f);
  };

  auto paveRoad = [&](int x, int y) {
    if (!inBounds(x, y)) return;
    if (w.water[y][x] > 0) return;   // bridges are laid separately
    w.terrain[y][x] = CITY_ROAD;
    w.height[y][x] = CITY_GROUND;
  };

  // ---- Street grid: avenues, then the streets between them ----
  std::vector<int> avX, avY, stX, stY;
  for (int x = r.i(5, 13); x < W - 4; ) {
    avX.push_back(x);
    x += r.i(13, 31);
  }
  for (int y = r.i(5, 13); y < H - 4; ) {
    avY.push_back(y);
    y += r.i(13, 31);
  }
  for (size_t i = 0; i + 1 < avX.size(); ++i) {
    int gap = avX[i + 1] - avX[i];
    if (gap >= 15 && !r.oneIn(5)) stX.push_back(avX[i] + gap / 2 + r.i(-3, 3));
    if (gap >= 26) stX.push_back(avX[i] + gap / 4 + r.i(-2, 2));
  }
  for (size_t i = 0; i + 1 < avY.size(); ++i) {
    int gap = avY[i + 1] - avY[i];
    if (gap >= 15 && !r.oneIn(5)) stY.push_back(avY[i] + gap / 2 + r.i(-3, 3));
    if (gap >= 26) stY.push_back(avY[i] + gap / 4 + r.i(-2, 2));
  }

  for (int ax : avX) for (int y = 0; y < H; ++y)
    for (int d = -1; d <= 1; ++d) paveRoad(ax + d, y);
  for (int ay : avY) for (int x = 0; x < W; ++x)
    for (int d = -1; d <= 1; ++d) paveRoad(x, ay + d);
  for (int sx : stX) for (int y = 0; y < H; ++y) paveRoad(sx, y);
  for (int sy : stY) for (int x = 0; x < W; ++x) paveRoad(x, sy);

  // Sidewalks wrap every road that still has a lot beside it.
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
    if (w.terrain[y][x] != CITY_LOT) continue;
    bool nextToRoad = false;
    const int dx4s[4] = {1, -1, 0, 0}, dy4s[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4 && !nextToRoad; ++k) {
      int nx = x + dx4s[k], ny = y + dy4s[k];
      if (inBounds(nx, ny) && w.terrain[ny][nx] == CITY_ROAD) nextToRoad = true;
    }
    if (nextToRoad) { w.terrain[y][x] = CITY_WALK; w.height[y][x] = CITY_WALK_H; }
  }

  // ---- Blocks: flood the lots between roads and build on each ----
  std::vector<std::vector<uint8_t>> seen(H, std::vector<uint8_t>(W, 0));
  std::vector<std::pair<int,int>> block;
  std::vector<std::pair<int,int>> stack;
  for (int by = 0; by < H; ++by) for (int bx = 0; bx < W; ++bx) {
    if (seen[by][bx] || w.terrain[by][bx] != CITY_LOT) continue;
    block.clear();
    stack.clear();
    stack.push_back({bx, by});
    seen[by][bx] = 1;
    while (!stack.empty()) {
      auto [cx, cy] = stack.back();
      stack.pop_back();
      block.push_back({cx, cy});
      const int dx4[4] = {1, -1, 0, 0}, dy4[4] = {0, 0, 1, -1};
      for (int k = 0; k < 4; ++k) {
        int nx = cx + dx4[k], ny = cy + dy4[k];
        if (!inBounds(nx, ny) || seen[ny][nx]) continue;
        if (w.terrain[ny][nx] != CITY_LOT) continue;
        seen[ny][nx] = 1;
        stack.push_back({nx, ny});
      }
    }
    if (block.empty()) continue;

    int cx = 0, cy = 0;
    for (auto& p : block) { cx += p.first; cy += p.second; }
    cx /= (int)block.size(); cy /= (int)block.size();
    float dt = downtown01(cx, cy);

    // District roll: parks and vacant ground stay, everything else builds.
    float roll = r.u01();
    if (roll < (0.04f + 0.07f * (1.f - dt)) * (dt > 0.50f ? 0.15f : 1.f)) {
      for (auto& p : block) {           // park — the plant passes fill it in
        w.terrain[p.second][p.first] = ',';
        w.height[p.second][p.first] = CITY_PARK_H;
      }
      continue;
    }
    if (roll > 0.96f) continue;         // a lot left empty; it may build later

    // Lots: chop the block into building footprints of a few cells each.
    // The lot grid is anchored to THIS block, so neighbouring blocks don't
    // share a subdivision and the city stops reading as graph paper.
    int lotW = r.i(3, 6), lotH = r.i(3, 6);
    int ox0 = r.i(0, lotW - 1), oy0 = r.i(0, lotH - 1);
    uint32_t blockSalt = hash3((uint32_t)bx, (uint32_t)by, w.worldSeed ^ 0xB10Cu);
    for (auto& p : block) {
      int lx = (p.first + ox0) / lotW, ly = (p.second + oy0) / lotH;
      uint32_t lh = hash3((uint32_t)lx, (uint32_t)ly, blockSalt);
      float lotRoll = (float)(lh & 1023u) / 1023.f;
      // Storeys: downtown is tall, the fringe is low; each lot is uniform so
      // the renderer can read a whole building off the height field.
      float tall = dt * dt * dt * (0.60f + 0.40f * (float)((lh >> 10) & 255u) / 255.f);
      // Every lot gets its own storey count: uniform heights made whole
      // districts share one roof colour and flattened the skyline.
      int storeys = 2 + (int)(tall * 34.f) + (int)((lh >> 18) % 5u);
      if (((lh >> 23) % 11u) == 0u) storeys += 3 + (int)((lh >> 26) % 9u);  // a spike
      char kind;
      if (storeys >= 18)      kind = (lotRoll < 0.55f) ? CITY_GLASS : CITY_TOWER;
      else if (storeys >= 9)  kind = (lotRoll < 0.25f) ? CITY_GLASS : CITY_MID;
      else                    kind = CITY_LOW;
      w.terrain[p.second][p.first] = kind;
      w.height[p.second][p.first] =
          (uint8_t)std::clamp(CITY_BASE_H + storeys * CITY_STOREY, CITY_BASE_H, 255);
    }
  }

  // ---- Neon: signage on the street-facing skin of low buildings ----
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
    char t = w.terrain[y][x];
    if (t != CITY_LOW && t != CITY_MID) continue;
    bool facesStreet = false;
    for (int k = 0; k < 4 && !facesStreet; ++k) {
      const int dx4[4] = {1, -1, 0, 0}, dy4[4] = {0, 0, 1, -1};
      int nx = x + dx4[k], ny = y + dy4[k];
      if (inBounds(nx, ny) &&
          (w.terrain[ny][nx] == CITY_WALK || w.terrain[ny][nx] == CITY_ROAD))
        facesStreet = true;
    }
    if (!facesStreet) continue;
    float dt = downtown01(x, y);
    if (r.u01() < 0.05f + 0.22f * dt) w.terrain[y][x] = CITY_NEON;
  }

  // ---- Quays where the built city meets the water ----
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
    if (w.water[y][x] > 0 || w.terrain[y][x] == '.') continue;
    if (countNeighborsWater(w.water, x, y) == 0) continue;
    if (w.terrain[y][x] == ',' || isTree(w.terrain[y][x])) continue;
    w.terrain[y][x] = CITY_QUAY;
    w.height[y][x] = CITY_QUAY_H;
  }

  // ---- Bridges: carry the avenues over the water ----
  auto bridgeLine = [&](bool horizontal, int fixed) {
    int span = horizontal ? W : H;
    int runStart = -1;
    for (int i = 0; i <= span; ++i) {
      int x = horizontal ? i : fixed;
      int y = horizontal ? fixed : i;
      bool wet = (i < span) && inBounds(x, y) && w.water[y][x] > 0;
      if (wet && runStart < 0) runStart = i;
      if (!wet && runStart >= 0) {
        int len = i - runStart;
        if (len >= 3 && len <= std::max(W, H) / 2) {  // a crossing, not the open bay
          for (int k = runStart; k < i; ++k) {
            for (int d = -1; d <= 1; ++d) {
              int bx = horizontal ? k : fixed + d;
              int by = horizontal ? fixed + d : k;
              if (inBounds(bx, by)) {
                w.terrain[by][bx] = CITY_BRIDGE;
                w.height[by][bx] = CITY_DECK_H;
              }
            }
          }
        }
        runStart = -1;
      }
    }
  };
  for (int ax : avX) bridgeLine(false, ax);
  for (int ay : avY) bridgeLine(true, ay);

  // ---- The expressway: one long elevated curve over the whole city ----
  {
    bool horizontal = r.oneIn(2);
    float amp = (float)(horizontal ? H : W) * (0.10f + 0.08f * r.u01());
    float base = (float)(horizontal ? H : W) * (0.30f + 0.40f * r.u01());
    float ph = r.u01() * 6.28f, freq = 0.9f + 1.4f * r.u01();
    int span = horizontal ? W : H;
    for (int i = 0; i < span; ++i) {
      float f = (float)i / (float)span;
      float off = base + amp * std::sin(f * freq * 6.28f + ph);
      for (int d = -1; d <= 1; ++d) {
        int x = horizontal ? i : (int)off + d;
        int y = horizontal ? (int)off + d : i;
        if (!inBounds(x, y)) continue;
        w.terrain[y][x] = CITY_BRIDGE;
        w.height[y][x] = CITY_DECK_H;
      }
    }
  }

  // Street trees along some avenues, and the harbour's own edge life.
  for (int ax : avX) {
    if (!r.oneIn(2)) continue;
    for (int y = 0; y < H; ++y) {
      int x = ax + (r.oneIn(2) ? 2 : -2);
      if (!inBounds(x, y) || w.terrain[y][x] != CITY_WALK) continue;
      if ((y % r.i(4, 7)) == 0) w.terrain[y][x] = 'T';
    }
  }

  w.ventX = -1; w.ventY = -1; w.eruptEnd = 0;
}

void seedWorld(World& w, Rng& r, Biome biome) {
  
  w.worldSeed = r.u32();
w.biome = biome;
  w.targetBiome = biome;
  w.biomeFade = 0.0f;
  w.biomeFadeDir = 0;
  w.bw = weightsFor(biome);
  w.biomeMorphActive = false;
  w.biomeMorphT = 0.0f;
  w.bwFrom = w.bw;
  w.bwTo = w.bw;

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
      // Wobble the pond boundary — perfect circles read as crop circles
      // once vegetation traces the moisture contour around them.
      uint32_t wobSeed = hash3((uint32_t)cx, (uint32_t)cy, w.worldSeed);
      for (int y=cy-rad-3; y<=cy+rad+3; ++y) for (int x=cx-rad-3; x<=cx+rad+3; ++x) {
        if (!inBounds(x,y)) continue;
        int dx=x-cx, dy=y-cy;
        float ang = std::atan2((float)dy, (float)dx);
        float wob = 1.f + 0.22f * std::sin(ang * 3.f + (float)(wobSeed & 63u)) +
                    0.14f * std::sin(ang * 6.f + (float)((wobSeed >> 6) & 63u));
        float radW = (float)rad * wob;
        float d2f = (float)(dx*dx+dy*dy);
        if (d2f > radW*radW) continue;
        float d2 = d2f, rr2 = radW*radW;
        if (d2 < rr2/3.f) w.water[y][x] = (uint8_t)std::max<int>(w.water[y][x], 3);
        else if (d2 < rr2*2.f/3.f) w.water[y][x] = (uint8_t)std::max<int>(w.water[y][x], 2);
        else w.water[y][x] = (uint8_t)std::max<int>(w.water[y][x], 1);
      }
    }
  } else if (biome == OCEAN) {
    // Open sea. Island mode rings a landmass with water; this is the
    // opposite — deep water everywhere, and whatever land there is has to
    // earn it. The swell, surf, whale and serpent machinery already exists;
    // this is the biome that is nothing but that.
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
      // Depth from the noise field so the sea floor has trenches and banks.
      int alt = (int)w.height[y][x];
      int depth = std::clamp(7 - (alt - 90) / 26, 3, 7);
      w.water[y][x] = (uint8_t)depth;
      w.terrain[y][x] = '.';
      w.height[y][x] = (uint8_t)std::clamp(40 + (alt - 90) / 3, 10, 90);
    }
    // Atolls: a handful of reef rings, most of them barely breaking the
    // surface, one or two with a beach and a palm on it.
    int atolls = 2 + r.i(0, 3);
    for (int a=0; a<atolls; ++a) {
      int cx = r.i(W/8, W-1-W/8), cy = r.i(H/8, H-1-H/8);
      float rad = (float)std::min(W,H) * (0.08f + 0.09f * r.u01());
      bool dry = !r.oneIn(3);           // most of them break the surface
      uint32_t wob = hash3((uint32_t)cx, (uint32_t)cy, w.worldSeed);
      for (int y=(int)(cy-rad-4); y<=(int)(cy+rad+4); ++y)
        for (int x=(int)(cx-rad-4); x<=(int)(cx+rad+4); ++x) {
          if (!inBounds(x,y)) continue;
          float dx = (float)(x-cx), dy = (float)(y-cy);
          float dist = std::sqrt(dx*dx + dy*dy);
          float ang = std::atan2(dy, dx);
          float rw = rad * (1.f + 0.20f * std::sin(ang*3.f + (float)(wob & 63u)) +
                                  0.12f * std::sin(ang*7.f + (float)((wob>>6) & 63u)));
          if (dist > rw) continue;
          float tt = dist / std::max(1.f, rw);   // 0 centre .. 1 rim
          if (tt > 0.66f) {                       // the reef rim
            w.water[y][x] = (uint8_t)(dry && tt > 0.78f ? 0 : (tt > 0.82f ? 1 : 2));
            w.height[y][x] = (uint8_t)(dry ? 150 : 120);
            if (w.water[y][x] == 0) w.terrain[y][x] = r.oneIn(4) ? '.' : 's';
            else if (r.oneIn(9)) w.terrain[y][x] = 'C';
          } else if (tt > 0.55f) {                // the lagoon shelf
            w.water[y][x] = 2;
            w.height[y][x] = 110;
            if (r.oneIn(14)) w.terrain[y][x] = 'C';
          } else {                                // the lagoon itself
            w.water[y][x] = 3;
            w.height[y][x] = 100;
          }
        }
      // Sea stacks: bare rock standing out of the water beside the reef.
      if (r.oneIn(2)) {
        int sx = cx + r.i(-(int)rad, (int)rad), sy = cy + r.i(-(int)rad, (int)rad);
        if (inBounds(sx, sy)) {
          w.water[sy][sx] = 0;
          w.height[sy][sx] = 200;
          w.terrain[sy][sx] = 'B';
          if (inBounds(sx+1, sy)) {
            w.water[sy][sx+1] = 0; w.height[sy][sx+1] = 190;
            w.terrain[sy][sx+1] = '^';
          }
        }
      }
    }
    // Kelp forests in the shallows around the atolls.
    for (int k=0; k < (W*H)/220; ++k) {
      int x = r.i(0, W-1), y = r.i(0, H-1);
      if (w.water[y][x] >= 1 && w.water[y][x] <= 3 && r.oneIn(2))
        w.terrain[y][x] = KELP_GLYPH;
    }
  } else if (biome == SKY) {
    // Nothing to seed. There is no ground up here: no terrain, no water, no
    // planting. `height` is the only field that carries anything, and what
    // it carries is not altitude but CLOUD BODY — the renderer reads it as
    // how much vapour is stacked over that cell, which is why the noise
    // field genHeight already produced is exactly the right shape to keep.
    // Everything else you see is drawn: the gradient, the layers, and the
    // traffic crossing them.
    for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
      w.terrain[y][x] = '.';
      w.water[y][x] = 0;
      w.moist[y][x] = 0;
    }
  } else if (biome == CITY) {
    seedCity(w, r);
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
  if (biome == CITY) ponds = 0;   // the harbour is the water here
  if (biome == OCEAN) ponds = 0;  // it is already all water
  if (biome == SKY) ponds = 0;    // ponds do not hang in mid-air
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
      if (w.water[y][x] >= 5) { w.springs.emplace_back(x,y); ++added; }
    }
  }

  // Through-rivers: carve a meandering channel from one map edge to the
  // opposite one, with a monotonic downhill height ramp so gravity carries
  // the flow ACROSS the world — in from offscreen at the headwater (a
  // persistent edge spring), out to offscreen at the mouth (edge drain).
  {
    int riverCount = 0;
    switch (biome) {
      case MEADOW: case ALPINE: case TROPICAL: riverCount = 1 + (r.oneIn(2) ? 1 : 0); break;
      case ALIEN:   riverCount = r.oneIn(2) ? 1 : 0; break;
      case WETLAND: riverCount = r.oneIn(3) ? 1 : 0; break;  // slow bayou channel
      case DESERT:  riverCount = r.oneIn(6) ? 1 : 0; break;  // rare oasis creek
      default: break;
    }
    for (int rv = 0; rv < riverCount; ++rv) {
      bool horizontal = r.oneIn(2);
      float px, py, txf, tyf;
      if (horizontal) { px = 0.f; py = (float)r.i(H/6, H-1-H/6); txf = (float)(W-1); tyf = (float)r.i(H/6, H-1-H/6); }
      else            { px = (float)r.i(W/6, W-1-W/6); py = 0.f; txf = (float)r.i(W/6, W-1-W/6); tyf = (float)(H-1); }
      if (r.oneIn(2)) { std::swap(px, txf); std::swap(py, tyf); }  // either direction

      float hStart = 150.f + 40.f * r.u01();
      float hEnd = 55.f + 20.f * r.u01();
      float total = std::abs(txf-px) + std::abs(tyf-py);
      float wanderPhase = r.u01() * 6.28f;
      float wanderAmp = 2.5f + 3.5f * r.u01();
      int guard = (int)(total * 3.f) + 64;
      float traveled = 0.f;

      int sx = (int)px, sy = (int)py;
      while (guard-- > 0) {
        int cx = (int)px, cy = (int)py;
        if (!inBounds(cx, cy)) break;
        float prog = std::min(1.f, traveled / std::max(1.f, total));
        uint8_t rampH = (uint8_t)(hStart + (hEnd - hStart) * prog);
        for (int oy=-2; oy<=2; ++oy) for (int ox=-2; ox<=2; ++ox) {
          int nx = cx+ox, ny = cy+oy;
          if (!inBounds(nx, ny)) continue;
          int man = std::abs(ox) + std::abs(oy);
          if (man <= 1) {  // channel
            w.height[ny][nx] = std::min(w.height[ny][nx], rampH);
            w.water[ny][nx] = std::max<uint8_t>(w.water[ny][nx], man == 0 ? 3 : 2);
            w.terrain[ny][nx] = '.';
          } else if (man == 2) {  // banks slope in
            w.height[ny][nx] = std::min(w.height[ny][nx], (uint8_t)std::min(255, rampH + 14));
          }
        }
        if (cx == (int)txf && cy == (int)tyf) break;
        float dx = txf - px, dy = tyf - py;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len < 1.f) break;
        dx /= len; dy /= len;
        float wob = std::sin(traveled * 0.12f + wanderPhase) * wanderAmp * 0.22f;
        px += dx - dy * wob + (r.u01() - 0.5f) * 0.5f;
        py += dy + dx * wob + (r.u01() - 0.5f) * 0.5f;
        traveled += 1.f;
      }
      // Headwater feeds forever; mouth drains offscreen via edgeDrain.
      w.springs.emplace_back(sx, sy);
    }
  }

for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
    if (w.water[y][x] > 0) continue;
    // In the city, height is storeys — a 30-floor tower is not a mountain.
    if (isCityGlyph(w.terrain[y][x])) continue;
    uint8_t alt = w.height[y][x];
    if (alt > 245) { w.terrain[y][x] = 'M'; if (biome==ALPINE) w.terrain[y][x] = '*'; continue; }
    if (alt > 232 && (biome==ALPINE ? r.oneIn(1) : r.oneIn(2))) w.terrain[y][x] = '^';
    if (alt > 238 && (biome==ALPINE ? r.oneIn(2) : r.oneIn(3))) w.terrain[y][x] = 'B';
  }

  for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
    if (w.water[y][x] > 0) continue;
    if (isCityGlyph(w.terrain[y][x])) continue;  // no reeds through the asphalt
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

  // Mud tends to form near water edges (adds earth tones) — but keep it biome-appropriate.
// Meadow should not become "mud-face fields".
  for (int k=0; k< (W*H)/520; ++k) {
    int x=r.i(0,W-1), y=r.i(0,H-1);
    if (w.water[y][x]>0) continue;
    int wet = countNeighborsWater(w.water, x, y);
    if (wet==0) continue;

    // Wetlands make mud readily; meadows rarely; alpine almost never.
    int denomEdge = 5;
    int denomGrass = 7;
    if (biome==WETLAND) { denomEdge = 3; denomGrass = 5; }
    if (biome==TROPICAL){ denomEdge = 4; denomGrass = 6; }
    if (biome==MEADOW)  { denomEdge = 18; denomGrass = 24; }
    if (biome==ALPINE)  { denomEdge = 40; denomGrass = 55; }
    if (biome==DESERT)  { denomEdge = 70; denomGrass = 90; }

    if (wet>0 && r.oneIn(denomEdge) && w.terrain[y][x]=='.') w.terrain[y][x]='d';
    if (wet>1 && r.oneIn(denomGrass) && (w.terrain[y][x]==','||w.terrain[y][x]=='"')) w.terrain[y][x]='d';
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
    if (biome==OCEAN) springCount = 0;
    if (biome==SKY)   springCount = 0;
    if (biome==CITY) {
      // The tide keeps the harbour full; a spring hunting for a low basin
      // would sink a pond into the middle of downtown instead.
      springCount = 0;
      int added = 0;
      for (int tries = 0; tries < 6000 && added < 2; ++tries) {
        int x = r.i(0, W-1), y = r.i(0, H-1);
        if (w.water[y][x] >= 5) { w.springs.emplace_back(x, y); ++added; }
      }
    }

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

  // ---- Island shaping (any biome): radial falloff into an ocean ring ----
  if (w.island) {
    const float cc = (float)W * 0.5f - 0.5f;
    const float R = (float)W * 0.5f;
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
      float dx = (float)x - cc, dy = (float)y - cc;
      float t = std::sqrt(dx*dx + dy*dy) / R;  // 0 centre .. 1 edge
      // Wobble the coastline so it isn't a perfect circle.
      float ang = std::atan2(dy, dx);
      float wob = 0.06f * std::sin(ang * 3.f + (float)(w.worldSeed & 63u)) +
                  0.04f * std::sin(ang * 7.f + (float)((w.worldSeed >> 6) & 63u));
      float shoreT = 0.60f + wob;   // where the beach starts
      float oceanT = shoreT + 0.10f;
      if (t > shoreT) {
        float sink = std::min(1.f, (t - shoreT) / 0.18f);
        int hh = (int)((float)w.height[y][x] * (1.f - sink) + 30.f * sink);
        w.height[y][x] = (uint8_t)std::max(0, hh);
        if (t > oceanT) {
          int depth = 3 + (int)((t - oceanT) / 0.10f * 3.f);
          w.water[y][x] = (uint8_t)std::min(7, std::max((int)w.water[y][x], depth));
          w.terrain[y][x] = '.';
          w.entities[y][x] = ' ';
        } else if (t > shoreT + 0.04f) {
          // beach / shallows
          if (w.water[y][x] == 0 && r.oneIn(3)) w.terrain[y][x] = 's';
          if (t > shoreT + 0.07f)
            w.water[y][x] = (uint8_t)std::max((int)w.water[y][x], r.oneIn(2) ? 1 : 2);
        }
        // Coral colonies in the sunlit shallows just off the beach.
        if (t > oceanT - 0.01f && t < oceanT + 0.07f && w.water[y][x] > 0 &&
            w.water[y][x] <= 3 && r.oneIn(9)) {
          w.terrain[y][x] = 'C';
        }
      }
    }

    // One island in three is volcanic: a basalt cone at its heart.
    w.ventX = -1; w.ventY = -1; w.eruptEnd = 0;
    if (r.oneIn(3)) {
      int vx = W / 2 + r.i(-W / 12, W / 12);
      int vy = H / 2 + r.i(-H / 12, H / 12);
      float coneR = (float)W * 0.14f;
      for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        float dx = (float)(x - vx), dy = (float)(y - vy);
        float t = std::sqrt(dx * dx + dy * dy) / coneR;
        if (t > 1.f) continue;
        int hh = 200 + (int)((1.f - t) * 55.f);
        w.height[y][x] = (uint8_t)std::max((int)w.height[y][x], hh);
        w.water[y][x] = 0;
        if (t < 0.20f) w.terrain[y][x] = 'B';        // crater rim rock
        else if (t < 0.55f && r.oneIn(2)) w.terrain[y][x] = '^';
      }
      w.terrain[vy][vx] = 'V';
      if (inBounds(vx + 1, vy)) w.terrain[vy][vx + 1] = 'V';
      w.ventX = vx; w.ventY = vy;
    }
  }
}

// ---------------- Water flow ----------------
// How much of the world is underwater right now (0..1).
static float wetFraction(const World& w) {
  int wet = 0;
  for (int y=0;y<H;++y) for (int x=0;x<W;++x) if (w.water[y][x] > 0) ++wet;
  return (float)wet / (float)(W*H);
}

// The wet fraction each biome should settle around. A kiosk vat runs for
// days: without a target, any tiny source/sink imbalance eventually turns
// the whole world to ocean (or desert).
static float biomeWetTarget(Biome b) {
  switch (b) {
    case WETLAND:  return 0.45f;
    case TROPICAL: return 0.30f;
    case ALIEN:    return 0.28f;
    case DESERT:   return 0.08f;
    case ALPINE:   return 0.18f;
    case CITY:     return 0.26f;  // the harbour, and it stays a harbour
    case OCEAN:    return 0.93f;  // it is the sea
    case SKY:      return 0.00f;  // there is no ground to be wet
    default:       return 0.22f;  // meadow
  }
}

static void stepWater(World& w, Rng& r) {
  Water next = w.water;

  // Island mode: the map edge is open sea — keep it topped up so the
  // ocean ring never drains away.
  if (hasOpenSea(w)) {
    for (int x=0; x<W; ++x) {
      if (next[0][x] > 0)   next[0][x]   = std::max<uint8_t>(next[0][x], 5);
      if (next[H-1][x] > 0) next[H-1][x] = std::max<uint8_t>(next[H-1][x], 5);
    }
    for (int y=0; y<H; ++y) {
      if (next[y][0] > 0)   next[y][0]   = std::max<uint8_t>(next[y][0], 5);
      if (next[y][W-1] > 0) next[y][W-1] = std::max<uint8_t>(next[y][W-1], 5);
    }
  }

  // Homeostat, source side: springs throttle as the world exceeds its
  // biome's wet target (sinks strengthen in waterSinks — the two together
  // guarantee an equilibrium instead of hoping the tuning balances).
  const float wetOver = wetFraction(w) -
      (biomeWetTarget(w.biome) + (w.island ? 0.30f : 0.f));


  // Persistent springs: keep rivers/lakes alive long-term.
  if (!w.springs.empty()) {
    for (auto &p : w.springs) {
      if (wetOver > 0.f && r.u01() < std::min(0.95f, wetOver * 6.f)) continue;
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
    int hitsBase = (int)((W*H)/420 * w.weather.rainStrength); // less global flooding
    float rainTileMul = 1.0f;
    if (w.biome==MEADOW) rainTileMul = 0.55f;
    if (w.biome==ALPINE) rainTileMul = 0.35f;
    if (w.biome==WETLAND) rainTileMul = 1.10f;
    if (w.biome==TROPICAL) rainTileMul = 1.05f;
    if (w.biome==DESERT) rainTileMul = 0.08f;
    int hits = (int)std::max(0.0f, hitsBase * rainTileMul);
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
      // Gravity: water seeks the lowest *surface* (terrain height + depth),
      // so it genuinely runs downhill, pools in basins, and overflows —
      // previously it only equalized depth and drifted with the wind.
      int score = (int)next[ny][nx]*10 + (int)w.height[ny][nx] + i;

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

  // Homeostat, sink side (see stepWater): the wetter the world is beyond
  // its biome's target, the harder evaporation/infiltration/drainage pull.
  {
    float over = wetFraction(w) -
        (biomeWetTarget(w.biome) + (w.island ? 0.30f : 0.f));
    if (over > 0.f) {
      float boost = 1.0f + 14.0f * over;
      evap *= boost;
      infil *= boost;
      edgeDrain *= boost;
    }
  }

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
    // The outermost ring is "the world continues offscreen": river mouths
    // that reach it flow out instead of pooling against an invisible wall.
    // (Not in island mode — there the edge IS the open sea.)
    if (!w.island && (x == 0 || y == 0 || x == W-1 || y == H-1) &&
        r.u01() < 0.05f) {
      d--;
      continue;
    }
  }
}



// ---------------- City succession ----------------
// A city is an ecology on a slower clock: lots are cleared and rebuilt, low
// blocks are replaced by taller ones where the land is worth it, signage
// comes and goes, and anything left alone long enough goes green. Rates are
// tuned for a kiosk — a visible change every few minutes, a different
// skyline by the end of the day, and no runaway in either direction.
static void stepCity(World& w, Rng& r, int tick) {
  const int lots = std::max(1, (W * H) / 900);
  for (int k = 0; k < lots; ++k) {
    int x = r.i(0, W - 1), y = r.i(0, H - 1);
    if (w.water[y][x] > 0) continue;
    char t = w.terrain[y][x];
    int h = (int)w.height[y][x];

    // Demand: tall neighbours mean a valuable block.
    int tallNear = 0, greenNear = 0;
    for (int oy = -2; oy <= 2; ++oy) for (int ox = -2; ox <= 2; ++ox) {
      int nx = x + ox, ny = y + oy;
      if (!inBounds(nx, ny) || (!ox && !oy)) continue;
      char n = w.terrain[ny][nx];
      if (isCityBuilding(n) && w.height[ny][nx] > (uint8_t)h) ++tallNear;
      if (isVeg(n) || isTree(n)) ++greenNear;
    }

    if (t == CITY_LOT) {
      // Vacant ground: it builds, or the weeds get there first.
      if (r.oneIn(greenNear > 3 ? 40 : 260)) { w.terrain[y][x] = ','; continue; }
      if (r.oneIn(90)) {
        w.terrain[y][x] = (tallNear > 6) ? CITY_MID : CITY_LOW;
        w.height[y][x] = (uint8_t)std::clamp(
            CITY_BASE_H + (tallNear > 6 ? 10 : 3) * CITY_STOREY, CITY_BASE_H, 255);
      }
      continue;
    }

    if (isCityBuilding(t)) {
      // Redevelopment: a block goes up a class when its neighbours have.
      if (r.oneIn(1400) && tallNear >= 8) {
        char up = (t == CITY_LOW) ? CITY_MID
                : (t == CITY_MID) ? (r.oneIn(2) ? CITY_GLASS : CITY_TOWER)
                : t;
        if (up != t) {
          w.terrain[y][x] = up;
          w.height[y][x] = (uint8_t)std::min(255, h + 6 * CITY_STOREY);
        } else if (h < 250) {
          w.height[y][x] = (uint8_t)std::min(255, h + CITY_STOREY);
        }
      }
      // Demolition: rarer than building, or the skyline only ever grows.
      else if (r.oneIn(9000)) {
        w.terrain[y][x] = CITY_LOT;
        w.height[y][x] = CITY_GROUND;
      }
      continue;
    }

    if (t == CITY_NEON) {
      if (r.oneIn(3000)) w.terrain[y][x] = CITY_LOW;  // the sign comes down
      continue;
    }

    // Pavement cracks: grass takes a sidewalk cell beside a park.
    if (t == CITY_WALK && greenNear >= 4 && r.oneIn(2200)) w.terrain[y][x] = ',';
    // ...and the city takes it back.
    if ((t == ',' || t == '"') && greenNear <= 1 && r.oneIn(2600) &&
        (int)w.height[y][x] <= CITY_PARK_H)
      w.terrain[y][x] = CITY_WALK;
  }
  (void)tick;
}

// ---------------- Terrain ecology ----------------
static void stepTerrain(World& w, Rng& r, Season s, int tick) {
  Grid next = w.terrain;

  float springBoost = (s==SPRING) ? 1.35f : 1.0f;
  float autumnMush  = (s==AUTUMN) ? 1.35f : 1.0f;
  float winterSlow  = (s==WINTER) ? 1.55f : 1.0f;

  float rainBoost = (w.weather.state==RAIN || w.weather.state==STORM) ? (1.0f + 0.7f*w.weather.rainStrength) : 1.0f;

  // Fire is rare; skip the per-cell ignite neighbor scan entirely on the
  // (vast majority of) ticks where nothing is burning.
  bool anyFire = false;
  for (int y=0; y<H && !anyFire; ++y)
    anyFire = (w.terrain[y].find('*') != std::string::npos);

  for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
    if (w.water[y][x] > 0) {
      if (w.terrain[y][x] == '*') next[y][x] = 'x';

      // Kelp dies back rarely so beds stay sparse instead of carpeting the
      // whole lake over kiosk timescales (it had no removal path at all).
      if (w.terrain[y][x] == KELP_GLYPH && r.oneIn(700)) next[y][x] = '.';

      // Underwater flora (fish cover): very sparse kelp in shallow water, wind+rain help it.
      if (w.water[y][x] <= 2 && (w.terrain[y][x]=='.' || w.terrain[y][x]==',' || w.terrain[y][x]==';')) {
        int boost = (w.biome==WETLAND || w.biome==TROPICAL) ? 1 : 0;
        boost += (w.weather.state==RAIN || w.weather.state==STORM) ? 1 : 0;
        int chance = 520 - 80*boost; // base very rare
        uint32_t hh = hash3((uint32_t)(x + tick/17), (uint32_t)(y - tick/23), 0x4B454C50u);
        if (chance > 60 && (hh % (uint32_t)chance)==0u) next[y][x] = KELP_GLYPH;
      }
      continue;
    }

    char c = w.terrain[y][x];

    // Beached kelp (water receded) dries out quickly.
    if (c == KELP_GLYPH) {
      if (r.oneIn(12)) next[y][x] = (r.oneIn(2) ? ',' : '.');
      continue;
    }

    // Dry coral bleaches into sand over time.
    if (c == 'C') {
      if (r.oneIn(160)) next[y][x] = 's';
      continue;
    }

    // The volcanic vent is eternal rock — nothing grows there.
    if (c == 'V') continue;

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

    if (anyFire && isVeg(c)) {
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

    // One pass over the 8 neighbors, bucketing by glyph. This used to be 12
    // separate countNeighborsChar() calls (~96 probes per cell) and dominated
    // the whole tick (~87% of step() in gprof).
    int wet=0, g=0, tg=0, sh=0, tr=0, flo=0, fern=0, reeds=0;
    for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
      if (dx==0 && dy==0) continue;
      int nx=x+dx, ny=y+dy;
      if (!inBounds(nx,ny)) continue;
      if (w.water[ny][nx] > 0) wet++;
      switch (w.terrain[ny][nx]) {
        case ',': g++; break;
        case '"': tg++; break;
        case '#': sh++; break;
        case 'T': case 'Y': case 'P': tr++; break;
        case 'f': case '+': case '&': case '!': case '$': flo++; break;
        case ';': fern++; break;
        case ':': reeds++; break;
        default: break;
      }
    }

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

    if (c=='d') { // mud dries back into soil/grass when not persistently wet
      if (wet==0) {
        int mm = (int)w.moist[y][x];
        bool dryAir = (w.weather.state==CLEAR || w.weather.state==OVERCAST);
        int denom = 140; // base drying rate
        if (w.biome==MEADOW) denom = 65;
        if (w.biome==ALPINE) denom = 55;
        if (w.biome==WETLAND) denom = 190;
        if (w.biome==TROPICAL) denom = 150;
        if (w.biome==DESERT) denom = 28;
        if (mm < 50 && dryAir && r.oneIn(std::max(10, (int)(denom*winterSlow)))) next[y][x] = (r.oneIn(3)?',':'.');
      }
      // If mud is right at the water edge, allow it to spread slightly in wetlands only.
      if (wet>1 && w.biome==WETLAND && r.oneIn(90)) {
        int nx=x+r.i(-1,1), ny=y+r.i(-1,1);
        if (inBounds(nx,ny) && w.water[ny][nx]==0 && w.terrain[ny][nx]=='.') next[ny][nx]='d';
      }
      continue;
    }

    if (c=='$') {
      // Fruit ripens and falls year-round (was winter-only decay — fine
      // with 3-minute seasons, but with day-long seasons a whole summer of
      // unchecked spread turned the land yellow).
      if (s==SUMMER && wet>0 && r.oneIn(900)) {
        int nx=x+r.i(-1,1), ny=y+r.i(-1,1);
        if (inBounds(nx,ny) && w.water[ny][nx]==0 && (w.terrain[ny][nx]==',' || w.terrain[ny][nx]==';')) next[ny][nx]='$';
      }
      if (r.oneIn(350)) next[y][x]='#';
      if (s==WINTER && r.oneIn(80)) next[y][x]='#';
      continue;
    }

    if (c==',') {
      // Desert superbloom: after real rain (live weather) soaks the ground,
      // the desert flowers — and fades again as the moisture decays.
      {
        // Ragged bloom edge: per-cell threshold jitter so the superbloom
        // forms organic patches, not a contour ring around round water.
        uint32_t bh2 = hash3((uint32_t)x, (uint32_t)y, 0xB100E5u);
        int thresh = 95 + (int)(bh2 % 55u);
        if (w.biome==DESERT && (int)w.moist[y][x] > thresh && r.oneIn(400))
          next[y][x] = (r.oneIn(4) ? '&' : 'f');
      }
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
      if (s==SUMMER && r.oneIn(2000) && w.biome!=ALPINE) next[y][x]='$';
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
      // Old age takes a tree now and then even on wet ground — in wetland
      // the dry-ground path above never fires, so forests only ever grew.
      if (r.oneIn(9000)) next[y][x]='#';
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

static void agentsInitFromGrid(World& w, Rng& r){
  w.agents.clear();
  int nextId=0;
  for(int y=0;y<H;++y){
    for(int x=0;x<W;++x){
      char g = w.entities[y][x];
      if(g=='.' || g==' ') continue;
      Agent a; a.id=nextId++; a.x=x; a.y=y; a.glyph=g; a.species = pickBiomeSpecies(w.biome, r);
      // randomize initial needs a bit
      a.hunger = r.u01()*0.5f;
      a.thirst = r.u01()*0.5f;
      a.stress = r.u01()*0.2f;
      a.health = 0.8f + r.u01()*0.2f;
      w.agents.push_back(a);
    }
  }
}


static inline bool cellPassableForAgent(const World& w, int x, int y){
  if(!inBounds(x,y)) return false;
  char t = w.terrain[y][x];
  if(t=='#') return false;
  return true;
}

static void ensureLegendaryCouple(World& w, Rng& r){
  bool haveA=false, haveB=false;
  for(const auto& a: w.agents){
    if(a.flags & AGF_LEGEND_A) haveA=true;
    if(a.flags & AGF_LEGEND_B) haveB=true;
  }
  auto spawnLegend = [&](uint8_t flag){
    // Try near camera center-ish, else random.
    int cx = W/2, cy = H/2;
    for(int tries=0; tries<4000; ++tries){
      int x = (tries<500) ? clampi(cx + (int)r.irange(-20,20), 0, W-1) : r.irange(0,W-1);
      int y = (tries<500) ? clampi(cy + (int)r.irange(-12,12), 0, H-1) : r.irange(0,H-1);
      if(!cellPassableForAgent(w,x,y)) continue;
      // don't spawn on another agent
      bool occ=false;
      for(const auto& a: w.agents){ if(a.x==x && a.y==y) { occ=true; break; } }
      if(occ) continue;

      Agent a;
      a.id = (int)w.agents.size() ? (w.agents.back().id + 1) : 999999;
      a.x=x; a.y=y;
      a.glyph = (flag==AGF_LEGEND_A)? 'Y':'Z'; // behavior glyph (kept distinct)
      a.species = pickBiomeSpecies(w.biome, r);
      a.hunger = r.u01()*0.2f;
      a.thirst = r.u01()*0.2f;
      a.stress = r.u01()*0.1f;
      a.fatigue = r.u01()*0.2f;
      a.health = 1.0f;
      a.flags |= flag;
      w.agents.push_back(a);
      return;
    }
  };

  if(!haveA) spawnLegend(AGF_LEGEND_A);
  if(!haveB) spawnLegend(AGF_LEGEND_B);
}
static void agentsWriteToGrid(World& w, int tick){
  // clear entities grid
  for(int y=0;y<H;++y) for(int x=0;x<W;++x) w.entities[y][x]=' ';
  for(auto &a: w.agents){
    if(!inBounds(a.x,a.y)) continue;
    w.entities[a.y][a.x]=speciesDisplayGlyph(a.species, w.biome, tick, (a.flags&AGF_LEGEND_A)!=0, (a.flags&AGF_LEGEND_B)!=0);
  }
}

float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }

static void applyRippleChaos(World& w, Rng& r, int tick) {
  if (g_ripples.empty()) return;

  for (const auto& rp : g_ripples) {
    float ring = rp.speed * rp.t;

    const float chaos = rp.chaos * g_alea.chaos;
    for (auto& agent : w.agents) {
      float rx = float(agent.x - rp.cx);
      float ry = float(agent.y - rp.cy);
      float dist = std::sqrt(rx * rx + ry * ry);
      float delta = std::fabs(dist - ring);
      if (delta > rp.width) continue;

      float strength =
          (1.0f - delta / std::max(0.001f, rp.width)) * rp.amp;
      if (dist < 0.001f) {
        rx = 1.0f;
        ry = 0.0f;
        dist = 1.0f;
      }

      float nx = rx / dist;
      float ny = ry / dist;
      int push = (strength > 1.6f) ? 2 : 1;
      int nextX = clampi(agent.x + (int)std::lround(nx * push), 0, W - 1);
      int nextY = clampi(agent.y + (int)std::lround(ny * push), 0, H - 1);

      if (!isAquatic(agent.glyph) && w.water[nextY][nextX] > 5) {
        if (std::fabs(nx) > std::fabs(ny)) {
          nextY = clampi(nextY + (r.oneIn(2) ? 1 : -1), 0, H - 1);
        } else {
          nextX = clampi(nextX + (r.oneIn(2) ? 1 : -1), 0, W - 1);
        }
      }

      agent.x = nextX;
      agent.y = nextY;
      agent.stress = clamp01(agent.stress + 0.10f * strength * chaos);
      if (r.u01() < 0.10f * chaos) agent.flags |= AGF_PANIC;
      if (r.u01() < 0.08f * chaos) {
        agent.hunger = clamp01(agent.hunger + 0.05f * chaos);
        agent.thirst = clamp01(agent.thirst + 0.05f * chaos);
      }
    }

    for (int i = 0; i < 10; ++i) {
      float angle = float(r.u01() * 6.2831853);
      float radius = ring + (r.u01() * 2.0f - 1.0f) * rp.width;
      int x = rp.cx + (int)std::lround(std::cos(angle) * radius);
      int y = rp.cy + (int)std::lround(std::sin(angle) * radius);
      if (!inBounds(x, y)) continue;

      float p = rp.chaos * g_alea.chaos;
      if (r.u01() < 0.25f * p) {
        int depth = (int)w.water[y][x];
        depth += (r.oneIn(2) ? 1 : -1);
        w.water[y][x] = (uint8_t)clampi(depth, 0, 7);
      }
      if (r.u01() < 0.18f * p) {
        static const char kOverlayChoices[] = {'~', '`', '*', '+', ';', '"', ':', '.'};
        w.overlay[y][x] = kOverlayChoices[r.irange(0, (int)(sizeof(kOverlayChoices) - 1))];
      }
      if (r.u01() < 0.08f * p) {
        char& terrain = w.terrain[y][x];
        if (terrain == '.') terrain = ',';
        else if (terrain == ',') terrain = '"';
        else if (terrain == '"') terrain = ';';
        else if (terrain == ';') terrain = '.';
      }
    }
  }

  (void)tick;
}


static inline float waterNearby01(const World& w,int x,int y){
  int wet=0, tot=0;
  for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx){
    if(dx==0 && dy==0) continue;
    int nx=x+dx, ny=y+dy;
    if(!inBounds(nx,ny)) continue;
    tot++;
    if(w.water[ny][nx]>0) wet++;
  }
  return tot? (float)wet/(float)tot : 0.f;
}

static inline int nearestPredatorDist(const World& w,int x,int y,int radius){
  int best = radius + 1;
  for (const auto &p : w.agents) {
    if (!isPredator(p.glyph)) continue;
    int dx = p.x - x;
    int dy = p.y - y;
    int d = std::abs(dx) + std::abs(dy);
    if (d > radius) continue;
    if (d < best) best = d;
  }
  return best;
}

static inline void moveRandom(Rng& r,int &x,int &y){
  static const int dirs[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
  int k=r.irange(0,7);
  x+=dirs[k][0]; y+=dirs[k][1];
}
// Occasional immigration keeps the ecosystem alive: without it the only
// agents that ever exist are the legendary couple (deaths are never replaced).
static void maybeImmigrateAgents(World& w, Rng& r) {
  const int cap = (W * H) / 300;
  if ((int)w.agents.size() >= cap) return;

  int denom = (int)std::max(2.0f, 8.0f / std::max(0.1f, g_alea.spawnChance));
  if (!r.oneIn(denom)) return;

  // Pick a behavior glyph: mostly grazers, some birds, a few predators,
  // fish when we land on water.
  char glyph = 'r';
  float u = r.u01();
  if (u < 0.15f) glyph = 'n';
  else if (u < 0.32f) glyph = 'v';

  for (int tries = 0; tries < 60; ++tries) {
    // Arrive from a world edge like a real migrant.
    int side = r.irange(0, 3);
    int x = (side == 0) ? 0 : (side == 1) ? W - 1 : r.irange(0, W - 1);
    int y = (side == 2) ? 0 : (side == 3) ? H - 1 : r.irange(0, H - 1);

    uint8_t depth = w.water[y][x];
    char resolved = glyph;
    if (depth > 2) resolved = (r.oneIn(2) ? '>' : '<');  // deep water: fish
    else if (depth > 0 && !isBird(glyph)) continue;      // shallows: birds only
    if (!cellPassableForAgent(w, x, y)) continue;

    Agent a;
    a.id = w.agents.empty() ? 1 : (w.agents.back().id + 1);
    a.x = x;
    a.y = y;
    a.glyph = resolved;
    a.species = pickBiomeSpecies(w.biome, r);
    a.hunger = r.u01() * 0.4f;
    a.thirst = r.u01() * 0.4f;
    a.fatigue = r.u01() * 0.3f;
    a.stress = r.u01() * 0.2f;
    a.health = 0.85f + r.u01() * 0.15f;
    w.agents.push_back(a);
    return;
  }
}

static void stepEntities(World& w, Rng& r, int tick) {
  // One-time migration: if agents list is empty, seed it from the existing entity glyph grid.
  if (w.agents.empty()) agentsInitFromGrid(w, r);

  // Update at a lower rate than the main sim tick to keep things cheap.
  // 10Hz-ish: every 6 ticks at 60 TPS, but still works at other TPS.
  bool doUpdate = (tick % 6) == 0;

  if (doUpdate) {
    maybeImmigrateAgents(w, r);
    for (auto &a : w.agents) {
      // Baseline needs
      float dt = 0.1f; // ~100ms
      float biomeThirst = (w.biome==DESERT) ? 1.6f : (w.biome==TROPICAL?0.9f:1.0f);
      float biomeHunger = (w.biome==ALPINE) ? 1.15f : 1.0f;

      a.hunger = clamp01(a.hunger + (0.020f*biomeHunger) * dt);
      a.thirst = clamp01(a.thirst + (0.030f*biomeThirst) * dt);

      // Stress decays
      a.stress = clamp01(a.stress - 0.050f * dt);

      // Species quirks (abstract "species" that vary per biome)
      // These are designed to create wacky interactions + interesting modulation signals.
      if (a.species == SPEC_PARASITE) {
        // Parasites raise nearby stress and steal a bit of hydration/energy.
        for (auto &b : w.agents) {
          if (&b==&a) continue;
          int d = std::abs(b.x-a.x) + std::abs(b.y-a.y);
          if (d<=1) {
            b.stress = clamp01(b.stress + 0.10f*dt);
            b.thirst = clamp01(b.thirst + 0.04f*dt);
            a.hunger = clamp01(a.hunger - 0.03f*dt);
          }
        }
      } else if (a.species == SPEC_ENGINEER) {
        // Engineers occasionally tweak terrain: seed plants or shallow water.
        if (r.oneIn(30)) {
          if (w.water[a.y][a.x]>0 && w.water[a.y][a.x]<3) {
            // turn shallow water into algae/plant hint
            if (isEdiblePlant(w.terrain[a.y][a.x])==false && r.oneIn(2)) w.terrain[a.y][a.x] = '"';
          } else if (w.water[a.y][a.x]==0 && r.oneIn(3)) {
            // sometimes dig a tiny puddle
            w.water[a.y][a.x] = 1;
          }
          a.stress = clamp01(a.stress - 0.04f*dt);
        }
      } else if (a.species == SPEC_SWARMER) {
        // Swarmers like being near others of their kind: calm when clustered, stress when isolated.
        int near=0;
        for (auto &b : w.agents) {
          if (&b==&a) continue;
          if (b.species != SPEC_SWARMER) continue;
          int d = std::abs(b.x-a.x) + std::abs(b.y-a.y);
          if (d<=4) near++;
        }
        if (near>=3) a.stress = clamp01(a.stress - 0.06f*dt);
        else a.stress = clamp01(a.stress + 0.03f*dt);
      } else if (a.species == SPEC_SHELLBACK) {
        // "Shellbacks" are calm but react strongly to ripples/chaos.
        if (!g_ripples.empty()) a.stress = clamp01(a.stress + 0.03f*dt);
        a.fatigue = clamp01(a.fatigue - 0.02f*dt);
      } else if (a.species == SPEC_MYSTIC) {
        // Mystics fluctuate: occasionally spike mood in either direction.
        if (r.oneIn(40)) {
          float j = (r.u01()<0.5f) ? -0.25f : 0.25f;
          a.stress = clamp01(a.stress + j);
          a.hunger = clamp01(a.hunger + 0.10f*(r.u01()-0.5f));
        }
      } else if (a.species == SPEC_TRICKSTER) {
        // Tricksters chase click-ripples; when close to a ripple origin they get euphoric.
        if (!g_ripples.empty()) {
          const Ripple& rp = g_ripples.back();
          int d = std::abs(a.x - rp.cx) + std::abs(a.y - rp.cy);
          if (d<6) {
            a.stress = clamp01(a.stress - 0.08f*dt);
            a.hunger = clamp01(a.hunger + 0.02f*dt); // "forget to eat"
          }
        }
      } else if (a.species == SPEC_PACKHUNTER) {
        // Packhunters get bold in groups and increase predator pressure.
        int pack=0;
        for (auto &b : w.agents) {
          if (b.species!=SPEC_PACKHUNTER) continue;
          int d=std::abs(b.x-a.x)+std::abs(b.y-a.y);
          if (d<=5) pack++;
        }
        if (pack>=3) a.stress = clamp01(a.stress - 0.05f*dt);
      }

      // Threat / chase
      int pd = nearestPredatorDist(w, a.x, a.y, 8);
      if (!isPredator(a.glyph) && pd <= 6) {
        float threat = (6 - pd) / 6.0f;
        a.stress = clamp01(a.stress + (0.45f * threat) * dt);
        if (a.stress > 0.75f) a.flags |= 1;
      } else {
        a.flags &= ~1;
      }

      // Drink if near water (non-aquatic). Aquatic counts as always hydrated.
      if (!isAquatic(a.glyph)) {
        float wet01 = waterNearby01(w, a.x, a.y);
        if (wet01 > 0.3f) a.thirst = clamp01(a.thirst - (0.25f*wet01) * dt);
      } else {
        a.thirst = clamp01(a.thirst - 0.40f * dt);
      }

      // Eat if on edible plant and herbivore-ish
      char &tile = w.terrain[a.y][a.x];
      if (isHerbivore(a.glyph) && isEdiblePlant(tile)) {
        a.hunger = clamp01(a.hunger - 0.30f * dt);
        tile = grazed(tile);
        a.stress = clamp01(a.stress - 0.05f * dt);
      }

      // Health: starve/dehydrate/panic costs
      float harm = 0.0f;
      if (a.hunger > 0.92f) harm += (a.hunger - 0.92f) * 0.6f;
      if (a.thirst > 0.90f) harm += (a.thirst - 0.90f) * 0.9f;
      if ((a.flags & 1) && a.stress > 0.8f) harm += 0.15f * (a.stress - 0.8f);
      a.health = clamp01(a.health - harm * dt);
      if (isLegendary(a)) { a.health = std::max(a.health, 0.12f); a.stress = clamp01(a.stress * 0.985f); }

      // Predators can damage nearby prey
      if (isPredator(a.glyph)) {
        for (auto &prey : w.agents) {
          if (&prey==&a) continue;
          if (isPredator(prey.glyph)) continue;
          int d = std::abs(prey.x - a.x) + std::abs(prey.y - a.y);
          if (d==1 && r.oneIn(8)) {
            prey.health = clamp01(prey.health - 0.25f);
            prey.stress = clamp01(prey.stress + 0.6f);
            prey.flags |= 1;
          }
        }
      }

      // Simple recovery if calm & fed
      if (a.hunger < 0.35f && a.thirst < 0.35f && a.stress < 0.35f) {
        a.health = clamp01(a.health + 0.06f * dt);
      }

            // Derive mood from strongest drive (DF-ish but cheap)
      a.mood = MOOD_CALM;
      float best = a.stress;
      if (a.thirst > best) { best = a.thirst; a.mood = MOOD_THIRSTY; }
      if (a.hunger > best) { best = a.hunger; a.mood = MOOD_HUNGRY; }
      if (a.stress > 0.70f) a.mood = MOOD_FEARFUL;
      if (a.health < 0.25f && a.stress > 0.6f) a.mood = MOOD_ENRAGED;
      if (a.hunger < 0.25f && a.thirst < 0.25f && a.stress < 0.20f) a.mood = MOOD_EUPHORIC;

// Movement: very simple, but driven by needs and panic.
      int ox=a.x, oy=a.y;
      int nx=a.x, ny=a.y;

      // Night: calm creatures mostly sleep (rest recovers fatigue); anyone
      // fleeing, starving or parched still moves. Daylight ramps back in
      // smoothly, so activity fades rather than switching.
      {
        float dl = daylightNow(tick).level;
        bool urgent = (a.flags & 1) || a.hunger > 0.75f || a.thirst > 0.75f;
        if (!urgent && dl < 0.6f) {
          float restChance = (0.6f - dl) / 0.6f * 0.75f;  // up to 75% at full dark
          if (r.u01() < restChance) {
            a.intent = INTENT_WANDER;
            a.fatigue = clamp01(a.fatigue - 0.05f * dt);
            continue;
          }
        }
      }

      if (a.flags & 1) {
        a.intent = INTENT_FLEE;
        // flee: move away from nearest predator (Manhattan)
        int bestDx=0, bestDy=0, bestScore=-999;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx){
          if(dx==0 && dy==0) continue;
          int tx=a.x+dx, ty=a.y+dy;
          if(!inBounds(tx,ty)) continue;
          // avoid deep water if land
          if(!isAquatic(a.glyph) && w.water[ty][tx]>2) continue;
          int d = nearestPredatorDist(w, tx, ty, 8);
          int score = d;
          if(score>bestScore){ bestScore=score; bestDx=dx; bestDy=dy; }
        }
        nx=a.x+bestDx; ny=a.y+bestDy;
      } else if (a.thirst > 0.65f && !isAquatic(a.glyph)) {
        a.intent = INTENT_DRINK;
        // drift toward water
        int bestDx=0,bestDy=0; float best=waterNearby01(w,a.x,a.y);
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx){
          if(dx==0 && dy==0) continue;
          int tx=a.x+dx, ty=a.y+dy;
          if(!inBounds(tx,ty)) continue;
          float w01=waterNearby01(w,tx,ty);
          if(w01>best){ best=w01; bestDx=dx; bestDy=dy; }
        }
        nx=a.x+bestDx; ny=a.y+bestDy;
        if(best<0.34f && r.oneIn(3)) moveRandom(r,nx,ny);
      } else if (a.hunger > 0.65f && isHerbivore(a.glyph)) {
        a.intent = INTENT_FORAGE;
        // drift toward plants
        int bestDx=0,bestDy=0; int bestScore=-999;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx){
          if(dx==0 && dy==0) continue;
          int tx=a.x+dx, ty=a.y+dy;
          if(!inBounds(tx,ty)) continue;
          if(!isAquatic(a.glyph) && w.water[ty][tx]>2) continue;
          int score = isEdiblePlant(w.terrain[ty][tx]) ? 2 : 0;
          score += (w.water[ty][tx]==0)?1:0;
          if(score>bestScore){ bestScore=score; bestDx=dx; bestDy=dy; }
        }
        nx=a.x+bestDx; ny=a.y+bestDy;
        if(bestScore<=0 && r.oneIn(3)) moveRandom(r,nx,ny);
      } else {
        a.intent = INTENT_WANDER;
        // wander
        if (r.oneIn(3)) moveRandom(r,nx,ny);
      }

      // Aquatic: keep in water
      if (isAquatic(a.glyph)) {
        if (!inBounds(nx,ny) || w.water[ny][nx]==0) { nx=a.x; ny=a.y; }
      } else {
        if (!inBounds(nx,ny)) { nx=a.x; ny=a.y; }
      }

      a.x=nx; a.y=ny;

      // Fatigue: movement costs, calm recovers
      if (a.x!=ox || a.y!=oy) a.fatigue = clamp01(a.fatigue + 0.08f * dt);
      else a.fatigue = clamp01(a.fatigue - 0.04f * dt);

      if (a.x!=ox || a.y!=oy) {
        StepEvent ev; ev.x=a.x; ev.y=a.y; ev.dx=a.x-ox; ev.dy=a.y-oy; ev.glyph=a.glyph;
        ev.strength = (std::abs(ev.dx)+std::abs(ev.dy) > 1) ? 2.f : 1.f;
        g_stepEvents.push_back(ev);
      }
    }

    // Cull dead agents (and clear their glyph). The inspected agent is
    // tracked by index, so re-find it by id after the erase shifts indices.
    int inspectId = -1;
    if (g_inspectIdx >= 0 && g_inspectIdx < (int)w.agents.size())
      inspectId = w.agents[g_inspectIdx].id;
    w.agents.erase(std::remove_if(w.agents.begin(), w.agents.end(),
      [&](const Agent& a){ return a.health <= 0.01f; }), w.agents.end());
    if (inspectId >= 0) {
      g_inspectIdx = -1;
      for (int i = 0; i < (int)w.agents.size(); ++i) {
        if (w.agents[i].id == inspectId) { g_inspectIdx = i; break; }
      }
    }
  }

  agentsWriteToGrid(w, tick);
}

// big ancient tree anchors 'Q'
static void maybeSpawnAncientTree(World& w, Rng& r) {
  // Ancient trees were immortal — over kiosk timescales they slowly filled
  // the world. Cap the population and let each one very rarely fall,
  // leaving a mushroom ring on the forest floor.
  int count = 0;
  for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
    if (w.entities[y][x] != 'Q') continue;
    ++count;
    if (r.oneIn(30000)) {
      w.entities[y][x] = ' ';
      w.terrain[y][x] = 'm';
      --count;
    }
  }
  if (count >= std::max(2, (W*H)/900)) return;
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
void step(World& w, Rng& r, std::string& banner, int tick) {
  clearOverlay(w);
  // keep the Legendary Couple in play
  ensureLegendaryCouple(w, r);

  Season s = seasonAt(tick);

  evolveClouds(w.clouds, r, w.wind, w.weather, tick);
  updateWeather(w, r, tick);
  updateWind(w, r, tick);

  if (w.weather.state == STORM && r.oneIn(35)) lightning(w, r, banner);
  maybeChaos(w, r, banner, s);

  // Volcano: long dormancy, then a few minutes of eruption — lava light,
  // embers, and fires seeded down the slopes (the fire/regrowth ecology
  // handles the aftermath; the scar heals over the next day).
  if (w.ventX >= 0) {
    if (w.eruptEnd <= tick && r.oneIn(35000)) {
      w.eruptEnd = tick + 500;
      banner = "the mountain wakes";
    }
    if (w.eruptEnd > tick && r.oneIn(4)) {
      int ex = w.ventX + r.i(-12, 12), ey = w.ventY + r.i(-12, 12);
      if (inBounds(ex, ey) && w.water[ey][ex] == 0 && isVeg(w.terrain[ey][ex]))
        w.terrain[ey][ex] = '*';
    }
  }

  stepWater(w, r);
  waterSinks(w, r, s);
  stepTerrain(w, r, s, tick);
  if (w.biome == CITY) stepCity(w, r, tick);
  stepEntities(w, r, tick);
  applyRippleChaos(w, r, tick);
  maybeSpawnAncientTree(w, r);

  applyRainOverlay(w, tick);
}
