// ============================================================================
// app_render_file.ipp -- RENDER TO FILE
//
// A file made frame by frame, not a recording. RECORD films the programme as it
// plays, so a laptop that cannot play 4K60 live records a 4K60 file that stutters.
// This draws each frame when the one before it is written: a slow machine takes
// longer and the file comes out perfect.
//
// THE FRAME CLOCK. While a render runs, the show's time is a number the render
// owns: frame g of the job is g / fps seconds, whatever the wall clock says.
//   - a generated source is built at that moment (buildPatternFrameInto)
//   - the LFOs read it (the thread's own LfoClockSample, cue_effects.hpp --
//     the live loop publishes wall time on every tick and must not leak in)
//   - an effect gets that frame's index and seconds
//   - a video file is decoded by ffmpeg at exactly the job's rate, through a
//     pipe: ffmpeg waits for the render to read, so nothing is dropped
// Nothing here touches the live decks, the outputs or the desk's clocks, so a
// show can go on playing while a render runs, and the same render gives the
// same file twice (tools/check_render_to_file.py proves both).
//
// NOT FRAME-EXACT YET, and refused with a reason in the dialog rather than
// drawn wrong: live sources (camera, window, NDI, stream, capture), browser
// pages, Timer, Terrarium (its simulation keeps its own clock), rotation and
// geometry LFOs, text mode, the motion puppet and Audioprint. Not drawn at all:
// sound, transitions (cuts between cues), layers, masks, slices.
// ============================================================================

  // ---- What a job is made of ------------------------------------------------
  enum class RenderCodec { ProRes4444 = 0, H264 = 1, Png = 2 };

  struct RenderFileSegment {
    Cue cue;
    int frames = 0;
    int cueNumber = 0;   // 1-based, as the list shows it
  };
  struct RenderFilePlan {
    std::vector<RenderFileSegment> segments;
    int width = 1920;
    int height = 1080;
    int fps = 30;
    RenderCodec codec = RenderCodec::ProRes4444;
    std::string path;    // the file; for PNG frames, the folder
    double bpm = 120.0;  // the beat the LFOs count in
    long long totalFrames = 0;
  };
  struct RenderFileJob {
    std::atomic<long long> done {0};
    std::atomic<bool> cancel {false};
    std::atomic<bool> finished {false};
    std::atomic<bool> ok {false};
    long long total = 0;
    Uint64 startedMs = 0;
    std::string path;
    std::mutex mutex;
    std::string result;   // set once, when finished
  };
  std::shared_ptr<RenderFileJob> renderJob_;
  std::string renderLastResult_;   // what the last job did, for RENDER STATUS and the dialog

  struct RenderDialogState {
    bool open = false;
    int what = 0;       // 0 this cue, 1 from cue N to M, 2 an output's whole show
    int size = 0;       // 0 1080p, 1 4K, 2 the output's
    int fps = 1;        // index into kRenderFps
    int codec = 0;
    int from = 1;
    int to = 1;
    std::string path;   // empty: a name is made from the cue
    SDL_Rect panel {}, close {}, start {}, cancel {}, choose {}, whatBtn {}, sizeBtn {}, fpsBtn {}, codecBtn {};
    SDL_Rect fromMinus {}, fromPlus {}, toMinus {}, toPlus {};
  } renderDlg_;
  static constexpr int kRenderFps[4] = {25, 30, 50, 60};

  // ---- Small helpers --------------------------------------------------------
  static std::string renderCount(long long n) {
    std::string digits = std::to_string(n), out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
      if (i && (digits.size() - i) % 3 == 0) out += ',';
      out += digits[i];
    }
    return out;
  }
  static std::string renderTimeLeft(double seconds) {
    const long long s = static_cast<long long>(std::lround(std::max(0.0, seconds)));
    if (s >= 3600) return std::to_string(s / 3600) + " h " + std::to_string((s / 60) % 60) + " min";
    if (s >= 60) return std::to_string(s / 60) + " min " + std::to_string(s % 60) + " s";
    return std::to_string(s) + " s";
  }
  static const char* renderCodecLabel(int codec) {
    return codec == 1 ? "H.264" : codec == 2 ? "PNG frames" : "ProRes 4444 (alpha)";
  }
  static const char* renderCodecName(RenderCodec codec) {
    return codec == RenderCodec::H264 ? "h264" : codec == RenderCodec::Png ? "png" : "prores";
  }

  // Why a cue cannot be drawn frame by frame yet. Empty: it can.
  static std::string renderRefusal(const Cue& cue) {
    switch (cue.kind) {
      case CueKind::Video:
        if (cue.duration <= 0.0) return "its length is not known";
        if (cue.width <= 0 || cue.height <= 0) return "its picture size is not known";
        break;
      case CueKind::Image:
        if (cue.width <= 0 || cue.height <= 0) return "its picture size is not known";
        break;
      case CueKind::Pattern:
        if (cue.path.find("terrarium") != std::string::npos) return "the Terrarium keeps its own clock";
        break;
      default:
        return cueKindLabel(cue.kind) + " cues cannot be drawn by frames yet";
    }
    if (std::fabs(cue.outputRotationDegrees) > 0.001f) return "its rotation is not drawn by a render yet";
    if (cueHasGeometryLfo(cue)) return "its size and position oscillators are not drawn by a render yet";
    for (const deckboy::effects::CueEffect& fx : cue.effects) {
      if (fx.bypassed || fx.kind == deckboy::effects::CueEffectKind::None) continue;
      if (fx.kind == deckboy::effects::CueEffectKind::TextMode) return "Text mode is not drawn by a render yet";
      if (deckboy::effects::cueEffectNeedsDriver(fx.kind)) return "the motion puppet needs the live driver";
    }
    if (deckboy::effects::cueEffectStackDrawsWithAudio(cue.effects)) return "Audioprint needs the live sound";
    return {};
  }

  // ---- The plan: what to draw, how many frames --------------------------------
  bool planRenderFile(int what, int from, int to, int sizeChoice, int fps, int codec, const std::string& pathIn,
                      RenderFilePlan& plan, std::string& why) {
    plan = RenderFilePlan {};
    int deckIndex = project_.focusedDeckIndex;
    int outputIndex = project_.focusedOutputIndex;
    const bool haveOutput = outputIndex >= 0 && outputIndex < static_cast<int>(project_.outputs.size());
    if (what == 2 && haveOutput) deckIndex = project_.outputs[static_cast<std::size_t>(outputIndex)].hostDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      why = "there is no playlist to render";
      return false;
    }
    const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
    const int count = static_cast<int>(deck.cues.size());
    int first = 0, last = count - 1;
    if (what == 0) {
      first = last = deck.selectedIndex >= 0 ? deck.selectedIndex : deck.activeIndex;
      if (first < 0 || first >= count) {
        why = "select a cue first";
        return false;
      }
    } else if (what == 1) {
      if (from > count || to > count) {
        why = "the playlist has " + std::to_string(count) + " cues";
        return false;
      }
      first = std::clamp(from - 1, 0, std::max(0, count - 1));
      last = std::clamp(to - 1, 0, std::max(0, count - 1));
      if (last < first) std::swap(first, last);
    }
    if (count == 0) {
      why = "the playlist has no cues";
      return false;
    }
    if (fps != 25 && fps != 30 && fps != 50 && fps != 60) {
      why = "frames a second: 25, 30, 50 or 60";
      return false;
    }
    plan.fps = fps;
    plan.codec = codec == 1 ? RenderCodec::H264 : codec == 2 ? RenderCodec::Png : RenderCodec::ProRes4444;
    plan.bpm = std::clamp(project_.vjTempoBpm, 20.0, 300.0);
    if (sizeChoice == 1) {
      plan.width = 3840;
      plan.height = 2160;
    } else if (sizeChoice == 2) {
      const auto size = haveOutput ? outputRequestedRenderSize(outputIndex) : fixedOutputRenderSize();
      plan.width = size.first;
      plan.height = size.second;
    }
    plan.width = std::max(16, plan.width & ~1);
    plan.height = std::max(16, plan.height & ~1);
    for (int i = first; i <= last; ++i) {
      const Cue& cue = deck.cues[static_cast<std::size_t>(i)];
      if (cue.kind == CueKind::Audio || cue.kind == CueKind::Tone) continue;   // no picture
      const std::string refusal = renderRefusal(cue);
      if (!refusal.empty()) {
        why = "Cue " + std::to_string(i + 1) + ": " + refusal;
        return false;
      }
      double seconds = 0.0;
      if (cue.kind == CueKind::Video) {
        const double end = cue.outPointSeconds > 0.0 ? cue.outPointSeconds : cue.duration;
        seconds = std::max(0.0, end - cue.inPointSeconds) / std::clamp(cue.playbackSpeed, 0.1, 4.0);
      } else {
        seconds = cue.stillDurationSeconds > 0.0 ? cue.stillDurationSeconds
                                                 : std::max(1.0, deck.playlistDefaultStillDurationSeconds);
      }
      RenderFileSegment seg;
      seg.cue = cue;
      seg.cueNumber = i + 1;
      seg.frames = std::max(1, static_cast<int>(std::lround(seconds * fps)));
      plan.totalFrames += seg.frames;
      plan.segments.push_back(std::move(seg));
    }
    if (plan.segments.empty()) {
      why = "there is no picture in that range";
      return false;
    }
    std::string path = pathIn;
    if (path.empty()) {
      fs::path dir = project_.recordingDir.empty() ? Paths::stateDir() / "renders" : fs::u8path(project_.recordingDir);
      std::string stem = what == 0 ? drawFileStem(plan.segments.front().cue) : "render";
      stem += "-" + std::to_string(plan.width) + "x" + std::to_string(plan.height) + "-" + std::to_string(fps) + "fps";
      path = (dir / stem).string();
    }
    // The extension follows the codec, so a name typed without one still opens.
    fs::path p = fs::u8path(path);
    if (plan.codec == RenderCodec::Png) {
      p.replace_extension();
    } else {
      const std::string ext = plan.codec == RenderCodec::H264 ? ".mp4" : ".mov";
      if (toLower(p.extension().string()) != ext) p.replace_extension(ext);
    }
    plan.path = p.string();
    return true;
  }

  // ---- The picture of one frame, placed in the output -------------------------
  // The cue's crop, scale mode, size and offset, as cuePlacementFor works them
  // out, drawn with a bilinear sample. Rotation and the oscillators are refused
  // before a job starts, so they are not here.
  static void renderPlaceInCanvas(const Cue& cue, const std::vector<std::uint8_t>& src, int sw, int sh,
                                  std::vector<std::uint8_t>& canvas, int W, int H) {
    canvas.assign(static_cast<std::size_t>(W) * H * 4, 0);
    const int cropL = std::clamp(static_cast<int>(std::lround(sw * cue.cropLeft)), 0, sw - 1);
    const int cropR = std::clamp(static_cast<int>(std::lround(sw * cue.cropRight)), 0, sw - 1);
    const int cropT = std::clamp(static_cast<int>(std::lround(sh * cue.cropTop)), 0, sh - 1);
    const int cropB = std::clamp(static_cast<int>(std::lround(sh * cue.cropBottom)), 0, sh - 1);
    const int srcW = std::max(1, sw - cropL - cropR);
    const int srcH = std::max(1, sh - cropT - cropB);
    double bx = 1.0, by = 1.0;
    if (cue.scaleMode == ScaleMode::Fit) {
      bx = by = std::min(static_cast<double>(W) / srcW, static_cast<double>(H) / srcH);
    } else if (cue.scaleMode == ScaleMode::Fill) {
      bx = by = std::max(static_cast<double>(W) / srcW, static_cast<double>(H) / srcH);
    } else if (cue.scaleMode == ScaleMode::Stretch) {
      bx = static_cast<double>(W) / srcW;
      by = static_cast<double>(H) / srcH;
    }
    const int drawW = std::max(1, static_cast<int>(std::round(srcW * bx * cue.outputScaleX)));
    const int drawH = std::max(1, static_cast<int>(std::round(srcH * by * cue.outputScaleY)));
    const int dx0 = (W - drawW) / 2 + static_cast<int>(cue.outputOffsetX);
    const int dy0 = (H - drawH) / 2 + static_cast<int>(cue.outputOffsetY);
    if (drawW == srcW && drawH == srcH && cropL == 0 && cropT == 0 && dx0 == 0 && dy0 == 0 && sw == W && sh == H) {
      canvas = src;   // the common case: the picture already is the output
      return;
    }
    const int y0 = std::max(0, dy0), y1 = std::min(H, dy0 + drawH);
    const int x0 = std::max(0, dx0), x1 = std::min(W, dx0 + drawW);
    if (y1 <= y0 || x1 <= x0) return;
    deckboy::effects::detail::parallelRows(y1 - y0, x1 - x0, [&](int firstRow, int lastRow) {
      for (int row = firstRow; row < lastRow; ++row) {
        const int dy = y0 + row;
        const double fy = std::clamp((dy - dy0 + 0.5) * srcH / drawH - 0.5 + cropT, static_cast<double>(cropT),
                                     static_cast<double>(cropT + srcH - 1));
        const int ya = static_cast<int>(fy), yb = std::min(ya + 1, cropT + srcH - 1);
        const double wy = fy - ya;
        for (int dx = x0; dx < x1; ++dx) {
          const double fx = std::clamp((dx - dx0 + 0.5) * srcW / drawW - 0.5 + cropL, static_cast<double>(cropL),
                                       static_cast<double>(cropL + srcW - 1));
          const int xa = static_cast<int>(fx), xb = std::min(xa + 1, cropL + srcW - 1);
          const double wx = fx - xa;
          std::uint8_t* out = &canvas[(static_cast<std::size_t>(dy) * W + dx) * 4];
          for (int c = 0; c < 4; ++c) {
            const double top = src[(static_cast<std::size_t>(ya) * sw + xa) * 4 + c] * (1.0 - wx) +
                               src[(static_cast<std::size_t>(ya) * sw + xb) * 4 + c] * wx;
            const double bottom = src[(static_cast<std::size_t>(yb) * sw + xa) * 4 + c] * (1.0 - wx) +
                                  src[(static_cast<std::size_t>(yb) * sw + xb) * 4 + c] * wx;
            out[c] = static_cast<std::uint8_t>(std::lround(top * (1.0 - wy) + bottom * wy));
          }
        }
      }
    });
  }

  // ---- The worker -----------------------------------------------------------
  // Runs on its own thread and reads nothing of the App: the plan is a copy.
  static std::string runRenderFileJob(const RenderFilePlan& plan, RenderFileJob& job) {
    const int W = plan.width, H = plan.height;
    const std::size_t frameBytes = static_cast<std::size_t>(W) * H * 4;
    const bool png = plan.codec == RenderCodec::Png;
    std::error_code ec;
    fs::path out = fs::u8path(plan.path);
    fs::create_directories(png ? out : out.parent_path(), ec);
    char rate[32];
    std::snprintf(rate, sizeof rate, "%d", plan.fps);
    std::vector<std::string> args = {"ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgba", "-s",
                                     std::to_string(W) + "x" + std::to_string(H), "-framerate", rate, "-i", "-"};
    // Bit-exact flags and no metadata: the same frames make the same BYTES.
    if (plan.codec == RenderCodec::ProRes4444) {
      for (const char* a : {"-c:v", "prores_ks", "-profile:v", "4444", "-pix_fmt", "yuva444p10le", "-alpha_bits", "16",
                            "-fflags", "+bitexact", "-flags:v", "+bitexact", "-map_metadata", "-1"})
        args.push_back(a);
      args.push_back(plan.path);
    } else if (plan.codec == RenderCodec::H264) {
      for (const char* a : {"-c:v", "libx264", "-preset", "medium", "-crf", "14", "-pix_fmt", "yuv420p", "-fflags",
                            "+bitexact", "-flags:v", "+bitexact", "-map_metadata", "-1", "-movflags", "+faststart"})
        args.push_back(a);
      args.push_back(plan.path);
    } else {
      for (const char* a : {"-c:v", "png", "-pix_fmt", "rgba", "-start_number", "1"}) args.push_back(a);
      args.push_back((out / (out.filename().string() + "_%06d.png")).string());
    }
    ChildProcess encoder;
    if (!spawnProcess(encoder, args, SpawnOptions::pipedStdin()) || encoder.writeFd < 0) {
      return "failed: could not start ffmpeg";
    }
    std::string error;
    bool ok = true;
    long long globalFrame = 0;
    int segmentNumber = 0;
    static std::atomic<int> jobSerial {0};
    const int serial = ++jobSerial;
    std::vector<std::uint8_t> src, canvas, still, held;
    DecodedFrame patternFrame;
    for (const RenderFileSegment& segment : plan.segments) {
      if (!ok || job.cancel.load()) break;
      ++segmentNumber;
      Cue cue = segment.cue;
      // A key of its own: the generators keep per-cue state by id, and a render
      // must neither start from the live deck's state nor leave any in it.
      cue.id = "render-" + std::to_string(serial) + "-" + std::to_string(segmentNumber);
      ChildProcess decoder;
      int sw = cue.width, sh = cue.height;
      const bool isVideo = cue.kind == CueKind::Video, isImage = cue.kind == CueKind::Image;
      if (isVideo || isImage) {
        std::vector<std::string> dec = {"ffmpeg", "-v", "error"};
        if (isVideo && cue.inPointSeconds > 0.0) {
          char at[32];
          std::snprintf(at, sizeof at, "%.6f", cue.inPointSeconds);
          dec.push_back("-ss");
          dec.push_back(at);
        }
        dec.push_back("-i");
        dec.push_back(cue.path);
        std::string filter;
        if (isVideo) {
          const double speed = std::clamp(cue.playbackSpeed, 0.1, 4.0);
          if (std::fabs(speed - 1.0) > 1e-6) {
            char s[48];
            std::snprintf(s, sizeof s, "setpts=PTS/%.6f,", speed);
            filter += s;
          }
          filter += "fps=" + std::to_string(plan.fps) + ",";
        }
        filter += "format=rgba";
        for (const char* a : {"-an", "-sn", "-vf"}) dec.push_back(a);
        dec.push_back(filter);
        dec.push_back("-frames:v");
        dec.push_back(std::to_string(isVideo ? segment.frames : 1));
        for (const char* a : {"-f", "rawvideo", "-pix_fmt", "rgba", "-"}) dec.push_back(a);
        if (!spawnProcess(decoder, dec, SpawnOptions::pipedStdout()) || decoder.readFd < 0) {
          ok = false;
          error = "could not start ffmpeg to read " + cue.name;
          break;
        }
        if (isImage) {
          still.resize(static_cast<std::size_t>(sw) * sh * 4);
          if (!readExact(decoder.readFd, still.data(), still.size())) {
            ok = false;
            error = "could not read the picture " + cue.name;
            break;
          }
          decoder.stop();
        }
      }
      std::vector<std::vector<std::uint8_t>> effectState(std::max<std::size_t>(1, cue.effects.size()));
      const bool gradeOn = cueHasPixelEffects(cue);
      const bool effectsOn = deckboy::effects::cueEffectStackActive(cue.effects);
      held.clear();
      for (int i = 0; i < segment.frames && ok; ++i) {
        if (job.cancel.load()) break;
        const double tCue = static_cast<double>(i) / plan.fps;
        const double tShow = static_cast<double>(globalFrame) / plan.fps;
        // The show's clock for this frame, for everything on this thread.
        deckboy::effects::LfoClockSample clock {tShow, tShow * plan.bpm / 60.0, 0.0};
        deckboy::effects::lfoClockThreadOverride() = &clock;
        if (cue.kind == CueKind::Pattern) {
          MediaEngine::buildPatternFrameInto(patternFrame, cue, tCue, W, H);
          if (patternFrame.pixels.empty()) {
            ok = false;
            error = "the source made no picture";
            break;
          }
          src = patternFrame.pixels;
          sw = patternFrame.width;
          sh = patternFrame.height;
        } else if (isImage) {
          src = still;
        } else {
          src.resize(static_cast<std::size_t>(sw) * sh * 4);
          if (readExact(decoder.readFd, src.data(), src.size())) {
            held = src;
          } else if (!held.empty()) {
            src = held;   // the file ran a frame short: hold its last
          } else {
            ok = false;
            error = "ffmpeg gave no picture for " + cue.name;
            break;
          }
        }
        if (gradeOn) applyCueVisualEffectsToPixels(src, cue);
        if (effectsOn) {
          deckboy::effects::CueEffectContext ctx;
          ctx.width = sw;
          ctx.height = sh;
          ctx.frameIndex = static_cast<std::uint64_t>(i);
          ctx.seconds = tShow;
          ctx.effectState = &effectState;
          std::vector<deckboy::effects::CueEffect> modulated;
          const bool moving = deckboy::effects::modulateCueEffectStack(cue.effects, clock.seconds, clock.beats, modulated, 0.0);
          deckboy::effects::applyCueEffectStack(src, moving ? modulated : cue.effects, ctx);
        }
        renderPlaceInCanvas(cue, src, sw, sh, canvas, W, H);
        // The cue's own fades, on the alpha: there is nothing underneath to fade to.
        double gain = 1.0;
        const double length = static_cast<double>(segment.frames) / plan.fps;
        if (cue.fadeInSeconds > 0.0 && tCue < cue.fadeInSeconds) gain = tCue / cue.fadeInSeconds;
        if (cue.fadeOutSeconds > 0.0 && length - tCue < cue.fadeOutSeconds) {
          gain = std::min(gain, std::max(0.0, (length - tCue) / cue.fadeOutSeconds));
        }
        const bool opaqueOut = plan.codec == RenderCodec::H264;
        if (gain < 0.9999 || opaqueOut) {
          for (std::size_t p = 0; p + 3 < canvas.size(); p += 4) {
            unsigned a = static_cast<unsigned>(std::lround(canvas[p + 3] * gain));
            if (opaqueOut) {   // no alpha in the file: lay the picture over black
              for (int c = 0; c < 3; ++c) canvas[p + c] = static_cast<std::uint8_t>(canvas[p + c] * a / 255u);
              a = 255;
            }
            canvas[p + 3] = static_cast<std::uint8_t>(a);
          }
        }
        if (!writeAllToFd(encoder.writeFd, canvas.data(), frameBytes)) {
          ok = false;
          error = png ? "ffmpeg stopped taking frames" : "ffmpeg stopped taking frames (is the codec in this ffmpeg?)";
          break;
        }
        ++globalFrame;
        job.done.store(globalFrame);
      }
      deckboy::effects::lfoClockThreadOverride() = nullptr;
      decoder.stop();
    }
    deckboy::effects::lfoClockThreadOverride() = nullptr;
    if (job.cancel.load()) {
      encoder.stop();
      std::error_code gone;
      if (png) fs::remove_all(out, gone); else fs::remove(out, gone);
      return "cancelled";
    }
    encoder.finish(ok ? 600000 : 2000);
    if (ok) {
      std::error_code exists;
      if (!png) ok = fs::exists(out, exists) && fs::file_size(out, exists) > 0;
      else ok = fs::exists(out / (out.filename().string() + "_000001.png"), exists);
      if (!ok) error = "ffmpeg made no file";
    }
    return ok ? "saved " + plan.path : "failed: " + error;
  }

  bool renderFileRunning() const { return renderJob_ && !renderJob_->finished.load(); }

  bool startRenderFile(const RenderFilePlan& plan, std::string& why) {
    if (renderFileRunning()) {
      why = "a render is already running";
      return false;
    }
    auto job = std::make_shared<RenderFileJob>();
    job->total = plan.totalFrames;
    job->startedMs = SDL_GetTicks();
    job->path = plan.path;
    renderJob_ = job;
    auto shared = std::make_shared<RenderFilePlan>(plan);
    App* app = this;
    std::thread([app, shared, job]() {
      const std::string result = runRenderFileJob(*shared, *job);
      job->ok.store(result.rfind("saved", 0) == 0);
      {
        std::lock_guard<std::mutex> lock(job->mutex);
        job->result = result;
      }
      job->finished.store(true);
      std::lock_guard<std::mutex> lock(app->sdlDialogMutex_);
      app->sdlDialogActions_.emplace_back([app, job, result]() {
        app->renderLastResult_ = result;
        if (result == "cancelled") {
          app->triggerToast("render cancelled");
        } else if (job->ok.load()) {
          app->triggerToast("render saved: " + fs::u8path(job->path).filename().string());
        } else {
          app->triggerToast("render " + result, ToastKind::Warning, kToastReadableMs);
        }
      });
    }).detach();
    return true;
  }

  // ---- The dialog -----------------------------------------------------------
  void openRenderDialog() {
    renderDlg_.open = true;
    renderDlg_.path.clear();
    if (const Deck* deck = project_.focusedDeckIndex >= 0 &&
                           project_.focusedDeckIndex < static_cast<int>(project_.decks.size())
                             ? &project_.decks[static_cast<std::size_t>(project_.focusedDeckIndex)] : nullptr) {
      const int n = static_cast<int>(deck->cues.size());
      renderDlg_.from = std::clamp(deck->selectedIndex + 1, 1, std::max(1, n));
      renderDlg_.to = std::clamp(std::max(renderDlg_.from, n), 1, std::max(1, n));
    }
  }

  bool renderDialogPlan(RenderFilePlan& plan, std::string& why) {
    return planRenderFile(renderDlg_.what, renderDlg_.from, renderDlg_.to, renderDlg_.size, kRenderFps[renderDlg_.fps],
                          renderDlg_.codec, renderDlg_.path, plan, why);
  }

  void renderRenderDialog() {
    if (!renderDlg_.open) return;
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, pal.deep.r, pal.deep.g, pal.deep.b, 180);
    SDL_Rect overlay {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &overlay);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    const int pad = uiScaled(24);
    const int dialogW = std::min(width - uiScaled(32), uiScaled(640));
    const int rowH = std::max(uiScaled(30), textLineHeight(fontSmall_) + uiScaled(12));
    const int gap = uiScaled(8);
    const int lineH = textLineHeight(fontSmall_) + uiScaled(4);
    const int titleH = textLineHeight(fontLarge_) + uiScaled(6);
    const int labelW = uiScaled(170);
    const bool running = renderFileRunning();
    const bool range = renderDlg_.what == 1;
    const int rows = 4 + (range ? 1 : 0);
    const int dialogH = pad + titleH + gap + rows * (rowH + gap) + rowH + gap + lineH * 2 + gap +
                        (renderJob_ ? lineH * 3 + gap : 0) + rowH + pad;
    SDL_Rect dialog {(width - dialogW) / 2, std::max(uiScaled(8), (height - dialogH) / 2), dialogW, dialogH};
    renderDlg_.panel = dialog;
    Primitives::drawFramedPanel(controlRenderer_, dialog, pal.light, pal.deep, pal.mid);
    const int innerX = dialog.x + pad, innerW = dialog.w - pad * 2;
    int y = dialog.y + pad;
    drawTextSafe(controlRenderer_, fontLarge_, SDL_Rect {innerX, y, innerW, titleH}, "RENDER TO FILE", pal.deep);
    y += titleH + gap;

    RenderFilePlan plan;
    std::string why;
    const bool planned = renderDialogPlan(plan, why);

    auto cycle = [&](SDL_Rect& rect, const char* label, const std::string& value) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {innerX, y, labelW, rowH}, label, pal.deep);
      rect = SDL_Rect {innerX + labelW, y, innerW - labelW, rowH};
      drawUIPanel(rect, pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, rect, value, pal.deep);
      y += rowH + gap;
    };
    cycle(renderDlg_.whatBtn, "What",
          renderDlg_.what == 0 ? "this cue"
          : renderDlg_.what == 1 ? "from cue N to M" : "an output's whole show");
    if (range) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {innerX, y, labelW, rowH}, "From cue and to cue", pal.deep);
      const int btn = rowH, mid = (innerW - labelW - btn * 4 - gap * 4) / 2;
      int x = innerX + labelW;
      auto step = [&](SDL_Rect& rect, const char* mark) {
        rect = SDL_Rect {x, y, btn, rowH};
        drawUIPanel(rect, pal.mid, pal.deep, pal.light);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, rect, mark, pal.deep);
        x += btn + gap;
      };
      auto number = [&](int value) {
        drawCenteredTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x, y, mid, rowH}, std::to_string(value), pal.deep);
        x += mid;
      };
      step(renderDlg_.fromMinus, "-");
      number(renderDlg_.from);
      step(renderDlg_.fromPlus, "+");
      step(renderDlg_.toMinus, "-");
      number(renderDlg_.to);
      step(renderDlg_.toPlus, "+");
      y += rowH + gap;
    }
    const std::string outputSize = [&] {
      const int idx = project_.focusedOutputIndex;
      const auto s = idx >= 0 && idx < static_cast<int>(project_.outputs.size()) ? outputRequestedRenderSize(idx)
                                                                                : fixedOutputRenderSize();
      return std::to_string(s.first) + "x" + std::to_string(s.second);
    }();
    cycle(renderDlg_.sizeBtn, "Size",
          renderDlg_.size == 0 ? "1080p" : renderDlg_.size == 1 ? "4K" : deckboy::core::i18n::format("the output's ({1})", {outputSize}));
    cycle(renderDlg_.fpsBtn, "Frames a second", std::to_string(kRenderFps[renderDlg_.fps]));
    cycle(renderDlg_.codecBtn, "File", renderCodecLabel(renderDlg_.codec));

    // Where it goes.
    {
      const int chooseW = uiScaled(120);
      SDL_Rect pathRect {innerX, y, innerW - chooseW - gap, rowH};
      const std::string shown = planned ? plan.path : (renderDlg_.path.empty() ? std::string("a name is made from the cue") : renderDlg_.path);
      drawTextSafe(controlRenderer_, fontSmall_, pathRect, ellipsizeToPixelWidth(fontSmall_, shown, pathRect.w), pal.inkSoft);
      renderDlg_.choose = SDL_Rect {pathRect.x + pathRect.w + gap, y, chooseW, rowH};
      drawUIPanel(renderDlg_.choose, pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, renderDlg_.choose, "CHOOSE...", pal.deep);
      y += rowH + gap;
    }
    // What it will make, or why it cannot.
    {
      std::string line1, line2;
      if (planned) {
        const double seconds = static_cast<double>(plan.totalFrames) / plan.fps;
        line1 = deckboy::core::i18n::format("{1} frames, {2}, {3}x{4}",
                                            {renderCount(plan.totalFrames), renderTimeLeft(seconds).c_str(),
                                             std::to_string(plan.width), std::to_string(plan.height)});
        line2 = "Each frame is drawn when the last is written. It is not a recording.";
      } else {
        line1 = "Not yet: " + why;
        line2 = "Live inputs are refused, because a render steps by frames.";
      }
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {innerX, y, innerW, lineH}, line1, planned ? pal.deep : detail::paletteLiftToContrast(pal.deleteBezel, pal.light, 4.5));
      y += lineH;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {innerX, y, innerW, lineH}, line2, pal.inkSoft);
      y += lineH + gap;
    }
    // Progress.
    if (renderJob_) {
      const long long done = renderJob_->done.load();
      const long long total = std::max<long long>(1, renderJob_->total);
      const bool finished = renderJob_->finished.load();
      const double elapsed = std::max(0.001, (SDL_GetTicks() - renderJob_->startedMs) / 1000.0);
      const double rate = done / elapsed;
      SDL_Rect bar {innerX, y, innerW, lineH};
      Primitives::fillRect(controlRenderer_, bar, pal.deep);
      Primitives::fillRect(controlRenderer_, SDL_Rect {bar.x, bar.y, static_cast<int>(bar.w * done / total), bar.h}, pal.dark);
      Primitives::strokeRect(controlRenderer_, bar, pal.mid);
      y += lineH + gap / 2;
      std::string a, b;
      if (!finished) {
        a = deckboy::core::i18n::format("frame {1} of {2} \xC2\xB7 {3} frames/s",
                                        {renderCount(done), renderCount(total), std::to_string(static_cast<int>(std::lround(rate)))});
        b = rate > 0.01 ? deckboy::core::i18n::format("about {1} left", {renderTimeLeft((total - done) / rate)}) : "starting";
      } else {
        std::lock_guard<std::mutex> lock(renderJob_->mutex);
        a = renderJob_->result;
        b = deckboy::core::i18n::format("{1} frames in {2}", {renderCount(done), renderTimeLeft(elapsed)});
      }
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {innerX, y, innerW, lineH}, ellipsizeToPixelWidth(fontSmall_, a, innerW), pal.deep);
      y += lineH;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {innerX, y, innerW, lineH}, b, pal.inkSoft);
      y += lineH + gap;
    }
    // Buttons.
    const int closeW = uiScaled(120);
    renderDlg_.close = SDL_Rect {dialog.x + dialog.w - pad - closeW, y, closeW, rowH};
    drawUIPanel(renderDlg_.close, pal.mid, pal.deep, pal.light);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, renderDlg_.close, "CLOSE", pal.deep);
    SDL_Rect main {innerX, y, innerW - closeW - gap, rowH};
    renderDlg_.start = SDL_Rect {};
    renderDlg_.cancel = SDL_Rect {};
    if (running) {
      renderDlg_.cancel = main;
      drawUIPanel(main, pal.dark, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, main, "CANCEL RENDER", pal.light);
    } else {
      renderDlg_.start = main;
      drawUIPanel(main, planned ? pal.dark : pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, main, "START RENDER", planned ? pal.light : pal.inkSoft);
    }
  }

  void handleRenderDialogClick(int x, int y, Uint8 button) {
    if (button != SDL_BUTTON_LEFT) return;
    RenderDialogState& d = renderDlg_;
    if (pointInRect(x, y, d.close) || !pointInRect(x, y, d.panel)) {
      d.open = false;
      return;
    }
    const int n = [&] {
      const int idx = project_.focusedDeckIndex;
      return idx >= 0 && idx < static_cast<int>(project_.decks.size()) ? static_cast<int>(project_.decks[static_cast<std::size_t>(idx)].cues.size()) : 1;
    }();
    if (pointInRect(x, y, d.whatBtn)) d.what = (d.what + 1) % 3;
    else if (pointInRect(x, y, d.sizeBtn)) d.size = (d.size + 1) % 3;
    else if (pointInRect(x, y, d.fpsBtn)) d.fps = (d.fps + 1) % 4;
    else if (pointInRect(x, y, d.codecBtn)) d.codec = (d.codec + 1) % 3;
    else if (d.what == 1 && pointInRect(x, y, d.fromMinus)) d.from = std::clamp(d.from - 1, 1, std::max(1, n));
    else if (d.what == 1 && pointInRect(x, y, d.fromPlus)) d.from = std::clamp(d.from + 1, 1, std::max(1, n));
    else if (d.what == 1 && pointInRect(x, y, d.toMinus)) d.to = std::clamp(d.to - 1, 1, std::max(1, n));
    else if (d.what == 1 && pointInRect(x, y, d.toPlus)) d.to = std::clamp(d.to + 1, 1, std::max(1, n));
    else if (pointInRect(x, y, d.choose)) {
      static const std::vector<SDL_DialogFileFilter> kFilters = {
        {"Video", "mov;mp4"}, {"All files", "*"}};
      showSaveFileDialog(kFilters, [this](std::vector<std::string> picked) {
        if (!picked.empty()) renderDlg_.path = picked.front();
      });
    } else if (d.cancel.w > 0 && pointInRect(x, y, d.cancel)) {
      if (renderJob_) renderJob_->cancel.store(true);
    } else if (d.start.w > 0 && pointInRect(x, y, d.start)) {
      RenderFilePlan plan;
      std::string why;
      if (renderDialogPlan(plan, why) && startRenderFile(plan, why)) {
        triggerToast("rendering: " + renderCount(plan.totalFrames) + " frames");
      } else if (!why.empty()) {
        triggerToast("render: " + why, ToastKind::Warning, kToastReadableMs);
      }
    }
  }

  // ---- Remote: RENDER [DIALOG] | START <what> <size> <fps> <codec> <path> | STATUS | CANCEL
  void handleRenderCommand(const std::vector<std::string>& parts) {
    const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : "STATUS";
    if (sub == "DIALOG" || sub == "OPEN") {
      openRenderDialog();
      remoteCommandDetail_ = "dialog open";
      return;
    }
    if (sub == "CLOSE") {
      renderDlg_.open = false;
      return;
    }
    if (sub == "CANCEL") {
      if (!renderFileRunning()) {
        failRemoteCommand("RENDER CANCEL: nothing is rendering");
        return;
      }
      renderJob_->cancel.store(true);
      return;
    }
    if (sub == "STATUS") {
      std::ostringstream out;
      if (!renderJob_) {
        out << "idle";
      } else if (!renderJob_->finished.load()) {
        const double elapsed = std::max(0.001, (SDL_GetTicks() - renderJob_->startedMs) / 1000.0);
        const long long done = renderJob_->done.load();
        out << "running frame " << done << " of " << renderJob_->total << " rate=" << std::fixed << std::setprecision(1)
            << done / elapsed << " file=" << renderJob_->path;
      } else {
        std::lock_guard<std::mutex> lock(renderJob_->mutex);
        out << "done frames=" << renderJob_->done.load() << " of " << renderJob_->total << " result=\"" << renderJob_->result << "\"";
      }
      remoteCommandDetail_ = out.str();
      return;
    }
    if (sub != "START" || parts.size() < 6) {
      failRemoteCommand("RENDER: START <CUE|CUES a-b|OUTPUT> <1080P|4K|OUTPUT> <25|30|50|60> <PRORES|H264|PNG> <file>, STATUS, CANCEL, DIALOG");
      return;
    }
    // what: CUE, CUES a-b (two words), OUTPUT.
    std::size_t at = 2;
    int what = 0, from = 1, to = 1;
    const std::string w = toUpper(parts[at++]);
    if (w == "CUE") {
      what = 0;
    } else if (w == "OUTPUT") {
      what = 2;
    } else if (w == "CUES" && at < parts.size()) {
      what = 1;
      const std::string span = parts[at++];
      const std::size_t dash = span.find('-');
      from = std::atoi(span.substr(0, dash).c_str());
      to = dash == std::string::npos ? from : std::atoi(span.substr(dash + 1).c_str());
      if (from < 1 || to < 1) {
        failRemoteCommand("RENDER START CUES: first-last, numbered from 1");
        return;
      }
    } else {
      failRemoteCommand("RENDER START: CUE, CUES a-b or OUTPUT");
      return;
    }
    if (parts.size() < at + 4) {
      failRemoteCommand("RENDER START: needs a size, a rate, a codec and a file");
      return;
    }
    const std::string sizeWord = toUpper(parts[at++]);
    const int size = sizeWord == "1080P" ? 0 : sizeWord == "4K" ? 1 : sizeWord == "OUTPUT" ? 2 : -1;
    const int fps = std::atoi(parts[at++].c_str());
    const std::string codecWord = toUpper(parts[at++]);
    const int codec = codecWord == "PRORES" ? 0 : codecWord == "H264" ? 1 : codecWord == "PNG" ? 2 : -1;
    if (size < 0 || codec < 0) {
      failRemoteCommand("RENDER START: size is 1080P, 4K or OUTPUT; codec is PRORES, H264 or PNG");
      return;
    }
    std::string path;
    for (std::size_t i = at; i < parts.size(); ++i) path += (path.empty() ? "" : " ") + parts[i];
    RenderFilePlan plan;
    std::string why;
    if (!planRenderFile(what, from, to, size, fps, codec, path, plan, why) || !startRenderFile(plan, why)) {
      failRemoteCommand("RENDER START: " + why);
      return;
    }
    remoteCommandDetail_ = std::to_string(plan.totalFrames) + " frames to " + plan.path;
  }
