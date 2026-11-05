#pragma once
// BUG SWAT: the Motion lesson, as a game.
//
// James, 2026-11-05 (tutorials board Cihkd2Se, "i dig"): Game Boy greens,
// cute software bugs, swatted by waving at the webcam. Each round teaches one
// Motion control, and each bug is built so that it CANNOT be fixed without
// that control set right -- the game reads Motion's real grid and its real
// smoothed readings, so it tests exactly what the operator will later put on
// a knob:
//
//   1 NullPointer     hollow: a brush passes through,    Motion on, what it reads
//                     only a real wave lands
//   2 Race Condition  twins, one in each zone; fixed     zones (one reading cannot
//                     only when both zones read at once   tell two hands apart)
//   3 Off By One      the tiny +1 segment is the bug,    sensitivity
//                     and a coarse reading misses it
//   4 Memory Leak     caught only by movement held up    rise and fall
//                     long enough over it
//   5 Heisenbug       fades while you move and wanders;  Motion X, by ear
//                     follow it with your reading's x
//
// SDL-free; tools/bugswat_check.cpp plays it with made-up movement.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "motion.hpp"

namespace deckboy::bugswat {

enum class Kind { NullPointer = 0, Twin, OffByOne, Leak, Heisen };

constexpr int kRounds = 5;

struct Round {
  Kind kind;
  int toFix;          // bugs to fix to clear the round
  float speed;        // fractions of the play area a second, leftward
  float seconds;      // how long the round runs at most
  const char* name;
};

inline const std::array<Round, kRounds>& rounds() {
  static const std::array<Round, kRounds> r {{
    {Kind::NullPointer, 5, 0.045f, 60.0f, "NULLPOINTER"},
    {Kind::Twin, 3, 0.0f, 60.0f, "RACE CONDITION"},
    {Kind::OffByOne, 4, 0.04f, 60.0f, "OFF BY ONE"},
    {Kind::Leak, 3, 0.025f, 60.0f, "MEMORY LEAK"},
    {Kind::Heisen, 1, 0.0f, 75.0f, "HEISENBUG"},
  }};
  return r;
}

// Where the +1 segment sits, right of the bug's centre.
// It matches the sprite (app_bugswat.ipp): the segment is 22 of the
// handheld's 160 pixels behind the face.
constexpr float kOffByOneTail = 0.14f;

struct Bug {
  Kind kind = Kind::NullPointer;
  float x = 1.0f, y = 0.5f;   // fractions of the play area
  float vy = 0.0f;
  bool fixed = false;
  bool escaped = false;       // reached the cue list instead
  float fixedFor = 0.0f;      // seconds since it was fixed or escaped
  float charge = 0.0f;        // a Leak's or the boss's progress, 0-1; a Twin's hit, fading
  int twin = -1;              // a Twin's partner
  int zone = 0;               // a Twin's zone, 1 or 2
  bool lapsed = false;        // a Twin's hit ran out this frame
  float seen = 1.0f;          // the boss's visibility, 0-1
  float phase = 0.0f;         // the boss's wander
};

// One reading, as core/motion.hpp has it.
struct Reading {
  float amount = 0.0f;
  float x = 0.5f;
  float y = 0.5f;
};

struct ZoneRect {
  float x = 0, y = 0, w = 1, h = 1;
};

// What the game reads from Motion each frame.
struct MotionView {
  const std::uint8_t* moved = nullptr;   // w x h, non-zero where it moved
  int w = 0, h = 0;
  // Reading 0 is the whole picture, 1.. the zones.
  std::array<Reading, 9> readings {};
  std::array<ZoneRect, 8> zones {};
  int zoneCount = 0;

  // The fraction of moving cells in a box round a point (fractions), radius
  // in cells.
  float around(float fx, float fy, int radius) const {
    if (!moved || w <= 0 || h <= 0) return 0.0f;
    const int cx = static_cast<int>(fx * w), cy = static_cast<int>(fy * h);
    int hit = 0, all = 0;
    for (int y = std::max(0, cy - radius); y <= std::min(h - 1, cy + radius); ++y) {
      for (int x = std::max(0, cx - radius); x <= std::min(w - 1, cx + radius); ++x) {
        ++all;
        if (moved[static_cast<std::size_t>(y) * w + x]) ++hit;
      }
    }
    return all ? static_cast<float>(hit) / all : 0.0f;
  }
  bool moving() const { return readings[0].amount > 0.15f; }
};

// The view of a running Motion analyser, as the game reads it.
inline MotionView viewOf(const motion::Analyzer& a, const motion::Settings& s) {
  MotionView v;
  v.moved = a.moved().data();
  v.w = motion::kGridW;
  v.h = motion::kGridH;
  for (int r = 0; r < motion::kReadings && r < static_cast<int>(v.readings.size()); ++r) {
    const motion::Reading& in = a.reading(r);
    v.readings[static_cast<std::size_t>(r)] = {in.amount, in.x, in.y};
  }
  v.zoneCount = std::min<int>(a.zoneCount(), static_cast<int>(v.zones.size()));
  for (int z = 0; z < v.zoneCount && z < static_cast<int>(s.zones.size()); ++z) {
    const motion::Zone& in = s.zones[static_cast<std::size_t>(z)];
    v.zones[static_cast<std::size_t>(z)] = {in.x, in.y, in.w, in.h};
  }
  return v;
}

class Game {
 public:
  // Rounds are 0-4. A seed makes a game repeatable (the tests use one).
  void start(int round = 0, std::uint32_t seed = 12345) {
    rng_ = seed ? seed : 1;
    score_ = 0;
    escaped_ = 0;
    swaps_ = 0;
    roundsCleared_ = 0;
    over_ = false;
    begin(std::clamp(round, 0, kRounds - 1));
  }

  // Jump to a round (SKIP), keeping the score.
  void skipToRound(int round) {
    if (round >= kRounds) {
      over_ = true;
      return;
    }
    begin(std::clamp(round, 0, kRounds - 1));
  }

  void update(double dt, const MotionView& mv) {
    if (over_) return;
    const float d = static_cast<float>(std::clamp(dt, 0.0, 0.1));
    t_ += d;
    const Round& r = rounds()[static_cast<std::size_t>(round_)];
    spawnIn_ -= d;
    const int live = static_cast<int>(std::count_if(bugs_.begin(), bugs_.end(), [](const Bug& b) {
      return !b.fixed && !b.escaped;
    }));
    if (spawnIn_ <= 0.0f && live < maxLive() && spawned_ < r.toFix + 6) {
      spawn(r.kind, mv);
      spawnIn_ = 2.2f;
    }
    for (Bug& b : bugs_) {
      if (b.fixed || b.escaped) {
        b.fixedFor += d;
        continue;
      }
      b.x -= r.speed * d;
      b.y = std::clamp(b.y + b.vy * d, 0.1f, 0.9f);
      if (b.y <= 0.1f || b.y >= 0.9f) b.vy = -b.vy;
      switch (b.kind) {
        case Kind::NullPointer:
          if (mv.around(b.x, b.y, 6) > 0.25f) fix(b, 1);
          break;
        case Kind::OffByOne:
          if (mv.around(b.x + kOffByOneTail, b.y, 1) > 0.3f) fix(b, 1);
          break;
        case Kind::Leak: {
          // Held: the whole picture's SMOOTHED reading stays up, centred on
          // the bug. The reading keeps its place between sweeps, so a longer
          // fall joins separate sweeps into one held reading: that is the
          // lesson.
          const Reading& r0 = mv.readings[0];
          const bool held = r0.amount > 0.45f && std::fabs(r0.x - b.x) < 0.15f && std::fabs(r0.y - b.y) < 0.2f;
          b.charge = std::clamp(b.charge + (held ? d / 1.2f : -d * 0.5f), 0.0f, 1.0f);
          if (b.charge >= 1.0f) fix(b, 2);
          break;
        }
        case Kind::Twin: {
          // Only a zone's own reading tells one hand from the other.
          const bool hit = b.zone >= 1 && b.zone <= mv.zoneCount &&
                           mv.readings[static_cast<std::size_t>(b.zone)].amount > 0.35f;
          if (hit) b.charge = 0.8f + d;
          const bool was = b.charge > 0.0f;
          b.charge = std::max(0.0f, b.charge - d);
          b.lapsed = was && b.charge <= 0.0f;
          // Keep to its zone.
          if (b.zone >= 1 && b.zone <= mv.zoneCount) {
            const ZoneRect& z = mv.zones[static_cast<std::size_t>(b.zone - 1)];
            b.x = z.x + z.w * 0.5f;
            b.y = std::clamp(b.y, z.y + z.h * 0.2f, z.y + z.h * 0.8f);
          }
          break;
        }
        case Kind::Heisen: {
          // It wanders, and fades while anything moves.
          b.phase += d;
          b.x = 0.5f + 0.32f * std::sin(b.phase * 0.45f) + 0.08f * std::sin(b.phase * 1.7f);
          b.y = 0.5f + 0.15f * std::sin(b.phase * 0.31f);
          b.seen = std::clamp(b.seen + (mv.moving() ? -d * 1.5f : d * 0.8f), 0.08f, 1.0f);
          // Caught by following it: the reading's x kept on it.
          const bool on = mv.moving() && std::fabs(mv.readings[0].x - b.x) < 0.1f;
          b.charge = std::clamp(b.charge + (on ? d / 2.0f : -d * 0.3f), 0.0f, 1.0f);
          if (b.charge >= 1.0f) fix(b, 5);
          break;
        }
      }
      if (!b.fixed && b.x < 0.0f) {
        // Reached the cue list: it files a bug report about you.
        b.escaped = true;
        b.fixedFor = 0.0f;
        ++escaped_;
      }
    }
    // The twins: both zones within the moment, or they swap.
    for (Bug& b : bugs_) {
      if (b.kind != Kind::Twin || b.fixed || b.twin < 0) continue;
      Bug& o = bugs_[static_cast<std::size_t>(b.twin)];
      if (o.fixed) continue;
      if (b.charge > 0.0f && o.charge > 0.0f) {
        fix(b, 2);
        fix(o, 2);
      } else if (b.lapsed && o.charge <= 0.0f) {
        // One got there first, so the other never happened: they swap.
        std::swap(b.zone, o.zone);
        std::swap(b.x, o.x);
        b.lapsed = false;
        ++swaps_;
      }
    }
    if (fixedThisRound_ >= r.toFix || t_ >= r.seconds) {
      if (fixedThisRound_ >= r.toFix) ++roundsCleared_;
      if (round_ >= kRounds - 1) {
        over_ = true;
      } else {
        begin(round_ + 1);
      }
    }
  }

  // How near the reading is to the boss, 0 far .. 1 on it: the ticking.
  float heisenNearness(const MotionView& mv) const {
    if (!mv.moving()) return 0.0f;
    for (const Bug& b : bugs_) {
      if (b.kind == Kind::Heisen && !b.fixed) {
        return std::clamp(1.0f - std::fabs(mv.readings[0].x - b.x) * 2.5f, 0.0f, 1.0f);
      }
    }
    return 0.0f;
  }

  int round() const { return round_; }
  int score() const { return score_; }
  int escaped() const { return escaped_; }
  int swaps() const { return swaps_; }
  // Rounds won by fixing enough bugs, not lost to the clock or skipped.
  int roundsCleared() const { return roundsCleared_; }
  int fixedThisRound() const { return fixedThisRound_; }
  int toFixThisRound() const { return rounds()[static_cast<std::size_t>(round_)].toFix; }
  bool over() const { return over_; }
  float roundTime() const { return t_; }
  float roundSecondsLeft() const {
    return std::max(0.0f, rounds()[static_cast<std::size_t>(round_)].seconds - t_);
  }
  const std::vector<Bug>& bugs() const { return bugs_; }

 private:
  int maxLive() const {
    const Kind k = rounds()[static_cast<std::size_t>(round_)].kind;
    // One snail at a time: a held reading near two would catch both.
    return k == Kind::Twin ? 2 : (k == Kind::Heisen || k == Kind::Leak) ? 1 : 3;
  }

  float rnd() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<float>((rng_ >> 8) & 0xFFFF) / 65535.0f;
  }

  void begin(int r) {
    round_ = r;
    t_ = 0.0f;
    bugs_.clear();
    spawned_ = 0;
    fixedThisRound_ = 0;
    spawnIn_ = 1.0f;
  }

  void spawn(Kind k, const MotionView& mv) {
    if (k == Kind::Heisen && spawned_ > 0) return;   // one boss
    if (k == Kind::Twin) {
      Bug a, b;
      a.kind = b.kind = Kind::Twin;
      a.zone = 1;
      b.zone = 2;
      // With no zones they wait at either side, so the screen shows what
      // the zones are for.
      a.x = mv.zoneCount >= 1 ? mv.zones[0].x + mv.zones[0].w * 0.5f : 0.3f;
      b.x = mv.zoneCount >= 2 ? mv.zones[1].x + mv.zones[1].w * 0.5f : 0.7f;
      a.y = b.y = 0.35f + rnd() * 0.3f;
      a.vy = 0.05f;
      b.vy = -0.05f;
      a.twin = static_cast<int>(bugs_.size()) + 1;
      b.twin = static_cast<int>(bugs_.size());
      bugs_.push_back(a);
      bugs_.push_back(b);
      spawned_ += 2;
      return;
    }
    Bug b;
    b.kind = k;
    b.x = k == Kind::Heisen ? 0.5f : 1.0f;
    b.y = 0.15f + rnd() * 0.7f;
    b.vy = k == Kind::Heisen ? 0.0f : (rnd() - 0.5f) * 0.08f;
    bugs_.push_back(b);
    ++spawned_;
  }

  void fix(Bug& b, int points) {
    if (b.fixed) return;
    b.fixed = true;
    b.fixedFor = 0.0f;
    score_ += points;
    ++fixedThisRound_;
  }

  std::vector<Bug> bugs_;
  int round_ = 0;
  float t_ = 0.0f;
  float spawnIn_ = 1.0f;
  int spawned_ = 0;
  int fixedThisRound_ = 0;
  int score_ = 0;
  int escaped_ = 0;
  int swaps_ = 0;
  int roundsCleared_ = 0;
  bool over_ = false;
  std::uint32_t rng_ = 12345;
};

}  // namespace deckboy::bugswat
