// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/capture_backend.hpp"

#include <algorithm>
#include <cctype>

namespace deckboy::platform {
namespace {

std::string trim(const std::string& value) {
  size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string defaultDisplay(const std::string& token) {
  std::string display = trim(token);
  if (display.empty()) {
    return ":0.0";
  }
  return display;
}

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

    std::string inputSpec = display + "+0,0";
    bool useWindowId = false;
    std::string windowId;
    if (sourceRefLower.rfind("x11:", 0) == 0 && sourceRef.size() > 4) {
      inputSpec = trim(sourceRef.substr(4));
      if (inputSpec.empty()) {
        inputSpec = display + "+0,0";
      }
    } else if (sourceRefLower.rfind("id:", 0) == 0 && sourceRef.size() > 3) {
      useWindowId = true;
      windowId = trim(sourceRef.substr(3));
      inputSpec = display;
    } else if (sourceRefLower.rfind("window_id:", 0) == 0 && sourceRef.size() > 10) {
      useWindowId = true;
      windowId = trim(sourceRef.substr(10));
      inputSpec = display;
    } else if (!sourceRef.empty() && sourceRef[0] == ':') {
      inputSpec = sourceRef;
      if (inputSpec.find('+') == std::string::npos) {
        inputSpec += "+0,0";
      }
    } else if (!sourceRef.empty() && sourceRef[0] == '+') {
      inputSpec = display + sourceRef;
    } else if (sourceRefLower == "active-window" || sourceRefLower == "default-window"
               || sourceRefLower == "screen" || sourceRefLower == "desktop"
               || sourceRefLower == "default-bus" || sourceRefLower == "default-source"
               || sourceRefLower.empty()) {
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
      plan.ffmpegArgs.push_back("-window_id");
      plan.ffmpegArgs.push_back(windowId);
      plan.ffmpegArgs.push_back("-i");
      plan.ffmpegArgs.push_back(inputSpec);
    } else {
      plan.ffmpegArgs.push_back("-video_size");
      plan.ffmpegArgs.push_back(std::to_string(w) + "x" + std::to_string(h));
      plan.ffmpegArgs.push_back("-i");
      plan.ffmpegArgs.push_back(inputSpec);
    }
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

    auto cameraDeviceForRef = [&](const std::string& refLower, const std::string& rawRef) -> std::string {
      if (refLower.empty() || refLower == "default-camera" || refLower == "default") {
        return "/dev/video0";
      }
      if (refLower.rfind("v4l2:", 0) == 0 && rawRef.size() > 5) {
        return trim(rawRef.substr(5));
      }
      if (refLower.rfind("/dev/video", 0) == 0) {
        return rawRef;
      }
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
      "-thread_queue_size", "64",
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
    out.push_back({CaptureBackendKind::Window, "dxgi", "Window Capture (DXGI)", false, "backend scaffold only"});
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

std::unique_ptr<CaptureBackendCatalog> createCaptureBackendCatalog() {
  return std::make_unique<DefaultCaptureBackendCatalog>();
}

std::unique_ptr<SourceCaptureBackend> createWindowCaptureBackend() {
  return std::make_unique<LinuxWindowCaptureBackend>();
}

std::unique_ptr<SourceCaptureBackend> createCameraCaptureBackend() {
  return std::make_unique<LinuxCameraCaptureBackend>();
}

std::unique_ptr<SourceCaptureBackend> createAppTextureCaptureBackend() {
  return std::make_unique<LinuxAppTextureCaptureBackend>();
}

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

}  // namespace deckboy::platform
