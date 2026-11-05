// Bug Swat against made-up camera frames. A pretend player waves "hands"
// (boxes that flicker frame to frame) into a 640x360 picture, the picture goes
// through Motion's real analyser with real settings, and the game reads what
// it reads -- so each round is proved to need the control it teaches.
//
//   deckboy-bugswat-check
#include "core/bugswat.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace deckboy;
using bugswat::Game;
using bugswat::Kind;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %-70s %s\n", what.c_str(), ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

constexpr int kW = 640, kH = 360;
constexpr double kDt = 1.0 / 30;

struct Hand {
  float x, y;           // centre, fractions
  int size = 90;        // pixels
  int contrast = 255;   // how much it changes the picture
};

struct Camera {
  motion::Analyzer analyzer;
  motion::Settings settings;
  std::vector<std::uint8_t> px = std::vector<std::uint8_t>(kW * kH * 4, 0);
  long frame = 0;

  Camera() { settings.mirror = false; }

  // One frame: every hand shows on even frames and is gone on odd ones, so
  // each one reads as movement wherever it is.
  bugswat::MotionView feed(const std::vector<Hand>& hands) {
    std::fill(px.begin(), px.end(), std::uint8_t {0});
    for (std::size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    if (frame % 2 == 0) {
      for (const Hand& h : hands) {
        const int cx = static_cast<int>(h.x * kW), cy = static_cast<int>(h.y * kH);
        for (int y = std::max(0, cy - h.size / 2); y < std::min(kH, cy + h.size / 2); ++y) {
          for (int x = std::max(0, cx - h.size / 2); x < std::min(kW, cx + h.size / 2); ++x) {
            std::uint8_t* p = &px[static_cast<std::size_t>((y * kW + x) * 4)];
            p[0] = p[1] = p[2] = static_cast<std::uint8_t>(h.contrast);
          }
        }
      }
    }
    ++frame;
    analyzer.feed(px.data(), kW, kH, kW * 4, motion::PixelLayout::Rgba, kDt, settings);
    return bugswat::viewOf(analyzer, settings);
  }
};

using Player = std::function<std::vector<Hand>(const Game&, double t)>;

static double play(Game& g, Camera& cam, double seconds, const Player& player,
                   const std::function<bool()>& until = [] { return false; }) {
  double t = 0;
  while (t < seconds && !until()) {
    g.update(kDt, cam.feed(player(g, t)));
    t += kDt;
  }
  return t;
}

static const bugswat::Bug* firstLive(const Game& g) {
  for (const auto& b : g.bugs()) {
    if (!b.fixed && !b.escaped) return &b;
  }
  return nullptr;
}

static const Player still = [](const Game&, double) { return std::vector<Hand> {}; };

// Wait, still, until a bug is on screen (and, for the caterpillar, its tail).
static void waitForBug(Game& g, Camera& cam, float maxX = 1.01f) {
  play(g, cam, 10.0, still, [&] { return firstLive(g) && firstLive(g)->x < maxX; });
}

static std::vector<Hand> onFirstBug(const Game& g, int size) {
  const auto* b = firstLive(g);
  return b ? std::vector<Hand> {{b->x, b->y, size}} : std::vector<Hand> {};
}

int main() {
  std::printf("bugswat:\n");

  {
    Game g;
    Camera cam;
    g.start(0);
    play(g, cam, 61.0, still, [&] { return g.round() != 0; });
    check(g.score() == 0, "still player: nothing fixed");
    check(g.escaped() > 0, "still player: bugs reach the cue list");
    check(g.round() == 1, "a round ends on time without a swat");
    check(g.roundsCleared() == 0, "...and does not count as cleared");
  }

  {
    // NullPointer: a fingertip passes through; a hand lands.
    Game g;
    Camera cam;
    g.start(0);
    waitForBug(g, cam, 0.9f);
    play(g, cam, 1.0, [](const Game& gg, double) { return onFirstBug(gg, 8); });
    check(g.fixedThisRound() == 0, "NullPointer: a fingertip passes through it");
    play(g, cam, 1.0, [](const Game& gg, double) { return onFirstBug(gg, 90); },
         [&] { return g.fixedThisRound() > 0; });
    check(g.fixedThisRound() == 1, "NullPointer: a waving hand fixes it");
  }

  const Player bothTwins = [](const Game& gg, double) {
    std::vector<Hand> hands;
    for (const auto& b : gg.bugs()) {
      if (b.kind == Kind::Twin && !b.fixed) hands.push_back({b.x, b.y, 90});
    }
    return hands;
  };

  {
    // Race Condition without zones: one reading cannot tell the hands apart.
    Game g;
    Camera cam;
    g.start(1);
    waitForBug(g, cam);
    play(g, cam, 5.0, bothTwins);
    check(g.fixedThisRound() == 0, "Race Condition: no zones, two hands still fix nothing");
  }

  {
    // ...with a zone for each hand.
    Game g;
    Camera cam;
    cam.settings.zones = {{0.0f, 0.0f, 0.5f, 1.0f}, {0.5f, 0.0f, 0.5f, 1.0f}};
    g.start(1);
    waitForBug(g, cam);
    check(g.bugs().size() == 2, "Race Condition arrives as a pair");
    play(g, cam, 1.0, [](const Game& gg, double) {
      const auto& a = gg.bugs()[0];
      return std::vector<Hand> {{a.x, a.y, 90}};
    });
    play(g, cam, 1.5, still);
    check(g.swaps() >= 1, "Race Condition: one hand alone swaps them");
    check(g.fixedThisRound() == 0, "Race Condition: ...and fixes neither");
    play(g, cam, 1.0, bothTwins, [&] { return g.fixedThisRound() > 0; });
    check(g.fixedThisRound() == 2, "Race Condition: a hand in each zone fixes both");
  }

  const Player tailFlicker = [](const Game& gg, double) {
    const auto* b = firstLive(gg);
    // A faint, small movement: a finger on the +1 segment.
    return b ? std::vector<Hand> {{b->x + bugswat::kOffByOneTail, b->y, 14, 22}} : std::vector<Hand> {};
  };

  {
    // Off By One: the face is not the bug, and a coarse reading misses the
    // +1 segment.
    Game g;
    Camera cam;
    g.start(2);
    waitForBug(g, cam, 0.85f);
    play(g, cam, 0.5, [](const Game& gg, double) {
      const auto* b = firstLive(gg);
      return b ? std::vector<Hand> {{b->x - 0.03f, b->y, 24}} : std::vector<Hand> {};
    });
    check(g.fixedThisRound() == 0, "Off By One: swatting its face misses");
    play(g, cam, 1.0, tailFlicker);
    check(g.fixedThisRound() == 0, "Off By One: default sensitivity misses the faint +1");
    cam.settings.sensitivity = 0.05f;
    play(g, cam, 1.0, tailFlicker, [&] { return g.fixedThisRound() > 0; });
    check(g.fixedThisRound() == 1, "Off By One: turned-up sensitivity catches it");
  }

  // Short sweeps over the bug: a third of a second in every second and a half.
  const Player sweeps = [](const Game& gg, double t) {
    if (std::fmod(t, 1.5) > 0.33) return std::vector<Hand> {};
    return onFirstBug(gg, 110);
  };

  {
    // Memory Leak: short sweeps fall away with the default fall...
    Game g;
    Camera cam;
    g.start(3);
    waitForBug(g, cam, 0.9f);
    play(g, cam, 8.0, sweeps);
    check(g.fixedThisRound() == 0, "Memory Leak: short sweeps, default fall: not caught");
    // ...and join up into one held reading with a long one.
    cam.settings.fallSeconds = 3.0f;
    play(g, cam, 10.0, sweeps, [&] { return g.fixedThisRound() > 0; });
    check(g.fixedThisRound() == 1, "Memory Leak: the same sweeps with a long fall catch it");
  }

  {
    // Heisenbug: fades while you move; following it with the reading's x
    // catches it; waving in one place does not.
    Game g;
    Camera cam;
    g.start(4);
    waitForBug(g, cam);
    play(g, cam, 6.0, [](const Game&, double) { return std::vector<Hand> {{0.5f, 0.5f, 90}}; });
    const auto* b = firstLive(g);
    check(b && b->seen < 0.5f, "Heisenbug: fades while you move");
    check(g.fixedThisRound() == 0, "Heisenbug: waving in one place does not catch it");
    play(g, cam, 3.0, still);
    b = firstLive(g);
    check(b && b->seen > 0.9f, "Heisenbug: comes back when you are still");
    if (b) {
      // Each view settles for half a second, so the smoothed x has arrived.
      const float farX = b->x > 0.5f ? 0.05f : 0.95f;
      float farNear = 0.0f;
      for (int i = 0; i < 15; ++i) farNear = g.heisenNearness(cam.feed({{farX, 0.5f, 90}}));
      float onNear = 0.0f;
      for (int i = 0; i < 15; ++i) onNear = g.heisenNearness(cam.feed({{b->x, b->y, 90}}));
      check(onNear > farNear + 0.3f, "Heisenbug: the ticking rises as you near it");
    }
    const double took = play(g, cam, 20.0, [](const Game& gg, double) { return onFirstBug(gg, 90); },
                             [&] { return g.over(); });
    check(g.over(), "Heisenbug: following it catches it, and the game ends");
    check(took > 1.5, "Heisenbug: ...after following it for a while");
  }

  {
    // A player who has set everything up and swats every bug the way it
    // must be swatted finishes all five rounds; the same seed plays the
    // same game.
    auto perfect = [](std::uint32_t seed, double& took) {
      Game g;
      Camera cam;
      g.start(0, seed);
      took = play(g, cam, 400.0, [&cam](const Game& gg, double) {
        std::vector<Hand> hands;
        const int r = gg.round();
        cam.settings.zones.clear();
        cam.settings.sensitivity = 0.12f;
        cam.settings.fallSeconds = 0.4f;
        if (r == 1) cam.settings.zones = {{0.0f, 0.0f, 0.5f, 1.0f}, {0.5f, 0.0f, 0.5f, 1.0f}};
        if (r == 2) cam.settings.sensitivity = 0.05f;
        if (r == 3) cam.settings.fallSeconds = 3.0f;
        for (const auto& bug : gg.bugs()) {
          if (bug.fixed || bug.escaped) continue;
          if (bug.kind == Kind::OffByOne) {
            hands.push_back({bug.x + bugswat::kOffByOneTail, bug.y, 14, 22});
          } else {
            hands.push_back({bug.x, bug.y, bug.kind == Kind::Leak ? 110 : 90});
          }
          if (bug.kind != Kind::Twin) break;   // one hand at a time, as people have
        }
        return hands;
      }, [&] { return g.over(); });
      return g;
    };
    double t1 = 0, t2 = 0;
    const Game a = perfect(7, t1);
    const Game b = perfect(7, t2);
    check(a.over(), "a set-up player finishes all five rounds");
    check(a.roundsCleared() == bugswat::kRounds, "...clearing every one of them");
    check(t1 < 5 * 60.0, "...faster than the clock");
    check(a.score() == b.score() && t1 == t2, "the same seed plays the same game");
    std::printf("  set-up game: score %d in %.1f s, %d reached the cue list\n", a.score(), t1, a.escaped());
  }

  std::printf(failures ? "FAILED: %d\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
