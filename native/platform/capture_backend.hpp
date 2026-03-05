// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace deckboy::platform {

enum class CaptureBackendKind {
  Window,
  Camera,
  AppTexture
};

enum class SourceCaptureKind {
  Window,
  Camera,
  AppTexture
};

struct CaptureBackendInfo {
  CaptureBackendKind kind = CaptureBackendKind::Window;
  std::string id;
  std::string displayName;
  bool supported = false;
  std::string reasonUnavailable;
};

struct SourceCaptureRequest {
  SourceCaptureKind kind = SourceCaptureKind::Window;
  std::string sourceRef;
  int width = 1280;
  int height = 720;
  int frameRate = 30;
  bool drawMouse = true;
  std::string display;
};

struct SourceCapturePlan {
  bool supported = false;
  std::string backendId;
  std::vector<std::string> ffmpegArgs;
  std::string reasonUnavailable;
};

class CaptureBackendCatalog {
 public:
  virtual ~CaptureBackendCatalog() = default;
  virtual std::vector<CaptureBackendInfo> list() const = 0;
};

class SourceCaptureBackend {
 public:
  virtual ~SourceCaptureBackend() = default;
  virtual SourceCaptureKind kind() const = 0;
  virtual std::string id() const = 0;
  virtual SourceCapturePlan plan(const SourceCaptureRequest& request) const = 0;
};

std::unique_ptr<CaptureBackendCatalog> createCaptureBackendCatalog();
std::unique_ptr<SourceCaptureBackend> createWindowCaptureBackend();
std::unique_ptr<SourceCaptureBackend> createCameraCaptureBackend();
std::unique_ptr<SourceCaptureBackend> createAppTextureCaptureBackend();
SourceCapturePlan planSourceCapture(const SourceCaptureRequest& request);

}  // namespace deckboy::platform
