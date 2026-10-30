// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// screen_colour_mac.mm -- pick from screen on the Mac: NSColorSampler, the
// loupe the system's own colour panel uses. It reads every window on every
// display without Screen Recording permission, and handles Esc and the click
// itself. macOS 10.15 and later; an older Mac is told so.

#include "platform/screen_colour.hpp"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <mutex>

namespace deckboy::platform {

namespace {
std::mutex gMutex;
ScreenPickState gState = ScreenPickState::Idle;
std::uint32_t gResult = 0;
id gSampler = nil;   // held until the handler runs
}  // namespace

bool beginScreenPick(std::string* why) {
  if (@available(macOS 10.15, *)) {
    {
      std::lock_guard<std::mutex> lock(gMutex);
      if (gState == ScreenPickState::Picking) return true;
      gState = ScreenPickState::Picking;
    }
    NSColorSampler* sampler = [[NSColorSampler alloc] init];
    gSampler = sampler;
    [sampler showSamplerWithSelectionHandler:^(NSColor* picked) {
      std::lock_guard<std::mutex> lock(gMutex);
      gSampler = nil;
      if (gState != ScreenPickState::Picking) return;
      NSColor* srgb = picked ? [picked colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] : nil;
      if (!srgb) {
        gState = ScreenPickState::Cancelled;
        return;
      }
      const auto byte = [](CGFloat v) {
        return static_cast<std::uint32_t>(std::clamp(static_cast<double>(v), 0.0, 1.0) * 255.0 + 0.5);
      };
      gResult = (byte(srgb.redComponent) << 16) | (byte(srgb.greenComponent) << 8) | byte(srgb.blueComponent);
      gState = ScreenPickState::Picked;
    }];
    return true;
  }
  if (why) *why = "needs macOS 10.15 or later";
  return false;
}

ScreenPickFrame pollScreenPick() {
  std::lock_guard<std::mutex> lock(gMutex);
  ScreenPickFrame frame;
  frame.state = gState;
  frame.systemLoupe = true;
  frame.colour = gResult;
  if (gState != ScreenPickState::Picking) gState = ScreenPickState::Idle;
  return frame;
}

// The system loupe owns Esc and the click; a cancel from the app only stops
// listening, and the loupe goes when the operator next clicks or presses Esc.
void cancelScreenPick() {
  std::lock_guard<std::mutex> lock(gMutex);
  if (gState == ScreenPickState::Picking) gState = ScreenPickState::Cancelled;
}

void nudgeScreenPick(int, int) {}
void takeScreenPick() {}

}  // namespace deckboy::platform
