// ============================================================================
// app_input.ipp — Keyboard and mouse input handling for the App class.
//
// Processes SDL events and dispatches them to the appropriate handlers:
//
//   Keyboard:
//     - Global hotkeys (Space=play/stop, Esc=stop, Enter=go-next, etc.)
//     - Modifier-aware shortcuts (Ctrl+S=save, Ctrl+Z=undo, Ctrl+K=quick action)
//     - Settings modal keyboard navigation
//     - Inline text editor input forwarding
//
//   Mouse:
//     - Timeline scrubbing (seekFocusedTimelineFraction)
//     - Button click dispatch (transport controls, cue list, settings)
//     - Drag operations (panel resize, slider adjustment)
//     - Right-click context menus
//
//   Other:
//     - Window focus/resize events
//     - Drag-and-drop file import
//     - SDL_QUIT handling with unsaved-changes confirmation
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Seek the focused deck's timeline to a fractional position (0.0–1.0).
  // Handles in/out point zoom: when trim points are set, the fraction maps
  // to the visible (trimmed) range, not the full media duration.
  bool seekFocusedTimelineFraction(double clampedFraction, bool audioScrub = false) {
    MediaEngine* engine = focusedMediaEngine();
    if (!engine) {
      return false;
    }
    if (const Cue* cue = activeCuePtr()) {
      if ((cue->kind == CueKind::Video || cue->kind == CueKind::Audio) && cue->duration > 0.0) {
        double cueIn = std::clamp(cue->inPointSeconds, 0.0, cue->duration);
        double cueOut = cue->outPointSeconds > 0.0
          ? std::clamp(cue->outPointSeconds, cueIn, cue->duration)
          : cue->duration;
        bool zoomedToTrim = cueIn > 0.001 || cueOut < cue->duration - 0.001;
        double visibleDuration = zoomedToTrim ? std::max(0.01, cueOut - cueIn) : cue->duration;
        double mediaTargetSeconds = zoomedToTrim
          ? cueIn + (visibleDuration * clampedFraction)
          : cue->duration * clampedFraction;
        double relativeSeekSeconds = std::clamp(mediaTargetSeconds - cueIn, 0.0, std::max(0.0, engine->duration()));
        engine->seek(relativeSeekSeconds);
        if (audioScrub) {
          if (auto* runtime = focusedRuntime(); runtime && runtime->audioDevice != 0) {
            SDL_PauseAudioDevice(runtime->audioDevice, 0);
          }
        }
        return true;
      }
    }
    if (engine->duration() <= 0.0) {
      return false;
    }
    engine->seek(engine->duration() * clampedFraction);
    if (audioScrub) {
      if (auto* runtime = focusedRuntime(); runtime && runtime->audioDevice != 0) {
        SDL_PauseAudioDevice(runtime->audioDevice, 0);
      }
    }
    return true;
  }

  bool seekFocusedTimelineAtX(int x, bool audioScrub = false) {
    if (progressBarRect_.w <= 0) {
      return false;
    }
    double fraction = static_cast<double>(x - progressBarRect_.x) / static_cast<double>(progressBarRect_.w);
    return seekFocusedTimelineFraction(std::clamp(fraction, 0.0, 1.0), audioScrub);
  }

  void handleMouseDown(int x, int y, Uint8 button) {
    if (showSplashOverlay_) {
      showSplashOverlay_ = false;
      return;
    }
    if (showStartupDialog_) {
      bool hasSavedFile = !currentProjectFile_.empty() && fs::exists(currentProjectFile_);
      if (pointInRect(x, y, startupNewBtn_)) {
        startNewShow(false);
        showStartupDialog_ = false;
      } else if (pointInRect(x, y, startupLoadBtn_) && hasSavedFile) {
        // Open previous show file already loaded from startup path.
        showStartupDialog_ = false;
      } else if (pointInRect(x, y, startupOpenSavedBtn_)) {
        openProjectFromPicker();
        showStartupDialog_ = false;
      }
      return;
    }
    if (confirmQuit_) {
      if (pointInRect(x, y, quitYesBtn_)) {
        gShouldQuit.store(true);
      } else {
        confirmQuit_ = false;
      }
      return;
    }
    if (depPrompt_.active) {
      // CTA opens the vendor's download page. Either way, dismiss the prompt
      // so the operator can return to whatever they were doing.
      if (pointInRect(x, y, depPrompt_.ctaRect)) {
        deckboy::platform::openExternalUrl(depPrompt_.url);
      }
      dismissDependencyPrompt();
      return;
    }

    if (handleInlineTextEditorMouseDown(x, y)) {
      return;
    }

    if (handleDropdownMouseDown(x, y)) {
      return;
    }
    if (handleKeyColorPickerMouseDown(x, y)) {
      return;
    }

    if (playlistSplitterRect_.w > 0 && pointInRect(x, y, playlistSplitterRect_)) {
      layoutDragMode_ = LayoutDragMode::Playlist;
      return;
    }
    if (inspectorSplitterRect_.w > 0 && pointInRect(x, y, inspectorSplitterRect_)) {
      layoutDragMode_ = LayoutDragMode::Inspector;
      return;
    }

    // Shuffle footer button
    if (shuffleBtnRect_.w > 0 && pointInRect(x, y, shuffleBtnRect_)) {
      Deck& d = focusedDeckMutable();
      d.shuffle = !d.shuffle;
      triggerToast(d.shuffle ? "shuffle on" : "shuffle off");
      playUiSound(UiSoundEffect::Toggle);
      markProjectDirty();
      return;
    }

    // Settings modal intercepts all clicks when open
    if (settingsOpen_) {
      handleSettingsClick(x, y);
      return;
    }

    if (pointInRect(x, y, cueSourceTypeDropdownRect_)) {
      const Cue* cue = selectedCuePtr();
      if (cue && isSourceCueKind(cue->kind)) {
        openDropdown(
          "cue.source_type",
          cueSourceTypeDropdownRect_,
          sourceCueTypeChoices(),
          sourceCueTokenForKind(cue->kind),
          [this](const std::string& nextType) {
            setSelectedSourceCueKind(sourceCueKindFromToken(nextType));
          });
        return;
      }
      if (cue && cue->kind == CueKind::Pip) {
        std::string currentType = pipSourceTypeTokenFromCue(*cue);
        if (currentType == "legacy") {
          currentType = "media";
        }
        openDropdown(
          "cue.pip_source_type",
          cueSourceTypeDropdownRect_,
          pipSourceTypeChoices(),
          currentType,
          [this](const std::string& nextType) {
            setSelectedPipSourceType(nextType);
          });
        return;
      }
    }
    if (pointInRect(x, y, cueWindowSourceDropdownRect_)) {
      const Cue* cue = selectedCuePtr();
      if (cue && cue->kind == CueKind::WindowSource) {
        // Enumerate available windows and build dropdown choices
        auto windows = deckboy::platform::listCaptureWindows();
        std::vector<std::pair<std::string, std::string>> choices;
        for (const auto& w : windows) {
          choices.push_back({w.id, w.displayName});
        }
        // Determine currently selected source ref for highlighting
        std::string currentRef = sourceCueRefFromCue(*cue);
        if (currentRef.empty()) currentRef = defaultSourceRefForKind(cue->kind);
        // Map active-window to desktop for dropdown matching (closest equivalent)
        if (currentRef == "active-window") currentRef = "desktop";
        openDropdown(
          "cue.window_source",
          cueWindowSourceDropdownRect_,
          choices,
          currentRef,
          [this](const std::string& selectedId) {
            setSelectedSourceCueRef(selectedId);
          });
        return;
      }
    }
    if (pointInRect(x, y, cuePatternTypeDropdownRect_)) {
      const Cue* cue = selectedCuePtr();
      if (cue && cue->kind == CueKind::Pattern) {
        openDropdown(
          "cue.pattern",
          cuePatternTypeDropdownRect_,
          patternTypes(),
          normalizePatternTypeId(cue->path),
          [this](const std::string& nextType) {
            applyPatternTypeToSelectedCue(nextType, true);
          });
        return;
      }
    }
    if (pointInRect(x, y, cueTransitionStyleDropdownRect_)) {
      const Cue* cue = selectedCuePtr();
      std::string currentStyle = cue
        ? (cue->cueTransitionStyle.empty() ? focusedDeck().transitionStyle : cue->cueTransitionStyle)
        : focusedDeck().transitionStyle;
      openDropdown(
        "cue.transition_style",
        cueTransitionStyleDropdownRect_,
        transitionStyleChoices(),
        toLower(trim(currentStyle)),
        [this](const std::string& nextStyle) {
          setSelectedCueTransitionStyle(nextStyle);
        });
      return;
    }
    for (const auto& outputBtn : outputMenuButtons_) {
      if (!pointInRect(x, y, outputBtn.rect)) {
        continue;
      }
      if (outputBtn.action == kOutputMenuActionAddOutput) {
        addOutput(project_.focusedDeckIndex);
        return;
      }
      if (outputBtn.action == kOutputMenuActionToggleFps) {
        outputFpsCounterEnabled_ = !outputFpsCounterEnabled_;
        triggerToast(std::string("output fps ") + (outputFpsCounterEnabled_ ? "on" : "off"));
        return;
      }
      if (outputBtn.action == kOutputMenuActionFocus) {
        if (outputBtn.outputIndex >= 0 && outputBtn.outputIndex < static_cast<int>(project_.outputs.size())) {
          setFocusedOutputIndex(outputBtn.outputIndex);
        }
        return;
      }
      if (outputBtn.action == kOutputMenuActionRecover) {
        if (outputBtn.outputIndex >= 0 && outputBtn.outputIndex < static_cast<int>(project_.outputs.size())) {
          setFocusedOutputIndex(outputBtn.outputIndex);
          setFocusedOutputEnabled(true);
        }
        return;
      }
      if (outputBtn.action == kOutputMenuActionDisarm) {
        if (outputBtn.outputIndex >= 0 && outputBtn.outputIndex < static_cast<int>(project_.outputs.size())) {
          setFocusedOutputIndex(outputBtn.outputIndex);
          setFocusedOutputEnabled(false, false);
        }
        return;
      }
      if (outputBtn.action == kOutputMenuActionSelectDisplay) {
        if (outputBtn.outputIndex >= 0 && outputBtn.outputIndex < static_cast<int>(project_.outputs.size())) {
          setFocusedOutputIndex(outputBtn.outputIndex);
          openDropdown(
            "output.display",
            outputBtn.rect,
            outputDisplayDropdownChoices(),
            std::to_string(outputDisplayIndex(outputBtn.outputIndex)),
            [this](const std::string& value) {
              try {
                int displayIndex = std::stoi(trim(value));
                if (displayIndex >= 0) setOutputDisplayIndex(displayIndex);
              } catch (...) {}
            });
        }
        return;
      }
      if (outputBtn.action == kOutputMenuActionRouteFocusDeck ||
          outputBtn.action == kOutputMenuActionRouteAssignToggle ||
          outputBtn.action == kOutputMenuActionRouteLayerDec ||
          outputBtn.action == kOutputMenuActionRouteLayerInc ||
          outputBtn.action == kOutputMenuActionRouteOutputPrev ||
          outputBtn.action == kOutputMenuActionRouteOutputNext) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = std::clamp(outputBtn.deckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
        setFocusedDeckIndex(deckIndex);

        int outputCount = static_cast<int>(project_.outputs.size());
        int routeOutput = outputBtn.outputIndex;
        if (routeOutput < 0 || routeOutput >= outputCount) {
          routeOutput = std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        }

        if (outputBtn.action == kOutputMenuActionRouteOutputPrev ||
            outputBtn.action == kOutputMenuActionRouteOutputNext) {
          int delta = outputBtn.action == kOutputMenuActionRouteOutputPrev ? -1 : 1;
          int nextOutput = (routeOutput + delta + outputCount) % outputCount;
          moveDeckToOutput(deckIndex, nextOutput);
          setFocusedOutputIndex(nextOutput);
          return;
        }

        setFocusedOutputIndex(routeOutput);
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (outputBtn.action == kOutputMenuActionRouteFocusDeck) {
          return;
        }
        if (outputBtn.action == kOutputMenuActionRouteAssignToggle) {
          if (assignmentIndex) {
            unassignDeckFromOutput(deckIndex, routeOutput);
          } else {
            assignDeckToOutput(deckIndex, routeOutput);
          }
          return;
        }
        if (!assignmentIndex) {
          assignDeckToOutput(deckIndex, routeOutput);
          assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
          if (!assignmentIndex) {
            return;
          }
        }
        int currentLayer = 0; // Single-deck: always layer 0
        int delta = outputBtn.action == kOutputMenuActionRouteLayerDec ? -1 : 1;
        setDeckOutputAssignmentLayer(deckIndex, routeOutput, currentLayer + delta);
        return;
      }
      if (outputBtn.action == kOutputMenuActionRouteLayerCycle) {
        // Crosspoint matrix cell: cycle OFF -> BG(0) -> L1(1) -> L2(2) -> L3(3) -> L4(4) -> OFF
        if (project_.decks.empty() || project_.outputs.empty()) return;
        int deckIdx  = std::clamp(outputBtn.deckIndex,  0, static_cast<int>(project_.decks.size())   - 1);
        int outIdx   = std::clamp(outputBtn.outputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
        // Single-deck: just toggle assignment.
        auto ai = assignmentIndexForDeckOutput(deckIdx, outIdx);
        if (!ai) {
          assignDeckToOutput(deckIdx, outIdx);
        } else {
          unassignDeckFromOutput(deckIdx, outIdx);
        }
        return;
      }
      return;
    }
    for (const auto& hit : cueRowActionHits_) {
      if (!hit.enabled || !pointInRect(x, y, hit.rect)) {
        continue;
      }
      selectCueInDeck(hit.deckIndex, hit.cueIndex, false, false);
      dispatchQuickAction(hit.action);
      return;
    }
    for (int deckIndex = 0; deckIndex < static_cast<int>(deckColumnRects_.size()); ++deckIndex) {
      if (!pointInRect(x, y, deckColumnRects_[deckIndex])) {
        continue;
      }
      setFocusedDeckIndex(deckIndex);
      if (deckIndex < static_cast<int>(deckOpacityFaderRects_.size()) &&
          pointInRect(x, y, deckOpacityFaderRects_[deckIndex]) &&
          deckOpacityFaderRects_[deckIndex].w > 0) {
        bool altHeld = (SDL_GetModState() & KMOD_ALT) != 0;
        const SDL_Rect& rail = deckOpacityFaderRects_[deckIndex];
        float value = static_cast<float>(std::clamp(
          static_cast<double>(x - rail.x) / static_cast<double>(rail.w),
          0.0,
          1.0));
        if (altHeld) {
          value = value >= 0.5f ? 1.0f : 0.0f;
        }
        setDeckPlaylistOpacity(deckIndex, value, true);
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const SDL_Rect& primaryFrame = deckListClipRects_[deckIndex];
      SDL_Rect primaryClip {primaryFrame.x + 8, primaryFrame.y + 30, primaryFrame.w - 16, primaryFrame.h - 38};
      const SDL_Rect& overlayFrame = deckOverlayClipRects_[deckIndex];
      SDL_Rect overlayClip {overlayFrame.x + 8, overlayFrame.y + 42, overlayFrame.w - 16, overlayFrame.h - 50};
      if (pointInRect(x, y, overlayClip)) {
        int overlayY = overlayClip.y - deckOverlayScrolls_[deckIndex];
        for (int cueIndex : cueIndicesForOverlayRole(deck, true)) {
          SDL_Rect row {overlayClip.x, overlayY, overlayClip.w, kRowHeight};
          if (pointInRect(x, y, row)) {
            selectCueInDeck(deckIndex, cueIndex, false, false);
            drag_.active = false;
            drag_.deckIndex = deckIndex;
            drag_.cueIndex = -1;
            return;
          }
          overlayY += kRowHeight + 8;
        }
        return;
      }
      if (!pointInRect(x, y, primaryClip)) {
        return;
      }
      int listY = primaryClip.y - deckScrolls_[deckIndex];
      for (int cueIndex : cueIndicesForOverlayRole(deck, false)) {
        SDL_Rect row {primaryClip.x, listY, primaryClip.w, kRowHeight};
        if (pointInRect(x, y, row)) {
          bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
          bool ctrlHeld = (SDL_GetModState() & KMOD_CTRL) != 0;
          selectCueInDeck(deckIndex, cueIndex, shiftHeld, ctrlHeld);
          drag_.active = true;
          drag_.deckIndex = deckIndex;
          drag_.cueIndex = cueIndex;
          return;
        }
        listY += kRowHeight + 8;
      }
      return;
    }
    if (pointInRect(x, y, fileNewBtnRect_)) {
      startNewShow(true);
      return;
    }
    if (pointInRect(x, y, fileOpenBtnRect_)) {
      openProjectFromPicker();
      return;
    }
    if (pointInRect(x, y, fileSaveBtnRect_)) {
      saveProjectNow(true);
      return;
    }
    if (pointInRect(x, y, fileBundleBtnRect_)) {
      exportProjectBundleFromPicker();
      return;
    }
    if (pointInRect(x, y, fileSaveAsBtnRect_)) {
      saveProjectAsFromPicker();
      return;
    }
    if (pointInRect(x, y, deckSidebarToggleRect_)) {
      // Sidebar is fixed-visible; keep click handler as a no-op safety.
      return;
    }
    // Master cue sidebar buttons / program hits / tracker cells / sidebar rows removed
    // Settings gear button
    if (pointInRect(x, y, settingsGearRect_)) {
      settingsOpen_ = !settingsOpen_;
      uiWatchdogPopupEvent("settings_modal", settingsOpen_);
      return;
    }
    // BLK (blackout) button
    if (pointInRect(x, y, blackoutBtnRect_)) {
      masterDimmerTarget_ = (masterDimmerTarget_ < 0.5) ? 1.0 : 0.0;
      triggerToast(masterDimmerTarget_ < 0.5 ? "blackout ON" : "blackout off");
      return;
    }
    if (pointInRect(x, y, deckLoopBtnRect_)) {
      togglePlaylistLoop();
      return;
    }
    if (pointInRect(x, y, deckShuffleBtnRect_)) {
      toggleShuffle();
      return;
    }
    if (pointInRect(x, y, fullscreenBtnRect_)) {
      toggleOutputFullscreen();
      return;
    }
    // Warp editor buttons
    if (warpEditBtnRect_.w > 0 && pointInRect(x, y, warpEditBtnRect_)) {
      Deck& wd = focusedDeckMutable();
      if (!wd.warpEnabled) {
        setFocusedDeckWarpEnabled(true);
        warpEditMode_ = true;
      } else {
        warpEditMode_ = !warpEditMode_;
      }
      playUiSound(UiSoundEffect::Toggle);
      return;
    }
    if (warpModeBtnRect_.w > 0 && pointInRect(x, y, warpModeBtnRect_)) {
      cycleFocusedDeckWarpMode(1);
      playUiSound(UiSoundEffect::Toggle);
      return;
    }
    if (warpResetBtnRect_.w > 0 && pointInRect(x, y, warpResetBtnRect_)) {
      pushUndoSnapshot();
      Deck& wd = focusedDeckMutable();
      wd.warpTopLeftX = wd.warpTopLeftY = 0.0f;
      wd.warpTopRightX = wd.warpTopRightY = 0.0f;
      wd.warpBottomRightX = wd.warpBottomRightY = 0.0f;
      wd.warpBottomLeftX = wd.warpBottomLeftY = 0.0f;
      markProjectDirty();
      triggerToast("warp reset");
      playUiSound(UiSoundEffect::Clear);
      return;
    }
    if (warpSaveBtnRect_.w > 0 && pointInRect(x, y, warpSaveBtnRect_)) {
      openInlineTextEditor("warp.preset", "Save Warp Preset",
                           "Preset name:", "Preset " + std::to_string(warpPresets_.size() + 1),
                           [this](const std::string& value) {
        std::string name = trim(value);
        if (name.empty()) {
          triggerToast("warp preset: name required");
          return;
        }
        const Deck& wd = focusedDeck();
        WarpPreset p;
        p.name = name;
        p.tlx = wd.warpTopLeftX; p.tly = wd.warpTopLeftY;
        p.trx = wd.warpTopRightX; p.try_ = wd.warpTopRightY;
        p.brx = wd.warpBottomRightX; p.bry = wd.warpBottomRightY;
        p.blx = wd.warpBottomLeftX; p.bly = wd.warpBottomLeftY;
        p.mode = wd.warpMode;
        warpPresets_.push_back(p);
        triggerToast("warp preset saved");
      });
      return;
    }
    if (warpCopyBtnRect_.w > 0 && pointInRect(x, y, warpCopyBtnRect_)) {
      copyFocusedWarpSettings();
      return;
    }
    if (warpPasteBtnRect_.w > 0 && pointInRect(x, y, warpPasteBtnRect_)) {
      pasteFocusedWarpSettings();
      return;
    }
    if (warpRecallBtnRect_.w > 0 && pointInRect(x, y, warpRecallBtnRect_)) {
      if (!warpPresets_.empty()) {
        static int lastRecalled = -1;
        lastRecalled = (lastRecalled + 1) % static_cast<int>(warpPresets_.size());
        const WarpPreset& p = warpPresets_[lastRecalled];
        Deck& wd = focusedDeckMutable();
        wd.warpTopLeftX = p.tlx; wd.warpTopLeftY = p.tly;
        wd.warpTopRightX = p.trx; wd.warpTopRightY = p.try_;
        wd.warpBottomRightX = p.brx; wd.warpBottomRightY = p.bry;
        wd.warpBottomLeftX = p.blx; wd.warpBottomLeftY = p.bly;
        wd.warpMode = p.mode;
        markProjectDirty();
        triggerToast("recalled: " + p.name);
        playUiSound(UiSoundEffect::Navigate);
      }
      return;
    }
    // Warp corner drag start
    if (warpEditMode_ && focusedDeck().warpEnabled && warpMonitorInner_.w > 0) {
      const Deck& wd = focusedDeck();
      SDL_Rect mi = warpMonitorInner_;
      int focOutIdx = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
      auto [outW, outH] = outputRenderSizeForOutput(focOutIdx);
      float sx = static_cast<float>(mi.w) / std::max(1.0f, static_cast<float>(outW));
      float sy = static_cast<float>(mi.h) / std::max(1.0f, static_cast<float>(outH));
      float cornersX[4] = {
        static_cast<float>(mi.x) + wd.warpTopLeftX * sx,
        static_cast<float>(mi.x + mi.w) + wd.warpTopRightX * sx,
        static_cast<float>(mi.x + mi.w) + wd.warpBottomRightX * sx,
        static_cast<float>(mi.x) + wd.warpBottomLeftX * sx,
      };
      float cornersY[4] = {
        static_cast<float>(mi.y) + wd.warpTopLeftY * sy,
        static_cast<float>(mi.y) + wd.warpTopRightY * sy,
        static_cast<float>(mi.y + mi.h) + wd.warpBottomRightY * sy,
        static_cast<float>(mi.y + mi.h) + wd.warpBottomLeftY * sy,
      };
      constexpr int kGrabR = 14;
      for (int i = 0; i < 4; ++i) {
        int dx = x - static_cast<int>(cornersX[i]);
        int dy = y - static_cast<int>(cornersY[i]);
        if (dx * dx + dy * dy <= kGrabR * kGrabR) {
          warpDragCorner_ = i;
          return;
        }
      }
    }

    for (size_t i = 0; i < quickButtons_.size(); ++i) {
      const auto& qb = quickButtons_[i];
      bool isCueSettingsButton = i >= cueSettingsQuickButtonStartIndex_;
      if (isCueSettingsButton && !pointInRect(x, y, cueSettingsViewportRect_)) {
        continue;
      }
      if (pointInRect(x, y, qb.rect)) {
        lastInlineEditorAnchorRect_ = qb.rect;
        dispatchQuickAction(qb.action);
        return;
      }
    }

    for (const auto& button : buttons_) {
      if (pointInRect(x, y, button.rect)) {
        triggerButton(button.label);
        return;
      }
    }

    if (pointInRect(x, y, masterFaderRect_) && masterFaderRect_.w > 0) {
      double frac = static_cast<double>(x - masterFaderRect_.x) / static_cast<double>(masterFaderRect_.w);
      project_.masterVolume = std::clamp(frac * 2.0, 0.0, 2.0);
      markProjectDirty();
      return;
    }
    // Check trim handles before progress bar seek
    if (trimInHandleRect_.w > 0 && pointInRect(x, y, trimInHandleRect_)) {
      trimDragMode_ = TrimDragMode::In;
      return;
    }
    if (trimOutHandleRect_.w > 0 && pointInRect(x, y, trimOutHandleRect_)) {
      trimDragMode_ = TrimDragMode::Out;
      return;
    }
    if (button == SDL_BUTTON_LEFT && pointInRect(x, y, progressBarRect_)) {
      if (MediaEngine* engine = focusedMediaEngine()) {
        scrubWasPlaying_ = (engine->state() == TransportState::Playing);
        if (scrubWasPlaying_) {
          engine->pause();
        }
      }
      timelineScrubActive_ = seekFocusedTimelineAtX(x, true);
      return;
    }
  }

  void handleMouseMotion(int x, int y) {
    if (layoutDragMode_ == LayoutDragMode::Playlist && contentAreaRect_.w > 0) {
      constexpr int kPlaylistMinW = 236;
      int maxW = std::max(kPlaylistMinW, contentAreaRect_.w - 520);
      playlistPaneWidth_ = std::clamp(x - contentAreaRect_.x, kPlaylistMinW, maxW);
      return;
    }
    if (layoutDragMode_ == LayoutDragMode::Inspector && mainPanelLayoutRect_.w > 0) {
      constexpr int kInspectorMinW = 360;
      constexpr int kProgramMinW = 420;
      int maxW = std::max(kInspectorMinW, mainPanelLayoutRect_.w - kProgramMinW);
      inspectorPaneWidth_ = std::clamp(mainPanelLayoutRect_.x + mainPanelLayoutRect_.w - x,
                                       kInspectorMinW, maxW);
      return;
    }
    // Warp corner dragging — convert mouse delta in monitor-space to output-pixel warp offsets
    if (warpDragCorner_ >= 0 && warpEditMode_ && warpMonitorInner_.w > 0) {
      Deck& wd = focusedDeckMutable();
      SDL_Rect mi = warpMonitorInner_;
      int focOutIdx = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
      auto [outW, outH] = outputRenderSizeForOutput(focOutIdx);
      float sx = static_cast<float>(mi.w) / std::max(1.0f, static_cast<float>(outW));
      float sy = static_cast<float>(mi.h) / std::max(1.0f, static_cast<float>(outH));
      // Compute where the corner *should* be (its base position in monitor-space)
      float baseX = 0.0f, baseY = 0.0f;
      float* warpX = nullptr;
      float* warpY = nullptr;
      switch (warpDragCorner_) {
        case 0: baseX = static_cast<float>(mi.x);        baseY = static_cast<float>(mi.y);        warpX = &wd.warpTopLeftX;     warpY = &wd.warpTopLeftY;     break;
        case 1: baseX = static_cast<float>(mi.x + mi.w); baseY = static_cast<float>(mi.y);        warpX = &wd.warpTopRightX;    warpY = &wd.warpTopRightY;    break;
        case 2: baseX = static_cast<float>(mi.x + mi.w); baseY = static_cast<float>(mi.y + mi.h); warpX = &wd.warpBottomRightX; warpY = &wd.warpBottomRightY; break;
        case 3: baseX = static_cast<float>(mi.x);        baseY = static_cast<float>(mi.y + mi.h); warpX = &wd.warpBottomLeftX;  warpY = &wd.warpBottomLeftY;  break;
        default: break;
      }
      if (warpX && warpY) {
        float newX = (static_cast<float>(x) - baseX) / sx;
        float newY = (static_cast<float>(y) - baseY) / sy;
        // Snap to grid when Shift is held (10px grid in output space)
        SDL_Keymod mod = SDL_GetModState();
        if (mod & KMOD_SHIFT) {
          constexpr float kSnapGrid = 10.0f;
          newX = std::round(newX / kSnapGrid) * kSnapGrid;
          newY = std::round(newY / kSnapGrid) * kSnapGrid;
        }
        *warpX = newX;
        *warpY = newY;
        markProjectDirty();
      }
      return;
    }
    // Trim handle dragging
    if (trimDragMode_ != TrimDragMode::None && progressBarRect_.w > 0) {
      Cue* cue = activeCueMutable();
      MediaEngine* engine = focusedMediaEngine();
      if (cue && engine && cue->duration > 0.0) {
        double fraction = static_cast<double>(x - progressBarRect_.x) / static_cast<double>(progressBarRect_.w);
        double cueIn = std::clamp(cue->inPointSeconds, 0.0, cue->duration);
        double cueOut = cue->outPointSeconds > 0.0
          ? std::clamp(cue->outPointSeconds, cueIn, cue->duration)
          : cue->duration;
        bool zoomedToTrim = cueIn > 0.001 || cueOut < cue->duration - 0.001;
        double visibleDuration = zoomedToTrim ? std::max(0.01, cueOut - cueIn) : cue->duration;
        double seconds = zoomedToTrim
          ? cueIn + (visibleDuration * std::clamp(fraction, 0.0, 1.0))
          : cue->duration * std::clamp(fraction, 0.0, 1.0);
        if (trimDragMode_ == TrimDragMode::In) {
          cue->inPointSeconds = std::clamp(snapToCueFrame(*cue, seconds), 0.0, cue->duration);
          double out = cue->outPointSeconds > 0.0 ? cue->outPointSeconds : cue->duration;
          cue->outPointSeconds = std::clamp(snapToCueFrame(*cue, out), cue->inPointSeconds, cue->duration);
          triggerToast("in: " + formatSeconds(cue->inPointSeconds));
        } else {
          cue->outPointSeconds = std::clamp(snapToCueFrame(*cue, seconds), cue->inPointSeconds, cue->duration);
          triggerToast("out: " + formatSeconds(cue->outPointSeconds));
        }
        markProjectDirty();
      }
      return;
    }
    if (timelineScrubActive_) {
      seekFocusedTimelineAtX(x, true);
      return;
    }
    if (!drag_.active || drag_.cueIndex < 0) {
      return;
    }
    int di = drag_.deckIndex;
    if (di < 0 || di >= static_cast<int>(deckListClipRects_.size())) {
      return;
    }
    const SDL_Rect& clipFrame = deckListClipRects_[di];
    SDL_Rect clipRect {clipFrame.x + 8, clipFrame.y + 30, clipFrame.w - 16, clipFrame.h - 38};
    Deck& deck = project_.decks[di];
    int listY = clipRect.y - deckScrolls_[di];
    for (int index : cueIndicesForOverlayRole(deck, false)) {
      SDL_Rect row {clipRect.x, listY, clipRect.w, kRowHeight};
      if (pointInRect(x, y, row) && index != drag_.cueIndex) {
        pushUndoSnapshot();
        auto cue = deck.cues[drag_.cueIndex];
        deck.cues.erase(deck.cues.begin() + drag_.cueIndex);
        deck.cues.insert(deck.cues.begin() + index, cue);
        deck.selectedIndex = index;
        deck.selectedIndices.clear();
        deck.selectedIndices.push_back(index);
        if (deck.activeIndex == drag_.cueIndex) {
          deck.activeIndex = index;
        } else if (deck.activeIndex >= 0) {
          if (drag_.cueIndex < deck.activeIndex && index >= deck.activeIndex) {
            deck.activeIndex -= 1;
          } else if (drag_.cueIndex > deck.activeIndex && index <= deck.activeIndex) {
            deck.activeIndex += 1;
          }
        }
        drag_.cueIndex = index;
        triggerToast("cue reordered");
        markProjectDirty();
        return;
      }
      listY += kRowHeight + 8;
    }
  }

  void handleKeyDown(SDL_Keycode key, Uint16 mod, Uint32 sourceWindowId = 0, bool keyRepeat = false) {
    bool ctrl = (mod & KMOD_CTRL) != 0;
    bool shift = (mod & KMOD_SHIFT) != 0;

    if (showSplashOverlay_) {
      if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
        showSplashOverlay_ = false;
      }
      return;
    }

    // F11 — toggle borderless fullscreen on the control window. The renderer's
    // logical size scales the fixed-grid UI to fill the screen automatically.
    if (key == SDLK_F11 && !keyRepeat && controlWindow_) {
      Uint32 winFlags = SDL_GetWindowFlags(controlWindow_);
      bool isFullscreen =
        (winFlags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
      SDL_SetWindowFullscreen(controlWindow_,
                              isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
      triggerToast(isFullscreen ? "windowed" : "fullscreen");
      return;
    }

    if (showStartupDialog_) {
      bool hasSavedFile = !currentProjectFile_.empty() && fs::exists(currentProjectFile_);
      if (key == SDLK_n) {
        startNewShow(false);
        showStartupDialog_ = false;
      } else if (key == SDLK_o) {
        openProjectFromPicker();
        showStartupDialog_ = false;
      } else if (key == SDLK_p && hasSavedFile) {
        showStartupDialog_ = false;
      } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (hasSavedFile) {
          showStartupDialog_ = false;
        } else {
          openProjectFromPicker();
          showStartupDialog_ = false;
        }
      } else if (key == SDLK_ESCAPE) {
        showStartupDialog_ = false;
      }
      return;
    }

    if (handleInlineTextEditorKey(key, mod)) {
      return;
    }

    if (handleDropdownKey(key, mod)) {
      return;
    }
    if (keyColorPickerArmed_ && key == SDLK_ESCAPE) {
      keyColorPickerArmed_ = false;
      triggerToast("key color picker canceled");
      return;
    }

    if (shortcutsOverlayOpen_) {
      if (key == SDLK_ESCAPE || key == SDLK_SLASH) {
        shortcutsOverlayOpen_ = false;
      }
      return;
    }

    if (settingsOpen_) {
      if (key == SDLK_ESCAPE) {
        settingsOpen_ = false;
        uiWatchdogPopupEvent("settings_modal", false);
      }
      return;
    }

    if (confirmQuit_) {
      if (key == SDLK_y || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        gShouldQuit.store(true);
      } else {
        confirmQuit_ = false;
      }
      return;
    }

    if (ctrl && key == SDLK_q) {
      confirmQuit_ = true;
      return;
    }

    // Ctrl+Enter / Ctrl+Return — simultaneous all-deck take
    if (ctrl && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
      takeAllDecks(true);
      return;
    }
    // Ctrl+Z — undo, Ctrl+Shift+Z — redo
    if (ctrl && !shift && key == SDLK_z) {
      undo();
      return;
    }
    if (ctrl && shift && key == SDLK_z) {
      redo();
      return;
    }
    if (ctrl && !shift && key == SDLK_c) {
      copySelectedCueSettings();
      return;
    }
    if (ctrl && !shift && key == SDLK_v) {
      pasteSelectedCueSettings();
      return;
    }
    if (ctrl && shift && key == SDLK_c) {
      copyFocusedWarpSettings();
      return;
    }
    if (ctrl && shift && key == SDLK_v) {
      pasteFocusedWarpSettings();
      return;
    }
    // Ctrl+/ — keyboard shortcuts overlay
    if (ctrl && key == SDLK_SLASH) {
      shortcutsOverlayOpen_ = !shortcutsOverlayOpen_;
      return;
    }
    // Ctrl+Shift+Space — stop all decks (must be checked before Ctrl+Space)
    if (ctrl && shift && key == SDLK_SPACE) {
      allStop();
      return;
    }
    // Group preset keyboard shortcuts removed
    // Ctrl+Space — all-deck go (play/pause toggle)
    if (ctrl && key == SDLK_SPACE) {
      goAllDecks();
      return;
    }
    // Ctrl+G — GOTO cue
    if (ctrl && key == SDLK_g) {
      openInlineGotoCueEditor();
      return;
    }
    if (ctrl && !shift && key == SDLK_f) {
      if (settingsOpen_) {
        settingsOpen_ = false;
      }
      openInlineCueFindEditor(false);
      return;
    }
    if (ctrl && shift && key == SDLK_f) {
      if (!lastCueFindToken_.empty()) {
        findCueToken(lastCueFindToken_, 1, false);
      } else {
        triggerToast("find: press Ctrl+F first");
      }
      return;
    }
    if (ctrl && !shift && key == SDLK_r) {
      rerackTransport();
      return;
    }
    if (ctrl && shift && key == SDLK_r) {
      openInlineCueRenumberEditor(false);
      return;
    }

    if (ctrl && key == SDLK_o) {
      setActiveTrimFromPlayhead(false);
      return;
    }
    if (ctrl && key == SDLK_i) {
      setActiveTrimFromPlayhead(true);
      return;
    }
    if (ctrl && key == SDLK_n) {
      startNewShow(true);
      return;
    }
    if (ctrl && !shift && key == SDLK_s) {
      saveProjectNow(true);
      return;
    }
    if (ctrl && shift && key == SDLK_s) {
      saveProjectAsFromPicker();
      return;
    }
    if (ctrl && shift && key == SDLK_e) {
      exportProjectBundleFromPicker();
      return;
    }

    switch (key) {
      case SDLK_ESCAPE:
        if (keyRepeat) {
          break;
        }
        {
          constexpr Uint64 kEscRepeatMs = 900;
          Uint64 now = SDL_GetTicks64();
          bool quickEsc = lastEscapeKeyMs_ > 0 && (now - lastEscapeKeyMs_) <= kEscRepeatMs;
          escapePressStreak_ = quickEsc ? (escapePressStreak_ + 1) : 1;
          lastEscapeKeyMs_ = now;
          if (escapePressStreak_ >= 3) {
            if (emergencyDisarmOutputsFromEsc(sourceWindowId)) {
              escapePressStreak_ = 0;
              confirmQuit_ = false;
              break;
            }
            // No active output-safety context: treat this press as a new sequence start.
            escapePressStreak_ = 1;
          }
        }
        if (!escapeOutputFullscreen(sourceWindowId)) {
          confirmQuit_ = true;
        } else {
          confirmQuit_ = false;
        }
        break;
      case SDLK_TAB:
        cycleFocusedDeck(shift ? -1 : 1);
        break;
      case SDLK_UP:
        selectRelative(-1, shift);
        break;
      case SDLK_DOWN:
        selectRelative(1, shift);
        break;
      case SDLK_LEFT:
        nudgeFocusedPausedPlayback(-1, mod);
        break;
      case SDLK_RIGHT:
        nudgeFocusedPausedPlayback(1, mod);
        break;
      case SDLK_1:
        toggleUiSounds();
        break;
      case SDLK_2:
        toggleUiTransitions();
        break;
      case SDLK_4:
        togglePlaylistLoop();
        break;
      case SDLK_RETURN:
      case SDLK_KP_ENTER:
        jumpSelectedCue();
        break;
      case SDLK_SPACE:
        toggleTransport();
        break;
      case SDLK_s:
        stopTransport();
        break;
      case SDLK_c:
        clearOutput();
        break;
      case SDLK_f:
        toggleOutputFullscreen();
        break;
      case SDLK_i:
        importWithPicker();
        break;
      case SDLK_b:
        addBrowserCueFromPrompt();
        break;
      case SDLK_g:
        triggerParkedCueCreationToast("lower third");
        break;
      case SDLK_m:
        triggerParkedCueCreationToast("scene");
        break;
      case SDLK_p:
        if (shift) {
          triggerParkedCueCreationToast("pip");
        } else {
          addKawaiiPatternCue();
        }
        break;
      case SDLK_l:
        toggleSelectedLoop();
        break;
      case SDLK_e:
        toggleSelectedPauseOnLastFrame();
        break;
      case SDLK_x:
        cycleSelectedEndAction();
        break;
      case SDLK_6:
        toggleShuffle();
        break;
      case SDLK_k:
        cycleSelectedColorTag();
        break;
      case SDLK_LEFTBRACKET:
        adjustSelectedFade(!shift, -0.25);
        break;
      case SDLK_RIGHTBRACKET:
        adjustSelectedFade(!shift, 0.25);
        break;
      case SDLK_a:
        cycleAudioOutputDevice(1);
        break;
      case SDLK_d:
        cycleOutputDisplay(1);
        break;
      case SDLK_n:
        // toggleFocusedOutputNdi → setFocusedOutputNdiEnabled, which gates on
        // ndiRuntimeAvailable() and shows the prompt internally.
        toggleFocusedOutputNdi();
        break;
      case SDLK_o:
        if (shift) {
          toggleTimeOverlayEnabled();
        } else {
          openProjectFromPicker();
        }
        break;
      case SDLK_t:
        setTimecodeRunEnabled(!focusedDeck().timecodeRunEnabled);
        break;
      case SDLK_5:
        setTimecodeChaseEnabled(!focusedDeck().timecodeChaseEnabled);
        break;
      case SDLK_DELETE:
        deleteSelected();
        break;
      case SDLK_BACKSPACE:
        if (!focusedDeck().overlayActiveIndices.empty()) {
          clearOverlay();
        } else {
          deleteSelected();
        }
        break;
      case SDLK_EQUALS:
      case SDLK_PLUS:
        if (MediaEngine* engine = focusedMediaEngine()) {
          engine->setVolume(engine->volume() + 0.05f);
          triggerToast("speaker up");
        }
        break;
      case SDLK_MINUS:
        if (MediaEngine* engine = focusedMediaEngine()) {
          engine->setVolume(engine->volume() - 0.05f);
          triggerToast("speaker down");
        }
        break;
      default:
        if (handleCueTypeAheadKey(key, mod)) {
          return;
        }
        break;
    }
  }
