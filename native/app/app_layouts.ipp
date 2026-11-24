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
  enum class LayoutHitKind { Go, Back, Hold, Skip, Transition, HotCue, Black, Row, ModeShow, ModeEdit, Fader, Cut, Clip, StopDeck, Quick, Crossfade, VjOn, PauseAll, Panic, HotCut, Style, Duration, Ftb, Tab };
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
      {"rundown", "Rundown"},
      {"clipgrid", "Clip Grid"},
      {"cuestack", "Cue Stack"},
      {"console", "Console"},
      {"compact", "Compact"},
    };
    return choices;
  }

  // Does this layout take the bottom bar away? Operator in SHOW: everything a
  // show needs is on screen; EDIT brings the bar back with the inspector.
  bool deskLayoutHidesBottomBar() const {
    return !deskLayoutIsClassic() && !operatorEditMode_;
  }

  // SHOW / EDIT, the pair every layout but Classic carries: SHOW hides the
  // editing (and the bottom bar), EDIT brings the inspector and the bar back.
  // `stickerRoom`: the strip left of the pair is empty, so a skin's stickers
  // can go there (drawLayoutStickers). Rundown's holds the clock.
  void drawLayoutModePair(const SDL_Rect& right, bool stickerRoom = true) {
    const int segW = std::max(uiScaled(66), measuredTextWidth(fontSmall_, "SHOW") + uiScaled(24));
    SDL_Rect editSeg {right.x + right.w - segW, right.y, segW, right.h};
    SDL_Rect showSeg {editSeg.x - segW, right.y, segW, right.h};
    if (stickerRoom) layoutStickerStrip_ = {right.x, right.y, std::max(0, showSeg.x - uiScaled(8) - right.x), right.h};
    for (int k = 0; k < 2; ++k) {
      const SDL_Rect& seg = k == 0 ? showSeg : editSeg;
      const bool on = (k == 1) == operatorEditMode_;
      drawUIPanel(seg, on ? pal.light : pal.tile, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, seg, k == 0 ? "SHOW" : "EDIT", paletteInkOnFill(on ? pal.light : pal.tile));
      layoutHit(seg, k == 0 ? LayoutHitKind::ModeShow : LayoutHitKind::ModeEdit);
    }
  }

  // The layout's whole content area, for every layout but Classic.
  SDL_Rect layoutStickerStrip_ {};

  // A skin's stickers, slapped in a row next to SHOW / EDIT, the way a
  // sticker goes on the one bare bit of a panel.
  void drawLayoutStickers() {
    const SDL_Rect strip = layoutStickerStrip_;
    if (strip.w <= 0 || deckboy::render::gChrome.stickers.empty()) return;
    const float base = static_cast<float>(uiScaled(30));
    float x = static_cast<float>(strip.x + strip.w);
    for (const auto& sticker : deckboy::render::gChrome.stickers) {
      const float size = base * sticker.size;
      x -= size * 0.75f;
      if (x - size * 0.6f < static_cast<float>(strip.x)) break;
      Primitives::drawSticker(controlRenderer_, sticker, x, static_cast<float>(strip.y) + strip.h * 0.5f, size,
                              static_cast<std::uint32_t>(strip.x * 7 + static_cast<int>(x)));
      x -= size * 0.85f;
    }
  }

  void renderDeskLayout(const SDL_Rect& area) {
    layoutStickerStrip_ = {};
    renderDeskLayoutBody(area);
    drawLayoutStickers();
  }

  void renderDeskLayoutBody(const SDL_Rect& area) {
    if (deskLayoutIs("showpiece")) renderShowpieceLayout(area);
    else if (deskLayoutIs("rundown")) renderRundownLayout(area);
    else if (deskLayoutIs("clipgrid")) renderClipGridLayout(area);
    else if (deskLayoutIs("cuestack")) renderCueStackLayout(area);
    else if (deskLayoutIs("console")) renderConsoleLayout(area);
    else if (deskLayoutIs("compact")) renderCompactLayout(area);
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

  // The face to draw `text` in within `w`: `font`, or the next smaller one
  // that fits. The headline face is wide; a narrow box must not ellipsize a
  // countdown or turn AUTO into "AU...".
  TTF_Font* layoutFitFont(TTF_Font* font, const std::string& text, int w) {
    for (TTF_Font* smaller : {fontLarge_, fontBase_, fontSmall_}) {
      if (!font || !smaller || measuredTextWidth(font, text) <= w) break;
      font = smaller;
    }
    return font;
  }

  // A plain layout button: a panel, a big label and an optional small line.
  void drawLayoutButton(const SDL_Rect& r, const std::string& label, const std::string& subtitle,
                        SDL_Color fill, SDL_Color border, TTF_Font* font) {
    drawUIPanel(r, fill, border, pal.mid);
    const SDL_Color ink = paletteInkOnFill(fill);
    // The label steps down a face until it fits: the headline face is wide,
    // and a narrow window must not turn AUTO into "AU...".
    font = layoutFitFont(font, label, r.w - uiScaled(12));
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
    for (int o = 0; o < static_cast<int>(project_.outputs.size()); ++o) {
      chip(outputLabel(o), project_.outputs[static_cast<std::size_t>(o)].enabled ? red : grey);
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
        case LayoutHitKind::Cut: layoutCut(); break;
        case LayoutHitKind::Clip: layoutFireClip(hit.a, hit.b); break;
        case LayoutHitKind::StopDeck: layoutStopDeck(hit.a); break;
        case LayoutHitKind::Quick: dispatchQuickAction(static_cast<QuickAction>(hit.a), 0); break;
        case LayoutHitKind::Crossfade:
          setVjMix(static_cast<double>(x - hit.rect.x) / std::max(1, hit.rect.w));
          break;
        case LayoutHitKind::VjOn: setVjMode(true); break;
        case LayoutHitKind::PauseAll: {
          const int acted = transportAllDecks(CueTargetVerb::PauseAll);
          triggerToast(deckboy::core::i18n::format("paused {1}", {std::to_string(acted)}));
          break;
        }
        case LayoutHitKind::Panic: triggerPanicProfile(); break;
        case LayoutHitKind::HotCut: layoutHotCut(hit.a, hit.b); break;
        case LayoutHitKind::Style: {
          const auto& styles = consoleStyles();
          const int d = project_.focusedDeckIndex;
          if (hit.a >= 0 && hit.a < static_cast<int>(styles.size()) && d >= 0 && d < static_cast<int>(project_.decks.size())) {
            project_.decks[static_cast<std::size_t>(d)].transitionStyle = transitionStyleToken(styles[static_cast<std::size_t>(hit.a)]);
            markProjectDirty();
          }
          break;
        }
        case LayoutHitKind::Duration: {
          const int d = project_.focusedDeckIndex;
          if (d >= 0 && d < static_cast<int>(project_.decks.size())) {
            double& s = project_.decks[static_cast<std::size_t>(d)].transitionSeconds;
            s = std::clamp(std::round((s + hit.a * 0.25) * 4.0) / 4.0, 0.0, 30.0);
            markProjectDirty();
          }
          break;
        }
        case LayoutHitKind::Ftb: triggerButton("BLACK"); break;
        case LayoutHitKind::Tab: compactTab_ = hit.a; break;
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
      for (int o = static_cast<int>(project_.outputs.size()) - 1; o >= 0; --o) {
        const auto* it = &project_.outputs[static_cast<std::size_t>(o)];
        const std::string text = outputLabel(o);
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

  // ── RUNDOWN ───────────────────────────────────────────────────────────────
  //
  // A newsroom's desk (the board's "Rundown"): preview and programme with a
  // multiview of the other playlists, CUT and AUTO, and the rundown itself,
  // back-timed: when each item should hit air if the show runs as planned.

  // CUT: the next cue, now, with no transition; the selection moves on.
  void layoutCut() {
    const int deck = project_.focusedDeckIndex;
    if (goGuarded(deck)) {
      triggerToast("CUT ignored: pressed again too soon (double-GO guard)");
      return;
    }
    int next = -1;
    if (!previewCuePtr(deck, &next) || next < 0) {
      triggerToast("nothing follows");
      return;
    }
    selectCueInDeck(deck, next, false, false);
    takeSelected(jumpTriggersPlayback(), false);
    selectNextAfterTake();
  }

  // A clock time from seconds since midnight, local.
  static std::string layoutClockText(double secondsOfDay) {
    long long s = static_cast<long long>(std::floor(secondsOfDay));
    s = ((s % 86400) + 86400) % 86400;
    char b[16];
    std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    return b;
  }

  static double layoutSecondsOfDayNow() {
    const std::time_t t = std::time(nullptr);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    return local.tm_hour * 3600.0 + local.tm_min * 60.0 + local.tm_sec;
  }

  static double layoutCueLength(const Cue& cue) {
    double end = cue.outPointSeconds > 0.001 ? cue.outPointSeconds : cue.duration;
    if (end <= 0.0) end = cue.stillDurationSeconds;
    return end > cue.inPointSeconds ? end - cue.inPointSeconds : 0.0;
  }

  void renderRundownLayout(const SDL_Rect& area) {
    layoutHits_.clear();
    layoutBlackRect_ = {};
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return;
    const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
    const int gap = uiScaled(10);
    const SDL_Color red {220, 50, 45, 255}, green {60, 200, 90, 255};
    const int labelH = std::max(uiScaled(14), textLineHeight(fontSmall_));

    // ── the monitors ──
    const int monH = std::clamp(area.h * 40 / 100, uiScaled(150), uiScaled(520));
    const int btnW = std::clamp(area.w * 8 / 100, uiScaled(90), uiScaled(150));
    const int mvW = std::clamp(area.w * 26 / 100, uiScaled(200), uiScaled(640));
    const int pairW = std::max(0, area.w - btnW - mvW - gap * 3);
    SDL_Rect pvw {area.x, area.y, (pairW - gap) / 2, monH};
    SDL_Rect pgm {pvw.x + pvw.w + gap, area.y, pairW - pvw.w - gap, monH};
    SDL_Rect mv {pgm.x + pgm.w + gap, area.y, mvW, monH};
    SDL_Rect btns {mv.x + mv.w + gap, area.y, area.x + area.w - mv.x - mv.w - gap, monH};
    MultiviewTile feed;
    feed.source = "next";
    drawMonitorTile(pvw, feed, -2, false, labelH, uiScaled(7));
    feed.source = "programme";
    drawMonitorTile(pgm, feed, -1, false, labelH, uiScaled(7));
    {
      // the other playlists, then the outputs, four windows
      std::vector<MultiviewTile> others;
      for (int d = 0; d < static_cast<int>(project_.decks.size()) && others.size() < 4; ++d) {
        if (d == deckIndex) continue;
        MultiviewTile t;
        t.source = "deck:" + std::to_string(d);
        others.push_back(t);
      }
      // then what each output carries, then the scopes and the clock
      for (const auto& out : project_.outputs) {
        if (others.size() >= 2 || out.outputId.empty()) break;
        MultiviewTile t;
        t.source = "output:" + out.outputId;
        others.push_back(t);
      }
      for (const char* fill : {"scopes", "clock", "", ""}) {
        if (others.size() >= 4) break;
        MultiviewTile t;
        t.source = fill;
        others.push_back(t);
      }
      const int cw = (mv.w - gap) / 2, ch = (mv.h - gap) / 2;
      for (int i = 0; i < 4; ++i) {
        SDL_Rect tile {mv.x + (i % 2) * (cw + gap), mv.y + (i / 2) * (ch + gap), i % 2 ? mv.w - cw - gap : cw, i / 2 ? mv.h - ch - gap : ch};
        const MultiviewTile& t = others[static_cast<std::size_t>(i)];
        if (t.source.empty()) {
          drawUIPanel(tile, pal.deep, pal.deep, pal.mid);
          continue;
        }
        drawMonitorTile(tile, t, multiviewTileDeck(t), false, labelH, uiScaled(7));
      }
    }
    {
      // CUT and AUTO, and what AUTO will do
      const int bh = (btns.h - gap * 2 - labelH * 2) / 2;
      SDL_Rect cut {btns.x, btns.y, btns.w, bh};
      SDL_Rect autoBtn {btns.x, btns.y + bh + gap, btns.w, bh};
      drawLayoutButton(cut, "CUT", "", paletteMix(pal.tile, red, 0.55), pal.deep, fontPixelTitle_ ? fontPixelTitle_ : fontLarge_);
      drawLayoutButton(autoBtn, "AUTO", "", pal.light, pal.deep, fontPixelTitle_ ? fontPixelTitle_ : fontLarge_);
      layoutHit(cut, LayoutHitKind::Cut);
      layoutHit(autoBtn, LayoutHitKind::Go);
      const std::string trans = deck.transitionSeconds > 0.01 ? formatSeconds(deck.transitionSeconds) : std::string("cut");
      drawCenteredTextSafe(controlRenderer_, fontSmall_, SDL_Rect {btns.x, autoBtn.y + autoBtn.h + uiScaled(4), btns.w, labelH * 2},
                           deckboy::core::i18n::format("AUTO: {1}", {trans}), pal.fgSoft);
    }

    // ── the clock row ──
    const double nowDay = layoutSecondsOfDayNow();
    const int clockH = std::max(uiScaled(34), textLineHeight(fontLarge_ ? fontLarge_ : fontBase_) + uiScaled(8));
    SDL_Rect clockRow {area.x, area.y + monH + gap, area.w, clockH};
    int nextIndex = -1;
    previewCuePtr(deckIndex, &nextIndex);
    // back-time: from the live cue's end, each following cue in turn; a cue
    // that holds stops the estimate (nobody knows when its GO comes)
    const int count = static_cast<int>(deck.cues.size());
    std::vector<double> airAt(static_cast<std::size_t>(count), -1.0);
    double endAt = -1.0;
    {
      double t = nowDay;
      bool known = true;
      if (deck.activeIndex >= 0 && deck.activeIndex < count) {
        const double left = layoutLiveRemaining();
        const MediaEngine* engine = focusedMediaEngine();
        const Cue& live = deck.cues[static_cast<std::size_t>(deck.activeIndex)];
        airAt[static_cast<std::size_t>(deck.activeIndex)] = nowDay - std::max(0.0, (engine ? engine->position() : 0.0) - live.inPointSeconds);
        if (left < 0.0) known = false;
        else t += left;
      }
      for (int i = std::max(0, deck.activeIndex + 1); i < count && known; ++i) {
        const Cue& c = deck.cues[static_cast<std::size_t>(i)];
        if (!c.armed) continue;
        airAt[static_cast<std::size_t>(i)] = t;
        const double len = layoutCueLength(c);
        if (len <= 0.0) { known = false; break; }
        t += len;
      }
      if (known) endAt = t;
    }
    {
      TTF_Font* big = fontLarge_ ? fontLarge_ : fontBase_;
      int x = clockRow.x + uiScaled(4);
      const std::string now = layoutClockText(nowDay);
      const int w = measuredTextWidth(big, "88:88:88") + uiScaled(16);
      drawTextSafe(controlRenderer_, big, SDL_Rect {x, clockRow.y, w, clockRow.h}, now, pal.light);
      x += w + uiScaled(10);
      std::string ends = endAt >= 0.0 ? deckboy::core::i18n::format("ends about {1}", {layoutClockText(endAt)})
                                       : std::string("ends: a cue holds for its GO");
      drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {x, clockRow.y, clockRow.w - (x - clockRow.x) - uiScaled(160), clockRow.h}, ends, pal.fgSoft);
      const int pillH = std::max(uiScaled(26), textLineHeight(fontSmall_) + uiScaled(10));
      drawLayoutModePair(SDL_Rect {clockRow.x, clockRow.y + (clockRow.h - pillH) / 2, clockRow.w, pillH}, false);
    }

    // ── the rundown ──
    SDL_Rect list {area.x, clockRow.y + clockRow.h + gap, area.w, std::max(0, area.y + area.h - clockRow.y - clockRow.h - gap)};
    if (operatorEditMode_) {
      // EDIT: the inspector beside the rundown
      const int insW = std::clamp(list.w * 30 / 100, uiScaled(300), uiScaled(520));
      renderInspectorBody(SDL_Rect {list.x + list.w - insW, list.y, insW, list.h});
      list.w -= insW + gap;
    }
    drawUIPanel(list, pal.tile, pal.deep, pal.mid);
    const int rowH = std::max(uiScaled(32), textLineHeight(fontBase_) + uiScaled(12));
    const int headH = std::max(uiScaled(24), textLineHeight(fontSmall_) + uiScaled(8));
    const int cPage = uiScaled(60), cSrc = uiScaled(190), cLen = uiScaled(100), cBack = uiScaled(110), cStatus = uiScaled(120), cThen = uiScaled(100);
    const int cSlug = std::max(uiScaled(100), list.w - cPage - cSrc - cLen - cBack - cStatus - cThen - uiScaled(24));
    auto row = [&](int y, int h, const std::string& page, const std::string& slug, const std::string& src, const std::string& len,
                   const std::string& back, const std::string& status, SDL_Color statusFill, const std::string& then,
                   TTF_Font* font, SDL_Color ink, SDL_Color soft) {
      int x = list.x + uiScaled(12);
      drawTextSafe(controlRenderer_, fontMono_ ? fontMono_ : font, SDL_Rect {x, y, cPage, h}, page, ink); x += cPage;
      drawTextSafe(controlRenderer_, font, SDL_Rect {x, y, cSlug - uiScaled(8), h}, ellipsizeToPixelWidth(font, slug, cSlug - uiScaled(8)), ink); x += cSlug;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x, y, cSrc - uiScaled(8), h}, ellipsizeToPixelWidth(fontSmall_, src, cSrc - uiScaled(8)), soft); x += cSrc;
      drawTextSafe(controlRenderer_, fontMono_ ? fontMono_ : font, SDL_Rect {x, y, cLen, h}, len, ink); x += cLen;
      drawTextSafe(controlRenderer_, fontMono_ ? fontMono_ : font, SDL_Rect {x, y, cBack, h}, back, soft); x += cBack;
      if (statusFill.a) {
        const int tw = measuredTextWidth(fontSmall_, status) + uiScaled(14);
        SDL_Rect chip {x, y + (h - textLineHeight(fontSmall_) - uiScaled(6)) / 2, std::min(tw, cStatus - uiScaled(8)), textLineHeight(fontSmall_) + uiScaled(6)};
        Primitives::fillRect(controlRenderer_, chip, statusFill);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, chip, status, paletteInkOnFill(statusFill));
      } else {
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x, y, cStatus, h}, status, soft);
      }
      x += cStatus;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x, y, cThen, h}, then, soft);
    };
    SDL_Rect head {list.x + 1, list.y + 1, list.w - 2, headH};
    Primitives::fillRect(controlRenderer_, head, pal.shellInner);
    row(head.y, head.h, "PAGE", "SLUG", "SOURCE", "LENGTH", "BACK-TIME", "STATUS", SDL_Color {0, 0, 0, 0}, "THEN",
        fontSmall_, pal.fgSoft, pal.fgSoft);
    const int rowsFit = std::max(1, (list.h - headH - uiScaled(4)) / rowH);
    int first = std::max(0, deck.activeIndex - 2);
    first = std::clamp(first, 0, std::max(0, count - rowsFit));
    SDL_Rect clip {list.x + 1, list.y + headH + 1, list.w - 2, list.h - headH - 2};
    SDL_SetRenderClipRect(controlRenderer_, &clip);
    for (int i = first; i < count && i < first + rowsFit; ++i) {
      const Cue& cue = deck.cues[static_cast<std::size_t>(i)];
      SDL_Rect r {list.x + 1, list.y + headH + 1 + (i - first) * rowH, list.w - 2, rowH};
      const bool isLive = i == deck.activeIndex;
      const bool isNext = i == nextIndex && !isLive;
      const bool done = deck.activeIndex >= 0 && i < deck.activeIndex;
      SDL_Color fill = pal.tile;
      if (isLive) fill = paletteMix(pal.tile, red, 0.30);
      else if (isNext) fill = paletteMix(pal.tile, green, 0.22);
      Primitives::fillRect(controlRenderer_, r, fill);
      Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y + r.h - 1, r.w, 1}, pal.mid);
      std::string status = "READY";
      SDL_Color statusFill = paletteMix(pal.tile, pal.deep, 0.25);
      if (isLive) { status = "ON AIR"; statusFill = red; }
      else if (isNext) { status = "PREVIEW"; statusFill = green; }
      else if (cue.mediaMissing) { status = "MISSING"; statusFill = pal.deleteBezel; }
      else if (!cue.armed) { status = "SKIPPED"; statusFill = SDL_Color {0, 0, 0, 0}; }
      else if (done) { status = "DONE"; statusFill = SDL_Color {0, 0, 0, 0}; }
      const SDL_Color ink = detail::paletteLiftToContrast(done ? pal.inkSoft : pal.fg, fill, 4.5);
      const SDL_Color soft = detail::paletteLiftToContrast(pal.fgSoft, fill, 3.0);
      char page[16];
      std::snprintf(page, sizeof page, "%c%d", static_cast<char>('A' + std::min(25, i / 10)), i % 10 + 1);
      const double len = layoutCueLength(cue);
      const double at = airAt[static_cast<std::size_t>(i)];
      row(r.y, r.h, page, cue.name, cueKindLabel(cue.kind) + "  " + deck.name, len > 0.0 ? formatSeconds(len) : "hold",
          at >= 0.0 ? layoutClockText(at) : std::string("--"), status, statusFill, cueEndActionLabel(cue.endAction),
          fontBase_, ink, soft);
      layoutHit(r, LayoutHitKind::Row, deckIndex, i);
    }
    SDL_SetRenderClipRect(controlRenderer_, nullptr);
  }

  // ── CLIP GRID ─────────────────────────────────────────────────────────────
  //
  // For VJs (the board's "Clip Grid"): every playlist is a column of clips,
  // fired with one press (on the beat when quantise is on, the desk's own
  // take); the tempo, Link and quantise above; the programme, the live cue's
  // effects and the crossfader at the side.

  // Fire one clip: its playlist becomes the focus and takes it.
  void layoutFireClip(int deckIndex, int cueIndex) {
    if (!setFocusedDeckIndex(deckIndex)) return;
    selectCueInDeck(deckIndex, cueIndex, false, false);
    jumpSelectedCue();
  }

  void layoutStopDeck(int deckIndex) {
    if (!setFocusedDeckIndex(deckIndex)) return;
    stopTransport();
  }

  void renderClipGridLayout(const SDL_Rect& area) {
    layoutHits_.clear();
    layoutBlackRect_ = {};
    const int gap = uiScaled(10);
    const SDL_Color red {220, 50, 45, 255}, amber {240, 180, 41, 255};

    // ── the tempo row ──
    const int barH = std::max(uiScaled(40), textLineHeight(fontBase_) + uiScaled(16));
    SDL_Rect bar {area.x, area.y, area.w, barH};
    {
      int x = bar.x;
      auto button = [&](int w, const std::string& label, bool on, QuickAction action) {
        SDL_Rect r {x, bar.y, w, bar.h};
        drawLayoutButton(r, label, "", on ? pal.light : pal.tile, pal.deep, fontBase_);
        layoutHit(r, LayoutHitKind::Quick, static_cast<int>(action));
        x += w + gap;
      };
      button(uiScaled(80), "TAP", false, QuickAction::VjTapTempo);
      {
        TTF_Font* big = fontLarge_ ? fontLarge_ : fontBase_;
        char bpm[24];
        std::snprintf(bpm, sizeof bpm, "%.1f", std::clamp(project_.vjTempoBpm, 20.0, 300.0));
        const int w = measuredTextWidth(big, "888.8") + uiScaled(12);
        drawTextSafe(controlRenderer_, big, SDL_Rect {x, bar.y, w, bar.h}, bpm, pal.light);
        x += w;
        const double beats = vjBeatCount();
        int now = static_cast<int>(std::floor(beats)) % 4;
        if (now < 0) now += 4;
        const int d = uiScaled(14);
        for (int i = 0; i < 4; ++i) {
          Primitives::fillRect(controlRenderer_, SDL_Rect {x + i * (d + uiScaled(5)), bar.y + (bar.h - d) / 2, d, d},
                               i == now ? amber : paletteMix(pal.tile, pal.deep, 0.4));
        }
        x += 4 * (d + uiScaled(5)) + gap;
      }
      std::string link = project_.vjLinkEnabled ? deckboy::core::i18n::format("LINK {1}", {std::to_string(link_.peers())})
                                                : std::string("LINK");
      button(std::max(uiScaled(100), measuredTextWidth(fontBase_, link) + uiScaled(24)), link, project_.vjLinkEnabled, QuickAction::VjToggleLink);
      const std::string quant = deckboy::vjclock::quantiseLabel({project_.vjQuantiseTakes, project_.vjQuantiseBeats});
      button(std::max(uiScaled(150), measuredTextWidth(fontBase_, quant) + uiScaled(24)), quant, project_.vjQuantiseTakes, QuickAction::VjCycleQuantise);
      // SHOW / EDIT at the right end
      drawLayoutModePair(SDL_Rect {x, bar.y + uiScaled(4), bar.x + bar.w - x, bar.h - uiScaled(8)});
    }

    SDL_Rect body {area.x, area.y + barH + gap, area.w, std::max(0, area.h - barH - gap)};
    const int sideW = std::clamp(body.w * 28 / 100, uiScaled(300), uiScaled(560));
    SDL_Rect side {body.x + body.w - sideW, body.y, sideW, body.h};
    SDL_Rect grid {body.x, body.y, std::max(0, body.w - sideW - gap), body.h};

    // ── the grid: a column per playlist ──
    drawUIPanel(grid, pal.tile, pal.deep, pal.mid);
    {
      const int deckCount = static_cast<int>(project_.decks.size());
      const int cols = std::clamp(deckCount, 1, 6);
      const int pad = uiScaled(8);
      const int colW = (grid.w - pad * (cols + 1)) / cols;
      const int headH = textLineHeight(fontSmall_) + uiScaled(8);
      const int stopH = std::max(uiScaled(28), textLineHeight(fontSmall_) + uiScaled(10));
      const int tileH = std::max(uiScaled(70), colW * 9 / 16 + textLineHeight(fontSmall_) + uiScaled(10));
      const int labelH = textLineHeight(fontSmall_) + uiScaled(6);
      for (int d = 0; d < cols; ++d) {
        const Deck& dk = project_.decks[static_cast<std::size_t>(d)];
        const int x = grid.x + pad + d * (colW + pad);
        char pct[16];
        std::snprintf(pct, sizeof pct, "%d%%", static_cast<int>(std::lround(dk.playlistOpacity * 100)));
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x, grid.y + uiScaled(4), colW - uiScaled(40), headH},
                     ellipsizeToPixelWidth(fontSmall_, dk.name, colW - uiScaled(44)), d == project_.focusedDeckIndex ? pal.light : pal.fgSoft);
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x + colW - uiScaled(40), grid.y + uiScaled(4), uiScaled(40), headH}, pct, pal.fgSoft);
        const int top = grid.y + headH + uiScaled(6);
        const int bottom = grid.y + grid.h - stopH - pad * 2;
        const int fit = std::max(1, (bottom - top + pad) / (tileH + pad));
        const int count = static_cast<int>(dk.cues.size());
        // keep the live clip on screen
        int first = std::clamp(dk.activeIndex - fit / 2, 0, std::max(0, count - fit));
        int nextIndex = -1;
        previewCuePtr(d, &nextIndex);
        for (int i = first; i < count && i < first + fit; ++i) {
          const Cue& cue = dk.cues[static_cast<std::size_t>(i)];
          SDL_Rect tile {x, top + (i - first) * (tileH + pad), colW, tileH};
          const bool live = i == dk.activeIndex;
          const bool queued = vjTakeDeck_ == d && i == dk.selectedIndex && !live && project_.vjQuantiseTakes;
          drawUIPanel(tile, pal.deep, live ? red : (queued ? amber : pal.deep), pal.mid);
          if (live || queued) {
            const SDL_Color c = live ? red : amber;
            const int t = std::max(2, uiScaled(2));
            Primitives::fillRect(controlRenderer_, SDL_Rect {tile.x, tile.y, tile.w, t}, c);
            Primitives::fillRect(controlRenderer_, SDL_Rect {tile.x, tile.y + tile.h - t, tile.w, t}, c);
            Primitives::fillRect(controlRenderer_, SDL_Rect {tile.x, tile.y, t, tile.h}, c);
            Primitives::fillRect(controlRenderer_, SDL_Rect {tile.x + tile.w - t, tile.y, t, tile.h}, c);
          }
          SDL_Rect pic {tile.x + uiScaled(3), tile.y + uiScaled(3), tile.w - uiScaled(6), tile.h - labelH - uiScaled(4)};
          SDL_Texture* still = cueRowStillTexture(cue);
          if (!still && (cue.kind == CueKind::Video || cue.kind == CueKind::Image)) wantCueStill(cueVisualCacheKey(cue), d, i);
          if (still && pic.w > 0 && pic.h > 0) {
            float tw = 0, th = 0;
            SDL_GetTextureSize(still, &tw, &th);
            const double s = std::min(pic.w / std::max(1.0f, tw), pic.h / std::max(1.0f, th));
            const float dw = static_cast<float>(tw * s), dh = static_cast<float>(th * s);
            drawTextureSmooth(still, SDL_FRect {pic.x + (pic.w - dw) / 2, pic.y + (pic.h - dh) / 2, dw, dh});
          } else if (pic.w > 0 && pic.h > 0) {
            drawCenteredTextSafe(controlRenderer_, fontSmall_, pic, cueKindLabel(cue.kind), pal.fgSoft);
          }
          SDL_Rect label {tile.x + uiScaled(6), tile.y + tile.h - labelH, tile.w - uiScaled(12), labelH};
          std::string tag = live ? "LIVE" : (queued ? "NEXT BEAT" : (i == nextIndex && d == project_.focusedDeckIndex ? "NEXT" : ""));
          const int tagW = tag.empty() ? 0 : measuredTextWidth(fontSmall_, tag) + uiScaled(6);
          drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {label.x, label.y, label.w - tagW, label.h},
                       ellipsizeToPixelWidth(fontSmall_, cue.name, label.w - tagW), pal.light);
          if (!tag.empty()) {
            drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {label.x + label.w - tagW, label.y, tagW, label.h}, tag, live ? red : amber);
          }
          layoutHit(tile, LayoutHitKind::Clip, d, i);
        }
        SDL_Rect stop {x, grid.y + grid.h - stopH - pad, colW, stopH};
        drawLayoutButton(stop, "STOP", "", pal.tile, pal.deep, fontSmall_);
        layoutHit(stop, LayoutHitKind::StopDeck, d);
      }
    }

    // ── the side: programme, effects, crossfader ──
    if (operatorEditMode_) {
      renderInspectorBody(side);
      return;
    }
    const int labelH = std::max(uiScaled(14), textLineHeight(fontSmall_));
    const int monH = std::min(side.h * 40 / 100, side.w * 9 / 16 + labelH + uiScaled(8));
    MultiviewTile feed;
    feed.source = "programme";
    drawMonitorTile(SDL_Rect {side.x, side.y, side.w, monH}, feed, -1, false, labelH, uiScaled(7));
    const int xfH = std::max(uiScaled(64), labelH * 2 + uiScaled(30));
    SDL_Rect fxPanel {side.x, side.y + monH + gap, side.w, std::max(0, side.h - monH - xfH - gap * 2)};
    drawUIPanel(fxPanel, pal.tile, pal.deep, pal.mid);
    {
      const Cue* live = activeCuePtr();
      const int pad = uiScaled(10);
      const int rowH = std::max(uiScaled(36), textLineHeight(fontBase_) + uiScaled(14));
      std::string title = "EFFECTS";
      if (live) title += "  " + live->name;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {fxPanel.x + pad, fxPanel.y + uiScaled(4), fxPanel.w - pad * 2, labelH},
                   ellipsizeToPixelWidth(fontSmall_, title, fxPanel.w - pad * 2), pal.fgSoft);
      int y = fxPanel.y + labelH + uiScaled(10);
      if (!live || live->effects.empty()) {
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {fxPanel.x + pad, y, fxPanel.w - pad * 2, labelH},
                     live ? "no effects on the live cue" : "nothing live", pal.inkSoft);
      } else {
        for (const auto& fx : live->effects) {
          if (y + rowH > fxPanel.y + fxPanel.h - pad) break;
          SDL_Rect r {fxPanel.x + pad, y, fxPanel.w - pad * 2, rowH - uiScaled(6)};
          drawUIPanel(r, pal.shellInner, pal.deep, pal.mid);
          const int nameW = r.w * 40 / 100;
          drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {r.x + uiScaled(8), r.y, nameW, r.h},
                       ellipsizeToPixelWidth(fontBase_, deckboy::effects::cueEffectLabel(fx.kind), nameW), pal.fg);
          // the amount as a bar; a ~ when something on it moves
          SDL_Rect meter {r.x + nameW + uiScaled(12), r.y + r.h / 2 - uiScaled(4), r.w - nameW - uiScaled(60), uiScaled(8)};
          Primitives::fillRect(controlRenderer_, meter, pal.deep);
          Primitives::fillRect(controlRenderer_, SDL_Rect {meter.x, meter.y, static_cast<int>(meter.w * std::clamp(fx.amount, 0.0f, 1.0f)), meter.h}, pal.light);
          bool moving = false;
          for (const auto& l : fx.lfo) moving = moving || l.on;
          if (moving) {
            drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {r.x + r.w - uiScaled(36), r.y, uiScaled(30), r.h}, "~", amber);
          }
          y += rowH;
        }
      }
    }
    SDL_Rect xf {side.x, side.y + side.h - xfH, side.w, xfH};
    drawUIPanel(xf, pal.tile, pal.deep, pal.mid);
    if (project_.vjModeEnabled && project_.decks.size() > 1) {
      const int pad = uiScaled(14);
      const int deckCount = static_cast<int>(project_.decks.size());
      const std::string a = project_.decks[static_cast<std::size_t>(std::clamp(project_.vjDeckA, 0, deckCount - 1))].name;
      const std::string b = project_.decks[static_cast<std::size_t>(std::clamp(project_.vjDeckB, 0, deckCount - 1))].name;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {xf.x + pad, xf.y + uiScaled(4), xf.w / 2, labelH}, a, pal.fgSoft);
      const int bw = measuredTextWidth(fontSmall_, b);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {xf.x + xf.w - pad - bw, xf.y + uiScaled(4), bw + 1, labelH}, b, pal.fgSoft);
      SDL_Rect rail {xf.x + pad, xf.y + labelH + uiScaled(18), xf.w - pad * 2, uiScaled(10)};
      Primitives::fillRect(controlRenderer_, rail, pal.deep);
      const int capW = uiScaled(40);
      const int cx = rail.x + static_cast<int>((rail.w - capW) * std::clamp(project_.vjMixPosition, 0.0, 1.0));
      Primitives::fillRect(controlRenderer_, SDL_Rect {cx, rail.y - uiScaled(10), capW, rail.h + uiScaled(20)}, SDL_Color {225, 225, 225, 255});
      layoutHit(SDL_Rect {rail.x, rail.y - uiScaled(14), rail.w, rail.h + uiScaled(28)}, LayoutHitKind::Crossfade);
    } else {
      drawLayoutButton(SDL_Rect {xf.x + uiScaled(10), xf.y + uiScaled(10), xf.w - uiScaled(20), xf.h - uiScaled(20)},
                       "VJ MODE", "two playlists and a crossfader", pal.tile, pal.deep, fontBase_);
      layoutHit(SDL_Rect {xf.x + uiScaled(10), xf.y + uiScaled(10), xf.w - uiScaled(20), xf.h - uiScaled(20)}, LayoutHitKind::VjOn);
    }
  }

  // ── CUE STACK ─────────────────────────────────────────────────────────────
  //
  // For theatre and conferences (the board's "Cue Stack"): big rows with
  // pre-wait, action, post-wait and continue; the cue standing by in the
  // middle of the eye line with GO beside it; the stage manager's notes; and
  // the running cue's countdown in large type.

  // Words onto lines no wider than `w`; draws them from `y`, returns the y
  // after the last line. Stops at `bottom`.
  int drawWrappedLines(TTF_Font* font, const std::string& text, int x, int y, int w, int bottom, SDL_Color ink) {
    const int lineH = textLineHeight(font) + uiScaled(2);
    std::string line;
    std::size_t at = 0;
    auto flush = [&]() {
      if (!line.empty() && y + lineH <= bottom) {
        drawTextSafe(controlRenderer_, font, SDL_Rect {x, y, w, lineH}, line, ink);
        y += lineH;
      }
      line.clear();
    };
    while (at <= text.size()) {
      std::size_t end = text.find_first_of(" \n", at);
      if (end == std::string::npos) end = text.size();
      const std::string word = text.substr(at, end - at);
      const std::string tryLine = line.empty() ? word : line + " " + word;
      if (!line.empty() && measuredTextWidth(font, tryLine) > w) {
        flush();
        line = word;
      } else {
        line = tryLine;
      }
      if (end < text.size() && text[end] == '\n') flush();
      at = end + 1;
    }
    flush();
    return y;
  }

  static const char* layoutContinueLabel(CueContinueMode mode) {
    switch (mode) {
      case CueContinueMode::AutoContinue: return "continue";
      case CueContinueMode::AutoFollow: return "follow";
      default: return "no";
    }
  }

  void renderCueStackLayout(const SDL_Rect& area) {
    layoutHits_.clear();
    layoutBlackRect_ = {};
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return;
    const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
    const int gap = uiScaled(12);
    const SDL_Color red {220, 50, 45, 255}, green {60, 200, 90, 255};
    const int sideW = std::clamp(area.w * 26 / 100, uiScaled(300), uiScaled(520));
    SDL_Rect side {area.x + area.w - sideW, area.y, sideW, area.h};
    SDL_Rect main {area.x, area.y, std::max(0, area.w - sideW - gap), area.h};
    int nextIndex = -1;
    const Cue* next = previewCuePtr(deckIndex, &nextIndex);
    const Cue* live = activeCuePtr();
    TTF_Font* big = fontPixelTitle_ ? fontPixelTitle_ : (fontLarge_ ? fontLarge_ : fontBase_);

    // ── GO | standing by | running ──
    const int topH = std::max(uiScaled(96), textLineHeight(big) + textLineHeight(fontBase_) * 2 + uiScaled(20));
    {
      const int goW = std::clamp(main.w * 20 / 100, uiScaled(140), uiScaled(280));
      const int runW = std::clamp(main.w * 18 / 100, uiScaled(150), uiScaled(260));
      SDL_Rect go {main.x, main.y, goW, topH};
      SDL_Rect run {main.x + main.w - runW, main.y, runW, topH};
      SDL_Rect sb {go.x + go.w + gap, main.y, std::max(0, run.x - go.x - go.w - gap * 2), topH};
      drawLayoutButton(go, "GO", "", pal.light, pal.deep, big);
      layoutHit(go, LayoutHitKind::Go);
      drawUIPanel(sb, pal.tile, green, pal.mid);
      const int pad = uiScaled(12);
      const int smallH = textLineHeight(fontSmall_) + uiScaled(4);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {sb.x + pad, sb.y + uiScaled(6), sb.w - pad * 2, smallH}, "STANDING BY", green);
      TTF_Font* nameFont = fontLarge_ ? fontLarge_ : fontBase_;
      const int nameH = textLineHeight(nameFont) + uiScaled(4);
      drawTextSafe(controlRenderer_, nameFont, SDL_Rect {sb.x + pad, sb.y + uiScaled(6) + smallH, sb.w - pad * 2, nameH},
                   next ? ellipsizeToPixelWidth(nameFont, next->name, sb.w - pad * 2) : std::string("nothing follows"), pal.fg);
      if (next) {
        const double len = layoutCueLength(*next);
        std::string detail = deckboy::core::i18n::format("pre-wait {1}  -  {2}  -  then {3}",
          {formatSeconds(next->preWaitSeconds), len > 0.0 ? formatSeconds(len) : std::string("holds"),
           next->continueMode == CueContinueMode::DoNotContinue ? std::string("waits for GO")
             : deckboy::core::i18n::format("continues after {1}", {formatSeconds(next->postWaitSeconds)})});
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {sb.x + pad, sb.y + uiScaled(6) + smallH + nameH, sb.w - pad * 2, smallH},
                     ellipsizeToPixelWidth(fontSmall_, detail, sb.w - pad * 2), pal.fgSoft);
      }
      drawUIPanel(run, pal.tile, live ? red : pal.deep, pal.mid);
      std::string runLabel = live ? deckboy::core::i18n::format("{1} RUNNING", {live->name}) : std::string("NOTHING RUNNING");
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {run.x + pad, run.y + uiScaled(6), run.w - pad * 2, smallH},
                   ellipsizeToPixelWidth(fontSmall_, runLabel, run.w - pad * 2), live ? red : pal.fgSoft);
      const double left = layoutLiveRemaining();
      const std::string count = left >= 0.0 ? "-" + formatSeconds(left) : std::string(live ? "holds" : "--");
      drawCenteredTextSafe(controlRenderer_, layoutFitFont(big, "-00:00.0", run.w - uiScaled(16)),
                           SDL_Rect {run.x, run.y + smallH, run.w, run.h - smallH}, count, pal.fg);
    }

    // ── the stack ──
    SDL_Rect list {main.x, main.y + topH + gap, main.w, std::max(0, main.h - topH - gap)};
    drawUIPanel(list, pal.tile, pal.deep, pal.mid);
    TTF_Font* rowFont = fontLarge_ ? fontLarge_ : fontBase_;
    const int rowH = std::max(uiScaled(44), textLineHeight(rowFont) + uiScaled(16));
    const int headH = std::max(uiScaled(26), textLineHeight(fontSmall_) + uiScaled(8));
    const int cQ = uiScaled(70), cPre = uiScaled(100), cAct = uiScaled(110), cPost = uiScaled(100), cCont = uiScaled(110);
    const int cWhat = std::max(uiScaled(120), list.w - cQ - cPre - cAct - cPost - cCont - uiScaled(28));
    auto cols = [&](int y, int h, const std::string& q, const std::string& what, const std::string& pre, const std::string& act,
                    const std::string& post, const std::string& cont, TTF_Font* font, SDL_Color ink, SDL_Color soft) {
      int x = list.x + uiScaled(16);
      TTF_Font* mono = fontMono_ ? fontMono_ : font;
      drawTextSafe(controlRenderer_, font, SDL_Rect {x, y, cQ, h}, q, ink); x += cQ;
      drawTextSafe(controlRenderer_, font, SDL_Rect {x, y, cWhat - uiScaled(8), h}, ellipsizeToPixelWidth(font, what, cWhat - uiScaled(8)), ink); x += cWhat;
      drawTextSafe(controlRenderer_, mono, SDL_Rect {x, y, cPre, h}, pre, soft); x += cPre;
      drawTextSafe(controlRenderer_, mono, SDL_Rect {x, y, cAct, h}, act, ink); x += cAct;
      drawTextSafe(controlRenderer_, mono, SDL_Rect {x, y, cPost, h}, post, soft); x += cPost;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x, y, cCont, h}, cont, soft);
    };
    SDL_Rect head {list.x + 1, list.y + 1, list.w - 2, headH};
    Primitives::fillRect(controlRenderer_, head, pal.shellInner);
    cols(head.y, head.h, "CUE", "WHAT HAPPENS", "PRE-WAIT", "ACTION", "POST-WAIT", "CONTINUE", fontSmall_, pal.fgSoft, pal.fgSoft);
    const int count = static_cast<int>(deck.cues.size());
    const int rowsFit = std::max(1, (list.h - headH - uiScaled(4)) / rowH);
    // the standing-by row sits a third of the way down, where the eye rests
    const int anchor = nextIndex >= 0 ? nextIndex : std::max(0, deck.activeIndex);
    int first = std::clamp(anchor - rowsFit / 3, 0, std::max(0, count - rowsFit));
    SDL_Rect clip {list.x + 1, list.y + headH + 1, list.w - 2, list.h - headH - 2};
    SDL_SetRenderClipRect(controlRenderer_, &clip);
    for (int i = first; i < count && i < first + rowsFit; ++i) {
      const Cue& cue = deck.cues[static_cast<std::size_t>(i)];
      SDL_Rect r {list.x + 1, list.y + headH + 1 + (i - first) * rowH, list.w - 2, rowH};
      const bool isLive = i == deck.activeIndex;
      const bool isNext = i == nextIndex && !isLive;
      const bool done = deck.activeIndex >= 0 && i < deck.activeIndex;
      SDL_Color fill = pal.tile;
      if (isLive) fill = paletteMix(pal.tile, red, 0.30);
      else if (isNext) fill = paletteMix(pal.tile, green, 0.25);
      Primitives::fillRect(controlRenderer_, r, fill);
      Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y + r.h - 1, r.w, 1}, pal.mid);
      if (isNext) Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y, uiScaled(6), r.h}, green);
      if (isLive) Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y, uiScaled(6), r.h}, red);
      const SDL_Color ink = detail::paletteLiftToContrast(done || !cue.armed ? pal.inkSoft : pal.fg, fill, 4.5);
      const SDL_Color soft = detail::paletteLiftToContrast(pal.fgSoft, fill, 3.0);
      const double len = layoutCueLength(cue);
      cols(r.y, r.h, std::to_string(i + 1), cue.name, formatSeconds(cue.preWaitSeconds), len > 0.0 ? formatSeconds(len) : "holds",
           formatSeconds(cue.postWaitSeconds), layoutContinueLabel(cue.continueMode), isNext ? fontLarge_ ? fontLarge_ : rowFont : fontBase_, ink, soft);
      layoutHit(r, LayoutHitKind::Row, deckIndex, i);
    }
    SDL_SetRenderClipRect(controlRenderer_, nullptr);

    // ── the side: programme, notes, back / pause all / panic ──
    if (operatorEditMode_) {
      const int pillH = std::max(uiScaled(26), textLineHeight(fontSmall_) + uiScaled(10));
      drawLayoutModePair(SDL_Rect {side.x, side.y, side.w, pillH});
      renderInspectorBody(SDL_Rect {side.x, side.y + pillH + gap, side.w, side.h - pillH - gap});
      return;
    }
    const int labelH = std::max(uiScaled(14), textLineHeight(fontSmall_));
    const int pillH = std::max(uiScaled(26), textLineHeight(fontSmall_) + uiScaled(10));
    drawLayoutModePair(SDL_Rect {side.x, side.y, side.w, pillH});
    const int monY = side.y + pillH + gap;
    const int monH = std::min(side.h * 32 / 100, side.w * 9 / 16 + labelH + uiScaled(8));
    MultiviewTile feed;
    feed.source = "programme";
    drawMonitorTile(SDL_Rect {side.x, monY, side.w, monH}, feed, -1, false, labelH, uiScaled(7));
    const int btnH = std::max(uiScaled(50), textLineHeight(fontBase_) + uiScaled(20));
    SDL_Rect notes {side.x, monY + monH + gap, side.w, std::max(0, side.y + side.h - monY - monH - btnH - gap * 2)};
    drawUIPanel(notes, pal.tile, pal.deep, pal.mid);
    {
      const int pad = uiScaled(12);
      const std::string title = next ? deckboy::core::i18n::format("NOTES  {1}", {std::to_string(nextIndex + 1)}) : std::string("NOTES");
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {notes.x + pad, notes.y + uiScaled(6), notes.w - pad * 2, labelH}, title, pal.fgSoft);
      int y = notes.y + labelH + uiScaled(14);
      const int bottom = notes.y + notes.h - pad;
      if (next && !next->notes.empty()) {
        y = drawWrappedLines(fontBase_, next->notes, notes.x + pad, y, notes.w - pad * 2, bottom, pal.fg);
      } else {
        y = drawWrappedLines(fontSmall_, "no notes on the cue standing by", notes.x + pad, y, notes.w - pad * 2, bottom, pal.inkSoft);
      }
      // coming up: the notes of the cues after it, a line each
      y += uiScaled(10);
      if (y + labelH < bottom) {
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {notes.x + pad, y, notes.w - pad * 2, labelH}, "COMING UP", pal.fgSoft);
        y += labelH + uiScaled(4);
      }
      for (int i = nextIndex + 1; i >= 1 && i < count && y + labelH < bottom; ++i) {
        const Cue& c = deck.cues[static_cast<std::size_t>(i)];
        if (c.notes.empty()) continue;
        const std::string line = std::to_string(i + 1) + "  " + c.notes;
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {notes.x + pad, y, notes.w - pad * 2, labelH},
                     ellipsizeToPixelWidth(fontSmall_, line, notes.w - pad * 2), pal.fg);
        y += labelH + uiScaled(4);
      }
    }
    {
      const int w = (side.w - gap * 2) / 3;
      SDL_Rect back {side.x, side.y + side.h - btnH, w, btnH};
      SDL_Rect pause {side.x + w + gap, back.y, w, btnH};
      SDL_Rect panic {side.x + (w + gap) * 2, back.y, side.w - (w + gap) * 2, btnH};
      drawLayoutButton(back, "BACK", "Up", pal.tile, pal.deep, fontBase_);
      drawLayoutButton(pause, "PAUSE ALL", "", pal.tile, pal.deep, fontBase_);
      drawLayoutButton(panic, "PANIC", "", paletteMix(pal.tile, pal.deleteBezel, 0.4), pal.deleteBezel, fontBase_);
      layoutHit(back, LayoutHitKind::Back);
      layoutHit(pause, LayoutHitKind::PauseAll);
      layoutHit(panic, LayoutHitKind::Panic);
    }
  }

  // ── CONSOLE ───────────────────────────────────────────────────────────────
  //
  // A vision mixer (the board's "Console"): a row of monitors, a PROGRAM bus
  // that hot-cuts and a PRESET bus that picks what AUTO takes, the transition
  // row, and CUT / AUTO / FTB. The layout the rendered 3D skins are for.

  static const std::vector<TransitionStyle>& consoleStyles() {
    static const std::vector<TransitionStyle> styles = {
      TransitionStyle::Crossfade, TransitionStyle::DipBlack, TransitionStyle::WipeLeft,
      TransitionStyle::Iris, TransitionStyle::Portal, TransitionStyle::Shatter};
    return styles;
  }

  // PROGRAM bus: this cue on air now, no transition (a switcher's hot cut).
  void layoutHotCut(int deckIndex, int cueIndex) {
    if (!setFocusedDeckIndex(deckIndex)) return;
    selectCueInDeck(deckIndex, cueIndex, false, false);
    takeSelected(jumpTriggersPlayback(), false);
  }

  void renderConsoleLayout(const SDL_Rect& area) {
    layoutHits_.clear();
    layoutBlackRect_ = {};
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return;
    const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
    const int gap = uiScaled(10);
    const SDL_Color red {220, 50, 45, 255}, green {60, 200, 90, 255};
    const int labelH = std::max(uiScaled(14), textLineHeight(fontSmall_));

    // ── the monitor row: the playlists, then PVW and PGM, wider ──
    const int monH = std::clamp(area.h * 34 / 100, uiScaled(130), uiScaled(420));
    {
      std::vector<MultiviewTile> tiles;
      for (int d = 0; d < static_cast<int>(project_.decks.size()) && tiles.size() < 4; ++d) {
        MultiviewTile t;
        t.source = "deck:" + std::to_string(d);
        tiles.push_back(t);
      }
      const int smallCount = static_cast<int>(tiles.size());
      // two big windows count as 1.6 each
      const double units = smallCount + 3.2;
      const int unitW = static_cast<int>((area.w - gap * (smallCount + 1)) / units);
      int x = area.x;
      for (const auto& t : tiles) {
        drawMonitorTile(SDL_Rect {x, area.y, unitW, monH}, t, multiviewTileDeck(t), false, labelH, uiScaled(7));
        x += unitW + gap;
      }
      const int bigW = (area.x + area.w - x - gap) / 2;
      MultiviewTile pvw;
      pvw.source = "next";
      drawMonitorTile(SDL_Rect {x, area.y, bigW, monH}, pvw, -2, false, labelH, uiScaled(7));
      MultiviewTile pgm;
      pgm.source = "programme";
      drawMonitorTile(SDL_Rect {x + bigW + gap, area.y, area.x + area.w - x - bigW - gap, monH}, pgm, -1, false, labelH, uiScaled(7));
    }

    // ── the panel ──
    SDL_Rect panel {area.x, area.y + monH + gap, area.w, std::max(0, area.h - monH - gap)};
    drawUIPanel(panel, pal.shellInner, pal.deep, pal.mid);
    const int pad = uiScaled(14);
    const int pillH = std::max(uiScaled(26), textLineHeight(fontSmall_) + uiScaled(10));
    drawLayoutModePair(SDL_Rect {panel.x + pad, panel.y + pad / 2, panel.w - pad * 2, pillH});
    if (operatorEditMode_) {
      renderInspectorBody(SDL_Rect {panel.x + pad, panel.y + pillH + pad, panel.w - pad * 2, panel.h - pillH - pad * 2});
      return;
    }
    const int bigW = std::clamp(panel.w * 15 / 100, uiScaled(150), uiScaled(300));
    SDL_Rect right {panel.x + panel.w - pad - bigW, panel.y + pillH + pad, bigW, panel.h - pillH - pad * 2};
    SDL_Rect left {panel.x + pad, panel.y + pillH + pad, std::max(0, right.x - panel.x - pad * 2), panel.h - pillH - pad * 2};
    const int busLabelW = std::max(uiScaled(90), measuredTextWidth(fontSmall_, "PROGRAM") + uiScaled(16));
    const int rowH = std::max(uiScaled(50), (left.h - gap * 3) / 3);
    const int count = static_cast<int>(deck.cues.size());
    const int keys = std::clamp((left.w - busLabelW) / std::max(1, uiScaled(110)), 4, 10);
    const int keyW = (left.w - busLabelW - gap * (keys - 1)) / keys;
    int nextIndex = -1;
    previewCuePtr(deckIndex, &nextIndex);
    const int anchor = std::max(0, deck.activeIndex);
    const int first = std::clamp(anchor - keys / 3, 0, std::max(0, count - keys));
    // the two buses
    for (int bus = 0; bus < 2; ++bus) {
      const int y = left.y + bus * (rowH + gap);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {left.x, y, busLabelW, rowH}, bus == 0 ? "PROGRAM" : "PRESET", pal.fgSoft);
      for (int k = 0; k < keys; ++k) {
        const int i = first + k;
        SDL_Rect key {left.x + busLabelW + k * (keyW + gap), y, keyW, rowH};
        if (i >= count) {
          drawUIPanel(key, paletteMix(pal.tile, pal.deep, 0.5), pal.deep, pal.mid);
          continue;
        }
        const Cue& cue = deck.cues[static_cast<std::size_t>(i)];
        const bool lit = bus == 0 ? i == deck.activeIndex : (i == nextIndex && i != deck.activeIndex);
        const SDL_Color litColour = bus == 0 ? red : green;
        const SDL_Color fill = lit ? litColour : pal.tile;
        if (lit) drawGlowAround(key, litColour, 4, 2);
        drawUIPanel(key, fill, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, SDL_Rect {key.x + uiScaled(4), key.y, key.w - uiScaled(8), key.h},
                             ellipsizeToPixelWidth(fontSmall_, cue.name, key.w - uiScaled(8)), paletteInkOnFill(fill));
        layoutHit(key, bus == 0 ? LayoutHitKind::HotCut : LayoutHitKind::Row, deckIndex, i);
      }
    }
    // the transition row: the style, then how long
    {
      const int y = left.y + 2 * (rowH + gap) + gap;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {left.x, y, busLabelW, rowH}, "NEXT TRANS", pal.fgSoft);
      const auto& styles = consoleStyles();
      const int n = static_cast<int>(styles.size());
      const int durW = std::max(uiScaled(200), keyW * 2);
      const int sw = (left.w - busLabelW - durW - gap * n) / n;
      const ::TransitionStyle current = parseTransitionStyleToken(deck.transitionStyle);
      for (int k = 0; k < n; ++k) {
        SDL_Rect b {left.x + busLabelW + k * (sw + gap), y, sw, rowH};
        const bool on = styles[static_cast<std::size_t>(k)] == current;
        drawLayoutButton(b, toUpper(transitionStyleLabel(transitionStyleToken(styles[static_cast<std::size_t>(k)]))), "", on ? pal.light : pal.tile, pal.deep, fontSmall_);
        layoutHit(b, LayoutHitKind::Style, k);
      }
      SDL_Rect dur {left.x + left.w - durW, y, durW, rowH};
      const int stepW = uiScaled(44);
      SDL_Rect minus {dur.x, y, stepW, rowH}, plus {dur.x + dur.w - stepW, y, stepW, rowH};
      drawLayoutButton(minus, "-", "", pal.tile, pal.deep, fontBase_);
      drawLayoutButton(plus, "+", "", pal.tile, pal.deep, fontBase_);
      layoutHit(minus, LayoutHitKind::Duration, -1);
      layoutHit(plus, LayoutHitKind::Duration, 1);
      TTF_Font* mono = fontLarge_ ? fontLarge_ : fontBase_;
      drawCenteredTextSafe(controlRenderer_, mono, SDL_Rect {minus.x + stepW, y, dur.w - stepW * 2, rowH},
                           deck.transitionSeconds > 0.01 ? formatSeconds(deck.transitionSeconds) : std::string("CUT"), pal.light);
    }
    // CUT / AUTO / FTB
    {
      TTF_Font* big = fontPixelTitle_ ? fontPixelTitle_ : (fontLarge_ ? fontLarge_ : fontBase_);
      const int h = (right.h - gap * 2) / 3;
      SDL_Rect cut {right.x, right.y, right.w, h};
      SDL_Rect autoB {right.x, right.y + h + gap, right.w, h};
      SDL_Rect ftb {right.x, right.y + (h + gap) * 2, right.w, right.h - (h + gap) * 2};
      drawLayoutButton(cut, "CUT", "", red, pal.deep, big);
      drawLayoutButton(autoB, "AUTO", "", pal.light, pal.deep, big);
      const bool dark = masterDimmerTarget_ < 0.5;
      drawLayoutButton(ftb, "FTB", dark ? "black: press for picture" : "", paletteMix(pal.tile, pal.deleteBezel, dark ? 0.7 : 0.35), pal.deleteBezel, big);
      layoutHit(cut, LayoutHitKind::Cut);
      layoutHit(autoB, LayoutHitKind::Go);
      layoutHit(ftb, LayoutHitKind::Ftb);
    }
  }

  // ── COMPACT ───────────────────────────────────────────────────────────────
  //
  // For a laptop screen, 1366x768 (the board's "Compact"): one playlist, the
  // live monitor, a tray with tabs (the inspector, what is next, the outputs),
  // and GO along the bottom where a thumb or a trackpad finds it.

  int compactTab_ = 0;   // 0 inspect, 1 next, 2 outputs

  void renderCompactLayout(const SDL_Rect& area) {
    layoutHits_.clear();
    layoutBlackRect_ = {};
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return;
    const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
    const int gap = uiScaled(8);
    const SDL_Color red {220, 50, 45, 255}, green {60, 200, 90, 255};
    const int labelH = std::max(uiScaled(14), textLineHeight(fontSmall_));

    // ── the GO bar along the bottom ──
    const int barH = std::max(uiScaled(52), textLineHeight(fontLarge_ ? fontLarge_ : fontBase_) + uiScaled(18));
    SDL_Rect bar {area.x, area.y + area.h - barH, area.w, barH};
    {
      const int sideW = std::max(uiScaled(80), measuredTextWidth(fontBase_, "BACK") + uiScaled(30));
      const int blackW = std::max(uiScaled(140), measuredTextWidth(fontSmall_, "HOLD TO BLACK") + uiScaled(24));
      SDL_Rect back {bar.x, bar.y, sideW, bar.h};
      SDL_Rect hold {back.x + back.w + gap, bar.y, sideW, bar.h};
      SDL_Rect black {bar.x + bar.w - blackW, bar.y, blackW, bar.h};
      SDL_Rect go {hold.x + hold.w + gap, bar.y, black.x - hold.x - hold.w - gap * 2, bar.h};
      drawLayoutButton(back, "BACK", "Up", pal.tile, pal.deep, fontBase_);
      drawLayoutButton(hold, "HOLD", "Space", pal.tile, pal.deep, fontBase_);
      const Cue* next = previewCuePtr(deckIndex, nullptr);
      const std::string goLabel = next ? deckboy::core::i18n::format("GO  {1}", {next->name}) : std::string("GO");
      drawLayoutButton(go, ellipsizeToPixelWidth(fontLarge_ ? fontLarge_ : fontBase_, goLabel, go.w - uiScaled(20)), "",
                       pal.light, pal.deep, fontLarge_ ? fontLarge_ : fontBase_);
      const bool dark = masterDimmerTarget_ < 0.5;
      drawLayoutButton(black, dark ? "HOLD FOR PICTURE" : "HOLD TO BLACK", "", paletteMix(pal.tile, pal.deleteBezel, 0.35),
                       pal.deleteBezel, fontSmall_);
      if (layoutBlackHoldStart_ && !layoutBlackHoldFired_) {
        const double held = std::clamp((static_cast<double>(SDL_GetTicks()) - static_cast<double>(layoutBlackHoldStart_)) / kLayoutBlackHoldMs, 0.0, 1.0);
        Primitives::fillRect(controlRenderer_, SDL_Rect {black.x + 2, black.y + black.h - uiScaled(5), static_cast<int>((black.w - 4) * held), uiScaled(3)}, pal.deleteBezel);
      }
      layoutHit(back, LayoutHitKind::Back);
      layoutHit(hold, LayoutHitKind::Hold);
      layoutHit(go, LayoutHitKind::Go);
      layoutHit(black, LayoutHitKind::Black);
      layoutBlackRect_ = black;
    }

    SDL_Rect body {area.x, area.y, area.w, std::max(0, area.h - barH - gap)};
    const int listW = std::clamp(body.w * 30 / 100, uiScaled(240), uiScaled(440));
    SDL_Rect list {body.x, body.y, listW, body.h};
    SDL_Rect right {body.x + listW + gap, body.y, std::max(0, body.w - listW - gap), body.h};

    // ── the running order ──
    drawUIPanel(list, pal.tile, pal.deep, pal.mid);
    {
      const int pillH = std::max(uiScaled(24), textLineHeight(fontSmall_) + uiScaled(8));
      const int headH = pillH + uiScaled(8);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {list.x + uiScaled(8), list.y + uiScaled(4), list.w / 2, pillH},
                   ellipsizeToPixelWidth(fontSmall_, deck.name, list.w / 2), pal.fgSoft);
      drawLayoutModePair(SDL_Rect {list.x + list.w / 2, list.y + uiScaled(4), list.w / 2 - uiScaled(6), pillH});
      const int rowH = std::max(uiScaled(34), textLineHeight(fontBase_) + uiScaled(14));
      const int count = static_cast<int>(deck.cues.size());
      const int rowsFit = std::max(1, (list.h - headH) / rowH);
      int nextIndex = -1;
      previewCuePtr(deckIndex, &nextIndex);
      const int first = std::clamp(deck.activeIndex - 1, 0, std::max(0, count - rowsFit));
      SDL_Rect clip {list.x + 1, list.y + headH, list.w - 2, list.h - headH - 1};
      SDL_SetRenderClipRect(controlRenderer_, &clip);
      for (int i = first; i < count && i < first + rowsFit; ++i) {
        const Cue& cue = deck.cues[static_cast<std::size_t>(i)];
        SDL_Rect r {list.x + 1, list.y + headH + (i - first) * rowH, list.w - 2, rowH};
        const bool isLive = i == deck.activeIndex;
        const bool isNext = i == nextIndex && !isLive;
        const bool done = deck.activeIndex >= 0 && i < deck.activeIndex;
        SDL_Color fill = pal.tile;
        if (isLive) fill = paletteMix(pal.tile, red, 0.30);
        else if (isNext) fill = paletteMix(pal.tile, green, 0.22);
        Primitives::fillRect(controlRenderer_, r, fill);
        Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y + r.h - 1, r.w, 1}, pal.mid);
        if (isLive || isNext) Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y, uiScaled(4), r.h}, isLive ? red : green);
        const SDL_Color ink = detail::paletteLiftToContrast(done ? pal.inkSoft : pal.fg, fill, 4.5);
        const SDL_Color soft = detail::paletteLiftToContrast(pal.fgSoft, fill, 3.0);
        const double len = layoutCueLength(cue);
        const std::string lenText = len > 0.0 ? formatSeconds(len) : std::string("hold");
        const int lenW = measuredTextWidth(fontMono_ ? fontMono_ : fontSmall_, "00:00.0") + uiScaled(10);
        const int numW = uiScaled(34);
        drawTextSafe(controlRenderer_, fontMono_ ? fontMono_ : fontSmall_, SDL_Rect {r.x + uiScaled(10), r.y, numW, r.h},
                     std::to_string(i + 1), soft);
        drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {r.x + uiScaled(10) + numW, r.y, r.w - numW - lenW - uiScaled(20), r.h},
                     ellipsizeToPixelWidth(fontBase_, cue.name, r.w - numW - lenW - uiScaled(20)), ink);
        drawTextSafe(controlRenderer_, fontMono_ ? fontMono_ : fontSmall_, SDL_Rect {r.x + r.w - lenW, r.y, lenW, r.h}, lenText, soft);
        layoutHit(r, LayoutHitKind::Row, deckIndex, i);
      }
      SDL_SetRenderClipRect(controlRenderer_, nullptr);
    }

    // ── the live monitor ──
    // the monitor takes a third: the tray below carries the inspector
    const int monH = std::min(right.h * 34 / 100, right.w * 9 / 16 + labelH + uiScaled(8));
    MultiviewTile feed;
    feed.source = "programme";
    SDL_Rect mon {right.x, right.y, right.w, monH};
    drawMonitorTile(mon, feed, -1, false, labelH, uiScaled(7));
    {
      const double left = layoutLiveRemaining();
      if (left >= 0.0) {
        TTF_Font* big = fontLarge_ ? fontLarge_ : fontBase_;
        const std::string t = "-" + formatSeconds(left);
        const int tw = measuredTextWidth(big, t) + uiScaled(14);
        const int th = textLineHeight(big) + uiScaled(4);
        SDL_Rect tag {mon.x + mon.w - tw - uiScaled(8), mon.y + mon.h - th - labelH - uiScaled(10), tw, th};
        drawUIPanel(tag, pal.deep, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, big, tag, t, SDL_Color {255, 255, 255, 255});
      }
    }

    // ── the tray ──
    SDL_Rect tray {right.x, right.y + monH + gap, right.w, std::max(0, right.h - monH - gap)};
    const int tabH = std::max(uiScaled(30), textLineHeight(fontSmall_) + uiScaled(12));
    {
      const char* tabs[3] = {"INSPECT", "NEXT", "OUTPUTS"};
      int x = tray.x;
      for (int k = 0; k < 3; ++k) {
        const int w = measuredTextWidth(fontSmall_, tabs[k]) + uiScaled(32);
        SDL_Rect t {x, tray.y, w, tabH};
        const bool on = compactTab_ == k;
        drawUIPanel(t, on ? pal.light : pal.tile, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, t, tabs[k], paletteInkOnFill(on ? pal.light : pal.tile));
        layoutHit(t, LayoutHitKind::Tab, k);
        x += w + uiScaled(4);
      }
    }
    SDL_Rect page {tray.x, tray.y + tabH, tray.w, std::max(0, tray.h - tabH)};
    if (compactTab_ == 0) {
      renderInspectorBody(page);
    } else if (compactTab_ == 1) {
      drawUIPanel(page, pal.tile, pal.deep, pal.mid);
      const int pad = uiScaled(10);
      const int monW = std::min(page.w * 45 / 100, (page.h - pad * 2) * 16 / 9);
      MultiviewTile nf;
      nf.source = "next";
      drawMonitorTile(SDL_Rect {page.x + pad, page.y + pad, monW, std::min(page.h - pad * 2, monW * 9 / 16 + labelH)}, nf, -2, false, labelH, uiScaled(7));
      const Cue* next = previewCuePtr(deckIndex, nullptr);
      const int tx = page.x + pad * 2 + monW;
      const int tw = page.x + page.w - tx - pad;
      if (next) {
        int y = page.y + pad;
        drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {tx, y, tw, textLineHeight(fontBase_) + uiScaled(4)},
                     ellipsizeToPixelWidth(fontBase_, next->name, tw), pal.fg);
        y += textLineHeight(fontBase_) + uiScaled(8);
        drawWrappedLines(fontSmall_, next->notes.empty() ? std::string("no notes") : next->notes, tx, y, tw, page.y + page.h - pad,
                         next->notes.empty() ? pal.inkSoft : pal.fg);
      }
    } else {
      drawUIPanel(page, pal.tile, pal.deep, pal.mid);
      const int pad = uiScaled(10);
      const int rowH = std::max(uiScaled(30), textLineHeight(fontBase_) + uiScaled(10));
      int y = page.y + pad;
      for (int o = 0; o < static_cast<int>(project_.outputs.size()) && y + rowH <= page.y + page.h; ++o) {
        const auto& out = project_.outputs[static_cast<std::size_t>(o)];
        const int d = uiScaled(10);
        Primitives::fillRect(controlRenderer_, SDL_Rect {page.x + pad, y + (rowH - d) / 2, d, d}, out.enabled ? red : pal.inkSoft);
        drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {page.x + pad + d + uiScaled(10), y, page.w / 2, rowH}, outputLabel(o), pal.fg);
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {page.x + page.w / 2, y, page.w / 2 - pad, rowH},
                     out.enabled ? std::string("ON") : std::string("off"), out.enabled ? red : pal.fgSoft);
        y += rowH;
      }
      if (project_.outputs.empty()) {
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {page.x + pad, y, page.w - pad * 2, rowH}, "no outputs", pal.inkSoft);
      }
    }
  }
