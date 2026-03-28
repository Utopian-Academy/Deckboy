// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace deckboy::platform::browser {

enum class BrowserStartPhase { None, WaitXvfb, WaitChrome, WaitCapture, Live };

// Decoded browser frame (same as video frame)
struct BrowserFrame {
  std::vector<std::uint8_t> rgba;
  int width = 0;
  int height = 0;
};

// Cross-platform browser renderer
// - Linux: Xvfb + x11grab + ffmpeg
// - macOS: WKWebView with offscreen rendering
// - Windows: Windows.Foundation.WebView2 (Chromium-based)
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
