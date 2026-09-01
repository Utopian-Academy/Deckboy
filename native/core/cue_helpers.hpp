// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// cue_helpers.hpp — Utility functions for cue kind classification and behavior.
//
// These helpers answer questions about a cue's type and end-of-playback
// behavior. They are used throughout the codebase:
//   - MediaEngine:   decides decode pipeline based on cue kind
//   - Transport:     decides what happens when a cue finishes playing
//   - Inspector UI:  shows source-specific editing fields
//   - Serialization: resolves "Inherit" end actions for actual behavior
//
// All functions are inline (header-only) because they are small and
// frequently called in hot paths (render loop, transport tick).
// ============================================================================

#pragma once

#include <string>
#include "types.hpp"
#include "utils.hpp"

// Is this a live source capture cue kind? (WindowSource, Camera, Syphon)
// These cue kinds use the capture backend (platform/capture_backend.*) rather
// than the ffmpeg decode pipeline, and have a "source://" path format.
inline bool isSourceCueKind(CueKind kind) {
  return kind == CueKind::WindowSource
    || kind == CueKind::Camera
    || kind == CueKind::Syphon;
}

// Does the inspector show this cue the plain live-picture layout?
//
// SEPARATE FROM isSourceCueKind, which is about the capture BACKEND -- the
// source:// path format and the ffmpeg-free capture pipeline. A DeckLink input
// is a live picture and is not that: it arrives through the SDK on a
// decklink:// path, so widening the backend predicate to cover it would route
// it into a pipeline it does not use. It is the same question only for the
// inspector, which cares what a cue LOOKS like, not where the pixels came from.
//
// Streams are live pictures too and are deliberately absent: they are addressed
// by URL, so they have a layout of their own with a field for it.
inline bool cueUsesLivePictureInspector(CueKind kind) {
  return isSourceCueKind(kind) || kind == CueKind::DeckLinkSource;
}

// Return a human-readable default source reference for a given capture kind.
// Used when creating a new source cue to populate the initial path field.
inline std::string defaultSourceRefForKind(CueKind kind) {
  switch (kind) {
    case CueKind::WindowSource: return "active-window";   // capture the focused window
    case CueKind::Camera:       return "default-camera";   // first available camera
    case CueKind::Syphon:       return "default-bus";      // default Syphon/Spout bus
    default:                    return "source";
  }
}

// Extract the source reference name from a source cue's path.
// Source cue paths follow the format "source://<kind>/<reference>".
// Returns the <reference> portion, or "" if the cue is not a source kind
// or the path doesn't match the expected format.
inline std::string sourceCueRefFromCue(const Cue& cue) {
  if (!isSourceCueKind(cue.kind)) {
    return "";
  }
  std::string path = deckboy::core::utils::trim(cue.path);
  if (path.rfind("source://", 0) == 0) {
    size_t kindStart = std::string("source://").size();
    size_t slash = path.find('/', kindStart);
    if (slash != std::string::npos && slash + 1 < path.size()) {
      return deckboy::core::utils::trim(path.substr(slash + 1));
    }
  }
  return "";
}

// Name the DeckLink card a capture cue is watching, for display.
//
// The path is "decklink://<device index>" and the cue's name is the device
// name it was added with, so the pair reads as "DeckLink Duo (2) [1]". Falls
// back to the raw path if either half is missing, which is better than an
// empty row.
inline std::string deckLinkCueDeviceLabel(const Cue& cue) {
  std::string path = deckboy::core::utils::trim(cue.path);
  const std::string prefix = "decklink://";
  if (path.rfind(prefix, 0) != 0) {
    return path.empty() ? "(no device)" : path;
  }
  std::string index = deckboy::core::utils::trim(path.substr(prefix.size()));
  std::string name = deckboy::core::utils::trim(cue.name);
  if (name.empty()) {
    return index.empty() ? "(no device)" : ("device " + index);
  }
  return index.empty() ? name : (name + " [" + index + "]");
}

// Can this cue be trimmed to an in and an out point?
//
// File-backed media with a real duration: a clip and a sound file. The engine
// applies the trim to BOTH -- loadCue() returns early only for Browser, Lower
// Third and Composite -- so an audio cue reaches the same clamp, the same
// duration_ and the same seek to the in-point as a clip does. The adjusters
// tested for Video alone, which made the trim on an audio cue reachable by
// editing the show file and by no other means.
//
// Deliberately not the live kinds: a stream, an NDI feed or a capture input has
// no duration to trim against, and the clamp would let an operator dial an
// in-point of up to an hour into something that has none.
inline bool cueSupportsTrimPoints(CueKind kind) {
  return kind == CueKind::Video || kind == CueKind::Audio;
}

// CURATED GLYPH SETS.
//
// Once text mode could draw any character through a font, a "set" stopped
// needing artwork and became a STRING -- so instead of the one hand-drawn marks
// set there can be as many as are worth having. Picking one fills the custom
// glyph field, which stays editable: these are starting points, not modes.
//
// Ordered LIGHT TO HEAVY within each set, because that is the order text mode
// maps brightness onto. A set that starts with a space keeps its darkest cells
// empty, which is usually what makes a picture read.
struct GlyphPreset {
  const char* name;
  const char* glyphs;
};

inline const std::vector<GlyphPreset>& glyphPresets() {
  static const std::vector<GlyphPreset> sets = {
  {"dots", "\x20\xC2\xB7\xE2\x88\x99\xE2\x80\xA2\xE2\x97\x8F\xE2\x97\x8B\xE2\x97\x8C\xE2\x97\x8D\xE2\x97\x8E\xE2\x97\x89\xE2\xAC\xA4"},
  {"stars", "\x20\xCB\x99\xC2\xB7\xE2\x8B\x86\xE2\x9C\xA6\xE2\x9C\xA7\xE2\x98\x85\xE2\x98\x86\xE2\x9C\xA9\xE2\x9C\xAA\xE2\x9C\xAB\xE2\x9C\xAC\xE2\x9C\xAD\xE2\x9C\xAE\xE2\x9C\xAF\xE2\x9C\xB0"},
  {"sparkles", "\x20\xC2\xB7\xCB\x99\xE2\x9C\xA2\xE2\x9C\xA3\xE2\x9C\xA4\xE2\x9C\xA5\xE2\x9C\xA6\xE2\x9C\xA7\xE2\x9D\x8B\xE2\x9D\x8A\xE2\x9D\x89\xE2\x9D\x88\xE2\x9D\x87\xE2\x9D\x86"},
  {"music", "\x20\xC2\xB7\xCB\x99\xE2\x99\xA9\xE2\x99\xAA\xE2\x99\xAB\xE2\x99\xAC\xE2\x99\xAD\xE2\x99\xAE\xE2\x99\xAF"},
  {"hearts", "\x20\xC2\xB7\xE2\x99\xA1\xE2\x99\xA5\xE2\x9D\xA4\xE2\x9D\xA5\xE2\x9D\xA6\xE2\x9D\xA7"},
  {"flowers", "\x20\xC2\xB7\xE2\x9D\x80\xE2\x9C\xBF\xE2\x9D\x81\xE2\x9C\xBE\xE2\x9D\x83\xE2\x9D\x8B\xE2\x9C\xBD\xE2\x9C\xBC\xE2\x9C\xBB\xE2\x9C\xBA"},
  {"arrows", "\x20\xC2\xB7\xE2\x86\x90\xE2\x86\x91\xE2\x86\x92\xE2\x86\x93\xE2\x86\x94\xE2\x86\x95\xE2\x86\x96\xE2\x86\x97\xE2\x86\x98\xE2\x86\x99\xE2\x87\x90\xE2\x87\x91\xE2\x87\x92\xE2\x87\x93"},
  {"geometry", "\x20\xC2\xB7\xE2\x96\xAA\xE2\x96\xAB\xE2\x97\xA6\xE2\x97\x98\xE2\x97\x99\xE2\x96\xA1\xE2\x96\xA0\xE2\x97\x87\xE2\x97\x86\xE2\x96\xB3\xE2\x96\xB2\xE2\x96\xBD\xE2\x96\xBC"},
  {"blocks", "\x20\xE2\x96\x91\xE2\x96\x92\xE2\x96\x93\xE2\x96\x88\xE2\x96\x81\xE2\x96\x82\xE2\x96\x83\xE2\x96\x84\xE2\x96\x85\xE2\x96\x86\xE2\x96\x87"},
  {"box drawing", "\x20\xE2\x94\x80\xE2\x94\x82\xE2\x94\x8C\xE2\x94\x90\xE2\x94\x94\xE2\x94\x98\xE2\x94\x9C\xE2\x94\xA4\xE2\x94\xAC\xE2\x94\xB4\xE2\x94\xBC\xE2\x95\x94\xE2\x95\x97\xE2\x95\x9A\xE2\x95\x9D\xE2\x95\x90\xE2\x95\x91"},
  {"circles", "\x20\xC2\xB7\xCB\x99\xE2\x97\x9C\xE2\x97\x9D\xE2\x97\x9E\xE2\x97\x9F\xE2\x97\xA0\xE2\x97\xA1\xE2\x97\x8B\xE2\x97\x8D\xE2\x97\x90\xE2\x97\x91\xE2\x97\x92\xE2\x97\x93\xE2\x97\x8F"},
  {"weather", "\x20\xC2\xB7\xE2\x98\x80\xE2\x98\x81\xE2\x98\x82\xE2\x98\x83\xE2\x98\x84\xE2\x98\x85\xE2\x98\xBE\xE2\x98\xBD\xE2\x9D\x84\xE2\x9D\x85\xE2\x9D\x86"},
  {"zodiac", "\x20\xC2\xB7\xE2\x99\x88\xE2\x99\x89\xE2\x99\x8A\xE2\x99\x8B\xE2\x99\x8C\xE2\x99\x8D\xE2\x99\x8E\xE2\x99\x8F\xE2\x99\x90\xE2\x99\x91\xE2\x99\x92\xE2\x99\x93"},
  {"chess & cards", "\x20\xC2\xB7\xE2\x99\x9F\xE2\x99\x9C\xE2\x99\x9E\xE2\x99\x9D\xE2\x99\x9B\xE2\x99\x9A\xE2\x99\xA0\xE2\x99\xA3\xE2\x99\xA5\xE2\x99\xA6"},
  {"runes", "\x20\xC2\xB7\xE1\x9A\xA0\xE1\x9A\xA2\xE1\x9A\xA6\xE1\x9A\xA8\xE1\x9A\xB1\xE1\x9A\xB2\xE1\x9A\xB7\xE1\x9A\xB9\xE1\x9A\xBA\xE1\x9A\xBE\xE1\x9B\x81\xE1\x9B\x83\xE1\x9B\x87\xE1\x9B\x88"},
  {"greek", "\x20\xC2\xB7\xCE\xB1\xCE\xB2\xCE\xB3\xCE\xB4\xCE\xB5\xCE\xB6\xCE\xB7\xCE\xB8\xCE\xB9\xCE\xBA\xCE\xBB\xCE\xBC\xCE\xBD\xCE\xBE\xCF\x80\xCF\x81\xCF\x83\xCF\x84\xCF\x85\xCF\x86\xCF\x87\xCF\x88\xCF\x89"},
  {"braille", "\x20\xE2\xA0\x81\xE2\xA0\x83\xE2\xA0\x87\xE2\xA0\x8F\xE2\xA0\x9F\xE2\xA0\xBF\xE2\xA1\xBF\xE2\xA3\xBF"},
  {"currency", "\x20\xC2\xB7\xC2\xA2\x24\xE2\x82\xAC\xC2\xA3\xC2\xA5\xE2\x82\xA9\xE2\x82\xAA\xE2\x82\xAB\xE2\x82\xB4\xE2\x82\xBD\xE2\x82\xBF"},
  {"maths", "\x20\xC2\xB7\x2B\xE2\x88\x92\xC3\x97\xC3\xB7\xC2\xB1\xE2\x88\x93\xE2\x88\x9E\xE2\x89\x88\xE2\x89\xA0\xE2\x89\xA4\xE2\x89\xA5\xE2\x88\x91\xE2\x88\x8F\xE2\x88\xAB\xE2\x88\x9A\xE2\x88\x82\xE2\x88\x87"},
  {"dice", "\x20\xC2\xB7\xE2\x9A\x80\xE2\x9A\x81\xE2\x9A\x82\xE2\x9A\x83\xE2\x9A\x84\xE2\x9A\x85"},
  {"faces", "\x20\xC2\xB7\xE2\x98\xBA\xE2\x98\xBB\xE2\x98\xB9\xE2\x98\xA0\xE2\x9C\x8C\xE2\x9C\x8B"},
  {"emoji faces", "\x20\xF0\x9F\x98\x90\xF0\x9F\x99\x82\xF0\x9F\x98\x80\xF0\x9F\x98\x83\xF0\x9F\x98\x84\xF0\x9F\x98\x81\xF0\x9F\x98\x86\xF0\x9F\x98\x82\xF0\x9F\xA4\xA3"},
  {"emoji nature", "\x20\xF0\x9F\x8C\xB1\xF0\x9F\x8C\xBF\xF0\x9F\x8D\x80\xF0\x9F\x8C\xB3\xF0\x9F\x8C\xB2\xF0\x9F\x8C\xB8\xF0\x9F\x8C\xBA\xF0\x9F\x8C\xBB\xF0\x9F\x8C\x9E\xF0\x9F\x8C\x88"},
  {"emoji night", "\x20\xF0\x9F\x8C\x91\xF0\x9F\x8C\x92\xF0\x9F\x8C\x93\xF0\x9F\x8C\x94\xF0\x9F\x8C\x95\xE2\xAD\x90\xF0\x9F\x8C\x9F\xE2\x9C\xA8\xF0\x9F\x92\xAB"},
  {"emoji elements", "\x20\xE2\x9D\x84\xF0\x9F\xA7\x8A\xF0\x9F\x92\xA7\xF0\x9F\x8C\x8A\xF0\x9F\x94\xA5\xF0\x9F\x92\xA5\xE2\x98\x84"},
  {"emoji show", "\x20\xF0\x9F\x8E\xAC\xF0\x9F\x93\xBA\xF0\x9F\x8E\x9E\xF0\x9F\x8E\xA5\xF0\x9F\x8E\xA4\xF0\x9F\x8E\xA7\xF0\x9F\x8E\xB5\xF0\x9F\x8E\xB6\xF0\x9F\x8E\xB9\xF0\x9F\xA5\x81"},
  {"emoji party", "\x20\xF0\x9F\x8E\x88\xF0\x9F\x8E\x89\xF0\x9F\x8E\x8A\xF0\x9F\xA5\x82\xF0\x9F\x8D\xBA\xF0\x9F\x8D\xBB\xF0\x9F\x8E\x82\xF0\x9F\x8D\xB0"},
  {"emoji hearts", "\x20\xF0\x9F\x96\xA4\xF0\x9F\x92\x9C\xF0\x9F\x92\x99\xF0\x9F\x92\x9A\xF0\x9F\x92\x9B\xF0\x9F\xA7\xA1\xE2\x9D\xA4\xF0\x9F\x92\x96\xF0\x9F\x92\x97\xF0\x9F\x92\x93"},
  {"emoji creatures", "\x20\xF0\x9F\x90\x8C\xF0\x9F\x90\x9B\xF0\x9F\xA6\x8B\xF0\x9F\x90\x9D\xF0\x9F\x90\x9E\xF0\x9F\x90\x9C\xF0\x9F\xA6\x97\xF0\x9F\x95\xB7\xF0\x9F\x90\x99\xF0\x9F\xA6\x91"},
  {"emoji space", "\x20\xF0\x9F\x9B\xB0\xF0\x9F\x9A\x80\xF0\x9F\x9B\xB8\xF0\x9F\x8C\x8D\xF0\x9F\x8C\x8E\xF0\x9F\x8C\x8F\xF0\x9F\xAA\x90\xE2\x98\x84\xF0\x9F\x8C\x8C"},
  {"emoji weather", "\x20\xE2\x98\x81\xF0\x9F\x8C\xA4\xE2\x9B\x85\xF0\x9F\x8C\xA5\xF0\x9F\x8C\xA6\xF0\x9F\x8C\xA7\xE2\x9B\x88\xF0\x9F\x8C\xA9\xF0\x9F\x8C\xA8"},
  {"emoji fruit", "\x20\xF0\x9F\x8D\x87\xF0\x9F\x8D\x89\xF0\x9F\x8D\x8A\xF0\x9F\x8D\x8B\xF0\x9F\x8D\x8C\xF0\x9F\x8D\x8D\xF0\x9F\xA5\xAD\xF0\x9F\x8D\x8E\xF0\x9F\x8D\x91\xF0\x9F\x8D\x92"},
  };
  return sets;
}

// Resolve the actual end action for a cue, handling the "Inherit" case.
// When a cue's endAction is Inherit, the behavior is derived from its
// boolean flags: loop → Loop, pauseOnLastFrame → PauseOnLast, else → AutoNext.
// Used by MediaEngine and transport to decide post-playback behavior.
inline CueEndAction resolvedCueEndAction(const Cue& cue) {
  if (cue.endAction != CueEndAction::Inherit) {
    return cue.endAction;  // explicit override — use as-is
  }
  // Inherit: derive from legacy boolean flags for backwards compatibility
  if (cue.loop) {
    return CueEndAction::Loop;
  }
  if (cue.pauseOnLastFrame) {
    return CueEndAction::PauseOnLast;
  }
  return CueEndAction::AutoNext;  // default: advance to next cue
}

// Quick check: will this cue auto-advance to the next cue when it finishes?
// Used by the transition system to decide whether to prepare the next cue.
inline bool cueAdvancesWhenFinished(const Cue& cue) {
  return resolvedCueEndAction(cue) == CueEndAction::AutoNext;
}
