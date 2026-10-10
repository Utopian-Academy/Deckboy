// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// watch_folder.cpp -- see watch_folder.hpp.

#include "core/watch_folder.hpp"

#include <algorithm>
#include <system_error>
#include <thread>
#include <utility>

#include "core/media_probe.hpp"

namespace deckboy::core {

namespace fs = std::filesystem;

WatchFolder::WatchFolder(Accept accept)
  : accept_(accept ? std::move(accept)
                   : Accept([](const fs::path& p) { return media::isAcceptableMediaPath(p); })) {}

void WatchFolder::setFolder(const std::string& folder) {
  if (folder == folder_) return;
  folder_ = folder;
  // A scan of the old folder may still be running; letting go of it means
  // its result is never read, so nothing from there arrives here.
  scan_.reset();
  nextScanAt_ = 0.0;
  seeded_ = false;
  seen_.clear();
  pending_.clear();
}

void WatchFolder::seed(const std::vector<std::string>& paths) {
  for (const std::string& path : paths) {
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    seen_.insert(ec ? path : absolute.string());
  }
  seeded_ = true;
}

std::vector<std::string> WatchFolder::poll(double now) {
  std::vector<std::string> take;
  if (!watching()) return take;

  // Collect the last scan first, so the next one starts from what it found.
  if (scan_ && scan_->ready.load()) {
    std::vector<Find> finds;
    {
      std::lock_guard<std::mutex> lock(scan_->mutex);
      finds.swap(scan_->finds);
    }
    scan_.reset();
    ++scans_;
    lastSeen_ = static_cast<int>(finds.size());
    for (const Find& find : finds) {
      if (seen_.count(find.path)) continue;
      // HOLD STILL FIRST. Size and time must match what the previous scan
      // saw: a copy that stalls for a moment keeps its size but not, once it
      // resumes, its time.
      const auto measured = std::make_pair(find.size, find.modified);
      auto pending = pending_.find(find.path);
      if (pending == pending_.end() || pending->second != measured) {
        pending_[find.path] = measured;
        continue;
      }
      pending_.erase(pending);
      seen_.insert(find.path);
      take.push_back(find.path);
    }
    // Forget anything that has gone, or a file deleted mid-copy and copied
    // again at the same size would be taken on its first sighting.
    for (auto it = pending_.begin(); it != pending_.end();) {
      const bool present = std::any_of(finds.begin(), finds.end(),
                                       [&](const Find& f) { return f.path == it->first; });
      it = present ? std::next(it) : pending_.erase(it);
    }
    std::sort(take.begin(), take.end());
    taken_ += static_cast<int>(take.size());
  }

  if (scan_ || now < nextScanAt_) return take;
  nextScanAt_ = now + kScanSeconds;

  auto scan = std::make_shared<Scan>();
  scan_ = scan;
  std::thread([scan, folder = folder_, accept = accept_]() {
    std::vector<Find> finds;
    std::error_code ec;
    if (fs::is_directory(folder, ec)) {
      for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc) || !accept(it->path())) continue;
        const std::uintmax_t size = fs::file_size(it->path(), fileEc);
        if (fileEc) continue;
        const auto modified = fs::last_write_time(it->path(), fileEc);
        if (fileEc) continue;
        finds.push_back({it->path().string(), size,
                         static_cast<std::int64_t>(modified.time_since_epoch().count())});
      }
    }
    {
      std::lock_guard<std::mutex> lock(scan->mutex);
      scan->finds = std::move(finds);
    }
    scan->ready.store(true);
  }).detach();
  return take;
}

}  // namespace deckboy::core
