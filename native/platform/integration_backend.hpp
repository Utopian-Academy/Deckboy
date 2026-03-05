// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace deckboy::platform {

enum class IntegrationBackendKind {
  AtemTrigger,
  NdiTrigger,
  NmcSync,
  MtcIngest,
  LtcIngest,
  DmxArtNet
};

struct IntegrationBackendInfo {
  IntegrationBackendKind kind = IntegrationBackendKind::AtemTrigger;
  std::string id;
  std::string displayName;
  bool supported = false;
  std::string reasonUnavailable;
};

class IntegrationBackendCatalog {
 public:
  virtual ~IntegrationBackendCatalog() = default;
  virtual std::vector<IntegrationBackendInfo> list() const = 0;
};

struct IntegrationBackendRouteRequest {
  bool atemTriggerEnabled = false;
  bool ndiTriggerEnabled = false;
  bool nmcSyncEnabled = false;
  bool mtcIngestEnabled = false;
  bool ltcIngestEnabled = false;
  bool dmxArtNetEnabled = false;
};

struct IntegrationBackendRouteStep {
  IntegrationBackendKind kind = IntegrationBackendKind::AtemTrigger;
  std::string backendId;
  bool enabled = false;
  bool supported = false;
  std::string reasonUnavailable;
};

struct IntegrationBackendRoutePlan {
  std::vector<IntegrationBackendRouteStep> steps;
};

std::unique_ptr<IntegrationBackendCatalog> createIntegrationBackendCatalog();
IntegrationBackendRoutePlan planIntegrationBackendRoute(
  const IntegrationBackendRouteRequest& request,
  const IntegrationBackendCatalog& catalog);
IntegrationBackendRoutePlan planIntegrationBackendRoute(const IntegrationBackendRouteRequest& request);

}  // namespace deckboy::platform
