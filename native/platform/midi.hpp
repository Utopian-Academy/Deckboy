// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Playboy Contributors
// This file is part of Playboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace playboy::platform::midi {

// MIDI input device information
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

}  // namespace playboy::platform::midi
