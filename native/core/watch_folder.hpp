// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// watch_folder.hpp — a list that fills itself from a folder.
//
// Shared by the desk (one per playlist, Deck::watchFolder) and Deckboy Mini
// (--watch), so a file that arrives is judged the same way by both: there is
// one definition of "new" and one of "finished copying", on purpose.
//
// The folder is listed on a worker thread every 2.5 seconds, because a share
// can take long enough to list that doing it on the main thread drops frames.
// A file is handed over only once its size and modification time have held
// still across two scans, so a large clip part-way through a copy is left
// alone until it is complete: imported half-written, it is a cue that fails
// on air. NOT recursive: a drop folder is a drop folder, and walking a whole
// media tree on a share every few seconds is a different feature.
// ============================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace deckboy::core {

class WatchFolder {
 public:
  using Accept = std::function<bool(const std::filesystem::path&)>;

  // `accept` says which files count; the default is anything the desk imports.
  explicit WatchFolder(Accept accept = {});

  // Empty stops watching. A different folder forgets what was taken from the
  // last one, so a new folder is taken whole rather than skipping anything
  // that shares a name with something taken earlier.
  void setFolder(const std::string& folder);
  const std::string& folder() const { return folder_; }
  bool watching() const { return !folder_.empty(); }

  // What the list already holds, so a show reopened on a folder that is still
  // full does not take every file a second time. needsSeed() is true from
  // setFolder() until seed() is called.
  bool needsSeed() const { return watching() && !seeded_; }
  void seed(const std::vector<std::string>& paths);

  // Call every tick with a steady clock in seconds. Starts a scan when one is
  // due, and returns the files that have held still since the previous scan
  // and were not taken before, sorted by name. The caller adds them.
  std::vector<std::string> poll(double now);

  // Counted so an operator whose files are not arriving can tell a folder
  // nobody is looking at from one that is being looked at and is empty.
  int scans() const { return scans_; }
  int lastSeen() const { return lastSeen_; }
  int taken() const { return taken_; }

  static constexpr double kScanSeconds = 2.5;

 private:
  struct Find {
    std::string path;
    std::uintmax_t size = 0;
    std::int64_t modified = 0;
  };
  // Shared with the worker. A scan outlives a WatchFolder that is destroyed or
  // pointed elsewhere while it runs; it writes into this and nobody reads it.
  struct Scan {
    std::mutex mutex;
    std::vector<Find> finds;
    std::atomic<bool> ready {false};
  };

  Accept accept_;
  std::string folder_;
  std::shared_ptr<Scan> scan_;      // the scan in flight, if any
  double nextScanAt_ = 0.0;
  bool seeded_ = false;
  std::set<std::string> seen_;
  // What each path measured on the PREVIOUS scan.
  std::map<std::string, std::pair<std::uintmax_t, std::int64_t>> pending_;
  int scans_ = 0;
  int lastSeen_ = 0;
  int taken_ = 0;
};

}  // namespace deckboy::core
