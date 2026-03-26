// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Deckboy contributors

#ifndef DECKBOY_RENDER_OUTPUT_RENDERER_HPP
#define DECKBOY_RENDER_OUTPUT_RENDERER_HPP

#include <SDL2/SDL.h>
#include <cstdint>
#include <string>

namespace deckboy::render {

// OutputRenderer encapsulates the sequence of operations needed to render a deck's output window.
// This is a stateless facade; it does not own SDL resources. It serves as documentation and
// organization for rendering logic that lives in the App class.
//
// The output window rendering sequence:
// 1. Size and configure compositor texture if needed
// 2. Clear to black
// 3. Render all deck layers (media + composition)
// 4. Render audio visualization (waveform) for audio-only cues
// 5. Render lower-third overlays (stacked, color-coded)
// 6. Render time overlay (timecode, cue ID, position)
// 7. Apply master dimmer fade
// 8. Present compositor to window
// 9. Send NDI frame to outputs
// 10. Present to SDL window
//
// Each step is documented below as a pure virtual method that should be implemented
// in a renderer class or directly in App with proper SDL2 context.
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

