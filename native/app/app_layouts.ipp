// ============================================================================
// app_layouts.ipp -- DESK LAYOUTS (2026-11-24)
// ============================================================================
//
// A layout decides WHERE the desk's pieces go and WHICH appear; the theme and
// skin still decide how everything looks (pal.* colours, style_ lines). So any
// theme wears any layout. Classic is today's desk, drawn by
// renderControlWindow exactly as before. The approved board:
// https://claude.ai/artifact/G7431Y3KHQMBUuqZkdm9Zo
//
// Every size here is uiScaled or a share of the area, so a layout follows the
// UI scale and any window size.
//
// Included into the App class body from main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // One clickable piece of a layout. Layouts draw immediate-mode like the rest
  // of the desk; the press handler walks this list (handleLayoutClick).
  enum class LayoutHitKind { Go, Back, Hold, Skip, Transition, HotCue, Black, Row, ModeShow, ModeEdit };
  struct LayoutHit {
    SDL_Rect rect {};
    LayoutHitKind kind = LayoutHitKind::Go;
    int a = 0;
    int b = 0;
  };
  std::vector<LayoutHit> layoutHits_;
  bool operatorEditMode_ = false;       // Operator: the inspector replaces the GO column
  Uint64 layoutBlackHoldStart_ = 0;     // HOLD TO BLACK: when the press began
  bool layoutBlackHoldFired_ = false;
  SDL_Rect layoutBlackRect_ {};

  static constexpr double kLayoutBlackHoldMs = 500.0;

  bool deskLayoutIs(const char* name) const { return project_.deskLayout == name; }
  bool deskLayoutIsClassic() const { return project_.deskLayout.empty() || project_.deskLayout == "classic"; }

  // The layouts the Layout setting offers, in the order it lists them. Only
  // the ones that are built: a layout on the list is one that works.
  static const std::vector<std::pair<std::string, std::string>>& deskLayoutChoices() {
    static const std::vector<std::pair<std::string, std::string>> choices = {
      {"classic", "Classic"},
      {"operator", "Operator"},
    };
    return choices;
  }

  // Does this layout take the bottom bar away? Operator in SHOW: everything a
  // show needs is on screen; EDIT brings the bar back with the inspector.
  bool deskLayoutHidesBottomBar() const {
    return deskLayoutIs("operator") && !operatorEditMode_;
  }

  // ── OPERATOR ──────────────────────────────────────────────────────────────
  //
  // Built for the operator's eyes and hands during a show (the board's
  // "Operator: built for speed"):
  //  - LIVE and NEXT side by side at eye level, the live one counting down;
  //  - one running order, live row and next row marked, played rows dimmed;
  //  - GO the biggest target on screen, naming what it plays;
  //  - the things a show reaches for (back, hold, skip, the transition, hot
  //    cues) beside it, each with the key that does the same;
  //  - BLACK at the far end from GO and only on a held press;
  //  - SHOW hides editing; EDIT puts the inspector where the GO column was.

  void layoutHit(const SDL_Rect& r, LayoutHitKind kind, int a = 0, int b = 0) {
    layoutHits_.push_back({r, kind, a, b});
  }

  // A plain layout button: a panel, a big label and an optional small line.
  void drawLayoutButton(const SDL_Rect& r, const std::string& label, const std::string& subtitle,
                        SDL_Color fill, SDL_Color border, TTF_Font* font) {
    drawUIPanel(r, fill, border, pal.mid);
    const SDL_Color ink = paletteInkOnFill(fill);
    if (subtitle.empty()) {
      drawCenteredTextSafe(controlRenderer_, font, r, label, ink);
      return;
    }
    const int smallH = textLineHeight(fontSmall_);
    const int bigH = textLineHeight(font);
    const int top = r.y + (r.h - bigH - smallH) / 2;
    drawCenteredTextSafe(controlRenderer_, font, SDL_Rect {r.x, top, r.w, bigH}, label, ink);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, SDL_Rect {r.x, top + bigH, r.w, smallH}, subtitle,
                         paletteMix(ink, fill, 0.35));
  }

  // The live cue's time left, or a negative number when it holds.
  double layoutLiveRemaining(double* fraction = nullptr) const {
    if (fraction) *fraction = 0.0;
    const Cue* live = activeCuePtr();
    const MediaEngine* engine = focusedMediaEngine();
    if (!live || !engine) return -1.0;
    double end = live->outPointSeconds > 0.001 ? live->outPointSeconds : live->duration;
    if (end <= 0.0 && live->stillDurationSeconds > 0.0) end = live->stillDurationSeconds;
    if (end <= 0.0) return -1.0;
    const double start = live->inPointSeconds;
    const double pos = engine->position();
    if (fraction && end > start) *fraction = std::clamp((pos - start) / (end - start), 0.0, 1.0);
    return std::max(0.0, end - pos);
  }

  // GO in Operator plays what NEXT shows: a standby when one is set (the
  // desk's own GO), else the next cue, the same as TAKE with TAKE selects next.
  void layoutGo() {
    const int deck = project_.focusedDeckIndex;
    if (standbyIndexFor(deck) >= 0) {
      goTransport();
      return;
    }
    if (goGuarded(deck)) {
      triggerToast("GO ignored: pressed again too soon (double-GO guard)");
      return;
    }
    int next = -1;
    if (!previewCuePtr(deck, &next) || next < 0) {
      triggerToast("nothing follows");
      return;
    }
    selectCueInDeck(deck, next, false, false);
    operatorTake();
  }

  // Move NEXT up or down the running order without playing anything.
  void layoutMoveNext(int step) {
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return;
    const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
    int next = -1;
    previewCuePtr(deckIndex, &next);
    if (next < 0) next = deck.activeIndex;
    const int target = std::clamp(next + step, 0, std::max(0, static_cast<int>(deck.cues.size()) - 1));
    if (target >= 0 && target < static_cast<int>(deck.cues.size())) {
      selectCueInDeck(deckIndex, target, false, false);
    }
  }

  void renderOperatorLayout(const SDL_Rect& area) {
    layoutHits_.clear();
    layoutBlackRect_ = {};
    const int gap = uiScaled(10);
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return;
    const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];

    // ── the status row ──
    const int statusH = std::max(uiScaled(34), textLineHeight(fontBase_) + uiScaled(14));
    SDL_Rect status {area.x, area.y, area.w, statusH};
    drawUIPanel(status, pal.shellInner, pal.deep, pal.mid);
    int sx = status.x + uiScaled(12);
    {
      const Cue* live = activeCuePtr();
      std::string onAir = live ? live->name : std::string("nothing on air");
      const double left = layoutLiveRemaining();
      if (live && left >= 0.0) onAir += "   -" + formatSeconds(left);
      const int w = std::min(status.w / 3, measuredTextWidth(fontBase_, onAir) + uiScaled(8));
      drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {sx, status.y, w, status.h}, onAir, pal.light);
      sx += w + uiScaled(18);
    }
    // SHOW / EDIT, at the right end of the row.
    const int segW = uiScaled(74);
    const int segH = status.h - uiScaled(10);
    SDL_Rect showSeg {status.x + status.w - uiScaled(10) - segW * 2, status.y + uiScaled(5), segW, segH};
    SDL_Rect editSeg {showSeg.x + segW, showSeg.y, segW, segH};
    drawUIPanel(showSeg, operatorEditMode_ ? pal.tile : pal.light, pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, showSeg, "SHOW", paletteInkOnFill(operatorEditMode_ ? pal.tile : pal.light));
    drawUIPanel(editSeg, operatorEditMode_ ? pal.light : pal.tile, pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, editSeg, "EDIT", paletteInkOnFill(operatorEditMode_ ? pal.light : pal.tile));
    layoutHit(showSeg, LayoutHitKind::ModeShow);
    layoutHit(editSeg, LayoutHitKind::ModeEdit);
    // Every output's tally, then the followers, between the two.
    int cx = sx;
    const int chipH = segH;
    auto chip = [&](const std::string& text, SDL_Color dot) {
      const int w = measuredTextWidth(fontSmall_, text) + uiScaled(30);
      if (cx + w > showSeg.x - uiScaled(10)) return;
      SDL_Rect c {cx, status.y + uiScaled(5), w, chipH};
      drawUIPanel(c, pal.tile, pal.deep, pal.mid);
      const int d = uiScaled(8);
      Primitives::fillRect(controlRenderer_, SDL_Rect {c.x + uiScaled(9), c.y + (c.h - d) / 2, d, d}, dot);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {c.x + uiScaled(22), c.y, c.w - uiScaled(26), c.h}, text, pal.fg);
      cx += w + uiScaled(8);
    };
    const SDL_Color red {220, 50, 45, 255}, green {60, 200, 90, 255}, grey = pal.inkSoft;
    for (const auto& out : project_.outputs) {
      const std::string name = out.displayName.empty() ? out.name : out.displayName;
      chip(name.empty() ? std::string("Output") : name, out.enabled ? red : grey);
    }
    if (project_.syncLead) {
      chip(syncFollowers_.size() == 1 ? std::string("1 follower")
                                       : deckboy::core::i18n::format("{1} followers", {std::to_string(syncFollowers_.size())}),
           syncFollowers_.empty() ? grey : green);
    }

    SDL_Rect body {area.x, area.y + statusH + gap, area.w, std::max(0, area.h - statusH - gap)};
    const int sideW = std::clamp(body.w * 27 / 100, uiScaled(300), uiScaled(460));
    SDL_Rect side {body.x + body.w - sideW, body.y, sideW, body.h};
    SDL_Rect main {body.x, body.y, std::max(0, body.w - sideW - gap), body.h};

    // ── LIVE and NEXT ──
    const int monH = std::clamp(main.h * 44 / 100, uiScaled(160), main.w * 9 / 32 + uiScaled(30));
    const int labelH = std::max(uiScaled(14), textLineHeight(fontSmall_));
    const int nextW = (main.w - gap) * 42 / 100;
    SDL_Rect nextTile {main.x, main.y, nextW, monH};
    SDL_Rect liveTile {main.x + nextW + gap, main.y, main.w - nextW - gap, monH};
    MultiviewTile nextFeed;
    nextFeed.source = "next";
    MultiviewTile liveFeed;
    liveFeed.source = "programme";
    drawMonitorTile(nextTile, nextFeed, -2, false, labelH, uiScaled(7));
    drawMonitorTile(liveTile, liveFeed, -1, false, labelH, uiScaled(7));
    {
      double frac = 0.0;
      const double left = layoutLiveRemaining(&frac);
      if (left >= 0.0) {
        TTF_Font* big = fontLarge_ ? fontLarge_ : fontBase_;
        const std::string t = "-" + formatSeconds(left);
        const int tw = measuredTextWidth(big, t) + uiScaled(16);
        const int th = textLineHeight(big) + uiScaled(6);
        SDL_Rect tag {liveTile.x + liveTile.w - tw - uiScaled(10), liveTile.y + liveTile.h - th - labelH - uiScaled(14), tw, th};
        drawUIPanel(tag, pal.deep, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, big, tag, t, SDL_Color {255, 255, 255, 255});
        SDL_Rect bar {liveTile.x + uiScaled(4), liveTile.y + liveTile.h - uiScaled(6), liveTile.w - uiScaled(8), uiScaled(3)};
        Primitives::fillRect(controlRenderer_, bar, pal.deep);
        Primitives::fillRect(controlRenderer_, SDL_Rect {bar.x, bar.y, static_cast<int>(bar.w * frac), bar.h}, red);
      }
    }

    // ── the running order ──
    SDL_Rect list {main.x, main.y + monH + gap, main.w, std::max(0, main.h - monH - gap)};
    drawUIPanel(list, pal.tile, pal.deep, pal.mid);
    const int rowH = std::max(uiScaled(34), textLineHeight(fontBase_) + uiScaled(14));
    const int headH = std::max(uiScaled(24), textLineHeight(fontSmall_) + uiScaled(8));
    const int colNum = uiScaled(52), colType = uiScaled(96), colLen = uiScaled(96), colThen = uiScaled(110);
    const int notesW = std::max(0, list.w * 28 / 100);
    const int nameW = std::max(uiScaled(80), list.w - colNum - colType - colLen - colThen - notesW - uiScaled(24));
    auto columns = [&](int y, int h, const std::string& n, const std::string& name, const std::string& type,
                       const std::string& len, const std::string& then, const std::string& notes,
                       TTF_Font* font, SDL_Color ink, SDL_Color soft) {
      int x = list.x + uiScaled(12);
      drawTextSafe(controlRenderer_, fontMono_ ? fontMono_ : font, SDL_Rect {x, y, colNum, h}, n, ink); x += colNum;
      drawTextSafe(controlRenderer_, font, SDL_Rect {x, y, nameW - uiScaled(8), h},
                   ellipsizeToPixelWidth(font, name, nameW - uiScaled(8)), ink); x += nameW;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x, y, colType, h}, type, soft); x += colType;
      drawTextSafe(controlRenderer_, fontMono_ ? fontMono_ : font, SDL_Rect {x, y, colLen, h}, len, ink); x += colLen;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x, y, colThen, h}, then, soft); x += colThen;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x, y, notesW - uiScaled(8), h},
                   ellipsizeToPixelWidth(fontSmall_, notes, notesW - uiScaled(8)), soft);
    };
    SDL_Rect head {list.x + 1, list.y + 1, list.w - 2, headH};
    Primitives::fillRect(controlRenderer_, head, pal.shellInner);
    std::string orderTitle = "RUNNING ORDER";
    if (!deck.name.empty() && toUpper(deck.name) != orderTitle) orderTitle += "  " + deck.name;
    columns(head.y, head.h, "#", orderTitle, "TYPE", "LENGTH", "THEN", "NOTES",
            fontSmall_, pal.fgSoft, pal.fgSoft);
    int nextIndex = -1;
    previewCuePtr(deckIndex, &nextIndex);
    const int rowsFit = std::max(1, (list.h - headH - uiScaled(4)) / rowH);
    const int count = static_cast<int>(deck.cues.size());
    // Keep the live row near the top: one played row above it for context.
    int first = std::max(0, deck.activeIndex - 1);
    first = std::clamp(first, 0, std::max(0, count - rowsFit));
    SDL_Rect clip {list.x + 1, list.y + headH + 1, list.w - 2, list.h - headH - 2};
    SDL_SetRenderClipRect(controlRenderer_, &clip);
    for (int i = first; i < count && i < first + rowsFit; ++i) {
      const Cue& cue = deck.cues[static_cast<std::size_t>(i)];
      SDL_Rect row {list.x + 1, list.y + headH + 1 + (i - first) * rowH, list.w - 2, rowH};
      const bool isLive = i == deck.activeIndex;
      const bool isNext = i == nextIndex && !isLive;
      const bool played = deck.activeIndex >= 0 && i < deck.activeIndex;
      SDL_Color fill = pal.tile;
      if (isLive) fill = paletteMix(pal.tile, red, 0.30);
      else if (isNext) fill = paletteMix(pal.tile, green, 0.22);
      Primitives::fillRect(controlRenderer_, row, fill);
      Primitives::fillRect(controlRenderer_, SDL_Rect {row.x, row.y + row.h - 1, row.w, 1}, pal.mid);
      if (isLive || isNext) {
        Primitives::fillRect(controlRenderer_, SDL_Rect {row.x, row.y, uiScaled(4), row.h}, isLive ? red : green);
      }
      const SDL_Color ink = detail::paletteLiftToContrast(played ? pal.inkSoft : pal.fg, fill, 4.5);
      const SDL_Color soft = detail::paletteLiftToContrast(pal.fgSoft, fill, 3.0);
      const double len = cue.duration > 0.0 ? cue.duration : cue.stillDurationSeconds;
      char num[16];
      std::snprintf(num, sizeof num, "%02d", i + 1);
      std::string name = cue.name;
      if (isLive) name = deckboy::core::i18n::format("LIVE  {1}", {name});
      else if (isNext) name = deckboy::core::i18n::format("NEXT  {1}", {name});
      columns(row.y, row.h, num, name, cueKindLabel(cue.kind), len > 0.0 ? formatSeconds(len) : "hold",
              cueEndActionLabel(cue.endAction), cue.notes, fontBase_, ink, soft);
      layoutHit(row, LayoutHitKind::Row, deckIndex, i);
    }
    SDL_SetRenderClipRect(controlRenderer_, nullptr);

    // ── the right column: GO and the rest, or the inspector in EDIT ──
    if (operatorEditMode_) {
      renderInspectorBody(side);
      return;
    }
    int y = side.y;
    const Cue* next = previewCuePtr(deckIndex, nullptr);
    {
      const int goH = std::clamp(side.h * 30 / 100, uiScaled(120), uiScaled(240));
      SDL_Rect go {side.x, y, side.w, goH};
      // The desk's headline face (it follows the skin's typeface).
      TTF_Font* huge = fontPixelTitle_ ? fontPixelTitle_ : (fontLarge_ ? fontLarge_ : fontBase_);
      drawLayoutButton(go, "GO", next ? next->name : std::string("nothing follows"), pal.light, pal.deep, huge);
      layoutHit(go, LayoutHitKind::Go);
      y += goH + gap;
    }
    {
      const int h = std::max(uiScaled(54), textLineHeight(fontBase_) + textLineHeight(fontSmall_) + uiScaled(14));
      const int w = (side.w - gap * 2) / 3;
      SDL_Rect back {side.x, y, w, h};
      SDL_Rect hold {side.x + w + gap, y, w, h};
      SDL_Rect skip {side.x + (w + gap) * 2, y, side.w - (w + gap) * 2, h};
      drawLayoutButton(back, "BACK", "Up", pal.tile, pal.deep, fontBase_);
      drawLayoutButton(hold, "HOLD", "Space", pal.tile, pal.deep, fontBase_);
      drawLayoutButton(skip, "SKIP", "Down", pal.tile, pal.deep, fontBase_);
      layoutHit(back, LayoutHitKind::Back);
      layoutHit(hold, LayoutHitKind::Hold);
      layoutHit(skip, LayoutHitKind::Skip);
      y += h + gap;
    }
    {
      const int labelRowH = textLineHeight(fontSmall_) + uiScaled(6);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x, y, side.w, labelRowH}, "TRANSITION", pal.fgSoft);
      y += labelRowH;
      const int h = std::max(uiScaled(36), textLineHeight(fontBase_) + uiScaled(12));
      const int w = (side.w - gap * 3) / 4;
      const double secs[4] = {0.0, 1.0, 2.0, 4.0};
      const char* labels[4] = {"CUT", "1 s", "2 s", "4 s"};
      for (int k = 0; k < 4; ++k) {
        SDL_Rect r {side.x + k * (w + gap), y, k == 3 ? side.w - 3 * (w + gap) : w, h};
        const bool on = std::fabs(deck.transitionSeconds - secs[k]) < 0.01;
        drawLayoutButton(r, labels[k], "", on ? pal.light : pal.tile, pal.deep, fontBase_);
        layoutHit(r, LayoutHitKind::Transition, static_cast<int>(secs[k] * 10));
      }
      y += h + gap;
    }
    {
      // Hot cues: the cues that answer F1-F8, wherever they are.
      const int labelRowH = textLineHeight(fontSmall_) + uiScaled(6);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x, y, side.w, labelRowH}, "HOT CUES", pal.fgSoft);
      y += labelRowH;
      const int h = std::max(uiScaled(44), textLineHeight(fontSmall_) * 2 + uiScaled(10));
      const int w = (side.w - gap * 3) / 4;
      for (int k = 0; k < 8; ++k) {
        const std::string key = "F" + std::to_string(k + 1);
        std::string name;
        for (const auto& d : project_.decks) {
          for (const auto& c : d.cues) {
            if (c.triggerHotkey == key) { name = c.name; break; }
          }
          if (!name.empty()) break;
        }
        SDL_Rect r {side.x + (k % 4) * (w + gap), y + (k / 4) * (h + gap), (k % 4) == 3 ? side.w - 3 * (w + gap) : w, h};
        drawLayoutButton(r, key, name.empty() ? std::string("-") : ellipsizeToPixelWidth(fontSmall_, name, r.w - uiScaled(8)),
                         pal.tile, pal.deep, fontSmall_);
        if (!name.empty()) layoutHit(r, LayoutHitKind::HotCue, k);
      }
      y += (h + gap) * 2;
    }
    {
      // BLACK: far from GO, and only on a held press.
      const int h = std::max(uiScaled(50), textLineHeight(fontBase_) + textLineHeight(fontSmall_) + uiScaled(12));
      SDL_Rect black {side.x, side.y + side.h - h, side.w, h};
      if (black.y > y) {
        const bool dark = masterDimmerTarget_ < 0.5;
        double held = 0.0;
        if (layoutBlackHoldStart_ && !layoutBlackHoldFired_) {
          held = std::clamp((static_cast<double>(SDL_GetTicks()) - static_cast<double>(layoutBlackHoldStart_)) / kLayoutBlackHoldMs, 0.0, 1.0);
        }
        drawLayoutButton(black, dark ? "HOLD FOR PICTURE" : "HOLD TO BLACK", "press and hold half a second",
                         paletteMix(pal.tile, pal.deleteBezel, 0.35), pal.deleteBezel, fontBase_);
        if (held > 0.0) {
          Primitives::fillRect(controlRenderer_, SDL_Rect {black.x + 2, black.y + black.h - uiScaled(5),
                                                           static_cast<int>((black.w - 4) * held), uiScaled(3)}, pal.deleteBezel);
        }
        layoutBlackRect_ = black;
        layoutHit(black, LayoutHitKind::Black);
      }
    }
  }

  // Called every frame: a held press on BLACK fires once it has lasted.
  void serviceLayoutBlackHold() {
    if (!layoutBlackHoldStart_ || layoutBlackHoldFired_) return;
    if (static_cast<double>(SDL_GetTicks() - layoutBlackHoldStart_) >= kLayoutBlackHoldMs) {
      layoutBlackHoldFired_ = true;
      triggerButton("BLACK");
    }
  }

  void endLayoutPress() {
    layoutBlackHoldStart_ = 0;
    layoutBlackHoldFired_ = false;
  }

  // A press on a layout's own controls. True when it was one.
  bool handleLayoutClick(int x, int y) {
    if (deskLayoutIsClassic()) return false;
    for (const auto& hit : layoutHits_) {
      if (!pointInRect(x, y, hit.rect)) continue;
      switch (hit.kind) {
        case LayoutHitKind::Go: layoutGo(); break;
        case LayoutHitKind::Back: layoutMoveNext(-1); break;
        case LayoutHitKind::Skip: layoutMoveNext(+1); break;
        case LayoutHitKind::Hold: toggleTransport(); break;
        case LayoutHitKind::Transition: {
          const int d = project_.focusedDeckIndex;
          if (d >= 0 && d < static_cast<int>(project_.decks.size())) {
            project_.decks[static_cast<std::size_t>(d)].transitionSeconds = hit.a / 10.0;
            markProjectDirty();
            triggerToast(hit.a == 0 ? std::string("transition: cut") : deckboy::core::i18n::format("transition: {1}", {formatSeconds(hit.a / 10.0)}));
          }
          break;
        }
        case LayoutHitKind::HotCue: {
          const std::string key = "F" + std::to_string(hit.a + 1);
          fireCuesWhere([&](const Cue& cue) { return cue.triggerHotkey == key; }, "hot cue " + key);
          break;
        }
        case LayoutHitKind::Black:
          layoutBlackHoldStart_ = SDL_GetTicks();
          if (!layoutBlackHoldStart_) layoutBlackHoldStart_ = 1;
          layoutBlackHoldFired_ = false;
          break;
        case LayoutHitKind::Row:
          if (hit.a == project_.focusedDeckIndex) selectCueInDeck(hit.a, hit.b, false, false);
          break;
        case LayoutHitKind::ModeShow: operatorEditMode_ = false; break;
        case LayoutHitKind::ModeEdit: operatorEditMode_ = true; break;
      }
      return true;
    }
    return false;
  }
