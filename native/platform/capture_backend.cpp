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
#include "platform/dynamic_library.hpp"  // dlopen libX11 for the Linux window picker
#include "core/utils.hpp"
#include "core/paths.hpp"    // executablePath() to locate the mac capture helper
#include <cstdio>
#include <filesystem>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>   // CGWindowListCopyWindowInfo for the window picker
#include <CoreFoundation/CoreFoundation.h>
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

// ── ONE SOURCE-REF GRAMMAR FOR ALL THREE PLATFORMS ─────────────────────────
//
// Each backend used to invent its own vocabulary, and -- worse -- treat a ref
// it did not recognise as "capture the whole desktop" while still reporting
// supported=true. So a window cue authored on one machine captured something
// else entirely on the other two, and nothing anywhere said so:
//
//   title:Firefox   captured a window on Windows, display 0 on macOS,
//                   and the whole screen on Linux
//   window:41       captured a window on macOS and the whole desktop elsewhere
//   screen:1        captured display 1 on macOS and the whole desktop elsewhere
//
// The refs below are the shared vocabulary. Every backend parses them here, and
// anything a backend genuinely cannot do is now refused with a reason instead of
// quietly turning into a full-desktop grab -- because a wrong picture that plays
// is more expensive on air than a cue that says what it needs.
//
// The legacy per-platform spellings still parse, so existing shows keep working.
enum class SourceRefMode {
  Desktop,      // whole primary display (the explicit, asked-for kind)
  Screen,       // a numbered display
  WindowId,     // a specific window, by platform window id
  WindowTitle,  // a specific window, by title
  Region,       // a rectangle of the desktop
  RawX11,       // an explicit x11grab input spec (Linux only, legacy)
  Unknown,      // parsed as nothing we know -- never silently captured
};

struct ParsedSourceRef {
  SourceRefMode mode = SourceRefMode::Desktop;
  int screen = 0;
  std::string windowId;     // kept as text: X11 ids are hex, others decimal
  std::string title;
  int x = 0, y = 0, w = 0, h = 0;   // Region; w/h 0 means "the request's size"
  std::string raw;          // RawX11 spec
  std::string original;     // what the operator actually wrote, for messages
};

inline bool refIsAllDigits(const std::string& value) {
  return !value.empty() && std::all_of(value.begin(), value.end(),
    [](unsigned char c) { return std::isdigit(c) != 0; });
}

ParsedSourceRef parseSourceRef(const std::string& rawRef) {
  ParsedSourceRef out;
  const std::string ref = trim(rawRef);
  const std::string lower = toLower(ref);
  out.original = ref;

  // Empty and the "give me something sensible" aliases all mean the desktop.
  if (ref.empty() || lower == "desktop" || lower == "screen" ||
      lower == "default" || lower == "active-window" ||
      lower == "default-window" || lower == "default-source" ||
      lower == "default-bus") {
    out.mode = SourceRefMode::Desktop;
    return out;
  }
  if (lower.rfind("screen:", 0) == 0) {
    out.mode = SourceRefMode::Screen;
    out.screen = std::atoi(ref.substr(7).c_str());
    return out;
  }
  if (lower.rfind("display:", 0) == 0) {
    // Not a spelling any backend accepted, but the obvious thing to type --
    // and it used to land silently on display 0.
    out.mode = SourceRefMode::Screen;
    out.screen = std::atoi(ref.substr(8).c_str());
    return out;
  }
  if (lower.rfind("window:", 0) == 0) {
    out.mode = SourceRefMode::WindowId;
    out.windowId = trim(ref.substr(7));
    return out;
  }
  // Legacy Linux spellings for the same thing.
  if (lower.rfind("window_id:", 0) == 0) {
    out.mode = SourceRefMode::WindowId;
    out.windowId = trim(ref.substr(10));
    return out;
  }
  if (lower.rfind("id:", 0) == 0) {
    out.mode = SourceRefMode::WindowId;
    out.windowId = trim(ref.substr(3));
    return out;
  }
  if (lower.rfind("title:", 0) == 0) {
    out.mode = SourceRefMode::WindowTitle;
    out.title = trim(ref.substr(6));
    return out;
  }
  if (lower.rfind("region:", 0) == 0) {
    out.mode = SourceRefMode::Region;
    const std::string coords = ref.substr(7);
    std::vector<int> vals;
    std::size_t start = 0;
    while (start <= coords.size()) {
      std::size_t end = coords.find(',', start);
      if (end == std::string::npos) {
        end = coords.size();
      }
      if (end > start) {
        try {
          vals.push_back(std::stoi(coords.substr(start, end - start)));
        } catch (...) {
          // A malformed number leaves the field at its default rather than
          // aborting the whole ref: "region:0,0" is a legitimate shorthand.
        }
      }
      if (end == coords.size()) {
        break;
      }
      start = end + 1;
    }
    if (vals.size() >= 1) out.x = vals[0];
    if (vals.size() >= 2) out.y = vals[1];
    if (vals.size() >= 3) out.w = std::max(1, vals[2]);
    if (vals.size() >= 4) out.h = std::max(1, vals[3]);
    return out;
  }
  if (lower.rfind("x11:", 0) == 0) {
    out.mode = SourceRefMode::RawX11;
    out.raw = trim(ref.substr(4));
    return out;
  }
  if (ref[0] == ':' || ref[0] == '+') {
    out.mode = SourceRefMode::RawX11;
    out.raw = ref;
    return out;
  }
  // A bare number has always meant a display index.
  if (refIsAllDigits(ref)) {
    out.mode = SourceRefMode::Screen;
    out.screen = std::atoi(ref.c_str());
    return out;
  }
  out.mode = SourceRefMode::Unknown;
  return out;
}

// The same sentence everywhere, so an unsupported ref reads the same whichever
// machine the show was opened on.
std::string refUnsupportedReason(const ParsedSourceRef& ref,
                                 const char* backend,
                                 const char* what) {
  return std::string(backend) + " cannot capture " + what + " (\"" +
         ref.original + "\")";
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
    const ParsedSourceRef ref = parseSourceRef(request.sourceRef);
    std::string display = defaultDisplay(request.display);
    int w = std::max(1, request.width);
    int h = std::max(1, request.height);
    int fps = std::clamp(request.frameRate, 1, 120);

    std::string inputSpec = display + "+0,0";
    bool useWindowId = false;
    std::string windowId;

    switch (ref.mode) {
      case SourceRefMode::Desktop:
        inputSpec = display + "+0,0";
        break;
      case SourceRefMode::Screen:
        // X11 numbers screens inside the display string, so display 1 is
        // ":1.0" -- not the same axis as macOS's display index, but the same
        // intent, and it beats silently grabbing screen 0.
        inputSpec = ":" + std::to_string(ref.screen) + ".0+0,0";
        break;
      case SourceRefMode::WindowId:
        useWindowId = true;
        windowId = ref.windowId;
        inputSpec = display;
        break;
      case SourceRefMode::Region:
        inputSpec = display + "+" + std::to_string(ref.x) + "," + std::to_string(ref.y);
        if (ref.w > 0) w = ref.w;
        if (ref.h > 0) h = ref.h;
        break;
      case SourceRefMode::RawX11:
        inputSpec = ref.raw;
        if (inputSpec.empty()) {
          inputSpec = display + "+0,0";
        } else if (inputSpec[0] == '+') {
          inputSpec = display + inputSpec;
        } else if (inputSpec.find('+') == std::string::npos) {
          inputSpec += "+0,0";
        }
        break;
      case SourceRefMode::WindowTitle:
        // x11grab takes an id, not a title. Resolving one to the other needs
        // the window list, which the picker already has -- so this is a
        // re-pick, not a capture we can guess at.
        plan.supported = false;
        plan.backendId = id();
        plan.reasonUnavailable =
          refUnsupportedReason(ref, "x11grab", "a window by title; re-pick the window on this machine");
        return plan;
      case SourceRefMode::Unknown:
        plan.supported = false;
        plan.backendId = id();
        plan.reasonUnavailable = refUnsupportedReason(ref, "x11grab", "that source");
        return plan;
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

// ── Windows camera/capture-device input via ffmpeg dshow ────────────────────
// One backend covers webcams AND capture devices (HDMI capture sticks, etc.)
// — they are all DirectShow video devices to the OS. sourceRef is the device
// name (optionally prefixed "video="); the app resolves placeholder refs to a
// real device via dshow enumeration before the cue reaches this plan.
#ifdef _WIN32
class WindowsDshowCameraBackend final : public SourceCaptureBackend {
 public:
  SourceCaptureKind kind() const override {
    return SourceCaptureKind::Camera;
  }

  std::string id() const override {
    return "dshow";
  }

  SourceCapturePlan plan(const SourceCaptureRequest& request) const override {
    std::string device = trim(request.sourceRef);
    if (device.rfind("video=", 0) == 0) {
      device = trim(device.substr(6));
    }
    SourceCapturePlan plan;
    plan.backendId = id();
    std::string deviceLower = toLower(device);
    if (device.empty() || deviceLower == "default-camera" || deviceLower == "default") {
      plan.supported = false;
      plan.reasonUnavailable = "no capture device selected";
      return plan;
    }
    plan.supported = true;
    // No -framerate: let dshow negotiate the device's native mode — forcing
    // a rate many devices don't offer makes ffmpeg fail outright.
    plan.ffmpegArgs = {
      "ffmpeg",
      "-hide_banner",
      "-loglevel", "error",
      "-f", "dshow",
      "-i", "video=" + device,
      "-vf", "scale=" + std::to_string(request.width) + ":" + std::to_string(request.height) + ":flags=fast_bilinear",
      "-f", "rawvideo",
      "-pix_fmt", "rgba",
      "pipe:1"
    };
    return plan;
  }
};
#endif

// ── macOS camera capture via ffmpeg avfoundation ────────────────────────────
// Same shape as the Linux v4l2 backend, just ffmpeg's macOS input device.
// avfoundation addresses devices by INDEX ("0", "1", ...) or exact name; the
// default camera is index 0. The input spec is "<video>:<audio>" and we want
// video only, so ":none".
//
// sourceRef formats:
//   "default-camera" / "default" / ""  -> "0"
//   "avfoundation:1" or "1"            -> device index 1
//   "FaceTime HD Camera"               -> passed through as an exact name
//
// PERMISSION: the first capture triggers the macOS camera TCC prompt, attributed
// to Deckboy.app via the NSCameraUsageDescription in Info.plist. If the user
// declines, ffmpeg fails to open the device and the deck reracks with the usual
// decode-stall path — no crash.
#if defined(__APPLE__)
class MacCameraCaptureBackend final : public SourceCaptureBackend {
 public:
  SourceCaptureKind kind() const override { return SourceCaptureKind::Camera; }
  std::string id() const override { return "avfoundation"; }

  SourceCapturePlan plan(const SourceCaptureRequest& request) const override {
    SourceCapturePlan plan;
    std::string ref = trim(request.sourceRef);
    std::string refLower = toLower(ref);
    int w = std::max(1, request.width);
    int h = std::max(1, request.height);
    int fps = std::clamp(request.frameRate, 1, 60);

    std::string device;
    if (refLower.empty() || refLower == "default-camera" || refLower == "default") {
      device = "0";
    } else if (refLower.rfind("avfoundation:", 0) == 0 && ref.size() > 13) {
      device = trim(ref.substr(13));
    } else {
      device = ref;  // numeric index or exact device name
    }

    // avfoundation is strict: it hard-rejects any (size, framerate, pixel-format)
    // the device doesn't list EXACTLY, and does NOT snap to the nearest. On the
    // MacBook camera ffmpeg's own defaults are both rejected — 29.97 fps ("not
    // supported by the device") and yuv420p ("not supported... overriding") —
    // which made the cue open then freeze with zero frames delivered. The device
    // reports modes like 1280x720@[15,30] in nv12/uyvy422, so pin an explicit
    // supported mode (1280x720 @ exactly 30 @ nv12) and let the scale filter
    // resample to whatever size the cue reads. 1280x720@30 is near-universal;
    // external webcams that lack it fail cleanly and the deck reracks.
    int camFps = (fps <= 22) ? 15 : 30;   // device lists 15 and 30 — snap to one
    plan.supported = true;
    plan.backendId = id();
    plan.ffmpegArgs = {
      "ffmpeg",
      "-hide_banner",
      "-loglevel", "error",
      "-f", "avfoundation",
      "-framerate", std::to_string(camFps),
      "-video_size", "1280x720",
      "-pixel_format", "nv12",
      "-i", device + ":none",
      "-vf", "scale=" + std::to_string(w) + ":" + std::to_string(h) + ":flags=neighbor",
      "-f", "rawvideo",
      "-pix_fmt", "rgba",
      "pipe:1"
    };
    return plan;
  }
};
#endif

// ── macOS screen capture via ffmpeg avfoundation ────────────────────────────
// avfoundation exposes screens as capture devices named "Capture screen N",
// listed AFTER the cameras. On current macOS (verified on 26.6) avfoundation no
// longer enumerates a "Capture screen" device — Apple deprecated
// AVCaptureScreenInput for ScreenCaptureKit — so a real screen backend needs
// ScreenCaptureKit (native, a future task). Until then macOS window/screen
// capture reports honestly unsupported rather than shipping an ffmpeg command
// that captures nothing and hangs.
#if defined(__APPLE__)
// ASK FOR SCREEN RECORDING AS THE APP, BEFORE THE HELPER NEEDS IT.
//
// deckboy-sckcapture is a bare executable inside the bundle with no bundle
// identity of its own, and macOS will not show a permission prompt on behalf of
// one. It just answers "The user declined TCCs for application, window, display
// capture" and exits -- so on a fresh install, or after any update changes the
// binary, screen and window sources and every browser cue fail with a message
// telling the operator to visit System Settings, and no prompt ever appears.
//
// CGRequestScreenCaptureAccess is the app asking in its own name, which is the
// prompt people expect and the one the system will actually draw. Once granted,
// the helper inherits it as a child of the responsible process.
//
// Called once per run: the system only prompts the first time, and a repeated
// call on every cue would be a wasted round trip through TCC on the render path.
bool ensureScreenCaptureAccess() {
  static const bool granted = [] {
    if (CGPreflightScreenCaptureAccess()) {
      return true;
    }
    return static_cast<bool>(CGRequestScreenCaptureAccess());
  }();
  return granted;
}

class MacScreenCaptureBackend final : public SourceCaptureBackend {
 public:
  SourceCaptureKind kind() const override { return SourceCaptureKind::Window; }
  std::string id() const override { return "screencapturekit"; }

  SourceCapturePlan plan(const SourceCaptureRequest& request) const override {
    SourceCapturePlan plan;
    plan.backendId = id();

    // The helper lives next to the executable (Contents/MacOS/ in a bundle).
    std::filesystem::path exeDir =
        deckboy::core::Paths::executablePath().parent_path();
    std::filesystem::path helper = exeDir / "deckboy-sckcapture";
    std::error_code ec;
    if (exeDir.empty() || !std::filesystem::exists(helper, ec)) {
      plan.supported = false;
      plan.reasonUnavailable = "deckboy-sckcapture helper not found beside the app";
      return plan;
    }

    // Ask in the app's own name before the helper is spawned; without this the
    // helper is refused with no prompt ever shown.
    if (!ensureScreenCaptureAccess()) {
      plan.supported = false;
      plan.backendId = id();
      plan.reasonUnavailable =
        "screen recording permission not granted -- allow Deckboy in System "
        "Settings > Privacy & Security > Screen Recording, then reopen Deckboy";
      return plan;
    }
    const ParsedSourceRef ref = parseSourceRef(request.sourceRef);
    int display = 0;
    std::string windowId;
    switch (ref.mode) {
      case SourceRefMode::Desktop:
        display = 0;
        break;
      case SourceRefMode::Screen:
        display = ref.screen;
        break;
      case SourceRefMode::WindowId:
        windowId = ref.windowId;
        break;
      case SourceRefMode::WindowTitle:
        // ScreenCaptureKit selects by window id. The picker resolves a title to
        // one; this layer will not guess, because guessing is how a cue ends up
        // on the wrong window with nobody told.
        plan.supported = false;
        plan.reasonUnavailable = refUnsupportedReason(
          ref, "ScreenCaptureKit", "a window by title; re-pick the window on this machine");
        return plan;
      case SourceRefMode::Region:
        plan.supported = false;
        plan.reasonUnavailable = refUnsupportedReason(
          ref, "ScreenCaptureKit", "an arbitrary desktop region; capture a display or a window");
        return plan;
      case SourceRefMode::RawX11:
        plan.supported = false;
        plan.reasonUnavailable = refUnsupportedReason(ref, "ScreenCaptureKit", "an X11 source");
        return plan;
      case SourceRefMode::Unknown:
        plan.supported = false;
        plan.reasonUnavailable = refUnsupportedReason(ref, "ScreenCaptureKit", "that source");
        return plan;
    }

    int w = std::max(1, request.width);
    int h = std::max(1, request.height);
    int fps = std::clamp(request.frameRate, 1, 60);

    plan.supported = true;
    // These reuse the ffmpegArgs channel: MediaEngine spawns args[0] and reads
    // raw RGBA frames from its stdout, and the helper emits exactly that.
    plan.ffmpegArgs = {
      helper.string(),
      "--width", std::to_string(w),
      "--height", std::to_string(h),
      "--fps", std::to_string(fps),
    };
    // A specific window overrides the display; otherwise capture the display.
    if (!windowId.empty()) {
      plan.ffmpegArgs.push_back("--window");
      plan.ffmpegArgs.push_back(windowId);
    } else {
      plan.ffmpegArgs.push_back("--display");
      plan.ffmpegArgs.push_back(std::to_string(display));
    }
    return plan;
  }
};
#endif

// ── Camera capture fallback for platforms without a real backend ────────────
#if !defined(__linux__) && !defined(_WIN32) && !defined(__APPLE__)
class UnsupportedCameraCaptureBackend final : public SourceCaptureBackend {
 public:
  SourceCaptureKind kind() const override {
    return SourceCaptureKind::Camera;
  }

  std::string id() const override {
    return "unknown";
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

    const ParsedSourceRef ref = parseSourceRef(request.sourceRef);
    SourceCapturePlan plan;
    plan.backendId = id();

    auto finish = [&](const std::string& input, int offsetX, int offsetY,
                      bool sized) {
      plan.supported = true;
      plan.ffmpegArgs = {
        "ffmpeg",
        "-hide_banner",
        "-loglevel", "error",
        "-f", "gdigrab",
        "-framerate", std::to_string(fps),
        "-draw_mouse", request.drawMouse ? "1" : "0",
      };
      if (sized) {
        plan.ffmpegArgs.push_back("-offset_x");
        plan.ffmpegArgs.push_back(std::to_string(offsetX));
        plan.ffmpegArgs.push_back("-offset_y");
        plan.ffmpegArgs.push_back(std::to_string(offsetY));
        plan.ffmpegArgs.push_back("-video_size");
        plan.ffmpegArgs.push_back(std::to_string(w) + "x" + std::to_string(h));
      }
      plan.ffmpegArgs.push_back("-i");
      plan.ffmpegArgs.push_back(input);
      plan.ffmpegArgs.push_back("-vf");
      plan.ffmpegArgs.push_back("scale=" + std::to_string(request.width) + ":" +
                                std::to_string(request.height) + ":flags=neighbor");
      plan.ffmpegArgs.push_back("-f");
      plan.ffmpegArgs.push_back("rawvideo");
      plan.ffmpegArgs.push_back("-pix_fmt");
      plan.ffmpegArgs.push_back("rgba");
      plan.ffmpegArgs.push_back("pipe:1");
      return plan;
    };

    switch (ref.mode) {
      case SourceRefMode::WindowTitle:
        // gdigrab matches the title EXACTLY, so an app that retitles itself
        // (a browser, per tab) needs re-picking after the title changes.
        return finish("title=" + ref.title, 0, 0, false);
      case SourceRefMode::Region:
        if (ref.w > 0) w = ref.w;
        if (ref.h > 0) h = ref.h;
        return finish("desktop", ref.x, ref.y, true);
      case SourceRefMode::Desktop:
        return finish("desktop", 0, 0, true);
      case SourceRefMode::Screen:
        // gdigrab has no display selector: everything is one virtual desktop.
        // Display 0 is that desktop's origin and is the honest answer; any
        // other index would need the monitor rectangle, which belongs to the
        // picker, so say so rather than hand back the wrong screen.
        if (ref.screen == 0) {
          return finish("desktop", 0, 0, true);
        }
        plan.supported = false;
        plan.reasonUnavailable = refUnsupportedReason(
          ref, "gdigrab", "a numbered display; use a region or re-pick the window");
        return plan;
      case SourceRefMode::WindowId:
        // A window id from another platform means nothing to gdigrab, and the
        // old code turned it into a full-desktop grab without a word.
        plan.supported = false;
        plan.reasonUnavailable = refUnsupportedReason(
          ref, "gdigrab", "a window by id; re-pick the window on this machine");
        return plan;
      case SourceRefMode::RawX11:
        plan.supported = false;
        plan.reasonUnavailable = refUnsupportedReason(ref, "gdigrab", "an X11 source");
        return plan;
      case SourceRefMode::Unknown:
        plan.supported = false;
        plan.reasonUnavailable = refUnsupportedReason(ref, "gdigrab", "that source");
        return plan;
    }
    plan.supported = false;
    plan.reasonUnavailable = refUnsupportedReason(ref, "gdigrab", "that source");
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
    // Camera capture goes through ffmpeg's avfoundation input — real, verified
    // (the device enumerates as "[0] <model> Camera"). First use triggers the
    // camera permission prompt via NSCameraUsageDescription.
    out.push_back({CaptureBackendKind::Camera, "avfoundation", "Camera Capture (AVFoundation)", true, ""});
    // Screen capture via the deckboy-sckcapture ScreenCaptureKit helper (real —
    // avfoundation screen capture was removed on current macOS). Whole-display
    // capture; per-window is a future extension of the same helper.
    out.push_back({CaptureBackendKind::Window, "screencapturekit", "Screen Capture (ScreenCaptureKit)", true, ""});
    out.push_back({CaptureBackendKind::AppTexture, "syphon", "Syphon App Texture", false, "backend scaffold only"});
#endif

#if defined(_WIN32)
    out.push_back({CaptureBackendKind::Window, "gdigrab", "Window/Region Capture (GDI grab)", true, ""});
    out.push_back({CaptureBackendKind::Camera, "dshow", "Camera/Capture Device (DirectShow)", true, ""});
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
#if defined(_WIN32)
  return std::make_unique<WindowsGdigrabCaptureBackend>();
#elif defined(__APPLE__)
  return std::make_unique<MacScreenCaptureBackend>();
#else
  return std::make_unique<LinuxWindowCaptureBackend>();
#endif
}

std::unique_ptr<SourceCaptureBackend> createCameraCaptureBackend() {
#if defined(__linux__)
  return std::make_unique<LinuxCameraCaptureBackend>();
#elif defined(_WIN32)
  return std::make_unique<WindowsDshowCameraBackend>();
#elif defined(__APPLE__)
  return std::make_unique<MacCameraCaptureBackend>();
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

    // Tool windows are system overlays and palettes, not things anyone means
    // to capture. Cloaked UWP windows were meant to be skipped here too, via
    // DwmGetWindowAttribute; that was never written, and the variable for it
    // sat unread ever since.
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return TRUE;

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
#elif defined(__APPLE__)
  // Enumerate on-screen windows via CoreGraphics. Each becomes a
  // "window:<CGWindowID>" sourceRef; the sckcapture helper resolves that id to
  // an SCWindow and captures just that window. The numeric id is stabler than a
  // title (titles repeat and change), and the display name pairs the owning app
  // with the window title so the dropdown is readable. (kCGWindowName is only
  // populated once Screen Recording is granted; owner name is always present,
  // so we fall back to it.)
  CFArrayRef windowList = CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
      kCGNullWindowID);
  if (windowList) {
    const CFIndex count = CFArrayGetCount(windowList);
    for (CFIndex i = 0; i < count; ++i) {
      auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windowList, i));
      if (!info) continue;

      // Normal application windows live on layer 0; skip menus, the Dock,
      // shadows and other chrome that sit on other layers.
      int layer = -1;
      if (auto layerNum = static_cast<CFNumberRef>(
              CFDictionaryGetValue(info, kCGWindowLayer))) {
        CFNumberGetValue(layerNum, kCFNumberIntType, &layer);
      }
      if (layer != 0) continue;

      // Window id (kCGWindowNumber).
      long windowId = 0;
      if (auto numRef = static_cast<CFNumberRef>(
              CFDictionaryGetValue(info, kCGWindowNumber))) {
        CFNumberGetValue(numRef, kCFNumberLongType, &windowId);
      }
      if (windowId <= 0) continue;

      auto cfToUtf8 = [](CFStringRef s) -> std::string {
        if (!s) return std::string();
        CFIndex len = CFStringGetLength(s);
        CFIndex maxBytes = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
        std::string out(static_cast<size_t>(maxBytes), '\0');
        if (CFStringGetCString(s, out.data(), maxBytes, kCFStringEncodingUTF8)) {
          out.resize(std::strlen(out.c_str()));
          return out;
        }
        return std::string();
      };

      std::string owner = cfToUtf8(static_cast<CFStringRef>(
          CFDictionaryGetValue(info, kCGWindowOwnerName)));
      std::string title = cfToUtf8(static_cast<CFStringRef>(
          CFDictionaryGetValue(info, kCGWindowName)));

      // Skip our own windows and untitled/empty owners — they add noise.
      if (owner.empty()) continue;
      if (owner == "Deckboy") continue;
      if (owner == "Window Server" || owner == "Dock" || owner == "Control Center") continue;

      std::string label = owner;
      if (!title.empty() && title != owner) label += " — " + title;

      result.push_back({"window:" + std::to_string(windowId), label});
    }
    CFRelease(windowList);
  }
#elif defined(__linux__)
  // Enumerate managed top-level windows via EWMH _NET_CLIENT_LIST, using a
  // dlopen'd libX11 so the app carries no build-time X11 dependency (matches the
  // NDI/libltc convention). Each window becomes an "id:0x..." sourceRef, which
  // LinuxWindowCaptureBackend already feeds to x11grab's -window_id. If libX11
  // is missing or we're on a bare Wayland session, we fall through with just the
  // Desktop entry rather than failing.
  using XDisplay = void;
  using XWindow = unsigned long;
  using XAtom = unsigned long;

  DynamicLibrary xlib({"libX11.so.6", "libX11.so"});
  if (xlib.load()) {
    auto pXOpenDisplay = xlib.loadSymbol<XDisplay* (*)(const char*)>("XOpenDisplay");
    auto pXCloseDisplay = xlib.loadSymbol<int (*)(XDisplay*)>("XCloseDisplay");
    auto pXDefaultRootWindow = xlib.loadSymbol<XWindow (*)(XDisplay*)>("XDefaultRootWindow");
    auto pXInternAtom = xlib.loadSymbol<XAtom (*)(XDisplay*, const char*, int)>("XInternAtom");
    auto pXGetWindowProperty = xlib.loadSymbol<int (*)(
        XDisplay*, XWindow, XAtom, long, long, int, XAtom,
        XAtom*, int*, unsigned long*, unsigned long*, unsigned char**)>("XGetWindowProperty");
    auto pXFree = xlib.loadSymbol<int (*)(void*)>("XFree");
    auto pXFetchName = xlib.loadSymbol<int (*)(XDisplay*, XWindow, char**)>("XFetchName");

    if (pXOpenDisplay && pXCloseDisplay && pXDefaultRootWindow && pXInternAtom &&
        pXGetWindowProperty && pXFree) {
      if (XDisplay* dpy = pXOpenDisplay(nullptr)) {
        const XAtom XA_WINDOW = 33;  // from Xatom.h, stable
        XAtom clientList = pXInternAtom(dpy, "_NET_CLIENT_LIST", 1 /*only if exists*/);
        XAtom netWmName = pXInternAtom(dpy, "_NET_WM_NAME", 0);
        XAtom utf8 = pXInternAtom(dpy, "UTF8_STRING", 0);

        // Resolve a window's title: prefer UTF-8 _NET_WM_NAME, fall back to the
        // legacy WM_NAME via XFetchName.
        auto titleOf = [&](XWindow w) -> std::string {
          XAtom aType = 0; int aFmt = 0; unsigned long n = 0, after = 0;
          unsigned char* data = nullptr;
          if (netWmName &&
              pXGetWindowProperty(dpy, w, netWmName, 0, 1024, 0, utf8,
                                  &aType, &aFmt, &n, &after, &data) == 0 && data) {
            std::string s(reinterpret_cast<char*>(data), static_cast<size_t>(n));
            pXFree(data);
            if (!s.empty()) return s;
          }
          if (pXFetchName) {
            char* nm = nullptr;
            if (pXFetchName(dpy, w, &nm) != 0 && nm) {
              std::string s(nm);
              pXFree(nm);
              return s;
            }
          }
          return std::string();
        };

        XWindow root = pXDefaultRootWindow(dpy);
        XAtom aType = 0; int aFmt = 0; unsigned long n = 0, after = 0;
        unsigned char* data = nullptr;
        if (clientList &&
            pXGetWindowProperty(dpy, root, clientList, 0, 4096, 0, XA_WINDOW,
                                &aType, &aFmt, &n, &after, &data) == 0 && data) {
          // Format-32 properties come back as an array of `long` (native width).
          auto* wins = reinterpret_cast<XWindow*>(data);
          for (unsigned long i = 0; i < n; ++i) {
            XWindow w = wins[i];
            std::string title = titleOf(w);
            if (title.empty()) continue;
            char idbuf[32];
            std::snprintf(idbuf, sizeof(idbuf), "id:0x%lx", w);
            result.push_back({idbuf, title});
          }
          pXFree(data);
        }
        pXCloseDisplay(dpy);
      }
    }
  }
#endif

  return result;
}

}  // namespace deckboy::platform
