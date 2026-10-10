// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// bidi_check -- run Unicode's own conformance tests against native/core/bidi.
//
//   deckboy-bidi-check <BidiTest.txt> <BidiCharacterTest.txt>
//
// Both files come from https://www.unicode.org/Public/<version>/ucd/ and must
// be the version the tables were generated from (it is checked). Every case in
// both must pass; one failure is a failure, because a bidi algorithm that is
// right "nearly always" is the thing this replaced.
//
// BidiTest.txt gives bidi CLASSES and up to three paragraph directions per
// line; BidiCharacterTest.txt gives real code points, which is where paired
// brackets (rule N0) are tested. Between them they cover the whole algorithm
// through rule L2.

#include "core/bidi.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace bidi = deckboy::core::bidi;

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string item;
  std::istringstream in(s);
  while (std::getline(in, item, sep)) out.push_back(item);
  return out;
}

std::vector<std::string> words(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  std::string w;
  while (in >> w) out.push_back(w);
  return out;
}

std::string versionOf(const std::string& path) {
  std::ifstream in(path);
  std::string first;
  std::getline(in, first);
  const auto dash = first.rfind('-');
  const auto dot = first.rfind(".txt");
  if (dash == std::string::npos || dot == std::string::npos || dot <= dash) return "?";
  return first.substr(dash + 1, dot - dash - 1);
}

struct Tally {
  long cases = 0;
  long failures = 0;
};

void report(Tally& t, const std::string& what, int line, const std::string& detail) {
  ++t.failures;
  if (t.failures <= 20) {
    std::cerr << what << " line " << line << ": " << detail << "\n";
  }
}

std::string join(const std::vector<int>& v, bool levels) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) out += ' ';
    if (levels && v[i] == bidi::kRemoved) out += 'x';
    else out += std::to_string(v[i]);
  }
  return out;
}

std::string join(const std::vector<std::size_t>& v) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) out += ' ';
    out += std::to_string(v[i]);
  }
  return out;
}

std::vector<int> parseLevels(const std::vector<std::string>& w) {
  std::vector<int> out;
  for (const auto& s : w) out.push_back(s == "x" ? bidi::kRemoved : std::atoi(s.c_str()));
  return out;
}

Tally runClassTests(const std::string& path) {
  Tally t;
  std::ifstream in(path);
  std::string line;
  int lineNo = 0;
  std::vector<int> expectLevels;
  std::vector<std::size_t> expectOrder;
  while (std::getline(in, line)) {
    ++lineNo;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (line.rfind("@Levels:", 0) == 0) {
      expectLevels = parseLevels(words(line.substr(8)));
      continue;
    }
    if (line.rfind("@Reorder:", 0) == 0) {
      expectOrder.clear();
      for (const auto& w : words(line.substr(9))) expectOrder.push_back(std::stoul(w));
      continue;
    }
    if (line[0] == '@') continue;
    const auto fields = split(line, ';');
    if (fields.size() < 2) continue;
    std::vector<bidi::BidiClass> types;
    bool ok = true;
    for (const auto& w : words(fields[0])) {
      bidi::BidiClass c;
      if (!bidi::classFromName(w, c)) {
        ok = false;
        break;
      }
      types.push_back(c);
    }
    if (!ok) {
      report(t, "BidiTest", lineNo, "unknown class in: " + fields[0]);
      continue;
    }
    const int bits = std::stoi(words(fields[1]).front(), nullptr, 16);
    const struct {
      int bit;
      bidi::Direction direction;
    } modes[] = {{1, bidi::Direction::Auto},
                 {2, bidi::Direction::LeftToRight},
                 {4, bidi::Direction::RightToLeft}};
    for (const auto& m : modes) {
      if (!(bits & m.bit)) continue;
      ++t.cases;
      const auto r = bidi::resolve(types, {}, {}, m.direction);
      const auto levels = bidi::lineLevels(r, 0, types.size());
      const auto order = bidi::visualOrder(r, 0, types.size());
      if (levels != expectLevels || order != expectOrder) {
        report(t, "BidiTest", lineNo,
               "[" + fields[0] + "] dir " + std::to_string(m.bit) + ": levels " +
                 join(levels, true) + " (want " + join(expectLevels, true) + "), order " +
                 join(order) + " (want " + join(expectOrder) + ")");
      }
    }
  }
  return t;
}

std::string utf8(const std::vector<char32_t>& cps, std::vector<std::size_t>& starts) {
  std::string out;
  for (char32_t c : cps) {
    starts.push_back(out.size());
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

// The renderer never calls resolve() directly: it hands UTF-8 to paragraphs()
// and draws lineRuns(). So every character test goes through that path as
// well, and the runs -- expanded back into characters, right-to-left ones
// reversed the way the shaper will reverse them -- must give Unicode's order.
bool runsGiveOrder(const std::vector<char32_t>& cps, bidi::Direction direction,
                   const std::vector<int>& wantLevels, const std::vector<std::size_t>& wantOrder,
                   std::string& got) {
  std::vector<std::size_t> starts;
  const std::string text = utf8(cps, starts);
  const auto paras = bidi::paragraphs(text, direction);
  if (paras.size() != 1) {
    got = std::to_string(paras.size()) + " paragraphs";
    return false;
  }
  std::vector<std::size_t> order;
  for (const auto& run : bidi::lineRuns(paras.front(), 0, text.size())) {
    std::vector<std::size_t> inRun;
    for (std::size_t i = 0; i < starts.size(); ++i) {
      if (starts[i] >= run.begin && starts[i] < run.end) inRun.push_back(i);
    }
    if (run.rightToLeft()) std::reverse(inRun.begin(), inRun.end());
    for (std::size_t i : inRun) {
      if (wantLevels[i] != bidi::kRemoved) order.push_back(i);
    }
  }
  got = join(order);
  return order == wantOrder;
}

Tally runCharacterTests(const std::string& path) {
  Tally t;
  std::ifstream in(path);
  std::string line;
  int lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const auto f = split(line, ';');
    if (f.size() < 5) continue;
    std::vector<char32_t> cps;
    for (const auto& w : words(f[0])) cps.push_back(static_cast<char32_t>(std::stoul(w, nullptr, 16)));
    std::vector<bidi::BidiClass> types;
    std::vector<bidi::BracketInfo> brackets;
    for (char32_t cp : cps) {
      types.push_back(bidi::classOf(cp));
      brackets.push_back(bidi::bracketOf(cp));
    }
    const int dir = std::atoi(f[1].c_str());
    const bidi::Direction direction = dir == 0   ? bidi::Direction::LeftToRight
                                      : dir == 1 ? bidi::Direction::RightToLeft
                                                 : bidi::Direction::Auto;
    const int wantParagraph = std::atoi(f[2].c_str());
    const auto wantLevels = parseLevels(words(f[3]));
    std::vector<std::size_t> wantOrder;
    for (const auto& w : words(f[4])) wantOrder.push_back(std::stoul(w));
    ++t.cases;
    const auto r = bidi::resolve(types, brackets, cps, direction);
    const auto levels = bidi::lineLevels(r, 0, cps.size());
    const auto order = bidi::visualOrder(r, 0, cps.size());
    if (r.paragraphLevel != wantParagraph || levels != wantLevels || order != wantOrder) {
      report(t, "BidiCharacterTest", lineNo,
             "[" + f[0] + "] dir " + f[1] + ": paragraph " + std::to_string(r.paragraphLevel) +
               " (want " + std::to_string(wantParagraph) + "), levels " + join(levels, true) +
               " (want " + join(wantLevels, true) + "), order " + join(order) + " (want " +
               join(wantOrder) + ")");
      continue;
    }
    std::string got;
    if (!runsGiveOrder(cps, direction, wantLevels, wantOrder, got)) {
      report(t, "BidiCharacterTest (UTF-8 runs)", lineNo,
             "[" + f[0] + "] dir " + f[1] + ": order " + got + " (want " + join(wantOrder) + ")");
    }
  }
  return t;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: deckboy-bidi-check <BidiTest.txt> <BidiCharacterTest.txt>\n";
    return 2;
  }
  const std::string tables = bidi::unicodeVersion();
  for (int i = 1; i <= 2; ++i) {
    const std::string v = versionOf(argv[i]);
    if (v != tables) {
      std::cerr << argv[i] << " is Unicode " << v << " but the tables are " << tables
                << ": regenerate with tools/gen_bidi_tables.py or fetch matching tests\n";
      return 2;
    }
  }
  const Tally a = runClassTests(argv[1]);
  const Tally b = runCharacterTests(argv[2]);
  std::cout << "Unicode " << tables << " bidi conformance\n";
  std::cout << "  BidiTest.txt          " << (a.cases - a.failures) << " / " << a.cases << " pass\n";
  std::cout << "  BidiCharacterTest.txt " << (b.cases - b.failures) << " / " << b.cases << " pass\n";
  if (a.cases == 0 || b.cases == 0) {
    std::cerr << "no test cases read -- wrong files?\n";
    return 1;
  }
  return (a.failures || b.failures) ? 1 : 0;
}
