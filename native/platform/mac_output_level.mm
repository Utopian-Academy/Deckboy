// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#import <Cocoa/Cocoa.h>

#include "platform/mac_output_level.hpp"

namespace deckboy::platform {

void keepWindowAboveMenuBar(SDL_Window* window, bool above) {
  if (!window) return;
  NSWindow* nswindow = (__bridge NSWindow*)SDL_GetPointerProperty(
    SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
  if (!nswindow) return;
  // The level SDL itself gives a focused full-screen window; kept when it is not.
  const NSWindowLevel overMenuBar = kCGMainMenuWindowLevel + 1;
  if (above) {
    if (nswindow.level != overMenuBar) nswindow.level = overMenuBar;
    // Stay put when another app is active, and on every Space.
    nswindow.hidesOnDeactivate = NO;
    nswindow.collectionBehavior |= NSWindowCollectionBehaviorCanJoinAllSpaces |
                                   NSWindowCollectionBehaviorFullScreenAuxiliary;
  } else if (nswindow.level == overMenuBar) {
    nswindow.level = kCGNormalWindowLevel;
  }
}

}  // namespace deckboy::platform
