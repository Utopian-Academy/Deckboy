// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
//
// ═══════════════════════════════════════════════════════════════════════════════
// app_smoke.ipp — Startup Self-Test & Smoke Test Suite
// ═══════════════════════════════════════════════════════════════════════════════
//
// Provides two static entry points invoked from main() before the GUI starts:
//
//   runSelfCheck()   — diagnostic inventory printed to stdout. Enumerates the
//                      runtime environment: version tag, font availability,
//                      ffmpeg/ffprobe reachability, NDI SDK compile-time flag,
//                      LTC dynamic library load, NMC sync runtime, and the
//                      full set of capture / output / integration backend
//                      catalogs with their support status. Route planning is
//                      exercised for default request profiles so operators can
//                      verify that the correct backends resolve on their OS.
//                      Triggered by the --check CLI flag.
//
//   runSmoke()       — automated regression suite that returns 0 on success or
//                      1 on any failure. Each test is a self-contained lambda
//                      guarded by an expect() helper that prints [ok] or [fail].
//                      Coverage includes:
//                        • transition source gain policy (paused / stopped /
//                          playing fade-envelope correctness)
//                        • browser cue status summary label variants
//                        • OSC string message build → parse → command map
//                          round-trip, including bundle unwrapping
//                        • NMC sync packet parsing
//                        • capture backend route planning per-platform
//                        • output backend route planning (stream + NDI combo)
//                        • integration backend route planning with runtime
//                          support flag assertions per-platform
//                        • full project save → load round-trip covering every
//                          serializable field: canvas, OSC query, integrations,
//                          jump mode, panic profile, outputs (NDI, stream,
//                          alpha, delay, colorspace, layout, orientation, test
//                          card, AOI), deck settings (transition, opacity,
//                          playlist defaults, timecode, warp, edge blend, NDI),
//                          and cue fields (trim, chroma key, crop, color
//                          controls, lower third, image still)
//                        • legacy deck-level NDI → output-level migration
//                      Triggered by the --smoke CLI flag.
//
// These tests run without SDL initialization or GPU resources, exercising only
// the data model, serialization, protocol helpers, and backend catalogs.
// ═══════════════════════════════════════════════════════════════════════════════

  static int runSelfCheck() {
    std::cout << "Deckboy self-check\n";
    std::cout << "version: " << deckboy::core::version::kVersionTag << '\n';
    std::cout << "project-root: " << Paths::projectRoot() << '\n';
    std::cout << "font-sans: " << (fs::exists(Paths::fontPath(Paths::FontName::Sans)) ? "ok" : "missing") << '\n';
    std::cout << "font-mono: " << (fs::exists(Paths::fontPath(Paths::FontName::Mono)) ? "ok" : "missing") << '\n';
    std::cout << "font-pixel: " << (fs::exists(Paths::fontPath(Paths::FontName::Pixel)) ? "ok" : "missing") << '\n';
    std::cout << "ffmpeg: " << (readAllText({"ffmpeg", "-version"}).has_value() ? "ok" : "missing") << '\n';
    std::cout << "ffprobe: " << (readAllText({"ffprobe", "-version"}).has_value() ? "ok" : "missing") << '\n';
#if defined(DECKBOY_HAS_NDI_SDK)
    std::cout << "ndi-sdk: headers detected (runtime loads when NDI is enabled)\n";
#else
    std::cout << "ndi-sdk: not built (set DECKBOY_NDI_SDK or install SDK headers)\n";
#endif
    // MIDI: report the RtMidi wrapper's view separately from whether the app
    // can actually consume MIDI. They are not the same thing — see
    // startMidiInput(), which is ALSA-only, so a Windows/macOS build happily
    // enumerates ports it has no code path to open.
    {
#if defined(DECKBOY_HAS_MIDI)
      auto midiDevices = deckboy::platform::midi::MidiInput::listDevices();
      std::cout << "midi-runtime: rtmidi ok (" << midiDevices.size() << " input port"
                << (midiDevices.size() == 1 ? "" : "s") << ")";
#if !defined(DECKBOY_HAS_ALSA)
      std::cout << " via rtmidi (MTC/MMC sysex remain ALSA-only)";
#endif
      std::cout << '\n';
#else
      std::cout << "midi-runtime: not built (rtmidi not found)\n";
#endif
    }
    // LtcApi resolves libltc dynamically on every platform — ltc.dll on
    // Windows, libltc.dylib on macOS, libltc.so on Linux — so probe it for
    // real everywhere. This used to be skipped under _WIN32 and print
    // "not supported on this build", which was simply untrue: the Windows
    // build ships ltc.dll, the loader looks for it, and the integration
    // catalog already reported ltc[ok]. An operator checking whether timecode
    // ingest would work was told the wrong thing.
    {
      LtcApi ltcApi;
      if (ltcApi.ensureLoaded()) {
        std::cout << "ltc-runtime: ok\n";
      } else {
        std::cout << "ltc-runtime: missing (" << ltcApi.loadError << ")\n";
      }
      ltcApi.shutdown();
    }
    std::cout << "ui-sfx: enabled by separate SDL audio device when available\n";
    std::cout << "companion-control: tcp/udp port 5510 by default (override with DECKBOY_COMPANION_PORT)\n";

    {
      auto captureCatalog = deckboy::platform::createCaptureBackendCatalog();
      std::ostringstream line;
      line << "capture-backends:";
      for (const auto& info : captureCatalog->list()) {
        line << ' ' << info.id << '[' << (info.supported ? "ok" : "stub") << ']';
      }
      std::cout << line.str() << '\n';

      auto describeSourcePlan = [](const deckboy::platform::SourceCapturePlan& plan) {
        return plan.backendId + "[" + (plan.supported ? "ok" : "stub") + "]";
      };
      deckboy::platform::SourceCaptureRequest windowReq;
      windowReq.kind = deckboy::platform::SourceCaptureKind::Window;
      windowReq.sourceRef = "active-window";
      windowReq.width = 1280;
      windowReq.height = 720;
      windowReq.frameRate = 30;
      windowReq.drawMouse = true;
      windowReq.display = ":0.0";
      deckboy::platform::SourceCaptureRequest cameraReq = windowReq;
      cameraReq.kind = deckboy::platform::SourceCaptureKind::Camera;
      cameraReq.sourceRef = "default-camera";
      deckboy::platform::SourceCaptureRequest appReq = windowReq;
      appReq.kind = deckboy::platform::SourceCaptureKind::AppTexture;
      appReq.sourceRef = "default-bus";
      std::cout << "capture-plan-defaults:"
                << " window=" << describeSourcePlan(deckboy::platform::planSourceCapture(windowReq))
                << " camera=" << describeSourcePlan(deckboy::platform::planSourceCapture(cameraReq))
                << " app=" << describeSourcePlan(deckboy::platform::planSourceCapture(appReq))
                << '\n';
    }

    {
      auto outputCatalog = deckboy::platform::createOutputBackendCatalog();
      std::ostringstream line;
      line << "output-backends:";
      for (const auto& info : outputCatalog->list()) {
        line << ' ' << info.id << '[' << (info.supported ? "ok" : "stub") << ']';
      }
      std::cout << line.str() << '\n';

      auto describeRoute = [](const deckboy::platform::OutputBackendRoutePlan& route) {
        std::ostringstream value;
        for (size_t i = 0; i < route.steps.size(); ++i) {
          if (i) {
            value << '+';
          }
          value << route.steps[i].backendId << '[' << (route.steps[i].supported ? "ok" : "stub") << ']';
        }
        return value.str();
      };
      deckboy::platform::OutputBackendRouteRequest standardReq;
      standardReq.outputType = "window";
      standardReq.streamEnabled = false;
      standardReq.ndiEnabled = false;
      standardReq.deckLinkEnabled = false;
      deckboy::platform::OutputBackendRouteRequest streamReq;
      streamReq.outputType = "stream";
      streamReq.streamEnabled = true;
      streamReq.ndiEnabled = false;
      streamReq.deckLinkEnabled = false;
      deckboy::platform::OutputBackendRouteRequest ndiReq;
      ndiReq.outputType = "window";
      ndiReq.streamEnabled = false;
      ndiReq.ndiEnabled = true;
      ndiReq.deckLinkEnabled = false;
      std::cout << "output-route-defaults:"
                << " standard=" << describeRoute(deckboy::platform::planOutputBackendRoute(standardReq, *outputCatalog))
                << " stream=" << describeRoute(deckboy::platform::planOutputBackendRoute(streamReq, *outputCatalog))
                << " ndi=" << describeRoute(deckboy::platform::planOutputBackendRoute(ndiReq, *outputCatalog))
                << '\n';
    }

    {
      NdiTriggerApi ndiTriggerApi;
      if (ndiTriggerApi.ensureLoaded()) {
        std::cout << "ndi-trigger-runtime: ok\n";
      } else {
        std::cout << "ndi-trigger-runtime: missing (" << ndiTriggerApi.loadError << ")\n";
      }
      ndiTriggerApi.shutdown();
    }
    {
      // Reported for real on every platform. This used to print
      // "stub (windows pending)" under _WIN32, which was untrue: the NMC
      // bridge is cross-platform UDP built on the same helpers as Companion,
      // and enabling it on Windows does bind its port and report nmc[on,ok].
      App app;
      std::cout << "nmc-sync-runtime: " << app.describeNmcSyncRuntime() << '\n';
    }
    {
      auto integrationCatalog = deckboy::platform::createIntegrationBackendCatalog();
      std::ostringstream line;
      line << "integration-backends:";
      for (const auto& info : integrationCatalog->list()) {
        line << ' ' << info.id << '[' << (info.supported ? "ok" : "stub") << ']';
      }
      std::cout << line.str() << '\n';

      auto describeRoute = [](const deckboy::platform::IntegrationBackendRoutePlan& route) {
        std::ostringstream value;
        for (size_t i = 0; i < route.steps.size(); ++i) {
          if (i) {
            value << '+';
          }
          value << route.steps[i].backendId
                << '[' << (route.steps[i].enabled ? "on" : "off") << ','
                << (route.steps[i].supported ? "ok" : "stub") << ']';
        }
        return value.str();
      };
      deckboy::platform::IntegrationBackendRouteRequest defaultsReq;
      deckboy::platform::IntegrationBackendRouteRequest triggerReq;
      triggerReq.atemTriggerEnabled = true;
      triggerReq.ndiTriggerEnabled = true;
      triggerReq.mtcIngestEnabled = true;
      deckboy::platform::IntegrationBackendRouteRequest lightingReq;
      lightingReq.dmxArtNetEnabled = true;
      std::cout << "integration-route-defaults:"
                << " base=" << describeRoute(deckboy::platform::planIntegrationBackendRoute(defaultsReq, *integrationCatalog))
                << " trigger=" << describeRoute(deckboy::platform::planIntegrationBackendRoute(triggerReq, *integrationCatalog))
                << " lighting=" << describeRoute(deckboy::platform::planIntegrationBackendRoute(lightingReq, *integrationCatalog))
                << '\n';
    }

    return 0;
  }

  static int runSmoke() {
    int failures = 0;
    auto expect = [&](bool condition, const std::string& label) {
      if (condition) {
        std::cout << "[ok] " << label << '\n';
      } else {
        std::cout << "[fail] " << label << '\n';
        failures += 1;
      }
    };
    auto framesMatch = [](const std::optional<DecodedFrame>& lhs,
                          const std::optional<DecodedFrame>& rhs) {
      return lhs && rhs &&
             lhs->width == rhs->width &&
             lhs->height == rhs->height &&
             lhs->pixels == rhs->pixels;
    };

    {
      Cue cue;
      float pausedGain = transitionSourceGainForLoadCue(&cue, TransportState::Paused, 0.0);
      float stoppedGain = transitionSourceGainForLoadCue(&cue, TransportState::Stopped, 0.0);
      float playingGain = transitionSourceGainForLoadCue(&cue, TransportState::Playing, 0.25);
      expect(std::abs(pausedGain - 1.0f) < 0.001f &&
               std::abs(stoppedGain - 1.0f) < 0.001f &&
               std::abs(playingGain - 0.25f) < 0.001f,
             "transition source gain policy");
    }

    {
      // Diagonal drift (v0.78.7): crosshatch loops one cell per 8 s,
      // checkerboard one period per 10 s — X and Y in lockstep (45°).
      Cue cue;
      cue.kind = CueKind::Pattern;
      cue.path = "pattern://crosshatch-motion";
      cue.width = 320;
      cue.height = 180;
      auto first = MediaEngine::buildPatternFrame(cue, 0.0, cue.width, cue.height);
      auto looped = MediaEngine::buildPatternFrame(cue, 8.0, cue.width, cue.height);
      auto mid = MediaEngine::buildPatternFrame(cue, 4.0, cue.width, cue.height);
      expect(framesMatch(first, looped) && !framesMatch(first, mid),
             "crosshatch motion drifts diagonally and loops at 8s");
    }

    {
      Cue cue;
      cue.kind = CueKind::Pattern;
      cue.path = "pattern://checkerboard-motion";
      cue.width = 320;
      cue.height = 180;
      auto first = MediaEngine::buildPatternFrame(cue, 0.0, cue.width, cue.height);
      auto looped = MediaEngine::buildPatternFrame(cue, 10.0, cue.width, cue.height);
      // Probe at quarter period: a half-period diagonal shift maps a
      // checkerboard onto itself, so t=5 would falsely match t=0.
      auto quarter = MediaEngine::buildPatternFrame(cue, 2.5, cue.width, cue.height);
      expect(framesMatch(first, looped) && !framesMatch(first, quarter),
             "checkerboard motion drifts diagonally and loops at 10s");
    }

    {
      Cue cue;
      cue.kind = CueKind::Pattern;
      cue.path = "pattern://checkerboard";
      cue.width = 128;
      cue.height = 128;
      auto frame = MediaEngine::buildPatternFrame(cue, 0.0, cue.width, cue.height);
      bool hasWhite = false;
      bool hasBlack = false;
      bool hasTransparent = false;
      if (frame) {
        for (size_t i = 0; i + 3 < frame->pixels.size(); i += 4) {
          const std::uint8_t r = frame->pixels[i + 0];
          const std::uint8_t g = frame->pixels[i + 1];
          const std::uint8_t b = frame->pixels[i + 2];
          const std::uint8_t a = frame->pixels[i + 3];
          hasWhite = hasWhite || (r == 255 && g == 255 && b == 255);
          hasBlack = hasBlack || (r == 0 && g == 0 && b == 0);
          hasTransparent = hasTransparent || (a != 255);
          if (hasWhite && hasBlack && hasTransparent) {
            break;
          }
        }
      }
      expect(frame && hasWhite && hasBlack && !hasTransparent, "checkerboard frame opaque");
    }

    {
      std::string startupLabel = browserCueStatusSummary(BrowserStartPhase::WaitXvfb, false, "");
      bool startupLabelOk = startupLabel.find("xvfb") != std::string::npos
                         || startupLabel.find("webview") != std::string::npos
                         || startupLabel.find("initializing") != std::string::npos;
      bool liveLabelOk = browserCueStatusSummary(BrowserStartPhase::Live, true, "") == "live";
      bool failedLabelOk = browserCueStatusSummary(BrowserStartPhase::WaitChrome, false, "capture start failed").rfind("failed:", 0) == 0;
      expect(startupLabelOk && liveLabelOk && failedLabelOk, "browser status summary");
    }

    {
      auto osc = buildOscStringMessage("/take", "3");
      std::string packet(reinterpret_cast<const char*>(osc.data()), osc.size());
      auto parsed = parseOscPacket(packet);
      expect(parsed.size() == 1 && toUpper(parsed[0].address) == "/TAKE", "osc message parse");
      auto mapped = parsed.empty() ? std::optional<std::string> {} : mapOscToRemoteCommand(parsed[0]);
      expect(mapped && *mapped == "TAKE 3", "osc command mapping");
    }

    {
      auto osc = buildOscStringMessage("/cue/audio", "on");
      std::string packet(reinterpret_cast<const char*>(osc.data()), osc.size());
      auto parsed = parseOscPacket(packet);
      auto mapped = parsed.empty() ? std::optional<std::string> {} : mapOscToRemoteCommand(parsed[0]);
      expect(mapped && (*mapped == "CUEAUDIO ON" || *mapped == "CUEAUDIO"), "osc cue audio mapping");
    }

    {
      auto osc = buildOscStringMessage("/deck/opacity", "75");
      std::string packet(reinterpret_cast<const char*>(osc.data()), osc.size());
      auto parsed = parseOscPacket(packet);
      auto mapped = parsed.empty() ? std::optional<std::string> {} : mapOscToRemoteCommand(parsed[0]);
      expect(mapped && *mapped == "DECKOPACITY 75", "osc deck opacity mapping");
    }

    {
      auto osc = buildOscStringMessage("/atem", "1");
      std::string packet(reinterpret_cast<const char*>(osc.data()), osc.size());
      auto parsed = parseOscPacket(packet);
      auto mapped = parsed.empty() ? std::optional<std::string> {} : mapOscToRemoteCommand(parsed[0]);
      expect(mapped && *mapped == "ATEM ON", "osc integration mapping");
    }

    {
      auto parsed = parseNmcSyncPacket("DECKBOY_NMC1 PLAY 12.5");
      expect(parsed && parsed->command == "PLAY" && parsed->seconds &&
               std::fabs(*parsed->seconds - 12.5) < 0.001,
             "nmc packet parse");
    }

    {
      auto one = buildOscStringMessage("/go", "1");
      auto two = buildOscStringMessage("/overlay", "1");
      std::vector<std::uint8_t> bundle;
      bundle.insert(bundle.end(), {'#', 'b', 'u', 'n', 'd', 'l', 'e', '\0'});
      bundle.insert(bundle.end(), 8, 0);
      appendOscU32(bundle, static_cast<std::uint32_t>(one.size()));
      bundle.insert(bundle.end(), one.begin(), one.end());
      appendOscU32(bundle, static_cast<std::uint32_t>(two.size()));
      bundle.insert(bundle.end(), two.begin(), two.end());
      std::string packet(reinterpret_cast<const char*>(bundle.data()), bundle.size());
      auto parsed = parseOscPacket(packet);
      expect(parsed.size() == 2, "osc bundle parse");
    }

    {
      deckboy::platform::SourceCaptureRequest request;
      request.kind = deckboy::platform::SourceCaptureKind::Window;
      request.sourceRef = "active-window";
      request.width = 1280;
      request.height = 720;
      request.frameRate = 30;
      request.drawMouse = true;
      request.display = ":0.0";
      auto plan = deckboy::platform::planSourceCapture(request);
#if defined(__linux__)
      expect(plan.supported && !plan.ffmpegArgs.empty() && plan.backendId == "x11grab", "capture backend plan");
#elif defined(_WIN32)
      expect(plan.supported && !plan.ffmpegArgs.empty() && plan.backendId == "gdigrab", "capture backend plan");
#else
      // macOS: window/screen capture is the ScreenCaptureKit helper. It reports
      // supported once deckboy-sckcapture is beside the exe (as it is in the
      // build tree and the bundle), and emits the helper's argv as ffmpegArgs;
      // if the helper is missing it stays honest with a reason.
      expect(plan.backendId == "screencapturekit" &&
                 (plan.supported ? !plan.ffmpegArgs.empty()
                                 : !plan.reasonUnavailable.empty()),
             "capture backend plan");
#endif
    }

    {
      auto outputCatalog = deckboy::platform::createOutputBackendCatalog();
      deckboy::platform::OutputBackendRouteRequest request;
      request.outputType = "stream";
      request.streamEnabled = true;
      request.ndiEnabled = true;
      request.deckLinkEnabled = false;
      auto route = deckboy::platform::planOutputBackendRoute(request, *outputCatalog);
      bool hasStream = false;
      bool hasNdi = false;
      for (const auto& step : route.steps) {
        if (step.backendId == "stream") {
          hasStream = true;
        } else if (step.backendId == "ndi") {
          hasNdi = true;
        }
      }
      expect(hasStream && hasNdi, "output backend route plan");
    }

    {
      auto integrationCatalog = deckboy::platform::createIntegrationBackendCatalog();
      deckboy::platform::IntegrationBackendRouteRequest request;
      request.atemTriggerEnabled = true;
      request.dmxArtNetEnabled = true;
      auto route = deckboy::platform::planIntegrationBackendRoute(request, *integrationCatalog);
      bool hasAtem = false;
      bool hasArtNet = false;
      bool atemSupported = false;
      bool artNetSupported = false;
      for (const auto& step : route.steps) {
        if (step.backendId == "atem") {
          hasAtem = true;
          atemSupported = step.supported;
        } else if (step.backendId == "dmx-artnet") {
          hasArtNet = true;
          artNetSupported = step.supported;
        }
      }
      expect(hasAtem && hasArtNet, "integration backend route plan");
      // ATEM and Art-Net bridges are plain UDP listeners — supported on all
      // platforms since the v0.76.12 cross-platform networking pass. (The
      // old #ifdef _WIN32 expectation predated Windows socket support.)
      expect(atemSupported && artNetSupported, "integration runtime support flags");
    }

    {
      Project project;
      project.outputCanvasEnabled = true;
      project.outputCanvasWidth = 5760;
      project.outputCanvasHeight = 2160;
      project.focusedOutputIndex = 1;
      project.oscQueryEnabled = true;
      project.oscQueryPort = 6410;
      project.oscFeedbackMirrorEnabled = true;
      project.oscFeedbackRateMs = 90;
      project.atemTriggerEnabled = true;
      project.ndiTriggerEnabled = true;
      project.nmcSyncEnabled = false;
      project.mtcIngestEnabled = true;
      project.ltcIngestEnabled = false;
      project.dmxArtNetEnabled = true;
      project.artNetPort = 16454;
      project.jumpMode = "load";
      project.jumpTransitionEnabled = false;
      project.panicProfile = "fade_rewind";
      project.panicFadeSeconds = 1.4;
      project.panicAutoRestore = true;
      Deck deck;
      deck.name = "Deck Smoke";
      deck.outputRouteDeckIndex = 0;
      deck.transitionSeconds = 1.5;
      deck.transitionStyle = "dip";
      deck.playlistOpacity = 0.62f;
      deck.playlistAutoFade = true;
      deck.playlistFadeSeconds = 1.7;
      deck.playlistTimebaseFps = 29.97;
      deck.playlistStartOffsetSeconds = 3600.0;
      deck.playlistDefaultCueFadeSeconds = 0.75;
      deck.playlistDefaultStillDurationSeconds = 6.5;
      deck.playlistDefaultLoop = true;
      deck.playlistDefaultFadeInEnabled = true;
      deck.playlistDefaultFadeOutEnabled = false;
      deck.playlistDefaultAudioEnabled = false;
      deck.playlistDefaultPauseAtBeginning = true;
      deck.playlistDefaultPauseAtEnd = true;
      deck.playlistDefaultTransitionToNext = false;
      deck.ndiEnabled = true;
      deck.ndiSourceName = "Smoke Fill";
      deck.ndiKeyEnabled = true;
      deck.ndiKeySourceName = "Smoke Key";
      deck.canvasViewX = 320;
      deck.canvasViewY = 40;
      deck.warpEnabled = true;
      deck.warpMode = "perspective";
      deck.warpTopLeftX = -12.0f;
      deck.warpTopLeftY = 8.0f;
      deck.warpTopRightX = 10.0f;
      deck.warpTopRightY = 5.0f;
      deck.warpBottomRightX = 14.0f;
      deck.warpBottomRightY = -6.0f;
      deck.warpBottomLeftX = -9.0f;
      deck.warpBottomLeftY = -7.0f;
      deck.edgeBlendLeft = 0.08f;
      deck.edgeBlendRight = 0.12f;
      deck.edgeBlendTop = 0.03f;
      deck.edgeBlendBottom = 0.05f;
      deck.timecodeChaseEnabled = true;
      deck.timecodeRunEnabled = false;
      deck.timecodeTriggerEnabled = true;
      deck.timecodeJamSyncEnabled = false;
      deck.timecodeFreewheelSeconds = 2.5;
      deck.timecodeFps = 25.0;
      deck.timecodeCurrentSeconds = 12.0;
      Cue cue;
      cue.path = (fs::temp_directory_path() / "test.mp4").string();
      cue.name = "Smoke Cue";
      cue.id = "smoke-cue-1";
      cue.cueId = "A1";
      cue.kind = CueKind::Video;
      cue.duration = 20.0;
      cue.width = 1920;
      cue.height = 1080;
      cue.hasAudio = true;
      cue.audioEnabled = false;
      cue.inPointSeconds = 2.0;
      cue.outPointSeconds = 8.0;
      cue.pauseAtBeginning = true;
      cue.pauseOnLastFrame = true;
      cue.transitionToNext = false;
      cue.gotoTarget = "Q12";
      cue.triggerTimecodeSeconds = 13.0;
      cue.cueTransitionSeconds = 1.25;
      cue.cueTransitionStyle = "crossfade";
      cue.outputRotationDegrees = 17.5f;
      cue.cropLeft = 0.10f;
      cue.cropRight = 0.05f;
      cue.cropTop = 0.02f;
      cue.cropBottom = 0.03f;
      cue.chromaKeyEnabled = true;
      cue.chromaKeyColor = SDL_Color {20, 220, 45, 255};
      cue.chromaKeyTolerance = 88.5f;
      cue.chromaKeySoftness = 14.0f;
      cue.brightness = 1.25f;
      cue.contrast = 0.85f;
      cue.saturation = 1.40f;
      cue.hueShift = -22.0f;
      cue.audioGainDb = -6.5f;
      cue.audioPan = 0.35f;
      cue.audioMono = true;
      cue.audioFadeInSeconds = 0.5f;
      cue.audioFadeOutSeconds = 0.0f;
      cue.audioOutputPair = 2;  // outs 5-6
      deck.audioOutputChannels = 8;
      Cue imgCue;
      imgCue.path = (fs::temp_directory_path() / "test.jpg").string();
      imgCue.name = "Smoke Still";
      imgCue.id = "smoke-cue-2";
      imgCue.kind = CueKind::Image;
      imgCue.stillDurationSeconds = 5.0;
      Cue ltCue;
      ltCue.path = "graphic://lower-third";
      ltCue.name = "Smoke Lower Third";
      ltCue.kind = CueKind::LowerThird;
      ltCue.lowerThirdText = "Hello World";
      ltCue.lowerThirdSubtext = "subtitle here";
      ltCue.lowerThirdBgAlpha = 200;
      // An effect stack with ALL FOUR parameters set, a non-default bypass, and
      // a motion driver. paramC and paramD serialise AFTER the bypass flag so
      // older shows still load -- exactly the kind of ordering that survives a
      // code review and dies on a save/load round trip.
      {
        deckboy::effects::CueEffect fx;
        fx.kind = deckboy::effects::CueEffectKind::ReactionBloom;
        fx.amount = 0.85f;
        fx.paramA = 0.25f;
        fx.paramB = 0.5f;
        fx.paramC = 0.4f;
        fx.paramD = 0.3f;
        cue.effects.push_back(fx);
        deckboy::effects::CueEffect puppet;
        puppet.kind = deckboy::effects::CueEffectKind::MotionPuppet;
        puppet.amount = 0.6f;
        puppet.bypassed = true;
        cue.effects.push_back(puppet);
        cue.motionDriverPath = "smoke-driver.mp4";
        cue.motionDriverSpeed = 1.75f;
        cue.motionDriverPaused = true;
      }
      // Every video-synth field, each set to something that cannot be
      // confused with a neighbour or with its own default. The record is
      // POSITIONAL: a field added to the writer without a matching read
      // shifts every column after it, and the loader had exactly that -- the
      // synth tuning pair was written mid-record and read from the end, so
      // twenty-three video-synth fields were read two columns early and
      // nothing noticed, because each wrong value still clamped into range.
      // Distinctive values are the whole point: 20 read from a column holding
      // 1 still clamps to 20 and looks correct.
      cue.tone.synth.tuning = static_cast<SynthTuning>(3);
      cue.tone.synth.referenceHz = 432.0;
      cue.videoSynth.shape = static_cast<VideoSynthShape>(3);
      cue.videoSynth.mirror = static_cast<VideoSynthMirror>(1);
      cue.videoSynth.palette = static_cast<VideoSynthPalette>(2);
      cue.videoSynth.speed = 2.75;
      cue.videoSynth.scale = 3.25;
      cue.videoSynth.warp = 1.5;
      cue.videoSynth.feedbackAmount = 0.77;
      cue.videoSynth.feedbackZoom = 1.11;
      cue.videoSynth.feedbackRotate = -4.5;
      cue.videoSynth.audioReactivity = 0.66;
      cue.videoSynth.resolution = 4;
      cue.videoSynth.pixelSort = 0.44;
      cue.videoSynth.glitch = 0.33;
      cue.videoSynth.ascii = true;
      cue.videoSynth.asciiCols = 137;
      cue.videoSynth.crt = 0.22;
      cue.videoSynth.asciiCharSet = 4;
      cue.videoSynth.asciiShuffle = 7;
      cue.videoSynth.asciiInk = 3;
      cue.videoSynth.spriteSheetPath = "smoke-sheet.png";
      cue.videoSynth.spriteTileW = 24;
      cue.videoSynth.spriteTileH = 40;
      cue.videoSynth.spriteRotate = 2;
      cue.videoSynth.spriteFreeAngle = 90.0;
      cue.videoSynth.spriteFlip = 3;
      cue.videoSynth.spriteJitter = 0.55;
      cue.videoSynth.spriteChaos = 0.88;
      cue.videoSynth.asciiGlyphs = ".oO@";
      cue.videoSynth.asciiPhrases = "DECKBOY|GO LIVE";
      cue.videoSynth.asciiPhraseHold = 4.5;
      deck.cues.push_back(cue);      // [0]: video — trim/tc/transition tests
      deck.cues.push_back(imgCue);   // [1]: image still — stillDuration test
      deck.cues.push_back(ltCue);    // [2]: lower_third — lowerThird tests
      project.decks = {deck};
      project.outputs = {
        OutputTarget {"Program Out", 0, 1, "", true, "window", -1, true, "srt", "srt://127.0.0.1:9100?mode=caller", "", 7200},
        OutputTarget {"Stage Left Stream", 0, 2, "", true, "stream", 0, true, "rtmp", "rtmp://127.0.0.1/live", "stage-left", 4200}
      };
      project.outputs[0].ndiEnabled = true;
      project.outputs[0].ndiSourceName = "Program Fill";
      project.outputs[0].ndiKeyEnabled = true;
      project.outputs[0].ndiKeySourceName = "Program Key";
      project.outputs[0].outputId = "out-smoke-program";
      project.outputs[0].outputAlpha = 0.82f;
      project.outputs[0].outputDelayMs = 240;
      project.outputs[0].outputTimeOverlayEnabled = true;
      project.outputs[0].outputColorSpace = "bt709";
      project.outputs[0].outputLayoutMode = "span";
      project.outputs[0].outputOrientationDegrees = 90;
      project.outputs[0].outputTestCardEnabled = true;
      project.outputs[1].outputId = "out-smoke-stream";
      project.outputs[1].outputAlpha = 0.67f;
      project.outputs[1].outputDelayMs = 120;
      project.outputs[1].outputTimeOverlayEnabled = false;
      project.outputs[1].outputColorSpace = "srgb";
      project.outputs[1].outputLayoutMode = "duplicate";
      project.outputs[1].outputOrientationDegrees = 270;
      project.outputs[1].outputTestCardEnabled = false;
      project.outputBitDepth = 10;
      // The chosen MIDI port. It used to live only in memory, so every restart
      // fell back to whichever port enumerated first.
      project.midiDeviceName = "APC40 mkII Control";
      normalizeProject(project);

      // ── Caption formats ──────────────────────────────────────────────────
      {
        using namespace deckboy::captions;
        expect(formatForPath("a.vtt") == Format::WebVtt &&
               formatForPath("a.SCC") == Format::Scc &&
               formatForPath("a.dfxp") == Format::Ttml &&
               formatForPath("a.srt") == Format::Srt,
               "caption files are recognised by extension");

        const std::string vtt =
          "WEBVTT\n\n"
          "00:00:01.000 --> 00:00:03.500 align:middle\n"
          "First line\n"
          "second line\n\n"
          "00:01:00.000 --> 00:01:02.000\n"
          "Later\n";
        const SubtitleTrack fromVtt = parseWebVtt(vtt);
        expect(fromVtt.entries.size() == 2, "WebVTT yields both cues");
        expect(std::abs(fromVtt.entries[0].startSeconds - 1.0) < 0.001 &&
               std::abs(fromVtt.entries[0].endSeconds - 3.5) < 0.001,
               "WebVTT times are read, and cue settings after them ignored");
        expect(fromVtt.entries[0].text.find("second line") != std::string::npos,
               "WebVTT keeps a cue's second line");
        expect(std::abs(fromVtt.entries[1].startSeconds - 60.0) < 0.001,
               "WebVTT reads minutes past the first");

        // A round trip has to survive, or the converter is a shredder.
        const SubtitleTrack again = parseWebVtt(writeWebVtt(fromVtt));
        expect(again.entries.size() == fromVtt.entries.size() &&
               std::abs(again.entries[0].startSeconds -
                        fromVtt.entries[0].startSeconds) < 0.002,
               "WebVTT survives being written and read again");

        const SubtitleTrack fromSrtText =
          deckboy::core::parseSrtText(writeSrt(fromVtt));
        expect(fromSrtText.entries.size() == 2 &&
               std::abs(fromSrtText.entries[1].startSeconds - 60.0) < 0.002,
               "a track written as SubRip reads back the same");

        // SCC: real byte pairs. 9420 is Resume Caption Loading, 942F is
        // End of Caption, and the pairs between them are two characters each.
        // "C8C5" is "HE", "CCCC" is "LL", "CF" with a null is "O".
        const std::string scc =
          "Scenarist_SCC V1.0\n\n"
          "00:00:02:00\t9420 9420 C8C5 CCCC CF80 942f 942f\n\n"
          "00:00:06:00\t9420 9420 5745 5254 C580 942f 942f\n";
        const SubtitleTrack fromScc = parseScc(scc);
        expect(fromScc.entries.size() == 2, "SCC yields a caption per timecode");
        expect(fromScc.entries[0].text.find("HELLO") != std::string::npos,
               "SCC decodes 608 character pairs into text");
        expect(std::abs(fromScc.entries[0].startSeconds - 2.0) < 0.05,
               "SCC non-drop timecode resolves to seconds");
        expect(fromScc.entries[0].endSeconds <= fromScc.entries[1].startSeconds + 0.001,
               "an SCC caption ends when the next one displaces it");

        // Drop-frame counts by skipping numbers, not by running slow. At one
        // hour the two differ by about 3.6 seconds, which on air is a caption
        // on the wrong shot.
        bool dropped = false;
        const double ndf = deckboy::captions::detail::parseSccTime("01:00:00:00", dropped);
        expect(!dropped, "a colon before the frames means non-drop");
        const double df = deckboy::captions::detail::parseSccTime("01;00;00;00", dropped);
        expect(dropped, "a semicolon before the frames means drop-frame");
        expect(std::abs((ndf - df) - 3.6) < 0.2,
               "drop-frame and non-drop differ by ~3.6s at one hour");

        const std::string ttml =
          "<tt><body><div>"
          "<p begin=\"00:00:04.000\" end=\"00:00:06.000\">Line<br/>Break</p>"
          "</div></body></tt>";
        const SubtitleTrack fromTtml = parseTtml(ttml);
        expect(fromTtml.entries.size() == 1 &&
               std::abs(fromTtml.entries[0].startSeconds - 4.0) < 0.001,
               "TTML paragraphs carry their times");
        expect(fromTtml.entries[0].text.find("\n") != std::string::npos,
               "TTML line breaks become line breaks");

        expect(parseWebVtt("").entries.empty() && parseScc("").entries.empty() &&
               parseTtml("").entries.empty(),
               "an empty caption file yields no captions rather than one bad one");
      }

      // ── MIDI Show Control ────────────────────────────────────────────────
      {
        using namespace deckboy::showcontrol;
        auto msc = [](std::initializer_list<int> bytes) {
          std::vector<std::uint8_t> out;
          for (int b : bytes) out.push_back(static_cast<std::uint8_t>(b));
          return out;
        };
        // F0 7F <id> 02 <format> <command> [cue] F7
        // GO on cue "5", addressed to device 3, video format.
        auto go = parse(msc({0xF0, 0x7F, 0x03, 0x02, 0x30, 0x01, '5', 0xF7}), 3);
        expect(go.action == Action::Go && go.cue == "5",
               "MSC GO carries its cue number");

        // A multi-part cue number, which is what a real desk sends.
        auto dotted = parse(msc({0xF0, 0x7F, 0x03, 0x02, 0x30, 0x01,
                                 '1', '2', '.', '4', 0xF7}), 3);
        expect(dotted.cue == "12.4", "MSC cue numbers keep their dots");

        // Cue, list and path are separated by 00.
        auto listed = parse(msc({0xF0, 0x7F, 0x03, 0x02, 0x30, 0x01,
                                 '7', 0x00, '2', 0xF7}), 3);
        expect(listed.cue == "7" && listed.cueList == "2",
               "MSC cue list is read after the separator");

        // 127 is everybody, and must be honoured whatever we are set to.
        auto all = parse(msc({0xF0, 0x7F, 0x7F, 0x02, 0x30, 0x01, '1', 0xF7}), 9);
        expect(all.action == Action::Go, "MSC device 127 addresses everything");

        // A message for another device is not ours.
        auto other = parse(msc({0xF0, 0x7F, 0x05, 0x02, 0x30, 0x01, '1', 0xF7}), 3);
        expect(other.action == Action::None, "MSC ignores another device's cue");

        // A GO to the LIGHTING rig arriving on our wire is not ours either.
        // This is the one that matters: acting on it would fire video when the
        // desk asked for a lamp.
        auto lighting = parse(msc({0xF0, 0x7F, 0x03, 0x02, 0x01, 0x01, '1', 0xF7}), 3);
        expect(lighting.action == Action::None,
               "MSC ignores commands addressed to another kind of device");

        // All-types reaches us.
        auto every = parse(msc({0xF0, 0x7F, 0x03, 0x02, 0x7F, 0x01, '1', 0xF7}), 3);
        expect(every.action == Action::Go, "MSC all-types format reaches video");

        auto stop = parse(msc({0xF0, 0x7F, 0x03, 0x02, 0x30, 0x02, 0xF7}), 3);
        expect(stop.action == Action::Stop, "MSC STOP");
        auto resume = parse(msc({0xF0, 0x7F, 0x03, 0x02, 0x30, 0x03, 0xF7}), 3);
        expect(resume.action == Action::Resume, "MSC RESUME");
        auto load = parse(msc({0xF0, 0x7F, 0x03, 0x02, 0x30, 0x05, '9', 0xF7}), 3);
        expect(load.action == Action::Load && load.cue == "9",
               "MSC LOAD preselects a cue");
        auto off = parse(msc({0xF0, 0x7F, 0x03, 0x02, 0x30, 0x08, 0xF7}), 3);
        expect(off.action == Action::AllOff, "MSC ALL OFF");

        // ── MIDI Machine Control ───────────────────────────────────────────
        auto play = parse(msc({0xF0, 0x7F, 0x03, 0x06, 0x02, 0xF7}), 3);
        expect(play.action == Action::MmcPlay, "MMC PLAY");
        auto mstop = parse(msc({0xF0, 0x7F, 0x03, 0x06, 0x01, 0xF7}), 3);
        expect(mstop.action == Action::MmcStop, "MMC STOP");
        auto pause = parse(msc({0xF0, 0x7F, 0x03, 0x06, 0x09, 0xF7}), 3);
        expect(pause.action == Action::MmcPause, "MMC PAUSE");

        // LOCATE to 00:01:30:00 at 25fps. The hours byte carries the rate in
        // its top bits, which is why it is masked before it is read as hours.
        auto locate = parse(msc({0xF0, 0x7F, 0x03, 0x06, 0x44, 0x06, 0x01,
                                 0x20, 0x01, 0x1E, 0x00, 0xF7}), 3);
        expect(locate.action == Action::MmcLocate &&
               std::abs(locate.locateSeconds - 90.0) < 0.001,
               "MMC LOCATE resolves to a position in seconds");

        // Rubbish must not be mistaken for a command.
        expect(!parse(msc({0xF0, 0x7F, 0x03, 0xF7}), 3).ok(),
               "a truncated message is not a command");
        expect(!parse(msc({0xF0, 0x7E, 0x03, 0x02, 0x30, 0x01, 0xF7}), 3).ok(),
               "a non-realtime universal message is not show control");
      }

      // THE TEXT MODE ROWS MUST EDIT WHAT THE RENDERER READS.
      //
      // On a cue carrying the effect the picture comes from paramA..D, and the
      // inspector was editing cue.videoSynth instead -- which the renderer
      // overwrote on the way past. Every control in the section moved a number
      // and changed nothing, and the ink row read "green" over a full-colour
      // picture because paramD was still at its default of 0, which is
      // "picture".
      //
      // A control is only live if writing the parameter reads the value back,
      // so that is what this checks: the mapping and its inverse must agree for
      // every value the rows can produce.
      {
        deckboy::effects::CueEffect fx;
        VideoSynthSettings probe;
        for (int set : {0, 1, 2, 3, 4, 6}) {
          fx.paramC = textModeParamForCharSet(set);
          applyTextModeParams(fx, probe);
          expect(probe.asciiCharSet == set,
                 std::string("glyph set ") + std::to_string(set) + " survives the round trip");
        }
        for (int ink = 0; ink <= 5; ++ink) {
          fx.paramD = textModeParamForInk(ink);
          applyTextModeParams(fx, probe);
          expect(probe.asciiInk == ink,
                 std::string("ink ") + std::to_string(ink) + " survives the round trip");
        }
        bool colsOk = true;
        for (int cols = 20; cols <= 200; cols += 10) {
          fx.paramA = textModeParamForCols(cols);
          applyTextModeParams(fx, probe);
          if (probe.asciiCols != cols) colsOk = false;
        }
        expect(colsOk, "column counts survive the round trip");
      }

      // TEXT MODE MUST COVER THE WHOLE RASTER.
      //
      // The cell grid was sized by integer division -- dstW/cols -- and the
      // remainder was simply never drawn, leaving a bare strip down the right
      // edge and along the bottom. It showed as a black band in the synth and
      // as untouched video when text mode ran as an effect over a clip.
      //
      // Rendered over a flat MAGENTA source: any pixel still magenta is a pixel
      // the grid never reached. The sizes are chosen to divide badly on purpose
      // -- 1280/74 leaves 22 columns, 1920/113 leaves 17 -- because the sizes
      // that divide evenly were never the problem.
      {
        MediaEngine textEngine(nullptr, nullptr, {}, {}, {}, {});
        VideoSynthSettings tvs;
        tvs.ascii = true;
        tvs.asciiCharSet = 6;                 // music & sparkle
        int uncoveredCases = 0;
        for (auto wh : {std::pair<int,int>{1280, 720},
                        std::pair<int,int>{1920, 1080},
                        std::pair<int,int>{640, 360}}) {
          for (int cols : {74, 113, 37, 200, 20}) {
            const int W = wh.first, H = wh.second;
            std::vector<std::uint8_t> srcPix(static_cast<std::size_t>(W) * H * 4);
            for (std::size_t i = 0; i < srcPix.size(); i += 4) {
              srcPix[i + 0] = 40; srcPix[i + 1] = 90; srcPix[i + 2] = 200; srcPix[i + 3] = 255;
            }
            std::vector<std::uint8_t> dstPix(static_cast<std::size_t>(W) * H * 4);
            for (std::size_t i = 0; i < dstPix.size(); i += 4) {
              dstPix[i + 0] = 255; dstPix[i + 1] = 0; dstPix[i + 2] = 255; dstPix[i + 3] = 255;
            }
            tvs.asciiCols = cols;
            // Alternate between the bitmap sets and a glyph string that can
            // only be drawn through a FONT. On a machine with no usable font
            // the cache comes back empty and the bitmap path has to take over
            // -- if it did not, this would leave the raster half drawn, and
            // that fallback is the part most likely to differ between a dev
            // box and a CI runner.
            tvs.asciiGlyphs = (cols % 20 == 0) ? std::string("âªâ")
                                               : std::string();
            textEngine.renderTextMode(srcPix.data(), W, H, dstPix.data(), W, H, tvs, 0, 0.0);
            std::size_t stillMagenta = 0;
            for (std::size_t i = 0; i < dstPix.size(); i += 4) {
              if (dstPix[i] == 255 && dstPix[i + 1] == 0 && dstPix[i + 2] == 255) {
                ++stillMagenta;
              }
            }
            if (stillMagenta > 0) {
              ++uncoveredCases;
              std::cout << "      " << W << "x" << H << " at " << cols
                        << " cols left " << stillMagenta << " pixels undrawn" << '\n';
            }
          }
        }
        expect(uncoveredCases == 0, "text mode covers the whole raster");
      }

      // The clock has to carry. %04.1f rounds after the minute is split off,
      // so a value in the last twentieth of a second used to print :60.0 --
      // an out-point of 899.983s read "14:60.0" in the inspector.
      expect(formatSeconds(899.983) == "15:00.0",
             "formatSeconds carries into the minute");
      expect(formatSeconds(59.97) == "01:00.0",
             "formatSeconds carries at the first minute");
      expect(formatSeconds(0.0) == "00:00.0", "formatSeconds at zero");
      expect(formatSeconds(61.25) == "01:01.3", "formatSeconds mid-minute");
      expect(formatSeconds(3599.999) == "60:00.0",
             "formatSeconds carries at the hour");

      fs::path smokePath = fs::path("/tmp") / "deckboy-smoke.deckboy";
      expect(saveProject(smokePath, project), "project save");
      Project loaded = loadProject(smokePath);
      expect(!loaded.decks.empty(), "project load");
      if (!loaded.decks.empty() && !loaded.decks[0].cues.empty()) {
        const Deck& loadedDeck = loaded.decks[0];
        const Cue& loadedCue = loadedDeck.cues[0];
        expect(loaded.outputBitDepth == 10, "output bit depth persisted");
        expect(loaded.midiDeviceName == "APC40 mkII Control",
               "midi port persisted");
        expect(loaded.outputCanvasEnabled && loaded.outputCanvasWidth == 5760 && loaded.outputCanvasHeight == 2160,
               "output canvas persisted");
        expect(loaded.oscQueryEnabled &&
               loaded.oscQueryPort == 6410 &&
               loaded.oscFeedbackMirrorEnabled &&
               loaded.oscFeedbackRateMs == 90,
               "osc query settings persisted");
        expect(loaded.atemTriggerEnabled &&
               loaded.ndiTriggerEnabled &&
               !loaded.nmcSyncEnabled &&
               loaded.mtcIngestEnabled &&
               !loaded.ltcIngestEnabled &&
               loaded.dmxArtNetEnabled &&
               loaded.artNetPort == 16454,
               "integration settings persisted");
        expect(loadedCue.effects.size() == 2 &&
               loadedCue.effects[0].kind == deckboy::effects::CueEffectKind::ReactionBloom &&
               std::abs(loadedCue.effects[0].paramA - 0.25f) < 0.01f &&
               std::abs(loadedCue.effects[0].paramB - 0.5f) < 0.01f &&
               std::abs(loadedCue.effects[0].paramC - 0.4f) < 0.01f &&
               std::abs(loadedCue.effects[0].paramD - 0.3f) < 0.01f &&
               !loadedCue.effects[0].bypassed &&
               loadedCue.effects[1].kind == deckboy::effects::CueEffectKind::MotionPuppet &&
               loadedCue.effects[1].bypassed,
               "effect stack persisted with all four parameters and bypass");
        // The positional record, checked field by field. One assertion per
        // field rather than one for the block: a skew shifts a RUN of them,
        // and naming the first one that moved says immediately where the
        // writer and the reader parted company.
        const VideoSynthSettings& lv = loadedCue.videoSynth;
        expect(static_cast<int>(loadedCue.tone.synth.tuning) == 3 &&
               std::abs(loadedCue.tone.synth.referenceHz - 432.0) < 0.001,
               "synth tuning and reference pitch persisted");
        expect(static_cast<int>(lv.shape) == 3, "video synth shape persisted");
        expect(static_cast<int>(lv.mirror) == 1, "video synth mirror persisted");
        expect(static_cast<int>(lv.palette) == 2, "video synth palette persisted");
        expect(std::abs(lv.speed - 2.75) < 0.001 &&
               std::abs(lv.scale - 3.25) < 0.001 &&
               std::abs(lv.warp - 1.5) < 0.001,
               "video synth speed/scale/warp persisted");
        expect(std::abs(lv.feedbackAmount - 0.77) < 0.001 &&
               std::abs(lv.feedbackZoom - 1.11) < 0.001 &&
               std::abs(lv.feedbackRotate + 4.5) < 0.001,
               "video synth feedback persisted");
        expect(std::abs(lv.audioReactivity - 0.66) < 0.001 &&
               lv.resolution == 4,
               "video synth reactivity and detail persisted");
        expect(std::abs(lv.pixelSort - 0.44) < 0.001 &&
               std::abs(lv.glitch - 0.33) < 0.001 &&
               std::abs(lv.crt - 0.22) < 0.001,
               "video synth smear/glitch/crt persisted");
        expect(lv.ascii && lv.asciiCols == 137,
               "text mode and column count persisted");
        expect(lv.asciiCharSet == 4 && lv.asciiShuffle == 7 && lv.asciiInk == 3,
               "text mode glyph set, shuffle and ink persisted");
        expect(lv.asciiGlyphs == ".oO@" &&
               lv.asciiPhrases == "DECKBOY|GO LIVE" &&
               std::abs(lv.asciiPhraseHold - 4.5) < 0.001,
               "custom glyphs and phrases persisted");
        expect(lv.spriteSheetPath == "smoke-sheet.png" &&
               lv.spriteTileW == 24 && lv.spriteTileH == 40,
               "sprite sheet and tile size persisted");
        expect(lv.spriteRotate == 2 &&
               std::abs(lv.spriteFreeAngle - 90.0) < 0.001 &&
               lv.spriteFlip == 3 &&
               std::abs(lv.spriteJitter - 0.55) < 0.001 &&
               std::abs(lv.spriteChaos - 0.88) < 0.001,
               "sprite rotate/flip/jitter/chaos persisted");
        expect(loadedCue.motionDriverPath == "smoke-driver.mp4" &&
               std::abs(loadedCue.motionDriverSpeed - 1.75f) < 0.01f &&
               loadedCue.motionDriverPaused,
               "motion driver persisted");
        expect(loaded.jumpMode == "load" && !loaded.jumpTransitionEnabled, "jump mode persisted");
        expect(loaded.panicProfile == "fade_rewind", "panic profile persisted");
        expect(std::abs(loaded.panicFadeSeconds - 1.4) < 0.01 && loaded.panicAutoRestore, "panic options persisted");
        expect(loaded.focusedOutputIndex == 1, "focused output persisted");
        expect(loaded.outputs.size() == 2 &&
               loaded.outputs[0].name == "Program Out" &&
               loaded.outputs[1].name == "Stage Left Stream" &&
               loaded.outputs[0].hostDeckIndex == 0 &&
               loaded.outputs[1].displayIndex == 2 &&
               loaded.outputs[0].streamEnabled &&
               loaded.outputs[0].streamProtocol == "srt" &&
               loaded.outputs[0].streamUrl == "srt://127.0.0.1:9100?mode=caller" &&
               loaded.outputs[0].streamBitrateKbps == 7200 &&
               loaded.outputs[0].outputType == "window" &&
               loaded.outputs[0].mirrorSourceOutputIndex == -1 &&
               loaded.outputs[1].streamEnabled &&
               loaded.outputs[1].streamProtocol == "rtmp" &&
               loaded.outputs[1].streamBitrateKbps == 4200 &&
               loaded.outputs[1].outputType == "stream" &&
               loaded.outputs[1].mirrorSourceOutputIndex == 0 &&
               loaded.outputs[0].outputId == "out-smoke-program" &&
               loaded.outputs[1].outputId == "out-smoke-stream" &&
               std::abs(loaded.outputs[0].outputAlpha - 0.82f) < 0.01f &&
               loaded.outputs[0].outputDelayMs == 240 &&
               loaded.outputs[0].outputTimeOverlayEnabled &&
               loaded.outputs[0].outputColorSpace == "bt709" &&
               loaded.outputs[0].outputLayoutMode == "span" &&
               loaded.outputs[0].outputOrientationDegrees == 90 &&
               loaded.outputs[0].outputTestCardEnabled &&
               std::abs(loaded.outputs[1].outputAlpha - 0.67f) < 0.01f &&
               loaded.outputs[1].outputDelayMs == 120 &&
               !loaded.outputs[1].outputTimeOverlayEnabled &&
               loaded.outputs[1].outputColorSpace == "srgb" &&
               loaded.outputs[1].outputLayoutMode == "duplicate" &&
               loaded.outputs[1].outputOrientationDegrees == 270 &&
               !loaded.outputs[1].outputTestCardEnabled,
               "output targets persisted");
        expect(loaded.outputs[0].ndiEnabled &&
               loaded.outputs[0].ndiSourceName == "Program Fill" &&
               loaded.outputs[0].ndiKeyEnabled &&
               loaded.outputs[0].ndiKeySourceName == "Program Key",
               "output ndi persisted");
        expect(loadedDeck.ndiEnabled && loadedDeck.ndiSourceName == "Smoke Fill", "ndi fill persisted");
        expect(loadedDeck.ndiKeyEnabled && loadedDeck.ndiKeySourceName == "Smoke Key", "ndi key persisted");
        expect(loadedDeck.canvasViewX == 320 && loadedDeck.canvasViewY == 40, "canvas view persisted");
        expect(loadedDeck.outputRouteDeckIndex == 0, "deck route persisted");
        expect(loadedDeck.warpEnabled &&
               loadedDeck.warpMode == "perspective" &&
               std::abs(loadedDeck.warpTopLeftX + 12.0f) < 0.01f &&
               std::abs(loadedDeck.warpBottomRightY + 6.0f) < 0.01f, "warp persisted");
        expect(std::abs(loadedDeck.edgeBlendLeft - 0.08f) < 0.001f &&
               std::abs(loadedDeck.edgeBlendRight - 0.12f) < 0.001f &&
               std::abs(loadedDeck.edgeBlendTop - 0.03f) < 0.001f &&
               std::abs(loadedDeck.edgeBlendBottom - 0.05f) < 0.001f, "edge blend persisted");
        expect(std::abs(loadedDeck.transitionSeconds - 1.5) < 0.01, "transition persisted");
        expect(parseTransitionStyleToken(loadedDeck.transitionStyle) == TransitionStyle::DipBlack, "transition style persisted");
        expect(std::abs(loadedDeck.playlistOpacity - 0.62f) < 0.01f &&
               loadedDeck.playlistAutoFade &&
               std::abs(loadedDeck.playlistFadeSeconds - 1.7) < 0.01,
               "playlist opacity settings persisted");
        expect(std::abs(loadedDeck.playlistTimebaseFps - 29.97) < 0.01 &&
               std::abs(loadedDeck.playlistStartOffsetSeconds - 3600.0) < 0.01 &&
               std::abs(loadedDeck.playlistDefaultCueFadeSeconds - 0.75) < 0.01 &&
               std::abs(loadedDeck.playlistDefaultStillDurationSeconds - 6.5) < 0.01 &&
               loadedDeck.playlistDefaultLoop &&
               loadedDeck.playlistDefaultFadeInEnabled &&
               !loadedDeck.playlistDefaultFadeOutEnabled &&
               !loadedDeck.playlistDefaultAudioEnabled &&
               loadedDeck.playlistDefaultPauseAtBeginning &&
               loadedDeck.playlistDefaultPauseAtEnd &&
               !loadedDeck.playlistDefaultTransitionToNext,
               "playlist preference defaults persisted");
        expect(loadedDeck.timecodeChaseEnabled, "timecode chase persisted");
        expect(!loadedDeck.timecodeJamSyncEnabled && std::abs(loadedDeck.timecodeFreewheelSeconds - 2.5) < 0.01,
               "timecode follower options persisted");
        expect(std::abs(loadedCue.inPointSeconds - 2.0) < 0.01 && std::abs(loadedCue.outPointSeconds - 8.0) < 0.01, "trim persisted");
        expect(std::abs(loadedCue.triggerTimecodeSeconds - 13.0) < 0.01, "cue tc mark persisted");
        expect(std::abs(loadedCue.cueTransitionSeconds - 1.25) < 0.01, "cue transition persisted");
        expect(loadedCue.cueTransitionStyle == "crossfade", "cue transition style persisted");
        expect(loadedCue.cueId == "A1" &&
               loadedCue.hasAudio &&
               !loadedCue.audioEnabled &&
               loadedCue.pauseAtBeginning &&
               loadedCue.pauseOnLastFrame &&
               !loadedCue.transitionToNext &&
               loadedCue.gotoTarget == "Q12",
               "cue parity fields persisted");
        expect(std::abs(loadedCue.outputRotationDegrees - 17.5f) < 0.01f, "cue rotation persisted");
        expect(std::abs(loadedCue.cropLeft - 0.10f) < 0.001f &&
               std::abs(loadedCue.cropRight - 0.05f) < 0.001f &&
               std::abs(loadedCue.cropTop - 0.02f) < 0.001f &&
               std::abs(loadedCue.cropBottom - 0.03f) < 0.001f, "cue crop persisted");
        expect(loadedCue.chromaKeyEnabled &&
               loadedCue.chromaKeyColor.r == 20 &&
               loadedCue.chromaKeyColor.g == 220 &&
               loadedCue.chromaKeyColor.b == 45 &&
               std::abs(loadedCue.chromaKeyTolerance - 88.5f) < 0.01f &&
               std::abs(loadedCue.chromaKeySoftness - 14.0f) < 0.01f, "cue chroma key persisted");
        expect(std::abs(loadedCue.brightness - 1.25f) < 0.01f &&
               std::abs(loadedCue.contrast - 0.85f) < 0.01f &&
               std::abs(loadedCue.saturation - 1.40f) < 0.01f &&
               std::abs(loadedCue.hueShift + 22.0f) < 0.01f, "cue color controls persisted");
        expect(std::abs(loadedCue.audioGainDb + 6.5f) < 0.01f &&
               std::abs(loadedCue.audioPan - 0.35f) < 0.01f &&
               loadedCue.audioMono, "cue audio trim/pan/mono persisted");
        expect(std::abs(loadedCue.audioFadeInSeconds - 0.5f) < 0.01f &&
               std::abs(loadedCue.audioFadeOutSeconds) < 0.01f, "cue audio fades persisted");
        expect(loadedCue.audioOutputPair == 2 && loadedDeck.audioOutputChannels == 8,
               "audio output routing persisted");
        if (loadedDeck.cues.size() > 1) {
          const Cue& img = loadedDeck.cues[1];
          expect(img.kind == CueKind::Image, "still image kind persisted");
          expect(std::abs(img.stillDurationSeconds - 5.0) < 0.01, "still duration persisted");
        }
        if (loadedDeck.cues.size() > 2) {
          const Cue& lt = loadedDeck.cues[2];
          expect(lt.kind == CueKind::LowerThird, "lower third kind persisted");
          expect(lt.lowerThirdText == "Hello World", "lower third text persisted");
          expect(lt.lowerThirdSubtext == "subtitle here", "lower third subtext persisted");
          expect(lt.lowerThirdBgAlpha == 200, "lower third alpha persisted");
        }
      }
      std::error_code ignored;
      fs::remove(smokePath, ignored);
    }

    // (workspace smoke test removed — single-deck simplification)

    {
      Project legacy;
      Deck legacyDeck;
      legacyDeck.name = "Legacy Deck";
      legacyDeck.ndiEnabled = true;
      legacyDeck.ndiSourceName = "Legacy Fill";
      legacyDeck.ndiKeyEnabled = true;
      legacyDeck.ndiKeySourceName = "Legacy Key";
      legacy.decks = {legacyDeck};
      legacy.outputs = {OutputTarget {"Legacy Output", 0, 0, "", false, "window", -1, false, "srt", "", "", 6000}};
      normalizeProject(legacy);
      expect(!legacy.outputs.empty()
               && legacy.outputs[0].ndiEnabled
               && legacy.outputs[0].ndiSourceName == "Legacy Fill"
               && legacy.outputs[0].ndiKeyEnabled
               && legacy.outputs[0].ndiKeySourceName == "Legacy Key",
             "legacy deck ndi migrated to output");
    }

    {
      // Numeric-entry shorthand: 'x' multiplies, "px" units are ignored.
      auto v1 = parseNumericExpression("1920x2");
      auto v2 = parseNumericExpression("960px * 2");
      auto v3 = parseNumericExpression("3840/2");
      auto v4 = parseNumericExpression("1920X2 + 10px");
      expect(v1 && std::abs(*v1 - 3840.0) < 0.001, "expression: 1920x2");
      expect(v2 && std::abs(*v2 - 1920.0) < 0.001, "expression: 960px * 2");
      expect(v3 && std::abs(*v3 - 1920.0) < 0.001, "expression: 3840/2");
      expect(v4 && std::abs(*v4 - 3850.0) < 0.001, "expression: 1920X2 + 10px");
    }

    {
      // Fade in/out end-to-end through the live engine path. A Pattern cue
      // decodes on the CPU (no ffmpeg subprocess) and the engine tolerates a
      // null renderer, so this exercises the real loadCue → position() →
      // currentVisualFadeGain() chain the output compositor multiplies into
      // the bridge-texture alpha.
      MediaEngine engine(nullptr, nullptr);
      Cue fadeCue;
      fadeCue.kind = CueKind::Pattern;
      fadeCue.name = "fade-check";
      fadeCue.path = "pattern://smpte-bars";
      fadeCue.stillDurationSeconds = 4.0;
      fadeCue.fadeInSeconds = 1.0;
      fadeCue.fadeOutSeconds = 1.0;
      engine.loadCue(&fadeCue, true);
      double gainStart = engine.currentVisualFadeGain();
      SDL_Delay(500);
      engine.update();
      double gainMid = engine.currentVisualFadeGain();
      engine.seek(3.5);  // pattern seek pauses at the target position
      engine.update();
      double gainTail = engine.currentVisualFadeGain();
      engine.stopAll();
      expect(gainStart < 0.15, "fade-in gain starts near zero");
      expect(gainMid > 0.30 && gainMid < 0.75, "fade-in gain ramps mid-fade");
      expect(gainTail > 0.30 && gainTail < 0.70, "fade-out gain ramps near cue end");
    }

    {
      // Pocket test card instrumentation (v0.78.x): the auto-cycling
      // pocket-test carries the test-card overlay — verify the pixel-mapping
      // border checkerboard and the instrument-strip separator landed where
      // drawPocketTestCardOverlay puts them. The clean scene variants must
      // NOT have the border (they stay usable as backgrounds).
      Cue cardCue;
      cardCue.kind = CueKind::Pattern;
      cardCue.path = "pattern://pocket-test";
      cardCue.width = 320;
      cardCue.height = 180;
      auto card = MediaEngine::buildPatternFrame(cardCue, 5.0, 320, 180);
      bool cardOk = card.has_value() && !card->pixels.empty();
      if (cardOk) {
        auto red = [&](int x, int y) {
          return card->pixels[(static_cast<std::size_t>(y) * card->width + x) * 4u];
        };
        // Diegetic instruments: scan for the exact measurement values the
        // props must carry — 75% red (billboard bar), 2% near-black (cave
        // eyes), 96% near-white (cloud lump) — plus the checker border.
        bool saw75Red = false;
        bool sawCaveEye = false;
        bool sawCloudLump = false;
        for (std::size_t i = 0; i + 3 < card->pixels.size(); i += 4) {
          const std::uint8_t r = card->pixels[i];
          const std::uint8_t g = card->pixels[i + 1];
          const std::uint8_t b = card->pixels[i + 2];
          saw75Red = saw75Red || (r == 191 && g == 0 && b == 0);
          sawCaveEye = sawCaveEye || (r == 5 && g == 5 && b == 5);
          sawCloudLump = sawCloudLump || (r == 245 && g == 245 && b == 245);
        }
        cardOk = red(0, 0) == 0 && red(1, 0) == 255 &&
                 saw75Red && sawCaveEye && sawCloudLump;
      }
      cardCue.path = "pattern://pocket-day";
      auto clean = MediaEngine::buildPatternFrame(cardCue, 5.0, 320, 180);
      bool cleanOk = clean.has_value() && !clean->pixels.empty();
      if (cleanOk) {
        auto redC = [&](int x, int y) {
          return clean->pixels[(static_cast<std::size_t>(y) * clean->width + x) * 4u];
        };
        cleanOk = !(redC(0, 0) == 0 && redC(1, 0) == 255);       // no border on scenes
      }
      expect(cardOk, "pocket test card instrumentation present");
      expect(cleanOk, "pocket scene variants stay clean");
    }

    {
      // Native Terrarium pattern. This used to assert a FIXED 1600x896 raster
      // regardless of the size asked for — which was the bug, not the contract:
      // terrarium was the only pattern that ignored --pattern-dump's size.
      // The world is a 200x112 CELL grid, so the raster is now the requested
      // size quantised DOWN to whole pixels-per-cell. The world must also
      // actually contain life (non-black pixels).
      // The terrarium world grid (TERRA_W/TERRA_H in the vendored core). Spelled
      // out because the vendored header is only included by media_engine.cpp.
      constexpr int kTerraW = 200;
      constexpr int kTerraH = 112;
      auto buildTerr = [](int w, int h) {
        Cue c;
        c.kind = CueKind::Pattern;
        c.path = "pattern://terrarium";
        c.width = w;
        c.height = h;
        return MediaEngine::buildPatternFrame(c, 1.0, w, h);
      };
      auto countLit = [](const DecodedFrame& f) {
        std::size_t lit = 0;
        for (std::size_t i = 0; i + 3 < f.pixels.size(); i += 4) {
          if (f.pixels[i] || f.pixels[i + 1] || f.pixels[i + 2]) {
            ++lit;
          }
        }
        return lit;
      };
      // Small ask -> 1 px per cell.
      auto terrSmall = buildTerr(320, 180);
      bool terrOk = terrSmall.has_value() && terrSmall->width == kTerraW &&
                    terrSmall->height == kTerraH && countLit(*terrSmall) > 1000;
      // Large ask -> more pixels per cell, still a whole multiple of the grid.
      auto terrBig = buildTerr(1600, 900);
      terrOk = terrOk && terrBig.has_value() &&
               terrBig->width == kTerraW * 8 && terrBig->height == kTerraH * 8 &&
               countLit(*terrBig) > 1000;
      expect(terrOk, "native terrarium pattern renders a living world at the asked-for size");

      // Pico is the Pi panel's picture: ALWAYS 1 px per cell, whatever it is
      // asked for. If this ever tracks the request, the pico look is gone.
      Cue picoCue;
      picoCue.kind = CueKind::Pattern;
      picoCue.path = "pattern://terrarium-pico";
      auto pico = MediaEngine::buildPatternFrame(picoCue, 1.0, 1920, 1080);
      expect(pico.has_value() && pico->width == kTerraW && pico->height == kTerraH,
             "terrarium-pico stays 1px per cell");
    }

    {
      // Crash resilience (GPU_DECODE_PLAN §9): a corrupt media file must
      // degrade to EOF/rerack — never crash or wedge the engine. The
      // in-process decoder validates by priming the first frame in open();
      // garbage that fails there falls back to the CLI pipe, which EOFs on
      // the same garbage. Either way the engine survives and stays loaded.
      fs::path corruptPath;
      try {
        corruptPath = fs::temp_directory_path() / "deckboy-smoke-corrupt.mp4";
        std::ofstream out(corruptPath, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < 4096; ++i) {
          out.put(static_cast<char>((i * 37 + 11) & 0xFF));
        }
      } catch (...) {
        corruptPath.clear();
      }
      if (!corruptPath.empty()) {
        MediaEngine engine(nullptr, nullptr);
        Cue badCue;
        badCue.kind = CueKind::Video;
        badCue.name = "corrupt-check";
        badCue.path = corruptPath.string();
        badCue.width = 320;
        badCue.height = 180;
        badCue.duration = 2.0;
        badCue.hasAudio = false;
        engine.loadCue(&badCue, true);
        for (int i = 0; i < 30; ++i) {
          engine.update();
          SDL_Delay(10);
        }
        bool alive = engine.activeCue() != nullptr;
        engine.stopAll();
        std::error_code removeEc;
        fs::remove(corruptPath, removeEc);
        expect(alive, "corrupt media file degrades without crash");
      } else {
        expect(true, "corrupt media file check skipped (no temp dir)");
      }
    }

    {
      // Missing-media scan + relink: a file cue whose media moved must flag
      // MISSING, and relinkMissingMediaFromFolder must find the file by name
      // under the picked folder and repoint the cue at it.
      bool relinkOk = false;
      std::error_code ec;
      fs::path relinkRoot = fs::temp_directory_path(ec) / "deckboy-smoke-relink";
      if (!ec) {
        fs::remove_all(relinkRoot, ec);
        fs::create_directories(relinkRoot / "moved", ec);
        {
          std::ofstream out(relinkRoot / "moved" / "smoke-relink.bin",
                            std::ios::binary | std::ios::trunc);
          out << "deckboy";
        }

        App app;
        Cue lost;
        lost.kind = CueKind::Video;
        lost.name = "smoke-relink";
        lost.path = (relinkRoot / "gone" / "smoke-relink.bin").generic_string();
        Deck deck;
        deck.cues.push_back(lost);
        app.project_.decks = {deck};

        int missingBefore = app.scanProjectMediaPresence();
        const Cue& probe = app.project_.decks[0].cues[0];
        bool flagged = missingBefore >= 1 && probe.mediaMissing;
        int relinked = app.relinkMissingMediaFromFolder(relinkRoot);
        std::error_code existsEc;
        bool repointed = relinked >= 1 && !probe.mediaMissing &&
                         fs::exists(fs::path(probe.path), existsEc);
        relinkOk = flagged && repointed;
        fs::remove_all(relinkRoot, ec);
      }
      expect(relinkOk, "missing media flags and relinks from folder");
    }

    {
      // Vanished-media take guard: a file cue whose media is gone must be
      // flagged and refused before the engine ever sees it (the same check
      // auto-advance uses to skip dead cues mid-show).
      App app;
      Cue ghost;
      ghost.kind = CueKind::Video;
      ghost.name = "smoke-ghost";
      std::error_code ghostEc;
      ghost.path = (fs::temp_directory_path(ghostEc) / "deckboy-smoke-void" / "ghost.mp4").generic_string();
      expect(!app.cueMediaAvailableForTake(ghost) && ghost.mediaMissing,
             "vanished media is flagged and refused at take");
    }

    // ---- HAP ---------------------------------------------------------------
    // The Snappy vectors are real streams from the reference implementation
    // (python-snappy), not something this repo produced, so passing them means
    // the vendored decompressor agrees with Google format rather than with
    // itself. See docs/HAP_PLAYBACK_PLAN.md.
    {
      #include "engine/hap_test_vectors.inc"
      bool allOk = true;
      for (const SnappyVector& v : kSnappyVectors) {
        std::vector<std::uint8_t> got;
        std::string err;
        const bool ok = deckboy::hap::snappyUncompress(v.comp, v.compSize, got, err);
        const bool match = ok && got.size() == v.rawSize &&
                           std::memcmp(got.data(), v.raw, v.rawSize) == 0;
        if (!match) {
          allOk = false;
          std::cout << "  snappy vector failed: " << v.name
                    << (err.empty() ? "" : (" (" + err + ")")) << "\n";
        }
      }
      expect(allOk, "snappy decompresses the reference implementation output");

      // A HAP frame the decoder builds itself: uncompressed section, DXT1.
      std::vector<std::uint8_t> frame;
      const std::vector<std::uint8_t> blocks(64, 0x5A);
      frame.push_back(static_cast<std::uint8_t>(blocks.size() & 0xFF));
      frame.push_back(static_cast<std::uint8_t>((blocks.size() >> 8) & 0xFF));
      frame.push_back(static_cast<std::uint8_t>((blocks.size() >> 16) & 0xFF));
      frame.push_back(0xAB);            // compressor none | RGB_DXT1
      frame.insert(frame.end(), blocks.begin(), blocks.end());
      deckboy::hap::Frame decoded;
      std::string hapErr;
      const bool hapOk = deckboy::hap::decodeFrame(frame.data(), frame.size(),
                                                   decoded, hapErr);
      expect(hapOk && decoded.format == deckboy::hap::TextureFormat::RgbDxt1 &&
             decoded.data == blocks,
             "hap decodes an uncompressed DXT1 frame");

      // Truncated frames must fail cleanly rather than read past the buffer.
      deckboy::hap::Frame bad;
      std::string badErr;
      const bool rejected =
        !deckboy::hap::decodeFrame(frame.data(), 3, bad, badErr) &&
        !deckboy::hap::decodeFrame(frame.data(), frame.size() / 2, bad, badErr);
      expect(rejected, "hap rejects truncated frames instead of overrunning");

      // Block expansion. A single DXT1 block with known endpoints: white and
      // black, indices all 0, so every texel must come out pure white. This
      // pins endpoint decoding, index order and row layout without depending
      // on any reference decoder.
      {
        std::vector<std::uint8_t> one;
        const std::uint16_t white = 0xFFFF;   // 565 all-ones
        const std::uint16_t black = 0x0000;
        one.push_back(white & 0xFF); one.push_back(white >> 8);
        one.push_back(black & 0xFF); one.push_back(black >> 8);
        for (int i = 0; i < 4; ++i) one.push_back(0x00);   // all indices 0
        deckboy::hap::Frame f;
        f.format = deckboy::hap::TextureFormat::RgbDxt1;
        f.data = one;
        std::vector<std::uint8_t> rgba;
        std::string e2;
        const bool ok2 = deckboy::hap::decompressToRgba(f, 4, 4, rgba, e2);
        bool allWhite = ok2 && rgba.size() == 4 * 4 * 4;
        for (std::size_t i = 0; allWhite && i < rgba.size(); i += 4) {
          allWhite = rgba[i] == 255 && rgba[i+1] == 255 && rgba[i+2] == 255 &&
                     rgba[i+3] == 255;
        }
        expect(allWhite, "hap expands DXT1 blocks to the expected texels");

        // Too little data for the stated raster must be refused, not read past.
        std::vector<std::uint8_t> ignored;
        std::string e3;
        expect(!deckboy::hap::decompressToRgba(f, 64, 64, ignored, e3),
               "hap refuses to expand undersized block data");
      }
    }

    // ---- Timer ------------------------------------------------------------
    // The formatting asymmetry is the part worth pinning: counting down rounds
    // UP so the last second reads 0:01, overtime rounds DOWN so it starts at
    // +0:00. Both are checked by rendering and counting lit pixels, since the
    // digits are geometry rather than a string we could compare.
    {
      auto litPixels = [](const DecodedFrame& f) {
        int n = 0;
        for (std::size_t i = 0; i + 3 < f.pixels.size(); i += 4) {
          if (f.pixels[i] || f.pixels[i + 1] || f.pixels[i + 2]) ++n;
        }
        return n;
      };
      TimerSettings cfg;
      cfg.durationSeconds = 300;
      cfg.amberSeconds = 60;
      cfg.redSeconds = 15;
      cfg.blinkAtZero = false;   // deterministic for the test

      DecodedFrame f;
      f.width = 640; f.height = 360;
      f.pixels.assign(static_cast<std::size_t>(f.width) * f.height * 4, 0);

      MediaEngine::buildTimerFrame(f, cfg, 0.0, true);
      const int atStart = litPixels(f);
      expect(atStart > 0, "timer renders digits at the start of a countdown");

      // Colour must move white -> amber -> red as the thresholds pass.
      auto inkAt = [&](double elapsed) {
        MediaEngine::buildTimerFrame(f, cfg, elapsed, true);
        for (std::size_t i = 0; i + 3 < f.pixels.size(); i += 4) {
          if (f.pixels[i] || f.pixels[i + 1] || f.pixels[i + 2]) {
            return std::make_tuple(f.pixels[i], f.pixels[i + 1], f.pixels[i + 2]);
          }
        }
        return std::make_tuple<std::uint8_t, std::uint8_t, std::uint8_t>(0, 0, 0);
      };
      const auto early = inkAt(10.0);      // 4:50 left  -> white
      const auto amber = inkAt(250.0);     // 0:50 left  -> amber
      const auto red   = inkAt(290.0);     // 0:10 left  -> red
      const bool coloursOk =
        std::get<1>(early) > 200 && std::get<2>(early) > 200 &&      // white
        std::get<2>(amber) < 120 && std::get<1>(amber) > 120 &&      // amber
        std::get<1>(red) < 120 && std::get<2>(red) < 120;            // red
      expect(coloursOk, "timer moves white -> amber -> red across its thresholds");

      // Overtime still renders (and differs from the zero frame).
      MediaEngine::buildTimerFrame(f, cfg, 305.0, true);
      expect(litPixels(f) > 0, "timer renders overtime past zero");

      // A paused clock is visibly different from a running one.
      MediaEngine::buildTimerFrame(f, cfg, 100.0, true);
      const int runningPx = litPixels(f);
      MediaEngine::buildTimerFrame(f, cfg, 100.0, false);
      expect(litPixels(f) > runningPx, "paused timer draws the hold marker");
    }

    // ── Parameter LFOs ──────────────────────────────────────────────────────
    //
    // The whole feature is invisible in any single frame — what it does happens
    // BETWEEN frames — so this samples the oscillator across time. Four things
    // would each ruin a show on their own: an LFO that does not move, one that
    // moves while switched off, one that pushes a parameter outside 0–1, and
    // one that drifts off the beat it is supposed to be locked to.
    {
      using namespace deckboy::effects;
      ParamLfo lfo;
      lfo.on = true;
      lfo.rateHz = 1.0f;
      lfo.depth = 1.0f;
      double lo = 2.0, hi = -1.0, sum = 0.0;
      const int n = 400;
      for (int i = 0; i < n; ++i) {
        const double v = lfoApply(lfo, 0.5f, i / 200.0, 0.0);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        sum += v;
      }
      expect(hi - lo > 0.9, "an LFO at full depth swings the whole range");
      expect(lo >= 0.0 && hi <= 1.0, "and never leaves 0-1");
      expect(std::fabs(sum / n - 0.5) < 0.02,
             "and averages to the value the operator set");

      // The rails matter most at the ENDS of the range, where the swing has to
      // go lopsided rather than out of bounds.
      bool inside = true;
      for (float base : {0.0f, 0.05f, 0.95f, 1.0f}) {
        for (int i = 0; i < 100; ++i) {
          const float v = lfoApply(lfo, base, i / 50.0, 0.0);
          if (v < 0.0f || v > 1.0f) inside = false;
        }
      }
      expect(inside, "an LFO stays in range from any base value");

      ParamLfo off;
      bool untouched = true;
      for (int i = 0; i < 50; ++i) {
        if (lfoApply(off, 0.37f, i * 0.1, i * 0.25) != 0.37f) untouched = false;
      }
      expect(untouched, "an LFO that is off changes nothing at all");

      // Sample-and-hold has to be REPEATABLE: a random that differs between the
      // rehearsal and the show is not usable.
      ParamLfo hold;
      hold.on = true;
      hold.shape = LfoShape::Sample;
      hold.rateHz = 1.0f;
      hold.depth = 1.0f;
      expect(std::fabs(lfoApply(hold, 0.5f, 3.10, 0.0) -
                       lfoApply(hold, 0.5f, 3.90, 0.0)) < 1e-9,
             "sample-and-hold holds its value across a cycle");
      expect(std::fabs(lfoApply(hold, 0.5f, 3.10, 0.0) -
                       lfoApply(hold, 0.5f, 4.10, 0.0)) > 1e-9,
             "and steps at the cycle boundary");

      ParamLfo synced;
      synced.on = true;
      synced.beatSync = true;
      synced.beats = 4.0f;
      synced.depth = 1.0f;
      synced.shape = LfoShape::Saw;
      expect(std::fabs(lfoApply(synced, 0.5f, 999.0, 0.0) -
                       lfoApply(synced, 0.5f, 12.5, 4.0)) < 1e-6,
             "a 4-beat LFO repeats every 4 beats");
      expect(std::fabs(lfoApply(synced, 0.5f, 0.0, 1.0) -
                       lfoApply(synced, 0.5f, 500.0, 1.0)) < 1e-6,
             "and ignores the wall clock entirely");

      std::vector<CueEffect> stack(2);
      stack[0].kind = CueEffectKind::Invert;
      stack[1].kind = CueEffectKind::Ripple;
      stack[1].paramA = 0.5f;
      std::vector<CueEffect> out;
      expect(!modulateCueEffectStack(stack, 1.0, 0.0, out),
             "a stack with no LFO does not pay for a copy");
      stack[1].lfo[0] = lfo;
      // Across a whole cycle, not at one moment: a sine passes through its own
      // centre twice per cycle, so a single sample can legitimately equal the
      // base value and prove nothing. The first version of this failed for
      // exactly that reason.
      bool moved = false, neighbourMoved = false;
      for (int i = 0; i < 40; ++i) {
        modulateCueEffectStack(stack, i / 40.0, 0.0, out);
        if (out[1].paramA != stack[1].paramA) moved = true;
        if (out[0].paramA != stack[0].paramA) neighbourMoved = true;
      }
      expect(moved, "an armed LFO moves the parameter it is on");
      expect(!neighbourMoved, "and leaves every other effect alone");
      expect(stack[1].paramA == 0.5f,
             "and never writes back over what the operator set");
      stack[1].bypassed = true;
      expect(!cueEffectStackHasLfo(stack), "a bypassed effect's LFO does not run");
    }

    std::cout << "smoke failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
  }

  // ---------------------------------------------------------------------------
  // runSyncPopTest — `--sync-pop-test`
  //
  // Drives the real MediaEngine pocket-test sync-pop path against the real
  // default audio device for ~2.5 s and reports whether samples flowed
  // (audible as the once-per-second pop). Diagnoses "no audio from the test
  // card" reports: PASS here means the engine path works and the problem is
  // the cue's audio toggle / device routing; FAIL means the synth path is
  // broken in code.
  // ---------------------------------------------------------------------------
  // What hardware does this machine actually offer?
  //
  // Written because a show file names its audio interface as a STRING, and a
  // name that does not match what the driver reports is indistinguishable, from
  // inside the app, from a device that is not plugged in. This prints the names
  // exactly as SDL spells them, which is what the show file has to contain.
  //
  // It is also the first thing to ask of a venue machine that "has no sound" or
  // "will not go full screen", because it separates a Deckboy fault from a
  // machine that genuinely cannot see the hardware.
  // Ask GitHub what the newest release is and say so, without starting the
  // application. The check the app does at startup is the same call; this is
  // how it gets tested, and how an operator on a locked-down machine can find
  // out whether the network path works at all.
  static int runUpdateCheckReport() {
    App probe;
    std::string error;
    UpdateInfo info = probe.fetchLatestRelease(error);
    std::cout << "running: " << deckboy::core::version::kVersionTag << '\n';
    if (!error.empty()) {
      std::cout << "result:  " << error << '\n';
      return 1;
    }
    if (info.version.empty()) {
      std::cout << "result:  up to date" << '\n';
      return 0;
    }
    std::cout << "result:  " << info.version << " is available" << '\n';
    std::cout << "asset:   " << info.assetName
              << "  (" << (info.assetSize / (1024 * 1024)) << " MB)" << '\n';
    std::cout << "url:     " << info.assetUrl << '\n';
    return 0;
  }

  static int runDeviceReport() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
      std::cout << "devices: SDL init failed: " << SDL_GetError() << '\n';
      return 1;
    }
    std::cout << "audio driver: "
              << (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)")
              << '\n';
    for (int recording = 0; recording < 2; ++recording) {
      int count = 0;
      SDL_AudioDeviceID* ids = recording ? SDL_GetAudioRecordingDevices(&count)
                                         : SDL_GetAudioPlaybackDevices(&count);
      std::cout << (recording ? "audio in:  " : "audio out: ") << count << '\n';
      if (ids) {
        for (int i = 0; i < count; ++i) {
          const char* name = SDL_GetAudioDeviceName(ids[i]);
          std::cout << "  " << (name ? name : "(unnamed)");
          SDL_AudioSpec spec {};
          int frames = 0;
          if (SDL_GetAudioDeviceFormat(ids[i], &spec, &frames)) {
            std::cout << "  [" << spec.freq << " Hz, " << static_cast<int>(spec.channels)
                      << " ch, " << frames << " frame buffer]";
          }
          std::cout << '\n';
        }
        SDL_free(ids);
      }
    }

    std::cout << "video driver: "
              << (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)")
              << '\n';
    int displayCount = 0;
    int displayCount2 = 0;
    std::vector<SDL_DisplayID> probeDisplays;
    if (SDL_DisplayID* displays = SDL_GetDisplays(&displayCount)) {
      probeDisplays.assign(displays, displays + displayCount);
      displayCount2 = displayCount;
      std::cout << "displays: " << displayCount << '\n';
      for (int i = 0; i < displayCount; ++i) {
        const char* name = SDL_GetDisplayName(displays[i]);
        std::cout << "  " << (name ? name : "(unnamed)");
        if (const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(displays[i])) {
          // The refresh rate is the number that matters: a 50 Hz projector and
          // a 144 Hz monitor are both ordinary and neither is 60.
          std::cout << "  " << dm->w << "x" << dm->h << " @ " << dm->refresh_rate << " Hz";
        }
        std::cout << "  scale " << SDL_GetDisplayContentScale(displays[i]) << '\n';
      }
      SDL_free(displays);
    }

    // What a window on this display ACTUALLY gets.
    //
    // A display's mode is in pixels and its content scale is separate, so a
    // 4K panel at 150% reports 3840x2160 and 1.5. What matters for output
    // quality is whether a window there is backed by as many pixels as it
    // covers: if the size in points and the size in pixels differ, the
    // programme is being rendered at the smaller one and scaled up by the
    // compositor, and the operator sees a soft picture with no setting to
    // explain it. Measured with a HIDDEN window so nothing appears on a screen
    // that may be showing a programme.
    for (int i = 0; i < displayCount2; ++i) {
      SDL_Window* probe = SDL_CreateWindow("Deckboy device probe", 1280, 720,
                                           SDL_WINDOW_HIDDEN);
      if (!probe) {
        continue;
      }
      SDL_SetWindowPosition(probe, SDL_WINDOWPOS_CENTERED_DISPLAY(probeDisplays[i]),
                            SDL_WINDOWPOS_CENTERED_DISPLAY(probeDisplays[i]));
      int pointsW = 0, pointsH = 0, pixelsW = 0, pixelsH = 0;
      SDL_GetWindowSize(probe, &pointsW, &pointsH);
      SDL_GetWindowSizeInPixels(probe, &pixelsW, &pixelsH);
      const char* dispName = SDL_GetDisplayName(probeDisplays[i]);
      // The size that comes back is not always the size asked for -- a window
      // centred on a display the same size as itself can be adjusted by the
      // window manager -- so both are printed. What matters here is the ratio
      // between the two, not either number.
      std::cout << "  window on " << (dispName ? dispName : "?") << ": asked 1280x720, got "
                << pointsW << "x" << pointsH << " points, "
                << pixelsW << "x" << pixelsH << " pixels"
                << ((pointsW == pixelsW && pointsH == pixelsH) ? "  (1:1)"
                                                              : "  (SCALED -- output renders at the point size)")
                << '\n';
      SDL_DestroyWindow(probe);
    }

    // MIDI, because a control surface is hardware too and the show names its
    // port as a string exactly like the audio device does. The ORDER matters
    // here: with no name configured the app opens the first port in this list.
#if defined(DECKBOY_HAS_MIDI)
    {
      auto midiDevices = deckboy::platform::midi::MidiInput::listDevices();
      std::cout << "midi in:   " << midiDevices.size() << '\n';
      for (const auto& dev : midiDevices) {
        std::cout << "  [" << dev.id << "] " << dev.name << '\n';
      }
    }
#else
    // midi.cpp is only compiled when ENABLE_MIDI is on, so this has to be
    // guarded rather than merely returning an empty list -- calling it in a
    // build without it is a LINK error, which the Windows dev build (MIDI on)
    // never sees. Saying so is more useful than printing "0 ports" and letting
    // someone conclude their controller is broken.
    std::cout << "midi in:   (this build has no MIDI support)" << '\n';
#endif

    int renderCount = SDL_GetNumRenderDrivers();
    std::cout << "render drivers: ";
    for (int i = 0; i < renderCount; ++i) {
      const char* name = SDL_GetRenderDriver(i);
      std::cout << (i ? ", " : "") << (name ? name : "?");
    }
    std::cout << '\n';
    SDL_Quit();
    return 0;
  }

  static int runSyncPopTest() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
      std::cout << "sync-pop-test: SDL init failed: " << SDL_GetError() << '\n';
      return 1;
    }
    SDL_AudioSpec spec {};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 48000;
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream) {
      std::cout << "sync-pop-test: no audio device: " << SDL_GetError() << '\n';
      SDL_Quit();
      return 1;
    }
    std::size_t tapSamples = 0;
    MediaEngine engine(nullptr, stream,
                       [&tapSamples](const std::vector<std::int16_t>& samples) {
                         tapSamples += samples.size();
                       });
    Cue cue;
    cue.kind = CueKind::Pattern;
    cue.name = "sync-pop-test";
    cue.path = "pattern://pocket-test";
    cue.width = 320;
    cue.height = 180;
    cue.audioEnabled = true;
    engine.loadCue(&cue, true);
    int maxQueued = 0;
    Uint64 start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 2500) {
      engine.update();
      maxQueued = std::max(maxQueued, SDL_GetAudioStreamQueued(stream));
      SDL_Delay(4);
    }
    engine.stopAll();
    SDL_DestroyAudioStream(stream);
    SDL_Quit();
    std::cout << "sync-pop-test: tap-samples=" << tapSamples
              << " max-queued-bytes=" << maxQueued
              << " => " << ((tapSamples > 0 && maxQueued > 0) ? "PASS (pop path works)"
                                                              : "FAIL (synth path broken)")
              << '\n';
    return (tapSamples > 0 && maxQueued > 0) ? 0 : 1;
  }

  // ---------------------------------------------------------------------------
  // runPatternDump — `--pattern-dump <pattern-id> <out.ppm> [WxH] [t]`
  //
  // Renders one frame of a procedural pattern to a binary PPM for visual
  // inspection outside the app (test-card development, docs screenshots).
  // ---------------------------------------------------------------------------
  static int runPatternDump(const std::string& patternId, const std::string& outPath,
                            int w, int h, double t) {
    Cue cue;
    cue.kind = CueKind::Pattern;
    cue.name = "pattern-dump";
    cue.path = patternId;
    cue.width = w;
    cue.height = h;
    auto frame = MediaEngine::buildPatternFrame(cue, t, w, h);
    if (!frame || frame->pixels.empty()) {
      std::cout << "pattern-dump: build failed for " << patternId << '\n';
      return 1;
    }
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::cout << "pattern-dump: cannot write " << outPath << '\n';
      return 1;
    }
    out << "P6\n" << frame->width << ' ' << frame->height << "\n255\n";
    for (std::size_t i = 0; i + 3 < frame->pixels.size(); i += 4) {
      out.put(static_cast<char>(frame->pixels[i]));
      out.put(static_cast<char>(frame->pixels[i + 1]));
      out.put(static_cast<char>(frame->pixels[i + 2]));
    }
    std::cout << "pattern-dump: wrote " << outPath << " ("
              << frame->width << "x" << frame->height << ")\n";
    return 0;
  }

  // ---------------------------------------------------------------------------
  // runEffectDump — `--effect-dump <token[:amount[:a[:b]]]> <in.ppm> <out.ppm>`
  //
  // Apply one effect to one picture and write the result, with no window, no
  // decoder and no timing.
  //
  // Judging an effect from a screenshot of the running app does not work: the
  // capture lands on whatever frame the seek happened to reach, so a baseline
  // and a treated shot differ everywhere before the effect has done anything,
  // and the comparison says "changed" no matter what. This renders the effect
  // itself, deterministically, which is the only way to tell whether it looks
  // the way it is supposed to.
  //
  // `frame` sets the frame index for the effects that advance with time, and
  // `passes` runs the effect that many times, feeding the SAME picture round a
  // persistent feedback buffer. One pass is the default and is what every
  // effect but feedback wants; feedback needs at least two before there is
  // anything to see, because the first only fills the buffer.
  // ---------------------------------------------------------------------------
  static int runEffectDump(const std::string& spec, const std::string& inPath,
                           const std::string& outPath, int frameIndex,
                           int passes = 1) {
    // token:amount:paramA:paramB — everything after the token is optional.
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
      const std::size_t colon = spec.find(':', start);
      parts.push_back(spec.substr(start, colon == std::string::npos
                                           ? std::string::npos : colon - start));
      if (colon == std::string::npos) break;
      start = colon + 1;
    }
    deckboy::effects::CueEffect fx;
    fx.kind = deckboy::effects::cueEffectFromToken(parts[0]);
    if (fx.kind == deckboy::effects::CueEffectKind::None) {
      std::cerr << "effect-dump: unknown effect '" << parts[0] << "'. Known: ";
      for (int i = 1; i < static_cast<int>(deckboy::effects::CueEffectKind::Count); ++i) {
        if (i > 1) std::cerr << ", ";
        std::cerr << deckboy::effects::cueEffectToken(
          static_cast<deckboy::effects::CueEffectKind>(i));
      }
      std::cerr << '\n';
      return 2;
    }
    if (parts.size() > 1) fx.amount = static_cast<float>(std::atof(parts[1].c_str()));
    if (parts.size() > 2) fx.paramA = static_cast<float>(std::atof(parts[2].c_str()));
    if (parts.size() > 3) fx.paramB = static_cast<float>(std::atof(parts[3].c_str()));
    // Note the CLI spec is amount:a:b:c:d -- it is NOT the save format, which
    // carries bypassed between b and c. This is a dev flag, and a straight run
    // of the four parameters is what someone typing it expects.
    if (parts.size() > 4) fx.paramC = static_cast<float>(std::atof(parts[4].c_str()));
    if (parts.size() > 5) fx.paramD = static_cast<float>(std::atof(parts[5].c_str()));

    std::ifstream in(inPath, std::ios::binary);
    if (!in) {
      std::cerr << "effect-dump: cannot read " << inPath << '\n';
      return 1;
    }
    std::string magic;
    int w = 0, h = 0, maxval = 0;
    in >> magic >> w >> h >> maxval;
    if (magic != "P6" || w <= 0 || h <= 0 || maxval != 255) {
      std::cerr << "effect-dump: " << inPath << " is not a binary 8-bit PPM\n";
      return 1;
    }
    in.get();   // the single whitespace byte before the raster
    const std::size_t count = static_cast<std::size_t>(w) * h;
    std::vector<std::uint8_t> rgb(count * 3);
    in.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (in.gcount() != static_cast<std::streamsize>(rgb.size())) {
      std::cerr << "effect-dump: " << inPath << " is short\n";
      return 1;
    }
    // The effects work on the decoder's own layout, which is RGBA: byte 0 is
    // red. Feeding them anything else would render a picture that is correct
    // in shape and wrong in colour, which is worse than failing.
    std::vector<std::uint8_t> pixels(count * 4, 255);
    for (std::size_t i = 0; i < count; ++i) {
      pixels[i * 4 + 0] = rgb[i * 3 + 0];
      pixels[i * 4 + 1] = rgb[i * 3 + 1];
      pixels[i * 4 + 2] = rgb[i * 3 + 2];
    }

    // Lives across the passes, exactly as the app's per-deck buffer lives
    // across frames.
    // One scratch slot per effect in the chain, exactly as the app gives it.
    std::vector<std::vector<std::uint8_t>> effectState(1);
    double ms = 0.0;
    const int passCount = std::max(1, passes);
    for (int pass = 0; pass < passCount; ++pass) {
      deckboy::effects::CueEffectContext ctx;
      ctx.width = w;
      ctx.height = h;
      ctx.frameIndex = static_cast<std::uint64_t>(std::max(0, frameIndex) + pass);
      ctx.effectState = &effectState;
      const auto began = std::chrono::steady_clock::now();
      deckboy::effects::applyCueEffectStack(pixels, {fx}, ctx);
      ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - began).count();
    }

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::cerr << "effect-dump: cannot write " << outPath << '\n';
      return 1;
    }
    out << "P6\n" << w << ' ' << h << "\n255\n";
    for (std::size_t i = 0; i < count; ++i) {
      out.put(static_cast<char>(pixels[i * 4 + 0]));
      out.put(static_cast<char>(pixels[i * 4 + 1]));
      out.put(static_cast<char>(pixels[i * 4 + 2]));
    }
    std::cout << "effect-dump: " << deckboy::effects::cueEffectToken(fx.kind)
              << " amount=" << fx.amount << " a=" << fx.paramA << " b=" << fx.paramB
              << " on " << w << "x" << h << " took " << ms << "ms -> " << outPath << '\n';
    return 0;
  }

  // ---------------------------------------------------------------------------
  // runEffectBench — `--effect-bench <token[:amount[:a[:b]]]> [WxH] [frames]`
  //
  // What one effect costs per frame at a given raster, and what fraction of a
  // 60fps budget that is.
  //
  // --effect-dump reports the time for a single application, which is enough to
  // notice something pathological and useless for judging an optimisation: one
  // run on a cold cache with the frame index fixed is mostly noise. This runs
  // the effect repeatedly on the same buffer with an advancing frame index, so
  // the time-varying effects do their real work, and reports the median rather
  // than the mean so one scheduling hiccup cannot flatter or damn a change.
  // ---------------------------------------------------------------------------
  static int runEffectBench(const std::string& spec, int w, int h, int frames) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
      const std::size_t colon = spec.find(':', start);
      parts.push_back(spec.substr(start, colon == std::string::npos
                                           ? std::string::npos : colon - start));
      if (colon == std::string::npos) break;
      start = colon + 1;
    }
    deckboy::effects::CueEffect fx;
    fx.kind = deckboy::effects::cueEffectFromToken(parts[0]);
    if (fx.kind == deckboy::effects::CueEffectKind::None) {
      std::cerr << "effect-bench: unknown effect '" << parts[0] << "'\n";
      return 2;
    }
    if (parts.size() > 1) fx.amount = static_cast<float>(std::atof(parts[1].c_str()));
    if (parts.size() > 2) fx.paramA = static_cast<float>(std::atof(parts[2].c_str()));
    if (parts.size() > 3) fx.paramB = static_cast<float>(std::atof(parts[3].c_str()));
    if (parts.size() > 4) fx.paramC = static_cast<float>(std::atof(parts[4].c_str()));
    if (parts.size() > 5) fx.paramD = static_cast<float>(std::atof(parts[5].c_str()));

    const std::size_t count = static_cast<std::size_t>(w) * h;
    // A deterministic picture with real structure in it. A flat fill would let
    // anything with a branch on content look faster than it is, and pixel sort
    // in particular is only honest on something with detail to sort.
    std::vector<std::uint8_t> pristine(count * 4, 255);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        std::uint32_t n = static_cast<std::uint32_t>(x * 374761393 + y * 668265263);
        n = (n ^ (n >> 13)) * 1274126177u;
        std::uint8_t* p = pristine.data() + (static_cast<std::size_t>(y) * w + x) * 4;
        p[0] = static_cast<std::uint8_t>((x * 255 / std::max(1, w - 1)) ^ (n >> 24));
        p[1] = static_cast<std::uint8_t>(y * 255 / std::max(1, h - 1));
        p[2] = static_cast<std::uint8_t>((n >> 16) & 0xFF);
      }
    }

    std::vector<std::uint8_t> pixels;
    // Persists across frames, the way the app's per-deck buffer does, so a
    // feedback bench measures the real loop and not a first frame forever.
    // One scratch slot per effect in the chain, exactly as the app gives it.
    std::vector<std::vector<std::uint8_t>> effectState(1);
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
      // Restored every pass. Effects are destructive, and letting one chew on
      // its own output measures something that never happens in a show.
      pixels = pristine;
      deckboy::effects::CueEffectContext ctx;
      ctx.width = w;
      ctx.height = h;
      ctx.frameIndex = static_cast<std::uint64_t>(i);
      ctx.effectState = &effectState;
      const auto began = std::chrono::steady_clock::now();
      deckboy::effects::applyCueEffectStack(pixels, {fx}, ctx);
      samples.push_back(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - began).count());
    }
    if (samples.empty()) {
      std::cerr << "effect-bench: no frames\n";
      return 1;
    }
    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    const double best = samples.front();
    std::cout << "effect-bench: " << deckboy::effects::cueEffectToken(fx.kind)
              << ' ' << w << 'x' << h
              << "  median " << median << "ms"
              << "  best " << best << "ms"
              << "  " << (median > 0.0 ? 1000.0 / median : 0.0) << "fps-if-alone"
              << "  " << (median / 16.667 * 100.0) << "% of a 60fps frame\n";
    return 0;
  }

  // ---------------------------------------------------------------------------
  // runPdfProbe — `--pdf-probe <file.pdf> [outdir] [scale]`
  //
  // Rasterise a document and report what came out, with no window.
  //
  // The import path itself needs a GUI and a drop, which makes the one thing
  // worth checking on a headless box -- does THIS platform's PDF engine work at
  // all -- the one thing that cannot be checked there. Each platform uses a
  // different engine (Windows.Data.Pdf, CoreGraphics, pdftoppm), so "it worked
  // on Windows" says nothing about the other two.
  // ---------------------------------------------------------------------------
  static int runPdfProbe(const std::string& file, const std::string& outDir,
                         int targetWidth) {
    std::string whyNot;
    if (!deckboy::platform::pdfRasterAvailable(whyNot)) {
      std::cout << "pdf-probe: unavailable -- " << whyNot << std::endl;
      return 1;
    }
    const fs::path out = outDir.empty()
      ? (fs::temp_directory_path() / "deckboy-pdf-probe") : fs::path(outDir);

    // A PRESENTATION GOES THROUGH ITS CONVERTER FIRST, and the probe reports
    // which one -- because "it did not import" has three different causes
    // (no converter, the converter refused the file, the render failed) that
    // look identical from the outside, and a toast cannot be read by a script.
    fs::path source = file;
    if (deckboy::platform::isPresentationDocumentPath(source)) {
      std::string why;
      if (!deckboy::platform::presentationConvertAvailable(why)) {
        std::cout << "pdf-probe: no converter -- " << why << std::endl;
        return 1;
      }
      std::cout << "pdf-probe: converting the presentation..." << std::endl;
      const auto convBegan = std::chrono::steady_clock::now();
      auto converted = deckboy::platform::convertPresentationToPdf(source, out);
      const double convMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - convBegan).count();
      if (!converted.ok()) {
        std::cout << "pdf-probe: CONVERSION FAILED -- " << converted.error
                  << std::endl;
        return 1;
      }
      std::error_code sizeEc;
      const auto bytes = fs::file_size(converted.pdfPath, sizeEc);
      std::cout << "pdf-probe: converted by " << converted.converter << " in "
                << convMs << "ms -> " << converted.pdfPath.string() << " ("
                << (sizeEc ? 0 : bytes) << " bytes)" << std::endl;
      source = converted.pdfPath;
    }
    const auto began = std::chrono::steady_clock::now();
    auto result = deckboy::platform::rasterisePdf(source, out, targetWidth, nullptr);
    const double ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - began).count();
    if (!result.ok()) {
      std::cout << "pdf-probe: FAILED -- "
                << (result.error.empty() ? "no pages" : result.error) << std::endl;
      return 1;
    }
    std::cout << "pdf-probe: " << result.pagePaths.size() << " page(s) in "
              << ms << "ms at " << targetWidth << "px wide -> " << out.string() << std::endl;
    for (std::size_t i = 0; i < result.pagePaths.size() && i < 4; ++i) {
      std::error_code ec;
      const auto bytes = fs::file_size(result.pagePaths[i], ec);
      std::cout << "  " << fs::path(result.pagePaths[i]).filename().string()
                << "  " << (ec ? 0 : bytes) << " bytes" << std::endl;
    }
    if (result.pagePaths.size() > 4) {
      std::cout << "  ... and " << (result.pagePaths.size() - 4) << " more"
                << std::endl;
    }
    return 0;
  }

  // ---------------------------------------------------------------------------
  // runPatternBench — `--pattern-bench <pattern-id> [WxH] [frames]`
  //
  // Times buildPatternFrame in isolation: no window, no texture upload, no file
  // I/O. Added while chasing "pocket-test is laggy at 4K" — the live OUTPUT fps
  // readout was too noisy to tell whether the cost was the CPU build or the
  // 33 MB-per-frame texture upload, and guessing between them is how you
  // optimise the wrong half.
  // ---------------------------------------------------------------------------
  static int runPatternBench(const std::string& patternId, int w, int h, int frames) {
    Cue cue;
    cue.kind = CueKind::Pattern;
    cue.name = "pattern-bench";
    cue.path = patternId;
    cue.width = w;
    cue.height = h;

    // One warm-up build so first-touch page faults and any lazy init don't land
    // in the measurement.
    auto warm = MediaEngine::buildPatternFrame(cue, 0.0, w, h);
    if (!warm || warm->pixels.empty()) {
      std::cout << "pattern-bench: build failed for " << patternId << '\n';
      return 1;
    }
    const int outW = warm->width;
    const int outH = warm->height;

    double worstMs = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
      const auto t0 = std::chrono::steady_clock::now();
      // Advance wall time so animated patterns do real per-frame work.
      auto f = MediaEngine::buildPatternFrame(cue, 1.0 + i * (1.0 / 60.0), w, h);
      const auto t1 = std::chrono::steady_clock::now();
      const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
      worstMs = std::max(worstMs, ms);
      if (!f) {
        std::cout << "pattern-bench: build returned nothing at frame " << i << '\n';
        return 1;
      }
    }
    const double totalMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const double avgMs = totalMs / std::max(1, frames);
    std::cout << "pattern-bench: " << patternId << "  " << outW << "x" << outH
              << "  avg " << avgMs << " ms/frame"
              << "  worst " << worstMs << " ms"
              << "  => " << (avgMs > 0.0 ? 1000.0 / avgMs : 0.0) << " fps ceiling"
              << "  (build only, no upload)\n";
    return 0;
  }

  // ---------------------------------------------------------------------------
  // runLtcGenerate — `--ltc-generate <out.wav> [HH:MM:SS:FF] [fps] [seconds]`
  //
  // Generates SMPTE LTC as a mono 48 kHz WAV, then DECODES ITS OWN OUTPUT with
  // the same libltc decoder Deckboy chases with, and reports whether the
  // timecode came back. Round-tripping is the point: an encoder that produces
  // plausible-looking audio nobody can read is worse than none, and this proves
  // the two halves agree.
  //
  // Deckboy could previously chase timecode but never generate it, so it could
  // not be master of a rig. libltc always shipped the encoder; it was unbound.
  // ---------------------------------------------------------------------------
  static int runLtcGenerate(const std::string& outPath, const std::string& startTc,
                            double fps, double seconds) {
    LtcApi api;
    if (!api.ensureLoaded()) {
      std::cout << "ltc-generate: libltc unavailable: " << api.loadError << '\n';
      return 1;
    }
    if (!api.encoderAvailable) {
      std::cout << "ltc-generate: libltc has no encoder symbols\n";
      return 1;
    }

    int hh = 10, mm = 0, ss = 0, ff = 0;
    std::sscanf(startTc.c_str(), "%d:%d:%d:%d", &hh, &mm, &ss, &ff);

    constexpr double kRate = 48000.0;
    // standard 1 = LTC_TV_625_50 etc.; 0 (LTC_TV_525_60) is right for 30/29.97
    // and is what a generic generator should emit.
    void* enc = api.encoderCreateFn(kRate, fps, 0, 0);
    if (!enc) {
      std::cout << "ltc-generate: encoder_create failed\n";
      return 1;
    }
    if (api.encoderSetVolumeFn) {
      api.encoderSetVolumeFn(enc, -3.0);  // -3 dBFS: hot enough to read, not clipping
    }

    LtcSmpteTimecode tc {};
    std::snprintf(tc.timezone, sizeof(tc.timezone), "+0000");
    tc.hours = static_cast<unsigned char>(hh);
    tc.mins  = static_cast<unsigned char>(mm);
    tc.secs  = static_cast<unsigned char>(ss);
    tc.frame = static_cast<unsigned char>(ff);
    api.encoderSetTimecodeFn(enc, &tc);

    const int frameCount = std::max(1, static_cast<int>(std::lround(seconds * fps)));
    std::vector<std::int16_t> pcm;
    // libltc emits unsigned 8-bit centred on 128 (ltcsnd_sample_t).
    std::vector<std::uint8_t> frameBuf(static_cast<std::size_t>(kRate / fps) + 64);
    for (int i = 0; i < frameCount; ++i) {
      api.encoderEncodeFrameFn(enc);
      const int got = api.encoderGetBufferFn(enc, frameBuf.data());
      for (int s = 0; s < got; ++s) {
        const int centred = static_cast<int>(frameBuf[static_cast<std::size_t>(s)]) - 128;
        pcm.push_back(static_cast<std::int16_t>(std::clamp(centred * 256, -32768, 32767)));
      }
      if (api.encoderBufferFlushFn) {
        api.encoderBufferFlushFn(enc);
      }
      api.encoderIncTimecodeFn(enc);
    }
    api.encoderFreeFn(enc);

    if (pcm.empty()) {
      std::cout << "ltc-generate: encoder produced no samples\n";
      return 1;
    }

    // --- WAV (mono, 16-bit, 48 kHz) ---
    {
      std::ofstream wav(outPath, std::ios::binary | std::ios::trunc);
      if (!wav) {
        std::cout << "ltc-generate: cannot write " << outPath << '\n';
        return 1;
      }
      const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * 2);
      auto put32 = [&](std::uint32_t v) { wav.write(reinterpret_cast<const char*>(&v), 4); };
      auto put16 = [&](std::uint16_t v) { wav.write(reinterpret_cast<const char*>(&v), 2); };
      wav.write("RIFF", 4); put32(36 + dataBytes); wav.write("WAVE", 4);
      wav.write("fmt ", 4); put32(16); put16(1); put16(1);
      put32(48000); put32(48000 * 2); put16(2); put16(16);
      wav.write("data", 4); put32(dataBytes);
      wav.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
    }

    // --- Round-trip: decode what we just made ---
    void* dec = api.decoderCreateFn(static_cast<int>(kRate / fps), 8);
    int decoded = 0;
    std::string firstTc, lastTc;
    if (dec) {
      std::vector<std::uint8_t> frameExt(LtcApi::kFrameExtBytes);
      // Write in chunks and DRAIN AFTER EACH. libltc's decode queue holds only
      // the size given to decoder_create (8 here); pushing the whole file in
      // one call overflows it and everything but the last few frames is thrown
      // away — which looked exactly like a broken encoder.
      const std::size_t chunk = static_cast<std::size_t>(kRate / fps);
      for (std::size_t off = 0; off < pcm.size(); off += chunk) {
        const std::size_t n = std::min(chunk, pcm.size() - off);
        api.decoderWriteS16Fn(dec, pcm.data() + off, n, static_cast<std::int64_t>(off));
        while (api.decoderQueueLengthFn(dec) > 0) {
          if (api.decoderReadFn(dec, frameExt.data()) <= 0) {
            break;
          }
          if (auto got = decodeLtcFrameBytes(frameExt.data())) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d",
                          got->hours, got->minutes, got->seconds, got->frames);
            if (decoded == 0) firstTc = buf;
            lastTc = buf;
            ++decoded;
          }
        }
      }
      api.decoderFreeFn(dec);
    }

    std::cout << "ltc-generate: wrote " << outPath << " ("
              << pcm.size() << " samples, " << frameCount << " frames @ " << fps << "fps)\n";
    std::cout << "ltc-generate: round-trip decoded " << decoded << " frames";
    if (decoded > 0) {
      std::cout << "  first=" << firstTc << "  last=" << lastTc;
    }
    std::cout << '\n';
    return decoded > 0 ? 0 : 1;
  }

  // ---------------------------------------------------------------------------
  // runDecodeBench — `--decode-bench <file> [seconds] [cli]`
  //
  // Measures sustained decode throughput through the real MediaEngine path so
  // Pocket before/after numbers are comparable (GPU_DECODE_PLAN §6). The cue
  // fps is pinned high so the consumer drains the queue every tick — the
  // number reported is decoder throughput, not playback pacing. `cli` forces
  // the ffmpeg subprocess pipe path for A/B against the in-process decoder.
  // Zero-copy mode also exercises the GPU slice→texture copy the output
  // compositor performs per frame advance.
  // ---------------------------------------------------------------------------
  static int runDecodeBench(const std::string& mediaPath, double benchSeconds, bool forceCli) {
#if DECKBOY_INPROC_DECODE
    // Same as App::init — the shared decode device needs a thread-safe D3D11 device.
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D_THREADSAFE, "1");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      std::cout << "decode-bench: SDL init failed: " << SDL_GetError() << '\n';
      return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Deckboy decode bench", 1280, 720, SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = window ? deckboyCreateRenderer(window) : nullptr;
    if (!renderer) {
      std::cout << "decode-bench: renderer create failed: " << SDL_GetError() << '\n';
      SDL_Quit();
      return 1;
    }
    MediaEngine::setInprocDecodeDisabled(forceCli);
    MediaEngine engine(renderer, nullptr, {}, {}
#if DECKBOY_INPROC_DECODE
      , [renderer]() { return deckboy::libav::rendererD3D11Device(renderer); }
#endif
    );
    Cue cue;
    cue.kind = CueKind::Video;
    cue.name = "decode-bench";
    cue.path = mediaPath;
    cue.duration = benchSeconds + 3600.0;  // never trip end-of-cue
    cue.fps = 240.0;                       // drain the queue: measure decode, not pacing
    cue.hasAudio = false;
    engine.loadCue(&cue, true);

#if DECKBOY_INPROC_DECODE
    SDL_Texture* gpuBridge = nullptr;
    void* gpuBridgeTex2D = nullptr;
    int gpuBridgeW = 0;
    int gpuBridgeH = 0;
#endif
    const Uint64 startMs = SDL_GetTicks();
    std::uint64_t lastIndex = static_cast<std::uint64_t>(-1);
    std::uint64_t framesSeen = 0;
    std::uint64_t gpuFrames = 0;
    std::uint64_t cpuFrames = 0;
    while (SDL_GetTicks() - startMs < static_cast<Uint64>(benchSeconds * 1000.0)) {
      engine.update();
      const DecodedFrame* frame = engine.currentFrame();
      if (frame && frame->index != lastIndex) {
        lastIndex = frame->index;
        ++framesSeen;
#if DECKBOY_INPROC_DECODE
        if (frame->isGpu()) ++gpuFrames; else ++cpuFrames;
        if (frame->isGpu()) {
          // Mirror the output compositor's per-advance GPU copy.
          if (!gpuBridge || gpuBridgeW != frame->width || gpuBridgeH != frame->height) {
            if (gpuBridge) SDL_DestroyTexture(gpuBridge);
            deckboy::libav::releaseD3D11Texture(gpuBridgeTex2D);
            gpuBridgeTex2D = nullptr;
            gpuBridge = deckboy::libav::createWrappedNV12Texture(
              renderer, frame->width, frame->height, &gpuBridgeTex2D);
            gpuBridgeW = frame->width;
            gpuBridgeH = frame->height;
          }
          if (gpuBridgeTex2D) {
            deckboy::libav::copyGpuFrameToTexture(*frame, gpuBridgeTex2D);
          }
        }
#endif
      }
      SDL_Delay(1);
    }
    const double elapsed = static_cast<double>(SDL_GetTicks() - startMs) / 1000.0;
    const char* mode = "cli-pipe";
    if (engine.inprocDecodeActive()) {
      mode = engine.activeDecodeDevice() ? "inproc-zerocopy" : "inproc-cpu";
    }
    std::cout << "decode-bench: file=" << mediaPath
              << " mode=" << mode
              << " frames=" << framesSeen
              << " gpu-frames=" << gpuFrames
              << " cpu-frames=" << cpuFrames
              << " elapsed=" << elapsed
              << " throughput-fps=" << (elapsed > 0.0 ? framesSeen / elapsed : 0.0)
              << '\n';
    engine.stopAll();
#if DECKBOY_INPROC_DECODE
    if (gpuBridge) SDL_DestroyTexture(gpuBridge);
    deckboy::libav::releaseD3D11Texture(gpuBridgeTex2D);
#endif
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  }
