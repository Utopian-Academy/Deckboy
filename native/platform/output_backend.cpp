// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/output_backend.hpp"
#include "core/utils.hpp"

#include <unordered_map>

namespace deckboy::platform {
namespace {

using deckboy::core::utils::trim;
using deckboy::core::utils::toLower;

class DefaultOutputBackendCatalog final : public OutputBackendCatalog {
 public:
  std::vector<OutputBackendInfo> list() const override {
    std::vector<OutputBackendInfo> out;
    out.push_back({"window", "SDL Window Output", true, ""});
#if defined(_WIN32)
    out.push_back({"stream", "FFmpeg Stream Output", false, "runtime backend pending on Windows"});
#else
    out.push_back({"stream", "FFmpeg Stream Output", true, ""});
#endif
#if defined(DECKBOY_HAS_NDI_SDK)
    out.push_back({"ndi", "NDI Output", true, ""});
#else
    out.push_back({"ndi", "NDI Output", false, "NDI SDK not built"});
#endif
#if defined(DECKBOY_HAS_DECKLINK)
    out.push_back({"decklink", "DeckLink Output", true, ""});
#else
    out.push_back({"decklink", "DeckLink Output", false, "DeckLink SDK feature gate disabled"});
#endif
    return out;
  }
};

}  // namespace

std::unique_ptr<OutputBackendCatalog> createOutputBackendCatalog() {
  return std::make_unique<DefaultOutputBackendCatalog>();
}

OutputBackendRoutePlan planOutputBackendRoute(
  const OutputBackendRouteRequest& request,
  const OutputBackendCatalog& catalog) {
  OutputBackendRoutePlan plan;
  std::unordered_map<std::string, OutputBackendInfo> byId;
  for (const auto& info : catalog.list()) {
    byId[info.id] = info;
  }

  auto pushStep = [&](OutputRouteKind kind, const std::string& backendId) {
    OutputBackendRouteStep step;
    step.kind = kind;
    step.backendId = backendId;
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

  std::string outputType = toLower(trim(request.outputType));
  if (outputType == "stream") {
    pushStep(OutputRouteKind::Stream, "stream");
  } else {
    pushStep(OutputRouteKind::Window, "window");
  }

  if (request.streamEnabled && outputType != "stream") {
    pushStep(OutputRouteKind::Stream, "stream");
  }
  if (request.ndiEnabled) {
    pushStep(OutputRouteKind::Ndi, "ndi");
  }
  if (request.deckLinkEnabled) {
    pushStep(OutputRouteKind::DeckLink, "decklink");
  }

  return plan;
}

OutputBackendRoutePlan planOutputBackendRoute(const OutputBackendRouteRequest& request) {
  auto catalog = createOutputBackendCatalog();
  return planOutputBackendRoute(request, *catalog);
}

}  // namespace deckboy::platform
