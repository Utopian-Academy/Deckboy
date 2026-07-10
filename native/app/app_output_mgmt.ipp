// ============================================================================
// app_output_mgmt.ipp — Output window and stream lifecycle management.
//
// The largest .ipp file (~4000 lines). Manages the complete lifecycle of
// output destinations: SDL windows, ffmpeg streams, NDI senders, and
// DeckLink card outputs.
//
// Output window management:
//   setFocusedOutputEnabled()       — arm/disarm an output destination
//   addOutput() / removeOutput()    — add/remove output targets
//   applyOutputDisplaySelection()   — configure display/monitor assignment
//   setOutputFullscreen()           — toggle fullscreen on output windows
//
// Stream writer:
//   startStreamWriter()     — spawn ffmpeg subprocess for SRT/RTMP streaming
//   stopStreamWriter()      — shut down the stream writer thread
//   queueStreamPacket()     — push a captured frame + audio to the writer
//   streamWriterLoop()      — background thread: reads packets, pipes to ffmpeg
//
// NDI output:
//   initNdiSender()         — create NDI sender via dynamic library
//   shutdownNdiSender()     — destroy NDI sender
//   sendNdiFrame()          — send BGRA frame to NDI
//
// DeckLink output:
//   initDeckLinkOutput()    — open DeckLink device, set mode
//   shutdownDeckLinkOutput() — release DeckLink resources
//   sendDeckLinkFrame()     — convert and send frame to DeckLink card
//
// Output health state machine:
//   Off → Armed → Live → (Error → Recovering → Live)
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Enable or disable the focused output. When enabling a window output,
  // automatically enables display-follow mode and applies fullscreen.
  bool setFocusedOutputEnabled(bool enabled, bool autoFullscreenWhenEnabling = true) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return false;
    }
    int outputIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    OutputTarget& output = project_.outputs[outputIndex];
    bool windowOutput = normalizeOutputType(output.outputType) == "window";
    bool autoSwitchedToNative = false;
    if (enabled && windowOutput && !project_.outputFollowDisplay) {
      project_.outputFollowDisplay = true;
      autoSwitchedToNative = true;
    }

    if (output.enabled == enabled) {
      if (enabled) {
        setOutputHealthState(outputIndex, OutputHealthState::Recovering,
                             windowOutput ? "reasserting fullscreen" : "restarting egress");
        if (autoSwitchedToNative) {
          applyOutputDisplaySelectionAllOutputs(true, true);
        }
        setOutputRecoveryPausedByEscape(outputIndex, false);
        if (windowOutput) {
          bool recovered = recoverWindowOutputIfNeeded(outputIndex, true);
          if (!recovered) {
            bool fullscreenOk = enableOutputFullscreen(outputIndex, false);
            if (fullscreenOk) {
              setOutputHealthState(outputIndex, OutputHealthState::Live);
              triggerToast("output on");
            } else {
              setOutputHealthState(outputIndex, OutputHealthState::Error, "fullscreen unavailable");
              triggerToast("output recover failed: fullscreen unavailable");
            }
          }
          if (autoSwitchedToNative) {
            triggerToast("output recovered: auto native");
          }
        } else {
          stopOutputStream(outputIndex);
          if (auto* runtime = runtimeForOutput(outputIndex)) {
            runtime->streamStartFailed = false;
          }
          applyOutputNdiSettings(outputIndex, false);
          setOutputHealthState(outputIndex, OutputHealthState::Armed);
          triggerToast("output recovering: stream");
        }
        playUiSound(UiSoundEffect::Toggle);
        if (autoSwitchedToNative) {
          markProjectDirty();
        }
        return true;
      }
      setOutputHealthState(outputIndex, OutputHealthState::Off);
      triggerToast("output: off");
      return false;
    }

    output.enabled = enabled;
    setOutputRecoveryPausedByEscape(outputIndex, false);
    
    // Ensure runtimes exist for newly enabled outputs
    if (enabled) {
      ensureOutputRuntimesSynced();
    }

    if (!enabled) {
      stopOutputStream(outputIndex);
      if (auto* runtime = runtimeForOutput(outputIndex)) {
        runtime->delayFrames.clear();
        runtime->latestCapturedFrame = {};
      }
      setOutputHealthState(outputIndex, OutputHealthState::Off);
    } else {
      setOutputHealthState(outputIndex, OutputHealthState::Armed);
    }
    
    if (autoSwitchedToNative) {
      applyOutputDisplaySelectionAllOutputs(true, true);
    } else {
      applyOutputDisplaySelection(outputIndex, !enabled);
    }

    if (enabled && autoFullscreenWhenEnabling && windowOutput) {
      bool fullscreenOk = enableOutputFullscreen(outputIndex, false);
      if (fullscreenOk) {
        setOutputHealthState(outputIndex, OutputHealthState::Live);
        triggerToast("output on: " + currentDisplayLabel() + (autoSwitchedToNative ? "  auto native" : ""));
      } else {
        setOutputHealthState(outputIndex, OutputHealthState::Error, "fullscreen unavailable");
        triggerToast("output on (windowed): fullscreen failed");
      }
    } else {
      if (!enabled) {
        setOutputHealthState(outputIndex, OutputHealthState::Off);
      }
      triggerToast(std::string("output: ") + (enabled ? "on" : "off"));
    }

    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  void toggleFocusedOutputEnabled() {
    setFocusedOutputEnabled(!focusedOutput().enabled);
  }

  bool setFocusedOutputType(const std::string& outputType) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return false;
    }
    int outputIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    OutputTarget& output = project_.outputs[outputIndex];
    std::string current = normalizeOutputType(output.outputType);
    std::string nextType = normalizeOutputType(outputType);
    if (current == nextType) {
      triggerToast("output type: " + current);
      return false;
    }
    output.outputType = nextType;
    if (nextType == "stream") {
      output.streamEnabled = true;
    } else {
      output.mirrorSourceOutputIndex = -1;
    }
    stopOutputStream(outputIndex);
    if (rebuildOutputRuntimes()) {
      if (project_.outputs[outputIndex].enabled && nextType == "window") {
        if (enableOutputFullscreen(outputIndex, false)) {
          setOutputHealthState(outputIndex, OutputHealthState::Live);
        } else {
          setOutputHealthState(outputIndex, OutputHealthState::Error, "fullscreen unavailable");
        }
      } else if (project_.outputs[outputIndex].enabled) {
        setOutputHealthState(outputIndex, OutputHealthState::Armed);
      } else {
        setOutputHealthState(outputIndex, OutputHealthState::Off);
      }
      triggerToast("output type: " + nextType);
      playUiSound(UiSoundEffect::Toggle);
      markProjectDirty();
      return true;
    }
    setOutputHealthState(outputIndex, OutputHealthState::Error, "output type change failed");
    triggerToast("output type change failed");
    return false;
  }

  bool setFocusedOutputMirrorSource(int sourceOutputIndex) {
    normalizeProject(project_);
    int normalized = sourceOutputIndex;
    if (normalized < 0 || normalized >= static_cast<int>(project_.outputs.size()) ||
        normalized == project_.focusedOutputIndex) {
      normalized = -1;
    }
    OutputTarget& output = focusedOutputMutable();
    if (output.mirrorSourceOutputIndex == normalized) {
      std::string label = normalized >= 0 ? ("out " + std::to_string(normalized + 1)) : "off";
      triggerToast("mirror: " + label);
      return false;
    }
    output.mirrorSourceOutputIndex = normalized;
    stopOutputStream(project_.focusedOutputIndex);
    std::string label = normalized >= 0 ? ("out " + std::to_string(normalized + 1)) : "off";
    triggerToast("mirror: " + label);
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  void cycleFocusedOutputMirrorSource(int direction) {
    normalizeProject(project_);
    std::vector<int> candidates;
    candidates.push_back(-1);
    for (int i = 0; i < static_cast<int>(project_.outputs.size()); ++i) {
      if (i == project_.focusedOutputIndex) {
        continue;
      }
      candidates.push_back(i);
    }
    if (candidates.empty()) {
      return;
    }
    int current = focusedOutput().mirrorSourceOutputIndex;
    auto it = std::find(candidates.begin(), candidates.end(), current);
    int index = (it == candidates.end()) ? 0 : static_cast<int>(std::distance(candidates.begin(), it));
    int next = (index + direction + static_cast<int>(candidates.size())) % static_cast<int>(candidates.size());
    setFocusedOutputMirrorSource(candidates[next]);
  }

  bool setFocusedOutputStreamEnabled(bool enabled) {
    normalizeProject(project_);
    int outputIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    OutputTarget& output = focusedOutputMutable();
    if (output.streamEnabled == enabled) {
      triggerToast("stream: " + std::string(enabled ? "on" : "off"));
      return false;
    }
    output.streamEnabled = enabled;
    output.streamProtocol = normalizeOutputStreamProtocol(output.streamProtocol);
    if (trim(output.streamUrl).empty()) {
      output.streamUrl = defaultOutputStreamUrl(output.streamProtocol, outputIndex);
    }
    stopOutputStream(outputIndex);
    if (OutputRuntime* runtime = runtimeForOutput(outputIndex)) {
      runtime->streamStartFailed = false;
    }
    if (!output.enabled) {
      setOutputHealthState(outputIndex, OutputHealthState::Off);
    } else if (enabled) {
      setOutputHealthState(outputIndex, OutputHealthState::Armed);
    } else {
      setOutputHealthState(outputIndex, OutputHealthState::Armed);
    }
    triggerToast("stream: " + std::string(enabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  void toggleFocusedOutputStreamEnabled() {
    setFocusedOutputStreamEnabled(!focusedOutput().streamEnabled);
  }

  bool setFocusedOutputStreamProtocol(const std::string& protocol) {
    normalizeProject(project_);
    int outputIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    OutputTarget& output = focusedOutputMutable();
    std::string current = normalizeOutputStreamProtocol(output.streamProtocol);
    std::string nextProtocol = normalizeOutputStreamProtocol(protocol);
    if (nextProtocol == current) {
      triggerToast("stream protocol: " + toUpper(current));
      return false;
    }
    output.streamProtocol = nextProtocol;
    if (trim(output.streamUrl).empty() || output.streamUrl == defaultOutputStreamUrl(current, outputIndex)) {
      output.streamUrl = defaultOutputStreamUrl(nextProtocol, outputIndex);
    }
    stopOutputStream(outputIndex);
    if (OutputRuntime* runtime = runtimeForOutput(outputIndex)) {
      runtime->streamStartFailed = false;
    }
    if (output.enabled) {
      setOutputHealthState(outputIndex, OutputHealthState::Armed);
    }
    triggerToast("stream protocol: " + toUpper(nextProtocol));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool cycleFocusedOutputStreamProtocol(int direction) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    static const std::array<std::string, 2> protocols {"srt", "rtmp"};
    std::string current = normalizeOutputStreamProtocol(output.streamProtocol);
    int index = 0;
    for (int i = 0; i < static_cast<int>(protocols.size()); ++i) {
      if (protocols[i] == current) {
        index = i;
        break;
      }
    }
    int next = (index + direction + static_cast<int>(protocols.size())) % static_cast<int>(protocols.size());
    return setFocusedOutputStreamProtocol(protocols[next]);
  }

  bool setFocusedOutputStreamUrl(const std::string& streamUrl) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    std::string normalized = trim(streamUrl);
    if (normalized.empty()) {
      normalized = defaultOutputStreamUrl(output.streamProtocol, project_.focusedOutputIndex);
    }
    // Validate URL scheme — only allow known streaming protocols
    {
      std::string lower = normalized;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      bool validScheme = lower.rfind("srt://", 0) == 0
                      || lower.rfind("rtmp://", 0) == 0
                      || lower.rfind("rtmps://", 0) == 0
                      || lower.rfind("rtsp://", 0) == 0
                      || lower.rfind("udp://", 0) == 0
                      || lower.rfind("tcp://", 0) == 0
                      || lower.rfind("rtp://", 0) == 0;
      if (!validScheme) {
        triggerToast("stream url: unsupported scheme");
        return false;
      }
    }
    if (output.streamUrl == normalized) {
      triggerToast("stream url unchanged");
      return false;
    }
    output.streamUrl = normalized;
    stopOutputStream(project_.focusedOutputIndex);
    triggerToast("stream url set");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool setFocusedOutputStreamBitrateKbps(int bitrateKbps) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    int clamped = std::clamp(bitrateKbps, 500, 50000);
    if (output.streamBitrateKbps == clamped) {
      triggerToast("stream bitrate: " + std::to_string(clamped) + " kbps");
      return false;
    }
    output.streamBitrateKbps = clamped;
    stopOutputStream(project_.focusedOutputIndex);
    triggerToast("stream bitrate: " + std::to_string(clamped) + " kbps");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool setFocusedOutputAlpha(float alpha) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    float clamped = std::clamp(alpha, 0.0f, 1.0f);
    if (std::fabs(output.outputAlpha - clamped) < 0.001f) {
      int pct = static_cast<int>(std::lround(clamped * 100.0f));
      triggerToast("output alpha: " + std::to_string(pct) + "%");
      return false;
    }
    output.outputAlpha = clamped;
    int pct = static_cast<int>(std::lround(clamped * 100.0f));
    triggerToast("output alpha: " + std::to_string(pct) + "%");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool setFocusedOutputDelayMs(int delayMs) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    int clamped = std::clamp(delayMs, 0, 5000);
    if (output.outputDelayMs == clamped) {
      triggerToast("output delay: " + std::to_string(clamped) + " ms");
      return false;
    }
    output.outputDelayMs = clamped;
    if (auto* runtime = runtimeForOutput(project_.focusedOutputIndex)) {
      runtime->delayFrames.clear();
    }
    triggerToast("output delay: " + std::to_string(clamped) + " ms");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool setFocusedOutputTimeOverlayEnabled(bool enabled) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    if (output.outputTimeOverlayEnabled == enabled) {
      triggerToast(enabled ? "output overlay on" : "output overlay off");
      return false;
    }
    output.outputTimeOverlayEnabled = enabled;
    triggerToast(enabled ? "output overlay on" : "output overlay off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool toggleFocusedOutputTimeOverlayEnabled() {
    return setFocusedOutputTimeOverlayEnabled(!focusedOutput().outputTimeOverlayEnabled);
  }

  bool setFocusedOutputColorSpace(const std::string& colorSpaceToken) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    std::string next = normalizeOutputColorSpace(colorSpaceToken);
    if (output.outputColorSpace == next) {
      triggerToast("color space: " + toUpper(next));
      return false;
    }
    output.outputColorSpace = next;
    stopOutputStream(project_.focusedOutputIndex);
    triggerToast("color space: " + toUpper(next));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool cycleFocusedOutputColorSpace(int direction) {
    normalizeProject(project_);
    static const std::array<std::string, 3> kColorSpaces {"auto", "bt709", "srgb"};
    std::string current = normalizeOutputColorSpace(focusedOutput().outputColorSpace);
    int index = 0;
    for (int i = 0; i < static_cast<int>(kColorSpaces.size()); ++i) {
      if (kColorSpaces[i] == current) {
        index = i;
        break;
      }
    }
    int next = (index + direction + static_cast<int>(kColorSpaces.size())) % static_cast<int>(kColorSpaces.size());
    return setFocusedOutputColorSpace(kColorSpaces[next]);
  }

  bool setFocusedOutputLayoutMode(const std::string& modeToken) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    std::string next = normalizeOutputLayoutMode(modeToken);
    if (normalizeOutputLayoutMode(output.outputLayoutMode) == next) {
      triggerToast("layout: " + next);
      return false;
    }
    output.outputLayoutMode = next;
    triggerToast("layout: " + next);
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool cycleFocusedOutputLayoutMode(int direction) {
    normalizeProject(project_);
    static const std::array<std::string, 2> kModes {"span", "duplicate"};
    std::string current = normalizeOutputLayoutMode(focusedOutput().outputLayoutMode);
    int index = 0;
    for (int i = 0; i < static_cast<int>(kModes.size()); ++i) {
      if (kModes[i] == current) {
        index = i;
        break;
      }
    }
    int next = (index + direction + static_cast<int>(kModes.size())) % static_cast<int>(kModes.size());
    return setFocusedOutputLayoutMode(kModes[next]);
  }

  bool setFocusedOutputOrientationDegrees(int degrees) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    int normalized = normalizeOutputOrientationDegrees(degrees);
    if (normalizeOutputOrientationDegrees(output.outputOrientationDegrees) == normalized) {
      triggerToast("orientation: " + outputOrientationLabel(normalized));
      return false;
    }
    output.outputOrientationDegrees = normalized;
    if (auto* runtime = runtimeForOutput(project_.focusedOutputIndex)) {
      runtime->delayFrames.clear();
      runtime->latestCapturedFrame = {};
    }
    triggerToast("orientation: " + outputOrientationLabel(normalized));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool cycleFocusedOutputOrientation(int direction) {
    normalizeProject(project_);
    static const std::array<int, 4> kOrientations {0, 90, 180, 270};
    int current = normalizeOutputOrientationDegrees(focusedOutput().outputOrientationDegrees);
    int index = 0;
    for (int i = 0; i < static_cast<int>(kOrientations.size()); ++i) {
      if (kOrientations[i] == current) {
        index = i;
        break;
      }
    }
    int next = (index + direction + static_cast<int>(kOrientations.size())) % static_cast<int>(kOrientations.size());
    return setFocusedOutputOrientationDegrees(kOrientations[next]);
  }

  bool setFocusedOutputTestCardEnabled(bool enabled) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    if (output.outputTestCardEnabled == enabled) {
      triggerToast(std::string("test card override: ") + (enabled ? "on" : "off"));
      return false;
    }
    output.outputTestCardEnabled = enabled;
    if (auto* runtime = runtimeForOutput(project_.focusedOutputIndex)) {
      runtime->delayFrames.clear();
      runtime->latestCapturedFrame = {};
    }
    triggerToast(std::string("test card override: ") + (enabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool toggleFocusedOutputTestCardEnabled() {
    return setFocusedOutputTestCardEnabled(!focusedOutput().outputTestCardEnabled);
  }

  bool setAllOutputsTestCardEnabled(bool enabled) {
    normalizeProject(project_);
    bool changed = false;
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      OutputTarget& output = project_.outputs[outputIndex];
      if (output.outputTestCardEnabled == enabled) {
        continue;
      }
      output.outputTestCardEnabled = enabled;
      if (auto* runtime = runtimeForOutput(outputIndex)) {
        runtime->delayFrames.clear();
        runtime->latestCapturedFrame = {};
      }
      changed = true;
    }
    triggerToast(std::string("test card overrides: ") + (enabled ? "on" : "off"));
    if (changed) {
      playUiSound(UiSoundEffect::Toggle);
      markProjectDirty();
    }
    return changed;
  }

  // Operator buffer-size setting → SDL3 device buffer hint. Must be set
  // before a device open; replaces the SDL2 SDL_AudioSpec.samples field.
  void applyAudioBufferSizeHint() {
    std::string samples = std::to_string(std::clamp(project_.audioBufferSamples, 256, 2048));
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, samples.c_str());
  }

  bool ensureUiAudioDevice() {
    if (uiAudioStream_) {
      return true;
    }

    applyAudioBufferSizeHint();
    SDL_AudioSpec desired {};
    desired.freq = kAudioRate;
    desired.format = kAudioFormat;
    desired.channels = kAudioChannels;
    // Opens a logical device + bound stream in one call; starts paused.
    uiAudioStream_ = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, nullptr, nullptr);
    return uiAudioStream_ != nullptr;
  }

  // Resolve a persisted device name to a live SDL3 playback device id.
  // Returns 0 when the name is not currently present.
  SDL_AudioDeviceID audioPlaybackDeviceIdForName(const std::string& name) {
    if (name.empty()) {
      return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    }
    SDL_AudioDeviceID result = 0;
    int count = 0;
    if (SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count)) {
      for (int index = 0; index < count; ++index) {
        const char* deviceName = SDL_GetAudioDeviceName(ids[index]);
        if (deviceName && name == deviceName) {
          result = ids[index];
          break;
        }
      }
      SDL_free(ids);
    }
    return result;
  }

  SDL_AudioStream* openMainAudioDevice(const std::string& preferredDeviceName, std::string& effectiveName) {
    applyAudioBufferSizeHint();
    SDL_AudioSpec desired {};
    desired.freq = kAudioRate;
    desired.format = kAudioFormat;
    desired.channels = kAudioChannels;

    effectiveName = preferredDeviceName;
    SDL_AudioDeviceID target = audioPlaybackDeviceIdForName(preferredDeviceName);
    if (target == 0) {
      // Preferred device not present — fall back to the system default.
      effectiveName.clear();
      target = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    }

    SDL_AudioStream* mainOut = SDL_OpenAudioDeviceStream(target, &desired, nullptr, nullptr);
    if (!mainOut && target != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) {
      effectiveName.clear();
      mainOut = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, nullptr, nullptr);
    }
    // Stream starts paused; the media engine resumes it per transport state.
    return mainOut;
  }

  double defaultLtcCaptureFpsHint() const {
    if (project_.focusedDeckIndex >= 0 &&
        project_.focusedDeckIndex < static_cast<int>(project_.decks.size())) {
      double fps = project_.decks[project_.focusedDeckIndex].timecodeFps;
      if (std::isfinite(fps) && fps > 1.0) {
        return fps;
      }
    }
    return 30.0;
  }

  std::string preferredLtcCaptureDeviceName() const {
    if (const char* env = std::getenv("DECKBOY_LTC_DEVICE"); env && *env) {
      return std::string(env);
    }
    return {};
  }

  void ltcLoop(void* decoder, SDL_AudioStream* captureStream, int sampleRate, int channelCount, double fpsHint) {
    std::vector<std::int16_t> interleavedSamples(4096u * static_cast<size_t>(std::max(1, channelCount)));
    std::vector<std::int16_t> monoSamples(4096);
    std::array<std::uint8_t, LtcApi::kFrameExtBytes> frameExt {};
    std::int64_t samplePos = 0;
    LtcFpsEstimator fpsEstimator;
    fpsEstimator.estimate = std::isfinite(fpsHint) && fpsHint > 1.0 ? fpsHint : 30.0;
    double lastSentSeconds = -1.0;
    double lastSentFps = 0.0;

    while (!ltcStop_.load()) {
      Uint32 queuedBytes = static_cast<Uint32>(
        std::max(0, SDL_GetAudioStreamAvailable(captureStream)));
      Uint32 minBytes = static_cast<Uint32>(sizeof(std::int16_t) * std::max(1, channelCount));
      if (queuedBytes < minBytes) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }

      size_t sampleFramesAvailable = queuedBytes / (sizeof(std::int16_t) * static_cast<size_t>(std::max(1, channelCount)));
      size_t framesToRead = std::clamp<size_t>(sampleFramesAvailable, 1, 4096);
      interleavedSamples.resize(framesToRead * static_cast<size_t>(std::max(1, channelCount)));
      int bytesRead = SDL_GetAudioStreamData(
        captureStream,
        interleavedSamples.data(),
        static_cast<int>(interleavedSamples.size() * sizeof(std::int16_t)));
      size_t samplesRead = bytesRead > 0 ? static_cast<size_t>(bytesRead) / sizeof(std::int16_t) : 0;
      size_t framesRead = samplesRead / static_cast<size_t>(std::max(1, channelCount));
      if (framesRead == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }

      monoSamples.resize(framesRead);
      for (size_t frameIndex = 0; frameIndex < framesRead; ++frameIndex) {
        monoSamples[frameIndex] = interleavedSamples[frameIndex * static_cast<size_t>(std::max(1, channelCount))];
      }

      ltcApi_.decoderWriteS16Fn(decoder, monoSamples.data(), monoSamples.size(), samplePos);
      samplePos += static_cast<std::int64_t>(monoSamples.size());

      while (ltcApi_.decoderQueueLengthFn(decoder) > 0) {
        if (ltcApi_.decoderReadFn(decoder, frameExt.data()) <= 0) {
          break;
        }
        auto decoded = decodeLtcFrameBytes(frameExt.data());
        if (!decoded) {
          continue;
        }

        fpsEstimator.observe(*decoded);
        double decodedFps = fpsEstimator.current(fpsHint);
        if (!std::isfinite(decodedFps) || decodedFps <= 1.0) {
          decodedFps = 30.0;
        }
        double tcSeconds = decoded->hours * 3600.0
          + decoded->minutes * 60.0
          + decoded->seconds
          + (decoded->frames / decodedFps);

        bool changed = std::fabs(tcSeconds - lastSentSeconds) > (0.5 / std::max(1.0, decodedFps))
                     || std::fabs(decodedFps - lastSentFps) > 0.01;
        if (!changed) {
          continue;
        }

        std::ostringstream secondsText;
        secondsText << std::fixed << std::setprecision(6) << tcSeconds;
        std::ostringstream fpsText;
        if (std::fabs(decodedFps - 29.97) < 0.01) {
          fpsText << "29.97";
        } else {
          fpsText << std::fixed << std::setprecision(2) << decodedFps;
        }
        enqueueRemoteCommand("LTCEXT " + secondsText.str() + " " + fpsText.str());
        lastSentSeconds = tcSeconds;
        lastSentFps = decodedFps;
      }
    }

    ltcApi_.decoderQueueFlushFn(decoder);
    ltcApi_.decoderFreeFn(decoder);
  }

  // Resolve a preferred recording device name to a live SDL3 device id.
  SDL_AudioDeviceID audioRecordingDeviceIdForName(const std::string& name) {
    if (name.empty()) {
      return SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    }
    SDL_AudioDeviceID result = 0;
    int count = 0;
    if (SDL_AudioDeviceID* ids = SDL_GetAudioRecordingDevices(&count)) {
      for (int index = 0; index < count; ++index) {
        const char* deviceName = SDL_GetAudioDeviceName(ids[index]);
        if (deviceName && name == deviceName) {
          result = ids[index];
          break;
        }
      }
      SDL_free(ids);
    }
    return result;
  }

  bool startLtcIngest() {
    if (ltcThread_.joinable() || ltcCaptureStream_ != nullptr) {
      return true;
    }
    if (!ltcApi_.ensureLoaded()) {
      ltcLastError_ = ltcApi_.loadError.empty() ? "ltc runtime missing" : ltcApi_.loadError;
      return false;
    }

    // The SDL3 recording stream converts to this spec regardless of the
    // hardware format, so no "obtained"-spec handling is needed: the read
    // side is always 48 kHz mono S16.
    SDL_AudioSpec desired {};
    desired.freq = kAudioRate;
    desired.format = SDL_AUDIO_S16;
    desired.channels = 1;

    std::string preferredDevice = preferredLtcCaptureDeviceName();
    std::string effectiveDevice = preferredDevice;
    SDL_AudioDeviceID target = audioRecordingDeviceIdForName(preferredDevice);
    if (target == 0) {
      effectiveDevice.clear();
      target = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    }
    SDL_AudioStream* captureStream = SDL_OpenAudioDeviceStream(target, &desired, nullptr, nullptr);
    if (!captureStream && target != SDL_AUDIO_DEVICE_DEFAULT_RECORDING) {
      effectiveDevice.clear();
      captureStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &desired, nullptr, nullptr);
    }
    if (!captureStream) {
      ltcLastError_ = "ltc audio input unavailable";
      return false;
    }

    int sampleRate = kAudioRate;
    int channelCount = 1;
    double fpsHint = defaultLtcCaptureFpsHint();
    int apv = static_cast<int>(std::llround(static_cast<double>(sampleRate) / std::max(1.0, fpsHint)));
    apv = std::clamp(apv, 200, 4000);
    void* decoder = ltcApi_.decoderCreateFn(apv, 32);
    if (!decoder) {
      SDL_DestroyAudioStream(captureStream);
      ltcLastError_ = "ltc decoder init failed";
      return false;
    }

    ltcCaptureStream_ = captureStream;
    ltcCaptureSampleRate_ = sampleRate;
    ltcCaptureChannels_ = channelCount;
    ltcCaptureDeviceName_ = effectiveDevice;
    ltcLastError_.clear();
    ltcLastAnnouncedError_.clear();
    ltcRestartBlockedUntilMs_ = 0;
    ltcStop_.store(false);
    ltcThread_ = std::thread([this, decoder, captureStream, sampleRate, channelCount, fpsHint]() {
      ltcLoop(decoder, captureStream, sampleRate, channelCount, fpsHint);
    });
    SDL_ResumeAudioStreamDevice(captureStream);
    return true;
  }

  void stopLtcIngest() {
    ltcRestartBlockedUntilMs_ = 0;
    ltcStop_.store(true);
    if (ltcThread_.joinable()) {
      ltcThread_.join();
    }
    if (ltcCaptureStream_) {
      SDL_DestroyAudioStream(ltcCaptureStream_);
      ltcCaptureStream_ = nullptr;
    }
    ltcCaptureSampleRate_ = 0;
    ltcCaptureChannels_ = 0;
    ltcCaptureDeviceName_.clear();
  }

  void refreshLtcCaptureState() {
    if (!project_.ltcIngestEnabled) {
      stopLtcIngest();
      ltcLastError_.clear();
      ltcLastAnnouncedError_.clear();
      return;
    }
    if (ltcThread_.joinable() || ltcCaptureStream_ != nullptr) {
      return;
    }
    Uint64 now = SDL_GetTicks();
    if (ltcRestartBlockedUntilMs_ > now) {
      return;
    }
    if (!startLtcIngest()) {
      ltcRestartBlockedUntilMs_ = now + 3000;
      if (!ltcLastError_.empty() && ltcLastError_ != ltcLastAnnouncedError_) {
        triggerToast("ltc: " + ltcLastError_);
        ltcLastAnnouncedError_ = ltcLastError_;
      }
    }
  }

  void destroyDeckRuntime(DeckRuntime& runtime) {
    if (runtime.mediaEngine) {
      runtime.mediaEngine->stopAll();
      runtime.mediaEngine.reset();
    }
    if (runtime.browserRenderer) {
      runtime.browserRenderer->stop();
      runtime.browserRenderer.reset();
    }
    runtime.browserCueLive = false;
    if (runtime.audioStream) {
      SDL_DestroyAudioStream(runtime.audioStream);
      runtime.audioStream = nullptr;
    }
    if (runtime.outputRenderer) {
      SDL_DestroyRenderer(runtime.outputRenderer);
      runtime.outputRenderer = nullptr;
    }
    if (runtime.outputWindow) {
      SDL_DestroyWindow(runtime.outputWindow);
      runtime.outputWindow = nullptr;
    }
  }

  static std::string shellQuoteSingle(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    escaped.push_back('\'');
    for (char ch : value) {
      if (ch == '\'') {
        escaped += "'\"'\"'";
      } else {
        escaped.push_back(ch);
      }
    }
    escaped.push_back('\'');
    return escaped;
  }

  struct OutputBackendRuntimeRoute {
    bool windowSupported = true;
    bool streamSupported = false;
    bool ndiSupported = false;
    bool deckLinkSupported = false;
    bool spoutSupported = false;
    std::string summary;
  };

  const deckboy::platform::OutputBackendCatalog& outputBackendCatalog() const {
    static std::unique_ptr<deckboy::platform::OutputBackendCatalog> catalog =
      deckboy::platform::createOutputBackendCatalog();
    return *catalog;
  }

  deckboy::platform::OutputBackendRouteRequest outputBackendRouteRequestForOutput(int outputIndex) const {
    deckboy::platform::OutputBackendRouteRequest request;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return request;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    request.outputType = normalizeOutputType(output.outputType);
    request.streamEnabled = output.streamEnabled;
    request.ndiEnabled = output.ndiEnabled || output.ndiKeyEnabled;
    request.deckLinkEnabled = output.deckLinkEnabled;
    request.spoutEnabled = output.spoutEnabled;
    return request;
  }

  OutputBackendRuntimeRoute resolveOutputBackendRuntimeRoute(int outputIndex) const {
    OutputBackendRuntimeRoute route;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      route.summary = "invalid";
      return route;
    }
    auto request = outputBackendRouteRequestForOutput(outputIndex);
    auto plan = deckboy::platform::planOutputBackendRoute(request, outputBackendCatalog());
    std::ostringstream summary;
    bool wroteToken = false;
    for (const auto& step : plan.steps) {
      if (wroteToken) {
        summary << '+';
      }
      summary << step.backendId << '[' << (step.supported ? "ok" : "stub") << ']';
      wroteToken = true;
      switch (step.kind) {
        case deckboy::platform::OutputRouteKind::Window:
          route.windowSupported = step.supported;
          break;
        case deckboy::platform::OutputRouteKind::Stream:
          route.streamSupported = step.supported;
          break;
        case deckboy::platform::OutputRouteKind::Ndi:
          route.ndiSupported = step.supported;
          break;
        case deckboy::platform::OutputRouteKind::DeckLink:
          route.deckLinkSupported = step.supported;
          break;
        case deckboy::platform::OutputRouteKind::Spout:
          route.spoutSupported = step.supported;
          break;
      }
    }
    route.summary = wroteToken ? summary.str() : "none";
    return route;
  }

  std::string outputBackendRouteSummary(int outputIndex) const {
    return resolveOutputBackendRuntimeRoute(outputIndex).summary;
  }

  struct IntegrationBackendRuntimeRoute {
    bool atemEnabled = false;
    bool atemSupported = false;
    bool ndiTriggerEnabled = false;
    bool ndiTriggerSupported = false;
    bool nmcSyncEnabled = false;
    bool nmcSyncSupported = false;
    std::string nmcSyncMode;
    bool mtcIngestEnabled = false;
    bool mtcIngestSupported = false;
    bool ltcIngestEnabled = false;
    bool ltcIngestSupported = false;
    bool dmxArtNetEnabled = false;
    bool dmxArtNetSupported = false;
    std::string summary;
  };

  const deckboy::platform::IntegrationBackendCatalog& integrationBackendCatalog() const {
    static std::unique_ptr<deckboy::platform::IntegrationBackendCatalog> catalog =
      deckboy::platform::createIntegrationBackendCatalog();
    return *catalog;
  }

  deckboy::platform::IntegrationBackendRouteRequest integrationBackendRouteRequest() const {
    deckboy::platform::IntegrationBackendRouteRequest request;
    request.atemTriggerEnabled = project_.atemTriggerEnabled;
    request.ndiTriggerEnabled = project_.ndiTriggerEnabled;
    request.nmcSyncEnabled = project_.nmcSyncEnabled;
    request.mtcIngestEnabled = project_.mtcIngestEnabled;
    request.ltcIngestEnabled = project_.ltcIngestEnabled;
    request.dmxArtNetEnabled = project_.dmxArtNetEnabled;
    return request;
  }

  IntegrationBackendRuntimeRoute resolveIntegrationBackendRuntimeRoute() const {
    IntegrationBackendRuntimeRoute route;
    auto plan = deckboy::platform::planIntegrationBackendRoute(
      integrationBackendRouteRequest(),
      integrationBackendCatalog());
    std::ostringstream summary;
    bool wroteToken = false;
    for (const auto& step : plan.steps) {
      if (wroteToken) {
        summary << ',';
      }
      summary << step.backendId
              << '[' << (step.enabled ? "on" : "off") << ','
              << (step.supported ? "ok" : "stub");
      switch (step.kind) {
        case deckboy::platform::IntegrationBackendKind::AtemTrigger:
          route.atemEnabled = step.enabled;
          route.atemSupported = step.supported;
          break;
        case deckboy::platform::IntegrationBackendKind::NdiTrigger:
          route.ndiTriggerEnabled = step.enabled;
          route.ndiTriggerSupported = step.supported;
          break;
        case deckboy::platform::IntegrationBackendKind::NmcSync:
          route.nmcSyncEnabled = step.enabled;
          route.nmcSyncSupported = step.supported;
          route.nmcSyncMode = resolvedNmcSyncMode();
          if (step.enabled && step.supported) {
            summary << ',' << (route.nmcSyncMode == "output" ? "out" : "in");
          }
          break;
        case deckboy::platform::IntegrationBackendKind::MtcIngest:
          route.mtcIngestEnabled = step.enabled;
          route.mtcIngestSupported = step.supported;
          break;
        case deckboy::platform::IntegrationBackendKind::LtcIngest:
          route.ltcIngestEnabled = step.enabled;
          route.ltcIngestSupported = step.supported;
          break;
        case deckboy::platform::IntegrationBackendKind::DmxArtNet:
          route.dmxArtNetEnabled = step.enabled;
          route.dmxArtNetSupported = step.supported;
          break;
      }
      summary << ']';
      wroteToken = true;
    }
    route.summary = wroteToken ? summary.str() : "none";
    return route;
  }

  std::string integrationBackendRouteSummary() const {
    return resolveIntegrationBackendRuntimeRoute().summary;
  }

  double outputStreamFps(double fpsHint) const {
    if (std::isfinite(project_.outputRefreshRateHz) && project_.outputRefreshRateHz > 1.0) {
      return std::clamp(project_.outputRefreshRateHz, 1.0, 120.0);
    }
    if (std::isfinite(fpsHint) && fpsHint > 1.0) {
      return std::clamp(fpsHint, 1.0, 120.0);
    }
    return 30.0;
  }

  void pushDeckStreamAudioSamples(int deckIndex, const std::vector<std::int16_t>& samples) {
    if (deckIndex < 0 || samples.empty()) {
      return;
    }
    static constexpr size_t kMaxBufferedSamplesPerDeck = static_cast<size_t>(48000 * 2 * 10); // ~10s stereo
    std::lock_guard<std::mutex> lock(streamAudioMutex_);
    if (deckIndex >= static_cast<int>(deckStreamAudioBuffers_.size())) {
      deckStreamAudioBuffers_.resize(deckIndex + 1);
    }
    DeckStreamAudioBuffer& buffer = deckStreamAudioBuffers_[deckIndex];
    buffer.samples.insert(buffer.samples.end(), samples.begin(), samples.end());
    if (buffer.samples.size() > kMaxBufferedSamplesPerDeck) {
      size_t dropCount = buffer.samples.size() - kMaxBufferedSamplesPerDeck;
      buffer.samples.erase(buffer.samples.begin(), buffer.samples.begin() + static_cast<std::ptrdiff_t>(dropCount));
      buffer.droppedSamples += static_cast<std::uint64_t>(dropCount);
    }
  }

  void clearDeckStreamAudioBuffers() {
    std::lock_guard<std::mutex> lock(streamAudioMutex_);
    deckStreamAudioBuffers_.clear();
    deckStreamAudioBuffers_.resize(project_.decks.size());
  }

  std::vector<int> streamAudioDecksForOutput(int outputIndex) const {
    std::vector<int> deckIndices;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size()) || project_.decks.empty()) {
      return deckIndices;
    }
    int routedOutputIndex = outputIndex;
    const OutputTarget& output = project_.outputs[outputIndex];
    if (output.mirrorSourceOutputIndex >= 0 &&
        output.mirrorSourceOutputIndex < static_cast<int>(project_.outputs.size())) {
      routedOutputIndex = output.mirrorSourceOutputIndex;
    }
    for (const auto& entry : layeredDeckEntriesForOutput(routedOutputIndex)) {
      if (entry.second >= 0 && entry.second < static_cast<int>(project_.decks.size())) {
        deckIndices.push_back(entry.second);
      }
    }
    if (deckIndices.empty()) {
      int hostDeck = std::clamp(project_.outputs[routedOutputIndex].hostDeckIndex, 0,
                                static_cast<int>(project_.decks.size()) - 1);
      deckIndices.push_back(hostDeck);
    }
    std::sort(deckIndices.begin(), deckIndices.end());
    deckIndices.erase(std::unique(deckIndices.begin(), deckIndices.end()), deckIndices.end());
    return deckIndices;
  }

  std::string buildOutputStreamSpec(int outputIndex, int width, int height, double fpsHint) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return {};
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    std::string protocol = normalizeOutputStreamProtocol(output.streamProtocol);
    std::string url = trim(output.streamUrl);
    if (url.empty()) {
      url = defaultOutputStreamUrl(protocol, outputIndex);
    }
    std::string key = trim(output.streamKey);
    if (!key.empty() && (protocol == "rtmp" || protocol == "rtmps")) {
      if (url.back() != '/') url += '/';
      url += key;
    }
    int bitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    double fps = outputStreamFps(fpsHint);
    std::string colorSpace = normalizeOutputColorSpace(output.outputColorSpace);
    std::ostringstream spec;
    spec << protocol << '|'
         << url << '|'
         << width << 'x' << height << '|'
         << std::fixed << std::setprecision(2) << fps << '|'
         << bitrateKbps << '|'
         << colorSpace;
    return spec.str();
  }

  std::vector<std::string> buildOutputStreamArgs(int outputIndex,
                                                 int width,
                                                 int height,
                                                 double fpsHint,
                                                 const std::string& videoInputPath) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return {};
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    std::string protocol = normalizeOutputStreamProtocol(output.streamProtocol);
    std::string url = trim(output.streamUrl);
    if (url.empty()) {
      url = defaultOutputStreamUrl(protocol, outputIndex);
    }
    std::string key = trim(output.streamKey);
    if (!key.empty() && (protocol == "rtmp" || protocol == "rtmps")) {
      if (url.back() != '/') url += '/';
      url += key;
    }
    int bitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    int bufferKbps = std::clamp(bitrateKbps * 2, 1000, 100000);
    double fps = outputStreamFps(fpsHint);
    int gop = std::max(1, static_cast<int>(std::lround(fps)));
    std::string mux = (protocol == "rtmp") ? "flv" : "mpegts";
    std::string colorSpace = normalizeOutputColorSpace(output.outputColorSpace);
    std::ostringstream fpsText;
    fpsText << std::fixed << std::setprecision(2) << fps;

    std::vector<std::string> args {
      "ffmpeg",
      "-hide_banner",
      "-nostdin",
      "-loglevel", "error",
      "-thread_queue_size", "2048",
      "-f", "rawvideo",
      "-pix_fmt", "bgra",
      "-video_size", std::to_string(width) + "x" + std::to_string(height),
      "-framerate", fpsText.str(),
      "-i", videoInputPath.empty() ? "pipe:0" : videoInputPath,
      // Keep a stable AAC track present so SRT/RTMP egress starts cleanly
      // even when there is no routed deck audio available yet.
      "-f", "lavfi",
      "-i", "anullsrc=r=48000:cl=stereo",
      "-map", "0:v:0",
      "-map", "1:a:0",
      "-c:v", "libx264",
      "-preset", "veryfast",
      "-tune", "zerolatency",
      "-pix_fmt", "yuv420p"
    };
    if (colorSpace == "bt709") {
      args.insert(args.end(), {
        "-colorspace", "bt709",
        "-color_primaries", "bt709",
        "-color_trc", "bt709"
      });
    } else if (colorSpace == "srgb") {
      args.insert(args.end(), {
        "-colorspace", "bt709",
        "-color_primaries", "bt709",
        "-color_trc", "iec61966-2-1"
      });
    }
    args.insert(args.end(), {
      "-fflags", "+genpts",
      "-max_interleave_delta", "0",
      "-flush_packets", "1",
      "-muxdelay", "0",
      "-muxpreload", "0",
      "-mpegts_flags", "+resend_headers",
      "-g", std::to_string(gop),
      "-b:v", std::to_string(bitrateKbps) + "k",
      "-maxrate", std::to_string(bitrateKbps) + "k",
      "-bufsize", std::to_string(bufferKbps) + "k",
      "-c:a", "aac",
      "-b:a", "160k",
      "-ar", "48000",
      "-ac", "2",
      "-f", mux,
      url
    });
    return args;
  }

  std::string shellCommandString(const std::vector<std::string>& args) const {
    std::ostringstream command;
    bool first = true;
    for (const std::string& arg : args) {
      if (!first) {
        command << ' ';
      }
      first = false;
      command << shellQuoteSingle(arg);
    }
    return command.str();
  }

  static void setFdBlockingMode(int fd, bool blocking) {
#ifndef _WIN32
    if (fd < 0) {
      return;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
      return;
    }
    if (blocking) {
      flags &= ~O_NONBLOCK;
    } else {
      flags |= O_NONBLOCK;
    }
    fcntl(fd, F_SETFL, flags);
#else
    (void) fd;
    (void) blocking;
#endif
  }

  static bool writeOutputStreamBytesBlocking(
      const std::shared_ptr<OutputStreamWriterState>& writer,
      int fd,
      const std::uint8_t* bytes,
      size_t byteCount,
      const char* reason) {
    size_t offset = 0;
    while (offset < byteCount) {
      {
        std::lock_guard<std::mutex> lock(writer->mutex);
        if (writer->stop) {
          return false;
        }
      }
#ifdef _WIN32
      int written = _write(fd, bytes + offset, static_cast<unsigned int>(std::min(byteCount - offset, size_t(0x7FFFFFFFu))));
#else
      ssize_t written = write(fd, bytes + offset, byteCount - offset);
#endif
      if (written > 0) {
        offset += static_cast<size_t>(written);
        continue;
      }
#ifndef _WIN32
      if (written < 0 && errno == EINTR) {
        continue;
      }
      if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        SDL_Delay(1);
        continue;
      }
#endif
      std::lock_guard<std::mutex> lock(writer->mutex);
      writer->failed = true;
      writer->failureReason = reason ? reason : "stream write failed";
      writer->stop = true;
      writer->hasPendingPacket = false;
      return false;
    }
    return true;
  }

  static bool writeOutputStreamBytesBestEffort(
      const std::shared_ptr<OutputStreamWriterState>& writer,
      int fd,
      const std::uint8_t* bytes,
      size_t byteCount,
      const char* reason) {
    size_t offset = 0;
    while (offset < byteCount) {
      {
        std::lock_guard<std::mutex> lock(writer->mutex);
        if (writer->stop) {
          return false;
        }
      }
#ifdef _WIN32
      int written = _write(fd, bytes + offset, static_cast<unsigned int>(std::min(byteCount - offset, size_t(0x7FFFFFFFu))));
#else
      ssize_t written = write(fd, bytes + offset, byteCount - offset);
#endif
      if (written > 0) {
        offset += static_cast<size_t>(written);
        continue;
      }
#ifndef _WIN32
      if (written < 0 && errno == EINTR) {
        continue;
      }
      if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return true;
      }
#endif
      std::lock_guard<std::mutex> lock(writer->mutex);
      writer->failed = true;
      writer->failureReason = reason ? reason : "stream write failed";
      writer->stop = true;
      writer->hasPendingPacket = false;
      return false;
    }
    return true;
  }

  void startOutputStreamWriter(OutputRuntime& runtime) {
#ifdef _WIN32
    int videoPipeFd = runtime.streamProcess.writeFd;
    int audioPipeFd = -1;  // audio muxed via ffmpeg stdin on Windows
    if (runtime.streamWriter || videoPipeFd < 0) {
      return;
    }
#else
    if (runtime.streamWriter || runtime.streamPipeFd < 0) {
      return;
    }
    int videoPipeFd = runtime.streamPipeFd;
    int audioPipeFd = runtime.streamAudioPipeFd;
#endif
    auto writer = std::make_shared<OutputStreamWriterState>();
    writer->videoPipeFd = videoPipeFd;
    writer->audioPipeFd = audioPipeFd;
    writer->thread = std::thread([writer]() {
      for (;;) {
        OutputStreamPacket packet;
        {
          std::unique_lock<std::mutex> lock(writer->mutex);
          writer->cv.wait(lock, [&]() {
            return writer->stop || writer->hasPendingPacket;
          });
          if (writer->stop) {
            break;
          }
          packet = std::move(writer->pendingPacket);
          writer->hasPendingPacket = false;
        }

        if (!packet.audioSamples.empty() && writer->audioPipeFd >= 0) {
          const std::uint8_t* audioBytes =
            reinterpret_cast<const std::uint8_t*>(packet.audioSamples.data());
          size_t audioByteCount = packet.audioSamples.size() * sizeof(std::int16_t);
          if (!writeOutputStreamBytesBestEffort(
                writer,
                writer->audioPipeFd,
                audioBytes,
                audioByteCount,
                "stream audio stopped")) {
            break;
          }
          {
            std::lock_guard<std::mutex> lock(writer->mutex);
            writer->audioBytesWritten += static_cast<std::uint64_t>(audioByteCount);
          }
        }

        if (!packet.videoBytes.empty() && writer->videoPipeFd >= 0) {
          if (!writeOutputStreamBytesBlocking(
                writer,
                writer->videoPipeFd,
                packet.videoBytes.data(),
                packet.videoBytes.size(),
                "stream video stopped")) {
            break;
          }
          {
            std::lock_guard<std::mutex> lock(writer->mutex);
            writer->packetsWritten += 1;
            writer->videoBytesWritten += static_cast<std::uint64_t>(packet.videoBytes.size());
          }
        }
      }
    });
    runtime.streamWriter = writer;
  }

  std::string outputStreamWriterFailure(OutputRuntime& runtime) const {
    auto writer = runtime.streamWriter;
    if (!writer) {
      return {};
    }
    std::lock_guard<std::mutex> lock(writer->mutex);
    if (!writer->failed) {
      return {};
    }
    return writer->failureReason.empty() ? "stream write failed" : writer->failureReason;
  }

  void stopOutputStreamRuntime(OutputRuntime& runtime) {
    auto writer = runtime.streamWriter;
    runtime.streamWriter.reset();
    if (writer) {
      {
        std::lock_guard<std::mutex> lock(writer->mutex);
        writer->stop = true;
        writer->hasPendingPacket = false;
      }
      writer->cv.notify_all();
    }
#ifdef _WIN32
    // Close the write pipe first so ffmpeg gets EOF on stdin and exits gracefully
    if (runtime.streamProcess.writeFd >= 0) {
      _close(runtime.streamProcess.writeFd);
      runtime.streamProcess.writeFd = -1;
    }
    if (writer && writer->thread.joinable()) {
      writer->thread.join();
    }
    // Give ffmpeg a moment to exit gracefully before force-killing
    if (runtime.streamProcess.running()) {
      WaitForSingleObject(runtime.streamProcess.hProcess, 500);
    }
    runtime.streamProcess.stop();
#else
    pid_t streamPid = runtime.streamPid;
    runtime.streamPid = -1;
    if (streamPid > 0) {
      kill(streamPid, SIGTERM);
    }
    if (runtime.streamPipeFd >= 0) {
      close(runtime.streamPipeFd);
      runtime.streamPipeFd = -1;
    }
    if (runtime.streamAudioPipeFd >= 0) {
      close(runtime.streamAudioPipeFd);
      runtime.streamAudioPipeFd = -1;
    }
    if (writer && writer->thread.joinable()) {
      writer->thread.join();
    }
    if (streamPid > 0) {
      int status = 0;
      Uint64 deadline = SDL_GetTicks() + 500;
      while (waitpid(streamPid, &status, WNOHANG) == 0 && SDL_GetTicks() < deadline) {
        SDL_Delay(10);
      }
      if (waitpid(streamPid, &status, WNOHANG) == 0) {
        kill(streamPid, SIGKILL);
        waitpid(streamPid, &status, 0);
      }
    }
    if (!runtime.streamVideoPipePath.empty()) {
      unlink(runtime.streamVideoPipePath.c_str());
      runtime.streamVideoPipePath.clear();
    }
#endif
    runtime.streamSpec.clear();
    runtime.streamCommand.clear();
    runtime.streamFrameBuffer.clear();
    runtime.streamAudioReadSamplesByDeck.clear();
    runtime.streamAudioSampleRemainder = 0.0;
    runtime.streamFrameWidth = 0;
    runtime.streamFrameHeight = 0;
    runtime.lastStreamCaptureSentAtMs = 0;
    resetOutputStreamFpsTelemetry(runtime);
  }

  void stopOutputStream(int outputIndex) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      return;
    }
    stopOutputStreamRuntime(*runtime);
  }

#ifdef _WIN32
  // Windows: spawn ffmpeg with stdin pipe for video input.
  // The args should use "pipe:0" or "-" as the input path for reading from stdin.
  bool spawnOutputStreamProcess(OutputRuntime& runtime,
                                const std::vector<std::string>& args) {
    if (args.empty()) {
      return false;
    }
    runtime.streamProcess.stop();
    if (!spawnProcess(runtime.streamProcess, args, SpawnOptions::pipedStdin())) {
      return false;
    }
    return runtime.streamProcess.writeFd >= 0;
  }
#else
  bool spawnOutputStreamProcess(OutputRuntime& runtime,
                                const std::vector<std::string>& args,
                                const std::string& videoInputPath) {
    if (args.empty() || videoInputPath.empty()) {
      return false;
    }

    unlink(videoInputPath.c_str());
    if (mkfifo(videoInputPath.c_str(), 0600) != 0) {
      return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
      unlink(videoInputPath.c_str());
      return false;
    }

    if (pid == 0) {
      int stdinFd = open("/dev/null", O_RDONLY);
      if (stdinFd >= 0) {
        dup2(stdinFd, STDIN_FILENO);
        close(stdinFd);
      }
      int stdoutFd = open("/dev/null", O_WRONLY);
      if (stdoutFd >= 0) {
        dup2(stdoutFd, STDOUT_FILENO);
        close(stdoutFd);
      }

      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
      }
      argv.push_back(nullptr);
      execvp(argv[0], argv.data());
      _exit(127);
    }

    int videoFd = -1;
    Uint64 deadline = SDL_GetTicks() + 1000;
    while (videoFd < 0 && SDL_GetTicks() < deadline) {
      videoFd = open(videoInputPath.c_str(), O_WRONLY | O_NONBLOCK);
      if (videoFd >= 0) {
        break;
      }
      if (errno != ENXIO && errno != ENOENT && errno != EINTR) {
        break;
      }
      SDL_Delay(10);
    }
    if (videoFd < 0) {
      kill(pid, SIGTERM);
      int status = 0;
      waitpid(pid, &status, 0);
      unlink(videoInputPath.c_str());
      return false;
    }

    runtime.streamPid = pid;
    runtime.streamVideoPipePath = videoInputPath;
    runtime.streamPipeFd = videoFd;
    runtime.streamAudioPipeFd = -1;
    setCloseOnExec(runtime.streamPipeFd);
    setFdBlockingMode(runtime.streamPipeFd, true);
    return true;
  }
#endif

  void primeOutputStreamAudioReadPositions(int outputIndex, OutputRuntime& runtime) {
    runtime.streamAudioReadSamplesByDeck.clear();
    auto deckIndices = streamAudioDecksForOutput(outputIndex);
    std::lock_guard<std::mutex> lock(streamAudioMutex_);
    for (int deckIndex : deckIndices) {
      if (deckIndex < 0 || deckIndex >= static_cast<int>(deckStreamAudioBuffers_.size())) {
        runtime.streamAudioReadSamplesByDeck[deckIndex] = 0;
        continue;
      }
      const DeckStreamAudioBuffer& buffer = deckStreamAudioBuffers_[deckIndex];
      runtime.streamAudioReadSamplesByDeck[deckIndex] = buffer.droppedSamples + static_cast<std::uint64_t>(buffer.samples.size());
    }
  }

  // NDI audio priming and sample collection — cross-platform (used by NDI send
  // and potentially DeckLink audio). Moved out of #ifndef _WIN32 so NDI audio
  // works on Windows.
  void primeOutputNdiAudioReadPositions(int outputIndex, OutputRuntime& runtime) {
    runtime.ndiAudioReadSamplesByDeck.clear();
    auto deckIndices = streamAudioDecksForOutput(outputIndex);
    std::lock_guard<std::mutex> lock(streamAudioMutex_);
    for (int deckIndex : deckIndices) {
      if (deckIndex < 0 || deckIndex >= static_cast<int>(deckStreamAudioBuffers_.size())) {
        runtime.ndiAudioReadSamplesByDeck[deckIndex] = 0;
        continue;
      }
      const DeckStreamAudioBuffer& buffer = deckStreamAudioBuffers_[deckIndex];
      runtime.ndiAudioReadSamplesByDeck[deckIndex] = buffer.droppedSamples + static_cast<std::uint64_t>(buffer.samples.size());
    }
  }

  std::vector<std::int16_t> collectOutputAudioFrameSamples(
      int outputIndex,
      std::map<int, std::uint64_t>& readSamplesByDeck,
      double& sampleRemainder,
      double fpsHint) {
    constexpr int kSampleRate = 48000;
    constexpr int kChannels = 2;

    double fps = outputStreamFps(fpsHint);
    double exactFrames = (static_cast<double>(kSampleRate) / std::max(1.0, fps)) + sampleRemainder;
    int sampleFrames = std::max(1, static_cast<int>(std::floor(exactFrames)));
    sampleRemainder = exactFrames - static_cast<double>(sampleFrames);
    int interleavedSamples = sampleFrames * kChannels;
    std::vector<std::int32_t> mixed(interleavedSamples, 0);

    auto deckIndices = streamAudioDecksForOutput(outputIndex);
    {
      std::lock_guard<std::mutex> lock(streamAudioMutex_);
      if (deckStreamAudioBuffers_.size() < project_.decks.size()) {
        deckStreamAudioBuffers_.resize(project_.decks.size());
      }
      for (int deckIndex : deckIndices) {
        if (deckIndex < 0 || deckIndex >= static_cast<int>(deckStreamAudioBuffers_.size())) {
          continue;
        }
        const DeckStreamAudioBuffer& buffer = deckStreamAudioBuffers_[deckIndex];
        std::uint64_t availableBegin = buffer.droppedSamples;
        std::uint64_t availableEnd = availableBegin + static_cast<std::uint64_t>(buffer.samples.size());
        auto [it, inserted] = readSamplesByDeck.try_emplace(deckIndex, availableEnd);
        std::uint64_t& readPos = it->second;
        if (inserted) {
          continue;
        }
        if (readPos < availableBegin) {
          readPos = availableBegin;
        } else if (readPos > availableEnd) {
          readPos = availableEnd;
        }
        std::uint64_t available = availableEnd - readPos;
        size_t toMix = static_cast<size_t>(std::min<std::uint64_t>(available, static_cast<std::uint64_t>(interleavedSamples)));
        size_t sourceOffset = static_cast<size_t>(readPos - availableBegin);
        for (size_t i = 0; i < toMix; ++i) {
          mixed[i] += static_cast<std::int32_t>(buffer.samples[sourceOffset + i]);
        }
        readPos += static_cast<std::uint64_t>(toMix);
      }
    }

    std::vector<std::int16_t> out(interleavedSamples, 0);
    for (int i = 0; i < interleavedSamples; ++i) {
      out[i] = static_cast<std::int16_t>(std::clamp(mixed[i], -32768, 32767));
    }
    return out;
  }

  bool ensureOutputStreamRunning(int outputIndex, int width, int height, double fpsHint) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "stream runtime unavailable");
      return false;
    }
    if (!output.streamEnabled) {
      stopOutputStreamRuntime(*runtime);
      if (output.enabled) {
        setOutputHealthState(outputIndex, OutputHealthState::Armed);
      }
      return false;
    }
    Uint64 nowMs = SDL_GetTicks();
    if (runtime->streamStartFailed &&
        runtime->streamRestartBlockedUntilMs > nowMs) {
      setOutputHealthState(outputIndex, OutputHealthState::Recovering, "stream retrying");
      return false;
    }
    width = std::max(1, width);
    height = std::max(1, height);
    output.streamProtocol = normalizeOutputStreamProtocol(output.streamProtocol);
    output.streamBitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    if (trim(output.streamUrl).empty()) {
      output.streamUrl = defaultOutputStreamUrl(output.streamProtocol, outputIndex);
    }
    std::string desiredSpec = buildOutputStreamSpec(outputIndex, width, height, fpsHint);
    bool processAlive = false;
#ifdef _WIN32
    processAlive = runtime->streamProcess.running();
#else
    processAlive = runtime->streamPid > 0;
#endif
    if (runtime->streamSpec == desiredSpec && processAlive) {
      if (output.enabled) {
        setOutputHealthState(outputIndex, OutputHealthState::Live);
      }
      return true;
    }

    stopOutputStreamRuntime(*runtime);
    setOutputHealthState(outputIndex, OutputHealthState::Recovering, "starting stream");
#ifdef _WIN32
    // Windows: use stdin pipe (pipe:0) instead of named FIFO
    std::vector<std::string> args = buildOutputStreamArgs(outputIndex, width, height, fpsHint, "");
    // Remove -nostdin since we're piping video via stdin
    args.erase(std::remove(args.begin(), args.end(), "-nostdin"), args.end());
#else
    std::string videoInputPath = (fs::temp_directory_path() /
      ("deckboy_stream_video_" + std::to_string(outputIndex) + "_" + std::to_string(SDL_GetTicks()) + ".fifo")).string();
    std::vector<std::string> args = buildOutputStreamArgs(outputIndex, width, height, fpsHint, videoInputPath);
#endif
    if (args.empty()) {
      runtime->streamStartFailed = true;
      runtime->streamRestartBlockedUntilMs = SDL_GetTicks() + 1500;
      stopOutputStreamRuntime(*runtime);
      setOutputHealthState(outputIndex, OutputHealthState::Error, "stream command invalid");
      return false;
    }
#ifdef _WIN32
    if (!spawnOutputStreamProcess(*runtime, args)) {
#else
    if (!spawnOutputStreamProcess(*runtime, args, videoInputPath)) {
#endif
      stopOutputStreamRuntime(*runtime);
      if (!runtime->streamStartFailed && outputIndex == project_.focusedOutputIndex) {
        triggerToast("stream failed");
      }
      runtime->streamStartFailed = true;
      runtime->streamRestartBlockedUntilMs = SDL_GetTicks() + 1500;
      setOutputHealthState(outputIndex, OutputHealthState::Error, "stream process failed");
      return false;
    }
    runtime->streamSpec = desiredSpec;
    runtime->streamCommand = shellCommandString(args);
    runtime->streamFrameWidth = width;
    runtime->streamFrameHeight = height;
    runtime->streamAudioSampleRemainder = 0.0;
    primeOutputStreamAudioReadPositions(outputIndex, *runtime);
    runtime->streamFrameBuffer.clear();
    runtime->streamStartFailed = false;
    runtime->streamRestartBlockedUntilMs = 0;
    startOutputStreamWriter(*runtime);
    setOutputHealthState(outputIndex, OutputHealthState::Live);
    return true;
  }

  bool rotateCapturedFramePixels(const std::vector<std::uint8_t>& sourcePixels,
                                 int sourceW,
                                 int sourceH,
                                 int orientationDegrees,
                                 std::vector<std::uint8_t>& destPixels,
                                 int& destW,
                                 int& destH) const {
    int normalized = normalizeOutputOrientationDegrees(orientationDegrees);
    if (sourceW <= 0 || sourceH <= 0) {
      return false;
    }
    if (normalized == 0) {
      destW = sourceW;
      destH = sourceH;
      destPixels = sourcePixels;
      return true;
    }

    if (normalized == 90 || normalized == 270) {
      destW = sourceH;
      destH = sourceW;
    } else {
      destW = sourceW;
      destH = sourceH;
    }
    destPixels.assign(static_cast<size_t>(destW) * static_cast<size_t>(destH) * 4u, 0);
    if (destPixels.empty()) {
      return false;
    }

    for (int y = 0; y < sourceH; ++y) {
      for (int x = 0; x < sourceW; ++x) {
        int dx = x;
        int dy = y;
        if (normalized == 90) {
          dx = sourceH - 1 - y;
          dy = x;
        } else if (normalized == 180) {
          dx = sourceW - 1 - x;
          dy = sourceH - 1 - y;
        } else if (normalized == 270) {
          dx = y;
          dy = sourceW - 1 - x;
        }
        size_t srcOffset = (static_cast<size_t>(y) * static_cast<size_t>(sourceW) + static_cast<size_t>(x)) * 4u;
        size_t dstOffset = (static_cast<size_t>(dy) * static_cast<size_t>(destW) + static_cast<size_t>(dx)) * 4u;
        std::memcpy(destPixels.data() + dstOffset, sourcePixels.data() + srcOffset, 4u);
      }
    }
    return true;
  }

  bool captureOutputFrameForEgress(int outputIndex,
                                   OutputRuntime& runtime,
                                   const SDL_Rect& requestedRect,
                                   double fpsHint) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size()) || !runtime.outputRenderer) {
      return false;
    }
    Uint64 nowMs = SDL_GetTicks();
    double captureFps = outputStreamFps(fpsHint);
    Uint64 minCaptureIntervalMs = static_cast<Uint64>(std::max(1.0, std::floor(1000.0 / std::max(1.0, captureFps))));
    if (runtime.latestCapturedFrame.width > 0 &&
        runtime.latestCapturedFrame.height > 0 &&
        !runtime.latestCapturedFrame.pixels.empty() &&
        runtime.lastEgressCaptureAtMs > 0 &&
        nowMs > runtime.lastEgressCaptureAtMs &&
        nowMs - runtime.lastEgressCaptureAtMs < minCaptureIntervalMs) {
      return true;
    }
    SDL_Rect captureRect = requestedRect;
    captureRect.w = std::max(1, captureRect.w);
    captureRect.h = std::max(1, captureRect.h);
    captureRect.x = std::max(0, captureRect.x);
    captureRect.y = std::max(0, captureRect.y);
    if (runtime.compositorTexture) {
      int texW = std::max(1, runtime.compositorWidth);
      int texH = std::max(1, runtime.compositorHeight);
      if (captureRect.x >= texW || captureRect.y >= texH) {
        return false;
      }
      captureRect.w = std::min(captureRect.w, texW - captureRect.x);
      captureRect.h = std::min(captureRect.h, texH - captureRect.y);
    } else {
      captureRect.x = 0;
      captureRect.y = 0;
    }

    int captureW = std::max(1, captureRect.w);
    int captureH = std::max(1, captureRect.h);
    size_t stride = static_cast<size_t>(captureW) * 4u;
    size_t frameBytes = stride * static_cast<size_t>(captureH);
    if (runtime.latestCapturedFrame.pixels.size() != frameBytes) {
      runtime.latestCapturedFrame.pixels.resize(frameBytes);
    }
    if (runtime.latestCapturedFrame.pixels.empty()) {
      return false;
    }

    SDL_Texture* previousTarget = SDL_GetRenderTarget(runtime.outputRenderer);
    if (runtime.compositorTexture) {
      SDL_SetRenderTarget(runtime.outputRenderer, runtime.compositorTexture);
    }
    // SDL3: read into a temporary surface, then convert into the persistent
    // BGRA egress buffer (SDL2 wrote the requested format directly).
    bool ok = false;
    if (SDL_Surface* captured = SDL_RenderReadPixels(runtime.outputRenderer, &captureRect)) {
      ok = captured->w == captureW && captured->h == captureH &&
           SDL_ConvertPixels(captured->w, captured->h,
                             captured->format, captured->pixels, captured->pitch,
                             SDL_PIXELFORMAT_BGRA32,
                             runtime.latestCapturedFrame.pixels.data(),
                             static_cast<int>(stride));
      SDL_DestroySurface(captured);
    }
    if (runtime.compositorTexture) {
      SDL_SetRenderTarget(runtime.outputRenderer, previousTarget);
    }
    if (!ok) {
      return false;
    }

    int orientationDegrees = normalizeOutputOrientationDegrees(project_.outputs[outputIndex].outputOrientationDegrees);
    if (orientationDegrees != 0) {
      std::vector<std::uint8_t> rotatedPixels;
      int rotatedW = captureW;
      int rotatedH = captureH;
      if (!rotateCapturedFramePixels(
            runtime.latestCapturedFrame.pixels,
            captureW,
            captureH,
            orientationDegrees,
            rotatedPixels,
            rotatedW,
            rotatedH)) {
        return false;
      }
      runtime.latestCapturedFrame.pixels.swap(rotatedPixels);
      captureW = rotatedW;
      captureH = rotatedH;
    }

    nowMs = SDL_GetTicks();
    runtime.latestCapturedFrame.width = captureW;
    runtime.latestCapturedFrame.height = captureH;
    runtime.latestCapturedFrame.capturedAtMs = nowMs;
    runtime.lastEgressCaptureAtMs = nowMs;

    int delayMs = std::clamp(project_.outputs[outputIndex].outputDelayMs, 0, 5000);
    if (delayMs <= 0) {
      runtime.delayFrames.clear();
      return true;
    }

    OutputRuntime::CapturedFrame delayedFrame;
    delayedFrame.width = captureW;
    delayedFrame.height = captureH;
    delayedFrame.capturedAtMs = nowMs;
    delayedFrame.pixels = runtime.latestCapturedFrame.pixels;
    runtime.delayFrames.push_back(std::move(delayedFrame));

    size_t maxFrames = static_cast<size_t>(std::clamp(delayMs / 8 + 12, 16, 900));
    while (runtime.delayFrames.size() > maxFrames) {
      runtime.delayFrames.pop_front();
    }
    while (runtime.delayFrames.size() > 2 &&
           nowMs > runtime.delayFrames.front().capturedAtMs + static_cast<Uint64>(delayMs + 2500)) {
      runtime.delayFrames.pop_front();
    }
    return true;
  }

  const OutputRuntime::CapturedFrame* outputFrameForEgress(int outputIndex, OutputRuntime& runtime) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return nullptr;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    int delayMs = std::clamp(output.outputDelayMs, 0, 5000);
    if (delayMs > 0) {
      Uint64 nowMs = SDL_GetTicks();
      const OutputRuntime::CapturedFrame* selected = nullptr;
      for (const auto& frame : runtime.delayFrames) {
        if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
          continue;
        }
        if (nowMs >= frame.capturedAtMs + static_cast<Uint64>(delayMs)) {
          selected = &frame;
        }
      }
      if (selected) {
        return selected;
      }
    }
    if (runtime.latestCapturedFrame.width > 0 &&
        runtime.latestCapturedFrame.height > 0 &&
        !runtime.latestCapturedFrame.pixels.empty()) {
      return &runtime.latestCapturedFrame;
    }
    return nullptr;
  }

  const OutputRuntime::CapturedFrame* ensureBlackOutputFrameForEgress(
      int outputIndex,
      OutputRuntime& runtime,
      int width,
      int height) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return nullptr;
    }
    width = std::max(1, width);
    height = std::max(1, height);
    int orientationDegrees = normalizeOutputOrientationDegrees(project_.outputs[outputIndex].outputOrientationDegrees);
    if (orientationDegrees == 90 || orientationDegrees == 270) {
      std::swap(width, height);
    }
    size_t frameBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (runtime.latestCapturedFrame.pixels.size() != frameBytes) {
      runtime.latestCapturedFrame.pixels.assign(frameBytes, 0u);
    } else {
      std::fill(runtime.latestCapturedFrame.pixels.begin(), runtime.latestCapturedFrame.pixels.end(), 0u);
    }
    if (runtime.latestCapturedFrame.pixels.empty()) {
      return nullptr;
    }
    Uint64 nowMs = SDL_GetTicks();
    runtime.latestCapturedFrame.width = width;
    runtime.latestCapturedFrame.height = height;
    runtime.latestCapturedFrame.capturedAtMs = nowMs;
    runtime.lastEgressCaptureAtMs = nowMs;
    return &runtime.latestCapturedFrame;
  }

  void sendOutputStreamFrame(int outputIndex, int width, int height, double fpsHint) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      return;
    }
    const OutputRuntime::CapturedFrame* frame = outputFrameForEgress(outputIndex, *runtime);
    if ((!frame || frame->width <= 0 || frame->height <= 0 || frame->pixels.empty()) &&
        width > 0 && height > 0) {
      frame = ensureBlackOutputFrameForEgress(outputIndex, *runtime, width, height);
    }
    if (!frame || frame->width <= 0 || frame->height <= 0 || frame->pixels.empty()) {
      return;
    }

    std::string writerFailure = outputStreamWriterFailure(*runtime);
    if (!writerFailure.empty()) {
      runtime->streamStartFailed = true;
      runtime->streamRestartBlockedUntilMs = SDL_GetTicks() + 1500;
      setOutputHealthState(outputIndex, OutputHealthState::Error, writerFailure);
      stopOutputStreamRuntime(*runtime);
      if (outputIndex == project_.focusedOutputIndex) {
        triggerToast(writerFailure);
      }
      return;
    }

    if (!ensureOutputStreamRunning(outputIndex, frame->width, frame->height, fpsHint)) {
      return;
    }
    runtime = runtimeForOutput(outputIndex);
    bool processAlive = false;
#ifdef _WIN32
    processAlive = runtime && runtime->streamProcess.running();
#else
    processAlive = runtime && runtime->streamPid > 0;
#endif
    if (!processAlive || !runtime->streamWriter) {
      return;
    }
    if (runtime->lastStreamCaptureSentAtMs == frame->capturedAtMs) {
      return;
    }

    width = frame->width;
    height = frame->height;
    size_t stride = static_cast<size_t>(width) * 4u;
    size_t frameBytes = stride * static_cast<size_t>(height);
    if (runtime->streamFrameBuffer.size() != frameBytes) {
      runtime->streamFrameBuffer.resize(frameBytes);
    }
    if (runtime->streamFrameBuffer.empty()) {
      return;
    }
    OutputStreamPacket packet;
    packet.width = width;
    packet.height = height;
    packet.capturedAtMs = frame->capturedAtMs;
    packet.videoBytes = frame->pixels;
#ifndef _WIN32
    if (runtime->streamAudioPipeFd >= 0) {
      packet.audioSamples = collectOutputAudioFrameSamples(
        outputIndex,
        runtime->streamAudioReadSamplesByDeck,
        runtime->streamAudioSampleRemainder,
        fpsHint);
    }
#endif

    auto writer = runtime->streamWriter;
    bool writerFailed = false;
    std::string writerFailureReason;
    {
      std::lock_guard<std::mutex> lock(writer->mutex);
      writerFailed = writer->failed;
      writerFailureReason = writer->failureReason;
    }
    if (writerFailed) {
      runtime->streamStartFailed = true;
      runtime->streamRestartBlockedUntilMs = SDL_GetTicks() + 1500;
      setOutputHealthState(
        outputIndex,
        OutputHealthState::Error,
        writerFailureReason.empty() ? "stream write failed" : writerFailureReason);
      stopOutputStreamRuntime(*runtime);
      if (outputIndex == project_.focusedOutputIndex) {
        triggerToast("stream stopped");
      }
      return;
    }
    {
      std::lock_guard<std::mutex> lock(writer->mutex);
      writer->pendingPacket = std::move(packet);
      writer->hasPendingPacket = true;
      writer->packetsQueued += 1;
    }
    writer->cv.notify_one();
    runtime->lastStreamCaptureSentAtMs = frame->capturedAtMs;
  }

  void destroyOutputRuntime(OutputRuntime& runtime) {
    stopOutputStreamRuntime(runtime);
#if defined(DECKBOY_HAS_NDI_SDK)
    if (runtime.ndiKeySender && ndiApi_.sendDestroyFn) {
      ndiApi_.sendDestroyFn(runtime.ndiKeySender);
      runtime.ndiKeySender = nullptr;
    }
    runtime.ndiKeySenderName.clear();
    runtime.ndiKeyFrameBuffer.clear();
    if (runtime.ndiSender && ndiApi_.sendDestroyFn) {
      ndiApi_.sendDestroyFn(runtime.ndiSender);
      runtime.ndiSender = nullptr;
    }
    runtime.ndiSenderName.clear();
    runtime.ndiFrameBuffer.clear();
#endif
    runtime.ndiAudioReadSamplesByDeck.clear();
    runtime.ndiAudioSampleRemainder = 0.0;
    runtime.latestCapturedFrame = {};
    runtime.delayFrames.clear();
    for (auto& [sourceDeckIndex, texture] : runtime.layerBridgeTextures) {
      (void) sourceDeckIndex;
      if (texture) {
        SDL_DestroyTexture(texture);
      }
    }
    runtime.layerBridgeTextures.clear();
    runtime.layerBridgeTextureWidths.clear();
    runtime.layerBridgeTextureHeights.clear();
    runtime.layerBridgeTextureFormats.clear();
    runtime.layerBridgeFrameIndices.clear();
    runtime.layerBridgeCueKeys.clear();
    for (auto& [overlayKey, texture] : runtime.overlayBridgeTextures) {
      (void) overlayKey;
      if (texture) {
        SDL_DestroyTexture(texture);
      }
    }
    runtime.overlayBridgeTextures.clear();
    runtime.overlayBridgeTextureWidths.clear();
    runtime.overlayBridgeTextureHeights.clear();
    runtime.overlayBridgeTextureFormats.clear();
    runtime.overlayBridgeFrameIndices.clear();
    runtime.overlayBridgeCueKeys.clear();
    runtime.layerBridgeScratchPixels.clear();
#if DECKBOY_INPROC_DECODE
    for (auto& [gpuDeckIndex, texture] : runtime.layerGpuTextures) {
      (void) gpuDeckIndex;
      if (texture) {
        SDL_DestroyTexture(texture);  // wrapped texture first…
      }
    }
    runtime.layerGpuTextures.clear();
    for (auto& [gpuDeckIndex, texture2D] : runtime.layerGpuTexture2Ds) {
      (void) gpuDeckIndex;
      deckboy::libav::releaseD3D11Texture(texture2D);  // …then its backing D3D11 texture
    }
    runtime.layerGpuTexture2Ds.clear();
    runtime.layerGpuTextureSizes.clear();
    runtime.layerGpuFrameIndices.clear();
    runtime.gpuDownloadScratch = DecodedFrame{};
    if (runtime.rendererD3DDevice) {
      runtime.rendererD3DDevice = nullptr;
      // This device is going away — zero-copy decks bound to it must restart.
      scheduleDecodeDeviceReconcile();
    }
#endif
    if (runtime.compositorTexture) {
      SDL_DestroyTexture(runtime.compositorTexture);
      runtime.compositorTexture = nullptr;
    }
    runtime.compositorWidth = 0;
    runtime.compositorHeight = 0;
    runtime.compositorFormat = SDL_PIXELFORMAT_UNKNOWN;
    runtime.compositorBitDepth = 8;
    // Flush a final black frame before the window goes away so the physical
    // display — and capture dongles that latch the last signal they received —
    // clears to black instead of freezing on the last-shown frame. Only a
    // visible window has anything on screen; hidden/stream runtimes skip this.
    if (runtime.outputRenderer && runtime.outputWindow &&
        !(SDL_GetWindowFlags(runtime.outputWindow) & SDL_WINDOW_HIDDEN)) {
      SDL_SetRenderTarget(runtime.outputRenderer, nullptr);
      SDL_SetRenderDrawColor(runtime.outputRenderer, 0, 0, 0, 255);
      // Two swaps: double/triple-buffered chains need a couple of presents to
      // push black all the way to the scanout buffer the dongle samples.
      for (int i = 0; i < 2; ++i) {
        SDL_RenderClear(runtime.outputRenderer);
        SDL_RenderPresent(runtime.outputRenderer);
      }
    }
    if (runtime.outputRenderer) {
      SDL_DestroyRenderer(runtime.outputRenderer);
      runtime.outputRenderer = nullptr;
    }
    if (runtime.outputWindow) {
      SDL_DestroyWindow(runtime.outputWindow);
      runtime.outputWindow = nullptr;
    }
    runtime.pendingDisplayRuntimeRebuild = false;
    runtime.pendingDisplayMoveFullscreen = false;
    runtime.displayMoveRetryAtMs = 0;
    runtime.suppressRecoveryUntilMs = 0;
    runtime.recoveryPausedByEscape = false;
    runtime.fullscreenIntended = false;
    runtime.lastFullscreenRequestMs = 0;
    runtime.lastRecoveryAttemptMs = 0;
    runtime.healthState = OutputHealthState::Off;
    runtime.healthReason.clear();
    runtime.healthUpdatedAtMs = 0;
    runtime.fpsSampleStartedAtMs = 0;
    runtime.fpsFrameCount = 0;
    runtime.fpsMeasured = 0.0;
  }

#if DECKBOY_INPROC_DECODE
  // The D3D11 device in-process decode should target: the first enabled
  // window-type output's renderer, so zero-copy frames land on the device
  // that composites the program output. Falls back to any output runtime
  // with a hardware renderer; null means CPU-output decode.
  void* primaryOutputDecodeDevice() {
    void* anyDevice = nullptr;
    for (size_t i = 0; i < outputRuntimes_.size() && i < project_.outputs.size(); ++i) {
      OutputRuntime& runtime = outputRuntimes_[i];
      if (!runtime.outputRenderer || !runtime.rendererD3DDevice) {
        continue;
      }
      const OutputTarget& output = project_.outputs[i];
      if (output.enabled && output.outputType != "stream") {
        return runtime.rendererD3DDevice;
      }
      if (!anyDevice) {
        anyDevice = runtime.rendererD3DDevice;
      }
    }
    return anyDevice;
  }

  void scheduleDecodeDeviceReconcile() {
    decodeDeviceReconcilePending_ = true;
  }

  // After output topology changes, restart decode on decks whose zero-copy
  // frames are bound to the wrong (or a destroyed) device — and on decks
  // that decoded to CPU because no device existed yet. RGBA effects cues
  // never bind a device and their frames report RGBA, so they are skipped.
  void reconcileDecodeDevices() {
    if (!decodeDeviceReconcilePending_) {
      return;
    }
    decodeDeviceReconcilePending_ = false;
    void* wanted = primaryOutputDecodeDevice();
    for (size_t deckIndex = 0; deckIndex < deckRuntimes_.size(); ++deckIndex) {
      MediaEngine* engine = deckRuntimes_[deckIndex].mediaEngine.get();
      if (!engine || !engine->inprocDecodeActive()) {
        continue;
      }
      const Cue* cue = engine->activeCue();
      if (!cue || cue->kind != CueKind::Video) {
        continue;
      }
      if (engine->activeDecodeDevice() == wanted) {
        continue;
      }
      const DecodedFrame* frame = engine->currentFrame();
      if (frame && frame->format != FramePixelFormat::NV12) {
        continue;  // RGBA effects path — device is irrelevant
      }
      engine->refreshActiveCueRuntime();
    }
  }
#endif

  // The pixel raster of the display the operator currently has the program
  // output on: first enabled window-type output's live renderer size.
  // Patterns build at this size so their pixel-precision instruments stay
  // 1:1 with the physical display, tracking display switches automatically.
  std::pair<int, int> primaryOutputRasterSize() {
    std::pair<int, int> fallback {0, 0};
    for (size_t i = 0; i < outputRuntimes_.size() && i < project_.outputs.size(); ++i) {
      OutputRuntime& runtime = outputRuntimes_[i];
      if (!runtime.outputRenderer) {
        continue;
      }
      int rw = 0;
      int rh = 0;
      if (!SDL_GetCurrentRenderOutputSize(runtime.outputRenderer, &rw, &rh) || rw <= 0 || rh <= 0) {
        continue;
      }
      const OutputTarget& output = project_.outputs[i];
      if (output.enabled && output.outputType != "stream") {
        return {rw, rh};
      }
      if (fallback.first == 0) {
        fallback = {rw, rh};
      }
    }
    return fallback;
  }

  bool reopenDeckAudioOutput(int deckIndex, const std::string& preferredDeviceName) {
    Deck& deck = project_.decks[deckIndex];
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime) {
      return false;
    }

    std::string effectiveName;
    SDL_AudioStream* newMain = openMainAudioDevice(preferredDeviceName, effectiveName);
    if (!newMain) {
      return false;
    }

    SDL_AudioStream* oldStream = runtime->audioStream;
    runtime->audioStream = newMain;
    deck.audioOutputDeviceName = effectiveName;
    if (runtime->mediaEngine) {
      // Hot-swap the output device on the existing engine so a device change
      // mid-cue keeps playing instead of tearing the engine down.
      runtime->mediaEngine->setAudioDevice(newMain);
    } else {
      runtime->mediaEngine = std::make_unique<MediaEngine>(
        runtime->outputRenderer,
        runtime->audioStream,
        [this, deckIndex](const std::vector<std::int16_t>& samples) {
          pushDeckStreamAudioSamples(deckIndex, samples);
          // Capture samples for VU meter (only from focused deck)
          if (deckIndex == project_.focusedDeckIndex) {
            std::lock_guard<std::mutex> lock(vuSamplesMutex_);
            vuSamples_ = samples;
            vuSamplesUpdatedAtMs_ = SDL_GetTicks();
          }
        },
        [this](const Cue& cue) {
          return resolvedCueFilesystemPathString(cue, currentProjectFile_);
        },
#if DECKBOY_INPROC_DECODE
        // Deck engines decode zero-copy onto the program output's device.
        [this]() { return primaryOutputDecodeDevice(); },
#else
        MediaEngine::DecodeDeviceProvider {},
#endif
        // Patterns build pixel-mapped to the live program-output raster.
        [this]() { return primaryOutputRasterSize(); }
      );
    }
    if (oldStream) {
      SDL_DestroyAudioStream(oldStream);
    }
    return true;
  }

  bool ensureNdiRuntimeReady(std::string* errorMessage = nullptr) {
#if defined(DECKBOY_HAS_NDI_SDK)
    if (ndiApi_.ensureLoaded()) {
      return true;
    }
    if (errorMessage) {
      *errorMessage = ndiApi_.loadError;
    }
    return false;
#else
    if (errorMessage) {
      *errorMessage = "built without NDI SDK headers";
    }
    return false;
#endif
  }

  void applyOutputNdiSettings(int outputIndex, bool withToast) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      if (output.enabled) {
        setOutputHealthState(outputIndex, OutputHealthState::Error, "ndi runtime unavailable");
      }
      return;
    }

#if defined(DECKBOY_HAS_NDI_SDK)
    auto clearFillSender = [&]() {
      if (runtime->ndiSender && ndiApi_.sendDestroyFn) {
        ndiApi_.sendDestroyFn(runtime->ndiSender);
      }
      runtime->ndiSender = nullptr;
      runtime->ndiSenderName.clear();
      runtime->ndiFrameBuffer.clear();
      runtime->ndiAudioReadSamplesByDeck.clear();
      runtime->ndiAudioSampleRemainder = 0.0;
    };
    auto clearKeySender = [&]() {
      if (runtime->ndiKeySender && ndiApi_.sendDestroyFn) {
        ndiApi_.sendDestroyFn(runtime->ndiKeySender);
      }
      runtime->ndiKeySender = nullptr;
      runtime->ndiKeySenderName.clear();
      runtime->ndiKeyFrameBuffer.clear();
    };
    auto clearSenders = [&]() {
      clearFillSender();
      clearKeySender();
    };

    if (!output.ndiEnabled) {
      clearSenders();
      output.ndiKeyEnabled = false;
      if (output.enabled) {
        setOutputHealthState(outputIndex, OutputHealthState::Armed);
      }
      if (withToast) {
        triggerToast("ndi off");
      }
      return;
    }

    std::string loadError;
    if (!ensureNdiRuntimeReady(&loadError)) {
      output.ndiEnabled = false;
      output.ndiKeyEnabled = false;
      clearSenders();
      setOutputHealthState(outputIndex, OutputHealthState::Error,
                           loadError.empty() ? "ndi unavailable" : ("ndi unavailable: " + loadError));
      if (withToast) {
        triggerToast("ndi unavailable");
      }
      return;
    }

    if (trim(output.ndiSourceName).empty()) {
      output.ndiSourceName = defaultOutputNdiSourceName(output, outputIndex);
    }
    if (trim(output.ndiKeySourceName).empty()) {
      output.ndiKeySourceName = defaultOutputNdiKeySourceName(output, outputIndex);
    }

    clearSenders();

    NDIlib_send_create_t fillCreate {};
    fillCreate.p_ndi_name = output.ndiSourceName.c_str();
    fillCreate.p_groups = nullptr;
    fillCreate.clock_video = false;
    fillCreate.clock_audio = false;
    runtime->ndiSender = ndiApi_.sendCreateFn ? ndiApi_.sendCreateFn(&fillCreate) : nullptr;
    if (!runtime->ndiSender) {
      output.ndiEnabled = false;
      output.ndiKeyEnabled = false;
      setOutputHealthState(outputIndex, OutputHealthState::Error, "ndi sender failed");
      if (withToast) {
        triggerToast("ndi sender failed");
      }
      return;
    }
    runtime->ndiSenderName = output.ndiSourceName;
    primeOutputNdiAudioReadPositions(outputIndex, *runtime);
    runtime->ndiAudioSampleRemainder = 0.0;

    if (output.ndiKeyEnabled) {
      NDIlib_send_create_t keyCreate {};
      keyCreate.p_ndi_name = output.ndiKeySourceName.c_str();
      keyCreate.p_groups = nullptr;
      keyCreate.clock_video = false;
      keyCreate.clock_audio = false;
      runtime->ndiKeySender = ndiApi_.sendCreateFn ? ndiApi_.sendCreateFn(&keyCreate) : nullptr;
      if (!runtime->ndiKeySender) {
        output.ndiKeyEnabled = false;
      } else {
        runtime->ndiKeySenderName = output.ndiKeySourceName;
      }
    }
    if (output.enabled) {
      setOutputHealthState(outputIndex, OutputHealthState::Live);
    }

    if (withToast) {
      triggerToast("ndi: " + currentNdiOutputLabel());
    }
#else
    (void) output;
    (void) runtime;
    if (project_.outputs[outputIndex].enabled) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "ndi unsupported build");
    }
    if (withToast) {
      triggerToast("ndi unsupported build");
    }
#endif
  }

  void setFocusedOutputNdiEnabled(bool enabled) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return;
    }
    OutputTarget& output = focusedOutputMutable();
    if (output.ndiEnabled == enabled) {
      return;
    }
    // Enable path is operator-initiated by every path that reaches this
    // setter (UI toggle, hotkey, OSC command). Project file load writes
    // the field directly and never comes through here, so prompting here
    // never surprises a project being opened from disk.
    if (enabled && !ndiRuntimeAvailable()) {
      promptForNdiRuntime();
      return;
    }
    output.ndiEnabled = enabled;
    if (!output.ndiEnabled) {
      output.ndiKeyEnabled = false;
    }
    applyOutputNdiSettings(project_.focusedOutputIndex, true);
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleFocusedOutputNdi() {
    setFocusedOutputNdiEnabled(!focusedOutput().ndiEnabled);
  }

  // DeckLink enable wrapper — gates on deckLinkRuntimeAvailable() so the
  // dependency prompt fires from every operator-initiated path (settings
  // toggle, OSC command, hotkey if one ever exists). Project file load
  // writes the field directly and stays silent.
  void setFocusedOutputDeckLinkEnabled(bool enabled) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return;
    }
    OutputTarget& output = focusedOutputMutable();
    if (output.deckLinkEnabled == enabled) {
      return;
    }
    if (enabled && !deckLinkRuntimeAvailable()) {
      promptForDeckLinkRuntime();
      return;
    }
    output.deckLinkEnabled = enabled;
    if (!output.deckLinkEnabled) {
      auto& rt = outputRuntimes_[project_.focusedOutputIndex];
      shutdownOutputDeckLink(rt);
    }
    triggerToast(std::string("decklink: ") + (output.deckLinkEnabled ? "on" : "off"));
    markProjectDirty();
  }

  void toggleFocusedOutputDeckLink() {
    setFocusedOutputDeckLinkEnabled(!focusedOutput().deckLinkEnabled);
  }

  void setFocusedOutputNdiName(const std::string& requestedName) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return;
    }
    OutputTarget& output = focusedOutputMutable();
    std::string normalized = trim(requestedName);
    if (normalized.empty()) {
      normalized = defaultOutputNdiSourceName(output, project_.focusedOutputIndex);
    }
    if (output.ndiSourceName == normalized) {
      return;
    }
    output.ndiSourceName = normalized;
    if (output.ndiEnabled) {
      applyOutputNdiSettings(project_.focusedOutputIndex, true);
    } else {
      triggerToast("ndi name: " + output.ndiSourceName);
    }
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setFocusedOutputNdiKeyEnabled(bool enabled) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return;
    }
    OutputTarget& output = focusedOutputMutable();
    if (output.ndiKeyEnabled == enabled && (!enabled || output.ndiEnabled)) {
      return;
    }
    // Enabling the key also implicitly enables the main NDI sender below,
    // so the runtime check applies to both sides of the path.
    if (enabled && !ndiRuntimeAvailable()) {
      promptForNdiRuntime();
      return;
    }
    if (enabled) {
      output.ndiEnabled = true;
      if (trim(output.ndiKeySourceName).empty()) {
        output.ndiKeySourceName = defaultOutputNdiKeySourceName(output, project_.focusedOutputIndex);
      }
    }
    output.ndiKeyEnabled = enabled;
    applyOutputNdiSettings(project_.focusedOutputIndex, true);
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleFocusedOutputNdiKey() {
    setFocusedOutputNdiKeyEnabled(!focusedOutput().ndiKeyEnabled);
  }

  void setFocusedOutputNdiKeyName(const std::string& requestedName) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return;
    }
    OutputTarget& output = focusedOutputMutable();
    std::string normalized = trim(requestedName);
    if (normalized.empty()) {
      normalized = defaultOutputNdiKeySourceName(output, project_.focusedOutputIndex);
    }
    if (output.ndiKeySourceName == normalized) {
      return;
    }
    output.ndiKeySourceName = normalized;
    if (output.ndiEnabled && output.ndiKeyEnabled) {
      applyOutputNdiSettings(project_.focusedOutputIndex, true);
    } else {
      triggerToast("ndi key name: " + output.ndiKeySourceName);
    }
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void sendOutputNdiAudioFrame(int outputIndex, double fpsHint) {
#if defined(DECKBOY_HAS_NDI_SDK)
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->ndiSender || !ndiApi_.sendAudioInterleaved16sFn) {
      return;
    }
    std::vector<std::int16_t> samples = collectOutputAudioFrameSamples(
      outputIndex,
      runtime->ndiAudioReadSamplesByDeck,
      runtime->ndiAudioSampleRemainder,
      fpsHint);
    if (samples.empty()) {
      return;
    }
    constexpr int kChannels = 2;
    int sampleCount = static_cast<int>(samples.size() / static_cast<size_t>(kChannels));
    if (sampleCount <= 0) {
      return;
    }
    NDIlib_audio_frame_interleaved_16s_t frame {};
    frame.sample_rate = kAudioRate;
    frame.no_channels = kChannels;
    frame.no_samples = sampleCount;
    frame.timecode = NDIlib_send_timecode_synthesize;
    frame.reference_level = 0;
    frame.p_data = samples.data();
    ndiApi_.sendAudioInterleaved16sFn(runtime->ndiSender, &frame);
#else
    (void) outputIndex;
    (void) fpsHint;
#endif
  }

  void sendOutputNdiFrame(int outputIndex, OutputRuntime& outputRuntime, int width, int height, double fpsHint) {
#if defined(DECKBOY_HAS_NDI_SDK)
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size()) || !outputRuntime.outputRenderer) {
      return;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    if (!output.ndiEnabled) {
      return;
    }
    if (!outputRuntime.ndiSender || (output.ndiKeyEnabled && !outputRuntime.ndiKeySender)) {
      applyOutputNdiSettings(outputIndex, false);
      if (!outputRuntime.ndiSender || (output.ndiKeyEnabled && !outputRuntime.ndiKeySender)) {
        return;
      }
    }
    const OutputRuntime::CapturedFrame* frameCapture = outputFrameForEgress(outputIndex, outputRuntime);
    if (!frameCapture || frameCapture->width <= 0 || frameCapture->height <= 0 || frameCapture->pixels.empty()) {
      frameCapture = ensureBlackOutputFrameForEgress(outputIndex, outputRuntime, width, height);
    }
    if (!frameCapture || frameCapture->width <= 0 || frameCapture->height <= 0 || frameCapture->pixels.empty()) {
      return;
    }
    width = frameCapture->width;
    height = frameCapture->height;

    size_t stride = static_cast<size_t>(width) * 4u;
    size_t frameBytes = stride * static_cast<size_t>(height);
    if (outputRuntime.ndiFrameBuffer.size() != frameBytes) {
      outputRuntime.ndiFrameBuffer.resize(frameBytes);
    }
    if (outputRuntime.ndiFrameBuffer.empty()) {
      return;
    }

    std::memcpy(outputRuntime.ndiFrameBuffer.data(), frameCapture->pixels.data(), frameBytes);

    int frameRateN = 30000;
    int frameRateD = 1000;
    if (std::isfinite(fpsHint) && fpsHint > 1.0) {
      frameRateN = std::max(1, static_cast<int>(std::round(fpsHint * 1000.0)));
    }

    NDIlib_video_frame_v2_t frame {};
    frame.xres = width;
    frame.yres = height;
    frame.FourCC = NDIlib_FourCC_video_type_BGRA;
    frame.frame_rate_N = frameRateN;
    frame.frame_rate_D = frameRateD;
    frame.picture_aspect_ratio = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : (16.0f / 9.0f);
    frame.frame_format_type = NDIlib_frame_format_type_progressive;
    frame.timecode = NDIlib_send_timecode_synthesize;
    frame.p_data = outputRuntime.ndiFrameBuffer.data();
    frame.line_stride_in_bytes = static_cast<int>(stride);
    frame.p_metadata = nullptr;
    frame.timestamp = 0;
    ndiApi_.sendVideoFn(outputRuntime.ndiSender, &frame);

    if (output.ndiKeyEnabled && outputRuntime.ndiKeySender) {
      if (outputRuntime.ndiKeyFrameBuffer.size() != frameBytes) {
        outputRuntime.ndiKeyFrameBuffer.resize(frameBytes);
      }
      if (!outputRuntime.ndiKeyFrameBuffer.empty()) {
        for (size_t i = 0; i + 3 < outputRuntime.ndiFrameBuffer.size(); i += 4) {
          Uint8 a = outputRuntime.ndiFrameBuffer[i + 3];
          outputRuntime.ndiKeyFrameBuffer[i + 0] = a;
          outputRuntime.ndiKeyFrameBuffer[i + 1] = a;
          outputRuntime.ndiKeyFrameBuffer[i + 2] = a;
          outputRuntime.ndiKeyFrameBuffer[i + 3] = 255;
        }
        NDIlib_video_frame_v2_t keyFrame {};
        keyFrame.xres = width;
        keyFrame.yres = height;
        keyFrame.FourCC = NDIlib_FourCC_video_type_BGRA;
        keyFrame.frame_rate_N = frameRateN;
        keyFrame.frame_rate_D = frameRateD;
        keyFrame.picture_aspect_ratio = frame.picture_aspect_ratio;
        keyFrame.frame_format_type = NDIlib_frame_format_type_progressive;
        keyFrame.timecode = NDIlib_send_timecode_synthesize;
        keyFrame.p_data = outputRuntime.ndiKeyFrameBuffer.data();
        keyFrame.line_stride_in_bytes = static_cast<int>(stride);
        keyFrame.p_metadata = nullptr;
        keyFrame.timestamp = 0;
        ndiApi_.sendVideoFn(outputRuntime.ndiKeySender, &keyFrame);
      }
    }
    sendOutputNdiAudioFrame(outputIndex, fpsHint);
#else
    (void) outputIndex;
    (void) outputRuntime;
    (void) width;
    (void) height;
    (void) fpsHint;
#endif
  }

  void sendOutputDeckLinkFrame(int outputIndex, OutputRuntime& outputRuntime, int width, int height, double fpsHint) {
#if defined(DECKBOY_HAS_DECKLINK)
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size()) || !outputRuntime.outputRenderer) {
      return;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    if (!output.deckLinkEnabled) {
      return;
    }
    // Lazy-init DeckLink output
    if (!outputRuntime.deckLinkOutput) {
      outputRuntime.deckLinkOutput = std::make_unique<deckboy::platform::video::DeckLinkOutput>();
    }
    if (!outputRuntime.deckLinkOutput->isInitialized()) {
      auto mode = deckboy::platform::video::parseDeckLinkMode(output.deckLinkMode);
      if (!outputRuntime.deckLinkOutput->init(output.deckLinkDeviceId, mode, output.deckLink10Bit)) {
        return;
      }
    }
    const OutputRuntime::CapturedFrame* frameCapture = outputFrameForEgress(outputIndex, outputRuntime);
    if (!frameCapture || frameCapture->width <= 0 || frameCapture->height <= 0 || frameCapture->pixels.empty()) {
      frameCapture = ensureBlackOutputFrameForEgress(outputIndex, outputRuntime, width, height);
    }
    if (!frameCapture || frameCapture->width <= 0 || frameCapture->height <= 0 || frameCapture->pixels.empty()) {
      return;
    }
    int fw = frameCapture->width;
    int fh = frameCapture->height;
    int stride = fw * 4;
    outputRuntime.deckLinkOutput->sendFrame(frameCapture->pixels.data(), fw, fh, stride);
#else
    (void) outputIndex;
    (void) outputRuntime;
    (void) width;
    (void) height;
    (void) fpsHint;
#endif
  }

  void shutdownOutputDeckLink(OutputRuntime& outputRuntime) {
#if defined(DECKBOY_HAS_DECKLINK)
    if (outputRuntime.deckLinkOutput) {
      outputRuntime.deckLinkOutput->shutdown();
      outputRuntime.deckLinkOutput.reset();
    }
    outputRuntime.deckLinkFrameBuffer.clear();
#else
    (void) outputRuntime;
#endif
  }

  // ── Spout output ──────────────────────────────────────────────────────────
  void sendOutputSpoutFrame(int outputIndex, OutputRuntime& outputRuntime, int width, int height) {
#if defined(DECKBOY_HAS_SPOUT)
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    if (!output.spoutEnabled) {
      return;
    }
    // Lazy-init Spout sender
    if (!outputRuntime.spoutSender) {
      std::string name = trim(output.spoutSenderName);
      if (name.empty()) {
        name = "Deckboy Output " + std::to_string(outputIndex + 1);
      }
      outputRuntime.spoutSender = std::make_unique<deckboy::platform::video::SiphonSpoutSender>(name);
      if (!outputRuntime.spoutSender->init(width, height)) {
        std::cerr << "[Spout] Failed to init sender for output " << outputIndex << "\n";
        outputRuntime.spoutSender.reset();
        return;
      }
    }
    // Check if sender name changed
    std::string desiredName = trim(output.spoutSenderName);
    if (desiredName.empty()) {
      desiredName = "Deckboy Output " + std::to_string(outputIndex + 1);
    }
    if (outputRuntime.spoutSender->getName() != desiredName) {
      outputRuntime.spoutSender->setName(desiredName);
    }
    // Get the latest captured frame and send via Spout using the output texture
    if (!outputRuntime.outputRenderer) {
      return;
    }
    // Use the composited output texture from the render target
    const auto& frameCapture = outputRuntime.latestCapturedFrame;
    if (frameCapture.pixels.empty()) {
      return;
    }
    int fw = frameCapture.width;
    int fh = frameCapture.height;
    if (fw <= 0 || fh <= 0) {
      return;
    }
    // Create a temporary texture, upload the captured pixels, and send
    SDL_Texture* tempTex = deckboyCreateTexture(
      outputRuntime.outputRenderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STREAMING, fw, fh);
    if (!tempTex) {
      return;
    }
    void* texPixels = nullptr;
    int texPitch = 0;
    if (SDL_LockTexture(tempTex, nullptr, &texPixels, &texPitch)) {
      int stride = fw * 4;
      for (int y = 0; y < fh; ++y) {
        std::memcpy(
          static_cast<unsigned char*>(texPixels) + y * texPitch,
          frameCapture.pixels.data() + y * stride,
          static_cast<size_t>(stride));
      }
      SDL_UnlockTexture(tempTex);
      outputRuntime.spoutSender->sendFrame(tempTex);
    }
    SDL_DestroyTexture(tempTex);
#else
    (void) outputIndex;
    (void) outputRuntime;
    (void) width;
    (void) height;
#endif
  }

  void shutdownOutputSpout(OutputRuntime& outputRuntime) {
#if defined(DECKBOY_HAS_SPOUT)
    if (outputRuntime.spoutSender) {
      outputRuntime.spoutSender->shutdown();
      outputRuntime.spoutSender.reset();
    }
#else
    (void) outputRuntime;
#endif
  }

  int ndiConnectionCountForOutput(int outputIndex) const {
#if defined(DECKBOY_HAS_NDI_SDK)
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->ndiSender || !ndiApi_.sendConnectionsFn) {
      return 0;
    }
    return std::max(0, ndiApi_.sendConnectionsFn(runtime->ndiSender, 0));
#else
    (void) outputIndex;
    return 0;
#endif
  }

  int ndiKeyConnectionCountForOutput(int outputIndex) const {
#if defined(DECKBOY_HAS_NDI_SDK)
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->ndiKeySender || !ndiApi_.sendConnectionsFn) {
      return 0;
    }
    return std::max(0, ndiApi_.sendConnectionsFn(runtime->ndiKeySender, 0));
#else
    (void) outputIndex;
    return 0;
#endif
  }

  std::pair<int, int> fixedOutputRenderSize() const {
    int w = std::clamp(project_.outputRenderWidth, 320, 7680);
    int h = std::clamp(project_.outputRenderHeight, 180, 4320);
    return {w, h};
  }

  std::pair<int, int> displayNativeRenderSize(int displayIndex) const {
    int displayCount = deckboyGetNumVideoDisplays();
    if (displayCount <= 0) {
      return fixedOutputRenderSize();
    }
    int normalizedIndex = std::clamp(displayIndex, 0, displayCount - 1);

    SDL_DisplayMode desktopMode {};
    if (deckboyGetDesktopDisplayMode(normalizedIndex, &desktopMode) &&
        desktopMode.w > 0 && desktopMode.h > 0) {
      return {desktopMode.w, desktopMode.h};
    }

    SDL_Rect bounds {};
    if (deckboyGetDisplayBounds(normalizedIndex, &bounds) &&
        bounds.w > 0 && bounds.h > 0) {
      return {bounds.w, bounds.h};
    }
    return fixedOutputRenderSize();
  }

  int outputDisplayIndex(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return 0;
    }
    return std::max(0, project_.outputs[outputIndex].displayIndex);
  }

  std::pair<int, int> outputRenderSizeForOutput(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return fixedOutputRenderSize();
    }
    if (project_.outputFollowDisplay) {
      return displayNativeRenderSize(outputDisplayIndex(outputIndex));
    }
    return fixedOutputRenderSize();
  }

  std::pair<int, int> outputRenderSizeForDeck(int deckIndex) const {
    auto outputIndex = primaryOutputIndexForDeck(deckIndex);
    if (!outputIndex) {
      return fixedOutputRenderSize();
    }
    return outputRenderSizeForOutput(*outputIndex);
  }

  std::string outputResolutionLabelForOutput(int outputIndex) const {
    auto [w, h] = outputRenderSizeForOutput(outputIndex);
    return std::to_string(w) + "x" + std::to_string(h);
  }

  std::string outputResolutionLabel(int deckIndex) const {
    auto outputIndex = primaryOutputIndexForDeck(deckIndex);
    if (!outputIndex) {
      return outputResolutionLabelForOutput(project_.focusedOutputIndex);
    }
    return outputResolutionLabelForOutput(*outputIndex);
  }

  std::string outputSizingModeLabel() const {
    return project_.outputFollowDisplay ? "display native" : "fixed";
  }

  std::string formatRefreshRateLabel(double hz) const {
    if (!std::isfinite(hz) || hz <= 0.0) {
      return "auto";
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(hz < 100.0 ? 2 : 1) << hz;
    std::string text = ss.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text + " Hz";
  }

  std::string outputRefreshRateLabel() const {
    return formatRefreshRateLabel(project_.outputRefreshRateHz);
  }

  static bool isTenBitFormat(Uint32 format) {
    return format == SDL_PIXELFORMAT_ARGB2101010;
  }

  static int normalizeOutputBitDepthMode(int mode) {
    if (mode == 8 || mode == 10) {
      return mode;
    }
    return 0;
  }

  std::string outputBitDepthModeLabel() const {
    int mode = normalizeOutputBitDepthMode(project_.outputBitDepth);
    if (mode == 8) return "8-bit";
    if (mode == 10) return "10-bit";
    return "auto";
  }

  std::string outputBitDepthActiveLabelForOutput(int outputIndex) const {
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer) {
      return "n/a";
    }
    return std::to_string(runtime->compositorBitDepth) + "-bit";
  }

  std::string outputBitDepthActiveLabel(int deckIndex) const {
    auto outputIndex = primaryOutputIndexForDeck(deckIndex);
    return outputBitDepthActiveLabelForOutput(outputIndex ? *outputIndex : project_.focusedOutputIndex);
  }

  static bool rendererSupportsTextureFormat(SDL_Renderer* renderer, Uint32 format) {
    if (!renderer || format == SDL_PIXELFORMAT_UNKNOWN) {
      return false;
    }
    // SDL3: the supported-format list moved from SDL_RendererInfo to a
    // renderer property — an UNKNOWN-terminated SDL_PixelFormat array.
    const SDL_PixelFormat* formats = static_cast<const SDL_PixelFormat*>(
      SDL_GetPointerProperty(SDL_GetRendererProperties(renderer),
                             SDL_PROP_RENDERER_TEXTURE_FORMATS_POINTER, nullptr));
    if (!formats) {
      return false;
    }
    for (int i = 0; formats[i] != SDL_PIXELFORMAT_UNKNOWN; ++i) {
      if (static_cast<Uint32>(formats[i]) == format) {
        return true;
      }
    }
    return false;
  }

  Uint32 preferredCompositorFormat(SDL_Renderer* renderer) const {
    if (!renderer) {
      return SDL_PIXELFORMAT_RGBA32;
    }
    int mode = normalizeOutputBitDepthMode(project_.outputBitDepth);
    bool supportsArgb2101010 = rendererSupportsTextureFormat(renderer, SDL_PIXELFORMAT_ARGB2101010);
    if (mode == 10) {
      if (supportsArgb2101010) return SDL_PIXELFORMAT_ARGB2101010;
      return SDL_PIXELFORMAT_RGBA32;
    }
    if (mode == 0) {
      if (supportsArgb2101010) return SDL_PIXELFORMAT_ARGB2101010;
    }
    return SDL_PIXELFORMAT_RGBA32;
  }

  bool configureOutputCompositor(int outputIndex, int width = -1, int height = -1) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer) {
      return false;
    }
    int targetW = width;
    int targetH = height;
    if (targetW <= 0 || targetH <= 0) {
      int windowW = 0;
      int windowH = 0;
      if (runtime->outputWindow) {
        SDL_GetWindowSize(runtime->outputWindow, &windowW, &windowH);
      }
      if (project_.outputCanvasEnabled) {
        auto [canvasW, canvasH] = outputCanvasRenderSize();
        targetW = canvasW;
        targetH = canvasH;
      } else {
        if (windowW <= 0 || windowH <= 0) {
          auto [rasterW, rasterH] = outputRenderSizeForOutput(outputIndex);
          windowW = rasterW;
          windowH = rasterH;
        }
        targetW = windowW;
        targetH = windowH;
      }
    }
    targetW = std::max(1, targetW);
    targetH = std::max(1, targetH);

    Uint32 format = preferredCompositorFormat(runtime->outputRenderer);
    SDL_Texture* compositor = deckboyCreateTexture(
      runtime->outputRenderer,
      format,
      SDL_TEXTUREACCESS_TARGET,
      targetW,
      targetH
    );
    if (!compositor && format != SDL_PIXELFORMAT_RGBA32) {
      format = SDL_PIXELFORMAT_RGBA32;
      compositor = deckboyCreateTexture(
        runtime->outputRenderer,
        format,
        SDL_TEXTUREACCESS_TARGET,
        targetW,
        targetH
      );
    }
    if (!compositor) {
      return false;
    }
    SDL_SetTextureBlendMode(compositor, SDL_BLENDMODE_BLEND);

    if (runtime->compositorTexture) {
      SDL_DestroyTexture(runtime->compositorTexture);
    }
    runtime->compositorTexture = compositor;
    runtime->compositorWidth = targetW;
    runtime->compositorHeight = targetH;
    runtime->compositorFormat = format;
    runtime->compositorBitDepth = isTenBitFormat(format) ? 10 : 8;
    runtime->delayFrames.clear();
    runtime->latestCapturedFrame = {};
    return true;
  }

  void applyOutputBitDepthAllOutputs() {
    for (int outputIndex = 0; outputIndex < static_cast<int>(outputRuntimes_.size()); ++outputIndex) {
      configureOutputCompositor(outputIndex);
    }
  }

  void setOutputBitDepthMode(int mode) {
    int normalized = normalizeOutputBitDepthMode(mode);
    bool changed = normalized != normalizeOutputBitDepthMode(project_.outputBitDepth);
    project_.outputBitDepth = normalized;
    applyOutputBitDepthAllOutputs();
    triggerToast("video depth: " + outputBitDepthModeLabel() + " (" + outputBitDepthActiveLabelForOutput(project_.focusedOutputIndex) + ")");
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  std::pair<int, int> outputCanvasRenderSize() const {
    int w = std::clamp(project_.outputCanvasWidth, 320, 16384);
    int h = std::clamp(project_.outputCanvasHeight, 180, 16384);
    return {w, h};
  }

  void clampDeckCanvasViewToWindow(int deckIndex, int windowW, int windowH) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    if (!project_.outputCanvasEnabled) {
      deck.canvasViewX = 0;
      deck.canvasViewY = 0;
      return;
    }
    auto [canvasW, canvasH] = outputCanvasRenderSize();
    int maxX = std::max(0, canvasW - std::max(1, windowW));
    int maxY = std::max(0, canvasH - std::max(1, windowH));
    deck.canvasViewX = std::clamp(deck.canvasViewX, 0, maxX);
    deck.canvasViewY = std::clamp(deck.canvasViewY, 0, maxY);
  }

  void setOutputCanvasMode(bool enabled, int width = 0, int height = 0) {
    bool changed = false;
    if (enabled) {
      int targetW = width;
      int targetH = height;
      if (targetW <= 0 || targetH <= 0) {
        auto [nativeW, nativeH] = displayNativeRenderSize(outputDisplayIndex(project_.focusedOutputIndex));
        targetW = std::max(nativeW * 2, nativeW);
        targetH = nativeH;
      }
      targetW = std::clamp(targetW, 320, 16384);
      targetH = std::clamp(targetH, 180, 16384);
      changed = !project_.outputCanvasEnabled
        || project_.outputCanvasWidth != targetW
        || project_.outputCanvasHeight != targetH;
      project_.outputCanvasEnabled = true;
      project_.outputCanvasWidth = targetW;
      project_.outputCanvasHeight = targetH;
    } else {
      changed = project_.outputCanvasEnabled;
      project_.outputCanvasEnabled = false;
      for (auto& deck : project_.decks) {
        deck.canvasViewX = 0;
        deck.canvasViewY = 0;
      }
    }

    applyOutputBitDepthAllOutputs();
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      OutputRuntime* runtime = runtimeForOutput(outputIndex);
      if (!runtime || !runtime->outputWindow) {
        continue;
      }
      int hostDeckIndex = std::clamp(project_.outputs[outputIndex].hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
      int ww = 0;
      int wh = 0;
      SDL_GetWindowSize(runtime->outputWindow, &ww, &wh);
      clampDeckCanvasViewToWindow(hostDeckIndex, ww, wh);
    }

    std::string label = project_.outputCanvasEnabled
      ? ("canvas " + std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
      : "canvas off";
    triggerToast("video: " + label);
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  void setFocusedDeckCanvasView(int x, int y) {
    if (!project_.outputCanvasEnabled) {
      triggerToast("canvas mode off");
      return;
    }
    Deck& deck = focusedDeckMutable();
    deck.canvasViewX = std::max(0, x);
    deck.canvasViewY = std::max(0, y);
    int outputIndex = primaryOutputIndexForDeck(project_.focusedDeckIndex).value_or(project_.focusedOutputIndex);
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (runtime && runtime->outputWindow) {
      int ww = 0;
      int wh = 0;
      SDL_GetWindowSize(runtime->outputWindow, &ww, &wh);
      clampDeckCanvasViewToWindow(project_.focusedDeckIndex, ww, wh);
    }
    triggerToast("view: " + std::to_string(deck.canvasViewX) + "," + std::to_string(deck.canvasViewY));
    markProjectDirty();
  }

  void nudgeFocusedDeckCanvasView(int dx, int dy) {
    const Deck& deck = focusedDeck();
    setFocusedDeckCanvasView(deck.canvasViewX + dx, deck.canvasViewY + dy);
  }

  void setFocusedDeckWarpEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.warpEnabled == enabled) {
      return;
    }
    deck.warpEnabled = enabled;
    if (deck.warpEnabled) {
      triggerToast("warp on (" + toLower(warpModeLabel(deck.warpMode)) + ")");
    } else {
      triggerToast("warp off");
    }
    markProjectDirty();
  }

  void toggleFocusedDeckWarpEnabled() {
    setFocusedDeckWarpEnabled(!focusedDeck().warpEnabled);
  }

  bool setFocusedDeckWarpMode(const std::string& modeToken) {
    Deck& deck = focusedDeckMutable();
    std::string normalized = normalizeWarpMode(modeToken);
    if (deck.warpMode == normalized) {
      triggerToast("warp mode: " + toLower(warpModeLabel(normalized)));
      return false;
    }
    deck.warpMode = normalized;
    triggerToast("warp mode: " + toLower(warpModeLabel(deck.warpMode)));
    markProjectDirty();
    return true;
  }

  void cycleFocusedDeckWarpMode(int direction) {
    static constexpr std::array<const char*, 2> kModes {"linear", "perspective"};
    std::string current = normalizeWarpMode(focusedDeck().warpMode);
    int index = current == "perspective" ? 1 : 0;
    int step = direction < 0 ? -1 : 1;
    int next = (index + step + static_cast<int>(kModes.size())) % static_cast<int>(kModes.size());
    setFocusedDeckWarpMode(kModes[static_cast<size_t>(next)]);
  }

  void resetFocusedDeckWarp() {
    Deck& deck = focusedDeckMutable();
    deck.warpTopLeftX = 0.0f;
    deck.warpTopLeftY = 0.0f;
    deck.warpTopRightX = 0.0f;
    deck.warpTopRightY = 0.0f;
    deck.warpBottomRightX = 0.0f;
    deck.warpBottomRightY = 0.0f;
    deck.warpBottomLeftX = 0.0f;
    deck.warpBottomLeftY = 0.0f;
    deck.edgeBlendLeft = 0.0f;
    deck.edgeBlendRight = 0.0f;
    deck.edgeBlendTop = 0.0f;
    deck.edgeBlendBottom = 0.0f;
    triggerToast("warp/blend reset");
    markProjectDirty();
  }

  void adjustFocusedDeckWarpCorner(const std::string& cornerToken, float dx, float dy) {
    Deck& deck = focusedDeckMutable();
    std::string corner = toUpper(cornerToken);
    if (corner == "TL" || corner == "TOPLEFT") {
      deck.warpTopLeftX += dx;
      deck.warpTopLeftY += dy;
    } else if (corner == "TR" || corner == "TOPRIGHT") {
      deck.warpTopRightX += dx;
      deck.warpTopRightY += dy;
    } else if (corner == "BR" || corner == "BOTTOMRIGHT") {
      deck.warpBottomRightX += dx;
      deck.warpBottomRightY += dy;
    } else if (corner == "BL" || corner == "BOTTOMLEFT") {
      deck.warpBottomLeftX += dx;
      deck.warpBottomLeftY += dy;
    } else {
      return;
    }
    normalizeDeck(deck, project_.focusedDeckIndex);
    triggerToast("warp " + corner + " " + std::to_string(static_cast<int>(std::lround(dx))) + "," + std::to_string(static_cast<int>(std::lround(dy))));
    markProjectDirty();
  }

  void setFocusedDeckEdgeBlend(const std::string& edgeToken, float value) {
    Deck& deck = focusedDeckMutable();
    float v = std::clamp(value, 0.0f, 0.49f);
    std::string edge = toUpper(edgeToken);
    if (edge == "L" || edge == "LEFT") {
      deck.edgeBlendLeft = v;
    } else if (edge == "R" || edge == "RIGHT") {
      deck.edgeBlendRight = v;
    } else if (edge == "T" || edge == "TOP") {
      deck.edgeBlendTop = v;
    } else if (edge == "B" || edge == "BOTTOM") {
      deck.edgeBlendBottom = v;
    } else {
      return;
    }
    normalizeDeck(deck, project_.focusedDeckIndex);
    triggerToast("blend " + edge + " " + std::to_string(static_cast<int>(std::lround(v * 100.0f))) + "%");
    markProjectDirty();
  }

  std::vector<int> refreshChoicesForOutput(int outputIndex) const {
    std::vector<int> refreshes;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return refreshes;
    }
    int displayCount = deckboyGetNumVideoDisplays();
    if (displayCount <= 0) {
      return refreshes;
    }
    int displayIndex = std::clamp(outputDisplayIndex(outputIndex), 0, displayCount - 1);
    auto [targetW, targetH] = outputRenderSizeForOutput(outputIndex);

    int modeCount = 0;
    if (SDL_DisplayMode** modes =
          SDL_GetFullscreenDisplayModes(deckboyDisplayIdFromIndex(displayIndex), &modeCount)) {
      for (int modeIndex = 0; modeIndex < modeCount; ++modeIndex) {
        const SDL_DisplayMode& mode = *modes[modeIndex];
        if (mode.w != targetW || mode.h != targetH) {
          continue;
        }
        if (mode.refresh_rate > 0.0f) {
          refreshes.push_back(static_cast<int>(std::lround(mode.refresh_rate)));
        }
      }
      SDL_free(modes);
    }
    std::sort(refreshes.begin(), refreshes.end());
    refreshes.erase(std::unique(refreshes.begin(), refreshes.end()), refreshes.end());
    return refreshes;
  }

  bool selectDisplayModeForOutput(int outputIndex, SDL_DisplayMode& selectedMode) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    int displayCount = deckboyGetNumVideoDisplays();
    if (displayCount <= 0) {
      return false;
    }
    int displayIndex = std::clamp(outputDisplayIndex(outputIndex), 0, displayCount - 1);
    auto [targetW, targetH] = outputRenderSizeForOutput(outputIndex);
    double targetHz = project_.outputRefreshRateHz;

    SDL_DisplayMode desktopMode {};
    bool hasDesktop = deckboyGetDesktopDisplayMode(displayIndex, &desktopMode);
    if (targetHz <= 0.0 && hasDesktop && desktopMode.w == targetW && desktopMode.h == targetH) {
      selectedMode = desktopMode;
      return true;
    }

    bool found = false;
    SDL_DisplayMode best {};
    double bestScore = 1e9;

    int modeCount = 0;
    SDL_DisplayMode** modes =
      SDL_GetFullscreenDisplayModes(deckboyDisplayIdFromIndex(displayIndex), &modeCount);
    for (int modeIndex = 0; modes && modeIndex < modeCount; ++modeIndex) {
      const SDL_DisplayMode& mode = *modes[modeIndex];
      if (mode.w != targetW || mode.h != targetH) {
        continue;
      }

      double hz = mode.refresh_rate > 0.0f ? static_cast<double>(mode.refresh_rate) : 60.0;
      double score = 0.0;
      if (targetHz > 0.0) {
        score = std::abs(hz - targetHz);
      } else {
        // Auto: prefer desktop refresh if available, then highest refresh.
        if (hasDesktop && desktopMode.w == targetW && desktopMode.h == targetH && desktopMode.refresh_rate > 0.0f) {
          score = std::abs(hz - static_cast<double>(desktopMode.refresh_rate));
        } else {
          score = -hz;
        }
      }

      if (!found || score < bestScore) {
        found = true;
        best = mode;
        bestScore = score;
      }
    }
    if (modes) {
      SDL_free(modes);
    }

    if (!found) {
      return false;
    }
    selectedMode = best;
    return true;
  }

  bool enableOutputFullscreen(int outputIndex, bool withToast) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputWindow) {
      if (outputIndex >= 0 && outputIndex < static_cast<int>(project_.outputs.size()) &&
          project_.outputs[outputIndex].enabled) {
        setOutputHealthState(outputIndex, OutputHealthState::Error, "output window unavailable");
      }
      return false;
    }
    if (outputIndex >= 0 && outputIndex < static_cast<int>(project_.outputs.size()) &&
        project_.outputs[outputIndex].enabled) {
      setOutputHealthState(outputIndex, OutputHealthState::Recovering, "entering fullscreen");
    }
    runtime->recoveryPausedByEscape = false;
    runtime->fullscreenIntended = true;
    runtime->lastFullscreenRequestMs = SDL_GetTicks();

    // Exclusive fullscreen (real display-mode switch) only when the operator
    // explicitly asked for it: a fixed raster or a specific refresh rate.
    // In the default display-native case the "selected mode" is the desktop
    // mode anyway, so exclusive buys nothing over borderless fullscreen but
    // costs a mode switch — screen blanking, focus quirks, and unreliable
    // placement on mixed-DPI multi-monitor setups ("output frozen" /
    // "taking over the wrong screen" field reports).
    bool wantsExclusiveMode = !project_.outputFollowDisplay || project_.outputRefreshRateHz > 0.0;
    SDL_DisplayMode selectedMode {};
    if (wantsExclusiveMode && selectDisplayModeForOutput(outputIndex, selectedMode)) {
      // SDL3: exclusive fullscreen = set an explicit mode, then fullscreen.
      SDL_SetWindowFullscreenMode(runtime->outputWindow, &selectedMode);
      if (SDL_SetWindowFullscreen(runtime->outputWindow, true)) {
        runtime->lastRecoveryAttemptMs = runtime->lastFullscreenRequestMs;
        setOutputHealthState(outputIndex, OutputHealthState::Live);
        if (withToast) {
          triggerToast("big screen @" + formatRefreshRateLabel(selectedMode.refresh_rate));
        }
        return true;
      }
    }

    // SDL3: borderless fullscreen desktop = NULL mode, then fullscreen.
    SDL_SetWindowFullscreenMode(runtime->outputWindow, nullptr);
    if (SDL_SetWindowFullscreen(runtime->outputWindow, true)) {
      runtime->lastRecoveryAttemptMs = runtime->lastFullscreenRequestMs;
      setOutputHealthState(outputIndex, OutputHealthState::Live);
      if (withToast) {
        triggerToast("big screen");
      }
      return true;
    }
    if (outputIndex >= 0 && outputIndex < static_cast<int>(project_.outputs.size()) &&
        project_.outputs[outputIndex].enabled) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "fullscreen unavailable");
    }
    return false;
  }

  bool createDeckRuntime(int deckIndex) {
    Deck& deck = project_.decks[deckIndex];
    DeckRuntime& runtime = deckRuntimes_[deckIndex];
    destroyDeckRuntime(runtime);
    auto [targetW, targetH] = outputRenderSizeForDeck(deckIndex);
    targetW = std::max(1, targetW);
    targetH = std::max(1, targetH);

    // Hidden per-deck renderer used by the media engine decode/upload pipeline.
    std::string title = std::string("Deckboy Deck Runtime - ") + (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name);
    runtime.outputWindow = SDL_CreateWindow(
      title.c_str(),
      targetW,
      targetH,
      SDL_WINDOW_HIDDEN
    );
    if (!runtime.outputWindow) {
      return false;
    }
    applyDeckboyWindowIcon(runtime.outputWindow);

    runtime.outputRenderer = SDL_CreateRenderer(runtime.outputWindow, nullptr);
    if (!runtime.outputRenderer) {
      runtime.outputRenderer = SDL_CreateRenderer(runtime.outputWindow, SDL_SOFTWARE_RENDERER);
    }
    if (!runtime.outputRenderer) {
      destroyDeckRuntime(runtime);
      return false;
    }

    if (!reopenDeckAudioOutput(deckIndex, deck.audioOutputDeviceName)) {
      destroyDeckRuntime(runtime);
      return false;
    }

    return true;
  }

  bool createOutputRuntime(int outputIndex) {
    OutputTarget& output = project_.outputs[outputIndex];
    OutputRuntime& runtime = outputRuntimes_[outputIndex];
    destroyOutputRuntime(runtime);
    output.outputType = normalizeOutputType(output.outputType);
    auto [targetW, targetH] = outputRenderSizeForOutput(outputIndex);
    targetW = std::max(1, targetW);
    targetH = std::max(1, targetH);

    std::string label = output.name.empty() ? outputDefaultName(outputIndex) : output.name;
    bool streamType = output.outputType == "stream";
    bool windowOutputEnabled = output.enabled && !streamType;
    std::string title = std::string(kOutputTitle) + " - " + label + (streamType ? " [stream]" : "");
    Uint32 windowFlags = streamType
      ? SDL_WINDOW_HIDDEN
      : (SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    int windowX = SDL_WINDOWPOS_UNDEFINED;
    int windowY = SDL_WINDOWPOS_UNDEFINED;
    if (!streamType) {
      int displayCount = deckboyGetNumVideoDisplays();
      int displayIndex = displayCount > 0
        ? std::clamp(output.displayIndex, 0, displayCount - 1)
        : 0;
      windowX = SDL_WINDOWPOS_CENTERED_DISPLAY(deckboyDisplayIdFromIndex(displayIndex));
      windowY = SDL_WINDOWPOS_CENTERED_DISPLAY(deckboyDisplayIdFromIndex(displayIndex));
    }
    runtime.outputWindow = SDL_CreateWindow(
      title.c_str(),
      targetW,
      targetH,
      windowFlags
    );
    if (!runtime.outputWindow) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "window create failed");
      return false;
    }
    SDL_SetWindowPosition(runtime.outputWindow, windowX, windowY);
    applyDeckboyWindowIcon(runtime.outputWindow);

    runtime.outputRenderer = SDL_CreateRenderer(runtime.outputWindow, nullptr);
    if (!runtime.outputRenderer) {
      runtime.outputRenderer = SDL_CreateRenderer(runtime.outputWindow, SDL_SOFTWARE_RENDERER);
    }
    if (runtime.outputRenderer && !streamType) {
      // Program output stays vsynced to its display; stream-only outputs run
      // unthrottled (per-renderer vsync is an SDL3 runtime property).
      SDL_SetRenderVSync(runtime.outputRenderer, 1);
    }
    if (!runtime.outputRenderer) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "renderer create failed");
      destroyOutputRuntime(runtime);
      return false;
    }
#if DECKBOY_INPROC_DECODE
    // Cache the renderer's D3D11 device for zero-copy decode targeting and
    // per-frame device-match checks (null for the software renderer).
    runtime.rendererD3DDevice = deckboy::libav::rendererD3D11Device(runtime.outputRenderer);
    // Output topology changed — decks decoding zero-copy against a previous
    // output device must restart onto the new one (or CPU mode) next tick.
    scheduleDecodeDeviceReconcile();
#endif

    if (!configureOutputCompositor(outputIndex)) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "compositor init failed");
      destroyOutputRuntime(runtime);
      return false;
    }

    applyOutputDisplaySelection(outputIndex);
    applyOutputNdiSettings(outputIndex, false);
    if (output.enabled && !streamType) {
      if (!enableOutputFullscreen(outputIndex, false)) {
        setOutputHealthState(outputIndex, OutputHealthState::Error, "fullscreen unavailable");
      }
    } else if (output.enabled) {
      setOutputHealthState(outputIndex, OutputHealthState::Armed);
    } else {
      setOutputHealthState(outputIndex, OutputHealthState::Off);
    }
    return true;
  }

  bool recreateOutputRuntime(int outputIndex) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    if (outputIndex >= static_cast<int>(outputRuntimes_.size())) {
      return false;
    }
    return createOutputRuntime(outputIndex);
  }

  bool queueOutputDisplayRuntimeRebuild(int outputIndex, Uint64 now) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      return false;
    }
    runtime->pendingDisplayRuntimeRebuild = true;
    runtime->pendingDisplayMoveFullscreen = false;
    runtime->displayMoveRetryAtMs = now + 60;
    runtime->suppressRecoveryUntilMs = now + 1800;
    runtime->lastRecoveryAttemptMs = now;
    runtime->recoveryPausedByEscape = false;
    setOutputHealthState(outputIndex, OutputHealthState::Recovering, "switching display");
    return true;
  }

  bool rebuildDeckRuntimes() {
    normalizeProject(project_);
    for (auto& runtime : deckRuntimes_) {
      destroyDeckRuntime(runtime);
    }
    clearDeckStreamAudioBuffers();
    deckRuntimes_.clear();
    deckRuntimes_.resize(project_.decks.size());
    ensureTimecodeFollowerStateSize();

    for (size_t index = 0; index < project_.decks.size(); ++index) {
      if (!createDeckRuntime(static_cast<int>(index))) {
        return false;
      }
    }
    return true;
  }

  bool ensureOutputRuntimesSynced() {
    normalizeProject(project_);
    if (outputRuntimes_.size() == project_.outputs.size()) {
      return true;
    }
    
    // Growth: only create what's missing
    while (outputRuntimes_.size() < project_.outputs.size()) {
      outputRuntimes_.emplace_back();
      if (!createOutputRuntime(static_cast<int>(outputRuntimes_.size()) - 1)) {
        return false;
      }
    }
    
    // Shrinking: only destroy from the end
    while (outputRuntimes_.size() > project_.outputs.size()) {
      destroyOutputRuntime(outputRuntimes_.back());
      outputRuntimes_.pop_back();
    }
    
    return true;
  }

  // Legacy alias for compatibility during refactor
  bool rebuildOutputRuntimes() {
    return ensureOutputRuntimesSynced();
  }

  std::vector<std::string> outputAudioDeviceChoices() const {
    std::vector<std::string> names;
    names.push_back("");
    int deviceCount = 0;
    if (SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&deviceCount)) {
      for (int index = 0; index < deviceCount; ++index) {
        const char* name = SDL_GetAudioDeviceName(ids[index]);
        if (name && *name) {
          names.emplace_back(name);
        }
      }
      SDL_free(ids);
    }
    return names;
  }

  void cycleAudioOutputDevice(int direction) {
    auto choices = outputAudioDeviceChoices();
    if (choices.empty()) {
      return;
    }

    auto current = std::find(choices.begin(), choices.end(), focusedDeck().audioOutputDeviceName);
    int currentIndex = current == choices.end() ? 0 : static_cast<int>(std::distance(choices.begin(), current));
    int nextIndex = (currentIndex + direction + static_cast<int>(choices.size())) % static_cast<int>(choices.size());
    if (!reopenDeckAudioOutput(project_.focusedDeckIndex, choices[nextIndex])) {
      triggerToast("audio switch failed", {79, 98, 48, 230}, {223, 248, 185, 255});
      return;
    }
    triggerToast("audio: " + currentAudioOutputLabel());
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void restartLiveBrowserCueIfNeeded(int deckIndex) {
    const Cue* active = activeCuePtr(deckIndex);
    if (!active || active->kind != CueKind::Browser) {
      return;
    }
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->browserCueLive) {
      return;
    }
    startBrowserCue(deckIndex, *active);
  }

  void tickPendingOutputDisplayTransitions(Uint64 now) {
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      OutputTarget& output = project_.outputs[outputIndex];
      OutputRuntime* runtime = runtimeForOutput(outputIndex);
      if (!runtime) {
        continue;
      }
      if (runtime->pendingDisplayRuntimeRebuild) {
        if (!output.enabled || normalizeOutputType(output.outputType) != "window") {
          runtime->pendingDisplayRuntimeRebuild = false;
          runtime->displayMoveRetryAtMs = 0;
          continue;
        }
        if (now < runtime->displayMoveRetryAtMs) {
          continue;
        }
        runtime->pendingDisplayRuntimeRebuild = false;
        runtime->displayMoveRetryAtMs = 0;
        if (!recreateOutputRuntime(outputIndex)) {
          OutputRuntime* failedRuntime = runtimeForOutput(outputIndex);
          if (failedRuntime) {
            failedRuntime->suppressRecoveryUntilMs = now + 1200;
          }
          setOutputHealthState(outputIndex, OutputHealthState::Error, "display switch failed");
          triggerToast("display switch failed");
          continue;
        }
        OutputRuntime* rebuiltRuntime = runtimeForOutput(outputIndex);
        if (rebuiltRuntime) {
          rebuiltRuntime->pendingDisplayRuntimeRebuild = false;
          rebuiltRuntime->pendingDisplayMoveFullscreen = false;
          rebuiltRuntime->displayMoveRetryAtMs = 0;
          rebuiltRuntime->suppressRecoveryUntilMs = now + 900;
          rebuiltRuntime->lastRecoveryAttemptMs = now;
        }
        continue;
      }
      if (!runtime->pendingDisplayMoveFullscreen) {
        continue;
      }
      if (runtime->recoveryPausedByEscape) {
        // Operator escaped to windowed while a display move was pending —
        // never re-enter fullscreen behind their back. Explicit re-arm
        // (F / VIDEO OUTPUT ON / a new display pick) clears the escape flag.
        runtime->pendingDisplayMoveFullscreen = false;
        continue;
      }
      if (!output.enabled || normalizeOutputType(output.outputType) != "window" ||
          !runtime->outputWindow) {
        runtime->pendingDisplayMoveFullscreen = false;
        continue;
      }
      if (now < runtime->displayMoveRetryAtMs) {
        continue;
      }

      SDL_WindowFlags flags = SDL_GetWindowFlags(runtime->outputWindow);
      bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
      if (fullscreen) {
        runtime->displayMoveRetryAtMs = now + 120;
        continue;
      }

      runtime->pendingDisplayMoveFullscreen = false;
      bool controlHadFocus = controlWindow_ && SDL_GetKeyboardFocus() == controlWindow_;
      applyOutputDisplaySelection(outputIndex, false, false);
      if (enableOutputFullscreen(outputIndex, false)) {
        runtime->suppressRecoveryUntilMs = now + 500;
        setOutputHealthState(outputIndex, OutputHealthState::Live);
      } else {
        runtime->suppressRecoveryUntilMs = now + 1200;
        setOutputHealthState(outputIndex, OutputHealthState::Error, "fullscreen unavailable");
      }
      if (controlHadFocus) {
        SDL_RaiseWindow(controlWindow_);  // don't strand the operator's keyboard on the output
      }
    }
  }

  // --- Screen identify overlay ---------------------------------------------
  // Shows a numbered badge window on every connected display for a couple of
  // seconds so the operator can match settings indices to physical screens.

  void closeDisplayIdentify() {
    for (auto& iw : identifyWindows_) {
      if (iw.renderer) SDL_DestroyRenderer(iw.renderer);
      if (iw.window) SDL_DestroyWindow(iw.window);
    }
    identifyWindows_.clear();
    identifyUntilMs_ = 0;
  }

  void showDisplayIdentify(Uint64 durationMs = 2500) {
    closeDisplayIdentify();
    bool controlHadFocus = controlWindow_ && SDL_GetKeyboardFocus() == controlWindow_;
    int displayCount = deckboyGetNumVideoDisplays();
    for (int di = 0; di < displayCount; ++di) {
      SDL_Rect bounds;
      if (!deckboyGetDisplayBounds(di, &bounds)) {
        continue;
      }
      int w = std::min(360, std::max(220, bounds.w / 6));
      int h = std::min(220, std::max(140, bounds.h / 6));
      SDL_Window* win = SDL_CreateWindow("Deckboy Display Identify",
        w, h,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_UTILITY);
      if (!win) {
        continue;
      }
      SDL_SetWindowPosition(win,
        bounds.x + (bounds.w - w) / 2, bounds.y + (bounds.h - h) / 2);
      SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
      if (!ren) {
        SDL_DestroyWindow(win);
        continue;
      }
      SDL_SetRenderVSync(ren, 1);
      identifyWindows_.push_back({win, ren, di});
    }
    identifyUntilMs_ = SDL_GetTicks() + durationMs;
    if (controlHadFocus) {
      SDL_RaiseWindow(controlWindow_);  // creating the badges must not strand typing
    }
  }

  void renderDisplayIdentify() {
    if (identifyWindows_.empty()) {
      return;
    }
    if (SDL_GetTicks() > identifyUntilMs_) {
      closeDisplayIdentify();
      return;
    }
    // drawText helpers are bound to controlRenderer_ textures — render text
    // for these per-display renderers straight from TTF surfaces instead.
    auto blitText = [](SDL_Renderer* ren, TTF_Font* font, const std::string& text,
                       SDL_Color color, int centerX, int centerY, double maxScale, int maxW) {
      if (!font || text.empty()) return;
      SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), 0, color);
      if (!surf) return;
      if (SDL_Texture* tex = deckboyCreateTextureFromSurface(ren, surf)) {
        double scale = std::min(maxScale, static_cast<double>(maxW) / std::max(1, surf->w));
        scale = std::max(0.5, scale);
        int dw = static_cast<int>(surf->w * scale);
        int dh = static_cast<int>(surf->h * scale);
        SDL_Rect dst {centerX - dw / 2, centerY - dh / 2, dw, dh};
        SDL_RenderTexture(ren, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
      }
      SDL_DestroySurface(surf);
    };
    for (auto& iw : identifyWindows_) {
      if (!iw.renderer || !iw.window) {
        continue;
      }
      int winW = 0, winH = 0;
      SDL_GetWindowSize(iw.window, &winW, &winH);
      SDL_SetRenderDrawColor(iw.renderer, pal.deep.r, pal.deep.g, pal.deep.b, 255);
      SDL_RenderClear(iw.renderer);
      SDL_SetRenderDrawColor(iw.renderer, pal.light.r, pal.light.g, pal.light.b, 255);
      for (int border = 0; border < 3; ++border) {
        SDL_Rect frame {border, border, winW - border * 2, winH - border * 2};
        SDL_RenderRect(iw.renderer, &frame);
      }
      blitText(iw.renderer, fontLarge_, std::to_string(iw.displayIndex + 1), pal.light,
               winW / 2, winH / 2 - 16, 3.0, winW - 40);
      const char* dName = deckboyGetDisplayName(iw.displayIndex);
      std::string info = dName && *dName ? std::string(dName) : std::string();
      SDL_Rect dispBounds;
      if (deckboyGetDisplayBounds(iw.displayIndex, &dispBounds)) {
        info += (info.empty() ? "" : "  ") + std::to_string(dispBounds.w)
              + "x" + std::to_string(dispBounds.h);
      }
      blitText(iw.renderer, fontSmall_, info, pal.mid,
               winW / 2, winH - 26, 1.0, winW - 24);
      SDL_RenderPresent(iw.renderer);
    }
  }

  // Record the SDL display name for the output's current displayIndex.
  // Called on explicit operator display choices so topology changes can
  // re-match the intended physical display by name later.
  void recordOutputDisplayName(OutputTarget& output) {
    const char* name = deckboyGetDisplayName(output.displayIndex);
    output.displayName = (name && *name) ? name : std::string();
  }

  // Resolve which SDL display index an output should target right now.
  // SDL display indices are enumeration-order-dependent and shuffle on
  // hot-plug/reboot, so the persisted displayName is authoritative: keep the
  // current index if its name still matches, otherwise find the display
  // carrying that name. Falls back to a clamped index WITHOUT erasing
  // displayName — if the intended display comes back later, the next
  // resolve re-matches it.
  int resolveOutputDisplayIndex(const OutputTarget& output, int displayCount) const {
    if (displayCount <= 0) {
      return 0;
    }
    int clamped = std::clamp(output.displayIndex, 0, displayCount - 1);
    if (output.displayName.empty()) {
      return clamped;
    }
    const char* currentName = deckboyGetDisplayName(clamped);
    if (currentName && output.displayName == currentName) {
      return clamped;
    }
    for (int index = 0; index < displayCount; ++index) {
      const char* name = deckboyGetDisplayName(index);
      if (name && output.displayName == name) {
        return index;
      }
    }
    return clamped;
  }

  void applyOutputDisplaySelection(int outputIndex,
                                   bool allowFullscreenTransition = false,
                                   bool reenterFullscreenAfterMove = false) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    output.outputType = normalizeOutputType(output.outputType);
    bool streamType = output.outputType == "stream";
    bool windowOutputEnabled = output.enabled && !streamType;
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputWindow) {
      return;
    }
    Uint64 now = SDL_GetTicks();

    int displayCount = deckboyGetNumVideoDisplays();
    bool haveDisplayBounds = false;
    SDL_Rect bounds {};
    if (displayCount > 0) {
      output.displayIndex = resolveOutputDisplayIndex(output, displayCount);
      haveDisplayBounds = (deckboyGetDisplayBounds(output.displayIndex, &bounds));
    } else {
      output.displayIndex = 0;
    }

    auto [targetW, targetH] = outputRenderSizeForOutput(outputIndex);
    targetW = std::max(1, targetW);
    targetH = std::max(1, targetH);

    SDL_WindowFlags flags = SDL_GetWindowFlags(runtime->outputWindow);
    bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    if (!streamType && fullscreen && allowFullscreenTransition) {
      SDL_SetWindowFullscreen(runtime->outputWindow, false);
      flags = SDL_GetWindowFlags(runtime->outputWindow);
      fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    }

    // Only move/resize once SDL confirms the window is no longer fullscreen.
    // Trying to reposition a still-fullscreen output window is unstable on
    // some window managers and especially noisy on Windows multi-monitor hops.
    bool canApplyGeometry = !fullscreen;
    if (windowOutputEnabled && reenterFullscreenAfterMove) {
      runtime->pendingDisplayMoveFullscreen = true;
      runtime->displayMoveRetryAtMs = now + (canApplyGeometry ? 0 : 180);
      runtime->suppressRecoveryUntilMs = now + 1500;
      runtime->lastRecoveryAttemptMs = now;
      setOutputHealthState(outputIndex, OutputHealthState::Recovering, "switching display");
    }
    if (streamType || canApplyGeometry) {
      SDL_SetWindowSize(runtime->outputWindow, targetW, targetH);
    }
    if (!streamType && haveDisplayBounds && canApplyGeometry) {
      int x = bounds.x + std::max(0, (bounds.w - targetW) / 2) + outputIndex * 20;
      int y = bounds.y + std::max(0, (bounds.h - targetH) / 2) + outputIndex * 20;
      if (targetW > bounds.w) x = bounds.x + 20 + outputIndex * 20;
      if (targetH > bounds.h) y = bounds.y + 20 + outputIndex * 20;
      SDL_SetWindowPosition(runtime->outputWindow, x, y);
    }

    if (windowOutputEnabled) {
      SDL_ShowWindow(runtime->outputWindow);
      SDL_RaiseWindow(runtime->outputWindow);
    } else {
      SDL_HideWindow(runtime->outputWindow);
    }
    configureOutputCompositor(outputIndex);

    if (!project_.decks.empty()) {  // std::clamp(x, 0, -1) is UB when decks is empty
      int hostDeckIndex = std::clamp(output.hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
      project_.decks[hostDeckIndex].outputDisplayIndex = output.displayIndex;
    }
    std::string titleLabel = output.name.empty() ? outputDefaultName(outputIndex) : output.name;
    std::string title = std::string(kOutputTitle) + " - " + titleLabel;
    SDL_SetWindowTitle(runtime->outputWindow, title.c_str());
  }

  void applyOutputDisplaySelectionAllOutputs(bool restartLiveBrowsers,
                                             bool allowFullscreenTransition = false) {
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      applyOutputDisplaySelection(outputIndex,
                                 allowFullscreenTransition,
                                 allowFullscreenTransition);
    }
    if (restartLiveBrowsers) {
      for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
        restartLiveBrowserCueIfNeeded(deckIndex);
      }
    }
  }

  void setOutputSizingModeDisplayNative() {
    bool changed = !project_.outputFollowDisplay;
    project_.outputFollowDisplay = true;
    applyOutputDisplaySelectionAllOutputs(true, true);
    triggerToast("video mode: native (" + currentDisplayLabel() + ")");
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  void setOutputSizingModeFixed(int width, int height) {
    int w = std::clamp(width, 320, 7680);
    int h = std::clamp(height, 180, 4320);
    bool changed = project_.outputFollowDisplay
      || project_.outputRenderWidth != w
      || project_.outputRenderHeight != h;
    project_.outputFollowDisplay = false;
    project_.outputRenderWidth = w;
    project_.outputRenderHeight = h;
    applyOutputDisplaySelectionAllOutputs(true, true);
    triggerToast("video mode: fixed " + std::to_string(w) + "x" + std::to_string(h));
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  void sizeFocusedOutputToSelectedDisplay() {
    applyOutputDisplaySelection(project_.focusedOutputIndex, true, true);
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (primaryOutputIndexForDeck(deckIndex) && *primaryOutputIndexForDeck(deckIndex) == project_.focusedOutputIndex) {
        restartLiveBrowserCueIfNeeded(deckIndex);
      }
    }
    triggerToast("output sized: " + outputResolutionLabelForOutput(project_.focusedOutputIndex));
    playUiSound(UiSoundEffect::Toggle);
  }

  void setOutputRefreshRate(double hz) {
    double normalized = (!std::isfinite(hz) || hz <= 0.0) ? 0.0 : std::clamp(hz, 1.0, 240.0);
    bool changed = std::abs(project_.outputRefreshRateHz - normalized) > 0.0001;
    project_.outputRefreshRateHz = normalized;
    applyOutputDisplaySelectionAllOutputs(false, true);
    triggerToast("video refresh: " + outputRefreshRateLabel());
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  void cycleOutputRefreshRate(int direction) {
    auto choices = refreshChoicesForOutput(project_.focusedOutputIndex);
    if (choices.empty()) {
      triggerToast("no refresh choices for raster");
      return;
    }

    int current = project_.outputRefreshRateHz > 0.0
      ? static_cast<int>(std::lround(project_.outputRefreshRateHz))
      : 0;

    int currentIndex = -1;
    for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
      if (choices[i] == current) {
        currentIndex = i;
        break;
      }
    }

    int nextIndex = 0;
    if (currentIndex >= 0) {
      nextIndex = (currentIndex + direction + static_cast<int>(choices.size())) % static_cast<int>(choices.size());
    } else if (current > 0) {
      int nearest = 0;
      int bestDelta = std::abs(choices[0] - current);
      for (int i = 1; i < static_cast<int>(choices.size()); ++i) {
        int delta = std::abs(choices[i] - current);
        if (delta < bestDelta) {
          bestDelta = delta;
          nearest = i;
        }
      }
      nextIndex = (nearest + direction + static_cast<int>(choices.size())) % static_cast<int>(choices.size());
    } else {
      nextIndex = direction >= 0 ? 0 : static_cast<int>(choices.size()) - 1;
    }

    setOutputRefreshRate(static_cast<double>(choices[nextIndex]));
  }

  void cycleOutputDisplay(int direction) {
    int displayCount = deckboyGetNumVideoDisplays();
    if (displayCount <= 0) {
      return;
    }
    OutputTarget& output = focusedOutputMutable();
    output.displayIndex = (output.displayIndex + direction + displayCount) % displayCount;
    recordOutputDisplayName(output);
    bool autoSwitchedToNative = false;
    if (!project_.outputFollowDisplay) {
      project_.outputFollowDisplay = true;
      autoSwitchedToNative = true;
    }
    Uint64 now = SDL_GetTicks();
    bool queuedRuntimeRebuild = false;
    if (output.enabled && normalizeOutputType(output.outputType) == "window") {
      queuedRuntimeRebuild = queueOutputDisplayRuntimeRebuild(project_.focusedOutputIndex, now);
      if (!queuedRuntimeRebuild) {
        setOutputHealthState(project_.focusedOutputIndex, OutputHealthState::Error, "display switch failed");
        triggerToast("display switch failed");
        return;
      }
    } else {
      applyOutputDisplaySelection(project_.focusedOutputIndex, true, true);
    }
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (primaryOutputIndexForDeck(deckIndex) && *primaryOutputIndexForDeck(deckIndex) == project_.focusedOutputIndex) {
        restartLiveBrowserCueIfNeeded(deckIndex);
      }
    }
    const char* labelPtr = deckboyGetDisplayName(output.displayIndex);
    std::string label = (labelPtr && *labelPtr) ? labelPtr : "";
    triggerToast("display: "
      + (label.empty() ? std::to_string(output.displayIndex + 1) : label)
      + "  " + outputResolutionLabelForOutput(project_.focusedOutputIndex)
      + (autoSwitchedToNative ? "  auto native" : ""));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool setOutputDisplayIndex(int index) {
    int displayCount = deckboyGetNumVideoDisplays();
    if (displayCount <= 0 || index < 0 || index >= displayCount) {
      return false;
    }
    OutputTarget& output = focusedOutputMutable();
    output.displayIndex = index;
    recordOutputDisplayName(output);
    bool autoSwitchedToNative = false;
    if (!project_.outputFollowDisplay) {
      project_.outputFollowDisplay = true;
      autoSwitchedToNative = true;
    }
    Uint64 now = SDL_GetTicks();
    bool queuedRuntimeRebuild = false;
    if (output.enabled && normalizeOutputType(output.outputType) == "window") {
      queuedRuntimeRebuild = queueOutputDisplayRuntimeRebuild(project_.focusedOutputIndex, now);
      if (!queuedRuntimeRebuild) {
        setOutputHealthState(project_.focusedOutputIndex, OutputHealthState::Error, "display switch failed");
        triggerToast("display switch failed");
        return false;
      }
    } else {
      applyOutputDisplaySelection(project_.focusedOutputIndex, true, true);
    }
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (primaryOutputIndexForDeck(deckIndex) && *primaryOutputIndexForDeck(deckIndex) == project_.focusedOutputIndex) {
        restartLiveBrowserCueIfNeeded(deckIndex);
      }
    }
    triggerToast("display: " + currentDisplayLabel() + "  "
      + outputResolutionLabelForOutput(project_.focusedOutputIndex)
      + (autoSwitchedToNative ? "  auto native" : ""));
    markProjectDirty();
    return true;
  }

  void refreshDisplayTopology(bool withToast = false) {
    int displayCount = deckboyGetNumVideoDisplays();
    if (displayCount <= 0) {
      // Zero displays is a transient state (RDP handoff, driver reset).
      // Don't mutate persisted display targets over it — when displays
      // return, name matching restores the intended routing.
      if (withToast) {
        triggerToast("display scan: no displays reported");
      }
      return;
    }

    bool changed = false;
    for (auto& output : project_.outputs) {
      int resolved = resolveOutputDisplayIndex(output, displayCount);
      if (output.displayIndex != resolved) {
        output.displayIndex = resolved;
        changed = true;
      }
    }
    if (changed) {
      markProjectDirty();
    }

    // Heal outputs individually via the recovery path, which honors
    // recoveryPausedByEscape and fullscreenIntended. A topology change must
    // never slam an operator-escaped output back to fullscreen — the old
    // applyOutputDisplaySelectionAllOutputs(true, true) here did exactly
    // that, and also churned every enabled output through a fullscreen
    // exit/re-enter even when its display was unaffected.
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      recoverWindowOutputIfNeeded(outputIndex, false);
    }
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      restartLiveBrowserCueIfNeeded(deckIndex);
    }
    if (withToast) {
      triggerToast("display scan: " + std::to_string(displayCount) + " detected");
      playUiSound(UiSoundEffect::Toggle);
    }
  }

  bool setAudioOutputDevice(const std::string& deviceName) {
    if (!reopenDeckAudioOutput(project_.focusedDeckIndex, deviceName)) {
      triggerToast("audio switch failed", {79, 98, 48, 230}, {223, 248, 185, 255});
      return false;
    }
    triggerToast("audio: " + currentAudioOutputLabel());
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool recoverWindowOutputIfNeeded(int outputIndex, bool withToast = false) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    if (!output.enabled || normalizeOutputType(output.outputType) != "window") {
      return false;
    }
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputWindow) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "output window unavailable");
      return false;
    }
    if (runtime->recoveryPausedByEscape) {
      setOutputHealthState(outputIndex, OutputHealthState::Armed, "escaped to windowed");
      return false;
    }
    Uint64 now = SDL_GetTicks();
    if (runtime->pendingDisplayRuntimeRebuild ||
        runtime->pendingDisplayMoveFullscreen ||
        now < runtime->suppressRecoveryUntilMs) {
      return false;
    }

    SDL_WindowFlags flags = SDL_GetWindowFlags(runtime->outputWindow);
    bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    bool hidden = (flags & SDL_WINDOW_HIDDEN) != 0;
    bool minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;

    int displayCount = deckboyGetNumVideoDisplays();
    int targetDisplay = displayCount > 0
      ? std::clamp(output.displayIndex, 0, displayCount - 1)
      : 0;
    int windowDisplay = deckboyGetWindowDisplayIndex(runtime->outputWindow);
    // SDL can report -1 transiently during fullscreen transitions.
    // Treat unknown index as non-actionable to avoid recovery loops.
    // Deliberately NOT checked while fullscreen: SDL's reported display for a
    // fullscreen window can persistently disagree with the target on
    // mixed-DPI multi-monitor setups, and acting on it here produced an
    // exit/move/re-enter/raise fight every recovery tick (v0.76.19
    // regression: focus-stealing churn, "trying to take over the wrong
    // screen"). A stable fullscreen window is left alone; wrong placement is
    // for the operator to correct via an explicit display pick.
    bool wrongDisplay = false;
    if (displayCount > 0 && !fullscreen && windowDisplay >= 0) {
      wrongDisplay = windowDisplay != targetDisplay;
    }

    // Re-assert fullscreen whenever the user intended it and the WM dropped it.
    // fullscreenIntended is set on explicit toggle-on and cleared on toggle-off/escape.
    bool needsFullscreenRecovery = (!fullscreen) && runtime->fullscreenIntended;
    bool needsRecovery = hidden || minimized || wrongDisplay || needsFullscreenRecovery;
    if (!needsRecovery) {
      return false;
    }
    if (runtime->lastRecoveryAttemptMs > 0 &&
        (now - runtime->lastRecoveryAttemptMs) < 1200) {
      return false;
    }
    // Strike backoff: a healthy output needs recovery rarely. If we're here
    // repeatedly in a short window, recovery itself is the problem (each
    // pass raises the output window, stealing keyboard focus from the
    // control window) — stop fighting and tell the operator.
    std::string recoverReason = hidden ? "window hidden"
      : (minimized ? "window minimized"
      : (wrongDisplay ? "wrong display"
      : "fullscreen dropped"));
    constexpr int kMaxRecoveryStrikes = 3;
    constexpr Uint64 kRecoveryStrikeWindowMs = 15000;
    if (runtime->recoveryStrikeWindowStartMs == 0 ||
        now - runtime->recoveryStrikeWindowStartMs > kRecoveryStrikeWindowMs) {
      runtime->recoveryStrikeWindowStartMs = now;
      runtime->recoveryStrikeCount = 0;
    }
    if (++runtime->recoveryStrikeCount > kMaxRecoveryStrikes) {
      runtime->suppressRecoveryUntilMs = now + 30000;
      runtime->recoveryStrikeWindowStartMs = 0;
      runtime->recoveryStrikeCount = 0;
      // Name the trigger — "unstable" alone hides what keeps firing.
      setOutputHealthState(outputIndex, OutputHealthState::Error,
                           "output unstable (" + recoverReason + ") - recovery paused 30s");
      triggerToast("output " + std::to_string(outputIndex + 1)
                   + " unstable (" + recoverReason + "): recovery paused 30s");
      return false;
    }
    runtime->lastRecoveryAttemptMs = now;
    setOutputHealthState(outputIndex, OutputHealthState::Recovering, recoverReason);

    // Raising the output window moves keyboard focus to it, where all keys
    // except Esc are ignored by design — restore focus to the control window
    // afterwards if the operator was working there ("typing becomes
    // difficult" regression).
    bool controlHadFocus = controlWindow_ && SDL_GetKeyboardFocus() == controlWindow_;
    applyOutputDisplaySelection(outputIndex, wrongDisplay);
    SDL_ShowWindow(runtime->outputWindow);
    SDL_RaiseWindow(runtime->outputWindow);
    bool fullscreenOk = enableOutputFullscreen(outputIndex, false);
    if (controlHadFocus) {
      SDL_RaiseWindow(controlWindow_);
    }
    if (!fullscreenOk) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "fullscreen unavailable");
      if (withToast) {
        triggerToast("output recover failed: fullscreen unavailable");
      }
      return false;
    }
    setOutputHealthState(outputIndex, OutputHealthState::Live);
    if (withToast) {
      std::string displayLabel = "display " + std::to_string(targetDisplay + 1);
      if (displayCount > 0) {
        const char* displayName = deckboyGetDisplayName(targetDisplay);
        if (displayName && *displayName) {
          displayLabel = displayName;
        }
      }
      triggerToast("output recovered: " + outputLabel(outputIndex) + " -> " + displayLabel);
    }
    return true;
  }

  std::string currentDisplayLabel() const {
    const char* name = deckboyGetDisplayName(outputDisplayIndex(project_.focusedOutputIndex));
    if (name && *name) {
      return name;
    }
    return "display " + std::to_string(outputDisplayIndex(project_.focusedOutputIndex) + 1);
  }

  bool cueIsOverlayOnly(const Cue& cue) const {
    return cue.kind == CueKind::LowerThird || cue.kind == CueKind::Pip;
  }

  bool cueIndexMatchesOverlayRole(const Deck& deck, int cueIndex, bool overlayOnly) const {
    return cueIndex >= 0 &&
           cueIndex < static_cast<int>(deck.cues.size()) &&
           cueIsOverlayOnly(deck.cues[cueIndex]) == overlayOnly;
  }

  std::vector<int> cueIndicesForOverlayRole(const Deck& deck, bool overlayOnly) const {
    std::vector<int> indices;
    indices.reserve(deck.cues.size());
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      if (cueIndexMatchesOverlayRole(deck, cueIndex, overlayOnly)) {
        indices.push_back(cueIndex);
      }
    }
    return indices;
  }

  int firstCueIndexForOverlayRole(const Deck& deck, bool overlayOnly) const {
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      if (cueIndexMatchesOverlayRole(deck, cueIndex, overlayOnly)) {
        return cueIndex;
      }
    }
    return -1;
  }

  int lastCueIndexForOverlayRole(const Deck& deck, bool overlayOnly) const {
    for (int cueIndex = static_cast<int>(deck.cues.size()) - 1; cueIndex >= 0; --cueIndex) {
      if (cueIndexMatchesOverlayRole(deck, cueIndex, overlayOnly)) {
        return cueIndex;
      }
    }
    return -1;
  }

  int adjacentCueIndexForOverlayRole(const Deck& deck, int currentIndex, int direction,
                                     bool overlayOnly, bool wrap = false) const {
    if (deck.cues.empty() || direction == 0) {
      return -1;
    }
    if (currentIndex < 0 || currentIndex >= static_cast<int>(deck.cues.size())) {
      return direction > 0
        ? firstCueIndexForOverlayRole(deck, overlayOnly)
        : lastCueIndexForOverlayRole(deck, overlayOnly);
    }

    if (direction > 0) {
      for (int cueIndex = currentIndex + 1; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
        if (cueIndexMatchesOverlayRole(deck, cueIndex, overlayOnly)) {
          return cueIndex;
        }
      }
      if (wrap) {
        for (int cueIndex = 0; cueIndex < currentIndex; ++cueIndex) {
          if (cueIndexMatchesOverlayRole(deck, cueIndex, overlayOnly)) {
            return cueIndex;
          }
        }
      }
    } else {
      for (int cueIndex = currentIndex - 1; cueIndex >= 0; --cueIndex) {
        if (cueIndexMatchesOverlayRole(deck, cueIndex, overlayOnly)) {
          return cueIndex;
        }
      }
      if (wrap) {
        for (int cueIndex = static_cast<int>(deck.cues.size()) - 1; cueIndex > currentIndex; --cueIndex) {
          if (cueIndexMatchesOverlayRole(deck, cueIndex, overlayOnly)) {
            return cueIndex;
          }
        }
      }
    }
    return -1;
  }

  int nextCueIndexForDeck(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return -1;
    }
    const Deck& deck = project_.decks[deckIndex];
    int cueCount = static_cast<int>(deck.cues.size());
    if (cueCount <= 0) {
      return -1;
    }
    if (deck.selectedIndex >= 0 &&
        deck.selectedIndex < cueCount &&
        deck.selectedIndex != deck.activeIndex &&
        !cueIsOverlayOnly(deck.cues[deck.selectedIndex])) {
      return deck.selectedIndex;
    }
    if (deck.activeIndex >= 0 && deck.activeIndex < cueCount && !cueIsOverlayOnly(deck.cues[deck.activeIndex])) {
      int nextPlayable = adjacentCueIndexForOverlayRole(deck, deck.activeIndex, 1, false, deck.playlistLoop);
      if (nextPlayable >= 0) {
        return nextPlayable;
      }
    }
    if (deck.selectedIndex >= 0 &&
        deck.selectedIndex < cueCount &&
        !cueIsOverlayOnly(deck.cues[deck.selectedIndex])) {
      return deck.selectedIndex;
    }
    return firstCueIndexForOverlayRole(deck, false);
  }

  void stopBrowserCue(int deckIndex) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime) {
      return;
    }
    // Stop x11grab capture in the media engine
    if (runtime->mediaEngine && runtime->mediaEngine->isBrowserCapturing()) {
      runtime->mediaEngine->stopBrowserCapture();
    }
    if (runtime->browserRenderer) {
      runtime->browserRenderer->stop();
      runtime->browserRenderer.reset();
    }
    runtime->browserCueLive = false;
  }

  void stopBrowserCue() {
    stopBrowserCue(project_.focusedDeckIndex);
  }
  bool startBrowserCue(int deckIndex, const Cue& cue) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime) {
      return false;
    }
    std::string browserUrl = normalizeBrowserUrl(cue.path);
    stopBrowserCue(deckIndex);

    auto [targetW, targetH] = outputRenderSizeForDeck(deckIndex);
    int w = cue.width > 0 ? cue.width : targetW;
    int h = cue.height > 0 ? cue.height : targetH;
    bool legacyRaster = cue.width == kOutputWidth && cue.height == kOutputHeight;
    if (legacyRaster && (targetW != kOutputWidth || targetH != kOutputHeight)) {
      w = targetW;
      h = targetH;
    }
    runtime->browserRenderer = std::make_unique<deckboy::platform::browser::BrowserRenderer>();
    if (!runtime->browserRenderer->start(browserUrl, w, h)) {
      std::string lastError = runtime->browserRenderer->lastError();
      if (!lastError.empty()) {
        triggerToast("browser: " + lastError,
                     {79, 98, 48, 230},
                     {223, 248, 185, 255});
      }
      return false;
    }
    triggerToast("browser loading…");
    return true;
  }

  bool startBrowserCue(const Cue& cue) {
    return startBrowserCue(project_.focusedDeckIndex, cue);
  }

  // Called from App::update() to advance the phased browser startup.
  void tickBrowserStartup(int deckIndex) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->browserRenderer) {
      return;
    }

    std::string prevError = runtime->browserRenderer->lastError();
    runtime->browserRenderer->tick();
    std::string nowError = runtime->browserRenderer->lastError();
    if (!nowError.empty()) {
      if (prevError.empty()) {
        triggerToast("browser: " + nowError,
                     {79, 98, 48, 230},
                     {223, 248, 185, 255});
      }
      return;
    }

    MediaEngine* eng = runtime->mediaEngine.get();

    std::string captureSourceRef;
    int captureW = 0;
    int captureH = 0;
    if (!runtime->browserRenderer->consumeCaptureRequest(captureSourceRef, captureW, captureH)) {
      // Direct frame path (WebView2 offscreen rendering)
      deckboy::platform::browser::BrowserFrame frame;
      if (runtime->browserRenderer->grabFrame(frame) && frame.width > 0 && eng) {
        if (!runtime->browserCueLive) {
          const Deck& deck = project_.decks[deckIndex];
          double transSecs = deck.transitionSeconds;
          TransitionStyle transStyle = parseTransitionStyleToken(deck.transitionStyle);
          if (deck.activeIndex >= 0 && deck.activeIndex < (int)deck.cues.size()) {
            const Cue& ac = deck.cues[deck.activeIndex];
            if (ac.cueTransitionSeconds >= 0.0) transSecs = ac.cueTransitionSeconds;
            if (!ac.cueTransitionStyle.empty()) transStyle = parseTransitionStyleToken(ac.cueTransitionStyle);
          }
          eng->startBrowserFrameMode(frame.width, frame.height, transSecs, transStyle);
          runtime->browserCueLive = true;
          runtime->browserRenderer->markCaptureStarted();
          triggerToast("browser live");
        }
        eng->pushBrowserFrame(frame.rgba.data(), frame.width, frame.height);
      }
      runtime->browserCueLive = runtime->browserCueLive || runtime->browserRenderer->isLive();
      return;
    }

    if (!eng) {
      runtime->browserRenderer->markCaptureFailed("media engine unavailable");
      return;
    }

    const Deck& deck = project_.decks[deckIndex];
    double transSecs = deck.transitionSeconds;
    TransitionStyle transStyle = parseTransitionStyleToken(deck.transitionStyle);
    if (deck.activeIndex >= 0 && deck.activeIndex < static_cast<int>(deck.cues.size())) {
      const Cue& ac = deck.cues[deck.activeIndex];
      if (ac.cueTransitionSeconds >= 0.0) transSecs = ac.cueTransitionSeconds;
      if (!ac.cueTransitionStyle.empty()) transStyle = parseTransitionStyleToken(ac.cueTransitionStyle);
    }

    bool captureStarted = eng->startBrowserCapture(
      captureSourceRef,
      captureW,
      captureH,
      deck.activeIndex >= 0 ? deck.cues[deck.activeIndex].fadeInSeconds : 0.0,
      deck.activeIndex >= 0 ? deck.cues[deck.activeIndex].fadeOutSeconds : 0.0,
      transSecs,
      transStyle
    );
    if (!captureStarted) {
      runtime->browserRenderer->markCaptureFailed("capture start failed");
      triggerToast("browser capture failed");
      return;
    }
    runtime->browserRenderer->markCaptureStarted();
    runtime->browserCueLive = true;
    triggerToast("browser live");
  }
