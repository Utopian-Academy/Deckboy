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
#include <cstdint>
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
ChromeGeom chromeGeomFor(const SDL_Rect& rect, bool* isControl) {
  const float s = std::max(0.25f, gChrome.scale);
  const bool control = rect.w <= 560.0f * s && rect.h <= 150.0f * s;
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

void Primitives::drawChrome(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color fill, SDL_Color border,
                            bool raised) {
  if (rect.w <= 0 || rect.h <= 0) return;
  bool control = false;
  const ChromeGeom g = chromeGeomFor(rect, &control);
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

void Primitives::fillChromeShape(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
  if (rect.w <= 0 || rect.h <= 0) return;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  chromeFillFlat(renderer, chromeGeomFor(rect, nullptr), toF(color), 0.7f);
}

void Primitives::strokeChromeShape(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color, int width) {
  if (rect.w <= 0 || rect.h <= 0 || width <= 0) return;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  const ChromeGeom g = chromeGeomFor(rect, nullptr);
  chromeRing(renderer, g, insetGeom(g, static_cast<float>(width)), toF(color));
}

void Primitives::forgetChromeRenderer(SDL_Renderer* renderer) {
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
