// creatures_check.cpp -- the theme creatures' behaviour, without a window.
//
// Each social creature reads something: its flock, the pointer, the cat. A
// still screenshot cannot show that, so this steps the simulation in fake
// time and checks what each one does.

#include "core/creatures.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace deckboy::creatures;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) ++failures;
}

const Habitat kHome {0, 0, 400, 300, true, false};

std::vector<Creature> make(Species s, int n) {
  std::vector<Creature> all;
  for (int i = 0; i < n; ++i) {
    Creature c;
    c.species = s;
    place(c, kHome, static_cast<std::uint32_t>(i + 1));
    all.push_back(c);
  }
  return all;
}

void run(std::vector<Creature>& all, double& now, double seconds, const World& world) {
  const double dt = 1.0 / 60.0;
  for (double t = 0.0; t < seconds; t += dt) {
    now += dt;
    step(all, kHome, dt, now, world);
  }
}

double spread(const std::vector<Creature>& all) {
  double cx = 0.0, cy = 0.0;
  for (const Creature& c : all) { cx += c.x; cy += c.y; }
  cx /= all.size();
  cy /= all.size();
  double d = 0.0;
  for (const Creature& c : all) d += std::hypot(c.x - cx, c.y - cy);
  return d / all.size();
}

bool inside(const std::vector<Creature>& all) {
  for (const Creature& c : all) {
    if (c.x < kHome.x || c.x > kHome.x + kHome.w || c.y < kHome.y || c.y > kHome.y + kHome.h) return false;
  }
  return true;
}

}  // namespace

int main() {
  const World nobody;   // pointer far away

  {
    // A flock gathers: twelve starlings scattered over the gap end up close.
    auto birds = make(Species::Starling, 12);
    double now = 0.0;
    const double before = spread(birds);
    run(birds, now, 20.0, nobody);
    const double after = spread(birds);
    std::printf("    starlings: spread %.0f px -> %.0f px\n", before, after);
    check(after < before * 0.6, "starlings gather into a flock");
    check(inside(birds), "...and stay inside their gap");
    // The pointer in the middle of the flock scatters it.
    World w;
    double cx = 0.0, cy = 0.0;
    for (const Creature& c : birds) { cx += c.x; cy += c.y; }
    w.pointerX = cx / birds.size();
    w.pointerY = cy / birds.size();
    double nearest = 1e9;
    run(birds, now, 0.8, w);
    for (const Creature& c : birds) nearest = std::min(nearest, std::hypot(c.x - w.pointerX, c.y - w.pointerY));
    std::printf("    starlings: nearest to the pointer after 0.8 s: %.0f px\n", nearest);
    check(nearest > 20.0, "the pointer scatters the flock");
  }
  {
    auto koi = make(Species::Koi, 6);
    double now = 0.0;
    const double before = spread(koi);
    run(koi, now, 40.0, nobody);
    std::printf("    koi: spread %.0f px -> %.0f px\n", before, spread(koi));
    check(spread(koi) < before, "koi school together");
    check(inside(koi), "...inside their gap");
  }
  {
    // The owl's head follows the pointer, both ways.
    auto owls = make(Species::Owl, 1);
    double now = 0.0;
    World w;
    w.pointerX = owls[0].x + 300.0;
    w.pointerY = owls[0].y;
    run(owls, now, 2.0, w);
    const double right = owls[0].look;
    w.pointerX = owls[0].x - 300.0;
    run(owls, now, 2.0, w);
    const double left = owls[0].look;
    std::printf("    owl: look %.2f (pointer right), %.2f (pointer left)\n", right, left);
    check(right > 0.8 && left < -0.8, "the owl watches the pointer");
    check(owls[0].vx == 0.0 && owls[0].vy == 0.0, "...without leaving its perch");
  }
  {
    // A roosting bat flies when the pointer comes near, then roosts again.
    auto bats = make(Species::Bat, 1);
    double now = 0.0;
    run(bats, now, 1.0, nobody);
    check(bats[0].state == 0, "the bat roosts while nothing is near");
    World w;
    w.pointerX = bats[0].x;
    w.pointerY = bats[0].y + 20.0;
    run(bats, now, 0.3, w);
    check(bats[0].state == 1, "the pointer sends it flying");
    run(bats, now, 15.0, nobody);
    check(bats[0].state == 0, "...and it roosts again");
  }
  {
    // Ants keep their line: each one stays close behind the one ahead.
    auto ants = make(Species::Ant, 6);
    double now = 0.0;
    run(ants, now, 20.0, nobody);
    double worst = 0.0;
    for (std::size_t i = 1; i < ants.size(); ++i) worst = std::max(worst, std::fabs(ants[i].x - ants[i - 1].x));
    std::printf("    ants: widest gap in the line %.0f px\n", worst);
    check(worst < 20.0, "ants walk in a line");
  }
  {
    // The snake's body follows its head.
    auto snakes = make(Species::Snake, 1);
    double now = 0.0;
    run(snakes, now, 10.0, nobody);
    const Creature& s = snakes[0];
    check(s.trailCount == Creature::kTrail, "the snake grows its whole body");
    check(std::hypot(s.trail[0] - s.x, s.trail[1] - s.y) < 6.0, "...and the body starts at the head");
    check(inside(snakes), "...inside its gap");
  }
  {
    // A cat and a mouse: over a few minutes the cat stalks, the mouse runs,
    // and the cat never lands on it.
    std::vector<Creature> pair;
    Creature cat, mouse;
    cat.species = Species::Cat;
    place(cat, kHome, 1);
    mouse.species = Species::Mouse;
    place(mouse, kHome, 2);
    pair.push_back(cat);
    pair.push_back(mouse);
    double now = 0.0;
    bool stalked = false;
    double closest = 1e9;
    for (int i = 0; i < 300; ++i) {
      run(pair, now, 1.0, nobody);
      stalked = stalked || pair[0].state > 0;
      if (pair[0].state == 2) closest = std::min(closest, std::fabs(pair[0].x - pair[1].x));
    }
    std::printf("    cat and mouse: stalked=%d, closest %.0f px\n", stalked ? 1 : 0, closest);
    check(stalked, "the cat wakes and stalks the mouse");
    check(closest < 1e8, "...and pounces");
    check(closest > 8.0, "...and never lands on it");
    check(inside(pair), "...both stay in their gap");
  }
  {
    // The pointer close to a mouse sends it running away from it.
    auto mice = make(Species::Mouse, 1);
    double now = 0.0;
    World w;
    w.pointerX = mice[0].x - 20.0;
    w.pointerY = mice[0].y;
    run(mice, now, 0.1, w);
    check(mice[0].vx > 50.0, "a mouse runs from the pointer");
  }
  {
    auto octo = make(Species::Octopus, 1);
    double now = 0.0;
    run(octo, now, 3.0, nobody);
    World w;
    w.pointerX = octo[0].x + 30.0;
    w.pointerY = octo[0].y;
    octo[0].state = 0;
    run(octo, now, 0.05, w);
    check(octo[0].vx < -20.0, "the octopus jets away from the pointer");
  }
  // Every species has a token, and the token reads back.
  bool tokens = true;
  for (int i = 0; i < static_cast<int>(Species::Count); ++i) {
    Species back = Species::Moth;
    tokens = tokens && speciesFromToken(speciesToken(static_cast<Species>(i)), back) &&
             back == static_cast<Species>(i);
  }
  check(tokens, "every species' theme token reads back as itself");

  std::printf(failures ? "FAILED: %d\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
