// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.


#ifndef PLAYBOY_CORE_CONSTANTS_HPP
#define PLAYBOY_CORE_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

// Global constants for drop-in use. No namespace so existing code keeps working.
// UI dimensions
constexpr int kControlWidth = 1660;
constexpr int kControlHeight = 980;
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
// DMG palette (4-tone): light / mid / dark / deepest.
constexpr std::uint32_t kShellOuterColor = 0x9BBC0FFFu;
constexpr std::uint32_t kShellInnerColor = 0x8BAC0FFFu;
constexpr std::uint32_t kShellShadowColor = 0x306230FFu;
constexpr std::uint32_t kScreenLightColor = 0x9BBC0FFFu;
constexpr std::uint32_t kScreenMidColor = 0x8BAC0FFFu;
constexpr std::uint32_t kScreenDarkColor = 0x306230FFu;
constexpr std::uint32_t kScreenDeepColor = 0x0F380FFFu;
constexpr std::uint32_t kScreenInkSoftColor = 0x306230FFu;
constexpr std::uint32_t kButtonBezelColor = 0x306230FFu;
constexpr std::uint32_t kDeleteBezelColor = 0x306230FFu;

// App strings
constexpr std::string_view kAppTitle = "Deckboy";
constexpr std::string_view kOutputTitle = "Deckboy Output";
constexpr std::string_view kAppModelLabel = "model db-001 / v0.01";

#endif
