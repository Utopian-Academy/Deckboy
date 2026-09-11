/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Deckboy — Interface language
 * Copyright (C) 2026 Deckboy Contributors
 */

#include "i18n.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <system_error>
#include <unordered_map>

namespace fs = std::filesystem;

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
std::string gCode = "en";
std::string gName = "English";
std::string gFontFile;      // empty = the bundled face is fine
bool gFontMissing = false;  // asked for a face that is not installed
bool gRtl = false;          // catalogue said #rtl 1
bool gRtlOk = false;        // the renderer accepted that direction
bool gShaping = false;      // this build can shape/reorder at all
int gCypher = 0;   // 0 = none; see kCyphers

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
// cypher that reached for those would draw a wall of empty boxes -- which is
// also why Alienese II is written in Latin letters rather than its own glyphs.
// These five render on any build, on any platform, in any theme.
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
  // drawn in Latin letters it is a puzzle answer rather than the alphabet. Put
  // Alienese.ttf in data/fonts and it is drawn properly; without it the maths
  // still works and the result is readable, which is the honest fallback.
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
// That is the interesting part and it survives being written in Latin letters,
// which is what this does -- the real glyphs are not in Unicode and no bundled
// font has them, so drawing those would mean a wall of empty boxes.
//
// The sum runs per word: the show's own puzzles reset it at spaces, and a sum
// carried across a whole interface would make every label depend on the one
// before it, which is nonsense in a menu.
std::string applyAlienese2(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  int running = 0;
  for (char c : in) {
    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (u >= 'A' && u <= 'Z') {
      const int v = u - 'A';
      running = (running + v) % 26;
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
    case 5: return applyAlienese2(in);
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
                   bool* rtl = nullptr) {
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
  out.push_back({"en", "English", false});

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
      bool rtl = false;
      if (!readCatalogue(entry.path(), probe, name, &font, &rtl)) continue;
      // Offering a right-to-left language that this build cannot shape means
      // offering unjoined letters in the wrong order. Better absent, and
      // --self-check says why.
      if (rtl && !gShaping) continue;
      // An empty catalogue is a file somebody started, not a language anybody
      // can pick; offering it would just be English under another name.
      if (probe.empty()) continue;
      found.push_back({code, name, false});
    }
    std::sort(found.begin(), found.end(),
              [](const LanguageInfo& a, const LanguageInfo& b) { return a.name < b.name; });
    out.insert(out.end(), found.begin(), found.end());
  }

  for (int i = 0; i < kCypherCount; ++i) {
    out.push_back({kCyphers[i].code, kCyphers[i].name, true});
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
  };
  (void)dataDir;

  if (want == "en") {
    gActive = nullptr;
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
    const fs::path file = catalogueDir(dataDir) / (want + ".tsv");
    if (!readCatalogue(file, loaded, name, &font, &rtl) || loaded.empty()) {
      error = "no language catalogue for " + want;
      return false;
    }
    loaded.emplace("\x01" "font", font);
    loaded.emplace("\x01" "rtl", rtl ? "1" : "0");
    // The display name rides along with the catalogue so the picker can show
    // it without re-reading every file.
    loaded.emplace("\x01" "name", name);
    cached = gCatalogues.emplace(want, std::move(loaded)).first;
  }

  gActive = &cached->second;
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
void noteFontResolved(bool found) { gFontMissing = !gFontFile.empty() && !found; }

bool passthrough() { return gActive == nullptr && gCypher == 0; }

std::string translate(const std::string& source) {
  if (gCypher != 0) {
    return applyCypher(gCypher, source);
  }
  if (!gActive || source.empty()) {
    return source;
  }
  const auto at = gActive->find(source);
  return (at == gActive->end()) ? source : at->second;
}

}  // namespace i18n
}  // namespace core
}  // namespace deckboy
