// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// RAT DAD
// ============================================================================
//
// James, 2026-09-22: "somewhere in deckboy should be rat dat, hiding in a
// hole. dont even tell me" -- Rat Dad -- "fully animated with dialogue".
// Deferred that day until the rat art was better, then forgotten until James
// went looking for him on 2026-10-30. Never mention him in CHANGES, the
// manual, the README or the site (tools/audit_spoilers.py).
//
// He lives in a hole in the corner of the About page's BUILT WITH card,
// below its last line (James, 2026-10-31: not the main playlist). He comes
// out when the hole is clicked, on his own once you have lingered on the
// page a while, and in the tour (app_tutorial.ipp). Never while anything is
// on air: the moment an output is on or a cue is live he goes back in, and
// the hole is not drawn. Off that page he costs nothing.
//
// DECKBOY_TEST_RAT_DAD=1 brings him out as soon as there is somewhere to be.

  struct RatDadState {
    enum class Phase { Hidden, Out, Talking, In };
    Phase phase = Phase::Hidden;
    Uint64 phaseAtMs = 0;
    int line = 0;               // lines said this session (paces his visits)
    // RANDOM, NEVER TWICE IN A ROW (James, 2026-11-05: "more silly rat dad
    // jokes and sayings, and they should be random"). A shuffled bag of
    // jokes: every one is told before any is told again. A two-part joke
    // keeps its punchline next, even across the end of a visit.
    std::vector<int> bag;
    int joke = -1;
    bool onPunchline = false;
    int saidThisVisit = 0;
    Uint64 nextPeekMs = 0;      // when he comes out by himself
    SDL_Rect hole {};
    SDL_Rect body {};
    SDL_Rect area {};           // where he may be, as the page last drew it
    Uint64 drawnAtMs = 0;       // when it last did
    bool testForced = false;
  };
  RatDadState ratDad_;

  static constexpr int kRatDadW = 16;
  static constexpr int kRatDadH = 12;

  // In profile, facing left: a red cap, a moustache, and a slice. The rat is
  // the busy critters' rat grown up; the slice is the family business.
  static const char* const* ratDadSprite(bool mouthOpen) {
    static const char* kShut[kRatDadH] = {
      "......RRRR......",
      ".....RRRRRR.....",
      "...rrrRRRRR.EE..",
      "....GGGGGGGGEE..",
      "...GKGGGGGGGGG..",
      ".NGGGGGGGGGGGGG.",
      "NMMMGGGGWWWGGGGT",
      ".GMMGGWWWWWGGG.T",
      "UUUUWWWWWWGGGG.T",
      ".POPPWWWWGGGG..T",
      "..PP...FF..FF.TT",
      "...P............",
    };
    static const char* kOpen[kRatDadH] = {
      "......RRRR......",
      ".....RRRRRR.....",
      "...rrrRRRRR.EE..",
      "....GGGGGGGGEE..",
      "...GKGGGGGGGGG..",
      ".NGGGGGGGGGGGGG.",
      "NMMMGGGGWWWGGGGT",
      ".KKMGGWWWWWGGG.T",
      "UUUUWWWWWWGGGG.T",
      ".POPPWWWWGGGG..T",
      "..PP...FF..FF.TT",
      "...P............",
    };
    return mouthOpen ? kOpen : kShut;
  }

  void drawRatDadSprite(SDL_Renderer* renderer, int x, int y, int cell, bool mouthOpen, bool blink) {
    const char* const* rows = ratDadSprite(mouthOpen);
    for (int r = 0; r < kRatDadH; ++r) {
      for (int c = 0; c < kRatDadW; ++c) {
        SDL_Color color {};
        switch (rows[r][c]) {
          case 'R': color = {200, 36, 60, 255}; break;     // the cap
          case 'r': color = {140, 24, 40, 255}; break;     // its brim
          case 'E': color = {232, 160, 168, 255}; break;   // ear
          case 'G': color = {122, 106, 92, 255}; break;    // fur
          case 'K': color = blink && r == 4 ? SDL_Color {122, 106, 92, 255} : SDL_Color {20, 16, 12, 255}; break;
          case 'N': color = {232, 120, 140, 255}; break;   // nose
          case 'M': color = {42, 32, 24, 255}; break;      // the moustache
          case 'W': color = {176, 160, 140, 255}; break;   // belly
          case 'P': color = {242, 193, 78, 255}; break;    // cheese
          case 'U': color = {196, 132, 60, 255}; break;    // crust
          case 'O': color = {176, 48, 48, 255}; break;     // pepperoni
          case 'F': color = {232, 160, 168, 255}; break;   // feet
          case 'T': color = {214, 140, 150, 255}; break;   // tail
          default: continue;
        }
        Primitives::fillRect(renderer, SDL_Rect {x + c * cell, y + r * cell, cell, cell}, color);
      }
    }
  }

  struct RatDadJoke {
    const char* line;
    const char* punchline;   // said next, when there is one
  };

  static const std::vector<RatDadJoke>& ratDadJokes() {
    static const std::vector<RatDadJoke> kJokes = {
      {"Hey kiddo. Just checking in. You eating? There's pizza.", nullptr},
      {"Don't tell your mother I'm in the walls again.", nullptr},
      {"Back in my day we crossfaded uphill. Both ways. In the snow.", nullptr},
      {"Proud of you, kiddo. Now save the show. I'll wait.", nullptr},
      {"Hi hungry, I'm Dad.", nullptr},
      {"I'm not lost. I'm taking the scenic route through your wiring.", nullptr},
      {"You know what this playlist needs? Pizza. Every playlist does.", nullptr},
      {"Your fades are smooth. You get that from your mother.", nullptr},
      {"I fixed the hum in your audio. It was me. I was humming.", nullptr},
      {"If the show goes black, that's not a mistake. That's drama.", nullptr},
      {"Did you turn it off and on again? I always turn it off and on again. Then I nap.", nullptr},
      {"I'm not saying I chewed the HDMI cable. I'm saying it was delicious.", nullptr},
      {"Rule of thumb, kiddo: if it blinks, don't lick it. I learned that one twice.", nullptr},
      {"Every cable in this building has my teeth marks on it. That's how you know it's been tested.", nullptr},
      {"Your mother says I spend too much time in the server room. It's warm and nobody asks questions.", nullptr},
      {"I don't need a GPS. I follow the cheese.", nullptr},
      {"Label your cables, kiddo. I label all of mine 'mine'.", nullptr},
      {"4K? In my day we had four pixels and we were grateful.", nullptr},
      {"I'm on a diet. One slice per output.", nullptr},
      {"Measure twice, gnaw once.", nullptr},
      {"I'm not sleeping. I'm buffering.", nullptr},
      {"Is it hot in here, or is that the projector?", nullptr},
      {"I've been in this hole so long the hole has a hole.", nullptr},
      {"When the show's over, we're getting ice cream. Rat rules.", nullptr},
      {"I put a sandwich in the rack. For emergencies. Don't touch it, it's load-bearing.", nullptr},
      {"Sit up straight, kiddo. The camera adds ten pounds. The cheese adds the rest.", nullptr},
      {"Who left the lights on? ...Oh. That's the show. Carry on.", nullptr},
      {"Why was the cue late?", "It got stuck in the queue. ...I'll see myself out. Into the hole."},
      {"Why did the video go to therapy?", "Too many unresolved frames."},
      {"How does a rat cut between cameras?", "A cheese wipe. ...Kiddo, that one was good."},
      {"What's the difference between a cue and a queue?", "About four letters and all of my patience."},
      {"Knock knock.", "...It's me. Rat Dad. I live in the wall. You don't have to answer."},
      {"Why don't rats run sound?", "Too many squeaks in the mix."},
      {"I told your mother I'd fix the flickering monitor.", "Now it flickers on purpose. Artistic choice."},
      {"What do you call a crossfade that never ends?", "Your uncle's wedding video."},
      {"Why did the frame drop?", "It saw me coming."},
      {"Want to hear a joke about latency?", "............Okay, it's arrived. It wasn't worth the wait."},
      {"What's a projector's favourite sport?", "The long throw. ...I'll workshop it."},
      {"Did you hear about the rat who joined the AV team?", "He was great with cables. Terrible with cheese budgets. ...It was me."},
      {"I asked the vision mixer for a raise.", "It gave me a fade to black. Same thing, really."},
      {"Wear a jacket, kiddo. It's cold in the server room. That's where I keep my snacks.", nullptr},
      {"I'm not hoarding cables. I'm curating them.", nullptr},
      {"You've got your mother's eyes. And my taste in cheese.", nullptr},
      {"I'd help you with the show, but I've got paws. Paws for thought.", nullptr},
      {"Don't worry about the rumbling in the ceiling. That's just me doing my cardio.", nullptr},
      {"Your grandfather lived in a cinema. Saw every film nine times. Never paid once.", nullptr},
      {"I'm proud of you. I'd hug you, but I've been in the bins.", nullptr},
      {"Look at you, running a whole show. I remember when you could barely chew through drywall.", nullptr},
      {"No, I won't get out of the rack. It's cosy and the fans go whoosh.", nullptr},
      {"Ever notice how every venue smells like old pizza? That's not an accident.", nullptr},
      {"I've seen things, kiddo. Dropped frames. Wrong inputs. A projector lens with a fingerprint on it. Shudder.", nullptr},
      {"Don't stay up too late fiddling with the colours. ...Okay, one more tweak. Then bed.", nullptr},
      {"I don't trust wireless. I trust a cable I can bite.", nullptr},
      {"Your uncle runs a video wall in a nightclub. He hasn't slept since 2009.", nullptr},
      {"Pro tip: gaffer tape fixes everything. Except me. I'm unfixable.", nullptr},
      {"Ask me about timecode. ...No. Don't. It goes on for hours.", nullptr},
      {"In my day the remote control was a stick. You poked the telly.", nullptr},
      {"I'm not nosy. I'm a rat. It's literally my whole face.", nullptr},
      {"Save early, save often, and always tip the lighting crew. In cheese.", nullptr},
      {"Shh. I'm listening for the hum. ...There it is. It's the fridge.", nullptr},
      {"Be nice to the stagehands, kiddo. They know where the good snacks are.", nullptr},
      {"Sorry about the smell. I took a shortcut through the fog machine.", nullptr},
      {"Why did the rat bring a ladder to the show?", "He heard the projector had a high throw. ...Okay, that's two throw jokes. I'm done. I'm not done."},
      {"How many rats does it take to change a projector bulb?", "None. We like it dark."},
      {"Why was the cable so calm?", "It was well grounded. Unlike your mother's cousin."},
      {"What do you call a cheese that's not yours?", "Nacho cheese. ...I've been saving that one since 1998."},
      {"Why did the playlist break up with the cue?", "It needed some space. And a fade out."},
      {"What do you call a rat with a microphone?", "A squeak-er. Get it? Speaker. I'll be here all week. In the wall."},
      {"Why don't monitors ever win arguments?", "They always lose their signal."},
      {"Did you hear about the projector that went to school?", "It wanted to get a little brighter."},
      {"Why did the HDMI cable apologise?", "It had a bad connection. Like me and your mother's sister."},
      {"What's a camera's favourite drink?", "Shots. ...Not for you, kiddo. Water."},
      {"Why did the subtitle cross the road?", "To get to the other side of the sentence."},
      {"What do you call a crossfade between two cheeses?", "A brie-ze. ...I'm a genius."},
      {"Why was the speaker so good at gossip?", "It knew how to amplify a story."},
      {"What did the dimmer say to the light?", "Don't worry, it's just a phase."},
      {"Why did the rat get a job in video?", "He heard there were lots of frames to chew."},
      {"What do you call a nervous video signal?", "A jittery feed. ...Your uncle again."},
      {"Why did the tripod sit down?", "Its legs were tired. Three of them. Imagine."},
      {"What's the scariest thing in a control room?", "A blank screen and a silent phone. Brrr."},
    };
    return kJokes;
  }

  // Draws the next joke from the bag, refilled and shuffled when empty, and
  // never the one just told.
  void ratDadNextJoke() {
    const int count = static_cast<int>(ratDadJokes().size());
    if (ratDad_.bag.empty()) {
      static std::mt19937 rng {std::random_device {}()};
      for (int i = 0; i < count; ++i) ratDad_.bag.push_back(i);
      std::shuffle(ratDad_.bag.begin(), ratDad_.bag.end(), rng);
      if (ratDad_.bag.size() > 1 && ratDad_.bag.back() == ratDad_.joke) {
        std::swap(ratDad_.bag.front(), ratDad_.bag.back());
      }
    }
    ratDad_.joke = ratDad_.bag.back();
    ratDad_.bag.pop_back();
    ratDad_.onPunchline = false;
  }

  // The joke's punchline is still to come.
  bool ratDadPunchlinePending() const {
    const auto& jokes = ratDadJokes();
    return ratDad_.joke >= 0 && ratDad_.joke < static_cast<int>(jokes.size()) &&
           jokes[static_cast<std::size_t>(ratDad_.joke)].punchline && !ratDad_.onPunchline;
  }

  // On to what he says next: the punchline, or another joke.
  void ratDadAdvanceLine() {
    ++ratDad_.line;
    if (ratDadPunchlinePending()) {
      ratDad_.onPunchline = true;
    } else {
      ratDadNextJoke();
    }
  }

  std::string ratDadSays() {
    if (ratDad_.joke < 0) ratDadNextJoke();
    const RatDadJoke& joke = ratDadJokes()[static_cast<std::size_t>(ratDad_.joke)];
    return ratDad_.onPunchline && joke.punchline ? joke.punchline : joke.line;
  }

  // Somewhere to be -- the page drawn a moment ago, with room -- and nothing
  // on air.
  bool ratDadMayBeOut() const {
    if (!settingsOpen_ || settingsTab_ != 4 || SDL_GetTicks() - ratDad_.drawnAtMs > 250) return false;
    if (ratDad_.area.h < uiScaled(44) || ratDad_.area.w < uiScaled(140)) return false;
    if (showIsLive() || tutorial_.active) return false;
    for (const auto& output : project_.outputs) {
      if (output.enabled) return false;
    }
    return true;
  }

  void ratDadSetPhase(RatDadState::Phase phase) {
    ratDad_.phase = phase;
    ratDad_.phaseAtMs = SDL_GetTicks();
  }

  void ratDadComeOut() {
    if (ratDad_.phase != RatDadState::Phase::Hidden) return;
    ratDad_.saidThisVisit = 0;
    ratDadSetPhase(RatDadState::Phase::Out);
  }

  void serviceRatDad() {
    const Uint64 now = SDL_GetTicks();
    if (now - ratDad_.drawnAtMs > 250) {
      // Just arrived on the page: he stays in, and peeks out by himself only
      // if you are still reading eight seconds later.
      ratDad_.phase = RatDadState::Phase::Hidden;
      ratDad_.nextPeekMs = now + 8000;
      if (const char* forced = std::getenv("DECKBOY_TEST_RAT_DAD"); forced && *forced == '1') {
        ratDad_.testForced = true;
        ratDad_.nextPeekMs = now;
      }
    }
    ratDad_.drawnAtMs = now;
    const bool mayBeOut = ratDadMayBeOut();
    if (!mayBeOut) {
      // Something went on air: straight back in, no goodbye.
      ratDad_.phase = RatDadState::Phase::Hidden;
      return;
    }
    const Uint64 inPhase = now - ratDad_.phaseAtMs;
    switch (ratDad_.phase) {
      case RatDadState::Phase::Hidden:
        if (now >= ratDad_.nextPeekMs) {
          ratDad_.nextPeekMs = now + (ratDad_.testForced ? 4000ull : 45000ull + (ratDad_.line % 5) * 15000ull);
          ratDadComeOut();
        }
        break;
      case RatDadState::Phase::Out:
        if (inPhase > 700) ratDadSetPhase(RatDadState::Phase::Talking);
        break;
      case RatDadState::Phase::Talking:
        if (inPhase > 3600) {
          // Two lines a visit, and a third when the second was a setup: a
          // joke is never left without its punchline.
          ++ratDad_.saidThisVisit;
          const bool done = ratDad_.saidThisVisit >= 2 && !ratDadPunchlinePending();
          ratDadAdvanceLine();
          ratDadSetPhase(done ? RatDadState::Phase::In : RatDadState::Phase::Talking);
        }
        break;
      case RatDadState::Phase::In:
        if (inPhase > 600) ratDadSetPhase(RatDadState::Phase::Hidden);
        break;
    }
  }

  // area: the empty corner of the page he lives in, never over a control.
  void renderRatDad(const SDL_Rect& area) {
    ratDad_.area = area;
    serviceRatDad();
    ratDad_.hole = SDL_Rect {};
    ratDad_.body = SDL_Rect {};
    if (!ratDadMayBeOut()) return;
    const int cell = std::max(2, uiScaled(3));
    const int ratW = kRatDadW * cell;
    const int ratH = kRatDadH * cell;
    // The hole: a dark arch in the bottom right corner of the empty space.
    const int holeW = ratH + cell * 2;
    const int holeH = ratH + cell;
    const SDL_Rect hole {area.x + area.w - holeW - uiScaled(10), area.y + area.h - holeH - uiScaled(6), holeW, holeH};
    ratDad_.hole = hole;
    // An arch, rimmed in the theme's soft ink so it reads on a dark theme
    // and a light one alike, and filled near-black.
    const SDL_Color dark {static_cast<Uint8>(pal.deep.r / 3), static_cast<Uint8>(pal.deep.g / 3),
                          static_cast<Uint8>(pal.deep.b / 3), 255};
    auto arch = [&](const SDL_Rect& r, int inset, SDL_Color c) {
      const SDL_Rect a {r.x + inset, r.y + inset, r.w - inset * 2, r.h - inset};
      Primitives::fillRect(controlRenderer_, SDL_Rect {a.x, a.y + a.h / 3, a.w, a.h - a.h / 3}, c);
      Primitives::fillRect(controlRenderer_, SDL_Rect {a.x + cell, a.y + a.h / 6, a.w - cell * 2, a.h / 3}, c);
      Primitives::fillRect(controlRenderer_, SDL_Rect {a.x + cell * 3, a.y, a.w - cell * 6, a.h / 4}, c);
    };
    arch(hole, 0, pal.fgSoft);
    arch(hole, std::max(1, cell / 2), dark);
    if (ratDad_.phase == RatDadState::Phase::Hidden) return;

    // Out of the hole to the left, and back.
    const Uint64 now = SDL_GetTicks();
    const double inPhase = static_cast<double>(now - ratDad_.phaseAtMs);
    double out = 1.0;
    if (ratDad_.phase == RatDadState::Phase::Out) out = std::clamp(inPhase / 700.0, 0.0, 1.0);
    if (ratDad_.phase == RatDadState::Phase::In) out = 1.0 - std::clamp(inPhase / 600.0, 0.0, 1.0);
    const int x = hole.x + hole.w / 2 - static_cast<int>(out * (ratW + uiScaled(8)));
    const int bob = ratDad_.phase == RatDadState::Phase::Talking ? static_cast<int>((now / 220) % 2) * cell : 0;
    const int y = hole.y + hole.h - ratH - bob;
    const bool talking = ratDad_.phase == RatDadState::Phase::Talking;
    const bool mouthOpen = talking && inPhase < 2600 && ((now / 110) % 2) == 0;
    const bool blink = (now % 3700) < 130;
    // Clipped at the hole's mouth, so he comes OUT of it rather than through
    // the wall beside it.
    const SDL_Rect clip {area.x, area.y, hole.x + hole.w / 2 - area.x, area.h};
    SDL_SetRenderClipRect(controlRenderer_, &clip);
    drawRatDadSprite(controlRenderer_, x, y, cell, mouthOpen, blink);
    SDL_SetRenderClipRect(controlRenderer_, nullptr);
    ratDad_.body = SDL_Rect {x, y, ratW, ratH};

    if (!talking || !fontSmall_) return;
    // What he says, in a bubble above him, inside the empty space.
    const std::string said = ratDadSays();
    const int pad = uiScaled(6);
    const int maxW = std::max(uiScaled(120), std::min(area.w - uiScaled(16), uiScaled(300)));
    const auto lines = codeWrapText(fontSmall_, said, maxW - pad * 2);
    const int lineH = textLineHeight(fontSmall_);
    int bubbleW = 0;
    for (const auto& l : lines) bubbleW = std::max(bubbleW, measuredWidthIn(fontSmall_, l));
    bubbleW += pad * 2;
    const int bubbleH = static_cast<int>(lines.size()) * lineH + pad * 2;
    const int bx = std::clamp(x + ratW / 2 - bubbleW / 2, area.x + uiScaled(4), area.x + area.w - bubbleW - uiScaled(4));
    const int by = std::max(area.y + uiScaled(4), y - bubbleH - cell * 3);
    const SDL_Color paper {244, 244, 232, 255};
    const SDL_Color ink {30, 26, 20, 255};
    Primitives::fillRect(controlRenderer_, SDL_Rect {bx - 1, by - 1, bubbleW + 2, bubbleH + 2}, ink);
    Primitives::fillRect(controlRenderer_, SDL_Rect {bx, by, bubbleW, bubbleH}, paper);
    // The tail of the bubble, pointing at him.
    const int tx = std::clamp(x + ratW / 3, bx + cell, bx + bubbleW - cell * 3);
    Primitives::fillRect(controlRenderer_, SDL_Rect {tx, by + bubbleH, cell * 2, cell}, paper);
    Primitives::fillRect(controlRenderer_, SDL_Rect {tx, by + bubbleH + cell, cell, cell}, paper);
    for (std::size_t i = 0; i < lines.size(); ++i) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {bx + pad, by + pad + static_cast<int>(i) * lineH, bubbleW - pad * 2, lineH},
                   lines[i], ink);
    }
  }

  // A click on the hole brings him out; a click on him moves him on.
  bool handleRatDadMouseDown(int x, int y) {
    if (!ratDadMayBeOut()) return false;
    const SDL_Point at {x, y};
    if (ratDad_.phase == RatDadState::Phase::Hidden) {
      if (ratDad_.hole.w > 0 && SDL_PointInRect(&at, &ratDad_.hole)) {
        ratDadComeOut();
        return true;
      }
      return false;
    }
    if ((ratDad_.body.w > 0 && SDL_PointInRect(&at, &ratDad_.body)) ||
        (ratDad_.hole.w > 0 && SDL_PointInRect(&at, &ratDad_.hole))) {
      if (ratDad_.phase == RatDadState::Phase::Talking) {
        ++ratDad_.saidThisVisit;
        const bool done = ratDad_.saidThisVisit >= 2 && !ratDadPunchlinePending();
        ratDadAdvanceLine();
        ratDadSetPhase(done ? RatDadState::Phase::In : RatDadState::Phase::Talking);
      }
      return true;
    }
    return false;
  }
