// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/show_file.hpp"
#include "core/constants.hpp"
#include "core/utils.hpp"
#include "core/cue_helpers.hpp"
#include "core/pattern_helpers.hpp"
#include "core/midi_map.hpp"
#include "core/fs.hpp"
#include "core/paths.hpp"
#include <chrono>
#include <functional>
#include <set>
#include <thread>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
namespace fs = dbfs;
using namespace deckboy::core::utils;
using deckboy::core::Paths;
// The desk retains its included copy. Both copies have internal linkage, so
// there is no ODR clash. Runtime deck keys are local to each consumer and are
// not serialized; the desk continues to normalize through its own copy.
#include "core/show_helpers.ipp"
#include "core/project_file.ipp"
}

namespace deckboy::showfile {
bool save(const std::string& path, const Project& project) {
  return saveProject(fs::path(path), project);
}
Project load(const std::string& path) {
  return loadProject(fs::path(path));
}
}
