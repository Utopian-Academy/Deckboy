// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// capture_backend.cpp — Platform-specific source capture backend implementations.
//
// Implements the SourceCaptureBackend interface for each platform's capture
// method, producing ffmpeg argument lists that pipe raw RGBA frames to stdout:
//
//   Linux:
//     LinuxWindowCaptureBackend  — x11grab (screen/window capture via X11)
//     LinuxCameraCaptureBackend  — v4l2 (Video4Linux2 camera devices)
//     LinuxAppTextureCaptureBackend — desktop-fallback (x11grab as Syphon/Spout proxy)
//
//   Windows:
//     WindowsGdigrabCaptureBackend — gdigrab (GDI screen/region capture)
//     UnsupportedCameraCaptureBackend — mediafoundation (scaffold only)
//
//   macOS:
//     UnsupportedCameraCaptureBackend — avfoundation (scaffold only)
//
// Each backend's plan() method interprets the SourceCaptureRequest's sourceRef
// string (e.g. "x11::0+0,0", "id:0x1234", "v4l2:/dev/video0", "region:X,Y,W,H")
// and constructs the full ffmpeg command line including scaling to the requested
// output resolution via nearest-neighbor.
//
// Also provides:
//   DefaultCaptureBackendCatalog — enumerates which backends are available
//   Factory functions — create*CaptureBackend() for each capture kind
//   planSourceCapture() — convenience function: pick backend + generate plan
//
// Header: capture_backend.hpp
// Used by: media_engine.cpp (buildSourceCaptureArgs for WindowSource/Camera cues).
// ============================================================================

#include "platform/capture_backend.hpp"
#include "core/utils.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace deckboy::platform {
namespace {

using deckboy::core::utils::trim;
using deckboy::core::utils::toLower;

// Returns the X11 display string, defaulting to ":0.0" (primary display)
// if the user didn't specify one in the capture request.
std::string defaultDisplay(const std::string& token) {
  std::string display = trim(token);
  if (display.empty()) {
    return ":0.0";
  }
  return display;
}

// ── Linux X11 window/screen capture via ffmpeg x11grab ──────────────────────
// Supports multiple sourceRef formats:
//   "x11::0+100,200"   — explicit x11grab input specification
//   "id:0x1234"         — capture a specific X11 window by its window ID
//   "window_id:0x1234"  — alias for id: prefix
//   ":1+0,0"            — raw display specification (starts with colon)
//   "+100,200"           — offset on the default display
//   "screen" / "desktop" / "" — full-screen capture at default display
class LinuxWindowCaptureBackend final : public SourceCaptureBackend {
 public:
  SourceCaptureKind kind() const override {
    return SourceCaptureKind::Window;
  }

  std::string id() const override {
    return "x11grab";
  }

  SourceCapturePlan plan(const SourceCaptureRequest& request) const override {
    SourceCapturePlan plan;
#if defined(__linux__)
    std::string sourceRef = trim(request.sourceRef);
    std::string sourceRefLower = toLower(sourceRef);
    std::string display = defaultDisplay(request.display);
    int w = std::max(1, request.width);
    int h = std::max(1, request.height);
    int fps = std::clamp(request.frameRate, 1, 120);

    // Default: capture full screen on the default display
    std::string inputSpec = display + "+0,0";
    bool useWindowId = false;
    std::string windowId;

    // Parse the sourceRef string to determine the x11grab input specification.
    // Each prefix maps to a different capture mode:
    if (sourceRefLower.rfind("x11:", 0) == 0 && sourceRef.size() > 4) {
      // Explicit x11grab input spec (e.g. "x11::0+100,200")
      inputSpec = trim(sourceRef.substr(4));
      if (inputSpec.empty()) {
        inputSpec = display + "+0,0";
      }
    } else if (sourceRefLower.rfind("id:", 0) == 0 && sourceRef.size() > 3) {
      // Capture a specific window by X11 window ID (hex or decimal)
      useWindowId = true;
      windowId = trim(sourceRef.substr(3));
      inputSpec = display;
    } else if (sourceRefLower.rfind("window_id:", 0) == 0 && sourceRef.size() > 10) {
      // Alternative prefix for window ID capture
      useWindowId = true;
      windowId = trim(sourceRef.substr(10));
      inputSpec = display;
    } else if (!sourceRef.empty() && sourceRef[0] == ':') {
      // Raw display string (e.g. ":1" or ":0+100,200")
      inputSpec = sourceRef;
      if (inputSpec.find('+') == std::string::npos) {
        inputSpec += "+0,0";  // Ensure offset is present for x11grab
      }
    } else if (!sourceRef.empty() && sourceRef[0] == '+') {
      // Offset-only: prepend default display (e.g. "+100,200" → ":0.0+100,200")
      inputSpec = display + sourceRef;
    } else if (sourceRefLower == "active-window" || sourceRefLower == "default-window"
               || sourceRefLower == "screen" || sourceRefLower == "desktop"
               || sourceRefLower == "default-bus" || sourceRefLower == "default-source"
               || sourceRefLower.empty()) {
      // Named aliases: all map to full-screen capture on default display
      inputSpec = display + "+0,0";
    }

    plan.supported = true;
    plan.backendId = id();
    plan.ffmpegArgs = {
      "ffmpeg",
      "-hide_banner",
      "-loglevel", "error",
      "-f", "x11grab",
      "-framerate", std::to_string(fps),
      "-draw_mouse", request.drawMouse ? "1" : "0"
    };
    if (useWindowId && !windowId.empty()) {
      // Window-ID mode: x11grab sizes the capture to the window automatically
      plan.ffmpegArgs.push_back("-window_id");
      plan.ffmpegArgs.push_back(windowId);
      plan.ffmpegArgs.push_back("-i");
      plan.ffmpegArgs.push_back(inputSpec);
    } else {
      // Region mode: specify explicit video_size for the capture area
      plan.ffmpegArgs.push_back("-video_size");
      plan.ffmpegArgs.push_back(std::to_string(w) + "x" + std::to_string(h));
      plan.ffmpegArgs.push_back("-i");
      plan.ffmpegArgs.push_back(inputSpec);
    }
    // Scale to requested output dimensions and output raw RGBA to stdout
    plan.ffmpegArgs.push_back("-vf");
    plan.ffmpegArgs.push_back("scale=" + std::to_string(w) + ":" + std::to_string(h) + ":flags=neighbor");
    plan.ffmpegArgs.push_back("-f");
    plan.ffmpegArgs.push_back("rawvideo");
    plan.ffmpegArgs.push_back("-pix_fmt");
    plan.ffmpegArgs.push_back("rgba");
    plan.ffmpegArgs.push_back("pipe:1");
#else
    (void) request;
    plan.supported = false;
    plan.backendId = id();
    plan.reasonUnavailable = "x11grab backend only available on Linux";
#endif
    return plan;
  }
};

// ── Linux camera capture via ffmpeg V4L2 ────────────────────────────────────
// Supports sourceRef formats:
//   "default-camera" / "default" / "" — /dev/video0
//   "v4l2:/dev/video2"               — explicit V4L2 device path
//   "/dev/video1"                      — direct device path
//   "2"                                — numeric shorthand → /dev/video2
class LinuxCameraCaptureBackend final : public SourceCaptureBackend {
 public:
  SourceCaptureKind kind() const override {
    return SourceCaptureKind::Camera;
  }

  std::string id() const override {
    return "v4l2";
  }

  SourceCapturePlan plan(const SourceCaptureRequest& request) const override {
    SourceCapturePlan plan;
#if defined(__linux__)
    std::string sourceRef = trim(request.sourceRef);
    std::string sourceRefLower = toLower(sourceRef);
    int w = std::max(1, request.width);
    int h = std::max(1, request.height);
    int fps = std::clamp(request.frameRate, 1, 120);

    // Resolve the sourceRef to a /dev/videoN device path.
    auto cameraDeviceForRef = [&](const std::string& refLower, const std::string& rawRef) -> std::string {
      if (refLower.empty() || refLower == "default-camera" || refLower == "default") {
        return "/dev/video0";
      }
      if (refLower.rfind("v4l2:", 0) == 0 && rawRef.size() > 5) {
        return trim(rawRef.substr(5));  // Strip "v4l2:" prefix
      }
      if (refLower.rfind("/dev/video", 0) == 0) {
        return rawRef;  // Already a device path
      }
      // Bare number → /dev/videoN shorthand
      bool numeric = !rawRef.empty() &&
        std::all_of(rawRef.begin(), rawRef.end(), [](unsigned char ch) { return std::isdigit(ch); });
      if (numeric) {
        return "/dev/video" + rawRef;
      }
      return rawRef;
    };

    std::string device = cameraDeviceForRef(sourceRefLower, sourceRef);
    if (device.empty()) {
      device = "/dev/video0";
    }

    plan.supported = true;
    plan.backendId = id();
    plan.ffmpegArgs = {
      "ffmpeg",
      "-hide_banner",
      "-loglevel", "error",
      "-f", "v4l2",
      "-thread_queue_size", "64",  // Prevent "overrun" warnings on slower cameras
      "-framerate", std::to_string(fps),
      "-i", device,
      "-vf", "scale=" + std::to_string(w) + ":" + std::to_string(h) + ":flags=neighbor",
      "-f", "rawvideo",
      "-pix_fmt", "rgba",
      "pipe:1"
    };
#else
    (void) request;
    plan.supported = false;
    plan.backendId = id();
    plan.reasonUnavailable = "v4l2 backend only available on Linux";
#endif
    return plan;
  }
};

// ── Non-Linux camera capture scaffold ───────────────────────────────────────
// Placeholder for Windows (Media Foundation) and macOS (AVFoundation) camera
// backends. Returns unsupported with a diagnostic message until implemented.
#if !defined(__linux__)
class UnsupportedCameraCaptureBackend final : public SourceCaptureBackend {
 public:
  SourceCaptureKind kind() const override {
    return SourceCaptureKind::Camera;
  }

  std::string id() const override {
#if defined(_WIN32)
    return "mediafoundation";
#elif defined(__APPLE__)
    return "avfoundation";
#else
    return "unknown";
#endif
  }

  SourceCapturePlan plan(const SourceCaptureRequest& request) const override {
    (void) request;
    SourceCapturePlan plan;
    plan.supported = false;
    plan.backendId = id();
    plan.reasonUnavailable = "camera capture backend scaffold only";
    return plan;
  }
};
#endif

// ── App texture capture (Syphon/Spout proxy) ────────────────────────────────
// On Linux, falls back to x11grab desktop capture since there's no native
// Syphon/Spout. On macOS/Windows, returns scaffold-only until native
// Syphon/Spout receive is implemented.
class LinuxAppTextureCaptureBackend final : public SourceCaptureBackend {
 public:
  SourceCaptureKind kind() const override {
    return SourceCaptureKind::AppTexture;
  }

  std::string id() const override {
    return "desktop-fallback";
  }

  SourceCapturePlan plan(const SourceCaptureRequest& request) const override {
#if defined(__linux__)
    // Delegate to x11grab as a fallback for app texture capture on Linux
    LinuxWindowCaptureBackend fallback;
    SourceCaptureRequest delegated = request;
    if (trim(delegated.sourceRef).empty()) {
      delegated.sourceRef = "default-bus";
    }
    SourceCapturePlan plan = fallback.plan(delegated);
    plan.backendId = id();
    if (plan.reasonUnavailable.empty()) {
      plan.reasonUnavailable = "native Syphon/Spout capture backend pending";
    }
    return plan;
#else
    (void) request;
    SourceCapturePlan plan;
    plan.supported = false;
    plan.backendId = id();
#if defined(__APPLE__)
    plan.reasonUnavailable = "native Syphon capture backend scaffold only";
#elif defined(_WIN32)
    plan.reasonUnavailable = "native Spout capture backend scaffold only";
#else
    plan.reasonUnavailable = "app texture backend unsupported on this platform";
#endif
    return plan;
#endif
  }
};

// ── Windows desktop/region capture via ffmpeg gdigrab ────────────────────────
// Supports sourceRef format:
//   "region:X,Y,W,H" — capture a specific screen region at pixel coordinates
//   anything else     — capture entire desktop at default offset (0,0)
#ifdef _WIN32
class WindowsGdigrabCaptureBackend final : public SourceCaptureBackend {
 public:
  SourceCaptureKind kind() const override {
    return SourceCaptureKind::Window;
  }

  std::string id() const override {
    return "gdigrab";
  }

  SourceCapturePlan plan(const SourceCaptureRequest& request) const override {
    int w = std::max(1, request.width);
    int h = std::max(1, request.height);
    int fps = std::clamp(request.frameRate, 1, 120);

    std::string src = trim(request.sourceRef);
    std::string srcLower = toLower(src);

    // "title:Window Title" — capture a specific window by its title via gdigrab
    if (src.rfind("title:", 0) == 0 && src.size() > 6) {
      std::string windowTitle = trim(src.substr(6));
      SourceCapturePlan plan;
      plan.supported = true;
      plan.backendId = id();
      plan.ffmpegArgs = {
        "ffmpeg",
        "-hide_banner",
        "-loglevel", "error",
        "-f", "gdigrab",
        "-framerate", std::to_string(fps),
        "-draw_mouse", request.drawMouse ? "1" : "0",
        "-i", "title=" + windowTitle,
        "-vf", "scale=" + std::to_string(request.width) + ":" + std::to_string(request.height) + ":flags=neighbor",
        "-f", "rawvideo",
        "-pix_fmt", "rgba",
        "pipe:1"
      };
      return plan;
    }

    // Parse "region:X,Y,W,H" format to extract capture offset and optional size
    int offsetX = 0, offsetY = 0;
    if (src.rfind("region:", 0) == 0) {
      std::string coords = src.substr(7);
      std::vector<int> vals;
      size_t start = 0;
      // Parse comma-separated integer values from the region string
      while (start <= coords.size()) {
        size_t end = coords.find(',', start);
        if (end == std::string::npos) end = coords.size();
        if (end > start) {
          try { vals.push_back(std::stoi(coords.substr(start, end - start))); }
          catch (...) {}
        }
        if (end == coords.size()) break;
        start = end + 1;
      }
      // vals[0]=X, vals[1]=Y, vals[2]=W, vals[3]=H (all optional)
      if (vals.size() >= 1) offsetX = vals[0];
      if (vals.size() >= 2) offsetY = vals[1];
      if (vals.size() >= 3) w = std::max(1, vals[2]);
      if (vals.size() >= 4) h = std::max(1, vals[3]);
    }

    SourceCapturePlan plan;
    plan.supported = true;
    plan.backendId = id();
    plan.ffmpegArgs = {
      "ffmpeg",
      "-hide_banner",
      "-loglevel", "error",
      "-f", "gdigrab",
      "-framerate", std::to_string(fps),
      "-draw_mouse", request.drawMouse ? "1" : "0",
      "-offset_x", std::to_string(offsetX),
      "-offset_y", std::to_string(offsetY),
      "-video_size", std::to_string(w) + "x" + std::to_string(h),
      "-i", "desktop",
      // Scale captured region to the final requested output dimensions
      "-vf", "scale=" + std::to_string(request.width) + ":" + std::to_string(request.height) + ":flags=neighbor",
      "-f", "rawvideo",
      "-pix_fmt", "rgba",
      "pipe:1"
    };
    return plan;
  }
};
#endif

// ── Catalog: enumerates all capture backends available on this platform ──────
class DefaultCaptureBackendCatalog final : public CaptureBackendCatalog {
 public:
  std::vector<CaptureBackendInfo> list() const override {
    std::vector<CaptureBackendInfo> out;

#if defined(__linux__)
    out.push_back({CaptureBackendKind::Window, "x11grab", "Window Capture (X11)", true, ""});
    out.push_back({CaptureBackendKind::Camera, "v4l2", "Camera Capture (V4L2)", true, ""});
    out.push_back({CaptureBackendKind::AppTexture, "desktop-fallback", "Syphon/Spout Fallback", true, "native Syphon/Spout backend pending"});
#endif

#if defined(__APPLE__)
    out.push_back({CaptureBackendKind::Window, "screencapturekit", "Window Capture (ScreenCaptureKit)", false, "backend scaffold only"});
    out.push_back({CaptureBackendKind::Camera, "avfoundation", "Camera Capture (AVFoundation)", false, "backend scaffold only"});
    out.push_back({CaptureBackendKind::AppTexture, "syphon", "Syphon App Texture", false, "backend scaffold only"});
#endif

#if defined(_WIN32)
    out.push_back({CaptureBackendKind::Window, "gdigrab", "Window/Region Capture (GDI grab)", true, ""});
    out.push_back({CaptureBackendKind::Camera, "mediafoundation", "Camera Capture (Media Foundation)", false, "backend scaffold only"});
    out.push_back({CaptureBackendKind::AppTexture, "spout", "Spout App Texture", false, "backend scaffold only"});
#endif

    if (out.empty()) {
      out.push_back({CaptureBackendKind::Window, "unknown", "Window Capture", false, "unsupported platform"});
    }

    return out;
  }
};

}  // namespace

// ── Factory functions ───────────────────────────────────────────────────────
// Each factory returns the appropriate platform-specific backend implementation.

std::unique_ptr<CaptureBackendCatalog> createCaptureBackendCatalog() {
  return std::make_unique<DefaultCaptureBackendCatalog>();
}

std::unique_ptr<SourceCaptureBackend> createWindowCaptureBackend() {
#ifdef _WIN32
  return std::make_unique<WindowsGdigrabCaptureBackend>();
#else
  return std::make_unique<LinuxWindowCaptureBackend>();
#endif
}

std::unique_ptr<SourceCaptureBackend> createCameraCaptureBackend() {
#if defined(__linux__)
  return std::make_unique<LinuxCameraCaptureBackend>();
#else
  return std::make_unique<UnsupportedCameraCaptureBackend>();
#endif
}

std::unique_ptr<SourceCaptureBackend> createAppTextureCaptureBackend() {
  return std::make_unique<LinuxAppTextureCaptureBackend>();
}

// Convenience: select the right backend for the capture kind, then generate a plan.
// Called by media_engine.cpp::buildSourceCaptureArgs().
SourceCapturePlan planSourceCapture(const SourceCaptureRequest& request) {
  std::unique_ptr<SourceCaptureBackend> backend;
  switch (request.kind) {
    case SourceCaptureKind::Window:
      backend = createWindowCaptureBackend();
      break;
    case SourceCaptureKind::Camera:
      backend = createCameraCaptureBackend();
      break;
    case SourceCaptureKind::AppTexture:
    default:
      backend = createAppTextureCaptureBackend();
      break;
  }
  if (!backend) {
    SourceCapturePlan plan;
    plan.supported = false;
    plan.backendId = "none";
    plan.reasonUnavailable = "capture backend factory failed";
    return plan;
  }
  return backend->plan(request);
}

// ── Window enumeration for the window picker UI ────────────────────────────
// Lists visible, titled windows that can be captured. Used by the inspector
// dropdown to let users pick a specific window for WindowSource cues.

std::vector<CaptureWindowInfo> listCaptureWindows() {
  std::vector<CaptureWindowInfo> result;
  // Always offer full-screen desktop capture as the first option
  result.push_back({"desktop", "Desktop (full screen)"});

#ifdef _WIN32
  // Use EnumWindows to collect all visible, titled top-level windows.
  // Each window's title becomes a "title:Window Title" sourceRef.
  struct EnumCtx {
    std::vector<CaptureWindowInfo>* out;
  };
  EnumCtx ctx {&result};

  EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;

    // Skip windows without meaningful titles
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return TRUE;

    // Skip cloaked UWP windows (hidden system overlays)
    DWORD cloaked = 0;
    // DwmGetWindowAttribute may not be available, use GetWindowLong check instead
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return TRUE;  // Skip tool windows

    // Get the window title
    std::vector<wchar_t> buf(static_cast<size_t>(len) + 1);
    GetWindowTextW(hwnd, buf.data(), static_cast<int>(buf.size()));
    std::wstring wideTitle(buf.data());

    // Convert wide string to UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideTitle.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return TRUE;
    std::string title(static_cast<size_t>(utf8Len) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideTitle.c_str(), -1, title.data(), utf8Len, nullptr, nullptr);

    if (title.empty()) return TRUE;

    ctx->out->push_back({"title:" + title, title});
    return TRUE;
  }, reinterpret_cast<LPARAM>(&ctx));
#elif defined(__linux__)
  // On Linux, window enumeration would require X11/XCB — not implemented yet.
  // The text editor fallback still works for typing x11grab source refs.
#endif

  return result;
}

}  // namespace deckboy::platform
