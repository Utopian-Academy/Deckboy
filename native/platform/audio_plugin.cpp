// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// audio_plugin.cpp — Finding and loading third-party audio plugins.
//
// Two halves that are deliberately separable:
//
//   THE SCAN is filesystem work and needs no SDK. It walks the folders each
//   platform keeps plugins in and reports what is there. A build without VST3
//   support still scans, so the inspector can say "you have 50 plugins and this
//   build cannot load them" rather than showing an empty list that reads as
//   "you own none".
//
//   THE HOST needs the SDK and lives behind DECKBOY_HAS_VST3.
//
// Header: audio_plugin.hpp, which carries the licensing note on formats and the
// reasoning about the audio thread.
// ============================================================================

#include "platform/audio_plugin.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace deckboy::platform::audioplugin {
namespace {

namespace fs = std::filesystem;

// Where each platform keeps its VST3s. The user folder comes FIRST: a plugin
// installed for one user shadows a system copy of the same thing, which is what
// every other host does and what an operator expects when they install a demo
// over a licensed build.
std::vector<fs::path> vst3SearchPaths() {
  std::vector<fs::path> paths;
  auto env = [](const char* name) -> std::string {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
  };
#if defined(_WIN32)
  const std::string localAppData = env("LOCALAPPDATA");
  if (!localAppData.empty()) {
    paths.emplace_back(fs::path(localAppData) / "Programs" / "Common" / "VST3");
  }
  const std::string commonFiles = env("CommonProgramFiles");
  if (!commonFiles.empty()) {
    paths.emplace_back(fs::path(commonFiles) / "VST3");
  } else {
    paths.emplace_back("C:/Program Files/Common Files/VST3");
  }
#elif defined(__APPLE__)
  const std::string home = env("HOME");
  if (!home.empty()) {
    paths.emplace_back(fs::path(home) / "Library" / "Audio" / "Plug-Ins" / "VST3");
  }
  paths.emplace_back("/Library/Audio/Plug-Ins/VST3");
#else
  const std::string home = env("HOME");
  if (!home.empty()) {
    paths.emplace_back(fs::path(home) / ".vst3");
  }
  // The locations the Linux VST3 spec names, in its order.
  paths.emplace_back("/usr/lib/vst3");
  paths.emplace_back("/usr/local/lib/vst3");
#endif
  return paths;
}

// A .vst3 is EITHER a single module file or a bundle directory, depending on
// the platform and on how old the plugin is. Both spellings are in the wild on
// Windows -- the Neutron plugins on this machine ship plain .dll files beside
// bundle directories -- so the scan accepts a directory or a file and lets the
// loader work out which.
bool looksLikePlugin(const fs::directory_entry& entry) {
  std::error_code ec;
  const fs::path& p = entry.path();
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (ext == ".vst3") {
    return entry.is_directory(ec) || entry.is_regular_file(ec);
  }
#if defined(_WIN32)
  // A .dll INSIDE a VST3 folder is a VST3 module that kept the old extension --
  // iZotope ship every Neutron component that way, 35 of them on the machine
  // this was written on, and a scan that only accepted ".vst3" reported a
  // library a third smaller than the operator knows they have. Whether it is
  // really a VST3 is settled by asking the module for its factory, not by its
  // name, so the loader has the last word and a VST2 left in the wrong folder
  // simply fails to open.
  if (ext == ".dll") {
    return entry.is_regular_file(ec);
  }
#endif
  return false;
}

void scanFolder(const fs::path& root, std::vector<PluginDescriptor>& out, int depth) {
  std::error_code ec;
  if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
    return;
  }
  for (const auto& entry : fs::directory_iterator(root, ec)) {
    if (ec) {
      break;
    }
    if (looksLikePlugin(entry)) {
      PluginDescriptor d;
      d.format = "vst3";
      d.path = entry.path().string();
      d.name = entry.path().stem().string();
      // The vendor is the folder a plugin sits in when the installer made one
      // ("VST3/FabFilter/Pro-Q 4.vst3"), which is how every vendor ships.
      // Left empty at the top level rather than guessed.
      if (depth > 0) {
        d.vendor = root.filename().string();
      }
      // Until the module is opened, the plugin's real class id is unknown, so
      // the id is its path. openAudioPlugin accepts both spellings and the
      // descriptor is upgraded to the class id once the SDK has read it.
      d.id = "vst3:" + d.path;
      out.push_back(std::move(d));
      continue;
    }
    std::error_code dirEc;
    if (entry.is_directory(dirEc) && depth < 2) {
      // Vendors nest one level ("VST3/Arturia/..."), and a couple nest twice.
      // Deeper than that is somebody's sample library and not worth the stat
      // calls on a machine with a slow drive.
      scanFolder(entry.path(), out, depth + 1);
    }
  }
}

}  // namespace
}  // namespace deckboy::platform::audioplugin

// The SDK's headers have to be included at global scope, so the VST3 half opens
// the namespace again for itself rather than being pulled inside this one.
#if defined(DECKBOY_HAS_VST3)
#include "platform/audio_plugin_vst3.inc"
#endif

namespace deckboy::platform::audioplugin {

std::vector<std::string> audioPluginSearchPaths() {
  std::vector<std::string> out;
  for (const auto& p : vst3SearchPaths()) {
    out.push_back(p.string());
  }
  return out;
}

std::vector<PluginDescriptor> scanAudioPlugins() {
  std::vector<PluginDescriptor> found;
  for (const auto& root : vst3SearchPaths()) {
    scanFolder(root, found, 0);
  }
  // Stable order, so the picker does not reshuffle itself between runs and an
  // operator can learn where their reverb sits in the list.
  std::sort(found.begin(), found.end(), [](const PluginDescriptor& a, const PluginDescriptor& b) {
    if (a.vendor != b.vendor) {
      return a.vendor < b.vendor;
    }
    return a.name < b.name;
  });
  return found;
}

#if defined(DECKBOY_HAS_VST3)
// Defined in audio_plugin_vst3.inc.
std::unique_ptr<AudioPluginInstance> openVst3Plugin(const std::string& path,
                                                    double sampleRate,
                                                    int maxBlockFrames);
#endif

bool audioPluginsSupported() {
#if defined(DECKBOY_HAS_VST3)
  return true;
#else
  return false;
#endif
}

std::unique_ptr<AudioPluginInstance> openAudioPlugin(const std::string& id,
                                                     double sampleRate,
                                                     int maxBlockFrames) {
  (void)sampleRate;
  (void)maxBlockFrames;
#if defined(DECKBOY_HAS_VST3)
  // Ids are "<format>:<reference>". Only vst3 exists today; the split is here
  // so a second format is a branch rather than a rewrite.
  const std::string::size_type colon = id.find(':');
  if (colon == std::string::npos) {
    return nullptr;
  }
  const std::string format = id.substr(0, colon);
  const std::string reference = id.substr(colon + 1);
  if (format == "vst3") {
    return openVst3Plugin(reference, sampleRate, maxBlockFrames);
  }
  return nullptr;
#else
  (void)id;
  return nullptr;
#endif
}

}  // namespace deckboy::platform::audioplugin
