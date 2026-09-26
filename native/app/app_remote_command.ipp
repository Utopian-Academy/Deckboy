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
  static const char* scaleModeToken(ScaleMode mode) {
    switch (mode) {
      case ScaleMode::Fit:      return "fit";
      case ScaleMode::Fill:     return "fill";
      case ScaleMode::Stretch:  return "stretch";
      case ScaleMode::Unscaled: return "unscaled";
    }
    return "fit";
  }

  // One oscillator's property, set from a remote line: parts[whatAt] names
  // it and parts[whatAt + 1] is the value. Shared by FX LFO and GEOLFO so the
  // two verbs cannot grow different spellings of the same settings. Reports
  // its own failure; returns false when there was one.
  bool setLfoFromRemote(deckboy::effects::ParamLfo& lfo,
                        const std::vector<std::string>& parts, std::size_t whatAt,
                        const char* verb) {
    const std::string what = parts.size() > whatAt ? toUpper(parts[whatAt]) : std::string("ON");
    const std::string valueText = parts.size() > whatAt + 1 ? parts[whatAt + 1] : std::string();
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
        failRemoteCommand(std::string(verb) + " SHAPE: sine, triangle, saw, ramp, square or sample");
        return false;
      }
      lfo.shape = static_cast<deckboy::effects::LfoShape>(found);
    } else if (what == "RATE") {
      if (value <= 0.0 || value > 40.0) {
        failRemoteCommand(std::string(verb) + " RATE: hertz, 0-40");
        return false;
      }
      lfo.rateHz = static_cast<float>(value);
    } else if (what == "DEPTH" || what == "PHASE") {
      if (value < 0.0 || value > 1.0) {
        failRemoteCommand(std::string(verb) + " " + what + ": 0-1");
        return false;
      }
      (what == "DEPTH" ? lfo.depth : lfo.phase) = static_cast<float>(value);
    } else if (what == "BEATS") {
      if (value < 0.25 || value > 64.0) {
        failRemoteCommand(std::string(verb) + " BEATS: 0.25-64");
        return false;
      }
      lfo.beats = static_cast<float>(value);
    } else {
      failRemoteCommand(std::string(verb) + ": on | off | shape <s> | rate <hz> | "
                        "depth <0-1> | phase <0-1> | sync <on|off> | beats <n>");
      return false;
    }
    return true;
  }

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

    // A TALLY EDGE, from whichever integration saw it. The watchers run on
    // their own threads and cannot touch the decks, so the edge comes through
    // here and lands on the main thread like every other command.
    if (command == "TALLYEVENT") {
      if (parts.size() > 1) {
        const std::string state = toUpper(parts[1]);
        const std::string source = parts.size() > 2 ? parts[2] : std::string("tally");
        if (state == "ON" || state == "OFF") {
          handleTallyTransition(state == "ON", source.c_str());
        } else {
          failRemoteCommand("TALLYEVENT wants ON or OFF");
        }
      } else {
        failRemoteCommand("TALLYEVENT wants ON or OFF");
      }
      return;
    }
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
      // Each of the three ways out used to be a bare return, which reads as
      // OK: `DECK 3 STATUS` with two decks answered "OK DECK" and then did not
      // run the STATUS at all.
      if (parts.size() < 2) {
        failRemoteCommand("DECK: expected a deck number");
        return;
      }
      int deckIndex = -1;
      try {
        deckIndex = std::stoi(parts[1]) - 1;
      } catch (...) {
        failRemoteCommand("DECK: '" + parts[1] + "' is not a deck number");
        return;
      }
      if (!setFocusedDeckIndex(deckIndex)) {
        failRemoteCommand("DECK: there is no deck " + parts[1] + " (" +
                          std::to_string(project_.decks.size()) + " open)");
        return;
      }
      if (parts.size() > 2) {
        handleRemoteCommand(joinParts(parts, 2));
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
      // AND THE MESSAGE ITSELF WAS FALSE. "single deck only" is not true: the
      // app carries up to kMaxDecks and VJ mode adds the second precisely so
      // there is something to mix. Only this verb cannot do it, so that is
      // what it now says -- a refusal that misdescribes the app sends the
      // caller looking for a limit that is not there, which is what happened.
      //
      // UNDERSTOOD BUT CANNOT ACT, which is failRemoteCommand's whole purpose:
      // the operator still gets the toast and the caller gets the reason. This
      // answered a plain OK, so over the socket a refusal was indistinguishable
      // from success -- somebody drove it, believed decks were being created,
      // and concluded STATUS was lying about decks=1.
      // It can now. This refused for as long as there was no general way to
      // create a deck; master cues made that a real limit rather than a
      // curiosity, because a master fires OTHER decks.
      if (addDeck()) {
        remoteCommandDetail_ = "decks: " + std::to_string(project_.decks.size());
      }
      return;
    }
    if (command == "CHECK" || command == "BROKEN") {
      // CHECK        -> list everything wrong with the show
      // CHECK <n>    -> jump to the nth problem
      //
      // The validation TAKE already does, run across the whole show before
      // doors instead of one cue at a time during it.
      const std::vector<ShowProblem> problems = scanShowForProblems();
      if (problems.empty()) {
        remoteCommandDetail_ = "nothing broken";
        return;
      }
      if (parts.size() > 1) {
        int which = 0;
        try {
          which = std::stoi(parts[1]) - 1;
        } catch (...) {
          failRemoteCommand("CHECK: expected a problem number");
          return;
        }
        if (which < 0 || which >= static_cast<int>(problems.size())) {
          failRemoteCommand("CHECK: there are " +
                            std::to_string(problems.size()) + " problems");
          return;
        }
        const ShowProblem& p = problems[which];
        setFocusedDeckIndex(p.deckIndex);
        selectCueInDeck(p.deckIndex, p.cueIndex, false, false);
        scrollDeckToCueIndex(p.deckIndex, p.cueIndex, false);
        remoteCommandDetail_ = "deck " + std::to_string(p.deckIndex + 1) +
                               " cue " + std::to_string(p.cueIndex + 1) +
                               ": " + p.what;
        return;
      }
      std::ostringstream out;
      out << problems.size() << (problems.size() == 1 ? " problem" : " problems");
      int shown = 0;
      for (const auto& p : problems) {
        if (shown++ >= 12) {             // a socket reply, not a report
          out << " | ...";
          break;
        }
        out << " | " << (shown) << ") deck " << (p.deckIndex + 1)
            << " cue " << (p.cueIndex + 1) << ": " << p.what;
      }
      remoteCommandDetail_ = out.str();
      return;
    }
    if (command == "PREWAIT" || command == "POSTWAIT" || command == "CONTINUE") {
      // The sequencing spine, over the wire. These three fields have existed
      // in the show file since the spine landed and NOTHING could set them --
      // no inspector row, no verb. They persisted perfectly and did nothing.
      Cue* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand(command + ": select a cue first");
        return;
      }
      if (command == "CONTINUE") {
        if (parts.size() < 2) {
          remoteCommandDetail_ = std::string("continue: ") +
                                 cueContinueModeToken(cue->continueMode);
          return;
        }
        const std::string mode = toUpper(parts[1]);
        if (mode == "OFF" || mode == "NONE" || mode == "DONOTCONTINUE") {
          cue->continueMode = CueContinueMode::DoNotContinue;
        } else if (mode == "AUTO" || mode == "AUTOCONTINUE" || mode == "CONTINUE") {
          cue->continueMode = CueContinueMode::AutoContinue;
        } else if (mode == "FOLLOW" || mode == "AUTOFOLLOW") {
          cue->continueMode = CueContinueMode::AutoFollow;
        } else {
          failRemoteCommand("CONTINUE: use OFF, AUTO (from the start) or FOLLOW (from the end)");
          return;
        }
        markProjectDirty();
        remoteCommandDetail_ = std::string("continue: ") +
                               cueContinueModeToken(cue->continueMode);
        return;
      }
      double& field = (command == "PREWAIT") ? cue->preWaitSeconds : cue->postWaitSeconds;
      if (parts.size() < 2) {
        remoteCommandDetail_ = toLower(command) + ": " + formatSeconds(field);
        return;
      }
      try {
        // Clamped at zero rather than trusted: a negative pre-wait would ask
        // for a cue to start before its own GO.
        field = std::max(0.0, std::stod(parts[1]));
      } catch (...) {
        failRemoteCommand(command + ": expected seconds");
        return;
      }
      markProjectDirty();
      remoteCommandDetail_ = toLower(command) + ": " + formatSeconds(field);
      return;
    }
    if (command == "PENDING") {
      // What a deck is about to take, and in how long. Nothing could see this.
      std::ostringstream out;
      bool any = false;
      for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
        const double left = pendingTakeRemaining(d);
        if (left < 0.0) continue;
        any = true;
        out << (any && !out.str().empty() ? " | " : "")
            << "deck " << (d + 1) << " in " << formatSeconds(left);
      }
      remoteCommandDetail_ = any ? out.str() : "nothing pending";
      return;
    }
    if (command == "MATRIX") {
      // MATRIX                    -> report the selected cue's routing
      // MATRIX SET <src> <dest> <0-100>   src 1=L 2=R, dest is 1-based
      // MATRIX SEED               -> open it, routed exactly as it is now
      // MATRIX CLEAR              -> back to the stereo pair
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("MATRIX: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("MATRIX: select a cue first");
        return;
      }
      Cue& cue = deck.cues[deck.selectedIndex];
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();

      if (sub.empty()) {
        if (cue.audioMatrix.empty()) {
          remoteCommandDetail_ = "off - stereo pair on outs " +
            std::to_string(cue.audioOutputPair * 2 + 1) + "-" +
            std::to_string(cue.audioOutputPair * 2 + 2);
          return;
        }
        std::ostringstream out;
        out << cue.audioMatrix.size() << " point(s)";
        for (const AudioCrosspoint& p : cue.audioMatrix) {
          out << " | " << (p.source == 0 ? "L" : "R") << "->" << (p.dest + 1)
              << " " << static_cast<int>(std::lround(p.gain * 100.0f)) << "%";
        }
        remoteCommandDetail_ = out.str();
        return;
      }
      if (sub == "SEED" || sub == "ON") {
        seedCueMatrixFromPair();
        remoteCommandDetail_ = std::to_string(cue.audioMatrix.size()) + " point(s)";
        return;
      }
      if (sub == "CLEAR" || sub == "OFF") {
        clearCueMatrix();
        remoteCommandDetail_ = "off - back to the output pair";
        return;
      }
      if (sub == "SET" && parts.size() >= 5) {
        int src = 0;
        int dest = 0;
        double pct = 0.0;
        try {
          src = std::stoi(parts[2]);
          dest = std::stoi(parts[3]);
          pct = std::stod(parts[4]);
        } catch (...) {
          failRemoteCommand("MATRIX SET: expected <source 1-2> <dest> <0-100>");
          return;
        }
        // ONE-BASED ON THE WIRE, both of them, because that is how an operator
        // counts a channel and how every desk labels one. Refused rather than
        // clamped, the same rule MASTERVOL had to learn.
        if (src < 1 || src > 2) {
          failRemoteCommand("MATRIX SET: source is 1 (left) or 2 (right)");
          return;
        }
        if (dest < 1 || dest > 64) {
          failRemoteCommand("MATRIX SET: destination is 1-64");
          return;
        }
        if (pct < 0.0 || pct > 100.0) {
          failRemoteCommand("MATRIX SET: level is a percent from 0 to 100");
          return;
        }
        setCueMatrixGain(cue, src - 1, dest - 1, static_cast<float>(pct / 100.0));
        refreshLiveCueAudioMatrix();
        remoteCommandDetail_ = std::string(src == 1 ? "L" : "R") + "->" +
                               std::to_string(dest) + " " +
                               std::to_string(static_cast<int>(std::lround(pct))) + "%";
        return;
      }
      failRemoteCommand("MATRIX: expected SET <src> <dest> <0-100>, SEED or CLEAR");
      return;
    }
    if (command == "DECKREMOVE" || command == "DECKDEL") {
      // DECKREMOVE [<n>]  -- the focused playlist, or the one named.
      int victim = project_.focusedDeckIndex;
      if (parts.size() > 1) {
        auto parsed = parseDeckReferenceToken(parts[1]);
        if (!parsed) {
          failRemoteCommand("DECKREMOVE: no such playlist '" + parts[1] + "'");
          return;
        }
        victim = *parsed;
      }
      const std::string name = deckLabel(victim);
      if (!removeDeck(victim)) {
        return;   // removeDeck said why
      }
      remoteCommandDetail_ = "removed " + name + "; decks: " +
                             std::to_string(project_.decks.size());
      return;
    }
    if (command == "AUDIOALSO") {
      // AUDIOALSO                 -> where else this deck's sound goes
      // AUDIOALSO ADD <name>      -> send it there too
      // AUDIOALSO REMOVE <name>   -> stop
      // AUDIOALSO CLEAR           -> only the main device again
      //
      // The crosspoint MATRIX routes a cue across the channels of one device;
      // this is the other axis -- the same audio, to more devices at once.
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("AUDIOALSO: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();
      if (sub.empty()) {
        std::ostringstream out;
        out << "main: "
            << (deck.audioOutputDeviceName.empty() ? std::string("system default")
                                                   : deck.audioOutputDeviceName);
        if (deck.extraAudioDeviceNames.empty()) {
          out << " | and nowhere else";
        } else {
          for (const std::string& name : deck.extraAudioDeviceNames) {
            out << " | also: " << name;
            if (audioPlaybackDeviceIdForName(name) == 0) {
              out << " (NOT PRESENT)";
            }
          }
        }
        remoteCommandDetail_ = out.str();
        return;
      }
      if (sub == "CLEAR") {
        deck.extraAudioDeviceNames.clear();
        reopenDeckAudioOutput(deckIndex, deck.audioOutputDeviceName);
        markProjectDirty();
        remoteCommandDetail_ = "only the main device";
        return;
      }
      if ((sub == "ADD" || sub == "REMOVE") && parts.size() >= 3) {
        // The rest of the line, because device names have spaces in them.
        const std::string name = trim(joinParts(parts, 2));
        auto& list = deck.extraAudioDeviceNames;
        auto at = std::find(list.begin(), list.end(), name);
        if (sub == "ADD") {
          if (name == deck.audioOutputDeviceName) {
            failRemoteCommand("AUDIOALSO ADD: that is already this deck's main "
                              "device -- it would play twice");
            return;
          }
          if (at != list.end()) {
            failRemoteCommand("AUDIOALSO ADD: already sending to " + name);
            return;
          }
          if (list.size() >= 7) {
            failRemoteCommand("AUDIOALSO ADD: seven extra destinations is the limit");
            return;
          }
          list.push_back(name);
        } else {
          if (at == list.end()) {
            failRemoteCommand("AUDIOALSO REMOVE: not sending to " + name);
            return;
          }
          list.erase(at);
        }
        reopenDeckAudioOutput(deckIndex, deck.audioOutputDeviceName);
        markProjectDirty();
        // THE NAME IS KEPT EVEN WHEN THE DEVICE IS NOT THERE, which is the
        // same rule the primary device follows and for the same reason: a
        // rack powered on after the PC, a USB interface still enumerating.
        // The request belongs in the show; what opened belongs to this run.
        //
        // But it is SAID. Answering "1 extra destination" for a name that
        // reached nothing is the reply-contract fault this protocol keeps
        // having to relearn -- understood is not the same as done.
        std::string detail = std::to_string(list.size()) + " extra destination(s)";
        if (sub == "ADD" && audioPlaybackDeviceIdForName(name) == 0) {
          detail += "; " + name + " is NOT PRESENT right now - kept, and it "
                    "will be used when it appears";
        }
        remoteCommandDetail_ = detail;
        return;
      }
      failRemoteCommand("AUDIOALSO: expected ADD <name>, REMOVE <name> or CLEAR");
      return;
    }
    if (command == "MONITOR") {
      // MONITOR                      -> the device, the playlist, the room
      // MONITOR DEVICE [<name>]      -> listen here ("" turns the monitor off)
      // MONITOR DECK <n>|FOLLOW      -> which playlist you hear
      // MONITOR ROOM <n> ON|OFF      -> whether playlist n reaches its device
      //
      // Two separate things on purpose. Playing several videos at once put
      // every playlist onto the same default device with no way to hear one
      // by itself and no way to keep one out of the PA; the monitor answers
      // the first and ROOM answers the second.
      if (parts.size() == 1) {
        std::string detail = project_.monitorDeviceName.empty()
          ? std::string("no monitor device")
          : ("on " + project_.monitorDeviceName);
        // READ THE ENGINES, not the project. The project says what was asked
        // for; the engine gates are what the audio thread actually obeys, and
        // a report built from the intent would agree with itself even if
        // applyAudioMonitorSelection had never run. That is the difference
        // between a check that measures and one that restates its own input.
        std::string audible;
        std::string muted;
        for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
          const DeckRuntime* runtime = runtimeForDeck(d);
          if (!runtime || !runtime->mediaEngine) {
            continue;
          }
          if (!runtime->mediaEngine->monitorMuted()) {
            if (!audible.empty()) audible += ", ";
            audible += deckLabel(d);
          }
          if (runtime->mediaEngine->mainDeviceMuted()) {
            if (!muted.empty()) muted += ", ";
            muted += deckLabel(d);
          }
        }
        detail += "; hearing " + (audible.empty() ? std::string("nothing") : audible);
        detail += project_.monitorDeckIndex < 0 ? " (follows focus)" : " (pinned)";
        detail += "; out of the room: " + (muted.empty() ? std::string("none") : muted);
        remoteCommandDetail_ = detail;
        return;
      }
      const std::string sub = toUpper(parts[1]);
      if (sub == "DEVICE") {
        // An empty name is a real answer: it turns the monitor off.
        std::string wanted;
        for (std::size_t i = 2; i < parts.size(); ++i) {
          if (!wanted.empty()) wanted += " ";
          wanted += parts[i];
        }
        project_.monitorDeviceName = wanted;
        markProjectDirty();
        // The device really has to be opened, so this one DOES reopen -- it
        // is the only part of the monitor that touches hardware.
        for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
          reopenDeckAudioOutput(d, project_.decks[d].audioOutputDeviceName);
        }
        applyAudioMonitorSelection();
        remoteCommandDetail_ = wanted.empty() ? "monitor off" : ("monitor on " + wanted);
        return;
      }
      if (sub == "DECK" || sub == "PLAYLIST") {
        if (parts.size() < 3) {
          failRemoteCommand("MONITOR DECK: expected a playlist number or FOLLOW");
          return;
        }
        const std::string which = toUpper(parts[2]);
        if (which == "FOLLOW" || which == "FOCUS" || which == "AUTO") {
          project_.monitorDeckIndex = -1;
        } else {
          auto parsed = parseDeckReferenceToken(parts[2]);
          if (!parsed) {
            failRemoteCommand("MONITOR DECK: no such playlist '" + parts[2] + "'");
            return;
          }
          project_.monitorDeckIndex = *parsed;
        }
        markProjectDirty();
        applyAudioMonitorSelection();
        remoteCommandDetail_ = "hearing " + deckLabel(monitoredDeckIndex()) +
                               (project_.monitorDeckIndex < 0 ? " (follows focus)" : "");
        return;
      }
      if (sub == "ROOM" || sub == "PROGRAM" || sub == "PROGRAMME") {
        if (parts.size() < 4) {
          failRemoteCommand("MONITOR ROOM: expected a playlist and ON or OFF");
          return;
        }
        auto parsed = parseDeckReferenceToken(parts[2]);
        if (!parsed) {
          failRemoteCommand("MONITOR ROOM: no such playlist '" + parts[2] + "'");
          return;
        }
        const std::string state = toUpper(parts[3]);
        if (state != "ON" && state != "OFF") {
          failRemoteCommand("MONITOR ROOM: expected ON or OFF");
          return;
        }
        project_.decks[*parsed].audioToProgram = (state == "ON");
        markProjectDirty();
        applyAudioMonitorSelection();
        remoteCommandDetail_ = deckLabel(*parsed) +
          (state == "ON" ? " reaches the room" : " is out of the room");
        return;
      }
      failRemoteCommand("MONITOR: expected DEVICE, DECK or ROOM");
      return;
    }
    // NO XFADE ALIAS. XFADE has meant TRANSITION -- the cue transition
    // TIME -- for as long as that verb has existed, and this branch
    // sits earlier in the dispatcher, so claiming it would have taken
    // the name from every script already using it without a word.
    // audit_remote_help caught it; it is the second time this release
    // that a new verb reached for a name that was already spoken for.
    if (command == "CROSSFADE") {
      // CROSSFADE                      -> what the focused output is doing
      // CROSSFADE OFF                  -> no fade; the stack composites plain
      // CROSSFADE <0-100>              -> the position, arming it if needed
      // CROSSFADE <from> <to> [<0-100>]-> which two stack entries, 1 = base
      //
      // Stack positions are spoken 1-based, the way the routing menu and the
      // layer verb number them, and stored 0-based.
      //
      // THIS IS WHAT VJ MODE BECAME. It was a global that claimed the
      // programme output and knew about exactly two decks; an output can now
      // fade between any two entries of its own stack, and the blend comes
      // from the layer rather than from a second setting that could disagree
      // with it.
      if (project_.outputs.empty()) {
        failRemoteCommand("CROSSFADE: there are no outputs");
        return;
      }
      OutputTarget& out = focusedOutputMutable();
      const int stackSize = static_cast<int>(out.layerDecks.size()) + 1;
      if (parts.size() <= 1) {
        if (!out.crossfadeEnabled) {
          remoteCommandDetail_ = "off";
          return;
        }
        remoteCommandDetail_ =
          std::to_string(out.crossfadeFrom + 1) + " -> " +
          std::to_string(out.crossfadeTo + 1) + " at " +
          std::to_string(static_cast<int>(std::lround(out.crossfadeMix * 100.0))) + "%";
        return;
      }
      const std::string first = toUpper(parts[1]);
      if (first == "OFF" || first == "NONE") {
        out.crossfadeEnabled = false;
        markProjectDirty();
        remoteCommandDetail_ = "off";
        return;
      }
      // A CROSSFADER NEEDS SOMETHING TO FADE BETWEEN. An output with only a
      // base has one picture, and a fader on it would be a control that
      // cannot do anything.
      if (stackSize < 2) {
        failRemoteCommand("CROSSFADE: " + outputLabel(project_.focusedOutputIndex) +
                          " has only a base; assign a playlist as a layer first");
        return;
      }
      auto position = [&](std::size_t index) -> std::optional<double> {
        return parseNumber(static_cast<int>(index));
      };
      if (parts.size() == 2) {
        auto value = position(1);
        if (!value) {
          failRemoteCommand("CROSSFADE: expected OFF, a percent, or two stack "
                            "positions");
          return;
        }
        out.crossfadeMix = std::clamp(*value / 100.0, 0.0, 1.0);
        out.crossfadeEnabled = true;
        markProjectDirty();
        remoteCommandDetail_ =
          std::to_string(static_cast<int>(std::lround(out.crossfadeMix * 100.0))) + "%";
        return;
      }
      auto fromValue = position(1);
      auto toValue = position(2);
      if (!fromValue || !toValue) {
        failRemoteCommand("CROSSFADE: expected two stack positions (1 is the base)");
        return;
      }
      const int from = static_cast<int>(std::lround(*fromValue)) - 1;
      const int to = static_cast<int>(std::lround(*toValue)) - 1;
      if (from < 0 || from >= stackSize || to < 0 || to >= stackSize) {
        failRemoteCommand("CROSSFADE: this output has " + std::to_string(stackSize) +
                          " stack positions");
        return;
      }
      if (from == to) {
        failRemoteCommand("CROSSFADE: a fader needs two different positions");
        return;
      }
      out.crossfadeFrom = from;
      out.crossfadeTo = to;
      out.crossfadeEnabled = true;
      if (parts.size() > 3) {
        if (auto mix = position(3)) {
          out.crossfadeMix = std::clamp(*mix / 100.0, 0.0, 1.0);
        }
      }
      markProjectDirty();
      remoteCommandDetail_ =
        std::to_string(from + 1) + " -> " + std::to_string(to + 1) + " at " +
        std::to_string(static_cast<int>(std::lround(out.crossfadeMix * 100.0))) + "%";
      return;
    }
    if (command == "MULTIVIEW" || command == "MULTI") {
      // MULTIVIEW [ON|OFF|TOGGLE] -- the programme and every playlist in a
      // grid where the single monitor usually is.
      const std::string arg = parts.size() > 1 ? toUpper(parts[1]) : std::string("TOGGLE");
      // -- THE WINDOWS ------------------------------------------------
      //
      //   MULTIVIEW WINDOW                      what every window shows
      //   MULTIVIEW WINDOW <n> <source>         programme | deck:<i> | empty
      //   MULTIVIEW WINDOW <n> SAFE|METER|LABEL ON|OFF
      //   MULTIVIEW WINDOW ADD | MULTIVIEW WINDOW REMOVE <n>
      //   MULTIVIEW WINDOW RESET                back to one per playlist
      //
      // Windows are numbered from 1 the way the operator sees them.
      if (arg == "WINDOW" || arg == "TILE") {
        if (parts.size() == 2) {
          const auto plan = multiviewTilePlan();
          std::string detail;
          for (std::size_t t = 0; t < plan.size(); ++t) {
            if (!detail.empty()) detail += "; ";
            detail += std::to_string(t + 1) + "=" +
                      (plan[t].source.empty() ? std::string("empty") : plan[t].source);
            if (plan[t].safeAreas) detail += "+safe";
            if (plan[t].vuMeter)   detail += "+meter";
            if (!plan[t].label)    detail += "-label";
          }
          remoteCommandDetail_ = detail.empty() ? "no windows" : detail;
          return;
        }
        const std::string sub = toUpper(parts[2]);
        if (project_.multiviewTiles.empty()) {
          project_.multiviewTiles = multiviewTilePlan();
        }
        if (sub == "RESET") {
          project_.multiviewTiles.clear();
          markProjectDirty();
          remoteCommandDetail_ = "back to one window per playlist";
          return;
        }
        if (sub == "ADD") {
          if (static_cast<int>(project_.multiviewTiles.size()) >= kMaxMultiviewTiles) {
            failRemoteCommand("MULTIVIEW WINDOW: already at the limit of " +
                              std::to_string(kMaxMultiviewTiles));
            return;
          }
          MultiviewTile fresh;
          fresh.source = "";
          project_.multiviewTiles.push_back(fresh);
          markProjectDirty();
          remoteCommandDetail_ = "windows: " +
                                 std::to_string(project_.multiviewTiles.size());
          return;
        }
        // Everything below names a window by number.
        const int which = std::atoi(parts[2].c_str()) - 1;
        if (sub == "REMOVE" || sub == "DEL") {
          const int victim = (parts.size() > 3) ? std::atoi(parts[3].c_str()) - 1 : -1;
          if (victim < 0 ||
              victim >= static_cast<int>(project_.multiviewTiles.size())) {
            failRemoteCommand("MULTIVIEW WINDOW REMOVE: expected a window number");
            return;
          }
          if (project_.multiviewTiles.size() <= 1) {
            failRemoteCommand("MULTIVIEW WINDOW: the last window stays");
            return;
          }
          project_.multiviewTiles.erase(project_.multiviewTiles.begin() + victim);
          markProjectDirty();
          remoteCommandDetail_ = "windows: " +
                                 std::to_string(project_.multiviewTiles.size());
          return;
        }
        if (which < 0 ||
            which >= static_cast<int>(project_.multiviewTiles.size())) {
          failRemoteCommand("MULTIVIEW WINDOW: there is no window " + parts[2]);
          return;
        }
        MultiviewTile& tile = project_.multiviewTiles[which];
        if (parts.size() < 4) {
          failRemoteCommand("MULTIVIEW WINDOW <n>: expected a source, or "
                            "SAFE/METER/LABEL ON|OFF");
          return;
        }
        const std::string what = toUpper(parts[3]);
        if (what == "SAFE" || what == "METER" || what == "LABEL") {
          if (parts.size() < 5) {
            failRemoteCommand("MULTIVIEW WINDOW " + parts[2] + " " + parts[3] +
                              ": expected ON or OFF");
            return;
          }
          const std::string state = toUpper(parts[4]);
          if (state != "ON" && state != "OFF") {
            failRemoteCommand("MULTIVIEW WINDOW: expected ON or OFF");
            return;
          }
          const bool on = state == "ON";
          if (what == "SAFE")       tile.safeAreas = on;
          else if (what == "METER") tile.vuMeter = on;
          else                      tile.label = on;
          markProjectDirty();
          remoteCommandDetail_ = "window " + parts[2] + " " + parts[3] + " " + state;
          return;
        }
        // A SOURCE. Validated rather than stored blind: a window pointed at a
        // playlist that does not exist draws "missing", which is honest but is
        // not something a caller should be able to ask for by accident.
        std::string source = parts[3];
        const std::string upper = toUpper(source);
        if (upper == "EMPTY" || upper == "NONE" || upper == "OFF") {
          source = "";
        } else if (upper == "PROGRAMME" || upper == "PROGRAM" || upper == "PGM") {
          source = "programme";
        } else if (upper.rfind("DECK", 0) == 0) {
          const char* digits = source.c_str() + 4;
          while (*digits == ':' || *digits == ' ') ++digits;
          const int d = std::atoi(digits) - 1;   // the operator counts from 1
          if (d < 0 || d >= static_cast<int>(project_.decks.size())) {
            failRemoteCommand("MULTIVIEW WINDOW: there is no playlist " +
                              std::string(digits));
            return;
          }
          source = "deck:" + std::to_string(d);
        } else {
          failRemoteCommand("MULTIVIEW WINDOW: expected PROGRAMME, DECK<n> or EMPTY");
          return;
        }
        tile.source = source;
        markProjectDirty();
        remoteCommandDetail_ = "window " + parts[2] + " shows " +
                               (source.empty() ? std::string("nothing") : source);
        return;
      }
      if (arg == "ON")        project_.multiviewMode = 1;
      else if (arg == "OFF")  project_.multiviewMode = 0;
      else if (arg == "TOGGLE") project_.multiviewMode = project_.multiviewMode ? 0 : 1;
      else {
        failRemoteCommand("MULTIVIEW: expected ON, OFF or TOGGLE");
        return;
      }
      if (project_.multiviewMode == 0) {
        clearDeckPreviewTextures();
      }
      markProjectDirty();
      remoteCommandDetail_ = project_.multiviewMode ? "on" : "off";
      return;
    }
    if (command == "MIDIFILE") {
      // MIDIFILE            -> report the selected midi file cue
      // MIDIFILE PORT [name]-> which output it plays to ("" = first found)
      //
      // There is no NEW: a midi file cue comes from a FILE, so it is made by
      // importing one. A verb that made an empty one would make a cue that
      // can never play anything.
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("MIDIFILE: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("MIDIFILE: select a cue first");
        return;
      }
      Cue& cue = deck.cues[deck.selectedIndex];
      if (cue.kind != CueKind::MidiFile) {
        failRemoteCommand("MIDIFILE: the selected cue is not a midi file cue");
        return;
      }
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();
      if (sub == "PORT") {
        cue.midiPortName = parts.size() >= 3 ? trim(joinParts(parts, 2)) : std::string();
        markProjectDirty();
        remoteCommandDetail_ = cue.midiPortName.empty() ? "first available"
                                                        : cue.midiPortName;
        return;
      }
      if (sub.empty()) {
        // READ FROM THE PARSE, not from the cue record: the answer to "what
        // will this send" has to come from the file on disk, or a file that
        // has been replaced since import reports what it used to be.
        const auto* parsed =
          loadedMidiFile(resolvedCueFilesystemPathString(cue, currentProjectFile_));
        std::ostringstream out;
        if (!parsed || !parsed->ok) {
          out << "UNREADABLE: "
              << (parsed && !parsed->error.empty() ? parsed->error : "no such file");
        } else {
          out << "format " << parsed->format << " | " << parsed->trackCount
              << " track(s) | " << parsed->events.size() << " event(s) | "
              << formatSeconds(parsed->durationSeconds) << " | -> "
              << (cue.midiPortName.empty() ? std::string("first available")
                                           : cue.midiPortName);
        }
        remoteCommandDetail_ = out.str();
        return;
      }
      failRemoteCommand("MIDIFILE: expected PORT, or no argument to report it");
      return;
    }
    if (command == "TEXTCUE") {
      // TEXTCUE NEW                 -> add a text cue to this deck
      // TEXTCUE BODY <words...>     -> what it says (\n makes a new line)
      // TEXTCUE ANIM none|fade|typewriter|scroll|crawl|pulse
      // TEXTCUE SIZE <1-100>        -> percent of the raster height
      // TEXTCUE SPEED <0.05-20>
      // TEXTCUE ALIGN left|centre|right
      // TEXTCUE CARD <0-255>        -> black behind the words
      // TEXTCUE                     -> report it
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("TEXTCUE: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();

      if (sub == "NEW") {
        addTextCue();
        remoteCommandDetail_ = "text cue " + std::to_string(deck.cues.size());
        return;
      }
      // A lower third already laid out, and OUT for whichever is on air --
      // neither needs a selection, which is what makes them work from a
      // Companion button in the middle of a show.
      if (sub == "LOWERNEW" || sub == "L3NEW") {
        addLowerThirdTextCue();
        remoteCommandDetail_ = "lower third " + std::to_string(deck.cues.size());
        return;
      }
      if (sub == "OUT") {
        const int sent = lowerThirdTakeOutAnywhere();
        if (sent == 0) {
          failRemoteCommand("TEXTCUE OUT: no lower third on air");
          return;
        }
        remoteCommandDetail_ = std::to_string(sent) + " leaving";
        return;
      }

      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("TEXTCUE: select a cue first");
        return;
      }
      Cue& cue = deck.cues[deck.selectedIndex];
      if (cue.kind != CueKind::Text) {
        failRemoteCommand("TEXTCUE: the selected cue is not a text cue");
        return;
      }

      if (sub.empty()) {
        std::string first = cue.textBody;
        const std::size_t nl = first.find('\n');
        if (nl != std::string::npos) {
          first = first.substr(0, nl) + " ...";
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s | %s | %.0f%% | %.2fx | %s | card %d",
                      first.empty() ? "(empty)" : first.c_str(),
                      cueTextAnimationLabel(cue.textAnimation),
                      cue.textSizePct, cue.textSpeed,
                      cue.textAlign == 0 ? "left" : (cue.textAlign == 2 ? "right" : "centre"),
                      cue.textBgAlpha);
        remoteCommandDetail_ = buf;
        return;
      }
      if (sub == "BODY" || sub == "TEXT") {
        // The rest of the line verbatim, with the same tiny escape set the
        // network cue uses -- a title card wants a second line more often than
        // it wants a backslash.
        cue.textBody = parts.size() >= 3
          ? expandNetworkEscapes(joinParts(parts, 2)) : std::string();
        markProjectDirty();
        remoteCommandDetail_ = cue.textBody.empty() ? "cleared" : cue.textBody;
        return;
      }
      if (sub == "ANIM" && parts.size() >= 3) {
        const std::string token = toLower(parts[2]);
        static const char* kKnown[] = {"none", "fade", "typewriter",
                                       "scroll", "crawl", "pulse", "wobble"};
        bool known = false;
        for (const char* k : kKnown) {
          known = known || token == k;
        }
        if (!known) {
          failRemoteCommand("TEXTCUE ANIM: expected none, fade, typewriter, "
                            "scroll, crawl, pulse or wobble");
          return;
        }
        cue.textAnimation = cueTextAnimationFromToken(token);
        markProjectDirty();
        remoteCommandDetail_ = cueTextAnimationLabel(cue.textAnimation);
        return;
      }
      if (sub == "ALIGN" && parts.size() >= 3) {
        const std::string token = toLower(parts[2]);
        if (token == "left")        cue.textAlign = 0;
        else if (token == "centre" || token == "center") cue.textAlign = 1;
        else if (token == "right")  cue.textAlign = 2;
        else {
          failRemoteCommand("TEXTCUE ALIGN: expected left, centre or right");
          return;
        }
        markProjectDirty();
        remoteCommandDetail_ = token;
        return;
      }
      if ((sub == "SIZE" || sub == "SPEED" || sub == "CARD") && parts.size() >= 3) {
        auto parsed = parseNumber(2);
        if (!parsed) {
          failRemoteCommand("TEXTCUE " + sub + ": expected a number");
          return;
        }
        // Refused rather than clamped, the rule the whole protocol follows.
        if (sub == "SIZE") {
          if (*parsed < 1.0 || *parsed > 100.0) {
            failRemoteCommand("TEXTCUE SIZE: expected 1-100 (percent of the raster)");
            return;
          }
          cue.textSizePct = *parsed;
        } else if (sub == "SPEED") {
          if (*parsed < 0.05 || *parsed > 20.0) {
            failRemoteCommand("TEXTCUE SPEED: expected 0.05-20");
            return;
          }
          cue.textSpeed = *parsed;
        } else {
          if (*parsed < 0.0 || *parsed > 255.0) {
            failRemoteCommand("TEXTCUE CARD: expected 0-255");
            return;
          }
          cue.textBgAlpha = static_cast<int>(std::lround(*parsed));
        }
        markProjectDirty();
        remoteCommandDetail_ = parts[2];
        return;
      }
      // THE LOWER-THIRD LAYOUT.
      if (sub == "LOWER" || sub == "L3") {
        const std::string v = parts.size() > 2 ? toUpper(parts[2]) : std::string("ON");
        cue.lowerThird.on = v == "ON" || v == "1" || v == "TRUE";
        if (cue.lowerThird.on && cue.textBody.empty()) {
          cue.textBody = "Name Surname\nTitle or role";
        }
        markProjectDirty();
        remoteCommandDetail_ = cue.lowerThird.on ? "lower third" : "card";
        return;
      }
      if (sub == "TITLE" || sub == "SUB") {
        setTextBodyLine(cue.textBody, sub == "TITLE" ? 0 : 1,
                        parts.size() >= 3 ? joinParts(parts, 2) : std::string());
        markProjectDirty();
        remoteCommandDetail_ = textBodyLine(cue.textBody, sub == "TITLE" ? 0 : 1);
        return;
      }
      if (sub == "LOOK" && parts.size() >= 3) {
        const std::string want = toLower(parts[2]);
        for (int i = 0; i < static_cast<int>(LowerThirdLook::Count); ++i) {
          if (want == lowerThirdLookLabel(static_cast<LowerThirdLook>(i))) {
            cue.lowerThird.look = static_cast<LowerThirdLook>(i);
            markProjectDirty();
            remoteCommandDetail_ = want;
            return;
          }
        }
        failRemoteCommand("TEXTCUE LOOK: bar, boxes, line, tag or glass");
        return;
      }
      if ((sub == "IN" || sub == "OUTMOVE") && parts.size() >= 3) {
        const std::string want = toLower(parts[2]);
        const LowerThirdMove move = lowerThirdMoveFromToken(want);
        if (move == LowerThirdMove::None && want != "cut") {
          failRemoteCommand("TEXTCUE " + sub + ": cut, fade, left, right, rise, "
                            "wipe, grow, typewriter or pop");
          return;
        }
        (sub == "IN" ? cue.lowerThird.moveIn : cue.lowerThird.moveOut) = move;
        markProjectDirty();
        remoteCommandDetail_ = lowerThirdMoveLabel(move);
        return;
      }
      failRemoteCommand("TEXTCUE: expected NEW, BODY, ANIM, SIZE, SPEED, ALIGN, "
                        "CARD, LOWERNEW, LOWER, TITLE, SUB, LOOK, IN, OUTMOVE or OUT");
      return;
    }
    if (command == "DMXCUE") {
      // DMXCUE NEW                 -> add a DMX cue to this deck
      // DMXCUE SET <spec>          -> "1=255, 10-14=64"
      // DMXCUE FADE <seconds> | UNIVERSE <n> | HOST <ipv4> | PORT <n>
      // DMXCUE FIRE                -> send it now
      // DMXCUE BLACKOUT            -> every channel on every universe to 0
      // DMXCUE                     -> report it, and whether it can send
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("DMXCUE: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();

      if (sub == "BLACKOUT") {
        remoteCommandDetail_ = dmxBlackout();
        return;
      }
      if (sub == "NEW") {
        Cue cue;
        cue.kind = CueKind::Dmx;
        cue.name = "DMX " + std::to_string(deck.cues.size() + 1);
        deck.cues.push_back(cue);
        deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
        onSelectionChanged();
        markProjectDirty();
        remoteCommandDetail_ = "dmx cue " + std::to_string(deck.cues.size());
        return;
      }

      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("DMXCUE: select a cue first");
        return;
      }
      Cue& cue = deck.cues[deck.selectedIndex];
      if (cue.kind != CueKind::Dmx) {
        failRemoteCommand("DMXCUE: the selected cue is not a dmx cue");
        return;
      }

      if (sub.empty()) {
        auto spec = deckboy::platform::parseDmxChannelSpec(cue.dmxChannels);
        std::ostringstream out;
        out << (cue.dmxChannels.empty() ? std::string("(no channels)") : cue.dmxChannels)
            << " -> universe " << cue.dmxUniverse
            << " " << cue.dmxHost << ":" << cue.dmxPort
            << (cue.dmxFadeSeconds > 0.0
                  ? (" over " + formatSeconds(cue.dmxFadeSeconds))
                  : std::string(" now"))
            << " | " << (spec && !spec->empty()
                           ? (std::to_string(spec->size()) + " channel(s)")
                           : std::string("NOTHING TO SEND"));
        remoteCommandDetail_ = out.str();
        return;
      }
      if (sub == "FIRE" || sub == "GO" || sub == "SEND") {
        remoteCommandDetail_ = fireDmxCue(deckIndex, deck.selectedIndex);
        return;
      }
      if (sub == "SET" || sub == "CHANNELS") {
        const std::string text = parts.size() >= 3 ? trim(joinParts(parts, 2))
                                                   : std::string();
        // CHECKED WHEN TYPED, not at GO. A typo here is silent on the night.
        if (!text.empty() && !deckboy::platform::parseDmxChannelSpec(text)) {
          failRemoteCommand("DMXCUE SET: expected channel=level pairs, e.g. "
                            "1=255,10-14=64");
          return;
        }
        cue.dmxChannels = text;
        markProjectDirty();
        remoteCommandDetail_ = text.empty() ? "cleared" : text;
        return;
      }
      if (sub == "HOST" && parts.size() >= 3) {
        const std::string host = trim(parts[2]);
        sockaddr_in probe {};
        if (inet_pton(AF_INET, host.c_str(), &probe.sin_addr) != 1) {
          failRemoteCommand("DMXCUE HOST: " + host + " is not an IPv4 address");
          return;
        }
        cue.dmxHost = host;
        markProjectDirty();
        remoteCommandDetail_ = host;
        return;
      }
      if ((sub == "FADE" || sub == "UNIVERSE" || sub == "PORT") && parts.size() >= 3) {
        auto parsed = parseNumber(2);
        if (!parsed || *parsed < 0.0) {
          failRemoteCommand("DMXCUE " + sub + ": expected a number");
          return;
        }
        if (sub == "FADE") {
          cue.dmxFadeSeconds = *parsed;
          remoteCommandDetail_ = formatSeconds(cue.dmxFadeSeconds);
        } else if (sub == "UNIVERSE") {
          if (*parsed > 32767.0) {
            failRemoteCommand("DMXCUE UNIVERSE: expected 0-32767");
            return;
          }
          cue.dmxUniverse = static_cast<int>(std::lround(*parsed));
          remoteCommandDetail_ = std::to_string(cue.dmxUniverse);
        } else {
          if (*parsed < 1.0 || *parsed > 65535.0) {
            failRemoteCommand("DMXCUE PORT: expected 1-65535");
            return;
          }
          cue.dmxPort = static_cast<int>(std::lround(*parsed));
          remoteCommandDetail_ = std::to_string(cue.dmxPort);
        }
        markProjectDirty();
        return;
      }
      failRemoteCommand("DMXCUE: expected NEW, SET, FADE, UNIVERSE, HOST, PORT, "
                        "FIRE or BLACKOUT");
      return;
    }
    if (command == "SCRIPTCUE") {
      // SCRIPTCUE NEW           -> add a script cue to this deck
      // SCRIPTCUE ADD <line>    -> append a line
      // SCRIPTCUE CLEAR         -> empty it
      // SCRIPTCUE RUN           -> run it now
      // SCRIPTCUE               -> report how many lines, and the first one
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("SCRIPTCUE: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();

      if (sub == "NEW") {
        Cue cue;
        cue.kind = CueKind::Script;
        cue.name = "Script " + std::to_string(deck.cues.size() + 1);
        deck.cues.push_back(cue);
        deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
        onSelectionChanged();
        markProjectDirty();
        remoteCommandDetail_ = "script cue " + std::to_string(deck.cues.size());
        return;
      }

      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("SCRIPTCUE: select a cue first");
        return;
      }
      Cue& cue = deck.cues[deck.selectedIndex];
      if (cue.kind != CueKind::Script) {
        failRemoteCommand("SCRIPTCUE: the selected cue is not a script cue");
        return;
      }

      if (sub.empty()) {
        const int count = scriptLineCount(cue.scriptText);
        std::string first;
        std::istringstream lines(cue.scriptText);
        std::string line;
        while (std::getline(lines, line)) {
          const std::string trimmed = trim(line);
          if (!trimmed.empty() && trimmed.front() != '#') {
            first = trimmed;
            break;
          }
        }
        remoteCommandDetail_ = std::to_string(count) + " line" +
                               (count == 1 ? "" : "s") +
                               (first.empty() ? std::string() : (" | " + first));
        return;
      }
      if (sub == "RUN" || sub == "FIRE" || sub == "GO") {
        remoteCommandDetail_ = runScriptCue(deckIndex, deck.selectedIndex);
        return;
      }
      if (sub == "CLEAR") {
        cue.scriptText.clear();
        markProjectDirty();
        remoteCommandDetail_ = "cleared";
        return;
      }
      if (sub == "ADD" && parts.size() >= 3) {
        // The rest of the line verbatim: a protocol line has spaces in it and
        // is not this verb's business to interpret.
        const std::string added = joinParts(parts, 2);
        if (!cue.scriptText.empty() && cue.scriptText.back() != '\n') {
          cue.scriptText.push_back('\n');
        }
        cue.scriptText += added;
        markProjectDirty();
        remoteCommandDetail_ = std::to_string(scriptLineCount(cue.scriptText)) +
                               " line(s)";
        return;
      }
      failRemoteCommand("SCRIPTCUE: expected NEW, ADD, CLEAR or RUN");
      return;
    }
    if (command == "TCCUE" || command == "TIMECODECUE") {
      // TCCUE NEW              -> add a timecode cue to this deck
      // TCCUE ACTION start|stop|jam
      // TCCUE JAM <hh:mm:ss:ff|seconds>
      // TCCUE FIRE             -> do it now
      // TCCUE                  -> report it, and whether the generator is up
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("TCCUE: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();

      if (sub == "NEW") {
        Cue cue;
        cue.kind = CueKind::Timecode;
        cue.name = "Timecode " + std::to_string(deck.cues.size() + 1);
        deck.cues.push_back(cue);
        deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
        onSelectionChanged();
        markProjectDirty();
        remoteCommandDetail_ = "timecode cue " + std::to_string(deck.cues.size());
        return;
      }

      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("TCCUE: select a cue first");
        return;
      }
      Cue& cue = deck.cues[deck.selectedIndex];
      if (cue.kind != CueKind::Timecode) {
        failRemoteCommand("TCCUE: the selected cue is not a timecode cue");
        return;
      }

      if (sub.empty()) {
        std::ostringstream out;
        out << toUpper(cue.tcAction);
        if (toLower(trim(cue.tcAction)) == "jam") {
          out << " " << formatTimecode(cue.tcJamSeconds, deck.playlistTimebaseFps);
        }
        out << " | generator " << (project_.ltcOutputEnabled ? "on" : "off");
        remoteCommandDetail_ = out.str();
        return;
      }
      if (sub == "FIRE" || sub == "GO" || sub == "TAKE") {
        remoteCommandDetail_ = fireTimecodeCue(deckIndex, deck.selectedIndex);
        return;
      }
      if (sub == "ACTION" && parts.size() >= 3) {
        const std::string token = toLower(parts[2]);
        if (token != "start" && token != "stop" && token != "jam") {
          failRemoteCommand("TCCUE ACTION: expected start, stop or jam");
          return;
        }
        cue.tcAction = token;
        markProjectDirty();
        remoteCommandDetail_ = toUpper(token);
        return;
      }
      if (sub == "JAM" && parts.size() >= 3) {
        const std::string text = trim(parts[2]);
        double seconds = 0.0;
        if (text.find(':') != std::string::npos) {
          auto parsed = parseTimecodeSeconds(text, deck.playlistTimebaseFps);
          if (!parsed) {
            failRemoteCommand("TCCUE JAM: " + text + " is not a timecode");
            return;
          }
          seconds = *parsed;
        } else {
          auto parsed = parseNumber(2);
          if (!parsed || *parsed < 0.0) {
            failRemoteCommand("TCCUE JAM: expected a timecode or seconds");
            return;
          }
          seconds = *parsed;
        }
        cue.tcJamSeconds = seconds;
        markProjectDirty();
        remoteCommandDetail_ = formatTimecode(seconds, deck.playlistTimebaseFps);
        return;
      }
      failRemoteCommand("TCCUE: expected NEW, ACTION, JAM or FIRE");
      return;
    }
    if (command == "NETCUE") {
      // NETCUE NEW                 -> add a network cue to this deck
      // NETCUE PROTO osc|udp|tcp
      // NETCUE HOST <ipv4> | PORT <1-65535>
      // NETCUE ADDRESS </osc/path> | PAYLOAD <text...>
      // NETCUE SEND                -> send it now
      // NETCUE                     -> report where it sends and what
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("NETCUE: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();

      if (sub == "NEW") {
        Cue cue;
        cue.kind = CueKind::Network;
        cue.name = "Network " + std::to_string(deck.cues.size() + 1);
        deck.cues.push_back(cue);
        deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
        onSelectionChanged();
        markProjectDirty();
        remoteCommandDetail_ = "network cue " + std::to_string(deck.cues.size());
        return;
      }

      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("NETCUE: select a cue first");
        return;
      }
      Cue& cue = deck.cues[deck.selectedIndex];
      if (cue.kind != CueKind::Network) {
        failRemoteCommand("NETCUE: the selected cue is not a network cue");
        return;
      }

      if (sub.empty()) {
        std::ostringstream out;
        out << toUpper(cue.netProtocol) << " -> " << cue.netHost << ":" << cue.netPort;
        if (toLower(trim(cue.netProtocol)) == "osc") {
          out << " " << cue.netAddress;
        }
        out << " | " << (cue.netPayload.empty() ? std::string("(empty)") : cue.netPayload);
        remoteCommandDetail_ = out.str();
        return;
      }
      if (sub == "SEND" || sub == "FIRE" || sub == "GO") {
        remoteCommandDetail_ = fireNetworkCue(deckIndex, deck.selectedIndex);
        return;
      }
      if (sub == "PROTO" || sub == "PROTOCOL") {
        if (parts.size() < 3) {
          failRemoteCommand("NETCUE PROTO: expected osc, udp or tcp");
          return;
        }
        const std::string token = toLower(parts[2]);
        // Checked, not trusted: an unknown protocol would otherwise fall
        // through to "send nothing" and look like a network fault.
        if (token != "osc" && token != "udp" && token != "tcp") {
          failRemoteCommand("NETCUE PROTO: expected osc, udp or tcp");
          return;
        }
        cue.netProtocol = token;
        markProjectDirty();
        remoteCommandDetail_ = toUpper(token);
        return;
      }
      if (sub == "HOST" && parts.size() >= 3) {
        const std::string host = trim(parts[2]);
        sockaddr_in probe {};
        // REFUSED HERE, not at GO. A name that needs DNS is not usable from a
        // cue at all, so saying so when it is typed is the only useful moment.
        if (inet_pton(AF_INET, host.c_str(), &probe.sin_addr) != 1) {
          failRemoteCommand("NETCUE HOST: " + host + " is not an IPv4 address");
          return;
        }
        cue.netHost = host;
        markProjectDirty();
        remoteCommandDetail_ = host;
        return;
      }
      if (sub == "PORT" && parts.size() >= 3) {
        auto parsed = parseNumber(2);
        if (!parsed || *parsed < 1.0 || *parsed > 65535.0) {
          failRemoteCommand("NETCUE PORT: expected 1-65535");
          return;
        }
        cue.netPort = static_cast<int>(std::lround(*parsed));
        markProjectDirty();
        remoteCommandDetail_ = std::to_string(cue.netPort);
        return;
      }
      if (sub == "ADDRESS" || sub == "ADDR" || sub == "PATH") {
        if (parts.size() < 3) {
          failRemoteCommand("NETCUE ADDRESS: expected an OSC path starting with /");
          return;
        }
        const std::string address = trim(parts[2]);
        if (address.empty() || address.front() != '/') {
          failRemoteCommand("NETCUE ADDRESS: an OSC address must start with /");
          return;
        }
        cue.netAddress = address;
        markProjectDirty();
        remoteCommandDetail_ = address;
        return;
      }
      if (sub == "PAYLOAD" || sub == "ARG" || sub == "TEXT") {
        // The rest of the line, untrimmed on the right: a trailing space or
        // newline is often the point of a line-based protocol.
        cue.netPayload = parts.size() >= 3 ? joinParts(parts, 2) : std::string();
        markProjectDirty();
        remoteCommandDetail_ = cue.netPayload.empty() ? "cleared" : cue.netPayload;
        return;
      }
      failRemoteCommand("NETCUE: expected NEW, PROTO, HOST, PORT, ADDRESS, "
                        "PAYLOAD or SEND");
      return;
    }
    if (command == "MIDICUE") {
      // MIDICUE NEW              -> add a MIDI cue to this deck
      // MIDICUE PORTS            -> what this machine can send to
      // MIDICUE PORT <name>      -> which one this cue uses ("" = first found)
      // MIDICUE KIND <token>     -> note-on|note-off|cc|program|msc-go|
      //                             msc-stop|msc-resume|raw
      // MIDICUE CH <1-16> | D1 <0-127> | D2 <0-127>
      // MIDICUE DEVICE <0-127> | CUENUM <n> | LIST <n> | RAW <hex>
      // MIDICUE SEND             -> send it now
      // MIDICUE                  -> report it, and the bytes it would send
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("MIDICUE: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();

      if (sub == "PORTS") {
        const auto ports = deckboy::platform::midi::MidiOutput::listDevices();
        if (ports.empty()) {
          remoteCommandDetail_ = "no MIDI output ports";
          return;
        }
        std::ostringstream out;
        for (std::size_t i = 0; i < ports.size(); ++i) {
          if (i) out << " | ";
          out << (i + 1) << ":" << ports[i].name;
        }
        remoteCommandDetail_ = out.str();
        return;
      }
      if (sub == "NEW") {
        Cue cue;
        cue.kind = CueKind::Midi;
        cue.name = "MIDI " + std::to_string(deck.cues.size() + 1);
        deck.cues.push_back(cue);
        deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
        onSelectionChanged();
        markProjectDirty();
        remoteCommandDetail_ = "midi cue " + std::to_string(deck.cues.size());
        return;
      }

      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("MIDICUE: select a cue first");
        return;
      }
      Cue& cue = deck.cues[deck.selectedIndex];
      if (cue.kind != CueKind::Midi) {
        failRemoteCommand("MIDICUE: the selected cue is not a midi cue");
        return;
      }

      if (sub.empty()) {
        const auto message = midiMessageForCue(cue);
        const auto bytes = deckboy::platform::midi::encodeOutMessage(message);
        std::ostringstream out;
        out << deckboy::platform::midi::describeOutMessage(message)
            << " -> " << (cue.midiPortName.empty() ? std::string("first available")
                                                   : cue.midiPortName)
            << " | ";
        if (bytes.empty()) {
          out << "NOTHING TO SEND";
        } else {
          for (std::size_t i = 0; i < bytes.size(); ++i) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X", bytes[i]);
            if (i) out << " ";
            out << buf;
          }
        }
        remoteCommandDetail_ = out.str();
        return;
      }
      if (sub == "SEND" || sub == "FIRE" || sub == "GO") {
        remoteCommandDetail_ = fireMidiCue(deckIndex, deck.selectedIndex);
        return;
      }
      if (sub == "KIND" && parts.size() >= 3) {
        const std::string token = toLower(parts[2]);
        static const char* kKnown[] = {"note-on", "note-off", "cc", "program",
                                       "msc-go", "msc-stop", "msc-resume", "raw"};
        bool known = false;
        for (const char* k : kKnown) {
          known = known || token == k;
        }
        // Checked against the list rather than trusted: an unknown token falls
        // back to note-on, and a cue labelled "house lights out" that sends a
        // note instead is the worst possible failure here.
        if (!known) {
          failRemoteCommand("MIDICUE KIND: expected note-on, note-off, cc, "
                            "program, msc-go, msc-stop, msc-resume or raw");
          return;
        }
        cue.midiMessage = token;
        markProjectDirty();
        remoteCommandDetail_ = deckboy::platform::midi::outMessageKindLabel(
          deckboy::platform::midi::outMessageKindFromToken(token));
        return;
      }
      if (sub == "PORT") {
        if (parts.size() < 3) {
          cue.midiPortName.clear();
          markProjectDirty();
          remoteCommandDetail_ = "first available";
          return;
        }
        // The rest of the line, because port names have spaces in them.
        std::string name = trim(joinParts(parts, 2));
        cue.midiPortName = name;
        markProjectDirty();
        remoteCommandDetail_ = name.empty() ? "first available" : name;
        return;
      }
      if (sub == "RAW") {
        cue.midiRawHex = parts.size() >= 3 ? trim(joinParts(parts, 2)) : std::string();
        markProjectDirty();
        remoteCommandDetail_ = cue.midiRawHex.empty() ? "cleared" : cue.midiRawHex;
        return;
      }
      if ((sub == "CUENUM" || sub == "CUE") && parts.size() >= 3) {
        cue.mscCue = trim(parts[2]);
        markProjectDirty();
        remoteCommandDetail_ = cue.mscCue;
        return;
      }
      if (sub == "LIST" && parts.size() >= 3) {
        cue.mscList = trim(parts[2]);
        markProjectDirty();
        remoteCommandDetail_ = cue.mscList;
        return;
      }
      if ((sub == "CH" || sub == "CHANNEL" || sub == "D1" || sub == "D2" ||
           sub == "DEVICE") && parts.size() >= 3) {
        auto parsed = parseNumber(2);
        if (!parsed) {
          failRemoteCommand("MIDICUE " + sub + ": expected a number");
          return;
        }
        const int v = static_cast<int>(std::lround(*parsed));
        // NOT CLAMPED SILENTLY. Out of range is refused with the range in the
        // message, the same rule MASTERVOL had to learn: a clamp is what makes
        // wrong units invisible.
        if (sub == "CH" || sub == "CHANNEL") {
          if (v < 1 || v > 16) {
            failRemoteCommand("MIDICUE CHANNEL: expected 1-16");
            return;
          }
          cue.midiChannel = v;
        } else if (v < 0 || v > 127) {
          failRemoteCommand("MIDICUE " + sub + ": expected 0-127");
          return;
        } else if (sub == "D1") {
          cue.midiData1 = v;
        } else if (sub == "D2") {
          cue.midiData2 = v;
        } else {
          cue.mscDevice = v;
        }
        markProjectDirty();
        remoteCommandDetail_ = std::to_string(v);
        return;
      }
      failRemoteCommand("MIDICUE: expected NEW, PORTS, PORT, KIND, CH, D1, D2, "
                        "DEVICE, CUENUM, LIST, RAW or SEND");
      return;
    }
    if (command == "PRELOAD") {
      // PRELOAD [<seconds>]  -> rack the selected cue paused at that position,
      //                         held off the outputs, decode warm
      // PRELOAD OFF|CLEAR    -> forget it
      // PRELOAD STATUS       -> what is racked, if anything
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();
      if (sub == "OFF" || sub == "CLEAR" || sub == "END") {
        if (preloadDeckIndex_ < 0) {
          remoteCommandDetail_ = "nothing preloaded";
          return;
        }
        clearPreload();
        remoteCommandDetail_ = "preload cleared";
        return;
      }
      if (sub == "STATUS") {
        if (preloadDeckIndex_ < 0 ||
            preloadDeckIndex_ >= static_cast<int>(project_.decks.size())) {
          remoteCommandDetail_ = "nothing preloaded";
          return;
        }
        const Deck& d = project_.decks[preloadDeckIndex_];
        remoteCommandDetail_ =
          "deck " + std::to_string(preloadDeckIndex_ + 1) + " cue " +
          std::to_string(preloadCueIndex_ + 1) +
          (preloadCueIndex_ >= 0 && preloadCueIndex_ < static_cast<int>(d.cues.size())
             ? (" " + d.cues[preloadCueIndex_].name) : std::string());
        return;
      }
      double at = 0.0;
      if (!sub.empty()) {
        auto parsed = parseNumber(1);
        if (!parsed || *parsed < 0.0) {
          failRemoteCommand("PRELOAD: expected a position in seconds, OFF or STATUS");
          return;
        }
        at = *parsed;
      }
      remoteCommandDetail_ = preloadSelected(at);
      return;
    }
    if (command == "AUDITION" || command == "PFL") {
      // AUDITION            -> audition the selected cue (PFL, as a sound desk
      //                        calls the same idea: pre-fade listen)
      // AUDITION OFF|END    -> put the deck back on its output
      // AUDITION STATUS     -> who, if anyone
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();
      if (sub == "OFF" || sub == "END" || sub == "STOP") {
        if (!anyDeckAuditioning()) {
          remoteCommandDetail_ = "nothing auditioning";
          return;
        }
        endAudition(false);
        remoteCommandDetail_ = "audition ended";
        return;
      }
      if (sub == "STATUS") {
        remoteCommandDetail_ = anyDeckAuditioning()
          ? ("deck " + std::to_string(auditionDeckIndex_ + 1) + " auditioning")
          : std::string("nothing auditioning");
        return;
      }
      if (!sub.empty()) {
        failRemoteCommand("AUDITION: expected OFF, END or STATUS");
        return;
      }
      remoteCommandDetail_ = auditionSelected();
      return;
    }
    if (command == "FADE" || command == "FADECUE") {
      // FADE NEW                -> add a fade cue to this deck
      // FADE WHAT opacity|volume|dimmer
      // FADE TO <0-100>         -> where the ramp ends
      // FADE OVER <seconds>
      // FADE CURVE linear|ease-in|ease-out|s-curve
      // FADE DECK <n>           -> which deck it acts on
      // FADE STOP on|off        -> stop that deck when the ramp lands
      // FADE FIRE               -> run it now
      // FADE                    -> report it, and what is currently fading
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("FADE: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();

      if (sub == "NEW") {
        Cue cue;
        cue.kind = CueKind::Fade;
        cue.name = "Fade " + std::to_string(deck.cues.size() + 1);
        deck.cues.push_back(cue);
        deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
        onSelectionChanged();
        markProjectDirty();
        remoteCommandDetail_ = "fade cue " + std::to_string(deck.cues.size());
        return;
      }
      if (sub == "RUNNING" || sub == "ACTIVE") {
        remoteCommandDetail_ = fadeRunSummary();
        return;
      }
      if (sub == "CANCEL") {
        const std::size_t before = fadeRuns_.size();
        fadeRuns_.clear();
        remoteCommandDetail_ = "cancelled " + std::to_string(before) + " fade(s)";
        return;
      }

      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("FADE: select a cue first");
        return;
      }
      Cue& cue = deck.cues[deck.selectedIndex];
      if (cue.kind != CueKind::Fade) {
        failRemoteCommand("FADE: the selected cue is not a fade");
        return;
      }

      if (sub.empty()) {
        char pct[8];
        std::snprintf(pct, sizeof(pct), "%d%%",
                      static_cast<int>(std::lround(cue.fadeToValue * 100.0)));
        std::ostringstream out;
        out << cueFadeWhatLabel(cue.fadeWhat) << " -> " << pct
            << " over " << formatSeconds(cue.fadeOverSeconds)
            << ", " << cueFadeCurveLabel(cue.fadeCurve);
        if (cue.fadeWhat != CueFadeWhat::MasterDimmer) {
          out << ", deck " << (cue.targetDeckIndex >= 0
                                 ? std::to_string(cue.targetDeckIndex + 1)
                                 : std::string("this one"));
        }
        if (cue.fadeStopWhenDone) {
          out << ", then stop";
        }
        out << " | " << fadeRunSummary();
        remoteCommandDetail_ = out.str();
        return;
      }
      if (sub == "FIRE" || sub == "GO" || sub == "TAKE") {
        remoteCommandDetail_ = fireFadeCue(deckIndex, deck.selectedIndex);
        return;
      }
      if (sub == "WHAT" && parts.size() >= 3) {
        const std::string token = toLower(parts[2]);
        if (token != "opacity" && token != "volume" && token != "dimmer") {
          failRemoteCommand("FADE WHAT: expected opacity, volume or dimmer");
          return;
        }
        cue.fadeWhat = cueFadeWhatFromToken(token);
        markProjectDirty();
        remoteCommandDetail_ = cueFadeWhatLabel(cue.fadeWhat);
        return;
      }
      if (sub == "CURVE" && parts.size() >= 3) {
        const std::string token = toLower(parts[2]);
        if (token != "linear" && token != "ease-in" && token != "ease-out" &&
            token != "s-curve") {
          failRemoteCommand("FADE CURVE: expected linear, ease-in, ease-out or s-curve");
          return;
        }
        cue.fadeCurve = cueFadeCurveFromToken(token);
        markProjectDirty();
        remoteCommandDetail_ = cueFadeCurveLabel(cue.fadeCurve);
        return;
      }
      if (sub == "TO" && parts.size() >= 3) {
        auto parsed = parseNumber(2);
        // Percent, and NOT clamped into range silently -- MASTERVOL taught that
        // lesson by quietly folding a percent into a multiplier for years while
        // nobody could see it.
        if (!parsed || *parsed < 0.0 || *parsed > 100.0) {
          failRemoteCommand("FADE TO: expected a percent from 0 to 100");
          return;
        }
        cue.fadeToValue = *parsed / 100.0;
        markProjectDirty();
        remoteCommandDetail_ = std::to_string(static_cast<int>(std::lround(*parsed))) + "%";
        return;
      }
      if (sub == "OVER" && parts.size() >= 3) {
        auto parsed = parseNumber(2);
        if (!parsed || *parsed < 0.0) {
          failRemoteCommand("FADE OVER: expected a duration in seconds");
          return;
        }
        cue.fadeOverSeconds = *parsed;
        markProjectDirty();
        remoteCommandDetail_ = formatSeconds(cue.fadeOverSeconds);
        return;
      }
      if (sub == "DECK" && parts.size() >= 3) {
        int target = 0;
        try {
          target = std::stoi(parts[2]) - 1;
        } catch (...) {
          failRemoteCommand("FADE DECK: expected a deck number");
          return;
        }
        if (target < 0 || target >= static_cast<int>(project_.decks.size())) {
          failRemoteCommand("FADE DECK: no deck " + parts[2]);
          return;
        }
        cue.targetDeckIndex = target;
        markProjectDirty();
        remoteCommandDetail_ = "deck " + parts[2];
        return;
      }
      if (sub == "STOP" && parts.size() >= 3) {
        auto state = parseToggleWord(2);
        if (!state) {
          failRemoteCommand("FADE STOP: expected on or off");
          return;
        }
        cue.fadeStopWhenDone = *state;
        markProjectDirty();
        remoteCommandDetail_ = cue.fadeStopWhenDone ? "stop when done" : "leave it running";
        return;
      }
      failRemoteCommand("FADE: expected NEW, WHAT, TO, OVER, CURVE, DECK, STOP, "
                        "FIRE, RUNNING or CANCEL");
      return;
    }
    if (command == "TARGET" || command == "TARGETCUE") {
      // TARGET NEW                  -> add a target cue to this deck
      // TARGET CUE <deck> <cue>     -> point it at deck <deck>'s cue <cue>
      // TARGET VERB <verb>          -> start|stop|pause|resume|load|arm|disarm
      // TARGET FIRE                 -> do it now
      // TARGET                      -> report what it points at
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("TARGET: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();

      if (sub == "NEW") {
        Cue cue;
        cue.kind = CueKind::Target;
        cue.name = "Target " + std::to_string(deck.cues.size() + 1);
        deck.cues.push_back(cue);
        deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
        onSelectionChanged();
        markProjectDirty();
        remoteCommandDetail_ = "target cue " + std::to_string(deck.cues.size());
        return;
      }

      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("TARGET: select a cue first");
        return;
      }
      Cue& cue = deck.cues[deck.selectedIndex];
      if (cue.kind != CueKind::Target) {
        failRemoteCommand("TARGET: the selected cue is not a target");
        return;
      }

      if (sub.empty()) {
        int vd = -1;
        int vi = -1;
        std::ostringstream out;
        out << cueTargetVerbLabel(cue.targetVerb) << " -> ";
        if (!resolveTargetCue(cue, vd, vi)) {
          out << (cue.targetCueId.empty() ? "nothing" : "UNRESOLVED");
        } else {
          out << "deck " << (vd + 1) << " cue " << (vi + 1) << " "
              << project_.decks[vd].cues[vi].name;
        }
        remoteCommandDetail_ = out.str();
        return;
      }
      if (sub == "FIRE" || sub == "GO" || sub == "TAKE") {
        // The reply says what it DID, not merely that the verb was understood.
        // A Stop aimed at a cue that is no longer on air and a Stop that
        // stopped one are the same "OK TARGET" otherwise, which makes the
        // whole thing untestable and unloggable.
        remoteCommandDetail_ = fireTargetCue(deckIndex, deck.selectedIndex);
        return;
      }
      if (sub == "CLEAR") {
        cue.targetCueId.clear();
        cue.targetDeckIndex = -1;
        markProjectDirty();
        remoteCommandDetail_ = "target cleared";
        return;
      }
      if (sub == "VERB" && parts.size() >= 3) {
        const std::string token = toLower(parts[2]);
        // Checked against the token list rather than trusted: an unknown verb
        // would fall back to Start, and a cue labelled "stop the music" that
        // quietly starts it is the worst possible failure here.
        static const char* kVerbs[] = {"start", "stop", "pause", "resume",
                                       "load", "arm", "disarm"};
        bool known = false;
        for (const char* v : kVerbs) {
          known = known || token == v;
        }
        if (!known) {
          failRemoteCommand("TARGET VERB: expected start|stop|pause|resume|load|arm|disarm");
          return;
        }
        cue.targetVerb = cueTargetVerbFromToken(token);
        markProjectDirty();
        remoteCommandDetail_ = cueTargetVerbLabel(cue.targetVerb);
        return;
      }
      if (sub == "CUE" && parts.size() >= 4) {
        int target = 0;
        int cueNumber = 0;
        try {
          target = std::stoi(parts[2]) - 1;
          cueNumber = std::stoi(parts[3]) - 1;
        } catch (...) {
          failRemoteCommand("TARGET CUE: expected a deck number and a cue number");
          return;
        }
        if (target < 0 || target >= static_cast<int>(project_.decks.size())) {
          failRemoteCommand("TARGET CUE: no deck " + parts[2]);
          return;
        }
        const Deck& targetDeck = project_.decks[target];
        if (cueNumber < 0 || cueNumber >= static_cast<int>(targetDeck.cues.size())) {
          failRemoteCommand("TARGET CUE: deck " + parts[2] + " has no cue " + parts[3]);
          return;
        }
        cue.targetCueId = targetDeck.cues[cueNumber].id;
        cue.targetDeckIndex = target;
        markProjectDirty();
        remoteCommandDetail_ = "-> deck " + parts[2] + " " + targetDeck.cues[cueNumber].name;
        return;
      }
      failRemoteCommand("TARGET: expected NEW, CUE, VERB, CLEAR or FIRE");
      return;
    }
    if (command == "ARM" || command == "DISARM") {
      // ARM / DISARM            -> the selected cue
      // ARM <n> / DISARM <n>    -> cue n of this deck
      // ARM ALL / DISARM ALL    -> every cue on this deck
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand(command + ": no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const bool arm = command == "ARM";
      if (parts.size() > 1 && toUpper(parts[1]) == "ALL") {
        for (Cue& c : deck.cues) {
          c.armed = arm;
        }
        markProjectDirty();
        remoteCommandDetail_ = std::string(arm ? "armed " : "disarmed ") +
                               std::to_string(deck.cues.size()) + " cue(s)";
        return;
      }
      int index = deck.selectedIndex;
      if (parts.size() > 1) {
        try {
          index = std::stoi(parts[1]) - 1;
        } catch (...) {
          failRemoteCommand(command + ": expected a cue number or ALL");
          return;
        }
      }
      if (index < 0 || index >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand(command + ": no cue " +
                          (parts.size() > 1 ? parts[1] : std::string("selected")));
        return;
      }
      deck.cues[index].armed = arm;
      markProjectDirty();
      remoteCommandDetail_ = std::string(arm ? "armed: " : "disarmed: ") +
                             deck.cues[index].name;
      return;
    }
    if (command == "TRACKER" || command == "SEQUENCE") {
      // TRACKER GO|NEXT        -> fire the next step
      // TRACKER BACK|PREV      -> fire the step before
      // TRACKER PLAY | STOP    -> run the sequence on the steps' lengths / halt it
      // TRACKER STEP <n>       -> fire step n (1-based)
      // TRACKER LOOP [ON|OFF]  -> wrap from the last step to the first
      // TRACKER CLICKER [ON|OFF] -> Page Down / Page Up step the tracker
      // TRACKER                -> where the sequence stands
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();
      const auto rows = masterTrackerRows();
      auto onOff = [&](bool current) {
        if (parts.size() < 3) return !current;
        const std::string v = toUpper(parts[2]);
        return v == "ON" || v == "1" || v == "TRUE";
      };
      if (sub.empty() || sub == "STATUS") {
        const int head = trackerPlayheadRow();
        std::ostringstream out;
        out << rows.size() << " steps";
        if (head >= 0) out << " | step " << (head + 1);
        out << (trackerPlaying_ ? " | playing" : " | stopped");
        const double remaining = trackerRemainingSeconds();
        if (remaining >= 0.0) out << " | next in " << std::fixed << std::setprecision(1) << remaining << "s";
        out << (project_.trackerLoop ? " | loop" : "")
            << (project_.clickerDrivesTracker ? " | clicker" : "");
        remoteCommandDetail_ = out.str();
        return;
      }
      if (rows.empty() && sub != "LOOP" && sub != "CLICKER") {
        failRemoteCommand("TRACKER: there are no steps (MASTER NEW makes one)");
        return;
      }
      if (sub == "GO" || sub == "NEXT") { trackerGo(); return; }
      if (sub == "BACK" || sub == "PREV" || sub == "PREVIOUS") { trackerBack(); return; }
      if (sub == "PLAY") { trackerPlay(); return; }
      if (sub == "STOP" || sub == "HALT") { trackerStop(); return; }
      if (sub == "STEP" && parts.size() >= 3) {
        const int step = std::atoi(parts[2].c_str());
        if (step < 1 || step > static_cast<int>(rows.size())) {
          failRemoteCommand("TRACKER STEP: no step " + parts[2] + " (1-" +
                            std::to_string(rows.size()) + ")");
          return;
        }
        fireTrackerRow(step - 1);
        return;
      }
      if (sub == "LOOP") {
        project_.trackerLoop = onOff(project_.trackerLoop);
        markProjectDirty();
        remoteCommandDetail_ = project_.trackerLoop ? "loop on" : "loop off";
        return;
      }
      if (sub == "CLICKER") {
        project_.clickerDrivesTracker = onOff(project_.clickerDrivesTracker);
        markProjectDirty();
        remoteCommandDetail_ = project_.clickerDrivesTracker ? "clicker on" : "clicker off";
        return;
      }
      failRemoteCommand("TRACKER: expected GO, BACK, PLAY, STOP, STEP <n>, LOOP or CLICKER");
      return;
    }
    if (command == "MASTER" || command == "MASTERCUE") {
      // MASTER NEW                     -> add a master cue to this deck
      // MASTER DECK <n> <cue>          -> assign: deck n plays cue <cue>
      // MASTER BYPASS <n> ON|OFF       -> skip deck n when this master fires
      // MASTER CLEAR                   -> drop every assignment
      // MASTER FIRE                    -> take it now
      // MASTER                         -> report what it holds
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("MASTER: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();

      if (sub == "NEW") {
        Cue cue;
        cue.kind = CueKind::Master;
        // No id set here: normalizeProject assigns one to any cue that lacks
        // it and dedupes the result, which is how every other cue-creating
        // path does it.
        cue.name = "Master " + std::to_string(deck.cues.size() + 1);
        deck.cues.push_back(cue);
        deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
        deck.isMasterDeck = true;   // a deck that holds masters IS the master deck
        onSelectionChanged();
        markProjectDirty();
        remoteCommandDetail_ = "master cue " + std::to_string(deck.cues.size());
        return;
      }

      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("MASTER: select a cue first");
        return;
      }
      Cue& cue = deck.cues[deck.selectedIndex];
      if (cue.kind != CueKind::Master) {
        failRemoteCommand("MASTER: the selected cue is not a master");
        return;
      }

      if (sub.empty()) {
        std::ostringstream out;
        out << cue.masterAssignments.size() << " assignment(s)";
        for (const auto& a : cue.masterAssignments) {
          const int idx = findCueIndexById(a.deckIndex, a.cueId);
          out << " | deck " << (a.deckIndex + 1) << " -> "
              << (idx < 0 ? std::string("UNRESOLVED")
                          : project_.decks[a.deckIndex].cues[idx].name)
              << (a.bypassed ? " (bypassed)" : "");
        }
        out << (masterCueIsLive(cue) ? " | LIVE" : "");
        remoteCommandDetail_ = out.str();
        return;
      }
      if (sub == "FIRE" || sub == "GO" || sub == "TAKE") {
        // WITH A NAME, it fires THAT master wherever it lives; without one,
        // the selected cue as before. A dashboard button has no selection --
        // it is pressed from across the room, from Companion, or from a
        // controller -- so a verb that can only fire "the current one" makes
        // every button on a dashboard of masters do the same thing.
        if (parts.size() >= 3) {
          const std::string wanted = trim(joinParts(parts, 2));
          for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
            const Deck& searched = project_.decks[d];
            for (int c = 0; c < static_cast<int>(searched.cues.size()); ++c) {
              const Cue& candidate = searched.cues[c];
              if (candidate.kind != CueKind::Master) {
                continue;
              }
              if (candidate.id == wanted || candidate.name == wanted) {
                fireMasterCue(d, c);
                remoteCommandDetail_ = candidate.name;
                return;
              }
            }
          }
          failRemoteCommand("MASTER FIRE: no master cue called '" + wanted + "'");
          return;
        }
        fireMasterCue(deckIndex, deck.selectedIndex);
        return;
      }
      if (sub == "CLEAR") {
        cue.masterAssignments.clear();
        markProjectDirty();
        remoteCommandDetail_ = "master cleared";
        return;
      }
      if (sub == "DECK" && parts.size() >= 4) {
        int target = 0;
        int cueNumber = 0;
        try {
          target = std::stoi(parts[2]) - 1;
          cueNumber = std::stoi(parts[3]) - 1;
        } catch (...) {
          failRemoteCommand("MASTER DECK: expected a deck number and a cue number");
          return;
        }
        if (target < 0 || target >= static_cast<int>(project_.decks.size())) {
          failRemoteCommand("MASTER DECK: no deck " + parts[2]);
          return;
        }
        const Deck& targetDeck = project_.decks[target];
        if (cueNumber < 0 || cueNumber >= static_cast<int>(targetDeck.cues.size())) {
          failRemoteCommand("MASTER DECK: deck " + parts[2] + " has no cue " + parts[3]);
          return;
        }
        // Stored BY ID: an index would silently repoint at the neighbour the
        // moment somebody reorders or deletes a cue above it.
        const std::string id = targetDeck.cues[cueNumber].id;
        bool replaced = false;
        for (auto& a : cue.masterAssignments) {
          if (a.deckIndex == target) {
            a.cueId = id;
            replaced = true;
            break;
          }
        }
        if (!replaced) {
          MasterAssignment a;
          a.deckIndex = target;
          a.cueId = id;
          cue.masterAssignments.push_back(a);
        }
        markProjectDirty();
        remoteCommandDetail_ = "deck " + parts[2] + " -> " + targetDeck.cues[cueNumber].name;
        return;
      }
      if (sub == "BYPASS" && parts.size() >= 3) {
        int target = 0;
        try {
          target = std::stoi(parts[2]) - 1;
        } catch (...) {
          failRemoteCommand("MASTER BYPASS: expected a deck number");
          return;
        }
        const std::string state = parts.size() > 3 ? toUpper(parts[3]) : std::string("TOGGLE");
        for (auto& a : cue.masterAssignments) {
          if (a.deckIndex == target) {
            a.bypassed = (state == "ON") ? true
                       : (state == "OFF") ? false
                       : !a.bypassed;
            markProjectDirty();
            remoteCommandDetail_ = "deck " + parts[2] +
              (a.bypassed ? " bypassed" : " active");
            return;
          }
        }
        failRemoteCommand("MASTER BYPASS: deck " + parts[2] + " is not assigned");
        return;
      }
      failRemoteCommand("MASTER: use NEW | DECK <n> <cue> | BYPASS <n> [ON|OFF] | CLEAR | FIRE");
      return;
    }
    if (command == "STANDBY") {
      // STANDBY            -> report what is armed
      // STANDBY <n>        -> arm cue n (1-based, as the list numbers them)
      // STANDBY NEXT|PREV  -> step it
      // STANDBY CLEAR      -> disarm; GO falls back to the selection
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("STANDBY: no deck");
        return;
      }
      Deck& deck = project_.decks[deckIndex];
      const int current = standbyIndexFor(deckIndex);
      if (parts.size() < 2) {
        remoteCommandDetail_ = current < 0
          ? "standby: none"
          : ("standby: " + std::to_string(current + 1) + " " + deck.cues[current].name);
        return;
      }
      const std::string sub = toUpper(parts[1]);
      if (sub == "CLEAR" || sub == "NONE" || sub == "OFF") {
        setStandbyIndex(deckIndex, -1);
        return;
      }
      if (sub == "NEXT" || sub == "PREV" || sub == "PREVIOUS") {
        if (deck.cues.empty()) {
          failRemoteCommand("STANDBY: the deck is empty");
          return;
        }
        const int step = (sub == "NEXT") ? 1 : -1;
        const int base = (current < 0) ? (step > 0 ? -1 : static_cast<int>(deck.cues.size()))
                                       : current;
        setStandbyIndex(deckIndex,
                        std::clamp(base + step, 0, static_cast<int>(deck.cues.size()) - 1));
        return;
      }
      try {
        setStandbyIndex(deckIndex, std::stoi(parts[1]) - 1);
      } catch (...) {
        failRemoteCommand("STANDBY: expected a cue number, NEXT, PREV or CLEAR");
      }
      return;
    }
    if (command == "GO") {
      goTransport();
      return;
    }
    if (command == "TOGGLE") {
      // PLAY/PAUSE, and only that. It shared GO's implementation, so a show
      // with a standby armed could not be paused from a controller either.
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
      } else if (sub == "FACE") {
        // 7seg / blocky / typeface. The geometric two never depend on what is
        // installed, which is why they stay the default for a stage screen;
        // typeface is for an event that has a font and expects to see it.
        Cue* cue = selectedCueMutable();
        if (!cue || cue->kind != CueKind::Timer) {
          failRemoteCommand("timer face: select a timer cue first");
          return;
        }
        if (parts.size() < 3) {
          remoteCommandDetail_ =
            cue->timer.face == TimerFace::Blocky ? "blocky"
            : cue->timer.face == TimerFace::Typeface ? "typeface" : "7seg";
          return;
        }
        const std::string want = toLower(parts[2]);
        if (want == "7seg" || want == "sevenseg" || want == "seven") {
          cue->timer.face = TimerFace::SevenSegment;
        } else if (want == "blocky" || want == "dot" || want == "matrix") {
          cue->timer.face = TimerFace::Blocky;
        } else if (want == "typeface" || want == "font" || want == "ttf") {
          cue->timer.face = TimerFace::Typeface;
        } else {
          failRemoteCommand("timer face: expected 7seg|blocky|typeface, got " +
                            parts[2]);
          return;
        }
        markProjectDirty();
        remoteCommandDetail_ = want;
      } else if (sub == "FONT") {
        Cue* cue = selectedCueMutable();
        if (!cue || cue->kind != CueKind::Timer) {
          failRemoteCommand("timer font: select a timer cue first");
          return;
        }
        // No argument clears it, which is how a clock goes back to the app's
        // own bundled face.
        cue->timer.fontPath = parts.size() < 3 ? std::string() : joinParts(parts, 2);
        if (!cue->timer.fontPath.empty()) {
          cue->timer.face = TimerFace::Typeface;   // picking one means using it
        }
        markProjectDirty();
        remoteCommandDetail_ = cue->timer.fontPath.empty()
                                 ? "the app's own face"
                                 : cue->timer.fontPath;
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
        // The answer goes to the CALLER as well as the screen: a query
        // whose reply is a bare OK has not answered.
        remoteCommandDetail_ = "jump mode: " + jumpModeLabelFromToken(project_.jumpMode);
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
        // The answer goes to the CALLER as well as the screen: a query
        // whose reply is a bare OK has not answered.
        remoteCommandDetail_ = "panic profile: " + panicProfileLabelFromToken(project_.panicProfile);
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
        // The answer goes to the CALLER as well as the screen: a query
        // whose reply is a bare OK has not answered.
        remoteCommandDetail_ = "panic fade " + label.str() + "s";
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
        // The answer goes to the CALLER as well as the screen: a query
        // whose reply is a bare OK has not answered.
        remoteCommandDetail_ = "osc query port: " + std::to_string(project_.oscQueryPort);
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
        // The answer goes to the CALLER as well as the screen: a query
        // whose reply is a bare OK has not answered.
        remoteCommandDetail_ = "osc feedback rate: " + std::to_string(project_.oscFeedbackRateMs) + " ms";
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
        // The answer goes to the CALLER as well as the screen: a query
        // whose reply is a bare OK has not answered.
        remoteCommandDetail_ = "artnet port: " + std::to_string(project_.artNetPort);
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
    // MASTER and MASTERCUE are implemented again, above -- master cues came
    // back 2026-09-21. The other three named the OLD scene-preset feature,
    // which is a different idea and stays gone.
    if (command == "GROUP" || command == "GROUPPRESET") {
      failRemoteCommand("group presets became master cues: use MASTER");
      return;
    }
    if (command == "PRESET") {
      // PRESET | PRESET LIST             -> what there is, and what each recalls
      // PRESET SAVE [name]               -> a new preset from what is on now
      // PRESET UPDATE <n|name|id>        -> capture again into it
      // PRESET RECALL <n|name|id>, or PRESET <n>
      // PRESET SCOPE <n|name|id> [ALL | ONLY <group...> | <group> ON|OFF ...]
      //   groups: cues position look effects levels routing master
      // PRESET RENAME <n|name|id> <name> | PRESET DELETE <n|name|id>
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string("LIST");
      auto groupBit = [](const std::string& word) {
        const std::string w = toLower(word);
        for (int bit = 0; bit < kPresetScopeCount; ++bit) {
          if (w == presetScopeToken(bit)) return bit;
        }
        return -1;
      };
      if (sub == "LIST") {
        if (project_.presets.empty()) {
          remoteCommandDetail_ = "no presets (PRESET SAVE makes one)";
          return;
        }
        std::string out;
        for (std::size_t i = 0; i < project_.presets.size(); ++i) {
          if (i) out += " | ";
          out += std::to_string(i + 1) + " " + project_.presets[i].name + " [" +
                 presetScopeSummary(project_.presets[i].scope) + "]";
        }
        remoteCommandDetail_ = out;
        return;
      }
      if (sub == "SAVE" || sub == "NEW") {
        const ShowPreset& made = savePresetFromNow(parts.size() > 2 ? joinParts(parts, 2)
                                                                    : std::string());
        remoteCommandDetail_ = std::to_string(project_.presets.size()) + " " + made.name +
                               " (" + made.id + ")";
        return;
      }
      // Everything below names a preset. A bare number is a recall. RECALL,
      // UPDATE and DELETE take the rest of the line, so a name with spaces in
      // it works; SCOPE and RENAME have more to say after it, so there it is
      // one word -- a number or an id always is.
      const bool bareNumber = sub.find_first_not_of("0123456789") == std::string::npos;
      const bool restIsName = sub == "RECALL" || sub == "GO" || sub == "FIRE" ||
                              sub == "UPDATE" || sub == "CAPTURE" ||
                              sub == "DELETE" || sub == "REMOVE";
      const std::string key = bareNumber ? parts[1]
        : parts.size() <= 2 ? std::string()
        : restIsName ? joinParts(parts, 2) : parts[2];
      ShowPreset* preset = findPreset(key);
      if (!preset) {
        failRemoteCommand("PRESET: no preset '" + key + "'");
        return;
      }
      if (bareNumber || sub == "RECALL" || sub == "GO" || sub == "FIRE") {
        recallPreset(*preset);
        remoteCommandDetail_ = preset->name;
        return;
      }
      if (sub == "UPDATE" || sub == "CAPTURE") {
        capturePresetState(*preset);
        markProjectDirty();
        remoteCommandDetail_ = preset->name + " updated";
        return;
      }
      if (sub == "RENAME" && parts.size() > 3) {
        preset->name = joinParts(parts, 3);
        markProjectDirty();
        remoteCommandDetail_ = preset->name;
        return;
      }
      if (sub == "DELETE" || sub == "REMOVE") {
        const std::string name = preset->name;
        project_.presets.erase(project_.presets.begin() + (preset - project_.presets.data()));
        markProjectDirty();
        remoteCommandDetail_ = name + " deleted";
        return;
      }
      if (sub == "SCOPE") {
        std::size_t at = 3;
        if (parts.size() > at) {
          const std::string first = toUpper(parts[at]);
          if (first == "ALL") {
            preset->scope = kPresetScopeAll;
            ++at;
          } else if (first == "ONLY") {
            preset->scope = 0;
            for (++at; at < parts.size(); ++at) {
              const int bit = groupBit(parts[at]);
              if (bit < 0) {
                failRemoteCommand("PRESET SCOPE: no group '" + parts[at] + "'");
                return;
              }
              preset->scope |= (1u << bit);
            }
          }
          for (; at < parts.size(); at += 2) {
            const int bit = groupBit(parts[at]);
            if (bit < 0) {
              failRemoteCommand("PRESET SCOPE: no group '" + parts[at] + "' (cues, position, "
                                "look, effects, levels, routing, master)");
              return;
            }
            const std::string v = at + 1 < parts.size() ? toUpper(parts[at + 1]) : std::string("ON");
            if (v == "OFF" || v == "0") {
              preset->scope &= ~(1u << bit);
            } else {
              preset->scope |= (1u << bit);
            }
          }
          markProjectDirty();
        }
        remoteCommandDetail_ = preset->name + " recalls " + presetScopeSummary(preset->scope);
        return;
      }
      failRemoteCommand("PRESET: expected LIST, SAVE, RECALL, UPDATE, SCOPE, RENAME or DELETE");
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
    // ── SPLITNOTES [<cue>] ────────────────────────────────────────────────
    //
    // Turn one cue into one cue per part of its notes. The other way to pace a
    // long note: the presenter view scrolls WITHIN a cue, this puts the parts
    // in the playlist where the rest of the show can address them.
    if (command == "SPLITNOTES") {
      const int deckIndex = project_.focusedDeckIndex;
      if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
        failRemoteCommand("splitnotes: no deck");
        return;
      }
      const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
      // 1-based over the wire, like every other cue index; no argument means
      // the selected cue.
      int cueIndex = deck.selectedIndex;
      if (parts.size() > 1) {
        const int asked = std::atoi(parts[1].c_str());
        if (asked < 1 || asked > static_cast<int>(deck.cues.size())) {
          failRemoteCommand("splitnotes: cue out of range 1-" +
                            std::to_string(deck.cues.size()) + ", got " + parts[1]);
          return;
        }
        cueIndex = asked - 1;
      }
      if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("splitnotes: no cue selected");
        return;
      }
      const std::size_t count = noteBuildParts(deck.cues[
        static_cast<std::size_t>(cueIndex)].notes).size();
      if (count < 2) {
        failRemoteCommand("splitnotes: that cue's notes have no parts "
                          "(separate them with a line of ---)");
        return;
      }
      if (!splitCueNotesIntoCues(deckIndex, cueIndex)) {
        failRemoteCommand("splitnotes: could not split that cue");
        return;
      }
      remoteCommandDetail_ = "split into " + std::to_string(count) + " cues";
      return;
    }
    // ── PROMPTER <setting> [value] ────────────────────────────────────────
    //
    // A prompter is DRIVEN, not configured and left: the operator rides the
    // pace against the reader all the way through a take. So every control a
    // hand controller would have is here, on the focused output.
    if (command == "PROMPTER") {
      if (project_.outputs.empty()) {
        failRemoteCommand("prompter: no outputs");
        return;
      }
      OutputTarget::PrompterOptions& opt = focusedOutputMutable().prompter;
      const int outputIndex = std::clamp(project_.focusedOutputIndex, 0,
                                         static_cast<int>(project_.outputs.size()) - 1);
      const std::string sub = parts.size() < 2 ? std::string("STATUS")
                                               : toUpper(parts[1]);
      auto report = [&]() {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "%s  %.0f lpm  size=%.2f  line=%.2f  mirror=%s%s  %s",
                      opt.running ? "RUNNING" : "paused", opt.linesPerMinute,
                      opt.fontScale, opt.readingLineFraction,
                      opt.mirrorHorizontal ? "h" : "-",
                      opt.mirrorVertical ? "v" : "-",
                      opt.script.empty() ? "following the live cue's notes"
                                         : "own script");
        remoteCommandDetail_ = buf;
      };
      if (sub == "STATUS") { report(); return; }
      if (sub == "RUN" || sub == "START" || sub == "GO") {
        opt.running = true; markProjectDirty(); report(); return;
      }
      if (sub == "STOP" || sub == "PAUSE" || sub == "HOLD") {
        opt.running = false; markProjectDirty(); report(); return;
      }
      if (sub == "TOGGLE") {
        opt.running = !opt.running; markProjectDirty(); report(); return;
      }
      if (sub == "TOP" || sub == "RESET" || sub == "REWIND") {
        prompterScroll_[outputIndex] = 0.0;
        remoteCommandDetail_ = "back to the top";
        return;
      }
      // SCROLL nudges by whole lines, which is what a jog wheel sends. The
      // sign is the direction, and going back is as important as going on:
      // readers lose their place.
      if (sub == "SCROLL" || sub == "JOG") {
        const double lines = parts.size() < 3 ? 1.0 : std::atof(parts[2].c_str());
        // In LINES, spent by the next frame. A line's height depends on the
        // font and the screen, which only the renderer knows -- storing a
        // guess in pixels here would make a jog mean different distances on
        // different outputs.
        prompterJog_[outputIndex] += lines;
        remoteCommandDetail_ = "jogged " + std::to_string(lines) + " lines";
        return;
      }
      if (sub == "SPEED" || sub == "PACE" || sub == "LPM") {
        if (parts.size() < 3) { report(); return; }
        const double want = std::atof(parts[2].c_str());
        if (want < 10.0 || want > 600.0) {
          failRemoteCommand("prompter speed: expected 10-600 lines per minute, got " +
                            parts[2]);
          return;
        }
        opt.linesPerMinute = want; markProjectDirty(); report(); return;
      }
      if (sub == "SIZE" || sub == "SCALE") {
        if (parts.size() < 3) { report(); return; }
        const double want = std::atof(parts[2].c_str());
        if (want < 0.5 || want > 8.0) {
          failRemoteCommand("prompter size: expected 0.5-8.0, got " + parts[2]);
          return;
        }
        opt.fontScale = want; markProjectDirty(); report(); return;
      }
      if (sub == "LINE" || sub == "READLINE") {
        if (parts.size() < 3) { report(); return; }
        const double want = std::atof(parts[2].c_str());
        if (want < 0.05 || want > 0.95) {
          failRemoteCommand("prompter line: expected 0.05-0.95 down the screen, got " +
                            parts[2]);
          return;
        }
        opt.readingLineFraction = want; markProjectDirty(); report(); return;
      }
      if (sub == "MIRROR") {
        const std::string arg = parts.size() < 3 ? std::string("TOGGLE")
                                                 : toUpper(parts[2]);
        if (arg == "OFF" || arg == "NONE") {
          opt.mirrorHorizontal = opt.mirrorVertical = false;
        } else if (arg == "H" || arg == "HORIZONTAL" || arg == "ON") {
          opt.mirrorHorizontal = true; opt.mirrorVertical = false;
        } else if (arg == "V" || arg == "VERTICAL") {
          opt.mirrorHorizontal = false; opt.mirrorVertical = true;
        } else if (arg == "BOTH" || arg == "HV") {
          opt.mirrorHorizontal = opt.mirrorVertical = true;
        } else {
          opt.mirrorHorizontal = !opt.mirrorHorizontal;
        }
        markProjectDirty(); report(); return;
      }
      // SCRIPT with no argument clears it, which is how an output goes back to
      // following the live cue's notes.
      if (sub == "SCRIPT") {
        opt.script = parts.size() < 3 ? std::string() : joinParts(parts, 2);
        prompterScroll_[outputIndex] = 0.0;
        markProjectDirty();
        report();
        return;
      }
      failRemoteCommand("prompter: expected RUN|STOP|TOGGLE|TOP|SCROLL|SPEED|"
                        "SIZE|LINE|MIRROR|SCRIPT|STATUS, got " + parts[1]);
      return;
    }
    // ── PRESENTER <setting> [value] ───────────────────────────────────────
    //
    // Everything on the presenter screen, over the wire. It exists for the
    // same reason the settings page does -- the screen belongs to whoever is
    // reading it, and what they want changed they want changed NOW -- and it
    // exists as a command as well as a page because the person who needs it
    // adjusted is usually standing at the lectern rather than at the rack.
    //
    // Acts on the FOCUSED output, like every other output setting.
    if (command == "PRESENTER") {
      if (project_.outputs.empty()) {
        failRemoteCommand("presenter: no outputs");
        return;
      }
      OutputTarget& output = focusedOutputMutable();
      OutputTarget::PresenterOptions& opt = output.presenter;
      const std::string sub = parts.size() < 2 ? std::string("STATUS")
                                               : toUpper(parts[1]);
      auto scaleText = [&]() {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", opt.notesScale);
        return std::string(buf);
      };
      auto shareText = [&]() {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", opt.notesShare);
        return std::string(buf);
      };
      auto report = [&]() {
        remoteCommandDetail_ =
          opt.layout + "  prev=" + (opt.showPrevious ? "on" : "off") +
          " next=" + (opt.showNext ? "on" : "off") +
          " notes=" + (opt.showNotes ? "on" : "off") +
          " clock=" + (opt.showClock ? "on" : "off") +
          " timers=" + (opt.showTimers ? "on" : "off") +
          " live=" + (opt.showLive ? "on" : "off") +
          " share=" + shareText() +
          " builds=" + (opt.buildsConsumeAdvance ? "on" : "off") +
          " scale=" + scaleText() +
          " bg=" + opt.background + " ink=" + opt.ink + " accent=" + opt.accent +
          (opt.customLayout.empty() ? std::string()
                                    : ("  panels " + opt.customLayout));
      };
      if (sub == "STATUS") {
        report();
        return;
      }
      // A switch takes on|off|toggle, and no argument toggles -- the same
      // shape every other boolean in this protocol has.
      auto flag = [&](bool& value) {
        const std::string arg = parts.size() < 3 ? std::string("TOGGLE")
                                                 : toUpper(parts[2]);
        if (arg == "ON" || arg == "1" || arg == "YES") value = true;
        else if (arg == "OFF" || arg == "0" || arg == "NO") value = false;
        else value = !value;
        markProjectDirty();
        report();
      };
      if (sub == "LAYOUT") {
        if (parts.size() < 3) {
          remoteCommandDetail_ = opt.layout;
          return;
        }
        const std::string want = toLower(parts[2]);
        if (want != "wide" && want != "filmstrip" && want != "notes" &&
            want != "custom") {
          failRemoteCommand("presenter layout: expected "
                            "wide|filmstrip|notes|custom, got " + parts[2]);
          return;
        }
        // Switching TO custom with nothing arranged yet would give a blank
        // screen, so it starts from whatever preset was up -- which is also
        // how anybody sane would begin arranging one.
        if (want == "custom" && opt.customLayout.empty()) {
          opt.customLayout = formatPresenterLayout(presenterPresetFracs(opt.layout));
        }
        opt.layout = want;
        markProjectDirty();
        report();
        return;
      }
      // ── PRESENTER PANEL <live|prev|next|notes> <x> <y> <w> <h> ─────────
      //
      // Percentages of the area below the header and above the footer, which
      // is the same space the drag editor works in. Setting one switches the
      // layout to custom: you cannot half-arrange a preset, and silently
      // storing an arrangement nothing displays would be worse than saying so.
      if (sub == "PANEL") {
        if (parts.size() < 7) {
          failRemoteCommand("presenter panel: expected "
                            "<live|prev|next|notes> <x> <y> <w> <h> in percent");
          return;
        }
        const std::string which = toLower(parts[2]);
        if (which != "live" && which != "prev" && which != "previous" &&
            which != "next" && which != "notes") {
          failRemoteCommand("presenter panel: expected live|prev|next|notes, "
                            "got " + parts[2]);
          return;
        }
        double v[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
          try {
            v[i] = std::stod(parts[static_cast<std::size_t>(3 + i)]);
          } catch (...) {
            failRemoteCommand("presenter panel: expected a number, got " +
                              parts[static_cast<std::size_t>(3 + i)]);
            return;
          }
        }
        if (v[0] < 0.0 || v[1] < 0.0 || v[2] <= 0.0 || v[3] <= 0.0 ||
            v[0] + v[2] > 100.5 || v[1] + v[3] > 100.5) {
          failRemoteCommand("presenter panel: the panel must fit on the screen "
                            "(x+w and y+h no more than 100)");
          return;
        }
        if (opt.customLayout.empty()) {
          opt.customLayout = formatPresenterLayout(presenterPresetFracs(opt.layout));
        }
        PresenterFracs f = parsePresenterLayout(opt.customLayout);
        const PresenterFrac placed {v[0] / 100.0, v[1] / 100.0, v[2] / 100.0,
                                    v[3] / 100.0};
        if (which == "live") f.live = placed;
        else if (which == "next") f.next = placed;
        else if (which == "notes") f.notes = placed;
        else f.previous = placed;
        opt.customLayout = formatPresenterLayout(f);
        opt.layout = "custom";
        markProjectDirty();
        remoteCommandDetail_ = "custom  " + opt.customLayout;
        return;
      }
      // Take the layout that is on screen now and make it editable, so
      // arranging one starts from something that already works.
      if (sub == "CAPTURE" || sub == "COPY") {
        opt.customLayout = formatPresenterLayout(presenterPresetFracs(opt.layout));
        opt.layout = "custom";
        markProjectDirty();
        remoteCommandDetail_ = "custom  " + opt.customLayout;
        return;
      }
      // Open the arranger on the control window. Here as well as on the
      // monitor button because an operator with a controller in their hands
      // should not have to find a button with a mouse.
      if (sub == "ARRANGE" || sub == "EDIT") {
        const std::string arg = parts.size() < 3 ? std::string("TOGGLE")
                                                 : toUpper(parts[2]);
        if (arg == "ON") presenterLayoutEditMode_ = true;
        else if (arg == "OFF") presenterLayoutEditMode_ = false;
        else presenterLayoutEditMode_ = !presenterLayoutEditMode_;
        if (presenterLayoutEditMode_) warpEditMode_ = false;
        remoteCommandDetail_ = presenterLayoutEditMode_ ? "arranging" : "closed";
        return;
      }
      if (sub == "LIVE" || sub == "PICTURE")   { flag(opt.showLive); return; }
      if (sub == "PREV" || sub == "PREVIOUS") { flag(opt.showPrevious); return; }
      if (sub == "NEXT")                      { flag(opt.showNext); return; }
      if (sub == "NOTES")                     { flag(opt.showNotes); return; }
      if (sub == "CLOCK")                     { flag(opt.showClock); return; }
      if (sub == "TIMERS")                    { flag(opt.showTimers); return; }
      if (sub == "BUILDS")  { flag(opt.buildsConsumeAdvance); return; }
      // How much of the screen the words get, as a scale on each layout's own
      // proportion. With every picture switched off they get all of it.
      if (sub == "SHARE" || sub == "NOTESHARE") {
        if (parts.size() < 3) {
          remoteCommandDetail_ = shareText();
          return;
        }
        double want = 0.0;
        try {
          want = std::stod(parts[2]);
        } catch (...) {
          failRemoteCommand("presenter share: expected a number, got " + parts[2]);
          return;
        }
        if (want < 0.15 || want > 1.0) {
          failRemoteCommand("presenter share: expected 0.15-1.0, got " + parts[2]);
          return;
        }
        opt.notesShare = want;
        markProjectDirty();
        report();
        return;
      }
      if (sub == "SCALE" || sub == "NOTESCALE") {
        if (parts.size() < 3) {
          remoteCommandDetail_ = scaleText();
          return;
        }
        double want = 0.0;
        try {
          want = std::stod(parts[2]);
        } catch (...) {
          failRemoteCommand("presenter scale: expected a number, got " + parts[2]);
          return;
        }
        // Not clamped silently: a scale outside this is either a typo or a
        // misunderstanding of the units, and both deserve an answer.
        if (want < 0.5 || want > 4.0) {
          failRemoteCommand("presenter scale: expected 0.5-4.0, got " + parts[2]);
          return;
        }
        opt.notesScale = want;
        markProjectDirty();
        report();
        return;
      }
      if (sub == "COLOUR" || sub == "COLOR") {
        if (parts.size() < 4) {
          failRemoteCommand("presenter colour: expected "
                            "BG|INK|ACCENT #rrggbb");
          return;
        }
        const std::string which = toUpper(parts[2]);
        if (!tryParseColor(parts[3])) {
          failRemoteCommand("presenter colour: expected #rrggbb, got " + parts[3]);
          return;
        }
        if (which == "BG" || which == "BACKGROUND") opt.background = parts[3];
        else if (which == "INK" || which == "TEXT") opt.ink = parts[3];
        else if (which == "ACCENT") opt.accent = parts[3];
        else {
          failRemoteCommand("presenter colour: expected BG|INK|ACCENT, got " +
                            parts[2]);
          return;
        }
        markProjectDirty();
        report();
        return;
      }
      if (sub == "RESET" || sub == "DEFAULTS") {
        opt = OutputTarget::PresenterOptions {};
        markProjectDirty();
        report();
        return;
      }
      failRemoteCommand("presenter: expected LAYOUT|PANEL|CAPTURE|ARRANGE|LIVE|PREV|"
                        "NEXT|NOTES|CLOCK|TIMERS|BUILDS|SHARE|SCALE|COLOUR|"
                        "RESET|STATUS, got " + parts[1]);
      return;
    }
    // ── NOTESTEP [NEXT|PREV|FIRST|<n>] ────────────────────────────────────
    //
    // Move through the live cue's note BUILDS without changing the cue, so a
    // show-control system can reveal a speaker's notes on the same cue it uses
    // for everything else. The clicker does this on its own when a presenter
    // view asks for it; this is the same step for anybody driving from outside.
    if (command == "NOTESTEP") {
      const int deckIndex = project_.focusedDeckIndex;
      const Cue* live = activeCuePtr(deckIndex);
      if (!live) {
        failRemoteCommand("notestep: nothing live on this deck");
        return;
      }
      const int total = static_cast<int>(noteBuildParts(live->notes).size());
      if (total <= 1) {
        failRemoteCommand("notestep: this cue's notes have no builds "
                          "(separate them with a line of ---)");
        return;
      }
      const std::string sub = parts.size() < 2 ? std::string("NEXT")
                                               : toUpper(parts[1]);
      // Read-only, so a caller can ask where the speaker is without moving
      // them. Every other verb here has a STATUS form and this one did not,
      // which made the state unobservable except by changing it.
      if (sub == "STATUS") {
        remoteCommandDetail_ =
          "build " + std::to_string(presenterNoteStepFor(deckIndex) + 1) +
          " of " + std::to_string(total) +
          (presenterNoteMoreBelow(deckIndex) ? ", more below" : ", fully shown");
        return;
      }
      if (sub == "NEXT") {
        presenterNoteStepAdvance(deckIndex, 1);
      } else if (sub == "PREV" || sub == "PREVIOUS" || sub == "BACK") {
        presenterNoteStepAdvance(deckIndex, -1);
      } else if (sub == "FIRST" || sub == "RESET" || sub == "TOP") {
        presenterNoteStepSet(deckIndex, 0);
      } else if (sub == "LAST" || sub == "END") {
        presenterNoteStepSet(deckIndex, total - 1);
      } else if (sub == "SCROLL") {
        // Rows, signed. For anybody who wants the notes to creep rather than
        // to step -- a foot pedal, a fader, a cue stack of its own.
        const int rows = (parts.size() < 3) ? 1 : std::atoi(parts[2].c_str());
        presenterNoteScrollBy(deckIndex, rows);
        remoteCommandDetail_ = "scrolled " + std::to_string(rows) + " rows";
        return;
      } else {
        // 1-based over the wire, like the cue indices.
        const int asked = std::atoi(sub.c_str());
        if (asked < 1 || asked > total) {
          failRemoteCommand("notestep: build out of range 1-" +
                            std::to_string(total) + ", got " + sub);
          return;
        }
        presenterNoteStepSet(deckIndex, asked - 1);
      }
      remoteCommandDetail_ = "build " +
        std::to_string(presenterNoteStepFor(deckIndex) + 1) + " of " +
        std::to_string(total);
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
        // REFUSED rather than silently accepted. setVjBlend took any string,
        // and the renderer falls back to dissolve for a name it does not know
        // -- so a typo looked exactly like a mode that does nothing.
        const std::string want = toLower(parts[2]);
        if (!isVjBlendMode(want)) {
          std::string names;
          for (const auto& m : vjBlendModes()) {
            names += (names.empty() ? "" : " | ") + m;
          }
          failRemoteCommand("VJ BLEND: " + names);
          return;
        }
        setVjBlend(want);
        return;
      }
      if (sub == "BLEND") {
        // No argument cycles, matching every other mode control here.
        setVjBlend(vjBlendModeAfter(project_.vjBlendMode));
        remoteCommandDetail_ = project_.vjBlendMode;
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
      if (sub == "CLOCK") {
        // VJ CLOCK on|off|status -- follow an incoming MIDI clock instead of
        // the tapped tempo.
        const std::string arg = parts.size() >= 3 ? toUpper(parts[2]) : std::string("STATUS");
        if (arg == "ON" || arg == "OFF" || arg == "TOGGLE") {
          project_.midiClockSlave = (arg == "TOGGLE") ? !project_.midiClockSlave
                                                      : (arg == "ON");
          markProjectDirty();
          triggerToast(project_.midiClockSlave ? "tempo follows MIDI clock"
                                               : "tempo is tapped");
        }
        std::ostringstream state;
        state << (project_.midiClockSlave ? "slave" : "internal");
        if (midiClockBpm_ > 0.0) {
          state << " measured " << std::fixed << std::setprecision(1) << midiClockBpm_ << " bpm";
        }
        // Whether ticks are still arriving matters more than the last number:
        // a slaved deck on a dead cable would otherwise report a tempo it is
        // no longer being given.
        state << (midiClockAlive() ? " (receiving)" : " (no clock)");
        remoteCommandDetail_ = state.str();
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
                        "BPM <n>|CLOCK <on|off>|QUANTISE <on|off>|DECKS <a> <b>|STATUS");
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
        failRemoteCommand("SECTION: expected playback|metadata|geometry|key|audio|effects|timer|tone|synth");
        return;
      }
      const std::string which = toUpper(parts[1]);
      std::optional<QuickAction> action;
      if (which == "PLAYBACK")      action = QuickAction::CueSectionPlaybackToggle;
      else if (which == "METADATA") action = QuickAction::CueSectionMetadataToggle;
      else if (which == "GEOMETRY") action = QuickAction::CueSectionGeometryToggle;
      else if (which == "KEY")      action = QuickAction::CueSectionKeyToggle;
      else if (which == "AUDIO")    action = QuickAction::CueSectionAudioToggle;
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
      // ON has to reach a cue that has no text mode yet, so the on/off verbs
      // take any selected cue; everything else needs one that is showing.
      const std::string sub0 = parts.size() < 2 ? std::string("") : toUpper(parts[1]);
      const bool switching = (sub0 == "ON" || sub0 == "OFF" || sub0 == "TOGGLE");
      Cue* cue = switching ? selectedCueMutable() : selectedTextModeCueMutable();
      if (!cue) {
        failRemoteCommand(selectedCueMutable()
                            ? "ASCII: the selected cue has no text mode "
                              "(add the TEXT MODE effect, or select a video synth cue)"
                            : "ASCII: no cue selected");
        return;
      }
      const std::string sub = parts.size() < 2 ? std::string("") : toUpper(parts[1]);
      // Read the whole section back in one line.
      //
      // Every verb above SETS something and nothing reported, so the only way
      // to know what a cue was actually doing was to look at the screen --
      // which a surface cannot do, and which made every one of these verbs
      // untestable without a human watching. A control you cannot read back is
      // half a control.
      if (sub == "STATUS") {
        // THE EFFECTIVE VIEW, not the cue's raw fields. A cue carrying the
        // TEXT MODE effect takes its columns, glitch, character set and ink
        // from the effect's parameters, which overwrite the cue's own before
        // the renderer sees them -- so reading the cue reported a set and an
        // ink that were not the ones on screen. That gap is what made these
        // controls look dead in the first place; a readback that repeats it
        // would confirm the wrong answer instead of catching it.
        VideoSynthSettings vs = cue->videoSynth;
        const bool viaEffect = textModeEffectFor(*cue) != nullptr;
        if (const deckboy::effects::CueEffect* fx = textModeEffectFor(*cue)) {
          applyTextModeParams(*fx, vs);
        }
        std::string presetName = "none";
        for (const auto& p : glyphPresets()) {
          if (vs.asciiGlyphs == p.glyphs) { presetName = p.name; break; }
        }
        char line[512];
        std::snprintf(line, sizeof(line),
                      "on=%d via=%s ink=%s set=%s preset=%s custom=%d "
                      "cols=%d shuffle=%d chaos=%.2f wobble=%.2f wobblemode=%s font=%s",
                      viaEffect ? 1 : 0,
                      viaEffect ? "effect" : "cue", vsInkLabel(vs.asciiInk),
                      vsCharSetLabel(vs.asciiCharSet), presetName.c_str(),
                      vs.asciiGlyphs.empty() ? 0 : 1, vs.asciiCols, vs.asciiShuffle,
                      vs.asciiChaos, vs.asciiWobble,
                      vsWobbleModeLabel(vs.asciiWobbleMode),
                      vs.asciiFontPath.empty() ? "built-in" : "custom");
        remoteCommandDetail_ = line;
        return;
      }
      // Text mode itself had no verb at all, so the one mode an operator most
      // wants to flip mid-set could only be reached by clicking.
      if (sub == "ON" || sub == "OFF" || sub == "TOGGLE") {
        // Adds or removes the effect, which is the only switch now.
        const bool have = textModeEffectFor(*cue) != nullptr;
        const bool want = (sub == "TOGGLE") ? !have : (sub == "ON");
        if (want != have) {
          toggleTextModeEffect(*cue);
        }
        markProjectDirty();
        refreshAllLiveCueRuntimes();
        triggerToast(want ? "text mode on" : "text mode off");
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
      if (sub == "WOBBLE") {
        auto value = parseNumber(2);
        if (!value) {
          failRemoteCommand("ASCII WOBBLE: expected 0..1");
          return;
        }
        cue->videoSynth.asciiWobble = std::clamp(*value, 0.0, 1.0);
        markProjectDirty();
        triggerToast("wobble " + fmtFloat(cue->videoSynth.asciiWobble, 2));
        return;
      }
      if (sub == "WOBBLEMODE") {
        if (parts.size() >= 3) {
          // ANY name that was not "flow" or "hue" silently meant drift, and
          // the command still answered OK -- so asking for a mode that does
          // not exist looked exactly like getting the one you asked for.
          //
          // The aliases are the words the modes are described BY: flow
          // follows luma and hue follows colour, so those are what somebody
          // reaches for. Refusing them to insist on the internal name would
          // be pedantry; accepting them silently as drift was worse.
          const std::string want = toLower(parts[2]);
          int mode = -1;
          if (want == "drift" || want == "none" || want == "off")      mode = 0;
          else if (want == "flow" || want == "luma" ||
                   want == "luminance")                                 mode = 1;
          else if (want == "hue" || want == "colour" || want == "color") mode = 2;
          if (mode < 0) {
            failRemoteCommand("ASCII WOBBLEMODE: drift | flow (follows luma) "
                              "| hue (follows colour); no argument cycles");
            return;
          }
          cue->videoSynth.asciiWobbleMode = mode;
        } else {
          cue->videoSynth.asciiWobbleMode = (cue->videoSynth.asciiWobbleMode + 1) % 3;
        }
        markProjectDirty();
        triggerToast(std::string("wobble: ") +
                     vsWobbleModeLabel(cue->videoSynth.asciiWobbleMode));
        return;
      }
      if (sub == "PRESET") {
        if (parts.size() < 3) {
          std::string names;
          for (const auto& preset : glyphPresets()) {
            if (!names.empty()) names += ", ";
            names += preset.name;
          }
          remoteCommandDetail_ = names;
          triggerToast("presets: " + names);
          return;
        }
        const std::string want = toLower(joinParts(parts, 2));
        for (const auto& preset : glyphPresets()) {
          if (toLower(preset.name) == want) {
            cue->videoSynth.asciiGlyphs = preset.glyphs;
            markProjectDirty();
            triggerToast(std::string("glyphs: ") + preset.name);
            return;
          }
        }
        failRemoteCommand("ASCII PRESET: no set called \"" + joinParts(parts, 2) + "\"");
        return;
      }
      if (sub == "FONT") {
        if (parts.size() < 3) {
          cue->videoSynth.asciiFontPath.clear();
          markProjectDirty();
          triggerToast("font: automatic");
          return;
        }
        cue->videoSynth.asciiFontPath = joinParts(parts, 2);
        markProjectDirty();
        triggerToast("font: " + cue->videoSynth.asciiFontPath);
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
      failRemoteCommand(
        "ASCII: use ON|OFF|TOGGLE | STATUS | INK | SET | SHUFFLE | PRESET "
        "[name|next|prev] | FONT | GLYPHS [chars] | CHAOS <0-1> | "
        "WOBBLE <0-1> | WOBBLEMODE [drift|flow|hue] | COLS <n> | "
        "GLITCH <a> <b> <c> [d] | PHRASES <a|b|c> | HOLD <seconds>");
      return;
    }
    if (command == "AVJUMP") {
      // AVJUMP        -- how many times the playhead has jumped, per playlist
      // AVJUMP RESET  -- start counting again
      //
      // A jump is the playhead moving by something other than elapsed wall
      // time, with deliberate moves (take, seek, loop wrap, resume) excused.
      // It is what "the video freezes and then speeds up to catch up" IS.
      const bool reset = parts.size() > 1 && toUpper(parts[1]) == "RESET";
      std::string report;
      for (std::size_t d = 0; d < project_.decks.size(); ++d) {
        MediaEngine* engine = mediaEngineForDeck(static_cast<int>(d));
        if (!engine) continue;
        if (reset) {
          engine->resetAvJumps();
          engine->resetStalls();
          continue;
        }
        if (!report.empty()) report += " ";
        char buf[128];
        // STALLS FIRST. "The picture stopped" is what an operator reports;
        // the playhead jumping is the rarer and more technical of the two.
        std::snprintf(buf, sizeof(buf),
                      "deck%d stalls=%llu/worst%.3f/last%.3f "
                      "jumps=%llu/worst%.3f/last%.3f",
                      static_cast<int>(d + 1),
                      static_cast<unsigned long long>(engine->stallCount()),
                      engine->stallWorstSeconds(), engine->stallLastSeconds(),
                      static_cast<unsigned long long>(engine->avJumpCount()),
                      engine->avJumpWorstSeconds(), engine->avJumpLastSeconds());
        report += buf;
      }
      if (reset) {
        triggerToast("av jumps: counting from zero");
        return;
      }
      remoteCommandDetail_ = report.empty() ? "no engines" : report;
      triggerToast(remoteCommandDetail_);
      return;
    }
    if (command == "VMIX") {
      // VMIX                -- report
      // VMIX ON|OFF|TOGGLE  -- the surface
      // VMIX PORTS <http> <tcp>
      if (parts.size() < 2) {
        const std::string report =
          std::string(project_.vmixApiEnabled ? "on" : "off") +
          " http " + std::to_string(project_.vmixHttpPort) +
          " tcp " + std::to_string(project_.vmixTcpPort) +
          " inputs " + std::to_string(vmixInputs().size());
        remoteCommandDetail_ = report;
        triggerToast("vmix api: " + report);
        return;
      }
      const std::string sub = toUpper(parts[1]);
      if (sub == "ON")     { setVmixApiEnabled(true);  return; }
      if (sub == "OFF")    { setVmixApiEnabled(false); return; }
      if (sub == "TOGGLE") { setVmixApiEnabled(!project_.vmixApiEnabled); return; }
      if (sub == "PORTS") {
        if (parts.size() < 4) {
          failRemoteCommand("VMIX PORTS: expected an http port and a tcp port");
          return;
        }
        setVmixPorts(std::atoi(parts[2].c_str()), std::atoi(parts[3].c_str()));
        return;
      }
      failRemoteCommand("VMIX: expected ON, OFF, TOGGLE or PORTS");
      return;
    }
    if (command == "WATCH") {
      // WATCH                  -- report every playlist that is watching
      // WATCH <deck> <path>    -- point that playlist at a folder
      // WATCH <deck> OFF       -- stop
      if (parts.size() < 2) {
        std::string report;
        for (std::size_t i = 0; i < project_.decks.size(); ++i) {
          if (project_.decks[i].watchFolder.empty()) continue;
          if (!report.empty()) report += "; ";
          report += std::to_string(i + 1) + " " + project_.decks[i].watchFolder;
        }
        // The counters matter as much as the paths: a folder nobody is
        // scanning and a folder being scanned that is empty look identical
        // from the playlist, and only one of them is a fault.
        report += (report.empty() ? std::string() : std::string("; ")) +
                  "scans=" + std::to_string(watchFolderScanCount_) +
                  " seen=" + std::to_string(watchFolderLastSeen_) +
                  " taken=" + std::to_string(watchFolderTakenTotal_);
        remoteCommandDetail_ = report;
        triggerToast(report);
        return;
      }
      const int deckNumber = std::atoi(parts[1].c_str());
      if (deckNumber < 1 || deckNumber > static_cast<int>(project_.decks.size())) {
        failRemoteCommand("WATCH: there is no playlist " + parts[1]);
        return;
      }
      Deck& deck = project_.decks[static_cast<std::size_t>(deckNumber - 1)];
      if (parts.size() < 3) {
        remoteCommandDetail_ =
          deck.watchFolder.empty() ? "off" : deck.watchFolder;
        triggerToast(deck.watchFolder.empty() ? "not watching" : deck.watchFolder);
        return;
      }
      const std::string rest = joinParts(parts, 2);
      if (toUpper(rest) == "OFF") {
        deck.watchFolder.clear();
        markProjectDirty();
        triggerToast(deck.name + ": no longer watching a folder");
        return;
      }
      std::error_code ec;
      fs::path folder = fs::absolute(trim(rest), ec);
      // REFUSED, not stored. A watch folder pointed at nothing looks exactly
      // like a watch folder that does not work, and the difference only shows
      // up as files failing to arrive.
      if (ec || !fs::is_directory(folder, ec)) {
        failRemoteCommand("WATCH: not a folder: " + rest);
        return;
      }
      deck.watchFolder = folder.string();
      // Forget what this deck has already taken, so pointing it at a new
      // folder takes that folder's contents rather than skipping whatever
      // happens to share a name with something imported earlier.
      if (watchFolderSeen_.size() > static_cast<std::size_t>(deckNumber - 1)) {
        watchFolderSeen_[static_cast<std::size_t>(deckNumber - 1)].clear();
      }
      if (watchFolderSeeded_.size() > static_cast<std::size_t>(deckNumber - 1)) {
        watchFolderSeeded_[static_cast<std::size_t>(deckNumber - 1)] = false;
      }
      markProjectDirty();
      triggerToast(deck.name + " is watching " + folder.filename().string());
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
      // FX LFO <index> <slot> <on|off|shape> [value]
      //
      // The oscillators were reachable from the inspector and nowhere else, so
      // a controller could set a parameter but not hand it to an LFO. Slots are
      // 0-3 for the named parameters and 4 for the amount, matching the
      // inspector's own numbering.
      if (sub == "LFO") {
        if (parts.size() < 5) {
          failRemoteCommand("FX LFO wants <index> <slot> <on|off|shape> [value]");
          return;
        }
        const int index = std::atoi(parts[2].c_str()) - 1;
        // HELP says <A-E>, and this read the slot with atoi -- so "E" became
        // 0, the letter every caller following HELP would type landed on the
        // FIRST parameter instead of the amount, and it answered OK. Letters
        // and digits are both accepted now; anything else is refused.
        int slot = -1;
        {
          const std::string slotText = toUpper(parts[3]);
          if (slotText.size() == 1 && slotText[0] >= 'A' && slotText[0] <= 'E') {
            slot = slotText[0] - 'A';
          } else if (slotText.size() == 1 && slotText[0] >= '0' && slotText[0] <= '4') {
            slot = slotText[0] - '0';
          }
        }
        if (index < 0 || index >= static_cast<int>(cue->effects.size())) {
          failRemoteCommand("FX LFO: no effect " + parts[2]);
          return;
        }
        if (slot < 0) {
          failRemoteCommand("FX LFO: slot is A-D for the parameters or E for the amount (or 0-4)");
          return;
        }
        auto& lfo = cue->effects[static_cast<std::size_t>(index)].lfo[slot];
        const std::string what = toLower(parts[4]);
        if (what == "on" || what == "off") {
          lfo.on = (what == "on");
        } else if (what == "shape" && parts.size() >= 6) {
          const std::string want = toLower(parts[5]);
          bool found = false;
          for (int sh = 0; sh < static_cast<int>(deckboy::effects::LfoShape::Count); ++sh) {
            const auto candidate = static_cast<deckboy::effects::LfoShape>(sh);
            if (want == deckboy::effects::lfoShapeToken(candidate)) {
              lfo.shape = candidate;
              lfo.on = true;   // asking for a shape means wanting it to run
              found = true;
              break;
            }
          }
          if (!found) {
            failRemoteCommand("FX LFO: unknown shape \"" + parts[5] + "\"");
            return;
          }
        } else if (what == "rate" && parts.size() >= 6) {
          lfo.rateHz = std::clamp(static_cast<float>(std::atof(parts[5].c_str())),
                                  0.001f, 20.0f);
        } else if (what == "depth" && parts.size() >= 6) {
          lfo.depth = std::clamp(static_cast<float>(std::atof(parts[5].c_str())),
                                 0.0f, 1.0f);
        } else {
          failRemoteCommand("FX LFO: on, off, shape, rate or depth");
          return;
        }
        markProjectDirty();
        triggerToast("lfo: " + std::string(deckboy::effects::lfoShapeToken(lfo.shape))
                     + (lfo.on ? " on" : " off"));
        return;
      }
      if (sub == "ADD" && parts.size() >= 3) {
        const auto kind = deckboy::effects::cueEffectFromToken(toLower(parts[2]));
        if (kind == deckboy::effects::CueEffectKind::None) {
          failRemoteCommand("FX ADD: unknown effect \"" + parts[2] + "\"");
          return;
        }
        // REFUSE BEFORE ADDING, over the wire.
        //
        // Datamosh is a decode behaviour, not a per-pixel effect, so it only
        // works on file-backed video. The inspector handles a refusal by
        // adding the entry and bypassing it -- deliberately, so the operator
        // can SEE what was refused instead of watching their click vanish.
        // That reasoning needs a visible row, and a socket reply has none: a
        // caller got "ERR datamosh: file-backed video cues only" and a stack
        // that had silently grown an entry anyway. A command that reports
        // failure must not have changed anything.
        if (kind == deckboy::effects::CueEffectKind::Datamosh &&
            (cue->kind != CueKind::Video || cue->path.empty())) {
          failRemoteCommand("FX ADD: datamosh needs a file-backed video cue");
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
        // The SAME words the inspector shows. This echoed the raw 0-1 it had
        // just been handed, which tells the operator what they typed rather
        // than what it did: "glyph set 0.40" instead of "glyph set symbols".
        const std::string shown =
          inspEffectParamValueText(*cue, fx, slot, *slots[slot]);
        triggerToast(std::string(deckboy::effects::cueEffectParamLabel(fx.kind, slot)) +
                     " " + shown);
        remoteCommandDetail_ =
          std::string(deckboy::effects::cueEffectParamLabel(fx.kind, slot)) + " = " + shown;
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
        if (setLfoFromRemote(lfo, parts, 4, "FX LFO")) {
          markProjectDirty();
        }
        return;
      }
      failRemoteCommand("FX: use LIST | ADD <effect> [amount] | AMOUNT <n> <v> | "
                        "PARAM <n> <A-D> <0-1> | LFO <n> <A-E> ... | "
                        "COPY | PASTE | CLEAR");
      return;
    }
    if (command == "GEOLFO") {
      // GEOLFO                              -> which geometry is moving
      // GEOLFO <param> <on|off|shape|rate|depth|phase|sync|beats> [value]
      //   params: X Y WIDTH HEIGHT ROTATION CROPLEFT CROPRIGHT CROPTOP CROPBOTTOM
      // On the selected cue, like every other geometry verb.
      static const std::pair<const char*, int> kParams[] = {
        {"X", kGeoLfoOffsetX}, {"Y", kGeoLfoOffsetY},
        {"WIDTH", kGeoLfoScaleX}, {"W", kGeoLfoScaleX}, {"SIZE", kGeoLfoScaleX},
        {"HEIGHT", kGeoLfoScaleY}, {"H", kGeoLfoScaleY},
        {"ROTATION", kGeoLfoRotation}, {"ROT", kGeoLfoRotation},
        {"CROPLEFT", kGeoLfoCropLeft}, {"CROPRIGHT", kGeoLfoCropRight},
        {"CROPTOP", kGeoLfoCropTop}, {"CROPBOTTOM", kGeoLfoCropBottom},
      };
      static const char* const kNames[kGeoLfoCount] = {
        "x", "y", "width", "height", "rotation",
        "cropleft", "cropright", "croptop", "cropbottom",
      };
      Cue* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand("GEOLFO: select a cue first");
        return;
      }
      if (parts.size() < 2) {
        std::string moving;
        for (int slot = 0; slot < kGeoLfoCount; ++slot) {
          const auto& lfo = cue->geometryLfo[static_cast<std::size_t>(slot)];
          if (lfo.on) {
            if (!moving.empty()) moving += " | ";
            moving += std::string(kNames[slot]) + " " +
                      deckboy::effects::lfoShapeToken(lfo.shape);
          }
        }
        remoteCommandDetail_ = moving.empty() ? std::string("nothing moving") : moving;
        return;
      }
      const std::string wanted = toUpper(parts[1]);
      int slot = -1;
      for (const auto& param : kParams) {
        if (wanted == param.first) {
          slot = param.second;
          break;
        }
      }
      if (slot < 0) {
        failRemoteCommand("GEOLFO: x, y, width, height, rotation, cropleft, "
                          "cropright, croptop or cropbottom");
        return;
      }
      if (setLfoFromRemote(cue->geometryLfo[static_cast<std::size_t>(slot)], parts, 2,
                           "GEOLFO")) {
        markProjectDirty();
        remoteCommandDetail_ = std::string(kNames[slot]) +
          (cue->geometryLfo[static_cast<std::size_t>(slot)].on ? " moving" : " still");
      }
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
    // What the selected AUDIO cue draws while it plays.
    //
    // Named modes, not an index: a surface button that sends "3" would follow
    // the enum if a mode were ever inserted, and silently start selecting a
    // different picture on every show that used it.
    // MESH <on|off|toggle|height|tilt|yaw|spin|grid> [value]
    //
    // The one cue property that changes HOW the picture is drawn rather than
    // what is in it: the quad becomes a displaced grid. Named sub-verbs, not
    // an index, for the same reason AUDIOVIS uses names.
    // CLICK <x> <y> -- a development verb, not part of the operator surface.
    //
    // Drives the real mouse-down chain at a window coordinate. Synthetic
    // OS clicks cannot be relied on here: SetForegroundWindow fails
    // silently unless the calling process holds foreground rights, so a
    // scripted click can produce a run where nothing happened and no error
    // either -- indistinguishable from a real bug, and it cost several
    // rounds of chasing one that was not there. This enters at
    // handleMouseDown, so everything that decides WHICH control was hit is
    // exercised, which is the layer those bugs live in.
    if (command == "CLICK") {
      if (parts.size() < 3) {
        failRemoteCommand("CLICK: expected x y");
        return;
      }
      try {
        // A CONTEXT MENU IS A SEPARATE DISPATCH, and entering at
        // handleMouseDown skipped it entirely.
        //
        // The real event path (app_update.ipp) branches on contextMenuOpen_
        // BEFORE handleMouseDown, so a click while a menu is up goes to
        // handleContextMenuClick and never reaches the hit-testing below.
        // CLICK called handleMouseDown directly, which meant it could OPEN a
        // menu -- that button lives in handleMouseDown -- and could never
        // choose anything from one. Everything reachable only through a menu
        // was therefore unreachable over the wire: eleven cue types behind
        // SOURCE alone, none of which could be created in a scripted test.
        //
        // Mirroring the branch makes CLICK more faithful to the real path,
        // not less, which is the whole reason it enters at the event layer.
        if (contextMenuOpen_) {
          handleContextMenuClick(std::stoi(parts[1]), std::stoi(parts[2]));
        } else {
          handleMouseDown(std::stoi(parts[1]), std::stoi(parts[2]), SDL_BUTTON_LEFT);
        }
        // Reports WHICH modal was up afterwards. A click that lands on a
        // covered control is silently swallowed by whatever claimed it first,
        // and without this the only symptom is "the button does nothing".
        remoteCommandDetail_ = "clicked " + parts[1] + "," + parts[2]
          + " [menu=" + std::to_string(contextMenuOpen_ ? 1 : 0)
          + " splash=" + std::to_string(showSplashOverlay_ ? 1 : 0)
          + " startup=" + std::to_string(showStartupDialog_ ? 1 : 0)
          + " dash=" + std::to_string(dashboardOverlayOpen_ ? 1 : 0)
          + " dashbtns=" + std::to_string(dashButtons_.size())
          + " editor=" + std::to_string(inlineEditor_.open ? 1 : 0)
          + " drop=" + std::to_string(dropdown_.open ? 1 : 0) + "]";
      } catch (...) {
        failRemoteCommand("CLICK: expected two numbers");
      }
      return;
    }
    if (command == "MIDICLOCK") {
      // DEV VERB, like CLICK and HOVER. Feeds the clock follower a run of
      // ticks at a known spacing so the tempo maths can be checked without a
      // MIDI cable, a virtual port, or a controller on the desk. It drives the
      // SAME function the real clock drives -- a test that exercised a copy of
      // the maths would prove nothing about the copy that ships.
      if (parts.size() < 3 || toUpper(parts[1]) != "SIM") {
        failRemoteCommand("MIDICLOCK: expected SIM <bpm> [beats]");
        return;
      }
      double bpm = 0.0;
      int beats = 4;
      try {
        bpm = std::stod(parts[2]);
        if (parts.size() > 3) {
          beats = std::clamp(std::stoi(parts[3]), 1, 64);
        }
      } catch (...) {
        failRemoteCommand("MIDICLOCK SIM: expected a number");
        return;
      }
      if (bpm < 20.0 || bpm > 600.0) {
        failRemoteCommand("MIDICLOCK SIM: bpm must be 20..600");
        return;
      }
      // Synthetic timestamps: a beat is 60000/bpm ms, and 24 ticks divide it.
      const double beatMs = 60000.0 / bpm;
      Uint64 stamp = SDL_GetTicks();
      onMidiRealtime(0xFA, stamp);
      for (int beat = 0; beat < beats; ++beat) {
        for (int tick = 1; tick <= 24; ++tick) {
          const double at = beatMs * beat + (beatMs * tick) / 24.0;
          onMidiRealtime(0xF8, stamp + static_cast<Uint64>(std::llround(at)));
        }
      }
      std::ostringstream out;
      out << "fed " << beats << " beats at " << std::fixed << std::setprecision(1)
          << bpm << "; measured " << std::setprecision(2) << midiClockBpm_
          << "; vj tempo " << std::setprecision(2) << project_.vjTempoBpm;
      remoteCommandDetail_ = out.str();
      return;
    }
    if (command == "HOVER") {
      // CLICK's sibling. Hover state drives the tips and the mascot's advice,
      // and none of that could be exercised without a way to put the pointer
      // somewhere -- a test that cannot move the mouse cannot test a tooltip.
      if (parts.size() < 3) {
        failRemoteCommand("HOVER: expected x y");
        return;
      }
      try {
        // BOTH halves, exactly as SDL_EVENT_MOUSE_MOTION does them: the event
        // handler sets mouseX_/mouseY_ and THEN calls handleMouseMotion, which
        // only deals with drags. Calling the handler alone moved nothing, so
        // every hover reported "no tip" while looking like it had worked.
        mouseX_ = std::stoi(parts[1]);
        mouseY_ = std::stoi(parts[2]);
        handleMouseMotion(mouseX_, mouseY_);
        remoteCommandDetail_ = "hover " + parts[1] + "," + parts[2]
          + (hoverTipLast_.empty() ? std::string(" [no tip]")
                                   : (" [" + hoverTipLast_ + "]"));
      } catch (...) {
        failRemoteCommand("HOVER: expected two numbers");
      }
      return;
    }
    if (command == "MESH" || command == "MESH3D") {
      Cue* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand("MESH: no cue selected");
        return;
      }
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string("STATUS");
      auto number = [&](int at, float lo, float hi, float& into) {
        if (parts.size() <= static_cast<std::size_t>(at)) return false;
        try {
          into = std::clamp(std::stof(parts[at]), lo, hi);
          return true;
        } catch (...) { return false; }
      };
      if (sub == "STATUS") {
        std::ostringstream s;
        s << (cue->meshEnabled ? "on" : "off")
          << " height=" << cue->meshHeight
          << " tilt=" << cue->meshTiltX
          << " yaw=" << cue->meshTiltY
          << " spin=" << cue->meshSpin
          << " grid=" << cue->meshGrid;
        remoteCommandDetail_ = s.str();
        return;
      }
      if (sub == "ON" || sub == "OFF" || sub == "TOGGLE") {
        cue->meshEnabled = (sub == "TOGGLE") ? !cue->meshEnabled : (sub == "ON");
        markProjectDirty();
        triggerToast(cue->meshEnabled ? "mesh on" : "mesh off");
        return;
      }
      if (sub == "HEIGHT" && number(2, 0.0f, 1.0f, cue->meshHeight)) {
        markProjectDirty();
        return;
      }
      if (sub == "TILT" && number(2, -1.0f, 1.0f, cue->meshTiltX)) {
        markProjectDirty();
        return;
      }
      if (sub == "YAW" && number(2, -1.0f, 1.0f, cue->meshTiltY)) {
        markProjectDirty();
        return;
      }
      if (sub == "SPIN" && number(2, 0.0f, 1.0f, cue->meshSpin)) {
        markProjectDirty();
        return;
      }
      if (sub == "GRID" && parts.size() > 2) {
        try {
          cue->meshGrid = std::clamp(std::stoi(parts[2]), 8, 160);
          markProjectDirty();
          return;
        } catch (...) {}
      }
      failRemoteCommand("MESH: expected on|off|toggle|height|tilt|yaw|spin|grid");
      return;
    }
    if (command == "AUDIOVIS" || command == "AUDIOVISUAL") {
      Cue* cue = selectedCueMutable();
      if (!cue || cue->kind != CueKind::Audio) {
        failRemoteCommand("no audio cue selected");
        return;
      }
      if (parts.size() < 2) {
        remoteCommandDetail_ = audioVisualToken(cue->audioVisual);
        return;
      }
      const std::string want = toLower(parts[1]);
      // parseAudioVisualToken falls back to waveform for anything it does not
      // know, which is right when READING A SHOW FILE and wrong here: a surface
      // sending a misspelled mode must be told, not quietly given the default.
      if (want != audioVisualToken(AudioVisual::Waveform) &&
          parseAudioVisualToken(want) == AudioVisual::Waveform) {
        failRemoteCommand("expected waveform|scope|lissajous|spectrum|level|cover");
        return;
      }
      const AudioVisual mode = parseAudioVisualToken(want);
      bool any = forEachFocusedSelectedCueMutable([&](Cue& each, int) {
        if (each.kind == CueKind::Audio) {
          each.audioVisual = mode;
        }
      });
      if (!any) {
        failRemoteCommand("no audio cue selected");
        return;
      }
      markProjectDirty();
      triggerToast("audio visual: " + audioVisualLabel(mode));
      return;
    }
    // TWO VERBS WERE CALLED "OVERLAY" AND THE FIRST ONE WON.
    //
    // This is the time-code overlay toggle. Further down there is a second
    // `command == "OVERLAY"` for the cue overlay bin -- PUSH, POP, CLEAR --
    // and it was unreachable: every OVERLAY message stopped here. Worse than
    // dead, it was WRONG. "OVERLAY PUSH 3" is not a toggle word, so it fell
    // into the no-argument branch and flipped the timecode burn-in on air,
    // answering OK for a cue overlay it never pushed.
    //
    // This one now claims only what is its own -- a bare OVERLAY, or one
    // followed by a toggle word -- and anything else falls through to the
    // cue overlay handler that was always meant to have it.
    // ONE BRANCH FOR ONE VERB.
    //
    // OVERLAY used to be tested twice at the top level: this timecode
    // burn-in toggle, and a second branch for the cue overlay bin. The
    // first won, so PUSH/POP/CLEAR were unreachable -- and "OVERLAY PUSH 3"
    // is not a toggle word, so it fell into the no-argument branch and
    // flipped the burn-in on air while answering OK.
    //
    // Both live here now, so there is no order to get wrong.
    if (command == "OVERLAY" || command == "TIMEOVERLAY") {
      const std::string overlaySub =
        (command == "OVERLAY" && parts.size() > 1) ? toUpper(parts[1]) : std::string();
      if (overlaySub == "PUSH" || overlaySub == "POP" || overlaySub == "CLEAR") {
        std::string sub = toUpper(parts[1]);
        if (sub == "CLEAR") { clearOverlay(); return; }
        if (sub == "POP")   { popOverlay();   return; }
        if (sub == "PUSH") {
          if (parts.size() < 3) {
            failRemoteCommand("PUSH expects a cue number");
            return;
          }
          int idx = 0;
          try {
            idx = std::stoi(parts[2]) - 1;  // 1-based
          } catch (...) {
            failRemoteCommand("PUSH expects a cue number, got \""
                              + parts[2] + "\"");
            return;
          }
          Deck& deck = focusedDeckMutable();
          if (idx < 0 || idx >= static_cast<int>(deck.cues.size())) {
            failRemoteCommand("no cue " + parts[2] + " to push");
            return;
          }
          auto& ov = deck.overlayActiveIndices;
          if (std::find(ov.begin(), ov.end(), idx) != ov.end()) {
            // Not a failure -- it is already up. Said out loud so a surface is
            // not left wondering whether the press registered.
            remoteCommandDetail_ = deck.cues[idx].name + " is already an overlay";
            return;
          }
          if (ov.size() >= 4) ov.erase(ov.begin());
          ov.push_back(idx);
          triggerToast("overlay pushed: " + deck.cues[idx].name);
          markProjectDirty();
          return;
        }
        return;
        return;
      }
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
      // DELETE ANSWERS HONESTLY. Deleting a cue that is on air asks for a
      // second DELETE to confirm, and activeIndex never clears on a stop --
      // so a cue that has ever been taken keeps asking. Replying OK to a
      // delete that did not happen makes that indistinguishable from one that
      // did, which is the exact failure `failRemoteCommand` exists for.
      const int deckIndex = project_.focusedDeckIndex;
      const std::size_t before =
        deckIndex >= 0 && deckIndex < static_cast<int>(project_.decks.size())
          ? project_.decks[deckIndex].cues.size() : 0;
      deleteSelected();
      const std::size_t after =
        deckIndex >= 0 && deckIndex < static_cast<int>(project_.decks.size())
          ? project_.decks[deckIndex].cues.size() : 0;
      if (after == before) {
        failRemoteCommand("DELETE: nothing deleted - send DELETE again to "
                          "confirm a cue that is on air");
        return;
      }
      remoteCommandDetail_ = "deleted " + std::to_string(before - after) + " cue(s)";
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
        // The answer goes to the CALLER as well as the screen: a query
        // whose reply is a bare OK has not answered.
        remoteCommandDetail_ = "cue id: " + (cue->cueId.empty() ? std::string("(none)") : cue->cueId);
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
        // A QUERY ANSWERS THE CALLER. This toasted the number onto the screen
        // and sent back a bare OK, so the one thing a remote asking "what is
        // the opacity" wanted was the one thing it could not have.
        int pct = static_cast<int>(std::lround(std::clamp(focusedDeck().playlistOpacity, 0.0f, 1.0f) * 100.0f));
        triggerToast("deck opacity: " + std::to_string(pct) + "%");
        remoteCommandDetail_ = std::to_string(pct) + "%";
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
        // SAY SO WHEN IT IS NOT A STYLE. An unknown token reads as a
        // crossfade, which is the right default for an old show file and a
        // silent wrong answer on the wire -- the operator asks for a push,
        // gets a dissolve, and nothing anywhere says why.
        bool recognised = false;
        const TransitionStyle style =
          parseTransitionStyleToken(parts[1], &recognised);
        if (!recognised) {
          failRemoteCommand("transitionstyle: unknown style \"" + parts[1] +
                            "\" -- try cut, crossfade, dip, dipwhite, "
                            "push-left/right/up/down, wipe-left/right/up/down, iris, portal");
          return;
        }
        setTransitionStyle(style);
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
        // The answer goes to the CALLER as well as the screen: a query
        // whose reply is a bare OK has not answered.
        remoteCommandDetail_ = "tc " + formatTimecode(focusedDeck().timecodeCurrentSeconds, focusedDeck().timecodeFps);
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
    // RENAME <text> -- rename the selected cue on the focused deck. Blank
    // restores the file name, matching the menu entry.
    //
    // Worth having over the wire and not only in the menu: a show that builds
    // its playlist from a controller wants to label what it just made, and
    // until now nothing outside the app could name anything.
    if (command == "RENAME") {
      Deck& deck = focusedDeckMutable();
      const int idx = deck.selectedIndex;
      if (idx < 0 || idx >= static_cast<int>(deck.cues.size())) {
        failRemoteCommand("RENAME: no cue selected");
        return;
      }
      Cue& target = deck.cues[idx];
      const std::string wanted = parts.size() > 1 ? trim(joinParts(parts, 1)) : std::string();
      if (wanted.empty()) {
        const fs::path p = fs::path(target.path);
        target.name = p.has_stem() ? p.stem().string() : target.path;
      } else {
        target.name = wanted;
      }
      markProjectDirty();
      triggerToast("renamed: " + target.name);
      return;
    }
    if (command == "PATTERN") {
      if (parts.size() > 1) {
        std::string sub = toUpper(parts[1]);
        if (sub == "LIST") {
          triggerToast("patterns: " + std::to_string(patternTypes().size()) + " types");
          return;
        }
        // ADD a pattern cue to the focused deck. PATTERN SET changes the cue
        // that is already there; there was no way to make one, which meant a
        // controller could reach every generator in the program except by
        // asking somebody to click first.
        if (sub == "ADD") {
          if (parts.size() < 3) {
            failRemoteCommand("PATTERN ADD needs a pattern id");
            return;
          }
          addPatternCue(joinParts(parts, 2));
          return;
        }
        if (sub == "SET") {
          std::string typeId = parts.size() > 2 ? normalizePatternTypeId(joinParts(parts, 2)) : "";
          if (typeId == "checker") {
            typeId = "checkerboard";
          }
          if (typeId.empty() || !isKnownPatternType(typeId)) {
            // Same OK-that-wasn't-true as PATTERN ADD had, and name the id
            // rather than saying "invalid" about something the caller cannot
            // see from the reply.
            failRemoteCommand(typeId.empty()
              ? "PATTERN SET needs a pattern id"
              : ("pattern default: \"" + typeId + "\" is not a pattern type"));
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
        command == "CAMERACUE" || command == "SYPHONCUE" || command == "SPOUTCUE" ||
        command == "NDICUE") {
      CueKind kind = CueKind::WindowSource;
      size_t refStartIndex = 1;
      if (command == "CAMERACUE") {
        kind = CueKind::Camera;
      } else if (command == "SYPHONCUE" || command == "SPOUTCUE") {
        kind = CueKind::Syphon;
      } else if (command == "NDICUE") {
        // NDI was the one live-source kind with no verb: addNdiSourceCue
        // existed and could only be reached from a prompt in the UI, so an NDI
        // input could not be added from a surface -- or tested without a human
        // typing into a dialog.
        kind = CueKind::NdiSource;
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
        } else if (typeArg == "NDI") {
          kind = CueKind::NdiSource;
          refStartIndex = 2;
        }
      }
      std::string sourceRef = parts.size() > refStartIndex ? joinParts(parts, refStartIndex) : "";

      // A SOURCE WITH NO REFERENCE OPENS A PICKER, AND A PICKER CANNOT BE
      // ANSWERED OVER A SOCKET. addSourceCue asks which window or which camera
      // when it is not told, which is right for an operator and useless for a
      // controller: the dropdown opens on the desk, this command answers OK,
      // and no cue is ever made. Measured with SOURCE CAMERA over the port --
      // "OK SOURCE", then a deck with no cue on it at all.
      //
      // So the caller is told what to name, and given the list to choose from.
      {
        const std::string refLower = toLower(trim(sourceRef));
        const bool wantsPicker =
          (kind == CueKind::Camera &&
           (refLower.empty() || refLower == "default-camera" || refLower == "default")) ||
          (kind == CueKind::WindowSource &&
           (refLower.empty() || refLower == "active-window"));
        if (wantsPicker) {
          std::string reason = (kind == CueKind::Camera)
            ? "SOURCE CAMERA: name the device"
            : "SOURCE WINDOW: name the window, e.g. title:Firefox (or \"desktop\")";
          std::vector<std::string> choices;
          if (kind == CueKind::WindowSource) {
            for (const auto& window : deckboy::platform::listCaptureWindows()) {
              choices.push_back(window.displayName);
            }
          }
#ifdef _WIN32
          if (kind == CueKind::Camera) {
            choices = listDshowVideoDevices();
          }
#endif
          if (!choices.empty()) {
            reason += " -- available:";
            for (std::size_t i = 0; i < choices.size() && i < 8; ++i) {
              reason += (i == 0 ? " \"" : ", \"") + choices[i] + "\"";
            }
          }
          failRemoteCommand(reason);
          return;
        }
      }

      // NDI has its OWN builder, and it is not interchangeable: an NDI cue's
      // path is "ndi://<name>" while addSourceCue writes "source://<kind>/...".
      // Routing it through the general one would have produced a cue that
      // looked right in the list and resolved to nothing.
      if (kind == CueKind::NdiSource) {
        addNdiSourceCue(sourceRef);
      } else {
        addSourceCue(kind, sourceRef);
      }
      return;
    }
    if (command == "STILLDUR" || command == "DURATION") {
      // FOUR WAYS THIS USED TO ANSWER "OK" HAVING DONE NOTHING: no argument, an
      // argument that is not a number, no cue selected, and a cue this does not
      // apply to. Each returned through a swallowed exception or a false `if`,
      // and the caller was told it had worked -- the exact silent success the
      // remote contract exists to stop.
      if (parts.size() < 2) {
        failRemoteCommand("expected a number of seconds");
        return;
      }
      double dur = 0.0;
      try {
        dur = std::stod(parts[1]);
      } catch (...) {
        failRemoteCommand("expected a number of seconds, got \"" + parts[1] + "\"");
        return;
      }
      Cue* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand("no cue selected");
        return;
      }
      if (cue->kind == CueKind::Video) {
        failRemoteCommand("a video cue's duration comes from its file; "
                          "still duration applies to stills, patterns and browsers");
        return;
      }
      cue->stillDurationSeconds = std::max(0.0, dur);
      triggerToast(cue->stillDurationSeconds > 0.0
        ? "still dur " + formatSeconds(cue->stillDurationSeconds)
        : "still dur: hold");
      markProjectDirty();
      return;
    }
    if (command == "GRAPHIC" || command == "LOWERTHIRD") {
      // The lower third the SOURCE menu makes: a text cue laid out as one,
      // fired from its own playlist over another. TEXTCUE TITLE / SUB / LOOK
      // / IN / OUTMOVE / OUT drive it from there.
      addLowerThirdTextCue();
      remoteCommandDetail_ = "lower third added to " + focusedDeckLabel();
      return;
    }
    if (command == "PIP") {
      // Same again: addPipCue was complete, with its own overlay decoder
      // runtime, and nothing could call it.
      addPipCue();
      remoteCommandDetail_ = "pip added to " + focusedDeckLabel();
      return;
    }
    // MULTIVIEW WAS AN ALIAS HERE, on a PARKED cue kind that answers "not
    // built yet" -- and the name now belongs to the monitor grid, which is
    // both the industry meaning of the word and a thing that exists. This
    // branch keeps its own two names. Caught by audit_remote_help, which saw
    // two branches for one verb and said the second could never run.
    if (command == "COMPOSITE" || command == "SCENE") {
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
    if (command == "LOWERSTYLE") {
      // LOWERSTYLE                 -> how this lower third comes on
      // LOWERSTYLE NEXT            -> step to the next move
      // LOWERSTYLE <name>          -> cut | fade | left | right | rise |
      //                               wipe | grow | typewriter | pop
      //
      // Acts on the TEXT-CUE lower third, which is what the SOURCE menu and
      // the LOWERTHIRD verb make. It used to require CueKind::LowerThird --
      // the other, older overlay kind -- so it answered "select a lower third
      // first" about the cue you had just created. TEXTCUE IN / OUTMOVE are
      // the fuller controls; these two stay because they are documented and
      // scripts may already use them.
      Cue* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand("LOWERSTYLE: select a cue first");
        return;
      }
      const bool designed = cue->lowerThird.on;
      const bool legacy = cue->kind == CueKind::LowerThird;
      if (!designed && !legacy) {
        failRemoteCommand("LOWERSTYLE: this cue is not a lower third");
        return;
      }
      if (parts.size() < 2) {
        remoteCommandDetail_ = designed
          ? lowerThirdMoveLabel(cue->lowerThird.moveIn)
          : lowerThirdStyleLabel(cue->lowerThirdStyle);
        return;
      }
      if (designed) {
        const std::string arg = toUpper(parts[1]);
        if (arg == "NEXT") {
          const int count = static_cast<int>(LowerThirdMove::Count);
          cue->lowerThird.moveIn = static_cast<LowerThirdMove>(
            (static_cast<int>(cue->lowerThird.moveIn) + 1) % count);
        } else {
          // NAMED OR REFUSED. An unknown name quietly becoming `cut` would be
          // a move that does nothing and answers OK.
          const std::string token = toLower(parts[1]);
          const LowerThirdMove picked = lowerThirdMoveFromToken(token);
          if (picked == LowerThirdMove::None && token != "cut") {
            failRemoteCommand("LOWERSTYLE: no move called '" + parts[1] + "'");
            return;
          }
          cue->lowerThird.moveIn = picked;
        }
        markProjectDirty();
        remoteCommandDetail_ = lowerThirdMoveLabel(cue->lowerThird.moveIn);
        return;
      }
      // The legacy overlay kind, for a show that still has one.
      const std::string arg = toUpper(parts[1]);
      if (arg == "NEXT") {
        cue->lowerThirdStyle =
          (cue->lowerThirdStyle + 1) % kLowerThirdStyleCount;
      } else {
        const int picked = lowerThirdStyleFromToken(toLower(parts[1]));
        if (picked == 0 && toLower(parts[1]) != "none") {
          failRemoteCommand("LOWERSTYLE: expected none, fade, rise, slide, "
                            "wipe or NEXT");
          return;
        }
        cue->lowerThirdStyle = picked;
      }
      markProjectDirty();
      remoteCommandDetail_ = lowerThirdStyleLabel(cue->lowerThirdStyle);
      return;
    }
    if (command == "LOWERANIM") {
      // LOWERANIM [<seconds>] -- how long the move takes, each way.
      Cue* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand("LOWERANIM: select a cue first");
        return;
      }
      const bool designed = cue->lowerThird.on;
      const bool legacy = cue->kind == CueKind::LowerThird;
      if (!designed && !legacy) {
        failRemoteCommand("LOWERANIM: this cue is not a lower third");
        return;
      }
      if (parts.size() < 2) {
        remoteCommandDetail_ = formatSeconds(
          designed ? cue->lowerThird.inSeconds : cue->lowerThirdAnimSeconds);
        return;
      }
      auto seconds = parseNumber(1);
      if (!seconds) {
        failRemoteCommand("LOWERANIM: '" + parts[1] + "' is not a number");
        return;
      }
      const double value = std::clamp(*seconds, 0.0, 5.0);
      if (designed) {
        // BOTH WAYS, which is what this verb has always meant. TEXTCUE IN and
        // TEXTCUE OUT set them apart when that is wanted.
        cue->lowerThird.inSeconds = value;
        cue->lowerThird.outSeconds = value;
      } else {
        cue->lowerThirdAnimSeconds = value;
      }
      markProjectDirty();
      remoteCommandDetail_ = formatSeconds(value);
      return;
    }
    if (command == "LOWERALPHA") {
      // Same four silent paths as STILLDUR had.
      if (parts.size() < 2) {
        failRemoteCommand("expected an alpha, 0-255");
        return;
      }
      int alpha = 0;
      try {
        alpha = std::stoi(parts[1]);
      } catch (...) {
        failRemoteCommand("expected an alpha 0-255, got \"" + parts[1] + "\"");
        return;
      }
      Cue* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand("no cue selected");
        return;
      }
      if (cue->kind != CueKind::LowerThird) {
        failRemoteCommand("the background alpha belongs to a lower third");
        return;
      }
      cue->lowerThirdBgAlpha = std::clamp(alpha, 0, 255);
      triggerToast("overlay alpha " + std::to_string(cue->lowerThirdBgAlpha));
      markProjectDirty();
      return;
    }
    if (command == "CLEAROVERLAY") {
      clearOverlay();
      return;
    }
    if (command == "BROWSER") {
      // BROWSER <url> still makes a cue, because that is what it has always
      // done. The sub-verbs drive the page that is already live on the focused
      // deck -- scrolling it, clicking through whatever it has put in the way,
      // and going back if that was the wrong link.
      const std::string first = parts.size() > 1 ? toUpper(parts[1]) : std::string();
      const bool isSubVerb =
        first == "SCROLL" || first == "CLICK" || first == "SCROLLBAR" ||
        first == "BACK" || first == "FORWARD" || first == "RELOAD" ||
        first == "URL" || first == "TOP" || first == "BOTTOM" ||
        first == "INTERACT" || first == "TYPE" || first == "KEY";
      if (!isSubVerb) {
        std::string url = joinParts(parts, 1);
        if (!url.empty()) {
          addBrowserCue(url);
        }
        return;
      }

      deckboy::platform::browser::BrowserRenderer* page = liveBrowserRenderer();
      if (!page) {
        failRemoteCommand("BROWSER: no browser cue is live on this deck");
        return;
      }
      if (first == "SCROLL") {
        // One argument scrolls vertically, which is what a page needs 99 times
        // in 100; two scroll both ways.
        int dy = 0;
        int dx = 0;
        try {
          if (parts.size() > 2) {
            dy = std::stoi(parts[2]);
          } else {
            failRemoteCommand("BROWSER SCROLL: expected pixels (negative is up)");
            return;
          }
          if (parts.size() > 3) {
            dx = dy;
            dy = std::stoi(parts[3]);
          }
        } catch (...) {
          failRemoteCommand("BROWSER SCROLL: expected a number");
          return;
        }
        // SAY SO WHEN THE BACKEND CANNOT DO IT.
        //
        // These are delivered as JavaScript, and only the Windows WebView2
        // backend implements executeJavaScript today -- the Linux Xvfb path
        // and the macOS scaffold both return false. Reporting OK for a command
        // that did nothing is the exact failure this codebase keeps finding;
        // an honest ERR is worth more than a tidy reply.
        if (!page->scrollBy(dx, dy)) {
          failRemoteCommand("BROWSER SCROLL: this browser backend cannot drive the page");
          return;
        }
        remoteCommandDetail_ = "scrolled " + std::to_string(dy);
        return;
      }
      if (first == "TOP" || first == "BOTTOM") {
        if (!page->scrollBy(0, first == "TOP" ? -100000 : 100000)) {
          failRemoteCommand("BROWSER: this browser backend cannot drive the page");
        }
        return;
      }
      if (first == "CLICK") {
        // Fractions of the frame, so a Companion button or a click in the
        // preview means the same thing at any raster size.
        double fx = 0.5;
        double fy = 0.5;
        try {
          if (parts.size() > 3) {
            fx = std::stod(parts[2]);
            fy = std::stod(parts[3]);
          }
        } catch (...) {
          failRemoteCommand("BROWSER CLICK: expected two fractions 0..1");
          return;
        }
        if (fx < 0.0 || fx > 1.0 || fy < 0.0 || fy > 1.0) {
          failRemoteCommand("BROWSER CLICK: fractions must be 0..1");
          return;
        }
        if (!page->clickAtFraction(fx, fy)) {
          failRemoteCommand("BROWSER CLICK: this browser backend cannot drive the page");
          return;
        }
        return;
      }
      if (first == "SCROLLBAR") {
        const bool show = parts.size() > 2 && toUpper(parts[2]) == "ON";
        if (!page->setScrollbarsVisible(show)) {
          failRemoteCommand("BROWSER SCROLLBAR: this browser backend cannot drive the page");
          return;
        }
        project_.browserScrollbars = show;
        markProjectDirty();
        remoteCommandDetail_ = show ? "scrollbar shown" : "scrollbar hidden";
        return;
      }
      if (first == "TYPE") {
        // Types into whatever the page has focused. The reason this exists is
        // logging in: a click can dismiss a banner, but only a keystroke gets
        // past a sign-in form.
        const std::string text = joinParts(parts, 2);
        if (text.empty()) {
          failRemoteCommand("BROWSER TYPE: expected some text");
          return;
        }
        if (!page->sendText(text)) {
          failRemoteCommand("BROWSER TYPE: this browser backend cannot type");
          return;
        }
        remoteCommandDetail_ = std::to_string(text.size()) + " characters";
        return;
      }
      if (first == "KEY") {
        const std::string name = parts.size() > 2 ? parts[2] : std::string();
        if (name.empty()) {
          failRemoteCommand("BROWSER KEY: expected Enter|Tab|Backspace|Escape");
          return;
        }
        if (!page->sendKey(name)) {
          failRemoteCommand("BROWSER KEY: this backend cannot send \"" + name + "\"");
          return;
        }
        return;
      }
      if (first == "INTERACT") {
        // The hand icon, as a verb. Shows the real browser window so the
        // operator can click, scroll, type and LOG IN -- the things no
        // synthetic event can do.
        const bool want = parts.size() > 2 ? (toUpper(parts[2]) == "ON")
                                           : !page->isInteractive();
        if (!page->setInteractive(want)) {
          failRemoteCommand("BROWSER INTERACT: this browser backend has no hands-on mode");
          return;
        }
#if defined(__linux__)
        remoteCommandDetail_ = want ? "hands-on: clicks and keys go to the page"
                                    : "hands-on off";
#else
        remoteCommandDetail_ = want ? "browser window shown - click, type, log in"
                                    : "browser window hidden";
#endif
        return;
      }
      if (first == "BACK") {
        page->goBack();
        return;
      }
      if (first == "FORWARD") {
        page->goForward();
        return;
      }
      if (first == "RELOAD") {
        page->reload();
        return;
      }
      if (first == "URL") {
        const std::string url = joinParts(parts, 2);
        if (url.empty()) {
          failRemoteCommand("BROWSER URL: expected an address");
          return;
        }
        page->loadUrl(url);
        remoteCommandDetail_ = url;
        return;
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
          // IT CAN ACT NOW. This used to refuse with "per-deck output
          // routing is not available yet" -- correctly, because every output
          // composited the programme whatever it was told. Super Deckboy is
          // that routing, so the refusal is gone and the verb does what its
          // name has always said.
          if (!assignFocusedDeckToFocusedOutput(layer)) {
            failRemoteCommand("VIDEO OUTPUT ASSIGN: already on this output, or "
                              "no such deck or output");
          }
          return;
        }
        if (outputArg == "UNASSIGN") {
          if (!unassignDeckFromOutput(project_.focusedDeckIndex,
                                      project_.focusedOutputIndex)) {
            failRemoteCommand("VIDEO OUTPUT UNASSIGN: this deck is not on this "
                              "output, or it is the only thing on it");
          }
          return;
        }
        if (outputArg == "LAYER") {
          // No argument: report the whole stack, which is the only way to see
          // it over the wire and the first thing a test asks for.
          if (parts.size() <= 3) {
            const OutputTarget& out = focusedOutput();
            std::ostringstream report;
            report << layerLetter(0) << ":" << deckLabel(out.hostDeckIndex);
            for (std::size_t i = 0; i < out.layerDecks.size(); ++i) {
              report << " | " << layerLetter(static_cast<int>(i) + 1) << ":"
                     << deckLabel(out.layerDecks[i].deckIndex);
            }
            remoteCommandDetail_ = report.str();
            return;
          }
          auto parsed = parseNumber(3);
          if (!parsed) {
            failRemoteCommand("VIDEO OUTPUT LAYER: expected a layer number "
                              "(1 is the base)");
            return;
          }
          // Spoken as 1 = the base, because that is how the UI numbers them;
          // stored as 0 = the base.
          const int wanted = static_cast<int>(std::lround(*parsed)) - 1;
          if (!setDeckOutputAssignmentLayer(project_.focusedDeckIndex,
                                            project_.focusedOutputIndex, wanted)) {
            failRemoteCommand("VIDEO OUTPUT LAYER: this deck is not on this "
                              "output, or it is already at that layer");
          }
          return;
        }
        if (outputArg == "LAYERWARP") {
          // VIDEO OUTPUT LAYERWARP
          //   -> the focused playlist's corner pin on the focused output
          // VIDEO OUTPUT LAYERWARP OFF
          // VIDEO OUTPUT LAYERWARP <tlx> <tly> <trx> <try> <brx> <bry> <blx> <bly>
          //
          // Eight numbers, clockwise from the top left, each a FRACTION of
          // the layer's own rect -- so a pin means the same thing after the
          // layer is moved or resized. The output's warp is in pixels for
          // the opposite reason: its raster does not move.
          OutputTarget& out = focusedOutputMutable();
          OutputLayer* layer = nullptr;
          for (OutputLayer& candidate : out.layerDecks) {
            if (candidate.deckIndex == project_.focusedDeckIndex) {
              layer = &candidate;
              break;
            }
          }
          if (!layer) {
            // The HOST is not a layer and has no pin of its own -- the
            // output's warp is its mapping. Saying so is better than
            // silently doing nothing to a deck that is plainly on the output.
            failRemoteCommand("VIDEO OUTPUT LAYERWARP: " +
                              deckLabel(project_.focusedDeckIndex) +
                              " is not a layer on " + outputLabel(project_.focusedOutputIndex) +
                              " (the base uses "
                              "the output's own warp)");
            return;
          }
          if (parts.size() <= 3) {
            std::ostringstream report;
            report << (layer->warpEnabled ? "on" : "off");
            if (layer->warpEnabled) {
              report << " " << layer->warpTopLeftX << "," << layer->warpTopLeftY
                     << " " << layer->warpTopRightX << "," << layer->warpTopRightY
                     << " " << layer->warpBottomRightX << "," << layer->warpBottomRightY
                     << " " << layer->warpBottomLeftX << "," << layer->warpBottomLeftY;
            }
            remoteCommandDetail_ = report.str();
            return;
          }
          const std::string mode = toUpper(parts[3]);
          if (mode == "OFF" || mode == "RESET" || mode == "NONE") {
            layer->warpEnabled = false;
            layer->warpTopLeftX = layer->warpTopLeftY = 0.0f;
            layer->warpTopRightX = layer->warpTopRightY = 0.0f;
            layer->warpBottomRightX = layer->warpBottomRightY = 0.0f;
            layer->warpBottomLeftX = layer->warpBottomLeftY = 0.0f;
            markProjectDirty();
            remoteCommandDetail_ = "off";
            return;
          }
          if (parts.size() < 11) {
            failRemoteCommand("VIDEO OUTPUT LAYERWARP: expected OFF, or eight "
                              "numbers clockwise from the top left");
            return;
          }
          float corners[8];
          for (int i = 0; i < 8; ++i) {
            // `corner`, not `value`: the VIDEO branch already has a `value`
            // in scope and hiding it is what the warnings audit fails on.
            auto corner = parseNumber(3 + i);
            if (!corner) {
              failRemoteCommand("VIDEO OUTPUT LAYERWARP: '" + parts[3 + i] +
                                "' is not a number");
              return;
            }
            // A corner may legitimately go outside the layer -- that is what
            // pinning onto a bigger surface looks like -- but not so far that
            // the quad turns inside out or leaves the raster entirely.
            corners[i] = std::clamp(static_cast<float>(*corner), -2.0f, 2.0f);
          }
          layer->warpTopLeftX = corners[0];  layer->warpTopLeftY = corners[1];
          layer->warpTopRightX = corners[2]; layer->warpTopRightY = corners[3];
          layer->warpBottomRightX = corners[4]; layer->warpBottomRightY = corners[5];
          layer->warpBottomLeftX = corners[6];  layer->warpBottomLeftY = corners[7];
          layer->warpEnabled = true;
          markProjectDirty();
          remoteCommandDetail_ = "on for " + deckLabel(project_.focusedDeckIndex);
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
          if (typeArg == "MULTIVIEW" || typeArg == "MULTI") {
            setFocusedOutputType("multiview");
            return;
          }
          if (typeArg == "PROMPTER" || typeArg == "TELEPROMPTER") {
            setFocusedOutputType("prompter");
            return;
          }
          if (typeArg == "PRESENTER" || typeArg == "NOTES") {
            setFocusedOutputType("presenter");
            return;
          }
          // Said, not swallowed: an unrecognised type used to return quietly
          // and leave the output exactly as it was.
          failRemoteCommand("output type: expected window|stream|presenter, got "
                            + parts[3]);
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
          // NOT SILENTLY OK. Anything that is not a number here is a sub-verb
          // this branch does not have, and answering OK to one is how
          // "VIDEO OUTPUT SELECT 1" came back successful while selecting
          // nothing -- which then made a measurement of the layer stack look
          // like a product fault.
          failRemoteCommand("VIDEO OUTPUT: no such option '" + parts[2] +
                            "' -- try ADD, ON, OFF, ASSIGN, UNASSIGN, LAYER, "
                            "HOST, TYPE, MIRROR or an output number");
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
        // The stream key. NEVER ECHOED -- not in the reply, not in the toast,
        // not in the show log. It is the one field here that is a credential:
        // anyone holding it can broadcast to the channel, and a value that
        // gets read back is a value that ends up in a screenshot.
        if (streamArg == "KEY" && parts.size() > 3) {
          const std::string key = trim(joinParts(parts, 3));
          project_.outputs[project_.focusedOutputIndex].streamKey = key;
          stopOutputStream(project_.focusedOutputIndex);
          markProjectDirty();
          triggerToast("stream key set (" + std::to_string(key.size()) + " chars)");
          remoteCommandDetail_ = "stream key set (" + std::to_string(key.size()) + " chars)";
          return;
        }
        if (streamArg == "KEY") {   // no argument clears it
          project_.outputs[project_.focusedOutputIndex].streamKey.clear();
          stopOutputStream(project_.focusedOutputIndex);
          markProjectDirty();
          triggerToast("stream key cleared");
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
          // The DECK, deliberately: canvas panning is where this playlist
          // looks within a larger canvas, which did not move with warp.
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
          // Reports the OUTPUT, which is where every setter below writes.
          // Reading the deck meant WARP always answered 'off'.
          const OutputTarget& out = focusedOutput();
          triggerToast(std::string("warp: ") + (out.warpEnabled ? "on" : "off")
                       + " (" + toLower(warpModeLabel(out.warpMode)) + ")");
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
            triggerToast("warp mode: " + toLower(warpModeLabel(focusedOutput().warpMode)));
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
          const OutputTarget& focused = focusedOutput();
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
    // THE DASHBOARD, which is a list of commands with labels on.
    //
    // Firing a slot RUNS ITS COMMAND through this same dispatcher, so a slot
    // can do anything the protocol can and needs no dispatch of its own. That
    // is why the on-screen panel and a Companion button are the same feature:
    // both end up here, at DASH <n>, running the line the operator wrote.
    //
    // Guarded against a slot that fires DASH, which would otherwise be a way
    // to build a loop that takes the show down with it.
    // SHOW THE SHORTCUTS PAGE. The same reasoning as DASH SHOW below: a
    // surface that can fire an action should be able to put the page of
    // actions on screen. It was reachable only from the keyboard, which is the
    // one input an operator driving Deckboy from a Stream Deck does not have
    // their hands on.
    // GET PAST THE BOOT SCREEN.
    //
    // Several verbs refuse while the startup dialog is up -- SHORTCUTS and
    // DASH say so in as many words -- and nothing could put it down. The only
    // ways past were a bare show path on the command line or --import, which
    // both mean a scripted run has to have a FILE to hand just to reach the
    // app. That is a real gap for anything driving Deckboy without a keyboard:
    // scripted capture, a Stream Deck at a kiosk, and the driven tests this
    // codebase relies on to prove a control actually works.
    //
    // DISMISS is Escape, exactly: it closes the dialog and chooses NOTHING. It
    // does not start a new show, open a picker or load a recent -- each of
    // those is a separate decision with its own button, and a verb that
    // silently picked one would be a verb that quietly discards whatever was
    // already loaded.
    // SYNTH <shape|mirror|palette> <name>   -- the video synth's LOOK.
    //
    // These three were reachable only by clicking an inspector dropdown, which
    // meant the runtime path -- changing the look of a synth cue that is
    // ALREADY PLAYING -- could not be exercised at all. That is where this
    // class of thing breaks: a control that works on a stopped cue and does
    // nothing, or restarts it, on a live one.
    //
    // NAMED, not indexed, for the same reason AUDIOVIS and MESH are: a surface
    // button sending "7" would follow the enum if a value were ever inserted,
    // and silently start selecting a different picture on every show using it.
    // Palettes especially -- the list has grown twice already.
    //
    // A bare sub-verb is a question, like a bare AUDIOGAIN, so a controller can
    // read the current look back rather than track it.
    // CHIP <voice|duty|noise|env|quantise> ...  -- the 2A03's TIMBRE.
    //
    // SYNTHNOTEON could play a note and nothing could choose what played it,
    // so every part recorded came out as the same pulse at the same duty --
    // four copies of one instrument rather than four voices. Voice and duty
    // are what make Pulse-at-eighth and Pulse-at-half two different parts.
    //
    // Named after the chip rather than SYNTH, which the VIDEO synth already
    // owns. Named values, not indices, for the same reason as everything else
    // on this surface.
    // TONECUE -- make a chip-synth cue.
    //
    // The same gap NDICUE closed: addToneCue() existed and could be reached
    // only from the SOURCE menu, so the one cue kind that is nothing but audio
    // could not be created from a controller, and could not be tested without
    // a human clicking. CHIP, SYNTHNOTEON and the whole keyboard surface all
    // need a tone cue to exist before they do anything.
    if (command == "STING") {
      // STING              -> report it
      // STING ON           -> make the selected tone cue a sting
      // STING PITCH <hz> | LENGTH <s> | SWEEP <semitones> | BODY <0-100>
      //
      // The built-in walk-up sting: generated, not a file. A tone cue with
      // this waveform ENDS by itself, unlike every other signal here.
      Cue* cue = selectedCueMutable();
      if (!cue || cue->kind != CueKind::Tone) {
        failRemoteCommand("STING: select a tone cue first (TONECUE makes one)");
        return;
      }
      ToneSettings& t = cue->tone;
      const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();
      if (sub == "ON") {
        t.waveform = ToneWaveform::Sting;
        markProjectDirty();
        refreshAllLiveCueRuntimes();
        remoteCommandDetail_ = "sting";
        return;
      }
      if (sub.empty()) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "%s | %.0f Hz | %.2f s | %+.1f semitones | body %.0f%%",
                      t.waveform == ToneWaveform::Sting ? "sting"
                                                        : "NOT a sting (set STING ON)",
                      t.stingPitchHz, t.stingSeconds, t.stingSweepSemitones,
                      t.stingBody * 100.0);
        remoteCommandDetail_ = buf;
        return;
      }
      if (parts.size() >= 3) {
        auto parsed = parseNumber(2);
        if (!parsed) {
          failRemoteCommand("STING " + sub + ": expected a number");
          return;
        }
        // REFUSED RATHER THAN CLAMPED, the rule the whole protocol follows:
        // a clamp is what makes wrong units invisible.
        if (sub == "PITCH") {
          if (*parsed < 20.0 || *parsed > 12000.0) {
            failRemoteCommand("STING PITCH: expected 20-12000 Hz");
            return;
          }
          t.stingPitchHz = *parsed;
        } else if (sub == "LENGTH" || sub == "LEN") {
          if (*parsed < 0.05 || *parsed > 10.0) {
            failRemoteCommand("STING LENGTH: expected 0.05-10 seconds");
            return;
          }
          t.stingSeconds = *parsed;
        } else if (sub == "SWEEP") {
          if (*parsed < -24.0 || *parsed > 24.0) {
            failRemoteCommand("STING SWEEP: expected -24 to 24 semitones");
            return;
          }
          t.stingSweepSemitones = *parsed;
        } else if (sub == "BODY") {
          if (*parsed < 0.0 || *parsed > 100.0) {
            failRemoteCommand("STING BODY: expected 0-100");
            return;
          }
          t.stingBody = *parsed / 100.0;
        } else {
          failRemoteCommand("STING: expected ON, PITCH, LENGTH, SWEEP or BODY");
          return;
        }
        markProjectDirty();
        refreshAllLiveCueRuntimes();
        remoteCommandDetail_ = parts[2];
        return;
      }
      failRemoteCommand("STING: expected ON, PITCH, LENGTH, SWEEP or BODY");
      return;
    }
    if (command == "TONECUE") {
      addToneCue();
      const Deck& deck = focusedDeck();
      remoteCommandDetail_ = "tone cue " + std::to_string(deck.cues.size());
      return;
    }
    if (command == "CHIP") {
      Cue* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand("CHIP: no cue selected");
        return;
      }
      if (cue->kind != CueKind::Tone) {
        failRemoteCommand("CHIP: the selected cue is not a chip synth");
        return;
      }
      // VOICE, DUTY and NOISE are 2A03 registers and have no FDS equivalent --
      // the FDS makes its timbre from a wavetable and a modulator instead. The
      // envelope is shared, so ENV is allowed on either rather than refused
      // for a chip that genuinely has one.
      const bool isNes = cue->tone.synth.chip == SynthChip::Nes;
      auto report = [&]() {
        const auto& s = cue->tone.synth;
        const char* voice = s.nesVoice == NesVoice::Triangle ? "triangle"
                          : s.nesVoice == NesVoice::Noise    ? "noise" : "pulse";
        const char* duty = s.nesDuty == NesDuty::Eighth       ? "eighth"
                         : s.nesDuty == NesDuty::Quarter      ? "quarter"
                         : s.nesDuty == NesDuty::ThreeQuarter ? "threequarter" : "half";
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "voice=%s duty=%s noise=%s attack=%.3f release=%.3f quantise=%d",
                      voice, duty, s.nesNoiseShort ? "short" : "long",
                      s.attackSeconds, s.releaseSeconds, s.nesQuantise ? 1 : 0);
        remoteCommandDetail_ = buf;
      };
      if (parts.size() < 2) {
        report();
        return;
      }
      const std::string sub = toUpper(parts[1]);
      std::string want;
      for (std::size_t i = 2; i < parts.size() && i < 3; ++i) {
        for (char c : parts[i]) {
          if (c != ' ' && c != '_' && c != '-') {
            want += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          }
        }
      }

      if (sub == "VOICE") {
        if (!isNes) { failRemoteCommand("CHIP VOICE: this cue is an FDS synth, which has no voice"); return; }
        if (want == "pulse")         cue->tone.synth.nesVoice = NesVoice::Pulse;
        else if (want == "triangle") cue->tone.synth.nesVoice = NesVoice::Triangle;
        else if (want == "noise")    cue->tone.synth.nesVoice = NesVoice::Noise;
        else {
          failRemoteCommand("CHIP VOICE: expected pulse|triangle|noise, got '" + want + "'");
          return;
        }
      } else if (sub == "DUTY") {
        if (!isNes) { failRemoteCommand("CHIP DUTY: this cue is an FDS synth, which has no duty"); return; }
        // 75% is included because trackers expose it and people look for it.
        // It is the phase inverse of 25% and sounds identical -- which is
        // worth knowing before spending a take trying to hear the difference.
        if (want == "eighth" || want == "12.5" || want == "125")   cue->tone.synth.nesDuty = NesDuty::Eighth;
        else if (want == "quarter" || want == "25")                cue->tone.synth.nesDuty = NesDuty::Quarter;
        else if (want == "half" || want == "50")                   cue->tone.synth.nesDuty = NesDuty::Half;
        else if (want == "threequarter" || want == "75")           cue->tone.synth.nesDuty = NesDuty::ThreeQuarter;
        else {
          failRemoteCommand("CHIP DUTY: expected eighth|quarter|half|threequarter, got '" + want + "'");
          return;
        }
      } else if (sub == "NOISE") {
        if (!isNes) { failRemoteCommand("CHIP NOISE: this cue is an FDS synth, which has no noise"); return; }
        if (want == "short" || want == "periodic")  cue->tone.synth.nesNoiseShort = true;
        else if (want == "long" || want == "hiss")  cue->tone.synth.nesNoiseShort = false;
        else {
          failRemoteCommand("CHIP NOISE: expected short|long, got '" + want + "'");
          return;
        }
      } else if (sub == "QUANTISE" || sub == "QUANTIZE") {
        if (want == "on" || want == "1")       cue->tone.synth.nesQuantise = true;
        else if (want == "off" || want == "0") cue->tone.synth.nesQuantise = false;
        else {
          failRemoteCommand("CHIP QUANTISE: expected on|off, got '" + want + "'");
          return;
        }
      } else if (sub == "ENV") {
        if (parts.size() < 4) {
          failRemoteCommand("CHIP ENV: expected <attack> <release> in seconds");
          return;
        }
        cue->tone.synth.attackSeconds = std::clamp(std::atof(parts[2].c_str()), 0.0, 5.0);
        cue->tone.synth.releaseSeconds = std::clamp(std::atof(parts[3].c_str()), 0.0, 10.0);
      } else {
        failRemoteCommand("CHIP: expected voice|duty|noise|env|quantise, got " + parts[1]);
        return;
      }
      // LIVE, like SYNTH: the edit reaches the engine's cue snapshot on the
      // next tick, so the timbre changes under a held note rather than at the
      // next take.
      markProjectDirty();
      report();
      return;
    }
    if (command == "SYNTH") {
      Cue* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand("SYNTH: no cue selected");
        return;
      }
      if (cue->kind != CueKind::VideoSynth) {
        failRemoteCommand("SYNTH: the selected cue is not a video synth");
        return;
      }
      if (parts.size() < 2) {
        remoteCommandDetail_ =
          std::string("shape=") + vsShapeLabel(cue->videoSynth.shape) +
          " mirror=" + vsMirrorLabel(cue->videoSynth.mirror) +
          " palette=" + vsPaletteLabel(cue->videoSynth.palette);
        return;
      }
      const std::string sub = toUpper(parts[1]);
      // The value, lower-cased and stripped of the spaces the labels carry --
      // so "game boy", "gameboy" and "GAMEBOY" all work. An operator reading
      // the name off the inspector should not have to guess the spelling.
      std::string want;
      for (std::size_t i = 2; i < parts.size(); ++i) {
        for (char c : parts[i]) {
          if (c != ' ' && c != '_' && c != '-') {
            want += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          }
        }
      }

      auto named = [&](std::initializer_list<std::pair<const char*, int>> table,
                       const char* what) -> std::optional<int> {
        if (want.empty()) {
          std::string known;
          for (const auto& entry : table) {
            if (!known.empty()) known += " ";
            known += entry.first;
          }
          failRemoteCommand(std::string("SYNTH ") + what + ": expected one of: " + known);
          return std::nullopt;
        }
        for (const auto& entry : table) {
          if (want == entry.first) return entry.second;
        }
        std::string known;
        for (const auto& entry : table) {
          if (!known.empty()) known += " ";
          known += entry.first;
        }
        failRemoteCommand(std::string("SYNTH ") + what + ": unknown '" + want +
                          "' (one of: " + known + ")");
        return std::nullopt;
      };

      if (sub == "SHAPE") {
        auto v = named({{"plasma", 0}, {"diamond", 1}, {"rings", 2},
                        {"grid", 3}, {"moire", 4}}, "SHAPE");
        if (!v) return;
        cue->videoSynth.shape = static_cast<VideoSynthShape>(*v);
      } else if (sub == "MIRROR") {
        auto v = named({{"none", 0}, {"horizontal", 1}, {"quad", 2},
                        {"kaleido", 3}}, "MIRROR");
        if (!v) return;
        cue->videoSynth.mirror = static_cast<VideoSynthMirror>(*v);
      } else if (sub == "PALETTE") {
        auto v = named({{"spectrum", 0}, {"amber", 1}, {"ice", 2}, {"fire", 3},
                        {"mono", 4}, {"ega16", 5}, {"ega", 5}, {"c64", 6},
                        {"gameboy", 7}, {"cga", 8}, {"nes", 9},
                        {"vapor", 10}}, "PALETTE");
        if (!v) return;
        cue->videoSynth.palette = static_cast<VideoSynthPalette>(*v);
      } else {
        failRemoteCommand("SYNTH: expected shape|mirror|palette, got " + parts[1]);
        return;
      }
      // LIVE. markProjectDirty publishes the edit to the engine's cue snapshot
      // on the next tick, which is what lets the look change under a cue that
      // is already playing rather than at the next take.
      markProjectDirty();
      remoteCommandDetail_ =
        std::string("shape=") + vsShapeLabel(cue->videoSynth.shape) +
        " mirror=" + vsMirrorLabel(cue->videoSynth.mirror) +
        " palette=" + vsPaletteLabel(cue->videoSynth.palette);
      return;
    }
    if (command == "STARTUP") {
      // A bare STARTUP is a question, like a bare AUDIOGAIN.
      if (parts.size() < 2) {
        remoteCommandDetail_ = showStartupDialog_ ? "dialog"
                             : (showSplashOverlay_ ? "splash" : "clear");
        return;
      }
      const std::string sub = toUpper(parts[1]);
      if (sub != "DISMISS") {
        failRemoteCommand("STARTUP: expected dismiss, got " + parts[1]);
        return;
      }
      const bool had = showStartupDialog_ || showSplashOverlay_;
      showStartupDialog_ = false;
      showSplashOverlay_ = false;
      remoteCommandDetail_ = had ? "dismissed" : "nothing was up";
      return;
    }
    if (command == "SHORTCUTS" || command == "KEYS") {
      const std::string sub = parts.size() < 2 ? std::string("TOGGLE") : toUpper(parts[1]);
      if (sub != "SHOW" && sub != "HIDE" && sub != "TOGGLE") {
        failRemoteCommand("SHORTCUTS: expected show|hide|toggle, got " + parts[1]);
        return;
      }
      if (showStartupDialog_ || showSplashOverlay_) {
        failRemoteCommand("SHORTCUTS: the startup dialog is up");
        return;
      }
      shortcutsOverlayOpen_ = (sub == "TOGGLE") ? !shortcutsOverlayOpen_
                                                : (sub == "SHOW");
      remoteCommandDetail_ = shortcutsOverlayOpen_ ? "shortcuts shown" : "shortcuts hidden";
      return;
    }
    if (command == "DASH" || command == "DASHBOARD") {
      const std::string sub = parts.size() < 2 ? std::string("LIST") : toUpper(parts[1]);

      if (sub == "LIST") {
        if (project_.dashboard.empty()) {
          remoteCommandDetail_ = "empty (DASH SET <n> <label> | <command>)";
          return;
        }
        std::ostringstream s;
        for (std::size_t n = 0; n < project_.dashboard.size(); ++n) {
          const DashboardSlot& slot = project_.dashboard[n];
          if (n) s << " | ";
          s << (n + 1) << ':';
          if (!slot.glyph.empty()) s << slot.glyph << ' ';
          s << (slot.label.empty() ? "(unset)" : slot.label);
          if (!slot.command.empty()) s << " -> " << slot.command;
        }
        remoteCommandDetail_ = s.str();
        return;
      }

      if (sub == "SET" && parts.size() >= 3) {
        // DASH SET <n> <label> | <command> [| <glyph>]
        int at = 0;
        try {
          at = std::stoi(parts[2]) - 1;
        } catch (...) {
          failRemoteCommand("DASH SET: <n> <label> | <command> [| <glyph>]");
          return;
        }
        if (at < 0 || at >= 512) {
          failRemoteCommand("DASH SET: slot number out of range");
          return;
        }
        const std::string rest = parts.size() > 3 ? joinParts(parts, 3) : std::string();
        std::vector<std::string> bits;
        std::size_t from = 0;
        for (;;) {
          const std::size_t bar = rest.find('|', from);
          bits.push_back(trim(rest.substr(from, bar == std::string::npos
                                                  ? std::string::npos : bar - from)));
          if (bar == std::string::npos) break;
          from = bar + 1;
        }
        if (bits.size() < 2 || bits[1].empty()) {
          failRemoteCommand("DASH SET: needs a command -- "
                            "DASH SET <n> <label> | <command> [| <glyph>]");
          return;
        }
        if (toUpper(bits[1]).rfind("DASH", 0) == 0) {
          // A slot that fires the dashboard is a loop waiting to happen, and
          // it would run during a show rather than while it was written.
          failRemoteCommand("DASH SET: a slot cannot fire the dashboard");
          return;
        }
        if (static_cast<int>(project_.dashboard.size()) <= at) {
          project_.dashboard.resize(at + 1);
        }
        DashboardSlot& slot = project_.dashboard[at];
        slot.label = bits[0];
        slot.command = bits[1];
        if (bits.size() > 2) slot.glyph = bits[2];
        markProjectDirty();
        remoteCommandDetail_ = std::to_string(at + 1) + ": " + slot.label +
                               " -> " + slot.command;
        return;
      }

      if (sub == "CLEAR" && parts.size() >= 3) {
        int at = -1;
        try { at = std::stoi(parts[2]) - 1; } catch (...) { at = -1; }
        if (at < 0 || at >= static_cast<int>(project_.dashboard.size())) {
          failRemoteCommand("DASH CLEAR: no slot " + (parts.size() > 2 ? parts[2] : ""));
          return;
        }
        project_.dashboard[at] = DashboardSlot{};
        markProjectDirty();
        remoteCommandDetail_ = "slot " + std::to_string(at + 1) + " cleared";
        return;
      }

      // Which view the page shows, and show it: a Companion page that runs
      // the master sequence wants the tracker up, not the tiles.
      if (sub == "TRACKER" || sub == "TILES") {
        if (showStartupDialog_ || showSplashOverlay_) {
          failRemoteCommand("DASH: the startup dialog is up");
          return;
        }
        project_.dashboardMode = sub == "TRACKER" ? 1 : 0;
        dashboardOverlayOpen_ = true;
        markProjectDirty();
        remoteCommandDetail_ = sub == "TRACKER" ? "tracker shown" : "tiles shown";
        return;
      }
      // Put the page on screen. A surface that can fire a slot should also
      // be able to show the operator the page those slots live on.
      if (sub == "SHOW" || sub == "HIDE" || sub == "TOGGLE") {
        if (showStartupDialog_ || showSplashOverlay_) {
          failRemoteCommand("DASH: the startup dialog is up");
          return;
        }
        dashboardOverlayOpen_ = (sub == "TOGGLE") ? !dashboardOverlayOpen_
                                                  : (sub == "SHOW");
        remoteCommandDetail_ = dashboardOverlayOpen_ ? "dashboard shown"
                                                     : "dashboard hidden";
        return;
      }

      // A bare number fires that slot -- the whole point, and the shortest
      // thing to bind a Companion button to.
      int at = -1;
      try { at = std::stoi(parts[1]) - 1; } catch (...) { at = -1; }
      if (at >= 0 && at < static_cast<int>(project_.dashboard.size())) {
        const DashboardSlot slot = project_.dashboard[at];
        if (slot.command.empty()) {
          failRemoteCommand("DASH: slot " + std::to_string(at + 1) + " is empty");
          return;
        }
        handleRemoteCommand(slot.command);
        // The slot's own answer, not the inner command's, so a surface sees
        // what it pressed.
        remoteCommandRecognized_ = true;
        remoteCommandError_.clear();
        remoteCommandDetail_ = slot.label.empty() ? slot.command : slot.label;
        return;
      }
      failRemoteCommand("DASH: LIST | <n> | SET <n> <label> | <command> | CLEAR <n>");
      return;
    }

    // EVERY NAMED PROJECT SETTING, readable and writable.
    //
    // Taken from what Resolume gets right: a surface should be able to ask what
    // a setting IS, not only tell it what to be. Deckboy had 86 project
    // settings with names, defaults and validated setters -- all of it built
    // for the show file -- and not one of them reachable over the wire. The
    // only way to change most was to edit a .deckboy by hand, which is exactly
    // how several things got tested today.
    //
    // BOTH DIRECTIONS GO THROUGH THE SHOW FILE'S OWN CODE. SET builds the same
    // "key<TAB>value" line the loader reads and hands it to
    // applyProjectScalarLine, so every clamp and alias applies unchanged; GET
    // runs the writer into a string and reads the line back. A second table
    // mapping names to fields would drift the first time a setting was added,
    // and drift here means a command that silently sets nothing -- which is
    // the exact fault this session kept finding.
    if (command == "GET" || command == "SET") {
      std::ostringstream dump;
      writeProjectScalars(dump, project_);
      const std::string scalars = dump.str();

      if (parts.size() < 2) {
        // No key: list them, so the space is discoverable rather than
        // something you have to read the source to learn.
        std::ostringstream keys;
        std::istringstream in(scalars);
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
          const std::size_t tab = line.find('	');
          if (tab == std::string::npos) continue;
          if (n++) keys << ' ';
          keys << line.substr(0, tab);
        }
        remoteCommandDetail_ = std::to_string(n) + " keys: " + keys.str();
        return;
      }

      const std::string key = toLower(parts[1]);
      auto valueOf = [&](const std::string& want) -> std::optional<std::string> {
        std::istringstream in(scalars);
        std::string line;
        while (std::getline(in, line)) {
          const std::size_t tab = line.find('	');
          if (tab == std::string::npos) continue;
          if (line.substr(0, tab) == want) {
            return line.substr(tab + 1);
          }
        }
        return std::nullopt;
      };

      if (command == "GET") {
        if (auto value = valueOf(key)) {
          remoteCommandDetail_ = key + " = " + *value;
          return;
        }
        failRemoteCommand("GET: no setting called \"" + key +
                          "\" (GET with no key lists them)");
        return;
      }

      if (parts.size() < 3) {
        failRemoteCommand("SET: needs a value -- SET <key> <value>");
        return;
      }
      // Refused rather than ignored. applyProjectScalarLine returns false for
      // a name it does not know, and answering OK to that would be the same
      // silent-success fault as everything else fixed today.
      if (!valueOf(key)) {
        failRemoteCommand("SET: no setting called \"" + key +
                          "\" (GET with no key lists them)");
        return;
      }
      std::vector<std::string> fields {key, joinParts(parts, 2)};
      auto ensureDeck = [this](std::size_t index) -> Deck& {
        while (project_.decks.size() <= index) {
          Deck added;
          added.name = deckDefaultName(static_cast<int>(project_.decks.size()));
          project_.decks.push_back(added);
        }
        return project_.decks[index];
      };
      if (!applyProjectScalarLine(project_, fields, ensureDeck)) {
        failRemoteCommand("SET: \"" + key + "\" is not a settable scalar");
        return;
      }
      markProjectDirty();
      // SOME SETTINGS NEED APPLYING, not just storing.
      //
      // applyProjectScalarLine assigns the field, which is all the show file
      // loader needs -- the loader is followed by a startup pass that applies
      // everything. Reached from a command there is no such pass, so SET theme
      // stored the name, answered OK, and left the old palette on screen.
      // Which is the silent success this whole verb exists to avoid, shipped
      // in the verb itself.
      //
      // Only the keys with a side effect are listed; the rest are read where
      // they are used and need nothing.
      if (key == "theme" && !project_.theme.empty()) {
        loadTheme(project_.theme);
      } else if (key == "ui_scale") {
        applyUiScale();
      } else if (key == "splash_character") {
        refreshSplashAsset();
      }
      // Read back what it BECAME, not what was asked for: these setters clamp,
      // and an operator is owed the number that actually took.
      std::ostringstream after;
      writeProjectScalars(after, project_);
      std::istringstream in(after.str());
      std::string line;
      while (std::getline(in, line)) {
        const std::size_t tab = line.find('	');
        if (tab != std::string::npos && line.substr(0, tab) == key) {
          remoteCommandDetail_ = key + " = " + line.substr(tab + 1);
          break;
        }
      }
      return;
    }

    // THE OUTPUT ITSELF, and the sinks that had no verb.
    //
    // NDI and DeckLink have had commands since they were written; Spout never
    // did, and neither did the output's own enable -- so arming an output or
    // routing it to Spout could not be done from a surface at all. Testing it
    // meant editing the show file by hand, which is exactly what it took.
    //
    // OUT is the trim out-point and cannot be reused, which is also why HELP
    // advertising "OUT <on|off>" was wrong.
    if (command == "OUTPUT") {
      const std::string sub = parts.size() < 2 ? std::string("STATUS") : toUpper(parts[1]);
      // OUTPUT ADD [<deck>]  -> a second output, hosted by that deck
      // OUTPUT DECK <n>      -> point the focused output at deck n
      // OUTPUT SELECT <n>    -> which output the other verbs act on
      //
      // Until now a deck other than the first had nowhere to go: adding a deck
      // created no output, and every output composited deck 1 regardless. So
      // these three are what make a second deck visible at all.
      if (sub == "ADD" || sub == "NEW") {
        int host = static_cast<int>(project_.decks.size()) - 1;
        if (parts.size() >= 3) {
          try {
            host = std::stoi(parts[2]) - 1;
          } catch (...) {
            failRemoteCommand("OUTPUT ADD: expected a deck number");
            return;
          }
        }
        if (host < 0 || host >= static_cast<int>(project_.decks.size())) {
          failRemoteCommand("OUTPUT ADD: no deck " +
                            (parts.size() >= 3 ? parts[2] : std::to_string(host + 1)));
          return;
        }
        const int added = addOutput(host);
        if (added < 0 || added >= static_cast<int>(project_.outputs.size())) {
          failRemoteCommand("OUTPUT ADD: could not add an output");
          return;
        }
        setFocusedOutputIndex(added);
        markProjectDirty();
        remoteCommandDetail_ = "output " + std::to_string(added + 1) +
                               " hosting deck " + std::to_string(host + 1);
        return;
      }
      if (sub == "SELECT" && parts.size() >= 3) {
        int which = 0;
        try {
          which = std::stoi(parts[2]) - 1;
        } catch (...) {
          failRemoteCommand("OUTPUT SELECT: expected an output number");
          return;
        }
        if (which < 0 || which >= static_cast<int>(project_.outputs.size())) {
          failRemoteCommand("OUTPUT SELECT: no output " + parts[2]);
          return;
        }
        setFocusedOutputIndex(which);
        remoteCommandDetail_ = "output " + parts[2];
        return;
      }
      if (sub == "DECK") {
        if (project_.outputs.empty()) {
          failRemoteCommand("OUTPUT DECK: no outputs");
          return;
        }
        if (parts.size() < 3) {
          remoteCommandDetail_ = "deck " +
            std::to_string(std::clamp(focusedOutput().hostDeckIndex, 0,
                                      static_cast<int>(project_.decks.size()) - 1) + 1);
          return;
        }
        int host = 0;
        try {
          host = std::stoi(parts[2]) - 1;
        } catch (...) {
          failRemoteCommand("OUTPUT DECK: expected a deck number");
          return;
        }
        if (host < 0 || host >= static_cast<int>(project_.decks.size())) {
          failRemoteCommand("OUTPUT DECK: no deck " + parts[2]);
          return;
        }
        project_.outputs[project_.focusedOutputIndex].hostDeckIndex = host;
        markProjectDirty();
        remoteCommandDetail_ = "output " +
                               std::to_string(project_.focusedOutputIndex + 1) +
                               " -> deck " + parts[2];
        return;
      }
      if (sub == "STATUS") {
        if (project_.outputs.empty()) {
          failRemoteCommand("OUTPUT: no outputs");
          return;
        }
        const OutputTarget& o = focusedOutput();
        std::ostringstream s;
        s << (project_.focusedOutputIndex + 1) << "/" << project_.outputs.size()
          << " \"" << o.name << "\""
          << " enabled=" << (o.enabled ? 1 : 0)
          << " type=" << o.outputType
          << " display=" << o.displayIndex
          << " ndi=" << (o.ndiEnabled ? 1 : 0)
          << " spout=" << (o.spoutEnabled ? 1 : 0)
          << " decklink=" << (o.deckLinkEnabled ? 1 : 0)
          << " stream=" << (o.streamEnabled ? 1 : 0)
          << " health=" << outputHealthLabel(project_.focusedOutputIndex);
        remoteCommandDetail_ = s.str();
        return;
      }
      if (sub == "LIST") {
        std::ostringstream s;
        for (std::size_t n = 0; n < project_.outputs.size(); ++n) {
          if (n) s << " | ";
          s << (n + 1) << ":" << project_.outputs[n].name
            << (project_.outputs[n].enabled ? "(on)" : "(off)");
        }
        remoteCommandDetail_ = s.str();
        return;
      }
      if (sub == "ON" || sub == "OFF" || sub == "TOGGLE") {
        const bool want = (sub == "TOGGLE") ? !focusedOutput().enabled : (sub == "ON");
        if (want != focusedOutput().enabled) {
          toggleFocusedOutputEnabled();
        }
        remoteCommandDetail_ = focusedOutput().enabled ? "on" : "off";
        return;
      }
      if (sub == "SPOUT") {
        if (parts.size() >= 4 && toUpper(parts[2]) == "NAME") {
          setFocusedOutputSpoutName(joinParts(parts, 3));
          remoteCommandDetail_ = focusedOutput().spoutSenderName;
          return;
        }
        const std::string arg = parts.size() < 3 ? std::string("TOGGLE") : toUpper(parts[2]);
        if (arg == "ON" || arg == "OFF" || arg == "TOGGLE") {
          setFocusedOutputSpoutEnabled(arg == "TOGGLE" ? !focusedOutput().spoutEnabled
                                                       : (arg == "ON"));
          remoteCommandDetail_ = focusedOutput().spoutEnabled
            ? ("on as \"" + focusedOutput().spoutSenderName + "\"") : "off";
          return;
        }
        failRemoteCommand("OUTPUT SPOUT: ON | OFF | TOGGLE | NAME <sender name>");
        return;
      }
      // A bare number focuses that output, so every other verb here can stay
      // about "the focused one" rather than growing an index argument.
      try {
        const int want = std::stoi(parts[1]) - 1;
        if (want >= 0 && want < static_cast<int>(project_.outputs.size())) {
          project_.focusedOutputIndex = want;
          remoteCommandDetail_ = focusedOutput().name;
          return;
        }
      } catch (...) {
      }
      failRemoteCommand("OUTPUT: STATUS | LIST | ON|OFF|TOGGLE | SPOUT ... | <n>");
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
        // CHECK THAT IT HAPPENED. A build without the NDI SDK answered
        // "OK NDI" here while ndiEnabled stayed false and nothing appeared on
        // the network -- measured with an NDI receiver that saw every other
        // source on the LAN. The operator gets the toast either way; the
        // caller now gets the reason instead of a false success.
        if (!focusedOutput().ndiEnabled) {
#if defined(DECKBOY_HAS_NDI_SDK)
          failRemoteCommand("NDI: output could not be enabled -- is the NDI runtime installed?");
#else
          failRemoteCommand("NDI: this build has no NDI output");
#endif
        }
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
      } else if (sub == "KEYFILL") {
        // DECKLINK KEYFILL ON|OFF|TOGGLE [key device index]
        const std::string kfSub = parts.size() > 2 ? toUpper(parts[2]) : "TOGGLE";
        if (kfSub == "ON") output.deckLinkKeyFill = true;
        else if (kfSub == "OFF") output.deckLinkKeyFill = false;
        else output.deckLinkKeyFill = !output.deckLinkKeyFill;
        if (parts.size() > 3) {
          output.deckLinkKeyDeviceId = std::atoi(parts[3].c_str());
        }
        // SAY WHEN IT CANNOT WORK. Key+fill with no second device sends the
        // fill and nothing else -- a premultiplied picture on black, which
        // looks almost right and is not what was asked for.
        if (output.deckLinkKeyFill && output.deckLinkKeyDeviceId < 0) {
          triggerToast("key+fill ON, but no key device set "
                       "(DECKLINK KEYFILL ON <device>)",
                       kToastWarnFill, kToastWarnInk, kToastReadableMs);
        } else {
          triggerToast(output.deckLinkKeyFill
                         ? ("key+fill ON, key on device " +
                            std::to_string(output.deckLinkKeyDeviceId))
                         : std::string("key+fill off"));
        }
      } else if (sub == "KEYDEVICE") {
        auto val = parseNumber(2);
        if (val) {
          output.deckLinkKeyDeviceId = static_cast<int>(*val);
          triggerToast("DeckLink key device " +
                       std::to_string(output.deckLinkKeyDeviceId));
        }
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
      // A BARE AUDIOGAIN IS A QUESTION, and it used to answer "OK" while
      // saying nothing at all -- so there was no way to check what normalize
      // had actually done except by eye.
      if (parts.size() < 2) {
        const Cue* cue = selectedCuePtr();
        if (!cue) {
          failRemoteCommand("AUDIOGAIN: no cue selected");
          return;
        }
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%+.1f dB", cue->audioGainDb);
        remoteCommandDetail_ = buf;
        return;
      }
      auto value = parseNumber(1);
      if (value && setSelectedAudioGainDb(*value)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "gain %+.1f dB", std::clamp(*value, static_cast<double>(kCueAudioGainMinDb), static_cast<double>(kCueAudioGainMaxDb)));
        triggerToast(std::string(buf) + audioEditScopeSuffix());
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
    if (command == "AUDIOFX") {
      // AUDIOFX                          -- read the chain back
      // AUDIOFX CLEAR                    -- empty it
      // AUDIOFX ADD <token> [amount%]    -- append an effect
      // AUDIOFX <n> OFF                  -- remove the nth (1-based)
      // AUDIOFX <n> BYPASS ON|OFF
      // AUDIOFX <n> <amount%> [a% [b% [c% [d%]]]]
      //
      // A BARE AUDIOFX IS A QUESTION, like a bare AUDIOGAIN. Without it there
      // is no way to check what a chain actually is except by eye and ear,
      // which is not a check at all -- and it is the only way a scripted test
      // can prove the effect it asked for is the effect that landed.
      Cue* cue = selectedCueMutable();
      if (!cue) {
        failRemoteCommand("AUDIOFX: no cue selected");
        return;
      }
      if (parts.size() < 2) {
        std::string reply;
        for (std::size_t i = 0; i < cue->audioEffects.size(); ++i) {
          const auto& fx = cue->audioEffects[i];
          char buf[128];
          std::snprintf(buf, sizeof(buf), "%zu:%s amount=%d%% a=%d b=%d c=%d d=%d%s",
                        i + 1, deckboy::audiofx::audioEffectToken(fx.kind),
                        static_cast<int>(std::lround(fx.amount * 100.0f)),
                        static_cast<int>(std::lround(fx.paramA * 100.0f)),
                        static_cast<int>(std::lround(fx.paramB * 100.0f)),
                        static_cast<int>(std::lround(fx.paramC * 100.0f)),
                        static_cast<int>(std::lround(fx.paramD * 100.0f)),
                        fx.bypassed ? " BYPASSED" : "");
          if (!reply.empty()) reply += "; ";
          reply += buf;
          // WHICH plugin, or the read-back cannot tell two plugin slots apart
          // -- and telling them apart is the only reason to read it back.
          if (fx.kind == deckboy::audiofx::AudioEffectKind::Plugin) {
            reply += " plugin=" +
                     (fx.pluginId.empty() ? std::string("(none)")
                                          : audioPluginSlotLabel(fx));
          }
        }
        remoteCommandDetail_ = reply.empty() ? "empty" : reply;
        return;
      }
      const std::string first = toUpper(parts[1]);
      if (first == "CLEAR") {
        forEachSelectedAudioStack([](std::vector<deckboy::audiofx::AudioEffect>& s) {
          s.clear();
        });
        triggerToast("audio chain cleared" + audioEditScopeSuffix());
        return;
      }
      if (first == "ADD") {
        if (parts.size() < 3) {
          failRemoteCommand("AUDIOFX ADD: needs an effect token");
          return;
        }
        const std::string token = toLower(parts[2]);
        const auto kind = deckboy::audiofx::audioEffectKindFromToken(token);
        if (kind == deckboy::audiofx::AudioEffectKind::None) {
          // NAME THE OPTIONS. "unknown effect: hipass" and nothing else makes
          // the caller guess at spelling; the list is nine words long.
          std::string known;
          for (int i = 1; i < static_cast<int>(deckboy::audiofx::AudioEffectKind::Count); ++i) {
            if (!known.empty()) known += " ";
            known += deckboy::audiofx::audioEffectToken(
              static_cast<deckboy::audiofx::AudioEffectKind>(i));
          }
          failRemoteCommand("AUDIOFX ADD: unknown effect '" + token +
                            "' (one of: " + known + ")");
          return;
        }
        deckboy::audiofx::AudioEffect fx =
          deckboy::audiofx::audioEffectDefaults(kind);
        if (parts.size() > 3) {
          if (auto amount = parseNumber(3)) {
            fx.amount = std::clamp(static_cast<float>(*amount / 100.0), 0.0f, 1.0f);
          }
        }
        bool full = false;
        const bool any = forEachSelectedAudioStack(
          [&fx, &full](std::vector<deckboy::audiofx::AudioEffect>& s) {
            if (s.size() >= 8) { full = true; return; }
            s.push_back(fx);
          });
        if (!any) {
          failRemoteCommand("AUDIOFX ADD: no cue with audio selected");
          return;
        }
        if (full) {
          failRemoteCommand("AUDIOFX ADD: chain full (8)");
          return;
        }
        remoteCommandDetail_ = std::string("added ") +
                               deckboy::audiofx::audioEffectLabel(kind);
        return;
      }
      // Everything else addresses one slot by its 1-based position, which is
      // what the read-back prints -- the caller never has to convert.
      auto slot = parseNumber(1);
      if (!slot) {
        failRemoteCommand("AUDIOFX: expected CLEAR, ADD or a slot number");
        return;
      }
      const int index = static_cast<int>(std::lround(*slot)) - 1;
      if (!audioEffectIndexValid(&cue->audioEffects, index)) {
        failRemoteCommand("AUDIOFX: no effect in slot " + parts[1]);
        return;
      }
      if (parts.size() >= 3) {
        const std::string verb = toUpper(parts[2]);
        if (verb == "OFF") {
          audioEffectStackRemove(index);
          return;
        }
        if (verb == "PLUGIN") {
          // AUDIOFX <n> PLUGIN <id or part of a name>
          //
          // A plugin slot is the one kind whose identity is not its kind, so
          // without this the remote can create the slot and never fill it.
          // Matched on the NAME as well as the id because an id is a full
          // path on Windows and nobody is typing that into a cue list.
          if (cue->audioEffects[index].kind !=
              deckboy::audiofx::AudioEffectKind::Plugin) {
            failRemoteCommand("AUDIOFX PLUGIN: slot " + parts[1] +
                              " is a " +
                              deckboy::audiofx::audioEffectToken(
                                cue->audioEffects[index].kind) +
                              ", not a plugin slot");
            return;
          }
          if (!deckboy::platform::audioplugin::audioPluginsSupported()) {
            failRemoteCommand("AUDIOFX PLUGIN: this build has no plugin host");
            return;
          }
          std::string wanted;
          for (std::size_t at = 3; at < parts.size(); ++at) {
            if (!wanted.empty()) wanted += " ";
            wanted += parts[at];
          }
          const auto& catalog = audioPluginCatalog(true);
          if (wanted.empty()) {
            // NAME THE OPTIONS, as AUDIOFX ADD does. An empty failure makes
            // the caller guess, and the guess is a file path.
            std::string some;
            for (std::size_t i = 0; i < catalog.size() && i < 6; ++i) {
              if (!some.empty()) some += ", ";
              some += catalog[i].name;
            }
            failRemoteCommand("AUDIOFX PLUGIN: needs a plugin name (" +
                              std::to_string(catalog.size()) +
                              " installed" +
                              (some.empty() ? ")" : ", e.g. " + some + ")"));
            return;
          }
          const std::string lowered = toLower(wanted);
          const deckboy::platform::audioplugin::PluginDescriptor* match = nullptr;
          for (const auto& d : catalog) {
            if (d.id == wanted || toLower(d.name) == lowered) {
              match = &d;
              break;
            }
            if (!match && toLower(d.name).find(lowered) != std::string::npos) {
              match = &d;   // keep looking for an exact one
            }
          }
          if (!match) {
            failRemoteCommand("AUDIOFX PLUGIN: no plugin matching '" + wanted +
                              "' among " + std::to_string(catalog.size()) +
                              " installed");
            return;
          }
          // Opened HERE so a bad plugin fails the command rather than the
          // show: the operator gets an error now instead of a silent slot
          // when the cue goes.
          auto probe = deckboy::platform::audioplugin::openAudioPlugin(
            match->id, 48000.0, 2048);
          if (!probe) {
            failRemoteCommand("AUDIOFX PLUGIN: " + match->name +
                              " would not load");
            return;
          }
          float seeded[4] = {0.5f, 0.5f, 0.5f, 0.5f};
          int taken = 0;
          for (const auto& p : probe->parameters()) {
            if (!p.automatable) {
              continue;
            }
            if (taken >= 4) {
              break;
            }
            seeded[taken++] = static_cast<float>(std::clamp(p.defaultValue, 0.0, 1.0));
          }
          const std::string id = match->id;
          forEachSelectedAudioStack(
            [index, &id, &seeded](std::vector<deckboy::audiofx::AudioEffect>& s) {
              if (index >= static_cast<int>(s.size()) ||
                  s[index].kind != deckboy::audiofx::AudioEffectKind::Plugin) {
                return;
              }
              s[index].pluginId = id;
              s[index].pluginState.clear();
              s[index].paramA = seeded[0];
              s[index].paramB = seeded[1];
              s[index].paramC = seeded[2];
              s[index].paramD = seeded[3];
            });
          remoteCommandDetail_ = match->name;
          return;
        }
        if (verb == "BYPASS") {
          const bool want = parts.size() < 4 || toUpper(parts[3]) != "OFF";
          forEachSelectedAudioStack(
            [index, want](std::vector<deckboy::audiofx::AudioEffect>& s) {
              if (index < static_cast<int>(s.size())) {
                s[index].bypassed = want;
              }
            });
          remoteCommandDetail_ = want ? "bypassed" : "active";
          return;
        }
      }
      // AUDIOFX <n> <amount%> [a% [b% [c% [d%]]]] -- every number is a
      // percentage, the same units the inspector rows and the read-back show.
      // Mixing 0-1 and 0-100 across the two is how somebody types 37 meaning
      // 37% and pins the parameter to its maximum.
      bool wrote = false;
      for (std::size_t at = 2; at < parts.size() && at < 7; ++at) {
        auto value = parseNumber(static_cast<int>(at));
        if (!value) {
          continue;
        }
        const float next = std::clamp(static_cast<float>(*value / 100.0), 0.0f, 1.0f);
        const std::size_t which = at - 2;   // 0 = amount, 1-4 = paramA-D
        forEachSelectedAudioStack(
          [index, which, next](std::vector<deckboy::audiofx::AudioEffect>& s) {
            if (index >= static_cast<int>(s.size())) {
              return;
            }
            auto& target = s[index];
            switch (which) {
              case 0: target.amount = next; break;
              case 1: target.paramA = next; break;
              case 2: target.paramB = next; break;
              case 3: target.paramC = next; break;
              default: target.paramD = next; break;
            }
          });
        wrote = true;
      }
      if (!wrote) {
        failRemoteCommand("AUDIOFX: expected OFF, BYPASS, or amount and "
                          "parameters as percentages");
        return;
      }
      remoteCommandDetail_ = deckboy::audiofx::audioEffectToken(
        cue->audioEffects[index].kind);
      return;
    }
    if (command == "SELECTALL") {
      // Ctrl+A's verb. Companion needs it to drive the multi-select edits, and
      // without it the cross-deck audio edits could not be exercised at all.
      selectAllCuesInFocusedDeck();
      const Deck& deck = focusedDeck();
      remoteCommandDetail_ = std::to_string(deck.selectedIndices.size()) + " selected";
      return;
    }
    if (command == "AUDIONORM") {
      // AUDIONORM ALL matches every cue in the focused deck, which is the
      // whole point of normalising: levels that agree with each other.
      if (parts.size() > 1 && toUpper(parts[1]) == "ALL") {
        normalizeAllCuesInFocusedDeck();
        return;
      }
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
    if (command == "SCALEMODE") {
      // How the source maps into the output: fit (letterbox), fill (cover and
      // crop), stretch (ignore aspect), unscaled (1:1). Until now this was
      // reachable only by clicking the inspector's cycle button -- so it could
      // not be driven from Companion, and it could not be TESTED without a
      // human at the desk, which is why "is fit/fill/stretch even working?"
      // had no cheap answer.
      //
      // A bare SCALEMODE is a question, like a bare AUDIOGAIN.
      const Cue* selected = selectedCuePtr();
      if (parts.size() < 2) {
        if (!selected) {
          failRemoteCommand("SCALEMODE: no cue selected");
          return;
        }
        remoteCommandDetail_ = scaleModeToken(selected->scaleMode);
        return;
      }
      const std::string want = toLower(trim(parts[1]));
      ScaleMode mode = ScaleMode::Fit;
      if (want == "fit")            mode = ScaleMode::Fit;
      else if (want == "fill")      mode = ScaleMode::Fill;
      else if (want == "stretch")   mode = ScaleMode::Stretch;
      else if (want == "unscaled" || want == "none" || want == "1:1")
                                    mode = ScaleMode::Unscaled;
      else {
        failRemoteCommand("SCALEMODE: expected fit|fill|stretch|unscaled");
        return;
      }
      bool changed = false;
      forEachFocusedSelectedCueMutable([&](Cue& each, int) {
        if (!cueSupportsGeometry(&each)) {
          return;
        }
        each.scaleMode = mode;
        changed = true;
      });
      if (!changed) {
        failRemoteCommand("SCALEMODE: selection has no cue with geometry");
        return;
      }
      markProjectDirty();
      remoteCommandDetail_ = scaleModeToken(mode);
      return;
    }
    if (command == "SCALE") {
      // Backward compatibility: SCALE sets both X and Y
      // A QUERY ANSWERS THE CALLER, the rule DECKOPACITY follows: with no value
      // this says what the selected cue's scale is.
      if (parts.size() <= 1) {
        if (const Cue* cue = selectedCueMutable()) {
          std::ostringstream ss;
          ss << std::fixed << std::setprecision(2) << cue->outputScaleX << " "
             << cue->outputScaleY;
          remoteCommandDetail_ = ss.str();
        } else {
          failRemoteCommand("SCALE: select a cue first");
        }
        return;
      }
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
