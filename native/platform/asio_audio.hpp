// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// asio_audio.hpp — Steinberg ASIO device discovery and capability query.
//
// ASIO is how Windows does professional multichannel audio: the driver is
// supplied by the interface vendor, talks to the hardware directly, and gives
// far lower latency and far higher channel counts than the shared mixer. It is
// also how Dante reaches an application, via Dante Virtual Soundcard's ASIO
// driver.
//
// The SDK is used under the GPL v3 half of its dual licence; see
// native/third_party/asio/README.md.
//
// Everything here degrades to "no devices" when ASIO is not compiled in or no
// driver is installed. A machine with no ASIO interface is the NORMAL case, not
// an error, and must never produce a warning an operator has to dismiss.
// ============================================================================

#ifndef DECKBOY_PLATFORM_ASIO_AUDIO_HPP
#define DECKBOY_PLATFORM_ASIO_AUDIO_HPP

#include <string>
#include <vector>

namespace deckboy {
namespace platform {
namespace audio {

// One installed ASIO driver, as reported by the registry plus (when probed) the
// driver itself. Channel counts and buffer size are only meaningful once
// `probed` is true, because obtaining them requires LOADING the driver, which
// can take hundreds of milliseconds and may pop the vendor's own dialog.
struct AsioDeviceInfo {
  std::string name;             // driver name as the vendor registered it
  int inputChannels = 0;
  int outputChannels = 0;
  int preferredBufferFrames = 0;
  double sampleRate = 0.0;
  bool probed = false;          // false = registry entry only, counts unknown
  std::string error;            // why a probe failed, for the operator
};

// True when ASIO support was compiled in at all.
bool asioSupportCompiled();

// Installed drivers, from the registry. Cheap: does NOT load any driver, so it
// is safe to call while a show is running.
std::vector<AsioDeviceInfo> listAsioDevices();

// Load one driver and fill in its capabilities. EXPENSIVE and intrusive: some
// drivers show their own control panel or seize the device on load, so this is
// only called when the operator asks for it, never on a timer or at boot.
bool probeAsioDevice(const std::string& name, AsioDeviceInfo& out);

}  // namespace audio
}  // namespace platform
}  // namespace deckboy

#endif  // DECKBOY_PLATFORM_ASIO_AUDIO_HPP
