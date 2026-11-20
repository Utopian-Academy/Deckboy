// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// direction_inproc.hpp -- a REVERSED or BOUNCED copy of any file, made IN THIS
// PROCESS with libav: no ffmpeg program. Mini and Pocket ship without one,
// and the desk uses this when its own is missing.
//
// A compressed file decodes only forwards, so the range is decoded once,
// forwards, and every frame is encoded on its own as a JPEG (MJPEG, libav's
// own encoder, in every build). Frames that stand alone can be written in
// any order: the copy is those frames last-first (or there and back for
// BOUNCE, without the two turnaround frames), and the sound reversed
// alongside as PCM. Only the JPEGs are held, in a file beside the copy, so a
// long 4K clip never sits in memory as pictures.
//
// The copy is a .mov the engine plays like any other (Cue::directionPath).
// A phone's rotation (its display matrix) is carried across.
// ============================================================================

#pragma once

#include <atomic>
#include <string>

#include "engine/direction_remux.hpp"   // RemuxResult

namespace deckboy::engine {

struct DirectionCopy {
  double seconds = 0.0;   // the copy's length
  int width = 0, height = 0;
  bool sound = false;
};

#if defined(DECKBOY_INPROC_DECODE)
// in/out: the range in seconds from the file's start. maxWidth caps the
// picture (0 = the file's own size), so a phone does not make 4K JPEGs.
RemuxResult bakeDirectionInProcess(const std::string& source, const std::string& dest, double in, double out,
                                   bool bounce, int maxWidth, std::atomic<double>* progress,
                                   std::atomic<bool>* cancel, std::string* why, DirectionCopy* made);
#else
inline RemuxResult bakeDirectionInProcess(const std::string&, const std::string&, double, double, bool, int,
                                          std::atomic<double>*, std::atomic<bool>*, std::string* why,
                                          DirectionCopy*) {
  if (why) *why = "this build has no in-process libav";
  return RemuxResult::Failed;
}
#endif

}  // namespace deckboy::engine
