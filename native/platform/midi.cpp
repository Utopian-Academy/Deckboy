// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// midi.cpp — MIDI input handler implementation.
//
// When DECKBOY_HAS_MIDI is defined (RtMidi SDK available), provides full
// hardware MIDI input via RtMidi 6.x. Otherwise falls back to a stub
// implementation that compiles but produces no MIDI events.
//
// Structure (pimpl pattern):
//   MidiInput::Impl — holds RtMidiIn instance, callback slots, device state
//   listDevices()   — enumerates available MIDI input ports
//   open()/close()  — connect/disconnect to a MIDI device
//   update()        — polls RtMidiIn::getMessage and dispatches callbacks
//   on*() setters   — store callbacks for CC, NoteOn, NoteOff, ProgramChange
//
// parseMidiMessage() is always available (production-ready):
//   Decodes raw MIDI bytes into typed (MessageType, data) pairs.
//   Handles ControlChange (0xB0), NoteOn (0x90), NoteOff (0x80),
//   ProgramChange (0xC0), and PitchBend (0xE0). Channel is masked off.
//
// Header: midi.hpp
// Used by: main.cpp MIDI controller mapping system.
// ============================================================================

#include "midi.hpp"

#include <iostream>
#include <vector>

#if defined(DECKBOY_HAS_MIDI)
#include <rtmidi/RtMidi.h>
#endif

namespace deckboy::platform::midi {

// ── Pimpl implementation ────────────────────────────────────────────────────

class MidiInput::Impl {
 public:
  MidiInput::ControlChangeCallback ccCallback_;
  MidiInput::NoteOnCallback noteOnCallback_;
  MidiInput::NoteOffCallback noteOffCallback_;
  MidiInput::ProgramChangeCallback progChangeCallback_;

  bool isOpen_ = false;
  int deviceId_ = -1;

#if defined(DECKBOY_HAS_MIDI)
  std::unique_ptr<RtMidiIn> midiIn_;
#endif
};

MidiInput::MidiInput() : impl_(std::make_unique<Impl>()) {}

MidiInput::~MidiInput() {
  close();
}

std::vector<DeviceInfo> MidiInput::listDevices() {
  std::vector<DeviceInfo> devices;

#if defined(DECKBOY_HAS_MIDI)
  try {
    RtMidiIn probe;
    unsigned int nPorts = probe.getPortCount();
    for (unsigned int i = 0; i < nPorts; ++i) {
      DeviceInfo info;
      info.id = static_cast<int>(i);
      info.name = probe.getPortName(i);
      devices.push_back(info);
    }
  } catch (const RtMidiError& e) {
    std::cerr << "MIDI enumerate failed: " << e.getMessage() << '\n';
  }
#endif

  return devices;
}

bool MidiInput::open(int deviceId) {
  if (impl_->isOpen_) {
    close();
  }

#if defined(DECKBOY_HAS_MIDI)
  try {
    impl_->midiIn_ = std::make_unique<RtMidiIn>();
    unsigned int nPorts = impl_->midiIn_->getPortCount();
    if (deviceId < 0 || static_cast<unsigned int>(deviceId) >= nPorts) {
      std::cerr << "MIDI open: port " << deviceId << " out of range (0-" << nPorts - 1 << ")\n";
      impl_->midiIn_.reset();
      return false;
    }
    impl_->midiIn_->openPort(static_cast<unsigned int>(deviceId));
    // Don't ignore sysex, timing, or active sensing — let parseMidiMessage handle all
    impl_->midiIn_->ignoreTypes(true, true, true);
    impl_->deviceId_ = deviceId;
    impl_->isOpen_ = true;
    return true;
  } catch (const RtMidiError& e) {
    std::cerr << "MIDI open failed: " << e.getMessage() << '\n';
    impl_->midiIn_.reset();
    return false;
  }
#else
  impl_->deviceId_ = deviceId;
  impl_->isOpen_ = true;
  return true;
#endif
}

bool MidiInput::isOpen() const {
  return impl_->isOpen_;
}

void MidiInput::close() {
  if (!impl_->isOpen_) {
    return;
  }

#if defined(DECKBOY_HAS_MIDI)
  impl_->midiIn_.reset();
#endif

  impl_->isOpen_ = false;
  impl_->deviceId_ = -1;
}

void MidiInput::update() {
  if (!impl_->isOpen_) {
    return;
  }

#if defined(DECKBOY_HAS_MIDI)
  if (!impl_->midiIn_) return;

  std::vector<unsigned char> message;
  while (true) {
    impl_->midiIn_->getMessage(&message);
    if (message.empty()) break;

    // Convert to uint8_t for parseMidiMessage
    std::vector<std::uint8_t> data(message.begin(), message.end());
    auto parsed = parseMidiMessage(data);
    if (!parsed) continue;

    auto [type, bytes] = *parsed;
    switch (type) {
      case MessageType::ControlChange:
        if (impl_->ccCallback_ && bytes.size() >= 2) {
          impl_->ccCallback_(bytes[0], bytes[1]);
        }
        break;
      case MessageType::NoteOn:
        if (impl_->noteOnCallback_ && bytes.size() >= 2) {
          impl_->noteOnCallback_(bytes[0], bytes[1]);
        }
        break;
      case MessageType::NoteOff:
        if (impl_->noteOffCallback_ && bytes.size() >= 1) {
          impl_->noteOffCallback_(bytes[0]);
        }
        break;
      case MessageType::ProgramChange:
        if (impl_->progChangeCallback_ && bytes.size() >= 1) {
          impl_->progChangeCallback_(bytes[0]);
        }
        break;
      default:
        break;
    }
  }
#endif
}

void MidiInput::onControlChange(ControlChangeCallback callback) {
  impl_->ccCallback_ = std::move(callback);
}

void MidiInput::onNoteOn(NoteOnCallback callback) {
  impl_->noteOnCallback_ = std::move(callback);
}

void MidiInput::onNoteOff(NoteOffCallback callback) {
  impl_->noteOffCallback_ = std::move(callback);
}

void MidiInput::onProgramChange(ProgramChangeCallback callback) {
  impl_->progChangeCallback_ = std::move(callback);
}

// ── MIDI message parser (production-ready) ──────────────────────────────────
// Decodes raw MIDI bytes into a typed (MessageType, data) pair.
// The status byte format is: [command nibble (4 bits)][channel nibble (4 bits)]
// Channel is masked off (& 0xF0) since we handle all channels uniformly.
std::optional<std::pair<MessageType, std::vector<int>>> parseMidiMessage(const std::vector<std::uint8_t>& data) {
  if (data.empty()) {
    return std::nullopt;
  }

  std::uint8_t status = data[0];
  std::uint8_t command = status & 0xF0;  // Strip channel nibble

  // Extract data bytes (everything after the status byte)
  std::vector<int> bytes;
  for (size_t i = 1; i < data.size(); ++i) {
    bytes.push_back(static_cast<int>(data[i]));
  }

  switch (command) {
    case 0xB0:  // Control Change — bytes: [controller, value]
      if (bytes.size() >= 2) {
        return std::make_pair(MessageType::ControlChange, std::vector<int>{bytes[0], bytes[1]});
      }
      break;
    case 0x90:  // Note On — bytes: [note, velocity]
      if (bytes.size() >= 2) {
        // Note On with velocity 0 is conventionally Note Off
        if (bytes[1] == 0) {
          return std::make_pair(MessageType::NoteOff, std::vector<int>{bytes[0]});
        }
        return std::make_pair(MessageType::NoteOn, std::vector<int>{bytes[0], bytes[1]});
      }
      break;
    case 0x80:  // Note Off — bytes: [note] (velocity ignored)
      if (bytes.size() >= 1) {
        return std::make_pair(MessageType::NoteOff, std::vector<int>{bytes[0]});
      }
      break;
    case 0xC0:  // Program Change — bytes: [program] (single data byte)
      if (bytes.size() >= 1) {
        return std::make_pair(MessageType::ProgramChange, std::vector<int>{bytes[0]});
      }
      break;
    case 0xE0:  // Pitch Bend — bytes: [LSB, MSB] → 14-bit value (0–16383, center=8192)
      if (bytes.size() >= 2) {
        int bend = (bytes[1] << 7) | bytes[0];
        return std::make_pair(MessageType::PitchBend, std::vector<int>{bend});
      }
      break;
    default:
      // SysEx, timing clock, active sensing, etc. — pass through as Other
      return std::make_pair(MessageType::Other, bytes);
  }

  return std::nullopt;  // Incomplete message (not enough data bytes)
}

}  // namespace deckboy::platform::midi
