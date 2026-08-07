// One-pixel-per-cell world rendering, shared by the pico build and the
// plugin UI: cheap enough for a Pi Zero (or a host's audio thread),
// detailed enough to watch the vat live.
//
// SDL-free on purpose — consumers pack the returned triple into whatever
// pixel format their surface wants (pico: ARGB texture, plugin UI: RGBA GL).
#pragma once

#include "terrarium_core.hpp"

struct PixelviewRGB {
  uint8_t r = 0, g = 0, b = 0;
};

// Ambient mote scheduler: per-cell time offsets so nothing blinks in sync,
// a sin^2 lifetime envelope so motes fade in and out instead of popping,
// and an individual flutter rate per mote. Returns 0..1 intensity, or 0
// when this cell has no mote in its current epoch (sparse is sexy).
inline float pixelviewMote(uint32_t h, float animT, float lifeSec,
                           uint32_t density, float flutterHz) {
  float t = (animT + (float)(h % 1024u) * 0.037f) / lifeSec;
  uint32_t epoch = (uint32_t)t;
  float frac = t - (float)epoch;
  uint32_t sh = hash3(h, epoch * 2654435761u, 0x4D4F5445u);
  if ((sh % density) != 0u) return 0.f;
  float env = std::sin(3.14159f * frac);
  env *= env;
  float rate = flutterHz * (0.7f + 0.6f * (float)((sh >> 3) & 15u) / 15.f);
  float fl = 0.75f + 0.25f * std::sin(animT * rate + (float)(sh & 63u));
  return env * fl;
}

// Smooth value noise over the cell grid, 0..1. Hash noise alone is white
// noise — every cell independent — which at 1px/cell is indistinguishable
// from dead pixels. Interpolating between lattice points at `scale` cells
// gives blotches with SIZE, which is what makes a dusting of snow read as
// drifted rather than as static, and a cloud read as a cloud.
inline float pixelviewValueNoise(int x, int y, float scale, uint32_t salt) {
  float fx = (float)x / scale, fy = (float)y / scale;
  int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
  float tx = fx - (float)ix, ty = fy - (float)iy;
  tx = tx * tx * (3.f - 2.f * tx);   // smoothstep: no lattice creases
  ty = ty * ty * (3.f - 2.f * ty);
  auto at = [&](int gx, int gy) {
    return (float)(hash3((uint32_t)gx, (uint32_t)gy, salt) & 1023u) / 1023.f;
  };
  float a = at(ix, iy), b2 = at(ix + 1, iy);
  float c = at(ix, iy + 1), d2 = at(ix + 1, iy + 1);
  return (a + (b2 - a) * tx) * (1.f - ty) + (c + (d2 - c) * tx) * ty;
}

// Two octaves, for when one blotch size reads as wallpaper.
inline float pixelviewFbm2(int x, int y, float scale, uint32_t salt) {
  return pixelviewValueNoise(x, y, scale, salt) * 0.65f +
         pixelviewValueNoise(x, y, scale * 0.4f, salt ^ 0x9E37u) * 0.35f;
}

// Float-coordinate versions. The cell-grid ones above are enough for
// anything locked to the lattice (snow, soil), but clouds are sampled in a
// ROTATED, STRETCHED frame — along the wind rather than along the pixel
// grid — and rounding those coordinates to integers quantises the rotation
// into visible staircase edges.
inline float pixelviewValueNoiseF(float fx, float fy, float scale,
                                  uint32_t salt) {
  fx /= scale; fy /= scale;
  float ffx = std::floor(fx), ffy = std::floor(fy);
  int ix = (int)ffx, iy = (int)ffy;
  float tx = fx - ffx, ty = fy - ffy;
  tx = tx * tx * (3.f - 2.f * tx);
  ty = ty * ty * (3.f - 2.f * ty);
  auto at = [&](int gx, int gy) {
    return (float)(hash3((uint32_t)gx, (uint32_t)gy, salt) & 1023u) / 1023.f;
  };
  float a = at(ix, iy), b2 = at(ix + 1, iy);
  float c = at(ix, iy + 1), d2 = at(ix + 1, iy + 1);
  return (a + (b2 - a) * tx) * (1.f - ty) + (c + (d2 - c) * tx) * ty;
}

// Three octaves for clouds: one blob size is a texture, three is a FORM —
// the big octave gives the mass, the middle the lobes, the small the frayed
// edge where it is coming apart.
inline float pixelviewFbm3F(float fx, float fy, float scale, uint32_t salt) {
  return pixelviewValueNoiseF(fx, fy, scale, salt) * 0.54f +
         pixelviewValueNoiseF(fx, fy, scale * 0.45f, salt ^ 0x9E37u) * 0.31f +
         pixelviewValueNoiseF(fx, fy, scale * 0.19f, salt ^ 0x2C1Fu) * 0.15f;
}

// CUMULUS, which smooth fBm cannot give you. Thresholding a smooth field
// produces rounded blobs that merge and part like metaballs — mathematically
// pretty, meteorologically wrong, and instantly recognisable as fake.
//
// Two changes fix it. BILLOW noise (1 - |2n-1|) creases where the smooth
// version had a gentle maximum, so every octave contributes a puffy lobe
// with a hard crease between it and the next — the cauliflower structure of
// a real cumulus. DOMAIN WARPING then offsets where the field is sampled by
// another noise field, so boundaries wander and fold back on themselves
// instead of running smoothly. Neither is expensive; together they are the
// difference between a lava lamp and a sky.
inline float pixelviewBillowF(float fx, float fy, float scale, uint32_t salt) {
  auto oct = [&](float s, uint32_t k) {
    float n = pixelviewValueNoiseF(fx, fy, s, salt ^ k);
    return 1.f - std::fabs(2.f * n - 1.f);        // crease at the midline
  };
  // Weighted hard toward the LOW octave. Billow's creases are ridges, and
  // piling up high-frequency octaves of them turns the field into a mess of
  // thin filaments — marble, not cumulus. The big lobe carries it; the
  // small ones only roughen the edge.
  return oct(scale, 0u) * 0.66f +
         oct(scale * 0.44f, 0x9E37u) * 0.22f +
         oct(scale * 0.19f, 0x2C1Fu) * 0.12f;
}

inline float pixelviewCloudF(float fx, float fy, float scale, uint32_t salt) {
  // Warp by a lower-frequency field, offset per axis. The 0.5 shift keeps
  // the two warp channels from being the same wave. Kept MODEST: a strong
  // warp swirls the field into marbling, which is a different wrong answer
  // from metaballs but just as wrong.
  float wu = pixelviewValueNoiseF(fx + 31.7f, fy - 12.3f, scale * 1.9f,
                                  salt ^ 0x77A1u) - 0.5f;
  float wv = pixelviewValueNoiseF(fx - 8.9f, fy + 44.1f, scale * 1.9f,
                                  salt ^ 0x3B0Du) - 0.5f;
  const float warp = scale * 0.22f;
  float wxx = fx + wu * warp, wyy = fy + wv * warp;
  // Smooth fBm carries the MASS (where cloud is at all); billow supplies the
  // lobes and creases within it. Pure billow is all edge and no body; pure
  // fBm is the metaball. The blend is the cloud.
  float smooth = pixelviewFbm3F(wxx, wyy, scale, salt);
  float billow = pixelviewBillowF(wxx, wyy, scale * 0.85f, salt ^ 0x1234u);
  return smooth * 0.62f + billow * 0.38f;
}

// Shared ocean swell field: three wave components, angular shape noise,
// slow group envelope (waves arrive in sets). Island mode propagates
// radially inward; mainland follows the wind. Used by BOTH the deep and
// the shallows so sets roll continuously from open sea into the break.
inline float pixelviewSwell(const World& w, int x, int y, float animT,
                            uint32_t h, float* grpOut, float* chopOut = nullptr) {
  // A sea current, not a bullseye: one coherent directional flow across
  // the whole ocean (radial island waves read as a clock face). Wind sets
  // the heading; it veers slowly (~10 min) so the sea never goes static.
  float wa = (w.wind.dx == 0 && w.wind.dy == 0)
                 ? 0.7f
                 : std::atan2((float)w.wind.dy, (float)w.wind.dx);
  wa += 0.5f * std::sin(animT * 0.009f);
  float ca = std::cos(wa), sa = std::sin(wa);
  float base = (float)x * ca + (float)y * sa;
  float along = (float)y * ca - (float)x * sa;   // along the crest
  float ang = 0.13f * along;

  // Primary swell: LONG-CRESTED. Real ground swell arrives as coherent lines
  // that hold across the whole field — the old version modulated crest phase
  // hard enough (1.3 rad) that the lines broke up into mush at this scale.
  float s1 = std::sin(0.34f * base + animT * 1.55f +
                      0.42f * std::sin(ang * 1.3f + animT * 0.13f));
  // Secondary train, crossing at a slight angle: the interference between two
  // swells is what stops a sea looking like corrugated iron.
  float wa2 = wa + 0.42f;
  float base2 = (float)x * std::cos(wa2) + (float)y * std::sin(wa2);
  float s2 = std::sin(0.21f * base2 + animT * 1.02f +
                      0.5f * std::sin(ang * 0.7f - animT * 0.09f));
  // Chop: short, fast, wind-aligned, and incoherent — the surface texture.
  float chop = std::sin(1.35f * base + animT * 5.2f + (float)(h & 15u) * 0.41f) *
               0.6f +
               std::sin(1.90f * (base * 0.7f + along * 0.7f) - animT * 6.4f +
                        (float)((h >> 4) & 15u) * 0.37f) * 0.4f;

  // Sets: long groups, so the sea breathes instead of pulsing evenly.
  float grp = 0.55f + 0.45f * std::sin(0.045f * base + animT * 0.30f +
                                       0.7f * std::sin(along * 0.02f));
  if (grpOut) *grpOut = grp;
  // Wave energy follows the wind (live mode: the real wind).
  float energy = 0.70f + 0.10f * (float)w.wind.strength;
  if (chopOut) *chopOut = chop * (0.35f + 0.65f * energy);
  return (0.62f * s1 + 0.38f * s2) * grp * energy;
}

// Island cast (ship, seabirds, whale): positions computed once per frame.
struct PixelviewCast {
  float t = -1e9f;
  float birdX[3], birdY[3];
  bool shipUp = false;
  float shipX, shipY, shipAng;
  bool pirateUp = false;
  float pirX, pirY, pirAng;
  bool whaleUp = false;
  float whaleX, whaleY, whaleAge;
  bool serpentUp = false;
  float serpX[6], serpY[6];
};
inline PixelviewCast& pixelviewCast(float animT) {
  static PixelviewCast C;
  if (C.t != animT) {
    C.t = animT;
    float cc = (float)W * 0.5f - 0.5f;
    float R = (float)W * 0.5f;
    for (int i = 0; i < 3; ++i) {
      float a = animT * (0.09f + 0.02f * (float)i) + (float)i * 2.09f;
      float rad = R * (0.52f + 0.05f * std::sin(animT * 0.21f + (float)i));
      C.birdX[i] = cc + rad * std::cos(a);
      C.birdY[i] = cc + rad * std::sin(a);
    }
    // The ship comes and goes: every ~4 minutes she enters from beyond the
    // edge, sails a straight passage that clears the island, and exits.
    {
      uint32_t sep = (uint32_t)(animT / 240.f);
      uint32_t shh = hash3(sep, 0x5A11u, 0xB0A7u);
      float age = animT - (float)sep * 240.f;
      C.shipUp = age < 110.f;
      if (C.shipUp) {
        float th = (float)(shh & 1023u) / 1023.f * 6.283f;
        float side = ((shh >> 12) & 1u) ? 1.f : -1.f;
        float tx2 = cc + R * 0.82f * std::cos(th);
        float ty2 = cc + R * 0.82f * std::sin(th);
        float dirx = -std::sin(th) * side, diry = std::cos(th) * side;
        float span = R * 1.35f;
        C.shipX = tx2 + dirx * (-span + age * (2.f * span / 110.f));
        C.shipY = ty2 + diry * (-span + age * (2.f * span / 110.f));
        C.shipAng = std::atan2(diry, dirx);
      }
    }
    // The pirate ship: rarer, on her own schedule, running dark.
    {
      uint32_t pep = (uint32_t)(animT / 560.f);
      uint32_t phh = hash3(pep, 0x9147Eu, 0xB1AC4u);
      float age = animT - (float)pep * 560.f;
      C.pirateUp = ((phh % 2u) == 0u) && age < 100.f;
      if (C.pirateUp) {
        float th = (float)(phh & 1023u) / 1023.f * 6.283f;
        float side = ((phh >> 12) & 1u) ? 1.f : -1.f;
        float tx2 = cc + R * 0.78f * std::cos(th);
        float ty2 = cc + R * 0.78f * std::sin(th);
        float dirx = -std::sin(th) * side, diry = std::cos(th) * side;
        float span = R * 1.35f;
        C.pirX = tx2 + dirx * (-span + age * (2.f * span / 100.f));
        C.pirY = ty2 + diry * (-span + age * (2.f * span / 100.f));
        C.pirAng = std::atan2(diry, dirx);
      }
    }
    uint32_t wep = (uint32_t)(animT / 75.f);
    uint32_t wh = hash3(wep, 0x37A1Eu, 0x1234u);
    C.whaleAge = animT - (float)wep * 75.f;
    C.whaleUp = ((wh % 3u) == 0u) && C.whaleAge < 14.f;
    if (C.whaleUp) {
      float wa = (float)((wh >> 4) & 1023u) / 1023.f * 6.283f;
      float wr = R * (0.68f + 0.14f * (float)((wh >> 14) & 255u) / 255.f);
      C.whaleX = cc + wr * std::cos(wa);
      C.whaleY = cc + wr * std::sin(wa);
    }
    // Sea serpent: rare, surfaces for half a minute, undulating humps.
    uint32_t sep2 = (uint32_t)(animT / 160.f);
    uint32_t sh3 = hash3(sep2, 0x5EA9u, 0x77u);
    float sAge = animT - (float)sep2 * 160.f;
    C.serpentUp = ((sh3 % 4u) == 0u) && sAge < 30.f;
    if (C.serpentUp) {
      float dir = ((sh3 >> 8) & 1u) ? 1.f : -1.f;
      float sa = (float)((sh3 >> 4) & 1023u) / 1023.f * 6.283f +
                 dir * sAge * 0.06f;
      float srad = R * (0.70f + 0.08f * std::sin(sAge * 0.35f));
      for (int i = 0; i < 6; ++i) {
        float aa = sa - dir * (float)i * 0.05f;
        float rr2 = srad + 1.6f * std::sin(sAge * 1.1f + (float)i * 1.3f);
        C.serpX[i] = cc + rr2 * std::cos(aa);
        C.serpY[i] = cc + rr2 * std::sin(aa);
      }
    }
  }
  return C;
}

// ---------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------
// Everything that crosses the sky, resolved once per frame. Same idiom as
// the offshore cast: each traveller gets an epoch, a hash off that epoch
// decides whether it shows up at all and where, and its age within the
// epoch drives it across. Nothing is stored between frames, so this costs
// nothing when nobody is looking and can never drift out of sync.
//
// The one thing worth saying about scale: these are at DIFFERENT HEIGHTS,
// and height is the only depth cue available when there is no ground to
// judge against. So a distant airliner is two pixels and crawls, while a
// balloon a few hundred feet up is six pixels and slides. Getting that
// wrong makes everything look like it is painted on the same pane of glass.
struct PixelviewSkyCast {
  float t = -1e9f;
  static const int kBalloons = 3;
  bool balloonUp[kBalloons];
  float balloonX[kBalloons], balloonY[kBalloons], balloonR[kBalloons];
  uint32_t balloonH[kBalloons];
  static const int kPlanes = 2;
  bool planeUp[kPlanes];
  float planeX[kPlanes], planeY[kPlanes], planeDX[kPlanes], planeDY[kPlanes];
  float planeSize[kPlanes], planeAge[kPlanes];
  bool unicornUp = false;
  float uniX = 0.f, uniY = 0.f, uniDir = 1.f, uniAge = 0.f, uniSize = 1.f;
  bool ufoUp = false;
  float ufoX = 0.f, ufoY = 0.f, ufoR = 0.f, ufoAge = 0.f, ufoTilt = 0.f;
  float ufoSpeed = 0.f, ufoVX = 0.f, ufoVY = 0.f;
  // A long eastern dragon, carried as a chain of body segments so it can
  // undulate. Serpentine, not winged — the body IS the animation.
  static const int kDragonSegs = 16;
  bool dragonUp = false;
  float dragX[kDragonSegs], dragY[kDragonSegs], dragR[kDragonSegs];
  uint32_t dragHue = 0;
  // A rider sitting on a small golden cloud of their own.
  bool riderUp = false;
  float riderX = 0.f, riderY = 0.f, riderDir = 1.f;
  // A witch on a broom, with a companion on the tail.
  bool witchUp = false;
  float witchX = 0.f, witchY = 0.f, witchDir = 1.f, witchAge = 0.f;
  static const int kBirds = 9;
  float birdX[kBirds], birdY[kBirds];
  bool flamingo = false;
};

inline PixelviewSkyCast& pixelviewSkyCast(float animT) {
  static PixelviewSkyCast S;
  if (S.t == animT) return S;
  S.t = animT;
  const float fw = (float)W, fh = (float)H;

  // Balloons drift with the wind and bob. They are the slowest thing up
  // here and the most likely to be on screen — a sky with nothing in it for
  // minutes at a time is just a gradient.
  for (int i = 0; i < PixelviewSkyCast::kBalloons; ++i) {
    float period = 150.f + 40.f * (float)i;
    uint32_t ep = (uint32_t)(animT / period);
    uint32_t hh = hash3(ep, 0xBA1100u + (uint32_t)i, 0x5C1Eu);
    float age = animT - (float)ep * period;
    float cross = period * 0.82f;
    S.balloonUp[i] = ((hh % 3u) != 0u) && age < cross;
    if (S.balloonUp[i]) {
      float p = age / cross;
      float ang = (float)(hh & 1023u) / 1023.f * 6.283f;
      // Squash the heading toward the horizontal — balloons drift sideways
      // far more than they climb — and then NORMALISE it. Un-normalised,
      // a near-vertical heading gave (0, 0.45): the balloon covered less
      // than half the intended distance and so began and ended its crossing
      // inside the disc, appearing and vanishing in mid-air.
      float dirx = std::cos(ang), diry = std::sin(ang) * 0.45f;
      float dlen = std::sqrt(dirx * dirx + diry * diry);
      if (dlen < 0.001f) { dirx = 1.f; diry = 0.f; dlen = 1.f; }
      dirx /= dlen; diry /= dlen;
      // The path has to clear the panel at BOTH ends whatever the lane
      // offset does, so it is measured off the half-diagonal rather than
      // guessed: nothing may wink out where it can be seen.
      float halfDiag = 0.5f * std::sqrt(fw * fw + fh * fh);
      float span = 2.f * (halfDiag + 10.f);
      float lane = ((float)((hh >> 12) & 255u) / 255.f - 0.5f) * fh * 0.7f;
      S.balloonX[i] = fw * 0.5f + dirx * (-span * 0.5f + p * span) - diry * lane;
      S.balloonY[i] = fh * 0.5f + diry * (-span * 0.5f + p * span) + dirx * lane +
                      2.2f * std::sin(animT * 0.30f + (float)i * 2.1f);
      S.balloonR[i] = 2.6f + 1.9f * (float)((hh >> 20) & 15u) / 15.f;
      S.balloonH[i] = hh;
    }
  }

  // Airliners: high, straight, fast, and trailing a contrail that persists
  // behind them and spreads. The contrail is what sells the altitude.
  for (int i = 0; i < PixelviewSkyCast::kPlanes; ++i) {
    float period = 95.f + 37.f * (float)i;
    uint32_t ep = (uint32_t)(animT / period);
    uint32_t hh = hash3(ep, 0x50A12u + (uint32_t)i, 0xA12Cu);
    float age = animT - (float)ep * period;
    float cross = 34.f;
    S.planeUp[i] = ((hh % 5u) < 3u) && age < cross;
    if (S.planeUp[i]) {
      float p = age / cross;
      float ang = (float)(hh & 1023u) / 1023.f * 6.283f;
      float dirx = std::cos(ang), diry = std::sin(ang);
      float span = fw * 1.9f;
      float off = ((float)((hh >> 11) & 255u) / 255.f - 0.5f) * fh * 0.85f;
      S.planeX[i] = fw * 0.5f + dirx * (-span * 0.5f + p * span) - diry * off;
      S.planeY[i] = fh * 0.5f + diry * (-span * 0.5f + p * span) + dirx * off;
      S.planeDX[i] = dirx;
      S.planeDY[i] = diry;
      S.planeSize[i] = ((hh >> 19) & 1u) ? 1.0f : 0.62f;  // near one / far one
      S.planeAge[i] = age;
    }
  }

  // And rarely, a unicorn. It does not fly in a straight line: it canters
  // over an arc with a bit of rise and fall, because the whole point of it
  // is that it is not an aircraft.
  {
    float age = 0.f; uint32_t hh = 0u;
    const float cross = SKY_UNICORN.dwell;
    S.unicornUp = skyFlyerUp(SKY_UNICORN, animT, &age, &hh);
    if (S.unicornUp) {
      float p = age / cross;
      S.uniDir = ((hh >> 5) & 1u) ? 1.f : -1.f;
      float lane = 0.24f + 0.42f * (float)((hh >> 8) & 255u) / 255.f;
      S.uniX = (S.uniDir > 0.f) ? (-8.f + p * (fw + 16.f))
                                : (fw + 8.f - p * (fw + 16.f));
      S.uniY = fh * lane - 9.f * std::sin(p * 3.14159f) +
               1.8f * std::sin(animT * 2.3f);   // the canter
      S.uniSize = 1.f;
      S.uniAge = age;
    }
  }

  // A UFO. The whole trick is that it does not travel like anything else up
  // here: a balloon drifts and a plane commits to a heading, so the saucer
  // HOVERS and then DARTS — holding a spot dead still, then crossing a third
  // of the sky in under a second and stopping dead again. Motion is the
  // characterisation; the shape is almost incidental.
  {
    float age = 0.f; uint32_t hh = 0u;
    const float dwell = SKY_UFO.dwell;
    S.ufoUp = skyFlyerUp(SKY_UFO, animT, &age, &hh);
    if (S.ufoUp) {
      // A chain of waypoints; between them it eases with a very sharp curve,
      // so most of each leg is spent stationary. The FIRST and LAST are off
      // the panel entirely: it has to arrive from somewhere and leave for
      // somewhere. Without that it blinked out of existence in mid-air
      // wherever it happened to be hovering when its dwell ran out.
      const int kLegs = 8;
      const float legDur = dwell / (float)kLegs;
      int leg = std::min((int)(age / legDur), kLegs - 1);
      float lt = (age - (float)leg * legDur) / legDur;
      auto wp = [&](int k, float* wx2, float* wy2) {
        if (k <= 0 || k >= kLegs) {
          // Off-panel, on the far side of the disc — entry and exit.
          uint32_t eh = hash3(hh, (k <= 0) ? 0x1Du : 0u, 0x0FFu);
          float ang = (float)(eh & 1023u) / 1023.f * 6.283f;
          *wx2 = fw * 0.5f + std::cos(ang) * fw * 0.95f;
          *wy2 = fh * 0.5f + std::sin(ang) * fh * 0.95f;
          return;
        }
        uint32_t wh = hash3(hh, (uint32_t)k * 2654435761u, 0x0F0Bu);
        *wx2 = fw * (0.16f + 0.68f * (float)(wh & 1023u) / 1023.f);
        *wy2 = fh * (0.16f + 0.68f * (float)((wh >> 10) & 1023u) / 1023.f);
      };
      float ax, ay, bx2, by2;
      wp(leg, &ax, &ay);
      wp(leg + 1, &bx2, &by2);
      // Hold, snap, hold: a steep smootherstep spends most of the leg
      // parked. The arrival and departure legs get a longer, earlier ramp,
      // because covering that distance in the same snap would put it past
      // the panel in a single frame and look like a glitch rather than a
      // departure.
      bool edgeLeg = (leg == 0) || (leg == kLegs - 1);
      float e = edgeLeg ? std::clamp(lt * 1.25f, 0.f, 1.f)
                        : std::clamp((lt - 0.62f) / 0.16f, 0.f, 1.f);
      e = e * e * e * (e * (e * 6.f - 15.f) + 10.f);
      S.ufoX = ax + (bx2 - ax) * e;
      S.ufoY = ay + (by2 - ay) * e;
      S.ufoR = 3.4f + 1.3f * (float)((hh >> 22) & 7u) / 7.f;
      // It banks into the dart and rights itself again.
      S.ufoTilt = (bx2 - ax) * 0.010f * std::sin(e * 3.14159f);
      // How hard it is moving right now, for the streak it drags behind.
      float de = 30.f * e * (1.f - e) * (e * (1.f - e));   // d/dt of smootherstep
      S.ufoSpeed = std::clamp(de * 2.2f, 0.f, 1.f);
      S.ufoVX = bx2 - ax;
      S.ufoVY = by2 - ay;
      S.ufoAge = age;
    }
  }

  // A dragon, the long eastern kind: it swims through the air rather than
  // flapping, so the body follows the head down a travelling sine and every
  // segment lags the one in front. That lag is the whole creature — give
  // them all the same phase and you get a rigid bar with a face on it.
  {
    float age = 0.f; uint32_t hh = 0u;
    const float cross = SKY_DRAGON.dwell;
    S.dragonUp = skyFlyerUp(SKY_DRAGON, animT, &age, &hh);
    if (S.dragonUp) {
      float p = age / cross;
      float dir = ((hh >> 3) & 1u) ? 1.f : -1.f;
      float lane = 0.22f + 0.5f * (float)((hh >> 8) & 255u) / 255.f;
      float headU = -0.25f + p * 1.5f;                // travel, with margin
      S.dragHue = hh;
      for (int i = 0; i < PixelviewSkyCast::kDragonSegs; ++i) {
        float lag = (float)i * 0.035f;
        float uu = headU - lag;
        float sx2 = (dir > 0.f) ? uu * fw : fw - uu * fw;
        // The body swims: a wave that travels DOWN it, plus a slow overall
        // rise and fall so it is not pinned to one altitude.
        float sy2 = fh * lane
                  + 7.5f * std::sin(uu * 9.0f - animT * 2.1f)
                  + 4.0f * std::sin(uu * 3.1f + animT * 0.5f);
        S.dragX[i] = sx2;
        S.dragY[i] = sy2;
        // Thick at the shoulders, tapering to the tail.
        float t2 = (float)i / (float)(PixelviewSkyCast::kDragonSegs - 1);
        S.dragR[i] = (i == 0) ? 2.5f : 2.15f * (1.f - t2 * 0.78f);
      }
    }
  }

  // A rider, cross-legged on a small golden cloud of their own.
  {
    float age = 0.f; uint32_t hh = 0u;
    const float cross = SKY_RIDER.dwell;
    S.riderUp = skyFlyerUp(SKY_RIDER, animT, &age, &hh);
    if (S.riderUp) {
      float p = age / cross;
      S.riderDir = ((hh >> 4) & 1u) ? 1.f : -1.f;
      float lane = 0.20f + 0.55f * (float)((hh >> 9) & 255u) / 255.f;
      S.riderX = (S.riderDir > 0.f) ? (-10.f + p * (fw + 20.f))
                                    : (fw + 10.f - p * (fw + 20.f));
      S.riderY = fh * lane + 3.0f * std::sin(animT * 1.2f);
    }
  }

  // A witch on a broomstick, with something small riding the tail. She
  // keeps unsociable hours: mostly around dusk and after dark.
  {
    float age = 0.f; uint32_t hh = 0u;
    const float cross = SKY_WITCH.dwell;
    S.witchUp = skyFlyerUp(SKY_WITCH, animT, &age, &hh);
    if (S.witchUp) {
      float p = age / cross;
      S.witchDir = ((hh >> 6) & 1u) ? 1.f : -1.f;
      float lane = 0.18f + 0.5f * (float)((hh >> 11) & 255u) / 255.f;
      S.witchX = (S.witchDir > 0.f) ? (-12.f + p * (fw + 24.f))
                                    : (fw + 12.f - p * (fw + 24.f));
      // She swoops — a long shallow dive and climb across the crossing.
      S.witchY = fh * lane - 10.f * std::sin(p * 3.14159f * 1.5f);
      S.witchAge = age;
    }
  }

  // A skein in proper V FORMATION, not a scatter of dots. Birds fly in a
  // vee because each one rides the vortex off the wingtip ahead, and drawn
  // that way a handful of pixels reads instantly as birds — a random cloud
  // of the same pixels reads as dirt on the panel. Every few passes the
  // skein is flamingos, which are the same shape in a much better colour.
  {
    float lead = animT * 0.30f;
    uint32_t fh2 = hash3((uint32_t)(animT / 210.f), 0xF1A3u, 0x60u);
    S.flamingo = (fh2 % 3u) == 0u;
    // The whole formation crosses on a slow diagonal and wraps.
    float span = fw + 44.f;
    float lx = std::fmod(lead * 7.f, span) - 22.f;
    float ly = fh * (0.30f + 0.16f * std::sin(lead * 0.5f));
    for (int i = 0; i < PixelviewSkyCast::kBirds; ++i) {
      // Alternate sides of the vee, stepping back and out.
      int rank = (i + 1) / 2;
      float side = (i == 0) ? 0.f : ((i & 1) ? -1.f : 1.f);
      float bob = 0.7f * std::sin(animT * 3.4f + (float)i * 0.8f);
      S.birdX[i] = lx - (float)rank * 2.6f;
      S.birdY[i] = ly + side * (float)rank * 1.9f + bob;
    }
  }
  return S;
}

// The whole sky, for one cell. Returns the colour directly — there is no
// terrain underneath to blend with, so this replaces the palette outright
// the way ALIEN does rather than tinting anything.
inline void pixelviewSkyCell(const World& w, int x, int y, float animT,
                             const Daylight& dl, float& rr, float& gg,
                             float& bb) {
  const float fw = (float)W, fh = (float)H;
  const float fx = (float)x, fy = (float)y;
  float br = displayBrightness();

  // ---- The air itself ----
  // Looking up, the deepest colour is at the zenith and it pales toward the
  // horizon all round, because you are looking through more atmosphere at a
  // shallower angle. On a round panel that is a radial ramp, which happily
  // is also the shape of the panel.
  float dxc = (fx - fw * 0.5f) / (fw * 0.5f);
  float dyc = (fy - fh * 0.5f) / (fh * 0.5f);
  float rad = std::sqrt(dxc * dxc + dyc * dyc);
  float horizon = std::clamp(rad, 0.f, 1.f);
  horizon = horizon * horizon;

  // Day colour, dusk colour, night colour — picked by the same daylight the
  // rest of the vat runs on, so the sky agrees with every other biome about
  // what time it is.
  float lv = std::clamp(dl.level, 0.f, 1.f);
  float warm = dl.warm > 0.f ? dl.warm : 0.f;

  float zr = 38.f, zg = 96.f, zb = 176.f;     // zenith, noon
  float hr = 152.f, hg = 196.f, hb = 232.f;   // horizon haze, noon
  // Golden hour drags the horizon through amber and the zenith through rose.
  zr += 44.f * warm; zg += 2.f * warm;  zb -= 34.f * warm;
  hr += 74.f * warm; hg += 6.f * warm;  hb -= 96.f * warm;
  // Night: deep blue, not black — a night sky still reads as sky.
  float nightMix = 1.f - lv;
  zr = zr * (1.f - nightMix) + 6.f * nightMix;
  zg = zg * (1.f - nightMix) + 10.f * nightMix;
  zb = zb * (1.f - nightMix) + 34.f * nightMix;
  hr = hr * (1.f - nightMix) + 16.f * nightMix;
  hg = hg * (1.f - nightMix) + 22.f * nightMix;
  hb = hb * (1.f - nightMix) + 52.f * nightMix;

  rr = zr + (hr - zr) * horizon;
  gg = zg + (hg - zg) * horizon;
  bb = zb + (hb - zb) * horizon;

  // ---- Stars ----
  if (lv < 0.45f) {
    float sNight = (0.45f - lv) / 0.45f;
    uint32_t sh = hash3((uint32_t)x, (uint32_t)y, w.worldSeed ^ 0x57A25u);
    if ((sh % 190u) == 0u) {
      float tw = 0.55f + 0.45f * std::sin(animT * 1.7f + (float)(sh & 63u));
      float p = sNight * tw * br;
      rr += 190.f * p; gg += 200.f * p; bb += 225.f * p;
    }
  }

  // ---- Cloud layers ----
  // Three decks at different heights, drifting at different speeds. The
  // parallax between them is the entire illusion of depth: one layer of
  // noise, however pretty, reads as a texture on a flat wall.
  float wx = (w.wind.dx == 0 && w.wind.dy == 0) ? 0.55f : (float)w.wind.dx;
  float wy = (float)w.wind.dy;
  float wind = 0.85f + 0.35f * (float)w.wind.strength;

  // Coverage is per-deck, but what you SEE is the union of three, and the
  // union of three half-covered decks is a 90% overcast. Each one has to be
  // sparse for the sky to stay a sky: at 0.24/0.26/0.18 roughly half the
  // panel is open air, which is what lets the blue read and gives the
  // traffic something to cross.
  // `stretch` elongates the deck ALONG the wind: cirrus is combed out into
  // long streaks by the fast air it sits in, while the low cumulus is much
  // closer to round.
  struct Deck { float scale, speed, cover, soft, lit, stretch; };
  static const Deck decks[3] = {
      // High cirrus: thin, fast, wispy, barely there.
      {30.f, 1.55f, 0.36f, 0.30f, 1.06f, 3.4f},
      // Mid deck: the classic fair-weather cumulus field.
      {19.f, 0.85f, 0.37f, 0.17f, 1.00f, 1.9f},
      // Low and close: big, slow, and it passes right over you.
      {11.f, 0.42f, 0.28f, 0.12f, 0.94f, 1.35f},
  };

  // The wind's frame — everything below is sampled along/across it.
  float wlen = std::sqrt(wx * wx + wy * wy);
  if (wlen < 0.001f) { wx = 1.f; wy = 0.f; wlen = 1.f; }
  float ca = wx / wlen, sa = wy / wlen;

  // The sun's azimuth swings through the day, so the relief on the clouds
  // turns with it: side-lit at dawn and dusk, when a sky looks best.
  float sunA = 3.14159f * (0.25f + 0.5f * std::clamp(lv, 0.f, 1.f)) +
               (warm > 0.f ? 0.6f : 0.f);
  float sunX = std::cos(sunA), sunY = std::sin(sunA);

  for (int L = 0; L < 3; ++L) {
    const Deck& D = decks[L];
    // Drift: this deck's own speed. The parallax between the three is the
    // depth cue, so they must not share a rate.
    float drift = animT * D.speed * wind * 2.4f * wlen;
    float px2 = (float)x - ca * drift;
    float py2 = (float)y - sa * drift;
    // Into the wind's frame, then STRETCHED along it. Wind shears a cloud
    // out in the direction it is pushing, and that elongation is most of
    // what makes even a still frame look like it is being blown.
    float u = px2 * ca + py2 * sa;
    float v = -px2 * sa + py2 * ca;
    float stretch = D.stretch * (1.f + 0.45f * (float)w.wind.strength);
    uint32_t salt = w.worldSeed ^ (0x51000u + (uint32_t)L * 0x77u);
    auto field = [&](float uu, float vv) {
      return pixelviewCloudF(uu / stretch, vv, D.scale, salt);
    };
    float n = field(u, v);

    // The world's own weather opens and closes the sky, so an overcast in
    // the sim is an overcast up here too.
    float cover = D.cover + 0.20f * w.weather.rainStrength +
                  0.05f * std::sin(animT * 0.033f + (float)L);
    float thr = 1.f - cover;
    float a = std::clamp((n - thr) / std::max(0.04f, D.soft), 0.f, 1.f);
    if (a <= 0.f) continue;
    a = a * a * (3.f - 2.f * a);

    // DEPTH, not merely coverage. How far past the threshold a cell sits
    // stands in for how much cloud is stacked above it, so a cumulus has a
    // solid core and a thin edge you can nearly see through. Flat opacity
    // is what made them read as paper cut-outs.
    float depth = std::clamp((n - thr) / 0.22f, 0.f, 1.f);

    // Shading from the real GRADIENT of the field, lit from the sun's
    // azimuth, instead of one offset sample. Where the cloud rises toward
    // the sun it catches light; the lee side falls into its own shadow.
    const float e2 = 1.6f;
    float gu = field(u + e2, v) - field(u - e2, v);
    float gv = field(u, v + e2) - field(u, v - e2);
    float gx2 = gu * ca - gv * sa;      // gradient back into world axes
    float gy2 = gu * sa + gv * ca;
    float lit = std::clamp(0.5f + (gx2 * sunX + gy2 * sunY) * 7.0f, 0.f, 1.f);

    // From below you are looking at undersides, so the base sits in shade
    // and only the shoulders take the sun. A cloud's shadow is lit by the
    // sky, so it is never neutral grey and never black.
    float body = 132.f + (252.f - 132.f) * (0.30f + 0.70f * lit);
    body *= 0.80f + 0.20f * depth;      // thin edges are translucent

    float cr = body * D.lit, cg = body * D.lit, cb = body * D.lit;
    cr += (-14.f + 40.f * lit) * (0.35f + warm);
    cg += ( -9.f + 24.f * lit) * (0.35f + warm);
    cb += ( 20.f - 14.f * lit);
    // Night: clouds are darker than the sky they cross, not brighter.
    cr = cr * lv + (cr * 0.10f + 14.f) * (1.f - lv);
    cg = cg * lv + (cg * 0.10f + 17.f) * (1.f - lv);
    cb = cb * lv + (cb * 0.12f + 30.f) * (1.f - lv);

    // Thin edges blend less, so cloud fades into sky instead of ending on
    // a hard line.
    float alpha = a * (0.55f + 0.45f * depth);
    rr += (cr - rr) * alpha;
    gg += (cg - gg) * alpha;
    bb += (cb - bb) * alpha;
  }

  // ---- The wind made visible ----
  // Long faint streaks combed out along the wind, drifting faster than any
  // deck. Alone they are nearly invisible; what they do is give the eye
  // something unambiguously moving and unambiguously DIRECTIONAL, so the
  // clouds read as being pushed rather than as a texture that slides.
  {
    float sp = 5.5f * (0.6f + 0.5f * (float)w.wind.strength);
    float pu = ((float)x * ca + (float)y * sa) - animT * sp;
    float pv = -(float)x * sa + (float)y * ca;
    float s = pixelviewValueNoiseF(pu / 9.0f, pv, 15.f, w.worldSeed ^ 0x71DDu);
    if (s > 0.80f) {
      float k = (s - 0.80f) / 0.20f * 0.15f * (0.4f + 0.6f * lv);
      rr += (250.f - rr) * k;
      gg += (252.f - gg) * k;
      bb += (255.f - bb) * k;
    }
  }

  // ---- Traffic ----
  const PixelviewSkyCast& S = pixelviewSkyCast(animT);
  auto paint = [&](float pr, float pg, float pb, float amt) {
    amt = std::clamp(amt, 0.f, 1.f);
    rr += (pr * br - rr) * amt;
    gg += (pg * br - gg) * amt;
    bb += (pb * br - bb) * amt;
  };

  // Contrails first: everything else flies in front of them.
  for (int i = 0; i < PixelviewSkyCast::kPlanes; ++i) {
    if (!S.planeUp[i] || S.planeSize[i] < 0.8f) continue;
    float relx = fx - S.planeX[i], rely = fy - S.planeY[i];
    float along = -(relx * S.planeDX[i] + rely * S.planeDY[i]);   // behind
    float across = relx * S.planeDY[i] - rely * S.planeDX[i];
    // Length scales with the WORLD, and stays a fraction of it. A flat 130
    // cells was written against a 200-cell world; on the 140-cell panel a
    // vertically-travelling plane drew an unbroken line from one edge of the
    // disc to the other, which reads as a scratch — or a river in the sky.
    const float trailLen = (float)W * 0.26f;
    if (along < 1.f || along > trailLen) continue;
    float spread = 0.30f + along * (2.2f / trailLen);   // widens as it ages
    float fade = std::clamp(1.f - along / trailLen, 0.f, 1.f);
    float core = std::clamp(1.f - std::fabs(across) / spread, 0.f, 1.f);
    if (core <= 0.f) continue;
    // It breaks up downwind rather than ending in a clean line.
    uint32_t ch = hash3((uint32_t)x, (uint32_t)y, 0xC047A11u);
    float ragged = 0.55f + 0.45f * (float)(ch & 255u) / 255.f;
    paint(238.f, 244.f, 250.f, core * fade * fade * 0.55f * ragged);
  }

  // Birds: seen from below they are silhouettes, but a hard near-black
  // pixel on bright sky is indistinguishable from a dead LED, so they stay
  // soft-edged and only part-opaque — far more like a distant bird than a
  // full-strength dot ever looked.
  for (int i = 0; i < PixelviewSkyCast::kBirds; ++i) {
    float dbx = fx - S.birdX[i], dby = fy - S.birdY[i];
    // Wings, not dots: the beat opens and closes the span, so the shape
    // stretches sideways and snaps back. Each bird is offset in the cycle,
    // because a formation beating in unison looks mechanical.
    float flap = std::sin(animT * 6.5f + (float)i * 0.9f);
    float span = 1.05f + 0.85f * std::fabs(flap);
    float u2 = dbx / span, v2 = dby / 0.55f;
    float d2b = u2 * u2 + v2 * v2;
    if (d2b < 1.f) {
      float soft = std::clamp(1.f - d2b, 0.f, 1.f);
      if (S.flamingo) {
        // Flamingos: pink, and paler where the light comes through the
        // flight feathers, with black tips.
        float edge = std::clamp(std::fabs(u2), 0.f, 1.f);
        if (edge > 0.72f) paint(48.f, 34.f, 44.f, 0.85f * soft);        // tips
        else paint(246.f - 30.f * edge, 128.f + 26.f * edge,
                   156.f + 20.f * edge, 0.92f * soft);
      } else {
        float lit2 = 0.5f + 0.5f * flap;
        paint(78.f + 40.f * lit2, 84.f + 40.f * lit2, 104.f + 40.f * lit2,
              0.72f * soft);
      }
    }
  }

  // Balloons: envelope, banded; a basket under it.
  for (int i = 0; i < PixelviewSkyCast::kBalloons; ++i) {
    if (!S.balloonUp[i]) continue;
    float R = S.balloonR[i];
    float dbx = fx - S.balloonX[i], dby = fy - S.balloonY[i];
    // Envelope: a touch taller than wide, and narrowing at the bottom.
    float v = dby / (R * 1.15f);
    float taper = 1.f - 0.45f * std::max(0.f, v);
    float u = dbx / std::max(0.25f, R * taper);
    float d2 = u * u + v * v;
    if (d2 < 1.f) {
      uint32_t bh = S.balloonH[i];
      // Gore angle and height band — the two coordinates any balloon
      // pattern is actually painted in.
      float goreF = std::atan2(dbx, -dby) * 2.6f;
      int gore = (int)std::floor(goreF) + 8;
      float band = v * 2.4f;                       // 0 at the crown, down
      float shade = 0.72f + 0.28f * std::clamp(1.f - d2 * 0.9f, 0.f, 1.f);
      shade *= 0.80f + 0.20f * (0.5f - u * 0.5f);  // lit from one side

      static const uint8_t kEnv[8][6] = {
          {228, 64, 72,  248, 196, 92},    // red / gold
          {60, 128, 208, 246, 246, 238},   // blue / white
          {236, 132, 48, 250, 224, 120},   // orange / cream
          {96, 176, 96, 240, 236, 176},    // green / straw
          {186, 92, 196, 250, 214, 236},   // violet / pink
          {40, 148, 156, 244, 236, 200},   // teal / bone
          {232, 88, 140, 252, 236, 244},   // rose / shell
          {250, 206, 64, 70, 74, 96},      // gold / slate
      };
      static const uint8_t kRainbow[6][3] = {
          {232, 66, 62}, {244, 148, 52}, {248, 218, 74},
          {104, 196, 104}, {74, 150, 232}, {172, 104, 216}};

      // Every balloon gets its OWN pattern, not just its own two colours —
      // an envelope is a made object and no two crews build the same one.
      // Five ways of dividing the same envelope: vertical gores, horizontal
      // bands, a chequer of the two, a plain cap over a band, and rainbow.
      float pr, pg, pb;
      uint32_t style = (bh >> 3) % 5u;
      const uint8_t* pal = kEnv[bh % 8u];
      if (style == 4u) {
        // Rainbow: the bands run round the envelope, warm at the crown.
        int k = (int)std::floor(band + 0.5f);
        k = (k % 6 + 6) % 6;
        pr = kRainbow[k][0]; pg = kRainbow[k][1]; pb = kRainbow[k][2];
      } else {
        int pick;
        if (style == 0u)      pick = gore & 1;                    // gores
        else if (style == 1u) pick = ((int)std::floor(band) & 1);  // bands
        else if (style == 2u) pick = (gore ^ (int)std::floor(band)) & 1;  // chequer
        else                  pick = (band < -0.35f) ? 0 : 1;      // capped
        const uint8_t* c2 = pal + (pick ? 3 : 0);
        pr = c2[0]; pg = c2[1]; pb = c2[2];
      }
      paint(pr * shade, pg * shade, pb * shade, 1.f);
    } else {
      // Basket, hanging below on its lines.
      float bx2 = dbx, by2 = dby - R * 1.55f;
      if (std::fabs(bx2) < R * 0.28f && std::fabs(by2) < R * 0.22f)
        paint(122.f, 82.f, 46.f, 1.f);
      else if (std::fabs(bx2) < R * 0.62f && by2 > -R * 0.30f &&
               by2 < R * 0.02f && ((x + y) & 1))
        paint(150.f, 130.f, 96.f, 0.5f);      // the rigging, suggested
    }
  }

  // Airliners: a fuselage and a wing, aligned to travel.
  for (int i = 0; i < PixelviewSkyCast::kPlanes; ++i) {
    if (!S.planeUp[i]) continue;
    float sz = S.planeSize[i];
    float relx = fx - S.planeX[i], rely = fy - S.planeY[i];
    float along = relx * S.planeDX[i] + rely * S.planeDY[i];
    float across = relx * S.planeDY[i] - rely * S.planeDX[i];
    bool body = std::fabs(along) < 2.2f * sz && std::fabs(across) < 0.62f * sz;
    bool wing = std::fabs(along) < 0.72f * sz && std::fabs(across) < 1.9f * sz;
    bool tail = along < -1.3f * sz && along > -2.3f * sz &&
                std::fabs(across) < 1.15f * sz;
    if (body || wing || tail) {
      float lit = 0.86f + 0.14f * std::clamp(across, -1.f, 1.f);
      paint(226.f * lit, 230.f * lit, 238.f * lit, 1.f);
    } else if (lv < 0.4f) {
      // Nav lights, at night: red to port, green to starboard, and a
      // strobe. At this scale they are most of what you see.
      float bl = (std::sin(animT * 3.4f) > 0.72f) ? 1.f : 0.25f;
      if (std::fabs(along) < 0.7f * sz && std::fabs(across - 1.9f * sz) < 0.8f)
        paint(60.f, 240.f, 90.f, 0.9f * bl);
      if (std::fabs(along) < 0.7f * sz && std::fabs(across + 1.9f * sz) < 0.8f)
        paint(250.f, 60.f, 55.f, 0.9f * bl);
    }
  }

  // The dragon. Drawn tail-first so the head lands on top of the neck.
  if (S.dragonUp) {
    static const uint8_t kScale[3][6] = {
        { 62, 168, 108,  180, 246, 176},   // jade / pale green
        {188,  56,  62,  250, 206, 120},   // crimson / gold
        { 78, 126, 214,  198, 232, 250},   // lapis / ice
    };
    const uint8_t* pal = kScale[S.dragHue % 3u];
    for (int i = PixelviewSkyCast::kDragonSegs - 1; i >= 0; --i) {
      float ddx = fx - S.dragX[i], ddy = fy - S.dragY[i];
      float R = S.dragR[i];
      float d2 = (ddx * ddx + ddy * ddy) / (R * R);
      if (d2 >= 1.f) continue;
      // Rounded body with a lighter belly ridge along the underside.
      float belly = std::clamp((ddy / R) * 1.4f, -1.f, 1.f);
      float k = std::clamp(0.5f + belly * 0.5f, 0.f, 1.f);
      float shade = 0.80f + 0.20f * (1.f - d2);
      float cr2 = (pal[0] + (pal[3] - pal[0]) * k) * shade;
      float cg2 = (pal[1] + (pal[4] - pal[1]) * k) * shade;
      float cb2 = (pal[2] + (pal[5] - pal[2]) * k) * shade;
      paint(cr2, cg2, cb2, 1.f);
      if (i == 0) {
        // The head: a brighter brow, a hot eye, and a horn swept back.
        if (ddy < -R * 0.25f && std::fabs(ddx) < R * 0.75f)
          paint(cr2 * 1.12f, cg2 * 1.10f, cb2 * 1.06f, 0.8f);
        if (std::fabs(ddx - R * 0.30f) < 0.75f &&
            std::fabs(ddy + R * 0.10f) < 0.75f)
          paint(255.f, 196.f, 70.f, 1.f);
      }
      // A mane runs the first third of the body.
      if (i > 0 && i < 6 && ddy < -R * 0.55f) {
        float m = 0.6f + 0.4f * std::sin(animT * 3.f + (float)i);
        paint(250.f, 232.f, 170.f, 0.55f * m);
      }
    }
    // Whiskers, trailing back from the head.
    {
      float hx2 = S.dragX[0], hy2 = S.dragY[0];
      for (int s2 = -1; s2 <= 1; s2 += 2) {
        for (int t3 = 1; t3 <= 7; ++t3) {
          float wxx = hx2 - (S.dragX[0] - S.dragX[1]) * (float)t3 * 0.55f;
          float wyy = hy2 + (float)s2 * (1.4f + 0.5f * (float)t3) +
                      1.1f * std::sin(animT * 2.6f + (float)t3 * 0.6f);
          if (std::fabs(fx - wxx) < 0.6f && std::fabs(fy - wyy) < 0.6f)
            paint(250.f, 236.f, 186.f, 0.8f);
        }
      }
    }
  }

  // The rider on their own little cloud.
  if (S.riderUp) {
    float d = S.riderDir;
    float rx2 = (fx - S.riderX) * d, ry2 = fy - S.riderY;
    // The cloud: a small flat golden puff.
    float cu = rx2 / 4.2f, cv = (ry2 - 1.4f) / 1.5f;
    if (cu * cu + cv * cv < 1.f) {
      float g2 = 0.82f + 0.18f * std::sin(animT * 2.f + rx2 * 0.5f);
      paint(252.f * g2, 216.f * g2, 96.f * g2, 1.f);
    }
    // The rider: legs crossed, a small body, and a spiked silhouette.
    if (std::fabs(rx2) < 1.3f && ry2 > -1.6f && ry2 < 0.4f)
      paint(238.f, 130.f, 44.f, 1.f);                    // body
    if (std::fabs(rx2) < 1.0f && ry2 > -2.9f && ry2 < -1.5f)
      paint(246.f, 206.f, 160.f, 1.f);                   // head
    // Hair, in spikes.
    for (int k = -2; k <= 2; ++k) {
      float sxk = rx2 - (float)k * 0.85f;
      float syk = ry2 + 3.5f + 0.55f * std::fabs((float)k);
      if (std::fabs(sxk) < 0.55f && std::fabs(syk) < 0.85f)
        paint(46.f, 38.f, 44.f, 1.f);
    }
  }

  // The witch. She reads almost entirely as silhouette — a hat, a hunched
  // figure, a broom line and a streaming cloak — because at this size a
  // shape you recognise beats any amount of detail you cannot see.
  if (S.witchUp) {
    float d = S.witchDir;
    float wx2 = (fx - S.witchX) * d, wy2 = fy - S.witchY;
    const float ink = 34.f, ink2 = 40.f, ink3 = 58.f;
    // Broom handle, angled, with bristles at the back.
    float handle = wy2 - (0.22f * wx2);
    if (wx2 > -6.5f && wx2 < 4.2f && std::fabs(handle - 0.9f) < 0.55f)
      paint(122.f, 84.f, 46.f, 1.f);
    if (wx2 > -9.5f && wx2 <= -6.0f) {
      float spread = 1.5f * (-wx2 - 6.0f) / 3.5f;
      if (std::fabs(handle - 0.9f) < 0.5f + spread)
        paint(186.f, 148.f, 78.f, 0.92f);
    }
    // Figure.
    if (std::fabs(wx2) < 1.25f && wy2 > -2.2f && wy2 < 0.9f)
      paint(ink, ink2, ink3, 1.f);
    if (std::fabs(wx2 - 0.2f) < 0.95f && wy2 > -3.4f && wy2 < -2.0f)
      paint(ink, ink2, ink3, 1.f);                      // head
    // The hat: a brim and a leaning cone.
    if (std::fabs(wx2 - 0.2f) < 2.1f && std::fabs(wy2 + 3.6f) < 0.5f)
      paint(ink, ink2, ink3, 1.f);
    for (int t3 = 0; t3 < 4; ++t3) {
      float hxk = wx2 - 0.1f + (float)t3 * 0.42f;
      float hyk = wy2 + 4.1f + (float)t3 * 0.75f;
      if (std::fabs(hxk) < 0.85f - (float)t3 * 0.16f && std::fabs(hyk) < 0.6f)
        paint(ink, ink2, ink3, 1.f);
    }
    // Cloak, streaming behind and rippling.
    for (int t3 = 1; t3 <= 6; ++t3) {
      float cxk = wx2 + (float)t3 * 1.0f;
      float cyk = wy2 - 0.4f - 0.8f * std::sin((float)t3 * 0.7f - animT * 4.f);
      float thick = 1.15f - (float)t3 * 0.13f;
      if (std::fabs(cxk) < 0.62f && std::fabs(cyk) < thick)
        paint(ink + 14.f, ink2 + 10.f, ink3 + 18.f, 0.92f);
    }
    // And a companion riding the bristles.
    if (std::fabs(wx2 + 7.3f) < 0.85f && std::fabs(handle - 0.1f) < 0.85f)
      paint(28.f, 26.f, 34.f, 1.f);
  }

  // The UFO: a hull with a dome, a ring of running lights, and a beam it
  // occasionally puts down through the cloud below it.
  if (S.ufoUp) {
    float R = S.ufoR;
    float dux = fx - S.ufoX, duy = fy - S.ufoY;
    // Bank: rotate into the direction of travel.
    float ct = std::cos(S.ufoTilt), st = std::sin(S.ufoTilt);
    float hx = dux * ct + duy * st;
    float hy = -dux * st + duy * ct;

    // A streak trailing the dart. At 60fps a craft crossing a third of the
    // panel in a fraction of a second moves further between frames than its
    // own diameter, so without a trail the eye sees it teleport rather than
    // travel — the smear is what makes the speed legible.
    if (S.ufoSpeed > 0.05f) {
      float vl = std::sqrt(S.ufoVX * S.ufoVX + S.ufoVY * S.ufoVY);
      if (vl > 0.001f) {
        float nx = S.ufoVX / vl, ny = S.ufoVY / vl;
        float behind = -(dux * nx + duy * ny);      // along the trail
        float across2 = dux * ny - duy * nx;
        float len = R * 9.f * S.ufoSpeed;
        if (behind > 0.f && behind < len &&
            std::fabs(across2) < R * 0.55f) {
          float k = (1.f - behind / len) * S.ufoSpeed * 0.5f *
                    (1.f - std::fabs(across2) / (R * 0.55f));
          paint(170.f, 226.f, 236.f, k);
        }
      }
    }

    // The beam, first, so the craft paints over it.
    float beamLen = R * 4.2f;
    float beamPhase = std::sin(S.ufoAge * 0.55f);
    if (beamPhase > 0.55f && hy > 0.f && hy < beamLen) {
      float t2 = hy / beamLen;
      float halfW = R * (0.30f + 0.85f * t2);
      if (std::fabs(hx) < halfW) {
        float p = (1.f - t2) * (beamPhase - 0.55f) / 0.45f * 0.42f *
                  (1.f - std::fabs(hx) / halfW);
        paint(190.f, 255.f, 205.f, p);
      }
    }

    // Hull: a flattened ellipse.
    float hu = hx / (R * 1.9f), hv = hy / (R * 0.52f);
    bool hull = (hu * hu + hv * hv) < 1.f;
    // Dome on top.
    float du2 = hx / (R * 0.80f), dv2 = (hy + R * 0.42f) / (R * 0.62f);
    bool dome = (du2 * du2 + dv2 * dv2) < 1.f && hy < R * 0.1f;

    if (dome) {
      float g2 = 0.55f + 0.45f * std::sin(S.ufoAge * 2.2f);
      paint(150.f + 60.f * g2, 232.f, 220.f, 1.f);
    } else if (hull) {
      // Brushed metal, lit from above, dark underneath.
      float lit = std::clamp(0.5f - hv * 0.55f, 0.f, 1.f);
      float base = 74.f + 96.f * lit;
      paint(base * 0.92f, base * 0.98f, base * 1.06f, 1.f);
      // Running lights around the rim, chasing.
      if (hv > 0.15f) {
        float ring = std::sin(hx * 1.5f - S.ufoAge * 7.f);
        if (ring > 0.80f) {
          uint32_t lh = (uint32_t)((hx + 64.f) * 0.5f);
          switch (lh % 3u) {
            case 0:  paint(255.f, 90.f, 90.f, 0.95f); break;
            case 1:  paint(120.f, 255.f, 140.f, 0.95f); break;
            default: paint(140.f, 190.f, 255.f, 0.95f); break;
          }
        }
      }
    }
  }

  // The unicorn.
  if (S.unicornUp) {
    float sz = S.uniSize;
    float dux = (fx - S.uniX) * S.uniDir, duy = fy - S.uniY;
    auto blob = [&](float bx2, float by2, float rx2, float ry2) {
      float ux = (dux - bx2) / rx2, uy = (duy - by2) / ry2;
      return ux * ux + uy * uy < 1.f;
    };
    // Legs first (they are behind), then barrel, neck, head, horn, mane.
    float gait = std::sin(animT * 7.0f) * 1.5f;
    bool leg = false;
    for (int L = 0; L < 4; ++L) {
      float lx = (L < 2 ? -1.9f : 1.7f) * sz;
      float sw = ((L & 1) ? gait : -gait) * 0.5f;
      if (blob(lx + sw, 2.3f * sz, 0.55f * sz, 1.5f * sz)) leg = true;
    }
    bool barrel = blob(0.f, 0.f, 3.3f * sz, 1.7f * sz);
    bool neck   = blob(2.6f * sz, -1.9f * sz, 1.1f * sz, 1.7f * sz);
    bool head   = blob(3.7f * sz, -3.3f * sz, 1.4f * sz, 0.95f * sz);
    bool horn   = blob(4.9f * sz, -4.5f * sz, 0.95f * sz, 0.42f * sz);
    bool tail2  = blob(-3.7f * sz, -0.9f * sz, 1.3f * sz, 1.0f * sz);

    if (horn) {
      float shimmer = 0.7f + 0.3f * std::sin(animT * 4.f + fx * 0.4f);
      paint(255.f, 232.f, 150.f * shimmer + 90.f, 1.f);
    } else if (head || neck || barrel || leg) {
      // White, but not flat white: it takes the sky's own light.
      float shade = 0.88f + 0.12f * std::sin(dux * 0.5f + duy * 0.7f);
      paint(248.f * shade, 246.f * shade, 252.f * shade, 1.f);
    } else if (tail2) {
      // Mane and tail: the one place it is allowed to be a rainbow.
      float band = duy * 0.55f + dux * 0.30f + animT * 1.1f;
      int k = ((int)std::floor(band) % 6 + 6) % 6;
      static const uint8_t kMane[6][3] = {
          {236, 82, 96}, {246, 158, 70}, {248, 226, 96},
          {110, 208, 122}, {96, 168, 240}, {186, 122, 226}};
      paint((float)kMane[k][0], (float)kMane[k][1], (float)kMane[k][2], 0.95f);
    }
    // A few sparkles trailing off it.
    uint32_t gh = hash3((uint32_t)(x + (int)(animT * 3.f)), (uint32_t)y,
                        0x5A2C1Eu);
    if (dux < 0.f && dux > -14.f * sz && std::fabs(duy) < 5.f * sz &&
        (gh % 60u) == 0u) {
      float tw = 0.5f + 0.5f * std::sin(animT * 5.f + (float)(gh & 31u));
      paint(255.f, 246.f, 210.f, 0.75f * tw);
    }
  }
}

// ---------------------------------------------------------------------
// City
// ---------------------------------------------------------------------

// Where the harbour is, and which way it runs. Derived once per world from
// the water itself (second moments -> principal axis) so the boats know
// where to sail without the renderer being told anything about worldgen.
struct CityHarbour {
  uint32_t seed = 0xFFFFFFFFu;
  bool valid = false;
  float cx = 0.f, cy = 0.f;    // centroid of the water
  float ax = 1.f, ay = 0.f;    // long axis, unit
  float halfLen = 0.f;         // extent along the long axis
  float halfWid = 0.f;
  int berthN = 0;              // moorings along the quays
  float berthX[6] = {0}, berthY[6] = {0};
};

inline const CityHarbour& pixelviewHarbour(const World& w) {
  // Two slots: a voyage crosses from one world to the next, and both are
  // rendered on the same frame.
  static CityHarbour slot[2];
  static int next = 0;
  for (int i = 0; i < 2; ++i)
    if (slot[i].seed == w.worldSeed) return slot[i];

  CityHarbour h;
  h.seed = w.worldSeed;
  double sx = 0, sy = 0, n = 0;
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
    if (w.water[y][x] >= 3) { sx += x; sy += y; n += 1; }
  if (n > 40) {
    h.valid = true;
    h.cx = (float)(sx / n); h.cy = (float)(sy / n);
    double mxx = 0, myy = 0, mxy = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
      if (w.water[y][x] < 3) continue;
      double dx = x - h.cx, dy = y - h.cy;
      mxx += dx * dx; myy += dy * dy; mxy += dx * dy;
    }
    mxx /= n; myy /= n; mxy /= n;
    double th = 0.5 * std::atan2(2.0 * mxy, mxx - myy);
    h.ax = (float)std::cos(th); h.ay = (float)std::sin(th);
    double e1 = 0.5 * (mxx + myy) + 0.5 * std::sqrt((mxx - myy) * (mxx - myy) + 4 * mxy * mxy);
    double e2 = 0.5 * (mxx + myy) - 0.5 * std::sqrt((mxx - myy) * (mxx - myy) + 4 * mxy * mxy);
    h.halfLen = (float)(2.0 * std::sqrt(std::max(1.0, e1)));
    h.halfWid = (float)(2.0 * std::sqrt(std::max(1.0, e2)));
    // Moorings: deep-ish water that still has a quay within reach.
    for (int tries = 0; tries < 4000 && h.berthN < 6; ++tries) {
      uint32_t q = hash3((uint32_t)tries, w.worldSeed, 0xB0A7u);
      int x = (int)(q % (uint32_t)W), y = (int)((q >> 11) % (uint32_t)H);
      if (w.water[y][x] < 2 || w.water[y][x] > 4) continue;
      bool nearQuay = false;
      for (int oy = -2; oy <= 2 && !nearQuay; ++oy)
        for (int ox = -2; ox <= 2; ++ox) {
          int nx = x + ox, ny = y + oy;
          if (inBounds(nx, ny) && w.water[ny][nx] == 0) { nearQuay = true; break; }
        }
      if (!nearQuay) continue;
      bool tooClose = false;
      for (int i = 0; i < h.berthN; ++i) {
        float dx = h.berthX[i] - (float)x, dy = h.berthY[i] - (float)y;
        if (dx * dx + dy * dy < 100.f) { tooClose = true; break; }
      }
      if (tooClose) continue;
      h.berthX[h.berthN] = (float)x; h.berthY[h.berthN] = (float)y;
      ++h.berthN;
    }
  }
  slot[next] = h;
  next ^= 1;
  return slot[next ^ 1];
}

// The harbour traffic, positioned once per frame: a ferry working her route,
// an occasional freighter standing in or out, and the moored boats nodding
// at their berths.
struct CityBoats {
  float t = -1e9f;
  uint32_t seed = 0;
  bool ferryUp = false;
  float ferX = 0, ferY = 0, ferDx = 1, ferDy = 0;
  bool shipUp = false;
  float shpX = 0, shpY = 0, shpDx = 1, shpDy = 0;
  int mooredN = 0;
  float mooX[6] = {0}, mooY[6] = {0};
};

inline const CityBoats& pixelviewCityBoats(const World& w, float animT) {
  static CityBoats B;
  const CityHarbour& h = pixelviewHarbour(w);
  if (B.t == animT && B.seed == w.worldSeed) return B;
  B.t = animT; B.seed = w.worldSeed;
  B.ferryUp = B.shipUp = false; B.mooredN = 0;
  if (!h.valid) return B;

  // Ferry: a shuttle up and down the harbour, turning at each end.
  {
    const float period = 84.f;
    float ph = std::fmod(animT, period) / period;      // 0..1
    float tri = (ph < 0.5f) ? (ph * 2.f) : (2.f - ph * 2.f);   // 0..1..0
    float dir = (ph < 0.5f) ? 1.f : -1.f;
    float s = (tri * 2.f - 1.f) * h.halfLen * 0.86f;
    float cross = 0.35f * h.halfWid * std::sin(animT * 0.05f);
    B.ferX = h.cx + h.ax * s - h.ay * cross;
    B.ferY = h.cy + h.ay * s + h.ax * cross;
    B.ferDx = h.ax * dir; B.ferDy = h.ay * dir;
    B.ferryUp = true;
  }
  // Freighter: in on one epoch, gone the next.
  {
    uint32_t ep = (uint32_t)(animT / 200.f);
    uint32_t hh = hash3(ep, w.worldSeed, 0x5417u);
    float age = animT - (float)ep * 200.f;
    if ((hh % 3u) == 0u && age < 150.f) {
      float dir = (hh & 0x10000u) ? 1.f : -1.f;
      float s = (age / 150.f * 2.f - 1.f) * h.halfLen * 1.25f * dir;
      float cross = ((float)((hh >> 4) & 255u) / 255.f - 0.5f) * h.halfWid * 0.7f;
      B.shpX = h.cx + h.ax * s - h.ay * cross;
      B.shpY = h.cy + h.ay * s + h.ax * cross;
      B.shpDx = h.ax * dir; B.shpDy = h.ay * dir;
      B.shipUp = true;
    }
  }
  // Moored boats nod on the swell.
  for (int i = 0; i < h.berthN; ++i) {
    float bob = 0.45f * std::sin(animT * 0.9f + (float)i * 1.7f);
    B.mooX[i] = h.berthX[i] + bob * 0.4f;
    B.mooY[i] = h.berthY[i] + bob;
    ++B.mooredN;
  }
  return B;
}

// ---- Traffic ----
// Cars are agents, not a pattern. The first version derived them per cell
// from a closed form (lane parity, phase, speed): that could not turn, could
// not vary within a lane, and vanished wherever a cell had road on both axes
// — which is every intersection AND every cell of a three-wide avenue. So
// they drive now: each car holds a position and a heading, follows the road,
// picks a way at junctions, and gets rasterised into an occupancy grid once
// per frame so the per-cell lookup stays O(1).
struct CityCar {
  float x = 0.f, y = 0.f;
  int8_t dx = 1, dy = 0;
  float speed = 6.f;
  float wait = 0.f;      // held at a junction
  uint8_t hue = 0;
};
struct CityTraffic {
  float t = -1e9f;
  uint32_t seed = 0xFFFFFFFFu;
  std::vector<CityCar> cars;
  std::vector<uint8_t> cell;   // 0 empty, else 1 + car index (capped)
  std::vector<uint8_t> dirOf;  // 0 none, 1 = heading +, 2 = heading -
};

inline bool pixelviewIsRoad(const World& w, int x, int y) {
  if (!inBounds(x, y)) return false;
  char c = w.terrain[y][x];
  return c == CITY_ROAD || c == CITY_BRIDGE;
}

inline const CityTraffic& pixelviewTraffic(const World& w, float animT) {
  static CityTraffic slot[2];
  static int rr2 = 0;
  CityTraffic* T = nullptr;
  for (int i = 0; i < 2; ++i) if (slot[i].seed == w.worldSeed) T = &slot[i];
  if (!T) { T = &slot[rr2]; rr2 ^= 1; *T = CityTraffic{}; T->seed = w.worldSeed; }
  if (T->t == animT) return *T;

  float dt = (T->t < -1e8f) ? 0.f : std::min(0.35f, animT - T->t);
  bool first = T->cars.empty();
  T->t = animT;
  if (T->cell.empty()) { T->cell.assign((size_t)W * H, 0); T->dirOf.assign((size_t)W * H, 0); }

  uint32_t rs = w.worldSeed ^ 0x7A4F1Cu;
  auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; };

  if (first) {
    int want = std::max(8, (W * H) / 620);
    for (int tries = 0; tries < want * 60 && (int)T->cars.size() < want; ++tries) {
      int x = (int)(rnd() % (uint32_t)W), y = (int)(rnd() % (uint32_t)H);
      if (!pixelviewIsRoad(w, x, y)) continue;
      // A heading the road actually goes in.
      const int dxs[4] = {1, -1, 0, 0}, dys[4] = {0, 0, 1, -1};
      int pick = -1;
      for (int k = 0; k < 4; ++k) {
        int kk = (int)((rnd() + (uint32_t)k) % 4u);
        if (pixelviewIsRoad(w, x + dxs[kk], y + dys[kk])) { pick = kk; break; }
      }
      if (pick < 0) continue;
      CityCar c;
      c.x = (float)x + 0.5f; c.y = (float)y + 0.5f;
      c.dx = (int8_t)dxs[pick]; c.dy = (int8_t)dys[pick];
      c.speed = 3.2f + (float)(rnd() % 1000u) * 0.0075f;   // 3.2 .. 10.7 cells/s
      c.hue = (uint8_t)(rnd() & 255u);
      T->cars.push_back(c);
    }
  }

  std::fill(T->cell.begin(), T->cell.end(), (uint8_t)0);
  std::fill(T->dirOf.begin(), T->dirOf.end(), (uint8_t)0);

  for (size_t i = 0; i < T->cars.size(); ++i) {
    CityCar& c = T->cars[i];
    if (c.wait > 0.f) {
      c.wait -= dt;
    } else {
      int cx = (int)c.x, cy = (int)c.y;
      float step = c.speed * dt;
      // Advance in short hops so a fast car cannot tunnel through a junction.
      while (step > 0.f) {
        float hop = std::min(step, 0.45f);
        step -= hop;
        c.x += (float)c.dx * hop;
        c.y += (float)c.dy * hop;
        int nx = (int)c.x, ny = (int)c.y;
        if (nx == cx && ny == cy) continue;
        cx = nx; cy = ny;
        // Entered a new cell: is there still road ahead, and is this a junction?
        bool ahead = pixelviewIsRoad(w, nx + c.dx, ny + c.dy);
        int lx = -c.dy, ly = c.dx;                  // the two perpendiculars
        bool left = pixelviewIsRoad(w, nx + lx, ny + ly);
        bool right = pixelviewIsRoad(w, nx - lx, ny - ly);
        bool junction = (left || right);
        uint32_t roll = rnd();
        if (!ahead || (junction && (roll % 100u) < 16u)) {
          // Turn. Prefer a turn that exists; if the road dead-ends, come about.
          bool goLeft = left && (!right || (roll & 0x100u));
          if (goLeft)       { c.dx = (int8_t)lx; c.dy = (int8_t)ly; }
          else if (right)   { c.dx = (int8_t)-lx; c.dy = (int8_t)-ly; }
          else if (!ahead)  { c.dx = (int8_t)-c.dx; c.dy = (int8_t)-c.dy; }
          c.x = (float)nx + 0.5f; c.y = (float)ny + 0.5f;
          if ((roll >> 12) % 5u == 0u) c.wait = 0.25f + (float)((roll >> 16) % 90u) * 0.01f;
          break;
        }
        if (!pixelviewIsRoad(w, nx, ny)) {          // shoved off the network
          c.dx = (int8_t)-c.dx; c.dy = (int8_t)-c.dy;
          c.x = (float)cx + 0.5f; c.y = (float)cy + 0.5f;
          break;
        }
      }
    }
    int ix = std::clamp((int)c.x, 0, W - 1), iy = std::clamp((int)c.y, 0, H - 1);
    T->cell[(size_t)iy * W + ix] = (uint8_t)(1u + (i & 0x7Fu));
    T->dirOf[(size_t)iy * W + ix] = (uint8_t)((c.dx + c.dy > 0) ? 1u : 2u);
  }
  return *T;
}

// ---- Marine life ----
// The sim's own fish are capped for a pond, so an ocean read as empty water.
// Shoals are drawn instead: a handful of schools, each a cloud of fish that
// swims as one body, wheels, and scatters when something big goes past. Same
// trick as the traffic — integrate a few dozen agents, rasterise once per
// frame, keep the per-cell lookup O(1).
struct Shoal {
  float cx = 0.f, cy = 0.f;      // the school's centre
  float hx = 1.f, hy = 0.f;      // heading
  float speed = 2.2f;
  int n = 0;
  float ox[42], oy[42];          // each fish's offset within the school
  float phase[42];
  uint8_t kind = 0;              // 0 sardine, 1 reef fish, 2 ray/turtle
};
struct MarineLife {
  float t = -1e9f;
  uint32_t seed = 0xFFFFFFFFu;
  std::vector<Shoal> shoals;
  std::vector<uint8_t> cell;     // 0 none, else 1+kind
};

inline const MarineLife& pixelviewMarine(const World& w, float animT) {
  static MarineLife slot[2];
  static int rr3 = 0;
  MarineLife* M = nullptr;
  for (int i = 0; i < 2; ++i) if (slot[i].seed == w.worldSeed) M = &slot[i];
  if (!M) { M = &slot[rr3]; rr3 ^= 1; *M = MarineLife{}; M->seed = w.worldSeed; }
  if (M->t == animT) return *M;
  float dt = (M->t < -1e8f) ? 0.f : std::min(0.35f, animT - M->t);
  bool first = M->shoals.empty();
  M->t = animT;
  if (M->cell.empty()) M->cell.assign((size_t)W * H, 0);

  uint32_t rs = w.worldSeed ^ 0xF15Au;
  auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; };

  auto wet = [&](float fx2, float fy2) {
    int xi = (int)fx2, yi = (int)fy2;
    return inBounds(xi, yi) && w.water[yi][xi] > 0;
  };

  if (first) {
    int want = std::max(4, (W * H) / 1500);
    for (int tries = 0; tries < want * 80 && (int)M->shoals.size() < want; ++tries) {
      int x = (int)(rnd() % (uint32_t)W), y = (int)(rnd() % (uint32_t)H);
      if (!inBounds(x, y) || w.water[y][x] < 2) continue;
      Shoal s;
      s.cx = (float)x; s.cy = (float)y;
      float a = (float)(rnd() % 628u) * 0.01f;
      s.hx = std::cos(a); s.hy = std::sin(a);
      s.kind = (uint8_t)(rnd() % 10u < 6u ? 0 : (rnd() % 2u ? 1 : 2));
      s.speed = (s.kind == 2) ? 0.9f : 1.8f + (float)(rnd() % 100u) * 0.02f;
      s.n = (s.kind == 2) ? 1 + (int)(rnd() % 2u)
                          : 14 + (int)(rnd() % 26u);
      float spread = (s.kind == 0) ? 2.6f : 2.0f;
      for (int i = 0; i < s.n; ++i) {
        s.ox[i] = ((float)(rnd() % 1000u) / 500.f - 1.f) * spread;
        s.oy[i] = ((float)(rnd() % 1000u) / 500.f - 1.f) * spread * 0.7f;
        s.phase[i] = (float)(rnd() % 628u) * 0.01f;
      }
      M->shoals.push_back(s);
    }
  }

  std::fill(M->cell.begin(), M->cell.end(), (uint8_t)0);
  for (auto& s : M->shoals) {
    // Wander, and turn away from land before hitting it.
    float turn = 0.35f * std::sin(animT * 0.23f + s.phase[0]);
    float ahead = 3.5f;
    if (!wet(s.cx + s.hx * ahead, s.cy + s.hy * ahead)) turn += 2.2f;
    float ca = std::cos(turn * dt), sa2 = std::sin(turn * dt);
    float nhx = s.hx * ca - s.hy * sa2, nhy = s.hx * sa2 + s.hy * ca;
    s.hx = nhx; s.hy = nhy;
    s.cx += s.hx * s.speed * dt;
    s.cy += s.hy * s.speed * dt;
    // Wrap rather than pile up on a coast.
    if (s.cx < 1.f) s.cx = (float)W - 2.f;
    if (s.cx > (float)W - 1.f) s.cx = 1.f;
    if (s.cy < 1.f) s.cy = (float)H - 2.f;
    if (s.cy > (float)H - 1.f) s.cy = 1.f;

    float px2 = -s.hy, py2 = s.hx;   // the school's own axes
    for (int i = 0; i < s.n; ++i) {
      // Each fish weaves within the body of the school.
      float wob = 0.5f * std::sin(animT * 3.1f + s.phase[i]);
      float ax = s.ox[i] + wob * 0.35f, ay = s.oy[i] + wob;
      float fx2 = s.cx + s.hx * ax + px2 * ay;
      float fy2 = s.cy + s.hy * ax + py2 * ay;
      int xi = (int)fx2, yi = (int)fy2;
      if (!inBounds(xi, yi) || w.water[yi][xi] == 0) continue;
      M->cell[(size_t)yi * W + xi] = (uint8_t)(1u + s.kind);
    }
  }
  return *M;
}

// Roof palette. Seen from above a city is roofs, not facades: tar and
// gravel, painted membrane, rusted steel, weathered copper — with the pastel
// plaster only showing on the parapets that catch the light.
inline void pixelviewCityRoof(uint32_t fam, int storeys, int& r, int& g, int& b) {
  // Painted metal, mostly — the blue and green roofs you see over any
  // Japanese city, with terracotta and mustard between them and only the
  // occasional stretch of plain tar. The city has to carry colour by day,
  // when the neon is off.
  switch (fam % 14u) {
    case 0:  r = 48;  g = 122; b = 178; break;  // cobalt metal
    case 1:  r = 40;  g = 148; b = 132; break;  // sea green metal
    case 2:  r = 196; g = 92;  b = 62;  break;  // terracotta
    case 3:  r = 214; g = 168; b = 58;  break;  // mustard
    case 4:  r = 72;  g = 96;  b = 168; break;  // deep blue
    case 5:  r = 226; g = 118; b = 132; break;  // coral
    case 6:  r = 232; g = 158; b = 120; break;  // peach membrane
    case 7:  r = 238; g = 212; b = 176; break;  // cream
    case 8:  r = 84;  g = 80;  b = 88;  break;  // tar
    case 9:  r = 158; g = 74;  b = 130; break;  // plum
    case 10: r = 96;  g = 186; b = 196; break;  // pale teal
    case 11: r = 190; g = 150; b = 108; break;  // sand
    case 12: r = 130; g = 128; b = 136; break;  // galvanised
    default: r = 208; g = 96;  b = 88;  break;  // red oxide
  }
  // Tall stock reads cooler and more mechanical, but never goes grey.
  float t = std::min(1.f, (float)storeys / 26.f) * 0.38f;
  r = (int)(r * (1.f - t) + 120 * t);
  g = (int)(g * (1.f - t) + 138 * t);
  b = (int)(b * (1.f - t) + 160 * t);
}

// Is this cell part of the same building as its neighbour? Buildings are
// uniform in height, which is what lets the renderer find their outlines.
inline bool pixelviewSameBuilding(const World& w, int x, int y, int nx, int ny) {
  if (!inBounds(nx, ny)) return false;
  if (!isCityBuilding(w.terrain[ny][nx])) return false;
  return w.height[ny][nx] == w.height[y][x];
}

// ---- The thing in the alien world that notices you ----
// Rarely, something very large rises at the edge of the world, looks slowly
// around, and goes back down. It reads as a silhouette — the world dims and
// hazes behind it — and only the eyes are lit, which is what makes it
// unpleasant. Same schedule-by-epoch trick as the whale and the serpent.
struct AlienHead {
  float t = -1e9f;
  bool up = false;
  int side = -1;                 // -1 leans in from the left rim, +1 the right
  float cx = 0.f, cy = 0.f;      // centre of the cranium, in cells
  float rx = 1.f, ry = 1.f;
  float tilt = 0.f;              // head cocked toward the middle
  float rise = 0.f;              // 0 hidden .. 1 fully leaning in
  float gaze = 0.f;              // -1 .. +1, where it is looking
  float blink = 1.f;             // 1 open, 0 shut
  float handX = 0.f, handY = 0.f;  // the hand that holds the rim
  float age = 0.f;
};

inline const AlienHead& pixelviewAlienHead(const World& w, float animT) {
  static AlienHead A;
  if (A.t == animT) return A;
  A.t = animT;
  // Lugia-rare, and the SCHEDULE lives in the core (alienApparition01) so the
  // mod matrix sees the same event this does — see terrarium_core.hpp. Only
  // the geometry is decided here.
  const float kEpoch = ALIEN_APPARITION_EPOCH, kDwell = ALIEN_APPARITION_DWELL;
  uint32_t ep = (uint32_t)(animT / kEpoch);
  uint32_t hh = hash3(ep, w.worldSeed, 0x8EAD5u);
  float age = animT - (float)ep * kEpoch;
  float rise = alienApparition01(w, animT);
  A.up = rise > 0.f;
#ifdef TERRA_FORCE_HEAD
  A.up = true;
  age = std::fmod(animT, kDwell * 2.f) * 0.5f + 6.f;
  rise = 1.f;
#endif
  if (!A.up) return A;
  A.age = age;
  A.rise = rise;

  A.side = ((hh >> 20) & 1u) ? 1 : -1;
  A.rx = (float)W * (0.15f + 0.04f * (float)((hh >> 4) & 15u) / 15.f);
  A.ry = A.rx * 1.35f;
  // It leans in around the rim: off the panel entirely when hidden, head and
  // one shoulder inside the circle when fully out, the rest of it still
  // beyond the edge (the circle mask does the cropping for us).
  float inX  = (float)W * (A.side < 0 ? 0.26f : 0.74f);
  float outX = (A.side < 0) ? -A.rx * 1.6f : (float)W + A.rx * 1.6f;
  A.cx = outX + (inX - outX) * rise;
  A.cy = (float)H * (0.40f + 0.03f * std::sin(age * 0.35f));   // it shifts a little
  A.tilt = (float)-A.side * 0.26f * rise;                      // cocked inward
  // A hand comes round the rim below the head to steady itself.
  {
    float cc = (float)W * 0.5f - 0.5f, R = (float)W * 0.5f;
    float baseAng = (A.side < 0) ? 3.14159f : 0.f;
    float ang = baseAng + (float)-A.side * (0.62f + 0.06f * std::sin(age * 0.5f));
    float rr3 = R * (0.995f - 0.16f * rise);
    A.handX = cc + rr3 * std::cos(ang);
    A.handY = cc + rr3 * std::sin(ang);
  }

  // It looks around in long sweeps, with the odd slow blink.
  A.gaze = std::sin(age * 0.42f) * 0.75f + 0.25f * std::sin(age * 0.17f + 1.3f);
  float bph = std::sin(age * 0.9f + (float)(hh & 31u));
  A.blink = (bph > 0.972f) ? 0.12f : 1.f;
  return A;
}

// ---------------------------------------------------------------------
// Biome identity
// ---------------------------------------------------------------------
// The plant glyphs are shared, so for a long time meadow, wetland, alpine
// and tropical differed only in how MUCH of each grew — four variations on
// one green. Each terrestrial biome now grades its own land: a whole-palette
// tint applied after the per-glyph colour, which keeps the plant families
// while making the biome recognisable from across the room.
//
// (ALIEN and CITY don't come through here — they replace the palette
// outright.)
inline void pixelviewBiomeGrade(Biome bi, int& r, int& g, int& b) {
  float fr = (float)r, fg = (float)g, fb = (float)b;
  switch (bi) {
    case MEADOW:
      // Sunlit hay: warm yellow-greens. The blue used to be pulled right
      // out (x0.76), which is what left bare ground reading as mud under
      // the grass instead of as dry earth standing in the same light.
      fr *= 1.10f; fg *= 1.07f; fb *= 0.86f;
      fr += 6.f; fg += 6.f; fb += 2.f;
      break;
    case WETLAND:
      // Peat and shade: deep blue-greens, everything a stop darker.
      fr *= 0.76f; fg *= 0.98f; fb *= 1.12f;
      fb += 10.f; fg += 4.f;
      break;
    case ALPINE: {
      // Thin air: sage and lichen over blue-grey stone. The blue push was
      // hard enough (x1.14, +12) that scree came out periwinkle on its own,
      // and autumn's warm nudge on top turned the whole mountain lilac.
      float grey = 0.30f * fr + 0.59f * fg + 0.11f * fb;
      fr = fr * 0.58f + grey * 0.42f;
      fg = fg * 0.64f + grey * 0.36f;
      fb = fb * 0.58f + grey * 0.42f;
      fr *= 0.96f; fg *= 1.02f; fb *= 1.05f;
      fb += 5.f;
      break;
    }
    case TROPICAL:
      // Rainforest: saturated jade, dark under the canopy.
      fr *= 0.80f; fg *= 1.10f; fb *= 0.90f;
      fg += 6.f;
      break;
    case DESERT:
      // Everything bakes: ochre and rose, greens only in the cactus. Keeping
      // a little blue in the sand is what stops a whole frame of it reading
      // as one flat sheet of highlighter.
      fr *= 1.08f; fg *= 1.00f; fb *= 0.87f;
      fr += 6.f; fg += 3.f; fb += 2.f;
      break;
    default:
      return;
  }
  r = (int)fr; g = (int)fg; b = (int)fb;
}

// Bare ground differs as much as the planting does: pale tan under a meadow,
// black peat in a bog, grey scree on a mountain.
// Takes x/y as well as the jitter, because the dark wants SHAPE — see the
// tropical case.
inline void pixelviewBiomeSoil(Biome bi, int j, int x, int y, uint32_t seed,
                               int& r, int& g, int& b) {
  switch (bi) {
    case MEADOW:   r = 84 + j / 2; g = 80 + j / 2; b = 52; break;  // dry olive
    case WETLAND:  r = 48 + j / 2; g = 46 + j / 2; b = 38; break;  // peat
    case ALPINE:   r = 92 + j / 2; g = 96 + j / 2; b = 104; break; // scree
    case TROPICAL: {
      // Bare ground is the second most common thing in a tropical frame
      // (670 cells in 12100), and it was ALL near-black loam at luminance
      // 45 with reef water at 130 beside it — which is why the world looked
      // like it had holes punched in it. The dark is worth keeping; what it
      // needed was shape. So the floor runs from sunlit leaf litter to deep
      // shade across a smooth field: the gloom now POOLS under the canopy in
      // patches you can read as depth, instead of being the flat default
      // everywhere the planting happens to thin out.
      float shade = pixelviewFbm2(x, y, 9.0f, seed ^ 0x50113u);
      shade = std::clamp((shade - 0.34f) * 2.1f, 0.f, 1.f);
      r = (int)(38.f + 44.f * shade) + j / 2;
      g = (int)(32.f + 40.f * shade) + j / 2;
      b = (int)(26.f + 22.f * shade);
      break;
    }
    case DESERT:   r = 178 + j / 2; g = 148 + j / 2; b = 104; break;
    default:       r = 26 + j / 2; g = 22 + j / 2; b = 18; break;
  }
}

// ...and so does the water. One blue ramp served every biome; a tannin bog,
// a glacial tarn and a coral shallow are not the same colour of water.
inline void pixelviewBiomeWater(Biome bi, int depth, float& fr, float& fg,
                                float& fb) {
  switch (bi) {
    case WETLAND: {   // tannin: brown-green, and you cannot see into it
      float t = 0.72f;
      fr = fr * (1.f - t) + (44.f + 5.f * (float)depth) * t;
      fg = fg * (1.f - t) + (62.f + 3.f * (float)depth) * t;
      fb = fb * (1.f - t) + (40.f + 2.f * (float)depth) * t;
      break;
    }
    case ALPINE:      // glacial: pale milky cyan in the shallows
      fr *= 0.92f; fg *= 1.14f; fb *= 1.10f;
      fr += 18.f; fg += 26.f; fb += 14.f;
      break;
    case TROPICAL:    // reef turquoise — bright, but it is still water
      fr *= 0.86f; fg *= 1.12f; fb *= 1.04f;
      fg += 8.f;
      break;
    case DESERT:      // an oasis is jade, not ocean
      // Deeper and greener than it was. A light cyan pool against a field of
      // yellow sand is a straight complementary clash at full chroma, which
      // is about the most abrasive thing two colours can do to each other.
      fr *= 0.82f; fg *= 1.06f; fb *= 0.92f;
      fg += 5.f;
      break;
    case MEADOW:      // a clear pond takes the sky
      fg += 6.f; fb += 10.f;
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------
// Palette harmony
// ---------------------------------------------------------------------
// Every colour in this file was authored on its own — a green for grass, a
// blue for water, a red for a fly agaric — and independently chosen colours
// do not add up to a scene. They compete. Three things make them belong to
// one world, applied last so nothing has to know about them:
//
//   CHROMA CEILING. A soft knee that only bites on the loudest cells, so a
//   poppy stays a poppy while the mid-greens stop shouting. A flat
//   saturation multiply pulls the quiet 80% of the frame down with the loud
//   20% and reads as a faded photograph; this leaves it alone.
//
//   SPLIT TONE. Shadows cool, highlights warm. The oldest trick there is for
//   making separately-painted things share a light, and the reason a graded
//   frame reads as air rather than as a chart.
//
//   BLACK LIFT. Nothing in a lit scene is 0,0,0, and saturated colour hard
//   against true black is exactly what makes an edge buzz. Skipped in OLED
//   ground mode, where true black is the entire point of the setting.
//
// `ceilAmt` scales the chroma ceiling alone. WATER passes 0 for it, and that
// is not a special case so much as the whole rule stated once: the ceiling
// exists to tame colours that were each chosen on their own and never
// checked against each other. The water ramp is the opposite of that — it
// was built as one deliberate navy→turquoise scale and tuned against real
// water. Running a ceiling over it desaturated the shallows into flat grey
// sheets in the rivers, which is exactly the disease, not the cure.
inline void pixelviewHarmonise(float& rr, float& gg, float& bb, float amt,
                               float ceilAmt = 1.0f) {
  if (amt <= 0.002f) return;
  float lum = 0.299f * rr + 0.587f * gg + 0.114f * bb;

  float mx = std::max(rr, std::max(gg, bb));
  float mn = std::min(rr, std::min(gg, bb));
  float chroma = mx - mn;
  const float kKnee = 82.f;    // below this a colour is already calm
  if (chroma > kKnee && ceilAmt > 0.002f) {
    // Compress the excess toward a ceiling rather than clipping it, so the
    // ordering of two loud colours survives even as the gap closes.
    float over = chroma - kKnee;
    float want = kKnee + over / (1.f + over / 105.f);
    float k = 1.f - (1.f - want / chroma) * amt * ceilAmt;
    rr = lum + (rr - lum) * k;
    gg = lum + (gg - lum) * k;
    bb = lum + (bb - lum) * k;
  }

  float t = std::clamp(lum * (1.f / 255.f), 0.f, 1.f);
  float shadow = (1.f - t) * (1.f - t);
  float high = t * t;
  rr += (-2.5f * shadow + 6.0f * high) * amt;
  gg += (-0.5f * shadow + 3.5f * high) * amt;
  bb += ( 8.0f * shadow - 5.0f * high) * amt;

  if (displayBgMode() != 1) {
    float lift = shadow * 7.0f * amt;
    rr += lift * 0.85f; gg += lift * 0.90f; bb += lift;
  }
}

// Nearest-neighbour cloud sample (the full app does bilinear; not needed at
// 1px/cell).
inline uint8_t pixelviewCloudAt(const World& w, int x, int y) {
  int cx = ((int)(((float)x / (float)W) * CW + w.clouds.offX)) % CW;
  int cy = ((int)(((float)y / (float)H) * CH + w.clouds.offY)) % CH;
  if (cx < 0) cx += CW;
  if (cy < 0) cy += CH;
  uint8_t c = w.clouds.field[cy * CW + cx];
  return (uint8_t)std::min<int>(255, (int)(c * w.cloudOpacity));
}

// Shaded per-cell color: entities, overlays, water, terrain, then season,
// cloud shadow, lightning, and the day/night cycle.
// animT: seconds for water motion; pass wall-clock time for smooth flow
// between sim ticks (defaults to tick-derived time when negative).
inline PixelviewRGB pixelviewCellColor(const World& w, int x, int y, int tick,
                                       float animT = -1.0f) {
  if (animT < 0.0f) animT = (float)tick * 0.2f;
  // Small stable per-cell jitter so flat areas read as texture, not banding.
  uint32_t h = hash3((uint32_t)x, (uint32_t)y, w.worldSeed);
  int j = (int)(h & 15u) - 8;
  Season season = seasonAt(tick);
  bool liveSnow = (g_weatherMode == 1 && liveWeatherNow().snowing);
  bool snowy = liveSnow ||
               (season == WINTER && w.biome != TROPICAL && w.biome != DESERT);

  int r = 0, g = 0, b = 0;
  bool tintable = false;  // terrain cells take the season grade

  char e = w.entities[y][x];
  char o = w.overlay[y][x];
  uint8_t d = w.water[y][x];
  char t = w.terrain[y][x];

  if (e != ' ') {
    if (isAquatic(e)) {
      // Sea critters: silvery blue-white, so fish read as fish and not
      // as bananas bobbing offshore.
      r = 185; g = 220; b = 250;
    } else {
      // Land critters: warm bright dots so they pop at one pixel.
      r = 255; g = 230; b = 160;
    }
  } else if (o == '|' || o == '/' || o == '\\') {
    if (snowy) { r = 238; g = 242; b = 250; }  // winter rain falls as snow
    else       { r = 150; g = 190; b = 235; }  // rain streak
  } else if (o == '=' || o == '-' || o == '~' || o == '+' || o == '!') {
    // Rainbow bands, in actual rainbow order (was hashed confetti).
    switch (o) {
      case '=': r = 235; g = 70;  b = 60;  break;  // red
      case '-': r = 255; g = 150; b = 45;  break;  // orange
      case '~': r = 255; g = 220; b = 70;  break;  // yellow
      case '+': r = 95;  g = 205; b = 95;  break;  // green
      default:  r = 150; g = 110; b = 235; break;  // violet
    }
  } else if (o != ' ') {
    // Chaos overlays: hue from the glyph itself.
    uint32_t oh = hash3((uint32_t)o, 7u, 77u);
    r = 140 + (int)(oh & 0x7F); g = 140 + (int)((oh >> 7) & 0x7F);
    b = 140 + (int)((oh >> 14) & 0x7F);
  } else if (d > 0) {
    // Layered blues, Surf Sandbox style: pale aqua shallows deepening to
    // rich navy (was inverted — deep water rendered brighter than shallow).
    int dd = std::min<int>(7, d);
    int sh = 7 - dd;  // shallowness 0..6
    r = 10 + sh * 4 + j / 2;
    g = 56 + sh * 14 + j;
    b = 118 + sh * 15 + j;
    {  // each biome's water is its own colour, before any motion is added
      float fr2 = (float)r, fg2 = (float)g, fb2 = (float)b;
      pixelviewBiomeWater(w.biome, dd, fr2, fg2, fb2);
      r = (int)fr2; g = (int)fg2; b = (int)fb2;
    }
    // sparse foam shimmer
    if (((h >> 4) + (uint32_t)(tick / 6)) % 97u == 0u) { r = g = b = 235; }

    // Coral colonies glow through the shallow water.
    if (t == 'C' && d <= 3) {
      switch ((h >> 5) % 4u) {
        case 0:  r = 235; g = 110; b = 140; break;  // pink
        case 1:  r = 240; g = 140; b = 70;  break;  // orange
        case 2:  r = 175; g = 110; b = 220; break;  // violet
        default: r = 245; g = 205; b = 160; break;  // cream
      }
      r -= d * 14; g -= d * 8; b += d * 4;  // seen through the water
    }

    // Water in motion. Rivers (steep gradient) flow in EVERY biome — a
    // wetland bayou still runs even though its ponds sit glassy. Open
    // water gets contour surf except in the stillwater biomes
    // (wetland/desert), where flat water stays calm on purpose.
    {
      int gx = 0, gy = 0;
      if (x > 0 && x < W - 1) gx = (int)w.height[y][x-1] - (int)w.height[y][x+1];
      if (y > 0 && y < H - 1) gy = (int)w.height[y-1][x] - (int)w.height[y+1][x];

      // How enclosed is this water? Counted once, up here, because it
      // decides BOTH whether this is open water at all and, if it is, how
      // hard it shoals. 24 neighbours: near 0 is open sea, near 24 is a
      // ditch.
      int landNear = 0;
      for (int oy = -2; oy <= 2; ++oy)
        for (int ox = -2; ox <= 2; ++ox) {
          if (ox == 0 && oy == 0) continue;
          int nx2 = x + ox, ny2 = y + oy;
          if (nx2 < 0 || ny2 < 0 || nx2 >= W || ny2 >= H) continue;
          if (w.water[ny2][nx2] == 0) ++landNear;
        }

      // A WATERCOURSE IS NOT A COASTLINE. Surf needs a fetch to build over;
      // a channel a few cells wide has none, and it was getting the full
      // breaking-wave treatment because `shore` counted the land hemming it
      // in and read that as "shallowing beach". With more than about
      // two-thirds land around it the shoal term pinned high, which made
      // `breaking` (shoal > 0.55) permanently true — so entire rivers
      // rendered as standing whitewater: the flat grey sheets. Narrow water
      // takes the flowing-ripple path instead, whatever the gradient says.
      bool channel = landNear >= 16;
      // >=2 catches the gentle ~0.7/cell ramps of carved through-rivers,
      // not just steep mountain streams.
      bool river = ((std::abs(gx) + std::abs(gy) >= 2) && d <= 3) || channel;
      bool stillBiome = (w.biome == WETLAND || w.biome == DESERT);

      if (river) {
        float fx = (float)((gx > 0) - (gx < 0));
        float fy = (float)((gy > 0) - (gy < 0));
        float ph = 0.8f * ((float)x * fx + (float)y * fy) - animT * 2.8f;
        float ripple = std::sin(ph + 0.7f * std::sin(ph * 0.37f + (float)(h & 7u)));
        int lift = (int)(ripple * 14.f);
        r += lift / 2; g += lift; b += lift;
        if (ripple > 0.92f) { r += 50; g += 55; b += 50; }  // whitewater glints
      } else if (!stillBiome) {
        // One wave model for the whole sea, depth-aware — because the thing
        // that makes water read as water is a wave SHOALING: the same swell
        // that rolls unbroken through deep water feels the bottom as it
        // comes in, steepens, and breaks against the reef or the beach.
        // Previously the deep and the shallows ran different maths, so big
        // bands marched across open water and never broke on anything.
        float grp, chop;
        float swell = pixelviewSwell(w, x, y, animT, h, &grp, &chop);

        // How much this cell feels the bottom: 0 in the deep, 1 in the surf.
        float shoal = std::clamp((5.f - (float)d) / 4.f, 0.f, 1.f);
        // /6 meant a quarter of the neighbourhood being land pinned this to
        // full shore, so surf started forming a long way out. A real break
        // wants to be close in: ramp it over the range an actual beach cell
        // occupies (roughly 2 to 13 of 24).
        float shore = std::clamp(((float)landNear - 2.f) / 11.f, 0.f, 1.f);
        shoal = std::max(shoal, shore * 0.9f);

        // Steepening: amplitude grows and the face sharpens as it shallows.
        float crest = swell * std::fabs(swell);
        float amp = 0.42f + 1.45f * shoal;
        float surf = crest * amp + chop * (0.20f - 0.13f * shoal);

        // Body colour: DF-style discrete states, paling as the water thins.
        static const uint8_t kSea[12][3] = {
            {  4,  18,  52}, {  6,  26,  68}, {  9,  36,  86}, { 12,  48, 104},
            { 16,  62, 122}, { 20,  78, 140}, { 25,  95, 158}, { 31, 113, 175},
            { 38, 133, 190}, { 47, 152, 202}, { 58, 172, 213}, { 74, 194, 222},
        };
        // How much the swell shows offshore is a matter of taste: full bands
        // rolling across open water break the illusion, but none at all is
        // dead flat. So it is a live knob (~/.terrarium-swell, `vat swell`),
        // judged on the panel. Inshore the gain always climbs regardless —
        // the breaking surf is not up for negotiation.
        float offshore = 0.06f + 0.80f * displaySwell();
        float gain = offshore + (1.15f - offshore) * shoal;
        float t01 = std::clamp(surf * gain + 0.5f, 0.f, 1.f);
        int lvl = std::clamp((int)(t01 * 11.999f), 0, 11);
        float pale = shoal * 26.f;
        r = (int)((float)kSea[lvl][0] + pale * 0.7f + (float)(j / 3));
        g = (int)((float)kSea[lvl][1] + pale + (float)(j / 2));
        b = (int)((float)kSea[lvl][2] + pale * 0.8f + (float)(j / 2));

        // The lit face under a crest — again, only once it is shoaling.
        if (surf > 0.28f && surf <= 0.72f && shoal > 0.12f) {
          float f = (surf - 0.28f) / 0.44f * (0.55f * shoal);
          r += (int)(26.f * f); g += (int)(64.f * f); b += (int)(52.f * f);
        }

        // Sun glitter: offshore you read the swell from the way light
        // scatters off the wave faces, not from bands of colour. Density
        // follows the slope, so the sets are still legible as movement.
        if (shoal < 0.45f) {
          float slope = std::fabs(swell) * (1.25f - 0.5f * displaySwell());
          uint32_t gh = hash3((uint32_t)x, (uint32_t)(y + (int)(animT * 4.f)),
                              w.worldSeed ^ 0x611778u);
          uint32_t density = (uint32_t)(90.f - 62.f * slope);
          if (density < 8u) density = 8u;
          if ((gh % density) == 0u) {
            float g2 = 0.35f + 0.65f * slope;
            r = (int)(r + (210 - r) * g2 * 0.55f);
            g = (int)(g + (232 - g) * g2 * 0.55f);
            b = (int)(b + (246 - b) * g2 * 0.55f);
          }
        }

        // BREAKING. The threshold falls as the wave shoals, so one set rolls
        // on untouched in deep water and detonates on the reef.
        float thresh = 0.74f - 0.48f * shoal;
        if (surf > thresh) {
          float over = std::clamp((surf - thresh) / std::max(0.12f, 1.f - thresh), 0.f, 1.f);
          uint32_t cellId = hash3((uint32_t)(x / 2), (uint32_t)(y / 2), w.worldSeed);
          uint32_t epoch = (uint32_t)(animT * 1.7f);
          uint32_t bh = hash3(cellId, epoch, 0xB2EAu);
          float life = (animT * 1.7f) - (float)epoch;
          // In the surf zone it always breaks; offshore only sometimes.
          bool breaking = shoal > 0.55f ||
                          (bh % 100u) < (uint32_t)(10.f + 60.f * over);
          if (breaking) {
            float fade = shoal > 0.55f ? 1.f
                                       : std::sin(3.14159f * std::clamp(life, 0.f, 1.f));
            float f = std::min(1.f, over * (0.9f + 1.4f * shoal)) * fade;
            r = (int)(r + (236 - r) * f);
            g = (int)(g + (246 - g) * f);
            b = (int)(b + (252 - b) * f);
          }
        }

        // Whitewater: once a wave has broken it keeps churning inshore, and
        // the wash runs up against whatever it broke on.
        if (shore > 0.f) {
          float washPh = surf * 0.5f + 0.5f;
          float wash = shore * (0.35f + 0.45f * grp) * washPh;
          uint32_t wh2 = hash3((uint32_t)x, (uint32_t)y,
                               (uint32_t)(animT * 5.f) * 2654435761u);
          if ((wh2 % 5u) < 2u) {
            r = (int)(r + (232 - r) * wash);
            g = (int)(g + (242 - g) * wash);
            b = (int)(b + (248 - b) * wash);
          }
        }

        // Spindrift blowing off the biggest crests.
        if (surf > 0.55f && shoal < 0.5f) {
          uint32_t sp = hash3((uint32_t)x, (uint32_t)(y + (int)(animT * 3.f)),
                              w.worldSeed ^ 0x5D1F7u);
          if ((sp % 90u) == 0u) { r += 55; g += 60; b += 62; }
        }
      }
    }
  } else {
    tintable = true;
    // Foliage picks a per-cell green *family* (yellow-green, deep forest,
    // blue-green, sage) — jitter alone read as one flat green.
    uint32_t fam = (h >> 6) & 3u;
    switch (t) {
      case ',':
        switch (fam) {
          case 0:  r = 60 + j; g = 140 + j; b = 70; break;
          case 1:  r = 82 + j; g = 148 + j; b = 56; break;   // yellow-green
          case 2:  r = 48 + j; g = 130 + j; b = 92; break;   // blue-green
          default: r = 92 + j; g = 138 + j; b = 78; break;   // sage
        }
        break;
      case '"':
        switch (fam) {
          case 0:  r = 48 + j; g = 126 + j; b = 62; break;
          case 1:  r = 66 + j; g = 132 + j; b = 48; break;
          case 2:  r = 38 + j; g = 116 + j; b = 82; break;
          default: r = 74 + j; g = 122 + j; b = 66; break;
        }
        break;
      case ';':
        r = (fam & 1) ? 52 + j : 40 + j;
        g = (fam & 1) ? 118 + j : 110 + j;
        b = (fam & 1) ? 46 : 58;
        break;
      case '#':
        switch (fam) {
          case 0:  r = 36 + j; g = 96 + j; b = 52; break;
          case 1:  r = 58 + j; g = 102 + j; b = 40; break;   // olive
          case 2:  r = 30 + j; g = 88 + j; b = 66; break;    // teal shrub
          default: r = 48 + j; g = 94 + j; b = 56; break;
        }
        break;
      case ':': r = 66 + j; g = 150 + j; b = 96; break;
      case 'T': case 'Y': case 'P':
        switch (fam) {
          case 0:  r = 30; g = 84 + j; b = 40; break;        // forest
          case 1:  r = 24; g = 74 + j; b = 52; break;        // dark pine
          case 2:  r = 44; g = 92 + j; b = 36; break;        // warm canopy
          default: r = 34; g = 88 + j; b = 58; break;        // mossy
        }
        break;
      case 'm':  // forest-floor caps: cream / tan / fly-agaric red
        switch ((h >> 5) % 3u) {
          case 0:  r = 230 + j; g = 210; b = 185; break;
          case 1:  r = 195 + j; g = 160; b = 120; break;
          default: r = 205; g = 70 + j; b = 55; break;
        }
        break;
      case 'f': case '+': case '&': case '!':
        if (w.biome == TROPICAL) {
          // Lush island blooms: hibiscus, plumeria, bird-of-paradise,
          // orchid, bougainvillea.
          switch ((h >> 5) % 5u) {
            case 0:  r = 255; g = 95 + j; b = 125; break;
            case 1:  r = 255; g = 225 + j; b = 150; break;
            case 2:  r = 255; g = 150 + j; b = 45; break;
            case 3:  r = 230; g = 105 + j; b = 220; break;
            default: r = 255; g = 125 + j; b = 175; break;
          }
        } else {
          // Wildflower mix per-cell: poppy, marigold, orchid, white,
          // cornflower.
          switch ((h >> 5) % 5u) {
            case 0:  r = 225; g = 70 + j; b = 55; break;
            case 1:  r = 255; g = 215 + j; b = 90; break;
            case 2:  r = 175; g = 120 + j; b = 235; break;
            case 3:  r = 248; g = 246; b = 238; break;
            default: r = 120; g = 190 + j; b = 250; break;
          }
        }
        break;
      // ---- City ----
      case CITY_ROAD: {
        r = 34 + j / 2; g = 36 + j / 2; b = 46 + j / 2;
        // Wet asphalt goes darker and glossier when it rains.
        float wet = w.weather.rainStrength;
        if (wet > 0.02f) {
          r = (int)(r * (1.f - 0.30f * wet)); g = (int)(g * (1.f - 0.30f * wet));
          b = (int)(b * (1.f - 0.18f * wet));
        }
        break;
      }
      case CITY_WALK:  r = 78 + j; g = 76 + j; b = 84 + j; break;
      case CITY_QUAY:  r = 96 + j; g = 90 + j; b = 82 + j; break;
      case CITY_BRIDGE:r = 112 + j; g = 106 + j; b = 116 + j; break;
      case CITY_LOT:   r = 62 + j; g = 57 + j; b = 50 + j; break;
      case CITY_LOW: case CITY_MID: case CITY_TOWER: case CITY_GLASS: {
        int storeys = std::max(0, ((int)w.height[y][x] - CITY_BASE_H) / CITY_STOREY);
        // One family per building, keyed off height ALONE: every cell of a
        // building shares its height, and worldgen gives each lot its own
        // storey count. (Mixing a coarse position into this key split single
        // roofs across two colours — the city came out as confetti.)
        uint32_t famh = hash3((uint32_t)w.height[y][x], w.worldSeed, 0x0F00Fu);
        if (t == CITY_GLASS) {   // a glazed atrium roof, banded by its bays
          float band = 0.5f + 0.5f * std::sin((float)(x + y) * 1.9f);
          r = 92 + (int)(30 * band); g = 118 + (int)(34 * band);
          b = 140 + (int)(38 * band);
        } else {
          pixelviewCityRoof(famh, storeys, r, g, b);
        }
        r += j / 2; g += j / 2; b += j / 2;

        // Parapet, DIRECTIONALLY. A lot is only 3-6 cells across, so with a
        // four-way test almost every cell of a building is an edge and a flat
        // lift brightened whole roofs to near-white — badly so once the
        // brightness lift was turned up. The lip is lit on the north-west
        // sides (matching the hillshade) and shadowed on the south-east, so
        // the outline reads as relief instead of a highlight.
        bool edgeNW = !pixelviewSameBuilding(w, x, y, x - 1, y) ||
                      !pixelviewSameBuilding(w, x, y, x, y - 1);
        bool edgeSE = !pixelviewSameBuilding(w, x, y, x + 1, y) ||
                      !pixelviewSameBuilding(w, x, y, x, y + 1);
        bool edge = edgeNW || edgeSE;
        if (edgeNW)      { r += 24; g += 23; b += 22; }
        else if (edgeSE) { r -= 18; g -= 17; b -= 16; }

        // Rooftop plant: tanks, ducts, stair heads, the odd garden or pad.
        uint32_t rh = hash3((uint32_t)x, (uint32_t)y, famh ^ 0x0F007u);
        uint32_t kind = rh % 100u;
        if (!edge) {   // sparse: plant should punctuate a roof, not pepper it
          if (kind < 4u) { r = 118; g = 112; b = 104; }          // AC plant
          else if (kind < 6u) { r = 96; g = 72; b = 52; }        // water tank
          else if (kind < 8u && storeys >= 4) { r = 150; g = 150; b = 156; }   // stair head
          else if (kind < 11u && storeys <= 6) { r = 62; g = 116; b = 64; }    // roof garden
        }
        break;
      }
      case CITY_NEON: {
        // The sign's own colour; the glow is added after the light grade.
        switch ((h >> 7) % 5u) {
          case 0:  r = 236; g = 60;  b = 150; break;  // magenta
          case 1:  r = 70;  g = 220; b = 226; break;  // cyan
          case 2:  r = 255; g = 156; b = 60;  break;  // amber
          case 3:  r = 150; g = 240; b = 110; break;  // lime
          default: r = 240; g = 84;  b = 80;  break;  // red
        }
        break;
      }
      case 'V': r = 46; g = 38 + j / 2; b = 40; break;  // basalt vent
      case '$': r = 220; g = 190 + j; b = 90; break;
      case 'd': case 'e': case 'g': r = 105 + j; g = 70; b = 44; break;
      case '^': case 'B': r = 130 + j; g = 130 + j; b = 142; break;
      case 'M': r = 150 + j; g = 165 + j; b = 190; break;
      case '*': r = 255; g = 120 + ((tick * 13 + (int)(h & 31)) % 80); b = 30; break;
      case 'x': r = 70; g = 40; b = 36; break;
      case 's': r = 205 + j; g = 185 + j; b = 130; break;
      case 'c': r = 90; g = 170 + j; b = 100; break;
      default:
        if (displayBgMode() == 1) { r = g = b = 0; }             // oled: true black
        else pixelviewBiomeSoil(w.biome, j, x, y, w.worldSeed, r, g, b);
        break;
    }
    if (t == KELP_GLYPH) { r = 24; g = 140 + j; b = 110; }
    // Whole-palette biome grade over the foliage, soil and rock. The
    // accents stay true: the wildflower distribution and the fly-agaric caps
    // are chosen colours, and grading them turned white petals yellow and
    // every desert bloom orange.
    bool accent = (t == 'f' || t == '+' || t == '&' || t == '!' || t == 'm' ||
                   t == '$' || t == '*' || t == 'V' || t == 'C');
    if (!accent && w.biome != CITY) {
      pixelviewBiomeGrade(w.biome, r, g, b);
    } else if (accent && w.biome != CITY) {
      // Accents keep their chosen hue — grading them turned white petals
      // yellow and every desert bloom orange, and that rule stands. But an
      // ungraded full-chroma cell sitting alone on a field of sand does not
      // read as a flower at 1px/cell, it reads as a stuck pixel. So the
      // BLOOM stays its own colour and only its LIGHT is shared: grade a
      // copy, then take a fifth of the way there. Enough to belong to the
      // world, far too little to shift the hue.
      int gr = r, gg2 = g, gb = b;
      pixelviewBiomeGrade(w.biome, gr, gg2, gb);
      r += (gr - r) / 5; g += (gg2 - g) / 5; b += (gb - b) / 5;
    }
  }

  // The alien world is not Earth with odd colours: its whole biology is
  // bioluminescent, so the ground itself carries slow chromatic tides and
  // the flora glows in daylight instead of merely reflecting it. This
  // overrides the terrestrial hue bands wholesale for ALIEN.
  if (tintable && w.biome == ALIEN) {
    // Chromatic tide: a slow wave crossing the world, so the palette drifts
    // through violet, teal and chartreuse rather than sitting still.
    float tide = std::sin((float)x * 0.045f + (float)y * 0.031f + animT * 0.11f);
    float tide2 = std::sin((float)x * 0.017f - (float)y * 0.023f - animT * 0.07f);
    float shift = 0.5f + 0.5f * tide;
    uint32_t fam = (h >> 6) & 3u;
    switch (t) {
      case ',': case '"': case ';': case ':': {   // filament turf
        r = 26 + (int)(38.f * shift) + j;
        g = 88 + (int)(52.f * (1.f - shift)) + j;
        b = 96 + (int)(58.f * shift);
        if (fam == 0) { r += 20; b += 18; }        // violet strain
        break;
      }
      case '#': {                                  // bladder-shrub
        r = 62 + (int)(48.f * shift) + j;
        g = 42 + (int)(36.f * (1.f - shift));
        b = 104 + (int)(42.f * shift);
        break;
      }
      case 'T': case 'Y': case 'P': {              // spires, cyan at the tips
        r = 38 + (int)(30.f * shift);
        g = 28 + (int)(72.f * (0.35f + 0.65f * (1.f - shift))) + j;
        b = 92 + (int)(62.f * shift);
        break;
      }
      case 'm': {                                  // glow pods
        float pulse = 0.55f + 0.45f * std::sin(animT * 1.9f + (float)(h & 31u));
        r = (int)(70.f + 120.f * pulse);
        g = (int)(210.f + 40.f * pulse);
        b = (int)(180.f + 60.f * pulse);
        break;
      }
      case 'f': case '+': case '&': case '!': {    // luminous blooms
        float pulse = 0.5f + 0.5f * std::sin(animT * 1.3f + (float)(h & 63u));
        switch ((h >> 5) % 4u) {
          case 0:  r = (int)(90 + 90 * pulse);  g = 250; b = 230; break;  // cyan
          case 1:  r = 240; g = (int)(70 + 60 * pulse);  b = 250; break;  // magenta
          case 2:  r = (int)(180 + 60 * pulse); g = 255; b = 90;  break;  // chartreuse
          default: r = 150; g = (int)(120 + 90 * pulse); b = 255; break;  // violet
        }
        break;
      }
      case '$': r = 250; g = 230; b = 120; break;  // amber sap
      case 'c': r = 90;  g = 230; b = 190; break;
      case 'd': case 'e': case 'g':                // regolith / flesh ground
        r = 74 + (int)(20.f * tide2) + j / 2;
        g = 52 + (int)(16.f * tide) + j / 2;
        b = 86 + (int)(26.f * tide2);
        break;
      case '^': case 'B': r = 104; g = 96; b = 128; break;
      case 'M': r = 150; g = 140; b = 178; break;
      case 's': r = 168; g = 150; b = 190; break;
      case '.': default:
        if (displayBgMode() == 1) { r = g = b = 0; }
        else {
          r = 34 + (int)(14.f * tide2) + j / 2;
          g = 24 + j / 2;
          b = 44 + (int)(18.f * tide) + j / 2;
        }
        break;
    }
  }

  // Depth when the world fills in: once vegetation covers everything the
  // scene loses its value contrast, so shape the land itself —
  // hillshade (NW light: facing slopes brighten, far slopes shadow) and
  // canopy occlusion (cells buried in tall growth darken; clump edges
  // stay lit, so forests read as sculpted masses).
  if (tintable) {
    int sgx = 0, sgy = 0;
    if (x > 0 && x < W - 1) sgx = (int)w.height[y][x+1] - (int)w.height[y][x-1];
    if (y > 0 && y < H - 1) sgy = (int)w.height[y+1][x] - (int)w.height[y-1][x];
    float hill = 1.0f + std::max(-0.16f, std::min(0.20f, (float)(-sgx - sgy) * 0.010f));

    int tall = 0;
    for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
      if (!ox && !oy) continue;
      int nx2 = x + ox, ny2 = y + oy;
      if (nx2 < 0 || ny2 < 0 || nx2 >= W || ny2 >= H) continue;
      char n = w.terrain[ny2][nx2];
      if (n == 'T' || n == 'Y' || n == 'P' || n == '#') ++tall;
    }
    bool selfTall = (t == 'T' || t == 'Y' || t == 'P' || t == '#');
    float ao = 1.0f - (selfTall ? 0.030f : 0.020f) * (float)tall;

    float shape = hill * ao;
    r = (int)(r * shape); g = (int)(g * shape); b = (int)(b * shape);
  }

  // ---- Seasonal legibility ----
  // A +14 red / -6 green nudge was not a season, it was a rumour. The canopy
  // is the loudest signal nature has, so it carries most of this: woods turn,
  // go bare, and come back, per-tree so a stand is mottled rather than
  // uniformly repainted.
  if (tintable && w.biome != TROPICAL && w.biome != ALIEN && w.biome != CITY) {
    bool woody = (t == 'T' || t == 'Y' || t == 'P');
    bool leafy = woody || t == '#';
    bool grassy = (t == ',' || t == '"' || t == ';' || t == ':');
    if (leafy) {
      float sl = seasonLerp(tick);
      uint32_t th = hash3((uint32_t)(x / 2), (uint32_t)(y / 2),
                          w.worldSeed ^ 0xAC0A17u);
      float own = (float)(th & 255u) / 255.f;      // this tree's own clock
      if (season == AUTUMN) {
        float turn = std::clamp(sl * 1.5f - own * 0.45f, 0.f, 1.f);
        int tr, tg, tb;
        switch ((th >> 9) % 4u) {                  // gold, amber, rust, scarlet
          case 0:  tr = 226; tg = 178; tb = 52;  break;
          case 1:  tr = 218; tg = 132; tb = 44;  break;
          case 2:  tr = 176; tg = 86;  tb = 40;  break;
          default: tr = 196; tg = 62;  tb = 46;  break;
        }
        r = (int)(r + (tr - r) * turn);
        g = (int)(g + (tg - g) * turn);
        b = (int)(b + (tb - b) * turn);
      } else if (season == WINTER) {
        // Bare wood: the leaves are on the ground, not the tree.
        float bare = std::clamp(0.55f + 0.45f * sl, 0.f, 1.f);
        int tr = 104 + (int)(26.f * own), tg = 88 + (int)(20.f * own), tb = 74;
        r = (int)(r + (tr - r) * bare);
        g = (int)(g + (tg - g) * bare);
        b = (int)(b + (tb - b) * bare);
      } else if (season == SPRING) {
        // New growth, and blossom on about a third of the stand.
        float fresh = std::clamp(1.f - sl * 0.7f, 0.f, 1.f);
        g = (int)(g + (200 - g) * 0.30f * fresh);
        r = (int)(r + (150 - r) * 0.16f * fresh);
        if (woody && ((th >> 17) % 3u) == 0u) {
          float bloom = std::clamp(1.3f - sl * 1.8f, 0.f, 1.f);
          uint32_t ph = hash3((uint32_t)x, (uint32_t)y, 0xB105u);
          if ((ph % 3u) == 0u) {
            int pr = ((th >> 20) & 1u) ? 244 : 250;
            int pg = ((th >> 20) & 1u) ? 196 : 232;
            int pb = ((th >> 20) & 1u) ? 214 : 236;
            r = (int)(r + (pr - r) * bloom);
            g = (int)(g + (pg - g) * bloom);
            b = (int)(b + (pb - b) * bloom);
          }
        }
      } else if (season == SUMMER) {
        g = (int)(g * 0.94f); r = (int)(r * 1.05f);   // deep, slightly dusty
      }
    } else if (grassy) {
      // Grass turns too — and in a meadow it is the only thing that turns at
      // any scale, because the trees are far too sparse to carry a season on
      // their own. That is why softening the terrain grade made summer and
      // autumn look identical out here. It goes TAWNY, not gold: a hayfield
      // in October is straw, not a maple.
      float sl = seasonLerp(tick);
      uint32_t gh2 = hash3((uint32_t)(x / 3), (uint32_t)(y / 3),
                           w.worldSeed ^ 0x62A55u);
      float own = (float)(gh2 & 255u) / 255.f;
      if (season == AUTUMN) {
        float turn = std::clamp(sl * 1.3f - own * 0.35f, 0.f, 1.f) * 0.70f;
        int tr = 172 + (int)(26.f * own), tg = 148 + (int)(20.f * own), tb = 84;
        r = (int)(r + (tr - r) * turn);
        g = (int)(g + (tg - g) * turn);
        b = (int)(b + (tb - b) * turn);
      } else if (season == WINTER) {
        // Dead straw under the snow, where the snow has not covered it.
        float dead = std::clamp(0.45f + 0.40f * sl, 0.f, 1.f) * 0.78f;
        int tr = 134 + (int)(20.f * own), tg = 120 + (int)(16.f * own), tb = 88;
        r = (int)(r + (tr - r) * dead);
        g = (int)(g + (tg - g) * dead);
        b = (int)(b + (tb - b) * dead);
      } else if (season == SPRING) {
        float fresh = std::clamp(1.f - sl * 0.6f, 0.f, 1.f);
        g = (int)(g + (190 - g) * 0.22f * fresh);
        b = (int)(b + (110 - b) * 0.10f * fresh);
      }
    }
  }

  // Season grade on terrain: autumn browns the foliage, spring vivifies,
  // winter cools — plus a frost/snow dusting on open ground in winter.
  if (tintable) {
    // A WARM LIGHT over the ground, not a coat of brown paint. At +14 red /
    // -6 green this stacked on top of the canopy turn above and took the
    // whole world to chocolate — and on ALPINE's blue-grey scree the two
    // together came out lilac.
    if (season == AUTUMN) { r += 6; g += 1; b -= 4; }
    else if (season == SPRING) { g += 7; b += 2; }
    else if (snowy) {
      r = (int)(r * 0.90f) + 14; g = (int)(g * 0.92f) + 12; b += 22;
      // Snow settles on open ground and grass; trees and shrubs just take
      // the frost tint above (snowy pine forest, not white-out).
      bool ground = (t == ',' || t == '"' || t == ';' || t == '.' ||
                     t == ':' || t == 's' ||
                     isCityPaved(t) || t == CITY_LOT);  // snow lies on streets
      if (ground) {
        // Snow DRIFTS. A per-cell coin flip against a coverage fraction is
        // white noise, and white noise at 1px/cell is indistinguishable from
        // a failing panel — this was far and away the loudest thing in a
        // winter frame. A smooth depth field instead: it banks against the
        // low-frequency pattern, and the edges of the banks come out
        // dithered for free because the blend is continuous.
        float depth = pixelviewFbm2(x, y, 7.0f, w.worldSeed ^ 0x534E4F57u);
        float level = 0.26f + 0.44f * seasonLerp(tick);
        float lay = std::clamp((depth - (1.f - level)) * 3.2f, 0.f, 1.f);
        if (lay > 0.f) {
          // Snow in shade is BLUE. Flat 226,232,244 is why it read as paper
          // cut-outs instead of as something lying on a surface with shape.
          float sh2 = 0.82f + 0.18f * pixelviewValueNoise(x, y, 3.0f, 0x5D0Cu);
          int sr = (int)(204.f * sh2) + 14, sg = (int)(214.f * sh2) + 16,
              sb = (int)(230.f * sh2) + 18;
          r = (int)(r + (sr - r) * lay);
          g = (int)(g + (sg - g) * lay);
          b = (int)(b + (sb - b) * lay);
        }
      }
      // Roofs take snow too, but blended: a hard white speckle over the
      // city's painted metal read as static rather than settled snow.
      if (w.biome == CITY && isCityBuilding(t)) {
        // A DUSTING. At 0.35-0.80 this repainted the city white for the whole
        // of a (day-long) winter, which on a bright panel is a lot of lamp.
        uint32_t sh = hash3((uint32_t)x, (uint32_t)y, 0x524F4F46u);
        float lay = (0.10f + 0.20f * seasonLerp(tick)) *
                    (0.35f + 0.65f * (float)(sh & 255u) / 255.f);
        r = (int)(r + (232 - r) * lay);
        g = (int)(g + (238 - g) * lay);
        b = (int)(b + (248 - b) * lay);
      }
    }
  }

  // Marine life: shoals working the water, drawn over it. Only where there is
  // enough sea to be worth swimming in.
  if (d > 0 && (w.biome == OCEAN || w.island || w.biome == TROPICAL)) {
    const MarineLife& M = pixelviewMarine(w, animT);
    uint8_t m = M.cell[(size_t)y * W + x];
    if (m) {
      int sh2 = 7 - std::min(7, (int)d);     // deeper fish read dimmer
      switch (m - 1) {
        case 0:  // sardines: a silver flicker
          r = 176 + sh2 * 6; g = 200 + sh2 * 5; b = 214 + sh2 * 4;
          break;
        case 1:  // reef fish: colour on the shallows
          switch ((h >> 11) % 4u) {
            case 0:  r = 250; g = 170; b = 60;  break;
            case 1:  r = 240; g = 210; b = 90;  break;
            case 2:  r = 90;  g = 200; b = 220; break;
            default: r = 220; g = 110; b = 160; break;
          }
          break;
        default: // a ray or a turtle, moving on its own errand
          r = 54 + sh2 * 3; g = 66 + sh2 * 3; b = 86 + sh2 * 3;
          break;
      }
    }
  }

  // Ice. Water is the other unmistakable seasonal tell: it freezes from the
  // shallows outward, so a pond goes over completely while a harbour or an
  // open sea only crusts at its edges.
  if (d > 0 && snowy && w.biome != TROPICAL) {
    float sl = seasonLerp(tick);
    float depthFactor = (d <= 2) ? 1.0f : (d <= 4 ? 0.45f : 0.10f);
    // Coherent roughness, for the same reason the snow drifts: a per-cell
    // random made the ice surface white noise, and a crack chosen by
    // `hash % 41` put an isolated pure-white pixel on one frozen cell in
    // forty-one. Ice does not craze one pixel at a time.
    float rough = 0.78f + 0.22f * pixelviewFbm2(x, y, 6.0f,
                                                w.worldSeed ^ 0x1CE1u);
    float ice = std::clamp(sl * 1.25f, 0.f, 1.f) * depthFactor * rough;
    if (ice > 0.02f) {
      r = (int)(r + (198 - r) * ice);
      g = (int)(g + (220 - g) * ice);
      b = (int)(b + (236 - b) * ice);
      // Cracks are LINES. Taking the contour where a smooth field crosses a
      // level gives connected veins that wander and branch, which is what a
      // frozen pond actually looks like, for the same cost as the dots.
      if (ice > 0.55f) {
        float cf = pixelviewValueNoise(x, y, 11.0f, w.worldSeed ^ 0xC2ACu);
        float vein = std::fabs(cf - 0.5f);
        if (vein < 0.028f) {
          float k = (1.f - vein / 0.028f) * 0.85f;
          r = (int)(r + (236 - r) * k);
          g = (int)(g + (246 - g) * k);
          b = (int)(b + (252 - b) * k);
        }
      }
    }
  }

  // Cloud shadow + smooth day/night cycle (dawn gold, dusk amber, cool
  // moonlit nights — never a hard brightness step).
  float shade = 1.0f - (pixelviewCloudAt(w, x, y) / 255.0f) * 0.35f;
  Daylight dl = daylightNow(tick);
  float bright = shade * (0.38f + 0.62f * dl.level) * displayBrightness();
  float rr = (float)r * bright * (1.f + 0.20f * dl.warm);
  float gg = (float)g * bright * (1.f + 0.04f * (dl.warm > 0.f ? dl.warm : 0.f));
  float bb = (float)b * bright * (1.f - 0.18f * dl.warm);

  // The sky replaces all of the above. It takes the daylight directly
  // rather than through the cloud-shadow/brightness chain, because up here
  // the clouds are the subject rather than something casting on the ground,
  // and a cloud shadow falling on the sky itself is a contradiction.
  if (w.biome == SKY) {
    pixelviewSkyCell(w, x, y, animT, dl, rr, gg, bb);
    // Only the split tone and black lift, never the chroma ceiling: the
    // balloons and the mane are the point.
    pixelviewHarmonise(rr, gg, bb, displayHarmony(), 0.0f);
    float lift = displayLift();
    if (lift > 1.001f) {
      rr = 255.f * (1.f - std::pow(1.f - std::clamp(rr / 255.f, 0.f, 1.f), lift));
      gg = 255.f * (1.f - std::pow(1.f - std::clamp(gg / 255.f, 0.f, 1.f), lift));
      bb = 255.f * (1.f - std::pow(1.f - std::clamp(bb / 255.f, 0.f, 1.f), lift));
    }
    float ck2 = displayContrast();
    if (ck2 != 1.0f) {
      rr = (rr - 128.f) * ck2 + 128.f;
      gg = (gg - 128.f) * ck2 + 128.f;
      bb = (bb - 128.f) * ck2 + 128.f;
    }
    return PixelviewRGB{clampU8((int)rr), clampU8((int)gg), clampU8((int)bb)};
  }

  // Things in the air: leaves off the turning wood in autumn, blossom in
  // spring. They blow with the wind, which also makes the wind visible.
  if (w.biome != TROPICAL && w.biome != ALIEN && d == 0 && e == ' ' &&
      (season == AUTUMN || season == SPRING)) {
    float br2 = displayBrightness();
    float wx4 = (w.wind.dx == 0 && w.wind.dy == 0) ? 0.6f : (float)w.wind.dx;
    float wy4 = (float)w.wind.dy + 0.55f;          // everything settles
    float drift = 0.9f + 0.45f * (float)w.wind.strength;
    uint32_t lh = hash3((uint32_t)(x - (int)(animT * wx4 * drift)),
                        (uint32_t)(y - (int)(animT * wy4 * drift)),
                        w.worldSeed ^ (season == AUTUMN ? 0x1EAFu : 0xB1055u));
    if ((lh % (season == AUTUMN ? 300u : 420u)) == 0u) {
      float flutter = 0.55f + 0.45f * std::sin(animT * 3.1f + (float)(lh & 31u));
      float p = flutter * br2 * (season == AUTUMN ? 0.85f : 0.75f);
      if (season == AUTUMN) {
        switch ((lh >> 7) % 3u) {
          case 0:  rr += 210.f * p; gg += 140.f * p; bb += 40.f * p; break;
          case 1:  rr += 190.f * p; gg += 80.f * p;  bb += 36.f * p; break;
          default: rr += 220.f * p; gg += 176.f * p; bb += 56.f * p; break;
        }
      } else {
        rr += 240.f * p; gg += 200.f * p; bb += 215.f * p;   // petals
      }
    }
  }

  // Fireflies: sparse warm motes drifting through non-winter nights.
  if (dl.level < 0.30f && season != WINTER && d == 0 && e == ' ' &&
      (w.biome == MEADOW || w.biome == WETLAND || w.biome == TROPICAL)) {
    uint32_t fh = hash3((uint32_t)x, (uint32_t)y, 0xF12EF1u ^ w.worldSeed);
    float p = pixelviewMote(fh, animT, 6.0f, 1600u, 2.0f) * displayBrightness();
    rr += 200.f * p; gg += 215.f * p; bb += 90.f * p;
  }

  // ---- The city after dark (and its traffic, which never stops) ----
  if (w.biome == CITY) {
    const float br = displayBrightness();
    const float night = std::clamp(1.0f - dl.level / 0.60f, 0.0f, 1.0f);
    const float fx = (float)x, fy = (float)y;

    // Shadows. From above, a city is legible because its towers lie across
    // everything north-west of them — this is what puts the skyline into a
    // top-down view at all.
    if (dl.level > 0.20f) {
      int hHere = (int)w.height[y][x];
      float dark = 0.f;
      for (int k = 1; k <= 4; ++k) {
        int nx = x - k, ny = y - k;
        if (!inBounds(nx, ny)) break;
        if (!isCityBuilding(w.terrain[ny][nx])) continue;
        int over = (int)w.height[ny][nx] - hHere;
        if (over <= 0) continue;
        // A storey throws about a cell of shadow at this sun angle.
        float reach = (float)over / (float)CITY_STOREY;
        if (reach >= (float)k)
          dark = std::max(dark, 0.42f * (1.f - (float)(k - 1) * 0.18f));
      }
      if (dark > 0.f) {
        float f = dark * dl.level;
        rr *= (1.f - f); gg *= (1.f - f * 0.94f); bb *= (1.f - f * 0.82f);
      }
    }

    // Golden hour over a harbour city: the whole thing goes rose.
    if (dl.warm > 0.f) {
      float ww = dl.warm * br;
      rr += 34.f * ww; gg += 9.f * ww; bb += 20.f * ww;
    }

    // Lit windows: each cell is a floor's worth of glass that switches on
    // its own slow clock, so the towers shimmer instead of strobing.
    if (isCityBuilding(t) && night > 0.02f) {
      uint32_t wh = hash3((uint32_t)x, (uint32_t)y, 0x5711DEu ^ w.worldSeed);
      float lifeSec = 50.f + (float)(wh % 120u);
      uint32_t ep = (uint32_t)(animT / lifeSec + (float)(wh % 977u) * 0.0011f);
      uint32_t st = hash3(wh, ep * 2654435761u, 0x1417u);
      float density = (t == CITY_LOW) ? 0.26f
                    : (t == CITY_GLASS) ? 0.30f : 0.38f;
      density *= 0.45f + 0.55f * night;     // the offices empty overnight
      // Occupancy per building (height keys a building, as with its roof):
      // some blocks blaze, some are dark for the night. Uniform density made
      // every tower an identical smear of scattered lights.
      uint32_t bh = hash3((uint32_t)w.height[y][x], w.worldSeed, 0x0CC0u);
      density *= 0.20f + 1.35f * (float)(bh & 255u) / 255.f;
      // The perimeter is where the windows are; a roof is mostly roof.
      bool onEdge = !pixelviewSameBuilding(w, x, y, x - 1, y) ||
                    !pixelviewSameBuilding(w, x, y, x + 1, y) ||
                    !pixelviewSameBuilding(w, x, y, x, y - 1) ||
                    !pixelviewSameBuilding(w, x, y, x, y + 1);
      density *= onEdge ? 1.6f : 0.35f;
      if ((float)(st & 1023u) / 1023.f < density) {
        float p = night * br;
        switch ((st >> 12) % 8u) {
          case 0:  rr += 120.f * p; gg += 170.f * p; bb += 235.f * p; break;  // a TV
          case 1:  rr += 235.f * p; gg += 232.f * p; bb += 215.f * p; break;  // office white
          default: rr += 245.f * p; gg += 205.f * p; bb += 140.f * p; break;  // lamplight
        }
      }
    }

    // Signage, street lamps and their spill. One 3x3 pass gathers both.
    if (night > 0.02f && (isCityPaved(t) || isCityBuilding(t) ||
                          t == CITY_NEON || t == CITY_LOT)) {
      float glowR = 0.f, glowG = 0.f, glowB = 0.f;
      for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
        int nx = x + ox, ny = y + oy;
        if (!inBounds(nx, ny)) continue;
        char n = w.terrain[ny][nx];
        bool self = (ox == 0 && oy == 0);
        float fall = self ? 1.0f : (ox && oy ? 0.28f : 0.42f);
        if (n == CITY_NEON) {
          uint32_t nh = hash3((uint32_t)nx, (uint32_t)ny, w.worldSeed);
          float pulse = 0.72f + 0.28f * std::sin(animT * (1.1f + (float)(nh & 7u) * 0.3f) +
                                                 (float)(nh & 63u));
          // A tube on the blink: rare, brief, and it comes back.
          if (((nh >> 9) % 7u) == 0u &&
              std::sin(animT * 6.0f + (float)(nh & 31u)) > 0.93f) pulse *= 0.25f;
          float a = pulse * fall * night;
          switch ((nh >> 7) % 5u) {
            case 0:  glowR += 236.f * a; glowG += 60.f * a;  glowB += 150.f * a; break;
            case 1:  glowR += 70.f * a;  glowG += 220.f * a; glowB += 226.f * a; break;
            case 2:  glowR += 255.f * a; glowG += 156.f * a; glowB += 60.f * a;  break;
            case 3:  glowR += 150.f * a; glowG += 240.f * a; glowB += 110.f * a; break;
            default: glowR += 240.f * a; glowG += 84.f * a;  glowB += 80.f * a;  break;
          }
        } else if (n == CITY_WALK || n == CITY_BRIDGE) {
          // Street lamps at regular intervals along the pavement.
          if ((hash3((uint32_t)nx, (uint32_t)ny, 0x1A47Fu) % 19u) == 0u) {
            float a = fall * night * (0.85f + 0.15f * std::sin(animT * 0.7f + (float)nx));
            glowR += 210.f * a; glowG += 160.f * a; glowB += 85.f * a;
          }
        }
      }
      rr += glowR * 0.80f * br; gg += glowG * 0.80f * br; bb += glowB * 0.80f * br;
    }

    // Aviation lights: a slow red blink on the tallest towers.
    if (isCityBuilding(t) && night > 0.15f &&
        (int)w.height[y][x] >= CITY_BASE_H + 20 * CITY_STOREY) {
      uint32_t ah = hash3((uint32_t)w.height[y][x], w.worldSeed, 0xA71Au);
      if ((hash3((uint32_t)x, (uint32_t)y, ah) % 23u) == 0u) {
        float blink = std::sin(animT * 1.7f + (float)(ah & 63u));
        if (blink > 0.55f) {
          float p = night * br * (blink - 0.55f) / 0.45f;
          rr += 235.f * p; gg += 40.f * p; bb += 30.f * p;
        }
      }
    }

    // Traffic: driven agents, looked up from this frame's occupancy grid.
    if (t == CITY_ROAD || t == CITY_BRIDGE) {
      const CityTraffic& T = pixelviewTraffic(w, animT);
      uint8_t occ = T.cell[(size_t)y * W + x];
      if (occ) {
        uint32_t ch = hash3((uint32_t)occ, w.worldSeed, 0xCA25u);
        bool heading = (T.dirOf[(size_t)y * W + x] == 1u);
        uint32_t kind = ch % 100u;
        if (night > 0.35f) {
          // Lamps, not paintwork: what you see of a car at night.
          if (kind < 4u) {                       // emergency, running blue
            float f = std::sin(animT * 9.f + (float)(ch & 31u)) > 0.f ? 1.f : 0.25f;
            rr = 60.f * br * f; gg = 110.f * br * f; bb = 255.f * br * f;
          } else if (kind < 12u) {               // a taxi's roof sign
            rr = 255.f * br; gg = 196.f * br; bb = 70.f * br;
          } else if (heading) {
            if (kind < 56u) { rr = 255.f * br; gg = 238.f * br; bb = 200.f * br; }
            else            { rr = 226.f * br; gg = 238.f * br; bb = 255.f * br; }  // xenon
          } else {
            if (kind < 60u) { rr = 240.f * br; gg = 52.f * br;  bb = 40.f * br; }
            else            { rr = 245.f * br; gg = 120.f * br; bb = 40.f * br; }   // amber
          }
        } else {
          switch (kind % 11u) {                  // daylight paintwork
            case 0:  rr = 232.f; gg = 236.f; bb = 240.f; break;  // white
            case 1:  rr = 44.f;  gg = 46.f;  bb = 54.f;  break;  // black
            case 2:  rr = 198.f; gg = 58.f;  bb = 52.f;  break;  // red
            case 3:  rr = 62.f;  gg = 104.f; bb = 188.f; break;  // blue
            case 4:  rr = 246.f; gg = 202.f; bb = 58.f;  break;  // taxi yellow
            case 5:  rr = 132.f; gg = 140.f; bb = 146.f; break;  // silver
            case 6:  rr = 54.f;  gg = 132.f; bb = 96.f;  break;  // green
            case 7:  rr = 236.f; gg = 138.f; bb = 62.f;  break;  // orange bus
            case 8:  rr = 88.f;  gg = 176.f; bb = 186.f; break;  // teal
            case 9:  rr = 176.f; gg = 84.f;  bb = 152.f; break;  // plum
            default: rr = 214.f; gg = 208.f; bb = 190.f; break;  // cream van
          }
          rr *= br; gg *= br; bb *= br;
        }
      }
    }

    // Harbour: the skyline falls into the water, wobbling.
    if (d > 0 && night > 0.05f) {
      float wob = 1.4f * std::sin(fy * 0.8f + animT * 1.7f);
      for (int k = 2; k <= 8; k += 3) {
        int sxr = (int)(fx + wob), syr = y - k;
        if (!inBounds(sxr, syr)) continue;
        char src = w.terrain[syr][sxr];
        float a = night * br * (0.30f - 0.03f * (float)k);
        if (src == CITY_NEON) {
          uint32_t nh = hash3((uint32_t)sxr, (uint32_t)syr, w.worldSeed);
          switch ((nh >> 7) % 5u) {
            case 0:  rr += 190.f * a; gg += 50.f * a;  bb += 120.f * a; break;
            case 1:  rr += 55.f * a;  gg += 175.f * a; bb += 180.f * a; break;
            case 2:  rr += 205.f * a; gg += 125.f * a; bb += 50.f * a;  break;
            case 3:  rr += 120.f * a; gg += 190.f * a; bb += 90.f * a;  break;
            default: rr += 190.f * a; gg += 70.f * a;  bb += 65.f * a;  break;
          }
        } else if (isCityBuilding(src)) {
          uint32_t wh2 = hash3((uint32_t)sxr, (uint32_t)syr, 0x5711DEu ^ w.worldSeed);
          if ((wh2 % 5u) == 0u) {
            rr += 170.f * a; gg += 145.f * a; bb += 100.f * a;
          }
        }
      }
    }

    // Boats. The ferry works her route whatever the weather; a freighter
    // stands in and out; the moored ones nod at the quay.
    if (d > 0) {
      const CityBoats& B = pixelviewCityBoats(w, animT);
      auto hull = [&](float bx, float by, float dx2, float dy2, float len,
                      float wid) {
        float px = fx - bx, py = fy - by;
        float along = px * dx2 + py * dy2;
        float across = -px * dy2 + py * dx2;
        return (along * along) / (len * len) + (across * across) / (wid * wid);
      };
      if (B.ferryUp) {
        float q = hull(B.ferX, B.ferY, B.ferDx, B.ferDy, 2.6f, 1.15f);
        if (q < 1.f) {
          rr = 232.f * br; gg = 234.f * br; bb = 238.f * br;   // white hull
          if (q < 0.28f && night > 0.3f) {                     // lit saloon
            rr = 255.f * br; gg = 226.f * br; bb = 160.f * br;
          }
        } else if (q < 2.6f) {
          // Wake: foam trailing astern.
          float px = fx - B.ferX, py = fy - B.ferY;
          float along = px * B.ferDx + py * B.ferDy;
          if (along < 0.f) {
            uint32_t fh = hash3((uint32_t)x, (uint32_t)y,
                                (uint32_t)(animT * 5.f) * 2654435761u);
            if ((fh % 3u) == 0u) {
              rr = rr * 0.4f + 205.f * br * 0.6f;
              gg = gg * 0.4f + 225.f * br * 0.6f;
              bb = bb * 0.4f + 240.f * br * 0.6f;
            }
          }
        }
        if (night > 0.35f) {   // nav lights: red to port, green to starboard
          float px = fx - B.ferX, py = fy - B.ferY;
          float across = -px * B.ferDy + py * B.ferDx;
          float along = px * B.ferDx + py * B.ferDy;
          if (along > 1.6f && along < 3.0f && std::fabs(across) > 0.6f &&
              std::fabs(across) < 1.8f) {
            if (across > 0.f) { rr = 60.f * br; gg = 240.f * br; bb = 90.f * br; }
            else              { rr = 245.f * br; gg = 50.f * br; bb = 50.f * br; }
          }
        }
      }
      if (B.shipUp) {
        float q = hull(B.shpX, B.shpY, B.shpDx, B.shpDy, 6.5f, 1.9f);
        if (q < 1.f) {
          float px = fx - B.shpX, py = fy - B.shpY;
          float along = px * B.shpDx + py * B.shpDy;
          if (along > 2.0f) {                    // stacked containers
            uint32_t ch = hash3((uint32_t)x, (uint32_t)y, w.worldSeed ^ 0xC0A7u);
            switch (ch % 4u) {
              case 0:  rr = 170.f * br; gg = 55.f * br;  bb = 48.f * br; break;
              case 1:  rr = 45.f * br;  gg = 95.f * br;  bb = 150.f * br; break;
              case 2:  rr = 60.f * br;  gg = 130.f * br; bb = 90.f * br; break;
              default: rr = 190.f * br; gg = 150.f * br; bb = 60.f * br; break;
            }
          } else {
            rr = 48.f * br; gg = 52.f * br; bb = 62.f * br;   // hull and house
            if (night > 0.3f && along < -3.5f) {
              rr = 250.f * br; gg = 220.f * br; bb = 160.f * br;
            }
          }
        }
      }
      for (int i = 0; i < B.mooredN; ++i) {
        float dxm = fx - B.mooX[i], dym = fy - B.mooY[i];
        if (dxm * dxm + dym * dym * 2.2f < 1.6f) {
          rr = 208.f * br; gg = 206.f * br; bb = 196.f * br;
          if (night > 0.4f && ((hash3((uint32_t)i, 0x11u, w.worldSeed) & 1u) == 0u)) {
            rr = 250.f * br; gg = 210.f * br; bb = 120.f * br;
          }
        }
      }
    }
  }

  // Alpine aurora: slow green/violet curtains wash over the night.
  if (w.biome == ALPINE && dl.level < 0.25f) {
    float aph = (float)x * 0.05f + (float)y * 0.02f;
    float a1 = std::sin(aph + animT * 0.50f);  a1 = a1 > 0.f ? a1 * a1 : 0.f;
    float a2 = std::sin(aph * 0.7f - animT * 0.33f + 2.1f);
    a2 = a2 > 0.f ? a2 * a2 : 0.f;
    float br = displayBrightness();
    rr += 14.f * a2 * br;
    gg += (26.f * a1 + 10.f * a2) * br;
    bb += (16.f * a1 + 22.f * a2) * br;
  }

  // Desert night: starlight twinkle, and now and then a shooting star.
  if (w.biome == DESERT && dl.level < 0.25f) {
    float br = displayBrightness();
    uint32_t st = hash3((uint32_t)x, (uint32_t)y, 0xDE5E27u ^ w.worldSeed);
    {
      float p = pixelviewMote(st, animT, 5.0f, 1900u, 3.0f) * br;
      rr += 175.f * p; gg += 190.f * p; bb += 220.f * p;
    }
    uint32_t sep = (uint32_t)(animT / 40.0f);
    uint32_t se = hash3(sep, 0x57A2u, w.worldSeed);
    float t0 = (float)sep * 40.0f + (float)(se % 860u) / 25.0f;
    float tt = animT - t0;
    if (tt > 0.f && tt < 1.2f) {
      float px = (float)((se >> 8) & 127u) / 127.f * (float)W + tt * 90.f;
      float py = (float)((se >> 16) & 63u) / 63.f * (float)H * 0.5f + tt * 38.f;
      float ddx = (float)x - px, ddy = (float)y - py;
      float dist2 = ddx * ddx + ddy * ddy;
      if (dist2 < 2.5f) { rr += 230.f * br; gg += 235.f * br; bb += 240.f * br; }
      else {
        float back = (ddx * -90.f + ddy * -38.f) / 97.7f;
        if (back > 0.f && back < 7.f && dist2 - back * back < 1.8f) {
          float fade = (1.f - back / 7.f) * 0.8f * br;
          rr += 200.f * fade; gg += 205.f * fade; bb += 215.f * fade;
        }
      }
    }
  }

  // Alien biology is its own light source: the blooms and pods do not go
  // dark at dusk, they take over. Emission is added after the day/night
  // grade and scaled by how dark it has got, so the world inverts from a
  // strange daylight into a glowing one.
  if (w.biome == ALIEN) {
    const float br = displayBrightness();
    const float dark = std::clamp(1.0f - dl.level, 0.0f, 1.0f);
    if (e == ' ' && d == 0) {
      float emit = 0.f;
      switch (t) {
        case 'f': case '+': case '&': case '!': emit = 0.85f; break;
        case 'm': emit = 0.70f; break;
        case 'T': case 'Y': case 'P': emit = 0.18f; break;
        case '#': emit = 0.10f; break;
        case ',': case '"': case ';': case ':': emit = 0.04f; break;
        default: break;
      }
      if (emit > 0.f) {
        float pulse = 0.62f + 0.38f * std::sin(animT * 1.5f + (float)(h & 63u));
        float p = emit * pulse * (0.25f + 0.75f * dark) * br;
        rr += (float)r * p * 0.55f; gg += (float)g * p * 0.55f; bb += (float)b * p * 0.55f;
      }
    }
    // Ammonia sea: the water is not Earth-blue.
    if (d > 0) {
      float vio = 0.5f + 0.5f * std::sin((float)x * 0.03f - (float)y * 0.02f + animT * 0.09f);
      float nr = rr * 0.72f + bb * 0.30f * vio + 18.f * vio;
      float ng = gg * 0.94f + 16.f * (1.f - vio);
      float nb = bb * 0.88f + gg * 0.16f;
      rr = nr; gg = ng; bb = nb;
    }
    // A bioluminescent tide sweeps the whole world every half minute or so.
    {
      float band = std::sin(((float)x + (float)y) * 0.06f - animT * 0.55f);
      if (band > 0.92f) {
        float p = (band - 0.92f) / 0.08f * 0.22f * br;
        rr += 40.f * p; gg += 150.f * p; bb += 130.f * p;
      }
    }

    // ...and, rarely, something leans in around the edge of the world to see
    // what is in here. The circle mask crops whatever is still outside the
    // rim, which is what sells it as peering round a corner.
    const AlienHead& A = pixelviewAlienHead(w, animT);
    if (A.up && A.rise > 0.01f) {
      const float fxx = (float)x, fyy = (float)y;
      // Cheap reject: everything below is confined to this box.
      float bx0 = std::min(A.cx, A.handX) - A.rx * 2.6f;
      float bx1 = std::max(A.cx, A.handX) + A.rx * 2.6f;
      float by0 = A.cy - A.ry * 1.8f;
      float by1 = std::max(A.cy + A.ry * 3.4f, A.handY + A.rx * 1.2f);
      if (fxx > bx0 && fxx < bx1 && fyy > by0 && fyy < by1) {
        auto ellipse = [&](float cx2, float cy2, float rx2, float ry2, float rot) {
          float dx2 = fxx - cx2, dy2 = fyy - cy2;
          float c = std::cos(rot), s2 = std::sin(rot);
          float a2 = dx2 * c + dy2 * s2, b2 = -dx2 * s2 + dy2 * c;
          return (a2 * a2) / (rx2 * rx2) + (b2 * b2) / (ry2 * ry2);
        };
        auto capsule = [&](float x0, float y0, float x1, float y1, float rad) {
          float vx = x1 - x0, vy = y1 - y0;
          float len2 = vx * vx + vy * vy;
          float tt = (len2 > 0.f) ? ((fxx - x0) * vx + (fyy - y0) * vy) / len2 : 0.f;
          tt = std::clamp(tt, 0.f, 1.f);
          float px2 = x0 + vx * tt - fxx, py2 = y0 + vy * tt - fyy;
          return (px2 * px2 + py2 * py2) / (rad * rad);
        };

        // --- head, in its own tilted frame ---
        float ct = std::cos(A.tilt), st = std::sin(A.tilt);
        float hx = (fxx - A.cx) * ct + (fyy - A.cy) * st;
        float hy = -(fxx - A.cx) * st + (fyy - A.cy) * ct;
        float v = hy / A.ry;
        float taper = 1.f - 0.62f * std::max(0.f, v);
        float u = hx / (A.rx * std::max(0.18f, taper));
        float dHead = u * u + v * v;

        // --- shoulder / torso, still mostly beyond the rim ---
        float shX = A.cx - (float)A.side * A.rx * 0.85f;
        float shY = A.cy + A.ry * 1.30f;
        float dBody = ellipse(shX, shY, A.rx * 1.75f, A.ry * 1.15f,
                              (float)-A.side * 0.35f);
        // --- neck ---
        float dNeck = capsule(A.cx, A.cy + A.ry * 0.72f, shX, shY, A.rx * 0.30f);
        // --- arm reaching along the rim to the hand ---
        float elbowX = (shX + A.handX) * 0.5f + (float)A.side * A.rx * 0.30f;
        float elbowY = (shY + A.handY) * 0.5f + A.ry * 0.55f;
        float dArm = std::min(capsule(shX, shY, elbowX, elbowY, A.rx * 0.24f),
                              capsule(elbowX, elbowY, A.handX, A.handY, A.rx * 0.19f));
        // --- hand: palm plus long fingers curling in over the edge ---
        float inwX = (float)W * 0.5f - A.handX, inwY = (float)H * 0.5f - A.handY;
        float inwL = std::sqrt(inwX * inwX + inwY * inwY);
        if (inwL > 0.001f) { inwX /= inwL; inwY /= inwL; }
        float tanX = -inwY, tanY = inwX;
        float dHand = ellipse(A.handX, A.handY, A.rx * 0.34f, A.rx * 0.26f, 0.f);
        for (int fgr = 0; fgr < 4; ++fgr) {
          float off = ((float)fgr - 1.5f) * A.rx * 0.17f;
          float bxf = A.handX + tanX * off, byf = A.handY + tanY * off;
          float tipLen = A.rx * (0.52f - 0.06f * std::fabs((float)fgr - 1.5f));
          float txf = bxf + inwX * tipLen, tyf = byf + inwY * tipLen;
          dHand = std::min(dHand, capsule(bxf, byf, txf, tyf, A.rx * 0.072f));
        }

        // Head only, for now. A full figure (shoulder, arm, a hand round the
        // rim) is computed above but reads as one huge blob at this scale:
        // the torso lands well inside the circle instead of being cropped by
        // it. Left in place, out of the silhouette, until it earns its keep.
        (void)dBody; (void)dNeck; (void)dArm; (void)dHand;
        float dAny = dHead;

        // Atmosphere: it displaces the air it leans through.
        if (dAny >= 1.f && dAny < 2.0f) {
          float haze = (1.f - (dAny - 1.f) / 1.0f) * 0.32f * A.rise;
          rr = rr * (1.f - haze) + 26.f * haze;
          gg = gg * (1.f - haze) + 30.f * haze;
          bb = bb * (1.f - haze) + 44.f * haze;
        }
        if (dAny < 1.f) {
          // Near-black, with a faint rim where the world's glow catches it.
          float rim = std::max(0.f, (dAny - 0.70f) / 0.30f);
          float bR = 12.f + 42.f * rim, bG = 14.f + 50.f * rim, bB = 22.f + 70.f * rim;
          rr = rr * (1.f - A.rise) + bR * A.rise;
          gg = gg * (1.f - A.rise) + bG * A.rise;
          bb = bb * (1.f - A.rise) + bB * A.rise;
        }

        // Eyes last, so nothing paints over them.
        if (dHead < 1.2f) {
          for (int sd = -1; sd <= 1; sd += 2) {
            float ex = (0.44f * (float)sd + A.gaze * 0.10f) * A.rx;
            float ey = -0.10f * A.ry;
            float dx2 = hx - ex, dy2 = hy - ey;
            float sl = 0.42f * (float)sd;
            float rxq = dx2 * std::cos(sl) - dy2 * std::sin(sl);
            float ryq = dx2 * std::sin(sl) + dy2 * std::cos(sl);
            float exr = A.rx * 0.30f, eyr = A.ry * 0.13f * A.blink;
            float q = (rxq * rxq) / (exr * exr) + (ryq * ryq) / (eyr * eyr);
            if (q < 1.f) {
              float core = 1.f - q;
              float pulse = 0.72f + 0.28f * std::sin(animT * 2.1f);
              float p = A.rise * pulse * br * (0.35f + 0.65f * core);
              rr = rr * (1.f - p) + 120.f * p;
              gg = gg * (1.f - p) + 246.f * p;
              bb = bb * (1.f - p) + 238.f * p;
              if (q < 0.22f) { rr += 60.f * p; gg += 20.f * p; bb += 40.f * p; }
            }
          }
        }
      }
    }
  }

  // Alien night: sparse bioluminescent spores drift slowly upward.
  if (w.biome == ALIEN && dl.level < 0.35f && d == 0) {
    uint32_t yy = (uint32_t)((float)y + animT * 1.4f);
    uint32_t sp2 = hash3((uint32_t)x, yy, 0xA11E17u ^ w.worldSeed);
    if ((sp2 % 1500u) == 0u) {
      float p = (0.55f + 0.45f * std::sin(animT * 1.6f + (float)(sp2 & 63u))) *
                displayBrightness();
      if (sp2 & 64u) { rr += 90.f * p; gg += 200.f * p; bb += 210.f * p; }
      else           { rr += 200.f * p; gg += 80.f * p; bb += 220.f * p; }
    }
  }

  // Day life, one signature per biome: butterflies, dragonfly glints,
  // heat shimmer, snow glitter, prismatic motes.
  if (dl.level > 0.85f) {
    float br = displayBrightness();
    if ((w.biome == MEADOW || w.biome == TROPICAL) && d == 0 && e == ' ' &&
        season != WINTER) {
      uint32_t bh = hash3((uint32_t)x, (uint32_t)y, 0xB77E12u ^ w.worldSeed);
      float p = pixelviewMote(bh, animT, 5.0f, 3200u, 3.5f) * br;
      if (p > 0.f) {
        if (w.biome == MEADOW) {
          switch ((bh >> 8) % 3u) {  // cabbage white, gold, monarch
            case 0:  rr += 190.f * p; gg += 190.f * p; bb += 180.f * p; break;
            case 1:  rr += 205.f * p; gg += 175.f * p; bb += 60.f * p; break;
            default: rr += 215.f * p; gg += 120.f * p; bb += 45.f * p; break;
          }
        } else {
          switch ((bh >> 8) % 3u) {  // morpho, orchid, sulphur
            case 0:  rr += 65.f * p;  gg += 160.f * p; bb += 210.f * p; break;
            case 1:  rr += 205.f * p; gg += 65.f * p;  bb += 150.f * p; break;
            default: rr += 205.f * p; gg += 180.f * p; bb += 50.f * p; break;
          }
        }
      }
    }
    if (w.biome == WETLAND && d == 0) {
      bool nearWater = (x > 0 && w.water[y][x-1] > 0) ||
                       (x < W-1 && w.water[y][x+1] > 0) ||
                       (y > 0 && w.water[y-1][x] > 0) ||
                       (y < H-1 && w.water[y+1][x] > 0);
      if (nearWater) {
        uint32_t dh = hash3((uint32_t)x, (uint32_t)y, 0xD2A60Fu ^ w.worldSeed);
        float p = pixelviewMote(dh, animT, 3.0f, 2400u, 5.0f) * br;
        rr += 60.f * p; gg += 205.f * p; bb += 195.f * p;
      }
    }
    if (w.biome == DESERT && d == 0) {
      // Tumbleweed: rolls with the wind, bouncing, one crossing at a time.
      float twSpan = (float)W * 1.4f;
      uint32_t twe = (uint32_t)(animT / 45.f);
      uint32_t twh = hash3(twe, 0x70B1Eu, w.worldSeed);
      float age = animT - (float)twe * 45.f;
      if ((twh % 2u) == 0u && age < 34.f) {
        float wx3 = (w.wind.dx == 0 && w.wind.dy == 0) ? 1.f : (float)w.wind.dx;
        float wy3 = (float)w.wind.dy * 0.4f;
        float n = std::sqrt(wx3 * wx3 + wy3 * wy3);
        float sx0 = ((twh >> 8) & 1023u) / 1023.f * (float)W;
        float sy0 = ((twh >> 18) & 1023u) / 1023.f * (float)H;
        float px2 = sx0 + wx3 / n * (age * twSpan / 34.f - twSpan * 0.5f);
        float py2 = sy0 + wy3 / n * (age * twSpan / 34.f - twSpan * 0.5f) -
                    2.2f * std::fabs(std::sin(age * 2.2f));
        px2 = px2 - std::floor(px2 / (float)W) * (float)W;  // wrap
        float ddx2 = (float)x - px2;
        if (ddx2 > (float)W * 0.5f) ddx2 -= (float)W;
        if (ddx2 < -(float)W * 0.5f) ddx2 += (float)W;
        float ddy2 = (float)y - py2;
        if (ddx2 * ddx2 + ddy2 * ddy2 < 1.2f) {
          rr = 185.f * br; gg = 155.f * br; bb = 105.f * br;
        }
      }
      // Dust devil: occasional wandering spiral of lifted sand.
      uint32_t dde = (uint32_t)(animT / 90.f);
      uint32_t ddh = hash3(dde, 0xD05Eu, w.worldSeed);
      float dage = animT - (float)dde * 90.f;
      if ((ddh % 3u) == 0u && dage < 20.f) {
        float dxc = ((ddh >> 6) & 1023u) / 1023.f * (float)W * 0.7f + (float)W * 0.15f +
                    6.f * std::sin(dage * 0.7f);
        float dyc = ((ddh >> 16) & 1023u) / 1023.f * (float)H * 0.7f + (float)H * 0.15f +
                    6.f * std::cos(dage * 0.5f);
        float ex = (float)x - dxc, ey = (float)y - dyc;
        float dist2 = ex * ex + ey * ey;
        if (dist2 < 6.5f) {
          float swirl = std::sin(std::atan2(ey, ex) * 3.f + animT * 8.f -
                                 std::sqrt(dist2) * 1.8f);
          float f = (1.f - dist2 / 6.5f) * (0.5f + 0.5f * swirl) * 0.5f * br;
          rr += 120.f * f; gg += 105.f * f; bb += 75.f * f;
        }
      }
    }
    if (w.biome == DESERT && dl.level > 0.95f) {
      float sh2 = std::sin((float)y * 0.35f + (float)x * 0.07f + animT * 1.6f) *
                  std::sin((float)x * 0.22f - animT * 1.1f);
      float amp = 3.2f * br;  // barely-there midday waver
      rr += sh2 * amp; gg += sh2 * amp * 0.9f; bb += sh2 * amp * 0.7f;
    }
    if (w.biome == ALPINE && d == 0) {
      uint32_t gh = hash3((uint32_t)x, (uint32_t)y, 0x911770u ^ w.worldSeed);
      float p = pixelviewMote(gh, animT, 2.5f, 3600u, 5.0f) * br;
      p = p * p;  // sharp glints
      rr += 200.f * p; gg += 205.f * p; bb += 215.f * p;
    }
    if (w.biome == ALIEN && d == 0) {
      uint32_t ph2 = hash3((uint32_t)x, (uint32_t)y, 0xA71E20u ^ w.worldSeed);
      float p = pixelviewMote(ph2, animT, 7.0f, 3000u, 1.0f) * br;
      if (p > 0.f) {
        float base = animT * 1.3f + (float)(ph2 & 63u);
        rr += (60.f + 60.f * std::sin(base)) * p;
        gg += (60.f + 60.f * std::sin(base + 2.09f)) * p;
        bb += (60.f + 60.f * std::sin(base + 4.19f)) * p;
      }
    }
  }

  // Offshore cast: the ship on her endless circuit (lantern-lit at night),
  // seabirds skimming the shore by day, an occasional whale, and — rarely
  // — the serpent's humps arcing through the deep. Island mode and the open
  // OCEAN biome both get it.
  if (hasOpenSea(w)) {
    PixelviewCast& C = pixelviewCast(animT);
    float br = displayBrightness();
    float fx = (float)x, fy = (float)y;
    if (C.serpentUp) {
      for (int i = 0; i < 6; i += 2) {
        float dxs = fx - C.serpX[i], dys = fy - C.serpY[i];
        if (dxs * dxs + dys * dys < (i == 0 ? 2.2f : 1.4f)) {
          rr = 26.f * br; gg = 62.f * br; bb = 52.f * br;
          if (i == 0 && dl.level < 0.35f) {  // her eye, by moonlight
            rr = 200.f * br; gg = 40.f * br; bb = 30.f * br;
          }
        }
      }
    }
    if (C.whaleUp) {
      float dxw = fx - C.whaleX, dyw = fy - C.whaleY;
      if ((dxw * dxw) / 9.f + (dyw * dyw) / 2.8f < 1.f) {
        rr = 42.f * br; gg = 52.f * br; bb = 74.f * br;
      }
      if (C.whaleAge < 4.f) {  // the spout, fading
        float sx2 = fx - C.whaleX, sy2 = fy - (C.whaleY - 2.6f);
        if (sx2 * sx2 + sy2 * sy2 < 1.2f) {
          float fd = (1.f - C.whaleAge / 4.f) * br;
          rr = 215.f * fd + rr * (1.f - fd);
          gg = 228.f * fd + gg * (1.f - fd);
          bb = 240.f * fd + bb * (1.f - fd);
        }
      }
    }
    if (C.shipUp && d > 0) {
      float dxs = fx - C.shipX, dys = fy - C.shipY;
      if (dxs * dxs + dys * dys < 2.4f) {  // hull
        rr = 108.f * br; gg = 76.f * br; bb = 48.f * br;
      }
      float sailX = C.shipX - std::sin(C.shipAng) * 1.5f;
      float sailY = C.shipY + std::cos(C.shipAng) * 1.5f;
      float dxl = fx - sailX, dyl = fy - sailY;
      if (dxl * dxl + dyl * dyl < 1.1f) {
        if (dl.level > 0.35f) {  // sail by day
          rr = 232.f * br; gg = 232.f * br; bb = 226.f * br;
        } else {  // lantern by night
          float p = (0.75f + 0.25f * std::sin(animT * 2.3f)) * br;
          rr = 255.f * p; gg = 185.f * p; bb = 90.f * p;
        }
      }
    }
    if (C.pirateUp && d > 0) {
      float dxp = fx - C.pirX, dyp = fy - C.pirY;
      if (dxp * dxp + dyp * dyp < 2.4f) {  // black hull
        rr = 38.f * br; gg = 34.f * br; bb = 36.f * br;
      }
      float sailX = C.pirX - std::sin(C.pirAng) * 1.5f;
      float sailY = C.pirY + std::cos(C.pirAng) * 1.5f;
      float dxl = fx - sailX, dyl = fy - sailY;
      if (dxl * dxl + dyl * dyl < 1.1f) {
        if (dl.level > 0.35f) {  // dark sails by day
          rr = 72.f * br; gg = 66.f * br; bb = 78.f * br;
        } else {  // she runs dark at night — barely a silhouette
          rr = 30.f * br; gg = 28.f * br; bb = 34.f * br;
        }
      }
      // red pennant above the sail
      float pnX = sailX - std::sin(C.pirAng) * 1.4f;
      float pnY = sailY + std::cos(C.pirAng) * 1.4f;
      float dxn = fx - pnX, dyn = fy - pnY;
      if (dxn * dxn + dyn * dyn < 0.5f) {
        rr = 190.f * br; gg = 35.f * br; bb = 30.f * br;
      }
    }
    if (dl.level > 0.5f) {
      for (int i = 0; i < 3; ++i) {
        float dxb = fx - C.birdX[i], dyb = fy - C.birdY[i];
        if (dxb * dxb + dyb * dyb < 0.9f) {
          float flap = 0.75f + 0.25f * std::sin(animT * 7.f + (float)i * 2.1f);
          rr = 238.f * flap * br; gg = 240.f * flap * br; bb = 244.f * flap * br;
        }
      }
    }
  }

  // Volcano: the crater breathes a faint glow when dormant (strongest at
  // night); during an eruption the vent floods with lava light, embers
  // scatter downwind, and smoke hazes the slopes.
  if (w.ventX >= 0) {
    float br = displayBrightness();
    float vdx = (float)x - (float)w.ventX, vdy = (float)y - (float)w.ventY;
    float vd2 = vdx * vdx + vdy * vdy;
    bool erupting = (w.eruptEnd > tick);
    if (!erupting && vd2 < 3.5f) {
      float breathe = 0.5f + 0.5f * std::sin(animT * 0.8f);
      float glow = (0.25f + 0.45f * (1.f - dl.level)) * breathe *
                   (1.f - vd2 / 3.5f) * br;
      rr += 160.f * glow; gg += 55.f * glow; bb += 15.f * glow;
    }
    if (erupting) {
      float fury = std::min(1.f, (float)(w.eruptEnd - tick) / 120.f);
      if (vd2 < 9.f) {  // lava light
        float f = (1.f - vd2 / 9.f) * (0.65f + 0.35f * std::sin(animT * 7.f)) * br;
        rr += 255.f * f * fury; gg += 130.f * f * fury; bb += 25.f * f * fury;
      }
      if (vd2 < 240.f && d == 0) {  // embers drifting downwind
        float wdot = vdx * (float)w.wind.dx + vdy * (float)w.wind.dy;
        uint32_t eh = hash3((uint32_t)x, (uint32_t)y, 0xE38E5u ^ w.worldSeed);
        float em = pixelviewMote(eh, animT, 2.2f, wdot > 0.f ? 240u : 700u, 6.f);
        em *= fury * br;
        rr += 230.f * em; gg += 120.f * em; bb += 25.f * em;
        // smoke haze thickens downwind of the vent
        if (wdot > 0.f) {
          float haze = std::min(1.f, wdot / 16.f) * (1.f - vd2 / 240.f) * 0.35f * fury;
          rr = rr * (1.f - haze) + 70.f * haze * br;
          gg = gg * (1.f - haze) + 66.f * haze * br;
          bb = bb * (1.f - haze) + 68.f * haze * br;
        }
      }
    }
  }

  // Storm lightning: single-tick global flashes (the sim's strikes were
  // invisible at 1px/cell — the whole sky flickering sells the storm).
  if (w.weather.state == STORM && (hash3((uint32_t)tick, 99u, 7u) % 19u) == 0u) {
    rr = rr * 1.5f + 70.f; gg = gg * 1.5f + 70.f; bb = bb * 1.4f + 60.f;
  }

  // Palette harmony: chroma ceiling, split tone, black lift. Last thing
  // before the user's own brightness/contrast controls, so every colour in
  // the frame — terrain, water, weather, cast — passes through one light.
  // ALIEN takes only a third of it: its whole identity is that it emits, and
  // a chroma ceiling is precisely the wrong instrument to point at that.
  // Water keeps its own saturation (ceilAmt 0) — see pixelviewHarmonise.
  pixelviewHarmonise(rr, gg, bb,
                     displayHarmony() * (w.biome == ALIEN ? 0.33f : 1.0f),
                     d > 0 ? 0.0f : 1.0f);

  // Brightness lift (live). A 256-entry table, rebuilt only when the setting
  // moves, keeps this off the per-cell pow() path — it runs on a Pi Zero.
  {
    float lift = displayLift();
    if (lift > 1.001f) {
      static float cachedLift = -1.f;
      static float lut[256];
      if (cachedLift != lift) {
        cachedLift = lift;
        for (int i = 0; i < 256; ++i)
          lut[i] = 255.f * (1.f - std::pow(1.f - (float)i / 255.f, lift));
      }
      rr = lut[clampU8((int)rr)];
      gg = lut[clampU8((int)gg)];
      bb = lut[clampU8((int)bb)];
    }
  }

  // User contrast (live, from the kiosk remote).
  float ck = displayContrast();
  if (ck != 1.0f) {
    rr = (rr - 128.f) * ck + 128.f;
    gg = (gg - 128.f) * ck + 128.f;
    bb = (bb - 128.f) * ck + 128.f;
  }
  return PixelviewRGB{clampU8((int)rr), clampU8((int)gg), clampU8((int)bb)};
}
