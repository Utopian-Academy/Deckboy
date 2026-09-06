// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// browser.hpp — Cross-platform browser renderer for Browser and LowerThird cues.
//
// BrowserRenderer wraps a headless browser engine to render web content into
// raw RGBA frames that MediaEngine can display. This enables HTML/CSS/JS-based
// cues: lower thirds, tickers, web pages, interactive overlays.
//
// Platform implementations:
//   Linux:   Xvfb (virtual X server) + Chromium + x11grab/ffmpeg pipeline
//   macOS:   WKWebView with offscreen rendering (scaffold)
//   Windows: WebView2 (Edge/Chromium, offscreen via PrintWindow)
//
// Lifecycle:
//   1. Construct with optional user agent
//   2. start(url, w, h) — launches the browser backend
//   3. tick() each frame to advance the browser lifecycle state machine
//   4. grabFrame() to get the latest rendered RGBA frame
//   5. stop() or destructor to tear down
//
// The capture handoff system (consumeCaptureRequest / markCaptureStarted)
// coordinates between the browser backend (which determines the capture
// source) and MediaEngine (which does the actual screen capture via ffmpeg).
//
// Implementation: browser.cpp (pimpl pattern, platform-specific Impl class)
// ============================================================================

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace deckboy::platform::browser {

// State machine phases for the browser startup sequence.
// Linux startup is multi-step (Xvfb → Chrome → capture handoff → live).
// Windows/macOS go directly from None → Live.
enum class BrowserStartPhase { None, WaitXvfb, WaitChrome, WaitCapture, Live };

// Decoded browser frame (same as video frame)
struct BrowserFrame {
  std::vector<std::uint8_t> rgba;
  int width = 0;
  int height = 0;
};

// Cross-platform browser renderer
// - Linux: Xvfb + Chromium + x11grab/ffmpeg pipeline
// - macOS: WKWebView with offscreen rendering (scaffold)
// - Windows: WebView2 (Edge/Chromium, offscreen via PrintWindow)
class BrowserRenderer {
 public:
  explicit BrowserRenderer(const std::string& userAgent = {});
  ~BrowserRenderer();

  // Prevent copying
  BrowserRenderer(const BrowserRenderer&) = delete;
  BrowserRenderer& operator=(const BrowserRenderer&) = delete;

  // Lifecycle
  bool start(const std::string& url, int width, int height);
  void stop();
  void tick();
  bool isRunning() const;
  bool isLive() const;
  BrowserStartPhase phase() const;
  std::string lastError() const;

  // Frame capture (call each frame)
  bool grabFrame(BrowserFrame& outFrame);

  // Capture handoff
  bool consumeCaptureRequest(std::string& outSourceRef, int& outWidth, int& outHeight);
  void markCaptureStarted();
  void markCaptureFailed(const std::string& error);

  // Navigation
  bool loadUrl(const std::string& url);
  bool goBack();
  bool goForward();
  bool reload();

  // Script execution
  bool executeJavaScript(const std::string& script);

  // ---- Driving the page ----------------------------------------------------
  //
  // All three are JavaScript, because that is the one input path every backend
  // already has -- WebView2 on Windows, WKWebView on macOS, Chromium on Linux.
  // Synthetic DOM events reach page CONTENT (banners, consent buttons, links,
  // form controls), which is what a cue actually needs; they do not drive the
  // browser's own chrome, and nothing here pretends otherwise.

  // Hide the scrollbars without making the page unscrollable. A cue is a
  // picture on a screen -- a scrollbar down its edge is furniture the audience
  // should never see, and scrollBy below is how it moves instead.
  bool setScrollbarsVisible(bool visible);

  // Scroll the page. Pixels, positive dy is down.
  bool scrollBy(int dx, int dy);

  // Click at a point given as a FRACTION of the rendered frame (0..1), so the
  // caller does not have to know the browser's pixel size -- it clicks where
  // the operator clicked in the preview.
  bool clickAtFraction(double fx, double fy);

  // ---- HAND THE PAGE OVER TO THE OPERATOR --------------------------------
  //
  // The approach Mitti takes, and it is the right one: rather than synthesise
  // input, SHOW the real browser window and let the operator use it with a
  // real mouse and keyboard -- cookie banners, consent dialogs, and the one
  // thing no amount of synthetic clicking can do, which is LOG IN. Typing a
  // password, a 2FA code, or picking from a password manager all need a real
  // window; a fabricated click event does not get you there.
  //
  // Offscreen is the normal state and the cue keeps rendering throughout; this
  // just stops hiding the window that was always there.
  bool setInteractive(bool interactive);
  bool isInteractive() const;

  // Configuration
  void setUserAgent(const std::string& agent);
  void setZoomLevel(double scale);
  void setDevicePixelRatio(double ratio);

 private:
#if defined(__APPLE__)
  // One line down the helper's stdin. macOS only, where the page lives in the
  // deckboy-webview child process.
  bool sendHelperCommand(const std::string& command);
#endif

  class Impl;
  std::unique_ptr<Impl> impl_;

  friend class Impl;
};

}  // namespace deckboy::platform::browser
