/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Deckboy — Interface language
 * Copyright (C) 2026 Deckboy Contributors
 */

#include "i18n.hpp"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace fs = dbfs;

namespace deckboy {
namespace core {
namespace i18n {

namespace {

// ── State ───────────────────────────────────────────────────────────────────
//
// Catalogues are never freed. translate() hands back std::strings so nothing
// outlives a call, but keeping them also means switching back to a language
// already used costs nothing, and a show that flips language mid-run does not
// hit the disk again.
std::map<std::string, std::unordered_map<std::string, std::string>> gCatalogues;
const std::unordered_map<std::string, std::string>* gActive = nullptr;
// The same catalogue keyed in lower case, so one entry covers Apply and
// APPLY. Rebuilt whenever gActive is. See translate() for why the value
// comes back as written rather than re-cased.
std::unordered_map<std::string, std::string> gActiveLower;
std::string gCode = "en";
std::string gName = "English";
std::string gFontFile;      // empty = the bundled face is fine
bool gFontMissing = false;  // asked for a face that is not installed
bool gSymbols = false;      // a cypher's face was found: write its own symbols
bool gRtl = false;          // catalogue said #rtl 1
bool gRtlOk = false;        // the renderer accepted that direction
bool gShaping = false;      // this build can shape/reorder at all
int gCypher = 0;   // 0 = none; see kCyphers
void clearComposed();   // beside translate(): the memo of composed strings

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

// ── Cyphers ─────────────────────────────────────────────────────────────────
//
// ASCII in, ASCII out, every one of them. That is not a limitation, it is the
// selection rule: the bundled fonts have no Braille block and no runes, so a
// cypher that reached for those would draw a wall of empty boxes. Alienese II
// steps outside it only when it can: its symbols come from a face Deckboy
// ships, and until the loader has found that face it writes Latin letters,
// which read. These six render on any build, on any platform, in any theme.
//
// They also transform whatever they are given, including cue names and file
// paths. That is intended -- "the whole desk is in runes" is the joke, and a
// cypher that politely left the operator's own text alone would not be one.

struct Cypher {
  const char* code;
  const char* name;
  // The face this cypher wants, if its symbols are not Latin letters. Empty
  // means "the bundled font is fine".
  const char* font;
};

const Cypher kCyphers[] = {
  {"cy-rot13", "ROT13", ""},
  {"cy-atbash", "Atbash", ""},
  {"cy-leet", "1337", ""},
  {"cy-morse", "Morse", ""},
  // ALIEN LANGUAGE II IS A SCRIPT, not a way of spelling English. The cipher
  // below is right either way -- it is the running-sum one from the show -- but
  // drawn in Latin letters it is a puzzle answer rather than the alphabet.
  // data/fonts/Alienese.ttf (tools/gen_alienese_font.py) draws the symbols,
  // so with it found the cypher writes them; without it the maths still works
  // and the result is readable, which is the honest fallback.
  {"cy-alienese2", "Alienese II", "Alienese.ttf"},
  // Backwards. Every label reversed, which is readable with a little effort
  // and completely disorienting for the first ten seconds, and unlike the
  // others it needs no key at all -- you just read it the other way.
  {"cy-mirror", "Backwards", ""},
};
constexpr int kCypherCount = static_cast<int>(sizeof(kCyphers) / sizeof(kCyphers[0]));

std::string applyRot13(const std::string& in) {
  std::string out = in;
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>('a' + (c - 'a' + 13) % 26);
    else if (c >= 'A' && c <= 'Z') c = static_cast<char>('A' + (c - 'A' + 13) % 26);
  }
  return out;
}

std::string applyAtbash(const std::string& in) {
  std::string out = in;
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>('z' - (c - 'a'));
    else if (c >= 'A' && c <= 'Z') c = static_cast<char>('Z' - (c - 'A'));
  }
  return out;
}

std::string applyLeet(const std::string& in) {
  // Only the letters with an unmistakable digit twin. Pushing it further (|_|
  // for U, |\| for N) makes the interface wider as well as sillier, and a
  // button that no longer fits its box stops being a joke.
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    switch (c) {
      case 'a': case 'A': out += '4'; break;
      case 'e': case 'E': out += '3'; break;
      case 'i': case 'I': out += '1'; break;
      case 'o': case 'O': out += '0'; break;
      case 's': case 'S': out += '5'; break;
      case 't': case 'T': out += '7'; break;
      default: out += c; break;
    }
  }
  return out;
}

std::string applyMorse(const std::string& in) {
  static const char* kMorse[26] = {
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
    "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
    "..-", "...-", ".--", "-..-", "-.--", "--.."
  };
  static const char* kDigits[10] = {
    "-----", ".----", "..---", "...--", "....-", ".....",
    "-....", "--...", "---..", "----."
  };
  std::string out;
  bool first = true;
  for (char c : in) {
    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const char* sym = nullptr;
    if (u >= 'A' && u <= 'Z') sym = kMorse[u - 'A'];
    else if (u >= '0' && u <= '9') sym = kDigits[u - '0'];
    if (sym) {
      if (!first) out += ' ';
      out += sym;
      first = false;
    } else if (c == ' ') {
      out += " / ";
      first = true;
    }
    // Anything else -- punctuation, accents -- is dropped rather than passed
    // through: a lone bracket among the dots reads as a rendering fault.
  }
  return out;
}

// Alienese II, the running-sum cipher.
//
// Futurama's second alien language is not a letter-for-letter substitution
// like its first: each symbol carries the RUNNING TOTAL of everything before
// it, so the same letter encodes differently depending on what it follows.
// That is the interesting part, and it survives being written in Latin
// letters -- A for 0 up to Z for 25 -- which is what this writes when the
// symbols themselves cannot be drawn.
//
// The sum runs per word: the show's own puzzles reset it at spaces, and a sum
// carried across a whole interface would make every label depend on the one
// before it, which is nonsense in a menu.
//
// With `symbols`, each value is written as its own symbol rather than as the
// letter standing in for it: kAlieneseFirst onwards, in the private use area,
// which only Alienese.ttf draws. The symbols are in the TEXT, not the face, so
// they appear wherever a cypher label is drawn and nowhere else -- a lower
// third on air shares the desk's faces but is never put through a cypher.
std::string applyAlienese2(const std::string& in, bool symbols) {
  std::string out;
  out.reserve(symbols ? in.size() * 3 : in.size());
  int running = 0;
  for (char c : in) {
    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (u >= 'A' && u <= 'Z') {
      const int v = u - 'A';
      running = (running + v) % 26;
      if (symbols) {
        // The alphabet has no case, so both cases come out as the symbol.
        const char32_t cp = kAlieneseFirst + static_cast<char32_t>(running);
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
        continue;
      }
      const char enc = static_cast<char>('A' + running);
      out += (c >= 'a' && c <= 'z') ? static_cast<char>(enc - 'A' + 'a') : enc;
    } else {
      // Anything that is not a letter both passes through and RESETS the sum,
      // so each word is independent and a digit cannot smear into the next one.
      out += c;
      running = 0;
    }
  }
  return out;
}

// Reversed, by CHARACTER not by byte. Reversing the bytes of a UTF-8 string
// destroys every multi-byte character in it, which for an interface that also
// speaks Greek, Cyrillic and Japanese would turn the joke into mojibake.
std::string applyMirror(const std::string& in) {
  std::vector<std::string> glyphs;
  for (std::size_t i = 0; i < in.size();) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    std::size_t len = 1;
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    if (i + len > in.size()) len = 1;
    glyphs.push_back(in.substr(i, len));
    i += len;
  }
  std::string out;
  out.reserve(in.size());
  for (auto it = glyphs.rbegin(); it != glyphs.rend(); ++it) out += *it;
  return out;
}

std::string applyCypher(int which, const std::string& in) {
  switch (which) {
    case 1: return applyRot13(in);
    case 2: return applyAtbash(in);
    case 3: return applyLeet(in);
    case 4: return applyMorse(in);
    case 5: return applyAlienese2(in, gSymbols);
    case 6: return applyMirror(in);
    default: return in;
  }
}

// ── Catalogues ──────────────────────────────────────────────────────────────

fs::path catalogueDir(const fs::path& dataDir) { return dataDir / "lang"; }

// First line may be `#name <display name>`; everything else is
// `english<TAB>translation`, and blank or `#` lines are ignored.
bool readCatalogue(const fs::path& file,
                   std::unordered_map<std::string, std::string>& into,
                   std::string& displayName,
                   std::string* fontFile = nullptr,
                   bool* rtl = nullptr,
                   std::string* englishName = nullptr,
                   bool* unverified = nullptr,
                   std::string* fallbackCode = nullptr) {
  std::ifstream in(file);
  if (!in) return false;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (line[0] == '#') {
      const std::string tag = "#name";
      if (line.rfind(tag, 0) == 0) displayName = trim(line.substr(tag.size()));
      const std::string fontTag = "#font";
      if (fontFile && line.rfind(fontTag, 0) == 0) {
        *fontFile = trim(line.substr(fontTag.size()));
      }
      // THE NAME IN ENGLISH, so the picker can be read in the language you
      // are leaving. Every entry is shown in its own script, and the face
      // only changes once a language has been chosen -- so on an English
      // desk the CJK, Arabic and Devanagari entries are boxes and you cannot
      // read what you are selecting.
      const std::string englishTag = "#english";
      if (englishName && line.rfind(englishTag, 0) == 0) {
        *englishName = trim(line.substr(englishTag.size()));
      }
      // NOBODY WHO SPEAKS IT HAS READ THIS FILE. A bare `#unverified` marks a
      // catalogue whose entries were produced without a speaker's review, so
      // the picker can say so rather than presenting it as finished work.
      const std::string unverifiedTag = "#unverified";
      if (unverified && line.rfind(unverifiedTag, 0) == 0) {
        *unverified = true;
      }
      // WHERE AN UNTRANSLATED LINE GOES INSTEAD OF ENGLISH. A catalogue that
      // covers part of the interface reads better falling through to the
      // language its readers already work in -- Kabyle and Tamazight to
      // French, as the trade in the Maghreb does -- than to English.
      const std::string fallbackTag = "#fallback";
      if (fallbackCode && line.rfind(fallbackTag, 0) == 0) {
        *fallbackCode = trim(line.substr(fallbackTag.size()));
      }
      const std::string rtlTag = "#rtl";
      if (rtl && line.rfind(rtlTag, 0) == 0) {
        const std::string v = trim(line.substr(rtlTag.size()));
        *rtl = (v == "1" || lower(v) == "true" || lower(v) == "yes");
      }
      continue;
    }
    const auto tab = line.find('\t');
    if (tab == std::string::npos) continue;
    std::string key = line.substr(0, tab);
    std::string value = trim(line.substr(tab + 1));
    // An empty translation means "not done yet", which must read as English
    // rather than as an empty button.
    if (key.empty() || value.empty()) continue;
    into.emplace(std::move(key), std::move(value));
  }
  return true;
}

}  // namespace

std::vector<LanguageInfo> availableLanguages(const fs::path& dataDir) {
  std::vector<LanguageInfo> out;
  out.push_back({"en", "English", "English", false});

  std::error_code ec;
  const fs::path dir = catalogueDir(dataDir);
  if (fs::is_directory(dir, ec)) {
    std::vector<LanguageInfo> found;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      if (ec) break;
      if (!entry.is_regular_file()) continue;
      if (lower(entry.path().extension().string()) != ".tsv") continue;
      const std::string code = entry.path().stem().string();
      if (code == "en") continue;
      std::unordered_map<std::string, std::string> probe;
      std::string name = code;
      std::string font;
      std::string english;
      bool rtl = false;
      bool unverified = false;
      if (!readCatalogue(entry.path(), probe, name, &font, &rtl, &english,
                         &unverified)) {
        continue;
      }
      // Offering a right-to-left language that this build cannot shape means
      // offering unjoined letters in the wrong order. Better absent, and
      // --self-check says why.
      if (rtl && !gShaping) continue;
      // An empty catalogue is a file somebody started, not a language anybody
      // can pick; offering it would just be English under another name.
      if (probe.empty()) continue;
      found.push_back({code, name, english, false, unverified});
    }
    std::sort(found.begin(), found.end(),
              [](const LanguageInfo& a, const LanguageInfo& b) { return a.name < b.name; });
    out.insert(out.end(), found.begin(), found.end());
  }

  for (int i = 0; i < kCypherCount; ++i) {
    out.push_back({kCyphers[i].code, kCyphers[i].name, kCyphers[i].name, true});
  }
  return out;
}

bool setLanguage(const std::string& code, const fs::path& dataDir, std::string& error) {
  error.clear();
  const std::string want = trim(code).empty() ? std::string("en") : trim(code);

  // Whatever we end up choosing, the face question is answered here, once.
  // Just remembers the request. Whether any of the candidates exists is the
  // font loader's business -- it is the only thing that knows where it looked
  // and what it managed to open -- and it reports back through noteFontResolved.
  auto adoptFont = [&](const std::string& file) {
    gFontFile = file;
    gFontMissing = false;
    gSymbols = false;  // until the loader says it found the face
  };
  (void)dataDir;

  if (want == "en") {
    gActive = nullptr;
    gActiveLower.clear();
    clearComposed();
    gCypher = 0;
    gCode = "en";
    gName = "English";
    adoptFont({});
    gRtl = false;
    gRtlOk = false;
    return true;
  }

  for (int i = 0; i < kCypherCount; ++i) {
    if (want == kCyphers[i].code) {
      gActive = nullptr;
      gActiveLower.clear();
      clearComposed();
      gCypher = i + 1;
      gCode = want;
      gName = kCyphers[i].name;
      adoptFont(kCyphers[i].font);
      gRtl = false;
      gRtlOk = false;
      return true;
    }
  }

  auto cached = gCatalogues.find(want);
  if (cached == gCatalogues.end()) {
    std::unordered_map<std::string, std::string> loaded;
    std::string name = want;
    std::string font;
    bool rtl = false;
    std::string fallback;
    const fs::path file = catalogueDir(dataDir) / (want + ".tsv");
    if (!readCatalogue(file, loaded, name, &font, &rtl, nullptr, nullptr, &fallback) ||
        loaded.empty()) {
      error = "no language catalogue for " + want;
      return false;
    }
    // One level only, and never itself: its own #fallback is not followed,
    // so two catalogues naming each other cannot loop. What it fills in is
    // only what this one leaves out.
    if (!fallback.empty() && fallback != want && fallback != "en") {
      std::unordered_map<std::string, std::string> filler;
      std::string fillerName;
      if (readCatalogue(catalogueDir(dataDir) / (fallback + ".tsv"), filler, fillerName)) {
        for (auto& entry : filler) loaded.try_emplace(entry.first, std::move(entry.second));
      }
    }
    loaded.emplace("\x01" "font", font);
    loaded.emplace("\x01" "rtl", rtl ? "1" : "0");
    // The display name rides along with the catalogue so the picker can show
    // it without re-reading every file.
    loaded.emplace("\x01" "name", name);
    cached = gCatalogues.emplace(want, std::move(loaded)).first;
  }

  gActive = &cached->second;
  // The case-insensitive index, rebuilt with the catalogue. A key that
  // differs from another only in case would collide here; first wins, which
  // is the same answer exact lookup would have given for that spelling.
  gActiveLower.clear();
  clearComposed();
  for (const auto& entry : cached->second) {
    if (!entry.first.empty() && entry.first[0] == '\x01') continue;  // metadata
    gActiveLower.emplace(lower(entry.first), entry.second);
  }
  gCypher = 0;
  gCode = want;
  const auto nameAt = cached->second.find("\x01" "name");
  gName = (nameAt == cached->second.end()) ? want : nameAt->second;
  const auto fontAt = cached->second.find("\x01" "font");
  adoptFont(fontAt == cached->second.end() ? std::string() : fontAt->second);
  const auto rtlAt = cached->second.find("\x01" "rtl");
  gRtl = rtlAt != cached->second.end() && rtlAt->second == "1";
  gRtlOk = false;
  return true;
}

const std::string& activeCode() { return gCode; }
const std::string& activeName() { return gName; }

std::vector<std::string> activeFontCandidates() {
  std::vector<std::string> out;
  std::string item;
  for (char c : gFontFile) {
    if (c == ',') {
      const std::string t = trim(item);
      if (!t.empty()) out.push_back(t);
      item.clear();
    } else {
      item += c;
    }
  }
  const std::string t = trim(item);
  if (!t.empty()) out.push_back(t);
  return out;
}

bool activeFontMissing() { return gFontMissing; }
bool activeIsCypher() { return gCypher != 0; }

std::vector<std::string> cypherFonts() {
  std::vector<std::string> out;
  for (const Cypher& c : kCyphers) {
    if (c.font[0] != '\0') out.push_back(c.font);
  }
  return out;
}
bool activeIsRtl() { return gRtl; }
void noteRtlSupported(bool supported) { gRtlOk = supported; }
bool rtlSupported() { return gRtlOk; }
void noteShapingAvailable(bool available) { gShaping = available; }
bool shapingAvailable() { return gShaping; }

std::vector<std::string> languagesAwaitingShaping(const fs::path& dataDir) {
  std::vector<std::string> out;
  if (gShaping) return out;
  std::error_code ec;
  const fs::path dir = catalogueDir(dataDir);
  if (!fs::is_directory(dir, ec)) return out;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    if (lower(entry.path().extension().string()) != ".tsv") continue;
    std::unordered_map<std::string, std::string> probe;
    std::string name = entry.path().stem().string();
    std::string font;
    bool rtl = false;
    if (!readCatalogue(entry.path(), probe, name, &font, &rtl)) continue;
    if (rtl) out.push_back(name);
  }
  return out;
}
void noteFontResolved(bool found) {
  gFontMissing = !gFontFile.empty() && !found;
  gSymbols = gCypher != 0 && !gFontFile.empty() && found;
}

bool passthrough() { return gActive == nullptr && gCypher == 0; }

namespace {

// An exact line, or the same line in another case. See translate().
bool lookupHit(const std::string& source, std::string& out) {
  const auto at = gActive->find(source);
  if (at != gActive->end()) {
    out = at->second;
    return true;
  }
  // ── THE SAME WORD, SHOUTED ────────────────────────────────────────────────
  //
  // The interface draws Apply and APPLY, Output and OUTPUT, Cancel and
  // CANCEL: a button shouts and a row label does not. Exact lookup made each
  // of those a separate line for the translator to write twice, and about
  // half the untranslated list was a case variant of something already in it.
  //
  // The stored value comes back AS WRITTEN rather than re-cased to match the
  // source. Case is not a byte operation outside ASCII -- upper-casing "ä" or
  // "я" one byte at a time corrupts it, and for Japanese it means nothing --
  // so a German button reading "Anwenden" under an English "APPLY" is the
  // right answer and "ANWENDEN" with a broken umlaut is not.
  const auto caseHit = gActiveLower.find(lower(source));
  if (caseHit != gActiveLower.end()) {
    out = caseHit->second;
    return true;
  }
  return false;
}

// Put `args` into a translated template. "{n}" takes the next one in order;
// "{1}".."{9}" take that one, so a language can reorder them ("{2}件中{1}").
std::string substitute(const std::string& value, const std::vector<std::string>& args) {
  std::string out;
  out.reserve(value.size() + 16);
  std::size_t next = 0;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '{' && i + 2 < value.size() && value[i + 2] == '}') {
      const char c = value[i + 1];
      if (c == 'n') {
        if (next < args.size()) out += args[next];
        ++next;
        i += 2;
        continue;
      }
      if (c >= '1' && c <= '9') {
        const std::size_t k = static_cast<std::size_t>(c - '1');
        if (k < args.size()) out += args[k];
        i += 2;
        continue;
      }
    }
    out += value[i];
  }
  return out;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool composeHit(const std::string& source, std::string& out, int depth);

// ── A STRING BUILT AT RUN TIME ────────────────────────────────────────────────
//
// "Deck 2", "1 of 2", "Bitrate: 6000 kbps", "Deck 1  to Out 1": a label and a
// value put together by the code, which no catalogue can hold whole, because
// the value changes. They were English in every language (2026-11-08: about
// 120 of them in one sweep). Each rule below takes the string apart along a
// seam the interface really uses, translates what the catalogue knows, and
// leaves what it does not as it is -- a path, a screen's name, a codec, a cue
// name. A string none of them can take apart stays English, exactly as an
// untranslated line always has.
bool composeHit(const std::string& source, std::string& out, int depth) {
  if (source.empty() || depth > 4) return false;
  if (lookupHit(source, out)) return true;

  // ── A NAME AND ITS QUALIFIER ──────────────────────────────────────────────
  //
  // Menus qualify a name in brackets: "Full White (motion)", "Pocket Test
  // (night)", "Crosshatch (motion)". Each of those was a separate line to
  // translate, so the picker stayed English until every combination had been
  // written out. Without a line for the whole, each half is looked up on its
  // own and the two are put back together; a half nobody translated stays
  // English, which is what an untranslated line does anyway.
  if (source.size() > 4 && source.back() == ')') {
    const std::size_t open = source.rfind(" (");
    if (open != std::string::npos && open > 0 &&
        source.find_first_of("()", open + 2) == source.size() - 1) {
      // A base laid out in columns ends in spaces ("OPEN THE MAP  (3 rows)"):
      // they are kept, and the words before them are what is looked up.
      std::string base = source.substr(0, open);
      std::string pad;
      while (!base.empty() && base.back() == ' ') {
        pad += ' ';
        base.pop_back();
      }
      const std::string qualifier = source.substr(open + 2, source.size() - open - 3);
      std::string baseShown;
      std::string qualifierShown;
      const bool baseHit = composeHit(base, baseShown, depth + 1);
      const bool qualifierHit = composeHit(qualifier, qualifierShown, depth + 1);
      if (baseHit || qualifierHit) {
        out = (baseHit ? baseShown : base) + pad + " (" + (qualifierHit ? qualifierShown : qualifier) + ")";
        return true;
      }
    }
  }

  // ── NUMBERS ───────────────────────────────────────────────────────────────
  //
  // "Deck 2" is the line "Deck {n}" with a 2 in it; "FADE 2.0 s" is
  // "FADE {n} s". A number is a run of digits with . , or : inside it, so a
  // time (00:01.5) or a decimal is one value. The numbers go back in the order
  // they came out, unless the translation numbers them ({1}, {2}).
  {
    std::string key;
    std::vector<std::string> numbers;
    for (std::size_t i = 0; i < source.size();) {
      if (isDigit(source[i])) {
        std::size_t j = i + 1;
        while (j < source.size() &&
               (isDigit(source[j]) ||
                ((source[j] == '.' || source[j] == ',' || source[j] == ':') &&
                 j + 1 < source.size() && isDigit(source[j + 1])))) {
          ++j;
        }
        numbers.push_back(source.substr(i, j - i));
        key += "{n}";
        i = j;
      } else {
        key += source[i++];
      }
    }
    std::string value;
    if (!numbers.empty() && lookupHit(key, value)) {
      out = substitute(value, numbers);
      return true;
    }
  }

  // ── COLUMNS ───────────────────────────────────────────────────────────────
  //
  // Two or more spaces is how the desk lays a line out in columns: "Deck 1  to
  // Out 1", "HyperDeck 9993  |  OSC Query 5511", "AREA OF INTEREST  full
  // 3840x2160". Each column is its own phrase, so each is looked up on its own
  // and the spacing is kept.
  if (source.find("  ") != std::string::npos) {
    std::string built;
    bool any = false;
    std::size_t i = 0;
    while (i < source.size()) {
      std::size_t gap = source.find("  ", i);
      if (gap == std::string::npos) gap = source.size();
      const std::string piece = source.substr(i, gap - i);
      std::string shown;
      if (!piece.empty() && composeHit(piece, shown, depth + 1)) {
        built += shown;
        any = true;
      } else {
        built += piece;
      }
      std::size_t after = gap;
      while (after < source.size() && source[after] == ' ') ++after;
      built.append(source, gap, after - gap);
      i = after;
    }
    if (any) {
      out = built;
      return true;
    }
  }

  // ── A LABEL AND ITS VALUE ─────────────────────────────────────────────────
  //
  // "Bitrate: 6000 kbps", "PTP: not running", "camera: the first one found".
  // The label is looked up, and the value is too -- but a value nobody wrote
  // a line for is left exactly as it is, because it is so often a path, a
  // screen or a cue that must read as the operator knows it.
  {
    const std::size_t colon = source.find(": ");
    if (colon != std::string::npos && colon > 0 && colon + 2 < source.size()) {
      const std::string label = source.substr(0, colon);
      const std::string value = source.substr(colon + 2);
      std::string labelShown;
      std::string valueShown;
      const bool labelHit = composeHit(label, labelShown, depth + 1);
      const bool valueHit = composeHit(value, valueShown, depth + 1);
      if (labelHit || valueHit) {
        out = (labelHit ? labelShown : label) + ": " + (valueHit ? valueShown : value);
        return true;
      }
    }
  }
  return false;
}

// Strings built at run time are looked up every frame they are drawn, so what
// the rules above made of each is kept. Cleared with the catalogue; emptied
// when it grows past a bound, because a running timecode is a new string
// every frame.
std::unordered_map<std::string, std::string> gComposed;
std::mutex gComposedMutex;

void clearComposed() {
  std::lock_guard<std::mutex> lock(gComposedMutex);
  gComposed.clear();
}

}  // namespace

std::string translate(const std::string& source) {
  if (gCypher != 0) {
    return applyCypher(gCypher, source);
  }
  if (!gActive || source.empty()) {
    return source;
  }
  std::string hit;
  if (lookupHit(source, hit)) {
    return hit;
  }
  {
    std::lock_guard<std::mutex> lock(gComposedMutex);
    const auto known = gComposed.find(source);
    if (known != gComposed.end()) return known->second;
  }
  const bool composed = composeHit(source, hit, 0);
  {
    std::lock_guard<std::mutex> lock(gComposedMutex);
    if (gComposed.size() > 20000) gComposed.clear();
    gComposed.emplace(source, composed ? hit : source);
  }
  if (composed) {
    return hit;
  }
  // FOR TESTS: DECKBOY_I18N_MISSES=<file> lists every string the app asked
  // for and the active catalogue did not have, once each. Only a running app
  // knows what it builds at run time ("VOLUME " + a number, a renamed button),
  // which is what a source audit cannot see.
  static const char* missFile = std::getenv("DECKBOY_I18N_MISSES");
  if (missFile && *missFile) {
    static std::set<std::string> seen;
    int letters = 0;
    for (unsigned char ch : source) letters += std::isalpha(ch) ? 1 : 0;
    if (letters >= 2 && seen.insert(source).second) {
      std::ofstream(missFile, std::ios::app) << source << '\n';
    }
  }
  return source;
}

std::string format(const std::string& tmpl, const std::vector<std::string>& args) {
  return substitute(translate(tmpl), args);
}

}  // namespace i18n
}  // namespace core
}  // namespace deckboy
