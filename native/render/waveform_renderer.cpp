// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// waveform_renderer.cpp — Audio waveform visualization implementation.
//
// Renders a symmetric waveform display within a bounding rectangle:
//   1. Draw dark background and border
//   2. Draw subtle center line (amplitude zero reference)
//   3. Draw peak bars: each column samples the peak array, drawing a
//      vertical bar centered on the midline. Bars within the in/out
//      range are colored differently from out-of-range bars.
//   4. Draw pause point markers (orange vertical lines)
//   5. Draw playhead (yellow/lime vertical line at current position)
//   6. Draw in/out markers (green/red vertical lines)
//
// If no peak data is available yet, shows "analyzing..." centered text.
//
// Header: waveform_renderer.hpp
// Used by: app_render_inspector.ipp (cue inspector waveform display).
// ============================================================================

#include "render/waveform_renderer.hpp"
#include "render/primitives.hpp"
#include "core/sdl_compat.hpp"
#include <algorithm>
#include <cmath>

namespace deckboy::render {

// ── Color palette for waveform elements ─────────────────────────────────────
// Defined as functions (not constexpr) because SDL_Color aggregate
// initialization is not constexpr in C++17 with all compilers.
namespace {
  // Deep dark background for waveform area
  SDL_Color kWaveformBg() { return {8, 16, 24, 255}; }
  // Mid-tone for waveform outline and center line
  SDL_Color kWaveformMid() { return {55, 100, 55, 255}; }
  // Highlight color for in-range waveform bars
  SDL_Color kWaveformInRange() { return {30, 60, 30, 255}; }
  // Muted color for out-of-range areas
  SDL_Color kWaveformOutRange() { return {80, 90, 80, 255}; }
  // Playhead (yellow/lime)
  SDL_Color kPlayhead() { return {200, 220, 80, 255}; }
  // In point marker (green)
  SDL_Color kInMarker() { return {80, 220, 80, 255}; }
  // Out point marker (red)
  SDL_Color kOutMarker() { return {220, 80, 80, 255}; }
  // Pause point (orange)
  SDL_Color kPauseMarker() { return {220, 120, 30, 200}; }
}

void WaveformRenderer::render(SDL_Renderer* renderer, const SDL_Rect& bounds,
                              const std::vector<float>& peaks,
                              float playFrac, float inFrac, float outFrac,
                              TTF_Font* fontSmall,
                              const std::vector<double>& pausePoints,
                              double duration) {
  if (!renderer) {
    return;
  }

  // Draw background and border
  Primitives::fillRect(renderer, bounds, kWaveformBg());
  Primitives::strokeRect(renderer, bounds, kWaveformMid());

  // If no peaks yet, show "analyzing..." message
  if (peaks.empty()) {
    if (fontSmall) {
      TextRenderer::drawCenteredText(renderer, fontSmall, "analyzing...",
                                    {180, 150, 100, 255}, bounds);
    }
    return;
  }

  int n = static_cast<int>(peaks.size());
  // Inset by 2px for the border, giving us the drawable waveform area
  int x0 = bounds.x + 2;
  int y0 = bounds.y + 2;
  int w = bounds.w - 4;
  int h = bounds.h - 4;
  int cy = y0 + h / 2;  // Center line: amplitude zero reference

  // Draw subtle center line (amplitude zero reference)
  SDL_SetRenderDrawColor(renderer, 30, 50, 30, 255);
  SDL_RenderLine(renderer, x0, cy, x0 + w, cy);

  // Draw waveform peaks: one vertical bar per pixel column, symmetric
  // around the center line. Each column samples the peak array using
  // nearest-neighbor interpolation (peaks may have fewer entries than pixels).
  for (int i = 0; i < w; ++i) {
    // Map pixel column → peak array index (nearest-neighbor)
    int pi = std::min(i * n / std::max(1, w), n - 1);
    float peak = peaks[pi];
    int halfH = std::max(1, static_cast<int>(peak * h / 2));
    // Determine if this column falls within the in/out edit range
    float frac = static_cast<float>(i) / std::max(1, w);
    bool inRange = (frac >= inFrac && frac <= outFrac);

    SDL_Color c = inRange ? kWaveformInRange() : kWaveformOutRange();
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);

    // Symmetric: draw bar from centre up and down
    SDL_RenderLine(renderer, x0 + i, cy - halfH, x0 + i, cy + halfH);
  }

  // Pause point markers: orange vertical lines at each pause point position.
  // Pause points are stored in seconds; convert to fractional position using duration.
  if (!pausePoints.empty() && duration > 0.0) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (double pp : pausePoints) {
      float ppFrac = static_cast<float>(std::clamp(pp / duration, 0.0, 1.0));
      int px = x0 + static_cast<int>(ppFrac * w);
      drawMarker(renderer, px, y0, y0 + h, 220, 120, 30, 200);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  }

  // Playhead (current position during playback)
  if (playFrac >= 0.0f && playFrac <= 1.0f) {
    int px = x0 + static_cast<int>(playFrac * w);
    drawMarker(renderer, px, y0, y0 + h, kPlayhead().r, kPlayhead().g, kPlayhead().b);
  }

  // In/out markers (editing points)
  if (inFrac > 0.0f) {
    int mx = x0 + static_cast<int>(inFrac * std::max(1, w));
    drawMarker(renderer, mx, y0, y0 + h, kInMarker().r, kInMarker().g, kInMarker().b);
  }
  if (outFrac < 1.0f) {
    int mx = x0 + static_cast<int>(outFrac * std::max(1, w));
    drawMarker(renderer, mx, y0, y0 + h, kOutMarker().r, kOutMarker().g, kOutMarker().b);
  }
}

// Draw a single vertical marker line from y0 to y1 at pixel column x.
// Used for playhead, in/out points, and pause point indicators.
void WaveformRenderer::drawMarker(SDL_Renderer* renderer, int x, int y0, int y1,
                                  Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
  if (!renderer) {
    return;
  }
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
  SDL_RenderLine(renderer, x, y0, x, y1);
}

}  // namespace deckboy::render
