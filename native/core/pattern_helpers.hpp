// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// pattern_helpers.hpp — Utility functions for procedural pattern cues.
//
// Pattern cues (CueKind::Pattern) are procedurally generated test patterns
// and animated backgrounds. They don't use ffmpeg — the pixels are generated
// directly in the render loop. Each pattern type is identified by a string
// ID stored in the cue's path field as "pattern://<type-id>".
//
// Available pattern types:
//   Static:    smpte-bars, crosshatch, checkerboard, gradient,
//              full-white, full-black, full-red, full-green, full-blue
//   Animated:  pocket-test, pocket-day, pocket-sunset, pocket-night, pocket-storm,
//              terrarium (native living-ecosystem sim, v0.78.4)
//              (plus "-motion" variants of the static patterns)
//
// This file handles:
//   - Normalizing type IDs (aliases, old names, underscore→hyphen)
//   - Querying whether a pattern supports motion animation
//   - Detecting whether a pattern is inherently animated
//
// Used by: MediaEngine (to decide decode vs. generate), inspector UI
// (to show motion toggle), cue import (to normalize the path).
// ============================================================================

#pragma once

#include <algorithm>
#include <string>
#include "utils.hpp"

// Check if a string ends with the given suffix. Used to detect "-motion"
// suffixes on pattern type IDs.
inline bool endsWith(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Normalize a pattern type ID to its canonical form.
// Handles:
//   - Stripping the "pattern://" URL prefix
//   - Lowercasing and trimming whitespace
//   - Replacing underscores with hyphens (legacy format)
//   - Mapping aliases to canonical names (e.g. "colorbars" → "smpte-bars")
//   - Normalizing "-animated" suffix to "-motion" (standardized naming)
inline std::string normalizePatternTypeId(std::string value) {
  value = deckboy::core::utils::toLower(deckboy::core::utils::trim(std::move(value)));
  // Strip URL prefix if present (cue path stores "pattern://smpte-bars")
  if (value.rfind("pattern://", 0) == 0) {
    value = value.substr(10);
  }
  // Normalize separators: underscores → hyphens
  std::replace(value.begin(), value.end(), '_', '-');
  // Alias mapping: multiple names for the same pattern
  if (value == "colourbars" || value == "colorbars" || value == "smpte75") {
    value = "smpte-bars";            // standard SMPTE color bars
  } else if (value == "kawaii" || value == "kawaii-pocket") {
    value = "pocket-test";           // retro pixel art test pattern
  } else if (value == "pocket-daytime" || value == "pocket-sunny") {
    value = "pocket-day";            // daytime sky scene
  } else if (value == "pocket-dusk" || value == "pocket-golden") {
    value = "pocket-sunset";         // sunset scene
  } else if (value == "pocket-moon" || value == "pocket-evening") {
    value = "pocket-night";          // night scene
  } else if (value == "pocket-rain" || value == "pocket-tempest") {
    value = "pocket-storm";          // storm/rain scene
  } else if (value == "smpte-bars-animated") {
    value = "smpte-bars-motion";     // standardize "-animated" → "-motion"
  } else if (value == "crosshatch-animated") {
    value = "crosshatch-motion";
  } else if (value == "checkerboard-animated") {
    value = "checkerboard-motion";
  } else if (value == "full-white-animated") {
    value = "full-white-motion";
  } else if (value == "full-black-animated") {
    value = "full-black-motion";
  } else if (value == "full-red-animated") {
    value = "full-red-motion";
  } else if (value == "full-green-animated") {
    value = "full-green-motion";
  } else if (value == "full-blue-animated") {
    value = "full-blue-motion";
  }
  return value;
}

// Strip the "-motion" suffix from a pattern type ID to get the base pattern name.
// e.g. "smpte-bars-motion" → "smpte-bars"
// Normalizes the ID first (handles aliases and formatting).
inline std::string stripPatternMotionSuffix(std::string typeId) {
  typeId = normalizePatternTypeId(std::move(typeId));
  if (endsWith(typeId, "-motion")) {
    typeId = typeId.substr(0, typeId.size() - 7);
  }
  return typeId;
}

// Check if a pattern type can have a "-motion" animated variant.
// Only certain technical test patterns support the motion toggle.
// Pocket scenes are always animated and don't use the motion suffix.
inline bool patternTypeSupportsMotion(const std::string& typeId) {
  std::string base = stripPatternMotionSuffix(typeId);
  return base == "smpte-bars" || base == "crosshatch" || base == "checkerboard" ||
         base == "full-white" || base == "full-black" || base == "full-red" ||
         base == "full-green" || base == "full-blue";
}

// Check if a pattern type is inherently animated (changes over time).
// Returns true for:
//   - All pocket-* scenes (always animated pixel art)
//   - Any pattern with the "-motion" suffix
// Used by MediaEngine to decide whether to start a frame timer for the pattern.
inline bool patternTypeIsAnimated(const std::string& typeId) {
  std::string normalized = normalizePatternTypeId(typeId);
  return normalized.rfind("pocket-", 0) == 0 ||       // pocket scenes: always animated
         normalized == "terrarium" ||                  // living ecosystem: always animated
         normalized.find("kawaii") != std::string::npos || // legacy alias for pocket
         endsWith(normalized, "-motion");               // motion variant of static pattern
}
