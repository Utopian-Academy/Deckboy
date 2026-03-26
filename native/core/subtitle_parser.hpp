// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.


#pragma once

#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace deckboy::core {

struct SubtitleEntry {
  double startSeconds = 0.0;
  double endSeconds = 0.0;
  std::string text;
};

struct SubtitleTrack {
  std::vector<SubtitleEntry> entries;

  const SubtitleEntry* entryAtTime(double seconds) const {
    for (const auto& entry : entries) {
      if (seconds >= entry.startSeconds && seconds < entry.endSeconds) {
        return &entry;
      }
    }
    return nullptr;
  }

  bool empty() const { return entries.empty(); }
};

// Parse SRT subtitle time "HH:MM:SS,mmm" to seconds.
inline std::optional<double> parseSrtTime(const std::string& s) {
  // Formats: "HH:MM:SS,mmm" or "HH:MM:SS.mmm"
  int h = 0, m = 0, sec = 0, ms = 0;
  char sep = ',';
  if (std::sscanf(s.c_str(), "%d:%d:%d,%d", &h, &m, &sec, &ms) == 4) {
    return h * 3600.0 + m * 60.0 + sec + ms / 1000.0;
  }
  if (std::sscanf(s.c_str(), "%d:%d:%d.%d", &h, &m, &sec, &ms) == 4) {
    return h * 3600.0 + m * 60.0 + sec + ms / 1000.0;
  }
  return std::nullopt;
}

// Parse an SRT file into a SubtitleTrack.
inline SubtitleTrack parseSrtFile(const std::string& path) {
  SubtitleTrack track;
  std::ifstream file(path);
  if (!file) return track;

  std::string line;
  enum class State { Index, Timing, Text };
  State state = State::Index;
  SubtitleEntry current;

  while (std::getline(file, line)) {
    // Strip CR
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    switch (state) {
      case State::Index:
        // Skip blank lines and sequence numbers
        if (line.empty()) continue;
        // If this line contains "-->", it's actually a timing line (some SRTs skip numbers)
        if (line.find("-->") != std::string::npos) {
          goto parse_timing;
        }
        // Otherwise it should be a sequence number — skip it
        state = State::Timing;
        break;

      case State::Timing:
      parse_timing: {
        auto arrow = line.find("-->");
        if (arrow == std::string::npos) {
          // Malformed — try to recover
          state = State::Index;
          break;
        }
        std::string startStr = line.substr(0, arrow);
        std::string endStr = line.substr(arrow + 3);
        // Trim whitespace
        while (!startStr.empty() && std::isspace(static_cast<unsigned char>(startStr.back())))
          startStr.pop_back();
        while (!endStr.empty() && std::isspace(static_cast<unsigned char>(endStr.front())))
          endStr.erase(endStr.begin());
        // Strip position tags after the end time (e.g. "X1:... X2:...")
        auto space = endStr.find(' ');
        if (space != std::string::npos)
          endStr = endStr.substr(0, space);

        auto start = parseSrtTime(startStr);
        auto end = parseSrtTime(endStr);
        if (start && end) {
          current.startSeconds = *start;
          current.endSeconds = *end;
          current.text.clear();
          state = State::Text;
        } else {
          state = State::Index;
        }
        break;
      }

      case State::Text:
        if (line.empty()) {
          // End of this subtitle block
          if (!current.text.empty()) {
            track.entries.push_back(current);
          }
          current = {};
          state = State::Index;
        } else {
          if (!current.text.empty()) {
            current.text += '\n';
          }
          current.text += line;
        }
        break;
    }
  }

  // Flush last entry
  if (!current.text.empty() && state == State::Text) {
    track.entries.push_back(current);
  }

  // Sort by start time
  std::sort(track.entries.begin(), track.entries.end(),
    [](const SubtitleEntry& a, const SubtitleEntry& b) {
      return a.startSeconds < b.startSeconds;
    });

  return track;
}

// Strip basic HTML-like tags from subtitle text (<b>, <i>, <u>, <font ...>)
inline std::string stripSubtitleTags(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  bool inTag = false;
  for (char ch : text) {
    if (ch == '<') {
      inTag = true;
      continue;
    }
    if (ch == '>') {
      inTag = false;
      continue;
    }
    if (!inTag) {
      result += ch;
    }
  }
  return result;
}

}  // namespace deckboy::core
