// ---------------------------------------------------------------------------
// terrarium_render_rgba.hpp — Deckboy's pixel renderer for the Terrarium
// pattern sources. NOT upstream; keep it out of native/extras/upstream/.
//
// Upstream has two renderers. The glyph one (terrarium_visuals/glyphs/render)
// draws through an SDL_Renderer, which Deckboy's pattern path — a raw RGBA
// buffer handed to the media engine — cannot use. The pixelview one
// (terrarium_pixelview.hpp) is pure computation: give it a cell, get a colour.
// That is the renderer the Raspberry Pi panel runs, and it is where the recent
// work has gone (ocean swell, biome palettes, day/night, the harmony grade), so
// it is the one Deckboy carries.
//
// One cell becomes a cellPx × cellPx block of flat colour. At cellPx = 1 this
// is exactly the Pi's "pico" picture; larger values are the same image with
// bigger pixels, which is what you want on a program output — Deckboy's
// textures are nearest-filtered, so upscaling stays crisp either way.
// ---------------------------------------------------------------------------
#pragma once

#include "terrarium_vendor.hpp"

#include <cstdint>
#include <vector>

namespace terra {

// Frame size for a given cell size. The world is TERRA_W × TERRA_H (200 × 112),
// so cellPx = 8 reproduces the 1600 × 896 raster Deckboy has always used.
inline int frameWidthForCellPx(int cellPx) { return W * cellPx; }
inline int frameHeightForCellPx(int cellPx) { return H * cellPx; }

// animT drives everything that moves between simulation ticks — surf crests,
// drifting motes, fireflies. The sim only steps at 9 TPS, so if this were
// derived from `tick` alone the water would move in visible lurches. Passing a
// wall-clock value lets the picture breathe between steps, which is how the Pi
// panel runs it.
inline void renderWorldRgba(const World& world,
                            int tick,
                            float animT,
                            int cellPx,
                            std::vector<std::uint8_t>& out) {
  if (cellPx < 1) {
    cellPx = 1;
  }
  const int frameW = frameWidthForCellPx(cellPx);
  const int frameH = frameHeightForCellPx(cellPx);
  out.resize(static_cast<std::size_t>(frameW) * static_cast<std::size_t>(frameH) * 4u);

  for (int cy = 0; cy < H; ++cy) {
    for (int cx = 0; cx < W; ++cx) {
      const PixelviewRGB c = pixelviewCellColor(world, cx, cy, tick, animT);
      // Fill the cell's block. Written row-major so each destination row is a
      // contiguous run — this is the hot loop at 1600×896.
      for (int py = 0; py < cellPx; ++py) {
        const std::size_t rowStart =
          (static_cast<std::size_t>(cy * cellPx + py) * static_cast<std::size_t>(frameW)
           + static_cast<std::size_t>(cx * cellPx)) * 4u;
        std::uint8_t* p = out.data() + rowStart;
        for (int px = 0; px < cellPx; ++px) {
          p[0] = c.r;
          p[1] = c.g;
          p[2] = c.b;
          p[3] = 255;
          p += 4;
        }
      }
    }
  }
}

}  // namespace terra
