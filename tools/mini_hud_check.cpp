// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#include "mini/mini_hud.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool value, const char* name) {
  ++checks;
  if (!value) throw std::runtime_error(name);
}
}

int main(int argc, char** argv) {
  try {
    using mini::Containment;
    for (const char* bad : {"", "0", "-1", "+1", "1x", " 1", "4294967296"})
      check(Containment::odds(bad) == Containment::kOdds, "bad odds must retain production rate");
    check(Containment::odds(nullptr) == 10000000, "production odds");
    check(Containment::odds("1") == 1, "force override");
    check(Containment::odds("100") == 100, "test denominator");
    Containment c;
    c.begin(0, 21, 1);
    check(c.status() == "gremlin containment: nominal", "starts nominal");
    check(!c.advance(0.999), "no roll before one second");
    check(!c.advance(-1.0) && !c.advance(std::nan("")), "invalid times ignored");
    check(c.advance(1.0) && c.count() == 1 && c.active(), "forced breach at one second");
    check(c.status().find("BREACHED") != std::string::npos, "breach status");
    for (int frame = 1; frame < 360; ++frame) {
      check(!c.advance(1.0 + frame / 60.0), "active escape never retriggers");
      check(c.count() == 1, "frame-rate independent count");
    }
    check(c.ribbon(1.1, 56) != c.ribbon(2.0, 56), "gremlin moves");
    check(c.ribbon(4.7, 56).find(c.catcher()) != std::string::npos, "catcher enters");
    check(c.ribbon(5.0, 56).find("[o_o]") != std::string::npos, "visible catch");
    for (int width : {0, 1, 3, 10, 56, 100}) {
      for (int frame = 0; frame <= 60; ++frame)
        check(c.ribbon(1.0 + frame / 10.0, width).size() == static_cast<std::size_t>(width),
              "animation stays within its ribbon");
    }
    check(!c.advance(7.0) && !c.active(), "catch ends after six seconds");
    check(c.status().find(std::string("re-contained by ") + c.catcher()) != std::string::npos,
          "named catcher in completion");
    check(c.ribbon(7.0, 56).find("1 escapes on record") != std::string::npos, "completion count");
    check(c.advance(8.0) && c.count() == 2, "later escape increments");
    c.begin(std::numeric_limits<std::uint64_t>::max(), 1, 1);
    c.advance(1.0);
    check(c.count() == std::numeric_limits<std::uint64_t>::max(), "counter does not wrap");

    bool boy = false, girl = false;
    for (unsigned seed = 0; seed < 100; ++seed) {
      c.begin(0, seed, 1);
      c.advance(1.0);
      boy |= std::string(c.catcher()) == "Deckboy";
      girl |= std::string(c.catcher()) == "Deckgirl";
    }
    check(boy && girl, "both catchers reachable without show settings");
    // Same random stream, sampled at 10 and 60 fps, must give the same events.
    Containment slow, fast;
    slow.begin(0, 567, 10);
    fast.begin(0, 567, 10);
    for (int second = 1; second <= 3600; ++second) {
      for (int tick = 1; tick <= 60; ++tick) fast.advance(second - 1 + tick / 60.0);
      for (int tick = 1; tick <= 10; ++tick) slow.advance(second - 1 + tick / 10.0);
      check(fast.count() == slow.count() && fast.active() == slow.active(), "rate independent trials");
    }
    c.begin(0, 1, 1);
    c.advance(1000000000.0);
    check(c.count() == 1, "resume has no backlog");

    if (argc > 1) {
      const dbfs::path dir(argv[1]);
      dbfs::create_directories(dir);
      const auto file = dir / "mini-containment";
      check(Containment::save(file, 4), "save first count");
      check(Containment::load(file) == 4, "load first count");
      check(Containment::save(file, 5), "replace existing count");
      check(Containment::load(file) == 5, "load replacement");
      c.begin(Containment::load(file), 1, 1);
      check(!c.active() && c.status().find("re-contained") != std::string::npos, "restart settled");
      c.advance(1.0);
      check(c.count() == 6, "restart continues counter");
      // An unwriteable temporary leaves the last complete record intact.
      const auto temporary = dir / "mini-containment.saving";
      dbfs::create_directory(temporary);
      check(!Containment::save(file, 100), "failed save reported");
      check(Containment::load(file) == 5, "failed save preserves count");
      dbfs::remove(temporary);
      for (const char* invalid : {"", "-1\n", "4junk\n", "3", "18446744073709551616\n",
                                  "111111111111111111111111111111111111111111111111111111\n"}) {
        { std::ofstream out(file, std::ios::binary); out << invalid; }
        check(Containment::load(file) == 0, "corrupt record rejected");
      }
      check(Containment::save(file, std::numeric_limits<std::uint64_t>::max()), "save max counter");
      check(Containment::load(file) == std::numeric_limits<std::uint64_t>::max(), "load max counter");
    }
    std::cout << "mini HUD: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "mini HUD failed after " << checks << " checks: " << e.what() << '\n';
    return 1;
  }
}
