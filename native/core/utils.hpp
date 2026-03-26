// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.


#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL.h>

#include "types.hpp"

namespace deckboy::core::utils {

// String utilities
std::string trim(const std::string& value);
std::vector<std::string> splitLines(const std::string& text);
std::vector<std::string> splitByChar(const std::string& text, char separator);
std::vector<std::string> splitWhitespace(const std::string& text);
std::string toUpper(std::string value);
std::string toLower(std::string value);
std::string joinParts(const std::vector<std::string>& parts, size_t startIndex);

// Timecode & time formatting
std::string formatSeconds(double seconds);
std::string formatTimecode(double seconds, double fps);
std::optional<double> parseTimecodeSeconds(std::string value, double fps);

// Cue kind conversions
std::string cueKindLabel(::CueKind kind);
std::string cueKindToken(::CueKind kind);

// Cue end action conversions
std::string cueEndActionToken(::CueEndAction action);
std::string cueEndActionLabel(::CueEndAction action);
::CueEndAction parseCueEndAction(const std::string& token);

// Transport state conversions
std::string transportLabel(::TransportState state);

// Transition style conversions
std::string transitionStyleToken(::TransitionStyle style);
::TransitionStyle parseTransitionStyleToken(std::string token);

// Easing functions
double easeOutCubic(double value);

// Color utilities
SDL_Color parseColor(std::string_view input);
std::string colorToHex(SDL_Color color);
SDL_Color colorFromRgba(std::uint32_t rgba);
SDL_Color colorTagToSdl(const std::string& tag, std::uint8_t alpha = 255);
std::string nextColorTag(const std::string& current);
std::uint8_t alpha(std::uint32_t rgba);
std::uint8_t blue(std::uint32_t rgba);
std::uint8_t green(std::uint32_t rgba);
std::uint8_t red(std::uint32_t rgba);

// Rectangle utilities
SDL_Rect insetRect(const SDL_Rect& rect, int amount);

// Geometry
bool pointInRect(int x, int y, const SDL_Rect& rect);

// Field parsing helpers
std::string safeString(const std::vector<std::string>& fields, size_t index);
int safeInt(const std::vector<std::string>& fields, size_t index, int fallback = 0);
std::uintmax_t safeSize(const std::vector<std::string>& fields, size_t index, std::uintmax_t fallback = 0);
bool safeBool(const std::vector<std::string>& fields, size_t index, bool fallback = false);

// JSON escaping
std::string escapeJson(const std::string& value);

}  // namespace deckboy::core::utils
