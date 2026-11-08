  // ── MOTION ──────────────────────────────────────────────────────────────
  //
  // A camera read for how much moves and where (core/motion.hpp), published
  // for the Motion oscillator shapes. James, 2026-11-04 (board DGrGWwFS),
  // after his Max patch: the webcam as a CONTROL, never a picture to cut to.
  //
  // Two sources. "camera:<device>" opens a capture of its own, small, with
  // no sound, that never reaches an output. "deck:<n>" reads the picture
  // playlist n is already playing, so a camera that is also in the show is
  // never opened twice (most webcams and capture inputs allow one reader).

  std::unique_ptr<MediaEngine> motionEngine_;
  std::string motionEngineDevice_;       // what motionEngine_ was opened on
  Cue motionCameraCue_;
  deckboy::motion::Analyzer motionAnalyzer_;
  Uint64 motionLastMs_ = 0;
  std::uint64_t motionLastFrame_ = static_cast<std::uint64_t>(-1);
  const void* motionLastEngine_ = nullptr;
  DecodedFrame motionGpuScratch_;
  std::vector<std::uint8_t> motionGrey_;
  std::string motionState_ = "off";      // what the settings page and MOTION say
  std::uint64_t motionFramesRead_ = 0;   // since Motion last went on, for MOTION and the page
  SDL_Texture* motionTex_ = nullptr;     // the "what moved" picture, for the settings page
  bool motionTexStale_ = false;
  std::vector<std::string> motionCameraList_;   // Windows: dshow names, listed once on demand

  bool motionUsesCamera() const { return project_.motionSource.rfind("deck:", 0) != 0; }

  std::string motionCameraName() const {
    return project_.motionSource.rfind("camera:", 0) == 0 ? project_.motionSource.substr(7) : std::string();
  }

  int motionDeck() const {
    if (motionUsesCamera()) return -1;
    const int d = std::atoi(project_.motionSource.c_str() + 5);
    return d >= 0 && d < static_cast<int>(project_.decks.size()) ? d : -1;
  }

  // The cameras the operator can pick. Windows names its DirectShow devices,
  // which costs an ffmpeg run, so the list is made once and kept; the Mac
  // and Linux backends resolve "default" to the first camera themselves.
  const std::vector<std::string>& motionCameras(bool refresh = false) {
#ifdef _WIN32
    if (refresh || motionCameraList_.empty()) motionCameraList_ = listDshowVideoDevices();
#else
    (void) refresh;
    if (motionCameraList_.empty()) motionCameraList_ = {"default"};
#endif
    return motionCameraList_;
  }

  void stopMotionCamera() {
    if (motionEngine_) {
      motionEngine_->stopAll();
      motionEngine_.reset();
    }
    motionEngineDevice_.clear();
  }

  void publishMotionReadings() {
    for (int r = 0; r < deckboy::motion::kReadings; ++r) {
      const auto& reading = motionAnalyzer_.reading(r);
      deckboy::effects::publishMotionReading(r, reading.amount, reading.x, reading.y);
    }
  }

  // Off: no camera, nothing read, every Motion oscillator back where it was set.
  void motionGoQuiet(const char* state) {
    stopMotionCamera();
    motionAnalyzer_.reset();
    publishMotionReadings();
    motionState_ = state;
    motionFramesRead_ = 0;
    motionLastFrame_ = static_cast<std::uint64_t>(-1);
    motionLastEngine_ = nullptr;
    motionTexStale_ = true;
  }

  // The frame to read this tick, or null. Opens the camera on first use.
  const DecodedFrame* motionFrame() {
    if (!motionUsesCamera()) {
      stopMotionCamera();
      const int deck = motionDeck();
      if (deck < 0) {
        motionState_ = "no such playlist";
        return nullptr;
      }
      const MediaEngine* engine = mediaEngineForDeck(deck);
      motionState_ = "reading " + deckLabel(deck);
      return engine ? engine->currentFrame() : nullptr;
    }
    std::string device = motionCameraName();
#ifdef _WIN32
    if (device.empty() || toLower(device) == "default") {
      const auto& cameras = motionCameras();
      device = cameras.empty() ? std::string() : cameras.front();
    }
    if (device.empty()) {
      motionState_ = "no camera found";
      return nullptr;
    }
#else
    if (device.empty()) device = "default-camera";
#endif
    if (!motionEngine_ || motionEngineDevice_ != device) {
      stopMotionCamera();
      motionEngine_ = std::make_unique<MediaEngine>(
        controlRenderer_, nullptr, MediaEngine::AudioTapCallback {},
        [this](const Cue& cue) { return resolvedCueFilesystemPathString(cue, currentProjectFile_); });
      motionCameraCue_ = Cue {};
      motionCameraCue_.kind = CueKind::Camera;
      motionCameraCue_.path = "source://" + sourceCueTokenForKind(CueKind::Camera) + "/" + device;
      motionCameraCue_.name = "Motion camera";
      // Small and silent: a reading needs 160 x 90, and a control camera's
      // microphone has no business in the show.
      motionCameraCue_.width = 640;
      motionCameraCue_.height = 360;
      motionCameraCue_.hasAudio = false;
      motionCameraCue_.formatName = "source";
      motionCameraCue_.videoCodec = sourceCueTokenForKind(CueKind::Camera);
      motionEngine_->loadCue(&motionCameraCue_, true);
      motionEngineDevice_ = device;
      motionAnalyzer_.reset();
    }
    motionEngine_->update();
    motionState_ = "camera: " + device;
    return motionEngine_->currentFrame();
  }

  // Once per tick, from update(). Reads at most ~30 times a second and only
  // a frame it has not read before.
  void serviceMotion() {
    if (!project_.motionEnabled) {
      if (motionState_ != "off") motionGoQuiet("off");
      return;
    }
    const Uint64 now = SDL_GetTicks();
    if (now - motionLastMs_ < 33) {
      if (motionEngine_) motionEngine_->update();
      return;
    }
    const DecodedFrame* frame = motionFrame();
    if (!frame || frame->width <= 0 || frame->height <= 0) return;
    const void* engineId = motionUsesCamera() ? static_cast<const void*>(motionEngine_.get())
                                              : static_cast<const void*>(mediaEngineForDeck(motionDeck()));
    if (frame->index == motionLastFrame_ && engineId == motionLastEngine_) return;
    const double dt = motionLastMs_ ? std::min(0.25, (now - motionLastMs_) / 1000.0) : 1.0 / 30.0;
    motionLastMs_ = now;
    motionLastFrame_ = frame->index;
    if (engineId != motionLastEngine_) motionAnalyzer_.reset();   // a new source: start fresh
    motionLastEngine_ = engineId;
#if DECKBOY_INPROC_DECODE
    if (frame->isGpu()) {
      if (!deckboy::libav::downloadGpuFrameNV12(*frame, motionGpuScratch_)) return;
      frame = &motionGpuScratch_;
    }
#endif
    if (frame->isGpu() || frame->pixels.empty()) return;
    const auto& settings = project_.motion;
    switch (frame->format) {
      case FramePixelFormat::RGBA32:
        motionAnalyzer_.feed(frame->pixels.data(), frame->width, frame->height, frame->width * 4,
                             deckboy::motion::PixelLayout::Rgba, dt, settings);
        break;
      case FramePixelFormat::NV12:
        // The Y plane comes first and is exactly a grey picture.
        motionAnalyzer_.feed(frame->pixels.data(), frame->width, frame->height, frame->width,
                             deckboy::motion::PixelLayout::Grey, dt, settings);
        break;
      case FramePixelFormat::P010: {
        // Sixteen-bit samples with the value in the high byte.
        const std::size_t n = static_cast<std::size_t>(frame->width) * static_cast<std::size_t>(frame->height);
        if (frame->pixels.size() < n * 2) return;
        motionGrey_.resize(n);
        for (std::size_t i = 0; i < n; ++i) motionGrey_[i] = frame->pixels[i * 2 + 1];
        motionAnalyzer_.feed(motionGrey_.data(), frame->width, frame->height, frame->width,
                             deckboy::motion::PixelLayout::Grey, dt, settings);
        break;
      }
      default:
        return;
    }
    publishMotionReadings();
    ++motionFramesRead_;
    motionTexStale_ = true;
  }

  // The "what moved" picture as a texture, refreshed when a frame was read.
  SDL_Texture* motionPictureTexture() {
    if (!motionTex_) {
      motionTex_ = deckboyCreateTexture(controlRenderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                        deckboy::motion::kGridW, deckboy::motion::kGridH);
      motionTexStale_ = true;
    }
    if (motionTex_ && motionTexStale_) {
      const auto& moved = motionAnalyzer_.moved();
      std::vector<std::uint8_t> rgba(moved.size() * 4);
      for (std::size_t i = 0; i < moved.size(); ++i) {
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = moved[i];
        rgba[i * 4 + 3] = 255;
      }
      SDL_UpdateTexture(motionTex_, nullptr, rgba.data(), deckboy::motion::kGridW * 4);
      motionTexStale_ = false;
    }
    return motionTex_;
  }

  void shutdownMotion() {
    stopMotionCamera();
    if (motionTex_) {
      SDL_DestroyTexture(motionTex_);
      motionTex_ = nullptr;
    }
  }

  // What MOTION LEVEL answers: each reading as amount@x,y.
  std::string motionLevelsText() const {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    for (int r = 0; r <= motionAnalyzer_.zoneCount() && r < deckboy::motion::kReadings; ++r) {
      const auto& reading = motionAnalyzer_.reading(r);
      out << (r ? " " : "") << (r == 0 ? std::string("whole") : "zone" + std::to_string(r)) << '='
          << reading.amount << '@' << reading.x << ',' << reading.y;
    }
    return out.str();
  }

  // ── MOTION over the wire ────────────────────────────────────────────────
  //
  //   MOTION                         on/off, the source, the state
  //   MOTION ON | OFF
  //   MOTION SOURCE CAMERA [name] | DECK <n>
  //   MOTION CAMERAS                 the cameras this computer can see
  //   MOTION SENSITIVITY <1-90 %> | RISE <s> | FALL <s> | MIRROR ON|OFF
  //   MOTION ZONE ADD <x> <y> <w> <h>   percentages of the picture
  //   MOTION ZONE REMOVE <n> | MOTION ZONE CLEAR | MOTION ZONE
  //   MOTION LEVEL                   whole=amount@x,y zone1=...
  void handleMotionCommand(const std::vector<std::string>& parts) {
    const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();
    auto number = [&](std::size_t at, double lo, double hi, double& out) {
      if (parts.size() <= at) return false;
      try {
        std::size_t used = 0;
        out = std::stod(parts[at], &used);
        return used == parts[at].size() && std::isfinite(out) && out >= lo && out <= hi;
      } catch (...) {
        return false;
      }
    };
    if (sub.empty()) {
      remoteCommandDetail_ = std::string(project_.motionEnabled ? "on" : "off") + " source=" +
                             project_.motionSource + " state=" + motionState_ +
                             " frames=" + std::to_string(motionFramesRead_) +
                             " picture=" + std::to_string(motionPicRect_.x) + "," + std::to_string(motionPicRect_.y) +
                             "," + std::to_string(motionPicRect_.w) + "," + std::to_string(motionPicRect_.h);
      return;
    }
    if (sub == "ON" || sub == "OFF") {
      project_.motionEnabled = sub == "ON";
      markProjectDirty();
      remoteCommandDetail_ = sub == "ON" ? "on" : "off";
      return;
    }
    if (sub == "LEVEL" || sub == "LEVELS") {
      remoteCommandDetail_ = motionLevelsText();
      return;
    }
    if (sub == "CAMERAS") {
      std::string list;
      for (const auto& name : motionCameras(true)) list += (list.empty() ? "" : " | ") + name;
      remoteCommandDetail_ = list.empty() ? "no cameras found" : list;
      return;
    }
    if (sub == "SOURCE") {
      const std::string kind = parts.size() > 2 ? toUpper(parts[2]) : std::string();
      if (kind == "CAMERA") {
        std::string name;
        for (std::size_t i = 3; i < parts.size(); ++i) name += (i > 3 ? " " : "") + parts[i];
        project_.motionSource = "camera:" + name;
      } else if (kind == "DECK" || kind == "PLAYLIST") {
        double d = 0;
        if (!number(3, 1, static_cast<double>(project_.decks.size()), d) || d != std::floor(d)) {
          failRemoteCommand("MOTION SOURCE DECK: a playlist number, 1-" + std::to_string(project_.decks.size()));
          return;
        }
        project_.motionSource = "deck:" + std::to_string(static_cast<int>(d) - 1);
      } else {
        failRemoteCommand("MOTION SOURCE: CAMERA [name] or DECK <n>");
        return;
      }
      markProjectDirty();
      remoteCommandDetail_ = project_.motionSource;
      return;
    }
    auto& m = project_.motion;
    if (sub == "SENSITIVITY" || sub == "RISE" || sub == "FALL") {
      double v = 0;
      const double hi = sub == "SENSITIVITY" ? 90.0 : sub == "RISE" ? 5.0 : 10.0;
      const double lo = sub == "SENSITIVITY" ? 1.0 : 0.0;
      if (!number(2, lo, hi, v)) {
        failRemoteCommand("MOTION " + sub + ": " + (sub == "SENSITIVITY" ? std::string("1-90 (percent)")
                                                                          : "seconds, 0-" + std::to_string(static_cast<int>(hi))));
        return;
      }
      if (sub == "SENSITIVITY") m.sensitivity = static_cast<float>(v / 100.0);
      else if (sub == "RISE") m.riseSeconds = static_cast<float>(v);
      else m.fallSeconds = static_cast<float>(v);
      markProjectDirty();
      remoteCommandDetail_ = sub + " " + parts[2];
      return;
    }
    if (sub == "MIRROR") {
      const std::string state = parts.size() > 2 ? toUpper(parts[2]) : std::string();
      if (state != "ON" && state != "OFF") {
        failRemoteCommand("MOTION MIRROR: ON or OFF");
        return;
      }
      m.mirror = state == "ON";
      markProjectDirty();
      remoteCommandDetail_ = "mirror " + state;
      return;
    }
    if (sub == "ZONE" || sub == "ZONES") {
      const std::string op = parts.size() > 2 ? toUpper(parts[2]) : std::string();
      if (op.empty()) {
        remoteCommandDetail_ = m.zones.empty() ? std::string("no zones") : deckboy::motion::zonesToText(m.zones);
        return;
      }
      if (op == "CLEAR") {
        m.zones.clear();
      } else if (op == "ADD") {
        double v[4];
        for (int i = 0; i < 4; ++i) {
          if (!number(3 + static_cast<std::size_t>(i), 0, 100, v[i])) {
            failRemoteCommand("MOTION ZONE ADD: x y w h, percentages 0-100");
            return;
          }
        }
        if (static_cast<int>(m.zones.size()) >= deckboy::motion::kMaxZones) {
          failRemoteCommand("MOTION ZONE ADD: already 8 zones");
          return;
        }
        if (v[2] < 1 || v[3] < 1 || v[0] + v[2] > 100 || v[1] + v[3] > 100) {
          failRemoteCommand("MOTION ZONE ADD: the zone must fit inside the picture");
          return;
        }
        m.zones.push_back({static_cast<float>(v[0] / 100), static_cast<float>(v[1] / 100),
                           static_cast<float>(v[2] / 100), static_cast<float>(v[3] / 100)});
      } else if (op == "REMOVE") {
        double n = 0;
        if (!number(3, 1, static_cast<double>(m.zones.size()), n) || n != std::floor(n)) {
          failRemoteCommand("MOTION ZONE REMOVE: a zone number");
          return;
        }
        m.zones.erase(m.zones.begin() + static_cast<std::ptrdiff_t>(n) - 1);
      } else {
        failRemoteCommand("MOTION ZONE: ADD x y w h, REMOVE n or CLEAR");
        return;
      }
      m = deckboy::motion::clampSettings(m);
      markProjectDirty();
      remoteCommandDetail_ = "zones: " + std::to_string(m.zones.size());
      return;
    }
    failRemoteCommand("MOTION: ON, OFF, SOURCE, CAMERAS, SENSITIVITY, RISE, FALL, MIRROR, ZONE or LEVEL");
  }

  // ── THE SETTINGS CARD (Audio tab, under LIVE CAPTIONS) ──────────────────

  SDL_Rect motionPicRect_ {};
  SDL_Rect motionLampRect_ {};   // the monitor header's camera-in-use chip
  struct MotionZoneDrag {
    bool active = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  } motionZoneDrag_;

  std::string motionSourceLabel() const {
    if (!motionUsesCamera()) {
      const int deck = motionDeck();
      return deck >= 0 ? deckboy::core::i18n::format(
                           "the picture {1} plays", {deckboy::core::i18n::translate(deckLabel(deck))})
                       : std::string("a playlist that is gone");
    }
    const std::string name = motionCameraName();
    return "camera: " + (name.empty() ? std::string("the first one found") : name);
  }

  std::string motionHint() const {
    if (!project_.motionEnabled) return "off: no camera is open. Drag on the picture to draw a zone.";
    return motionState_ + ". Drag to draw a zone, click one to remove it.";
  }

  // What moved, white on black, with the zones over it and a ring where each
  // reading's movement is centred.
  void drawMotionPicture(const SDL_Rect& pic) {
    Primitives::fillRect(controlRenderer_, pic, SDL_Color {0, 0, 0, 255});
    if (project_.motionEnabled) {
      if (SDL_Texture* tex = motionPictureTexture()) {
        const SDL_FRect dst {static_cast<float>(pic.x), static_cast<float>(pic.y),
                             static_cast<float>(pic.w), static_cast<float>(pic.h)};
        SDL_RenderTexture(controlRenderer_, tex, nullptr, &dst);
      }
    }
    auto toPic = [&](float fx, float fy) {
      return SDL_Point {pic.x + static_cast<int>(std::lround(fx * pic.w)),
                        pic.y + static_cast<int>(std::lround(fy * pic.h))};
    };
    auto outline = [&](const SDL_Rect& r, SDL_Color c) {
      Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y, r.w, 2}, c);
      Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y + r.h - 2, r.w, 2}, c);
      Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y, 2, r.h}, c);
      Primitives::fillRect(controlRenderer_, SDL_Rect {r.x + r.w - 2, r.y, 2, r.h}, c);
    };
    const auto& zones = project_.motion.zones;
    for (std::size_t z = 0; z < zones.size(); ++z) {
      const SDL_Point a = toPic(zones[z].x, zones[z].y);
      const SDL_Point b = toPic(zones[z].x + zones[z].w, zones[z].y + zones[z].h);
      const SDL_Rect r {a.x, a.y, std::max(4, b.x - a.x), std::max(4, b.y - a.y)};
      outline(r, pal.light);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {r.x + 4, r.y + 2, r.w - 8, textLineHeight(fontSmall_)},
                   "zone " + std::to_string(z + 1), pal.light);
    }
    if (project_.motionEnabled) {
      for (int r = 0; r <= motionAnalyzer_.zoneCount() && r <= static_cast<int>(zones.size()); ++r) {
        const auto& reading = motionAnalyzer_.reading(r);
        if (reading.amount < 0.03f) continue;
        float fx = reading.x, fy = reading.y;
        if (r > 0) {
          const auto& zone = zones[static_cast<std::size_t>(r - 1)];
          fx = zone.x + fx * zone.w;
          fy = zone.y + fy * zone.h;
        }
        const SDL_Point c = toPic(fx, fy);
        // White for the whole picture, amber for a zone: both read on black.
        const int ring = std::max(6, uiScaled(9));
        const SDL_Color ink = r == 0 ? SDL_Color {255, 255, 255, 255} : SDL_Color {242, 179, 61, 255};
        outline(SDL_Rect {c.x - ring, c.y - ring, ring * 2, ring * 2}, ink);
        outline(SDL_Rect {c.x - ring + 2, c.y - ring + 2, ring * 2 - 4, ring * 2 - 4}, ink);
      }
    }
    if (motionZoneDrag_.active) {
      const SDL_Rect r {std::min(motionZoneDrag_.x0, motionZoneDrag_.x1),
                        std::min(motionZoneDrag_.y0, motionZoneDrag_.y1),
                        std::abs(motionZoneDrag_.x1 - motionZoneDrag_.x0),
                        std::abs(motionZoneDrag_.y1 - motionZoneDrag_.y0)};
      outline(r, SDL_Color {242, 179, 61, 255});
    }
  }

  // One bar per reading: the whole picture, then each zone.
  void drawMotionLevels(const SDL_Rect& row) {
    const int n = std::min(motionAnalyzer_.zoneCount(), static_cast<int>(project_.motion.zones.size())) + 1;
    const int gap = uiScaled(3);
    const int w = std::max(1, (row.w - gap * (n - 1)) / n);
    for (int r = 0; r < n; ++r) {
      const SDL_Rect cell {row.x + r * (w + gap), row.y, w, row.h};
      Primitives::fillRect(controlRenderer_, cell, pal.tile);
      const float amount = project_.motionEnabled ? motionAnalyzer_.reading(r).amount : 0.0f;
      const int lit = static_cast<int>(std::lround(cell.w * std::clamp(amount, 0.0f, 1.0f)));
      // The ink on a tile, as a fill: the one pairing every theme keeps apart.
      if (lit > 0) Primitives::fillRect(controlRenderer_, SDL_Rect {cell.x, cell.y, lit, cell.h}, pal.fg);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, cell, r == 0 ? std::string("all") : std::to_string(r),
                           pal.fgSoft);
    }
  }

  // A press on the picture starts a zone. Only on the Audio tab, where the
  // picture is drawn.
  bool beginMotionZoneDrag(int x, int y) {
    if (settingsTab_ != 1 || motionPicRect_.w <= 0 || !pointInRect(x, y, motionPicRect_)) return false;
    motionZoneDrag_ = MotionZoneDrag {true, x, y, x, y};
    return true;
  }

  // Let go: a drag draws a zone; a click on a zone removes it.
  void endMotionZoneDrag() {
    if (!motionZoneDrag_.active) return;
    motionZoneDrag_.active = false;
    const SDL_Rect pic = motionPicRect_;
    if (pic.w <= 0 || pic.h <= 0) return;
    auto fx = [&](int x) { return std::clamp(static_cast<float>(x - pic.x) / pic.w, 0.0f, 1.0f); };
    auto fy = [&](int y) { return std::clamp(static_cast<float>(y - pic.y) / pic.h, 0.0f, 1.0f); };
    auto& zones = project_.motion.zones;
    const bool dragged = std::abs(motionZoneDrag_.x1 - motionZoneDrag_.x0) > uiScaled(6) &&
                         std::abs(motionZoneDrag_.y1 - motionZoneDrag_.y0) > uiScaled(6);
    if (!dragged) {
      const float px = fx(motionZoneDrag_.x0);
      const float py = fy(motionZoneDrag_.y0);
      for (int z = static_cast<int>(zones.size()) - 1; z >= 0; --z) {
        const auto& zone = zones[static_cast<std::size_t>(z)];
        if (px >= zone.x && px <= zone.x + zone.w && py >= zone.y && py <= zone.y + zone.h) {
          pushUndoSnapshot();
          zones.erase(zones.begin() + z);
          markProjectDirty();
          triggerToast("zone " + std::to_string(z + 1) + " removed");
          return;
        }
      }
      return;
    }
    if (static_cast<int>(zones.size()) >= deckboy::motion::kMaxZones) {
      triggerToast("8 zones already: click one to remove it first", ToastKind::Warning, kToastReadableMs);
      return;
    }
    const float x0 = fx(std::min(motionZoneDrag_.x0, motionZoneDrag_.x1));
    const float y0 = fy(std::min(motionZoneDrag_.y0, motionZoneDrag_.y1));
    const float x1 = fx(std::max(motionZoneDrag_.x0, motionZoneDrag_.x1));
    const float y1 = fy(std::max(motionZoneDrag_.y0, motionZoneDrag_.y1));
    pushUndoSnapshot();
    zones.push_back({x0, y0, x1 - x0, y1 - y0});
    project_.motion = deckboy::motion::clampSettings(project_.motion);
    markProjectDirty();
    triggerToast("zone " + std::to_string(zones.size()) + " drawn: pick it on a Motion ~ (the middle cell)");
  }

  // The card's buttons. True when the action was one of them.
  bool handleMotionSettingsAction(int action, const SDL_Rect& rect) {
    auto& m = project_.motion;
    switch (action) {
      case kSettingsActionMotionToggle:
        project_.motionEnabled = !project_.motionEnabled;
        triggerToast(project_.motionEnabled ? "motion on: the camera is read, never recorded"
                                            : "motion off: the camera is closed");
        break;
      case kSettingsActionMotionSource: {
        std::vector<std::pair<std::string, std::string>> choices;
        for (const auto& name : motionCameras(true)) choices.emplace_back("camera:" + name, "camera: " + name);
        for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
          choices.emplace_back("deck:" + std::to_string(d),
                               deckboy::core::i18n::format(
                                 "the picture {1} plays", {deckboy::core::i18n::translate(deckLabel(d))}));
        }
        openDropdown("motion.source", rect, choices, project_.motionSource, [this](const std::string& value) {
          project_.motionSource = value;
          markProjectDirty();
        });
        return true;
      }
      case kSettingsActionMotionSensDec: m.sensitivity -= 0.01f; break;
      case kSettingsActionMotionSensInc: m.sensitivity += 0.01f; break;
      case kSettingsActionMotionRiseDec: m.riseSeconds -= 0.05f; break;
      case kSettingsActionMotionRiseInc: m.riseSeconds += 0.05f; break;
      case kSettingsActionMotionFallDec: m.fallSeconds -= 0.1f; break;
      case kSettingsActionMotionFallInc: m.fallSeconds += 0.1f; break;
      case kSettingsActionMotionMirror: m.mirror = !m.mirror; break;
      case kSettingsActionMotionZonesClear:
        if (m.zones.empty()) return true;
        pushUndoSnapshot();
        m.zones.clear();
        triggerToast("zones cleared: Motion reads the whole picture");
        break;
      default:
        return false;
    }
    m = deckboy::motion::clampSettings(m);
    markProjectDirty();
    return true;
  }
