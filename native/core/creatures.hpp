// creatures.hpp — the small living things a theme can ask for.
//
// The startup mascot proved that one friendly face in an empty monitor is worth
// having. This is that idea spread through the rest of the interface: a theme
// can declare creatures, and they wander the app's chrome.
//
// THREE RULES, and they are what make this shippable in show software rather
// than a novelty that gets switched off on the first gig:
//
//   1. They live on the SHELL, never over a control. The chrome between and
//      behind the panels carries no information, so nothing they do can hide a
//      value, a level or a cue name. A creature that covers a number is a bug
//      in a way a creature that walks along a bezel is not.
//
//   2. They VANISH the moment an output is live. During a show the only thing
//      moving on that machine should be the show. They fade out rather than
//      blink off, and come back when the outputs go down.
//
//   3. They are THEME DATA, not renderer behaviour. A theme that asks for
//      nothing gets nothing, which is every existing theme; the drawing code
//      has no opinion about which themes should have creatures in them. This
//      is the same contract the colour roles follow, for the same reason:
//      "fix the theme, never the renderer".
//
// They are drawn from primitives rather than sprite sheets so they take a
// colour from the palette and therefore read correctly on all 25 of the OLED
// terminals without a single per-theme asset.

#ifndef DECKBOY_CORE_CREATURES_HPP
#define DECKBOY_CORE_CREATURES_HPP

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace deckboy::creatures {

enum class Species : std::uint8_t {
  Moth,      // flutters, drawn toward the brightest thing nearby
  Crab,      // scuttles along a ledge, sidles, pauses
  Fish,      // drifts in open space, turns lazily
  Firefly,   // wanders slowly and blinks
  Cat,       // sleeps in a corner, stretches, resettles
  Snail,     // crawls the ledge slowly, shell on its back
  Spider,    // drops on a thread from above, climbs back up
  Mouse,     // darts along the bottom, FREEZES, darts again
  Frog,      // sits, then hops in an arc
  Jellyfish, // pulses upward, then sinks while it rests
  Bird,      // hops along the ledge and turns its head
  Count
};

inline const char* speciesToken(Species s) {
  switch (s) {
    case Species::Moth:    return "moth";
    case Species::Crab:    return "crab";
    case Species::Fish:    return "fish";
    case Species::Firefly: return "firefly";
    case Species::Cat:       return "cat";
    case Species::Snail:     return "snail";
    case Species::Spider:    return "spider";
    case Species::Mouse:     return "mouse";
    case Species::Frog:      return "frog";
    case Species::Jellyfish: return "jellyfish";
    case Species::Bird:      return "bird";
    default:                 return "moth";
  }
}

inline bool speciesFromToken(const std::string& token, Species& out) {
  for (int i = 0; i < static_cast<int>(Species::Count); ++i) {
    if (token == speciesToken(static_cast<Species>(i))) {
      out = static_cast<Species>(i);
      return true;
    }
  }
  return false;
}

// What a theme asked for. One line in theme.txt becomes one of these.
struct Request {
  Species species = Species::Moth;
  int count = 1;
};

// One animal, alive.
struct Creature {
  Species species = Species::Moth;
  double x = 0.0, y = 0.0;
  double vx = 0.0, vy = 0.0;
  double phase = 0.0;        // its own offset, so a pair never moves in step
  double restUntil = 0.0;    // seconds; crabs and cats stop and think
  double facing = 1.0;       // -1 or 1
  double blink = 0.0;
};

// A band of chrome a creature is allowed to occupy. The app fills these in from
// the shell's own layout, so the creatures cannot know or care what the panels
// are -- only where the gaps between them are.
struct Habitat {
  int x = 0, y = 0, w = 0, h = 0;
  bool ledge = false;   // a horizontal edge things can walk along
};

// Deterministic, so a creature does not teleport when the window is resized and
// the same show looks the same twice.
inline double hash01(std::uint32_t n) {
  n = (n ^ 61u) ^ (n >> 16);
  n *= 9u;
  n = n ^ (n >> 4);
  n *= 0x27d4eb2du;
  n = n ^ (n >> 15);
  return static_cast<double>(n & 0xFFFFFF) / static_cast<double>(0x1000000);
}

// Place a new creature somewhere sensible for its kind.
inline void place(Creature& c, const Habitat& home, std::uint32_t seed) {
  const double rx = hash01(seed * 7919u + 11u);
  const double ry = hash01(seed * 104729u + 7u);
  c.phase = hash01(seed * 31u + 3u) * 6.283185307179586;
  c.facing = rx < 0.5 ? -1.0 : 1.0;
  switch (c.species) {
    case Species::Crab:
      // On the ledge, not floating above it.
      c.x = home.x + rx * std::max(1, home.w);
      c.y = home.y + home.h - 4.0;
      c.vx = c.facing * (10.0 + ry * 14.0);
      break;
    case Species::Snail:
    case Species::Mouse:
    case Species::Frog:
    case Species::Bird:
      // Ground dwellers start ON the floor, not hovering above it.
      c.x = home.x + rx * std::max(1, home.w);
      c.y = home.y + home.h - 8.0;
      c.vx = c.facing * (c.species == Species::Snail ? 3.0 : 20.0 + ry * 20.0);
      break;
    case Species::Spider:
      // Hangs from the top edge; the thread pays out from where it started.
      c.x = home.x + 20.0 + rx * std::max(1.0, home.w - 40.0);
      c.y = home.y + 6.0;
      c.restUntil = ry * 6.0;
      break;
    case Species::Jellyfish:
      c.x = home.x + rx * std::max(1, home.w);
      c.y = home.y + ry * std::max(1, home.h);
      c.vy = -6.0;
      break;
    case Species::Cat:
      // Cats pick a corner and stay in it.
      c.x = home.x + (rx < 0.5 ? 18.0 : std::max(1, home.w) - 30.0);
      c.y = home.y + home.h - 8.0;
      c.vx = 0.0;
      c.restUntil = 4.0 + ry * 12.0;
      break;
    default:
      c.x = home.x + rx * std::max(1, home.w);
      c.y = home.y + ry * std::max(1, home.h);
      c.vx = (rx - 0.5) * 26.0;
      c.vy = (ry - 0.5) * 18.0;
      break;
  }
}

// Move everything on by dt seconds, keeping each animal inside its habitat.
//
// `lure` is the point a moth steers toward -- the app passes the brightest
// thing on screen, which is usually the program monitor. Moths going to the
// light is the entire reason to have a moth.
inline void step(std::vector<Creature>& all, const Habitat& home, double dt,
                 double now, double lureX, double lureY) {
  // Room for the animal itself, not just its anchor point. Clamping the
  // centre to the edge lets half a cat hang outside the panel and get clipped
  // away, which reads as it not being there at all.
  const double margin = 16.0;
  const double left = home.x + margin;
  const double right = home.x + std::max(2.0 * margin + 8.0, static_cast<double>(home.w)) - margin;
  const double top = home.y + margin;
  const double bottom = home.y + std::max(2.0 * margin + 8.0, static_cast<double>(home.h)) - margin;
  for (Creature& c : all) {
    switch (c.species) {
      case Species::Moth: {
        // Toward the light, but badly: a moth that flew straight at a lamp
        // would just sit on it. The wobble is the animal.
        const double dx = lureX - c.x, dy = lureY - c.y;
        const double dist = std::sqrt(dx * dx + dy * dy) + 1.0;
        c.vx += (dx / dist) * 26.0 * dt;
        c.vy += (dy / dist) * 26.0 * dt;
        c.vx += std::sin(now * 7.0 + c.phase) * 46.0 * dt;
        c.vy += std::cos(now * 6.1 + c.phase * 1.7) * 46.0 * dt;
        const double speed = std::sqrt(c.vx * c.vx + c.vy * c.vy);
        if (speed > 42.0) { c.vx = c.vx / speed * 42.0; c.vy = c.vy / speed * 42.0; }
        break;
      }
      case Species::Fish:
        // A slow turn rather than a heading: fish do not corner.
        c.vx += std::sin(now * 0.5 + c.phase) * 5.0 * dt;
        c.vy += std::cos(now * 0.37 + c.phase) * 3.0 * dt;
        c.vx = std::clamp(c.vx, -18.0, 18.0);
        c.vy = std::clamp(c.vy, -9.0, 9.0);
        break;
      case Species::Firefly:
        c.vx += (hash01(static_cast<std::uint32_t>(now * 3.0) + static_cast<std::uint32_t>(c.phase * 100)) - 0.5) * 30.0 * dt;
        c.vy += (hash01(static_cast<std::uint32_t>(now * 3.7) + static_cast<std::uint32_t>(c.phase * 57)) - 0.5) * 30.0 * dt;
        c.vx = std::clamp(c.vx, -12.0, 12.0);
        c.vy = std::clamp(c.vy, -12.0, 12.0);
        // A slow pulse rather than a flicker: it should read as breathing.
        c.blink = 0.35 + 0.65 * std::pow(0.5 + 0.5 * std::sin(now * 1.6 + c.phase), 3.0);
        break;
      case Species::Crab:
        if (now < c.restUntil) {
          c.vx = 0.0;
        } else if (c.vx == 0.0) {
          // Off again, possibly the other way. Crabs change their minds.
          c.facing = hash01(static_cast<std::uint32_t>(now * 10.0) + 17u) < 0.5 ? -1.0 : 1.0;
          c.vx = c.facing * (10.0 + hash01(static_cast<std::uint32_t>(now * 13.0)) * 16.0);
        } else if (hash01(static_cast<std::uint32_t>(now * 4.0) +
                          static_cast<std::uint32_t>(c.phase * 91)) > 0.995) {
          c.restUntil = now + 0.8 + hash01(static_cast<std::uint32_t>(now * 6.0)) * 2.5;
          c.vx = 0.0;
        }
        c.y = bottom - 2.0;
        c.vy = 0.0;
        break;
      case Species::Snail:
        // Slow enough that you only notice it moved by looking twice, which
        // is the entire joke.
        c.y = bottom;
        c.vy = 0.0;
        if (c.vx == 0.0) c.vx = c.facing * 3.0;
        break;
      case Species::Mouse: {
        // Dart, FREEZE, dart. A mouse that moved steadily would read as a
        // beetle; the stillness between runs is what makes it a mouse.
        c.y = bottom;
        c.vy = 0.0;
        if (now < c.restUntil) {
          c.vx = 0.0;
        } else if (c.vx == 0.0) {
          c.facing = hash01(static_cast<std::uint32_t>(now * 9.0) + 5u) < 0.5 ? -1.0 : 1.0;
          c.vx = c.facing * (52.0 + hash01(static_cast<std::uint32_t>(now * 11.0)) * 34.0);
        } else if (hash01(static_cast<std::uint32_t>(now * 6.0) +
                          static_cast<std::uint32_t>(c.phase * 71)) > 0.97) {
          c.restUntil = now + 0.6 + hash01(static_cast<std::uint32_t>(now * 3.0)) * 2.0;
          c.vx = 0.0;
        }
        break;
      }
      case Species::Frog: {
        // Sits, then leaves the ground entirely. The arc is real gravity, so
        // the landing is not a guess.
        const bool airborne = c.y < bottom - 0.5;
        if (airborne) {
          c.vy += 220.0 * dt;
        } else if (now < c.restUntil) {
          c.vx = 0.0;
          c.vy = 0.0;
          c.y = bottom;
        } else {
          c.facing = hash01(static_cast<std::uint32_t>(now * 8.0) + 23u) < 0.5 ? -1.0 : 1.0;
          c.vx = c.facing * 34.0;
          c.vy = -78.0;
          c.restUntil = now + 1.6 + hash01(static_cast<std::uint32_t>(now * 5.0)) * 3.0;
        }
        break;
      }
      case Species::Jellyfish:
        // Pulse up, sink back. The rest is longer than the push, which is why
        // it drifts downward overall and has to keep swimming.
        c.blink = 0.5 + 0.5 * std::sin(now * 1.1 + c.phase);
        c.vy = c.blink > 0.8 ? -14.0 : 5.0;
        c.vx = std::sin(now * 0.3 + c.phase) * 4.0;
        break;
      case Species::Spider:
        // Down the thread and back up it, pausing at the bottom.
        if (now < c.restUntil) {
          c.vy = 0.0;
        } else {
          const double swing = std::sin(now * 0.4 + c.phase);
          c.vy = swing > 0.0 ? 11.0 : -11.0;
          if (swing > 0.0 && c.y > bottom - 30.0 &&
              hash01(static_cast<std::uint32_t>(now * 5.0)) > 0.98) {
            c.restUntil = now + 1.0 + hash01(static_cast<std::uint32_t>(now * 7.0)) * 2.0;
          }
        }
        c.vx = std::sin(now * 0.7 + c.phase) * 2.0;
        break;
      case Species::Bird: {
        // Hops rather than walks, and stops to look around.
        c.y = bottom;
        c.vy = 0.0;
        if (now < c.restUntil) {
          c.vx = 0.0;
        } else if (c.vx == 0.0) {
          c.facing = hash01(static_cast<std::uint32_t>(now * 7.0) + 31u) < 0.5 ? -1.0 : 1.0;
          c.vx = c.facing * 26.0;
        } else if (hash01(static_cast<std::uint32_t>(now * 5.0) +
                          static_cast<std::uint32_t>(c.phase * 43)) > 0.975) {
          c.restUntil = now + 1.2 + hash01(static_cast<std::uint32_t>(now * 4.0)) * 2.5;
          c.vx = 0.0;
        }
        break;
      }
      case Species::Cat:
        // Asleep, mostly. The stretch is the whole performance.
        c.vx = 0.0;
        c.vy = 0.0;
        if (now > c.restUntil) {
          c.restUntil = now + 8.0 + hash01(static_cast<std::uint32_t>(now)) * 14.0;
          c.facing = -c.facing;
        }
        break;
      default:
        break;
    }
    c.x += c.vx * dt;
    c.y += c.vy * dt;
    // Turn around at the edges rather than vanishing through them.
    if (c.x < left)  { c.x = left;  c.vx = std::fabs(c.vx); c.facing = 1.0; }
    if (c.x > right) { c.x = right; c.vx = -std::fabs(c.vx); c.facing = -1.0; }
    if (c.y < top)    { c.y = top;    c.vy = std::fabs(c.vy); }
    if (c.y > bottom) { c.y = bottom; c.vy = -std::fabs(c.vy); }
  }
}

}  // namespace deckboy::creatures

#endif  // DECKBOY_CORE_CREATURES_HPP
