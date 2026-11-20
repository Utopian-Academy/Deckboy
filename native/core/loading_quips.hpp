// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// What the loading screens say while the machine works: the desk's loading
// overlay and Pocket's splash share one list.
//
// Short, warm lines so a wait reads as the machine doing something charming,
// not as it having died. A few point at things the desk keeps hidden
// (James, 2026-10-30: "perhaps a few could allude to the secret codes").
// Allusions only, never the way in: tools/audit_spoilers.py rules the
// published text, and these lines are written to the same standard.

namespace deckboy::core {

inline const char* const kLoadingQuips[] = {
      "waking the cue gremlins",
      "counting frames by hand",
      "buttering the playhead",
      "reticulating playlists",
      "asking the drive nicely",
      "warming the flux capacitor",
      "alphabetising the sprockets",
      "feeding the terrarium",
      "blood for the blood code",
      "the terrarium remembers an old cheat for thirty lives",
      "something in the walls smells of pizza",
      "dad says hi. from the hole. don't ask",
      "Baba says: knock three times for the good bottle",
      "the dice are in the About box. don't tell anyone",
      "folding the Lower Thirds",
      "polishing the crossfades",
      "reminding the strobe about epilepsy warnings",
      "counting the cues called Untitled",
      "letting the dolphin out",
      "untangling a fade from a fade",
      "explaining 1983 to Prof. Video",
      "Dr. Pixel is labelling the labels",
      "teaching the eel the Thames",
      "rounding the timecode to the nearest feeling",
      "consulting the vectorscope",
      "persuading the projector it is straight",
      "loading the playlist the long way round",
      "finding the second screen",
};

inline constexpr int kLoadingQuipCount = static_cast<int>(sizeof(kLoadingQuips) / sizeof(kLoadingQuips[0]));

}  // namespace deckboy::core
