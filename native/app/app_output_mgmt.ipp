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
    // Derived from the dropdown rather than repeated here. The two lists had
    // already drifted -- this one was {"srt","rtmp"} while the dropdown offered
    // more -- so cycling could never reach the extra protocols.
    const auto choices = outputStreamProtocolDropdownChoices();
    if (choices.empty()) return false;
    const int count = static_cast<int>(choices.size());
    std::string current = normalizeOutputStreamProtocol(output.streamProtocol);
    int index = 0;
    for (int i = 0; i < count; ++i) {
      if (choices[i].first == current) {
        index = i;
        break;
      }
    }
    int next = ((index + direction) % count + count) % count;
    return setFocusedOutputStreamProtocol(choices[next].first);
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

  SDL_AudioStream* openMainAudioDevice(const std::string& preferredDeviceName, std::string& effectiveName,
                                       int channels = kAudioChannels) {
    applyAudioBufferSizeHint();
    SDL_AudioSpec desired {};
    desired.freq = kAudioRate;
    desired.format = kAudioFormat;
    // Even channel counts only (stereo pairs); SDL folds down when the
    // physical device has fewer outs.
    desired.channels = std::clamp(channels - (channels % 2), static_cast<int>(kAudioChannels), 8);

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

  // ── SMPTE LTC generator (timecode OUT) ─────────────────────────────────────
  // Deckboy's own show clock, encoded as LTC onto a real audio device. This is
  // what makes Deckboy a timecode MASTER rather than only a chaser: feed the
  // chosen output to a spare pair or an interface's TC out and everything else
  // on the floor can lock to the deck.
  //
  // The timecode emitted is the focused deck's playlist start offset plus the
  // live cue's position. When nothing is playing the generator HOLDS the last
  // value and keeps emitting it, rather than stopping — a receiver that loses
  // carrier drops out of lock, and coming back from that is far more disruptive
  // than sitting on a stationary code.
  double ltcOutputTimecodeSeconds() const {
    const Deck& deck = focusedDeck();
    double seconds = deck.playlistStartOffsetSeconds;
    if (const MediaEngine* engine = focusedMediaEngine()) {
      seconds += std::max(0.0, engine->position());
    }
    return std::max(0.0, seconds);
  }

  bool startLtcOutput() {
    if (ltcOutStream_) {
      return true;
    }
    if (!ltcApi_.ensureLoaded()) {
      triggerToast("ltc out: libltc unavailable");
      return false;
    }
    if (!ltcApi_.encoderAvailable) {
      triggerToast("ltc out: libltc has no encoder");
      return false;
    }
    // 48 kHz s16. LTC is a single-channel biphase-mark signal, but the device is
    // opened with the operator's channel count so the code can be placed on ONE
    // channel and every other channel held silent — timecode is a control
    // signal and must not leak into the programme mix.
    ltcOutChannels_ = std::clamp(project_.ltcOutputChannelCount, 1, 8);
    ltcOutChannel_ = std::clamp(project_.ltcOutputChannel, 0, ltcOutChannels_ - 1);
    SDL_AudioSpec desired {};
    desired.freq = 48000;
    desired.format = SDL_AUDIO_S16;
    desired.channels = ltcOutChannels_;

    ltcOutDeviceName_ = trim(project_.ltcOutputDeviceName);
    SDL_AudioDeviceID target = audioPlaybackDeviceIdForName(ltcOutDeviceName_);
    if (target == 0) {
      ltcOutDeviceName_.clear();
      target = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    }
    ltcOutStream_ = SDL_OpenAudioDeviceStream(target, &desired, nullptr, nullptr);
    if (!ltcOutStream_ && target != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) {
      ltcOutDeviceName_.clear();
      ltcOutStream_ = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, nullptr, nullptr);
    }
    if (!ltcOutStream_) {
      triggerToast("ltc out: no audio device");
      return false;
    }

    ltcOutFps_ = std::clamp(project_.ltcOutputFps, 23.0, 60.0);
    // standard 0 = LTC_TV_525_60, the generic choice for 30/29.97.
    ltcEncoder_ = ltcApi_.encoderCreateFn(48000.0, ltcOutFps_, 0, 0);
    if (!ltcEncoder_) {
      SDL_DestroyAudioStream(ltcOutStream_);
      ltcOutStream_ = nullptr;
      triggerToast("ltc out: encoder failed");
      return false;
    }
    if (ltcApi_.encoderSetVolumeFn) {
      // -3 dBFS: hot enough for a reader to lock, short of clipping.
      ltcApi_.encoderSetVolumeFn(ltcEncoder_, -3.0);
    }
    ltcOutFrameBuf_.assign(static_cast<std::size_t>(48000.0 / ltcOutFps_) + 64, 0);
    ltcOutPrimed_ = false;
    deckboySetAudioPaused(ltcOutStream_, false);
    triggerToast("ltc out: on @ " + fmtFloat(ltcOutFps_, 2) + "fps"
                 + (ltcOutDeviceName_.empty() ? " (default device)" : (" - " + ltcOutDeviceName_)));
    return true;
  }

  void stopLtcOutput() {
    if (ltcEncoder_) {
      ltcApi_.encoderFreeFn(ltcEncoder_);
      ltcEncoder_ = nullptr;
    }
    if (ltcOutStream_) {
      SDL_DestroyAudioStream(ltcOutStream_);
      ltcOutStream_ = nullptr;
    }
    ltcOutPrimed_ = false;
  }

  // Called every tick. Keeps roughly 120 ms of LTC queued: enough that a
  // scheduling hiccup cannot punch a hole in the carrier, short enough that the
  // code a receiver reads is never far behind the deck.
  void pumpLtcOutput() {
    if (!project_.ltcOutputEnabled) {
      if (ltcOutStream_) {
        stopLtcOutput();
      }
      return;
    }
    // Re-open on a settings change (device or rate).
    const double wantFps = std::clamp(project_.ltcOutputFps, 23.0, 60.0);
    if (ltcOutStream_ && (std::abs(wantFps - ltcOutFps_) > 0.001 ||
                          trim(project_.ltcOutputDeviceName) != ltcOutDeviceName_ ||
                          std::clamp(project_.ltcOutputChannelCount, 1, 8) != ltcOutChannels_ ||
                          std::clamp(project_.ltcOutputChannel, 0, ltcOutChannels_ - 1) != ltcOutChannel_)) {
      stopLtcOutput();
    }
    if (!ltcOutStream_ && !startLtcOutput()) {
      // Don't retry every frame on a hard failure — the operator turns it off.
      project_.ltcOutputEnabled = false;
      return;
    }

    constexpr int kTargetQueuedMs = 120;
    // s16 x channels: the queue target must scale with the opened channel
    // count or a multi-channel route would be starved.
    const int targetBytes = 48000 * 2 * ltcOutChannels_ * kTargetQueuedMs / 1000;

    double sourceSeconds = ltcOutputTimecodeSeconds();
    int guard = 0;
    while (SDL_GetAudioStreamQueued(ltcOutStream_) < targetBytes && guard++ < 32) {
      // Re-seat the encoder when it has drifted more than a frame from the
      // deck, but let it free-run otherwise: re-setting every frame from a
      // jittery position read would make the emitted code stutter, and a
      // stuttering master is worse than a slightly late one.
      const double encoderSeconds = ltcOutEmittedFrames_ / ltcOutFps_ + ltcOutBaseSeconds_;
      if (!ltcOutPrimed_ || std::abs(sourceSeconds - encoderSeconds) > (2.0 / ltcOutFps_)) {
        const long long totalFrames =
          static_cast<long long>(std::llround(sourceSeconds * ltcOutFps_));
        const long long fpsWhole = static_cast<long long>(std::llround(ltcOutFps_));
        LtcSmpteTimecode tc {};
        std::snprintf(tc.timezone, sizeof(tc.timezone), "+0000");
        tc.frame = static_cast<unsigned char>(totalFrames % std::max(1LL, fpsWhole));
        const long long totalSecs = totalFrames / std::max(1LL, fpsWhole);
        tc.secs  = static_cast<unsigned char>(totalSecs % 60);
        tc.mins  = static_cast<unsigned char>((totalSecs / 60) % 60);
        tc.hours = static_cast<unsigned char>((totalSecs / 3600) % 24);
        ltcApi_.encoderSetTimecodeFn(ltcEncoder_, &tc);
        ltcOutBaseSeconds_ = sourceSeconds;
        ltcOutEmittedFrames_ = 0;
        ltcOutPrimed_ = true;
      }

      ltcApi_.encoderEncodeFrameFn(ltcEncoder_);
      const int got = ltcApi_.encoderGetBufferFn(ltcEncoder_, ltcOutFrameBuf_.data());
      if (got <= 0) {
        break;
      }
      // libltc emits unsigned 8-bit centred on 128; widen to s16 and place it on
      // the routed channel only, leaving the rest silent.
      const std::size_t frames = static_cast<std::size_t>(got);
      ltcOutPcm_.assign(frames * static_cast<std::size_t>(ltcOutChannels_), 0);
      for (std::size_t f = 0; f < frames; ++f) {
        const int centred = static_cast<int>(ltcOutFrameBuf_[f]) - 128;
        ltcOutPcm_[f * static_cast<std::size_t>(ltcOutChannels_) +
                   static_cast<std::size_t>(ltcOutChannel_)] =
          static_cast<std::int16_t>(std::clamp(centred * 256, -32768, 32767));
      }
      SDL_PutAudioStreamData(ltcOutStream_, ltcOutPcm_.data(),
                             static_cast<int>(ltcOutPcm_.size() * sizeof(std::int16_t)));
      if (ltcApi_.encoderBufferFlushFn) {
        ltcApi_.encoderBufferFlushFn(ltcEncoder_);
      }
      ltcApi_.encoderIncTimecodeFn(ltcEncoder_);
      ++ltcOutEmittedFrames_;
    }
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
  // ---- Live audio input ----------------------------------------------------
  // Opening someone's microphone is not something to do implicitly, so this is
  // strictly opt-in and reports the device that actually opened rather than the
  // one that was asked for -- they differ whenever the saved device has been
  // unplugged, and silently falling back to a different mic is the kind of
  // surprise that ruins a recording.
  bool startAudioInput() {
    stopAudioInput();
    SDL_AudioSpec desired {};
    desired.freq = kAudioRate;
    desired.format = SDL_AUDIO_S16;
    desired.channels = 2;

    std::string wanted = project_.audioInputDeviceName;
    std::string effective = wanted;
    SDL_AudioDeviceID target = audioRecordingDeviceIdForName(wanted);
    if (target == 0) {
      effective.clear();
      target = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    }
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(target, &desired, nullptr, nullptr);
    if (!stream && target != SDL_AUDIO_DEVICE_DEFAULT_RECORDING) {
      effective.clear();
      stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &desired, nullptr, nullptr);
    }
    if (!stream) {
      failRemoteCommand("audio input: device unavailable");
      showLog("AUDIO IN FAIL", wanted.empty() ? "(default)" : wanted);
      return false;
    }
    // A recording stream opens PAUSED like any other; without this it captures
    // nothing and the meter sits at zero looking like a dead microphone.
    deckboySetAudioPaused(stream, false);
    audioInputStream_ = stream;
    audioInputActiveDevice_ = effective;
    const std::string shown = effective.empty() ? "(system default)" : effective;
    triggerToast("audio input: " + shown);
    showLog("AUDIO IN", shown);
    if (!wanted.empty() && effective.empty()) {
      // Said out loud rather than swallowed: the operator chose a device and
      // is getting a different one.
      triggerToast("requested input missing - using default");
    }
    return true;
  }

  void stopAudioInput() {
    if (!audioInputStream_) return;
    SDL_DestroyAudioStream(audioInputStream_);
    audioInputStream_ = nullptr;
    audioInputActiveDevice_.clear();
    audioInputPeak_ = 0.0;
    // Drop anything captured but not yet muxed, or the next recording would
    // open with audio from the previous session.
    std::lock_guard<std::mutex> lock(audioInputMixMutex_);
    audioInputMixBuffer_.clear();
  }

  bool audioInputRunning() const { return audioInputStream_ != nullptr; }

  // Drain whatever the device has and reduce it to a level. Called every tick;
  // the data is DISCARDED after metering because nothing routes it yet -- if it
  // were left in the stream it would back up until the device stalled.
  void pumpAudioInput() {
    if (!audioInputStream_) return;
    const int avail = SDL_GetAudioStreamAvailable(audioInputStream_);
    if (avail <= 0) return;
    audioInputScratch_.resize(static_cast<std::size_t>(avail) / sizeof(std::int16_t));
    const int got = SDL_GetAudioStreamData(
      audioInputStream_, audioInputScratch_.data(), avail);
    if (got <= 0) return;
    const std::size_t n = static_cast<std::size_t>(got) / sizeof(std::int16_t);
    const double gain = std::pow(10.0, std::clamp(project_.audioInputGainDb, -40.0, 40.0) / 20.0);
    double peak = 0.0;
    // Gain is applied to the SAMPLES, not just to the meter. Metering a level
    // the recording does not actually carry would make the meter a liar.
    for (std::size_t i = 0; i < n; ++i) {
      const double scaled = audioInputScratch_[i] * gain;
      // Clip is detected BEFORE the clamp. Checking the clamped value can only
      // ever report exactly full scale, which real signals rarely hit even
      // when badly overdriven.
      if (std::abs(scaled) >= 32767.0) project_.audioInputClipLatch = true;
      audioInputScratch_[i] = static_cast<std::int16_t>(std::clamp(scaled, -32768.0, 32767.0));
      const double v = std::abs(audioInputScratch_[i] / 32768.0);
      if (v > peak) peak = v;
    }
    // A mono source captured as stereo lands in one leg with silence in the
    // other. Sum to both so it plays back centred rather than one-sided.
    if (project_.audioInputMono) {
      for (std::size_t i = 0; i + 1 < n; i += 2) {
        const int mixL = audioInputScratch_[i];
        const int mixR = audioInputScratch_[i + 1];
        const std::int16_t m = static_cast<std::int16_t>(
          std::clamp((mixL + mixR) / 2, -32768, 32767));
        audioInputScratch_[i] = m;
        audioInputScratch_[i + 1] = m;
      }
    }
    if (project_.audioInputToProgram) {
      std::lock_guard<std::mutex> lock(audioInputMixMutex_);
      audioInputMixBuffer_.insert(audioInputMixBuffer_.end(),
                                  audioInputScratch_.begin(),
                                  audioInputScratch_.begin() + static_cast<std::ptrdiff_t>(n));
      // Bounded like the deck buffers. An input nobody is consuming -- no
      // stream armed -- must not grow without limit for the length of a show.
      constexpr std::size_t kMaxInputSamples = 48000 * 2 * 4;   // ~4s stereo
      if (audioInputMixBuffer_.size() > kMaxInputSamples) {
        audioInputMixBuffer_.erase(
          audioInputMixBuffer_.begin(),
          audioInputMixBuffer_.begin() +
            static_cast<std::ptrdiff_t>(audioInputMixBuffer_.size() - kMaxInputSamples));
      }
    }
    // Fast attack, slow release. A meter that falls as fast as it rises is
    // unreadable, and a visualiser driven by one twitches instead of moving.
    if (peak > audioInputPeak_) {
      audioInputPeak_ = peak;
    } else {
      audioInputPeak_ += (peak - audioInputPeak_) * 0.08;
    }
  }

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
      // Take the device back BEFORE tearing the engine down. Without this the
      // engine still holds the stream through stopAll() and again through
      // ~MediaEngine's own stopAll(), touching an SDL audio stream while the
      // rest of the shutdown is pulling the audio subsystem apart around it.
      runtime.mediaEngine->detachAudioDevice();
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

  // The rate a RECORDING is written at. Stated explicitly, never inferred:
  // a deliverable has to land on the standard it claims, and "whatever the
  // operator's monitor refreshes at" is not a standard. 0 = follow programme.
  double recordingFps(double fpsHint) const {
    if (std::isfinite(project_.recordingFps) && project_.recordingFps > 1.0) {
      return std::clamp(project_.recordingFps, 1.0, 120.0);
    }
    return outputStreamFps(fpsHint);
  }

  // Video encoder for RECORDINGS, probed once against the ffmpeg actually
  // installed. Software x264 cannot hold a broadcast rate here: the app is
  // already compositing a 4K programme, and at 1080p50 the encoder fell behind,
  // built a backlog and the take was truncated when the muxer was closed
  // (MEASURED: 601 frames delivered, 100 in the file). A GPU encoder runs
  // 1080p50 at 222fps -- 4.4x realtime -- and costs the CPU nothing.
  //
  // Order is by how widely each is available on show hardware. Empty means no
  // hardware encoder, and the caller falls back to libx264.
  const std::string& recordingVideoEncoder() const {
    static bool probed = false;
    static std::string chosen;
    if (probed) {
      return chosen;
    }
    probed = true;
    if (auto text = readAllText({"ffmpeg", "-hide_banner", "-encoders"})) {
      for (const char* candidate : {"h264_nvenc", "h264_qsv", "h264_amf"}) {
        if (text->find(candidate) != std::string::npos) {
          chosen = candidate;
          break;
        }
      }
    }
    return chosen;
  }

  // Is anything actually going OUT of the box right now — a recording, a
  // stream, NDI, DeckLink, Spout or ST 2110? While something is, the capture
  // path is on the critical path and the UI must not pace the loop.
  bool anyOutputEgressActive() const {
    for (const OutputTarget& out : project_.outputs) {
      if (!out.enabled) {
        continue;
      }
      if (out.streamEnabled || out.ndiEnabled || out.ndiKeyEnabled ||
          out.deckLinkEnabled || out.spoutEnabled || out.st2110Enabled) {
        return true;
      }
    }
    return false;
  }

  // Should the take roll onto a new file now?
  //
  // Two operator-set caps (time and size), plus a hard ceiling that is not
  // optional. FAT32 -- which is what most USB media arrives formatted as --
  // cannot hold a file over 4GB, and the failure mode is losing the take at the
  // moment it crosses the line. Every hardware recorder segments for exactly
  // this reason; AJA does it at both the operator's cap and the filesystem's.
  // Rolling at 3.8GB costs nothing on NTFS or exFAT and saves the show on FAT32.
  bool shouldRollRecordingSegment(OutputRuntime& runtime) const {
    if (runtime.recordPacerStartMs == 0) {
      return false;
    }
    const Uint64 nowMs = SDL_GetTicks();
    const int minutes = std::clamp(project_.recordingSegmentMinutes, 0, 240);
    if (minutes > 0 &&
        (nowMs - runtime.recordPacerStartMs) >=
          static_cast<Uint64>(minutes) * 60000u) {
      return true;
    }
    // Size is a filesystem stat ON A FILE THE ENCODER IS ACTIVELY WRITING, so
    // it is deliberately infrequent: every five seconds, not every frame. Even
    // at DNxHR HQ rates it takes minutes to cover the gap between checks, and
    // this runs on the render thread where a stall costs frames.
    if (nowMs - runtime.lastSegmentSizeCheckMs < 5000) {
      return false;
    }
    runtime.lastSegmentSizeCheckMs = nowMs;
    constexpr std::uintmax_t kFat32Ceiling =
      static_cast<std::uintmax_t>(3800) * 1024u * 1024u;
    const std::uintmax_t cap =
      project_.recordingSegmentMegabytes > 0
        ? std::min<std::uintmax_t>(
            static_cast<std::uintmax_t>(project_.recordingSegmentMegabytes) * 1024u * 1024u,
            kFat32Ceiling)
        : kFat32Ceiling;
    return recordingBytesOnDisk() >= cap;
  }

  // Rewrite a finished recording from fragmented into an ordinary MP4/MOV.
  //
  // Fragmented is what makes a killed or crashed encoder still leave a playable
  // file, and that is worth keeping. But a fragmented file is awkward
  // downstream: browsers cannot show its duration, seeking misbehaves in some
  // players, and plenty of editors reject it outright. OBS solved this with
  // what they call hybrid MP4 -- fragmented while recording, rewritten to a
  // normal file at stop. This is the same trade done with a stream copy, so it
  // costs seconds and not a single re-encoded pixel.
  //
  // Detached: a long take takes a moment to rewrite and the operator must not
  // wait for it. The original is only replaced once the rewrite has succeeded
  // and produced a non-empty file, so a failure here can never cost the take.
  void queueRecordingRemux(const std::string& path) {
    if (path.empty()) {
      return;
    }
    std::thread([path]() {
      std::error_code ec;
      if (!fs::exists(path, ec) || fs::file_size(path, ec) == 0) {
        return;
      }
      const fs::path src(path);
      const fs::path tmp = fs::path(src).replace_extension(
        ".remux" + src.extension().string());
      const std::vector<std::string> args {
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin",
        "-y", "-i", path,
        "-c", "copy",
        // Index at the FRONT, which is what makes the file seek instantly and
        // is the whole point of the rewrite.
        "-movflags", "+faststart",
        tmp.string()
      };
      if (!readAllText(args)) {
        fs::remove(tmp, ec);
        return;   // keep the fragmented original; it is still playable
      }
      if (!fs::exists(tmp, ec) || fs::file_size(tmp, ec) == 0) {
        fs::remove(tmp, ec);
        return;
      }
      fs::remove(src, ec);
      fs::rename(tmp, src, ec);
      if (ec) {
        // Rename failed (AV scanner, permissions): leave both rather than lose
        // the take.
        return;
      }
    }).detach();
  }

  // ── Recording codec ────────────────────────────────────────────────────────
  static std::string normalizeRecordingCodec(std::string token) {
    token = toLower(trim(token));
    static const std::set<std::string> known {
      "h264", "hevc",
      "prores_proxy", "prores_lt", "prores_422", "prores_hq", "prores_4444",
      "dnxhr_lb", "dnxhr_sq", "dnxhr_hq", "dnxhr_hqx"
    };
    return known.count(token) ? token : std::string("h264");
  }

  static bool recordingCodecIsProRes(const std::string& c) {
    return c.rfind("prores", 0) == 0;
  }
  static bool recordingCodecIsDnx(const std::string& c) {
    return c.rfind("dnxhr", 0) == 0;
  }

  // The container a codec has to live in. ProRes and DNxHR are QuickTime-native
  // and an .mp4 wrapper is exactly the kind of file an edit suite bounces.
  std::string recordingContainerExtension() const {
    const std::string c = normalizeRecordingCodec(project_.recordingCodec);
    return (recordingCodecIsProRes(c) || recordingCodecIsDnx(c)) ? ".mov" : ".mp4";
  }

  // Encoder arguments for the chosen codec. ProRes profile numbers follow
  // prores_ks: 0 proxy, 1 LT, 2 standard(422), 3 HQ, 4 4444.
  std::vector<std::string> recordingCodecArgs() const {
    const std::string c = normalizeRecordingCodec(project_.recordingCodec);
    if (recordingCodecIsProRes(c)) {
      const char* profile = c == "prores_proxy" ? "0"
                          : c == "prores_lt"    ? "1"
                          : c == "prores_422"   ? "2"
                          : c == "prores_4444"  ? "4"
                                                : "3";   // hq
      // 4444 carries alpha and needs a 4:4:4 pixel format; everything else is
      // 10-bit 4:2:2, which is what ProRes actually is -- feeding it 8-bit
      // yuv420p would throw away the reason for choosing it.
      return {"-c:v", "prores_ks", "-profile:v", profile,
              "-pix_fmt", (c == "prores_4444") ? "yuva444p10le" : "yuv422p10le",
              "-vendor", "apl0"};
    }
    if (recordingCodecIsDnx(c)) {
      const std::string profile =
        c == "dnxhr_lb"  ? "dnxhr_lb"  :
        c == "dnxhr_sq"  ? "dnxhr_sq"  :
        c == "dnxhr_hqx" ? "dnxhr_hqx" : "dnxhr_hq";
      // HQX is the 10-bit rung; LB/SQ/HQ are 8-bit 4:2:2.
      return {"-c:v", "dnxhd", "-profile:v", profile,
              "-pix_fmt", (c == "dnxhr_hqx") ? "yuv422p10le" : "yuv422p"};
    }
    if (c == "hevc") {
      return {"-c:v", "libx265", "-preset", "veryfast", "-pix_fmt", "yuv420p"};
    }
    return {"-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"};
  }

  // Mezzanine codecs are CONSTANT quality by profile -- a bitrate ceiling is
  // meaningless and ffmpeg rejects some of these combinations outright.
  bool recordingCodecUsesBitrate() const {
    const std::string c = normalizeRecordingCodec(project_.recordingCodec);
    return !recordingCodecIsProRes(c) && !recordingCodecIsDnx(c);
  }

  // ── Broadcast rates are RATIONAL ───────────────────────────────────────────
  // 23.976, 29.97 and 59.94 are 24000/1001, 30000/1001 and 60000/1001. Writing
  // them as two-decimal strings ("23.98", "29.97") is close but WRONG: 23.98
  // against 23.976023976 drifts about 0.6 seconds an hour, which is nothing to
  // a viewer and fatal to anything conformed against timecode. ffmpeg accepts
  // the exact ratio, so give it the exact ratio.
  static std::string exactRateToken(double fps) {
    struct Known { double value; const char* token; };
    static const Known table[] = {
      {24000.0 / 1001.0, "24000/1001"},
      {30000.0 / 1001.0, "30000/1001"},
      {60000.0 / 1001.0, "60000/1001"},
      {120000.0 / 1001.0, "120000/1001"},
    };
    for (const Known& k : table) {
      // Generous window: the operator may have typed 23.98, 23.976 or 23.9760.
      if (std::fabs(fps - k.value) < 0.02) {
        return k.token;
      }
    }
    std::ostringstream out;
    if (std::fabs(fps - std::lround(fps)) < 1e-6) {
      out << std::lround(fps);        // 25, 30, 50 -- no spurious decimals
    } else {
      out << std::fixed << std::setprecision(6) << fps;
    }
    return out.str();
  }

  // True when the rate is one of the NTSC fractional family, which is the only
  // case where drop-frame timecode means anything.
  static bool rateIsFractional(double fps) {
    return std::fabs(fps - std::lround(fps)) > 1e-6;
  }

  // Drop-frame skips two timecode NUMBERS at the top of every minute except
  // every tenth, so the count tracks wall clock at 29.97/59.94. No video frame
  // is ever lost -- only labels. At an integer rate it is meaningless, and AJA
  // resolves it exactly this way: auto-select by the detected rate.
  bool recordingUsesDropFrame(double rate) const {
    const std::string mode = toLower(trim(project_.recordingTimecodeDropFrame));
    if (mode == "df")  return rateIsFractional(rate);
    if (mode == "ndf") return false;
    return rateIsFractional(rate);
  }

  // The timecode stamped onto the take. Separator carries the DF/NDF flag:
  // ';' before the frames field means drop-frame, ':' means non-drop. That is
  // the SMPTE convention and what ffmpeg's tmcd writer reads.
  std::string recordingStartTimecode(double rate) const {
    int hh = 0, mm = 0, ss = 0, ff = 0;
    if (toLower(trim(project_.recordingTimecodeMode)) == "timeofday") {
      const std::time_t now = std::time(nullptr);
      if (std::tm* lt = std::localtime(&now)) {
        hh = lt->tm_hour; mm = lt->tm_min; ss = lt->tm_sec;
      }
    } else {
      const std::string& s = project_.recordingTimecodeStart;
      if (std::sscanf(s.c_str(), "%d:%d:%d:%d", &hh, &mm, &ss, &ff) != 4) {
        hh = mm = ss = ff = 0;
      }
    }
    const int maxFrame = std::max(1, static_cast<int>(std::lround(rate))) - 1;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d%c%02d",
                  std::clamp(hh, 0, 23), std::clamp(mm, 0, 59),
                  std::clamp(ss, 0, 59),
                  recordingUsesDropFrame(rate) ? ';' : ':',
                  std::clamp(ff, 0, maxFrame));
    return buf;
  }

  // Human-readable recording standard, for toasts, STATUS and the settings UI.
  std::string recordingFormatLabel() const {
    std::string raster = (project_.recordingWidth > 0 && project_.recordingHeight > 0)
      ? (std::to_string(project_.recordingWidth) + "x" +
         std::to_string(project_.recordingHeight))
      : std::string("programme");
    std::string rate;
    if (std::isfinite(project_.recordingFps) && project_.recordingFps > 1.0) {
      char buf[32];
      // 23.98/29.97/59.94 must not be rounded away in the label; a trailing
      // ".00" on an integer rate is noise.
      const double r = project_.recordingFps;
      if (std::fabs(r - std::lround(r)) < 0.005) {
        std::snprintf(buf, sizeof(buf), "%ld", std::lround(r));
      } else {
        std::snprintf(buf, sizeof(buf), "%.2f", r);
      }
      rate = buf;
    } else {
      rate = "programme";
    }
    return raster + " @ " + rate;
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

  // Merge the operator's SRT settings into the destination URL as query
  // parameters. Until now these could only be set by hand-typing a query
  // string, which is not a thing to ask of someone mid-show.
  //
  // ANYTHING ALREADY PRESENT IN THE URL WINS — shows that were configured the
  // old way (and receiver-specific keys Deckboy has no field for) must keep
  // working untouched.
  std::string applySrtUrlParameters(const OutputTarget& output, std::string url) const {
    if (normalizeOutputStreamProtocol(output.streamProtocol) != "srt") {
      return url;
    }
    const std::string existing = url.find('?') != std::string::npos
      ? toLower(url.substr(url.find('?') + 1)) : std::string();
    auto hasParam = [&](const std::string& keyName) {
      return existing.find(keyName + "=") != std::string::npos;
    };
    std::vector<std::pair<std::string, std::string>> params;
    if (!hasParam("mode")) {
      params.emplace_back("mode", output.srtMode == "listener" ? "listener" : "caller");
    }
    if (!hasParam("transtype")) {
      params.emplace_back("transtype", "live");
    }
    if (!hasParam("latency")) {
      // ffmpeg's SRT latency is in MICROseconds.
      params.emplace_back("latency",
                          std::to_string(std::clamp(output.srtLatencyMs, 20, 8000) * 1000));
    }
    const std::string passphrase = trim(output.srtPassphrase);
    // SRT rejects passphrases shorter than 10 characters outright, so a short
    // one would silently kill the connection rather than stream unencrypted.
    if (!passphrase.empty() && passphrase.size() >= 10 && !hasParam("passphrase")) {
      params.emplace_back("passphrase", passphrase);
    }
    const std::string streamId = trim(output.srtStreamId);
    if (!streamId.empty() && !hasParam("streamid")) {
      params.emplace_back("streamid", streamId);
    }
    for (const auto& [k, v] : params) {
      url += (url.find('?') == std::string::npos) ? '?' : '&';
      url += k + "=" + v;
    }
    return url;
  }

  // True when the SRT passphrase is set but too short for SRT to accept.
  // Surfaced in the UI rather than left to fail as a mystery connection error.
  bool srtPassphraseTooShort(const OutputTarget& output) const {
    const std::string p = trim(output.srtPassphrase);
    return !p.empty() && p.size() < 10;
  }

  // ── Stream destinations ────────────────────────────────────────────────────
  // Streaming is not a property of "whichever output is focused" — it carries
  // the programme, and an operator wants SRT and RTMP configured side by side
  // and able to run at the same time. Each protocol therefore gets its OWN
  // dedicated stream output, found by protocol and created on demand.
  //
  // This reuses the existing per-output stream runtime rather than adding a
  // second streaming system: a stream OutputTarget already spawns an encoder
  // fed from the programme composite, so two of them is genuinely two streams.
  int findStreamOutputForProtocol(const std::string& protocol) const {
    const std::string wanted = normalizeOutputStreamProtocol(protocol);
    for (std::size_t i = 0; i < project_.outputs.size(); ++i) {
      const OutputTarget& out = project_.outputs[i];
      if (normalizeOutputType(out.outputType) == "stream" &&
          normalizeOutputStreamProtocol(out.streamProtocol) == wanted) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  // Created only when the operator actually turns a destination on, so an
  // untouched show never accumulates phantom outputs.
  int ensureStreamOutputForProtocol(const std::string& protocol) {
    const std::string wanted = normalizeOutputStreamProtocol(protocol);
    int existing = findStreamOutputForProtocol(wanted);
    if (existing >= 0) {
      return existing;
    }
    int previousFocus = project_.focusedOutputIndex;
    int index = addOutput(project_.focusedDeckIndex, "stream");
    if (index < 0 || index >= static_cast<int>(project_.outputs.size())) {
      return -1;
    }
    OutputTarget& out = project_.outputs[index];
    out.streamProtocol = wanted;
    out.name = (wanted == "srt")  ? "SRT Stream"
             : (wanted == "file") ? "Program Recording"
                                  : "RTMP Stream";
    out.streamUrl = defaultOutputStreamUrl(wanted, index);
    out.streamEnabled = false;   // configured, not yet live
    out.enabled = false;
    // addOutput focuses what it creates; the Streaming page is not an output
    // picker, so put the operator's focus back where it was.
    project_.focusedOutputIndex = std::clamp(previousFocus, 0,
                                             static_cast<int>(project_.outputs.size()) - 1);
    markProjectDirty();
    return index;
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
    if (!key.empty() && outputStreamProtocolIsRtmp(protocol)) {
      if (url.back() != '/') url += '/';
      url += key;
    }
    url = applySrtUrlParameters(output, url);
    int bitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    double fps = outputStreamProtocolIsFile(protocol)
      ? recordingFps(fpsHint) : outputStreamFps(fpsHint);
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

  // Turn the operator's recording NAME into a real path, stamped with the
  // start time. Two invariants:
  //   1. A recording never overwrites a previous take. The operator is running
  //      a live show; losing the last one to a same-named restart is not a
  //      recoverable mistake.
  //   2. It never writes inside a macOS .app bundle. A bare name lands in
  //      recordings/ beside the show, or in stateDir() when no show is open --
  //      the same rule the converted-media cache follows.
  // An absolute path from the operator is honoured as-is, minus the stamp.
  // ---- Program recording ---------------------------------------------------
  // Recording IS a stream output whose sink is a file, so it reuses the whole
  // egress path. These wrap that so the operator never has to know.
  int recordingOutputIndex() const { return findStreamOutputForProtocol("file"); }

  bool recordingActive() const {
    const int idx = recordingOutputIndex();
    if (idx < 0) return false;
    const OutputTarget& out = project_.outputs[idx];
    return out.enabled && out.streamEnabled;
  }

  double recordingElapsedSeconds() const {
    if (!recordingActive() || recordingStartedMs_ == 0) return 0.0;
    return (SDL_GetTicks() - recordingStartedMs_) / 1000.0;
  }

  // Size of the file being written right now. Cheap enough to poll per frame,
  // and it is the only honest confirmation that bytes are actually landing --
  // an armed output that is silently failing looks identical otherwise.
  std::uintmax_t recordingBytesOnDisk() const {
    if (lastRecordingPath_.empty()) return 0;
    std::error_code ec;
    auto n = fs::file_size(fs::path(lastRecordingPath_), ec);
    return ec ? 0 : n;
  }

  void toggleRecording() {
    const bool wasActive = recordingActive();
    int idx = wasActive ? recordingOutputIndex() : ensureStreamOutputForProtocol("file");
    if (idx < 0 || idx >= static_cast<int>(project_.outputs.size())) {
      failRemoteCommand("record: could not create a recording output");
      return;
    }
    OutputTarget& out = project_.outputs[idx];
    out.enabled = !wasActive;
    out.streamEnabled = !wasActive;
    markProjectDirty();
    playUiSound(UiSoundEffect::Toggle);
    if (wasActive) {
      const std::uintmax_t bytes = recordingBytesOnDisk();
      triggerToast(bytes > 0
        ? "RECORDING STOPPED - " + std::to_string(bytes / (1024 * 1024)) + " MB"
        : "RECORDING STOPPED");
      showLog("RECORD STOP", lastRecordingPath_);
      recordingStartedMs_ = 0;
    } else {
      recordingStartedMs_ = SDL_GetTicks();
      lastRecordingPath_.clear();   // filled in when the args are built
      triggerToast("RECORDING");
      showLog("RECORD START", recordingDirLabel());
    }
  }

  // Where recordings will land, for display. Mirrors resolveRecordingPath's
  // rules so the label can never disagree with what actually happens.
  std::string recordingDirLabel() const {
    if (!project_.recordingDir.empty()) return project_.recordingDir;
    fs::path base = (!currentProjectFile_.empty() && currentProjectFile_.has_parent_path())
      ? currentProjectFile_.parent_path()
      : Paths::stateDir();
    return (base / "recordings").string();
  }

  // True when recordings would land on the same volume Deckboy is running
  // from. Worth saying out loud: a live recording competes for seek bandwidth
  // with media playback, and a disk that fills takes the app down too.
  bool recordingSharesAppVolume() const {
    std::error_code ec;
    const fs::path rec(recordingDirLabel());
    const fs::path app = Paths::stateDir();
    return fs::path(rec).root_name().string() == fs::path(app).root_name().string();
  }

  void pickRecordingDir() {
    showFolderDialog([this](std::vector<std::string> chosen) {
      if (chosen.empty() || chosen.front().empty()) return;
      project_.recordingDir = chosen.front();
      markProjectDirty();
      triggerToast(recordingSharesAppVolume()
        ? "record folder set - SAME DISK as Deckboy"
        : "record folder set");
    });
  }

  std::string resolveRecordingPath(const std::string& nameOrPath) const {
    std::error_code ec;
    fs::path given(trim(nameOrPath));
    fs::path dir;
    fs::path stem = given.stem();
    std::string ext = given.extension().string();
    if (given.is_absolute() && given.has_parent_path()) {
      dir = given.parent_path();
    } else if (!project_.recordingDir.empty()) {
      // An explicitly chosen record volume. Separate from the encoder's output
      // directory on purpose: recording a live show to the same disk Deckboy is
      // reading media from costs seek bandwidth, and a disk that fills takes
      // both the recording AND the app down with it.
      dir = fs::path(project_.recordingDir);
    } else {
      fs::path base = (!currentProjectFile_.empty() && currentProjectFile_.has_parent_path())
        ? currentProjectFile_.parent_path()
        : Paths::stateDir();
      dir = base / "recordings";
    }
    if (stem.empty()) stem = "program";
    // The CODEC decides the container, not whatever extension the URL happens
    // to carry. A ProRes or DNxHR essence in an .mp4 wrapper is the kind of
    // file an edit suite bounces, and the default URL says .mp4 for everything.
    ext = recordingContainerExtension();
    std::time_t now = std::time(nullptr);
    char stamp[32] = "";
    if (std::tm* lt = std::localtime(&now)) {
      std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", lt);
    }
    fs::create_directories(dir, ec);
    // Remembered so the UI can report the size of the file actually being
    // written. Mutable because the args builder is const; the alternative was
    // recomputing the timestamp for display, which would drift from the real
    // filename.
    lastRecordingPath_ = (dir / (stem.string() + "_" + stamp + ext)).string();
    return lastRecordingPath_;
  }

  std::vector<std::string> buildOutputStreamArgs(int outputIndex,
                                                 int width,
                                                 int height,
                                                 double fpsHint,
                                                 const std::string& videoInputPath,
                                                 const std::string& audioInputPath = {}) const {
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
    if (!key.empty() && outputStreamProtocolIsRtmp(protocol)) {
      if (url.back() != '/') url += '/';
      url += key;
    }
    // Recording resolves to a real path with a timestamp stamped on so a
    // second take can never overwrite the first.
    const bool toFile = outputStreamProtocolIsFile(protocol);
    if (toFile) {
      url = resolveRecordingPath(url);
    }
    url = applySrtUrlParameters(output, url);
    int bitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    int bufferKbps = std::clamp(bitrateKbps * 2, 1000, 100000);
    double fps = outputStreamProtocolIsFile(protocol)
      ? recordingFps(fpsHint) : outputStreamFps(fpsHint);
    // GOP was hardcoded to one second. A keyframe every second is a lot of
    // bitrate spent on I-frames; most platforms want 2s, and some (YouTube)
    // require <= 4s. Now the operator's setting.
    int gop = std::max(1, static_cast<int>(std::lround(
      fps * std::clamp(output.streamKeyframeSeconds, 1, 10))));
    // RTMPS is RTMP over TLS — same FLV muxer. It used to normalize to "srt"
    // and land here as mpegts, which no RTMP server would accept.
    // A recording follows its own EXTENSION -- an .mp4 written as mpegts is a
    // file most editors will not open.
    std::string mux = outputStreamProtocolIsRtmp(protocol) ? "flv" : "mpegts";
    if (toFile) {
      const std::string ext = toLower(fs::path(url).extension().string());
      mux = (ext == ".mov")  ? "mov"
          : (ext == ".mkv")  ? "matroska"
          : (ext == ".ts")   ? "mpegts"
          : (ext == ".avi")  ? "avi"
                             : "mp4";
    }
    std::string colorSpace = normalizeOutputColorSpace(output.outputColorSpace);
    std::ostringstream fpsText;
    if (toFile) {
      fpsText << exactRateToken(fps);
    } else {
      fpsText << std::fixed << std::setprecision(2) << fps;
    }

    std::vector<std::string> args {
      "ffmpeg",
      "-hide_banner",
      "-nostdin",
      "-loglevel", "error",
      // Sized in FRAMES, and these frames are raw BGRA: at 4K one packet is
      // 33MB, so the old flat 2048 authorised sixty-eight GIGABYTES of input
      // queue. When the encoder fell behind (it does at 4K) ffmpeg grew to
      // 1.5GB rather than applying backpressure, and the app kept shovelling.
      // Cap the queue by BYTES instead so the ceiling is the same at every
      // raster, and a slow encoder pushes back on the writer as it should.
      "-thread_queue_size", std::to_string(std::clamp(
        (256 * 1024 * 1024) /
          std::max(1, width * height * 4), 8, 512)),
      "-f", "rawvideo",
      "-pix_fmt", "bgra",
      "-video_size", std::to_string(width) + "x" + std::to_string(height),
      "-framerate", fpsText.str(),
      // KNOWN LIMITATION, measured twice. -framerate is a PROMISE the capture
      // path cannot keep: at 4K it delivers ~13fps against a declared 30,
      // because each frame is 33MB of raw BGRA down a pipe. Every undelivered
      // frame shortens the file, so a 20-second take lands as 8.07s and plays
      // ~2.5x fast.
      //
      // -use_wallclock_as_timestamps is the textbook fix and works when ffmpeg
      // is driven directly, but NOT here: retested after the deadlock and
      // -shortest bugs were fixed, it made the same take 2.17s. Do not reach
      // for it again without re-measuring.
      //
      // The real fix is to move fewer bytes -- NV12 is 12bpp against BGRA's 32,
      // which is 2.7x less down the pipe -- or to record a smaller raster.
      "-i", videoInputPath.empty() ? "pipe:0" : videoInputPath,
      // Real programme audio when a pipe is available, silence only as a
      // fallback. anullsrc was UNCONDITIONAL on Windows, so every SRT stream,
      // RTMP stream and file recording carried a silent audio track while the
      // deck audio sat buffered and unread. NDI was unaffected because it
      // reads those buffers directly.
      "-f", audioInputPath.empty() ? "lavfi" : "s16le"
    };
    if (audioInputPath.empty()) {
      // lavfi takes its rate and layout INSIDE the graph description. Passing
      // -ar/-ac as input options to it is an error on current ffmpeg
      // ("Option sample_rate not found") and kills the whole encode, so the
      // silence fallback could never have run on this build.
      args.insert(args.end(), {"-i", "anullsrc=r=48000:cl=stereo"});
    } else {
      // s16le carries no header, so the rate and layout must be stated or
      // ffmpeg guesses and the audio plays at the wrong speed.
      args.insert(args.end(), {"-ar", "48000", "-ac", "2", "-i", audioInputPath});
    }
    args.insert(args.end(), {
      "-map", "0:v:0",
      "-map", "1:a:0",
      "-pix_fmt", "yuv420p"
    });
    if (toFile) {
      // Normally the composite is scaled to the recording raster on the GPU
      // before readback, so the frames arriving here are already the right
      // size and this filter is absent. It is needed only where the captured
      // frame is SHARED with NDI, DeckLink, Spout, ST 2110 or the output delay:
      // those must have the programme raster, so the capture keeps it and the
      // encoder resizes instead. Slower than the GPU blit, and correct -- the
      // alternative was silently sending every one of them at the recording's
      // size.
      const int wantW = project_.recordingWidth;
      const int wantH = project_.recordingHeight;
      if (wantW > 0 && wantH > 0 && (wantW != width || wantH != height)) {
        args.insert(args.end(),
                    {"-vf", "scale=" + std::to_string(wantW) + ":" +
                            std::to_string(wantH) + ":flags=bicubic"});
      }
      const std::vector<std::string> codecArgs = recordingCodecArgs();
      args.insert(args.end(), codecArgs.begin(), codecArgs.end());
    } else {
      args.insert(args.end(),
                  {"-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"});
    }
    if (!toFile) {
      // zerolatency exists to cut STREAM delay: it disables lookahead and
      // B-frames, which costs real quality. A recording is not watched live,
      // so it keeps the encoder's normal behaviour.
      args.insert(args.end(), {"-tune", "zerolatency"});
    }
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
    args.insert(args.end(), {"-fflags", "+genpts"});
    if (toFile) {
      // A real tmcd track, so the take can be conformed against a running
      // order. mov and mp4 both carry it; ';' in the value is the drop-frame
      // flag. Without this the file has no timecode at all, which on its own
      // disqualifies it as a deliverable.
      args.insert(args.end(), {"-timecode", recordingStartTimecode(fps)});
    }
    if (!toFile || recordingCodecUsesBitrate()) {
      // Rate control belongs to the long-GOP codecs only. A mezzanine codec is
      // constant quality chosen by PROFILE; handing prores_ks a -b:v/-maxrate
      // is at best ignored and at worst refused.
      args.insert(args.end(), {
        "-g", std::to_string(gop),
        "-b:v", std::to_string(bitrateKbps) + "k",
        "-maxrate", std::to_string(bitrateKbps) + "k",
        "-bufsize", std::to_string(bufferKbps) + "k"
      });
    }
    if (toFile && !recordingCodecUsesBitrate()) {
      // Uncompressed audio alongside a mezzanine picture: a facility expects
      // PCM in a ProRes/DNx MOV, not a lossy AAC track.
      args.insert(args.end(), {"-c:a", "pcm_s16le", "-ar", "48000", "-ac", "2"});
    } else {
      args.insert(args.end(), {
        "-c:a", "aac",
        "-b:a", std::to_string(std::clamp(output.streamAudioBitrateKbps, 32, 512)) + "k",
        "-ar", "48000",
        "-ac", "2"
      });
    }
    if (toFile) {
      // -shortest ONLY for the anullsrc fallback, whose silence is INFINITE:
      // without it the recording never ends when the video pipe closes, it
      // keeps muxing silence forever (MEASURED: a 2s source produced a 26MB
      // file and was still growing when killed).
      //
      // It must NOT be applied to the real audio pipe. With a live pipe that
      // has not yet delivered a packet, -shortest lets ffmpeg treat the output
      // as already finished: it went on draining our video pipe -- eleven
      // GIGABYTES of it -- while reporting frame=0 and leaving the file at its
      // bare header. That pipe ends on its own when we close it at stop, so
      // there is nothing for -shortest to do here anyway.
      if (audioInputPath.empty()) {
        args.insert(args.end(), {"-shortest"});
      }
      // FRAGMENTED mp4, deliberately NOT +faststart. faststart rewrites the
      // whole file at close to move the moov atom to the front; shutdown
      // closes stdin and force-kills after 500ms, which on a show-length
      // recording lands mid-rewrite and leaves a file with NO moov atom at
      // all. MEASURED: ffprobe on such a file reports "moov atom not found"
      // and it will not open anywhere. A fragmented file is valid at every
      // instant, so a killed -- or crashed -- recording is still playable up
      // to the last keyframe. That matters more here than the marginal
      // compatibility faststart buys.
      if (mux == "mp4" || mux == "mov") {
        // +frag_keyframe starts a fragment at every keyframe. On a long-GOP
        // codec that is once per GOP, which is what we want. On an INTRA codec
        // -- ProRes, DNxHR -- every frame is a keyframe, so it would emit a
        // fragment per frame and bloat the file with thousands of moof boxes.
        // Those rely on the time-based cap alone.
        args.insert(args.end(), {"-movflags",
          recordingCodecUsesBitrate()
            ? "+frag_keyframe+empty_moov+default_base_moof"
            : "+empty_moov+default_base_moof"});
        // A fragment lands on every keyframe -- ONE PER GOP, and the GOP is in
        // FRAMES. At 4K the encoder runs well under realtime, so a 120-frame
        // GOP is most of a minute of wall clock: a short take reached its
        // second keyframe never, no fragment was ever emitted, and the moov sat
        // in the muxer's buffer until the process died. MEASURED: every
        // recording under a minute was exactly 28 bytes on disk -- the ftyp box
        // and nothing else. A time-based fragment cap makes the file grow on a
        // schedule the operator can actually observe.
        args.insert(args.end(), {"-frag_duration", "2000000"});
      }
      // Flush as we go. This was on the network branch only, on the reasoning
      // that latency is a streaming concern -- but an unflushed RECORDING is
      // worse than a late one: the bytes exist nowhere until the muxer decides
      // to write them, so a crash mid-show loses a take that looked healthy.
      args.insert(args.end(), {"-flush_packets", "1"});
    } else {
      // Low-latency muxing, for network sinks only. resend_headers is an
      // mpegts option and the mp4 muxer rejects it outright.
      args.insert(args.end(), {
        "-max_interleave_delta", "0",
        "-flush_packets", "1",
        "-muxdelay", "0",
        "-muxpreload", "0"
      });
      if (mux == "mpegts") {
        args.insert(args.end(), {"-mpegts_flags", "+resend_headers"});
      }
    }
    args.insert(args.end(), {"-f", mux, url});
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
      const char* reason,
      // An AUDIO write failure must not abort the take. The egress-wide failed
      // flag tears the encoder down and opens a NEW file, so a transient on the
      // audio pipe -- which happens around take boundaries, where ffmpeg has
      // connected but is being replaced -- cost the operator the top of the
      // next recording as a stray 0.05-second fragment. MEASURED: the teardown
      // reason was this failure path, twice across three takes.
      //
      // Video is the essence. Losing audio is a defect worth reporting, not
      // worth throwing the recording away for.
      bool fatal = true) {
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
      if (!fatal) {
        return false;   // this pipe gives up; the take carries on without it
      }
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
    writer->audioPipeFd.store(audioPipeFd, std::memory_order_release);
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

        // Audio is handed to its own thread (see OutputStreamWriterState): the
        // video write below BLOCKS, and doing both here is what deadlocked the
        // encoder.
        if (!packet.audioSamples.empty()) {
          {
            std::lock_guard<std::mutex> lock(writer->audioMutex);
            // Bounded: about two seconds of 48k stereo. If the audio pipe ever
            // does stall, drop the oldest rather than grow without limit -- a
            // gap in a recording beats the app eating memory during a show.
            constexpr std::size_t kMaxPendingSamples = 48000 * 2 * 2;
            writer->pendingAudio.insert(writer->pendingAudio.end(),
                                        packet.audioSamples.begin(),
                                        packet.audioSamples.end());
            if (writer->pendingAudio.size() > kMaxPendingSamples) {
              writer->pendingAudio.erase(
                writer->pendingAudio.begin(),
                writer->pendingAudio.begin() +
                  static_cast<std::ptrdiff_t>(writer->pendingAudio.size() -
                                              kMaxPendingSamples));
            }
          }
          writer->audioCv.notify_one();
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
      // Nothing more will be queued, so release anyone waiting on audio.
      {
        std::lock_guard<std::mutex> lock(writer->mutex);
        writer->stop = true;
      }
      writer->audioCv.notify_all();
    });
    writer->audioThread = std::thread([writer]() {
      std::vector<std::int16_t> chunk;
      for (;;) {
        {
          std::unique_lock<std::mutex> lock(writer->audioMutex);
          writer->audioCv.wait(lock, [&]() {
            if (!writer->pendingAudio.empty()) {
              return true;
            }
            std::lock_guard<std::mutex> stopLock(writer->mutex);
            return writer->stop;
          });
          if (writer->pendingAudio.empty()) {
            break;   // stopping, and nothing left to flush
          }
          chunk.swap(writer->pendingAudio);
          writer->pendingAudio.clear();
        }
        // Read the fd fresh each time: on Windows it is assigned once ffmpeg
        // connects to the named pipe, which happens after this thread starts.
        const int audioFd = writer->audioPipeFd.load(std::memory_order_acquire);
        if (audioFd < 0) {
          continue;   // encoder has not opened its end yet; drop this chunk
        }
        const std::uint8_t* audioBytes =
          reinterpret_cast<const std::uint8_t*>(chunk.data());
        const std::size_t audioByteCount = chunk.size() * sizeof(std::int16_t);
        if (!writeOutputStreamBytesBestEffort(
              writer, audioFd, audioBytes, audioByteCount,
              "stream audio stopped", /*fatal=*/false)) {
          break;
        }
        {
          std::lock_guard<std::mutex> lock(writer->mutex);
          writer->audioBytesWritten += static_cast<std::uint64_t>(audioByteCount);
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
    // Captured BEFORE the platform split and acted on after it: the remux is
    // not a Windows feature, and it lived inside the #ifdef so a macOS or
    // Linux recording stayed fragmented forever.
    const bool remuxThisTake = runtime.streamToFile && project_.recordingRemuxOnStop;
    const std::string remuxPath = lastRecordingPath_;
#ifdef _WIN32
    // Close the audio pipe's SERVER end. Once ffmpeg connected, the handle was
    // adopted by a CRT fd and closing that fd closes it -- but if the encoder
    // never connected (bad URL, instant exit) the handle is still ours, and
    // leaking one per stream start would accumulate over a long show.
    if (runtime.streamAudioPipeHandle &&
        runtime.streamWriter &&
        runtime.streamWriter->audioPipeFd.load(std::memory_order_acquire) < 0) {
      CloseHandle(reinterpret_cast<HANDLE>(runtime.streamAudioPipeHandle));
    }
    runtime.streamAudioPipeHandle = nullptr;
#endif
    auto writer = runtime.streamWriter;
    runtime.streamWriter.reset();
    if (writer) {
      {
        std::lock_guard<std::mutex> lock(writer->mutex);
        writer->stop = true;
        writer->hasPendingPacket = false;
      }
      writer->cv.notify_all();
      writer->audioCv.notify_all();
    }
#ifdef _WIN32
    // Close the write pipe first so ffmpeg gets EOF on stdin and exits gracefully
    if (runtime.streamProcess.writeFd >= 0) {
      _close(runtime.streamProcess.writeFd);
      runtime.streamProcess.writeFd = -1;
    }
    // EOF on the AUDIO input, BEFORE joining its thread. Two reasons, and the
    // order is load-bearing for both.
    //
    // 1. ffmpeg needs EOF on input 1 or it never reaches its finalize: closing
    //    stdin only ends input 0, so the grace period below always expired into
    //    a force-kill with the mp4 header still unflushed. MEASURED: an
    //    eight-second recording produced a 28-byte file, the ftyp box alone.
    // 2. The audio writer BLOCKS on a full pipe. Joining it before closing its
    //    fd hangs the main thread for as long as the encoder is wedged -- which
    //    is exactly when a stop matters. Closing first makes the blocked write
    //    fail immediately and the thread exit.
    if (writer) {
      const int audioFd = writer->audioPipeFd.exchange(-1, std::memory_order_acq_rel);
      if (audioFd >= 0) {
        _close(audioFd);
      }
    }
    if (writer && writer->thread.joinable()) {
      writer->thread.join();
    }
    if (writer && writer->audioThread.joinable()) {
      writer->audioThread.join();
    }
    // Give ffmpeg a moment to exit gracefully before force-killing. A network
    // sink has nothing to finalize, so 500ms is plenty; a file has to flush and
    // write its trailer, and killing it early is exactly what corrupts the take
    // the operator is least able to redo.
    if (runtime.streamProcess.running()) {
      WaitForSingleObject(runtime.streamProcess.hProcess,
                          runtime.streamToFile ? 8000 : 500);
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
    if (writer && writer->audioThread.joinable()) {
      writer->audioThread.join();
    }
    if (streamPid > 0) {
      int status = 0;
      // Same asymmetry as the Windows branch: a file has a trailer to write,
      // and SIGKILLing the muxer mid-finalize is what leaves an unopenable take.
      Uint64 deadline = SDL_GetTicks() + (runtime.streamToFile ? 8000 : 500);
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
    if (remuxThisTake) {
      queueRecordingRemux(remuxPath);
    }
    runtime.streamSpec.clear();
    runtime.streamToFile = false;
    // Per TAKE, not per session: take two must start its own frame clock.
    runtime.recordPacerStartMs = 0;
    runtime.recordFramesWritten = 0;
    runtime.lastSegmentSizeCheckMs = 0;
    runtime.lastDropWarnMs = 0;
    runtime.recordDroppedFrames = 0;
    runtime.streamCommand.clear();
    runtime.streamFrameBuffer.clear();
    runtime.streamAudioReadSamplesByDeck.clear();
    runtime.streamAudioSampleRemainder = 0.0;
    runtime.streamFrameWidth = 0;
    runtime.streamFrameHeight = 0;
    runtime.lastStreamCaptureSentAtMs = 0;
    resetOutputStreamFpsTelemetry(runtime);
  }

  // Stop a disabled output's encoder. Called from the render loop, which is the
  // ONLY place that runs for a disabled output: renderOutputWindow holds the
  // other teardown but the loop never calls it once the output is off, and
  // clearDisabledOutputWindow returns immediately for a stream output because
  // it has no window. So nothing stopped the encoder -- and turning an output
  // off is exactly what STOP RECORDING does. MEASURED: ffmpeg ran on holding
  // the file open, outlived the app, and the next start reused it, appending
  // three separate takes into one file (8s -> 20s -> 32s).
  //
  // Guarded so the common case (an output that has been off all show) costs a
  // pointer check per frame rather than a full teardown.
  void stopEgressForDisabledOutput(int outputIndex) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      return;
    }
    const bool egressAlive =
      runtime->streamWriter != nullptr
#ifdef _WIN32
      || runtime->streamProcess.running();
#else
      || runtime->streamPid > 0;
#endif
    if (!egressAlive) {
      return;
    }
    stopOutputStreamRuntime(*runtime);
    resetOutputStreamFpsTelemetry(*runtime);
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
  // A Windows child gets exactly one piped stdin, which video already uses, so
  // audio needs its own channel. A NAMED PIPE is the way: ffmpeg opens it by
  // path like any other input, and converting the handle to a CRT fd afterwards
  // means the existing writer code works unchanged.
  std::string createOutputAudioPipe(OutputRuntime& runtime, int outputIndex) {
    // UNIQUE PER START. The name used to be just pid-outputIndex, i.e. the same
    // string for every take on an output, and the pipe is created with
    // nMaxInstances = 1. Starting a second recording while the previous
    // instance was still closing therefore failed, and the encoder died and was
    // retried until the old handle finally went away. MEASURED: take one wrote
    // a single clean file; take two produced two stray 0.055-second fragments
    // first. The POSIX fifo path already stamps its name for this reason.
    static std::atomic<unsigned> pipeSerial {0};
    const std::string name = "\\\\.\\pipe\\deckboy-audio-" +
                             std::to_string(GetCurrentProcessId()) + "-" +
                             std::to_string(outputIndex) + "-" +
                             std::to_string(pipeSerial.fetch_add(1,
                                              std::memory_order_relaxed));
    HANDLE h = CreateNamedPipeA(
      name.c_str(),
      PIPE_ACCESS_OUTBOUND,
      PIPE_TYPE_BYTE | PIPE_WAIT,
      1,                      // one client: ffmpeg
      1 << 20, 1 << 20,       // 1MB each way, ample for 48k stereo
      0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      return {};
    }
    runtime.streamAudioPipeHandle = reinterpret_cast<void*>(h);
    return name;
  }

  // ConnectNamedPipe BLOCKS until ffmpeg opens its end, so it cannot run on the
  // main thread -- arming an output would freeze the UI until the encoder got
  // round to it. Detached, and it publishes the fd when the connection lands.
  void connectOutputAudioPipeAsync(OutputRuntime& runtime) {
    HANDLE h = reinterpret_cast<HANDLE>(runtime.streamAudioPipeHandle);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return;
    auto writer = runtime.streamWriter;
    std::thread([h, writer]() {
      const BOOL ok = ConnectNamedPipe(h, nullptr);
      if (!ok && GetLastError() != ERROR_PIPE_CONNECTED) {
        CloseHandle(h);
        return;
      }
      const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), 0);
      if (fd < 0) {
        CloseHandle(h);
        return;
      }
      if (writer) {
        writer->audioPipeFd.store(fd, std::memory_order_release);
      }
    }).detach();
  }

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
    // Discard captured input that predates this stream. Without it a recording
    // starts by muxing up to four seconds of STALE microphone -- the buffer
    // fills while nothing is armed -- and the mic stays that far out of sync
    // for the whole take.
    {
      std::lock_guard<std::mutex> lock(audioInputMixMutex_);
      audioInputMixBuffer_.clear();
    }
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

    // Must be the SAME rate the video is written at, or audio and video drift:
    // this decides how many samples accompany one frame.
    const double fps =
      (outputIndex >= 0 && outputIndex < static_cast<int>(project_.outputs.size()) &&
       outputStreamProtocolIsFile(
         normalizeOutputStreamProtocol(project_.outputs[outputIndex].streamProtocol)))
        ? recordingFps(fpsHint)
        : outputStreamFps(fpsHint);
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

    // Live input, summed with the deck mix. This reaches the STREAM and the
    // RECORDING but never the speakers, which is deliberate -- see
    // Project::audioInputToProgram.
    if (project_.audioInputToProgram) {
      std::lock_guard<std::mutex> lock(audioInputMixMutex_);
      const std::size_t take =
        std::min(audioInputMixBuffer_.size(), static_cast<std::size_t>(interleavedSamples));
      for (std::size_t i = 0; i < take; ++i) {
        mixed[i] += static_cast<std::int32_t>(audioInputMixBuffer_[i]);
      }
      // Consume what was mixed. Leaving it would replay the same audio on the
      // next frame and the microphone would stutter.
      audioInputMixBuffer_.erase(audioInputMixBuffer_.begin(),
                                 audioInputMixBuffer_.begin() +
                                   static_cast<std::ptrdiff_t>(take));
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
    // A RECORDING never restarts mid-take. Restarting closes the current file
    // and opens a new one, so a spec change a few frames in silently splits the
    // take in two and the operator loses the top of it in a stray fragment.
    // MEASURED: every recording produced an extra 0.088-second file -- two
    // frames -- before the real one, because the fps in the spec settles just
    // after the output comes up.
    //
    // A network sink genuinely should re-dial on a parameter change; a file
    // sink should keep writing with the parameters it opened with. The new
    // settings apply to the NEXT take, which is when the operator expects them.
    if (processAlive && !runtime->streamSpec.empty() && runtime->streamToFile) {
      if (output.enabled) {
        setOutputHealthState(outputIndex, OutputHealthState::Live);
      }
      return true;
    }

    stopOutputStreamRuntime(*runtime);
    setOutputHealthState(outputIndex, OutputHealthState::Recovering, "starting stream");
#ifdef _WIN32
    // Windows: video over stdin (pipe:0), audio over a named pipe. The pipe
    // must EXIST before ffmpeg starts, or its open fails and the encoder dies
    // with a confusing "no such file" on an input the operator never chose.
    const std::string audioPipeName = createOutputAudioPipe(*runtime, outputIndex);
    std::vector<std::string> args =
      buildOutputStreamArgs(outputIndex, width, height, fpsHint, "", audioPipeName);
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
    runtime->streamToFile = outputStreamProtocolIsFile(
      normalizeOutputStreamProtocol(project_.outputs[outputIndex].streamProtocol));
    runtime->streamCommand = shellCommandString(args);
    runtime->streamFrameWidth = width;
    runtime->streamFrameHeight = height;
    runtime->streamAudioSampleRemainder = 0.0;
    primeOutputStreamAudioReadPositions(outputIndex, *runtime);
    runtime->streamFrameBuffer.clear();
    runtime->streamStartFailed = false;
    runtime->streamRestartBlockedUntilMs = 0;
    startOutputStreamWriter(*runtime);
#ifdef _WIN32
    // Must come AFTER the writer exists: the connect thread publishes the fd
    // into it once ffmpeg opens the pipe.
    connectOutputAudioPipeAsync(*runtime);
#endif
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

  // ── Program-monitor tap ───────────────────────────────────────────────────
  // The control window used to preview the *decoder's* frame. On the GPU
  // zero-copy path that meant a full-resolution hwframe download, which is far
  // too expensive per frame, so it ran at ~10fps and visibly trailed the
  // program feed. Instead, sample the output's finished composite here — on
  // the output's own render pass, once per presented frame — scaled down to a
  // preview-sized target first. The readback is ~0.5MB instead of 3-12MB, so
  // it can run every frame, and because it is taken from the same pass that
  // presents, the preview cannot drift away from the output.
  //
  // Tapped BEFORE warp/AOI/edge-blend (those are applied at present time in
  // presentOutputCompositorToWindow), which keeps the warp editor's handle
  // overlay meaningful — it still draws over an unwarped image.
  static constexpr int kPreviewTapMaxW = 640;
  static constexpr int kPreviewTapMaxH = 360;

  bool captureOutputPreviewTap(OutputRuntime& runtime, const SDL_Rect& sourceRect) {
    if (!runtime.outputRenderer || !runtime.compositorTexture) {
      return false;
    }
    int srcW = std::max(1, sourceRect.w);
    int srcH = std::max(1, sourceRect.h);
    // Fit the output's aspect inside the preview budget; never upscale.
    double scale = std::min({1.0,
                             static_cast<double>(kPreviewTapMaxW) / srcW,
                             static_cast<double>(kPreviewTapMaxH) / srcH});
    int tapW = std::max(2, static_cast<int>(std::lround(srcW * scale)) & ~1);
    int tapH = std::max(2, static_cast<int>(std::lround(srcH * scale)) & ~1);

    if (runtime.previewTapTexture &&
        (runtime.previewTapTextureW != tapW || runtime.previewTapTextureH != tapH)) {
      SDL_DestroyTexture(runtime.previewTapTexture);
      runtime.previewTapTexture = nullptr;
      runtime.previewTapTextureW = 0;
      runtime.previewTapTextureH = 0;
    }
    if (!runtime.previewTapTexture) {
      runtime.previewTapTexture = deckboyCreateTexture(runtime.outputRenderer,
                                                       SDL_PIXELFORMAT_RGBA32,
                                                       SDL_TEXTUREACCESS_TARGET,
                                                       tapW, tapH);
      if (!runtime.previewTapTexture) {
        return false;
      }
      runtime.previewTapTextureW = tapW;
      runtime.previewTapTextureH = tapH;
    }

    SDL_Texture* previousTarget = SDL_GetRenderTarget(runtime.outputRenderer);
    SDL_SetRenderTarget(runtime.outputRenderer, runtime.previewTapTexture);
    // Linear only for this downscale — nearest-neighbour minification of moving
    // video shimmers badly. The compositor's own nearest mode is restored
    // immediately so the real output present is untouched.
    SDL_ScaleMode previousScale = SDL_SCALEMODE_NEAREST;
    SDL_GetTextureScaleMode(runtime.compositorTexture, &previousScale);
    SDL_SetTextureScaleMode(runtime.compositorTexture, SDL_SCALEMODE_LINEAR);
    SDL_SetRenderDrawColor(runtime.outputRenderer, 0, 0, 0, 255);
    SDL_RenderClear(runtime.outputRenderer);
    SDL_FRect src {static_cast<float>(sourceRect.x), static_cast<float>(sourceRect.y),
                   static_cast<float>(srcW), static_cast<float>(srcH)};
    SDL_RenderTexture(runtime.outputRenderer, runtime.compositorTexture, &src, nullptr);

    bool ok = false;
    if (SDL_Surface* captured = SDL_RenderReadPixels(runtime.outputRenderer, nullptr)) {
      std::size_t stride = static_cast<std::size_t>(captured->w) * 4u;
      std::size_t bytes = stride * static_cast<std::size_t>(captured->h);
      if (runtime.previewTapPixels.size() != bytes) {
        runtime.previewTapPixels.resize(bytes);
      }
      ok = !runtime.previewTapPixels.empty() &&
           SDL_ConvertPixels(captured->w, captured->h,
                             captured->format, captured->pixels, captured->pitch,
                             SDL_PIXELFORMAT_RGBA32,
                             runtime.previewTapPixels.data(),
                             static_cast<int>(stride));
      if (ok) {
        runtime.previewTapW = captured->w;
        runtime.previewTapH = captured->h;
        ++runtime.previewTapSerial;
      }
      SDL_DestroySurface(captured);
    }

    SDL_SetTextureScaleMode(runtime.compositorTexture, previousScale);
    SDL_SetRenderTarget(runtime.outputRenderer, previousTarget);
    return ok;
  }

  void releaseOutputPreviewTap(OutputRuntime& runtime) {
    if (runtime.previewTapTexture) {
      SDL_DestroyTexture(runtime.previewTapTexture);
      runtime.previewTapTexture = nullptr;
    }
    runtime.previewTapTextureW = 0;
    runtime.previewTapTextureH = 0;
    runtime.previewTapPixels.clear();
    runtime.previewTapW = 0;
    runtime.previewTapH = 0;
    runtime.previewTapSerial = 0;
  }

  // Which output the control window's program monitor mirrors: the focused
  // deck's primary output, when that output is actually compositing.
  std::optional<int> previewTapOutputIndex() const {
    auto primary = primaryOutputIndexForDeck(project_.focusedDeckIndex);
    if (!primary) {
      return std::nullopt;
    }
    int outputIndex = *primary;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return std::nullopt;
    }
    if (!project_.outputs[outputIndex].enabled) {
      return std::nullopt;
    }
    return outputIndex;
  }

  // How the recording frame gets off the GPU. AUTO takes the asynchronous
  // staging ring where the backend has one (D3D11 today) and falls back to the
  // plain synchronous read where it does not -- which is macOS and Linux.
  //
  // DECKBOY_EGRESS_READBACK=sync forces the fallback on a machine that HAS the
  // fast path, which is the only way to measure the other platforms' behaviour
  // from here. Measured on a 4K programme, 14s takes:
  //
  //     1080p50     700/700      1080p59.94  838/839
  //     2160p25     349/350      2160p50     326/700
  //
  // So the fallback is frame-exact for every raster and rate a show is likely
  // to deliver, and only 4K above 30p outruns it. The cost is the GPU round
  // trip inside SDL_RenderReadPixels (16.8-18.8ms at 4K); the CPU-side format
  // conversion is 2.9ms of it, so there is nothing to win by tightening this
  // side of the call. A ping-pong between two scale targets -- read the one the
  // GPU finished with last frame -- was tried and MEASURED NO BETTER (311 vs
  // 326 frames), because SDL copies and maps in a single call and the map waits
  // on the copy regardless of the texture's age. Closing the 4K gap needs a
  // real per-backend async path (GL pixel buffer objects, a Metal blit with a
  // completion handler), not a rearrangement of SDL calls.
  static constexpr int kEgressReadbackAuto = 0;
  static constexpr int kEgressReadbackSync = 1;

  static int egressReadbackMode() {
    static const int mode = [] {
      const char* env = std::getenv("DECKBOY_EGRESS_READBACK");
      if (!env || !*env) return kEgressReadbackAuto;
      return deckboy::core::utils::toLower(env) == "sync" ? kEgressReadbackSync
                                                          : kEgressReadbackAuto;
    }();
    return mode;
  }

  // The renderer an OUTPUT window gets. This is deliberately not the default on
  // every platform: recording reads each frame back off the GPU, and only two
  // backends can do that without stalling the render thread -- D3D11, through
  // its staging ring, and SDL_GPU, through SDL_DownloadFromGPUTexture. Anything
  // else falls back to a synchronous read that costs 16.8-18.8ms at 4K, which
  // 60fps (16.7ms a frame in total) simply cannot afford.
  //
  // So Windows keeps its default D3D11 renderer, and macOS and Linux ask for
  // "gpu" -- Metal and Vulkan underneath -- falling back to the default if it
  // will not create. DECKBOY_OUTPUT_RENDERER overrides the choice by driver
  // name, both to force "gpu" on a Windows desk (which is how this path gets
  // tested at all) and to get an operator back to the old behaviour in a hurry
  // if a driver misbehaves in the field.
  SDL_Renderer* createOutputRenderer(SDL_Window* window) {
    if (const char* forced = std::getenv("DECKBOY_OUTPUT_RENDERER")) {
      if (*forced) {
        if (SDL_Renderer* r = SDL_CreateRenderer(window, forced)) {
          return r;
        }
        std::cerr << "output-renderer: DECKBOY_OUTPUT_RENDERER=" << forced
                  << " could not be created, falling back" << std::endl;
      }
    }
#if !defined(_WIN32)
    if (SDL_Renderer* r = SDL_CreateRenderer(window, "gpu")) {
      return r;
    }
#endif
    if (SDL_Renderer* r = SDL_CreateRenderer(window, nullptr)) {
      return r;
    }
    return SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
  }

  bool captureOutputFrameForEgress(int outputIndex,
                                   OutputRuntime& runtime,
                                   const SDL_Rect& requestedRect,
                                   double fpsHint) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size()) || !runtime.outputRenderer) {
      return false;
    }
    Uint64 nowMs = SDL_GetTicks();
    // A recording captures at ITS OWN rate. This used the stream rate, which
    // follows the display refresh or a 30fps default -- so asking for a 50 or
    // 59.94 recording still only ever captured ~30 frames a second and the file
    // came out short no matter how fast the readback and the encoder were.
    double captureFps =
      (outputIndex >= 0 && outputIndex < static_cast<int>(project_.outputs.size()) &&
       outputStreamProtocolIsFile(
         normalizeOutputStreamProtocol(project_.outputs[outputIndex].streamProtocol)))
        ? recordingFps(fpsHint)
        : outputStreamFps(fpsHint);
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

    // ── Scale to the RECORDING raster on the GPU, before readback ───────────
    // Reading the full programme raster back and letting something downstream
    // resize it is what made a broadcast rate unreachable: at 4K the readback
    // alone costs 21-24ms per frame (MEASURED), i.e. a ~45fps ceiling before
    // the encoder is handed anything, on the render thread. Blitting the
    // composite into a target already at the delivery raster is a GPU
    // operation, and the readback then moves (recW*recH)/(progW*progH) of the
    // bytes -- a quarter, for 1080 off a 4K programme.
    //
    // Only the file-recording output takes this path. NDI, DeckLink and Spout
    // read the same captured frame and must keep the programme raster.
    SDL_Texture* readbackTarget = runtime.compositorTexture;
    SDL_Rect readbackRect = captureRect;
    // EVERY file recording takes this path, including the default one that
    // follows the input raster. It was gated on an explicit recording size,
    // which meant the DEFAULT configuration -- the one most shows will use --
    // fell through to the synchronous readback and its 21ms-per-frame stall at
    // 4K. MEASURED: 320 frames where 360 were owed, purely from that.
    //
    // The blit is 1:1 when no resize is asked for, and nearly free; its real
    // job is putting the frame into a BGRA target we own, which is what the
    // asynchronous staging readback needs.
    // The captured frame is SHARED. NDI, DeckLink, Spout, ST 2110 and the output
    // delay buffer all read `latestCapturedFrame`, and they must have the
    // PROGRAMME raster. Scaling it to the recording's raster silently dropped
    // every one of them to the recording size -- a 4K NDI send became 1080
    // because someone armed a 1080 recording on the same output. The comment
    // above claimed this could not happen; the guard did not enforce it, and
    // these are independent per-output flags that can all be on at once.
    //
    // So the fast path is taken only when the recording is the frame's ONLY
    // consumer, which is the ordinary case. Where it is not, the capture keeps
    // the programme raster and the recording is scaled by the encoder instead
    // (see recordingScaleFilterArgs) -- slower, and correct.
    const OutputTarget& egressOutput = project_.outputs[outputIndex];
    const bool sharedWithOtherEgress =
      egressOutput.ndiEnabled || egressOutput.ndiKeyEnabled ||
      egressOutput.deckLinkEnabled || egressOutput.spoutEnabled ||
      egressOutput.st2110Enabled ||
      std::clamp(egressOutput.outputDelayMs, 0, 5000) > 0;
    const bool scaledEgress =
      outputStreamProtocolIsFile(
        normalizeOutputStreamProtocol(egressOutput.streamProtocol)) &&
      !sharedWithOtherEgress &&
      runtime.compositorTexture;
    const int readbackMode = egressReadbackMode();
    if (scaledEgress) {
      // 0 means "follow the input", which is the default and the sane one: a
      // recording should look like what went in unless someone says otherwise.
      const int targetW = project_.recordingWidth  > 0 ? project_.recordingWidth  : captureW;
      const int targetH = project_.recordingHeight > 0 ? project_.recordingHeight : captureH;
      if (runtime.egressScaleTexture &&
          (runtime.egressScaleW != targetW || runtime.egressScaleH != targetH)) {
        SDL_DestroyTexture(runtime.egressScaleTexture);
        runtime.egressScaleTexture = nullptr;
      }
      if (!runtime.egressScaleTexture) {
        // The held frame is now the WRONG SIZE. The async path serves the
        // previous picture whenever the staging ring has nothing ready, so
        // without dropping it here the first frames of a take after a raster
        // change are the old raster -- and the encoder is started from the
        // first frame it sees, so it locks to the old size for the whole take.
        // MEASURED: a recording asked for 1280x720 came back 1920x1080.
        runtime.latestCapturedFrame = {};
        // BGRA32 so the staging readback lands in exactly the byte order the
        // encoder is fed (-pix_fmt bgra) and no conversion pass is needed.
        runtime.egressScaleTexture = deckboyCreateTexture(
          runtime.outputRenderer, SDL_PIXELFORMAT_BGRA32,
          SDL_TEXTUREACCESS_TARGET, targetW, targetH);
        runtime.egressScaleW = targetW;
        runtime.egressScaleH = targetH;
      }
      if (runtime.egressScaleTexture) {
        SDL_Texture* savedTarget = SDL_GetRenderTarget(runtime.outputRenderer);
        SDL_SetRenderTarget(runtime.outputRenderer, runtime.egressScaleTexture);
        SDL_SetRenderDrawColor(runtime.outputRenderer, 0, 0, 0, 255);
        SDL_RenderClear(runtime.outputRenderer);
        const SDL_FRect src {static_cast<float>(captureRect.x),
                             static_cast<float>(captureRect.y),
                             static_cast<float>(captureW),
                             static_cast<float>(captureH)};
        SDL_RenderTexture(runtime.outputRenderer, runtime.compositorTexture,
                          &src, nullptr);
        SDL_SetRenderTarget(runtime.outputRenderer, savedTarget);
        readbackTarget = runtime.egressScaleTexture;
        readbackRect = SDL_Rect {0, 0, targetW, targetH};
        captureW = targetW;
        captureH = targetH;
      }
    }

    size_t stride = static_cast<size_t>(captureW) * 4u;
    size_t frameBytes = stride * static_cast<size_t>(captureH);
    if (runtime.latestCapturedFrame.pixels.size() != frameBytes) {
      runtime.latestCapturedFrame.pixels.resize(frameBytes);
    }
    if (runtime.latestCapturedFrame.pixels.empty()) {
      return false;
    }

    bool asyncCaptured = false;
    // ── Asynchronous path ──────────────────────────────────────────────────
    // Only for the scaled recording target, which is a plain RGBA render target
    // we own. Everything else keeps the synchronous readback below.
    if (scaledEgress && readbackMode == kEgressReadbackAuto &&
        readbackTarget == runtime.egressScaleTexture) {
      if (runtime.egressReadback &&
          (runtime.egressReadbackW != captureW || runtime.egressReadbackH != captureH)) {
        deckboy::gpu::destroyStagingReadback(runtime.egressReadback);
        runtime.egressReadback = nullptr;
      }
      if (!runtime.egressReadback && !runtime.egressReadbackUnavailable) {
        runtime.egressReadback = deckboy::gpu::createStagingReadback(
          runtime.outputRenderer, captureW, captureH);
        runtime.egressReadbackW = captureW;
        runtime.egressReadbackH = captureH;
        // A renderer with no asynchronous path says so once. Without this the
        // creation was attempted EVERY FRAME for the whole session, on the
        // render thread, for a null that was never going to change.
        runtime.egressReadbackUnavailable = runtime.egressReadback == nullptr;
      }
      if (runtime.egressReadback) {
        // Straight into the egress buffer. The earlier version allocated and
        // zero-filled an 8MB temporary EVERY FRAME and then walked it twice
        // more (memcpy, then a format conversion) -- about 24MB of pointless
        // traffic per frame on the render thread.
        const bool got = deckboy::gpu::stagingReadbackFrame(
          runtime.egressReadback, runtime.egressScaleTexture,
          runtime.latestCapturedFrame.pixels.data(),
          runtime.latestCapturedFrame.pixels.size(), captureW, captureH);
        // NOT a return: this used to hand the frame straight back, which
        // skipped the orientation rotation below -- so a rotated output
        // recorded UNROTATED on the asynchronous path and rotated on the
        // synchronous one. Falling through means both paths agree.
        asyncCaptured = got;
        // Nothing ready: keep the previous picture. The CFR pacer will repeat
        // it, which is exactly the right behaviour -- a held frame, never a
        // gap in the timeline. The held frame was already rotated when it was
        // captured, so this one DOES return early.
        if (!got && runtime.latestCapturedFrame.width > 0) {
          runtime.lastEgressCaptureAtMs = nowMs;
          return true;
        }
      }
    }

    bool ok = asyncCaptured;
    SDL_Texture* previousTarget = SDL_GetRenderTarget(runtime.outputRenderer);
    if (!asyncCaptured && readbackTarget) {
      SDL_SetRenderTarget(runtime.outputRenderer, readbackTarget);
    }
    // SDL3: read into a temporary surface, then convert into the persistent
    // BGRA egress buffer (SDL2 wrote the requested format directly).
    if (SDL_Surface* captured = asyncCaptured
          ? nullptr
          : SDL_RenderReadPixels(runtime.outputRenderer, &readbackRect)) {
      ok = captured->w == captureW && captured->h == captureH &&
           SDL_ConvertPixels(captured->w, captured->h,
                             captured->format, captured->pixels, captured->pitch,
                             SDL_PIXELFORMAT_BGRA32,
                             runtime.latestCapturedFrame.pixels.data(),
                             static_cast<int>(stride));
      SDL_DestroySurface(captured);
    }
    if (!asyncCaptured && readbackTarget) {
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
    // ── Frame pacing ───────────────────────────────────────────────────────
    // A network stream sends only what is NEW: a duplicate frame costs
    // bandwidth and buys nothing, and the receiver has its own clock.
    //
    // A RECORDING is the opposite. The muxer stamps frames by arrival order at
    // the declared rate, so a frame we fail to deliver does not slow the file
    // down -- it shortens it. That is the whole "20-second take is 8 seconds
    // long and plays 2.5x fast" defect: it was never a timestamp problem, it
    // was a frame-COUNT problem. So the recording delivers exactly
    // rate x elapsed frames, repeating the last picture to cover a gap, which
    // is what a broadcast recorder does and what makes the duration correct by
    // construction instead of dependent on the capture keeping up.
    const bool toFileSink = outputStreamProtocolIsFile(
      normalizeOutputStreamProtocol(project_.outputs[outputIndex].streamProtocol));
    // Roll onto a new file at the operator's cap or the filesystem's. The take
    // keeps running; only the file changes, and the next frame opens the next
    // segment with a fresh timestamped name.
    if (toFileSink && shouldRollRecordingSegment(*runtime)) {
      stopOutputStreamRuntime(*runtime);
      return;
    }
    if (toFileSink) {
      const Uint64 nowMs = SDL_GetTicks();
      if (runtime->recordPacerStartMs == 0) {
        runtime->recordPacerStartMs = nowMs;
        runtime->recordFramesWritten = 0;
      }
      const double rate = std::max(1.0, recordingFps(fpsHint));
      const std::uint64_t owed = static_cast<std::uint64_t>(
        (static_cast<double>(nowMs - runtime->recordPacerStartMs) * rate) / 1000.0) + 1;
      // ── Dropped-frame alarm ──────────────────────────────────────────────
      // The pacer knows exactly what the timeline owes and exactly what has
      // been delivered, so a shortfall is measurable rather than inferred. Both
      // AJA and OBS treat this as a first-class warning, and rightly: a
      // recording that quietly runs short looks perfectly healthy until an
      // editor opens it. Deckboy dropped frames silently, which is how a
      // twenty-second take became eight seconds with nobody the wiser.
      //
      // Reported once a second while it persists, so a wedged disk is loud
      // without spamming the operator every frame.
      // The asynchronous readback is a PIPELINE: the frame handed to the writer
      // is kAsyncReadbackDepth behind the one just rendered, so a perfectly
      // healthy recording sits a constant few frames behind what the pacer says
      // is owed. Reporting that as a drop meant EVERY 4K take raised
      // "RECORDING DROPPING FRAMES - 3 behind" once a second for its whole
      // length and drove the output into an error state -- while the file came
      // out 449 frames of 450. An alarm that cries wolf on every take is worse
      // than no alarm, because the operator learns to ignore the one that
      // matters. MEASURED after this: 4K at 30, 50 and 60 all silent, and the
      // genuinely starved synchronous path at 2160p50 still shouts.
      //
      // The floor is a QUARTER SECOND of frames, not a frame count, because
      // that is the thing an operator can act on: "you are 5 frames behind at
      // 60" is jitter the pacer absorbs, while "you are a quarter second
      // behind" is a disk or an encoder in trouble. A starved capture sits
      // HUNDREDS behind and trips this instantly.
      const std::uint64_t pacerAllowance = std::max<std::uint64_t>(
        static_cast<std::uint64_t>(deckboy::gpu::kAsyncReadbackDepth) + 2u,
        static_cast<std::uint64_t>(rate / 4.0));
      if (owed > runtime->recordFramesWritten + pacerAllowance) {
        const std::uint64_t behind = owed - runtime->recordFramesWritten;
        if (nowMs - runtime->lastDropWarnMs >= 1000) {
          runtime->lastDropWarnMs = nowMs;
          const std::string msg =
            "RECORDING DROPPING FRAMES - " + std::to_string(behind) + " behind";
          setOutputHealthState(outputIndex, OutputHealthState::Error, msg);
          triggerToast(msg);
          showLog("RECORD DROP", msg);
          // Also to stderr. The three lines above reach the operator's screen
          // and the show log, which is the right place for an operator -- but
          // it left the condition invisible to anything reading the process's
          // output: a support bundle, a CI harness, a field diagnosis over ssh.
          // A recording running short is exactly the thing you want in the log
          // you actually have.
          std::cerr << "record-drop: " << msg << " (output " << outputIndex
                    << ")" << std::endl;
        }
        runtime->recordDroppedFrames = behind;
      }
      if (runtime->recordFramesWritten >= owed) {
        return;   // ahead of schedule; nothing is due yet
      }

    } else if (runtime->lastStreamCaptureSentAtMs == frame->capturedAtMs) {
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
    // Cross-platform. This was inside #ifndef _WIN32, so on Windows the packet
    // NEVER carried audio -- the deck mix sat buffered and unread while the
    // encoder muxed silence. Gated on the writer actually having somewhere to
    // put it, which on Windows arrives once ffmpeg connects to the named pipe.
    {
      bool haveAudioSink = false;
#ifdef _WIN32
      haveAudioSink = runtime->streamWriter &&
                      runtime->streamWriter->audioPipeFd.load(
                        std::memory_order_acquire) >= 0;
#else
      haveAudioSink = runtime->streamAudioPipeFd >= 0;
#endif
      if (haveAudioSink) {
        packet.audioSamples = collectOutputAudioFrameSamples(
          outputIndex,
          runtime->streamAudioReadSamplesByDeck,
          runtime->streamAudioSampleRemainder,
          fpsHint);
      }
    }

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
    if (toFileSink) {
      runtime->recordFramesWritten += 1;
    }
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
    if (runtime.egressScaleTexture) {
      SDL_DestroyTexture(runtime.egressScaleTexture);
      runtime.egressScaleTexture = nullptr;
      runtime.egressScaleW = 0;
      runtime.egressScaleH = 0;
    }
    if (runtime.egressReadback) {
      deckboy::gpu::destroyStagingReadback(runtime.egressReadback);
      runtime.egressReadback = nullptr;
      runtime.egressReadbackW = 0;
      runtime.egressReadbackH = 0;
    }
    // Outside the branch above: when the renderer had no asynchronous path
    // there is no handle to free, and clearing the latch only inside it would
    // mean a new renderer never got asked.
    runtime.egressReadbackUnavailable = false;
    releaseOutputPreviewTap(runtime);
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

  // The live mode of the display the operator currently has the program
  // output on: first enabled window-type output's renderer size + that
  // display's refresh rate (the project's explicit refresh override wins).
  // Patterns build at this raster (pixel-precision instruments stay 1:1
  // with the physical display) and animate at this refresh rate, tracking
  // display switches automatically.
  MediaEngine::OutputModeHint primaryOutputMode() {
    MediaEngine::OutputModeHint fallback;
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
      MediaEngine::OutputModeHint mode;
      mode.width = rw;
      mode.height = rh;
      if (project_.outputRefreshRateHz > 0.0) {
        mode.refreshHz = project_.outputRefreshRateHz;  // "unless otherwise set"
      } else if (runtime.outputWindow) {
        SDL_DisplayID display = SDL_GetDisplayForWindow(runtime.outputWindow);
        if (const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(display)) {
          mode.refreshHz = dm->refresh_rate;
        }
      }
      const OutputTarget& output = project_.outputs[i];
      if (output.enabled && output.outputType != "stream") {
        return mode;
      }
      if (fallback.width == 0) {
        fallback = mode;
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
    SDL_AudioStream* newMain = openMainAudioDevice(preferredDeviceName, effectiveName,
                                                   deck.audioOutputChannels);
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
      runtime->mediaEngine->setAudioDeviceChannels(deck.audioOutputChannels);
    } else {
      runtime->mediaEngine = std::make_unique<MediaEngine>(
        runtime->outputRenderer,
        runtime->audioStream,
        [this, deckIndex](const std::vector<std::int16_t>& samples) {
          pushDeckStreamAudioSamples(deckIndex, samples);
          // Capture samples for VU meter (only from focused deck)
          if (deckIndex == project_.focusedDeckIndex) {
            // Streaming carries the programme, so 2110 audio comes from the
            // focused deck's final mix — same rule as the video essence.
            pushSt2110AudioSamples(samples);
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
        // Patterns build pixel-mapped to the live program-output raster and
        // animate at its refresh rate (project override wins).
        [this]() { return primaryOutputMode(); }
      );
      runtime->mediaEngine->setAudioDeviceChannels(deck.audioOutputChannels);
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

  // ── ST 2110-20 egress ──────────────────────────────────────────────────────
  // Same shape as the Spout/DeckLink senders: lazily open on first frame,
  // reopen when the operator changes anything the socket or packetiser depends
  // on, and feed it the BGRA egress capture the other backends already use.
  void sendOutputSt2110Frame(int outputIndex, OutputRuntime& outputRuntime,
                             int width, int height, double fpsHint) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    if (!output.st2110Enabled) {
      return;
    }
    const auto& frameCapture = outputRuntime.latestCapturedFrame;
    if (frameCapture.pixels.empty() || frameCapture.width <= 0 || frameCapture.height <= 0) {
      return;
    }
    // ST 2110-20 carries 4:2:2, so the raster must be an even number of pixels
    // wide — a half pgroup cannot be expressed on the wire.
    const int sendW = frameCapture.width & ~1;
    if (sendW <= 0) {
      return;
    }

    using deckboy::platform::video::St2110Config;
    using deckboy::platform::video::St2110Sampling;
    St2110Config wanted;
    wanted.enabled = true;
    wanted.destinationAddress = trim(output.st2110Address).empty()
      ? std::string("239.20.10.1") : trim(output.st2110Address);
    wanted.interfaceAddress = trim(output.st2110Interface);
    wanted.destinationPort = std::clamp(output.st2110Port, 1, 65535);
    wanted.width = sendW;
    wanted.height = frameCapture.height;
    wanted.frameRate = fpsHint > 1.0 ? fpsHint : 60000.0 / 1001.0;
    wanted.sampling = output.st2110TenBit ? St2110Sampling::YCbCr422_10bit
                                          : St2110Sampling::YCbCr422_8bit;

    const bool needsReopen =
      !outputRuntime.st2110Sender ||
      !outputRuntime.st2110Sender->isOpen() ||
      outputRuntime.st2110Sender->config().width != wanted.width ||
      outputRuntime.st2110Sender->config().height != wanted.height ||
      outputRuntime.st2110Sender->config().destinationAddress != wanted.destinationAddress ||
      outputRuntime.st2110Sender->config().interfaceAddress != wanted.interfaceAddress ||
      outputRuntime.st2110Sender->config().destinationPort != wanted.destinationPort ||
      outputRuntime.st2110Sender->config().sampling != wanted.sampling;

    if (needsReopen) {
      // Bring PTP up the first time 2110 is actually armed. Failing to bind
      // 319/320 is common (a system PTP service usually holds them) and is NOT
      // fatal — the stream falls back to the local clock and keeps saying so in
      // the SDP, which is exactly the honest behaviour.
      ensurePtpClientStarted();
      outputRuntime.st2110Sender =
        std::make_unique<deckboy::platform::video::St2110Output>();
      outputRuntime.st2110Sender->setPtpClient(&ptpClient_);
      if (!outputRuntime.st2110Sender->open(wanted)) {
        triggerToast("st 2110: " + outputRuntime.st2110Sender->lastError());
        outputRuntime.st2110Sender.reset();
        return;
      }
      // ST 2110-30 audio rides alongside, two ports up by convention. Video
      // without audio is half a feed, and audio is 0.2% of the bitrate.
      deckboy::platform::video::St2110AudioConfig audioCfg;
      audioCfg.destinationAddress = wanted.destinationAddress;
      audioCfg.interfaceAddress = wanted.interfaceAddress;
      audioCfg.destinationPort = wanted.destinationPort + 2;
      audioCfg.channels = 2;
      auto audioSender = std::make_unique<deckboy::platform::video::St2110AudioOutput>();
      audioSender->setPtpClient(&ptpClient_);
      if (audioSender->open(audioCfg)) {
        outputRuntime.st2110AudioSender = std::move(audioSender);
      } else {
        // Audio failing must not take the video flow down with it.
        triggerToast("st 2110 audio: " + audioSender->lastError());
        outputRuntime.st2110AudioSender.reset();
      }
      republishSt2110AudioSenders();
    }

    const int stride = frameCapture.width * 4;
    if (!outputRuntime.st2110Sender->sendFrame(frameCapture.pixels.data(), stride)) {
      triggerToast("st 2110: " + outputRuntime.st2110Sender->lastError());
      outputRuntime.st2110Sender.reset();
    }
  }

  // Lazy, once, and non-fatal on failure.
  void ensurePtpClientStarted() {
    if (ptpStartAttempted_ && ptpClient_.running()) {
      return;
    }
    if (ptpStartAttempted_ && !ptpClient_.running()) {
      return;  // already tried and failed; don't retry every frame
    }
    ptpStartAttempted_ = true;
    deckboy::platform::video::PtpConfig cfg;
    cfg.domain = std::clamp(project_.ptpDomain, 0, 127);
    if (!ptpClient_.start(cfg)) {
      triggerToast("ptp: " + ptpClient_.lastError() + " - using local clock");
    }
  }

  // One line for the settings panel: whether the media clock is traceable.
  std::string ptpStatusLabel() const {
    if (!ptpClient_.running()) {
      return "PTP: not running (local clock)";
    }
    if (!ptpClient_.locked()) {
      return "PTP: listening on domain " + std::to_string(project_.ptpDomain) + " (local clock)";
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "PTP LOCKED  gm %s  domain %d  offset %+.3f ms  path %.0f us",
                  ptpClient_.grandmasterIdentity().c_str(),
                  project_.ptpDomain,
                  static_cast<double>(ptpClient_.offsetNanos()) / 1e6,
                  ptpClient_.meanPathDelayMicros());
    return buf;
  }

  void shutdownOutputSt2110(OutputRuntime& outputRuntime) {
    const bool hadAudio = outputRuntime.st2110AudioSender != nullptr;
    if (outputRuntime.st2110AudioSender) {
      // Unpublish BEFORE destroying, or the audio thread can hold a dangling
      // pointer for the length of one callback.
      outputRuntime.st2110AudioSender->close();
      outputRuntime.st2110AudioSender.reset();
    }
    if (outputRuntime.st2110Sender) {
      outputRuntime.st2110Sender->close();
      outputRuntime.st2110Sender.reset();
    }
    if (hadAudio) {
      republishSt2110AudioSenders();
    }
  }

  // Rebuild the audio thread's view of live ST 2110-30 senders. Main thread only.
  void republishSt2110AudioSenders() {
    std::vector<deckboy::platform::video::St2110AudioOutput*> live;
    for (auto& rt : outputRuntimes_) {
      if (rt.st2110AudioSender && rt.st2110AudioSender->isOpen()) {
        live.push_back(rt.st2110AudioSender.get());
      }
    }
    std::lock_guard<std::mutex> lock(st2110AudioMutex_);
    st2110AudioSenders_.swap(live);
  }

  // Called from the AUDIO THREAD with the final post-delay stereo the room
  // hears, so the 2110 flow carries exactly what the PA does.
  void pushSt2110AudioSamples(const std::vector<std::int16_t>& interleavedStereo) {
    if (interleavedStereo.size() < 2) {
      return;
    }
    std::lock_guard<std::mutex> lock(st2110AudioMutex_);
    if (st2110AudioSenders_.empty()) {
      return;
    }
    const std::size_t frames = interleavedStereo.size() / 2;
    for (auto* sender : st2110AudioSenders_) {
      sender->pushSamples(interleavedStereo.data(), frames);
    }
  }

  // The ST 2110-20 config for one output, exactly as the sender would open it.
  // Single source of truth: both the SDP shown in settings and the SDP served
  // as the NMOS IS-05 transportfile come from here, so a receiver can never be
  // handed two different descriptions of the same flow.
  deckboy::platform::video::St2110Config st2110ConfigForOutput(int outputIndex) const {
    using deckboy::platform::video::St2110Config;
    using deckboy::platform::video::St2110Sampling;
    static const OutputTarget kFallback {};
    const bool valid = outputIndex >= 0 &&
                       outputIndex < static_cast<int>(project_.outputs.size());
    const OutputTarget& output = valid ? project_.outputs[outputIndex] : kFallback;
    auto [rasterW, rasterH] = outputRenderSizeForOutput(outputIndex);
    St2110Config cfg;
    cfg.destinationAddress = trim(output.st2110Address).empty()
      ? std::string("239.20.10.1") : trim(output.st2110Address);
    cfg.interfaceAddress = trim(output.st2110Interface);
    cfg.destinationPort = std::clamp(output.st2110Port, 1, 65535);
    cfg.width = std::max(2, rasterW & ~1);
    cfg.height = std::max(1, rasterH);
    cfg.frameRate = outputStreamFps(0.0);
    cfg.sampling = output.st2110TenBit ? St2110Sampling::YCbCr422_10bit
                                       : St2110Sampling::YCbCr422_8bit;
    return cfg;
  }

  std::string outputSt2110Sdp(int outputIndex) {
    return deckboy::platform::video::st2110BuildSdp(
      st2110ConfigForOutput(outputIndex), "Deckboy " + outputLabel(outputIndex),
      ptpClient_.locked(), ptpClient_.grandmasterIdentity(), project_.ptpDomain);
  }

  // The SDP a receiver needs. Surfaced in settings so the operator can copy it
  // to the receiving device by hand — still the fallback when no NMOS registry
  // is configured, or when the receiving device predates IS-05.
  std::string focusedOutputSt2110Sdp() {
    return outputSt2110Sdp(project_.focusedOutputIndex);
  }

  // ── NMOS ──────────────────────────────────────────────────────────────────
  // Build the sender list the node advertises: every output with ST 2110 armed
  // contributes a video sender, and a paired audio sender on the +2 port that
  // shutdownOutputSt2110/ensureOutputSt2110 use by convention.
  std::vector<deckboy::platform::video::NmosSenderInfo> nmosSenderSnapshot() {
    using deckboy::platform::video::NmosFormat;
    using deckboy::platform::video::NmosSenderInfo;
    std::vector<NmosSenderInfo> senders;
    for (int i = 0; i < static_cast<int>(project_.outputs.size()); ++i) {
      const OutputTarget& output = project_.outputs[i];
      if (!output.st2110Enabled) {
        continue;
      }
      const auto cfg = st2110ConfigForOutput(i);
      const std::string label = outputLabel(i);
      const OutputRuntime* runtime = runtimeForOutput(i);

      NmosSenderInfo video;
      // The key is the stable identity seed. It must depend ONLY on which
      // output this is — fold in the address or the label and every retune
      // would look like a brand new sender to every controller that has us
      // saved.
      video.key = "output-" + std::to_string(i) + "-video";
      video.label = "Deckboy " + label;
      video.description = "Deckboy programme output " + std::to_string(i + 1);
      video.format = NmosFormat::Video;
      video.destinationAddress = cfg.destinationAddress;
      video.destinationPort = cfg.destinationPort;
      video.sourceAddress = cfg.interfaceAddress;
      video.active = runtime && runtime->st2110Sender && runtime->st2110Sender->isOpen();
      video.width = cfg.width;
      video.height = cfg.height;
      video.frameRate = cfg.frameRate;
      video.bitDepth = output.st2110TenBit ? 10 : 8;
      video.sdp = outputSt2110Sdp(i);
      senders.push_back(std::move(video));

      deckboy::platform::video::St2110AudioConfig audioCfg;
      audioCfg.destinationAddress = cfg.destinationAddress;
      audioCfg.interfaceAddress = cfg.interfaceAddress;
      audioCfg.destinationPort = cfg.destinationPort + 2;
      audioCfg.channels = 2;

      NmosSenderInfo audio;
      audio.key = "output-" + std::to_string(i) + "-audio";
      audio.label = "Deckboy " + label + " Audio";
      audio.description = "Deckboy programme output " + std::to_string(i + 1) + " audio";
      audio.format = NmosFormat::Audio;
      audio.destinationAddress = audioCfg.destinationAddress;
      audio.destinationPort = audioCfg.destinationPort;
      audio.sourceAddress = cfg.interfaceAddress;
      audio.active = runtime && runtime->st2110AudioSender && runtime->st2110AudioSender->isOpen();
      audio.channels = 2;
      audio.sampleRate = 48000;
      audio.sdp = deckboy::platform::video::st2110BuildAudioSdp(
        audioCfg, "Deckboy " + label + " Audio", ptpClient_.locked(),
        ptpClient_.grandmasterIdentity(), project_.ptpDomain);
      senders.push_back(std::move(audio));
    }
    return senders;
  }

  // Map a sender key back to the output it came from. Returns -1 if the key is
  // not one we minted.
  int nmosOutputIndexForKey(const std::string& key) const {
    if (key.rfind("output-", 0) != 0) {
      return -1;
    }
    const std::size_t dash = key.find('-', 7);
    if (dash == std::string::npos) {
      return -1;
    }
    const int index = std::atoi(key.substr(7, dash - 7).c_str());
    if (index < 0 || index >= static_cast<int>(project_.outputs.size())) {
      return -1;
    }
    return index;
  }

  // Main thread. Drains IS-05 patches queued by the node's HTTP thread and
  // really applies them, then wakes whoever is blocked waiting on the result.
  void applyPendingNmosPatches() {
    std::vector<std::shared_ptr<PendingNmosPatch>> batch;
    {
      std::lock_guard<std::mutex> lock(nmosPatchMutex_);
      if (nmosPendingPatches_.empty()) {
        return;
      }
      batch.swap(nmosPendingPatches_);
    }
    for (auto& pending : batch) {
      const auto& patch = pending->patch;
      const int index = nmosOutputIndexForKey(patch.senderKey);
      bool applied = false;
      if (index >= 0) {
        OutputTarget& output = project_.outputs[index];
        const bool isAudioLeg = patch.senderKey.find("-audio") != std::string::npos;
        if (patch.destinationChanged) {
          // The audio leg lives at video port + 2 by convention, so a
          // controller retuning audio moves the video base port with it rather
          // than silently splitting the pair.
          output.st2110Address = patch.destinationAddress;
          output.st2110Port = isAudioLeg ? std::max(1, patch.destinationPort - 2)
                                         : patch.destinationPort;
        }
        if (patch.masterEnableChanged) {
          output.st2110Enabled = patch.masterEnable;
        }
        if (patch.destinationChanged || patch.masterEnableChanged) {
          // Tear the sender down; the render loop reopens it on the next frame
          // with the new config. This is the same path a settings edit takes.
          if (OutputRuntime* runtime = runtimeForOutput(index)) {
            shutdownOutputSt2110(*runtime);
          }
          markProjectDirty();
        }
        applied = true;
      }
      {
        std::lock_guard<std::mutex> lock(nmosPatchMutex_);
        pending->applied = applied;
        pending->done = true;
      }
    }
    nmosPatchCv_.notify_all();
  }

  // Stopping the node JOINS its HTTP thread — and that thread may be parked in
  // the patch handler waiting for this very thread to apply its change. Joining
  // without releasing it first is a straight deadlock (it would eventually
  // break on the handler's 2s timeout, but a 2s freeze on every settings change
  // is not acceptable either). So: fail every waiter, wake them, then join.
  void shutdownNmosNode() {
    {
      std::lock_guard<std::mutex> lock(nmosPatchMutex_);
      for (auto& pending : nmosPendingPatches_) {
        pending->applied = false;
        pending->done = true;
      }
      nmosPendingPatches_.clear();
    }
    nmosPatchCv_.notify_all();
    nmosNode_.stop();
    nmosStarted_ = false;
  }

  // Called every update tick. Starts, stops and refreshes the node to match
  // the project. Cheap when nothing changed — setSenders() early-outs on an
  // unchanged list, so this does not spam the registry at frame rate.
  void syncNmosNode() {
    // Hard early-out for the overwhelmingly common case: NMOS off and never
    // started. This runs on every update tick, and a show that never touches
    // ST 2110 should not pay a mutex acquisition per frame for a subsystem it
    // is not using. Two bool loads and out.
    if (!project_.nmosEnabled && !nmosStarted_) {
      return;
    }
    applyPendingNmosPatches();

    const bool wantRunning = project_.nmosEnabled;
    const int wantPort = std::clamp(project_.nmosPort, 1, 65535);
    // NMOS only means anything if the node is reachable. With LOCAL ONLY the
    // listener binds 127.0.0.1, so registering would publish an href to the
    // machine's LAN address that nothing on the network can open — a
    // controller would find the sender, try to fetch its transport file, and
    // fail. Refusing to register is the honest behaviour; the operator sees
    // why in the status line and flips REMOTE ON in the Network tab.
    nmosLocalOnlyBlocked_ = wantRunning && !project_.allowRemoteNetwork &&
                            !trim(project_.nmosRegistryUrl).empty();
    const std::string wantRegistry =
      nmosLocalOnlyBlocked_ ? std::string() : trim(project_.nmosRegistryUrl);

    // A port or registry change needs a genuine restart — the listen socket is
    // already bound and the registry client caches the parsed URL.
    const bool settingsMoved =
      nmosStarted_ && (wantPort != nmosLastPort_ || wantRegistry != nmosLastRegistry_);

    if ((!wantRunning && nmosStarted_) || settingsMoved) {
      shutdownNmosNode();
    }

    if (wantRunning && !nmosStarted_) {
      deckboy::platform::video::NmosConfig cfg;
      cfg.enabled = true;
      cfg.registryUrl = wantRegistry;
      cfg.nodePort = wantPort;
      cfg.label = "Deckboy";
      cfg.interfaceName = trim(project_.nmosInterfaceName).empty()
        ? std::string("eth0") : trim(project_.nmosInterfaceName);
      cfg.allowRemote = project_.allowRemoteNetwork;
      cfg.ptpLocked = ptpClient_.locked();
      cfg.ptpGrandmaster = ptpClient_.grandmasterIdentity();
      nmosNode_.setPatchHandler([this](const deckboy::platform::video::NmosSenderPatch& patch) {
        auto pending = std::make_shared<PendingNmosPatch>();
        pending->patch = patch;
        {
          std::lock_guard<std::mutex> lock(nmosPatchMutex_);
          nmosPendingPatches_.push_back(pending);
        }
        // Block until the main thread has really done it. Two seconds is far
        // longer than a frame; if we time out the app is wedged and reporting
        // failure to the controller is the truthful answer.
        std::unique_lock<std::mutex> lock(nmosPatchMutex_);
        const bool settled = nmosPatchCv_.wait_for(
          lock, std::chrono::seconds(2), [&pending]() { return pending->done; });
        return settled && pending->applied;
      });
      if (!nmosNode_.start(cfg)) {
        triggerToast("nmos: " + nmosNode_.lastError());
        project_.nmosEnabled = false;   // don't retry every tick against a bound port
        return;
      }
      nmosStarted_ = true;
      nmosLastPort_ = wantPort;
      nmosLastRegistry_ = wantRegistry;
    }

    if (!nmosStarted_) {
      return;
    }
    nmosNode_.setPtpState(ptpClient_.locked(), ptpClient_.grandmasterIdentity());
    nmosNode_.setSenders(nmosSenderSnapshot());
  }

  // One line for the settings panel.
  std::string nmosStatusLabel() const {
    if (!project_.nmosEnabled) {
      return "NMOS: off";
    }
    if (!nmosStarted_ || !nmosNode_.httpReady()) {
      return "NMOS: not running";
    }
    if (nmosLocalOnlyBlocked_) {
      return "NMOS: NOT registering - network is LOCAL ONLY (turn REMOTE ON to publish)";
    }
    const int senders = nmosNode_.senderCount();
    if (trim(project_.nmosRegistryUrl).empty()) {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "NMOS: node API on :%d, %d sender%s - no registry set",
                    project_.nmosPort, senders, senders == 1 ? "" : "s");
      return buf;
    }
    if (!nmosNode_.registered()) {
      return "NMOS: registering... " + nmosNode_.lastError();
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "NMOS REGISTERED  %d sender%s  %llu heartbeat%s",
                  senders, senders == 1 ? "" : "s",
                  static_cast<unsigned long long>(nmosNode_.heartbeatCount()),
                  nmosNode_.heartbeatCount() == 1 ? "" : "s");
    return buf;
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
    // A RECORDING composites at its DELIVERY raster, never the display's. It
    // has no display, and "follow display" made it build the full programme
    // raster only to scale that down again for readback -- so with a programme
    // output also armed the app composited 4K TWICE per loop. MEASURED: that
    // pinned the loop at 15-23fps and put every rate above 30 out of reach.
    // Compositing straight to 1080 is a quarter of the pixels and skips the
    // scale entirely.
    const OutputTarget& recOut = project_.outputs[outputIndex];
    if (project_.recordingWidth > 0 && project_.recordingHeight > 0 &&
        normalizeOutputType(recOut.outputType) == "stream" &&
        outputStreamProtocolIsFile(
          normalizeOutputStreamProtocol(recOut.streamProtocol))) {
      return {project_.recordingWidth, project_.recordingHeight};
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

    // NOT createOutputRenderer: this hidden renderer is the media engine's
    // decode/upload target, never an egress surface, so it has nothing to gain
    // from a backend chosen for asynchronous readback -- and on Windows it is
    // where zero-copy D3D11 decode lives, which a different backend would cost.
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

    runtime.outputRenderer = createOutputRenderer(runtime.outputWindow);
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

  // One fingerprint per display: identity plus desktop placement. A monitor
  // that is unplugged and re-plugged, re-arranged, or switched to another
  // resolution all change this string, which is what the topology scan
  // compares against — SDL display indices alone are not stable enough.
  std::vector<std::string> currentDisplaySignatureEntries() const {
    int displayCount = deckboyGetNumVideoDisplays();
    std::vector<std::string> entries;
    entries.reserve(static_cast<std::size_t>(std::max(0, displayCount)));
    for (int index = 0; index < displayCount; ++index) {
      const char* name = deckboyGetDisplayName(index);
      SDL_Rect bounds {};
      std::string entry = name && *name ? name : ("display " + std::to_string(index + 1));
      entry += '@';
      if (deckboyGetDisplayBounds(index, &bounds)) {
        entry += std::to_string(bounds.x) + ',' + std::to_string(bounds.y) + ','
               + std::to_string(bounds.w) + ',' + std::to_string(bounds.h);
      } else {
        entry += "?";
      }
      entries.push_back(std::move(entry));
    }
    return entries;
  }

  static std::string displayEntryName(const std::string& entry) {
    std::size_t at = entry.rfind('@');
    return at == std::string::npos ? entry : entry.substr(0, at);
  }

  void refreshDisplayTopology(bool withToast = false, bool forceRehome = false) {
    int displayCount = deckboyGetNumVideoDisplays();
    if (displayCount <= 0) {
      // Zero displays is a transient state (RDP handoff, driver reset).
      // Don't mutate persisted display targets over it — when displays
      // return, name matching restores the intended routing. The old
      // fingerprints are deliberately kept so the return scan still sees a
      // difference and re-homes the outputs.
      if (withToast) {
        triggerToast("display scan: no displays reported");
      }
      return;
    }

    std::vector<std::string> previousEntries = displaySignatureEntries_;
    std::vector<std::string> entries = currentDisplaySignatureEntries();
    bool firstScan = previousEntries.empty();
    displaySignatureEntries_ = entries;
    observedDisplayCount_ = displayCount;

    // Re-home only the outputs whose target display actually changed: either
    // name matching moved them to a different index, or the display sitting
    // at their index is not the one that was there before. A topology event
    // must never churn an unaffected output through a fullscreen
    // exit/re-enter (v0.76.19 regression: focus-stealing recovery fight).
    bool changed = false;
    std::vector<int> affected;
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      OutputTarget& output = project_.outputs[outputIndex];
      int previousIndex = output.displayIndex;
      int resolved = resolveOutputDisplayIndex(output, displayCount);
      if (previousIndex != resolved) {
        output.displayIndex = resolved;
        changed = true;
      }
      if (firstScan && !forceRehome) {
        continue;
      }
      if (forceRehome) {
        affected.push_back(outputIndex);
        continue;
      }
      bool displayMoved = previousIndex != resolved;
      bool panelSwapped =
        previousIndex < 0 || previousIndex >= static_cast<int>(previousEntries.size()) ||
        resolved < 0 || resolved >= static_cast<int>(entries.size()) ||
        previousEntries[static_cast<std::size_t>(previousIndex)] !=
          entries[static_cast<std::size_t>(resolved)];
      if (displayMoved || panelSwapped) {
        affected.push_back(outputIndex);
      }
    }
    if (changed) {
      markProjectDirty();
    }

    // An explicit hot-plug is the one moment a stable fullscreen output SHOULD
    // be moved: the per-tick recovery path deliberately ignores `wrongDisplay`
    // while fullscreen (SDL's reported display disagrees on mixed-DPI setups),
    // so without this a monitor connected mid-show never picks up its output.
    // This is one-shot per real topology change, not a poll.
    for (int outputIndex : affected) {
      OutputTarget& output = project_.outputs[outputIndex];
      if (!output.enabled || normalizeOutputType(output.outputType) != "window") {
        continue;
      }
      OutputRuntime* runtime = runtimeForOutput(outputIndex);
      if (!runtime || !runtime->outputWindow || runtime->recoveryPausedByEscape) {
        continue;
      }
      bool fullscreen = (SDL_GetWindowFlags(runtime->outputWindow) & SDL_WINDOW_FULLSCREEN) != 0;
      bool wantsFullscreen = fullscreen || runtime->fullscreenIntended;

      // The panel this output is pinned to is gone. Park it windowed rather
      // than re-homing the program feed onto a monitor the operator is
      // working on. fullscreenIntended survives, so the return trip is
      // automatic.
      if (!output.displayName.empty() &&
          std::none_of(entries.begin(), entries.end(), [&](const std::string& entry) {
            return displayEntryName(entry) == output.displayName;
          })) {
        runtime->awaitingDisplayReturn = true;
        if (fullscreen) {
          SDL_SetWindowFullscreen(runtime->outputWindow, false);
        }
        SDL_HideWindow(runtime->outputWindow);
        setOutputHealthState(outputIndex, OutputHealthState::Error,
                             "display missing: " + output.displayName);
        triggerToast("output " + std::to_string(outputIndex + 1) + " parked - "
                     + output.displayName + " disconnected");
        continue;
      }
      if (runtime->awaitingDisplayReturn) {
        runtime->awaitingDisplayReturn = false;
        triggerToast("output " + std::to_string(outputIndex + 1) + " restored - "
                     + output.displayName + " reconnected");
      }

      // Clear the strike backoff: those strikes were counted against the old
      // topology, and a genuine hot-plug deserves a fresh attempt.
      runtime->recoveryStrikeWindowStartMs = 0;
      runtime->recoveryStrikeCount = 0;
      runtime->suppressRecoveryUntilMs = 0;
      bool controlHadFocus = controlWindow_ && SDL_GetKeyboardFocus() == controlWindow_;
      applyOutputDisplaySelection(outputIndex, true, wantsFullscreen);
      if (controlHadFocus) {
        SDL_RaiseWindow(controlWindow_);
      }
    }

    // Heal everything else via the recovery path, which honors
    // recoveryPausedByEscape and fullscreenIntended.
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (std::find(affected.begin(), affected.end(), outputIndex) != affected.end()) {
        continue;  // already re-homed above; a second pass would fight it
      }
      recoverWindowOutputIfNeeded(outputIndex, false);
    }
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      restartLiveBrowserCueIfNeeded(deckIndex);
    }
    if (withToast) {
      triggerToast(describeDisplayTopologyChange(previousEntries, entries, affected));
      playUiSound(UiSoundEffect::Toggle);
    }
  }

  // Operator-facing summary of what just happened to the desktop, so the
  // toast says "display connected: DELL U2720Q" instead of a bare count.
  std::string describeDisplayTopologyChange(const std::vector<std::string>& previousEntries,
                                            const std::vector<std::string>& entries,
                                            const std::vector<int>& affected) const {
    std::vector<std::string> previousNames;
    std::vector<std::string> currentNames;
    for (const auto& entry : previousEntries) previousNames.push_back(displayEntryName(entry));
    for (const auto& entry : entries) currentNames.push_back(displayEntryName(entry));

    auto missingFrom = [](const std::vector<std::string>& from,
                          const std::vector<std::string>& against) {
      std::vector<std::string> names = against;
      std::vector<std::string> result;
      for (const auto& name : from) {
        auto it = std::find(names.begin(), names.end(), name);
        if (it == names.end()) {
          result.push_back(name);
        } else {
          names.erase(it);  // consume, so duplicate panel names pair off
        }
      }
      return result;
    };
    std::vector<std::string> added = missingFrom(currentNames, previousNames);
    std::vector<std::string> removed = missingFrom(previousNames, currentNames);

    std::string summary;
    if (!added.empty()) {
      summary = "display connected: " + added.front();
      if (added.size() > 1) summary += " +" + std::to_string(added.size() - 1);
    } else if (!removed.empty()) {
      summary = "display disconnected: " + removed.front();
      if (removed.size() > 1) summary += " +" + std::to_string(removed.size() - 1);
    } else if (previousNames != currentNames || previousEntries != entries) {
      summary = "displays rearranged";
    } else {
      summary = "display scan";
    }
    summary += " (" + std::to_string(static_cast<int>(entries.size())) + " total)";
    if (!affected.empty()) {
      summary += " - " + std::to_string(static_cast<int>(affected.size()))
               + (affected.size() == 1 ? " output re-homed" : " outputs re-homed");
    }
    return summary;
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
    if (runtime->awaitingDisplayReturn) {
      // Pinned display is unplugged. Don't re-assert fullscreen anywhere —
      // the topology scan un-parks this output when the panel comes back.
      setOutputHealthState(outputIndex, OutputHealthState::Error,
                           "display missing: " + output.displayName);
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
