// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// primitives.cpp — drawing primitives implementation.
//
// Implements the stateless Primitives class for basic 2D rendering operations.
// The drawFramedPanel() bevel effect auto-detects raised vs. sunken style
// by comparing the luminance of the body and inner border colors.
//
// Header: primitives.hpp
// Used by: waveform_renderer.cpp, app_render_*.ipp, main.cpp UI drawing.
// ============================================================================

#include "primitives.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace deckboy::render {

// Helper: shrink a rectangle by N pixels on all sides.
static SDL_Rect insetRect(const SDL_Rect& rect, int inset) {
  return SDL_Rect{
    rect.x + inset,
    rect.y + inset,
    std::max(0, rect.w - 2 * inset),
    std::max(0, rect.h - 2 * inset)
  };
}

namespace {

struct Surface {
  SDL_Rect rect;
  SDL_Color color;
};

// One renderer at a time: the control window. Output renderers draw pictures,
// not labels, and are never tracked.
SDL_Renderer* gSurfaceRenderer = nullptr;
SDL_Color gSurfaceClear {0, 0, 0, 255};
std::vector<Surface> gSurfaces;

// Fills smaller than this are bevels, meters and dots -- never what a label
// sits on -- and leaving them out keeps the lookup short.
constexpr int kMinSurfaceArea = 48;
// Bounded so a frame that draws a huge number of cells cannot grow it without
// limit; past the cap the oldest half goes, which are the furthest down.
constexpr std::size_t kMaxSurfaces = 6000;

bool containsPoint(const SDL_Rect& r, int x, int y) {
  return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

SDL_Color colorAt(int x, int y) {
  for (auto it = gSurfaces.rbegin(); it != gSurfaces.rend(); ++it) {
    if (containsPoint(it->rect, x, y)) return it->color;
  }
  return gSurfaceClear;
}

void recordSurface(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
  if (renderer != gSurfaceRenderer || color.a == 0 ||
      rect.w * rect.h < kMinSurfaceArea || SDL_GetRenderTarget(renderer) != nullptr) {
    return;
  }
  SDL_Color effective = color;
  if (color.a < 255) {
    // What the eye sees is the fill over whatever it covered, so a 50% wash
    // over a dark panel is recorded as the darkened colour, not the wash.
    const SDL_Color under = colorAt(rect.x + rect.w / 2, rect.y + rect.h / 2);
    const int a = color.a;
    effective = {static_cast<Uint8>((color.r * a + under.r * (255 - a)) / 255),
                 static_cast<Uint8>((color.g * a + under.g * (255 - a)) / 255),
                 static_cast<Uint8>((color.b * a + under.b * (255 - a)) / 255), 255};
  }
  if (gSurfaces.size() >= kMaxSurfaces) {
    gSurfaces.erase(gSurfaces.begin(), gSurfaces.begin() + kMaxSurfaces / 2);
  }
  gSurfaces.push_back({rect, effective});
}

}  // namespace

namespace {
bool gPressHeld = false;
int gPressX = 0, gPressY = 0, gPressMaxW = 0, gPressMaxH = 0;
bool gPressFound = false;
SDL_Rect gPressRect {};
}  // namespace

void Primitives::beginSurfaceFrame(SDL_Renderer* renderer, SDL_Color clearColor) {
  gSurfaceRenderer = renderer;
  gSurfaceClear = clearColor;
  gSurfaces.clear();
  gPressFound = false;
}

void Primitives::setPress(bool held, int x, int y, int maxW, int maxH) {
  gPressHeld = held;
  gPressX = x;
  gPressY = y;
  gPressMaxW = maxW;
  gPressMaxH = maxH;
}

void Primitives::notePressable(SDL_Renderer* renderer, const SDL_Rect& rect) {
  if (!gPressHeld || renderer != gSurfaceRenderer || SDL_GetRenderTarget(renderer) != nullptr) {
    return;
  }
  const SDL_Point p {gPressX, gPressY};
  if (!SDL_PointInRect(&p, &rect)) {
    return;
  }
  gPressFound = rect.w <= gPressMaxW && rect.h <= gPressMaxH;
  if (gPressFound) {
    gPressRect = rect;
  }
}

bool Primitives::pressedTarget(SDL_Rect* out) {
  if (!gPressHeld || !gPressFound) {
    return false;
  }
  *out = gPressRect;
  return true;
}

bool Primitives::surfaceUnder(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color* out) {
  if (!renderer || renderer != gSurfaceRenderer || SDL_GetRenderTarget(renderer) != nullptr) {
    return false;
  }
  *out = colorAt(rect.x + rect.w / 2, rect.y + rect.h / 2);
  return true;
}

void Primitives::fillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(renderer, &rect);
  recordSurface(renderer, rect, color);
}

void Primitives::strokeRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderRect(renderer, &rect);
}

// Draws a panel with a 3D bevel effect:
//   1. Fill the body rectangle with the body color
//   2. Draw a 1px outer border
//   3. Inset by 2px and draw highlight (top/left) + shadow (bottom/right)
// The bevel direction is auto-detected from relative luminance.
void Primitives::drawFramedPanel(SDL_Renderer* renderer, const SDL_Rect& rect,
                                  SDL_Color body, SDL_Color border, SDL_Color innerBorder) {
  if (gChrome.enabled) {
    drawChrome(renderer, rect, body, border,
               innerBorder.r + innerBorder.g + innerBorder.b >= body.r + body.g + body.b);
    return;
  }
  fillRect(renderer, rect, body);
  notePressable(renderer, rect);
  strokeRect(renderer, rect, border);
  SDL_Rect inner = insetRect(rect, 2);
  if (inner.w > 2 && inner.h > 2) {
    // Compare luminance to determine bevel direction:
    // brighter innerBorder → raised panel, darker → sunken panel
    int bodyLuma = body.r + body.g + body.b;
    int innerLuma = innerBorder.r + innerBorder.g + innerBorder.b;
    bool raised = (innerLuma >= bodyLuma);
    SDL_Color hi = raised ? innerBorder : body;  // Highlight (top-left edges)
    SDL_Color lo = {  // Shadow (bottom-right edges, darkened by 1/3)
      static_cast<Uint8>(std::min(255, (raised ? body.r : innerBorder.r) * 2 / 3)),
      static_cast<Uint8>(std::min(255, (raised ? body.g : innerBorder.g) * 2 / 3)),
      static_cast<Uint8>(std::min(255, (raised ? body.b : innerBorder.b) * 2 / 3)),
      (raised ? body : innerBorder).a
    };
    int x1 = inner.x, y1 = inner.y;
    int x2 = inner.x + inner.w - 1, y2 = inner.y + inner.h - 1;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, hi.r, hi.g, hi.b, hi.a);
    SDL_RenderLine(renderer, x1, y1, x2, y1);
    SDL_RenderLine(renderer, x1, y1, x1, y2);
    SDL_SetRenderDrawColor(renderer, lo.r, lo.g, lo.b, lo.a);
    SDL_RenderLine(renderer, x1, y2, x2, y2);
    SDL_RenderLine(renderer, x2, y1, x2, y2);
  }
}

// ── SKINS: SHAPE AND FINISH ─────────────────────────────────────────────────
//
// A shape is drawn as horizontal bands: for each row the left and right edge
// come from the corners, so a vertical gradient is exact (a fan from the
// centre would make it radial) and every shape -- round, pill, cut corner,
// a cartoon blob with eight different radii -- is the same few lines. One
// pixel of transparent fringe on each side stands in for antialiasing, which
// SDL's geometry does not do; the software renderer draws it the same way.

ChromeShape chromeShapeFromToken(const std::string& token, ChromeShape fallback) {
  if (token == "rect") return ChromeShape::Rect;
  if (token == "round") return ChromeShape::Round;
  if (token == "pill") return ChromeShape::Pill;
  if (token == "chamfer") return ChromeShape::Chamfer;
  if (token == "blob") return ChromeShape::Blob;
  return fallback;
}

ChromeTexture chromeTextureFromToken(const std::string& token) {
  if (token == "brushed") return ChromeTexture::Brushed;
  if (token == "grain") return ChromeTexture::Grain;
  if (token == "crackle") return ChromeTexture::Crackle;
  if (token == "dots") return ChromeTexture::Dots;
  if (token == "wood") return ChromeTexture::Wood;
  if (token == "carbon") return ChromeTexture::Carbon;
  if (token == "halftone") return ChromeTexture::Halftone;
  if (token == "circuit") return ChromeTexture::Circuit;
  if (token == "mesh") return ChromeTexture::Mesh;
  return ChromeTexture::None;
}

namespace {

struct ChromeGeom {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  float rx[4] {0, 0, 0, 0};   // top-left, top-right, bottom-right, bottom-left
  float ry[4] {0, 0, 0, 0};
  bool chamfer = false;
};

ChromeGeom offsetGeom(ChromeGeom g, float dx, float dy) {
  g.x0 += dx; g.x1 += dx; g.y0 += dy; g.y1 += dy;
  return g;
}

// Negative d grows the shape.
ChromeGeom insetGeom(ChromeGeom g, float d) {
  g.x0 += d; g.y0 += d; g.x1 -= d; g.y1 -= d;
  for (int i = 0; i < 4; ++i) {
    g.rx[i] = std::max(0.0f, g.rx[i] - d);
    g.ry[i] = std::max(0.0f, g.ry[i] - d);
  }
  return g;
}

bool geomEmpty(const ChromeGeom& g) { return g.x1 - g.x0 < 0.5f || g.y1 - g.y0 < 0.5f; }

// How far the edge is pulled in at distance t (0..ry) above the corner's end.
float cornerInset(float rx, float ry, float t, bool chamfer) {
  if (rx <= 0.0f || ry <= 0.0f) return 0.0f;
  t = std::clamp(t, 0.0f, ry);
  if (chamfer) return rx * (t / ry);
  const float u = t / ry;
  return rx - rx * std::sqrt(std::max(0.0f, 1.0f - u * u));
}

void chromeSpan(const ChromeGeom& g, float y, float& xl, float& xr) {
  xl = g.x0;
  xr = g.x1;
  if (y < g.y0 + g.ry[0]) xl = std::max(xl, g.x0 + cornerInset(g.rx[0], g.ry[0], g.y0 + g.ry[0] - y, g.chamfer));
  if (y > g.y1 - g.ry[3]) xl = std::max(xl, g.x0 + cornerInset(g.rx[3], g.ry[3], y - (g.y1 - g.ry[3]), g.chamfer));
  if (y < g.y0 + g.ry[1]) xr = std::min(xr, g.x1 - cornerInset(g.rx[1], g.ry[1], g.y0 + g.ry[1] - y, g.chamfer));
  if (y > g.y1 - g.ry[2]) xr = std::min(xr, g.x1 - cornerInset(g.rx[2], g.ry[2], y - (g.y1 - g.ry[2]), g.chamfer));
  if (xr < xl) xl = xr = (xl + xr) * 0.5f;
}

// Rows close together where a corner turns (spaced by angle, not by height,
// so the flat top of a curve gets as many as its steep side), and a few down
// the straight sides for the gradient.
std::vector<float> chromeRows(const ChromeGeom& g) {
  std::vector<float> ys {g.y0, g.y1};
  auto corner = [&](float ry, bool top) {
    if (ry <= 0.0f) return;
    const int steps = std::clamp(static_cast<int>(ry / 2.0f) + 3, 3, 14);
    for (int i = 0; i <= steps; ++i) {
      const float a = 1.5707963f * static_cast<float>(i) / static_cast<float>(steps);
      const float d = g.chamfer ? ry * static_cast<float>(i) / static_cast<float>(steps) : ry * (1.0f - std::sin(a));
      ys.push_back(top ? g.y0 + d : g.y1 - d);
    }
  };
  corner(g.ry[0], true);
  corner(g.ry[1], true);
  corner(g.ry[2], false);
  corner(g.ry[3], false);
  for (int i = 1; i < 8; ++i) ys.push_back(g.y0 + (g.y1 - g.y0) * static_cast<float>(i) / 8.0f);
  for (float& y : ys) y = std::clamp(y, g.y0, g.y1);
  std::sort(ys.begin(), ys.end());
  std::vector<float> out;
  for (float y : ys) {
    if (out.empty() || y - out.back() > 0.35f) out.push_back(y);
  }
  if (out.back() < g.y1) out.back() = g.y1;
  return out;
}

SDL_FColor toF(SDL_Color c, float alpha = 1.0f) {
  return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, (c.a / 255.0f) * alpha};
}

// k > 0 toward white, k < 0 toward black.
SDL_FColor shadeF(SDL_Color c, float k, float alpha = 1.0f) {
  SDL_FColor f = toF(c, alpha);
  const float t = std::clamp(std::fabs(k), 0.0f, 1.0f);
  const float to = k > 0.0f ? 1.0f : 0.0f;
  f.r += (to - f.r) * t;
  f.g += (to - f.g) * t;
  f.b += (to - f.b) * t;
  return f;
}

template <typename ColourAt>
void chromeFill(SDL_Renderer* renderer, const ChromeGeom& g, ColourAt colourAt, float feather,
                SDL_Texture* texture = nullptr, float tile = 256.0f) {
  if (geomEmpty(g)) return;
  std::vector<float> ys = chromeRows(g);
  struct Row { float y, xl, xr; SDL_FColor c; };
  std::vector<Row> rows;
  rows.reserve(ys.size() + 2);
  for (float y : ys) {
    Row row {y, 0, 0, colourAt(y)};
    chromeSpan(g, y, row.xl, row.xr);
    rows.push_back(row);
  }
  if (feather > 0.0f) {
    Row top = rows.front(), bottom = rows.back();
    top.y -= feather; top.c.a = 0.0f;
    bottom.y += feather; bottom.c.a = 0.0f;
    rows.insert(rows.begin(), top);
    rows.push_back(bottom);
  }
  const int per = feather > 0.0f ? 4 : 2;
  std::vector<SDL_Vertex> v;
  std::vector<int> idx;
  v.reserve(rows.size() * per);
  idx.reserve(rows.size() * 18);
  auto vert = [&](float x, float y, SDL_FColor c) {
    SDL_Vertex vx {};
    vx.position = {x, y};
    vx.color = c;
    vx.tex_coord = {x / tile, y / tile};
    v.push_back(vx);
  };
  for (const Row& row : rows) {
    SDL_FColor clear = row.c;
    clear.a = 0.0f;
    if (feather > 0.0f) vert(row.xl - feather, row.y, clear);
    vert(row.xl, row.y, row.c);
    vert(row.xr, row.y, row.c);
    if (feather > 0.0f) vert(row.xr + feather, row.y, clear);
  }
  for (std::size_t i = 0; i + 1 < rows.size(); ++i) {
    const int a = static_cast<int>(i) * per, b = a + per;
    for (int k = 0; k + 1 < per; ++k) {
      idx.insert(idx.end(), {a + k, a + k + 1, b + k, a + k + 1, b + k + 1, b + k});
    }
  }
  SDL_RenderGeometry(renderer, texture, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
}

void chromeFillFlat(SDL_Renderer* renderer, const ChromeGeom& g, SDL_FColor c, float feather) {
  chromeFill(renderer, g, [&](float) { return c; }, feather);
}

// The band between two nested shapes: rows where the inner shape is absent
// are filled across, the rest get a piece on each side.
void chromeRing(SDL_Renderer* renderer, const ChromeGeom& outer, const ChromeGeom& inner, SDL_FColor c) {
  if (geomEmpty(outer)) return;
  if (geomEmpty(inner)) {
    chromeFillFlat(renderer, outer, c, 0.6f);
    return;
  }
  std::vector<float> ys = chromeRows(outer);
  const std::vector<float> more = chromeRows(inner);
  ys.insert(ys.end(), more.begin(), more.end());
  ys.push_back(inner.y0 - 0.01f);
  ys.push_back(inner.y1 + 0.01f);
  std::sort(ys.begin(), ys.end());
  std::vector<SDL_Vertex> v;
  std::vector<int> idx;
  auto quad = [&](float xa0, float xb0, float ya, float xa1, float xb1, float yb) {
    const int base = static_cast<int>(v.size());
    v.push_back({{xa0, ya}, c, {0, 0}});
    v.push_back({{xb0, ya}, c, {0, 0}});
    v.push_back({{xa1, yb}, c, {0, 0}});
    v.push_back({{xb1, yb}, c, {0, 0}});
    idx.insert(idx.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
  };
  for (std::size_t i = 0; i + 1 < ys.size(); ++i) {
    const float ya = std::clamp(ys[i], outer.y0, outer.y1), yb = std::clamp(ys[i + 1], outer.y0, outer.y1);
    if (yb - ya < 0.01f) continue;
    const float mid = (ya + yb) * 0.5f;
    float ol0, or0, ol1, or1;
    chromeSpan(outer, ya, ol0, or0);
    chromeSpan(outer, yb, ol1, or1);
    if (mid < inner.y0 || mid > inner.y1) {
      quad(ol0, or0, ya, ol1, or1, yb);
      continue;
    }
    float il0, ir0, il1, ir1;
    chromeSpan(inner, std::clamp(ya, inner.y0, inner.y1), il0, ir0);
    chromeSpan(inner, std::clamp(yb, inner.y0, inner.y1), il1, ir1);
    quad(ol0, il0, ya, ol1, il1, yb);
    quad(ir0, or0, ya, ir1, or1, yb);
  }
  SDL_RenderGeometry(renderer, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
}

std::uint32_t chromeHash(std::uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}

float hash01(int x, int y, int seed) {
  const std::uint32_t h = chromeHash(static_cast<std::uint32_t>(x) * 73856093U ^
                                     static_cast<std::uint32_t>(y) * 19349663U ^
                                     static_cast<std::uint32_t>(seed) * 83492791U);
  return static_cast<float>(h & 0xFFFFFF) / 16777216.0f;
}

// Value noise that tiles with a period of `period` cells.
float tiledNoise(float x, float y, int period, int seed) {
  const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(xi), fy = y - static_cast<float>(yi);
  auto at = [&](int cx, int cy) {
    return hash01(((cx % period) + period) % period, ((cy % period) + period) % period, seed);
  };
  const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
  const float a = at(xi, yi) + (at(xi + 1, yi) - at(xi, yi)) * sx;
  const float b = at(xi, yi + 1) + (at(xi + 1, yi + 1) - at(xi, yi + 1)) * sx;
  return a + (b - a) * sy;
}

// A texture is light and shade, not colour: each texel is white or black
// with an alpha, laid over whatever colour the face already is.
constexpr int kTex = 256;

void makeTexture(ChromeTexture kind, std::vector<std::uint32_t>& px) {
  px.assign(kTex * kTex, 0);
  auto put = [&](int x, int y, float s) {   // s in -1..1: dark .. light
    const float a = std::clamp(std::fabs(s), 0.0f, 1.0f);
    const std::uint8_t c = s >= 0 ? 255 : 0;
    const std::uint8_t al = static_cast<std::uint8_t>(a * 255.0f);
    // SDL_PIXELFORMAT_RGBA32 is bytes R, G, B, A in memory.
    std::uint8_t* b = reinterpret_cast<std::uint8_t*>(&px[static_cast<std::size_t>(y) * kTex + x]);
    b[0] = c; b[1] = c; b[2] = c; b[3] = al;
  };
  for (int y = 0; y < kTex; ++y) {
    for (int x = 0; x < kTex; ++x) {
      float s = 0.0f;
      switch (kind) {
        case ChromeTexture::Brushed: {
          // Long horizontal strokes: each row has its own tone, and a slow
          // noise along it breaks the stroke up the way a brush does.
          // Every row is its own stroke: a tone, and a slow wander along it
          // (no correlation between rows, or it blotches like wood).
          const float row = hash01(0, y, 11) - 0.5f;
          const float along = tiledNoise(static_cast<float>(x) / 64.0f, static_cast<float>(y) * 7.31f, 4, 12) - 0.5f;
          const float fine = hash01(x / 6, y, 13) - 0.5f;
          s = row * 0.75f + along * 0.45f + fine * 0.12f;
          break;
        }
        case ChromeTexture::Grain:
          s = (hash01(x, y, 21) - 0.5f) * 1.4f;
          break;
        case ChromeTexture::Crackle: {
          // Cells about 21 px across; dark where two cells meet.
          const int cell = 21;
          float d1 = 1e9f, d2 = 1e9f;
          const int cx = x / cell, cy = y / cell, n = kTex / cell + 1;
          for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
              const int gx = cx + ox, gy = cy + oy;
              const int wx = ((gx % n) + n) % n, wy = ((gy % n) + n) % n;
              const float px0 = (static_cast<float>(gx) + hash01(wx, wy, 31)) * cell;
              const float py0 = (static_cast<float>(gy) + hash01(wx, wy, 32)) * cell;
              const float d = std::hypot(px0 - static_cast<float>(x), py0 - static_cast<float>(y));
              if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) { d2 = d; }
            }
          }
          const float edge = std::clamp(1.0f - (d2 - d1) / 2.2f, 0.0f, 1.0f);
          s = -edge * 0.9f + (hash01(x, y, 33) - 0.5f) * 0.35f;
          break;
        }
        case ChromeTexture::Dots: {
          const float dx = static_cast<float>(x % 8) - 3.5f, dy = static_cast<float>(y % 8) - 3.5f;
          s = (dx * dx + dy * dy < 4.0f) ? -0.8f : (hash01(x, y, 41) - 0.5f) * 0.2f;
          break;
        }
        case ChromeTexture::Wood: {
          // Grain runs down the page, so a stretch never smears it.
          const float warp = tiledNoise(static_cast<float>(y) / 64.0f, static_cast<float>(x) / 32.0f, 4, 51) * 9.0f;
          const float ring = std::sin((static_cast<float>(x) + warp) * 6.2831853f * 18.0f / kTex);
          s = ring * 0.55f + (hash01(x, y / 6, 52) - 0.5f) * 0.4f;
          break;
        }
        case ChromeTexture::Carbon: {
          const int bx = (x / 8) % 2, by = (y / 8) % 2;
          const float u = static_cast<float>(x % 8) / 8.0f, w = static_cast<float>(y % 8) / 8.0f;
          s = (bx == by) ? (0.5f - u) * 0.9f : (0.5f - w) * 0.9f;
          break;
        }
        case ChromeTexture::Halftone: {
          const float dx = static_cast<float>(x % 6) - 2.5f, dy = static_cast<float>(y % 6) - 2.5f;
          s = (dx * dx + dy * dy < 3.2f) ? -0.6f : 0.0f;
          break;
        }
        case ChromeTexture::Circuit: {
          // Traces on a 16 px grid, a pad where two meet.
          const int gx = x / 16, gy = y / 16, lx = x % 16, ly = y % 16;
          const bool h = hash01(gx, gy, 61) > 0.45f && ly >= 7 && ly <= 8;
          const bool v = hash01(gx, gy, 62) > 0.55f && lx >= 7 && lx <= 8;
          const bool pad = hash01(gx, gy, 63) > 0.8f && (lx - 7.5f) * (lx - 7.5f) + (ly - 7.5f) * (ly - 7.5f) < 9.0f;
          s = (h || v || pad) ? 0.75f : 0.0f;
          break;
        }
        case ChromeTexture::Mesh: {
          const int lx = x % 4, ly = y % 4;
          s = (lx >= 1 && lx <= 2 && ly >= 1 && ly <= 2) ? -0.85f : 0.1f;
          break;
        }
        case ChromeTexture::None:
          break;
      }
      put(x, y, std::clamp(s, -1.0f, 1.0f));
    }
  }
}

struct TexKey {
  SDL_Renderer* renderer;
  ChromeTexture kind;
};
std::vector<std::pair<TexKey, SDL_Texture*>> gChromeTextures;

SDL_Texture* chromeTexture(SDL_Renderer* renderer, ChromeTexture kind) {
  if (kind == ChromeTexture::None) return nullptr;
  for (const auto& entry : gChromeTextures) {
    if (entry.first.renderer == renderer && entry.first.kind == kind) return entry.second;
  }
  std::vector<std::uint32_t> px;
  makeTexture(kind, px);
  SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kTex, kTex);
  if (tex) {
    SDL_UpdateTexture(tex, nullptr, px.data(), kTex * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);   // a finish, not pixel art
  }
  gChromeTextures.push_back({{renderer, kind}, tex});
  return tex;
}

void chromeTextureOver(SDL_Renderer* renderer, const ChromeGeom& g, ChromeTexture kind, float alpha) {
  if (kind == ChromeTexture::None || alpha <= 0.0f) return;
  SDL_Texture* tex = chromeTexture(renderer, kind);
  if (!tex) return;
  SDL_SetRenderTextureAddressMode(renderer, SDL_TEXTURE_ADDRESS_WRAP, SDL_TEXTURE_ADDRESS_WRAP);
  const float tile = static_cast<float>(kTex) * std::max(0.25f, gChrome.textureScale * gChrome.scale);
  const SDL_FColor c {1.0f, 1.0f, 1.0f, std::clamp(alpha, 0.0f, 1.0f)};
  chromeFill(renderer, g, [&](float) { return c; }, 0.0f, tex, tile);
  SDL_SetRenderTextureAddressMode(renderer, SDL_TEXTURE_ADDRESS_AUTO, SDL_TEXTURE_ADDRESS_AUTO);
}

// What shape a rect is in this skin. Controls and panels are told apart the
// way the press tracker does it.
ChromeGeom chromeGeomFor(const SDL_Rect& rect, bool* isControl, ChromeRole role = ChromeRole::Auto) {
  const float s = std::max(0.25f, gChrome.scale);
  const bool control = role == ChromeRole::Control ||
    (role == ChromeRole::Auto && rect.w <= 560.0f * s && rect.h <= 150.0f * s);
  if (isControl) *isControl = control;
  ChromeGeom g;
  g.x0 = static_cast<float>(rect.x);
  g.y0 = static_cast<float>(rect.y);
  g.x1 = static_cast<float>(rect.x + rect.w);
  g.y1 = static_cast<float>(rect.y + rect.h);
  const float maxR = std::min(rect.w, rect.h) * 0.5f;
  const ChromeShape shape = control ? gChrome.controlShape : gChrome.panelShape;
  float r = std::min(maxR, (control ? gChrome.controlRadius : gChrome.panelRadius) * s);
  auto all = [&](float rr) {
    for (int i = 0; i < 4; ++i) { g.rx[i] = rr; g.ry[i] = rr; }
  };
  switch (shape) {
    case ChromeShape::Rect:
      break;
    case ChromeShape::Round:
      all(r);
      break;
    case ChromeShape::Chamfer:
      g.chamfer = true;
      all(r);
      break;
    case ChromeShape::Pill:
      // Full round ends only on short controls (toolbar buttons, chips, a
      // row): on a tall card or a bottom-bar button the ends cut into the
      // label, so those keep the skin's corner radius.
      all(control && rect.h <= 48.0f * s ? maxR : r);
      break;
    case ChromeShape::Blob: {
      // Hand-drawn: every corner its own pair of radii, fixed per control
      // so it does not wobble from frame to frame.
      const std::uint32_t seed = chromeHash(static_cast<std::uint32_t>(rect.w) * 131u + static_cast<std::uint32_t>(rect.h) * 7u);
      for (int i = 0; i < 4; ++i) {
        g.rx[i] = std::min(maxR, r * (0.55f + 0.7f * hash01(static_cast<int>(seed & 0xFFFF), i, 71)));
        g.ry[i] = std::min(maxR, r * (0.55f + 0.7f * hash01(static_cast<int>(seed >> 16), i, 72)));
      }
      break;
    }
  }
  return g;
}

}  // namespace

// ── RENDERED SKINS ──────────────────────────────────────────────────────────
//
// A skin with style_kit draws its panels, recesses, caps and stickers from
// pictures rendered in Blender (ChromeKit, primitives.hpp). Everything else
// about the skin (shape for the press wash and the rings, the colours the
// labels are judged against) still comes from its theme.

namespace {

bool parseKitColour(const std::string& s, SDL_Color* out) {
  if (s.size() < 6) return false;
  try {
    const auto v = static_cast<std::uint32_t>(std::stoul(s.substr(0, 6), nullptr, 16));
    *out = {static_cast<Uint8>(v >> 16), static_cast<Uint8>(v >> 8), static_cast<Uint8>(v), 255};
    return true;
  } catch (...) {
    return false;
  }
}

struct KitTexture {
  SDL_Renderer* renderer = nullptr;
  std::string path;
  int density = 0;          // texture px per UI px, after halving
  SDL_Texture* texture = nullptr;
};
std::vector<KitTexture> gKitTextures;

void forgetKitTextures(SDL_Renderer* renderer) {
  for (auto it = gKitTextures.begin(); it != gKitTextures.end();) {
    if (!renderer || it->renderer == renderer) {
      if (it->texture) SDL_DestroyTexture(it->texture);
      it = gKitTextures.erase(it);
    } else {
      ++it;
    }
  }
}

// Half the size, each pixel the average of four weighted by their alpha (a
// plain average darkens the edge of a cut-out against its transparent black).
SDL_Surface* halveSurface(SDL_Surface* in) {
  const int w = std::max(1, in->w / 2), h = std::max(1, in->h / 2);
  SDL_Surface* out = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
  if (!out) return nullptr;
  for (int y = 0; y < h; ++y) {
    auto* dst = static_cast<std::uint8_t*>(out->pixels) + static_cast<std::size_t>(y) * out->pitch;
    const auto* r0 = static_cast<const std::uint8_t*>(in->pixels) + static_cast<std::size_t>(std::min(in->h - 1, y * 2)) * in->pitch;
    const auto* r1 = static_cast<const std::uint8_t*>(in->pixels) + static_cast<std::size_t>(std::min(in->h - 1, y * 2 + 1)) * in->pitch;
    for (int x = 0; x < w; ++x) {
      const int x0 = std::min(in->w - 1, x * 2) * 4, x1 = std::min(in->w - 1, x * 2 + 1) * 4;
      const std::uint8_t* q[4] = {r0 + x0, r0 + x1, r1 + x0, r1 + x1};
      int a = 0, rr = 0, gg = 0, bb = 0;
      for (const std::uint8_t* p : q) {
        a += p[3];
        rr += p[0] * p[3];
        gg += p[1] * p[3];
        bb += p[2] * p[3];
      }
      std::uint8_t* o = dst + x * 4;
      o[0] = static_cast<std::uint8_t>(a ? rr / a : 0);
      o[1] = static_cast<std::uint8_t>(a ? gg / a : 0);
      o[2] = static_cast<std::uint8_t>(a ? bb / a : 0);
      o[3] = static_cast<std::uint8_t>(a / 4);
    }
  }
  return out;
}

// The kit's picture for this renderer, halved while it stays at least as
// fine as the UI scale draws it: linear filtering alone, shrinking a 4x
// picture to 1x, skips three pixels in four and the bevels shimmer.
SDL_Texture* kitTexture(SDL_Renderer* renderer, const std::string& file, int density, float scale, int* densityOut) {
  if (file.empty() || gChrome.kit.dir.empty()) return nullptr;
  int want = std::max(1, density);
  while (want % 2 == 0 && static_cast<float>(want / 2) >= scale) want /= 2;
  const std::string path = gChrome.kit.dir + "/" + file;
  for (const auto& t : gKitTextures) {
    if (t.renderer == renderer && t.density == want && t.path == path) {
      if (densityOut) *densityOut = want;
      return t.texture;
    }
  }
  SDL_Texture* texture = nullptr;
  if (SDL_Surface* loaded = SDL_LoadSurface(path.c_str())) {
    SDL_Surface* surface = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    for (int d = std::max(1, density); surface && d > want; d /= 2) {
      SDL_Surface* half = halveSurface(surface);
      SDL_DestroySurface(surface);
      surface = half;
    }
    if (surface) {
      texture = SDL_CreateTextureFromSurface(renderer, surface);
      SDL_DestroySurface(surface);
    }
    if (texture) {
      SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);   // a photograph, not pixel art
      SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
  }
  // A miss is remembered too, so a kit with a missing file costs one look.
  gKitTextures.push_back({renderer, path, want, texture});
  if (densityOut) *densityOut = want;
  return texture;
}

int colourDistance(SDL_Color a, SDL_Color b) {
  return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
}

// One 9-sliced piece round `rect` (grown by its shadow room). The corners
// shrink together on a rect too small for them. Returns the middle, which
// the caller fills (a tile) or leaves (the cap, stretched).
bool drawKitPiece(SDL_Renderer* renderer, const ChromeKitPiece& piece, const std::string& file,
                  const SDL_Rect& rect, float s, SDL_Color mod, SDL_BlendMode mode, SDL_FRect* middle,
                  const std::function<void(const SDL_FRect&)>& under = {}) {
  int density = 1;
  SDL_Texture* tex = kitTexture(renderer, file, static_cast<int>(gChrome.kit.density), s, &density);
  if (!tex) return false;
  const float body = std::max(1.0f, piece.inset - piece.pad);
  const float k = std::min({1.0f, static_cast<float>(rect.w) / (2.0f * body * s),
                            static_cast<float>(rect.h) / (2.0f * body * s)});
  const float pad = piece.pad * s * k, inset = piece.inset * s * k;
  const SDL_FRect dst {rect.x - pad, rect.y - pad, rect.w + 2.0f * pad, rect.h + 2.0f * pad};
  const float srcInset = piece.inset * static_cast<float>(density);
  // What fills the middle goes UNDER the piece, out as far as its edge
  // fades, so the render melts into it instead of meeting it at a line.
  if (under) {
    const float reach = inset - piece.fade * s * k;
    under(SDL_FRect {dst.x + reach, dst.y + reach, std::max(0.0f, dst.w - 2 * reach), std::max(0.0f, dst.h - 2 * reach)});
  }
  SDL_SetTextureColorMod(tex, mod.r, mod.g, mod.b);
  SDL_SetTextureBlendMode(tex, mode);
  SDL_RenderTexture9Grid(renderer, tex, nullptr, srcInset, srcInset, srcInset, srcInset, inset / srcInset, &dst);
  SDL_SetTextureColorMod(tex, 255, 255, 255);
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  if (middle) *middle = {dst.x + inset, dst.y + inset, std::max(0.0f, dst.w - 2 * inset), std::max(0.0f, dst.h - 2 * inset)};
  return true;
}

// A seamless material over `area`, at the UI scale.
bool drawKitTile(SDL_Renderer* renderer, const std::string& file, const SDL_FRect& area, float s) {
  if (area.w <= 0.0f || area.h <= 0.0f) return true;
  int density = 1;
  SDL_Texture* tex = kitTexture(renderer, file, static_cast<int>(gChrome.kit.tileDensity), s, &density);
  if (!tex) return false;
  SDL_RenderTextureTiled(renderer, tex, nullptr, s / static_cast<float>(density), &area);
  return true;
}


// THE BOARD UNDER CLEAR PLASTIC, ALIVE. LEDs and pulses of data along the
// traces, seen through the plastic. They sit on a grid in WINDOW space, not
// per panel, so a light stays where it is when a panel moves or resizes, and
// they are drawn into the panel's material before anything on the panel.
// While the show is live (gChrome.lively false) the LEDs glow steady and the
// pulses stop: nothing moves near a picture that is on air.
std::uint32_t lifeHash(int a, int b, int salt) {
  std::uint32_t h = static_cast<std::uint32_t>(a) * 73856093u ^ static_cast<std::uint32_t>(b) * 19349663u ^
                    static_cast<std::uint32_t>(salt) * 83492791u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return h;
}
float lifeRand(int a, int b, int salt) { return static_cast<float>(lifeHash(a, b, salt) & 0xFFFF) / 65535.0f; }

SDL_Texture* glowTexture(SDL_Renderer* renderer) {
  static std::vector<std::pair<SDL_Renderer*, SDL_Texture*>> made;
  for (const auto& m : made) if (m.first == renderer) return m.second;
  const int n = 48;
  std::vector<std::uint32_t> px(static_cast<std::size_t>(n) * n);
  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n; ++x) {
      const float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
      const float d = std::sqrt(dx * dx + dy * dy);
      const float core = std::clamp(1.0f - d * 4.0f, 0.0f, 1.0f);
      const float halo = std::clamp(1.0f - d, 0.0f, 1.0f);
      const float v = std::clamp(core + halo * halo * 0.55f, 0.0f, 1.0f);
      const auto a = static_cast<std::uint32_t>(v * 255.0f);
      px[static_cast<std::size_t>(y) * n + x] = (a << 24) | 0x00FFFFFFu;
    }
  }
  SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, n, n);
  if (tex) {
    SDL_UpdateTexture(tex, nullptr, px.data(), n * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
  }
  made.push_back({renderer, tex});
  return tex;
}

void drawKitBoardLife(SDL_Renderer* renderer, const SDL_FRect& area, float s) {
  if (!gChrome.kit.board || area.w < 8.0f || area.h < 8.0f) return;
  SDL_Texture* glow = glowTexture(renderer);
  if (!glow) return;
  // clip to the area, inside whatever clip is already set
  SDL_Rect want {static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.w), static_cast<int>(area.h)};
  const bool clipped = SDL_RenderClipEnabled(renderer);
  SDL_Rect previous {};
  SDL_GetRenderClipRect(renderer, &previous);
  if (clipped && !SDL_GetRectIntersection(&previous, &want, &want)) return;
  SDL_SetRenderClipRect(renderer, &want);
  const double t = gChrome.time;
  const bool live = gChrome.lively;
  // The plastic tints what shines through it.
  const SDL_Color tint = gChrome.kit.surfacePanel;
  const float tl = std::max(1.0f, static_cast<float>(std::max({tint.r, tint.g, tint.b})));
  auto through = [&](SDL_Color c) {
    auto ch = [&](Uint8 v, Uint8 k) { return static_cast<Uint8>(std::clamp(v * (0.55f + 0.45f * k / tl), 0.0f, 255.0f)); };
    return SDL_Color {ch(c.r, tint.r), ch(c.g, tint.g), ch(c.b, tint.b), 255};
  };
  const float cell = 84.0f * s;
  const int cx0 = static_cast<int>(std::floor(area.x / cell)), cx1 = static_cast<int>(std::floor((area.x + area.w) / cell));
  const int cy0 = static_cast<int>(std::floor(area.y / cell)), cy1 = static_cast<int>(std::floor((area.y + area.h) / cell));
  // PROCESSES: a pulse of light running along a trace, one trace per row
  // and per column of the grid, each at its own speed and its own gaps.
  for (int row = cy0; row <= cy1; ++row) {
    if (lifeRand(row, 7, 1) > 0.55f) continue;
    const float y = (row + 0.2f + 0.6f * lifeRand(row, 7, 2)) * cell;
    if (y < area.y || y > area.y + area.h) continue;
    const float speed = (90.0f + 160.0f * lifeRand(row, 7, 3)) * s;
    const float spanW = 1600.0f * s + 900.0f * s * lifeRand(row, 7, 4);
    const float dir = lifeRand(row, 7, 5) < 0.5f ? 1.0f : -1.0f;
    const float head = live ? static_cast<float>(std::fmod(t * speed + lifeRand(row, 7, 6) * spanW, spanW)) : -1.0f;
    if (head < 0.0f) continue;
    const float x = dir > 0 ? head - 200.0f * s : spanW - head - 200.0f * s;
    const float len = 46.0f * s;
    const SDL_Color c = through(SDL_Color {120, 220, 255, 255});
    SDL_SetTextureColorMod(glow, c.r, c.g, c.b);
    for (int k = 0; k < 8; ++k) {
      const float f = static_cast<float>(k) / 8.0f;
      const float px = x + (dir > 0 ? -f : f) * len;
      const float sz = (7.0f - 4.0f * f) * s;
      SDL_SetTextureAlphaMod(glow, static_cast<Uint8>(150 * (1.0f - f)));
      const SDL_FRect d {px - sz, y - sz, sz * 2, sz * 2};
      SDL_RenderTexture(renderer, glow, nullptr, &d);
    }
  }
  for (int col = cx0; col <= cx1; ++col) {
    if (lifeRand(col, 11, 1) > 0.35f) continue;
    const float x = (col + 0.2f + 0.6f * lifeRand(col, 11, 2)) * cell;
    if (x < area.x || x > area.x + area.w) continue;
    const float speed = (70.0f + 120.0f * lifeRand(col, 11, 3)) * s;
    const float spanH = 1100.0f * s + 700.0f * s * lifeRand(col, 11, 4);
    if (!live) continue;
    const float head = static_cast<float>(std::fmod(t * speed + lifeRand(col, 11, 6) * spanH, spanH)) - 150.0f * s;
    const float len = 36.0f * s;
    const SDL_Color c = through(SDL_Color {255, 210, 120, 255});
    SDL_SetTextureColorMod(glow, c.r, c.g, c.b);
    for (int k = 0; k < 7; ++k) {
      const float f = static_cast<float>(k) / 7.0f;
      const float sz = (6.0f - 3.5f * f) * s;
      SDL_SetTextureAlphaMod(glow, static_cast<Uint8>(130 * (1.0f - f)));
      const SDL_FRect d {x - sz, head - f * len - sz, sz * 2, sz * 2};
      SDL_RenderTexture(renderer, glow, nullptr, &d);
    }
  }
  // LEDS: about one cell in three has one. Most blink at their own slow
  // rate, some flicker like a disk working, some breathe.
  static const SDL_Color kLed[] = {{255, 40, 30, 255}, {60, 255, 90, 255}, {255, 170, 20, 255}, {60, 140, 255, 255}};
  for (int cy = cy0; cy <= cy1; ++cy) {
    for (int cx = cx0; cx <= cx1; ++cx) {
      if (lifeRand(cx, cy, 1) > 0.34f) continue;
      const float x = (cx + 0.15f + 0.7f * lifeRand(cx, cy, 2)) * cell;
      const float y = (cy + 0.15f + 0.7f * lifeRand(cx, cy, 3)) * cell;
      const SDL_Color c = through(kLed[lifeHash(cx, cy, 4) % 4]);
      const float kind = lifeRand(cx, cy, 5);
      const float phase = lifeRand(cx, cy, 6) * 10.0f;
      float level = 0.8f;
      if (live) {
        if (kind < 0.55f) {          // blinks
          const double period = 0.8 + 2.4 * lifeRand(cx, cy, 7);
          level = std::fmod(t / period + phase, 1.0) < 0.5 ? 1.0f : 0.12f;
        } else if (kind < 0.8f) {    // activity: flickers in bursts
          const int slot = static_cast<int>(std::floor(t * 14.0 + phase));
          const bool burst = lifeRand(cx, static_cast<int>(std::floor(t * 0.7 + phase)), 8) < 0.5f;
          level = burst && lifeRand(cx * 31 + cy, slot, 9) < 0.6f ? 1.0f : 0.1f;
        } else {                     // breathes
          level = 0.3f + 0.7f * static_cast<float>(0.5 + 0.5 * std::sin(t * 1.6 + phase));
        }
      }
      const float sz = 10.0f * s;
      SDL_SetTextureColorMod(glow, c.r, c.g, c.b);
      SDL_SetTextureAlphaMod(glow, static_cast<Uint8>(std::clamp(level, 0.0f, 1.0f) * 235.0f));
      const SDL_FRect d {x - sz, y - sz, sz * 2, sz * 2};
      SDL_RenderTexture(renderer, glow, nullptr, &d);
    }
  }
  SDL_SetTextureColorMod(glow, 255, 255, 255);
  SDL_SetTextureAlphaMod(glow, 255);
  SDL_SetRenderClipRect(renderer, clipped ? &previous : nullptr);
}

bool drawKitChrome(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color fill, bool raised, ChromeRole role) {
  const ChromeKit& kit = gChrome.kit;
  bool control = false;
  chromeGeomFor(rect, &control, role);
  const float s = std::max(0.25f, gChrome.scale);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  // The desk's own body: a panel covering most of the window.
  int outW = 0, outH = 0;
  if (!control && SDL_GetCurrentRenderOutputSize(renderer, &outW, &outH) && outW > 0 && outH > 0 &&
      static_cast<double>(rect.w) * rect.h > 0.6 * static_cast<double>(outW) * outH) {
    const SDL_FRect area {static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.w), static_cast<float>(rect.h)};
    if (!drawKitTile(renderer, kit.tileDesk, area, s)) return false;
    drawKitBoardLife(renderer, area, s);
    recordSurface(renderer, rect, kit.surfaceDesk);
    return true;
  }
  // Is this the skin's own surface, or a colour a control is lit in?
  const int tol = 60;
  const bool panelColour = colourDistance(fill, kit.surfacePanel) < tol;
  const bool wellColour = colourDistance(fill, kit.surfaceWell) < tol;
  const bool deskColour = colourDistance(fill, kit.surfaceDesk) < tol;
  const bool surface = panelColour || wellColour || deskColour;
  // A dark fill on a large area is a screen or a monitor's surround.
  const bool screenDark = fill.r + fill.g + fill.b < 3 * 34;
  SDL_FRect middle {};
  // Controls are always caps. The drawn painter's raised/sunken comes from
  // comparing two colours, and on a lit key that says "sunken": rendered,
  // that was a lit key down a black hole.
  if (!control && !raised && (surface || screenDark)) {
    // A recess: its lip and the shadow under it rendered, the floor tiled,
    // or the colour asked for (a black monitor well stays black).
    auto floor = [&](const SDL_FRect& area) {
      if (surface) {
        drawKitTile(renderer, kit.tileWell, area, s);
      } else {
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, 255);
        SDL_RenderFillRect(renderer, &area);
      }
    };
    if (!drawKitPiece(renderer, kit.well, kit.well.file, rect, s, {255, 255, 255, 255}, SDL_BLENDMODE_BLEND, &middle, floor)) return false;
    recordSurface(renderer, rect, surface ? kit.surfaceWell : fill);
    return true;
  }
  if (!control && surface) {
    // A clear shell's corner screws belong on the big plates: a panel less
    // than three corners across in either direction takes the plain piece.
    const bool smallPanel = !kit.panelPlain.file.empty() &&
                       (rect.w < 3.0f * kit.panel.inset * s || rect.h < 3.0f * kit.panel.inset * s);
    const ChromeKitPiece& panelPiece = smallPanel ? kit.panelPlain : kit.panel;
    if (!drawKitPiece(renderer, panelPiece, panelPiece.file, rect, s, {255, 255, 255, 255}, SDL_BLENDMODE_BLEND, &middle,
                      [&](const SDL_FRect& area) {
                        drawKitTile(renderer, kit.tilePanel, area, s);
                        drawKitBoardLife(renderer, area, s);
                      })) return false;
    recordSurface(renderer, rect, kit.surfacePanel);
    return true;
  }
  // A cap in the control's own colour: the white render's light times the
  // colour, then its reflections added.
  if (!drawKitPiece(renderer, kit.cap, kit.cap.file, rect, s, fill, SDL_BLENDMODE_BLEND, nullptr)) return false;
  drawKitPiece(renderer, kit.cap, kit.cap.gloss, rect, s, {255, 255, 255, 255}, SDL_BLENDMODE_ADD, nullptr);
  recordSurface(renderer, rect, SDL_Color {static_cast<Uint8>(fill.r * 15 / 16), static_cast<Uint8>(fill.g * 15 / 16),
                                           static_cast<Uint8>(fill.b * 15 / 16), 255});
  return true;
}

}  // namespace

bool loadChromeKit(const std::string& dir, ChromeKit& kit) {
  forgetKitTextures(nullptr);
  SDL_IOStream* io = SDL_IOFromFile((dir + "/kit.txt").c_str(), "rb");
  if (!io) return false;
  std::string text;
  char buf[1024];
  std::size_t n = 0;
  while ((n = SDL_ReadIO(io, buf, sizeof(buf))) > 0) text.append(buf, n);
  SDL_CloseIO(io);
  ChromeKit k;
  k.dir = dir;
  int seen = 0;
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> f;
    for (std::size_t p = 0;;) {
      const std::size_t tab = line.find('\t', p);
      f.push_back(line.substr(p, tab == std::string::npos ? std::string::npos : tab - p));
      if (tab == std::string::npos) break;
      p = tab + 1;
    }
    auto num = [&](std::size_t i, float fallback) {
      try { return i < f.size() ? std::stof(f[i]) : fallback; } catch (...) { return fallback; }
    };
    auto str = [&](std::size_t i) { return i < f.size() ? f[i] : std::string(); };
    const std::string& key = f[0];
    if (key == "density") k.density = std::clamp(num(1, 4.0f), 1.0f, 16.0f);
    else if (key == "tile_density") k.tileDensity = std::clamp(num(1, 2.0f), 1.0f, 16.0f);
    else if (key == "panel" || key == "well" || key == "panel_plain") {
      ChromeKitPiece& piece = key == "panel" ? k.panel : key == "well" ? k.well : k.panelPlain;
      piece.file = str(1);
      piece.pad = num(2, 0.0f);
      piece.inset = num(3, 16.0f);
      piece.fade = std::clamp(num(4, 0.0f), 0.0f, piece.inset);
      ++seen;
    } else if (key == "cap") {
      k.cap.file = str(1);
      k.cap.gloss = str(2);
      k.cap.pad = num(3, 0.0f);
      k.cap.inset = num(4, 16.0f);
      ++seen;
    } else if (key == "tile_panel") { k.tilePanel = str(1); ++seen; }
    else if (key == "tile_well") k.tileWell = str(1);
    else if (key == "tile_desk") k.tileDesk = str(1);
    else if (key == "sticker") {
      k.stickerImage = num(1, 68.0f);
      k.stickerFoil = std::max(1.0f, num(2, 37.44f));
    }
    else if (key == "surface_panel") parseKitColour(str(1), &k.surfacePanel);
    else if (key == "surface_well") parseKitColour(str(1), &k.surfaceWell);
    else if (key == "surface_desk") parseKitColour(str(1), &k.surfaceDesk);
    else if (key == "board") k.board = str(1) == "1";
  }
  if (seen < 4) return false;
  kit = k;
  return true;
}

void Primitives::drawChrome(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color fill, SDL_Color border,
                            bool raised, ChromeRole role) {
  if (rect.w <= 0 || rect.h <= 0) return;
  // A rendered skin draws what it has a picture for; a translucent fill (a
  // wash, a fade) or a kit that will not load falls through to the painter.
  if (!gChrome.kit.dir.empty() && fill.a == 255 && drawKitChrome(renderer, rect, fill, raised, role)) {
    notePressable(renderer, rect);
    return;
  }
  bool control = false;
  const ChromeGeom g = chromeGeomFor(rect, &control, role);
  const float s = std::max(0.25f, gChrome.scale);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  if (raised && gChrome.shadowAlpha > 0.0f) {
    const float blur = std::max(1.0f, gChrome.shadowBlur * s);
    const ChromeGeom sh = insetGeom(offsetGeom(g, 0.0f, gChrome.shadowY * s), -blur * 0.25f);
    chromeFillFlat(renderer, sh, {0.0f, 0.0f, 0.0f, std::clamp(gChrome.shadowAlpha, 0.0f, 1.0f)}, blur);
  }
  if (raised && control && gChrome.lip > 0.0f) {
    chromeFillFlat(renderer, offsetGeom(g, 0.0f, std::max(1.0f, gChrome.lip * s)), shadeF(border, -0.25f), 0.7f);
  }
  const float outline = gChrome.outline > 0.0f ? std::max(1.0f, std::round(gChrome.outline * s)) : 0.0f;
  if (outline > 0.0f) chromeFillFlat(renderer, g, toF(border), 0.7f);
  ChromeGeom face = insetGeom(g, outline);
  const float h = std::max(1.0f, g.y1 - g.y0);
  if (gChrome.rim > 0.0f) {
    // Light from above catches the top inside edge and fades down the sides.
    const float rim = gChrome.rim;
    chromeFill(renderer, face, [&](float y) {
      const float t = std::clamp((y - g.y0) / h, 0.0f, 1.0f);
      return raised ? shadeF(fill, rim * (1.0f - t) * (1.0f - t))
                    : shadeF(fill, -rim * 0.6f * (1.0f - t));
    }, outline > 0.0f ? 0.0f : 0.7f);
    face = insetGeom(face, std::max(1.0f, std::round(s)));
  }
  const float grad = gChrome.gradient, sheen = gChrome.sheen;
  chromeFill(renderer, face, [&](float y) {
    const float t = std::clamp((y - g.y0) / h, 0.0f, 1.0f);
    float k = raised ? grad * (1.0f - 2.0f * t) : -grad * 0.6f * (1.0f - 2.0f * t);
    if (raised && sheen > 0.0f) {
      const float d = (t - 0.32f) / 0.13f;
      k += sheen * std::exp(-d * d);
    }
    return shadeF(fill, k);
  }, (outline > 0.0f || gChrome.rim > 0.0f) ? 0.0f : 0.7f);
  // The desk's own body (a panel covering most of the window) takes the
  // background finish; the panels on it take theirs.
  ChromeTexture texture = control ? gChrome.controlTexture : gChrome.panelTexture;
  float textureAlpha = control ? gChrome.controlTextureAlpha : gChrome.panelTextureAlpha;
  int outW = 0, outH = 0;
  if (!control && SDL_GetCurrentRenderOutputSize(renderer, &outW, &outH) && outW > 0 && outH > 0 &&
      static_cast<double>(rect.w) * rect.h > 0.6 * static_cast<double>(outW) * outH) {
    texture = gChrome.backgroundTexture;
    textureAlpha = gChrome.backgroundTextureAlpha;
  }
  chromeTextureOver(renderer, face, texture, textureAlpha);
  recordSurface(renderer, rect, fill);
  notePressable(renderer, rect);
}

void Primitives::fillChromeShape(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color, ChromeRole role) {
  if (rect.w <= 0 || rect.h <= 0) return;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  chromeFillFlat(renderer, chromeGeomFor(rect, nullptr, role), toF(color), 0.7f);
}

void Primitives::strokeChromeShape(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color, int width,
                                  ChromeRole role, int inset) {
  if (rect.w <= 0 || rect.h <= 0 || width <= 0) return;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  const ChromeGeom g = insetGeom(chromeGeomFor(rect, nullptr, role), static_cast<float>(inset));
  chromeRing(renderer, g, insetGeom(g, static_cast<float>(width)), toF(color));
}

bool stickerShapeFromToken(const std::string& token, StickerShape* out) {
  if (token == "star") { *out = StickerShape::Star; return true; }
  if (token == "bolt") { *out = StickerShape::Bolt; return true; }
  if (token == "heart") { *out = StickerShape::Heart; return true; }
  if (token == "dot") { *out = StickerShape::Dot; return true; }
  if (token == "cross") { *out = StickerShape::Cross; return true; }
  return false;
}

namespace {

// Outline of a sticker in -1..1.
std::vector<SDL_FPoint> stickerOutline(StickerShape shape) {
  std::vector<SDL_FPoint> p;
  const float pi = 3.14159265f;
  switch (shape) {
    case StickerShape::Star:
      for (int i = 0; i < 10; ++i) {
        const float a = -pi / 2 + static_cast<float>(i) * pi / 5, r = (i % 2 == 0) ? 1.0f : 0.46f;
        p.push_back({r * std::cos(a), r * std::sin(a)});
      }
      break;
    case StickerShape::Bolt:
      p = {{-0.10f, -1.0f}, {0.62f, -1.0f}, {0.18f, -0.18f}, {0.58f, -0.18f},
           {-0.38f, 1.0f}, {-0.06f, 0.14f}, {-0.48f, 0.14f}};
      break;
    case StickerShape::Heart:
      for (int i = 0; i < 40; ++i) {
        const float t = static_cast<float>(i) / 40.0f * 2 * pi;
        const float x = 16 * std::pow(std::sin(t), 3.0f);
        const float y = 13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t);
        p.push_back({x / 17.0f, -y / 17.0f + 0.1f});
      }
      break;
    case StickerShape::Dot:
      for (int i = 0; i < 36; ++i) {
        const float a = static_cast<float>(i) / 36.0f * 2 * pi;
        p.push_back({std::cos(a), std::sin(a)});
      }
      break;
    case StickerShape::Cross: {
      const float w = 0.34f;
      p = {{-w, -1}, {w, -1}, {w, -w}, {1, -w}, {1, w}, {w, w}, {w, 1}, {-w, 1}, {-w, w}, {-1, w}, {-1, -w}, {-w, -w}};
      break;
    }
  }
  return p;
}

// Ear clipping: the bolt and the heart are not convex, so a fan from the
// middle would spill outside them.
std::vector<int> triangulate(const std::vector<SDL_FPoint>& p) {
  std::vector<int> out, idx(p.size());
  for (std::size_t i = 0; i < p.size(); ++i) idx[i] = static_cast<int>(i);
  float area = 0;
  for (std::size_t i = 0; i < p.size(); ++i) {
    const SDL_FPoint& a = p[i];
    const SDL_FPoint& b = p[(i + 1) % p.size()];
    area += a.x * b.y - b.x * a.y;
  }
  const float sign = area >= 0 ? 1.0f : -1.0f;
  auto cross = [](SDL_FPoint a, SDL_FPoint b, SDL_FPoint c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
  };
  int guard = 0;
  while (idx.size() > 3 && guard++ < 1000) {
    bool clipped = false;
    for (std::size_t i = 0; i < idx.size(); ++i) {
      const int i0 = idx[(i + idx.size() - 1) % idx.size()], i1 = idx[i], i2 = idx[(i + 1) % idx.size()];
      const SDL_FPoint& a = p[static_cast<std::size_t>(i0)];
      const SDL_FPoint& b = p[static_cast<std::size_t>(i1)];
      const SDL_FPoint& c = p[static_cast<std::size_t>(i2)];
      if (cross(a, b, c) * sign <= 0) continue;
      bool inside = false;
      for (int j : idx) {
        if (j == i0 || j == i1 || j == i2) continue;
        const SDL_FPoint& q = p[static_cast<std::size_t>(j)];
        if (cross(a, b, q) * sign >= 0 && cross(b, c, q) * sign >= 0 && cross(c, a, q) * sign >= 0) {
          inside = true;
          break;
        }
      }
      if (inside) continue;
      out.insert(out.end(), {i0, i1, i2});
      idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
      clipped = true;
      break;
    }
    if (!clipped) break;
  }
  if (idx.size() == 3) out.insert(out.end(), {idx[0], idx[1], idx[2]});
  return out;
}

SDL_FColor hueColour(float h, float sat, float val) {
  h = h - std::floor(h);
  const float r = std::clamp(std::fabs(h * 6 - 3) - 1, 0.0f, 1.0f);
  const float g = std::clamp(2 - std::fabs(h * 6 - 2), 0.0f, 1.0f);
  const float b = std::clamp(2 - std::fabs(h * 6 - 4), 0.0f, 1.0f);
  auto mix = [&](float c) { return val * (1 - sat + sat * c); };
  return {mix(r), mix(g), mix(b), 1.0f};
}

}  // namespace

void Primitives::drawSticker(SDL_Renderer* renderer, const ChromeSticker& sticker, float cx, float cy,
                             float sizePx, std::uint32_t seed) {
  if (sizePx < 4.0f) return;
  if (!gChrome.kit.dir.empty()) {
    static const char* const kNames[] = {"sticker_star.png", "sticker_bolt.png", "sticker_heart.png", "sticker_dot.png", "sticker_cross.png"};
    const std::size_t which = static_cast<std::size_t>(sticker.shape);
    int density = 1;
    SDL_Texture* tex = which < 5 ? kitTexture(renderer, kNames[which], 3, std::max(0.25f, gChrome.scale), &density) : nullptr;
    if (tex) {
      // The rendered foil is `stickerFoil` across inside a picture
      // `stickerImage` across (its border and shadow round it).
      const float side = sizePx * gChrome.kit.stickerImage / gChrome.kit.stickerFoil;
      const SDL_FRect dst {cx - side / 2, cy - side / 2, side, side};
      SDL_RenderTextureRotated(renderer, tex, nullptr, &dst, sticker.angle, nullptr, SDL_FLIP_NONE);
      (void)seed;
      return;
    }
  }
  const std::vector<SDL_FPoint> unit = stickerOutline(sticker.shape);
  const std::vector<int> tris = triangulate(unit);
  const float ang = sticker.angle * 3.14159265f / 180.0f, ca = std::cos(ang), sa = std::sin(ang);
  const float half = sizePx * 0.5f;
  const float turn = static_cast<float>(seed % 997) / 997.0f;
  // Each triangle is split into small ones so the foil's colours and its
  // bright streak run smoothly across it (geometry colours only blend
  // between corners).
  auto emit = [&](float grow, float dx, float dy, const std::function<SDL_FColor(float, float)>& colourAt) {
    std::vector<SDL_Vertex> v;
    std::function<void(SDL_FPoint, SDL_FPoint, SDL_FPoint, int)> split =
        [&](SDL_FPoint p0, SDL_FPoint p1, SDL_FPoint p2, int depth) {
          if (depth == 0) {
            for (SDL_FPoint q : {p0, p1, p2}) {
              const float x = q.x * grow, y = q.y * grow;
              SDL_Vertex vx {};
              vx.position = {cx + dx + (x * ca - y * sa) * half, cy + dy + (x * sa + y * ca) * half};
              vx.color = colourAt(q.x, q.y);
              v.push_back(vx);
            }
            return;
          }
          const SDL_FPoint m01 {(p0.x + p1.x) / 2, (p0.y + p1.y) / 2};
          const SDL_FPoint m12 {(p1.x + p2.x) / 2, (p1.y + p2.y) / 2};
          const SDL_FPoint m20 {(p2.x + p0.x) / 2, (p2.y + p0.y) / 2};
          split(p0, m01, m20, depth - 1);
          split(m01, p1, m12, depth - 1);
          split(m20, m12, p2, depth - 1);
          split(m01, m12, m20, depth - 1);
        };
    for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
      split(unit[static_cast<std::size_t>(tris[i])], unit[static_cast<std::size_t>(tris[i + 1])],
            unit[static_cast<std::size_t>(tris[i + 2])], 3);
    }
    SDL_RenderGeometry(renderer, nullptr, v.data(), static_cast<int>(v.size()), nullptr, 0);
  };
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  const float border = 1.0f + std::max(0.08f, 3.0f / half);   // the die-cut white edge
  const float lift = std::max(1.0f, sizePx * 0.03f);
  emit(border, lift * 0.6f, lift, [](float, float) { return SDL_FColor {0, 0, 0, 0.28f}; });
  emit(border, 0, 0, [](float, float) { return SDL_FColor {0.97f, 0.97f, 0.95f, 1.0f}; });
  const SDL_FColor base = toF(sticker.colour);
  const bool holo = sticker.holo;
  emit(1.0f, 0, 0, [&](float x, float y) {
    // A bright streak across the face, where the light catches it.
    const float u = (x * 0.8f + y * 0.6f) * 0.5f + 0.5f;
    const float d = (u - 0.35f) / 0.10f;
    const float streak = 0.55f * std::exp(-d * d);
    SDL_FColor c = holo ? hueColour(turn + x * 0.32f + y * 0.22f, 0.55f, 1.0f) : base;
    if (!holo) {
      const float shade = 0.08f * (y + 1) * 0.5f;
      c.r -= shade;
      c.g -= shade;
      c.b -= shade;
    }
    c.r += (1 - c.r) * streak;
    c.g += (1 - c.g) * streak;
    c.b += (1 - c.b) * streak;
    c.a = 1.0f;
    return c;
  });
}

void Primitives::fillChromeSlice(SDL_Renderer* renderer, const SDL_Rect& parent, const SDL_Rect& area,
                                SDL_Color color, int inset, ChromeRole role) {
  SDL_Rect clip;
  if (!SDL_GetRectIntersection(&parent, &area, &clip)) return;
  const bool clipped = SDL_RenderClipEnabled(renderer);
  SDL_Rect previous;
  SDL_GetRenderClipRect(renderer, &previous);
  if (clipped && !SDL_GetRectIntersection(&previous, &clip, &clip)) return;
  SDL_SetRenderClipRect(renderer, &clip);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  chromeFillFlat(renderer, insetGeom(chromeGeomFor(parent, nullptr, role), static_cast<float>(inset)),
                 toF(color), 0.0f);
  recordSurface(renderer, clip, color);
  SDL_SetRenderClipRect(renderer, clipped ? &previous : nullptr);
}

void Primitives::forgetChromeRenderer(SDL_Renderer* renderer) {
  forgetKitTextures(renderer);
  for (auto it = gChromeTextures.begin(); it != gChromeTextures.end();) {
    if (it->first.renderer == renderer) {
      if (it->second) SDL_DestroyTexture(it->second);
      it = gChromeTextures.erase(it);
    } else {
      ++it;
    }
  }
}

void Primitives::drawSpeakerGrille(SDL_Renderer* renderer, int x, int y,
                                    int width, int bars, SDL_Color color) {
  for (int index = 0; index < bars; ++index) {
    SDL_Rect slot{x, y + index * 7, width, 3};
    fillRect(renderer, slot, color);
  }
}

}  // namespace deckboy::render
