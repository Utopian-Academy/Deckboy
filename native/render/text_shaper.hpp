// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// text_shaper.hpp -- every string Deckboy draws or measures goes through here.
//
// SDL_ttf shapes one run of text in one font and one direction. Real text is
// rarely that: a Persian lower third carries a Latin name and a year, an English
// desk shows a cue someone named in Japanese, an Arabic label holds "95%". This
// layer cuts a string into the pieces SDL_ttf can do -- by direction first
// (core/bidi, the full Unicode algorithm), then by the font that actually has the
// glyphs -- shapes each piece in its own font and direction, and puts them back
// together on one baseline.
//
// WHY NOT SDL_ttf's OWN FALLBACK FONTS. TTF_AddFallbackFont does reshape the
// missing spans with the fallback face, which keeps Arabic joined, but it puts
// each fallback glyph on its OWN font's ascent rather than the line's -- so a
// Japanese title beside Latin text sat a tenth of an em low, and Arabic further.
// Doing the itemising here keeps every piece on one baseline, and lets the
// fallback faces be opened lazily, only when a string needs them.
//
// A string that is plain left-to-right text the primary font covers -- every
// label of the English interface -- takes the old single-call path unchanged.
//
// tools/audit_text_shaping.py keeps it that way: it fails the build if any
// TTF_RenderText* or TTF_GetStringSize appears outside this file.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/bidi.hpp"
#include "core/sdl_compat.hpp"
#include <SDL3_ttf/SDL_ttf.h>

namespace deckboy::render::shaping {

using deckboy::core::bidi::Direction;

// A font file the shaper may fall back to, and which face in it (for a .ttc).
struct FallbackFont {
  std::string path;
  int faceIndex = 0;
};

// The ordered fallback list, shared by every face. Changing it closes every
// fallback face opened so far; the next string that needs one opens it again.
void setFallbackChain(std::vector<FallbackFont> chain);
std::vector<FallbackFont> fallbackChain();

// Tell the shaper where a face came from, so its fallbacks can be opened at the
// same size and its own file left out of its chain. A face nobody registered
// still works -- it just has no fallbacks.
void registerFace(TTF_Font* face, const std::string& path, float pointSize);
// MUST be called before TTF_CloseFont on a registered face: it closes the
// fallback faces opened for it and forgets cached layouts keyed on its address,
// which the allocator will hand to the next font that is opened.
void forgetFace(TTF_Font* face);
// Every fallback face opened for anything, closed (fonts are being reloaded).
void forgetAllFaces();

// ── One line ────────────────────────────────────────────────────────────────

// Width and height of the line as drawn. Same contract as TTF_GetStringSize:
// `length` 0 means NUL-terminated.
bool textSize(TTF_Font* font, const char* text, std::size_t length, int* w, int* h,
              Direction direction = Direction::Auto);

// The line as an ARGB8888 surface, blended, in `color`. `baseline`, if given,
// receives the baseline's distance from the surface's top, which is the font's
// ascent unless a glyph from a taller fallback face pushed the top up.
SDL_Surface* renderText(TTF_Font* font, const char* text, std::size_t length,
                        SDL_Color color, int* baseline = nullptr,
                        Direction direction = Direction::Auto);

// ── Wrapped text ────────────────────────────────────────────────────────────

enum class Align { Start, Centre, End };

// Paragraphs (split on newlines) broken into lines no wider than `wrapWidth`,
// each line laid out by the bidi algorithm -- resolved per paragraph and
// reordered per line, as UAX #9 requires. Start/End follow each paragraph's
// own direction, so an Arabic subtitle line starts at the right.
SDL_Surface* renderTextWrapped(TTF_Font* font, const char* text, std::size_t length,
                               SDL_Color color, int wrapWidth, Align align = Align::Start,
                               Direction direction = Direction::Auto);
bool textSizeWrapped(TTF_Font* font, const char* text, std::size_t length, int wrapWidth,
                     int* w, int* h, Direction direction = Direction::Auto);

// ── Editing ─────────────────────────────────────────────────────────────────

// Where a caret at byte `offset` of `text` is drawn, in pixels from the left of
// the line as renderText draws it. Right-to-left text puts it on the other side
// of the character, which is what a reader of it expects.
int caretX(TTF_Font* font, std::string_view text, std::size_t offset,
           Direction direction = Direction::Auto);
// The byte offset nearest to x pixels from the left of the drawn line.
std::size_t offsetAtX(TTF_Font* font, std::string_view text, int x,
                      Direction direction = Direction::Auto);

// Whether every character of `text` has a face to draw it -- `font` itself or
// one of its fallbacks -- and, if not, which are missing. Spaces and joiners
// draw nothing and are not asked about. For --self-check's "what can this
// machine draw".
bool covers(TTF_Font* font, std::string_view text, std::vector<char32_t>* missing = nullptr);

// The paragraph direction the text resolves to (UAX #9 rules P2 and P3).
bool resolvesRightToLeft(std::string_view text);

// Diagnostics for --self-check and the smoke test: how many fallback faces are
// open, and how many strings needed more than the primary font.
struct Stats {
  long fastPath = 0;
  long itemised = 0;
  int openFallbacks = 0;
};
Stats stats();

}  // namespace deckboy::render::shaping
