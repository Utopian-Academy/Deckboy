// ============================================================================
// app_accessors.ipp — State accessor methods for the App class.
//
// Provides getter/helper functions for accessing deck, cue, output, and
// project state. These are convenience wrappers that handle bounds checking,
// focused-deck resolution, and safe fallbacks for empty state.
//
// Key accessors:
//   focusedDeck() / focusedDeckMutable() — the currently selected deck
//   activeCue() / activeCuePtr()         — the cue loaded in the focused deck
//   focusedMediaEngine()                  — the MediaEngine for the focused deck
//   focusedRuntime()                      — the DeckRuntime for the focused deck
//   focusedOutput()                       — the selected output target
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  Deck& focusedDeckMutable() {
    normalizeProject(project_);
    return project_.decks[project_.focusedDeckIndex];
  }

  // The live browser page on the focused deck, or nullptr. Everything that
  // drives a page goes through here so there is one definition of "the page
  // the operator means".
  // Is the cue currently live on this deck a browser page? Both cue kinds that
  // use the browser backend count -- a lower third is a page too.
  bool activeCueIsBrowser(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      return false;
    }
    const CueKind kind = deck.cues[deck.activeIndex].kind;
    return kind == CueKind::Browser || kind == CueKind::LowerThird;
  }

  // A browser cue is LIVE on this deck and its renderer is up. Both halves
  // matter: the cue can be selected without being taken, and the renderer can
  // be gone while the cue is still the active one.
  bool deckHasLiveBrowserCue(int deckIndex) {
    if (!activeCueIsBrowser(deckIndex)) {
      return false;
    }
    const DeckRuntime* runtime = runtimeForDeck(deckIndex);
    return runtime && runtime->browserRenderer && runtime->browserRenderer->isRunning();
  }

  bool liveBrowserIsInteractive() {
    const auto* page = const_cast<App*>(this)->liveBrowserRenderer();
    return page && page->isInteractive();
  }

  deckboy::platform::browser::BrowserRenderer* liveBrowserRenderer() {
    DeckRuntime* runtime = runtimeForDeck(project_.focusedDeckIndex);
    if (!runtime || !runtime->browserRenderer) {
      return nullptr;
    }
    return runtime->browserRenderer.get();
  }

  const Deck& focusedDeck() const {
    if (project_.decks.empty()) {
      static Deck fallback;
      return fallback;
    }
    int index = std::clamp(project_.focusedDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    return project_.decks[index];
  }

  std::string focusedDeckLabel() const {
    const Deck& deck = focusedDeck();
    return deck.name.empty() ? deckDefaultName(project_.focusedDeckIndex) : deck.name;
  }


  DeckRuntime* runtimeForDeck(int deckIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(deckRuntimes_.size())) {
      return nullptr;
    }
    return &deckRuntimes_[deckIndex];
  }

  const DeckRuntime* runtimeForDeck(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(deckRuntimes_.size())) {
      return nullptr;
    }
    return &deckRuntimes_[deckIndex];
  }

  DeckRuntime* focusedRuntime() {
    return runtimeForDeck(project_.focusedDeckIndex);
  }

  const DeckRuntime* focusedRuntime() const {
    return runtimeForDeck(project_.focusedDeckIndex);
  }

  MediaEngine* focusedMediaEngine() {
    auto* runtime = focusedRuntime();
    return runtime ? runtime->mediaEngine.get() : nullptr;
  }

  const MediaEngine* focusedMediaEngine() const {
    auto* runtime = focusedRuntime();
    return runtime ? runtime->mediaEngine.get() : nullptr;
  }

  // A callable that draws a picture as a character grid, using THIS cue's
  // text-mode settings and THIS deck's engine (which owns any sprite sheet).
  //
  // Returned empty when the cue carries no text-mode effect, so the common
  // case builds no std::function and the stack sees a null callback and skips
  // the work entirely.
  //
  // The four effect parameters are folded into a copy of the cue's settings
  // rather than replacing them: everything the character grid can do stays
  // reachable in the inspector, and the four here are the ones worth having
  // on a fader.
  std::function<void(std::uint8_t*, int, int)> textModeRendererFor(
      int deckIndex, const Cue& cue) {
    bool wanted = false;
    deckboy::effects::CueEffect chosen;
    for (const auto& fx : cue.effects) {
      if (fx.kind == deckboy::effects::CueEffectKind::TextMode && !fx.bypassed) {
        wanted = true;
        chosen = fx;
        break;
      }
    }
    if (!wanted) {
      return {};
    }
    MediaEngine* engine = mediaEngineForDeck(deckIndex);
    if (!engine) {
      return {};
    }
    VideoSynthSettings vs = cue.videoSynth;
    vs.ascii = true;
    // paramA..D ride ON TOP of the cue's own settings. The neutral values a
    // freshly added effect carries (A=0.5, B=0, C=0, D=0) have to land
    // somewhere sensible, because that is what the operator sees first: 0.5
    // gives a middling grid, and zero corruption, glyph set and ink are all
    // the defaults anyone would expect.
    // ONE mapping, shared with the inspector -- see applyTextModeParams. When
    // these were written out twice the two drifted, and every control in the
    // TEXT MODE section was dead against a picture that ignored it.
    applyTextModeParams(chosen, vs);
    const std::uint64_t serial = motionDriverFrameCounter_;
    const double seconds = static_cast<double>(animationNow_) / 1000.0;
    return [engine, vs, serial, seconds](std::uint8_t* pixels, int w, int h) {
      // In place: the grid is drawn from the picture into the same buffer,
      // which the renderer supports because it reads a cell's worth of source
      // before it writes that cell.
      engine->renderTextMode(pixels, w, h, pixels, w, h, vs, serial, seconds);
    };
  }

  MediaEngine* mediaEngineForDeck(int deckIndex) {
    auto* runtime = runtimeForDeck(deckIndex);
    return runtime ? runtime->mediaEngine.get() : nullptr;
  }

  const MediaEngine* mediaEngineForDeck(int deckIndex) const {
    auto* runtime = runtimeForDeck(deckIndex);
    return runtime ? runtime->mediaEngine.get() : nullptr;
  }

  OutputRuntime* runtimeForOutput(int outputIndex) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(outputRuntimes_.size())) {
      return nullptr;
    }
    return &outputRuntimes_[outputIndex];
  }

  const OutputRuntime* runtimeForOutput(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(outputRuntimes_.size())) {
      return nullptr;
    }
    return &outputRuntimes_[outputIndex];
  }

  void setOutputRecoveryPausedByEscape(int outputIndex, bool paused) {
    if (OutputRuntime* runtime = runtimeForOutput(outputIndex); runtime) {
      runtime->recoveryPausedByEscape = paused;
      if (outputIndex >= 0 && outputIndex < static_cast<int>(project_.outputs.size()) &&
          project_.outputs[outputIndex].enabled) {
        if (paused) {
          setOutputHealthState(outputIndex, OutputHealthState::Armed, "escaped to windowed");
        } else if (runtime->healthState != OutputHealthState::Error &&
                   runtime->healthState != OutputHealthState::Recovering) {
          setOutputHealthState(outputIndex, OutputHealthState::Armed);
        }
      }
    }
  }

  const OutputTarget& focusedOutput() const {
    static OutputTarget fallback;
    if (project_.outputs.empty()) {
      return fallback;
    }
    int index = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    return project_.outputs[index];
  }

  OutputTarget& focusedOutputMutable() {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      project_.outputs.push_back(OutputTarget {});
      project_.focusedOutputIndex = 0;
    }
    int index = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    return project_.outputs[index];
  }

  std::string outputLabel(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return outputDefaultName(0);
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    return output.name.empty() ? outputDefaultName(outputIndex) : output.name;
  }

  static const char* outputHealthLabelToken(OutputHealthState state) {
    switch (state) {
      case OutputHealthState::Off: return "OFF";
      case OutputHealthState::Armed: return "ARMED";
      case OutputHealthState::Live: return "LIVE";
      case OutputHealthState::Recovering: return "RECOVERING";
      case OutputHealthState::Error: return "ERROR";
    }
    return "OFF";
  }

  void setOutputHealthState(int outputIndex, OutputHealthState state, const std::string& reason = {}) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      return;
    }
    std::string normalizedReason = trim(reason);
    bool changed = runtime->healthState != state || runtime->healthReason != normalizedReason;
    runtime->healthState = state;
    runtime->healthReason = normalizedReason;
    if (changed) {
      runtime->healthUpdatedAtMs = SDL_GetTicks();
    }
  }

  OutputHealthState outputHealthStateForDisplay(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return OutputHealthState::Off;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    if (!output.enabled) {
      return OutputHealthState::Off;
    }
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      return OutputHealthState::Error;
    }
    if (runtime->healthState == OutputHealthState::Recovering ||
        runtime->healthState == OutputHealthState::Error) {
      return runtime->healthState;
    }

    bool streamLive = false;
#ifdef _WIN32
    streamLive = output.streamEnabled && runtime->streamProcess.running();
#else
    streamLive = output.streamEnabled && runtime->streamPid > 0;
#endif
    bool ndiLive = false;
#if defined(DECKBOY_HAS_NDI_SDK)
    ndiLive = (output.ndiEnabled || output.ndiKeyEnabled) && runtime->ndiSender;
#endif

    std::string outputType = normalizeOutputType(output.outputType);
    // A presenter view is a window and is healthy on the same terms as one.
    if (outputTypeIsWindowed(outputType)) {
      if (!runtime->outputWindow) {
        return OutputHealthState::Error;
      }
      SDL_WindowFlags flags = SDL_GetWindowFlags(runtime->outputWindow);
      bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
      bool hidden = (flags & SDL_WINDOW_HIDDEN) != 0;
      bool minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
      if (fullscreen && !hidden && !minimized && !runtime->recoveryPausedByEscape) {
        return OutputHealthState::Live;
      }
      if (streamLive || ndiLive) {
        return OutputHealthState::Live;
      }
      return OutputHealthState::Armed;
    }
    if (streamLive || ndiLive) {
      return OutputHealthState::Live;
    }
    return OutputHealthState::Armed;
  }

  std::string outputHealthLabel(int outputIndex) const {
    return outputHealthLabelToken(outputHealthStateForDisplay(outputIndex));
  }

  std::string outputHealthReason(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return "";
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      return output.enabled ? "runtime unavailable" : "";
    }
    OutputHealthState state = outputHealthStateForDisplay(outputIndex);
    if (state == OutputHealthState::Error || state == OutputHealthState::Recovering) {
      if (!runtime->healthReason.empty()) {
        return runtime->healthReason;
      }
      return state == OutputHealthState::Recovering ? "recovering" : "output fault";
    }
    bool streamLive = false;
#ifdef _WIN32
    streamLive = output.streamEnabled && runtime->streamProcess.running();
#else
    streamLive = output.streamEnabled && runtime->streamPid > 0;
#endif
    bool ndiLive = false;
#if defined(DECKBOY_HAS_NDI_SDK)
    ndiLive = (output.ndiEnabled || output.ndiKeyEnabled) && runtime->ndiSender;
#endif
    if (runtime->recoveryPausedByEscape &&
        normalizeOutputType(output.outputType) == "window" &&
        !streamLive && !ndiLive) {
      return "escaped to windowed";
    }
    if (output.streamEnabled && runtime->streamStartFailed) {
      return "stream restart needed";
    }
    return "";
  }

  void recordOutputFramePresented(int outputIndex) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      return;
    }
    Uint64 now = SDL_GetTicks();
    if (runtime->fpsSampleStartedAtMs == 0) {
      runtime->fpsSampleStartedAtMs = now;
      runtime->fpsFrameCount = 0;
      runtime->fpsMeasured = 0.0;
    }
    runtime->fpsFrameCount += 1;
    Uint64 elapsedMs = now - runtime->fpsSampleStartedAtMs;
    if (elapsedMs >= 750) {
      runtime->fpsMeasured = elapsedMs > 0
        ? (static_cast<double>(runtime->fpsFrameCount) * 1000.0 / static_cast<double>(elapsedMs))
        : runtime->fpsMeasured;
      runtime->fpsFrameCount = 0;
      runtime->fpsSampleStartedAtMs = now;
    }
  }

  void resetOutputStreamFpsTelemetry(OutputRuntime& runtime) {
    runtime.streamFpsSampleStartedAtMs = 0;
    runtime.streamFpsPacketsAtSampleStart = 0;
    runtime.streamFpsMeasured = 0.0;
  }

  void recordOutputStreamFrameWritten(int outputIndex) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    if (!output.enabled || !output.streamEnabled || !runtime->streamWriter) {
      resetOutputStreamFpsTelemetry(*runtime);
      return;
    }

    std::uint64_t packetsWritten = 0;
    {
      std::lock_guard<std::mutex> lock(runtime->streamWriter->mutex);
      packetsWritten = runtime->streamWriter->packetsWritten;
    }

    Uint64 now = SDL_GetTicks();
    if (runtime->streamFpsSampleStartedAtMs == 0) {
      runtime->streamFpsSampleStartedAtMs = now;
      runtime->streamFpsPacketsAtSampleStart = packetsWritten;
      runtime->streamFpsMeasured = 0.0;
      return;
    }

    Uint64 elapsedMs = now - runtime->streamFpsSampleStartedAtMs;
    if (elapsedMs >= 750) {
      std::uint64_t packetDelta = packetsWritten >= runtime->streamFpsPacketsAtSampleStart
        ? (packetsWritten - runtime->streamFpsPacketsAtSampleStart)
        : 0;
      runtime->streamFpsMeasured = elapsedMs > 0
        ? (static_cast<double>(packetDelta) * 1000.0 / static_cast<double>(elapsedMs))
        : runtime->streamFpsMeasured;
      runtime->streamFpsPacketsAtSampleStart = packetsWritten;
      runtime->streamFpsSampleStartedAtMs = now;
    }
  }

  std::string formatTelemetryFps(double fpsValue) const {
    if (fpsValue <= 0.01) {
      return "--.-";
    }
    std::ostringstream fps;
    fps << std::fixed << std::setprecision(1) << fpsValue;
    return fps.str();
  }

  std::string outputFpsLabel(int outputIndex) const {
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    return runtime ? formatTelemetryFps(runtime->fpsMeasured) : "--.-";
  }

  std::string outputStreamFpsLabel(int outputIndex) const {
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    return runtime ? formatTelemetryFps(runtime->streamFpsMeasured) : "--.-";
  }

  std::string deckDecodeFpsLabel(int deckIndex) const {
    const MediaEngine* engine = mediaEngineForDeck(deckIndex);
    return engine ? formatTelemetryFps(engine->mediaFpsMeasured()) : "--.-";
  }

  std::string programMonitorOutputTelemetryLabel(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return "OUTPUT --.-";
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (runtime && runtime->fpsMeasured > 0.01) {
      return "OUTPUT " + outputFpsLabel(outputIndex);
    }
    if (!output.enabled) {
      return "OUTPUT OFF";
    }
    return "OUTPUT WARM";
  }

  std::string programMonitorDecodeTelemetryLabel(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "DECODE --.-";
    }
    const Cue* liveCue = activeCuePtr(deckIndex);
    const MediaEngine* engine = mediaEngineForDeck(deckIndex);
    if (engine && engine->mediaFpsMeasured() > 0.01) {
      return "DECODE " + deckDecodeFpsLabel(deckIndex);
    }
    bool decodeRelevantCue = liveCue &&
      (liveCue->kind == CueKind::Video ||
       liveCue->kind == CueKind::Browser ||
       isSourceCueKind(liveCue->kind));
    if (!decodeRelevantCue) {
      return "DECODE OFF";
    }
    return "DECODE WARM";
  }

  std::string programMonitorStreamTelemetryLabel(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return "STREAM --.-";
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (runtime && runtime->streamFpsMeasured > 0.01) {
      return "STREAM " + outputStreamFpsLabel(outputIndex);
    }
    if (!output.enabled || !output.streamEnabled) {
      return "STREAM OFF";
    }
    if (runtime && runtime->streamStartFailed) {
      return "STREAM ERR";
    }
    return "STREAM WARM";
  }

  std::string deckLabel(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return deckDefaultName(0);
    }
    const Deck& deck = project_.decks[deckIndex];
    return deck.name.empty() ? deckDefaultName(deckIndex) : deck.name;
  }

  int outputIndexById(const std::string& outputId) const {
    std::string needle = trim(outputId);
    if (needle.empty()) {
      return -1;
    }
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (project_.outputs[outputIndex].outputId == needle) {
        return outputIndex;
      }
    }
    return -1;
  }

  // Single-deck: layer index is always 0.
  int primaryLayerIndexForDeck(int /*deckIndex*/) const {
    return 0;
  }

  // Single-deck: deck 0 always maps to output 0.
  std::optional<int> primaryOutputIndexForDeck(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return std::nullopt;
    }
    if (project_.outputs.empty()) {
      return std::nullopt;
    }
    // The output this deck is shown on, which is the one hosting it. Returning
    // 0 unconditionally meant every deck reported deck 1's raster, bit depth
    // and routing label -- so even where a second deck DID have an output, the
    // status said otherwise.
    //
    // Falls back to 0 when no output claims this deck, which is what every
    // single-deck show is and keeps their behaviour identical.
    const int owned = outputIndexForHostDeck(deckIndex);
    return owned >= 0 ? owned : 0;
  }

  int resolveDeckOutputHostIndex(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return deckIndex;
    }
    auto outputIndex = primaryOutputIndexForDeck(deckIndex);
    if (!outputIndex || *outputIndex < 0 || *outputIndex >= static_cast<int>(project_.outputs.size())) {
      return std::clamp(project_.decks[deckIndex].outputRouteDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    }
    return std::clamp(project_.outputs[*outputIndex].hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
  }

  int outputIndexForHostDeck(int hostDeckIndex) const {
    for (int i = 0; i < static_cast<int>(project_.outputs.size()); ++i) {
      if (project_.outputs[i].hostDeckIndex == hostDeckIndex) {
        return i;
      }
    }
    return -1;
  }

  int ensureOutputIndexForHostDeck(int hostDeckIndex, bool* created = nullptr) {
    normalizeProject(project_);
    if (created) {
      *created = false;
    }
    int clampedHost = std::clamp(hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    int existing = outputIndexForHostDeck(clampedHost);
    if (existing >= 0) {
      return existing;
    }
    OutputTarget output;
    output.name = outputDefaultName(static_cast<int>(project_.outputs.size()));
    output.hostDeckIndex = clampedHost;
    output.displayIndex = std::max(0, project_.decks[clampedHost].outputDisplayIndex);
    output.enabled = false;
    output.outputId = makeOutputId(output, static_cast<int>(project_.outputs.size()));
    project_.outputs.push_back(output);
    if (created) {
      *created = true;
    }
    return static_cast<int>(project_.outputs.size()) - 1;
  }

  std::vector<std::pair<int, int>> layeredDeckEntriesForOutput(int outputIndex) const {
    std::vector<std::pair<int, int>> entries;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return entries;
    }
    // ── VJ MODE IS ONE OUTPUT'S CROSSFADER, NOT THE WHOLE SHOW'S ──────
    //
    // Two decks, A under B, and the crossfader decides how much of B you see.
    //
    // IT USED TO CLAIM EVERY OUTPUT. This branch tested only "is VJ mode on",
    // so with it enabled every output in the show composited the A/B pair and
    // each output's own layer stack was silently discarded -- a clean feed and
    // a feed with a bug would both become the crossfade, with nothing on
    // screen to say why. Harmless when the stack did not exist; not harmless
    // now that it does.
    //
    // So it applies to the output VJ mode is FOR, which is the programme --
    // the first window output, the one an audience is looking at. Every other
    // destination keeps the routing it was given.
    const bool vjOwnsThisOutput =
      project_.vjModeEnabled && project_.decks.size() > 1 &&
      outputIndex == std::max(0, primaryProgrammeOutputIndex(-1));
    if (vjOwnsThisOutput) {
      const int deckCount = static_cast<int>(project_.decks.size());
      const int deckA = std::clamp(project_.vjDeckA, 0, deckCount - 1);
      const int deckB = std::clamp(project_.vjDeckB, 0, deckCount - 1);
      entries.emplace_back(0, deckA);
      if (deckB != deckA) {
        entries.emplace_back(1, deckB);
      }
      return entries;
    }
    // EVERY OUTPUT SHOWS THE DECK IT IS HOSTED BY.
    //
    // This said `entries.emplace_back(0, 0)` -- deck 0, always, whatever the
    // output was. OutputTarget::hostDeckIndex existed the whole time, was
    // saved, was settable, and was overridden here, so a second deck could
    // decode and play and never appear anywhere. Measured rather than read:
    // deck 1 showing full white recorded a mean of 251, deck 2 showing the
    // same pattern with deck 1 stopped recorded 0.
    //
    // That is also what made master cues decorative for picture -- a master
    // fires cues on decks 2 and 3 that nothing composites.
    if (!project_.decks.empty()) {
      const int deckCount = static_cast<int>(project_.decks.size());
      const int host = std::clamp(project_.outputs[outputIndex].hostDeckIndex,
                                  0, deckCount - 1);
      entries.emplace_back(0, host);
      // SUPER DECKBOY: the rest of the stack, bottom first. The compositor
      // below already walks whatever this returns and draws each deck in
      // order, so the whole of "playlist 2 sits on top of playlist 1 on this
      // output" is this loop.
      int layer = 1;
      for (int extra : project_.outputs[outputIndex].layerDecks) {
        if (extra < 0 || extra >= deckCount || extra == host) {
          continue;   // normalizeProject prunes these; belt and braces
        }
        entries.emplace_back(layer++, extra);
      }
    }
    return entries;
  }

  std::string deckOutputRoutingLabel(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "out:--";
    }
    auto outputIndex = primaryOutputIndexForDeck(deckIndex);
    if (!outputIndex) {
      return "out:--";
    }
    return "out:" + std::to_string(*outputIndex + 1);
  }

  std::optional<int> parseDeckReferenceToken(const std::string& token) const {
    std::string trimmed = trim(token);
    if (trimmed.empty()) {
      return std::nullopt;
    }
    std::string upper = toUpper(trimmed);
    if (upper == "SELF" || upper == "THIS" || upper == "FOCUSED" || upper == "FOCUS") {
      return project_.focusedDeckIndex;
    }
    if (upper == "HOST" || upper == "OUTPUT") {
      return resolveDeckOutputHostIndex(project_.focusedDeckIndex);
    }
    try {
      int index = std::stoi(trimmed);
      if (index >= 1 && index <= static_cast<int>(project_.decks.size())) {
        return index - 1;
      }
    } catch (...) {
    }
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      std::string deckName = project_.decks[deckIndex].name.empty()
        ? deckDefaultName(deckIndex)
        : project_.decks[deckIndex].name;
      if (toUpper(deckName) == upper) {
        return deckIndex;
      }
    }
    return std::nullopt;
  }

  bool setFocusedDeckIndex(int deckIndex) {
    normalizeProject(project_);
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    project_.focusedDeckIndex = deckIndex;
    if (auto outputIndex = primaryOutputIndexForDeck(deckIndex); outputIndex) {
      project_.focusedOutputIndex = *outputIndex;
    }
    selectionChangedAt_ = SDL_GetTicks();
    cueSettingsScroll_ = 0;
    cueSettingsScrollMax_ = 0;
    clearCueFindState();
    triggerToast("deck: " + focusedDeckLabel());
    markProjectDirty();
    return true;
  }

  void cycleFocusedDeck(int direction) {
    normalizeProject(project_);
    if (project_.decks.empty()) {
      return;
    }
    int deckCount = static_cast<int>(project_.decks.size());
    int nextIndex = (project_.focusedDeckIndex + direction + deckCount) % deckCount;
    setFocusedDeckIndex(nextIndex);
    playUiSound(UiSoundEffect::Navigate);
  }

  bool setFocusedOutputIndex(int outputIndex) {
    normalizeProject(project_);
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    if (project_.focusedOutputIndex == outputIndex) {
      triggerToast("output: " + outputLabel(outputIndex));
      return false;
    }
    project_.focusedOutputIndex = outputIndex;
    triggerToast("output: " + outputLabel(outputIndex));
    markProjectDirty();
    return true;
  }

  void cycleFocusedOutput(int direction) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return;
    }
    int outputCount = static_cast<int>(project_.outputs.size());
    int nextIndex = (project_.focusedOutputIndex + direction + outputCount) % outputCount;
    if (setFocusedOutputIndex(nextIndex)) {
      playUiSound(UiSoundEffect::Navigate);
    }
  }

  int addOutput(int hostDeckIndex = -1, std::string outputType = "window") {
    normalizeProject(project_);
    if (project_.decks.empty()) {
      return 0;
    }
    int normalizedHost = hostDeckIndex;
    if (normalizedHost < 0 || normalizedHost >= static_cast<int>(project_.decks.size())) {
      normalizedHost = project_.focusedDeckIndex;
    }
    normalizedHost = std::clamp(normalizedHost, 0, static_cast<int>(project_.decks.size()) - 1);

    OutputTarget output;
    output.name = outputDefaultName(static_cast<int>(project_.outputs.size()));
    output.hostDeckIndex = normalizedHost;
    output.displayIndex = std::max(0, project_.decks[normalizedHost].outputDisplayIndex);
    output.enabled = false;
    output.outputType = normalizeOutputType(outputType);
    output.mirrorSourceOutputIndex = -1;
    output.streamEnabled = (output.outputType == "stream");
    output.streamProtocol = "srt";
    output.streamUrl = defaultOutputStreamUrl(output.streamProtocol, static_cast<int>(project_.outputs.size()));
    output.streamBitrateKbps = 6000;
    output.outputId = makeOutputId(output, static_cast<int>(project_.outputs.size()));
    project_.outputs.push_back(output);
    int newIndex = static_cast<int>(project_.outputs.size()) - 1;
    project_.focusedOutputIndex = newIndex;

    if (!rebuildOutputRuntimes()) {
      triggerToast("output create failed");
      return project_.focusedOutputIndex;
    }
    triggerToast("output added (off): " + outputLabel(newIndex));
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
    return newIndex;
  }

  bool removeOutput(int outputIndex) {
    normalizeProject(project_);
    int outputCount = static_cast<int>(project_.outputs.size());
    if (outputCount <= 1) {
      triggerToast("keep at least one output");
      return false;
    }
    if (outputIndex < 0 || outputIndex >= outputCount) {
      return false;
    }

    std::string removedLabel = outputLabel(outputIndex);
    project_.outputs.erase(project_.outputs.begin() + outputIndex);

    for (OutputTarget& output : project_.outputs) {
      if (output.mirrorSourceOutputIndex == outputIndex) {
        output.mirrorSourceOutputIndex = -1;
      } else if (output.mirrorSourceOutputIndex > outputIndex) {
        output.mirrorSourceOutputIndex -= 1;
      }
    }

    if (project_.focusedOutputIndex > outputIndex) {
      project_.focusedOutputIndex -= 1;
    } else if (project_.focusedOutputIndex >= static_cast<int>(project_.outputs.size())) {
      project_.focusedOutputIndex = static_cast<int>(project_.outputs.size()) - 1;
    }

    normalizeProject(project_);
    if (!rebuildOutputRuntimes()) {
      triggerToast("output remove failed");
      return false;
    }
    triggerToast("output removed: " + removedLabel);
    playUiSound(UiSoundEffect::Delete);
    markProjectDirty();
    return true;
  }

  // ── SUPER DECKBOY: DECK -> OUTPUT ROUTING ─────────────────────────────
  //
  // Every function in this block was a single-deck stub. assignDeckToOutput
  // ignored the layer it was handed and set a field nothing composited;
  // assignmentIndexForDeckOutput answered "deck 0 on output 0" and nothing
  // else; setDeckOutputAssignmentLayer returned false; unassign refused
  // outright. The output menu's ASSIGN, LAYER - / + and MOVE controls were all
  // wired to them, so all six controls did nothing at all -- which is what
  // "the UI is a mess, with controls missing" reads like from the operator's
  // side, and why there was no way to answer "how do I assign a playlist to an
  // output?" except over the socket.
  //
  // The stack is the output's, not the deck's: layer 0 IS hostDeckIndex, and
  // layerDecks holds 1..N above it.

  // Which layer this deck occupies on this output, or nothing if it is not
  // on it at all.
  std::optional<int> assignmentIndexForDeckOutput(int deckIndex, int outputIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return std::nullopt;
    }
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return std::nullopt;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    if (output.hostDeckIndex == deckIndex) {
      return 0;
    }
    for (std::size_t i = 0; i < output.layerDecks.size(); ++i) {
      if (output.layerDecks[i] == deckIndex) {
        return static_cast<int>(i) + 1;
      }
    }
    return std::nullopt;
  }

  bool assignDeckToOutput(int deckIndex, int outputIndex,
                          std::optional<int> requestedLayer = std::nullopt) {
    normalizeProject(project_);
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    project_.focusedOutputIndex = outputIndex;
    OutputTarget& output = project_.outputs[outputIndex];
    if (output.hostDeckIndex == deckIndex) {
      triggerToast(deckLabel(deckIndex) + " is already the base of " + outputLabel(outputIndex));
      return false;
    }
    if (assignmentIndexForDeckOutput(deckIndex, outputIndex)) {
      triggerToast(deckLabel(deckIndex) + " is already on " + outputLabel(outputIndex));
      return false;
    }
    // Bounded by how many playlists there ARE, which is the real limit: an
    // output cannot carry more layers than the show has decks to put on them.
    // This said kMaxDecks, which is a different quantity that happens to be a
    // safe number -- the kind of coincidence that stops being one later.
    if (static_cast<int>(output.layerDecks.size()) + 1 >=
        static_cast<int>(project_.decks.size())) {
      triggerToast("layer limit reached on " + outputLabel(outputIndex));
      return false;
    }
    // A requested layer of 0 would mean "be the base", which is a different
    // operation (setFocusedOutputHostDeck) -- so the lowest a new layer can
    // land is 1, directly above the host.
    int at = static_cast<int>(output.layerDecks.size());
    if (requestedLayer) {
      at = std::clamp(*requestedLayer - 1, 0, static_cast<int>(output.layerDecks.size()));
    }
    output.layerDecks.insert(output.layerDecks.begin() + at, deckIndex);
    triggerToast("assign: " + deckLabel(deckIndex) + " -> " + outputLabel(outputIndex) +
                 " layer " + layerLetter(at + 1));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool assignFocusedDeckToFocusedOutput(std::optional<int> requestedLayer = std::nullopt) {
    return assignDeckToOutput(project_.focusedDeckIndex, project_.focusedOutputIndex, requestedLayer);
  }

  // How many destinations this deck reaches. Used to warn about a playlist
  // that is running and going nowhere.
  int enabledAssignmentCountForDeck(int deckIndex) const {
    int count = 0;
    for (int i = 0; i < static_cast<int>(project_.outputs.size()); ++i) {
      if (assignmentIndexForDeckOutput(deckIndex, i)) {
        ++count;
      }
    }
    return count;
  }

  bool setDeckOutputAssignmentLayer(int deckIndex, int outputIndex, int layerIndex) {
    auto at = assignmentIndexForDeckOutput(deckIndex, outputIndex);
    if (!at) {
      return false;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    const int stackSize = static_cast<int>(output.layerDecks.size());
    // MOVING THE BASE, OR MOVING SOMETHING ONTO THE BASE, IS A SWAP.
    //
    // The host is layer 0 and cannot simply be reordered out of existence --
    // an output with no base deck has nothing to composite onto. So dragging
    // the base up, or a layer down to 0, exchanges the two.
    if (layerIndex <= 0) {
      if (*at == 0) {
        return false;   // already the base
      }
      const int wasHost = output.hostDeckIndex;
      output.hostDeckIndex = deckIndex;
      output.layerDecks[static_cast<std::size_t>(*at) - 1] = wasHost;
      triggerToast(deckLabel(deckIndex) + " is now the base of " + outputLabel(outputIndex));
      markProjectDirty();
      return true;
    }
    if (layerIndex > stackSize) {
      return false;
    }
    if (*at == 0) {
      const int promoted = output.layerDecks[static_cast<std::size_t>(layerIndex) - 1];
      output.layerDecks[static_cast<std::size_t>(layerIndex) - 1] = deckIndex;
      output.hostDeckIndex = promoted;
      triggerToast(deckLabel(deckIndex) + " -> layer " + layerLetter(layerIndex));
      markProjectDirty();
      return true;
    }
    if (*at == layerIndex) {
      return false;
    }
    const int deck = output.layerDecks[static_cast<std::size_t>(*at) - 1];
    output.layerDecks.erase(output.layerDecks.begin() + (*at - 1));
    output.layerDecks.insert(output.layerDecks.begin() + (layerIndex - 1), deck);
    triggerToast(deckLabel(deckIndex) + " -> layer " + layerLetter(layerIndex));
    markProjectDirty();
    return true;
  }

  bool unassignDeckFromOutput(int deckIndex, int outputIndex) {
    auto at = assignmentIndexForDeckOutput(deckIndex, outputIndex);
    if (!at) {
      return false;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    if (*at == 0) {
      // THE BASE CANNOT JUST LEAVE. If something is layered above it, that
      // becomes the base; if nothing is, the output would have no picture at
      // all, and an output that silently shows black is the fault this whole
      // feature exists to stop being possible.
      if (output.layerDecks.empty()) {
        triggerToast("an output needs a base playlist");
        return false;
      }
      output.hostDeckIndex = output.layerDecks.front();
      output.layerDecks.erase(output.layerDecks.begin());
      triggerToast(deckLabel(output.hostDeckIndex) + " is now the base of " +
                   outputLabel(outputIndex));
      markProjectDirty();
      return true;
    }
    output.layerDecks.erase(output.layerDecks.begin() + (*at - 1));
    triggerToast("off " + outputLabel(outputIndex) + ": " + deckLabel(deckIndex));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  // Take this deck off every other output and put it on this one. What the
  // output menu's "move" arrows mean.
  bool moveDeckToOutput(int deckIndex, int outputIndex,
                        std::optional<int> requestedLayer = std::nullopt) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    for (int i = 0; i < static_cast<int>(project_.outputs.size()); ++i) {
      if (i == outputIndex) {
        continue;
      }
      // Only where it can leave without stranding that output.
      if (assignmentIndexForDeckOutput(deckIndex, i)) {
        OutputTarget& other = project_.outputs[i];
        if (other.hostDeckIndex == deckIndex && other.layerDecks.empty()) {
          continue;
        }
        unassignDeckFromOutput(deckIndex, i);
      }
    }
    if (assignmentIndexForDeckOutput(deckIndex, outputIndex)) {
      project_.focusedOutputIndex = outputIndex;
      return true;
    }
    return assignDeckToOutput(deckIndex, outputIndex, requestedLayer);
  }

  // A, B, C... An output's layers are read out loud far more often than they
  // are counted, and "layer B over layer A" is how an operator says it.
  static std::string layerLetter(int layerIndex) {
    if (layerIndex < 0) {
      return "-";
    }
    if (layerIndex < 26) {
      return std::string(1, static_cast<char>('A' + layerIndex));
    }
    return std::to_string(layerIndex + 1);
  }

  bool setFocusedOutputHostDeck(int hostDeckIndex) {
    normalizeProject(project_);
    if (hostDeckIndex < 0 || hostDeckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    OutputTarget& output = focusedOutputMutable();
    int clampedHost = std::clamp(hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    if (output.hostDeckIndex == clampedHost) {
      triggerToast("host: " + deckLabel(clampedHost));
      return false;
    }
    output.hostDeckIndex = clampedHost;
    project_.decks[clampedHost].outputDisplayIndex = std::max(0, output.displayIndex);
    applyOutputDisplaySelection(project_.focusedOutputIndex);
    triggerToast("host: " + deckLabel(clampedHost));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

