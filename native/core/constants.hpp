// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.


#ifndef DECKBOY_CORE_CONSTANTS_HPP
#define DECKBOY_CORE_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

// Global constants for drop-in use. No namespace so existing code keeps working.
// UI dimensions
constexpr int kControlWidth = 1760;
constexpr int kControlHeight = 1020;
constexpr int kOutputWidth = 1280;
constexpr int kOutputHeight = 720;
constexpr int kSidebarWidth = 460;
constexpr int kColWidth = 270;
constexpr int kColHeaderH = 66;
constexpr int kColFooterH = 50;
constexpr int kGlobalHeaderH = 64;
constexpr int kRowHeight = 72;
constexpr int kPadding = 24;

// Audio
constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;

// Media
constexpr size_t kMaxVideoFrames = 6;

// Colors (SDL-style RGBA as Uint32)
// DMG Game Boy palette (4-tone) — default theme.
// These are intentionally non-const so a theme asset pack can overwrite them at runtime.
inline std::uint32_t kShellOuterColor   = 0xC4CFA1FFu;  // case plastic (196,207,161)
inline std::uint32_t kShellInnerColor   = 0xA5B088FFu;  // inner case (165,176,136)
inline std::uint32_t kShellShadowColor  = 0x5A6B4AFFu;  // case shadow (90,107,74)
inline std::uint32_t kScreenLightColor  = 0x9BBC0FFFu;  // LCD lightest (155,188,15)
inline std::uint32_t kScreenMidColor    = 0x8BAC0FFFu;  // LCD mid (139,172,15)
inline std::uint32_t kScreenDarkColor   = 0x306230FFu;  // LCD dark (48,98,48)
inline std::uint32_t kScreenDeepColor   = 0x0F380FFFu;  // LCD deepest (15,56,15)
inline std::uint32_t kScreenInkSoftColor= 0x4A7A2AFFu;  // soft ink (74,122,42)
inline std::uint32_t kButtonBezelColor  = 0x7B8B5EFFu;  // button rim (123,139,94)
inline std::uint32_t kDeleteBezelColor  = 0x8B3A3AFFu;  // danger red (139,58,58)

// Layout system
constexpr int kLayoutSpacingUnit = 8;
constexpr int kLayoutPanelPadding = 16;
constexpr int kLayoutPanelGap = 12;
constexpr int kLayoutPanelBorder = 2;
constexpr int kLayoutTextInset = 12;
constexpr int kLayoutHeaderHeight = 56;
constexpr int kLayoutBottomBarHeight = 132;
constexpr int kLayoutButtonHeight = 48;
constexpr int kLayoutButtonPadding = 12;
constexpr int kLayoutButtonGap = 8;

// App strings
constexpr std::string_view kAppTitle = "Deckboy";
constexpr std::string_view kOutputTitle = "Deckboy Output";
constexpr std::string_view kAppVersion = "0.74";
constexpr std::string_view kAppModelLabel = "Deckboy v0.74";

#endif
