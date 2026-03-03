// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Playboy contributors

#ifndef PLAYBOY_RENDER_WAVEFORM_RENDERER_HPP
#define PLAYBOY_RENDER_WAVEFORM_RENDERER_HPP

#include "render/text_renderer.hpp"
#include <SDL2/SDL.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace playboy::render {

// WaveformRenderer visualizes audio waveforms in the UI.
// Displays peak data, playhead position, in/out markers, and pause points.
class WaveformRenderer {
 public:
  // Render waveform visualization within bounds
  // Parameters:
  //   renderer   - SDL renderer for drawing
  //   bounds     - Rectangle to contain the waveform
  //   peaks      - Array of peak heights (0.0-1.0 range)
  //   playFrac   - Current playhead position (0.0-1.0, -1 to hide)
  //   inFrac     - In point position (0.0-1.0)
  //   outFrac    - Out point position (0.0-1.0)
  //   pausePoints- Optional pause markers (in seconds)
  //   duration   - Total duration for pause point scaling
  //   fontSmall  - Font for "analyzing..." text
  static void render(SDL_Renderer* renderer, const SDL_Rect& bounds,
                     const std::vector<float>& peaks,
                     float playFrac, float inFrac, float outFrac,
                     TTF_Font* fontSmall = nullptr,
                     const std::vector<double>& pausePoints = {},
                     double duration = 0.0);

 private:
  // Helper to draw a marker line (in/out point, playhead, pause point)
  static void drawMarker(SDL_Renderer* renderer, int x, int y0, int y1,
                         Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255);
};

}  // namespace playboy::render

#endif  // PLAYBOY_RENDER_WAVEFORM_RENDERER_HPP
