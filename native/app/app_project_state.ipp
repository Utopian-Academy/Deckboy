// ============================================================================
// app_project_state.ipp — Project save/load and file management.
//
// Implements .deckboy project file I/O using tab-delimited format:
//
//   Project file resolution:
//     defaultProjectFile()     — resolve DECKBOY_PROJECT env or default path
//     startupProjectFile()     — last-opened project or default
//     lastOpenedProjectPointerFile() — persists the most recently opened file
//
//   Save/load:
//     saveProject()            — serialize project to .deckboy file
//     loadProject()            — deserialize project from .deckboy file
//     autoSaveProject()        — periodic auto-save with dirty tracking
//     markProjectDirty()       — flag unsaved changes
//
//   Import/export:
//     importProjectFile()      — merge cues from another project
//     exportProjectBundleTo()  — export project + media as a portable bundle
//     importMediaFiles()       — batch import media files as new cues
//
//   Field serialization:
//     Tab-delimited format with escape sequences for special characters.
//     Backward compatibility: guard `if (fields.size() >= N)` for new fields.
//     See escapeField() / unescapeField() in main.cpp for the encoding.
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Returns the default project file path (from DECKBOY_PROJECT env or default).
  fs::path defaultProjectFile() const {
    const char* envPath = std::getenv("DECKBOY_PROJECT");
    if (envPath && *envPath) {
      std::error_code ec;
      return Paths::normalizeProjectPath(fs::absolute(envPath, ec));
    }
    return Paths::defaultProjectFile();
  }

  fs::path lastOpenedProjectPointerFile() const {
    return Paths::stateDir() / "last_project.txt";
  }

  fs::path startupProjectFile() const {
    const char* envPath = std::getenv("DECKBOY_PROJECT");
    if (envPath && *envPath) {
      return defaultProjectFile();
    }

    std::ifstream input(lastOpenedProjectPointerFile(), std::ios::binary);
    if (input) {
      std::string storedPath;
      std::getline(input, storedPath);
      fs::path remembered = normalizeProjectPath(trim(storedPath));
      if (!remembered.empty() && fs::exists(remembered)) {
        return remembered;
      }
    }
    return Paths::defaultProjectFile();
  }

  void rememberLastOpenedProjectFile(const fs::path& projectFile) const {
    if (projectFile.empty()) {
      return;
    }
    fs::path normalized = normalizeProjectPath(projectFile);
    std::error_code ec;
    fs::create_directories(lastOpenedProjectPointerFile().parent_path(), ec);
    std::ofstream output(lastOpenedProjectPointerFile(), std::ios::binary | std::ios::trunc);
    if (!output) {
      return;
    }
    output << normalized.string();
  }

  fs::path normalizeProjectPath(fs::path path) const {
    return Paths::normalizeProjectPath(path);
  }

  std::string currentProjectLabel() const {
    if (currentProjectFile_.empty()) {
      return "default.deckboy";
    }
    return currentProjectFile_.filename().string();
  }

  std::string currentAudioOutputLabel() const {
    return focusedDeck().audioOutputDeviceName.empty() ? "system default" : focusedDeck().audioOutputDeviceName;
  }

  std::string currentNdiOutputLabel() const {
    if (project_.outputs.empty()) {
      return "off";
    }
    int outputIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    const OutputTarget& output = project_.outputs[outputIndex];
    std::string source = output.ndiSourceName.empty()
      ? defaultOutputNdiSourceName(output, outputIndex)
      : output.ndiSourceName;
    std::string keySource = output.ndiKeySourceName.empty()
      ? defaultOutputNdiKeySourceName(output, outputIndex)
      : output.ndiKeySourceName;
    if (!output.ndiEnabled) {
      return "off";
    }
#if defined(DECKBOY_HAS_NDI_SDK)
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    bool live = runtime && runtime->ndiSender;
    bool keyLive = runtime && runtime->ndiKeySender;
    int listeners = ndiConnectionCountForOutput(outputIndex);
    int keyListeners = ndiKeyConnectionCountForOutput(outputIndex);
    std::string suffix = live ? "on" : "pending";
    if (listeners > 0) {
      suffix += " (" + std::to_string(listeners) + " rx)";
    }
    std::string label = suffix + " / fill:" + source;
    if (output.ndiKeyEnabled) {
      std::string keySuffix = keyLive ? "on" : "pending";
      if (keyListeners > 0) {
        keySuffix += " (" + std::to_string(keyListeners) + " rx)";
      }
      label += "  key:" + keySource + " [" + keySuffix + "]";
    }
    return label;
#else
    return "unavailable";
#endif
  }

  std::string currentTransitionLabel() const {
    const Deck& deck = focusedDeck();
    return transitionStyleToken(parseTransitionStyleToken(deck.transitionStyle)) + " " + formatSeconds(deck.transitionSeconds);
  }

  std::string currentTimecodeLabel() const {
    const Deck& deck = focusedDeck();
    return formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps) +
           " @" + std::to_string(static_cast<int>(std::round(deck.timecodeFps))) +
           (deck.timecodeChaseEnabled ? " chase" : " free");
  }

  std::string deckSummaryLabel() const {
    return focusedDeckLabel() + "  (" + std::to_string(project_.focusedDeckIndex + 1) + "/" + std::to_string(project_.decks.size()) + ")";
  }

  std::string deckStatusSummary(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "offline";
    }
    const Deck& deck = project_.decks[deckIndex];
    const Cue* activeCue = activeCuePtr(deckIndex);
    const MediaEngine* engine = mediaEngineForDeck(deckIndex);
    std::ostringstream output;
    output << (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << " | " << transportStatusLabel(deckIndex);
    if (activeCue) {
      output << " | " << activeCue->name;
    } else {
      output << " | idle";
    }
    if (engine) {
      output << " | " << formatSeconds(engine->position()) << "/" << formatSeconds(engine->duration());
    }
    output << " | " << deckOutputRoutingLabel(deckIndex);
    if (deck.timecodeChaseEnabled) {
      output << " | tc:" << formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps);
    }
    return output.str();
  }

  std::string cueNumberLabelForStatus(const Deck& deck, int cueIndex) const {
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return "";
    }
    return cueDisplayToken(deck.cues[cueIndex], cueIndex);
  }

  std::string cueIdForStatus(const Deck& deck, int cueIndex) const {
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return "";
    }
    return deck.cues[cueIndex].id;
  }

  std::string buildCueProgrammingSnapshot() const {
    std::ostringstream output;
    output << "DECKBOY_0.01 cues"
           << " focus=" << (project_.focusedDeckIndex + 1)
           << " decks=" << project_.decks.size();
    if (!lastCueFindToken_.empty() && !lastCueFindMatches_.empty()) {
      int cursor = std::clamp(lastCueFindCursor_, 0, static_cast<int>(lastCueFindMatches_.size()) - 1);
      output << " find=\"" << lastCueFindToken_ << "\""
             << " match=" << (cursor + 1) << "/" << lastCueFindMatches_.size()
             << " find_deck=" << (lastCueFindDeckIndex_ >= 0 ? lastCueFindDeckIndex_ + 1 : 0);
    } else {
      output << " find=none";
    }
    output << '\n';

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      const Deck& deck = project_.decks[deckIndex];
      std::string selectedNum = cueNumberLabelForStatus(deck, deck.selectedIndex);
      std::string selectedId = cueIdForStatus(deck, deck.selectedIndex);
      std::string activeNum = cueNumberLabelForStatus(deck, deck.activeIndex);
      std::string activeId = cueIdForStatus(deck, deck.activeIndex);
      output << "CUEDECK " << (deckIndex + 1)
             << " name=\"" << (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << "\""
             << " selected_num=\"" << selectedNum << "\""
             << " selected_id=\"" << selectedId << "\""
             << " active_num=\"" << activeNum << "\""
             << " active_id=\"" << activeId << "\"";
      if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
        output << " selected_name=\"" << deck.cues[deck.selectedIndex].name << "\"";
      }
      if (deck.activeIndex >= 0 && deck.activeIndex < static_cast<int>(deck.cues.size())) {
        output << " active_name=\"" << deck.cues[deck.activeIndex].name << "\"";
      }
      output << '\n';
    }
    return output.str();
  }

  std::string buildStatusSnapshot() const {
    std::ostringstream output;
    output << "DECKBOY_0.01"
           << " focus=" << (project_.focusedDeckIndex + 1)
           << " decks=" << project_.decks.size()
           << " focused_output=" << (project_.focusedOutputIndex + 1)
           << " outputs=" << project_.outputs.size()
           << " panic_profile=" << normalizePanicProfileToken(project_.panicProfile)
           << " panic_fade_s=" << project_.panicFadeSeconds
           << " panic_restore=" << (project_.panicAutoRestore ? "on" : "off")
           << " find=\"" << lastCueFindToken_ << "\""
           << " find_matches=" << lastCueFindMatches_.size()
           << " find_cursor=" << (lastCueFindCursor_ >= 0 ? lastCueFindCursor_ + 1 : 0)
           << " find_deck=" << (lastCueFindDeckIndex_ >= 0 ? lastCueFindDeckIndex_ + 1 : 0)
           << " video_mode=" << (project_.outputFollowDisplay ? "native" : "fixed")
           << " video_hz=" << formatRefreshRateLabel(project_.outputRefreshRateHz)
           << " video_depth=" << outputBitDepthModeLabel()
           << " canvas=" << (project_.outputCanvasEnabled
                ? (std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
                : "off")
           << " integrations=\"" << integrationBackendRouteSummary() << "\""
           << " master_dimmer=" << static_cast<int>(std::round(std::clamp(masterDimmerTarget_, 0.0, 1.0) * 100.0))
           << " blackout=" << (masterDimmerTarget_ <= 0.001 ? "on" : "off")
           << " master_vol=" << static_cast<int>(std::round(std::clamp(project_.masterVolume, 0.0, 2.0) * 100.0))
           << '\n';
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      const Deck& deck = project_.decks[deckIndex];
      auto outputIndexOpt = primaryOutputIndexForDeck(deckIndex);
      int outputIndex = outputIndexOpt ? *outputIndexOpt : -1;
      int displayIndex = outputIndex >= 0 ? outputDisplayIndex(outputIndex) : deck.outputDisplayIndex;
      int layerIndex = primaryLayerIndexForDeck(deckIndex);
      std::string routeLabel = outputIndex >= 0 ? std::to_string(outputIndex + 1) : "--";
      std::string rasterLabel = outputIndex >= 0 ? outputResolutionLabelForOutput(outputIndex) : outputResolutionLabel(deckIndex);
      std::string depthLabel = outputIndex >= 0 ? outputBitDepthActiveLabelForOutput(outputIndex) : outputBitDepthActiveLabel(deckIndex);
      const Cue* activeCue = activeCuePtr(deckIndex);
      const Cue* selectedCue = selectedCuePtr(deckIndex);
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      std::string decodeFps = deckDecodeFpsLabel(deckIndex);
      std::string selectedNum = cueNumberLabelForStatus(deck, deck.selectedIndex);
      std::string selectedId = cueIdForStatus(deck, deck.selectedIndex);
      std::string activeNum = cueNumberLabelForStatus(deck, deck.activeIndex);
      std::string activeId = cueIdForStatus(deck, deck.activeIndex);
      output << "DECK " << (deckIndex + 1)
             << " name=\"" << (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << "\""
             << " status=" << transportStatusLabel(deckIndex)
             << " selected=" << (deck.selectedIndex >= 0 ? deck.selectedIndex + 1 : 0)
             << " active=" << (deck.activeIndex >= 0 ? deck.activeIndex + 1 : 0)
             << " selected_num=\"" << selectedNum << "\""
             << " selected_id=\"" << selectedId << "\""
             << " active_num=\"" << activeNum << "\""
             << " active_id=\"" << activeId << "\""
             << " display=" << (displayIndex + 1)
             << " route=" << routeLabel
             << " layer=" << layerIndex
             << " raster=" << rasterLabel
             << " depth=" << depthLabel
             << " audio=\"" << (deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\""
             << " overlay=" << (deck.timeOverlayEnabled ? "on" : "off")
             << " view=" << deck.canvasViewX << "," << deck.canvasViewY
             << " warp=" << (deck.warpEnabled ? "on" : "off")
             << " warp_mode=" << normalizeWarpMode(deck.warpMode)
             << " blend=" << static_cast<int>(std::lround(deck.edgeBlendLeft * 100.0f))
             << "," << static_cast<int>(std::lround(deck.edgeBlendRight * 100.0f))
             << "," << static_cast<int>(std::lround(deck.edgeBlendTop * 100.0f))
             << "," << static_cast<int>(std::lround(deck.edgeBlendBottom * 100.0f))
             << " transition=" << transitionStyleToken(parseTransitionStyleToken(deck.transitionStyle))
             << " transition_s=" << deck.transitionSeconds
             << " tc=" << formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps)
             << " tc_chase=" << (deck.timecodeChaseEnabled ? "on" : "off")
             << " tc_run=" << (deck.timecodeRunEnabled ? "on" : "off")
             << " tc_trigger=" << (deck.timecodeTriggerEnabled ? "on" : "off")
             << " tc_jam=" << (deck.timecodeJamSyncEnabled ? "on" : "off")
             << " tc_freewheel_s=" << deck.timecodeFreewheelSeconds
             << " cue=\"" << (activeCue ? activeCue->name : (selectedCue ? selectedCue->name : "")) << "\"";
      if (activeCue) {
        output << " cue_id=\"" << activeCue->id << "\""
               << " in=" << formatSeconds(activeCue->inPointSeconds)
               << " out=" << formatSeconds(activeCue->outPointSeconds > 0.0 ? activeCue->outPointSeconds : activeCue->duration)
               << " tc_mark=" << (activeCue->triggerTimecodeSeconds >= 0.0 ? formatTimecode(activeCue->triggerTimecodeSeconds, deck.timecodeFps) : "--:--:--:--");
      }
      if (engine) {
        output << " pos=" << formatSeconds(engine->position())
               << " dur=" << formatSeconds(engine->duration())
               << " vol=" << static_cast<int>(std::round(engine->volume() * 100.0f))
               << " decode_fps=" << decodeFps;
      }
      output << '\n';
    }
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      const OutputTarget& out = project_.outputs[outputIndex];
      int hostDeckIndex = std::clamp(out.hostDeckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
      int layerCount = 1; // Single-deck: always one layer
      std::string type = normalizeOutputType(out.outputType);
      std::string protocol = normalizeOutputStreamProtocol(out.streamProtocol);
      std::string mirror = out.mirrorSourceOutputIndex >= 0 ? std::to_string(out.mirrorSourceOutputIndex + 1) : "off";
      std::string url = trim(out.streamUrl);
      int alphaPct = static_cast<int>(std::lround(std::clamp(out.outputAlpha, 0.0f, 1.0f) * 100.0f));
      std::string backendRoute = outputBackendRouteSummary(outputIndex);
      std::string ndiSource = trim(out.ndiSourceName).empty() ? defaultOutputNdiSourceName(out, outputIndex) : out.ndiSourceName;
      std::string ndiKeySource = trim(out.ndiKeySourceName).empty() ? defaultOutputNdiKeySourceName(out, outputIndex) : out.ndiKeySourceName;
      std::string health = outputHealthLabel(outputIndex);
      std::string healthReason = outputHealthReason(outputIndex);
      std::string outputFps = outputFpsLabel(outputIndex);
      std::string streamFps = outputStreamFpsLabel(outputIndex);
      std::uint64_t streamQueued = 0;
      std::uint64_t streamWritten = 0;
      std::uint64_t streamVideoBytes = 0;
      std::uint64_t streamAudioBytes = 0;
      if (const OutputRuntime* runtime = runtimeForOutput(outputIndex); runtime && runtime->streamWriter) {
        std::lock_guard<std::mutex> lock(runtime->streamWriter->mutex);
        streamQueued = runtime->streamWriter->packetsQueued;
        streamWritten = runtime->streamWriter->packetsWritten;
        streamVideoBytes = runtime->streamWriter->videoBytesWritten;
        streamAudioBytes = runtime->streamWriter->audioBytesWritten;
      }
      if (url.empty()) {
        url = defaultOutputStreamUrl(protocol, outputIndex);
      }
      output << "OUTPUT " << (outputIndex + 1)
             << " name=\"" << (out.name.empty() ? outputDefaultName(outputIndex) : out.name) << "\""
             << " id=\"" << out.outputId << "\""
             << " enabled=" << (out.enabled ? "on" : "off")
             << " health=" << toLower(health)
             << " type=" << type
             << " host=" << (hostDeckIndex + 1)
             << " display=" << (outputDisplayIndex(outputIndex) + 1)
             << " layers=" << layerCount
             << " mirror=" << mirror
             << " stream=" << (out.streamEnabled ? "on" : "off")
             << " proto=" << protocol
             << " bitrate=" << out.streamBitrateKbps
             << " url=\"" << url << "\""
             << " ndi=" << (out.ndiEnabled ? "on" : "off")
             << " ndi_name=\"" << ndiSource << "\""
             << " ndi_rx=" << ndiConnectionCountForOutput(outputIndex)
             << " ndi_key=" << (out.ndiKeyEnabled ? "on" : "off")
             << " ndi_key_name=\"" << ndiKeySource << "\""
             << " ndi_key_rx=" << ndiKeyConnectionCountForOutput(outputIndex)
             << " alpha=" << alphaPct
             << " delay_ms=" << std::clamp(out.outputDelayMs, 0, 5000)
             << " overlay=" << (out.outputTimeOverlayEnabled ? "on" : "off")
             << " color_space=" << normalizeOutputColorSpace(out.outputColorSpace)
             << " layout=" << normalizeOutputLayoutMode(out.outputLayoutMode)
             << " orientation=" << normalizeOutputOrientationDegrees(out.outputOrientationDegrees)
             << " test_card=" << (out.outputTestCardEnabled ? "on" : "off")
             << " output_fps=" << outputFps
             << " stream_fps=" << streamFps
             << " stream_q=" << streamQueued
             << " stream_sent=" << streamWritten
             << " stream_vbytes=" << streamVideoBytes
             << " stream_abytes=" << streamAudioBytes
             << " backend=" << backendRoute
             << (healthReason.empty() ? "" : (" health_reason=\"" + healthReason + "\""))
             << '\n';
    }
    return output.str();
  }

  std::string buildDeckStatusSnapshot(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "DECK offline\n";
    }
    std::ostringstream output;
    const Deck& deck = project_.decks[deckIndex];
    auto outputIndexOpt = primaryOutputIndexForDeck(deckIndex);
    int outputIndex = outputIndexOpt ? *outputIndexOpt : -1;
    int displayIndex = outputIndex >= 0 ? outputDisplayIndex(outputIndex) : deck.outputDisplayIndex;
    int layerIndex = primaryLayerIndexForDeck(deckIndex);
    std::string routeLabel = outputIndex >= 0 ? std::to_string(outputIndex + 1) : "--";
    std::string rasterLabel = outputIndex >= 0 ? outputResolutionLabelForOutput(outputIndex) : outputResolutionLabel(deckIndex);
    std::string depthLabel = outputIndex >= 0 ? outputBitDepthActiveLabelForOutput(outputIndex) : outputBitDepthActiveLabel(deckIndex);
    const Cue* activeCue = activeCuePtr(deckIndex);
    const Cue* selectedCue = selectedCuePtr(deckIndex);
    const MediaEngine* engine = mediaEngineForDeck(deckIndex);
    std::string decodeFps = deckDecodeFpsLabel(deckIndex);
    std::string selectedNum = cueNumberLabelForStatus(deck, deck.selectedIndex);
    std::string selectedId = cueIdForStatus(deck, deck.selectedIndex);
    std::string activeNum = cueNumberLabelForStatus(deck, deck.activeIndex);
    std::string activeId = cueIdForStatus(deck, deck.activeIndex);
    output << "DECKBOY_0.01"
           << " focus=" << (project_.focusedDeckIndex + 1)
           << " decks=" << project_.decks.size()
           << " panic_profile=" << normalizePanicProfileToken(project_.panicProfile)
           << " panic_fade_s=" << project_.panicFadeSeconds
           << " panic_restore=" << (project_.panicAutoRestore ? "on" : "off")
           << " find=\"" << lastCueFindToken_ << "\""
           << " find_matches=" << lastCueFindMatches_.size()
           << " find_cursor=" << (lastCueFindCursor_ >= 0 ? lastCueFindCursor_ + 1 : 0)
           << " find_deck=" << (lastCueFindDeckIndex_ >= 0 ? lastCueFindDeckIndex_ + 1 : 0)
           << " video_mode=" << (project_.outputFollowDisplay ? "native" : "fixed")
           << " video_hz=" << formatRefreshRateLabel(project_.outputRefreshRateHz)
           << " video_depth=" << outputBitDepthModeLabel()
           << " canvas=" << (project_.outputCanvasEnabled
                ? (std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
                : "off")
           << " integrations=\"" << integrationBackendRouteSummary() << "\""
           << '\n';
    output << "DECK " << (deckIndex + 1)
           << " name=\"" << (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << "\""
           << " status=" << transportStatusLabel(deckIndex)
           << " selected=" << (deck.selectedIndex >= 0 ? deck.selectedIndex + 1 : 0)
           << " active=" << (deck.activeIndex >= 0 ? deck.activeIndex + 1 : 0)
           << " selected_num=\"" << selectedNum << "\""
           << " selected_id=\"" << selectedId << "\""
           << " active_num=\"" << activeNum << "\""
           << " active_id=\"" << activeId << "\""
           << " display=" << (displayIndex + 1)
           << " route=" << routeLabel
           << " layer=" << layerIndex
           << " raster=" << rasterLabel
           << " depth=" << depthLabel
           << " audio=\"" << (deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\""
           << " overlay=" << (deck.timeOverlayEnabled ? "on" : "off")
           << " view=" << deck.canvasViewX << "," << deck.canvasViewY
           << " warp=" << (deck.warpEnabled ? "on" : "off")
           << " warp_mode=" << normalizeWarpMode(deck.warpMode)
           << " blend=" << static_cast<int>(std::lround(deck.edgeBlendLeft * 100.0f))
           << "," << static_cast<int>(std::lround(deck.edgeBlendRight * 100.0f))
           << "," << static_cast<int>(std::lround(deck.edgeBlendTop * 100.0f))
           << "," << static_cast<int>(std::lround(deck.edgeBlendBottom * 100.0f))
           << " transition=" << transitionStyleToken(parseTransitionStyleToken(deck.transitionStyle))
           << " transition_s=" << deck.transitionSeconds
           << " tc=" << formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps)
           << " tc_chase=" << (deck.timecodeChaseEnabled ? "on" : "off")
           << " tc_run=" << (deck.timecodeRunEnabled ? "on" : "off")
           << " tc_trigger=" << (deck.timecodeTriggerEnabled ? "on" : "off")
           << " tc_jam=" << (deck.timecodeJamSyncEnabled ? "on" : "off")
           << " tc_freewheel_s=" << deck.timecodeFreewheelSeconds
           << " cue=\"" << (activeCue ? activeCue->name : (selectedCue ? selectedCue->name : "")) << "\"";
    if (activeCue) {
      output << " cue_id=\"" << activeCue->id << "\""
             << " in=" << formatSeconds(activeCue->inPointSeconds)
             << " out=" << formatSeconds(activeCue->outPointSeconds > 0.0 ? activeCue->outPointSeconds : activeCue->duration)
             << " tc_mark=" << (activeCue->triggerTimecodeSeconds >= 0.0 ? formatTimecode(activeCue->triggerTimecodeSeconds, deck.timecodeFps) : "--:--:--:--");
    }
    if (engine) {
      output << " pos=" << formatSeconds(engine->position())
             << " dur=" << formatSeconds(engine->duration())
             << " vol=" << static_cast<int>(std::round(engine->volume() * 100.0f))
             << " decode_fps=" << decodeFps;
    }
    output << '\n';
    return output.str();
  }

  std::string buildStatusSnapshotJson() const {
    IntegrationBackendRuntimeRoute integrationRoute = resolveIntegrationBackendRuntimeRoute();
    std::ostringstream output;
    output << "{"
           << "\"app\":\"DECKBOY_0.01\","
           << "\"focusedDeck\":" << (project_.focusedDeckIndex + 1) << ","
           << "\"deckCount\":" << project_.decks.size() << ","
           << "\"focusedOutput\":" << (project_.focusedOutputIndex + 1) << ","
           << "\"outputCount\":" << project_.outputs.size() << ","
           << "\"panicProfile\":\"" << escapeJson(normalizePanicProfileToken(project_.panicProfile)) << "\","
           << "\"panicFadeSeconds\":" << project_.panicFadeSeconds << ","
           << "\"panicAutoRestore\":" << (project_.panicAutoRestore ? "true" : "false") << ","
           << "\"masterDimmer\":" << static_cast<int>(std::round(std::clamp(masterDimmerTarget_, 0.0, 1.0) * 100.0)) << ","
           << "\"blackout\":" << (masterDimmerTarget_ <= 0.001 ? "true" : "false") << ","
           << "\"masterVolume\":" << static_cast<int>(std::round(std::clamp(project_.masterVolume, 0.0, 2.0) * 100.0)) << ","
           << "\"findToken\":\"" << escapeJson(lastCueFindToken_) << "\","
           << "\"findMatchCount\":" << lastCueFindMatches_.size() << ","
           << "\"findCursor\":" << (lastCueFindCursor_ >= 0 ? lastCueFindCursor_ + 1 : 0) << ","
           << "\"findDeck\":" << (lastCueFindDeckIndex_ >= 0 ? lastCueFindDeckIndex_ + 1 : 0) << ","
           << "\"outputMode\":\"" << (project_.outputFollowDisplay ? "native" : "fixed") << "\","
           << "\"outputRefreshHz\":" << project_.outputRefreshRateHz << ","
           << "\"outputBitDepthMode\":\"" << escapeJson(outputBitDepthModeLabel()) << "\","
           << "\"outputCanvasEnabled\":" << (project_.outputCanvasEnabled ? "true" : "false") << ","
           << "\"outputCanvasWidth\":" << project_.outputCanvasWidth << ","
           << "\"outputCanvasHeight\":" << project_.outputCanvasHeight << ","
           << "\"integrationRoute\":\"" << escapeJson(integrationRoute.summary) << "\","
           << "\"integrations\":{"
           << "\"atem\":" << (project_.atemTriggerEnabled ? "true" : "false") << ","
           << "\"ndiTrigger\":" << (project_.ndiTriggerEnabled ? "true" : "false") << ","
           << "\"nmc\":" << (project_.nmcSyncEnabled ? "true" : "false") << ","
           << "\"nmcMode\":\"" << escapeJson(integrationRoute.nmcSyncMode.empty() ? resolvedNmcSyncMode() : integrationRoute.nmcSyncMode) << "\","
           << "\"nmcRuntime\":\"" << escapeJson(describeNmcSyncRuntime()) << "\","
           << "\"mtc\":" << (project_.mtcIngestEnabled ? "true" : "false") << ","
           << "\"ltc\":" << (project_.ltcIngestEnabled ? "true" : "false") << ","
           << "\"dmxArtNet\":" << (project_.dmxArtNetEnabled ? "true" : "false") << ","
           << "\"artNetPort\":" << project_.artNetPort << ","
           << "\"atemSupported\":" << (integrationRoute.atemSupported ? "true" : "false") << ","
           << "\"ndiTriggerSupported\":" << (integrationRoute.ndiTriggerSupported ? "true" : "false") << ","
           << "\"nmcSupported\":" << (integrationRoute.nmcSyncSupported ? "true" : "false") << ","
           << "\"mtcSupported\":" << (integrationRoute.mtcIngestSupported ? "true" : "false") << ","
           << "\"ltcSupported\":" << (integrationRoute.ltcIngestSupported ? "true" : "false") << ","
           << "\"dmxArtNetSupported\":" << (integrationRoute.dmxArtNetSupported ? "true" : "false")
           << "},"
           << "\"outputs\":[";
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (outputIndex > 0) {
        output << ",";
      }
      const OutputTarget& out = project_.outputs[outputIndex];
      int hostDeckIndex = std::clamp(out.hostDeckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
      int layerCount = 1; // Single-deck: always one layer
      std::string type = normalizeOutputType(out.outputType);
      std::string protocol = normalizeOutputStreamProtocol(out.streamProtocol);
      std::string url = trim(out.streamUrl);
      int alphaPct = static_cast<int>(std::lround(std::clamp(out.outputAlpha, 0.0f, 1.0f) * 100.0f));
      std::string backendRoute = outputBackendRouteSummary(outputIndex);
      std::string ndiSource = trim(out.ndiSourceName).empty() ? defaultOutputNdiSourceName(out, outputIndex) : out.ndiSourceName;
      std::string ndiKeySource = trim(out.ndiKeySourceName).empty() ? defaultOutputNdiKeySourceName(out, outputIndex) : out.ndiKeySourceName;
      std::string health = outputHealthLabel(outputIndex);
      std::string healthReason = outputHealthReason(outputIndex);
      const OutputRuntime* runtime = runtimeForOutput(outputIndex);
      double outputFps = runtime ? runtime->fpsMeasured : 0.0;
      double streamFps = runtime ? runtime->streamFpsMeasured : 0.0;
      if (url.empty()) {
        url = defaultOutputStreamUrl(protocol, outputIndex);
      }
      output << "{"
             << "\"index\":" << (outputIndex + 1) << ","
             << "\"name\":\"" << escapeJson(out.name.empty() ? outputDefaultName(outputIndex) : out.name) << "\","
             << "\"outputId\":\"" << escapeJson(out.outputId) << "\","
             << "\"type\":\"" << escapeJson(type) << "\","
             << "\"hostDeck\":" << (hostDeckIndex + 1) << ","
             << "\"display\":" << (outputDisplayIndex(outputIndex) + 1) << ","
             << "\"enabled\":" << (out.enabled ? "true" : "false") << ","
             << "\"health\":\"" << escapeJson(health) << "\","
             << "\"healthReason\":\"" << escapeJson(healthReason) << "\","
             << "\"mirrorSourceOutput\":" << (out.mirrorSourceOutputIndex >= 0 ? out.mirrorSourceOutputIndex + 1 : 0) << ","
             << "\"layerCount\":" << layerCount << ","
             << "\"streamEnabled\":" << (out.streamEnabled ? "true" : "false") << ","
             << "\"streamProtocol\":\"" << escapeJson(protocol) << "\","
             << "\"streamUrl\":\"" << escapeJson(url) << "\","
             << "\"streamBitrateKbps\":" << out.streamBitrateKbps << ","
             << "\"ndiEnabled\":" << (out.ndiEnabled ? "true" : "false") << ","
             << "\"ndiName\":\"" << escapeJson(ndiSource) << "\","
             << "\"ndiReceivers\":" << ndiConnectionCountForOutput(outputIndex) << ","
             << "\"ndiKeyEnabled\":" << (out.ndiKeyEnabled ? "true" : "false") << ","
             << "\"ndiKeyName\":\"" << escapeJson(ndiKeySource) << "\","
             << "\"ndiKeyReceivers\":" << ndiKeyConnectionCountForOutput(outputIndex) << ","
             << "\"outputAlphaPercent\":" << alphaPct << ","
             << "\"outputDelayMs\":" << std::clamp(out.outputDelayMs, 0, 5000) << ","
             << "\"outputTimeOverlay\":" << (out.outputTimeOverlayEnabled ? "true" : "false") << ","
             << "\"outputColorSpace\":\"" << escapeJson(normalizeOutputColorSpace(out.outputColorSpace)) << "\","
             << "\"outputLayoutMode\":\"" << escapeJson(normalizeOutputLayoutMode(out.outputLayoutMode)) << "\","
             << "\"outputOrientation\":" << normalizeOutputOrientationDegrees(out.outputOrientationDegrees) << ","
             << "\"outputTestCard\":" << (out.outputTestCardEnabled ? "true" : "false") << ","
             << "\"outputFps\":" << outputFps << ","
             << "\"streamFps\":" << streamFps << ","
             << "\"backendRoute\":\"" << escapeJson(backendRoute) << "\""
             << "}";
    }
    output << "],"
           << "\"decks\":[";
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (deckIndex > 0) {
        output << ",";
      }
      const Deck& deck = project_.decks[deckIndex];
      auto outputIndexOpt = primaryOutputIndexForDeck(deckIndex);
      int outputIndex = outputIndexOpt ? *outputIndexOpt : -1;
      int displayIndex = outputIndex >= 0 ? outputDisplayIndex(outputIndex) : deck.outputDisplayIndex;
      int layerIndex = primaryLayerIndexForDeck(deckIndex);
      std::string rasterLabel = outputIndex >= 0 ? outputResolutionLabelForOutput(outputIndex) : outputResolutionLabel(deckIndex);
      std::string depthLabel = outputIndex >= 0 ? outputBitDepthActiveLabelForOutput(outputIndex) : outputBitDepthActiveLabel(deckIndex);
      const Cue* activeCue = activeCuePtr(deckIndex);
      const Cue* selectedCue = selectedCuePtr(deckIndex);
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      double decodeFps = engine ? engine->mediaFpsMeasured() : 0.0;
      std::string selectedNum = cueNumberLabelForStatus(deck, deck.selectedIndex);
      std::string selectedId = cueIdForStatus(deck, deck.selectedIndex);
      std::string activeNum = cueNumberLabelForStatus(deck, deck.activeIndex);
      std::string activeId = cueIdForStatus(deck, deck.activeIndex);
      output << "{"
             << "\"index\":" << (deckIndex + 1) << ","
             << "\"name\":\"" << escapeJson(deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << "\","
             << "\"status\":\"" << escapeJson(transportStatusLabel(deckIndex)) << "\","
             << "\"selected\":" << (deck.selectedIndex >= 0 ? deck.selectedIndex + 1 : 0) << ","
             << "\"active\":" << (deck.activeIndex >= 0 ? deck.activeIndex + 1 : 0) << ","
             << "\"selectedCueNumber\":\"" << escapeJson(selectedNum) << "\","
             << "\"selectedCueId\":\"" << escapeJson(selectedId) << "\","
             << "\"activeCueNumber\":\"" << escapeJson(activeNum) << "\","
             << "\"activeCueId\":\"" << escapeJson(activeId) << "\","
             << "\"display\":" << (displayIndex + 1) << ","
             << "\"routeOutput\":" << (outputIndex + 1) << ","
             << "\"layer\":" << layerIndex << ","
             << "\"raster\":\"" << rasterLabel << "\","
             << "\"outputDepth\":\"" << depthLabel << "\","
             << "\"audio\":\"" << escapeJson(deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\","
             << "\"timeOverlay\":" << (deck.timeOverlayEnabled ? "true" : "false") << ","
             << "\"canvasViewX\":" << deck.canvasViewX << ","
             << "\"canvasViewY\":" << deck.canvasViewY << ","
             << "\"warpEnabled\":" << (deck.warpEnabled ? "true" : "false") << ","
             << "\"warpMode\":\"" << escapeJson(normalizeWarpMode(deck.warpMode)) << "\","
             << "\"warpTopLeftX\":" << deck.warpTopLeftX << ","
             << "\"warpTopLeftY\":" << deck.warpTopLeftY << ","
             << "\"warpTopRightX\":" << deck.warpTopRightX << ","
             << "\"warpTopRightY\":" << deck.warpTopRightY << ","
             << "\"warpBottomRightX\":" << deck.warpBottomRightX << ","
             << "\"warpBottomRightY\":" << deck.warpBottomRightY << ","
             << "\"warpBottomLeftX\":" << deck.warpBottomLeftX << ","
             << "\"warpBottomLeftY\":" << deck.warpBottomLeftY << ","
             << "\"edgeBlendLeft\":" << deck.edgeBlendLeft << ","
             << "\"edgeBlendRight\":" << deck.edgeBlendRight << ","
             << "\"edgeBlendTop\":" << deck.edgeBlendTop << ","
             << "\"edgeBlendBottom\":" << deck.edgeBlendBottom << ","
             << "\"transitionStyle\":\"" << escapeJson(transitionStyleToken(parseTransitionStyleToken(deck.transitionStyle))) << "\","
             << "\"transitionSeconds\":" << deck.transitionSeconds << ","
             << "\"timecode\":\"" << escapeJson(formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps)) << "\","
             << "\"timecodeFps\":" << deck.timecodeFps << ","
             << "\"timecodeChase\":" << (deck.timecodeChaseEnabled ? "true" : "false") << ","
             << "\"timecodeRun\":" << (deck.timecodeRunEnabled ? "true" : "false") << ","
             << "\"timecodeTrigger\":" << (deck.timecodeTriggerEnabled ? "true" : "false") << ","
             << "\"timecodeJam\":" << (deck.timecodeJamSyncEnabled ? "true" : "false") << ","
             << "\"timecodeFreewheelSeconds\":" << deck.timecodeFreewheelSeconds << ","
             << "\"cue\":\"" << escapeJson(activeCue ? activeCue->name : (selectedCue ? selectedCue->name : "")) << "\"";
      if (activeCue) {
        output << ",\"cueId\":\"" << escapeJson(activeCue->id) << "\""
               << ",\"cueIn\":\"" << escapeJson(formatSeconds(activeCue->inPointSeconds)) << "\""
               << ",\"cueOut\":\"" << escapeJson(formatSeconds(activeCue->outPointSeconds > 0.0 ? activeCue->outPointSeconds : activeCue->duration)) << "\""
               << ",\"cueTriggerTc\":\"" << (activeCue->triggerTimecodeSeconds >= 0.0 ? escapeJson(formatTimecode(activeCue->triggerTimecodeSeconds, deck.timecodeFps)) : std::string("")) << "\"";
      }
      if (engine) {
        output << ",\"position\":\"" << escapeJson(formatSeconds(engine->position())) << "\""
               << ",\"duration\":\"" << escapeJson(formatSeconds(engine->duration())) << "\""
               << ",\"volume\":" << static_cast<int>(std::round(engine->volume() * 100.0f))
               << ",\"decodeFps\":" << decodeFps;
      }
      output << "}";
    }
    output << "]}\n";
    return output.str();
  }

  void updateStatusSnapshot() {
    std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
    statusSnapshot_ = buildStatusSnapshot();
    statusSnapshotJson_ = buildStatusSnapshotJson();
    statusCueSnapshot_ = buildCueProgrammingSnapshot();
    statusDeckSnapshots_.clear();
    statusDeckSnapshots_.reserve(project_.decks.size());
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      statusDeckSnapshots_.push_back(buildDeckStatusSnapshot(deckIndex));
    }
    hyperDeckSnapshot_.transport = "stopped";
    if (const MediaEngine* engine = mediaEngineForDeck(project_.focusedDeckIndex)) {
      if (engine->state() == TransportState::Playing) {
        hyperDeckSnapshot_.transport = "play";
      } else if (engine->state() == TransportState::Paused) {
        hyperDeckSnapshot_.transport = "paused";
      }
    }
    const Deck& focused = focusedDeck();
    hyperDeckSnapshot_.clipId = focused.activeIndex + 1;
    hyperDeckSnapshot_.loop = focused.playlistLoop;
    hyperDeckSnapshot_.clips.clear();
    hyperDeckSnapshot_.clips.reserve(focused.cues.size());
    for (const Cue& cue : focused.cues) {
      hyperDeckSnapshot_.clips.emplace_back(cue.name, formatSeconds(cue.duration));
    }
  }

  void disarmAllOutputsForStartup() {
    normalizeProject(project_);
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      OutputTarget& output = project_.outputs[outputIndex];
      output.enabled = false;
      stopOutputStream(outputIndex);
      setOutputHealthState(outputIndex, OutputHealthState::Off);
    }
  }

  bool jumpTriggersPlayback() const {
    return normalizeJumpModeToken(project_.jumpMode) == "trigger";
  }

  void setJumpModeToken(const std::string& token) {
    std::string normalized = normalizeJumpModeToken(token);
    if (project_.jumpMode == normalized) {
      triggerToast("jump mode: " + jumpModeLabelFromToken(project_.jumpMode));
      return;
    }
    project_.jumpMode = normalized;
    triggerToast("jump mode: " + jumpModeLabelFromToken(project_.jumpMode));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleJumpMode() {
    setJumpModeToken(jumpTriggersPlayback() ? "load" : "trigger");
  }

  void setJumpTransitionEnabled(bool enabled) {
    if (project_.jumpTransitionEnabled == enabled) {
      triggerToast(std::string("jump transition: ") + (project_.jumpTransitionEnabled ? "on" : "off"));
      return;
    }
    project_.jumpTransitionEnabled = enabled;
    triggerToast(std::string("jump transition: ") + (project_.jumpTransitionEnabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void cyclePanicProfile(int direction) {
    static const std::array<std::string, 4> kProfiles {
      "outputs_off", "fade_pause", "fade_rewind", "fade_load_next"
    };
    std::string current = normalizePanicProfileToken(project_.panicProfile);
    int currentIndex = 0;
    for (int i = 0; i < static_cast<int>(kProfiles.size()); ++i) {
      if (kProfiles[i] == current) {
        currentIndex = i;
        break;
      }
    }
    int nextIndex = (currentIndex + direction + static_cast<int>(kProfiles.size())) % static_cast<int>(kProfiles.size());
    project_.panicProfile = kProfiles[nextIndex];
    triggerToast("panic profile: " + panicProfileLabelFromToken(project_.panicProfile));
    playUiSound(UiSoundEffect::Navigate);
    markProjectDirty();
  }

  void setPanicFadeSeconds(double seconds) {
    double next = std::clamp(std::isfinite(seconds) ? seconds : 0.9, 0.1, 5.0);
    if (std::abs(project_.panicFadeSeconds - next) < 0.001) {
      std::ostringstream msg;
      msg << std::fixed << std::setprecision(1) << next;
      triggerToast("panic fade " + msg.str() + "s");
      return;
    }
    project_.panicFadeSeconds = next;
    std::ostringstream msg;
    msg << std::fixed << std::setprecision(1) << project_.panicFadeSeconds;
    triggerToast("panic fade " + msg.str() + "s");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void adjustPanicFadeSeconds(double deltaSeconds) {
    setPanicFadeSeconds(project_.panicFadeSeconds + deltaSeconds);
  }

  void setPanicAutoRestoreEnabled(bool enabled) {
    if (project_.panicAutoRestore == enabled) {
      triggerToast(std::string("panic auto restore: ") + (enabled ? "on" : "off"));
      return;
    }
    project_.panicAutoRestore = enabled;
    triggerToast(std::string("panic auto restore: ") + (enabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setOscQueryEnabled(bool enabled) {
    normalizeProject(project_);
    // This used to refuse outright under _WIN32 and toast "osc query:
    // unavailable" without trying. That guard was stale: the OSC Query server
    // is the same cross-platform socket code Companion control already runs on
    // Windows (createBoundSocket / select / accept / recv / send), with only an
    // errno==EINTR check platform-guarded. Nothing about it is POSIX-only, so
    // the primary platform was being denied a feature it supports. If the bind
    // genuinely fails, startOscQueryServer below reports it the same way it
    // does everywhere else.
    if (project_.oscQueryEnabled == enabled) {
      triggerToast(std::string("osc query: ") + (enabled ? "on" : "off"));
      return;
    }
    project_.oscQueryEnabled = enabled;
    if (project_.oscQueryEnabled) {
      if (!startOscQueryServer()) {
        project_.oscQueryEnabled = false;
        triggerToast("osc query: unavailable");
        playUiSound(UiSoundEffect::Toggle);
        return;
      }
    } else {
      stopOscQueryServer();
    }
    triggerToast(std::string("osc query: ") + (project_.oscQueryEnabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setOscQueryPort(int port) {
    normalizeProject(project_);
    int normalized = normalizeOscQueryPort(port);
    if (project_.oscQueryPort == normalized) {
      triggerToast("osc query port: " + std::to_string(project_.oscQueryPort));
      return;
    }
    project_.oscQueryPort = normalized;
    if (project_.oscQueryEnabled) {
      stopOscQueryServer();
      startOscQueryServer();
    }
    triggerToast("osc query port: " + std::to_string(project_.oscQueryPort));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setOscFeedbackMirrorEnabled(bool enabled) {
    normalizeProject(project_);
    if (project_.oscFeedbackMirrorEnabled == enabled) {
      triggerToast(std::string("osc feedback mirror: ") + (enabled ? "on" : "off"));
      return;
    }
    project_.oscFeedbackMirrorEnabled = enabled;
    lastOscMirrorFeedbackPayload_.clear();
    lastOscMirrorFeedbackBroadcastMs_ = 0;
    triggerToast(std::string("osc feedback mirror: ") + (enabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setOscFeedbackRateMs(int rateMs) {
    normalizeProject(project_);
    int normalized = normalizeOscFeedbackRateMs(rateMs);
    if (project_.oscFeedbackRateMs == normalized) {
      triggerToast("osc feedback rate: " + std::to_string(project_.oscFeedbackRateMs) + " ms");
      return;
    }
    project_.oscFeedbackRateMs = normalized;
    triggerToast("osc feedback rate: " + std::to_string(project_.oscFeedbackRateMs) + " ms");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool isIntegrationBackendSupported(const std::string& backendId) const {
    auto list = integrationBackendCatalog().list();
    auto it = std::find_if(list.begin(), list.end(), [&](const auto& info) {
      return info.id == backendId;
    });
    return it != list.end() && it->supported;
  }

  void setIntegrationAdapterEnabled(const std::string& adapterToken, bool enabled) {
    normalizeProject(project_);
    bool* target = nullptr;
    std::string label;
    std::string backendId;
    std::string token = toUpper(trim(adapterToken));
    if (token == "ATEM") {
      target = &project_.atemTriggerEnabled;
      label = "atem trigger";
      backendId = "atem";
    } else if (token == "NDI" || token == "NDITRIGGER" || token == "NDI_TRIGGER") {
      target = &project_.ndiTriggerEnabled;
      label = "ndi trigger";
      backendId = "ndi-trigger";
    } else if (token == "NMC") {
      target = &project_.nmcSyncEnabled;
      label = "nmc sync";
      backendId = "nmc";
    } else if (token == "MTC") {
      target = &project_.mtcIngestEnabled;
      label = "mtc ingest";
      backendId = "mtc";
    } else if (token == "LTC") {
      target = &project_.ltcIngestEnabled;
      label = "ltc ingest";
      backendId = "ltc";
    } else if (token == "ARTNET" || token == "DMX" || token == "DMXARTNET" || token == "DMX_ARTNET") {
      target = &project_.dmxArtNetEnabled;
      label = "dmx/artnet";
      backendId = "dmx-artnet";
    }
    if (!target) {
      return;
    }
    if (*target == enabled) {
      bool supported = isIntegrationBackendSupported(backendId);
      triggerToast(label + ": " + (enabled ? "on" : "off") + (enabled && !supported ? " (stub)" : ""));
      return;
    }
    *target = enabled;
    if (backendId == "atem" && enabled && atemBridgeSocket_ == kInvalidSocket) {
      startAtemBridgeListener();
    } else if (backendId == "nmc") {
      refreshNmcSyncState();
    } else if (backendId == "ndi-trigger") {
      refreshNdiTriggerBridgeState();
    } else if (backendId == "dmx-artnet" && enabled) {
      restartArtNetBridgeListener();
    }
    bool supported = isIntegrationBackendSupported(backendId);
    triggerToast(label + ": " + (enabled ? "on" : "off") + (enabled && !supported ? " (stub)" : ""));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    if (backendId == "ltc") {
      refreshLtcCaptureState();
    }
  }

  void setAllIntegrationAdaptersEnabled(bool enabled) {
    normalizeProject(project_);
    bool changed = false;
    auto apply = [&](bool& value) {
      if (value != enabled) {
        value = enabled;
        changed = true;
      }
    };
    apply(project_.atemTriggerEnabled);
    apply(project_.ndiTriggerEnabled);
    apply(project_.nmcSyncEnabled);
    apply(project_.mtcIngestEnabled);
    apply(project_.ltcIngestEnabled);
    apply(project_.dmxArtNetEnabled);
    if (enabled && project_.atemTriggerEnabled && atemBridgeSocket_ == kInvalidSocket) {
      startAtemBridgeListener();
    }
    refreshNmcSyncState();
    refreshNdiTriggerBridgeState();
    if (enabled && project_.dmxArtNetEnabled) {
      restartArtNetBridgeListener();
    }
    triggerToast(std::string("integrations: ") + (enabled ? "on" : "off"));
    if (changed) {
      playUiSound(UiSoundEffect::Toggle);
      markProjectDirty();
    }
    refreshLtcCaptureState();
  }

  void setArtNetPort(int port) {
    normalizeProject(project_);
    int normalized = normalizeArtNetPort(port);
    if (project_.artNetPort == normalized) {
      triggerToast("artnet port: " + std::to_string(project_.artNetPort));
      return;
    }
    project_.artNetPort = normalized;
    restartArtNetBridgeListener();
    triggerToast("artnet port: " + std::to_string(project_.artNetPort));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool runPanicOutputsOff(bool requireSafetyContext, Uint32 sourceWindowId) {
    // Panic is THE event you want in a show log: it means something went wrong
    // enough that the operator hit the big button.
    showLog("PANIC", "outputs off, engines stopped");
    normalizeProject(project_);

    bool fromOutputWindow = outputIndexForWindowId(sourceWindowId).has_value();
    bool anyWindowOutputEnabled = false;
    for (const auto& output : project_.outputs) {
      if (output.enabled && normalizeOutputType(output.outputType) == "window") {
        anyWindowOutputEnabled = true;
        break;
      }
    }
    if (requireSafetyContext && !fromOutputWindow && !anyWindowOutputEnabled) {
      return false;
    }

    bool anyEnabled = false;
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      OutputTarget& output = project_.outputs[outputIndex];
      if (output.enabled) {
        anyEnabled = true;
      }
	      output.enabled = false;
	      setOutputRecoveryPausedByEscape(outputIndex, false);
	      stopOutputStream(outputIndex);
	      setOutputHealthState(outputIndex, OutputHealthState::Off);
	      applyOutputDisplaySelection(outputIndex, true);
	    }

    // Outputs-off must also silence the decks: audio does not route through
    // the video outputs, so leaving the engines playing after a panic keeps
    // sound running against a dark program — the operator hit panic to kill
    // everything AV.
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      stopBrowserCue(deckIndex);
      if (auto* engine = mediaEngineForDeck(deckIndex)) {
        engine->stop(true);
      }
    }

    panicProfilePending_ = false;
    pendingPanicProfileToken_.clear();
    panicProfileRequestedAt_ = 0;
    SDL_RaiseWindow(controlWindow_);
    triggerToast(anyEnabled ? "panic: outputs off" : "panic: outputs already off");
    playUiSound(UiSoundEffect::Panic);
    if (anyEnabled) {
      markProjectDirty();
    }
    return true;
  }

  bool emergencyDisarmOutputsFromEsc(Uint32 sourceWindowId) {
    return runPanicOutputsOff(true, sourceWindowId);
  }

  void executePanicDeckAction(const std::string& token) {
    std::string profile = normalizePanicProfileToken(token);
    int savedFocus = project_.focusedDeckIndex;
    bool changed = false;
    if (profile == "fade_pause") {
      for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
        if (const Cue* activeCue = activeCuePtr(deckIndex); activeCue && activeCue->kind == CueKind::Browser) {
          stopBrowserCue(deckIndex);
          changed = true;
        }
        if (auto* engine = mediaEngineForDeck(deckIndex)) {
          engine->pause();
          changed = true;
        }
      }
    } else if (profile == "fade_rewind") {
      for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
        stopBrowserCue(deckIndex);
        if (auto* engine = mediaEngineForDeck(deckIndex)) {
          engine->stop();
          changed = true;
        }
      }
    } else if (profile == "fade_load_next") {
      for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
        Deck& deck = project_.decks[deckIndex];
        if (deck.cues.empty()) {
          continue;
        }
        int nextIndex = deck.selectedIndex < 0
          ? 0
          : std::clamp(deck.selectedIndex + 1, 0, static_cast<int>(deck.cues.size()) - 1);
        if (deck.selectedIndex != nextIndex) {
          deck.selectedIndex = nextIndex;
          changed = true;
        }
        project_.focusedDeckIndex = deckIndex;
        takeSelected(false, false);
        changed = true;
      }
    }
    project_.focusedDeckIndex = savedFocus;
    onSelectionChanged();
    if (project_.panicAutoRestore) {
      masterDimmerTarget_ = std::clamp(panicRestoreDimmerTarget_, 0.0, 1.0);
    }
    triggerToast("panic: " + panicProfileLabelFromToken(profile));
    playUiSound(UiSoundEffect::Stop);
    if (changed) {
      markProjectDirty();
    }
  }

  void triggerPanicProfile(std::optional<std::string> overrideProfile = std::nullopt) {
    normalizeProject(project_);
    std::string profile = overrideProfile
      ? normalizePanicProfileToken(*overrideProfile)
      : normalizePanicProfileToken(project_.panicProfile);
    if (profile == "outputs_off") {
      runPanicOutputsOff(false, 0);
      return;
    }
    pendingPanicProfileToken_ = profile;
    panicProfilePending_ = true;
    panicProfileRequestedAt_ = SDL_GetTicks();
    panicRestoreDimmerTarget_ = std::clamp(masterDimmerTarget_, 0.0, 1.0);
    masterDimmerTarget_ = 0.0;
    triggerToast("panic arm: " + panicProfileLabelFromToken(profile));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool saveProjectNow(bool withToast = true) {
    normalizeProject(project_);
    if (currentProjectFile_.empty()) {
      currentProjectFile_ = defaultProjectFile();
    }
    bool ok = saveProject(currentProjectFile_, project_);
    if (ok) {
      rememberLastOpenedProjectFile(currentProjectFile_);
      projectDirty_ = false;
      if (withToast) {
        triggerToast("saved " + currentProjectLabel());
      }
    } else if (withToast) {
      triggerToast("save failed");
    }
    return ok;
  }

  bool exportProjectBundleTo(const fs::path& destinationFile, bool withToast = true) {
    normalizeProject(project_);

    fs::path bundleProjectFile = normalizeProjectPath(destinationFile);
    if (bundleProjectFile.empty()) {
      if (withToast) {
        triggerToast("bundle export failed");
      }
      return false;
    }
    if (bundleProjectFile.extension().empty()) {
      bundleProjectFile += ".deckboy";
    }

    std::error_code ec;
    if (bundleProjectFile.has_parent_path()) {
      fs::create_directories(bundleProjectFile.parent_path(), ec);
      if (ec) {
        if (withToast) {
          triggerToast("bundle folder failed");
        }
        return false;
      }
    }

    Project bundledProject = project_;
    fs::path mediaDirName = bundleProjectFile.stem().string() + "_media";
    fs::path mediaDirPath = bundleProjectFile.parent_path() / mediaDirName;
    fs::create_directories(mediaDirPath, ec);
    if (ec) {
      if (withToast) {
        triggerToast("bundle media failed");
      }
      return false;
    }

    std::unordered_map<std::string, std::string> copiedRelativeBySource;
    int copiedCount = 0;
    int reusedCount = 0;
    int missingCount = 0;

    for (auto& deck : bundledProject.decks) {
      for (auto& cue : deck.cues) {
        if (!cueUsesFilesystemMedia(cue)) {
          continue;
        }

        auto sourcePath = resolveCueFilesystemPath(cue, currentProjectFile_);
        if (!sourcePath || sourcePath->empty()) {
          continue;
        }
        if (!fs::exists(*sourcePath)) {
          ++missingCount;
          continue;
        }

        std::string sourceKey = sourcePath->string();
        auto copiedIt = copiedRelativeBySource.find(sourceKey);
        if (copiedIt != copiedRelativeBySource.end()) {
          cue.path = copiedIt->second;
          ++reusedCount;
          continue;
        }

        std::string stem = sanitizeBundleFilenameStem(sourcePath->stem().string());
        std::string extension = sourcePath->extension().string();
        if (extension.empty() && cue.kind == CueKind::Audio) {
          extension = ".wav";
        }

        fs::path relativeAssetPath;
        fs::path destinationAssetPath;
        for (int suffix = 0;; ++suffix) {
          std::string candidateName = stem;
          if (suffix > 0) {
            candidateName += "_" + std::to_string(suffix + 1);
          }
          candidateName += extension;
          relativeAssetPath = mediaDirName / candidateName;
          destinationAssetPath = bundleProjectFile.parent_path() / relativeAssetPath;

          bool collision = false;
          for (const auto& [existingSource, existingRelative] : copiedRelativeBySource) {
            if (existingRelative == relativeAssetPath.generic_string() && existingSource != sourceKey) {
              collision = true;
              break;
            }
          }
          if (!collision) {
            break;
          }
        }

        bool sameFile = false;
        if (fs::exists(destinationAssetPath, ec)) {
          std::error_code eqEc;
          sameFile = fs::equivalent(*sourcePath, destinationAssetPath, eqEc);
        }
        if (!sameFile) {
          fs::copy_file(*sourcePath, destinationAssetPath, fs::copy_options::overwrite_existing, ec);
          if (ec) {
            ++missingCount;
            continue;
          }
          ++copiedCount;
        } else {
          ++reusedCount;
        }

        std::string relativeString = relativeAssetPath.generic_string();
        copiedRelativeBySource[sourceKey] = relativeString;
        cue.path = relativeString;
      }
    }

    if (!saveProject(bundleProjectFile, bundledProject)) {
      if (withToast) {
        triggerToast("bundle save failed");
      }
      return false;
    }

    if (withToast) {
      std::ostringstream message;
      message << "bundle exported";
      if (copiedCount > 0 || reusedCount > 0 || missingCount > 0) {
        message << " " << copiedCount << " copied";
        if (reusedCount > 0) {
          message << " " << reusedCount << " reused";
        }
        if (missingCount > 0) {
          message << " " << missingCount << " missing";
        }
      }
      triggerToast(message.str());
    }
    return true;
  }

  void exportProjectBundleFromPicker() {
    showSaveFileDialog(deckboyProjectFilters(),
                       [this](std::vector<std::string> files) {
                         if (files.empty()) {
                           return;
                         }
                         fs::path chosen = normalizeProjectPath(fs::path(files[0]));
                         if (chosen.extension() != ".deckboy") {
                           chosen += ".deckboy";
                         }
                         exportProjectBundleTo(chosen, true);
                       });
  }

  // Missing-media scan: flags file-backed cues whose media can't be found on
  // disk right now. Runtime-only state — never serialized. One fs::exists per
  // file cue, so it's cheap enough to re-run after load, import, and relink
  // rather than cached (network paths only pay the cost at those moments,
  // not per frame). Returns the missing count and mirrors it into
  // missingMediaCount_ for the toolbar RELINK button.
  int scanProjectMediaPresence() {
    int missing = 0;
    for (auto& deck : project_.decks) {
      for (auto& cue : deck.cues) {
        cue.mediaMissing = false;
        if (!cueUsesFilesystemMedia(cue)) {
          continue;
        }
        auto sourcePath = resolveCueFilesystemPath(cue, currentProjectFile_);
        if (!sourcePath || sourcePath->empty()) {
          continue;
        }
        std::error_code ec;
        cue.mediaMissing = !fs::exists(*sourcePath, ec);
        if (cue.mediaMissing) {
          ++missing;
        }
      }
    }
    missingMediaCount_ = missing;
    return missing;
  }

  // Async variant for boot and project open. The sync scan stats every file
  // cue — seconds on a big playlist living on USB — and used to run before
  // the first frame was ever presented, so the "black window" at boot grew
  // with the playlist. The worker resolves + stats off-thread; results are
  // applied on the update tick by cue id (cues may move or vanish meanwhile).
  void startMediaPresenceScanAsync(bool announceMissing) {
    struct Item { std::string id; std::string path; CueKind kind; };
    std::vector<Item> items;
    for (const auto& deck : project_.decks) {
      for (const auto& cue : deck.cues) {
        if (cueUsesFilesystemMedia(cue)) {
          items.push_back({cue.id, cue.path, cue.kind});
        }
      }
    }
    std::uint64_t gen = mediaScanGeneration_.fetch_add(1) + 1;
    if (mediaScanThread_.joinable()) {
      mediaScanThread_.join();  // superseded worker bails at its next gen check
    }
    mediaScanReady_.store(false);
    mediaScanAnnounce_ = announceMissing;
    mediaScanThread_ = std::thread(
        [this, gen, projectFile = currentProjectFile_, items = std::move(items)]() {
      std::vector<std::pair<std::string, bool>> results;
      results.reserve(items.size());
      for (const Item& item : items) {
        if (mediaScanGeneration_.load() != gen) {
          return;  // a newer scan started — this one's results are stale
        }
        Cue probe;
        probe.id = item.id;
        probe.path = item.path;
        probe.kind = item.kind;
        auto resolved = resolveCueFilesystemPath(probe, projectFile);
        bool missing = false;
        if (resolved && !resolved->empty()) {
          std::error_code ec;
          missing = !fs::exists(*resolved, ec);
        }
        results.emplace_back(item.id, missing);
      }
      std::lock_guard<std::mutex> lock(mediaScanMutex_);
      if (mediaScanGeneration_.load() != gen) {
        return;
      }
      mediaScanResults_ = std::move(results);
      mediaScanResultsGeneration_ = gen;
      mediaScanReady_.store(true);
    });
  }

  // Update-tick half of the async scan: fold results into the live cues.
  void pollMediaPresenceScan() {
    if (!mediaScanReady_.load()) {
      return;
    }
    std::vector<std::pair<std::string, bool>> results;
    {
      std::lock_guard<std::mutex> lock(mediaScanMutex_);
      if (mediaScanResultsGeneration_ != mediaScanGeneration_.load()) {
        mediaScanReady_.store(false);
        return;
      }
      results = std::move(mediaScanResults_);
      mediaScanResults_.clear();
    }
    mediaScanReady_.store(false);
    std::unordered_map<std::string, bool> byId;
    byId.reserve(results.size());
    for (auto& [id, missing] : results) {
      byId.emplace(std::move(id), missing);
    }
    int missingCount = 0;
    for (auto& deck : project_.decks) {
      for (auto& cue : deck.cues) {
        auto it = byId.find(cue.id);
        if (it != byId.end()) {
          cue.mediaMissing = it->second;
        }
        if (cue.mediaMissing) {
          ++missingCount;
        }
      }
    }
    missingMediaCount_ = missingCount;
    if (mediaScanAnnounce_ && missingCount > 0) {
      triggerToast(std::to_string(missingCount) + " media missing (RELINK)");
      playUiSound(UiSoundEffect::Error);
    }
  }

  // Relink: given a folder the operator picked, re-point every missing cue at
  // a file with the same name found anywhere under that folder. When several
  // candidates share the name, an exact file-size match (cue.sizeBytes from
  // the original probe) wins; otherwise the first hit is taken. Returns how
  // many cues were relinked.
  int relinkMissingMediaFromFolder(const fs::path& root) {
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) {
      triggerToast("relink: folder not found");
      return 0;
    }

    // Index the folder once: lowercase filename → full paths. Capped so a
    // mistaken pick of C:\ can't hang the app for minutes.
    constexpr int kMaxIndexEntries = 200000;
    std::unordered_map<std::string, std::vector<fs::path>> byName;
    int indexed = 0;
    auto it = fs::recursive_directory_iterator(
      root, fs::directory_options::skip_permission_denied, ec);
    auto end = fs::recursive_directory_iterator();
    for (; !ec && it != end && indexed < kMaxIndexEntries; it.increment(ec)) {
      if (!it->is_regular_file(ec)) {
        continue;
      }
      byName[toLower(it->path().filename().string())].push_back(it->path());
      ++indexed;
    }

    pushUndoSnapshot();
    int stillMissing = 0;
    int relinked = 0;
    for (auto& deck : project_.decks) {
      for (auto& cue : deck.cues) {
        if (!cue.mediaMissing) {
          continue;
        }
        std::string wantedName = fs::path(cue.path).filename().string();
        auto found = byName.find(toLower(wantedName));
        if (wantedName.empty() || found == byName.end() || found->second.empty()) {
          ++stillMissing;
          continue;
        }
        const fs::path* pick = &found->second.front();
        if (cue.sizeBytes > 0 && found->second.size() > 1) {
          for (const auto& candidate : found->second) {
            std::error_code sizeEc;
            if (fs::file_size(candidate, sizeEc) == cue.sizeBytes && !sizeEc) {
              pick = &candidate;
              break;
            }
          }
        }
        cue.path = pick->generic_string();
        cue.mediaMissing = false;
        ++relinked;
      }
    }
    missingMediaCount_ = stillMissing;

    if (relinked > 0) {
      markProjectDirty();
    }
    std::ostringstream message;
    message << "relinked " << relinked << " cue" << (relinked == 1 ? "" : "s");
    if (stillMissing > 0) {
      message << ", " << stillMissing << " still missing";
    }
    triggerToast(message.str());
    return relinked;
  }

  void relinkMediaFromPicker() {
    showFolderDialog([this](std::vector<std::string> folders) {
      if (!folders.empty()) {
        relinkMissingMediaFromFolder(fs::path(folders[0]));
      }
    });
  }

  bool startNewShow(bool withToast = true) {
    resetTransientPreviewState();
    project_ = Project {};
    // A fresh show resets to the default skin rather than carrying over the
    // colorway of whatever was loaded before.
    loadTheme("gameboy");
    project_.theme = "gameboy";
    normalizeProject(project_);
    disarmAllOutputsForStartup();
    for (auto& deck : project_.decks) {
      deck.activeIndex = -1;
    }
    currentProjectFile_ = defaultProjectFile();
    missingMediaCount_ = 0;
    timecodeTriggeredCueIds_.clear();
    cueRowDisplayCache_.clear();
    resetTimecodeFollowerState();
    selectionChangedAt_ = SDL_GetTicks();
    if (!rebuildDeckRuntimes()) {
      std::cerr << "Deck runtime creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    if (!rebuildOutputRuntimes()) {
      std::cerr << "Output runtime creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    projectDirty_ = false;
    if (withToast) {
      triggerToast("new show");
    }
    return true;
  }

  void persistProject() {
    normalizeProject(project_);
    saveProject(currentProjectFile_, project_);
    rememberLastOpenedProjectFile(currentProjectFile_);
    projectDirty_ = false;
  }

  void pushUndoSnapshot() {
    // Debounce: don't push if stack top matches current state (same cue count + selection)
    if (!undoStack_.empty()) {
      const auto& top = undoStack_.back();
      if (top.decks.size() == project_.decks.size() && !top.decks.empty() && !project_.decks.empty() &&
          top.decks[0].cues.size() == project_.decks[0].cues.size() &&
          top.decks[0].selectedIndex == project_.decks[0].selectedIndex) {
        return;
      }
    }
    undoStack_.push_back(project_);
    if (static_cast<int>(undoStack_.size()) > kMaxUndoLevels) {
      undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
  }

  void undo() {
    if (undoStack_.empty()) {
      triggerToast("nothing to undo");
      return;
    }
    resetTransientPreviewState();
    redoStack_.push_back(project_);
    project_ = undoStack_.back();
    undoStack_.pop_back();
    markProjectDirty();
    triggerToast("undo");
    playUiSound(UiSoundEffect::Navigate);
  }

  void redo() {
    if (redoStack_.empty()) {
      triggerToast("nothing to redo");
      return;
    }
    resetTransientPreviewState();
    undoStack_.push_back(project_);
    project_ = redoStack_.back();
    redoStack_.pop_back();
    markProjectDirty();
    triggerToast("redo");
    playUiSound(UiSoundEffect::Navigate);
  }

  void markProjectDirty() {
    if (!projectDirty_) {
      projectDirty_ = true;
      projectDirtyAt_ = std::chrono::steady_clock::now();
    }
    // Any project edit may have touched the active cue of some deck — refresh
    // the engines' owned cue snapshots on the next update() tick so live-
    // editable fields (fade in/out, still duration) stay current.
    engineCueSyncPending_ = true;
  }

  void flushDirtyProject() {
    if (!projectDirty_) {
      return;
    }
    // Soak mode rewires end actions to force a loop; none of that may ever
    // be persisted into the operator's show file.
    if (soakMode_) {
      return;
    }
    auto age = std::chrono::steady_clock::now() - projectDirtyAt_;
    if (age >= std::chrono::milliseconds(300)) {
      persistProject();
    }
  }

  struct VuReading {
    float rmsLeft = 0.0f;
    float rmsRight = 0.0f;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
  };

  static float linearLevelToDb(float level) {
    if (level <= 0.00001f) {
      return -60.0f;
    }
    return std::clamp(20.0f * static_cast<float>(std::log10(level)), -60.0f, 0.0f);
  }

  void clearVuMeterState(bool resetDisplay = false) {
    std::lock_guard<std::mutex> lock(vuSamplesMutex_);
    vuSamples_.clear();
    vuSamplesUpdatedAtMs_ = 0;
    if (resetDisplay) {
      vuDisplayRmsLeft_ = 0.0f;
      vuDisplayRmsRight_ = 0.0f;
      vuDisplayPeakLeft_ = 0.0f;
      vuDisplayPeakRight_ = 0.0f;
      vuDisplayUpdatedAtMs_ = SDL_GetTicks();
    }
  }

  VuReading computeVuReading() {
    std::vector<std::int16_t> samples;
    Uint64 samplesUpdatedAtMs = 0;
    {
      std::lock_guard<std::mutex> lock(vuSamplesMutex_);
      samples = vuSamples_;
      samplesUpdatedAtMs = vuSamplesUpdatedAtMs_;
    }

    VuReading target;
    Uint64 nowMs = SDL_GetTicks();
    bool samplesFresh = !samples.empty()
                     && samplesUpdatedAtMs > 0
                     && nowMs >= samplesUpdatedAtMs
                     && (nowMs - samplesUpdatedAtMs) <= 150;
    if (!samplesFresh) {
      samples.clear();
    }

    double sumLeft = 0.0;
    double sumRight = 0.0;
    size_t countLeft = 0;
    size_t countRight = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
      float normalized = std::abs(samples[i]) / 32768.0f;
      if ((i & 1u) == 0u) {
        sumLeft += static_cast<double>(normalized) * normalized;
        target.peakLeft = std::max(target.peakLeft, normalized);
        ++countLeft;
      } else {
        sumRight += static_cast<double>(normalized) * normalized;
        target.peakRight = std::max(target.peakRight, normalized);
        ++countRight;
      }
    }
    if (countLeft > 0) {
      target.rmsLeft = static_cast<float>(std::sqrt(sumLeft / static_cast<double>(countLeft)));
    }
    if (countRight > 0) {
      target.rmsRight = static_cast<float>(std::sqrt(sumRight / static_cast<double>(countRight)));
    } else {
      target.rmsRight = target.rmsLeft;
      target.peakRight = target.peakLeft;
    }

    if (vuDisplayUpdatedAtMs_ == 0 || nowMs < vuDisplayUpdatedAtMs_) {
      vuDisplayUpdatedAtMs_ = nowMs;
    }
    float deltaSeconds = static_cast<float>(std::min<Uint64>(nowMs - vuDisplayUpdatedAtMs_, 250)) / 1000.0f;
    vuDisplayUpdatedAtMs_ = nowMs;
    auto stepLevel = [&](float current, float desired, float fallPerSecond) {
      if (desired >= current) {
        return desired;
      }
      return std::max(desired, current - fallPerSecond * deltaSeconds);
    };

    vuDisplayRmsLeft_ = stepLevel(vuDisplayRmsLeft_, target.rmsLeft, 1.9f);
    vuDisplayRmsRight_ = stepLevel(vuDisplayRmsRight_, target.rmsRight, 1.9f);
    vuDisplayPeakLeft_ = stepLevel(vuDisplayPeakLeft_, target.peakLeft, 1.2f);
    vuDisplayPeakRight_ = stepLevel(vuDisplayPeakRight_, target.peakRight, 1.2f);

    VuReading reading;
    reading.rmsLeft = vuDisplayRmsLeft_;
    reading.rmsRight = vuDisplayRmsRight_;
    reading.peakLeft = vuDisplayPeakLeft_;
    reading.peakRight = vuDisplayPeakRight_;
    return reading;
  }

  void triggerWaveformAnalysis(const std::string& path) {
    if (path.empty()) return;
    std::lock_guard<std::mutex> lk(waveformMutex_);
    if (waveformCache_.count(path) || waveformFutures_.count(path)) return;
    waveformFutures_[path] = std::async(std::launch::async,
      [path]() { return computeWaveformPeaks(path); });
  }

  WaveformPeaks getWaveformPeaks(const std::string& path, bool& pending) {
    std::lock_guard<std::mutex> lk(waveformMutex_);
    auto it = waveformCache_.find(path);
    if (it != waveformCache_.end()) { pending = false; return it->second; }
    pending = waveformFutures_.count(path) > 0;
    return {};
  }

  // Linear amplitude scale for waveform display from the cue's gain trim, so
  // gain edits and R128 normalize visibly grow/shrink the drawn transients.
  float waveformGainScale(const Cue& cue) const {
    return std::pow(10.0f, std::clamp(cue.audioGainDb, kCueAudioGainMinDb, kCueAudioGainMaxDb) / 20.0f);
  }

  // Draw a waveform bar graph into dest. playFrac/inFrac/outFrac in [0,1].
  // gainScale multiplies drawn amplitudes (clamped at full scale).
  void drawWaveform(SDL_Renderer* ren, SDL_Rect dest, const WaveformPeaks& peaks,
                    bool splitChannels,
                    float playFrac, float inFrac, float outFrac,
                    const std::vector<double>& pausePoints = {}, double duration = 0.0,
                    float gainScale = 1.0f) {
    Primitives::fillRect(ren, dest, pal.deep);
    Primitives::strokeRect(ren, dest, pal.mid);
    if (peaks.empty()) {
      drawCenteredText(ren, fontSmall_, "analyzing...", pal.inkSoft, dest);
      return;
    }
    int x0 = dest.x + 2, y0 = dest.y + 2;
    int w  = dest.w - 4, h = dest.h - 4;
    int n  = static_cast<int>(std::max(peaks.left.size(), peaks.right.size()));
    if (w <= 0 || h <= 0 || n <= 0) {
      return;
    }
    // The analysis pass measures true stereo-ness sample-by-sample
    // (WaveformPeaks::distinct) and outranks cue metadata in both
    // directions: old saves predate audioChannels and claim nothing, while
    // stereo containers often carry mono content — twin identical lanes
    // would waste half the display.
    splitChannels = peaks.distinct;

    SDL_Color trackLine = pal.dark;
    SDL_Color activeOuter = pal.mid;
    SDL_Color activeInner = pal.light;
    SDL_Color dimOuter = pal.dark;
    SDL_Color dimInner = pal.mid;

    auto sampleAt = [&](const std::vector<float>& channel, int pixelIndex) {
      if (channel.empty()) {
        return 0.0f;
      }
      int n = static_cast<int>(channel.size());
      // Map pixel to a range of buckets and take the max to preserve transients
      int i0 = pixelIndex * n / std::max(1, w);
      int i1 = (pixelIndex + 1) * n / std::max(1, w);
      i0 = std::clamp(i0, 0, n - 1);
      i1 = std::clamp(i1, i0, n - 1);
      float maxVal = 0.0f;
      for (int k = i0; k <= i1; ++k) maxVal = std::max(maxVal, channel[k]);
      // Deliberately NOT clamped here — drawColumn needs to know when the
      // trimmed level exceeds full scale so it can flag it. Clamping at this
      // point is what made gain edits invisible on already-loud material:
      // peaks were pinned at 1.0 before and after, so nothing moved on screen.
      return maxVal * gainScale;
    };

    auto drawColumn = [&](int px, int topY, int baseY, float peak, bool upward, bool inRange) {
      // The lane is a dB scale, not linear amplitude, and it keeps headroom
      // ABOVE 0 dBFS.
      //
      // Linear was the reason gain edits looked broken. Real programme material
      // peaks a few dB below full scale, which on a linear scale is already at
      // the top of the lane — measured: a -3 dBFS file filled the lane by +3 dB
      // of trim and then never moved again, so the +8..+11 dB that R128
      // normalize actually applies was completely invisible. Everything above
      // about -6 dBFS looks identical on a linear lane.
      //
      // Mapping kWaveformFloorDb..kWaveformCeilingDb across the lane fixes both
      // ends: quiet material stops being a flat 1px line, and loud material has
      // somewhere left to go, so a boost visibly climbs INTO the region above
      // 0 dBFS — which is drawn hot, because that is a clip warning.
      constexpr float kWaveformFloorDb = -48.0f;
      constexpr float kWaveformCeilingDb = 12.0f;
      bool over = peak > 1.0f;  // above 0 dBFS
      float frac = 0.0f;
      if (peak > 0.0f) {
        const float db = 20.0f * std::log10(peak);
        frac = (db - kWaveformFloorDb) / (kWaveformCeilingDb - kWaveformFloorDb);
        frac = std::clamp(frac, 0.0f, 1.0f);
      }
      int amp = std::max(1, static_cast<int>(std::round(frac * std::max(2, std::abs(baseY - topY)))));
      SDL_Color outer = inRange ? activeOuter : dimOuter;
      SDL_Color inner = inRange ? activeInner : dimInner;
      if (over && inRange) {
        // Reuse the theme's existing danger colour rather than inventing a
        // palette role — every theme already defines it, so no theme file
        // needs touching and it stays legible in all 30 colourways.
        outer = pal.deleteBezel;
        inner = SDL_Color{static_cast<Uint8>(std::min(255, pal.deleteBezel.r + 60)),
                          static_cast<Uint8>(std::min(255, pal.deleteBezel.g + 40)),
                          static_cast<Uint8>(std::min(255, pal.deleteBezel.b + 40)),
                          pal.deleteBezel.a};
      }
      SDL_SetRenderDrawColor(ren, outer.r, outer.g, outer.b, outer.a);
      if (upward) {
        SDL_RenderLine(ren, px, baseY, px, std::max(topY, baseY - amp));
      } else {
        SDL_RenderLine(ren, px, baseY, px, std::min(topY, baseY + amp));
      }
      int innerAmp = std::max(1, amp - 2);
      SDL_SetRenderDrawColor(ren, inner.r, inner.g, inner.b, inner.a);
      if (upward) {
        SDL_RenderLine(ren, px, baseY, px, std::max(topY, baseY - innerAmp));
      } else {
        SDL_RenderLine(ren, px, baseY, px, std::min(topY, baseY + innerAmp));
      }
    };

    if (splitChannels) {
      int dividerY = y0 + h / 2;
      int topBase = dividerY - 2;
      int bottomBase = dividerY + 2;
      int topLimit = y0 + 2;
      int bottomLimit = y0 + h - 2;
      SDL_SetRenderDrawColor(ren, trackLine.r, trackLine.g, trackLine.b, trackLine.a);
      SDL_RenderLine(ren, x0, dividerY, x0 + w, dividerY);
      for (int i = 0; i < w; ++i) {
        float frac = static_cast<float>(i) / std::max(1, w);
        bool inRange = (frac >= inFrac && frac <= outFrac);
        float leftPeak = sampleAt(peaks.left, i);
        float rightPeak = sampleAt(peaks.right.empty() ? peaks.left : peaks.right, i);
        drawColumn(x0 + i, topLimit, topBase, leftPeak, true, inRange);
        drawColumn(x0 + i, bottomLimit, bottomBase, rightPeak, false, inRange);
      }
      drawTextSafe(ren, fontSmall_, SDL_Rect {dest.x + 6, dest.y + 2, 16, 12}, "L", pal.light);
      drawTextSafe(ren, fontSmall_, SDL_Rect {dest.x + 6, dest.y + dest.h - 14, 16, 12}, "R", pal.light);
    } else {
      // Mono view. This used to draw its columns inline instead of going
      // through drawColumn, which meant it missed the over-scale tint and —
      // once sampleAt stopped clamping — had no upper bound on bar height, so
      // a large gain trim painted straight out of the lane. Both views share
      // drawColumn now, so the clamp and the clip warning can't diverge again.
      int cy = y0 + h / 2;
      int topLimit = y0 + 2;
      int bottomLimit = y0 + h - 2;
      SDL_SetRenderDrawColor(ren, trackLine.r, trackLine.g, trackLine.b, trackLine.a);
      SDL_RenderLine(ren, x0, cy, x0 + w, cy);
      for (int i = 0; i < w; ++i) {
        float frac = static_cast<float>(i) / std::max(1, w);
        bool inRange = (frac >= inFrac && frac <= outFrac);
        float peak = std::max(sampleAt(peaks.left, i), sampleAt(peaks.right, i));
        drawColumn(x0 + i, topLimit, cy, peak, true, inRange);
        drawColumn(x0 + i, bottomLimit, cy, peak, false, inRange);
      }
    }

    // Pause point ticks (orange verticals)
    if (!pausePoints.empty() && duration > 0.0) {
      SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
      for (double pp : pausePoints) {
        float ppFrac = static_cast<float>(std::clamp(pp / duration, 0.0, 1.0));
        int px = x0 + static_cast<int>(ppFrac * w);
        SDL_SetRenderDrawColor(ren, 220, 120, 30, 200);
        SDL_RenderLine(ren, px, y0, px, y0 + h);
      }
      SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    }
    // Playhead
    if (playFrac >= 0.0f && playFrac <= 1.0f) {
      int px = x0 + static_cast<int>(playFrac * w);
      SDL_SetRenderDrawColor(ren, 200, 220, 80, 255);
      SDL_RenderLine(ren, px, y0, px, y0 + h);
    }
    // In/out markers
    auto drawMarker = [&](float frac, Uint8 r, Uint8 g, Uint8 b) {
      int mx = x0 + static_cast<int>(frac * std::max(1, w));
      SDL_SetRenderDrawColor(ren, r, g, b, 255);
      SDL_RenderLine(ren, mx, y0, mx, y0 + h);
    };
    if (inFrac > 0.0f)  drawMarker(inFrac,  80, 220, 80);
    if (outFrac < 1.0f) drawMarker(outFrac, 220, 80, 80);
  }

  void triggerToast(std::string message, SDL_Color fill = {155, 188, 15, 220}, SDL_Color ink = {15, 56, 15, 255}, Uint32 durationMs = 1200) {
    if (!project_.uiTransitionsEnabled) {
      return;
    }
    toast_.active = true;
    toast_.startedAt = SDL_GetTicks();
    toast_.durationMs = durationMs;
    toast_.message = std::move(message);
    toast_.fill = fill;
    toast_.ink = ink;
  }

  void queueUiPattern(const std::vector<std::pair<double, int>>& notes, float level = 0.13f) {
    if (!project_.uiSoundsEnabled || !uiAudioStream_ || SDL_GetTicks() < uiJingleUntilMs_) {
      return;
    }

    SDL_ClearAudioStream(uiAudioStream_);

    std::vector<std::int16_t> pcm;
    for (const auto& [frequency, milliseconds] : notes) {
      int samples = std::max(1, milliseconds * kAudioRate / 1000);
      int ramp = std::max(4, samples / 10);
      for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
        double time = static_cast<double>(sampleIndex) / static_cast<double>(kAudioRate);
        double wave = std::sin(time * frequency * 6.28318530718) >= 0.0 ? 1.0 : -1.0;
        double envelope = 1.0;
        if (sampleIndex < ramp) {
          envelope = static_cast<double>(sampleIndex) / static_cast<double>(ramp);
        } else if (sampleIndex > samples - ramp) {
          envelope = static_cast<double>(samples - sampleIndex) / static_cast<double>(ramp);
        }
        std::int16_t value = static_cast<std::int16_t>(std::lround(32767.0 * level * envelope * wave));
        pcm.push_back(value);
        pcm.push_back(value);
      }

      int gapSamples = kAudioRate / 80;
      for (int gap = 0; gap < gapSamples; ++gap) {
        pcm.push_back(0);
        pcm.push_back(0);
      }
    }

    if (!pcm.empty()) {
      SDL_PutAudioStreamData(uiAudioStream_, pcm.data(), static_cast<int>(pcm.size() * sizeof(std::int16_t)));
      deckboySetAudioPaused(uiAudioStream_, false);
    }
  }

  // ---------------------------------------------------------------------
  // DMG voice synth — a tiny Game Boy (DMG-01) style tracker for the richer
  // UI sounds and the boot jingle. Honors the original chip's constraints:
  // two pulse channels locked to the four hardware duty cycles, a 15-bit
  // LFSR noise voice, and 4-bit (16-step) volume envelopes. Channels get the
  // NR51-style stereo placement (PU1 leans right, PU2 leans left) so chords
  // shimmer a little instead of sitting mono.
  // ---------------------------------------------------------------------
  struct DmgNote {
    int channel;      // 0 = pulse 1, 1 = pulse 2, 2 = noise
    double freq;      // pulse: pitch Hz; noise: LFSR step rate Hz
    int startMs;
    int durMs;
    double duty;      // pulse only: 0.125 / 0.25 / 0.5 / 0.75
    int vol;          // 0..15, DMG envelope start volume
  };

  void queueDmgPattern(const std::vector<DmgNote>& notes, float level = 0.13f) {
    if (!project_.uiSoundsEnabled || !uiAudioStream_ || notes.empty() ||
        SDL_GetTicks() < uiJingleUntilMs_) {
      return;
    }
    SDL_ClearAudioStream(uiAudioStream_);

    int totalMs = 0;
    for (const DmgNote& n : notes) {
      totalMs = std::max(totalMs, n.startMs + n.durMs);
    }
    totalMs += 30;  // release tail
    const int totalSamples = totalMs * kAudioRate / 1000;
    std::vector<float> mixL(totalSamples, 0.0f), mixR(totalSamples, 0.0f);

    for (const DmgNote& n : notes) {
      int start = n.startMs * kAudioRate / 1000;
      int len = std::max(1, n.durMs * kAudioRate / 1000);
      float gainL = 0.85f, gainR = 0.85f;
      if (n.channel == 0) { gainL = 0.70f; gainR = 1.0f; }
      if (n.channel == 1) { gainL = 1.0f;  gainR = 0.70f; }
      double phase = 0.0;
      std::uint16_t lfsr = 0x7FFF;
      double lfsrAcc = 0.0;
      double noiseOut = 1.0;
      for (int i = 0; i < len && start + i < totalSamples; ++i) {
        double t = static_cast<double>(i) / len;
        // 4-bit envelope: quick attack, then step down through the DMG's
        // 16 volume levels — the quantized fade IS the retro character.
        double env = (i < 32) ? i / 32.0 : (1.0 - t);
        int envSteps = static_cast<int>(env * n.vol);
        double amp = static_cast<double>(envSteps) / 15.0;
        double sample;
        if (n.channel == 2) {
          lfsrAcc += n.freq / kAudioRate;
          while (lfsrAcc >= 1.0) {
            lfsrAcc -= 1.0;
            std::uint16_t bit = ((lfsr ^ (lfsr >> 1)) & 1);
            lfsr = static_cast<std::uint16_t>((lfsr >> 1) | (bit << 14));
            noiseOut = (lfsr & 1) ? 1.0 : -1.0;
          }
          sample = noiseOut;
        } else {
          phase += n.freq / kAudioRate;
          phase -= std::floor(phase);
          sample = (phase < n.duty) ? 1.0 : -1.0;
        }
        mixL[start + i] += static_cast<float>(sample * amp * gainL);
        mixR[start + i] += static_cast<float>(sample * amp * gainR);
      }
    }

    std::vector<std::int16_t> pcm(static_cast<size_t>(totalSamples) * 2u);
    for (int i = 0; i < totalSamples; ++i) {
      float l = std::clamp(mixL[i] * level, -1.0f, 1.0f);
      float r = std::clamp(mixR[i] * level, -1.0f, 1.0f);
      pcm[static_cast<size_t>(i) * 2u] = static_cast<std::int16_t>(std::lround(l * 32767.0f));
      pcm[static_cast<size_t>(i) * 2u + 1u] = static_cast<std::int16_t>(std::lround(r * 32767.0f));
    }
    SDL_PutAudioStreamData(uiAudioStream_, pcm.data(), static_cast<int>(pcm.size() * sizeof(std::int16_t)));
    deckboySetAudioPaused(uiAudioStream_, false);
  }

  // Boot jingle — Coltrane changes on a DMG: key centers falling by major
  // thirds with V7 links (B△7 → D7 → G△7 → B♭7 → E♭△7), the Giant Steps
  // harmonic engine under an original melody (1-2-3-5 digital patterns on
  // the majors, 9→♭7 sighs on the dominants). PU1 melody on the thin 25%
  // duty, PU2 roots on the fat 50%, noise swings hats. ~214 BPM, swung
  // eighth pairs of 170 + 110 ms; each chord holds two beats (560 ms).
  void playStartupJingle() {
    std::vector<DmgNote> song = {
      // PU1 melody (duty 25%)
      {0, 493.88, 0,    170, 0.25, 11},  // B4    B△7: 1-2-3-5 up
      {0, 554.37, 170,  110, 0.25, 10},  // C#5
      {0, 622.25, 280,  170, 0.25, 11},  // D#5
      {0, 739.99, 450,  110, 0.25, 10},  // F#5
      {0, 659.26, 560,  280, 0.25, 11},  // E5    D7: 9 →
      {0, 523.25, 840,  280, 0.25, 10},  // C5        ♭7 sigh
      {0, 783.99, 1120, 170, 0.25, 11},  // G5    G△7: 1-2-3-5, octave up
      {0, 880.00, 1290, 110, 0.25, 10},  // A5
      {0, 987.77, 1400, 170, 0.25, 11},  // B5
      {0, 1174.66, 1570, 110, 0.25, 11}, // D6
      {0, 1046.50, 1680, 280, 0.25, 11}, // C6    B♭7: 9 →
      {0, 830.61,  1960, 280, 0.25, 10}, // A♭5       ♭7 sigh
      // E♭△7 lands: sparkle arp up the tonic, then a held 7th
      {0, 622.25, 2240, 90,  0.25, 10},  // E♭5
      {0, 783.99, 2330, 90,  0.25, 10},  // G5
      {0, 932.33, 2420, 90,  0.25, 11},  // B♭5
      {0, 1174.66, 2510, 90, 0.25, 11},  // D6
      {0, 1244.51, 2600, 420, 0.25, 12}, // E♭6 held
      // PU2 roots walking the major-third cycle (duty 50%)
      {1, 123.47, 0,    150, 0.50, 8},   // B2
      {1, 146.83, 560,  150, 0.50, 7},   // D3
      {1, 196.00, 1120, 150, 0.50, 8},   // G3
      {1, 116.54, 1680, 150, 0.50, 7},   // B♭2
      {1, 155.56, 2240, 160, 0.50, 8},   // E♭3
      {1, 196.00, 2600, 420, 0.50, 7},   // G3 under the held E♭6
      // Noise swung hats: accent each chord, off-tick between
      {2, 12000, 0,    45, 0.5, 6},
      {2, 9000,  280,  25, 0.5, 3},
      {2, 12000, 560,  45, 0.5, 5},
      {2, 9000,  840,  25, 0.5, 3},
      {2, 12000, 1120, 45, 0.5, 6},
      {2, 9000,  1400, 25, 0.5, 3},
      {2, 12000, 1680, 45, 0.5, 5},
      {2, 9000,  1960, 25, 0.5, 3},
      {2, 12000, 2240, 45, 0.5, 6},
      {2, 14000, 2600, 130, 0.5, 7},     // little crash with the final chord
    };
    queueDmgPattern(song, 0.11f);
    // Hold ordinary bloops off until the last chord rings out.
    uiJingleUntilMs_ = SDL_GetTicks() + 3060;
  }

  void playUiSound(UiSoundEffect effect) {
    switch (effect) {
      case UiSoundEffect::Navigate:
        queueUiPattern({{880.0, 26}, {1046.5, 34}}, 0.08f);
        break;
      case UiSoundEffect::Import:
        queueUiPattern({{523.3, 34}, {659.3, 34}, {784.0, 48}}, 0.10f);
        break;
      case UiSoundEffect::Take:
        queueUiPattern({{659.3, 34}, {987.8, 34}, {1318.5, 62}}, 0.11f);
        break;
      case UiSoundEffect::Toggle:
        queueUiPattern({{784.0, 30}, {1174.7, 42}}, 0.09f);
        break;
      case UiSoundEffect::Stop:
        queueUiPattern({{659.3, 32}, {440.0, 46}}, 0.09f);
        break;
      case UiSoundEffect::Clear:
        queueUiPattern({{392.0, 34}, {293.7, 50}}, 0.09f);
        break;
      case UiSoundEffect::Delete:
        queueUiPattern({{523.3, 24}, {349.2, 58}}, 0.09f);
        break;
      case UiSoundEffect::Error:
        // Refused-action buzzer: two low fat-duty honks with a noise edge.
        queueDmgPattern({{1, 155.56, 0, 90, 0.50, 11},   // Eb3
                         {2, 6000,   0, 40, 0.50, 5},
                         {1, 146.83, 110, 150, 0.50, 11}, // D3
                         {2, 6000,   110, 40, 0.50, 4}},
                        0.11f);
        break;
      case UiSoundEffect::Panic:
        // Everything-off: a fast chromatic dive plus a noise whoosh.
        queueDmgPattern({{0, 880.00, 0,   70, 0.25, 12},  // A5
                         {0, 659.26, 70,  70, 0.25, 11},  // E5
                         {0, 493.88, 140, 70, 0.25, 10},  // B4
                         {0, 349.23, 210, 90, 0.25, 9},   // F4
                         {0, 233.08, 300, 160, 0.25, 9},  // Bb3
                         {2, 10000,  0,   360, 0.50, 6}},
                        0.12f);
        break;
      case UiSoundEffect::Shuffle:
        // Dice-roll trill: quick alternating thirds with a tick underneath.
        queueDmgPattern({{0, 783.99, 0,   45, 0.25, 9},   // G5
                         {0, 987.77, 45,  45, 0.25, 9},   // B5
                         {0, 783.99, 90,  45, 0.25, 10},  // G5
                         {0, 987.77, 135, 45, 0.25, 10},  // B5
                         {0, 1174.66, 180, 90, 0.25, 11}, // D6
                         {2, 9000,   0,   30, 0.50, 3},
                         {2, 9000,   90,  30, 0.50, 3}},
                        0.10f);
        break;
    }
  }

  void toggleUiSounds() {
    project_.uiSoundsEnabled = !project_.uiSoundsEnabled && uiAudioStream_ != nullptr;
    if (!uiAudioStream_) {
      project_.uiSoundsEnabled = false;
    }
    if (project_.uiSoundsEnabled) {
      playUiSound(UiSoundEffect::Toggle);
    }
    triggerToast(project_.uiSoundsEnabled ? "little bloops on" : "little bloops off");
    markProjectDirty();
  }

  void toggleUiTransitions() {
    bool changed = !project_.uiTransitionsEnabled;
    project_.uiTransitionsEnabled = true;
    if (changed) {
      markProjectDirty();
    }
    triggerToast("ui motion always on");
    playUiSound(UiSoundEffect::Toggle);
  }

  void setAutoAdvance(bool enabled) {
    (void) enabled;
    triggerToast("auto-advance removed: cue end follows hold");
  }

  void toggleAutoAdvance() {
    triggerToast("auto-advance removed: cue end follows hold");
  }

  void triggerParkedCueCreationToast(const std::string& cueLabel) {
    triggerToast(cueLabel + ": parked for cleanup");
  }

  void setPlaylistLoop(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.playlistLoop == enabled) {
      return;
    }
    deck.playlistLoop = enabled;
    triggerToast(deck.playlistLoop ? "playlist loop on" : "playlist loop off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void togglePlaylistLoop() {
    setPlaylistLoop(!focusedDeck().playlistLoop);
  }

  void ensureDeckOpacityTargetsSize() {
    if (deckPlaylistOpacityTargets_.size() < project_.decks.size()) {
      size_t previous = deckPlaylistOpacityTargets_.size();
      deckPlaylistOpacityTargets_.resize(project_.decks.size(), 1.0f);
      for (size_t i = previous; i < project_.decks.size(); ++i) {
        deckPlaylistOpacityTargets_[i] = std::clamp(project_.decks[i].playlistOpacity, 0.0f, 1.0f);
      }
    } else if (deckPlaylistOpacityTargets_.size() > project_.decks.size()) {
      deckPlaylistOpacityTargets_.resize(project_.decks.size(), 1.0f);
    }
  }

  float deckPlaylistOpacityTarget(int deckIndex) {
    ensureDeckOpacityTargetsSize();
    if (deckIndex < 0 || deckIndex >= static_cast<int>(deckPlaylistOpacityTargets_.size())) {
      return 1.0f;
    }
    return std::clamp(deckPlaylistOpacityTargets_[deckIndex], 0.0f, 1.0f);
  }

  void setDeckPlaylistOpacityTarget(int deckIndex, float value) {
    ensureDeckOpacityTargetsSize();
    if (deckIndex < 0 || deckIndex >= static_cast<int>(deckPlaylistOpacityTargets_.size())) {
      return;
    }
    deckPlaylistOpacityTargets_[deckIndex] = std::clamp(value, 0.0f, 1.0f);
  }

  void setDeckPlaylistOpacity(int deckIndex, float value, bool immediate = true) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    float clamped = std::clamp(value, 0.0f, 1.0f);
    if (std::fabs(deck.playlistOpacity - clamped) < 0.001f && std::fabs(deckPlaylistOpacityTarget(deckIndex) - clamped) < 0.001f) {
      int pct = static_cast<int>(std::lround(clamped * 100.0f));
      triggerToast("deck opacity: " + std::to_string(pct) + "%");
      return;
    }
    if (immediate) {
      deck.playlistOpacity = clamped;
    }
    setDeckPlaylistOpacityTarget(deckIndex, clamped);
    int pct = static_cast<int>(std::lround(clamped * 100.0f));
    triggerToast("deck opacity: " + std::to_string(pct) + "%");
    markProjectDirty();
  }

  void setFocusedDeckPlaylistOpacity(float value, bool immediate = true) {
    setDeckPlaylistOpacity(project_.focusedDeckIndex, value, immediate);
  }

  void setFocusedDeckPlaylistAutoFade(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.playlistAutoFade == enabled) {
      return;
    }
    deck.playlistAutoFade = enabled;
    triggerToast(deck.playlistAutoFade ? "auto fade: on" : "auto fade: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleFocusedDeckPlaylistAutoFade() {
    setFocusedDeckPlaylistAutoFade(!focusedDeck().playlistAutoFade);
  }

  void setFocusedDeckPlaylistFadeSeconds(double seconds) {
    Deck& deck = focusedDeckMutable();
    double clamped = std::clamp(std::isfinite(seconds) ? seconds : deck.playlistFadeSeconds, 0.05, 10.0);
    if (std::abs(deck.playlistFadeSeconds - clamped) < 0.001) {
      std::ostringstream label;
      label << std::fixed << std::setprecision(2) << deck.playlistFadeSeconds;
      triggerToast("deck fade: " + label.str() + "s");
      return;
    }
    deck.playlistFadeSeconds = clamped;
    std::ostringstream label;
    label << std::fixed << std::setprecision(2) << deck.playlistFadeSeconds;
    triggerToast("deck fade: " + label.str() + "s");
    markProjectDirty();
  }

  std::vector<int> selectedCueIndices(const Deck& deck) const {
    std::vector<int> result;
    result.reserve(deck.selectedIndices.size() + 1);
    std::unordered_set<int> seen;
    for (int index : deck.selectedIndices) {
      if (index < 0 || index >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      if (seen.insert(index).second) {
        result.push_back(index);
      }
    }
    if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
      if (seen.insert(deck.selectedIndex).second) {
        result.push_back(deck.selectedIndex);
      }
    }
    std::sort(result.begin(), result.end());
    return result;
  }

  bool cueIndexSelected(const Deck& deck, int cueIndex) const {
    if (cueIndex < 0) {
      return false;
    }
    if (deck.selectedIndex == cueIndex) {
      return true;
    }
    return std::find(deck.selectedIndices.begin(), deck.selectedIndices.end(), cueIndex) != deck.selectedIndices.end();
  }

  template <typename Fn>
  bool forEachFocusedSelectedCueMutable(Fn&& fn) {
    Deck& deck = focusedDeckMutable();
    auto indices = selectedCueIndices(deck);
    if (indices.empty()) {
      return false;
    }
    bool changed = false;
    for (int index : indices) {
      if (index < 0 || index >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      fn(deck.cues[index], index);
      changed = true;
    }
    return changed;
  }

  template <typename Pred>
  Cue* firstFocusedSelectedCueMutable(Pred&& pred) {
    Deck& deck = focusedDeckMutable();
    auto indices = selectedCueIndices(deck);
    for (int index : indices) {
      if (index < 0 || index >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      Cue& cue = deck.cues[index];
      if (pred(cue)) {
        return &cue;
      }
    }
    return nullptr;
  }

  void selectCueInDeck(int deckIndex, int cueIndex, bool extendRange, bool toggleSingle) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    if (extendRange) {
      int anchor = deck.selectedIndex >= 0 ? deck.selectedIndex : cueIndex;
      int start = std::min(anchor, cueIndex);
      int end = std::max(anchor, cueIndex);
      deck.selectedIndices.clear();
      for (int i = start; i <= end; ++i) {
        deck.selectedIndices.push_back(i);
      }
      deck.selectedIndex = cueIndex;
    } else if (toggleSingle) {
      auto it = std::find(deck.selectedIndices.begin(), deck.selectedIndices.end(), cueIndex);
      if (it == deck.selectedIndices.end()) {
        deck.selectedIndices.push_back(cueIndex);
      } else if (deck.selectedIndices.size() > 1 || deck.selectedIndex != cueIndex) {
        deck.selectedIndices.erase(it);
      }
      deck.selectedIndex = cueIndex;
    } else {
      deck.selectedIndex = cueIndex;
      deck.selectedIndices.clear();
      deck.selectedIndices.push_back(cueIndex);
    }
    onSelectionChanged();
    markProjectDirty();
  }

  void onSelectionChanged() {
    clearPendingLiveDeleteConfirmation();
    Deck& deck = focusedDeckMutable();
    if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
      if (std::find(deck.selectedIndices.begin(), deck.selectedIndices.end(), deck.selectedIndex) == deck.selectedIndices.end()) {
        deck.selectedIndices.push_back(deck.selectedIndex);
      }
    }
    deck.selectedIndices.erase(
      std::remove_if(deck.selectedIndices.begin(), deck.selectedIndices.end(), [&](int index) {
        return index < 0 || index >= static_cast<int>(deck.cues.size());
      }),
      deck.selectedIndices.end());
    std::sort(deck.selectedIndices.begin(), deck.selectedIndices.end());
    deck.selectedIndices.erase(std::unique(deck.selectedIndices.begin(), deck.selectedIndices.end()), deck.selectedIndices.end());

    selectionChangedAt_ = SDL_GetTicks();
    cueSettingsScroll_ = 0;
   cueSettingsScrollMax_ = 0;
    playUiSound(UiSoundEffect::Navigate);
    if (const Cue* cue = selectedCuePtr()) {
      if (cue->kind == CueKind::Pip) {
        Cue resolvedCue;
        if (buildResolvedPipSourceCue(deck, *cue, resolvedCue, nullptr)) {
          requestThumbnail(resolvedCue);
        } else {
          clearSelectedThumbnail();
        }
      } else {
        requestThumbnail(*cue);
      }
    } else {
      clearSelectedThumbnail();
    }
  }

  std::string cueVisualCacheKey(const Cue& cue) const {
    auto quantizeMillis = [](double seconds) {
      return static_cast<long long>(std::llround(std::max(0.0, seconds) * 1000.0));
    };
    std::ostringstream key;
    key << cue.id << '|'
        << cue.path << '|'
        << static_cast<int>(cue.kind) << '|'
        << quantizeMillis(cue.inPointSeconds) << '|'
        << quantizeMillis(cue.outPointSeconds) << '|'
        << quantizeMillis(cue.duration) << '|'
        << quantizeMillis(cue.stillDurationSeconds);
    return key.str();
  }

  std::string cuePreviewCacheKey(const Cue& cue) const {
    auto quantizeFloat = [](float value) {
      return static_cast<long long>(std::llround(static_cast<double>(value) * 1000.0));
    };
    std::ostringstream key;
    key << cueVisualCacheKey(cue) << '|'
        << (cue.chromaKeyEnabled ? 1 : 0) << '|'
        << static_cast<int>(cue.chromaKeyColor.r) << ','
        << static_cast<int>(cue.chromaKeyColor.g) << ','
        << static_cast<int>(cue.chromaKeyColor.b) << '|'
        << quantizeFloat(cue.chromaKeyTolerance) << '|'
        << quantizeFloat(cue.chromaKeySoftness) << '|'
        << quantizeFloat(cue.brightness) << '|'
        << quantizeFloat(cue.contrast) << '|'
        << quantizeFloat(cue.saturation) << '|'
        << quantizeFloat(cue.hueShift);
    return key.str();
  }

  std::string cueRuntimeCacheKey(const Cue& cue) const {
    auto quantizeMillis = [](double seconds) {
      return static_cast<long long>(std::llround(std::max(0.0, seconds) * 1000.0));
    };
    std::ostringstream key;
    key << cuePreviewCacheKey(cue) << '|'
        << quantizeMillis(cue.fadeInSeconds) << '|'
        << quantizeMillis(cue.fadeOutSeconds) << '|'
        << quantizeMillis(cue.playbackSpeed) << '|'
        << (cue.loop ? 1 : 0) << '|'
        << cue.loopCount << '|'
        << (cue.pauseAtBeginning ? 1 : 0) << '|'
        << (cue.pauseOnLastFrame ? 1 : 0) << '|'
        << static_cast<int>(cue.endAction);
    return key.str();
  }

  void clearSelectedThumbnail() {
    selectedThumbnailCueKey_.clear();
    if (thumbnailThread_.joinable()) {
      thumbnailProcess_.killProcessOnly();
      thumbnailThread_.join();
      thumbnailProcess_.stop();
    }
    {
      std::lock_guard<std::mutex> lk(thumbnailMutex_);
      pendingThumbnail_.reset();
    }
    thumbnailPending_.store(false);
    thumbnailLoading_.store(false);
    if (selectedThumbnailTex_) {
      SDL_DestroyTexture(selectedThumbnailTex_);
      selectedThumbnailTex_ = nullptr;
      selectedThumbnailTexW_ = 0;
      selectedThumbnailTexH_ = 0;
    }
  }

  void clearTimelineStrip() {
    timelineStripJobSerial_.fetch_add(1);
    timelineStripCueKey_.clear();
    timelineStripCueId_.clear();
    if (timelineStripThread_.joinable()) {
      timelineStripProcess_.killProcessOnly();
      timelineStripThread_.join();
      timelineStripProcess_.stop();
    }
    {
      std::lock_guard<std::mutex> lk(timelineStripMutex_);
      pendingTimelineStrip_.reset();
      pendingTimelineStripReadyTiles_ = 0;
    }
    timelineStripPending_.store(false);
    timelineStripLoading_.store(false);
    if (timelineStripTex_) {
      SDL_DestroyTexture(timelineStripTex_);
      timelineStripTex_ = nullptr;
      timelineStripTexW_ = 0;
      timelineStripTexH_ = 0;
    }
    timelineStripTexReadyTiles_ = 0;
  }

  void clearPreviewCueTexture() {
    if (previewCueTex_) {
      SDL_DestroyTexture(previewCueTex_);
      previewCueTex_ = nullptr;
    }
    previewCueTexW_ = 0;
    previewCueTexH_ = 0;
    previewCueTexFormat_ = 0;
    previewCueFrameIdx_ = static_cast<std::uint64_t>(-1);
  }

  void uploadSelectedThumbnailTexture(const DecodedFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
      if (selectedThumbnailTex_) {
        SDL_DestroyTexture(selectedThumbnailTex_);
        selectedThumbnailTex_ = nullptr;
        selectedThumbnailTexW_ = 0;
        selectedThumbnailTexH_ = 0;
      }
      return;
    }
    syncTexture(controlRenderer_, selectedThumbnailTex_,
                selectedThumbnailTexW_, selectedThumbnailTexH_,
                frame.width, frame.height,
                frame.pixels.data(), frame.width * 4);
  }

  void uploadTimelineStripTexture(const DecodedFrame& frame, int readyTiles = kTimelineStripThumbCount) {
    if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
      if (timelineStripTex_) {
        SDL_DestroyTexture(timelineStripTex_);
        timelineStripTex_ = nullptr;
        timelineStripTexW_ = 0;
        timelineStripTexH_ = 0;
      }
      timelineStripTexReadyTiles_ = 0;
      return;
    }
    syncTexture(controlRenderer_, timelineStripTex_,
                timelineStripTexW_, timelineStripTexH_,
                frame.width, frame.height,
                frame.pixels.data(), frame.width * 4);
    timelineStripTexReadyTiles_ = std::clamp(readyTiles, 0, kTimelineStripThumbCount);
  }

  void uploadPreviewCueTexture(const DecodedFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
      clearPreviewCueTexture();
      return;
    }
    if (!syncFrameTexture(controlRenderer_, previewCueTex_,
                          previewCueTexW_, previewCueTexH_,
                          previewCueTexFormat_, frame)) {
      clearPreviewCueTexture();
    }
  }

  void resetTransientPreviewState() {
    clearSelectedThumbnail();
    clearTimelineStrip();
    clearPipOverlayRuntimes();
    if (previewMediaEngine_) {
      previewMediaEngine_->stopAll();
    }
    clearPreviewCueTexture();
    previewCueKey_.clear();
    clearControlPreviewTexture();
  }

  // Drop the control window's program-monitor texture and every cache key that
  // decides whether an incoming frame is "new". Also called when the preview
  // source switches between the output composite tap and a decoder frame,
  // since those differ in both size and pixel format.
  void clearControlPreviewTexture() {
    if (controlPreviewTex_) {
      SDL_DestroyTexture(controlPreviewTex_);
      controlPreviewTex_ = nullptr;
    }
    controlPreviewTexW_ = 0;
    controlPreviewTexH_ = 0;
    controlPreviewTexFormat_ = 0;
    controlPreviewFrameIdx_ = static_cast<std::uint64_t>(-1);
    controlPreviewTapSerial_ = 0;
  }

  std::string pipOverlayRuntimeKey(int deckIndex, int cueIndex) const {
    return std::to_string(deckIndex) + ":" + std::to_string(cueIndex);
  }

  void clearPipOverlayRuntimes() {
    for (auto& [key, runtime] : pipOverlayRuntimes_) {
      (void) key;
      if (runtime.mediaEngine) {
        runtime.mediaEngine->stopAll();
      }
    }
    pipOverlayRuntimes_.clear();
  }

  void requestThumbnail(const Cue& cue) {
    if (cue.kind != CueKind::Video && cue.kind != CueKind::Image) {
      clearSelectedThumbnail();
      return;
    }
    std::string mediaPath = resolvedCueFilesystemPathString(cue, currentProjectFile_);
    if (trim(mediaPath).empty()) {
      clearSelectedThumbnail();
      return;
    }
    std::string cacheKey = cueVisualCacheKey(cue);
    bool sameCueVisual = selectedThumbnailCueKey_ == cacheKey;
    if (selectedThumbnailCueKey_ == cacheKey &&
        (selectedThumbnailTex_ || thumbnailLoading_.load())) {
      return;  // already loaded or loading
    }
    if (thumbnailThread_.joinable()) {
      thumbnailProcess_.killProcessOnly();
      thumbnailThread_.join();
      thumbnailProcess_.stop();
    }
    {
      std::lock_guard<std::mutex> lk(thumbnailMutex_);
      pendingThumbnail_.reset();
    }
    thumbnailPending_.store(false);
    thumbnailLoading_.store(false);
    if (!sameCueVisual && selectedThumbnailTex_) {
      SDL_DestroyTexture(selectedThumbnailTex_);
      selectedThumbnailTex_ = nullptr;
      selectedThumbnailTexW_ = 0;
      selectedThumbnailTexH_ = 0;
    }
    {
      std::lock_guard<std::mutex> lk(thumbnailMutex_);
      auto it = selectedThumbnailCache_.find(cacheKey);
      if (it != selectedThumbnailCache_.end()) {
        selectedThumbnailCacheOrder_.erase(
          std::remove(selectedThumbnailCacheOrder_.begin(), selectedThumbnailCacheOrder_.end(), cacheKey),
          selectedThumbnailCacheOrder_.end());
        selectedThumbnailCacheOrder_.push_back(cacheKey);
        selectedThumbnailCueKey_ = cacheKey;
        uploadSelectedThumbnailTexture(it->second);
        return;
      }
    }

    constexpr int kThumbW = 320;
    constexpr int kThumbH = 180;
    std::string scaleFilter =
      "scale=" + std::to_string(kThumbW) + ":" + std::to_string(kThumbH) +
      ":force_original_aspect_ratio=decrease:flags=lanczos,"
      "pad=" + std::to_string(kThumbW) + ":" + std::to_string(kThumbH) + ":(ow-iw)/2:(oh-ih)/2";

    std::vector<std::string> args = {"ffmpeg", "-hide_banner", "-loglevel", "error"};
    if (cue.kind == CueKind::Video && cue.duration > 0.5) {
      double clipStart = std::clamp(cue.inPointSeconds, 0.0, std::max(0.0, cue.duration - 0.1));
      double clipEnd = cue.outPointSeconds > clipStart + 0.05
        ? std::clamp(cue.outPointSeconds, clipStart + 0.05, cue.duration)
        : cue.duration;
      double clipDuration = std::max(0.1, clipEnd - clipStart);
      double seekPos = clipStart + std::min(clipDuration * 0.1, clipDuration - 0.05);
      args.push_back("-ss");
      args.push_back(std::to_string(std::max(0.0, seekPos)));
    }
    args.insert(args.end(), {"-i", mediaPath, "-an", "-sn", "-dn", "-frames:v", "1",
                             "-vf", scaleFilter,
                             "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"});

    if (!spawnPipeProcess(thumbnailProcess_, args)) {
      selectedThumbnailCueKey_.clear();
      return;
    }
    selectedThumbnailCueKey_ = std::move(cacheKey);
    thumbnailLoading_.store(true);
    int fd = thumbnailProcess_.readFd;
    thumbnailThread_ = std::thread([this, fd, cacheKey = selectedThumbnailCueKey_]() {
      constexpr int kThumbW = 320;
      constexpr int kThumbH = 180;
      DecodedFrame frame;
      frame.width = kThumbW;
      frame.height = kThumbH;
      frame.index = 0;
      frame.pixels.resize(static_cast<size_t>(kThumbW) * kThumbH * 4u);
      if (readExact(fd, frame.pixels.data(), frame.pixels.size())) {
        std::lock_guard<std::mutex> lk(thumbnailMutex_);
        selectedThumbnailCache_[cacheKey] = frame;
        selectedThumbnailCacheOrder_.erase(
          std::remove(selectedThumbnailCacheOrder_.begin(), selectedThumbnailCacheOrder_.end(), cacheKey),
          selectedThumbnailCacheOrder_.end());
        selectedThumbnailCacheOrder_.push_back(cacheKey);
        while (selectedThumbnailCacheOrder_.size() > kThumbnailCacheLimit) {
          const std::string staleKey = selectedThumbnailCacheOrder_.front();
          selectedThumbnailCacheOrder_.pop_front();
          selectedThumbnailCache_.erase(staleKey);
        }
        pendingThumbnail_ = std::move(frame);
        thumbnailPending_.store(true);
      }
      thumbnailLoading_.store(false);
    });
  }

  void requestTimelineStrip(const Cue& cue) {
    if (cue.kind != CueKind::Video || cue.path.empty() || cue.duration <= 0.05) {
      clearTimelineStrip();
      return;
    }
    std::string mediaPath = resolvedCueFilesystemPathString(cue, currentProjectFile_);
    if (trim(mediaPath).empty()) {
      clearTimelineStrip();
      return;
    }
    std::string cacheKey = cueVisualCacheKey(cue);
    bool sameCueVisual = timelineStripCueKey_ == cacheKey;
    Uint64 now = SDL_GetTicks();
    {
      std::lock_guard<std::mutex> lk(timelineStripMutex_);
      if (timelineStripFailedCueKey_ == cacheKey &&
          now - timelineStripFailedAtMs_ < 3000) {
        return;
      }
    }
    if (sameCueVisual && timelineStripLoading_.load()) {
      return;
    }
    std::uint64_t jobSerial = timelineStripJobSerial_.fetch_add(1) + 1;
    if (timelineStripThread_.joinable()) {
      timelineStripProcess_.killProcessOnly();
      timelineStripThread_.join();
      timelineStripProcess_.stop();
    }
    TimelineStripCacheEntry cachedEntry;
    bool hasCachedEntry = false;
    {
      std::lock_guard<std::mutex> lk(timelineStripMutex_);
      pendingTimelineStrip_.reset();
      pendingTimelineStripReadyTiles_ = 0;
      auto it = timelineStripCache_.find(cacheKey);
      if (it != timelineStripCache_.end()) {
        cachedEntry = it->second;
        hasCachedEntry = true;
        timelineStripCacheOrder_.erase(
          std::remove(timelineStripCacheOrder_.begin(), timelineStripCacheOrder_.end(), cacheKey),
          timelineStripCacheOrder_.end());
        timelineStripCacheOrder_.push_back(cacheKey);
      }
    }
    timelineStripPending_.store(false);
    timelineStripLoading_.store(false);
    bool alreadyShowingCurrentStrip = sameCueVisual && timelineStripTex_ &&
                                      timelineStripTexW_ > 0 && timelineStripTexH_ > 0;
    if (!sameCueVisual) {
      if (timelineStripTex_) {
        SDL_DestroyTexture(timelineStripTex_);
        timelineStripTex_ = nullptr;
        timelineStripTexW_ = 0;
        timelineStripTexH_ = 0;
      }
      timelineStripTexReadyTiles_ = 0;
    }

    const int stripW = kTimelineStripThumbCount * kTimelineStripThumbW +
                       (kTimelineStripThumbCount - 1) * kTimelineStripPadding;
    if (hasCachedEntry && cachedEntry.frame.width > 0 && cachedEntry.frame.height > 0) {
      timelineStripCueKey_ = cacheKey;
      timelineStripCueId_ = cue.id;
      if (!alreadyShowingCurrentStrip ||
          timelineStripTexReadyTiles_ < cachedEntry.readyTiles ||
          timelineStripTexW_ != cachedEntry.frame.width ||
          timelineStripTexH_ != cachedEntry.frame.height) {
        uploadTimelineStripTexture(cachedEntry.frame, cachedEntry.readyTiles);
      }
      if (cachedEntry.readyTiles >= kTimelineStripThumbCount) {
        return;
      }
    }

    int startTile = hasCachedEntry ? std::clamp(cachedEntry.readyTiles, 0, kTimelineStripThumbCount) : 0;
    if (startTile >= kTimelineStripThumbCount) {
      return;
    }

    double stripStart = std::clamp(cue.inPointSeconds, 0.0, std::max(0.0, cue.duration - 0.05));
    double stripEnd = cue.outPointSeconds > stripStart + 0.05
      ? std::clamp(cue.outPointSeconds, stripStart + 0.05, cue.duration)
      : cue.duration;
    double stripDuration = std::max(0.05, stripEnd - stripStart);

    DecodedFrame baseFrame;
    if (hasCachedEntry && cachedEntry.frame.width == stripW && cachedEntry.frame.height == kTimelineStripThumbH) {
      baseFrame = std::move(cachedEntry.frame);
    } else {
      baseFrame.width = stripW;
      baseFrame.height = kTimelineStripThumbH;
      baseFrame.index = 0;
      baseFrame.pixels.resize(static_cast<size_t>(baseFrame.width) * static_cast<size_t>(baseFrame.height) * 4u, 0);
    }

    timelineStripCueKey_ = cacheKey;
    timelineStripCueId_ = cue.id;
    timelineStripLoading_.store(true);
    timelineStripThread_ = std::thread([this,
                                        cacheKey,
                                        cueId = cue.id,
                                        sourcePath = mediaPath,
                                        cueDuration = cue.duration,
                                        cueFps = cue.fps,
                                        stripStart,
                                        stripEnd,
                                        stripDuration,
                                        startTile,
                                        jobSerial,
                                        frame = std::move(baseFrame)]() mutable {
      auto copyTilePixels = [&](int fromTile, int toTile) {
        if (fromTile < 0 || toTile < 0 || fromTile == toTile) {
          return;
        }
        int srcX = fromTile * (kTimelineStripThumbW + kTimelineStripPadding);
        int dstX = toTile * (kTimelineStripThumbW + kTimelineStripPadding);
        for (int y = 0; y < kTimelineStripThumbH; ++y) {
          std::uint8_t* dst = frame.pixels.data() +
            (static_cast<size_t>(y) * static_cast<size_t>(frame.width) + static_cast<size_t>(dstX)) * 4u;
          const std::uint8_t* src = frame.pixels.data() +
            (static_cast<size_t>(y) * static_cast<size_t>(frame.width) + static_cast<size_t>(srcX)) * 4u;
          std::memcpy(dst, src, static_cast<size_t>(kTimelineStripThumbW) * 4u);
        }
      };

      auto safeLastTileDecodeEnd = [&]() {
        double maxSpanEnd = std::min(stripEnd, cueDuration);
        if (maxSpanEnd <= stripStart + 0.001) {
          return stripStart;
        }
        double segmentSpan = (maxSpanEnd - stripStart) /
          static_cast<double>(std::max(1, kTimelineStripThumbCount));
        double safePad = cueFps > 1.0 ? (18.0 / cueFps) : 0.60;
        safePad = std::clamp(std::max(safePad, segmentSpan * 0.10), 0.45, 1.80);
        double safeEnd = maxSpanEnd - safePad;
        if (safeEnd <= stripStart + 0.001) {
          return stripStart + (maxSpanEnd - stripStart) * 0.5;
        }
        return safeEnd;
      };

      auto samplePositionForTile = [&](int tileIndex) {
        double maxSpanEnd = std::min(stripEnd, cueDuration);
        if (maxSpanEnd <= stripStart + 0.001) {
          return stripStart;
        }
        if (kTimelineStripThumbCount <= 1) {
          return std::clamp(stripStart + (maxSpanEnd - stripStart) * 0.5,
                            stripStart, maxSpanEnd);
        }
        // Sample each timeline segment away from the boundaries. The final tile
        // intentionally stays well clear of EOF because some clips report a
        // nominal duration slightly past the last decodable frame on disk.
        double segmentCount = static_cast<double>(kTimelineStripThumbCount);
        double segStart = stripStart + (maxSpanEnd - stripStart) *
          (static_cast<double>(tileIndex) / segmentCount);
        double segEnd = stripStart + (maxSpanEnd - stripStart) *
          (static_cast<double>(tileIndex + 1) / segmentCount);
        bool isLastTile = tileIndex >= kTimelineStripThumbCount - 1;
        if (isLastTile) {
          double framePad = cueFps > 1.0
            ? std::clamp(8.0 / cueFps, 0.08, 0.35)
            : 0.18;
          double nearEnd = safeLastTileDecodeEnd();
          double minLastPos = std::clamp(segStart + framePad, stripStart, maxSpanEnd);
          return std::clamp(nearEnd, minLastPos, maxSpanEnd);
        }
        double midpoint = segStart + (segEnd - segStart) * 0.50;
        double framePad = cueFps > 1.0
          ? std::clamp(4.0 / cueFps, 0.05, 0.22)
          : 0.10;
        double minPos = std::clamp(stripStart + framePad, stripStart, maxSpanEnd);
        double maxPos = std::clamp(maxSpanEnd - framePad, stripStart, maxSpanEnd);
        if (maxPos <= minPos + 0.001) {
          return std::clamp(midpoint, stripStart, std::max(stripStart, maxPos));
        }
        return std::clamp(midpoint, minPos, maxPos);
      };

      auto tileLooksSuspiciouslyBlack = [](const std::vector<std::uint8_t>& pixels) {
        if (pixels.size() < 4) {
          return true;
        }
        size_t sampleStride = 4u * 32u;
        size_t samples = 0;
        size_t darkSamples = 0;
        long long lumaSum = 0;
        for (size_t i = 0; i + 3 < pixels.size(); i += sampleStride) {
          int luma = (static_cast<int>(pixels[i + 0]) * 54 +
                      static_cast<int>(pixels[i + 1]) * 183 +
                      static_cast<int>(pixels[i + 2]) * 19) / 256;
          if (luma <= 24) {
            ++darkSamples;
          }
          lumaSum += luma;
          ++samples;
        }
        if (samples == 0) {
          return true;
        }
        double avgLuma = static_cast<double>(lumaSum) / static_cast<double>(samples);
        return darkSamples * 100 >= samples * 88 || avgLuma <= 18.0;
      };

      auto decodeTileAt = [&](double sampleSeconds, std::vector<std::uint8_t>& tilePixels) {
        double coarseSeekSeconds = sampleSeconds > 0.35 ? (sampleSeconds - 0.35) : 0.0;
        double fineSeekSeconds = sampleSeconds - coarseSeekSeconds;
        std::string filter =
          "scale=" + std::to_string(kTimelineStripThumbW) + ":" + std::to_string(kTimelineStripThumbH) +
          ":force_original_aspect_ratio=increase:flags=fast_bilinear,"
          "crop=" + std::to_string(kTimelineStripThumbW) + ":" + std::to_string(kTimelineStripThumbH) +
          ",format=rgba";
        std::vector<std::string> args {"ffmpeg", "-hide_banner", "-loglevel", "error"};
        if (coarseSeekSeconds > 0.001) {
          args.push_back("-ss");
          args.push_back(std::to_string(coarseSeekSeconds));
        }
        args.push_back("-i");
        args.push_back(sourcePath);
        if (fineSeekSeconds > 0.001) {
          args.push_back("-ss");
          args.push_back(std::to_string(fineSeekSeconds));
        }
        args.insert(args.end(), {
          "-an", "-sn", "-dn",
          "-frames:v", "1",
          "-vf", filter,
          "-f", "rawvideo",
          "-pix_fmt", "rgba",
          "pipe:1"
        });

        if (!spawnPipeProcess(timelineStripProcess_, args)) {
          return false;
        }

        size_t totalRead = 0;
        while (totalRead < tilePixels.size()) {
#ifdef _WIN32
          unsigned int chunk = static_cast<unsigned int>(tilePixels.size() - totalRead < 65536u ? tilePixels.size() - totalRead : 65536u);
          int bytes = _read(timelineStripProcess_.readFd, tilePixels.data() + totalRead, chunk);
#else
          int bytes = static_cast<int>(read(timelineStripProcess_.readFd, tilePixels.data() + totalRead, tilePixels.size() - totalRead));
#endif
          if (bytes <= 0) {
            break;
          }
          totalRead += static_cast<size_t>(bytes);
        }
        bool ok = (totalRead == tilePixels.size());
        timelineStripProcess_.stop();
        return ok;
      };

      auto timelineStripJobCancelled = [&]() {
        return timelineStripJobSerial_.load() != jobSerial;
      };

      bool wroteAnyTile = startTile > 0;
      for (int tileIndex = startTile; tileIndex < kTimelineStripThumbCount; ++tileIndex) {
        if (timelineStripJobCancelled()) {
          timelineStripLoading_.store(false);
          return;
        }

        std::vector<std::uint8_t> tilePixels(
          static_cast<size_t>(kTimelineStripThumbW) * static_cast<size_t>(kTimelineStripThumbH) * 4u);
        double sampleSeconds = std::max(0.0, samplePositionForTile(tileIndex));
        bool decodedAny = false;
        bool ok = false;
        bool sawSuspiciousBlackOnly = false;
        std::vector<double> attempts {sampleSeconds};
        if (tileIndex >= kTimelineStripThumbCount - 1) {
          double safeEnd = std::min(sampleSeconds, safeLastTileDecodeEnd());
          attempts[0] = safeEnd;
          auto pushRetryAttempt = [&](double sample) {
            double clamped = std::clamp(sample, stripStart, safeEnd);
            if (std::fabs(clamped - attempts.back()) > 0.01 &&
                std::fabs(clamped - safeEnd) > 0.01) {
              attempts.push_back(clamped);
            }
          };
          double retryPadA = cueFps > 1.0 ? std::clamp(12.0 / cueFps, 0.30, 0.80) : 0.45;
          double retryPadB = cueFps > 1.0 ? std::clamp(24.0 / cueFps, 0.60, 1.60) : 0.90;
          double retryPadC = cueFps > 1.0 ? std::clamp(40.0 / cueFps, 1.00, 2.40) : 1.40;
          pushRetryAttempt(safeEnd - retryPadA);
          pushRetryAttempt(safeEnd - retryPadB);
          pushRetryAttempt(safeEnd - retryPadC);
        }

        std::vector<std::uint8_t> lastDecodedPixels;
        for (size_t attemptIndex = 0; attemptIndex < attempts.size(); ++attemptIndex) {
          if (timelineStripJobCancelled()) {
            timelineStripLoading_.store(false);
            return;
          }
          std::vector<std::uint8_t> candidatePixels(tilePixels.size());
          if (!decodeTileAt(std::max(0.0, attempts[attemptIndex]), candidatePixels)) {
            if (timelineStripJobCancelled()) {
              timelineStripLoading_.store(false);
              return;
            }
            continue;
          }
          if (timelineStripJobCancelled()) {
            timelineStripLoading_.store(false);
            return;
          }
          decodedAny = true;
          lastDecodedPixels = candidatePixels;
          bool suspiciousLastTile =
            tileIndex >= kTimelineStripThumbCount - 1 &&
            tileLooksSuspiciouslyBlack(candidatePixels);
          if (suspiciousLastTile) {
            sawSuspiciousBlackOnly = true;
            if (attemptIndex + 1 < attempts.size()) {
              continue;
            }
            if (tileIndex > 0 || wroteAnyTile) {
              break;
            }
            continue;
          } else {
            sawSuspiciousBlackOnly = false;
          }
          tilePixels = std::move(candidatePixels);
          ok = true;
          break;
        }
        if (!ok && sawSuspiciousBlackOnly && (tileIndex > 0 || wroteAnyTile)) {
          int fallbackTile = std::max(0, tileIndex - 1);
          copyTilePixels(fallbackTile, tileIndex);
          wroteAnyTile = true;
          if (timelineStripJobCancelled()) {
            timelineStripLoading_.store(false);
            return;
          }
          {
            std::lock_guard<std::mutex> lk(timelineStripMutex_);
            timelineStripCache_[cacheKey] = TimelineStripCacheEntry {frame, tileIndex + 1};
            timelineStripCacheOrder_.erase(
              std::remove(timelineStripCacheOrder_.begin(), timelineStripCacheOrder_.end(), cacheKey),
              timelineStripCacheOrder_.end());
            timelineStripCacheOrder_.push_back(cacheKey);
            while (timelineStripCacheOrder_.size() > kTimelineStripCacheLimit) {
              const std::string staleKey = timelineStripCacheOrder_.front();
              timelineStripCacheOrder_.pop_front();
              timelineStripCache_.erase(staleKey);
            }
            pendingTimelineStrip_ = frame;
            pendingTimelineStripReadyTiles_ = tileIndex + 1;
            timelineStripPending_.store(true);
            timelineStripFailedCueKey_.clear();
            timelineStripFailedAtMs_ = 0;
          }
          continue;
        }
        if (!ok && decodedAny && !lastDecodedPixels.empty()) {
          tilePixels = std::move(lastDecodedPixels);
          ok = true;
        }

        if (!ok) {
          if (timelineStripJobCancelled()) {
            timelineStripLoading_.store(false);
            return;
          }
          if (tileIndex > 0 || wroteAnyTile) {
            int fallbackTile = std::max(0, tileIndex - 1);
            copyTilePixels(fallbackTile, tileIndex);
          } else {
            break;
          }
        } else {
          int dstX = tileIndex * (kTimelineStripThumbW + kTimelineStripPadding);
          for (int y = 0; y < kTimelineStripThumbH; ++y) {
            std::uint8_t* dst = frame.pixels.data() +
              (static_cast<size_t>(y) * static_cast<size_t>(frame.width) + static_cast<size_t>(dstX)) * 4u;
            const std::uint8_t* src = tilePixels.data() +
              static_cast<size_t>(y) * static_cast<size_t>(kTimelineStripThumbW) * 4u;
            std::memcpy(dst, src, static_cast<size_t>(kTimelineStripThumbW) * 4u);
          }
        }

        wroteAnyTile = true;
        if (timelineStripJobCancelled()) {
          timelineStripLoading_.store(false);
          return;
        }
        {
          std::lock_guard<std::mutex> lk(timelineStripMutex_);
          timelineStripCache_[cacheKey] = TimelineStripCacheEntry {frame, tileIndex + 1};
          timelineStripCacheOrder_.erase(
            std::remove(timelineStripCacheOrder_.begin(), timelineStripCacheOrder_.end(), cacheKey),
            timelineStripCacheOrder_.end());
          timelineStripCacheOrder_.push_back(cacheKey);
          while (timelineStripCacheOrder_.size() > kTimelineStripCacheLimit) {
            const std::string staleKey = timelineStripCacheOrder_.front();
            timelineStripCacheOrder_.pop_front();
            timelineStripCache_.erase(staleKey);
          }
          pendingTimelineStrip_ = frame;
          pendingTimelineStripReadyTiles_ = tileIndex + 1;
          timelineStripPending_.store(true);
          timelineStripFailedCueKey_.clear();
          timelineStripFailedAtMs_ = 0;
        }
      }

      if (!wroteAnyTile && !timelineStripJobCancelled()) {
        std::lock_guard<std::mutex> lk(timelineStripMutex_);
        timelineStripFailedCueKey_ = cacheKey;
        timelineStripFailedAtMs_ = SDL_GetTicks();
      }
      timelineStripLoading_.store(false);
    });
  }

  // A command whose verb we know but whose arguments we can't act on. The
  // operator gets the toast they always got; a Companion or script caller now
  // also gets ERR with the same words instead of an OK that wasn't true.
  void failRemoteCommand(const std::string& reason) {
    remoteCommandError_ = reason;
    triggerToast(reason);
  }

  void enqueueRemoteCommand(std::string command, SocketHandle replyTo = kInvalidSocket) {
    std::lock_guard<std::mutex> lock(remoteCommandMutex_);
    remoteCommands_.push_back(PendingRemoteCommand {std::move(command), replyTo});
  }

  void enqueueRemoteCommandBatch(const std::string& payload, char separatorHint = '\n') {
    if (separatorHint == '\n') {
      for (auto& line : splitLines(payload)) {
        std::string trimmed = trim(line);
        if (!trimmed.empty()) {
          enqueueRemoteCommand(trimmed);
        }
      }
      return;
    }

    for (auto& item : splitByChar(payload, separatorHint)) {
      std::string trimmed = trim(item);
      if (!trimmed.empty()) {
        enqueueRemoteCommand(trimmed);
      }
    }
  }
