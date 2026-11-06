#pragma once
// THE MIDI MAP: any key, pad, knob, fader, wheel or OSC address runs any
// command line, aimed at the cue the operator chooses.
//
// James, 2026-11-05 (board 88SSWvGzsmdtNBskdW579e, approved): "can we make
// midi mappable to any command?" -- "it needs to be more robust". A row says
// what comes in (a note, a CC, the pitch wheel, a program change or an OSC
// address, on a port and channel), how its value is read (a knob, an endless
// encoder, a 14-bit fader, a press, a hold, a press-and-release, or played
// like a keyboard), what command it runs (the same lines a DASH button or
// Companion sends, with {value} and friends filled in) and what that command
// is aimed at (the desk, the selected cue, what is on air in a playlist, or
// one named cue).
//
// Two maps are active at once, as Resolume does it: the COMPUTER's (saved
// with this machine, because the controller is plugged into it) and the
// SHOW's (rows that travel with the show file).
//
// SDL-free and header-only, so tools/midi_map_check.cpp can drive the whole
// engine -- matching, value mapping, encoders, 14-bit pairs, holds, pickup and
// coalescing -- without a MIDI port.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace deckboy::midimap {

// Pressure is aftertouch: one value for the channel, or one per key (poly).
enum class Source { Note, Cc, PitchBend, Program, Osc, Pressure, PolyPressure };
enum class Read { Knob, Encoder, Fine, Press, Hold, PressRelease, Play };
enum class Target { Desk, Selected, OnAir, Named };
// How an endless encoder says "one step left / right". Auto reads the three
// common kinds from the value itself (see encoderDelta).
enum class EncoderMode { Auto, TwosComplement, Offset64, SignBit };

struct Row {
  bool enabled = true;
  bool builtIn = false;        // today's fixed behaviour, now an ordinary row
  bool unclaimed = false;      // a fallback: only when no cue's TRIGGERS and no other row answers
  std::string port;            // "" = any port. A choice, never a guess.
  Source source = Source::Note;
  int channel = 0;             // 0 = any, 1-16 as operators count them
  int number = -1;             // note / CC / program; -1 = any
  int numberTo = -1;           // a range of notes (a keyboard), inclusive
  std::string oscAddress;
  Read read = Read::Press;
  double holdSeconds = 1.0;
  EncoderMode encoder = EncoderMode::Auto;
  std::string command;         // {value} {note} {note+1} {velocity} {channel}
  std::string releaseCommand;  // press+release and play
  double from = 0.0;
  double to = 127.0;
  bool hasCentre = false;      // the knob's centre lands exactly here
  double centre = 0.0;
  int decimals = -1;           // -1: two when the range is small, else none
  bool pickup = false;
  Target target = Target::Desk;
  int playlist = 0;            // 1-based; 0 = the focused one
  std::string cue;             // Target::Named: a cue's name, number or id
  bool feedback = true;        // send its state back to the controller's LEDs
};

// One message from a controller, as the engine reads it.
struct Message {
  Source source = Source::Note;
  int channel = 1;             // 1-16
  int number = 0;              // note / CC / program
  int value = 0;               // velocity, CC value, or 0-16383 for the wheel
  bool on = true;              // notes: on (velocity > 0) or off
  std::string port;
  std::string oscAddress;
  bool hasOscValue = false;
  double oscValue = 0.0;       // the first numeric argument
};

// ── Text forms ────────────────────────────────────────────────────────────

inline const char* sourceToken(Source s) {
  switch (s) {
    case Source::Cc: return "cc";
    case Source::PitchBend: return "pitch";
    case Source::Program: return "program";
    case Source::Osc: return "osc";
    case Source::Pressure: return "pressure";
    case Source::PolyPressure: return "polypressure";
    case Source::Note: break;
  }
  return "note";
}
inline std::optional<Source> sourceFromToken(const std::string& t) {
  if (t == "note") return Source::Note;
  if (t == "cc") return Source::Cc;
  if (t == "pitch") return Source::PitchBend;
  if (t == "program") return Source::Program;
  if (t == "osc") return Source::Osc;
  if (t == "pressure") return Source::Pressure;
  if (t == "polypressure") return Source::PolyPressure;
  return std::nullopt;
}
inline const char* readToken(Read r) {
  switch (r) {
    case Read::Encoder: return "encoder";
    case Read::Fine: return "fine";
    case Read::Press: return "press";
    case Read::Hold: return "hold";
    case Read::PressRelease: return "pressrelease";
    case Read::Play: return "play";
    case Read::Knob: break;
  }
  return "knob";
}
inline std::optional<Read> readFromToken(const std::string& t) {
  if (t == "knob") return Read::Knob;
  if (t == "encoder") return Read::Encoder;
  if (t == "fine") return Read::Fine;
  if (t == "press") return Read::Press;
  if (t == "hold") return Read::Hold;
  if (t == "pressrelease") return Read::PressRelease;
  if (t == "play") return Read::Play;
  return std::nullopt;
}
// What the MAP page calls each way of reading.
inline const char* readLabel(Read r) {
  switch (r) {
    case Read::Encoder: return "endless encoder";
    case Read::Fine: return "14-bit";
    case Read::Press: return "press";
    case Read::Hold: return "hold";
    case Read::PressRelease: return "press + release";
    case Read::Play: return "play";
    case Read::Knob: break;
  }
  return "knob 0-127";
}
inline const char* targetToken(Target t) {
  switch (t) {
    case Target::Selected: return "selected";
    case Target::OnAir: return "onair";
    case Target::Named: return "cue";
    case Target::Desk: break;
  }
  return "desk";
}
inline std::optional<Target> targetFromToken(const std::string& t) {
  if (t == "desk") return Target::Desk;
  if (t == "selected") return Target::Selected;
  if (t == "onair") return Target::OnAir;
  if (t == "cue") return Target::Named;
  return std::nullopt;
}
inline const char* encoderToken(EncoderMode e) {
  switch (e) {
    case EncoderMode::TwosComplement: return "twos";
    case EncoderMode::Offset64: return "offset";
    case EncoderMode::SignBit: return "signbit";
    case EncoderMode::Auto: break;
  }
  return "auto";
}
inline std::optional<EncoderMode> encoderFromToken(const std::string& t) {
  if (t == "auto") return EncoderMode::Auto;
  if (t == "twos") return EncoderMode::TwosComplement;
  if (t == "offset") return EncoderMode::Offset64;
  if (t == "signbit") return EncoderMode::SignBit;
  return std::nullopt;
}

inline bool isContinuous(Read r) {
  return r == Read::Knob || r == Read::Encoder || r == Read::Fine;
}
// Sources that only ever carry a value, never a press.
inline bool continuousSource(Source s) {
  return s == Source::PitchBend || s == Source::Pressure || s == Source::PolyPressure;
}

static const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F",
                                           "F#", "G", "G#", "A", "A#", "B"};
inline std::string noteName(int note) {
  if (note < 0 || note > 127) return "?";
  return std::string(kNoteNames[note % 12]) + std::to_string(note / 12 - 1);
}

// "ch 1 · CC 16", "any · note 36-60", "OSC /deckboy/speed". The MAP page's
// left column and the monitor's "map row" both use it.
inline std::string describeSource(const Row& row) {
  if (row.source == Source::Osc) {
    return "OSC " + (row.oscAddress.empty() ? std::string("(any)") : row.oscAddress);
  }
  std::string s = row.channel <= 0 ? std::string("any") : "ch " + std::to_string(row.channel);
  s += " \xC2\xB7 ";
  switch (row.source) {
    case Source::Note:
      if (row.number < 0) {
        s += "note 0-127";
      } else if (row.numberTo > row.number) {
        s += "note " + std::to_string(row.number) + "-" + std::to_string(row.numberTo);
      } else {
        s += "note " + std::to_string(row.number);
      }
      break;
    case Source::Cc:
      s += row.number < 0 ? std::string("CC any") : "CC " + std::to_string(row.number);
      if (row.read == Read::Fine && row.number >= 0 && row.number < 32) {
        s += "+" + std::to_string(row.number + 32);
      }
      break;
    case Source::PitchBend: s += "pitch bend"; break;
    case Source::Pressure: s += "aftertouch"; break;
    case Source::PolyPressure:
      if (row.number < 0) {
        s += "key pressure, any key";
      } else if (row.numberTo > row.number) {
        s += "key pressure " + std::to_string(row.number) + "-" + std::to_string(row.numberTo);
      } else {
        s += "key pressure " + std::to_string(row.number);
      }
      break;
    case Source::Program:
      s += row.number < 0 ? std::string("program any") : "program " + std::to_string(row.number);
      break;
    case Source::Osc: break;
  }
  return s;
}

inline std::string describeMessage(const Message& m) {
  switch (m.source) {
    case Source::Osc: {
      std::string s = "OSC " + m.oscAddress;
      if (m.hasOscValue) {
        char buf[32];
        std::snprintf(buf, sizeof buf, " %.3g", m.oscValue);
        s += buf;
      }
      return s;
    }
    case Source::Note:
      return std::string(m.on ? "note " : "note off ") + std::to_string(m.number) + " (" +
             noteName(m.number) + ")" + (m.on ? " vel " + std::to_string(m.value) : std::string()) +
             " \xC2\xB7 ch " + std::to_string(m.channel);
    case Source::Cc:
      return "CC " + std::to_string(m.number) + " = " + std::to_string(m.value) +
             " \xC2\xB7 ch " + std::to_string(m.channel);
    case Source::PitchBend:
      return "pitch bend " + std::to_string(m.value - 8192) + " \xC2\xB7 ch " + std::to_string(m.channel);
    case Source::Program:
      return "program " + std::to_string(m.number) + " \xC2\xB7 ch " + std::to_string(m.channel);
    case Source::Pressure:
      return "aftertouch " + std::to_string(m.value) + " \xC2\xB7 ch " + std::to_string(m.channel);
    case Source::PolyPressure:
      return "key pressure " + std::to_string(m.number) + " (" + noteName(m.number) + ") = " +
             std::to_string(m.value) + " \xC2\xB7 ch " + std::to_string(m.channel);
  }
  return {};
}

// ── Saving ────────────────────────────────────────────────────────────────
// One row per line: key=value fields separated by tabs, values escaped so a
// command line may hold anything. Unknown keys are ignored, so a newer row
// read by an older Deckboy keeps what it understands.

inline std::string escapeValue(const std::string& v) {
  std::string out;
  for (char c : v) {
    if (c == '\\') out += "\\\\";
    else if (c == '\t') out += "\\t";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}
inline std::string unescapeValue(const std::string& v) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '\\' && i + 1 < v.size()) {
      const char n = v[++i];
      out += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
    } else {
      out += v[i];
    }
  }
  return out;
}

inline std::string formatNumber(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.6g", v);
  return buf;
}

inline std::string rowToLine(const Row& r) {
  std::string s;
  auto add = [&](const char* k, const std::string& v) {
    if (!s.empty()) s += '\t';
    s += k;
    s += '=';
    s += escapeValue(v);
  };
  add("src", sourceToken(r.source));
  if (!r.enabled) add("on", "0");
  if (r.builtIn) add("builtin", "1");
  if (r.unclaimed) add("unclaimed", "1");
  if (!r.port.empty()) add("port", r.port);
  if (r.channel > 0) add("ch", std::to_string(r.channel));
  if (r.number >= 0) add("num", std::to_string(r.number));
  if (r.numberTo >= 0) add("to_num", std::to_string(r.numberTo));
  if (!r.oscAddress.empty()) add("osc", r.oscAddress);
  add("read", readToken(r.read));
  if (r.read == Read::Hold) add("hold", formatNumber(r.holdSeconds));
  if (r.read == Read::Encoder) add("enc", encoderToken(r.encoder));
  add("cmd", r.command);
  if (!r.releaseCommand.empty()) add("release", r.releaseCommand);
  if (isContinuous(r.read) || r.source == Source::Osc || continuousSource(r.source)) {
    add("from", formatNumber(r.from));
    add("to", formatNumber(r.to));
    if (r.hasCentre) add("centre", formatNumber(r.centre));
    if (r.decimals >= 0) add("dec", std::to_string(r.decimals));
    if (r.pickup) add("pickup", "1");
  }
  add("target", targetToken(r.target));
  if (r.playlist > 0) add("playlist", std::to_string(r.playlist));
  if (!r.cue.empty()) add("cue", r.cue);
  if (!r.feedback) add("feedback", "0");
  return s;
}

inline std::optional<Row> rowFromLine(const std::string& line) {
  Row r;
  bool sawSource = false;
  std::size_t start = 0;
  while (start <= line.size()) {
    std::size_t end = line.find('\t', start);
    if (end == std::string::npos) end = line.size();
    const std::string field = line.substr(start, end - start);
    start = end + 1;
    const std::size_t eq = field.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = field.substr(0, eq);
    const std::string v = unescapeValue(field.substr(eq + 1));
    const double d = std::atof(v.c_str());
    const int i = std::atoi(v.c_str());
    if (k == "src") {
      auto s = sourceFromToken(v);
      if (!s) return std::nullopt;
      r.source = *s;
      sawSource = true;
    } else if (k == "on") r.enabled = i != 0;
    else if (k == "builtin") r.builtIn = i != 0;
    else if (k == "unclaimed") r.unclaimed = i != 0;
    else if (k == "port") r.port = v;
    else if (k == "ch") r.channel = std::clamp(i, 0, 16);
    else if (k == "num") r.number = std::clamp(i, -1, 127);
    else if (k == "to_num") r.numberTo = std::clamp(i, -1, 127);
    else if (k == "osc") r.oscAddress = v;
    else if (k == "read") {
      if (auto x = readFromToken(v)) r.read = *x;
    } else if (k == "hold") r.holdSeconds = std::clamp(d, 0.1, 10.0);
    else if (k == "enc") {
      if (auto x = encoderFromToken(v)) r.encoder = *x;
    } else if (k == "cmd") r.command = v;
    else if (k == "release") r.releaseCommand = v;
    else if (k == "from") r.from = d;
    else if (k == "to") r.to = d;
    else if (k == "centre") { r.hasCentre = true; r.centre = d; }
    else if (k == "dec") r.decimals = std::clamp(i, 0, 6);
    else if (k == "pickup") r.pickup = i != 0;
    else if (k == "target") {
      if (auto x = targetFromToken(v)) r.target = *x;
    } else if (k == "playlist") r.playlist = std::max(0, i);
    else if (k == "cue") r.cue = v;
    else if (k == "feedback") r.feedback = i != 0;
    if (end == line.size()) break;
  }
  if (!sawSource || r.command.empty()) return std::nullopt;
  return r;
}

// ── Values ────────────────────────────────────────────────────────────────

// A 7-bit position with 64 as the exact centre: 0..64 is the lower half and
// 64..127 the upper. The old CC 20 read value*1.5/127 and put 1.0x at 42.
inline double position7(int v) {
  v = std::clamp(v, 0, 127);
  return v <= 64 ? v / 128.0 : 0.5 + (v - 64) / 126.0;
}
// The wheel's 14 bits, centre 8192.
inline double position14(int v) {
  v = std::clamp(v, 0, 16383);
  return v <= 8192 ? v / 16384.0 : 0.5 + (v - 8192) / 16382.0;
}

// Position 0..1 -> the command's number, through the centre when there is one.
inline double mapPosition(const Row& r, double p) {
  p = std::clamp(p, 0.0, 1.0);
  if (r.hasCentre) {
    return p <= 0.5 ? r.from + (r.centre - r.from) * (p / 0.5)
                    : r.centre + (r.to - r.centre) * ((p - 0.5) / 0.5);
  }
  return r.from + (r.to - r.from) * p;
}
// And back: where a value sits on the knob (pickup and the LED ring).
inline double positionOf(const Row& r, double value) {
  auto frac = [](double v, double a, double b) {
    return std::abs(b - a) < 1e-12 ? 0.0 : (v - a) / (b - a);
  };
  double p;
  if (r.hasCentre) {
    const bool lower = (r.centre >= r.from) ? value <= r.centre : value >= r.centre;
    p = lower ? 0.5 * frac(value, r.from, r.centre) : 0.5 + 0.5 * frac(value, r.centre, r.to);
  } else {
    p = frac(value, r.from, r.to);
  }
  return std::clamp(p, 0.0, 1.0);
}
// 7-bit value for a position: the inverse of position7, for the LED ring.
inline int value7(double p) {
  p = std::clamp(p, 0.0, 1.0);
  return p <= 0.5 ? static_cast<int>(std::lround(p * 128.0))
                  : 64 + static_cast<int>(std::lround((p - 0.5) * 126.0));
}

inline int decimalsFor(const Row& r) {
  if (r.decimals >= 0) return r.decimals;
  const double span = std::max({std::abs(r.to - r.from), std::abs(r.from), std::abs(r.to)});
  return span <= 10.0 ? 2 : 0;
}
inline std::string formatValue(const Row& r, double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.*f", decimalsFor(r), v);
  std::string s = buf;
  if (s == "-0" || s == "-0.0" || s == "-0.00") s.erase(0, 1);
  return s;
}

// One step of an endless encoder. Auto reads the two common kinds from the
// value: around 64 is "offset" (65 = +1, 63 = -1), near the ends is two's
// complement (1 = +1, 127 = -1). Sign-bit (65 = -1) only when chosen, because
// it reads 65 the opposite way to offset and no value can tell them apart.
inline int encoderDelta(EncoderMode mode, int v) {
  v = std::clamp(v, 0, 127);
  switch (mode) {
    case EncoderMode::TwosComplement: return v < 64 ? v : v - 128;
    case EncoderMode::Offset64: return v - 64;
    case EncoderMode::SignBit: return v < 64 ? v : -(v - 64);
    case EncoderMode::Auto:
      if (v >= 1 && v <= 15) return v;
      if (v >= 113) return v - 128;
      if (v >= 49 && v <= 79) return v - 64;
      return 0;
  }
  return 0;
}
// What learn guesses from the first few turns.
inline EncoderMode guessEncoder(const std::vector<int>& values) {
  int nearMiddle = 0;
  int nearEnds = 0;
  for (int v : values) {
    if (v >= 49 && v <= 79) ++nearMiddle;
    else if (v <= 15 || v >= 113) ++nearEnds;
  }
  if (nearMiddle > nearEnds) return EncoderMode::Offset64;
  if (nearEnds > 0) return EncoderMode::TwosComplement;
  return EncoderMode::Auto;
}

inline std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  if (from.empty()) return s;
  std::size_t at = 0;
  while ((at = s.find(from, at)) != std::string::npos) {
    s.replace(at, from.size(), to);
    at += to.size();
  }
  return s;
}

inline std::string expandCommand(const std::string& command, const Message& m,
                                 const std::string& value) {
  std::string s = command;
  s = replaceAll(s, "{value}", value);
  s = replaceAll(s, "{note+1}", std::to_string(m.number + 1));
  s = replaceAll(s, "{note}", std::to_string(m.number));
  s = replaceAll(s, "{number}", std::to_string(m.number));
  s = replaceAll(s, "{velocity}", std::to_string(m.value));
  s = replaceAll(s, "{channel}", std::to_string(m.channel));
  return s;
}

inline std::string normalizedOsc(std::string a) {
  while (!a.empty() && (a.back() == ' ' || a.back() == '/')) a.pop_back();
  if (!a.empty() && a.front() != '/') a.insert(a.begin(), '/');
  for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return a;
}

inline bool portMatches(const std::string& rowPort, const std::string& msgPort) {
  return rowPort.empty() || rowPort == msgPort;
}

// Does this row answer this message? For a 14-bit row the LSB's CC (n+32)
// answers too.
inline bool matches(const Row& r, const Message& m) {
  if (!r.enabled) return false;
  if (r.source == Source::Osc || m.source == Source::Osc) {
    return r.source == Source::Osc && m.source == Source::Osc &&
           (r.oscAddress.empty() || normalizedOsc(r.oscAddress) == normalizedOsc(m.oscAddress));
  }
  if (!portMatches(r.port, m.port)) return false;
  if (r.channel > 0 && r.channel != m.channel) return false;
  if (r.source != m.source) return false;
  if (r.source == Source::PitchBend || r.source == Source::Pressure) return true;
  if (r.number < 0) return true;
  if (r.source == Source::Cc && r.read == Read::Fine && r.number < 32 && m.number == r.number + 32) {
    return true;
  }
  const int hi = r.numberTo > r.number ? r.numberTo : r.number;
  return m.number >= r.number && m.number <= hi;
}

// Two rows that answer the same message: the MAP page outlines both in amber.
inline bool overlaps(const Row& a, const Row& b) {
  if (!a.enabled || !b.enabled || a.source != b.source) return false;
  if (a.unclaimed || b.unclaimed) return false;  // a fallback stands aside by design
  if (a.source == Source::Osc) {
    return a.oscAddress.empty() || b.oscAddress.empty() ||
           normalizedOsc(a.oscAddress) == normalizedOsc(b.oscAddress);
  }
  if (!a.port.empty() && !b.port.empty() && a.port != b.port) return false;
  if (a.channel > 0 && b.channel > 0 && a.channel != b.channel) return false;
  if (a.source == Source::PitchBend || a.source == Source::Pressure) return true;
  if (a.number < 0 || b.number < 0) return true;
  const int aHi = std::max(a.number, a.numberTo);
  const int bHi = std::max(b.number, b.numberTo);
  return a.number <= bHi && b.number <= aHi;
}

// Does a row answer this note (for flagging a cue's own TRIGGERS note)?
inline bool rowClaimsNote(const Row& r, int note) {
  if (!r.enabled || r.source != Source::Note || r.unclaimed) return false;
  if (r.number < 0) return true;
  return note >= r.number && note <= std::max(r.number, r.numberTo);
}

// Today's fixed behaviour, as rows anyone can change or delete. CC 20's 1.0x
// now sits at the knob's centre (James, 2026-11-05: "move").
inline std::vector<Row> builtInRows() {
  std::vector<Row> rows;
  Row note;
  note.builtIn = true;
  note.unclaimed = true;
  note.source = Source::Note;
  note.read = Read::Press;
  note.command = "MIDINOTE {note}";  // quiet when there is no cue N+1
  note.target = Target::Desk;
  rows.push_back(note);
  Row vol;
  vol.builtIn = true;
  vol.source = Source::Cc;
  vol.number = 7;
  vol.read = Read::Knob;
  vol.command = "MASTERVOL {value}";
  vol.from = 0.0;
  vol.to = 200.0;
  vol.decimals = 0;
  rows.push_back(vol);
  Row speed;
  speed.builtIn = true;
  speed.source = Source::Cc;
  speed.number = 20;
  speed.read = Read::Knob;
  speed.command = "SPEED {value}";
  speed.from = 0.5;
  speed.hasCentre = true;
  speed.centre = 1.0;
  speed.to = 2.0;
  speed.decimals = 2;
  speed.target = Target::Selected;
  rows.push_back(speed);
  return rows;
}

// ── The engine ────────────────────────────────────────────────────────────

enum class Origin { Show, Computer };

struct Action {
  Origin origin = Origin::Computer;
  int row = -1;                // index within its map
  int message = -1;            // which of process()'s messages; -1 from tick()
  std::string command;         // expanded; empty when nothing runs
  std::string note;            // for the monitor: why nothing ran, or what waits
  double value = 0.0;          // continuous rows: the number sent
  bool continuous = false;
};

// What a row's target is worth right now, in the command's own units, when
// the app knows it. Pickup needs it; without it pickup cannot hold the knob.
using CurrentValue = std::function<std::optional<double>(Origin, int row, const Row&)>;

class Engine {
 public:
  // Show rows first, then the computer's -- both answer, as Resolume's two
  // maps do; overlaps are the MAP page's amber, not a silent winner.
  void setRows(std::vector<Row> show, std::vector<Row> computer) {
    show_ = std::move(show);
    computer_ = std::move(computer);
    states_.clear();
  }
  const std::vector<Row>& rows(Origin o) const { return o == Origin::Show ? show_ : computer_; }

  // One frame's messages, in order. Presses are never dropped; a knob that
  // sent forty values this frame produces ONE action, the latest, so a fast
  // fader never backs up the desk. claimedNotes: notes a cue's own TRIGGERS
  // took this frame (the built-in note row stands aside for them).
  std::vector<Action> process(const std::vector<Message>& messages, double nowSec,
                              const CurrentValue& current = nullptr,
                              const std::vector<int>& claimedNotes = {}) {
    std::vector<Action> out;
    std::map<std::pair<int, int>, std::size_t> latest;  // continuous row -> slot in out
    for (int mi = 0; mi < static_cast<int>(messages.size()); ++mi) {
      const Message& m = messages[static_cast<std::size_t>(mi)];
      // A note a cue's own TRIGGERS took is answered, even if no row is.
      bool any = m.source == Source::Note &&
                 std::find(claimedNotes.begin(), claimedNotes.end(), m.number) != claimedNotes.end();
      // Does a row that is not a fallback answer this message? Then the
      // fallbacks (today's "note N takes cue N+1") stand aside, as the old
      // rule only ever applied to a note nothing else claimed.
      bool answered = false;
      for (Origin o : {Origin::Show, Origin::Computer}) {
        for (const Row& r : this->rows(o)) {
          if (!r.unclaimed && matches(r, m)) answered = true;
        }
      }
      for (Origin o : {Origin::Show, Origin::Computer}) {
        const auto& rows = this->rows(o);
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
          const Row& r = rows[static_cast<std::size_t>(i)];
          if (!matches(r, m)) continue;
          if (r.unclaimed &&
              (answered || (m.source == Source::Note &&
                            std::find(claimedNotes.begin(), claimedNotes.end(), m.number) !=
                              claimedNotes.end()))) {
            continue;
          }
          any = true;
          std::optional<Action> a = handle(o, i, r, m, nowSec, current);
          if (!a) continue;
          a->message = mi;
          if (a->continuous) {
            const auto key = std::make_pair(static_cast<int>(o), i);
            auto it = latest.find(key);
            if (it != latest.end()) {
              out[it->second] = *a;
              continue;
            }
            latest[key] = out.size();
          }
          out.push_back(*a);
        }
      }
      if (!any) {
        Action miss;
        miss.message = mi;
        miss.note = "nothing mapped to " + describeMessage(m);
        if (m.source == Source::Note && !m.on) miss.note.clear();  // offs are noise
        if (!miss.note.empty()) out.push_back(miss);
      }
    }
    return out;
  }

  // Holds that have now been held long enough.
  std::vector<Action> tick(double nowSec) {
    std::vector<Action> out;
    for (auto& [key, st] : states_) {
      if (st.holdStart < 0.0 || st.holdFired) continue;
      const Origin o = static_cast<Origin>(key.first);
      const auto& rows = this->rows(o);
      if (key.second >= static_cast<int>(rows.size())) continue;
      const Row& r = rows[static_cast<std::size_t>(key.second)];
      if (nowSec - st.holdStart >= r.holdSeconds) {
        st.holdFired = true;
        Action a;
        a.origin = o;
        a.row = key.second;
        a.command = expandCommand(r.command, st.holdMessage, "");
        a.note = "held " + formatNumber(r.holdSeconds) + " s";
        out.push_back(a);
      }
    }
    return out;
  }

  // Where each continuous row's knob sits (0..1), for the LED ring and the
  // MAP page's "Now" column. -1 until it has moved.
  double position(Origin o, int row) const {
    auto it = states_.find({static_cast<int>(o), row});
    return it == states_.end() ? -1.0 : it->second.pos;
  }
  bool caught(Origin o, int row) const {
    auto it = states_.find({static_cast<int>(o), row});
    return it == states_.end() || it->second.caught;
  }

 private:
  struct State {
    double pos = -1.0;         // last position 0..1 (encoders accumulate here)
    bool caught = false;       // pickup: has the knob met the value yet?
    int msb = -1;
    int lsb = 0;
    double holdStart = -1.0;
    bool holdFired = false;
    Message holdMessage;
  };

  std::optional<Action> handle(Origin o, int i, const Row& r, const Message& m, double now,
                               const CurrentValue& current) {
    State& st = states_[{static_cast<int>(o), i}];
    Action a;
    a.origin = o;
    a.row = i;
    // Keys first.
    if (!isContinuous(r.read) && !continuousSource(r.source)) {
      const bool press = m.source == Source::Note ? m.on
                       : m.source == Source::Cc ? m.value >= 64
                       : m.source == Source::Osc ? (!m.hasOscValue || m.oscValue > 0.5)
                       : true;  // a program change is a press
      switch (r.read) {
        case Read::Hold:
          if (press) {
            st.holdStart = now;
            st.holdFired = false;
            st.holdMessage = m;
            a.note = "waits for a " + formatNumber(r.holdSeconds) + " s hold";
          } else {
            if (st.holdStart >= 0.0 && !st.holdFired) {
              char buf[64];
              std::snprintf(buf, sizeof buf, "released at %.1f s, nothing done", now - st.holdStart);
              a.note = buf;
            } else {
              st.holdStart = -1.0;
              return std::nullopt;
            }
            st.holdStart = -1.0;
          }
          return a;
        case Read::PressRelease:
        case Read::Play:
          if (press) {
            a.command = expandCommand(r.command, m, "");
          } else if (!r.releaseCommand.empty()) {
            a.command = expandCommand(r.releaseCommand, m, "");
          } else {
            return std::nullopt;
          }
          return a;
        default:
          if (!press) return std::nullopt;  // the release of a press does nothing
          a.command = expandCommand(r.command, m, "");
          return a;
      }
    }
    // Continuous.
    double p = st.pos < 0.0 ? 0.5 : st.pos;
    if (m.source == Source::PitchBend) {
      p = position14(m.value);
    } else if (m.source == Source::Osc) {
      p = std::clamp(m.hasOscValue ? m.oscValue : 0.0, 0.0, 1.0);
    } else if (m.source == Source::Pressure || m.source == Source::PolyPressure) {
      // Pressure has no centre: nothing pressed is 0, all the way is 1.
      p = std::clamp(m.value, 0, 127) / 127.0;
    } else if (r.read == Read::Encoder) {
      const int d = encoderDelta(r.encoder, m.value);
      if (d == 0) return std::nullopt;
      if (st.pos < 0.0) {
        const auto start = current ? current(o, i, r) : std::nullopt;
        p = start ? positionOf(r, *start) : 0.5;
      }
      p = std::clamp(p + d / 127.0, 0.0, 1.0);
    } else if (r.read == Read::Fine && r.number >= 0 && r.number < 32) {
      if (m.number == r.number) {
        st.msb = m.value;
        st.lsb = 0;
      } else {
        st.lsb = m.value;
        if (st.msb < 0) return std::nullopt;
      }
      p = position14((st.msb << 7) | st.lsb);
    } else {
      p = position7(m.value);
    }
    const double value = mapPosition(r, p);
    const double previous = st.pos;
    st.pos = p;
    a.continuous = true;
    a.value = value;
    // Pickup: if the value was changed elsewhere, the knob does nothing until
    // it reaches or passes the current value, so touching it never jumps the
    // picture. An encoder starts from the current value, so needs none.
    if (r.pickup && r.read != Read::Encoder && current) {
      const auto live = current(o, i, r);
      if (!live) st.caught = true;  // nothing to catch: it is the value
      if (live) {
        const double target = positionOf(r, *live);
        const double tolerance = 1.5 / 127.0;
        if (!st.caught) {
          const bool crossed = previous >= 0.0 &&
                               ((previous <= target && p >= target) || (previous >= target && p <= target));
          if (crossed || std::abs(p - target) <= tolerance) {
            st.caught = true;
          } else {
            a.note = "pickup: it is " + formatValue(r, *live) + ", the knob is at " +
                     formatValue(r, value) + ", not caught yet";
            return a;
          }
        } else if (std::abs(p - target) > 8.0 / 127.0 && previous >= 0.0 &&
                   std::abs(previous - target) > 8.0 / 127.0) {
          // Changed elsewhere since: catch it again.
          st.caught = false;
          a.note = "pickup: it is " + formatValue(r, *live) + ", the knob is at " +
                   formatValue(r, value) + ", not caught yet";
          return a;
        }
      }
    }
    a.command = expandCommand(r.command, m, formatValue(r, value));
    return a;
  }

  std::vector<Row> show_;
  std::vector<Row> computer_;
  std::map<std::pair<int, int>, State> states_;
};

}  // namespace deckboy::midimap
