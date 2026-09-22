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
    } else if (label == "BLACK" || label == "BLACKOUT") {   // old label still accepted
      // Instant and reversible: kills the picture without touching playback,
      // so the show keeps running underneath and one press brings it back.
      const bool dark = masterDimmerTarget_ < 0.5;
      masterDimmerTarget_ = dark ? 1.0 : 0.0;
      triggerToast(dark ? "blackout off" : "BLACKOUT");
      playUiSound(dark ? UiSoundEffect::Toggle : UiSoundEffect::Clear);
    } else if (label == "RECORD") {
      toggleRecording();
    } else if (label == "MENU" || label == "SETUP" || label == "SETTINGS") {   // old labels still accepted
      settingsOpen_ = true;
      // SYSTEM, which is what the tab strip starts on and what the field
      // defaults to. This forced tab 3 (Video Outputs), so the button always
      // opened on the fourth tab regardless of what the operator last used or
      // what they pressed the button to do.
      settingsTab_ = 0;
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
    // THE CLICKER SPENDS THE BUILDS FIRST. A presenter set up this way expects
    // the same thing every other deck gives them: one press reveals the next
    // part of what they are saying, and only the press after the last part
    // changes the slide. Off unless a presenter view asked for it, so a show
    // that does not use notes sees no change at all.
    if (presenterAdvanceSpendsBuild(deckIndex)) {
      presenterNoteStepAdvance(deckIndex, 1);
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
    // Back through the builds before going back a slide, so the two directions
    // are each other's opposite. Tested on the step rather than on
    // presenterAdvanceSpendsBuild, which asks whether there is anything left
    // to REVEAL -- here the question is whether there is anything to take back.
    if (presenterNoteStepFor(deckIndex) > 0 &&
        presenterAdvanceSpendsBuildBackwards(deckIndex)) {
      presenterNoteStepAdvance(deckIndex, -1);
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
    // STOP is a decision. Anything this deck was about to take -- a pre-wait
    // counting down, a continue waiting -- dies with it, or it fires seconds
    // later on top of an operator who thought they had stopped the show.
    cancelPendingTake(project_.focusedDeckIndex);
    // And anything this deck was fading. A fade that lands seconds after the
    // operator stopped the show is the same fault as a pending take that
    // fires after it -- see cancelPendingTake, directly above.
    cancelFadesForDeck(project_.focusedDeckIndex);
    // And the audition, if this was the deck being auditioned. Stopping is how
    // an operator finishes looking at something; leaving the deck held off the
    // outputs after it would silently black their screen for the next cue.
    if (deckIsAuditioning(project_.focusedDeckIndex)) {
      endAudition(false);
    }
    clearPreload();
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

  // ── STANDBY ───────────────────────────────────────────────────────────
  //
  // The cue GO will fire, kept apart from the selection. See Deck::standbyIndex
  // for why the two are different things.

  // Clamp to something real. A standby can be orphaned by a delete, a reorder
  // or a show that arrived with a stale index, and a pointer into nothing is
  // worse than none -- GO would silently do nothing on a show day.
  int standbyIndexFor(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return -1;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.standbyIndex < 0 ||
        deck.standbyIndex >= static_cast<int>(deck.cues.size())) {
      return -1;
    }
    return deck.standbyIndex;
  }

  void setStandbyIndex(int deckIndex, int cueIndex, bool announce = true) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      deck.standbyIndex = -1;
      if (announce) {
        triggerToast("standby cleared");
      }
      markProjectDirty();
      return;
    }
    deck.standbyIndex = cueIndex;
    scrollDeckToCueIndex(deckIndex, cueIndex, true);
    if (announce) {
      triggerToast("standby: " + cueDisplayToken(deck.cues[cueIndex], cueIndex) +
                   "  " + deck.cues[cueIndex].name);
    }
    markProjectDirty();
  }

  // Arm the cue the operator is looking at.
  void setStandbyToSelected() {
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0) {
      triggerToast("standby: select a cue first");
      return;
    }
    setStandbyIndex(deckIndex, deck.selectedIndex);
  }

  // After firing, the standby steps to the next cue -- that is the whole point
  // of a running order. Off the end it clears rather than wrapping: a list that
  // silently returns to the top is how a show restarts itself on the last GO.
  void advanceStandby(int deckIndex) {
    const int current = standbyIndexFor(deckIndex);
    if (current < 0) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    // A DISARMED CUE IS STEPPED OVER, not stood by. That is the whole reason
    // the flag exists: an operator disarms a cue so the running order flows
    // past it without them having to remember it is there. Standing by on one
    // would make every GO need a second GO.
    int next = current + 1;
    while (next < static_cast<int>(deck.cues.size()) && !deck.cues[next].armed) {
      ++next;
    }
    deck.standbyIndex = (next < static_cast<int>(deck.cues.size())) ? next : -1;
    if (deck.standbyIndex >= 0) {
      scrollDeckToCueIndex(deckIndex, deck.standbyIndex, true);
    }
    markProjectDirty();
  }

  void toggleTransport() {
    MediaEngine* engine = focusedMediaEngine();
    DeckRuntime* runtime = focusedRuntime();
    const Cue* activeCue = activeCuePtr();
    // AN ARMED STANDBY OWNS GO -- whatever is playing.
    //
    // This check has to come BEFORE the active-cue branch below, not after.
    // Behind it, GO only reached the standby when nothing was live, so it
    // fired once at the top of a session and then went back to being
    // play/pause forever: a running order that runs one cue. Walking the list
    // IS the feature, so while a standby is armed, GO walks it.
    //
    // With none armed nothing below changes, which is the whole compatibility
    // story: every existing show opens with -1 and keeps select-and-take.
    const int deckIndex = project_.focusedDeckIndex;
    const int standby = standbyIndexFor(deckIndex);
    if (standby >= 0) {
      selectCueInDeck(deckIndex, standby, false, false);
      takeSelected(true);
      advanceStandby(deckIndex);
      return;
    }
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

  // The next cue in the running order, by the same rules a manual skip uses --
  // goto targets, shuffle, and walking past cues whose media has vanished.
  int continueTargetIndex(Deck& deck, const Cue& from) {
    return resolveAutoAdvanceIndex(deck, from, true);
  }

  // Arm the continue that counts from a cue STARTING. Does nothing unless the
  // cue asked for AutoContinue, so a show with no continues behaves exactly as
  // it always has.
  void scheduleContinueAfterStart(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    const Cue cue = deck.cues[cueIndex];   // copied: resolve may reorder nothing,
                                           // but the reference outliving a take
                                           // is not worth risking
    if (cue.continueMode != CueContinueMode::AutoContinue) {
      return;
    }
    const int next = continueTargetIndex(deck, cue);
    if (next < 0 || next == cueIndex) {
      return;                            // nowhere to go, or it would re-fire itself
    }
    schedulePendingTake(deckIndex, next, cue.postWaitSeconds,
                        cue.transitionToNext, "continue");
  }

  // Nudge a wait on the selected cue. Clamped at zero: a negative pre-wait
  // would ask for a cue to start before its own GO.
  void nudgeSelectedWait(bool preWait, double delta) {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      triggerToast("select a cue first");
      return;
    }
    double& field = preWait ? cue->preWaitSeconds : cue->postWaitSeconds;
    field = std::max(0.0, field + delta);
    markProjectDirty();
    triggerToast(std::string(preWait ? "pre-wait " : "post-wait ") +
                 formatSeconds(field));
  }

  void cycleSelectedContinueMode() {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      triggerToast("select a cue first");
      return;
    }
    switch (cue->continueMode) {
      case CueContinueMode::DoNotContinue:
        cue->continueMode = CueContinueMode::AutoContinue; break;
      case CueContinueMode::AutoContinue:
        cue->continueMode = CueContinueMode::AutoFollow; break;
      case CueContinueMode::AutoFollow:
      default:
        cue->continueMode = CueContinueMode::DoNotContinue; break;
    }
    markProjectDirty();
    triggerToast(std::string("continue: ") + cueContinueModeToken(cue->continueMode));
  }

  // ── THE SEQUENCING SPINE'S ONE PIECE OF RUNTIME STATE ──────────────────
  //
  // A cue that has been fired but has not started yet: a pre-wait counting
  // down, or a continue waiting on its post-wait. One per deck, because a deck
  // can only be about to play one thing.
  //
  // NOT saved. A show reopened must never come back mid-countdown with a cue
  // about to fire on its own -- the operator did not press anything.
  //
  // Declared here rather than beside the other members because this file is
  // included into the class body ABOVE them, and the type has to exist before
  // the functions below name it.
  struct PendingTake {
    bool armed = false;
    int cueIndex = -1;
    double dueAtSeconds = 0.0;
    bool useTransition = true;
    std::string reason;        // "pre-wait" / "continue", for the toast
  };

  // ── PENDING TAKES: pre-waits and continues ────────────────────────────
  //
  // Everything in the sequencing spine that does not happen immediately goes
  // through here. A cue with a pre-wait is fired, then waits. A cue with a
  // continue schedules the NEXT one. Both are "a deck is about to take
  // something", which is one fact per deck.
  //
  // ANY MANUAL ACTION ON A DECK CANCELS ITS PENDING TAKE. An operator who hits
  // STOP has decided; a cue that fires two seconds later because a countdown
  // nobody could see was still running is the exact failure that makes people
  // distrust an auto-follow.
  double nowSeconds() const {
    return static_cast<double>(SDL_GetTicks()) / 1000.0;
  }

  PendingTake& pendingTakeFor(int deckIndex) {
    if (static_cast<int>(deckPendingTakes_.size()) <= deckIndex) {
      deckPendingTakes_.resize(deckIndex + 1);
    }
    return deckPendingTakes_[deckIndex];
  }

  void cancelPendingTake(int deckIndex, const char* why = nullptr) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(deckPendingTakes_.size())) {
      return;
    }
    PendingTake& p = deckPendingTakes_[deckIndex];
    if (!p.armed) {
      return;
    }
    p = PendingTake {};
    if (why) {
      triggerToast(std::string("cancelled: ") + why);
    }
  }

  void cancelAllPendingTakes() {
    for (auto& p : deckPendingTakes_) {
      p = PendingTake {};
    }
  }

  void schedulePendingTake(int deckIndex, int cueIndex, double delaySeconds,
                           bool useTransition, const char* reason) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    if (cueIndex < 0 || cueIndex >= static_cast<int>(project_.decks[deckIndex].cues.size())) {
      return;
    }
    PendingTake& p = pendingTakeFor(deckIndex);
    p.armed = true;
    p.cueIndex = cueIndex;
    p.dueAtSeconds = nowSeconds() + std::max(0.0, delaySeconds);
    p.useTransition = useTransition;
    p.reason = reason ? reason : "";
  }

  // How long until a deck's pending take fires, or -1 when nothing is pending.
  // The UI wants this for a countdown; the remote wants it to report.
  double pendingTakeRemaining(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(deckPendingTakes_.size())) {
      return -1.0;
    }
    const PendingTake& p = deckPendingTakes_[deckIndex];
    return p.armed ? std::max(0.0, p.dueAtSeconds - nowSeconds()) : -1.0;
  }

  void servicePendingTakes() {
    const double now = nowSeconds();
    for (int deckIndex = 0; deckIndex < static_cast<int>(deckPendingTakes_.size()); ++deckIndex) {
      PendingTake& p = deckPendingTakes_[deckIndex];
      if (!p.armed || now < p.dueAtSeconds) {
        continue;
      }
      if (deckIndex >= static_cast<int>(project_.decks.size())) {
        p = PendingTake {};
        continue;
      }
      const int cueIndex = p.cueIndex;
      const bool useTransition = p.useTransition;
      // Disarm BEFORE taking: takeSelected can schedule the next continue, and
      // an armed slot underneath it would be overwritten or, worse, re-fire.
      p = PendingTake {};
      Deck& deck = project_.decks[deckIndex];
      if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
        continue;                       // deleted while it waited
      }
      const int savedFocus = project_.focusedDeckIndex;
      project_.focusedDeckIndex = deckIndex;
      selectCueInDeck(deckIndex, cueIndex, false, false);
      // honourPreWait=false: the wait has already been served.
      takeSelected(true, useTransition, false, false);
      project_.focusedDeckIndex = savedFocus;
    }
  }

  // ── MASTER CUES ───────────────────────────────────────────────────────
  //
  // A master cue is an Analog Way LiveCore MASTER MEMORY: it recalls one cue on
  // each of several decks at once. It carries no media and is never taken on
  // its own deck's engine -- firing it means taking OTHER decks.
  //
  // It stores assignments and nothing else. Whether a master "is live" is
  // DERIVED by asking the decks it names, never remembered here: two sources of
  // truth drift the first time somebody takes a cue on a target deck by hand,
  // and then nothing can say which is right.
  int findCueIndexById(int deckIndex, const std::string& cueId) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return -1;
    }
    const Deck& deck = project_.decks[deckIndex];
    for (int i = 0; i < static_cast<int>(deck.cues.size()); ++i) {
      if (deck.cues[i].id == cueId) {
        return i;
      }
    }
    return -1;
  }

  // Is every deck this master names currently holding the cue it assigned?
  // Asked at render time; nothing caches it.
  bool masterCueIsLive(const Cue& master) const {
    bool any = false;
    for (const auto& a : master.masterAssignments) {
      if (a.bypassed) {
        continue;
      }
      const int idx = findCueIndexById(a.deckIndex, a.cueId);
      if (idx < 0) {
        return false;                       // dangling: cannot be live
      }
      if (project_.decks[a.deckIndex].activeIndex != idx) {
        return false;
      }
      any = true;
    }
    return any;
  }

  // Which cue of `targetDeck` this master currently assigns, or -1 for none.
  int masterAssignedIndex(const Cue& master, int targetDeck) const {
    for (const auto& a : master.masterAssignments) {
      if (a.deckIndex == targetDeck) {
        return findCueIndexById(targetDeck, a.cueId);
      }
    }
    return -1;
  }

  // Does this master name that deck at all? Distinct from whether the cue it
  // names still exists -- "none" and "UNRESOLVED" are different problems and
  // the operator has to be able to tell them apart.
  bool hasMasterAssignmentFor(const Cue& master, int targetDeck) const {
    for (const auto& a : master.masterAssignments) {
      if (a.deckIndex == targetDeck) {
        return true;
      }
    }
    return false;
  }

  bool masterAssignmentBypassed(const Cue& master, int targetDeck) const {
    for (const auto& a : master.masterAssignments) {
      if (a.deckIndex == targetDeck) {
        return a.bypassed;
      }
    }
    return false;
  }

  // Step which cue of a deck this master assigns. Walking off either end
  // CLEARS the assignment rather than wrapping: "this master does not touch
  // deck 3" has to be reachable with the same control that set it, or the only
  // way to undo an assignment is to know a remote verb exists.
  void stepMasterAssignment(int targetDeck, int delta) {
    Cue* master = selectedCueMutable();
    if (!master || master->kind != CueKind::Master) {
      return;
    }
    if (targetDeck < 0 || targetDeck >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Deck& target = project_.decks[targetDeck];
    if (target.cues.empty()) {
      triggerToast("deck " + std::to_string(targetDeck + 1) + " has no cues");
      return;
    }
    const int current = masterAssignedIndex(*master, targetDeck);
    const int next = current + delta;      // -1 + 1 == 0, so "none" steps to the first
    if (next < 0 || next >= static_cast<int>(target.cues.size())) {
      clearMasterAssignment(targetDeck);
      return;
    }
    const std::string id = target.cues[next].id;
    for (auto& a : master->masterAssignments) {
      if (a.deckIndex == targetDeck) {
        a.cueId = id;
        markProjectDirty();
        return;
      }
    }
    MasterAssignment added;
    added.deckIndex = targetDeck;
    added.cueId = id;
    master->masterAssignments.push_back(added);
    markProjectDirty();
  }

  void clearMasterAssignment(int targetDeck) {
    Cue* master = selectedCueMutable();
    if (!master || master->kind != CueKind::Master) {
      return;
    }
    auto& list = master->masterAssignments;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const MasterAssignment& a) {
                                return a.deckIndex == targetDeck;
                              }),
               list.end());
    markProjectDirty();
  }

  void toggleMasterBypass(int targetDeck) {
    Cue* master = selectedCueMutable();
    if (!master || master->kind != CueKind::Master) {
      return;
    }
    for (auto& a : master->masterAssignments) {
      if (a.deckIndex == targetDeck) {
        a.bypassed = !a.bypassed;
        markProjectDirty();
        triggerToast("deck " + std::to_string(targetDeck + 1) +
                     (a.bypassed ? ": bypassed" : ": active"));
        return;
      }
    }
    triggerToast("deck " + std::to_string(targetDeck + 1) + " is not assigned");
  }

  void fireMasterCue(int masterDeckIndex, int masterCueIndex) {
    if (masterDeckIndex < 0 || masterDeckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& masterDeck = project_.decks[masterDeckIndex];
    if (masterCueIndex < 0 || masterCueIndex >= static_cast<int>(masterDeck.cues.size())) {
      return;
    }
    // Copied, not referenced: taking on the target decks can reallocate any
    // deck's cue vector, and a reference into it would dangle mid-loop.
    const std::vector<MasterAssignment> plan = masterDeck.cues[masterCueIndex].masterAssignments;
    const std::string masterName = masterDeck.cues[masterCueIndex].name;

    const int savedFocus = project_.focusedDeckIndex;
    int fired = 0;
    int dangling = 0;
    for (const auto& a : plan) {
      if (a.bypassed) {
        continue;
      }
      if (a.deckIndex == masterDeckIndex) {
        continue;                           // a master deck does not fire itself
      }
      const int idx = findCueIndexById(a.deckIndex, a.cueId);
      if (idx < 0) {
        ++dangling;                         // deleted or reordered away
        continue;
      }
      // A master may not fire another master. Nesting is not the model here,
      // and without this a pair of masters pointing at each other recurses
      // until the stack gives out.
      if (project_.decks[a.deckIndex].cues[idx].kind == CueKind::Master) {
        ++dangling;
        continue;
      }
      // takeSelected acts on the FOCUSED deck, which is how every take in the
      // app reaches its guards -- media checks, the motion driver, the
      // feedback-loop reset. Borrowing focus per assignment runs the real take
      // path rather than a parallel one that would drift from it.
      project_.focusedDeckIndex = a.deckIndex;
      selectCueInDeck(a.deckIndex, idx, false, false);
      takeSelected(true);
      ++fired;
    }
    project_.focusedDeckIndex = savedFocus;

    // The master deck's own pointer, so the list shows which master was last
    // fired. This is the master deck's state, not a copy of the targets'.
    masterDeck.activeIndex = masterCueIndex;

    std::string msg = "master: " + masterName + " — " + std::to_string(fired) +
                      (fired == 1 ? " deck" : " decks");
    if (dangling > 0) {
      msg += ", " + std::to_string(dangling) + " unresolved";
    }
    triggerToast(msg);
    if (dangling > 0) {
      playUiSound(UiSoundEffect::Error);
    }
    markProjectDirty();
  }

  // ---------------------------------------------------------------------
  // Target cues: a cue that acts on another cue.
  //
  // Every verb here borrows focus and calls the ordinary operator path, for
  // the same reason fireMasterCue does. A Target cue must not be a second way
  // of stopping a deck -- it must be the SAME way, reached from the cue list.
  // ---------------------------------------------------------------------------

  // The deck index on a Target cue is a HINT, not the truth. Cue ids are unique
  // across the show, so a victim that was dragged to another deck is still
  // found rather than reported broken. The hint is only there to make the
  // common case a two-element scan instead of a whole-show one.
  bool resolveTargetCue(const Cue& target, int& outDeck, int& outIndex) const {
    if (target.targetCueId.empty()) {
      return false;
    }
    const int hinted = findCueIndexById(target.targetDeckIndex, target.targetCueId);
    if (hinted >= 0) {
      outDeck = target.targetDeckIndex;
      outIndex = hinted;
      return true;
    }
    for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
      const int idx = findCueIndexById(d, target.targetCueId);
      if (idx >= 0) {
        outDeck = d;
        outIndex = idx;
        return true;
      }
    }
    return false;
  }

  // A PANEL MAP IS A STATIC PATTERN, built once when the cue is taken. So a
  // tile-size edit has to ask for the rebuild itself: without this the control
  // moves a number in the inspector and the wall carries on showing the old
  // grid, which is the "control that does nothing" bug in its purest form.
  void nudgeLedPanelSize(bool width, int delta) {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    int& field = width ? cue->ledPanelWidth : cue->ledPanelHeight;
    field = std::clamp(field + delta, 16, 1024);
    markProjectDirty();
    triggerToast(std::string(width ? "tile width " : "tile height ") +
                 std::to_string(field));
    // Redraw it now if it is on air anywhere. Every deck is asked, because a
    // cue can be live on more than one.
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex >= 0 && deckIndex < static_cast<int>(project_.decks.size()) &&
        project_.decks[deckIndex].activeIndex == project_.decks[deckIndex].selectedIndex) {
      if (MediaEngine* engine = mediaEngineForDeck(deckIndex)) {
        engine->rebuildPatternFrame(*cue, static_cast<double>(SDL_GetTicks()) / 1000.0);
      }
    }
  }

  // ── Fade cue edits ────────────────────────────────────────────────────
  //
  // Every one of these guards on the KIND. A fade control that quietly edited
  // a video cue's fields would be invisible until the show file came back
  // carrying a ramp nobody put there.
  Cue* selectedFadeCue() {
    Cue* cue = selectedCueMutable();
    return (cue && cue->kind == CueKind::Fade) ? cue : nullptr;
  }

  void cycleFadeWhat() {
    Cue* cue = selectedFadeCue();
    if (!cue) {
      return;
    }
    cue->fadeWhat = cue->fadeWhat == CueFadeWhat::DeckOpacity  ? CueFadeWhat::DeckVolume
                  : cue->fadeWhat == CueFadeWhat::DeckVolume   ? CueFadeWhat::MasterDimmer
                                                               : CueFadeWhat::DeckOpacity;
    markProjectDirty();
    triggerToast(cueFadeWhatLabel(cue->fadeWhat));
  }

  void cycleFadeCurve() {
    Cue* cue = selectedFadeCue();
    if (!cue) {
      return;
    }
    cue->fadeCurve = cue->fadeCurve == CueFadeCurve::Linear  ? CueFadeCurve::EaseIn
                   : cue->fadeCurve == CueFadeCurve::EaseIn  ? CueFadeCurve::EaseOut
                   : cue->fadeCurve == CueFadeCurve::EaseOut ? CueFadeCurve::SCurve
                                                             : CueFadeCurve::Linear;
    markProjectDirty();
    triggerToast(cueFadeCurveLabel(cue->fadeCurve));
  }

  void nudgeFadeTo(double delta) {
    Cue* cue = selectedFadeCue();
    if (!cue) {
      return;
    }
    cue->fadeToValue = std::clamp(cue->fadeToValue + delta, 0.0, 1.0);
    markProjectDirty();
  }

  void nudgeFadeOver(double delta) {
    Cue* cue = selectedFadeCue();
    if (!cue) {
      return;
    }
    // Floored at zero, not at some minimum: a zero-length fade is a SET, and
    // turning the duration all the way down is how an operator asks for one.
    cue->fadeOverSeconds = std::max(0.0, cue->fadeOverSeconds + delta);
    markProjectDirty();
  }

  void stepFadeDeck(int delta) {
    Cue* cue = selectedFadeCue();
    if (!cue) {
      return;
    }
    const int count = static_cast<int>(project_.decks.size());
    const int next = cue->targetDeckIndex + delta;
    // Off either end means "the deck this cue lives on", which is what a
    // single-deck show wants and should never have to set.
    cue->targetDeckIndex = (next < 0 || next >= count) ? -1 : next;
    markProjectDirty();
  }

  void toggleFadeStopWhenDone() {
    Cue* cue = selectedFadeCue();
    if (!cue) {
      return;
    }
    cue->fadeStopWhenDone = !cue->fadeStopWhenDone;
    markProjectDirty();
    triggerToast(cue->fadeStopWhenDone ? "stop when done" : "leave it running");
  }

  void fireSelectedFadeCue() {
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size()) ||
        deck.cues[deck.selectedIndex].kind != CueKind::Fade) {
      return;
    }
    (void)fireFadeCue(deckIndex, deck.selectedIndex);
  }

  // ── AUDITION ──────────────────────────────────────────────────────────
  //
  // Play a cue to the operator without sending it anywhere. Deckboy already
  // had the two worlds it needs -- the output's composite and the control
  // window's own preview -- so this is a ROUTE, not a renderer: the deck is
  // skipped by the output compositor, and the preview stops tapping the
  // output and draws the decoder frame instead.
  bool deckIsAuditioning(int deckIndex) const {
    return auditionDeckIndex_ >= 0 && auditionDeckIndex_ == deckIndex;
  }

  bool anyDeckAuditioning() const { return auditionDeckIndex_ >= 0; }

  bool deckIsPreloading(int deckIndex) const {
    return preloadDeckIndex_ >= 0 && preloadDeckIndex_ == deckIndex;
  }

  // The two reasons a deck's picture is deliberately not on its output. One
  // predicate, because the compositor and the preview tap must never disagree
  // about which of them is in force.
  bool deckIsHeldOffOutput(int deckIndex) const {
    return deckIsAuditioning(deckIndex) || deckIsPreloading(deckIndex);
  }

  void clearPreload(bool announce = false) {
    if (preloadDeckIndex_ < 0) {
      return;
    }
    preloadDeckIndex_ = -1;
    preloadCueIndex_ = -1;
    if (announce) {
      triggerToast("preload cleared");
    }
  }

  // Rack a cue paused at a position, decode warm, without the room seeing it.
  //
  // Deckboy could already load a cue paused -- takeSelected(false) does it --
  // but that puts the paused frame straight on the output, so preparing the
  // next cue meant showing it. Held off the output it becomes what it is for:
  // the spin-up paid early so GO is instant.
  std::string preloadSelected(double atSeconds) {
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "no deck";
    }
    Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return "select a cue first";
    }
    // Held off the output BEFORE the rack, for the same reason audition is:
    // set afterwards, one frame of it reaches the output first.
    preloadDeckIndex_ = deckIndex;
    preloadCueIndex_ = deck.selectedIndex;
    preloadStarting_ = true;
    // No transition and no autoplay: this is a rack, not a take. A transition
    // here would animate a picture nobody can see and leave the outgoing cue
    // mid-dissolve.
    takeSelected(false, false);
    preloadStarting_ = false;

    const std::string name = deck.cues[deck.selectedIndex].name;
    if (atSeconds > 0.0) {
      if (MediaEngine* engine = mediaEngineForDeck(deckIndex)) {
        engine->seek(atSeconds, false);
      }
      const std::string did = "preloaded " + name + " at " + formatSeconds(atSeconds);
      triggerToast(did);
      return did;
    }
    const std::string did = "preloaded " + name;
    triggerToast(did);
    return did;
  }

  // Let go of a preloaded cue: it is already racked and already at its
  // position, so this is a play rather than a load. That is the whole point --
  // no decoder spin-up between GO and the first frame.
  bool releasePreloadIfTaking(int deckIndex, int cueIndex) {
    if (!deckIsPreloading(deckIndex) || preloadCueIndex_ != cueIndex) {
      return false;
    }
    clearPreload();
    if (MediaEngine* engine = mediaEngineForDeck(deckIndex)) {
      engine->play();
    }
    return true;
  }


  void endAudition(bool announce = true) {
    if (auditionDeckIndex_ < 0) {
      return;
    }
    const int was = auditionDeckIndex_;
    auditionDeckIndex_ = -1;
    if (announce) {
      triggerToast("audition ended - deck " + std::to_string(was + 1) +
                   " is live again");
    }
  }

  // Take the selected cue with this deck held off the outputs.
  std::string auditionSelected() {
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "no deck";
    }
    Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return "select a cue first";
    }
    // ONE AUDITION AT A TIME. Auditioning a second deck while the first is
    // still held off would black two screens for one look, and the operator
    // cannot see both anyway -- the preview shows the focused deck.
    if (auditionDeckIndex_ >= 0 && auditionDeckIndex_ != deckIndex) {
      endAudition(false);
    }
    // Set BEFORE the take, so the first composited frame is already
    // suppressed. Setting it after let one frame of the cue reach the output,
    // which on a show is the entire thing you were trying to avoid.
    auditionDeckIndex_ = deckIndex;
    auditionStarting_ = true;
    takeSelected(true);
    auditionStarting_ = false;
    const std::string name = deck.cues[deck.selectedIndex].name;
    triggerToast("audition: " + name);
    return name;
  }

  // ── MIDI CUES ─────────────────────────────────────────────────────────
  //
  // Deckboy has listened to MIDI, MSC and MMC for a long time and has never
  // said anything. This is the other direction: a cue that, on GO, tells the
  // lighting desk or the sound rig or another Deckboy to do something.
  //
  // The bytes are built by a pure function in platform/midi.hpp, which is what
  // lets them be tested with no hardware in the room -- and the encoder tests
  // in --smoke are the only part of this a machine can check.

  // Resolve the port a cue asks for, opening it only when it changes.
  //
  // A NAMED PORT THAT IS ABSENT IS REPORTED, NOT SWAPPED, the same rule the
  // app already applies to a named MIDI input and a named audio device. A cue
  // that quietly drove whatever port happened to enumerate first would be a
  // show sending GOs to the wrong desk.
  bool ensureMidiOutPort(const std::string& requested, std::string& reasonOut) {
    const std::string want = trim(requested);
    if (want.empty()) {
      // No port named: use the first one there is, which is what a rig with a
      // single interface wants and never has to configure.
      const auto ports = deckboy::platform::midi::MidiOutput::listDevices();
      if (ports.empty()) {
        reasonOut = "no MIDI output ports on this machine";
        return false;
      }
      if (midiOut_.isOpen() && midiOutPortRequested_.empty()) {
        return true;
      }
      midiOutPortRequested_.clear();
      if (!midiOut_.open(ports.front().id)) {
        reasonOut = "could not open " + ports.front().name;
        return false;
      }
      return true;
    }
    if (midiOut_.isOpen() && midiOutPortRequested_ == want) {
      return true;
    }
    if (!midiOut_.openByName(want)) {
      reasonOut = "MIDI port not found: " + want;
      midiOutPortRequested_.clear();
      return false;
    }
    midiOutPortRequested_ = want;
    return true;
  }

  deckboy::platform::midi::OutMessage midiMessageForCue(const Cue& cue) const {
    deckboy::platform::midi::OutMessage message;
    message.kind = deckboy::platform::midi::outMessageKindFromToken(cue.midiMessage);
    message.channel = cue.midiChannel;
    message.data1 = cue.midiData1;
    message.data2 = cue.midiData2;
    message.mscDevice = cue.mscDevice;
    message.mscCue = cue.mscCue;
    message.mscList = cue.mscList;
    message.rawHex = cue.midiRawHex;
    return message;
  }

  std::string fireMidiCue(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "no deck";
    }
    const Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return "no cue";
    }
    const Cue& cue = deck.cues[cueIndex];
    const auto message = midiMessageForCue(cue);
    const auto bytes = deckboy::platform::midi::encodeOutMessage(message);
    // AN UNBUILDABLE MESSAGE SENDS NOTHING AND SAYS SO. Half a MIDI message on
    // a show network is worse than silence, and a cue that silently sent
    // nothing would be indistinguishable from a cable fault.
    if (bytes.empty()) {
      const std::string why = cue.name + ": nothing to send (check the message)";
      triggerToast(why);
      return why;
    }
    std::string reason;
    if (!ensureMidiOutPort(cue.midiPortName, reason)) {
      triggerToast(cue.name + ": " + reason);
      return reason;
    }
    if (!midiOut_.send(bytes)) {
      const std::string why = cue.name + ": the port refused the message";
      triggerToast(why);
      return why;
    }
    const std::string did = deckboy::platform::midi::describeOutMessage(message) +
                            " -> " + (midiOut_.portInUse().empty()
                                        ? std::string("default port")
                                        : midiOut_.portInUse());
    showLog("MIDI OUT", did);
    triggerToast(did);
    return did;
  }

  // ── TIMECODE CUES ─────────────────────────────────────────────────────
  //
  // The LTC generator has existed for a while and could only be reached from a
  // settings toggle, which is not something a show can cue. Three verbs cover
  // what a running order needs: start the carrier, stop it, and jam it to a
  // value at a known moment.
  std::string fireTimecodeCue(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "no deck";
    }
    const Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return "no cue";
    }
    const Cue& cue = deck.cues[cueIndex];
    const std::string action = toLower(trim(cue.tcAction));

    if (action == "stop") {
      project_.ltcOutputEnabled = false;
      stopLtcOutput();
      markProjectDirty();
      const std::string did = "timecode stopped";
      showLog("TIMECODE", did);
      triggerToast(did);
      return did;
    }

    if (action == "jam") {
      // The jam is the DIFFERENCE between where the clock is about to read and
      // where the operator wants it to read, worked out now. Computed rather
      // than stored so a jam means the same thing wherever the deck happens to
      // be when it fires.
      ltcJamOffsetSeconds_ = 0.0;
      const double natural = ltcOutputTimecodeSeconds();
      ltcJamOffsetSeconds_ = cue.tcJamSeconds - natural;
      const std::string did = "timecode jammed to " +
                              formatTimecode(cue.tcJamSeconds, deck.playlistTimebaseFps);
      showLog("TIMECODE", did);
      triggerToast(did);
      return did;
    }

    // START, which is also what an unrecognised action does -- the safe
    // reading of a cue somebody labelled "timecode" is that they want some.
    if (!project_.ltcOutputEnabled) {
      project_.ltcOutputEnabled = true;
      markProjectDirty();
    }
    if (!startLtcOutput()) {
      // startLtcOutput already said why -- no libltc, no encoder, no device.
      project_.ltcOutputEnabled = false;
      return "timecode could not start";
    }
    const std::string did = "timecode running @ " +
                            fmtFloat(std::clamp(project_.ltcOutputFps, 23.0, 60.0), 2) + "fps";
    showLog("TIMECODE", did);
    triggerToast(did);
    return did;
  }

  // ── SCRIPT CUES ───────────────────────────────────────────────────────
  //
  // A script cue runs Deckboy's OWN remote-protocol lines. That is deliberate
  // and it is most of why this was worth building: every verb the socket
  // accepts already exists, is already tested, and already answers honestly,
  // so a script cue inherits all of it without the project taking on an
  // embedded language and its security surface.
  //
  // It also composes with everything else: one cue that fires a master, sends
  // MIDI, jams timecode and pings the media server is four lines.
  static constexpr int kMaxScriptDepth = 4;

  std::string runScriptCue(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "no deck";
    }
    const Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return "no cue";
    }
    // A SCRIPT THAT RUNS A SCRIPT IS FINE. A script that runs ITSELF is not,
    // and neither is a pair that run each other -- both hang the app with no
    // way back. Depth is the guard because it catches every shape of it,
    // including ones that are not literally a cycle.
    if (scriptDepth_ >= kMaxScriptDepth) {
      const std::string why = "script nesting stopped at " +
                              std::to_string(kMaxScriptDepth) + " deep";
      triggerToast(why);
      showLog("SCRIPT", why);
      return why;
    }

    // COPIED before anything runs: a line can delete the cue it is written on.
    const std::string text = deck.cues[cueIndex].scriptText;
    const std::string name = deck.cues[cueIndex].name;

    // THE DISPATCHER'S REPLY STATE IS SAVED AND PUT BACK.
    //
    // handleRemoteCommand writes remoteCommandRecognized_, _Detail_ and
    // _Error_, and this calls it in a loop -- so a script containing one bad
    // line left those flags set to that line's failure, and whatever asked
    // for the script (the socket, a TAKE) reported ITSELF as unknown. Caught
    // by the test: `SCRIPTCUE RUN` on a script with a typo in it answered
    // "ERR unknown command: SCRIPTCUE".
    const bool savedRecognized = remoteCommandRecognized_;
    const std::string savedDetail = remoteCommandDetail_;
    const std::string savedError = remoteCommandError_;

    ++scriptDepth_;
    int ran = 0;
    int failed = 0;
    std::string firstError;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
      // A trailing CR from a file that travelled through Windows, a blank
      // line, and a comment are all "nothing to do" rather than errors.
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      const std::string trimmed = trim(line);
      if (trimmed.empty() || trimmed.front() == '#' ||
          trimmed.compare(0, 2, "//") == 0) {
        continue;
      }
      ++ran;
      // Straight into the dispatcher the socket uses, so a script line means
      // exactly what the same line typed over the wire means. There is no
      // second interpretation of the protocol to keep in step with this one.
      remoteCommandRecognized_ = true;
      remoteCommandDetail_.clear();
      remoteCommandError_.clear();
      handleRemoteCommand(trimmed);
      if (!remoteCommandRecognized_ || !remoteCommandError_.empty()) {
        ++failed;
        if (firstError.empty()) {
          firstError = trimmed + (remoteCommandError_.empty()
                                    ? std::string(" - unknown command")
                                    : (" - " + remoteCommandError_));
        }
      }
    }
    --scriptDepth_;
    remoteCommandRecognized_ = savedRecognized;
    remoteCommandDetail_ = savedDetail;
    remoteCommandError_ = savedError;

    std::string did = name + ": " + std::to_string(ran) + " line" +
                      (ran == 1 ? "" : "s");
    if (failed > 0) {
      // THE FAILURES ARE NAMED, not counted silently. A script that half-ran
      // and said "ok" is the worst outcome available here.
      did += ", " + std::to_string(failed) + " failed - " + firstError;
    }
    showLog("SCRIPT", did);
    triggerToast(did);
    return did;
  }

  Cue* selectedScriptCue() {
    Cue* cue = selectedCueMutable();
    return (cue && cue->kind == CueKind::Script) ? cue : nullptr;
  }

  void runSelectedScriptCue() {
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size()) ||
        deck.cues[deck.selectedIndex].kind != CueKind::Script) {
      return;
    }
    (void)runScriptCue(deckIndex, deck.selectedIndex);
  }

  // How many lines will actually run, for the inspector and the problem scan.
  static int scriptLineCount(const std::string& text) {
    int count = 0;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      const std::string trimmed = trim(line);
      if (trimmed.empty() || trimmed.front() == '#' ||
          trimmed.compare(0, 2, "//") == 0) {
        continue;
      }
      ++count;
    }
    return count;
  }

  Cue* selectedTimecodeCue() {
    Cue* cue = selectedCueMutable();
    return (cue && cue->kind == CueKind::Timecode) ? cue : nullptr;
  }

  void cycleTimecodeAction() {
    Cue* cue = selectedTimecodeCue();
    if (!cue) {
      return;
    }
    const std::string now = toLower(trim(cue->tcAction));
    cue->tcAction = now == "start" ? "stop" : (now == "stop" ? "jam" : "start");
    markProjectDirty();
    triggerToast(toUpper(cue->tcAction));
  }

  void nudgeTimecodeJam(double delta) {
    Cue* cue = selectedTimecodeCue();
    if (!cue) {
      return;
    }
    cue->tcJamSeconds = std::max(0.0, cue->tcJamSeconds + delta);
    markProjectDirty();
  }

  void editTimecodeJam() {
    if (!selectedTimecodeCue()) {
      return;
    }
    openInlineTextEditor("cue.tc_jam", "Jam To",
                         "hh:mm:ss or seconds",
                         formatTimecode(selectedTimecodeCue()->tcJamSeconds,
                                        focusedDeck().playlistTimebaseFps),
                         [this](const std::string& value) {
                           Cue* c = selectedTimecodeCue();
                           if (!c) {
                             return;
                           }
                           // Accepts a timecode or a bare number of seconds,
                           // because both are things people type.
                           const std::string text = trim(value);
                           double seconds = 0.0;
                           if (text.find(':') != std::string::npos) {
                             // The existing parser, which returns nullopt on
                             // rubbish rather than a plausible wrong time.
                             auto parsed = parseTimecodeSeconds(
                               text, focusedDeck().playlistTimebaseFps);
                             if (!parsed) {
                               triggerToast("jam: not a timecode");
                               return;
                             }
                             seconds = *parsed;
                           } else {
                             try {
                               seconds = std::stod(text);
                             } catch (...) {
                               triggerToast("jam: not a time");
                               return;
                             }
                           }
                           c->tcJamSeconds = std::max(0.0, seconds);
                           markProjectDirty();
                         });
  }

  void fireSelectedTimecodeCue() {
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size()) ||
        deck.cues[deck.selectedIndex].kind != CueKind::Timecode) {
      return;
    }
    (void)fireTimecodeCue(deckIndex, deck.selectedIndex);
  }

  Cue* selectedNetworkCue() {
    Cue* cue = selectedCueMutable();
    return (cue && cue->kind == CueKind::Network) ? cue : nullptr;
  }

  void cycleNetworkProtocol() {
    Cue* cue = selectedNetworkCue();
    if (!cue) {
      return;
    }
    const std::string now = toLower(trim(cue->netProtocol));
    cue->netProtocol = now == "osc" ? "udp" : (now == "udp" ? "tcp" : "osc");
    markProjectDirty();
    triggerToast(toUpper(cue->netProtocol));
  }

  void nudgeNetworkPort(int delta) {
    Cue* cue = selectedNetworkCue();
    if (!cue) {
      return;
    }
    cue->netPort = std::clamp(cue->netPort + delta, 1, 65535);
    markProjectDirty();
  }

  void editNetworkHost() {
    if (!selectedNetworkCue()) {
      return;
    }
    openInlineTextEditor("cue.net_host", "Destination Address",
                         "e.g. 192.168.1.50", selectedNetworkCue()->netHost,
                         [this](const std::string& value) {
                           if (Cue* c = selectedNetworkCue()) {
                             c->netHost = trim(value);
                             markProjectDirty();
                           }
                         });
  }

  void editNetworkAddress() {
    if (!selectedNetworkCue()) {
      return;
    }
    openInlineTextEditor("cue.net_address", "OSC Address",
                         "e.g. /cue/1/start", selectedNetworkCue()->netAddress,
                         [this](const std::string& value) {
                           if (Cue* c = selectedNetworkCue()) {
                             c->netAddress = trim(value);
                             markProjectDirty();
                           }
                         });
  }

  void editNetworkPayload() {
    if (!selectedNetworkCue()) {
      return;
    }
    openInlineTextEditor("cue.net_payload", "Payload",
                         "text to send", selectedNetworkCue()->netPayload,
                         [this](const std::string& value) {
                           if (Cue* c = selectedNetworkCue()) {
                             // NOT trimmed: a trailing newline is often the
                             // whole point of a TCP line, and trimming it here
                             // would silently break every line-based protocol.
                             c->netPayload = value;
                             markProjectDirty();
                           }
                         });
  }

  void sendSelectedNetworkCueNow() {
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size()) ||
        deck.cues[deck.selectedIndex].kind != CueKind::Network) {
      return;
    }
    (void)fireNetworkCue(deckIndex, deck.selectedIndex);
  }

  Cue* selectedMidiCue() {
    Cue* cue = selectedCueMutable();
    return (cue && cue->kind == CueKind::Midi) ? cue : nullptr;
  }

  void cycleMidiCueKind() {
    Cue* cue = selectedMidiCue();
    if (!cue) {
      return;
    }
    static const char* kOrder[] = {"note-on", "note-off", "cc", "program",
                                   "msc-go", "msc-stop", "msc-resume", "raw"};
    const int count = static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0]));
    int at = 0;
    for (int i = 0; i < count; ++i) {
      if (cue->midiMessage == kOrder[i]) {
        at = i;
        break;
      }
    }
    cue->midiMessage = kOrder[(at + 1) % count];
    markProjectDirty();
    triggerToast(deckboy::platform::midi::outMessageKindLabel(
      deckboy::platform::midi::outMessageKindFromToken(cue->midiMessage)));
  }

  // Steps through the ports that exist RIGHT NOW, plus an empty entry meaning
  // "the first one there is". Enumerated at every press rather than cached:
  // interfaces get plugged in during setup, and a list from boot would be a
  // list of what used to be there.
  void cycleMidiCuePort() {
    Cue* cue = selectedMidiCue();
    if (!cue) {
      return;
    }
    const auto ports = deckboy::platform::midi::MidiOutput::listDevices();
    if (ports.empty()) {
      triggerToast("no MIDI output ports on this machine");
      return;
    }
    int at = -1;
    for (int i = 0; i < static_cast<int>(ports.size()); ++i) {
      if (ports[i].name == cue->midiPortName) {
        at = i;
        break;
      }
    }
    const int next = at + 1;
    cue->midiPortName = next >= static_cast<int>(ports.size())
                          ? std::string() : ports[next].name;
    markProjectDirty();
    triggerToast(cue->midiPortName.empty() ? "first port available"
                                           : cue->midiPortName);
  }

  // 0 channel, 1 data1, 2 data2, 3 MSC device.
  void nudgeMidiCueField(int which, int delta) {
    Cue* cue = selectedMidiCue();
    if (!cue) {
      return;
    }
    switch (which) {
      case 0: cue->midiChannel = std::clamp(cue->midiChannel + delta, 1, 16); break;
      case 1: cue->midiData1 = std::clamp(cue->midiData1 + delta, 0, 127); break;
      case 2: cue->midiData2 = std::clamp(cue->midiData2 + delta, 0, 127); break;
      default: cue->mscDevice = std::clamp(cue->mscDevice + delta, 0, 127); break;
    }
    markProjectDirty();
  }

  void editMidiCueNumber() {
    Cue* cue = selectedMidiCue();
    if (!cue) {
      return;
    }
    openInlineTextEditor("cue.msc_number", "MSC Cue Number",
                         "e.g. 12.5", cue->mscCue,
                         [this](const std::string& value) {
                           if (Cue* c = selectedMidiCue()) {
                             c->mscCue = trim(value);
                             markProjectDirty();
                           }
                         });
  }

  void editMidiCueRawHex() {
    Cue* cue = selectedMidiCue();
    if (!cue) {
      return;
    }
    openInlineTextEditor("cue.midi_raw", "Raw MIDI Bytes",
                         "e.g. 90 3C 7F", cue->midiRawHex,
                         [this](const std::string& value) {
                           if (Cue* c = selectedMidiCue()) {
                             c->midiRawHex = trim(value);
                             markProjectDirty();
                           }
                         });
  }

  void sendSelectedMidiCueNow() {
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size()) ||
        deck.cues[deck.selectedIndex].kind != CueKind::Midi) {
      return;
    }
    (void)fireMidiCue(deckIndex, deck.selectedIndex);
  }

  // ── NETWORK CUES ──────────────────────────────────────────────────────
  //
  // Deckboy listens on OSC, UDP and TCP and has never spoken on any of them
  // except as feedback. A network cue is the other direction: on GO, tell the
  // media server or the desk or the other Deckboy to do something.
  //
  // UDP AND OSC ARE FIRE AND FORGET and cost nothing on the main thread. TCP
  // is not: a connect to a machine that is off can block for the operating
  // system's timeout, and a GO that stalls for even a second is unusable. So
  // TCP runs on a detached thread and reports back through a queue.

  // The payload an operator typed, with the escapes they would expect. Kept
  // small deliberately: this is a show-control line, not a scripting language.
  static std::string expandNetworkEscapes(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
      if (text[i] != '\\' || i + 1 >= text.size()) {
        out.push_back(text[i]);
        continue;
      }
      switch (text[++i]) {
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case '0': out.push_back('\0'); break;
        case '\\': out.push_back('\\'); break;
        // An unknown escape keeps BOTH characters rather than eating the
        // backslash: a Windows path in a payload should survive being typed.
        default: out.push_back('\\'); out.push_back(text[i]); break;
      }
    }
    return out;
  }

  void drainNetworkResults() {
    std::vector<std::string> results;
    {
      std::lock_guard<std::mutex> lock(networkResultMutex_);
      if (networkResults_.empty()) {
        return;
      }
      results.swap(networkResults_);
    }
    for (const std::string& line : results) {
      triggerToast(line);
      showLog("NETWORK", line);
    }
  }

  std::string fireNetworkCue(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "no deck";
    }
    const Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return "no cue";
    }
    const Cue& cue = deck.cues[cueIndex];
    const std::string host = trim(cue.netHost);
    const int port = std::clamp(cue.netPort, 1, 65535);
    const std::string protocol = toLower(trim(cue.netProtocol));
    if (host.empty()) {
      const std::string why = cue.name + ": no host";
      triggerToast(why);
      return why;
    }

    sockaddr_in target {};
    target.sin_family = AF_INET;
    target.sin_port = htons(static_cast<uint16_t>(port));
    // NUMERIC ADDRESSES ONLY, and said so rather than guessed at. Resolving a
    // name means a DNS lookup, which is a blocking call with no bound on a
    // show network -- exactly what a GO must not do.
    if (inet_pton(AF_INET, host.c_str(), &target.sin_addr) != 1) {
      const std::string why = cue.name + ": " + host +
                              " is not an IPv4 address";
      triggerToast(why);
      return why;
    }

    const std::string payload = expandNetworkEscapes(cue.netPayload);
    const std::string where = host + ":" + std::to_string(port);

    if (protocol == "osc") {
      const std::string address = trim(cue.netAddress);
      if (address.empty() || address.front() != '/') {
        const std::string why = cue.name + ": an OSC address must start with /";
        triggerToast(why);
        return why;
      }
      sendOscStringTo(target, address, payload);
      const std::string did = "OSC " + address + " -> " + where;
      showLog("NETWORK", did);
      triggerToast(did);
      return did;
    }

    if (protocol == "udp") {
      if (companionUdpSocket_ == deckboy::platform::kInvalidSocket) {
        const std::string why = cue.name + ": no UDP socket";
        triggerToast(why);
        return why;
      }
      sendto(companionUdpSocket_, payload.data(),
             static_cast<int>(payload.size()), 0,
             reinterpret_cast<const sockaddr*>(&target),
             static_cast<socklen_t>(sizeof(target)));
      const std::string did = "UDP " + std::to_string(payload.size()) +
                              " bytes -> " + where;
      showLog("NETWORK", did);
      triggerToast(did);
      return did;
    }

    if (protocol == "tcp") {
      // OFF THE MAIN THREAD, always. Everything it needs is copied in: the cue
      // can be edited or deleted while this is in flight.
      std::thread([this, target, payload, where, name = cue.name]() {
        std::string result;
        deckboy::platform::SocketHandle fd =
          socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd == deckboy::platform::kInvalidSocket) {
          result = name + ": could not open a socket";
        } else {
          if (connect(fd, reinterpret_cast<const sockaddr*>(&target),
                      static_cast<socklen_t>(sizeof(target))) == 0) {
            const int sent = static_cast<int>(
              ::send(fd, payload.data(), static_cast<int>(payload.size()), 0));
            result = sent >= 0
              ? ("TCP " + std::to_string(sent) + " bytes -> " + where)
              : (name + ": TCP send failed to " + where);
          } else {
            result = name + ": nothing answered at " + where;
          }
          deckboy::platform::closeSocket(fd);
        }
        std::lock_guard<std::mutex> lock(networkResultMutex_);
        networkResults_.push_back(result);
      }).detach();
      const std::string did = "TCP -> " + where + " (sending)";
      return did;
    }

    const std::string why = cue.name + ": unknown protocol " + protocol;
    triggerToast(why);
    return why;
  }

  void toggleSelectedCueArmed() {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    cue->armed = !cue->armed;
    markProjectDirty();
    triggerToast((cue->armed ? "armed: " : "disarmed: ") + cue->name);
  }

  // Step which DECK a target points at. Walking off either end clears the
  // target, the same way stepping a master assignment off the end does -- the
  // control that sets a thing has to be able to unset it.
  void stepTargetDeck(int delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Target) {
      return;
    }
    const int count = static_cast<int>(project_.decks.size());
    const int next = cue->targetDeckIndex + delta;
    if (next < 0 || next >= count) {
      cue->targetDeckIndex = -1;
      cue->targetCueId.clear();
      markProjectDirty();
      return;
    }
    cue->targetDeckIndex = next;
    // Changing deck invalidates the cue: an id from the old deck would resolve
    // through the whole-show fallback and point back where it came from, so
    // the row would read as if the deck had not changed at all.
    cue->targetCueId.clear();
    if (!project_.decks[next].cues.empty()) {
      cue->targetCueId = project_.decks[next].cues[0].id;
    }
    markProjectDirty();
  }

  void stepTargetCue(int delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Target) {
      return;
    }
    const int d = cue->targetDeckIndex;
    if (d < 0 || d >= static_cast<int>(project_.decks.size())) {
      triggerToast("pick a deck first");
      return;
    }
    const Deck& deck = project_.decks[d];
    if (deck.cues.empty()) {
      triggerToast("deck " + std::to_string(d + 1) + " has no cues");
      return;
    }
    const int current = findCueIndexById(d, cue->targetCueId);
    const int next = current + delta;        // -1 + 1 == 0, so "none" steps to the first
    if (next < 0 || next >= static_cast<int>(deck.cues.size())) {
      cue->targetCueId.clear();
      markProjectDirty();
      return;
    }
    cue->targetCueId = deck.cues[next].id;
    markProjectDirty();
  }

  void cycleTargetVerb() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Target) {
      return;
    }
    static const CueTargetVerb kOrder[] = {
      CueTargetVerb::Start, CueTargetVerb::Stop, CueTargetVerb::Pause,
      CueTargetVerb::Resume, CueTargetVerb::Load, CueTargetVerb::Arm,
      CueTargetVerb::Disarm,
    };
    const int count = static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0]));
    int at = 0;
    for (int i = 0; i < count; ++i) {
      if (kOrder[i] == cue->targetVerb) {
        at = i;
        break;
      }
    }
    cue->targetVerb = kOrder[(at + 1) % count];
    markProjectDirty();
  }

  void fireSelectedTargetCue() {
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    if (deck.cues[deck.selectedIndex].kind != CueKind::Target) {
      return;
    }
    (void)fireTargetCue(deckIndex, deck.selectedIndex);
  }

  // ── FADE CUES ─────────────────────────────────────────────────────────
  //
  // A fade is a RUN, not a setting: it has a start, a shape and an end, and it
  // has to be able to be interrupted by the next one without either of them
  // getting stuck halfway. So each firing pushes one of these and the update
  // loop walks them, rather than a per-deck "currently fading" flag that two
  // cues would fight over.
  //
  // What a fade moves is deliberately never a field the show file keeps. Deck
  // opacity has a target the app already ramps toward, and deck volume is the
  // engine's runtime level -- so a fade can never leave a saved show quieter
  // or darker than the operator left it.
  struct FadeRun {
    int deckIndex = -1;                     // -1 for master-scope fades
    CueFadeWhat what = CueFadeWhat::DeckOpacity;
    CueFadeCurve curve = CueFadeCurve::Linear;
    double from = 0.0;
    double to = 0.0;
    double durationSeconds = 0.0;
    Uint64 startMs = 0;
    bool stopWhenDone = false;
    std::string label;                      // for the toast when it lands
  };
  std::vector<FadeRun> fadeRuns_;

  // Where a fade would start from RIGHT NOW. Read at fire time rather than
  // stored on the cue: a fade that always started from 100% would jump the
  // level up before taking it down, which is the single most visible way to
  // get a fade wrong.
  double currentFadeValue(int deckIndex, CueFadeWhat what) const {
    switch (what) {
      case CueFadeWhat::MasterDimmer:
        return std::clamp(project_.masterDimmer, 0.0, 1.0);
      case CueFadeWhat::DeckVolume: {
        const MediaEngine* engine = mediaEngineForDeck(deckIndex);
        return engine ? std::clamp(static_cast<double>(engine->volume()), 0.0, 1.0) : 0.0;
      }
      case CueFadeWhat::DeckOpacity:
        break;
    }
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return 0.0;
    }
    return std::clamp(static_cast<double>(project_.decks[deckIndex].playlistOpacity), 0.0, 1.0);
  }

  void applyFadeValue(int deckIndex, CueFadeWhat what, double value) {
    value = std::clamp(value, 0.0, 1.0);
    switch (what) {
      case CueFadeWhat::MasterDimmer:
        project_.masterDimmer = value;
        return;
      case CueFadeWhat::DeckVolume:
        if (MediaEngine* engine = mediaEngineForDeck(deckIndex)) {
          engine->setVolume(static_cast<float>(value));
        }
        return;
      case CueFadeWhat::DeckOpacity:
        break;
    }
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    project_.decks[deckIndex].playlistOpacity = static_cast<float>(value);
    // AND THE TARGET, every tick. The app already ramps playlistOpacity toward
    // deckPlaylistOpacityTargets_ at the DECK's fade rate; leaving that target
    // where it was would have the two pulling in opposite directions and the
    // fade would never arrive. Writing both means the old ramp sees no error
    // and stands down while this one owns the value.
    setDeckPlaylistOpacityTarget(deckIndex, static_cast<float>(value));
  }

  // One fade per deck per thing. A new fade on the same pair replaces the old
  // one from wherever it had got to, which is what an operator means when they
  // fire a fade up in the middle of a fade down.
  void cancelFadesFor(int deckIndex, CueFadeWhat what) {
    fadeRuns_.erase(std::remove_if(fadeRuns_.begin(), fadeRuns_.end(),
                                   [&](const FadeRun& f) {
                                     return f.deckIndex == deckIndex && f.what == what;
                                   }),
                    fadeRuns_.end());
  }

  // STOP means stop. Anything this deck was fading dies with it, for the same
  // reason a pending take does -- otherwise a fade lands seconds later on an
  // operator who thought they had stopped the show.
  void cancelFadesForDeck(int deckIndex) {
    fadeRuns_.erase(std::remove_if(fadeRuns_.begin(), fadeRuns_.end(),
                                   [&](const FadeRun& f) {
                                     return f.deckIndex == deckIndex;
                                   }),
                    fadeRuns_.end());
  }

  std::string fireFadeCue(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "no deck";
    }
    Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return "no cue";
    }
    const Cue fading = deck.cues[cueIndex];

    // A fade with no deck named acts on the deck it lives on, which is what a
    // single-deck show wants and never has to be set.
    int victimDeck = fading.targetDeckIndex;
    if (victimDeck < 0 || victimDeck >= static_cast<int>(project_.decks.size())) {
      victimDeck = deckIndex;
    }
    if (fading.fadeWhat == CueFadeWhat::MasterDimmer) {
      victimDeck = -1;                      // master scope: no deck owns it
    }

    FadeRun run;
    run.deckIndex = victimDeck;
    run.what = fading.fadeWhat;
    run.curve = fading.fadeCurve;
    run.from = currentFadeValue(victimDeck, fading.fadeWhat);
    run.to = std::clamp(fading.fadeToValue, 0.0, 1.0);
    run.durationSeconds = std::max(0.0, fading.fadeOverSeconds);
    run.startMs = SDL_GetTicks();
    run.stopWhenDone = fading.fadeStopWhenDone;
    run.label = fading.name;

    cancelFadesFor(victimDeck, fading.fadeWhat);

    char pct[8];
    std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(std::lround(run.to * 100.0)));
    const std::string did = std::string(cueFadeWhatLabel(run.what)) + " -> " + pct +
                            " over " + formatSeconds(run.durationSeconds);

    // A ZERO-LENGTH FADE IS A SET, and must land on this tick rather than
    // waiting for the next one -- a fade cue with the duration turned all the
    // way down is how an operator asks for a snap.
    if (run.durationSeconds <= 0.0) {
      applyFadeValue(run.deckIndex, run.what, run.to);
      if (run.stopWhenDone) {
        finishFadeStop(run.deckIndex);
      }
      triggerToast(did);
      return did;
    }
    fadeRuns_.push_back(run);
    triggerToast(did);
    return did;
  }

  void finishFadeStop(int deckIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const int savedFocus = project_.focusedDeckIndex;
    project_.focusedDeckIndex = deckIndex;
    stopTransport();
    project_.focusedDeckIndex = savedFocus;
  }

  // Called once a tick. Walks backwards so a run can be erased in place.
  void serviceFades() {
    if (fadeRuns_.empty()) {
      return;
    }
    const Uint64 now = SDL_GetTicks();
    for (int i = static_cast<int>(fadeRuns_.size()) - 1; i >= 0; --i) {
      FadeRun& run = fadeRuns_[static_cast<std::size_t>(i)];
      const double elapsed = static_cast<double>(now - run.startMs) / 1000.0;
      const double raw = run.durationSeconds > 0.0 ? elapsed / run.durationSeconds : 1.0;
      if (raw >= 1.0) {
        // LANDS EXACTLY ON THE TARGET. Interpolating one last time would leave
        // it a fraction short, and "the fade ends at 0.3% instead of black" is
        // the kind of thing nobody sees in rehearsal and everybody sees on the
        // night.
        applyFadeValue(run.deckIndex, run.what, run.to);
        const bool stop = run.stopWhenDone;
        const int deckIndex = run.deckIndex;
        const std::string label = run.label;
        fadeRuns_.erase(fadeRuns_.begin() + i);
        if (stop) {
          finishFadeStop(deckIndex);
        }
        showLog("FADE DONE", label);
        continue;
      }
      const double shaped = applyCueFadeCurve(run.curve, raw);
      applyFadeValue(run.deckIndex, run.what, run.from + (run.to - run.from) * shaped);
    }
  }

  // What is fading, for the operator and for a remote caller. Derived, like
  // everything else that answers a question about now.
  std::string fadeRunSummary() const {
    if (fadeRuns_.empty()) {
      return "nothing fading";
    }
    const Uint64 now = SDL_GetTicks();
    std::ostringstream out;
    bool first = true;
    for (const FadeRun& run : fadeRuns_) {
      if (!first) {
        out << " | ";
      }
      first = false;
      const double elapsed = static_cast<double>(now - run.startMs) / 1000.0;
      const double left = std::max(0.0, run.durationSeconds - elapsed);
      if (run.deckIndex >= 0) {
        out << "deck " << (run.deckIndex + 1) << " ";
      }
      out << cueFadeWhatLabel(run.what) << " " << formatSeconds(left) << " left";
    }
    return out.str();
  }

  std::string fireTargetCue(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "no deck";
    }
    Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return "no cue";
    }
    // Copied for the same reason a master copies its plan: acting on the
    // victim's deck can reallocate any cue vector under a reference.
    const Cue targeting = deck.cues[cueIndex];

    int victimDeck = -1;
    int victimIndex = -1;
    if (!resolveTargetCue(targeting, victimDeck, victimIndex)) {
      const std::string why = targeting.targetCueId.empty()
                                ? "targets nothing"
                                : "target cue is gone";
      triggerToast(targeting.name + ": " + why);
      return why;
    }
    // A TARGET MAY NOT TARGET A TARGET. Two pointing at each other would
    // recurse until the stack gave out, which is the same reason a master
    // refuses to fire a master.
    if (project_.decks[victimDeck].cues[victimIndex].kind == CueKind::Target) {
      const std::string why = "a target cannot target another target";
      triggerToast(targeting.name + ": " + why);
      return why;
    }
    const std::string victimName = project_.decks[victimDeck].cues[victimIndex].name;
    const char* verbLabel = cueTargetVerbLabel(targeting.targetVerb);

    // Arm and disarm are edits, not transport: they change the victim and stop.
    if (targeting.targetVerb == CueTargetVerb::Arm ||
        targeting.targetVerb == CueTargetVerb::Disarm) {
      const bool arm = targeting.targetVerb == CueTargetVerb::Arm;
      project_.decks[victimDeck].cues[victimIndex].armed = arm;
      markProjectDirty();
      const std::string did = std::string(verbLabel) + ": " + victimName;
      triggerToast(did);
      return did;
    }

    // STOP, PAUSE AND RESUME ACT ON A DECK, and the named cue is the only
    // thing that makes them mean anything. If something else has since been
    // taken on that deck, a Stop target would stop THAT -- a cue labelled
    // "stop the walk-in music" killing the keynote. Refuse instead: a target
    // whose victim is not on air has nothing to do.
    const bool needsVictimLive = targeting.targetVerb == CueTargetVerb::Stop ||
                                 targeting.targetVerb == CueTargetVerb::Pause ||
                                 targeting.targetVerb == CueTargetVerb::Resume;
    if (needsVictimLive && project_.decks[victimDeck].activeIndex != victimIndex) {
      const std::string why = std::string(verbLabel) + ": " + victimName +
                              " is not on air";
      triggerToast(why);
      return why;
    }

    const int savedFocus = project_.focusedDeckIndex;
    project_.focusedDeckIndex = victimDeck;
    switch (targeting.targetVerb) {
      case CueTargetVerb::Start:
        selectCueInDeck(victimDeck, victimIndex, false, false);
        takeSelected(true);
        break;
      case CueTargetVerb::Load:
        // Selects and stands by without taking: the next GO on that deck
        // fires it. This is the verb that lets one list drive another's
        // running order without also firing it.
        selectCueInDeck(victimDeck, victimIndex, false, false);
        setStandbyIndex(victimDeck, victimIndex, false);
        break;
      case CueTargetVerb::Stop:
        stopTransport();
        break;
      case CueTargetVerb::Pause:
        pauseTransport();
        break;
      case CueTargetVerb::Resume:
        playTransport();
        break;
      case CueTargetVerb::Arm:
      case CueTargetVerb::Disarm:
        break;                                // handled above
    }
    project_.focusedDeckIndex = savedFocus;
    const std::string did = std::string(verbLabel) + ": " + victimName;
    triggerToast(did);
    return did;
  }

  void takeSelected(bool autoplay, bool useTransition = true, bool suppressIncomingFadeIn = false,
                    bool honourPreWait = true) {
    Deck& deck = focusedDeckMutable();
    int deckIndex = std::clamp(project_.focusedDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    MediaEngine* engine = focusedMediaEngine();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    // Taking anything cancels whatever this deck was about to take. The
    // operator has just made a decision; an older countdown must not survive
    // it and fire on top.
    cancelPendingTake(deckIndex);

    // AN ORDINARY TAKE ENDS AN AUDITION. auditionSelected sets the flag and
    // then calls straight through to here, so this must not undo its own take
    // -- the check is for a take arriving from anywhere else, which is the
    // operator deciding to put something on air.
    if (deckIsAuditioning(deckIndex) && !auditionStarting_) {
      endAudition(false);
    }
    // TAKING THE PRELOADED CUE IS A PLAY, NOT A LOAD. The cue is already
    // racked at its position, so re-loading it here would throw away the very
    // spin-up the preload paid for and put a decoder stall between GO and the
    // first frame -- the opposite of the feature.
    if (!preloadStarting_ && releasePreloadIfTaking(deckIndex, deck.selectedIndex)) {
      return;
    }
    // Taking anything ELSE abandons the preload; it is no longer what is next.
    if (!preloadStarting_ && deckIsPreloading(deckIndex)) {
      clearPreload();
    }

    // A DISARMED CUE DOES NOTHING AT ALL -- not its pre-wait, not its media,
    // not its continue. Checked first so that is true however it was fired:
    // by hand, by a continue, or by a master.
    if (!deck.cues[deck.selectedIndex].armed) {
      triggerToast("disarmed: " + deck.cues[deck.selectedIndex].name);
      return;
    }

    // PRE-WAIT: fired now, starts later. Applies however the cue was fired --
    // by GO, by a continue, or by a master -- because the wait belongs to the
    // cue, not to whoever pressed something. honourPreWait is false only when
    // the pending service is calling back, having already served it.
    if (honourPreWait && deck.cues[deck.selectedIndex].preWaitSeconds > 0.0) {
      const Cue& waiting = deck.cues[deck.selectedIndex];
      schedulePendingTake(deckIndex, deck.selectedIndex, waiting.preWaitSeconds,
                          useTransition, "pre-wait");
      triggerToast("pre-wait " + formatSeconds(waiting.preWaitSeconds) + ": " +
                   waiting.name);
      return;
    }

    // A MASTER FIRES OTHER DECKS and is never handed to this deck's engine --
    // it has no media. Intercepted before the engine check below, because a
    // master deck legitimately has no engine of its own to speak of.
    if (deck.cues[deck.selectedIndex].kind == CueKind::Master) {
      fireMasterCue(deckIndex, deck.selectedIndex);
      scheduleContinueAfterStart(deckIndex, deck.selectedIndex);
      return;
    }
    // A TARGET acts on another cue and is likewise never handed to an engine.
    if (deck.cues[deck.selectedIndex].kind == CueKind::Target) {
      (void)fireTargetCue(deckIndex, deck.selectedIndex);
      scheduleContinueAfterStart(deckIndex, deck.selectedIndex);
      return;
    }
    // A SCRIPT CUE RUNS ITS LINES AND IS DONE.
    if (deck.cues[deck.selectedIndex].kind == CueKind::Script) {
      (void)runScriptCue(deckIndex, deck.selectedIndex);
      scheduleContinueAfterStart(deckIndex, deck.selectedIndex);
      return;
    }
    // A TIMECODE CUE ACTS ON THE GENERATOR AND IS DONE.
    if (deck.cues[deck.selectedIndex].kind == CueKind::Timecode) {
      (void)fireTimecodeCue(deckIndex, deck.selectedIndex);
      scheduleContinueAfterStart(deckIndex, deck.selectedIndex);
      return;
    }
    // A NETWORK CUE SENDS AND IS DONE, for the same reasons as a MIDI one.
    if (deck.cues[deck.selectedIndex].kind == CueKind::Network) {
      (void)fireNetworkCue(deckIndex, deck.selectedIndex);
      scheduleContinueAfterStart(deckIndex, deck.selectedIndex);
      return;
    }
    // A MIDI CUE SENDS AND IS DONE. Never handed to an engine: it has no
    // media, and it must not disturb whatever picture the deck is carrying.
    if (deck.cues[deck.selectedIndex].kind == CueKind::Midi) {
      (void)fireMidiCue(deckIndex, deck.selectedIndex);
      scheduleContinueAfterStart(deckIndex, deck.selectedIndex);
      return;
    }
    // A FADE starts a ramp and is done; the ramp outlives the take.
    if (deck.cues[deck.selectedIndex].kind == CueKind::Fade) {
      (void)fireFadeCue(deckIndex, deck.selectedIndex);
      scheduleContinueAfterStart(deckIndex, deck.selectedIndex);
      return;
    }
    if (!engine) {
      return;
    }
    // A cue whose file has vanished must not be taken: the decode would fail
    // to black in ~100 ms and (on auto-advance) cascade through the playlist.
    // Flag it, tell the operator, keep whatever is on the output.
    // The motion driver is not a cue and does not get taken, so it needs
    // telling. Without this a rehearsed puppet look depended on how long the
    // app had been open, which is not something a show can rely on.
    if (deck.cues[deck.selectedIndex].motionDriverRestartOnTake &&
        !deck.cues[deck.selectedIndex].motionDriverPath.empty()) {
      restartMotionDriver(deckIndex);
    }
    if (!cueMediaAvailableForTake(deck.cues[deck.selectedIndex])) {
      showLog("TAKE-BLOCKED", showLogCueRef(deckIndex, deck.selectedIndex) + " media missing");
      triggerToast("MEDIA MISSING: " + deck.cues[deck.selectedIndex].name + " — take blocked");
      playUiSound(UiSoundEffect::Error);
      return;
    }
    // The feedback loop belongs to what was on this deck, not to the deck. Left
    // alone, the first frame of a new cue would echo the last frame of the old
    // one -- a ghost of the previous clip, on the output, at the take.
    resetDeckFeedback(deckIndex);
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
    // Before it moves: the cue leaving the screen is what the presenter view
    // shows as PREVIOUS, and the incoming one starts at the top of its notes.
    presenterOnCueTaken(deckIndex, deck.activeIndex);
    deck.activeIndex = deck.selectedIndex;
    // If refreshOnTake is set and a browser renderer is already running for this
    // cue, reload the page instead of tearing down and restarting.
    DeckRuntime* browserRuntime = runtimeForDeck(deckIndex);
    bool browserRefreshInstead = (cue.kind == CueKind::Browser)
      && cue.refreshOnTake
      && browserRuntime && browserRuntime->browserRenderer
      && browserRuntime->browserRenderer->isRunning();
    // Same deck as everything else in this function. The no-argument overload
    // stops the FOCUSED deck, so taking a cue on deck 2 tore down deck 1's
    // browser and left deck 2's running -- one deck went dark, the other
    // kept a page nobody asked for.
    if (!browserRefreshInstead) stopBrowserCue(deckIndex);
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
        // THE DECK THIS CUE IS ON, not whichever deck happens to be focused.
        // Everything around it already uses deckIndex -- browserRuntime is
        // resolved from it, and the refresh branch above reloads that deck --
        // so a browser cue taken on an unfocused deck started the browser on
        // the focused one instead: the wrong deck went live, and the deck the
        // operator was cueing got nothing.
        startBrowserCue(deckIndex, cue);
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
    // AUTO-CONTINUE is counted from HERE -- the moment the cue starts -- which
    // is the whole difference between it and auto-follow. The next cue can
    // therefore begin while this one is still playing, which is what makes a
    // sting land over the top of a video rather than after it.
    scheduleContinueAfterStart(deckIndex, deck.selectedIndex);
    markProjectDirty();
  }

  void jumpSelectedCue() {
    // QUANTISED TAKES. The point of tempo in a video mixer is not that
    // anything moves by itself -- it is that what the OPERATOR does lands on
    // the music instead of a moment after it. So the take is held until the
    // next beat rather than being fired by a metronome.
    //
    // Held only when the wait is worth having: past nine tenths of the way to
    // the beat a human already hit it, and delaying would push the cue a whole
    // beat late, which is the opposite of the intent.
    if (project_.vjModeEnabled && project_.vjQuantiseTakes && !vjTakeFiring_) {
      const double wait = vjSecondsToNextBeat();
      const double beatSeconds = 60.0 / std::clamp(project_.vjTempoBpm, 20.0, 300.0);
      if (wait > beatSeconds * 0.1) {
        vjTakePending_ = true;
        vjTakeDueAt_ = static_cast<double>(SDL_GetTicks()) / 1000.0 + wait;
        vjTakeDeck_ = project_.focusedDeckIndex;
        return;
      }
    }
    takeSelected(jumpTriggersPlayback(), project_.jumpTransitionEnabled);
  }

  // Called every update: fire a held take when its beat arrives.
  void serviceVjQuantisedTake() {
    if (!vjTakePending_) {
      return;
    }
    if (!project_.vjModeEnabled || !project_.vjQuantiseTakes) {
      vjTakePending_ = false;   // the mode went away under it
      return;
    }
    if (static_cast<double>(SDL_GetTicks()) / 1000.0 < vjTakeDueAt_) {
      return;
    }
    vjTakePending_ = false;
    // The deck the operator was on when they asked, not whichever one has
    // focus a beat later.
    const int restore = project_.focusedDeckIndex;
    if (vjTakeDeck_ >= 0 && vjTakeDeck_ < static_cast<int>(project_.decks.size())) {
      project_.focusedDeckIndex = vjTakeDeck_;
    }
    vjTakeFiring_ = true;      // so the take does not queue itself again
    jumpSelectedCue();
    vjTakeFiring_ = false;
    project_.focusedDeckIndex = restore;
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
    // The effect stack is an inspector setting like any other, and it was the
    // one thing COPY silently dropped -- so copying a cue you had spent time
    // grading gave you back everything except the look.
    target.effects = source.effects;
    target.motionDriverPath = source.motionDriverPath;
    target.motionDriverSpeed = source.motionDriverSpeed;
    target.motionDriverPaused = source.motionDriverPaused;
    target.motionDriverRestartOnTake = source.motionDriverRestartOnTake;
  }

  // ---------------------------------------------------------------------------
  // The effect chain on its own.
  //
  // Separate from the whole-cue clipboard on purpose. Copying a cue brings its
  // geometry, fades, crop and colour with it, which is not what is wanted when
  // the only thing worth keeping is the look that took twenty minutes to dial
  // in. This moves the chain and nothing else.
  // ---------------------------------------------------------------------------
  void copySelectedEffectChain() {
    const Cue* cue = selectedCueMutable();
    if (!cue) {
      triggerToast("effects copy: select a cue");
      return;
    }
    if (cue->effects.empty()) {
      triggerToast("effects copy: this cue has no effects");
      return;
    }
    effectChainClipboard_ = cue->effects;
    effectChainClipboardDriver_ = cue->motionDriverPath;
    triggerToast("copied " + std::to_string(effectChainClipboard_.size()) +
                 (effectChainClipboard_.size() == 1 ? " effect" : " effects"));
    playUiSound(UiSoundEffect::Navigate);
  }

  void pasteSelectedEffectChain() {
    if (effectChainClipboard_.empty()) {
      triggerToast("effects paste: copy a chain first");
      return;
    }
    Deck& deck = focusedDeckMutable();
    auto indices = selectedCueIndices(deck);
    if (indices.empty()) {
      triggerToast("effects paste: select a cue");
      return;
    }
    pushUndoSnapshot();
    int applied = 0;
    for (int index : indices) {
      if (index < 0 || index >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      Cue& target = deck.cues[index];
      target.effects = effectChainClipboard_;
      // The driver travels with the chain when there is one, because a motion
      // puppet pasted without its driver is an effect that does nothing and
      // gives no reason why.
      if (!effectChainClipboardDriver_.empty()) {
        target.motionDriverPath = effectChainClipboardDriver_;
      }
      // And a chain with no puppet in it leaves no driver behind on the cue
      // it landed on, the same as removing the last puppet by hand.
      if (!deckboy::effects::cueEffectStackNeedsDriver(target.effects)) {
        target.motionDriverPath.clear();
      }
      ++applied;
    }
    if (applied <= 0) {
      triggerToast("effects paste: no targets");
      return;
    }
    // Datamosh is a decode behaviour rather than a pixel one, so a pasted
    // chain containing it has to be reconciled with the cue's own flag, and
    // the decoder has to be reopened in a format the new chain can act on.
    syncDatamoshFromStack();
    refreshFocusedLiveCueRuntimeIfSelected();
    triggerToast(applied == 1
                   ? "effect chain pasted"
                   : ("effect chain pasted x" + std::to_string(applied)));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  // ── VJ mixer + tempo ─────────────────────────────────────────────────────

  // ── WHAT IS WRONG WITH THIS SHOW ──────────────────────────────────────
  //
  // Deckboy has always validated a cue -- at TAKE, which is during the show.
  // "MEDIA MISSING, take blocked" at 20:01 is the correct refusal at the worst
  // possible moment. The same checks, run across the whole show BEFORE doors,
  // are the difference between a surprise and a to-do list.
  //
  // Everything here is derived. Nothing is cached and nothing is stored on the
  // cue, so the list cannot go stale: fix the problem and it leaves.
  struct ShowProblem {
    int deckIndex = -1;
    int cueIndex = -1;
    std::string what;                  // short, operator-facing
  };

  std::vector<ShowProblem> scanShowForProblems() {
    std::vector<ShowProblem> out;
    for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
      Deck& deck = project_.decks[d];
      for (int c = 0; c < static_cast<int>(deck.cues.size()); ++c) {
        Cue& cue = deck.cues[c];

        // The file is gone. The check TAKE already does, asked early.
        if (cueUsesFilesystemMedia(cue)) {
          auto resolved = resolveCueFilesystemPath(cue, currentProjectFile_);
          if (resolved && !resolved->empty()) {
            std::error_code ec;
            if (!fs::exists(*resolved, ec)) {
              out.push_back({d, c, "media missing"});
            }
          }
        }

        // A goto that points at nothing. The cue plays and then the show stops
        // where nobody expected it to, which is the hardest kind to find later.
        const std::string goto_ = trim(cue.gotoTarget);
        if (!goto_.empty() && !cueIndexByTokenInOverlayRole(deck, goto_, false)) {
          out.push_back({d, c, "goto \"" + goto_ + "\" matches no cue"});
        }

        // A master pointing at a cue that has been deleted or moved to another
        // deck. This class of fault did not exist until master cues did, and
        // it is invisible until the master is fired.
        if (cue.kind == CueKind::Master) {
          if (cue.masterAssignments.empty()) {
            out.push_back({d, c, "master fires nothing"});
          }
          for (const auto& a : cue.masterAssignments) {
            if (a.deckIndex < 0 || a.deckIndex >= static_cast<int>(project_.decks.size())) {
              out.push_back({d, c, "master names deck " +
                                   std::to_string(a.deckIndex + 1) +
                                   ", which does not exist"});
            } else if (findCueIndexById(a.deckIndex, a.cueId) < 0) {
              out.push_back({d, c, "master target on deck " +
                                   std::to_string(a.deckIndex + 1) + " is gone"});
            }
          }
        }

        // A script with no lines in it. Not a fault anybody would notice
        // until the cue did nothing on the night.
        if (cue.kind == CueKind::Script && scriptLineCount(cue.scriptText) == 0) {
          out.push_back({d, c, "script has no lines"});
        }

        // A network cue with nowhere to send. The host is checked as an
        // address here rather than at GO for the same reason as everything
        // else on this list: a typo is silent on the night.
        if (cue.kind == CueKind::Network) {
          sockaddr_in probe {};
          const std::string host = trim(cue.netHost);
          if (host.empty()) {
            out.push_back({d, c, "network cue has no host"});
          } else if (inet_pton(AF_INET, host.c_str(), &probe.sin_addr) != 1) {
            out.push_back({d, c, "network cue host \"" + host +
                                 "\" is not an IPv4 address"});
          } else if (toLower(trim(cue.netProtocol)) == "osc" &&
                     (trim(cue.netAddress).empty() ||
                      trim(cue.netAddress).front() != '/')) {
            out.push_back({d, c, "OSC address must start with /"});
          }
        }

        // A MIDI cue whose message cannot be built. Checked here rather than
        // at GO, which is the whole point of the panel: a raw string with a
        // typo in it is silent on the night and obvious before doors.
        if (cue.kind == CueKind::Midi &&
            deckboy::platform::midi::encodeOutMessage(midiMessageForCue(cue)).empty()) {
          out.push_back({d, c, "MIDI cue has nothing to send"});
        }

        // A fade that does nothing: zero length AND already at its value is
        // fine (it is a set), but a fade naming a deck that has gone is not.
        if (cue.kind == CueKind::Fade && cue.fadeWhat != CueFadeWhat::MasterDimmer &&
            cue.targetDeckIndex >= 0 &&
            cue.targetDeckIndex >= static_cast<int>(project_.decks.size())) {
          out.push_back({d, c, "fade names deck " +
                               std::to_string(cue.targetDeckIndex + 1) +
                               ", which does not exist"});
        }

        // A target with nobody to act on, for the same reason.
        if (cue.kind == CueKind::Target) {
          int vd = -1;
          int vi = -1;
          if (cue.targetCueId.empty()) {
            out.push_back({d, c, "target acts on nothing"});
          } else if (!resolveTargetCue(cue, vd, vi)) {
            out.push_back({d, c, std::string(cueTargetVerbLabel(cue.targetVerb)) +
                                 " target is gone"});
          }
        }

        // A DISARMED CUE IS NOT A FAULT -- somebody meant it -- but a whole
        // deck of them is: GO would walk the list and never fire anything.
        // Reported once, on the first cue, rather than once per cue.
      }
      const bool anyArmed = std::any_of(deck.cues.begin(), deck.cues.end(),
                                        [](const Cue& c) { return c.armed; });
      if (!deck.cues.empty() && !anyArmed) {
        out.push_back({d, 0, "every cue on this deck is disarmed"});
      }
    }
    return out;
  }

  // One click, one problem, then the next one. Cycles rather than stopping at
  // the end so the button never becomes inert while faults remain.
  void jumpToNextShowProblem() {
    const std::vector<ShowProblem> problems = scanShowForProblems();
    if (problems.empty()) {
      triggerToast("nothing broken");
      return;
    }
    if (showProblemCursor_ < 0 ||
        showProblemCursor_ >= static_cast<int>(problems.size())) {
      showProblemCursor_ = 0;
    }
    const ShowProblem& p = problems[showProblemCursor_];
    showProblemCursor_ = (showProblemCursor_ + 1) % static_cast<int>(problems.size());
    setFocusedDeckIndex(p.deckIndex);
    selectCueInDeck(p.deckIndex, p.cueIndex, false, false);
    scrollDeckToCueIndex(p.deckIndex, p.cueIndex, false);
    triggerToast(std::to_string(problems.size()) + " to fix — deck " +
                 std::to_string(p.deckIndex + 1) + " cue " +
                 std::to_string(p.cueIndex + 1) + ": " + p.what);
  }

  // ADD A DECK. The app has always carried up to kMaxDecks and only VJ mode
  // could ever create one -- as a side effect, capped at two, so a show that
  // wanted three destinations could not have them unless a file already said
  // so. Master cues made that gap load-bearing: you cannot assign a master to
  // deck 3 if deck 3 cannot exist.
  //
  // This is VJ mode's recipe, generalised rather than copied: name it for its
  // POSITION (Deck's default name is "Deck 1", so a second one added naively is
  // another "Deck 1" in STATUS and in anything a controller labels from it),
  // then rebuild the runtimes so the new deck has an engine.
  //
  // APPEND ONLY, deliberately. Outputs address their source by hostDeckIndex
  // and master cues address their targets by deck index, so removing a deck
  // from the middle would silently repoint both at their neighbours. Removal
  // needs those references remapped and is not in here.
  bool addDeck(bool announce = true) {
    if (static_cast<int>(project_.decks.size()) >= kMaxDecks) {
      if (announce) {
        failRemoteCommand("deck limit is " + std::to_string(kMaxDecks));
      }
      return false;
    }
    Deck added;
    added.name = deckDefaultName(static_cast<int>(project_.decks.size()));
    project_.decks.push_back(added);
    // Tears down and recreates every engine, so it stops playback. Said out
    // loud rather than discovered: this is a setup action, not a show one.
    rebuildDeckRuntimes();
    markProjectDirty();
    if (announce) {
      triggerToast("added " + added.name + " (playback stopped)");
    }
    return true;
  }

  void setVjMode(bool on) {
    if (project_.vjModeEnabled == on) {
      return;
    }
    project_.vjModeEnabled = on;
    // Start the drop-in. Set on both edges so leaving the mode is as visible
    // as entering it.
    vjBarRevealAt_ = animationNow_;
    if (on && project_.decks.size() < 2) {
      // A mixer needs something to mix. Adding the deck here rather than
      // refusing means turning the mode on does what it says on a show file
      // that has only ever had one deck, which is most of them.
      //
      // rebuildDeckRuntimes tears down every engine, so this stops playback --
      // acceptable for a mode switch during setup, and said out loud below so
      // it is not a surprise if someone flips it mid-show.
      // Named for its POSITION. Deck's default name is "Deck 1", so the deck
      // VJ mode added was a second "Deck 1" -- visible in STATUS JSON and in
      // anything a controller labels from it.
      // One deck-creating path, so the naming and the runtime rebuild cannot
      // drift between here and DECKADD.
      addDeck(false);
    }
    const int deckCount = static_cast<int>(project_.decks.size());
    project_.vjDeckA = std::clamp(project_.vjDeckA, 0, std::max(0, deckCount - 1));
    project_.vjDeckB = std::clamp(project_.vjDeckB, 0, std::max(0, deckCount - 1));
    if (on && project_.vjDeckB == project_.vjDeckA && deckCount > 1) {
      project_.vjDeckB = (project_.vjDeckA + 1) % deckCount;
    }
    // Ask every deck for CPU-side pixels while the mixer is up, then re-take
    // the live cues so the change actually reaches the decoders -- the format
    // is chosen when a cue is taken, not per frame.
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (DeckRuntime* runtime = runtimeForDeck(deckIndex)) {
        if (runtime->mediaEngine) {
          runtime->mediaEngine->setForcePixelFrames(on);
        }
      }
    }
    refreshAllLiveCueRuntimes();
    triggerToast(on ? "VJ mode on" : "VJ mode off");
    markProjectDirty();
  }

  void setVjMix(double position) {
    project_.vjMixPosition = std::clamp(position, 0.0, 1.0);
    markProjectDirty();
  }

  void setVjBlend(const std::string& mode) {
    // AGAINST THE SHARED LIST. This carried its own copy of the modes -- the
    // fourth place they were written out -- so a mode the renderer, the cycle
    // and the remote verb all knew about arrived here, failed an `== "add" ||
    // == "multiply"` test written before it existed, and became dissolve. The
    // command still answered OK, so it looked like a mode that does nothing.
    project_.vjBlendMode = isVjBlendMode(mode) ? mode : "dissolve";
    triggerToast("mix: " + project_.vjBlendMode);
    markProjectDirty();
  }

  // TAP TEMPO.
  //
  // Averaged over the recent taps rather than taken from the last interval,
  // because no one taps evenly and a single interval makes the tempo jump
  // around on every beat. Taps more than two seconds apart start a new
  // measurement: that is a person starting again, not a 25bpm track.
  double tapVjTempo() {
    const double now = static_cast<double>(SDL_GetTicks()) / 1000.0;
    if (!vjTapTimes_.empty() && now - vjTapTimes_.back() > 2.0) {
      vjTapTimes_.clear();
    }
    vjTapTimes_.push_back(now);
    if (vjTapTimes_.size() > 8) {
      vjTapTimes_.erase(vjTapTimes_.begin());
    }
    if (vjTapTimes_.size() >= 2) {
      const double span = vjTapTimes_.back() - vjTapTimes_.front();
      const double intervals = static_cast<double>(vjTapTimes_.size() - 1);
      if (span > 0.05) {
        project_.vjTempoBpm = std::clamp(60.0 * intervals / span, 20.0, 300.0);
        // The downbeat is the tap that set the tempo, so the beat grid lines up
        // with the hand that tapped it rather than with when the app started.
        vjBeatOrigin_ = vjTapTimes_.back();
        markProjectDirty();
      }
    }
    return project_.vjTempoBpm;
  }

  void setVjTempo(double bpm) {
    project_.vjTempoBpm = std::clamp(bpm, 20.0, 300.0);
    vjBeatOrigin_ = static_cast<double>(SDL_GetTicks()) / 1000.0;
    markProjectDirty();
  }

  // Where we are between beats, 0 at the beat and approaching 1 just before the
  // next. Derived from a wall clock rather than counted per frame, so it cannot
  // drift when a frame is late.
  // Beats since the tempo origin, FRACTIONAL and running.
  //
  // vjBeatPhase wraps inside one beat, which is what a flashing badge wants and
  // useless to an LFO with a four-beat cycle -- that needs to know which beat
  // it is on, not just where it sits inside the current one.
  double vjBeatCount() const {
    const double bpm = std::clamp(project_.vjTempoBpm, 20.0, 300.0);
    const double beatSeconds = 60.0 / bpm;
    const double now = static_cast<double>(SDL_GetTicks()) / 1000.0;
    return (now - vjBeatOrigin_) / beatSeconds;
  }

  // ONE clock for every LFO in the show.
  //
  // The preview and the output both modulate the stack, and if each read its
  // own clock the operator's monitor would show a slightly different moment of
  // the same oscillator than the audience saw. Sampled once per frame and
  // handed to both.
  void sampleLfoClock() {
    lfoSeconds_ = static_cast<double>(SDL_GetTicks()) / 1000.0;
    lfoBeats_ = vjBeatCount();
  }

  double vjBeatPhase() const {
    const double bpm = std::clamp(project_.vjTempoBpm, 20.0, 300.0);
    const double beatSeconds = 60.0 / bpm;
    const double now = static_cast<double>(SDL_GetTicks()) / 1000.0;
    const double since = now - vjBeatOrigin_;
    const double phase = std::fmod(since, beatSeconds) / beatSeconds;
    return phase < 0.0 ? phase + 1.0 : phase;
  }

  double vjSecondsToNextBeat() const {
    const double bpm = std::clamp(project_.vjTempoBpm, 20.0, 300.0);
    const double beatSeconds = 60.0 / bpm;
    return beatSeconds * (1.0 - vjBeatPhase());
  }

  std::vector<double> vjTapTimes_;
  double vjBeatOrigin_ = 0.0;
  // A take waiting for the downbeat. Quantising is the whole point of tempo in
  // a video mixer: not that anything moves by itself, but that what the
  // operator does lands ON the music instead of a moment after it.
  bool vjTakePending_ = false;
  double vjTakeDueAt_ = 0.0;

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
      // Deckboy's own card rather than someone else's website: a PiP just
      // switched to a browser source should show something that says what it
      // is, and it works with no network.
      if (currentValue == "graphic://pip") {
        currentValue = defaultBrowserPageUrl();
        if (currentValue.empty()) {
          currentValue = "https://example.com";
        }
      }
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

  bool setSelectedTrimIn(double seconds) {
    if (!firstFocusedSelectedCueMutable([&](const Cue& cue) {
      return cueSupportsTrimPoints(cue.kind);
    })) {
      return false;
    }
    double sampleNext = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsTrimPoints(cue.kind)) {
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
      return false;
    }
    triggerToast("in " + formatSeconds(sampleNext));
    markProjectDirty();
    return true;
  }

  bool setSelectedTrimOut(double seconds) {
    if (!firstFocusedSelectedCueMutable([&](const Cue& cue) {
      return cueSupportsTrimPoints(cue.kind);
    })) {
      return false;
    }
    double sampleNext = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsTrimPoints(cue.kind)) {
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
      return false;
    }
    triggerToast("out " + formatSeconds(sampleNext));
    markProjectDirty();
    return true;
  }

  void clearSelectedTrim() {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsTrimPoints(cue.kind)) {
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
    if (!cueSupportsTrimPoints(cue.kind)) {
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
    } else if (deckboyShortcutHeld(mod)) {
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

    // ARM FROM NOW, never from the beginning of time. lastScheduleCheckSeconds_
    // starts at -1, and the crossing test below is "was it before, is it now
    // after" -- so on the first tick after launch EVERY cue scheduled earlier
    // today satisfied both halves and went STRAIGHT TO AIR. Open a show at 2pm
    // and the morning's schedule fired at once, unbidden. A schedule is a
    // promise about the future; a time that has already passed is not one.
    if (lastScheduleCheckSeconds_ < 0.0) {
      lastScheduleCheckSeconds_ = secondsToday;
      return;
    }

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

  // Runtimes are keyed by cue id, so anything driven off a runtime needs the
  // reverse lookup. Linear over the show: cue counts are in the hundreds and
  // this runs once per running timer per frame.
  const Cue* findCueById(const std::string& cueId) const {
    if (cueId.empty()) return nullptr;
    for (const Deck& deck : project_.decks) {
      for (const Cue& cue : deck.cues) {
        if (cue.id == cueId) return &cue;
      }
    }
    return nullptr;
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
      fireTimerChimes(id, rt);
    }
  }

  // A speaker looking at the audience is not looking at the clock, which is
  // the whole reason stage timers chime. Latched per stage so a crossing
  // sounds ONCE, and wound back if the clock is reset or nudged forward again.
  void fireTimerChimes(const std::string& cueId, TimerRuntime& rt) {
    const Cue* cue = findCueById(cueId);
    if (!cue || cue->kind != CueKind::Timer) return;
    const TimerSettings& t = cue->timer;
    if (t.mode == TimerMode::TimeOfDay || t.durationSeconds <= 0) return;
    const double remaining = static_cast<double>(t.durationSeconds) - rt.elapsedSeconds;
    int stage = 0;
    if (remaining <= 0.0)                                    stage = 3;
    else if (remaining <= static_cast<double>(t.redSeconds))   stage = 2;
    else if (remaining <= static_cast<double>(t.amberSeconds)) stage = 1;
    if (stage < rt.chimedStage) {
      rt.chimedStage = stage;   // wound back: re-arm the chimes above it
      return;
    }
    if (stage == rt.chimedStage) return;
    rt.chimedStage = stage;
    const bool wanted = (stage == 1 && t.chimeAtAmber)
                     || (stage == 2 && t.chimeAtRed)
                     || (stage == 3 && t.chimeAtZero);
    if (!wanted) return;
    playTimerChime(t.chimeSound, stage);
    showLog("TIMER CHIME", cue->name + (stage == 3 ? " time up" :
                                        stage == 2 ? " red" : " amber"));
  }

  // Edit a timer setting on the SELECTED cue (not the live one): these are
  // authoring changes, unlike the run/reset/nudge controls which target
  // whatever clock is on air.
  void toggleTimerFlag(bool TimerSettings::*field, const char* label) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Timer) return;
    bool& v = cue->timer.*field;
    v = !v;
    markProjectDirty();
    triggerToast(std::string(label) + (v ? ": on" : ": off"));
    playUiSound(UiSoundEffect::Toggle);
  }

  void pickTimerLogo() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Timer) {
      failRemoteCommand("logo: select a timer cue");
      return;
    }
    // Static so the filter list outlives the call: SDL requires it stay valid
    // until the callback fires, which is on another thread and later.
    // Static: SDL requires the filter array stay valid until the callback
    // fires, which happens later and on another thread.
    static const std::vector<SDL_DialogFileFilter> kLogoFilters {
      {"Images", "png;jpg;jpeg;bmp;gif;webp"},
      {"All files", "*"},
    };
    showOpenFileDialog(kLogoFilters, /*allowMany=*/false,
                       [this](std::vector<std::string> chosen) {
      if (chosen.empty() || chosen.front().empty()) return;
      Cue* c = selectedCueMutable();
      if (!c || c->kind != CueKind::Timer) return;
      c->timer.logoPath = chosen.front();
      markProjectDirty();
      triggerToast("timer logo set");
    });
  }

  void clearTimerLogo() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Timer) return;
    if (cue->timer.logoPath.empty()) return;
    cue->timer.logoPath.clear();
    markProjectDirty();
    triggerToast("timer logo cleared");
    playUiSound(UiSoundEffect::Toggle);
  }

  void cycleTimerChimeSound() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Timer) return;
    cue->timer.chimeSound = (cue->timer.chimeSound + 1) % 6;
    markProjectDirty();
    triggerToast(std::string("chime: ") + timerChimeName(cue->timer.chimeSound));
    // PLAY it. Choosing a chime by name is guesswork; the operator needs to
    // hear the one that will fire in the room.
    playTimerChime(cue->timer.chimeSound, 1);
  }

  // Named swatches rather than a colour picker: a stage clock wants a handful
  // of high-contrast choices, and cycling is faster than a picker mid-show.
  static const std::array<std::pair<int, const char*>, 8>& timerColorSwatches() {
    static const std::array<std::pair<int, const char*>, 8> kSwatches {{
      {-1,       "default"},
      {0xFFFFFF, "white"},
      {0x00FF66, "green"},
      {0xFFC400, "amber"},
      {0xFF4040, "red"},
      {0x40A0FF, "blue"},
      {0xFF40C0, "pink"},
      {0x000000, "black"},
    }};
    return kSwatches;
  }

  std::string timerColorLabel(int packed) const {
    for (const auto& s : timerColorSwatches()) {
      if (s.first == packed) return s.second;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%06X", packed & 0xFFFFFF);
    return buf;
  }

  void cycleTimerColor(int TimerSettings::*field, const char* label) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Timer) return;
    const auto& sw = timerColorSwatches();
    int idx = 0;
    for (std::size_t i = 0; i < sw.size(); ++i) {
      if (sw[i].first == cue->timer.*field) { idx = static_cast<int>(i); break; }
    }
    idx = (idx + 1) % static_cast<int>(sw.size());
    cue->timer.*field = sw[idx].first;
    markProjectDirty();
    triggerToast(std::string(label) + ": " + sw[idx].second);
    playUiSound(UiSoundEffect::Toggle);
  }

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

  // The clock's face, from anywhere on the machine. Clearing it goes back to
  // the app's own bundled sans, which is what makes the typeface mode work
  // before anybody has picked anything -- and keeps a show looking the same on
  // all three platforms, the same argument that had Liberation bundled at all.
  void pickTimerFont() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Timer) return;
    if (!cue->timer.fontPath.empty()) {
      cue->timer.fontPath.clear();
      markProjectDirty();
      triggerToast("timer font: the app's own");
      return;
    }
    // Static, because SDL needs the filter array to outlive the callback --
    // the trap the file-dialog notes warn about.
    static const std::vector<SDL_DialogFileFilter> kTimerFontFilters {
      {"Fonts", "ttf;otf;ttc;otc"},
      {"All files", "*"},
    };
    showOpenFileDialog(kTimerFontFilters, /*allowMany=*/false,
                       [this](std::vector<std::string> picked) {
      if (picked.empty()) return;
      Cue* target = selectedCueMutable();
      if (!target || target->kind != CueKind::Timer) return;
      target->timer.fontPath = picked.front();
      // Picking a font is asking for it to be USED. Leaving the face on
      // seven-segment would store the choice and show none of it.
      target->timer.face = TimerFace::Typeface;
      markProjectDirty();
      triggerToast("timer font: " + fs::path(picked.front()).filename().string());
    });
  }

  void cycleTimerFace() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Timer) return;
    cue->timer.face = cue->timer.face == TimerFace::SevenSegment ? TimerFace::Blocky
                    : cue->timer.face == TimerFace::Blocky        ? TimerFace::Typeface
                                                                  : TimerFace::SevenSegment;
    triggerToast(cue->timer.face == TimerFace::Blocky ? "timer face: blocky"
                 : cue->timer.face == TimerFace::Typeface
                     ? ("timer face: " + (cue->timer.fontPath.empty()
                          ? std::string("typeface (the app's own)")
                          : fs::path(cue->timer.fontPath).filename().string()))
                     : std::string("timer face: 7-segment"));
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
    rt.chimedStage = 0;   // re-arm: a restarted talk must chime again
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
