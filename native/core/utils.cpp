// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// utils.cpp — Implementation of shared utility functions (see utils.hpp).
//
// All functions here are stateless and side-effect-free. They are organized
// into groups matching the header declarations:
//   1. String utilities (trim, split, case, join)
//   2. Timecode formatting/parsing (SMPTE HH:MM:SS:FF)
//   3. Enum-to-string conversions (CueKind, CueEndAction, etc.)
//   4. Easing functions (UI animation curves)
//   5. Color utilities (hex parsing, RGBA extraction, color tag palette)
//   6. Geometry helpers (rect inset, point-in-rect)
//   7. Safe field parsers (for project file deserialization)
//   8. JSON escaping (for OSC/Companion feedback)
// ============================================================================

#include "utils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace deckboy::core::utils {
using ::CueKind;
using ::CueEndAction;
using ::TransportState;
using ::TransitionStyle;

// ============================================================================
// 1. String utilities
// ============================================================================

// Strip leading and trailing whitespace (spaces, tabs, CR, LF).
// Used by OSC command parsing, timecode parsing, and project field cleanup.
std::string trim(const std::string& value) {
  auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";  // string is entirely whitespace
  }
  auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

// Split text into lines on '\n', stripping '\r' for Windows line endings.
// Used by ffprobe output parsing (readAllText returns multi-line text).
std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::stringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();  // handle CRLF (Windows-generated ffprobe output)
    }
    lines.push_back(line);
  }
  return lines;
}

// Split text on an arbitrary single-character delimiter.
// The primary use is splitting tab-delimited fields from .deckboy project files
// (splitByChar(line, '\t')). Always returns at least one element.
std::vector<std::string> splitByChar(const std::string& text, char separator) {
  std::vector<std::string> parts;
  std::string current;
  for (char ch : text) {
    if (ch == separator) {
      parts.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  parts.push_back(current);  // don't forget the last segment
  return parts;
}

// ============================================================================
// 2. Timecode & time formatting
// ============================================================================

// Format seconds as "MM:SS.T" (minutes, seconds, tenths). Used by the cue list
// duration column and the control bar elapsed/remaining display.
// Returns "00:00.0" for invalid/negative values.
std::string formatSeconds(double seconds) {
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    return "00:00.0";
  }
  int wholeMinutes = static_cast<int>(seconds / 60.0);
  double remaining = seconds - wholeMinutes * 60.0;
  // ROUND TO THE TENTH WE ARE ABOUT TO PRINT, THEN CARRY.
  //
  // %04.1f rounds after the split, so anything in the last twentieth of a
  // second printed as :60.0 -- an out-point of 899.983s read "14:60.0" in the
  // inspector instead of "15:00.0". A minute has sixty seconds in it and the
  // readout has to agree.
  double tenths = std::round(remaining * 10.0);
  if (tenths >= 600.0) {
    tenths -= 600.0;
    wholeMinutes += 1;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%04.1f", wholeMinutes, tenths / 10.0);
  return buf;
}

// Format seconds as SMPTE timecode "HH:MM:SS:FF" at the given frame rate.
// The 0.0001 epsilon prevents rounding errors from dropping a frame.
// Used by the timecode overlay, control bar, and OSC feedback.
std::string formatTimecode(double seconds, double fps) {
  double safeFps = std::isfinite(fps) && fps > 1.0 ? fps : 30.0;  // fallback to 30fps
  double clamped = std::max(0.0, std::isfinite(seconds) ? seconds : 0.0);
  int totalFrames = static_cast<int>(std::floor(clamped * safeFps + 0.0001));
  int fpsInt = std::max(1, static_cast<int>(std::round(safeFps)));
  int frame = totalFrames % fpsInt;        // frame within current second
  int totalSeconds = totalFrames / fpsInt;
  int secs = totalSeconds % 60;
  int mins = (totalSeconds / 60) % 60;
  int hours = totalSeconds / 3600;
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d", hours, mins, secs, frame);
  return buf;
}

// Parse a timecode string back to seconds. Accepts multiple formats:
//   - Plain number:    "123.5"           → 123.5 seconds
//   - MM:SS:           "02:30"           → 150.0 seconds
//   - HH:MM:SS:        "01:02:30"        → 3750.0 seconds
//   - HH:MM:SS:FF:     "01:02:30:15"     → 3750.5 seconds (at 30fps)
// Returns nullopt if the string can't be parsed.
// Used by the inline timecode editor and OSC time-jump commands.
std::optional<double> parseTimecodeSeconds(std::string value, double fps) {
  value = trim(value);
  if (value.empty()) {
    return std::nullopt;
  }

  // No colons → treat as a plain seconds value
  if (value.find(':') == std::string::npos) {
    try {
      return std::max(0.0, std::stod(value));
    } catch (...) {
      return std::nullopt;
    }
  }

  // Split on colons and validate part count (2–4 parts supported)
  auto parts = splitByChar(value, ':');
  if (parts.size() < 2 || parts.size() > 4) {
    return std::nullopt;
  }

  // Helper: parse a single integer part, returning nullopt on failure
  auto parseIntPart = [&](const std::string& part) -> std::optional<int> {
    try {
      return std::max(0, std::stoi(part));
    } catch (...) {
      return std::nullopt;
    }
  };

  double safeFps = std::isfinite(fps) && fps > 1.0 ? fps : 30.0;
  int hours = 0;
  int mins = 0;
  int secs = 0;
  int frames = 0;

  // Parse based on the number of colon-separated parts
  if (parts.size() == 4) {           // HH:MM:SS:FF
    auto h = parseIntPart(parts[0]);
    auto m = parseIntPart(parts[1]);
    auto s = parseIntPart(parts[2]);
    auto f = parseIntPart(parts[3]);
    if (!h || !m || !s || !f) {
      return std::nullopt;
    }
    hours = *h;
    mins = *m;
    secs = *s;
    frames = *f;
  } else if (parts.size() == 3) {   // HH:MM:SS (no frames)
    auto h = parseIntPart(parts[0]);
    auto m = parseIntPart(parts[1]);
    auto s = parseIntPart(parts[2]);
    if (!h || !m || !s) {
      return std::nullopt;
    }
    hours = *h;
    mins = *m;
    secs = *s;
  } else {                            // MM:SS (no hours, no frames)
    auto m = parseIntPart(parts[0]);
    auto s = parseIntPart(parts[1]);
    if (!m || !s) {
      return std::nullopt;
    }
    mins = *m;
    secs = *s;
  }

  // Convert to total seconds, adding frame fraction
  double result = static_cast<double>(hours * 3600 + mins * 60 + secs);
  result += static_cast<double>(frames) / safeFps;
  return std::max(0.0, result);
}

// ============================================================================
// 3. Enum-to-string conversions
// ============================================================================

// What an audio cue draws. Token for the show file, label for the inspector.
//
// No `default` on either switch, for the reason the cue-kind ones say at
// length: an unlisted value should be a compiler warning, not a cue that
// silently claims to be something else.
std::string audioVisualToken(AudioVisual visual) {
  switch (visual) {
    case AudioVisual::Waveform:  return "waveform";
    case AudioVisual::Scope:     return "scope";
    case AudioVisual::Lissajous: return "lissajous";
    case AudioVisual::Spectrum:  return "spectrum";
    case AudioVisual::Level:     return "level";
    case AudioVisual::Cover:     return "cover";
  }
  return "waveform";
}

std::string audioVisualLabel(AudioVisual visual) {
  switch (visual) {
    case AudioVisual::Waveform:  return "Waveform";
    case AudioVisual::Scope:     return "Oscilloscope";
    case AudioVisual::Lissajous: return "Lissajous (L/R phase)";
    case AudioVisual::Spectrum:  return "Spectrum";
    case AudioVisual::Level:     return "Level Meters";
    case AudioVisual::Cover:     return "Name Card";
  }
  return "Waveform";
}

// An unknown token is the DEFAULT, not an error: a show written by a later
// Deckboy with a mode this build has never heard of should open and play with
// the picture audio cues have always had, rather than refusing the cue.
AudioVisual parseAudioVisualToken(const std::string& token) {
  if (token == "scope")     return AudioVisual::Scope;
  if (token == "lissajous") return AudioVisual::Lissajous;
  if (token == "spectrum")  return AudioVisual::Spectrum;
  if (token == "level")     return AudioVisual::Level;
  if (token == "cover")     return AudioVisual::Cover;
  return AudioVisual::Waveform;
}

// Human-readable labels for cue kinds (shown in UI inspector and cue list).
//
// EVERY CueKind must appear here, for the same reason cueKindToken must: a kind
// that falls through is LABELLED "Video", so a DeckLink input reads as a video
// file in the playlist and the inspector, and nothing about it looks wrong
// enough to question.
//
// This function had a SECOND DEFINITION, in main.cpp's anonymous namespace,
// with a different set of cases -- one function below the comment warning that
// cueKindToken had done the same thing. Being anonymous it is not an ODR
// violation and nothing warns: main.cpp and everything included into it simply
// got the other one. Between them the two copies covered every kind and neither
// covered all of them -- main.cpp knew the source kinds and Timer, this one knew
// DeckLink -- so the label a cue showed depended on which file asked. This is
// the only definition now; do not add another.
//
// No `default`: every kind is listed, so adding one to the enum without
// labelling it is a compiler warning rather than a cue that lies about itself.
std::string cueKindLabel(CueKind kind) {
  switch (kind) {
    case CueKind::Video:          return "Video";
    case CueKind::Image:          return "Still";
    case CueKind::Pattern:        return "Pattern";
    case CueKind::Browser:        return "Browser";
    case CueKind::WindowSource:   return "Window Source";
    case CueKind::Camera:         return "Camera Source";
    case CueKind::Syphon:         return "Syphon/Spout Source";
    case CueKind::SrtStream:      return "Stream";
    case CueKind::NdiSource:      return "NDI Source";
    case CueKind::DeckLinkSource: return "DeckLink Input";
    case CueKind::Pip:            return "PIP";
    case CueKind::LowerThird:     return "Lower Third";
    case CueKind::Composite:      return "Composite";
    case CueKind::Audio:          return "Audio";
    case CueKind::Timer:          return "Timer";
    case CueKind::Tone:           return "Tone";
    case CueKind::VideoSynth:     return "Video Synth";
  }
  return "Video";
}

// Machine-readable tokens for cue kinds (used in serialization and OSC commands).
// EVERY CueKind must appear here. A kind that falls through to the default is
// SAVED AS "video" and comes back from disk as a video cue: a tone:// or
// timer:// path handed to the file decoder, which cannot decode it, so the cue
// racks and never plays. That is exactly what happened -- Timer, Tone and
// VideoSynth were all missing, and a saved show could not fire any of them.
//
// This was also DUPLICATED in main.cpp with a different (also incomplete) set
// of cases. Two non-static definitions of one function is an ODR violation and
// the linker silently picks one, so which cases existed depended on the build.
// This is the only definition now; do not add another.
std::string cueKindToken(CueKind kind) {
  switch (kind) {
    case CueKind::Image:        return "image";
    case CueKind::Pattern:      return "pattern";
    case CueKind::Browser:      return "browser";
    case CueKind::WindowSource: return "window_source";
    case CueKind::Camera:       return "camera";
    case CueKind::Syphon:       return "syphon";
    case CueKind::SrtStream:    return "srt_stream";
    case CueKind::NdiSource:    return "ndi_source";
    case CueKind::DeckLinkSource: return "decklink_source";
    case CueKind::Pip:          return "pip";
    case CueKind::LowerThird:   return "lower_third";
    case CueKind::Composite:    return "composite";
    case CueKind::Audio:        return "audio";
    case CueKind::Timer:        return "timer";
    case CueKind::Tone:         return "tone";
    case CueKind::VideoSynth:   return "video_synth";
    case CueKind::Video:
    default:                    return "video";
  }
}

// Machine-readable tokens for cue end actions (serialization/OSC).
std::string cueEndActionToken(CueEndAction a) {
  switch (a) {
    case CueEndAction::Stop:         return "stop";
    case CueEndAction::Loop:         return "loop";
    case CueEndAction::PauseOnLast:  return "hold";
    case CueEndAction::AutoNext:     return "next";
    case CueEndAction::Inherit:
    default:                                return "inherit";
  }
}

// Human-readable labels for cue end actions (UI display).
std::string cueEndActionLabel(CueEndAction a) {
  switch (a) {
    case CueEndAction::Stop:         return "stop";
    case CueEndAction::Loop:         return "loop";
    case CueEndAction::PauseOnLast:  return "hold last";
    case CueEndAction::AutoNext:     return "auto next";
    case CueEndAction::Inherit:
    default:                                return "inherit";
  }
}

// Parse a cue end action token back to enum (for OSC commands and project load).
CueEndAction parseCueEndAction(const std::string& s) {
  if (s == "stop")    return CueEndAction::Stop;
  if (s == "loop")    return CueEndAction::Loop;
  if (s == "hold")    return CueEndAction::PauseOnLast;
  if (s == "next")    return CueEndAction::AutoNext;
  return CueEndAction::Inherit;
}

// Transport state label for UI display and OSC feedback.
std::string transportLabel(TransportState state) {
  switch (state) {
    case TransportState::Playing:
      return "Playing";
    case TransportState::Paused:
      return "Paused";
    case TransportState::Stopped:
    default:
      return "Stopped";
  }
}

// Machine-readable token for transition style (serialization/OSC).
std::string transitionStyleToken(TransitionStyle style) {
  switch (style) {
    case TransitionStyle::DipBlack:
      return "dip";
    case TransitionStyle::Cut:
      return "cut";
    case TransitionStyle::Crossfade:
    default:
      return "crossfade";
  }
}

// Parse a transition style token (case-insensitive) back to enum.
// Accepts multiple aliases: "dip", "dipblack", "dip_black" all map to DipBlack.
TransitionStyle parseTransitionStyleToken(std::string token) {
  token = trim(token);
  std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (token == "CUT") {
    return TransitionStyle::Cut;
  }
  if (token == "DIP" || token == "DIPBLACK" || token == "DIP_BLACK") {
    return TransitionStyle::DipBlack;
  }
  return TransitionStyle::Crossfade;
}

// ============================================================================
// 4. Easing functions
// ============================================================================

// Cubic ease-out: fast start, decelerating to a smooth stop.
// Input/output range: [0, 1]. Formula: 1 - (1-t)^3
// Used by UI animations (panel slides, toast popup, opacity transitions).
double easeOutCubic(double value) {
  double t = std::clamp(value, 0.0, 1.0);
  double inv = 1.0 - t;
  return 1.0 - inv * inv * inv;
}

// ============================================================================
// 5. Color utilities
// ============================================================================

// Parse a "#RRGGBB" hex color string to SDL_Color. Returns DMG dark green
// as the fallback for malformed input. Used by the chroma key color picker
// and OSC color commands.
// Parse "#RRGGBB" or "#RRGGBBAA", or nothing if it is neither.
//
// This copy could only read six digits, so an alpha colour anywhere outside
// main.cpp silently lost its alpha and fell back to the default green. main.cpp
// had a second copy that read both, and which behaviour a caller got depended
// on which file it was compiled into.
std::optional<SDL_Color> tryParseColor(std::string_view input) {
  std::string value(input);
  if ((value.size() != 7 && value.size() != 9) || value[0] != '#') {
    return std::nullopt;
  }
  auto fromHex = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') {
      return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
      return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
      return ch - 'A' + 10;
    }
    return -1;
  };
  auto readByte = [&](int offset) -> int {
    int hi = fromHex(value[offset]);
    int lo = fromHex(value[offset + 1]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    return hi * 16 + lo;
  };

  int r = readByte(1);
  int g = readByte(3);
  int b = readByte(5);
  int a = value.size() == 9 ? readByte(7) : 255;
  if (r < 0 || g < 0 || b < 0 || a < 0) {
    return std::nullopt;
  }
  return SDL_Color {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                    static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
}

// Same, but with the default cue green for anything unparseable. Use
// tryParseColor where "no colour given" has to be told from black.
SDL_Color parseColor(std::string_view input) {
  if (auto parsed = tryParseColor(input)) {
    return *parsed;
  }
  return {48, 98, 48, 255};
}

// Convert SDL_Color to "#rrggbb" hex string (lowercase). Used by OSC feedback
// and the color picker display.
std::string colorToHex(SDL_Color color) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x", color.r, color.g, color.b);
  return buf;
}

// Extract individual color channels from a packed 0xRRGGBBAAu uint32.
// Used by the primitives renderer to unpack theme color constants.
std::uint8_t alpha(std::uint32_t rgba) {
  return static_cast<std::uint8_t>(rgba & 0xFFu);
}

std::uint8_t blue(std::uint32_t rgba) {
  return static_cast<std::uint8_t>((rgba >> 8) & 0xFFu);
}

std::uint8_t green(std::uint32_t rgba) {
  return static_cast<std::uint8_t>((rgba >> 16) & 0xFFu);
}

std::uint8_t red(std::uint32_t rgba) {
  return static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
}

// Unpack a 0xRRGGBBAAu constant to an SDL_Color struct.
// Convenience wrapper around the individual channel extractors above.
SDL_Color colorFromRgba(std::uint32_t rgba) {
  return {red(rgba), green(rgba), blue(rgba), alpha(rgba)};
}

// Map a color tag name ("red", "blue", etc.) to its SDL_Color.
// The color tag palette provides 7 named colors for cue labeling.
// Returns DMG dark green for unrecognized tags (including empty = "no tag").
SDL_Color colorTagToSdl(const std::string& tag, std::uint8_t alpha_val) {
  if (tag == "red")    return {180,  40,  40, alpha_val};
  if (tag == "orange") return {190, 100,  20, alpha_val};
  if (tag == "yellow") return {160, 145,  10, alpha_val};
  if (tag == "cyan")   return { 15, 140, 140, alpha_val};
  if (tag == "blue")   return { 20,  60, 175, alpha_val};
  if (tag == "purple") return {110,  30, 150, alpha_val};
  if (tag == "pink")   return {175,  45, 115, alpha_val};
  return {48, 98, 48, alpha_val};
}

// Cycle to the next color tag in the palette. Wraps around to "" (no tag)
// after "pink". Used by the CycleColorTag quick action in the inspector.
std::string nextColorTag(const std::string& current) {
  static const std::vector<std::string> kTags =
    {"", "red", "orange", "yellow", "cyan", "blue", "purple", "pink"};
  auto it = std::find(kTags.begin(), kTags.end(), current);
  if (it == kTags.end() || std::next(it) == kTags.end()) return "";
  return *std::next(it);
}

// ============================================================================
// 6. Geometry helpers
// ============================================================================

// Shrink a rectangle by `amount` pixels on all four sides (padding/margin).
// Width and height are clamped to 0 to prevent negative dimensions.
SDL_Rect insetRect(const SDL_Rect& rect, int amount) {
  return {
    rect.x + amount,
    rect.y + amount,
    std::max(0, rect.w - amount * 2),
    std::max(0, rect.h - amount * 2)
  };
}

// Hit-test: is the point (x,y) inside the given rectangle?
// Used by app_input.ipp for mouse click detection on buttons, cue rows, etc.
bool pointInRect(int x, int y, const SDL_Rect& rect) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

// Split text on runs of whitespace. Used by OSC/Companion command parsing
// (e.g. "/take 1 2" → ["take", "1", "2"]).
std::vector<std::string> splitWhitespace(const std::string& text) {
  std::vector<std::string> parts;
  std::stringstream stream(text);
  std::string item;
  while (stream >> item) {
    parts.push_back(item);
  }
  return parts;
}

// ASCII-only uppercase conversion (returns a copy). Used for case-insensitive
// string matching in OSC commands and transition style parsing.
std::string toUpper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

// ASCII-only lowercase conversion (returns a copy). Used by safeBool() for
// case-insensitive "true"/"false" parsing in project files.
std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

// Join string parts from startIndex onward with spaces. Used by OSC command
// handling to reconstruct multi-word arguments (e.g. cue name with spaces).
std::string joinParts(const std::vector<std::string>& parts, size_t startIndex) {
  if (startIndex >= parts.size()) {
    return "";
  }
  std::string joined;
  for (size_t index = startIndex; index < parts.size(); ++index) {
    if (!joined.empty()) {
      joined += ' ';
    }
    joined += parts[index];
  }
  return joined;
}

// ============================================================================
// 7. Safe field parsers (project file deserialization)
//
// These functions safely extract typed values from a vector of tab-delimited
// strings (the fields of a single line in a .deckboy project file).
// If the index is out of bounds or the value can't be parsed, the fallback
// is returned. This enables backwards compatibility: older project files
// with fewer fields are loaded without errors.
// ============================================================================

// Return the string at `index`, or empty string if out of bounds.
std::string safeString(const std::vector<std::string>& fields, size_t index) {
  return index < fields.size() ? fields[index] : "";
}

// Parse an integer from the field at `index`, or return `fallback` on failure.
double safeDouble(const std::vector<std::string>& fields, size_t index, double fallback) {
  if (index >= fields.size()) {
    return fallback;
  }
  try {
    return std::stod(fields[index]);
  } catch (...) {
    return fallback;
  }
}

int safeInt(const std::vector<std::string>& fields, size_t index, int fallback) {
  if (index >= fields.size()) {
    return fallback;
  }
  try {
    return std::stoi(fields[index]);
  } catch (...) {
    return fallback;
  }
}

// Parse an unsigned size value (for file sizes) from the field at `index`.
std::uintmax_t safeSize(const std::vector<std::string>& fields, size_t index, std::uintmax_t fallback) {
  if (index >= fields.size()) {
    return fallback;
  }
  try {
    // stoull, not stoul: unsigned long is 32 bits on Windows, so a media file
    // over 4GB threw here and the cue reported the fallback size instead.
    return static_cast<std::uintmax_t>(std::stoull(fields[index]));
  } catch (...) {
    return fallback;
  }
}

// Parse a boolean from the field at `index`. Accepts multiple formats:
// true/false, 1/0, yes/no, on/off (case-insensitive).
bool safeBool(const std::vector<std::string>& fields, size_t index, bool fallback) {
  if (index >= fields.size()) {
    return fallback;
  }
  std::string lower = toLower(fields[index]);
  if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
    return true;
  }
  if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
    return false;
  }
  return fallback;
}

// ============================================================================
// 8. JSON escaping
// ============================================================================

// Escape a string for safe embedding in a JSON string literal.
// Handles: double quotes, backslashes, control characters (as \uXXXX).
// Used by OSC/Companion feedback to build JSON state payloads in app_network.ipp.
std::string escapeJson(const std::string& value) {
  std::string result;
  for (char ch : value) {
    switch (ch) {
      case '"':  result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (ch < 32) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(ch));
          result += buf;
        } else {
          result += ch;
        }
    }
  }
  return result;
}

}  // namespace deckboy::core::utils
