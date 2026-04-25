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

    // Title
    drawTextSafe(controlRenderer_, fontBase_,
                 SDL_Rect {modal.x + 16, modal.y + 10, modal.w - 64, 20},
                 "SYSTEM SETTINGS", pal.deep);

    // Close button [X]
    settingsCloseBtn_ = {modal.x + modal.w - 42, modal.y + 6, 34, 30};
    Primitives::drawFramedPanel(controlRenderer_, settingsCloseBtn_, pal.mid, pal.deep, pal.light);
    drawCenteredText(controlRenderer_, fontSmall_, "X", pal.deep, settingsCloseBtn_);

    // Tab bar
    constexpr int kTabW = 118;
    constexpr int kTabH = 36;
    int tabY = modal.y + 44;
    settingsBtns_.clear();
    const std::vector<std::string> tabs {"System", "Audio", "Network", "Video Outputs", "About"};
    for (int t = 0; t < (int)tabs.size(); ++t) {
      SDL_Rect tab {modal.x + 16 + t * (kTabW + 4), tabY, kTabW, kTabH};
      bool active = (t == settingsTab_);
      Primitives::drawFramedPanel(controlRenderer_, tab, active ? pal.dark : pal.light,
                      pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, tab, tabs[t],
                           active ? pal.light : pal.deep);
      settingsBtns_.push_back({tab, 100 + t, tabs[t]});
    }

    // Content area
    SDL_Rect content {modal.x + 16, tabY + kTabH + 10, modal.w - 32, modal.h - kTabH - 82};
    Primitives::drawFramedPanel(controlRenderer_, content, pal.light, pal.deep, pal.mid);

    int cx = content.x + 12, cy = content.y + 10;
    SDL_Color ink = pal.deep;
    SDL_Color soft = pal.inkSoft;

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
        Primitives::drawFramedPanel(controlRenderer_, rect, pal.shellInner,
                                    pal.deep, pal.light);
        drawText(controlRenderer_, fontBase_, title, ink, rect.x + 8, rect.y + 6);
        if (!subtitle.empty()) {
          drawText(controlRenderer_, fontSmall_,
                   ellipsizeToPixelWidth(fontSmall_, subtitle, rect.w - 16),
                   soft, rect.x + 8, rect.y + 28);
        }
      };

      int colGap = 12;
      int colH = content.h - 20;
      int leftW = std::max(320, (content.w - 12 - colGap) / 2);
      int rightW = std::max(320, content.w - 12 - colGap - leftW);
      SDL_Rect leftCol {cx, cy, leftW, colH};
      SDL_Rect rightCol {cx + leftW + colGap, cy, rightW, colH};
      constexpr int kCardGap = 10;

      int leftY = leftCol.y;
      SDL_Rect appearanceRect {leftCol.x, leftY, leftCol.w, 144};
      leftY += appearanceRect.h + kCardGap;
      SDL_Rect safetyRect {leftCol.x, leftY, leftCol.w, 152};
      leftY += safetyRect.h + kCardGap;
      SDL_Rect flowRect {leftCol.x, leftY, leftCol.w, std::max(116, leftCol.y + leftCol.h - leftY)};

      int rightY = rightCol.y;
      SDL_Rect cueToolsRect {rightCol.x, rightY, rightCol.w, 138};
      rightY += cueToolsRect.h + kCardGap;
      SDL_Rect prefsRect {rightCol.x, rightY, rightCol.w, std::max(180, rightCol.y + rightCol.h - rightY)};

      drawCard(appearanceRect, "APPEARANCE", "Theme and operator feedback");
      std::string themeName = currentThemeName_.empty() ? "gameboy" : currentThemeName_;
      SDL_Rect themeBtn {appearanceRect.x + 8, appearanceRect.y + 54, appearanceRect.w - 16, 30};
      drawUIDropdown(themeBtn, "Theme", themeName, "settings.theme");
      settingsBtns_.push_back({themeBtn, kSettingsActionThemeDropdown, "theme"});
      int appearanceToggleW = std::max(100, (appearanceRect.w - 24) / 2);
      SDL_Rect sfxBtn {appearanceRect.x + 8, appearanceRect.y + 92, appearanceToggleW, 24};
      SDL_Rect animBtn {sfxBtn.x + appearanceToggleW + 8, sfxBtn.y, appearanceRect.w - 16 - appearanceToggleW - 8, 24};
      drawPillToggle(sfxBtn, project_.uiSoundsEnabled, "SFX ON", "SFX OFF");
      drawUIPanel(animBtn, pal.light,
                  pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, animBtn, "UI MOTION", pal.deep);
      settingsBtns_.push_back({sfxBtn, 201, "sfx_toggle"});
      const int appearanceFootY = sfxBtn.y + sfxBtn.h + 8;
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, "Theme switching lives here; UI motion stays on; audio routing lives in Audio.", appearanceRect.w - 16),
               soft, appearanceRect.x + 8, appearanceFootY);

      drawCard(safetyRect, "SAFETY / TIMECODE", "Emergency fade and sync behavior");
      const int panicLabelY = safetyRect.y + 56;
      drawText(controlRenderer_, fontSmall_, "panic fade", soft, safetyRect.x + 8, panicLabelY);
      const int panicRowY = rowYBelowLabel(panicLabelY, fontSmall_, 4);
      SDL_Rect panicFadeDecBtn {safetyRect.x + 8, panicRowY, 26, 22};
      SDL_Rect panicFadeValRect {panicFadeDecBtn.x + 30, panicFadeDecBtn.y, 72, 22};
      SDL_Rect panicFadeIncBtn {panicFadeValRect.x + panicFadeValRect.w + 4, panicFadeDecBtn.y, 26, 22};
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
      SDL_Rect panicRestoreBtn {panicFadeIncBtn.x + panicFadeIncBtn.w + 8, panicFadeDecBtn.y,
                                safetyRect.x + safetyRect.w - 8 - (panicFadeIncBtn.x + panicFadeIncBtn.w + 8), 22};
      drawPillToggle(panicRestoreBtn, project_.panicAutoRestore, "AUTO RESTORE ON", "AUTO RESTORE OFF");
      settingsBtns_.push_back({panicRestoreBtn, 212, "panic_restore_toggle"});

      const int tcJamY = panicRowY + 22 + 8;
      SDL_Rect tcJamBtn {safetyRect.x + 8, tcJamY, 140, 22};
      SDL_Rect tcFwDecBtn {tcJamBtn.x + tcJamBtn.w + 8, tcJamBtn.y, 26, 22};
      SDL_Rect tcFwValRect {tcFwDecBtn.x + 30, tcFwDecBtn.y, 72, 22};
      SDL_Rect tcFwIncBtn {tcFwValRect.x + tcFwValRect.w + 4, tcFwDecBtn.y, 26, 22};
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
      SDL_Rect jumpModeBtn {flowRect.x + 8, flowRect.y + 54, 150, 24};
      SDL_Rect jumpTransBtn {jumpModeBtn.x + jumpModeBtn.w + 8, jumpModeBtn.y,
                             flowRect.w - 16 - jumpModeBtn.w - 8, 24};
      Primitives::drawFramedPanel(controlRenderer_, jumpModeBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, jumpModeLabelFromToken(project_.jumpMode), ink, jumpModeBtn);
      settingsBtns_.push_back({jumpModeBtn, 203, "jump_mode"});
      drawPillToggle(jumpTransBtn, project_.jumpTransitionEnabled, "GLOBAL XFADE ON", "GLOBAL XFADE OFF");
      settingsBtns_.push_back({jumpTransBtn, 204, "jump_transition"});

      const int profileLabelY = flowRect.y + 94;
      drawText(controlRenderer_, fontSmall_, "panic profile", soft, flowRect.x + 8, profileLabelY);
      const int profileRowY = rowYBelowLabel(profileLabelY, fontSmall_, 4);
      SDL_Rect panicPrevBtn {flowRect.x + 8, profileRowY, 26, 24};
      SDL_Rect panicNextBtn {flowRect.x + flowRect.w - 34, profileRowY, 26, 24};
      SDL_Rect panicLabelRect {panicPrevBtn.x + panicPrevBtn.w + 6, panicPrevBtn.y,
                               panicNextBtn.x - (panicPrevBtn.x + panicPrevBtn.w + 12), 24};
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

      const int panicRunY = profileRowY + 24 + 10;
      SDL_Rect panicRunBtn {flowRect.x + 8, panicRunY, flowRect.w - 16, 26};
      Primitives::drawFramedPanel(controlRenderer_, panicRunBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Run Panic", ink, panicRunBtn);
      settingsBtns_.push_back({panicRunBtn, 207, "panic_run"});

      drawCard(cueToolsRect, "CUE TOOLS", "Find from the playlist, not from a modal");
      const int cueToolsLine1Y = cueToolsRect.y + 54;
      drawText(controlRenderer_, fontSmall_, "Use Ctrl+F or type a cue id/number to search.", soft, cueToolsRect.x + 8, cueToolsLine1Y);
      const int cueToolsLine2Y = rowYBelowLabel(cueToolsLine1Y, fontSmall_, 0);
      std::string findStatus = "find: none";
      if (!lastCueFindToken_.empty() && !lastCueFindMatches_.empty()) {
        int cursor = std::clamp(lastCueFindCursor_, 0, static_cast<int>(lastCueFindMatches_.size()) - 1);
        findStatus = "find \"" + lastCueFindToken_ + "\" " + std::to_string(cursor + 1) + "/" + std::to_string(lastCueFindMatches_.size());
      }
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, findStatus, cueToolsRect.w - 16),
               soft, cueToolsRect.x + 8, cueToolsLine2Y);
      const int renumberY = rowYBelowLabel(cueToolsLine2Y, fontSmall_, 6);
      SDL_Rect renumberBtn {cueToolsRect.x + 8, renumberY, cueToolsRect.w - 16, 24};
      Primitives::drawFramedPanel(controlRenderer_, renumberBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Renumber...", ink, renumberBtn);
      settingsBtns_.push_back({renumberBtn, 221, "cue_renumber"});

      drawCard(prefsRect, "PLAYLIST PREFERENCES", "Defaults for newly created cues");
      const Deck& prefDeck = focusedDeck();
      SDL_Rect prefsEditBtn {prefsRect.x + 8, prefsRect.y + 54, prefsRect.w - 16, 24};
      Primitives::drawFramedPanel(controlRenderer_, prefsEditBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Edit Timebase / Start / Fade / Duration...", ink, prefsEditBtn);
      settingsBtns_.push_back({prefsEditBtn, kSettingsActionPlaylistPrefsEdit, "playlist_prefs_edit"});

      std::string prefSummary = "tc " + playlistTimebaseLabel(prefDeck.playlistTimebaseFps)
        + "  start " + formatTimecode(prefDeck.playlistStartOffsetSeconds, prefDeck.playlistTimebaseFps)
        + "  fade " + formatSeconds(prefDeck.playlistDefaultCueFadeSeconds)
        + "  still " + formatSeconds(prefDeck.playlistDefaultStillDurationSeconds);
      const int prefSummaryY = prefsEditBtn.y + prefsEditBtn.h + 6;
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, prefSummary, prefsRect.w - 16),
               soft, prefsRect.x + 8, prefSummaryY);

      int toggleGap = 6;
      int toggleW = std::max(120, (prefsRect.w - 16 - toggleGap) / 2);
      int toggleH = 24;
      int toggleY = rowYBelowLabel(prefSummaryY, fontSmall_, 6);
      SDL_Rect loopT {prefsRect.x + 8, toggleY, toggleW, toggleH};
      SDL_Rect fadeInT {loopT.x + toggleW + toggleGap, toggleY,
                        prefsRect.x + prefsRect.w - 8 - (loopT.x + toggleW + toggleGap), toggleH};
      drawPillToggle(loopT, prefDeck.playlistDefaultLoop, "LOOP ON", "LOOP OFF");
      drawPillToggle(fadeInT, prefDeck.playlistDefaultFadeInEnabled, "FADE IN ON", "FADE IN OFF");
      settingsBtns_.push_back({loopT, kSettingsActionPlaylistDefaultLoopToggle, "playlist_default_loop"});
      settingsBtns_.push_back({fadeInT, kSettingsActionPlaylistDefaultFadeInToggle, "playlist_default_fadein"});

      int toggleY2 = toggleY + toggleH + 4;
      SDL_Rect fadeOutT {prefsRect.x + 8, toggleY2, toggleW, toggleH};
      SDL_Rect audioT {fadeOutT.x + toggleW + toggleGap, toggleY2,
                       prefsRect.x + prefsRect.w - 8 - (fadeOutT.x + toggleW + toggleGap), toggleH};
      drawPillToggle(fadeOutT, prefDeck.playlistDefaultFadeOutEnabled, "FADE OUT ON", "FADE OUT OFF");
      drawPillToggle(audioT, prefDeck.playlistDefaultAudioEnabled, "AUDIO ON", "AUDIO OFF");
      settingsBtns_.push_back({fadeOutT, kSettingsActionPlaylistDefaultFadeOutToggle, "playlist_default_fadeout"});
      settingsBtns_.push_back({audioT, kSettingsActionPlaylistDefaultAudioToggle, "playlist_default_audio"});

      int toggleY3 = toggleY2 + toggleH + 4;
      SDL_Rect pauseBeginT {prefsRect.x + 8, toggleY3, toggleW, toggleH};
      SDL_Rect pauseEndT {pauseBeginT.x + toggleW + toggleGap, toggleY3,
                          prefsRect.x + prefsRect.w - 8 - (pauseBeginT.x + toggleW + toggleGap), toggleH};
      drawPillToggle(pauseBeginT, prefDeck.playlistDefaultPauseAtBeginning, "PAUSE BEGIN ON", "PAUSE BEGIN OFF");
      drawPillToggle(pauseEndT, prefDeck.playlistDefaultPauseAtEnd, "PAUSE END ON", "PAUSE END OFF");
      settingsBtns_.push_back({pauseBeginT, kSettingsActionPlaylistDefaultPauseBeginToggle, "playlist_default_pausebegin"});
      settingsBtns_.push_back({pauseEndT, kSettingsActionPlaylistDefaultPauseEndToggle, "playlist_default_pauseend"});

      int toggleY4 = toggleY3 + toggleH + 4;
      SDL_Rect nextTransT {prefsRect.x + 8, toggleY4, prefsRect.w - 16, toggleH};
      drawPillToggle(nextTransT, prefDeck.playlistDefaultTransitionToNext, "NEXT TRANSITION ON", "NEXT TRANSITION OFF");
      settingsBtns_.push_back({nextTransT, kSettingsActionPlaylistDefaultNextTransitionToggle, "playlist_default_nexttrans"});

    } else if (settingsTab_ == 1) {
      auto drawCard = [&](const SDL_Rect& rect, const std::string& title, const std::string& subtitle = std::string()) {
        Primitives::drawFramedPanel(controlRenderer_, rect, pal.shellInner,
                                    pal.deep, pal.light);
        drawText(controlRenderer_, fontBase_, title, ink, rect.x + 8, rect.y + 6);
        if (!subtitle.empty()) {
          drawText(controlRenderer_, fontSmall_,
                   ellipsizeToPixelWidth(fontSmall_, subtitle, rect.w - 16),
                   soft, rect.x + 8, rect.y + 28);
        }
      };
      auto drawPillToggle = [&](const SDL_Rect& rect, bool on, const std::string& onLabel, const std::string& offLabel) {
        Primitives::drawFramedPanel(controlRenderer_, rect,
                        on ? pal.dark : pal.mid,
                        pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, on ? onLabel : offLabel,
                         on ? pal.light : pal.deep, rect);
      };

      SDL_Rect audioRect {cx, cy, content.w - 24, 144};
      SDL_Rect midiRect {cx, audioRect.y + audioRect.h + 10, content.w - 24,
                         std::max(180, content.y + content.h - 10 - (audioRect.y + audioRect.h + 10))};

      drawCard(audioRect, "AUDIO OUTPUT", "Device routing for cue playback");
      const int smallLineH = textLineHeight(fontSmall_) + 2;
      std::string devName = focusedDeck().audioOutputDeviceName.empty() ? "(default system device)" : focusedDeck().audioOutputDeviceName;
      SDL_Rect devBtn {audioRect.x + 8, audioRect.y + 54, audioRect.w - 16, 30};
      drawUIDropdown(devBtn, "Device", devName, "settings.audio_device");
      settingsBtns_.push_back({devBtn, 200, "audio_device"});
      // Audio buffer size cycle button
      int bufSamples = project_.audioBufferSamples;
      std::string bufLabel = "Buffer: " + std::to_string(bufSamples) + " smp";
      const int bufBtnY = devBtn.y + devBtn.h + 8;
      SDL_Rect bufBtn {audioRect.x + 8, bufBtnY, 160, 24};
      Primitives::drawFramedPanel(controlRenderer_, bufBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, bufLabel, ink, bufBtn);
      settingsBtns_.push_back({bufBtn, kSettingsActionAudioBufferCycle, "audio_buffer_samples"});
      const int audioFootY = bufBtn.y + bufBtn.h + 6;
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, "Smaller buffer = lower latency; larger = more stable.", audioRect.w - 16),
               soft, audioRect.x + 8, audioFootY);

      drawCard(midiRect, "MIDI CONTROL", "Optional external transport and cue control");
      SDL_Rect midiEnBtn {midiRect.x + 8, midiRect.y + 54, 120, 26};
      drawPillToggle(midiEnBtn, midiEnabled_, "MIDI ON", "MIDI OFF");
      settingsBtns_.push_back({midiEnBtn, 210, "midi_toggle"});
      SDL_Rect midiPortBtn {midiRect.x + 136, midiEnBtn.y, midiRect.w - 144, 26};
      Primitives::drawFramedPanel(controlRenderer_, midiPortBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_,
                       midiDeviceName_.empty() ? "Set MIDI Port..." : ellipsizeToPixelWidth(fontSmall_, midiDeviceName_, midiPortBtn.w - 12),
                       ink, midiPortBtn);
      settingsBtns_.push_back({midiPortBtn, 211, "midi_port"});

      int mapY = midiEnBtn.y + midiEnBtn.h + 10;
      drawText(controlRenderer_, fontSmall_, "Mappings", ink, midiRect.x + 8, mapY);
      drawText(controlRenderer_, fontSmall_, "Note 0-127 -> trigger cue index in the focused playlist", soft, midiRect.x + 8, mapY + smallLineH);
      drawText(controlRenderer_, fontSmall_, "CC 7 -> master volume   |   CC 20 -> playback speed", soft, midiRect.x + 8, mapY + smallLineH * 2);
      drawText(controlRenderer_, fontSmall_, "MMC Play / Stop / Goto -> transport   |   MSC Trigger -> cue by id", soft, midiRect.x + 8, mapY + smallLineH * 3);

    } else if (settingsTab_ == 2) {
      auto drawCard = [&](const SDL_Rect& rect, const std::string& title, const std::string& subtitle = std::string()) {
        Primitives::drawFramedPanel(controlRenderer_, rect, pal.shellInner,
                                    pal.deep, pal.light);
        drawText(controlRenderer_, fontBase_, title, ink, rect.x + 8, rect.y + 6);
        if (!subtitle.empty()) {
          drawText(controlRenderer_, fontSmall_,
                   ellipsizeToPixelWidth(fontSmall_, subtitle, rect.w - 16),
                   soft, rect.x + 8, rect.y + 28);
        }
      };
      auto drawPill = [&](const SDL_Rect& rect, bool active, const std::string& onLabel, const std::string& offLabel, int action) {
        Primitives::drawFramedPanel(controlRenderer_, rect,
                        active ? pal.dark : pal.mid,
                        pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, active ? onLabel : offLabel,
                         active ? pal.light : pal.deep, rect);
        settingsBtns_.push_back({rect, action, onLabel});
      };

      int colGap = 12;
      int leftW = std::max(320, (content.w - 12 - colGap) / 2);
      int rightW = std::max(320, content.w - 12 - colGap - leftW);
      SDL_Rect leftCol {cx, cy, leftW, content.h - 20};
      SDL_Rect rightCol {cx + leftW + colGap, cy, rightW, content.h - 20};
      constexpr int kCardGap = 10;

      int leftY = leftCol.y;
      SDL_Rect remoteRect {leftCol.x, leftY, leftCol.w, 142};
      leftY += remoteRect.h + kCardGap;
      SDL_Rect oscRect {leftCol.x, leftY, leftCol.w, 128};
      leftY += oscRect.h + kCardGap;
      SDL_Rect notesRect {leftCol.x, leftY, leftCol.w, std::max(96, leftCol.y + leftCol.h - leftY)};
      SDL_Rect integrationRect {rightCol.x, rightCol.y, rightCol.w, rightCol.h};

      const int netLineH = textLineHeight(fontSmall_) + 2;

      drawCard(remoteRect, "REMOTE CONTROL", "Companion / OSC ingress and HyperDeck emulation");
      const int remoteLabelY = remoteRect.y + 54;
      drawText(controlRenderer_, fontSmall_, "Companion / OSC port", soft, remoteRect.x + 8, remoteLabelY);
      const int remotePortY = rowYBelowLabel(remoteLabelY, fontSmall_, 2);
      SDL_Rect portBtn {remoteRect.x + 8, remotePortY, 176, 26};
      Primitives::drawFramedPanel(controlRenderer_, portBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Port " + std::to_string(companionPort_), ink, portBtn);
      settingsBtns_.push_back({portBtn, 220, "osc_port"});
      drawText(controlRenderer_, fontSmall_, "HyperDeck emulation stays on at TCP 9992.", soft,
               remoteRect.x + 196, portBtn.y + (portBtn.h - textLineHeight(fontSmall_)) / 2);
      const int remoteToggleY = portBtn.y + portBtn.h + 6;
      SDL_Rect remoteToggle {remoteRect.x + 8, remoteToggleY, 176, 22};
      drawPill(remoteToggle, project_.allowRemoteNetwork, "REMOTE ON", "LOCAL ONLY", kSettingsActionAllowRemoteToggle);
      drawText(controlRenderer_, fontSmall_,
               project_.allowRemoteNetwork ? "Listening on all interfaces" : "Listening on localhost (127.0.0.1)",
               soft, remoteToggle.x + remoteToggle.w + 8,
               remoteToggle.y + (remoteToggle.h - textLineHeight(fontSmall_)) / 2);

      drawCard(oscRect, "OSC QUERY / FEEDBACK", "Discovery and mirrored state");
      std::string queryStatus = project_.oscQueryEnabled ? (oscQueryReady_ ? "running" : "error") : "off";
      const int oscStatusY = oscRect.y + 54;
      drawText(controlRenderer_, fontSmall_,
               "query status: " + queryStatus + "  http " + std::to_string(project_.oscQueryPort),
               soft, oscRect.x + 8, oscStatusY);
      const int oscQueryRowY = rowYBelowLabel(oscStatusY, fontSmall_, 4);
      SDL_Rect queryToggle {oscRect.x + 8, oscQueryRowY, 144, 22};
      SDL_Rect queryPortBtn {queryToggle.x + queryToggle.w + 8, queryToggle.y, 164, 22};
      drawPill(queryToggle, project_.oscQueryEnabled, "QUERY ON", "QUERY OFF", kSettingsActionOscQueryToggle);
      Primitives::drawFramedPanel(controlRenderer_, queryPortBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Set HTTP Port...", ink, queryPortBtn);
      settingsBtns_.push_back({queryPortBtn, kSettingsActionOscQueryPortPrompt, "osc_query_port"});
      const int oscFbRowY = queryToggle.y + queryToggle.h + 6;
      SDL_Rect fbToggle {oscRect.x + 8, oscFbRowY, 156, 22};
      SDL_Rect fbRateBtn {fbToggle.x + fbToggle.w + 8, fbToggle.y, 164, 22};
      drawPill(fbToggle, project_.oscFeedbackMirrorEnabled, "MIRROR ON", "MIRROR OFF", kSettingsActionOscFeedbackMirrorToggle);
      Primitives::drawFramedPanel(controlRenderer_, fbRateBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_,
                       std::to_string(project_.oscFeedbackRateMs) + " ms",
                       ink, fbRateBtn);
      settingsBtns_.push_back({fbRateBtn, kSettingsActionOscFeedbackRatePrompt, "osc_feedback_rate"});

      drawCard(notesRect, "DISCOVERY / NOTES", "Network-facing runtime notes");
      const int notesLineY = notesRect.y + 54;
      drawText(controlRenderer_, fontSmall_, "OSC subscribe: send /deckboy/subscribe from your controller.", soft,
               notesRect.x + 8, notesLineY);
      drawText(controlRenderer_, fontSmall_, "NDI transport is configured per output in Video Outputs.", soft,
               notesRect.x + 8, notesLineY + netLineH);
      drawText(controlRenderer_, fontSmall_, "NMC mode / host / port are runtime-configurable for sync work.", soft,
               notesRect.x + 8, notesLineY + netLineH * 2);

      drawCard(integrationRect, "INTEGRATION ADAPTERS", "ATEM, NDI trigger, NMC, MTC, LTC, Art-Net, and Tally/TSL");
      IntegrationBackendRuntimeRoute integrationRoute = resolveIntegrationBackendRuntimeRoute();
      const int integrationLineY = integrationRect.y + 54;
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, integrationRoute.summary, integrationRect.w - 16),
               soft, integrationRect.x + 8, integrationLineY);
      int atemBridgePortDisplay = atemBridgeListenPort_;
      drawText(controlRenderer_, fontSmall_,
               "atem " + std::to_string(atemBridgePortDisplay) + "  artnet " + std::to_string(project_.artNetPort),
               soft, integrationRect.x + 8, integrationLineY + netLineH);

      int pillGap = 8;
      int pillW = std::max(120, (integrationRect.w - 16 - pillGap) / 2);
      int pillH = 24;
      int pillX1 = integrationRect.x + 8;
      int pillX2 = pillX1 + pillW + pillGap;
      int pillY = integrationRect.y + 104;
      SDL_Rect atemBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect ndiTrigBtn {pillX2, pillY, pillW, pillH};
      pillY += pillH + 8;
      SDL_Rect nmcBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect mtcBtn {pillX2, pillY, pillW, pillH};
      pillY += pillH + 8;
      SDL_Rect ltcBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect artNetBtn {pillX2, pillY, pillW, pillH};
      drawPill(atemBtn, project_.atemTriggerEnabled, "ATEM ON", "ATEM OFF", kSettingsActionIntegrationAtemToggle);
      drawPill(ndiTrigBtn, project_.ndiTriggerEnabled, "NDI TRIGGER ON", "NDI TRIGGER OFF", kSettingsActionIntegrationNdiTriggerToggle);
      drawPill(nmcBtn, project_.nmcSyncEnabled, "NMC ON", "NMC OFF", kSettingsActionIntegrationNmcToggle);
      drawPill(mtcBtn, project_.mtcIngestEnabled, "MTC ON", "MTC OFF", kSettingsActionIntegrationMtcToggle);
      drawPill(ltcBtn, project_.ltcIngestEnabled, "LTC ON", "LTC OFF", kSettingsActionIntegrationLtcToggle);
      drawPill(artNetBtn, project_.dmxArtNetEnabled, "ARTNET ON", "ARTNET OFF", kSettingsActionIntegrationArtNetToggle);
      pillY += pillH + 8;
      SDL_Rect tslBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect tcChaseBtn {pillX2, pillY, pillW, pillH};
      pillY += pillH + 8;
      SDL_Rect tcRunBtn {pillX1, pillY, pillW, pillH};
      drawPill(tslBtn, project_.tslTallyEnabled, "TALLY ON", "TALLY OFF", kSettingsActionIntegrationTslToggle);
      drawPill(tcChaseBtn, focusedDeck().timecodeChaseEnabled, "TC CHASE ON", "TC CHASE OFF", kSettingsActionIntegrationTimecodeChaseToggle);
      drawPill(tcRunBtn, focusedDeck().timecodeRunEnabled, "TC RUN ON", "TC RUN OFF", kSettingsActionIntegrationTimecodeRunToggle);

      int footerY = integrationRect.y + integrationRect.h - 34 - 32;
      SDL_Rect tslPortBtn {integrationRect.x + 8, footerY, 148, 24};
      Primitives::drawFramedPanel(controlRenderer_, tslPortBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Tally :" + std::to_string(project_.tslTallyPort), ink, tslPortBtn);
      settingsBtns_.push_back({tslPortBtn, kSettingsActionIntegrationTslPortPrompt, "integration_tsl_port"});
      SDL_Rect tslAddrBtn {tslPortBtn.x + tslPortBtn.w + 8, footerY,
                           integrationRect.x + integrationRect.w - 8 - (tslPortBtn.x + tslPortBtn.w + 8), 24};
      Primitives::drawFramedPanel(controlRenderer_, tslAddrBtn, pal.mid, pal.deep, pal.light);
      std::string tslAddrLabel = project_.tslTallyAddress.empty() ? "255.255.255.255" : project_.tslTallyAddress;
      drawCenteredText(controlRenderer_, fontSmall_, tslAddrLabel, ink, tslAddrBtn);
      settingsBtns_.push_back({tslAddrBtn, kSettingsActionIntegrationTslAddrPrompt, "integration_tsl_address"});

      SDL_Rect artNetPortBtn {integrationRect.x + 8, integrationRect.y + integrationRect.h - 34, 148, 24};
      Primitives::drawFramedPanel(controlRenderer_, artNetPortBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Art-Net " + std::to_string(project_.artNetPort), ink, artNetPortBtn);
      settingsBtns_.push_back({artNetPortBtn, kSettingsActionIntegrationArtNetPortPrompt, "integration_artnet_port"});
      bool allAdaptersEnabled = project_.atemTriggerEnabled && project_.ndiTriggerEnabled &&
                                project_.nmcSyncEnabled && project_.mtcIngestEnabled &&
                                project_.ltcIngestEnabled && project_.dmxArtNetEnabled;
      SDL_Rect allToggleBtn {artNetPortBtn.x + artNetPortBtn.w + 8, artNetPortBtn.y,
                             integrationRect.x + integrationRect.w - 8 - (artNetPortBtn.x + artNetPortBtn.w + 8), 24};
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
      // VIDEO OUTPUTS — Two-column layout
      // Left: Display, Connected Displays, Edge Blend, Canvas, Transition
      // Right: NDI, Streaming (full height for URL)
      // ═══════════════════════════════════════════════════════════════

      // Header: Output Selector + Enable Toggle
      int headerBtnW = std::min(260, (content.w - 36) / 2 - 8);
      SDL_Rect outSelectRect {cx, cy, headerBtnW, 32};
      drawUIDropdown(outSelectRect, "Output", outputLabel(focusedOutputIndex), "settings.focused_output");
      settingsBtns_.push_back({outSelectRect, 251, "cycle_output"});
      SDL_Rect enableBtn {cx + headerBtnW + 12, cy, headerBtnW, 32};
      drawActionBtn(enableBtn, outputTarget.enabled ? "OUTPUT ON" : "OUTPUT OFF", kSettingsActionOutputToggle, outputTarget.enabled);
      cy += 40;

      // Section frame helper
      auto drawSectionFrame = [&](const SDL_Rect& rect, const std::string& title) {
        Primitives::drawFramedPanel(controlRenderer_, rect, pal.shellInner, pal.deep, pal.light);
        SDL_Rect hdr {rect.x, rect.y, rect.w, 26};
        Primitives::drawFramedPanel(controlRenderer_, hdr, pal.mid, pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, title, pal.deep, hdr);
        return SDL_Rect {rect.x + 8, rect.y + 32, rect.w - 16, rect.h - 40};
      };

      // Column geometry
      int colGap = 10;
      int availW = content.w - 24;
      int leftW = (availW - colGap) * 55 / 100;
      int rightW = availW - colGap - leftW;
      int leftX = cx;
      int rightX = cx + leftW + colGap;
      int availH = content.y + content.h - cy - 8;

      // ─── LEFT COLUMN ───
      int ly = cy;
      int displayCount = SDL_GetNumVideoDisplays();
      constexpr int kRowH = 30;   // standard row/button height
      constexpr int kLabelH = 16; // label row height
      constexpr int kRowGap = 5;  // gap between rows in VerticalLayout

      // Display & Raster — fullscreen + orientation as full-width rows
      int dispSectionH = 190;
      SDL_Rect displaySection {leftX, ly, leftW, dispSectionH};
      SDL_Rect dBody = drawSectionFrame(displaySection, "DISPLAY & RASTER");
      VerticalLayout dLayout(dBody, kRowGap);

      std::string displayLabel = displayCount <= 0 ? "None" : ("Display " + std::to_string(outputDisplayIndex(focusedOutputIndex) + 1));
      if (displayCount > 0) {
        const char* dName = SDL_GetDisplayName(outputDisplayIndex(focusedOutputIndex));
        if (dName && *dName) displayLabel += ": " + std::string(dName);
      }
      SDL_Rect dBtn = dLayout.takeFixed(kRowH);
      drawUIDropdown(dBtn, "Hardware Display", displayLabel, "settings.output_display");
      settingsBtns_.push_back({dBtn, kSettingsActionOutputDisplayDropdown, "output_display"});

      SDL_Rect rBtn = dLayout.takeFixed(kRowH);
      std::string resLabel = std::to_string(targetW) + "x" + std::to_string(targetH) + (project_.outputFollowDisplay ? " (Native)" : " (Fixed)");
      drawUIDropdown(rBtn, "Resolution", resLabel, "settings.output_raster");
      settingsBtns_.push_back({rBtn, 237, "custom_raster"});

      // Fullscreen and Orientation as full-width separate rows
      SDL_Rect fsBtn = dLayout.takeFixed(kRowH);
      drawActionBtn(fsBtn, "Toggle Fullscreen", 236);
      SDL_Rect orientBtn = dLayout.takeFixed(kRowH);
      std::string orientLabel = outputOrientation == 0 ? "0\xc2\xb0 (Normal)"
                              : std::to_string(outputOrientation) + "\xc2\xb0";
      drawActionBtn(orientBtn, "Orientation: " + orientLabel, kSettingsActionOutputOrientationCycle);
      ly += dispSectionH + 6;

      // Connected Displays
      int dispListH = std::clamp(availH - (ly - cy) - 276, 60, 150);
      SDL_Rect dispListPanel {leftX, ly, leftW, dispListH};
      Primitives::drawFramedPanel(controlRenderer_, dispListPanel, pal.light, pal.deep, pal.mid);
      drawText(controlRenderer_, fontBase_, "CONNECTED DISPLAYS", ink,
               dispListPanel.x + 8, dispListPanel.y + 4);
      int dispRowY = dispListPanel.y + 28;
      int currentDispIdx = outputDisplayIndex(focusedOutputIndex);
      for (int di = 0; di < displayCount; ++di) {
        if (dispRowY + kRowH > dispListPanel.y + dispListPanel.h - 4) break;
        SDL_Rect dispRow {dispListPanel.x + 6, dispRowY, dispListPanel.w - 12, kRowH};
        bool selected = di == currentDispIdx;
        Primitives::drawFramedPanel(controlRenderer_, dispRow,
                                    selected ? pal.dark : pal.shellInner, pal.deep, pal.mid);
        const char* dNameRaw = SDL_GetDisplayName(di);
        std::string dNameStr = dNameRaw && *dNameRaw ? dNameRaw : ("Display " + std::to_string(di + 1));
        SDL_Rect dispBounds;
        SDL_GetDisplayBounds(di, &dispBounds);
        std::string dispInfo = std::to_string(di + 1) + ": " + dNameStr
          + "  " + std::to_string(dispBounds.w) + "x" + std::to_string(dispBounds.h);
        if (selected) dispInfo += "  [ASSIGNED]";
        drawText(controlRenderer_, fontSmall_, dispInfo,
                 selected ? pal.light : ink, dispRow.x + 6, dispRow.y + 7);
        settingsBtns_.push_back({dispRow, kSettingsActionOutputDisplaySelectBase + di, "display_select"});
        dispRowY += kRowH + 4;
      }
      ly += dispListH + 6;

      // Edge Blending — full width, taller so labels don't overlap buttons
      {
        const Deck& bd = focusedDeck();
        int blendH = 90;
        SDL_Rect blendPanel {leftX, ly, leftW, blendH};
        Primitives::drawFramedPanel(controlRenderer_, blendPanel, pal.shellInner, pal.deep, pal.light);
        drawText(controlRenderer_, fontSmall_, "EDGE BLENDING", ink,
                 blendPanel.x + 8, blendPanel.y + 4);
        int bx = blendPanel.x + 10;
        int labelY = blendPanel.y + 24;
        int btnY = rowYBelowLabel(labelY, fontSmall_, 2);
        int bw = (blendPanel.w - 28) / 4;
        int bgap = 6;
        auto drawBlendCtrl = [&](const char* label, float val, int decAction, int incAction) {
          std::string valStr = std::string(label) + ": " + std::to_string(static_cast<int>(val * 100.0f)) + "%";
          drawText(controlRenderer_, fontSmall_, valStr, soft, bx, labelY);
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
        drawBlendCtrl("Left", bd.edgeBlendLeft, kSettingsActionOutputEdgeBlendLDec, kSettingsActionOutputEdgeBlendLInc);
        drawBlendCtrl("Right", bd.edgeBlendRight, kSettingsActionOutputEdgeBlendRDec, kSettingsActionOutputEdgeBlendRInc);
        drawBlendCtrl("Top", bd.edgeBlendTop, kSettingsActionOutputEdgeBlendTDec, kSettingsActionOutputEdgeBlendTInc);
        drawBlendCtrl("Bottom", bd.edgeBlendBottom, kSettingsActionOutputEdgeBlendBDec, kSettingsActionOutputEdgeBlendBInc);
        ly += blendH + 6;
      }

      // Area of Interest
      {
        const OutputTarget& ot = focusedOutput();
        bool aoiActive = ot.aoiLeft > 0.001f || ot.aoiRight > 0.001f
                      || ot.aoiTop > 0.001f   || ot.aoiBottom > 0.001f;
        int aoiPanH = 90;
        SDL_Rect aoiPanel {leftX, ly, leftW, aoiPanH};
        SDL_Color aoiFill = aoiActive ? pal.dark : pal.shellInner;
        SDL_Color aoiInk2 = aoiActive ? pal.light : ink;
        Primitives::drawFramedPanel(controlRenderer_, aoiPanel, aoiFill, pal.deep, pal.light);
        drawText(controlRenderer_, fontSmall_, "AREA OF INTEREST", aoiInk2,
                 aoiPanel.x + 8, aoiPanel.y + 4);
        // Reset button in the title bar area
        SDL_Rect aoiResetBtn {aoiPanel.x + aoiPanel.w - 56, aoiPanel.y + 3, 50, 16};
        Primitives::drawFramedPanel(controlRenderer_, aoiResetBtn, pal.mid, pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, "RESET", ink, aoiResetBtn);
        settingsBtns_.push_back({aoiResetBtn, kSettingsActionOutputAoiReset, "aoi_reset"});
        int abx = aoiPanel.x + 10;
        int ably = aoiPanel.y + 24;
        int aoBtnY = rowYBelowLabel(ably, fontSmall_, 4);
        int aoBW = (aoiPanel.w - 28) / 4;
        int aoGap = 6;
        auto drawAoiCtrl = [&](const char* label, float val, int decAct, int incAct) {
          std::string valStr = std::string(label) + ": " + std::to_string(static_cast<int>(val * 100.0f + 0.5f)) + "%";
          drawText(controlRenderer_, fontSmall_, valStr, aoiActive ? pal.light : soft, abx, ably);
          int btnW = (aoBW - 4) / 2;
          SDL_Rect decBtn {abx, aoBtnY, btnW, 24};
          SDL_Rect incBtn {abx + btnW + 4, aoBtnY, btnW, 24};
          Primitives::drawFramedPanel(controlRenderer_, decBtn, pal.mid, pal.deep, pal.light);
          drawCenteredText(controlRenderer_, fontSmall_, "-", aoiInk2, decBtn);
          settingsBtns_.push_back({decBtn, decAct, "aoi"});
          Primitives::drawFramedPanel(controlRenderer_, incBtn, pal.mid, pal.deep, pal.light);
          drawCenteredText(controlRenderer_, fontSmall_, "+", aoiInk2, incBtn);
          settingsBtns_.push_back({incBtn, incAct, "aoi"});
          abx += aoBW + aoGap;
        };
        drawAoiCtrl("L", ot.aoiLeft,   kSettingsActionOutputAoiLDec, kSettingsActionOutputAoiLInc);
        drawAoiCtrl("R", ot.aoiRight,  kSettingsActionOutputAoiRDec, kSettingsActionOutputAoiRInc);
        drawAoiCtrl("T", ot.aoiTop,    kSettingsActionOutputAoiTDec, kSettingsActionOutputAoiTInc);
        drawAoiCtrl("B", ot.aoiBottom, kSettingsActionOutputAoiBDec, kSettingsActionOutputAoiBInc);
        ly += aoiPanH + 6;
      }

      // Canvas Mode + Default Transition (side by side)
      {
        int halfW = (leftW - 6) / 2;
        int pairH = 68;

        // Canvas Mode
        {
          SDL_Rect canvasPanel {leftX, ly, halfW, pairH};
          Primitives::drawFramedPanel(controlRenderer_, canvasPanel, pal.shellInner, pal.deep, pal.light);
          drawText(controlRenderer_, fontSmall_, "CANVAS MODE", ink,
                   canvasPanel.x + 8, canvasPanel.y + 4);
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

        // Default Transition
        {
          const Deck& td = focusedDeck();
          SDL_Rect transPanel {leftX + halfW + 6, ly, halfW, pairH};
          Primitives::drawFramedPanel(controlRenderer_, transPanel, pal.shellInner, pal.deep, pal.light);
          drawText(controlRenderer_, fontSmall_, "DEFAULT TRANSITION", ink,
                   transPanel.x + 8, transPanel.y + 4);
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

      // ─── RIGHT COLUMN ───
      int ry = cy;

      // NDI — sized to fit source name
      int ndiH = 108;
      SDL_Rect ndiSection {rightX, ry, rightW, ndiH};
      SDL_Rect nBody = drawSectionFrame(ndiSection, "NDI\xc2\xae NETWORK");
      VerticalLayout nLayout(nBody, kRowGap);
      drawActionBtn(nLayout.takeFixed(kRowH), outputTarget.ndiEnabled ? "NDI SENDING: ON" : "NDI SENDING: OFF", 271, outputTarget.ndiEnabled);
      SDL_Rect nnBtn = nLayout.takeFixed(kRowH);
      std::string ndiName = trim(outputTarget.ndiSourceName).empty() ? "Default" : outputTarget.ndiSourceName;
      drawUIDropdown(nnBtn, "Source", ndiName, "settings.ndi_name");
      settingsBtns_.push_back({nnBtn, 272, "ndi_name"});
      ry += ndiH + 6;

      // NDI Key — separate small section
      {
        SDL_Rect ndiKeyBtn {rightX, ry, rightW, kRowH};
        drawActionBtn(ndiKeyBtn, outputTarget.ndiKeyEnabled ? "NDI KEY: ON" : "NDI KEY: OFF", 273, outputTarget.ndiKeyEnabled);
        ry += kRowH + 6;
      }

      // DeckLink SDI/HDMI output
      {
        int dlH = 173;
        SDL_Rect dlSection {rightX, ry, rightW, dlH};
        SDL_Rect dlBody = drawSectionFrame(dlSection, "DECKLINK SDI / HDMI");
        VerticalLayout dlLayout(dlBody, kRowGap);

        // Enable toggle
        SDL_Rect dlToggle = dlLayout.takeFixed(kRowH);
        drawActionBtn(dlToggle,
                      outputTarget.deckLinkEnabled ? "DECKLINK: ON" : "DECKLINK: OFF",
                      kSettingsActionDeckLinkToggle, outputTarget.deckLinkEnabled);

        // Device dropdown
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

        // Mode dropdown
        {
          auto mode = deckboy::platform::video::parseDeckLinkMode(outputTarget.deckLinkMode);
          std::string modeLabel = deckboy::platform::video::deckLinkModeLabel(mode);
          SDL_Rect modeBtn = dlLayout.takeFixed(kRowH);
          drawUIDropdown(modeBtn, "Mode", modeLabel, "settings.decklink_mode");
          settingsBtns_.push_back({modeBtn, kSettingsActionDeckLinkModeDropdown, "decklink_mode"});
        }

        // 10-bit toggle
        SDL_Rect bitBtn = dlLayout.takeFixed(kRowH);
        drawActionBtn(bitBtn,
                      outputTarget.deckLink10Bit ? "10-BIT: ON" : "10-BIT: OFF",
                      kSettingsActionDeckLink10BitToggle, outputTarget.deckLink10Bit);

        ry += dlH + 6;
      }

      // Spout interprocess texture sharing (Windows)
      {
        int spH = 108;
        SDL_Rect spSection {rightX, ry, rightW, spH};
        SDL_Rect spBody = drawSectionFrame(spSection, "SPOUT TEXTURE SHARE");
        VerticalLayout spLayout(spBody, kRowGap);

        // Enable toggle
        SDL_Rect spToggle = spLayout.takeFixed(kRowH);
        drawActionBtn(spToggle,
                      outputTarget.spoutEnabled ? "SPOUT: ON" : "SPOUT: OFF",
                      kSettingsActionSpoutToggle, outputTarget.spoutEnabled);

        // Sender name
        {
          std::string spName = trim(outputTarget.spoutSenderName);
          if (spName.empty()) {
            spName = "Deckboy Output " + std::to_string(focusedOutputIndex + 1);
          }
          SDL_Rect spNameBtn = spLayout.takeFixed(kRowH);
          drawUIDropdown(spNameBtn, "Sender", spName, "settings.spout_name");
          settingsBtns_.push_back({spNameBtn, kSettingsActionSpoutNamePrompt, "spout_name"});
        }

        ry += spH + 6;
      }

      // Streaming — takes all remaining height so URL has room
      int streamH = std::max(280, availH - (ry - cy));
      SDL_Rect streamSection {rightX, ry, rightW, streamH};
      SDL_Rect sBody = drawSectionFrame(streamSection, "STREAMING (SRT / RTMP)");
      VerticalLayout sLayout(sBody, kRowGap);

      drawActionBtn(sLayout.takeFixed(kRowH), outputTarget.streamEnabled ? "STREAMING: ON" : "STREAMING: OFF", 255, outputTarget.streamEnabled);

      // State line — two separate lines for readability
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
        drawText(controlRenderer_, fontSmall_, stateLine1, soft, sBody.x, sLayout.takeFixed(kLabelH).y);
        if (!stateLine2.empty()) {
          drawText(controlRenderer_, fontSmall_, stateLine2, soft, sBody.x, sLayout.takeFixed(kLabelH).y);
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

      // Target URL — full remaining space
      drawText(controlRenderer_, fontSmall_, "Target URL:", soft, sBody.x, sLayout.takeFixed(kLabelH).y);
      SDL_Rect suBtn = sLayout.takeRemaining();
      suBtn.h = std::max(48, suBtn.h);
      Primitives::drawFramedPanel(controlRenderer_, suBtn, pal.deep, pal.dark, pal.dark);
      TTF_Font* urlFont = fontMono_ ? fontMono_ : fontSmall_;
      std::string urlStr2 = streamUrl;
      {
        int urlPad = 8;
        SDL_Rect urlClip {suBtn.x + urlPad, suBtn.y + 4, suBtn.w - urlPad * 2, suBtn.h - 8};
        SDL_RenderSetClipRect(controlRenderer_, &urlClip);
        std::string urlLine1 = urlStr2;
        std::string urlLine2;
        size_t queryPos = urlStr2.find('?');
        if (queryPos != std::string::npos) {
          urlLine1 = urlStr2.substr(0, queryPos);
          urlLine2 = urlStr2.substr(queryPos);
        }
        drawText(controlRenderer_, urlFont,
                 urlLine1.empty() ? "click to edit" : urlLine1,
                 urlLine1.empty() ? pal.dark : pal.light,
                 suBtn.x + urlPad, suBtn.y + 8);
        if (!urlLine2.empty()) {
          drawText(controlRenderer_, fontSmall_, urlLine2, pal.mid,
                   suBtn.x + urlPad, suBtn.y + 28);
        } else if (urlLine1.empty()) {
          drawText(controlRenderer_, fontSmall_, "(no URL set)", pal.dark,
                   suBtn.x + urlPad, suBtn.y + 28);
        }
        SDL_RenderSetClipRect(controlRenderer_, nullptr);
      }
      settingsBtns_.push_back({suBtn, 257, "stream_url"});

    } else if (settingsTab_ == 4) {
      // About tab
      SDL_Rect logoRect {cx, cy, content.w - 24, 116};
      Primitives::drawFramedPanel(controlRenderer_, logoRect, pal.shellInner,
                                  pal.deep, pal.light);
      if (uiPackAvailable_) {
        SDL_Rect logoArtRect {logoRect.x + logoRect.w - 204, logoRect.y + 10, 190, logoRect.h - 20};
        drawUiImageContain(uiHeaderArt_, logoArtRect, 215);
      }
      TTF_Font* titleFont = fontPixel_ ? fontPixel_ : fontLarge_;
      drawText(controlRenderer_, titleFont, std::string(kAppTitle), ink,
               logoRect.x + 14, logoRect.y + 10);
      drawText(controlRenderer_, fontBase_, "dot-matrix cue deck", soft,
               logoRect.x + 16, logoRect.y + 52);
      drawText(controlRenderer_, fontSmall_,
               "version: " + std::string(kAppVersionTag), soft,
               logoRect.x + 16, logoRect.y + 78);

      SDL_Rect infoRect {cx, logoRect.y + logoRect.h + 8, content.w - 24, content.h - logoRect.h - 18};
      Primitives::drawFramedPanel(controlRenderer_, infoRect, pal.light,
                                  pal.deep, pal.mid);
      const int aboutHeaderY = infoRect.y + 8;
      drawText(controlRenderer_, fontBase_, "RUNTIME", ink,
               infoRect.x + 8, aboutHeaderY);
      int infoTextW = infoRect.w - 16;
      const int aboutLineH = textLineHeight(fontSmall_) + 2;
      const int aboutBodyY = aboutHeaderY + textLineHeight(fontBase_) + 6;
      drawText(controlRenderer_, fontSmall_,
               "Companion port: " + std::to_string(companionPort_), soft,
               infoRect.x + 8, aboutBodyY);
      drawText(controlRenderer_, fontSmall_,
               "HyperDeck port: 9992", soft,
               infoRect.x + 8, aboutBodyY + aboutLineH);
      drawText(controlRenderer_, fontSmall_,
               "UI mascot/sprite art is disabled in live control panels.", soft,
               infoRect.x + 8, aboutBodyY + aboutLineH * 2);
      drawText(controlRenderer_, fontSmall_,
               "Core transport keys: Enter Take | Space Play/Pause | S Stop | C Clear", soft,
               infoRect.x + 8, aboutBodyY + aboutLineH * 3 + 6);
      std::string packStatus = uiPackAvailable_
        ? ("UI pack: " + uiPackRoot_.string())
        : "UI pack: not found (data/ui/deckboy_ui_pack_v3 preferred)";
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, packStatus, infoTextW), soft,
               infoRect.x + 8, aboutBodyY + aboutLineH * 4 + 6);
      std::string themeStatus = currentThemeName_.empty()
        ? "Theme: gameboy (default) — set DECKBOY_THEME=dark to switch"
        : "Theme: " + currentThemeName_ + " (data/themes/" + currentThemeName_ + "/theme.txt)";
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, themeStatus, infoTextW), soft,
               infoRect.x + 8, aboutBodyY + aboutLineH * 5 + 6);
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
      if (sb.action >= 100 && sb.action <= 104) {
        // Tab switch
        settingsTab_ = sb.action - 100;
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
      } else if (sb.action >= kSettingsActionOutputAoiLInc && sb.action <= kSettingsActionOutputAoiReset) {
        OutputTarget& ot = focusedOutputMutable();
        constexpr float kAoiStep = 0.05f;
        switch (sb.action) {
          case kSettingsActionOutputAoiLInc: ot.aoiLeft   = std::clamp(ot.aoiLeft   + kAoiStep, 0.0f, 0.95f); break;
          case kSettingsActionOutputAoiLDec: ot.aoiLeft   = std::clamp(ot.aoiLeft   - kAoiStep, 0.0f, 0.95f); break;
          case kSettingsActionOutputAoiRInc: ot.aoiRight  = std::clamp(ot.aoiRight  + kAoiStep, 0.0f, 0.95f); break;
          case kSettingsActionOutputAoiRDec: ot.aoiRight  = std::clamp(ot.aoiRight  - kAoiStep, 0.0f, 0.95f); break;
          case kSettingsActionOutputAoiTInc: ot.aoiTop    = std::clamp(ot.aoiTop    + kAoiStep, 0.0f, 0.95f); break;
          case kSettingsActionOutputAoiTDec: ot.aoiTop    = std::clamp(ot.aoiTop    - kAoiStep, 0.0f, 0.95f); break;
          case kSettingsActionOutputAoiBInc: ot.aoiBottom = std::clamp(ot.aoiBottom + kAoiStep, 0.0f, 0.95f); break;
          case kSettingsActionOutputAoiBDec: ot.aoiBottom = std::clamp(ot.aoiBottom - kAoiStep, 0.0f, 0.95f); break;
          case kSettingsActionOutputAoiReset:
            ot.aoiLeft = ot.aoiRight = ot.aoiTop = ot.aoiBottom = 0.0f;
            triggerToast("aoi: reset");
            break;
          default: break;
        }
        markProjectDirty();
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
    if (sb.action == kSettingsActionDeckLinkToggle) {
        OutputTarget& output = focusedOutputMutable();
        output.deckLinkEnabled = !output.deckLinkEnabled;
        if (!output.deckLinkEnabled) {
          // Shut down DeckLink when disabled
          auto& rt = outputRuntimes_[project_.focusedOutputIndex];
          shutdownOutputDeckLink(rt);
        }
        triggerToast(std::string("decklink: ") + (output.deckLinkEnabled ? "on" : "off"));
        markProjectDirty();
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
      } else if (sb.action == kSettingsActionOutputAdvancedToggle) {
        videoOutputsAdvanced_ = !videoOutputsAdvanced_;
      } else if (sb.action == kSettingsActionOutputToggle) {
        toggleFocusedOutputEnabled();
      } else if (sb.action == kSettingsActionOutputDisplayPrev) {
        cycleOutputDisplay(-1);
      } else if (sb.action == kSettingsActionOutputDisplayNext) {
        cycleOutputDisplay(1);
      } else if (sb.action == kSettingsActionOutputDisplayRescan) {
        observedDisplayCount_ = SDL_GetNumVideoDisplays();
        refreshDisplayTopology(true);
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
          bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
          bool ctrlHeld = (SDL_GetModState() & KMOD_CTRL) != 0;
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
          bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
          bool ctrlHeld = (SDL_GetModState() & KMOD_CTRL) != 0;
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
      }
  }
