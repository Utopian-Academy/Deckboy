// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
//
// ═══════════════════════════════════════════════════════════════════════════════
// app_update.ipp — Per-Frame Update & Event Processing
// ═══════════════════════════════════════════════════════════════════════════════
//
// The main loop in App::run() calls processEvents() → update() → render()
// every frame. This file implements all three, keeping the run-loop body in
// main.cpp minimal.
//
// ─── processEvents() ────────────────────────────────────────────────────────
//   Drains the SDL event queue in a single pass. Each event type is dispatched
//   to the appropriate handler:
//
//     SDL_QUIT / WINDOWEVENT_CLOSE — sets gShouldQuit or closes a secondary
//       window (monitors window, output window). Closing the control window
//       always exits the app immediately with no confirmation trap.
//     SDL_DROPFILE       — forwards to handleDropFile() for cue import.
//     SDL_MOUSEWHEEL     — dropdown scroll, cue inspector scroll, overlay
//                          list scroll, or primary cue list scroll (in that
//                          priority order, based on mouse position hit-test).
//     SDL_MOUSEBUTTONDOWN — inline text editor → dropdown → right-click
//                          (settings click or context menu) → context menu
//                          click → general mouse-down handler. Monitors
//                          window clicks are handled separately.
//     SDL_MOUSEBUTTONUP  — resets all drag states: cue drag, trim drag,
//                          timeline scrub (resumes playback if scrubbing
//                          interrupted it), warp corner drag, layout drag.
//     SDL_MOUSEMOTION    — updates mouse position and forwards to
//                          handleMouseMotion().
//     SDL_KEYDOWN        — accepted from the control window always, or from
//                          output windows only for Escape (to close output).
//     SDL_TEXTINPUT      — forwarded to the inline text editor.
//     SDL_DISPLAYEVENT   — updates observed display count and triggers
//                          topology refresh on connect/disconnect.
//
// ─── update() ───────────────────────────────────────────────────────────────
//   Per-frame state tick. Runs every frame regardless of whether events
//   arrived. Major subsystems updated (in order):
//
//     1. Project state: flushDirtyProject(), processRemoteCommands()
//     2. Integration runtimes: NMC sync, NDI trigger bridge, LTC capture
//     3. Async futures: media probe results → cue metadata fill-in,
//        waveform analysis results → cache, VU meter sample cleanup
//     4. Waveform & timeline strip: trigger analysis for selected/active cue
//     5. Splash overlay: auto-dismiss after 2.6 seconds
//     6. Delta time: computed from SDL_GetTicks64 for animation stepping
//     7. Display topology: polled every 1.2s, refresh on change
//     8. Output recovery: polled every 1s per output
//     9. Timecode follower: ensure state arrays are sized, advance run-mode
//        timecode with freewheel timeout, fire cue triggers if chase+trigger
//        enabled
//    10. Master dimmer: smooth ramp toward target (panic or normal speed),
//        triggers finishClearOutput() when dimmer reaches black during
//        fade-to-clear, completes panic profile when faded or timed out
//    11. Playlist opacity: per-deck smooth ramp toward target with
//        configurable fade time
//    12. Media engines: per-deck engine->update(), pattern cue animation
//        rebuild, PiP overlay runtime sync, auto-advance on cue end
//        (handles goto targets, shuffle, playlist loop, transition-to-next)
//    13. NMC sync output: tickNmcSyncOutput()
//    14. Preview engine: next-cue preview for monitors window (currently
//        disabled behind kShowNextPreviewMonitor flag)
//    15. Status snapshot: updateStatusSnapshot() for OSC feedback
//    16. Texture uploads: control preview frame, monitors captured frames,
//        thumbnail decode result, timeline strip decode result
//
// ─── render() ───────────────────────────────────────────────────────────────
//   Dispatches rendering to all visible windows:
//     • renderControlWindow()  — main UI (see app_render_main.ipp)
//     • renderMonitorsWindow() — multi-output monitor view (see app_overlays.ipp)
//     • ensureOutputRuntimesSynced() — creates/destroys output runtimes to
//       match project_.outputs
//     • renderOutputWindow()   — per-output compositor (see app_render_output.ipp)
// ═══════════════════════════════════════════════════════════════════════════════

  void processEvents() {
    SDL_Event event {};
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT:
          // OS/app close request should quit immediately.
          gShouldQuit.store(true);
          break;
        case SDL_WINDOWEVENT:
          if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
            Uint32 closingWindowId = event.window.windowID;
            if (closingWindowId == SDL_GetWindowID(controlWindow_)) {
              // Closing the main window should exit immediately (no hidden confirm trap).
              gShouldQuit.store(true);
              break;
            }
            if (monitorsWindow_ && closingWindowId == SDL_GetWindowID(monitorsWindow_)) {
              setMonitorsVisible(false);
              break;
            }
            if (auto outputIndex = outputIndexForWindowId(closingWindowId); outputIndex) {
              setFocusedOutputIndex(*outputIndex);
              setFocusedOutputEnabled(false, false);
              break;
            }
          }
          break;
        case SDL_DROPFILE:
          handleDropFile(event.drop.file);
          SDL_free(event.drop.file);
          break;
        case SDL_MOUSEWHEEL:
          if (event.wheel.windowID == SDL_GetWindowID(controlWindow_)) {
            if (handleDropdownMouseWheel(event.wheel.y)) {
              break;
            }
            if (settingsOpen_ && settingsTab_ == 3 &&
                settingsVideoViewport_.w > 0 && settingsVideoViewport_.h > 0 &&
                pointInRect(mouseX_, mouseY_, settingsVideoViewport_) &&
                settingsVideoScrollMax_ > 0) {
              settingsVideoScroll_ = std::clamp(
                settingsVideoScroll_ - event.wheel.y * 36,
                0, settingsVideoScrollMax_);
              break;
            }
            if (cueSettingsViewportRect_.w > 0 && cueSettingsViewportRect_.h > 0 &&
                pointInRect(mouseX_, mouseY_, cueSettingsViewportRect_) &&
                cueSettingsScrollMax_ > 0) {
              cueSettingsScroll_ = std::clamp(
                cueSettingsScroll_ - event.wheel.y * 36,
                0,
                cueSettingsScrollMax_);
              break;
            }
            for (int di = 0; di < static_cast<int>(deckColumnRects_.size()); ++di) {
              if (di < static_cast<int>(deckOverlayClipRects_.size()) &&
                  pointInRect(mouseX_, mouseY_, deckOverlayClipRects_[di])) {
                if (di < static_cast<int>(deckOverlayScrolls_.size())) {
                  deckOverlayScrolls_[di] = std::max(0, deckOverlayScrolls_[di] - event.wheel.y * 36);
                }
                break;
              }
              if (di < static_cast<int>(deckListClipRects_.size()) &&
                  pointInRect(mouseX_, mouseY_, deckListClipRects_[di])) {
                if (di < static_cast<int>(deckScrolls_.size())) {
                  deckScrolls_[di] = std::max(0, deckScrolls_[di] - event.wheel.y * 36);
                }
                break;
              }
            }
          }
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (event.button.windowID == SDL_GetWindowID(controlWindow_)) {
            if (handleInlineTextEditorMouseDown(event.button.x, event.button.y)) {
              break;
            }
            if (handleDropdownMouseDown(event.button.x, event.button.y)) {
              break;
            }
            if (event.button.button == SDL_BUTTON_RIGHT) {
              if (settingsOpen_) {
                handleSettingsClick(event.button.x, event.button.y);
              } else {
                handleRightClick(event.button.x, event.button.y);
              }
            } else {
              if (contextMenuOpen_) {
                handleContextMenuClick(event.button.x, event.button.y);
              } else {
                handleMouseDown(event.button.x, event.button.y, event.button.button);
              }
            }
          } else if (monitorsWindow_ &&
                     event.button.windowID == SDL_GetWindowID(monitorsWindow_)) {
            handleMonitorsMouseDown(event.button.x, event.button.y);
          }
          break;
        case SDL_MOUSEBUTTONUP:
          if (event.button.windowID == SDL_GetWindowID(controlWindow_)) {
              drag_.active = false;
              drag_.cueIndex = -1;
              trimDragMode_ = TrimDragMode::None;
              if (timelineScrubActive_ && scrubWasPlaying_) {
                if (MediaEngine* engine = focusedMediaEngine()) {
                  engine->play();
                }
                scrubWasPlaying_ = false;
              }
              timelineScrubActive_ = false;
              warpDragCorner_ = -1;
              layoutDragMode_ = LayoutDragMode::None;
            }
          break;
        case SDL_MOUSEMOTION:
          if (event.motion.windowID == SDL_GetWindowID(controlWindow_)) {
            mouseX_ = event.motion.x;
            mouseY_ = event.motion.y;
            handleMouseMotion(event.motion.x, event.motion.y);
          }
          break;
        case SDL_KEYDOWN:
          {
            Uint32 controlWindowId = controlWindow_ ? SDL_GetWindowID(controlWindow_) : 0;
            bool fromControlWindow = controlWindowId != 0 && event.key.windowID == controlWindowId;
            bool fromOutputWindow = outputIndexForWindowId(event.key.windowID).has_value();
            bool allowFromOutputWindow = fromOutputWindow && event.key.keysym.sym == SDLK_ESCAPE;
            if (fromControlWindow || allowFromOutputWindow) {
              handleKeyDown(event.key.keysym.sym, event.key.keysym.mod, event.key.windowID, event.key.repeat != 0);
            }
          }
          break;
        case SDL_TEXTINPUT:
          if (event.text.windowID == SDL_GetWindowID(controlWindow_)) {
            handleInlineTextEditorTextInput(event.text.text);
          }
          break;
        case SDL_DISPLAYEVENT:
#if defined(SDL_DISPLAYEVENT_CONNECTED) && defined(SDL_DISPLAYEVENT_DISCONNECTED)
          if (event.display.event == SDL_DISPLAYEVENT_CONNECTED ||
              event.display.event == SDL_DISPLAYEVENT_DISCONNECTED) {
            observedDisplayCount_ = SDL_GetNumVideoDisplays();
            refreshDisplayTopology(true);
          }
#else
          observedDisplayCount_ = SDL_GetNumVideoDisplays();
          refreshDisplayTopology(true);
#endif
          break;
        default:
          break;
      }
    }
  }

  void update() {
    flushDirtyProject();
    processRemoteCommands();
    refreshNmcSyncState();
    refreshNdiTriggerBridgeState();
    refreshLtcCaptureState();
    Uint64 now = SDL_GetTicks64();
    // Poll async cue probe futures
    for (auto it = probeFutures_.begin(); it != probeFutures_.end(); ) {
      if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          auto probed = it->future.get();
          if (probed && it->deckIndex >= 0 && it->deckIndex < static_cast<int>(project_.decks.size())) {
            Deck& deck = project_.decks[it->deckIndex];
            for (auto& cue : deck.cues) {
              if (cue.path == it->path && cue.width == 0 && cue.height == 0) {
                // Update placeholder with probed metadata
                cue.duration = probed->duration;
                cue.width = probed->width;
                cue.height = probed->height;
                cue.fps = probed->fps;
                cue.formatName = probed->formatName;
                cue.videoCodec = probed->videoCodec;
                cue.audioCodec = probed->audioCodec;
                cue.hasAudio = probed->hasAudio;
                cue.audioChannels = probed->audioChannels;
                cue.audioSampleRate = probed->audioSampleRate;
                cue.sizeBytes = probed->sizeBytes;
                cue.kind = probed->kind;
                if (cue.hasAudio && !cue.audioEnabled) {
                  const auto& defs = deck;
                  cue.audioEnabled = defs.playlistDefaultAudioEnabled;
                }
                if (isDefaultStillDurationCueKind(cue.kind) && cue.stillDurationSeconds <= 0.0) {
                  cue.stillDurationSeconds = std::clamp(deck.playlistDefaultStillDurationSeconds, 0.0, 3600.0);
                }
                markProjectDirty();
                break;
              }
            }
          }
        } catch (...) {
          triggerToast("media probe failed");
        }
        it = probeFutures_.erase(it);
      } else {
        ++it;
      }
    }
    // Poll waveform analysis futures
    {
      std::lock_guard<std::mutex> lk(waveformMutex_);
      for (auto it = waveformFutures_.begin(); it != waveformFutures_.end(); ) {
        if (it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
          try {
            WaveformPeaks result = it->second.get();
            if (!result.empty()) {
              waveformCache_[it->first] = std::move(result);
            }
          } catch (...) {
          }
          it = waveformFutures_.erase(it);
        } else ++it;
      }
    }
    // Clear VU samples when focused deck is not playing (prevents meter sticking)
    {
      const MediaEngine* engine = mediaEngineForDeck(project_.focusedDeckIndex);
      if (!engine || engine->state() != TransportState::Playing) {
        std::lock_guard<std::mutex> lk(vuSamplesMutex_);
        vuSamples_.clear();
      }
    }
    // Trigger waveform analysis for selected/active cue
    {
      const Cue* sel = selectedCuePtr();
      if (sel && sel->hasAudio) triggerWaveformAnalysis(resolvedCueFilesystemPathString(*sel, currentProjectFile_));
      const Cue* act = activeCuePtr();
      if (act && act->hasAudio && act != sel) triggerWaveformAnalysis(resolvedCueFilesystemPathString(*act, currentProjectFile_));
      const Cue* timelineCue = act ? act : sel;
      bool shouldRequestTimelineStrip = timelineCue && timelineCue->kind == CueKind::Video;
      if (shouldRequestTimelineStrip && timelineCue == sel && timelineCue != act) {
        shouldRequestTimelineStrip = now - selectionChangedAt_ >= 140;
      }
      if (shouldRequestTimelineStrip) {
        requestTimelineStrip(*timelineCue);
      } else if (!timelineCue || timelineCue->kind != CueKind::Video) {
        clearTimelineStrip();
      }
    }
    if (showSplashOverlay_ && splashStartedAt_ > 0 && now - splashStartedAt_ > 2600) {
      showSplashOverlay_ = false;
    }
    double deltaSeconds = lastUpdateTickMs_ == 0 ? 0.0 : static_cast<double>(now - lastUpdateTickMs_) / 1000.0;
    lastUpdateTickMs_ = now;

    if (now - lastDisplayPollMs_ >= 1200) {
      lastDisplayPollMs_ = now;
      int displayCount = SDL_GetNumVideoDisplays();
      if (observedDisplayCount_ < 0) {
        observedDisplayCount_ = displayCount;
      } else if (displayCount != observedDisplayCount_) {
        observedDisplayCount_ = displayCount;
        refreshDisplayTopology(true);
      }
    }

    tickPendingOutputDisplayTransitions(now);

    if (now - lastOutputRecoveryPollMs_ >= 1000) {
      lastOutputRecoveryPollMs_ = now;
      for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
        recoverWindowOutputIfNeeded(outputIndex, false);
      }
    }

    ensureTimecodeFollowerStateSize();
    ensureDeckOpacityTargetsSize();

    // Animate master video dimmer toward target.
    if (std::abs(project_.masterDimmer - masterDimmerTarget_) > 0.001) {
      double dimDuration = panicProfilePending_
        ? std::clamp(project_.panicFadeSeconds, 0.1, 5.0)
        : 0.5;
      double dimSpeed = 1.0 / std::max(0.05, dimDuration);
      double step = dimSpeed * std::max(deltaSeconds, 1.0 / 120.0);
      project_.masterDimmer = std::clamp(
        project_.masterDimmer + std::copysign(std::min(step, std::abs(masterDimmerTarget_ - project_.masterDimmer)), masterDimmerTarget_ - project_.masterDimmer),
        0.0, 1.0);
    }
    // Complete pending clear-with-fade once dimmer reaches black
    if (pendingClearAfterFade_ && project_.masterDimmer <= 0.02) {
      finishClearOutput();
    }
    if (panicProfilePending_) {
      bool faded = project_.masterDimmer <= 0.05;
      Uint64 timeoutMs = static_cast<Uint64>(
        std::llround(std::clamp(project_.panicFadeSeconds, 0.1, 5.0) * 1000.0 + 150.0));
      bool timeout = panicProfileRequestedAt_ > 0 && (now - panicProfileRequestedAt_) >= timeoutMs;
      if (faded || timeout) {
        panicProfilePending_ = false;
        panicProfileRequestedAt_ = 0;
        executePanicDeckAction(pendingPanicProfileToken_);
      }
    }

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      Deck& deck = project_.decks[deckIndex];
      float target = deckPlaylistOpacityTarget(deckIndex);
      if (std::fabs(deck.playlistOpacity - target) <= 0.001f) {
        deck.playlistOpacity = target;
        continue;
      }
      double fadeTime = std::max(0.05, deck.playlistFadeSeconds);
      double speed = 1.0 / fadeTime;
      double step = speed * std::max(deltaSeconds, 1.0 / 120.0);
      float delta = static_cast<float>(std::copysign(
        std::min(step, static_cast<double>(std::fabs(deck.playlistOpacity - target))),
        static_cast<double>(target - deck.playlistOpacity)));
      deck.playlistOpacity = std::clamp(deck.playlistOpacity + delta, 0.0f, 1.0f);
    }

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      Deck& deck = project_.decks[deckIndex];
      double fromTc = deck.timecodeCurrentSeconds;
      if (deck.timecodeDirty) {
        fromTc = deck.timecodeLastSeconds;
      }
      bool shouldRunTimecode = deck.timecodeRunEnabled && deltaSeconds > 0.0;
      if (shouldRunTimecode && deck.timecodeChaseEnabled &&
          deckIndex >= 0 && deckIndex < static_cast<int>(deckTimecodeHasExternal_.size())) {
        if (!deckTimecodeHasExternal_[deckIndex]) {
          shouldRunTimecode = false;
        } else {
          Uint64 lastExternalMs = deckTimecodeLastExternalMs_[deckIndex];
          Uint64 ageMs = now >= lastExternalMs ? (now - lastExternalMs) : 0;
          Uint64 freewheelMs = static_cast<Uint64>(
            std::llround(std::max(0.0, deck.timecodeFreewheelSeconds) * 1000.0));
          if (ageMs > freewheelMs) {
            shouldRunTimecode = false;
          }
        }
      }
      if (shouldRunTimecode) {
        if (deck.timecodeDirty) {
          fromTc = deck.timecodeLastSeconds;
        } else {
          fromTc = deck.timecodeCurrentSeconds;
        }
        deck.timecodeCurrentSeconds = std::max(0.0, deck.timecodeCurrentSeconds + deltaSeconds);
      }

      if (deck.timecodeChaseEnabled && deck.timecodeTriggerEnabled) {
        processTimecodeTriggersForDeck(deckIndex, fromTc, deck.timecodeCurrentSeconds);
      }
      deck.timecodeLastSeconds = deck.timecodeCurrentSeconds;
      deck.timecodeDirty = false;
    }

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      // Advance browser cue Xvfb startup state machine.
      tickBrowserStartup(deckIndex);

      MediaEngine* engine = mediaEngineForDeck(deckIndex);
      if (!engine) {
        continue;
      }
      engine->update();

      // Animate pattern cues: rebuild frame every tick using wall-clock time.
      const Cue* activeCue = activeCuePtr(deckIndex);
      if (activeCue && activeCue->kind == CueKind::Pattern) {
        // Only animated patterns need continuous rebuilds.
        if (patternTypeIsAnimated(activeCue->path)) {
          engine->rebuildPatternFrame(*activeCue, static_cast<double>(now) / 1000.0);
        }
      }
      syncPipOverlayRuntimesForDeck(deckIndex, now);
      if (engine->reachedEnd()) {
        Deck& deck = project_.decks[deckIndex];
        bool keepEndedFrameVisible = false;
        if (deck.activeIndex >= 0 && !deck.cues.empty()) {
          const Cue& activeCue = deck.cues[deck.activeIndex];

          // Cue end behavior follows the cue itself: hold = hold, hold off = next.
          bool shouldAdvance = cueAdvancesWhenFinished(activeCue);

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
                nextIndex = shuffleChoices[std::rand() % shuffleChoices.size()];
              } else if (deck.playlistLoop && playableIndices.size() == 1) {
                nextIndex = playableIndices.front();
              }
            } else {
              nextIndex = adjacentCueIndexForOverlayRole(deck, deck.activeIndex, 1, false, deck.playlistLoop);
            }
          }

          if (nextIndex >= 0) {
            if (deck.selectedIndex != nextIndex) {
              deck.selectedIndex = nextIndex;
              if (deckIndex == project_.focusedDeckIndex) {
                onSelectionChanged();
              }
            }
            markProjectDirty();
            if (shouldAdvance) {
              keepEndedFrameVisible = true;
              int previousFocus = project_.focusedDeckIndex;
              project_.focusedDeckIndex = deckIndex;
              // Honor the incoming cue's fadeInSeconds: the per-cue fade ramp
              // is the visible transition on the output path (the legacy
              // crossfade inside MediaEngine::render() is not visible on
              // output — see v0.76.4 / v0.76.7 DEVNOTES).
              takeSelected(true, activeCue.transitionToNext, false);
              project_.focusedDeckIndex = previousFocus;
            }
          }
        }
        engine->finalizeReachedEnd(keepEndedFrameVisible);
      }
    }
    tickNmcSyncOutput();
    {
      constexpr bool kShowNextPreviewMonitor = false;
      if (!kShowNextPreviewMonitor) {
        if (previewMediaEngine_ && previewMediaEngine_->activeCue()) {
          previewMediaEngine_->stopAll();
        }
        previewCueKey_.clear();
        clearPreviewCueTexture();
      } else {
      int previewCueIndex = -1;
      const Cue* previewCue = previewCuePtr(project_.focusedDeckIndex, &previewCueIndex);
      const Cue* previewRenderCue = previewCue;
      previewResolvedCueValid_ = false;
      if (previewCue && previewCue->kind == CueKind::Pip &&
          project_.focusedDeckIndex >= 0 &&
          project_.focusedDeckIndex < static_cast<int>(project_.decks.size())) {
        const Deck& previewDeck = project_.decks[project_.focusedDeckIndex];
        if (buildResolvedPipSourceCue(previewDeck, *previewCue, previewResolvedCue_, &previewCueIndex)) {
          previewResolvedCueValid_ = true;
          previewRenderCue = &previewResolvedCue_;
        } else {
          previewRenderCue = nullptr;
        }
      }
      if (!previewMediaEngine_ || !previewRenderCue || !cueSupportsMonitorPreview(*previewRenderCue)) {
        if (previewMediaEngine_ && previewMediaEngine_->activeCue()) {
          previewMediaEngine_->stopAll();
        }
        previewCueKey_.clear();
        clearPreviewCueTexture();
      } else {
        std::string previewKey = cuePreviewCacheKey(*previewRenderCue);
        if (previewCue && previewCue->kind == CueKind::Pip) {
          previewKey = cueRuntimeCacheKey(*previewCue) + "|" + previewKey;
        }
        if (previewCueKey_ != previewKey) {
          previewCueKey_ = previewKey;
          clearPreviewCueTexture();
          previewMediaEngine_->loadCue(previewRenderCue, false);
        }
        previewMediaEngine_->update();
        const DecodedFrame* previewFrame = previewMediaEngine_->currentFrame();
        if (previewFrame && previewFrame->width > 0 && previewFrame->height > 0 &&
            (!previewCueTex_ ||
             previewCueFrameIdx_ != previewFrame->index ||
             previewCueTexW_ != previewFrame->width ||
             previewCueTexH_ != previewFrame->height)) {
          uploadPreviewCueTexture(*previewFrame);
          previewCueFrameIdx_ = previewFrame->index;
        } else if (!previewFrame) {
          clearPreviewCueTexture();
        }
      }
      }
    }
    updateStatusSnapshot();
    // Update control window preview texture from focused engine's current frame.
    {
      const MediaEngine* eng = focusedMediaEngine();
      const DecodedFrame* frame = eng ? eng->currentFrame() : nullptr;
      if (frame && frame->width > 0 && frame->height > 0 &&
          frame->index != controlPreviewFrameIdx_) {
        controlPreviewFrameIdx_ = frame->index;
        syncFrameTexture(controlRenderer_, controlPreviewTex_,
                         controlPreviewTexW_, controlPreviewTexH_,
                         controlPreviewTexFormat_, *frame);
      } else if (!frame) {
        // Clear preview when nothing is loaded
        if (controlPreviewTex_) {
          SDL_DestroyTexture(controlPreviewTex_);
          controlPreviewTex_ = nullptr;
          controlPreviewTexW_ = 0;
          controlPreviewTexH_ = 0;
          controlPreviewTexFormat_ = 0;
        }
        controlPreviewFrameIdx_ = static_cast<std::uint64_t>(-1);
      }
    }
    // Upload output captured frames to monitors window textures
    if (monitorsRenderer_ && monitorsVisible()) {
      int rCount = static_cast<int>(outputRuntimes_.size());
      if (static_cast<int>(monitorsOutputTextures_.size()) < rCount) {
        monitorsOutputTextures_.resize(rCount, nullptr);
        monitorsOutputTexW_.resize(rCount, 0);
        monitorsOutputTexH_.resize(rCount, 0);
      }
      for (int oi = 0; oi < rCount; ++oi) {
        const auto& cf = outputRuntimes_[oi].latestCapturedFrame;
        if (cf.pixels.empty()) continue;
        syncTexture(monitorsRenderer_, monitorsOutputTextures_[oi],
                    monitorsOutputTexW_[oi], monitorsOutputTexH_[oi],
                    cf.width, cf.height,
                    cf.pixels.data(), cf.width * 4);
      }
    }
    // Upload thumbnail if one finished decoding
    if (thumbnailPending_.exchange(false)) {
      std::lock_guard<std::mutex> lk(thumbnailMutex_);
      if (pendingThumbnail_) {
        const auto& f = *pendingThumbnail_;
        syncTexture(controlRenderer_, selectedThumbnailTex_,
                    selectedThumbnailTexW_, selectedThumbnailTexH_,
                    f.width, f.height,
                    f.pixels.data(), f.width * 4);
        pendingThumbnail_.reset();
      }
    }
    if (timelineStripPending_.exchange(false)) {
      std::lock_guard<std::mutex> lk(timelineStripMutex_);
      if (pendingTimelineStrip_) {
        const auto& f = *pendingTimelineStrip_;
        syncTexture(controlRenderer_, timelineStripTex_,
                    timelineStripTexW_, timelineStripTexH_,
                    f.width, f.height,
                    f.pixels.data(), f.width * 4);
        timelineStripTexReadyTiles_ = std::clamp(pendingTimelineStripReadyTiles_, 0, kTimelineStripThumbCount);
        pendingTimelineStrip_.reset();
        pendingTimelineStripReadyTiles_ = 0;
      }
    }
  }

  void render() {
    animationNow_ = SDL_GetTicks64();
    renderControlWindow();
    renderMonitorsWindow();
    
    ensureOutputRuntimesSynced();

    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (!project_.outputs[outputIndex].enabled) {
        continue;
      }
      renderOutputWindow(outputIndex);
    }
  }
