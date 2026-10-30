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
// He lives in a hole at the foot of the playlist's empty space -- the
// creatures' habitat, which by construction is never over a control -- and
// comes out when the hole is clicked, now and then on his own, and in the
// tour (app_tutorial.ipp). Never while anything is on air: the moment an
// output is on or a cue is live he goes back in, and the hole is not drawn.
// Hidden, he costs one rect test a frame.
//
// DECKBOY_TEST_RAT_DAD=1 brings him out as soon as there is somewhere to be.

  struct RatDadState {
    enum class Phase { Hidden, Out, Talking, In };
    Phase phase = Phase::Hidden;
    Uint64 phaseAtMs = 0;
    int line = 0;               // the next line to say, in order, all session
    int saidThisVisit = 0;
    Uint64 nextPeekMs = 0;      // when he comes out by himself
    SDL_Rect hole {};
    SDL_Rect body {};
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

  static const char* ratDadLine(int index) {
    static const char* kLines[] = {
      "Hey kiddo. Just checking in. You eating? There's pizza.",
      "Don't tell your mother I'm in the walls again.",
      "Why was the cue late? It got stuck in the queue.",
      "...I'll see myself out. Into the hole.",
      "Back in my day we crossfaded uphill. Both ways. In the snow.",
      "Proud of you, kiddo. Now save the show. I'll wait.",
      "Hi hungry, I'm Dad.",
      "I'm not lost. I'm taking the scenic route through your wiring.",
      "You know what this playlist needs? Pizza. Every playlist does.",
      "Your fades are smooth. You get that from your mother.",
      "I fixed the hum in your audio. It was me. I was humming.",
      "If the show goes black, that's not a mistake. That's drama.",
    };
    constexpr int count = static_cast<int>(sizeof(kLines) / sizeof(kLines[0]));
    return kLines[((index % count) + count) % count];
  }

  // Somewhere to be, and nothing on air.
  bool ratDadMayBeOut() const {
    if (playlistFreeRect_.h < 80 || playlistFreeRect_.w < 140) return false;
    if (showIsLive() || tutorial_.active || settingsOpen_) return false;
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
    if (ratDad_.nextPeekMs == 0) {
      // Now and then by himself: the first time a quarter of an hour in, then
      // every twenty minutes or so, and only when nothing is on air.
      ratDad_.nextPeekMs = now + 15ull * 60 * 1000;
      if (const char* forced = std::getenv("DECKBOY_TEST_RAT_DAD"); forced && *forced == '1') {
        ratDad_.testForced = true;
        ratDad_.nextPeekMs = now;
      }
    }
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
          ratDad_.nextPeekMs = now + (ratDad_.testForced ? 4000ull : (18ull + (ratDad_.line % 5)) * 60 * 1000);
          ratDadComeOut();
        }
        break;
      case RatDadState::Phase::Out:
        if (inPhase > 700) ratDadSetPhase(RatDadState::Phase::Talking);
        break;
      case RatDadState::Phase::Talking:
        if (inPhase > 3600) {
          ++ratDad_.line;
          ++ratDad_.saidThisVisit;
          if (ratDad_.saidThisVisit >= 2) {
            ratDadSetPhase(RatDadState::Phase::In);
          } else {
            ratDadSetPhase(RatDadState::Phase::Talking);
          }
        }
        break;
      case RatDadState::Phase::In:
        if (inPhase > 600) ratDadSetPhase(RatDadState::Phase::Hidden);
        break;
    }
  }

  void renderRatDad() {
    serviceRatDad();
    ratDad_.hole = SDL_Rect {};
    ratDad_.body = SDL_Rect {};
    if (!ratDadMayBeOut()) return;
    const SDL_Rect area = playlistFreeRect_;
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
    const std::string said = ratDadLine(ratDad_.line);
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
        ++ratDad_.line;
        ++ratDad_.saidThisVisit;
        ratDadSetPhase(ratDad_.saidThisVisit >= 2 ? RatDadState::Phase::In : RatDadState::Phase::Talking);
      }
      return true;
    }
    return false;
  }
