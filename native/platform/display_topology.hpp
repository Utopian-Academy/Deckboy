// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// display_topology.hpp — Is a desktop display one screen, or several mirrored?
//
// SDL reports DESKTOPS, not panels. When the OS mirrors two screens they become
// one SDL display, so "display 2" simply disappears and a program output aimed
// at it would land on whatever is left -- usually the screen the operator is
// working on. Nothing in SDL says why the display went away. This asks the OS
// how many physical screens are showing a given desktop display.
//
//   Windows  QueryDisplayConfig: several targets sharing one source = mirrored
//   macOS    CGDisplayMirrorsDisplay over the online display list
//   X11      XRandR: several outputs on one CRTC, or CRTCs on the same rect
//   Wayland  not knowable from a client -- reported as unknown, never guessed
// ============================================================================

#pragma once

#include <string>
#include <vector>

namespace deckboy::platform {

struct DisplayMirrorInfo {
  bool known = false;                    // false = this platform/session cannot tell
  int panelCount = 1;                    // physical screens showing this display
  std::vector<std::string> otherPanels;  // the screens beyond the one SDL names
};

// `x,y,w,h` are the display's SDL bounds; `sdlName` is SDL's name for it, so
// the panel SDL already names is not listed again as a mirror of itself.
DisplayMirrorInfo queryDisplayMirrorInfo(int x, int y, int w, int h,
                                         const char* sdlName);

}  // namespace deckboy::platform
