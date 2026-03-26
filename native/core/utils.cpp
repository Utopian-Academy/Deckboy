// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.


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

std::string trim(const std::string& value) {
  auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::stringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

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
  parts.push_back(current);
  return parts;
}

std::string formatSeconds(double seconds) {
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    return "00:00.0";
  }
  int wholeMinutes = static_cast<int>(seconds / 60.0);
  double remaining = seconds - wholeMinutes * 60.0;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%04.1f", wholeMinutes, remaining);
  return buf;
}

std::string formatTimecode(double seconds, double fps) {
  double safeFps = std::isfinite(fps) && fps > 1.0 ? fps : 30.0;
  double clamped = std::max(0.0, std::isfinite(seconds) ? seconds : 0.0);
  int totalFrames = static_cast<int>(std::floor(clamped * safeFps + 0.0001));
  int fpsInt = std::max(1, static_cast<int>(std::round(safeFps)));
  int frame = totalFrames % fpsInt;
  int totalSeconds = totalFrames / fpsInt;
  int secs = totalSeconds % 60;
  int mins = (totalSeconds / 60) % 60;
  int hours = totalSeconds / 3600;
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d", hours, mins, secs, frame);
  return buf;
}

std::optional<double> parseTimecodeSeconds(std::string value, double fps) {
  value = trim(value);
  if (value.empty()) {
    return std::nullopt;
  }

  if (value.find(':') == std::string::npos) {
    try {
      return std::max(0.0, std::stod(value));
    } catch (...) {
      return std::nullopt;
    }
  }

  auto parts = splitByChar(value, ':');
  if (parts.size() < 2 || parts.size() > 4) {
    return std::nullopt;
  }

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

  if (parts.size() == 4) {
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
  } else if (parts.size() == 3) {
    auto h = parseIntPart(parts[0]);
    auto m = parseIntPart(parts[1]);
    auto s = parseIntPart(parts[2]);
    if (!h || !m || !s) {
      return std::nullopt;
    }
    hours = *h;
    mins = *m;
    secs = *s;
  } else {
    auto m = parseIntPart(parts[0]);
    auto s = parseIntPart(parts[1]);
    if (!m || !s) {
      return std::nullopt;
    }
    mins = *m;
    secs = *s;
  }

  double result = static_cast<double>(hours * 3600 + mins * 60 + secs);
  result += static_cast<double>(frames) / safeFps;
  return std::max(0.0, result);
}

std::string cueKindLabel(CueKind kind) {
  switch (kind) {
    case CueKind::Image:      return "Still";
    case CueKind::Pattern:    return "Pattern";
    case CueKind::Browser:    return "Browser";
    case CueKind::LowerThird: return "Lower Third";
    case CueKind::Audio:      return "Audio";
    case CueKind::Video:
    default:                         return "Video";
  }
}

std::string cueKindToken(CueKind kind) {
  switch (kind) {
    case CueKind::Image:      return "image";
    case CueKind::Pattern:    return "pattern";
    case CueKind::Browser:    return "browser";
    case CueKind::LowerThird: return "lower_third";
    case CueKind::Audio:      return "audio";
    case CueKind::Video:
    default:                         return "video";
  }
}

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

CueEndAction parseCueEndAction(const std::string& s) {
  if (s == "stop")    return CueEndAction::Stop;
  if (s == "loop")    return CueEndAction::Loop;
  if (s == "hold")    return CueEndAction::PauseOnLast;
  if (s == "next")    return CueEndAction::AutoNext;
  return CueEndAction::Inherit;
}

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

double easeOutCubic(double value) {
  double t = std::clamp(value, 0.0, 1.0);
  double inv = 1.0 - t;
  return 1.0 - inv * inv * inv;
}

SDL_Color parseColor(std::string_view input) {
  std::string value(input);
  if (value.size() != 7 || value[0] != '#') {
    return {48, 98, 48, 255};
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
  if (r < 0 || g < 0 || b < 0) {
    return {48, 98, 48, 255};
  }
  return {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), 255};
}

std::string colorToHex(SDL_Color color) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x", color.r, color.g, color.b);
  return buf;
}

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

SDL_Color colorFromRgba(std::uint32_t rgba) {
  return {red(rgba), green(rgba), blue(rgba), alpha(rgba)};
}

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

std::string nextColorTag(const std::string& current) {
  static const std::vector<std::string> kTags =
    {"", "red", "orange", "yellow", "cyan", "blue", "purple", "pink"};
  auto it = std::find(kTags.begin(), kTags.end(), current);
  if (it == kTags.end() || std::next(it) == kTags.end()) return "";
  return *std::next(it);
}

SDL_Rect insetRect(const SDL_Rect& rect, int amount) {
  return {
    rect.x + amount,
    rect.y + amount,
    std::max(0, rect.w - amount * 2),
    std::max(0, rect.h - amount * 2)
  };
}

bool pointInRect(int x, int y, const SDL_Rect& rect) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

std::vector<std::string> splitWhitespace(const std::string& text) {
  std::vector<std::string> parts;
  std::stringstream stream(text);
  std::string item;
  while (stream >> item) {
    parts.push_back(item);
  }
  return parts;
}

std::string toUpper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

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

std::string safeString(const std::vector<std::string>& fields, size_t index) {
  return index < fields.size() ? fields[index] : "";
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

std::uintmax_t safeSize(const std::vector<std::string>& fields, size_t index, std::uintmax_t fallback) {
  if (index >= fields.size()) {
    return fallback;
  }
  try {
    unsigned long val = std::stoul(fields[index]);
    return static_cast<std::uintmax_t>(val);
  } catch (...) {
    return fallback;
  }
}

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
