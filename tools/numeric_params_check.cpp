// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/numeric_params.hpp"
#include <iostream>
#include <set>
using namespace deckboy::params;
namespace {
int failures = 0, rowsChecked = 0, choicesChecked = 0, cuesChecked = 0;
void require(bool ok, const std::string& message) {
  if (!ok) { ++failures; std::cerr << message << '\n'; }
}
std::shared_ptr<const deckboy::isf::Shader> fixture(const std::string&) {
  static auto shader = [] {
    auto result = std::make_shared<deckboy::isf::Shader>();
    deckboy::isf::Input scalar;
    scalar.name = "rate"; scalar.label = "Rate"; scalar.mn[0] = -2; scalar.mx[0] = 3;
    result->inputs.push_back(scalar);
    auto point = scalar; point.name = "point"; point.label = "Point";
    point.type = deckboy::isf::InputType::Point2D; point.comps = 2;
    point.mn[1] = -10; point.mx[1] = 10; result->inputs.push_back(point);
    auto boolean = scalar; boolean.name = "toggle"; boolean.type = deckboy::isf::InputType::Bool;
    result->inputs.push_back(boolean);
    auto choice = scalar; choice.name = "choice"; choice.type = deckboy::isf::InputType::Long;
    choice.values = {-8, 3, 12}; choice.labels = {"one", "two", "three"}; result->inputs.push_back(choice);
    return result;
  }();
  return shader;
}
void check(Cue cue, bool expectRows = true) {
  ++cuesChecked;
  const auto rows = paramRowsForCue(cue);
  require(!expectRows || !rows.empty(), "missing rows: " + cue.path);
  std::set<int> ids;
  for (const auto& row : rows) {
    ++rowsChecked;
    require(ids.insert(static_cast<int>(row.id)).second, "duplicate row");
    require(row.key && *row.key && row.tip && *row.tip, "empty row text");
    const auto* spec = numericParamSpec(row.id, &cue);
    require(spec != nullptr, "missing spec: " + std::string(row.key));
    if (!spec) continue;
    const auto bounds = *spec;
    double value = 0;
    require(readNumericParam(cue, row.id, value), "unreadable: " + std::string(row.key));
    for (double bound : {bounds.lo, bounds.hi}) {
      Cue copy = cue;
      writeNumericParam(copy, row.id, bound);
      require(readNumericParam(copy, row.id, value), "unreadable after write");
      require(std::abs(value - bound) <= std::max(1e-5, std::pow(10.0, -bounds.precision) * 0.5),
              "boundary mismatch: " + std::string(row.key) + " " + std::to_string(bound) + " got " + std::to_string(value));
    }
  }
  for (const auto& choice : choiceRowsForCue(cue)) {
    ++choicesChecked;
    require(!choice.label.empty() && !choice.options.empty(), "empty choice");
    for (int i = 0; i < static_cast<int>(choice.options.size()); ++i) {
      Cue copy = cue; choice.set(copy, i);
      require(choice.get(copy) == i, "choice roundtrip: " + choice.label);
    }
  }
}
}
int main() {
  setShaderLookup(fixture);
  Cue cue;
  cue.kind = CueKind::VideoSynth; check(cue);
  cue.videoSynth.spriteSheetPath = "fixture.png"; cue.videoSynth.spriteRotate = 5; check(cue);
  cue.kind = CueKind::Text; cue.lowerThird.on = true; check(cue);
  cue.kind = CueKind::Timer; check(cue); cue.timer.logoPath = "logo.png"; check(cue);
  cue.kind = CueKind::Tone;
  for (int i = 0; i < kToneWaveformCount; ++i) {
    cue.tone.waveform = static_cast<ToneWaveform>(i); check(cue);
    if (cue.tone.waveform == ToneWaveform::Voice) { cue.tone.voiceCharacter = "CUSTOM"; check(cue); }
    if (cue.tone.waveform == ToneWaveform::Fds) { cue.tone.synth.chip = SynthChip::Nes; check(cue); }
  }
  cue.kind = CueKind::Pattern;
  for (const char* type : {"swirl", "moire-field", "lumia", "illusion", "fireside", "portal", "panel-map", "panel-map-motion", "code", "code-motion", "isf:fixture.fs"}) {
    cue.path = std::string("pattern://") + type;
    check(cue);
  }
  cue.path = "pattern://draw";
  cue.drawing.playback = deckboy::draw::Playback::Flipbook; check(cue);
  cue.drawing.playback = deckboy::draw::Playback::Replay; check(cue);
  cue.path = "pattern://illusion";
  for (int shape = 0; shape < kIllusionShapeCount; ++shape) for (int palette = 0; palette < kIllusionPaletteCount; ++palette) {
    cue.illusion.shape = shape; cue.illusion.palette = palette; check(cue);
  }
  cue.path = "pattern://moire-field";
  for (int palette = 0; palette < kMoirePaletteCount; ++palette) { cue.moire.palette = palette; check(cue); }
  cue.path = "pattern://lumia";
  for (int mode = 0; mode < kLumiaModeCount; ++mode) { cue.lumia.mode = mode; check(cue); }
  cue.path = "pattern://smpte-bars"; check(cue, false);
  require(paramRowsForCue(cue).empty(), "unrelated pattern exposes controls");
  double value = 0;
  require(!readNumericParam(cue, NumericParam::SwirlSpeed, value), "stale source row accepted");
  std::cout << "numeric params: " << cuesChecked << " cues, " << rowsChecked << " rows, " << choicesChecked << " choices, failures: " << failures << '\n';
  return failures ? 1 : 0;
}
