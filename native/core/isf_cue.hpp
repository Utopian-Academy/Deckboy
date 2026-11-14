// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#pragma once
// What a SHADER cue (path "pattern://isf:<file>") keeps for its shader's
// inputs, and how those become the numbers the card is given.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../engine/isf.hpp"
#include "types.hpp"

namespace deckboy::isf {

inline const IsfSetting* findSetting(const Cue& cue, const std::string& name) {
  for (const auto& s : cue.isf)
    if (s.name == name) return &s;
  return nullptr;
}

// The cue's setting for `input`, made at the shader's default when it has none.
inline IsfSetting& ensureSetting(Cue& cue, const Input& input) {
  for (auto& s : cue.isf)
    if (s.name == input.name) return s;
  IsfSetting made;
  made.name = input.name;
  for (int c = 0; c < 4; ++c) made.v[c] = input.def[c];
  cue.isf.push_back(made);
  return cue.isf.back();
}

// The value's worth in the input's own units, and where the oscillator moves
// it. Only a float input has an oscillator.
inline float modulated(const Input& input, const IsfSetting& set, double seconds, double beats, float audio01) {
  float v = set.v[0];
  if (input.type == InputType::Float && set.lfo.on) {
    const float span = input.mx[0] - input.mn[0];
    if (span > 0.0f) {
      const float unit = std::clamp((v - input.mn[0]) / span, 0.0f, 1.0f);
      v = input.mn[0] + span * deckboy::effects::lfoApply(set.lfo, unit, seconds, beats, audio01);
    }
  }
  return v;
}

// Four floats per input, in the shader's order: the cue's values (the shader's
// own defaults where it has none), floats moved by their oscillators.
inline void cueValues(const Cue& cue, const Shader& shader, std::vector<float>& out) {
  defaultsFor(shader, out);
  const auto clock = deckboy::effects::readLfoClock();
  for (std::size_t i = 0; i < shader.inputs.size(); ++i) {
    const Input& in = shader.inputs[i];
    const IsfSetting* set = findSetting(cue, in.name);
    if (!set) continue;
    for (int c = 0; c < 4; ++c) out[i * 4 + static_cast<std::size_t>(c)] = set->v[c];
    out[i * 4] = modulated(in, *set, clock.seconds, clock.beats, clock.audio01);
  }
}

}  // namespace deckboy::isf
