// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// midi.hpp — MIDI input handler for external controller integration.
//
// Provides a callback-based MIDI input system for mapping physical controllers
// (faders, buttons, knobs) to Deckboy actions. Supports:
//   - Control Change (CC): faders, knobs → continuous values (0–127)
//   - Note On/Off: buttons, pads → trigger/release events
//   - Program Change: preset selection
//   - Pitch Bend: wheel input
//
// The MidiInput class wraps platform-specific MIDI APIs:
//   Windows: Windows Multimedia (winmm) MIDI input
//   macOS:   Core MIDI
//   Linux:   ALSA sequencer
//
// Usage:
//   1. MidiInput::listDevices() to enumerate available MIDI devices
//   2. open(deviceId) to connect to a device
//   3. Register callbacks (onControlChange, onNoteOn, etc.)
//   4. Call update() in the main loop to dispatch pending messages
//   5. close() or destructor to disconnect
//
// Implementation: midi.cpp (pimpl pattern, platform-specific Impl class)
// Used by: main.cpp MIDI controller mapping system.
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace deckboy::platform::midi {

// Information about an available MIDI input device.
struct DeviceInfo {
  int id = -1;
  std::string name;
  bool isOpen = false;
};

// MIDI message types
enum class MessageType {
  ControlChange,  // CC: continuous controller (fader, knob)
  NoteOn,         // Note on (trigger)
  NoteOff,        // Note off
  ProgramChange,  // Program change (preset)
  PitchBend,      // Pitch bend wheel
  Other
};

// MIDI input handler
class MidiInput {
 public:
  // Callbacks for MIDI events
  using ControlChangeCallback = std::function<void(int controller, int value)>;  // controller 0-119, value 0-127
  using NoteOnCallback = std::function<void(int note, int velocity)>;  // note 0-127, velocity 0-127
  using NoteOffCallback = std::function<void(int note)>;
  using ProgramChangeCallback = std::function<void(int program)>;  // program 0-127

  MidiInput();
  ~MidiInput();

  // Prevent copying
  MidiInput(const MidiInput&) = delete;
  MidiInput& operator=(const MidiInput&) = delete;

  // Device enumeration
  static std::vector<DeviceInfo> listDevices();

  // Lifecycle
  bool open(int deviceId);
  bool isOpen() const;
  void close();

  // Call in main loop to process pending MIDI messages
  void update();

  // Register callbacks
  void onControlChange(ControlChangeCallback callback);
  void onNoteOn(NoteOnCallback callback);
  void onNoteOff(NoteOffCallback callback);
  void onProgramChange(ProgramChangeCallback callback);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  friend class Impl;
};

// Helper: Parse MIDI message from raw bytes
std::optional<std::pair<MessageType, std::vector<int>>> parseMidiMessage(const std::vector<std::uint8_t>& data);

}  // namespace deckboy::platform::midi
