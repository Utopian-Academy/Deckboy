// ---------------------------------------------------------------------------
// terrarium_vendor.cpp — the vendored sim's implementation, inside `namespace terra`.
//
// Yes, this includes a .cpp. That is deliberate and it is the whole trick:
// upstream's definitions must land in the SAME namespace as the declarations
// that terrarium_vendor.hpp put there, and the only way to do that without
// editing upstream is to pull the translation unit in here. Adding
// upstream/terrarium_core.cpp to CMake directly would compile it at global
// scope and every symbol would fail to link against the `terra::` declarations.
//
// Do not add upstream/terrarium_core.cpp to CMakeLists — this file is its
// build entry point. See upstream/UPSTREAM.md.
// ---------------------------------------------------------------------------
#include "terrarium_vendor.hpp"

namespace terra {
#include "upstream/terrarium_core.cpp"
}  // namespace terra
