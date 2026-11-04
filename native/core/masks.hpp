// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// MASKS
// ============================================================================
//
// James, 2026-11-04: pen-tool masks with points to add and remove, a library
// of shapes, invert, export, and a mask on a layer, an output or the matte.
// Board (approved the same day, with masks on cues as well, LFOs on all six
// controls and SVG export): https://claude.ai/artifact/X2N9qJe1ejpoVHPo2DqcDx
//
// SDL-free. The app owns where a mask is drawn (app_masks.ipp); this file owns
// what it IS: paths of cubic Bezier points, the shapes, the oscillators, the
// raster and the text forms (the show file and SVG).
//
// COORDINATES ARE FRACTIONS of whatever the mask is laid over -- the output's
// raster for a layer or output mask, the cue's own placed rectangle for a cue
// mask -- so one mask means the same thing at 720p and at 4K. A handle is an
// offset from its anchor in the same units. Shapes are built for a given
// aspect, so a circle is round on the screen it was made for.
//
// The raster: each closed path is scan-converted with four sub-rows and exact
// span coverage (anti-aliased edges), the paths are combined in order -- a
// subtract path cuts whatever the paths before it made -- and feather and grow
// are taken from an exact Euclidean distance transform of the result, so a
// feather is the same width all the way round, corners included.

#pragma once

#include "cue_effects.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace deckboy::masks {

struct MaskPoint {
  float x = 0.5f;   // anchor, fraction of the width
  float y = 0.5f;   // anchor, fraction of the height
  // Handles, relative to the anchor. Zero for a corner.
  float inX = 0.0f, inY = 0.0f;
  float outX = 0.0f, outY = 0.0f;
};

struct MaskPath {
  bool subtract = false;   // cuts the paths before it instead of adding
  bool closed = true;      // an open path is still being drawn: it fills nothing
  std::string name;
  std::vector<MaskPoint> points;
};

// The six things an operator moves (position counts twice, X and Y), each with
// its own oscillator. Indices into Mask::lfo; appended, never inserted.
enum MaskLfoSlot : int {
  kMaskLfoX = 0,
  kMaskLfoY,
  kMaskLfoScale,
  kMaskLfoRotation,
  kMaskLfoFeather,
  kMaskLfoGrow,
  kMaskLfoOpacity,
  kMaskLfoCount
};

struct Mask {
  std::string id;      // "m1": never contains ':', ',', '|' or space
  std::string name;
  std::vector<MaskPath> paths;
  bool invert = false;
  float feather = 0.0f;   // ramp width, fraction of the height, 0-0.25
  float grow = 0.0f;      // edge pushed out (+) or in (-), fraction of the height
  float opacity = 1.0f;   // how hard it cuts: 0 leaves the picture alone
  float offsetX = 0.0f;   // fraction of the width
  float offsetY = 0.0f;   // fraction of the height
  float scale = 1.0f;     // about the middle of its paths
  float rotation = 0.0f;  // degrees, clockwise on screen
  // Nine so it shares the geometry LFO field format; 7 and 8 are unused.
  std::array<deckboy::effects::ParamLfo, 9> lfo {};
};

// ── THE CONTROLS' RANGES ────────────────────────────────────────────────────
// The inspector, the network verbs and the oscillators all clamp to these, so
// a typed value and a value an LFO reaches can never disagree.
struct MaskParamRange {
  float lo;
  float hi;
};

inline MaskParamRange maskParamRange(int slot) {
  switch (slot) {
    case kMaskLfoX:
    case kMaskLfoY:        return {-1.0f, 1.0f};
    case kMaskLfoScale:    return {0.05f, 4.0f};
    case kMaskLfoRotation: return {-360.0f, 360.0f};
    case kMaskLfoFeather:  return {0.0f, 0.25f};
    case kMaskLfoGrow:     return {-0.25f, 0.25f};
    default:               return {0.0f, 1.0f};   // opacity
  }
}

inline float& maskParam(Mask& m, int slot) {
  switch (slot) {
    case kMaskLfoX:        return m.offsetX;
    case kMaskLfoY:        return m.offsetY;
    case kMaskLfoScale:    return m.scale;
    case kMaskLfoRotation: return m.rotation;
    case kMaskLfoFeather:  return m.feather;
    case kMaskLfoGrow:     return m.grow;
    default:               return m.opacity;
  }
}

inline float maskParam(const Mask& m, int slot) {
  return maskParam(const_cast<Mask&>(m), slot);
}

inline void clampMaskParams(Mask& m) {
  for (int slot = 0; slot < kMaskLfoCount; ++slot) {
    const MaskParamRange r = maskParamRange(slot);
    float& v = maskParam(m, slot);
    if (!std::isfinite(v)) v = slot == kMaskLfoScale || slot == kMaskLfoOpacity ? 1.0f : 0.0f;
    v = std::clamp(v, r.lo, r.hi);
  }
}

inline bool maskAnimates(const Mask& m) {
  for (int slot = 0; slot < kMaskLfoCount; ++slot) {
    if (m.lfo[static_cast<std::size_t>(slot)].on) return true;
  }
  return false;
}

// The mask as it stands at this moment: every oscillator applied, centred on
// the value the operator set (lfoApply's contract), over the control's range.
inline Mask resolveMask(const Mask& m, double seconds, double beats, double audio01) {
  Mask r = m;
  for (int slot = 0; slot < kMaskLfoCount; ++slot) {
    const auto& lfo = m.lfo[static_cast<std::size_t>(slot)];
    if (!lfo.on) continue;
    const MaskParamRange range = maskParamRange(slot);
    const float span = range.hi - range.lo;
    const float base01 = (maskParam(m, slot) - range.lo) / span;
    const float out01 = deckboy::effects::lfoApply(lfo, std::clamp(base01, 0.0f, 1.0f),
                                                   seconds, beats, audio01);
    maskParam(r, slot) = range.lo + out01 * span;
  }
  return r;
}

// ── THE SHAPE LIBRARY ───────────────────────────────────────────────────────
// The four James named, and four more to fill the row (his "perf" on the
// board). Each arrives as ordinary points: a star can become a lopsided star.
enum class MaskShape { Circle, Ellipse, Heart, Star, Rectangle, RoundedRect, Triangle, Hexagon, Count };

inline const char* maskShapeToken(MaskShape s) {
  switch (s) {
    case MaskShape::Circle:      return "circle";
    case MaskShape::Ellipse:     return "ellipse";
    case MaskShape::Heart:       return "heart";
    case MaskShape::Star:        return "star";
    case MaskShape::Rectangle:   return "rectangle";
    case MaskShape::RoundedRect: return "rounded";
    case MaskShape::Triangle:    return "triangle";
    case MaskShape::Hexagon:     return "hexagon";
    default:                     return "circle";
  }
}

inline const char* maskShapeLabel(MaskShape s) {
  switch (s) {
    case MaskShape::RoundedRect: return "rounded rect";
    default:                     return maskShapeToken(s);
  }
}

inline bool maskShapeFromToken(const std::string& token, MaskShape& out) {
  std::string t;
  for (char c : token) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (t == "rect" || t == "square") t = "rectangle";
  if (t == "roundrect" || t == "rounded-rect" || t == "roundedrect") t = "rounded";
  if (t == "oval") t = "ellipse";
  if (t == "hex") t = "hexagon";
  for (int i = 0; i < static_cast<int>(MaskShape::Count); ++i) {
    if (t == maskShapeToken(static_cast<MaskShape>(i))) {
      out = static_cast<MaskShape>(i);
      return true;
    }
  }
  return false;
}

namespace detail {

constexpr double kCircleHandle = 0.5522847498;   // 4/3 (sqrt 2 - 1)
constexpr double kPi = 3.14159265358979323846;

// Shapes are designed in HEIGHT units about the middle of the frame: u right,
// v down, 1 = the frame's height. Dividing u by the aspect makes them true on
// a frame of that shape.
struct ShapeBuilder {
  double aspect = 16.0 / 9.0;
  MaskPath path;
  void point(double u, double v, double iu = 0, double iv = 0, double ou = 0, double ov = 0) {
    MaskPoint p;
    p.x = static_cast<float>(0.5 + u / aspect);
    p.y = static_cast<float>(0.5 + v);
    p.inX = static_cast<float>(iu / aspect);
    p.inY = static_cast<float>(iv);
    p.outX = static_cast<float>(ou / aspect);
    p.outY = static_cast<float>(ov);
    path.points.push_back(p);
  }
  void ellipse(double rx, double ry) {
    const double a = rx * kCircleHandle, b = ry * kCircleHandle;
    point(0, -ry, -a, 0, a, 0);
    point(rx, 0, 0, -b, 0, b);
    point(0, ry, a, 0, -a, 0);
    point(-rx, 0, 0, b, 0, -b);
  }
  void polygon(int corners, double r, double inner, double startDeg, double dv = 0) {
    const int n = inner > 0 ? corners * 2 : corners;
    for (int i = 0; i < n; ++i) {
      const double q = (inner > 0 && (i % 2)) ? inner : r;
      const double t = (startDeg + 360.0 * i / n) * kPi / 180.0;
      point(std::cos(t) * q, std::sin(t) * q + dv);
    }
  }
};

}  // namespace detail

// `aspect` is the width over the height of what the mask will sit on.
inline MaskPath makeMaskShape(MaskShape shape, double aspect) {
  detail::ShapeBuilder b;
  b.aspect = aspect > 0.05 ? aspect : 16.0 / 9.0;
  b.path.name = maskShapeLabel(shape);
  switch (shape) {
    case MaskShape::Circle:  b.ellipse(0.30, 0.30); break;
    case MaskShape::Ellipse: b.ellipse(0.46, 0.28); break;
    case MaskShape::Heart: {
      // Two lobes and a point; the bounding box is centred on the frame.
      const double s = 0.33, dv = -0.14 * s;
      b.point(0, -0.36 * s + dv, 0.16 * s, -0.40 * s, -0.16 * s, -0.40 * s);
      b.point(-0.84 * s, -0.16 * s + dv, -0.04 * s, -0.44 * s, 0.06 * s, 0.50 * s);
      b.point(0, 1.0 * s + dv, -0.50 * s, -0.46 * s, 0.50 * s, -0.46 * s);
      b.point(0.84 * s, -0.16 * s + dv, -0.06 * s, 0.50 * s, 0.04 * s, -0.44 * s);
      break;
    }
    case MaskShape::Star:      b.polygon(5, 0.33, 0.33 * 0.382, -90.0, 0.033); break;
    case MaskShape::Rectangle: {
      const double w = 0.42, h = 0.28;
      b.point(-w, -h); b.point(w, -h); b.point(w, h); b.point(-w, h);
      break;
    }
    case MaskShape::RoundedRect: {
      const double w = 0.42, h = 0.28, r = 0.07, k = r * detail::kCircleHandle;
      b.point(-w + r, -h, -k, 0, 0, 0);  b.point(w - r, -h, 0, 0, k, 0);
      b.point(w, -h + r, 0, -k, 0, 0);   b.point(w, h - r, 0, 0, 0, k);
      b.point(w - r, h, k, 0, 0, 0);     b.point(-w + r, h, 0, 0, -k, 0);
      b.point(-w, h - r, 0, k, 0, 0);    b.point(-w, -h + r, 0, 0, 0, -k);
      break;
    }
    case MaskShape::Triangle: b.polygon(3, 0.36, 0.0, -90.0, 0.36 / 4.0); break;
    case MaskShape::Hexagon:  b.polygon(6, 0.31, 0.0, -90.0); break;
    default: b.ellipse(0.3, 0.3); break;
  }
  return b.path;
}

// ── GEOMETRY ────────────────────────────────────────────────────────────────

struct Pt {
  double x = 0.0;
  double y = 0.0;
};

// One path as a polygon in pixels of a W x H frame, before the mask's move.
// Curves are cut finely enough that no chord strays more than a fraction of a
// pixel from the curve.
inline std::vector<Pt> flattenMaskPath(const MaskPath& path, double W, double H) {
  std::vector<Pt> out;
  const auto& p = path.points;
  const std::size_t n = p.size();
  if (n == 0) return out;
  const std::size_t segments = path.closed ? n : n - 1;
  out.push_back({p[0].x * W, p[0].y * H});
  for (std::size_t i = 0; i < segments; ++i) {
    const MaskPoint& a = p[i];
    const MaskPoint& b = p[(i + 1) % n];
    const Pt p0 {a.x * W, a.y * H};
    const Pt p1 {(a.x + a.outX) * W, (a.y + a.outY) * H};
    const Pt p2 {(b.x + b.inX) * W, (b.y + b.inY) * H};
    const Pt p3 {b.x * W, b.y * H};
    const bool straight = a.outX == 0.0f && a.outY == 0.0f && b.inX == 0.0f && b.inY == 0.0f;
    if (!straight) {
      const double len = std::hypot(p1.x - p0.x, p1.y - p0.y) + std::hypot(p2.x - p1.x, p2.y - p1.y) +
                         std::hypot(p3.x - p2.x, p3.y - p2.y);
      const int steps = std::clamp(static_cast<int>(std::ceil(len / 3.0)), 2, 512);
      for (int s = 1; s < steps; ++s) {
        const double t = static_cast<double>(s) / steps, u = 1.0 - t;
        out.push_back({u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
                       u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y});
      }
    }
    if (i + 1 < segments || !path.closed) out.push_back(p3);
  }
  return out;
}

// The middle the mask scales and turns about: the centre of the bounding box
// of every closed path's outline. Fixed by the points, never by the oscillators,
// so a mask on a rotation LFO turns in place.
inline Pt maskPivot(const Mask& m, double W, double H) {
  double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
  for (const MaskPath& path : m.paths) {
    if (!path.closed) continue;
    for (const Pt& q : flattenMaskPath(path, W, H)) {
      x0 = std::min(x0, q.x); y0 = std::min(y0, q.y);
      x1 = std::max(x1, q.x); y1 = std::max(y1, q.y);
    }
  }
  if (x0 > x1) return {W * 0.5, H * 0.5};
  return {(x0 + x1) * 0.5, (y0 + y1) * 0.5};
}

// Every closed path as a polygon in pixels, with the mask's own move applied.
struct PlacedPath {
  bool subtract = false;
  std::vector<Pt> polygon;
};

inline std::vector<PlacedPath> placeMaskPaths(const Mask& m, int W, int H) {
  std::vector<PlacedPath> out;
  const Pt pivot = maskPivot(m, W, H);
  const double rad = m.rotation * detail::kPi / 180.0;
  const double c = std::cos(rad) * m.scale, s = std::sin(rad) * m.scale;
  const double dx = m.offsetX * W, dy = m.offsetY * H;
  for (const MaskPath& path : m.paths) {
    if (!path.closed || path.points.size() < 3) continue;
    PlacedPath placed;
    placed.subtract = path.subtract;
    placed.polygon = flattenMaskPath(path, W, H);
    for (Pt& q : placed.polygon) {
      const double ux = q.x - pivot.x, uy = q.y - pivot.y;
      q.x = pivot.x + c * ux - s * uy + dx;
      q.y = pivot.y + s * ux + c * uy + dy;
    }
    out.push_back(std::move(placed));
  }
  return out;
}

// ── THE RASTER ──────────────────────────────────────────────────────────────

namespace detail {

constexpr int kSubRows = 4;

// Coverage of one polygon, nonzero winding, 0-1 per pixel.
inline void polygonCoverage(const std::vector<Pt>& poly, int W, int H, std::vector<float>& cov) {
  cov.assign(static_cast<std::size_t>(W) * H, 0.0f);   // reuses the buffer's memory
  struct Edge {
    double x0, y0, x1, y1;
    int dir;
  };
  std::vector<Edge> edges;
  double ymin = 1e300, ymax = -1e300;
  const std::size_t n = poly.size();
  for (std::size_t i = 0; i < n; ++i) {
    const Pt& a = poly[i];
    const Pt& b = poly[(i + 1) % n];
    if (a.y == b.y) continue;
    edges.push_back(a.y < b.y ? Edge {a.x, a.y, b.x, b.y, 1} : Edge {b.x, b.y, a.x, a.y, -1});
    ymin = std::min(ymin, std::min(a.y, b.y));
    ymax = std::max(ymax, std::max(a.y, b.y));
  }
  if (edges.empty()) return;
  const int rowFirst = std::max(0, static_cast<int>(std::floor(ymin)));
  const int rowLast = std::min(H, static_cast<int>(std::ceil(ymax)) + 1);
  if (rowFirst >= rowLast) return;
  deckboy::effects::detail::parallelRows(rowLast - rowFirst, W, [&](int first, int last) {
    std::vector<std::pair<double, int>> hits;
    std::vector<float> diff(static_cast<std::size_t>(W) + 1);
    const float w = 1.0f / kSubRows;
    for (int r = first; r < last; ++r) {
      const int y = rowFirst + r;
      float* row = cov.data() + static_cast<std::size_t>(y) * W;
      std::fill(diff.begin(), diff.end(), 0.0f);
      for (int k = 0; k < kSubRows; ++k) {
        const double sy = y + (k + 0.5) / kSubRows;
        hits.clear();
        for (const Edge& e : edges) {
          if (sy < e.y0 || sy >= e.y1) continue;
          hits.emplace_back(e.x0 + (sy - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.dir);
        }
        if (hits.size() < 2) continue;
        std::sort(hits.begin(), hits.end());
        int winding = 0;
        for (std::size_t h = 0; h + 1 < hits.size(); ++h) {
          winding += hits[h].second;
          if (winding == 0) continue;
          const double xa = std::clamp(hits[h].first, 0.0, static_cast<double>(W));
          const double xb = std::clamp(hits[h + 1].first, 0.0, static_cast<double>(W));
          if (xb <= xa) continue;
          const int ia = static_cast<int>(xa), ib = static_cast<int>(xb);
          if (ia == ib) {
            row[ia] += static_cast<float>(xb - xa) * w;
            continue;
          }
          row[ia] += static_cast<float>(ia + 1 - xa) * w;
          diff[static_cast<std::size_t>(ia + 1)] += w;
          diff[static_cast<std::size_t>(ib)] -= w;
          if (ib < W) row[ib] += static_cast<float>(xb - ib) * w;
        }
      }
      float run = 0.0f;
      for (int x = 0; x < W; ++x) {
        run += diff[static_cast<std::size_t>(x)];
        row[x] = std::min(1.0f, row[x] + run);
      }
    }
  });
}

// Squared Euclidean distance transform in one dimension (Felzenszwalb and
// Huttenlocher): f holds 0 at a feature and a large number elsewhere.
inline void edt1d(const float* f, int n, float* d, int* v, float* z, int* arg = nullptr) {
  int k = 0;
  v[0] = 0;
  z[0] = -1e30f;
  z[1] = 1e30f;
  for (int q = 1; q < n; ++q) {
    float s;
    for (;;) {
      const int p = v[k];
      s = ((f[q] + static_cast<float>(q) * q) - (f[p] + static_cast<float>(p) * p)) /
          (2.0f * static_cast<float>(q - p));
      if (s > z[k] || k == 0) break;
      --k;
    }
    if (s <= z[k]) {   // k == 0 and the new parabola replaces the first
      v[0] = q;
      z[0] = -1e30f;
      z[1] = 1e30f;
      continue;
    }
    ++k;
    v[k] = q;
    z[k] = s;
    z[k + 1] = 1e30f;
  }
  k = 0;
  for (int q = 0; q < n; ++q) {
    while (z[k + 1] < static_cast<float>(q)) ++k;
    const float dq = static_cast<float>(q - v[k]);
    d[q] = dq * dq + f[v[k]];
    if (arg) arg[q] = v[k];
  }
}

// Coverage in, alpha out: the signed distance from the edge (positive inside,
// in this grid's cells), pushed out by `grow`, ramped over `ramp`.
//
// THE EDGE IS FOUND WHERE THE COVERAGE SAYS, NOT AT A CELL BOUNDARY. Edge
// cells -- partly covered, or next to a cell on the other side -- are the
// features; a cell's distance is to the NEAREST edge cell, plus how far that
// cell's own coverage puts the edge from its centre (coverage 0.5 = on it).
// Thresholding to inside/outside instead put a feathered edge up to half a
// cell off, which on the coarse grid a wide feather uses was 4-5 px.
inline void signedRamp(std::vector<float>& cov, int W, int H, double growPx, double ramp) {
  constexpr float kFar = 1e20f;
  const std::size_t count = static_cast<std::size_t>(W) * H;
  std::vector<float> dist(count, kFar);
  bool anyEdge = false;
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * W + x;
      const float c = cov[i];
      auto partial = [](float v) { return v > 0.02f && v < 0.98f; };
      bool edge = partial(c);
      if (!edge) {
        // A whole cell is an edge only where it meets a whole cell of the
        // other side: an edge lying exactly on the grid. Next to a partial
        // cell it is not, or it would pin the ramp flat beside the real edge.
        const bool in = c >= 0.5f;
        auto across = [&](std::size_t j) { return !partial(cov[j]) && (cov[j] >= 0.5f) != in; };
        edge = (x > 0 && across(i - 1)) || (x + 1 < W && across(i + 1)) ||
               (y > 0 && across(i - W)) || (y + 1 < H && across(i + W));
      }
      if (edge) {
        dist[i] = 0.0f;
        anyEdge = true;
      }
    }
  }
  if (!anyEdge) {
    for (float& c : cov) c = c >= 0.5f ? 1.0f : 0.0f;   // all one side: nothing to move
    return;
  }
  // Columns: the nearest edge row in each column, and its squared distance.
  std::vector<int> nearRow(count, 0), nearest(count, 0);
  deckboy::effects::detail::parallelRows(W, H, [&](int first, int last) {
    std::vector<float> f(static_cast<std::size_t>(H)), d(static_cast<std::size_t>(H));
    std::vector<int> v(static_cast<std::size_t>(H)), arg(static_cast<std::size_t>(H));
    std::vector<float> z(static_cast<std::size_t>(H) + 1);
    for (int x = first; x < last; ++x) {
      for (int y = 0; y < H; ++y) f[static_cast<std::size_t>(y)] = dist[static_cast<std::size_t>(y) * W + x];
      edt1d(f.data(), H, d.data(), v.data(), z.data(), arg.data());
      for (int y = 0; y < H; ++y) {
        dist[static_cast<std::size_t>(y) * W + x] = d[static_cast<std::size_t>(y)];
        nearRow[static_cast<std::size_t>(y) * W + x] = arg[static_cast<std::size_t>(y)];
      }
    }
  });
  // Rows: the nearest column, whose nearest row completes the edge cell.
  deckboy::effects::detail::parallelRows(H, W, [&](int first, int last) {
    std::vector<float> f(static_cast<std::size_t>(W)), d(static_cast<std::size_t>(W));
    std::vector<int> v(static_cast<std::size_t>(W)), arg(static_cast<std::size_t>(W));
    std::vector<float> z(static_cast<std::size_t>(W) + 1);
    for (int y = first; y < last; ++y) {
      const std::size_t row = static_cast<std::size_t>(y) * W;
      for (int x = 0; x < W; ++x) f[static_cast<std::size_t>(x)] = dist[row + x];
      edt1d(f.data(), W, d.data(), v.data(), z.data(), arg.data());
      for (int x = 0; x < W; ++x) {
        const int ex = arg[static_cast<std::size_t>(x)];
        const int ey = nearRow[row + static_cast<std::size_t>(ex)];
        dist[row + x] = d[static_cast<std::size_t>(x)];
        nearest[row + x] = ey * W + ex;
      }
    }
  });
  deckboy::effects::detail::parallelRows(H, W, [&](int first, int last) {
    for (std::size_t i = static_cast<std::size_t>(first) * W; i < static_cast<std::size_t>(last) * W; ++i) {
      const double reach = std::sqrt(static_cast<double>(dist[i]));
      const double edgeShift = static_cast<double>(cov[static_cast<std::size_t>(nearest[i])]) - 0.5;
      const double sd = (cov[i] >= 0.5f ? reach : -reach) + edgeShift;
      dist[i] = static_cast<float>(std::clamp((sd + growPx) / ramp + 0.5, 0.0, 1.0));
    }
  });
  cov.swap(dist);
}

}  // namespace detail

// What the mask is worth where no path reaches: the frame outside a cue's
// rectangle, for a cue mask. Invert and opacity applied.
inline float maskValueOutside(const Mask& m) {
  const float shape = m.invert ? 1.0f : 0.0f;
  return 1.0f - std::clamp(m.opacity, 0.0f, 1.0f) * (1.0f - shape);
}

// The mask as an alpha plane, 0-255, row by row: 255 keeps the picture, 0 cuts
// it. `m` should already be resolved (resolveMask) for the moment drawn.
inline std::vector<std::uint8_t> rasterizeMask(const Mask& m, int W, int H) {
  std::vector<std::uint8_t> out;
  if (W <= 0 || H <= 0) return out;
  const std::size_t count = static_cast<std::size_t>(W) * H;
  std::vector<float> total, one;
  bool first = true;
  for (const PlacedPath& path : placeMaskPaths(m, W, H)) {
    detail::polygonCoverage(path.polygon, W, H, one);
    if (first) {
      // Nothing before it: an add IS the total, and a subtract cuts nothing.
      if (path.subtract) std::fill(one.begin(), one.end(), 0.0f);
      total.swap(one);
      first = false;
      continue;
    }
    const bool cut = path.subtract;
    deckboy::effects::detail::parallelRows(H, W, [&](int r0, int r1) {
      const std::size_t end = static_cast<std::size_t>(r1) * W;
      for (std::size_t i = static_cast<std::size_t>(r0) * W; i < end; ++i) {
        total[i] = cut ? total[i] * (1.0f - one[i]) : total[i] + one[i] - total[i] * one[i];
      }
    });
  }
  if (first) total.assign(count, 0.0f);
  const double featherPx = std::max(0.0, static_cast<double>(m.feather)) * H;
  const double growPx = static_cast<double>(m.grow) * H;
  if (featherPx >= 0.5 || std::fabs(growPx) >= 0.5) {
    const double ramp = std::max(1.0, featherPx);
    // A FEATHER CAN BE COMPUTED SMALL: its whole job is to soften the edge, so
    // the distance field is taken on a grid k times coarser -- never fewer than
    // four cells across the ramp -- and sampled back up bilinearly. Measured
    // at 1080p with a 3% feather: 17.7 ms at full size.
    const int k = ramp >= 8.0 ? std::clamp(static_cast<int>(ramp / 4.0), 1, 8) : 1;
    if (k == 1) {
      detail::signedRamp(total, W, H, growPx, ramp);
    } else {
      const int w = (W + k - 1) / k, h = (H + k - 1) / k;
      std::vector<float> small(static_cast<std::size_t>(w) * h, 0.0f);
      deckboy::effects::detail::parallelRows(h, w, [&](int r0, int r1) {
        for (int sy = r0; sy < r1; ++sy) {
          for (int sx = 0; sx < w; ++sx) {
            float sum = 0.0f;
            int n = 0;
            for (int y = sy * k; y < std::min(H, sy * k + k); ++y) {
              const float* row = total.data() + static_cast<std::size_t>(y) * W;
              for (int x = sx * k; x < std::min(W, sx * k + k); ++x, ++n) sum += row[x];
            }
            small[static_cast<std::size_t>(sy) * w + sx] = n ? sum / n : 0.0f;
          }
        }
      });
      detail::signedRamp(small, w, h, growPx / k, ramp / k);
      deckboy::effects::detail::parallelRows(H, W, [&](int r0, int r1) {
        for (int y = r0; y < r1; ++y) {
          const double fy = std::clamp((y + 0.5) / k - 0.5, 0.0, static_cast<double>(h - 1));
          const int y0 = static_cast<int>(fy), y1 = std::min(h - 1, y0 + 1);
          const float ty = static_cast<float>(fy - y0);
          float* row = total.data() + static_cast<std::size_t>(y) * W;
          const float* a = small.data() + static_cast<std::size_t>(y0) * w;
          const float* b = small.data() + static_cast<std::size_t>(y1) * w;
          for (int x = 0; x < W; ++x) {
            const double fx = std::clamp((x + 0.5) / k - 0.5, 0.0, static_cast<double>(w - 1));
            const int x0 = static_cast<int>(fx), x1 = std::min(w - 1, x0 + 1);
            const float tx = static_cast<float>(fx - x0);
            const float top = a[x0] + (a[x1] - a[x0]) * tx;
            const float bottom = b[x0] + (b[x1] - b[x0]) * tx;
            row[x] = top + (bottom - top) * ty;
          }
        }
      });
    }
  }
  const float strength = std::clamp(m.opacity, 0.0f, 1.0f);
  out.resize(count);
  const bool invert = m.invert;
  deckboy::effects::detail::parallelRows(H, W, [&](int r0, int r1) {
    const std::size_t end = static_cast<std::size_t>(r1) * W;
    for (std::size_t i = static_cast<std::size_t>(r0) * W; i < end; ++i) {
      float a = invert ? 1.0f - total[i] : total[i];
      a = 1.0f - strength * (1.0f - a);
      out[i] = static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
  });
  return out;
}

// ── TEXT FORMS ──────────────────────────────────────────────────────────────

namespace detail {

inline std::string percentEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '.' || c == '_' || c == '-') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 15]);
    }
  }
  return out;
}

inline std::string percentDecode(const std::string& s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      const std::string hx = s.substr(i + 1, 2);
      if (std::isxdigit(static_cast<unsigned char>(hx[0])) &&
          std::isxdigit(static_cast<unsigned char>(hx[1]))) {
        out.push_back(static_cast<char>(std::strtol(hx.c_str(), nullptr, 16)));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

inline std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (;;) {
    const std::size_t at = s.find(sep, start);
    out.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
    if (at == std::string::npos) break;
    start = at + 1;
  }
  return out;
}

inline float toFloat(const std::string& s, float fallback) {
  if (s.empty()) return fallback;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  return (end && end != s.c_str() && std::isfinite(v)) ? static_cast<float>(v) : fallback;
}

inline std::string num(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.9g", v);   // a float, exactly
  return buf;
}

}  // namespace detail

// One mask on one line of the show file:
//   id|name|invert|feather|grow|opacity|x|y|scale|rotation|lfos|paths
// paths: ';' between paths; each is flags:name:numbers, flags "a" or "s" then
// "c" or "o", numbers six to a point (x y inX inY outX outY) split by spaces.
// Every string is %-escaped, so no name can break the record. The oscillators
// arrive already in their own text form (the geometry LFO field's).
inline std::string serializeMask(const Mask& m, const std::string& lfoText) {
  using namespace detail;
  std::ostringstream out;
  out << percentEncode(m.id) << '|' << percentEncode(m.name) << '|' << (m.invert ? 1 : 0) << '|'
      << num(m.feather) << '|' << num(m.grow) << '|' << num(m.opacity) << '|' << num(m.offsetX) << '|'
      << num(m.offsetY) << '|' << num(m.scale) << '|' << num(m.rotation) << '|' << percentEncode(lfoText) << '|';
  for (std::size_t i = 0; i < m.paths.size(); ++i) {
    const MaskPath& p = m.paths[i];
    if (i) out << ';';
    out << (p.subtract ? 's' : 'a') << (p.closed ? 'c' : 'o') << ':' << percentEncode(p.name) << ':';
    for (std::size_t k = 0; k < p.points.size(); ++k) {
      const MaskPoint& q = p.points[k];
      if (k) out << ' ';
      out << num(q.x) << ' ' << num(q.y) << ' ' << num(q.inX) << ' ' << num(q.inY) << ' ' << num(q.outX)
          << ' ' << num(q.outY);
    }
  }
  return out.str();
}

// False for a line that is not a mask. `lfoText` receives the oscillators'
// text for the caller to parse with its own reader.
inline bool parseMask(const std::string& text, Mask& m, std::string& lfoText) {
  using namespace detail;
  const auto f = split(text, '|');
  if (f.size() < 12) return false;
  m = Mask {};
  m.id = percentDecode(f[0]);
  if (m.id.empty()) return false;
  m.name = percentDecode(f[1]);
  m.invert = f[2] == "1";
  m.feather = toFloat(f[3], 0.0f);
  m.grow = toFloat(f[4], 0.0f);
  m.opacity = toFloat(f[5], 1.0f);
  m.offsetX = toFloat(f[6], 0.0f);
  m.offsetY = toFloat(f[7], 0.0f);
  m.scale = toFloat(f[8], 1.0f);
  m.rotation = toFloat(f[9], 0.0f);
  clampMaskParams(m);
  lfoText = percentDecode(f[10]);
  if (!f[11].empty()) {
    for (const std::string& one : split(f[11], ';')) {
      const auto parts = split(one, ':');
      if (parts.size() < 3 || parts[0].size() < 2) continue;
      MaskPath path;
      path.subtract = parts[0][0] == 's';
      path.closed = parts[0][1] != 'o';
      path.name = percentDecode(parts[1]);
      std::vector<float> v;
      std::istringstream in(parts[2]);
      std::string tok;
      while (in >> tok) v.push_back(toFloat(tok, 0.0f));
      for (std::size_t k = 0; k + 5 < v.size(); k += 6) {
        MaskPoint q;
        q.x = std::clamp(v[k], -4.0f, 5.0f);
        q.y = std::clamp(v[k + 1], -4.0f, 5.0f);
        q.inX = v[k + 2]; q.inY = v[k + 3]; q.outX = v[k + 4]; q.outY = v[k + 5];
        path.points.push_back(q);
      }
      m.paths.push_back(std::move(path));
    }
  }
  return true;
}

// The mask as SVG, for Illustrator and anything else that reads paths: one
// <path> per path in the frame's pixels, painted in order on black -- white
// adds, black subtracts, which is exactly how the raster combines them for a
// hard-edged mask. The move is a transform on the group, so the points stay
// the points; feather is a blur and grow a stroke, both approximations, and
// the comment says so.
inline std::string maskToSvg(const Mask& m, int W, int H) {
  using namespace detail;
  const Pt pivot = maskPivot(m, W, H);
  const char* on = m.invert ? "#000000" : "#ffffff";
  const char* off = m.invert ? "#ffffff" : "#000000";
  std::ostringstream out;
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << W << "\" height=\"" << H
      << "\" viewBox=\"0 0 " << W << ' ' << H << "\">\n"
      << "<!-- Deckboy mask \"" << m.name << "\": white keeps the picture, black cuts it."
      << " Feather and grow are drawn approximately; opacity " << num(m.opacity) << ". -->\n"
      << "<title>" << m.name << "</title>\n";
  const double blur = std::max(0.0, static_cast<double>(m.feather)) * H / 2.5;
  if (blur > 0.25) {
    out << "<defs><filter id=\"feather\" x=\"-50%\" y=\"-50%\" width=\"200%\" height=\"200%\">"
        << "<feGaussianBlur stdDeviation=\"" << num(blur) << "\"/></filter></defs>\n";
  }
  out << "<rect width=\"" << W << "\" height=\"" << H << "\" fill=\"" << off << "\"/>\n";
  out << "<g" << (blur > 0.25 ? " filter=\"url(#feather)\"" : "") << " transform=\"translate("
      << num(pivot.x + m.offsetX * W) << ' ' << num(pivot.y + m.offsetY * H) << ") rotate(" << num(m.rotation)
      << ") scale(" << num(m.scale) << ") translate(" << num(-pivot.x) << ' ' << num(-pivot.y) << ")\">\n";
  const double growPx = static_cast<double>(m.grow) * H / std::max(0.05f, m.scale);
  for (const MaskPath& path : m.paths) {
    if (path.points.empty()) continue;
    const auto& p = path.points;
    std::ostringstream d;
    d << 'M' << num(p[0].x * W) << ' ' << num(p[0].y * H);
    const std::size_t n = p.size(), segs = path.closed ? n : n - 1;
    for (std::size_t i = 0; i < segs; ++i) {
      const MaskPoint& a = p[i];
      const MaskPoint& b = p[(i + 1) % n];
      d << " C" << num((a.x + a.outX) * W) << ' ' << num((a.y + a.outY) * H) << ' ' << num((b.x + b.inX) * W)
        << ' ' << num((b.y + b.inY) * H) << ' ' << num(b.x * W) << ' ' << num(b.y * H);
    }
    if (path.closed) d << " Z";
    const char* paint = path.subtract ? off : on;
    out << "<path";
    if (!path.name.empty()) out << " id=\"" << percentEncode(path.name) << "\"";
    if (!path.closed) {
      out << " d=\"" << d.str() << "\" fill=\"none\" stroke=\"#ff0000\" stroke-width=\"2\"/>\n";
      continue;
    }
    out << " d=\"" << d.str() << "\" fill=\"" << paint << "\"";
    if (std::fabs(growPx) >= 0.5) {
      // Out: a stroke in the path's own paint. In: a stroke in the other one.
      const bool growsOut = (growPx > 0) != path.subtract;
      out << " stroke=\"" << (growsOut ? on : off) << "\" stroke-width=\"" << num(std::fabs(growPx) * 2)
          << "\" stroke-linejoin=\"round\" paint-order=\"" << (growsOut ? "stroke" : "fill stroke") << "\"";
    }
    out << "/>\n";
  }
  out << "</g>\n</svg>\n";
  return out.str();
}

}  // namespace deckboy::masks
