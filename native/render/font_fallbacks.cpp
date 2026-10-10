// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// font_fallbacks.cpp -- see font_fallbacks.hpp. Every path below was checked on
// the platform it names -- the Linux ones against the file lists of the Debian
// packages that install them, which is what Ubuntu, Mint and Raspberry Pi OS
// ship -- and `Deckboy --self-check` prints the ones it found. CI runs that on
// every platform, so a font that moves shows up there.

#include "render/font_fallbacks.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <initializer_list>
#include <system_error>

#include "core/paths.hpp"
#include "core/sdl_compat.hpp"

#if !defined(_WIN32) && !defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace deckboy::render {

namespace fs = std::filesystem;
using shaping::FallbackFont;

namespace {

enum class Cjk { Simplified, Traditional, Japanese, Korean };

std::vector<FallbackFont> cjkFaces(Cjk which) {
  switch (which) {
    case Cjk::Simplified:
      return {
#if defined(_WIN32)
        {"C:/Windows/Fonts/msyh.ttc", 0},
        {"C:/Windows/Fonts/simsun.ttc", 0},
#elif defined(__APPLE__)
        {"/System/Library/Fonts/PingFang.ttc", 0},
        {"/System/Library/Fonts/Hiragino Sans GB.ttc", 0},
        {"/System/Library/Fonts/STHeiti Light.ttc", 0},
#else
        {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 2},
        {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 2},
        {"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", 0},
        {"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", 0},
        {"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", 0},
#endif
      };
    case Cjk::Traditional:
      return {
#if defined(_WIN32)
        {"C:/Windows/Fonts/msjh.ttc", 0},
        {"C:/Windows/Fonts/mingliu.ttc", 0},
#elif defined(__APPLE__)
        {"/System/Library/Fonts/PingFang.ttc", 1},
        {"/System/Library/Fonts/STHeiti Light.ttc", 0},
#else
        {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 3},
        {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 3},
        {"/usr/share/fonts/truetype/arphic/uming.ttc", 0},
        {"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", 0},
        {"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", 0},
#endif
      };
    case Cjk::Japanese:
      return {
#if defined(_WIN32)
        {"C:/Windows/Fonts/YuGothR.ttc", 0},
        {"C:/Windows/Fonts/meiryo.ttc", 0},
        {"C:/Windows/Fonts/msgothic.ttc", 0},
#elif defined(__APPLE__)
        {"/System/Library/Fonts/\xE3\x83\x92\xE3\x83\xA9\xE3\x82\xAE\xE3\x83\x8E\xE8\xA7\x92"
         "\xE3\x82\xB4\xE3\x82\xB7\xE3\x83\x83\xE3\x82\xAF W3.ttc", 0},  // Hiragino Kaku Gothic W3
        {"/System/Library/Fonts/Hiragino Sans GB.ttc", 0},
#else
        {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0},
        {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 0},
        {"/usr/share/fonts/opentype/ipafont-gothic/ipagp.ttf", 0},
        {"/usr/share/fonts/truetype/takao-gothic/TakaoPGothic.ttf", 0},
        {"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", 0},
#endif
      };
    case Cjk::Korean:
      return {
#if defined(_WIN32)
        {"C:/Windows/Fonts/malgun.ttf", 0},
#elif defined(__APPLE__)
        {"/System/Library/Fonts/AppleSDGothicNeo.ttc", 0},
#else
        {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 1},
        {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 1},
        {"/usr/share/fonts/truetype/nanum/NanumGothic.ttf", 0},
        {"/usr/share/fonts/truetype/unfonts-core/UnDotum.ttf", 0},
#endif
      };
  }
  return {};
}

// The order the four CJK styles are tried in.
std::vector<Cjk> cjkOrder(const std::string& uiLanguage) {
  std::vector<Cjk> order;
  auto add = [&](Cjk c) {
    if (std::find(order.begin(), order.end(), c) == order.end()) order.push_back(c);
  };
  auto fromLanguage = [&](const std::string& lang, const std::string& region) {
    if (lang == "ja") add(Cjk::Japanese);
    else if (lang == "ko") add(Cjk::Korean);
    else if (lang == "zh") {
      const bool traditional = region == "TW" || region == "HK" || region == "MO" ||
                               region == "Hant";
      add(traditional ? Cjk::Traditional : Cjk::Simplified);
    }
  };
  // The interface first: a Japanese desk draws Han in a Japanese face.
  if (uiLanguage == "zh-Hant") add(Cjk::Traditional);
  else if (uiLanguage.rfind("zh", 0) == 0) add(Cjk::Simplified);
  else fromLanguage(uiLanguage.substr(0, uiLanguage.find('-')), "");
  // Then whatever the computer itself says its languages are.
  int count = 0;
  if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
    for (int i = 0; i < count && locales[i]; ++i) {
      fromLanguage(locales[i]->language ? locales[i]->language : "",
                   locales[i]->country ? locales[i]->country : "");
    }
    SDL_free(locales);
  }
  for (Cjk c : {Cjk::Simplified, Cjk::Japanese, Cjk::Traditional, Cjk::Korean}) add(c);
  return order;
}

std::vector<FallbackFont> otherScripts() {
  return {
#if defined(_WIN32)
    {"C:/Windows/Fonts/segoeui.ttf", 0},   // Armenian, Georgian
    {"C:/Windows/Fonts/Nirmala.ttc", 0},   // the Indic scripts
    {"C:/Windows/Fonts/LeelawUI.ttf", 0},  // Thai, Lao, Khmer
    {"C:/Windows/Fonts/mmrtext.ttf", 0},   // Myanmar
    {"C:/Windows/Fonts/ebrima.ttf", 0},    // Ethiopic, N'Ko, Vai
    {"C:/Windows/Fonts/gadugi.ttf", 0},    // Cherokee, Canadian syllabics
    {"C:/Windows/Fonts/himalaya.ttf", 0},  // Tibetan
    {"C:/Windows/Fonts/monbaiti.ttf", 0},  // Mongolian
    {"C:/Windows/Fonts/javatext.ttf", 0},  // Javanese
    {"C:/Windows/Fonts/seguisym.ttf", 0},  // symbols, arrows, dingbats
    {"C:/Windows/Fonts/seguihis.ttf", 0},  // historic scripts
    {"C:/Windows/Fonts/seguiemj.ttf", 0},  // emoji
    {"C:/Windows/Fonts/ARIALUNI.TTF", 0},  // where Office installed it
    {"C:/Windows/Fonts/arial.ttf", 0},
#elif defined(__APPLE__)
    {"/System/Library/Fonts/Kohinoor.ttc", 0},  // Devanagari
    {"/System/Library/Fonts/KohinoorBangla.ttc", 0},
    {"/System/Library/Fonts/KohinoorTelugu.ttc", 0},
    {"/System/Library/Fonts/Supplemental/DevanagariMT.ttc", 0},
    {"/System/Library/Fonts/Supplemental/Tamil Sangam MN.ttc", 0},
    {"/System/Library/Fonts/Thonburi.ttc", 0},  // Thai
    {"/System/Library/Fonts/Supplemental/Lao Sangam MN.ttf", 0},
    {"/System/Library/Fonts/Supplemental/Khmer Sangam MN.ttf", 0},
    {"/System/Library/Fonts/Supplemental/Myanmar Sangam MN.ttc", 0},
    {"/System/Library/Fonts/Supplemental/Kefa.ttc", 0},  // Ethiopic
    {"/System/Library/Fonts/Supplemental/Mshtakan.ttc", 0},  // Armenian
    {"/System/Library/Fonts/Apple Symbols.ttf", 0},
    {"/System/Library/Fonts/Apple Color Emoji.ttc", 0},
    {"/System/Library/Fonts/Supplemental/Arial Unicode.ttf", 0},
#else
    {"/usr/share/fonts/truetype/noto/NotoSansDevanagari-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansBengali-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansTamil-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansTelugu-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansGujarati-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansGurmukhi-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansKannada-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansMalayalam-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansSinhala-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansThai-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansLao-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansKhmer-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansMyanmar-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansEthiopic-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansArmenian-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansGeorgian-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf", 0},
    {"/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", 0},
    {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 0},
    {"/usr/share/fonts/truetype/freefont/FreeSerif.ttf", 0},
    {"/usr/share/fonts/opentype/unifont/unifont.otf", 0},  // every BMP character, plainly
#endif
  };
}

const char* gRoute = "fixed list";

#if !defined(_WIN32) && !defined(__APPLE__)
// fontconfig, opened at run time rather than linked. Linked, Deckboy Mini would
// not start at all on a Raspberry Pi OS Lite without libfontconfig1, and the
// portable tarball's packager would bundle a fontconfig that then reads the
// host's newer /etc/fonts and misreads it. Opened here, a machine without it
// simply keeps the fixed Debian list.
//
// Declared by hand, not from <fontconfig/fontconfig.h>, so the build needs no
// -dev package. These are the library's stable C ABI since 2.0: opaque
// patterns and charsets, FcFontSet's three public fields, FcResult 0 = match.
class Fontconfig {
 public:
  struct FontSet {
    int nfont;
    int sfont;
    void** fonts;
  };

  static Fontconfig& get() {
    static Fontconfig fc;
    return fc;
  }
  bool ready() const { return config_ != nullptr; }

  // The best installed face that has every one of `chars`, for `lang` when
  // given (it picks the regional style of a Han character). Empty when none.
  FallbackFont bestFor(const char* lang, std::initializer_list<char32_t> chars) const {
    FallbackFont found, serif;
    void* charset = charSetCreate_();
    for (char32_t c : chars) charSetAddChar_(charset, static_cast<unsigned>(c));
    void* pattern = patternCreate_();
    patternAddCharSet_(pattern, "charset", charset);
    // Without a family the sort ranks serif and sans alike; the desk is sans.
    patternAddString_(pattern, "family", reinterpret_cast<const unsigned char*>("sans-serif"));
    if (lang) patternAddString_(pattern, "lang", reinterpret_cast<const unsigned char*>(lang));
    configSubstitute_(config_, pattern, 0 /* FcMatchPattern */);
    defaultSubstitute_(pattern);
    int result = 0;
    // FcFontSort, not FcFontMatch: the single best match comes back even when
    // it lacks the script, so walk the ranked list for the first that has it.
    if (FontSet* set = fontSort_(config_, pattern, 0, nullptr, &result)) {
      for (int i = 0; i < set->nfont && found.path.empty(); ++i) {
        void* font = set->fonts[i];
        unsigned char* file = nullptr;
        unsigned char* family = nullptr;
        void* has = nullptr;
        int index = 0;
        if (patternGetString_(font, "file", 0, &file) != 0 || !file) continue;
        if (patternGetCharSet_(font, "charset", 0, &has) != 0 || !has) continue;
        if (!charSetIsSubset_(charset, has)) continue;
        if (!openable(reinterpret_cast<const char*>(file))) continue;
        patternGetInteger_(font, "index", 0, &index);
        // The high bits name a variable font's instance; the face is the low 16.
        const FallbackFont face {reinterpret_cast<const char*>(file), index & 0xFFFF};
        // A script with no face in the sans-serif alias ranks its serif and sans
        // faces level (Noto Serif Gujarati beside Noto Sans Gujarati), and the
        // tie falls either way. fontconfig keeps no serif flag, so the family
        // name decides; a serif face is used only when no sans one has it.
        patternGetString_(font, "family", 0, &family);
        const std::string name = family ? reinterpret_cast<const char*>(family) : "";
        const bool isSerif = name.find("Serif") != std::string::npos &&
                             name.find("Sans") == std::string::npos;
        if (!isSerif) found = face;
        else if (serif.path.empty()) serif = face;
      }
      fontSetDestroy_(set);
    }
    patternDestroy_(pattern);
    charSetDestroy_(charset);
    return found.path.empty() ? serif : found;
  }

 private:
  Fontconfig() {
    lib_ = dlopen("libfontconfig.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib_) return;
    const bool all =
      load(initLoadConfigAndFonts_, "FcInitLoadConfigAndFonts") &&
      load(patternCreate_, "FcPatternCreate") && load(patternDestroy_, "FcPatternDestroy") &&
      load(patternAddCharSet_, "FcPatternAddCharSet") &&
      load(patternAddString_, "FcPatternAddString") &&
      load(patternGetString_, "FcPatternGetString") &&
      load(patternGetInteger_, "FcPatternGetInteger") &&
      load(patternGetCharSet_, "FcPatternGetCharSet") &&
      load(charSetCreate_, "FcCharSetCreate") && load(charSetDestroy_, "FcCharSetDestroy") &&
      load(charSetAddChar_, "FcCharSetAddChar") &&
      load(charSetIsSubset_, "FcCharSetIsSubset") &&
      load(configSubstitute_, "FcConfigSubstitute") &&
      load(defaultSubstitute_, "FcDefaultSubstitute") && load(fontSort_, "FcFontSort") &&
      load(fontSetDestroy_, "FcFontSetDestroy");
    // Kept for the life of the process: the list is rebuilt whenever the
    // interface language changes, and a fresh config would reread the caches.
    if (all) config_ = initLoadConfigAndFonts_();
  }

  template <typename Fn>
  bool load(Fn& fn, const char* name) {
    fn = reinterpret_cast<Fn>(dlsym(lib_, name));
    return fn != nullptr;
  }

  // SDL_ttf wants outline fonts. fontconfig also lists bitmap PCF and Type 1
  // faces, which would draw at one size or not at all.
  static bool openable(const char* file) {
    std::string ext = fs::path(file).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc" || ext == ".otc";
  }

  void* lib_ = nullptr;
  void* config_ = nullptr;
  void* (*initLoadConfigAndFonts_)() = nullptr;
  void* (*patternCreate_)() = nullptr;
  void (*patternDestroy_)(void*) = nullptr;
  int (*patternAddCharSet_)(void*, const char*, const void*) = nullptr;
  int (*patternAddString_)(void*, const char*, const unsigned char*) = nullptr;
  int (*patternGetString_)(const void*, const char*, int, unsigned char**) = nullptr;
  int (*patternGetInteger_)(const void*, const char*, int, int*) = nullptr;
  int (*patternGetCharSet_)(const void*, const char*, int, void**) = nullptr;
  void* (*charSetCreate_)() = nullptr;
  void (*charSetDestroy_)(void*) = nullptr;
  int (*charSetAddChar_)(void*, unsigned) = nullptr;
  int (*charSetIsSubset_)(const void*, const void*) = nullptr;
  int (*configSubstitute_)(void*, void*, int) = nullptr;
  void (*defaultSubstitute_)(void*) = nullptr;
  FontSet* (*fontSort_)(void*, void*, int, void**, int*) = nullptr;
  void (*fontSetDestroy_)(FontSet*) = nullptr;
};

// A few letters each script cannot do without, so a face that has them really
// draws the script rather than one stray sign of it.
FallbackFont fontconfigCjk(const Fontconfig& fc, Cjk which) {
  switch (which) {
    case Cjk::Simplified: return fc.bestFor("zh-cn", {0x7B80, 0x4F53, 0x4E2D, 0x6587});
    case Cjk::Traditional: return fc.bestFor("zh-tw", {0x7E41, 0x9AD4, 0x4E2D, 0x6587});
    case Cjk::Japanese: return fc.bestFor("ja", {0x65E5, 0x672C, 0x8A9E, 0x3072, 0x30AB});
    case Cjk::Korean: return fc.bestFor("ko", {0xD55C, 0xAD6D, 0xC5B4});
  }
  return {};
}

std::vector<FallbackFont> fontconfigOtherScripts(const Fontconfig& fc) {
  return {
    fc.bestFor("hi", {0x0939, 0x093F, 0x0928, 0x094D, 0x0926}),  // Devanagari
    fc.bestFor("bn", {0x09AC, 0x09BE, 0x0982, 0x09B2}),          // Bengali
    fc.bestFor("ta", {0x0BA4, 0x0BAE, 0x0BBF, 0x0BB4}),          // Tamil
    fc.bestFor("te", {0x0C24, 0x0C46, 0x0C32, 0x0C41}),          // Telugu
    fc.bestFor("gu", {0x0A97, 0x0AC1, 0x0A9C, 0x0AB0}),          // Gujarati
    fc.bestFor("pa", {0x0A2A, 0x0A70, 0x0A1C, 0x0A3E}),          // Gurmukhi
    fc.bestFor("kn", {0x0C95, 0x0CA8, 0x0CCD, 0x0CA1}),          // Kannada
    fc.bestFor("ml", {0x0D2E, 0x0D32, 0x0D2F, 0x0D3E}),          // Malayalam
    fc.bestFor("si", {0x0DC3, 0x0DD2, 0x0D82, 0x0DC4}),          // Sinhala
    fc.bestFor("or", {0x0B13, 0x0B21, 0x0B3F, 0x0B06}),          // Odia
    fc.bestFor("th", {0x0E44, 0x0E17, 0x0E22}),                  // Thai
    fc.bestFor("lo", {0x0EA5, 0x0EB2, 0x0EA7}),                  // Lao
    fc.bestFor("km", {0x1781, 0x17D2, 0x1798, 0x17C2}),          // Khmer
    fc.bestFor("my", {0x1019, 0x103C, 0x1014, 0x103A}),          // Myanmar
    fc.bestFor("bo", {0x0F56, 0x0F7C, 0x0F51, 0x0F0B}),          // Tibetan
    fc.bestFor("am", {0x12A0, 0x121B, 0x122D, 0x129B}),          // Ethiopic
    fc.bestFor("hy", {0x0540, 0x0561, 0x0575, 0x0565}),          // Armenian
    fc.bestFor("ka", {0x10E5, 0x10D0, 0x10E0, 0x10D7}),          // Georgian
    fc.bestFor(nullptr, {0x2605, 0x2192, 0x2713, 0x25B6}),       // symbols, arrows
    fc.bestFor(nullptr, {0x1F600, 0x1F3AC, 0x1F44D}),            // emoji
  };
}
#endif

}  // namespace

const char* fontDiscoveryRoute() { return gRoute; }

std::vector<FallbackFont> scriptFallbacks(const std::string& uiLanguage) {
  std::vector<FallbackFont> wanted;
  const fs::path bundled = deckboy::core::Paths::dataDir() / "fonts";
  // Latin, Greek and Cyrillic first: the pixel face and some CJK faces lack
  // accented Latin and Cyrillic, and Liberation is what the rest of the desk
  // draws them in.
  wanted.push_back({(bundled / "LiberationSans-Regular.ttf").string(), 0});
  // Shipped with Deckboy, so Arabic, Persian and Tamazight look the same on
  // every machine rather than in whatever each one happens to have.
  wanted.push_back({(bundled / "NotoSansArabic-Regular.ttf").string(), 0});
  wanted.push_back({(bundled / "NotoSansTifinagh-Regular.ttf").string(), 0});
  // The Alienese II cypher's symbols, in the private use area -- ahead of the
  // system's faces, any of which may put something of its own there.
  wanted.push_back({(bundled / "Alienese.ttf").string(), 0});
  // Then the system's faces. Where fontconfig answers, its face for each
  // script goes ahead of the fixed paths, which stay behind it: they cost one
  // stat each and still catch a face fontconfig ranked out.
#if !defined(_WIN32) && !defined(__APPLE__)
  const Fontconfig& fc = Fontconfig::get();
  gRoute = fc.ready() ? "fontconfig" : "fixed list";
#endif
  for (Cjk c : cjkOrder(uiLanguage)) {
#if !defined(_WIN32) && !defined(__APPLE__)
    if (fc.ready()) wanted.push_back(fontconfigCjk(fc, c));
#endif
    for (const FallbackFont& f : cjkFaces(c)) wanted.push_back(f);
  }
#if !defined(_WIN32) && !defined(__APPLE__)
  if (fc.ready()) {
    for (const FallbackFont& f : fontconfigOtherScripts(fc)) wanted.push_back(f);
  }
#endif
  for (const FallbackFont& f : otherScripts()) wanted.push_back(f);

  std::vector<FallbackFont> out;
  for (const FallbackFont& f : wanted) {
    std::error_code ec;
    if (f.path.empty() || !fs::is_regular_file(fs::path(f.path), ec)) continue;  // narrow paths are UTF-8 off Windows
    const bool seen = std::any_of(out.begin(), out.end(), [&](const FallbackFont& o) {
      return o.path == f.path && o.faceIndex == f.faceIndex;
    });
    if (!seen) out.push_back(f);
  }
  return out;
}

}  // namespace deckboy::render
