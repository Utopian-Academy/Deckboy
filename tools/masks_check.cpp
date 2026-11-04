// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
//
// The mask raster, checked against geometry it can be held to: areas, edges,
// symmetry, the order paths combine in, and the save format's round trip.
// Run by CTest (`masks`); no window, no SDL.
//
//   deckboy-masks-check [--bench WxH] [--dump shape out.pgm WxH]

#include "core/masks.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

using namespace deckboy::masks;

// "1920x1080" into two numbers, without sscanf (MSVC warns on it).
static void parseSize(const char* text, int& W, int& H) {
  char* end = nullptr;
  const long w = std::strtol(text, &end, 10);
  if (end && (*end == 'x' || *end == 'X')) {
    const long h = std::strtol(end + 1, nullptr, 10);
    if (w > 0 && h > 0) { W = static_cast<int>(w); H = static_cast<int>(h); }
  }
}

static int failures = 0;

static void check(const char* what, bool ok, double got = 0.0) {
  std::printf("  %-64s %s", what, ok ? "ok" : "FAIL");
  if (!ok) std::printf("  (%g)", got);
  std::printf("\n");
  if (!ok) ++failures;
}

static double area(const std::vector<std::uint8_t>& a) {
  double s = 0.0;
  for (std::uint8_t v : a) s += v / 255.0;
  return s;
}

static Mask single(MaskShape shape, int W, int H) {
  Mask m;
  m.id = "m1";
  m.name = "test";
  m.paths.push_back(makeMaskShape(shape, static_cast<double>(W) / H));
  return m;
}

static std::uint8_t at(const std::vector<std::uint8_t>& a, int W, int x, int y) {
  return a[static_cast<std::size_t>(y) * W + x];
}

static void writePgm(const std::string& path, const std::vector<std::uint8_t>& a, int W, int H) {
  std::ofstream f(path, std::ios::binary);
  f << "P5\n" << W << ' ' << H << "\n255\n";
  f.write(reinterpret_cast<const char*>(a.data()), static_cast<std::streamsize>(a.size()));
}

int main(int argc, char** argv) {
  if (argc >= 5 && std::strcmp(argv[1], "--dump") == 0) {
    int W = 1280, H = 720;
    parseSize(argv[4], W, H);
    MaskShape shape = MaskShape::Circle;
    maskShapeFromToken(argv[2], shape);
    writePgm(argv[3], rasterizeMask(single(shape, W, H), W, H), W, H);
    return 0;
  }
  // --profile feather: one row through a feathered circle's edge.
  if (argc >= 3 && std::strcmp(argv[1], "--profile") == 0) {
    const int W = 640, H = 360;
    Mask m = single(MaskShape::Circle, W, H);
    m.feather = static_cast<float>(std::atof(argv[2]));
    const auto a = rasterizeMask(m, W, H);
    for (int x = W / 2 + 60; x < W / 2 + 160; x += 4) std::printf("%d:%d ", x - W / 2, at(a, W, x, H / 2));
    std::printf("\n");
    return 0;
  }
  if (argc >= 3 && std::strcmp(argv[1], "--bench") == 0) {
    int W = 1920, H = 1080;
    parseSize(argv[2], W, H);
    Mask m = single(MaskShape::Star, W, H);
    for (int feathered = 0; feathered < 2; ++feathered) {
      m.feather = feathered ? 0.03f : 0.0f;
      const auto t0 = std::chrono::steady_clock::now();
      for (int i = 0; i < 10; ++i) rasterizeMask(m, W, H);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10;
      std::printf("%dx%d star, %s: %.2f ms\n", W, H, feathered ? "feathered" : "hard edge", ms);
    }
    return 0;
  }

  const int W = 640, H = 360;
  const double pi = 3.14159265358979;
  std::printf("masks: the raster\n");

  // A circle of radius 0.30 of the height: area pi r^2, round on a 16:9 frame.
  {
    const auto a = rasterizeMask(single(MaskShape::Circle, W, H), W, H);
    const double r = 0.30 * H, want = pi * r * r, got = area(a);
    check("circle area is pi r^2 within 0.5%", std::fabs(got - want) / want < 0.005, got / want);
    // Width equals height on screen: count the middle row and column.
    int across = 0, down = 0;
    for (int x = 0; x < W; ++x) across += at(a, W, x, H / 2) > 127;
    for (int y = 0; y < H; ++y) down += at(a, W, W / 2, y) > 127;
    check("...and as wide as it is tall (round, not an egg)", std::abs(across - down) <= 2, across - down);
    check("...with soft edges, not stairs", [&] {
      int partial = 0;
      for (std::uint8_t v : a) partial += v > 10 && v < 245;
      return partial > 200;
    }(), 0);
  }

  // Rectangle: exact area, and its corners are corners.
  {
    const auto a = rasterizeMask(single(MaskShape::Rectangle, W, H), W, H);
    const double want = (0.84 * H) * (0.56 * H);
    check("rectangle area is exact within 0.2%", std::fabs(area(a) - want) / want < 0.002, area(a) / want);
  }

  // Every shape fills something, sits in the frame, and is centred.
  for (int i = 0; i < static_cast<int>(MaskShape::Count); ++i) {
    const MaskShape shape = static_cast<MaskShape>(i);
    const auto a = rasterizeMask(single(shape, W, H), W, H);
    double sx = 0, sy = 0, s = 0;
    bool edge = false;
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        const double v = at(a, W, x, y) / 255.0;
        sx += v * x; sy += v * y; s += v;
        if (v > 0 && (x == 0 || y == 0 || x == W - 1 || y == H - 1)) edge = true;
      }
    }
    char what[96];
    std::snprintf(what, sizeof(what), "%s fills 3-45%% of the frame and stays off its edges", maskShapeLabel(shape));
    check(what, s > 0.03 * W * H && s < 0.45 * W * H && !edge, s / (W * H));
    // Left-right symmetric shapes keep their weight on the middle column.
    std::snprintf(what, sizeof(what), "%s is centred left to right", maskShapeLabel(shape));
    check(what, std::fabs(sx / s - (W - 1) / 2.0) < 1.0, sx / s - (W - 1) / 2.0);
  }

  // Invert: the two add up to the whole frame.
  {
    Mask m = single(MaskShape::Star, W, H);
    const double plain = area(rasterizeMask(m, W, H));
    m.invert = true;
    const double inverted = area(rasterizeMask(m, W, H));
    check("invert keeps exactly what the mask cut", std::fabs(plain + inverted - W * H) < W * H * 0.001,
          plain + inverted - W * H);
  }

  // Subtract cuts what came before it, in order.
  {
    Mask m = single(MaskShape::Rectangle, W, H);
    MaskPath hole = makeMaskShape(MaskShape::Circle, static_cast<double>(W) / H);
    for (auto& p : hole.points) {   // a smaller circle, r 0.15
      p.x = 0.5f + (p.x - 0.5f) * 0.5f; p.y = 0.5f + (p.y - 0.5f) * 0.5f;
      p.inX *= 0.5f; p.inY *= 0.5f; p.outX *= 0.5f; p.outY *= 0.5f;
    }
    hole.subtract = true;
    m.paths.push_back(hole);
    const auto a = rasterizeMask(m, W, H);
    const double want = 0.84 * H * 0.56 * H - pi * (0.15 * H) * (0.15 * H);
    check("subtract punches a hole of the right size", std::fabs(area(a) - want) / want < 0.005, area(a) / want);
    check("...and the hole is empty in the middle", at(a, W, W / 2, H / 2) == 0, at(a, W, W / 2, H / 2));
    // A subtract FIRST has nothing to cut; the add after it is whole.
    Mask reversed;
    reversed.paths = {hole, m.paths[0]};
    check("...a subtract before anything cuts nothing", at(rasterizeMask(reversed, W, H), W, W / 2, H / 2) == 255);
  }

  // Grow: the circle's radius moves by the grow, exactly.
  {
    Mask m = single(MaskShape::Circle, W, H);
    m.grow = 0.05f;
    const double r = (0.30 + 0.05) * H;
    const double got = area(rasterizeMask(m, W, H));
    check("grow +0.05 makes the radius 0.35 (area within 1%)", std::fabs(got - pi * r * r) / (pi * r * r) < 0.01,
          got / (pi * r * r));
    m.grow = -0.1f;
    const double r2 = 0.20 * H, got2 = area(rasterizeMask(m, W, H));
    check("grow -0.10 makes it 0.20", std::fabs(got2 - pi * r2 * r2) / (pi * r2 * r2) < 0.015, got2 / (pi * r2 * r2));
  }

  // Feather: a ramp as wide as asked, centred on the edge, area unchanged.
  {
    Mask m = single(MaskShape::Circle, W, H);
    m.feather = 0.1f;   // 36 px
    const auto a = rasterizeMask(m, W, H);
    const double r = 0.30 * H, want = pi * r * r;
    check("feather keeps the area (centred on the edge)", std::fabs(area(a) - want) / want < 0.02, area(a) / want);
    // The middle half of the ramp (75% down to 25%) spans half the feather.
    // Measured there rather than end to end: a wide feather is drawn on a
    // coarse grid and its shoulders are eased over one cell.
    double x75 = 0, x25 = 0;
    for (int x = W / 2; x + 1 < W; ++x) {
      const double v0 = at(a, W, x, H / 2), v1 = at(a, W, x + 1, H / 2);
      if (v0 >= 191.25 && v1 < 191.25) x75 = x + (v0 - 191.25) / (v0 - v1);
      if (v0 >= 63.75 && v1 < 63.75) x25 = x + (v0 - 63.75) / (v0 - v1);
    }
    check("...over 36 px, as asked (middle half 18 px)", std::fabs((x25 - x75) - 18.0) <= 1.5, x25 - x75);
    m.feather = 0.02f;   // 7.2 px: drawn at full size
    const auto b = rasterizeMask(m, W, H);
    int ramp = 0;
    for (int x = W / 2; x < W; ++x) ramp += at(b, W, x, H / 2) > 5 && at(b, W, x, H / 2) < 250;
    check("...and a narrow one end to end (7 px)", std::abs(ramp - 7) <= 1, ramp);
    // The edge value at the radius is half.
    const int ex = static_cast<int>(W / 2.0 + r);
    check("...and is half-way at the old edge", std::abs(at(a, W, ex, H / 2) - 128) < 12, at(a, W, ex, H / 2));
  }

  // Opacity: an opacity of 0.25 cuts only a quarter of the way.
  {
    Mask m = single(MaskShape::Circle, W, H);
    m.opacity = 0.25f;
    const auto a = rasterizeMask(m, W, H);
    check("opacity 25% leaves the outside at 75%", std::abs(at(a, W, 2, 2) - 191) <= 1, at(a, W, 2, 2));
    check("...and the inside whole", at(a, W, W / 2, H / 2) == 255);
    check("maskValueOutside agrees", std::fabs(maskValueOutside(m) - 0.75f) < 1e-6f, maskValueOutside(m));
  }

  // Move, scale and turn.
  {
    Mask m = single(MaskShape::Rectangle, W, H);
    m.offsetX = 0.25f;
    const auto a = rasterizeMask(m, W, H);
    check("x offset 0.25 moves it a quarter of the width", at(a, W, W / 2 + W / 4, H / 2) == 255 &&
          at(a, W, W / 2 - static_cast<int>(0.42 * H) + W / 4 - 5, H / 2) == 0);
    m.offsetX = 0.0f;
    m.scale = 0.5f;
    const double want = 0.84 * H * 0.56 * H * 0.25;
    check("scale 0.5 quarters the area", std::fabs(area(rasterizeMask(m, W, H)) - want) / want < 0.01);
    m.scale = 1.0f;
    m.rotation = 90.0f;
    const auto t = rasterizeMask(m, W, H);
    check("rotation keeps the area", std::fabs(area(t) - 0.84 * 0.56 * H * H) / (0.84 * 0.56 * H * H) < 0.003);
    check("...and a quarter turn stands it on end", at(t, W, W / 2, static_cast<int>(H / 2 - 0.38 * H)) == 255 &&
          at(t, W, static_cast<int>(W / 2 + 0.36 * H), H / 2) == 0);
  }

  // Oscillators: an LFO on scale at depth 1 swings the whole range about the
  // set value; off, the resolved mask is the mask.
  {
    Mask m = single(MaskShape::Circle, W, H);
    check("no oscillator: resolved == set", resolveMask(m, 3.3, 0.0, 0.0).scale == m.scale);
    m.lfo[kMaskLfoRotation].on = true;
    m.lfo[kMaskLfoRotation].depth = 0.5f;
    m.lfo[kMaskLfoRotation].rateHz = 1.0f;
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 100; ++i) {
      const float r = resolveMask(m, i / 100.0, 0.0, 0.0).rotation;
      lo = std::min(lo, r); hi = std::max(hi, r);
    }
    check("a rotation LFO at half depth swings +/-180 degrees", std::fabs(hi - 180.0f) < 4 && std::fabs(lo + 180.0f) < 4,
          hi);
    check("maskAnimates says so", maskAnimates(m));
  }

  // The save format.
  {
    Mask m = single(MaskShape::Heart, W, H);
    m.name = "Heart | odd; name: with % all";
    m.paths[0].name = "lobe;1";
    m.invert = true;
    m.feather = 0.0125f;
    m.grow = -0.02f;
    m.opacity = 0.5f;
    m.offsetX = 0.1f; m.offsetY = -0.2f; m.scale = 1.5f; m.rotation = 33.0f;
    MaskPath open;
    open.closed = false;
    open.subtract = true;
    open.points.resize(2);
    m.paths.push_back(open);
    const std::string text = serializeMask(m, "lfo;text|x");
    Mask back;
    std::string lfo;
    const bool ok = parseMask(text, back, lfo);
    check("a mask reads back", ok);
    check("...name, path name and oscillators intact", back.name == m.name && back.paths.size() == 2 &&
          back.paths[0].name == "lobe;1" && lfo == "lfo;text|x");
    check("...flags intact", back.invert && !back.paths[1].closed && back.paths[1].subtract && !back.paths[0].subtract);
    check("...and draws the same picture", rasterizeMask(back, W, H) == rasterizeMask(m, W, H));
    check("a line that is not a mask is refused", !parseMask("hello", back, lfo));
  }

  // SVG: one path element per path, closed with Z, and the move as a transform.
  {
    Mask m = single(MaskShape::Star, W, H);
    m.paths.push_back(makeMaskShape(MaskShape::Circle, 16.0 / 9.0));
    m.paths.back().subtract = true;
    m.rotation = 15.0f;
    const std::string svg = maskToSvg(m, W, H);
    std::size_t paths = 0;
    for (std::size_t pos = svg.find("<path"); pos != std::string::npos; pos = svg.find("<path", pos + 1)) ++paths;
    check("SVG has a path per path", paths == 2, static_cast<double>(paths));
    check("...the subtract painted black over the add",
          svg.find("fill=\"#000000\"", svg.find("<path")) != std::string::npos);
    check("...and the turn in the transform", svg.find("rotate(15)") != std::string::npos);
  }

  if (failures) {
    std::printf("\nmasks: %d FAILED\n", failures);
    return 1;
  }
  std::printf("\nmasks: all cases pass\n");
  return 0;
}
