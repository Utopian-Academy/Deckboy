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
  // Raw System Exclusive, F0 through F7 inclusive.
  //
  // Handed over whole rather than parsed here: SysEx carries MIDI Show
  // Control, MIDI Machine Control and every manufacturer's own dialect, and
  // this layer has no business knowing which is which.
  using SysExCallback = std::function<void(const std::vector<std::uint8_t>& data)>;

  // System Real-Time: a single status byte, no data, and it may arrive in the
  // MIDDLE of another message. Handed over raw because the only ones that
  // matter here are Clock (0xF8), Start (0xFA), Continue (0xFB) and Stop
  // (0xFC), and what they mean is the caller's business, not this layer's.
  using RealtimeCallback = std::function<void(std::uint8_t status)>;

  // MTC quarter frame (0xF1): System COMMON, not System Real-Time -- a status
  // byte plus one data byte carrying one nibble of a timecode. It has to be
  // taken before the channel-voice parser, which masks the channel nibble off
  // and would read 0xF1 as 0xF0. Handed over as the data byte alone, because
  // assembling eight of them into a time is core::MtcQuarterFrameDecoder's job
  // and not this layer's.
  using QuarterFrameCallback = std::function<void(std::uint8_t dataByte)>;

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
  void onSysEx(SysExCallback callback);
  void onRealtime(RealtimeCallback callback);
  void onQuarterFrame(QuarterFrameCallback callback);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  friend class Impl;
};

// Helper: Parse MIDI message from raw bytes
std::optional<std::pair<MessageType, std::vector<int>>> parseMidiMessage(const std::vector<std::uint8_t>& data);

// ── MIDI OUT ────────────────────────────────────────────────────────────────
//
// Deckboy has listened to MIDI, MSC and MMC since early on and has never
// spoken a word of any of them. A cue list that cannot tell the lighting desk
// to go is half a show-control system.
//
// THE ENCODER IS SEPARATE FROM THE PORT, and deliberately: building the bytes
// is a pure function that can be tested with no hardware in the room, which is
// the only part of this that can be checked by a machine. Whether a socket
// actually carried them to a desk is a question for a desk.
// ---------------------------------------------------------------------------

// What a MIDI cue sends.
enum class OutMessageKind {
  NoteOn,
  NoteOff,
  ControlChange,
  ProgramChange,
  MscGo,          // MIDI Show Control: GO
  MscStop,
  MscResume,
  Raw,            // the operator's own bytes, for everything else
};

const char* outMessageKindToken(OutMessageKind kind);
const char* outMessageKindLabel(OutMessageKind kind);
OutMessageKind outMessageKindFromToken(const std::string& token);

// Everything a MIDI cue carries. Channel and the two data bytes cover the
// channel-voice messages; the MSC fields cover the show-control ones; raw
// carries whatever the operator typed.
struct OutMessage {
  OutMessageKind kind = OutMessageKind::NoteOn;
  int channel = 1;          // 1-16 as an operator counts them
  int data1 = 60;           // note number, controller number, or program
  int data2 = 127;          // velocity or controller value
  int mscDevice = 0;        // 0-127, or 127 for "all devices"
  std::string mscCue;       // "12.5" -- kept as text, because the dots matter
  std::string mscList;
  std::string rawHex;       // "90 3C 7F", for Raw
};

// Build the bytes. Returns empty when the message cannot be built, which is
// the same answer as "do not send anything" -- a half-formed MIDI message on
// a show network is worse than silence.
std::vector<std::uint8_t> encodeOutMessage(const OutMessage& message);

// A description an operator can read, for the inspector row and the log.
std::string describeOutMessage(const OutMessage& message);

class MidiOutput {
 public:
  MidiOutput();
  ~MidiOutput();
  MidiOutput(const MidiOutput&) = delete;
  MidiOutput& operator=(const MidiOutput&) = delete;

  static std::vector<DeviceInfo> listDevices();

  // BY NAME, because that is what a show file can carry and a port number is
  // not: plug in one more controller and every number after it moves. Matches
  // on exact name first, then on a substring, and reports rather than guessing
  // when it finds nothing.
  bool openByName(const std::string& name);
  bool open(int deviceId);
  bool isOpen() const;
  void close();
  const std::string& portInUse() const;

  // False when the port is not open or the bytes are empty.
  bool send(const std::vector<std::uint8_t>& bytes);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace deckboy::platform::midi
