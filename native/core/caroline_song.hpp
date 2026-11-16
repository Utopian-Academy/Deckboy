// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// caroline_song.hpp -- "Ascendimonia", STAIRLIFT's song, which Caroline
// Polkadot sings live over the Datamosh lesson's music video.
//
// James, 2026-11-15: "if caroline polkadot sang over the video, that would be
// cool ... i mean live WITH the video ... make sure she sings no matter what
// voice type is selected". The lyrics are his approved draft.
//
// The reel's sound is the Deckboy theme (STAIRLIFT version): 84 BPM, C minor,
// twelve bars, one shot a bar. The lead synth plays the motif in bars 4-5
// (counting from 0) and the theme in 6-10, so the voice sings the first two
// lines where the lead rests (bars 2-3), doubles the motif an octave below on
// the third (bar 4), rests under the theme, and sings the hooks on the
// E-flat and A-flat major landings (bars 8-10) and the last "ah" over the
// final chord (bar 11).
//
// WORDS sings the lyrics with the vocal tract, SOUNDS sings her own syllables
// on the same notes, and CHIP plays the melody on a Game Boy pulse with an
// edge on each syllable (chipSing). Times are seconds from the reel's start.
// ============================================================================

#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "core/vocal_tract.hpp"

namespace deckboy::song {

inline constexpr double kAscendimoniaBpm = 84.0;

inline double midiHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

// The melody: bar (from 0), sixteenth in the bar, length in sixteenths,
// the sung syllable (spelled as it sounds), the note.
inline std::vector<voice::SungNote> ascendimonia(bool sounds) {
  struct N {
    int bar, at, len;
    const char* text;
    int midi;
  };
  static const N kScore[] = {
    // Bar 2, C minor 9: "My dragon is scared of the moving stairs,"
    {2, 0, 2, "my", 67}, {2, 2, 1, "dra", 63}, {2, 3, 1, "gon", 62}, {2, 4, 2, "is", 60},
    {2, 6, 2, "scared", 63}, {2, 8, 1, "of", 62}, {2, 9, 1, "the", 60}, {2, 10, 1, "mo", 58},
    {2, 11, 1, "ving", 60}, {2, 12, 4, "stairs", 67},
    // Bar 3, F minor 9: "my wizard forgot the magic word,"
    {3, 0, 2, "my", 68}, {3, 2, 1, "wiz", 67}, {3, 3, 1, "erd", 65}, {3, 4, 1, "for", 63},
    {3, 5, 2, "got", 60}, {3, 7, 1, "the", 60}, {3, 8, 1, "ma", 63}, {3, 9, 1, "jik", 65},
    {3, 10, 6, "word", 67},
    // Bar 4, C minor 9, the lead's motif an octave above: "I'm going up in my socks and my crown"
    {4, 0, 1, "aim", 60}, {4, 1, 1, "go", 62}, {4, 2, 1, "ing", 63}, {4, 3, 5, "up", 67},
    {4, 8, 1, "in", 62}, {4, 9, 1, "my", 62}, {4, 10, 1, "socks", 62}, {4, 11, 1, "and", 58},
    {4, 12, 1, "my", 58}, {4, 13, 7, "crown", 60},
    // Bars 8-9, E-flat major 7: "Ascendimonia (ah, ah)"
    {8, 4, 2, "as", 63}, {8, 6, 2, "sen", 67}, {8, 8, 2, "dee", 70}, {8, 10, 2, "mo", 67},
    {8, 12, 2, "nee", 65}, {8, 14, 6, "ah", 62}, {9, 8, 2, "ah", 70}, {9, 12, 4, "ah", 67},
    // Bar 10, A-flat major 7: "Ascendimonia"
    {10, 0, 2, "as", 63}, {10, 2, 2, "sen", 67}, {10, 4, 2, "dee", 68}, {10, 6, 2, "mo", 67},
    {10, 8, 2, "nee", 63}, {10, 10, 6, "ah", 60},
    // Bar 11, the final C minor (add 9): "(ah)"
    {11, 0, 10, "ah", 67},
  };
  // Her own syllables (lalalee, doobee, oooh), one a note. Voiced ones only:
  // the pitch cannot move through an unvoiced "sh", so a short note after a
  // leap landed late.
  static const char* kSounds[] = {"la", "la", "lee", "doo", "bee", "la", "la", "ooh", "lee"};
  const double sixteenth = 60.0 / kAscendimoniaBpm / 4.0;
  std::vector<voice::SungNote> out;
  int i = 0;
  for (const N& n : kScore) {
    voice::SungNote note;
    note.text = sounds ? (n.len >= 6 ? "ooh" : kSounds[i++ % 9]) : n.text;
    note.start = (n.bar * 16 + n.at) * sixteenth;
    note.length = n.len * sixteenth;
    note.hz = midiHz(n.midi);
    out.push_back(note);
  }
  return out;
}

// CHIP: the melody on a 25% pulse, an octave up so it sits like the
// lessons' chip voices, each syllable struck (fast attack, a fall to 70%,
// a short release before the next) with a little vibrato on held notes.
inline std::vector<float> chipSing(const std::vector<voice::SungNote>& notes, int rate = 48000) {
  double end = 0.0;
  for (const auto& n : notes) end = std::max(end, n.start + n.length);
  std::vector<float> out(static_cast<std::size_t>((end + 0.3) * rate), 0.0f);
  for (const auto& n : notes) {
    const int from = static_cast<int>(n.start * rate);
    const int count = static_cast<int>(n.length * rate);
    const double hz = n.hz * 2.0;
    double phase = 0.0;
    for (int k = 0; k < count && from + k < static_cast<int>(out.size()); ++k) {
      const double t = static_cast<double>(k) / rate;
      const double left = static_cast<double>(count - k) / rate;
      double env = std::min(1.0, t / 0.004);
      env *= 0.7 + 0.3 * std::exp(-t / 0.08);
      env *= std::min(1.0, left / 0.025);
      const double vib = 1.0 + (t > 0.25 ? 0.006 * std::sin(2.0 * 3.141592653589793 * 5.5 * t) : 0.0);
      phase += hz * vib / rate;
      phase -= std::floor(phase);
      out[static_cast<std::size_t>(from + k)] += static_cast<float>((phase < 0.25 ? 0.3 : -0.1) * env);
    }
  }
  return out;
}

}  // namespace deckboy::song
