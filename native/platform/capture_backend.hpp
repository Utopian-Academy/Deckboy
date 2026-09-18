// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// capture_backend.hpp — Source capture abstraction (camera, window, app texture).
//
// Provides a platform-independent interface for capturing live video from:
//   - Windows/screens (WindowSource cues): x11grab on Linux, avfoundation on macOS
//   - Cameras (Camera cues): v4l2 on Linux, avfoundation on macOS
//   - App textures (Syphon cues): Syphon on macOS (Linux/Windows: unsupported)
//
// Architecture:
//   SourceCaptureRequest → planSourceCapture() → SourceCapturePlan
//     The plan contains the ffmpeg command-line args to start the capture.
//     MediaEngine calls this to get the args, then spawns ffmpeg itself.
//
//   CaptureBackendCatalog: lists available capture backends on the current platform
//   SourceCaptureBackend:  per-kind backend that generates ffmpeg args
//
// Platform backends are constructed by factory functions and selected
// automatically by planSourceCapture() based on the request kind.
//
// Implementation: capture_backend.cpp (platform-specific via #ifdef)
// ============================================================================

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace deckboy::platform {

// The kind of capture backend (for catalog listing).
enum class CaptureBackendKind {
  Window,      // screen/window capture
  Camera,      // webcam / video input device
  AppTexture   // Syphon/Spout shared texture (macOS/Windows)
};

// The kind of source to capture (mirrors CaptureBackendKind but used for requests).
enum class SourceCaptureKind {
  Window,
  Camera,
  AppTexture
};

// Describes one available capture backend on this platform.
struct CaptureBackendInfo {
  CaptureBackendKind kind = CaptureBackendKind::Window;
  std::string id;                  // internal identifier (e.g. "x11grab", "avfoundation")
  std::string displayName;        // human-readable name for the UI
  bool supported = false;         // true if this backend works on the current system
  std::string reasonUnavailable;  // why it's not supported (for diagnostics)
};

// A request to capture from a specific source at a given resolution/framerate.
struct SourceCaptureRequest {
  SourceCaptureKind kind = SourceCaptureKind::Window;
  std::string sourceRef;    // device/window identifier (e.g. "/dev/video0", ":0.0")
  int width = 1280;         // requested capture width
  int height = 720;         // requested capture height
  int frameRate = 30;       // requested capture fps
  bool drawMouse = true;    // include mouse cursor in capture
  std::string display;      // X11 $DISPLAY value (Linux only)
};

// The result of planning a capture: contains the ffmpeg args to execute.
struct SourceCapturePlan {
  bool supported = false;               // true if capture is possible
  std::string backendId;                // which backend will be used
  std::vector<std::string> ffmpegArgs;  // full ffmpeg command-line arguments
  std::string reasonUnavailable;        // error message if not supported
  // A second arg list to try if the first one dies without delivering a frame.
  // Windows window capture prefers Windows.Graphics.Capture and keeps the old
  // gdigrab line here, so a machine whose ffmpeg predates the WGC filter still
  // gets a picture instead of a dark cue.
  std::vector<std::string> fallbackFfmpegArgs;
  std::string fallbackBackendId;
  // Title of the window this plan captures, when the backend only receives
  // frames on repaint (WGC). The engine asks the window to redraw while it is
  // waiting for the first frame -- see nudgeWindowRepaint.
  std::string repaintWindowTitle;
  // The size frames will ACTUALLY arrive at, when that is not the size the
  // request asked for. A capture is never upscaled into the cue's raster: a
  // 960x540 window blown up to 4K in ffmpeg costs sixteen times the pipe
  // bandwidth and the readback to carry pixels it does not have, and the
  // compositor can scale on the GPU for nothing. Zero means "as requested".
  int frameWidth = 0;
  int frameHeight = 0;
  // The rate frames will actually arrive at, when the backend chose one. A
  // capture small enough to afford it runs at 60 rather than 30: half the
  // frames of the output means every other frame is a repeat, which reads as
  // judder on anything moving. Zero means "as requested".
  int frameRate = 0;
};

// Abstract catalog of available capture backends on the current platform.
class CaptureBackendCatalog {
 public:
  virtual ~CaptureBackendCatalog() = default;
  virtual std::vector<CaptureBackendInfo> list() const = 0;
};

// Abstract backend that generates ffmpeg args for a specific capture kind.
class SourceCaptureBackend {
 public:
  virtual ~SourceCaptureBackend() = default;
  virtual SourceCaptureKind kind() const = 0;
  virtual std::string id() const = 0;
  virtual SourceCapturePlan plan(const SourceCaptureRequest& request) const = 0;
};

// Factory functions for platform-specific backends
std::unique_ptr<CaptureBackendCatalog> createCaptureBackendCatalog();
std::unique_ptr<SourceCaptureBackend> createWindowCaptureBackend();
std::unique_ptr<SourceCaptureBackend> createCameraCaptureBackend();
std::unique_ptr<SourceCaptureBackend> createAppTextureCaptureBackend();

// Convenience: plan a capture using the appropriate backend for the request kind.
// This is the main entry point called by MediaEngine::buildSourceCaptureArgs().
SourceCapturePlan planSourceCapture(const SourceCaptureRequest& request);

// Describes one capturable window on the current system (for window picker UI).
struct CaptureWindowInfo {
  std::string id;           // platform window identifier (e.g. "title:Notepad")
  std::string displayName;  // human-readable window title for dropdown display
};

// Enumerates visible, titled windows available for capture on this platform.
// Returns a list suitable for populating a window-picker dropdown in the inspector.
// The first entry is always "Desktop (full screen)".
std::vector<CaptureWindowInfo> listCaptureWindows();

// Ask the window with this exact title to repaint itself, and un-minimise it if
// it is iconic. Windows.Graphics.Capture delivers a frame only when the window
// draws, so a window holding a static picture -- a slide, a score, a spreadsheet
// -- hands over nothing at all until something invalidates it. Returns true if a
// window was found and poked. No-op (false) off Windows, where the capture
// backends are pull-based and do not need it.
bool nudgeWindowRepaint(const std::string& windowTitle);

// Can this machine's ffmpeg capture a window properly? True when the WGC source
// is available, which is what makes a window cue come through at the right size
// on a scaled display. False means window cues still work, through the older
// screen grab, with the geometry caveat that goes with it. Always false off
// Windows, where the window backends are different altogether.
//
// Reported by --self-check so a packaging change that moves the bundled ffmpeg
// back below 8.0 shows up as a line that changed, rather than as window cues
// quietly going back to a picture in the corner of a black frame.
bool windowCaptureIsExact();

}  // namespace deckboy::platform
