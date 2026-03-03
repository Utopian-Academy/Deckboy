// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Playboy Contributors
// This file is part of Playboy, a cue deck for live events.
// See LICENSE for details.


#ifndef PLAYBOY_CORE_CONSTANTS_HPP
#define PLAYBOY_CORE_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

// Global constants for drop-in use. No namespace so existing code keeps working.
// UI dimensions
constexpr int kControlWidth = 1440;
constexpr int kControlHeight = 900;
constexpr int kOutputWidth = 1280;
constexpr int kOutputHeight = 720;
constexpr int kSidebarWidth = 420;
constexpr int kColWidth = 240;
constexpr int kColHeaderH = 58;
constexpr int kColFooterH = 42;
constexpr int kGlobalHeaderH = 54;
constexpr int kRowHeight = 68;
constexpr int kPadding = 20;

// Audio
constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;

// Media
constexpr size_t kMaxVideoFrames = 6;

// Colors (SDL-style RGBA as Uint32)
constexpr std::uint32_t kShellOuterColor = 0xC9CFB3FFu;
constexpr std::uint32_t kShellInnerColor = 0xB0B98DFFu;
constexpr std::uint32_t kShellShadowColor = 0x7B8167FFu;
constexpr std::uint32_t kScreenLightColor = 0x9BBC0FFFu;
constexpr std::uint32_t kScreenMidColor = 0x8BAC0FFFu;
constexpr std::uint32_t kScreenDarkColor = 0x306230FFu;
constexpr std::uint32_t kScreenDeepColor = 0x0F380FFFu;
constexpr std::uint32_t kScreenInkSoftColor = 0x234A23FFu;
constexpr std::uint32_t kButtonBezelColor = 0x5E6954FFu;
constexpr std::uint32_t kDeleteBezelColor = 0x3B4B38FFu;

// App strings
constexpr std::string_view kAppTitle = "Playboy";
constexpr std::string_view kOutputTitle = "Playboy Output";
constexpr std::string_view kAppModelLabel = "model pb-001 / v0.01";

#endif
