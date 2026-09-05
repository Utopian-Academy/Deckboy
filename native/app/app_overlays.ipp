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

  // ---- HAP suggestion ------------------------------------------------------
  // Offer HAP only when it would actually pay, and say what it costs. MEASURED
  // on this machine, 5s of 1080p:
  //   per-stream CPU (1 thread)   H.264 454ms   HAP 265ms   -> 1.7x cheaper
  //   six concurrent (1 thread)   H.264 540ms   HAP 361ms
  //   file size                   H.264 4.5MB   HAP 19.3MB  -> 4.3x bigger
  // So the win is CPU per LAYER, which compounds with how many run at once,
  // and the price is disk. A single-layer show gains almost nothing and pays
  // the full 4x, which is why this does not fire on cue count alone.
  //
  // Trigger: the operator has actually SEEN decode trouble (a stall), or is
  // running video on more than one deck at once. Never twice in a session, and
  // never again once dismissed.
  void maybeSuggestHapConversion() {
    if (hapSuggestionShown_ || project_.hapSuggestionDismissed) return;
    if (depPrompt_.active) return;
    if (!encoderHasHapSupport()) return;

    int liveVideoDecks = 0;
    for (const Deck& deck : project_.decks) {
      if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) continue;
      const Cue& cue = deck.cues[deck.activeIndex];
      if (cue.kind == CueKind::Video && !cue.path.empty()) ++liveVideoDecks;
    }
    const bool worthIt = hapStallSeen_ || liveVideoDecks >= 2;
    if (!worthIt) return;

    // Count what would convert, and what it would cost on disk.
    std::vector<std::pair<int, int>> targets;   // deck, cue
    std::uintmax_t sourceBytes = 0;
    std::error_code ec;
    for (std::size_t d = 0; d < project_.decks.size(); ++d) {
      const Deck& deck = project_.decks[d];
      for (std::size_t c = 0; c < deck.cues.size(); ++c) {
        const Cue& cue = deck.cues[c];
        if (cue.kind != CueKind::Video || cue.path.empty()) continue;
        const std::string ext = toLower(fs::path(cue.path).extension().string());
        if (ext == ".mov" && toLower(cue.videoCodec).find("hap") != std::string::npos) continue;
        auto n = fs::file_size(fs::path(cue.path), ec);
        if (!ec) sourceBytes += n;
        targets.emplace_back(static_cast<int>(d), static_cast<int>(c));
      }
    }
    if (targets.empty()) return;

    // 4.3x measured, rounded up. If the disk cannot take it, say so and offer
    // nothing -- an offer that fills the operator's disk mid-show is worse
    // than no offer.
    const std::uintmax_t needBytes = static_cast<std::uintmax_t>(sourceBytes * 4.5);
    auto space = fs::space(convertedMediaDir().empty() ? Paths::stateDir() : convertedMediaDir(), ec);
    const bool roomOnDisk = !ec && space.available > needBytes;

    hapSuggestionShown_ = true;
    const int mb = static_cast<int>(needBytes / (1024 * 1024));
    std::string body =
      "This show runs several video layers at once. HAP costs about 1.7x less "
      "CPU per layer than H.264, which is what decides how many you can run "
      "together, and it seeks instantly because every frame is a keyframe.\n\n"
      "It is not free: HAP files are roughly 4x larger. Converting "
      + std::to_string(static_cast<int>(targets.size())) + " clip(s) needs about "
      + std::to_string(mb) + " MB.\n\n"
      "Originals are kept either way.";
    if (!roomOnDisk) {
      body += "\n\nThere is NOT enough free space on the encoder's disk for this, "
              "so conversion is not offered. Point the encoder at a bigger drive "
              "in Settings > Encoder if you want to reconsider.";
    }
    depPrompt_ = DependencyPromptState{};
    depPrompt_.title = "HAP would help this show";
    depPrompt_.body = body;
    depPrompt_.ctaLabel = roomOnDisk
      ? ("Convert " + std::to_string(static_cast<int>(targets.size())) + " clip(s) to HAP")
      : "";
    if (roomOnDisk) {
      depPrompt_.onCta = [this, targets]() { convertCuesToHap(targets); };
    }
    // CLOSING IT MEANS NEVER AGAIN, which is what the note above this
    // function has always promised. project_.hapSuggestionDismissed is saved
    // with the show and was read here and set NOWHERE, so the suggestion came
    // back every launch with no way to stop it -- the session flag only holds
    // until the app restarts.
    depPrompt_.onDismiss = [this]() {
      if (!project_.hapSuggestionDismissed) {
        project_.hapSuggestionDismissed = true;
        markProjectDirty();
      }
    };
    depPrompt_.active = true;
  }

  void dismissDependencyPrompt() {
    if (depPrompt_.onDismiss) {
      depPrompt_.onDismiss();
    }
    // Cleared whole rather than field by field, so a prompt cannot leave a
    // callback behind for the next one to run.
    depPrompt_ = DependencyPromptState{};
  }

  // Modal informing the operator that a runtime dependency is missing.
  // Mirrors renderQuitConfirm visually so it feels like part of the same
  // dialog family. CTA opens the vendor's download page.
  // ── Loading overlay ────────────────────────────────────────────────────────
  // Opening a 1,500-cue show parses, resolves and builds runtimes with the
  // render loop stopped, so the window just sat there looking hung. This draws
  // and PRESENTS its own frames from inside that work, which is the only way to
  // show anything while the main loop is not turning.
  //
  // It stays hidden for the first quarter second: a loader that flashes up on
  // every small file is worse than no loader at all, and most shows open too
  // fast to need one.
  void beginLoadingOverlay(std::string title, std::string detail = {}) {
    loadingTitle_ = std::move(title);
    loadingDetail_ = std::move(detail);
    loadingFrac_ = 0.0;
    loadingStartMs_ = SDL_GetTicks();
    loadingLastPresentMs_ = 0;
    loadingActive_ = true;
    // Fresh hand of quips per load, so a long open isn't the same three lines.
    loadingQuipSeed_ = static_cast<unsigned>(loadingStartMs_);
  }

  void loadingOverlayProgress(double frac, const std::string& detail = {}) {
    if (!loadingActive_) {
      return;
    }
    loadingFrac_ = std::clamp(frac, 0.0, 1.0);
    if (!detail.empty()) {
      loadingDetail_ = detail;
    }
    const Uint64 now = SDL_GetTicks();
    constexpr Uint64 kAppearAfterMs = 250;   // don't flash on quick opens
    constexpr Uint64 kFramePeriodMs = 33;    // ~30fps is plenty for a loader
    if (now - loadingStartMs_ < kAppearAfterMs) {
      return;
    }
    if (loadingLastPresentMs_ != 0 && now - loadingLastPresentMs_ < kFramePeriodMs) {
      return;
    }
    loadingLastPresentMs_ = now;
    // Keep Windows from marking the app "not responding" during a long open.
    SDL_PumpEvents();
    renderLoadingOverlayFrame();
  }

  void endLoadingOverlay() {
    loadingActive_ = false;
    loadingLastPresentMs_ = 0;
  }

  void renderLoadingOverlayFrame() {
    if (!controlRenderer_) {
      return;
    }
    int winW = 0, winH = 0;
    SDL_GetCurrentRenderOutputSize(controlRenderer_, &winW, &winH);
    if (winW <= 0 || winH <= 0) {
      return;
    }
    const Uint64 nowMs = SDL_GetTicks();

    SDL_SetRenderDrawColor(controlRenderer_, pal.shellOuter.r, pal.shellOuter.g,
                           pal.shellOuter.b, 255);
    SDL_RenderClear(controlRenderer_);

    const int panelW = std::min(560, std::max(320, winW - 120));
    const int panelH = 190;
    SDL_Rect panel {(winW - panelW) / 2, (winH - panelH) / 2, panelW, panelH};
    Primitives::drawFramedPanel(controlRenderer_, panel, pal.shellInner, pal.deep, pal.light);

    drawCenteredTextSafe(controlRenderer_, fontPixelSmall_ ? fontPixelSmall_ : fontSmall_,
                         SDL_Rect{panel.x, panel.y + 16, panel.w, 20},
                         loadingTitle_, pal.fg);

    // The cartridge: a row of blocks that fill left to right, with the leading
    // block pulsing. Reads as "something is happening" even when the percentage
    // is stuck on a slow drive.
    constexpr int kBlocks = 12;
    const int barW = panel.w - 64;
    const int blockW = barW / kBlocks;
    const int barX = panel.x + (panel.w - blockW * kBlocks) / 2;
    const int barY = panel.y + 62;
    const int filled = static_cast<int>(std::lround(loadingFrac_ * kBlocks));
    for (int i = 0; i < kBlocks; ++i) {
      SDL_Rect b {barX + i * blockW + 2, barY, blockW - 4, 22};
      if (i < filled) {
        Primitives::fillRect(controlRenderer_, b, pal.light);
      } else if (i == filled) {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(nowMs) * 0.010f);
        SDL_Color c {
          static_cast<Uint8>(pal.mid.r + (pal.light.r - pal.mid.r) * pulse),
          static_cast<Uint8>(pal.mid.g + (pal.light.g - pal.mid.g) * pulse),
          static_cast<Uint8>(pal.mid.b + (pal.light.b - pal.mid.b) * pulse),
          255};
        Primitives::fillRect(controlRenderer_, b, c);
      } else {
        Primitives::fillRect(controlRenderer_, b, pal.deep);
      }
      Primitives::strokeRect(controlRenderer_, b, pal.mid);
    }

    char pct[16];
    std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(std::lround(loadingFrac_ * 100.0)));
    drawCenteredTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{panel.x, barY + 30, panel.w, 18}, pct, pal.fgSoft);

    if (!loadingDetail_.empty()) {
      drawCenteredTextSafe(controlRenderer_, fontSmall_,
                           SDL_Rect{panel.x + 16, barY + 52, panel.w - 32, 18},
                           loadingDetail_, pal.inkSoft);
    }

    // Same playful register as the boot console — a slow open should feel like
    // the machine is doing something charming, not like it has died.
    static const char* kQuips[] = {
      "waking the cue gremlins",
      "counting frames by hand",
      "buttering the playhead",
      "reticulating playlists",
      "asking the drive nicely",
      "warming the flux capacitor",
      "alphabetising the sprockets",
      "feeding the terrarium",
    };
    constexpr int kQuipCount = static_cast<int>(sizeof(kQuips) / sizeof(kQuips[0]));
    const int quip = static_cast<int>(((nowMs / 1400) + loadingQuipSeed_) % kQuipCount);
    drawCenteredTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{panel.x + 16, panel.y + panel.h - 34, panel.w - 32, 18},
                         kQuips[quip], pal.mid);

    SDL_RenderPresent(controlRenderer_);
    revealControlWindow();  // the loading overlay, which can be the first frame when a show is opening
  }

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

    // Button width is derived from the MEASURED labels in the current font, and
    // the dialog is then sized to hold them — not the other way round. The old
    // code fixed the dialog at 660 and the buttons at 184, which left the three
    // labels nearly filling the panel: fine in Segoe UI, but "NEW SHOW FILE"
    // and "OPEN SAVED" ellipsized under the wider Arial/Liberation used on macOS
    // and Linux ("NEW SHOW F...", "OPEN SAVED..."). This is the splash text the
    // operator sees first, so it truncating reads as a broken build.
    const char* kStartupLabels[3] = {"NEW SHOW FILE", "OPEN PREVIOUS", "OPEN SAVED"};
    int startupWidest = 0;
    for (const char* label : kStartupLabels) {
      int lw = 0, lh = 0;
      if (fontBase_) {
        TTF_GetStringSize(fontBase_, label, std::strlen(label), &lw, &lh);
      }
      startupWidest = std::max(startupWidest, lw);
    }
    // +28 covers drawCenteredText's inset and the framed-panel bevel; 184 keeps
    // the original size as a floor so Segoe UI is unchanged.
    const int startupBtnW = std::max(184, startupWidest + 28);
    const int startupBtnGap = 10;
    const int startupMargin = 36;
    const int startupRowW = startupBtnW * 3 + startupBtnGap * 2;

    // Dialog panel — wide enough for the button row plus margins, never narrower
    // than the original 660.
    const int kDW = std::max(660, startupRowW + startupMargin * 2);
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

    // Buttons — centred as a row using the measured width computed above, so
    // they stay inside the (possibly widened) dialog on every font.
    int buttonY = dialog.y + 270;
    int buttonH = 58;
    int buttonRowX = dialog.x + (dialog.w - startupRowW) / 2;
    startupNewBtn_ = {buttonRowX, buttonY, startupBtnW, buttonH};
    startupLoadBtn_ = {startupNewBtn_.x + startupBtnW + startupBtnGap, buttonY, startupBtnW, buttonH};
    startupOpenSavedBtn_ = {startupLoadBtn_.x + startupBtnW + startupBtnGap, buttonY, startupBtnW, buttonH};

    Primitives::drawFramedPanel(controlRenderer_, startupNewBtn_, pal.mid,
                                pal.deep, pal.light);
    drawCenteredText(controlRenderer_, fontBase_, "NEW SHOW FILE", pal.deep, startupNewBtn_);

    SDL_Color loadFill = hasSavedFile ? pal.dark : pal.shellOuter;
    SDL_Color loadText = hasSavedFile ? pal.light : pal.mid;
    Primitives::drawFramedPanel(controlRenderer_, startupLoadBtn_, loadFill, pal.deep, pal.mid);
    drawCenteredText(controlRenderer_, fontBase_, hasSavedFile ? "OPEN PREVIOUS" : "NO PREVIOUS", loadText, startupLoadBtn_);

    Primitives::drawFramedPanel(controlRenderer_, startupOpenSavedBtn_, pal.mid,
                                pal.deep, pal.light);
    // "OPEN SAVED" without the trailing "..." — it cost width for no meaning
    // (this is a button; clicking it opens the picker regardless).
    drawCenteredText(controlRenderer_, fontBase_, "OPEN SAVED", pal.deep, startupOpenSavedBtn_);

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


  // ── THE DASHBOARD ──────────────────────────────────────────────────────────
  //
  // A page of the operator's own buttons. Each tile runs any command the remote
  // protocol understands, so anything the app can do can be put on one -- and
  // the same slots are what Companion fires with DASH <n>, so a surface and the
  // screen stay the same dashboard rather than two that drift.
  //
  // MODULAR: the grid flows to the window. Tiles are added and removed, and the
  // rest reflow around them; nothing is positioned by hand.
  //
  // ANIMATED: every tile floats on its own phase, so the page breathes instead
  // of sitting there, and a fired tile squishes and springs back. The motion is
  // per-tile and slow -- a dashboard that jitters is a dashboard nobody can
  // read at a glance across a room.
  //
  // The colours come out of the THEME, not out of a fixed sixteen: an indexed
  // palette of its own would be the one thing on screen ignoring the colourway.
  SDL_Color dashboardSlotColor(int index) const {
    // Four tints between the theme's own roles, so a slot's colour means
    // "different from its neighbour" in every theme rather than "red".
    const int i = ((index % 8) + 8) % 8;
    const SDL_Color a = pal.light;
    const SDL_Color b = pal.mid;
    const SDL_Color c = pal.fg;
    auto mix = [](SDL_Color x, SDL_Color y, double f) {
      SDL_Color o;
      o.r = static_cast<Uint8>(std::lround(x.r * (1.0 - f) + y.r * f));
      o.g = static_cast<Uint8>(std::lround(x.g * (1.0 - f) + y.g * f));
      o.b = static_cast<Uint8>(std::lround(x.b * (1.0 - f) + y.b * f));
      o.a = 255;
      return o;
    };
    switch (i) {
      case 0:  return a;
      case 1:  return mix(a, b, 0.35);
      case 2:  return mix(a, c, 0.30);
      case 3:  return b;
      case 4:  return mix(b, c, 0.35);
      case 5:  return mix(a, b, 0.65);
      case 6:  return mix(b, a, 0.20);
      default: return mix(c, a, 0.55);
    }
  }

  void renderDashboardOverlay() {
    dashButtons_.clear();
    dashModalRect_ = SDL_Rect {};
    if (!dashboardOverlayOpen_) return;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(controlWindow_, &ww, &wh);
    const double t = static_cast<double>(animationNow_) / 1000.0;

    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 190);
    SDL_Rect full {0, 0, ww, wh};
    SDL_RenderFillRect(controlRenderer_, &full);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    const int mw = std::min(uiScaled(900), ww - 40);
    const int mh = std::min(uiScaled(640), wh - 40);
    SDL_Rect modal {(ww - mw) / 2, (wh - mh) / 2, mw, mh};
    dashModalRect_ = modal;
    Primitives::drawFramedPanel(controlRenderer_, modal, pal.shellInner, pal.deep, pal.shellOuter);
    drawTextSafe(controlRenderer_, fontBase_,
                 SDL_Rect {modal.x + 16, modal.y + 10, mw - 200, 24},
                 "DASHBOARD", pal.fg);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {modal.x + mw - 190, modal.y + 14, 180, 16},
                 "Ctrl+D or Esc to close", pal.fgSoft);

    // ── The grid ────────────────────────────────────────────────────────────
    const int pad = uiScaled(14);
    // Clear of the title AND its baseline. At 40 the first row of tiles rode
    // up over the word DASHBOARD -- the tiles float by a couple of pixels, so
    // a gap that only just fits is a gap that intermittently does not.
    const int headerH = uiScaled(56);
    SDL_Rect area {modal.x + pad, modal.y + headerH,
                   mw - pad * 2, mh - headerH - pad};
    const int tileW = uiScaled(150);
    const int tileH = uiScaled(96);
    const int gap = uiScaled(10);
    const int cols = std::max(1, (area.w + gap) / (tileW + gap));

    const int slots = static_cast<int>(project_.dashboard.size());
    // One trailing tile to add another, so the page is never a dead end.
    const int cells = slots + 1;

    for (int i = 0; i < cells; ++i) {
      const int col = i % cols;
      const int row = i / cols;
      int tx = area.x + col * (tileW + gap);
      int ty = area.y + row * (tileH + gap);
      if (ty + tileH > area.y + area.h) {
        break;   // the page is full; the rest wait for a bigger window
      }

      // Each tile drifts on its own phase. Slow, and only a couple of pixels:
      // enough to be alive, not enough to make a target move under a finger.
      const double phase = i * 0.7;
      const int fx = static_cast<int>(std::lround(std::sin(t * 0.8 + phase) * 2.0));
      const int fy = static_cast<int>(std::lround(std::sin(t * 0.6 + phase * 1.3) * 2.5));

      // A fired tile squishes and springs back over a third of a second.
      double squish = 0.0;
      if (dashPressedSlot_ == i && animationNow_ >= dashPressedAtMs_) {
        const double age = static_cast<double>(animationNow_ - dashPressedAtMs_) / 320.0;
        if (age < 1.0) {
          squish = std::sin(age * 3.14159265358979) * (1.0 - age) * 6.0;
        }
      }
      const int sq = static_cast<int>(std::lround(squish));
      SDL_Rect tile {tx + fx + sq, ty + fy + sq / 2,
                     tileW - sq * 2, tileH - sq};

      const bool isAdd = (i >= slots);
      if (isAdd) {
        Primitives::drawFramedPanel(controlRenderer_, tile, pal.shellInner, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontLarge_ ? fontLarge_ : fontBase_,
                             SDL_Rect{tile.x, tile.y + tile.h / 2 - uiScaled(16),
                                      tile.w, uiScaled(28)},
                             "+", pal.fgSoft);
        drawCenteredTextSafe(controlRenderer_, fontSmall_,
                             SDL_Rect{tile.x, tile.y + tile.h - uiScaled(22),
                                      tile.w, uiScaled(16)},
                             "add a button", pal.fgSoft);
        dashButtons_.push_back({tile, QuickAction::DashSlotAdd,
                                 "Add a dashboard button", i});
        continue;
      }

      const DashboardSlot& slot = project_.dashboard[i];
      const SDL_Color tint = dashboardSlotColor(slot.colorIndex);
      Primitives::drawFramedPanel(controlRenderer_, tile, tint, pal.deep, pal.shellOuter);

      // The glyph breathes very slightly on its own clock, which is what makes
      // the tile read as a little creature rather than a rectangle.
      const std::string glyph = slot.glyph.empty() ? std::string("*") : slot.glyph;
      const int glyphDrop = static_cast<int>(std::lround(std::sin(t * 1.4 + phase) * 1.5));
      drawCenteredTextSafe(controlRenderer_, fontLarge_ ? fontLarge_ : fontBase_,
                           SDL_Rect{tile.x, tile.y + uiScaled(10) + glyphDrop,
                                    tile.w, uiScaled(30)},
                           glyph, pal.deep);

      const std::string label = slot.label.empty()
        ? (slot.command.empty() ? std::string("(empty)") : slot.command)
        : slot.label;
      drawCenteredTextSafe(controlRenderer_, fontSmall_,
                           SDL_Rect{tile.x + 4, tile.y + tile.h - uiScaled(34),
                                    tile.w - 8, uiScaled(16)},
                           label, pal.deep);

      // Firing is the whole tile; the two small controls sit in the bottom
      // corners so a fat finger aiming at the middle can never hit them.
      SDL_Rect editBtn {tile.x + tile.w - uiScaled(22), tile.y + tile.h - uiScaled(16),
                        uiScaled(18), uiScaled(13)};
      SDL_Rect colBtn {tile.x + uiScaled(4), tile.y + tile.h - uiScaled(16),
                       uiScaled(18), uiScaled(13)};
      dashButtons_.push_back({tile, QuickAction::DashSlotFire,
                               slot.command.empty() ? "Empty - use the pencil to set a command"
                                                    : ("Run: " + slot.command), i});
      Primitives::drawFramedPanel(controlRenderer_, colBtn,
                                  dashboardSlotColor(slot.colorIndex + 1), pal.deep, pal.deep);
      dashButtons_.push_back({colBtn, QuickAction::DashSlotColor, "Change this tile's colour", i});
      // On its own plate. Drawn straight onto the tile it was ink-on-ink at
      // several tints and simply vanished.
      Primitives::drawFramedPanel(controlRenderer_, editBtn, pal.shellInner, pal.deep, pal.deep);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, editBtn, "...", pal.fg);
      dashButtons_.push_back({editBtn, QuickAction::DashSlotEdit,
                               "Set this button's label, command and glyph", i});
    }

    if (slots == 0) {
      drawCenteredTextSafe(controlRenderer_, fontSmall_,
                           SDL_Rect{area.x, area.y + area.h - uiScaled(26),
                                    area.w, uiScaled(18)},
                           "a button can run any command the network protocol understands",
                           pal.fgSoft);
    }
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

    // These MUST match handleKeyDown in app_input.ipp. Audited v0.81.5, where
    // seven entries were found to be fiction: Ctrl+O was listed twice (it sets
    // the out point and returns early, so it never opened a project — that is
    // bare O), H/N/B/P named actions their keys do not perform (hold is E, N is
    // NDI send, B adds a browser cue, P adds a pattern cue), G advertised an
    // overlay feature that is parked, and Backspace was described as "clear all
    // overlays" while it silently DELETES THE SELECTED CUE when no overlay is
    // active. If you add or move a key binding, update this table in the same
    // commit.
    struct ShortcutEntry { const char* key; const char* desc; };
    static const ShortcutEntry shortcuts[] = {
      {"Enter",           "Take selected cue live"},
      {"Space",           "Play / Pause"},
      {". / ,",           "Skip to next / previous cue"},
      {"PgDn / PgUp",     "Same, for a presenter remote"},
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
      {"O",               "Open project"},
      {"Ctrl+N",          "New project"},
      {"L",               "Toggle loop"},
      {"E",               "Toggle hold (pause at end)"},
      {"X",               "Cycle end action"},
      {"K",               "Cycle color tag"},
      {"J",               "Jump to the live cue"},
      {"B",               "Blackout - instant, playback continues"},
      {"C",               "Clear output - fade, stops playback"},
      {"U",               "Clear overlays"},
      {"N",               "Toggle NDI send"},
      {"F",               "Toggle fullscreen output"},
      {"F11",             "Fullscreen the control window"},
      {"Shift+B",         "Add browser cue"},
      {"P",               "Add pattern cue"},
      {"A / D",           "Cycle audio device / display"},
      {"T",               "Run timecode"},
      {"Shift+O",         "Toggle time overlay"},
      {"[ / ]",           "Shorten / lengthen fade"},
      {"Esc",             "Desk, then clear output, then quit"},
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
      // Tint strength depends on which pool the art came from. The grayscale
      // masters take the theme accent FULLY -- they have no colour of their own
      // to lose. The colour scenes take it only PARTLY: colour-modulation is a
      // multiply, so tinting a saturated image to the accent muddies it, but
      // leaving it untouched makes it ignore the colorway entirely. Mixing the
      // accent toward white pulls the scene toward the theme while keeping the
      // artwork's own palette readable.
      const float k = splashTintStrength_;
      const bool tint = k > 0.01f;
      if (tint) {
        ensureUiImageLoaded(uiSplashArt_);
        if (uiSplashArt_.texture) {
          auto mix = [k](Uint8 accent) {
            return static_cast<Uint8>(255.0f + (accent - 255.0f) * k);
          };
          SDL_SetTextureColorMod(uiSplashArt_.texture,
                                 mix(pal.light.r), mix(pal.light.g), mix(pal.light.b));
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
        "spinning down the tape reels... ok",
        "buffering the laugh track... 1 loop",
        "calibrating the fog machine... haze nominal",
        "checking gaff tape reserves... 3 rolls",
        "aligning the follow spot... on their mark",
        "rechecking the redundant redundancy... twice",
        "waxing the fader caps... slick",
        "flushing the frame buffer... whoosh",
        "counting spare fuses... 5A x4",
        "negotiating with the smoke detector... truce",
        "seeding the random number goblin...",
        "syncing genlock to the moon... tidal",
        "topping up phantom power... +48V",
        "checking the gate for hairs... clean",
        "tightening truss bolts... 40 Nm",
        "coiling cables over-under... tidy",
        "labelling the mystery cable... 'do not touch'",
        "polling the stage manager... standing by",
        "counting gobos... 24 in the wheel",
        "leveling the turntable... 33 1/3",
        "checking latency budget... 2 frames",
        "waking the render farm... 1 node, brave",
        "aligning projector convergence... rgb stacked",
        "checking the fire curtain... ready",
        "chalking the spike marks... taped",
        "muting the green room monitor... shh",
        "swapping the gaffer's AA batteries... fresh",
        "priming haze timing... 8 s",
      };
      constexpr int kWhimsyCount = static_cast<int>(std::size(kBootWhimsyPool));
      // Deal a random-sized hand of distinct lines via partial Fisher-Yates.
      // The size itself varies per boot (9-13) so no two boots feel alike.
      int whimsyIdx[kWhimsyCount];
      for (int i = 0; i < kWhimsyCount; ++i) whimsyIdx[i] = i;
      int kHand = 9 + static_cast<int>(nextRand() % 5);
      kHand = std::min(kHand, kWhimsyCount);
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
