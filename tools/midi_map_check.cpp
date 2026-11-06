// The MIDI map's engine, without a MIDI port: matching, value mapping, the
// knob's centre, encoders, 14-bit pairs, holds, pickup, coalescing, play rows,
// OSC, overlaps and the saved form. CTest: midi_map.
#include "core/midi_map.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace deckboy::midimap;

static int failures = 0;

static void expect(bool ok, const std::string& what) {
  std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) ++failures;
}

static Message cc(int number, int value, int channel = 1) {
  Message m;
  m.source = Source::Cc;
  m.number = number;
  m.value = value;
  m.channel = channel;
  return m;
}
static Message note(int number, int velocity, int channel = 1) {
  Message m;
  m.source = Source::Note;
  m.number = number;
  m.value = velocity;
  m.on = velocity > 0;
  m.channel = channel;
  return m;
}

static std::string only(const std::vector<Action>& actions) {
  std::string s;
  for (const Action& a : actions) {
    if (!s.empty()) s += " | ";
    s += a.command.empty() ? "(" + a.note + ")" : a.command;
  }
  return s;
}

int main() {
  // Today's fixed behaviour, as rows.
  {
    Engine e;
    e.setRows({}, builtInRows());
    expect(only(e.process({cc(20, 64)}, 0)) == "SPEED 1.00", "CC 20 at the centre is exactly 1.0x");
    expect(only(e.process({cc(20, 0)}, 0)) == "SPEED 0.50", "CC 20 at 0 is 0.5x, as before");
    expect(only(e.process({cc(20, 127)}, 0)) == "SPEED 2.00", "CC 20 at 127 is 2.0x, as before");
    expect(only(e.process({cc(7, 127)}, 0)) == "MASTERVOL 200", "CC 7 full is 200%");
    expect(only(e.process({cc(7, 64)}, 0)) == "MASTERVOL 100", "CC 7 half is 100%");
    expect(only(e.process({note(5, 100)}, 0)) == "MIDINOTE 5", "note N goes the old way (cue N+1)");
    expect(e.process({note(5, 100)}, 0, nullptr, {5}).empty(),
           "the note row stands aside when a cue's TRIGGERS took the note");
    expect(e.process({note(5, 0)}, 0).empty(), "a note-off does nothing and says nothing");
    const auto miss = e.process({cc(30, 12)}, 0);
    expect(miss.size() == 1 && miss[0].command.empty() &&
               miss[0].note.find("nothing mapped to CC 30") == 0,
           "an unmapped CC says so: " + (miss.empty() ? std::string() : miss[0].note));
  }

  // The built-in note row is a fallback: a row of its own on a key wins.
  {
    Row mine;
    mine.source = Source::Note;
    mine.number = 43;
    mine.command = "PANIC";
    auto rows = builtInRows();
    Engine e;
    e.setRows({mine}, rows);
    expect(only(e.process({note(43, 100)}, 0)) == "PANIC", "a mapped key does not also take cue 44");
    expect(only(e.process({note(44, 100)}, 0)) == "MIDINOTE 44", "an unmapped key still does");
    expect(!overlaps(mine, rows[0]), "and the fallback is not flagged as an overlap");
  }

  // A fast knob: forty values in one frame are one action, the latest.
  {
    Row r;
    r.source = Source::Cc;
    r.number = 16;
    r.read = Read::Knob;
    r.command = "OPACITY {value}";
    r.from = 0;
    r.to = 100;
    r.target = Target::OnAir;
    r.playlist = 2;
    Engine e;
    e.setRows({}, {r});
    std::vector<Message> burst;
    for (int v = 0; v < 40; ++v) burst.push_back(cc(16, v * 3));
    burst.push_back(note(60, 100));  // a key in the middle is never dropped
    burst.push_back(cc(16, 127));
    const auto acts = e.process(burst, 0);
    int knob = 0;
    std::string last;
    for (const Action& a : acts) {
      if (a.continuous) {
        ++knob;
        last = a.command;
      }
    }
    expect(knob == 1 && last == "OPACITY 100", "a burst of a knob is one action, the last (" + last + ")");
  }

  // Channel and port are matched exactly.
  {
    Row r;
    r.source = Source::Note;
    r.number = 41;
    r.channel = 2;
    r.port = "nanoKONTROL2";
    r.command = "BLACK";
    Engine e;
    e.setRows({}, {r});
    Message m = note(41, 100, 2);
    m.port = "nanoKONTROL2";
    expect(only(e.process({m}, 0)) == "BLACK", "the right port and channel fire");
    m.channel = 1;
    expect(e.process({m}, 0)[0].command.empty(), "another channel does not");
    m.channel = 2;
    m.port = "APC40";
    expect(e.process({m}, 0)[0].command.empty(), "another port does not");
  }

  // Hold to fire.
  {
    Row r;
    r.source = Source::Note;
    r.number = 43;
    r.read = Read::Hold;
    r.holdSeconds = 1.0;
    r.command = "PANIC";
    Engine e;
    e.setRows({}, {r});
    auto a = e.process({note(43, 100)}, 10.0);
    expect(a.size() == 1 && a[0].command.empty() && a[0].note.find("waits") == 0, "a hold waits");
    expect(e.tick(10.5).empty(), "half a second is not a hold");
    expect(only(e.tick(11.0)) == "PANIC", "a second is");
    expect(e.tick(12.0).empty(), "and it fires once");
    e.process({note(43, 0)}, 12.5);
    e.process({note(43, 100)}, 20.0);
    a = e.process({note(43, 0)}, 20.3);
    expect(a.size() == 1 && a[0].note == "released at 0.3 s, nothing done", "let go early, nothing done");
    expect(e.tick(25.0).empty(), "and nothing later either");
  }

  // Press + release, and playing a synth from a keyboard.
  {
    Row r;
    r.source = Source::Note;
    r.number = 36;
    r.numberTo = 96;
    r.channel = 2;
    r.read = Read::Play;
    r.command = "SYNTHNOTEON {note} {velocity}";
    r.releaseCommand = "SYNTHNOTEOFF {note}";
    Engine e;
    e.setRows({r}, {});
    expect(only(e.process({note(60, 100, 2)}, 0)) == "SYNTHNOTEON 60 100", "a key plays the synth");
    expect(only(e.process({note(60, 0, 2)}, 0)) == "SYNTHNOTEOFF 60", "and lets go of it");
    expect(e.process({note(30, 100, 2)}, 0)[0].command.empty(), "below the range does nothing");
  }

  // An endless encoder from where the value already is.
  {
    Row r;
    r.source = Source::Cc;
    r.number = 17;
    r.read = Read::Encoder;
    r.command = "SPEED {value}";
    r.from = 0.25;
    r.hasCentre = true;
    r.centre = 1.0;
    r.to = 4.0;
    Engine e;
    e.setRows({}, {r});
    CurrentValue current = [](Origin, int, const Row&) { return std::optional<double>(1.0); };
    auto a = e.process({cc(17, 1)}, 0, current);
    expect(a.size() == 1 && a[0].value > 1.0 && a[0].value < 1.1, "one step right from 1.0 (" + only(a) + ")");
    a = e.process({cc(17, 127), cc(17, 127)}, 0, current);
    expect(a.size() == 1 && a[0].value < 1.0, "two steps left (" + only(a) + ")");
    expect(encoderDelta(EncoderMode::Auto, 65) == 1 && encoderDelta(EncoderMode::Auto, 63) == -1,
           "auto reads offset encoders");
    expect(encoderDelta(EncoderMode::SignBit, 65) == -1, "sign-bit reads 65 as one step left");
    expect(guessEncoder({65, 65, 66}) == EncoderMode::Offset64 &&
               guessEncoder({1, 1, 127}) == EncoderMode::TwosComplement,
           "learn guesses the kind");
  }

  // 14-bit: CC 1 and CC 33.
  {
    Row r;
    r.source = Source::Cc;
    r.number = 1;
    r.read = Read::Fine;
    r.command = "MASTERVOL {value}";
    r.from = 0;
    r.to = 200;
    r.decimals = 1;
    Engine e;
    e.setRows({}, {r});
    auto a = e.process({cc(1, 64), cc(33, 0)}, 0);
    expect(only(a) == "MASTERVOL 100.0", "MSB 64 + LSB 0 is the centre (" + only(a) + ")");
    a = e.process({cc(1, 127), cc(33, 127)}, 0);
    expect(only(a) == "MASTERVOL 200.0", "both full is the top (" + only(a) + ")");
  }

  // The wheel.
  {
    Row r;
    r.source = Source::PitchBend;
    r.read = Read::Knob;
    r.command = "SEEK {value}";
    r.from = -10;
    r.hasCentre = true;
    r.centre = 0;
    r.to = 10;
    r.decimals = 1;
    r.target = Target::Selected;
    Engine e;
    e.setRows({}, {r});
    Message m;
    m.source = Source::PitchBend;
    m.value = 8192;
    expect(only(e.process({m}, 0)) == "SEEK 0.0", "the wheel at rest is the centre");
    m.value = 0;
    expect(only(e.process({m}, 0)) == "SEEK -10.0", "fully down is the bottom");
  }

  // Pickup: the knob does nothing until it meets the value.
  {
    Row r;
    r.source = Source::Cc;
    r.number = 16;
    r.read = Read::Knob;
    r.command = "SPEED {value}";
    r.from = 0.25;
    r.hasCentre = true;
    r.centre = 1.0;
    r.to = 4.0;
    r.pickup = true;
    Engine e;
    e.setRows({}, {r});
    CurrentValue current = [](Origin, int, const Row&) { return std::optional<double>(2.5); };
    auto a = e.process({cc(16, 64)}, 0, current);
    expect(a.size() == 1 && a[0].command.empty() && a[0].note.find("pickup: it is 2.50") == 0,
           "not caught: " + only(a));
    a = e.process({cc(16, 110)}, 0, current);
    expect(a.size() == 1 && !a[0].command.empty(), "passing the value catches it (" + only(a) + ")");
  }

  // OSC in the same map.
  {
    Row r;
    r.source = Source::Osc;
    r.oscAddress = "/deck/speed";
    r.read = Read::Knob;
    r.command = "SPEED {value}";
    r.from = 0.5;
    r.to = 2.0;
    Engine e;
    e.setRows({}, {r});
    Message m;
    m.source = Source::Osc;
    m.oscAddress = "/DECK/speed/";
    m.hasOscValue = true;
    m.oscValue = 1.0;
    expect(only(e.process({m}, 0)) == "SPEED 2.00", "an OSC fader, address in any case");
    Row b;
    b.source = Source::Osc;
    b.oscAddress = "/go";
    b.command = "GO";
    e.setRows({b}, {});
    Message g;
    g.source = Source::Osc;
    g.oscAddress = "/go";
    expect(only(e.process({g}, 0)) == "GO", "an OSC button");
  }

  // Aftertouch: the channel's pressure, and one key's.
  {
    Row r;
    r.source = Source::Pressure;
    r.read = Read::Knob;
    r.command = "OPACITY {value}";
    r.from = 0;
    r.to = 100;
    Row k;
    k.source = Source::PolyPressure;
    k.number = 60;
    k.read = Read::Knob;
    k.command = "MASTERVOL {value}";
    k.from = 0;
    k.to = 200;
    Engine e;
    e.setRows({}, {r, k});
    Message m;
    m.source = Source::Pressure;
    m.value = 127;
    expect(only(e.process({m}, 0)) == "OPACITY 100", "full aftertouch is the top");
    m.value = 0;
    expect(only(e.process({m}, 0)) == "OPACITY 0", "none is the bottom");
    Message pk;
    pk.source = Source::PolyPressure;
    pk.number = 60;
    pk.value = 127;
    expect(only(e.process({pk}, 0)) == "MASTERVOL 200", "one key's pressure");
    pk.number = 61;
    expect(e.process({pk}, 0)[0].command.empty(), "another key's does not");
    auto back = rowFromLine(rowToLine(k));
    expect(back && back->source == Source::PolyPressure && back->number == 60, "and it saves");
  }

  // Overlaps are found; a cue's own note is found.
  {
    Row a;
    a.source = Source::Note;
    a.number = 36;
    a.command = "GOTO 4";
    Row b = a;
    b.number = 30;
    b.numberTo = 40;
    Row c = a;
    c.number = 50;
    expect(overlaps(a, b) && !overlaps(a, c), "two rows on one note overlap, others do not");
    expect(rowClaimsNote(a, 36) && !rowClaimsNote(builtInRows()[0], 36),
           "a row claims a TRIGGERS note; the built-in note row does not");
  }

  // The saved form: every field, a command holding a tab, and an old reader
  // ignoring a key it does not know.
  {
    Row r;
    r.port = "nanoKONTROL2";
    r.source = Source::Cc;
    r.channel = 3;
    r.number = 16;
    r.read = Read::Encoder;
    r.encoder = EncoderMode::Offset64;
    r.command = "TEXT SET a\tb {value}";
    r.from = 0.25;
    r.hasCentre = true;
    r.centre = 1.0;
    r.to = 4.0;
    r.pickup = true;
    r.target = Target::Named;
    r.cue = "Intro";
    r.feedback = false;
    const std::string line = rowToLine(r) + "\tfuture=1";
    auto back = rowFromLine(line);
    expect(back && rowToLine(*back) == rowToLine(r), "a row survives saving: " + rowToLine(r));
    expect(!rowFromLine("src=cc\tnum=4"), "a row with no command is refused");
    expect(!rowFromLine("src=banana\tcmd=GO"), "an unknown source is refused");
  }

  // The ready-made maps: every row saves and comes back the same, and no
  // two rows of one map answer the same message.
  for (const auto& map : controllerMaps()) {
    bool saved = true;
    int clashes = 0;
    for (std::size_t i = 0; i < map.rows.size(); ++i) {
      auto back = rowFromLine(rowToLine(map.rows[i]));
      if (!back || rowToLine(*back) != rowToLine(map.rows[i])) saved = false;
      for (std::size_t j = i + 1; j < map.rows.size(); ++j) {
        if (overlaps(map.rows[i], map.rows[j])) ++clashes;
      }
    }
    expect(saved && clashes == 0 && !map.rows.empty(),
           std::string(map.label) + ": " + std::to_string(map.rows.size()) + " rows, " +
             std::to_string(clashes) + " clashes");
  }

  std::printf("midi_map: %d failure%s\n", failures, failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
