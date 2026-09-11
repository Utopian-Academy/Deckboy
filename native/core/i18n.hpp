/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Deckboy — Interface language
 * Copyright (C) 2026 Deckboy Contributors
 */

// ============================================================================
// i18n.hpp — what the interface says, in the operator's language.
//
// TWO MECHANISMS, ONE DOOR.
//
//   A CATALOGUE is a real translation: data/lang/<code>.tsv, one line of
//   `english<TAB>translation`. A line nobody translated falls through to the
//   English, so a half-finished catalogue is a partly-translated app rather
//   than an app with holes in it. That property is load-bearing -- a missing
//   string on a panic button is worse than an English one.
//
//   A CYPHER is a transform of the English text, applied character by
//   character. It needs no catalogue at all and therefore covers every string
//   in the program, including ones written after it: ROT13, Atbash, leet,
//   Morse. These exist because they are funny, and because they are exact --
//   a cypher cannot mistranslate.
//
// WHERE IT IS APPLIED: inside the four functions that actually rasterise text
// -- drawTextSafe, drawCenteredTextSafe, drawText and drawCenteredTextUnclipped
// -- rather than at the ~1,300 call sites. drawCenteredText forwards into the
// second of those and so is covered without knowing anything about this. That is the whole reason this is
// tractable. English is a pass-through with an early-out, so the default costs
// a bool test per drawn string and nothing else.
//
// THE TRADE THIS MAKES, stated plainly because it is a real one. Translating
// at the draw call means the catalogue is consulted for EVERY string drawn,
// including the operator's own: a cue named exactly "LIVE" would come out
// "EN VIVO" in Spanish. Composite labels cannot collide -- a playlist row is an
// index, a name and a duration together -- so this needs a cue named exactly
// like a piece of chrome, and it is cosmetic when it happens.
//
// The alternative is marking each of the ~1,300 call sites by hand, which
// trades a rare cosmetic oddity for 1,300 chances to miss one. Under a cypher
// the same behaviour is not a flaw at all: transforming the operator's own text
// is the entire point.
//
// WHAT IS DELIBERATELY NOT TRANSLATED: the trade's own vocabulary. TAKE, CUE,
// PROGRAM, PREVIEW, NDI, SRT, tally, DeckLink -- broadcast software is not
// localised for these in any language, and an operator who learned the desk in
// one country must be able to work it in another. Catalogues leave them alone;
// that is a choice, not an omission.
//
// SCRIPT SUPPORT IS A FONT QUESTION. The bundled Liberation faces cover Latin
// with every accent, all of Cyrillic and most of Greek -- so those languages
// work today. They contain no CJK, no Braille, no Arabic and no runes, so
// those languages cannot be shipped until a font that has them is bundled;
// listing them anyway would render a wall of empty boxes. See availableLanguages.
// ============================================================================

#ifndef DECKBOY_CORE_I18N_HPP
#define DECKBOY_CORE_I18N_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace deckboy {
namespace core {
namespace i18n {

struct LanguageInfo {
  std::string code;      // "es", "es-CU", "tlh", "cy-rot13"
  std::string name;      // what to show in the picker, in its own language
  bool cypher = false;   // true = a transform, not a catalogue
};

// Every language this build can actually draw: the built-in cyphers plus every
// catalogue found in <dataDir>/lang. Sorted with English first, then real
// languages, then the cyphers, because the cyphers are a novelty and should
// not sit between two languages somebody is looking for.
std::vector<LanguageInfo> availableLanguages(const std::filesystem::path& dataDir);

// Switch the interface language. An unknown code, or a catalogue that will not
// load, leaves the previous language in place and reports why -- silently
// falling back to English would look exactly like the setting not saving.
bool setLanguage(const std::string& code, const std::filesystem::path& dataDir,
                 std::string& error);

// The active code, and its display name.
const std::string& activeCode();
const std::string& activeName();

// THE FACE THIS LANGUAGE NEEDS, or empty for the bundled ones.
//
// Some writing systems are not a translation problem, they are a font problem:
// the bundled Liberation faces have no CJK, no Devanagari, no Arabic, and
// certainly no Alienese. A catalogue can name the file it wants with a
// `#font <name>` line and a cypher can carry one too; the loader looks for it
// in data/fonts and falls back to the bundled face if it is not there.
//
// Declared here rather than hardcoded in the font loader so that adding a
// script is a matter of editing a .tsv -- no code, no rebuild.
//
// A LIST, tried in order, because the answer is different on every platform and
// most of the time it is already installed. Japanese is Yu Gothic on Windows,
// Hiragino on macOS and Noto on Linux; naming all three means Deckboy reads
// Japanese on all three without shipping sixteen megabytes of glyphs that the
// operating system already has. A name with no directory is looked for in
// data/fonts first and then among the system faces, so a bundled file still
// wins when somebody wants an exact look.
std::vector<std::string> activeFontCandidates();

// Does this language need a face we could not find? True means its text will
// draw as empty boxes, which callers that can warn, should.
bool activeFontMissing();

// IS THIS LANGUAGE WRITTEN RIGHT TO LEFT? Declared by the catalogue with an
// `#rtl 1` line.
//
// Direction is only half of what such a script needs. The other half is
// SHAPING: in Arabic a letter has a different form at the start, middle and end
// of a word, and joining them is a job for HarfBuzz, which SDL_ttf only does if
// it was built against it. Reversing unshaped glyphs produces something that
// looks like Arabic to someone who does not read it and is wrong to everyone
// who does -- so the two are reported separately and a language that needs
// shaping is not offered unless shaping is actually present.
bool activeIsRtl();

// Whether the renderer could actually honour that direction. Set by the font
// loader, which is the only thing that knows what SDL_ttf accepted.
void noteRtlSupported(bool supported);
bool rtlSupported();

// CAN THIS BUILD SHAPE AT ALL? Probed once at startup by asking SDL_ttf to set
// a right-to-left direction and seeing whether it agrees.
//
// A build without HarfBuzz refuses, and that refusal is the difference between
// offering Arabic and offering a row of unjoined letters in the wrong order.
// availableLanguages hides every right-to-left catalogue while this is false,
// so a language is never listed that cannot be drawn as its readers write it.
void noteShapingAvailable(bool available);
bool shapingAvailable();

// Right-to-left catalogues present but hidden for want of shaping. Empty when
// there are none or when shaping works -- so --self-check can say WHY a
// language somebody expected is not in the list.
std::vector<std::string> languagesAwaitingShaping(const std::filesystem::path& dataDir);

// Records what the loader actually managed to open, so activeFontMissing can
// tell "asked for nothing" from "asked and did not get it".
void noteFontResolved(bool found);

// True when nothing has to happen -- English, or no catalogue loaded. Every
// caller checks this first so the default path does no work at all.
bool passthrough();

// The operator-facing form of `source`. Returns `source` itself when there is
// nothing to do, so this is safe to call on anything, including text that is
// already a filename, a number or a cue name.
std::string translate(const std::string& source);

}  // namespace i18n
}  // namespace core
}  // namespace deckboy

#endif  // DECKBOY_CORE_I18N_HPP
