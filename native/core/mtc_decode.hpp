// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// mtc_decode.hpp — MIDI Time Code quarter-frame assembly.
//
// A sender does not transmit a timecode; it transmits EIGHT NIBBLES, one every
// quarter of a frame, and the receiver reassembles them. That is the whole of
// the format, and it is pure arithmetic on bytes -- it has nothing to do with
// which library delivered the byte.
//
// It lived inside `#if defined(DECKBOY_HAS_ALSA)` in the app, which is how
// Deckboy came to accept MIDI timecode on Linux and ignore it on Windows and
// macOS, where the MIDI input is RtMidi and perfectly capable of carrying the
// same bytes. The integration catalog then reported "MTC ingest needs the ALSA
// sequencer (Linux only)", which is not true of MTC -- it was true of where the
// decoder happened to be written. Here it is platform-free and testable
// without a MIDI interface (`--mtc-check`).
//
// The nibbles arrive in a fixed order, low to high: frames, seconds, minutes,
// then the hour and the RATE, which is why the frame rate cannot be known until
// the last piece arrives and why nothing can be reported until all eight are
// in.
//
// WHAT THIS DELIBERATELY DOES NOT DO: add the two-frame offset. Eight quarter
// frames take two frames to send, so by the time the last one lands the sender
// has moved on and a strict reading is two frames behind. Deckboy has never
// applied that offset, the Linux path has been used this way in real shows, and
// silently shifting everyone's timecode by 83ms while refactoring is not a
// change to make without a machine to verify it against. It is a real question,
// recorded rather than answered.
// ============================================================================

#ifndef DECKBOY_CORE_MTC_DECODE_HPP
#define DECKBOY_CORE_MTC_DECODE_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace deckboy::core {

// The two-bit rate code carried in the last nibble. 29.97 is the drop-frame
// rate; the code says nothing about whether the count itself drops frames, and
// Deckboy treats the number as a rate rather than trying to reconstruct a
// drop-frame count.
inline double mtcFpsForRateCode(int rateCode) {
  switch (rateCode & 0x03) {
    case 0: return 24.0;
    case 1: return 25.0;
    case 2: return 29.97;
    default: return 30.0;
  }
}

class MtcQuarterFrameDecoder {
 public:
  struct Timecode {
    double seconds = 0.0;
    double fps = 0.0;
  };

  MtcQuarterFrameDecoder() { reset(); }

  // -1 means "not seen yet", which is what makes the first full timecode wait
  // for a complete set rather than reporting a time assembled from whatever
  // the decoder happened to catch mid-sequence.
  void reset() { nibbles_.fill(-1); }

  // One quarter-frame data byte: the message type in bits 4-6, the nibble in
  // bits 0-3. Returns a timecode only once all eight pieces are present, and
  // then on every subsequent byte -- the sender keeps cycling, so after the
  // first complete set every byte updates one nibble of a time that is already
  // whole.
  std::optional<Timecode> feed(int quarterFrameByte) {
    const int messageType = (quarterFrameByte >> 4) & 0x07;
    const int nibbleValue = quarterFrameByte & 0x0F;
    nibbles_[static_cast<std::size_t>(messageType)] = nibbleValue;
    for (const int nibble : nibbles_) {
      if (nibble < 0) {
        return std::nullopt;
      }
    }

    int frames  = (nibbles_[1] << 4) | nibbles_[0];
    int seconds = (nibbles_[3] << 4) | nibbles_[2];
    int minutes = (nibbles_[5] << 4) | nibbles_[4];
    const int hourLow = nibbles_[6];
    const int hourHighAndRate = nibbles_[7];
    int hours = ((hourHighAndRate & 0x01) << 4) | hourLow;
    const double fps = mtcFpsForRateCode((hourHighAndRate >> 1) & 0x03);
    if (fps < 1.0) {
      return std::nullopt;
    }

    // The frame number runs 0 to fps-1. The old bound was ceil(fps), which let
    // a corrupt byte through as a frame count equal to a whole second and put
    // the reported time a second out -- the one value a clamp is there to stop.
    frames = std::clamp(frames, 0, static_cast<int>(std::ceil(fps)) - 1);
    seconds = std::clamp(seconds, 0, 59);
    minutes = std::clamp(minutes, 0, 59);
    hours = std::clamp(hours, 0, 23);

    Timecode out;
    out.seconds = hours * 3600.0 + minutes * 60.0 + seconds + (frames / fps);
    out.fps = fps;
    return out;
  }

  // Has a complete set arrived? Useful to a caller that wants to know whether
  // silence means "no sender" or "sender mid-sequence".
  bool complete() const {
    for (const int nibble : nibbles_) {
      if (nibble < 0) return false;
    }
    return true;
  }

 private:
  std::array<int, 8> nibbles_ {};
};

// The eight quarter-frame BYTES a sender would transmit for one timecode, in
// order. This is the encoder half, and it exists so the decoder can be checked
// without a MIDI interface: feed these in and the same timecode must come back.
inline std::array<int, 8> mtcQuarterFrameBytes(int hours, int minutes,
                                               int seconds, int frames,
                                               int rateCode) {
  const int hourField = ((rateCode & 0x03) << 1) | ((hours >> 4) & 0x01);
  const std::array<int, 8> pieces = {
    frames & 0x0F,         (frames >> 4) & 0x0F,
    seconds & 0x0F,        (seconds >> 4) & 0x0F,
    minutes & 0x0F,        (minutes >> 4) & 0x0F,
    hours & 0x0F,          hourField & 0x0F,
  };
  std::array<int, 8> bytes {};
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    bytes[i] = static_cast<int>((i << 4) | static_cast<std::size_t>(pieces[i]));
  }
  return bytes;
}

}  // namespace deckboy::core

#endif  // DECKBOY_CORE_MTC_DECODE_HPP
