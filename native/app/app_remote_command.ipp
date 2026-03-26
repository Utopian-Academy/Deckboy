// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
  void handleRemoteCommand(const std::string& rawCommand) {
    auto parts = splitWhitespace(rawCommand);
    if (parts.empty()) {
      return;
    }

    std::string command = toUpper(parts[0]);
    auto parseCueIndex = [&](size_t tokenIndex) -> std::optional<int> {
      if (tokenIndex >= parts.size()) {
        return std::nullopt;
      }
      try {
        int index = std::stoi(parts[tokenIndex]);
        if (index < 1 || index > static_cast<int>(focusedDeck().cues.size())) {
          return std::nullopt;
        }
        return index - 1;
      } catch (...) {
        return std::nullopt;
      }
    };
    auto parseNumber = [&](size_t tokenIndex) -> std::optional<double> {
      if (tokenIndex >= parts.size()) {
        return std::nullopt;
      }
      try {
        return std::stod(parts[tokenIndex]);
      } catch (...) {
        return std::nullopt;
      }
    };
    auto parseToggleWord = [&](size_t tokenIndex) -> std::optional<bool> {
      if (tokenIndex >= parts.size()) {
        return std::nullopt;
      }
      std::string value = toUpper(parts[tokenIndex]);
      if (value == "ON" || value == "1" || value == "TRUE") {
        return true;
      }
      if (value == "OFF" || value == "0" || value == "FALSE") {
        return false;
      }
      return std::nullopt;
    };

    if (command == "ATEMEVENT") {
      if (parts.size() > 1) {
        handleAtemEventPayload(joinParts(parts, 1));
      }
      return;
    }
    if (command == "NDIEVENT") {
      if (parts.size() > 1) {
        handleNdiTriggerPayload(joinParts(parts, 1));
      }
      return;
    }
    if (command == "NMCEVENT") {
      if (parts.size() > 1) {
        handleNmcSyncPayload(joinParts(parts, 1));
      }
      return;
    }
    if (command == "ARTNETEVENT") {
      if (parts.size() > 2) {
        try {
          int channel = std::stoi(parts[1]);
          int value = std::stoi(parts[2]);
          handleArtNetEvent(channel, value);
        } catch (...) {
        }
      }
      return;
    }
    if (command == "MTCEXT" || command == "TIMECODEEXT") {
      if (!project_.mtcIngestEnabled) {
        return;
      }
      if (auto seconds = parseNumber(1); seconds) {
        double fpsHint = focusedDeck().timecodeFps;
        if (auto fps = parseNumber(2); fps) {
          fpsHint = *fps;
        }
        ingestIntegrationTimecode(*seconds, fpsHint);
      }
      return;
    }
    if (command == "LTCEXT" || command == "TIMECODELTC") {
      if (!project_.ltcIngestEnabled) {
        return;
      }
      if (auto seconds = parseNumber(1); seconds) {
        double fpsHint = focusedDeck().timecodeFps;
        if (auto fps = parseNumber(2); fps) {
          fpsHint = *fps;
        }
        ingestIntegrationTimecode(*seconds, fpsHint);
      }
      return;
    }

    if (command == "PING") {
      triggerToast("companion ping");
      return;
    }
    if (command == "DECK") {
      if (parts.size() < 2) {
        return;
      }
      try {
        int deckIndex = std::stoi(parts[1]) - 1;
        if (!setFocusedDeckIndex(deckIndex)) {
          return;
        }
        if (parts.size() > 2) {
          handleRemoteCommand(joinParts(parts, 2));
        }
      } catch (...) {
      }
      return;
    }
    if (command == "DECKNEXT") {
      cycleFocusedDeck(1);
      return;
    }
    if (command == "DECKPREV" || command == "DECKPREVIOUS") {
      cycleFocusedDeck(-1);
      return;
    }
    if (command == "DECKADD" || command == "NEWDECK") {
      triggerToast("single deck only");
      return;
    }
    if (command == "GO" || command == "TOGGLE") {
      toggleTransport();
      return;
    }
    if (command == "PLAY") {
      playTransport();
      return;
    }
    if (command == "PAUSE") {
      pauseTransport();
      return;
    }
    if (command == "STOP") {
      stopTransport();
      return;
    }
    if (command == "JUMPMODE" || command == "JUMP_MODE") {
      if (parts.size() < 2) {
        triggerToast("jump mode: " + jumpModeLabelFromToken(project_.jumpMode));
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "TOGGLE") {
        toggleJumpMode();
      } else {
        setJumpModeToken(value);
      }
      return;
    }
    if (command == "JUMPTRANS" || command == "JUMPTRANSITION" || command == "JUMP_XFADE") {
      auto state = parseToggleWord(1);
      if (!state) {
        setJumpTransitionEnabled(!project_.jumpTransitionEnabled);
      } else {
        setJumpTransitionEnabled(*state);
      }
      return;
    }
    if (command == "PANICPROFILE" || command == "PANIC_PROFILE") {
      if (parts.size() < 2) {
        triggerToast("panic profile: " + panicProfileLabelFromToken(project_.panicProfile));
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "NEXT") {
        cyclePanicProfile(1);
      } else if (value == "PREV" || value == "PREVIOUS") {
        cyclePanicProfile(-1);
      } else {
        project_.panicProfile = normalizePanicProfileToken(value);
        triggerToast("panic profile: " + panicProfileLabelFromToken(project_.panicProfile));
        playUiSound(UiSoundEffect::Toggle);
        markProjectDirty();
      }
      return;
    }
    if (command == "PANICFADE" || command == "PANIC_FADE") {
      if (parts.size() < 2) {
        std::ostringstream label;
        label << std::fixed << std::setprecision(1) << project_.panicFadeSeconds;
        triggerToast("panic fade " + label.str() + "s");
      } else if (auto value = parseNumber(1); value) {
        setPanicFadeSeconds(*value);
      }
      return;
    }
    if (command == "PANICAUTORESTORE" || command == "PANIC_RESTORE") {
      auto state = parseToggleWord(1);
      if (!state) {
        setPanicAutoRestoreEnabled(!project_.panicAutoRestore);
      } else {
        setPanicAutoRestoreEnabled(*state);
      }
      return;
    }
    if (command == "PANIC") {
      if (parts.size() > 1) {
        triggerPanicProfile(parts[1]);
      } else {
        triggerPanicProfile();
      }
      return;
    }
    if (command == "OSCQUERY" || command == "OSC_QUERY") {
      auto state = parseToggleWord(1);
      if (!state) {
        setOscQueryEnabled(!project_.oscQueryEnabled);
      } else {
        setOscQueryEnabled(*state);
      }
      return;
    }
    if (command == "OSCQUERYPORT" || command == "OSC_QUERY_PORT") {
      if (parts.size() < 2) {
        triggerToast("osc query port: " + std::to_string(project_.oscQueryPort));
      } else if (auto value = parseNumber(1); value) {
        setOscQueryPort(static_cast<int>(std::lround(*value)));
      }
      return;
    }
    if (command == "OSCFEEDBACK" || command == "OSC_FEEDBACK") {
      auto state = parseToggleWord(1);
      if (!state) {
        setOscFeedbackMirrorEnabled(!project_.oscFeedbackMirrorEnabled);
      } else {
        setOscFeedbackMirrorEnabled(*state);
      }
      return;
    }
    if (command == "OSCFEEDBACKRATE" || command == "OSC_FEEDBACK_RATE") {
      if (parts.size() < 2) {
        triggerToast("osc feedback rate: " + std::to_string(project_.oscFeedbackRateMs) + " ms");
      } else if (auto value = parseNumber(1); value) {
        setOscFeedbackRateMs(static_cast<int>(std::lround(*value)));
      }
      return;
    }
    if (command == "ATEM" || command == "ATEMTRIGGER") {
      auto state = parseToggleWord(1);
      if (!state) {
        setIntegrationAdapterEnabled("ATEM", !project_.atemTriggerEnabled);
      } else {
        setIntegrationAdapterEnabled("ATEM", *state);
      }
      return;
    }
    if (command == "NDITRIGGER" || command == "NDI_TRIGGER") {
      auto state = parseToggleWord(1);
      if (!state) {
        setIntegrationAdapterEnabled("NDI", !project_.ndiTriggerEnabled);
      } else {
        setIntegrationAdapterEnabled("NDI", *state);
      }
      return;
    }
    if (command == "NMC" || command == "NMCSYNC" || command == "NMC_SYNC") {
      auto state = parseToggleWord(1);
      if (!state) {
        setIntegrationAdapterEnabled("NMC", !project_.nmcSyncEnabled);
      } else {
        setIntegrationAdapterEnabled("NMC", *state);
      }
      return;
    }
    if (command == "MTC" || command == "MTCINGEST" || command == "MTC_INGEST") {
      auto state = parseToggleWord(1);
      if (!state) {
        setIntegrationAdapterEnabled("MTC", !project_.mtcIngestEnabled);
      } else {
        setIntegrationAdapterEnabled("MTC", *state);
      }
      return;
    }
    if (command == "LTC" || command == "LTCINGEST" || command == "LTC_INGEST") {
      auto state = parseToggleWord(1);
      if (!state) {
        setIntegrationAdapterEnabled("LTC", !project_.ltcIngestEnabled);
      } else {
        setIntegrationAdapterEnabled("LTC", *state);
      }
      return;
    }
    if (command == "ARTNET" || command == "DMXARTNET" || command == "DMX_ARTNET" || command == "DMX") {
      auto state = parseToggleWord(1);
      if (!state) {
        setIntegrationAdapterEnabled("ARTNET", !project_.dmxArtNetEnabled);
      } else {
        setIntegrationAdapterEnabled("ARTNET", *state);
      }
      return;
    }
    if (command == "ARTNETPORT" || command == "DMXPORT" || command == "ART_NET_PORT") {
      if (parts.size() < 2) {
        triggerToast("artnet port: " + std::to_string(project_.artNetPort));
      } else if (auto value = parseNumber(1); value) {
        setArtNetPort(static_cast<int>(std::lround(*value)));
      }
      return;
    }
    if (command == "INTEGRATION" || command == "INTEGRATIONS") {
      if (parts.size() < 2 || toUpper(parts[1]) == "STATUS") {
        triggerToast("integrations: " + integrationBackendRouteSummary());
      } else {
        std::string mode = toUpper(parts[1]);
        if (mode == "ON") {
          setAllIntegrationAdaptersEnabled(true);
        } else if (mode == "OFF") {
          setAllIntegrationAdaptersEnabled(false);
        } else {
          triggerToast("integrations: " + integrationBackendRouteSummary());
        }
      }
      return;
    }
    // ── All-deck simultaneous commands ─────────────────────────────
    if (command == "ALLTAKE" || command == "SYNCTAKE") {
      takeAllDecks(true);
      return;
    }
    if (command == "ALLGO" || command == "SYNCGO") {
      goAllDecks();
      return;
    }
    if (command == "ALLPLAY") {
      for (int di = 0; di < static_cast<int>(project_.decks.size()); ++di) {
        if (auto* e = mediaEngineForDeck(di)) e->play();
      }
      triggerToast("all decks play");
      return;
    }
    if (command == "ALLPAUSE") {
      for (int di = 0; di < static_cast<int>(project_.decks.size()); ++di) {
        if (auto* e = mediaEngineForDeck(di)) e->pause();
      }
      triggerToast("all decks paused");
      return;
    }
    if (command == "ALLSTOP") {
      for (int di = 0; di < static_cast<int>(project_.decks.size()); ++di) {
        if (auto* e = mediaEngineForDeck(di)) e->stop();
      }
      triggerToast("all decks stopped");
      return;
    }
    if (command == "GROUP" || command == "MASTER" || command == "MASTERCUE" ||
        command == "PRESET" || command == "GROUPPRESET") {
      triggerToast("master scene commands: removed");
      return;
    }
    if (command == "CLEAR") {
      clearOutput();
      return;
    }
    if (command == "FULLSCREEN") {
      toggleOutputFullscreen();
      return;
    }
    if (command == "NEXT") {
      selectRelative(1, false);
      return;
    }
    if (command == "PREV" || command == "PREVIOUS") {
      selectRelative(-1, false);
      return;
    }
    if (command == "SELECT") {
      auto index = parseCueIndex(1);
      if (!index && parts.size() > 1) {
        index = cueIndexByToken(focusedDeck(), joinParts(parts, 1));
      }
      if (index) {
        Deck& deck = focusedDeckMutable();
        if (deck.selectedIndex != *index || !cueIndexSelected(deck, *index)) {
          selectCueInDeck(project_.focusedDeckIndex, *index, false, false);
          triggerToast("cue " + std::to_string(*index + 1) + " armed");
        }
      }
      return;
    }
    if (command == "FIND" || command == "CUEFIND") {
      if (parts.size() < 2) {
        if (!lastCueFindToken_.empty()) {
          findCueToken(lastCueFindToken_, 1, false);
        } else {
          triggerToast("find: token required");
        }
      } else {
        findCueToken(joinParts(parts, 1), 1, false);
      }
      return;
    }
    if (command == "FINDNEXT" || command == "CUEFINDNEXT") {
      if (lastCueFindToken_.empty()) {
        triggerToast("find: run FIND first");
      } else {
        findCueToken(lastCueFindToken_, 1, false);
      }
      return;
    }
    if (command == "FINDPREV" || command == "FINDPREVIOUS" || command == "CUEFINDPREV") {
      if (lastCueFindToken_.empty()) {
        triggerToast("find: run FIND first");
      } else {
        findCueToken(lastCueFindToken_, -1, false);
      }
      return;
    }
    if (command == "FINDTAKE" || command == "CUEFINDTAKE") {
      if (parts.size() < 2) {
        if (!lastCueFindToken_.empty()) {
          findCueToken(lastCueFindToken_, 1, true);
        } else {
          triggerToast("find: token required");
        }
      } else {
        findCueToken(joinParts(parts, 1), 1, true);
      }
      return;
    }
    if (command == "FINDCLEAR" || command == "FINDRESET" || command == "CUEFINDCLEAR") {
      clearCueFindState();
      triggerToast("find cleared");
      return;
    }
    if (command == "FINDSTATUS" || command == "CUEFINDSTATUS") {
      if (lastCueFindToken_.empty() || lastCueFindMatches_.empty()) {
        triggerToast("find: none");
      } else {
        int cursor = std::clamp(lastCueFindCursor_, 0, static_cast<int>(lastCueFindMatches_.size()) - 1);
        triggerToast("find \"" + lastCueFindToken_ + "\" "
          + std::to_string(cursor + 1) + "/" + std::to_string(lastCueFindMatches_.size()));
      }
      return;
    }
    if (command == "RENUMBER" || command == "CUEAUTOID" || command == "AUTOID") {
      if (parts.size() > 1 && toUpper(parts[1]) == "CLEAR") {
        clearFocusedDeckCueNumbers();
        return;
      }
      std::string prefix;
      int startAt = 1;
      if (parts.size() > 1) {
        prefix = parts[1];
      }
      if (parts.size() > 2) {
        try {
          startAt = std::stoi(parts[2]);
        } catch (...) {
        }
      }
      renumberFocusedDeckCueNumbers(prefix, startAt);
      return;
    }
    if (command == "SELECTID" || command == "CUEID") {
      selectCueById(joinParts(parts, 1));
      return;
    }
    if (command == "TAKE") {
      auto index = parseCueIndex(1);
      if (!index && parts.size() > 1) {
        index = cueIndexByToken(focusedDeck(), joinParts(parts, 1));
      }
      if (index) {
        selectCueInDeck(project_.focusedDeckIndex, *index, false, false);
      }
      if (parts.size() > 2 && toUpper(parts[2]) == "AUTO") {
        takeSelected(true);
      } else {
        jumpSelectedCue();
      }
      return;
    }
    if (command == "TAKEID") {
      if (selectCueById(joinParts(parts, 1))) {
        jumpSelectedCue();
      }
      return;
    }
    if (command == "GOTO") {
      std::string token = joinParts(parts, 1);
      if (token.empty()) {
        return;
      }
      Deck& deck = focusedDeckMutable();
      auto index = cueIndexByToken(deck, token);
      if (!index) {
        return;
      }
      selectCueInDeck(project_.focusedDeckIndex, *index, false, false);
      jumpSelectedCue();
      return;
    }
    if (command == "VOLUME") {
      auto value = parseNumber(1);
      if (value) {
        MediaEngine* engine = focusedMediaEngine();
        if (!engine) {
          return;
        }
        double normalized = *value > 1.0 ? *value / 100.0 : *value;
        engine->setVolume(static_cast<float>(std::clamp(normalized, 0.0, 1.0)));
        triggerToast("speaker " + std::to_string(static_cast<int>(std::round(engine->volume() * 100.0f))) + "%");
      }
      return;
    }
    if (command == "SEEK" || command == "SEEKPOS") {
      auto value = parseNumber(1);
      if (value) {
        MediaEngine* engine = focusedMediaEngine();
        if (!engine) {
          return;
        }
        engine->seek(*value);
        triggerToast("jump to " + formatSeconds(*value));
      }
      return;
    }
    if (command == "IN" || command == "TRIMIN") {
      auto value = parseNumber(1);
      if (value) {
        setSelectedTrimIn(*value);
      }
      return;
    }
    if (command == "OUT" || command == "TRIMOUT") {
      auto value = parseNumber(1);
      if (value) {
        setSelectedTrimOut(*value);
      }
      return;
    }
    if (command == "TRIM") {
      if (parts.size() < 2) {
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "CLEAR" || value == "RESET") {
        clearSelectedTrim();
      } else if (value == "IN") {
        auto number = parseNumber(2);
        if (number) {
          setSelectedTrimIn(*number);
        }
      } else if (value == "OUT") {
        auto number = parseNumber(2);
        if (number) {
          setSelectedTrimOut(*number);
        }
      }
      return;
    }
    if (command == "OVERLAY" || command == "TIMEOVERLAY") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleTimeOverlayEnabled();
      } else {
        setTimeOverlayEnabled(*state);
      }
      return;
    }
    if (command == "SFX") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleUiSounds();
      } else if (*state != project_.uiSoundsEnabled) {
        project_.uiSoundsEnabled = *state && uiAudioDevice_ != 0;
        if (project_.uiSoundsEnabled) {
          playUiSound(UiSoundEffect::Toggle);
        }
        triggerToast(project_.uiSoundsEnabled ? "little bloops on" : "little bloops off");
        markProjectDirty();
      }
      return;
    }
    if (command == "ANIM" || command == "ANIMATION") {
      toggleUiTransitions();
      return;
    }
    if (command == "DELETE") {
      deleteSelected();
      return;
    }
    if (command == "LOOP") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleSelectedLoop();
      } else {
        setSelectedLoop(*state);
      }
      return;
    }
    if (command == "HOLD" || command == "HOLDLAST" || command == "PAUSEEND" || command == "PAUSEATEND") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleSelectedPauseOnLastFrame();
      } else {
        setSelectedPauseOnLastFrame(*state);
      }
      return;
    }
    if (command == "PAUSEBEGIN" || command == "PAUSEATBEGIN" || command == "PAUSESTART") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleSelectedPauseAtBeginning();
      } else {
        setSelectedPauseAtBeginning(*state);
      }
      return;
    }
    if (command == "CUEAUDIO" || command == "AUDIOCUE" || command == "AUDIOENABLED") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleSelectedAudioEnabled();
      } else {
        setSelectedAudioEnabled(*state);
      }
      return;
    }
    if (command == "NEXTTRANS" || command == "TRANSITIONTONEXT" || command == "CUEXNEXT") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleSelectedTransitionToNext();
      } else {
        setSelectedTransitionToNext(*state);
      }
      return;
    }
    if (command == "CUEGOTO" || command == "GOTOTARGET") {
      if (parts.size() <= 1) {
        Cue* cue = selectedCueMutable();
        if (!cue) {
          return;
        }
        triggerToast(cue->gotoTarget.empty() ? "goto target: none" : ("goto target: " + cue->gotoTarget));
      } else {
        setSelectedGotoTarget(joinParts(parts, 1));
      }
      return;
    }
    if (command == "CUEIDSHORT" || command == "SHORTID" || command == "CUESHORTID") {
      Cue* cue = selectedCueMutable();
      if (!cue) {
        return;
      }
      if (parts.size() <= 1) {
        triggerToast("cue id: " + (cue->cueId.empty() ? std::string("(none)") : cue->cueId));
      } else {
        std::string cueIdShort = normalizeCueIdShort(parts[1]);
        forEachFocusedSelectedCueMutable([&](Cue& each, int) {
          each.cueId = cueIdShort;
        });
        triggerToast("cue id: " + (cueIdShort.empty() ? std::string("(none)") : cueIdShort));
        markProjectDirty();
      }
      return;
    }
    if (command == "FADEIN") {
      auto value = parseNumber(1);
      if (value) {
        setSelectedFade(true, *value);
      }
      return;
    }
    if (command == "FADEOUT") {
      auto value = parseNumber(1);
      if (value) {
        setSelectedFade(false, *value);
      }
      return;
    }
    if (command == "AUTONEXT" || command == "AUTOADVANCE") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleAutoAdvance();
      } else {
        setAutoAdvance(*state);
      }
      return;
    }
    if (command == "PLAYLISTLOOP") {
      auto state = parseToggleWord(1);
      if (!state) {
        togglePlaylistLoop();
      } else {
        setPlaylistLoop(*state);
      }
      return;
    }
    if (command == "PLAYLISTOPACITY" || command == "DECKOPACITY" || command == "DECKDIM") {
      if (parts.size() <= 1) {
        int pct = static_cast<int>(std::lround(std::clamp(focusedDeck().playlistOpacity, 0.0f, 1.0f) * 100.0f));
        triggerToast("deck opacity: " + std::to_string(pct) + "%");
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "UP" || value == "+" || value == "INC") {
        setFocusedDeckPlaylistOpacity(focusedDeck().playlistOpacity + 0.05f, true);
        return;
      }
      if (value == "DOWN" || value == "-" || value == "DEC") {
        setFocusedDeckPlaylistOpacity(focusedDeck().playlistOpacity - 0.05f, true);
        return;
      }
      if (value == "ON" || value == "100") {
        setFocusedDeckPlaylistOpacity(1.0f, true);
        return;
      }
      if (value == "OFF" || value == "0") {
        setFocusedDeckPlaylistOpacity(0.0f, true);
        return;
      }
      if (auto parsed = parseNumber(1); parsed) {
        double normalized = *parsed > 1.0 ? *parsed / 100.0 : *parsed;
        setFocusedDeckPlaylistOpacity(static_cast<float>(normalized), true);
      }
      return;
    }
    if (command == "PLAYLISTAUTOFADE" || command == "DECKAUTOFADE") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleFocusedDeckPlaylistAutoFade();
      } else {
        setFocusedDeckPlaylistAutoFade(*state);
      }
      return;
    }
    if (command == "PLAYLISTFADE" || command == "DECKFADE") {
      if (parts.size() <= 1) {
        setFocusedDeckPlaylistFadeSeconds(focusedDeck().playlistFadeSeconds);
        return;
      }
      if (auto parsed = parseNumber(1); parsed) {
        setFocusedDeckPlaylistFadeSeconds(*parsed);
      }
      return;
    }
    if (command == "SHUFFLE") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleShuffle();
      } else {
        Deck& deck = focusedDeckMutable();
        if (deck.shuffle != *state) {
          deck.shuffle = *state;
          triggerToast(deck.shuffle ? "shuffle on" : "shuffle off");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "ENDACTION") {
      if (parts.size() >= 2) {
        setSelectedEndAction(parseCueEndAction(toUpper(parts[1]) == "INHERIT" ? "inherit" :
                                               toUpper(parts[1]) == "STOP"    ? "stop"    :
                                               toUpper(parts[1]) == "LOOP"    ? "loop"    :
                                               toUpper(parts[1]) == "HOLD"    ? "hold"    :
                                               toUpper(parts[1]) == "NEXT"    ? "next"    : "inherit"));
      } else {
        cycleSelectedEndAction();
      }
      return;
    }
    if (command == "TRANSITION" || command == "XFADE") {
      if (parts.size() < 2) {
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "OFF" || value == "0" || value == "CUT") {
        setTransitionSeconds(0.0);
        if (value == "CUT") {
          setTransitionStyle(TransitionStyle::Cut);
        }
      } else if (value == "STYLE") {
        if (parts.size() > 2) {
          setTransitionStyle(parseTransitionStyleToken(parts[2]));
        }
      } else {
        auto seconds = parseNumber(1);
        if (seconds) {
          setTransitionSeconds(*seconds);
          if (focusedDeck().transitionStyle == "cut" && *seconds > 0.0) {
            setTransitionStyle(TransitionStyle::Crossfade);
          }
        } else {
          setTransitionStyle(parseTransitionStyleToken(parts[1]));
        }
      }
      return;
    }
    if (command == "TRANSITIONSTYLE") {
      if (parts.size() > 1) {
        setTransitionStyle(parseTransitionStyleToken(parts[1]));
      }
      return;
    }
    if (command == "TCMARK" || command == "TIMECODEMARK") {
      if (parts.size() < 2) {
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "NOW" || value == "HERE") {
        setSelectedCueTimecodeTrigger(focusedDeck().timecodeCurrentSeconds);
        return;
      }
      if (value == "CLEAR" || value == "NONE" || value == "OFF") {
        clearSelectedCueTimecodeTrigger();
        return;
      }
      auto parsed = parseTimecodeSeconds(joinParts(parts, 1), focusedDeck().timecodeFps);
      if (parsed) {
        setSelectedCueTimecodeTrigger(*parsed);
      }
      return;
    }
    if (command == "TIMECODE" || command == "TC") {
      if (parts.size() < 2) {
        triggerToast("tc " + formatTimecode(focusedDeck().timecodeCurrentSeconds, focusedDeck().timecodeFps));
        return;
      }
      std::string sub = toUpper(parts[1]);
      if (sub == "CHASE") {
        auto state = parseToggleWord(2);
        if (state) {
          setTimecodeChaseEnabled(*state);
        }
        return;
      }
      if (sub == "RUN") {
        auto state = parseToggleWord(2);
        if (state) {
          setTimecodeRunEnabled(*state);
        }
        return;
      }
      if (sub == "TRIGGER") {
        auto state = parseToggleWord(2);
        if (state) {
          focusedDeckMutable().timecodeTriggerEnabled = *state;
          triggerToast(*state ? "tc trigger on" : "tc trigger off");
          markProjectDirty();
        }
        return;
      }
      if (sub == "JAM") {
        auto state = parseToggleWord(2);
        if (state) {
          setTimecodeJamSyncEnabled(*state);
        } else {
          triggerToast(focusedDeck().timecodeJamSyncEnabled ? "tc jam on" : "tc jam off");
        }
        return;
      }
      if (sub == "FREEWHEEL" || sub == "FREE") {
        if (parts.size() < 3) {
          std::ostringstream label;
          label << std::fixed << std::setprecision(1) << focusedDeck().timecodeFreewheelSeconds;
          triggerToast("tc freewheel " + label.str() + "s");
        } else if (auto value = parseNumber(2); value) {
          setTimecodeFreewheelSeconds(*value);
        }
        return;
      }
      if (sub == "FPS") {
        auto value = parseNumber(2);
        if (value) {
          setTimecodeFps(*value);
        }
        return;
      }
      if (sub == "SET") {
        auto parsed = parseTimecodeSeconds(joinParts(parts, 2), focusedDeck().timecodeFps);
        if (parsed) {
          setFocusedDeckTimecode(*parsed, true);
        }
        return;
      }
      auto parsed = parseTimecodeSeconds(joinParts(parts, 1), focusedDeck().timecodeFps);
      if (parsed) {
        setFocusedDeckTimecode(*parsed, false);
      }
      return;
    }
    if (command == "PATTERN") {
      if (parts.size() > 1) {
        std::string sub = toUpper(parts[1]);
        if (sub == "LIST") {
          triggerToast("patterns: " + std::to_string(patternTypes().size()) + " types");
          return;
        }
        if (sub == "SET") {
          std::string typeId = parts.size() > 2 ? normalizePatternTypeId(joinParts(parts, 2)) : "";
          if (typeId == "checker") {
            typeId = "checkerboard";
          }
          if (typeId.empty() || !isKnownPatternType(typeId)) {
            triggerToast("pattern default: invalid");
            return;
          }
          patternDefaultTypeId_ = typeId;
          triggerToast("pattern default: " + patternLabelForType(typeId));
          return;
        }
      }

      std::string typeId;
      if (parts.size() > 2) {
        std::string tail = toUpper(parts.back());
        if (tail == "MOTION" || tail == "ANIM" || tail == "ANIMATED") {
          typeId = normalizePatternTypeId(parts[1] + "-motion");
        }
      }
      if (typeId.empty()) {
        typeId = parts.size() > 1 ? normalizePatternTypeId(joinParts(parts, 1)) : patternDefaultTypeId_;
      }
      addPatternCue(typeId);
      return;
    }
    if (command == "SOURCE" || command == "SRC" || command == "WINDOWSOURCE" ||
        command == "CAMERACUE" || command == "SYPHONCUE" || command == "SPOUTCUE") {
      CueKind kind = CueKind::WindowSource;
      size_t refStartIndex = 1;
      if (command == "CAMERACUE") {
        kind = CueKind::Camera;
      } else if (command == "SYPHONCUE" || command == "SPOUTCUE") {
        kind = CueKind::Syphon;
      } else if (parts.size() > 1) {
        std::string typeArg = toUpper(parts[1]);
        if (typeArg == "WINDOW" || typeArg == "WINDOWSOURCE" || typeArg == "WINDOWS") {
          kind = CueKind::WindowSource;
          refStartIndex = 2;
        } else if (typeArg == "CAMERA" || typeArg == "CAM" || typeArg == "WEBCAM") {
          kind = CueKind::Camera;
          refStartIndex = 2;
        } else if (typeArg == "SYPHON" || typeArg == "SIPHON" || typeArg == "SPOUT") {
          kind = CueKind::Syphon;
          refStartIndex = 2;
        }
      }
      std::string sourceRef = parts.size() > refStartIndex ? joinParts(parts, refStartIndex) : "";
      addSourceCue(kind, sourceRef);
      return;
    }
    if (command == "STILLDUR" || command == "DURATION") {
      if (parts.size() > 1) {
        try {
          double dur = std::stod(parts[1]);
          if (Cue* cue = selectedCueMutable()) {
            if (cue->kind != CueKind::Video) {
              cue->stillDurationSeconds = std::max(0.0, dur);
              triggerToast(cue->stillDurationSeconds > 0.0
                ? "still dur " + formatSeconds(cue->stillDurationSeconds)
                : "still dur: hold");
              markProjectDirty();
            }
          }
        } catch (...) {}
      }
      return;
    }
    if (command == "GRAPHIC" || command == "LOWERTHIRD") {
      triggerParkedCueCreationToast("lower third");
      return;
    }
    if (command == "PIP") {
      triggerParkedCueCreationToast("pip");
      return;
    }
    if (command == "COMPOSITE" || command == "SCENE" || command == "MULTIVIEW") {
      triggerParkedCueCreationToast("scene");
      return;
    }
    if (command == "LOWERTEXT") {
      std::string txt = joinParts(parts, 1);
      if (Cue* cue = selectedCueMutable()) {
        if (cue->kind == CueKind::LowerThird) {
          cue->lowerThirdText = txt;
          triggerToast("lower text set");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "LOWERSUB") {
      std::string txt = joinParts(parts, 1);
      if (Cue* cue = selectedCueMutable()) {
        if (cue->kind == CueKind::LowerThird) {
          cue->lowerThirdSubtext = txt;
          triggerToast("lower sub set");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "LOWERALPHA") {
      if (parts.size() > 1) {
        try {
          int alpha = std::stoi(parts[1]);
          if (Cue* cue = selectedCueMutable()) {
            if (cue->kind == CueKind::LowerThird) {
              cue->lowerThirdBgAlpha = std::clamp(alpha, 0, 255);
              triggerToast("overlay alpha " + std::to_string(cue->lowerThirdBgAlpha));
              markProjectDirty();
            }
          }
        } catch (...) {}
      }
      return;
    }
    if (command == "CLEAROVERLAY") {
      clearOverlay();
      return;
    }
    if (command == "OVERLAY" && parts.size() > 1) {
      std::string sub = toUpper(parts[1]);
      if (sub == "CLEAR") { clearOverlay(); return; }
      if (sub == "POP")   { popOverlay();   return; }
      if (sub == "PUSH" && parts.size() > 2) {
        try {
          int idx = std::stoi(parts[2]) - 1;  // 1-based
          Deck& deck = focusedDeckMutable();
          if (idx >= 0 && idx < static_cast<int>(deck.cues.size())) {
            auto& ov = deck.overlayActiveIndices;
            if (std::find(ov.begin(), ov.end(), idx) == ov.end()) {
              if (ov.size() >= 4) ov.erase(ov.begin());
              ov.push_back(idx);
              triggerToast("overlay pushed: " + deck.cues[idx].name);
              markProjectDirty();
            }
          }
        } catch (...) {}
        return;
      }
      return;
    }
    if (command == "BROWSER") {
      std::string url = joinParts(parts, 1);
      if (!url.empty()) {
        addBrowserCue(url);
      }
      return;
    }
    if (command == "AUDIO") {
      if (parts.size() > 1) {
        std::string value = toUpper(parts[1]);
        if (value == "NEXT") {
          cycleAudioOutputDevice(1);
        } else if (value == "PREV" || value == "PREVIOUS") {
          cycleAudioOutputDevice(-1);
        } else if (value == "DEFAULT") {
          setAudioOutputDevice("");
        } else {
          setAudioOutputDevice(joinParts(parts, 1));
        }
      }
      return;
    }
    if (command == "DISPLAY") {
      if (parts.size() > 1) {
        std::string value = toUpper(parts[1]);
        if (value == "NEXT") {
          cycleOutputDisplay(1);
        } else if (value == "PREV" || value == "PREVIOUS") {
          cycleOutputDisplay(-1);
        } else {
          try {
            int displayIndex = std::stoi(parts[1]);
            setOutputDisplayIndex(std::max(0, displayIndex - 1));
          } catch (...) {
          }
        }
      }
      return;
    }
    if (command == "ROUTE") {
      triggerToast("route command: removed");
      return;
    }
    if (command == "LAYER") {
      triggerToast("layer command: removed");
      return;
    }
    if (command == "LAYERNAME") {
      // Layer names removed (single-deck).
      return;
    }
    if (command == "CANVAS" || command == "VIEW" || command == "WARP" || command == "BLEND") {
      std::string forwarded = "VIDEO " + command;
      if (parts.size() > 1) {
        forwarded += " " + joinParts(parts, 1);
      }
      handleRemoteCommand(forwarded);
      return;
    }
    if (command == "VIDEO" || command == "OUTPUTMODE") {
      if (parts.size() <= 1) {
        std::string canvasLabel = project_.outputCanvasEnabled
          ? (" canvas " + std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
          : " canvas off";
        triggerToast("video: " + outputSizingModeLabel() + " " + outputResolutionLabelForOutput(project_.focusedOutputIndex)
          + " @" + outputRefreshRateLabel() + " " + outputBitDepthModeLabel() + canvasLabel);
        return;
      }

      auto applyRasterToken = [&](std::string token) -> bool {
        token = toUpper(trim(token));
        double hzOverride = -1.0;
        auto atPos = token.find('@');
        if (atPos != std::string::npos && atPos + 1 < token.size()) {
          try {
            hzOverride = std::stod(token.substr(atPos + 1));
          } catch (...) {
            hzOverride = -1.0;
          }
          token = token.substr(0, atPos);
        }
        auto xPos = token.find('X');
        if (xPos == std::string::npos || xPos == 0 || xPos + 1 >= token.size()) {
          return false;
        }
        try {
          int w = std::stoi(token.substr(0, xPos));
          int h = std::stoi(token.substr(xPos + 1));
          if (w > 0 && h > 0) {
            setOutputSizingModeFixed(w, h);
            if (hzOverride >= 0.0) {
              setOutputRefreshRate(hzOverride);
            }
            return true;
          }
        } catch (...) {
        }
        return false;
      };

      auto parsePointToken = [&](std::string token) -> std::optional<std::pair<int, int>> {
        token = trim(token);
        if (token.empty()) {
          return std::nullopt;
        }
        size_t split = token.find(',');
        if (split == std::string::npos) {
          split = token.find('x');
        }
        if (split == std::string::npos) {
          split = token.find('X');
        }
        if (split == std::string::npos || split == 0 || split + 1 >= token.size()) {
          return std::nullopt;
        }
        try {
          int x = std::stoi(token.substr(0, split));
          int y = std::stoi(token.substr(split + 1));
          return std::make_pair(x, y);
        } catch (...) {
          return std::nullopt;
        }
      };

      auto parseBlendValue = [&](size_t tokenIndex) -> std::optional<float> {
        auto value = parseNumber(tokenIndex);
        if (!value) {
          return std::nullopt;
        }
        float normalized = static_cast<float>(*value);
        if (std::abs(normalized) > 1.0f) {
          normalized /= 100.0f;
        }
        return normalized;
      };

      std::string value = toUpper(parts[1]);
      if (value == "OUTPUT" || value == "OUT") {
        if (parts.size() <= 2) {
          triggerToast("output: " + outputLabel(project_.focusedOutputIndex)
            + " host:" + deckLabel(focusedOutput().hostDeckIndex));
          return;
        }
        std::string outputArg = toUpper(parts[2]);
        if (outputArg == "NEXT") {
          cycleFocusedOutput(1);
          return;
        }
        if (outputArg == "PREV" || outputArg == "PREVIOUS") {
          cycleFocusedOutput(-1);
          return;
        }
        if (outputArg == "ADD" || outputArg == "NEW" || outputArg == "CREATE") {
          std::string newType = "window";
          if (parts.size() > 3) {
            std::string typeArg = toUpper(parts[3]);
            if (typeArg == "STREAM") {
              newType = "stream";
            }
          }
          addOutput(project_.focusedDeckIndex, newType);
          return;
        }
        if (outputArg == "ON" || outputArg == "ENABLE") {
          setFocusedOutputEnabled(true);
          return;
        }
        if (outputArg == "OFF" || outputArg == "DISABLE") {
          setFocusedOutputEnabled(false);
          return;
        }
        if (outputArg == "TOGGLE") {
          toggleFocusedOutputEnabled();
          return;
        }
        if (outputArg == "ASSIGN") {
          std::optional<int> layer;
          if (parts.size() > 3) {
            try {
              layer = std::stoi(parts[3]);
            } catch (...) {
              layer = std::nullopt;
            }
          }
          assignFocusedDeckToFocusedOutput(layer);
          return;
        }
        if (outputArg == "HOST") {
          if (parts.size() <= 3) {
            setFocusedOutputHostDeck(project_.focusedDeckIndex);
            return;
          }
          std::optional<int> targetDeck = parseDeckReferenceToken(parts[3]);
          if (!targetDeck && parts.size() > 4) {
            targetDeck = parseDeckReferenceToken(joinParts(parts, 3));
          }
          if (targetDeck) {
            setFocusedOutputHostDeck(*targetDeck);
          }
          return;
        }
        if (outputArg == "TYPE") {
          if (parts.size() <= 3) {
            const OutputTarget& focused = focusedOutput();
            triggerToast("type: " + normalizeOutputType(focused.outputType));
            return;
          }
          std::string typeArg = toUpper(parts[3]);
          if (typeArg == "WINDOW" || typeArg == "DISPLAY") {
            setFocusedOutputType("window");
            return;
          }
          if (typeArg == "STREAM") {
            setFocusedOutputType("stream");
            return;
          }
          return;
        }
        if (outputArg == "MIRROR") {
          if (parts.size() <= 3) {
            cycleFocusedOutputMirrorSource(1);
            return;
          }
          std::string mirrorArg = toUpper(parts[3]);
          if (mirrorArg == "OFF" || mirrorArg == "NONE") {
            setFocusedOutputMirrorSource(-1);
            return;
          }
          if (mirrorArg == "NEXT") {
            cycleFocusedOutputMirrorSource(1);
            return;
          }
          if (mirrorArg == "PREV" || mirrorArg == "PREVIOUS") {
            cycleFocusedOutputMirrorSource(-1);
            return;
          }
          try {
            int sourceOutput = std::stoi(parts[3]);
            setFocusedOutputMirrorSource(std::max(0, sourceOutput - 1));
          } catch (...) {
          }
          return;
        }
        if (outputArg == "ALPHA" || outputArg == "DIM" || outputArg == "OPACITY") {
          if (parts.size() <= 3) {
            int pct = static_cast<int>(std::lround(std::clamp(focusedOutput().outputAlpha, 0.0f, 1.0f) * 100.0f));
            triggerToast("output alpha: " + std::to_string(pct) + "%");
            return;
          }
          std::string alphaArg = toUpper(parts[3]);
          if (alphaArg == "UP" || alphaArg == "+" || alphaArg == "INC") {
            setFocusedOutputAlpha(focusedOutput().outputAlpha + 0.05f);
            return;
          }
          if (alphaArg == "DOWN" || alphaArg == "-" || alphaArg == "DEC") {
            setFocusedOutputAlpha(focusedOutput().outputAlpha - 0.05f);
            return;
          }
          if (alphaArg == "ON") {
            setFocusedOutputAlpha(1.0f);
            return;
          }
          if (alphaArg == "OFF") {
            setFocusedOutputAlpha(0.0f);
            return;
          }
          try {
            double value = std::stod(parts[3]);
            if (value > 1.0) {
              value /= 100.0;
            }
            setFocusedOutputAlpha(static_cast<float>(value));
          } catch (...) {
          }
          return;
        }
        if (outputArg == "DELAY" || outputArg == "LATENCY") {
          if (parts.size() <= 3) {
            triggerToast("output delay: " + std::to_string(focusedOutput().outputDelayMs) + " ms");
            return;
          }
          std::string delayArg = toUpper(parts[3]);
          if (delayArg == "OFF" || delayArg == "NONE") {
            setFocusedOutputDelayMs(0);
            return;
          }
          if (delayArg == "UP" || delayArg == "+" || delayArg == "INC") {
            setFocusedOutputDelayMs(focusedOutput().outputDelayMs + 100);
            return;
          }
          if (delayArg == "DOWN" || delayArg == "-" || delayArg == "DEC") {
            setFocusedOutputDelayMs(focusedOutput().outputDelayMs - 100);
            return;
          }
          try {
            setFocusedOutputDelayMs(std::stoi(parts[3]));
          } catch (...) {
          }
          return;
        }
        if (outputArg == "OVERLAY" || outputArg == "TIMEOVERLAY") {
          if (parts.size() <= 3) {
            toggleFocusedOutputTimeOverlayEnabled();
            return;
          }
          if (auto state = parseToggleWord(3); state) {
            setFocusedOutputTimeOverlayEnabled(*state);
          }
          return;
        }
        if (outputArg == "COLORSPACE" || outputArg == "COLOR" || outputArg == "SPACE") {
          size_t colorTokenIndex = 3;
          if (outputArg == "COLOR" && parts.size() > 4 && toUpper(parts[3]) == "SPACE") {
            colorTokenIndex = 4;
          }
          if (parts.size() <= colorTokenIndex) {
            triggerToast("color space: " + toUpper(normalizeOutputColorSpace(focusedOutput().outputColorSpace)));
            return;
          }
          std::string colorArg = toUpper(parts[colorTokenIndex]);
          if (colorArg == "NEXT") {
            cycleFocusedOutputColorSpace(1);
            return;
          }
          if (colorArg == "PREV" || colorArg == "PREVIOUS") {
            cycleFocusedOutputColorSpace(-1);
            return;
          }
          setFocusedOutputColorSpace(parts[colorTokenIndex]);
          return;
        }
        if (outputArg == "LAYOUT" || outputArg == "MODE") {
          if (parts.size() <= 3) {
            triggerToast("layout: " + normalizeOutputLayoutMode(focusedOutput().outputLayoutMode));
            return;
          }
          std::string modeArg = toUpper(parts[3]);
          if (modeArg == "NEXT") {
            cycleFocusedOutputLayoutMode(1);
            return;
          }
          if (modeArg == "PREV" || modeArg == "PREVIOUS") {
            cycleFocusedOutputLayoutMode(-1);
            return;
          }
          if (modeArg == "SPAN") {
            setFocusedOutputLayoutMode("span");
            return;
          }
          if (modeArg == "DUP" || modeArg == "DUPLICATE" || modeArg == "CLONE") {
            setFocusedOutputLayoutMode("duplicate");
            return;
          }
          return;
        }
        if (outputArg == "ORIENTATION" || outputArg == "ORIENT" || outputArg == "ROTATE" || outputArg == "ROT") {
          if (parts.size() <= 3) {
            triggerToast("orientation: " + outputOrientationLabel(focusedOutput().outputOrientationDegrees));
            return;
          }
          std::string rotateArg = toUpper(parts[3]);
          if (rotateArg == "NEXT" || rotateArg == "CW" || rotateArg == "RIGHT") {
            cycleFocusedOutputOrientation(1);
            return;
          }
          if (rotateArg == "PREV" || rotateArg == "PREVIOUS" || rotateArg == "CCW" || rotateArg == "LEFT") {
            cycleFocusedOutputOrientation(-1);
            return;
          }
          if (rotateArg == "RESET" || rotateArg == "NORMAL") {
            setFocusedOutputOrientationDegrees(0);
            return;
          }
          try {
            setFocusedOutputOrientationDegrees(std::stoi(parts[3]));
          } catch (...) {
          }
          return;
        }
        if (outputArg == "TESTCARD" || outputArg == "TEST") {
          if (parts.size() <= 3) {
            toggleFocusedOutputTestCardEnabled();
            return;
          }
          std::string testArg = toUpper(parts[3]);
          if (testArg == "ALL") {
            if (parts.size() > 4) {
              if (auto state = parseToggleWord(4); state) {
                setAllOutputsTestCardEnabled(*state);
              }
            } else {
              bool anyOff = false;
              for (const auto& out : project_.outputs) {
                if (!out.outputTestCardEnabled) {
                  anyOff = true;
                  break;
                }
              }
              setAllOutputsTestCardEnabled(anyOff);
            }
            return;
          }
          if (testArg == "TOGGLE") {
            toggleFocusedOutputTestCardEnabled();
            return;
          }
          if (auto state = parseToggleWord(3); state) {
            setFocusedOutputTestCardEnabled(*state);
          }
          return;
        }
        try {
          int outputIndex = std::stoi(parts[2]);
          setFocusedOutputIndex(std::max(0, outputIndex - 1));
        } catch (...) {
        }
        return;
      }
      if (value == "STREAM") {
        if (parts.size() <= 2) {
          const OutputTarget& output = focusedOutput();
          std::string protocol = normalizeOutputStreamProtocol(output.streamProtocol);
          triggerToast("stream: "
            + std::string(output.streamEnabled ? "on " : "off ")
            + toUpper(protocol)
            + " " + std::to_string(output.streamBitrateKbps) + "k");
          return;
        }
        std::string streamArg = toUpper(parts[2]);
        if (streamArg == "ON") {
          setFocusedOutputStreamEnabled(true);
          return;
        }
        if (streamArg == "OFF") {
          setFocusedOutputStreamEnabled(false);
          return;
        }
        if (streamArg == "TOGGLE") {
          toggleFocusedOutputStreamEnabled();
          return;
        }
        if (streamArg == "SRT" || streamArg == "RTMP") {
          setFocusedOutputStreamProtocol(toLower(streamArg));
          return;
        }
        if ((streamArg == "PROTO" || streamArg == "PROTOCOL") && parts.size() > 3) {
          setFocusedOutputStreamProtocol(toLower(parts[3]));
          return;
        }
        if ((streamArg == "URL" || streamArg == "TARGET") && parts.size() > 3) {
          setFocusedOutputStreamUrl(joinParts(parts, 3));
          return;
        }
        if ((streamArg == "BITRATE" || streamArg == "RATE") && parts.size() > 3) {
          try {
            setFocusedOutputStreamBitrateKbps(std::stoi(parts[3]));
          } catch (...) {
          }
          return;
        }
        return;
      }
      if (value == "CANVAS") {
        if (parts.size() <= 2) {
          std::string label = project_.outputCanvasEnabled
            ? (std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
            : "off";
          triggerToast("canvas: " + label);
          return;
        }
        std::string canvasArg = toUpper(parts[2]);
        if (canvasArg == "OFF" || canvasArg == "0") {
          setOutputCanvasMode(false);
          return;
        }
        if (canvasArg == "ON" || canvasArg == "1") {
          if (parts.size() > 3) {
            if (auto size = parsePointToken(parts[3]); size) {
              setOutputCanvasMode(true, size->first, size->second);
              return;
            }
          }
          setOutputCanvasMode(true, project_.outputCanvasWidth, project_.outputCanvasHeight);
          return;
        }
        if (canvasArg == "DISPLAY" || canvasArg == "NATIVE") {
          auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
          setOutputCanvasMode(true, rasterW, rasterH);
          return;
        }
        if (canvasArg == "DOUBLE" || canvasArg == "2X") {
          auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
          setOutputCanvasMode(true, std::max(320, rasterW * 2), std::max(180, rasterH));
          return;
        }
        if ((canvasArg == "SIZE" || canvasArg == "SET") && parts.size() > 3) {
          if (auto size = parsePointToken(parts[3]); size) {
            setOutputCanvasMode(true, size->first, size->second);
          }
          return;
        }
        if (auto size = parsePointToken(parts[2]); size) {
          setOutputCanvasMode(true, size->first, size->second);
        }
        return;
      }
      if (value == "VIEW" || value == "PAN") {
        if (parts.size() <= 2) {
          const Deck& focused = focusedDeck();
          triggerToast("view: " + std::to_string(focused.canvasViewX) + "," + std::to_string(focused.canvasViewY));
          return;
        }
        std::string viewArg = toUpper(parts[2]);
        if (viewArg == "LEFT" || viewArg == "RIGHT" || viewArg == "UP" || viewArg == "DOWN") {
          int amount = 100;
          if (parts.size() > 3) {
            try {
              amount = std::stoi(parts[3]);
            } catch (...) {
              amount = 100;
            }
          }
          amount = std::clamp(std::abs(amount), 1, 8192);
          if (viewArg == "LEFT") nudgeFocusedDeckCanvasView(-amount, 0);
          if (viewArg == "RIGHT") nudgeFocusedDeckCanvasView(amount, 0);
          if (viewArg == "UP") nudgeFocusedDeckCanvasView(0, -amount);
          if (viewArg == "DOWN") nudgeFocusedDeckCanvasView(0, amount);
          return;
        }
        if (viewArg == "NUDGE" && parts.size() > 4) {
          try {
            int dx = std::stoi(parts[3]);
            int dy = std::stoi(parts[4]);
            nudgeFocusedDeckCanvasView(dx, dy);
          } catch (...) {
          }
          return;
        }
        if (auto point = parsePointToken(parts[2]); point) {
          setFocusedDeckCanvasView(point->first, point->second);
          return;
        }
        if (parts.size() > 3) {
          try {
            int x = std::stoi(parts[2]);
            int y = std::stoi(parts[3]);
            setFocusedDeckCanvasView(x, y);
          } catch (...) {
          }
        }
        return;
      }
      if (value == "WARP") {
        if (parts.size() <= 2) {
          const Deck& deck = focusedDeck();
          triggerToast(std::string("warp: ") + (deck.warpEnabled ? "on" : "off")
                       + " (" + toLower(warpModeLabel(deck.warpMode)) + ")");
          return;
        }
        std::string warpArg = toUpper(parts[2]);
        if (warpArg == "ON") {
          setFocusedDeckWarpEnabled(true);
          return;
        }
        if (warpArg == "OFF") {
          setFocusedDeckWarpEnabled(false);
          return;
        }
        if (warpArg == "TOGGLE") {
          toggleFocusedDeckWarpEnabled();
          return;
        }
        if (warpArg == "RESET") {
          resetFocusedDeckWarp();
          return;
        }
        if (warpArg == "MODE") {
          if (parts.size() <= 3) {
            triggerToast("warp mode: " + toLower(warpModeLabel(focusedDeck().warpMode)));
            return;
          }
          std::string modeArg = toUpper(parts[3]);
          if (modeArg == "NEXT") {
            cycleFocusedDeckWarpMode(1);
            return;
          }
          if (modeArg == "PREV" || modeArg == "PREVIOUS") {
            cycleFocusedDeckWarpMode(-1);
            return;
          }
          setFocusedDeckWarpMode(parts[3]);
          return;
        }
        if (warpArg == "LINEAR" || warpArg == "PERSPECTIVE" || warpArg == "PERSP" || warpArg == "PROJECTIVE") {
          setFocusedDeckWarpMode(warpArg);
          return;
        }
        std::string corner = warpArg;
        size_t deltaIndex = 3;
        if ((warpArg == "MOVE" || warpArg == "ADJUST" || warpArg == "SET") && parts.size() > 3) {
          corner = parts[3];
          deltaIndex = 4;
        }
        if (parts.size() <= deltaIndex + 1) {
          return;
        }
        try {
          float dx = static_cast<float>(std::stod(parts[deltaIndex]));
          float dy = static_cast<float>(std::stod(parts[deltaIndex + 1]));
          adjustFocusedDeckWarpCorner(corner, dx, dy);
        } catch (...) {
        }
        return;
      }
      if (value == "BLEND") {
        if (parts.size() <= 2) {
          const Deck& focused = focusedDeck();
          triggerToast(
            "blend: L" + std::to_string(static_cast<int>(std::lround(focused.edgeBlendLeft * 100.0f))) +
            " R" + std::to_string(static_cast<int>(std::lround(focused.edgeBlendRight * 100.0f))) +
            " T" + std::to_string(static_cast<int>(std::lround(focused.edgeBlendTop * 100.0f))) +
            " B" + std::to_string(static_cast<int>(std::lround(focused.edgeBlendBottom * 100.0f)))
          );
          return;
        }
        std::string edge = toUpper(parts[2]);
        if (edge == "RESET") {
          setFocusedDeckEdgeBlend("L", 0.0f);
          setFocusedDeckEdgeBlend("R", 0.0f);
          setFocusedDeckEdgeBlend("T", 0.0f);
          setFocusedDeckEdgeBlend("B", 0.0f);
          triggerToast("blend reset");
          return;
        }
        if (edge == "ALL" && parts.size() > 3) {
          if (auto amount = parseBlendValue(3); amount) {
            setFocusedDeckEdgeBlend("L", *amount);
            setFocusedDeckEdgeBlend("R", *amount);
            setFocusedDeckEdgeBlend("T", *amount);
            setFocusedDeckEdgeBlend("B", *amount);
          }
          return;
        }
        if (parts.size() > 3) {
          if (auto amount = parseBlendValue(3); amount) {
            setFocusedDeckEdgeBlend(edge, *amount);
          }
        }
        return;
      }
      if (value == "REFRESH" || value == "RATE" || value == "HZ") {
        if (parts.size() <= 2) {
          triggerToast("video refresh: " + outputRefreshRateLabel());
          return;
        }
        std::string rateArg = toUpper(parts[2]);
        if (rateArg == "AUTO") {
          setOutputRefreshRate(0.0);
          return;
        }
        if (rateArg == "NEXT") {
          cycleOutputRefreshRate(1);
          return;
        }
        if (rateArg == "PREV" || rateArg == "PREVIOUS") {
          cycleOutputRefreshRate(-1);
          return;
        }
        try {
          setOutputRefreshRate(std::stod(parts[2]));
        } catch (...) {
        }
        return;
      }
      if (value == "BITDEPTH" || value == "DEPTH" || value == "FORMAT") {
        if (parts.size() <= 2) {
          triggerToast("video depth: " + outputBitDepthModeLabel() + " (" + outputBitDepthActiveLabelForOutput(project_.focusedOutputIndex) + ")");
          return;
        }
        std::string depthArg = toUpper(parts[2]);
        if (depthArg == "AUTO") {
          setOutputBitDepthMode(0);
          return;
        }
        if (depthArg == "8" || depthArg == "8BIT" || depthArg == "8BPC") {
          setOutputBitDepthMode(8);
          return;
        }
        if (depthArg == "10" || depthArg == "10BIT" || depthArg == "10BPC") {
          setOutputBitDepthMode(10);
          return;
        }
        return;
      }
      if (value == "8BIT" || value == "8BPC") {
        setOutputBitDepthMode(8);
        return;
      }
      if (value == "10BIT" || value == "10BPC") {
        setOutputBitDepthMode(10);
        return;
      }
      if (value == "NATIVE" || value == "AUTO" || value == "DISPLAY") {
        setOutputSizingModeDisplayNative();
        return;
      }
      if (value == "SIZE") {
        if (parts.size() > 2) {
          std::string sub = toUpper(parts[2]);
          if (sub == "DISPLAY" || sub == "NATIVE") {
            setOutputSizingModeDisplayNative();
            return;
          }
        }
        sizeFocusedOutputToSelectedDisplay();
        return;
      }
      if (value == "4K" || value == "UHD" || value == "2160P" || value == "2160") {
        setOutputSizingModeFixed(3840, 2160);
        return;
      }
      if (value == "1440P" || value == "1440") {
        setOutputSizingModeFixed(2560, 1440);
        return;
      }
      if (value == "1080P" || value == "1080") {
        setOutputSizingModeFixed(1920, 1080);
        return;
      }
      if (value == "720P" || value == "720") {
        setOutputSizingModeFixed(1280, 720);
        return;
      }
      if ((value == "CUSTOM" || value == "SET") && parts.size() > 2) {
        applyRasterToken(parts[2]);
        return;
      }
      if (applyRasterToken(value)) {
        return;
      }
      return;
    }
    if (command == "NDI") {
      if (parts.size() == 1) {
        toggleFocusedOutputNdi();
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "ON") {
        setFocusedOutputNdiEnabled(true);
      } else if (value == "OFF") {
        setFocusedOutputNdiEnabled(false);
      } else if (value == "TOGGLE") {
        toggleFocusedOutputNdi();
      } else if (value == "KEY") {
        if (parts.size() == 2) {
          toggleFocusedOutputNdiKey();
        } else {
          std::string keyValue = toUpper(parts[2]);
          if (keyValue == "ON") {
            setFocusedOutputNdiKeyEnabled(true);
          } else if (keyValue == "OFF") {
            setFocusedOutputNdiKeyEnabled(false);
          } else if (keyValue == "TOGGLE") {
            toggleFocusedOutputNdiKey();
          } else if (keyValue == "NAME") {
            setFocusedOutputNdiKeyName(joinParts(parts, 3));
          } else if (keyValue == "DEFAULT" || keyValue == "CLEAR") {
            setFocusedOutputNdiKeyName("");
          }
        }
      } else if (value == "NAME") {
        setFocusedOutputNdiName(joinParts(parts, 2));
      } else if (value == "KEYNAME") {
        setFocusedOutputNdiKeyName(joinParts(parts, 2));
      } else if (value == "DEFAULT" || value == "CLEAR") {
        setFocusedOutputNdiName("");
      } else if (value == "STATUS") {
        triggerToast("ndi: " + currentNdiOutputLabel());
      }
      return;
    }
    if (command == "NDINAME") {
      setFocusedOutputNdiName(joinParts(parts, 1));
      return;
    }
    if (command == "NDIKEY" || command == "NDIKEYER") {
      if (parts.size() <= 1) {
        toggleFocusedOutputNdiKey();
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "ON") {
        setFocusedOutputNdiKeyEnabled(true);
      } else if (value == "OFF") {
        setFocusedOutputNdiKeyEnabled(false);
      } else if (value == "TOGGLE") {
        toggleFocusedOutputNdiKey();
      } else if (value == "NAME") {
        setFocusedOutputNdiKeyName(joinParts(parts, 2));
      } else if (value == "DEFAULT" || value == "CLEAR") {
        setFocusedOutputNdiKeyName("");
      }
      return;
    }
    if (command == "NDIKEYNAME") {
      setFocusedOutputNdiKeyName(joinParts(parts, 1));
      return;
    }
    if (command == "BLACKOUT") {
      std::string val = parts.size() > 1 ? toUpper(parts[1]) : "TOGGLE";
      if (val == "ON")           masterDimmerTarget_ = 0.0;
      else if (val == "OFF")     masterDimmerTarget_ = 1.0;
      else if (val == "TOGGLE")  masterDimmerTarget_ = (masterDimmerTarget_ < 0.5) ? 1.0 : 0.0;
      else if (auto v = parseNumber(1); v) masterDimmerTarget_ = std::clamp(*v, 0.0, 1.0);
      triggerToast(masterDimmerTarget_ < 0.5 ? "blackout ON" : "blackout off");
      markProjectDirty();
      return;
    }
    if (command == "DIMMER") {
      auto value = parseNumber(1);
      if (value) {
        // 0-100 range
        masterDimmerTarget_ = std::clamp(*value / 100.0, 0.0, 1.0);
        triggerToast("dimmer " + std::to_string(static_cast<int>(std::round(masterDimmerTarget_ * 100.0))) + "%");
        markProjectDirty();
      }
      return;
    }
    if (command == "MASTERVOL" || command == "MASTERVOLUME") {
      auto value = parseNumber(1);
      if (value) {
        project_.masterVolume = std::clamp(*value, 0.0, 2.0);
        int pct = static_cast<int>(std::round(project_.masterVolume * 100.0));
        triggerToast("master vol " + std::to_string(pct) + "%");
        markProjectDirty();
      }
      return;
    }
    if (command == "SPEED") {
      auto value = parseNumber(1);
      if (value && *value > 0.0) {
        if (Cue* cue = selectedCueMutable()) {
          if (cue->kind == CueKind::Video || cue->kind == CueKind::Audio) {
            cue->playbackSpeed = std::clamp(*value, 0.25, 4.0);
            refreshFocusedLiveCueRuntimeIfSelected();
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << cue->playbackSpeed;
            triggerToast("speed " + ss.str() + "x");
            markProjectDirty();
          }
        }
      }
      return;
    }
    if (command == "SCALE") {
      // Backward compatibility: SCALE sets both X and Y
      auto value = parseNumber(1);
      if (value && *value > 0.0) {
        if (Cue* cue = selectedCueMutable()) {
          cue->outputScaleX = std::clamp(*value, 0.25, 4.0);
          cue->outputScaleY = std::clamp(*value, 0.25, 4.0);
          std::ostringstream ss;
          ss << std::fixed << std::setprecision(2) << cue->outputScaleX;
          triggerToast("scale " + ss.str() + "x");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "SCALEX") {
      auto value = parseNumber(1);
      if (value && *value > 0.0) {
        if (Cue* cue = selectedCueMutable()) {
          cue->outputScaleX = std::clamp(*value, 0.25, 4.0);
          std::ostringstream ss;
          ss << std::fixed << std::setprecision(2) << cue->outputScaleX;
          triggerToast("scale X " + ss.str() + "x");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "SCALEY") {
      auto value = parseNumber(1);
      if (value && *value > 0.0) {
        if (Cue* cue = selectedCueMutable()) {
          cue->outputScaleY = std::clamp(*value, 0.25, 4.0);
          std::ostringstream ss;
          ss << std::fixed << std::setprecision(2) << cue->outputScaleY;
          triggerToast("scale Y " + ss.str() + "x");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "COLOR" || command == "COLORTAG") {
      std::string tag = parts.size() > 1 ? toLower(parts[1]) : "";
      if (tag == "none" || tag == "clear") tag = "";
      static const std::vector<std::string> kValid =
        {"", "red", "orange", "yellow", "cyan", "blue", "purple", "pink"};
      if (std::find(kValid.begin(), kValid.end(), tag) != kValid.end()) {
        if (Cue* cue = selectedCueMutable()) {
          cue->colorTag = tag;
          triggerToast("color: " + (tag.empty() ? "none" : tag));
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "LOOPCOUNT") {
      auto value = parseNumber(1);
      if (value) {
        if (Cue* cue = selectedCueMutable()) {
          if (cue->kind == CueKind::Video) {
            cue->loopCount = std::max(0, static_cast<int>(*value));
            triggerToast(cue->loopCount == 0 ? "repeats: inf" : "repeats: " + std::to_string(cue->loopCount));
            markProjectDirty();
          }
        }
      }
      return;
    }
    if (command == "CUENOTES") {
      if (parts.size() < 2) return;
      std::string token = parts[1];
      std::string text = parts.size() > 2 ? joinParts(parts, 2) : "";
      Deck& deck = focusedDeckMutable();
      auto index = cueIndexByToken(deck, token);
      if (index) {
        deck.cues[*index].notes = text;
        triggerToast("notes set");
        markProjectDirty();
      }
      return;
    }
  }
