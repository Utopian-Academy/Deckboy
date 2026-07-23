// ============================================================================
// app_overlays.ipp — Monitor window and overlay management.
//
// Manages the secondary "Monitors" window and quit confirmation dialog:
//
//   monitorsVisible() / setMonitorsVisible() — show/hide the monitors window
//   renderQuitConfirm()                       — unsaved-changes quit dialog
//
// The monitors window provides a multi-output preview showing all active
// output destinations in a tiled layout. It also hosts the output menu
// for adding/removing outputs and toggling FPS counters.
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Returns whether the monitors window is currently visible.
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

  // ── Runtime dependency detection ──────────────────────────────────────────
  // Each helper returns true when the host machine has the runtime piece the
  // matching backend needs. False means "not installed" — the caller should
  // route the operator to showDependencyPrompt rather than silently enabling
  // a feature that will then no-op or crash.

  bool ndiRuntimeAvailable() {
#if defined(DECKBOY_HAS_NDI_SDK)
    // ensureLoaded() tries the DLL search order from ndi_api.hpp and caches
    // the result. Re-calling after a successful first load is effectively
    // free. After a failed first attempt the operator may install NDI and
    // hit the toggle again; we reset attempted to allow a fresh try.
    if (ndiApi_.ensureLoaded()) return true;
    ndiApi_.attempted = false;
    return false;
#else
    return false;
#endif
  }

  bool deckLinkRuntimeAvailable() {
#if defined(DECKBOY_HAS_DECKLINK)
    // listDevices() returns empty in two cases: Desktop Video is not
    // installed at all (CoCreateInstance fails on the CLSID) OR it is
    // installed but no card is present. We treat both as "show the prompt"
    // because the operator's next step is the same either way — install
    // / verify Desktop Video, then connect a device. The prompt body
    // explains both possibilities.
    auto devices = deckboy::platform::video::DeckLinkOutput::listDevices();
    return !devices.empty();
#else
    return false;
#endif
  }

  bool webView2RuntimeAvailable() {
#if defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
    // The official check is GetAvailableCoreWebView2BrowserVersionString
    // from WebView2Loader.dll. Querying the registry would also work but
    // breaks when WebView2 is installed per-user instead of per-machine.
    // We resolve the symbol dynamically to avoid a hard link dependency.
    // The returned buffer is COM-allocated; release with CoTaskMemFree
    // (combaseapi.h, pulled in via the explicit objbase include below).
    HMODULE lib = ::LoadLibraryW(L"WebView2Loader.dll");
    if (!lib) return false;
    using FnPtr = HRESULT (STDAPICALLTYPE *)(PCWSTR, LPWSTR*);
    auto fn = reinterpret_cast<FnPtr>(
      ::GetProcAddress(lib, "GetAvailableCoreWebView2BrowserVersionString"));
    bool available = false;
    if (fn) {
      LPWSTR version = nullptr;
      HRESULT hr = fn(nullptr, &version);
      available = SUCCEEDED(hr) && version != nullptr;
      if (version) ::CoTaskMemFree(version);
    }
    ::FreeLibrary(lib);
    return available;
#else
    return false;
#endif
  }

  // Shortcut helpers — show the prompt with the right text + URL for the
  // backend the operator was trying to enable. Keep these together so a
  // future fourth dependency follows the same shape.
  void promptForNdiRuntime() {
    showDependencyPrompt(
      "NDI Runtime required",
      "NDI Output sends video over IP to NDI receivers. The free NDI Tools "
      "or NDI Runtime installer from Vizrt provides the libraries Deckboy "
      "needs. Install it, then try the toggle again.",
      "https://ndi.video/tools/",
      "Open NDI Tools page");
  }

  void promptForDeckLinkRuntime() {
    showDependencyPrompt(
      "Blackmagic Desktop Video required",
      "DeckLink output needs Blackmagic Desktop Video installed AND a "
      "DeckLink card connected. Install Desktop Video first; if a card is "
      "already plugged in, also check it appears in Blackmagic's Desktop "
      "Video Setup utility.",
      "https://www.blackmagicdesign.com/support/family/capture-and-playback",
      "Open Blackmagic downloads");
  }

  void promptForWebView2Runtime() {
    showDependencyPrompt(
      "Microsoft WebView2 Runtime required",
      "Browser cues render web pages with the Microsoft WebView2 Runtime "
      "(usually preinstalled on Windows 11). Install the free Evergreen "
      "Runtime from Microsoft and try the cue again.",
      "https://developer.microsoft.com/en-us/microsoft-edge/webview2/",
      "Open WebView2 page");
  }

  // Open the dependency prompt with the given message + vendor URL. Caller
  // is responsible for deciding whether the dep is missing in the first
  // place — this is just the presentation step.
  void showDependencyPrompt(const std::string& title,
                            const std::string& body,
                            const std::string& url,
                            const std::string& ctaLabel) {
    depPrompt_.title = title;
    depPrompt_.body = body;
    depPrompt_.url = url;
    depPrompt_.ctaLabel = ctaLabel;
    depPrompt_.active = true;
  }

  void dismissDependencyPrompt() {
    depPrompt_.active = false;
    depPrompt_.ctaRect = {};
    depPrompt_.closeRect = {};
  }

  // Modal informing the operator that a runtime dependency is missing.
  // Mirrors renderQuitConfirm visually so it feels like part of the same
  // dialog family. CTA opens the vendor's download page.
  void renderDependencyPrompt() {
    if (!depPrompt_.active) {
      return;
    }
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);

    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0x0F, 0x38, 0x0F, 180);
    SDL_Rect overlay {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &overlay);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    // Dialog panel sized for the body text — wider than the quit dialog
    // because the body is a short sentence rather than a single label.
    SDL_Rect dialog {(width - 540) / 2, (height - 280) / 2, 540, 280};
    Primitives::drawFramedPanel(controlRenderer_, dialog, pal.light, pal.deep, pal.mid);

    drawTextSafe(controlRenderer_, fontLarge_,
                 SDL_Rect {dialog.x + 28, dialog.y + 28, dialog.w - 56, 32},
                 depPrompt_.title, pal.deep);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {dialog.x + 28, dialog.y + 74, dialog.w - 56, 56},
                 depPrompt_.body, pal.deep);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {dialog.x + 28, dialog.y + 144, dialog.w - 56, 18},
                 depPrompt_.url, pal.inkSoft);

    // Two buttons: CTA (open page) is wider, CLOSE is narrow on the right.
    depPrompt_.ctaRect   = {dialog.x + 28,  dialog.y + 200, dialog.w - 180, 44};
    depPrompt_.closeRect = {dialog.x + dialog.w - 140, dialog.y + 200, 112, 44};
    Primitives::drawFramedPanel(controlRenderer_, depPrompt_.ctaRect, pal.dark, pal.deep, pal.mid);
    Primitives::drawFramedPanel(controlRenderer_, depPrompt_.closeRect, pal.mid, pal.deep, pal.light);
    drawCenteredText(controlRenderer_, fontBase_, depPrompt_.ctaLabel.c_str(), pal.light, depPrompt_.ctaRect);
    drawCenteredText(controlRenderer_, fontBase_, "CLOSE", pal.deep, depPrompt_.closeRect);
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

    // Title — the WORDMARK is headline-sized; the version stays small on
    // the subtitle line (it's metadata, not brand).
    int tx = dialog.x + 36;
    TTF_Font* titleFont = fontPixelTitle_ ? fontPixelTitle_
                        : (fontPixel_ ? fontPixel_ : fontLarge_);
    drawTextSafe(controlRenderer_, titleFont,
                 SDL_Rect {tx, dialog.y + 24, dialog.w - 72, 52},
                 std::string(kAppTitle), pal.fg);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {tx, dialog.y + 78, dialog.w - 72, 18},
                 "dot-matrix cue deck  -  " + std::string(kAppVersionTag), pal.inkSoft);
    // The dialog panel is shell_inner (near-black on terminal themes), so its
    // body text rides the on-body ink roles, not the dark screen_deep/dark.
    SDL_Color dlgInk = pal.fg;
    SDL_Color dlgSub = pal.inkSoft;
    drawTextSafe(controlRenderer_, fontBase_,
                 SDL_Rect {tx, dialog.y + 116, dialog.w - 72, 24},
                 "Choose startup mode:", dlgInk);

    std::string fname = currentProjectFile_.empty() ? "default.deckboy" : currentProjectFile_.filename().string();
    bool hasSavedFile = !currentProjectFile_.empty() && fs::exists(currentProjectFile_);
    if (hasSavedFile) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {tx, dialog.y + 152, dialog.w - 72, 18},
                   "Previous show file:", dlgInk);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {tx, dialog.y + 176, dialog.w - 72, 18},
                   fname, dlgSub);
    } else {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {tx, dialog.y + 160, dialog.w - 72, 18},
                   "No previous show file found at startup path.", dlgSub);
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

    SDL_Color hintInk = pal.inkSoft;
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {tx, dialog.y + 352, dialog.w - 72, 18},
                 "N=new  Enter/P=previous  O=open saved picker",
                 hintInk);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {tx, dialog.y + 374, dialog.w - 72, 18},
                 "Esc=continue with current session",
                 hintInk);
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
                 "KEYBOARD SHORTCUTS", pal.fg);
    // Close hint
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {modal.x + mw - 180, modal.y + 14, 170, 16},
                 "Ctrl+/ to close", pal.inkSoft);

    struct ShortcutEntry { const char* key; const char* desc; };
    static const ShortcutEntry shortcuts[] = {
      {"Enter",           "Take selected cue live"},
      {"Space",           "Play / Pause"},
      {".",               "Skip to next cue"},
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
                   SDL_Rect {cx, rowY, 130, 16}, s.key, pal.fgSoft);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {cx + 134, rowY, colW - 140, 16}, s.desc, pal.fg);
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
    SDL_SetRenderDrawColor(controlRenderer_, pal.deep.r, pal.deep.g, pal.deep.b, 255);
    SDL_Rect full {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &full);
    if (uiPackAvailable_) {
      SDL_Rect artRect {0, 0, width, height};
      // Cycle splashes are grayscale masters — tint them to the theme accent so
      // the boot screen matches whatever colorway is active.
      bool tint = splashTintable_;
      if (tint) {
        ensureUiImageLoaded(uiSplashArt_);
        if (uiSplashArt_.texture) {
          SDL_SetTextureColorMod(uiSplashArt_.texture, pal.light.r, pal.light.g, pal.light.b);
        }
      }
      drawUiImageCover(uiSplashArt_, artRect, tint ? 235 : 220);
      if (tint && uiSplashArt_.texture) {
        SDL_SetTextureColorMod(uiSplashArt_.texture, 255, 255, 255);
      }
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
                 std::string(kAppTitle) + " " + std::string(kAppVersionTag), pal.fg);
    drawTextSafe(controlRenderer_, fontBase_,
                 SDL_Rect {card.x + 38, card.y + 76, card.w - 76, 24},
                 "dot-matrix cue deck", pal.inkSoft);

    // ── Boot console — nested framed panel ──
    // Real init values interleaved with the important diagnostics, built
    // once per boot (startupBootLog_) and revealed line by line. Uses the
    // tile fill + fg text so it reads as a dark terminal console on OLED
    // themes and a bright panel on light themes.
    SDL_Rect bootRect {card.x + 36, card.y + 126, card.w - 72, 196};
    Primitives::drawFramedPanel(controlRenderer_, bootRect, pal.tile, pal.deep, pal.mid);

    if (startupBootLog_.empty()) {
      auto [bootW, bootH] = outputRenderSizeForOutput(0);
      size_t bootCues = 0;
      for (const auto& bootDeck : project_.decks) bootCues += bootDeck.cues.size();
      std::string themeName = currentThemeName_.empty() ? "gameboy" : currentThemeName_;

      uint32_t seed = static_cast<uint32_t>(SDL_GetTicks() ^ 0x9E3779B9u);
      auto nextRand = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 16;
      };

      // The classic boot tasks joined by real init values — these always
      // print because they carry live numbers the operator can trust.
      startupBootLog_.push_back("cue gremlin bios " + std::string(kAppVersionTag) + "...");
      startupBootLog_.push_back("checking dot-matrix memory... 8192 KB OK");
      startupBootLog_.push_back("initializing deck runtime...");
      startupBootLog_.push_back("loading theme '" + themeName + "'... ok");
      startupBootLog_.push_back("mounting font cartridges... 6 faces");
      startupBootLog_.push_back("loading outputs... "
        + std::to_string(bootW) + "x" + std::to_string(bootH)
        + " on " + std::to_string(std::max(0, deckboyGetNumVideoDisplays())) + " display(s)");
      startupBootLog_.push_back("starting compositor...");
      startupBootLog_.push_back("spooling ffmpeg decode pipeline... armed");
      startupBootLog_.push_back("negotiating audio bus... "
        + std::to_string(project_.audioBufferSamples) + " sample buffer");
      startupBootLog_.push_back("shelving cartridges... "
        + std::to_string(project_.decks.size()) + " deck(s), "
        + std::to_string(bootCues) + " cues");
      startupBootLog_.push_back("opening companion port " + std::to_string(companionPort_) + "...");
      startupBootLog_.push_back(std::string("waking ndi mesh... ")
        + (ndiRuntimeAvailable() ? "listening" : "not installed"));

      // The sci-fi subsystems the hardware team insists are real: a wide
      // pool, a fresh random hand each boot so the console stays quotable.
      static const char* kBootWhimsyPool[] = {
        "reticulating splines...",
        "charging flux capacitor... 1.21 GW nominal",
        "engaging heisenberg compensators... probably",
        "dilithium matrix... crystal ok",
        "arming safety guards...",
        "pressurizing gremlin containment field...",
        "dampening tachyon feedback... 0.003%",
        "calibrating rubber chicken... ok",
        "aligning chroma phase array... locked",
        "defragmenting pixel silo... 3 shards",
        "warming vacuum tubes... 340K",
        "counting frame ghosts... none found",
        "bribing the vsync daemon... accepted",
        "untangling patch cables... 12 m reclaimed",
        "polishing scanlines... streak-free",
        "feeding the cue gremlins... fed",
        "rewinding cassette buffers... ok",
        "degaussing crt yoke... thunk",
        "consulting the show bible... canon",
        "sweeping dead pixels... 0 swept",
        "tuning subcarrier... 4.43361875 MHz",
        "greasing the t-bar... smooth",
        "waking the intern... declined",
        "sorting loose bnc caps... 7 found",
        "petting the watchdog... good boy",
        "spinning up hamster wheel... 88 mph",
        "checking for y2k residue... clean",
        "certifying blinkenlights... blinken",
        "torquing pixel bolts... 12 Nm",
        "asking the house electrician... granted",
        "phase-locking disco ball... 33 rpm",
        "shooing moths from the beam... 2 shooed",
        "laminating run sheet... crisp",
        "priming confetti cannons... standby",
        "zeroing the applause meter...",
        "warming green room coffee... 74C",
        "counting backstage flashlights... all lit",
        "ironing the pixel grid... flat",
        "auditioning standby pixels... cast",
        "sandbagging the render queue... secure",
      };
      constexpr int kWhimsyCount = static_cast<int>(std::size(kBootWhimsyPool));
      // Deal a hand of 8 distinct lines via partial Fisher-Yates.
      int whimsyIdx[kWhimsyCount];
      for (int i = 0; i < kWhimsyCount; ++i) whimsyIdx[i] = i;
      constexpr int kHand = 8;
      for (int i = 0; i < kHand; ++i) {
        int j = i + static_cast<int>(nextRand() % static_cast<uint32_t>(kWhimsyCount - i));
        std::swap(whimsyIdx[i], whimsyIdx[j]);
      }
      // Sprinkle the hand through the back half of the console (after the
      // early real-value lines, before the final status).
      for (int i = 0; i < kHand; ++i) {
        size_t minPos = 7;
        size_t span = startupBootLog_.size() - minPos;
        size_t pos = minPos + static_cast<size_t>(nextRand() % static_cast<uint32_t>(span + 1));
        startupBootLog_.insert(startupBootLog_.begin() + pos, kBootWhimsyPool[whimsyIdx[i]]);
      }
      // Konami hint + A/V clock line ride near the end every boot, then the
      // handoff to the operator.
      startupBootLog_.push_back("polling konami interrupt vector... hidden");
      startupBootLog_.push_back("syncing wall clock to audio crystal...");
      startupBootLog_.push_back("boot ok - awaiting operator");

      // Randomized reveal schedule — real boots stutter: quick bursts,
      // normal lines, and the occasional probe that stalls. Scaled so the
      // whole sequence always lands within the splash window.
      startupBootLogAtMs_.clear();
      Uint64 at = 0;
      for (size_t i = 0; i < startupBootLog_.size(); ++i) {
        uint32_t roll = nextRand() % 100;
        Uint64 delay;
        if (roll < 25) delay = 30 + nextRand() % 60;        // burst
        else if (roll < 85) delay = 110 + nextRand() % 200; // normal
        else delay = 480 + nextRand() % 420;                // hardware stall
        at += delay;
        startupBootLogAtMs_.push_back(at);
      }
      if (!startupBootLogAtMs_.empty() && startupBootLogAtMs_.back() > 4300) {
        double scale = 4300.0 / static_cast<double>(startupBootLogAtMs_.back());
        for (auto& ts : startupBootLogAtMs_) {
          ts = static_cast<Uint64>(ts * scale);
        }
      }
    }
    Uint64 now = SDL_GetTicks();
    Uint64 elapsed = splashStartedAt_ > 0 ? (now - splashStartedAt_) : 0;
    int totalLines = static_cast<int>(startupBootLog_.size());
    int visibleLines = 1;
    for (size_t i = 0; i < startupBootLogAtMs_.size(); ++i) {
      if (startupBootLogAtMs_[i] <= elapsed) {
        visibleLines = static_cast<int>(i) + 1;
      }
    }
    visibleLines = std::clamp(visibleLines, 1, totalLines);
    // Console-style scroll: the panel holds a window of lines; once the log
    // outgrows it, older lines scroll off the top.
    int lineStep = std::max(16, textLineHeight(fontSmall_) + 2);
    int fitLines = std::max(1, (bootRect.h - 20) / lineStep);
    int firstLine = std::max(0, visibleLines - fitLines);
    for (int i = firstLine; i < visibleLines; ++i) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {bootRect.x + 12, bootRect.y + 10 + (i - firstLine) * lineStep,
                             bootRect.w - 24, lineStep},
                   startupBootLog_[i], pal.fg);
    }
    if (visibleLines < totalLines && ((now / 300) % 2) == 0) {
      int cursorRow = std::min(visibleLines - firstLine, fitLines - 1);
      drawText(controlRenderer_, fontSmall_, "_", pal.fg,
               bootRect.x + 12, bootRect.y + 10 + cursorRow * lineStep);
    }

    // ── Hint text ──
    drawText(controlRenderer_, fontBase_, "press ENTER to start",
             pal.fg, card.x + 36, card.y + card.h - 74);
    drawText(controlRenderer_, fontSmall_, "Esc or click to skip",
             pal.inkSoft, card.x + 36, card.y + card.h - 44);

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
