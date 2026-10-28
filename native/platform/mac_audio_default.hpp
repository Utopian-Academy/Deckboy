// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// mac_audio_default.hpp -- which output the Mac's sound goes to, asked of
// CoreAudio directly rather than of SDL.
//
// James, 2026-10-28: on a Mac, plugging in headphones left Deckboy playing
// into the speakers until it was restarted. SDL moves every stream opened on
// the default to the new default -- when it hears about the change and can
// open the new device. On a headphone plug the Mac makes a new device AND makes
// it the default; if the second notice arrives first, SDL looks the new default
// up in a list that does not have it yet and drops the change for good (and if
// opening the new device fails, it leaves everything where it was). The app
// cannot see either from SDL, whose idea of the default is the stale one, so it
// asks the OS (App::reconcileDeckAudioDevices).
#pragma once

#include <string>

namespace deckboy::platform {

#ifdef __APPLE__
// The default output device's name, spelt the way SDL spells it (so the two
// compare equal), or empty when CoreAudio does not answer.
std::string macDefaultPlaybackDeviceName();
#else
inline std::string macDefaultPlaybackDeviceName() { return {}; }
#endif

}  // namespace deckboy::platform
