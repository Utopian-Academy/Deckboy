// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// bidi.hpp -- the Unicode Bidirectional Algorithm (UAX #9), in full.
//
// WHY THIS EXISTS. Arabic, Persian and Hebrew run right to left, and the text
// an operator types or a catalogue carries is rarely one direction only: a
// lower third reads "MARYAM 2026", a label reads "volume 95%", a file is called
// "promo_v2 (final)". Shaping (joining the letters) is HarfBuzz's job and SDL_ttf
// does it; putting the pieces in the right ORDER is this file's. Before it, a
// whole label was flipped one way or the other, which turned the percentage in
// an Arabic volume label into "%59".
//
// It is the whole algorithm, not the easy part of it: explicit embeddings,
// overrides and isolates (X1-X10), weak and neutral resolution with paired
// brackets (W1-W7, N0-N2), implicit levels (I1-I2) and line reordering (L1-L2).
// tools/bidi_check.cpp runs Unicode's own conformance files against it --
// BidiTest.txt and BidiCharacterTest.txt, about 770,000 cases -- and CI runs
// that on every push. The character data is generated from the Unicode
// Character Database by tools/gen_bidi_tables.py, never typed in.
//
// Mirroring (L4) and combining marks (L3) are left to the shaper, which does
// them per run: HarfBuzz mirrors brackets in a right-to-left run itself.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace deckboy::core::bidi {

enum class BidiClass : std::uint8_t {
  L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON,
  LRE, LRO, RLE, RLO, PDF, LRI, RLI, FSI, PDI
};

// What the paragraph's base direction should be.
enum class Direction { Auto, LeftToRight, RightToLeft };

struct BracketInfo {
  char32_t pairedWith = 0;  // 0 = not a paired bracket
  bool opening = false;
};

BidiClass classOf(char32_t codePoint);
BracketInfo bracketOf(char32_t codePoint);
const char* unicodeVersion();
// "L", "AL", "PDI" -- for the conformance check and diagnostics.
const char* className(BidiClass c);
bool classFromName(std::string_view name, BidiClass& out);

// An element removed by rule X9 (embedding controls, BN) has no level.
constexpr int kRemoved = -1;

// One paragraph, resolved through rule I2.
struct Resolution {
  int paragraphLevel = 0;
  std::vector<BidiClass> originalTypes;
  std::vector<int> levels;  // kRemoved for X9 removals
};

// The algorithm proper, on bidi classes. `brackets` and `codePoints` may be
// empty (no paired brackets, which is what BidiTest.txt assumes); otherwise
// they are the same length as `types`. The input is ONE paragraph: rule P1 is
// the caller's (see paragraphs() below).
Resolution resolve(const std::vector<BidiClass>& types,
                   const std::vector<BracketInfo>& brackets,
                   const std::vector<char32_t>& codePoints,
                   Direction direction);

// Rule L1 for the line [begin, end) of a resolution: its levels, with
// separators and trailing whitespace returned to the paragraph level.
std::vector<int> lineLevels(const Resolution& r, std::size_t begin, std::size_t end);

// Rule L2: the line's element indices in visual order, left to right.
// Elements removed by X9 are left out.
std::vector<std::size_t> visualOrder(const Resolution& r, std::size_t begin,
                                     std::size_t end);

// ── UTF-8 text ──────────────────────────────────────────────────────────────

// A span of the text that runs one way: [begin, end) in BYTES of the original
// string, in logical order, at one embedding level. Odd levels run right to
// left and are shaped that way; the run's characters stay in logical order --
// reversing them is the shaper's job, and doing it here as well would undo it.
struct Run {
  std::size_t begin = 0;
  std::size_t end = 0;
  int level = 0;
  bool rightToLeft() const { return (level & 1) != 0; }
};

// A paragraph of UTF-8, decoded and resolved. Kept so a caller that wraps text
// can resolve once and reorder line by line, which is what UAX #9 requires --
// resolving each line as a paragraph of its own gives different answers at the
// line ends.
struct Paragraph {
  std::size_t byteBegin = 0;      // where it starts in the caller's string
  std::size_t byteEnd = 0;
  std::vector<std::size_t> offsets;  // byte offset of each code point, plus the end
  Resolution resolution;
  bool rightToLeft() const { return (resolution.paragraphLevel & 1) != 0; }
};

// Rule P1: one Paragraph per paragraph separator in `text`, each resolved.
std::vector<Paragraph> paragraphs(std::string_view text, Direction direction);

// The runs of a line, left to right on screen. [byteBegin, byteEnd) must lie
// inside the paragraph; the runs' offsets are into the caller's string.
// Characters removed by X9 (zero-width joiners, embedding controls) are kept,
// attached to a neighbour's run, so a ZWNJ inside a Persian word stays inside
// the run HarfBuzz shapes and still does its job.
std::vector<Run> lineRuns(const Paragraph& p, std::size_t byteBegin, std::size_t byteEnd);

// The whole of a single-line string as runs, left to right. Paragraphs, if the
// string has more than one, follow each other left to right.
std::vector<Run> visualRuns(std::string_view text, Direction direction = Direction::Auto,
                            bool* paragraphRightToLeft = nullptr);

// Does any character need right-to-left handling? False means the string is
// left-to-right throughout under Direction::Auto, and a caller can skip the
// algorithm entirely -- which is the case for nearly every label in an English
// interface, and why it is checked first.
bool needsBidi(std::string_view text);

// The direction a paragraph starts in (rules P2 and P3).
bool firstStrongIsRightToLeft(std::string_view text);

// Decode one UTF-8 code point at `at`; returns its length in bytes (at least 1
// for any byte, so a caller always advances). Malformed input is U+FFFD.
std::size_t decodeUtf8(std::string_view text, std::size_t at, char32_t& out);

}  // namespace deckboy::core::bidi
