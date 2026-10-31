// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.
#pragma once

// The fonts installed on this computer, by family name.
//
// James, 2026-10-30: "when trying to load a font, it doent open a picker of
// all available fonts, it asks to load a file, which seems wrong". The list
// he approved (board H856G24w) names each family once and draws it in itself.
//
// Each platform keeps its fonts in known folders, and every TrueType and
// OpenType file carries its own family name in its 'name' table, so one
// reader serves all three: no DirectWrite, CoreText or fontconfig, and the
// path it hands back is a file the timer's TTF_OpenFont can open as it is.
// A family is listed once, by its regular face where it has one.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "core/fs.hpp"

namespace deckboy::fonts {

struct SystemFont {
  std::string family;
  std::string path;
};

namespace detail {

inline std::uint32_t be32(const unsigned char* p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
inline std::uint16_t be16(const unsigned char* p) {
  return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

inline bool readAt(std::ifstream& in, std::uint64_t at, unsigned char* out, std::size_t n) {
  in.clear();
  in.seekg(static_cast<std::streamoff>(at));
  in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n));
  return static_cast<std::size_t>(in.gcount()) == n;
}

// UTF-16BE (the Windows platform's names) to UTF-8.
inline std::string utf16beToUtf8(const unsigned char* p, std::size_t bytes) {
  std::string out;
  for (std::size_t i = 0; i + 1 < bytes; i += 2) {
    std::uint32_t c = be16(p + i);
    if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes) {
      const std::uint32_t lo = be16(p + i + 2);
      if (lo >= 0xDC00 && lo < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      }
    }
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

struct FaceNames {
  std::string family;
  std::string style;
};

// The family and style of the face whose table directory starts at `at`.
// The typographic names (16, 17) win over the legacy ones (1, 2), which split
// a big family into "Bahnschrift SemiBold" and friends; English wins over
// other languages; a Mac Roman name is used only when there is nothing else.
inline bool readFaceNames(std::ifstream& in, std::uint64_t at, FaceNames& out) {
  unsigned char head[12];
  if (!readAt(in, at, head, sizeof(head))) return false;
  const std::uint32_t tag = be32(head);
  if (tag != 0x00010000u && tag != 0x4F54544Fu /* OTTO */ && tag != 0x74727565u /* true */) return false;
  const int tables = be16(head + 4);
  if (tables <= 0 || tables > 512) return false;
  std::vector<unsigned char> dir(static_cast<std::size_t>(tables) * 16);
  if (!readAt(in, at + 12, dir.data(), dir.size())) return false;
  std::uint32_t nameOffset = 0, nameLength = 0;
  for (int i = 0; i < tables; ++i) {
    const unsigned char* e = dir.data() + i * 16;
    if (be32(e) == 0x6E616D65u /* name */) {
      nameOffset = be32(e + 8);
      nameLength = be32(e + 12);
      break;
    }
  }
  if (nameOffset == 0 || nameLength < 6 || nameLength > (1u << 20)) return false;
  std::vector<unsigned char> name(nameLength);
  if (!readAt(in, nameOffset, name.data(), name.size())) return false;
  const int count = be16(name.data() + 2);
  const std::uint32_t strings = be16(name.data() + 4);
  // Best score per name id seen so far: higher is better.
  int bestScore[4] = {-1, -1, -1, -1};   // ids 1, 2, 16, 17
  std::string best[4];
  for (int i = 0; i < count; ++i) {
    const std::size_t r = 6 + static_cast<std::size_t>(i) * 12;
    if (r + 12 > name.size()) break;
    const int platform = be16(&name[r]);
    const int language = be16(&name[r + 4]);
    const int id = be16(&name[r + 6]);
    const std::uint32_t length = be16(&name[r + 8]);
    const std::uint32_t offset = strings + be16(&name[r + 10]);
    const int slot = id == 1 ? 0 : id == 2 ? 1 : id == 16 ? 2 : id == 17 ? 3 : -1;
    if (slot < 0 || length == 0 || offset + length > name.size()) continue;
    int score = -1;
    std::string text;
    if (platform == 3 || platform == 0) {
      score = (platform == 3 && language == 0x0409) ? 3 : 2;
      text = utf16beToUtf8(&name[offset], length);
    } else if (platform == 1 && language == 0) {
      score = 1;
      text.assign(reinterpret_cast<const char*>(&name[offset]), length);
    }
    if (score > bestScore[slot] && !text.empty()) {
      bestScore[slot] = score;
      best[slot] = text;
    }
  }
  out.family = !best[2].empty() ? best[2] : best[0];
  out.style = !best[2].empty() ? (best[3].empty() ? std::string("Regular") : best[3]) : best[1];
  return !out.family.empty();
}

inline std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// How plainly a style is the family's everyday face: lower is plainer.
inline int styleRank(const std::string& style) {
  const std::string s = lower(style);
  if (s == "regular" || s == "normal" || s == "book" || s == "roman") return 0;
  if (s == "medium") return 1;
  if (s.find("italic") == std::string::npos && s.find("oblique") == std::string::npos) return 2;
  return 3;
}

inline std::string utf8Of(const dbfs::path& p) {
  const auto u8 = p.u8string();
  return std::string(u8.begin(), u8.end());
}

}  // namespace detail

// The folders each platform installs fonts into, the user's own included.
inline std::vector<dbfs::path> systemFontFolders() {
  std::vector<dbfs::path> folders;
  auto env = [](const char* name) -> std::string {
    const char* v = std::getenv(name);
    return (v && v[0] != '\0') ? std::string(v) : std::string();
  };
#ifdef _WIN32
  const std::string windir = env("WINDIR");
  folders.push_back(windir.empty() ? dbfs::path("C:/Windows/Fonts") : dbfs::path(windir) / "Fonts");
  // Fonts installed "for me only" since Windows 10 1809.
  if (const std::string local = env("LOCALAPPDATA"); !local.empty()) {
    folders.push_back(dbfs::path(local) / "Microsoft" / "Windows" / "Fonts");
  }
#elif defined(__APPLE__)
  folders.push_back("/System/Library/Fonts");
  folders.push_back("/Library/Fonts");
  if (const std::string home = env("HOME"); !home.empty()) folders.push_back(dbfs::path(home) / "Library" / "Fonts");
#else
  folders.push_back("/usr/share/fonts");
  folders.push_back("/usr/local/share/fonts");
  const std::string home = env("HOME");
  const std::string xdg = env("XDG_DATA_HOME");
  if (!xdg.empty()) folders.push_back(dbfs::path(xdg) / "fonts");
  else if (!home.empty()) folders.push_back(dbfs::path(home) / ".local" / "share" / "fonts");
  if (!home.empty()) folders.push_back(dbfs::path(home) / ".fonts");
#endif
  return folders;
}

// Every family found under `folders`, sorted by name, one entry each. A
// collection (.ttc) is listed by its first face, which is the one
// TTF_OpenFont opens. Names starting with a dot are the system's private
// faces (the Mac's .SF NS); they are left out, as every font menu does.
inline std::vector<SystemFont> listSystemFonts(const std::vector<dbfs::path>& folders) {
  struct Pick {
    std::string family;
    std::string path;
    int rank = 99;
  };
  std::map<std::string, Pick> byFamily;   // keyed by the lowered name
  for (const auto& folder : folders) {
    std::error_code ec;
    if (!dbfs::is_directory(folder, ec)) continue;
    dbfs::recursive_directory_iterator it(folder, dbfs::directory_options::skip_permission_denied, ec);
    const dbfs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
      std::error_code fileEc;
      if (!it->is_regular_file(fileEc)) continue;
      const std::string ext = detail::lower(detail::utf8Of(it->path().extension()));
      if (ext != ".ttf" && ext != ".otf" && ext != ".ttc" && ext != ".otc") continue;
      std::ifstream in(it->path(), std::ios::binary);
      if (!in) continue;
      std::uint64_t at = 0;
      unsigned char tag[12];
      if (!detail::readAt(in, 0, tag, sizeof(tag))) continue;
      if (detail::be32(tag) == 0x74746366u /* ttcf */) at = detail::be32(tag + 8);   // first face
      detail::FaceNames names;
      if (!detail::readFaceNames(in, at, names) || names.family.front() == '.') continue;
      const int rank = detail::styleRank(names.style);
      Pick& pick = byFamily[detail::lower(names.family)];
      if (rank < pick.rank) pick = Pick {names.family, detail::utf8Of(it->path()), rank};
    }
  }
  std::vector<SystemFont> out;
  out.reserve(byFamily.size());
  for (auto& [key, pick] : byFamily) out.push_back({pick.family, pick.path});
  return out;
}

}  // namespace deckboy::fonts
