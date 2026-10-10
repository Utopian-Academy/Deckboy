// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// font_fallbacks.cpp -- see font_fallbacks.hpp. Every path below was checked on
// the platform it names; `Deckboy --self-check` prints the ones it found, and CI
// runs that on every platform, so a font that moves shows up there.

#include "render/font_fallbacks.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "core/paths.hpp"
#include "core/sdl_compat.hpp"

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
    {"C:/Windows/Fonts/segoeui.ttf", 0},   // Hebrew, Armenian, Georgian
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
    {"/System/Library/Fonts/ArialHB.ttc", 0},  // Hebrew
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
    {"/usr/share/fonts/truetype/noto/NotoSansHebrew-Regular.ttf", 0},
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

}  // namespace

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
  for (Cjk c : cjkOrder(uiLanguage)) {
    for (const FallbackFont& f : cjkFaces(c)) wanted.push_back(f);
  }
  for (const FallbackFont& f : otherScripts()) wanted.push_back(f);

  std::vector<FallbackFont> out;
  for (const FallbackFont& f : wanted) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::path(f.path), ec)) continue;  // narrow paths are UTF-8 off Windows
    const bool seen = std::any_of(out.begin(), out.end(), [&](const FallbackFont& o) {
      return o.path == f.path && o.faceIndex == f.faceIndex;
    });
    if (!seen) out.push_back(f);
  }
  return out;
}

}  // namespace deckboy::render
