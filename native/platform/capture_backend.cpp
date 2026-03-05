// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/capture_backend.hpp"

namespace deckboy::platform {
namespace {

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

}  // namespace deckboy::platform
