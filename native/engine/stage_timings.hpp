// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// WHERE A FRAME'S TIME GOES, process-wide running totals. Read twice, diff,
// and the stage that cannot keep up is the one whose time per frame exceeds
// the film's frame period: decoding, converting to the upload format, or
// drawing (which includes the upload to the GPU). Added when a Pi 3 showed
// 19.8 new frames a second from a 24 fps film with hardware decode on, and
// nothing said which stage was short. Mini's STATUS prints them and
// tools/check_mini_smoothness.py turns them into milliseconds per frame.
//
// Its own header so that only the files that count or print these depend on
// it: on a 1 GB Pi a header the whole engine includes costs a full rebuild.

#include <atomic>
#include <cstdint>

namespace deckboy::libav {

struct StageTimings {
  std::atomic<std::uint64_t> decodeNs {0};   // packets in, a frame out (includes convert)
  std::atomic<std::uint64_t> convertNs {0};  // that picture into the frame format
  std::atomic<std::uint64_t> decoded {0};
  std::atomic<std::uint64_t> drawNs {0};     // engine update (uploads) + render (draws)
  std::atomic<std::uint64_t> draws {0};
};

inline StageTimings& stageTimings() {
  static StageTimings timings;
  return timings;
}

}  // namespace deckboy::libav
