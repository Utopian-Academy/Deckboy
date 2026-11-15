// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Deckboy contributors
//
// vocal_tract.hpp -- a talking vocal tract for the lesson characters.
//
// The physical model is Pink Trombone's (Neil Thapen, 2017, MIT licence):
// a Liljencrants-Fant glottal pulse with aspiration noise, a 44-section
// Kelly-Lochbaum waveguide for the mouth and a 28-section one for the nose,
// stepped twice per sample, and turbulence noise injected where the tongue,
// teeth or lips nearly close. James chose "best of both" (2026-11-14): that
// model, with the vowels tuned to the formants shown in his reference video
// (a vocal-tract simulator built on it: u 380/1028 Hz, a 734/1145 Hz).
//
// What Deckboy adds: words in, mouth shapes out. A line of text becomes a
// list of phonemes by spelling rules (rough on purpose: this is a cartoon
// voice), each phoneme a target for the tongue, a constriction, the velum and
// the voice; the tract glides between targets at a speed limit, which is what
// makes the result sound like a mouth and not a vocoder.
//
// SDL-free and allocation-light: speak() renders a whole line at once.
//
// The Pink Trombone licence (MIT) asks that this notice travel with the code:
//   Copyright 2017 Neil Thapen. Permission is hereby granted, free of charge,
//   to any person obtaining a copy of this software and associated
//   documentation files (the "Software"), to deal in the Software without
//   restriction, including without limitation the rights to use, copy,
//   modify, merge, publish, distribute, sublicense, and/or sell copies of the
//   Software, and to permit persons to whom the Software is furnished to do
//   so, subject to the following conditions: The above copyright notice and
//   this permission notice shall be included in all copies or substantial
//   portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
//   WARRANTY OF ANY KIND.

#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace deckboy::voice {

// Who is talking.
struct VoiceParams {
  double pitchHz = 140.0;     // the speaking pitch
  double tractScale = 1.0;    // tract length: 0.8 a child, 1.2 a giant
  double tenseness = 0.6;     // 0 breathy, 1 pressed
  double vibrato = 0.005;     // depth, as a fraction of the pitch
  double speed = 1.0;         // 1 normal; 1.3 quicker
  double melody = 0.25;       // how far the pitch roams, in octaves
  double swoop = 0.0;         // 0..1: the reference's silly slides between syllables
  double level = 0.9;
};

// One phoneme's mouth.
struct Target {
  double tongueIndex = 21.0;   // in Pink Trombone's 44-section units: 12 back .. 29 front
  double tongueDiam = 2.75;    // 2.05 high (closed) .. 3.5 low (open)
  double conIndex = -1.0;      // a constriction (consonant or lips), -1 none
  double conDiam = 0.0;        // 0 closed; ~0.4 a hiss; ~1 an approximant
  double lipRound = -1.0;      // a second constriction at the lips, -1 none
  double velum = 0.01;         // 0.01 shut, 0.4 open (nasals)
  double voice = 1.0;          // the voice's loudness
  double hiss = 0.0;           // fricative noise at the constriction
  double tenseness = -1.0;     // -1 the speaker's own; low for h and after a plosive
  double ms = 90.0;
  double pitch = 0.0;          // in octaves, from the phrase's line
  bool vowel = false;
};

namespace detail {

struct Rng {
  std::uint32_t s = 0x9E3779B9u;
  double next() {   // -1..1
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (static_cast<double>(s) / 4294967295.0) * 2.0 - 1.0;
  }
};

// A two-pole band-pass (RBJ), for the aspiration and the hiss.
struct BandPass {
  double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  void set(double f, double q, double rate) {
    const double w = 2.0 * 3.141592653589793 * f / rate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = alpha / a0; b2 = -alpha / a0;
    a1 = -2.0 * std::cos(w) / a0; a2 = (1.0 - alpha) / a0;
  }
  double run(double x) {
    const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    return y;
  }
};

class Glottis {
public:
  double frequency = 140, oldFrequency = 140, newFrequency = 140;
  double tenseness = 0.6, oldTenseness = 0.6, newTenseness = 0.6;
  double intensity = 0.0;   // set per block by the speaker

  explicit Glottis(double rate) : rate_(rate) { setup(0.0); }

  double step(double lambda, double noise) {
    t_ += 1.0 / rate_;
    if (t_ > period_) { t_ -= period_; setup(lambda); }
    double out = lf(t_ / period_);
    double asp = intensity * (1.0 - std::sqrt(tenseness)) * noiseModulator() * noise;
    asp *= 0.2;
    return out + asp;
  }
  double noiseModulator() const {
    const double voiced = 0.1 + 0.2 * std::max(0.0, std::sin(2.0 * 3.141592653589793 * t_ / period_));
    return tenseness * intensity * voiced + (1.0 - tenseness * intensity) * 0.3;
  }

private:
  void setup(double lambda) {
    frequency = oldFrequency * (1.0 - lambda) + newFrequency * lambda;
    tenseness = oldTenseness * (1.0 - lambda) + newTenseness * lambda;
    period_ = 1.0 / std::max(20.0, frequency);
    double rd = std::clamp(3.0 * (1.0 - tenseness), 0.5, 2.7);
    const double ra = -0.01 + 0.048 * rd;
    const double rk = 0.224 + 0.118 * rd;
    const double rg = (rk / 4.0) * (0.5 + 1.2 * rk) / (0.11 * rd - ra * (0.5 + 1.2 * rk));
    const double ta = ra, tp = 1.0 / (2.0 * rg), te = tp + tp * rk;
    epsilon_ = 1.0 / ta;
    shift_ = std::exp(-epsilon_ * (1.0 - te));
    delta_ = 1.0 - shift_;
    double rhs = (1.0 / epsilon_) * (shift_ - 1.0) + (1.0 - te) * shift_;
    rhs /= delta_;
    const double lower = -(te - tp) / 2.0 + rhs;
    const double upper = -lower;
    omega_ = 3.141592653589793 / tp;
    const double s = std::sin(omega_ * te);
    const double y = -3.141592653589793 * s * upper / (tp * 2.0);
    const double z = std::log(std::max(1e-9, y));
    alpha_ = z / (tp / 2.0 - te);
    e0_ = -1.0 / (s * std::exp(alpha_ * te));
    te_ = te;
    loudness_ = std::pow(tenseness, 0.25);
  }
  double lf(double t) const {
    double o = t > te_ ? (-std::exp(-epsilon_ * (t - te_)) + shift_) / delta_
                       : e0_ * std::exp(alpha_ * t) * std::sin(omega_ * t);
    return o * intensity * loudness_;
  }
  double rate_;
  double t_ = 0.0, period_ = 1.0 / 140.0;
  double epsilon_ = 0, shift_ = 0, delta_ = 1, omega_ = 0, alpha_ = 0, e0_ = 0, te_ = 0, loudness_ = 1;
};

class Tract {
public:
  explicit Tract(double scale) {
    n = std::clamp(static_cast<int>(std::lround(44.0 * scale)), 30, 64);
    bladeStart = n * 10 / 44;
    tipStart = n * 32 / 44;
    lipStart = n * 39 / 44;
    noseLength = n * 28 / 44;
    noseStart = n - noseLength + 1;
    const int vec = n + 1;
    diameter.assign(vec, 0); rest.assign(vec, 0); target.assign(vec, 0);
    R.assign(vec, 0); L.assign(vec, 0); refl.assign(vec, 0); newRefl.assign(vec, 0);
    jR.assign(vec + 1, 0); jL.assign(vec + 1, 0); A.assign(vec, 0);
    for (int i = 0; i < n; ++i) {
      double d = 0;
      if (i < 7.0 * n / 44.0 - 0.5) d = 0.6;
      else if (i < 12.0 * n / 44.0) d = 1.1;
      else d = 1.5;
      diameter[i] = rest[i] = target[i] = d;
    }
    noseR.assign(noseLength + 1, 0); noseL.assign(noseLength + 1, 0);
    nR.assign(noseLength + 2, 0); nL.assign(noseLength + 2, 0);
    noseRefl.assign(noseLength + 1, 0); noseD.assign(noseLength + 1, 0); noseA.assign(noseLength + 1, 0);
    for (int i = 0; i < noseLength; ++i) {
      const double d = 2.0 * (static_cast<double>(i) / noseLength);
      double dia = d < 1.0 ? 0.4 + 1.6 * d : 0.5 + 1.5 * (2.0 - d);
      noseD[i] = std::min(dia, 1.9);
    }
    reflections();
    noseReflections();
    noseD[0] = velum;
  }

  // Unit conversion: a target written in 44-section units, for this length.
  double u(double i44) const { return i44 * n / 44.0; }

  void shape(const Target& t) {
    // the tongue (Pink Trombone's setRestDiameter)
    const double ti = u(t.tongueIndex);
    const double fixedD = 2.0 + (t.tongueDiam - 2.0) / 1.5;
    for (int i = bladeStart; i < lipStart; ++i) {
      const double a = 1.1 * 3.141592653589793 * (ti - i) / (tipStart - bladeStart);
      double curve = (1.5 - fixedD + 1.7) * std::cos(a);
      if (i == bladeStart - 2 || i == lipStart - 1) curve *= 0.8;
      if (i == bladeStart || i == lipStart - 2) curve *= 0.94;
      rest[i] = 1.5 - curve;
    }
    for (int i = 0; i < n; ++i) target[i] = rest[i];
    velumTarget = t.velum;
    if (t.conIndex >= 0) constrict(u(t.conIndex), t.conDiam);
    if (t.lipRound >= 0) constrict(u(41.0), t.lipRound);
  }

  void constrict(double index, double dia) {
    const double width = index < u(25) ? 10.0 : index >= tipStart ? 5.0
                       : 10.0 - 5.0 * (index - u(25)) / (tipStart - u(25));
    for (int i = -static_cast<int>(std::ceil(width)) - 1; i < width + 1; ++i) {
      const int j = static_cast<int>(std::lround(index)) + i;
      if (j < 0 || j >= n) continue;
      double rel = std::abs(j - index) - 0.5;
      double shrink = rel <= 0 ? 0.0 : rel > width ? 1.0 : 0.5 * (1.0 - std::cos(3.141592653589793 * rel / width));
      if (dia < target[j]) target[j] = dia + (target[j] - dia) * shrink;
    }
  }

  // Move toward the target at a speed limit (Pink Trombone's reshapeTract).
  void reshape(double dt) {
    const double amount = dt * 15.0;
    int newLastObstruction = -1;
    for (int i = 0; i < n; ++i) {
      const double slow = i < noseStart ? 0.6 : i >= tipStart ? 1.0
                        : 0.6 + 0.4 * (i - noseStart) / (tipStart - noseStart);
      diameter[i] = towards(diameter[i], target[i], slow * amount, 2.0 * amount);
      if (diameter[i] <= 0) newLastObstruction = i;
    }
    lastObstruction = newLastObstruction;
    noseD[0] = towards(noseD[0], velumTarget, amount * 0.25, amount * 0.1);
    noseA[0] = noseD[0] * noseD[0];
  }

  void reflections() {
    for (int i = 0; i < n; ++i) A[i] = diameter[i] * diameter[i];
    for (int i = 1; i < n; ++i) {
      refl[i] = newRefl[i];
      newRefl[i] = A[i] == 0 ? 0.999 : (A[i - 1] - A[i]) / (A[i - 1] + A[i]);
    }
    reflLeft = newReflLeft; reflRight = newReflRight; reflNose = newReflNose;
    const double sum = A[noseStart] + A[noseStart + 1] + noseA[0];
    newReflLeft = (2.0 * A[noseStart] - sum) / sum;
    newReflRight = (2.0 * A[noseStart + 1] - sum) / sum;
    newReflNose = (2.0 * noseA[0] - sum) / sum;
  }
  void noseReflections() {
    for (int i = 0; i < noseLength; ++i) noseA[i] = noseD[i] * noseD[i];
    for (int i = 1; i < noseLength; ++i) noseRefl[i] = (noseA[i - 1] - noseA[i]) / (noseA[i - 1] + noseA[i]);
  }

  void turbulence(double noise, double index, double dia, double modulator) {
    const int i = static_cast<int>(std::floor(index));
    if (i < 2 || i + 2 >= n) return;
    const double delta = index - i;
    noise *= modulator;
    const double thin = std::clamp(8.0 * (0.7 - dia), 0.0, 1.0);
    const double open = std::clamp(30.0 * (dia - 0.3), 0.0, 1.0);
    const double n0 = noise * (1.0 - delta) * thin * open;
    const double n1 = noise * delta * thin * open;
    R[i + 1] += n0 / 2; L[i + 1] += n0 / 2;
    R[i + 2] += n1 / 2; L[i + 2] += n1 / 2;
  }

  double step(double glottal, double lambda) {
    // Pink Trombone adds a decaying push at a closure as it opens (its
    // "transient"). Here it pumped a section that was still nearly shut
    // and rang at the sample rate: the clicks James heard as "clippy"
    // (2026-11-14). The release's hiss burst gives a plosive its attack.
    jR[0] = L[0] * glottalReflection + glottal;
    jL[n] = R[n - 1] * lipReflection;
    for (int i = 1; i < n; ++i) {
      const double r = refl[i] * (1.0 - lambda) + newRefl[i] * lambda;
      const double w = r * (R[i - 1] + L[i]);
      jR[i] = R[i - 1] - w;
      jL[i] = L[i] + w;
    }
    const int i = noseStart;
    double r = newReflLeft * (1.0 - lambda) + reflLeft * lambda;
    jL[i] = r * R[i - 1] + (1.0 + r) * (noseL[0] + L[i]);
    r = newReflRight * (1.0 - lambda) + reflRight * lambda;
    jR[i] = r * L[i] + (1.0 + r) * (R[i - 1] + noseL[0]);
    r = newReflNose * (1.0 - lambda) + reflNose * lambda;
    nR[0] = r * noseL[0] + (1.0 + r) * (L[i] + R[i - 1]);
    for (int k = 0; k < n; ++k) {
      R[k] = jR[k] * 0.999;
      L[k] = jL[k + 1] * 0.999;
    }
    const double lip = R[n - 1];
    nL[noseLength] = noseR[noseLength - 1] * lipReflection;
    for (int k = 1; k < noseLength; ++k) {
      const double w = noseRefl[k] * (noseR[k - 1] + noseL[k]);
      nR[k] = noseR[k - 1] - w;
      nL[k] = noseL[k] + w;
    }
    for (int k = 0; k < noseLength; ++k) {
      noseR[k] = nR[k] * fade;
      noseL[k] = nL[k + 1] * fade;
    }
    return lip + noseR[noseLength - 1];
  }

  int n, bladeStart, tipStart, lipStart, noseLength, noseStart;
  std::vector<double> diameter, rest, target, R, L, refl, newRefl, jR, jL, A;
  std::vector<double> noseR, noseL, nR, nL, noseRefl, noseD, noseA;
  double reflLeft = 0, reflRight = 0, reflNose = 0, newReflLeft = 0, newReflRight = 0, newReflNose = 0;
  double glottalReflection = 0.75, lipReflection = -0.85, fade = 1.0;
  double velum = 0.01, velumTarget = 0.01;
  int lastObstruction = -1;

private:
  static double towards(double cur, double tgt, double up, double down) {
    return cur < tgt ? std::min(cur + up, tgt) : std::max(cur - down, tgt);
  }
};

// ── Phonemes ────────────────────────────────────────────────────────────────

inline Target vowel(double idx, double dia, double lips = -1.0, double ms = 95.0) {
  Target t;
  t.tongueIndex = idx; t.tongueDiam = dia; t.lipRound = lips; t.ms = ms; t.vowel = true;
  return t;
}

// Vowel positions, tuned by measuring the rendered formants (tools/check_voice.py).
// In this tongue model a SMALL diameter is a deep constriction at the index,
// which is what the back vowels need: a, o, ^ and U were searched for, not guessed.
inline Target vowelFor(const std::string& v) {
  if (v == "i")  return vowel(27.0, 2.10);           // see
  if (v == "I")  return vowel(25.5, 2.55);           // sit
  if (v == "e")  return vowel(24.5, 2.80);           // bed
  if (v == "ae") return vowel(22.0, 3.30);           // cat
  if (v == "a")  return vowel(17.0, 2.30);           // father
  if (v == "o")  return vowel(17.0, 2.30, 1.0);      // pot, go
  if (v == "u")  return vowel(13.0, 2.15, 0.55);     // boot
  if (v == "U")  return vowel(15.0, 2.30, 0.7);      // book
  if (v == "^")  return vowel(18.0, 2.30);           // cup
  return vowel(20.0, 2.75, -1.0, 70.0);              // schwa
}

inline Target consonant(double place, double dia, bool voiced, double hiss, double ms) {
  Target t;
  t.conIndex = place; t.conDiam = dia; t.voice = voiced ? 0.75 : 0.0; t.hiss = hiss; t.ms = ms;
  t.tongueIndex = 21.0; t.tongueDiam = 2.75;
  return t;
}

constexpr double kLips = 41.0, kTeeth = 39.5, kRidge = 36.5, kPost = 33.5, kVelar = 22.5;

// A plosive is a closure, then the release with a burst of noise.
inline void plosive(std::vector<Target>& out, double place, bool voiced) {
  Target closed = consonant(place, 0.0, voiced, 0.0, voiced ? 55 : 65);
  closed.voice = voiced ? 0.45 : 0.0;
  out.push_back(closed);
  Target burst = consonant(place, 0.45, voiced, voiced ? 0.6 : 1.0, voiced ? 18 : 30);
  if (!voiced) burst.tenseness = 0.1;
  out.push_back(burst);
}

inline void fricative(std::vector<Target>& out, double place, double dia, bool voiced, double ms = 90) {
  out.push_back(consonant(place, dia, voiced, 1.0, ms));
}

inline void nasal(std::vector<Target>& out, double place) {
  Target t = consonant(place, 0.0, true, 0.0, 70);
  t.voice = 0.85; t.velum = 0.4;
  out.push_back(t);
}

inline void approximant(std::vector<Target>& out, Target t, double ms = 55) {
  t.vowel = false; t.ms = ms;
  out.push_back(t);
}

// Spelling to phonemes. Rough on purpose: the lesson voices are cartoons.
// Returns phonemes with a word gap after each word and phrase marks applied.
inline std::vector<Target> phonemes(const std::string& text, double speed) {
  std::vector<Target> out;
  auto isV = [](char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; };
  std::vector<std::pair<std::string, bool>> words;   // word, said loudly (CAPITALS)
  std::vector<char> after;                            // the punctuation after each word
  std::string w;
  bool caps = true, letters = false;
  auto flush = [&](char p) {
    if (!w.empty()) {
      words.push_back({w, caps && w.size() > 1 && letters});
      after.push_back(p);
    } else if (!after.empty() && p != ' ') {
      after.back() = p;
    }
    w.clear(); caps = true; letters = false;
  };
  for (char ch : text) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isalpha(c)) {
      if (std::islower(c)) caps = false;
      letters = true;
      w += static_cast<char>(std::tolower(c));
    } else if (ch == '\'') {
      // keep contractions joined
    } else {
      flush(ch == '.' || ch == '!' || ch == '?' || ch == ',' || ch == ';' || ch == ':' ? ch : ' ');
    }
  }
  flush(' ');

  for (std::size_t wi = 0; wi < words.size(); ++wi) {
    const std::string& s = words[wi].first;
    const std::size_t start = out.size();
    // a silent final e
    std::size_t len = s.size();
    if (len > 2 && s[len - 1] == 'e' && !isV(s[len - 2])) --len;
    for (std::size_t i = 0; i < len; ++i) {
      const char c = s[i];
      const char nx = i + 1 < len ? s[i + 1] : 0;
      const std::string two = s.substr(i, 2);
      if (two == "sh") { fricative(out, kPost, 0.55, false); ++i; continue; }
      if (two == "ch") { plosive(out, kRidge, false); fricative(out, kPost, 0.55, false, 60); ++i; continue; }
      if (two == "th") { fricative(out, kTeeth, 0.5, false, 70); ++i; continue; }
      if (two == "ng") { nasal(out, kVelar); ++i; continue; }
      if (two == "ph") { fricative(out, kLips, 0.5, false); ++i; continue; }
      if (two == "ck") { plosive(out, kVelar, false); ++i; continue; }
      if (two == "qu") { plosive(out, kVelar, false); approximant(out, vowelFor("u")); ++i; continue; }
      if (two == "wh") { approximant(out, vowelFor("u")); ++i; continue; }
      if (two == "ee" || two == "ea") { out.push_back(vowelFor("i")); out.back().ms = 120; ++i; continue; }
      if (two == "oo") { out.push_back(vowelFor("u")); out.back().ms = 120; ++i; continue; }
      if (two == "ou" || two == "ow") { out.push_back(vowelFor("a")); out.push_back(vowelFor("U")); out.back().ms = 70; ++i; continue; }
      if (two == "ai" || two == "ay") { out.push_back(vowelFor("e")); out.push_back(vowelFor("I")); out.back().ms = 60; ++i; continue; }
      if (two == "oi" || two == "oy") { out.push_back(vowelFor("o")); out.push_back(vowelFor("I")); out.back().ms = 60; ++i; continue; }
      if (two == "ie" || two == "igh") { out.push_back(vowelFor("a")); out.push_back(vowelFor("I")); out.back().ms = 60; ++i; continue; }
      if (c == nx && !isV(c)) continue;   // doubled consonants say once
      switch (c) {
        case 'a': out.push_back(vowelFor(len < s.size() ? "e" : "ae")); break;   // make / cat
        case 'e': out.push_back(vowelFor(i + 1 == len && len <= 3 ? "i" : "e")); break;   // me / bed
        case 'i': out.push_back(vowelFor(len < s.size() ? "a" : "I")); if (len < s.size()) out.push_back(vowelFor("I")); break;
        case 'o': out.push_back(vowelFor(i + 1 == len || len < s.size() ? "o" : "o")); break;
        case 'u': out.push_back(vowelFor(len < s.size() ? "u" : "^")); break;
        case 'y': if (i == 0) approximant(out, vowelFor("i")); else out.push_back(vowelFor("i")); break;
        case 'w': approximant(out, vowelFor("u")); break;
        case 'b': plosive(out, kLips, true); break;
        case 'p': plosive(out, kLips, false); break;
        case 'd': plosive(out, kRidge, true); break;
        case 't': plosive(out, kRidge, false); break;
        case 'g': plosive(out, kVelar, true); break;
        case 'k': plosive(out, kVelar, false); break;
        case 'c': if (nx == 'e' || nx == 'i' || nx == 'y') fricative(out, kRidge, 0.42, false); else plosive(out, kVelar, false); break;
        case 'q': plosive(out, kVelar, false); break;
        case 'x': plosive(out, kVelar, false); fricative(out, kRidge, 0.42, false, 70); break;
        case 'j': plosive(out, kRidge, true); fricative(out, kPost, 0.55, true, 50); break;
        case 'm': nasal(out, kLips); break;
        case 'n': nasal(out, kRidge); break;
        case 's': fricative(out, kRidge, 0.42, i > 0 && i + 1 == len && !isV(s[i - 1]) && s[i - 1] != 't' && s[i - 1] != 'k' && s[i - 1] != 'p'); break;
        case 'z': fricative(out, kRidge, 0.42, true); break;
        case 'f': fricative(out, kLips, 0.5, false); break;
        case 'v': fricative(out, kLips, 0.5, true); break;
        case 'h': { Target t = consonant(-1, 0, true, 0, 60); t.conIndex = -1; t.voice = 0.6; t.tenseness = 0.05; out.push_back(t); break; }
        case 'l': { Target t = consonant(kRidge, 1.0, true, 0, 60); t.voice = 0.9; out.push_back(t); break; }
        case 'r': { Target t = consonant(31.5, 1.1, true, 0, 60); t.voice = 0.9; t.lipRound = 1.2; out.push_back(t); break; }
        default: break;
      }
    }
    // Phrase marks: louder words, the pitch line, the gap after.
    const bool loud = words[wi].second;
    bool firstVowel = true;
    for (std::size_t k = start; k < out.size(); ++k) {
      if (out[k].vowel && firstVowel) { out[k].pitch += loud ? 0.3 : 0.12; out[k].ms *= loud ? 1.4 : 1.15; firstVowel = false; }
    }
    const char p = after[wi];
    const double gap = p == '.' || p == '!' || p == '?' ? 260 : p == ',' || p == ';' || p == ':' ? 140 : 25;
    if (p == '?') {
      for (std::size_t k = start; k < out.size(); ++k) if (out[k].vowel) out[k].pitch += 0.35;
    }
    Target rest;
    rest.voice = 0.0; rest.ms = gap; rest.pitch = -1000;   // -1000 marks a phrase end for the pitch line
    if (p == ' ' || p == 0) rest.pitch = 0.0;
    out.push_back(rest);
  }
  for (Target& t : out) t.ms /= std::max(0.3, speed);
  return out;
}

}  // namespace detail

// Mouth openness 0..1 for each 1/60 s, for a face that moves with the voice.
struct Spoken {
  std::vector<float> samples;   // mono
  std::vector<float> mouth;     // one per 1/60 s
};

inline Spoken speak(const std::string& text, const VoiceParams& vp, int rate = 48000) {
  using namespace detail;
  Spoken out;
  std::vector<Target> ph = phonemes(text, vp.speed);
  if (ph.empty()) return out;

  // The pitch line: each phrase starts high and falls; a hashed wobble per
  // vowel keeps it from sounding like a metronome.
  {
    double phraseStart = 0; std::size_t from = 0;
    std::uint32_t h = 2166136261u;
    for (char c : text) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
    for (std::size_t i = 0; i <= ph.size(); ++i) {
      const bool end = i == ph.size() || ph[i].pitch <= -999;
      if (!end) continue;
      const std::size_t count = i - from;
      for (std::size_t k = from; k < i; ++k) {
        const double pos = count > 1 ? static_cast<double>(k - from) / (count - 1) : 0.0;
        h = h * 1664525u + 1013904223u;
        const double wob = ((h >> 8) % 1000) / 1000.0 - 0.5;
        if (ph[k].vowel) ph[k].pitch += vp.melody * (0.35 - 0.6 * pos + 0.5 * wob);
      }
      if (i < ph.size()) ph[i].pitch = 0.0;
      from = i + 1;
      (void)phraseStart;
    }
  }

  double total = 0;
  for (const Target& t : ph) total += t.ms;
  const int samples = static_cast<int>((total + 250.0) * rate / 1000.0);
  out.samples.assign(samples, 0.0f);

  Glottis glottis(rate);
  Tract tract(vp.tractScale);
  Rng rng;
  BandPass aspF, fricF;
  aspF.set(500.0, 0.5, rate);
  fricF.set(1000.0, 0.5, rate);

  const int block = 256;
  const double dt = static_cast<double>(block) / rate;
  std::size_t seg = 0;
  double segLeftMs = ph[0].ms;
  // Smoothed controls.
  double voice = 0.0, hiss = 0.0, tense = vp.tenseness, pitchOct = 0.0;
  double vibPhase = 0.0;
  double peak = 1e-9;
  std::vector<double> raw(samples, 0.0);
  int mouthEvery = rate / 60, mouthCount = 0;

  for (int b = 0; b < samples; b += block) {
    // advance the phoneme clock
    double elapsed = 1000.0 * dt;
    while (seg < ph.size() && elapsed > 0) {
      const double take = std::min(elapsed, segLeftMs);
      segLeftMs -= take; elapsed -= take;
      if (segLeftMs <= 0 && seg + 1 < ph.size()) { ++seg; segLeftMs = ph[seg].ms; }
      else if (segLeftMs <= 0) { seg = ph.size(); }
    }
    Target cur = seg < ph.size() ? ph[seg] : Target {};
    if (seg >= ph.size()) cur.voice = 0.0;
    // A rest keeps the mouth where it was; only the voice stops.
    if (seg < ph.size() && !cur.vowel && cur.conIndex < 0 && cur.voice == 0.0) {
      cur.tongueIndex = 20.0; cur.tongueDiam = 2.9;
    }
    tract.shape(cur);

    const double k = 1.0 - std::exp(-dt / 0.012);
    voice += (cur.voice - voice) * k;
    hiss += (cur.hiss - hiss) * (1.0 - std::exp(-dt / 0.008));
    const double wantTense = cur.tenseness >= 0 ? cur.tenseness : vp.tenseness;
    tense += (wantTense - tense) * k;
    if (cur.vowel) pitchOct += (cur.pitch - pitchOct) * (1.0 - std::exp(-dt / (0.03 + 0.12 * vp.swoop)));

    vibPhase += dt * 6.0;
    const double vib = 1.0 + vp.vibrato * std::sin(2.0 * 3.141592653589793 * vibPhase)
                     + 0.004 * rng.next();
    glottis.oldFrequency = glottis.newFrequency;
    glottis.newFrequency = vp.pitchHz * std::pow(2.0, pitchOct) * vib;
    glottis.oldTenseness = glottis.newTenseness;
    glottis.newTenseness = std::clamp(tense, 0.0, 1.0);
    glottis.intensity = std::clamp(voice, 0.0, 1.0);

    tract.reshape(dt);
    tract.reflections();
    tract.noseReflections();

    const int end = std::min(samples, b + block);
    for (int i = b; i < end; ++i) {
      const double lambda1 = static_cast<double>(i - b) / block;
      const double lambda2 = (i - b + 0.5) / block;
      const double asp = aspF.run(rng.next());
      const double fric = fricF.run(rng.next());
      const double g = glottis.step(lambda1, asp);
      double v = 0.0;
      for (int pass = 0; pass < 2; ++pass) {
        if (hiss > 0.001 && cur.conIndex >= 0) {
          // Gated by how open the tract REALLY is there, not the target:
          // the tract opens at a speed limit, and noise poured into a section
          // that is still shut rang at the sample rate (the "clippy" clicks,
          // James 2026-11-14), as Pink Trombone's own touch-based gate avoids.
          const double at = tract.u(cur.conIndex);
          const int ci = std::clamp(static_cast<int>(at), 1, tract.n - 2);
          const double real = std::min({tract.diameter[ci - 1], tract.diameter[ci], tract.diameter[ci + 1]});
          tract.turbulence(1.3 * fric * hiss, at, real, glottis.noiseModulator());
        }
        v += tract.step(g, pass == 0 ? lambda1 : lambda2);
      }
      raw[i] = v * 0.125;
      peak = std::max(peak, std::abs(raw[i]));
      if (++mouthCount >= mouthEvery) {
        mouthCount = 0;
        double open = 0;
        for (int s = tract.lipStart; s < tract.n; ++s) open += tract.diameter[s];
        open /= std::max(1, tract.n - tract.lipStart);
        out.mouth.push_back(static_cast<float>(std::clamp(open / 1.5 * std::min(1.0, voice * 2.0 + hiss), 0.0, 1.0)));
      }
    }
  }
  // Level: set by the LOUD part of the speech (the 90th percentile of 20 ms
  // windows), not the peak -- a single click would otherwise set it. Then a
  // soft limit, and a short fade at each end.
  (void)peak;
  std::vector<double> windows;
  const int win = rate / 50;
  for (int i = 0; i + win <= samples; i += win) {
    double e = 0;
    for (int k = i; k < i + win; ++k) e += raw[k] * raw[k];
    windows.push_back(std::sqrt(e / win));
  }
  double loud = 1e-9;
  if (!windows.empty()) {
    std::sort(windows.begin(), windows.end());
    loud = std::max(1e-9, windows[windows.size() * 9 / 10]);
  }
  const double gain = 0.25 * vp.level / loud;
  for (int i = 0; i < samples; ++i) {
    double f = 1.0;
    if (i < rate / 200) f = static_cast<double>(i) / (rate / 200);
    if (samples - i < rate / 50) f = static_cast<double>(samples - i) / (rate / 50);
    out.samples[i] = static_cast<float>(std::tanh(raw[i] * gain) * f * 0.95);
  }
  return out;
}

// One mouth shape held, for measuring the formants.
inline std::vector<float> holdShape(const Target& t, const VoiceParams& vp, double seconds, int rate = 48000) {
  using namespace detail;
  Glottis glottis(rate);
  Tract tract(vp.tractScale);
  Rng rng;
  BandPass aspF; aspF.set(500.0, 0.5, rate);
  const int samples = static_cast<int>(seconds * rate);
  std::vector<float> out(samples, 0.0f);
  const int block = 256;
  const double dt = static_cast<double>(block) / rate;
  glottis.newFrequency = glottis.oldFrequency = vp.pitchHz;
  glottis.newTenseness = glottis.oldTenseness = vp.tenseness;
  glottis.intensity = 1.0;
  double peak = 1e-9;
  for (int b = 0; b < samples; b += block) {
    tract.shape(t);
    tract.reshape(dt);
    tract.reflections();
    tract.noseReflections();
    for (int i = b; i < std::min(samples, b + block); ++i) {
      const double l = static_cast<double>(i - b) / block;
      const double g = glottis.step(l, aspF.run(rng.next()));
      double s = tract.step(g, l) + tract.step(g, (i - b + 0.5) / block);
      out[i] = static_cast<float>(s * 0.125);
      peak = std::max(peak, std::abs(s * 0.125));
    }
  }
  for (float& s : out) s = static_cast<float>(s * 0.9 / peak);
  return out;
}

inline std::vector<float> holdVowel(const std::string& v, const VoiceParams& vp, double seconds, int rate = 48000) {
  return holdShape(detail::vowelFor(v), vp, seconds, rate);
}

// THE CAST: a tract voice for each lesson character (the chip voices are in
// app_tutorial.ipp). Pitch and tract length follow the chip casting.
inline VoiceParams castVoice(const std::string& name) {
  VoiceParams v;
  if (name == "DR. PIXEL")              { v.pitchHz = 230; v.tractScale = 0.86; v.tenseness = 0.7; v.melody = 0.35; v.swoop = 0.4; v.speed = 1.1; }
  else if (name == "PROF. VIDEO")       { v.pitchHz = 118; v.tractScale = 1.05; v.tenseness = 0.6; v.melody = 0.25; v.speed = 0.95; }
  else if (name == "COACH MARV")        { v.pitchHz = 98;  v.tractScale = 1.12; v.tenseness = 0.85; v.melody = 0.3; v.speed = 1.15; }
  else if (name == "JUNE O'NEIL")       { v.pitchHz = 215; v.tractScale = 0.88; v.tenseness = 0.65; v.melody = 0.3; v.speed = 1.05; }
  else if (name == "TURNIP TOP")        { v.pitchHz = 180; v.tractScale = 0.92; v.tenseness = 0.5; v.melody = 0.45; v.swoop = 0.6; }
  else if (name == "CHEF BORK")         { v.pitchHz = 140; v.tractScale = 1.0; v.tenseness = 0.75; v.melody = 0.6; v.swoop = 0.9; v.speed = 1.1; }
  else if (name == "DONNA WHEEL")       { v.pitchHz = 240; v.tractScale = 0.84; v.tenseness = 0.7; v.melody = 0.35; v.speed = 1.25; }
  else if (name == "KEEPER ROOK")       { v.pitchHz = 78;  v.tractScale = 1.2; v.tenseness = 0.45; v.melody = 0.15; v.speed = 0.8; }
  else if (name == "AGENT BRANDT")      { v.pitchHz = 110; v.tractScale = 1.08; v.tenseness = 0.8; v.melody = 0.08; v.vibrato = 0.0; }
  else if (name == "AGENT HOLLIS")      { v.pitchHz = 135; v.tractScale = 1.02; v.tenseness = 0.75; v.melody = 0.12; v.vibrato = 0.0; }
  else if (name == "MR. SALTY")         { v.pitchHz = 160; v.tractScale = 0.98; v.tenseness = 0.35; v.melody = 0.4; v.swoop = 0.5; v.speed = 0.85; }
  else if (name == "OPTIMUS PRIMETIME") { v.pitchHz = 70;  v.tractScale = 1.25; v.tenseness = 0.9; v.melody = 0.05; v.vibrato = 0.0; v.speed = 0.75; }
  else if (name == "WALLIS")            { v.pitchHz = 165; v.tractScale = 0.95; v.tenseness = 0.6; v.melody = 0.3; }
  else if (name == "CAROLINE POLKADOT") { v.pitchHz = 260; v.tractScale = 0.82; v.tenseness = 0.4; v.melody = 0.7; v.swoop = 1.0; v.speed = 0.8; v.vibrato = 0.02; }
  else if (name == "LT. HAILA")         { v.pitchHz = 200; v.tractScale = 0.9; v.tenseness = 0.7; v.melody = 0.25; }
  else if (name == "ENSIGN JINKLEY")    { v.pitchHz = 185; v.tractScale = 0.9; v.tenseness = 0.65; v.melody = 0.5; v.speed = 1.4; }
  else {
    unsigned h = 2166136261u;
    for (char ch : name) h = (h ^ static_cast<unsigned char>(ch)) * 16777619u;
    v.pitchHz = 95.0 * std::pow(2.0, (h % 24) / 12.0);
    v.tractScale = 1.15 - 0.3 * ((h >> 5) % 10) / 10.0;
    v.melody = 0.2 + 0.3 * ((h >> 9) % 10) / 10.0;
  }
  return v;
}

}  // namespace deckboy::voice
