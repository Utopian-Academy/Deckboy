// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// primitives.hpp — Low-level drawing primitives for the Deckboy UI.
//
// Stateless utility class providing basic 2D drawing operations used by
// all Deckboy UI components. All methods are static — no state is held.
//
//   fillRect()        — solid color rectangle fill (alpha-blended)
//   strokeRect()      — rectangle outline (1px border)
//   drawFramedPanel() — beveled panel: fill + border + highlight/shadow edges
//   drawSpeakerGrille() — decorative horizontal bar pattern (cosmetic detail)
//
// Implementation: primitives.cpp
// Used by: waveform_renderer.cpp, app_render_*.ipp, main.cpp UI drawing.
// ============================================================================

#pragma once

#include <string>

#include "core/sdl_compat.hpp"

namespace deckboy::render {

// SKINS: SHAPE AND FINISH. A theme's colours say what the desk looks like;
// its style_* lines say what it is made of. With `enabled` false (every theme
// without style_* lines) the panels draw exactly as they always have.
//
// Two roles, told apart the way the press tracker tells them (a control is at
// most a row of the inspector wide and a bottom-bar button tall): PANELS are
// the large surfaces, CONTROLS everything an operator presses. All lengths are
// at 1x and are multiplied by `scale`, the UI scale the fonts were loaded at,
// so a skin scales with the desk.
enum class ChromeShape { Rect, Round, Pill, Chamfer, Blob };
enum class ChromeRole { Auto, Panel, Control };
enum class ChromeTexture { None, Brushed, Grain, Crackle, Dots, Wood, Carbon, Halftone, Circuit, Mesh };

struct ChromeStyle {
  bool enabled = false;
  float scale = 1.0f;
  ChromeShape panelShape = ChromeShape::Rect;
  ChromeShape controlShape = ChromeShape::Rect;
  float panelRadius = 0.0f;
  float controlRadius = 0.0f;
  float outline = 1.0f;        // border width
  float gradient = 0.0f;       // top this much lighter, bottom this much darker (0..0.4)
  float sheen = 0.0f;          // a bright band a third of the way down (metal, gloss)
  float rim = 0.0f;            // strength of the light along the top inside edge (0..1)
  float lip = 0.0f;            // a hard darker step under each control, in px
  float shadowY = 0.0f;        // soft shadow under each panel/control
  float shadowBlur = 0.0f;
  float shadowAlpha = 0.0f;
  ChromeTexture panelTexture = ChromeTexture::None;
  float panelTextureAlpha = 0.0f;
  ChromeTexture controlTexture = ChromeTexture::None;
  float controlTextureAlpha = 0.0f;
  ChromeTexture backgroundTexture = ChromeTexture::None;   // the desk's body behind the panels
  float backgroundTextureAlpha = 0.0f;
  float textureScale = 1.0f;
};

inline ChromeStyle gChrome;

ChromeShape chromeShapeFromToken(const std::string& token, ChromeShape fallback);
ChromeTexture chromeTextureFromToken(const std::string& token);

class Primitives {
 public:
  // Fill a rectangle with solid color (sets SDL_BLENDMODE_BLEND for alpha)
  static void fillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color);

  // Draw 1px rectangle outline (sets SDL_BLENDMODE_BLEND for alpha)
  static void strokeRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color);

  // Draw a beveled panel: solid body fill, 1px outer border, then inset
  // highlight/shadow edges for a raised or sunken 3D effect. The bevel
  // direction is auto-detected: if innerBorder is brighter than body,
  // the panel appears raised; if darker, it appears inset.
  static void drawFramedPanel(SDL_Renderer* renderer, const SDL_Rect& rect,
                              SDL_Color body, SDL_Color border, SDL_Color innerBorder);

  // A skinned face (gChrome.enabled): shadow, lip, outline, rim light,
  // gradient face and texture, in the shape its size gives it. `raised`
  // false is a well (inverted gradient, no lip or shadow). Records the
  // surface and the pressable exactly as the flat panels do.
  static void drawChrome(SDL_Renderer* renderer, const SDL_Rect& rect,
                         SDL_Color fill, SDL_Color border, bool raised, ChromeRole role = ChromeRole::Auto);
  // The skin's shape for this rect, filled flat or outlined: the pressed
  // wash and the open/release rings follow the control's own outline.
  static void fillChromeShape(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color,
                              ChromeRole role = ChromeRole::Auto);
  static void strokeChromeShape(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color, int width,
                                ChromeRole role = ChromeRole::Auto, int inset = 0);
  // Paint only `area` of the parent's shape, preserving its corner geometry
  // and the caller's clip. Insets shrink that geometry, not a fresh shape.
  static void fillChromeSlice(SDL_Renderer* renderer, const SDL_Rect& parent, const SDL_Rect& area,
                              SDL_Color color, int inset = 0, ChromeRole role = ChromeRole::Auto);
  // Textures are made per renderer; call when a renderer goes away.
  static void forgetChromeRenderer(SDL_Renderer* renderer);

  // Draw a decorative speaker grille pattern: evenly-spaced horizontal bars.
  // Each bar is 3px tall with 4px gaps (7px pitch). Used for cosmetic detail
  // in the deck output display area.
  static void drawSpeakerGrille(SDL_Renderer* renderer, int x, int y,
                                int width, int bars, SDL_Color color);

  // WHAT IS BEHIND A LABEL. While one renderer is being tracked, every fill
  // it receives on the window itself (not on a texture target) is remembered
  // for the frame, translucent ones composited over what they covered. The
  // text path asks surfaceUnder() for the colour beneath a label so it can
  // check the label's ink against the fill actually painted there rather than
  // the one the call site assumed. beginSurfaceFrame() starts a frame and
  // resets the record; nullptr stops tracking.
  static void beginSurfaceFrame(SDL_Renderer* renderer, SDL_Color clearColor);
  static bool surfaceUnder(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color* out);

  // WHICH CONTROL IS BEING PRESSED. Every bevelled control on the tracked
  // renderer passes through notePressable(); the last one drawn under the held
  // pointer is the one on top, so it is the one being pressed. A panel larger
  // than a control that is drawn over the point afterwards (a modal, say)
  // covers whatever was under it. The desk asks pressedTarget() at the end of
  // the frame and draws the pressed look over it, so every button in every
  // theme answers the moment it is pressed, without each call site knowing.
  static void setPress(bool held, int x, int y, int maxW, int maxH);
  static void notePressable(SDL_Renderer* renderer, const SDL_Rect& rect);
  static bool pressedTarget(SDL_Rect* out);
};

}  // namespace deckboy::render
