// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// deck_audio_ring.hpp — per-deck PCM for the sinks that mux audio.
//
// The stream writer, the recorder and the NDI sender all need the same decks'
// audio, and they consume it at their own pace: a recording started ten
// seconds into a show is at a different point in the same buffer than an NDI
// sender that has been running since the top. So this is a SLIDING WINDOW
// addressed by absolute sample position, not a queue -- taking samples for one
// sink must not remove them from under another.
//
// THE DROPPED COUNT IS NOT BOOKKEEPING. It is the absolute position of the
// oldest sample still held, which makes it the origin every consumer's read
// position is measured from. It is also what lets a consumer that fell behind
// be clamped FORWARD to the oldest audio that still exists, rather than
// silently resuming at whatever now sits at index zero -- which would be a
// jump in time that reads as a click and then a permanent offset.
//
// This looks like the single-consumer input-mix buffer elsewhere in the output
// code, and it is not one. That buffer is read from the front and erased; this
// one cannot be, and collapsing the two would break the position arithmetic
// that keeps recorded audio in sync with its picture.
//
// One lock covers a whole mix rather than one deck at a time: a push landing
// between two decks would otherwise give one deck a sample that the other
// deck's half of the same frame does not have.
// ============================================================================

#ifndef DECKBOY_CORE_DECK_AUDIO_RING_HPP
#define DECKBOY_CORE_DECK_AUDIO_RING_HPP

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace deckboy::core {

class DeckAudioRing {
 public:
  // ~10s of stereo at 48k. An deck nobody is muxing must not grow for the
  // length of a show; over the cap the OLDEST samples go, because a sink that
  // has fallen ten seconds behind wants to catch up, not to replay history.
  static constexpr std::size_t kMaxSamplesPerDeck =
    static_cast<std::size_t>(48000) * 2 * 10;

  void push(int deckIndex, const std::vector<std::int16_t>& samples) {
    if (deckIndex < 0 || samples.empty()) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (deckIndex >= static_cast<int>(decks_.size())) {
      decks_.resize(static_cast<std::size_t>(deckIndex) + 1);
    }
    Buffer& buffer = decks_[static_cast<std::size_t>(deckIndex)];
    buffer.samples.insert(buffer.samples.end(), samples.begin(), samples.end());
    if (buffer.samples.size() > kMaxSamplesPerDeck) {
      const std::size_t dropCount = buffer.samples.size() - kMaxSamplesPerDeck;
      buffer.samples.erase(buffer.samples.begin(),
                           buffer.samples.begin() +
                             static_cast<std::ptrdiff_t>(dropCount));
      buffer.dropped += static_cast<std::uint64_t>(dropCount);
    }
  }

  void clear(std::size_t deckCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    decks_.clear();
    decks_.resize(deckCount);
  }

  // Where a consumer starting NOW should begin: the end of what is held, so it
  // gets the next sample produced rather than the backlog.
  void primeEndPositions(const std::vector<int>& deckIndices,
                         std::map<int, std::uint64_t>& out) const {
    out.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    for (const int deckIndex : deckIndices) {
      out[deckIndex] = endPositionLocked(deckIndex);
    }
  }

  // Sum `want` samples per deck into `mixed`, advancing each deck's read
  // position. A consumer seeing a deck for the first time is placed at the end
  // and contributes nothing this frame -- the same rule primeEndPositions
  // applies, for a deck that appeared after the sink started.
  void mixDecks(const std::vector<int>& deckIndices,
                std::map<int, std::uint64_t>& readPositions,
                std::vector<std::int32_t>& mixed, std::size_t want,
                std::size_t deckCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (decks_.size() < deckCount) {
      decks_.resize(deckCount);
    }
    for (const int deckIndex : deckIndices) {
      if (deckIndex < 0 || deckIndex >= static_cast<int>(decks_.size())) {
        continue;
      }
      const Buffer& buffer = decks_[static_cast<std::size_t>(deckIndex)];
      const std::uint64_t availableBegin = buffer.dropped;
      const std::uint64_t availableEnd =
        availableBegin + static_cast<std::uint64_t>(buffer.samples.size());
      auto [it, inserted] = readPositions.try_emplace(deckIndex, availableEnd);
      if (inserted) {
        continue;
      }
      std::uint64_t& readPos = it->second;
      // Clamp INTO the window. Below it the consumer fell behind and the audio
      // it wanted no longer exists; above it the buffer was cleared under it.
      readPos = std::clamp(readPos, availableBegin, availableEnd);
      const std::uint64_t available = availableEnd - readPos;
      const std::size_t toMix = static_cast<std::size_t>(
        std::min<std::uint64_t>(available, static_cast<std::uint64_t>(want)));
      const std::size_t sourceOffset =
        static_cast<std::size_t>(readPos - availableBegin);
      for (std::size_t i = 0; i < toMix && i < mixed.size(); ++i) {
        mixed[i] += static_cast<std::int32_t>(buffer.samples[sourceOffset + i]);
      }
      readPos += static_cast<std::uint64_t>(toMix);
    }
  }

 private:
  struct Buffer {
    std::vector<std::int16_t> samples;
    std::uint64_t dropped = 0;   // absolute position of samples[0]
  };

  std::uint64_t endPositionLocked(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(decks_.size())) {
      return 0;
    }
    const Buffer& buffer = decks_[static_cast<std::size_t>(deckIndex)];
    return buffer.dropped + static_cast<std::uint64_t>(buffer.samples.size());
  }

  mutable std::mutex mutex_;
  std::vector<Buffer> decks_;
};

}  // namespace deckboy::core

#endif  // DECKBOY_CORE_DECK_AUDIO_RING_HPP
