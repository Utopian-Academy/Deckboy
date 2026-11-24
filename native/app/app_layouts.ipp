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
  enum class LayoutHitKind { Go, Back, Hold, Skip, Transition, HotCue, Black, Row, ModeShow, ModeEdit, Fader };
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
      {"showpiece", "Showpiece"},
    };
    return choices;
  }

  // Does this layout take the bottom bar away? Operator in SHOW: everything a
  // show needs is on screen; EDIT brings the bar back with the inspector.
  bool deskLayoutHidesBottomBar() const {
    return (deskLayoutIs("operator") || deskLayoutIs("showpiece")) && !operatorEditMode_;
  }

  // The layout's whole content area, for every layout but Classic.
  void renderDeskLayout(const SDL_Rect& area) {
    if (deskLayoutIs("showpiece")) renderShowpieceLayout(area);
    else renderOperatorLayout(area);
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
        case LayoutHitKind::Fader: {
          // Where along the rail you press is the value: the top is 100%.
          const double v = 1.0 - static_cast<double>(y - hit.rect.y) / std::max(1, hit.rect.h);
          setDeckPlaylistOpacity(hit.a, static_cast<float>(std::clamp(v, 0.0, 1.0)), true);
          break;
        }
        case LayoutHitKind::ModeShow: operatorEditMode_ = false; break;
        case LayoutHitKind::ModeEdit: operatorEditMode_ = true; break;
      }
      return true;
    }
    return false;
  }

  // ── SHOWPIECE ─────────────────────────────────────────────────────────────
  //
  // Built to look incredible (the board's "Showpiece"): the programme is the
  // room's light, and the controls are instruments floating over it.
  //  - the programme, soft and dimmed, fills the window behind everything (the
  //    preview tap is 640 wide: crisp only at monitor size, so the hero monitor
  //    in the middle is the sharp one and the backdrop is ambience);
  //  - smoked-glass panels: a HUD with the live timecode, the beat and Link,
  //    the outputs; the playlists' faders; every oscillator that is moving;
  //  - GO as a dial whose ring counts the live cue down;
  //  - the cues as a filmstrip along the bottom, the live one lifted.
  // Every colour is the theme's: the glass is its deepest ink, the glow its
  // lightest, so it is a different room in every theme.

  // A translucent panel. Alpha over whatever is behind it.
  void fillGlass(const SDL_Rect& r, Uint8 alpha = 165) {
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    const SDL_Color d = pal.deep;
    SDL_SetRenderDrawColor(controlRenderer_, d.r, d.g, d.b, alpha);
    const SDL_FRect f {static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w), static_cast<float>(r.h)};
    SDL_RenderFillRect(controlRenderer_, &f);
    const SDL_Color l = pal.light;
    SDL_SetRenderDrawColor(controlRenderer_, l.r, l.g, l.b, 70);
    SDL_RenderRect(controlRenderer_, &f);
    SDL_SetRenderDrawColor(controlRenderer_, 255, 255, 255, 38);
    SDL_RenderLine(controlRenderer_, f.x + 1, f.y + 1, f.x + f.w - 2, f.y + 1);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
  }

  // A ring from 12 o'clock, clockwise, `fraction` of the way round.
  void drawRing(float cx, float cy, float rOut, float rIn, double fraction, SDL_Color c, Uint8 alpha) {
    const double f = std::clamp(fraction, 0.0, 1.0);
    const int steps = std::max(8, static_cast<int>(96 * f));
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    const SDL_FColor col {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha / 255.0f};
    const double pi = 3.14159265358979323846;
    for (int i = 0; i <= steps; ++i) {
      const double a = -pi / 2 + 2 * pi * f * i / steps;
      const float ca = static_cast<float>(std::cos(a)), sa = static_cast<float>(std::sin(a));
      v.push_back({{cx + ca * rOut, cy + sa * rOut}, col, {0, 0}});
      v.push_back({{cx + ca * rIn, cy + sa * rIn}, col, {0, 0}});
      if (i > 0) {
        const int b = (i - 1) * 2;
        idx.insert(idx.end(), {b, b + 1, b + 2, b + 1, b + 3, b + 2});
      }
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(controlRenderer_, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
  }

  // A soft glow round a rect: rings of light, fading outward.
  void drawGlowAround(const SDL_Rect& r, SDL_Color c, int layers, int step) {
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    for (int i = layers; i >= 1; --i) {
      SDL_SetRenderDrawColor(controlRenderer_, c.r, c.g, c.b, static_cast<Uint8>(10 + (layers - i) * 8));
      const SDL_FRect g {static_cast<float>(r.x - i * step), static_cast<float>(r.y - i * step),
                         static_cast<float>(r.w + i * step * 2), static_cast<float>(r.h + i * step * 2)};
      SDL_RenderFillRect(controlRenderer_, &g);
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
  }

  // A texture drawn smooth (the desk's textures are nearest-neighbour).
  void drawTextureSmooth(SDL_Texture* tex, const SDL_FRect& dst, Uint8 alpha = 255) {
    if (!tex) return;
    SDL_ScaleMode was = SDL_SCALEMODE_NEAREST;
    SDL_GetTextureScaleMode(tex, &was);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    Uint8 wasA = 255;
    SDL_GetTextureAlphaMod(tex, &wasA);
    SDL_SetTextureAlphaMod(tex, alpha);
    SDL_RenderTexture(controlRenderer_, tex, nullptr, &dst);
    SDL_SetTextureAlphaMod(tex, wasA);
    SDL_SetTextureScaleMode(tex, was);
  }

  // The live cue's position as hh:mm:ss:ff, at its own frame rate.
  std::string layoutLiveTimecode() const {
    const Cue* live = activeCuePtr();
    const MediaEngine* engine = focusedMediaEngine();
    if (!live || !engine) return "--:--:--:--";
    const double fps = live->fps > 1.0 ? live->fps : 25.0;
    const double pos = std::max(0.0, engine->position());
    const long long total = static_cast<long long>(pos);
    const int ff = static_cast<int>((pos - static_cast<double>(total)) * fps);
    char b[32];
    std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld:%02d", total / 3600, (total / 60) % 60, total % 60, ff);
    return b;
  }

  // The live cue's moving oscillators, named: geometry, any number, effects.
  struct MovingLfo { std::string name; const deckboy::effects::ParamLfo* lfo; };
  std::vector<MovingLfo> layoutMovingLfos(std::size_t most) const {
    std::vector<MovingLfo> out;
    const Cue* live = activeCuePtr();
    if (!live) return out;
    static const char* geoNames[] = {"Offset X", "Offset Y", "Scale X", "Scale Y", "Rotation",
                                     "Crop left", "Crop right", "Crop top", "Crop bottom"};
    for (std::size_t i = 0; i < live->geometryLfo.size() && i < std::size(geoNames) && out.size() < most; ++i) {
      if (live->geometryLfo[i].on) out.push_back({geoNames[i], &live->geometryLfo[i]});
    }
    for (const auto& kv : live->paramLfo) {
      if (out.size() >= most) break;
      if (!kv.second.on) continue;
      const auto* spec = deckboy::params::numericParamSpec(static_cast<NumericParam>(kv.first), live);
      out.push_back({spec ? spec->label : "a number", &kv.second});
    }
    for (const auto& fx : live->effects) {
      for (std::size_t k = 0; k < std::size(fx.lfo) && out.size() < most; ++k) {
        if (fx.lfo[k].on) out.push_back({deckboy::effects::cueEffectLabel(fx.kind), &fx.lfo[k]});
      }
    }
    return out;
  }

  void renderShowpieceLayout(const SDL_Rect& area) {
    layoutHits_.clear();
    layoutBlackRect_ = {};
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return;
    const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
    const int gap = uiScaled(14);
    const SDL_Color red {220, 50, 45, 255}, green {60, 200, 90, 255};
    const Uint64 nowMs = SDL_GetTicks();

    SDL_SetRenderClipRect(controlRenderer_, &area);
    // ── the room: the programme, soft and dimmed ──
    Primitives::fillRect(controlRenderer_, area, SDL_Color {0, 0, 0, 255});
    SDL_Texture* prog = controlPreviewTex_;
    const int pw = controlPreviewTexW_, ph = controlPreviewTexH_;
    if (prog && pw > 0 && ph > 0) {
      // Cover: fill the area, crop the overflow. Twice, slightly offset, at
      // low alpha, which reads as a soft glow rather than a stretch.
      const double s = std::max(static_cast<double>(area.w) / pw, static_cast<double>(area.h) / ph) * 1.08;
      const float dw = static_cast<float>(pw * s), dh = static_cast<float>(ph * s);
      const SDL_FRect cover {area.x + (area.w - dw) / 2, area.y + (area.h - dh) / 2, dw, dh};
      drawTextureSmooth(prog, cover, 150);
      drawTextureSmooth(prog, SDL_FRect {cover.x - dw * 0.02f, cover.y - dh * 0.02f, dw * 1.04f, dh * 1.04f}, 80);
    }
    {
      // the vignette: darker toward every edge, and a little darker overall
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      const SDL_FRect r {static_cast<float>(area.x), static_cast<float>(area.y), static_cast<float>(area.w), static_cast<float>(area.h)};
      const int bands = 18;
      for (int i = 0; i < bands; ++i) {
        const float inset = static_cast<float>((std::min(area.w, area.h) / 3) * i / bands);
        SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 14);
        const SDL_FRect in {r.x + inset, r.y + inset, r.w - inset * 2, r.h - inset * 2};
        const SDL_FRect strips[4] = {{r.x, r.y, r.w, in.y - r.y}, {r.x, in.y + in.h, r.w, r.y + r.h - in.y - in.h},
                                     {r.x, in.y, in.x - r.x, in.h}, {in.x + in.w, in.y, r.x + r.w - in.x - in.w, in.h}};
        SDL_RenderFillRects(controlRenderer_, strips, 4);
      }
      SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 70);
      SDL_RenderFillRect(controlRenderer_, &r);
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    }

    // ── the HUD ──
    TTF_Font* head = fontPixelTitle_ ? fontPixelTitle_ : (fontLarge_ ? fontLarge_ : fontBase_);
    const int hudH = textLineHeight(head) + uiScaled(18);
    SDL_Rect hud {area.x + gap, area.y + gap, area.w - gap * 2, hudH};
    fillGlass(hud);
    int hx = hud.x + uiScaled(18);
    {
      const std::string tc = layoutLiveTimecode();
      // Measured on the widest digits, so it never ellipsizes as it counts.
      const int w = measuredTextWidth(head, "88:88:88:88") * 11 / 10 + uiScaled(12);
      drawTextSafe(controlRenderer_, head, SDL_Rect {hx, hud.y, w, hud.h}, tc, pal.light);
      hx += w + uiScaled(26);
    }
    {
      // the beat: four lights, the one that is now lit; the tempo; Link
      const double beats = vjBeatCount();
      int now = static_cast<int>(std::floor(beats)) % 4;
      if (now < 0) now += 4;
      const int d = uiScaled(12);
      for (int i = 0; i < 4; ++i) {
        SDL_Rect dot {hx + i * (d + uiScaled(6)), hud.y + (hud.h - d) / 2, d, d};
        Primitives::fillRect(controlRenderer_, dot, i == now ? pal.light : paletteMix(pal.deep, pal.light, 0.25));
      }
      hx += 4 * (d + uiScaled(6)) + uiScaled(10);
      char bpm[48];
      std::snprintf(bpm, sizeof bpm, "%.1f BPM", std::clamp(project_.vjTempoBpm, 20.0, 300.0));
      std::string beatText = bpm;
      if (project_.vjLinkEnabled) beatText += "  " + deckboy::core::i18n::format("LINK {1}", {std::to_string(link_.peers())});
      const int w = measuredTextWidth(fontBase_, beatText) + uiScaled(8);
      drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {hx, hud.y, w, hud.h}, beatText, pal.fg);
      hx += w + uiScaled(20);
    }
    // SHOW / EDIT at the right end; the outputs between.
    const int segW = uiScaled(70), segH = std::max(uiScaled(26), textLineHeight(fontSmall_) + uiScaled(10));
    SDL_Rect editSeg {hud.x + hud.w - uiScaled(12) - segW, hud.y + (hud.h - segH) / 2, segW, segH};
    SDL_Rect showSeg {editSeg.x - segW - uiScaled(4), editSeg.y, segW, segH};
    fillGlass(showSeg, operatorEditMode_ ? 120 : 235);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, showSeg, "SHOW", operatorEditMode_ ? pal.fgSoft : pal.light);
    layoutHit(showSeg, LayoutHitKind::ModeShow);
    fillGlass(editSeg, operatorEditMode_ ? 235 : 120);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, editSeg, "EDIT", operatorEditMode_ ? pal.light : pal.fgSoft);
    layoutHit(editSeg, LayoutHitKind::ModeEdit);
    {
      int ox = showSeg.x - uiScaled(16);
      for (auto it = project_.outputs.rbegin(); it != project_.outputs.rend(); ++it) {
        const std::string name = it->displayName.empty() ? it->name : it->displayName;
        const std::string text = name.empty() ? std::string("Output") : name;
        const int d = uiScaled(8);
        const int w = measuredTextWidth(fontSmall_, text) + d + uiScaled(10);
        if (ox - w < hx) break;
        ox -= w;
        Primitives::fillRect(controlRenderer_, SDL_Rect {ox, hud.y + (hud.h - d) / 2, d, d}, it->enabled ? red : pal.inkSoft);
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {ox + d + uiScaled(6), hud.y, w - d - uiScaled(6), hud.h}, text,
                     it->enabled ? pal.light : pal.fgSoft);
        ox -= uiScaled(14);
      }
    }

    // ── below the HUD: faders | hero monitor | moving now + GO ──
    const int stripH = std::clamp(area.h * 22 / 100, uiScaled(120), uiScaled(220));
    SDL_Rect mid {area.x + gap, hud.y + hud.h + gap, area.w - gap * 2,
                  std::max(0, area.h - hud.h - stripH - gap * 4)};
    const int sideW = std::clamp(mid.w * 17 / 100, uiScaled(170), uiScaled(320));
    SDL_Rect faders {mid.x, mid.y, std::max(uiScaled(120), sideW * 2 / 3), mid.h};
    SDL_Rect moving {mid.x + mid.w - sideW, mid.y, sideW, mid.h * 52 / 100};
    SDL_Rect hero {faders.x + faders.w + gap, mid.y, std::max(0, moving.x - faders.x - faders.w - gap * 2), mid.h};

    // the playlists' faders: press on a rail to set it
    fillGlass(faders);
    {
      const int n = std::clamp(static_cast<int>(project_.decks.size()), 1, 6);
      const int labelH = textLineHeight(fontSmall_) + uiScaled(4);
      const int colW = (faders.w - uiScaled(16)) / n;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {faders.x + uiScaled(10), faders.y + uiScaled(6), faders.w - uiScaled(20), labelH}, "DECKS", pal.fgSoft);
      for (int d = 0; d < n; ++d) {
        const float v = std::clamp(project_.decks[static_cast<std::size_t>(d)].playlistOpacity, 0.0f, 1.0f);
        const int railW = std::clamp(colW / 4, uiScaled(10), uiScaled(22));
        SDL_Rect rail {faders.x + uiScaled(8) + d * colW + (colW - railW) / 2, faders.y + labelH + uiScaled(14), railW,
                       std::max(0, faders.h - labelH * 2 - uiScaled(30))};
        Primitives::fillRect(controlRenderer_, rail, paletteMix(pal.deep, pal.light, 0.12));
        const int fill = static_cast<int>(rail.h * v);
        const SDL_Color lit = d == deckIndex ? pal.light : paletteMix(pal.light, pal.deep, 0.35);
        Primitives::fillRect(controlRenderer_, SDL_Rect {rail.x, rail.y + rail.h - fill, rail.w, fill}, lit);
        SDL_Rect cap {rail.x - railW / 2, rail.y + rail.h - fill - uiScaled(5), railW * 2, uiScaled(10)};
        Primitives::fillRect(controlRenderer_, cap, SDL_Color {240, 240, 240, 255});
        drawCenteredTextSafe(controlRenderer_, fontSmall_, SDL_Rect {faders.x + uiScaled(8) + d * colW, rail.y + rail.h + uiScaled(6), colW, labelH},
                             "D" + std::to_string(d + 1), d == deckIndex ? pal.light : pal.fgSoft);
        layoutHit(SDL_Rect {rail.x - railW, rail.y, railW * 3, rail.h}, LayoutHitKind::Fader, d);
      }
    }

    // the hero monitor: the programme, sharp, 16:9 in the middle
    {
      const int hw = std::min(hero.w, hero.h * 16 / 9);
      const int hh = hw * 9 / 16;
      SDL_Rect mon {hero.x + (hero.w - hw) / 2, hero.y + (hero.h - hh) / 2, hw, hh};
      drawGlowAround(mon, activeCuePtr() ? red : pal.light, 6, 3);
      Primitives::fillRect(controlRenderer_, mon, SDL_Color {0, 0, 0, 255});
      if (prog && pw > 0 && ph > 0) {
        const double s = std::min(static_cast<double>(mon.w) / pw, static_cast<double>(mon.h) / ph);
        const float dw = static_cast<float>(pw * s), dh = static_cast<float>(ph * s);
        drawTextureSmooth(prog, SDL_FRect {mon.x + (mon.w - dw) / 2, mon.y + (mon.h - dh) / 2, dw, dh});
      } else {
        drawCenteredTextSafe(controlRenderer_, fontBase_, mon, "NO PROGRAMME", pal.fgSoft);
      }
    }

    // moving now: every oscillator on the live cue, drawn as its last 4 s
    fillGlass(moving);
    {
      const int pad = uiScaled(12);
      const int labelH = textLineHeight(fontSmall_) + uiScaled(4);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {moving.x + pad, moving.y + uiScaled(6), moving.w - pad * 2, labelH}, "MOVING NOW", pal.fgSoft);
      const auto lfos = layoutMovingLfos(4);
      if (lfos.empty()) {
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {moving.x + pad, moving.y + labelH + uiScaled(12), moving.w - pad * 2, labelH},
                     "nothing on an oscillator", pal.inkSoft);
      }
      const int rowH = std::max(labelH * 3, (moving.h - labelH - uiScaled(16)) / 4);
      const double now = static_cast<double>(nowMs) / 1000.0;
      const double beatsNow = vjBeatCount();
      const double bps = std::clamp(project_.vjTempoBpm, 20.0, 300.0) / 60.0;
      for (std::size_t i = 0; i < lfos.size(); ++i) {
        const int y0 = moving.y + labelH + uiScaled(10) + static_cast<int>(i) * rowH;
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {moving.x + pad, y0, moving.w - pad * 2, labelH}, lfos[i].name, pal.fg);
        SDL_Rect wave {moving.x + pad, y0 + labelH, moving.w - pad * 2, rowH - labelH - uiScaled(8)};
        if (wave.h < 8 || wave.w < 20) continue;
        std::vector<SDL_FPoint> pts;
        const int n = std::max(16, wave.w / 3);
        for (int k = 0; k <= n; ++k) {
          const double back = 4.0 * (1.0 - static_cast<double>(k) / n);
          const double u = deckboy::effects::lfoUnitValueRaw(*lfos[i].lfo, now - back, beatsNow - back * bps, 0.0);
          pts.push_back({static_cast<float>(wave.x + wave.w * k / n), static_cast<float>(wave.y + wave.h * (1.0 - std::clamp(u, 0.0, 1.0)))});
        }
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
        const SDL_Color c = pal.light;
        SDL_SetRenderDrawColor(controlRenderer_, c.r, c.g, c.b, 70);
        for (auto& p : pts) p.y += 1;
        SDL_RenderLines(controlRenderer_, pts.data(), static_cast<int>(pts.size()));
        for (auto& p : pts) p.y -= 1;
        SDL_SetRenderDrawColor(controlRenderer_, c.r, c.g, c.b, 255);
        SDL_RenderLines(controlRenderer_, pts.data(), static_cast<int>(pts.size()));
        const SDL_FRect dot {pts.back().x - 3, pts.back().y - 3, 6, 6};
        SDL_RenderFillRect(controlRenderer_, &dot);
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
      }
    }

    // GO: a dial under the oscillators, its ring counting the live cue down
    {
      SDL_Rect dialArea {moving.x, moving.y + moving.h + gap, moving.w, std::max(0, mid.y + mid.h - moving.y - moving.h - gap)};
      const int dia = std::min(dialArea.w, dialArea.h);
      if (dia > uiScaled(80)) {
        const float cx = dialArea.x + dialArea.w / 2.0f, cy = dialArea.y + dialArea.h / 2.0f;
        const float rOut = dia / 2.0f - 8.0f, ring = std::max(6.0f, rOut * 0.09f);
        double frac = 0.0;
        const double left = layoutLiveRemaining(&frac);
        drawRing(cx, cy, rOut + 7, rOut - ring - 7, 1.0, pal.light, 34);
        drawRing(cx, cy, rOut, rOut - ring, 1.0, pal.deep, 210);
        drawRing(cx, cy, rOut, rOut - ring, left >= 0.0 ? 1.0 - frac : 1.0, pal.light, 255);
        drawRing(cx, cy, rOut - ring - uiScaled(4), 0.0f, 1.0, pal.deep, 235);
        const int inner = static_cast<int>((rOut - ring) * 2 - uiScaled(10));
        SDL_Rect face {static_cast<int>(cx) - inner / 2, static_cast<int>(cy) - inner / 2, inner, inner};
        const int bigH = textLineHeight(head);
        const int smallH = textLineHeight(fontSmall_);
        const int top = face.y + (face.h - bigH - smallH * 2) / 2;
        drawCenteredTextSafe(controlRenderer_, head, SDL_Rect {face.x, top, face.w, bigH}, "GO", pal.light);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, SDL_Rect {face.x, top + bigH, face.w, smallH},
                             left >= 0.0 ? "-" + formatSeconds(left) : std::string("holds"), pal.fg);
        const Cue* next = previewCuePtr(deckIndex, nullptr);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, SDL_Rect {face.x + uiScaled(8), top + bigH + smallH, face.w - uiScaled(16), smallH},
                             next ? ellipsizeToPixelWidth(fontSmall_, next->name, face.w - uiScaled(16)) : std::string("nothing follows"), pal.fgSoft);
        layoutHit(face, LayoutHitKind::Go);
      }
    }

    // ── the filmstrip: the live cue lifted in the middle ──
    SDL_Rect strip {area.x + gap, area.y + area.h - stripH - gap, area.w - gap * 2, stripH};
    {
      const int count = static_cast<int>(deck.cues.size());
      int nextIndex = -1;
      previewCuePtr(deckIndex, &nextIndex);
      const int centre = deck.activeIndex >= 0 ? deck.activeIndex : std::max(0, nextIndex);
      const int liveH = strip.h - uiScaled(8);
      const int liveW = liveH * 16 / 12;
      const int cardH = liveH * 78 / 100, cardW = cardH * 16 / 12;
      const int labelH = textLineHeight(fontSmall_) + uiScaled(6);
      auto drawCard = [&](int i, int x, int w, int h, bool isLive) {
        const Cue& cue = deck.cues[static_cast<std::size_t>(i)];
        SDL_Rect card {x, strip.y + strip.h - h, w, h};
        if (isLive) drawGlowAround(card, red, 5, 3);
        fillGlass(card, isLive ? 235 : 185);
        SDL_Rect pic {card.x + uiScaled(4), card.y + uiScaled(4), card.w - uiScaled(8), card.h - labelH - uiScaled(6)};
        SDL_Texture* still = cueRowStillTexture(cue);
        if (!still && (cue.kind == CueKind::Video || cue.kind == CueKind::Image)) wantCueStill(cueVisualCacheKey(cue), deckIndex, i);
        if (still && pic.w > 0 && pic.h > 0) {
          float tw = 0, th = 0;
          SDL_GetTextureSize(still, &tw, &th);
          const double s = std::min(pic.w / std::max(1.0f, tw), pic.h / std::max(1.0f, th));
          const float dw = static_cast<float>(tw * s), dh = static_cast<float>(th * s);
          drawTextureSmooth(still, SDL_FRect {pic.x + (pic.w - dw) / 2, pic.y + (pic.h - dh) / 2, dw, dh}, isLive ? 255 : 190);
        } else if (pic.w > 0 && pic.h > 0) {
          drawCenteredTextSafe(controlRenderer_, fontSmall_, pic, cueKindLabel(cue.kind), pal.fgSoft);
        }
        const bool isNext = i == nextIndex && !isLive;
        SDL_Rect label {card.x + uiScaled(6), card.y + card.h - labelH, card.w - uiScaled(12), labelH};
        drawTextSafe(controlRenderer_, fontSmall_, label, ellipsizeToPixelWidth(fontSmall_, cue.name, label.w),
                     isLive ? SDL_Color {255, 255, 255, 255} : (isNext ? pal.light : pal.fgSoft));
        if (isNext) {
          Primitives::fillRect(controlRenderer_, SDL_Rect {card.x, card.y + card.h - uiScaled(3), card.w, uiScaled(3)}, green);
        }
        layoutHit(card, LayoutHitKind::Row, deckIndex, i);
      };
      if (count > 0) {
        const int c = std::clamp(centre, 0, count - 1);
        const int cx = strip.x + strip.w / 2;
        drawCard(c, cx - liveW / 2, liveW, liveH, c == deck.activeIndex);
        int right = cx + liveW / 2 + gap;
        for (int i = c + 1; i < count && right + cardW <= strip.x + strip.w; ++i) {
          drawCard(i, right, cardW, cardH, false);
          right += cardW + gap;
        }
        int left = cx - liveW / 2 - gap;
        for (int i = c - 1; i >= 0 && left - cardW >= strip.x; --i) {
          drawCard(i, left - cardW, cardW, cardH, false);
          left -= cardW + gap;
        }
      }
    }
    SDL_SetRenderClipRect(controlRenderer_, nullptr);

    // EDIT: the inspector over the oscillators and the dial, on glass.
    if (operatorEditMode_) {
      SDL_Rect drawer {moving.x - sideW / 2, mid.y, sideW + sideW / 2, mid.h};
      fillGlass(drawer, 235);
      renderInspectorBody(drawer);
    }
  }
