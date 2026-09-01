// ============================================================================
// app_remote_command.ipp — Remote command handler for OSC and Companion.
//
// Processes text-based remote commands received via OSC, Companion, or
// other network integrations. Commands follow a simple verb + args format:
//
//   Transport: TAKE, GO, PLAY, PAUSE, STOP, RERACK, CLEAR, SKIP, SKIPBACK
//   Navigation: NEXT, PREV, GOTO <index>, SELECT <index>, FIND <text>
//   Cue control: LOOP ON/OFF, VOLUME <0-100>, SPEED <factor>, AUDIOGAIN <dB>
//   Output: OUT ON/OFF, DIMMER <0-100>, BLACKOUT, FULLSCREEN ON/OFF, RECORD
//   Query: STATUS, STATUS JSON, STATUS CUES, HELP (answered on the socket by
//     maybeRespondToCompanionQuery, not here)
//
// Anything not matched here falls off the end of handleRemoteCommand, which
// clears remoteCommandRecognized_ so the caller gets an ERR rather than
// silence. Keep this list honest: it previously advertised SAVE/LOAD/RELOAD/
// FADE, none of which were ever implemented.
//
// Also handles OSC address-based routing (/deck/1/go, /cue/select, etc.)
// and Companion button feedback updates.
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Process a remote command string (from OSC, Companion, or other sources).
  // Splits the command into verb + arguments and dispatches to the handler.
  void handleRemoteCommand(const std::string& rawCommand) {
    // Cleared only by falling off the end of this function (see the note
    // there); processRemoteCommands reads it to answer the caller OK or ERR.
    // Set here rather than at the call site so a nested dispatch — "DECK 1 GO"
    // re-entering with "GO" — reports on the verb that actually ran.
    remoteCommandRecognized_ = true;
    remoteCommandError_.clear();
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
    if (command == "ENCODE" || command == "CONVERT") {
      // Queue the selected cue(s) for transcode. Deliberately NOT in the
      // integration-safe list: encoding is heavy work, not a transport verb.
      convertSelectedCueMedia();
      return;
    }
    if (command == "ENCODEALL") {
      convertAllFlaggedCues();
      return;
    }
    if (command == "ENCODEFORMAT") {
      if (parts.size() < 2) {
        // No argument: list what this ffmpeg can actually do.
        std::string names;
        for (const EncoderFormat& f : encoderFormatCatalog()) {
          if (!encoderFormatAvailable(f)) continue;
          if (!names.empty()) names += " ";
          names += f.id;
        }
        failRemoteCommand("format: " + names);
        return;
      }
      std::string id = parts[1];
      std::transform(id.begin(), id.end(), id.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (!setEncoderFormat(id)) {
        failRemoteCommand("unknown or unavailable format: " + parts[1]);
      }
      return;
    }
    if (command == "MOSHLOOK") {
      if (parts.size() >= 2) {
        std::string v = toUpper(parts[1]);
        if (v == "CHUNKY" || v == "CLASSIC") moshClassicLook_ = true;
        else if (v == "SMOOTH" || v == "MODERN") moshClassicLook_ = false;
        else { failRemoteCommand("moshlook: smooth | chunky"); return; }
        if (encoderFormatId_ == "datamosh" || encoderFormatId_ == "datamosh_classic") {
          encoderFormatId_ = activeMoshFormatId();
        }
      } else {
        toggleMoshLook();
        return;
      }
      triggerToast(std::string("datamosh look: ") + moshLookLabel());
      return;
    }
    // Stage timer control. Deliberately separate from transport: these change
    // the CLOCK while the cue stays on air.
    if (command == "TIMER") {
      std::string sub = parts.size() > 1 ? toUpper(parts[1]) : "TOGGLE";
      if (sub == "START" || sub == "GO" || sub == "PAUSE" || sub == "TOGGLE") {
        timerToggleRun();
      } else if (sub == "RESET") {
        timerReset();
      } else if (sub == "ADD" || sub == "PLUS") {
        timerNudge(parts.size() > 2 ? std::atof(parts[2].c_str()) : 60.0);
      } else if (sub == "SUB" || sub == "MINUS") {
        timerNudge(-(parts.size() > 2 ? std::atof(parts[2].c_str()) : 60.0));
      } else if (sub == "SET") {
        if (parts.size() > 2) {
          timerSetRemaining(std::atof(parts[2].c_str()));
        } else {
          failRemoteCommand("timer set: needs seconds remaining");
        }
      } else {
        failRemoteCommand("timer: start | pause | reset | add <s> | sub <s> | set <s>");
      }
      return;
    }
    if (command == "MARKER" || command == "MARK") {
      std::string sub = parts.size() > 1 ? toUpper(parts[1]) : "ADD";
      if (sub == "ADD" || sub == "SET")        addMarkerAtPlayhead();
      else if (sub == "NEXT")                  jumpToMarker(1);
      else if (sub == "PREV" || sub == "PREVIOUS") jumpToMarker(-1);
      else if (sub == "CLEAR")                 clearMarkers();
      else failRemoteCommand("marker: add | next | prev | clear");
      return;
    }
    if (command == "SCHEDULE") {
      Cue* cue = selectedCueMutable();
      if (!cue) { failRemoteCommand("schedule: select a cue"); return; }
      if (parts.size() < 2) {
        failRemoteCommand("schedule: HH:MM[:SS] | off");
        return;
      }
      if (toUpper(parts[1]) == "OFF" || toUpper(parts[1]) == "NONE") {
        cue->scheduledStartSeconds = -1.0;
        cue->scheduledStartFired = false;
        triggerToast("schedule cleared");
        markProjectDirty();
        return;
      }
      int hh = 0, mm = 0, ss = 0;
      if (std::sscanf(parts[1].c_str(), "%d:%d:%d", &hh, &mm, &ss) < 2) {
        failRemoteCommand("schedule: HH:MM[:SS] | off");
        return;
      }
      cue->scheduledStartSeconds = hh * 3600.0 + mm * 60.0 + ss;
      cue->scheduledStartFired = false;
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hh, mm, ss);
      triggerToast(std::string("scheduled for ") + buf);
      markProjectDirty();
      return;
    }
    if (command == "TIMERCUE" || command == "ADDTIMER") {
      int seconds = 300;
      if (auto v = parseNumber(1); v && *v > 0.0) {
        seconds = static_cast<int>(*v);
      }
      addTimerCue(seconds);
      return;
    }
    if (command == "DATAMOSH" || command == "MOSH") {
      toggleSelectedDatamosh();
      return;
    }
    if (command == "ENCODEPRESET") {
      if (parts.size() < 2) {
        failRemoteCommand("preset: delivery | proxy | match | datamosh");
        return;
      }
      std::string v = toUpper(parts[1]);
      if (v == "DELIVERY" || v == "H264")   setEncoderPreset(EncoderPreset::DeliveryH264);
      else if (v == "PROXY")                setEncoderPreset(EncoderPreset::Proxy);
      else if (v == "MATCH")                setEncoderPreset(EncoderPreset::MatchSource);
      else if (v == "DATAMOSH" || v == "MOSH") setEncoderPreset(EncoderPreset::DatamoshFriendly);
      else failRemoteCommand("preset: delivery | proxy | match | datamosh");
      return;
    }
    if (command == "ENCODEPAUSE") {
      encoderQueuePaused_ = !encoderQueuePaused_;
      triggerToast(encoderQueuePaused_ ? "encoder queue paused" : "encoder queue running");
      pumpConversionQueue();
      return;
    }
    if (command == "RERACK") {
      // Documented in this file's header and whitelisted for integration
      // triggers since forever, but never actually implemented — a RERACK over
      // the wire did nothing at all. It is one of the four transport buttons.
      rerackTransport();
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
    // MIDI input had no remote command at all — it could only be toggled from
    // the Audio settings tab, so a Companion surface could not arm it.
    if (command == "MIDI" || command == "MIDIINPUT" || command == "MIDI_INPUT") {
      auto state = parseToggleWord(1);
      bool want = state ? *state : !midiEnabled_;
      if (want) {
        midiEnabled_ = startMidiInput();
        if (!midiEnabled_) {
          // MIDI ON answered OK whether or not it turned anything on, which is
          // the failure this protocol is explicitly built to avoid: a surface
          // gets an acknowledgement and the operator believes the deck is
          // listening. The reason travels with it now -- the port named in the
          // show is missing, or there is no MIDI input on the machine at all.
          failRemoteCommand(midiDeviceName_().empty()
                              ? std::string("no midi input device")
                              : (midiDeviceName_() + " not found"));
          return;
        }
        triggerToast("midi: on");
      } else {
        stopMidiInput();
        midiEnabled_ = false;
        triggerToast("midi: off");
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
    if (command == "SKIP") {
      // Take the natural next cue immediately (goto/shuffle/loop aware) —
      // same as the >| transport button / "." key.
      skipToNextCue();
      return;
    }
    if (command == "SKIPBACK") {
      // Take the previous playable cue — <| button / "," key.
      skipToPrevCue();
      return;
    }
    // ── VJ <sub> ──────────────────────────────────────────────────────────
    // The mixer over the wire, so it can be driven from a controller and
    // tested from a script. A crossfader is a fader, and a fader is the one
    // control nobody wants to be reaching for with a mouse.
    if (command == "VJ") {
      const std::string sub = parts.size() < 2 ? std::string("STATUS") : toUpper(parts[1]);
      if (sub == "ON" || sub == "OFF") {
        setVjMode(sub == "ON");
        remoteCommandDetail_ = project_.vjModeEnabled ? "on" : "off";
        return;
      }
      // TOGGLE, because a button on a control surface has one action and two
      // meanings. Without it a Stream Deck key needs to know which state the
      // app is in before it can pick between ON and OFF, which is exactly the
      // thing the surface is there to save you.
      if (sub == "TOGGLE") {
        setVjMode(!project_.vjModeEnabled);
        remoteCommandDetail_ = project_.vjModeEnabled ? "on" : "off";
        return;
      }
      if (sub == "MIX" && parts.size() >= 3) {
        setVjMix(std::atof(parts[2].c_str()));
        return;
      }
      if (sub == "BLEND" && parts.size() >= 3) {
        setVjBlend(toLower(parts[2]));
        return;
      }
      if (sub == "TAP") {
        std::ostringstream bpm;
        bpm << std::fixed << std::setprecision(1) << tapVjTempo();
        remoteCommandDetail_ = bpm.str() + " bpm";
        return;
      }
      if (sub == "BPM" && parts.size() >= 3) {
        setVjTempo(std::atof(parts[2].c_str()));
        return;
      }
      if (sub == "QUANTISE" && parts.size() >= 3) {
        project_.vjQuantiseTakes = (toLower(parts[2]) == "on");
        markProjectDirty();
        return;
      }
      if (sub == "DECKS" && parts.size() >= 4) {
        const int deckCount = static_cast<int>(project_.decks.size());
        project_.vjDeckA = std::clamp(std::atoi(parts[2].c_str()) - 1, 0, deckCount - 1);
        project_.vjDeckB = std::clamp(std::atoi(parts[3].c_str()) - 1, 0, deckCount - 1);
        markProjectDirty();
        return;
      }
      if (sub == "STATUS") {
        std::ostringstream state;
        state << (project_.vjModeEnabled ? "on" : "off")
              << " A=" << (project_.vjDeckA + 1) << " B=" << (project_.vjDeckB + 1)
              << " mix=" << std::fixed << std::setprecision(2) << project_.vjMixPosition
              << " " << project_.vjBlendMode
              << " " << std::setprecision(1) << project_.vjTempoBpm << "bpm"
              << (project_.vjQuantiseTakes ? " quantised" : "");
        remoteCommandDetail_ = state.str();
        return;
      }
      failRemoteCommand("VJ: expected ON|OFF|TOGGLE|MIX <0-1>|BLEND <mode>|TAP|"
                        "BPM <n>|QUANTISE <on|off>|DECKS <a> <b>|STATUS");
      return;
    }

    // ASCII <GLYPHS <chars> | PHRASES <a|b|c> | HOLD <seconds>>
    //
    // Text mode could only ever say what the picture's brightness said. These
    // let an operator put their OWN marks and their own words in it, which is
    // the difference between a filter and an instrument.
    // The update checker, from a surface or a script.
    //
    // CHECK asks and reports. DOWNLOAD fetches the installer and verifies its
    // size. INSTALL runs what was downloaded and quits. They are three verbs
    // rather than one because the middle step is the one worth doing ahead of
    // time -- fetching 90MB over a venue connection while nothing is live, and
    // installing later, in the gap.
    if (command == "UPDATE") {
      const std::string sub = parts.size() < 2 ? std::string("STATUS") : toUpper(parts[1]);
      if (sub == "CHECK") {
        checkForUpdateAsync(/*quiet=*/false);
        return;
      }
      if (sub == "DOWNLOAD") {
        downloadAndInstallUpdate();
        return;
      }
      if (sub == "INSTALL") {
        runDownloadedUpdate();
        return;
      }
      if (sub == "STATUS") {
        std::lock_guard<std::mutex> lock(updateMutex_);
        remoteCommandDetail_ = updateStatus_.empty()
          ? std::string("no check has run") : updateStatus_;
        triggerToast("update: " + remoteCommandDetail_);
        return;
      }
      failRemoteCommand("UPDATE: expected check|download|install|status");
      return;
    }

    // Collapse or expand an inspector section.
    //
    // Added because the sections could only be folded by clicking, and folding
    // them is what an operator does to reach the effects -- which is where the
    // layout broke. A whole class of inspector fault had no way to be
    // reproduced except by hand.
    if (command == "SECTION") {
      if (parts.size() < 2) {
        failRemoteCommand("SECTION: expected playback|metadata|geometry|key|effects|timer|tone|synth");
        return;
      }
      const std::string which = toUpper(parts[1]);
      std::optional<QuickAction> action;
      if (which == "PLAYBACK")      action = QuickAction::CueSectionPlaybackToggle;
      else if (which == "METADATA") action = QuickAction::CueSectionMetadataToggle;
      else if (which == "GEOMETRY") action = QuickAction::CueSectionGeometryToggle;
      else if (which == "KEY")      action = QuickAction::CueSectionKeyToggle;
      else if (which == "EFFECTS")  action = QuickAction::CueSectionEffectsToggle;
      else if (which == "TIMER")    action = QuickAction::CueSectionTimerToggle;
      else if (which == "TONE")     action = QuickAction::CueSectionToneToggle;
      else if (which == "SYNTH" || which == "TEXT")
        action = QuickAction::CueSectionVideoSynthToggle;
      if (!action) {
        failRemoteCommand("SECTION: unknown section \"" + parts[1] + "\"");
        return;
      }
      dispatchQuickAction(*action);
      return;
    }

    if (command == "ASCII") {
      // ANY cue whose text mode is on screen -- a video synth cue, where the
      // grid is native, or anything carrying the TEXT MODE effect. This
      // demanded a video synth cue, which is the same assumption that made
      // every control in the inspector's TEXT MODE section inert on a clip.
      Cue* cue = selectedTextModeCueMutable();
      if (!cue) {
        failRemoteCommand(selectedCueMutable()
                            ? "ASCII: the selected cue has no text mode "
                              "(add the TEXT MODE effect, or select a video synth cue)"
                            : "ASCII: no cue selected");
        return;
      }
      const std::string sub = parts.size() < 2 ? std::string("") : toUpper(parts[1]);
      // Text mode itself had no verb at all, so the one mode an operator most
      // wants to flip mid-set could only be reached by clicking.
      if (sub == "ON" || sub == "OFF" || sub == "TOGGLE") {
        cue->videoSynth.ascii = (sub == "TOGGLE") ? !cue->videoSynth.ascii
                                                  : (sub == "ON");
        markProjectDirty();
        refreshAllLiveCueRuntimes();
        triggerToast(cue->videoSynth.ascii ? "text mode on" : "text mode off");
        return;
      }
      // The cycles the inspector rows drive, so the same controls can be
      // reached from a surface -- and, just as usefully, TESTED. Every one of
      // these was dead on a clip and nothing could see it, because the only way
      // to fire them was to click.
      if (sub == "INK") {
        dispatchQuickAction(QuickAction::VsInkCycle);
        return;
      }
      if (sub == "SET" || sub == "CHARSET") {
        dispatchQuickAction(QuickAction::VsCharSetCycle);
        return;
      }
      if (sub == "SHUFFLE") {
        dispatchQuickAction(QuickAction::VsShuffleCycle);
        return;
      }
      if (sub == "CHAOS") {
        auto value = parseNumber(2);
        if (!value) {
          failRemoteCommand("ASCII CHAOS: expected 0..1");
          return;
        }
        cue->videoSynth.asciiChaos = std::clamp(*value, 0.0, 1.0);
        markProjectDirty();
        triggerToast("chaos " + fmtFloat(cue->videoSynth.asciiChaos, 2));
        return;
      }
      if (sub == "COLS" || sub == "COLUMNS") {
        auto value = parseNumber(2);
        if (!value) {
          failRemoteCommand("ASCII COLS: expected a column count");
          return;
        }
        const int want = std::clamp(static_cast<int>(std::lround(*value)), 20, 200);
        // Through the same adjuster the row uses, so the effect parameter is
        // written when there is one and the cue field when there is not.
        int guard = 0;
        while (guard++ < 40) {
          VideoSynthSettings shown = cue->videoSynth;
          if (const deckboy::effects::CueEffect* fx = textModeEffectFor(*cue)) {
            applyTextModeParams(*fx, shown);
          }
          if (shown.asciiCols == want) break;
          dispatchQuickAction(shown.asciiCols < want ? QuickAction::VsAsciiColsInc
                                                     : QuickAction::VsAsciiColsDec);
        }
        return;
      }
      if (sub == "GLYPHS" && parts.size() >= 3) {
        cue->videoSynth.asciiGlyphs = joinParts(parts, 2);
        markProjectDirty();
        triggerToast("glyphs: " + cue->videoSynth.asciiGlyphs);
        return;
      }
      if (sub == "GLYPHS") {   // no argument clears them
        cue->videoSynth.asciiGlyphs.clear();
        markProjectDirty();
        triggerToast("glyphs: the built-in set");
        return;
      }
      if (sub == "PHRASES" && parts.size() >= 3) {
        cue->videoSynth.asciiPhrases = joinParts(parts, 2);
        markProjectDirty();
        triggerToast("phrases set");
        return;
      }
      if (sub == "PHRASES") {
        cue->videoSynth.asciiPhrases.clear();
        markProjectDirty();
        triggerToast("phrases cleared");
        return;
      }
      // GLITCH up down mid reach drift -- the marks that climb out of the
      // characters. Five numbers because they are five independent things and
      // an operator reaching for this wants to push one of them.
      if (sub == "GLITCH" && parts.size() >= 4) {
        auto num = [&](std::size_t i, double fallback) {
          return parts.size() > i ? std::atof(parts[i].c_str()) : fallback;
        };
        cue->videoSynth.asciiZalgoUp = std::clamp(num(2, 0.0), 0.0, 1.0);
        cue->videoSynth.asciiZalgoDown = std::clamp(num(3, 0.0), 0.0, 1.0);
        cue->videoSynth.asciiZalgoMid = std::clamp(num(4, 0.0), 0.0, 1.0);
        cue->videoSynth.asciiZalgoReach =
          std::clamp(static_cast<int>(num(5, 2.0)), 1, 6);
        cue->videoSynth.asciiZalgoDrift = std::clamp(num(6, 0.0), 0.0, 1.0);
        markProjectDirty();
        refreshAllLiveCueRuntimes();
        triggerToast("glitch text set");
        return;
      }
      if (sub == "HOLD" && parts.size() >= 3) {
        const double v = std::atof(parts[2].c_str());
        if (v < 0.0 || v > 60.0) {
          failRemoteCommand("ASCII HOLD: seconds, 0-60 (0 mutes the phrases)");
          return;
        }
        cue->videoSynth.asciiPhraseHold = v;
        markProjectDirty();
        return;
      }
      failRemoteCommand("ASCII: use ON|OFF|TOGGLE | GLYPHS <chars> | "
                          "PHRASES <a|b|c> | HOLD <seconds>");
      return;
    }
    if (command == "CODE") {
      // The code source, over the wire.
      //
      // SET is the useful half: an expression can come from a controller or a
      // script, which is how a code cue gets driven by something other than a
      // person typing. EDIT is the other half and is honestly mostly for
      // testing -- the editor covers the window and scripted clicks and keys do
      // not reach SDL3, so without this there is no way to open it and LOOK at
      // it, and this codebase has learned twice over what happens to UI nobody
      // has looked at.
      Cue* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand("CODE: no cue selected");
        return;
      }
      if (!cueIsCodeSource(*cue)) {
        failRemoteCommand("CODE: the selected cue is not a code source");
        return;
      }
      const std::string sub = parts.size() < 2 ? std::string("get") : toUpper(parts[1]);
      if (sub == "GET") {
        remoteCommandDetail_ = cue->codeExpression;
        triggerToast(cue->codeExpression.empty() ? "no expression"
                                                 : cue->codeExpression);
        return;
      }
      if (sub == "EDIT") {
        openCodeEditor();
        return;
      }
      if (sub == "SET" && parts.size() >= 3) {
        std::string expr = joinParts(parts, 2);
        // BACKSLASH-N BECOMES A NEWLINE. The protocol is line based, so a
        // command cannot contain a real one -- and a source is a sequence of
        // statements now, so without this a controller could only ever send
        // the one-line form. Nothing else is escaped: this is the single
        // character the transport cannot carry.
        std::string unescaped;
        unescaped.reserve(expr.size());
        for (std::size_t i = 0; i < expr.size(); ++i) {
          if (expr[i] == 0x5C && i + 1 < expr.size() && expr[i + 1] == 'n') {
            unescaped.push_back(0x0A);
            ++i;
          } else {
            unescaped.push_back(expr[i]);
          }
        }
        expr = unescaped;
        // Refused rather than accepted-and-broken: a cue whose expression does
        // not compile draws nothing, and finding that out on stage is worse
        // than being told here.
        const auto compiled = deckboy::code::compile(expr);
        if (!compiled.ok()) {
          failRemoteCommand("CODE SET: " + compiled.error);
          return;
        }
        cue->codeExpression = expr;
        markProjectDirty();
        triggerToast("expression set");
        return;
      }
      failRemoteCommand("CODE: use GET | SET <expression> | EDIT");
      return;
    }
    if (command == "FX") {
      // Effect stack over the wire. Added because the effect stack could only
      // be driven by clicking, which meant the RUNTIME path -- adding an effect
      // to a cue that is already playing -- could not be tested at all, and
      // that is exactly where it was reported broken.
      auto* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand("FX: no cue selected");
        return;
      }
      const std::string sub = parts.size() < 2 ? std::string("list") : toUpper(parts[1]);
      if (sub == "LIST") {
        std::string reply;
        for (std::size_t k = 0; k < cue->effects.size(); ++k) {
          const auto& fx = cue->effects[k];
          if (!reply.empty()) reply += ", ";
          reply += std::to_string(k + 1) + ":" +
                   deckboy::effects::cueEffectToken(fx.kind) +
                   (fx.bypassed ? "(byp)" : "");
        }
        triggerToast(reply.empty() ? "no effects" : reply);
        showLog("FX LIST", reply);
        remoteCommandDetail_ = reply.empty() ? "no effects" : reply;
        return;
      }
      if (sub == "ADD" && parts.size() >= 3) {
        const auto kind = deckboy::effects::cueEffectFromToken(toLower(parts[2]));
        if (kind == deckboy::effects::CueEffectKind::None) {
          failRemoteCommand("FX ADD: unknown effect \"" + parts[2] + "\"");
          return;
        }
        const bool wasNeeded = cueNeedsCpuPixelPath(*cue);
        deckboy::effects::CueEffect fx;
        fx.kind = kind;
        fx.amount = parts.size() >= 4
          ? std::clamp(static_cast<float>(std::atof(parts[3].c_str())), 0.0f, 1.0f) : 1.0f;
        fx.paramA = 0.5f;
        cue->effects.push_back(fx);
        markProjectDirty();
        syncDatamoshFromStack();
        refreshLiveCueIfPixelPathChanged(wasNeeded);
        triggerToast(std::string("added ") + deckboy::effects::cueEffectLabel(kind));
        return;
      }
      // The same clipboard the inspector's buttons use, so a chain can be
      // moved from a controller or a script and not only by hand.
      if (sub == "COPY") {
        copySelectedEffectChain();
        return;
      }
      if (sub == "PASTE") {
        pasteSelectedEffectChain();
        return;
      }
      if (sub == "CLEAR") {
        const bool wasNeeded = cueNeedsCpuPixelPath(*cue);
        cue->effects.clear();
        markProjectDirty();
        syncDatamoshFromStack();
        pruneUnusedMotionDriver();   // an emptied stack cannot puppet anything
        refreshLiveCueIfPixelPathChanged(wasNeeded);
        return;
      }
      if (sub == "AMOUNT" && parts.size() >= 4) {
        const int idx = std::atoi(parts[2].c_str()) - 1;
        if (idx < 0 || idx >= static_cast<int>(cue->effects.size())) {
          failRemoteCommand("FX AMOUNT: no effect at that index");
          return;
        }
        const bool wasNeeded = cueNeedsCpuPixelPath(*cue);
        cue->effects[idx].amount =
          std::clamp(static_cast<float>(std::atof(parts[3].c_str())), 0.0f, 1.0f);
        markProjectDirty();
        refreshLiveCueIfPixelPathChanged(wasNeeded);
        return;
      }
      // PARAM <n> <A|B|C|D> <v> -- the shaping controls, over the wire.
      //
      // The same gap FX itself was added to close, one level down. Every effect
      // grew real parameters and the ONLY way to move one was to click it, so
      // the runtime path -- changing a parameter on a cue that is already
      // playing -- could not be driven by a controller, a script or a test. The
      // amount was reachable and the four things that decide what the effect
      // actually looks like were not.
      if (sub == "PARAM" && parts.size() >= 5) {
        const int idx = std::atoi(parts[2].c_str()) - 1;
        if (idx < 0 || idx >= static_cast<int>(cue->effects.size())) {
          failRemoteCommand("FX PARAM: no effect at that index");
          return;
        }
        const std::string slotName = toUpper(parts[3]);
        if (slotName.size() != 1 || slotName[0] < 'A' || slotName[0] > 'D') {
          failRemoteCommand("FX PARAM: slot must be A, B, C or D");
          return;
        }
        const int slot = slotName[0] - 'A';
        auto& fx = cue->effects[idx];
        // Refused rather than clamped, the same as MASTERVOL: a value out of
        // range is a caller bug, and silently clamping it is how MASTERVOL hid
        // a unit mismatch for years.
        const double value = std::atof(parts[4].c_str());
        if (value < 0.0 || value > 1.0) {
          failRemoteCommand("FX PARAM: value is 0-1");
          return;
        }
        // Named here too, so a script gets told when it is setting a slot the
        // effect does not use rather than writing into nothing.
        if (!deckboy::effects::cueEffectParamLabel(fx.kind, slot)) {
          failRemoteCommand(std::string("FX PARAM: ") +
                            deckboy::effects::cueEffectToken(fx.kind) +
                            " has no " + slotName + " parameter");
          return;
        }
        const bool wasNeeded = cueNeedsCpuPixelPath(*cue);
        float* slots[4] = {&fx.paramA, &fx.paramB, &fx.paramC, &fx.paramD};
        *slots[slot] = static_cast<float>(value);
        markProjectDirty();
        refreshLiveCueIfPixelPathChanged(wasNeeded);
        triggerToast(std::string(deckboy::effects::cueEffectParamLabel(fx.kind, slot)) +
                     " " + parts[4]);
        return;
      }
      // LFO <n> <A-E> <on|off|shape|rate|depth|phase|beats|sync> [value]
      //
      // E is the effect's AMOUNT, which is the parameter people most often want
      // breathing, and which has no letter of its own anywhere else.
      if (sub == "LFO" && parts.size() >= 4) {
        const int idx = std::atoi(parts[2].c_str()) - 1;
        if (idx < 0 || idx >= static_cast<int>(cue->effects.size())) {
          failRemoteCommand("FX LFO: no effect at that index");
          return;
        }
        const std::string slotName = toUpper(parts[3]);
        if (slotName.size() != 1 || slotName[0] < 'A' || slotName[0] > 'E') {
          failRemoteCommand("FX LFO: slot must be A-D, or E for the amount");
          return;
        }
        auto& lfo = cue->effects[idx].lfo[slotName[0] - 'A'];
        const std::string what = parts.size() > 4 ? toUpper(parts[4]) : std::string("ON");
        const std::string valueText = parts.size() > 5 ? parts[5] : std::string();
        const double value = std::atof(valueText.c_str());
        if (what == "ON")       { lfo.on = true; }
        else if (what == "OFF") { lfo.on = false; }
        else if (what == "SYNC") {
          lfo.beatSync = valueText.empty() || toUpper(valueText) == "ON" ||
                         valueText == "1";
        } else if (what == "SHAPE") {
          const std::string want = toLower(valueText);
          int found = -1;
          for (int s = 0; s < static_cast<int>(deckboy::effects::LfoShape::Count); ++s) {
            if (want == deckboy::effects::lfoShapeToken(
                          static_cast<deckboy::effects::LfoShape>(s))) {
              found = s;
              break;
            }
          }
          if (found < 0) {
            failRemoteCommand("FX LFO SHAPE: sine, triangle, saw, ramp, square or sample");
            return;
          }
          lfo.shape = static_cast<deckboy::effects::LfoShape>(found);
        } else if (what == "RATE") {
          if (value <= 0.0 || value > 40.0) {
            failRemoteCommand("FX LFO RATE: hertz, 0-40");
            return;
          }
          lfo.rateHz = static_cast<float>(value);
        } else if (what == "DEPTH" || what == "PHASE") {
          if (value < 0.0 || value > 1.0) {
            failRemoteCommand("FX LFO " + what + ": 0-1");
            return;
          }
          (what == "DEPTH" ? lfo.depth : lfo.phase) = static_cast<float>(value);
        } else if (what == "BEATS") {
          if (value < 0.25 || value > 64.0) {
            failRemoteCommand("FX LFO BEATS: 0.25-64");
            return;
          }
          lfo.beats = static_cast<float>(value);
        } else {
          failRemoteCommand("FX LFO: on | off | shape <s> | rate <hz> | "
                            "depth <0-1> | phase <0-1> | sync <on|off> | beats <n>");
          return;
        }
        markProjectDirty();
        return;
      }
      failRemoteCommand("FX: use LIST | ADD <effect> [amount] | AMOUNT <n> <v> | "
                        "PARAM <n> <A-D> <0-1> | LFO <n> <A-E> ... | "
                        "COPY | PASTE | CLEAR");
      return;
    }
    if (command == "GOEND" || command == "SKIPEND") {
      // Jump the playing cue to its last moment. The action existed and worked
      // and NOTHING could reach it -- no button, no key, no verb -- which an
      // audit of all 258 QuickActions turned up. A transport action nobody can
      // invoke is the same defect as a control that does nothing, facing the
      // other way.
      dispatchQuickAction(QuickAction::TransportSkipEnd);
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
    // Notes from a MIDI keyboard, routed here rather than acted on in the
    // MIDI thread: everything that touches a cue or an engine has to happen on
    // the main thread, and the remote queue is the existing road for that.
    if (command == "SYNTHNOTEON" || command == "SYNTHNOTEOFF") {
      if (parts.size() < 2) {
        failRemoteCommand(command + ": needs a note number");
        return;
      }
      int note = 60;
      int velocity = 100;
      try {
        note = std::stoi(parts[1]);
        if (parts.size() > 2) velocity = std::stoi(parts[2]);
      } catch (...) {
        failRemoteCommand(command + ": note must be a number");
        return;
      }
      Cue* cue = liveSynthCue();
      if (!cue) {
        // Said out loud rather than silently dropped: an operator playing keys
        // with no synth on air needs to know it is the routing, not the
        // keyboard.
        failRemoteCommand("no synth cue is live");
        return;
      }
      MediaEngine* engine = liveSynthEngine();
      if (!engine) {
        failRemoteCommand("synth cue has no engine");
        return;
      }
      const double hz = synthNoteToHz(note, cue->tone.synth.tuning,
                                      cue->tone.synth.referenceHz);
      if (command == "SYNTHNOTEON") {
        engine->synthNoteOn(hz, velocity);
      } else {
        engine->synthNoteOff(hz);
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
    // IN and OUT answered OK whether or not they moved anything -- no number
    // parsed, nothing selected, or a cue kind that cannot be trimmed all looked
    // like success to a surface. Say which.
    if (command == "IN" || command == "TRIMIN") {
      auto value = parseNumber(1);
      if (!value) {
        failRemoteCommand("expected a number of seconds");
      } else if (!setSelectedTrimIn(*value)) {
        failRemoteCommand("no trimmable cue selected");
      }
      return;
    }
    if (command == "OUT" || command == "TRIMOUT") {
      auto value = parseNumber(1);
      if (!value) {
        failRemoteCommand("expected a number of seconds");
      } else if (!setSelectedTrimOut(*value)) {
        failRemoteCommand("no trimmable cue selected");
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
        project_.uiSoundsEnabled = *state && uiAudioStream_ != nullptr;
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
            double alpha = std::stod(parts[3]);
            if (alpha > 1.0) {
              alpha /= 100.0;
            }
            setFocusedOutputAlpha(static_cast<float>(alpha));
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
    if (command == "DECKLINK") {
      int foIdx = project_.focusedOutputIndex;
      if (foIdx < 0 || foIdx >= static_cast<int>(project_.outputs.size())) {
        return;
      }
      OutputTarget& output = project_.outputs[foIdx];
      std::string sub = parts.size() > 1 ? toUpper(parts[1]) : "TOGGLE";
      if (sub == "ON") {
        // Goes through the dep-gated wrapper; toast lives there.
        setFocusedOutputDeckLinkEnabled(true);
      } else if (sub == "OFF") {
        setFocusedOutputDeckLinkEnabled(false);
      } else if (sub == "TOGGLE") {
        toggleFocusedOutputDeckLink();
      } else if (sub == "DEVICE") {
        auto val = parseNumber(2);
        if (val) {
          output.deckLinkDeviceId = static_cast<int>(*val);
          triggerToast("DeckLink device " + std::to_string(output.deckLinkDeviceId));
        }
      } else if (sub == "MODE") {
        if (parts.size() > 2) {
          output.deckLinkMode = parts[2];
          triggerToast("DeckLink mode " + output.deckLinkMode);
        }
      } else if (sub == "10BIT") {
        std::string bitSub = parts.size() > 2 ? toUpper(parts[2]) : "TOGGLE";
        if (bitSub == "ON") output.deckLink10Bit = true;
        else if (bitSub == "OFF") output.deckLink10Bit = false;
        else output.deckLink10Bit = !output.deckLink10Bit;
        triggerToast(output.deckLink10Bit ? "DeckLink 10-bit ON" : "DeckLink 10-bit off");
      }
      markProjectDirty();
      return;
    }
    if (command == "LTCOUT") {
      std::string sub = parts.size() > 1 ? toUpper(parts[1]) : "STATUS";
      if (sub == "STATUS") {
        char buf[200];
        const int queued = ltcOutStream_
          ? std::max(0, SDL_GetAudioStreamQueued(ltcOutStream_)) : 0;
        std::snprintf(buf, sizeof(buf),
                      "ltcout %s fps=%.2f queued=%dB emitted=%lld tc=%s dev=%s",
                      project_.ltcOutputEnabled ? "on" : "off",
                      project_.ltcOutputFps, queued,
                      static_cast<long long>(ltcOutEmittedFrames_),
                      formatTimecode(ltcOutputTimecodeSeconds(),
                                     std::clamp(project_.ltcOutputFps, 23.0, 60.0)).c_str(),
                      ltcOutDeviceName_.empty() ? "(default)" : ltcOutDeviceName_.c_str());
        // stdout so a scripted check can read it; toast for the operator.
        std::cout << buf << std::endl;
        triggerToast(buf);
        return;
      }
      if (sub == "ON" || sub == "OFF" || sub == "TOGGLE") {
        project_.ltcOutputEnabled = (sub == "ON") ? true
                                  : (sub == "OFF") ? false : !project_.ltcOutputEnabled;
        if (!project_.ltcOutputEnabled) {
          stopLtcOutput();
          triggerToast("ltc out: off");
        }
        markProjectDirty();
        return;
      }
      if (sub == "FPS" && parts.size() > 2) {
        if (auto v = parseNumber(2); v) {
          project_.ltcOutputFps = std::clamp(*v, 23.0, 60.0);
          triggerToast("ltc out fps: " + fmtFloat(project_.ltcOutputFps, 2));
          markProjectDirty();
        }
        return;
      }
      if (sub == "DEVICE") {
        project_.ltcOutputDeviceName = parts.size() > 2 ? trim(joinParts(parts, 2)) : std::string();
        stopLtcOutput();  // reopen on the new device next tick
        triggerToast("ltc out device: " + (project_.ltcOutputDeviceName.empty()
                                           ? std::string("(default)")
                                           : project_.ltcOutputDeviceName));
        markProjectDirty();
        return;
      }
      return;
    }
    if (command == "ST2110") {
      int foIdx = project_.focusedOutputIndex;
      if (foIdx < 0 || foIdx >= static_cast<int>(project_.outputs.size())) {
        return;
      }
      OutputTarget& output = project_.outputs[foIdx];
      std::string sub = parts.size() > 1 ? toUpper(parts[1]) : "STATUS";
      if (sub == "STATUS") {
        triggerToast("st2110 " + std::string(output.st2110Enabled ? "on " : "off ")
                     + output.st2110Address + ":" + std::to_string(output.st2110Port)
                     + (output.st2110TenBit ? " 10-bit" : " 8-bit"));
        return;
      }
      if (sub == "ON" || sub == "OFF" || sub == "TOGGLE") {
        output.st2110Enabled = (sub == "ON") ? true
                             : (sub == "OFF") ? false : !output.st2110Enabled;
        if (!output.st2110Enabled) {
          shutdownOutputSt2110(outputRuntimes_[foIdx]);
        }
        triggerToast(std::string("st2110: ") + (output.st2110Enabled ? "on" : "off"));
      } else if (sub == "ADDR" || sub == "GROUP") {
        if (parts.size() > 2) {
          output.st2110Address = trim(parts[2]);
          shutdownOutputSt2110(outputRuntimes_[foIdx]);
          triggerToast("st2110 group " + output.st2110Address);
        }
      } else if (sub == "PORT") {
        if (auto val = parseNumber(2); val) {
          output.st2110Port = std::clamp(static_cast<int>(*val), 1, 65535);
          shutdownOutputSt2110(outputRuntimes_[foIdx]);
          triggerToast("st2110 port " + std::to_string(output.st2110Port));
        }
      } else if (sub == "DEPTH") {
        std::string d = parts.size() > 2 ? toUpper(parts[2]) : "TOGGLE";
        output.st2110TenBit = (d == "10") ? true : (d == "8") ? false : !output.st2110TenBit;
        shutdownOutputSt2110(outputRuntimes_[foIdx]);
        triggerToast(output.st2110TenBit ? "st2110 10-bit" : "st2110 8-bit");
      } else if (sub == "SDP") {
        // Printed to stdout so a scripted receiver can capture it directly.
        std::cout << focusedOutputSt2110Sdp() << std::flush;
        triggerToast("st2110 sdp written to stdout");
      }
      markProjectDirty();
      return;
    }
    if (command == "NMOS") {
      // Mirrors ST2110 above. NMOS is machine-wide, so unlike ST2110 there is
      // no focused-output lookup here.
      std::string sub = parts.size() > 1 ? toUpper(parts[1]) : "STATUS";
      if (sub == "STATUS") {
        // stdout as well as a toast: this is the one command a test harness
        // needs to read back, and a toast is not capturable.
        std::cout << nmosStatusLabel()
                  << "  node=" << (nmosNode_.nodeApiUrl().empty()
                                     ? std::string("-") : nmosNode_.nodeApiUrl())
                  << "  registry=" << (trim(project_.nmosRegistryUrl).empty()
                                         ? std::string("-") : trim(project_.nmosRegistryUrl))
                  << std::endl;
        triggerToast(nmosStatusLabel());
        return;
      }
      if (sub == "ON" || sub == "OFF" || sub == "TOGGLE") {
        project_.nmosEnabled = (sub == "ON") ? true
                             : (sub == "OFF") ? false : !project_.nmosEnabled;
        if (!project_.nmosEnabled) {
          shutdownNmosNode();
        }
        triggerToast(std::string("nmos: ") + (project_.nmosEnabled ? "on" : "off"));
      } else if (sub == "REGISTRY") {
        // Bare "NMOS REGISTRY" clears it — that is how you deliberately go back
        // to serving the node API with no registration.
        const std::string url = parts.size() > 2 ? trim(parts[2]) : std::string();
        if (!url.empty()) {
          std::string host, path;
          int port = 0;
          if (!deckboy::platform::video::nmosParseUrl(url, host, port, path)) {
            triggerToast("nmos: need http://host:port");
            return;
          }
        }
        project_.nmosRegistryUrl = url;
        triggerToast("nmos registry " + (url.empty() ? std::string("cleared") : url));
      } else if (sub == "PORT") {
        if (auto val = parseNumber(2); val) {
          project_.nmosPort = std::clamp(static_cast<int>(*val), 1, 65535);
          triggerToast("nmos port " + std::to_string(project_.nmosPort));
        }
      } else if (sub == "NIC" || sub == "INTERFACE") {
        if (parts.size() > 2) {
          project_.nmosInterfaceName = trim(parts[2]);
          triggerToast("nmos nic " + project_.nmosInterfaceName);
        }
      }
      markProjectDirty();
      syncNmosNode();   // apply immediately so a scripted STATUS reads the truth
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
    if (command == "RECFORMAT") {
      // RECFORMAT <WxH|program> [fps|program] -- the recording's STANDARD,
      // independent of the programme raster and of the display.
      if (parts.size() < 2) {
        failRemoteCommand("recformat: expected <WxH|program> [fps]");
        return;
      }
      const std::string raster = toUpper(parts[1]);
      if (raster == "PROGRAM" || raster == "PROGRAMME") {
        project_.recordingWidth = 0;
        project_.recordingHeight = 0;
      } else {
        int w = 0, h = 0;
        if (std::sscanf(parts[1].c_str(), "%dx%d", &w, &h) != 2 ||
            w < 16 || h < 16 || w > 7680 || h > 4320) {
          failRemoteCommand("recformat: bad raster '" + parts[1] + "'");
          return;
        }
        // Even dimensions: yuv420p has half-resolution chroma and an odd
        // raster cannot be encoded.
        project_.recordingWidth = w & ~1;
        project_.recordingHeight = h & ~1;
      }
      if (parts.size() > 2) {
        const std::string rateTok = toUpper(parts[2]);
        if (rateTok == "PROGRAM" || rateTok == "PROGRAMME") {
          project_.recordingFps = 0.0;
        } else {
          const double r = std::atof(parts[2].c_str());
          if (!(r > 0.0) || r > 120.0) {
            failRemoteCommand("recformat: bad rate '" + parts[2] + "'");
            return;
          }
          project_.recordingFps = r;
        }
      }
      markProjectDirty();
      triggerToast("recording format " + recordingFormatLabel());
      return;
    }
    if (command == "RECCODEC") {
      // RECCODEC <token>  -- h264|hevc|prores_*|dnxhr_*
      if (parts.size() < 2) {
        failRemoteCommand("reccodec: expected a codec token");
        return;
      }
      const std::string want = toLower(trim(parts[1]));
      const std::string normalized = normalizeRecordingCodec(want);
      if (normalized != want) {
        failRemoteCommand("reccodec: unknown codec '" + parts[1] + "'");
        return;
      }
      project_.recordingCodec = normalized;
      markProjectDirty();
      triggerToast("recording codec " + normalized);
      return;
    }
    if (command == "RECTC") {
      // RECTC <value HH:MM:SS:FF | timeofday> [df|ndf|auto]
      if (parts.size() < 2) {
        failRemoteCommand("rectc: expected <hh:mm:ss:ff|timeofday> [df|ndf|auto]");
        return;
      }
      const std::string first = toLower(trim(parts[1]));
      if (first == "timeofday" || first == "tod") {
        project_.recordingTimecodeMode = "timeofday";
      } else {
        int hh = 0, mm = 0, ss = 0, ff = 0;
        if (std::sscanf(first.c_str(), "%d:%d:%d:%d", &hh, &mm, &ss, &ff) != 4) {
          failRemoteCommand("rectc: bad timecode '" + parts[1] + "'");
          return;
        }
        project_.recordingTimecodeMode = "value";
        project_.recordingTimecodeStart = first;
      }
      if (parts.size() > 2) {
        const std::string df = toLower(trim(parts[2]));
        if (df != "df" && df != "ndf" && df != "auto") {
          failRemoteCommand("rectc: expected df|ndf|auto, got " + parts[2]);
          return;
        }
        project_.recordingTimecodeDropFrame = df;
      }
      markProjectDirty();
      triggerToast("recording tc " + project_.recordingTimecodeMode + " " +
                   project_.recordingTimecodeDropFrame);
      return;
    }
    if (command == "RECSEGMENT") {
      // RECSEGMENT <minutes|0> [megabytes|0]
      if (parts.size() < 2) {
        failRemoteCommand("recsegment: expected <minutes> [megabytes]");
        return;
      }
      project_.recordingSegmentMinutes = std::clamp(std::atoi(parts[1].c_str()), 0, 240);
      if (parts.size() > 2) {
        project_.recordingSegmentMegabytes =
          std::clamp(std::atoi(parts[2].c_str()), 0, 1024 * 1024);
      }
      markProjectDirty();
      triggerToast("segment " + std::to_string(project_.recordingSegmentMinutes) +
                   "min / " + std::to_string(project_.recordingSegmentMegabytes) + "MB");
      return;
    }
    if (command == "RECORD" || command == "REC") {
      // toggleRecording already answers with failRemoteCommand when it cannot
      // create the output, so the caller gets a reason rather than a silent OK.
      const std::string val = parts.size() > 1 ? toUpper(parts[1]) : "TOGGLE";
      const bool rolling = recordingActive();
      if (val == "ON" || val == "START") {
        if (!rolling) toggleRecording();
      } else if (val == "OFF" || val == "STOP") {
        if (rolling) toggleRecording();
      } else if (val == "TOGGLE") {
        toggleRecording();
      } else {
        failRemoteCommand("record: expected on|off|toggle, got " + parts[1]);
      }
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
      // UNITS: PERCENT, 0-200. Everything else in the system already spoke
      // percent — STATE's master_vol, the toast, the Companion module's 0-200
      // action, the MIDI CC and OSC senders — while this handler alone read a
      // 0-2 multiplier and CLAMPED. So "MASTERVOL 60" quietly pinned the show
      // at 200%, and every Companion master-volume press did the same.
      //
      // Explicit spellings: "150%" percent, "1.5x" multiplier. A bare
      // fractional value <= 2 is still read as a multiplier so scripts written
      // against the old units keep meaning what they said. Out of range is
      // refused with a message rather than clamped — the clamp is what made
      // the wrong units invisible.
      if (parts.size() < 2) {
        failRemoteCommand("master vol: expected 0-200 (percent)");
        return;
      }
      std::string token = parts[1];
      bool asMultiplier = false;
      bool asPercent = false;
      if (!token.empty() && (token.back() == 'x' || token.back() == 'X')) {
        token.pop_back();
        asMultiplier = true;
      } else if (!token.empty() && token.back() == '%') {
        token.pop_back();
        asPercent = true;
      }
      double parsed = 0.0;
      try {
        parsed = std::stod(token);
      } catch (...) {
        failRemoteCommand("master vol: not a number (" + parts[1] + ")");
        return;
      }
      const bool legacyMultiplier =
        !asMultiplier && !asPercent && token.find('.') != std::string::npos && parsed <= 2.0;
      const double gain = (asMultiplier || legacyMultiplier) ? parsed : parsed / 100.0;
      if (!(gain >= 0.0) || gain > 2.0) {
        failRemoteCommand("master vol: 0-200% (got " + parts[1] + ")");
        return;
      }
      project_.masterVolume = gain;
      int pct = static_cast<int>(std::round(project_.masterVolume * 100.0));
      triggerToast("master vol " + std::to_string(pct) + "%");
      markProjectDirty();
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
    if (command == "AUDIOGAIN") {
      // Per-cue gain trim in dB (range: kCueAudioGainMinDb..kCueAudioGainMaxDb)
      // — same write path as the inspector gain row, applied live with no
      // decode restart.
      auto value = parseNumber(1);
      if (value && setSelectedAudioGainDb(*value)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "gain %+.1f dB", std::clamp(*value, static_cast<double>(kCueAudioGainMinDb), static_cast<double>(kCueAudioGainMaxDb)));
        triggerToast(buf);
      }
      return;
    }
    if (command == "AUDIOPAN") {
      // Stereo balance -1..+1 (0 = center).
      auto value = parseNumber(1);
      if (value && setSelectedAudioPan(*value)) {
        triggerToast("pan set");
      }
      return;
    }
    if (command == "AUDIOMONO") {
      auto value = parseNumber(1);
      if (value && setSelectedAudioMono(*value >= 0.5)) {
        triggerToast(*value >= 0.5 ? "cue audio: mono" : "cue audio: stereo");
      }
      return;
    }
    if (command == "AUDIONORM") {
      normalizeSelectedCueAudio();  // async; result toasts when the analysis lands
      return;
    }
    if (command == "AUDIOOUTS") {
      // AUDIOOUTS <pair> — 1-based output pair (1 = outs 1-2, 2 = outs 3-4...).
      auto value = parseNumber(1);
      if (value) {
        if (const Cue* cue = selectedCuePtr()) {
          int target = static_cast<int>(*value) - 1;
          adjustSelectedAudioOutPair(target - cue->audioOutputPair);
        }
      }
      return;
    }
    if (command == "WIDTH" || command == "HEIGHT") {
      // Pixel-based size commands — same path as the inspector width/height
      // editors, so the aspect link applies. SCALE/SCALEX/SCALEY below stay
      // as legacy raw-factor commands for existing Companion configs.
      auto value = parseNumber(1);
      if (value && *value > 0.0) {
        bool ok = command == "WIDTH" ? setSelectedWidthPx(*value)
                                     : setSelectedHeightPx(*value);
        if (ok) {
          triggerToast((command == "WIDTH" ? "width " : "height ")
                       + std::to_string(static_cast<int>(std::lround(*value))) + "px"
                       + (project_.geometryAspectLinked ? "  (aspect linked)" : ""));
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

    if (command == "SUBTITLE" || command == "SUBTITLES" || command == "SUB" || command == "CC") {
      if (parts.size() < 2) {
        // Toggle subtitles on active cue
        Cue* cue = activeCueMutable();
        if (cue) {
          cue->subtitleEnabled = !cue->subtitleEnabled;
          triggerToast(cue->subtitleEnabled ? "subtitles on" : "subtitles off");
          markProjectDirty();
        }
        return;
      }
      std::string sub = toUpper(parts[1]);
      if (sub == "ON" || sub == "OFF" || sub == "TOGGLE") {
        Cue* cue = activeCueMutable();
        if (cue) {
          if (sub == "ON") cue->subtitleEnabled = true;
          else if (sub == "OFF") cue->subtitleEnabled = false;
          else cue->subtitleEnabled = !cue->subtitleEnabled;
          triggerToast(cue->subtitleEnabled ? "subtitles on" : "subtitles off");
          markProjectDirty();
        }
        return;
      }
      // CONVERT: read the cue's caption file and write it out in whatever
      // format the destination extension asks for. Deckboy can read SubRip,
      // WebVTT, SCC and TTML, so being able to write three of them makes it
      // the converter an operator would otherwise have gone looking for.
      if (sub == "CONVERT" && parts.size() >= 3) {
        Cue* cue = selectedCueMutable();
        if (!cue || cue->subtitlePath.empty()) {
          failRemoteCommand("SUBTITLE CONVERT: the cue has no caption file");
          return;
        }
        const std::string destination = joinParts(parts, 2);
        const auto target = deckboy::captions::formatForPath(destination);
        if (target == deckboy::captions::Format::Unknown ||
            target == deckboy::captions::Format::Scc ||
            target == deckboy::captions::Format::Ttml) {
          // SCC and TTML are read but not written: writing SCC means encoding
          // 608 byte pairs and choosing a timebase, which is a real piece of
          // work and not one to half-do.
          failRemoteCommand("SUBTITLE CONVERT: destination must be .srt or .vtt");
          return;
        }
        const deckboy::core::SubtitleTrack track = loadSubtitleTrack(*cue);
        if (track.entries.empty()) {
          failRemoteCommand("SUBTITLE CONVERT: nothing to convert");
          return;
        }
        const std::string text = target == deckboy::captions::Format::WebVtt
          ? deckboy::captions::writeWebVtt(track)
          : deckboy::captions::writeSrt(track);
        std::ofstream out(destination, std::ios::binary);
        if (!out) {
          failRemoteCommand("SUBTITLE CONVERT: could not write " + destination);
          return;
        }
        out << text;
        triggerToast("captions converted: " +
                     std::to_string(track.entries.size()) + " lines");
        return;
      }
      if (sub == "FILE" || sub == "PATH" || sub == "SRT") {
        Cue* cue = selectedCueMutable();
        if (cue && parts.size() >= 3) {
          std::string subPath = joinParts(parts, 2);
          // Reject absolute paths and path traversal from remote commands
          if (subPath.find("..") != std::string::npos
              || (!subPath.empty() && (subPath[0] == '/' || subPath[0] == '\\'))
              || (subPath.size() >= 2 && subPath[1] == ':')) {
            triggerToast("subtitle: path rejected (security)");
          } else {
            cue->subtitlePath = subPath;
            triggerToast("subtitle file set");
            markProjectDirty();
          }
        }
        return;
      }
      if (sub == "CLEAR" || sub == "NONE") {
        Cue* cue = selectedCueMutable();
        if (cue) {
          cue->subtitlePath.clear();
          cue->subtitleStreamId.clear();
          triggerToast("subtitles cleared");
          markProjectDirty();
        }
        return;
      }
      return;
    }

    // Fell through every branch: the verb is not one we know. Only reachable
    // this way — every recognized command returns above — so this is what
    // turns an unknown command into an ERR reply instead of silence.
    remoteCommandRecognized_ = false;
  }
