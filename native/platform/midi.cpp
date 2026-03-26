// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "midi.hpp"

#include <iostream>
#include <queue>
#include <thread>
#include <mutex>

// Note: This implementation is a stub. For production use, integrate RtMidi:
// https://github.com/thestk/rtmidi
// Install: apt-get install librtmidi-dev (Linux) or brew install rtmidi (macOS)
// #include <RtMidi.h>

namespace deckboy::platform::midi {

class MidiInput::Impl {
 public:
  MidiInput::ControlChangeCallback ccCallback_;
  MidiInput::NoteOnCallback noteOnCallback_;
  MidiInput::NoteOffCallback noteOffCallback_;
  MidiInput::ProgramChangeCallback progChangeCallback_;
  
  bool isOpen_ = false;
  int deviceId_ = -1;
  
  // Placeholder for RtMidiIn instance
  // std::unique_ptr<RtMidiIn> midiIn_;
};

MidiInput::MidiInput() : impl_(std::make_unique<Impl>()) {}

MidiInput::~MidiInput() {
  close();
}

std::vector<DeviceInfo> MidiInput::listDevices() {
  std::vector<DeviceInfo> devices;
  
  // TODO: When RtMidi is integrated, enumerate real MIDI devices:
  // RtMidiIn midiIn;
  // unsigned int nPorts = midiIn.getPortCount();
  // for (unsigned int i = 0; i < nPorts; ++i) {
  //   DeviceInfo info;
  //   info.id = static_cast<int>(i);
  //   info.name = midiIn.getPortName(i);
  //   devices.push_back(info);
  // }
  
  // For now, return empty list (stub)
  return devices;
}

bool MidiInput::open(int deviceId) {
  if (impl_->isOpen_) {
    close();
  }

  impl_->deviceId_ = deviceId;
  impl_->isOpen_ = true;
  
  // TODO: Open RtMidiIn port:
  // impl_->midiIn_ = std::make_unique<RtMidiIn>();
  // try {
  //   impl_->midiIn_->openPort(deviceId);
  // } catch (const RtMidiError& e) {
  //   std::cerr << "MIDI open failed: " << e.what() << '\n';
  //   impl_->isOpen_ = false;
  //   return false;
  // }
  
  return true;
}

bool MidiInput::isOpen() const {
  return impl_->isOpen_;
}

void MidiInput::close() {
  if (!impl_->isOpen_) {
    return;
  }
  
  impl_->isOpen_ = false;
  impl_->deviceId_ = -1;
  
  // TODO: Close RtMidiIn:
  // impl_->midiIn_.reset();
}

void MidiInput::update() {
  if (!impl_->isOpen_) {
    return;
  }
  
  // TODO: Poll MIDI messages with RtMidiIn:
  // std::vector<unsigned char> message;
  // double stamp = 0.0;
  // while (true) {
  //   stamp = impl_->midiIn_->getMessage(&message);
  //   if (message.empty()) break;
  //   
  //   auto parsed = parseMidiMessage(message);
  //   if (!parsed) continue;
  //   
  //   auto [type, bytes] = *parsed;
  //   switch (type) {
  //     case MessageType::ControlChange:
  //       if (impl_->ccCallback_ && bytes.size() >= 2) {
  //         impl_->ccCallback_(bytes[0], bytes[1]);
  //       }
  //       break;
  //     case MessageType::NoteOn:
  //       if (impl_->noteOnCallback_ && bytes.size() >= 2) {
  //         impl_->noteOnCallback_(bytes[0], bytes[1]);
  //       }
  //       break;
  //     case MessageType::NoteOff:
  //       if (impl_->noteOffCallback_ && bytes.size() >= 1) {
  //         impl_->noteOffCallback_(bytes[0]);
  //       }
  //       break;
  //     case MessageType::ProgramChange:
  //       if (impl_->progChangeCallback_ && bytes.size() >= 1) {
  //         impl_->progChangeCallback_(bytes[0]);
  //       }
  //       break;
  //     default:
  //       break;
  //   }
  // }
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

std::optional<std::pair<MessageType, std::vector<int>>> parseMidiMessage(const std::vector<std::uint8_t>& data) {
  if (data.empty()) {
    return std::nullopt;
  }

  std::uint8_t status = data[0];
  std::uint8_t command = status & 0xF0;

  std::vector<int> bytes;
  for (size_t i = 1; i < data.size(); ++i) {
    bytes.push_back(static_cast<int>(data[i]));
  }

  switch (command) {
    case 0xB0:  // Control Change
      if (bytes.size() >= 2) {
        return std::make_pair(MessageType::ControlChange, std::vector<int>{bytes[0], bytes[1]});
      }
      break;
    case 0x90:  // Note On
      if (bytes.size() >= 2) {
        return std::make_pair(MessageType::NoteOn, std::vector<int>{bytes[0], bytes[1]});
      }
      break;
    case 0x80:  // Note Off
      if (bytes.size() >= 1) {
        return std::make_pair(MessageType::NoteOff, std::vector<int>{bytes[0]});
      }
      break;
    case 0xC0:  // Program Change
      if (bytes.size() >= 1) {
        return std::make_pair(MessageType::ProgramChange, std::vector<int>{bytes[0]});
      }
      break;
    case 0xE0:  // Pitch Bend
      if (bytes.size() >= 2) {
        int bend = (bytes[1] << 7) | bytes[0];
        return std::make_pair(MessageType::PitchBend, std::vector<int>{bend});
      }
      break;
    default:
      return std::make_pair(MessageType::Other, bytes);
  }

  return std::nullopt;
}

}  // namespace deckboy::platform::midi
