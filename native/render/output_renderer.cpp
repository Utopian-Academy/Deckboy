// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// output_renderer.cpp — OutputRenderer compilation unit.
//
// This file exists to provide a compilation unit for the OutputRenderer
// abstract interface. The interface is purely virtual with no concrete
// implementation here — the actual output rendering pipeline is
// implemented in app_render_output.ipp within the App class.
//
// Header: output_renderer.hpp
// ============================================================================

#include "render/output_renderer.hpp"

namespace deckboy::render {

// No implementation — OutputRenderer is an abstract interface.
// See app_render_output.ipp for the concrete rendering pipeline.

}  // namespace deckboy::render

