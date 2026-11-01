// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// mac_vblank.hpp -- on a Mac, the screen's own vblank paces the loop.
//
// MEASURED 2026-11-01 on an M4 MacBook Air (macOS 26.6.2, SDL 3.4.18): a
// full-screen SDL Metal window with vsync ON presents about 118 times a second
// on a 60 Hz screen -- the same as with vsync off -- on the built-in screen
// and an external one alike. So nothing in Deckboy's loop waited for the
// screen. The output presented ~103 times a second, the frame scheduler timed
// frames against a ~10 ms tick that no screen has, a 60 fps clip missed 141
// refreshes a minute, and a 60 fps recording got a new picture only every
// other frame. The same window waiting for a CVDisplayLink before each present
// makes 60.0 a second on both screens.
//
// One wait per pass of the loop, at most one pass per vblank: a pass that
// overran a refresh does not wait for another one.
#pragma once

#include "core/sdl_compat.hpp"

namespace deckboy::platform {

#ifdef __APPLE__
// Wait for the next vblank of the screen `window` is on, unless one has gone
// by since the last call. False, without waiting, when there is no display
// link for it (no Cocoa window, a link that will not start, or one that has
// gone quiet -- a sleeping display), and the caller runs unpaced, as before.
bool waitForWindowVblank(SDL_Window* window, int timeoutMs);
#else
inline bool waitForWindowVblank(SDL_Window*, int) { return false; }
#endif

}  // namespace deckboy::platform
