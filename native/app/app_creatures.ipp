// ═══════════════════════════════════════════════════════════════════════════════
// app_creatures.ipp — drawing the theme's creatures, and knowing when not to.
//
// The simulation is in core/creatures.hpp and knows nothing about SDL. This is
// the half that puts pixels down, and the half that decides whether any should
// be put down at all.
//
// They are drawn from rectangles rather than from sprite assets. That is not a
// shortcut: it means every creature takes its colour from the palette, so one
// implementation reads correctly on the LCD themes, the plastic ones and all 25
// of the OLED terminals without a single per-theme image — the same reason the
// mascot is drawn rather than loaded.
// ═══════════════════════════════════════════════════════════════════════════════

  // Whether anything should be alive right now.
  //
  // OUTPUTS LIVE MEANS NOTHING MOVES. During a show the only thing moving on
  // this machine should be the show: an operator watching a programme monitor
  // does not need something crawling across the chrome in their peripheral
  // vision, and a director standing behind them needs it even less. This is the
  // rule that makes the feature safe to ship rather than a thing people switch
  // off after the first gig.
  bool creaturesShouldBeAwake() const {
    if (!project_.creaturesEnabled) {
      return false;
    }
    if (themeCreatures_.empty()) {
      return false;   // the theme did not ask for any
    }
    if (creatureHabitat_.w < 80 || creatureHabitat_.h < 70) {
      return false;   // a full playlist leaves them nowhere to be
    }
    for (const auto& output : project_.outputs) {
      if (output.enabled) {
        return false;
      }
    }
    return true;
  }

  // Fade rather than vanish. An animal that blinks out of existence the instant
  // an output is armed looks like a glitch; one that wanders off over half a
  // second looks like it noticed the show starting.
  void updateCreatures(double nowSeconds) {
    const double target = creaturesShouldBeAwake() ? 1.0 : 0.0;
    const double dt = std::clamp(nowSeconds - creatureLastTime_, 0.0, 0.1);
    creatureLastTime_ = nowSeconds;
    creatureFade_ += (target - creatureFade_) * std::min(1.0, dt * 4.0);
    if (creatureFade_ < 0.01 && target == 0.0) {
      return;   // nothing on screen: do not pay to simulate
    }
    if (creatureHabitat_.w > 8 && creatureHabitat_.h > 8) {
      deckboy::creatures::step(creatures_, creatureHabitat_, dt, nowSeconds,
                               creatureLureX_, creatureLureY_);
    }
  }

  // Rebuild the population from whatever the current theme asked for.
  void rebuildCreatures() {
    creatures_.clear();
    // Nowhere to put them yet. Placing against an empty habitat pinned every
    // animal to 0,0 and left it clamped against an edge for the rest of the
    // session -- the cat was there the whole time, drawn half outside the
    // panel and clipped away.
    if (creatureHabitat_.w < 80 || creatureHabitat_.h < 70) {
      return;
    }
    std::uint32_t seed = 1u;
    for (const auto& request : themeCreatures_) {
      for (int i = 0; i < request.count; ++i) {
        deckboy::creatures::Creature c;
        c.species = request.species;
        deckboy::creatures::place(c, creatureHabitat_, seed++);
        creatures_.push_back(c);
      }
    }
  }

  void drawOneCreature(const deckboy::creatures::Creature& c, Uint8 alpha) {
    using deckboy::creatures::Species;
    const int x = static_cast<int>(std::lround(c.x));
    const int y = static_cast<int>(std::lround(c.y));
    const double t = static_cast<double>(animationNow_) / 1000.0;
    // BOTH colours are inks ON TILE, which is the fill this space actually
    // has. The first version used pal.light for the accent -- that is a bright
    // FILL role, not an ink, so on a theme whose tile is already bright the
    // wings were the same colour as the background and a moth was one dark
    // pixel. The chrome contract says tile is filled with tile and inked with
    // fg/fgSoft, and creatures are no exception to it.
    SDL_Color ink = pal.fg;
    ink.a = alpha;
    SDL_Color accent = pal.fgSoft;
    accent.a = alpha;
    // Twice the size they started at. At one pixel per limb these read as
    // dust on the screen rather than as animals, which is not the point of
    // having them.
    constexpr int S = 2;
    auto dot = [&](int dx, int dy, int w, int h, SDL_Color colour) {
      Primitives::fillRect(controlRenderer_,
                           SDL_Rect {x + dx * S, y + dy * S, w * S, h * S}, colour);
    };
    const int face = static_cast<int>(c.facing);
    switch (c.species) {
      case Species::Moth: {
        // Wings that beat. Two rectangles whose height is the wingbeat, which
        // at this size reads as fluttering and costs four fills.
        const int beat = 1 + static_cast<int>(std::lround(
          2.0 * std::fabs(std::sin(t * 18.0 + c.phase))));
        dot(-1, 0, 2, 3, ink);                       // body
        dot(-4, -beat / 2, 3, beat + 1, accent);     // left wing
        dot(2, -beat / 2, 3, beat + 1, accent);      // right wing
        break;
      }
      case Species::Fish: {
        dot(0, 0, 5, 3, ink);                        // body
        // The tail sweeps the opposite way to the body's drift, which is what
        // makes it look like swimming rather than sliding.
        const int sweep = static_cast<int>(std::lround(std::sin(t * 5.0 + c.phase) * 1.5));
        dot(face > 0 ? -2 : 5, sweep, 2, 3, ink);
        dot(face > 0 ? 3 : 1, 1, 1, 1, accent);      // eye
        break;
      }
      case Species::Firefly: {
        SDL_Color glow = accent;
        glow.a = static_cast<Uint8>(alpha * std::clamp(c.blink, 0.0, 1.0));
        dot(0, 0, 2, 2, glow);
        // A one-pixel halo at low alpha: the difference between a lit insect
        // and a stuck pixel.
        SDL_Color halo = glow;
        halo.a = static_cast<Uint8>(halo.a / 3);
        dot(-1, -1, 4, 4, halo);
        break;
      }
      case Species::Crab: {
        const bool moving = std::fabs(c.vx) > 0.5;
        const int step = moving ? static_cast<int>(std::lround(
          std::fabs(std::sin(t * 12.0 + c.phase)))) : 0;
        dot(-3, -3, 7, 3, ink);                      // shell
        dot(-4, -1 + step, 1, 2, ink);               // legs, left
        dot(4, -1 + (1 - step), 1, 2, ink);          // legs, right
        dot(-2, -5, 1, 2, accent);                   // eyestalks
        dot(2, -5, 1, 2, accent);
        break;
      }
      case Species::Cat: {
        // Asleep: a curl, two ears, and a tail that flicks now and then.
        const double breathe = std::sin(t * 1.2 + c.phase);
        const int rise = static_cast<int>(std::lround(breathe));
        dot(-6, -4 + rise, 12, 5, ink);              // curled body
        dot(face > 0 ? 4 : -6, -6 + rise, 2, 2, ink);   // ear
        dot(face > 0 ? 1 : -3, -6 + rise, 2, 2, ink);   // ear
        const int flick = static_cast<int>(std::lround(
          std::sin(t * 0.8 + c.phase) * 2.0));
        dot(face > 0 ? -8 : 6, -2 + flick, 3, 1, ink);  // tail
        break;
      }
      default:
        break;
    }
  }

  void renderCreatures() {
    if (creatureFade_ < 0.01 || creatures_.empty()) {
      return;
    }
    const Uint8 alpha = static_cast<Uint8>(std::clamp(creatureFade_, 0.0, 1.0) * 205.0);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    for (const auto& c : creatures_) {
      drawOneCreature(c, alpha);
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
  }
