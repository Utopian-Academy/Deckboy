// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// named_colours.hpp -- what the colour picker understands when someone TYPES
// a colour: a hex, three numbers, a CSS name, a RAL number, or a name from a
// library the operator added themselves.
//
// PANTONE IS NOT BUNDLED. Its values are licensed, and Pantone has had them
// pulled from other software over exactly this (Adobe, 2022). James,
// 2026-10-30: an operator who has them puts their own table in the colours
// folder (Paths::stateDir()/colours) and types the code; Deckboy ships none.
// The same folder takes a brand's palette or anything else.
//
// SDL-free, so the parsing can be checked without a window.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/fs.hpp"

namespace deckboy::colours {

struct NamedColour {
  const char* name;
  std::uint32_t rgb;   // 0xRRGGBB, sRGB
};

#include "core/named_colours_data.inc"

// A colour from one of the operator's own files.
struct LibraryColour {
  std::string name;
  std::uint32_t rgb = 0;
  std::string library;   // the file it came from, without the extension
};

// Lower case, letters and digits only: "PANTONE 186 C" and "186c" compare.
inline std::string normaliseColourName(std::string_view text) {
  std::string out;
  for (const char ch : text) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

// "#ff2040", "ff2040", "#f24", "0xff2040".
inline std::optional<std::uint32_t> parseHexColour(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
  bool marked = false;
  if (!text.empty() && text.front() == '#') {
    text.remove_prefix(1);
    marked = true;
  } else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    text.remove_prefix(2);
    marked = true;
  }
  // Three digits only with the #: "186" is a Pantone number, not #118866.
  if (text.size() != 6 && !(marked && text.size() == 3)) return std::nullopt;
  std::uint32_t value = 0;
  for (const char ch : text) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (!std::isxdigit(c)) return std::nullopt;
    value = value * 16 + static_cast<std::uint32_t>(std::isdigit(c) ? c - '0' : std::tolower(c) - 'a' + 10);
  }
  if (text.size() == 3) {
    // #f24 is #ff2244, as in CSS.
    const std::uint32_t r = (value >> 8) & 0xF, g = (value >> 4) & 0xF, b = value & 0xF;
    value = (r * 17 << 16) | (g * 17 << 8) | (b * 17);
  }
  return value;
}

// "255 32 64", "255,32,64", "rgb(255, 32, 64)": three whole numbers, 0-255.
inline std::optional<std::uint32_t> parseRgbTriplet(std::string_view text) {
  int values[3] = {0, 0, 0};
  int count = 0;
  std::size_t i = 0;
  std::string_view rest = text;
  const std::string lowered = normaliseColourName(text.substr(0, std::min<std::size_t>(3, text.size())));
  if (lowered == "rgb") rest.remove_prefix(3);
  while (i < rest.size()) {
    const char ch = rest[i];
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      if (count == 3) return std::nullopt;
      int v = 0;
      while (i < rest.size() && std::isdigit(static_cast<unsigned char>(rest[i]))) {
        v = v * 10 + (rest[i] - '0');
        if (v > 255) return std::nullopt;
        ++i;
      }
      values[count++] = v;
      continue;
    }
    if (ch != ' ' && ch != ',' && ch != '(' && ch != ')' && ch != '\t') return std::nullopt;
    ++i;
  }
  if (count != 3) return std::nullopt;
  return (static_cast<std::uint32_t>(values[0]) << 16) | (static_cast<std::uint32_t>(values[1]) << 8) |
         static_cast<std::uint32_t>(values[2]);
}

// Every .csv and .txt in the folder: one colour a line, a name and a hex in
// either order, split by a comma, a semicolon or a tab. A line with no hex on
// it (a header, a note) is skipped, so a table exported from a spreadsheet
// reads as it is.
inline std::vector<LibraryColour> loadColourLibraries(const dbfs::path& folder) {
  std::vector<LibraryColour> out;
  std::error_code ec;
  if (!dbfs::is_directory(folder, ec)) return out;
  std::vector<dbfs::path> files;
  for (const auto& entry : dbfs::directory_iterator(folder, ec)) {
    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".csv" || ext == ".txt") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  for (const auto& file : files) {
    std::ifstream in(file.string());
    std::string line;
    while (std::getline(in, line) && out.size() < 20000) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      std::vector<std::string> cells;
      std::string cell;
      for (const char ch : line) {
        if (ch == ',' || ch == ';' || ch == '\t') {
          cells.push_back(cell);
          cell.clear();
        } else {
          cell.push_back(ch);
        }
      }
      cells.push_back(cell);
      std::optional<std::uint32_t> rgb;
      std::string name;
      for (const auto& c : cells) {
        if (!rgb) {
          if (auto hex = parseHexColour(c); hex && c.find_first_not_of(" \t#0123456789abcdefABCDEFx") == std::string::npos) {
            rgb = hex;
            continue;
          }
        }
        if (name.empty() && normaliseColourName(c).size() > 0) {
          const auto first = c.find_first_not_of(" \t\"");
          const auto last = c.find_last_not_of(" \t\"");
          if (first != std::string::npos) name = c.substr(first, last - first + 1);
        }
      }
      if (rgb && !name.empty()) out.push_back({name, *rgb, file.stem().string()});
    }
  }
  return out;
}

struct ColourMatch {
  std::uint32_t rgb = 0;
  std::string name;    // what it matched, for the toast; empty for a typed value
  bool exact = true;   // false: the first name that CONTAINED what was typed
};

namespace detail {
// The ways a name can be typed: whole, without its family word ("186 C" for
// "PANTONE 186 C"), and for RAL the number alone or the English alone.
inline std::vector<std::string> colourNameForms(std::string_view name) {
  std::vector<std::string> forms {normaliseColourName(name)};
  const std::size_t space = name.find(' ');
  if (space != std::string_view::npos) {
    const std::string_view head = name.substr(0, space);
    const bool allLetters = std::all_of(head.begin(), head.end(), [](char c) {
      return std::isalpha(static_cast<unsigned char>(c)) != 0;
    });
    if (allLetters) forms.push_back(normaliseColourName(name.substr(space + 1)));
    if (head == "RAL" && name.size() > space + 5) {
      forms.push_back(normaliseColourName(name.substr(space + 1, 4)));   // 3020
      forms.push_back(normaliseColourName(name.substr(space + 5)));      // traffic red
    }
  }
  return forms;
}
}  // namespace detail

// What a typed colour means, or nothing. The operator's libraries are looked
// at first: they added them on purpose.
inline std::optional<ColourMatch> findColour(std::string_view text,
                                             const std::vector<LibraryColour>& libraries) {
  if (auto hex = parseHexColour(text)) return ColourMatch {*hex, {}, true};
  if (auto rgb = parseRgbTriplet(text)) return ColourMatch {*rgb, {}, true};
  const std::string wanted = normaliseColourName(text);
  if (wanted.empty()) return std::nullopt;
  struct Candidate { std::string_view name; std::uint32_t rgb; };
  std::vector<Candidate> all;
  all.reserve(libraries.size() + std::size(kCssColours) + std::size(kRalColours));
  for (const auto& c : libraries) all.push_back({c.name, c.rgb});
  for (const auto& c : kCssColours) all.push_back({c.name, c.rgb});
  for (const auto& c : kRalColours) all.push_back({c.name, c.rgb});
  for (const auto& c : all) {
    for (const auto& form : detail::colourNameForms(c.name)) {
      if (form == wanted) return ColourMatch {c.rgb, std::string(c.name), true};
    }
  }
  if (wanted.size() >= 3) {
    for (const auto& c : all) {
      if (normaliseColourName(c.name).find(wanted) != std::string::npos) {
        return ColourMatch {c.rgb, std::string(c.name), false};
      }
    }
  }
  return std::nullopt;
}

}  // namespace deckboy::colours
