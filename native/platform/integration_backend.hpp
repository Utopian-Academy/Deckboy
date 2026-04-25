// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// integration_backend.hpp — External protocol integration catalog.
//
// Manages the available integration backends for live production protocols:
//   - AtemTrigger: Blackmagic ATEM switcher trigger/tally via network
//   - NdiTrigger:  NDI source discovery and metadata receive
//   - NmcSync:     Network master clock sync
//   - MtcIngest:   MIDI Time Code ingest (via MIDI port)
//   - LtcIngest:   Linear Time Code ingest (via audio input)
//   - DmxArtNet:   Art-Net DMX lighting control protocol
//
// Architecture:
//   IntegrationBackendCatalog: queries which backends are available on this platform
//   IntegrationBackendRouteRequest: specifies which backends the user has enabled
//   planIntegrationBackendRoute(): generates a RoutePlan with step-by-step
//     enable/disable instructions, checking platform support for each
//
// This follows the same catalog/plan pattern as capture_backend.hpp and
// output_backend.hpp — separating "what's available" from "what's enabled".
//
// Implementation: integration_backend.cpp
// Used by: main.cpp (starts/stops integration threads based on the route plan).
// ============================================================================

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace deckboy::platform {

// All supported integration protocol types.
enum class IntegrationBackendKind {
  AtemTrigger,   // Blackmagic ATEM switcher tally/trigger
  NdiTrigger,    // NDI source discovery + metadata
  NmcSync,       // Network master clock synchronization
  MtcIngest,     // MIDI Time Code ingest
  LtcIngest,     // Linear Time Code (audio) ingest
  DmxArtNet      // Art-Net DMX lighting control
};

// Describes one available integration backend on this platform.
struct IntegrationBackendInfo {
  IntegrationBackendKind kind = IntegrationBackendKind::AtemTrigger;
  std::string id;                  // internal identifier
  std::string displayName;        // human-readable label for settings UI
  bool supported = false;         // true if the backend can be used
  std::string reasonUnavailable;  // diagnostic message if not supported
};

// Abstract catalog: queries which integration backends exist on this platform.
class IntegrationBackendCatalog {
 public:
  virtual ~IntegrationBackendCatalog() = default;
  virtual std::vector<IntegrationBackendInfo> list() const = 0;
};

// User's desired integration state: which backends are enabled in settings.
struct IntegrationBackendRouteRequest {
  bool atemTriggerEnabled = false;
  bool ndiTriggerEnabled = false;
  bool nmcSyncEnabled = false;
  bool mtcIngestEnabled = false;
  bool ltcIngestEnabled = false;
  bool dmxArtNetEnabled = false;
};

// One step in the route plan: instructions for starting/stopping a backend.
struct IntegrationBackendRouteStep {
  IntegrationBackendKind kind = IntegrationBackendKind::AtemTrigger;
  std::string backendId;
  bool enabled = false;           // user wants it on
  bool supported = false;         // platform can provide it
  std::string reasonUnavailable;
};

// The complete plan: one step per backend kind.
struct IntegrationBackendRoutePlan {
  std::vector<IntegrationBackendRouteStep> steps;
};

// Factory: create the platform-specific catalog.
std::unique_ptr<IntegrationBackendCatalog> createIntegrationBackendCatalog();
// Plan: cross-reference user requests with platform support.
IntegrationBackendRoutePlan planIntegrationBackendRoute(
  const IntegrationBackendRouteRequest& request,
  const IntegrationBackendCatalog& catalog);
// Convenience: creates a catalog internally.
IntegrationBackendRoutePlan planIntegrationBackendRoute(const IntegrationBackendRouteRequest& request);

}  // namespace deckboy::platform
