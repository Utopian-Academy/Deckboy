// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// direction_bake.hpp -- the ffmpeg steps that make a cue's REVERSED or BOUNCED
// copy (Cue::playDirection). No I/O here: the encoder queue runs the steps.
//
// A compressed file decodes only forwards, which is why the copy exists. And
// ffmpeg's reverse filter holds every frame it reverses in memory, which is
// why the range goes through it in short PIECES: each piece is reversed on its
// own, the pieces are written last-first, and one stream copy joins them --
// a four-minute 4K clip never needs more than a few seconds of frames held.
//
// BOUNCE is the range forwards, then backwards without its two end frames:
// at each turnaround the picture would otherwise show the same frame twice.
//
// Every piece is encoded with identical settings and PCM sound, so the join
// is a plain stream copy with no gap at the seams (AAC's priming silence
// would click at every one). Alpha survives as ProRes 4444; everything else
// is H.264 at a quality that does not show a second generation.
// ============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace deckboy::core {

struct DirectionBakeInput {
  std::string source;      // the cue's file
  std::string dest;        // the copy to write (.mov)
  std::string workDir;     // where the pieces go; emptied by the caller after
  bool bounce = false;     // false: reverse
  double in = 0.0;         // the cue's trimmed range, seconds
  double out = 0.0;
  double fps = 30.0;
  bool alpha = false;
  bool audio = true;
  double pieceSeconds = 4.0;
};

struct DirectionBakePlan {
  // Encodes, in order; the last step is the join.
  std::vector<std::vector<std::string>> steps;
  // Seconds of source each step covers (progress), the join counted as 0.
  std::vector<double> stepSeconds;
  // The join's list, to be written to listFile before the last step runs.
  std::string listFile;
  std::string listText;
  // Every piece, for the caller to delete afterwards.
  std::vector<std::string> pieces;
  // The copy's length.
  double seconds = 0.0;
  // Why there is no plan (an empty or too-short range), else empty.
  std::string problem;
};

inline std::string directionBakeNumber(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6f", v);
  return buf;
}

inline DirectionBakePlan planDirectionBake(const DirectionBakeInput& in) {
  DirectionBakePlan plan;
  const double fps = in.fps > 1.0 && in.fps < 1000.0 ? in.fps : 30.0;
  const double frame = 1.0 / fps;
  const double length = in.out - in.in;
  if (!(length >= frame * 3.0)) {
    plan.problem = "the cue is shorter than three frames";
    return plan;
  }
  auto codec = [&](std::vector<std::string>& a) {
    if (in.alpha) {
      a.insert(a.end(), {"-c:v", "prores_ks", "-profile:v", "4444", "-pix_fmt", "yuva444p10le"});
    } else {
      a.insert(a.end(), {"-c:v", "libx264", "-preset", "medium", "-crf", "14", "-pix_fmt", "yuv420p"});
    }
    // One frame rate for every piece, so the join's timestamps run on.
    a.insert(a.end(), {"-r", directionBakeNumber(fps)});
    if (in.audio) {
      a.insert(a.end(), {"-c:a", "pcm_s16le", "-ar", "48000", "-ac", "2"});
    } else {
      a.push_back("-an");
    }
  };
  auto piecePath = [&](std::size_t n) {
    char name[32];
    std::snprintf(name, sizeof(name), "piece%04zu.mov", n);
    return in.workDir + "/" + name;
  };

  std::vector<std::string> joinOrder;
  // Bounce: the range forwards first.
  if (in.bounce) {
    const std::string fwd = piecePath(plan.pieces.size());
    std::vector<std::string> a {"ffmpeg", "-y", "-ss", directionBakeNumber(in.in), "-t", directionBakeNumber(length),
                                "-i", in.source};
    codec(a);
    a.push_back(fwd);
    plan.steps.push_back(a);
    plan.stepSeconds.push_back(length);
    plan.pieces.push_back(fwd);
    joinOrder.push_back(fwd);
  }
  // Backwards: the range (bounce: less its two end frames), in pieces, the
  // last piece first so the join reads straight through.
  const double from = in.bounce ? in.in + frame : in.in;
  const double to = in.bounce ? in.out - frame : in.out;
  const double piece = std::max(frame * 2.0, in.pieceSeconds);
  std::vector<double> starts;
  for (double t = from; t < to - frame * 0.5; t += piece) starts.push_back(t);
  for (std::size_t k = starts.size(); k-- > 0;) {
    const double s = starts[k];
    const double d = std::min(piece, to - s);
    const std::string p = piecePath(plan.pieces.size());
    // -ss and -t BEFORE -i: they limit what is READ. After -i, -t only cuts
    // the output -- and reverse emits nothing until its input ends, so it read
    // to the end of the file and the cut kept the wrong end of it.
    std::vector<std::string> a {"ffmpeg", "-y", "-ss", directionBakeNumber(s), "-t", directionBakeNumber(d),
                                "-i", in.source, "-vf", "reverse"};
    if (in.audio) a.insert(a.end(), {"-af", "areverse"});
    codec(a);
    a.push_back(p);
    plan.steps.push_back(a);
    plan.stepSeconds.push_back(d);
    plan.pieces.push_back(p);
    joinOrder.push_back(p);
  }
  plan.listFile = in.workDir + "/pieces.txt";
  for (const auto& p : joinOrder) {
    std::string quoted;
    for (char c : p) {
      if (c == '\'') quoted += "'\\''";
      else quoted += c;
    }
    plan.listText += "file '" + quoted + "'\n";
  }
  plan.steps.push_back({"ffmpeg", "-y", "-f", "concat", "-safe", "0", "-i", plan.listFile, "-c", "copy",
                        "-movflags", "+faststart", in.dest});
  plan.stepSeconds.push_back(0.0);
  plan.seconds = length + (in.bounce ? std::max(0.0, length - 2.0 * frame) : 0.0);
  return plan;
}

// THE LOSSLESS COPY's other two steps (engine/direction_remux.hpp writes the
// picture): the range's sound reversed (bounce: forwards then backwards, less
// the turnaround frames, as the picture) to a WAV, then picture and sound
// joined by stream copy. The sound is small enough for areverse to hold.
inline std::vector<std::string> planDirectionSound(const DirectionBakeInput& in, const std::string& wav) {
  const double fps = in.fps > 1.0 && in.fps < 1000.0 ? in.fps : 30.0;
  const double frame = 1.0 / fps;
  const double length = in.out - in.in;
  std::vector<std::string> a {"ffmpeg", "-y", "-ss", directionBakeNumber(in.in), "-t", directionBakeNumber(length),
                              "-i", in.source, "-vn"};
  if (in.bounce) {
    a.insert(a.end(), {"-filter_complex",
                       "[0:a]asplit[f][b];[b]atrim=start=" + directionBakeNumber(frame) + ":end=" +
                         directionBakeNumber(length - frame) + ",asetpts=PTS-STARTPTS,areverse[r];[f][r]concat=n=2:v=0:a=1"});
  } else {
    a.insert(a.end(), {"-af", "areverse"});
  }
  a.insert(a.end(), {"-c:a", "pcm_s16le", "-ar", "48000", "-ac", "2", wav});
  return a;
}

inline std::vector<std::string> planDirectionJoin(const std::string& video, const std::string& wav, const std::string& dest) {
  return {"ffmpeg", "-y", "-i", video, "-i", wav, "-map", "0:v:0", "-map", "1:a:0", "-c", "copy",
          "-movflags", "+faststart", dest};
}

}  // namespace deckboy::core
