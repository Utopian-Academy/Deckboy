// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// media_probe.hpp — what kind of file is this, and what is in it.
//
// The extension tests and the ffprobe read that turn a file into a Cue. Shared
// by the desk's import and by Deckboy Mini, so both classify a file the same
// way: there is one definition of each, on purpose.
// ============================================================================

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/types.hpp"

namespace deckboy::core::media {

bool isImagePath(const std::filesystem::path& path);
bool isMidiFilePath(const std::filesystem::path& path);
bool isAudioPath(const std::filesystem::path& path);
// True for files a folder import accepts: video, stills, audio, PDF decks.
bool isAcceptableMediaPath(const std::filesystem::path& path);
std::optional<double> parseFps(const std::string& rate);
// ffprobe a media file into a Cue: kind, size, rate, length, audio. nullopt
// when the file cannot be read or has no picture size.
std::optional<Cue> probeCue(const std::filesystem::path& mediaPath);

}  // namespace deckboy::core::media
