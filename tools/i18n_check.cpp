// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.
//
// deckboy-i18n-check: the rules translate() uses for a string the code builds
// at run time -- numbers, "Label: value", columns, a name and its qualifier --
// and format(). Runs against a catalogue it writes itself, so it tests the
// rules and not whatever the real catalogues happen to hold.
#include "core/i18n.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace i18n = deckboy::core::i18n;

namespace {

int failures = 0;

void expect(const std::string& source, const std::string& want) {
  const std::string got = i18n::translate(source);
  if (got != want) {
    std::printf("FAIL  \"%s\"\n      want \"%s\"\n      got  \"%s\"\n", source.c_str(), want.c_str(),
                got.c_str());
    ++failures;
  } else {
    std::printf("ok    \"%s\" -> \"%s\"\n", source.c_str(), got.c_str());
  }
}

}  // namespace

int main() {
  std::error_code ec;
  const std::filesystem::path root = std::filesystem::temp_directory_path(ec) / "deckboy-i18n-check";
  std::filesystem::create_directories(root / "lang", ec);
  {
    std::ofstream out(root / "lang" / "xx.tsv", std::ios::binary);
    out << "#name Test\n"
           "Deck {n}\tDECK-{n}\n"
           "{n} of {n}\t{2}/{1}\n"
           "Bitrate\tRATE\n"
           "not running\tOFF-RUN\n"
           "to Out {n}\tTO-OUT-{n}\n"
           "Source\tSRC\n"
           "FADE {n} s\tFONDU {n} s\n"
           "Full White\tBLANC\n"
           "motion\tMOUVEMENT\n"
           "New media joins {1} by itself\t{1} PREND LES MEDIAS\n"
           "Apply\tAPPLIQUER\n";
  }
  std::string error;
  if (!i18n::setLanguage("xx", root, error)) {
    std::printf("FAIL  could not load the test catalogue: %s\n", error.c_str());
    return 1;
  }

  // A whole line, and the same line in another case, still come first.
  expect("Apply", "APPLIQUER");
  expect("APPLY", "APPLIQUER");
  // Numbers: in order, reordered, decimals and times as one number.
  expect("Deck 2", "DECK-2");
  expect("Deck 12", "DECK-12");
  expect("1 of 2", "2/1");
  expect("FADE 2.0 s", "FONDU 2.0 s");
  // A label and its value: the value is kept as it is unless a line says otherwise.
  expect("Bitrate: 6000 kbps", "RATE: 6000 kbps");
  expect("PTP: not running", "PTP: OFF-RUN");
  expect("Source: C:\\shows\\recordings", "SRC: C:\\shows\\recordings");
  // Columns keep their spacing.
  expect("Deck 1  to Out 1", "DECK-1  TO-OUT-1");
  expect("Deck 3   |   to Out 2", "DECK-3   |   TO-OUT-2");
  // A name and its qualifier, now through the other rules as well.
  expect("Full White (motion)", "BLANC (MOUVEMENT)");
  expect("PTP: not running (local clock)", "PTP: OFF-RUN (local clock)");
  expect("Full White  (motion)", "BLANC  (MOUVEMENT)");
  // Things that must come through untouched.
  expect("H.264 / MP4", "H.264 / MP4");
  expect("VG279QM  3840x2160", "VG279QM  3840x2160");
  expect("nothing to see here", "nothing to see here");
  expect("Deck", "Deck");
  // Asked twice: the remembered answer is the same answer.
  expect("Deck 2", "DECK-2");
  // format(): the template is translated, the name goes in as given.
  {
    const std::string got = i18n::format("New media joins {1} by itself", {"Drums"});
    if (got != "Drums PREND LES MEDIAS") {
      std::printf("FAIL  format: got \"%s\"\n", got.c_str());
      ++failures;
    } else {
      std::printf("ok    format -> \"%s\"\n", got.c_str());
    }
  }
  // Back to English: everything passes through, and nothing is remembered from before.
  i18n::setLanguage("en", root, error);
  expect("Deck 2", "Deck 2");
  {
    const std::string got = i18n::format("New media joins {1} by itself", {"Drums"});
    if (got != "New media joins Drums by itself") {
      std::printf("FAIL  English format: got \"%s\"\n", got.c_str());
      ++failures;
    }
  }

  std::filesystem::remove_all(root, ec);
  std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "passed", failures);
  return failures ? 1 : 0;
}
