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

struct CaptureBackendInfo {
  CaptureBackendKind kind = CaptureBackendKind::Window;
  std::string id;
  std::string displayName;
  bool supported = false;
  std::string reasonUnavailable;
};

class CaptureBackendCatalog {
 public:
  virtual ~CaptureBackendCatalog() = default;
  virtual std::vector<CaptureBackendInfo> list() const = 0;
};

std::unique_ptr<CaptureBackendCatalog> createCaptureBackendCatalog();

}  // namespace deckboy::platform
