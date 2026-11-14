// The VJ beat clock's arithmetic, with no SDL and no app state, so the smoke
// test can run it on its own.
//
// A "beat position" is beats on a running timeline: the tapped grid's origin
// counts as beat 0, and with Ableton Link it is the session's own timeline,
// whose multiples of four are the bar lines every peer agrees on.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace deckboy::vjclock {

constexpr int kBeatsPerBar = 4;   // Link's quantum, and the light's four lamps

// The lengths a take can wait for, in beats: a beat, 1 bar, 2 bars, 4 bars.
inline int sanitiseQuantiseBeats(int beats) {
  switch (beats) {
    case 1: case 4: case 8: case 16: return beats;
    default: return 1;
  }
}

// The QUANTISE choice: off, or a length in beats. `on` false is "off"; the
// length is remembered underneath so turning it back on keeps it.
struct Quantise {
  bool on = false;
  int beats = 1;
};

// beat -> 1 bar -> 2 bars -> 4 bars -> off -> beat.
inline Quantise nextQuantise(Quantise q) {
  if (!q.on) {
    return {true, 1};
  }
  switch (sanitiseQuantiseBeats(q.beats)) {
    case 1: return {true, 4};
    case 4: return {true, 8};
    case 8: return {true, 16};
    default: return {false, 16};
  }
}

// Whole literals, so each one has a line in the language catalogues.
inline const char* quantiseLabel(Quantise q) {
  if (!q.on) return "QUANTISE: off";
  switch (sanitiseQuantiseBeats(q.beats)) {
    case 4: return "QUANTISE: 1 bar";
    case 8: return "QUANTISE: 2 bars";
    case 16: return "QUANTISE: 4 bars";
    default: return "QUANTISE: beat";
  }
}

// The remote's words: BEAT, 1, 2, 4 (bars), OFF; ON keeps the current length.
// Returns false for anything else, so a typo is refused rather than ignored.
inline bool parseQuantise(const std::string& word, Quantise& q) {
  std::string w;
  for (char c : word) {
    w.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  if (w == "OFF") { q.on = false; return true; }
  if (w == "ON") { q.on = true; return true; }
  if (w == "BEAT") { q = {true, 1}; return true; }
  if (w == "1") { q = {true, 4}; return true; }
  if (w == "2") { q = {true, 8}; return true; }
  if (w == "4") { q = {true, 16}; return true; }
  return false;
}

// Which lamp of the four-beat light is lit: 0..3.
inline int beatInBar(double beatPos) {
  const double m = std::fmod(beatPos, static_cast<double>(kBeatsPerBar));
  const double pos = m < 0.0 ? m + kBeatsPerBar : m;
  return std::clamp(static_cast<int>(pos), 0, kBeatsPerBar - 1);
}

// Seconds until the next boundary of `lengthBeats`. A boundary we are standing
// on (to within a hair) is now, not a whole length away.
inline double secondsToBoundary(double beatPos, double bpm, int lengthBeats) {
  const double len = static_cast<double>(sanitiseQuantiseBeats(lengthBeats));
  const double beatSeconds = 60.0 / std::clamp(bpm, 20.0, 300.0);
  double into = std::fmod(beatPos, len);
  if (into < 0.0) {
    into += len;
  }
  if (into < 1e-6 || len - into < 1e-6) {
    return 0.0;
  }
  return (len - into) * beatSeconds;
}

}  // namespace deckboy::vjclock
