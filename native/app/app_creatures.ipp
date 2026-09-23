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
    if (colonies_.empty()) {
      return false;   // a full playlist and a full inspector leave nowhere to be
    }
    if (!project_.creaturesWhileLive) {
      for (const auto& output : project_.outputs) {
        if (output.enabled) {
          return false;
        }
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
    // Each colony is stepped against ITS OWN habitat. step() bounds a
    // creature to the band it was given, so sharing one habitat across two
    // panels would let an animal walk out of one and hover over the other.
    for (auto& colony : colonies_) {
      if (colony.home.w > 8 && colony.home.h > 8) {
        deckboy::creatures::step(colony.creatures, colony.home, dt, nowSeconds,
                                 creatureLureX_, creatureLureY_);
      }
    }
  }

  // Rebuild the population from whatever the current theme asked for.
  // Every gap in the shell the layout has told us about. A creature may only
  // ever live in one of these, which is what keeps the rule that they are
  // never drawn over a control -- the app finds the gaps, the creatures know
  // nothing about panels.
  std::vector<deckboy::creatures::Habitat> creatureHabitats() const {
    std::vector<deckboy::creatures::Habitat> out;
    auto consider = [&out](const SDL_Rect& r, bool ledge, bool darkGround = false) {
      if (r.w >= 80 && r.h >= 70) {
        out.push_back(deckboy::creatures::Habitat {r.x, r.y, r.w, r.h, ledge, darkGround});
      }
    };
    // The empty part of the playlist, under the last cue. A ledge: the row
    // above it is a surface things can walk along.
    consider(playlistFreeRect_, true);
    // The floor of the idle program monitor. A ledge, like the playlist: the
    // bottom of the monitor is a surface. Cleared the moment a cue goes live
    // (see below), so nothing is ever drawn over a picture.
    consider(idleMonitorRect_, true, /*darkGround=*/true);
    // The empty part of the inspector, under its last open section. No ledge --
    // it is a wall of panel, so this is where the fliers go.
    if (inspectorBodyRect_.w > 0 && inspectorSectionBottomMax_ > 0) {
      const int top = inspectorSectionBottomMax_ + 10;
      const int bottom = inspectorBodyRect_.y + inspectorBodyRect_.h - 6;
      consider(SDL_Rect {inspectorBodyRect_.x + 6, top,
                         inspectorBodyRect_.w - 12, std::max(0, bottom - top)}, false);
    }
    return out;
  }

  // How many animals are actually placed, across every colony. The rebuild
  // trigger asks this; it used to ask `creatures_.size()`, a vector that is
  // cleared here and filled NOWHERE -- so the answer was always 0, never
  // matched the theme's request, and rebuildCreatures() ran on every single
  // frame. Every animal was re-placed at its seed position sixty times a
  // second, which is a colony that can never take a step.
  std::size_t placedCreatureCount() const {
    std::size_t total = 0;
    for (const Colony& colony : colonies_) {
      total += colony.creatures.size();
    }
    return total;
  }

  void rebuildCreatures() {
    colonies_.clear();
    // Nowhere to put them yet. Placing against an empty habitat pinned every
    // animal to 0,0 and left it clamped against an edge for the rest of the
    // session -- the cat was there the whole time, drawn half outside the
    // panel and clipped away.
    const std::vector<deckboy::creatures::Habitat> homes = creatureHabitats();
    if (homes.empty()) {
      return;
    }
    // The theme's cast is DEALT ROUND the habitats rather than duplicated into
    // each: a theme that asks for two moths gets two moths in the window, not
    // two per gap. With one habitat this is exactly what it always did.
    std::uint32_t seed = 1u;
    colonies_.resize(homes.size());
    for (std::size_t i = 0; i < homes.size(); ++i) {
      colonies_[i].home = homes[i];
    }
    std::size_t next = 0;
    for (const auto& request : themeCreatures_) {
      for (int i = 0; i < request.count; ++i) {
        Colony& colony = colonies_[next % colonies_.size()];
        ++next;
        deckboy::creatures::Creature c;
        c.species = request.species;
        deckboy::creatures::place(c, colony.home, seed++);
        colony.creatures.push_back(c);
      }
    }
  }

  void drawOneCreature(const deckboy::creatures::Creature& c, Uint8 alpha,
                       const deckboy::creatures::Habitat& home) {
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
    // ON A DARK GROUND THEY ARE INKED BRIGHT. pal.fg is an ink for a
    // tile-filled panel; on the near-black floor of an idle monitor it is a
    // creature nobody can see -- which, with the habitat bug that kept them
    // out of the playlist gap, is the whole of why James had never seen one.
    SDL_Color ink = home.darkGround ? pal.light : pal.fg;
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
      case Species::Snail: {
        // A shell with a spiral suggested by one darker pixel, and feelers.
        dot(-4, -1, 7, 2, ink);                      // foot
        dot(-2, -5, 5, 4, accent);                   // shell
        dot(-1, -4, 2, 2, ink);                      // the whorl
        dot(face > 0 ? 3 : -5, -3, 2, 1, ink);       // feelers
        break;
      }
      case Species::Spider: {
        // The THREAD is what makes it a spider rather than a bug: a line back
        // up to wherever it started.
        // From the top of ITS OWN gap. With one habitat this was the same
        // number; with a spider in the inspector it was a thread anchored to
        // the playlist, drawn across everything in between.
        const int drop = std::max(0, y - home.y - 6);
        Primitives::fillRect(controlRenderer_,
          SDL_Rect {x, home.y + 6, 1, drop}, accent);
        dot(-2, 0, 5, 4, ink);                       // body
        const int leg = static_cast<int>(std::lround(
          std::sin(t * 6.0 + c.phase)));
        dot(-4, 1 + leg, 2, 1, ink);                 // legs
        dot(3, 1 - leg, 2, 1, ink);
        break;
      }
      case Species::Mouse: {
        const bool running = std::fabs(c.vx) > 0.5;
        dot(-4, -3, 8, 3, ink);                      // body
        dot(face > 0 ? 3 : -5, -5, 2, 2, ink);       // head
        dot(face > 0 ? 4 : -5, -6, 1, 1, accent);    // ear
        // The tail trails behind and only whips while it is moving.
        const int whip = running ? static_cast<int>(std::lround(
          std::sin(t * 16.0 + c.phase) * 2.0)) : 1;
        dot(face > 0 ? -7 : 4, -2 + whip, 3, 1, ink);
        break;
      }
      case Species::Frog: {
        const bool airborne = std::fabs(c.vy) > 1.0;
        // Legs tuck in the air and splay on the ground, which is the whole
        // difference between a hop and a slide.
        dot(-3, -4, 7, 4, ink);                      // body
        dot(face > 0 ? 2 : -3, -6, 2, 2, ink);       // head
        dot(face > 0 ? 3 : -3, -6, 1, 1, accent);    // eye
        if (airborne) {
          dot(-4, -2, 2, 2, ink);
          dot(3, -2, 2, 2, ink);
        } else {
          dot(-5, -1, 3, 1, ink);
          dot(3, -1, 3, 1, ink);
        }
        break;
      }
      case Species::Jellyfish: {
        // The bell squashes on the push and relaxes on the sink.
        const int squash = static_cast<int>(std::lround(c.blink * 2.0));
        dot(-3, -2 - squash, 7, 3 + squash, accent);   // bell
        for (int leg = 0; leg < 3; ++leg) {
          const int sway = static_cast<int>(std::lround(
            std::sin(t * 2.4 + c.phase + leg) * 1.5));
          dot(-2 + leg * 2 + sway, 1, 1, 3 - squash, ink);   // tentacles
        }
        break;
      }
      case Species::Bird: {
        const bool hopping = std::fabs(c.vx) > 0.5;
        const int hop = hopping ? static_cast<int>(std::lround(
          std::fabs(std::sin(t * 7.0 + c.phase)) * 2.0)) : 0;
        dot(-3, -5 - hop, 6, 4, ink);                // body
        dot(face > 0 ? 2 : -4, -8 - hop, 3, 3, ink); // head
        dot(face > 0 ? 5 : -5, -7 - hop, 1, 1, accent);  // beak
        dot(face > 0 ? -4 : 3, -4 - hop, 2, 2, accent); // tail
        dot(-2, -1, 1, 1 + hop, ink);                // legs
        dot(1, -1, 1, 1 + hop, ink);
        break;
      }
      default:
        break;
    }
  }

  void renderCreatures() {
    if (creatureFade_ < 0.01 || colonies_.empty()) {
      return;
    }
    const Uint8 alpha = static_cast<Uint8>(std::clamp(creatureFade_, 0.0, 1.0) * 205.0);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    for (const auto& colony : colonies_) {
      for (const auto& c : colony.creatures) {
        drawOneCreature(c, alpha, colony.home);
      }
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
  }

// ═══════════════════════════════════════════════════════════════════════════════
// BUSY CRITTERS — a critter that means "this is working"
// ═══════════════════════════════════════════════════════════════════════════════
//
// An operation that takes time and says nothing is indistinguishable from one
// that is broken. Normalise measured loudness on worker threads and kept its own
// batch counters, and not one of them was ever read by a renderer -- so it ran in
// complete silence and was reported as doing nothing. The update download had a
// status line that existed only inside the settings panel, so it was invisible
// unless you were already looking at the one place that had it.
//
// So a critter appears ON the thing that is working, scurries while it works and
// leaves when it is done. Local rather than one global spinner, because the point
// is to say WHICH thing is busy -- a status bar cannot.
//
// THESE ARE NOT THE THEME'S CREATURES above, and the difference is deliberate.
// creaturesShouldBeAwake() silences the ambient animals while an output is live
// and whenever the operator has switched them off. A busy critter obeys neither
// rule: it is information, not decoration, and silence during work is the entire
// complaint it exists to answer. What it does keep is the spirit of that rule --
// it never leaves the rect it was given, so nothing wanders the chrome mid-show.

  static constexpr int kBusyCritterFrames = 4;

  // Art lives in data/sprites/critters/<species>-<1..4>.png and is loaded through
  // the ordinary UI image path, so it goes through the same decoder and the same
  // nearest-neighbour texture rule as every other picture in the chrome.
  SDL_Texture* busyCritterFrame(const std::string& species, int frame) {
    const std::string key = species + "-" +
      std::to_string((frame % kBusyCritterFrames) + 1);
    auto it = busyCritterArt_.find(key);
    if (it == busyCritterArt_.end()) {
      UiImageAsset asset;
      asset.path = Paths::dataDir() / "sprites" / "critters" / (key + ".png");
      it = busyCritterArt_.emplace(key, std::move(asset)).first;
    }
    // A failed load latches through attemptedLoad, so a missing file is not
    // re-read every frame.
    return ensureUiImageLoaded(it->second) ? it->second.texture : nullptr;
  }

  static double busyCritterHash(const std::string& s) {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : s) {
      h = (h ^ c) * 16777619u;
    }
    return static_cast<double>(h % 10000u) / 10000.0;
  }

  // Called EVERY FRAME by whatever is working, with the rect it is working in.
  // Anything that stops calling fades and is dropped, so no operation has to
  // remember to announce that it finished -- forgetting exactly that is the
  // class of bug this whole feature is about.
  void markBusy(const std::string& id, const char* species, const SDL_Rect& where) {
    if (where.w <= 0 || where.h <= 0) {
      return;
    }
    BusyCritter& b = busyCritters_[id];
    if (b.species.empty()) {
      b.species = species;
      // Started somewhere arbitrary along the rect, so two rows busy at once are
      // not in lockstep -- that reads as one animation rather than two animals.
      b.x = 0.15 + 0.7 * busyCritterHash(id);
      b.dir = (busyCritterHash(id + "d") < 0.5) ? -1.0 : 1.0;
    }
    b.home = where;
    b.seenAtMs = SDL_GetTicks();
  }

  void serviceBusyCritters(double dt) {
    const Uint64 now = SDL_GetTicks();
    for (auto it = busyCritters_.begin(); it != busyCritters_.end(); ) {
      BusyCritter& b = it->second;
      const bool alive = (now - b.seenAtMs) < 120;
      b.fade += ((alive ? 1.0 : 0.0) - b.fade) * std::min(1.0, dt * 6.0);
      if (!alive && b.fade < 0.02) {
        it = busyCritters_.erase(it);
        continue;
      }
      b.phase += dt * 9.0;               // about nine frames a second of walk
      b.x += b.dir * dt * 0.42;          // across its own rect, in fractions
      if (b.x < 0.06) { b.x = 0.06; b.dir = 1.0; }
      if (b.x > 0.94) { b.x = 0.94; b.dir = -1.0; }
      ++it;
    }
  }

  void renderBusyCritters() {
    if (busyCritters_.empty()) {
      return;
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    for (const auto& [id, b] : busyCritters_) {
      SDL_Texture* tex = busyCritterFrame(b.species, static_cast<int>(b.phase));
      if (!tex) {
        continue;
      }
      // Sized from the rect it lives in, so one on a cue row and one on a
      // toolbar button are each right for their own furniture.
      const int size = std::clamp(b.home.h - uiScaled(6), uiScaled(12), uiScaled(28));
      const int x = b.home.x + static_cast<int>(b.x * (b.home.w - size));
      const int y = b.home.y + (b.home.h - size) / 2;
      const int bob = static_cast<int>(std::lround(std::sin(b.phase * 1.6)));
      SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(
        std::clamp(b.fade, 0.0, 1.0) * 255.0));
      SDL_FRect dst {static_cast<float>(x), static_cast<float>(y + bob),
                     static_cast<float>(size), static_cast<float>(size)};
      // Faced by flipping, so one set of art walks both ways.
      SDL_RenderTextureRotated(controlRenderer_, tex, nullptr, &dst, 0.0, nullptr,
                               b.dir < 0.0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
      SDL_SetTextureAlphaMod(tex, 255);
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
  }
