// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "browser.hpp"

#include "core/subprocess.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace deckboy::platform::browser {
namespace {

std::string trimCopy(std::string value) {
  auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(),
                           [&](unsigned char ch) { return !isSpace(ch); }));
  value.erase(std::find_if(value.rbegin(), value.rend(),
                           [&](unsigned char ch) { return !isSpace(ch); }).base(),
              value.end());
  return value;
}

bool executableOnPath(const std::string& name) {
  if (name.empty()) {
    return false;
  }
#ifdef _WIN32
  fs::path p(name);
  if (p.has_parent_path()) {
    std::error_code ec;
    return fs::is_regular_file(p, ec) && !ec;
  }
  return true;
#else
  if (name.find('/') != std::string::npos) {
    return access(name.c_str(), X_OK) == 0;
  }
  const char* pathEnv = std::getenv("PATH");
  if (!pathEnv) {
    return false;
  }
  std::string_view pathView(pathEnv);
  size_t start = 0;
  while (start <= pathView.size()) {
    size_t end = pathView.find(':', start);
    if (end == std::string_view::npos) {
      end = pathView.size();
    }
    fs::path candidate(pathView.substr(start, end - start));
    candidate /= name;
    if (access(candidate.string().c_str(), X_OK) == 0) {
      return true;
    }
    start = end + 1;
  }
  return false;
#endif
}

std::string detectBrowserExecutable() {
  if (const char* exact = std::getenv("DECKBOY_BROWSER"); exact && *exact) {
    std::string candidate = trimCopy(exact);
    if (executableOnPath(candidate)) {
      return candidate;
    }
  }

#ifdef _WIN32
  static const std::array<std::string, 3> candidates {
    "msedge.exe",
    "chrome.exe",
    "chrome"
  };
#elif __APPLE__
  static const std::array<std::string, 3> candidates {
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
    "/Applications/Chromium.app/Contents/MacOS/Chromium"
  };
#else
  static const std::array<std::string, 7> candidates {
    "chromium",
    "chromium-browser",
    "google-chrome",
    "google-chrome-stable",
    "microsoft-edge",
    "microsoft-edge-stable",
    "chrome"
  };
#endif

  for (const auto& candidate : candidates) {
    if (executableOnPath(candidate)) {
      return candidate;
    }
  }
  return {};
}

#ifdef __linux__
fs::path nextBrowserProfilePath() {
  auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return fs::temp_directory_path() / ("deckboy-browser-" + std::to_string(static_cast<long long>(now)));
}

int findFreeVirtualDisplay() {
  for (int n = 20; n < 100; ++n) {
    fs::path lock = fs::path("/tmp") / (".X" + std::to_string(n) + "-lock");
    if (!fs::exists(lock)) {
      return n;
    }
  }
  return -1;
}
#endif

}  // namespace

class BrowserRenderer::Impl {
 public:
  std::string url_;
  std::string userAgent_;
  int width_ = 0;
  int height_ = 0;
  bool isRunning_ = false;
  bool capturePending_ = false;
  double zoomLevel_ = 1.0;
  double devicePixelRatio_ = 1.0;
  BrowserStartPhase phase_ = BrowserStartPhase::None;
  std::string lastError_;
  std::chrono::steady_clock::time_point phaseStartedAt_ {};

#ifdef __linux__
  std::string browserExecutable_;
  ChildProcess browserProcess_;
  ChildProcess xvfbProcess_;
  fs::path browserProfileDir_;
  std::string virtualDisplayId_;
#endif

  void clearFailure() {
    lastError_.clear();
  }

  void stopProcesses(bool clearError) {
#ifdef __linux__
    browserProcess_.stop();
    xvfbProcess_.stop();
    if (!browserProfileDir_.empty()) {
      std::error_code error;
      fs::remove_all(browserProfileDir_, error);
      browserProfileDir_.clear();
    }
    virtualDisplayId_.clear();
    browserExecutable_.clear();
#endif
    isRunning_ = false;
    capturePending_ = false;
    phase_ = BrowserStartPhase::None;
    if (clearError) {
      clearFailure();
    }
  }

  void failSession(const std::string& error) {
    stopProcesses(false);
    lastError_ = error;
  }
};

BrowserRenderer::BrowserRenderer(const std::string& userAgent)
  : impl_(std::make_unique<Impl>()) {
  impl_->userAgent_ = userAgent.empty()
    ? "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
    : userAgent;
}

BrowserRenderer::~BrowserRenderer() {
  stop();
}

bool BrowserRenderer::start(const std::string& url, int width, int height) {
  stop();

  impl_->url_ = trimCopy(url);
  impl_->width_ = width;
  impl_->height_ = height;
  impl_->clearFailure();

  if (impl_->url_.empty()) {
    impl_->lastError_ = "url missing";
    return false;
  }

#ifdef __linux__
  impl_->browserExecutable_ = detectBrowserExecutable();
  if (impl_->browserExecutable_.empty()) {
    impl_->lastError_ = "browser not found";
    return false;
  }

  int displayNum = findFreeVirtualDisplay();
  if (displayNum < 0) {
    impl_->lastError_ = "virtual display unavailable";
    return false;
  }

  impl_->virtualDisplayId_ = ":" + std::to_string(displayNum);
  if (!spawnDetachedProcess(impl_->xvfbProcess_, {
      "Xvfb", impl_->virtualDisplayId_,
      "-screen", "0",
      std::to_string(width) + "x" + std::to_string(height) + "x24",
      "-nolisten", "tcp"
    })) {
    impl_->virtualDisplayId_.clear();
    impl_->lastError_ = "xvfb launch failed";
    return false;
  }

  impl_->browserProfileDir_ = nextBrowserProfilePath();
  std::error_code error;
  fs::create_directories(impl_->browserProfileDir_, error);
  if (error) {
    impl_->stopProcesses(false);
    impl_->lastError_ = "profile dir unavailable";
    return false;
  }

  impl_->isRunning_ = true;
  impl_->phase_ = BrowserStartPhase::WaitXvfb;
  impl_->phaseStartedAt_ = std::chrono::steady_clock::now();
  return true;
#else
  impl_->lastError_ = "native browser backend not implemented";
  return false;
#endif
}

void BrowserRenderer::stop() {
  impl_->stopProcesses(true);
}

bool BrowserRenderer::isRunning() const {
  return impl_->isRunning_;
}

bool BrowserRenderer::isLive() const {
  return impl_->phase_ == BrowserStartPhase::Live;
}

BrowserStartPhase BrowserRenderer::phase() const {
  return impl_->phase_;
}

std::string BrowserRenderer::lastError() const {
  return impl_->lastError_;
}

bool BrowserRenderer::grabFrame(BrowserFrame& outFrame) {
  (void) outFrame;
  if (!impl_->isRunning_) {
    return false;
  }
  return false;
}

bool BrowserRenderer::consumeCaptureRequest(std::string& outSourceRef, int& outWidth, int& outHeight) {
  if (!impl_->capturePending_) {
    return false;
  }

  outSourceRef = {};
  outWidth = impl_->width_;
  outHeight = impl_->height_;

#ifdef __linux__
  outSourceRef = impl_->virtualDisplayId_;
#endif

  impl_->capturePending_ = false;
  return !outSourceRef.empty();
}

void BrowserRenderer::markCaptureStarted() {
  if (!impl_->isRunning_) {
    return;
  }
  impl_->phase_ = BrowserStartPhase::Live;
  impl_->clearFailure();
}

void BrowserRenderer::markCaptureFailed(const std::string& error) {
  impl_->failSession(error.empty() ? "capture start failed" : error);
}

bool BrowserRenderer::loadUrl(const std::string& url) {
  impl_->url_ = trimCopy(url);
  if (!impl_->isRunning_) {
    return false;
  }
  return false;
}

bool BrowserRenderer::goBack() {
  return false;
}

bool BrowserRenderer::goForward() {
  return false;
}

bool BrowserRenderer::reload() {
  return false;
}

bool BrowserRenderer::executeJavaScript(const std::string& script) {
  (void) script;
  return false;
}

void BrowserRenderer::setUserAgent(const std::string& agent) {
  impl_->userAgent_ = agent;
}

void BrowserRenderer::setZoomLevel(double scale) {
  impl_->zoomLevel_ = scale;
}

void BrowserRenderer::setDevicePixelRatio(double ratio) {
  impl_->devicePixelRatio_ = ratio;
}

void BrowserRenderer::tick() {
  if (!impl_->isRunning_ || !impl_->lastError_.empty()) {
    return;
  }

#ifdef __linux__
  auto now = std::chrono::steady_clock::now();
  auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - impl_->phaseStartedAt_).count();

  if (impl_->phase_ == BrowserStartPhase::WaitXvfb) {
    if (elapsedMs < 400) {
      return;
    }

    std::vector<std::string> args {
      impl_->browserExecutable_,
      "--no-first-run",
      "--disable-session-crashed-bubble",
      "--disable-infobars",
      "--disable-gpu",
      "--app=" + impl_->url_,
      "--window-size=" + std::to_string(impl_->width_) + "," + std::to_string(impl_->height_),
      "--window-position=0,0",
      "--user-data-dir=" + impl_->browserProfileDir_.string(),
      "--start-maximized"
    };
    std::vector<std::string> envArgs {
      "env",
      "DISPLAY=" + impl_->virtualDisplayId_,
      "LIBGL_ALWAYS_SOFTWARE=1"
    };
    envArgs.insert(envArgs.end(), args.begin(), args.end());
    if (!spawnDetachedProcess(impl_->browserProcess_, envArgs)) {
      impl_->failSession("browser launch failed");
      return;
    }

    impl_->phase_ = BrowserStartPhase::WaitChrome;
    impl_->phaseStartedAt_ = now;
    return;
  }

  if (impl_->phase_ == BrowserStartPhase::WaitChrome) {
    if (elapsedMs < 1200) {
      return;
    }
    impl_->phase_ = BrowserStartPhase::WaitCapture;
    impl_->capturePending_ = true;
    impl_->phaseStartedAt_ = now;
  }
#endif
}

}  // namespace deckboy::platform::browser
