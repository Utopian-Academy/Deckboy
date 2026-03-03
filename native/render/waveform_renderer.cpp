// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Playboy contributors

#include "render/waveform_renderer.hpp"
#include "render/primitives.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>

namespace playboy::render {

// Color constants for waveform (matching App color scheme)
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
  int x0 = bounds.x + 2;
  int y0 = bounds.y + 2;
  int w = bounds.w - 4;
  int h = bounds.h - 4;
  int cy = y0 + h / 2;  // center line for symmetric waveform

  // Draw centre line (subtle grid reference)
  SDL_SetRenderDrawColor(renderer, 30, 50, 30, 255);
  SDL_RenderDrawLine(renderer, x0, cy, x0 + w, cy);

  // Draw waveform peaks (symmetric around center)
  for (int i = 0; i < w; ++i) {
    int pi = std::min(i * n / std::max(1, w), n - 1);
    float peak = peaks[pi];
    int halfH = std::max(1, static_cast<int>(peak * h / 2));
    float frac = static_cast<float>(i) / std::max(1, w);
    bool inRange = (frac >= inFrac && frac <= outFrac);

    SDL_Color c = inRange ? kWaveformInRange() : kWaveformOutRange();
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);

    // Symmetric: draw bar from centre up and down
    SDL_RenderDrawLine(renderer, x0 + i, cy - halfH, x0 + i, cy + halfH);
  }

  // Pause point ticks (orange verticals)
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

void WaveformRenderer::drawMarker(SDL_Renderer* renderer, int x, int y0, int y1,
                                  Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
  if (!renderer) {
    return;
  }
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
  SDL_RenderDrawLine(renderer, x, y0, x, y1);
}

}  // namespace playboy::render
