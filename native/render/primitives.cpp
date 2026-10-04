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

void Primitives::beginSurfaceFrame(SDL_Renderer* renderer, SDL_Color clearColor) {
  gSurfaceRenderer = renderer;
  gSurfaceClear = clearColor;
  gSurfaces.clear();
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
  fillRect(renderer, rect, body);
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

void Primitives::drawSpeakerGrille(SDL_Renderer* renderer, int x, int y, 
                                    int width, int bars, SDL_Color color) {
  for (int index = 0; index < bars; ++index) {
    SDL_Rect slot{x, y + index * 7, width, 3};
    fillRect(renderer, slot, color);
  }
}

}  // namespace deckboy::render
