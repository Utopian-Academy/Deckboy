// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// utils.hpp — Shared utility functions used across the entire codebase.
//
// This is a grab-bag of pure functions with no side effects or state:
//   - String manipulation (trim, split, case conversion, join)
//   - Timecode formatting and parsing (SMPTE HH:MM:SS:FF)
//   - Enum-to-string conversions for CueKind, CueEndAction, TransportState,
//     TransitionStyle (used by UI rendering, serialization, and OSC commands)
//   - Color conversions (hex parsing, RGBA unpacking, color tag palette)
//   - SDL_Rect geometry helpers (inset, point-in-rect hit testing)
//   - Tab-delimited field parsing (safe accessors for project load)
//   - JSON string escaping (for OSC/Companion feedback payloads)
//   - Easing functions (for UI animations)
//
// Implementation: utils.cpp
// ============================================================================

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL.h>

#include "types.hpp"

namespace deckboy::core::utils {

// -- String utilities ---------------------------------------------------------
// Used by project load/save (splitByChar with '\t'), OSC command parsing
// (splitWhitespace), and general UI text processing throughout the app.

std::string trim(const std::string& value);                              // strip leading/trailing whitespace
std::vector<std::string> splitLines(const std::string& text);            // split on '\n' (handles \r\n)
std::vector<std::string> splitByChar(const std::string& text, char separator); // split on arbitrary delimiter
std::vector<std::string> splitWhitespace(const std::string& text);       // split on runs of whitespace
std::string toUpper(std::string value);                                  // ASCII uppercase (in-place copy)
std::string toLower(std::string value);                                  // ASCII lowercase (in-place copy)
std::string joinParts(const std::vector<std::string>& parts, size_t startIndex); // join parts[startIndex..] with spaces

// -- Timecode & time formatting -----------------------------------------------
// Used by the control bar, cue list, timecode overlay, and OSC feedback.
// formatSeconds:  "1:23" or "0:05" (minutes:seconds, no frames)
// formatTimecode: "00:01:23:15" (SMPTE HH:MM:SS:FF at the given fps)
// parseTimecodeSeconds: inverse of formatTimecode — returns seconds or nullopt

std::string formatSeconds(double seconds);
std::string formatTimecode(double seconds, double fps);
std::optional<double> parseTimecodeSeconds(std::string value, double fps);

// -- Enum-to-string conversions -----------------------------------------------
// Each enum has a "token" form (lowercase, for serialization/OSC) and a
// "label" form (human-readable, for UI display). parse* functions go from
// token back to enum. These are used by save/load, OSC, and settings UI.

std::string cueKindLabel(::CueKind kind);          // e.g. "Video", "Browser", "SRT Stream"
std::string cueKindToken(::CueKind kind);          // e.g. "video", "browser", "srt_stream"

std::string cueEndActionToken(::CueEndAction action);    // e.g. "stop", "loop", "auto_next"
std::string cueEndActionLabel(::CueEndAction action);    // e.g. "Stop", "Loop", "Auto Next"
::CueEndAction parseCueEndAction(const std::string& token);

std::string transportLabel(::TransportState state);      // "Stopped", "Paused", "Playing"

std::string transitionStyleToken(::TransitionStyle style);   // "cut", "crossfade", "dipblack"
::TransitionStyle parseTransitionStyleToken(std::string token);

// -- Easing functions ---------------------------------------------------------
// Used by UI animation system (panel slides, opacity fades, toast popups).
// easeOutCubic: decelerating cubic curve, value in [0,1] → [0,1]

double easeOutCubic(double value);

// -- Color utilities ----------------------------------------------------------
// Used by theme rendering, chroma key UI, color tag display, and OSC feedback.
// Colors are stored as SDL_Color (RGBA bytes) or packed uint32 (0xRRGGBBAAu).

SDL_Color parseColor(std::string_view input);              // parse "#RRGGBB" or "#RRGGBBAA" hex string
std::string colorToHex(SDL_Color color);                   // convert to "#RRGGBB" hex string
SDL_Color colorFromRgba(std::uint32_t rgba);               // unpack 0xRRGGBBAAu → SDL_Color
SDL_Color colorTagToSdl(const std::string& tag, std::uint8_t alpha = 255); // "red"/"blue"/etc. → SDL_Color
std::string nextColorTag(const std::string& current);      // cycle to next color tag in palette
std::uint8_t alpha(std::uint32_t rgba);                    // extract alpha byte from packed RGBA
std::uint8_t blue(std::uint32_t rgba);                     // extract blue byte from packed RGBA
std::uint8_t green(std::uint32_t rgba);                    // extract green byte from packed RGBA
std::uint8_t red(std::uint32_t rgba);                      // extract red byte from packed RGBA

// -- Rectangle utilities ------------------------------------------------------
// Used by UI layout and rendering for padding and margins.

SDL_Rect insetRect(const SDL_Rect& rect, int amount);     // shrink rect by `amount` on all sides

// -- Geometry -----------------------------------------------------------------
// Hit-testing for mouse clicks on UI elements (buttons, cue rows, etc.).

bool pointInRect(int x, int y, const SDL_Rect& rect);     // true if (x,y) is inside rect

// -- Field parsing helpers (project load) ------------------------------------
// Safe accessors for tab-delimited fields from .deckboy project files.
// Return the fallback value if the index is out of range or the field is
// empty/malformed. Used exclusively by loadProject() in app_project_state.ipp.

std::string safeString(const std::vector<std::string>& fields, size_t index);
int safeInt(const std::vector<std::string>& fields, size_t index, int fallback = 0);
std::uintmax_t safeSize(const std::vector<std::string>& fields, size_t index, std::uintmax_t fallback = 0);
bool safeBool(const std::vector<std::string>& fields, size_t index, bool fallback = false);

// -- JSON escaping ------------------------------------------------------------
// Escapes special characters in a string for embedding in JSON payloads.
// Used by OSC/Companion feedback (app_network.ipp) to build JSON responses.

std::string escapeJson(const std::string& value);

}  // namespace deckboy::core::utils
