// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// output_renderer.hpp — Abstract output rendering pipeline interface.
//
// Defines the OutputRenderer abstract class that documents the 10-step
// sequence for rendering a deck's composited output to a window and
// external sinks (NDI, DeckLink, Siphon/Spout).
//
// This is a design/documentation interface — the actual implementation
// lives directly in the App class (app_render_output.ipp). The interface
// exists to formalize the rendering contract and enable future refactoring
// into a standalone renderer class.
//
// Rendering pipeline (per deck, per frame):
//   1. ensureCompositor() — create/resize the offscreen compositor texture
//   2. Clear to black
//   3. renderAllLayers()  — composite all active media layers
//   4. renderAudioVisualization() — waveform for audio-only cues
//   5. renderOverlays()   — lower-third text overlays
//   6. renderTimeInfo()   — timecode, cue ID, position overlay
//   7. applyDimmer()      — master brightness fade
//   8. presentCompositor() — blit compositor to the SDL output window
//   9. sendNdiFrame()     — send to NDI/DeckLink/Siphon outputs
//  10. Present to SDL window via SDL_RenderPresent
//
// Implementation: output_renderer.cpp (minimal — just namespace)
// Used by: app_render_output.ipp follows this pipeline structure.
// ============================================================================

#ifndef DECKBOY_RENDER_OUTPUT_RENDERER_HPP
#define DECKBOY_RENDER_OUTPUT_RENDERER_HPP

#include "core/sdl_compat.hpp"
#include <cstdint>
#include <string>

namespace deckboy::render {

class OutputRenderer {
 public:
  virtual ~OutputRenderer() = default;

  // Render the output window for the given deck. Returns false if rendering
  // should be skipped (e.g., missing media engine, window not ready).
  virtual bool render(int deckIndex) = 0;

 protected:
  // Subclass or App should implement these operations.
  // Each operation is kept abstract to allow flexibility in state management.
  
  // Ensure compositor texture is configured for target size
  virtual bool ensureCompositor(int deckIndex, int targetW, int targetH) = 0;

  // Render all layers for this output deck
  virtual void renderAllLayers(int deckIndex, const SDL_Rect& bounds) = 0;

  // Render audio-only cue visualization
  virtual void renderAudioVisualization(int deckIndex, int renderW, int renderH) = 0;

  // Render lower-third overlays
  virtual void renderOverlays(int deckIndex, int renderW, int renderH) = 0;

  // Render time overlay
  virtual void renderTimeInfo(int deckIndex, int renderW, int renderH) = 0;

  // Apply master dimmer overlay
  virtual void applyDimmer(double factor) = 0;

  // Present compositor to window
  virtual void presentCompositor(int deckIndex, int windowW, int windowH) = 0;

  // Send frame to NDI outputs
  virtual void sendNdiFrame(int deckIndex, int width, int height, double fps) = 0;
};

}  // namespace deckboy::render

#endif  // DECKBOY_RENDER_OUTPUT_RENDERER_HPP

