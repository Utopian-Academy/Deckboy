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
