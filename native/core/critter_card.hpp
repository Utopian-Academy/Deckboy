// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// critter_card.hpp -- the "critters" test pattern, SDL-free.
//
// Pixel critters bloom from a single pixel, grow, hold and shrink back,
// overlapping forever, in the four Game Boy greens. Cute, and a real test: a
// critter is only ever drawn at a WHOLE-number scale of its 8x8 sprite, so
// every edge on the frame is pixel-exact at every size. Anything soft, ringing
// or shimmering on the screen was added after Deckboy -- a scaler, a
// processor, a projector's keystone -- not by the art.
//
// Everything is a function of the time, so two outputs showing the same cue
// at the same moment show the same critters, and a dump is repeatable.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace deckboy::core::crittercard {

// The DMG greens, darkest to lightest.
inline constexpr std::array<std::array<std::uint8_t, 3>, 4> kShades {{
  {15, 56, 15}, {48, 98, 48}, {139, 172, 15}, {155, 188, 15},
}};

// 8x8 sprites. '.' is see-through; '0'-'3' index kShades. Each has a dark
// outline so overlapping critters stay readable.
inline constexpr int kSpriteCount = 8;
inline constexpr const char* kSprites[kSpriteCount][8] {
  {  // moth
    "0..00..0",
    "00.11.00",
    "01011010",
    "01100110",
    "01100110",
    "01011010",
    "00.00.00",
    "0......0" },
  {  // crab
    "00....00",
    "0.0..0.0",
    ".0.00.0.",
    "00122100",
    "01122110",
    ".000000.",
    "0.0..0.0",
    "........" },
  {  // fish
    "........",
    "..0000..",
    ".011110.",
    "0131110.",
    "011111.0",
    ".011110.",
    "..0000.0",
    "........" },
  {  // cat
    "0.....0.",
    "00...00.",
    "0100010.",
    "0313130.",
    "0111110.",
    "0101010.",
    ".00000..",
    "........" },
  {  // snail
    "........",
    "...000..",
    "..01210.",
    "0.02120.",
    "00.0110.",
    "0110000.",
    "0111110.",
    ".000000." },
  {  // frog
    "........",
    ".00..00.",
    "0310130.",
    "0111110.",
    "0100010.",
    "0111110.",
    "00.0.00.",
    "........" },
  {  // jellyfish
    "..0000..",
    ".012210.",
    "01222210",
    "01111110",
    ".000000.",
    ".0.0.0..",
    "..0.0.0.",
    ".0.0.0.." },
  {  // bird
    "........",
    "..000...",
    ".03110..",
    "001110..",
    ".0111000",
    "..01110.",
    "...000..",
    "...0.0.." },
};

inline std::uint32_t hash32(std::uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}

// Draw the card into RGBA bytes (R, G, B, A; w * h * 4) at time t seconds.
inline void draw(std::uint8_t* rgba, int w, int h, double t) {
  if (!rgba || w <= 0 || h <= 0) return;
  auto put = [&](int x, int y, const std::array<std::uint8_t, 3>& c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    std::uint8_t* p = rgba + (static_cast<std::size_t>(y) * w + x) * 4;
    p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
  };
  // The ground, and the cells the critters are born in: square, a twelfth of
  // the height, through the middle -- the same grid as the output test card.
  const int cell = std::max(8, static_cast<int>(std::lround(h / 12.0)));
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) put(x, y, kShades[3]);
  }
  const int ox = (w / 2) % cell, oy = (h / 2) % cell;
  for (int x = ox; x < w; x += cell) {
    for (int y = 0; y < h; ++y) put(x, y, kShades[2]);
  }
  for (int y = oy; y < h; y += cell) {
    for (int x = 0; x < w; ++x) put(x, y, kShades[2]);
  }

  // Twelve lives at once, staggered across a ten-second life. Each life picks
  // its critter and its cell from the slot and the generation, so it is a
  // new one every time and the same one for everybody watching.
  constexpr int kSlots = 12;
  constexpr double kLife = 10.0;
  // Biggest scale: about three cells across at the top of a life, and born
  // only in cells far enough in that the biggest never leaves the frame.
  const int maxScale = std::max(1, (cell * 3) / 8);
  const int reach = (8 * maxScale) / 2;
  const int firstCol = (reach - ox - cell / 2 + cell - 1) / cell;
  const int firstRow = (reach - oy - cell / 2 + cell - 1) / cell;
  const int lastCol = (w - reach - ox - cell / 2) / cell;
  const int lastRow = (h - reach - oy - cell / 2) / cell;
  const int col0 = std::max(0, firstCol), row0 = std::max(0, firstRow);
  const int cols = std::max(1, lastCol - col0 + 1);
  const int rows = std::max(1, lastRow - row0 + 1);
  for (int slot = 0; slot < kSlots; ++slot) {
    const double age = t + slot * (kLife / kSlots);
    const auto generation = static_cast<std::uint32_t>(std::floor(age / kLife));
    const double phase = (age - generation * kLife) / kLife;   // 0..1 through this life
    const std::uint32_t seed = hash32(static_cast<std::uint32_t>(slot) * 7919u + generation * 104729u);
    const int sprite = static_cast<int>(seed % kSpriteCount);
    const int cx = ox + (col0 + static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(cols))) * cell + cell / 2;
    const int cy = oy + (row0 + static_cast<int>((seed >> 20) % static_cast<std::uint32_t>(rows))) * cell + cell / 2;
    const bool flip = (seed >> 30) & 1u;

    // The first and last tenth of a life is the single cell it grows from:
    // one pixel, blinking. Between, a smooth swell -- quantised to whole
    // scales, so it steps up and down a pixel-size at a time.
    if (phase < 0.1 || phase > 0.9) {
      if (static_cast<int>(age * 4.0) % 2 == 0) put(cx, cy, kShades[0]);
      continue;
    }
    const double swell = std::sin((phase - 0.1) / 0.8 * 3.14159265358979);
    const int scale = std::clamp(1 + static_cast<int>(std::floor(swell * maxScale)), 1, maxScale);
    const int size = 8 * scale;
    const int left = cx - size / 2, top = cy - size / 2;
    for (int sy = 0; sy < 8; ++sy) {
      for (int sx = 0; sx < 8; ++sx) {
        const char c = kSprites[sprite][sy][flip ? 7 - sx : sx];
        if (c < '0' || c > '3') continue;
        const auto& shade = kShades[static_cast<std::size_t>(c - '0')];
        for (int py = 0; py < scale; ++py) {
          for (int px = 0; px < scale; ++px) put(left + sx * scale + px, top + sy * scale + py, shade);
        }
      }
    }
  }
}

}  // namespace deckboy::core::crittercard
