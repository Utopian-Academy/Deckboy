// ============================================================================
// app_network.ipp — Network integration methods for the App class.
//
// Implements the network protocols used for remote control and integration:
//
//   OSC server:
//     - UDP listener for Open Sound Control messages
//     - Subscriber tracking (auto-discovers Companion instances)
//     - OSC message encoding/sending for feedback and tally
//     - OSC Query server for endpoint discovery
//
//   Bitfocus Companion integration:
//     - Button state feedback (play/stop/cue colors)
//     - Tally reporting for active sources
//     - Two-way communication via OSC over UDP
//
//   ATEM tally bridge:
//     - Receives ATEM tally packets on the bridge port
//     - Maps tally sources to deck transport triggers
//
//   NMC (Network Master Clock) sync:
//     - Input mode: receives transport commands (play/stop/seek)
//     - Output mode: broadcasts transport state to followers
//
//   NDI trigger bridge:
//     - Receives NDI metadata frames as cue triggers
//
//   Art-Net DMX bridge:
//     - Receives Art-Net DMX packets for lighting trigger integration
//
//   LTC (Linear Time Code) ingest:
//     - Audio capture → LTC decode → timecode chase
//
// Cross-platform: uses Winsock2 on Windows, BSD sockets on POSIX.
// ALSA MIDI and LTC ingest remain platform-gated (DECKBOY_HAS_ALSA / libltc).
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Format a socket address as "host:port" for use as a map key.
  std::string oscSenderKey(const sockaddr_in& sender) const {
    char host[INET_ADDRSTRLEN] {};
    const char* text = inet_ntop(AF_INET, &sender.sin_addr, host, sizeof(host));
    if (!text) {
      return "unknown:" + std::to_string(ntohs(sender.sin_port));
    }
    return std::string(text) + ":" + std::to_string(ntohs(sender.sin_port));
  }

  void rememberOscSubscriber(const sockaddr_in& sender) {
    oscSubscribers_[oscSenderKey(sender)] = {sender, SDL_GetTicks()};
  }

  void sendOscStringTo(const sockaddr_in& target, const std::string& address, const std::string& payload) {
    if (companionUdpSocket_ == kInvalidSocket) {
      return;
    }
    std::vector<std::uint8_t> message = buildOscStringMessage(address, payload);
    sendto(
      companionUdpSocket_,
      reinterpret_cast<const char*>(message.data()),
      static_cast<int>(message.size()),
      0,
      reinterpret_cast<const sockaddr*>(&target),
      static_cast<socklen_t>(sizeof(target))
    );
  }

  std::string snapshotJsonForFeedback() {
    std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
    if (!statusSnapshotJson_.empty()) {
      return statusSnapshotJson_;
    }
    return "{\"app\":\"DECKBOY_0.01\",\"deckCount\":0,\"decks\":[]}\n";
  }

  std::vector<std::pair<std::string, std::string>> buildOscMirrorFeedbackValues() const {
    std::vector<std::pair<std::string, std::string>> values;
    values.reserve(16 + project_.decks.size() * 7 + project_.outputs.size() * 8);
    values.emplace_back("/deckboy/focus/deck", std::to_string(project_.focusedDeckIndex + 1));
    values.emplace_back("/deckboy/focus/output", std::to_string(project_.focusedOutputIndex + 1));
    values.emplace_back("/deckboy/decks/count", std::to_string(project_.decks.size()));
    values.emplace_back("/deckboy/outputs/count", std::to_string(project_.outputs.size()));
    values.emplace_back("/deckboy/jump_mode", normalizeJumpModeToken(project_.jumpMode));
    values.emplace_back("/deckboy/panic_profile", normalizePanicProfileToken(project_.panicProfile));
    values.emplace_back("/deckboy/integration/route", integrationBackendRouteSummary());
    values.emplace_back("/deckboy/integration/atem", project_.atemTriggerEnabled ? "1" : "0");
    values.emplace_back("/deckboy/integration/ndi_trigger", project_.ndiTriggerEnabled ? "1" : "0");
    values.emplace_back("/deckboy/integration/nmc", project_.nmcSyncEnabled ? "1" : "0");
    values.emplace_back("/deckboy/integration/mtc", project_.mtcIngestEnabled ? "1" : "0");
    values.emplace_back("/deckboy/integration/ltc", project_.ltcIngestEnabled ? "1" : "0");
    values.emplace_back("/deckboy/integration/dmx_artnet", project_.dmxArtNetEnabled ? "1" : "0");
    values.emplace_back("/deckboy/integration/artnet_port", std::to_string(project_.artNetPort));

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      const Deck& deck = project_.decks[deckIndex];
      std::string prefix = "/deckboy/deck/" + std::to_string(deckIndex + 1);
      values.emplace_back(prefix + "/status", transportStatusLabel(deckIndex));
      values.emplace_back(prefix + "/selected", std::to_string(deck.selectedIndex >= 0 ? deck.selectedIndex + 1 : 0));
      values.emplace_back(prefix + "/active", std::to_string(deck.activeIndex >= 0 ? deck.activeIndex + 1 : 0));
      values.emplace_back(prefix + "/opacity", std::to_string(static_cast<int>(std::lround(std::clamp(deck.playlistOpacity, 0.0f, 1.0f) * 100.0f))));
      auto outputIndex = primaryOutputIndexForDeck(deckIndex);
      values.emplace_back(prefix + "/route_output", std::to_string(outputIndex ? *outputIndex + 1 : 0));
      values.emplace_back(prefix + "/layer", std::to_string(primaryLayerIndexForDeck(deckIndex)));
      values.emplace_back(prefix + "/warp_mode", normalizeWarpMode(deck.warpMode));
    }

    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      const OutputTarget& output = project_.outputs[outputIndex];
      std::string prefix = "/deckboy/output/" + std::to_string(outputIndex + 1);
      values.emplace_back(prefix + "/enabled", output.enabled ? "1" : "0");
      values.emplace_back(prefix + "/type", normalizeOutputType(output.outputType));
      values.emplace_back(prefix + "/ndi", output.ndiEnabled ? "1" : "0");
      values.emplace_back(prefix + "/stream", output.streamEnabled ? "1" : "0");
      values.emplace_back(prefix + "/alpha", std::to_string(static_cast<int>(std::lround(std::clamp(output.outputAlpha, 0.0f, 1.0f) * 100.0f))));
      values.emplace_back(prefix + "/layout", normalizeOutputLayoutMode(output.outputLayoutMode));
      values.emplace_back(prefix + "/orientation", std::to_string(normalizeOutputOrientationDegrees(output.outputOrientationDegrees)));
      values.emplace_back(prefix + "/testcard", output.outputTestCardEnabled ? "1" : "0");
    }
    return values;
  }

  std::string buildOscQueryDocumentJson() {
    std::string stateJson = trim(snapshotJsonForFeedback());
    if (stateJson.empty()) {
      stateJson = "{\"app\":\"DECKBOY_0.01\",\"deckCount\":0,\"decks\":[]}";
    }

    std::ostringstream output;
    output << "{"
           << "\"name\":\"Deckboy OSC Query\","
           << "\"app\":\"DECKBOY_0.01\","
           << "\"oscUdpPort\":" << companionPort_ << ","
           << "\"httpPort\":" << project_.oscQueryPort << ","
           << "\"feedbackMirror\":" << (project_.oscFeedbackMirrorEnabled ? "true" : "false") << ","
           << "\"feedbackRateMs\":" << project_.oscFeedbackRateMs << ","
           << "\"integrationRoute\":\"" << escapeJson(integrationBackendRouteSummary()) << "\","
           << "\"endpoints\":[";
    for (size_t i = 0; i < kOscQueryEndpoints.size(); ++i) {
      if (i > 0) {
        output << ",";
      }
      const auto& endpoint = kOscQueryEndpoints[i];
      output << "{"
             << "\"path\":\"" << escapeJson(endpoint.path) << "\","
             << "\"command\":\"" << escapeJson(endpoint.command) << "\","
             << "\"args\":\"" << escapeJson(endpoint.args) << "\","
             << "\"notes\":\"" << escapeJson(endpoint.notes) << "\""
             << "}";
    }
    output << "],"
           << "\"state\":" << stateJson
           << "}\n";
    return output.str();
  }

  std::string buildOscQueryHtmlPage() {
    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset='utf-8'>"
         << "<title>Deckboy OSC Query</title>"
         << "<style>"
         << "body{font-family:monospace;background:#0f380f;color:#9bbc0f;margin:0;padding:18px;}"
         << "h1{margin:0 0 6px 0;font-size:22px;}"
         << "p{margin:4px 0;}"
         << "a{color:#c9d7a3;text-decoration:none;}"
         << "table{border-collapse:collapse;width:100%;margin-top:10px;}"
         << "th,td{border:1px solid #306230;padding:6px;text-align:left;font-size:12px;}"
         << "th{background:#306230;color:#9bbc0f;}"
         << "pre{background:#101410;border:1px solid #306230;padding:8px;overflow:auto;}"
         << "</style></head><body>";
    html << "<h1>DECKBOY OSC QUERY</h1>";
    html << "<p>OSC UDP Port: " << companionPort_ << " | HTTP Port: " << project_.oscQueryPort << "</p>";
    html << "<p>Feedback Mirror: " << (project_.oscFeedbackMirrorEnabled ? "ON" : "OFF")
         << " @ " << project_.oscFeedbackRateMs << " ms</p>";
    html << "<p><a href='/oscquery.json'>/oscquery.json</a> | <a href='/state.json'>/state.json</a></p>";
    html << "<table><tr><th>Path</th><th>Command</th><th>Args</th><th>Notes</th></tr>";
    for (const auto& endpoint : kOscQueryEndpoints) {
      html << "<tr><td>" << escapeHtml(endpoint.path) << "</td>"
           << "<td>" << escapeHtml(endpoint.command) << "</td>"
           << "<td>" << escapeHtml(endpoint.args) << "</td>"
           << "<td>" << escapeHtml(endpoint.notes) << "</td></tr>";
    }
    html << "</table>";
    html << "<h2>State</h2><pre>" << escapeHtml(trim(snapshotJsonForFeedback())) << "</pre>";
    html << "</body></html>";
    return html.str();
  }

  void sendHttpResponse(SocketHandle client,
                        const std::string& statusLine,
                        const std::string& contentType,
                        const std::string& body) {
    std::ostringstream header;
    header << "HTTP/1.1 " << statusLine << "\r\n"
           << "Content-Type: " << contentType << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n"
           << "Cache-Control: no-cache\r\n"
           << "\r\n";
    std::string response = header.str();
    response += body;
    send(client, response.c_str(), static_cast<int>(response.size()), kSocketSendFlags);
  }

  void handleOscQueryHttpClient(SocketHandle client) {
    std::string request;
    std::array<char, 2048> buffer {};
    // 5-second total deadline prevents slow-loris attacks from blocking the server thread
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
      auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
        deadline - std::chrono::steady_clock::now());
      if (remaining.count() <= 0) break;  // deadline expired
      fd_set readFds;
      FD_ZERO(&readFds);
      FD_SET(client, &readFds);
      timeval tv {};
      tv.tv_sec = static_cast<long>(remaining.count() / 1000000);
      tv.tv_usec = static_cast<long>(remaining.count() % 1000000);
      if (select(selectNfds(client), &readFds, nullptr, nullptr, &tv) <= 0) break;
      int bytes = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
      if (bytes <= 0) {
        break;
      }
      request.append(buffer.data(), static_cast<size_t>(bytes));
    }
    if (request.empty()) {
      return;
    }

    size_t lineEnd = request.find("\r\n");
    if (lineEnd == std::string::npos) {
      lineEnd = request.find('\n');
    }
    std::string requestLine = lineEnd == std::string::npos ? request : request.substr(0, lineEnd);
    auto parts = splitWhitespace(requestLine);
    if (parts.size() < 2 || toUpper(parts[0]) != "GET") {
      sendHttpResponse(client, "405 Method Not Allowed", "text/plain; charset=utf-8",
                       "Only GET is supported.\n");
      return;
    }

    std::string path = parts[1];
    size_t queryPos = path.find('?');
    if (queryPos != std::string::npos) {
      path = path.substr(0, queryPos);
    }
    if (path.empty()) {
      path = "/";
    }

    if (path == "/") {
      sendHttpResponse(client, "200 OK", "text/html; charset=utf-8", buildOscQueryHtmlPage());
      return;
    }
    if (path == "/oscquery" || path == "/oscquery.json") {
      sendHttpResponse(client, "200 OK", "application/json; charset=utf-8", buildOscQueryDocumentJson());
      return;
    }
    if (path == "/state" || path == "/state.json") {
      sendHttpResponse(client, "200 OK", "application/json; charset=utf-8", snapshotJsonForFeedback());
      return;
    }

    sendHttpResponse(client, "404 Not Found", "text/plain; charset=utf-8",
                     "Deckboy OSC Query endpoint not found.\n");
  }

  bool startOscQueryServer() {
    if (!project_.oscQueryEnabled) {
      oscQueryReady_ = false;
      return false;
    }
    if (oscQueryTcpListen_ != kInvalidSocket) {
      return true;
    }

    oscQueryTcpListen_ = createBoundSocket(SOCK_STREAM, project_.oscQueryPort, true, !project_.allowRemoteNetwork);
    if (oscQueryTcpListen_ == kInvalidSocket) {
      oscQueryReady_ = false;
      return false;
    }

    oscQueryStop_.store(false);
    oscQueryThread_ = std::thread([this]() {
      oscQueryLoop();
    });
    oscQueryReady_ = true;
    return true;
  }

  void stopOscQueryServer() {
    oscQueryStop_.store(true);
    if (oscQueryTcpListen_ != kInvalidSocket) {
      closeSocket(oscQueryTcpListen_);
      oscQueryTcpListen_ = kInvalidSocket;
    }
    if (oscQueryThread_.joinable()) {
      oscQueryThread_.join();
    }
    oscQueryReady_ = false;
  }

  void oscQueryLoop() {
    while (!oscQueryStop_.load()) {
      if (oscQueryTcpListen_ == kInvalidSocket) {
        break;
      }
      fd_set readFds;
      FD_ZERO(&readFds);
      FD_SET(oscQueryTcpListen_, &readFds);
      timeval timeout {};
      timeout.tv_sec = 0;
      timeout.tv_usec = 200000;
      int ready = select(selectNfds(oscQueryTcpListen_), &readFds, nullptr, nullptr, &timeout);
      if (ready < 0) {
#ifndef _WIN32
        if (errno == EINTR) {
          continue;
        }
#endif
        break;
      }
      if (ready == 0) {
        continue;
      }
      if (!FD_ISSET(oscQueryTcpListen_, &readFds)) {
        continue;
      }

      sockaddr_in clientAddress {};
      socklen_t clientLength = sizeof(clientAddress);
      SocketHandle client = accept(oscQueryTcpListen_, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
      if (client == kInvalidSocket) {
        continue;
      }
      setCloseOnExec(client);
      handleOscQueryHttpClient(client);
      closeSocket(client);
    }
  }

  void maybeBroadcastOscState() {
    Uint64 now = SDL_GetTicks();
    if (oscSubscribers_.empty()) {
      return;
    }

    std::string snapshot = snapshotJsonForFeedback();
    bool stateChanged = snapshot != lastOscFeedbackPayload_;
    bool shouldBroadcastState = stateChanged || now - lastOscFeedbackBroadcastMs_ >= 2000;

    std::vector<std::pair<std::string, std::string>> mirrorValues;
    std::string mirrorDigest;
    bool mirrorChanged = false;
    Uint64 mirrorInterval = static_cast<Uint64>(normalizeOscFeedbackRateMs(project_.oscFeedbackRateMs));
    bool shouldBroadcastMirror = false;
    if (project_.oscFeedbackMirrorEnabled) {
      mirrorValues = buildOscMirrorFeedbackValues();
      std::ostringstream mirrorDigestStream;
      for (const auto& value : mirrorValues) {
        mirrorDigestStream << value.first << '=' << value.second << '\n';
      }
      mirrorDigest = mirrorDigestStream.str();
      mirrorChanged = mirrorDigest != lastOscMirrorFeedbackPayload_;
      shouldBroadcastMirror =
        (mirrorChanged && now - lastOscMirrorFeedbackBroadcastMs_ >= mirrorInterval) ||
        (!mirrorChanged && now - lastOscMirrorFeedbackBroadcastMs_ >= 2000);
    }

    if (!shouldBroadcastState && !shouldBroadcastMirror) {
      return;
    }

    std::vector<std::string> stale;
    for (const auto& [key, entry] : oscSubscribers_) {
      if (now > entry.second + 30000) {
        stale.push_back(key);
        continue;
      }
      if (shouldBroadcastState) {
        sendOscStringTo(entry.first, "/deckboy/state", snapshot);
      }
      if (shouldBroadcastMirror) {
        for (const auto& value : mirrorValues) {
          sendOscStringTo(entry.first, value.first, value.second);
        }
      }
    }
    for (const auto& key : stale) {
      oscSubscribers_.erase(key);
    }

    if (shouldBroadcastState) {
      lastOscFeedbackPayload_ = snapshot;
      lastOscFeedbackBroadcastMs_ = now;
    }
    if (shouldBroadcastMirror) {
      lastOscMirrorFeedbackPayload_ = mirrorDigest;
      lastOscMirrorFeedbackBroadcastMs_ = now;
    }
  }

  bool maybeRespondToCompanionQuery(SocketHandle client, const std::string& line) {
    std::string query = trim(line);
    std::string upper = toUpper(query);

    auto sendSnapshot = [&](const std::string& payload) {
      if (!payload.empty()) {
        // Winsock's send takes an int length; a reply is never near that, but
        // saying so beats a silent narrowing.
        send(client, payload.c_str(), static_cast<int>(payload.size()), kSocketSendFlags);
      }
    };

    if (upper == "STATUS ENCODER" || upper == "STATE ENCODER") {
      // The encode queue, readable from a script or Companion. Progress is a
      // percentage, or "--" before ffmpeg has reported anything.
      std::string reply = "DECKBOY_0.01 encoder jobs=" +
                          std::to_string(static_cast<int>(conversionJobs_.size())) +
                          " concurrency=" + std::to_string(encoderConcurrency_) +
                          (encoderQueuePaused_ ? " paused=on" : " paused=off") + "\n";
      for (const auto& job : conversionJobs_) {
        double pct = job.progress ? job.progress->load() : -1.0;
        const char* st = job.state == ConversionState::Running ? "running"
                       : job.state == ConversionState::Queued  ? "queued" : "done";
        reply += std::string("JOB state=") + st + " progress=" +
                 (pct < 0.0 ? std::string("--")
                            : std::to_string(static_cast<int>(pct * 100.0 + 0.5))) +
                 " name=\"" + job.label + "\"\n";
      }
      sendSnapshot(reply);
      return true;
    }
    // EVERY verb, for anyone building a surface. The summary below is what
    // fits on a screen and covers a quarter of the protocol; a Companion or
    // Stream Deck author reading only that would never learn that PIP,
    // LOWERTHIRD, TIMER, SUBTITLE, ENCODE, SCHEDULE or MULTIVIEW exist.
    //
    // Kept honest by tools/audit_remote_help.py, which fails the build when a
    // verb the dispatcher handles is missing from this list. That is the only
    // reason a hand-maintained list is defensible: nothing here can drift
    // without something saying so.
    if (upper == "HELP ALL" || upper == "HELP FULL" || upper == "?? ") {
      sendSnapshot(
        "DECKBOY_0.01 every verb (264)\n"
        "ADDTIMER ALLGO ALLPAUSE ALLPLAY ALLSTOP ALLTAKE ANIM ANIMATION\n"
        "ARTNET ARTNETEVENT ARTNETPORT ART_NET_PORT ASCII ATEM ATEMEVENT\n"
        "ATEMTRIGGER AUDIO AUDIOCUE AUDIOENABLED AUDIOGAIN AUDIOMONO\n"
        "AUDIONORM AUDIOOUTS AUDIOPAN AUDIOVIS AUDIOVISUAL AUTOADVANCE\n"
        "AUTOID AUTONEXT BLACKOUT\n"
        "BLEND BROWSER CAMERACUE CANVAS CC CLEAR CLEAROVERLAY CODE COLOR\n"
        "COLORTAG COMPOSITE CONVERT CUEAUDIO CUEAUTOID CUEFIND CUEFINDCLEAR\n"
        "CUEFINDNEXT CUEFINDPREV CUEFINDSTATUS CUEFINDTAKE CUEGOTO CUEID\n"
        "CUEIDSHORT CUENOTES CUESHORTID CUEXNEXT DATAMOSH DECK DECKADD\n"
        "DECKAUTOFADE DECKDIM DECKFADE DECKLINK DECKNEXT DECKOPACITY\n"
        "DECKPREV DECKPREVIOUS DELETE DIMMER DISPLAY DMX DMXARTNET DMXPORT\n"
        "DMX_ARTNET DURATION ENCODE ENCODEALL ENCODEFORMAT ENCODEPAUSE\n"
        "ENCODEPRESET ENDACTION FADEIN FADEOUT FIND FINDCLEAR FINDNEXT\n"
        "FINDPREV FINDPREVIOUS FINDRESET FINDSTATUS FINDTAKE FULLSCREEN FX\n"
        "GO GOEND GOTO GOTOTARGET GRAPHIC GROUP GROUPPRESET HEIGHT HOLD\n"
        "HOLDLAST IN INTEGRATION INTEGRATIONS JUMPMODE JUMPTRANS\n"
        "JUMPTRANSITION JUMP_MODE JUMP_XFADE LAYER LAYERNAME LOOP LOOPCOUNT\n"
        "LOWERALPHA LOWERSUB LOWERTEXT LOWERTHIRD LTC LTCEXT LTCINGEST\n"
        "LTCOUT LTC_INGEST MARK MARKER MASTER MASTERCUE MASTERVOL\n"
        "MASTERVOLUME MIDI MIDIINPUT MIDI_INPUT MOSH MOSHLOOK MTC MTCEXT\n"
        "MTCINGEST MTC_INGEST MULTIVIEW NDI NDICUE NDIEVENT NDIKEY NDIKEYER\n"
        "NDIKEYNAME NDINAME NDITRIGGER NDI_TRIGGER NEWDECK NEXT NEXTTRANS\n"
        "NMC NMCEVENT NMCSYNC NMC_SYNC NMOS OSCFEEDBACK OSCFEEDBACKRATE\n"
        "OSCQUERY OSCQUERYPORT OSC_FEEDBACK OSC_FEEDBACK_RATE OSC_QUERY\n"
        "DASH DASHBOARD GET OUTPUT SET\n"
        "OSC_QUERY_PORT OUT OUTPUTMODE OVERLAY PANIC PANICAUTORESTORE\n"
        "PANICFADE PANICPROFILE PANIC_FADE PANIC_PROFILE PANIC_RESTORE\n"
        "PATTERN PAUSE PAUSEATBEGIN PAUSEATEND PAUSEBEGIN PAUSEEND\n"
        "PAUSESTART PING PIP PLAY PLAYLISTAUTOFADE PLAYLISTFADE PLAYLISTLOOP\n"
        "PLAYLISTOPACITY PRESET PREV PREVIOUS REC RECCODEC RECFORMAT RECORD\n"
        "RECSEGMENT RECTC RENUMBER RERACK ROUTE SCALE SCALEX SCALEY SCENE\n"
        "SCHEDULE SECTION SEEK SEEKPOS SELECT SELECTID SFX SHORTID SHUFFLE\n"
        "SKIP SKIPBACK SKIPEND SOURCE SPEED SPOUTCUE SRC ST2110 STILLDUR\n"
        "STOP SUB SUBTITLE SUBTITLES SYNCGO SYNCTAKE SYNTHNOTEOFF\n"
        "SYNTHNOTEON SYPHONCUE TAKE TAKEID TC TCMARK TIMECODE TIMECODEEXT\n"
        "TIMECODELTC TIMECODEMARK TIMEOVERLAY TIMER TIMERCUE TOGGLE\n"
        "TRANSITION TRANSITIONSTYLE TRANSITIONTONEXT TRIM TRIMIN TRIMOUT\n"
        "UPDATE VIDEO VIEW VJ VOLUME WARP WIDTH WINDOWSOURCE XFADE\n"
        "cue indices are 1-based; every command answers OK or ERR.\n"
      );
      return true;
    }
    if (upper == "HELP" || upper == "?") {
      // Discoverability over the wire. The only way to learn the protocol used
      // to be reading the source or guessing, and a guess that missed looked
      // exactly like a broken command.
      sendSnapshot(
        "DECKBOY_0.01 help\n"
        "queries (answered immediately): STATUS | STATUS JSON | STATUS CUES | STATUS <deck> | FINDSTATUS | HELP\n"
        "transport: TAKE GO PLAY PAUSE STOP TOGGLE RERACK CLEAR SKIP SKIPBACK GOEND NEXT PREV SEEK <s> LOOP <on|off>\n"
        "navigation: SELECT <n> GOTO <n> FIND <text> FINDNEXT FINDTAKE DECK <n> [command] DECKNEXT DECKPREV\n"
        "show: PANIC ALLSTOP ALLPAUSE BLACKOUT [on|off|toggle] DIMMER <0-100> SHUFFLE <on|off>\n"
        "audio: MASTERVOL <0-200 percent> VOLUME <0-100> AUDIOGAIN <dB> AUDIONORM SPEED <0.25-4>\n"
        "       AUDIOVIS [waveform|scope|lissajous|spectrum|level|cover]  (what an audio cue shows)\n"
        "output: OUTPUT [STATUS|LIST|ON|OFF|TOGGLE|SPOUT ...|<n>]\n"
        "settings: GET [key] | SET <key> <value>   (GET alone lists all 86)\n"
        "dashboard: DASH LIST | DASH <n> | DASH SET <n> <label> | <cmd> [| <glyph>]\n"
        "           DASH CLEAR <n>   (a slot runs any command line above)\n"
        "        FULLSCREEN DISPLAY <n> TC <hh:mm:ss:ff>   (OUT is trim-out)\n"
        "vj: VJ ON|OFF|TOGGLE | VJ MIX <0-1> | VJ BLEND [mode]\n"
        "    blends: dissolve add screen multiply lighten darken subtract\n"
        "            undercut infiltrate ember   (no argument cycles)\n"
        "    VJ TAP | VJ BPM <n> | VJ QUANTISE <on|off> | VJ DECKS <a> <b> | VJ STATUS\n"
        "effects: FX LIST | FX ADD <effect> [amount] | FX AMOUNT <n> <0-1> | FX CLEAR\n"
        "         FX PARAM <n> <A-D> <0-1>   (each effect's own shaping controls)\n"
        "         FX LFO <n> <A-E> on|off|shape|rate|depth|phase|sync|beats [v]\n"
        "         FX COPY | FX PASTE   (the chain only, not geometry or fades)\n"
        "code: CODE GET | CODE SET <expression> | CODE EDIT\n"
        "text mode: ASCII ON|OFF|TOGGLE | STATUS | INK | SET | SHUFFLE | PRESET\n"
        "           ASCII FONT | GLYPHS [chars] | CHAOS <0-1> | WOBBLE <0-1>\n"
        "           ASCII WOBBLEMODE [drift|flow|hue] | COLS <n> | GLITCH | PHRASES | HOLD\n"
        "record: RECORD [on|off|toggle]  (same action as the RECORD button on the bar)\n"
        "        RECFORMAT <WxH|program> [fps|program]  (recording standard + rate)\n"
        "        RECCODEC <h264|hevc|prores_lt|prores_422|prores_hq|prores_4444|dnxhr_lb|dnxhr_sq|dnxhr_hq|dnxhr_hqx>\n"
        "        RECTC <hh:mm:ss:ff|timeofday> [df|ndf|auto]   RECSEGMENT <minutes> [megabytes]\n"
        "cue indices are 1-based; every command answers 'OK <VERB>' or 'ERR unknown command: <VERB>'\n");
      return true;
    }
    if (upper == "STATUS JSON" || upper == "STATE JSON") {
      std::string snapshotJson;
      {
        std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
        snapshotJson = statusSnapshotJson_;
      }
      if (snapshotJson.empty()) {
        snapshotJson = "{\"app\":\"DECKBOY_0.01\",\"deckCount\":0,\"decks\":[]}\n";
      }
      sendSnapshot(snapshotJson);
      return true;
    }
    if (upper == "STATUS" || upper == "STATE" || upper == "STATUS ALL" || upper == "STATE ALL") {
      std::string snapshot;
      {
        std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
        snapshot = statusSnapshot_;
      }
      if (snapshot.empty()) {
        snapshot = "DECKBOY_0.01 decks=0\n";
      }
      sendSnapshot(snapshot);
      return true;
    }
    if (upper == "STATUS CUES" || upper == "STATE CUES") {
      std::string cueSnapshot;
      {
        std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
        cueSnapshot = statusCueSnapshot_;
      }
      if (cueSnapshot.empty()) {
        cueSnapshot = "DECKBOY_0.01 cues decks=0\n";
      }
      sendSnapshot(cueSnapshot);
      return true;
    }
    if (upper == "FINDSTATUS" || upper == "STATUS FIND" || upper == "STATE FIND") {
      std::string cueSnapshot;
      {
        std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
        cueSnapshot = statusCueSnapshot_;
      }
      if (cueSnapshot.empty()) {
        cueSnapshot = "DECKBOY_0.01 cues find=none\n";
      } else {
        size_t newline = cueSnapshot.find('\n');
        cueSnapshot = newline == std::string::npos
          ? cueSnapshot + "\n"
          : cueSnapshot.substr(0, newline + 1);
      }
      sendSnapshot(cueSnapshot);
      return true;
    }

    if (upper.rfind("STATUS ", 0) == 0 || upper.rfind("STATE ", 0) == 0) {
      auto parts = splitWhitespace(query);
      if (parts.size() >= 2) {
        try {
          int deckIndex = std::stoi(parts[1]) - 1;
          std::string deckSnapshot;
          {
            std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
            if (deckIndex >= 0 && deckIndex < static_cast<int>(statusDeckSnapshots_.size())) {
              deckSnapshot = statusDeckSnapshots_[deckIndex];
            }
          }
          if (!deckSnapshot.empty()) {
            sendSnapshot(deckSnapshot);
            return true;
          }
        } catch (...) {
        }
      }
    }

    return false;
  }

  bool startCompanionControl() {
    const char* portEnv = std::getenv("DECKBOY_COMPANION_PORT");
    if (portEnv && *portEnv) {
      try {
        companionPort_ = std::clamp(std::stoi(portEnv), 1, 65535);
      } catch (...) {
        companionPort_ = 5510;
      }
    }

    companionTcpListen_ = createBoundSocket(SOCK_STREAM, companionPort_, true, !project_.allowRemoteNetwork);
    companionUdpSocket_ = createBoundSocket(SOCK_DGRAM, companionPort_, false, !project_.allowRemoteNetwork);
    if (companionTcpListen_ == kInvalidSocket || companionUdpSocket_ == kInvalidSocket) {
      closeSocket(companionTcpListen_);
      closeSocket(companionUdpSocket_);
      companionTcpListen_ = kInvalidSocket;
      companionUdpSocket_ = kInvalidSocket;
      companionReady_ = false;
      // Windows silently reserves shifting port ranges for Hyper-V/WinNAT
      // (netsh int ipv4 show excludedportrange) — the bind fails with
      // WSAEACCES while netstat shows the port free. Without this warning
      // the operator's Companion/remote control just dies invisibly.
      triggerToast("COMPANION PORT " + std::to_string(companionPort_) +
                   " UNAVAILABLE - remote control offline (change port in settings)");
      std::cerr << "companion-control: could not bind tcp/udp port " << companionPort_
                << " (port in use or inside a Windows excluded port range)\n";
      return false;
    }

    companionStop_.store(false);
    companionThread_ = std::thread([this]() {
      companionLoop();
    });
    companionReady_ = true;
    return true;
  }

  void stopCompanionControl() {
    companionStop_.store(true);
    if (companionThread_.joinable()) {
      companionThread_.join();
    }
    {
      std::lock_guard<std::mutex> lk(companionClientsMutex_);
      for (auto client : companionClients_) {
        closeSocket(client);
      }
      companionClients_.clear();
      companionClientBuffers_.clear();
      companionDrainingClients_.clear();  // sockets already closed by the loop above
    }
    oscSubscribers_.clear();
    lastOscFeedbackPayload_.clear();
    lastOscFeedbackBroadcastMs_ = 0;
    lastOscMirrorFeedbackPayload_.clear();
    lastOscMirrorFeedbackBroadcastMs_ = 0;
    closeSocket(companionTcpListen_);
    closeSocket(companionUdpSocket_);
    companionTcpListen_ = kInvalidSocket;
    companionUdpSocket_ = kInvalidSocket;
    companionReady_ = false;
  }

  void startIntegrationBridges() {
    startTslTally();
    startAtemBridgeListener();
    startArtNetBridgeListener();
    refreshNmcSyncState();
    refreshNdiTriggerBridgeState();
    refreshLtcCaptureState();
  }

  void stopIntegrationBridges() {
    stopTslTally();
    stopLtcIngest();
    stopNmcSyncBridge();
    stopNdiTriggerBridge();
    stopAtemBridgeListener();
    stopArtNetBridgeListener();
  }

  int resolvedAtemBridgePort() const {
    int port = kDefaultAtemBridgePort;
    const char* env = std::getenv("DECKBOY_ATEM_BRIDGE_PORT");
    if (env && *env) {
      try {
        port = std::stoi(env);
      } catch (...) {
      }
    }
    return std::clamp(port, 1, 65535);
  }

  int resolvedArtNetBridgePort() const {
    return normalizeArtNetPort(project_.artNetPort);
  }

  std::string resolvedNmcSyncMode() const {
    const char* env = std::getenv("DECKBOY_NMC_MODE");
    return normalizeNmcSyncModeToken(env ? env : "");
  }

  int resolvedNmcSyncPort() const {
    int port = kDefaultNmcSyncPort;
    const char* env = std::getenv("DECKBOY_NMC_PORT");
    if (env && *env) {
      try {
        port = std::stoi(env);
      } catch (...) {
      }
    }
    return std::clamp(port, 1, 65535);
  }

  int resolvedNmcSyncLocateIntervalMs() const {
    int intervalMs = kDefaultNmcLocateIntervalMs;
    const char* env = std::getenv("DECKBOY_NMC_LOCATE_MS");
    if (env && *env) {
      try {
        intervalMs = std::stoi(env);
      } catch (...) {
      }
    }
    return std::clamp(intervalMs, 60, 2000);
  }

  std::string resolvedNmcSyncTargetHost() const {
    const char* env = std::getenv("DECKBOY_NMC_HOST");
    std::string value = env ? trim(env) : std::string();
    return value.empty() ? "255.255.255.255" : value;
  }

  std::string resolvedNmcSyncSourceFilter() const {
    const char* env = std::getenv("DECKBOY_NMC_SOURCE");
    return env ? trim(env) : std::string();
  }

  bool nmcSourceMatchesFilter(const std::string& sender, const std::string& filter) const {
    if (filter.empty()) {
      return true;
    }
    std::string senderUpper = toUpper(sender);
    std::string filterUpper = toUpper(filter);
    if (filterUpper == "LOCALHOST") {
      return sender == "127.0.0.1";
    }
    return senderUpper == filterUpper || senderUpper.find(filterUpper) != std::string::npos;
  }

  bool resolveNmcSyncTargetAddress(sockaddr_in* outAddress, std::string* error) const {
    if (!outAddress) {
      return false;
    }
    std::memset(outAddress, 0, sizeof(sockaddr_in));
    outAddress->sin_family = AF_INET;
    outAddress->sin_port = htons(static_cast<std::uint16_t>(resolvedNmcSyncPort()));

    std::string host = resolvedNmcSyncTargetHost();
    if (host.empty()) {
      if (error) {
        *error = "nmc host missing";
      }
      return false;
    }

    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* results = nullptr;
    int status = getaddrinfo(host.c_str(), nullptr, &hints, &results);
    if (status != 0 || !results) {
      if (error) {
        *error = "nmc host resolve failed";
      }
      if (results) {
        freeaddrinfo(results);
      }
      return false;
    }

    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(results->ai_addr);
    outAddress->sin_addr = ipv4->sin_addr;
    freeaddrinfo(results);
    return true;
  }

  std::string describeNmcSyncRuntime() const {
    if (!project_.nmcSyncEnabled) {
      return "off";
    }
    std::ostringstream summary;
    std::string mode = resolvedNmcSyncMode();
    summary << mode << '@' << resolvedNmcSyncPort();
    if (mode == "output") {
      summary << " -> " << resolvedNmcSyncTargetHost();
    } else {
      std::string filter = resolvedNmcSyncSourceFilter();
      if (!filter.empty()) {
        summary << " from " << filter;
      }
    }
    return summary.str();
  }

  void startAtemBridgeListener() {
    stopAtemBridgeListener();
    atemBridgeListenPort_ = resolvedAtemBridgePort();
    atemBridgeSocket_ = createBoundSocket(SOCK_DGRAM, atemBridgeListenPort_, false, !project_.allowRemoteNetwork);
    if (atemBridgeSocket_ == kInvalidSocket) {
      return;
    }
    atemBridgeStop_.store(false);
    atemBridgeThread_ = std::thread([this]() { atemBridgeLoop(); });
  }

  void stopAtemBridgeListener() {
    atemBridgeStop_.store(true);
    if (atemBridgeSocket_ != kInvalidSocket) {
      closeSocket(atemBridgeSocket_);
      atemBridgeSocket_ = kInvalidSocket;
    }
    if (atemBridgeThread_.joinable()) {
      atemBridgeThread_.join();
    }
  }

  void atemBridgeLoop() {
    while (!atemBridgeStop_.load()) {
      fd_set readFds;
      FD_ZERO(&readFds);
      FD_SET(atemBridgeSocket_, &readFds);
      timeval timeout {};
      timeout.tv_sec = 0;
      timeout.tv_usec = 200000;
      int ready = select(selectNfds(atemBridgeSocket_), &readFds, nullptr, nullptr, &timeout);
      if (ready <= 0) {
        continue;
      }
      if (!FD_ISSET(atemBridgeSocket_, &readFds)) {
        continue;
      }
      std::array<char, 1024> buffer {};
      sockaddr_in sourceAddr {};
      socklen_t sourceLen = sizeof(sourceAddr);
      int bytes = recvfrom(
        atemBridgeSocket_,
        buffer.data(),
        static_cast<int>(buffer.size() - 1),
        0,
        reinterpret_cast<sockaddr*>(&sourceAddr),
        &sourceLen);
      if (bytes <= 0) {
        continue;
      }
      buffer[bytes] = '\0';
      std::string payload = trim(std::string(buffer.data(), static_cast<size_t>(bytes)));
      if (payload.empty()) {
        continue;
      }
      enqueueRemoteCommand("ATEMEVENT " + payload);
    }
  }

  void startArtNetBridgeListener() {
    stopArtNetBridgeListener();
    artNetListenPort_ = resolvedArtNetBridgePort();
    artNetSocket_ = createBoundSocket(SOCK_DGRAM, artNetListenPort_, false, !project_.allowRemoteNetwork);
    if (artNetSocket_ == kInvalidSocket) {
      return;
    }
    std::fill(artNetLastDmx_.begin(), artNetLastDmx_.end(), 0);
    artNetBridgeStop_.store(false);
    artNetBridgeThread_ = std::thread([this]() { artNetBridgeLoop(); });
  }

  void stopArtNetBridgeListener() {
    artNetBridgeStop_.store(true);
    if (artNetSocket_ != kInvalidSocket) {
      closeSocket(artNetSocket_);
      artNetSocket_ = kInvalidSocket;
    }
    if (artNetBridgeThread_.joinable()) {
      artNetBridgeThread_.join();
    }
  }

  void restartArtNetBridgeListener() {
    if (artNetSocket_ == kInvalidSocket && !artNetBridgeThread_.joinable()) {
      startArtNetBridgeListener();
      return;
    }
    stopArtNetBridgeListener();
    startArtNetBridgeListener();
  }

  void artNetBridgeLoop() {
    while (!artNetBridgeStop_.load()) {
      fd_set readFds;
      FD_ZERO(&readFds);
      FD_SET(artNetSocket_, &readFds);
      timeval timeout {};
      timeout.tv_sec = 0;
      timeout.tv_usec = 200000;
      int ready = select(selectNfds(artNetSocket_), &readFds, nullptr, nullptr, &timeout);
      if (ready <= 0) {
        continue;
      }
      if (!FD_ISSET(artNetSocket_, &readFds)) {
        continue;
      }
      std::array<std::uint8_t, 1024> packet {};
      sockaddr_in sourceAddr {};
      socklen_t sourceLen = sizeof(sourceAddr);
      int bytes = recvfrom(
        artNetSocket_,
        reinterpret_cast<char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<sockaddr*>(&sourceAddr),
        &sourceLen);
      if (bytes < 18) {
        continue;
      }
      if (std::memcmp(packet.data(), "Art-Net\0", 8) != 0) {
        continue;
      }
      std::uint16_t opCode = static_cast<std::uint16_t>(packet[8]) |
                             (static_cast<std::uint16_t>(packet[9]) << 8);
      if (opCode != 0x5000) {  // ArtDMX
        continue;
      }
      int length = (static_cast<int>(packet[16]) << 8) | static_cast<int>(packet[17]);
      length = std::clamp(length, 0, std::min(512, bytes - 18));
      const std::uint8_t* data = packet.data() + 18;

      for (int ch = 0; ch < 8; ++ch) {
        std::uint8_t previous = artNetLastDmx_[ch];
        std::uint8_t current = ch < length ? data[ch] : 0;
        if (previous < kDmxTriggerThreshold && current >= kDmxTriggerThreshold) {
          enqueueRemoteCommand("ARTNETEVENT " + std::to_string(ch + 1) + " " + std::to_string(current));
        }
        artNetLastDmx_[ch] = current;
      }
      for (int ch = 8; ch < 10; ++ch) {
        std::uint8_t previous = artNetLastDmx_[ch];
        std::uint8_t current = ch < length ? data[ch] : 0;
        if (current > 0 && current != previous) {
          enqueueRemoteCommand("ARTNETEVENT " + std::to_string(ch + 1) + " " + std::to_string(current));
        }
        artNetLastDmx_[ch] = current;
      }
    }
  }

  void resetNmcSyncOutputState() {
    nmcSyncLastSentState_ = TransportState::Stopped;
    nmcSyncLastSentSeconds_ = -1.0;
    nmcSyncLastLocateSentMs_ = 0;
    nmcSyncOutputStateInitialized_ = false;
  }

  void nmcSyncLoop() {
    nmcSyncRunning_.store(true);
    nmcSyncLastError_.clear();
    std::string sourceFilter = resolvedNmcSyncSourceFilter();
    while (!nmcSyncStop_.load()) {
      fd_set readFds;
      FD_ZERO(&readFds);
      FD_SET(nmcSyncSocket_, &readFds);
      timeval timeout {};
      timeout.tv_sec = 0;
      timeout.tv_usec = 200000;
      int ready = select(selectNfds(nmcSyncSocket_), &readFds, nullptr, nullptr, &timeout);
      if (ready <= 0) {
        continue;
      }
      if (!FD_ISSET(nmcSyncSocket_, &readFds)) {
        continue;
      }
      std::array<char, 1024> buffer {};
      sockaddr_in sender {};
      socklen_t senderLen = sizeof(sender);
      int bytes = recvfrom(
        nmcSyncSocket_,
        buffer.data(),
        static_cast<int>(buffer.size() - 1),
        0,
        reinterpret_cast<sockaddr*>(&sender),
        &senderLen);
      if (bytes <= 0) {
        continue;
      }
      buffer[bytes] = '\0';
      std::string senderLabel = socketAddressToString(sender);
      if (!nmcSourceMatchesFilter(senderLabel, sourceFilter)) {
        continue;
      }
      std::string payload = trim(std::string(buffer.data(), static_cast<size_t>(bytes)));
      if (payload.empty()) {
        continue;
      }
      enqueueRemoteCommand("NMCEVENT " + payload);
    }
    nmcSyncRunning_.store(false);
  }

  bool startNmcSyncBridge() {
    stopNmcSyncBridge();

    std::string mode = resolvedNmcSyncMode();
    nmcSyncActiveMode_ = mode;
    nmcSyncActivePort_ = resolvedNmcSyncPort();
    nmcSyncActiveHost_ = resolvedNmcSyncTargetHost();
    nmcSyncActiveSourceFilter_ = resolvedNmcSyncSourceFilter();
    nmcSyncLastError_.clear();
    nmcSyncLastAnnouncedError_.clear();
    resetNmcSyncOutputState();

    if (mode == "output") {
      std::string error;
      if (!resolveNmcSyncTargetAddress(&nmcSyncTargetAddress_, &error)) {
        nmcSyncLastError_ = error.empty() ? "nmc target resolve failed" : error;
        return false;
      }
      nmcSyncSocket_ = createDatagramSocket(true);
      if (nmcSyncSocket_ == kInvalidSocket) {
        nmcSyncLastError_ = "nmc output socket unavailable";
        return false;
      }
      return true;
    }

    nmcSyncSocket_ = createBoundSocket(SOCK_DGRAM, nmcSyncActivePort_, false, !project_.allowRemoteNetwork);
    if (nmcSyncSocket_ == kInvalidSocket) {
      nmcSyncLastError_ = "nmc listen socket unavailable";
      return false;
    }
    nmcSyncStop_.store(false);
    nmcSyncRunning_.store(false);
    nmcSyncThread_ = std::thread([this]() { nmcSyncLoop(); });
    return true;
  }

  void stopNmcSyncBridge() {
    nmcSyncStop_.store(true);
    if (nmcSyncSocket_ != kInvalidSocket) {
      closeSocket(nmcSyncSocket_);
      nmcSyncSocket_ = kInvalidSocket;
    }
    if (nmcSyncThread_.joinable()) {
      nmcSyncThread_.join();
    }
    nmcSyncRunning_.store(false);
    nmcSyncActiveMode_.clear();
    nmcSyncActivePort_ = 0;
    nmcSyncActiveHost_.clear();
    nmcSyncActiveSourceFilter_.clear();
    resetNmcSyncOutputState();
  }

  void refreshNmcSyncState() {
    if (!project_.nmcSyncEnabled) {
      stopNmcSyncBridge();
      nmcSyncLastError_.clear();
      nmcSyncLastAnnouncedError_.clear();
      nmcSyncRestartBlockedUntilMs_ = 0;
      return;
    }

    std::string desiredMode = resolvedNmcSyncMode();
    int desiredPort = resolvedNmcSyncPort();
    std::string desiredHost = desiredMode == "output" ? resolvedNmcSyncTargetHost() : std::string();
    std::string desiredFilter = desiredMode == "input" ? resolvedNmcSyncSourceFilter() : std::string();
    bool configChanged = desiredMode != nmcSyncActiveMode_ ||
                         desiredPort != nmcSyncActivePort_ ||
                         desiredHost != nmcSyncActiveHost_ ||
                         desiredFilter != nmcSyncActiveSourceFilter_;
    if (configChanged) {
      stopNmcSyncBridge();
    }

    if (nmcSyncThread_.joinable()) {
      if (nmcSyncRunning_.load()) {
        return;
      }
      nmcSyncThread_.join();
    }

    if (desiredMode == "output" && nmcSyncSocket_ != kInvalidSocket) {
      return;
    }

    Uint64 now = SDL_GetTicks();
    if (nmcSyncRestartBlockedUntilMs_ > now) {
      if (!nmcSyncLastError_.empty() && nmcSyncLastError_ != nmcSyncLastAnnouncedError_) {
        triggerToast("nmc sync: " + nmcSyncLastError_);
        nmcSyncLastAnnouncedError_ = nmcSyncLastError_;
      }
      return;
    }

    if (!startNmcSyncBridge()) {
      nmcSyncRestartBlockedUntilMs_ = now + 3000;
      if (!nmcSyncLastError_.empty() && nmcSyncLastError_ != nmcSyncLastAnnouncedError_) {
        triggerToast("nmc sync: " + nmcSyncLastError_);
        nmcSyncLastAnnouncedError_ = nmcSyncLastError_;
      }
      return;
    }
    nmcSyncRestartBlockedUntilMs_ = 0;
  }

  bool sendNmcSyncPacket(const std::string& command, std::optional<double> seconds = std::nullopt) {
    if (nmcSyncSocket_ == kInvalidSocket || normalizeNmcSyncModeToken(nmcSyncActiveMode_) != "output") {
      return false;
    }
    std::string payload = formatNmcSyncPacket(command, seconds);
    int sent = sendto(
      nmcSyncSocket_,
      payload.c_str(),
      static_cast<int>(payload.size()),
      kSocketSendFlags,
      reinterpret_cast<const sockaddr*>(&nmcSyncTargetAddress_),
      static_cast<socklen_t>(sizeof(nmcSyncTargetAddress_)));
    if (sent < 0) {
      nmcSyncLastError_ = "nmc send failed";
      nmcSyncRestartBlockedUntilMs_ = SDL_GetTicks() + 3000;
      closeSocket(nmcSyncSocket_);
      nmcSyncSocket_ = kInvalidSocket;
      return false;
    }
    nmcSyncLastError_.clear();
    return true;
  }

  void tickNmcSyncOutput() {
    if (!project_.nmcSyncEnabled || normalizeNmcSyncModeToken(nmcSyncActiveMode_) != "output") {
      return;
    }
    if (nmcSyncSocket_ == kInvalidSocket) {
      return;
    }

    MediaEngine* engine = focusedMediaEngine();
    const Cue* activeCue = activeCuePtr();
    TransportState state = engine ? engine->state() : TransportState::Stopped;
    double seconds = (engine && activeCue) ? std::max(0.0, engine->position()) : 0.0;
    Uint64 now = SDL_GetTicks();

    bool stateChanged = !nmcSyncOutputStateInitialized_ || state != nmcSyncLastSentState_;
    bool positionJumped = nmcSyncOutputStateInitialized_ && std::fabs(seconds - nmcSyncLastSentSeconds_) >= 0.75;
    bool shouldSendLocate = activeCue &&
      (state == TransportState::Playing
       ? (!nmcSyncOutputStateInitialized_ ||
          now >= nmcSyncLastLocateSentMs_ + static_cast<Uint64>(resolvedNmcSyncLocateIntervalMs()))
       : positionJumped);

    if (stateChanged) {
      switch (state) {
        case TransportState::Playing:
          sendNmcSyncPacket("PLAY", seconds);
          break;
        case TransportState::Paused:
          sendNmcSyncPacket("PAUSE", seconds);
          break;
        case TransportState::Stopped:
          sendNmcSyncPacket("STOP", seconds);
          break;
      }
      nmcSyncLastLocateSentMs_ = now;
    } else if (shouldSendLocate) {
      sendNmcSyncPacket("LOCATE", seconds);
      nmcSyncLastLocateSentMs_ = now;
    }

    nmcSyncLastSentState_ = state;
    nmcSyncLastSentSeconds_ = seconds;
    nmcSyncOutputStateInitialized_ = true;
  }

  std::string configuredNdiTriggerSourceFilter() const {
    const char* env = std::getenv("DECKBOY_NDI_TRIGGER_SOURCE");
    return env ? trim(env) : std::string();
  }

  static std::string normalizedNdiMetadataPayload(std::string payload) {
    auto replaceAll = [&](const std::string& from, const std::string& to) {
      if (from.empty()) {
        return;
      }
      size_t pos = 0;
      while ((pos = payload.find(from, pos)) != std::string::npos) {
        payload.replace(pos, from.size(), to);
        pos += to.size();
      }
    };
    replaceAll("&quot;", "\"");
    replaceAll("&apos;", "'");
    replaceAll("&lt;", "<");
    replaceAll("&gt;", ">");
    replaceAll("&amp;", "&");
    return trim(payload);
  }

  static std::optional<std::string> xmlAttributeValueCaseInsensitive(
      const std::string& payload,
      const std::string& attributeName) {
    if (payload.empty() || attributeName.empty()) {
      return std::nullopt;
    }
    std::string haystack = toUpper(payload);
    std::string needle = toUpper(attributeName);
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
      size_t cursor = pos + needle.size();
      while (cursor < haystack.size() && std::isspace(static_cast<unsigned char>(haystack[cursor]))) {
        ++cursor;
      }
      if (cursor >= haystack.size() || haystack[cursor] != '=') {
        pos += needle.size();
        continue;
      }
      ++cursor;
      while (cursor < haystack.size() && std::isspace(static_cast<unsigned char>(haystack[cursor]))) {
        ++cursor;
      }
      if (cursor >= payload.size()) {
        break;
      }
      char quote = payload[cursor];
      if (quote == '"' || quote == '\'') {
        ++cursor;
        size_t end = payload.find(quote, cursor);
        if (end == std::string::npos) {
          break;
        }
        return normalizedNdiMetadataPayload(payload.substr(cursor, end - cursor));
      }
      size_t end = cursor;
      while (end < payload.size() && !std::isspace(static_cast<unsigned char>(payload[end])) &&
             payload[end] != '>' && payload[end] != '/') {
        ++end;
      }
      return normalizedNdiMetadataPayload(payload.substr(cursor, end - cursor));
    }
    return std::nullopt;
  }

  static std::optional<std::string> xmlElementTextCaseInsensitive(
      const std::string& payload,
      const std::string& elementName) {
    if (payload.empty() || elementName.empty()) {
      return std::nullopt;
    }
    std::string upperPayload = toUpper(payload);
    std::string openNeedle = "<" + toUpper(elementName);
    size_t open = upperPayload.find(openNeedle);
    if (open == std::string::npos) {
      return std::nullopt;
    }
    size_t openEnd = payload.find('>', open);
    if (openEnd == std::string::npos) {
      return std::nullopt;
    }
    std::string closeNeedle = "</" + toUpper(elementName) + ">";
    size_t close = upperPayload.find(closeNeedle, openEnd + 1);
    if (close == std::string::npos || close <= openEnd + 1) {
      return std::nullopt;
    }
    return normalizedNdiMetadataPayload(payload.substr(openEnd + 1, close - openEnd - 1));
  }

  static bool ndiSourceNameMatchesFilter(const std::string& sourceName, const std::string& filter) {
    if (filter.empty()) {
      return true;
    }
    std::string sourceUpper = toUpper(sourceName);
    std::string filterUpper = toUpper(filter);
    return sourceUpper == filterUpper || sourceUpper.find(filterUpper) != std::string::npos;
  }

  const NdiTriggerRuntimeSource* chooseNdiTriggerSource(
      const NdiTriggerRuntimeSource* sources,
      std::uint32_t count,
      const std::string& filter,
      const std::string& preferredName) const {
    if (!sources || count == 0) {
      return nullptr;
    }
    if (!preferredName.empty()) {
      for (std::uint32_t i = 0; i < count; ++i) {
        std::string sourceName = sources[i].p_ndi_name ? trim(sources[i].p_ndi_name) : std::string();
        if (!sourceName.empty() && toUpper(sourceName) == toUpper(preferredName)) {
          return &sources[i];
        }
      }
    }
    if (!filter.empty()) {
      for (std::uint32_t i = 0; i < count; ++i) {
        std::string sourceName = sources[i].p_ndi_name ? trim(sources[i].p_ndi_name) : std::string();
        if (!sourceName.empty() && ndiSourceNameMatchesFilter(sourceName, filter)) {
          return &sources[i];
        }
      }
      return nullptr;
    }
    return &sources[0];
  }

  void ndiTriggerLoop() {
    ndiTriggerRunning_.store(true);
    void* finder = nullptr;
    void* receiver = nullptr;
    auto cleanup = [&]() {
      if (receiver && ndiTriggerApi_.recvDestroyFn) {
        ndiTriggerApi_.recvDestroyFn(receiver);
        receiver = nullptr;
      }
      if (finder && ndiTriggerApi_.findDestroyFn) {
        ndiTriggerApi_.findDestroyFn(finder);
        finder = nullptr;
      }
      ndiTriggerRunning_.store(false);
    };

    if (!ndiTriggerApi_.ensureLoaded()) {
      ndiTriggerLastError_ = ndiTriggerApi_.loadError.empty() ? "ndi runtime missing" : ndiTriggerApi_.loadError;
      ndiTriggerRunning_.store(false);
      return;
    }

    finder = ndiTriggerApi_.findCreateFn ? ndiTriggerApi_.findCreateFn(nullptr) : nullptr;
    if (!finder) {
      ndiTriggerLastError_ = "ndi source discovery unavailable";
      ndiTriggerRunning_.store(false);
      return;
    }
    receiver = ndiTriggerApi_.recvCreateFn ? ndiTriggerApi_.recvCreateFn(nullptr) : nullptr;
    if (!receiver) {
      ndiTriggerLastError_ = "ndi trigger receiver unavailable";
      cleanup();
      return;
    }

    ndiTriggerLastError_.clear();
    ndiTriggerLastAnnouncedError_.clear();
    ndiTriggerConnectedSource_.clear();
    std::string requestedFilter = configuredNdiTriggerSourceFilter();
    std::string lastPayload;
    Uint64 lastPayloadAtMs = 0;

    while (!ndiTriggerStop_.load()) {
      std::uint32_t sourceCount = 0;
      const NdiTriggerRuntimeSource* sources =
        ndiTriggerApi_.findGetCurrentSourcesFn ? ndiTriggerApi_.findGetCurrentSourcesFn(finder, &sourceCount) : nullptr;
      const NdiTriggerRuntimeSource* chosen =
        chooseNdiTriggerSource(sources, sourceCount, requestedFilter, ndiTriggerConnectedSource_);
      std::string chosenName = (chosen && chosen->p_ndi_name) ? trim(chosen->p_ndi_name) : std::string();
      if (chosen && !chosenName.empty() && chosenName != ndiTriggerConnectedSource_) {
        ndiTriggerApi_.recvConnectFn(receiver, chosen);
        ndiTriggerConnectedSource_ = chosenName;
        ndiTriggerLastError_.clear();
      } else if (!chosen && !requestedFilter.empty() && ndiTriggerConnectedSource_.empty()) {
        ndiTriggerLastError_ = "ndi trigger source not found";
      }

      NdiTriggerRuntimeMetadataFrame metadata {};
      int frameType = ndiTriggerApi_.recvCaptureFn
        ? ndiTriggerApi_.recvCaptureFn(receiver, nullptr, nullptr, &metadata, 250)
        : 0;
      if (frameType == 3) {
        std::string payload;
        if (metadata.p_data) {
          if (metadata.length > 0) {
            payload.assign(metadata.p_data, metadata.p_data + metadata.length);
          } else {
            payload = metadata.p_data;
          }
        }
        if (ndiTriggerApi_.recvFreeMetadataFn) {
          ndiTriggerApi_.recvFreeMetadataFn(receiver, &metadata);
        }
        payload = trim(payload);
        if (!payload.empty()) {
          Uint64 now = SDL_GetTicks();
          if (!(payload == lastPayload && now - lastPayloadAtMs < 250)) {
            enqueueRemoteCommand("NDIEVENT " + payload);
            lastPayload = payload;
            lastPayloadAtMs = now;
          }
        }
        continue;
      }
      if (frameType == 4) {
        ndiTriggerLastError_ = "ndi trigger capture error";
      } else if (frameType == 100) {
        ndiTriggerConnectedSource_.clear();
      }
      if (ndiTriggerApi_.findWaitForSourcesFn) {
        ndiTriggerApi_.findWaitForSourcesFn(finder, 150);
      } else {
        SDL_Delay(120);
      }
    }

    cleanup();
  }

  bool startNdiTriggerBridge() {
    stopNdiTriggerBridge();
    if (!ndiTriggerApi_.ensureLoaded()) {
      ndiTriggerLastError_ = ndiTriggerApi_.loadError.empty() ? "ndi runtime missing" : ndiTriggerApi_.loadError;
      return false;
    }
    ndiTriggerStop_.store(false);
    ndiTriggerRunning_.store(false);
    ndiTriggerThread_ = std::thread([this]() { ndiTriggerLoop(); });
    return true;
  }

  void stopNdiTriggerBridge() {
    ndiTriggerStop_.store(true);
    if (ndiTriggerThread_.joinable()) {
      ndiTriggerThread_.join();
    }
    ndiTriggerApi_.shutdown();
    ndiTriggerConnectedSource_.clear();
  }

  void refreshNdiTriggerBridgeState() {
    if (!project_.ndiTriggerEnabled) {
      stopNdiTriggerBridge();
      ndiTriggerLastError_.clear();
      ndiTriggerLastAnnouncedError_.clear();
      ndiTriggerRestartBlockedUntilMs_ = 0;
      return;
    }
    if (ndiTriggerThread_.joinable()) {
      if (ndiTriggerRunning_.load()) {
        return;
      }
      ndiTriggerThread_.join();
    }
    Uint64 now = SDL_GetTicks();
    if (ndiTriggerRestartBlockedUntilMs_ > now) {
      if (!ndiTriggerLastError_.empty() && ndiTriggerLastError_ != ndiTriggerLastAnnouncedError_) {
        triggerToast("ndi trigger: " + ndiTriggerLastError_);
        ndiTriggerLastAnnouncedError_ = ndiTriggerLastError_;
      }
      return;
    }
    if (!startNdiTriggerBridge()) {
      ndiTriggerRestartBlockedUntilMs_ = now + 3000;
      if (!ndiTriggerLastError_.empty() && ndiTriggerLastError_ != ndiTriggerLastAnnouncedError_) {
        triggerToast("ndi trigger: " + ndiTriggerLastError_);
        ndiTriggerLastAnnouncedError_ = ndiTriggerLastError_;
      }
      return;
    }
    ndiTriggerRestartBlockedUntilMs_ = 0;
  }

  void resetMidiMtcDecoder() {
#if defined(DECKBOY_HAS_ALSA)
    midiMtcQuarterFrameNibbles_.fill(-1);
    midiMtcLastSentSeconds_ = -1.0;
    midiMtcLastSentFps_ = 0.0;
#endif
  }

#if defined(DECKBOY_HAS_ALSA)
  std::optional<std::pair<double, double>> decodeMidiMtcQuarterFrame(int qfByte) {
    int messageType = (qfByte >> 4) & 0x07;
    int nibbleValue = qfByte & 0x0F;
    midiMtcQuarterFrameNibbles_[messageType] = nibbleValue;
    for (int nibble : midiMtcQuarterFrameNibbles_) {
      if (nibble < 0) {
        return std::nullopt;
      }
    }

    int frames = (midiMtcQuarterFrameNibbles_[1] << 4) | midiMtcQuarterFrameNibbles_[0];
    int seconds = (midiMtcQuarterFrameNibbles_[3] << 4) | midiMtcQuarterFrameNibbles_[2];
    int minutes = (midiMtcQuarterFrameNibbles_[5] << 4) | midiMtcQuarterFrameNibbles_[4];
    int hourLow = midiMtcQuarterFrameNibbles_[6];
    int hourHighAndRate = midiMtcQuarterFrameNibbles_[7];
    int hours = ((hourHighAndRate & 0x01) << 4) | hourLow;
    int rateCode = (hourHighAndRate >> 1) & 0x03;

    double fps = 30.0;
    switch (rateCode) {
      case 0: fps = 24.0; break;
      case 1: fps = 25.0; break;
      case 2: fps = 29.97; break;
      case 3: fps = 30.0; break;
      default: break;
    }
    if (fps < 1.0) {
      return std::nullopt;
    }
    frames = std::clamp(frames, 0, static_cast<int>(std::ceil(fps)));
    seconds = std::clamp(seconds, 0, 59);
    minutes = std::clamp(minutes, 0, 59);
    hours = std::clamp(hours, 0, 23);
    double tcSeconds = hours * 3600.0 + minutes * 60.0 + seconds + (frames / fps);
    return std::make_pair(tcSeconds, fps);
  }
#endif

  void startHyperDeckServer() {
    const char* portEnv = std::getenv("DECKBOY_HYPERDECK_PORT");
    if (portEnv && *portEnv) {
      try { hyperDeckPort_ = std::clamp(std::stoi(portEnv), 1, 65535); } catch (...) {}
    }
    hyperDeckListenFd_ = createBoundSocket(SOCK_STREAM, hyperDeckPort_, true, !project_.allowRemoteNetwork);
    if (hyperDeckListenFd_ == kInvalidSocket) return;
    hyperDeckRunning_.store(true);
    hyperDeckThread_ = std::thread([this]() { hyperDeckLoop(); });
  }

  void stopHyperDeckServer() {
    hyperDeckRunning_.store(false);
    if (hyperDeckListenFd_ != kInvalidSocket) {
      closeSocket(hyperDeckListenFd_);
      hyperDeckListenFd_ = kInvalidSocket;
    }
    if (hyperDeckThread_.joinable()) hyperDeckThread_.join();
  }

  // Translate a parsed MIDI event into the same remote command the ALSA path
  // produces, and queue it the same way. Shared so the two transports can't
  // drift apart in what a given controller message does.
  void queueMidiCommand(std::string cmd) {
    if (cmd.empty()) {
      return;
    }
    std::lock_guard<std::mutex> lk(remoteCommandMutex_);
    remoteCommands_.push_back(PendingRemoteCommand {std::move(cmd), kInvalidSocket});
  }

  void onMidiNoteOn(int note, int velocity) {
    // Routed to a live synth when the operator has asked for that, otherwise
    // the historical behaviour: notes fire cues. Both are legitimate uses of a
    // MIDI keyboard and only the operator knows which they want, so this is a
    // switch rather than a guess.
    if (project_.midiToSynth) {
      // Velocity 0 is note-off by MIDI convention -- the wrapper surfaces only
      // note-on, so this is where the distinction is made.
      queueMidiCommand(velocity > 0
        ? ("SYNTHNOTEON " + std::to_string(note) + " " + std::to_string(velocity))
        : ("SYNTHNOTEOFF " + std::to_string(note)));
      return;
    }
    if (velocity > 0) {
      queueMidiCommand("GOTO " + std::to_string(note + 1));
    }
  }

  void onMidiControlChange(int controller, int value) {
    if (controller == 7) {
      queueMidiCommand("MASTERVOL " + std::to_string(static_cast<int>(value * 200.0 / 127.0)));
    } else if (controller == 20) {
      std::ostringstream ss;
      ss << std::fixed << std::setprecision(2) << (0.5 + value * 1.5 / 127.0);
      queueMidiCommand("SPEED " + ss.str());
    }
  }

  // Put MIDI back on the port its operator asked for, when it comes back.
  //
  // The audio path grew this first and the reasoning is identical: a control
  // surface knocked out of a USB hub, or a rack powered on after the PC, left
  // the deck with MIDI armed and nothing connected until somebody restarted the
  // application. The difference is that MIDI now REFUSES an absent port rather
  // than binding to a stranger, so the recovery is simply to try again once the
  // port list changes -- there is no wrong device to move off first.
  //
  // Runs on a slow poll rather than an event: neither RtMidi nor the ALSA
  // sequencer gives us a device-change callback here, and the cost is one
  // enumeration every few seconds while MIDI is armed and not connected.
  void reconcileMidiInput() {
    if (!midiEnabled_ || midiDeviceName_().empty()) {
      // Nothing armed, or no particular port asked for. A refused arm is left
      // refused on purpose: the operator asked once and was told why, and a
      // deck that silently connects itself minutes later is worse than one that
      // waits to be asked again.
      return;
    }
    if (midiDeviceInUse_ == midiDeviceName_()) {
      // Where it should be -- unless the surface has since been unplugged, and
      // nothing else would ever notice. RtMidi holds a port that has gone away
      // without erroring; the callbacks simply stop, which from the operator's
      // side is indistinguishable from a quiet desk.
#if defined(DECKBOY_HAS_MIDI)
      bool stillThere = false;
      for (const auto& device : deckboy::platform::midi::MidiInput::listDevices()) {
        if (device.name == midiDeviceName_()) {
          stillThere = true;
          break;
        }
      }
      if (!stillThere) {
        triggerToast("midi: " + midiDeviceName_() + " disconnected");
        stopMidiInput();
        midiDeviceInUse_.clear();   // the poll below reclaims it when it returns
      }
#endif
      return;
    }
    // QUIETLY. The retry runs every few seconds forever while the surface is
    // away, and startMidiInput says "not found" on each failure -- which as a
    // one-off is exactly right and as a repeating toast is unusable.
    if (startMidiInput(/*quiet=*/true)) {
      triggerToast("midi: back on " + midiDeviceName_());
    }
  }

  // ── Update check ──────────────────────────────────────────────────────────
  //
  // THE RULES THIS OBEYS, because it is a live-show tool:
  //
  //   It never installs anything by itself. It asks, it tells you, and the
  //   installing is a button you press.
  //   It never runs while anything is live. Not the check, not the download,
  //   and certainly not the installer.
  //   It is off until switched on. This is the only connection Deckboy opens
  //   outward of its own accord, and a machine on a venue's network should do
  //   nothing nobody asked for.
  //
  // The fetching is curl, which ships with Windows 10 and later, with macOS,
  // and with essentially every Linux. That keeps TLS out of this codebase
  // entirely -- the alternative was linking a TLS stack to read one number.

  struct UpdateInfo {
    std::string version;        // "0.90.0", empty when there is nothing newer
    std::string assetName;
    std::string assetUrl;
    std::uintmax_t assetSize = 0;
  };

  // Declared here rather than with the rest of the app's state, because
  // UpdateInfo is declared here: this file is included into the class body, so
  // a member of that type has to come after it.
  //
  // Written by a worker (the curl call blocks for as long as the network takes)
  // and read by the render loop, so everything but the running flag is under
  // the mutex.
  std::atomic<bool> updateCheckRunning_ {false};
  std::mutex updateMutex_;
  UpdateInfo updateFound_;              // version empty = nothing newer
  std::string updateStatus_;            // one line, shown in settings
  std::string updateReadyInstaller_;    // a verified download waiting to be run
  std::string updateRunningInstaller_;  // spared from the startup sweep
  bool updateAnnouncePending_ = false;  // the render loop owes a toast

  // "v1.2.3" / "1.2.3" -> {1,2,3}. Anything unparseable sorts as {0,0,0}, so a
  // tag we do not understand can never look newer than what is running.
  static std::array<int, 3> parseVersionTriple(const std::string& text) {
    std::array<int, 3> out {0, 0, 0};
    std::size_t at = (!text.empty() && (text[0] == 'v' || text[0] == 'V')) ? 1 : 0;
    for (int part = 0; part < 3 && at < text.size(); ++part) {
      int value = 0;
      bool any = false;
      while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        if (value < 100000) value = value * 10 + (text[at] - '0');
        any = true;
        ++at;
      }
      if (!any) break;
      out[static_cast<std::size_t>(part)] = value;
      if (at < text.size() && text[at] == '.') ++at;
    }
    return out;
  }

  // Pull one string field out of the releases JSON.
  //
  // A hand parser rather than a JSON library, because this reads exactly three
  // fields from one endpoint whose shape is fixed, and adding a dependency to
  // do that would be the larger change. It is deliberately literal: find the
  // key, expect a quoted value, stop at the closing quote.
  static std::string jsonStringField(const std::string& json, const std::string& key,
                                     std::size_t from = 0) {
    const std::string needle = "\"" + key + "\"";
    std::size_t at = json.find(needle, from);
    if (at == std::string::npos) return "";
    at = json.find(':', at + needle.size());
    if (at == std::string::npos) return "";
    ++at;
    while (at < json.size() && (json[at] == ' ' || json[at] == '\t')) ++at;
    if (at >= json.size() || json[at] != '"') return "";
    ++at;
    std::string out;
    while (at < json.size() && json[at] != '"') {
      if (json[at] == '\\' && at + 1 < json.size()) ++at;   // keep it simple: unescape one level
      out += json[at++];
    }
    return out;
  }

  // Which artefact this build should be offered.
  static std::string updateAssetPatternForPlatform() {
#if defined(_WIN32)
    return "-windows-x64-setup.exe";
#elif defined(__APPLE__)
    return "-macos-arm64.dmg";
#else
    return "-x86_64.AppImage";
#endif
  }

  bool anythingIsLive() const {
    for (const auto& runtime : deckRuntimes_) {
      if (runtime.mediaEngine &&
          runtime.mediaEngine->state() != TransportState::Stopped) {
        return true;
      }
    }
    for (const auto& output : project_.outputs) {
      if (output.enabled || output.streamEnabled) {
        return true;
      }
    }
    return false;
  }

  // Ask GitHub what the newest release is. Blocking, so it is called from a
  // worker; the caller owns the thread.
  UpdateInfo fetchLatestRelease(std::string& error) const {
    UpdateInfo info;
    const std::vector<std::string> args {
      "curl", "-sS", "-L", "--max-time", "20", "--fail",
      "-H", "Accept: application/vnd.github+json",
      "-A", std::string("Deckboy/") + deckboy::core::version::kVersion,
      "https://api.github.com/repos/Utopian-Academy/Deckboy/releases/latest"
    };
    auto body = readAllText(args);
    if (!body || body->empty()) {
      error = "could not reach github";
      return info;
    }
    const std::string tag = jsonStringField(*body, "tag_name");
    if (tag.empty()) {
      error = "no release found";
      return info;
    }
    const auto latest = parseVersionTriple(tag);
    const auto running = parseVersionTriple(deckboy::core::version::kVersion);
    if (!(latest > running)) {
      return info;                       // up to date; version stays empty
    }
    // Find the artefact for this platform, and take the URL and size that sit
    // beside its name in the same asset object.
    const std::string want = updateAssetPatternForPlatform();
    std::size_t at = 0;
    while ((at = body->find("\"name\"", at)) != std::string::npos) {
      const std::string name = jsonStringField(*body, "name", at);
      if (name.size() > want.size() &&
          name.compare(name.size() - want.size(), want.size(), want) == 0) {
        info.assetName = name;
        info.assetUrl = jsonStringField(*body, "browser_download_url", at);
        const std::size_t sizeAt = body->find("\"size\"", at);
        if (sizeAt != std::string::npos) {
          info.assetSize = static_cast<std::uintmax_t>(
            std::strtoull(body->c_str() + body->find(':', sizeAt) + 1, nullptr, 10));
        }
        break;
      }
      at += 6;
    }
    if (info.assetUrl.empty()) {
      error = "release has no build for this platform";
      return info;
    }
    info.version = tag;
    return info;
  }

  // Kick the check off on a worker. Never blocks the render loop, and never
  // runs twice at once.
  void checkForUpdateAsync(bool quiet) {
    if (updateCheckRunning_.exchange(true)) {
      return;
    }
    if (!quiet) {
      std::lock_guard<std::mutex> lock(updateMutex_);
      updateStatus_ = "checking...";
    }
    std::thread([this, quiet]() {
      std::string error;
      UpdateInfo info = fetchLatestRelease(error);
      {
        std::lock_guard<std::mutex> lock(updateMutex_);
        updateFound_ = info;
        if (!error.empty()) {
          updateStatus_ = error;
        } else if (info.version.empty()) {
          updateStatus_ = std::string("up to date (") + deckboy::core::version::kVersionTag + ")";
        } else {
          updateStatus_ = info.version + " is available";
        }
        updateAnnouncePending_ = !quiet || !info.version.empty();
      }
      updateCheckRunning_ = false;
    }).detach();
  }

  // Where a downloaded installer lives: under the state dir, never beside the
  // application, which may sit in a read-only or signed location.
  fs::path updateDownloadDir() const {
    return deckboy::core::Paths::stateDir() / "updates";
  }

  // TIDY UP AFTER OURSELVES.
  //
  // Run at startup: anything left in the update folder has already served its
  // purpose -- either it was installed, in which case this build IS the
  // installer's work, or it was abandoned. Either way a 90MB file should not
  // sit there forever, which is the part of this James asked for by name.
  void removeStaleUpdateDownloads() {
    std::error_code ec;
    const fs::path dir = updateDownloadDir();
    if (!fs::exists(dir, ec)) {
      return;
    }
    int removed = 0;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      if (ec) break;
      if (!entry.is_regular_file(ec)) continue;
      // Never delete the one we are about to run.
      if (entry.path().filename().string() == updateRunningInstaller_) continue;
      std::error_code rmEc;
      if (fs::remove(entry.path(), rmEc)) ++removed;
    }
    if (removed > 0) {
      showLog("UPDATE-CLEAN", std::to_string(removed) + " old download(s) removed");
    }
  }

  // Download the offered build and hand it to the system installer.
  //
  // Refuses while anything is live. An update is a restart, and a restart in
  // the middle of a show is the worst thing this program could do to somebody.
  void downloadAndInstallUpdate() {
    UpdateInfo info;
    {
      std::lock_guard<std::mutex> lock(updateMutex_);
      info = updateFound_;
    }
    if (info.version.empty() || info.assetUrl.empty()) {
      triggerToast("update: nothing to install");
      return;
    }
    if (anythingIsLive()) {
      triggerToast("update: stop playback and disarm outputs first");
      return;
    }
    if (updateCheckRunning_.exchange(true)) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(updateMutex_);
      updateStatus_ = "downloading " + info.assetName + "...";
    }
    std::thread([this, info]() {
      std::error_code ec;
      const fs::path dir = updateDownloadDir();
      fs::create_directories(dir, ec);
      const fs::path target = dir / info.assetName;
      std::error_code rmEc;
      fs::remove(target, rmEc);
      const std::vector<std::string> args {
        "curl", "-sS", "-L", "--fail", "--max-time", "1800",
        "-A", std::string("Deckboy/") + deckboy::core::version::kVersion,
        "-o", target.string(), info.assetUrl
      };
      const bool fetched = readAllText(args).has_value();
      std::uintmax_t got = 0;
      if (fetched) {
        got = fs::file_size(target, ec);
        if (ec) got = 0;
      }
      // SIZE IS THE CHECK. The release carries no digest to compare against, so
      // this proves only that the whole file arrived -- which is the failure
      // that actually happens on a venue connection. A partial installer that
      // ran anyway would be far worse than one that refuses.
      const bool complete = fetched && info.assetSize > 0 && got == info.assetSize;
      std::lock_guard<std::mutex> lock(updateMutex_);
      if (!complete) {
        std::error_code cleanEc;
        fs::remove(target, cleanEc);
        updateStatus_ = fetched ? "download was incomplete" : "download failed";
        updateReadyInstaller_.clear();
      } else {
        updateReadyInstaller_ = target.string();
        updateStatus_ = "ready to install " + info.version;
      }
      updateAnnouncePending_ = true;
      updateCheckRunning_ = false;
    }).detach();
  }

  // Launch the downloaded installer and step out of its way.
  void runDownloadedUpdate() {
    std::string installer;
    {
      std::lock_guard<std::mutex> lock(updateMutex_);
      installer = updateReadyInstaller_;
    }
    if (installer.empty()) {
      triggerToast("update: nothing downloaded");
      return;
    }
    if (anythingIsLive()) {
      triggerToast("update: stop playback and disarm outputs first");
      return;
    }
    updateRunningInstaller_ = fs::path(installer).filename().string();
    showLog("UPDATE-RUN", updateRunningInstaller_);
    // EACH BRANCH IS SELF-CONTAINED, deliberately. The first version declared
    // `args` in the Windows and macOS arms and shared the spawn below the
    // #endif -- which the Linux arm, having returned early, never declares, so
    // the shared code did not compile there at all. Windows never sees that
    // branch, which is why it took CI to find. Build the other configurations.
#if defined(_WIN32) || defined(__APPLE__)
#if defined(_WIN32)
    const std::vector<std::string> args {installer};
#else
    const std::vector<std::string> args {"open", installer};
#endif
    ChildProcess child;
    if (!spawnProcess(child, args, SpawnOptions::detachedSilent())) {
      triggerToast("update: could not start the installer");
      return;
    }
    gShouldQuit.store(true);   // the installer replaces this build; step aside
#else
    // A Linux build is an AppImage or a tarball: there is nothing to run, so
    // show the operator where it landed and let them put it where they keep
    // things.
    deckboy::platform::revealFileInFileManager(installer);
    triggerToast("update: downloaded to " + installer);
#endif
  }

  bool startMidiInput(bool quiet = false) {
#if defined(DECKBOY_HAS_ALSA)
    stopMidiInput();
    midiStop_ = false;
    resetMidiMtcDecoder();
    int err = snd_seq_open(&midiSeq_, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK);
    if (err < 0) { midiSeq_ = nullptr; return false; }
    snd_seq_set_client_name(midiSeq_, "Deckboy");
    midiSeqPort_ = snd_seq_create_simple_port(midiSeq_, "input",
      SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
      SND_SEQ_PORT_TYPE_APPLICATION);
    if (midiSeqPort_ < 0) { snd_seq_close(midiSeq_); midiSeq_ = nullptr; return false; }

    // A CONFIGURED PORT THAT WILL NOT CONNECT IS A FAILURE, NOT A DETAIL.
    //
    // This used to parse the address, subscribe, and ignore the result of both
    // -- then return true regardless. So a port that had been renamed, or was
    // not plugged in, left the deck with an ALSA port created, nothing
    // subscribed to it, and the operator told MIDI was on. Nothing would ever
    // fire a cue and there was no way to tell that from a quiet desk. It is the
    // same fault as the RtMidi path opening whatever port came first, wearing
    // different clothes: one binds to a stranger, this one binds to nobody.
    //
    // With NO port configured the bare port is correct and stays: it advertises
    // SUBS_WRITE, so an operator can `aconnect` anything to it by hand, which is
    // how this is normally done on Linux.
    if (!midiDeviceName_().empty()) {
      snd_seq_addr_t sender;
      if (snd_seq_parse_address(midiSeq_, &sender, midiDeviceName_().c_str()) != 0) {
        if (!quiet) {
          triggerToast("midi: " + midiDeviceName_() + " not found");
        }
        stopMidiInput();
        return false;
      }
      snd_seq_port_subscribe_t* sub;
      snd_seq_port_subscribe_alloca(&sub);
      snd_seq_port_subscribe_set_sender(sub, &sender);
      snd_seq_addr_t dest {static_cast<unsigned char>(snd_seq_client_id(midiSeq_)), static_cast<unsigned char>(midiSeqPort_)};
      snd_seq_port_subscribe_set_dest(sub, &dest);
      if (snd_seq_subscribe_port(midiSeq_, sub) < 0) {
        if (!quiet) {
          triggerToast("midi: " + midiDeviceName_() + " refused the connection");
        }
        stopMidiInput();
        return false;
      }
    }

    midiDeviceInUse_ = midiDeviceName_();
    midiThread_ = std::thread([this]() { midiLoop(); });
    return true;
#elif defined(DECKBOY_HAS_MIDI)
    // Non-ALSA platforms (Windows, macOS) go through the cross-platform RtMidi
    // wrapper. This path did not exist before: startMidiInput() simply returned
    // false off Linux, so the MIDI toggle, the port picker and the documented
    // note/CC mappings did nothing at all on the primary platform even though
    // RtMidi was compiled in and enumerating ports.
    //
    // MTC quarter-frame and MMC/MSC sysex stay ALSA-only for now: the wrapper
    // surfaces channel-voice messages only, which is why the integration
    // catalog still reports mtc[stub] off Linux.
    stopMidiInput();
    auto devices = deckboy::platform::midi::MidiInput::listDevices();
    if (devices.empty()) {
      return false;
    }
    // A CONFIGURED PORT THAT IS ABSENT IS AN ERROR, NOT A REASON TO GUESS.
    //
    // This used to start from devices.front() and only overwrite it on a name
    // match, so a controller that was unplugged, renamed by its driver, or
    // simply not switched on handed control of the deck to whatever enumerated
    // first. That is rarely a control surface: a machine with a DAW installed
    // answers this question with its plugin bridge ports, and a stray note from
    // one of those fires cues. Refuse, say which port was wanted, and let the
    // operator pick again -- `deckboy --devices` lists the names.
    int deviceId = -1;
    if (!midiDeviceName_().empty()) {
      for (const auto& device : devices) {
        if (device.name == midiDeviceName_()) {
          deviceId = device.id;
          break;
        }
      }
      if (deviceId < 0) {
        if (!quiet) {
          triggerToast("midi: " + midiDeviceName_() + " not found");
        }
        return false;
      }
    } else {
      // Nothing chosen: the first port is a reasonable guess when there is only
      // one, and worth naming out loud when there is not.
      deviceId = devices.front().id;
      if (devices.size() > 1 && !quiet) {
        triggerToast("midi: using " + devices.front().name +
                     " (" + std::to_string(devices.size()) + " ports — pick one in settings)");
      }
    }
    midiRt_.onNoteOn([this](int note, int velocity) { onMidiNoteOn(note, velocity); });
    midiRt_.onControlChange([this](int controller, int value) { onMidiControlChange(controller, value); });
    midiRt_.onSysEx([this](const std::vector<std::uint8_t>& data) {
      onMidiSysEx(data);
    });
    if (!midiRt_.open(deviceId)) {
      return false;
    }
    midiDeviceInUse_ = midiDeviceName_().empty() ? devices.front().name : midiDeviceName_();
    return true;
#else
    return false;
#endif
  }

  // ── MIDI Show Control ──────────────────────────────────────────────────────
  //
  // A desk sends GO with a cue number and everything in the rig answering to
  // that number goes at once. The number is TEXT -- "12.4.1" and "A" are both
  // legitimate cue numbers -- so it is matched the way an operator reads it,
  // through the same lookup TAKE <token> uses.
  //
  // Everything understood is written to the show log whether or not it changed
  // anything. When a cue fails to fire the first question is always whether
  // the message reached the machine, and silence is not an answer to that.
  void onMidiSysEx(const std::vector<std::uint8_t>& data) {
    const auto message =
      deckboy::showcontrol::parse(data, project_.showControlDeviceId);
    if (!message.ok()) {
      return;
    }
    using deckboy::showcontrol::Action;
    showLog(std::string("MSC ") + deckboy::showcontrol::actionName(message.action),
            message.cue.empty() ? std::string() : "cue " + message.cue);

    auto selectByNumber = [this](const std::string& token) {
      const Deck& deck = focusedDeck();
      if (auto index = cueIndexByToken(deck, token); index) {
        selectCueInDeck(project_.focusedDeckIndex, *index, false, false);
        return true;
      }
      return false;
    };

    switch (message.action) {
      case Action::Go:
      case Action::MmcPlay:
        // A GO with no cue number means "the next one", which is how a desk
        // drives a straight rundown.
        if (!message.cue.empty() && !selectByNumber(message.cue)) {
          triggerToast("MSC: no cue " + message.cue);
          return;
        }
        jumpSelectedCue();
        break;
      case Action::Load:
        // Preselect without firing: the desk is arming the next one.
        if (!message.cue.empty() && !selectByNumber(message.cue)) {
          triggerToast("MSC: no cue " + message.cue);
        }
        break;
      case Action::Stop:
      case Action::MmcStop:
        stopTransport();
        break;
      case Action::MmcPause:
      case Action::Resume:
        // RESUME is the counterpart of MSC STOP, and pauseTransport already
        // toggles -- one control for both directions, as the transport button
        // does.
        pauseTransport();
        break;
      case Action::AllOff:
        // No safety context: a desk sending ALL OFF has already decided.
        runPanicOutputsOff(false, 0);
        break;
      case Action::Reset:
        rerackTransport();
        break;
      case Action::MmcLocate:
        if (message.locateSeconds >= 0.0) {
          if (MediaEngine* engine = focusedMediaEngine()) {
            engine->seek(message.locateSeconds);
          }
        }
        break;
      default:
        break;
    }
  }

  void stopMidiInput() {
#if defined(DECKBOY_HAS_ALSA)
    midiStop_ = true;
    if (midiThread_.joinable()) midiThread_.join();
    if (midiSeq_) {
      if (midiSeqPort_ >= 0) snd_seq_delete_port(midiSeq_, midiSeqPort_);
      snd_seq_close(midiSeq_);
      midiSeq_ = nullptr;
      midiSeqPort_ = -1;
    }
    resetMidiMtcDecoder();
#elif defined(DECKBOY_HAS_MIDI)
    midiRt_.close();
#endif
  }

  // Polled from the update tick: the RtMidi wrapper dispatches its callbacks
  // from here, so they land on the main thread. No-op on ALSA (that path has
  // its own reader thread) and when MIDI isn't compiled in.
  void pumpMidiInput() {
#if !defined(DECKBOY_HAS_ALSA) && defined(DECKBOY_HAS_MIDI)
    if (midiEnabled_) {
      midiRt_.update();
    }
#endif
  }

  void midiLoop() {
#if defined(DECKBOY_HAS_ALSA)
    while (!midiStop_) {
      snd_seq_event_t* ev = nullptr;
      int r = snd_seq_event_input(midiSeq_, &ev);
      if (r < 0) {
        if (r == -EAGAIN) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        break;
      }
      if (!ev) continue;

      std::string cmd;
      switch (ev->type) {
        case SND_SEQ_EVENT_NOTEON:
          if (ev->data.note.velocity > 0) {
            // Note On: trigger cue at note index (1-based for GOTO)
            cmd = "GOTO " + std::to_string(ev->data.note.note + 1);
          }
          break;
        case SND_SEQ_EVENT_CONTROLLER:
          if (ev->data.control.param == 7) {
            // CC7 = volume: 0-127 -> 0-200%
            int pct = static_cast<int>(ev->data.control.value * 200.0 / 127.0);
            cmd = "MASTERVOL " + std::to_string(pct);
          } else if (ev->data.control.param == 20) {
            // CC20 = speed: 0-127 -> 0.5x-2.0x
            double spd = 0.5 + ev->data.control.value * 1.5 / 127.0;
            std::ostringstream ss; ss << std::fixed << std::setprecision(2) << spd;
            cmd = "SPEED " + ss.str();
          }
          break;
        case SND_SEQ_EVENT_QFRAME: {
          auto decoded = decodeMidiMtcQuarterFrame(ev->data.control.value);
          if (decoded) {
            double seconds = decoded->first;
            double fps = decoded->second;
            bool changed = std::fabs(seconds - midiMtcLastSentSeconds_) > (0.5 / std::max(1.0, fps))
                        || std::fabs(fps - midiMtcLastSentFps_) > 0.01;
            if (changed) {
              midiMtcLastSentSeconds_ = seconds;
              midiMtcLastSentFps_ = fps;
              std::ostringstream ss;
              ss << std::fixed << std::setprecision(6) << seconds;
              std::ostringstream fpsText;
              if (std::fabs(fps - 29.97) < 0.01) {
                fpsText << "29.97";
              } else {
                fpsText << std::fixed << std::setprecision(2) << fps;
              }
              cmd = "MTCEXT " + ss.str() + " " + fpsText.str();
            }
          }
          break;
        }
        case SND_SEQ_EVENT_SYSEX: {
          // Check for MMC (F0 7F <dev> 06 <cmd> F7)
          auto* data = static_cast<unsigned char*>(ev->data.ext.ptr);
          size_t len = ev->data.ext.len;
          if (len >= 6 && data[0] == 0xF0 && data[1] == 0x7F && data[3] == 0x06) {
            switch (data[4]) {
              case 0x01: cmd = "STOP"; break;   // MMC Stop
              case 0x02: cmd = "PLAY"; break;   // MMC Play
              case 0x03: cmd = "PLAY"; break;   // MMC Deferred Play
              case 0x05: cmd = "STOP"; break;   // MMC Record Exit (treat as stop)
              case 0x44: {                        // MMC Goto
                if (len >= 12 && data[5] == 0x06) {
                  // Timecode target: data[7]=hr, data[8]=min, data[9]=sec, data[10]=fr
                  double secs = data[7] * 3600.0 + data[8] * 60.0 + data[9] + data[10] / 30.0;
                  cmd = "SEEKPOS " + std::to_string(secs);
                }
                break;
              }
              default: break;
            }
          }
          // Check for MSC (F0 7F <dev> 02 <cmdFmt> 01 <cueNum> F7)
          if (len >= 7 && data[0] == 0xF0 && data[1] == 0x7F && data[3] == 0x02 && data[5] == 0x01) {
            int cueNum = data[6]; // cue number
            cmd = "GOTO " + std::to_string(cueNum);
          }
          break;
        }
        default: break;
      }

      if (!cmd.empty()) {
        std::lock_guard<std::mutex> lk(remoteCommandMutex_);
        remoteCommands_.push_back(PendingRemoteCommand {std::move(cmd), kInvalidSocket});
      }
      snd_seq_free_event(ev);
    }
#endif
  }

  void hyperDeckLoop() {
    // Each connected client gets a simple blocking handler in this loop.
    // We only handle one client at a time (sufficient for HyperDeck use).
    while (hyperDeckRunning_.load()) {
      fd_set readFds;
      FD_ZERO(&readFds);
      FD_SET(hyperDeckListenFd_, &readFds);
      timeval tv {0, 100000};  // 100ms timeout
      if (select(selectNfds(hyperDeckListenFd_), &readFds, nullptr, nullptr, &tv) <= 0) continue;
      sockaddr_in clientAddr {};
      socklen_t addrLen = sizeof(clientAddr);
      SocketHandle clientFd = accept(hyperDeckListenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
      if (clientFd == kInvalidSocket) continue;
      setCloseOnExec(clientFd);
      // Greet
      const char* greeting =
        "500 connection info:\r\n"
        "protocol version: 1.11\r\n"
        "model: HyperDeck Studio Mini\r\n"
        "\r\n";
      send(clientFd, greeting, static_cast<int>(strlen(greeting)), kSocketSendFlags);
      // Handle client
      std::string buf;
      constexpr size_t kHyperDeckMaxBuf = 65536;  // 64KB max pending data per client
      while (hyperDeckRunning_.load()) {
        char tmp[256];
        int n = recv(clientFd, tmp, static_cast<int>(sizeof(tmp) - 1), 0);
        if (n <= 0) break;
        tmp[n] = '\0';
        buf += tmp;
        // Disconnect clients that send too much data without line terminators
        if (buf.size() > kHyperDeckMaxBuf) {
          break;
        }
        // Process complete lines (\r\n terminated)
        size_t pos;
        while ((pos = buf.find("\r\n")) != std::string::npos) {
          std::string line = buf.substr(0, pos);
          buf.erase(0, pos + 2);
          // Trim trailing \r if any
          while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
          if (line.empty()) continue;

          std::string resp = hyperDeckHandleCommand(line);
          if (!resp.empty()) {
            send(clientFd, resp.c_str(), static_cast<int>(resp.size()), kSocketSendFlags);
          }
        }
      }
      closeSocket(clientFd);
    }
  }

  std::string hyperDeckHandleCommand(const std::string& line) {
    // Parse "verb: key: val" style
    std::string cmd = line;
    // Lowercase for matching
    std::string cmdL;
    for (char ch : cmd) cmdL += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    auto enqueueAndWait = [this](const std::string& rc) {
      enqueueRemoteCommand(rc);
    };

    if (cmdL == "play") {
      enqueueAndWait("PLAY");
      return "200 ok\r\n\r\n";
    }
    if (cmdL == "stop") {
      enqueueAndWait("STOP");
      return "200 ok\r\n\r\n";
    }
    if (cmdL.rfind("goto:", 0) == 0) {
      // "goto: clip id: N" or "goto: timeline: HH:MM:SS:FF"
      size_t ci = cmdL.find("clip id:");
      if (ci != std::string::npos) {
        std::string numStr = cmd.substr(ci + 8);
        while (!numStr.empty() && (numStr[0] == ' ' || numStr[0] == '\t')) numStr.erase(0, 1);
        enqueueAndWait("GOTO " + numStr);
        return "200 ok\r\n\r\n";
      }
      size_t ti = cmdL.find("timeline:");
      if (ti != std::string::npos) {
        std::string tcStr = cmd.substr(ti + 9);
        while (!tcStr.empty() && (tcStr[0] == ' ' || tcStr[0] == '\t')) tcStr.erase(0, 1);
        enqueueAndWait("TC SET " + tcStr);
        return "200 ok\r\n\r\n";
      }
      return "200 ok\r\n\r\n";
    }
    if (cmdL.rfind("play range:", 0) == 0) {
      size_t ci = cmdL.find("clip id:");
      if (ci != std::string::npos) {
        std::string numStr = cmd.substr(ci + 8);
        while (!numStr.empty() && (numStr[0] == ' ' || numStr[0] == '\t')) numStr.erase(0, 1);
        enqueueAndWait("GOTO " + numStr);
        enqueueAndWait("PLAY");
        return "200 ok\r\n\r\n";
      }
      return "200 ok\r\n\r\n";
    }
    // NOTE: these run on the HyperDeck TCP thread. Read ONLY the structured
    // hyperDeckSnapshot_ (rebuilt on the main thread each tick) — never
    // focusedDeck()/project_ directly, and never by substring-matching the
    // human-readable snapshot text (a cue named "playing" would corrupt it).
    if (cmdL == "transport info") {
      std::string status;
      int clipId = 0;
      bool loop = false;
      {
        std::lock_guard<std::mutex> lk(statusSnapshotMutex_);
        status = hyperDeckSnapshot_.transport;
        clipId = hyperDeckSnapshot_.clipId;
        loop = hyperDeckSnapshot_.loop;
      }
      std::ostringstream resp;
      resp << "208 transport info:\r\n"
           << "status: " << status << "\r\n"
           << "speed: 100\r\n"
           << "slot id: 1\r\n"
           << "clip id: " << clipId << "\r\n"
           << "single clip: false\r\n"
           << "display timecode: 00:00:00:00\r\n"
           << "timecode: 00:00:00:00\r\n"
           << "video format: 1080p25\r\n"
           << "loop: " << (loop ? "true" : "false") << "\r\n"
           << "\r\n";
      return resp.str();
    }
    if (cmdL == "clips count") {
      size_t clipCount = 0;
      {
        std::lock_guard<std::mutex> lk(statusSnapshotMutex_);
        clipCount = hyperDeckSnapshot_.clips.size();
      }
      std::ostringstream resp;
      resp << "214 clips count:\r\n"
           << "clip count: " << clipCount << "\r\n"
           << "\r\n";
      return resp.str();
    }
    if (cmdL == "clips get") {
      std::vector<std::pair<std::string, std::string>> clips;
      {
        std::lock_guard<std::mutex> lk(statusSnapshotMutex_);
        clips = hyperDeckSnapshot_.clips;
      }
      std::ostringstream resp;
      resp << "205 clips info:\r\n"
           << "clip count: " << clips.size() << "\r\n";
      for (size_t i = 0; i < clips.size(); ++i) {
        resp << (i + 1) << ": " << clips[i].first << " 00:00:00:00 "
             << clips[i].second << "\r\n";
      }
      resp << "\r\n";
      return resp.str();
    }
    if (cmdL == "device info") {
      return "500 device info:\r\n"
             "protocol version: 1.11\r\n"
             "model: HyperDeck Studio Mini\r\n"
             "unique id: DECKBOY00001\r\n"
             "\r\n";
    }
    // Unknown command
    return "109 unsupported parameter\r\n\r\n";
  }

  void companionLoop() {
    while (!companionStop_.load()) {
      fd_set readFds;
      FD_ZERO(&readFds);
      SocketHandle maxFd = 0;

      FD_SET(companionTcpListen_, &readFds);
      maxFd = std::max(maxFd, companionTcpListen_);
      FD_SET(companionUdpSocket_, &readFds);
      maxFd = std::max(maxFd, companionUdpSocket_);

      // Snapshot client list for select() FD setup (lock briefly, release before blocking select)
      std::vector<SocketHandle> clientSnapshot;
      {
        std::lock_guard<std::mutex> lk(companionClientsMutex_);
        for (SocketHandle client : companionClients_) {
          // A half-closed client reads EOF forever. Selecting on it would spin
          // this loop and keep pushing its deadline back; it is only waiting
          // for its reply now, not for us to read from it.
          if (companionDrainingClients_.count(client) == 0) {
            clientSnapshot.push_back(client);
          }
        }
      }
      for (auto client : clientSnapshot) {
        FD_SET(client, &readFds);
        maxFd = std::max(maxFd, client);
      }

      timeval timeout {};
      timeout.tv_sec = 0;
      timeout.tv_usec = 100000;  // 100ms (was 200ms)

      int ready = select(selectNfds(maxFd), &readFds, nullptr, nullptr, &timeout);
      if (ready < 0) {
#ifndef _WIN32
        if (errno == EINTR) {
          continue;
        }
#endif
        break;
      }
      if (ready == 0) {
        maybeBroadcastOscState();
        retireDrainedCompanionClients();
        continue;
      }

      // Accept new TCP clients (capped at 32 concurrent connections)
      if (FD_ISSET(companionTcpListen_, &readFds)) {
        sockaddr_in clientAddress {};
        socklen_t clientLength = sizeof(clientAddress);
        SocketHandle client = accept(companionTcpListen_, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
        if (client != kInvalidSocket) {
          setCloseOnExec(client);
          std::lock_guard<std::mutex> lk(companionClientsMutex_);
          constexpr size_t kMaxCompanionClients = 32;
          if (companionClients_.size() >= kMaxCompanionClients) {
            closeSocket(client);  // reject — too many connections
          } else {
            companionClients_.push_back(client);
            companionClientBuffers_[client] = "";
          }
        }
      }

      if (FD_ISSET(companionUdpSocket_, &readFds)) {
        std::array<char, 2048> buffer {};
        sockaddr_in sender {};
        socklen_t senderLen = sizeof(sender);
        int bytes = recvfrom(
          companionUdpSocket_,
          buffer.data(),
          static_cast<int>(buffer.size()),
          0,
          reinterpret_cast<sockaddr*>(&sender),
          &senderLen
        );
        if (bytes > 0) {
          std::string payload(buffer.data(), static_cast<size_t>(bytes));
          auto oscMessages = parseOscPacket(payload);
          if (!oscMessages.empty()) {
            rememberOscSubscriber(sender);
            for (const auto& osc : oscMessages) {
              std::string path = toUpper(trim(osc.address));
              if (path == "/STATUS" || path == "/STATE" || path == "/DECKBOY/STATUS" || path == "/DECKBOY/STATE") {
                sendOscStringTo(sender, "/deckboy/state", snapshotJsonForFeedback());
                continue;
              }
              if (path == "/PING" || path == "/DECKBOY/PING") {
                sendOscStringTo(sender, "/deckboy/pong", "DECKBOY_0.01");
                continue;
              }
              auto mapped = mapOscToRemoteCommand(osc);
              if (mapped && !mapped->empty()) {
                enqueueRemoteCommand(*mapped);
                sendOscStringTo(sender, "/deckboy/ack", *mapped);
              }
            }
          } else if (payload.find('\0') != std::string::npos) {
            continue;
          } else {
            enqueueRemoteCommandBatch(payload);
          }
        }
      }

      // Process TCP client data (lock held for client vector + buffer access)
      std::vector<SocketHandle> closedClients;
      {
        std::lock_guard<std::mutex> lk(companionClientsMutex_);
        for (auto client : companionClients_) {
          if (!FD_ISSET(client, &readFds) || companionDrainingClients_.count(client)) {
            continue;
          }

          std::array<char, 2048> buffer {};
          int bytes = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
          if (bytes <= 0) {
            closedClients.push_back(client);
            continue;
          }

          std::string& pending = companionClientBuffers_[client];
          pending.append(buffer.data(), static_cast<size_t>(bytes));

          // Disconnect clients that send too much data without newlines
          constexpr size_t kMaxCompanionClientBuf = 65536;  // 64KB
          if (pending.size() > kMaxCompanionClientBuf) {
            closedClients.push_back(client);
            continue;
          }

          size_t newlinePos = std::string::npos;
          while ((newlinePos = pending.find('\n')) != std::string::npos) {
            std::string line = trim(pending.substr(0, newlinePos));
            pending.erase(0, newlinePos + 1);
            if (!line.empty()) {
              if (!maybeRespondToCompanionQuery(client, line)) {
                // The client gets an OK/ERR line once the main thread has run
                // this command.
                enqueueRemoteCommand(line, client);
              }
            }
          }
        }

        // A client that stops sending is NOT necessarily gone. `printf 'TAKE\n'
        // | nc host 5510` — the obvious way to script this — half-closes the
        // moment its input ends, which arrives here as recv() == 0 while the
        // socket is still perfectly writable. Closing immediately threw away
        // the OK/ERR the main thread was about to produce, so the command ran
        // but the caller saw silence: the exact failure the acks exist to fix.
        //
        // So a half-closed client lingers briefly instead. Its commands are
        // already queued with it as the reply target, the queue drains every
        // frame, and the deadline bounds the wait if the peer is truly gone.
        for (auto client : closedClients) {
          auto pendingIt = companionClientBuffers_.find(client);
          if (pendingIt != companionClientBuffers_.end()) {
            std::string leftover = trim(pendingIt->second);
            if (!leftover.empty()) {
              if (!maybeRespondToCompanionQuery(client, leftover)) {
                enqueueRemoteCommand(leftover, client);
              }
            }
            companionClientBuffers_.erase(pendingIt);
          }
          companionDrainingClients_[client] = SDL_GetTicks() + kCompanionDrainMs;
        }

        retireDrainedCompanionClientsLocked();
      }
      retireDrainedCompanionClients();
      maybeBroadcastOscState();
    }
  }

  // ── TSL/Tally Protocol (cross-platform, UDP send-only) ─────────────────────
  // Sends TSL 3.1 tally packets to tally hardware on every deck state change.
  // One 20-byte packet per deck: 1 address byte, 1 control byte, 16 label bytes.

  void startTslTally() {
    stopTslTally();
    if (!project_.tslTallyEnabled) {
      return;
    }
    tslTallySocket_ = deckboy::platform::createDatagramSocket(true);
  }

  void stopTslTally() {
    if (tslTallySocket_ != deckboy::platform::kInvalidSocket) {
      deckboy::platform::closeSocket(tslTallySocket_);
      tslTallySocket_ = deckboy::platform::kInvalidSocket;
    }
  }

  void sendTslTallyState() {
    if (!project_.tslTallyEnabled || tslTallySocket_ == deckboy::platform::kInvalidSocket) {
      return;
    }
    int port = std::clamp(project_.tslTallyPort, 1, 65535);
    const std::string& host = project_.tslTallyAddress.empty()
      ? std::string("255.255.255.255")
      : project_.tslTallyAddress;

    sockaddr_in target {};
    target.sin_family = AF_INET;
    target.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &target.sin_addr) != 1) {
      target.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    }

    int numDecks = static_cast<int>(project_.decks.size());
    for (int i = 0; i < numDecks; ++i) {
      const Deck& deck = project_.decks[i];
      bool isPgm = (deck.activeIndex >= 0);
      bool isPvw = (!isPgm && i == project_.focusedDeckIndex);

      // TSL 3.1 control byte:
      //   bit7 = 1 (TSL 3.1 marker)
      //   bit6 = RH Tally (program / on-air)
      //   bit2 = LH Tally (preview)
      //   bits1-0 = brightness (3 = full)
      std::uint8_t ctrl = 0x83; // bit7 + bits1-0 (full brightness, no tally)
      if (isPgm) ctrl |= (1 << 6); // RH tally
      if (isPvw) ctrl |= (1 << 2); // LH tally

      // Build label: deck name, truncated/padded to 16 chars
      std::array<std::uint8_t, 20> packet {};
      packet[0] = static_cast<std::uint8_t>(std::clamp(i, 0, 255));
      packet[1] = ctrl;
      const std::string& label = deck.name;
      for (int c = 0; c < 16; ++c) {
        packet[2 + c] = (c < static_cast<int>(label.size()))
          ? static_cast<std::uint8_t>(label[c]) : ' ';
      }

#ifdef _WIN32
      sendto(
        static_cast<SOCKET>(tslTallySocket_),
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        deckboy::platform::kSocketSendFlags,
        reinterpret_cast<const sockaddr*>(&target),
        static_cast<int>(sizeof(target))
      );
#else
      sendto(
        static_cast<int>(tslTallySocket_),
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        deckboy::platform::kSocketSendFlags,
        reinterpret_cast<const sockaddr*>(&target),
        static_cast<socklen_t>(sizeof(target))
      );
#endif
    }
  }

  void notifyTallyStateChange() {
    if (project_.tslTallyEnabled) {
      sendTslTallyState();
    }
  }

  // Close half-closed clients whose linger has expired. Caller holds
  // companionClientsMutex_.
  void retireDrainedCompanionClientsLocked() {
    const Uint64 nowMs = SDL_GetTicks();
    for (auto it = companionDrainingClients_.begin(); it != companionDrainingClients_.end();) {
      if (nowMs < it->second) {
        ++it;
        continue;
      }
      SocketHandle client = it->first;
      it = companionDrainingClients_.erase(it);
      closeSocket(client);
      companionClients_.erase(
        std::remove(companionClients_.begin(), companionClients_.end(), client),
        companionClients_.end()
      );
    }
  }

  // Same, taking the lock. Runs on the idle path too — a quiet socket must
  // still be retired, or the descriptor lingers until the next packet arrives.
  void retireDrainedCompanionClients() {
    std::lock_guard<std::mutex> lk(companionClientsMutex_);
    retireDrainedCompanionClientsLocked();
  }

  // Write one line to a Companion client, from any thread. Takes the same lock
  // the network thread holds while it reads and answers queries, so the two
  // can't interleave mid-line, and skips a client that has since disconnected.
  void sendCompanionLine(SocketHandle client, const std::string& line) {
    if (client == kInvalidSocket) {
      return;
    }
    const std::string payload = line + "\n";
    std::lock_guard<std::mutex> lk(companionClientsMutex_);
    if (std::find(companionClients_.begin(), companionClients_.end(), client)
        == companionClients_.end()) {
      return;
    }
    send(client, payload.c_str(), static_cast<int>(payload.size()), kSocketSendFlags);
  }

  void processRemoteCommands() {
    std::deque<PendingRemoteCommand> pending;
    {
      std::lock_guard<std::mutex> lock(remoteCommandMutex_);
      pending.swap(remoteCommands_);
    }

    for (const auto& command : pending) {
      remoteCommandRecognized_ = true;
      remoteCommandDetail_.clear();
      handleRemoteCommand(command.text);
      if (command.replyTo == kInvalidSocket) {
        continue;
      }
      // Every command gets an answer. Without one the only feedback a command
      // has is a toast on the control window, which is useless to Companion,
      // to a script, and to anyone testing over SSH — an unknown verb and a
      // verb that worked were indistinguishable silence.
      auto parts = splitWhitespace(command.text);
      const std::string verb = parts.empty() ? std::string() : toUpper(parts[0]);
      std::string reply;
      if (!remoteCommandRecognized_) {
        reply = "ERR unknown command: " + verb;
      } else if (!remoteCommandError_.empty()) {
        reply = "ERR " + verb + ": " + remoteCommandError_;
      } else if (!remoteCommandDetail_.empty()) {
        // A query that ran on the main thread and has something to SAY. The
        // bare "OK VERB" is honest but useless to a script asking what the
        // state is -- FX LIST answered OK and put the list in a toast, where
        // nothing over the wire could read it.
        reply = "OK " + verb + ": " + remoteCommandDetail_;
      } else {
        reply = "OK " + verb;
      }
      sendCompanionLine(command.replyTo, reply);
    }
  }
