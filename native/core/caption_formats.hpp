// ═══════════════════════════════════════════════════════════════════════════
// caption_formats.hpp — the caption formats a broadcast job actually arrives in.
//
// Deckboy read SubRip and nothing else, which is fine for a film festival and
// no use at all to a broadcaster. Captions turn up as WebVTT from the web
// side, as SCC from anyone working to a US broadcast spec, and increasingly as
// TTML from an archive. A playback tool that can only open one of them makes
// the operator go and find a converter before they can start.
//
// So: read all four into the one SubtitleTrack the renderer already draws, and
// write three of them back out. Converting between them is then a side effect
// of being able to read and write, which is most of what a captioning tool is
// for.
//
// SCC IS THE INTERESTING ONE. It is not text with timestamps -- it is the
// actual CEA-608 byte pairs a broadcast encoder would put on line 21, written
// as hex, against drop-frame timecode. Reading it means decoding 608: the
// control codes that position and colour a caption, the two-byte character
// pairs, and the odd-parity bit on every byte. That is why it is the format
// nothing else in this class opens, and why being able to is worth something.
//
// Everything here is pure: text in, track out, track in, text out. The whole
// surface is covered in the smoke tests, which is the only sane way to work on
// a format defined by a document you cannot run.
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "subtitle_parser.hpp"

namespace deckboy::captions {

using deckboy::core::SubtitleEntry;
using deckboy::core::SubtitleTrack;

enum class Format { Unknown, Srt, WebVtt, Scc, Ttml };

// What a file is, by its extension. Content sniffing would be better and is
// not worth it: caption files are named by tools that care about extensions.
inline Format formatForPath(const std::string& path) {
  const std::size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return Format::Unknown;
  std::string ext = path.substr(dot + 1);
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (ext == "srt") return Format::Srt;
  if (ext == "vtt" || ext == "webvtt") return Format::WebVtt;
  if (ext == "scc") return Format::Scc;
  if (ext == "ttml" || ext == "dfxp" || ext == "xml") return Format::Ttml;
  return Format::Unknown;
}

namespace detail {

inline std::string trimmed(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

// hh:mm:ss.mmm or mm:ss.mmm, which WebVTT allows both of.
inline double parseVttTime(const std::string& text) {
  int h = 0, m = 0;
  double s = 0.0;
  const std::string t = trimmed(text);
  if (std::sscanf(t.c_str(), "%d:%d:%lf", &h, &m, &s) == 3) {
    return h * 3600.0 + m * 60.0 + s;
  }
  if (std::sscanf(t.c_str(), "%d:%lf", &m, &s) == 2) {
    return m * 60.0 + s;
  }
  return -1.0;
}

// SCC timecode is hh:mm:ss:ff, with a semicolon before the frames when it is
// drop-frame. Drop-frame counts 29.97 by skipping numbers, not by running
// slow, so the arithmetic differs and getting it wrong drifts by about 3.6
// seconds an hour -- which on a broadcast is a caption landing on the wrong
// shot.
inline double parseSccTime(const std::string& text, bool& dropFrame) {
  int h = 0, m = 0, s = 0, f = 0;
  dropFrame = text.find(';') != std::string::npos;
  std::string normalised = text;
  for (char& c : normalised) {
    if (c == ';') c = ':';
  }
  if (std::sscanf(normalised.c_str(), "%d:%d:%d:%d", &h, &m, &s, &f) != 4) {
    return -1.0;
  }
  const long long totalMinutes = h * 60LL + m;
  long long frames = ((h * 3600LL + m * 60LL + s) * 30LL) + f;
  if (dropFrame) {
    // Two frame NUMBERS dropped every minute except every tenth. Nothing is
    // dropped from the picture -- the count skips ahead so that the timecode
    // keeps up with the clock.
    frames -= 2 * (totalMinutes - totalMinutes / 10);
  }
  // BOTH divide by 29.97, because that is the rate the material runs at.
  // Non-drop counts every frame and therefore falls behind the clock -- by
  // 3.6 seconds an hour, which is exactly the drift drop-frame exists to
  // remove. Dividing non-drop by 30 instead would hide that and put a caption
  // on the wrong shot an hour into a programme.
  return static_cast<double>(frames) / 29.97;
}

// One CEA-608 byte, without its odd-parity bit.
inline std::uint8_t strip608Parity(std::uint8_t byte) {
  return static_cast<std::uint8_t>(byte & 0x7F);
}

// The 608 character set is ASCII for most of its range with a handful of
// substitutions. Only the printable range matters here: control codes are
// handled by the caller, which knows what they mean.
inline char decode608Char(std::uint8_t byte) {
  const std::uint8_t c = strip608Parity(byte);
  switch (c) {
    case 0x2A: return 'a';   // á, flattened
    case 0x5C: return 'e';   // é
    case 0x5E: return 'i';   // í
    case 0x5F: return 'o';   // ó
    case 0x60: return 'u';   // ú
    case 0x7B: return 'c';   // ç
    case 0x7C: return '/';   // ÷
    case 0x7D: return 'N';   // Ñ
    case 0x7E: return 'n';   // ñ
    default:
      return (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '\0';
  }
}

}  // namespace detail

// ── WebVTT ──────────────────────────────────────────────────────────────────
inline SubtitleTrack parseWebVtt(const std::string& text) {
  SubtitleTrack track;
  std::istringstream input(text);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::size_t arrow = line.find("-->");
    if (arrow == std::string::npos) continue;
    SubtitleEntry entry;
    entry.startSeconds = detail::parseVttTime(line.substr(0, arrow));
    // Cue settings (align, line, position) follow the end time on the same
    // line. Dropped rather than parsed: the renderer places captions itself,
    // and a half-honoured position is worse than a consistent one.
    std::string rest = detail::trimmed(line.substr(arrow + 3));
    const std::size_t space = rest.find(' ');
    entry.endSeconds = detail::parseVttTime(
      space == std::string::npos ? rest : rest.substr(0, space));
    if (entry.startSeconds < 0.0 || entry.endSeconds < 0.0) continue;
    std::string body;
    while (std::getline(input, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (detail::trimmed(line).empty()) break;
      if (!body.empty()) body += "\n";
      body += line;
    }
    entry.text = deckboy::core::stripSubtitleTags(body);
    if (!entry.text.empty()) track.entries.push_back(std::move(entry));
  }
  std::sort(track.entries.begin(), track.entries.end(),
            [](const SubtitleEntry& a, const SubtitleEntry& b) {
              return a.startSeconds < b.startSeconds;
            });
  return track;
}

inline std::string writeWebVtt(const SubtitleTrack& track) {
  auto stamp = [](double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    const int h = static_cast<int>(seconds / 3600.0);
    const int m = static_cast<int>(std::fmod(seconds / 60.0, 60.0));
    const double s = std::fmod(seconds, 60.0);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%06.3f", h, m, s);
    return std::string(buffer);
  };
  std::string out = "WEBVTT\n\n";
  for (const SubtitleEntry& entry : track.entries) {
    out += stamp(entry.startSeconds) + " --> " + stamp(entry.endSeconds) + "\n";
    out += entry.text + "\n\n";
  }
  return out;
}

// ── SCC (Scenarist, carrying CEA-608) ───────────────────────────────────────
//
// Each line is a timecode followed by hex byte pairs. A pair is either a
// control code -- which starts a caption, ends one, or moves the cursor -- or
// two characters. Captions are built up until an End-of-Caption or a new
// timecode closes them.
inline SubtitleTrack parseScc(const std::string& text) {
  SubtitleTrack track;
  std::istringstream input(text);
  std::string line;
  std::string pending;
  double pendingStart = -1.0;

  auto flush = [&](double endSeconds) {
    const std::string body = detail::trimmed(pending);
    if (!body.empty() && pendingStart >= 0.0) {
      SubtitleEntry entry;
      entry.startSeconds = pendingStart;
      // An SCC caption runs until the next one displaces it. A run with no
      // successor gets four seconds, which is the usual reading allowance and
      // better than leaving it on screen for the rest of the show.
      entry.endSeconds = endSeconds > pendingStart ? endSeconds : pendingStart + 4.0;
      entry.text = body;
      track.entries.push_back(std::move(entry));
    }
    pending.clear();
    pendingStart = -1.0;
  };

  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string trimmedLine = detail::trimmed(line);
    if (trimmedLine.empty()) continue;
    // The header line, and anything else that is not a timecode.
    if (trimmedLine.rfind("Scenarist_SCC", 0) == 0) continue;
    const std::size_t space = trimmedLine.find_first_of(" \t");
    if (space == std::string::npos) continue;
    bool dropFrame = false;
    const double when = detail::parseSccTime(trimmedLine.substr(0, space), dropFrame);
    if (when < 0.0) continue;

    // A new timecode closes whatever was on screen.
    flush(when);

    std::istringstream pairs(trimmedLine.substr(space));
    std::string token;
    std::string built;
    while (pairs >> token) {
      if (token.size() != 4) continue;
      unsigned value = 0;
      if (std::sscanf(token.c_str(), "%4x", &value) != 1) continue;
      const std::uint8_t hi = detail::strip608Parity(
        static_cast<std::uint8_t>((value >> 8) & 0xFF));
      const std::uint8_t lo = detail::strip608Parity(
        static_cast<std::uint8_t>(value & 0xFF));
      // 0x10-0x1F in the high byte marks a control code rather than text.
      if (hi >= 0x10 && hi <= 0x1F) {
        // Preamble address codes move the cursor to a new row, which reads as
        // a line break. End-of-Caption (0x2F) and Carriage Return (0x2D) do
        // too; Erase codes clear what was building.
        if (lo == 0x2D || (lo >= 0x40 && lo <= 0x7F)) {
          if (!built.empty() && built.back() != '\n') built += "\n";
        } else if (lo == 0x2C || lo == 0x2E) {
          built.clear();   // erase displayed / erase non-displayed
        }
        continue;
      }
      const char a = detail::decode608Char(hi);
      const char b = detail::decode608Char(lo);
      if (a) built += a;
      if (b) built += b;
    }
    const std::string cleaned = detail::trimmed(built);
    if (!cleaned.empty()) {
      pending = cleaned;
      pendingStart = when;
    }
  }
  flush(-1.0);
  std::sort(track.entries.begin(), track.entries.end(),
            [](const SubtitleEntry& a, const SubtitleEntry& b) {
              return a.startSeconds < b.startSeconds;
            });
  return track;
}

// ── TTML / DFXP ─────────────────────────────────────────────────────────────
//
// Read with a deliberately small XML reader rather than a parser: what is
// wanted is every <p> with a begin and an end, and pulling those out of the
// tag soup is a dozen lines. A dependency would be a poor trade for that.
inline SubtitleTrack parseTtml(const std::string& text) {
  SubtitleTrack track;
  std::size_t at = 0;
  auto attribute = [](const std::string& tag, const std::string& name) {
    const std::size_t key = tag.find(name + "=\"");
    if (key == std::string::npos) return std::string();
    const std::size_t start = key + name.size() + 2;
    const std::size_t end = tag.find('"', start);
    if (end == std::string::npos) return std::string();
    return tag.substr(start, end - start);
  };
  while ((at = text.find("<p", at)) != std::string::npos) {
    const std::size_t tagEnd = text.find('>', at);
    if (tagEnd == std::string::npos) break;
    const std::string tag = text.substr(at, tagEnd - at);
    const std::size_t close = text.find("</p>", tagEnd);
    if (close == std::string::npos) break;
    std::string body = text.substr(tagEnd + 1, close - tagEnd - 1);
    // <br/> is the line break TTML uses; everything else in the body is
    // markup we do not draw.
    std::size_t br;
    while ((br = body.find("<br")) != std::string::npos) {
      const std::size_t brEnd = body.find('>', br);
      if (brEnd == std::string::npos) break;
      body = body.substr(0, br) + "\n" + body.substr(brEnd + 1);
    }
    SubtitleEntry entry;
    entry.startSeconds = detail::parseVttTime(attribute(tag, "begin"));
    entry.endSeconds = detail::parseVttTime(attribute(tag, "end"));
    entry.text = detail::trimmed(deckboy::core::stripSubtitleTags(body));
    if (entry.startSeconds >= 0.0 && entry.endSeconds > entry.startSeconds &&
        !entry.text.empty()) {
      track.entries.push_back(std::move(entry));
    }
    at = close + 4;
  }
  return track;
}

// ── SRT out ─────────────────────────────────────────────────────────────────
inline std::string writeSrt(const SubtitleTrack& track) {
  auto stamp = [](double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    const int h = static_cast<int>(seconds / 3600.0);
    const int m = static_cast<int>(std::fmod(seconds / 60.0, 60.0));
    const int s = static_cast<int>(std::fmod(seconds, 60.0));
    const int ms = static_cast<int>((seconds - std::floor(seconds)) * 1000.0 + 0.5);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d,%03d", h, m, s, ms);
    return std::string(buffer);
  };
  std::string out;
  int index = 1;
  for (const SubtitleEntry& entry : track.entries) {
    out += std::to_string(index++) + "\n";
    out += stamp(entry.startSeconds) + " --> " + stamp(entry.endSeconds) + "\n";
    out += entry.text + "\n\n";
  }
  return out;
}

// Read whatever it is, by extension, into the one track the renderer draws.
inline SubtitleTrack parseText(const std::string& text, Format format) {
  switch (format) {
    case Format::WebVtt: return parseWebVtt(text);
    case Format::Scc:    return parseScc(text);
    case Format::Ttml:   return parseTtml(text);
    case Format::Srt:    return deckboy::core::parseSrtText(text);
    default:             return {};
  }
}

}  // namespace deckboy::captions
