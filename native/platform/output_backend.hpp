// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// output_backend.hpp — Output destination catalog (window, stream, NDI, DeckLink).
//
// Manages the available output backends for routing the composited deck output:
//   - Window:   SDL output window (local monitor / projector)
//   - Stream:   ffmpeg-based streaming (SRT, RTMP for remote viewers)
//   - Ndi:      NDI network output (requires NDI SDK runtime)
//   - DeckLink: Blackmagic DeckLink SDI/HDMI (professional video I/O cards)
//
// Follows the same catalog/plan architecture as integration_backend.hpp:
//   OutputBackendCatalog: queries available output backends
//   OutputBackendRouteRequest: user's desired output configuration
//   planOutputBackendRoute(): generates a plan with supported/unsupported steps
//
// Implementation: output_backend.cpp
// Used by: main.cpp output setup and app_render_settings.ipp output tab.
// ============================================================================

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace deckboy::platform {

// The kinds of output destinations.
enum class OutputRouteKind {
  Window,    // SDL window on a local display
  Stream,    // ffmpeg streaming (SRT/RTMP/etc.)
  Ndi,       // NDI network output
  DeckLink,  // Blackmagic DeckLink SDI/HDMI card
  Spout      // Windows Spout interprocess texture sharing
};

// Describes one available output backend on this platform.
struct OutputBackendInfo {
  std::string id;                  // internal identifier (e.g. "ndi", "decklink")
  std::string displayName;        // human-readable label for settings UI
  bool supported = false;         // true if the backend can be used
  std::string reasonUnavailable;  // diagnostic message if not supported
};

// Abstract catalog: queries which output backends exist on this platform.
class OutputBackendCatalog {
 public:
  virtual ~OutputBackendCatalog() = default;
  virtual std::vector<OutputBackendInfo> list() const = 0;
};

// User's desired output configuration from settings.
struct OutputBackendRouteRequest {
  std::string outputType = "window"; // "window" or "stream"
  bool streamEnabled = false;         // enable ffmpeg streaming output
  bool ndiEnabled = false;            // enable NDI network output
  bool deckLinkEnabled = false;       // enable DeckLink SDI/HDMI output
  bool spoutEnabled = false;          // enable Spout interprocess texture sharing
};

// One step in the output route plan.
struct OutputBackendRouteStep {
  OutputRouteKind kind = OutputRouteKind::Window;
  std::string backendId;
  bool supported = false;
  std::string reasonUnavailable;
};

// The complete output route plan.
struct OutputBackendRoutePlan {
  std::vector<OutputBackendRouteStep> steps;
};

// Factory: create the platform-specific catalog.
std::unique_ptr<OutputBackendCatalog> createOutputBackendCatalog();
// Plan: cross-reference user requests with platform support.
OutputBackendRoutePlan planOutputBackendRoute(
  const OutputBackendRouteRequest& request,
  const OutputBackendCatalog& catalog);
// Convenience: creates a catalog internally.
OutputBackendRoutePlan planOutputBackendRoute(const OutputBackendRouteRequest& request);

}  // namespace deckboy::platform
