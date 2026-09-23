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

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string>
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
  MidiInput::SysExCallback sysExCallback_;
  MidiInput::RealtimeCallback realtimeCallback_;
  MidiInput::QuarterFrameCallback quarterFrameCallback_;

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
    // SysEx is NOT ignored: MSC and MMC are carried in nothing else, and the
    // default here is to throw them away.
    //
    // TIMING IS NO LONGER IGNORED EITHER. It was, on the grounds that clock
    // ticks arrive hundreds of times a second and mean nothing to a cue deck --
    // true until the deck grew a tempo. 24 ticks a beat is how a VJ rig follows
    // the desk it is plugged into, and the app throws away all but one in 24
    // (see the clock follower), so the rate costs nothing.
    //
    // Active sensing stays ignored: it is a keep-alive and says nothing.
    impl_->midiIn_->ignoreTypes(false, false, true);
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
    // SysEx first: it is the one message that is not a status byte plus one or
    // two data bytes, so the channel-voice parser cannot speak for it.
    if (!data.empty() && data.front() == 0xF0) {
      if (impl_->sysExCallback_) {
        impl_->sysExCallback_(data);
      }
      continue;
    }
    // System Real-Time next: one byte, 0xF8..0xFF, no channel and no data.
    // parseMidiMessage masks the channel nibble off the status byte, which
    // would turn a 0xF8 clock tick into a nonsense 0xF0 -- so these have to be
    // taken before it ever sees them.
    if (data.size() == 1 && data.front() >= 0xF8) {
      if (impl_->realtimeCallback_) {
        impl_->realtimeCallback_(data.front());
      }
      continue;
    }
    // MTC quarter frame (0xF1) before the parser too, and for the same reason:
    // 0xF1 & 0xF0 is 0xF0, so the channel-voice parser reads a piece of
    // timecode as the start of a SysEx and drops it. That is exactly what
    // happened -- MIDI timecode was accepted on the ALSA path and silently
    // ignored everywhere else, while the integration catalog blamed ALSA for
    // it rather than the wiring.
    if (data.size() >= 2 && data.front() == 0xF1) {
      if (impl_->quarterFrameCallback_) {
        impl_->quarterFrameCallback_(data[1]);
      }
      continue;
    }

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

void MidiInput::onSysEx(SysExCallback callback) {
  impl_->sysExCallback_ = std::move(callback);
}

void MidiInput::onRealtime(RealtimeCallback callback) {
  impl_->realtimeCallback_ = std::move(callback);
}

void MidiInput::onQuarterFrame(QuarterFrameCallback callback) {
  impl_->quarterFrameCallback_ = std::move(callback);
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

// ── MIDI OUT ────────────────────────────────────────────────────────────────

const char* outMessageKindToken(OutMessageKind kind) {
  switch (kind) {
    case OutMessageKind::NoteOff:       return "note-off";
    case OutMessageKind::ControlChange: return "cc";
    case OutMessageKind::ProgramChange: return "program";
    case OutMessageKind::MscGo:         return "msc-go";
    case OutMessageKind::MscStop:       return "msc-stop";
    case OutMessageKind::MscResume:     return "msc-resume";
    case OutMessageKind::Raw:           return "raw";
    case OutMessageKind::NoteOn:        break;
  }
  return "note-on";
}

const char* outMessageKindLabel(OutMessageKind kind) {
  switch (kind) {
    case OutMessageKind::NoteOff:       return "Note off";
    case OutMessageKind::ControlChange: return "Control change";
    case OutMessageKind::ProgramChange: return "Program change";
    case OutMessageKind::MscGo:         return "MSC GO";
    case OutMessageKind::MscStop:       return "MSC STOP";
    case OutMessageKind::MscResume:     return "MSC RESUME";
    case OutMessageKind::Raw:           return "Raw bytes";
    case OutMessageKind::NoteOn:        break;
  }
  return "Note on";
}

OutMessageKind outMessageKindFromToken(const std::string& token) {
  if (token == "note-off")   return OutMessageKind::NoteOff;
  if (token == "cc")         return OutMessageKind::ControlChange;
  if (token == "program")    return OutMessageKind::ProgramChange;
  if (token == "msc-go")     return OutMessageKind::MscGo;
  if (token == "msc-stop")   return OutMessageKind::MscStop;
  if (token == "msc-resume") return OutMessageKind::MscResume;
  if (token == "raw")        return OutMessageKind::Raw;
  return OutMessageKind::NoteOn;
}

namespace {

// MSC carries cue numbers as ASCII with the dots intact -- "12.5" is twelve
// point five, not 125 -- which is why Cue::mscCue is text and not a number.
void appendMscAscii(std::vector<std::uint8_t>& out, const std::string& text) {
  for (char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    // Only the characters MSC defines for a cue number. Anything else would
    // put a byte above 0x7F inside a SysEx and terminate it early, which
    // desks answer by ignoring the whole message.
    if ((u >= '0' && u <= '9') || u == '.') {
      out.push_back(static_cast<std::uint8_t>(u));
    }
  }
}

int clampByte(int v) { return v < 0 ? 0 : (v > 127 ? 127 : v); }

}  // namespace

std::vector<std::uint8_t> encodeOutMessage(const OutMessage& message) {
  std::vector<std::uint8_t> out;
  // Operators count channels from 1; the wire counts from 0.
  const int channel = (message.channel < 1 ? 1 : (message.channel > 16 ? 16 : message.channel)) - 1;

  switch (message.kind) {
    case OutMessageKind::NoteOn:
      out = {static_cast<std::uint8_t>(0x90 | channel),
             static_cast<std::uint8_t>(clampByte(message.data1)),
             static_cast<std::uint8_t>(clampByte(message.data2))};
      return out;
    case OutMessageKind::NoteOff:
      out = {static_cast<std::uint8_t>(0x80 | channel),
             static_cast<std::uint8_t>(clampByte(message.data1)),
             static_cast<std::uint8_t>(clampByte(message.data2))};
      return out;
    case OutMessageKind::ControlChange:
      out = {static_cast<std::uint8_t>(0xB0 | channel),
             static_cast<std::uint8_t>(clampByte(message.data1)),
             static_cast<std::uint8_t>(clampByte(message.data2))};
      return out;
    case OutMessageKind::ProgramChange:
      out = {static_cast<std::uint8_t>(0xC0 | channel),
             static_cast<std::uint8_t>(clampByte(message.data1))};
      return out;
    case OutMessageKind::Raw: {
      // "90 3C 7F" or "903C7F". Anything that is not a pair of hex digits ends
      // the parse and yields NOTHING, rather than a truncated message: half a
      // MIDI message on a show network is worse than silence.
      std::string digits;
      for (char c : message.rawHex) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
          digits.push_back(c);
        } else if (c != ' ' && c != ',' && c != '\t') {
          return {};
        }
      }
      if (digits.empty() || (digits.size() % 2) != 0) {
        return {};
      }
      for (std::size_t i = 0; i + 1 < digits.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(
          std::stoi(digits.substr(i, 2), nullptr, 16)));
      }
      return out;
    }
    case OutMessageKind::MscGo:
    case OutMessageKind::MscStop:
    case OutMessageKind::MscResume:
      break;
  }

  // MIDI Show Control, the same shape this app already PARSES on the way in
  // (see core/show_control.hpp) -- which is why the vocabulary matches and a
  // Deckboy can drive another Deckboy.
  //
  //   F0 7F <device> 02 <command format> <command> [<cue> 00 <list>] F7
  //
  // Command format 0x01 is "lighting general"; 0x7F is all-types, which is
  // what a cue aimed at a whole rack wants and what Deckboy itself accepts.
  const std::uint8_t command =
    message.kind == OutMessageKind::MscGo     ? 0x01 :
    message.kind == OutMessageKind::MscStop   ? 0x02 : 0x03;
  out.push_back(0xF0);
  out.push_back(0x7F);
  out.push_back(static_cast<std::uint8_t>(clampByte(message.mscDevice)));
  out.push_back(0x02);                       // MSC
  out.push_back(0x7F);                       // all-types
  out.push_back(command);
  if (!message.mscCue.empty()) {
    appendMscAscii(out, message.mscCue);
    if (!message.mscList.empty()) {
      out.push_back(0x00);                   // separator
      appendMscAscii(out, message.mscList);
    }
  }
  out.push_back(0xF7);
  return out;
}

std::string describeOutMessage(const OutMessage& message) {
  std::string text = outMessageKindLabel(message.kind);
  switch (message.kind) {
    case OutMessageKind::NoteOn:
    case OutMessageKind::NoteOff:
      return text + " ch" + std::to_string(message.channel) +
             " note " + std::to_string(message.data1) +
             " vel " + std::to_string(message.data2);
    case OutMessageKind::ControlChange:
      return text + " ch" + std::to_string(message.channel) +
             " cc " + std::to_string(message.data1) +
             " = " + std::to_string(message.data2);
    case OutMessageKind::ProgramChange:
      return text + " ch" + std::to_string(message.channel) +
             " program " + std::to_string(message.data1);
    case OutMessageKind::Raw:
      return text + " " + (message.rawHex.empty() ? std::string("(none)") : message.rawHex);
    case OutMessageKind::MscGo:
    case OutMessageKind::MscStop:
    case OutMessageKind::MscResume:
      break;
  }
  text += " device " + std::to_string(message.mscDevice);
  if (!message.mscCue.empty()) {
    text += " cue " + message.mscCue;
  }
  if (!message.mscList.empty()) {
    text += " list " + message.mscList;
  }
  return text;
}

class MidiOutput::Impl {
 public:
  bool isOpen_ = false;
  std::string portInUse_;
#if defined(DECKBOY_HAS_MIDI)
  std::unique_ptr<RtMidiOut> midiOut_;
#endif
};

MidiOutput::MidiOutput() : impl_(std::make_unique<Impl>()) {}

MidiOutput::~MidiOutput() { close(); }

std::vector<DeviceInfo> MidiOutput::listDevices() {
  std::vector<DeviceInfo> devices;
#if defined(DECKBOY_HAS_MIDI)
  try {
    RtMidiOut probe;
    unsigned int nPorts = probe.getPortCount();
    for (unsigned int i = 0; i < nPorts; ++i) {
      DeviceInfo info;
      info.id = static_cast<int>(i);
      info.name = probe.getPortName(i);
      devices.push_back(info);
    }
  } catch (const RtMidiError& e) {
    std::cerr << "MIDI out enumerate failed: " << e.getMessage() << '\n';
  }
#endif
  return devices;
}

bool MidiOutput::open(int deviceId) {
  close();
#if defined(DECKBOY_HAS_MIDI)
  try {
    impl_->midiOut_ = std::make_unique<RtMidiOut>();
    unsigned int nPorts = impl_->midiOut_->getPortCount();
    if (deviceId < 0 || static_cast<unsigned int>(deviceId) >= nPorts) {
      std::cerr << "MIDI out open: port " << deviceId << " out of range\n";
      impl_->midiOut_.reset();
      return false;
    }
    impl_->portInUse_ = impl_->midiOut_->getPortName(static_cast<unsigned int>(deviceId));
    impl_->midiOut_->openPort(static_cast<unsigned int>(deviceId));
    impl_->isOpen_ = true;
    return true;
  } catch (const RtMidiError& e) {
    std::cerr << "MIDI out open failed: " << e.getMessage() << '\n';
    impl_->midiOut_.reset();
    return false;
  }
#else
  (void)deviceId;
  return false;
#endif
}

bool MidiOutput::openByName(const std::string& name) {
  if (name.empty()) {
    return false;
  }
  // A NAMED PORT THAT IS ABSENT IS REPORTED, NOT SWAPPED. The same rule the
  // app already applies to a named MIDI input and a named audio device: what
  // the operator asked for is the request, and binding to whatever happens to
  // enumerate first is how a show ends up driving the wrong desk.
  const auto devices = listDevices();
  for (const auto& d : devices) {
    if (d.name == name) {
      return open(d.id);
    }
  }
  for (const auto& d : devices) {
    if (d.name.find(name) != std::string::npos) {
      return open(d.id);
    }
  }
  return false;
}

bool MidiOutput::isOpen() const { return impl_->isOpen_; }

const std::string& MidiOutput::portInUse() const { return impl_->portInUse_; }

void MidiOutput::close() {
#if defined(DECKBOY_HAS_MIDI)
  if (impl_->midiOut_) {
    try {
      impl_->midiOut_->closePort();
    } catch (const RtMidiError&) {
    }
    impl_->midiOut_.reset();
  }
#endif
  impl_->isOpen_ = false;
  impl_->portInUse_.clear();
}

bool MidiOutput::send(const std::vector<std::uint8_t>& bytes) {
  if (bytes.empty() || !impl_->isOpen_) {
    return false;
  }
#if defined(DECKBOY_HAS_MIDI)
  try {
    impl_->midiOut_->sendMessage(&bytes);
    return true;
  } catch (const RtMidiError& e) {
    std::cerr << "MIDI out send failed: " << e.getMessage() << '\n';
    return false;
  }
#else
  return false;
#endif
}


// ── STANDARD MIDI FILE PARSING ──────────────────────────────────────────────

namespace {

std::uint32_t beU32(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) |
         static_cast<std::uint32_t>(p[3]);
}

std::uint16_t beU16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

// A variable-length quantity: seven bits per byte, high bit means "another
// one follows". Capped at four bytes, which is the format's own limit -- an
// uncapped reader walks off the end of a damaged file.
bool readVarLen(const std::vector<std::uint8_t>& d, std::size_t& at, std::uint32_t& out) {
  out = 0;
  for (int i = 0; i < 4; ++i) {
    if (at >= d.size()) {
      return false;
    }
    const std::uint8_t byte = d[at++];
    out = (out << 7) | static_cast<std::uint32_t>(byte & 0x7F);
    if ((byte & 0x80) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool looksLikeMidiFile(const std::vector<std::uint8_t>& data) {
  return data.size() >= 14 && data[0] == 'M' && data[1] == 'T' &&
         data[2] == 'h' && data[3] == 'd';
}

File parseMidiFile(const std::vector<std::uint8_t>& data) {
  File file;
  if (!looksLikeMidiFile(data)) {
    file.error = "not a MIDI file (no MThd header)";
    return file;
  }
  const std::uint32_t headerLen = beU32(&data[4]);
  if (headerLen < 6 || 8 + headerLen > data.size()) {
    file.error = "the header is truncated";
    return file;
  }
  file.format = beU16(&data[8]);
  file.trackCount = beU16(&data[10]);
  const std::int16_t division = static_cast<std::int16_t>(beU16(&data[12]));
  if (file.format == 2) {
    // Refused rather than guessed. Format 2's tracks are independent patterns
    // with no shared timeline, so "play the file" has no single meaning --
    // flattening them would invent an arrangement nobody wrote.
    file.error = "format 2 files are a set of independent patterns, not a performance";
    return file;
  }

  // TICKS PER QUARTER, or SMPTE. A negative division is frames-per-second in
  // the high byte and ticks-per-frame in the low one, which is how anything
  // stamped against video arrives -- and its tempo is fixed by the format, so
  // tempo meta events do not apply to it.
  double secondsPerTick = 0.0;
  bool smpte = false;
  int ticksPerQuarter = 0;
  if (division > 0) {
    ticksPerQuarter = division;
    secondsPerTick = 0.5 / static_cast<double>(ticksPerQuarter);   // 120bpm until told
  } else {
    smpte = true;
    const int fps = -(division >> 8);
    const int ticksPerFrame = division & 0xFF;
    if (fps <= 0 || ticksPerFrame <= 0) {
      file.error = "the time division is not readable";
      return file;
    }
    // 29 in the file means 29.97 drop-frame; every other value is exact.
    const double realFps = (fps == 29) ? 30000.0 / 1001.0 : static_cast<double>(fps);
    secondsPerTick = 1.0 / (realFps * static_cast<double>(ticksPerFrame));
  }

  // Each track is read into (tick, bytes) first and the tracks are merged
  // afterwards, because TEMPO IS GLOBAL: a tempo change on track 1 moves
  // every later event on every track, so nothing can be converted to seconds
  // until all of them are on one timeline.
  struct RawEvent {
    std::uint64_t tick = 0;
    std::size_t order = 0;            // keeps a stable sort within one tick
    std::vector<std::uint8_t> bytes;
    int tempoMicros = 0;              // non-zero: a tempo change, not a message
  };
  std::vector<RawEvent> raw;
  std::size_t at = 8 + headerLen;
  std::size_t order = 0;

  for (int track = 0; track < file.trackCount; ++track) {
    if (at + 8 > data.size()) {
      break;   // fewer tracks than the header promised; play what is there
    }
    if (!(data[at] == 'M' && data[at + 1] == 'T' && data[at + 2] == 'r' &&
          data[at + 3] == 'k')) {
      // An unknown chunk is skipped by its own length, which is exactly what
      // the specification says to do -- it is how a file survives a writer
      // that stores its own private data alongside the music.
      const std::uint32_t skip = beU32(&data[at + 4]);
      at += 8 + skip;
      continue;
    }
    const std::uint32_t trackLen = beU32(&data[at + 4]);
    std::size_t p = at + 8;
    const std::size_t trackEnd = std::min(data.size(), p + trackLen);
    at = trackEnd;

    std::uint64_t tick = 0;
    std::uint8_t runningStatus = 0;
    while (p < trackEnd) {
      std::uint32_t delta = 0;
      if (!readVarLen(data, p, delta)) {
        break;
      }
      tick += delta;
      if (p >= trackEnd) {
        break;
      }
      std::uint8_t status = data[p];
      if (status < 0x80) {
        // RUNNING STATUS: no status byte, so the last one still applies. A
        // parser without this reads one note correctly and then nonsense.
        if (runningStatus == 0) {
          break;
        }
        status = runningStatus;
      } else {
        ++p;
        if (status < 0xF0) {
          runningStatus = status;
        }
      }

      if (status == 0xFF) {
        // Meta. Not sent to the port -- it is information about the file.
        if (p >= trackEnd) break;
        const std::uint8_t type = data[p++];
        std::uint32_t len = 0;
        if (!readVarLen(data, p, len)) break;
        if (p + len > trackEnd) break;
        if (type == 0x51 && len == 3 && !smpte) {
          RawEvent ev;
          ev.tick = tick;
          ev.order = order++;
          ev.tempoMicros = (data[p] << 16) | (data[p + 1] << 8) | data[p + 2];
          raw.push_back(std::move(ev));
        }
        p += len;
        if (type == 0x2F) {
          break;   // end of track
        }
        continue;
      }
      if (status == 0xF0 || status == 0xF7) {
        // SysEx, passed through whole -- a show that drives a desk is mostly
        // this, and dropping it would make the feature useless for the one
        // job it is most often wanted for.
        std::uint32_t len = 0;
        if (!readVarLen(data, p, len)) break;
        if (p + len > trackEnd) break;
        RawEvent ev;
        ev.tick = tick;
        ev.order = order++;
        ev.bytes.push_back(status);
        ev.bytes.insert(ev.bytes.end(), data.begin() + p, data.begin() + p + len);
        raw.push_back(std::move(ev));
        p += len;
        continue;
      }

      // An ordinary channel message: one or two data bytes by status.
      const int dataBytes =
        ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) ? 1 : 2;
      if (p + static_cast<std::size_t>(dataBytes) > trackEnd) {
        break;
      }
      RawEvent ev;
      ev.tick = tick;
      ev.order = order++;
      ev.bytes.push_back(status);
      for (int i = 0; i < dataBytes; ++i) {
        ev.bytes.push_back(data[p++]);
      }
      raw.push_back(std::move(ev));
    }
  }

  std::stable_sort(raw.begin(), raw.end(), [](const RawEvent& a, const RawEvent& b) {
    return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
  });

  // Walk the merged timeline once, converting ticks to seconds and applying
  // each tempo change from the tick it happens at.
  double seconds = 0.0;
  std::uint64_t lastTick = 0;
  for (const RawEvent& ev : raw) {
    seconds += static_cast<double>(ev.tick - lastTick) * secondsPerTick;
    lastTick = ev.tick;
    if (ev.tempoMicros > 0) {
      if (ticksPerQuarter > 0) {
        secondsPerTick = (static_cast<double>(ev.tempoMicros) / 1000000.0) /
                         static_cast<double>(ticksPerQuarter);
      }
      continue;
    }
    if (ev.bytes.empty()) {
      continue;
    }
    FileEvent out;
    out.seconds = seconds;
    out.bytes = ev.bytes;
    file.events.push_back(std::move(out));
  }

  file.durationSeconds = file.events.empty() ? 0.0 : file.events.back().seconds;
  file.ok = true;
  if (file.events.empty()) {
    file.error = "the file has no playable events";
  }
  return file;
}

File readMidiFile(const std::string& path) {
  File file;
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    file.error = "could not open " + path;
    return file;
  }
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
  if (data.empty()) {
    file.error = "the file is empty";
    return file;
  }
  return parseMidiFile(data);
}

}  // namespace deckboy::platform::midi
