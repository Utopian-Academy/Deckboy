// ============================================================================
// app_cue_transport.ipp — Cue playback transport operations.
//
// Implements the core transport controls for cue playback:
//
//   triggerButton()      — dispatches UI button presses (IMPORT, TAKE, STOP, etc.)
//   goNextCue()          — advance to the next cue and start playback
//   jumpSelectedCue()    — TAKE: load and play the selected cue immediately
//   stopTransport()      — stop all playback with optional fade-out
//   rerackTransport()    — rewind to the beginning of the current cue
//   clearOutput()        — stop playback and clear the output to black
//   playTransport()      — start/resume playback of the active cue
//   pauseTransport()     — pause playback at the current position
//   togglePlayPause()    — toggle between play and pause states
//   fadeOutAndStop()     — fade audio and/or video, then stop
//
// Also handles:
//   - Auto-advance: chain cues with configurable end actions
//   - Crossfade transitions between cues
//   - Still timer management for image/pattern cues
//   - In/out point trimming for seek operations
//   - NMC sync transport broadcasting
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Dispatch a UI button press by its label string.
  // Maps button labels to transport and management operations.
  void triggerButton(const std::string& label) {
    if (label == "IMPORT") {
      importWithPicker();
    } else if (label == "BROWSER") {
      addBrowserCueFromPrompt();
    } else if (label == "SOURCE") {
      openSourceTypeMenu();
    } else if (label == "PATTERN") {
      addKawaiiPatternCue();
    } else if (label == "TAKE") {
      jumpSelectedCue();
    } else if (label == "RERACK") {
      rerackTransport();
    } else if (label == "STOP") {
      stopTransport();
    } else if (label == "CLEAR") {
      clearOutput();
    } else if (label == "BLACKOUT") {
      // Instant and reversible: kills the picture without touching playback,
      // so the show keeps running underneath and one press brings it back.
      const bool dark = masterDimmerTarget_ < 0.5;
      masterDimmerTarget_ = dark ? 1.0 : 0.0;
      triggerToast(dark ? "blackout off" : "BLACKOUT");
      playUiSound(dark ? UiSoundEffect::Toggle : UiSoundEffect::Clear);
    } else if (label == "SETTINGS") {
      settingsOpen_ = true;
      settingsTab_ = 3;
      uiWatchdogPopupEvent("settings_modal", true);
    }
  }

  // Resolve where a deck's playback naturally goes after activeCue: goto
  // target first, then shuffle (when advancing), then the adjacent playable
  // cue; finally a bounded walk past missing-media cues so one dead drive
  // doesn't stop the show. Shared by end-of-cue auto-advance and manual SKIP.
  int resolveAutoAdvanceIndex(Deck& deck, const Cue& activeCue, bool shouldAdvance) {
    int nextIndex = -1;
    if (!trim(activeCue.gotoTarget).empty()) {
      if (auto resolved = cueIndexByTokenInOverlayRole(deck, activeCue.gotoTarget, false); resolved) {
        if (!cueIsOverlayOnly(deck.cues[*resolved])) {
          nextIndex = *resolved;
        }
      }
    }
    if (nextIndex < 0) {
      auto playableIndices = cueIndicesForOverlayRole(deck, false);
      if (deck.shuffle && shouldAdvance && !playableIndices.empty()) {
        std::vector<int> shuffleChoices;
        shuffleChoices.reserve(playableIndices.size());
        for (int cueIndex : playableIndices) {
          if (cueIndex != deck.activeIndex) {
            shuffleChoices.push_back(cueIndex);
          }
        }
        if (!shuffleChoices.empty()) {
          std::uniform_int_distribution<std::size_t> pick(0, shuffleChoices.size() - 1);
          nextIndex = shuffleChoices[pick(shuffleRng_)];
        } else if (deck.playlistLoop && playableIndices.size() == 1) {
          nextIndex = playableIndices.front();
        }
      } else {
        nextIndex = adjacentCueIndexForOverlayRole(deck, deck.activeIndex, 1, false, deck.playlistLoop);
      }
    }
    if (nextIndex >= 0 && shouldAdvance) {
      int hops = 0;
      const int hopLimit = static_cast<int>(deck.cues.size());
      while (nextIndex >= 0 && hops < hopLimit &&
             !cueMediaAvailableForTake(deck.cues[nextIndex])) {
        triggerToast("skipped missing: " + deck.cues[nextIndex].name);
        int following = adjacentCueIndexForOverlayRole(deck, nextIndex, 1, false, deck.playlistLoop);
        nextIndex = (following == nextIndex) ? -1 : following;
        ++hops;
      }
    }
    return nextIndex;
  }

  // Manual SKIP: jump the focused deck to whatever it would naturally play
  // next (honors goto targets, shuffle, playlist loop, and the missing-media
  // walk) without waiting for the current cue to end. "." key / SKIP button /
  // remote SKIP.
  void skipToNextCue() {
    int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    if (deck.cues.empty()) {
      triggerToast("skip: playlist is empty");
      return;
    }
    int nextIndex = -1;
    bool useTransition = false;
    if (deck.activeIndex >= 0 && deck.activeIndex < static_cast<int>(deck.cues.size())) {
      const Cue& activeCue = deck.cues[deck.activeIndex];
      nextIndex = resolveAutoAdvanceIndex(deck, activeCue, true);
      useTransition = activeCue.transitionToNext;
    } else {
      // Nothing live: skip just takes whatever is queued next.
      nextIndex = nextCueIndexForDeck(deckIndex);
    }
    if (nextIndex < 0 || nextIndex >= static_cast<int>(deck.cues.size())) {
      triggerToast("skip: nothing queued");
      return;
    }
    if (deck.selectedIndex != nextIndex) {
      deck.selectedIndex = nextIndex;
      onSelectionChanged();
    }
    markProjectDirty();
    takeSelected(true, useTransition, false);
    triggerToast("skip: " + deck.cues[nextIndex].name);
  }

  // Manual SKIP BACK: take the previous playable cue (playlist-loop aware,
  // walks past missing media). Deliberately ignores goto/shuffle — "back"
  // means the cue physically above this one. "," key / <| button / remote
  // SKIPBACK.
  void skipToPrevCue() {
    int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    if (deck.cues.empty()) {
      triggerToast("skip back: playlist is empty");
      return;
    }
    int fromIndex = deck.activeIndex >= 0 ? deck.activeIndex : deck.selectedIndex;
    int prevIndex = adjacentCueIndexForOverlayRole(deck, fromIndex, -1, false, deck.playlistLoop);
    int hops = 0;
    const int hopLimit = static_cast<int>(deck.cues.size());
    while (prevIndex >= 0 && hops < hopLimit &&
           !cueMediaAvailableForTake(deck.cues[prevIndex])) {
      triggerToast("skipped missing: " + deck.cues[prevIndex].name);
      int preceding = adjacentCueIndexForOverlayRole(deck, prevIndex, -1, false, deck.playlistLoop);
      prevIndex = (preceding == prevIndex) ? -1 : preceding;
      ++hops;
    }
    if (prevIndex < 0 || prevIndex >= static_cast<int>(deck.cues.size())) {
      triggerToast("skip back: nothing previous");
      return;
    }
    bool useTransition = false;
    if (deck.activeIndex >= 0 && deck.activeIndex < static_cast<int>(deck.cues.size())) {
      useTransition = deck.cues[deck.activeIndex].transitionToNext;
    }
    if (deck.selectedIndex != prevIndex) {
      deck.selectedIndex = prevIndex;
      onSelectionChanged();
    }
    markProjectDirty();
    takeSelected(true, useTransition, false);
    triggerToast("skip back: " + deck.cues[prevIndex].name);
  }

  std::string transportStatusLabel(int deckIndex) const {
    const Cue* activeCue = activeCuePtr(deckIndex);
    const DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (activeCue && activeCue->kind == CueKind::Browser) {
      return runtime && runtime->browserCueLive ? "Live Browser" : "Browser Ready";
    }
    if (activeCue && isSourceCueKind(activeCue->kind)) {
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      return (engine && engine->isSourceCapturing()) ? "Live Source" : "Source Ready";
    }
    const MediaEngine* engine = mediaEngineForDeck(deckIndex);
    return engine ? transportLabel(engine->state()) : transportLabel(TransportState::Stopped);
  }

  std::string browserCueStatusLabel(int deckIndex) const {
    const DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime) {
      return "offline";
    }
    BrowserStartPhase phase = BrowserStartPhase::None;
    std::string lastError;
    bool live = runtime->browserCueLive;
    if (runtime->browserRenderer) {
      phase = runtime->browserRenderer->phase();
      lastError = runtime->browserRenderer->lastError();
      live = live || runtime->browserRenderer->isLive();
    }
    return browserCueStatusSummary(phase, live, lastError);
  }

  std::string transportStatusLabel() const {
    return transportStatusLabel(project_.focusedDeckIndex);
  }

  void playTransport() {
    MediaEngine* engine = focusedMediaEngine();
    DeckRuntime* runtime = focusedRuntime();
    const Cue* activeCue = activeCuePtr();
    if (!activeCue && selectedCuePtr()) {
      takeSelected(true);
      return;
    }
    if (!activeCue || !engine || !runtime) {
      return;
    }
    if (activeCue->kind == CueKind::Browser) {
      // Don't restart if already loading/running — let initialization finish.
      if (runtime->browserRenderer && runtime->browserRenderer->isRunning()) {
        return;
      }
      if (startBrowserCue(project_.focusedDeckIndex, *activeCue)) {
        playUiSound(UiSoundEffect::Toggle);
      }
      return;
    }
    engine->play();
    triggerToast("rolling");
    playUiSound(UiSoundEffect::Toggle);
  }

  void pauseTransport() {
    MediaEngine* engine = focusedMediaEngine();
    DeckRuntime* runtime = focusedRuntime();
    const Cue* activeCue = activeCuePtr();
    if (activeCue && runtime && activeCue->kind == CueKind::Browser) {
      stopBrowserCue();
      triggerToast("browser parked");
      playUiSound(UiSoundEffect::Toggle);
      return;
    }
    if (!engine) {
      return;
    }
    engine->pause();
    triggerToast("paused");
    playUiSound(UiSoundEffect::Toggle);
  }

  void stopTransport() {
    showLog("STOP", showLogCueRef(project_.focusedDeckIndex, focusedDeck().activeIndex));
    MediaEngine* engine = focusedMediaEngine();
    DeckRuntime* runtime = focusedRuntime();
    const Cue* activeCue = activeCuePtr();
    if (!engine) {
      return;
    }
    if (activeCue && runtime && activeCue->kind == CueKind::Browser) {
      stopBrowserCue();
      engine->stop(true);
      triggerToast("browser stopped - deck dark");
      playUiSound(UiSoundEffect::Stop);
      return;
    }
    // STOP darkens the deck and reracks; RERACK holds the first frame;
    // PAUSE freezes in place. Three distinct verbs (STOP used to be a
    // duplicate of RERACK).
    engine->stop(true);
    triggerToast("stopped - deck dark, reracked");
    playUiSound(UiSoundEffect::Stop);
  }

  void toggleTransport() {
    MediaEngine* engine = focusedMediaEngine();
    DeckRuntime* runtime = focusedRuntime();
    const Cue* activeCue = activeCuePtr();
    if (!activeCue && selectedCuePtr()) {
      takeSelected(true);
      return;
    }
    if (!activeCue || !engine || !runtime) {
      return;
    }
    if (activeCue->kind == CueKind::Browser) {
      if (runtime->browserCueLive) {
        pauseTransport();
      } else if (!runtime->browserRenderer || !runtime->browserRenderer->isRunning()) {
        // Only (re)start if not already loading — don't interrupt initialization.
        playTransport();
      }
      return;
    }
    engine->toggle();
    triggerToast(engine->state() == TransportState::Playing ? "playing" : "paused");
    playUiSound(UiSoundEffect::Toggle);
  }

  void clearOutput() {
    // Fade to black via dimmer, then clear after fade completes
    focusedDeckMutable().overlayActiveIndices.clear();
    syncPipOverlayRuntimesForDeck(project_.focusedDeckIndex, SDL_GetTicks());
    masterDimmerTarget_ = 0.0;
    pendingClearAfterFade_ = true;
    triggerToast("fading out");
    playUiSound(UiSoundEffect::Clear);
  }

  void finishClearOutput() {
    MediaEngine* engine = focusedMediaEngine();
    stopBrowserCue();
    focusedDeckMutable().overlayActiveIndices.clear();
    syncPipOverlayRuntimesForDeck(project_.focusedDeckIndex, SDL_GetTicks());
    focusedDeckMutable().activeIndex = -1;
    if (engine) {
      engine->clear();
    }
    // Restore dimmer for next cue
    masterDimmerTarget_ = 1.0;
    project_.masterDimmer = 1.0;
    pendingClearAfterFade_ = false;
    notifyTallyStateChange();
    markProjectDirty();
  }

  void rerackTransport() {
    showLog("RERACK", showLogCueRef(project_.focusedDeckIndex, focusedDeck().activeIndex));
    MediaEngine* engine = focusedMediaEngine();
    if (!engine) return;
    engine->seek(0.0);
    engine->pause();
    triggerToast("reracked - holding first frame");
    playUiSound(UiSoundEffect::Stop);
  }

  bool activateOverlayCueIndex(Deck& deck, int cueIndex) {
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return false;
    }
    const Cue& overlayCue = deck.cues[cueIndex];
    if (!cueIsOverlayOnly(overlayCue)) {
      return false;
    }
    auto& liveOverlays = deck.overlayActiveIndices;
    liveOverlays.erase(
      std::remove_if(liveOverlays.begin(), liveOverlays.end(),
                     [&](int liveIndex) {
                       if (liveIndex < 0 || liveIndex >= static_cast<int>(deck.cues.size())) {
                         return true;
                       }
                       return deck.cues[liveIndex].kind == overlayCue.kind || liveIndex == cueIndex;
                     }),
      liveOverlays.end());
    if (liveOverlays.size() >= 4) {
      liveOverlays.erase(liveOverlays.begin());
    }
    liveOverlays.push_back(cueIndex);
    return true;
  }

  void activateAttachedOverlaysForCue(Deck& deck, int deckIndex, const Cue& cue) {
    auto activateAttachedOverlay = [&](CueKind overlayKind, const std::string& token) {
      std::string trimmedToken = trim(token);
      if (trimmedToken.empty()) {
        return;
      }
      auto overlayIndex = cueIndexByTokenInOverlayRole(deck, trimmedToken, true);
      if (!overlayIndex || *overlayIndex < 0 || *overlayIndex >= static_cast<int>(deck.cues.size())) {
        return;
      }
      if (deck.cues[*overlayIndex].kind != overlayKind) {
        return;
      }
      activateOverlayCueIndex(deck, *overlayIndex);
    };

    activateAttachedOverlay(CueKind::LowerThird, cue.attachedLowerThirdCue);
    activateAttachedOverlay(CueKind::Pip, cue.attachedPipCue);
    syncPipOverlayRuntimesForDeck(deckIndex, SDL_GetTicks());
  }

  // Fresh disk check at take/advance time. Updates the cue's missing flag and
  // the toolbar RELINK count in BOTH directions: a re-mounted drive clears the
  // badge on the next take, a vanished one raises it without waiting for a
  // project reload. Non-file cues (patterns, browsers, streams) always pass.
  bool cueMediaAvailableForTake(Cue& cue) {
    if (!cueUsesFilesystemMedia(cue)) {
      return true;
    }
    auto resolved = resolveCueFilesystemPath(cue, currentProjectFile_);
    if (!resolved || resolved->empty()) {
      return true;  // nothing checkable (URI etc.) — let the engine try
    }
    std::error_code ec;
    bool available = fs::exists(*resolved, ec);
    if (cue.mediaMissing == available) {
      cue.mediaMissing = !available;
      missingMediaCount_ = std::max(0, missingMediaCount_ + (available ? -1 : 1));
    }
    return available;
  }

  // Scroll the focused deck's playlist so a cue is centred (or, when
  // onlyIfOffscreen, only if it isn't already fully visible). Coordinates
  // mirror the render loop: each primary row is (kRowHeight + 8) tall and the
  // visible height is the list clip rect inset by 8 on each side.
  void scrollDeckToCueIndex(int deckIndex, int cueIndex, bool onlyIfOffscreen) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return;
    if (deckIndex >= static_cast<int>(deckScrolls_.size())) return;
    const Deck& deck = project_.decks[deckIndex];
    auto primary = cueIndicesForOverlayRole(deck, false);
    int pos = -1;
    for (int i = 0; i < static_cast<int>(primary.size()); ++i) {
      if (primary[i] == cueIndex) { pos = i; break; }
    }
    if (pos < 0) return;
    int clipH = (deckIndex < static_cast<int>(deckListClipRects_.size()))
      ? std::max(0, deckListClipRects_[deckIndex].h - 16) : 0;
    if (clipH <= 0) return;  // list not laid out yet
    int cueY = pos * (kRowHeight + 8);
    int cur = deckScrolls_[deckIndex];
    if (onlyIfOffscreen) {
      int top = cueY - cur;
      if (top >= 0 && top + kRowHeight <= clipH) return;  // already fully visible
    }
    int scrollMax = (deckIndex < static_cast<int>(deckScrollMax_.size())) ? deckScrollMax_[deckIndex] : 0;
    deckScrolls_[deckIndex] = std::clamp(cueY - (clipH - kRowHeight) / 2, 0, scrollMax);
  }

  // Snap the playlist to the live cue (or the selection if nothing is live) and
  // select it — the "where's the show right now?" action for long playlists.
  void jumpToCurrentCue() {
    int deckIndex = std::clamp(project_.focusedDeckIndex, 0,
                               std::max(0, static_cast<int>(project_.decks.size()) - 1));
    Deck& deck = focusedDeckMutable();
    int target = deck.activeIndex >= 0 ? deck.activeIndex : deck.selectedIndex;
    if (target < 0 || target >= static_cast<int>(deck.cues.size())) {
      triggerToast("no cue to jump to");
      return;
    }
    bool wasLive = deck.activeIndex >= 0;
    if (deck.selectedIndex != target) {
      deck.selectedIndex = target;
      onSelectionChanged();
    }
    scrollDeckToCueIndex(deckIndex, target, false);
    triggerToast(wasLive ? "jumped to live cue" : "jumped to selected cue");
  }

  // Auto-follow: when the focused deck's live cue changes (take, auto-advance,
  // shuffle), reveal it — but only if it scrolled out of view, so the list
  // never yanks while the operator is looking right at it.
  void followLiveCueIfChanged() {
    int deckIndex = std::clamp(project_.focusedDeckIndex, 0,
                               std::max(0, static_cast<int>(project_.decks.size()) - 1));
    if (deckIndex >= static_cast<int>(deckFollowedActive_.size())) {
      deckFollowedActive_.resize(deckIndex + 1, -1);
    }
    int active = project_.decks[deckIndex].activeIndex;
    if (active != deckFollowedActive_[deckIndex]) {
      deckFollowedActive_[deckIndex] = active;
      if (active >= 0) {
        scrollDeckToCueIndex(deckIndex, active, /*onlyIfOffscreen=*/true);
      }
    }
  }

  void takeSelected(bool autoplay, bool useTransition = true, bool suppressIncomingFadeIn = false) {
    Deck& deck = focusedDeckMutable();
    int deckIndex = std::clamp(project_.focusedDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    MediaEngine* engine = focusedMediaEngine();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    if (!engine) {
      return;
    }
    // A cue whose file has vanished must not be taken: the decode would fail
    // to black in ~100 ms and (on auto-advance) cascade through the playlist.
    // Flag it, tell the operator, keep whatever is on the output.
    if (!cueMediaAvailableForTake(deck.cues[deck.selectedIndex])) {
      showLog("TAKE-BLOCKED", showLogCueRef(deckIndex, deck.selectedIndex) + " media missing");
      triggerToast("MEDIA MISSING: " + deck.cues[deck.selectedIndex].name + " — take blocked");
      playUiSound(UiSoundEffect::Error);
      return;
    }
    const Cue& cue = deck.cues[deck.selectedIndex];
    // Overlay cues go to the overlay slot, not the main slot.
    if (cue.kind == CueKind::LowerThird || cue.kind == CueKind::Pip) {
      activateOverlayCueIndex(deck, deck.selectedIndex);
      syncPipOverlayRuntimesForDeck(deckIndex, SDL_GetTicks());
      triggerToast(cue.kind == CueKind::Pip ? ("pip live: " + cue.name) : ("overlay live: " + cue.name));
      playUiSound(UiSoundEffect::Take);
      markProjectDirty();
      return;
    }

    bool cueProducesAudio = (cue.kind == CueKind::Video || cue.kind == CueKind::Audio)
                         && cue.hasAudio
                         && cue.audioEnabled;
    if (!cueProducesAudio) {
      clearVuMeterState(false);
    }

    showLog("TAKE", showLogCueRef(deckIndex, deck.selectedIndex));
    deck.activeIndex = deck.selectedIndex;
    // If refreshOnTake is set and a browser renderer is already running for this
    // cue, reload the page instead of tearing down and restarting.
    DeckRuntime* browserRuntime = runtimeForDeck(deckIndex);
    bool browserRefreshInstead = (cue.kind == CueKind::Browser)
      && cue.refreshOnTake
      && browserRuntime && browserRuntime->browserRenderer
      && browserRuntime->browserRenderer->isRunning();
    if (!browserRefreshInstead) stopBrowserCue();
    bool effectiveAutoplay = autoplay && !cue.pauseAtBeginning;
    if (deck.playlistAutoFade && autoplay) {
      deck.playlistOpacity = 0.0f;
      setDeckPlaylistOpacityTarget(deckIndex, 1.0f);
    }
    // Use per-cue transition override if set, else deck default
    double transSecs = 0.0;
    std::string transStyleStr = "cut";
    if (useTransition) {
      transSecs = (cue.cueTransitionSeconds >= 0.0)
        ? cue.cueTransitionSeconds : deck.transitionSeconds;
      transStyleStr = !cue.cueTransitionStyle.empty()
        ? cue.cueTransitionStyle : deck.transitionStyle;
    }
    engine->loadCue(
      &cue,
      effectiveAutoplay,
      transSecs,
      parseTransitionStyleToken(transStyleStr),
      suppressIncomingFadeIn
    );
    // Load subtitle track if the cue has one
    if (cue.subtitleEnabled && (!cue.subtitlePath.empty() || !cue.subtitleStreamId.empty())) {
      std::string subtitleKey = cue.subtitlePath.empty() ? (cue.path + "::" + cue.subtitleStreamId) : cue.subtitlePath;
      if (subtitleCache_.find(subtitleKey) == subtitleCache_.end()) {
        subtitleCache_[subtitleKey] = loadSubtitleTrack(cue);
      }
    }
    if (cue.kind == CueKind::Browser) {
      if (browserRefreshInstead) {
        browserRuntime->browserRenderer->reload();
        triggerToast("browser refreshed");
      } else {
        startBrowserCue(project_.focusedDeckIndex, cue);
        triggerToast("browser jumped live");
      }
    } else if (cue.kind == CueKind::Composite) {
      triggerToast(effectiveAutoplay ? "scene live" : "scene loaded");
    } else if (isSourceCueKind(cue.kind)) {
      bool live = engine->isSourceCapturing();
      if (effectiveAutoplay && live) {
        triggerToast("source live");
      } else if (effectiveAutoplay && !live) {
        triggerToast("source unavailable");
      } else {
        triggerToast("source loaded");
      }
    } else {
      triggerToast(effectiveAutoplay ? "cue jumped live" : "cue loaded");
    }
    activateAttachedOverlaysForCue(deck, deckIndex, cue);
    playUiSound(UiSoundEffect::Take);
    notifyTallyStateChange();
    markProjectDirty();
  }

  void jumpSelectedCue() {
    takeSelected(jumpTriggersPlayback(), project_.jumpTransitionEnabled);
  }

  // Fire the selected cue on every deck simultaneously.
  // Useful for synced multi-layer playback (e.g. video + audio on separate decks).
  void takeAllDecks(bool autoplay) {
    int savedFocus = project_.focusedDeckIndex;
    for (int di = 0; di < static_cast<int>(project_.decks.size()); ++di) {
      Deck& deck = project_.decks[di];
      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      project_.focusedDeckIndex = di;
      takeSelected(autoplay);
    }
    project_.focusedDeckIndex = savedFocus;
    triggerToast("all decks fired");
  }

  // Toggle play/pause on every deck simultaneously.
  void goAllDecks() {
    int savedFocus = project_.focusedDeckIndex;
    for (int di = 0; di < static_cast<int>(project_.decks.size()); ++di) {
      project_.focusedDeckIndex = di;
      MediaEngine* engine = focusedMediaEngine();
      const Cue* activeCue = activeCuePtr();
      if (engine && activeCue) {
        if (engine->state() == TransportState::Playing) {
          engine->pause();
        } else {
          engine->play();
        }
      } else {
        takeSelected(true);
      }
    }
    project_.focusedDeckIndex = savedFocus;
    triggerToast("all decks go");
  }

  void allStop() {
    int saved = project_.focusedDeckIndex;
    for (int di = 0; di < (int)project_.decks.size(); ++di) {
      project_.focusedDeckIndex = di;
      if (auto* e = focusedMediaEngine()) e->stop();
    }
    project_.focusedDeckIndex = saved;
    triggerToast("all decks stopped");
  }

  void selectRelative(int direction, bool reorder) {
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) {
      return;
    }

    bool overlayGroup = false;
    if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
      overlayGroup = cueIsOverlayOnly(deck.cues[deck.selectedIndex]);
    } else if (firstCueIndexForOverlayRole(deck, false) < 0) {
      overlayGroup = true;
    }

    if (deck.selectedIndex < 0) {
      int firstIndex = direction >= 0
        ? firstCueIndexForOverlayRole(deck, overlayGroup)
        : lastCueIndexForOverlayRole(deck, overlayGroup);
      if (firstIndex < 0 && !overlayGroup) {
        firstIndex = direction >= 0
          ? firstCueIndexForOverlayRole(deck, true)
          : lastCueIndexForOverlayRole(deck, true);
      }
      if (firstIndex >= 0) {
        deck.selectedIndex = firstIndex;
        onSelectionChanged();
        markProjectDirty();
      }
      return;
    }

    int nextIndex = adjacentCueIndexForOverlayRole(deck, deck.selectedIndex, direction, overlayGroup, false);
    if (nextIndex < 0) {
      return;
    }
    if (reorder && nextIndex != deck.selectedIndex) {
      std::swap(deck.cues[deck.selectedIndex], deck.cues[nextIndex]);
      if (deck.activeIndex == deck.selectedIndex) {
        deck.activeIndex = nextIndex;
      } else if (deck.activeIndex == nextIndex) {
        deck.activeIndex = deck.selectedIndex;
      }
      triggerToast("cue reordered");
      playUiSound(UiSoundEffect::Toggle);
    }
    if (deck.selectedIndex != nextIndex) {
      deck.selectedIndex = nextIndex;
      deck.selectedIndices.clear();
      deck.selectedIndices.push_back(nextIndex);
      onSelectionChanged();
    }
    markProjectDirty();
  }

  Cue* selectedCueMutable() {
    Deck& deck = focusedDeckMutable();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.selectedIndex];
  }

  void refreshSelectedCuePreviewCaches() {
    selectionChangedAt_ = SDL_GetTicks();
    const Cue* cue = selectedCuePtr();
    if (!cue) {
      clearSelectedThumbnail();
      return;
    }
    if (cue->kind == CueKind::Composite) {
      clearSelectedThumbnail();
      return;
    }
    if (cue->kind == CueKind::Pip) {
      Cue resolvedCue;
      if (buildResolvedPipSourceCue(focusedDeck(), *cue, resolvedCue, nullptr)) {
        requestThumbnail(resolvedCue);
      } else {
        clearSelectedThumbnail();
      }
      return;
    }
    requestThumbnail(*cue);
  }

  void applyCopiedCueSettings(Cue& target, const Cue& source) {
    target.audioEnabled = target.hasAudio ? source.audioEnabled : false;
    target.fadeInSeconds = std::clamp(source.fadeInSeconds, 0.0, 10.0);
    target.fadeOutSeconds = std::clamp(source.fadeOutSeconds, 0.0, 10.0);
    target.loop = source.loop;
    target.pauseAtBeginning = source.pauseAtBeginning;
    target.pauseOnLastFrame = source.pauseOnLastFrame;
    target.transitionToNext = source.transitionToNext;
    target.gotoTarget = source.gotoTarget;
    double cueDuration = target.duration > 0.0 ? target.duration : 3600.0;
    target.inPointSeconds = std::clamp(source.inPointSeconds, 0.0, cueDuration);
    target.outPointSeconds = source.outPointSeconds > 0.0
      ? std::clamp(source.outPointSeconds, target.inPointSeconds, cueDuration)
      : 0.0;
    target.stillDurationSeconds = std::max(0.0, source.stillDurationSeconds);
    target.cueTransitionSeconds = source.cueTransitionSeconds < 0.0
      ? -1.0
      : std::clamp(source.cueTransitionSeconds, 0.0, 10.0);
    target.cueTransitionStyle = source.cueTransitionStyle;
    target.lowerThirdBgAlpha = std::clamp(source.lowerThirdBgAlpha, 0, 255);
    target.attachedLowerThirdCue = source.attachedLowerThirdCue;
    target.attachedPipCue = source.attachedPipCue;
    target.compositeLayoutPreset = source.compositeLayoutPreset;
    target.compositeAudioSlotId = source.compositeAudioSlotId;
    target.compositeBackgroundColor = source.compositeBackgroundColor;
    target.compositeSlots = source.compositeSlots;
    target.loopCount = std::max(0, source.loopCount);
    target.playbackSpeed = std::clamp(source.playbackSpeed, 0.25, 4.0);
    target.colorTag = source.colorTag;
    target.outputScaleX = std::clamp(source.outputScaleX, 0.25f, 4.0f);
    target.outputScaleY = std::clamp(source.outputScaleY, 0.25f, 4.0f);
    target.scaleMode = source.scaleMode;
    target.outputOffsetX = std::clamp(source.outputOffsetX, -4096.0f, 4096.0f);
    target.outputOffsetY = std::clamp(source.outputOffsetY, -4096.0f, 4096.0f);
    target.outputRotationDegrees = std::clamp(source.outputRotationDegrees, -180.0f, 180.0f);
    target.cropLeft = std::clamp(source.cropLeft, 0.0f, 0.95f);
    target.cropRight = std::clamp(source.cropRight, 0.0f, 0.95f);
    target.cropTop = std::clamp(source.cropTop, 0.0f, 0.95f);
    target.cropBottom = std::clamp(source.cropBottom, 0.0f, 0.95f);
    target.chromaKeyEnabled = source.chromaKeyEnabled;
    target.chromaKeyColor = source.chromaKeyColor;
    target.chromaKeyTolerance = std::clamp(source.chromaKeyTolerance, 0.0f, 441.0f);
    target.chromaKeySoftness = std::clamp(source.chromaKeySoftness, 0.0f, 200.0f);
    target.brightness = std::clamp(source.brightness, 0.0f, 2.0f);
    target.contrast = std::clamp(source.contrast, 0.0f, 2.0f);
    target.saturation = std::clamp(source.saturation, 0.0f, 2.0f);
    target.hueShift = std::clamp(source.hueShift, -180.0f, 180.0f);
    target.endAction = source.endAction;
    target.pausePoints = source.pausePoints;
  }

  void copySelectedCueSettings() {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      triggerToast("cue copy: select cue");
      return;
    }
    cueSettingsClipboard_ = *cue;
    triggerToast("cue settings copied");
    playUiSound(UiSoundEffect::Navigate);
  }

  void pasteSelectedCueSettings() {
    if (!cueSettingsClipboard_) {
      triggerToast("cue paste: copy first");
      return;
    }
    Deck& deck = focusedDeckMutable();
    auto indices = selectedCueIndices(deck);
    if (indices.empty()) {
      triggerToast("cue paste: select cue");
      return;
    }
    pushUndoSnapshot();
    const Cue source = *cueSettingsClipboard_;
    int appliedCount = 0;
    for (int index : indices) {
      if (index < 0 || index >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      applyCopiedCueSettings(deck.cues[index], source);
      ++appliedCount;
    }
    if (appliedCount <= 0) {
      triggerToast("cue paste: no targets");
      return;
    }
    refreshFocusedLiveCueRuntimeIfSelected();
    syncPipOverlayRuntimesForDeck(project_.focusedDeckIndex, SDL_GetTicks());
    refreshSelectedCuePreviewCaches();
    triggerToast(appliedCount == 1 ? "cue settings pasted"
                                   : ("cue settings pasted x" + std::to_string(appliedCount)));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  // Reset every selected cue's inspector settings (geometry, color, fades,
  // crop, chroma, etc.) back to deck defaults, leaving media path/name/id and
  // probed metadata intact. Applies to the whole selection like paste.
  void resetSelectedCueSettings() {
    Deck& deck = focusedDeckMutable();
    auto indices = selectedCueIndices(deck);
    if (indices.empty()) {
      triggerToast("cue reset: select cue");
      return;
    }
    pushUndoSnapshot();
    int appliedCount = 0;
    for (int index : indices) {
      if (index < 0 || index >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      Cue& cue = deck.cues[index];
      // Match the target's kind so kind-specific defaults (still duration, fade
      // defaults) reset correctly; hasAudio/duration keep audio + trim sane.
      Cue defaults;
      defaults.kind = cue.kind;
      defaults.hasAudio = cue.hasAudio;
      defaults.duration = cue.duration;
      applyDeckDefaultsToCue(defaults, deck);
      applyCopiedCueSettings(cue, defaults);
      ++appliedCount;
    }
    if (appliedCount <= 0) {
      triggerToast("cue reset: no targets");
      return;
    }
    refreshFocusedLiveCueRuntimeIfSelected();
    syncPipOverlayRuntimesForDeck(project_.focusedDeckIndex, SDL_GetTicks());
    refreshSelectedCuePreviewCaches();
    triggerToast(appliedCount == 1 ? "cue settings reset"
                                   : ("cue settings reset x" + std::to_string(appliedCount)));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void applyCopiedWarpSettings(Deck& target, const Deck& source) {
    target.warpEnabled = source.warpEnabled;
    target.warpMode = normalizeWarpMode(source.warpMode);
    target.warpTopLeftX = source.warpTopLeftX;
    target.warpTopLeftY = source.warpTopLeftY;
    target.warpTopRightX = source.warpTopRightX;
    target.warpTopRightY = source.warpTopRightY;
    target.warpBottomRightX = source.warpBottomRightX;
    target.warpBottomRightY = source.warpBottomRightY;
    target.warpBottomLeftX = source.warpBottomLeftX;
    target.warpBottomLeftY = source.warpBottomLeftY;
    target.edgeBlendLeft = source.edgeBlendLeft;
    target.edgeBlendRight = source.edgeBlendRight;
    target.edgeBlendTop = source.edgeBlendTop;
    target.edgeBlendBottom = source.edgeBlendBottom;
    normalizeDeck(target, project_.focusedDeckIndex);
  }

  void copyFocusedWarpSettings() {
    warpSettingsClipboard_ = focusedDeck();
    triggerToast("warp settings copied");
    playUiSound(UiSoundEffect::Navigate);
  }

  void pasteFocusedWarpSettings() {
    if (!warpSettingsClipboard_) {
      triggerToast("warp paste: copy first");
      return;
    }
    pushUndoSnapshot();
    Deck& deck = focusedDeckMutable();
    applyCopiedWarpSettings(deck, *warpSettingsClipboard_);
    triggerToast("warp settings pasted");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  std::optional<int> cueIndexById(const Deck& deck, const std::string& cueId) const {
    std::string needle = toUpper(trim(cueId));
    if (needle.empty()) {
      return std::nullopt;
    }
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      if (toUpper(deck.cues[index].id) == needle) {
        return index;
      }
    }
    return std::nullopt;
  }

  std::optional<int> cueIndexByTokenInOverlayRole(const Deck& deck, const std::string& token,
                                                  bool overlayGroup) const {
    std::string trimmed = trim(token);
    if (trimmed.empty()) {
      return std::nullopt;
    }

    std::string upperToken = toUpper(trimmed);
    if (upperToken == "FIRST") {
      int firstIndex = firstCueIndexForOverlayRole(deck, overlayGroup);
      if (firstIndex < 0 && !overlayGroup) {
        firstIndex = firstCueIndexForOverlayRole(deck, true);
      }
      return firstIndex >= 0 ? std::optional<int> {firstIndex} : std::nullopt;
    }
    if (upperToken == "LAST") {
      int lastIndex = lastCueIndexForOverlayRole(deck, overlayGroup);
      if (lastIndex < 0 && !overlayGroup) {
        lastIndex = lastCueIndexForOverlayRole(deck, true);
      }
      return lastIndex >= 0 ? std::optional<int> {lastIndex} : std::nullopt;
    }
    if (upperToken == "NEXT") {
      int nextIndex = adjacentCueIndexForOverlayRole(deck, deck.selectedIndex, 1, overlayGroup, false);
      if (nextIndex < 0 && !overlayGroup) {
        nextIndex = firstCueIndexForOverlayRole(deck, false);
      }
      return nextIndex >= 0 ? std::optional<int> {nextIndex} : std::nullopt;
    }
    if (upperToken == "PREV" || upperToken == "PREVIOUS") {
      int prevIndex = adjacentCueIndexForOverlayRole(deck, deck.selectedIndex, -1, overlayGroup, false);
      if (prevIndex < 0 && !overlayGroup) {
        prevIndex = firstCueIndexForOverlayRole(deck, false);
      }
      return prevIndex >= 0 ? std::optional<int> {prevIndex} : std::nullopt;
    }
    if (upperToken == "SEL" || upperToken == "SELECTED") {
      if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
        return deck.selectedIndex;
      }
      return std::nullopt;
    }
    if (upperToken == "ACT" || upperToken == "ACTIVE") {
      if (deck.activeIndex >= 0 && deck.activeIndex < static_cast<int>(deck.cues.size())) {
        return deck.activeIndex;
      }
      return std::nullopt;
    }
    if ((trimmed[0] == '+' || trimmed[0] == '-') && deck.selectedIndex >= 0) {
      try {
        int delta = std::stoi(trimmed);
        return std::clamp(deck.selectedIndex + delta, 0, static_cast<int>(deck.cues.size()) - 1);
      } catch (...) {
      }
    }

    try {
      int index = std::stoi(trimmed);
      if (index >= 1 && index <= static_cast<int>(deck.cues.size())) {
        return index - 1;
      }
    } catch (...) {
    }

    if (auto byId = cueIndexById(deck, trimmed); byId) {
      return byId;
    }

    // Exact match on operator-facing short cue id / cue number
    std::string needle = toUpper(trimmed);
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      if (!deck.cues[index].cueId.empty() && toUpper(deck.cues[index].cueId) == needle) {
        return index;
      }
      if (!deck.cues[index].cueNumber.empty() && toUpper(deck.cues[index].cueNumber) == needle) {
        return index;
      }
    }

    // Prefix match on short cue id / cue number (operator-friendly shorthand)
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      if (!deck.cues[index].cueId.empty()) {
        std::string cueIdShort = toUpper(deck.cues[index].cueId);
        if (cueIdShort.rfind(needle, 0) == 0) {
          return index;
        }
      }
      if (!deck.cues[index].cueNumber.empty()) {
        std::string cueNum = toUpper(deck.cues[index].cueNumber);
        if (cueNum.rfind(needle, 0) == 0) {
          return index;
        }
      }
    }

    // Partial match on name
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      if (toUpper(deck.cues[index].name).find(needle) != std::string::npos) {
        return index;
      }
    }
    return std::nullopt;
  }

  std::optional<int> cueIndexByToken(const Deck& deck, const std::string& token) const {
    bool overlayGroup = deck.selectedIndex >= 0 &&
                        deck.selectedIndex < static_cast<int>(deck.cues.size()) &&
                        cueIsOverlayOnly(deck.cues[deck.selectedIndex]);
    return cueIndexByTokenInOverlayRole(deck, token, overlayGroup);
  }

  std::vector<int> cueFindMatches(const Deck& deck, const std::string& token) const {
    std::string needle = toUpper(trim(token));
    if (needle.empty()) {
      return {};
    }

    std::vector<int> matches;
    std::unordered_set<int> seen;
    auto pushMatch = [&](int cueIndex) {
      if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
        return;
      }
      if (seen.insert(cueIndex).second) {
        matches.push_back(cueIndex);
      }
    };

    auto isExact = [&](const Cue& cue) {
      return toUpper(cue.id) == needle
        || toUpper(cue.cueId) == needle
        || toUpper(cue.cueNumber) == needle
        || toUpper(cue.name) == needle;
    };
    auto isPrefix = [&](const Cue& cue) {
      std::string cueId = toUpper(cue.id);
      std::string cueIdShort = toUpper(cue.cueId);
      std::string cueNum = toUpper(cue.cueNumber);
      std::string cueName = toUpper(cue.name);
      return cueId.rfind(needle, 0) == 0
        || cueIdShort.rfind(needle, 0) == 0
        || cueNum.rfind(needle, 0) == 0
        || cueName.rfind(needle, 0) == 0;
    };
    auto isContains = [&](const Cue& cue) {
      std::string cueId = toUpper(cue.id);
      std::string cueIdShort = toUpper(cue.cueId);
      std::string cueNum = toUpper(cue.cueNumber);
      std::string cueName = toUpper(cue.name);
      return cueId.find(needle) != std::string::npos ||
        cueIdShort.find(needle) != std::string::npos ||
        cueNum.find(needle) != std::string::npos ||
        cueName.find(needle) != std::string::npos;
    };

    if (auto direct = cueIndexByToken(deck, token); direct) {
      pushMatch(*direct);
    }
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      if (isExact(deck.cues[cueIndex])) {
        pushMatch(cueIndex);
      }
    }
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      if (isPrefix(deck.cues[cueIndex])) {
        pushMatch(cueIndex);
      }
    }
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      if (isContains(deck.cues[cueIndex])) {
        pushMatch(cueIndex);
      }
    }

    return matches;
  }

  const Cue* resolvePipTargetCue(const Deck& deck, const Cue& pipCue, int* targetCueIndexOut = nullptr) const {
    if (pipCue.kind != CueKind::Pip) {
      return nullptr;
    }
    std::string token = pipCueTargetDisplayToken(pipCue);
    if (token.empty()) {
      return nullptr;
    }
    auto targetIndex = cueIndexByToken(deck, token);
    if (!targetIndex || *targetIndex < 0 || *targetIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    const Cue& targetCue = deck.cues[*targetIndex];
    if (&targetCue == &pipCue || !cueCanBePipSource(targetCue)) {
      return nullptr;
    }
    if (targetCueIndexOut) {
      *targetCueIndexOut = *targetIndex;
    }
    return &targetCue;
  }

  std::string pipTargetSummaryLabel(const Deck& deck, const Cue& pipCue) const {
    std::string token = pipCueTargetDisplayToken(pipCue);
    if (token.empty()) {
      return "(choose cue)";
    }
    int targetCueIndex = -1;
    if (const Cue* targetCue = resolvePipTargetCue(deck, pipCue, &targetCueIndex)) {
      return cueDisplayToken(*targetCue, targetCueIndex) + "  " + targetCue->name;
    }
    return token + "  (missing)";
  }

  std::string pipSourceDisplayLabel(const Cue& pipCue) const {
    std::string sourceType = pipSourceTypeTokenFromCue(pipCue);
    if (sourceType == "legacy") {
      return std::string("legacy  ") + pipCueTargetDisplayToken(pipCue);
    }
    if (sourceType == "browser") {
      return pipCue.path.empty() ? std::string("(set url)") : pipCue.path;
    }
    if (pipSourceTypeUsesSourceRef(sourceType)) {
      Cue tempCue;
      tempCue.kind = sourceCueKindFromToken(sourceType);
      tempCue.path = pipCue.path;
      std::string sourceRef = sourceCueRefFromCue(tempCue);
      if (sourceRef.empty()) {
        sourceRef = defaultSourceRefForKind(tempCue.kind);
      }
      return sourceCueRefFriendlyLabel(tempCue.kind, sourceRef);
    }
    if (pipCue.path.empty() || pipCue.path == "graphic://pip") {
      return "(set media file)";
    }
    return pipCue.path;
  }

  bool buildResolvedPipSourceCue(const Deck& deck, const Cue& pipCue, Cue& resolvedCue,
                                 int* legacyTargetCueIndexOut = nullptr) const {
    if (legacyTargetCueIndexOut) {
      *legacyTargetCueIndexOut = -1;
    }
    std::string sourceType = pipSourceTypeTokenFromCue(pipCue);
    if (sourceType == "legacy") {
      int targetCueIndex = -1;
      if (const Cue* targetCue = resolvePipTargetCue(deck, pipCue, &targetCueIndex)) {
        resolvedCue = *targetCue;
        if (legacyTargetCueIndexOut) {
          *legacyTargetCueIndexOut = targetCueIndex;
        }
        return true;
      }
      return false;
    }

    resolvedCue = pipCue;
    resolvedCue.id = pipCue.id + "|pipsrc";
    resolvedCue.cueId.clear();
    resolvedCue.cueNumber.clear();
    resolvedCue.colorTag.clear();
    resolvedCue.notes.clear();
    resolvedCue.gotoTarget.clear();
    resolvedCue.pausePoints.clear();
    resolvedCue.lowerThirdText.clear();
    resolvedCue.lowerThirdSubtext.clear();
    resolvedCue.lowerThirdBgAlpha = 180;
    resolvedCue.pipTargetCue.clear();
    resolvedCue.attachedLowerThirdCue.clear();
    resolvedCue.attachedPipCue.clear();
    resolvedCue.fadeInSeconds = 0.0;
    resolvedCue.fadeOutSeconds = 0.0;
    resolvedCue.transitionToNext = false;
    resolvedCue.pauseAtBeginning = false;
    resolvedCue.pauseOnLastFrame = true;
    resolvedCue.loop = false;
    resolvedCue.loopCount = 0;
    resolvedCue.audioEnabled = false;

    int outputIndex = std::clamp(project_.focusedOutputIndex, 0,
                                 std::max(0, static_cast<int>(project_.outputs.size()) - 1));
    auto [defaultW, defaultH] = outputRenderSizeForOutput(outputIndex);

    if (sourceType == "browser") {
      std::string url = normalizeBrowserUrl(pipCue.path);
      if (url.empty()) {
        return false;
      }
      resolvedCue.kind = CueKind::Browser;
      resolvedCue.path = url;
      resolvedCue.width = std::max(1, pipCue.width > 0 ? pipCue.width : defaultW);
      resolvedCue.height = std::max(1, pipCue.height > 0 ? pipCue.height : defaultH);
      resolvedCue.duration = 0.0;
      resolvedCue.stillDurationSeconds = 0.0;
      resolvedCue.fps = 30.0;
      resolvedCue.formatName = "browser";
      resolvedCue.videoCodec = "browser";
      resolvedCue.audioCodec.clear();
      resolvedCue.hasAudio = false;
      resolvedCue.audioChannels = 0;
      resolvedCue.audioSampleRate = 0;
      return true;
    }

    if (pipSourceTypeUsesSourceRef(sourceType)) {
      CueKind sourceKind = sourceCueKindFromToken(sourceType);
      std::string sourceRef = trim(pipCue.path);
      if (sourceRef.rfind("source://", 0) == 0) {
        Cue tempCue;
        tempCue.kind = sourceKind;
        tempCue.path = sourceRef;
        sourceRef = sourceCueRefFromCue(tempCue);
      }
      sourceRef = sourceCueRefFromAlias(sourceKind, sourceRef);
      resolvedCue.kind = sourceKind;
      resolvedCue.path = "source://" + sourceCueTokenForKind(sourceKind) + "/" + sourceRef;
      resolvedCue.width = std::max(1, pipCue.width > 0 ? pipCue.width : defaultW);
      resolvedCue.height = std::max(1, pipCue.height > 0 ? pipCue.height : defaultH);
      resolvedCue.duration = 0.0;
      resolvedCue.stillDurationSeconds = 0.0;
      resolvedCue.fps = 30.0;
      resolvedCue.formatName = "source";
      resolvedCue.videoCodec = sourceCueTokenForKind(sourceKind);
      resolvedCue.hasAudio = sourceKind == CueKind::Camera;
      resolvedCue.audioCodec = resolvedCue.hasAudio ? "source" : "";
      resolvedCue.audioChannels = resolvedCue.hasAudio ? std::max(2, pipCue.audioChannels) : 0;
      resolvedCue.audioSampleRate = resolvedCue.hasAudio ? std::max(48000, pipCue.audioSampleRate) : 0;
      return true;
    }

    std::string mediaPath = resolvedCueFilesystemPathString(pipCue, currentProjectFile_);
    if (trim(mediaPath).empty() || mediaPath == "graphic://pip") {
      return false;
    }
    resolvedCue.path = mediaPath;
    resolvedCue.kind = isImagePath(fs::path(mediaPath)) ? CueKind::Image : CueKind::Video;
    resolvedCue.width = std::max(1, pipCue.width > 0 ? pipCue.width : defaultW);
    resolvedCue.height = std::max(1, pipCue.height > 0 ? pipCue.height : defaultH);
    return true;
  }

  bool applyPipSourceToCue(Cue& cue, const std::string& rawType, const std::string& rawValue,
                           std::string* errorOut = nullptr) {
    auto setError = [&](const std::string& message) {
      if (errorOut) {
        *errorOut = message;
      }
      return false;
    };
    auto refreshAutoName = [&](const std::string& sourceType) {
      if (cue.name.empty() || cue.name == "PIP" || cue.name.rfind("PIP · ", 0) == 0) {
        cue.name = "PIP · " + pipSourceTypeLabel(sourceType);
      }
    };
    std::string sourceType = toLower(trim(rawType));
    if (sourceType.empty()) {
      sourceType = "media";
    }

    if (sourceType == "browser") {
      std::string url = normalizeBrowserUrl(rawValue);
      if (url.empty()) {
        return setError("pip url required");
      }
      auto [defaultW, defaultH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
      cue.pipSourceType = sourceType;
      cue.path = url;
      cue.duration = 0.0;
      cue.stillDurationSeconds = 0.0;
      cue.width = std::max(1, cue.width > 0 ? cue.width : defaultW);
      cue.height = std::max(1, cue.height > 0 ? cue.height : defaultH);
      cue.fps = 30.0;
      cue.formatName = "browser";
      cue.videoCodec = "browser";
      cue.audioCodec.clear();
      cue.hasAudio = false;
      cue.audioEnabled = false;
      cue.audioChannels = 0;
      cue.audioSampleRate = 0;
      cue.sizeBytes = 0;
      cue.pipTargetCue.clear();
      refreshAutoName(sourceType);
      return true;
    }

    if (pipSourceTypeUsesSourceRef(sourceType)) {
      CueKind sourceKind = sourceCueKindFromToken(sourceType);
      std::string sourceRef = sourceCueRefFromAlias(sourceKind, rawValue);
      auto [defaultW, defaultH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
      cue.pipSourceType = sourceType;
      cue.path = "source://" + sourceCueTokenForKind(sourceKind) + "/" + sourceRef;
      cue.duration = 0.0;
      cue.stillDurationSeconds = 0.0;
      cue.width = std::max(1, cue.width > 0 ? cue.width : defaultW);
      cue.height = std::max(1, cue.height > 0 ? cue.height : defaultH);
      cue.fps = 30.0;
      cue.formatName = "source";
      cue.videoCodec = sourceCueTokenForKind(sourceKind);
      cue.hasAudio = sourceKind == CueKind::Camera;
      cue.audioEnabled = false;
      cue.audioCodec = cue.hasAudio ? "source" : "";
      cue.audioChannels = cue.hasAudio ? 2 : 0;
      cue.audioSampleRate = cue.hasAudio ? 48000 : 0;
      cue.sizeBytes = 0;
      cue.pipTargetCue.clear();
      refreshAutoName(sourceType);
      return true;
    }

    std::string mediaValue = trim(rawValue);
    if (mediaValue.empty() || mediaValue == "graphic://pip") {
      cue.pipSourceType = "media";
      cue.path = "graphic://pip";
      cue.duration = 0.0;
      cue.sizeBytes = 0;
      cue.width = 0;
      cue.height = 0;
      cue.fps = 0.0;
      cue.formatName = "overlay";
      cue.videoCodec.clear();
      cue.audioCodec.clear();
      cue.hasAudio = false;
      cue.audioEnabled = false;
      cue.audioChannels = 0;
      cue.audioSampleRate = 0;
      cue.pipTargetCue.clear();
      refreshAutoName("media");
      return true;
    }

    std::error_code ec;
    fs::path inputPath(mediaValue);
    if (!inputPath.is_absolute() && !fs::exists(inputPath, ec)) {
      fs::path base = currentProjectFile_.has_parent_path() ? currentProjectFile_.parent_path() : fs::path(".");
      inputPath = base / inputPath;
    }
    inputPath = fs::absolute(inputPath, ec);
    auto probed = probeCue(inputPath);
    if (!probed) {
      return setError("pip media unavailable");
    }
    if (probed->kind != CueKind::Video && probed->kind != CueKind::Image) {
      return setError("pip needs video or still");
    }
    cue.pipSourceType = "media";
    cue.path = inputPath.string();
    cue.duration = probed->duration;
    cue.width = probed->width;
    cue.height = probed->height;
    cue.fps = probed->fps;
    cue.formatName = probed->formatName;
    cue.videoCodec = probed->videoCodec;
    cue.audioCodec = probed->audioCodec;
    cue.hasAudio = probed->hasAudio;
    cue.audioEnabled = false;
    cue.sizeBytes = probed->sizeBytes;
    cue.audioChannels = probed->audioChannels;
    cue.audioSampleRate = probed->audioSampleRate;
    cue.pipTargetCue.clear();
    refreshAutoName("media");
    return true;
  }

  void setSelectedPipSourceType(const std::string& rawType) {
    Cue* selected = selectedCueMutable();
    if (!selected || selected->kind != CueKind::Pip) {
      return;
    }
    std::string sourceType = toLower(trim(rawType));
    if (sourceType.empty()) {
      sourceType = "media";
    }
    std::string currentValue = selected->path;
    if (pipSourceTypeUsesSourceRef(sourceType)) {
      if (currentValue.rfind("source://", 0) == 0) {
        Cue tempCue;
        tempCue.kind = sourceCueKindFromToken(sourceType);
        tempCue.path = currentValue;
        currentValue = sourceCueRefFromCue(tempCue);
      } else {
        currentValue = defaultSourceRefForKind(sourceCueKindFromToken(sourceType));
      }
    } else if (sourceType == "browser") {
      currentValue = currentValue == "graphic://pip" ? "https://example.com" : currentValue;
    } else if (currentValue == "graphic://pip") {
      currentValue.clear();
    }

    std::string error;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Pip) {
        return;
      }
      if (applyPipSourceToCue(cue, sourceType, currentValue, &error)) {
        changed = true;
      }
    });
    if (!changed) {
      if (!error.empty()) {
        triggerToast(error);
      }
      return;
    }
    syncPipOverlayRuntimesForDeck(project_.focusedDeckIndex, SDL_GetTicks());
    onSelectionChanged();
    triggerToast("pip source type: " + pipSourceTypeLabel(sourceType));
    markProjectDirty();
  }

  void setSelectedPipSourceValue(const std::string& rawValue) {
    Cue* selected = selectedCueMutable();
    if (!selected || selected->kind != CueKind::Pip) {
      return;
    }
    std::string sourceType = pipSourceTypeTokenFromCue(*selected);
    std::string error;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Pip) {
        return;
      }
      if (applyPipSourceToCue(cue, sourceType, rawValue, &error)) {
        changed = true;
      }
    });
    if (!changed) {
      if (!error.empty()) {
        triggerToast(error);
      }
      return;
    }
    syncPipOverlayRuntimesForDeck(project_.focusedDeckIndex, SDL_GetTicks());
    onSelectionChanged();
    triggerToast("pip source set");
    markProjectDirty();
  }

  std::string attachedOverlaySummaryLabel(const Deck& deck, const Cue& cue, CueKind overlayKind) const {
    std::string token = trim(overlayKind == CueKind::LowerThird ? cue.attachedLowerThirdCue : cue.attachedPipCue);
    if (token.empty()) {
      return "(none)";
    }
    if (auto cueIndex = cueIndexByTokenInOverlayRole(deck, token, true); cueIndex) {
      const Cue& targetCue = deck.cues[*cueIndex];
      if (targetCue.kind == overlayKind) {
        return cueDisplayToken(targetCue, *cueIndex) + "  " + targetCue.name;
      }
    }
    return token + "  (missing)";
  }

  void setSelectedAttachedOverlayCue(CueKind overlayKind, const std::string& rawToken) {
    Cue* selected = selectedCueMutable();
    if (!selected || cueIsOverlayOnly(*selected)) {
      return;
    }
    std::string token = trim(rawToken);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cueIsOverlayOnly(cue)) {
        return;
      }
      if (overlayKind == CueKind::LowerThird) {
        cue.attachedLowerThirdCue = token;
      } else if (overlayKind == CueKind::Pip) {
        cue.attachedPipCue = token;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast(token.empty()
      ? (overlayKind == CueKind::LowerThird ? "lower third cleared" : "pip cleared")
      : (overlayKind == CueKind::LowerThird ? "lower third attached" : "pip attached"));
    markProjectDirty();
  }

  void anchorPipCueToCorner(Cue& cue, int horizontalDir, int verticalDir) {
    int outputIndex = std::clamp(project_.focusedOutputIndex, 0,
                                 std::max(0, static_cast<int>(project_.outputs.size()) - 1));
    auto [outW, outH] = outputRenderSizeForOutput(outputIndex);
    float marginX = std::max(32.0f, static_cast<float>(outW) * 0.04f);
    float marginY = std::max(28.0f, static_cast<float>(outH) * 0.05f);
    float freeX = std::max(0.0f, static_cast<float>(outW) * std::max(0.0f, 1.0f - cue.outputScaleX) * 0.5f - marginX);
    float freeY = std::max(0.0f, static_cast<float>(outH) * std::max(0.0f, 1.0f - cue.outputScaleY) * 0.5f - marginY);
    cue.outputOffsetX = static_cast<float>(horizontalDir) * freeX * 0.72f;
    cue.outputOffsetY = static_cast<float>(verticalDir) * freeY * 0.72f;
  }

  std::pair<int, int> pipCueCornerDirections(const Cue& cue) const {
    int xDir = cue.outputOffsetX < -1.0f ? -1 : 1;
    int yDir = cue.outputOffsetY > 1.0f ? 1 : -1;
    return {xDir, yDir};
  }

  void applySelectedPipCornerPreset(int horizontalDir, int verticalDir, const std::string& label) {
    if (forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
          if (cue.kind == CueKind::Pip) {
            anchorPipCueToCorner(cue, horizontalDir, verticalDir);
          }
        })) {
      triggerToast("pip corner: " + label);
      playUiSound(UiSoundEffect::Toggle);
      markProjectDirty();
    }
  }

  void applySelectedPipSizePreset(float scale, const std::string& label) {
    if (forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
          if (cue.kind == CueKind::Pip) {
            cue.outputScaleX = std::clamp(scale, 0.18f, 0.90f);
            cue.outputScaleY = std::clamp(scale, 0.18f, 0.90f);
            auto [xDir, yDir] = pipCueCornerDirections(cue);
            anchorPipCueToCorner(cue, xDir, yDir);
          }
        })) {
      triggerToast("pip size: " + label);
      playUiSound(UiSoundEffect::Toggle);
      markProjectDirty();
    }
  }

  void setSelectedPipCueTarget(const std::string& rawTarget) {
    Cue* selected = selectedCueMutable();
    if (!selected || selected->kind != CueKind::Pip) {
      return;
    }
    std::string normalized = trim(rawTarget);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Pip) {
        return;
      }
      cue.pipSourceType.clear();
      cue.pipTargetCue = normalized;
      std::string prefix = "PIP";
      if (!normalized.empty()) {
        prefix += " · " + normalized;
      }
      if (cue.name.empty() || cue.name == "PIP" || cue.name.rfind("PIP · ", 0) == 0) {
        cue.name = prefix;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    if (const Cue* current = selectedCuePtr()) {
      if (current->kind == CueKind::Pip) {
        const Deck& deck = focusedDeck();
        int targetCueIndex = -1;
        if (const Cue* targetCue = resolvePipTargetCue(deck, *current, &targetCueIndex)) {
          (void) targetCueIndex;
          requestThumbnail(*targetCue);
        } else {
          clearSelectedThumbnail();
        }
        syncPipOverlayRuntimesForDeck(project_.focusedDeckIndex, SDL_GetTicks());
      }
    }
    triggerToast(normalized.empty() ? "pip target cleared" : ("pip target: " + normalized));
    markProjectDirty();
  }

  void syncPipOverlayRuntimesForDeck(int deckIndex, Uint64 now) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    std::unordered_set<std::string> keepKeys;
    for (int overlayCueIndex : deck.overlayActiveIndices) {
      if (overlayCueIndex < 0 || overlayCueIndex >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      const Cue& overlayCue = deck.cues[overlayCueIndex];
      if (overlayCue.kind != CueKind::Pip) {
        continue;
      }
      std::string runtimeKey = pipOverlayRuntimeKey(deckIndex, overlayCueIndex);
      keepKeys.insert(runtimeKey);
      Cue resolvedCue;
      int targetCueIndex = -1;
      if (!buildResolvedPipSourceCue(deck, overlayCue, resolvedCue, &targetCueIndex)) {
        auto stale = pipOverlayRuntimes_.find(runtimeKey);
        if (stale != pipOverlayRuntimes_.end()) {
          if (stale->second.mediaEngine) {
            stale->second.mediaEngine->stopAll();
          }
          pipOverlayRuntimes_.erase(stale);
        }
        continue;
      }

      PipOverlayRuntime& runtime = pipOverlayRuntimes_[runtimeKey];
      if (!runtime.mediaEngine) {
        runtime.mediaEngine = std::make_unique<MediaEngine>(
          controlRenderer_,
          nullptr,
          MediaEngine::AudioTapCallback {},
          [this](const Cue& cue) {
            return resolvedCueFilesystemPathString(cue, currentProjectFile_);
          }
        );
      }

      std::string targetCueKey = cueRuntimeCacheKey(resolvedCue);
      if (runtime.loadedCueKey != targetCueKey ||
          runtime.targetCueIndex != targetCueIndex ||
          !runtime.resolvedCueValid ||
          !runtime.mediaEngine->activeCue()) {
        runtime.mediaEngine->stopAll();
        runtime.resolvedCue = resolvedCue;
        runtime.resolvedCueValid = true;
        runtime.mediaEngine->loadCue(&runtime.resolvedCue, !runtime.resolvedCue.pauseAtBeginning);
        runtime.loadedCueKey = targetCueKey;
        runtime.targetCueIndex = targetCueIndex;
      } else {
        runtime.resolvedCue = resolvedCue;
        runtime.resolvedCueValid = true;
      }

      runtime.mediaEngine->update();
      if (runtime.resolvedCue.kind == CueKind::Pattern && patternTypeIsAnimated(runtime.resolvedCue.path)) {
        runtime.mediaEngine->rebuildPatternFrame(runtime.resolvedCue, static_cast<double>(now) / 1000.0);
      }
      if (runtime.mediaEngine->reachedEnd()) {
        runtime.mediaEngine->finalizeReachedEnd(runtime.resolvedCue.pauseOnLastFrame);
      }
    }

    std::vector<std::string> staleKeys;
    staleKeys.reserve(pipOverlayRuntimes_.size());
    for (const auto& [runtimeKey, runtime] : pipOverlayRuntimes_) {
      (void) runtime;
      if (runtimeKey.rfind(std::to_string(deckIndex) + ":", 0) == 0 &&
          keepKeys.find(runtimeKey) == keepKeys.end()) {
        staleKeys.push_back(runtimeKey);
      }
    }
    for (const std::string& runtimeKey : staleKeys) {
      auto it = pipOverlayRuntimes_.find(runtimeKey);
      if (it == pipOverlayRuntimes_.end()) {
        continue;
      }
      if (it->second.mediaEngine) {
        it->second.mediaEngine->stopAll();
      }
      pipOverlayRuntimes_.erase(it);
    }
  }

  PipOverlayRuntime* pipOverlayRuntimeForCue(int deckIndex, int cueIndex) {
    auto it = pipOverlayRuntimes_.find(pipOverlayRuntimeKey(deckIndex, cueIndex));
    return it == pipOverlayRuntimes_.end() ? nullptr : &it->second;
  }

  std::string compositeAudioSummaryLabel(const Cue& cue) const {
    if (cue.compositeSlots.empty()) {
      return "(no slots)";
    }
    if (trim(cue.compositeAudioSlotId).empty()) {
      return "none";
    }
    for (const CompositeSlot& slot : cue.compositeSlots) {
      if (slot.id == cue.compositeAudioSlotId) {
        return slot.name + "  " + compositeSourceDisplayLabel(slot);
      }
    }
    return "(missing)";
  }

  void applySelectedCompositePreset(const std::string& presetToken, const std::string& label) {
    if (forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
          if (cue.kind == CueKind::Composite) {
            applyCompositePresetToCue(cue, presetToken);
          }
        })) {
      refreshSelectedCuePreviewCaches();
      triggerToast("scene preset: " + label);
      playUiSound(UiSoundEffect::Toggle);
      markProjectDirty();
    }
  }

  void setSelectedCompositeSlotSource(int slotIndex, const std::string& rawSpec) {
    Cue* selected = selectedCueMutable();
    if (!selected || selected->kind != CueKind::Composite) {
      return;
    }
    std::string spec = trim(rawSpec);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Composite) {
        return;
      }
      if (slotIndex < 0 || slotIndex >= static_cast<int>(cue.compositeSlots.size())) {
        return;
      }
      CompositeSlot& slot = cue.compositeSlots[slotIndex];
      auto [sourceType, sourceValue] = parseCompositeSourceSpec(spec);
      slot.sourceType = sourceType;
      slot.source = sourceValue;
      ensureCompositeSlotIdentity(slot, slotIndex);
      changed = true;
    });
    if (!changed) {
      return;
    }
    refreshSelectedCuePreviewCaches();
    triggerToast("scene slot source set");
    markProjectDirty();
  }

  void cycleSelectedCompositeAudioSlot() {
    Cue* selected = selectedCueMutable();
    if (!selected || selected->kind != CueKind::Composite) {
      return;
    }
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Composite) {
        return;
      }
      std::vector<std::string> choices;
      choices.push_back("");
      for (const CompositeSlot& slot : cue.compositeSlots) {
        choices.push_back(slot.id);
      }
      if (choices.empty()) {
        return;
      }
      auto it = std::find(choices.begin(), choices.end(), cue.compositeAudioSlotId);
      size_t index = it == choices.end() ? 0u : static_cast<size_t>(std::distance(choices.begin(), it));
      cue.compositeAudioSlotId = choices[(index + 1) % choices.size()];
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("scene audio: " + compositeAudioSummaryLabel(*selectedCuePtr()));
    markProjectDirty();
  }

  bool findCueToken(const std::string& token, int direction = 1, bool triggerJump = false) {
    normalizeProject(project_);
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) {
      triggerToast("find: empty deck");
      return false;
    }
    std::string trimmed = trim(token);
    if (trimmed.empty()) {
      triggerToast("find: enter cue id/number/name");
      return false;
    }

    bool tokenChanged = toUpper(trimmed) != toUpper(lastCueFindToken_) ||
      lastCueFindDeckIndex_ != project_.focusedDeckIndex || lastCueFindMatches_.empty();
    if (tokenChanged) {
      lastCueFindToken_ = trimmed;
      lastCueFindDeckIndex_ = project_.focusedDeckIndex;
      lastCueFindMatches_ = cueFindMatches(deck, trimmed);
      lastCueFindCursor_ = -1;
    }
    if (lastCueFindMatches_.empty()) {
      triggerToast("find: none");
      return false;
    }

    if (lastCueFindCursor_ < 0 || tokenChanged) {
      lastCueFindCursor_ = direction < 0
        ? static_cast<int>(lastCueFindMatches_.size()) - 1
        : 0;
    } else if (direction != 0) {
      int count = static_cast<int>(lastCueFindMatches_.size());
      lastCueFindCursor_ = (lastCueFindCursor_ + (direction > 0 ? 1 : -1) + count) % count;
    }

    int cueIndex = lastCueFindMatches_[lastCueFindCursor_];
    if (deck.selectedIndex != cueIndex) {
      deck.selectedIndex = cueIndex;
      onSelectionChanged();
      markProjectDirty();
    }

    std::string cueNum = cueDisplayToken(deck.cues[cueIndex], cueIndex);
    triggerToast("find " + std::to_string(lastCueFindCursor_ + 1) + "/" +
      std::to_string(lastCueFindMatches_.size()) + " -> " + cueNum + " " + deck.cues[cueIndex].name);
    if (triggerJump) {
      jumpSelectedCue();
    }
    return true;
  }

  void clearCueFindState() {
    lastCueFindToken_.clear();
    lastCueFindMatches_.clear();
    lastCueFindCursor_ = -1;
    lastCueFindDeckIndex_ = -1;
  }

  bool handleCueTypeAheadKey(SDL_Keycode key, Uint16 mod) {
    if ((mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0) {
      return false;
    }
    auto toSearchChar = [&](SDL_Keycode code) -> char {
      if (code >= SDLK_0 && code <= SDLK_9) {
        return static_cast<char>('0' + (code - SDLK_0));
      }
      if (code >= SDLK_A && code <= SDLK_Z) {
        return static_cast<char>('A' + (code - SDLK_A));
      }
      if (code == SDLK_MINUS) return '-';
      if (code == SDLK_UNDERSCORE) return '_';
      return '\0';
    };

    Uint64 now = SDL_GetTicks();
    if (now > typedCueSearchLastKeyAtMs_ + 1200) {
      typedCueSearchBuffer_.clear();
    }
    typedCueSearchLastKeyAtMs_ = now;

    if (key == SDLK_BACKSPACE) {
      if (!typedCueSearchBuffer_.empty()) {
        typedCueSearchBuffer_.pop_back();
        if (!typedCueSearchBuffer_.empty()) {
          findCueToken(typedCueSearchBuffer_, 1, false);
          triggerToast("id: " + typedCueSearchBuffer_);
        } else {
          triggerToast("id: cleared");
        }
      }
      return true;
    }

    char ch = toSearchChar(key);
    if (ch == '\0') {
      return false;
    }
    typedCueSearchBuffer_.push_back(ch);
    if (typedCueSearchBuffer_.size() > 6) {
      typedCueSearchBuffer_.erase(typedCueSearchBuffer_.begin());
    }
    findCueToken(typedCueSearchBuffer_, 1, false);
    triggerToast("id: " + typedCueSearchBuffer_);
    return true;
  }

  void renumberFocusedDeckCueNumbers(const std::string& prefix = "", int startAt = 1) {
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) {
      triggerToast("renumber: empty deck");
      return;
    }
    int start = std::max(1, startAt);
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      std::string token = prefix + std::to_string(start + cueIndex);
      deck.cues[cueIndex].cueNumber = token;
      deck.cues[cueIndex].cueId = normalizeCueIdShort(token);
    }
    clearCueFindState();
    triggerToast("renumbered " + std::to_string(deck.cues.size()) + " cues");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void clearFocusedDeckCueNumbers() {
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) {
      return;
    }
    for (auto& cue : deck.cues) {
      cue.cueNumber.clear();
      cue.cueId.clear();
    }
    clearCueFindState();
    triggerToast("cue numbers cleared");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool selectCueById(const std::string& cueId) {
    Deck& deck = focusedDeckMutable();
    auto index = cueIndexById(deck, cueId);
    if (!index) {
      return false;
    }
    selectCueInDeck(project_.focusedDeckIndex, *index, false, false);
    triggerToast("cue " + std::to_string(*index + 1) + " armed");
    return true;
  }

  bool takeCueById(const std::string& cueId, bool autoplay) {
    Deck& deck = focusedDeckMutable();
    auto index = cueIndexById(deck, cueId);
    if (!index) {
      return false;
    }
    selectCueInDeck(project_.focusedDeckIndex, *index, false, false);
    takeSelected(autoplay);
    return true;
  }

  double snapToCueFrame(const Cue& cue, double seconds) const {
    double duration = std::max(0.0, cue.duration);
    double clamped = std::clamp(seconds, 0.0, duration);
    double fps = cue.fps;
    if (!std::isfinite(fps) || fps <= 0.0) {
      return clamped;
    }
    double frameIndex = std::round(clamped * fps);
    double snapped = frameIndex / fps;
    return std::clamp(snapped, 0.0, duration);
  }

  void setSelectedTrimIn(double seconds) {
    if (!firstFocusedSelectedCueMutable([&](const Cue& cue) {
      return cue.kind == CueKind::Video;
    })) {
      return;
    }
    double sampleNext = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video) {
        return;
      }
      double duration = std::max(0.0, cue.duration);
      double next = snapToCueFrame(cue, seconds);
      double out = cue.outPointSeconds > 0.0 ? cue.outPointSeconds : duration;
      out = std::clamp(snapToCueFrame(cue, out), next, duration);
      cue.inPointSeconds = next;
      cue.outPointSeconds = out;
      if (!changed) {
        sampleNext = next;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("in " + formatSeconds(sampleNext));
    markProjectDirty();
  }

  void setSelectedTrimOut(double seconds) {
    if (!firstFocusedSelectedCueMutable([&](const Cue& cue) {
      return cue.kind == CueKind::Video;
    })) {
      return;
    }
    double sampleNext = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video) {
        return;
      }
      double duration = std::max(0.0, cue.duration);
      double next = std::clamp(snapToCueFrame(cue, seconds), cue.inPointSeconds, duration);
      cue.outPointSeconds = next;
      if (!changed) {
        sampleNext = next;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("out " + formatSeconds(sampleNext));
    markProjectDirty();
  }

  void clearSelectedTrim() {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video) {
        return;
      }
      cue.inPointSeconds = 0.0;
      cue.outPointSeconds = cue.duration;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("trim reset");
    markProjectDirty();
  }

  bool setActiveTrimFromPlayhead(bool setInPoint) {
    Deck& deck = focusedDeckMutable();
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      return false;
    }
    Cue& cue = deck.cues[deck.activeIndex];
    if (cue.kind != CueKind::Video) {
      return false;
    }
    MediaEngine* engine = focusedMediaEngine();
    if (!engine) {
      return false;
    }
    double duration = std::max(0.0, cue.duration);
    double playhead = snapToCueFrame(cue, cueAbsolutePlayheadSeconds(cue, *engine));
    if (setInPoint) {
      double out = cue.outPointSeconds > 0.0 ? cue.outPointSeconds : duration;
      cue.inPointSeconds = playhead;
      cue.outPointSeconds = std::clamp(snapToCueFrame(cue, out), cue.inPointSeconds, duration);
      triggerToast("in " + formatSeconds(cue.inPointSeconds));
    } else {
      cue.outPointSeconds = std::clamp(playhead, cue.inPointSeconds, duration);
      triggerToast("out " + formatSeconds(cue.outPointSeconds));
    }
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool nudgeFocusedPausedPlayback(int direction, Uint16 mod) {
    if (direction == 0) {
      return false;
    }
    MediaEngine* engine = focusedMediaEngine();
    const Cue* cue = activeCuePtr();
    if (!engine || !cue || cue->kind != CueKind::Video) {
      return false;
    }
    if (engine->state() != TransportState::Paused) {
      return false;
    }
    double fps = (std::isfinite(cue->fps) && cue->fps > 1.0) ? cue->fps : 30.0;
    double stepSeconds = 1.0 / fps;
    std::string stepLabel = "1f";
    if ((mod & SDL_KMOD_ALT) != 0) {
      stepSeconds = 1.0;
      stepLabel = "1s";
    } else if ((mod & SDL_KMOD_CTRL) != 0) {
      stepSeconds = 10.0 / fps;
      stepLabel = "10f";
    } else if ((mod & SDL_KMOD_SHIFT) != 0) {
      stepSeconds = 5.0 / fps;
      stepLabel = "5f";
    }
    double target = snapToCueFrame(*cue, engine->position() + static_cast<double>(direction) * stepSeconds);
    engine->seek(target);
    triggerToast((direction > 0 ? ">> " : "<< ") + stepLabel + "  " + formatTimecode(target, fps));
    playUiSound(UiSoundEffect::Toggle);
    return true;
  }

  void setTimeOverlayEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.timeOverlayEnabled == enabled) {
      return;
    }
    deck.timeOverlayEnabled = enabled;
    triggerToast(deck.timeOverlayEnabled ? "time overlay on" : "time overlay off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleTimeOverlayEnabled() {
    setTimeOverlayEnabled(!focusedDeck().timeOverlayEnabled);
  }

  void setTransitionSeconds(double seconds) {
    Deck& deck = focusedDeckMutable();
    double next = std::clamp(seconds, 0.0, 10.0);
    if (std::abs(deck.transitionSeconds - next) < 0.001) {
      return;
    }
    deck.transitionSeconds = next;
    triggerToast("transition " + formatSeconds(deck.transitionSeconds));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setTransitionStyle(TransitionStyle style) {
    Deck& deck = focusedDeckMutable();
    std::string token = transitionStyleToken(style);
    if (deck.transitionStyle == token) {
      return;
    }
    deck.transitionStyle = token;
    triggerToast("style " + deck.transitionStyle);
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool setSelectedCueTimecodeTrigger(double seconds) {
    double clamped = std::max(0.0, seconds);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      cue.triggerTimecodeSeconds = clamped;
      changed = true;
    });
    if (!changed) {
      return false;
    }
    triggerToast("tc mark " + formatTimecode(clamped, focusedDeck().timecodeFps));
    markProjectDirty();
    return true;
  }

  void clearSelectedCueTimecodeTrigger() {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      cue.triggerTimecodeSeconds = -1.0;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("tc mark cleared");
    markProjectDirty();
  }

  void ensureTimecodeFollowerStateSize() {
    size_t deckCount = project_.decks.size();
    if (deckTimecodeLastExternalMs_.size() != deckCount) {
      deckTimecodeLastExternalMs_.resize(deckCount, 0);
    }
    if (deckTimecodeLastExternalSeconds_.size() != deckCount) {
      deckTimecodeLastExternalSeconds_.resize(deckCount, 0.0);
    }
    if (deckTimecodeHasExternal_.size() != deckCount) {
      deckTimecodeHasExternal_.resize(deckCount, false);
    }
  }

  void resetTimecodeFollowerState() {
    ensureTimecodeFollowerStateSize();
    std::fill(deckTimecodeLastExternalMs_.begin(), deckTimecodeLastExternalMs_.end(), 0);
    std::fill(deckTimecodeLastExternalSeconds_.begin(), deckTimecodeLastExternalSeconds_.end(), 0.0);
    std::fill(deckTimecodeHasExternal_.begin(), deckTimecodeHasExternal_.end(), false);
  }

  void setTimecodeFps(double fps) {
    Deck& deck = focusedDeckMutable();
    double next = std::clamp(fps, 1.0, 120.0);
    if (std::abs(deck.timecodeFps - next) < 0.001) {
      return;
    }
    deck.timecodeFps = next;
    triggerToast("tc fps " + std::to_string(static_cast<int>(std::round(deck.timecodeFps))));
    markProjectDirty();
  }

  void setTimecodeChaseEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.timecodeChaseEnabled == enabled) {
      return;
    }
    deck.timecodeChaseEnabled = enabled;
    triggerToast(deck.timecodeChaseEnabled ? "tc chase on" : "tc chase off");
    markProjectDirty();
  }

  void setTimecodeRunEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.timecodeRunEnabled == enabled) {
      return;
    }
    deck.timecodeRunEnabled = enabled;
    triggerToast(deck.timecodeRunEnabled ? "tc run on" : "tc run off");
    markProjectDirty();
  }

  void setTimecodeJamSyncEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.timecodeJamSyncEnabled == enabled) {
      triggerToast(enabled ? "tc jam on" : "tc jam off");
      return;
    }
    deck.timecodeJamSyncEnabled = enabled;
    triggerToast(deck.timecodeJamSyncEnabled ? "tc jam on" : "tc jam off");
    markProjectDirty();
  }

  void setTimecodeFreewheelSeconds(double seconds) {
    Deck& deck = focusedDeckMutable();
    double next = std::clamp(std::isfinite(seconds) ? seconds : 1.0, 0.0, 10.0);
    if (std::abs(deck.timecodeFreewheelSeconds - next) < 0.001) {
      std::ostringstream label;
      label << std::fixed << std::setprecision(1) << next;
      triggerToast("tc freewheel " + label.str() + "s");
      return;
    }
    deck.timecodeFreewheelSeconds = next;
    std::ostringstream label;
    label << std::fixed << std::setprecision(1) << deck.timecodeFreewheelSeconds;
    triggerToast("tc freewheel " + label.str() + "s");
    markProjectDirty();
  }

  void setDeckTimecode(int deckIndex, double seconds, bool forceApply = false) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    ensureTimecodeFollowerStateSize();
    Deck& deck = project_.decks[deckIndex];
    double normalized = std::max(0.0, std::isfinite(seconds) ? seconds : 0.0);
    Uint64 now = SDL_GetTicks();
    bool hadExternal = deckTimecodeHasExternal_[deckIndex];
    Uint64 previousExternalMs = deckTimecodeLastExternalMs_[deckIndex];
    Uint64 freewheelMs = static_cast<Uint64>(
      std::llround(std::max(0.0, deck.timecodeFreewheelSeconds) * 1000.0));
    bool withinFreewheel = hadExternal && now >= previousExternalMs && (now - previousExternalMs) <= freewheelMs;
    bool suppressJam = !forceApply
      && !deck.timecodeJamSyncEnabled
      && deck.timecodeChaseEnabled
      && deck.timecodeRunEnabled
      && withinFreewheel;

    deckTimecodeHasExternal_[deckIndex] = true;
    deckTimecodeLastExternalMs_[deckIndex] = now;
    deckTimecodeLastExternalSeconds_[deckIndex] = normalized;
    if (suppressJam) {
      return;
    }

    if (normalized + 0.0001 < deck.timecodeCurrentSeconds) {
      timecodeTriggeredCueIds_.erase(deckIndex);
    }
    deck.timecodeLastSeconds = deck.timecodeCurrentSeconds;
    deck.timecodeCurrentSeconds = normalized;
    deck.timecodeDirty = true;
  }

  void ingestIntegrationTimecode(double seconds, double fpsHint) {
    if (project_.decks.empty()) {
      return;
    }
    double normalizedSeconds = std::max(0.0, std::isfinite(seconds) ? seconds : 0.0);
    double normalizedFps = std::isfinite(fpsHint) && fpsHint > 1.0 ? fpsHint : focusedDeck().timecodeFps;
    bool shouldSkip = std::fabs(normalizedSeconds - lastMtcIngestSeconds_) < 0.0005
                   && std::fabs(normalizedFps - lastMtcIngestFps_) < 0.01;
    if (shouldSkip) {
      return;
    }
    lastMtcIngestSeconds_ = normalizedSeconds;
    lastMtcIngestFps_ = normalizedFps;

    bool applied = false;
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (!project_.decks[deckIndex].timecodeChaseEnabled) {
        continue;
      }
      setDeckTimecode(deckIndex, normalizedSeconds, false);
      applied = true;
    }
    if (!applied) {
      setDeckTimecode(project_.focusedDeckIndex, normalizedSeconds, false);
    }
  }

  // Integration trigger commands are restricted to a safe subset to prevent
  // malicious NDI/ATEM/NMC sources from executing destructive operations.
  bool isIntegrationSafeCommand(const std::string& upperCmd) const {
    // Safe: transport and navigation commands only
    static const std::array<const char*, 20> kSafe {{
      "TAKE", "GO", "PLAY", "PAUSE", "STOP", "TOGGLE",
      "NEXT", "PREV", "PREVIOUS", "SKIP", "SKIPBACK",
      "RERACK", "LOOP", "FADE",
      "DECK", "SELECT", "GOTO", "FIND", "JUMP",
      "STATUS"
    }};
    // Extract the first word (the verb)
    std::string verb = upperCmd;
    auto sp = verb.find(' ');
    if (sp != std::string::npos) verb = verb.substr(0, sp);
    for (const char* safe : kSafe) {
      if (verb == safe) return true;
    }
    return false;
  }

  void handleAtemEventPayload(const std::string& payloadRaw) {
    if (!project_.atemTriggerEnabled) {
      return;
    }
    std::string payload = toUpper(trim(payloadRaw));
    if (payload.empty()) {
      return;
    }
    if (payload.rfind("DECKBOY ", 0) == 0) {
      std::string inner = payload.substr(8);
      if (!isIntegrationSafeCommand(inner)) return;  // block unsafe commands from integrations
      handleRemoteCommand(inner);
      return;
    }
    if (payload == "CUT" || payload == "AUTO" || payload == "TAKE") {
      handleRemoteCommand("TAKE");
      return;
    }
    if (payload == "BLACK" || payload == "FTB") {
      handleRemoteCommand("CLEAR");
      return;
    }
    if (payload == "PLAY" || payload == "PAUSE" || payload == "STOP"
        || payload == "NEXT" || payload == "PREV" || payload == "GO"
        || payload == "CLEAR" || payload == "PANIC") {
      handleRemoteCommand(payload);
      return;
    }
    if (payload.rfind("SCENE ", 0) == 0) {
      std::string token = trim(payload.substr(6));
      if (!token.empty()) {
        handleRemoteCommand("GROUP " + token + " FIRE");
      }
      return;
    }

    // Only forward integration-safe prefixed commands (no MASTER, VIDEO, PANIC, CLEAR)
    static const std::array<const char*, 5> kSafeIntegrationPrefixes {{
      "DECK ", "TAKE ", "GOTO ", "SELECT ", "FIND "
    }};
    for (const char* prefix : kSafeIntegrationPrefixes) {
      if (payload.rfind(prefix, 0) == 0) {
        handleRemoteCommand(payload);
        return;
      }
    }
  }

  void dispatchIntegrationTriggerCandidate(const std::string& candidateRaw) {
    std::string candidate = trim(candidateRaw);
    if (candidate.empty()) {
      return;
    }
    std::string upper = toUpper(candidate);
    if (upper.rfind("DECKBOY:", 0) == 0) {
      upper = trim(upper.substr(8));
    } else if (upper.rfind("CMD:", 0) == 0) {
      upper = trim(upper.substr(4));
    } else if (upper.rfind("COMMAND:", 0) == 0) {
      upper = trim(upper.substr(8));
    }
    if (upper.empty()) {
      return;
    }
    handleAtemEventPayload(upper);
  }

  void handleNdiTriggerPayload(const std::string& payloadRaw) {
    if (!project_.ndiTriggerEnabled) {
      return;
    }
    std::string payload = trim(payloadRaw);
    if (payload.empty()) {
      return;
    }

    std::vector<std::string> candidates;
    auto pushCandidate = [&](const std::string& value) {
      std::string normalized = trim(value);
      if (normalized.empty()) {
        return;
      }
      if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end()) {
        candidates.push_back(normalized);
      }
    };

    if (payload.find('<') == std::string::npos) {
      pushCandidate(payload);
    } else {
#ifndef _WIN32
      for (const char* attr : {"command", "cmd", "action", "event", "payload"}) {
        if (auto value = xmlAttributeValueCaseInsensitive(payload, attr)) {
          pushCandidate(*value);
        }
      }
      if (auto cue = xmlAttributeValueCaseInsensitive(payload, "cue")) {
        pushCandidate("GOTO " + *cue);
      }
      if (auto cue = xmlAttributeValueCaseInsensitive(payload, "goto")) {
        pushCandidate("GOTO " + *cue);
      }
      if (auto group = xmlAttributeValueCaseInsensitive(payload, "group")) {
        pushCandidate("GROUP " + *group + " FIRE");
      }
      for (const char* element : {"command", "cmd", "action", "event", "payload", "deckboy"}) {
        if (auto value = xmlElementTextCaseInsensitive(payload, element)) {
          pushCandidate(*value);
        }
      }
#endif
    }

    for (const auto& candidate : candidates) {
      dispatchIntegrationTriggerCandidate(candidate);
    }
  }

  void handleNmcSyncPayload(const std::string& payloadRaw) {
    if (!project_.nmcSyncEnabled) {
      return;
    }
    if (focusedDeck().timecodeChaseEnabled) {
      return;
    }

    auto packet = parseNmcSyncPacket(payloadRaw);
    if (!packet) {
      return;
    }

    auto ensureCueLoaded = [&]() -> bool {
      if (activeCuePtr()) {
        return true;
      }
      if (!selectedCuePtr()) {
        return false;
      }
      takeSelected(false, false, true);
      return activeCuePtr() != nullptr;
    };

    std::string command = toUpper(packet->command);
    if (command == "LOCATE") {
      if (!packet->seconds || !ensureCueLoaded()) {
        return;
      }
      if (MediaEngine* engine = focusedMediaEngine()) {
        engine->seek(*packet->seconds, false);
      }
      return;
    }
    if (command == "PLAY") {
      if (!ensureCueLoaded()) {
        return;
      }
      if (packet->seconds) {
        if (MediaEngine* engine = focusedMediaEngine()) {
          engine->seek(*packet->seconds, false);
        }
      }
      playTransport();
      return;
    }
    if (command == "PAUSE") {
      if (!ensureCueLoaded()) {
        return;
      }
      if (packet->seconds) {
        if (MediaEngine* engine = focusedMediaEngine()) {
          engine->seek(*packet->seconds, false);
        }
      }
      pauseTransport();
      return;
    }
    if (command == "STOP") {
      if (!ensureCueLoaded()) {
        stopTransport();
        return;
      }
      stopTransport();
      if (packet->seconds) {
        if (MediaEngine* engine = focusedMediaEngine()) {
          engine->seek(*packet->seconds, false);
        }
      }
      return;
    }
  }

  void handleArtNetEvent(int channel, int value) {
    if (!project_.dmxArtNetEnabled) {
      return;
    }
    switch (channel) {
      case 1: handleRemoteCommand("TAKE"); break;
      case 2: handleRemoteCommand("PLAY"); break;
      case 3: handleRemoteCommand("STOP"); break;
      case 4: handleRemoteCommand("GO"); break;
      case 5: handleRemoteCommand("NEXT"); break;
      case 6: handleRemoteCommand("PREV"); break;
      case 7: handleRemoteCommand("CLEAR"); break;
      case 8: handleRemoteCommand("PANIC"); break;
      case 9:
        if (value > 0) {
          handleRemoteCommand("TAKE " + std::to_string(std::clamp(value, 1, 255)));
        }
        break;
      case 10:
        if (value > 0) {
          handleRemoteCommand("GROUP " + std::to_string(std::clamp(value, 1, 255)) + " FIRE");
        }
        break;
      default:
        break;
    }
  }

  void processTimecodeTriggersForDeck(int deckIndex, double fromSeconds, double toSeconds) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    if (deck.cues.empty()) {
      return;
    }

    auto& fired = timecodeTriggeredCueIds_[deckIndex];
    if (toSeconds + 0.0001 < fromSeconds) {
      fired.clear();
      return;
    }

    std::optional<int> bestIndex;
    double bestTc = 0.0;
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      const Cue& cue = deck.cues[cueIndex];
      if (cue.triggerTimecodeSeconds < 0.0) {
        continue;
      }
      if (cue.triggerTimecodeSeconds <= toSeconds + 0.0001 && cue.triggerTimecodeSeconds > fromSeconds + 0.0001) {
        if (fired.find(cue.id) != fired.end()) {
          continue;
        }
        if (!bestIndex || cue.triggerTimecodeSeconds < bestTc) {
          bestIndex = cueIndex;
          bestTc = cue.triggerTimecodeSeconds;
        }
      }
    }

    if (!bestIndex) {
      return;
    }

    fired.insert(deck.cues[*bestIndex].id);
    deck.selectedIndex = *bestIndex;
    int previousFocus = project_.focusedDeckIndex;
    project_.focusedDeckIndex = deckIndex;
    takeSelected(true);
    project_.focusedDeckIndex = previousFocus;
  }

  void setFocusedDeckTimecode(double seconds, bool forceApply = false) {
    setDeckTimecode(project_.focusedDeckIndex, seconds, forceApply);
    triggerToast("tc " + formatTimecode(focusedDeck().timecodeCurrentSeconds, focusedDeck().timecodeFps));
  }

  void cycleSelectedEndAction() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.kind == CueKind::Video || each.kind == CueKind::Audio;
    });
    if (!cue) {
      return;
    }
    CueEndAction next = cue->endAction;
    // Cycle: Inherit → Stop → Loop → PauseOnLast → AutoNext → Inherit
    switch (next) {
      case CueEndAction::Inherit:     next = CueEndAction::Stop;       break;
      case CueEndAction::Stop:        next = CueEndAction::Loop;       break;
      case CueEndAction::Loop:        next = CueEndAction::PauseOnLast; break;
      case CueEndAction::PauseOnLast: next = CueEndAction::AutoNext;   break;
      case CueEndAction::AutoNext:    next = CueEndAction::Inherit;    break;
    }
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.kind != CueKind::Video && each.kind != CueKind::Audio) {
        return;
      }
      each.endAction = next;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("end: " + cueEndActionLabel(next));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedEndAction(CueEndAction action) {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.kind == CueKind::Video || each.kind == CueKind::Audio;
    });
    if (!cue || cue->endAction == action) {
      return;
    }
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.kind != CueKind::Video && each.kind != CueKind::Audio) {
        return;
      }
      each.endAction = action;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("end: " + cueEndActionLabel(action));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool cueSupportsGeometry(const Cue* cue) const {
    return cue && (cue->kind == CueKind::Video
      || cue->kind == CueKind::Image
      || cue->kind == CueKind::Pattern
      || cue->kind == CueKind::Browser
      || cue->kind == CueKind::Pip
      || isSourceCueKind(cue->kind));
  }

  bool cueSupportsKeying(const Cue* cue) const {
    return cue && (cue->kind == CueKind::Video
      || cue->kind == CueKind::Image
      || cue->kind == CueKind::Pattern
      || cue->kind == CueKind::Browser
      || cue->kind == CueKind::Pip
      || isSourceCueKind(cue->kind));
  }

  bool cueSupportsColorControls(const Cue* cue) const {
    return cueSupportsKeying(cue);
  }

  bool setSelectedKeyColor(SDL_Color color) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsKeying(&each)) {
        return;
      }
      each.chromaKeyColor = color;
      changed = true;
    });
    if (!changed) {
      return false;
    }
    triggerToast("key color " + colorToHex(color));
    markProjectDirty();
    return true;
  }

  void openInlineNumericExpressionEditor(const std::string& owner,
                                         const std::string& title,
                                         const std::string& prompt,
                                         const std::string& currentValue,
                                         std::function<void(double)> onParsed) {
    openInlineTextEditor(owner, title, prompt, currentValue,
                         [this, onParsed = std::move(onParsed)](const std::string& value) {
      auto parsed = parseNumericExpression(value);
      if (!parsed) {
        triggerToast("invalid number");
        return;
      }
      onParsed(*parsed);
    });
  }

  void openInlineKeyColorEditor() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsKeying(cue)) {
      return;
    }
    openInlineTextEditor("cue.key_color", "Key Color",
                         "Hex color (#RRGGBB or #RRGGBBAA):", colorToHex(cue->chromaKeyColor),
                         [this](const std::string& value) {
      auto color = tryParseColor(value);
      if (!color) {
        triggerToast("invalid key color");
        return;
      }
      setSelectedKeyColor(*color);
    });
  }

  void armSelectedKeyColorPicker() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return cueSupportsKeying(&each);
    });
    if (!cue) {
      return;
    }
    keyColorPickerArmed_ = true;
    triggerToast("click program or preview monitor to sample key color");
  }

  std::optional<SDL_Color> sampleControlWindowColor(int x, int y) {
    if (!controlRenderer_) {
      return std::nullopt;
    }
    // SDL3: SDL_RenderReadPixels returns a freshly allocated surface.
    SDL_Rect sampleRect {x, y, 1, 1};
    SDL_Surface* sampled = SDL_RenderReadPixels(controlRenderer_, &sampleRect);
    if (!sampled) {
      return std::nullopt;
    }
    Uint8 r = 0, g = 0, b = 0, a = 0;
    bool ok = SDL_ReadSurfacePixel(sampled, 0, 0, &r, &g, &b, &a);
    SDL_DestroySurface(sampled);
    if (!ok) {
      return std::nullopt;
    }
    return SDL_Color {r, g, b, a};
  }

  bool handleKeyColorPickerMouseDown(int x, int y) {
    if (!keyColorPickerArmed_) {
      return false;
    }
    bool hitMonitor = pointInRect(x, y, warpMonitorInner_) ||
                      pointInRect(x, y, previewMonitorInner_);
    keyColorPickerArmed_ = false;
    if (!hitMonitor) {
      triggerToast("key color picker canceled");
      return true;
    }
    auto color = sampleControlWindowColor(x, y);
    if (!color) {
      triggerToast("key color sample failed");
      return true;
    }
    setSelectedKeyColor(*color);
    playUiSound(UiSoundEffect::Take);
    return true;
  }

  // Geometry size editing is pixel-first: the operator types the target
  // rendered width/height in output pixels; the stored outputScaleX/Y
  // multiplier is derived per cue from its base rendered size. When the
  // aspect link is on (Project::geometryAspectLinked, the default), the
  // other axis scales by the same relative factor. These two setters are the
  // single write path — inspector editors and remote WIDTH/HEIGHT commands
  // both go through them so the link always applies.
  bool setSelectedWidthPx(double px) {
    bool link = project_.geometryAspectLinked;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsGeometry(&each)) {
        return;
      }
      auto [bw, bh] = cueBaseRenderSize(each);
      if (bw <= 0.0) {
        return;
      }
      float oldX = each.outputScaleX;
      float next = std::clamp(static_cast<float>(px / bw), 0.25f, 4.0f);
      each.outputScaleX = next;
      if (link && oldX > 0.0001f) {
        each.outputScaleY = std::clamp(each.outputScaleY * (next / oldX), 0.25f, 4.0f);
      }
      changed = true;
    });
    if (changed) {
      markProjectDirty();
    }
    return changed;
  }

  bool setSelectedHeightPx(double px) {
    bool link = project_.geometryAspectLinked;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsGeometry(&each)) {
        return;
      }
      auto [bw, bh] = cueBaseRenderSize(each);
      if (bh <= 0.0) {
        return;
      }
      float oldY = each.outputScaleY;
      float next = std::clamp(static_cast<float>(px / bh), 0.25f, 4.0f);
      each.outputScaleY = next;
      if (link && oldY > 0.0001f) {
        each.outputScaleX = std::clamp(each.outputScaleX * (next / oldY), 0.25f, 4.0f);
      }
      changed = true;
    });
    if (changed) {
      markProjectDirty();
    }
    return changed;
  }

  void editSelectedScaleX() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    auto [baseW, baseH] = cueBaseRenderSize(*cue);
    if (baseW <= 0.0) {
      return;
    }
    std::string current = std::to_string(
      std::max(1, static_cast<int>(std::lround(baseW * cue->outputScaleX))));
    openInlineNumericExpressionEditor("cue.scale_x", "Width (px)",
                                      "Output width in px - math ok: 1920x2, 3840/2, 960+64", current,
                                      [this](double value) {
      if (!setSelectedWidthPx(value)) {
        return;
      }
      triggerToast("width " + std::to_string(static_cast<int>(std::lround(value))) + "px"
                   + (project_.geometryAspectLinked ? "  (aspect linked)" : ""));
    });
  }

  void editSelectedScaleY() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    auto [baseW, baseH] = cueBaseRenderSize(*cue);
    if (baseH <= 0.0) {
      return;
    }
    std::string current = std::to_string(
      std::max(1, static_cast<int>(std::lround(baseH * cue->outputScaleY))));
    openInlineNumericExpressionEditor("cue.scale_y", "Height (px)",
                                      "Output height in px - math ok: 1080x2, 2160/2, 540+30", current,
                                      [this](double value) {
      if (!setSelectedHeightPx(value)) {
        return;
      }
      triggerToast("height " + std::to_string(static_cast<int>(std::lround(value))) + "px"
                   + (project_.geometryAspectLinked ? "  (aspect linked)" : ""));
    });
  }

  void editSelectedOffsetX() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    int current = static_cast<int>(std::lround(cue->outputOffsetX));
    openInlineNumericExpressionEditor("cue.offset_x", "Offset X",
                                      "Pixels (supports + - * / and ())", std::to_string(current),
                                      [this](double value) {
      float next = static_cast<float>(std::lround(value));
      bool changed = false;
      forEachFocusedSelectedCueMutable([&](Cue& each, int) {
        if (!cueSupportsGeometry(&each)) {
          return;
        }
        each.outputOffsetX = next;
        changed = true;
      });
      if (!changed) {
        return;
      }
      triggerToast("off X " + std::to_string(static_cast<int>(std::lround(next))) + "px");
      markProjectDirty();
    });
  }

  void editSelectedOffsetY() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    int current = static_cast<int>(std::lround(cue->outputOffsetY));
    openInlineNumericExpressionEditor("cue.offset_y", "Offset Y",
                                      "Pixels (supports + - * / and ())", std::to_string(current),
                                      [this](double value) {
      float next = static_cast<float>(std::lround(value));
      bool changed = false;
      forEachFocusedSelectedCueMutable([&](Cue& each, int) {
        if (!cueSupportsGeometry(&each)) {
          return;
        }
        each.outputOffsetY = next;
        changed = true;
      });
      if (!changed) {
        return;
      }
      triggerToast("off Y " + std::to_string(static_cast<int>(std::lround(next))) + "px");
      markProjectDirty();
    });
  }

  void editSelectedRotation() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    std::ostringstream current;
    current << std::fixed << std::setprecision(1) << cue->outputRotationDegrees;
    openInlineNumericExpressionEditor("cue.rotation", "Rotation",
                                      "Degrees -180..180 (supports + - * / and ())", current.str(),
                                      [this](double value) {
      float next = std::clamp(static_cast<float>(value), -180.0f, 180.0f);
      bool changed = false;
      forEachFocusedSelectedCueMutable([&](Cue& each, int) {
        if (!cueSupportsGeometry(&each)) {
          return;
        }
        each.outputRotationDegrees = next;
        changed = true;
      });
      if (!changed) {
        return;
      }
      std::ostringstream ss;
      ss << std::fixed << std::setprecision(1) << next;
      triggerToast("rot " + ss.str() + " deg");
      markProjectDirty();
    });
  }

  void normalizeCueCrop(Cue& cue) {
    cue.cropLeft = std::clamp(cue.cropLeft, 0.0f, 0.90f);
    cue.cropRight = std::clamp(cue.cropRight, 0.0f, 0.90f);
    cue.cropTop = std::clamp(cue.cropTop, 0.0f, 0.90f);
    cue.cropBottom = std::clamp(cue.cropBottom, 0.0f, 0.90f);
    cue.cropRight = std::min(cue.cropRight, std::max(0.0f, 0.95f - cue.cropLeft));
    cue.cropBottom = std::min(cue.cropBottom, std::max(0.0f, 0.95f - cue.cropTop));
  }

  void adjustSelectedRotation(float deltaDegrees) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      cue.outputRotationDegrees = std::clamp(cue.outputRotationDegrees + deltaDegrees, -180.0f, 180.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedCrop(char edge, float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      switch (edge) {
        case 'L': cue.cropLeft += delta; break;
        case 'R': cue.cropRight += delta; break;
        case 'T': cue.cropTop += delta; break;
        case 'B': cue.cropBottom += delta; break;
        default: return;
      }
      normalizeCueCrop(cue);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void setSelectedChromaKeyEnabled(bool enabled) {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return cueSupportsKeying(&each);
    });
    if (!cue) {
      return;
    }
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsKeying(&each)) {
        return;
      }
      if (each.chromaKeyEnabled == enabled) {
        return;
      }
      each.chromaKeyEnabled = enabled;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast(enabled ? "key on" : "key off");
    markProjectDirty();
  }

  // Datamosh needs a PREPARED copy (Encoder tab -> Datamosh preset): a normal
  // H.264 file with B-frames smears badly and an all-intra one cannot smear at
  // all. Rather than silently doing nothing, say which state the cue is in.
  // Toggling ON prepares the cue automatically (the owner, 2026-08-20). It does NOT
  // block: datamoshEnabled is set immediately, and datamoshActiveForCue()
  // additionally requires the prepared file to exist -- so the cue keeps playing
  // the original until the transcode lands, then picks up the moshed copy on the
  // next load. No half-written file is ever taken.
  // ---- Cue markers ---------------------------------------------------------
  // Named jump marks inside a clip. Distinct from pause points, which stop
  // playback; a marker is somewhere you jump TO. For a long clip an operator
  // wants "verse 2", not a scrub bar.

  // Drop a marker at the live position. Named by index unless the operator
  // renames it, because being made to type mid-show is worse than a dull name.
  void addMarkerAtPlayhead() {
    Cue* cue = selectedCueMutable();
    MediaEngine* engine = focusedMediaEngine();
    if (!cue || !engine) {
      failRemoteCommand("marker: select a cue");
      return;
    }
    const double at = std::max(0.0, engine->position());
    // Keep sorted on insert so nextMarker/prevMarker stay a linear scan.
    auto it = std::lower_bound(cue->markerSeconds.begin(), cue->markerSeconds.end(), at);
    const std::size_t idx = static_cast<std::size_t>(it - cue->markerSeconds.begin());
    cue->markerSeconds.insert(it, at);
    cue->markerNames.insert(cue->markerNames.begin() + static_cast<std::ptrdiff_t>(idx),
                            "mark " + std::to_string(cue->markerSeconds.size()));
    triggerToast("marker at " + formatSeconds(at));
    playUiSound(UiSoundEffect::Toggle);
    showLog("MARKER-ADD", showLogCueRef(project_.focusedDeckIndex,
                                        focusedDeck().selectedIndex) +
                          " @" + formatSeconds(at));
    markProjectDirty();
  }

  void clearMarkers() {
    Cue* cue = selectedCueMutable();
    if (!cue) { failRemoteCommand("marker: select a cue"); return; }
    cue->markerSeconds.clear();
    cue->markerNames.clear();
    triggerToast("markers cleared");
    markProjectDirty();
  }

  // Jump to the next/previous marker on the LIVE cue. Seeks rather than takes,
  // so the cue stays on air and the picture does not blink.
  void jumpToMarker(int direction) {
    Deck& deck = focusedDeckMutable();
    MediaEngine* engine = focusedMediaEngine();
    if (!engine || deck.activeIndex < 0 ||
        deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      failRemoteCommand("marker: nothing live");
      return;
    }
    const Cue& cue = deck.cues[deck.activeIndex];
    if (cue.markerSeconds.empty()) {
      failRemoteCommand("marker: none on this cue");
      return;
    }
    const double now = engine->position();
    // 0.75s of slack going backwards, so pressing prev just after a marker
    // returns to THAT marker rather than skipping to the one before it -- the
    // same convention as track-skip on a CD player, and what a hand expects.
    double target = -1.0;
    if (direction > 0) {
      for (double m : cue.markerSeconds) {
        if (m > now + 0.05) { target = m; break; }
      }
    } else {
      for (auto it = cue.markerSeconds.rbegin(); it != cue.markerSeconds.rend(); ++it) {
        if (*it < now - 0.75) { target = *it; break; }
      }
      if (target < 0.0 && !cue.markerSeconds.empty()) {
        target = cue.markerSeconds.front();
      }
    }
    if (target < 0.0) {
      triggerToast(direction > 0 ? "no marker ahead" : "no marker behind");
      return;
    }
    engine->seek(target);
    triggerToast("marker " + formatSeconds(target));
    playUiSound(UiSoundEffect::Navigate);
  }

  // ---- Scheduled start -----------------------------------------------------
  // Fires a cue at a wall-clock time, with no external timecode source. That
  // is the difference from the existing trigger-timecode path and the whole
  // point: it makes unattended playback possible.
  void processScheduledStarts() {
    const std::time_t now = std::time(nullptr);
    std::tm lt {};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    const double secondsToday = lt.tm_hour * 3600.0 + lt.tm_min * 60.0 + lt.tm_sec;

    // Midnight rollover: clear the fired latches so a daily schedule repeats.
    if (secondsToday < lastScheduleCheckSeconds_) {
      for (Deck& deck : project_.decks) {
        for (Cue& cue : deck.cues) {
          cue.scheduledStartFired = false;
        }
      }
      showLog("SCHEDULE", "midnight rollover, daily schedule re-armed");
    }

    for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
      Deck& deck = project_.decks[d];
      for (int c = 0; c < static_cast<int>(deck.cues.size()); ++c) {
        Cue& cue = deck.cues[c];
        if (cue.scheduledStartSeconds < 0.0 || cue.scheduledStartFired) {
          continue;
        }
        // Edge-triggered on crossing the time, not "is it now": a tick can be
        // late, and a schedule that only fires on an exact match would be
        // skipped entirely by one long frame.
        if (lastScheduleCheckSeconds_ < cue.scheduledStartSeconds &&
            secondsToday >= cue.scheduledStartSeconds) {
          cue.scheduledStartFired = true;
          showLog("SCHEDULED-TAKE", showLogCueRef(d, c));
          const int savedDeck = project_.focusedDeckIndex;
          project_.focusedDeckIndex = d;
          deck.selectedIndex = c;
          takeSelected(true);
          project_.focusedDeckIndex = savedDeck;
        }
      }
    }
    lastScheduleCheckSeconds_ = secondsToday;
  }

  // ---- Stage timer controls -----------------------------------------------
  // These drive the timer WITHOUT touching transport, so the clock can be
  // started, held, reset or nudged while the cue stays live on the stage
  // screen. Anything that takes the cue off air to change the time is wrong.

  TimerRuntime& timerRuntimeFor(const Cue& cue) {
    return timerRuntimes_[cue.id];
  }

  // The timer the operator means: the LIVE one if a timer is on air, otherwise
  // the selected one. Mid-show the live clock is almost always the intent.
  Cue* activeTimerCue() {
    Deck& deck = focusedDeckMutable();
    if (deck.activeIndex >= 0 && deck.activeIndex < static_cast<int>(deck.cues.size()) &&
        deck.cues[deck.activeIndex].kind == CueKind::Timer) {
      return &deck.cues[deck.activeIndex];
    }
    Cue* sel = selectedCueMutable();
    return (sel && sel->kind == CueKind::Timer) ? sel : nullptr;
  }

  // Drop runtime state for timer cues that no longer exist. Without this the
  // map grows for the life of the session as cues are added and deleted, and a
  // recreated cue could inherit a dead clock. Cheap: only runs when the count
  // looks stale, and shows have tens of cues, not thousands.
  void pruneTimerRuntimes() {
    if (timerRuntimes_.empty()) {
      return;
    }
    std::set<std::string> live;
    for (const Deck& deck : project_.decks) {
      for (const Cue& cue : deck.cues) {
        if (cue.kind == CueKind::Timer) {
          live.insert(cue.id);
        }
      }
    }
    for (auto it = timerRuntimes_.begin(); it != timerRuntimes_.end();) {
      it = live.count(it->first) ? std::next(it) : timerRuntimes_.erase(it);
    }
  }

  void advanceTimerRuntimes(Uint64 nowMs) {
    for (auto& [id, rt] : timerRuntimes_) {
      if (!rt.running) {
        rt.lastTickMs = nowMs;
        continue;
      }
      if (rt.lastTickMs == 0) {
        rt.lastTickMs = nowMs;
        continue;
      }
      // Wall-clock delta, not a fixed per-frame increment: a dropped frame or a
      // busy render loop must not make the countdown drift slow.
      rt.elapsedSeconds += static_cast<double>(nowMs - rt.lastTickMs) / 1000.0;
      rt.lastTickMs = nowMs;
    }
  }

  // Edit a timer setting on the SELECTED cue (not the live one): these are
  // authoring changes, unlike the run/reset/nudge controls which target
  // whatever clock is on air.
  void adjustTimerField(int TimerSettings::*field, int delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Timer) {
      return;
    }
    int& v = cue->timer.*field;
    v = std::clamp(v + delta, 0, 24 * 3600);
    // Thresholds must stay ordered or the colour logic silently never fires.
    cue->timer.amberSeconds = std::min(cue->timer.amberSeconds, cue->timer.durationSeconds);
    cue->timer.redSeconds = std::min(cue->timer.redSeconds, cue->timer.amberSeconds);
    markProjectDirty();
  }

  void cycleTimerMode() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Timer) return;
    cue->timer.mode = cue->timer.mode == TimerMode::Countdown ? TimerMode::CountUp
                    : cue->timer.mode == TimerMode::CountUp   ? TimerMode::TimeOfDay
                                                              : TimerMode::Countdown;
    triggerToast(cue->timer.mode == TimerMode::CountUp ? "timer: count up"
                 : cue->timer.mode == TimerMode::TimeOfDay ? "timer: time of day"
                                                           : "timer: countdown");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void cycleTimerFace() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Timer) return;
    cue->timer.face = cue->timer.face == TimerFace::SevenSegment ? TimerFace::Blocky
                                                                 : TimerFace::SevenSegment;
    triggerToast(cue->timer.face == TimerFace::Blocky ? "timer face: blocky"
                                                      : "timer face: 7-segment");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleTimerCountUp() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Timer) return;
    cue->timer.countUpAfterZero = !cue->timer.countUpAfterZero;
    triggerToast(cue->timer.countUpAfterZero ? "overtime: counts up" : "overtime: stops at 0");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void timerToggleRun() {
    Cue* cue = activeTimerCue();
    if (!cue) { failRemoteCommand("timer: no timer cue"); return; }
    TimerRuntime& rt = timerRuntimeFor(*cue);
    rt.running = !rt.running;
    rt.lastTickMs = SDL_GetTicks();
    triggerToast(rt.running ? "timer running" : "timer held");
    playUiSound(UiSoundEffect::Toggle);
  }

  void timerReset() {
    Cue* cue = activeTimerCue();
    if (!cue) { failRemoteCommand("timer: no timer cue"); return; }
    TimerRuntime& rt = timerRuntimeFor(*cue);
    rt.elapsedSeconds = 0.0;
    rt.lastTickMs = SDL_GetTicks();
    triggerToast("timer reset");
    playUiSound(UiSoundEffect::Stop);
  }

  // Nudge the clock. Positive adds TIME REMAINING (so it counts elapsed DOWN),
  // which is what an operator means by "+1 minute" -- they are giving the
  // speaker another minute, not aging the clock.
  void timerNudge(double seconds) {
    Cue* cue = activeTimerCue();
    if (!cue) { failRemoteCommand("timer: no timer cue"); return; }
    TimerRuntime& rt = timerRuntimeFor(*cue);
    rt.elapsedSeconds = std::max(0.0, rt.elapsedSeconds - seconds);
    const int mins = static_cast<int>(std::abs(seconds)) / 60;
    triggerToast(std::string(seconds >= 0 ? "+" : "-") +
                 (mins > 0 ? std::to_string(mins) + " min" : std::to_string(static_cast<int>(std::abs(seconds))) + " sec"));
    playUiSound(UiSoundEffect::Navigate);
  }

  // Jump to a specific REMAINING time.
  void timerSetRemaining(double remainingSeconds) {
    Cue* cue = activeTimerCue();
    if (!cue) { failRemoteCommand("timer: no timer cue"); return; }
    TimerRuntime& rt = timerRuntimeFor(*cue);
    rt.elapsedSeconds =
      std::max(0.0, static_cast<double>(cue->timer.durationSeconds) - remainingSeconds);
    rt.lastTickMs = SDL_GetTicks();
    triggerToast("timer set");
    playUiSound(UiSoundEffect::Navigate);
  }

  void toggleSelectedDatamosh() {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      failRemoteCommand("datamosh: select a cue");
      return;
    }
    if (cue->kind != CueKind::Video || cue->path.empty()) {
      failRemoteCommand("datamosh: file-backed video cues only");
      playUiSound(UiSoundEffect::Error);
      return;
    }
    if (cue->datamoshEnabled) {
      cue->datamoshEnabled = false;
      triggerToast("datamosh off");
      playUiSound(UiSoundEffect::Toggle);
      refreshFocusedLiveCueRuntimeIfSelected();  // swaps back to the original
      markProjectDirty();
      return;
    }

    std::error_code ec;
    const bool prepared =
      !cue->moshPath.empty() && fs::exists(fs::path(cue->moshPath), ec);
    cue->datamoshEnabled = true;
    markProjectDirty();

    if (prepared) {
      triggerToast("DATAMOSH on");
      playUiSound(UiSoundEffect::Toggle);
      refreshFocusedLiveCueRuntimeIfSelected();
      return;
    }
    if (datamoshPrepInFlight(cue->path)) {
      triggerToast("datamosh: already preparing");
      return;
    }
    // A long source is minutes of encoding. Say so rather than appear to hang;
    // the operator can watch it in Settings > Encoder.
    const int deckIndex = project_.focusedDeckIndex;
    const int cueIndex = focusedDeck().selectedIndex;
    queueDatamoshPrepForCue(deckIndex, cueIndex);
    triggerToast(cue->duration > 120.0
                   ? "DATAMOSH on - preparing (long clip, watch Encoder tab)"
                   : "DATAMOSH on - preparing...");
    playUiSound(UiSoundEffect::Toggle);
  }
  // Change which mosh recipe this cue uses. The prepared file was encoded with
  // the OLD recipe, so it is stale the moment the look changes: drop it and
  // re-prepare, otherwise the label would claim EXTREME while the cue kept
  // playing the gentle version -- a control that reads as wired but is not.
  void cycleSelectedDatamoshLook(int delta) {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      failRemoteCommand("datamosh look: select a cue");
      return;
    }
    if (cue->kind != CueKind::Video || cue->path.empty()) {
      failRemoteCommand("datamosh look: file-backed video cues only");
      playUiSound(UiSoundEffect::Error);
      return;
    }
    const int previous = cue->datamoshLook;
    int next = previous + delta;
    while (next < 0) next += kDatamoshLookCount;
    next %= kDatamoshLookCount;
    if (next == previous) return;
    cue->datamoshLook = next;
    cue->moshPath.clear();
    markProjectDirty();
    playUiSound(UiSoundEffect::Toggle);

    if (!cue->datamoshEnabled) {
      // Not on yet, so nothing to re-encode: the new look is simply what the
      // toggle will prepare when it is switched on.
      triggerToast(std::string("datamosh look: ") + moshLookLabelFor(next));
      return;
    }
    // Live cue is now playing the original again until the new prep lands.
    refreshFocusedLiveCueRuntimeIfSelected();
    if (datamoshPrepInFlight(cue->path)) {
      triggerToast(std::string("datamosh look: ") + moshLookLabelFor(next) +
                   " - already preparing");
      return;
    }
    queueDatamoshPrepForCue(project_.focusedDeckIndex, focusedDeck().selectedIndex);
    triggerToast(std::string("datamosh look: ") + moshLookLabelFor(next) +
                 " - re-preparing...");
  }

  void toggleSelectedChromaKey() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return cueSupportsKeying(&each);
    });
    if (!cue) {
      return;
    }
    setSelectedChromaKeyEnabled(!cue->chromaKeyEnabled);
  }

  void adjustSelectedKeyTolerance(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsKeying(&cue)) {
        return;
      }
      cue.chromaKeyTolerance = std::clamp(cue.chromaKeyTolerance + delta, 0.0f, 441.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedKeySoftness(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsKeying(&cue)) {
        return;
      }
      cue.chromaKeySoftness = std::clamp(cue.chromaKeySoftness + delta, 0.0f, 200.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void editSelectedKeyColor() {
    openInlineKeyColorEditor();
  }

  void adjustSelectedBrightness(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsColorControls(&cue)) {
        return;
      }
      cue.brightness = std::clamp(cue.brightness + delta, 0.0f, 2.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedContrast(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsColorControls(&cue)) {
        return;
      }
      cue.contrast = std::clamp(cue.contrast + delta, 0.0f, 2.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedSaturation(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsColorControls(&cue)) {
        return;
      }
      cue.saturation = std::clamp(cue.saturation + delta, 0.0f, 2.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedHueShift(float deltaDegrees) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsColorControls(&cue)) {
        return;
      }
      cue.hueShift = std::clamp(cue.hueShift + deltaDegrees, -180.0f, 180.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedScaleX(float delta) {
    bool link = project_.geometryAspectLinked;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      float oldX = cue.outputScaleX;
      cue.outputScaleX = std::clamp(oldX + delta, 0.25f, 4.0f);
      if (link && oldX > 0.0001f) {
        cue.outputScaleY = std::clamp(cue.outputScaleY * (cue.outputScaleX / oldX), 0.25f, 4.0f);
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedScaleY(float delta) {
    bool link = project_.geometryAspectLinked;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      float oldY = cue.outputScaleY;
      cue.outputScaleY = std::clamp(oldY + delta, 0.25f, 4.0f);
      if (link && oldY > 0.0001f) {
        cue.outputScaleX = std::clamp(cue.outputScaleX * (cue.outputScaleY / oldY), 0.25f, 4.0f);
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedOffsetX(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      cue.outputOffsetX += delta;
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedOffsetY(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      cue.outputOffsetY += delta;
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void cycleSelectedScaleMode() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return cueSupportsGeometry(&each);
    });
    if (!cue) {
      return;
    }
    ScaleMode next = static_cast<ScaleMode>((static_cast<int>(cue->scaleMode) + 1) % 4);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsGeometry(&each)) {
        return;
      }
      each.scaleMode = next;
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void setSelectedSourceCueRef(const std::string& rawRef) {
    Cue* selected = selectedCueMutable();
    if (!selected || !isSourceCueKind(selected->kind)) {
      return;
    }
    std::string typedRef = trim(rawRef);
    std::string selectedSourceRef = sourceCueRefFromAlias(selected->kind, typedRef);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!isSourceCueKind(cue.kind)) {
        return;
      }
      std::string sourceRef = sourceCueRefFromAlias(cue.kind, typedRef);
      cue.path = "source://" + sourceCueTokenForKind(cue.kind) + "/" + sourceRef;
      std::string prefix = cueKindLabel(cue.kind) + " · ";
      if (cue.name.empty() || cue.name.rfind(prefix, 0) == 0) {
        cue.name = prefix + sourceRef;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    Deck& deck = focusedDeckMutable();
    if (deck.activeIndex >= 0 && deck.activeIndex == deck.selectedIndex) {
      if (MediaEngine* engine = focusedMediaEngine()) {
        bool autoplay = engine->state() == TransportState::Playing;
        Cue& activeCue = deck.cues[deck.activeIndex];
        engine->loadCue(&activeCue, autoplay);
      }
    }
    triggerToast("source: " + sourceCueRefFriendlyLabel(selected->kind, selectedSourceRef));
    markProjectDirty();
  }

  void setSelectedSourceCueKind(CueKind nextKind) {
    Cue* selected = selectedCueMutable();
    if (!selected || !isSourceCueKind(selected->kind) || !isSourceCueKind(nextKind)) {
      return;
    }
    CueKind previousKind = selected->kind;
    if (previousKind == nextKind) {
      return;
    }
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!isSourceCueKind(cue.kind)) {
        return;
      }
      std::string currentRef = sourceCueRefFromCue(cue);
      std::string nextRef = sourceCueRefFromAlias(nextKind,
        currentRef.empty() ? defaultSourceRefForKind(nextKind) : currentRef);
      std::string oldPrefix = cueKindLabel(cue.kind) + " · ";
      cue.kind = nextKind;
      cue.path = "source://" + sourceCueTokenForKind(nextKind) + "/" + nextRef;
      cue.videoCodec = sourceCueTokenForKind(nextKind);
      if (cue.name.empty() || cue.name.rfind(oldPrefix, 0) == 0) {
        cue.name = cueKindLabel(nextKind) + " · " + nextRef;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    Deck& deck = focusedDeckMutable();
    if (deck.activeIndex >= 0 && deck.activeIndex == deck.selectedIndex) {
      if (MediaEngine* engine = focusedMediaEngine()) {
        bool autoplay = engine->state() == TransportState::Playing;
        Cue& activeCue = deck.cues[deck.activeIndex];
        engine->loadCue(&activeCue, autoplay);
      }
    }
    triggerToast("source type: " + sourceCueLabelForType(sourceCueTokenForKind(nextKind)));
    markProjectDirty();
  }

  void setSelectedBrowserCueUrl(const std::string& rawUrl) {
    Cue* selected = selectedCueMutable();
    if (!selected || selected->kind != CueKind::Browser) {
      return;
    }
    std::string normalizedUrl = normalizeBrowserUrl(rawUrl);
    if (normalizedUrl.empty()) {
      triggerToast("browser url required");
      return;
    }
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Browser) {
        return;
      }
      cue.path = normalizedUrl;
      if (cue.name.empty() || cue.name == "Browser Cue" || cue.name.rfind("Browser:", 0) == 0) {
        cue.name = browserCueNameForUrl(normalizedUrl);
      }
      changed = true;
    });
    if (!changed) {
      return;
    }

    Deck& deck = focusedDeckMutable();
    bool selectedIsActiveBrowser =
      deck.activeIndex >= 0 &&
      deck.activeIndex == deck.selectedIndex &&
      deck.activeIndex < static_cast<int>(deck.cues.size()) &&
      deck.cues[deck.activeIndex].kind == CueKind::Browser;
    if (selectedIsActiveBrowser) {
      bool shouldAutoplay = false;
      if (const DeckRuntime* runtime = focusedRuntime()) {
        shouldAutoplay = runtime->browserCueLive;
      }
      if (const MediaEngine* engine = focusedMediaEngine()) {
        shouldAutoplay = shouldAutoplay || engine->state() == TransportState::Playing;
      }
      takeSelected(shouldAutoplay);
    }
    triggerToast("browser url updated");
    markProjectDirty();
  }
