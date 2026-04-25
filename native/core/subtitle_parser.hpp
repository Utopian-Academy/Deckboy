// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// subtitle_parser.hpp — SRT subtitle file parser and runtime lookup.
//
// Parses SubRip (.srt) subtitle files into a time-indexed SubtitleTrack.
// The renderer queries entryAtTime() each frame to get the current subtitle
// text, which is drawn as a text overlay on the output.
//
// SRT format (per entry):
//   1           ← sequence number (ignored, may be missing)
//   00:01:23,456 --> 00:01:25,789  ← timing (comma or dot for milliseconds)
//   Hello world ← text (may span multiple lines, may contain HTML-like tags)
//                ← blank line separates entries
//
// The parser is tolerant of format variations:
//   - Accepts both comma and dot as millisecond separator
//   - Handles missing sequence numbers
//   - Strips position tags after the end time
//   - Sorts entries by start time after parsing
//
// stripSubtitleTags() removes basic HTML formatting (<b>, <i>, <font>, etc.)
// since the text renderer doesn't support rich text.
//
// Used by: MediaEngine (loads subtitle track on cue load) and the output
// renderer (queries current subtitle for each frame).
// ============================================================================

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace deckboy::core {

// A single subtitle entry with its time range and display text.
struct SubtitleEntry {
  double startSeconds = 0.0;  // when to start showing this subtitle
  double endSeconds = 0.0;    // when to stop showing this subtitle
  std::string text;           // subtitle text (may contain newlines for multi-line)
};

// A complete subtitle track (ordered list of timed entries).
struct SubtitleTrack {
  std::vector<SubtitleEntry> entries;  // sorted by startSeconds after parsing

  // Look up the subtitle entry active at the given time.
  // Returns nullptr if no subtitle is visible at that time.
  // Linear scan is fine for typical subtitle counts (<1000 entries).
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

// Parse an SRT time string "HH:MM:SS,mmm" or "HH:MM:SS.mmm" to seconds.
// Returns nullopt if the format doesn't match.
inline std::optional<double> parseSrtTime(const std::string& s) {
  int h = 0, m = 0, sec = 0, ms = 0;
  // Try comma separator first (standard SRT), then dot (common variant)
  if (std::sscanf(s.c_str(), "%d:%d:%d,%d", &h, &m, &sec, &ms) == 4) {
    return h * 3600.0 + m * 60.0 + sec + ms / 1000.0;
  }
  if (std::sscanf(s.c_str(), "%d:%d:%d.%d", &h, &m, &sec, &ms) == 4) {
    return h * 3600.0 + m * 60.0 + sec + ms / 1000.0;
  }
  return std::nullopt;
}

// Parse an SRT subtitle stream into a SubtitleTrack.
// Uses a state machine: Index → Timing → Text → (blank line) → Index.
// Tolerant of malformed input: skips bad entries and continues parsing.
inline SubtitleTrack parseSrtStream(std::istream& input) {
  SubtitleTrack track;
  std::string line;
  enum class State { Index, Timing, Text };
  State state = State::Index;
  SubtitleEntry current;

  while (std::getline(input, line)) {
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

// Parse an SRT file from disk into a SubtitleTrack.
// Returns an empty track if the file can't be opened.
inline SubtitleTrack parseSrtFile(const std::string& path) {
  std::ifstream file(path);
  if (!file) return {};
  return parseSrtStream(file);
}

// Parse SRT content from a string (e.g. extracted from ffmpeg embedded subtitle stream).
inline SubtitleTrack parseSrtText(const std::string& text) {
  std::istringstream input(text);
  return parseSrtStream(input);
}

// Strip basic HTML-like tags from subtitle text (<b>, <i>, <u>, <font ...>).
// SRT files commonly include formatting tags that Deckboy's text renderer
// doesn't support, so they're stripped before display.
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
