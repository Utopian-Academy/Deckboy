// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/integration_backend.hpp"

#include <unordered_map>

namespace deckboy::platform {
namespace {

class DefaultIntegrationBackendCatalog final : public IntegrationBackendCatalog {
 public:
  std::vector<IntegrationBackendInfo> list() const override {
    std::vector<IntegrationBackendInfo> out;
    out.push_back({
      IntegrationBackendKind::AtemTrigger,
      "atem",
      "ATEM Trigger Bridge",
 #if defined(_WIN32)
      false,
      "ATEM UDP bridge backend currently Linux/macOS only"
 #else
      true,
      ""
 #endif
    });
#if defined(_WIN32)
    out.push_back({
      IntegrationBackendKind::NdiTrigger,
      "ndi-trigger",
      "NDI Metadata Trigger Bridge",
      false,
      "NDI metadata trigger runtime not yet enabled on Windows"
    });
#else
    out.push_back({
      IntegrationBackendKind::NdiTrigger,
      "ndi-trigger",
      "NDI Metadata Trigger Bridge",
      true,
      ""
    });
#endif
    out.push_back({
      IntegrationBackendKind::NmcSync,
      "nmc",
      "NMC Transport Sync",
#if defined(_WIN32)
      false,
      "NMC sync backend currently Linux/macOS only"
#else
      true,
      ""
#endif
    });
#if defined(DECKBOY_HAS_ALSA)
    out.push_back({
      IntegrationBackendKind::MtcIngest,
      "mtc",
      "MTC Ingest",
      true,
      ""
    });
#else
    out.push_back({
      IntegrationBackendKind::MtcIngest,
      "mtc",
      "MTC Ingest",
      false,
      "MIDI backend not available on this build"
    });
#endif
#if defined(_WIN32)
    out.push_back({
      IntegrationBackendKind::LtcIngest,
      "ltc",
      "LTC Ingest",
      false,
      "LTC ingest runtime currently Linux/macOS only"
    });
#else
    out.push_back({
      IntegrationBackendKind::LtcIngest,
      "ltc",
      "LTC Ingest",
      true,
      ""
    });
#endif
    out.push_back({
      IntegrationBackendKind::DmxArtNet,
      "dmx-artnet",
      "DMX / Art-Net Trigger Bridge",
#if defined(_WIN32)
      false,
      "DMX/Art-Net UDP bridge currently Linux/macOS only"
#else
      true,
      ""
#endif
    });
    return out;
  }
};

}  // namespace

std::unique_ptr<IntegrationBackendCatalog> createIntegrationBackendCatalog() {
  return std::make_unique<DefaultIntegrationBackendCatalog>();
}

IntegrationBackendRoutePlan planIntegrationBackendRoute(
  const IntegrationBackendRouteRequest& request,
  const IntegrationBackendCatalog& catalog) {
  IntegrationBackendRoutePlan plan;
  std::unordered_map<std::string, IntegrationBackendInfo> byId;
  for (const auto& info : catalog.list()) {
    byId[info.id] = info;
  }

  auto pushStep = [&](IntegrationBackendKind kind, const std::string& backendId, bool enabled) {
    IntegrationBackendRouteStep step;
    step.kind = kind;
    step.backendId = backendId;
    step.enabled = enabled;
    auto it = byId.find(backendId);
    if (it != byId.end()) {
      step.supported = it->second.supported;
      step.reasonUnavailable = it->second.reasonUnavailable;
    } else {
      step.supported = false;
      step.reasonUnavailable = "backend id not registered";
    }
    plan.steps.push_back(std::move(step));
  };

  pushStep(IntegrationBackendKind::AtemTrigger, "atem", request.atemTriggerEnabled);
  pushStep(IntegrationBackendKind::NdiTrigger, "ndi-trigger", request.ndiTriggerEnabled);
  pushStep(IntegrationBackendKind::NmcSync, "nmc", request.nmcSyncEnabled);
  pushStep(IntegrationBackendKind::MtcIngest, "mtc", request.mtcIngestEnabled);
  pushStep(IntegrationBackendKind::LtcIngest, "ltc", request.ltcIngestEnabled);
  pushStep(IntegrationBackendKind::DmxArtNet, "dmx-artnet", request.dmxArtNetEnabled);
  return plan;
}

IntegrationBackendRoutePlan planIntegrationBackendRoute(const IntegrationBackendRouteRequest& request) {
  auto catalog = createIntegrationBackendCatalog();
  return planIntegrationBackendRoute(request, *catalog);
}

}  // namespace deckboy::platform
