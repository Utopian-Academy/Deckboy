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
