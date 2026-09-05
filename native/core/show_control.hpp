// ═══════════════════════════════════════════════════════════════════════════
// show_control.hpp — MIDI Show Control and MIDI Machine Control, parsed.
//
// MSC is how a lighting desk fires a video cue. It is the theatre standard:
// an ETC Eos or a GrandMA sends GO with a cue number and every device in the
// rig that answers to that number goes at the same instant. Without it Deckboy
// cannot be specified into a theatre at all, however much else it does.
//
// Both protocols live entirely inside System Exclusive:
//
//   MSC   F0 7F <deviceID> 02 <format> <command> [data] F7
//   MMC   F0 7F <deviceID> 06 <command> F7
//
// The device ID is 0-111 for a single device, 112-126 for a group, and 127 for
// "everybody". A cue deck almost always wants either its own ID or 127, and
// answering to a message meant for the lighting rig is worse than missing one,
// so the match is exact.
//
// PARSING IS SEPARATE FROM ACTING, deliberately. This header knows the wire
// format and nothing about cues, which is what lets the whole protocol be
// tested from a byte array with no MIDI interface, no desk, and no theatre --
// and every message shape below is covered in the smoke tests.
//
// Reference: MIDI Show Control 1.1 (MMA RP-002/RP-014), MIDI Machine Control
// (MMA RP-013).
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace deckboy::showcontrol {

// What a message is asking for. Deliberately smaller than the specifications:
// these are the commands a video playback device can honour, and a command it
// cannot honour is better reported as Unknown than silently treated as one it
// can.
enum class Action {
  None,
  Go,          // MSC GO           — take the cue
  Stop,        // MSC STOP         — stop, hold position
  Resume,      // MSC RESUME       — continue from where STOP left it
  Load,        // MSC LOAD         — preselect without firing
  AllOff,      // MSC ALL_OFF      — blackout
  Reset,       // MSC RESET        — return to the top
  MmcPlay,     // MMC PLAY / DEFERRED PLAY
  MmcStop,     // MMC STOP
  MmcPause,    // MMC PAUSE
  MmcLocate,   // MMC LOCATE       — go to a timecode position
};

struct Message {
  Action action = Action::None;
  // MSC cue numbers are TEXT, not numbers: "12.4.1" is a legitimate cue and so
  // is "A". Kept as typed so it can be matched against a cue number the same
  // way an operator would read it.
  std::string cue;
  std::string cueList;
  std::string cuePath;
  // MMC LOCATE target, in seconds. Negative when the message carried none.
  double locateSeconds = -1.0;
  int deviceId = 0;
  bool ok() const { return action != Action::None; }
};

namespace detail {

// MSC command format. A video player answers to the video formats and to
// all-types, and ignores the rest -- a GO addressed to the lighting rig is not
// ours even when it arrives on our wire.
inline bool formatIsForVideo(std::uint8_t format) {
  if (format == 0x7F) return true;          // all-types
  return format >= 0x30 && format <= 0x3F;  // video general and its subtypes
}

// MSC packs its text fields as ASCII digits separated by 00, terminated by the
// end of the message. Read one field from `at`, leaving `at` past the
// separator.
inline std::string readField(const std::vector<std::uint8_t>& data,
                             std::size_t& at, std::size_t end) {
  std::string out;
  while (at < end && data[at] != 0x00) {
    out.push_back(static_cast<char>(data[at]));
    ++at;
  }
  if (at < end && data[at] == 0x00) {
    ++at;   // step over the separator
  }
  return out;
}

}  // namespace detail

// Parse one complete SysEx message, F0 through F7 inclusive.
//
// `myDeviceId` is what this machine answers to. 127 in the message is
// everybody and always matches; anything else has to be exact.
inline Message parse(const std::vector<std::uint8_t>& data, int myDeviceId) {
  Message message;
  // F0 7F <id> <sub-id> <command> ... F7 is the shortest useful shape.
  if (data.size() < 6) return message;
  if (data.front() != 0xF0 || data.back() != 0xF7) return message;
  if (data[1] != 0x7F) return message;      // 7F is real-time universal

  const int deviceId = data[2];
  const bool addressed = deviceId == 0x7F ||
                         deviceId == (myDeviceId & 0x7F);
  if (!addressed) return message;
  message.deviceId = deviceId;

  const std::uint8_t subId = data[3];

  // ── MIDI Machine Control ────────────────────────────────────────────────
  if (subId == 0x06) {
    switch (data[4]) {
      case 0x01: message.action = Action::MmcStop; break;
      case 0x02: message.action = Action::MmcPlay; break;
      case 0x03: message.action = Action::MmcPlay; break;   // deferred play
      case 0x09: message.action = Action::MmcPause; break;
      case 0x44: {
        // LOCATE: 44 06 01 hr mn sc fr. The hours byte also carries the frame
        // rate in its top bits, which is not something a cue deck needs -- the
        // position is what matters, and the frames are dropped into it at the
        // rate the message says.
        // TWELVE, not eleven. At eleven the last byte IS the F7
        // terminator, and it was read as the frame count: a truncated
        // LOCATE resolved to 00:01:34.76 instead of being rejected,
        // because 0xF7 & 0x7F is 119 frames. A desk that stutters out a
        // short message would have moved the video five seconds.
        //
        // The sub-command is checked too: 44 06 01 is LOCATE TARGET, the
        // one that carries a timecode. The other LOCATE form (44 01 ...)
        // addresses an information field and its bytes mean something
        // else entirely -- reading those as hours and minutes is how you
        // seek somewhere nobody asked for.
        if (data.size() >= 12 && data[5] == 0x06 && data[6] == 0x01) {
          const int hours = data[7] & 0x1F;
          const int minutes = data[8] & 0x7F;
          const int seconds = data[9] & 0x7F;
          const int frames = data[10] & 0x7F;
          const int rateCode = (data[7] >> 5) & 0x03;
          const double fps = rateCode == 0 ? 24.0
                           : rateCode == 1 ? 25.0
                           : rateCode == 2 ? 29.97 : 30.0;
          message.action = Action::MmcLocate;
          message.locateSeconds = hours * 3600.0 + minutes * 60.0 + seconds +
                                  (fps > 0.0 ? frames / fps : 0.0);
        }
        break;
      }
      default: break;
    }
    return message;
  }

  // ── MIDI Show Control ───────────────────────────────────────────────────
  if (subId != 0x02) return message;
  if (data.size() < 7) return message;
  if (!detail::formatIsForVideo(data[4])) return message;

  const std::uint8_t command = data[5];
  switch (command) {
    case 0x01: message.action = Action::Go; break;
    case 0x02: message.action = Action::Stop; break;
    case 0x03: message.action = Action::Resume; break;
    case 0x05: message.action = Action::Load; break;
    case 0x08: message.action = Action::AllOff; break;
    case 0x0A: message.action = Action::Reset; break;
    default: return message;   // understood protocol, command we do not honour
  }

  // The three optional text fields, in order: cue, list, path. A GO with none
  // of them means "the next cue", which is how a desk drives a straight
  // rundown.
  std::size_t at = 6;
  const std::size_t end = data.size() - 1;   // stop before F7
  message.cue = detail::readField(data, at, end);
  message.cueList = detail::readField(data, at, end);
  message.cuePath = detail::readField(data, at, end);
  return message;
}

// A readable name, for the log and for telling an operator what arrived.
inline const char* actionName(Action action) {
  switch (action) {
    case Action::Go:        return "GO";
    case Action::Stop:      return "STOP";
    case Action::Resume:    return "RESUME";
    case Action::Load:      return "LOAD";
    case Action::AllOff:    return "ALL OFF";
    case Action::Reset:     return "RESET";
    case Action::MmcPlay:   return "MMC PLAY";
    case Action::MmcStop:   return "MMC STOP";
    case Action::MmcPause:  return "MMC PAUSE";
    case Action::MmcLocate: return "MMC LOCATE";
    default:                return "none";
  }
}

}  // namespace deckboy::showcontrol
