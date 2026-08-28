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
    if (outputType == "window") {
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
    return 0;
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
    // VJ MODE: two decks, A under B, and the crossfader decides how much of
    // B you see. This hook has always returned a single deck, but the
    // layering it was built for is exactly what a mixer needs -- so the mixer
    // uses it instead of adding a second path through the compositor.
    if (project_.vjModeEnabled && project_.decks.size() > 1) {
      const int deckCount = static_cast<int>(project_.decks.size());
      const int deckA = std::clamp(project_.vjDeckA, 0, deckCount - 1);
      const int deckB = std::clamp(project_.vjDeckB, 0, deckCount - 1);
      entries.emplace_back(0, deckA);
      if (deckB != deckA) {
        entries.emplace_back(1, deckB);
      }
      return entries;
    }
    // Single-deck: just deck 0 at layer 0.
    if (!project_.decks.empty()) {
      entries.emplace_back(0, 0);
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

  bool assignDeckToOutput(int deckIndex, int outputIndex, std::optional<int> /*requestedLayer*/ = std::nullopt) {
    normalizeProject(project_);
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    project_.focusedOutputIndex = outputIndex;
    project_.decks[deckIndex].outputRouteDeckIndex = std::clamp(project_.outputs[outputIndex].hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    triggerToast("assign: " + deckLabel(deckIndex) + " -> " + outputLabel(outputIndex));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool assignFocusedDeckToFocusedOutput(std::optional<int> requestedLayer = std::nullopt) {
    return assignDeckToOutput(project_.focusedDeckIndex, project_.focusedOutputIndex, requestedLayer);
  }

  std::optional<int> assignmentIndexForDeckOutput(int deckIndex, int outputIndex) const {
    // Single-deck: deck 0 is always assigned to output 0.
    if (deckIndex == 0 && outputIndex == 0 && !project_.decks.empty() && !project_.outputs.empty()) {
      return 0;
    }
    return std::nullopt;
  }

  int enabledAssignmentCountForDeck(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return 0;
    }
    return project_.outputs.empty() ? 0 : 1;
  }

  bool setDeckOutputAssignmentLayer(int /*deckIndex*/, int /*outputIndex*/, int /*layerIndex*/) {
    // Single-deck: no layer assignments to modify.
    return false;
  }

  bool unassignDeckFromOutput(int /*deckIndex*/, int /*outputIndex*/) {
    triggerToast("routing: keep at least one output");
    return false;
  }

  bool moveDeckToOutput(int deckIndex, int outputIndex, std::optional<int> requestedLayer = std::nullopt) {
    return assignDeckToOutput(deckIndex, outputIndex, requestedLayer);
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

