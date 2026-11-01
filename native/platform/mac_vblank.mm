// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#import <Cocoa/Cocoa.h>
#import <CoreVideo/CoreVideo.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "platform/mac_vblank.hpp"

// CVDisplayLink is deprecated from the macOS 15 SDK in favour of
// NSScreen.displayLink, which needs macOS 14; Deckboy runs from 12.0, and
// CVDisplayLink works on all of them.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace deckboy::platform {
namespace {

struct Pacer {
  CVDisplayLinkRef link = nullptr;
  CGDirectDisplayID display = 0;
  std::mutex mutex;
  std::condition_variable tick;
  std::uint64_t vblanks = 0;    // counted on the display link's thread
  std::uint64_t consumed = 0;   // the last one a pass of the loop took
  std::chrono::steady_clock::time_point quietUntil {};
};

// Never destroyed: the link's thread can still call in while the app exits.
Pacer& pacer() {
  static Pacer* instance = new Pacer;
  return *instance;
}

CVReturn onVblank(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*,
                  CVOptionFlags, CVOptionFlags*, void* context) {
  auto* p = static_cast<Pacer*>(context);
  {
    std::lock_guard<std::mutex> lock(p->mutex);
    ++p->vblanks;
  }
  p->tick.notify_all();
  return kCVReturnSuccess;
}

CGDirectDisplayID displayOf(SDL_Window* window) {
  NSWindow* nswindow = (__bridge NSWindow*)SDL_GetPointerProperty(
    SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
  NSScreen* screen = nswindow ? nswindow.screen : nil;
  NSNumber* number = screen ? screen.deviceDescription[@"NSScreenNumber"] : nil;
  return number ? static_cast<CGDirectDisplayID>(number.unsignedIntValue) : 0;
}

}  // namespace

bool waitForWindowVblank(SDL_Window* window, int timeoutMs) {
  if (!window) return false;
  Pacer& p = pacer();
  const auto now = std::chrono::steady_clock::now();
  if (now < p.quietUntil) return false;
  const CGDirectDisplayID display = displayOf(window);
  if (display == 0) return false;
  if (display != p.display || !p.link) {
    // A new screen: its own link, counting from nothing.
    if (p.link) {
      CVDisplayLinkStop(p.link);
      CVDisplayLinkRelease(p.link);
      p.link = nullptr;
    }
    p.display = 0;
    CVDisplayLinkRef link = nullptr;
    if (CVDisplayLinkCreateWithCGDisplay(display, &link) != kCVReturnSuccess || !link) {
      p.quietUntil = now + std::chrono::seconds(1);
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(p.mutex);
      p.vblanks = 0;
      p.consumed = 0;
    }
    CVDisplayLinkSetOutputCallback(link, onVblank, &p);
    if (CVDisplayLinkStart(link) != kCVReturnSuccess) {
      CVDisplayLinkRelease(link);
      p.quietUntil = now + std::chrono::seconds(1);
      return false;
    }
    p.link = link;
    p.display = display;
  }
  std::unique_lock<std::mutex> lock(p.mutex);
  if (p.vblanks == p.consumed &&
      !p.tick.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                       [&] { return p.vblanks != p.consumed; })) {
    // A link that has gone quiet (the display asleep) must not hold the loop
    // to one pass per timeout: run unpaced for a second, then look again.
    p.quietUntil = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    return false;
  }
  p.consumed = p.vblanks;
  return true;
}

}  // namespace deckboy::platform

#pragma clang diagnostic pop
