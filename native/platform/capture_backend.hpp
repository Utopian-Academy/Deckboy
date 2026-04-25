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

}  // namespace deckboy::platform
