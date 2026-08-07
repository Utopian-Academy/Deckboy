// ---------------------------------------------------------------------------
// terrarium_vendor.hpp — the vendored Terrarium sim, wrapped in `namespace terra`.
//
// Upstream declares everything at global scope: `World`, `Rng`, `step`,
// `clamp01`, `lerp`, `nightish`… Deckboy compiles almost the entire app as one
// enormous translation unit, so importing names that generic into the global
// namespace is asking for a collision. Deckboy's call sites have always said
// `terra::`, so the namespace is supplied HERE rather than by editing upstream —
// that keeps native/extras/upstream/* byte-identical, which is what makes the
// next re-sync a copy instead of a merge. (The previous vendored copy was an
// edited fork with no provenance, and sat five weeks stale.)
//
// The technique only works if every standard header upstream needs is included
// FIRST, outside the namespace. Otherwise `#include <vector>` would drag std
// into `terra` on the first inclusion. They are listed exhaustively below; if
// upstream adds an include, add it here too or the build will fail loudly (a
// std type will resolve to terra::std::…), which is the failure mode we want.
// ---------------------------------------------------------------------------
#pragma once

// Everything terrarium_core.hpp includes …
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
// … plus what terrarium_core.cpp adds (the .cpp is compiled through this same
// wrapper by terrarium_vendor.cpp).
#include <ctime>
// … plus what terrarium_pixelview.hpp uses.
#include <cmath>

namespace terra {
#include "upstream/terrarium_core.hpp"
#include "upstream/terrarium_pixelview.hpp"
}  // namespace terra
