// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// integration_backend.cpp — Platform-aware catalog of integration backends.
//
// Implements the IntegrationBackendCatalog interface with platform-specific
// availability for each live production protocol:
//
//   ATEM Trigger    — UDP bridge to Blackmagic ATEM switchers (cross-platform)
//   NDI Trigger     — NDI source discovery + metadata receive (cross-platform)
//   NMC Sync        — Network master clock synchronization (cross-platform)
//   MTC Ingest      — MIDI Time Code via ALSA sequencer (Linux + ALSA only)
//   LTC Ingest      — Linear Time Code via audio input (cross-platform)
//   DMX / Art-Net   — Art-Net UDP lighting control protocol (cross-platform)
//
// The catalog uses compile-time platform guards (#ifdef _WIN32, DECKBOY_HAS_ALSA)
// to report each backend as supported or unavailable with a diagnostic reason.
//
// planIntegrationBackendRoute() cross-references the user's enabled flags
// (from IntegrationBackendRouteRequest) against the catalog, producing a
// step-by-step RoutePlan that main.cpp uses to start/stop integration threads.
//
// Header: integration_backend.hpp
// Used by: main.cpp (integration thread lifecycle based on route plan).
// ============================================================================

#include "platform/integration_backend.hpp"

#include <unordered_map>

namespace deckboy::platform {
namespace {

// ── Catalog: reports availability of each integration protocol on this platform.
// Each entry uses compile-time guards to check platform/library support.
// Backends that aren't available get a diagnostic reasonUnavailable string
// that surfaces in the settings UI to explain why a toggle is greyed out.
class DefaultIntegrationBackendCatalog final : public IntegrationBackendCatalog {
 public:
  std::vector<IntegrationBackendInfo> list() const override {
    std::vector<IntegrationBackendInfo> out;

    // ATEM switcher tally/trigger — UDP network protocol (cross-platform)
    out.push_back({
      IntegrationBackendKind::AtemTrigger,
      "atem",
      "ATEM Trigger Bridge",
      true,
      ""
    });
    // NDI source discovery + metadata receive (cross-platform)
    out.push_back({
      IntegrationBackendKind::NdiTrigger,
      "ndi-trigger",
      "NDI Metadata Trigger Bridge",
      true,
      ""
    });

    // Network master clock synchronization (cross-platform)
    out.push_back({
      IntegrationBackendKind::NmcSync,
      "nmc",
      "NMC Transport Sync",
      true,
      ""
    });
    // MIDI Time Code ingest — requires ALSA sequencer (Linux + ALSA only)
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

    // Linear Time Code ingest via audio input (cross-platform, libltc loaded dynamically)
    out.push_back({
      IntegrationBackendKind::LtcIngest,
      "ltc",
      "LTC Ingest",
      true,
      ""
    });

    // Art-Net DMX lighting control — UDP broadcast (cross-platform)
    out.push_back({
      IntegrationBackendKind::DmxArtNet,
      "dmx-artnet",
      "DMX / Art-Net Trigger Bridge",
      true,
      ""
    });
    return out;
  }
};

}  // namespace

// ── Factory ─────────────────────────────────────────────────────────────────

std::unique_ptr<IntegrationBackendCatalog> createIntegrationBackendCatalog() {
  return std::make_unique<DefaultIntegrationBackendCatalog>();
}

// ── Route planning ──────────────────────────────────────────────────────────
// Cross-references the user's enabled flags against the catalog to produce
// a step-by-step plan. Each step indicates whether the backend is both
// enabled (user wants it) and supported (platform can provide it).
// main.cpp uses this plan to start/stop integration threads.

IntegrationBackendRoutePlan planIntegrationBackendRoute(
  const IntegrationBackendRouteRequest& request,
  const IntegrationBackendCatalog& catalog) {
  IntegrationBackendRoutePlan plan;

  // Index catalog entries by ID for O(1) lookup during step generation
  std::unordered_map<std::string, IntegrationBackendInfo> byId;
  for (const auto& info : catalog.list()) {
    byId[info.id] = info;
  }

  // Helper: create a route step for one backend kind, annotated with
  // platform support status from the catalog.
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

  // Generate one step per integration kind, preserving a fixed order
  pushStep(IntegrationBackendKind::AtemTrigger, "atem", request.atemTriggerEnabled);
  pushStep(IntegrationBackendKind::NdiTrigger, "ndi-trigger", request.ndiTriggerEnabled);
  pushStep(IntegrationBackendKind::NmcSync, "nmc", request.nmcSyncEnabled);
  pushStep(IntegrationBackendKind::MtcIngest, "mtc", request.mtcIngestEnabled);
  pushStep(IntegrationBackendKind::LtcIngest, "ltc", request.ltcIngestEnabled);
  pushStep(IntegrationBackendKind::DmxArtNet, "dmx-artnet", request.dmxArtNetEnabled);
  return plan;
}

// Convenience overload: creates a catalog internally for single-shot planning.
IntegrationBackendRoutePlan planIntegrationBackendRoute(const IntegrationBackendRouteRequest& request) {
  auto catalog = createIntegrationBackendCatalog();
  return planIntegrationBackendRoute(request, *catalog);
}

}  // namespace deckboy::platform
