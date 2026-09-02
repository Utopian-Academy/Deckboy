// ============================================================================
// app_render_control.ipp — Cue list row rendering and transport controls.
//
// Renders individual cue rows in the deck's cue list panel:
//
//   renderDeckPanelCueRow()  — draw one cue row with:
//     - Color chip (cue.color)
//     - Cue ID label (user-assigned or auto-generated)
//     - Cue name (ellipsized to fit available width)
//     - Kind badge (Video, Image, Pattern, etc.)
//     - Duration display
//     - Status indicator (playing, paused, selected, next)
//     - Selection highlight and live/next state colors
//
//   Row colors follow a state hierarchy:
//     Live (playing) → deep background, light text
//     Selected       → mid background
//     Next           → light background
//     Default        → shell inner background
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Render a single cue row in the deck panel's scrollable cue list.
  void renderDeckPanelCueRow(const SDL_Rect& row, int deckIndex, int cueIndex) {
    const Deck& deck = project_.decks[deckIndex];
    const Cue& cue = deck.cues[cueIndex];
    bool isSelected = cueIndexSelected(deck, cueIndex);
    bool isLive     = (cueIndex == deck.activeIndex);
    bool isNext     = !isLive && (cueIndex == nextCueIndexForDeck(deckIndex));

    SDL_Color fill   = isLive     ? pal.deep
                     : isSelected ? pal.mid
                     : isNext     ? pal.light
                                  : pal.shellInner;
    SDL_Color ink    = isLive ? pal.light : pal.deep;
    SDL_Color subInk = isLive ? pal.mid   : pal.dark;

    drawUIPanel(row, fill, pal.deep, pal.mid);

    // Color chip
    SDL_Rect chip {row.x + 2, row.y + 3, 4, row.h - 6};
    SDL_Color chipColor = !cue.colorTag.empty() ? colorTagToSdl(cue.colorTag) : cue.color;
    Primitives::fillRect(controlRenderer_, chip, chipColor);

    // Status glyph
    const char* glyph = isLive ? "\xe2\x97\x8f" : (isSelected ? "\xe2\x96\xb8" : (isNext ? "N" : " "));
    SDL_Rect glyphR {row.x + 8, row.y + (row.h - 18) / 2, 18, 18};
    drawCenteredTextSafe(controlRenderer_, fontSmall_, glyphR, glyph, ink);

    // Cue token
    std::string token = cueDisplayToken(cue, cueIndex);
    SDL_Rect tokenR {row.x + 26, row.y + (row.h - 18) / 2, 44, 18};
    drawTextSafe(controlRenderer_, fontMono_, tokenR, token, subInk);

    // Remaining-time badge geometry is resolved FIRST, because the cue name
    // needs to know how much room it actually loses. The old code hardcoded a
    // 58px badge and reserved 66px from the name; "-00:15.2" is eight
    // monospace glyphs, which fits Consolas on Windows but not the wider
    // Liberation/DejaVu Mono on Linux, where it rendered as "-00...".
    // Measuring keeps one layout correct on every platform, and 58 stays the
    // floor so Windows geometry is unchanged.
    std::string remStr;
    int remBadgeW = 0;
    if (isLive) {
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      if (engine && engine->duration() > 0.0) {
        remStr = "-" + formatSeconds(std::max(0.0, engine->duration() - engine->position()));
        int remTextW = 0, remTextH = 0;
        TTF_GetStringSize(fontMono_, remStr.c_str(), remStr.size(), &remTextW, &remTextH);
        // +12 for the badge fill and drawCenteredTextSafe's inset — measuring
        // exactly and padding thinly is what reproduced the truncation in the
        // timeline chips.
        remBadgeW = std::max(58, remTextW + 12);
        // Never let the badge crowd the name off the row entirely.
        remBadgeW = std::min(remBadgeW, std::max(58, row.w - 72 - 40));
      }
    }

    // Cue name — narrower when live (leaves room for remaining-time badge)
    int nameX = row.x + 72;
    int nameW = remBadgeW > 0 ? std::max(10, row.w - 72 - (remBadgeW + 8))
                              : std::max(10, row.w - 72 - 4);
    std::string name = ellipsizeToPixelWidth(fontSmall_, cue.name, nameW);
    SDL_Rect nameR {nameX, row.y + (row.h - 18) / 2, nameW, 18};
    drawTextSafe(controlRenderer_, fontSmall_, nameR, name, ink);

    if (remBadgeW > 0) {
      SDL_Rect remR {row.x + row.w - (remBadgeW + 4), row.y + 2, remBadgeW, row.h - 4};
      Primitives::fillRect(controlRenderer_, remR, pal.dark);
      drawCenteredTextSafe(controlRenderer_, fontMono_, remR, remStr, pal.light);
    }

    decksPanelCueHits_.push_back({deckIndex, cueIndex, row});
  }

  void renderDeckPanelColumn(const SDL_Rect& col, int deckIndex) {
    const Deck& deck = project_.decks[deckIndex];
    bool focused = (deckIndex == project_.focusedDeckIndex);
    const Cue* activeCue = activeCuePtr(deckIndex);
    const MediaEngine* engine = mediaEngineForDeck(deckIndex);
    TransportState state = engine ? engine->state() : TransportState::Stopped;

    // Column background
    SDL_Color colBg = focused ? pal.light : pal.shellInner;
    drawUIPanel(col, colBg, pal.deep, pal.mid);

    // --- HEADER (52px) ---
    constexpr int kColHdrH = 52;
    SDL_Rect hdr {col.x + 2, col.y + 2, col.w - 4, kColHdrH};
    SDL_Color hdrFill = focused ? pal.mid : pal.shellOuter;
    drawUIPanel(hdr, hdrFill, pal.deep, pal.dark);

    SDL_Color hdrInk = focused ? pal.light : pal.mid;

    // State badge (top-right)
    const char* stateLabel = (state == TransportState::Playing) ? "PLAY"
                           : (state == TransportState::Paused)  ? "PAUSE" : "STOP";
    SDL_Color stateFill = (state == TransportState::Playing) ? SDL_Color{30, 120, 30, 255}
                        : (state == TransportState::Paused)  ? SDL_Color{120, 100, 0, 255}
                                                             : pal.dark;
    SDL_Rect stateBadge {hdr.x + hdr.w - 60, hdr.y + 4, 54, 22};
    Primitives::fillRect(controlRenderer_, stateBadge, stateFill);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, stateBadge, stateLabel,
                         pal.light);

    // Deck name
    std::string deckName = deck.name.empty() ? deckDefaultName(deckIndex) : deck.name;
    drawTextSafe(controlRenderer_, fontBase_,
                 SDL_Rect {hdr.x + 6, hdr.y + 5, hdr.w - 62, 20},
                 deckName, hdrInk);

    // Route info
    auto primaryOut = primaryOutputIndexForDeck(deckIndex);
    int layerIdx    = primaryLayerIndexForDeck(deckIndex);
    std::string layerStr = layerIdx <= 0 ? "BG" : "L" + std::to_string(layerIdx);
    std::string routeStr = primaryOut
      ? ("\xe2\x86\x92 Out " + std::to_string(*primaryOut + 1) + "  " + layerStr)
      : "\xe2\x86\x92 Unrouted";
    SDL_Color routeInk = focused ? pal.mid : pal.dark;
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {hdr.x + 6, hdr.y + 28, hdr.w - 12, 20},
                 routeStr, routeInk);

    int y = hdr.y + hdr.h + 4;

    // --- PROGRESS + TIME (52px) ---
    constexpr int kProgressAreaH = 52;
    SDL_Rect progressArea {col.x + 4, y, col.w - 8, kProgressAreaH};

    double pos = engine ? engine->position() : 0.0;
    double dur = engine ? engine->duration() : 0.0;

    // Bar
    SDL_Rect rail {progressArea.x + 2, progressArea.y + 4, progressArea.w - 4, 10};
    Primitives::drawFramedPanel(controlRenderer_, rail, pal.light,
                                pal.deep, pal.mid);
    if (dur > 0.0) {
      int fillW = static_cast<int>((pos / dur) * (rail.w - 4));
      fillW = std::clamp(fillW, 0, rail.w - 4);
      SDL_Rect barFill {rail.x + 2, rail.y + 2, fillW, rail.h - 4};
      Primitives::fillRect(controlRenderer_, barFill, pal.dark);
    }

    // Current / remaining time
    SDL_Color timeInk = pal.deep;
    drawTextSafe(controlRenderer_, fontMono_,
                 SDL_Rect {progressArea.x + 2, progressArea.y + 18, 72, 16},
                 formatSeconds(pos), timeInk);
    std::string remStr = dur > 0.0
      ? ("-" + formatSeconds(std::max(0.0, dur - pos))) : "--:--";
    drawTextSafe(controlRenderer_, fontMono_,
                 SDL_Rect {progressArea.x + progressArea.w - 74, progressArea.y + 18, 72, 16},
                 remStr, timeInk);

    // Active cue name
    std::string activeToken = activeCue ? cueDisplayToken(*activeCue, deck.activeIndex) : "";
    std::string activeName  = activeCue ? activeCue->name : "---";
    std::string activeStr   = activeToken.empty() ? activeName : (activeToken + "  " + activeName);
    activeStr = ellipsizeToPixelWidth(fontSmall_, activeStr, progressArea.w - 4);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {progressArea.x + 2, progressArea.y + 36, progressArea.w - 4, 14},
                 activeStr, pal.dark);

    y += kProgressAreaH + 4;

    // --- TRANSPORT BUTTONS (44px) ---
    constexpr int kTransportH = 44;
    constexpr int kBtnGap = 6;
    constexpr int kSmallBtnW = 38;
    SDL_Rect transArea {col.x + 4, y, col.w - 8, kTransportH};
    int largeBtnW = std::max(60, transArea.w - 2 * (kSmallBtnW + kBtnGap) - kBtnGap);
    int btnH  = 32;
    int btnY  = transArea.y + (transArea.h - btnH) / 2;
    SDL_Rect takeBtn {transArea.x, btnY, largeBtnW, btnH};
    SDL_Rect stopBtn {takeBtn.x + largeBtnW + kBtnGap, btnY, kSmallBtnW, btnH};
    SDL_Rect playBtn {stopBtn.x + kSmallBtnW + kBtnGap, btnY, kSmallBtnW, btnH};

    // TAKE — icon
    drawUIPanel(takeBtn, pal.dark, pal.deep, pal.mid);
    if (uiBtnTake_.texture) {
      int sz = std::min(20, std::min(takeBtn.w - 8, takeBtn.h - 8));
      SDL_Rect ir {takeBtn.x + (takeBtn.w - sz) / 2, takeBtn.y + (takeBtn.h - sz) / 2, sz, sz};
      drawUiImageContain(uiBtnTake_, ir, 255, pal.light);
    } else {
      drawCenteredTextSafe(controlRenderer_, fontBase_, takeBtn, "TAKE", pal.light);
    }
    decksPanelDeckButtonHits_.push_back({deckIndex, kDecksPanelDeckActionTake, takeBtn});

    // STOP — icon
    drawUIPanel(stopBtn, pal.mid, pal.deep, pal.light);
    if (uiBtnStop_.texture) {
      int sz = std::min(18, std::min(stopBtn.w - 8, stopBtn.h - 8));
      SDL_Rect ir {stopBtn.x + (stopBtn.w - sz) / 2, stopBtn.y + (stopBtn.h - sz) / 2, sz, sz};
      drawUiImageContain(uiBtnStop_, ir, 255, pal.deep);
    } else {
      drawCenteredTextSafe(controlRenderer_, fontSmall_, stopBtn, "\xe2\x96\xa0", pal.deep);
    }
    decksPanelDeckButtonHits_.push_back({deckIndex, kDecksPanelDeckActionStop, stopBtn});

    // PLAY — icon (lit when playing)
    SDL_Color playFill = (state == TransportState::Playing) ? pal.dark : pal.mid;
    SDL_Color playInk = (state == TransportState::Playing) ? pal.light : pal.deep;
    drawUIPanel(playBtn, playFill, pal.deep, pal.light);
    if (uiBtnPlay_.texture) {
      int sz = std::min(18, std::min(playBtn.w - 8, playBtn.h - 8));
      SDL_Rect ir {playBtn.x + (playBtn.w - sz) / 2, playBtn.y + (playBtn.h - sz) / 2, sz, sz};
      drawUiImageContain(uiBtnPlay_, ir, 255, playInk);
    } else {
      drawCenteredTextSafe(controlRenderer_, fontSmall_, playBtn, "\xe2\x96\xb6", pal.deep);
    }
    decksPanelDeckButtonHits_.push_back({deckIndex, kDecksPanelDeckActionPlay, playBtn});

    y += kTransportH + 4;

    // --- CUE LIST (fills remaining height) ---
    constexpr int kFooterH = 36;
    constexpr int kCueRowH = 80;
    constexpr int kCueRowGap = 2;
    int listH = std::max(0, col.y + col.h - y - kFooterH - 8);
    SDL_Rect listRect {col.x + 4, y, col.w - 8, listH};
    drawUIPanel(listRect, pal.tile,
                pal.deep, pal.mid);

    if (deckScrolls_.size() <= static_cast<size_t>(deckIndex))
      deckScrolls_.resize(deckIndex + 1, 0);

    SDL_Rect listClip {listRect.x + 4, listRect.y + 4, listRect.w - 8, listRect.h - 8};
    deckListClipRects_[deckIndex] = listClip;
    SDL_SetRenderClipRect(controlRenderer_, &listClip);

    int cueCount = static_cast<int>(deck.cues.size());
    if (cueCount == 0) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {listClip.x + 4, listClip.y + 8, listClip.w - 8, 16},
                   "(empty — import media)", pal.inkSoft);

    } else {
      int totalCueH = cueCount * (kCueRowH + kCueRowGap) - kCueRowGap;
      int scrollMax = std::max(0, totalCueH - listClip.h);
      // Clamp before drawing so the wheel can never scroll past the last cue
      // into empty space (that over-scroll was easy to get lost in).
      deckScrolls_[deckIndex] = std::clamp(deckScrolls_[deckIndex], 0, scrollMax);
      int rowY = listClip.y - deckScrolls_[deckIndex];
      for (int ci = 0; ci < cueCount; ++ci) {
        if (rowY + kCueRowH >= listClip.y && rowY <= listClip.y + listClip.h) {
          SDL_Rect cueRow {listClip.x, rowY, listClip.w, kCueRowH};
          renderDeckPanelCueRow(cueRow, deckIndex, ci);
        }
        rowY += kCueRowH + kCueRowGap;
      }
    }
    SDL_SetRenderClipRect(controlRenderer_, nullptr);

    y += listH + 4;

    // --- FOOTER (36px) ---
    SDL_Rect footer {col.x + 4, col.y + col.h - kFooterH - 2, col.w - 8, kFooterH};
    drawUIPanel(footer, pal.shellOuter,
                pal.deep, pal.shellInner);

    // Cue end behavior follows the selected/active cue rather than a deck toggle.
    int autoW = (footer.w - 12) / 2;
    SDL_Rect endBtn {footer.x + 4, footer.y + 6, autoW, 24};
    const Cue* footerCue = nullptr;
    if (deck.selectedIndex >= 0 && deck.selectedIndex < cueCount) {
      footerCue = &deck.cues[deck.selectedIndex];
    } else if (deck.activeIndex >= 0 && deck.activeIndex < cueCount) {
      footerCue = &deck.cues[deck.activeIndex];
    }
    CueEndAction footerEndAction = footerCue ? resolvedCueEndAction(*footerCue) : CueEndAction::Inherit;
    SDL_Color endFill = (footerEndAction == CueEndAction::PauseOnLast || footerEndAction == CueEndAction::Loop)
      ? pal.dark
      : pal.light;
    SDL_Color endInk = (footerEndAction == CueEndAction::PauseOnLast || footerEndAction == CueEndAction::Loop)
      ? pal.light
      : pal.deep;
    Primitives::drawFramedPanel(controlRenderer_, endBtn, endFill, pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, endBtn, footerCue ? cueEndStatusLabel(*footerCue) : "END CUE", endInk);
    // Shuffle indicator
    SDL_Rect shuffleBtn {endBtn.x + autoW + 4, footer.y + 6, autoW, 24};
    Primitives::drawFramedPanel(controlRenderer_, shuffleBtn,
                                deck.shuffle ? pal.dark : pal.light,
                                pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, shuffleBtn,
                         deck.shuffle ? "SHUFFLE" : "ORDER",
                         deck.shuffle ? pal.light : pal.deep);
    shuffleBtnRect_ = shuffleBtn;

    SDL_Rect opRail {footer.x + 4, footer.y + footer.h - 14, footer.w - 8, 8};
    Primitives::drawFramedPanel(controlRenderer_, opRail, pal.light,
                                pal.deep, pal.shellOuter);
    int opFillW = static_cast<int>(
      std::lround(std::clamp(deck.playlistOpacity, 0.0f, 1.0f) * (opRail.w - 4)));
    opFillW = std::clamp(opFillW, 0, opRail.w - 4);
    SDL_Rect opFill {opRail.x + 2, opRail.y + 2, opFillW, opRail.h - 4};
    Primitives::fillRect(controlRenderer_, opFill, pal.dark);
    if (deckOpacityFaderRects_.size() <= static_cast<size_t>(deckIndex))
      deckOpacityFaderRects_.resize(deckIndex + 1, SDL_Rect{});
    deckOpacityFaderRects_[deckIndex] = opRail;
  }

  // ---------------------------------------------------------------------------
  // Monitors window — one tile per output, program frame + preview + controls
  // ---------------------------------------------------------------------------

  void renderMonitorsTile(const SDL_Rect& tile, int outputIndex) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) return;
    const OutputTarget& output = project_.outputs[outputIndex];
    bool focused = (outputIndex == project_.focusedOutputIndex);

    // Tile background
    SDL_Color tileBg = focused ? pal.light : pal.shellInner;
    drawUIPanel(tile, tileBg, pal.deep, pal.mid);

    // --- Header strip (28px) ---
    constexpr int kTileHdrH = 28;
    SDL_Rect hdr {tile.x + 2, tile.y + 2, tile.w - 4, kTileHdrH};
    SDL_Color hdrBg = focused ? pal.mid : pal.shellOuter;
    drawUIPanel(hdr, hdrBg, pal.deep, pal.dark);

    // Health badge
    OutputHealthState health = outputHealthStateForDisplay(outputIndex);
    SDL_Color healthFill = (health == OutputHealthState::Live)       ? SDL_Color{30,140,30,255}
                         : (health == OutputHealthState::Error)      ? SDL_Color{160,40,40,255}
                         : (health == OutputHealthState::Recovering) ? SDL_Color{160,120,40,255}
                                                                     : pal.dark;
    SDL_Rect healthBadge {hdr.x + 4, hdr.y + 4, 58, 22};
    Primitives::fillRect(controlRenderer_, healthBadge, healthFill);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, healthBadge,
                         outputHealthLabelToken(health), pal.light);

    // Output name
    std::string outName = "O" + std::to_string(outputIndex + 1) + "  " + outputLabel(outputIndex);
    SDL_Color hdrInk = focused ? pal.light : pal.mid;
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {hdr.x + 60, hdr.y + (hdr.h - 20) / 2, hdr.w - 130, 20},
                 outName, hdrInk);

    // FPS (top-right of header)
    if (outputFpsCounterEnabled_) {
      std::string fpsStr = (output.enabled ? outputFpsLabel(outputIndex) : "--.-") + "fps";
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {hdr.x + hdr.w - 68, hdr.y + (hdr.h - 20) / 2, 64, 20},
                   fpsStr, pal.dark);
    }

    // --- Bottom strip for buttons (30px) ---
    constexpr int kTileBtnH = 30;
    SDL_Rect btnStrip {tile.x + 2, tile.y + tile.h - kTileBtnH - 2, tile.w - 4, kTileBtnH};
    constexpr int kBtnW = 68;
    constexpr int kBtnGap = 6;
    int bx = btnStrip.x + (btnStrip.w - 2 * kBtnW - kBtnGap) / 2;
    int bby = btnStrip.y + (btnStrip.h - 28) / 2;

    // GO / LIVE
    SDL_Color goFill = output.enabled ? SDL_Color{30,140,30,255} : pal.mid;
    SDL_Color goInk  = output.enabled ? pal.light : pal.deep;
    SDL_Rect goBtn {bx, bby, kBtnW, 28};
    drawUIPanel(goBtn, goFill, pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, goBtn, output.enabled ? "LIVE" : "GO", goInk);
    outputMenuButtons_.push_back({goBtn, -1, outputIndex, kOutputMenuActionRecover});

    // OFF
    SDL_Color offFill = output.enabled ? pal.light : pal.dark;
    SDL_Color offInk  = output.enabled ? pal.dark  : pal.mid;
    SDL_Rect offBtn {bx + kBtnW + kBtnGap, bby, kBtnW, 28};
    drawUIPanel(offBtn, offFill, pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, offBtn, "OFF", offInk);
    outputMenuButtons_.push_back({offBtn, -1, outputIndex, kOutputMenuActionDisarm});

    // Register tile click for focus
    outputMenuButtons_.push_back({tile, -1, outputIndex, kOutputMenuActionFocus});

    // --- Program frame area ---
    int programTop    = hdr.y + hdr.h + 2;
    int programBottom = btnStrip.y - 2;
    SDL_Rect programRect {tile.x + 4, programTop, tile.w - 8, std::max(0, programBottom - programTop)};
    drawUIPanel(programRect, pal.deep,
                pal.deep, pal.mid);

    bool hasTex = outputIndex < static_cast<int>(monitorsOutputTextures_.size()) &&
                  monitorsOutputTextures_[outputIndex] != nullptr &&
                  monitorsOutputTexW_[outputIndex] > 0;
    if (hasTex && output.enabled) {
      SDL_Texture* tex = monitorsOutputTextures_[outputIndex];
      int texW = monitorsOutputTexW_[outputIndex];
      int texH = monitorsOutputTexH_[outputIndex];
      // Letterbox scale
      float scaleW = static_cast<float>(programRect.w) / texW;
      float scaleH = static_cast<float>(programRect.h) / texH;
      float scale  = std::min(scaleW, scaleH);
      int dstW = static_cast<int>(texW * scale);
      int dstH = static_cast<int>(texH * scale);
      SDL_Rect dst {programRect.x + (programRect.w - dstW) / 2,
                    programRect.y + (programRect.h - dstH) / 2,
                    dstW, dstH};
      SDL_RenderTexture(controlRenderer_, tex, nullptr, &dst);
    } else {
      // No signal label
      std::string noSigLabel = output.enabled ? "NO SIGNAL" : "OFFLINE";
      drawCenteredTextSafe(controlRenderer_, fontSmall_, programRect,
                           noSigLabel, pal.mid);
    }

    // Next cue info bar (inside program area, bottom-left overlay)
    {
      auto stackEntries = layeredDeckEntriesForOutput(outputIndex);
      if (!stackEntries.empty()) {
        int bgDeck = stackEntries[0].second;
        int nextIdx = nextCueIndexForDeck(bgDeck);
        if (nextIdx >= 0 && nextIdx < static_cast<int>(project_.decks[bgDeck].cues.size())) {
          const Cue& nxt = project_.decks[bgDeck].cues[nextIdx];
          std::string nxtStr = "NEXT  " + cueDisplayToken(nxt, nextIdx) + "  " + nxt.name;
          nxtStr = ellipsizeToPixelWidth(fontSmall_, nxtStr, programRect.w - 8);
          SDL_Rect nxtBar {programRect.x, programRect.y + programRect.h - 18,
                           programRect.w, 18};
          SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 160);
          SDL_RenderFillRect(controlRenderer_, &nxtBar);
          SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {nxtBar.x + 4, nxtBar.y + 2, nxtBar.w - 8, 14},
                       nxtStr, pal.light);
        }
      }
    }
  }

  void renderMonitorsWindow() {
    if (!monitorsWindow_ || !monitorsRenderer_) return;
    if (!monitorsVisible()) return;

    monitorsTileHits_.clear();

    // Swap renderer context
    SDL_Renderer* savedRenderer = controlRenderer_;
    SDL_Window*   savedWindow   = controlWindow_;
    int savedMouseX = mouseX_, savedMouseY = mouseY_;
    controlRenderer_ = monitorsRenderer_;
    controlWindow_   = monitorsWindow_;
    mouseX_ = -10000; mouseY_ = -10000;

    int W = 0, H = 0;
    SDL_GetWindowSize(monitorsWindow_, &W, &H);

    SDL_SetRenderDrawColor(controlRenderer_,
      red(kShellShadowColor), green(kShellShadowColor), blue(kShellShadowColor), 255);
    SDL_RenderClear(controlRenderer_);

    SDL_Rect shell {kLayoutSpacingUnit, kLayoutSpacingUnit,
                    std::max(0, W - kLayoutSpacingUnit * 2),
                    std::max(0, H - kLayoutSpacingUnit * 2)};
    drawUIPanel(shell, pal.shellOuter,
                pal.deep, pal.shellInner);

    // --- Window header ---
    constexpr int kWinHdrH = 44;
    constexpr int kHBtnH   = 32;
    constexpr int kHBtnGap = 6;
    SDL_Rect winHdr {shell.x + 4, shell.y + 4, shell.w - 8, kWinHdrH};
    drawUIPanel(winHdr, pal.shellInner,
                pal.deep, pal.shellOuter);

    int outCount = static_cast<int>(project_.outputs.size());
    std::string title = "MONITORS";
    drawTextSafe(controlRenderer_, fontPixelSmall_ ? fontPixelSmall_ : fontBase_,
                 SDL_Rect {winHdr.x + 10, winHdr.y + (winHdr.h - 22) / 2, 100, 22},
                 title, pal.fg);
    std::string countStr = std::to_string(outCount) + (outCount == 1 ? " output" : " outputs");
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {winHdr.x + 112, winHdr.y + (winHdr.h - 20) / 2, 80, 20},
                 countStr, pal.dark);

    // Right buttons: FPS toggle, + OUTPUT
    int btnY = winHdr.y + (winHdr.h - kHBtnH) / 2;
    SDL_Rect addOutBtn  {winHdr.x + winHdr.w - 80,              btnY, 74, kHBtnH};
    SDL_Rect fpsTogBtn  {addOutBtn.x - 92 - kHBtnGap,           btnY, 86, kHBtnH};

    SDL_Color fpsFill = outputFpsCounterEnabled_ ? pal.dark : pal.shellOuter;
    SDL_Color fpsInk  = outputFpsCounterEnabled_ ? pal.light : pal.mid;
    drawUIPanel(fpsTogBtn, fpsFill, pal.deep, pal.mid);
    drawCenteredText(controlRenderer_, fontSmall_,
                     outputFpsCounterEnabled_ ? "FPS: ON" : "FPS: OFF", fpsInk, fpsTogBtn);
    outputMenuButtons_.push_back({fpsTogBtn, -1, -1, kOutputMenuActionToggleFps});

    drawUIPanel(addOutBtn, pal.shellOuter,
                pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, addOutBtn, "+ OUTPUT",
                         pal.mid);
    outputMenuButtons_.push_back({addOutBtn, -1, -1, kOutputMenuActionAddOutput});

    // --- Tile grid ---
    int gridTop = winHdr.y + winHdr.h + 6;
    int gridH   = shell.y + shell.h - gridTop - 4;
    int gridX   = shell.x + 4;
    int gridW   = shell.w - 8;

    if (outCount == 0) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {gridX + 20, gridTop + 20, gridW - 40, 18},
                   "No outputs — click + OUTPUT to add one.",
                   pal.inkSoft);
    } else {
      // Auto grid: cols = ceil(sqrt(N)), rows = ceil(N / cols)
      int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(outCount)))));
      int rows = (outCount + cols - 1) / cols;
      constexpr int kTileGap = 8;
      int tileW = (gridW - (cols - 1) * kTileGap) / cols;
      int tileH = (gridH - (rows - 1) * kTileGap) / rows;

      for (int oi = 0; oi < outCount; ++oi) {
        int col = oi % cols;
        int row = oi / cols;
        SDL_Rect tileRect {
          gridX + col * (tileW + kTileGap),
          gridTop + row * (tileH + kTileGap),
          tileW, tileH
        };
        renderMonitorsTile(tileRect, oi);
        monitorsTileHits_.push_back({oi, tileRect});
      }
    }

    if (scanlineOverlay_ && pal.scanlineAlpha > 0) {
      int ww, wh;
      SDL_GetCurrentRenderOutputSize(controlRenderer_, &ww, &wh);
      SDL_Rect dst {0, 0, ww, wh};
      SDL_RenderTexture(controlRenderer_, scanlineOverlay_, nullptr, &dst);
    }
    SDL_RenderPresent(controlRenderer_);

    controlRenderer_ = savedRenderer;
    controlWindow_   = savedWindow;
    mouseX_ = savedMouseX;
    mouseY_ = savedMouseY;
  }

  void renderControlWindow() {
    auto uiFrameStart = std::chrono::steady_clock::now();
    int numDecks = static_cast<int>(project_.decks.size());
    deckScrolls_.resize(numDecks, 0);
    deckOverlayScrolls_.resize(numDecks, 0);
    deckColumnRects_.resize(numDecks);
    deckListClipRects_.resize(numDecks);
    deckOverlayClipRects_.resize(numDecks);
    outputMenuButtons_.clear();
    deckSidebarToggleRect_ = SDL_Rect {};
    std::fill(deckColumnRects_.begin(), deckColumnRects_.end(), SDL_Rect {0, 0, 0, 0});
    std::fill(deckListClipRects_.begin(), deckListClipRects_.end(), SDL_Rect {0, 0, 0, 0});
    std::fill(deckOverlayClipRects_.begin(), deckOverlayClipRects_.end(), SDL_Rect {0, 0, 0, 0});

    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);
    layoutButtons(width, height);
    auto uiLayoutDone = std::chrono::steady_clock::now();

    SDL_SetRenderDrawColor(controlRenderer_, red(kShellShadowColor), green(kShellShadowColor), blue(kShellShadowColor), 255);
    SDL_RenderClear(controlRenderer_);

    SDL_Rect shell {kLayoutSpacingUnit, kLayoutSpacingUnit,
                    std::max(0, width - kLayoutSpacingUnit * 2),
                    std::max(0, height - kLayoutSpacingUnit * 2)};
    drawUIPanel(shell,
                pal.shellOuter,
                pal.deep,
                pal.shellInner);

    // ─── Toolbar: full-width button row ──────────────────────────────────────
    constexpr int kToolbarH = 52;
    SDL_Rect toolbar {shell.x + kLayoutPanelBorder * 2,
                      shell.y + kLayoutPanelBorder * 2,
                      shell.w - kLayoutPanelBorder * 4,
                      kToolbarH};
    drawUIPanel(toolbar, pal.shellInner,
                pal.deep, pal.shellOuter);
    {
      TTF_Font* btnFont = fontPixelSmall_ ? fontPixelSmall_ : fontSmall_;
      auto drawTBtn = [&](SDL_Rect& r, const std::string& label,
                          bool lit = false, bool danger = false) {
        SDL_Color fill = danger ? SDL_Color{160,18,18,255}
                       : (lit ? pal.dark : pal.tile);
        SDL_Color ink  = danger ? SDL_Color{255,200,200,255}
                       : (lit ? pal.light : pal.fg);
        drawUIPanel(r, fill, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, btnFont, r, label, ink);
      };

      constexpr int kTBtnH = kToolbarH - 8;
      constexpr int kTBtnGap = 6;
      constexpr int kTGrpGap = 14;
      int ty = toolbar.y + (toolbar.h - kTBtnH) / 2;

      // Place right-anchored buttons first so fader can fill what's left
      int rx = toolbar.x + toolbar.w - 8;
      auto autoW = [&](const char* text, int minW = 60) -> int {
        int tw = 0; TTF_GetStringSize(fontSmall_, text, 0, &tw, nullptr);
        return std::max(minW, tw + 20);
      };
      bool isFullscreen = isAnyOutputFullscreen();
      constexpr int kIconBtnW = 44;

      // ─── Recording indicator ───────────────────────────────────────────
      // On the TOOLBAR, not in the settings modal. An operator running a show
      // is on the deck firing cues -- nobody sits in a settings page during a
      // take, so a record readout there is a readout nobody sees.
      //
      // Only takes space while recording. A permanent "not recording" badge is
      // noise on every other show.
      if (recordingActive()) {
          const int secs = static_cast<int>(recordingElapsedSeconds());
          char t[32];
          std::snprintf(t, sizeof(t), "REC %d:%02d:%02d",
                        secs / 3600, (secs / 60) % 60, secs % 60);
          const int recW = autoW(t, 130);
          SDL_Rect recRect {rx - recW, ty, recW, kTBtnH};
          rx -= recW + kTBtnGap;

          // Pulsing border, same language as blackout: this is a state the
          // operator must not lose track of.
          SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
          const double pulse =
            0.5 + 0.5 * std::sin(static_cast<double>(animationNow_) * 0.006);
          SDL_SetRenderDrawColor(controlRenderer_, 230, 40, 40,
                                 static_cast<Uint8>(80 + 140 * pulse));
          SDL_Rect glow {recRect.x - 2, recRect.y - 2, recRect.w + 4, recRect.h + 4};
          SDL_RenderRect(controlRenderer_, &glow);
          SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

          drawUIPanel(recRect, SDL_Color{150, 20, 20, 255}, pal.deep, pal.light);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, recRect, t,
                               SDL_Color{255, 210, 210, 255});

          // Input level beside it, when a microphone is open. Elapsed time
          // alone cannot tell an operator the recording is capturing anything;
          // a level that moves can, and that is the question actually being
          // asked mid-take.
          if (audioInputRunning()) {
            const int meterW = 90;
            SDL_Rect meter {rx - meterW, ty, meterW, kTBtnH};
            rx -= meterW + kTBtnGap;
            drawUIPanel(meter, pal.dark, pal.deep, pal.mid);
            const int inset = 4;
            const int barMax = meter.w - inset * 2;
            const int barW = std::clamp(
              static_cast<int>(audioInputPeak_ * barMax), 0, barMax);
            if (barW > 0) {
              // Green until it is close to clipping, then red. The number an
              // operator needs at a glance is "is this about to distort".
              const bool hot = audioInputPeak_ > 0.89;
              SDL_Color lvl = hot ? SDL_Color{240, 60, 60, 255}
                                  : SDL_Color{60, 220, 110, 255};
              SDL_SetRenderDrawColor(controlRenderer_, lvl.r, lvl.g, lvl.b, 255);
              SDL_Rect fill {meter.x + inset, meter.y + inset,
                             barW, meter.h - inset * 2};
              SDL_RenderFillRect(controlRenderer_, &fill);
            }
            if (project_.audioInputClipLatch) {
              drawCenteredTextSafe(controlRenderer_, fontSmall_, meter, "CLIP",
                                   SDL_Color{255, 230, 230, 255});
            }
        }
      }

      settingsGearRect_  = SDL_Rect {}; // settings moved to bottom bar
      blackoutBtnRect_   = {rx - kIconBtnW, ty, kIconBtnW, kTBtnH}; rx -= kIconBtnW + kTBtnGap;
      fullscreenBtnRect_ = {rx - kIconBtnW, ty, kIconBtnW, kTBtnH}; rx -= kIconBtnW + kTGrpGap;
      // Blackout — icon only, with pulsing glow when active
      {
        bool blkOn = masterDimmerTarget_ < 0.5;
        SDL_Color fill = blkOn ? SDL_Color{160,18,18,255} : pal.light;
        SDL_Color ink  = blkOn ? SDL_Color{255,200,200,255} : pal.deep;
        // Pulsing red border glow when blackout is on
        if (blkOn) {
          SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
          double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_) * 0.006);
          Uint8 glowA = static_cast<Uint8>(80 + 140 * pulse);
          SDL_SetRenderDrawColor(controlRenderer_, 220, 30, 30, glowA);
          SDL_Rect glow {blackoutBtnRect_.x - 2, blackoutBtnRect_.y - 2,
                         blackoutBtnRect_.w + 4, blackoutBtnRect_.h + 4};
          SDL_RenderRect(controlRenderer_, &glow);
          SDL_Rect glow2 {blackoutBtnRect_.x - 3, blackoutBtnRect_.y - 3,
                          blackoutBtnRect_.w + 6, blackoutBtnRect_.h + 6};
          SDL_SetRenderDrawColor(controlRenderer_, 220, 30, 30, static_cast<Uint8>(glowA / 2));
          SDL_RenderRect(controlRenderer_, &glow2);
          SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
        }
        drawUIPanel(blackoutBtnRect_, fill, pal.deep, pal.mid);
        if (uiBtnBlackout_.texture) {
          int sz = std::min(20, kTBtnH - 12);
          SDL_Rect ir {blackoutBtnRect_.x + (blackoutBtnRect_.w - sz) / 2,
                       blackoutBtnRect_.y + (blackoutBtnRect_.h - sz) / 2, sz, sz};
          drawUiImageContain(uiBtnBlackout_, ir, 255, ink);
        }
      }
      // Fullscreen — icon only
      {
        SDL_Color fill = isFullscreen ? pal.dark : pal.light;
        SDL_Color ink  = isFullscreen ? pal.light : pal.deep;
        drawUIPanel(fullscreenBtnRect_, fill, pal.deep, pal.mid);
        UiImageAsset& fsIcon = isFullscreen ? uiBtnWindow_ : uiBtnFullscreen_;
        if (fsIcon.texture) {
          int sz = std::min(20, kTBtnH - 12);
          SDL_Rect ir {fullscreenBtnRect_.x + (fullscreenBtnRect_.w - sz) / 2,
                       fullscreenBtnRect_.y + (fullscreenBtnRect_.h - sz) / 2, sz, sz};
          drawUiImageContain(fsIcon, ir, 255, ink);
        }
      }

      // Left-anchored buttons
      int ax = toolbar.x + 8;
      fileNewBtnRect_    = {ax, ty, 60, kTBtnH}; ax += 60 + kTBtnGap;
      fileOpenBtnRect_   = {ax, ty, 72, kTBtnH}; ax += 72 + kTBtnGap;
      fileSaveBtnRect_   = {ax, ty, 60, kTBtnH}; ax += 60 + kTGrpGap;
      fileSaveAsBtnRect_ = SDL_Rect {};  // SAVE always prompts, so no separate SAVE AS
      fileBundleBtnRect_ = {ax, ty, 92, kTBtnH}; ax += 92 + kTGrpGap;
      drawTBtn(fileNewBtnRect_,  "NEW");
      drawTBtn(fileOpenBtnRect_, "OPEN");
      drawTBtn(fileSaveBtnRect_, "SAVE");
      drawTBtn(fileBundleBtnRect_, "BUNDLE");

      // RELINK — only exists while media is missing; red so it reads as a
      // warning, not another file action.
      fileRelinkBtnRect_ = SDL_Rect {};
      if (missingMediaCount_ > 0) {
        fileRelinkBtnRect_ = {ax, ty, 104, kTBtnH}; ax += 104 + kTGrpGap;
        drawUIPanel(fileRelinkBtnRect_, SDL_Color {160, 18, 18, 255}, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, fileRelinkBtnRect_,
                             "RELINK " + std::to_string(missingMediaCount_),
                             SDL_Color {255, 210, 210, 255});
      }

      SDL_Rect sep1 {ax, ty + 4, 2, kTBtnH - 8};
      Primitives::fillRect(controlRenderer_, sep1, pal.mid);
      ax += 2 + kTGrpGap;

      // Deck loop and shuffle toggles — icon buttons
      const Deck& focDeck = focusedDeck();
      {
        constexpr int kModeBtnW = 44;
        deckLoopBtnRect_    = {ax, ty, kModeBtnW, kTBtnH}; ax += kModeBtnW + kTBtnGap;
        deckShuffleBtnRect_ = {ax, ty, kModeBtnW, kTBtnH}; ax += kModeBtnW + kTGrpGap;

        auto drawModeBtn = [&](const SDL_Rect& rect, UiImageAsset& icon, bool lit) {
          SDL_Color fill = lit ? pal.dark : pal.light;
          SDL_Color ink  = lit ? pal.light : pal.deep;
          drawUIPanel(rect, fill, pal.deep, pal.mid);
          if (icon.texture) {
            int iconSize = std::min(20, std::min(rect.w - 12, rect.h - 12));
            SDL_Rect iconRect {
              rect.x + (rect.w - iconSize) / 2,
              rect.y + (rect.h - iconSize) / 2,
              iconSize, iconSize
            };
            drawUiImageContain(icon, iconRect, 255, ink);
          }
        };
        UiImageAsset& loopIcon = focDeck.playlistLoop ? uiModeLoopOn_ : uiModeOnce_;
        UiImageAsset& shuffIcon = focDeck.shuffle ? uiModeShuffleOn_ : uiModeOrder_;
        drawModeBtn(deckLoopBtnRect_, loopIcon, focDeck.playlistLoop);
        drawModeBtn(deckShuffleBtnRect_, shuffIcon, focDeck.shuffle);
      }

      SDL_Rect sep2 {ax, ty + 4, 2, kTBtnH - 8};
      Primitives::fillRect(controlRenderer_, sep2, pal.mid);
      ax += 2 + kTGrpGap;

      // Volume fader fills remaining space
      int faderAreaW = std::max(80, rx - ax - kTGrpGap);
      {
        int volPct = static_cast<int>(std::round(project_.masterVolume * 100.0));
        std::string volText = (faderAreaW > 160)
          ? "VOLUME " + std::to_string(volPct) + "%"
          : std::to_string(volPct) + "%";
        int volTextW = 0;
        TTF_GetStringSize(fontSmall_, volText.c_str(), 0, &volTextW, nullptr);
        int kVolLblW = std::min(volTextW + 24, faderAreaW - 44);
        SDL_Rect volLbl {ax, ty, kVolLblW, kTBtnH};
        drawUIPanel(volLbl, pal.mid, pal.deep, pal.light);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, volLbl, volText, pal.deep);
        int trackX = ax + kVolLblW + 4;
        int trackW = std::max(40, faderAreaW - kVolLblW - 4);
        masterFaderRect_ = {trackX, ty + kTBtnH/2 - 7, trackW, 14};
        SDL_Rect track = masterFaderRect_;
        Primitives::fillRect(controlRenderer_, track, pal.deep);
        int fillW = static_cast<int>(std::clamp(project_.masterVolume, 0.0, 2.0) / 2.0 * track.w);
        SDL_Rect fillR {track.x, track.y, fillW, track.h};
        SDL_Color fCol = project_.masterVolume > 1.0 ? SDL_Color{180,80,20,255} : pal.dark;
        Primitives::fillRect(controlRenderer_, fillR, fCol);
        Primitives::strokeRect(controlRenderer_, track, pal.mid);
        // Grabber thumb at current position
        int thumbX = track.x + fillW;
        int thumbW = 6;
        int thumbH = track.h + 6;
        SDL_Rect thumb {thumbX - thumbW / 2, track.y - 3, thumbW, thumbH};
        Primitives::fillRect(controlRenderer_, thumb, pal.light);
        Primitives::strokeRect(controlRenderer_, thumb, pal.mid);
      }

      // Shuffle sparkle — fast twinkling when shuffle is on
      if (focDeck.shuffle) {
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
        double phase = static_cast<double>(animationNow_) * 0.004;
        int sx = deckShuffleBtnRect_.x + deckShuffleBtnRect_.w + 4;
        int sy = deckShuffleBtnRect_.y + deckShuffleBtnRect_.h / 2;
        SDL_Color sc = pal.deep;
        sc.a = static_cast<Uint8>(160 + 95 * std::abs(std::sin(phase * 0.8)));
        drawStar(controlRenderer_, sx, sy, 3, sc);
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
      }
    }

    deckSidebarToggleRect_ = SDL_Rect {};

    // Content area: below toolbar
    int contentY = toolbar.y + toolbar.h + kLayoutPanelGap;
    int controlsTop = bottomBarRect_.w > 0 ? bottomBarRect_.y : (height - kLayoutBottomBarHeight - kLayoutPanelPadding);
    int contentBottom = controlsTop - kLayoutPanelGap;
    int contentH = std::max(120, contentBottom - contentY);
    int contentLeft  = shell.x + kLayoutPanelBorder * 2;
    int contentRight = shell.x + shell.w - kLayoutPanelBorder * 2;
    int contentW = std::max(0, contentRight - contentLeft);

    // Layout: playlist column (left) | program + inspector (right), with draggable splitters.
    SDL_Rect contentArea {contentLeft, contentY, contentW, contentH};
    playlistSplitterRect_ = {};
    inspectorSplitterRect_ = {};
    constexpr int kPlaylistMinW = 236;
    int playlistMaxW = std::max(kPlaylistMinW, contentW - 520);
    int playlistW = playlistPaneWidth_ > 0
      ? std::clamp(playlistPaneWidth_, kPlaylistMinW, playlistMaxW)
      : std::clamp(contentW / 5, kPlaylistMinW, 336);
    // VJ MODE SHOWS BOTH DECKS AT ONCE, side by side, so the column takes the
    // width for two. A mixer where you can only see the playlist you are
    // fading away from is not a mixer -- and those two lists are exactly what
    // the crossfader is choosing between, so they belong next to each other.
    const bool vjSplitDecks = project_.vjModeEnabled && project_.decks.size() > 1;
    if (vjSplitDecks) {
      playlistW = std::clamp(playlistW * 2 + kLayoutPanelGap, kPlaylistMinW * 2,
                             std::max(kPlaylistMinW * 2, contentW - 460));
    }
    SDL_Rect playlistCol {contentArea.x, contentArea.y, playlistW, contentArea.h};
    SDL_Rect mainPanel {contentArea.x + playlistW + kLayoutPanelGap, contentArea.y,
                        std::max(0, contentArea.w - playlistW - kLayoutPanelGap), contentArea.h};
    contentAreaRect_ = contentArea;
    mainPanelLayoutRect_ = mainPanel;
    playlistSplitterRect_ = {playlistCol.x + playlistCol.w, contentArea.y, kLayoutPanelGap, contentArea.h};
    cueRowActionHits_.clear();

    if (vjSplitDecks) {
      // A on the left and B on the right, matching the bar above and the way
      // a hand moves across a fader.
      const int deckCount = static_cast<int>(project_.decks.size());
      const int deckA = std::clamp(project_.vjDeckA, 0, deckCount - 1);
      const int deckB = std::clamp(project_.vjDeckB, 0, deckCount - 1);
      const int halfW = (playlistCol.w - kLayoutPanelGap) / 2;
      SDL_Rect colA {playlistCol.x, playlistCol.y, halfW, playlistCol.h};
      SDL_Rect colB {playlistCol.x + halfW + kLayoutPanelGap, playlistCol.y,
                     playlistCol.w - halfW - kLayoutPanelGap, playlistCol.h};
      renderPlaylistColumn(colA, deckA);
      if (deckB != deckA) {
        renderPlaylistColumn(colB, deckB);
      }
    } else {
      renderPlaylistColumn(playlistCol, 0);
    }
    if (mainPanel.w > 60 && mainPanel.h > 60) {
      renderMainPanel(mainPanel);
    } else {
      mainPanelLayoutRect_ = {};
    }
    if (playlistSplitterRect_.w > 0 && playlistSplitterRect_.h > 0) {
      bool active = layoutDragMode_ == LayoutDragMode::Playlist;
      // Touch mode skips the hover highlight — a tap can't hover, and the
      // sticky highlight after a drag is more distracting than helpful.
      bool hover = !inTouchMode() && pointInRect(mouseX_, mouseY_, playlistSplitterRect_);
      SDL_Rect rail {playlistSplitterRect_.x + playlistSplitterRect_.w / 2 - 1,
                     playlistSplitterRect_.y + 12, 2, std::max(0, playlistSplitterRect_.h - 24)};
      SDL_Color railColor = active ? pal.light
                                   : (hover ? pal.dark : pal.mid);
      Primitives::fillRect(controlRenderer_, rail, railColor);
      SDL_Rect grip {playlistSplitterRect_.x + 1,
                     playlistSplitterRect_.y + playlistSplitterRect_.h / 2 - 18,
                     std::max(0, playlistSplitterRect_.w - 2), 36};
      drawUIPanel(grip,
                  active ? pal.dark : pal.shellInner,
                  pal.deep,
                  hover || active ? pal.light : pal.mid);
      for (int dot = 0; dot < 3; ++dot) {
        SDL_Rect pip {grip.x + grip.w / 2 - 1, grip.y + 9 + dot * 8, 2, 2};
        Primitives::fillRect(controlRenderer_, pip,
                             active ? pal.light : pal.dark);
      }
    }

    renderButtons();
    // The theme's creatures, on the chrome between the panels.
    //
    // AFTER the panels, so they are not painted over, and BEFORE every popup,
    // toast and modal, so nothing they do can obscure something the operator
    // needs to read. Their habitat is the strip of shell along the bottom,
    // which carries no information at all -- and they steer toward the program
    // monitor, because a moth that ignored the brightest thing in the room
    // would not be a moth.
    // The empty part of the playlist, when there is enough of it. That space
    // is inside a panel but contains NOTHING -- no control, no value, no cue
    // -- and it is the part of the window an operator looks at between cues.
    // The first attempt used the strip above the bottom bar, which turned out
    // to overlap the transport row: a firefly ended up sitting on a button,
    // which is exactly what these are not allowed to do.
    //
    // Below about 70px there is not enough room to move, so they stay away
    // rather than jitter in a slot.
    if (playlistFreeRect_.h >= 70 && playlistFreeRect_.w >= 80) {
      creatureHabitat_ = deckboy::creatures::Habitat {
        playlistFreeRect_.x, playlistFreeRect_.y,
        playlistFreeRect_.w, playlistFreeRect_.h, true};
    } else {
      creatureHabitat_ = deckboy::creatures::Habitat {};
    }
    // Repopulate once there is somewhere to live -- which may be several
    // frames after the theme loaded, because the habitat is measured from a
    // laid-out playlist.
    {
      std::size_t wanted = 0;
      for (const auto& r : themeCreatures_) wanted += r.count;
      if (creatures_.size() != wanted) rebuildCreatures();
    }
    creatureLureX_ = programAreaRect_.w > 0
      ? programAreaRect_.x + programAreaRect_.w * 0.5 : width * 0.5;
    creatureLureY_ = programAreaRect_.h > 0
      ? programAreaRect_.y + programAreaRect_.h * 0.5 : height * 0.4;
    updateCreatures(static_cast<double>(animationNow_) / 1000.0);
    renderCreatures();
    renderSlideRenderCard(width, height);
    renderToast(width);
    if (confirmQuit_) {
      renderQuitConfirm();
    }
    if (showStartupDialog_ && !showSplashOverlay_) {
      renderStartupDialog();
    }
    // Dependency prompts sit above the settings modal so the operator sees
    // them right where the toggle they just clicked lives.
    renderDependencyPrompt();
    // Popups rendered last (on top)
    renderContextMenu();
    renderSettingsModal();
    renderDropdownPopover();
    renderShortcutsOverlay();
    renderInlineTextEditor();
    // Last, and over everything: it is modal.
    renderCodeEditor();
    renderSplashOverlay();
    if (scanlineOverlay_ && pal.scanlineAlpha > 0) {
      int ww, wh;
      SDL_GetCurrentRenderOutputSize(controlRenderer_, &ww, &wh);
      SDL_Rect dst {0, 0, ww, wh};
      SDL_RenderTexture(controlRenderer_, scanlineOverlay_, nullptr, &dst);
    }
    // VJ MODE FRAMES THE WHOLE WINDOW.
    //
    // The bar alone is visible, but an operator glancing at a rack from the
    // other side of a room needs to know which mode this machine is in before
    // they touch it -- VJ mode puts a second deck live and makes a fader, not
    // a take, decide what the audience sees. Running a normal show in it by
    // accident is the failure worth designing against, so the entire window
    // is edged in a colour that appears nowhere else.
    //
    // Drawn BEFORE the present, which is the whole of why it never appeared:
    // it sat after SDL_RenderPresent, painting onto a back buffer nothing ever
    // showed and the next frame cleared. It rendered perfectly, every frame,
    // into nothing.
    //
    // It breathes on the beat: alive enough to catch the eye, slow enough to
    // ignore while working, and it doubles as a tempo readout you can see
    // without looking directly at it.
    if (project_.vjModeEnabled) {
      int winW = 0, winH = 0;
      SDL_GetCurrentRenderOutputSize(controlRenderer_, &winW, &winH);
      const double pulse = std::pow(1.0 - vjBeatPhase(), 3.0);
      const int thickness = 3 + static_cast<int>(std::lround(pulse * 2.0));
      SDL_Color edge {255, 176, 32,
                      static_cast<Uint8>(170 + static_cast<int>(pulse * 85))};
      for (int t = 0; t < thickness; ++t) {
        SDL_Rect ring {t, t, winW - t * 2, winH - t * 2};
        Primitives::strokeRect(controlRenderer_, ring, edge);
      }
    }
    SDL_RenderPresent(controlRenderer_);
    revealControlWindow();  // the main control-window frame
    auto uiFrameEnd = std::chrono::steady_clock::now();
    lastUiLayoutMs_ = std::chrono::duration<double, std::milli>(uiLayoutDone - uiFrameStart).count();
    lastUiRenderMs_ = std::chrono::duration<double, std::milli>(uiFrameEnd - uiLayoutDone).count();
  }

  // What a playlist column calls itself. In VJ mode two of them are on screen
  // and both saying "PLAYLIST" is the ambiguity that puts the wrong clip in
  // front of an audience, so each says which side of the crossfader it is.
  std::string playlistColumnTitle(int deckIndex) const {
    if (!project_.vjModeEnabled || project_.decks.size() < 2) {
      return "PLAYLIST";
    }
    const int deckCount = static_cast<int>(project_.decks.size());
    if (deckIndex == std::clamp(project_.vjDeckA, 0, deckCount - 1)) {
      return "A - DECK " + std::to_string(deckIndex + 1);
    }
    if (deckIndex == std::clamp(project_.vjDeckB, 0, deckCount - 1)) {
      return "B - DECK " + std::to_string(deckIndex + 1);
    }
    return "PLAYLIST";
  }

  void renderPlaylistColumn(const SDL_Rect& col, int deckIndex) {
    const Deck& deck = project_.decks[deckIndex];
    if (deckOpacityFaderRects_.size() < project_.decks.size()) {
      deckOpacityFaderRects_.resize(project_.decks.size(), SDL_Rect {});
    }
    if (deckIndex >= 0 && deckIndex < static_cast<int>(deckColumnRects_.size())) {
      deckColumnRects_[deckIndex] = col;
    }

    // --- Simple playlist header ---
    constexpr int kPlaylistHeaderH = 28;
    SDL_Rect colHeader {col.x, col.y, col.w, kPlaylistHeaderH};
    drawUIPanel(colHeader, pal.dark,
                pal.deep, pal.mid);
    drawPanelHeaderTitle(colHeader, playlistColumnTitle(deckIndex));
    // "Jump to live cue" button — snaps the (possibly huge) list back to the
    // cue that's playing. Only for the focused deck's column (the one the
    // keyboard acts on). Sized to its label so the text never ellipsizes
    // into a mystery ">..." chip.
    bool jumpBtnShown = deckIndex == project_.focusedDeckIndex && !deck.cues.empty();
    int jumpBtnW = 0;
    if (jumpBtnShown) {
      TTF_Font* jbFont = fontPixelSmall_ ? fontPixelSmall_ : fontSmall_;
      int txtW = 0;
      int txtH = 0;
      if (jbFont) {
        TTF_GetStringSize(jbFont, ">LIVE", 0, &txtW, &txtH);
      }
      jumpBtnW = std::max(56, txtW + 16);
      SDL_Rect jb {colHeader.x + colHeader.w - jumpBtnW - 6, colHeader.y + 3,
                   jumpBtnW, colHeader.h - 6};
      drawUIPanel(jb, pal.light, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, jbFont, jb, ">LIVE", pal.deep);
      playlistJumpBtnRect_ = jb;
    } else if (deckIndex == project_.focusedDeckIndex) {
      playlistJumpBtnRect_ = SDL_Rect {};
    }

    // Playlist header — scrolling dot animation (like a marquee). The track
    // stops short of the >LIVE button so the dots never crawl across it.
    {
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      int dotRightEdge = colHeader.x + colHeader.w - 12
                       - (jumpBtnShown ? jumpBtnW + 10 : 0);
      int dotTrack = dotRightEdge - (colHeader.x + 80);
      if (dotTrack > 20) {
        for (int d = 0; d < 3; ++d) {
          double phase = static_cast<double>(animationNow_) * 0.001 + d * 0.33;
          double frac = std::fmod(phase, 1.0);
          int dx = dotRightEdge - static_cast<int>(frac * dotTrack);
          int dy = colHeader.y + colHeader.h / 2;
          SDL_Color dotC = pal.mid;
          dotC.a = static_cast<Uint8>(80 + 175 * (1.0 - frac));
          SDL_Rect dot {dx - 1, dy - 1, 2, 2};
          Primitives::fillRect(controlRenderer_, dot, dotC);
        }
      }
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    }

    auto primaryIndices = cueIndicesForOverlayRole(deck, false);
    auto overlayIndices = cueIndicesForOverlayRole(deck, true);
    bool showOverlayBin = !overlayIndices.empty();
    if (deckOverlayScrolls_.size() <= static_cast<size_t>(deckIndex)) {
      deckOverlayScrolls_.resize(deckIndex + 1, 0);
    }

    // Cue list area
    int listAreaY = col.y + kPlaylistHeaderH + 4;
    int listAreaH = col.h - kPlaylistHeaderH - 4 - kColFooterH - 4;
    int overlayPanelH = 0;
    if (showOverlayBin) {
      overlayPanelH = 112;
      overlayPanelH = std::clamp(44 + static_cast<int>(overlayIndices.size()) * (kRowHeight + 8), 120, 232);
      overlayPanelH = std::min(overlayPanelH, std::max(96, listAreaH / 2));
    }
    int primaryPanelH = std::max(120, listAreaH - overlayPanelH - 8);
    if (showOverlayBin && primaryPanelH + overlayPanelH + 8 > listAreaH) {
      overlayPanelH = std::max(96, listAreaH - primaryPanelH - 8);
    }
    if (!showOverlayBin) {
      primaryPanelH = listAreaH;
    }

    SDL_Rect primaryFrame {col.x + 4, listAreaY, col.w - 8, std::max(0, primaryPanelH)};
    SDL_Rect overlayFrame {0, 0, 0, 0};
    if (showOverlayBin) {
      overlayFrame = {col.x + 4, primaryFrame.y + primaryFrame.h + 8, col.w - 8,
                      std::max(0, listAreaH - primaryFrame.h - 8)};
    }
    deckListClipRects_[deckIndex] = primaryFrame;
    deckOverlayClipRects_[deckIndex] = overlayFrame;

    drawUIPanel(primaryFrame, pal.tile, pal.deep, pal.mid);
    SDL_Rect primaryClip {primaryFrame.x + 8, primaryFrame.y + 8, primaryFrame.w - 16, std::max(0, primaryFrame.h - 16)};
    SDL_SetRenderClipRect(controlRenderer_, &primaryClip);
    int primaryTotalH = static_cast<int>(primaryIndices.size()) * (kRowHeight + 8) - 8;
    int primaryScrollMax = std::max(0, primaryTotalH - primaryClip.h);
    if (static_cast<int>(deckScrollMax_.size()) <= deckIndex) {
      deckScrollMax_.resize(deckIndex + 1, 0);
    }
    if (static_cast<int>(deckScrollSettleMs_.size()) <= deckIndex) {
      deckScrollSettleMs_.resize(deckIndex + 1, 0);
    }
    deckScrollMax_[deckIndex] = primaryScrollMax;
    // Rubber-band at the BOTTOM only: the wheel may push a little past the last
    // cue (capped), then this eases it back once the wheel is idle. Time-based
    // decay so the spring is visible regardless of the high render rate.
    Uint64 nowMs = SDL_GetTicks();
    Uint64 lastMs = deckScrollSettleMs_[deckIndex];
    deckScrollSettleMs_[deckIndex] = nowMs;
    if (deckScrolls_[deckIndex] > primaryScrollMax && nowMs - lastDeckScrollMs_ > 90) {
      double dt = (lastMs == 0) ? 16.0 : std::min(100.0, static_cast<double>(nowMs - lastMs));
      double over = static_cast<double>(deckScrolls_[deckIndex] - primaryScrollMax);
      int next = primaryScrollMax + static_cast<int>(std::lround(over * std::exp(-dt / 71.0)));
      deckScrolls_[deckIndex] = (next <= primaryScrollMax + 1) ? primaryScrollMax : next;
    }
    deckScrolls_[deckIndex] = std::clamp(deckScrolls_[deckIndex], 0,
                                         primaryScrollMax + kDeckScrollOverscroll);
    // What is left of the list below the last cue.
    //
    // This is where the theme's creatures live: genuinely empty, containing
    // no control and no value, and the part of the window an operator's eye
    // rests on between cues. It shrinks as the playlist grows, and when there
    // is nothing left they simply have nowhere to be.
    if (deckIndex == project_.focusedDeckIndex) {
      const int used = std::max(0, primaryTotalH - deckScrolls_[deckIndex]);
      playlistFreeRect_ = SDL_Rect {
        primaryClip.x, primaryClip.y + used + 8,
        primaryClip.w, std::max(0, primaryClip.h - used - 12)};
    }
    int y = primaryClip.y - deckScrolls_[deckIndex];
    for (int cueIndex : primaryIndices) {
      SDL_Rect row {primaryClip.x, y, primaryClip.w, kRowHeight};
      renderCueRow(row, deckIndex, cueIndex);
      y += kRowHeight + 8;
    }
    if (primaryIndices.empty()) {
      const int emptyLineH = textLineHeight(fontSmall_) + 4;
      int hx = primaryClip.x + primaryClip.w / 2 - 30;
      int hy = primaryClip.y + primaryClip.h / 2 - emptyLineH;
      drawText(controlRenderer_, fontSmall_, "I  import", pal.fg, hx, hy);
      drawText(controlRenderer_, fontSmall_, "B  browser", pal.fg, hx, hy + emptyLineH);
      drawText(controlRenderer_, fontSmall_, "P  pattern", pal.fg, hx, hy + emptyLineH * 2);
    }
    SDL_SetRenderClipRect(controlRenderer_, nullptr);

    if (showOverlayBin) {
      drawUIPanel(overlayFrame, pal.shellInner, pal.deep, pal.mid);
      SDL_Rect overlayHeader {overlayFrame.x + 6, overlayFrame.y + 6, overlayFrame.w - 12, 22};
      drawTextSafe(controlRenderer_, fontSmall_, overlayHeader, "OVERLAY BIN", pal.light);
      SDL_Rect overlayHint {overlayFrame.x + 8, overlayFrame.y + 24, overlayFrame.w - 16, 14};
      drawTextSafe(controlRenderer_, fontSmall_, overlayHint,
                   "Lower Third and PIP fire independently",
                   pal.mid);
      SDL_Rect overlayClip {overlayFrame.x + 8, overlayFrame.y + 42, overlayFrame.w - 16, std::max(0, overlayFrame.h - 50)};
      SDL_SetRenderClipRect(controlRenderer_, &overlayClip);
      int totalOverlayH = static_cast<int>(overlayIndices.size()) * (kRowHeight + 8) - 8;
      int overlayScrollMax = std::max(0, totalOverlayH - overlayClip.h);
      deckOverlayScrolls_[deckIndex] = std::clamp(deckOverlayScrolls_[deckIndex], 0, overlayScrollMax);
      int overlayY = overlayClip.y - deckOverlayScrolls_[deckIndex];
      for (int cueIndex : overlayIndices) {
        SDL_Rect row {overlayClip.x, overlayY, overlayClip.w, kRowHeight};
        renderCueRow(row, deckIndex, cueIndex);
        overlayY += kRowHeight + 8;
      }
      SDL_SetRenderClipRect(controlRenderer_, nullptr);
    } else {
      deckOverlayScrolls_[deckIndex] = 0;
    }

    // Column footer (playlist settings)
    int footerY = col.y + col.h - kColFooterH;
    SDL_Rect footer {col.x, footerY, col.w, kColFooterH};
    drawUIPanel(footer, pal.shellInner, pal.deep, pal.shellOuter);
    
    std::string playlistInfo = std::string(deck.playlistLoop ? "LOOP" : "ONCE")
      + std::string("  |  ") + (deck.shuffle ? "SHUFFLE" : "ORDER");
    // Height derived from the live font, not a literal 24 — scaled/HiDPI
    // faces are taller and the hardcoded rect clipped the descenders.
    drawTextSafe(controlRenderer_, fontSmall_,
                 {footer.x + 6, footer.y + 6, footer.w - 12, textLineHeight(fontSmall_)},
                 playlistInfo, pal.dark);
    // Deck LAYER fader — the compositing opacity for stacking multiple
    // decks on one output (the multi-deck "Super Deckboy" model). It is not
    // a per-cue control, so in a single-deck show it's meaningless clutter:
    // hide it entirely and let multi-deck shows get it back, labeled.
    if (project_.decks.size() > 1) {
      int opacityPct = static_cast<int>(std::lround(std::clamp(deck.playlistOpacity, 0.0f, 1.0f) * 100.0f));
      std::string opacityLabel = "LAYER " + std::to_string(opacityPct) + "%";
      int labelW = 0, labelH = 0;
      if (fontSmall_) TTF_GetStringSize(fontSmall_, opacityLabel.c_str(), 0, &labelW, &labelH);
      // Same row as LOOP|ORDER (the 50px footer only fits one text line),
      // right-aligned in fg ink — reads on the shellInner footer fill in
      // light themes (fg = deep) and on black in terminal themes.
      drawTextSafe(controlRenderer_, fontSmall_,
                   {footer.x + footer.w - labelW - 10, footer.y + 6,
                    labelW + 4, textLineHeight(fontSmall_)},
                   opacityLabel, pal.fg);
      SDL_Rect opacityRail {col.x + 8, footerY + kColFooterH - 12, col.w - 16, 8};
      Primitives::drawFramedPanel(controlRenderer_, opacityRail, pal.light,
                      pal.deep, pal.shellOuter);
      int fillW = static_cast<int>(std::lround(std::clamp(deck.playlistOpacity, 0.0f, 1.0f) * (opacityRail.w - 4)));
      fillW = std::clamp(fillW, 0, opacityRail.w - 4);
      SDL_Rect opacityFill {opacityRail.x + 2, opacityRail.y + 2, fillW, opacityRail.h - 4};
      Primitives::fillRect(controlRenderer_, opacityFill, pal.dark);
      deckOpacityFaderRects_[deckIndex] = opacityRail;
    } else {
      deckOpacityFaderRects_[deckIndex] = SDL_Rect {};  // no invisible click target
    }
  }

  void renderCueRow(const SDL_Rect& row, int deckIndex, int index) {
    if (row.y + row.h < 0 || row.y > 2000) {
      return;
    }

    const Deck& deck = project_.decks[deckIndex];
    const auto& cue = deck.cues[index];
    bool isOverlay = std::any_of(deck.overlayActiveIndices.begin(), deck.overlayActiveIndices.end(),
                                  [&](int i) { return i == index; });
    bool isSelected = cueIndexSelected(deck, index);
    bool isLive = index == deck.activeIndex;
    bool isQueued = !isLive && index == nextCueIndexForDeck(deckIndex);
    
    // Idle/queued rows use the tile fill (bright in light themes, near-black in
    // terminal themes); selected stays a bright inverse-video highlight, live
    // stays deep. Text ink follows: dark on the bright selected/light-theme
    // tiles, bright (fg) on dark terminal tiles.
    SDL_Color fill = pal.tile;
    SDL_Color border = pal.deep;
    SDL_Color accent = pal.shellInner;

    if (isLive) {
      fill = pal.deep;
      border = pal.mid;
      accent = pal.dark;
    } else if (isSelected) {
      fill = pal.mid;
    } else if (isQueued) {
      fill = pal.tile;
      border = pal.dark;
    } else if (isOverlay) {
      fill = {48, 80, 48, 255};
    }

    drawUIPanel(row, fill, border, accent);

    // Color tag chip
    SDL_Rect chip {row.x + 4, row.y + 4, 6, row.h - 8};
    SDL_Color chipColor = !cue.colorTag.empty() ? colorTagToSdl(cue.colorTag) : cue.color;
    Primitives::fillRect(controlRenderer_, chip, chipColor);

    // Bright fill (selected highlight, light-theme rows) wants dark ink; dark
    // fill (terminal idle rows) wants bright fg ink. isSelected keeps the
    // dark-on-bright highlight; idle/queued follow fg (= deep in light themes).
    SDL_Color ink = isLive ? pal.light : (isSelected ? pal.deep : pal.fg);
    // Idle-row subtext uses the secondary-ink role (reads on both bright
    // light-theme rows and dark terminal tiles); selected keeps dark subink.
    SDL_Color subInk = isLive ? pal.mid : (isSelected ? pal.dark : pal.fgSoft);

    // Indicator area (vertically centered in row)
    int indSize = 36;
    SDL_Rect indicatorRect {row.x + 10, row.y + (row.h - indSize) / 2, indSize, indSize};
    if (isLive) {
      drawUIPanel(indicatorRect, pal.dark, pal.light, pal.mid);
      if (uiBtnPlay_.texture) {
        int sz = std::min(22, std::min(indicatorRect.w - 8, indicatorRect.h - 8));
        SDL_Rect ir {indicatorRect.x + (indicatorRect.w - sz) / 2, indicatorRect.y + (indicatorRect.h - sz) / 2, sz, sz};
        drawUiImageContain(uiBtnPlay_, ir, 255, pal.light);
      } else {
        drawCenteredTextSafe(controlRenderer_, fontBase_, indicatorRect, "\xe2\x96\xb6", pal.light);
      }
    } else if (isQueued) {
      drawUIPanel(indicatorRect, pal.mid, pal.deep, pal.dark);
      if (uiBtnPlay_.texture) {
        int sz = std::min(18, std::min(indicatorRect.w - 10, indicatorRect.h - 10));
        SDL_Rect ir {indicatorRect.x + (indicatorRect.w - sz) / 2, indicatorRect.y + (indicatorRect.h - sz) / 2, sz, sz};
        drawUiImageContain(uiBtnPlay_, ir, 160, pal.deep);
      } else {
        drawCenteredTextSafe(controlRenderer_, fontBase_, indicatorRect, "\xe2\x96\xb6", pal.dark);
      }
    } else if (isSelected) {
      drawCenteredTextSafe(controlRenderer_, fontBase_, indicatorRect, "\xe2\x96\xb8", ink); // ▸
    }

    constexpr int kCueActionBtnW = 24;  // multiple of 8 — matches grid snap in drawUIPanel
    constexpr int kCueActionBtnH = 16;  // multiple of 8
    constexpr int kCueActionBtnGap = 4;
    constexpr int kCueActionCount = 5;
    int actionStripW = kCueActionCount * kCueActionBtnW + (kCueActionCount - 1) * kCueActionBtnGap;
    bool showActionStrip = row.w >= 254;  // nameX(52)+minName(52)+gap(8)+strip(136)+margin(6)
    int actionStripX = row.x + row.w - actionStripW - 6;

    int nameX = row.x + 52;
    // The action strip used to sit on the NAME line, right-aligned, which cost
    // the name 144px and truncated most real filenames to "Rick and Mo...".
    // It now sits on the metadata line, so the name gets the full row width and
    // the toggles read as a group under it rather than floating beside it.
    int nameW = row.w - nameX - 10 + row.x;

    // Cached display strings — avoids TTF loop in ellipsizeToPixelWidth every frame
    bool isProbing = cue.width == 0 && cue.height == 0 && !cue.path.empty()
                     && cue.kind != CueKind::Pattern && cue.kind != CueKind::LowerThird;
    auto& dc = cueRowDisplayCache_[cue.id];
    if (dc.name != cue.name || dc.nameW != nameW || dc.cueId != cue.cueId
        || dc.cueNumber != cue.cueNumber || dc.index != index || dc.kind != cue.kind
        || dc.duration != cue.duration || dc.stillDurationSeconds != cue.stillDurationSeconds
        || dc.endAction != cue.endAction || dc.width != cue.width || dc.height != cue.height
        || dc.pathEmpty != cue.path.empty()) {
      dc.name = cue.name;
      dc.nameW = nameW;
      dc.cueId = cue.cueId;
      dc.cueNumber = cue.cueNumber;
      dc.index = index;
      dc.kind = cue.kind;
      dc.duration = cue.duration;
      dc.stillDurationSeconds = cue.stillDurationSeconds;
      dc.endAction = cue.endAction;
      dc.width = cue.width;
      dc.height = cue.height;
      dc.pathEmpty = cue.path.empty();
      // Recompute cached strings
      dc.token = cueDisplayToken(cue, index);
      dc.kindUpper = toUpper(cueKindLabel(cue.kind));
      dc.ellipsizedName = ellipsizeToPixelWidth(fontSmall_, cue.name, nameW);
      if (isProbing) {
        dc.meta = "probing...";
      } else if (cue.kind == CueKind::Video || cue.kind == CueKind::Audio) {
        dc.meta = cue.duration > 0.0 ? formatSeconds(cue.duration) : "hold";
      } else {
        dc.meta = cue.stillDurationSeconds > 0.0 ? formatSeconds(cue.stillDurationSeconds) : "hold";
      }
      if (!isProbing && cue.endAction != CueEndAction::Inherit && cue.endAction != CueEndAction::Stop) {
        dc.meta += "  " + toUpper(cueEndActionLabel(cue.endAction));
      }
    }

    // Cue ID and Type — line 1 (top of row)
    SDL_Rect tokenRect {row.x + 52, row.y + 4, 50, 18};
    drawTextSafe(controlRenderer_, fontMono_, tokenRect, dc.token, subInk);

    {
      UiImageAsset* cueIcon = cueIconAssetForKind(cue.kind);
      SDL_Rect iconRect {row.x + 106, row.y + 3, 22, 22};
      if (cueIcon && drawUiImageContainTinted(*cueIcon, iconRect)) {
        // Icon drawn — show kind label shifted right
        SDL_Rect typeRect {row.x + 130, row.y + 5, 72, 18};
        drawTextSafe(controlRenderer_, fontSmall_, typeRect, dc.kindUpper, subInk);
      } else {
        SDL_Rect typeRect {row.x + 106, row.y + 5, 96, 18};
        drawTextSafe(controlRenderer_, fontSmall_, typeRect, dc.kindUpper, subInk);
      }
    }

    // Name — line 2 (middle of row, prominent)
    int nameY = row.y + 26;
    SDL_Rect nameRect {nameX, nameY, nameW, 24};
    drawTextSafe(controlRenderer_, fontSmall_, nameRect, dc.ellipsizedName, ink);

    // Metadata — line 3 (bottom of row, within bounds)
    // Metadata shares the bottom line with the action strip, so it stops short
    // of it instead of running underneath.
    const int metaW = showActionStrip ? std::max(40, actionStripX - nameX - 8) : nameW;
    SDL_Rect metaRect {nameX, row.y + 50, metaW, 18};
    drawTextSafe(controlRenderer_, fontSmall_, metaRect, dc.meta, isProbing ? pal.inkSoft : subInk);

    // Missing-media badge — right end of the name column, drawn live (not
    // via the display cache) so a relink clears it the same frame.
    if (cue.mediaMissing && nameW > 120) {
      SDL_Rect missRect {nameX + nameW - 66, row.y + 4, 62, 18};
      Primitives::fillRect(controlRenderer_, missRect, SDL_Color {160, 18, 18, 255});
      drawCenteredTextSafe(controlRenderer_, fontSmall_, missRect, "MISSING",
                           SDL_Color {255, 210, 210, 255});
    }

    auto drawCueRowActionIcon = [&](const SDL_Rect& rect, QuickAction action, SDL_Color inkColor, bool enabled) {
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(controlRenderer_, inkColor.r, inkColor.g, inkColor.b, inkColor.a);
      switch (action) {
        case QuickAction::ToggleFadeIn: {
          // Filled rising wedge — 1px outline ramps read as stray marks at
          // 20px button sizes; a solid shape reads as "fade up" at a glance.
          int left = rect.x + 3;
          int right = rect.x + rect.w - 4;
          int bottom = rect.y + rect.h - 5;
          int top = rect.y + 4;
          int span = std::max(1, right - left);
          for (int x = left; x <= right; ++x) {
            int h = (x - left) * (bottom - top) / span;
            SDL_RenderLine(controlRenderer_, x, bottom - h, x, bottom);
          }
          break;
        }
        case QuickAction::ToggleFadeOut: {
          // Filled falling wedge — mirror of fade-in.
          int left = rect.x + 3;
          int right = rect.x + rect.w - 4;
          int bottom = rect.y + rect.h - 5;
          int top = rect.y + 4;
          int span = std::max(1, right - left);
          for (int x = left; x <= right; ++x) {
            int h = (right - x) * (bottom - top) / span;
            SDL_RenderLine(controlRenderer_, x, bottom - h, x, bottom);
          }
          break;
        }
        case QuickAction::ToggleLoop: {
          // Drawn, not typed. This used to render the Unicode infinity glyph
          // through the UI font; Press Start 2P has no U+221E, so it came out
          // as tofu spilling out of the top-left of the button. Every icon in
          // this strip is now geometry, which cannot depend on font coverage.
          const int cx = rect.x + rect.w / 2;
          const int cy = rect.y + rect.h / 2;
          const int r = std::max(2, rect.h / 2 - 4);
          for (int lobe = -1; lobe <= 1; lobe += 2) {
            const int ox = cx + lobe * r;
            for (int a = 0; a < 360; a += 12) {
              const double rad = a * 3.14159265 / 180.0;
              SDL_RenderPoint(controlRenderer_,
                              static_cast<float>(ox + std::cos(rad) * r),
                              static_cast<float>(cy + std::sin(rad) * r * 0.9));
            }
          }
          break;
        }
        case QuickAction::ToggleHold: {
          // 3px pause bars — 2px reads as hairlines next to the solid wedges.
          SDL_Rect barL {rect.x + rect.w / 2 - 5, rect.y + 4, 3, rect.h - 8};
          SDL_Rect barR {rect.x + rect.w / 2 + 2, rect.y + 4, 3, rect.h - 8};
          Primitives::fillRect(controlRenderer_, barL, inkColor);
          Primitives::fillRect(controlRenderer_, barR, inkColor);
          break;
        }
        case QuickAction::ToggleCueAudio: {
          // Speaker box — taller, starts 2px from left
          int bw = std::max(3, rect.w / 6);
          int bh = std::max(5, rect.h * 5 / 9);
          int bx = rect.x + 2;
          int by = rect.y + (rect.h - bh) / 2;
          SDL_Rect box {bx, by, bw, bh};
          // Horn — triangle from box right edge to ~55% of button width
          int hornX = rect.x + rect.w * 55 / 100;
          int hornTopY = rect.y + 2;
          int hornBotY = rect.y + rect.h - 2;
          Primitives::fillRect(controlRenderer_, box, inkColor);
          // Fill the horn (outline-only didn't read as a speaker at this size)
          int hornSpan = std::max(1, hornX - (box.x + box.w));
          for (int x = box.x + box.w; x <= hornX; ++x) {
            int frac = (x - (box.x + box.w)) * 100 / hornSpan;
            int topY = by + (hornTopY - by) * frac / 100;
            int botY = by + bh + (hornBotY - (by + bh)) * frac / 100;
            SDL_RenderLine(controlRenderer_, x, topY, x, botY);
          }
          if (enabled) {
            // Two short wave lines at right side of button
            int wx = hornX + 3;
            int my = rect.y + rect.h / 2;
            SDL_RenderLine(controlRenderer_, wx, my - 3, wx + 2, my - 1);
            SDL_RenderLine(controlRenderer_, wx, my + 3, wx + 2, my + 1);
          }
          break;
        }
        default:
          break;
      }
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    };

    bool toggleHover = false;
    auto drawCueActionButton = [&](int buttonX, QuickAction action, bool on, bool enabled, const std::string& tip) {
      SDL_Rect btn {buttonX, row.y + 48, kCueActionBtnW, kCueActionBtnH};
      SDL_Color btnFill = !enabled
        ? pal.mid
        : (on ? pal.dark : pal.light);
      SDL_Color btnAccent = enabled && on ? pal.light : pal.mid;
      SDL_Color iconInk = !enabled
        ? pal.inkSoft
        : (on ? pal.light : pal.deep);
      drawUIPanel(btn, btnFill, pal.deep, btnAccent);
      // The icon must use the SAME rect the button was painted with. This used
      // to pass snapRectToGrid(btn) while drawUIPanel painted btn unsnapped, so
      // box and glyph lived in different coordinate spaces and every icon sat up
      // to 7px up-and-left of its box -- "outside their box", as it looked.
      // Same class as the v0.81.0 text-placement fix: when drawUIPanel stopped
      // grid-snapping, this call was missed.
      drawCueRowActionIcon(btn, action, iconInk, on);
      if (enabled) {
        cueRowActionHits_.push_back({btn, deckIndex, index, action, true, tip});
      }
      if (pointInRect(mouseX_, mouseY_, btn)) {
        drawHoverTip(tip, btn.x + btn.w / 2, btn.y);
        toggleHover = true;
      }
    };

    if (showActionStrip) {
      int bx = actionStripX;
      drawCueActionButton(bx, QuickAction::ToggleFadeIn, cue.fadeInSeconds > 0.001,
                          true, "Toggle fade in");
      bx += kCueActionBtnW + kCueActionBtnGap;
      drawCueActionButton(bx, QuickAction::ToggleFadeOut, cue.fadeOutSeconds > 0.001,
                          true, "Toggle fade out");
      bx += kCueActionBtnW + kCueActionBtnGap;
      drawCueActionButton(bx, QuickAction::ToggleLoop, cue.loop,
                          true, "Toggle loop");
      bx += kCueActionBtnW + kCueActionBtnGap;
      drawCueActionButton(bx, QuickAction::ToggleHold, cue.pauseOnLastFrame,
                          true, "Toggle hold on last frame");
      bx += kCueActionBtnW + kCueActionBtnGap;
      drawCueActionButton(bx, QuickAction::ToggleCueAudio, cue.audioEnabled,
                          cue.hasAudio, cue.hasAudio ? "Toggle cue audio" : "Cue has no audio");
    }

    // Remaining time badge for live row.
    // Width is measured, not the old fixed 80px: "-00:15.2" is eight monospace
    // glyphs, which fits Consolas on Windows but not the wider Liberation/DejaVu
    // Mono on Linux, where it rendered "-00...". drawUIPanel's bevel AND
    // drawCenteredTextSafe's inset both eat into the rect, hence the generous
    // pad — measuring exactly and padding thinly is precisely what reproduced
    // this bug in the timeline chips. 80 stays the floor, so Windows is
    // unchanged.
    if (isLive) {
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      if (engine && engine->duration() > 0.0) {
        double remaining = std::max(0.0, engine->duration() - engine->position());
        std::string remStr = "-" + formatSeconds(remaining);
        int remTextW = 0, remTextH = 0;
        TTF_GetStringSize(fontMono_, remStr.c_str(), remStr.size(), &remTextW, &remTextH);
        int badgeW = std::max(80, remTextW + 16);
        badgeW = std::min(badgeW, std::max(80, row.w - 24));  // never overrun the row
        SDL_Rect badge {row.x + row.w - (badgeW + 4), row.y + 4, badgeW, 24};
        drawUIPanel(badge, pal.dark, pal.light, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontMono_, badge, remStr, pal.light);
      }
    }
    
    // Selection pulse
    if (project_.uiTransitionsEnabled && isSelected) {
      double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_ - selectionChangedAt_) / 95.0);
      SDL_Color glow {155, 188, 15, static_cast<Uint8>(40 + pulse * 60.0)};
      Primitives::strokeRect(controlRenderer_, insetRect(row, 1), glow);
    }

    // Hover tooltip logic preserved...
    if (!toggleHover && pointInRect(mouseX_, mouseY_, row)) {
      std::string rowTip = cue.name + "  |  " + cueKindLabel(cue.kind);
      drawHoverTip(rowTip, row.x + row.w / 2, row.y);
    }
  }

  // Draw a small floating tooltip panel anchored below/above (ax, ay).
  void drawHoverTip(const std::string& tip, int ax, int ay) {
    if (tip.empty()) return;
    int w = 0;
    TTF_GetStringSize(fontSmall_, tip.c_str(), 0, &w, nullptr);
    w += 20;
    int h = 26;
    int x = ax - w / 2;
    int y = ay - h - 6;
    // Keep on screen
    int winW = 0, winH = 0;
    SDL_GetWindowSize(controlWindow_, &winW, &winH);
    x = std::clamp(x, 6, winW - w - 6);
    y = std::max(y, 6);
    SDL_Rect panel {x, y, w, h};
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_Color bg {15, 56, 15, 230};
    Primitives::fillRect(controlRenderer_, panel, bg);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    Primitives::strokeRect(controlRenderer_, panel, pal.dark);
    drawText(controlRenderer_, fontSmall_, tip, pal.light, panel.x + 10, panel.y + 6);
  }

  void renderButtons() {
    if (bottomBarRect_.w > 0 && bottomBarRect_.h > 0) {
      drawUIPanel(bottomBarRect_,
                  pal.shellInner,
                  pal.deep,
                  pal.shellOuter);
    }
    auto drawGroupFrame = [&](const SDL_Rect& rect, const std::string& label) {
      if (rect.w <= 0 || rect.h <= 0) {
        return;
      }
      // Structural chrome uses the tile/fg pair rather than light/deep. Both
      // roles fall back to exactly what was here before (tile -> screen_light,
      // fg_soft -> screen_dark), so themes that don't define them are
      // unchanged — but a theme that does can finally make the shell dark
      // while keeping screen_light bright for ink. screen_light does double
      // duty as ink AND fill, which is why no theme could be dark before.
      drawUIPanel(rect,
                  pal.tile,
                  pal.deep,
                  pal.mid);
      SDL_Rect labelRect {rect.x + 10, rect.y + 6, rect.w - 20, 22};
      drawTextSafe(controlRenderer_, fontPixelSmall_ ? fontPixelSmall_ : fontSmall_, labelRect, label,
                   pal.fgSoft);
      // (no divider — label alone provides group separation)
    };
    drawGroupFrame(mediaGroupRect_, "MEDIA");
    drawGroupFrame(transportGroupRect_, "TRANSPORT");
    drawGroupFrame(outputGroupRect_, "OUTPUT");

    // ── On-air row ────────────────────────────────────────────────────────
    // One transmitting badge per stream type, on the OUTPUT group header where
    // the operator already looks. Settings is the wrong place to learn you are
    // live: by the time you have opened a modal to check, you are not looking
    // at the show. The rings only animate while frames are genuinely going
    // out, so a frozen badge is itself the warning.
    {
      struct OnAirEntry { const char* label; bool configured; bool live; OutputHealthState health; };
      std::vector<OnAirEntry> entries;
      auto streamEntry = [&](const char* label, const char* protocol) {
        const int idx = findStreamOutputForProtocol(protocol);
        if (idx < 0) {
          return;  // never configured: don't clutter the bar with it
        }
        const OutputTarget& o = project_.outputs[idx];
        entries.push_back({label, true, o.enabled && o.streamEnabled,
                           outputHealthStateForDisplay(idx)});
      };
      streamEntry("SRT", "srt");
      streamEntry("RTMP", "rtmp");
      for (std::size_t i = 0; i < project_.outputs.size(); ++i) {
        const OutputTarget& o = project_.outputs[i];
        if (o.st2110Enabled) {
          entries.push_back({"2110", true, o.enabled,
                             outputHealthStateForDisplay(static_cast<int>(i))});
          break;
        }
      }
      for (std::size_t i = 0; i < project_.outputs.size(); ++i) {
        const OutputTarget& o = project_.outputs[i];
        if (o.ndiEnabled) {
          entries.push_back({"NDI", true, o.enabled,
                             outputHealthStateForDisplay(static_cast<int>(i))});
          break;
        }
      }

      if (!entries.empty() && outputGroupRect_.w > 0) {
        const int badgeH = 14;
        const int gap = 6;
        TTF_Font* onAirFont = fontSmall_;
        int totalW = 0;
        std::vector<int> textW(entries.size(), 0);
        for (std::size_t i = 0; i < entries.size(); ++i) {
          int w = 0, h = 0;
          if (onAirFont) {
            TTF_GetStringSize(onAirFont, entries[i].label,
                              std::strlen(entries[i].label), &w, &h);
          }
          // drawTextSafe insets the rect it is given before laying out, so a
          // rect measured to the exact string width ellipsizes ("2110" became
          // "21..."). Pad past the inset.
          textW[i] = w + 10;
          totalW += badgeH + 3 + textW[i] + gap;
        }
        // Right-aligned in the group header, so it never collides with the
        // "OUTPUT" label on the left however many badges appear.
        int x = outputGroupRect_.x + outputGroupRect_.w - 12 - totalW + gap;
        const int y = outputGroupRect_.y + 6;
        for (std::size_t i = 0; i < entries.size(); ++i) {
          SDL_Rect badge {x, y + 1, badgeH, badgeH};
          // Bottom-bar groups fill with pal.tile, so the live tint is pal.fg —
          // pal.light here would be light-on-light and invisible.
          drawStreamOnAirBadge(badge, entries[i].configured, entries[i].live,
                               entries[i].health, pal.fg);
          SDL_Rect lab {x + badgeH + 3, y, textW[i], badgeH + 4};
          drawTextSafe(controlRenderer_, onAirFont, lab, entries[i].label,
                       entries[i].live ? pal.fg : pal.fgSoft);
          x += badgeH + 3 + textW[i] + gap;
        }
      }
    }

    auto buttonIconForLabel = [&](const std::string& label) -> UiImageAsset* {
      if (label == "IMPORT")   return &uiBtnImport_;
      if (label == "BROWSER")  return &uiCueIconBrowser_;
      if (label == "SOURCE")   return &uiCueIconSource_;
      if (label == "PATTERN")  return &uiCueIconPattern_;
      if (label == "TAKE")     return &uiBtnTake_;
      if (label == "STOP")     return &uiBtnStop_;
      if (label == "RERACK")   return &uiBtnRerack_;
      if (label == "CLEAR")    return &uiBtnClear_;
      if (label == "SETTINGS") return &uiBtnSettings_;
      return nullptr;
    };

    for (const auto& button : buttons_) {
      bool emphasized = button.label == "TAKE";
      UiImageAsset* icon = buttonIconForLabel(button.label);
      // Pulsing red border while rolling — the same language blackout uses,
      // because it is the same kind of state: one the operator must not lose
      // track of, and one whose cost of not noticing is the whole show.
      if (button.label == "RECORD" && recordingActive()) {
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
        double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_) * 0.006);
        Uint8 glowA = static_cast<Uint8>(80 + 140 * pulse);
        SDL_SetRenderDrawColor(controlRenderer_, 220, 30, 30, glowA);
        SDL_Rect glow {button.rect.x - 2, button.rect.y - 2,
                       button.rect.w + 4, button.rect.h + 4};
        SDL_RenderRect(controlRenderer_, &glow);
        SDL_SetRenderDrawColor(controlRenderer_, 220, 30, 30,
                               static_cast<Uint8>(glowA / 2));
        SDL_Rect glow2 {button.rect.x - 3, button.rect.y - 3,
                        button.rect.w + 6, button.rect.h + 6};
        SDL_RenderRect(controlRenderer_, &glow2);
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
      }
      // Draw button chrome (bevel lines in drawUIPanel provide the raised look)
      SDL_Color accent = emphasized ? pal.light : pal.dark;
      drawUIPanel(button.rect, button.fill, pal.deep, accent);

      // An icon is an ADORNMENT; the label is the control. When both cannot
      // fit, drop the icon, not the word — a gear beside "SET..." tells an
      // operator strictly less than "SETTINGS" alone. Narrowing the OUTPUT
      // group to fit RECORD is what surfaced this, but it was always latent:
      // any window narrow enough truncated a labelled button silently.
      if (icon && icon->texture) {
        TTF_Font* probeFont = fontPixelSmall_ ? fontPixelSmall_ : fontSmall_;
        const int probeIcon = std::min(24, button.rect.h - 16);
        const int probeTextX = button.rect.x + 8 + probeIcon + 4;
        // Measure against the rect the label ACTUALLY gets: drawCenteredTextSafe
        // runs safeTextRect first, so comparing against the raw width leaves the
        // label ellipsized anyway. That inset is why the first attempt at this
        // changed nothing.
        const SDL_Rect probeRect = safeTextRect(SDL_Rect {
          probeTextX, button.rect.y + 8,
          button.rect.x + button.rect.w - probeTextX - 6,
          button.rect.h - 14});
        int fullW = 0;
        if (probeFont) {
          TTF_GetStringSize(probeFont, button.label.c_str(), button.label.size(),
                            &fullW, nullptr);
        }
        if (fullW > probeRect.w) {
          icon = nullptr;
        }
      }

      if (icon && icon->texture) {
        // Icon on left, label on right
        int iconSize = std::min(24, button.rect.h - 16);
        int iconX = button.rect.x + 8;
        int iconY = button.rect.y + (button.rect.h - iconSize) / 2;
        SDL_Rect iconRect {iconX, iconY, iconSize, iconSize};
        drawUiImageContainTinted(*icon, iconRect);
        int textX = iconX + iconSize + 4;
        int textW = button.rect.x + button.rect.w - textX - 6;
        SDL_Rect labelRect {textX, button.rect.y + 8, textW, button.rect.h - 14};
        // Icon+text buttons take the pixel face too, matching the text-only
        // branch below: these are short fixed labels (TAKE, STOP, IMPORT), the
        // half of the UI the pixel font is actually good at.
        TTF_Font* btnFont = fontPixelSmall_ ? fontPixelSmall_ : fontSmall_;
        std::string clipped = ellipsizeToPixelWidth(btnFont, button.label, std::max(0, textW));
        drawCenteredTextSafe(controlRenderer_, btnFont, labelRect, clipped, button.text);
      } else {
        // Text only — centered, prefer pixel font for that Nintendo feel
        TTF_Font* titleFont = fontPixelSmall_ ? fontPixelSmall_ :
                              ((button.rect.h < 34 || button.rect.w < 112 || button.label.size() > 7)
                              ? fontSmall_ : fontBase_);
        std::string clipped = ellipsizeToPixelWidth(titleFont, button.label, std::max(0, button.rect.w - 10));
        SDL_Rect titleRect {button.rect.x + 4, button.rect.y + 8, button.rect.w - 8, button.rect.h - 14};
        drawCenteredTextSafe(controlRenderer_, titleFont, titleRect, clipped, button.text);
      }
    }
    // Hover tip for bottom-bar buttons
    for (const auto& button : buttons_) {
      if (!button.tip.empty() && pointInRect(mouseX_, mouseY_, button.rect)) {
        drawHoverTip(button.tip, button.rect.x + button.rect.w / 2, button.rect.y);
        break;
      }
    }

    // ─── Bottom bar sparkle area ───
    // Ambient sparkles + state-indicating animations in the empty space
    // within the output group, after the 2 buttons
    if (bottomBarRect_.w > 0 && bottomBarRect_.h > 0 && buttons_.size() >= 10) {
      // Rightmost button in the output group — SETTINGS, now buttons_[9]
      // after BLACKOUT and RECORD joined the group. Index-based anchors like
      // this are exactly what breaks when the bar changes; keep it in step.
      SDL_Rect lastOutBtn = buttons_[9].rect;
      int sparkleAreaX = lastOutBtn.x + lastOutBtn.w + 12;
      int sparkleAreaW = outputGroupRect_.x + outputGroupRect_.w - sparkleAreaX - 8;
      if (sparkleAreaW < 40) {
        // No room: draw nothing. The old fallback re-anchored to the whole
        // group and drew the sparkles straight over the buttons, since they
        // are painted after them — decoration on top of controls.
        sparkleAreaW = 0;
      }
      if (sparkleAreaW > 30) {
        int sparkleAreaCY = bottomBarRect_.y + bottomBarRect_.h / 2;

        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);

        // Ambient orbiting stars
        {
          SDL_Color starC = pal.dark;
          for (int s = 0; s < 5; ++s) {
            double phase = static_cast<double>(animationNow_) * 0.0015 + s * 1.257;
            int orbitR = std::min(sparkleAreaW / 3, 60);
            int sx = sparkleAreaX + sparkleAreaW / 2 + static_cast<int>(std::sin(phase) * orbitR);
            int sy = sparkleAreaCY + static_cast<int>(std::cos(phase * 0.8) * 18.0);
            int arm = 3 + (s % 3);
            starC.a = static_cast<Uint8>(100 + 155 * std::abs(std::sin(phase * 0.5)));
            drawStar(controlRenderer_, sx, sy, arm, starC);
          }
        }

        // Blackout warning — pulsing red stars when blacked out
        if (masterDimmerTarget_ < 0.5) {
          SDL_Color rc {180, 40, 40, 255};
          for (int s = 0; s < 4; ++s) {
            double phase = static_cast<double>(animationNow_) * 0.005 + s * 1.57;
            int sx = sparkleAreaX + 20 + s * (sparkleAreaW / 5);
            int sy = sparkleAreaCY + static_cast<int>(std::sin(phase) * 14.0);
            rc.a = static_cast<Uint8>(120 + 135 * std::abs(std::sin(phase * 1.2)));
            drawStar(controlRenderer_, sx, sy, 3 + (s % 2), rc);
          }
        }

        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
      }
    }
  }

  // The wait while a slide deck converts and renders.
  //
  // A hundred-slide deck is half a minute of nothing, and a toast that says
  // "rendering..." and then fades is indistinguishable from an app that
  // ignored you. This is the same friend from the empty program monitor,
  // told what it is doing -- so the wait has a face on it and a bar that
  // visibly moves, and the operator can see the machine is working.
  void renderSlideRenderCard(int windowWidth, int windowHeight) {
    if (!slideRenderActive_) {
      return;
    }
    const int done = slideRenderPage_.load(std::memory_order_relaxed);
    const int total = slideRenderTotal_.load(std::memory_order_relaxed);

    const int cardW = std::min(uiScaled(360), std::max(uiScaled(220), windowWidth - uiScaled(80)));
    const int cardH = uiScaled(250);
    SDL_Rect card {(windowWidth - cardW) / 2, (windowHeight - cardH) / 2, cardW, cardH};
    Primitives::drawFramedPanel(controlRenderer_, card, pal.shellInner, pal.shellShadow, pal.mid);

    // The face gets the top of the card; the bar and the words sit under it.
    SDL_Rect face {card.x + uiScaled(12), card.y + uiScaled(10),
                   card.w - uiScaled(24), cardH - uiScaled(76)};
    // Counting pages only once the renderer has reported one: before that the
    // honest thing to say is that the converter is still running, because it
    // is, and a "0 of 0" would look stuck.
    std::string tip;
    if (total > 0) {
      tip = "rendering slide " + std::to_string(std::min(done + 1, total)) +
            " of " + std::to_string(total);
    } else {
      tip = slideRenderTitle_.empty() ? std::string("converting the deck...")
                                      : ("converting " + slideRenderTitle_ + "...");
    }
    drawStartupMascot(face, animationNow_, tip.c_str());

    SDL_Rect bar {card.x + uiScaled(20), card.y + cardH - uiScaled(46),
                  card.w - uiScaled(40), uiScaled(14)};
    Primitives::drawFramedPanel(controlRenderer_, bar, pal.deep, pal.deep, pal.mid);
    if (total > 0) {
      SDL_Rect fill = bar;
      fill.w = static_cast<int>(bar.w * std::clamp(
        static_cast<double>(done) / static_cast<double>(total), 0.0, 1.0));
      if (fill.w > 0) {
        SDL_SetRenderDrawColor(controlRenderer_, pal.light.r, pal.light.g,
                               pal.light.b, 255);
        SDL_FRect f {static_cast<float>(fill.x), static_cast<float>(fill.y),
                     static_cast<float>(fill.w), static_cast<float>(fill.h)};
        SDL_RenderFillRect(controlRenderer_, &f);
      }
    } else {
      // NOTHING TO MEASURE YET, so the bar paces rather than pretending to a
      // percentage. The converter is another application and reports nothing
      // on its way through; inventing a number for it would be a lie that
      // stalls at 40%.
      const double t = static_cast<double>(animationNow_ % 1400) / 1400.0;
      const int runW = std::max(uiScaled(24), bar.w / 5);
      SDL_Rect fill = bar;
      fill.w = runW;
      fill.x = bar.x + static_cast<int>((bar.w - runW) *
                                        (0.5 - 0.5 * std::cos(t * 6.2831853)));
      SDL_SetRenderDrawColor(controlRenderer_, pal.light.r, pal.light.g,
                             pal.light.b, 255);
      SDL_FRect f {static_cast<float>(fill.x), static_cast<float>(fill.y),
                   static_cast<float>(fill.w), static_cast<float>(fill.h)};
      SDL_RenderFillRect(controlRenderer_, &f);
    }
  }

  void renderToast(int windowWidth) {
    if (!project_.uiTransitionsEnabled || !toast_.active) {
      return;
    }

    Uint64 elapsed = animationNow_ - toast_.startedAt;
    if (elapsed >= toast_.durationMs) {
      toast_.active = false;
      return;
    }

    double progress = static_cast<double>(elapsed) / static_cast<double>(toast_.durationMs);
    double intro = easeOutCubic(std::min(progress / 0.2, 1.0));
    double outro = progress > 0.78 ? 1.0 - easeOutCubic((progress - 0.78) / 0.22) : 1.0;
    double visibility = std::min(intro, outro);

    // Size the panel to its message rather than a fixed 300px. Several toasts
    // are legitimately long — "normalized: -0.9 dB (was -25.7 LUFS) -
    // peak-limited at -0.1 dBFS", "display connected: <name> (2 total)" — and
    // the fixed panel cut them off mid-sentence, which is the worst possible
    // outcome for a message whose entire job is to explain something.
    // Bounded so it can never span the whole header, and routed through
    // drawTextSafe so it ellipsizes rather than bleeding past the frame.
    int textW = 0;
    int textH = 0;
    if (fontBase_) {
      TTF_GetStringSize(fontBase_, toast_.message.c_str(), 0, &textW, &textH);
    }
    const int toastPadX = uiScaled(14);
    const int minToastW = uiScaled(220);
    int maxToastW = std::max(minToastW, windowWidth - uiScaled(88));
    int panelW = std::clamp(textW + toastPadX * 2, minToastW, maxToastW);
    int panelH = std::max(uiScaled(58), textH + uiScaled(24));

    SDL_Rect panel {0, uiScaled(36) + static_cast<int>((1.0 - visibility) * -uiScaled(24)),
                    panelW, panelH};
    panel.x = windowWidth - uiScaled(44) - static_cast<int>(panelW * visibility);
    Primitives::drawFramedPanel(controlRenderer_, panel, toast_.fill, pal.deep, pal.mid);
    drawTextSafe(controlRenderer_, fontBase_, panel, toast_.message, toast_.ink);
  }

  // Draws a tiny 4-pointed pixel star centered at (cx, cy), arm half-length S.
  void drawStar(SDL_Renderer* r, int cx, int cy, int S, SDL_Color c) {
    // Center pixel
    SDL_Rect center {cx - 1, cy - 1, 2, 2};
    Primitives::fillRect(r, center, c);
    // Four arms
    for (int i = 1; i <= S; ++i) {
      Uint8 fade = static_cast<Uint8>(c.a * (S - i + 1) / (S + 1));
      SDL_Color arm {c.r, c.g, c.b, fade};
      SDL_Rect h {cx + i, cy - 1, 2, 2}; Primitives::fillRect(r, h, arm);
      SDL_Rect hl{cx - i - 1, cy - 1, 2, 2}; Primitives::fillRect(r, hl, arm);
      SDL_Rect v {cx - 1, cy + i, 2, 2}; Primitives::fillRect(r, v, arm);
      SDL_Rect vt{cx - 1, cy - i - 1, 2, 2}; Primitives::fillRect(r, vt, arm);
    }
  }

  // Character-art rendering has been intentionally removed from operational UI paths.



