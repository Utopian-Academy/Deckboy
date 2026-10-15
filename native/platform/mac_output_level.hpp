// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// mac_output_level.hpp -- a full-screen output stays over the macOS menu bar,
// whatever has focus.
//
// James, 2026-10-15: on a Mac, clicking anywhere away from the output made it
// show the menu bar and leave full screen -- "show destroying". Two causes,
// both in SDL's Cocoa backend once outputs stopped using Spaces (45dc630):
//   - a full-screen window that loses focus is MINIMISED unless
//     SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS is "0", and that hint was set
//     inside the Windows-only block;
//   - on focus loss SDL also drops the window to the normal level
//     (Cocoa_SetWindowMouseGrab), under the menu bar and under other apps.
// The hint is now set everywhere; this puts the level back.
#pragma once

#include "core/sdl_compat.hpp"

namespace deckboy::platform {

#ifdef __APPLE__
// above=true raises the window over the menu bar and keeps it there when the
// app is not active; above=false undoes only what this raised.
void keepWindowAboveMenuBar(SDL_Window* window, bool above);
#else
inline void keepWindowAboveMenuBar(SDL_Window*, bool) {}
#endif

}  // namespace deckboy::platform
