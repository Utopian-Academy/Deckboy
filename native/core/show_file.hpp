// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/types.hpp"
#include <string>

namespace deckboy::showfile {
// Same contract as the desk: save writes the supplied project; load applies
// legacy migrations and normalization (and supplies defaults for missing files).
// Call serially on the consumer's main thread, as the desk does.
bool save(const std::string& path, const Project& project);
Project load(const std::string& path);
}
