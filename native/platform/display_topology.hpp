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

#include <cstdint>
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

// ============================================================================
// And a second question: how does an output window's picture reach its
// screen, and on whose clock?
//
// Every renderer Deckboy uses on Windows keeps ONE frame in flight: SDL 3.4's
// Direct3D 11 backend calls SetMaximumFrameLatency(1) on a two-buffer flip
// chain, and the Direct3D 12 and GPU renderers do the same. So each present
// waits until whatever consumes that window's frames has taken the last one.
// A window flipped straight to its screen (independent flip) is consumed at
// that screen's vblank. A COMPOSED window is consumed by the desktop
// compositor (DWM), on DWM's clock -- and with screens at different rates
// that clock follows one screen, not each window's own.
//
// That is the reading of what was measured on 2026-11-01 (Windows, desk on a
// 60 Hz monitor, the output on a 59.94 Hz HDMI capture dongle): the loop
// ticked at 60 with the output on the 59.94 Hz screen, the lock to the screen
// measured that screen as 60, and one output present in about 17 s
// (1 / 0.06 Hz, the beat of the two clocks) came a refresh late.
// A present with vsync on and one frame in flight cannot run faster than the
// screen that consumes it, so that output was being consumed at 60 -- by the
// compositor. These answer why, without a tool: which clock DWM keeps, whether
// the GPU that draws the window is the one that drives its screen (a frame
// that has to cross to another GPU is always composed), whether anything sits
// over it, and the swap chain's own frame statistics. Nothing here changes how
// anything is drawn.
// ============================================================================
struct PresentPathInfo {
  bool known = false;             // false: not Windows, or no window handle
  double compositorHz = 0.0;      // DWM's composition clock; 0 = unknown
  int sameAdapter = -1;           // 1: the GPU it draws on drives its screen; 0: another does; -1: unknown
  std::string drawAdapter;        // the GPU the swap chain renders on
  std::string screenAdapter;      // the GPU driving the window's screen
  int dxgiMode = -1;              // DXGI_FRAME_PRESENTATION_MODE (IDXGISwapChainMedia); -1 = unavailable
  int coversScreen = -1;          // 1: the window is exactly its screen's rectangle
  int windowsAbove = -1;          // visible windows above it in z-order that overlap its screen
  std::string windowsAboveTitles; // their titles, " | " between, capped
};

// One sample of a swap chain's frame statistics (IDXGISwapChain::
// GetFrameStatistics): the last present the screen showed, the refresh it was
// shown on, and a refresh the scheduler stamped with QueryPerformanceCounter.
// Cheap enough to take after every present while tracing.
struct PresentStats {
  bool valid = false;
  std::uint32_t lastPresentCount = 0;     // Present calls so far (GetLastPresentCount)
  std::uint32_t presentCount = 0;         // the last of them a refresh showed
  std::uint32_t presentRefreshCount = 0;  // the refresh that showed it
  std::uint32_t syncRefreshCount = 0;     // a refresh the scheduler sampled...
  std::int64_t syncQpcTime = 0;           // ...and when, in QueryPerformanceCounter ticks
};

// DWM's own timing (DwmGetCompositionTimingInfo): its clock, its last vblank
// and its frame counters, so a trace can see a composition DWM skipped.
struct CompositorTiming {
  bool valid = false;
  double hz = 0.0;
  std::int64_t vblankQpc = 0;        // qpcVBlank: the time of its last vblank
  std::uint64_t refreshCount = 0;    // cRefresh: vblanks counted
  std::uint64_t frameCount = 0;      // cFrame: frames composed
  std::uint64_t framesDropped = 0;   // cFramesDropped
  std::uint64_t framesMissed = 0;    // cFramesMissed
};

CompositorTiming queryCompositorTiming();

// DWM's composition clock in Hz, 0 where there is none to ask (not Windows).
double compositorRefreshHz();

// `hwnd` is the window's HWND; `d3d11Device` and `dxgiSwapChain` are SDL's
// SDL_PROP_RENDERER_D3D11_DEVICE_POINTER / _SWAPCHAIN_POINTER, null for any
// other renderer (the adapter and DXGI mode are then left unknown).
// `windowsAbove` walks the windows over it, a call into DWM for each, so the
// caller asks only when there is something to explain (else -1).
PresentPathInfo queryPresentPath(void* hwnd, void* d3d11Device, void* dxgiSwapChain, bool windowsAbove);

PresentStats queryPresentStats(void* dxgiSwapChain);

// "composed", "overlay", "none", "failure", or "unknown" for -1.
const char* dxgiPresentationModeName(int mode);

}  // namespace deckboy::platform
