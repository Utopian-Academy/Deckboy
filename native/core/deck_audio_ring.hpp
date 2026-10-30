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
#include <atomic>
#include <chrono>
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
    buffer.lastPushAt = Clock::now();
    buffer.samples.insert(buffer.samples.end(), samples.begin(), samples.end());
    if (buffer.samples.size() > kMaxSamplesPerDeck) {
      const std::size_t dropCount = buffer.samples.size() - kMaxSamplesPerDeck;
      buffer.samples.erase(buffer.samples.begin(),
                           buffer.samples.begin() +
                             static_cast<std::ptrdiff_t>(dropCount));
      buffer.dropped += static_cast<std::uint64_t>(dropCount);
    }
  }

  // The same, with the playlist read from its slot INSIDE the lock, so a push
  // and remap() cannot cross: a block lands either before the playlists move
  // (and moves with them) or after (at the new place), never in between.
  void push(const std::atomic<int>& slot, const std::vector<std::int16_t>& samples) {
    if (samples.empty()) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const int deckIndex = slot.load(std::memory_order_relaxed);
    if (deckIndex < 0) {
      return;
    }
    if (deckIndex >= static_cast<int>(decks_.size())) {
      decks_.resize(static_cast<std::size_t>(deckIndex) + 1);
    }
    Buffer& buffer = decks_[static_cast<std::size_t>(deckIndex)];
    buffer.lastPushAt = Clock::now();
    buffer.samples.insert(buffer.samples.end(), samples.begin(), samples.end());
    if (buffer.samples.size() > kMaxSamplesPerDeck) {
      const std::size_t dropCount = buffer.samples.size() - kMaxSamplesPerDeck;
      buffer.samples.erase(buffer.samples.begin(),
                           buffer.samples.begin() +
                             static_cast<std::ptrdiff_t>(dropCount));
      buffer.dropped += static_cast<std::uint64_t>(dropCount);
    }
  }

  // THE PLAYLISTS MOVED -- one removed, one put back by an undo. Each buffer
  // goes with its playlist (oldToNew[o] is where playlist o is now, -1 if it
  // is gone), so a recording of playlist 3 that is now playlist 2 carries on
  // where it was. `whileLocked` runs inside the lock: the caller moves the
  // engines' slots there, so no push can land between the two.
  template <typename WhileLocked>
  void remap(const std::vector<int>& oldToNew, std::size_t deckCount, WhileLocked&& whileLocked) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Buffer> next(deckCount);
    for (std::size_t o = 0; o < decks_.size() && o < oldToNew.size(); ++o) {
      const int d = oldToNew[o];
      if (d >= 0 && static_cast<std::size_t>(d) < deckCount) {
        next[static_cast<std::size_t>(d)] = std::move(decks_[o]);
      }
    }
    decks_.swap(next);
    whileLocked();
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

  // How many samples a consumer could take right now without any deck having
  // to be padded with silence: the smallest amount available across the decks
  // that are actually producing. A deck that has never pushed anything -- a
  // silent cue, a deck with no audio at all -- is not counted, because waiting
  // for audio it will never send would starve the encoder.
  //
  // A recorder that simply takes one frame's worth per frame written pads the
  // shortfall with zeroes whenever a chunk lands a beat late, and that zero
  // padding is PERMANENT in the file: MEASURED, three silent runs of 5-19ms in
  // every five-second take. The samples themselves were never lost -- the read
  // position only advances by what was mixed -- so the fix is to carry the
  // shortfall and take it on a later frame instead of writing a hole.
  std::size_t mixableSamples(const std::vector<int>& deckIndices,
                             const std::map<int, std::uint64_t>& readPositions,
                             int liveWithinMs = 400) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = Clock::now();
    std::size_t smallest = kUnlimited;
    for (const int deckIndex : deckIndices) {
      if (deckIndex < 0 || deckIndex >= static_cast<int>(decks_.size())) {
        continue;
      }
      const Buffer& buffer = decks_[static_cast<std::size_t>(deckIndex)];
      if (buffer.samples.empty() && buffer.dropped == 0) {
        continue;  // never produced anything; nothing to wait for
      }
      // A deck that has not pushed for a WHILE is not behind, it is QUIET --
      // stopped, paused, or holding a still. Waiting for audio it is not
      // sending would hold the whole pull back; silence is the right answer
      // for it, and the consumer is told so by leaving it out of the limit.
      //
      // The window is generous on purpose. A deck that has just been taken
      // delivers its first audio in a burst and can then be a couple of
      // hundred milliseconds quiet while the decode settles, and calling that
      // "quiet" wrote a 39ms hole into the middle of the sound -- measured, on
      // the takes where the first burst arrived early.
      if (buffer.lastPushAt.time_since_epoch().count() == 0 ||
          std::chrono::duration_cast<std::chrono::milliseconds>(
            now - buffer.lastPushAt).count() > liveWithinMs) {
        continue;
      }
      const auto it = readPositions.find(deckIndex);
      if (it == readPositions.end()) {
        continue;  // first sight of this deck: it contributes nothing yet
      }
      const std::uint64_t availableBegin = buffer.dropped;
      const std::uint64_t availableEnd =
        availableBegin + static_cast<std::uint64_t>(buffer.samples.size());
      const std::uint64_t readPos =
        std::clamp(it->second, availableBegin, availableEnd);
      smallest = std::min<std::size_t>(
        smallest, static_cast<std::size_t>(availableEnd - readPos));
    }
    return smallest;
  }

  // INVESTIGATION: what a reader would find on one playlist right now.
  struct TraceState {
    std::uint64_t available = 0;   // samples past the reader's position
    long long sinceLastPushMs = -1;
  };
  TraceState traceState(int deckIndex, const std::map<int, std::uint64_t>& readPositions) const {
    std::lock_guard<std::mutex> lock(mutex_);
    TraceState state;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(decks_.size())) return state;
    const Buffer& buffer = decks_[static_cast<std::size_t>(deckIndex)];
    const std::uint64_t begin = buffer.dropped;
    const std::uint64_t end = begin + buffer.samples.size();
    const auto it = readPositions.find(deckIndex);
    const std::uint64_t pos = it == readPositions.end() ? end : std::clamp(it->second, begin, end);
    state.available = end - pos;
    if (buffer.lastPushAt.time_since_epoch().count() != 0) {
      state.sinceLastPushMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - buffer.lastPushAt).count();
    }
    return state;
  }

  // Returned by mixableSamples when no deck limits the pull.
  static constexpr std::size_t kUnlimited = static_cast<std::size_t>(-1);

  struct ClockSample {
    int deckIndex = -1;
    std::uint64_t samples = 0;
    double ageSeconds = 0.0;
  };

  // The tap counts audio the device has played. A live muxer can follow that
  // clock instead of letting the sound-card crystal drift against wall time.
  ClockSample clockSample(const std::vector<int>& deckIndices, int preferredDeck) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = Clock::now();
    ClockSample first;
    for (int deckIndex : deckIndices) {
      if (deckIndex < 0 || deckIndex >= static_cast<int>(decks_.size())) continue;
      const Buffer& buffer = decks_[static_cast<std::size_t>(deckIndex)];
      if (buffer.lastPushAt.time_since_epoch().count() == 0) continue;
      const double age = std::chrono::duration<double>(now - buffer.lastPushAt).count();
      if (age > 0.4) continue;
      ClockSample sample {deckIndex, endPositionLocked(deckIndex), age};
      if (deckIndex == preferredDeck) return sample;
      if (first.deckIndex < 0) first = sample;
    }
    return first;
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
  using Clock = std::chrono::steady_clock;

  struct Buffer {
    std::vector<std::int16_t> samples;
    std::uint64_t dropped = 0;   // absolute position of samples[0]
    Clock::time_point lastPushAt{};  // when this deck last produced anything
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
