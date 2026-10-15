// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// test_card.hpp -- the output test card's geometry, SDL-free.
//
// One card that answers what you walk up to a screen to find out: is it
// 1:1, is it the right shape, are the edges on the glass, are the levels
// right, is it smooth, and which screen is this. It is drawn at the raster
// the output really composites at -- never scaled -- because a 1-pixel line
// is only a test of 1:1 mapping if nothing in between resamples it.
//
// Two halves:
//   CANVAS (drawCanvas): grid, circle, colour bars, grey step, PLUGE and the
//     motion lane. Drawn once over the whole raster -- over the whole CANVAS
//     when outputs span one -- and cached, so a spanned wall shows one
//     continuous picture and a break between panels is a fault you can see.
//   PER SCREEN (screenBoxes): what belongs to each display -- its border and
//     edge arrows, its safe areas, the moving parts. A handful of rectangles a
//     frame, filled by the caller's renderer.
// The same functions build the exported movie, so the file is the card.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace deckboy::core::testcard {

struct Rgba {
  std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct Box {
  int x = 0, y = 0, w = 0, h = 0;
  Rgba c;
};

// Every measurement is in grid cells, and a cell is a twelfth of the height,
// so the card has the same proportions at 720p and at 4K and its cells are
// square on any raster.
struct Layout {
  int w = 0, h = 0;
  int cell = 8;
  int cx = 0, cy = 0;
  int radius = 0;
};

inline Layout layoutFor(int w, int h) {
  Layout l;
  l.w = std::max(1, w);
  l.h = std::max(1, h);
  l.cell = std::max(8, static_cast<int>(std::lround(l.h / 12.0)));
  l.cx = l.w / 2;
  l.cy = l.h / 2;
  l.radius = std::min(l.h / 2 - 1, static_cast<int>(std::lround(l.cell * 5.25)));
  return l;
}

inline constexpr Rgba kBackground {56, 56, 56, 255};
inline constexpr Rgba kGridLine {200, 200, 200, 255};
inline constexpr Rgba kWhite {255, 255, 255, 255};
inline constexpr Rgba kBlack {0, 0, 0, 255};
inline constexpr Rgba kActionSafe {232, 200, 64, 255};
inline constexpr Rgba kTitleSafe {80, 200, 232, 255};
inline constexpr Rgba kBlend {255, 160, 40, 255};

// 75% bars, the order every scope expects.
inline constexpr Rgba kBars[7] {
  {191, 191, 191, 255}, {191, 191, 0, 255}, {0, 191, 191, 255}, {0, 191, 0, 255},
  {191, 0, 191, 255},   {191, 0, 0, 255},   {0, 0, 191, 255},
};

// The bands inside the circle, as cell offsets from the centre. The middle
// (-2..+2) is left for the per-screen box: the screen's number and what it
// is being sent.
inline constexpr double kBarsTop = -4.0, kBarsBottom = -2.0;
inline constexpr double kGreysTop = 2.0, kGreysBottom = 3.0;
inline constexpr double kPlugeTop = 3.0, kPlugeBottom = 3.75;
inline constexpr double kLaneTop = 4.0, kLaneBottom = 4.75;
inline constexpr double kBandHalfWidth = 5.0;   // cells either side of centre

inline int cellY(const Layout& l, double cells) {
  return l.cy + static_cast<int>(std::lround(cells * l.cell));
}

// Draw the canvas half into RGBA bytes (R, G, B, A; w * h * 4).
inline void drawCanvas(std::uint8_t* rgba, int w, int h) {
  if (!rgba || w <= 0 || h <= 0) return;
  const Layout l = layoutFor(w, h);
  auto put = [&](int x, int y, Rgba c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    std::uint8_t* p = rgba + (static_cast<std::size_t>(y) * w + x) * 4;
    p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
  };
  auto span = [&](int x0, int x1, int y, Rgba c) {
    x0 = std::max(0, x0); x1 = std::min(w, x1);
    for (int x = x0; x < x1; ++x) put(x, y, c);
  };
  for (int y = 0; y < h; ++y) span(0, w, y, kBackground);

  // The grid runs through the centre, so it is symmetric on any raster and
  // the middle line of each axis lands on the middle pixel.
  for (int x = l.cx % l.cell; x < w; x += l.cell) {
    for (int y = 0; y < h; ++y) put(x, y, kGridLine);
  }
  for (int y = l.cy % l.cell; y < h; y += l.cell) span(0, w, y, kGridLine);

  // Bands clipped to the circle, scanline by scanline.
  const double r = static_cast<double>(l.radius);
  auto chord = [&](int y, int& x0, int& x1) {
    const double dy = (y + 0.5) - l.cy;
    if (std::abs(dy) >= r) return false;
    const double half = std::sqrt(r * r - dy * dy);
    x0 = static_cast<int>(std::lround(l.cx - half));
    x1 = static_cast<int>(std::lround(l.cx + half));
    return true;
  };
  const int bandL = l.cx - static_cast<int>(std::lround(kBandHalfWidth * l.cell));
  const int bandR = l.cx + static_cast<int>(std::lround(kBandHalfWidth * l.cell));
  auto band = [&](double top, double bottom, auto colourAt) {
    for (int y = cellY(l, top); y < cellY(l, bottom); ++y) {
      int x0 = 0, x1 = 0;
      if (!chord(y, x0, x1)) continue;
      for (int x = std::max(x0, bandL); x < std::min(x1, bandR); ++x) put(x, y, colourAt(x));
    }
  };
  const int bandW = std::max(1, bandR - bandL);
  band(kBarsTop, kBarsBottom, [&](int x) { return kBars[std::clamp((x - bandL) * 7 / bandW, 0, 6)]; });
  band(kGreysTop, kGreysBottom, [&](int x) {
    const int step = std::clamp((x - bandL) * 11 / bandW, 0, 10);
    const auto v = static_cast<std::uint8_t>(std::lround(step * 25.5));
    return Rgba {v, v, v, 255};
  });
  // PLUGE, the RGB way: black with 2% and 4% above it (sub-black does not
  // exist in full-range RGB), then 100% white. Set the brightness so 2% just
  // disappears and 4% just shows.
  band(kPlugeTop, kPlugeBottom, [&](int x) {
    const int part = std::clamp((x - bandL) * 6 / bandW, 0, 5);
    switch (part) {
      case 1: return Rgba {5, 5, 5, 255};
      case 3: return Rgba {10, 10, 10, 255};
      case 5: return kWhite;
      default: return kBlack;
    }
  });

  // The ring, two cells' worth of anti-alias-free pixels: a circle that must
  // look round on the screen. Oval means the raster is being stretched.
  const int ring = std::max(2, l.cell / 24);
  const double inner2 = (r - ring) * (r - ring), outer2 = r * r;
  for (int y = std::max(0, l.cy - l.radius); y < std::min(h, l.cy + l.radius + 1); ++y) {
    const double dy = (y + 0.5) - l.cy;
    for (int x = std::max(0, l.cx - l.radius); x < std::min(w, l.cx + l.radius + 1); ++x) {
      const double dx = (x + 0.5) - l.cx;
      const double d2 = dx * dx + dy * dy;
      if (d2 >= inner2 && d2 < outer2) put(x, y, kWhite);
    }
  }

  // The motion lane runs the full width -- across every panel of a canvas --
  // with a tick every cell, so a sweep that judders or tears reads against
  // something fixed.
  for (int y = cellY(l, kLaneTop); y < cellY(l, kLaneBottom); ++y) span(0, w, y, kBlack);
  const int tickH = std::max(2, l.cell / 6);
  for (int x = l.cx % l.cell; x < w; x += l.cell) {
    for (int y = cellY(l, kLaneBottom) - tickH; y < cellY(l, kLaneBottom); ++y) put(x, y, kGridLine);
  }
}

// A 1-pixel checker and a 1-pixel line grating side by side, cell x cell
// each: they read as flat grey when they arrive 1:1 and as moire, beats or
// mush when anything has scaled them.
inline void drawResolutionPatch(std::uint8_t* rgba, int cell) {
  const int w = cell * 2;
  for (int y = 0; y < cell; ++y) {
    for (int x = 0; x < w; ++x) {
      const bool on = x < cell ? ((x + y) % 2 == 0) : (x % 2 == 0);
      std::uint8_t* p = rgba + (static_cast<std::size_t>(y) * w + x) * 4;
      p[0] = p[1] = p[2] = on ? 255 : 0;
      p[3] = 255;
    }
  }
}

// Seven-segment digits from rectangles: crisp at any size, no font, and a
// counter that a camera can read off a wall.
inline void sevenSegment(int x, int y, int digitH, unsigned long long value, int digits, Rgba on,
                         Rgba off, std::vector<Box>& out) {
  static constexpr std::uint8_t kSegs[10] {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  const int dh = std::max(7, digitH);
  const int t = std::max(1, dh / 9);
  const int dw = dh / 2;
  const int gap = std::max(2, dh / 5);
  for (int i = digits - 1; i >= 0; --i) {
    const int d = static_cast<int>(value % 10);
    value /= 10;
    const int ox = x + i * (dw + gap);
    const int mid = y + dh / 2 - t / 2;
    const Box seg[7] {
      {ox + t, y, dw - 2 * t, t, {}},                         // a
      {ox + dw - t, y + t, t, dh / 2 - t, {}},                // b
      {ox + dw - t, y + dh / 2, t, dh / 2 - t, {}},           // c
      {ox + t, y + dh - t, dw - 2 * t, t, {}},                // d
      {ox, y + dh / 2, t, dh / 2 - t, {}},                    // e
      {ox, y + t, t, dh / 2 - t, {}},                         // f
      {ox + t, mid, dw - 2 * t, t, {}},                       // g
    };
    for (int s = 0; s < 7; ++s) {
      Box b = seg[s];
      b.c = (kSegs[d] >> s) & 1 ? on : off;
      if (b.c.a) out.push_back(b);
    }
  }
}

inline int sevenSegmentWidth(int digitH, int digits) {
  const int dh = std::max(7, digitH);
  return digits * (dh / 2) + (digits - 1) * std::max(2, dh / 5);
}

// One screen's view of the card: the rectangle of the raster that display
// shows (the whole raster, or its piece of a canvas).
struct Screen {
  int x = 0, y = 0, w = 0, h = 0;
  int number = 1;               // the output's number, 1-based
  double refreshHz = 60.0;      // what the frame counter counts in
  float blendL = 0, blendR = 0, blendT = 0, blendB = 0;   // edge-blend widths, fractions
};

// Where the per-screen information box sits, in raster pixels.
inline Box infoBox(const Screen& s) {
  const Layout l = layoutFor(s.w, s.h);
  return {s.x + l.cx - 9 * l.cell / 2, s.y + l.cy - 2 * l.cell, 9 * l.cell, 4 * l.cell, kBlack};
}

// The boxes a screen adds each frame. tSeconds is ONE clock shared by every
// output, so outputs in sync show the same sweep position and the same count,
// and outputs that are not, do not -- photograph the wall and compare.
inline void screenBoxes(const Screen& s, const Layout& canvas, double tSeconds, std::vector<Box>& out) {
  const Layout l = layoutFor(s.w, s.h);
  // The edge of the glass: a 1-pixel border and an arrow at the middle of
  // each side whose tip must touch it. A missing border or a blunt arrow is
  // overscan or a crop.
  const int x0 = s.x, y0 = s.y, x1 = s.x + s.w - 1, y1 = s.y + s.h - 1;
  out.push_back({x0, y0, s.w, 1, kWhite});
  out.push_back({x0, y1, s.w, 1, kWhite});
  out.push_back({x0, y0, 1, s.h, kWhite});
  out.push_back({x1, y0, 1, s.h, kWhite});
  const int arrow = std::max(4, l.cell / 2);
  for (int i = 0; i < arrow; ++i) {
    const int half = i * 3 / 5;
    const int mx = s.x + s.w / 2, my = s.y + s.h / 2;
    out.push_back({mx - half, y0 + i, half * 2 + 1, 1, kWhite});
    out.push_back({mx - half, y1 - i, half * 2 + 1, 1, kWhite});
    out.push_back({x0 + i, my - half, 1, half * 2 + 1, kWhite});
    out.push_back({x1 - i, my - half, 1, half * 2 + 1, kWhite});
  }
  // Safe areas: action 93%, title 90% (SMPTE ST 2046-1 / EBU R 95).
  auto outline = [&](double frac, Rgba c) {
    const int mx = static_cast<int>(std::lround(s.w * (1.0 - frac) / 2.0));
    const int my = static_cast<int>(std::lround(s.h * (1.0 - frac) / 2.0));
    const int bw = s.w - 2 * mx, bh = s.h - 2 * my;
    out.push_back({s.x + mx, s.y + my, bw, 1, c});
    out.push_back({s.x + mx, s.y + my + bh - 1, bw, 1, c});
    out.push_back({s.x + mx, s.y + my, 1, bh, c});
    out.push_back({s.x + mx + bw - 1, s.y + my, 1, bh, c});
  };
  outline(0.93, kActionSafe);
  outline(0.90, kTitleSafe);
  // Edge blend: where this output's blend begins, as a dashed line. The
  // blend itself is applied after the card, so the ramp is visible beyond it.
  auto dashedV = [&](int x) {
    for (int y = s.y; y < s.y + s.h; y += l.cell / 2) out.push_back({x, y, 2, std::max(2, l.cell / 4), kBlend});
  };
  auto dashedH = [&](int y) {
    for (int x = s.x; x < s.x + s.w; x += l.cell / 2) out.push_back({x, y, std::max(2, l.cell / 4), 2, kBlend});
  };
  if (s.blendL > 0.001f) dashedV(s.x + static_cast<int>(std::lround(s.blendL * s.w)));
  if (s.blendR > 0.001f) dashedV(s.x + s.w - 1 - static_cast<int>(std::lround(s.blendR * s.w)));
  if (s.blendT > 0.001f) dashedH(s.y + static_cast<int>(std::lround(s.blendT * s.h)));
  if (s.blendB > 0.001f) dashedH(s.y + s.h - 1 - static_cast<int>(std::lround(s.blendB * s.h)));

  // THE SWEEP, in canvas coordinates: one bar crossing the whole canvas
  // every four seconds, so on a spanned wall it travels from panel to panel.
  const double period = 4.0;
  const double phase = std::fmod(std::max(0.0, tSeconds), period) / period;
  const int barW = std::max(2, canvas.cell / 4);
  const int laneY = cellY(canvas, kLaneTop);
  const int laneH = cellY(canvas, kLaneBottom) - laneY;
  const int sweepX = static_cast<int>(phase * (canvas.w + barW)) - barW;
  out.push_back({sweepX, laneY, barW, laneH, kWhite});

  // The info box (the caller writes the screen's number and details in it)
  // and a frame counter at this screen's refresh: a dropped frame skips one.
  const Box info = infoBox(s);
  out.push_back(info);
  const unsigned long long frame = static_cast<unsigned long long>(std::floor(std::max(0.0, tSeconds) * s.refreshHz));
  const int counterH = std::max(10, l.cell * 2 / 3);
  const int counterW = sevenSegmentWidth(counterH, 7);
  sevenSegment(info.x + info.w - counterW - l.cell / 3, info.y + info.h - counterH - l.cell / 3, counterH, frame % 10000000ULL,
               7, Rgba {120, 230, 140, 255}, Rgba {22, 34, 24, 255}, out);
  // Phase blocks: ten squares, one lit, stepping each frame. Two screens
  // showing different squares at the same instant are out of step by that
  // many frames.
  const int block = std::max(4, l.cell / 3);
  const int blocksX = info.x + l.cell / 2;
  const int blocksY = info.y + info.h - block - l.cell / 3;
  for (int i = 0; i < 10; ++i) {
    const bool lit = static_cast<int>(frame % 10) == i;
    out.push_back({blocksX + i * (block + block / 3), blocksY, block, block,
                   lit ? Rgba {255, 255, 255, 255} : Rgba {40, 40, 40, 255}});
  }
}

}  // namespace deckboy::core::testcard
