// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// font_fallbacks.hpp -- which fonts can draw what the bundled ones cannot.
//
// The bundled faces are Latin, Greek and Cyrillic (Liberation) and a pixel face
// that is barely more. Everything else -- Arabic and Persian, Han, kana and
// Hangul, the Indic scripts, Thai, Ethiopic, Tifinagh, symbols -- comes
// from here: two small faces Deckboy ships (Noto Sans Arabic and Noto Sans
// Tifinagh, so the North African scripts look the same everywhere) and the
// fonts every desktop already has (Yu Gothic and Microsoft YaHei on Windows,
// Hiragino and PingFang on macOS, Noto on Linux), listed by path because each of
// them is at a fixed place on its platform.
//
// Han characters are shared between Chinese and Japanese but drawn differently
// in each, so the CJK faces are ordered by the interface language first and the
// computer's own preferred languages after that: an English desk on a Japanese
// computer draws a Japanese cue name in a Japanese face.

#pragma once

#include <string>
#include <vector>

#include "render/text_shaper.hpp"

namespace deckboy::render {

// The fallback list for an interface in `uiLanguage` (a catalogue code such as
// "en", "ja", "zh-Hans", "fa"), only fonts that exist on this machine, best
// first.
std::vector<shaping::FallbackFont> scriptFallbacks(const std::string& uiLanguage);

}  // namespace deckboy::render
