// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// browser.cpp — Headless browser renderer for Browser and LowerThird cues.
//
// Renders HTML/CSS content to raw RGBA pixel frames for compositing into the
// deck output. Uses platform-specific approaches:
//
//   Linux:   Headless Chromium via Xvfb virtual framebuffer
//            - Spawns Xvfb on a free display, then launches Chromium with
//              --headless --screenshot flags, capturing screenshots at interval
//            - Detects browser executable via DECKBOY_BROWSER env var or
//              well-known paths (chromium, google-chrome, firefox)
//
//   Windows: Microsoft WebView2 (Edge Chromium runtime)
//            - Creates an offscreen HWND hosting WebView2 control
//            - Captures frames via WIC (Windows Imaging Component) screenshot
//            - Requires WebView2 runtime (ships with Windows 10/11)
//
//   macOS:   WKWebView (scaffold — not yet implemented)
//
// The renderer produces RGBA frames that are pushed to the MediaEngine via
// the pushBrowserFrame() callback, which then uploads them as SDL_Textures.
// Frame capture runs on a background thread at a configurable interval.
//
// Key helpers:
//   detectBrowserExecutable() — finds a usable browser binary on the system
//   executableOnPath()        — checks if a binary exists in PATH
//
// Header: browser.hpp
// Used by: media_engine.cpp (startBrowserCapture/stopBrowserCapture).
// ============================================================================

#include "browser.hpp"

#include "core/subprocess.hpp"
// Not macOS-only any more: the warning card for an unreachable page is a
// bundled data file, and every platform has to be able to find it.
#include "core/paths.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <system_error>
#include <thread>

#if defined(__linux__) && defined(DECKBOY_HAS_XTEST)
#include <X11/Xlib.h>
#include <X11/XKBlib.h>          // XkbKeycodeToKeysym, for typing
#include <X11/keysym.h>          // XK_Shift_L
#include <X11/extensions/XTest.h>
// X11 defines these as bare macros, and they collide with ordinary C++ names
// -- `None` is an enumerator of BrowserStartPhase, and `Status` is a word any
// codebase will reach for eventually. Undefined immediately so the pollution
// cannot travel past this include; the XTEST calls below use plain 1/0 rather
// than X's True/False so they do not depend on what survives.
#undef None
#undef Status
#undef Success
#undef Always
#undef Bool
#endif

#ifdef _WIN32
#ifdef DECKBOY_HAS_WEBVIEW
#include <wrl/client.h>
#include <wrl/event.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <wincodec.h>
#include <dwmapi.h>
#pragma comment(lib, "windowscodecs.lib")
#endif
#else
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
  // Check well-known installation paths first (Edge ships with Windows 10/11)
  {
    std::vector<std::string> wellKnown {
      "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
      "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
      "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
      "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
    };
    if (const char* localAppData = std::getenv("LOCALAPPDATA"); localAppData && *localAppData) {
      std::string base(localAppData);
      wellKnown.push_back(base + "\\Microsoft\\Edge\\Application\\msedge.exe");
      wellKnown.push_back(base + "\\Google\\Chrome\\Application\\chrome.exe");
    }
    for (const auto& p : wellKnown) {
      std::error_code ec;
      if (fs::is_regular_file(fs::path(p), ec) && !ec) {
        return p;
      }
    }
  }
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

fs::path nextBrowserProfilePath() {
  auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return fs::temp_directory_path() / ("deckboy-browser-" + std::to_string(static_cast<long long>(now)));
}

#if defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
static std::wstring utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (wlen <= 1) return {};
  std::wstring w(static_cast<size_t>(wlen - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), wlen);
  return w;
}

static void registerBrowserHostClass() {
  static std::once_flag s_flag;
  std::call_once(s_flag, []() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DeckboyBrowserHost";
    RegisterClassExW(&wc);
  });
}

// Custom thread messages for the WebView2 STA message loop
static constexpr UINT WM_WV2_NAVIGATE      = WM_USER + 1;
static constexpr UINT WM_WV2_RELOAD        = WM_USER + 2;
static constexpr UINT WM_WV2_EXEC_JS       = WM_USER + 3;
static constexpr UINT WM_WV2_CLOSE_AND_QUIT = WM_USER + 4;
// Show or hide the host window for hands-on interaction. Handled on the STA
// thread that owns the window, like every other message here -- SetWindowPos
// from another thread on a window owned by a message pump is a deadlock
// waiting for a quiet afternoon.
static constexpr UINT WM_WV2_SET_INTERACTIVE = WM_USER + 5;
// WM_TIMER timer-id for the ~30fps CapturePreview loop
static constexpr UINT_PTR WV2_CAPTURE_TIMER_ID = 1;

// Lightweight append-only debug log written to the user's temp directory.
// Only active in this build; remove once the WebView2 capture issues are resolved.
static void wv2Log(const char* fmt, ...) {
  static char s_path[MAX_PATH] = {};
  if (s_path[0] == '\0') {
    char tmp[MAX_PATH] = {};
    if (GetTempPathA(MAX_PATH, tmp) > 0) {
      snprintf(s_path, MAX_PATH, "%swv2debug.log", tmp);
    } else {
      strncpy(s_path, "C:\\wv2debug.log", MAX_PATH - 1);
    }
  }
  FILE* f = fopen(s_path, "a");
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

// Decode a PNG IStream (from ICoreWebView2::CapturePreview) to packed RGBA bytes.
// Called on the WebView2 STA thread inside a CapturePreview completion handler.
static HRESULT decodeWicPngToRgba(IStream* stream, int& outW, int& outH,
                                   std::vector<uint8_t>& outRgba) {
  LARGE_INTEGER zero = {};
  stream->Seek(zero, STREAM_SEEK_SET, nullptr);

  Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
  if (FAILED(hr)) return hr;

  Microsoft::WRL::ComPtr<IWICBitmapDecoder> dec;
  hr = wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &dec);
  if (FAILED(hr)) return hr;

  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  hr = dec->GetFrame(0, &frame);
  if (FAILED(hr)) return hr;

  UINT fw = 0, fh = 0;
  frame->GetSize(&fw, &fh);
  if (fw == 0 || fh == 0) return E_FAIL;

  Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
  hr = wic->CreateFormatConverter(&conv);
  if (FAILED(hr)) return hr;
  hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                        WICBitmapDitherTypeNone, nullptr, 0.0,
                        WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) return hr;

  const UINT stride = fw * 4;
  outRgba.resize(static_cast<size_t>(stride) * fh);
  hr = conv->CopyPixels(nullptr, stride, static_cast<UINT>(outRgba.size()), outRgba.data());
  if (FAILED(hr)) { outRgba.clear(); return hr; }

  outW = static_cast<int>(fw);
  outH = static_cast<int>(fh);
  return S_OK;
}

#endif

#ifdef __linux__
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

// THE CARD A BROWSER CUE SHOWS WHEN THE PAGE IS NOT THERE.
//
// Every backend's own error page is a different shape, in a different language,
// and none of them say which cue is at fault -- and a browser cue that fails
// quietly is worse: it is a black rectangle, indistinguishable from broken. So
// a failed navigation lands here instead: one card, the same on all three
// platforms, naming the address that would not load.
//
// The failed address travels as the URL fragment, so the card needs nothing
// from the app beyond being opened.
std::string unreachableCardUrl(const std::string& failedUrl) {
  std::error_code error;
  const fs::path card =
    deckboy::core::Paths::dataDir() / "browser" / "unreachable.html";
  if (!fs::exists(card, error)) {
    return {};
  }
  std::string out = "file://" + fs::absolute(card, error).string();
  if (!failedUrl.empty()) {
    out += "#";
    // Percent-encode everything that is not plainly safe: an address with a
    // '#' of its own would otherwise truncate the fragment it is being carried
    // in, and the card would name the wrong page.
    for (unsigned char c : failedUrl) {
      const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                        c == '.' || c == '~';
      if (safe) {
        out.push_back(static_cast<char>(c));
      } else {
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%%%02X", c);
        out += buf;
      }
    }
  }
  return out;
}

// A failure while SHOWING the card must not send us back to the card.
bool isUnreachableCard(const std::string& url) {
  return url.find("browser/unreachable.html") != std::string::npos ||
         url.find("browser\\unreachable.html") != std::string::npos;
}

}  // namespace

class BrowserRenderer::Impl {
 public:
  std::string url_;
  std::string userAgent_;
  int width_ = 0;
  int height_ = 0;
  bool isRunning_ = false;
  bool capturePending_ = false;
  // Scrollbars are chrome, and a cue is a picture an audience sees. Hidden
  // unless the operator asks otherwise; on Linux this becomes a launch flag.
  bool hideScrollbars_ = true;
  // True while the real browser window is on screen for the operator to use.
  bool interactive_ = false;
  double zoomLevel_ = 1.0;
  double devicePixelRatio_ = 1.0;
  BrowserStartPhase phase_ = BrowserStartPhase::None;
  std::string lastError_;
  std::chrono::steady_clock::time_point phaseStartedAt_ {};

#ifdef __linux__
  std::string browserExecutable_;
  ChildProcess browserProcess_;
  ChildProcess xvfbProcess_;
  // The hands-on window, on the operator's own display. Separate from the cue
  // browser so closing it cannot take the cue off air.
  ChildProcess interactiveProcess_;
  fs::path browserProfileDir_;
  std::string virtualDisplayId_;
#elif defined(__APPLE__)
  // The deckboy-webview helper: a WKWebView in a parked window, spoken to over
  // its stdin and captured by window id like any other window source.
  ChildProcess webviewProcess_;
  std::string webviewWindowId_;
  // "window:<CGWindowID>", handed to the capture layer. Declared per-arm
  // because each backend means something different by it -- the non-WebView
  // Windows path uses a screen region, this one a window.
  std::string captureSourceRef_;
#elif defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
  // WebView2 offscreen rendering via CapturePreview (WIC PNG decode)
  std::thread wv2Thread_;
  std::atomic<bool> captureInFlight_ {false};  // prevents overlapping CapturePreview calls
  std::atomic<bool> hasFirstFrame_ {false};     // set on first successful CapturePreview decode
  std::atomic<bool> wv2Initialized_ {false};
  std::atomic<bool> wv2Failed_ {false};
  std::string wv2ErrorMsg_;
  HWND hostHwnd_ = nullptr;
  std::atomic<DWORD> wv2ThreadId_ {0};
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
  Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
  int captureW_ = 0, captureH_ = 0;
  std::mutex frameMutex_;
  std::vector<uint8_t> latestRgba_;
  bool frameReady_ = false;
  fs::path browserProfileDir_;
  std::mutex navigateMutex_;
  std::wstring pendingNavigateUrl_;
  std::wstring pendingScript_;
  // STA-thread-only (no atomic needed): counts timer ticks captureInFlight_ has
  // been stuck so we can force-reset it after ~5 s.
  int captureHangTicks_ = 0;
  // Token for the add_NavigationCompleted registration; cleared on teardown.
  EventRegistrationToken navCompletedToken_ {};
#elif defined(_WIN32)
  ChildProcess browserProcess_;
  fs::path browserProfileDir_;
  std::string captureSourceRef_;
#endif

  void clearFailure() {
    lastError_.clear();
  }

  void stopProcesses(bool clearError) {
#if defined(__APPLE__)
    // Closing its stdin is the polite stop -- the helper reads EOF and
    // terminates itself, which lets WebKit tear down in its own time. stop()
    // is the guarantee behind it.
    webviewProcess_.stop();
    webviewWindowId_.clear();
#elif defined(__linux__)
    // The hands-on window first: it holds the profile directory open, and the
    // profile is deleted below.
    interactiveProcess_.stop();
    browserProcess_.stop();
    xvfbProcess_.stop();
    if (!browserProfileDir_.empty()) {
      std::error_code error;
      fs::remove_all(browserProfileDir_, error);
      browserProfileDir_.clear();
    }
    virtualDisplayId_.clear();
    browserExecutable_.clear();
#elif defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
    // Wait briefly if the thread hasn't registered its ID yet (race on fast stop).
    for (int i = 0; i < 100 && wv2ThreadId_.load() == 0 && !wv2Failed_.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Wait for WebView2 environment/controller creation to complete or fail before
    // posting the close message. If WM_WV2_CLOSE_AND_QUIT arrives while
    // CreateCoreWebView2EnvironmentWithOptions callbacks are still in the pump queue,
    // the host window is destroyed before they fire, which can crash.
    // In practice WebView2 initializes in ~1s, so this loop exits quickly.
    for (int i = 0; i < 300 && wv2ThreadId_.load() != 0
                             && !wv2Initialized_.load()
                             && !wv2Failed_.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Wait for any in-flight CapturePreview to complete before sending the close
    // message. controller_->Close() called while a capture is in-flight can crash.
    for (int i = 0; i < 50 && captureInFlight_.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (wv2ThreadId_.load() != 0) {
      // WM_WV2_CLOSE_AND_QUIT: Close() inside the pump, then PostQuitMessage.
      // Posting WM_QUIT directly bypasses Close() and crashes on teardown.
      PostThreadMessageW(wv2ThreadId_.load(), WM_WV2_CLOSE_AND_QUIT, 0, 0);
    }
    if (wv2Thread_.joinable()) wv2Thread_.join();
    wv2ThreadId_ = 0;
    wv2Initialized_.store(false);
    wv2Failed_.store(false);
    wv2ErrorMsg_.clear();
    hostHwnd_ = nullptr;
    if (!browserProfileDir_.empty()) {
      std::error_code error;
      fs::remove_all(browserProfileDir_, error);
      browserProfileDir_.clear();
    }
    captureInFlight_.store(false);
    hasFirstFrame_.store(false);
    captureHangTicks_ = 0;
    navCompletedToken_ = {};
    { std::lock_guard<std::mutex> lk(frameMutex_); latestRgba_.clear(); frameReady_ = false; }
#elif defined(_WIN32)
    browserProcess_.stop();
    if (!browserProfileDir_.empty()) {
      std::error_code error;
      fs::remove_all(browserProfileDir_, error);
      browserProfileDir_.clear();
    }
    captureSourceRef_.clear();
#endif
    isRunning_ = false;
    capturePending_ = false;
    interactive_ = false;
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
#elif defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
  impl_->captureW_ = width;
  impl_->captureH_ = height;
  impl_->browserProfileDir_ = nextBrowserProfilePath();
  {
    std::error_code err;
    fs::create_directories(impl_->browserProfileDir_, err);
    if (err) {
      impl_->lastError_ = "profile dir unavailable";
      return false;
    }
  }

  wv2Log("start() called url=%s w=%d h=%d", impl_->url_.c_str(), width, height);
  Impl* p = impl_.get();
  std::wstring urlW = utf8ToWide(impl_->url_);
  std::wstring profileW = utf8ToWide(impl_->browserProfileDir_.string());

  impl_->wv2Thread_ = std::thread([p, urlW, profileW, width, height]() {
    wv2Log("wv2 thread started, tid=%lu", GetCurrentThreadId());
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    p->wv2ThreadId_ = GetCurrentThreadId();

    registerBrowserHostClass();
    // WebView2 uses DirectComposition for rendering. Constraints observed:
    //   - Off-screen at negative coords → no DComp surface → 0x8007139F.
    //   - WS_EX_LAYERED is incompatible with WebView2's DComp pipeline.
    //   - SW_HIDE, 1×1 size, WS_EX_TRANSPARENT all corrupt DComp state,
    //     causing crashes in get_IsVisible on the first capture attempt.
    //
    // Working config: full-size WS_POPUP | WS_VISIBLE at (0,0), pushed to
    // HWND_BOTTOM so every real window stays on top. WS_EX_TOOLWINDOW keeps
    // it out of taskbar/Alt+Tab. WS_EX_NOACTIVATE prevents focus theft.
    HWND hwnd = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      L"DeckboyBrowserHost", L"",
      WS_POPUP | WS_VISIBLE,
      0, 0, width, height,
      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    if (!hwnd) {
      p->wv2ErrorMsg_ = "host window creation failed";
      p->wv2Failed_.store(true);
      CoUninitialize();
      return;
    }
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    p->hostHwnd_ = hwnd;

    auto envOpts = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, profileW.c_str(), envOpts.Get(),
      Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [p, hwnd, urlW, width, height](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
          if (FAILED(hr) || !env) {
            wv2Log("env failed hr=0x%08lx", (unsigned long)hr);
            p->wv2ErrorMsg_ = "WebView2 environment failed — is Edge installed?";
            p->wv2Failed_.store(true);
            PostQuitMessage(0);
            return S_OK;
          }
          wv2Log("env ready, creating controller");
          env->CreateCoreWebView2Controller(hwnd,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
              [p, urlW, width, height](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                if (FAILED(hr) || !ctrl) {
                  wv2Log("ctrl failed hr=0x%08lx", (unsigned long)hr);
                  p->wv2ErrorMsg_ = "WebView2 controller failed";
                  p->wv2Failed_.store(true);
                  PostQuitMessage(0);
                  return S_OK;
                }
                // The controller is passed [in] (borrowed) — must AddRef to
                // keep it alive after this callback returns. Use ComPtr
                // assignment (calls AddRef) not Attach (no AddRef).
                p->controller_ = ctrl;
                ICoreWebView2* wv = nullptr;
                ctrl->get_CoreWebView2(&wv);
                if (!wv) {
                  wv2Log("get_CoreWebView2 returned null");
                  p->wv2ErrorMsg_ = "get_CoreWebView2 returned null";
                  p->wv2Failed_.store(true);
                  PostQuitMessage(0);
                  return S_OK;
                }
                // get_CoreWebView2 returns an [out] AddRef'd pointer — Attach
                // takes ownership of that ref without a redundant AddRef.
                p->webview_.Attach(wv);
                RECT bounds {0, 0, width, height};
                HRESULT hrBounds = ctrl->put_Bounds(bounds);
                HRESULT hrVis   = ctrl->put_IsVisible(TRUE);
                wv2Log("put_Bounds hr=0x%08lx  put_IsVisible hr=0x%08lx",
                       (unsigned long)hrBounds, (unsigned long)hrVis);
                // Lock rasterization to 1:1 so CapturePreview returns a PNG at the
                // logical viewport size, not 2x/3x on HiDPI displays.
                Microsoft::WRL::ComPtr<ICoreWebView2Controller3> ctrl3;
                if (SUCCEEDED(ctrl->QueryInterface(IID_PPV_ARGS(&ctrl3)))) {
                  ctrl3->put_RasterizationScale(1.0);
                }
                // Log NavigationStarting to confirm navigation begins.
                EventRegistrationToken navStartToken = {};
                p->webview_->add_NavigationStarting(
                  Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                    [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*) -> HRESULT {
                      wv2Log("NavigationStarting");
                      return S_OK;
                    }
                  ).Get(),
                  &navStartToken
                );
                // Reset hang state whenever a navigation finishes so a stale
                // captureInFlight_ from the previous page never blocks the new one.
                p->webview_->add_NavigationCompleted(
                  Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                    [p](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                      BOOL success = FALSE;
                      if (args) args->get_IsSuccess(&success);
                      wv2Log("NavigationCompleted success=%d", (int)success);
                      // A page that did not load leaves the last picture up --
                      // or nothing at all, which on air is a black rectangle
                      // nobody can tell from a broken cue. Say so instead.
                      if (!success && !isUnreachableCard(p->url_)) {
                        const std::string card = unreachableCardUrl(p->url_);
                        if (!card.empty()) {
                          wv2Log("navigating to the unreachable card");
                          p->webview_->Navigate(utf8ToWide(card).c_str());
                        }
                      }
                      p->captureHangTicks_ = 0;
                      // If a CapturePreview was stuck in-flight during navigation,
                      // clear it now so the next timer tick can issue a fresh one.
                      p->captureInFlight_.store(false);
                      return S_OK;
                    }
                  ).Get(),
                  &p->navCompletedToken_
                );
                // Inject CSS on every document load to hide scrollbars.
                p->webview_->AddScriptToExecuteOnDocumentCreated(
                  L"(function(){"
                  L"var s=document.createElement('style');"
                  L"s.textContent='::-webkit-scrollbar{display:none!important}';"
                  L"document.documentElement.appendChild(s);"
                  L"document.documentElement.style.overflow='hidden';"
                  L"})();",
                  nullptr
                );
                if (p->webview_) p->webview_->Navigate(urlW.c_str());
                p->wv2Initialized_.store(true);
                wv2Log("wv2Initialized, hwnd=%p", (void*)p->hostHwnd_);
                SetTimer(p->hostHwnd_, WV2_CAPTURE_TIMER_ID, 33, nullptr);
                return S_OK;
              }
            ).Get()
          );
          return S_OK;
        }
      ).Get()
    );

    if (FAILED(hr)) {
      p->wv2ErrorMsg_ = "WebView2 init failed (Edge not installed?)";
      p->wv2Failed_.store(true);
      DestroyWindow(hwnd);
      p->hostHwnd_ = nullptr;
      CoUninitialize();
      return;
    }

    wv2Log("entering message loop");
    MSG msg;
    bool wv2QuitRequested = false;
    while (!wv2QuitRequested && GetMessage(&msg, nullptr, 0, 0)) {
      if (msg.hwnd == nullptr && msg.message == WM_WV2_NAVIGATE) {
        std::wstring url;
        { std::lock_guard<std::mutex> lk(p->navigateMutex_); url = p->pendingNavigateUrl_; }
        if (p->webview_ && !url.empty()) p->webview_->Navigate(url.c_str());
      } else if (msg.hwnd == nullptr && msg.message == WM_WV2_RELOAD) {
        if (p->webview_) p->webview_->Reload();
      } else if (msg.hwnd == nullptr && msg.message == WM_WV2_EXEC_JS) {
        std::wstring script;
        { std::lock_guard<std::mutex> lk(p->navigateMutex_); script = p->pendingScript_; }
        if (p->webview_ && !script.empty()) {
          p->webview_->ExecuteScript(script.c_str(), nullptr);
        }
      } else if (msg.hwnd == nullptr && msg.message == WM_WV2_SET_INTERACTIVE) {
        // HAND THE PAGE OVER, THE WAY MITTI DOES.
        //
        // The host window has been there all along -- full size at (0,0),
        // pushed to HWND_BOTTOM behind every real window, and marked
        // NOACTIVATE so it can never steal focus. Interaction is simply
        // undoing that: bring it forward, let it take focus, and let the
        // operator use the page with a real mouse and keyboard.
        //
        // This is the only way to LOG IN. A synthetic click cannot type a
        // password, satisfy a 2FA prompt, or open a password manager, and
        // those are exactly what stands between a cue and the page it wants.
        //
        // The window is NOT resized or moved: WebView2's DirectComposition
        // pipeline is fussy about this window's geometry (see the notes where
        // it is created -- hiding it, shrinking it or layering it corrupts the
        // surface), and the capture keeps running throughout, so the cue stays
        // on air while the operator works.
        const bool wantInteractive = (msg.wParam != 0);
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (wantInteractive) {
          exStyle &= ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
          SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

          // FIT IT TO THE SCREEN, AND MEAN IT.
          //
          // The window is the CUE's raster -- 3840x2160 for a 4K show -- and
          // at that size on a 1080p desk the operator sees the top-left
          // quarter of the page and cannot reach a centred consent button.
          //
          // Measured before writing this: resizing the host window does NOT
          // disturb the cue (435 identical pixels of the page on air, at the
          // same coordinates, before and after), because CapturePreview
          // follows the CONTROLLER's bounds and not the window's. Which is
          // also why the bounds have to be moved deliberately here -- with the
          // window alone resized, the operator gets a crop.
          const int screenW = GetSystemMetrics(SM_CXSCREEN);
          const int screenH = GetSystemMetrics(SM_CYSCREEN);
          const int fitW = std::min(p->width_, screenW * 4 / 5);
          const int fitH = std::min(p->height_, screenH * 4 / 5);
          SetWindowPos(hwnd, HWND_TOP,
                       (screenW - fitW) / 2, (screenH - fitH) / 2,
                       fitW, fitH, SWP_SHOWWINDOW);
          if (p->controller_) {
            // THE VIEW KEEPS ITS SIZE. ONLY ITS ORIGIN MOVES.
            //
            // Fitting the CONTROLLER to the window was tried and measured, and
            // it reflows the page -- the picture on air went from 435 to 651
            // pixels of the same element the instant the operator opened the
            // window. A cue that restages itself because someone reached for a
            // cookie banner is not acceptable during a show.
            //
            // So the WebView stays exactly the cue's raster, which is what
            // CapturePreview follows, and is instead SHIFTED so its middle sits
            // in the smaller window. The operator sees the centre of the page,
            // where consent dialogs live, and the audience sees no change at
            // all. On a screen bigger than the raster nothing shifts.
            const int dx = (p->width_ - fitW) / 2;
            const int dy = (p->height_ - fitH) / 2;
            RECT bounds {-dx, -dy, -dx + p->width_, -dy + p->height_};
            p->controller_->put_Bounds(bounds);
            p->controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
          }
          SetForegroundWindow(hwnd);
        } else {
          exStyle |= static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
          SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
          // Back to the cue's raster, or the next captured frame is the wrong
          // shape for everything downstream.
          SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, p->width_, p->height_,
                       SWP_NOACTIVATE);
          if (p->controller_) {
            RECT bounds {0, 0, p->width_, p->height_};
            p->controller_->put_Bounds(bounds);
          }
        }
      } else if (msg.hwnd == hwnd && msg.message == WM_TIMER
                 && msg.wParam == WV2_CAPTURE_TIMER_ID) {
        // ~30fps capture tick: call CapturePreview if no capture is in-flight.
        // The completion handler fires on this STA thread via the message pump.
        //
        // Hang guard: if captureInFlight_ has been true for >~5 s the renderer
        // stalled and the callback will never arrive.  Force-reset so we can retry.
        if (p->captureInFlight_.load()) {
          if (++p->captureHangTicks_ > 150) {
            p->captureInFlight_.store(false);
            p->captureHangTicks_ = 0;
          }
        } else {
          p->captureHangTicks_ = 0;
        }
        if (p->webview_ && !p->captureInFlight_.load()) {
          p->captureInFlight_.store(true);
          Microsoft::WRL::ComPtr<IStream> stream;
          if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
            HRESULT cpHr = p->webview_->CapturePreview(
              COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,
              stream.Get(),
              Microsoft::WRL::Callback<ICoreWebView2CapturePreviewCompletedHandler>(
                [p, stream](HRESULT hr) mutable -> HRESULT {
                  p->captureInFlight_.store(false);
                  if (SUCCEEDED(hr)) {
                    int w = 0, h = 0;
                    std::vector<uint8_t> rgba;
                    if (SUCCEEDED(decodeWicPngToRgba(stream.Get(), w, h, rgba))) {
                      std::lock_guard<std::mutex> lk(p->frameMutex_);
                      p->latestRgba_ = std::move(rgba);
                      p->captureW_ = w;
                      p->captureH_ = h;
                      p->frameReady_ = true;
                      p->hasFirstFrame_.store(true);
                    }
                  }
                  return S_OK;
                }
              ).Get()
            );
            if (FAILED(cpHr)) p->captureInFlight_.store(false);
          } else {
            p->captureInFlight_.store(false);
          }
        }
      } else if (msg.hwnd == nullptr && msg.message == WM_WV2_CLOSE_AND_QUIT) {
        wv2Log("WM_WV2_CLOSE_AND_QUIT received");
        KillTimer(hwnd, WV2_CAPTURE_TIMER_ID);
        if (p->webview_ && p->navCompletedToken_.value != 0) {
          p->webview_->remove_NavigationCompleted(p->navCompletedToken_);
          p->navCompletedToken_ = {};
        }
        // Stop any in-progress navigation before Close() to avoid crash inside
        // the WebView2 runtime when Close() is called mid-navigation.
        if (p->webview_) p->webview_->Stop();
        // Close WebView2 while the message pump is still running. Release
        // webview BEFORE controller (controller owns webview internally).
        if (p->controller_) p->controller_->Close();
        p->webview_.Reset();
        p->controller_.Reset();
        DestroyWindow(hwnd);
        p->hostHwnd_ = nullptr;
        // Exit the loop without pumping further messages — GetMessage after
        // Close()/Reset() would dispatch callbacks to destroyed internal windows.
        wv2QuitRequested = true;
      } else {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }
    // COM objects and window were already cleaned up inside WM_WV2_CLOSE_AND_QUIT.
    // On the error path (PostQuitMessage without Close), controller/webview were
    // never set, so these are no-ops. Release webview before controller.
    p->webview_.Reset();
    p->controller_.Reset();
    if (p->hostHwnd_) {
      DestroyWindow(p->hostHwnd_);
      p->hostHwnd_ = nullptr;
    }
    CoUninitialize();
  });

  impl_->isRunning_ = true;
  impl_->phase_ = BrowserStartPhase::WaitXvfb;  // reused: "waiting for WebView2 init"
  impl_->phaseStartedAt_ = std::chrono::steady_clock::now();
  return true;
#elif defined(_WIN32)
  std::string exe = detectBrowserExecutable();
  if (exe.empty()) {
    impl_->lastError_ = "Edge or Chrome not found";
    return false;
  }

  impl_->browserProfileDir_ = nextBrowserProfilePath();
  std::error_code err;
  fs::create_directories(impl_->browserProfileDir_, err);
  if (err) {
    impl_->lastError_ = "profile dir unavailable";
    return false;
  }

  std::vector<std::string> args {
    exe,
    "--no-first-run",
    "--disable-extensions",
    "--disable-session-crashed-bubble",
    "--disable-infobars",
    "--app=" + impl_->url_,
    "--window-size=" + std::to_string(width) + "," + std::to_string(height),
    "--window-position=0,0",
    "--user-data-dir=" + impl_->browserProfileDir_.string()
  };
  if (!spawnDetachedProcess(impl_->browserProcess_, args)) {
    impl_->lastError_ = "browser launch failed";
    return false;
  }

  impl_->captureSourceRef_ = "region:0,0," + std::to_string(width) + "," + std::to_string(height);
  impl_->isRunning_ = true;
  impl_->phase_ = BrowserStartPhase::WaitChrome;
  impl_->phaseStartedAt_ = std::chrono::steady_clock::now();
  return true;
#elif defined(__APPLE__)
  // The helper is next to the app, exactly like deckboy-sckcapture.
  // Beside the executable (Contents/MacOS/ in a bundle), the same way
  // capture_backend.cpp finds deckboy-sckcapture.
  const fs::path helper =
    deckboy::core::Paths::executablePath().parent_path() / "deckboy-webview";
  std::error_code helperErr;
  if (!fs::exists(helper, helperErr)) {
    impl_->lastError_ = "deckboy-webview helper missing";
    return false;
  }

  SpawnOptions options;
  options.stdinMode = StdioMode::Pipe;    // commands: js / interact / url
  options.stdoutMode = StdioMode::Pipe;   // the window id comes back here
  options.stderrMode = StdioMode::Null;
  // The helper is told where the warning card lives rather than working it out:
  // it sits beside the executable, but the data tree is in Contents/Resources,
  // and only the app knows how its own install is laid out.
  std::vector<std::string> helperArgs {
    helper.string(),
    "--url", impl_->url_,
    "--width", std::to_string(width),
    "--height", std::to_string(height)
  };
  {
    const std::string card = unreachableCardUrl({});
    if (!card.empty()) {
      helperArgs.push_back("--fallback");
      helperArgs.push_back(card);
    }
  }
  if (!spawnProcess(impl_->webviewProcess_, helperArgs, options)) {
    impl_->lastError_ = "browser helper launch failed";
    return false;
  }

  // The helper prints "WINDOWID <n>" as soon as the window exists. Read it
  // before capture can start -- there is nothing to capture until we know
  // which window, and a helper that has not said is one that failed to open.
  std::string line;
  char ch = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto got = ::read(impl_->webviewProcess_.readFd, &ch, 1);
    if (got <= 0) {
      break;
    }
    if (ch == '\n') {
      break;
    }
    line.push_back(ch);
  }
  const std::string marker = "WINDOWID ";
  if (line.rfind(marker, 0) != 0) {
    impl_->stopProcesses(false);
    impl_->lastError_ = "browser helper did not report a window";
    return false;
  }
  impl_->webviewWindowId_ = trimCopy(line.substr(marker.size()));
  if (impl_->webviewWindowId_.empty()) {
    impl_->stopProcesses(false);
    impl_->lastError_ = "browser helper reported an empty window";
    return false;
  }

  // From here it is an ordinary window source, so the ScreenCaptureKit path
  // that already exists does the rest and nothing downstream has to know a
  // browser was involved.
  impl_->captureSourceRef_ = "window:" + impl_->webviewWindowId_;
  impl_->isRunning_ = true;
  impl_->capturePending_ = true;
  impl_->phase_ = BrowserStartPhase::WaitCapture;
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
  if (!impl_->isRunning_) return false;
#if defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
  std::lock_guard<std::mutex> lk(impl_->frameMutex_);
  if (!impl_->frameReady_ || impl_->latestRgba_.empty()) return false;
  outFrame.width  = impl_->captureW_;
  outFrame.height = impl_->captureH_;
  outFrame.rgba   = impl_->latestRgba_;
  impl_->frameReady_ = false;  // consumed — next grab waits for a new capture
  return true;
#else
  (void) outFrame;
  return false;
#endif
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
#elif defined(__APPLE__)
  // "window:<CGWindowID>" -- the sckcapture helper resolves it, so a browser
  // cue reaches the screen through the same path as any window source.
  outSourceRef = impl_->captureSourceRef_;
#elif defined(_WIN32) && !defined(DECKBOY_HAS_WEBVIEW)
  outSourceRef = impl_->captureSourceRef_;
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
#if defined(__APPLE__)
  if (!url.empty()) {
    impl_->url_ = trimCopy(url);
    return sendHelperCommand("url " + impl_->url_);
  }
#endif
  impl_->url_ = trimCopy(url);
  if (!impl_->isRunning_) {
    return false;
  }
#if defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
  DWORD tid = impl_->wv2ThreadId_.load();
  if (tid != 0 && impl_->wv2Initialized_.load()) {
    { std::lock_guard<std::mutex> lk(impl_->navigateMutex_); impl_->pendingNavigateUrl_ = utf8ToWide(impl_->url_); }
    PostThreadMessageW(tid, WM_WV2_NAVIGATE, 0, 0);
    return true;
  }
#endif
  return false;
}

bool BrowserRenderer::goBack() {
#if defined(__APPLE__)
  return sendHelperCommand("back");
#endif
  return false;
}

bool BrowserRenderer::goForward() {
#if defined(__APPLE__)
  return sendHelperCommand("forward");
#endif
  return false;
}

bool BrowserRenderer::reload() {
#if defined(__APPLE__)
  return sendHelperCommand("reload");
#endif
#if defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
  if (!impl_->isRunning_) return false;
  DWORD tid = impl_->wv2ThreadId_.load();
  if (tid != 0 && impl_->wv2Initialized_.load()) {
    PostThreadMessageW(tid, WM_WV2_RELOAD, 0, 0);
    return true;
  }
#endif
  return false;
}

bool BrowserRenderer::executeJavaScript(const std::string& script) {
  if (script.empty() || !impl_->isRunning_) return false;
#if defined(__APPLE__)
  // WKWebView's evaluateJavaScript:, one process along. A script with a
  // newline in it would be read as two commands, so they are folded to spaces
  // -- JavaScript does not care, and the alternative is a helper that
  // silently runs half a statement.
  std::string flat = script;
  std::replace(flat.begin(), flat.end(), '\n', ' ');
  std::replace(flat.begin(), flat.end(), '\r', ' ');
  return sendHelperCommand("js " + flat);
#endif
#if defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
  DWORD tid = impl_->wv2ThreadId_.load();
  if (tid != 0 && impl_->wv2Initialized_.load()) {
    { std::lock_guard<std::mutex> lk(impl_->navigateMutex_); impl_->pendingScript_ = utf8ToWide(script); }
    PostThreadMessageW(tid, WM_WV2_EXEC_JS, 0, 0);
    return true;
  }
#else
  (void) script;
#endif
  return false;
}

// A page can restyle itself at any time, so the rule is marked !important and
// re-applied rather than set once: a single injection at load is undone by the
// first framework that writes its own overflow style.
#if defined(__linux__) && defined(DECKBOY_HAS_XTEST)
// REAL POINTER EVENTS ON THE CUE'S OWN DISPLAY.
//
// Windows drives the page through WebView2's JavaScript channel. Linux has no
// such channel -- the backend is Chromium on a private Xvfb display -- so it
// drives the page the way a person would, with XTEST. That is not a lesser
// substitute: synthetic DOM events only reach content that listens for them,
// while a real button press reaches everything, the browser's own scrolling
// included.
//
// The display is opened per call rather than held: a cue's display comes and
// goes with the cue, and a stale Display* outliving its Xvfb is a crash the
// operator would see as "the browser cue killed the show".
namespace {

class ScopedDisplay {
 public:
  explicit ScopedDisplay(const std::string& name)
    : display_(name.empty() ? nullptr : XOpenDisplay(name.c_str())) {}
  ~ScopedDisplay() { if (display_) XCloseDisplay(display_); }
  ScopedDisplay(const ScopedDisplay&) = delete;
  ScopedDisplay& operator=(const ScopedDisplay&) = delete;
  Display* get() const { return display_; }
  explicit operator bool() const { return display_ != nullptr; }
 private:
  Display* display_;
};

}  // namespace
#endif

#if defined(__APPLE__)
// Every macOS page control is one line down the helper's stdin. Kept in one
// function so the newline discipline and the "is it even running" check cannot
// drift between callers.
bool BrowserRenderer::sendHelperCommand(const std::string& command) {
  if (!impl_->isRunning_ || impl_->webviewProcess_.writeFd < 0) {
    return false;
  }
  // THE PROTOCOL IS ONE COMMAND PER LINE, so an embedded newline is not a
  // character -- it is a command separator. A URL or a string carrying one
  // would inject a second command into the helper, which is a real hole given
  // a cue's URL can come from a show file or the network. Folded to spaces
  // HERE, at the one place that writes to the pipe, rather than trusting every
  // caller to remember.
  std::string safe = command;
  for (char& ch : safe) {
    if (ch == '\n' || ch == '\r') {
      ch = ' ';
    }
  }
  const std::string line = safe + "\n";
  const auto written = ::write(impl_->webviewProcess_.writeFd,
                               line.data(), line.size());
  return written == static_cast<decltype(written)>(line.size());
}
#endif

#if defined(__linux__) && defined(DECKBOY_HAS_XTEST)
namespace {

// Press one keysym, with shift if the layout needs it.
//
// A keysym the current layout does not carry has no keycode, so it cannot be
// pressed at all. Rather than drop the character, a SPARE keycode is
// temporarily remapped to it, pressed, and put back -- which is how every
// synthetic-typing tool on X handles accented and non-Latin input.
void pressKeysym(Display* display, KeySym symbol) {
  if (symbol == NoSymbol) {
    return;
  }
  KeyCode code = XKeysymToKeycode(display, symbol);
  bool remapped = false;
  bool needsShift = false;

  if (code != 0) {
    // Is it the shifted form? Column 0 is unshifted, column 1 is shifted.
    const KeySym plain = XkbKeycodeToKeysym(display, code, 0, 0);
    const KeySym shifted = XkbKeycodeToKeysym(display, code, 0, 1);
    if (plain != symbol && shifted == symbol) {
      needsShift = true;
    }
  } else {
    // Borrow a keycode. The high end of the range is where X leaves spares.
    int minCode = 0;
    int maxCode = 0;
    XDisplayKeycodes(display, &minCode, &maxCode);
    for (int candidate = maxCode; candidate > minCode; --candidate) {
      if (XkbKeycodeToKeysym(display, static_cast<KeyCode>(candidate), 0, 0) == NoSymbol) {
        KeySym mapping[2] = {symbol, symbol};
        XChangeKeyboardMapping(display, candidate, 2, mapping, 1);
        XSync(display, 0);
        code = static_cast<KeyCode>(candidate);
        remapped = true;
        break;
      }
    }
    if (code == 0) {
      return;   // no spare keycode; this character cannot be typed
    }
  }

  const KeyCode shiftCode = XKeysymToKeycode(display, XK_Shift_L);
  if (needsShift && shiftCode != 0) {
    XTestFakeKeyEvent(display, shiftCode, 1, CurrentTime);
  }
  XTestFakeKeyEvent(display, code, 1, CurrentTime);
  XTestFakeKeyEvent(display, code, 0, CurrentTime);
  if (needsShift && shiftCode != 0) {
    XTestFakeKeyEvent(display, shiftCode, 0, CurrentTime);
  }
  XFlush(display);

  if (remapped) {
    // Put the borrowed keycode back, or the operator's own keyboard starts
    // producing whatever was last typed into a browser cue.
    KeySym cleared[2] = {NoSymbol, NoSymbol};
    XChangeKeyboardMapping(display, code, 2, cleared, 1);
    XSync(display, 0);
  }
}

// UTF-8 -> Unicode code point. Returns the bytes consumed.
std::size_t decodeUtf8(const std::string& text, std::size_t at, unsigned long& out) {
  const unsigned char lead = static_cast<unsigned char>(text[at]);
  if (lead < 0x80) { out = lead; return 1; }
  auto cont = [&](std::size_t i) {
    return at + i < text.size()
         ? (static_cast<unsigned char>(text[at + i]) & 0x3F) : 0u;
  };
  if ((lead & 0xE0) == 0xC0 && at + 1 < text.size()) {
    out = ((lead & 0x1Fu) << 6) | cont(1);
    return 2;
  }
  if ((lead & 0xF0) == 0xE0 && at + 2 < text.size()) {
    out = ((lead & 0x0Fu) << 12) | (cont(1) << 6) | cont(2);
    return 3;
  }
  if ((lead & 0xF8) == 0xF0 && at + 3 < text.size()) {
    out = ((lead & 0x07u) << 18) | (cont(1) << 12) | (cont(2) << 6) | cont(3);
    return 4;
  }
  out = lead;
  return 1;
}

}  // namespace
#endif

bool BrowserRenderer::sendText(const std::string& utf8) {
  if (utf8.empty() || !impl_->isRunning_) {
    return false;
  }
#if defined(__linux__) && defined(DECKBOY_HAS_XTEST)
  if (!impl_->virtualDisplayId_.empty()) {
    ScopedDisplay display(impl_->virtualDisplayId_);
    if (display) {
      for (std::size_t at = 0; at < utf8.size(); ) {
        unsigned long code = 0;
        at += decodeUtf8(utf8, at, code);
        // Latin-1 maps straight onto keysyms; everything else uses X's
        // Unicode keysym range.
        const KeySym symbol = (code < 0x100)
          ? static_cast<KeySym>(code)
          : static_cast<KeySym>(code | 0x01000000);
        pressKeysym(display.get(), symbol);
      }
      return true;
    }
  }
  return false;
#else
  // Windows and macOS both have a JavaScript channel, so the text goes to
  // whatever the page has focused. Quotes and backslashes are escaped because
  // this is built into a script.
  // Escaped for a single-quoted JavaScript string literal. Backslash and quote
  // are the obvious two; U+2028 and U+2029 are the ones that get missed --
  // JavaScript treats them as line terminators INSIDE a string literal, so a
  // pasted character nobody can see would end the string and run whatever
  // followed as code.
  std::string escaped;
  escaped.reserve(utf8.size() + 8);
  for (std::size_t i = 0; i < utf8.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(utf8[i]);
    if (ch == 0xE2 && i + 2 < utf8.size()
        && static_cast<unsigned char>(utf8[i + 1]) == 0x80
        && (static_cast<unsigned char>(utf8[i + 2]) == 0xA8
            || static_cast<unsigned char>(utf8[i + 2]) == 0xA9)) {
      escaped += (static_cast<unsigned char>(utf8[i + 2]) == 0xA8) ? "\\u2028"
                                                                   : "\\u2029";
      i += 2;
      continue;
    }
    if (ch == '\\' || ch == '\'') {
      escaped.push_back('\\');
      escaped.push_back(static_cast<char>(ch));
      continue;
    }
    if (ch == '\n' || ch == '\r') {
      continue;
    }
    escaped.push_back(static_cast<char>(ch));
  }
  const std::string js =
    "(function(){var el=document.activeElement;if(!el)return;"
    "var t='" + escaped + "';"
    "if('value' in el){el.value=(el.value||'')+t;"
    "el.dispatchEvent(new Event('input',{bubbles:true}));"
    "el.dispatchEvent(new Event('change',{bubbles:true}));}"
    "else if(el.isContentEditable){el.textContent=(el.textContent||'')+t;}})()";
  return executeJavaScript(js);
#endif
}

bool BrowserRenderer::sendKey(const std::string& name) {
  if (name.empty() || !impl_->isRunning_) {
    return false;
  }
#if defined(__linux__) && defined(DECKBOY_HAS_XTEST)
  if (!impl_->virtualDisplayId_.empty()) {
    ScopedDisplay display(impl_->virtualDisplayId_);
    if (display) {
      // X11 DOES NOT USE THE NAMES THE REST OF DECKBOY USES.
      //
      // The verb is documented as Enter|Tab|Backspace|Escape, and X calls two
      // of those something else: "Return" and "BackSpace" (capital S). So
      // XStringToKeysym returned NoSymbol and the operator was told "this
      // backend cannot send Enter" -- for the single most useful key in the
      // set, on the platform where a browser cue most often needs to submit a
      // form. Tab and Escape happened to match, which is why the gap looked
      // like a backend limitation rather than a spelling one.
      std::string lower = name;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      std::string xName = name;
      if (lower == "enter" || lower == "return") {
        xName = "Return";
      } else if (lower == "backspace") {
        xName = "BackSpace";
      } else if (lower == "tab") {
        xName = "Tab";
      } else if (lower == "escape" || lower == "esc") {
        xName = "Escape";
      }
      const KeySym symbol = XStringToKeysym(xName.c_str());
      if (symbol == NoSymbol) {
        return false;
      }
      pressKeysym(display.get(), symbol);
      return true;
    }
  }
  return false;
#else
  // The name is pasted into a script, so it is checked rather than escaped: a
  // key name is letters and digits and nothing else, and anything else is a
  // caller trying to run code, not press a key.
  for (char ch : name) {
    if (!std::isalnum(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  // A named key as a real KeyboardEvent, so a form's own Enter handler fires.
  const std::string js =
    "(function(){var el=document.activeElement||document.body;"
    "['keydown','keyup'].forEach(function(t){"
    "el.dispatchEvent(new KeyboardEvent(t,{key:'" + name +
    "',bubbles:true,cancelable:true}));});"
    "if('" + name + "'==='Enter'&&el.form){el.form.requestSubmit&&el.form.requestSubmit();}})()";
  return executeJavaScript(js);
#endif
}

bool BrowserRenderer::isInteractive() const {
  return impl_->interactive_;
}

bool BrowserRenderer::setInteractive(bool interactive) {
  if (!impl_->isRunning_) {
    return false;
  }
#if defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
  DWORD tid = impl_->wv2ThreadId_.load();
  if (tid == 0 || !impl_->wv2Initialized_.load()) {
    return false;
  }
  impl_->interactive_ = interactive;
  PostThreadMessageW(tid, WM_WV2_SET_INTERACTIVE, interactive ? 1u : 0u, 0);
  return true;
#elif defined(__APPLE__)
  // The helper owns the window, so it does the showing. Everything AppKit
  // touches has to happen on that process's main thread, which is exactly why
  // this is a message rather than a call.
  if (!sendHelperCommand(interactive ? "interact 1" : "interact 0")) {
    return false;
  }
  impl_->interactive_ = interactive;
  return true;
#elif defined(__linux__) && defined(DECKBOY_HAS_XTEST)
  // LINUX TYPES INTO THE PAGE INSTEAD OF SHOWING A WINDOW.
  //
  // The first attempt here opened a second Chromium with --new-window against
  // the same profile, on the theory that a login taken there would be picked
  // up by the cue. It was measured and it does not work: Chromium hands
  // --new-window to the ALREADY-RUNNING instance for that profile, which owns
  // the cue's private Xvfb -- so the window opened on the hidden display where
  // nobody could see it (4 windows -> 5 on :21, none on the operator's), and
  // INTERACT off could not close it because the process it spawned had already
  // exited after handing off.
  //
  // There is no window to show on Linux, so interaction is delivered the same
  // way clicks and scrolling already are: straight into the cue's display with
  // XTEST. Turning it on routes the operator's KEYBOARD there too, which is
  // what makes signing in possible -- the thing that motivated the whole
  // feature.
  impl_->interactive_ = interactive;
  return true;
#else
  (void) interactive;
  return false;
#endif
}

bool BrowserRenderer::setScrollbarsVisible(bool visible) {
#if defined(__linux__)
  // On Linux this is decided at launch (see --hide-scrollbars above), so the
  // preference is recorded for the next take. Saying "yes" for a page already
  // on screen with the other setting would be a lie, and this codebase has
  // been bitten enough times by calls that report success and do nothing.
  const bool wanted = !visible;
  const bool alreadyRight = impl_->hideScrollbars_ == wanted;
  impl_->hideScrollbars_ = wanted;
  return alreadyRight;
#endif
  const char* kHide =
    "(function(){var s=document.getElementById('__deckboy_sb');"
    "if(!s){s=document.createElement('style');s.id='__deckboy_sb';"
    "document.documentElement.appendChild(s);}"
    "s.textContent='::-webkit-scrollbar{width:0!important;height:0!important;"
    "display:none!important}html{scrollbar-width:none!important;"
    "-ms-overflow-style:none!important}';})()";
  const char* kShow =
    "(function(){var s=document.getElementById('__deckboy_sb');"
    "if(s){s.remove();}})()";
  return executeJavaScript(visible ? kShow : kHide);
}

bool BrowserRenderer::scrollBy(int dx, int dy) {
#if defined(__linux__) && defined(DECKBOY_HAS_XTEST)
  if (impl_->isRunning_ && !impl_->virtualDisplayId_.empty()) {
    ScopedDisplay display(impl_->virtualDisplayId_);
    if (display) {
      // X wheel buttons: 4 up, 5 down, 6 left, 7 right. One notch is roughly
      // three lines, so the pixel request is turned into notches rather than
      // pretending to a precision the wheel does not have.
      auto wheel = [&](unsigned int button, int notches) {
        for (int i = 0; i < notches; ++i) {
          XTestFakeButtonEvent(display.get(), button, 1, CurrentTime);
          XTestFakeButtonEvent(display.get(), button, 0, CurrentTime);
        }
      };
      const int vertical = std::abs(dy) / 53;
      const int horizontal = std::abs(dx) / 53;
      if (vertical > 0) {
        wheel(dy > 0 ? 5u : 4u, std::min(vertical, 40));
      }
      if (horizontal > 0) {
        wheel(dx > 0 ? 7u : 6u, std::min(horizontal, 40));
      }
      XFlush(display.get());
      return vertical > 0 || horizontal > 0;
    }
  }
#endif
  // behavior:'instant' matters: a smooth scroll animates over several frames,
  // and a cue being captured frame by frame would show the tween.
  std::string js = "window.scrollBy({left:" + std::to_string(dx) +
                   ",top:" + std::to_string(dy) + ",behavior:'instant'})";
  return executeJavaScript(js);
}

bool BrowserRenderer::clickAtFraction(double fx, double fy) {
  if (fx < 0.0 || fx > 1.0 || fy < 0.0 || fy > 1.0) {
    return false;
  }
#if defined(__linux__) && defined(DECKBOY_HAS_XTEST)
  if (impl_->isRunning_ && !impl_->virtualDisplayId_.empty()
      && impl_->width_ > 0 && impl_->height_ > 0) {
    ScopedDisplay display(impl_->virtualDisplayId_);
    if (display) {
      const int x = static_cast<int>(fx * impl_->width_);
      const int y = static_cast<int>(fy * impl_->height_);
      XTestFakeMotionEvent(display.get(), -1, x, y, CurrentTime);
      XFlush(display.get());
      // A beat between arriving and pressing: hover states and menus that open
      // on mouseover need the pointer to have been somewhere before the click,
      // and a press in the same instant as the move can land on the old
      // element.
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      XTestFakeButtonEvent(display.get(), 1, 1, CurrentTime);
      XTestFakeButtonEvent(display.get(), 1, 0, CurrentTime);
      XFlush(display.get());
      return true;
    }
  }
#endif
  // elementFromPoint takes VIEWPORT coordinates, so the fraction is of the
  // window, not of the document -- which is what the preview shows.
  //
  // The full pointerdown/mouseup/click sequence, not just .click(): consent
  // dialogs and anything built on pointer events ignore a bare click().
  std::string js =
    "(function(){var x=Math.round(window.innerWidth*" + std::to_string(fx) +
    "),y=Math.round(window.innerHeight*" + std::to_string(fy) + ");"
    "var el=document.elementFromPoint(x,y);if(!el)return;"
    "var o={bubbles:true,cancelable:true,composed:true,clientX:x,clientY:y,"
    "button:0,buttons:1};"
    "try{el.dispatchEvent(new PointerEvent('pointerdown',o));}catch(e){}"
    "el.dispatchEvent(new MouseEvent('mousedown',o));"
    "try{el.dispatchEvent(new PointerEvent('pointerup',o));}catch(e){}"
    "el.dispatchEvent(new MouseEvent('mouseup',o));"
    "el.dispatchEvent(new MouseEvent('click',o));"
    "if(typeof el.focus==='function'){el.focus();}})()";
  return executeJavaScript(js);
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
    // Chromium's own switch, which is the Linux equivalent of the CSS the
    // Windows backend injects. It is a launch flag, so it settles the question
    // before the first frame rather than racing the page's own styling -- and
    // unlike the CSS it cannot be undone by a framework mounting.
    if (impl_->hideScrollbars_) {
      args.insert(args.begin() + 1, "--hide-scrollbars");
    }
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
#elif defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
  if (impl_->wv2Failed_.load() && impl_->lastError_.empty()) {
    impl_->failSession(impl_->wv2ErrorMsg_.empty() ? "WebView2 failed" : impl_->wv2ErrorMsg_);
    return;
  }
  if (impl_->phase_ == BrowserStartPhase::WaitXvfb && impl_->wv2Initialized_.load()) {
    // WebView2 is up. The capture timer (WM_TIMER) was started inside the
    // controller-completed callback; nothing to do here except advance the phase.
    impl_->phase_ = BrowserStartPhase::WaitChrome;
    impl_->phaseStartedAt_ = std::chrono::steady_clock::now();
  }
  if (impl_->phase_ == BrowserStartPhase::WaitChrome && impl_->hasFirstFrame_.load()) {
    impl_->phase_ = BrowserStartPhase::Live;
  }
#elif defined(_WIN32)
  {
    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - impl_->phaseStartedAt_).count();
    if (impl_->phase_ == BrowserStartPhase::WaitChrome) {
      if (elapsedMs < 2000) return;
      impl_->phase_ = BrowserStartPhase::WaitCapture;
      impl_->capturePending_ = true;
      impl_->phaseStartedAt_ = now;
    }
  }
#endif
}

}  // namespace deckboy::platform::browser
