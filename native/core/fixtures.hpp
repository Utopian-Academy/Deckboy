// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#pragma once

// THE FIXTURE PATCH: named lights at a universe and an address, and what a DMX
// cue's "this fixture at this level in this colour" means in channel values.
// SDL-free, so the conversion can be checked without a network or a window.
//
// Six types, which cover house lights, practicals, LED washes and uplights.
// No moving heads: pan and tilt want a position picker of their own.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace deckboy::fixtures {

// Appended, never reordered: shows store the type as its index.
enum class FixtureType : int { Dimmer = 0, Rgb, Rgbw, Rgba, RgbDimmer, DimmerCct, Count };

struct Fixture {
  std::string name;
  FixtureType type = FixtureType::Dimmer;
  int universe = 0;   // Art-Net port address, or sACN universe
  int address = 1;    // first channel, 1-512
  int videoDip = -1;  // on a video roll: -1 leave it, else the % it dips to
};

inline int footprint(FixtureType type) {
  switch (type) {
    case FixtureType::Dimmer: return 1;
    case FixtureType::Rgb: return 3;
    case FixtureType::Rgbw: return 4;
    case FixtureType::Rgba: return 4;
    case FixtureType::RgbDimmer: return 4;
    case FixtureType::DimmerCct: return 2;
    default: return 1;
  }
}

inline const char* fixtureTypeLabel(FixtureType type) {
  switch (type) {
    case FixtureType::Rgb: return "RGB";
    case FixtureType::Rgbw: return "RGBW";
    case FixtureType::Rgba: return "RGBA";
    case FixtureType::RgbDimmer: return "Dimmer + RGB";
    case FixtureType::DimmerCct: return "Dimmer + temperature";
    case FixtureType::Dimmer:
    default: return "Dimmer";
  }
}

// For commands: dimmer, rgb, rgbw, rgba, drgb, dcct.
inline const char* fixtureTypeToken(FixtureType type) {
  switch (type) {
    case FixtureType::Rgb: return "rgb";
    case FixtureType::Rgbw: return "rgbw";
    case FixtureType::Rgba: return "rgba";
    case FixtureType::RgbDimmer: return "drgb";
    case FixtureType::DimmerCct: return "dcct";
    case FixtureType::Dimmer:
    default: return "dimmer";
  }
}

inline bool fixtureTypeFromToken(const std::string& token, FixtureType& out) {
  for (int i = 0; i < static_cast<int>(FixtureType::Count); ++i) {
    if (token == fixtureTypeToken(static_cast<FixtureType>(i))) {
      out = static_cast<FixtureType>(i);
      return true;
    }
  }
  return false;
}

inline bool fixtureHasColour(FixtureType type) {
  return type != FixtureType::Dimmer;
}

// The colour chip's steps. -1 is "no colour": white on a colour fixture.
struct ColourPreset { const char* name; float r, g, b; };
constexpr int kColourCount = 8;
inline const std::array<ColourPreset, kColourCount>& colourPresets() {
  static const std::array<ColourPreset, kColourCount> presets {{
    {"white", 1.0f, 1.0f, 1.0f},
    {"warm", 1.0f, 0.72f, 0.42f},
    {"red", 1.0f, 0.0f, 0.0f},
    {"amber", 1.0f, 0.55f, 0.0f},
    {"green", 0.0f, 1.0f, 0.0f},
    {"cyan", 0.0f, 1.0f, 1.0f},
    {"blue", 0.0f, 0.25f, 1.0f},
    {"magenta", 1.0f, 0.0f, 1.0f},
  }};
  return presets;
}

inline const char* colourName(int colour) {
  return colour >= 0 && colour < kColourCount ? colourPresets()[static_cast<std::size_t>(colour)].name
                                              : "no colour";
}

inline int colourFromName(const std::string& name) {
  for (int i = 0; i < kColourCount; ++i) {
    if (name == colourPresets()[static_cast<std::size_t>(i)].name) return i;
  }
  return -2;   // not a colour
}

// The colour a fixture shows, 0-1 RGB, after a hue LFO has moved it round
// the wheel by `hueTurn` (0-1 of a turn). White and warm have no hue of their
// own, so a moving colour on them starts from red.
inline std::array<float, 3> colourRgb(int colour, double hueTurn) {
  const ColourPreset& p = colourPresets()[static_cast<std::size_t>(std::clamp(colour, 0, kColourCount - 1))];
  float r = p.r, g = p.g, b = p.b;
  if (std::fabs(hueTurn) < 1e-6) return {r, g, b};
  const float maxc = std::max({r, g, b}), minc = std::min({r, g, b});
  double hue = 0.0;
  double sat = maxc > 0.0f ? (maxc - minc) / maxc : 0.0;
  if (sat < 0.05) {
    sat = 1.0;   // white or warm: go round from red
  } else if (maxc == r) {
    hue = std::fmod((g - b) / (maxc - minc), 6.0) / 6.0;
  } else if (maxc == g) {
    hue = ((b - r) / (maxc - minc) + 2.0) / 6.0;
  } else {
    hue = ((r - g) / (maxc - minc) + 4.0) / 6.0;
  }
  hue = hue + hueTurn;
  hue -= std::floor(hue);
  const double h6 = hue * 6.0;
  const double x = 1.0 - std::fabs(std::fmod(h6, 2.0) - 1.0);
  double rr = 0, gg = 0, bb = 0;
  switch (static_cast<int>(h6) % 6) {
    case 0: rr = 1; gg = x; break;
    case 1: rr = x; gg = 1; break;
    case 2: gg = 1; bb = x; break;
    case 3: gg = x; bb = 1; break;
    case 4: rr = x; bb = 1; break;
    default: rr = 1; bb = x; break;
  }
  const double v = maxc > 0.0f ? maxc : 1.0;
  const double m = v * (1.0 - sat);
  return {static_cast<float>(m + (v - m) * rr), static_cast<float>(m + (v - m) * gg),
          static_cast<float>(m + (v - m) * bb)};
}

inline std::uint8_t level8(double v) {
  return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
}

// "This fixture at `intensity` (0-1) in `colour`" as (channel, value) pairs,
// channels counted from 1. A channel past 512 is dropped rather than wrapped.
inline std::vector<std::pair<int, std::uint8_t>> fixtureChannels(const Fixture& fixture, double intensity,
                                                                 int colour, double hueTurn = 0.0) {
  std::vector<std::uint8_t> values;
  const double i = std::clamp(intensity, 0.0, 1.0);
  const int c = colour < 0 ? 0 : colour;   // no colour = white
  const auto rgb = colourRgb(c, hueTurn);
  switch (fixture.type) {
    case FixtureType::Dimmer:
      values = {level8(i)};
      break;
    case FixtureType::Rgb:
      values = {level8(rgb[0] * i), level8(rgb[1] * i), level8(rgb[2] * i)};
      break;
    case FixtureType::Rgbw: {
      // The white the colour contains goes to the white emitter.
      const float w = std::min({rgb[0], rgb[1], rgb[2]});
      values = {level8((rgb[0] - w) * i), level8((rgb[1] - w) * i), level8((rgb[2] - w) * i), level8(w * i)};
      break;
    }
    case FixtureType::Rgba: {
      // Amber and warm lean on the amber emitter.
      const bool amberish = std::fabs(hueTurn) < 1e-6 && (c == 1 || c == 3);
      const double a = amberish ? 1.0 : 0.0;
      const double scale = amberish ? 0.35 : 1.0;
      values = {level8(rgb[0] * i * scale), level8(rgb[1] * i * scale), level8(rgb[2] * i * scale), level8(a * i)};
      break;
    }
    case FixtureType::RgbDimmer:
      values = {level8(i), level8(rgb[0]), level8(rgb[1]), level8(rgb[2])};
      break;
    case FixtureType::DimmerCct: {
      // Warm is the low end of the temperature channel, white the high end.
      const double cct = colour == 1 ? 0.0 : colour == 0 || colour < 0 ? 1.0 : 0.5;
      values = {level8(i), level8(cct)};
      break;
    }
    default:
      break;
  }
  std::vector<std::pair<int, std::uint8_t>> out;
  for (std::size_t k = 0; k < values.size(); ++k) {
    const int channel = fixture.address + static_cast<int>(k);
    if (channel >= 1 && channel <= 512) out.emplace_back(channel, values[k]);
  }
  return out;
}

// The channels of a fixture that carry its brightness (offsets from its
// address): the dimmer where it has one, every emitter where it has not.
inline std::vector<int> intensityOffsets(FixtureType type) {
  switch (type) {
    case FixtureType::Rgb: return {0, 1, 2};
    case FixtureType::Rgbw:
    case FixtureType::Rgba: return {0, 1, 2, 3};
    case FixtureType::Dimmer:
    case FixtureType::RgbDimmer:
    case FixtureType::DimmerCct:
    default: return {0};
  }
}

// The video-roll dip, applied to a universe's outgoing levels: the fixture's
// brightness is CEILINGED at `dip` (0-1), reached in proportion `amount`
// (0 = no dip, 1 = fully dipped). A fixture already below the dip is left;
// a colour fixture is scaled evenly so its colour holds.
inline void applyDip(std::uint8_t* levels512, const Fixture& fixture, double dip, double amount) {
  if (fixture.videoDip < 0 || amount <= 0.0) return;
  const auto offsets = intensityOffsets(fixture.type);
  int peak = 0;
  for (int o : offsets) {
    const int ch = fixture.address + o;
    if (ch >= 1 && ch <= 512) peak = std::max(peak, static_cast<int>(levels512[ch - 1]));
  }
  if (peak == 0) return;
  const double current = peak / 255.0;
  const double target = std::min(current, std::clamp(dip, 0.0, 1.0));
  const double out = current + (target - current) * std::clamp(amount, 0.0, 1.0);
  const double factor = out / current;
  for (int o : offsets) {
    const int ch = fixture.address + o;
    if (ch >= 1 && ch <= 512) levels512[ch - 1] = level8(levels512[ch - 1] / 255.0 * factor);
  }
}

// The first address on `universe` where a fixture of `type` fits without
// overlapping another, or -1 when the universe is full.
inline int nextFreeAddress(const std::vector<Fixture>& patch, int universe, FixtureType type,
                           int skipIndex = -1) {
  const int need = footprint(type);
  for (int start = 1; start + need - 1 <= 512; ++start) {
    bool clear = true;
    for (std::size_t k = 0; k < patch.size() && clear; ++k) {
      if (static_cast<int>(k) == skipIndex || patch[k].universe != universe) continue;
      const int a = patch[k].address, b = a + footprint(patch[k].type) - 1;
      clear = start + need - 1 < a || start > b;
    }
    if (clear) return start;
  }
  return -1;
}

// The fixture this one overlaps, or -1.
inline int clashWith(const std::vector<Fixture>& patch, std::size_t index) {
  const Fixture& f = patch[index];
  const int a0 = f.address, a1 = a0 + footprint(f.type) - 1;
  for (std::size_t k = 0; k < patch.size(); ++k) {
    if (k == index || patch[k].universe != f.universe) continue;
    const int b0 = patch[k].address, b1 = b0 + footprint(patch[k].type) - 1;
    if (a0 <= b1 && b0 <= a1) return static_cast<int>(k);
  }
  return -1;
}

}  // namespace deckboy::fixtures
