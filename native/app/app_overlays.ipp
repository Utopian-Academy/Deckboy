// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
  bool monitorsVisible() const {
    if (!monitorsWindow_) return false;
    return (SDL_GetWindowFlags(monitorsWindow_) & SDL_WINDOW_HIDDEN) == 0;
  }

  void setMonitorsVisible(bool visible, bool raiseWindow = false) {
    if (!monitorsWindow_) return;
    if (visible) {
      SDL_ShowWindow(monitorsWindow_);
      if ((SDL_GetWindowFlags(monitorsWindow_) & SDL_WINDOW_MINIMIZED) != 0)
        SDL_RestoreWindow(monitorsWindow_);
      if (raiseWindow) SDL_RaiseWindow(monitorsWindow_);
    } else {
      SDL_HideWindow(monitorsWindow_);
    }
  }

  void renderQuitConfirm() {
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);

    // Semi-transparent dark overlay
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0x0F, 0x38, 0x0F, 160);
    SDL_Rect overlay {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &overlay);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    // Dialog panel: larger default sizing for legibility.
    SDL_Rect dialog {(width - 420) / 2, (height - 232) / 2, 420, 232};
    Primitives::drawFramedPanel(controlRenderer_, dialog, pal.light, pal.deep, pal.mid);

    drawTextSafe(controlRenderer_, fontLarge_,
                 SDL_Rect {dialog.x + 28, dialog.y + 30, dialog.w - 56, 36},
                 "Quit Deckboy?", pal.deep);

    // YES / NO buttons
    quitYesBtn_ = {dialog.x + 32,  dialog.y + 130, 156, 54};
    quitNoBtn_  = {dialog.x + 232, dialog.y + 130, 156, 54};
    Primitives::drawFramedPanel(controlRenderer_, quitYesBtn_, pal.dark, pal.deep, pal.mid);
    Primitives::drawFramedPanel(controlRenderer_, quitNoBtn_,  pal.dark, pal.deep, pal.mid);
    drawCenteredText(controlRenderer_, fontBase_, "YES", pal.light, quitYesBtn_);
    drawCenteredText(controlRenderer_, fontBase_, "NO",  pal.light, quitNoBtn_);

    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {dialog.x + 32, dialog.y + 194, dialog.w - 64, 18},
                 "esc or N to cancel", pal.inkSoft);
  }

  void renderStartupDialog() {
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);

    // Full-screen dim
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0x0F, 0x38, 0x0F, 200);
    SDL_Rect overlay {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &overlay);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    // Dialog panel
    const int kDW = 660;
    const int kDH = 440;
    SDL_Rect dialog {(width - kDW) / 2, (height - kDH) / 2, kDW, kDH};
    Primitives::drawFramedPanel(controlRenderer_, dialog, pal.shellInner, pal.deep, pal.shellOuter);

    // Title + file name
    int tx = dialog.x + 36;
    TTF_Font* titleFont = fontPixel_ ? fontPixel_ : fontLarge_;
    drawTextSafe(controlRenderer_, titleFont,
                 SDL_Rect {tx, dialog.y + 38, dialog.w - 72, 34},
                 std::string(kAppTitle) + " " + std::string(kAppVersionTag), pal.deep);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {tx, dialog.y + 78, dialog.w - 72, 18},
                 "dot-matrix cue deck", pal.inkSoft);
    drawTextSafe(controlRenderer_, fontBase_,
                 SDL_Rect {tx, dialog.y + 116, dialog.w - 72, 24},
                 "Choose startup mode:", pal.deep);

    std::string fname = currentProjectFile_.empty() ? "default.deckboy" : currentProjectFile_.filename().string();
    bool hasSavedFile = !currentProjectFile_.empty() && fs::exists(currentProjectFile_);
    if (hasSavedFile) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {tx, dialog.y + 152, dialog.w - 72, 18},
                   "Previous show file:", pal.deep);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {tx, dialog.y + 176, dialog.w - 72, 18},
                   fname, pal.dark);
    } else {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {tx, dialog.y + 160, dialog.w - 72, 18},
                   "No previous show file found at startup path.", pal.dark);
    }

    // Buttons
    int buttonY = dialog.y + 270;
    int buttonW = 184;
    int buttonH = 58;
    startupNewBtn_ = {dialog.x + 36, buttonY, buttonW, buttonH};
    startupLoadBtn_ = {startupNewBtn_.x + buttonW + 10, buttonY, buttonW, buttonH};
    startupOpenSavedBtn_ = {startupLoadBtn_.x + buttonW + 10, buttonY, buttonW, buttonH};

    Primitives::drawFramedPanel(controlRenderer_, startupNewBtn_, pal.mid,
                                pal.deep, pal.light);
    drawCenteredText(controlRenderer_, fontBase_, "NEW SHOW FILE", pal.deep, startupNewBtn_);

    SDL_Color loadFill = hasSavedFile ? pal.dark : pal.shellOuter;
    SDL_Color loadText = hasSavedFile ? pal.light : pal.mid;
    Primitives::drawFramedPanel(controlRenderer_, startupLoadBtn_, loadFill, pal.deep, pal.mid);
    drawCenteredText(controlRenderer_, fontBase_, hasSavedFile ? "OPEN PREVIOUS" : "NO PREVIOUS", loadText, startupLoadBtn_);

    Primitives::drawFramedPanel(controlRenderer_, startupOpenSavedBtn_, pal.mid,
                                pal.deep, pal.light);
    drawCenteredText(controlRenderer_, fontBase_, "OPEN SAVED...", pal.deep, startupOpenSavedBtn_);

    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {tx, dialog.y + 352, dialog.w - 72, 18},
                 "N=new  Enter/P=previous  O=open saved picker",
                 pal.inkSoft);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {tx, dialog.y + 374, dialog.w - 72, 18},
                 "Esc=continue with current session",
                 pal.inkSoft);
  }

  void renderShortcutsOverlay() {
    if (!shortcutsOverlayOpen_) return;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(controlWindow_, &ww, &wh);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 180);
    SDL_Rect full {0, 0, ww, wh};
    SDL_RenderFillRect(controlRenderer_, &full);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    int mw = std::min(720, ww - 40);
    int mh = std::min(600, wh - 40);
    SDL_Rect modal {(ww - mw) / 2, (wh - mh) / 2, mw, mh};
    Primitives::drawFramedPanel(controlRenderer_, modal, pal.shellInner, pal.deep, pal.shellOuter);
    drawTextSafe(controlRenderer_, fontBase_,
                 SDL_Rect {modal.x + 16, modal.y + 10, mw - 64, 24},
                 "KEYBOARD SHORTCUTS", pal.deep);
    // Close hint
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {modal.x + mw - 180, modal.y + 14, 170, 16},
                 "Ctrl+/ to close", pal.dark);

    struct ShortcutEntry { const char* key; const char* desc; };
    static const ShortcutEntry shortcuts[] = {
      {"Enter",           "Take selected cue live"},
      {"Space",           "Play / Pause"},
      {"S",               "Stop active cue"},
      {"Ctrl+R",          "Rerack (rewind to start)"},
      {"Up / Down",       "Navigate cue list"},
      {"Left / Right",    "Skip back/forward 10s"},
      {"Home / End",      "Skip to start/end"},
      {"I",               "Import media files"},
      {"Ctrl+I",          "Set in point at playhead"},
      {"Ctrl+O",          "Set out point at playhead"},
      {"Delete / Bksp",   "Delete selected cue(s)"},
      {"Ctrl+C",          "Copy selected cue settings"},
      {"Ctrl+V",          "Paste cue settings to selection"},
      {"Ctrl+Shift+C",    "Copy focused warp settings"},
      {"Ctrl+Shift+V",    "Paste focused warp settings"},
      {"Ctrl+Z",          "Undo"},
      {"Ctrl+Shift+Z",    "Redo"},
      {"Ctrl+G",          "GOTO cue number"},
      {"Ctrl+F",          "Find cue by name/number"},
      {"Ctrl+S",          "Save project"},
      {"Ctrl+Shift+E",    "Export bundled project"},
      {"Ctrl+O",          "Open project"},
      {"Ctrl+N",          "New project"},
      {"L",               "Toggle loop"},
      {"H",               "Toggle hold (pause at end)"},
      {"X",               "Cycle end action"},
      {"K",               "Cycle color tag"},
      {"G",               "Add as graphic overlay"},
      {"Backspace",       "Clear all overlays"},
      {"N",               "Toggle output window"},
      {"F",               "Toggle fullscreen output"},
      {"B",               "Toggle blackout"},
      {"P",               "Open preferences"},
      {"Ctrl+/",          "This shortcut overlay"},
      {"+/-",             "Volume up/down"},
      {"Shift+drag",      "Snap warp corners to grid"},
    };
    int rowY = modal.y + 40;
    int colW = (mw - 32) / 2;
    int col = 0;
    for (const auto& s : shortcuts) {
      if (rowY + 18 > modal.y + mh - 8) {
        if (col == 0) { col = 1; rowY = modal.y + 40; } else break;
      }
      int cx = modal.x + 16 + col * colW;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {cx, rowY, 130, 16}, s.key, pal.dark);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {cx + 134, rowY, colW - 140, 16}, s.desc, pal.deep);
      rowY += 18;
    }
  }

  void renderSplashOverlay() {
    if (!showSplashOverlay_) {
      return;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);

    // ── Full-screen background: splash art ──
    SDL_SetRenderDrawColor(controlRenderer_, 0x9B, 0xBC, 0x0F, 255);
    SDL_Rect full {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &full);
    if (uiPackAvailable_) {
      SDL_Rect artRect {0, 0, width, height};
      drawUiImageCover(uiSplashArt_, artRect, 220);
    }

    // ── Framed card — the green golden box, centred over the art ──
    SDL_Rect card {(width - 760) / 2, (height - 430) / 2, 760, 430};
    // Semi-transparent backing so art peeks through edges
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, pal.shellInner.r, pal.shellInner.g, pal.shellInner.b, 220);
    SDL_RenderFillRect(controlRenderer_, &card);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    Primitives::drawFramedPanel(controlRenderer_, card, pal.shellInner, pal.deep, pal.mid);

    // ── Title ──
    TTF_Font* titleFont = fontPixel_ ? fontPixel_ : fontLarge_;
    drawTextSafe(controlRenderer_, titleFont,
                 SDL_Rect {card.x + 36, card.y + 28, card.w - 72, 34},
                 std::string(kAppTitle) + " " + std::string(kAppVersionTag), pal.deep);
    drawTextSafe(controlRenderer_, fontBase_,
                 SDL_Rect {card.x + 38, card.y + 76, card.w - 76, 24},
                 "dot-matrix cue deck", pal.dark);

    // ── Boot console — nested framed panel ──
    SDL_Rect bootRect {card.x + 36, card.y + 126, card.w - 72, 184};
    Primitives::drawFramedPanel(controlRenderer_, bootRect, pal.light, pal.deep, pal.mid);

    static const std::array<const char*, 5> kBootLines {
      "initializing deck runtime...",
      "loading outputs...",
      "starting compositor...",
      "opening companion port 5510...",
      "arming safety guards..."
    };
    Uint64 now = SDL_GetTicks64();
    Uint64 elapsed = splashStartedAt_ > 0 ? (now - splashStartedAt_) : 0;
    int visibleLines = std::clamp(static_cast<int>(elapsed / 380), 1, static_cast<int>(kBootLines.size()));
    for (int i = 0; i < visibleLines; ++i) {
      drawText(controlRenderer_, fontMono_, kBootLines[i], pal.deep,
               bootRect.x + 12, bootRect.y + 14 + i * 30);
    }
    if (((now / 300) % 2) == 0) {
      drawText(controlRenderer_, fontMono_, "_", pal.deep,
               bootRect.x + 12 + 9 * 14, bootRect.y + 14 + (visibleLines - 1) * 30);
    }

    // ── Hint text ──
    drawText(controlRenderer_, fontBase_, "press ENTER to start",
             pal.deep, card.x + 36, card.y + card.h - 74);
    drawText(controlRenderer_, fontSmall_, "Esc or click to skip",
             pal.dark, card.x + 36, card.y + card.h - 44);

    // ── Sparkles around the card ──
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    {
      SDL_Color starC = pal.mid;
      for (int s = 0; s < 6; ++s) {
        double phase = static_cast<double>(elapsed) * 0.002 + s * 1.047;
        int sx = card.x + card.w / 2 - 100 + s * 40 + static_cast<int>(std::sin(phase) * 14.0);
        int sy = card.y - 12 + static_cast<int>(std::cos(phase * 0.7) * 6.0);
        starC.a = static_cast<Uint8>(100 + 155 * std::abs(std::sin(phase * 0.5)));
        drawStar(controlRenderer_, sx, sy, 2 + (s % 3), starC);
      }
      for (int s = 0; s < 4; ++s) {
        double phase = static_cast<double>(elapsed) * 0.0018 + s * 1.57;
        int sx = card.x + card.w / 2 - 60 + s * 40 + static_cast<int>(std::sin(phase) * 10.0);
        int sy = card.y + card.h + 8 + static_cast<int>(std::cos(phase * 0.8) * 4.0);
        starC.a = static_cast<Uint8>(80 + 175 * std::abs(std::sin(phase * 0.6)));
        drawStar(controlRenderer_, sx, sy, 2 + (s % 2), starC);
      }
      // Corner accents
      for (int c = 0; c < 4; ++c) {
        int cornX = (c % 2 == 0) ? card.x - 8 : card.x + card.w + 8;
        int cornY = (c < 2) ? card.y - 8 : card.y + card.h + 8;
        double cPhase = static_cast<double>(elapsed) * 0.003 + c * 1.57;
        starC.a = static_cast<Uint8>(60 + 195 * std::abs(std::sin(cPhase)));
        drawStar(controlRenderer_, cornX, cornY, 3, starC);
      }
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
  }

  // ---------------------------------------------------------------------------
  // Deck panel column helpers
  // ---------------------------------------------------------------------------
