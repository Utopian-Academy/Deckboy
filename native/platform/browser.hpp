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

  // Configuration
  void setUserAgent(const std::string& agent);
  void setZoomLevel(double scale);
  void setDevicePixelRatio(double ratio);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  friend class Impl;
};

}  // namespace deckboy::platform::browser
