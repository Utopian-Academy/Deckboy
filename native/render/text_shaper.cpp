// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// text_shaper.cpp -- see text_shaper.hpp.

#include "render/text_shaper.hpp"

#include <algorithm>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace deckboy::render::shaping {

namespace bidi = deckboy::core::bidi;
using bidi::BidiClass;

namespace {

// ── The registry ────────────────────────────────────────────────────────────
//
// One mutex for all of it. The UI thread and the engine's frame-text path each
// draw with their OWN faces, so what is shared is only this bookkeeping and the
// coverage probes -- and FreeType wants face creation serialised anyway.

struct FaceState {
  std::string path;
  float pointSize = 0.0f;
  // Parallel to the chain: the fallback faces opened at this face's size, or
  // null until a string first needs that one.
  std::vector<TTF_Font*> fallbacks;
  std::vector<bool> tried;
  // Which ASCII characters the face itself has. Nearly every string the desk
  // draws is ASCII, and this makes "does the primary cover it" one bit test.
  std::bitset<128> ascii;
  bool asciiKnown = false;
  // The chain these fallbacks were opened from. A chain change only bumps the
  // global generation; each face's own thread closes its stale fallbacks the
  // next time it asks, because another thread may be drawing with them now.
  unsigned generation = 0;
  // How much larger than the face's own point size its fallbacks are opened.
  // A point size is not a visual size: the pixel face's capitals fill nearly
  // the whole em where an ordinary face's fill seven tenths of it, so Arabic
  // or Japanese at the pixel face's size came out a third smaller than the
  // Latin beside it. Matched on the height of a capital H.
  float fallbackScale = 1.0f;
};

std::mutex gMutex;
std::vector<FallbackFont> gChain;
unsigned gGeneration = 1;
std::unordered_map<TTF_Font*, FaceState> gFaces;
// Coverage, asked of one small probe face per chain entry so that deciding
// whether a fallback has a character never opens it at every size.
std::vector<TTF_Font*> gProbes;
std::vector<bool> gProbeTried;
std::unordered_map<std::uint64_t, bool> gCoverage;
TTF_TextEngine* gEngine = nullptr;

std::atomic<long> gFastPath {0};
std::atomic<long> gItemised {0};

TTF_Font* openFace(const FallbackFont& f, float size) {
  SDL_PropertiesID props = SDL_CreateProperties();
  if (!props) return nullptr;
  SDL_SetStringProperty(props, TTF_PROP_FONT_CREATE_FILENAME_STRING, f.path.c_str());
  SDL_SetFloatProperty(props, TTF_PROP_FONT_CREATE_SIZE_FLOAT, size);
  SDL_SetNumberProperty(props, TTF_PROP_FONT_CREATE_FACE_NUMBER, f.faceIndex);
  TTF_Font* face = TTF_OpenFontWithProperties(props);
  SDL_DestroyProperties(props);
  if (face) {
    // The UI turns kerning off on its own faces (whole-pixel kerns split
    // words at these sizes); a fallback piece must measure the same way.
    TTF_SetFontKerning(face, false);
  }
  return face;
}

void closeFallbacksLocked(FaceState& s) {
  for (TTF_Font*& f : s.fallbacks) {
    if (f) TTF_CloseFont(f);
    f = nullptr;
  }
  s.fallbacks.assign(gChain.size(), nullptr);
  s.tried.assign(gChain.size(), false);
  s.generation = gGeneration;
}

void closeProbesLocked() {
  for (TTF_Font*& f : gProbes) {
    if (f) TTF_CloseFont(f);
    f = nullptr;
  }
  gProbes.assign(gChain.size(), nullptr);
  gProbeTried.assign(gChain.size(), false);
  gCoverage.clear();
}

bool chainHasLocked(std::size_t k, char32_t cp) {
  const std::uint64_t key = (static_cast<std::uint64_t>(k) << 32) | cp;
  if (auto at = gCoverage.find(key); at != gCoverage.end()) return at->second;
  if (!gProbeTried[k]) {
    gProbeTried[k] = true;
    gProbes[k] = openFace(gChain[k], 16.0f);
  }
  const bool has = gProbes[k] && TTF_FontHasGlyph(gProbes[k], cp);
  if (gCoverage.size() > 65536) gCoverage.clear();
  gCoverage.emplace(key, has);
  return has;
}

bool sameFile(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    char x = a[i];
    char y = b[i];
    if (x == '\\') x = '/';
    if (y == '\\') y = '/';
    if (std::tolower(static_cast<unsigned char>(x)) != std::tolower(static_cast<unsigned char>(y))) {
      return false;
    }
  }
  return true;
}

// The first fallback of `primary` that has cp, opened at the primary's size.
TTF_Font* fallbackFor(TTF_Font* primary, char32_t cp) {
  std::lock_guard<std::mutex> lock(gMutex);
  auto at = gFaces.find(primary);
  if (at == gFaces.end()) return nullptr;
  FaceState& s = at->second;
  if (s.generation != gGeneration) closeFallbacksLocked(s);
  for (std::size_t k = 0; k < gChain.size(); ++k) {
    if (sameFile(gChain[k].path, s.path)) continue;  // itself: it already said no
    if (!chainHasLocked(k, cp)) continue;
    if (!s.tried[k]) {
      s.tried[k] = true;
      s.fallbacks[k] = openFace(gChain[k], s.pointSize * s.fallbackScale);
    }
    if (s.fallbacks[k]) return s.fallbacks[k];
  }
  return nullptr;
}

bool primaryHas(TTF_Font* face, char32_t cp) {
  if (cp < 128) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto at = gFaces.find(face);
    if (at != gFaces.end()) {
      FaceState& s = at->second;
      if (!s.asciiKnown) {
        for (char32_t c = 0; c < 128; ++c) s.ascii[c] = c < 32 || TTF_FontHasGlyph(face, c);
        s.asciiKnown = true;
      }
      return s.ascii[cp];
    }
  }
  return cp < 32 || TTF_FontHasGlyph(face, cp);
}

TTF_TextEngine* engineLocked() {
  if (!gEngine) gEngine = TTF_CreateSurfaceTextEngine();
  return gEngine;
}

TTF_Direction ttfDirection(bool rtl) { return rtl ? TTF_DIRECTION_RTL : TTF_DIRECTION_LTR; }

// ── Pieces ──────────────────────────────────────────────────────────────────

struct Piece {
  TTF_Font* face = nullptr;
  std::size_t begin = 0;  // bytes, logical
  std::size_t end = 0;
  bool rtl = false;
  int w = 0;
  int h = 0;
  int top = 0;  // rows SDL_ttf added above the face's line box
  int x = 0;    // where it starts in the line
  bool spaces = false;  // nothing but whitespace: an advance, no ink
};

// Rows above the line box. SDL_ttf grows a line upward when a glyph rises above
// its face's ascent and reports that only through a text object's layout, so a
// piece that is taller than its face is asked; any other is known to be zero.
int pieceTop(TTF_Font* face, const char* s, std::size_t n, bool rtl, int h) {
  if (h <= TTF_GetFontHeight(face)) return 0;
  std::lock_guard<std::mutex> lock(gMutex);
  TTF_TextEngine* engine = engineLocked();
  if (!engine) return 0;
  TTF_Text* t = TTF_CreateText(engine, face, s, n);
  if (!t) return 0;
  TTF_SetTextDirection(t, ttfDirection(rtl));
  TTF_SubString sub {};
  int top = 0;
  if (TTF_GetTextSubString(t, 0, &sub)) top = sub.rect.y;
  TTF_DestroyText(t);
  return std::max(0, top);
}

void measurePiece(Piece& p, const char* text) {
  const TTF_Direction before = TTF_GetFontDirection(p.face);
  TTF_SetFontDirection(p.face, ttfDirection(p.rtl));
  int w = 0;
  int h = 0;
  if (!TTF_GetStringSize(p.face, text + p.begin, p.end - p.begin, &w, &h)) {
    w = 0;
    h = TTF_GetFontHeight(p.face);
  }
  p.w = w;
  p.h = h;
  p.top = pieceTop(p.face, text + p.begin, p.end - p.begin, p.rtl, h);
  TTF_SetFontDirection(p.face, before);
}

// Split one bidi run (logical byte range, one direction) by face.
//
// Letters choose their face by coverage -- the primary if it has them, then
// the fallback chain. Everything without a script of its own takes the face of
// the letters BESIDE it: spaces, punctuation and digits from the letters
// before them if that face has them, else from the letters after, so the space
// in front of a bracket is the same width as the one after the word, and an
// Arabic comma stays in the Arabic face. Marks, joiners and variation
// selectors always go with what they follow -- a ZWNJ inside a Persian word,
// or an accent split into another font, would stop doing its job.
void splitRun(TTF_Font* primary, const char* text, std::size_t begin, std::size_t end,
              bool rtl, std::vector<Piece>& out) {
  const std::string_view s(text, end);
  struct Char {
    std::size_t at;
    std::size_t len;
    char32_t cp;
    bool attaches;
    bool common;
    TTF_Font* face;
  };
  std::vector<Char> chars;
  for (std::size_t at = begin; at < end;) {
    char32_t cp = 0;
    const std::size_t len = bidi::decodeUtf8(s, at, cp);
    const BidiClass cls = bidi::classOf(cp);
    // The direction marks are strong for the bidi algorithm and nothing for
    // the eye: they go with their neighbours, never into a piece of their own.
    const bool mark = cp == 0x200E || cp == 0x200F || cp == 0x061C;
    const bool attaches = cls == BidiClass::NSM || cls == BidiClass::BN || mark;
    const bool common = !attaches && !(cls == BidiClass::L || cls == BidiClass::R ||
                                       cls == BidiClass::AL);
    chars.push_back({at, len, cp, attaches, common, nullptr});
    at += len;
  }
  auto byCoverage = [&](char32_t cp) -> TTF_Font* {
    if (primaryHas(primary, cp)) return primary;
    if (TTF_Font* fb = fallbackFor(primary, cp)) return fb;
    return primary;  // nobody has it: the primary's box
  };
  // Letters first.
  for (Char& c : chars) {
    if (!c.common && !c.attaches) c.face = byCoverage(c.cp);
  }
  // A WORD IN ONE FACE. The pixel face has most of the Latin alphabet but not
  // all of it, so a Kabyle word -- aɣbalu -- took its ɣ from Liberation and
  // every other letter from the pixel face: two faces, two styles, one word.
  // Where a word of an alphabetic script was split between faces and one of
  // them has every letter of it, the whole word is drawn in that one. Scripts
  // whose words run on without spaces are left as coverage put them.
  auto alphabetic = [](char32_t cp) {
    return (cp >= 0x41 && cp <= 0x2AF) || (cp >= 0x370 && cp <= 0x52F) ||
           (cp >= 0x1C80 && cp <= 0x1C8F) || (cp >= 0x1E00 && cp <= 0x1FFF) ||
           (cp >= 0x2C60 && cp <= 0x2C7F) || (cp >= 0x2DE0 && cp <= 0x2DFF) ||
           (cp >= 0xA640 && cp <= 0xA69F) || (cp >= 0xA720 && cp <= 0xA7FF) ||
           (cp >= 0xAB30 && cp <= 0xAB6F);
  };
  for (std::size_t i = 0; i < chars.size();) {
    if (chars[i].common) {
      ++i;
      continue;
    }
    std::size_t end = i;
    bool wordOfAlphabet = true;
    bool mixed = false;
    TTF_Font* first = nullptr;
    for (; end < chars.size() && !chars[end].common; ++end) {
      const Char& c = chars[end];
      if (c.attaches) continue;  // marks ride on their letter, below
      if (!alphabetic(c.cp)) wordOfAlphabet = false;
      if (!first) first = c.face;
      else if (c.face != first) mixed = true;
    }
    if (wordOfAlphabet && mixed) {
      for (std::size_t k = i; k < end; ++k) {
        TTF_Font* candidate = chars[k].face;
        if (chars[k].attaches || candidate == primary) continue;
        bool hasAll = true;
        for (std::size_t m = i; m < end && hasAll; ++m) {
          if (!chars[m].attaches && !primaryHas(candidate, chars[m].cp)) hasAll = false;
        }
        if (hasAll) {
          for (std::size_t m = i; m < end; ++m) {
            if (!chars[m].attaches) chars[m].face = candidate;
          }
          break;
        }
      }
    }
    i = end;
  }
  // Then what sits between them, from its neighbours.
  for (std::size_t i = 0; i < chars.size(); ++i) {
    Char& c = chars[i];
    if (!c.common) continue;
    TTF_Font* before = nullptr;
    for (std::size_t j = i; j-- > 0;) {
      if (!chars[j].common && !chars[j].attaches) {
        before = chars[j].face;
        break;
      }
    }
    TTF_Font* after = nullptr;
    for (std::size_t j = i + 1; j < chars.size(); ++j) {
      if (!chars[j].common && !chars[j].attaches) {
        after = chars[j].face;
        break;
      }
    }
    if (before && primaryHas(before, c.cp)) c.face = before;
    else if (after && primaryHas(after, c.cp)) c.face = after;
    else c.face = byCoverage(c.cp);
  }
  // Marks with whatever they follow.
  for (std::size_t i = 0; i < chars.size(); ++i) {
    Char& c = chars[i];
    if (!c.attaches) continue;
    if (i > 0) {
      c.face = chars[i - 1].face;
    } else {
      for (std::size_t j = 1; j < chars.size() && !c.face; ++j) c.face = chars[j].face;
      if (!c.face) c.face = byCoverage(c.cp);
    }
  }
  // Spaces at either end of a piece become pieces of their own, measured
  // left to right. SDL_ttf's right-to-left measurement leaves out the advance
  // of a space at the piece's visual right -- its logical start -- so the
  // space between "1405" and "(final)" in a Persian name simply vanished.
  auto isSpace = [](char32_t cp) {
    return cp == 0x20 || cp == 0x09 || cp == 0xA0 || cp == 0x3000 || cp == 0x202F ||
           (cp >= 0x2000 && cp <= 0x200A);
  };
  std::vector<Piece> pieces;
  for (std::size_t i = 0; i < chars.size(); ++i) {
    const Char& c = chars[i];
    const bool space = isSpace(c.cp);
    const bool startNew = pieces.empty() || pieces.back().face != c.face ||
                          pieces.back().spaces != space;
    if (startNew) {
      Piece p;
      p.face = c.face;
      p.begin = c.at;
      p.rtl = space ? false : rtl;
      p.spaces = space;
      pieces.push_back(p);
    }
    pieces.back().end = c.at + c.len;
  }
  // Spaces BETWEEN letters of one face stay in their word's piece: there the
  // glyphs on both sides carry the measurement, and splitting would cost the
  // shaper its context for nothing.
  std::vector<Piece> merged;
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    const bool inner = pieces[i].spaces && i > 0 && i + 1 < pieces.size() &&
                       !pieces[i - 1].spaces && !pieces[i + 1].spaces &&
                       pieces[i - 1].face == pieces[i].face &&
                       pieces[i + 1].face == pieces[i].face;
    if (inner) {
      merged.back().end = pieces[i + 1].end;
      ++i;
      continue;
    }
    merged.push_back(pieces[i]);
  }
  pieces.swap(merged);
  // A right-to-left run is laid out from its first piece at the RIGHT.
  if (rtl) std::reverse(pieces.begin(), pieces.end());
  out.insert(out.end(), pieces.begin(), pieces.end());
}

bool coveredByPrimary(TTF_Font* font, std::string_view s) {
  for (std::size_t at = 0; at < s.size();) {
    const unsigned char b = static_cast<unsigned char>(s[at]);
    if (b < 0x80) {
      if (!primaryHas(font, b)) return false;
      ++at;
      continue;
    }
    char32_t cp = 0;
    at += bidi::decodeUtf8(s, at, cp);
    if (!primaryHas(font, cp)) return false;
  }
  return true;
}

// The pieces of one line, left to right, measured and placed. `runs` are the
// line's bidi runs (already in visual order).
std::vector<Piece> layoutRuns(TTF_Font* font, const char* text, const std::vector<bidi::Run>& runs) {
  std::vector<Piece> pieces;
  for (const bidi::Run& run : runs) {
    if (run.end > run.begin) splitRun(font, text, run.begin, run.end, run.rightToLeft(), pieces);
  }
  int x = 0;
  for (Piece& p : pieces) {
    measurePiece(p, text);
    p.x = x;
    x += p.w;
  }
  return pieces;
}

// The fast path, or the whole machine, for one line of `text`.
std::vector<Piece> layoutLine(TTF_Font* font, const char* text, std::size_t length,
                              Direction direction) {
  const std::string_view s(text, length);
  if (direction != Direction::RightToLeft && !bidi::needsBidi(s) && coveredByPrimary(font, s)) {
    ++gFastPath;
    Piece p;
    p.face = font;
    p.begin = 0;
    p.end = length;
    measurePiece(p, text);
    return {p};
  }
  ++gItemised;
  return layoutRuns(font, text, bidi::visualRuns(s, direction));
}

// Line metrics shared by measuring and drawing: the baseline from the top of
// the composite, and the composite's height.
void lineBox(TTF_Font* font, const std::vector<Piece>& pieces, int& baseline, int& height) {
  const int ascent = TTF_GetFontAscent(font);
  baseline = ascent;
  for (const Piece& p : pieces) baseline = std::max(baseline, p.top + TTF_GetFontAscent(p.face));
  height = baseline - ascent + TTF_GetFontHeight(font);
  for (const Piece& p : pieces) {
    const int pieceBaseline = p.top + TTF_GetFontAscent(p.face);
    height = std::max(height, baseline - pieceBaseline + p.h);
  }
}

int lineWidth(const std::vector<Piece>& pieces) {
  return pieces.empty() ? 0 : pieces.back().x + pieces.back().w;
}

SDL_Surface* renderPieces(TTF_Font* font, const char* text, const std::vector<Piece>& pieces,
                          SDL_Color color, int* baselineOut) {
  if (pieces.size() == 1) {
    // One piece: SDL_ttf's own surface, untouched -- the old path exactly.
    const Piece& p = pieces.front();
    const TTF_Direction before = TTF_GetFontDirection(p.face);
    TTF_SetFontDirection(p.face, ttfDirection(p.rtl));
    SDL_Surface* s = TTF_RenderText_Blended(p.face, text + p.begin, p.end - p.begin, color);
    TTF_SetFontDirection(p.face, before);
    if (baselineOut) *baselineOut = p.top + TTF_GetFontAscent(p.face);
    return s;
  }
  int baseline = 0;
  int height = 0;
  lineBox(font, pieces, baseline, height);
  const int width = std::max(1, lineWidth(pieces));
  SDL_Surface* out = SDL_CreateSurface(width, std::max(1, height), SDL_PIXELFORMAT_ARGB8888);
  if (!out) return nullptr;
  SDL_FillSurfaceRect(out, nullptr, 0);
  const Uint32 rgb = (static_cast<Uint32>(color.r) << 16) | (static_cast<Uint32>(color.g) << 8) |
                     static_cast<Uint32>(color.b);
  for (const Piece& p : pieces) {
    if (p.end <= p.begin || p.spaces) continue;
    const TTF_Direction before = TTF_GetFontDirection(p.face);
    TTF_SetFontDirection(p.face, ttfDirection(p.rtl));
    // Rendered opaque-white-in-alpha and then written as coverage: every piece
    // is the one colour, and blending a straight-alpha surface onto a clear one
    // would darken its edges.
    SDL_Surface* glyphs = TTF_RenderText_Blended(p.face, text + p.begin, p.end - p.begin,
                                                 SDL_Color {255, 255, 255, color.a});
    TTF_SetFontDirection(p.face, before);
    if (!glyphs) continue;
    SDL_Surface* argb = glyphs;
    if (glyphs->format != SDL_PIXELFORMAT_ARGB8888) {
      argb = SDL_ConvertSurface(glyphs, SDL_PIXELFORMAT_ARGB8888);
      SDL_DestroySurface(glyphs);
      if (!argb) continue;
    }
    const int dy = baseline - (p.top + TTF_GetFontAscent(p.face));
    for (int y = 0; y < argb->h; ++y) {
      const int oy = dy + y;
      if (oy < 0 || oy >= out->h) continue;
      const Uint32* src = reinterpret_cast<const Uint32*>(
        static_cast<const Uint8*>(argb->pixels) + static_cast<std::size_t>(y) * argb->pitch);
      Uint32* dst = reinterpret_cast<Uint32*>(static_cast<Uint8*>(out->pixels) +
                                              static_cast<std::size_t>(oy) * out->pitch);
      for (int x = 0; x < argb->w; ++x) {
        const int ox = p.x + x;
        if (ox < 0 || ox >= out->w) continue;
        const Uint32 a = src[x] >> 24;
        if (a == 0) continue;
        const Uint32 have = dst[ox] >> 24;
        if (a > have) dst[ox] = (a << 24) | rgb;
      }
    }
    SDL_DestroySurface(argb);
  }
  if (baselineOut) *baselineOut = baseline;
  return out;
}

// Lay `src` onto `dst` at (x, y) by COVERAGE. Every surface here is one colour
// with its shape in the alpha, so the larger coverage wins. A plain copy would
// also copy the transparent margin of a tall line -- an Arabic line borrowing a
// taller face -- over the line above it and erase that line's letters.
void blitCoverage(SDL_Surface* src, SDL_Surface* dst, int x, int y) {
  SDL_Surface* argb = src;
  if (src->format != SDL_PIXELFORMAT_ARGB8888) {
    argb = SDL_ConvertSurface(src, SDL_PIXELFORMAT_ARGB8888);
    if (!argb) return;
  }
  for (int row = 0; row < argb->h; ++row) {
    const int oy = y + row;
    if (oy < 0 || oy >= dst->h) continue;
    const Uint32* from = reinterpret_cast<const Uint32*>(
      static_cast<const Uint8*>(argb->pixels) + static_cast<std::size_t>(row) * argb->pitch);
    Uint32* to = reinterpret_cast<Uint32*>(static_cast<Uint8*>(dst->pixels) +
                                           static_cast<std::size_t>(oy) * dst->pitch);
    for (int col = 0; col < argb->w; ++col) {
      const int ox = x + col;
      if (ox < 0 || ox >= dst->w) continue;
      if ((from[col] >> 24) > (to[ox] >> 24)) to[ox] = from[col];
    }
  }
  if (argb != src) SDL_DestroySurface(argb);
}

// ── Line breaking for wrapped text ───────────────────────────────────────────

bool isBreakSpace(char32_t cp) {
  return cp == 0x20 || cp == 0x3000 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) ||
         cp == 0x205F || cp == 0x09;
}

// Characters a line may break after even without a space: ideographs and
// kana, which East Asian text runs together.
bool breaksAnywhere(char32_t cp) {
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
         (cp >= 0x20000 && cp <= 0x3FFFF);
}

int measureSpan(TTF_Font* font, const char* text, std::size_t begin, std::size_t end,
                Direction direction) {
  if (end <= begin) return 0;
  const auto pieces = layoutLine(font, text + begin, end - begin, direction);
  return lineWidth(pieces);
}

struct Line {
  const bidi::Paragraph* paragraph = nullptr;
  std::size_t begin = 0;
  std::size_t end = 0;
};

std::vector<Line> breakLines(TTF_Font* font, const char* text,
                             const std::vector<bidi::Paragraph>& paragraphs, int wrapWidth) {
  std::vector<Line> lines;
  const std::string_view all(text, paragraphs.empty() ? 0 : paragraphs.back().byteEnd);
  for (const bidi::Paragraph& p : paragraphs) {
    // The separator ending a paragraph is not drawn.
    std::size_t end = p.byteEnd;
    while (end > p.byteBegin && (all[end - 1] == '\n' || all[end - 1] == '\r')) --end;
    const Direction dir = p.rightToLeft() ? Direction::RightToLeft : Direction::LeftToRight;
    std::size_t start = p.byteBegin;
    if (wrapWidth <= 0 || start >= end) {
      lines.push_back({&p, start, end});
      continue;
    }
    while (start < end) {
      // Greedy: extend to the last break opportunity that still fits.
      std::size_t lastFit = start;
      std::size_t at = start;
      while (at < end) {
        char32_t cp = 0;
        const std::size_t len = bidi::decodeUtf8(all, at, cp);
        const std::size_t next = at + len;
        const bool opportunity = isBreakSpace(cp) || breaksAnywhere(cp) || next == end;
        if (opportunity) {
          // Trailing spaces hang: they do not count against the width.
          std::size_t trimmed = next;
          while (trimmed > start && isBreakSpace(static_cast<unsigned char>(all[trimmed - 1]))) --trimmed;
          if (measureSpan(font, text, start, trimmed, dir) <= wrapWidth) {
            lastFit = next;
          } else {
            break;
          }
        }
        at = next;
      }
      if (lastFit == start) {
        // One word wider than the line: cut it where it stops fitting, by
        // characters, so it is shown rather than lost.
        std::size_t cut = start;
        std::size_t at2 = start;
        while (at2 < end) {
          char32_t cp = 0;
          const std::size_t next = at2 + bidi::decodeUtf8(all, at2, cp);
          if (cut > start && measureSpan(font, text, start, next, dir) > wrapWidth) break;
          cut = next;
          at2 = next;
        }
        lastFit = std::max(cut, start + 1);
      }
      std::size_t lineEnd = lastFit;
      while (lineEnd > start && isBreakSpace(static_cast<unsigned char>(all[lineEnd - 1]))) --lineEnd;
      lines.push_back({&p, start, lineEnd});
      start = lastFit;
    }
  }
  return lines;
}

}  // namespace

// ── Public ──────────────────────────────────────────────────────────────────

void setFallbackChain(std::vector<FallbackFont> chain) {
  std::lock_guard<std::mutex> lock(gMutex);
  const bool same = chain.size() == gChain.size() &&
                    std::equal(chain.begin(), chain.end(), gChain.begin(),
                               [](const FallbackFont& a, const FallbackFont& b) {
                                 return a.path == b.path && a.faceIndex == b.faceIndex;
                               });
  if (same) return;  // a UI-scale reload asks again with the same list
  gChain = std::move(chain);
  ++gGeneration;
  // The probes are only ever touched under this lock, so they can go now.
  closeProbesLocked();
}

std::vector<FallbackFont> fallbackChain() {
  std::lock_guard<std::mutex> lock(gMutex);
  return gChain;
}

void registerFace(TTF_Font* face, const std::string& path, float pointSize) {
  if (!face) return;
  // 0.716 is the cap height of an ordinary sans (Arial, Liberation): a face
  // whose capitals are that tall needs no correction, the pixel face's do.
  float scale = 1.0f;
  int minx = 0;
  int maxx = 0;
  int miny = 0;
  int maxy = 0;
  int advance = 0;
  if (pointSize > 0.0f && TTF_FontHasGlyph(face, 'H') &&
      TTF_GetGlyphMetrics(face, 'H', &minx, &maxx, &miny, &maxy, &advance) && maxy > 0) {
    scale = std::clamp((static_cast<float>(maxy) / pointSize) / 0.716f, 0.75f, 1.6f);
  }
  std::lock_guard<std::mutex> lock(gMutex);
  FaceState& s = gFaces[face];
  for (TTF_Font*& f : s.fallbacks) {
    if (f) TTF_CloseFont(f);
    f = nullptr;
  }
  s.path = path;
  s.pointSize = pointSize;
  s.fallbacks.assign(gChain.size(), nullptr);
  s.tried.assign(gChain.size(), false);
  s.generation = gGeneration;
  s.fallbackScale = scale;
  s.asciiKnown = false;
}

void forgetFace(TTF_Font* face) {
  if (!face) return;
  std::lock_guard<std::mutex> lock(gMutex);
  auto at = gFaces.find(face);
  if (at == gFaces.end()) return;
  for (TTF_Font* f : at->second.fallbacks) {
    if (f) TTF_CloseFont(f);
  }
  gFaces.erase(at);
}

void forgetAllFaces() {
  std::lock_guard<std::mutex> lock(gMutex);
  for (auto& [face, s] : gFaces) {
    for (TTF_Font* f : s.fallbacks) {
      if (f) TTF_CloseFont(f);
    }
  }
  gFaces.clear();
}

bool textSize(TTF_Font* font, const char* text, std::size_t length, int* w, int* h,
              Direction direction) {
  if (w) *w = 0;
  if (h) *h = 0;
  if (!font || !text) return false;
  if (length == 0) length = std::strlen(text);
  if (length == 0) {
    if (h) *h = TTF_GetFontHeight(font);
    return true;
  }
  const auto pieces = layoutLine(font, text, length, direction);
  if (w) *w = lineWidth(pieces);
  if (h) {
    int baseline = 0;
    int height = 0;
    lineBox(font, pieces, baseline, height);
    *h = (pieces.size() == 1) ? pieces.front().h : height;
  }
  return true;
}

SDL_Surface* renderText(TTF_Font* font, const char* text, std::size_t length, SDL_Color color,
                        int* baseline, Direction direction) {
  if (!font || !text) return nullptr;
  if (length == 0) length = std::strlen(text);
  if (length == 0) return nullptr;
  const auto pieces = layoutLine(font, text, length, direction);
  return renderPieces(font, text, pieces, color, baseline);
}

bool textSizeWrapped(TTF_Font* font, const char* text, std::size_t length, int wrapWidth,
                     int* w, int* h, Direction direction) {
  if (w) *w = 0;
  if (h) *h = 0;
  if (!font || !text) return false;
  if (length == 0) length = std::strlen(text);
  const auto paragraphs = bidi::paragraphs(std::string_view(text, length), direction);
  const auto lines = breakLines(font, text, paragraphs, wrapWidth);
  int widest = 0;
  for (const Line& line : lines) {
    const auto pieces = layoutRuns(font, text, bidi::lineRuns(*line.paragraph, line.begin, line.end));
    widest = std::max(widest, lineWidth(pieces));
  }
  if (w) *w = widest;
  if (h) *h = static_cast<int>(lines.size()) * TTF_GetFontLineSkip(font);
  return true;
}

SDL_Surface* renderTextWrapped(TTF_Font* font, const char* text, std::size_t length,
                               SDL_Color color, int wrapWidth, Align align, Direction direction) {
  if (!font || !text) return nullptr;
  if (length == 0) length = std::strlen(text);
  if (length == 0) return nullptr;
  const auto paragraphs = bidi::paragraphs(std::string_view(text, length), direction);
  const auto lines = breakLines(font, text, paragraphs, wrapWidth);
  struct Drawn {
    SDL_Surface* surface;
    int baseline;
    bool rtl;
  };
  std::vector<Drawn> drawn;
  int widest = 0;
  for (const Line& line : lines) {
    const auto pieces = layoutRuns(font, text, bidi::lineRuns(*line.paragraph, line.begin, line.end));
    int baseline = TTF_GetFontAscent(font);
    SDL_Surface* s = pieces.empty() ? nullptr : renderPieces(font, text, pieces, color, &baseline);
    widest = std::max(widest, s ? s->w : 0);
    drawn.push_back({s, baseline, line.paragraph->rightToLeft()});
  }
  const int skip = TTF_GetFontLineSkip(font);
  const int ascent = TTF_GetFontAscent(font);
  const int width = std::max(widest, 1);
  // Every line's baseline is a line skip below the last. A line that borrowed
  // a taller face reaches above its own box, and the first one would reach
  // above the surface: everything moves down by that much rather than lose it.
  int lift = 0;
  for (std::size_t i = 0; i < drawn.size(); ++i) {
    if (!drawn[i].surface) continue;
    lift = std::max(lift, drawn[i].baseline - ascent - static_cast<int>(i) * skip);
  }
  auto topOf = [&](std::size_t i) {
    return lift + static_cast<int>(i) * skip + ascent - drawn[i].baseline;
  };
  int height = std::max(1, lift + static_cast<int>(drawn.size()) * skip);
  // ... and below: Arabic descenders hang under the last line's box.
  for (std::size_t i = 0; i < drawn.size(); ++i) {
    if (drawn[i].surface) height = std::max(height, topOf(i) + drawn[i].surface->h);
  }
  SDL_Surface* out = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
  if (out) {
    SDL_FillSurfaceRect(out, nullptr, 0);
    for (std::size_t i = 0; i < drawn.size(); ++i) {
      SDL_Surface* s = drawn[i].surface;
      if (!s) continue;
      int x = 0;
      const bool startsRight = drawn[i].rtl;
      if (align == Align::Centre) x = (width - s->w) / 2;
      else if ((align == Align::Start) == startsRight) x = width - s->w;
      blitCoverage(s, out, x, topOf(i));
    }
  }
  for (const Drawn& d : drawn) {
    if (d.surface) SDL_DestroySurface(d.surface);
  }
  return out;
}

int caretX(TTF_Font* font, std::string_view text, std::size_t offset, Direction direction) {
  if (!font || text.empty()) return 0;
  offset = std::min(offset, text.size());
  const std::string owned(text);
  const auto pieces = layoutLine(font, owned.c_str(), owned.size(), direction);
  if (pieces.empty()) return 0;
  // The piece holding the character before the caret -- or, at the very start,
  // the one holding the first character.
  const Piece* hit = nullptr;
  for (const Piece& p : pieces) {
    if (offset > p.begin && offset <= p.end) {
      hit = &p;
      break;
    }
  }
  if (!hit) {
    for (const Piece& p : pieces) {
      if (p.begin == offset) {
        hit = &p;
        break;
      }
    }
  }
  if (!hit) return offset == 0 ? 0 : lineWidth(pieces);
  int partial = 0;
  if (offset > hit->begin) {
    const TTF_Direction before = TTF_GetFontDirection(hit->face);
    TTF_SetFontDirection(hit->face, ttfDirection(hit->rtl));
    TTF_GetStringSize(hit->face, owned.c_str() + hit->begin, offset - hit->begin, &partial, nullptr);
    TTF_SetFontDirection(hit->face, before);
  }
  return hit->rtl ? hit->x + hit->w - partial : hit->x + partial;
}

std::size_t offsetAtX(TTF_Font* font, std::string_view text, int x, Direction direction) {
  if (!font || text.empty()) return 0;
  const std::string owned(text);
  const auto pieces = layoutLine(font, owned.c_str(), owned.size(), direction);
  if (pieces.empty()) return 0;
  const Piece* hit = &pieces.front();
  for (const Piece& p : pieces) {
    if (x >= p.x) hit = &p;
  }
  // Every character boundary in the piece, and the one whose caret lands
  // nearest x.
  std::size_t best = hit->begin;
  int bestDistance = 1 << 30;
  for (std::size_t at = hit->begin; at <= hit->end;) {
    const int cx = caretX(font, owned, at, direction);
    const int d = std::abs(cx - x);
    if (d < bestDistance) {
      bestDistance = d;
      best = at;
    }
    if (at == hit->end) break;
    char32_t cp = 0;
    at += bidi::decodeUtf8(owned, at, cp);
  }
  return best;
}

bool covers(TTF_Font* font, std::string_view text, std::vector<char32_t>* missing) {
  if (!font) return false;
  bool all = true;
  for (std::size_t at = 0; at < text.size();) {
    char32_t cp = 0;
    at += bidi::decodeUtf8(text, at, cp);
    const BidiClass cls = bidi::classOf(cp);
    if (cls == BidiClass::WS || cls == BidiClass::BN || cls == BidiClass::B ||
        cls == BidiClass::S) {
      continue;
    }
    if (primaryHas(font, cp) || fallbackFor(font, cp)) continue;
    all = false;
    if (missing) missing->push_back(cp);
  }
  return all;
}

bool resolvesRightToLeft(std::string_view text) { return bidi::firstStrongIsRightToLeft(text); }

Stats stats() {
  Stats s;
  s.fastPath = gFastPath.load();
  s.itemised = gItemised.load();
  std::lock_guard<std::mutex> lock(gMutex);
  for (const auto& [face, st] : gFaces) {
    for (TTF_Font* f : st.fallbacks) {
      if (f) ++s.openFallbacks;
    }
  }
  return s;
}

}  // namespace deckboy::render::shaping
