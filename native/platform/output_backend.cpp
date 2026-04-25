// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// output_backend.cpp — Platform-aware catalog of output destination backends.
//
// Implements the OutputBackendCatalog interface reporting availability for
// each egress path the composited deck output can be routed to:
//
//   Window   — SDL window on a local display (always available)
//   Stream   — ffmpeg SRT/RTMP streaming (Linux/macOS; pending on Windows)
//   NDI      — NDI network output (requires DECKBOY_HAS_NDI_SDK build flag)
//   DeckLink — Blackmagic SDI/HDMI card (requires DECKBOY_HAS_DECKLINK flag)
//
// planOutputBackendRoute() converts the user's OutputBackendRouteRequest
// (from settings) into a RoutePlan with one step per requested backend,
// each annotated with platform support status. This plan drives the output
// compositor in app_render_output.ipp to blit frames to the correct sinks.
//
// Header: output_backend.hpp
// Used by: main.cpp output setup and app_render_settings.ipp output tab.
// ============================================================================

#include "platform/output_backend.hpp"
#include "core/utils.hpp"

#include <unordered_map>

namespace deckboy::platform {
namespace {

using deckboy::core::utils::trim;
using deckboy::core::utils::toLower;

// ── Catalog: reports which output backends are available on this platform. ───
// Each entry is always present in the list (for settings UI display) but
// marked supported=false with a reason if the platform can't provide it.
class DefaultOutputBackendCatalog final : public OutputBackendCatalog {
 public:
  std::vector<OutputBackendInfo> list() const override {
    std::vector<OutputBackendInfo> out;
    // SDL window output — always available (the primary output path)
    out.push_back({"window", "SDL Window Output", true, ""});
    // ffmpeg streaming (SRT/RTMP) — available on all platforms
    out.push_back({"stream", "FFmpeg Stream Output", true, ""});
    // NDI network output — requires the NDI SDK at build time
#if defined(DECKBOY_HAS_NDI_SDK)
    out.push_back({"ndi", "NDI Output", true, ""});
#else
    out.push_back({"ndi", "NDI Output", false, "NDI SDK not built"});
#endif
    // DeckLink SDI/HDMI card output — requires the DeckLink SDK feature gate
#if defined(DECKBOY_HAS_DECKLINK)
    out.push_back({"decklink", "DeckLink Output", true, ""});
#else
    out.push_back({"decklink", "DeckLink Output", false, "DeckLink SDK feature gate disabled"});
#endif
    // Spout interprocess texture sharing — Windows only, requires Spout SDK
#if defined(DECKBOY_HAS_SPOUT)
    out.push_back({"spout", "Spout Output", true, ""});
#elif defined(_WIN32)
    out.push_back({"spout", "Spout Output", false, "Spout SDK not built"});
#else
    out.push_back({"spout", "Spout Output", false, "Spout is Windows-only"});
#endif
    return out;
  }
};

}  // namespace

// ── Factory ─────────────────────────────────────────────────────────────────

std::unique_ptr<OutputBackendCatalog> createOutputBackendCatalog() {
  return std::make_unique<DefaultOutputBackendCatalog>();
}

// ── Route planning ──────────────────────────────────────────────────────────
// Converts the user's output settings into an ordered list of output steps.
// The primary output is determined by outputType ("window" or "stream"),
// then additional sinks (stream, NDI, DeckLink) are appended if enabled.

OutputBackendRoutePlan planOutputBackendRoute(
  const OutputBackendRouteRequest& request,
  const OutputBackendCatalog& catalog) {
  OutputBackendRoutePlan plan;

  // Index catalog entries by ID for O(1) lookup
  std::unordered_map<std::string, OutputBackendInfo> byId;
  for (const auto& info : catalog.list()) {
    byId[info.id] = info;
  }

  // Helper: add one step annotated with platform support status
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

  // Step 1: Primary output — determined by the outputType setting
  std::string outputType = toLower(trim(request.outputType));
  if (outputType == "stream") {
    pushStep(OutputRouteKind::Stream, "stream");
  } else {
    pushStep(OutputRouteKind::Window, "window");
  }

  // Step 2+: Additional sinks enabled alongside the primary output
  if (request.streamEnabled && outputType != "stream") {
    pushStep(OutputRouteKind::Stream, "stream");  // Stream as secondary sink
  }
  if (request.ndiEnabled) {
    pushStep(OutputRouteKind::Ndi, "ndi");
  }
  if (request.deckLinkEnabled) {
    pushStep(OutputRouteKind::DeckLink, "decklink");
  }
  if (request.spoutEnabled) {
    pushStep(OutputRouteKind::Spout, "spout");
  }

  return plan;
}

// Convenience overload: creates a catalog internally for single-shot planning.
OutputBackendRoutePlan planOutputBackendRoute(const OutputBackendRouteRequest& request) {
  auto catalog = createOutputBackendCatalog();
  return planOutputBackendRoute(request, *catalog);
}

}  // namespace deckboy::platform
