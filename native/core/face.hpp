// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.
#pragma once

// DECKBOY'S FACE, AS GEOMETRY. The face that waits in an empty programme
// monitor -- hovering, rocking, blinking, winking, tiring over the hours,
// recoiling from a poke and in the end leaving -- worked out here as boxes
// and stars, so every front end draws the SAME face in its own way: the
// desk with its primitives (drawStartupMascot), Pocket with its own. The
// words it says stay with each front end (each has its own tips), except the
// ones about being poked and about the hours, which are the character's.
//
// Nothing here draws and nothing here needs SDL.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace deckboy::face {

struct Box { int x = 0, y = 0, w = 0, h = 0; };

// How it feels: 0 until three hours of uptime, then cheeky, tired and at
// last stressed by twenty-four. `mellow` (4:20) is the tired face without
// any of the fed-up.
struct Mood {
  double level = 0.0;   // 0-1 over the hours
  double cheek = 0.0, tired = 0.0, stress = 0.0;
  bool mellow = false;
  bool asleep = false;  // dozing: eyes shut, a slow small smile (Pocket, left alone)
};

inline Mood moodFor(double uptimeHours, bool mellow) {
  Mood m;
  m.level = uptimeHours <= 3.0 ? 0.0 : std::clamp((uptimeHours - 3.0) / 21.0, 0.0, 1.0);
  m.cheek = std::clamp(m.level / 0.35, 0.0, 1.0);
  m.tired = std::clamp((m.level - 0.35) / 0.35, 0.0, 1.0);
  m.stress = std::clamp((m.level - 0.70) / 0.30, 0.0, 1.0);
  m.mellow = mellow;
  if (mellow) {
    m.cheek = 0.0;
    m.stress = 0.0;
    m.tired = std::max(m.tired, 0.8);
  }
  return m;
}

// Three stages, earned by poking it: the eyes follow the pointer, then the
// face edges away from it, then it leaves.
inline constexpr int kWatchAfterPokes = 3;
inline constexpr int kShyAfterPokes = 6;
inline constexpr int kFleeAfterPokes = 10;

struct Pokes {
  int count = 0;
  std::uint64_t atMs = 0;        // the last poke
  double dx = 0.0, dy = 0.0;     // where it landed, -1..1 against the face's size
  bool hold = false;             // hold the reaction (screenshots)
  std::uint64_t fleeStartedMs = 0;
  int pointerX = 0, pointerY = 0;
};

struct Star { int x = 0, y = 0, size = 0; std::uint8_t alpha = 0; };

struct Frame {
  bool gone = false;            // it has left: draw nothing
  int unit = 0, thick = 0;
  int cx = 0, cy = 0;           // where it is, for the next poke's hit test
  int radius = 0;               // 0 while leaving: it cannot be poked then
  std::vector<Box> solid;       // eyes, brows, sweat and mouth, in the glow ink
  std::vector<Star> stars;      // the three orbiting stars (alpha varies)
};

// The face in `area` at `nowMs`. unitMin/unitMax bound its size (the desk's
// 18 and 64; a phone, whose pixels are small, may allow more).
inline Frame layout(const Box& area, std::uint64_t nowMs, const Mood& mood, const Pokes& pk,
                    int unitMin = 18, int unitMax = 64) {
  Frame f;
  const double kPi = 3.14159265358979;
  const double cheek = mood.cheek, tired = mood.tired, stress = mood.stress;
  // Everything slows as it tires: the same face, on a heavier clock.
  const double t = static_cast<double>(nowMs) / 1000.0 * (1.0 - 0.35 * tired) * (mood.asleep ? 0.4 : 1.0);
  const int unit = std::clamp(std::min(area.w, area.h) / 6, unitMin, unitMax);
  const int thick = std::max(3, unit / 4);
  f.unit = unit;
  f.thick = thick;

  // The centre, with a gentle overall hover.
  int cx = area.x + area.w / 2 + static_cast<int>(std::lround(std::sin(t * 0.8) * 4.0));
  int cy = area.y + area.h / 2 - area.h / 9 +
           static_cast<int>(std::lround(std::sin(t * 1.3) * 3.5 + std::sin(t * 0.55 + 1.0) * 2.0));

  // IT NOTICES YOU: the eyes follow the pointer, then the face backs off
  // along the line from it (never out of its own area).
  const bool watching = pk.count >= kWatchAfterPokes;
  const bool shy = pk.count >= kShyAfterPokes;
  double lookX = 0.0, lookY = 0.0;
  if (watching) {
    const double dx = static_cast<double>(pk.pointerX - cx);
    const double dy = static_cast<double>(pk.pointerY - cy);
    const double reach = std::max(1.0, static_cast<double>(std::min(area.w, area.h)) * 0.5);
    lookX = std::clamp(dx / reach, -1.0, 1.0);
    lookY = std::clamp(dy / reach, -1.0, 1.0);
    if (shy) {
      const double dist = std::sqrt(lookX * lookX + lookY * lookY);
      const double shove = (1.0 - std::clamp(dist, 0.0, 1.0)) * (unit * 2.2);
      if (dist > 0.001) {
        cx -= static_cast<int>(std::lround(lookX / dist * shove));
        cy -= static_cast<int>(std::lround(lookY / dist * shove));
      }
      const int margin = unit * 3;
      cx = std::clamp(cx, area.x + margin, area.x + area.w - margin);
      cy = std::clamp(cy, area.y + margin, area.y + area.h - margin);
    }
  }
  // IT FELT THAT: it recoils along the line it was pushed, and the part that
  // was hit answers (a poked eye squints, a poked smile goes round).
  constexpr std::uint64_t kReactMs = 520;
  double reactAmt = 0.0;
  if (pk.atMs != 0 && nowMs >= pk.atMs && (pk.hold || nowMs - pk.atMs < kReactMs)) {
    const double u = pk.hold ? 0.5 : static_cast<double>(nowMs - pk.atMs) / kReactMs;
    reactAmt = std::sin(u * kPi) * (1.0 - u * 0.35);   // up fast, down slow
    cx -= static_cast<int>(std::lround(pk.dx * reactAmt * unit * 0.9));
    cy -= static_cast<int>(std::lround(pk.dy * reactAmt * unit * 0.9));
  }
  const bool pokedMouth = reactAmt > 0.01 && pk.dy > 0.25;
  const bool pokedLeftEye = reactAmt > 0.01 && pk.dy <= 0.25 && pk.dx < -0.12;
  const bool pokedRightEye = reactAmt > 0.01 && pk.dy <= 0.25 && pk.dx > 0.12;

  if (pk.fleeStartedMs != 0) {
    // AND THEN IT GOES, off whichever side it was nearest, and once past the
    // edge it is simply not drawn.
    const double flee = static_cast<double>(nowMs - pk.fleeStartedMs) / 1400.0;
    if (flee >= 1.0) {
      f.gone = true;
      return f;
    }
    const double eased = flee * flee * (3.0 - 2.0 * flee);
    const int leftward = (cx < area.x + area.w / 2) ? -1 : 1;
    cx += static_cast<int>(std::lround(eased * leftward * (area.w * 0.75 + unit * 6)));
    cy += static_cast<int>(std::lround(std::sin(eased * kPi) * -unit * 1.5));
  }

  // The whole face rocks a little; every element is placed through that.
  const double theta = std::sin(t * 0.9) * 0.06 + std::sin(t * 1.7 + 0.5) * 0.02;
  const double ca = std::cos(theta), sa = std::sin(theta);
  auto place = [&](double ox, double oy, int& X, int& Y) {
    X = cx + static_cast<int>(std::lround(ox * ca - oy * sa));
    Y = cy + static_cast<int>(std::lround(ox * sa + oy * ca));
  };

  // An eased blink, both eyes.
  const double bp = std::fmod(t, 4.0);
  double blink = 0.0;
  if (bp < 0.22) {
    const double u = bp / 0.22;
    const double s = std::sin(u * kPi);
    blink = s * s * (3.0 - 2.0 * s);
  }
  const double eyeOpen = mood.asleep ? 0.0 : (1.0 - 0.90 * blink) * (1.0 - 0.45 * tired);
  const double smile = 0.55 + 0.45 * std::sin(t * 0.5);
  const int eyeGap = unit * 3;
  const int eyeW = std::max(6, unit * 4 / 5);
  const int eyeHFull = unit * 5 / 4;

  // ONE EYE, ON ITS OWN, EVERY NOW AND THEN: a wink, more often when cheeky.
  const std::uint64_t winkEveryMs = cheek > 0.0 && stress < 0.5 ? static_cast<std::uint64_t>(9000 - 5500 * cheek) : 9000;
  constexpr std::uint64_t kWinkLenMs = 420;
  const std::uint64_t intoWink = nowMs % winkEveryMs;
  double winkMul = 1.0;
  if (intoWink < kWinkLenMs && !mood.asleep) {
    const double w = static_cast<double>(intoWink) / kWinkLenMs;
    winkMul = 1.0 - 0.92 * std::sin(w * kPi);
  }
  double twitchMul = 1.0;
  if (stress > 0.0) {
    const std::uint64_t period = 2300 + static_cast<std::uint64_t>(1700 * (1.0 - stress));
    const std::uint64_t into = (nowMs * 7 / 5) % period;
    if (into < 120) twitchMul = 0.45;
  }
  auto eye = [&](double sideSign, double phase, double openMul) {
    double dx = std::sin(t * 1.1 + phase) * unit * 0.10;
    double dy = std::sin(t * 0.9 + phase * 1.7) * unit * 0.10;
    if (watching) {   // added to the drift, so it still has a life of its own
      dx += lookX * unit * 0.45;
      dy += lookY * unit * 0.35;
    }
    const double breathe = 1.0 + std::sin(t * 1.3 + phase) * 0.10;
    const int eh = std::max(thick, static_cast<int>(std::lround(eyeHFull * eyeOpen * openMul * breathe)));
    int X, Y;
    place(sideSign * eyeGap / 2 + dx, -static_cast<double>(unit) + dy, X, Y);
    f.solid.push_back({X - eyeW / 2, Y - eh / 2, eyeW, eh});
  };
  eye(-1.0, 0.0, pokedLeftEye ? (1.0 - 0.75 * reactAmt) : 1.0);
  eye(1.0, 2.3, (pokedRightEye ? std::min(winkMul, 1.0 - 0.75 * reactAmt) : winkMul) * twitchMul);
  // ANNOYED BROWS, and a sweat drop sliding down.
  if (stress > 0.0) {
    for (const double sideSign : {-1.0, 1.0}) {
      for (int i = 0; i <= 6; ++i) {
        const double fx = static_cast<double>(i) / 6.0;
        const double ox = sideSign * (eyeGap / 2 + eyeW * (0.9 - 1.6 * fx));
        const double oy = -unit * 1.95 + fx * unit * 0.45 * stress;
        int X, Y;
        place(ox, oy, X, Y);
        f.solid.push_back({X - thick / 2, Y - thick / 2, thick, thick});
      }
    }
    const double slide = std::fmod(t * 0.35, 1.0);
    int X, Y;
    place(eyeGap * 0.95, -unit * 1.6 + slide * unit * 1.2, X, Y);
    const int drop = std::max(3, thick);
    f.solid.push_back({X - drop / 2, Y, drop, drop * 2});
    f.solid.push_back({X - drop / 4, Y - drop / 2, std::max(1, drop / 2), drop / 2});
  }

  // The mouth: a breathing smile that goes out of it as it tires, turns
  // over when stressed, lifts at one corner when cheeky, and goes round
  // when poked.
  const double mdx = std::sin(t * 0.8 + 1.2) * unit * 0.10;
  const double mdy = std::sin(t * 1.05 + 0.4) * unit * 0.10;
  int mouthW = unit * 3;
  double depth = smile * unit;
  depth *= mood.mellow ? 0.85 : (1.0 - 0.55 * tired);
  if (mood.asleep) depth *= 0.4;
  depth = depth * (1.0 - stress) - unit * 0.35 * stress;
  if (pokedMouth) {
    mouthW = static_cast<int>(std::lround(mouthW * (1.0 - 0.45 * reactAmt)));
    depth = depth * (1.0 - 1.8 * reactAmt);
  }
  constexpr int N = 12;
  for (int i = 0; i <= N; ++i) {
    const double fx = static_cast<double>(i) / N * 2.0 - 1.0;
    const double ox = fx * mouthW / 2 + mdx;
    double oy = unit * 3.0 / 2.0 + depth * (1.0 - fx * fx) + mdy;
    const double towardRight = (fx + 1.0) * 0.5;
    oy -= cheek * (1.0 - stress) * unit * 0.6 * towardRight * towardRight;
    oy += stress * std::sin(fx * 9.0 + t * 6.0) * unit * 0.06;
    int X, Y;
    place(ox, oy, X, Y);
    f.solid.push_back({X - thick / 2, Y - thick / 2, thick, thick});
  }

  f.cx = cx;
  f.cy = cy;
  f.radius = pk.fleeStartedMs != 0 ? 0 : unit * 3;

  // Three stars orbiting, each with its own pulse, bounded by the area
  // (measured from the orbit's true centre); no room, no stars.
  for (int s = 0; s < 3; ++s) {
    const double ph = t * 0.6 + s * (2.0 * kPi / 3.0);
    const int sz = 2 + (s % 2) + (std::sin(t * 2.0 + s) > 0.6 ? 1 : 0);
    const double starPad = unit * 0.7 + sz;
    const int starCy = cy - unit;
    const double maxRadX = (std::min(cx - area.x, area.x + area.w - cx) - starPad) / 1.5;
    const double maxRadY = (std::min(starCy - area.y, area.y + area.h - starCy) - starPad) / 0.7;
    const double baseRad = std::min({unit * 3.2, maxRadX, maxRadY});
    if (baseRad < unit * 0.8) continue;
    const double rad = baseRad + std::sin(t * 0.7 + s) * baseRad * 0.12;
    Star st;
    st.x = std::clamp(cx + static_cast<int>(std::lround(std::cos(ph) * rad * 1.5)), area.x + sz, area.x + area.w - sz - 1);
    st.y = std::clamp(starCy + static_cast<int>(std::lround(std::sin(ph) * rad * 0.7)), area.y + sz, area.y + area.h - sz - 1);
    st.size = sz;
    st.alpha = static_cast<std::uint8_t>(60 + 150 * std::fabs(std::sin(t * 1.6 + s * 1.3)));
    f.stars.push_back(st);
  }
  return f;
}

// IT HAS SOMETHING TO SAY ABOUT BEING POKED, escalating: a single stock
// protest repeated ten times is a bug, an escalating one is a character.
inline const char* pokeLine(int pokes) {
  static const char* kLines[] = {
      "Hey!", "Quit it!", "Ow. That was rude.", "I am a professional.", "Seriously?",
      "This is my monitor.", "I am telling the operator.", "All right. Wow.", "Right. That does it.",
  };
  const int n = static_cast<int>(sizeof(kLines) / sizeof(kLines[0]));
  return kLines[std::clamp(pokes - 1, 0, n - 1)];
}
inline const char* kFleeLine = "I am leaving. Good luck with the show.";

// What it says as the hours go by, by mood; four each.
inline const char* moodLine(const Mood& m, int turn) {
  static const char* kCheeky[] = {"Still here? Me too.", "Long show, huh.", "I could do this all day. Apparently.",
                                  "You have been staring at me for hours."};
  static const char* kTired[] = {"Has anyone seen a clock?", "Is it still the same show?", "I'm not tired. You're tired.",
                                 "Five more minutes. Then five more."};
  static const char* kStressed[] = {"A whole day. I need a restart.", "Save the show. Then let me sleep.",
                                    "Every pixel of me is tired.", "Close me. Open me. I'll be fine."};
  const char* const* lines = m.stress > 0.0 ? kStressed : m.tired > 0.0 ? kTired : kCheeky;
  return lines[((turn % 4) + 4) % 4];
}

// The first `intoMs / msPerChar` CHARACTERS of a line (never half a UTF-8
// one), with a blinking cursor while it is still being typed.
inline std::string typed(const std::string& full, std::uint64_t intoMs, std::uint64_t msPerChar, std::uint64_t nowMs) {
  const std::size_t wanted = static_cast<std::size_t>(intoMs / std::max<std::uint64_t>(1, msPerChar));
  std::size_t shown = 0;
  for (std::size_t chars = 0; shown < full.size() && chars < wanted; ++chars) {
    ++shown;
    while (shown < full.size() && (static_cast<unsigned char>(full[shown]) & 0xC0) == 0x80) ++shown;
  }
  std::string out(full, 0, shown);
  if (shown < full.size()) out += ((nowMs / 320) % 2) ? "_" : " ";
  return out;
}

}  // namespace deckboy::face
