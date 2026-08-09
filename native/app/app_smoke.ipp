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
      normalizeProject(project);

      fs::path smokePath = fs::path("/tmp") / "deckboy-smoke.deckboy";
      expect(saveProject(smokePath, project), "project save");
      Project loaded = loadProject(smokePath);
      expect(!loaded.decks.empty(), "project load");
      if (!loaded.decks.empty() && !loaded.decks[0].cues.empty()) {
        const Deck& loadedDeck = loaded.decks[0];
        const Cue& loadedCue = loadedDeck.cues[0];
        expect(loaded.outputBitDepth == 10, "output bit depth persisted");
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
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
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
