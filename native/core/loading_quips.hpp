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
      // Surf's haunting (2026-11-26): allusions only.
      "the night tide keeps a calendar",
      "a ship nobody built waits for a round moon",
      "the About box knows a word the sea answers to",
      "the sea remembers what you throw, and in what order",
};

inline constexpr int kLoadingQuipCount = static_cast<int>(sizeof(kLoadingQuips) / sizeof(kLoadingQuips[0]));

// Pocket's own, shown alongside the desk's on its splash (James, 11-20:
// "phone should have its own loading lines ... in addition").
inline const char* const kPocketLoadingQuips[] = {
  "folding the desk into a pocket",
  "warming up the thumbs",
  "teaching the gyroscope which way is up",
  "asking the battery nicely",
  "polishing the shell",
  "checking the pocket for lint",
  "shrinking the cue gremlins to pocket size",
  "persuading the screen to stay awake",
  "the ghosts are rehearsing their float",
  "calling the desk. it says hi",
  "the ABOUT page listens more than it talks",
  "the d-pad remembers an old trick",
  "ironing the touch screen",
  "counting the corners of a rounded phone",
  "reminding the speaker it is small",
  "the haptics are practising their purr",
  "aligning the HDMI with the stars",
  "setting the clock for Eddie",
  "asking the notch to scoot over",
  "putting the big show in a small box",
  "finding the projector in the bag",
  "untangling the USB-C",
  "the swipe is stretching",
  "pocket-sizing the playhead",
  "fluffing the Wi-Fi",
  "charging the confidence",
  "the ghosts want a word with the buttons",
  "something mellow waits for the right number",
  "smoothing the swirl",
  "thanking the battery in advance",
  "wiping the glass",
  "checking the screen is the right way up",
  "tucking in the transitions",
  "asking the gyroscope to calm down",
  "polishing the d-pad",
  "warming up the GO button",
  "folding the timeline twice",
  "teaching the shell to blink",
  "the beach is quieter when the moon is thin",
};

inline constexpr int kPocketLoadingQuipCount = static_cast<int>(sizeof(kPocketLoadingQuips) / sizeof(kPocketLoadingQuips[0]));

}  // namespace deckboy::core
