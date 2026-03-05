// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace deckboy::platform {

enum class OutputRouteKind {
  Window,
  Stream,
  Ndi,
  DeckLink
};

struct OutputBackendInfo {
  std::string id;
  std::string displayName;
  bool supported = false;
  std::string reasonUnavailable;
};

class OutputBackendCatalog {
 public:
  virtual ~OutputBackendCatalog() = default;
  virtual std::vector<OutputBackendInfo> list() const = 0;
};

struct OutputBackendRouteRequest {
  std::string outputType = "window"; // window | stream
  bool streamEnabled = false;
  bool ndiEnabled = false;
  bool deckLinkEnabled = false;
};

struct OutputBackendRouteStep {
  OutputRouteKind kind = OutputRouteKind::Window;
  std::string backendId;
  bool supported = false;
  std::string reasonUnavailable;
};

struct OutputBackendRoutePlan {
  std::vector<OutputBackendRouteStep> steps;
};

std::unique_ptr<OutputBackendCatalog> createOutputBackendCatalog();
OutputBackendRoutePlan planOutputBackendRoute(
  const OutputBackendRouteRequest& request,
  const OutputBackendCatalog& catalog);
OutputBackendRoutePlan planOutputBackendRoute(const OutputBackendRouteRequest& request);

}  // namespace deckboy::platform
