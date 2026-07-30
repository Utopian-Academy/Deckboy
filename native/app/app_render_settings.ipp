// ============================================================================
// app_render_settings.ipp — Settings modal dialog rendering and actions.
//
// Renders the settings modal (opened by the SETTINGS button or Ctrl+,):
//
//   renderSettingsModal()        — entry point: backdrop, tabs, tab content
//   renderSettingsTabGeneral()   — general settings (project name, BPM, etc.)
//   renderSettingsTabOutput()    — output configuration (resolution, displays)
//   renderSettingsTabNetwork()   — network settings (OSC, Companion, NMC, etc.)
//   renderSettingsTabIntegration() — integration backends (ATEM, NDI, LTC, etc.)
//   handleSettingsAction()       — dispatch button presses by action constant
//
// Settings actions are integer constants (kSettingsAction*) defined in the
// App class. Each button/toggle in the settings UI has a unique action ID.
// When clicked, handleSettingsAction() routes to the appropriate handler.
//
// Current highest action constant: 623 (kSettingsActionOutputAoiBDec).
// New actions should be allocated from 624+.
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Render the settings modal dialog over the main control window.
  // Shared settings chrome: a cartridge-style "label plate" header — dark
  // plate, light title — so every card/section has one strong, scannable
  // anchor (the old headers were dark-on-light text that blended into the
  // card fill). Geometry contract: the header band occupies the same ~50px
  // the old drawCard used (title ~y+6, subtitle ~y+34), so the hardcoded
  // control offsets inside every card are untouched.
  // AOI pixel-rect view of the focused output's crop fractions. The operator
  // edits X/Y/WIDTH/HEIGHT in pixels of the output raster; storage stays the
  // four edge fractions (serialization untouched).
  struct AoiRectPx {
    int x = 0, y = 0, w = 0, h = 0;
    int rasterW = 0, rasterH = 0;
  };

  AoiRectPx focusedOutputAoiRectPx() {
    const OutputTarget& ot = focusedOutput();
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
    AoiRectPx rect;
    rect.rasterW = std::max(16, rasterW);
    rect.rasterH = std::max(16, rasterH);
    rect.x = static_cast<int>(std::lround(std::clamp(ot.aoiLeft, 0.0f, 0.95f) * rect.rasterW));
    rect.y = static_cast<int>(std::lround(std::clamp(ot.aoiTop, 0.0f, 0.95f) * rect.rasterH));
    rect.w = std::max(1, static_cast<int>(std::lround(
      std::clamp(1.0f - ot.aoiLeft - ot.aoiRight, 0.0f, 1.0f) * rect.rasterW)));
    rect.h = std::max(1, static_cast<int>(std::lround(
      std::clamp(1.0f - ot.aoiTop - ot.aoiBottom, 0.0f, 1.0f) * rect.rasterH)));
    return rect;
  }

  void applyFocusedOutputAoiRectPx(int x, int y, int w, int h) {
    OutputTarget& ot = focusedOutputMutable();
    AoiRectPx cur = focusedOutputAoiRectPx();
    // Keep the region at least 5% of the raster in both dimensions so each
    // stored edge fraction stays within the serializer's 0–0.95 clamp.
    int minW = std::max(1, (cur.rasterW + 19) / 20);
    int minH = std::max(1, (cur.rasterH + 19) / 20);
    w = std::clamp(w, minW, cur.rasterW);
    h = std::clamp(h, minH, cur.rasterH);
    x = std::clamp(x, 0, cur.rasterW - w);
    y = std::clamp(y, 0, cur.rasterH - h);
    ot.aoiLeft   = static_cast<float>(x) / static_cast<float>(cur.rasterW);
    ot.aoiRight  = static_cast<float>(cur.rasterW - x - w) / static_cast<float>(cur.rasterW);
    ot.aoiTop    = static_cast<float>(y) / static_cast<float>(cur.rasterH);
    ot.aoiBottom = static_cast<float>(cur.rasterH - y - h) / static_cast<float>(cur.rasterH);
    markProjectDirty();
  }

  void openAoiValueEditor(const char* token, const char* title, int currentPx,
                          std::function<void(int)> applyPx) {
    openInlineTextEditor(token, title, "pixels (of the output raster)",
                         std::to_string(currentPx),
                         [this, applyPx](const std::string& rawValue) {
                           try {
                             applyPx(std::stoi(trim(rawValue)));
                           } catch (...) {
                             triggerToast("aoi: invalid number");
                           }
                         });
  }

  // ── Section header contract ───────────────────────────────────────────────
  // Every titled box in Settings — the System tab's cards and the Video tab's
  // sections — gets its title plate from here, so they are inset from the frame
  // by the same margin and sized from the same font metric. Previously the two
  // had independent geometry (26px plate/fontBase_ vs 22px plate/fontSmall_,
  // both with hardcoded text offsets), which is why headers sat at different
  // heights depending on which tab you were looking at.
  int settingsPlateInset() const { return uiScaled(5); }

  int settingsPlateHeight(TTF_Font* font) const {
    return std::max(uiScaled(20), textLineHeight(font ? font : fontSmall_) + uiScaled(6));
  }

  // Vertical space a titled box spends on its header before body content
  // starts. Callers sizing a section from its row count use this instead of
  // the old hardcoded 32 so the box still fits when the font changes.
  int settingsHeaderHeight(TTF_Font* font) const {
    return settingsPlateInset() + settingsPlateHeight(font) + uiScaled(6);
  }

  SDL_Rect settingsPlateRect(const SDL_Rect& rect, TTF_Font* font) const {
    int inset = settingsPlateInset();
    return SDL_Rect {rect.x + inset, rect.y + inset,
                     std::max(0, rect.w - inset * 2),
                     settingsPlateHeight(font)};
  }

  // Draws the plate and returns it, so callers that need to park a control on
  // the header (the CONNECTED DISPLAYS "IDENTIFY" button) can align to the
  // plate itself instead of guessing an offset from the card frame.
  SDL_Rect drawSettingsPlate(const SDL_Rect& rect, const std::string& title, TTF_Font* font) {
    SDL_Rect plate = settingsPlateRect(rect, font);
    Primitives::drawFramedPanel(controlRenderer_, plate, pal.dark, pal.deep, pal.mid);
    drawTextSafe(controlRenderer_, font ? font : fontSmall_, plate, title, pal.light);
    return plate;
  }

  // Thin scrollbar down the right edge of a scrolling settings viewport, so it
  // is obvious there is more below rather than the tab looking truncated.
  void drawSettingsScrollHint(const SDL_Rect& viewport, int scroll, int scrollMax) {
    if (scrollMax <= 0 || viewport.h <= 0) {
      return;
    }
    int trackW = uiScaled(4);
    SDL_Rect track {viewport.x + viewport.w - trackW - uiScaled(3), viewport.y + uiScaled(3),
                    trackW, viewport.h - uiScaled(6)};
    Primitives::fillRect(controlRenderer_, track, pal.mid);
    double visible = static_cast<double>(viewport.h) / (viewport.h + scrollMax);
    int thumbH = std::max(uiScaled(18), static_cast<int>(track.h * visible));
    int travel = std::max(0, track.h - thumbH);
    int thumbY = track.y + (scrollMax > 0 ? travel * scroll / scrollMax : 0);
    Primitives::fillRect(controlRenderer_, SDL_Rect{track.x, thumbY, track.w, thumbH}, pal.deep);
  }

  void drawSettingsCard(const SDL_Rect& rect, const std::string& title,
                        const std::string& subtitle = std::string()) {
    Primitives::drawFramedPanel(controlRenderer_, rect, pal.shellInner, pal.deep, pal.light);
    SDL_Rect plate = drawSettingsPlate(rect, title, fontBase_);
    if (!subtitle.empty()) {
      int subH = std::max(uiScaled(14), textLineHeight(fontSmall_));
      int subInset = settingsPlateInset() + uiScaled(7);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {rect.x + subInset, plate.y + plate.h + uiScaled(4),
                             std::max(0, rect.w - subInset * 2), subH},
                   subtitle, pal.inkSoft);
    }
  }

  void renderSettingsModal() {
    if (!settingsOpen_) return;
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);

    // Dim backdrop
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 160);
    SDL_Rect full {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &full);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    // Modal panel
    SDL_Rect modal = settingsModalRect();
    Primitives::drawFramedPanel(controlRenderer_, modal, pal.shellInner, pal.deep, pal.shellOuter);

    // Title — pixel face for the Game Boy read
    TTF_Font* modalTitleFont = fontPixel_ ? fontPixel_ : fontBase_;
    int titleH = std::max(uiScaled(28), textLineHeight(modalTitleFont) + uiScaled(4));
    drawTextSafe(controlRenderer_, modalTitleFont,
                 SDL_Rect {modal.x + uiScaled(16), modal.y + uiScaled(8),
                           modal.w - uiScaled(64), titleH},
                 "SETTINGS", pal.fg);

    // Close button [X]
    int closeW = uiScaled(34);
    int closeH = uiScaled(30);
    settingsCloseBtn_ = {modal.x + modal.w - closeW - uiScaled(8), modal.y + uiScaled(6),
                         closeW, closeH};
    Primitives::drawFramedPanel(controlRenderer_, settingsCloseBtn_, pal.mid, pal.deep, pal.light);
    drawCenteredText(controlRenderer_, fontSmall_, "X", pal.deep, settingsCloseBtn_);

    // Tab bar — cartridge-shelf tabs: the active tab is full height and
    // "plugged in" (a joint bar bridges it to the content frame); inactive
    // tabs sit recessed. One glance shows where you are. Tabs size to their
    // labels (with a shared floor) so nothing ellipsizes.
    const int kTabH = std::max(uiScaled(40), textLineHeight(fontSmall_) + uiScaled(18));
    const int kTabGap = uiScaled(6);
    int tabY = modal.y + std::max(uiScaled(44), titleH + uiScaled(12));
    settingsBtns_.clear();
    const std::vector<std::string> tabs {"System", "Audio", "Network", "Video Outputs", "About", "Encoder"};
    int tabX = modal.x + uiScaled(16);
    for (int t = 0; t < (int)tabs.size(); ++t) {
      bool active = (t == settingsTab_);
      int recess = active ? 0 : uiScaled(6);
      int labelW = 0;
      int labelH = 0;
      if (fontSmall_) {
        TTF_GetStringSize(fontSmall_, tabs[t].c_str(), 0, &labelW, &labelH);
      }
      int tabW = std::max(uiScaled(88), labelW + uiScaled(24));
      SDL_Rect tab {tabX, tabY + recess, tabW, kTabH - recess};
      Primitives::drawFramedPanel(controlRenderer_, tab, active ? pal.dark : pal.light,
                      pal.deep, active ? pal.light : pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, tab, tabs[t],
                           active ? pal.light : pal.deep);
      if (active) {
        SDL_Rect joint {tab.x + 2, tab.y + tab.h - 2, tab.w - 4, uiScaled(8)};
        Primitives::fillRect(controlRenderer_, joint, pal.dark);
      }
      settingsBtns_.push_back({tab, 100 + t, tabs[t]});
      tabX += tabW + kTabGap;
    }

    // Content area — tile fill so it reads as a dark frame on terminal themes
    // (= screen_light on light themes, unchanged). All content sits inside
    // cards, so nothing draws text directly on this frame.
    int contentTop = tabY + kTabH + uiScaled(10);
    SDL_Rect content {modal.x + uiScaled(16), contentTop, modal.w - uiScaled(32),
                      std::max(uiScaled(80), modal.y + modal.h - contentTop - uiScaled(16))};
    Primitives::drawFramedPanel(controlRenderer_, content, pal.tile, pal.deep, pal.mid);

    int cx = content.x + uiScaled(12), cy = content.y + uiScaled(10);
    SDL_Color ink = pal.deep;
    SDL_Color soft = pal.inkSoft;

    // ── Shared layout metrics for every tab ─────────────────────────────────
    // Rows are sized from the font and every constant goes through uiScaled(),
    // so chrome grows with the type instead of staying at 1x and letting labels
    // collide. At uiScale 1.0 these reduce to the values the tabs were
    // originally authored with, so the 1x layout is unchanged.
    const int sPad = uiScaled(8);
    const int sGap = uiScaled(6);
    const int sLineH = std::max(uiScaled(16), textLineHeight(fontSmall_));
    const int sRowH = std::max(uiScaled(24), textLineHeight(fontSmall_) + uiScaled(9));
    const int sTallH = std::max(uiScaled(30), textLineHeight(fontSmall_) + uiScaled(15));
    const int sChipH = std::max(uiScaled(22), textLineHeight(fontSmall_) + uiScaled(7));
    // Header plate + the one-line subtitle the cards carry.
    const int sCardHeaderH = settingsHeaderHeight(fontBase_) + sLineH + sGap;
    auto cardBodyY = [&](const SDL_Rect& card) { return card.y + sCardHeaderH; };
    auto cardBodyX = [&](const SDL_Rect& card) { return card.x + sPad; };
    auto cardBodyW = [&](const SDL_Rect& card) { return card.w - sPad * 2; };

    if (settingsTab_ == 0) {
      const Deck& tcDeck = focusedDeck();
      auto drawPillToggle = [&](const SDL_Rect& rect, bool on, const std::string& onLabel, const std::string& offLabel) {
        Primitives::drawFramedPanel(controlRenderer_, rect,
                        on ? pal.dark : pal.mid,
                        pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, on ? onLabel : offLabel,
                         on ? pal.light : pal.deep, rect);
      };

      auto drawCard = [&](const SDL_Rect& rect, const std::string& title, const std::string& subtitle = std::string()) {
        drawSettingsCard(rect, title, subtitle);
      };

      // Card heights are computed from their contents rather than being magic
      // numbers, so a taller font pushes the card out instead of overflowing it.
      int colGap = uiScaled(12);
      int colH = content.h - uiScaled(20);
      int leftW = std::max(uiScaled(320), (content.w - uiScaled(12) - colGap) / 2);
      int rightW = std::max(uiScaled(320), content.w - uiScaled(12) - colGap - leftW);
      const int kCardGap = uiScaled(10);

      int appearanceH = sCardHeaderH + sTallH + sRowH * 3 + sTallH + sGap * 4 + sPad;
      int safetyH = sCardHeaderH + sLineH + uiScaled(4) + sChipH * 2 + sGap + sPad;
      int flowH = sCardHeaderH + sRowH * 3 + sLineH + uiScaled(4) + sGap * 2 + sPad;
      int cueToolsH = sCardHeaderH + sLineH * 2 + sGap + sRowH + sPad;
      int prefsH = sCardHeaderH + sRowH * 5 + sLineH + sGap * 2 + uiScaled(4) * 3 + sPad;

      // At large UI scales the cards are genuinely taller than the window can
      // show, so the tab scrolls rather than silently cropping the bottom card.
      // At 1x nothing overflows and the scroll is inert.
      int leftNeeded = appearanceH + kCardGap + safetyH + kCardGap + flowH;
      int rightNeeded = cueToolsH + kCardGap + prefsH;
      settingsSystemScrollMax_ = std::max(0, std::max(leftNeeded, rightNeeded) - colH);
      settingsSystemScroll_ = std::clamp(settingsSystemScroll_, 0, settingsSystemScrollMax_);
      settingsSystemViewport_ = content;
      bool systemScrolls = settingsSystemScrollMax_ > 0;
      int colTop = cy - settingsSystemScroll_;

      SDL_Rect leftCol {cx, colTop, leftW, colH};
      SDL_Rect rightCol {cx + leftW + colGap, colTop, rightW, colH};

      // Keep cards inside the content frame while scrolled.
      SDL_Rect previousSettingsClip {};
      bool hadSettingsClip = SDL_RenderClipEnabled(controlRenderer_) == true;
      if (hadSettingsClip) {
        SDL_GetRenderClipRect(controlRenderer_, &previousSettingsClip);
      }
      SDL_SetRenderClipRect(controlRenderer_, &content);
      const std::size_t systemButtonStart = settingsBtns_.size();

      int leftY = leftCol.y;
      SDL_Rect appearanceRect {leftCol.x, leftY, leftCol.w, appearanceH};
      leftY += appearanceRect.h + kCardGap;
      SDL_Rect safetyRect {leftCol.x, leftY, leftCol.w, safetyH};
      leftY += safetyRect.h + kCardGap;
      SDL_Rect flowRect {leftCol.x, leftY, leftCol.w,
                         systemScrolls ? flowH
                                       : std::max(flowH, leftCol.y + leftCol.h - leftY)};

      int rightY = rightCol.y;
      SDL_Rect cueToolsRect {rightCol.x, rightY, rightCol.w, cueToolsH};
      rightY += cueToolsRect.h + kCardGap;
      SDL_Rect prefsRect {rightCol.x, rightY, rightCol.w,
                          systemScrolls ? prefsH
                                        : std::max(prefsH, rightCol.y + rightCol.h - rightY)};

      drawCard(appearanceRect, "APPEARANCE", "Theme and operator feedback");
      std::string themeName = currentThemeName_.empty() ? "gameboy" : currentThemeName_;
      int appY = cardBodyY(appearanceRect);
      const int appX = cardBodyX(appearanceRect);
      const int appW = cardBodyW(appearanceRect);
      SDL_Rect themeBtn {appX, appY, appW, sTallH};
      appY += sTallH + sGap;
      drawUIDropdown(themeBtn, "Theme", themeName, "settings.theme");
      settingsBtns_.push_back({themeBtn, kSettingsActionThemeDropdown, "theme"});
      SDL_Rect sfxBtn {appX, appY, appW, sRowH};
      appY += sRowH + sGap;
      drawPillToggle(sfxBtn, project_.uiSoundsEnabled, "SFX ON", "SFX OFF");
      settingsBtns_.push_back({sfxBtn, 201, "sfx_toggle"});
      // Mascot dropdown: deckbot (default) or deckgirl. Picks the splash
      // character; refreshSplashAsset re-resolves the art on change.
      SDL_Rect mascotBtn {appX, appY, appW, sRowH};
      appY += sRowH + sGap;
      std::string mascotLabel =
        (project_.splashCharacter == "deckgirl") ? "Deckgirl" : "Deckbot";
      drawUIDropdown(mascotBtn, "Mascot", mascotLabel, "settings.mascot");
      settingsBtns_.push_back({mascotBtn, kSettingsActionMascotToggle, "mascot_toggle"});
      // UI scale dropdown — multiplies every font point size at load, and (as
      // of v0.81.0) the settings chrome scales with it too.
      SDL_Rect scaleBtn {appX, appY, appW, sTallH};
      appY += sTallH + sGap;
      char scaleLabel[16];
      snprintf(scaleLabel, sizeof(scaleLabel), "%.2fx", project_.uiScale);
      drawUIDropdown(scaleBtn, "UI Scale", scaleLabel, "settings.ui_scale");
      settingsBtns_.push_back({scaleBtn, kSettingsActionUiScaleDropdown, "ui_scale"});
      // Pocket 3 preset — one-click ergonomic bundle for the GPD Pocket 3
      // and other small high-DPI handhelds. Pushes uiScale to 2.0 and
      // refreshes fonts. Tap to apply; tap again to revert to 1.0.
      SDL_Rect pocketBtn {appX, appY, appW, sRowH};
      bool pocketActive = std::abs(project_.uiScale - 2.0) < 0.01 &&
                          project_.interactionMode == "touch";
      drawPillToggle(pocketBtn, pocketActive, "POCKET 3 / TOUCH", "POCKET 3 / TOUCH");
      settingsBtns_.push_back({pocketBtn, kSettingsActionPocket3Preset, "pocket3_preset"});

      drawCard(safetyRect, "SAFETY / TIMECODE", "Emergency fade and sync behavior");
      const int safetyX = cardBodyX(safetyRect);
      const int stepW = uiScaled(26);
      const int stepValW = uiScaled(72);
      const int panicLabelY = cardBodyY(safetyRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{safetyX, panicLabelY, cardBodyW(safetyRect), sLineH}, "panic fade", soft);
      const int panicRowY = panicLabelY + sLineH + uiScaled(4);
      SDL_Rect panicFadeDecBtn {safetyX, panicRowY, stepW, sChipH};
      SDL_Rect panicFadeValRect {panicFadeDecBtn.x + stepW + uiScaled(4), panicFadeDecBtn.y, stepValW, sChipH};
      SDL_Rect panicFadeIncBtn {panicFadeValRect.x + panicFadeValRect.w + uiScaled(4), panicFadeDecBtn.y, stepW, sChipH};
      Primitives::drawFramedPanel(controlRenderer_, panicFadeDecBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "-", ink, panicFadeDecBtn);
      settingsBtns_.push_back({panicFadeDecBtn, 208, "panic_fade_dec"});
      char panicFadeBuf[32];
      snprintf(panicFadeBuf, sizeof(panicFadeBuf), "%.1fs", project_.panicFadeSeconds);
      Primitives::drawFramedPanel(controlRenderer_, panicFadeValRect, pal.shellInner,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, panicFadeBuf, ink, panicFadeValRect);
      Primitives::drawFramedPanel(controlRenderer_, panicFadeIncBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "+", ink, panicFadeIncBtn);
      settingsBtns_.push_back({panicFadeIncBtn, 209, "panic_fade_inc"});
      const int panicRestoreX = panicFadeIncBtn.x + panicFadeIncBtn.w + sPad;
      SDL_Rect panicRestoreBtn {panicRestoreX, panicFadeDecBtn.y,
                                std::max(uiScaled(40), safetyRect.x + safetyRect.w - sPad - panicRestoreX),
                                sChipH};
      drawPillToggle(panicRestoreBtn, project_.panicAutoRestore, "AUTO RESTORE ON", "AUTO RESTORE OFF");
      settingsBtns_.push_back({panicRestoreBtn, 212, "panic_restore_toggle"});

      const int tcJamY = panicRowY + sChipH + sGap;
      SDL_Rect tcJamBtn {safetyX, tcJamY, uiScaled(140), sChipH};
      SDL_Rect tcFwDecBtn {tcJamBtn.x + tcJamBtn.w + sPad, tcJamBtn.y, stepW, sChipH};
      SDL_Rect tcFwValRect {tcFwDecBtn.x + stepW + uiScaled(4), tcFwDecBtn.y, stepValW, sChipH};
      SDL_Rect tcFwIncBtn {tcFwValRect.x + tcFwValRect.w + uiScaled(4), tcFwDecBtn.y, stepW, sChipH};
      drawPillToggle(tcJamBtn, tcDeck.timecodeJamSyncEnabled, "TC JAM ON", "TC JAM OFF");
      settingsBtns_.push_back({tcJamBtn, 213, "tc_jam_toggle"});
      Primitives::drawFramedPanel(controlRenderer_, tcFwDecBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "-", ink, tcFwDecBtn);
      settingsBtns_.push_back({tcFwDecBtn, 214, "tc_freewheel_dec"});
      char tcFwBuf[32];
      snprintf(tcFwBuf, sizeof(tcFwBuf), "%.1fs", tcDeck.timecodeFreewheelSeconds);
      Primitives::drawFramedPanel(controlRenderer_, tcFwValRect, pal.shellInner,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, tcFwBuf, ink, tcFwValRect);
      Primitives::drawFramedPanel(controlRenderer_, tcFwIncBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "+", ink, tcFwIncBtn);
      settingsBtns_.push_back({tcFwIncBtn, 215, "tc_freewheel_inc"});

      drawCard(flowRect, "SHOW FLOW", "Global jump and panic behavior");
      const int flowX = cardBodyX(flowRect);
      const int flowW = cardBodyW(flowRect);
      SDL_Rect jumpModeBtn {flowX, cardBodyY(flowRect), uiScaled(150), sRowH};
      SDL_Rect jumpTransBtn {jumpModeBtn.x + jumpModeBtn.w + sPad, jumpModeBtn.y,
                             std::max(uiScaled(40), flowW - jumpModeBtn.w - sPad), sRowH};
      Primitives::drawFramedPanel(controlRenderer_, jumpModeBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, jumpModeLabelFromToken(project_.jumpMode), ink, jumpModeBtn);
      settingsBtns_.push_back({jumpModeBtn, 203, "jump_mode"});
      drawPillToggle(jumpTransBtn, project_.jumpTransitionEnabled, "GLOBAL XFADE ON", "GLOBAL XFADE OFF");
      settingsBtns_.push_back({jumpTransBtn, 204, "jump_transition"});

      const int profileLabelY = jumpModeBtn.y + sRowH + sGap;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{flowX, profileLabelY, flowW, sLineH}, "panic profile", soft);
      const int profileRowY = profileLabelY + sLineH + uiScaled(4);
      SDL_Rect panicPrevBtn {flowX, profileRowY, stepW, sRowH};
      SDL_Rect panicNextBtn {flowRect.x + flowRect.w - sPad - stepW, profileRowY, stepW, sRowH};
      SDL_Rect panicLabelRect {panicPrevBtn.x + panicPrevBtn.w + sGap, panicPrevBtn.y,
                               std::max(uiScaled(40),
                                        panicNextBtn.x - sGap - (panicPrevBtn.x + panicPrevBtn.w + sGap)),
                               sRowH};
      Primitives::drawFramedPanel(controlRenderer_, panicPrevBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "<", ink, panicPrevBtn);
      settingsBtns_.push_back({panicPrevBtn, 205, "panic_profile_prev"});
      Primitives::drawFramedPanel(controlRenderer_, panicLabelRect, pal.shellInner,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, panicProfileLabelFromToken(project_.panicProfile), ink, panicLabelRect);
      Primitives::drawFramedPanel(controlRenderer_, panicNextBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, ">", ink, panicNextBtn);
      settingsBtns_.push_back({panicNextBtn, 206, "panic_profile_next"});

      const int panicRunY = profileRowY + sRowH + sGap;
      SDL_Rect panicRunBtn {flowX, panicRunY, flowW, sRowH};
      Primitives::drawFramedPanel(controlRenderer_, panicRunBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Run Panic", ink, panicRunBtn);
      settingsBtns_.push_back({panicRunBtn, 207, "panic_run"});

      drawCard(cueToolsRect, "CUE TOOLS", "Find from the playlist, not from a modal");
      const int cueX = cardBodyX(cueToolsRect);
      const int cueW = cardBodyW(cueToolsRect);
      const int cueToolsLine1Y = cardBodyY(cueToolsRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{cueX, cueToolsLine1Y, cueW, sLineH},
                   "Use Ctrl+F or type a cue id/number to search.", soft);
      const int cueToolsLine2Y = cueToolsLine1Y + sLineH;
      std::string findStatus = "find: none";
      if (!lastCueFindToken_.empty() && !lastCueFindMatches_.empty()) {
        int cursor = std::clamp(lastCueFindCursor_, 0, static_cast<int>(lastCueFindMatches_.size()) - 1);
        findStatus = "find \"" + lastCueFindToken_ + "\" " + std::to_string(cursor + 1) + "/" + std::to_string(lastCueFindMatches_.size());
      }
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{cueX, cueToolsLine2Y, cueW, sLineH},
                   findStatus, soft);
      const int renumberY = cueToolsLine2Y + sLineH + sGap;
      SDL_Rect renumberBtn {cueX, renumberY, cueW, sRowH};
      Primitives::drawFramedPanel(controlRenderer_, renumberBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Renumber...", ink, renumberBtn);
      settingsBtns_.push_back({renumberBtn, 221, "cue_renumber"});

      drawCard(prefsRect, "PLAYLIST PREFERENCES", "Defaults for newly created cues");
      const Deck& prefDeck = focusedDeck();
      const int prefsX = cardBodyX(prefsRect);
      const int prefsW = cardBodyW(prefsRect);
      SDL_Rect prefsEditBtn {prefsX, cardBodyY(prefsRect), prefsW, sRowH};
      Primitives::drawFramedPanel(controlRenderer_, prefsEditBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Edit Timebase / Start / Fade / Duration...", ink, prefsEditBtn);
      settingsBtns_.push_back({prefsEditBtn, kSettingsActionPlaylistPrefsEdit, "playlist_prefs_edit"});

      std::string prefSummary = "tc " + playlistTimebaseLabel(prefDeck.playlistTimebaseFps)
        + "  start " + formatTimecode(prefDeck.playlistStartOffsetSeconds, prefDeck.playlistTimebaseFps)
        + "  fade " + formatSeconds(prefDeck.playlistDefaultCueFadeSeconds)
        + "  still " + formatSeconds(prefDeck.playlistDefaultStillDurationSeconds);
      const int prefSummaryY = prefsEditBtn.y + prefsEditBtn.h + sGap;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{prefsX, prefSummaryY, prefsW, sLineH},
                   prefSummary, soft);

      int toggleGap = sGap;
      int toggleW = std::max(uiScaled(120), (prefsW - toggleGap) / 2);
      int toggleH = sRowH;
      int toggleY = prefSummaryY + sLineH + sGap;
      SDL_Rect loopT {prefsX, toggleY, toggleW, toggleH};
      SDL_Rect fadeInT {loopT.x + toggleW + toggleGap, toggleY,
                        std::max(uiScaled(40), prefsRect.x + prefsRect.w - sPad - (loopT.x + toggleW + toggleGap)),
                        toggleH};
      drawPillToggle(loopT, prefDeck.playlistDefaultLoop, "LOOP ON", "LOOP OFF");
      drawPillToggle(fadeInT, prefDeck.playlistDefaultFadeInEnabled, "FADE IN ON", "FADE IN OFF");
      settingsBtns_.push_back({loopT, kSettingsActionPlaylistDefaultLoopToggle, "playlist_default_loop"});
      settingsBtns_.push_back({fadeInT, kSettingsActionPlaylistDefaultFadeInToggle, "playlist_default_fadein"});

      int toggleY2 = toggleY + toggleH + uiScaled(4);
      SDL_Rect fadeOutT {prefsX, toggleY2, toggleW, toggleH};
      SDL_Rect audioT {fadeOutT.x + toggleW + toggleGap, toggleY2,
                       std::max(uiScaled(40), prefsRect.x + prefsRect.w - sPad - (fadeOutT.x + toggleW + toggleGap)),
                       toggleH};
      drawPillToggle(fadeOutT, prefDeck.playlistDefaultFadeOutEnabled, "FADE OUT ON", "FADE OUT OFF");
      drawPillToggle(audioT, prefDeck.playlistDefaultAudioEnabled, "AUDIO ON", "AUDIO OFF");
      settingsBtns_.push_back({fadeOutT, kSettingsActionPlaylistDefaultFadeOutToggle, "playlist_default_fadeout"});
      settingsBtns_.push_back({audioT, kSettingsActionPlaylistDefaultAudioToggle, "playlist_default_audio"});

      int toggleY3 = toggleY2 + toggleH + uiScaled(4);
      SDL_Rect pauseBeginT {prefsX, toggleY3, toggleW, toggleH};
      SDL_Rect pauseEndT {pauseBeginT.x + toggleW + toggleGap, toggleY3,
                          std::max(uiScaled(40), prefsRect.x + prefsRect.w - sPad - (pauseBeginT.x + toggleW + toggleGap)),
                          toggleH};
      drawPillToggle(pauseBeginT, prefDeck.playlistDefaultPauseAtBeginning, "PAUSE BEGIN ON", "PAUSE BEGIN OFF");
      drawPillToggle(pauseEndT, prefDeck.playlistDefaultPauseAtEnd, "PAUSE END ON", "PAUSE END OFF");
      settingsBtns_.push_back({pauseBeginT, kSettingsActionPlaylistDefaultPauseBeginToggle, "playlist_default_pausebegin"});
      settingsBtns_.push_back({pauseEndT, kSettingsActionPlaylistDefaultPauseEndToggle, "playlist_default_pauseend"});

      int toggleY4 = toggleY3 + toggleH + uiScaled(4);
      SDL_Rect nextTransT {prefsX, toggleY4, prefsW, toggleH};
      drawPillToggle(nextTransT, prefDeck.playlistDefaultTransitionToNext, "NEXT TRANSITION ON", "NEXT TRANSITION OFF");
      settingsBtns_.push_back({nextTransT, kSettingsActionPlaylistDefaultNextTransitionToggle, "playlist_default_nexttrans"});

      SDL_SetRenderClipRect(controlRenderer_, hadSettingsClip ? &previousSettingsClip : nullptr);
      // Scrolled-away controls are painted outside the viewport by the clip, so
      // drop their hit rects too — otherwise an off-screen toggle keeps eating
      // clicks that land over whatever is actually visible there.
      if (systemScrolls) {
        settingsBtns_.erase(
          std::remove_if(settingsBtns_.begin() + static_cast<std::ptrdiff_t>(systemButtonStart),
                         settingsBtns_.end(),
                         [&](const SettingsButton& b) {
                           SDL_Rect clipped {};
                           return !SDL_GetRectIntersection(&b.rect, &content, &clipped);
                         }),
          settingsBtns_.end());
      }
      if (settingsSystemScrollMax_ > 0) {
        drawSettingsScrollHint(content, settingsSystemScroll_, settingsSystemScrollMax_);
      }

    } else if (settingsTab_ == 1) {
      auto drawCard = [&](const SDL_Rect& rect, const std::string& title, const std::string& subtitle = std::string()) {
        drawSettingsCard(rect, title, subtitle);
      };
      auto drawPillToggle = [&](const SDL_Rect& rect, bool on, const std::string& onLabel, const std::string& offLabel) {
        Primitives::drawFramedPanel(controlRenderer_, rect,
                        on ? pal.dark : pal.mid,
                        pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, on ? onLabel : offLabel,
                         on ? pal.light : pal.deep, rect);
      };

      int audioH = sCardHeaderH + sTallH + sGap + sRowH + sGap + sLineH + sPad;
      SDL_Rect audioRect {cx, cy, content.w - uiScaled(24), audioH};
      int midiTop = audioRect.y + audioRect.h + uiScaled(10);
      SDL_Rect midiRect {cx, midiTop, content.w - uiScaled(24),
                         std::max(uiScaled(180), content.y + content.h - uiScaled(10) - midiTop)};

      drawCard(audioRect, "AUDIO OUTPUT", "Device routing for cue playback");
      const int smallLineH = sLineH;
      const int audioX = cardBodyX(audioRect);
      std::string devName = focusedDeck().audioOutputDeviceName.empty() ? "(default system device)" : focusedDeck().audioOutputDeviceName;
      SDL_Rect devBtn {audioX, cardBodyY(audioRect), cardBodyW(audioRect), sTallH};
      drawUIDropdown(devBtn, "Device", devName, "settings.audio_device");
      settingsBtns_.push_back({devBtn, 200, "audio_device"});
      // Audio buffer size cycle button
      int bufSamples = project_.audioBufferSamples;
      std::string bufLabel = "Buffer: " + std::to_string(bufSamples) + " smp";
      const int bufBtnY = devBtn.y + devBtn.h + sGap;
      SDL_Rect bufBtn {audioX, bufBtnY, uiScaled(160), sRowH};
      Primitives::drawFramedPanel(controlRenderer_, bufBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, bufLabel, ink, bufBtn);
      settingsBtns_.push_back({bufBtn, kSettingsActionAudioBufferCycle, "audio_buffer_samples"});
      // Chain A/V offset: -/+ buttons around the current delay readout.
      SDL_Rect delayDecBtn {bufBtn.x + bufBtn.w + uiScaled(12), bufBtn.y, uiScaled(24), sRowH};
      SDL_Rect delayLabel {delayDecBtn.x + delayDecBtn.w + uiScaled(4), bufBtn.y, uiScaled(120), sRowH};
      SDL_Rect delayIncBtn {delayLabel.x + delayLabel.w + uiScaled(4), bufBtn.y, uiScaled(24), sRowH};
      Primitives::drawFramedPanel(controlRenderer_, delayDecBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "-", ink, delayDecBtn);
      Primitives::drawFramedPanel(controlRenderer_, delayLabel, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_,
                       "A/V delay: " + std::to_string(project_.audioDelayMs) + " ms",
                       ink, delayLabel);
      Primitives::drawFramedPanel(controlRenderer_, delayIncBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "+", ink, delayIncBtn);
      settingsBtns_.push_back({delayDecBtn, kSettingsActionAudioDelayDec, "audio_delay_dec"});
      settingsBtns_.push_back({delayIncBtn, kSettingsActionAudioDelayInc, "audio_delay_inc"});
      // Device channel count (2/4/6/8) — reopens the deck device; cues then
      // route their stereo onto a pair of these outs (AUDIO section: "outs").
      SDL_Rect chanBtn {delayIncBtn.x + delayIncBtn.w + uiScaled(12), bufBtn.y, uiScaled(110), sRowH};
      Primitives::drawFramedPanel(controlRenderer_, chanBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_,
                       "Outs: " + std::to_string(focusedDeck().audioOutputChannels) + " ch",
                       ink, chanBtn);
      settingsBtns_.push_back({chanBtn, kSettingsActionAudioChannelsCycle, "audio_channels"});
      const int audioFootY = bufBtn.y + bufBtn.h + sGap;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{audioX, audioFootY, cardBodyW(audioRect), sLineH},
                   "Smaller buffer = lower latency. A/V delay holds audio back for lagging displays — dial in with the Pocket Test beacon.", soft);

      drawCard(midiRect, "MIDI CONTROL", "Optional external transport and cue control");
      const int midiX = cardBodyX(midiRect);
      SDL_Rect midiEnBtn {midiX, cardBodyY(midiRect), uiScaled(120), sRowH};
      drawPillToggle(midiEnBtn, midiEnabled_, "MIDI ON", "MIDI OFF");
      settingsBtns_.push_back({midiEnBtn, 210, "midi_toggle"});
      SDL_Rect midiPortBtn {midiEnBtn.x + midiEnBtn.w + sPad, midiEnBtn.y,
                            std::max(uiScaled(40),
                                     midiRect.x + midiRect.w - sPad - (midiEnBtn.x + midiEnBtn.w + sPad)),
                            sRowH};
      Primitives::drawFramedPanel(controlRenderer_, midiPortBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_,
                       midiDeviceName_.empty() ? "Set MIDI Port..." : ellipsizeToPixelWidth(fontSmall_, midiDeviceName_, midiPortBtn.w - 12),
                       ink, midiPortBtn);
      settingsBtns_.push_back({midiPortBtn, 211, "midi_port"});

      int mapY = midiEnBtn.y + midiEnBtn.h + sGap;
      int midiTextW = cardBodyW(midiRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{midiX, mapY, midiTextW, sLineH}, "Mappings", pal.fg);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{midiX, mapY + smallLineH, midiTextW, sLineH},
                   "Note 0-127 -> trigger cue index in the focused playlist", soft);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{midiX, mapY + smallLineH * 2, midiTextW, sLineH},
                   "CC 7 -> master volume   |   CC 20 -> playback speed", soft);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{midiX, mapY + smallLineH * 3, midiTextW, sLineH},
                   "MMC Play / Stop / Goto -> transport   |   MSC Trigger -> cue by id", soft);

    } else if (settingsTab_ == 2) {
      auto drawCard = [&](const SDL_Rect& rect, const std::string& title, const std::string& subtitle = std::string()) {
        drawSettingsCard(rect, title, subtitle);
      };
      auto drawPill = [&](const SDL_Rect& rect, bool active, const std::string& onLabel, const std::string& offLabel, int action) {
        Primitives::drawFramedPanel(controlRenderer_, rect,
                        active ? pal.dark : pal.mid,
                        pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, active ? onLabel : offLabel,
                         active ? pal.light : pal.deep, rect);
        settingsBtns_.push_back({rect, action, onLabel});
      };

      int colGap = uiScaled(12);
      int leftW = std::max(uiScaled(320), (content.w - uiScaled(12) - colGap) / 2);
      int rightW = std::max(uiScaled(320), content.w - uiScaled(12) - colGap - leftW);
      SDL_Rect leftCol {cx, cy, leftW, content.h - uiScaled(20)};
      SDL_Rect rightCol {cx + leftW + colGap, cy, rightW, content.h - uiScaled(20)};
      const int kCardGap = uiScaled(10);

      int leftY = leftCol.y;
      int remoteH = sCardHeaderH + sLineH + uiScaled(2) + sRowH + sGap + sChipH + sPad;
      SDL_Rect remoteRect {leftCol.x, leftY, leftCol.w, remoteH};
      leftY += remoteRect.h + kCardGap;
      int oscH = sCardHeaderH + sLineH + uiScaled(4) + sChipH * 2 + sGap + sPad;
      SDL_Rect oscRect {leftCol.x, leftY, leftCol.w, oscH};
      leftY += oscRect.h + kCardGap;
      int notesH = sCardHeaderH + sLineH * 3 + sPad;
      SDL_Rect notesRect {leftCol.x, leftY, leftCol.w,
                          std::max(notesH, leftCol.y + leftCol.h - leftY)};
      SDL_Rect integrationRect {rightCol.x, rightCol.y, rightCol.w, rightCol.h};

      const int netLineH = sLineH;

      drawCard(remoteRect, "REMOTE CONTROL", "Companion / OSC ingress and HyperDeck emulation");
      const int remoteX = cardBodyX(remoteRect);
      const int remoteW = cardBodyW(remoteRect);
      const int remoteLabelY = cardBodyY(remoteRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{remoteX, remoteLabelY, remoteW, sLineH},
                   "Companion / OSC port", soft);
      const int remotePortY = remoteLabelY + sLineH + uiScaled(2);
      SDL_Rect portBtn {remoteX, remotePortY, uiScaled(176), sRowH};
      Primitives::drawFramedPanel(controlRenderer_, portBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Port " + std::to_string(companionPort_), ink, portBtn);
      settingsBtns_.push_back({portBtn, 220, "osc_port"});
      {
        // Note text sits beside the control and shares its vertical centre —
        // pass the control's own height so the helper centres both identically.
        int noteX = portBtn.x + portBtn.w + sPad;
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect{noteX, portBtn.y,
                              std::max(0, remoteRect.x + remoteRect.w - sPad - noteX), portBtn.h},
                     "HyperDeck emulation stays on at TCP 9992.", soft);
      }
      const int remoteToggleY = portBtn.y + portBtn.h + sGap;
      SDL_Rect remoteToggle {remoteX, remoteToggleY, uiScaled(176), sChipH};
      drawPill(remoteToggle, project_.allowRemoteNetwork, "REMOTE ON", "LOCAL ONLY", kSettingsActionAllowRemoteToggle);
      {
        int noteX = remoteToggle.x + remoteToggle.w + sPad;
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect{noteX, remoteToggle.y,
                              std::max(0, remoteRect.x + remoteRect.w - sPad - noteX), remoteToggle.h},
                     project_.allowRemoteNetwork ? "Listening on all interfaces"
                                                 : "Listening on localhost (127.0.0.1)", soft);
      }

      drawCard(oscRect, "OSC QUERY / FEEDBACK", "Discovery and mirrored state");
      std::string queryStatus = project_.oscQueryEnabled ? (oscQueryReady_ ? "running" : "error") : "off";
      const int oscX = cardBodyX(oscRect);
      const int oscStatusY = cardBodyY(oscRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{oscX, oscStatusY, cardBodyW(oscRect), sLineH},
                   "query status: " + queryStatus + "  http " + std::to_string(project_.oscQueryPort), soft);
      const int oscQueryRowY = oscStatusY + sLineH + uiScaled(4);
      SDL_Rect queryToggle {oscX, oscQueryRowY, uiScaled(144), sChipH};
      SDL_Rect queryPortBtn {queryToggle.x + queryToggle.w + sPad, queryToggle.y, uiScaled(164), sChipH};
      drawPill(queryToggle, project_.oscQueryEnabled, "QUERY ON", "QUERY OFF", kSettingsActionOscQueryToggle);
      Primitives::drawFramedPanel(controlRenderer_, queryPortBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Set HTTP Port...", ink, queryPortBtn);
      settingsBtns_.push_back({queryPortBtn, kSettingsActionOscQueryPortPrompt, "osc_query_port"});
      const int oscFbRowY = queryToggle.y + queryToggle.h + sGap;
      SDL_Rect fbToggle {oscX, oscFbRowY, uiScaled(156), sChipH};
      SDL_Rect fbRateBtn {fbToggle.x + fbToggle.w + sPad, fbToggle.y, uiScaled(164), sChipH};
      drawPill(fbToggle, project_.oscFeedbackMirrorEnabled, "MIRROR ON", "MIRROR OFF", kSettingsActionOscFeedbackMirrorToggle);
      Primitives::drawFramedPanel(controlRenderer_, fbRateBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_,
                       std::to_string(project_.oscFeedbackRateMs) + " ms",
                       ink, fbRateBtn);
      settingsBtns_.push_back({fbRateBtn, kSettingsActionOscFeedbackRatePrompt, "osc_feedback_rate"});

      drawCard(notesRect, "DISCOVERY / NOTES", "Network-facing runtime notes");
      const int notesX = cardBodyX(notesRect);
      const int notesLineY = cardBodyY(notesRect);
      int notesTextW = cardBodyW(notesRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{notesX, notesLineY, notesTextW, sLineH},
                   "OSC subscribe: send /deckboy/subscribe from your controller.", soft);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{notesX, notesLineY + netLineH, notesTextW, sLineH},
                   "NDI transport is configured per output in Video Outputs.", soft);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{notesX, notesLineY + netLineH * 2, notesTextW, sLineH},
                   "NMC mode / host / port are runtime-configurable for sync work.", soft);

      drawCard(integrationRect, "INTEGRATION ADAPTERS", "ATEM, NDI trigger, NMC, MTC, LTC, Art-Net, and Tally/TSL");
      IntegrationBackendRuntimeRoute integrationRoute = resolveIntegrationBackendRuntimeRoute();
      const int integX = cardBodyX(integrationRect);
      const int integrationLineY = cardBodyY(integrationRect);
      int integTextW = cardBodyW(integrationRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{integX, integrationLineY, integTextW, sLineH},
                   integrationRoute.summary, soft);
      int atemBridgePortDisplay = atemBridgeListenPort_;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{integX, integrationLineY + netLineH, integTextW, sLineH},
                   "atem " + std::to_string(atemBridgePortDisplay) + "  artnet " + std::to_string(project_.artNetPort), soft);

      int pillGap = sPad;
      int pillW = std::max(uiScaled(120), (integTextW - pillGap) / 2);
      int pillH = sRowH;
      int pillX1 = integX;
      int pillX2 = pillX1 + pillW + pillGap;
      int pillY = integrationLineY + netLineH * 2 + sGap;
      SDL_Rect atemBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect ndiTrigBtn {pillX2, pillY, pillW, pillH};
      pillY += pillH + sGap;
      SDL_Rect nmcBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect mtcBtn {pillX2, pillY, pillW, pillH};
      pillY += pillH + sGap;
      SDL_Rect ltcBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect artNetBtn {pillX2, pillY, pillW, pillH};
      drawPill(atemBtn, project_.atemTriggerEnabled, "ATEM ON", "ATEM OFF", kSettingsActionIntegrationAtemToggle);
      drawPill(ndiTrigBtn, project_.ndiTriggerEnabled, "NDI TRIGGER ON", "NDI TRIGGER OFF", kSettingsActionIntegrationNdiTriggerToggle);
      drawPill(nmcBtn, project_.nmcSyncEnabled, "NMC ON", "NMC OFF", kSettingsActionIntegrationNmcToggle);
      drawPill(mtcBtn, project_.mtcIngestEnabled, "MTC ON", "MTC OFF", kSettingsActionIntegrationMtcToggle);
      drawPill(ltcBtn, project_.ltcIngestEnabled, "LTC ON", "LTC OFF", kSettingsActionIntegrationLtcToggle);
      drawPill(artNetBtn, project_.dmxArtNetEnabled, "ARTNET ON", "ARTNET OFF", kSettingsActionIntegrationArtNetToggle);
      pillY += pillH + sGap;
      SDL_Rect tslBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect tcChaseBtn {pillX2, pillY, pillW, pillH};
      pillY += pillH + sGap;
      SDL_Rect tcRunBtn {pillX1, pillY, pillW, pillH};
      drawPill(tslBtn, project_.tslTallyEnabled, "TALLY ON", "TALLY OFF", kSettingsActionIntegrationTslToggle);
      drawPill(tcChaseBtn, focusedDeck().timecodeChaseEnabled, "TC CHASE ON", "TC CHASE OFF", kSettingsActionIntegrationTimecodeChaseToggle);
      drawPill(tcRunBtn, focusedDeck().timecodeRunEnabled, "TC RUN ON", "TC RUN OFF", kSettingsActionIntegrationTimecodeRunToggle);

      // Two footer rows pinned to the bottom of the card.
      const int integFooterRowH = sRowH;
      int footerY = integrationRect.y + integrationRect.h - sPad - integFooterRowH * 2 - sGap;
      SDL_Rect tslPortBtn {integX, footerY, uiScaled(148), integFooterRowH};
      Primitives::drawFramedPanel(controlRenderer_, tslPortBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Tally :" + std::to_string(project_.tslTallyPort), ink, tslPortBtn);
      settingsBtns_.push_back({tslPortBtn, kSettingsActionIntegrationTslPortPrompt, "integration_tsl_port"});
      SDL_Rect tslAddrBtn {tslPortBtn.x + tslPortBtn.w + sPad, footerY,
                           std::max(uiScaled(40),
                                    integrationRect.x + integrationRect.w - sPad
                                      - (tslPortBtn.x + tslPortBtn.w + sPad)),
                           integFooterRowH};
      Primitives::drawFramedPanel(controlRenderer_, tslAddrBtn, pal.mid, pal.deep, pal.light);
      std::string tslAddrLabel = project_.tslTallyAddress.empty() ? "255.255.255.255" : project_.tslTallyAddress;
      drawCenteredText(controlRenderer_, fontSmall_, tslAddrLabel, ink, tslAddrBtn);
      settingsBtns_.push_back({tslAddrBtn, kSettingsActionIntegrationTslAddrPrompt, "integration_tsl_address"});

      SDL_Rect artNetPortBtn {integX, footerY + integFooterRowH + sGap,
                              uiScaled(148), integFooterRowH};
      Primitives::drawFramedPanel(controlRenderer_, artNetPortBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Art-Net " + std::to_string(project_.artNetPort), ink, artNetPortBtn);
      settingsBtns_.push_back({artNetPortBtn, kSettingsActionIntegrationArtNetPortPrompt, "integration_artnet_port"});
      bool allAdaptersEnabled = project_.atemTriggerEnabled && project_.ndiTriggerEnabled &&
                                project_.nmcSyncEnabled && project_.mtcIngestEnabled &&
                                project_.ltcIngestEnabled && project_.dmxArtNetEnabled;
      SDL_Rect allToggleBtn {artNetPortBtn.x + artNetPortBtn.w + sPad, artNetPortBtn.y,
                             std::max(uiScaled(40),
                                      integrationRect.x + integrationRect.w - sPad
                                        - (artNetPortBtn.x + artNetPortBtn.w + sPad)),
                             integFooterRowH};
      drawPill(allToggleBtn, allAdaptersEnabled, "ALL ON", "ALL OFF", kSettingsActionIntegrationAllToggle);

    } else if (settingsTab_ == 3) {
      // Video Outputs tab (simplified)
      const OutputTarget& outputTarget = focusedOutput();
      int focusedOutputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
      auto [nativeW, nativeH] = displayNativeRenderSize(outputDisplayIndex(project_.focusedOutputIndex));
      auto [targetW, targetH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
      std::string outputTypeLabel = normalizeOutputType(outputTarget.outputType);
      std::string mirrorLabel = outputTarget.mirrorSourceOutputIndex >= 0
        ? outputLabel(outputTarget.mirrorSourceOutputIndex)
        : "off";
      std::string streamProtocol = normalizeOutputStreamProtocol(outputTarget.streamProtocol);
      std::string streamUrl = trim(outputTarget.streamUrl);
      if (streamUrl.empty()) {
        streamUrl = defaultOutputStreamUrl(streamProtocol, focusedOutputIndex);
      }
      int outputAlphaPct = static_cast<int>(std::lround(std::clamp(outputTarget.outputAlpha, 0.0f, 1.0f) * 100.0f));
      int outputDelayMs = std::clamp(outputTarget.outputDelayMs, 0, 5000);
      std::string outputColorSpace = normalizeOutputColorSpace(outputTarget.outputColorSpace);
      std::string outputLayoutMode = normalizeOutputLayoutMode(outputTarget.outputLayoutMode);
      int outputOrientation = normalizeOutputOrientationDegrees(outputTarget.outputOrientationDegrees);
      std::string ndiSource = trim(outputTarget.ndiSourceName).empty()
        ? defaultOutputNdiSourceName(outputTarget, focusedOutputIndex)
        : outputTarget.ndiSourceName;
      bool anyOutputTestCardsOn = false;
      bool anyOutputTestCardsOff = false;
      for (const auto& out : project_.outputs) {
        if (out.outputTestCardEnabled) {
          anyOutputTestCardsOn = true;
        } else {
          anyOutputTestCardsOff = true;
        }
      }
      auto drawActionBtn = [&](const SDL_Rect& rect, const std::string& label, int action, bool active = false) {
        SDL_Color fill = active ? pal.dark : pal.mid;
        SDL_Color txt = active ? pal.light : ink;
        Primitives::drawFramedPanel(controlRenderer_, rect, fill, pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, label, txt, rect);
        settingsBtns_.push_back({rect, action, label});
      };

      // ═══════════════════════════════════════════════════════════════
      // VIDEO OUTPUTS — Sub-tabbed layout
      // ═══════════════════════════════════════════════════════════════

      int availW = content.w - uiScaled(24);
      const int kRowH = sTallH;
      constexpr int kLabelH = 16;
      const int kRowGap = uiScaled(5);
      constexpr int kSectionGap = 14;

      // Section frame helper — same cartridge label-plate treatment as
      // drawSettingsCard; body rect is unchanged so section content is
      // untouched.
      // Shares the plate contract with drawSettingsCard so a section header on
      // this tab sits exactly where a card header sits on the System tab.
      auto drawSectionFrame = [&](const SDL_Rect& rect, const std::string& title) {
        Primitives::drawFramedPanel(controlRenderer_, rect, pal.shellInner, pal.deep, pal.light);
        SDL_Rect hdr = drawSettingsPlate(rect, title, fontSmall_);
        int bodyTop = hdr.y + hdr.h + 6;
        return SDL_Rect {rect.x + 8, bodyTop, rect.w - 16,
                         std::max(0, rect.y + rect.h - bodyTop - 8)};
      };
      auto sectionHeaderRect = [&](const SDL_Rect& rect) {
        return settingsPlateRect(rect, fontSmall_);
      };

      // ─── Header: Output Selector + Enable Toggle ───
      int headerBtnW = std::min(uiScaled(260), (availW - uiScaled(12)) / 2);
      SDL_Rect outSelectRect {cx, cy, headerBtnW, std::max(sTallH, uiScaled(32))};
      drawUIDropdown(outSelectRect, "Output", outputLabel(focusedOutputIndex), "settings.focused_output");
      settingsBtns_.push_back({outSelectRect, 251, "cycle_output"});
      SDL_Rect enableBtn {cx + headerBtnW + uiScaled(12), cy, headerBtnW, std::max(sTallH, uiScaled(32))};
      drawActionBtn(enableBtn, outputTarget.enabled ? "OUTPUT ON" : "OUTPUT OFF", kSettingsActionOutputToggle, outputTarget.enabled);
      cy += std::max(sTallH, uiScaled(32)) + uiScaled(6);

      // ─── Sub-tab bar ───
      settingsVideoSubTab_ = std::clamp(settingsVideoSubTab_, 0, 3);
      const std::vector<std::string> subTabs {"Display", "Processing", "Other Outputs", "Streaming"};
      {
        // Same cartridge-shelf treatment as the main tab bar, one size down.
        const int kSubTabH = std::max(uiScaled(30), textLineHeight(fontSmall_) + uiScaled(12));
        int subTabW = (availW - (static_cast<int>(subTabs.size()) - 1) * uiScaled(4)) / static_cast<int>(subTabs.size());
        for (int st = 0; st < static_cast<int>(subTabs.size()); ++st) {
          bool active = (st == settingsVideoSubTab_);
          int recess = active ? 0 : uiScaled(4);
          SDL_Rect stBtn {cx + st * (subTabW + uiScaled(4)), cy + recess, subTabW, kSubTabH - recess};
          Primitives::drawFramedPanel(controlRenderer_, stBtn,
                                      active ? pal.dark : pal.light, pal.deep,
                                      active ? pal.light : pal.mid);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, stBtn, subTabs[st],
                               active ? pal.light : pal.deep);
          settingsBtns_.push_back({stBtn, kSettingsActionVideoSubTabBase + st, subTabs[st]});
        }
        cy += kSubTabH + uiScaled(10);
      }

      // ─── Sub-tab content area ───
      int subContentW = availW;
      int subContentH = content.y + content.h - cy - 4;
      int sy = cy; // content Y cursor

      // ═══════════════════════════════════════════════════════════════
      if (settingsVideoSubTab_ == 0) {
      // ─── DISPLAY sub-tab ─────────────────────────────────────────
        int displayCount = deckboyGetNumVideoDisplays();

        // Display & Raster — 3 rows (display, resolution, fullscreen+orientation)
        int dispSectionH = settingsHeaderHeight(fontSmall_) + 3 * (kRowH + kRowGap) + 8;
        SDL_Rect displaySection {cx, sy, subContentW, dispSectionH};
        SDL_Rect dBody = drawSectionFrame(displaySection, "DISPLAY & RASTER");
        VerticalLayout dLayout(dBody, kRowGap);

        std::string displayLabel = displayCount <= 0 ? "None" : ("Display " + std::to_string(outputDisplayIndex(focusedOutputIndex) + 1));
        if (displayCount > 0) {
          const char* dName = deckboyGetDisplayName(outputDisplayIndex(focusedOutputIndex));
          if (dName && *dName) displayLabel += ": " + std::string(dName);
        }
        SDL_Rect dBtn = dLayout.takeFixed(kRowH);
        drawUIDropdown(dBtn, "Hardware Display", displayLabel, "settings.output_display");
        settingsBtns_.push_back({dBtn, kSettingsActionOutputDisplayDropdown, "output_display"});

        SDL_Rect rBtn = dLayout.takeFixed(kRowH);
        std::string resLabel = std::to_string(targetW) + "x" + std::to_string(targetH) + (project_.outputFollowDisplay ? " (Native)" : " (Fixed)");
        drawUIDropdown(rBtn, "Resolution", resLabel, "settings.output_raster");
        settingsBtns_.push_back({rBtn, 237, "custom_raster"});

        // Fullscreen + orientation share a row — two half-width actions read
        // better than stacked full-width bars in a card this wide.
        SDL_Rect pairRow = dLayout.takeFixed(kRowH);
        SDL_Rect fsBtn {pairRow.x, pairRow.y, (pairRow.w - 8) / 2, pairRow.h};
        SDL_Rect orientBtn {pairRow.x + (pairRow.w - 8) / 2 + 8, pairRow.y,
                            pairRow.w - (pairRow.w - 8) / 2 - 8, pairRow.h};
        drawActionBtn(fsBtn, "Toggle Fullscreen", 236);
        std::string orientLabel = outputOrientation == 0 ? "0\xc2\xb0 (Normal)"
                                : std::to_string(outputOrientation) + "\xc2\xb0";
        drawActionBtn(orientBtn, "Orientation: " + orientLabel, kSettingsActionOutputOrientationCycle);
        sy += dispSectionH + kSectionGap;

        // Connected Displays — sized for EVERY display so the operator can
        // see and click all of them at once (this list used to collapse to a
        // row or two when the modal was short), clamped to remaining space.
        // Header height comes from the shared plate contract — hardcoding 32
        // here made the section too short once the header scaled with the
        // font, and the row loop silently dropped the last display.
        const int dispListHeaderH = settingsHeaderHeight(fontSmall_);
        int dispListNeededH = dispListHeaderH
                            + std::max(1, displayCount) * (kRowH + uiScaled(4)) + sPad;
        int dispListAvailH = subContentH - dispSectionH - kSectionGap;
        int dispListH = std::clamp(dispListAvailH,
                                   dispListHeaderH + (kRowH + uiScaled(4)) + sPad,
                                   dispListNeededH);
        SDL_Rect dispListSection {cx, sy, subContentW, dispListH};
        SDL_Rect dispListBody = drawSectionFrame(dispListSection, "CONNECTED DISPLAYS");
        {
          // Sits inside the header plate, sharing its vertical extent — it used
          // to be positioned from the card frame with its own height, which put
          // it over the plate's right end and through the card border.
          SDL_Rect plate = sectionHeaderRect(dispListSection);
          int idW = std::min(uiScaled(96), std::max(0, plate.w - uiScaled(12)));
          SDL_Rect idBtn {plate.x + plate.w - idW - uiScaled(3), plate.y + uiScaled(2),
                          idW, std::max(1, plate.h - uiScaled(4))};
          Primitives::drawFramedPanel(controlRenderer_, idBtn, pal.mid, pal.deep, pal.light);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, idBtn, "IDENTIFY", pal.deep);
          settingsBtns_.push_back({idBtn, kSettingsActionDisplayIdentify,
                                   "Show a number badge on every display"});
        }
        int dispRowY = dispListBody.y;
        int currentDispIdx = outputDisplayIndex(focusedOutputIndex);
        for (int di = 0; di < displayCount; ++di) {
          if (dispRowY + kRowH > dispListSection.y + dispListSection.h - 4) break;
          SDL_Rect dispRow {dispListBody.x, dispRowY, dispListBody.w, kRowH};
          bool selected = di == currentDispIdx;
          Primitives::drawFramedPanel(controlRenderer_, dispRow,
                                      selected ? pal.dark : pal.shellInner, pal.deep, pal.mid);
          const char* dNameRaw = deckboyGetDisplayName(di);
          std::string dNameStr = dNameRaw && *dNameRaw ? dNameRaw : ("Display " + std::to_string(di + 1));
          SDL_Rect dispBounds;
          deckboyGetDisplayBounds(di, &dispBounds);
          std::string dispInfo = std::to_string(di + 1) + ": " + dNameStr
            + "  " + std::to_string(dispBounds.w) + "x" + std::to_string(dispBounds.h);
          if (selected) dispInfo += "  [ASSIGNED]";
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect{dispRow.x + 6, dispRow.y + 7, dispRow.w - 12, 16},
                       dispInfo, selected ? pal.light : ink);
          settingsBtns_.push_back({dispRow, kSettingsActionOutputDisplaySelectBase + di, "display_select"});
          dispRowY += kRowH + 4;
        }

      // ═══════════════════════════════════════════════════════════════
      } else if (settingsVideoSubTab_ == 1) {
      // ─── PROCESSING sub-tab ──────────────────────────────────────

        // Edge Blending — shown in pixels of the output raster (same scheme
        // as AOI below; operators get px everywhere on this tab). Stored as
        // fractions of the raster dimension, unchanged.
        {
          const Deck& bd = focusedDeck();
          auto [ebRasterW, ebRasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
          ebRasterW = std::max(16, ebRasterW);
          ebRasterH = std::max(16, ebRasterH);
          // Sized from the shared header + row metrics, not a magic number: rows are
          // font-derived now, so a fixed height clipped the last control.
          int blendH = settingsHeaderHeight(fontSmall_) + 2 * (kRowH + kRowGap) + 8;
          SDL_Rect blendSection {cx, sy, subContentW, blendH};
          SDL_Rect blendBody = drawSectionFrame(blendSection, "EDGE BLENDING");
          int bx = blendBody.x + 2;
          int labelY = blendBody.y;
          int btnY = rowYBelowLabel(labelY, fontSmall_, 2);
          int bw = (blendBody.w - 18) / 4;
          int bgap = 6;
          auto drawBlendCtrl = [&](const char* label, float val, int raster,
                                   int decAction, int incAction) {
            int px = static_cast<int>(std::lround(val * raster));
            std::string valStr = std::string(label) + ": " + std::to_string(px) + "px";
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{bx, labelY, bw, 16}, valStr, soft);
            int btnW = (bw - 4) / 2;
            SDL_Rect decBtn {bx, btnY, btnW, 24};
            SDL_Rect incBtn {bx + btnW + 4, btnY, btnW, 24};
            Primitives::drawFramedPanel(controlRenderer_, decBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "-", ink, decBtn);
            settingsBtns_.push_back({decBtn, decAction, "blend"});
            Primitives::drawFramedPanel(controlRenderer_, incBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "+", ink, incBtn);
            settingsBtns_.push_back({incBtn, incAction, "blend"});
            bx += bw + bgap;
          };
          drawBlendCtrl("Left", bd.edgeBlendLeft, ebRasterW, kSettingsActionOutputEdgeBlendLDec, kSettingsActionOutputEdgeBlendLInc);
          drawBlendCtrl("Right", bd.edgeBlendRight, ebRasterW, kSettingsActionOutputEdgeBlendRDec, kSettingsActionOutputEdgeBlendRInc);
          drawBlendCtrl("Top", bd.edgeBlendTop, ebRasterH, kSettingsActionOutputEdgeBlendTDec, kSettingsActionOutputEdgeBlendTInc);
          drawBlendCtrl("Bottom", bd.edgeBlendBottom, ebRasterH, kSettingsActionOutputEdgeBlendBDec, kSettingsActionOutputEdgeBlendBInc);
          sy += blendH + kSectionGap;
        }

        // Area of Interest — edited as a pixel rect of the output raster
        // (a resolution + position, the way operators think about slices),
        // not four edge percentages. Fractions remain the storage format.
        {
          const OutputTarget& ot = focusedOutput();
          bool aoiActive = ot.aoiLeft > 0.001f || ot.aoiRight > 0.001f
                        || ot.aoiTop > 0.001f   || ot.aoiBottom > 0.001f;
          AoiRectPx aoi = focusedOutputAoiRectPx();
          int aoiH = settingsHeaderHeight(fontSmall_) + 2 * (kRowH + kRowGap) + 8;
          SDL_Rect aoiSection {cx, sy, subContentW, aoiH};
          SDL_Color aoiFill = aoiActive ? pal.dark : pal.shellInner;
          SDL_Color aoiInk2 = aoiActive ? pal.light : ink;
          Primitives::drawFramedPanel(controlRenderer_, aoiSection, aoiFill, pal.deep, pal.light);
          SDL_Rect aoiHdr {aoiSection.x, aoiSection.y, aoiSection.w, 26};
          Primitives::drawFramedPanel(controlRenderer_, aoiHdr, aoiActive ? pal.dark : pal.mid, pal.deep, pal.light);
          std::string aoiTitle = aoiActive
            ? "AREA OF INTEREST  " + std::to_string(aoi.w) + "x" + std::to_string(aoi.h)
              + " @ " + std::to_string(aoi.x) + "," + std::to_string(aoi.y)
            : "AREA OF INTEREST  full " + std::to_string(aoi.rasterW) + "x" + std::to_string(aoi.rasterH);
          drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{aoiHdr.x + 8, aoiHdr.y + 6, aoiHdr.w - 72, aoiHdr.h - 8},
                       aoiTitle, aoiActive ? pal.light : pal.deep);
          SDL_Rect aoiResetBtn {aoiSection.x + aoiSection.w - 56, aoiSection.y + 4, 50, 18};
          Primitives::drawFramedPanel(controlRenderer_, aoiResetBtn, pal.mid, pal.deep, pal.light);
          drawCenteredText(controlRenderer_, fontSmall_, "FULL", ink, aoiResetBtn);
          settingsBtns_.push_back({aoiResetBtn, kSettingsActionOutputAoiReset, "aoi_reset"});
          int abx = aoiSection.x + 16;
          int ably = aoiSection.y + 32;
          int aoBtnY = rowYBelowLabel(ably, fontSmall_, 4);
          int aoBW = (aoiSection.w - 40) / 4;
          int aoGap = 6;
          auto drawAoiCtrl = [&](const char* label, int px, int decAct, int incAct, int editAct) {
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{abx, ably, aoBW, 16}, label, aoiActive ? pal.light : soft);
            int nudgeW = 22;
            SDL_Rect decBtn {abx, aoBtnY, nudgeW, 24};
            SDL_Rect valBtn {abx + nudgeW + 3, aoBtnY, aoBW - 2 * (nudgeW + 3), 24};
            SDL_Rect incBtn {abx + aoBW - nudgeW, aoBtnY, nudgeW, 24};
            Primitives::drawFramedPanel(controlRenderer_, decBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "-", aoiInk2, decBtn);
            settingsBtns_.push_back({decBtn, decAct, "aoi"});
            Primitives::drawFramedPanel(controlRenderer_, valBtn, pal.shellInner, pal.deep, pal.light);
            drawCenteredTextSafe(controlRenderer_, fontSmall_, valBtn, std::to_string(px),
                                 aoiActive ? pal.light : ink);
            settingsBtns_.push_back({valBtn, editAct, "aoi_edit"});
            Primitives::drawFramedPanel(controlRenderer_, incBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "+", aoiInk2, incBtn);
            settingsBtns_.push_back({incBtn, incAct, "aoi"});
            abx += aoBW + aoGap;
          };
          drawAoiCtrl("X", aoi.x, kSettingsActionOutputAoiXDec, kSettingsActionOutputAoiXInc, kSettingsActionOutputAoiXEdit);
          drawAoiCtrl("Y", aoi.y, kSettingsActionOutputAoiYDec, kSettingsActionOutputAoiYInc, kSettingsActionOutputAoiYEdit);
          drawAoiCtrl("WIDTH", aoi.w, kSettingsActionOutputAoiWDec, kSettingsActionOutputAoiWInc, kSettingsActionOutputAoiWEdit);
          drawAoiCtrl("HEIGHT", aoi.h, kSettingsActionOutputAoiHDec, kSettingsActionOutputAoiHInc, kSettingsActionOutputAoiHEdit);
          sy += aoiH + kSectionGap;
        }

        // Canvas Mode + Default Transition (side by side)
        {
          int halfW = (subContentW - 8) / 2;
          int pairH = 68;

          {
            SDL_Rect canvasPanel {cx, sy, halfW, pairH};
            Primitives::drawFramedPanel(controlRenderer_, canvasPanel, pal.shellInner, pal.deep, pal.light);
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{canvasPanel.x + 8, canvasPanel.y + 4, canvasPanel.w - 16, 16},
                         "CANVAS MODE", ink);
            SDL_Rect canvasToggle {canvasPanel.x + 8, canvasPanel.y + 26, std::min(130, halfW - 16), kRowH};
            drawActionBtn(canvasToggle,
                          project_.outputCanvasEnabled ? "CANVAS: ON" : "CANVAS: OFF",
                          kSettingsActionCanvasToggle, project_.outputCanvasEnabled);
            if (project_.outputCanvasEnabled) {
              int cBtnX = canvasToggle.x + canvasToggle.w + 6;
              int cBtnW = std::max(56, (canvasPanel.x + canvasPanel.w - 8 - cBtnX - 4) / 2);
              SDL_Rect cwBtn {cBtnX, canvasPanel.y + 26, cBtnW, kRowH};
              Primitives::drawFramedPanel(controlRenderer_, cwBtn, pal.light, pal.deep, pal.mid);
              drawCenteredText(controlRenderer_, fontSmall_, "W:" + std::to_string(project_.outputCanvasWidth), ink, cwBtn);
              settingsBtns_.push_back({cwBtn, kSettingsActionCanvasWidthPrompt, "canvas_w"});
              SDL_Rect chBtn {cwBtn.x + cBtnW + 4, canvasPanel.y + 26, cBtnW, kRowH};
              Primitives::drawFramedPanel(controlRenderer_, chBtn, pal.light, pal.deep, pal.mid);
              drawCenteredText(controlRenderer_, fontSmall_, "H:" + std::to_string(project_.outputCanvasHeight), ink, chBtn);
              settingsBtns_.push_back({chBtn, kSettingsActionCanvasHeightPrompt, "canvas_h"});
            }
          }
          {
            const Deck& td = focusedDeck();
            SDL_Rect transPanel {cx + halfW + 8, sy, halfW, pairH};
            Primitives::drawFramedPanel(controlRenderer_, transPanel, pal.shellInner, pal.deep, pal.light);
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{transPanel.x + 8, transPanel.y + 4, transPanel.w - 16, 16},
                         "DEFAULT TRANSITION", ink);
            int tx = transPanel.x + 8;
            int ty = transPanel.y + 26;
            int tBtnW = 32;
            SDL_Rect tDecBtn {tx, ty, tBtnW, kRowH};
            Primitives::drawFramedPanel(controlRenderer_, tDecBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "-", ink, tDecBtn);
            settingsBtns_.push_back({tDecBtn, kSettingsActionTransitionSecondsDec, "trans_dec"});
            SDL_Rect tValRect {tx + tBtnW + 4, ty, 76, kRowH};
            Primitives::drawFramedPanel(controlRenderer_, tValRect, pal.light, pal.deep, pal.mid);
            drawCenteredText(controlRenderer_, fontSmall_, formatSeconds(td.transitionSeconds), ink, tValRect);
            SDL_Rect tIncBtn {tValRect.x + tValRect.w + 4, ty, tBtnW, kRowH};
            Primitives::drawFramedPanel(controlRenderer_, tIncBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "+", ink, tIncBtn);
            settingsBtns_.push_back({tIncBtn, kSettingsActionTransitionSecondsInc, "trans_inc"});
            int styleX = tIncBtn.x + tIncBtn.w + 6;
            int styleW = std::max(60, transPanel.x + transPanel.w - 8 - styleX);
            SDL_Rect tStyleBtn {styleX, ty, styleW, kRowH};
            std::string styleLabel = toUpper(td.transitionStyle.empty() ? "crossfade" : td.transitionStyle);
            Primitives::drawFramedPanel(controlRenderer_, tStyleBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, styleLabel, ink, tStyleBtn);
            settingsBtns_.push_back({tStyleBtn, kSettingsActionTransitionStyleCycle, "trans_style"});
          }
        }

      // ═══════════════════════════════════════════════════════════════
      } else if (settingsVideoSubTab_ == 2) {
      // ─── BACKENDS sub-tab ────────────────────────────────────────

        // Output Settings
        {
          int osH = 108;
          SDL_Rect osSection {cx, sy, subContentW, osH};
          SDL_Rect osBody = drawSectionFrame(osSection, "OUTPUT SETTINGS");
          VerticalLayout osLayout(osBody, kRowGap);
          {
            SDL_Rect row = osLayout.takeFixed(kRowH);
            int thirdW = (row.w - 8) / 3;
            SDL_Rect alphaBtn {row.x, row.y, thirdW, kRowH};
            int alphaPct = static_cast<int>(std::lround(std::clamp(outputTarget.outputAlpha, 0.0f, 1.0f) * 100.0f));
            drawActionBtn(alphaBtn, "Alpha: " + std::to_string(alphaPct) + "%", kSettingsActionOutputAlphaPrompt);
            SDL_Rect delayBtn {row.x + thirdW + 4, row.y, thirdW, kRowH};
            drawActionBtn(delayBtn, "Delay: " + std::to_string(outputDelayMs) + "ms", kSettingsActionOutputDelayPrompt);
            SDL_Rect csBtn {row.x + (thirdW + 4) * 2, row.y, row.w - (thirdW + 4) * 2, kRowH};
            drawActionBtn(csBtn, "Color: " + toUpper(outputColorSpace), kSettingsActionOutputColorSpaceCycle);
          }
          {
            SDL_Rect row = osLayout.takeFixed(kRowH);
            int halfW2 = (row.w - 4) / 2;
            SDL_Rect layoutBtn {row.x, row.y, halfW2, kRowH};
            drawActionBtn(layoutBtn, "Layout: " + toUpper(outputLayoutMode), kSettingsActionOutputLayoutSpan,
                          outputLayoutMode == "span");
            SDL_Rect mirrorBtn {row.x + halfW2 + 4, row.y, row.w - halfW2 - 4, kRowH};
            drawUIDropdown(mirrorBtn, "Mirror", mirrorLabel, "settings.output_mirror");
            settingsBtns_.push_back({mirrorBtn, kSettingsActionOutputMirrorDropdown, "output_mirror"});
          }
          sy += osH + kSectionGap;
        }

        // NDI Network
        {
          int ndiH = settingsHeaderHeight(fontSmall_) + 3 * (kRowH + kRowGap) + 8;
          SDL_Rect ndiSection {cx, sy, subContentW, ndiH};
          SDL_Rect nBody = drawSectionFrame(ndiSection, "NDI\xc2\xae NETWORK");
          VerticalLayout nLayout(nBody, kRowGap);
          drawActionBtn(nLayout.takeFixed(kRowH), outputTarget.ndiEnabled ? "NDI SENDING: ON" : "NDI SENDING: OFF", 271, outputTarget.ndiEnabled);
          SDL_Rect nnBtn = nLayout.takeFixed(kRowH);
          std::string ndiName = trim(outputTarget.ndiSourceName).empty() ? "Default" : outputTarget.ndiSourceName;
          drawUIDropdown(nnBtn, "Source", ndiName, "settings.ndi_name");
          settingsBtns_.push_back({nnBtn, 272, "ndi_name"});
          SDL_Rect ndiKeyBtn = nLayout.takeFixed(kRowH);
          drawActionBtn(ndiKeyBtn, outputTarget.ndiKeyEnabled ? "NDI KEY: ON" : "NDI KEY: OFF", 273, outputTarget.ndiKeyEnabled);
          sy += ndiH + kSectionGap;
        }

        // DeckLink + Spout side by side
        {
          int halfW = (subContentW - 8) / 2;

          // DeckLink
          int dlH = settingsHeaderHeight(fontSmall_) + 4 * (kRowH + kRowGap) + 8;
          SDL_Rect dlSection {cx, sy, halfW, dlH};
          SDL_Rect dlBody = drawSectionFrame(dlSection, "DECKLINK SDI / HDMI");
          VerticalLayout dlLayout(dlBody, kRowGap);
          SDL_Rect dlToggle = dlLayout.takeFixed(kRowH);
          drawActionBtn(dlToggle,
                        outputTarget.deckLinkEnabled ? "DECKLINK: ON" : "DECKLINK: OFF",
                        kSettingsActionDeckLinkToggle, outputTarget.deckLinkEnabled);
          {
            std::string devLabel = "None";
            if (outputTarget.deckLinkDeviceId >= 0) {
              auto devices = deckboy::platform::video::DeckLinkOutput::listDevices();
              for (const auto& d : devices) {
                if (d.id == outputTarget.deckLinkDeviceId) {
                  devLabel = d.modelName;
                  if (d.supportsSDI && d.supportsHDMI) devLabel += " (SDI+HDMI)";
                  else if (d.supportsSDI) devLabel += " (SDI)";
                  else if (d.supportsHDMI) devLabel += " (HDMI)";
                  break;
                }
              }
            }
            SDL_Rect devBtn = dlLayout.takeFixed(kRowH);
            drawUIDropdown(devBtn, "Device", devLabel, "settings.decklink_device");
            settingsBtns_.push_back({devBtn, kSettingsActionDeckLinkDeviceDropdown, "decklink_device"});
          }
          {
            auto mode = deckboy::platform::video::parseDeckLinkMode(outputTarget.deckLinkMode);
            std::string modeLabel2 = deckboy::platform::video::deckLinkModeLabel(mode);
            SDL_Rect modeBtn = dlLayout.takeFixed(kRowH);
            drawUIDropdown(modeBtn, "Mode", modeLabel2, "settings.decklink_mode");
            settingsBtns_.push_back({modeBtn, kSettingsActionDeckLinkModeDropdown, "decklink_mode"});
          }
          SDL_Rect bitBtn = dlLayout.takeFixed(kRowH);
          drawActionBtn(bitBtn, outputTarget.deckLink10Bit ? "10-BIT: ON" : "10-BIT: OFF",
                        kSettingsActionDeckLink10BitToggle, outputTarget.deckLink10Bit);

          // Spout
          int spH = dlH; // match DeckLink height
          SDL_Rect spSection {cx + halfW + 8, sy, halfW, spH};
          SDL_Rect spBody = drawSectionFrame(spSection, "SPOUT TEXTURE SHARE");
          VerticalLayout spLayout(spBody, kRowGap);
          SDL_Rect spToggle = spLayout.takeFixed(kRowH);
          drawActionBtn(spToggle,
                        outputTarget.spoutEnabled ? "SPOUT: ON" : "SPOUT: OFF",
                        kSettingsActionSpoutToggle, outputTarget.spoutEnabled);
          {
            std::string spName = trim(outputTarget.spoutSenderName);
            if (spName.empty()) {
              spName = "Deckboy Output " + std::to_string(focusedOutputIndex + 1);
            }
            SDL_Rect spNameBtn = spLayout.takeFixed(kRowH);
            drawUIDropdown(spNameBtn, "Sender", spName, "settings.spout_name");
            settingsBtns_.push_back({spNameBtn, kSettingsActionSpoutNamePrompt, "spout_name"});
          }
        }

      // ═══════════════════════════════════════════════════════════════
      } else if (settingsVideoSubTab_ == 3) {
      // ─── STREAMING sub-tab ───────────────────────────────────────

        int streamH = std::max(280, subContentH);
        SDL_Rect streamSection {cx, sy, subContentW, streamH};
        SDL_Rect sBody = drawSectionFrame(streamSection, "STREAMING (SRT / RTMP)");
        VerticalLayout sLayout(sBody, kRowGap);

        drawActionBtn(sLayout.takeFixed(kRowH), outputTarget.streamEnabled ? "STREAMING: ON" : "STREAMING: OFF", 255, outputTarget.streamEnabled);

        // State lines
        {
          std::string stateLine1 = "State: " + outputHealthLabel(focusedOutputIndex);
          std::string stateLine2;
          std::string streamReason = outputHealthReason(focusedOutputIndex);
          if (outputTarget.outputTestCardEnabled) {
            stateLine2 = "TEST CARD OVERRIDE ACTIVE";
          } else if (!streamReason.empty()) {
            stateLine2 = streamReason;
          } else if (!outputTarget.enabled) {
            stateLine2 = "Turn OUTPUT ON to send";
          } else if (!outputTarget.streamEnabled) {
            stateLine2 = "Toggle STREAMING ON to send";
          }
          SDL_Rect sl1Rect = sLayout.takeFixed(kLabelH);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect{sBody.x, sl1Rect.y, sBody.w, kLabelH}, stateLine1, soft);
          if (!stateLine2.empty()) {
            SDL_Rect sl2Rect = sLayout.takeFixed(kLabelH);
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{sBody.x, sl2Rect.y, sBody.w, kLabelH}, stateLine2, soft);
          }
        }

        SDL_Rect typeBtn = sLayout.takeFixed(kRowH);
        drawActionBtn(typeBtn, "TYPE: " + toUpper(outputTypeLabel), 259, outputTypeLabel == "stream");

        SDL_Rect spBtn = sLayout.takeFixed(kRowH);
        drawUIDropdown(spBtn, "Protocol", toUpper(normalizeOutputStreamProtocol(outputTarget.streamProtocol)), "settings.stream_proto");
        settingsBtns_.push_back({spBtn, kSettingsActionOutputStreamProtocolDropdown, "stream_proto"});

        SDL_Rect bitrateBtn = sLayout.takeFixed(kRowH);
        drawUIDropdown(bitrateBtn, "Bitrate",
                       std::to_string(outputTarget.streamBitrateKbps) + " kbps",
                       "settings.stream_bitrate");
        settingsBtns_.push_back({bitrateBtn, 258, "stream_bitrate"});

        SDL_Rect testCardBtn = sLayout.takeFixed(kRowH);
        drawActionBtn(testCardBtn,
                      outputTarget.outputTestCardEnabled
                        ? "TEST CARD OVERRIDE: ON"
                        : "TEST CARD OVERRIDE: OFF",
                      kSettingsActionOutputTestCardToggle,
                      outputTarget.outputTestCardEnabled);

        // Stream Key — RTMP only
        bool isRtmp = streamProtocol == "rtmp" || streamProtocol == "rtmps";
        if (isRtmp) {
          SDL_Rect skLabelRect = sLayout.takeFixed(kLabelH);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect{sBody.x, skLabelRect.y, sBody.w, kLabelH}, "Stream Key:", soft);
          SDL_Rect skBtn = sLayout.takeFixed(kRowH);
          std::string keyDisplay = trim(outputTarget.streamKey);
          Primitives::drawFramedPanel(controlRenderer_, skBtn, pal.deep, pal.dark, pal.dark);
          std::string keyMasked = keyDisplay.empty() ? "click to set" : std::string(keyDisplay.size(), '*');
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect{skBtn.x + 8, skBtn.y + 4, skBtn.w - 16, kRowH - 8},
                       keyMasked, keyDisplay.empty() ? pal.dark : pal.light);
          settingsBtns_.push_back({skBtn, kSettingsActionStreamKeyPrompt, "stream_key"});
        }

        // Target URL — takes all remaining space
        {
          SDL_Rect urlLabelRect = sLayout.takeFixed(kLabelH);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect{sBody.x, urlLabelRect.y, sBody.w, kLabelH},
                       isRtmp ? "Server URL:" : "Target URL:", soft);
        }
        SDL_Rect suBtn = sLayout.takeRemaining();
        suBtn.h = std::max(40, suBtn.h);
        Primitives::drawFramedPanel(controlRenderer_, suBtn, pal.deep, pal.dark, pal.dark);
        TTF_Font* urlFont = fontMono_ ? fontMono_ : fontSmall_;
        std::string urlStr2 = streamUrl;
        {
          int urlPad = 8;
          SDL_Rect urlClip {suBtn.x + urlPad, suBtn.y + 4, suBtn.w - urlPad * 2, suBtn.h - 8};
          std::string urlLine1 = urlStr2;
          std::string urlLine2;
          size_t queryPos = urlStr2.find('?');
          if (queryPos != std::string::npos) {
            urlLine1 = urlStr2.substr(0, queryPos);
            urlLine2 = urlStr2.substr(queryPos);
          }
          drawTextSafe(controlRenderer_, urlFont,
                       SDL_Rect{suBtn.x + urlPad, suBtn.y + 6, urlClip.w, 16},
                       urlLine1.empty() ? "click to edit" : urlLine1,
                       urlLine1.empty() ? pal.dark : pal.light);
          if (!urlLine2.empty()) {
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{suBtn.x + urlPad, suBtn.y + 24, urlClip.w, 16},
                         urlLine2, pal.mid);
          } else {
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{suBtn.x + urlPad, suBtn.y + 24, urlClip.w, 16},
                         "Add Output 2 to stream SRT + RTMP simultaneously", pal.dark);
          }
        }
        settingsBtns_.push_back({suBtn, 257, "stream_url"});
      }

      // No scrollbar needed — each sub-tab fits in the viewport
      settingsVideoViewport_ = {};
      settingsVideoScrollMax_ = 0;
      settingsVideoScroll_ = 0;

    } else if (settingsTab_ == 4) {
      // About tab — masthead + credits. Two columns of labelled rows sharing
      // one label gutter, so every value starts on the same x. Content is
      // factual only: what this is, who owns it, what it is built on, and what
      // this particular build/session is doing.
      const int aboutLineH = textLineHeight(fontSmall_) + 4;
      int aboutColGap = 12;
      int aboutLeftW = std::max(300, (content.w - 24 - aboutColGap) / 2);
      int aboutRightW = std::max(300, content.w - 24 - aboutColGap - aboutLeftW);

      // ── Masthead: art + wordmark + version ──
      int mastH = std::clamp(content.h * 30 / 100, 96, 190);
      SDL_Rect mastRect {cx, cy, content.w - 24, mastH};
      Primitives::drawFramedPanel(controlRenderer_, mastRect, pal.shellInner, pal.deep, pal.light);
      int artW = std::min(mastRect.w / 2 - 16, 300);
      if (uiAboutLogo_.texture || !uiAboutLogo_.path.empty()) {
        ensureUiImageLoaded(uiAboutLogo_);
        drawUiImageContain(uiAboutLogo_,
                           SDL_Rect{mastRect.x + 10, mastRect.y + 8, artW, mastRect.h - 16}, 255);
      } else if (uiPackAvailable_) {
        drawUiImageContain(uiHeaderArt_,
                           SDL_Rect{mastRect.x + 10, mastRect.y + 10, artW, mastRect.h - 20}, 215);
      }
      {
        TTF_Font* titleFont = fontPixel_ ? fontPixel_ : fontLarge_;
        int textX = mastRect.x + artW + 24;
        int textW = std::max(0, mastRect.x + mastRect.w - 12 - textX);
        int blockH = textLineHeight(titleFont) + aboutLineH * 2 + 6;
        int ty = mastRect.y + std::max(8, (mastRect.h - blockH) / 2);
        drawTextSafe(controlRenderer_, titleFont,
                     SDL_Rect{textX, ty, textW, textLineHeight(titleFont)},
                     std::string(kAppTitle), ink);
        ty += textLineHeight(titleFont) + 4;
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{textX, ty, textW, aboutLineH},
                     "dot-matrix cue deck for live events", soft);
        ty += aboutLineH;
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{textX, ty, textW, aboutLineH},
                     std::string(kAppVersionTag) + "   ("
                       + std::string(__DATE__) + " build)", soft);
      }

      // Shared row renderer: fixed label gutter, value flush against it. Every
      // credit line on this page lands on the same two x positions.
      int aboutY2 = cy + mastH + 10;
      int colsH = std::max(120, content.y + content.h - aboutY2 - 8);
      SDL_Rect leftAbout {cx, aboutY2, aboutLeftW, colsH};
      SDL_Rect rightAbout {cx + aboutLeftW + aboutColGap, aboutY2, aboutRightW, colsH};
      auto aboutRow = [&](const SDL_Rect& card, int& rowY, const std::string& label,
                          const std::string& value) {
        int gutter = std::min(96, card.w / 3);
        int rowH = aboutLineH;
        if (rowY + rowH > card.y + card.h - 6) {
          return;
        }
        if (!label.empty()) {
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect{card.x + 12, rowY, gutter - 8, rowH}, label, soft);
        }
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect{card.x + 12 + gutter, rowY,
                              std::max(0, card.w - 24 - gutter), rowH},
                     value, ink);
        rowY += rowH;
      };

      drawSettingsCard(leftAbout, "PROJECT");
      int leftRowY = leftAbout.y + settingsHeaderHeight(fontBase_) + 2;
      aboutRow(leftAbout, leftRowY, "Copyright", "(C) 2026 Deckboy Contributors");
      aboutRow(leftAbout, leftRowY, "Licence", "GPL-3.0-or-later");
      aboutRow(leftAbout, leftRowY, "", "Free software. You may use, study, share");
      aboutRow(leftAbout, leftRowY, "", "and modify it; derivatives stay GPL.");
      aboutRow(leftAbout, leftRowY, "Source", "github.com/Utopian-Academy/Deckboy");
      leftRowY += 6;
      aboutRow(leftAbout, leftRowY, "Warranty", "None. See LICENSE sections 15-16.");
      leftRowY += 6;
      aboutRow(leftAbout, leftRowY, "Session", "Companion " + std::to_string(companionPort_)
                 + (companionReady_ ? " (up)" : " (down)"));
      aboutRow(leftAbout, leftRowY, "",
               project_.allowRemoteNetwork ? "Remote: all interfaces"
                                           : "Remote: localhost only");
      aboutRow(leftAbout, leftRowY, "", "HyperDeck 9992  |  OSC Query "
                 + std::to_string(project_.oscQueryPort));
      aboutRow(leftAbout, leftRowY, "Theme",
               currentThemeName_.empty() ? "gameboy" : currentThemeName_);

      drawSettingsCard(rightAbout, "BUILT WITH");
      int rightRowY = rightAbout.y + settingsHeaderHeight(fontBase_) + 2;
      aboutRow(rightAbout, rightRowY, "Platform",
#if defined(_WIN32)
               "Windows x64"
#elif defined(__APPLE__)
               "macOS"
#else
               "Linux"
#endif
               );
      aboutRow(rightAbout, rightRowY, "Toolkit", "SDL3  +  SDL3_ttf  (FreeType)");
      aboutRow(rightAbout, rightRowY, "Media", "FFmpeg / libav"
#if DECKBOY_INPROC_DECODE
               "  (in-process)"
#endif
               );
      // Only claim the optional SDKs this binary was actually built against —
      // an About page that lists what it cannot do is worse than no page.
      {
        std::vector<std::string> optional;
#if defined(DECKBOY_HAS_NDI_SDK)
        optional.push_back("NDI");
#endif
#if defined(DECKBOY_HAS_DECKLINK)
        optional.push_back("DeckLink");
#endif
#if defined(DECKBOY_HAS_SPOUT)
        optional.push_back("Spout");
#endif
#if defined(DECKBOY_HAS_SIPHON)
        optional.push_back("Syphon");
#endif
#if defined(DECKBOY_HAS_MIDI)
        optional.push_back("RtMidi");
#endif
#if defined(DECKBOY_HAS_WEBVIEW)
        optional.push_back("WebView2");
#endif
#if defined(DECKBOY_HAS_CEF)
        optional.push_back("CEF");
#endif
        std::string joined;
        for (std::size_t i = 0; i < optional.size(); ++i) {
          joined += (i ? "  " : "") + optional[i];
        }
        aboutRow(rightAbout, rightRowY, "Optional",
                 joined.empty() ? std::string("none compiled in") : joined);
      }
      aboutRow(rightAbout, rightRowY, "Timecode", "libltc (x42)");
      rightRowY += 6;
      aboutRow(rightAbout, rightRowY, "Thanks",
               "the FFmpeg, SDL and FreeType projects,");
      aboutRow(rightAbout, rightRowY, "", "Bitfocus Companion, and x42/libltc.");
      rightRowY += 6;
      aboutRow(rightAbout, rightRowY, "Fonts", "Press Start 2P (Cody Boisclair,");
      aboutRow(rightAbout, rightRowY, "", "SIL Open Font License 1.1)");
      rightRowY += 6;
      aboutRow(rightAbout, rightRowY, "Keys",
               "Enter Take  Space Play/Pause");
      aboutRow(rightAbout, rightRowY, "", "S Stop   C Clear   Esc Panic");
    } else if (settingsTab_ == 5) {
      // Encoder tab — the built-in media converter surface.
      drawSettingsCard(SDL_Rect{cx, cy, content.w - 24, content.h - 20}, "MEDIA ENCODER",
                       "Convert cues Deckboy can't play (or plays poorly) to H.264");
      int ex = cx + 8;
      int ey = cy + 56;
      int ew = content.w - 40;
      struct FlaggedCue { int deck; int cue; std::string label; std::string reason; bool converting; };
      std::vector<FlaggedCue> flagged;
      bool anyToConvert = false;
      for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
        const Deck& deck = project_.decks[d];
        for (int c = 0; c < static_cast<int>(deck.cues.size()); ++c) {
          const Cue& cue = deck.cues[c];
          if (auto r = cueConvertReason(cue)) {
            bool cv = isCueConverting(cue.path);
            flagged.push_back({d, c, cue.name, *r, cv});
            if (!cv) anyToConvert = true;
          }
        }
      }
      SDL_Rect convAllBtn {ex, ey, 190, 28};
      drawUIPanel(convAllBtn, anyToConvert ? pal.dark : pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, convAllBtn,
                           "CONVERT ALL FLAGGED", anyToConvert ? pal.light : pal.inkSoft);
      if (anyToConvert) {
        settingsBtns_.push_back({convAllBtn, kSettingsActionEncoderConvertAll, "convert all flagged cues"});
      }
      SDL_Rect addBtn {ex + 200, ey, 120, 28};
      drawUIPanel(addBtn, pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, addBtn, "ADD FILE...", pal.deep);
      settingsBtns_.push_back({addBtn, kSettingsActionEncoderAddFile, "add file(s) to the show for conversion"});
      ey += 40;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{ex, ey, ew, 16},
                   "Target: H.264 MP4 (GPU, libx264 fallback) -> portable _converted/ next to show   |   active jobs: "
                   + std::to_string(static_cast<int>(conversionJobs_.size())), soft);
      ey += 24;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{ex, ey, ew, 16},
                   flagged.empty() ? "No cues need conversion." :
                     (std::to_string(static_cast<int>(flagged.size())) + " cue(s) flagged:"), ink);
      ey += 22;
      const int rowH = 20;
      int maxRows = std::max(0, (content.y + content.h - 14 - ey) / rowH);
      int shown = 0;
      for (const auto& f : flagged) {
        if (shown >= maxRows) break;
        std::string line = "D" + std::to_string(f.deck + 1) + " Q" + std::to_string(f.cue + 1)
                         + "  " + f.label + "   (" + f.reason + ")"
                         + (f.converting ? "   [converting...]" : "");
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{ex, ey, ew, 16},
                     ellipsizeToPixelWidth(fontSmall_, line, ew), f.converting ? soft : ink);
        ey += rowH;
        ++shown;
      }
      if (static_cast<int>(flagged.size()) > shown) {
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{ex, ey, ew, 16},
                     "  +" + std::to_string(static_cast<int>(flagged.size()) - shown) + " more", soft);
      }
    }
  }

  void handleSettingsClick(int mx, int my) {
    // Check close button
    if (pointInRect(mx, my, settingsCloseBtn_)) {
      settingsOpen_ = false;
      uiWatchdogPopupEvent("settings_modal", false);
      return;
    }
    if (!settingsOpen_) return;

    for (const auto& sb : settingsBtns_) {
      if (!pointInRect(mx, my, sb.rect)) continue;
      if (sb.action == kSettingsActionEncoderConvertAll) { convertAllFlaggedCues(); continue; }
      if (sb.action == kSettingsActionEncoderAddFile) {
        auto pf = pickFiles();
        if (!pf.empty()) { importPaths(pf); }
        continue;
      }
      if (sb.action >= 100 && sb.action <= 105) {
        // Tab switch
        settingsTab_ = sb.action - 100;
        settingsVideoSubTab_ = 0;
        settingsVideoScroll_ = 0;
        settingsVideoScrollMax_ = 0;
        settingsSystemScroll_ = 0;
        settingsSystemScrollMax_ = 0;
      } else if (sb.action == 200) {
        openDropdown(
          "settings.audio_device",
          sb.rect,
          audioOutputDeviceDropdownChoices(),
          focusedDeck().audioOutputDeviceName,
          [this](const std::string& value) {
            setAudioOutputDevice(value);
          });
        return;
      } else if (sb.action == 201) {
        project_.uiSoundsEnabled = !project_.uiSoundsEnabled;
        markProjectDirty();
      } else if (sb.action == 202) {
        toggleUiTransitions();
      } else if (sb.action == 203) {
        toggleJumpMode();
      } else if (sb.action == 204) {
        setJumpTransitionEnabled(!project_.jumpTransitionEnabled);
      } else if (sb.action == 205) {
        cyclePanicProfile(-1);
      } else if (sb.action == 206) {
        cyclePanicProfile(1);
      } else if (sb.action == 207) {
        triggerPanicProfile();
      } else if (sb.action == 208) {
        adjustPanicFadeSeconds(-0.1);
      } else if (sb.action == 209) {
        adjustPanicFadeSeconds(0.1);
      } else if (sb.action == 212) {
        setPanicAutoRestoreEnabled(!project_.panicAutoRestore);
      } else if (sb.action == 213) {
        setTimecodeJamSyncEnabled(!focusedDeck().timecodeJamSyncEnabled);
      } else if (sb.action == 214) {
        setTimecodeFreewheelSeconds(focusedDeck().timecodeFreewheelSeconds - 0.1);
      } else if (sb.action == 215) {
        setTimecodeFreewheelSeconds(focusedDeck().timecodeFreewheelSeconds + 0.1);
      } else if (sb.action == 216) {
        settingsOpen_ = false;
        openInlineCueFindEditor(false);
      } else if (sb.action == 217) {
        if (!lastCueFindToken_.empty()) {
          findCueToken(lastCueFindToken_, 1, false);
        } else {
          triggerToast("find: run Find Cue first");
        }
      } else if (sb.action == 218) {
        if (!lastCueFindToken_.empty()) {
          findCueToken(lastCueFindToken_, -1, false);
        } else {
          triggerToast("find: run Find Cue first");
        }
      } else if (sb.action == 219) {
        settingsOpen_ = false;
        openInlineCueFindEditor(true);
      } else if (sb.action == 221) {
        settingsOpen_ = false;
        openInlineCueRenumberEditor(true);
      } else if (sb.action == 222) {
        clearFocusedDeckCueNumbers();
      } else if (sb.action == 223) {
        addSourceCueFromMenu();
      } else if (sb.action == 224) {
        clearCueFindState();
        triggerToast("find cleared");
      } else if (sb.action == kSettingsActionPlaylistPrefsEdit) {
        editFocusedDeckPlaylistPreferences();
      } else if (sb.action == kSettingsActionPlaylistDefaultLoopToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultLoop = !deck.playlistDefaultLoop;
        triggerToast(std::string("new cues loop: ") + (deck.playlistDefaultLoop ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultFadeInToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultFadeInEnabled = !deck.playlistDefaultFadeInEnabled;
        triggerToast(std::string("new cues fade in: ") + (deck.playlistDefaultFadeInEnabled ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultFadeOutToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultFadeOutEnabled = !deck.playlistDefaultFadeOutEnabled;
        triggerToast(std::string("new cues fade out: ") + (deck.playlistDefaultFadeOutEnabled ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultAudioToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultAudioEnabled = !deck.playlistDefaultAudioEnabled;
        triggerToast(std::string("new cues audio: ") + (deck.playlistDefaultAudioEnabled ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultPauseBeginToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultPauseAtBeginning = !deck.playlistDefaultPauseAtBeginning;
        triggerToast(std::string("new cues pause begin: ") + (deck.playlistDefaultPauseAtBeginning ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultPauseEndToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultPauseAtEnd = !deck.playlistDefaultPauseAtEnd;
        triggerToast(std::string("new cues pause end: ") + (deck.playlistDefaultPauseAtEnd ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultNextTransitionToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultTransitionToNext = !deck.playlistDefaultTransitionToNext;
        triggerToast(std::string("new cues next transition: ") + (deck.playlistDefaultTransitionToNext ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionAudioDelayDec ||
                 sb.action == kSettingsActionAudioDelayInc) {
        int step = sb.action == kSettingsActionAudioDelayInc ? 10 : -10;
        project_.audioDelayMs = std::clamp(project_.audioDelayMs + step, 0, 1000);
        // Applied live next tick (engines sync the atomic alongside master gain).
        triggerToast("A/V delay: " + std::to_string(project_.audioDelayMs) + " ms");
        markProjectDirty();
      } else if (sb.action == kSettingsActionAudioChannelsCycle) {
        // Cycle deck device channels: 2 → 4 → 6 → 8 → 2, reopening the
        // device so the new stream spec applies immediately.
        Deck& deck = focusedDeckMutable();
        int cur = deck.audioOutputChannels;
        deck.audioOutputChannels = (cur <= 2) ? 4 : (cur <= 4) ? 6 : (cur <= 6) ? 8 : 2;
        reopenDeckAudioOutput(project_.focusedDeckIndex, deck.audioOutputDeviceName);
        triggerToast("audio outs: " + std::to_string(deck.audioOutputChannels)
                     + " ch (" + std::to_string(deck.audioOutputChannels / 2) + " pairs)");
        markProjectDirty();
      } else if (sb.action == kSettingsActionAudioBufferCycle) {
        // Cycle audio buffer: 256 → 512 → 1024 → 2048 → 256
        int cur = project_.audioBufferSamples;
        int next = (cur <= 256) ? 512 : (cur <= 512) ? 1024 : (cur <= 1024) ? 2048 : 256;
        project_.audioBufferSamples = next;
        triggerToast("audio buffer: " + std::to_string(next) + " smp (restart to apply)");
        markProjectDirty();
      } else if (sb.action == 210) {
        // Toggle MIDI
        midiEnabled_ = !midiEnabled_;
        if (midiEnabled_) startMidiInput(); else stopMidiInput();
      } else if (sb.action == 211) {
        settingsOpen_ = false;
        openInlineMidiPortEditor();
      } else if (sb.action == 220) {
        settingsOpen_ = false;
        openInlineCompanionPortEditor();
      } else if (sb.action == kSettingsActionAllowRemoteToggle) {
        project_.allowRemoteNetwork = !project_.allowRemoteNetwork;
        // Restart all network listeners with new bind address
        stopCompanionControl();
        stopOscQueryServer();
        stopHyperDeckServer();
        if (project_.atemTriggerEnabled) stopAtemBridgeListener();
        if (project_.dmxArtNetEnabled) stopArtNetBridgeListener();
        if (project_.nmcSyncEnabled) stopNmcSyncBridge();
        startCompanionControl();
        if (project_.oscQueryEnabled) startOscQueryServer();
        startHyperDeckServer();
        if (project_.atemTriggerEnabled) startAtemBridgeListener();
        if (project_.dmxArtNetEnabled) startArtNetBridgeListener();
        if (project_.nmcSyncEnabled) startNmcSyncBridge();
        triggerToast(project_.allowRemoteNetwork ? "network: remote connections enabled" : "network: local only");
        markProjectDirty();
      } else if (sb.action == kSettingsActionOscQueryToggle) {
        setOscQueryEnabled(!project_.oscQueryEnabled);
      } else if (sb.action == kSettingsActionOscQueryPortPrompt) {
        settingsOpen_ = false;
        openInlineOscQueryPortEditor();
      } else if (sb.action == kSettingsActionOscFeedbackMirrorToggle) {
        setOscFeedbackMirrorEnabled(!project_.oscFeedbackMirrorEnabled);
      } else if (sb.action == kSettingsActionOscFeedbackRatePrompt) {
        settingsOpen_ = false;
        openInlineOscFeedbackRateEditor();
      } else if (sb.action == kSettingsActionIntegrationAtemToggle) {
        setIntegrationAdapterEnabled("ATEM", !project_.atemTriggerEnabled);
      } else if (sb.action == kSettingsActionIntegrationNdiTriggerToggle) {
        setIntegrationAdapterEnabled("NDI", !project_.ndiTriggerEnabled);
      } else if (sb.action == kSettingsActionIntegrationNmcToggle) {
        setIntegrationAdapterEnabled("NMC", !project_.nmcSyncEnabled);
      } else if (sb.action == kSettingsActionIntegrationMtcToggle) {
        setIntegrationAdapterEnabled("MTC", !project_.mtcIngestEnabled);
      } else if (sb.action == kSettingsActionIntegrationLtcToggle) {
        setIntegrationAdapterEnabled("LTC", !project_.ltcIngestEnabled);
      } else if (sb.action == kSettingsActionIntegrationArtNetToggle) {
        setIntegrationAdapterEnabled("ARTNET", !project_.dmxArtNetEnabled);
      } else if (sb.action == kSettingsActionIntegrationArtNetPortPrompt) {
        settingsOpen_ = false;
        openInlineArtNetPortEditor();
      } else if (sb.action == kSettingsActionIntegrationTslToggle) {
        project_.tslTallyEnabled = !project_.tslTallyEnabled;
        if (project_.tslTallyEnabled) {
          startTslTally();
          triggerToast("tally: enabled");
        } else {
          stopTslTally();
          triggerToast("tally: disabled");
        }
        markProjectDirty();
      } else if (sb.action == kSettingsActionIntegrationTslPortPrompt) {
        settingsOpen_ = false;
        openInlineTextEditor("tsl_port", "TSL Tally", "Tally port (default 5800)",
          std::to_string(project_.tslTallyPort),
          [this](const std::string& val) {
            try {
              int p = std::stoi(val);
              project_.tslTallyPort = std::clamp(p, 1, 65535);
            } catch (...) {}
            if (project_.tslTallyEnabled) {
              startTslTally(); // reopen socket on new port
            }
            markProjectDirty();
          });
      } else if (sb.action == kSettingsActionIntegrationTslAddrPrompt) {
        settingsOpen_ = false;
        openInlineTextEditor("tsl_address", "TSL Tally", "Target IP (or 255.255.255.255 to broadcast)",
          project_.tslTallyAddress,
          [this](const std::string& val) {
            project_.tslTallyAddress = val.empty() ? "255.255.255.255" : val;
            markProjectDirty();
          });
      } else if (sb.action == kSettingsActionIntegrationAllToggle) {
        bool allAdaptersEnabled = project_.atemTriggerEnabled && project_.ndiTriggerEnabled &&
                                  project_.nmcSyncEnabled && project_.mtcIngestEnabled &&
                                  project_.ltcIngestEnabled && project_.dmxArtNetEnabled;
        setAllIntegrationAdaptersEnabled(!allAdaptersEnabled);
      } else if (sb.action == kSettingsActionIntegrationTimecodeChaseToggle) {
        setTimecodeChaseEnabled(!focusedDeck().timecodeChaseEnabled);
      } else if (sb.action == kSettingsActionIntegrationTimecodeRunToggle) {
        setTimecodeRunEnabled(!focusedDeck().timecodeRunEnabled);
      } else if (sb.action == 230) {
        setOutputSizingModeDisplayNative();
      } else if (sb.action == 231) {
        setOutputSizingModeFixed(1280, 720);
      } else if (sb.action == 232) {
        setOutputSizingModeFixed(1920, 1080);
      } else if (sb.action == 233) {
        setOutputSizingModeFixed(2560, 1440);
      } else if (sb.action == 234) {
        setOutputSizingModeFixed(3840, 2160);
      } else if (sb.action == 235) {
        sizeFocusedOutputToSelectedDisplay();
      } else if (sb.action == 236) {
        toggleOutputFullscreen();
      } else if (sb.action == 237) {
        std::string initial = std::to_string(project_.outputRenderWidth) + "x" + std::to_string(project_.outputRenderHeight);
        openInlineTextEditor("settings.output_raster", "Custom Output Raster",
                             "WIDTHxHEIGHT (e.g. 2560x1080)", initial,
                             [this](const std::string& rawValue) {
                               std::string token = toUpper(trim(rawValue));
                               auto xPos = token.find('X');
                               if (xPos == std::string::npos || xPos == 0 || xPos + 1 >= token.size()) {
                                 triggerToast("raster: invalid");
                                 return;
                               }
                               try {
                                 int w = std::stoi(token.substr(0, xPos));
                                 int h = std::stoi(token.substr(xPos + 1));
                                 if (w > 0 && h > 0) {
                                   setOutputSizingModeFixed(w, h);
                                 } else {
                                   triggerToast("raster: invalid");
                                 }
                               } catch (...) {
                                 triggerToast("raster: invalid");
                               }
                             });
      } else if (sb.action == 238) {
        setOutputRefreshRate(0.0);
      } else if (sb.action == 239) {
        cycleOutputRefreshRate(-1);
      } else if (sb.action == 240) {
        cycleOutputRefreshRate(1);
      } else if (sb.action == 241) {
        openInlineTextEditor("settings.output_refresh", "Output Refresh",
                             "Hz or AUTO (e.g. 60 or 59.94)", outputRefreshRateLabel(),
                             [this](const std::string& rawValue) {
                               std::string token = toUpper(trim(rawValue));
                               if (token == "AUTO") {
                                 setOutputRefreshRate(0.0);
                                 return;
                               }
                               try {
                                 setOutputRefreshRate(std::stod(rawValue));
                               } catch (...) {
                                 triggerToast("refresh: invalid");
                               }
                             });
      } else if (sb.action == 242) {
        setOutputBitDepthMode(0);
      } else if (sb.action == 243) {
        setOutputBitDepthMode(8);
      } else if (sb.action == 244) {
        setOutputBitDepthMode(10);
      } else if (sb.action == 245) {
        setOutputCanvasMode(false);
      } else if (sb.action == 246) {
        setOutputCanvasMode(true, project_.outputCanvasWidth, project_.outputCanvasHeight);
      } else if (sb.action == 247) {
        std::string initial = std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight);
        openInlineTextEditor("settings.output_canvas", "Output Canvas",
                             "WIDTHxHEIGHT (e.g. 5760x2160)", initial,
                             [this](const std::string& rawValue) {
                               std::string token = toUpper(trim(rawValue));
                               auto xPos = token.find('X');
                               if (xPos == std::string::npos || xPos == 0 || xPos + 1 >= token.size()) {
                                 triggerToast("canvas: invalid");
                                 return;
                               }
                               try {
                                 int w = std::stoi(token.substr(0, xPos));
                                 int h = std::stoi(token.substr(xPos + 1));
                                 if (w > 0 && h > 0) {
                                   setOutputCanvasMode(true, w, h);
                                 } else {
                                   triggerToast("canvas: invalid");
                                 }
                               } catch (...) {
                                 triggerToast("canvas: invalid");
                               }
                             });
      } else if (sb.action == 248) {
        const Deck& deck = focusedDeck();
        std::string initial = std::to_string(deck.canvasViewX) + "," + std::to_string(deck.canvasViewY);
        openInlineTextEditor("settings.canvas_view", "Canvas View",
                             "X,Y offset in pixels", initial,
                             [this](const std::string& rawValue) {
                               std::string token = trim(rawValue);
                               size_t split = token.find(',');
                               if (split == std::string::npos) {
                                 split = token.find(' ');
                               }
                               if (split == std::string::npos || split == 0 || split + 1 >= token.size()) {
                                 triggerToast("view: invalid");
                                 return;
                               }
                               try {
                                 int x = std::stoi(token.substr(0, split));
                                 int y = std::stoi(token.substr(split + 1));
                                 setFocusedDeckCanvasView(x, y);
                               } catch (...) {
                                 triggerToast("view: invalid");
                               }
                             });
      } else if (sb.action == 249) {
        toggleFocusedDeckWarpEnabled();
      } else if (sb.action == kSettingsActionOutputWarpModeCycle) {
        cycleFocusedDeckWarpMode(1);
      } else {
        handleSettingsClickPart2(sb);
      }
      return;
    }
    // Click outside modal = close
    SDL_Rect modal = settingsModalRect();
    if (!pointInRect(mx, my, modal)) settingsOpen_ = false;
  }

  // Second half of the settings-click handler, split off to keep the
  // if-else-if chain short enough for MSVC's block-nesting limit.
  void handleSettingsClickPart2(const SettingsButton& sb) {
    if (sb.action == 250) {
        cycleFocusedOutput(-1);
      } else if (sb.action == 251) {
        cycleFocusedOutput(1);
      } else if (sb.action == 252) {
        addOutput(project_.focusedDeckIndex);
      } else if (sb.action == 275) {
        addOutput(project_.focusedDeckIndex, "window");
      } else if (sb.action == 276) {
        addOutput(project_.focusedDeckIndex, "stream");
      } else if (sb.action == kSettingsActionOutputRemove) {
        removeOutput(project_.focusedOutputIndex);
      } else if (sb.action == 254) {
        setFocusedOutputHostDeck(project_.focusedDeckIndex);
      } else if (sb.action == 255) {
        toggleFocusedOutputStreamEnabled();
      } else if (sb.action == 256) {
        openDropdown(
          "settings.stream_proto",
          sb.rect,
          outputStreamProtocolDropdownChoices(),
          normalizeOutputStreamProtocol(focusedOutput().streamProtocol),
          [this](const std::string& value) {
            setFocusedOutputStreamProtocol(value);
          });
        return;
      } else if (sb.action == 257) {
        const OutputTarget& output = focusedOutput();
        std::string initial = trim(output.streamUrl).empty()
          ? defaultOutputStreamUrl(output.streamProtocol, project_.focusedOutputIndex)
          : output.streamUrl;
        openInlineTextEditor("settings.stream_url", "Stream URL",
                             "srt://... or rtmp://...", initial,
                             [this](const std::string& value) {
                               setFocusedOutputStreamUrl(value);
                             });
      } else if (sb.action == kSettingsActionStreamKeyPrompt) {
        settingsOpen_ = false;
        openInlineTextEditor("settings.stream_key", "Stream Key",
                             "paste your stream key here", focusedOutput().streamKey,
                             [this](const std::string& value) {
                               if (project_.focusedOutputIndex >= 0 &&
                                   project_.focusedOutputIndex < static_cast<int>(project_.outputs.size())) {
                                 project_.outputs[project_.focusedOutputIndex].streamKey = trim(value);
                                 markProjectDirty();
                               }
                             });
      } else if (sb.action == 258) {
        const OutputTarget& output = focusedOutput();
        openInlineTextEditor("settings.stream_bitrate", "Stream Bitrate",
                             "kbps (500-50000)", std::to_string(output.streamBitrateKbps),
                             [this](const std::string& value) {
                               try {
                                 setFocusedOutputStreamBitrateKbps(std::stoi(trim(value)));
                               } catch (...) {
                                 triggerToast("bitrate: invalid");
                               }
                             });
      } else if (sb.action == 259) {
        const OutputTarget& output = focusedOutput();
        std::string nextType = normalizeOutputType(output.outputType) == "stream" ? "window" : "stream";
        setFocusedOutputType(nextType);
      } else if (sb.action == 281) {
        setFocusedOutputType("window");
      } else if (sb.action == 282) {
        setFocusedOutputType("stream");
      } else if (sb.action == kSettingsActionOutputOverlayToggle) {
        toggleFocusedOutputTimeOverlayEnabled();
      } else if (sb.action == kSettingsActionOutputAlphaPrompt) {
        int initialPct = static_cast<int>(std::lround(std::clamp(focusedOutput().outputAlpha, 0.0f, 1.0f) * 100.0f));
        openInlineTextEditor("settings.output_alpha", "Output Alpha",
                             "Percent 0-100", std::to_string(initialPct),
                             [this](const std::string& value) {
                               try {
                                 setFocusedOutputAlpha(static_cast<float>(std::stod(trim(value)) / 100.0));
                               } catch (...) {
                                 triggerToast("alpha: invalid");
                               }
                             });
      } else if (sb.action == kSettingsActionOutputDelayPrompt) {
        openInlineTextEditor("settings.output_delay", "Output Delay",
                             "Milliseconds 0-5000", std::to_string(focusedOutput().outputDelayMs),
                             [this](const std::string& value) {
                               try {
                                 setFocusedOutputDelayMs(std::stoi(trim(value)));
                               } catch (...) {
                                 triggerToast("delay: invalid");
                               }
                             });
      } else if (sb.action == kSettingsActionOutputColorSpaceCycle) {
        cycleFocusedOutputColorSpace(1);
      } else if (sb.action == kSettingsActionOutputDelayInc) {
        setFocusedOutputDelayMs(focusedOutput().outputDelayMs + 100);
      } else if (sb.action == kSettingsActionOutputLayoutSpan) {
        setFocusedOutputLayoutMode("span");
      } else if (sb.action == kSettingsActionOutputLayoutDuplicate) {
        setFocusedOutputLayoutMode("duplicate");
      } else if (sb.action == kSettingsActionOutputOrientationCycle) {
        cycleFocusedOutputOrientation(1);
      } else if (sb.action == kSettingsActionOutputTestCardToggle) {
        toggleFocusedOutputTestCardEnabled();
      } else if (sb.action == kSettingsActionOutputTestCardAllToggle) {
        bool anyOff = false;
        for (const auto& out : project_.outputs) {
          if (!out.outputTestCardEnabled) {
            anyOff = true;
            break;
          }
        }
        setAllOutputsTestCardEnabled(anyOff);
      } else if (sb.action >= kSettingsActionOutputEdgeBlendLInc && sb.action <= kSettingsActionOutputEdgeBlendBDec) {
        Deck& bd = focusedDeckMutable();
        float step = 0.02f;
        switch (sb.action) {
          case kSettingsActionOutputEdgeBlendLInc: bd.edgeBlendLeft  = std::clamp(bd.edgeBlendLeft  + step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendLDec: bd.edgeBlendLeft  = std::clamp(bd.edgeBlendLeft  - step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendRInc: bd.edgeBlendRight = std::clamp(bd.edgeBlendRight + step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendRDec: bd.edgeBlendRight = std::clamp(bd.edgeBlendRight - step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendTInc: bd.edgeBlendTop   = std::clamp(bd.edgeBlendTop   + step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendTDec: bd.edgeBlendTop   = std::clamp(bd.edgeBlendTop   - step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendBInc: bd.edgeBlendBottom= std::clamp(bd.edgeBlendBottom+ step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendBDec: bd.edgeBlendBottom= std::clamp(bd.edgeBlendBottom- step, 0.0f, 0.49f); break;
          default: break;
        }
        normalizeDeck(bd, project_.focusedDeckIndex);
        markProjectDirty();
      } else if (sb.action >= kSettingsActionOutputAoiXInc && sb.action <= kSettingsActionOutputAoiReset) {
        AoiRectPx aoi = focusedOutputAoiRectPx();
        // Nudge step: 16px snaps to common raster grids; Shift is not
        // routed here, so keep one predictable increment.
        constexpr int kAoiStepPx = 16;
        switch (sb.action) {
          case kSettingsActionOutputAoiXInc: applyFocusedOutputAoiRectPx(aoi.x + kAoiStepPx, aoi.y, aoi.w, aoi.h); break;
          case kSettingsActionOutputAoiXDec: applyFocusedOutputAoiRectPx(aoi.x - kAoiStepPx, aoi.y, aoi.w, aoi.h); break;
          case kSettingsActionOutputAoiYInc: applyFocusedOutputAoiRectPx(aoi.x, aoi.y + kAoiStepPx, aoi.w, aoi.h); break;
          case kSettingsActionOutputAoiYDec: applyFocusedOutputAoiRectPx(aoi.x, aoi.y - kAoiStepPx, aoi.w, aoi.h); break;
          case kSettingsActionOutputAoiWInc: applyFocusedOutputAoiRectPx(aoi.x, aoi.y, aoi.w + kAoiStepPx, aoi.h); break;
          case kSettingsActionOutputAoiWDec: applyFocusedOutputAoiRectPx(aoi.x, aoi.y, aoi.w - kAoiStepPx, aoi.h); break;
          case kSettingsActionOutputAoiHInc: applyFocusedOutputAoiRectPx(aoi.x, aoi.y, aoi.w, aoi.h + kAoiStepPx); break;
          case kSettingsActionOutputAoiHDec: applyFocusedOutputAoiRectPx(aoi.x, aoi.y, aoi.w, aoi.h - kAoiStepPx); break;
          case kSettingsActionOutputAoiReset: {
            OutputTarget& ot = focusedOutputMutable();
            ot.aoiLeft = ot.aoiRight = ot.aoiTop = ot.aoiBottom = 0.0f;
            markProjectDirty();
            triggerToast("aoi: full raster");
            break;
          }
          default: break;
        }
      } else if (sb.action == kSettingsActionOutputAoiXEdit) {
        AoiRectPx aoi = focusedOutputAoiRectPx();
        openAoiValueEditor("settings.aoi_x", "AOI X Position", aoi.x,
                           [this, aoi](int px) { applyFocusedOutputAoiRectPx(px, aoi.y, aoi.w, aoi.h); });
      } else if (sb.action == kSettingsActionOutputAoiYEdit) {
        AoiRectPx aoi = focusedOutputAoiRectPx();
        openAoiValueEditor("settings.aoi_y", "AOI Y Position", aoi.y,
                           [this, aoi](int px) { applyFocusedOutputAoiRectPx(aoi.x, px, aoi.w, aoi.h); });
      } else if (sb.action == kSettingsActionOutputAoiWEdit) {
        AoiRectPx aoi = focusedOutputAoiRectPx();
        openAoiValueEditor("settings.aoi_w", "AOI Width", aoi.w,
                           [this, aoi](int px) { applyFocusedOutputAoiRectPx(aoi.x, aoi.y, px, aoi.h); });
      } else if (sb.action == kSettingsActionOutputAoiHEdit) {
        AoiRectPx aoi = focusedOutputAoiRectPx();
        openAoiValueEditor("settings.aoi_h", "AOI Height", aoi.h,
                           [this, aoi](int px) { applyFocusedOutputAoiRectPx(aoi.x, aoi.y, aoi.w, px); });
      } else if (sb.action == kSettingsActionCanvasToggle) {
        project_.outputCanvasEnabled = !project_.outputCanvasEnabled;
        markProjectDirty();
        triggerToast(project_.outputCanvasEnabled ? "canvas on" : "canvas off");
      } else if (sb.action == kSettingsActionCanvasWidthPrompt) {
        settingsOpen_ = false;
        openInlineCanvasWidthEditor();
      } else if (sb.action == kSettingsActionCanvasHeightPrompt) {
        settingsOpen_ = false;
        openInlineCanvasHeightEditor();
      } else if (sb.action == kSettingsActionThemeDropdown) {
        // Scan for available themes
        std::vector<std::pair<std::string, std::string>> choices;
        fs::path themesDir = Paths::dataDir() / "themes";
        if (fs::is_directory(themesDir)) {
          for (auto& entry : fs::directory_iterator(themesDir)) {
            if (entry.is_directory() && fs::exists(entry.path() / "theme.txt")) {
              std::string name = entry.path().filename().string();
              choices.push_back({name, name});
            }
          }
        }
        std::sort(choices.begin(), choices.end());
        std::string current = currentThemeName_.empty() ? "gameboy" : currentThemeName_;
        openDropdown("settings.theme", sb.rect, choices, current,
          [this](const std::string& value) {
            loadTheme(value);
            project_.theme = value;
            markProjectDirty();
            triggerToast("theme: " + value);
          });
        return;
      } else if (sb.action == kSettingsActionTransitionSecondsDec) {
        setTransitionSeconds(std::max(0.0, focusedDeck().transitionSeconds - 0.1));
      } else if (sb.action == kSettingsActionTransitionSecondsInc) {
        setTransitionSeconds(focusedDeck().transitionSeconds + 0.1);
      } else if (sb.action == kSettingsActionTransitionStyleCycle) {
        const Deck& td = focusedDeck();
        TransitionStyle current = parseTransitionStyleToken(td.transitionStyle);
        TransitionStyle next = current == TransitionStyle::Cut ? TransitionStyle::Crossfade
                             : current == TransitionStyle::Crossfade ? TransitionStyle::DipBlack
                             : TransitionStyle::Cut;
        setTransitionStyle(next);
      } else if (sb.action == 260) {
        openDropdown(
          "settings.output_mirror",
          sb.rect,
          outputMirrorSourceDropdownChoices(),
          std::to_string(focusedOutput().mirrorSourceOutputIndex),
          [this](const std::string& value) {
            try {
              setFocusedOutputMirrorSource(std::stoi(trim(value)));
            } catch (...) {
              triggerToast("mirror: invalid");
            }
          });
        return;
      } else if (sb.action == 271) {
        // setFocusedOutputNdiEnabled gates on ndiRuntimeAvailable() and shows
        // the dependency prompt itself, so we can route through unconditionally.
        setFocusedOutputNdiEnabled(!focusedOutput().ndiEnabled);
      } else if (sb.action == 272) {
        const OutputTarget& output = focusedOutput();
        std::string initial = trim(output.ndiSourceName).empty()
          ? defaultOutputNdiSourceName(output, project_.focusedOutputIndex)
          : output.ndiSourceName;
        openInlineTextEditor("settings.ndi_name", "NDI Name",
                             "Sender source name", initial,
                             [this](const std::string& value) {
                               setFocusedOutputNdiName(value);
                             });
      } else if (sb.action == 273) {
        setFocusedOutputNdiKeyEnabled(!focusedOutput().ndiKeyEnabled);
      } else if (sb.action == 274) {
        const OutputTarget& output = focusedOutput();
        std::string initial = trim(output.ndiKeySourceName).empty()
          ? defaultOutputNdiKeySourceName(output, project_.focusedOutputIndex)
          : output.ndiKeySourceName;
        openInlineTextEditor("settings.ndi_key_name", "NDI Key Name",
                             "Key sender source name", initial,
                             [this](const std::string& value) {
                               setFocusedOutputNdiKeyName(value);
                             });
      } else {
        handleSettingsClickPart3(sb);
      }
  }

  // Third part of the settings-click handler, split off to keep the
  // if-else-if chain short enough for MSVC's block-nesting limit.
  void handleSettingsClickPart3(const SettingsButton& sb) {
    if (sb.action == kSettingsActionDisplayIdentify) {
        showDisplayIdentify();
        triggerToast("identifying displays");
        playUiSound(UiSoundEffect::Toggle);
      } else if (sb.action == kSettingsActionDeckLinkToggle) {
        // setFocusedOutputDeckLinkEnabled gates on deckLinkRuntimeAvailable()
        // and runs the toast / shutdown path internally.
        toggleFocusedOutputDeckLink();
      } else if (sb.action == kSettingsActionDeckLinkDeviceDropdown) {
        auto devices = deckboy::platform::video::DeckLinkOutput::listDevices();
        std::vector<std::pair<std::string, std::string>> choices;
        choices.push_back({"-1", "None"});
        for (const auto& d : devices) {
          std::string label = d.modelName;
          if (d.supportsSDI && d.supportsHDMI) label += " (SDI+HDMI)";
          else if (d.supportsSDI) label += " (SDI)";
          else if (d.supportsHDMI) label += " (HDMI)";
          if (d.supports4K) label += " 4K";
          choices.push_back({std::to_string(d.id), label});
        }
        openDropdown("settings.decklink_device", sb.rect, choices,
          std::to_string(focusedOutput().deckLinkDeviceId),
          [this](const std::string& value) {
            OutputTarget& output = focusedOutputMutable();
            int newId = -1;
            try { newId = std::stoi(trim(value)); } catch (...) {}
            if (output.deckLinkDeviceId != newId) {
              output.deckLinkDeviceId = newId;
              // Shutdown so it re-inits with the new device
              auto& rt = outputRuntimes_[project_.focusedOutputIndex];
              shutdownOutputDeckLink(rt);
              markProjectDirty();
            }
          });
        return;
      } else if (sb.action == kSettingsActionDeckLinkModeDropdown) {
        using DLM = deckboy::platform::video::DeckLinkMode;
        static constexpr DLM allModes[] = {
          DLM::HD720p50, DLM::HD720p5994, DLM::HD720p60,
          DLM::HD1080i50, DLM::HD1080i5994, DLM::HD1080i60,
          DLM::HD1080p2398, DLM::HD1080p24, DLM::HD1080p25,
          DLM::HD1080p2997, DLM::HD1080p30, DLM::HD1080p50,
          DLM::HD1080p5994, DLM::HD1080p60,
          DLM::UHD2160p2398, DLM::UHD2160p24, DLM::UHD2160p25,
          DLM::UHD2160p2997, DLM::UHD2160p30, DLM::UHD2160p50,
          DLM::UHD2160p5994, DLM::UHD2160p60,
        };
        std::vector<std::pair<std::string, std::string>> choices;
        for (auto m : allModes) {
          std::string token = deckboy::platform::video::deckLinkModeToken(m);
          std::string label = deckboy::platform::video::deckLinkModeLabel(m);
          choices.push_back({token, label});
        }
        openDropdown("settings.decklink_mode", sb.rect, choices,
          focusedOutput().deckLinkMode,
          [this](const std::string& value) {
            OutputTarget& output = focusedOutputMutable();
            if (output.deckLinkMode != value) {
              output.deckLinkMode = value;
              // Shutdown so it re-inits with the new mode
              auto& rt = outputRuntimes_[project_.focusedOutputIndex];
              shutdownOutputDeckLink(rt);
              markProjectDirty();
            }
          });
        return;
      } else if (sb.action == kSettingsActionDeckLink10BitToggle) {
        OutputTarget& output = focusedOutputMutable();
        output.deckLink10Bit = !output.deckLink10Bit;
        // Shutdown so it re-inits with the new bit depth
        auto& rt = outputRuntimes_[project_.focusedOutputIndex];
        shutdownOutputDeckLink(rt);
        triggerToast(std::string("decklink 10-bit: ") + (output.deckLink10Bit ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionSpoutToggle) {
        OutputTarget& output = focusedOutputMutable();
        output.spoutEnabled = !output.spoutEnabled;
        if (!output.spoutEnabled) {
          auto& rt = outputRuntimes_[project_.focusedOutputIndex];
          shutdownOutputSpout(rt);
        }
        triggerToast(std::string("spout: ") + (output.spoutEnabled ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionSpoutNamePrompt) {
        const OutputTarget& output = focusedOutput();
        std::string initial = trim(output.spoutSenderName);
        if (initial.empty()) {
          initial = "Deckboy Output " + std::to_string(project_.focusedOutputIndex + 1);
        }
        openInlineTextEditor("settings.spout_name", "Spout Sender Name",
                             "Name visible to receivers", initial,
                             [this](const std::string& value) {
                               OutputTarget& output = focusedOutputMutable();
                               output.spoutSenderName = trim(value);
                               // Shutdown so it re-inits with the new name
                               auto& rt = outputRuntimes_[project_.focusedOutputIndex];
                               shutdownOutputSpout(rt);
                               markProjectDirty();
                             });
      } else if (sb.action == kSettingsActionMascotToggle) {
        // Dropdown: pick the splash mascot. refreshSplashAsset re-resolves the
        // path from the chosen character and lazy-reloads the texture.
        std::vector<std::pair<std::string, std::string>> choices = {
          {"deckbot",  "Deckbot  (default)"},
          {"deckgirl", "Deckgirl"},
        };
        openDropdown("settings.mascot", sb.rect, choices, project_.splashCharacter,
          [this](const std::string& value) {
            project_.splashCharacter = value;
            refreshSplashAsset();
            triggerToast(std::string("mascot: ") + project_.splashCharacter);
            markProjectDirty();
          });
      } else if (sb.action == kSettingsActionUiScaleDropdown) {
        std::vector<std::pair<std::string, std::string>> choices = {
          {"1.00", "1.00x  (default 1080p)"},
          {"1.25", "1.25x  (large desktop)"},
          {"1.50", "1.50x  (4K desktop)"},
          {"2.00", "2.00x  (Pocket 3 / 4K small)"},
        };
        char current[16];
        snprintf(current, sizeof(current), "%.2f", project_.uiScale);
        openDropdown("settings.ui_scale", sb.rect, choices, current,
          [this](const std::string& value) {
            double v = 1.0;
            try { v = std::stod(value); } catch (...) {}
            project_.uiScale = std::clamp(v, 0.75, 3.0);
            applyUiScale();
            triggerToast("ui scale: " + value + "x");
            markProjectDirty();
          });
      } else if (sb.action == kSettingsActionPocket3Preset) {
        // Toggle the Pocket 3 preset on/off. Currently bundles:
        //   - uiScale flip between 1.0 and 2.0
        //   - interactionMode flip between "mouse" and "touch" (skips hover
        //     affordances that don't translate to a tap-only input model)
        // Any future tap-friendly layout tweaks should land in this block so
        // one click flips the whole ergonomic profile in lockstep.
        bool active = std::abs(project_.uiScale - 2.0) < 0.01 &&
                      project_.interactionMode == "touch";
        if (active) {
          project_.uiScale = 1.0;
          project_.interactionMode = "mouse";
        } else {
          project_.uiScale = 2.0;
          project_.interactionMode = "touch";
        }
        applyUiScale();
        triggerToast(active ? "pocket 3 preset: off" : "pocket 3 preset: on");
        markProjectDirty();
      } else if (sb.action == kSettingsActionOutputAdvancedToggle) {
        videoOutputsAdvanced_ = !videoOutputsAdvanced_;
      } else if (sb.action == kSettingsActionOutputToggle) {
        toggleFocusedOutputEnabled();
      } else if (sb.action == kSettingsActionOutputDisplayPrev) {
        cycleOutputDisplay(-1);
      } else if (sb.action == kSettingsActionOutputDisplayNext) {
        cycleOutputDisplay(1);
      } else if (sb.action == kSettingsActionOutputDisplayRescan) {
        // Manual RESCAN is the operator saying "put the outputs back where
        // they belong" — force the re-home even when the fingerprints match.
        refreshDisplayTopology(true, true);
      } else if (sb.action == kSettingsActionOutputDisplayDropdown) {
        openDropdown(
          "settings.output_display",
          sb.rect,
          outputDisplayDropdownChoices(),
          std::to_string(outputDisplayIndex(project_.focusedOutputIndex)),
          [this](const std::string& value) {
            try {
              int displayIndex = std::stoi(trim(value));
              if (displayIndex >= 0) {
                setOutputDisplayIndex(displayIndex);
              }
            } catch (...) {
            }
          });
        return;
      } else if (sb.action >= kSettingsActionOutputDisplaySelectBase &&
                 sb.action < kSettingsActionOutputDisplaySelectBase + 32) {
        int selectedDisplay = sb.action - kSettingsActionOutputDisplaySelectBase;
        setOutputDisplayIndex(selectedDisplay);
      } else if (sb.action == kSettingsActionOutputStreamProtocolDropdown) {
        openDropdown(
          "settings.stream_proto",
          sb.rect,
          outputStreamProtocolDropdownChoices(),
          normalizeOutputStreamProtocol(focusedOutput().streamProtocol),
          [this](const std::string& value) {
            setFocusedOutputStreamProtocol(value);
          });
        return;
      } else if (sb.action == kSettingsActionOutputMirrorDropdown) {
        openDropdown(
          "settings.output_mirror",
          sb.rect,
          outputMirrorSourceDropdownChoices(),
          std::to_string(focusedOutput().mirrorSourceOutputIndex),
          [this](const std::string& value) {
            try {
              setFocusedOutputMirrorSource(std::stoi(trim(value)));
            } catch (...) {
              triggerToast("mirror: invalid");
            }
          });
        return;
      } else if (sb.action >= kSettingsActionOutputDisplayFocusBase &&
                 sb.action < kSettingsActionOutputDisplayFocusBase + 64) {
        int displayIndex = sb.action - kSettingsActionOutputDisplayFocusBase;
        setOutputDisplayIndex(displayIndex);
      } else if (sb.action >= kSettingsActionRoutingTableOutputPrevBase &&
                 sb.action < kSettingsActionRoutingTableOutputPrevBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableOutputPrevBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        int nextOutput = (routeOutput - 1 + outputCount) % outputCount;
        moveDeckToOutput(deckIndex, nextOutput);
        setFocusedOutputIndex(nextOutput);
      } else if (sb.action >= kSettingsActionRoutingTableOutputNextBase &&
                 sb.action < kSettingsActionRoutingTableOutputNextBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableOutputNextBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        int nextOutput = (routeOutput + 1) % outputCount;
        moveDeckToOutput(deckIndex, nextOutput);
        setFocusedOutputIndex(nextOutput);
      } else if (sb.action >= kSettingsActionRoutingTableLayerDecBase &&
                 sb.action < kSettingsActionRoutingTableLayerDecBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableLayerDecBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (!assignmentIndex) {
          assignDeckToOutput(deckIndex, routeOutput);
          assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        }
        if (!assignmentIndex) {
          return;
        }
        int currentLayer = 0; // Single-deck: always layer 0
        setDeckOutputAssignmentLayer(deckIndex, routeOutput, currentLayer - 1);
      } else if (sb.action >= kSettingsActionRoutingTableLayerIncBase &&
                 sb.action < kSettingsActionRoutingTableLayerIncBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableLayerIncBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (!assignmentIndex) {
          assignDeckToOutput(deckIndex, routeOutput);
          assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        }
        if (!assignmentIndex) {
          return;
        }
        int currentLayer = 0; // Single-deck: always layer 0
        setDeckOutputAssignmentLayer(deckIndex, routeOutput, currentLayer + 1);
      } else if (sb.action >= kSettingsActionRoutingTableAssignToggleBase &&
                 sb.action < kSettingsActionRoutingTableAssignToggleBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableAssignToggleBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (assignmentIndex) {
          unassignDeckFromOutput(deckIndex, routeOutput);
        } else {
          assignDeckToOutput(deckIndex, routeOutput);
        }
      } else if (sb.action == kSettingsActionRoutingLayerDec ||
                 sb.action == kSettingsActionRoutingLayerInc) {
        int deckIndex = std::clamp(project_.focusedDeckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
        int outputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, outputIndex);
        if (!assignmentIndex) {
          triggerToast("assign route first");
        } else {
          int currentLayer = 0; // Single-deck: always layer 0
          bool shiftHeld = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
          bool ctrlHeld = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
          int step = ctrlHeld ? 10 : 1;
          int delta = (sb.action == kSettingsActionRoutingLayerDec) ? -step : step;
          if (shiftHeld) {
            delta = -delta;
          }
          setDeckOutputAssignmentLayer(deckIndex, outputIndex, currentLayer + delta);
        }
      } else if (sb.action == kSettingsActionRoutingAssignToggle) {
        int deckIndex = std::clamp(project_.focusedDeckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
        int outputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, outputIndex);
        if (assignmentIndex) {
          unassignDeckFromOutput(deckIndex, outputIndex);
        } else {
          assignDeckToOutput(deckIndex, outputIndex);
        }
      } else if (sb.action == kSettingsActionRoutingModeToggle) {
        routingMoveMode_ = !routingMoveMode_;
        triggerToast(std::string("routing mode: ") + (routingMoveMode_ ? "Move" : "Add"));
        playUiSound(UiSoundEffect::Toggle);
      } else if (sb.action >= kSettingsActionRoutingDeckFocusBase &&
                 sb.action < kSettingsActionRoutingDeckFocusBase + static_cast<int>(project_.decks.size())) {
        int deckIndex = sb.action - kSettingsActionRoutingDeckFocusBase;
        setFocusedDeckIndex(deckIndex);
      } else if (sb.action >= kSettingsActionRoutingOutputFocusBase &&
                 sb.action < kSettingsActionRoutingOutputFocusBase + static_cast<int>(project_.outputs.size())) {
        int outputIndex = sb.action - kSettingsActionRoutingOutputFocusBase;
        setFocusedOutputIndex(outputIndex);
      } else if (sb.action >= kSettingsActionRoutingCellBase) {
        int packed = sb.action - kSettingsActionRoutingCellBase;
        int deckIndex = packed / kSettingsActionRoutingCellStride;
        int outputIndex = packed % kSettingsActionRoutingCellStride;
        if (deckIndex >= 0 && deckIndex < static_cast<int>(project_.decks.size()) &&
            outputIndex >= 0 && outputIndex < static_cast<int>(project_.outputs.size())) {
          setFocusedDeckIndex(deckIndex);
          setFocusedOutputIndex(outputIndex);
          auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, outputIndex);
          bool shiftHeld = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
          bool ctrlHeld = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
          if (routingMoveMode_) {
            if (assignmentIndex) {
              int currentLayer = 0; // Single-deck: always layer 0
              int step = ctrlHeld ? 10 : 1;
              int delta = shiftHeld ? -step : step;
              setDeckOutputAssignmentLayer(deckIndex, outputIndex, currentLayer + delta);
            } else {
              moveDeckToOutput(deckIndex, outputIndex);
            }
          } else {
            if (assignmentIndex) {
              if (shiftHeld || ctrlHeld) {
                int currentLayer = 0; // Single-deck: always layer 0
                int step = ctrlHeld ? 10 : 1;
                int delta = shiftHeld ? -step : step;
                setDeckOutputAssignmentLayer(deckIndex, outputIndex, currentLayer + delta);
              } else {
                unassignDeckFromOutput(deckIndex, outputIndex);
              }
            } else {
              assignDeckToOutput(deckIndex, outputIndex);
            }
          }
        }
      } else if (sb.action >= kSettingsActionVideoSubTabBase &&
                 sb.action < kSettingsActionVideoSubTabBase + 4) {
        settingsVideoSubTab_ = sb.action - kSettingsActionVideoSubTabBase;
      }
  }
