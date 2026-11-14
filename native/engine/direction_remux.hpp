// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// direction_remux.hpp -- a LOSSLESS reversed (or bounced) copy of the picture,
// for files whose every frame is a whole picture: ProRes, HAP, DNxHR, MJPEG,
// PNG and the like (libav's AV_CODEC_PROP_INTRA_ONLY, and every packet a
// keyframe). Any such frame decodes on its own, so the copy is the range's
// packets written in reverse order: no decode, no encode, the codec, quality
// and alpha kept, and seconds instead of a full encode.
//
// Picture only. The caller reverses the sound and joins the two (see
// core/direction_bake.hpp). Anything else answers NotIntra and the caller
// makes the encoded copy instead. A build without in-process decode has no
// libav, and always answers NotIntra.
// ============================================================================

#pragma once

#include <atomic>
#include <string>

namespace deckboy::engine {

enum class RemuxResult { Done, NotIntra, Failed, Cancelled };

#if defined(DECKBOY_INPROC_DECODE)
// in/out: the range in seconds from the file's start. bounce: forwards, then
// backwards without the two turnaround frames. dest is written as .mov.
RemuxResult reverseIntraVideo(const std::string& source, const std::string& dest, double in, double out,
                              bool bounce, std::atomic<double>* progress, std::atomic<bool>* cancel,
                              std::string* why);
#else
inline RemuxResult reverseIntraVideo(const std::string&, const std::string&, double, double, bool,
                                     std::atomic<double>*, std::atomic<bool>*, std::string* why) {
  if (why) *why = "this build has no in-process libav";
  return RemuxResult::NotIntra;
}
#endif

}  // namespace deckboy::engine
