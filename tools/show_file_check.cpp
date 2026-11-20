// SPDX-License-Identifier: GPL-3.0-or-later
// Links the public codec without main.cpp. No windows, audio or media decode.
#include "core/show_file.hpp"
#include "core/fs.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs = dbfs;
static void require(bool ok, const std::string& what) {
  if (!ok) throw std::runtime_error(what);
}
static std::string bytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  require(bool(in), "read " + path.string());
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static Project roundTrip(const fs::path& first, const fs::path& second) {
  Project loaded = deckboy::showfile::load(first.string());
  require(deckboy::showfile::save(second.string(), loaded), "save " + second.string());
  const auto a = bytes(first), b = bytes(second);
  require(a == b, "byte mismatch: " + first.string() + " -> " + second.string());
  std::cout << first.filename().string() << ": " << a.size() << " bytes, identical\n";
  return loaded;
}
int main(int argc, char** argv) {
  try {
    require(argc >= 2 && argc <= 3, "usage: show-file-check OUTPUT_DIR [DESK_SHOW]");
    const fs::path dir(argv[1]);
    fs::create_directories(dir);
    Project project;
    project.decks.resize(2);
    for (int d = 0; d < 2; ++d) {
      for (const auto kind : {CueKind::Video, CueKind::Pattern, CueKind::Text}) {
        Cue cue;
        cue.kind = kind;
        cue.name = "Fixture";
        cue.path = kind == CueKind::Video ? "media/clip.mp4" :
                   kind == CueKind::Pattern ? "pattern://smpte-bars" : "text://fixture";
        project.decks[d].cues.push_back(cue);
      }
    }
    // As the desk does before editing, give new decks/cues their generated
    // identities and normalized defaults. Test values are applied AFTER this.
    const auto seed = dir / "seed.deckboy";
    require(deckboy::showfile::save(seed.string(), project), "seed save");
    project = deckboy::showfile::load(seed.string());
    project.title = "Codec\tfixture\\with\nUnicode \xc3\xa9";
    for (auto& deck : project.decks) {
      for (auto& cue : deck.cues) {
        cue.duration = 12.0;
        cue.inPointSeconds = cue.kind == CueKind::Video ? 1.25 : 0.0;
        cue.outPointSeconds = cue.kind == CueKind::Video ? 8.5 : 0.0;
        cue.fadeInSeconds = 0.75;
        cue.fadeOutSeconds = 1.5;
        cue.outputScaleX = 1.25f;
        cue.outputRotationDegrees = 27.0f;
        cue.cropLeft = 0.125f;
        cue.flipHorizontal = true;
        cue.flipVertical = true;
        cue.geometryLfo[0].on = true;
        cue.geometryLfo[0].rateHz = 0.75f;
        cue.geometryLfo[0].depth = 0.25f;
        deckboy::effects::CueEffect fx;
        fx.kind = deckboy::effects::CueEffectKind::Blur;
        fx.amount = 0.375f;
        cue.effects = {fx};
        cue.textBody = "First\tline\nSecond \\ line";
      }
    }
    const auto first = dir / "synthetic.deckboy";
    require(deckboy::showfile::save(first.string(), project), "synthetic save");
    const auto loaded = roundTrip(first, dir / "synthetic-again.deckboy");
    require(loaded.title == project.title && loaded.decks.size() == 2, "title/decks preserved");
    for (std::size_t d = 0; d < 2; ++d) {
      require(loaded.decks[d].cues.size() == 3, "three cue kinds preserved");
      for (std::size_t c = 0; c < 3; ++c) {
        const auto& cue = loaded.decks[d].cues[c];
        require(cue.kind == project.decks[d].cues[c].kind &&
                cue.inPointSeconds == (cue.kind == CueKind::Video ? 1.25 : 0.0) &&
                cue.outPointSeconds == (cue.kind == CueKind::Video ? 8.5 : 0.0) &&
                cue.fadeInSeconds == 0.75 && cue.fadeOutSeconds == 1.5 &&
                cue.outputScaleX == 1.25f && cue.outputRotationDegrees == 27.0f &&
                cue.cropLeft == 0.125f && cue.flipHorizontal && cue.flipVertical &&
                cue.geometryLfo[0].on && cue.geometryLfo[0].rateHz == 0.75f &&
                cue.geometryLfo[0].depth == 0.25f && cue.effects.size() == 1 &&
                cue.effects[0].kind == deckboy::effects::CueEffectKind::Blur &&
                cue.effects[0].amount == 0.375f && cue.textBody == project.decks[d].cues[c].textBody,
                "non-default fields preserved");
      }
    }
    std::cout << "2 decks, 6 cues, 3 kinds: non-default fields preserved\n";
    if (argc == 3) roundTrip(fs::path(argv[2]), dir / "desk-again.deckboy");
    std::cout << "show-file failures: 0\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "show-file failure: " << e.what() << '\n';
    return 1;
  }
}
