// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// screen_colour.hpp -- PICK FROM SCREEN: take a colour from anywhere on any
// display, another app's window included, not just Deckboy's own.
//
// James, 2026-10-30: "make sure it can grab from any window, not just
// deckboy". Each platform has its own way to see past the app's windows:
//   - Windows: the desktop's DC, read where the cursor is, and a low-level
//     mouse hook so the click that takes the colour does not also land on
//     whatever app is under it.
//   - macOS: NSColorSampler (10.15+), the system's own loupe. It needs no
//     Screen Recording permission, and it draws the magnifier itself.
//   - Linux on X11: the root window, read with XGetImage, and a pointer grab
//     for the click. libX11 is opened at run time so a headless build box
//     needs none of it.
//   - Linux on Wayland: the desktop portal's PickColor, which is the only
//     thing a Wayland client is allowed to use; the compositor draws the
//     picker itself.
//
// One pick at a time. Begin it, then poll once a frame until it ends.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace deckboy::platform {

enum class ScreenPickState { Idle, Picking, Picked, Cancelled, Failed };

struct ScreenPickFrame {
  ScreenPickState state = ScreenPickState::Idle;
  // True where the system draws its own magnifier (macOS, Wayland): the app
  // then draws none.
  bool systemLoupe = false;
  // While picking without a system loupe: the 9x9 pixels round the cursor,
  // 0xRRGGBB, row by row, the centre at [40]. hasPatch is false until the
  // first read lands.
  bool hasPatch = false;
  std::array<std::uint32_t, 81> patch {};
  // The colour taken, once state is Picked; the colour under the cursor
  // while picking.
  std::uint32_t colour = 0;
  std::string error;   // why, once state is Failed
};

// Starts a pick. False, with the reason, where this machine cannot.
bool beginScreenPick(std::string* why);
// Once a frame while picking. Picked, Cancelled and Failed are reported once,
// then the picker goes back to Idle.
ScreenPickFrame pollScreenPick();
// Esc, from the app's own key handling.
void cancelScreenPick();
// The arrow keys: moves the cursor one pixel, for a pixel the hand cannot hit.
void nudgeScreenPick(int dx, int dy);
// Takes the colour under the cursor now, as a click would (Enter).
void takeScreenPick();

}  // namespace deckboy::platform
