// ============================================================================
// app_render_main.ipp — Main control window rendering.
//
// Renders the primary Deckboy control window UI, the largest rendering file
// (~2600 lines). Organized into nested panel areas:
//
//   renderMainPanel()           — top-level split: program area + inspector
//   renderDeckPanel()           — cue list with scrollable rows
//   renderDeckPanelCueRow()     — individual cue row (color chip, name, status)
//   renderDeckTransportPanel()  — timeline scrubber, play/stop/seek controls
//   renderDeckInfoBar()         — deck name, BPM, time display
//   renderMonitorsPanel()       — output preview tiles in monitors window
//   renderControlWindow()       — entry point: composes all panels
//
// Layout uses VerticalLayout/HorizontalLayout from render/layout.hpp.
// Drawing uses Primitives and TextRenderer from render/*.hpp.
// All button hit-rects are stored in member vectors for input dispatch.
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // A hovering "terminal face friend" (BMO-style) drawn straight onto the dark
  // program monitor — the monitor itself is the screen. Animates continuously
  // and smoothly (no state snaps), Balatro-style: a gentle overall hover + a
  // small whole-face tilt/rock, and on top of that EACH element (each eye, the
  // mouth) drifts and breathes on its own phase, so they float semi-
  // independently. Plus eased squish-blinks and orbiting twinkle-stars.
  // Glowing + theme-tinted; up until the first clip loads this session.
  void drawStartupMascot(const SDL_Rect& area, Uint64 nowMs) {
    static const char* kTips[] = {
      "hi! i'm deckboy :)",
      "press I to import a clip",
      "Enter takes a cue live",
      "S stops the live cue",
      "the timeline can be resized",
      "try a terminal theme in Settings (P)",
      "RELINK finds media that moved",
      "cues have their own gain & fades",
      "trim clips with I and O",
      "Ctrl+/ shows all shortcuts",
    };
    const int tipCount = static_cast<int>(sizeof(kTips) / sizeof(kTips[0]));

    if (area.w < 150 || area.h < 120) {
      drawCenteredTextSafe(controlRenderer_, fontSmall_, area,
                           "hi! press I to import a clip", pal.fg);
      return;
    }

    const double kPi = 3.14159265358979;
    double t = static_cast<double>(nowMs) / 1000.0;
    int unit = std::clamp(std::min(area.w, area.h) / 9, 14, 40);
    const SDL_Color glow = pal.light;
    const int thick = std::max(3, unit / 4);

    // Face centre — a gentle overall hover.
    int cx = area.x + area.w / 2 + static_cast<int>(std::lround(std::sin(t * 0.8) * 4.0));
    int cy = area.y + area.h / 2 - area.h / 9
             + static_cast<int>(std::lround(std::sin(t * 1.3) * 3.5 + std::sin(t * 0.55 + 1.0) * 2.0));

    // Balatro-style whole-face wobble: a small oscillating tilt so the face
    // gently rocks. Every element position is placed through this rotation
    // about the centre, so the eyes swing one way as the mouth swings the
    // other; each element then ALSO floats on its own phase below.
    double theta = std::sin(t * 0.9) * 0.06 + std::sin(t * 1.7 + 0.5) * 0.02;
    double ca = std::cos(theta), sa = std::sin(theta);
    auto place = [&](double ox, double oy, int& X, int& Y) {
      X = cx + static_cast<int>(std::lround(ox * ca - oy * sa));
      Y = cy + static_cast<int>(std::lround(ox * sa + oy * ca));
    };

    // Eased blink (shared by both eyes) — squish shut and reopen, no snap.
    double bp = std::fmod(t, 4.0);
    double blink = 0.0;
    if (bp < 0.22) {
      double u = bp / 0.22;
      double s = std::sin(u * kPi);
      blink = s * s * (3.0 - 2.0 * s);
    }
    double eyeOpen = 1.0 - 0.90 * blink;

    // The smile curvature breathes smoothly between gentle and wide.
    double smile = 0.55 + 0.45 * std::sin(t * 0.5);

    int eyeGap = unit * 3;
    int eyeW = std::max(6, unit * 4 / 5);
    int eyeHFull = unit * 5 / 4;

    // Each eye drifts and breathes on its own phase, so they float slightly
    // out of sync (the individual-element life you asked for).
    auto drawEye = [&](double sideSign, double phase) {
      double dx = std::sin(t * 1.1 + phase) * unit * 0.10;
      double dy = std::sin(t * 0.9 + phase * 1.7) * unit * 0.10;
      double breathe = 1.0 + std::sin(t * 1.3 + phase) * 0.10;
      int eh = std::max(thick, static_cast<int>(std::lround(eyeHFull * eyeOpen * breathe)));
      int X, Y;
      place(sideSign * eyeGap / 2 + dx, -static_cast<double>(unit) + dy, X, Y);
      Primitives::fillRect(controlRenderer_, SDL_Rect{X - eyeW / 2, Y - eh / 2, eyeW, eh}, glow);
    };
    drawEye(-1.0, 0.0);
    drawEye( 1.0, 2.3);

    // Mouth — a breathing smile parabola with its own drift; each sample is
    // placed through the face tilt, so the smile rocks with the wobble.
    double mdx = std::sin(t * 0.8 + 1.2) * unit * 0.10;
    double mdy = std::sin(t * 1.05 + 0.4) * unit * 0.10;
    int mouthW = unit * 3;
    double depth = smile * unit;
    int N = 12;
    for (int i = 0; i <= N; ++i) {
      double fx = static_cast<double>(i) / N * 2.0 - 1.0;
      double ox = fx * mouthW / 2 + mdx;
      double oy = unit * 3.0 / 2.0 + depth * (1.0 - fx * fx) + mdy;
      int X, Y;
      place(ox, oy, X, Y);
      Primitives::fillRect(controlRenderer_, SDL_Rect{X - thick / 2, Y - thick / 2, thick, thick}, glow);
    }

    // --- Twinkling stars slowly orbiting the face, each with its own pulse ---
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    for (int s = 0; s < 3; ++s) {
      double ph = t * 0.6 + s * (2.0 * kPi / 3.0);
      double rad = unit * 3.2 + std::sin(t * 0.7 + s) * unit * 0.4;
      int sx = cx + static_cast<int>(std::lround(std::cos(ph) * rad * 1.5));
      int sy = cy - unit + static_cast<int>(std::lround(std::sin(ph) * rad * 0.7));
      SDL_Color star = glow;
      star.a = static_cast<Uint8>(60 + 150 * std::fabs(std::sin(t * 1.6 + s * 1.3)));
      int sz = 2 + (s % 2) + (std::sin(t * 2.0 + s) > 0.6 ? 1 : 0);
      drawStar(controlRenderer_, sx, sy, sz, star);
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    // Rotating tip under the face — bigger text, no box.
    int tipIdx = static_cast<int>(nowMs / 4500) % tipCount;
    TTF_Font* tipFont = fontBase_ ? fontBase_ : fontSmall_;
    int tipH = std::max(22, textLineHeight(tipFont) + 4);
    int tipY = std::min(area.y + area.h - tipH - 6, cy + unit * 3);
    SDL_Rect tipRect {area.x + 12, tipY, area.w - 24, tipH};
    drawCenteredTextSafe(controlRenderer_, tipFont, tipRect, kTips[tipIdx], pal.fg);
  }

  // Render the main panel split into program area (left) and inspector (right).
  // The inspector width is adjustable via a splitter drag handle.
  void renderMainPanel(const SDL_Rect& panel) {
    const Deck& deck = focusedDeck();
    const MediaEngine* engine = focusedMediaEngine();
    const Cue* selectedCue = selectedCuePtr();
    const Cue* activeCue = activeCuePtr();
    constexpr int kInspectorMinW = 360;
    constexpr int kProgramMinW = 420;
    int inspectorShellMaxW = std::max(kInspectorMinW, panel.w - kProgramMinW);
    int inspectorShellW = inspectorPaneWidth_ > 0
      ? std::clamp(inspectorPaneWidth_, kInspectorMinW, inspectorShellMaxW)
      : std::clamp(panel.w / 2, 420, 560);
    if (panel.w - inspectorShellW < kProgramMinW) {
      inspectorShellW = std::max(kInspectorMinW, panel.w - kProgramMinW);
    }
    SDL_Rect programShell {panel.x, panel.y, std::max(0, panel.w - inspectorShellW - kLayoutPanelGap), panel.h};
    SDL_Rect inspectorShell {programShell.x + programShell.w + kLayoutPanelGap, panel.y,
                             std::max(0, panel.w - programShell.w - kLayoutPanelGap), panel.h};
    inspectorSplitterRect_ = {programShell.x + programShell.w, panel.y, kLayoutPanelGap, panel.h};
    // Simple panel chrome (drawOperationalPanel removed)
    constexpr int kOpHeaderH = 28;
    {
      SDL_Rect hdr {programShell.x, programShell.y, programShell.w, kOpHeaderH};
      drawUIPanel(hdr, pal.dark, pal.deep, pal.mid);
      drawText(controlRenderer_, fontSmall_, "TIMELINE", pal.light, hdr.x + 6, hdr.y + 4);
    }
    {
      SDL_Rect hdr {inspectorShell.x, inspectorShell.y, inspectorShell.w, kOpHeaderH};
      drawUIPanel(hdr, pal.dark, pal.deep, pal.mid);
      drawText(controlRenderer_, fontSmall_, "CUE INSPECTOR", pal.light, hdr.x + 6, hdr.y + 4);
    }
    if (inspectorSplitterRect_.w > 0 && inspectorSplitterRect_.h > 0) {
      bool active = layoutDragMode_ == LayoutDragMode::Inspector;
      bool hover = !inTouchMode() && pointInRect(mouseX_, mouseY_, inspectorSplitterRect_);
      SDL_Rect rail {inspectorSplitterRect_.x + inspectorSplitterRect_.w / 2 - 1,
                     inspectorSplitterRect_.y + 12, 2, std::max(0, inspectorSplitterRect_.h - 24)};
      SDL_Color railColor = active ? pal.light
                                   : (hover ? pal.dark : pal.mid);
      Primitives::fillRect(controlRenderer_, rail, railColor);
      SDL_Rect grip {inspectorSplitterRect_.x + 1,
                     inspectorSplitterRect_.y + inspectorSplitterRect_.h / 2 - 22,
                     std::max(0, inspectorSplitterRect_.w - 2), 44};
      drawUIPanel(grip,
                  active ? pal.dark : pal.shellInner,
                  pal.deep,
                  hover || active ? pal.light : pal.mid);
      for (int dot = 0; dot < 4; ++dot) {
        SDL_Rect pip {grip.x + grip.w / 2 - 1, grip.y + 8 + dot * 8, 2, 2};
        Primitives::fillRect(controlRenderer_, pip,
                             active ? pal.light : pal.dark);
      }
    }
    SDL_Rect programBody {programShell.x, programShell.y + kOpHeaderH, programShell.w, std::max(0, programShell.h - kOpHeaderH)};
    SDL_Rect inspectorBody {inspectorShell.x, inspectorShell.y + kOpHeaderH, inspectorShell.w, std::max(0, inspectorShell.h - kOpHeaderH)};
    int innerX = programBody.x;
    int innerY = programBody.y;
    int innerW = std::max(0, programBody.w);
    int innerH = std::max(0, programBody.h);
    int x = innerX;
    int y = innerY;
    quickButtons_.clear();
    valueScrubZones_.clear();
    cueSettingsQuickButtonStartIndex_ = 0;
    cueSettingsScrubZoneStartIndex_ = 0;
    cueSettingsViewportRect_ = SDL_Rect {};
    cuePatternTypeDropdownRect_ = SDL_Rect {};
    cueTransitionStyleDropdownRect_ = SDL_Rect {};

    auto cueSummaryLabel = [&](const Cue* cue, int cueIndex, const std::string& fallback) {
      if (!cue || cueIndex < 0) {
        return fallback;
      }
      return cueDisplayToken(*cue, cueIndex) + "  " + cue->name;
    };
    double engDuration = engine ? engine->duration() : 0.0;
    double engPosition = engine ? engine->position() : 0.0;
    double remaining = engDuration > 0.0 ? std::max(0.0, engDuration - engPosition) : 0.0;
    bool countdownActive = activeCue && engDuration > 0.0;
    bool liveDeleteWarnActive =
      pendingLiveDeleteConfirmDeckIndex_ == project_.focusedDeckIndex &&
      !pendingLiveDeleteConfirmMessage_.empty() &&
      animationNow_ <= pendingLiveDeleteConfirmUntilMs_;
    const Cue* timelineCue = activeCue ? activeCue : selectedCue;
    int timelineCueIndex = activeCue ? deck.activeIndex : deck.selectedIndex;
    double timelineDuration = 0.0;
    bool timelineZoomedToTrim = false;
    double timelineCueIn = 0.0;
    double timelineCueOut = 0.0;
    if (timelineCue) {
      if (timelineCue->kind == CueKind::Video || timelineCue->kind == CueKind::Audio) {
        double sourceDuration = std::max(0.0, timelineCue->duration);
        timelineCueIn = std::clamp(timelineCue->inPointSeconds, 0.0, sourceDuration);
        timelineCueOut = timelineCue->outPointSeconds > 0.0
          ? std::clamp(timelineCue->outPointSeconds, timelineCueIn, sourceDuration)
          : sourceDuration;
        timelineZoomedToTrim = timelineCueIn > 0.001 || timelineCueOut < sourceDuration - 0.001;
        timelineDuration = timelineZoomedToTrim
          ? std::max(0.01, timelineCueOut - timelineCueIn)
          : sourceDuration;
      } else if (timelineCue == activeCue && engDuration > 0.0) {
        timelineDuration = engDuration;
      } else {
        timelineDuration = timelineCue->stillDurationSeconds;
      }
    }
    double timelinePlaySeconds = 0.0;
    if (timelineCue == activeCue && timelineCue) {
      if (timelineCue->kind == CueKind::Video || timelineCue->kind == CueKind::Audio) {
        double absolutePlayhead = std::clamp(timelineCueIn + engPosition, timelineCueIn, timelineCueOut);
        timelinePlaySeconds = timelineZoomedToTrim
          ? std::clamp(absolutePlayhead - timelineCueIn, 0.0, timelineDuration)
          : absolutePlayhead;
      } else {
        timelinePlaySeconds = engPosition;
      }
    }
    float timelinePlayFrac = (timelineCue == activeCue && timelineDuration > 0.0)
      ? static_cast<float>(std::clamp(timelinePlaySeconds / timelineDuration, 0.0, 1.0))
      : -1.0f;
    float timelineInFrac = 0.0f;
    float timelineOutFrac = 1.0f;
    if (timelineCue && timelineDuration > 0.0 &&
        !timelineZoomedToTrim &&
        (timelineCue->kind == CueKind::Video || timelineCue->kind == CueKind::Audio)) {
      timelineInFrac = static_cast<float>(std::clamp(timelineCueIn / timelineDuration, 0.0, 1.0));
      timelineOutFrac = static_cast<float>(std::clamp(timelineCueOut / timelineDuration, 0.0, 1.0));
    }
    std::vector<double> timelinePausePoints = timelineCue ? timelineCue->pausePoints : std::vector<double> {};
    if (timelineCue &&
        !timelineZoomedToTrim &&
        (timelineCue->kind == CueKind::Video || timelineCue->kind == CueKind::Audio) &&
        timelineCueIn > 0.001) {
      for (double& pausePoint : timelinePausePoints) {
        pausePoint = std::clamp(timelineCueIn + pausePoint, timelineCueIn, timelineCueOut);
      }
    }

    constexpr int kTimelineHeaderH = 60;
    constexpr int kVideoLaneBaseH = 92;
    constexpr int kAudioLaneBaseH = 68;
    constexpr int kTimelineGap = 6;
    constexpr int kDeleteWarnH = 42;
    constexpr int kTransportRowH = 24;
    constexpr int kMonitorGap = 12;
    // Fixed timeline chrome plus the base lane heights. The operator can steal
    // extra height from the preview (timelineExtraH_, dragged via the splitter
    // below) to grow the two lanes; the video lane gets the larger share.
    int baseReservedH = kTimelineHeaderH + 2 + kVideoLaneBaseH + kTimelineGap
                      + kAudioLaneBaseH + kTimelineGap + kTransportRowH;
    if (liveDeleteWarnActive) {
      baseReservedH += kDeleteWarnH + kTimelineGap;
    }
    // Monitor height when no extra is taken; clamp the operator's request so
    // the preview never drops below kProgramMonitorMinH.
    int fullMonitorH = std::max(kProgramMonitorMinH, innerH - baseReservedH - kMonitorGap);
    programAreaRect_ = {innerX, innerY, innerW, innerH};
    programFullMonitorH_ = fullMonitorH;
    int maxExtra = std::max(0, fullMonitorH - kProgramMonitorMinH);
    timelineExtraH_ = std::clamp(timelineExtraH_, 0, maxExtra);
    int videoLaneH = kVideoLaneBaseH + (timelineExtraH_ * 57) / 100;
    int audioLaneH = kAudioLaneBaseH + (timelineExtraH_ - (timelineExtraH_ * 57) / 100);
    int reservedTimelineH = baseReservedH - kVideoLaneBaseH - kAudioLaneBaseH + videoLaneH + audioLaneH;
    int monitorAreaH = std::max(kProgramMonitorMinH, innerH - reservedTimelineH - kMonitorGap);

    // Program / Preview monitors.
    int monitorY = innerY;

    constexpr int kVuMeterW = 84;
    constexpr int kVuMeterGap = 8;
    int vuMeterW = std::clamp(innerW / 11, 68, kVuMeterW);
    int monitorContentW = std::max(360, innerW - vuMeterW - kVuMeterGap);
    int programMonitorW = monitorContentW;

    int monitorH = std::max(160, monitorAreaH);

    SDL_Rect programMonitorRect {x, monitorY, programMonitorW, monitorH};
    SDL_Rect vuMeterRect {
      programMonitorRect.x + programMonitorRect.w + kVuMeterGap,
      monitorY,
      vuMeterW,
      monitorH
    };
    int timelineTopY = programMonitorRect.y + programMonitorRect.h + kMonitorGap;

    // Program/timeline splitter — a draggable grip in the gap under the
    // monitor. Drag up to enlarge the timeline lanes (stealing height from the
    // preview); drag down to give it back. Only interactive when there is room
    // to move (maxExtra > 0).
    timelineSplitterRect_ = (maxExtra > 0)
      ? SDL_Rect {innerX, programMonitorRect.y + programMonitorRect.h, innerW, kMonitorGap}
      : SDL_Rect {};
    if (timelineSplitterRect_.w > 0) {
      bool active = layoutDragMode_ == LayoutDragMode::Timeline;
      bool hover = !inTouchMode() && pointInRect(mouseX_, mouseY_, timelineSplitterRect_);
      int gripW = std::clamp(innerW / 3, 80, 140);
      SDL_Rect grip {innerX + (innerW - gripW) / 2, timelineSplitterRect_.y + 1,
                     gripW, std::max(2, kMonitorGap - 2)};
      drawUIPanel(grip, active ? pal.dark : pal.shellInner, pal.deep,
                  hover || active ? pal.light : pal.mid);
      for (int d = 0; d < 3; ++d) {
        SDL_Rect pip {grip.x + grip.w / 2 - 8 + d * 8, grip.y + grip.h / 2 - 1, 3, 2};
        Primitives::fillRect(controlRenderer_, pip, active ? pal.light : pal.mid);
      }
    }

    int countdownPanelW = std::clamp(innerW / 4, 220, 280);
    if (countdownPanelW > innerW - 160) {
      countdownPanelW = std::max(180, innerW / 3);
    }
    SDL_Rect timelineInfoRect {x, timelineTopY, std::max(0, innerW - countdownPanelW - 10), kTimelineHeaderH};
    SDL_Rect countdownRect {timelineInfoRect.x + timelineInfoRect.w + 10, timelineTopY,
                            std::max(0, innerW - timelineInfoRect.w - 10), kTimelineHeaderH};

    SDL_Rect timelineCueRect {timelineInfoRect.x, timelineInfoRect.y + 1, timelineInfoRect.w, 22};
    drawTextSafe(controlRenderer_, fontSmall_, timelineCueRect,
                 timelineCue ? cueSummaryLabel(timelineCue, timelineCueIndex, "No cue loaded") : "No cue loaded",
                 pal.fg);
    SDL_Rect timelineClockRect {timelineInfoRect.x, timelineInfoRect.y + 24, timelineInfoRect.w, 24};
    std::string timelineClock = timelineCue == activeCue && timelineDuration > 0.0
      ? (formatSeconds(timelinePlaySeconds) + " / " + formatSeconds(timelineDuration))
      : (timelineDuration > 0.0 ? formatSeconds(timelineDuration) : "--:--");
    drawTextSafe(controlRenderer_, fontMono_, timelineClockRect, timelineClock, pal.dark);

    auto lerpColor = [](const SDL_Color& a, const SDL_Color& b, double t) {
      double clamped = std::clamp(t, 0.0, 1.0);
      return SDL_Color {
        static_cast<Uint8>(std::lround(a.r + (b.r - a.r) * clamped)),
        static_cast<Uint8>(std::lround(a.g + (b.g - a.g) * clamped)),
        static_cast<Uint8>(std::lround(a.b + (b.b - a.b) * clamped)),
        static_cast<Uint8>(std::lround(a.a + (b.a - a.a) * clamped))
      };
    };
    bool countdownWarn = countdownActive && remaining <= 10.0;
    bool countdownCritical = countdownActive && remaining <= 5.0;
    SDL_Color countdownFill = pal.dark;
    SDL_Color countdownDeep = pal.deep;
    SDL_Color countdownEdge = pal.light;
    SDL_Color countdownLabelInk = pal.mid;
    SDL_Color countdownInk = pal.light;
    if (countdownWarn) {
      double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_) / 140.0);
      SDL_Color amberFill {182, 118, 28, 255};
      SDL_Color hotFill {232, 170, 40, 255};
      SDL_Color amberDeep {66, 32, 4, 255};
      SDL_Color amberEdge {255, 235, 180, 255};
      countdownFill = lerpColor(amberFill, hotFill, pulse);
      countdownDeep = amberDeep;
      countdownEdge = amberEdge;
      countdownLabelInk = countdownCritical ? SDL_Color {255, 242, 214, 255} : SDL_Color {58, 28, 6, 255};
      countdownInk = countdownCritical ? SDL_Color {255, 252, 244, 255} : SDL_Color {28, 16, 6, 255};
    }
    drawUIPanel(countdownRect, countdownFill, countdownDeep, countdownEdge);
    SDL_Rect countdownLabelRect {countdownRect.x + 10, countdownRect.y + 4, countdownRect.w - 20, 22};
    drawTextSafe(controlRenderer_, fontSmall_, countdownLabelRect,
                 countdownActive ? "REMAINING" : "READY", countdownLabelInk);
    std::string countdownText = countdownActive ? ("-" + formatSeconds(remaining)) : "--:--";
    TTF_Font* countdownFont = fontLarge_ ? fontLarge_ : fontBase_;
    int countdownTextW = 0;
    int countdownTextH = 0;
    if (countdownFont &&
        TTF_GetStringSize(countdownFont, countdownText.c_str(), 0, &countdownTextW, &countdownTextH) &&
        (countdownTextW > countdownRect.w - 20 || countdownTextH > countdownRect.h - 24)) {
      countdownFont = fontBase_ ? fontBase_ : countdownFont;
      if (countdownFont &&
          TTF_GetStringSize(countdownFont, countdownText.c_str(), 0, &countdownTextW, &countdownTextH) &&
          (countdownTextW > countdownRect.w - 20 || countdownTextH > countdownRect.h - 24) &&
          fontSmall_) {
        countdownFont = fontSmall_;
      }
    }
    SDL_Rect countdownValueRect {countdownRect.x + 8, countdownRect.y + 18, countdownRect.w - 16, countdownRect.h - 22};
    drawCenteredTextSafe(controlRenderer_, countdownFont, countdownValueRect, countdownText, countdownInk);

    SDL_Rect videoLaneOuter {x, timelineTopY + kTimelineHeaderH + 2, innerW, videoLaneH};
    SDL_Rect audioLaneOuter {x, videoLaneOuter.y + videoLaneOuter.h + kTimelineGap, innerW, audioLaneH};
    int deleteWarnY = audioLaneOuter.y + audioLaneOuter.h + kTimelineGap;
    int transportRowY = deleteWarnY + (liveDeleteWarnActive ? (kDeleteWarnH + kTimelineGap) : 0);
    progressBarRect_ = insetRect(videoLaneOuter, 3);
    SDL_Rect audioLaneRect = insetRect(audioLaneOuter, 2);
    audioProgressBarRect_ = audioLaneRect;  // audio lane is click-to-seek too
    drawUIPanel(videoLaneOuter, pal.light, pal.deep, pal.mid);
    drawUIPanel(audioLaneOuter, pal.light, pal.deep, pal.mid);
    drawText(controlRenderer_, fontSmall_, "VIDEO", pal.deep,
             videoLaneOuter.x + 8, videoLaneOuter.y + 2);
    drawText(controlRenderer_, fontSmall_, "AUDIO", pal.deep,
             audioLaneOuter.x + 8, audioLaneOuter.y + 2);

    auto drawTimelineLoadingAnimation = [&](const SDL_Rect& laneRect,
                                           const char* label = "LOADING",
                                           bool audioMode = false) {
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      Primitives::fillRect(controlRenderer_, laneRect, SDL_Color {7, 12, 7, 148});
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

      int widgetW = std::min(188, std::max(124, laneRect.w - 28));
      int widgetH = std::min(46, std::max(34, laneRect.h - 16));
      SDL_Rect widget {
        laneRect.x + (laneRect.w - widgetW) / 2,
        laneRect.y + (laneRect.h - widgetH) / 2,
        widgetW,
        widgetH
      };
      drawUIPanel(widget, pal.light, pal.deep, pal.mid);

      constexpr int kCellCount = 5;
      constexpr int kCellW = 20;
      constexpr int kCellH = 16;
      constexpr int kCellGap = 6;
      int stripW = kCellCount * kCellW + (kCellCount - 1) * kCellGap;
      int stripX = widget.x + (widget.w - stripW) / 2;
      int stripY = widget.y + 6;
      int activeCell = static_cast<int>((animationNow_ / 140) % kCellCount);
      int accentCell = static_cast<int>((animationNow_ / 220) % kCellCount);

      for (int i = 0; i < kCellCount; ++i) {
        int bob = (i == activeCell) ? 2 : ((i + accentCell) % kCellCount == 0 ? 1 : 0);
        SDL_Rect cell {stripX + i * (kCellW + kCellGap), stripY - bob, kCellW, kCellH};
        SDL_Color fill = (i == activeCell) ? pal.dark : pal.mid;
        SDL_Color ink = (i == activeCell) ? pal.light : pal.deep;
        drawUIPanel(cell, fill, pal.deep, pal.light);

        if (audioMode) {
          int barCount = 3;
          int barGap = 2;
          int barW = 3;
          int barsTotalW = barCount * barW + (barCount - 1) * barGap;
          int barsX = cell.x + (cell.w - barsTotalW) / 2;
          int baseY = cell.y + cell.h - 4;
          for (int bar = 0; bar < barCount; ++bar) {
            double phase = static_cast<double>(animationNow_) * 0.012 + i * 0.9 + bar * 0.6;
            int barH = 3 + static_cast<int>(std::lround((std::sin(phase) * 0.5 + 0.5) * 6.0));
            SDL_Rect meter {barsX + bar * (barW + barGap), baseY - barH, barW, barH};
            Primitives::fillRect(controlRenderer_, meter, ink);
          }
        } else {
          SDL_Rect frameInner {cell.x + 4, cell.y + 3, cell.w - 8, cell.h - 6};
          SDL_Color innerFill = (i == activeCell) ? pal.light : pal.dark;
          Primitives::fillRect(controlRenderer_, frameInner, innerFill);

          SDL_Rect sprocketTopL {cell.x + 1, cell.y + 2, 2, 2};
          SDL_Rect sprocketBottomL {cell.x + 1, cell.y + cell.h - 4, 2, 2};
          SDL_Rect sprocketTopR {cell.x + cell.w - 3, cell.y + 2, 2, 2};
          SDL_Rect sprocketBottomR {cell.x + cell.w - 3, cell.y + cell.h - 4, 2, 2};
          Primitives::fillRect(controlRenderer_, sprocketTopL, ink);
          Primitives::fillRect(controlRenderer_, sprocketBottomL, ink);
          Primitives::fillRect(controlRenderer_, sprocketTopR, ink);
          Primitives::fillRect(controlRenderer_, sprocketBottomR, ink);
        }
      }

      int dotCount = static_cast<int>((animationNow_ / 180) % 4);
      std::string loadingLabel = label;
      for (int i = 0; i < dotCount; ++i) {
        loadingLabel += '.';
      }
      SDL_Rect loadingRect {widget.x + 4, widget.y + widget.h - 20, widget.w - 8, 18};
      TTF_Font* loadingFont = fontPixel_ ? fontPixel_ : fontSmall_;
      int loadingTextW = 0;
      int loadingTextH = 0;
      if (!loadingFont ||
          !TTF_GetStringSize(loadingFont, loadingLabel.c_str(), 0, &loadingTextW, &loadingTextH) ||
          loadingTextW > loadingRect.w) {
        loadingFont = fontSmall_ ? fontSmall_ : loadingFont;
      }
      if (loadingFont &&
          TTF_GetStringSize(loadingFont, loadingLabel.c_str(), 0, &loadingTextW, &loadingTextH) &&
          loadingTextW > loadingRect.w &&
          fontMono_) {
        loadingFont = fontMono_;
      }
      double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_) / 160.0);
      SDL_Color inkA = pal.deep;
      SDL_Color inkB = pal.dark;
      SDL_Color loadingInk {
        static_cast<Uint8>(std::lround(inkA.r + (inkB.r - inkA.r) * pulse)),
        static_cast<Uint8>(std::lround(inkA.g + (inkB.g - inkA.g) * pulse)),
        static_cast<Uint8>(std::lround(inkA.b + (inkB.b - inkA.b) * pulse)),
        255
      };
      drawCenteredTextSafe(controlRenderer_, loadingFont, loadingRect,
                           loadingLabel, loadingInk);
    };

    // Audio-lane companion to drawTimelineLoadingAnimation. Uses the same
    // widget frame and pulsing LOADING label so the two feel like siblings,
    // but the iconography is an animated EQ meter (rising/falling bars) to
    // clearly distinguish audio-loading from video-filmstrip-loading.
    auto drawAudioTimelineLoadingAnimation = [&](const SDL_Rect& laneRect) {
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      Primitives::fillRect(controlRenderer_, laneRect, SDL_Color {7, 12, 7, 148});
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

      int widgetW = std::min(188, std::max(124, laneRect.w - 28));
      int widgetH = std::min(46, std::max(34, laneRect.h - 16));
      SDL_Rect widget {
        laneRect.x + (laneRect.w - widgetW) / 2,
        laneRect.y + (laneRect.h - widgetH) / 2,
        widgetW,
        widgetH
      };
      drawUIPanel(widget, pal.light, pal.deep, pal.mid);

      constexpr int kBarCount = 9;
      constexpr int kBarW = 8;
      constexpr int kBarGap = 4;
      int metersW = kBarCount * kBarW + (kBarCount - 1) * kBarGap;
      int metersX = widget.x + (widget.w - metersW) / 2;
      int metersTop = widget.y + 5;
      int metersBot = widget.y + widget.h - 22;
      int metersH = std::max(6, metersBot - metersTop);

      SDL_Rect baseline {metersX - 2, metersBot, metersW + 4, 1};
      Primitives::fillRect(controlRenderer_, baseline, pal.deep);

      for (int i = 0; i < kBarCount; ++i) {
        double phase = static_cast<double>(animationNow_) / 160.0
                     + static_cast<double>(i) * 0.62;
        double s = 0.5 + 0.5 * std::sin(phase);
        double env = 0.18 + 0.82 * (s * s);
        int barH = std::max(2, static_cast<int>(std::round(env * metersH)));
        SDL_Rect bar {metersX + i * (kBarW + kBarGap),
                      metersBot - barH,
                      kBarW,
                      barH};
        SDL_Color barFill = (env > 0.75) ? pal.dark : pal.mid;
        drawUIPanel(bar, barFill, pal.deep, pal.light);
        SDL_Rect cap {bar.x + 1, bar.y, bar.w - 2, 2};
        Primitives::fillRect(controlRenderer_, cap, pal.light);
      }

      int dotCount = static_cast<int>((animationNow_ / 180) % 4);
      std::string loadingLabel = "LOADING";
      for (int i = 0; i < dotCount; ++i) {
        loadingLabel += '.';
      }
      SDL_Rect loadingRect {widget.x + 4, widget.y + widget.h - 20, widget.w - 8, 18};
      TTF_Font* loadingFont = fontPixel_ ? fontPixel_ : fontSmall_;
      int loadingTextW = 0;
      int loadingTextH = 0;
      if (!loadingFont ||
          !TTF_GetStringSize(loadingFont, loadingLabel.c_str(), 0, &loadingTextW, &loadingTextH) ||
          loadingTextW > loadingRect.w) {
        loadingFont = fontSmall_ ? fontSmall_ : loadingFont;
      }
      if (loadingFont &&
          TTF_GetStringSize(loadingFont, loadingLabel.c_str(), 0, &loadingTextW, &loadingTextH) &&
          loadingTextW > loadingRect.w &&
          fontMono_) {
        loadingFont = fontMono_;
      }
      double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_) / 160.0);
      SDL_Color inkA = pal.deep;
      SDL_Color inkB = pal.dark;
      SDL_Color loadingInk {
        static_cast<Uint8>(std::lround(inkA.r + (inkB.r - inkA.r) * pulse)),
        static_cast<Uint8>(std::lround(inkA.g + (inkB.g - inkA.g) * pulse)),
        static_cast<Uint8>(std::lround(inkA.b + (inkB.b - inkA.b) * pulse)),
        255
      };
      drawCenteredTextSafe(controlRenderer_, loadingFont, loadingRect,
                           loadingLabel, loadingInk);
    };

    auto drawTimelineMarkerLine = [&](float frac, SDL_Color color) {
      int px = progressBarRect_.x + static_cast<int>(std::round(std::clamp(frac, 0.0f, 1.0f) * progressBarRect_.w));
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(controlRenderer_, color.r, color.g, color.b, color.a);
      SDL_RenderLine(controlRenderer_, px, progressBarRect_.y, px, progressBarRect_.y + progressBarRect_.h);
      SDL_RenderLine(controlRenderer_, px, audioLaneRect.y, px, audioLaneRect.y + audioLaneRect.h);
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    };

    // Time graduation lines only make sense over an actual timeline. Content
    // (filmstrip/waveform) overpaints them, so drawing them unconditionally
    // meant they only ever showed through on the EMPTY lanes — stray vertical
    // bars under the "take or select a cue..." placeholder. Theme ink instead
    // of a hardcoded dark green so light themes don't get harsh lines.
    if (timelineCue && timelineDuration > 0.0) {
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      SDL_Color gridInk = pal.dark;
      for (int grid = 1; grid < 8; ++grid) {
        float frac = static_cast<float>(grid) / 8.0f;
        int px = progressBarRect_.x + static_cast<int>(std::round(frac * progressBarRect_.w));
        SDL_SetRenderDrawColor(controlRenderer_, gridInk.r, gridInk.g, gridInk.b, 140);
        SDL_SetRenderClipRect(controlRenderer_, &progressBarRect_);
        SDL_RenderLine(controlRenderer_, px, progressBarRect_.y, px, progressBarRect_.y + progressBarRect_.h - 1);
        SDL_SetRenderClipRect(controlRenderer_, &audioLaneRect);
        SDL_RenderLine(controlRenderer_, px, audioLaneRect.y, px, audioLaneRect.y + audioLaneRect.h - 1);
      }
      SDL_SetRenderClipRect(controlRenderer_, nullptr);
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    }

    bool timelineStripFailed = false;
    bool timelineHasCurrentSelectedThumb =
      timelineCue && timelineCue == selectedCue &&
      selectedThumbnailCueKey_ == cueVisualCacheKey(*timelineCue) &&
      selectedThumbnailTex_ && selectedThumbnailTexW_ > 0 && selectedThumbnailTexH_ > 0;
    bool timelineStripLoadingCurrent =
      timelineCue && timelineCue->kind == CueKind::Video &&
      timelineStripCueKey_ == cueVisualCacheKey(*timelineCue) &&
      timelineStripLoading_.load();
    if (timelineCue && timelineCue->kind == CueKind::Video) {
      std::lock_guard<std::mutex> lk(timelineStripMutex_);
      timelineStripFailed = timelineStripFailedCueKey_ == cueVisualCacheKey(*timelineCue);
    }

    if (timelineCue && timelineCue->kind == CueKind::Video &&
        timelineStripTex_ && timelineStripCueId_ == timelineCue->id &&
        timelineStripTexW_ > 0 && timelineStripTexH_ > 0) {
      SDL_Rect stripDst = progressBarRect_;
      SDL_SetTextureBlendMode(timelineStripTex_, SDL_BLENDMODE_NONE);
      // Smooth (linear) upscale for the photographic thumbnails.
      SDL_SetTextureScaleMode(timelineStripTex_, SDL_SCALEMODE_LINEAR);
      // Draw each tile into its own equal column with an aspect-preserving
      // centre-crop (fill), rather than stretching the whole strip to the lane
      // rect. Stretching skewed every thumbnail's aspect as the lane grew
      // taller via the splitter; cropping keeps them all consistent at any
      // lane size. Columns use rounded edges so they tile seamlessly.
      const int stripTiles = kTimelineStripThumbCount;
      const double srcAspect =
        static_cast<double>(kTimelineStripThumbW) / static_cast<double>(kTimelineStripThumbH);
      for (int ti = 0; ti < stripTiles; ++ti) {
        int colX0 = stripDst.x + ti * stripDst.w / stripTiles;
        int colX1 = stripDst.x + (ti + 1) * stripDst.w / stripTiles;
        SDL_Rect dst {colX0, stripDst.y, std::max(1, colX1 - colX0), stripDst.h};
        int srcX = ti * (kTimelineStripThumbW + kTimelineStripPadding);
        SDL_Rect src {srcX, 0, kTimelineStripThumbW, kTimelineStripThumbH};
        double dstAspect = static_cast<double>(dst.w) / static_cast<double>(std::max(1, dst.h));
        if (srcAspect > dstAspect) {
          // Source is wider than the column — crop its sides.
          int cropW = std::max(1, static_cast<int>(std::lround(kTimelineStripThumbH * dstAspect)));
          src.x = srcX + (kTimelineStripThumbW - cropW) / 2;
          src.w = cropW;
        } else {
          // Source is taller than the column — crop top and bottom.
          int cropH = std::max(1, static_cast<int>(std::lround(kTimelineStripThumbW / dstAspect)));
          src.y = (kTimelineStripThumbH - cropH) / 2;
          src.h = cropH;
        }
        SDL_RenderTexture(controlRenderer_, timelineStripTex_, &src, &dst);
      }
      if (timelineStripLoadingCurrent) {
        drawTimelineLoadingAnimation(progressBarRect_);
      }
    } else if (timelineCue && timelineCue->kind == CueKind::Video &&
               timelineHasCurrentSelectedThumb) {
      SDL_Rect dst = progressBarRect_;
      SDL_SetTextureBlendMode(selectedThumbnailTex_, SDL_BLENDMODE_NONE);
      SDL_RenderTexture(controlRenderer_, selectedThumbnailTex_, nullptr, &dst);
      if (!timelineStripFailed) {
        drawTimelineLoadingAnimation(progressBarRect_);
      }
    } else if (timelineCue && timelineCue->kind == CueKind::Image &&
               timelineHasCurrentSelectedThumb) {
      SDL_Rect dst = progressBarRect_;
      SDL_SetTextureBlendMode(selectedThumbnailTex_, SDL_BLENDMODE_NONE);
      SDL_RenderTexture(controlRenderer_, selectedThumbnailTex_, nullptr, &dst);
    } else if (timelineCue &&
               timelineHasCurrentSelectedThumb &&
               timelineCue->kind != CueKind::Video &&
               selectedThumbnailTex_ && selectedThumbnailTexW_ > 0 && selectedThumbnailTexH_ > 0) {
      SDL_SetTextureBlendMode(selectedThumbnailTex_, SDL_BLENDMODE_NONE);
      float aspect = static_cast<float>(selectedThumbnailTexW_) / static_cast<float>(std::max(1, selectedThumbnailTexH_));
      int tileW = std::max(56, static_cast<int>(std::round(progressBarRect_.h * aspect)));
      for (int tileX = progressBarRect_.x; tileX < progressBarRect_.x + progressBarRect_.w; tileX += tileW + 2) {
        SDL_Rect dst {tileX, progressBarRect_.y, std::min(tileW, progressBarRect_.x + progressBarRect_.w - tileX), progressBarRect_.h};
        SDL_RenderTexture(controlRenderer_, selectedThumbnailTex_, nullptr, &dst);
      }
    } else if (timelineCue) {
      if (timelineCue->kind == CueKind::Video && !timelineStripFailed) {
        drawTimelineLoadingAnimation(progressBarRect_);
      } else {
        drawCenteredTextSafe(controlRenderer_, fontSmall_, progressBarRect_,
                             timelineCue->kind == CueKind::Video
                               ? "filmstrip unavailable"
                               : "no video lane for this cue",
                             pal.inkSoft);
      }
    } else {
      drawCenteredTextSafe(controlRenderer_, fontSmall_, progressBarRect_, "take or select a cue to open the timeline",
                           pal.inkSoft);
      // Idle animation — subtle orbiting dots in the timeline strip (safe UI chrome area)
      if (!engine || engine->state() == TransportState::Stopped) {
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderClipRect(controlRenderer_, &progressBarRect_);
        SDL_Color idleDot = pal.mid;
        for (int d = 0; d < 5; ++d) {
          double phase = static_cast<double>(animationNow_) * 0.0008 + d * 1.256;
          int dx = progressBarRect_.x + progressBarRect_.w / 2 + static_cast<int>(std::cos(phase) * (progressBarRect_.w / 6));
          int dy = progressBarRect_.y + progressBarRect_.h / 2 + static_cast<int>(std::sin(phase * 0.7) * (progressBarRect_.h / 3));
          idleDot.a = static_cast<Uint8>(30 + 60 * std::abs(std::sin(phase * 0.5 + d)));
          drawStar(controlRenderer_, dx, dy, 2, idleDot);
        }
        SDL_SetRenderClipRect(controlRenderer_, nullptr);
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
      }
    }

    if (timelineDuration > 0.0) {
      int leftShadeW = static_cast<int>(std::round(progressBarRect_.w * std::clamp(timelineInFrac, 0.0f, 1.0f)));
      int rightShadeW = static_cast<int>(std::round(progressBarRect_.w * (1.0f - std::clamp(timelineOutFrac, 0.0f, 1.0f))));
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      if (leftShadeW > 0) {
        SDL_Rect shade {progressBarRect_.x, progressBarRect_.y, leftShadeW, progressBarRect_.h};
        Primitives::fillRect(controlRenderer_, shade, SDL_Color{8, 14, 8, 170});
      }
      if (rightShadeW > 0) {
        SDL_Rect shade {progressBarRect_.x + progressBarRect_.w - rightShadeW, progressBarRect_.y, rightShadeW, progressBarRect_.h};
        Primitives::fillRect(controlRenderer_, shade, SDL_Color{8, 14, 8, 170});
      }
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    }

    if (timelineCue && !timelinePausePoints.empty() && timelineDuration > 0.0) {
      for (double pp : timelinePausePoints) {
        float ppFrac = static_cast<float>(std::clamp(pp / timelineDuration, 0.0, 1.0));
        drawTimelineMarkerLine(ppFrac, SDL_Color{220, 120, 30, 190});
      }
    }

    trimInHandleRect_ = {};
    trimOutHandleRect_ = {};
    if (timelineDuration > 0.0) {
      if (timelineCue && timelineCue->inPointSeconds > 0.001) {
        drawTimelineMarkerLine(timelineInFrac, SDL_Color{30, 200, 60, 220});
      }
      if (timelineCue && timelineCue->outPointSeconds > 0.001 &&
          (!timelineZoomedToTrim ? timelineCue->outPointSeconds < timelineDuration - 0.01
                                 : timelineCue->outPointSeconds < std::max(0.0, timelineCue->duration) - 0.01)) {
        drawTimelineMarkerLine(timelineOutFrac, SDL_Color{220, 50, 40, 220});
      }
      if (timelineCue == activeCue && activeCue) {
        constexpr int kHandleW = 8;
        int kHandleH = videoLaneH - 4;
        if (activeCue->inPointSeconds > 0.001) {
          int inX = progressBarRect_.x + static_cast<int>(std::round(progressBarRect_.w * timelineInFrac)) - kHandleW / 2;
          inX = std::clamp(inX, progressBarRect_.x, progressBarRect_.x + progressBarRect_.w - kHandleW);
          trimInHandleRect_ = {inX, progressBarRect_.y, kHandleW, kHandleH};
        }
        if (activeCue->outPointSeconds > 0.001 &&
            (!timelineZoomedToTrim ? activeCue->outPointSeconds < timelineDuration - 0.01
                                   : activeCue->outPointSeconds < std::max(0.0, activeCue->duration) - 0.01)) {
          int outX = progressBarRect_.x + static_cast<int>(std::round(progressBarRect_.w * timelineOutFrac)) - kHandleW / 2;
          outX = std::clamp(outX, progressBarRect_.x, progressBarRect_.x + progressBarRect_.w - kHandleW);
          trimOutHandleRect_ = {outX, progressBarRect_.y, kHandleW, kHandleH};
        }
      }
    }

    bool timelineCueFileAudio = timelineCue && timelineCue->hasAudio &&
      (timelineCue->kind == CueKind::Video || timelineCue->kind == CueKind::Audio);
    bool timelineCueLiveAudio = timelineCue && timelineCue->hasAudio && !timelineCueFileAudio;
    if (timelineCueFileAudio) {
      bool _wfPending = false;
      WaveformPeaks peaks = getWaveformPeaks(resolvedCueFilesystemPathString(*timelineCue, currentProjectFile_), _wfPending);
      drawWaveform(controlRenderer_, audioLaneRect, peaks, timelineCue->audioChannels >= 2, timelinePlayFrac, timelineInFrac, timelineOutFrac,
                   timelinePausePoints, timelineDuration);
      drawAudioFadeEnvelope(audioLaneRect, *timelineCue);
      if (peaks.empty() && _wfPending) {
        drawAudioTimelineLoadingAnimation(audioLaneRect);
      }
    } else if (timelineCueLiveAudio) {
      // Live sources (camera/NDI/SRT) carry audio but have no finite
      // waveform to analyze — say so instead of a loading animation that
      // can never finish.
      drawCenteredTextSafe(controlRenderer_, fontSmall_, audioLaneRect, "live audio",
                           pal.inkSoft);
    } else {
      drawCenteredTextSafe(controlRenderer_, fontSmall_, audioLaneRect, "no audio track",
                           pal.inkSoft);
    }

    if (timelinePlayFrac >= 0.0f && timelinePlayFrac <= 1.0f) {
      drawTimelineMarkerLine(timelinePlayFrac, SDL_Color{200, 220, 80, 255});
      // Playhead sparkle — pulsing star at the playhead top when playing
      if (engine && engine->state() == TransportState::Playing) {
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
        int phX = progressBarRect_.x + static_cast<int>(timelinePlayFrac * progressBarRect_.w);
        double phPhase = static_cast<double>(animationNow_) * 0.004;
        SDL_Color phStar {200, 220, 80, static_cast<Uint8>(120 + 135 * std::abs(std::sin(phPhase)))};
        drawStar(controlRenderer_, phX, progressBarRect_.y - 4, 3, phStar);
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
      }
    }

    if (timelineDuration > 0.0) {
      // Timeline ruler labels — drawn inside the video lane content area (progressBarRect_)
      // so they don't overlap with the "VIDEO" label in the outer panel border region.
      // Use drawText directly: safeTextRect's 12px inset on 96px rects truncates time strings.
      int rulerY = progressBarRect_.y + 2;
      std::string leftStr = "0:00";
      std::string midStr = formatSeconds(timelineDuration / 2.0);
      std::string rightStr = formatSeconds(timelineDuration);
      drawText(controlRenderer_, fontSmall_, leftStr, pal.dark,
               progressBarRect_.x + 4, rulerY);
      int midW = 0, midH = 0;
      if (fontSmall_ && TTF_GetStringSize(fontSmall_, midStr.c_str(), 0, &midW, &midH)) {
        drawText(controlRenderer_, fontSmall_, midStr, pal.dark,
                 progressBarRect_.x + progressBarRect_.w / 2 - midW / 2, rulerY);
      }
      int rightW = 0, rightH = 0;
      if (fontSmall_ && TTF_GetStringSize(fontSmall_, rightStr.c_str(), 0, &rightW, &rightH)) {
        drawText(controlRenderer_, fontSmall_, rightStr, pal.dark,
                 progressBarRect_.x + progressBarRect_.w - rightW - 4, rulerY);
      }
    }

    // --- Program monitor / live output view ---
    bool hasLiveVideo = controlPreviewTex_ && controlPreviewTexW_ > 0 && controlPreviewTexH_ > 0;
    // Once any clip has been loaded into the monitor this session, the startup
    // mascot retires for good; a clip in the monitor (or an active cue) trips it.
    if (hasLiveVideo || activeCue) {
      firstClipLoadedThisSession_ = true;
    }
    bool showMascot = !firstClipLoadedThisSession_ && !activeCue && !hasLiveVideo;
    // The mascot needs a dark backdrop for its bright LCD face to read, so the
    // empty monitor goes deep while it's up (instead of the bright idle fill).
    bool darkMonitorBg = hasLiveVideo || showMascot;
    SDL_Color programBg = darkMonitorBg ? pal.deep : pal.light;
    SDL_Color programBorder = darkMonitorBg ? pal.dark : pal.mid;

    drawUIPanel(programMonitorRect, programBg, pal.deep, programBorder);
    
    // Dominant LIVE badge
    SDL_Rect liveBadge {programMonitorRect.x + 4, programMonitorRect.y + 3, 54, 26};
    drawUIPanel(liveBadge, pal.dark, pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, liveBadge, "LIVE", pal.light);
    // Live sparkle — gentle pulsing star when output is active
    if (hasLiveVideo) {
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      double phase = static_cast<double>(animationNow_) * 0.003;
      SDL_Color liveStarC = pal.light;
      liveStarC.a = static_cast<Uint8>(100 + 155 * std::abs(std::sin(phase)));
      drawStar(controlRenderer_, liveBadge.x + liveBadge.w + 8,
               liveBadge.y + liveBadge.h / 2, 3, liveStarC);
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    }
    int monitorTelemetryEndX = liveBadge.x + liveBadge.w;
    {
      int focOutIdx = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
      int telemetryDeckIndex = std::clamp(project_.focusedDeckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
      if (focOutIdx >= 0 && focOutIdx < static_cast<int>(project_.outputs.size())) {
        telemetryDeckIndex = std::clamp(project_.outputs[focOutIdx].hostDeckIndex,
                                        0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
      }
      std::array<std::string, 3> telemetryLabels {
        programMonitorOutputTelemetryLabel(focOutIdx),
        programMonitorDecodeTelemetryLabel(telemetryDeckIndex),
        programMonitorStreamTelemetryLabel(focOutIdx),
      };
      auto splitTelemetryLabel = [](const std::string& label) {
        size_t split = label.find(' ');
        if (split == std::string::npos) {
          return std::pair<std::string, std::string> {label, ""};
        }
        return std::pair<std::string, std::string> {
          label.substr(0, split),
          trim(label.substr(split + 1))
        };
      };
      auto telemetryColors = [&](const std::string& label) {
        SDL_Color fill = hasLiveVideo ? pal.deep : pal.light;
        SDL_Color ink = hasLiveVideo ? pal.light : pal.deep;
        if (label.find("ERR") != std::string::npos) {
          fill = SDL_Color {94, 56, 8, 255};
          ink = SDL_Color {255, 238, 182, 255};
        } else if (label.find("OFF") != std::string::npos) {
          fill = pal.mid;
          ink = pal.deep;
        } else if (label.find("WARM") != std::string::npos) {
          fill = pal.light;
          ink = pal.deep;
        }
        return std::pair<SDL_Color, SDL_Color> {fill, ink};
      };
      constexpr int kTelemetryBadgePreferredW = 180;
      constexpr int kTelemetryBadgeMinW = 116;
      constexpr int kTelemetryBadgeH = 26;
      constexpr int kTelemetryGap = 3;
      constexpr int kWarpBtnW = 76;
      constexpr int kHeaderRightPad = 8;
      constexpr int kProgramLabelMinW = 110;
      int badgeX = liveBadge.x + liveBadge.w + 4;
      int warpBtnLeft = programMonitorRect.x + programMonitorRect.w - kWarpBtnW - kHeaderRightPad;
      int telemetryRightLimit = std::max(badgeX, warpBtnLeft - kProgramLabelMinW - 8);
      int availableTelemetryW = std::max(0, telemetryRightLimit - badgeX);
      int telemetryCount = 0;
      if (availableTelemetryW >= kTelemetryBadgeMinW) {
        telemetryCount = std::min<int>(
          static_cast<int>(telemetryLabels.size()),
          (availableTelemetryW + kTelemetryGap) / (kTelemetryBadgeMinW + kTelemetryGap));
      }
      if (telemetryCount > 0) {
        int totalGap = kTelemetryGap * (telemetryCount - 1);
        int badgeW = std::clamp((availableTelemetryW - totalGap) / telemetryCount,
                                kTelemetryBadgeMinW, kTelemetryBadgePreferredW);
        for (int ti = 0; ti < telemetryCount; ++ti) {
          const std::string& telemetryLabel = telemetryLabels[static_cast<size_t>(ti)];
          SDL_Rect badge {badgeX, programMonitorRect.y + 3, badgeW, kTelemetryBadgeH};
          auto [fill, ink] = telemetryColors(telemetryLabel);
          drawUIPanel(badge, fill, pal.deep, pal.mid);
          auto [labelText, valueText] = splitTelemetryLabel(telemetryLabel);
          int labelW = std::max(40, badge.w / 2 - 6);
          SDL_Rect badgeLabelRect {badge.x + 6, badge.y, labelW, badge.h};
          SDL_Rect badgeValueRect {badgeLabelRect.x + badgeLabelRect.w + 4, badge.y,
                                   std::max(20, badge.w - (badgeLabelRect.w + 16)), badge.h};
          drawTextSafe(controlRenderer_, fontSmall_, badgeLabelRect,
                       ellipsizeToPixelWidth(fontSmall_, labelText, badgeLabelRect.w), ink);
          TTF_Font* valueFont = valueText.size() > 4 ? fontSmall_ : fontMono_;
          std::string shownValue = valueText.empty() ? "--.-" : valueText;
          shownValue = ellipsizeToPixelWidth(valueFont, shownValue, badgeValueRect.w);
          int valueTextW = 0, valueTextH = 0;
          TTF_GetStringSize(valueFont, shownValue.c_str(), 0, &valueTextW, &valueTextH);
          SDL_Rect badgeClip = snapRectToGrid(badge);
          SDL_SetRenderClipRect(controlRenderer_, &badgeClip);
          drawText(controlRenderer_, valueFont, shownValue, ink,
                   badgeValueRect.x + std::max(0, badgeValueRect.w - valueTextW),
                   badgeValueRect.y + (badgeValueRect.h - valueTextH) / 2);
          SDL_SetRenderClipRect(controlRenderer_, nullptr);
          badgeX += badgeW + kTelemetryGap;
          monitorTelemetryEndX = badge.x + badge.w;
        }
      }
    }

    // WARP edit toggle button in program monitor header
    {
      const Deck& warpDeck = focusedDeck();
      int warpBtnW = 76;
      bool warpActive = warpEditMode_ && warpDeck.warpEnabled;
      warpEditBtnRect_ = {programMonitorRect.x + programMonitorRect.w - warpBtnW - 8,
                           programMonitorRect.y + 3, warpBtnW, 26};
      SDL_Color warpFill = warpActive ? pal.dark : pal.mid;
      SDL_Color warpInk2 = warpActive ? pal.light : pal.deep;
      drawUIPanel(warpEditBtnRect_, warpFill, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, warpEditBtnRect_,
                           "WARP", warpInk2);
      // MODE and RESET buttons are in the warp overlay toolbar now, not in the header
      if (!warpActive) {
        warpModeBtnRect_ = {};
        warpResetBtnRect_ = {};
      }
    }
    int monitorLabelX = monitorTelemetryEndX + 8;
    {
      int monitorLabelAvailW = std::max(0, warpEditBtnRect_.x - monitorLabelX - 4);
      TTF_Font* monitorLabelFont = fontPixelSmall_ ? fontPixelSmall_ : fontSmall_;
      // Only render the label if it fits without truncation; use a shorter
      // fallback so the badge row never shows a half-word like "PROGRA..."
      const char* monitorLabel = nullptr;
      if (monitorLabelFont) {
        int fullW = 0;
        TTF_GetStringSize(monitorLabelFont, "PROGRAM MONITOR", 0, &fullW, nullptr);
        if (fullW <= monitorLabelAvailW) {
          monitorLabel = "PROGRAM MONITOR";
        } else {
          int shortW = 0;
          TTF_GetStringSize(monitorLabelFont, "PROGRAM", 0, &shortW, nullptr);
          if (shortW <= monitorLabelAvailW) {
            monitorLabel = "PROGRAM";
          }
          // else: too narrow — draw nothing rather than an ugly truncated word
        }
      }
      if (monitorLabel && monitorLabelAvailW > 0) {
        SDL_Rect monitorLabelRect {monitorLabelX, programMonitorRect.y + 4, monitorLabelAvailW, 22};
        drawTextSafe(controlRenderer_, monitorLabelFont, monitorLabelRect,
                     monitorLabel, hasLiveVideo ? pal.dark : (showMascot ? pal.inkSoft : pal.deep));
      }
    }
    
    {
      int focusedOutputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
      auto [outW, outH] = outputRenderSizeForOutput(focusedOutputIndex);
      std::string outInfo = outputLabel(focusedOutputIndex)
        + "  " + std::to_string(outW) + "x" + std::to_string(outH);
      SDL_Rect outInfoRect {programMonitorRect.x + 4, programMonitorRect.y + programMonitorRect.h - 32, programMonitorRect.w - 8, 20};
      drawTextSafe(controlRenderer_, fontSmall_, outInfoRect, outInfo,
                   hasLiveVideo ? pal.mid : (showMascot ? pal.inkSoft : pal.dark));
    }

    warpMonitorInner_ = {programMonitorRect.x + 4, programMonitorRect.y + 28, programMonitorRect.w - 8, programMonitorRect.h - 48};
    previewMonitorInner_ = {};
    if (hasLiveVideo) {
      SDL_Rect inner = warpMonitorInner_;
      renderTextureWithCueGeometry(
        controlRenderer_,
        controlPreviewTex_,
        controlPreviewTexW_,
        controlPreviewTexH_,
        activeCue,
        inner);
    } else if (activeCue && activeCue->kind == CueKind::Composite) {
      SDL_Rect inner = warpMonitorInner_;
      renderCompositeCuePlaceholder(controlRenderer_, inner, *activeCue, true);
    } else if (!activeCue) {
      if (showMascot) {
        drawStartupMascot(warpMonitorInner_, animationNow_);
      } else {
        SDL_Rect emptyRect {programMonitorRect.x + 12, programMonitorRect.y + programMonitorRect.h / 2 - 10, programMonitorRect.w - 24, 20};
        drawCenteredTextSafe(controlRenderer_, fontSmall_, emptyRect,
                     "NO LIVE CUE",
                     pal.deep);
      }
    } else if (activeCue->kind == CueKind::Audio) {
      SDL_Rect inner {programMonitorRect.x + 4, programMonitorRect.y + 28, programMonitorRect.w - 8, programMonitorRect.h - 54};
      bool _wfPending = false;
      WaveformPeaks peaks = getWaveformPeaks(resolvedCueFilesystemPathString(*activeCue, currentProjectFile_), _wfPending);
      double dur = activeCue->duration > 0.0 ? activeCue->duration : 1.0;
      float inFrac  = static_cast<float>(activeCue->inPointSeconds / dur);
      float outFrac = activeCue->outPointSeconds > 0.0
                    ? static_cast<float>(activeCue->outPointSeconds / dur) : 1.0f;
      float playFrac = engine ? static_cast<float>(std::clamp(engine->position() / dur, 0.0, 1.0)) : -1.0f;
      drawWaveform(controlRenderer_, inner, peaks, activeCue->audioChannels >= 2, playFrac, inFrac, outFrac,
                   activeCue->pausePoints, dur);
      drawAudioFadeEnvelope(inner, *activeCue);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {programMonitorRect.x + 10, programMonitorRect.y + programMonitorRect.h - 48, programMonitorRect.w - 20, 22},
                   activeCue->name, pal.light);
    } else {
      drawTextSafe(controlRenderer_, fontBase_,
                   SDL_Rect {programMonitorRect.x + 16, programMonitorRect.y + programMonitorRect.h / 2 - 10, programMonitorRect.w - 32, 20},
                   activeCue->name, pal.deep);
    }

    // (corner sparkles and idle dots removed from preview area — moved to timeline strip)

    {
      VuReading vu = computeVuReading();
      drawUIPanel(vuMeterRect, pal.light, pal.deep, pal.mid);

      // VU meter layout zones (top to bottom):
      //   "VU" header:   22px
      //   bars + dB:     flexible
      //   "L" / "R":     18px
      //   "dB" footer:   18px
      constexpr int kVuHeaderH = 22;
      constexpr int kVuFooterLabelH = 18;
      constexpr int kVuFooterUnitH = 18;
      constexpr int kVuBottomPad = 4;
      constexpr int kVuFooterTotal = kVuFooterLabelH + kVuFooterUnitH + 2 + kVuBottomPad;

      // "VU" header
      drawCenteredText(controlRenderer_, fontSmall_, "VU", pal.deep,
                       SDL_Rect {vuMeterRect.x, vuMeterRect.y + 2, vuMeterRect.w, kVuHeaderH});

      // Bars area — give dB scale labels ~40% of width (min 28px) so numbers aren't crushed
      int barsTop = vuMeterRect.y + kVuHeaderH + 2;
      int barsH = std::max(40, vuMeterRect.h - kVuHeaderH - 2 - kVuFooterTotal - 2);
      SDL_Rect meterInner {vuMeterRect.x + 4, barsTop, vuMeterRect.w - 8, barsH};
      int labelsW = std::max(28, meterInner.w * 2 / 5);
      int barsW = std::max(18, meterInner.w - labelsW - 4);
      SDL_Rect barsRect {meterInner.x, meterInner.y, barsW, meterInner.h};
      SDL_Rect labelsRect {barsRect.x + barsRect.w + 4, meterInner.y, labelsW, meterInner.h};
      int channelGap = 4;
      int barW = std::max(6, (barsRect.w - channelGap) / 2);
      SDL_Rect leftBar {barsRect.x, barsRect.y, barW, barsRect.h};
      SDL_Rect rightBar {barsRect.x + barsRect.w - barW, barsRect.y, barW, barsRect.h};
      Primitives::drawFramedPanel(controlRenderer_, leftBar, pal.deep, pal.deep, pal.mid);
      Primitives::drawFramedPanel(controlRenderer_, rightBar, pal.deep, pal.deep, pal.mid);

      auto dbToFillFrac = [](float db) {
        return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
      };
      auto drawMeterBar = [&](const SDL_Rect& rect, float rmsLevel, float peakLevel, const char* label) {
        float rmsDb = linearLevelToDb(rmsLevel);
        float peakDb = linearLevelToDb(peakLevel);
        float fillFrac = dbToFillFrac(rmsDb);
        float peakFrac = dbToFillFrac(peakDb);
        int segmentCount = 20;
        int innerPad = 2;
        int usableH = std::max(1, rect.h - innerPad * 2);
        int segGap = 1;
        int segH = std::max(2, (usableH - (segmentCount - 1) * segGap) / segmentCount);
        for (int seg = 0; seg < segmentCount; ++seg) {
          float segTopFrac = static_cast<float>(seg + 1) / static_cast<float>(segmentCount);
          int segY = rect.y + rect.h - innerPad - (seg + 1) * segH - seg * segGap;
          SDL_Rect segRect {rect.x + innerPad, segY, rect.w - innerPad * 2, segH};
          bool lit = fillFrac >= segTopFrac;
          SDL_Color segFill = lit ? pal.light : pal.dark;
          SDL_Color segEdge = lit ? pal.mid : pal.deep;
          Primitives::fillRect(controlRenderer_, segRect, segFill);
          Primitives::strokeRect(controlRenderer_, segRect, segEdge);
        }
        int peakY = rect.y + rect.h - innerPad - static_cast<int>(std::round(peakFrac * usableH));
        peakY = std::clamp(peakY, rect.y + innerPad, rect.y + rect.h - innerPad - 1);
        SDL_SetRenderDrawColor(controlRenderer_, 245, 248, 220, 255);
        SDL_RenderLine(controlRenderer_, rect.x + 1, peakY, rect.x + rect.w - 2, peakY);
      };

      // dB scale tick marks (across full bar width) then labels clipped to labelsRect
      // Skip 0dB label — it overlaps the "VU" header; the 0dB tick line is still drawn.
      for (float markDb : {0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f}) {
        float frac = dbToFillFrac(markDb);
        int y = barsRect.y + barsRect.h - 1 - static_cast<int>(std::round(frac * std::max(1, barsRect.h - 1)));
        SDL_SetRenderDrawColor(controlRenderer_, 34, 52, 34, 255);
        SDL_RenderLine(controlRenderer_, barsRect.x, y, barsRect.x + barsRect.w, y);
        if (markDb > -0.1f) continue; // skip "0" label — "VU" header is right above
        std::string dbStr = std::to_string(static_cast<int>(markDb));
        int tw = 0, th = 0;
        if (fontSmall_ && TTF_GetStringSize(fontSmall_, dbStr.c_str(), 0, &tw, &th)) {
          SDL_SetRenderClipRect(controlRenderer_, &labelsRect);
          drawText(controlRenderer_, fontSmall_, dbStr, pal.dark, labelsRect.x, y - th / 2);
          SDL_SetRenderClipRect(controlRenderer_, nullptr);
        }
      }

      drawMeterBar(leftBar, vu.rmsLeft, vu.peakLeft, "L");
      drawMeterBar(rightBar, vu.rmsRight, vu.peakRight, "R");

      // L/R labels — below bars, above "dB"
      int lrY = barsRect.y + barsRect.h + 2;
      drawCenteredText(controlRenderer_, fontSmall_, "L", pal.deep,
                       SDL_Rect {leftBar.x - 2, lrY, leftBar.w + 4, kVuFooterLabelH});
      drawCenteredText(controlRenderer_, fontSmall_, "R", pal.deep,
                       SDL_Rect {rightBar.x - 2, lrY, rightBar.w + 4, kVuFooterLabelH});

      // "dB" footer — below L/R labels
      int dbY = lrY + kVuFooterLabelH + 2;
      drawCenteredText(controlRenderer_, fontSmall_, "dB", pal.deep,
                       SDL_Rect {vuMeterRect.x, dbY, vuMeterRect.w, kVuFooterUnitH});
    }

    warpSaveBtnRect_ = {};
    warpRecallBtnRect_ = {};
    warpCopyBtnRect_ = {};
    warpPasteBtnRect_ = {};
    // Warp editor overlay on program monitor
    if (warpEditMode_ && focusedDeck().warpEnabled && warpMonitorInner_.w > 0 && warpMonitorInner_.h > 0) {
      const Deck& wd = focusedDeck();
      SDL_Rect mi = warpMonitorInner_;
      float fw = static_cast<float>(mi.w);
      float fh = static_cast<float>(mi.h);
      // Map warp corner offsets (in output pixels) to monitor-space positions.
      // Warp offsets are relative to output resolution corners; scale to monitor rect.
      int focOutIdx = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
      auto [outW, outH] = outputRenderSizeForOutput(focOutIdx);
      float sx = fw / std::max(1.0f, static_cast<float>(outW));
      float sy = fh / std::max(1.0f, static_cast<float>(outH));
      // Corner positions in monitor space
      SDL_FPoint corners[4] = {
        {static_cast<float>(mi.x) + wd.warpTopLeftX * sx,
         static_cast<float>(mi.y) + wd.warpTopLeftY * sy},
        {static_cast<float>(mi.x + mi.w) + wd.warpTopRightX * sx,
         static_cast<float>(mi.y) + wd.warpTopRightY * sy},
        {static_cast<float>(mi.x + mi.w) + wd.warpBottomRightX * sx,
         static_cast<float>(mi.y + mi.h) + wd.warpBottomRightY * sy},
        {static_cast<float>(mi.x) + wd.warpBottomLeftX * sx,
         static_cast<float>(mi.y + mi.h) + wd.warpBottomLeftY * sy},
      };
      // Draw wireframe quad
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(controlRenderer_, 255, 220, 0, 200);
      for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        SDL_RenderLine(controlRenderer_,
          static_cast<int>(corners[i].x), static_cast<int>(corners[i].y),
          static_cast<int>(corners[j].x), static_cast<int>(corners[j].y));
      }
      // Draw corner handles
      constexpr int kWarpHandleR = 8;
      const char* cornerLabels[] = {"TL", "TR", "BR", "BL"};
      for (int i = 0; i < 4; ++i) {
        int cx = static_cast<int>(corners[i].x);
        int cy = static_cast<int>(corners[i].y);
        SDL_Rect handle {cx - kWarpHandleR, cy - kWarpHandleR, kWarpHandleR * 2, kWarpHandleR * 2};
        bool dragging = warpDragCorner_ == i;
        SDL_Color hFill = dragging ? SDL_Color{255, 220, 0, 255} : SDL_Color{255, 220, 0, 180};
        Primitives::fillRect(controlRenderer_, handle, hFill);
        Primitives::strokeRect(controlRenderer_, handle, SDL_Color{0, 0, 0, 200});
        drawCenteredText(controlRenderer_, fontSmall_, cornerLabels[i], SDL_Color{0, 0, 0, 255}, handle);
      }
      // Draw crosshair at center
      int ccx = mi.x + mi.w / 2;
      int ccy = mi.y + mi.h / 2;
      SDL_SetRenderDrawColor(controlRenderer_, 255, 220, 0, 80);
      SDL_RenderLine(controlRenderer_, mi.x, ccy, mi.x + mi.w, ccy);
      SDL_RenderLine(controlRenderer_, ccx, mi.y, ccx, mi.y + mi.h);
      // Warp toolbar — horizontal strip below the monitor content
      {
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
        int toolY = mi.y + mi.h - 30;
        int toolH = 26;
        // Dark backdrop for the toolbar
        SDL_Rect toolbarBg {mi.x, toolY - 2, mi.w, toolH + 4};
        Primitives::fillRect(controlRenderer_, toolbarBg, SDL_Color{15, 15, 15, 180});

        SDL_Color warpBtnFill {40, 40, 40, 200};
        SDL_Color warpBtnEdge {255, 220, 0, 200};
        SDL_Color warpBtnInk {255, 220, 0, 255};
        SDL_Color warpBtnDim {160, 150, 96, 255};

        std::string modeLabel = focusedDeck().warpMode == "perspective" ? "PERSP" : "LINEAR";
        int bx = mi.x + 4;
        warpModeBtnRect_ = {bx, toolY, 64, toolH};
        Primitives::fillRect(controlRenderer_, warpModeBtnRect_, warpBtnFill);
        Primitives::strokeRect(controlRenderer_, warpModeBtnRect_, warpBtnEdge);
        drawCenteredText(controlRenderer_, fontSmall_, modeLabel, warpBtnInk, warpModeBtnRect_);
        bx += 68;

        warpResetBtnRect_ = {bx, toolY, 56, toolH};
        Primitives::fillRect(controlRenderer_, warpResetBtnRect_, warpBtnFill);
        Primitives::strokeRect(controlRenderer_, warpResetBtnRect_, warpBtnEdge);
        drawCenteredText(controlRenderer_, fontSmall_, "RESET", warpBtnInk, warpResetBtnRect_);
        bx += 60;

        warpSaveBtnRect_ = {bx, toolY, 50, toolH};
        Primitives::fillRect(controlRenderer_, warpSaveBtnRect_, warpBtnFill);
        Primitives::strokeRect(controlRenderer_, warpSaveBtnRect_, warpBtnEdge);
        drawCenteredText(controlRenderer_, fontSmall_, "SAVE", warpBtnInk, warpSaveBtnRect_);
        bx += 54;

        warpCopyBtnRect_ = {bx, toolY, 50, toolH};
        Primitives::fillRect(controlRenderer_, warpCopyBtnRect_, warpBtnFill);
        Primitives::strokeRect(controlRenderer_, warpCopyBtnRect_, warpBtnEdge);
        drawCenteredText(controlRenderer_, fontSmall_, "COPY", warpBtnInk, warpCopyBtnRect_);
        bx += 54;

        SDL_Color pasteInk = warpSettingsClipboard_ ? warpBtnInk : warpBtnDim;
        warpPasteBtnRect_ = {bx, toolY, 56, toolH};
        Primitives::fillRect(controlRenderer_, warpPasteBtnRect_, warpBtnFill);
        Primitives::strokeRect(controlRenderer_, warpPasteBtnRect_, warpBtnEdge);
        drawCenteredText(controlRenderer_, fontSmall_, "PASTE", pasteInk, warpPasteBtnRect_);
        bx += 60;

        if (!warpPresets_.empty()) {
          warpRecallBtnRect_ = {bx, toolY, 56, toolH};
          Primitives::fillRect(controlRenderer_, warpRecallBtnRect_, warpBtnFill);
          Primitives::strokeRect(controlRenderer_, warpRecallBtnRect_, warpBtnEdge);
          std::string recallLabel = "P" + std::to_string(warpPresets_.size());
          drawCenteredText(controlRenderer_, fontSmall_, recallLabel, warpBtnInk, warpRecallBtnRect_);
        } else {
          warpRecallBtnRect_ = {};
        }

        // Shift hint on the right side
        drawText(controlRenderer_, fontSmall_, "Shift: snap",
                 SDL_Color{255, 220, 0, 100},
                 mi.x + mi.w - 100, toolY + 4);

        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
      }
    }
    if (keyColorPickerArmed_ && warpMonitorInner_.w > 0 && warpMonitorInner_.h > 0) {
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      Primitives::strokeRect(controlRenderer_, warpMonitorInner_, SDL_Color {255, 220, 0, 220});
      SDL_Rect hintRect {warpMonitorInner_.x + 10, warpMonitorInner_.y + 10,
                         std::max(0, warpMonitorInner_.w - 20), 22};
      Primitives::fillRect(controlRenderer_, hintRect, SDL_Color {18, 24, 18, 200});
      drawCenteredTextSafe(controlRenderer_, fontSmall_, hintRect,
                           "CLICK TO SAMPLE KEY COLOR", pal.light);
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    }


    if (liveDeleteWarnActive) {
      SDL_Rect warnRect {x, deleteWarnY, innerW, kDeleteWarnH};
      SDL_Color warnFill {176, 116, 18, 255};
      SDL_Color warnBorder {44, 26, 0, 255};
      SDL_Color warnInk {20, 12, 0, 255};
      drawUIPanel(warnRect, warnFill, warnBorder, pal.light);
      double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_) / 120.0);
      SDL_Color glow {255, 222, 140, static_cast<Uint8>(60 + pulse * 90.0)};
      Primitives::strokeRect(controlRenderer_, insetRect(warnRect, 1), glow);
      SDL_Rect warnMsgRect {warnRect.x + 12, warnRect.y + 4, warnRect.w - 24, warnRect.h - 8};
      drawCenteredTextSafe(controlRenderer_, fontBase_, warnMsgRect,
                           pendingLiveDeleteConfirmMessage_, warnInk);
    }

    {
      constexpr int kTBtnH = kTransportRowH;
      constexpr int kTBtnGap = 4;
      int btnY = transportRowY;
      int btnX = x;
      auto drawTCtrl = [&](const std::string& label, int width, QuickAction action,
                           const std::string& tip, UiImageAsset* icon = nullptr) {
        SDL_Rect btn {btnX, btnY, width, kTBtnH};
        drawUIPanel(btn, pal.light, pal.deep, pal.mid);
        if (icon && icon->texture) {
          int sz = std::min(16, std::min(btn.w - 6, btn.h - 6));
          SDL_Rect ir {btn.x + (btn.w - sz) / 2, btn.y + (btn.h - sz) / 2, sz, sz};
          drawUiImageContain(*icon, ir, 255, pal.deep);
        } else {
          TTF_Font* btnFont = (label == "-30" || label == "-20" || label == "-10") ? fontMono_ : fontSmall_;
          drawCenteredTextSafe(controlRenderer_, btnFont, btn, label, pal.deep);
        }
        quickButtons_.push_back({btn, action, tip});
        btnX += width + kTBtnGap;
      };
      drawTCtrl("|<", 36, QuickAction::TransportSkipStart, "Home — skip to start", &uiBtnRerack_);
      drawTCtrl("<<", 42, QuickAction::TransportSkipBack, "Left — skip back 10s");
      bool transportPlaying = false;
      if (MediaEngine* eng = focusedMediaEngine()) {
        transportPlaying = (eng->state() == TransportState::Playing);
      }
      // Reflect state: show a pause glyph/icon while playing, play while paused,
      // so the button's meaning is unambiguous.
      drawTCtrl(transportPlaying ? "||" : "\xe2\x96\xba", 52, QuickAction::TransportPlayPause,
                transportPlaying ? "Space — pause" : "Space — play",
                transportPlaying ? &uiBtnPause_ : &uiBtnPlay_);
      drawTCtrl(">>", 42, QuickAction::TransportSkipForward, "Right — skip forward 10s");
      drawTCtrl("-30", 46, QuickAction::GotoMinus30, "-30 seconds from end");
      drawTCtrl("-20", 46, QuickAction::GotoMinus20, "-20 seconds from end");
      drawTCtrl("-10", 46, QuickAction::GotoMinus10, "-10 seconds from end");
      if (activeCue && timelineCue == activeCue &&
          (activeCue->inPointSeconds > 0.001 ||
           (activeCue->outPointSeconds > 0.001 && activeCue->outPointSeconds < timelineDuration - 0.01))) {
        drawTCtrl("RESET", 56, QuickAction::TrimReset, "Clear in/out trim points");
      }
      SDL_Rect noteRect {btnX + 8, btnY, std::max(0, innerW - (btnX - x) - 8), kTBtnH};
      std::string note = timelineCue == activeCue
        ? "live lane  |  I/O set trim  |  click or drag to seek"
        : "selected cue lane  |  TAKE to make it live";
      drawTextSafe(controlRenderer_, fontSmall_, noteRect, note, pal.fg);
    }

    // --- Cue Inspector panel (with thumbnail at top) ---
    SDL_Rect ctrl = inspectorBody;
    // Fill the inspector body with shell_inner so the bare row labels (drawn
    // in pal.deep) have the legible fill the palette.hpp contract assumes for
    // dark ink. Without this the labels fall on shell_outer (the case color),
    // which is near-black in most themes → black-on-black. shell_inner is the
    // fill the audit already verifies dark/deep/ink_soft against.
    Primitives::fillRect(controlRenderer_, inspectorBody, pal.shellInner);
    int kCtrlW = ctrl.w;
    constexpr int kInspectorInset = 14;
    constexpr int kInspectorHeaderGap = 18;
    constexpr int kInspectorSectionGap = 12;
    constexpr int kInspectorRowH = 36;
    constexpr int kInspectorRowStep = 48;
    constexpr int kInspectorSectionHeaderH = 32;

    // Thumbnail of selected cue (top portion)
    constexpr int kThumbAreaH = 110;
    SDL_Rect thumbArea {ctrl.x + kInspectorInset, ctrl.y + 4, kCtrlW - kInspectorInset * 2, kThumbAreaH};
    Primitives::drawFramedPanel(controlRenderer_, thumbArea, pal.deep, pal.deep, pal.dark);
    if (selectedCue && selectedCue->kind == CueKind::Audio) {
      // Audio cue: fill entire thumb area with waveform
      bool pending = false;
      WaveformPeaks peaks = getWaveformPeaks(resolvedCueFilesystemPathString(*selectedCue, currentProjectFile_), pending);
      double dur = selectedCue->duration > 0.0 ? selectedCue->duration : 1.0;
      float inFrac  = static_cast<float>(selectedCue->inPointSeconds / dur);
      float outFrac = selectedCue->outPointSeconds > 0.0
                    ? static_cast<float>(selectedCue->outPointSeconds / dur) : 1.0f;
      float playFrac = -1.0f;
      if (const MediaEngine* eng = focusedMediaEngine())
        playFrac = static_cast<float>(std::clamp(eng->position() / dur, 0.0, 1.0));
      drawWaveform(controlRenderer_, thumbArea, peaks, selectedCue->audioChannels >= 2, playFrac, inFrac, outFrac,
                   selectedCue->pausePoints, dur);
      drawAudioFadeEnvelope(thumbArea, *selectedCue);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {thumbArea.x + 6, thumbArea.y + 4, thumbArea.w - 12, 20},
                   selectedCue->name, pal.mid);
    } else if (selectedThumbnailTex_) {
      float aspect = static_cast<float>(selectedThumbnailTexW_) / static_cast<float>(selectedThumbnailTexH_);
      int drawW = thumbArea.w - 4;
      int drawH = static_cast<int>(drawW / aspect);
      if (drawH > thumbArea.h - 4) {
        drawH = thumbArea.h - 4;
        drawW = static_cast<int>(drawH * aspect);
      }
      SDL_Rect dst {thumbArea.x + (thumbArea.w - drawW) / 2, thumbArea.y + (thumbArea.h - drawH) / 2, drawW, drawH};
      SDL_SetTextureBlendMode(selectedThumbnailTex_, SDL_BLENDMODE_NONE);
      SDL_RenderTexture(controlRenderer_, selectedThumbnailTex_, nullptr, &dst);
    } else if (selectedCue) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {thumbArea.x + 6, thumbArea.y + 8, thumbArea.w - 12, 20},
                   selectedCue->name, pal.mid);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {thumbArea.x + 6, thumbArea.y + 30, thumbArea.w - 12, 20},
                   "loading preview...", pal.mid);
    } else {
      SDL_SetRenderClipRect(controlRenderer_, &thumbArea);
      int tw = thumbArea.w - 16;
      drawCenteredTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {thumbArea.x + 8, thumbArea.y + thumbArea.h / 2 - 20, tw, 20},
                   ellipsizeToPixelWidth(fontSmall_, "No cue selected", tw), pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {thumbArea.x + 8, thumbArea.y + thumbArea.h / 2, tw, 20},
                   ellipsizeToPixelWidth(fontSmall_, "Drop media here", tw), pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {thumbArea.x + 8, thumbArea.y + thumbArea.h / 2 + 20, tw, 20},
                   ellipsizeToPixelWidth(fontSmall_, "Press A to take cue", tw), pal.mid);
      SDL_SetRenderClipRect(controlRenderer_, nullptr);
    }

    // Waveform strip at bottom of thumb area (for video cues with audio — Audio cues get full thumb above)
    if (selectedCue && selectedCue->hasAudio && selectedCue->kind != CueKind::Audio) {
      bool pending = false;
      WaveformPeaks peaks = getWaveformPeaks(resolvedCueFilesystemPathString(*selectedCue, currentProjectFile_), pending);
      SDL_Rect waveRect {thumbArea.x + 2, thumbArea.y + thumbArea.h - 34, thumbArea.w - 4, 32};
      double dur = selectedCue->duration > 0.0 ? selectedCue->duration : 1.0;
      float inFrac  = static_cast<float>(selectedCue->inPointSeconds / dur);
      float outFrac = selectedCue->outPointSeconds > 0.0
                    ? static_cast<float>(selectedCue->outPointSeconds / dur) : 1.0f;
      float playFrac = -1.0f;
      if (const MediaEngine* eng = focusedMediaEngine())
        playFrac = static_cast<float>(eng->position() / dur);
      if (!peaks.empty() || pending) {
        drawWaveform(controlRenderer_, waveRect, peaks, selectedCue->audioChannels >= 2, playFrac, inFrac, outFrac,
                     selectedCue->pausePoints, dur);
        drawAudioFadeEnvelope(waveRect, *selectedCue);
      }
    }

    auto cueSummaryDurationLabel = [&](const Cue& cue) {
      if (cue.kind == CueKind::Video || cue.kind == CueKind::Audio) {
        return cue.duration > 0.0 ? formatSeconds(cue.duration) : std::string("--:--");
      }
      if (cue.stillDurationSeconds > 0.0) {
        return formatSeconds(cue.stillDurationSeconds);
      }
      return std::string("hold");
    };

    auto cueSummarySourceLine = [&](const Cue& cue) {
      if (cue.kind == CueKind::Browser) {
        return cue.path.empty() ? std::string("(unset url)") : cue.path;
      }
      if (cue.kind == CueKind::Composite) {
        return std::string("Scene  ") + compositeLayoutPresetLabel(cue.compositeLayoutPreset)
          + "  " + std::to_string(cue.compositeSlots.size()) + " slots";
      }
      if (cue.kind == CueKind::Pip) {
        return std::string("PIP  ") + pipSourceDisplayLabel(cue);
      }
      if (isSourceCueKind(cue.kind)) {
        std::string sourceRef = sourceCueRefFromCue(cue);
        if (sourceRef.empty()) {
          sourceRef = defaultSourceRefForKind(cue.kind);
        }
        return std::string("Source  ") + sourceCueRefFriendlyLabel(cue.kind, sourceRef);
      }
      if (cue.kind == CueKind::Pattern) {
        return std::string("Pattern  ") + patternLabelForType(normalizePatternTypeId(cue.path));
      }
      if (cue.path.empty()) {
        return std::string("(no media source)");
      }
      return cue.path;
    };

    auto cueSummaryDetailLine = [&](const Cue& cue) {
      if (cue.kind == CueKind::Composite) {
        if (!cue.compositeAudioSlotId.empty()) {
          for (const CompositeSlot& slot : cue.compositeSlots) {
            if (slot.id == cue.compositeAudioSlotId) {
              return std::string("Audio  ") + slot.name + " · " + compositeSourceDisplayLabel(slot);
            }
          }
        }
        return cue.notes.empty() ? std::string("Use SCENE presets and slot sources in the inspector") : cue.notes;
      }
      if (cue.kind == CueKind::Pip) {
        return std::string("Source  ") + pipSourceTypeLabel(pipSourceTypeTokenFromCue(cue));
      }
      if (cue.kind == CueKind::Video || cue.kind == CueKind::Audio) {
        double outPoint = cue.outPointSeconds > 0.001 ? cue.outPointSeconds : cue.duration;
        return "In " + formatSeconds(cue.inPointSeconds) + "  Out " + formatSeconds(std::max(0.0, outPoint));
      }
      if (!cue.notes.empty()) {
        return cue.notes;
      }
      return std::string();
    };

    auto cueSummaryTechLine = [&](const Cue& cue) {
      std::vector<std::string> parts;
      if ((cue.kind == CueKind::Video || cue.kind == CueKind::Audio) &&
          std::isfinite(cue.fps) && cue.fps > 1.0) {
        char fpsBuf[32];
        if (std::fabs(cue.fps - std::round(cue.fps)) < 0.01) {
          snprintf(fpsBuf, sizeof(fpsBuf), "%d", static_cast<int>(std::lround(cue.fps)));
        } else if (std::fabs(cue.fps - 29.97) < 0.01) {
          snprintf(fpsBuf, sizeof(fpsBuf), "29.97");
        } else {
          snprintf(fpsBuf, sizeof(fpsBuf), cue.fps < 10.0 ? "%.2f" : "%.1f", cue.fps);
        }
        parts.push_back(std::string(fpsBuf) + " fps");
      }
      std::string codecPart;
      if (!cue.videoCodec.empty()) {
        codecPart = cue.videoCodec;
      }
      if (!cue.audioCodec.empty()) {
        codecPart += codecPart.empty() ? cue.audioCodec : " / " + cue.audioCodec;
      }
      if (codecPart.empty() && !cue.formatName.empty()) {
        codecPart = cue.formatName;
      } else if (!cue.formatName.empty()) {
        codecPart += "  " + cue.formatName;
      }
      if (!codecPart.empty()) {
        parts.push_back(codecPart);
      }
      if (cue.audioChannels > 0 || cue.audioSampleRate > 0) {
        std::string audioPart;
        if (cue.audioChannels == 1) {
          audioPart = "mono";
        } else if (cue.audioChannels == 2) {
          audioPart = "stereo";
        } else if (cue.audioChannels > 2) {
          audioPart = std::to_string(cue.audioChannels) + "ch";
        }
        if (cue.audioSampleRate > 0) {
          char rateBuf[32];
          if (cue.audioSampleRate % 1000 != 0) {
            snprintf(rateBuf, sizeof(rateBuf), "%d.%03dk", cue.audioSampleRate / 1000, cue.audioSampleRate % 1000);
          } else {
            snprintf(rateBuf, sizeof(rateBuf), "%dk", cue.audioSampleRate / 1000);
          }
          audioPart += audioPart.empty() ? "" : "  ";
          audioPart += rateBuf;
        }
        if (!audioPart.empty()) {
          parts.push_back(audioPart);
        }
      }
      if (parts.empty()) {
        return std::string();
      }
      std::string line;
      for (size_t i = 0; i < parts.size(); ++i) {
        if (i) {
          line += "  ";
        }
        line += parts[i];
      }
      return line;
    };

    int ctrlSettingsY = ctrl.y + kThumbAreaH + kInspectorHeaderGap;
    if (selectedCue) {
      // kCueSummaryH sized to hold up to 5 text rows (name + meta + source + tech + detail)
      // with heights large enough to contain fontBase_/fontSmall_ without bottom-clipping.
      constexpr int kCueSummaryH = 180;
      constexpr int kSummaryBtnW = 60;
      constexpr int kSummaryBtnGap = 6;
      constexpr int kSummaryPad = 6;   // inner padding inside the summary panel
      SDL_Rect summaryRect {ctrl.x + kInspectorInset, ctrlSettingsY, kCtrlW - kInspectorInset * 2, kCueSummaryH};
      drawUIPanel(summaryRect, pal.light, pal.deep, pal.mid);

      auto convReason = cueConvertReason(*selectedCue);
      bool cueConverting = isCueConverting(selectedCue->path);
      bool showConvert = convReason.has_value() || cueConverting;
      int summaryBtnCount = 3 + (showConvert ? 1 : 0);
      int summaryBtnX = summaryRect.x + summaryRect.w - kSummaryPad
                        - (kSummaryBtnW * summaryBtnCount + kSummaryBtnGap * (summaryBtnCount - 1));
      SDL_Rect copyRect {summaryBtnX, summaryRect.y + 4, kSummaryBtnW, 26};
      SDL_Rect pasteRect {copyRect.x + copyRect.w + kSummaryBtnGap, summaryRect.y + 4, kSummaryBtnW, 26};
      SDL_Rect resetRect {pasteRect.x + pasteRect.w + kSummaryBtnGap, summaryRect.y + 4, kSummaryBtnW, 26};
      SDL_Rect convertRect {resetRect.x + resetRect.w + kSummaryBtnGap, summaryRect.y + 4, kSummaryBtnW, 26};
      int labelAvailW = std::max(0, copyRect.x - summaryRect.x - kSummaryPad - 8);
      SDL_Rect labelRect {summaryRect.x + kSummaryPad, summaryRect.y + 6, labelAvailW, 22};
      drawTextSafe(controlRenderer_, fontSmall_, labelRect, "SELECTED CUE", pal.inkSoft);
      drawUIPanel(copyRect, pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, copyRect, "COPY", pal.deep);
      SDL_Color pasteFill = cueSettingsClipboard_ ? pal.dark : pal.mid;
      SDL_Color pasteInk = cueSettingsClipboard_ ? pal.light : pal.inkSoft;
      drawUIPanel(pasteRect, pasteFill, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, pasteRect, "PASTE", pasteInk);
      drawUIPanel(resetRect, pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, resetRect, "RESET", pal.deep);
      quickButtons_.push_back({copyRect, QuickAction::CopyCueSettings, "Copy inspector settings from the selected cue"});
      quickButtons_.push_back({pasteRect, QuickAction::PasteCueSettings, "Paste copied settings to the current cue selection"});
      quickButtons_.push_back({resetRect, QuickAction::ResetCueSettings, "Reset all inspector settings on the cue selection to defaults"});
      if (showConvert) {
        // Contextual: only shows when Deckboy can't play the cue (or would play
        // it poorly), or while a conversion is running.
        drawUIPanel(convertRect, cueConverting ? pal.mid : pal.dark, pal.deep, pal.light);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, convertRect,
                             cueConverting ? "..." : "CONVERT", cueConverting ? pal.inkSoft : pal.light);
        if (!cueConverting) {
          quickButtons_.push_back({convertRect, QuickAction::ConvertCueMedia,
            std::string("May not play well") + (convReason ? (" (" + *convReason + ")") : std::string()) +
            " - convert to a compatible copy"});
        }
      }

      // Name row — h=28 to contain fontBase_ without bottom clip; width stops before copy button
      SDL_Rect nameRect {summaryRect.x + kSummaryPad, summaryRect.y + 34, labelAvailW, 28};
      drawTextSafe(controlRenderer_, fontBase_, nameRect,
                   ellipsizeToPixelWidth(fontBase_, selectedCue->name, nameRect.w),
                   pal.deep);

      int contentW = summaryRect.w - kSummaryPad * 2;
      std::string metaLine = cueDisplayToken(*selectedCue, focusedDeck().selectedIndex) + "  " +
                             cueKindLabel(selectedCue->kind);
      if (selectedCue->width > 0 && selectedCue->height > 0) {
        metaLine += "  " + std::to_string(selectedCue->width) + "x" + std::to_string(selectedCue->height);
      }
      metaLine += "  " + cueSummaryDurationLabel(*selectedCue);
      SDL_Rect metaRect {summaryRect.x + kSummaryPad, summaryRect.y + 66, contentW, 24};
      drawTextSafe(controlRenderer_, fontSmall_, metaRect,
                   ellipsizeToPixelWidth(fontSmall_, metaLine, contentW),
                   pal.dark);

      SDL_Rect sourceRect {summaryRect.x + kSummaryPad, summaryRect.y + 94, contentW, 24};
      drawTextSafe(controlRenderer_, fontSmall_, sourceRect,
                   ellipsizeToPixelWidth(fontSmall_, cueSummarySourceLine(*selectedCue), contentW),
                   pal.dark);

      std::string techLine = cueSummaryTechLine(*selectedCue);
      if (!techLine.empty()) {
        SDL_Rect techRect {summaryRect.x + kSummaryPad, summaryRect.y + 122, contentW, 24};
        drawTextSafe(controlRenderer_, fontSmall_, techRect,
                     ellipsizeToPixelWidth(fontSmall_, techLine, contentW),
                     pal.dark);
      }

      std::string detailLine = cueSummaryDetailLine(*selectedCue);
      if (!detailLine.empty()) {
        SDL_Rect detailRect {summaryRect.x + kSummaryPad, summaryRect.y + 150, contentW, 24};
        drawTextSafe(controlRenderer_, fontSmall_, detailRect,
                     ellipsizeToPixelWidth(fontSmall_, detailLine, contentW),
                     pal.inkSoft);
      }
      ctrlSettingsY = summaryRect.y + summaryRect.h + 10;
    } else {
      SDL_Rect summaryRect {ctrl.x + kInspectorInset, ctrlSettingsY, kCtrlW - kInspectorInset * 2, 120};
      drawUIPanel(summaryRect, pal.light, pal.deep, pal.mid);
      int sw = summaryRect.w - 12;
      SDL_Rect labelRect {summaryRect.x + 6, summaryRect.y + 6,  sw, 22};
      SDL_Rect titleRect {summaryRect.x + 6, summaryRect.y + 32, sw, 24};
      SDL_Rect bodyRectA {summaryRect.x + 6, summaryRect.y + 60, sw, 22};
      SDL_Rect bodyRectB {summaryRect.x + 6, summaryRect.y + 86, sw, 22};
      drawTextSafe(controlRenderer_, fontSmall_, labelRect, "SELECTED CUE", pal.inkSoft);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, titleRect, "NO CUE SELECTED", pal.deep);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, bodyRectA,
                           "Choose a cue to inspect it here", pal.dark);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, bodyRectB,
                           "Import or click a cue in the playlist", pal.inkSoft);
      ctrlSettingsY = summaryRect.y + summaryRect.h + 10;
    }

    // -- Shared inspector context for floating panel --
    InspectorCtx ix {ctrl, kCtrlW, kInspectorInset, kInspectorRowH, kInspectorRowStep,
                     kInspectorSectionHeaderH, kInspectorSectionGap, fontBase_, fontBase_, fontSmall_, true};

    auto drawQuickRow = [&](int rowY, const std::string& label, QuickAction decAction, const std::string& value,
                            QuickAction incAction, QuickAction toggleAction = QuickAction::ToggleLoop,
                            bool isToggle = false, bool toggleOn = false, std::string tip = "",
                            bool valueEditable = false, QuickAction valueAction = QuickAction::ToggleLoop) {
      inspDrawQuickRow(ix, rowY, label, decAction, value, incAction, toggleAction, isToggle, toggleOn, tip, valueEditable, valueAction);
    };

    auto drawInspectorMessageRow = [&](int rowY, const std::string& text,
                                       SDL_Color fill = pal.light, SDL_Color ink = pal.deep) {
      return inspDrawMessageRow(ix, rowY, text, fill, ink);
    };

    auto drawInspectorActionRow = [&](int rowY, const std::string& label, QuickAction action, const std::string& tip,
                                      SDL_Color fill = pal.light, SDL_Color ink = pal.deep) {
      return inspDrawActionRow(ix, rowY, label, action, tip, fill, ink);
    };

    auto drawInspectorEditableRow = [&](int rowY, const std::string& label, const std::string& value,
                                        QuickAction action, const std::string& tip,
                                        SDL_Color valueColor = pal.deep) {
      return inspDrawEditableRow(ix, rowY, label, value, action, tip, valueColor);
    };

    auto drawInspectorStatusRow = [&](int rowY, const std::string& label, const std::string& value, bool warning) {
      return inspDrawStatusRow(ix, rowY, label, value, warning);
    };

    auto drawCueTagRow = [&](int rowY, const Cue& cue, const std::string& tip) {
      SDL_Rect tagBtn {ix.ctrl.x + ix.inset, rowY, ix.ctrlW - ix.inset * 2, ix.rowH};
      std::string tagStr = cue.colorTag.empty() ? "none" : cue.colorTag;
      SDL_Color tagFill = colorTagToSdl(cue.colorTag, 200);
      drawUIPanel(tagBtn, tagFill, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, ix.valueFont, tagBtn, "tag: " + tagStr + "  [K cycle]", pal.light);
      quickButtons_.push_back({tagBtn, QuickAction::CycleColorTag, tip});
      return rowY + ix.rowStep;
    };

    auto drawPausePointsRow = [&](int rowY, int pointCount) {
      SDL_Rect tableRect {ix.ctrl.x + ix.inset, rowY, ix.ctrlW - ix.inset * 2, ix.rowH};
      int infoW = std::max(80, tableRect.w - 90 - 50 - 16);
      UITable table(tableRect, {90, 50, infoW}, ix.rowH, 8);
      SDL_Rect addBtn = table.cell(0, 0);
      SDL_Rect clearBtn = table.cell(0, 1);
      SDL_Rect infoRect = table.cell(0, 2);
      drawUIPanel(addBtn, pal.dark, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, addBtn, "+pause pt", pal.light);
      drawUIPanel(clearBtn, pal.deleteBezel, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, clearBtn, "clr", pal.light);
      drawUIPanel(infoRect, pal.light, pal.deep, pal.mid);
      drawTextSafe(controlRenderer_, ix.valueFont, infoRect,
                   "pause points: " + std::to_string(pointCount), pal.inkSoft);
      quickButtons_.push_back({addBtn, QuickAction::AddPausePoint, "Add auto-pause at current playback position"});
      quickButtons_.push_back({clearBtn, QuickAction::ClearPausePoints, "Clear all pause points"});
      return rowY + ix.rowStep;
    };

    auto formatCueStorage = [](std::uintmax_t bytes) {
      if (bytes == 0) {
        return std::string("unknown");
      }
      static const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
      double value = static_cast<double>(bytes);
      size_t unitIndex = 0;
      while (value >= 1024.0 && unitIndex + 1 < std::size(kUnits)) {
        value /= 1024.0;
        ++unitIndex;
      }
      char sizeBuf[32];
      if (value < 10.0 && unitIndex > 0) {
        snprintf(sizeBuf, sizeof(sizeBuf), "%.1f %s", value, kUnits[unitIndex]);
      } else {
        snprintf(sizeBuf, sizeof(sizeBuf), "%.0f %s", value, kUnits[unitIndex]);
      }
      return std::string(sizeBuf);
    };

    auto cueTimelineDurationLabel = [&](const Cue& cue) {
      if (cue.kind == CueKind::Video || cue.kind == CueKind::Audio) {
        return cue.duration > 0.0 ? formatSeconds(cue.duration) : std::string("--:--");
      }
      if (cue.stillDurationSeconds > 0.0) {
        return formatSeconds(cue.stillDurationSeconds);
      }
      return std::string("hold");
    };

    auto cueTrimSummary = [&](const Cue& cue) {
      if (cue.kind != CueKind::Video && cue.kind != CueKind::Audio &&
          cue.inPointSeconds <= 0.001 && cue.outPointSeconds <= 0.001) {
        return std::string();
      }
      double outPoint = cue.outPointSeconds > 0.001 ? cue.outPointSeconds : cue.duration;
      if (outPoint <= 0.001 && cue.stillDurationSeconds > 0.0) {
        outPoint = cue.stillDurationSeconds;
      }
      return "In " + formatSeconds(cue.inPointSeconds) + "  Out " + formatSeconds(std::max(0.0, outPoint));
    };

    auto drawCueTechnicalRows = [&](int rowY, const Cue& cue) {
      bool showSource = (cue.kind == CueKind::Video || cue.kind == CueKind::Audio || cue.kind == CueKind::Image) &&
                        !cue.path.empty();
      if (showSource) {
        rowY = drawInspectorStatusRow(rowY, "source", cue.path, false);
      }

      std::string mediaSummary = cueKindLabel(cue.kind);
      if (cue.width > 0 && cue.height > 0) {
        mediaSummary += "  " + std::to_string(cue.width) + "x" + std::to_string(cue.height);
      }
      mediaSummary += "  " + cueTimelineDurationLabel(cue);
      rowY = drawInspectorStatusRow(rowY, "media", mediaSummary, false);

      std::string codecSummary;
      if (!cue.formatName.empty()) {
        codecSummary += cue.formatName;
      }
      if (!cue.videoCodec.empty()) {
        if (!codecSummary.empty()) {
          codecSummary += " / ";
        }
        codecSummary += cue.videoCodec;
      }
      if (cue.hasAudio || !cue.audioCodec.empty()) {
        if (!codecSummary.empty()) {
          codecSummary += " / ";
        }
        codecSummary += cue.audioCodec.empty() ? "audio" : cue.audioCodec;
      }
      if (!codecSummary.empty()) {
        rowY = drawInspectorStatusRow(rowY, "codec", codecSummary, false);
      }

      std::string trimSummary = cueTrimSummary(cue);
      if (!trimSummary.empty()) {
        rowY = drawInspectorStatusRow(rowY, "trim", trimSummary, false);
      }

      if (cue.sizeBytes > 0) {
        rowY = drawInspectorStatusRow(rowY, "size", formatCueStorage(cue.sizeBytes), false);
      }
      return rowY;
    };

    int settingsContentTopY = ctrlSettingsY + 22;
    int settingsContentBottomY = ctrl.y + ctrl.h - 10;
    cueSettingsViewportRect_ = {
      ctrl.x + kInspectorInset,
      settingsContentTopY - 2,
      kCtrlW - kInspectorInset * 2,
      std::max(0, settingsContentBottomY - settingsContentTopY + 2)
    };
    cueSettingsScroll_ = std::clamp(cueSettingsScroll_, 0, cueSettingsScrollMax_);
    cueSettingsQuickButtonStartIndex_ = quickButtons_.size();
    cueSettingsScrubZoneStartIndex_ = valueScrubZones_.size();
    SDL_SetRenderClipRect(controlRenderer_,
      cueSettingsViewportRect_.h > 0 ? &cueSettingsViewportRect_ : nullptr);

    auto formatFloat = [](float v, int d = 2) { return fmtFloat(v, d); };
    auto formatPercent = [](float v) { return fmtPercent(v); };
    auto formatScaleMode = [](ScaleMode m) { return fmtScaleMode(m); };
    auto fitInspectorText = [&](TTF_Font* font, const std::string& text, int width) {
      return ellipsizeToPixelWidth(font ? font : fontSmall_, text, std::max(0, width));
    };
    auto drawKeyColorRow = [&](int rowY, const Cue& cue) { inspDrawKeyColorRow(ix, rowY, cue); };
    auto drawGeometryRows = [&](int startY, const Cue& cue, bool includeScaleOffset) {
      return inspDrawGeometryRows(ix, startY, cue, includeScaleOffset);
    };
    auto drawColorRows = [&](int startY, const Cue& cue) { return inspDrawColorRows(ix, startY, cue); };
    auto drawKeyRows = [&](int startY, const Cue& cue) { return inspDrawKeyRows(ix, startY, cue); };
    auto beginInspectorSection = [&](int rowY, const std::string& title, bool open,
                                     QuickAction toggleAction, const std::string& tip) {
      return inspBeginSection(ix, rowY, title, open, toggleAction, tip);
    };
    auto finishInspectorSection = [&](const InspectorSectionScope& section, int bodyBottom) {
      inspFinishSection(section, bodyBottom);
    };


    std::vector<int> panelSelectedIndices = selectedCue ? selectedCueIndices(deck) : std::vector<int> {};
    std::vector<const Cue*> panelSelectedCues;
    panelSelectedCues.reserve(panelSelectedIndices.size());
    for (int index : panelSelectedIndices) {
      if (index >= 0 && index < static_cast<int>(deck.cues.size())) {
        panelSelectedCues.push_back(&deck.cues[index]);
      }
    }
    bool panelMultiSelection = panelSelectedCues.size() > 1;

    auto allSelectedCues = [&](auto pred) {
      if (panelSelectedCues.empty()) {
        return false;
      }
      for (const Cue* cue : panelSelectedCues) {
        if (!pred(*cue)) {
          return false;
        }
      }
      return true;
    };

    auto boolMixedState = [&](auto getter) {
      bool first = getter(*panelSelectedCues.front());
      bool mixed = false;
      for (const Cue* cue : panelSelectedCues) {
        if (getter(*cue) != first) {
          mixed = true;
          break;
        }
      }
      return std::pair<bool, bool> {mixed, first};
    };

    auto doubleMixedLabel = [&](auto getter, int decimals, const std::string& suffix = std::string()) {
      double first = getter(*panelSelectedCues.front());
      bool mixed = false;
      for (const Cue* cue : panelSelectedCues) {
        if (std::abs(getter(*cue) - first) > 0.0001) {
          mixed = true;
          break;
        }
      }
      if (mixed) {
        return std::string("mixed");
      }
      return fmtFloat(static_cast<float>(first), decimals) + suffix;
    };

    auto intMixedLabel = [&](auto getter, const std::string& suffix = std::string()) {
      int first = getter(*panelSelectedCues.front());
      bool mixed = false;
      for (const Cue* cue : panelSelectedCues) {
        if (getter(*cue) != first) {
          mixed = true;
          break;
        }
      }
      if (mixed) {
        return std::string("mixed");
      }
      return std::to_string(first) + suffix;
    };

    auto stringMixedLabel = [&](auto getter, const std::string& emptyToken = std::string("none")) {
      std::string first = getter(*panelSelectedCues.front());
      bool mixed = false;
      for (const Cue* cue : panelSelectedCues) {
        if (getter(*cue) != first) {
          mixed = true;
          break;
        }
      }
      if (mixed) {
        return std::string("mixed");
      }
      return first.empty() ? emptyToken : first;
    };

    if (panelMultiSelection) {
      int ry = ctrlSettingsY + 22 - cueSettingsScroll_;
      constexpr int kRowStep = kInspectorRowStep;

      std::vector<std::string> kindLabels;
      for (const Cue* cue : panelSelectedCues) {
        std::string label = cueKindLabel(cue->kind);
        if (std::find(kindLabels.begin(), kindLabels.end(), label) == kindLabels.end()) {
          kindLabels.push_back(label);
        }
      }
      std::string kindSummary;
      for (size_t i = 0; i < kindLabels.size(); ++i) {
        if (i > 0) {
          kindSummary += ", ";
        }
        kindSummary += kindLabels[i];
      }

      bool allVideoAudio = allSelectedCues([&](const Cue& cue) {
        return cue.kind == CueKind::Video || cue.kind == CueKind::Audio;
      });
      bool allStillLike = allSelectedCues([&](const Cue& cue) {
        return cue.kind != CueKind::Video && cue.kind != CueKind::Audio;
      });
      bool allSupportsGeometry = allSelectedCues([&](const Cue& cue) {
        return cueSupportsGeometry(&cue);
      });
      bool allSupportsKey = allSelectedCues([&](const Cue& cue) {
        return cueSupportsKeying(&cue);
      });
      bool allHasAudio = allSelectedCues([&](const Cue& cue) {
        return cue.hasAudio;
      });
      bool allLowerThird = allSelectedCues([&](const Cue& cue) {
        return cue.kind == CueKind::LowerThird;
      });

      auto playbackSection = beginInspectorSection(ry, "PLAYBACK", cueSectionPlaybackOpen_,
                                                   QuickAction::CueSectionPlaybackToggle,
                                                   "Common controls for selected cues");
      int playbackBodyY = playbackSection.bodyStartY;
      if (cueSectionPlaybackOpen_) {
        ry = playbackBodyY;
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {ctrl.x + 10, ry, kCtrlW - 20, kInspectorRowH},
                     fitInspectorText(fontSmall_,
                                      std::to_string(panelSelectedCues.size()) + " cues: " + kindSummary,
                                      kCtrlW - 24),
                     pal.inkSoft);
        ry += kRowStep;

        if (allVideoAudio) {
          drawQuickRow(ry, "fade in", QuickAction::FadeInDec,
                       stringMixedLabel([&](const Cue& cue) { return formatSeconds(cue.fadeInSeconds); }),
                       QuickAction::FadeInInc, QuickAction::ToggleLoop, false, false,
                       "Apply fade-in to selected video/audio cues");
          ry += kRowStep;
          drawQuickRow(ry, "fade out", QuickAction::FadeOutDec,
                       stringMixedLabel([&](const Cue& cue) { return formatSeconds(cue.fadeOutSeconds); }),
                       QuickAction::FadeOutInc, QuickAction::ToggleLoop, false, false,
                       "Apply fade-out to selected video/audio cues");
          ry += kRowStep;

          auto loopState = boolMixedState([&](const Cue& cue) { return cue.loop; });
          drawQuickRow(ry, "loop", QuickAction::ToggleLoop,
                       loopState.first ? "mixed" : (loopState.second ? "on" : "off"),
                       QuickAction::ToggleLoop, QuickAction::ToggleLoop, true, !loopState.first && loopState.second,
                       "Toggle loop for selected cues");
          ry += kRowStep;

          auto holdState = boolMixedState([&](const Cue& cue) { return cue.pauseOnLastFrame; });
          drawQuickRow(ry, "hold", QuickAction::ToggleHold,
                       holdState.first ? "mixed" : (holdState.second ? "on" : "off"),
                       QuickAction::ToggleHold, QuickAction::ToggleHold, true, !holdState.first && holdState.second,
                       "Toggle hold-at-end for selected cues");
          ry += kRowStep;

          bool mixedEnd = false;
          CueEndAction endAction = panelSelectedCues.front()->endAction;
          for (const Cue* cue : panelSelectedCues) {
            if (cue->endAction != endAction) {
              mixedEnd = true;
              break;
            }
          }
          SDL_Rect endBtn {ctrl.x + 10, ry, kCtrlW - 20, 30};
          Primitives::drawFramedPanel(controlRenderer_, endBtn, pal.light,
                                      pal.deep, pal.mid);
          std::string endLabel = "end: " + std::string(mixedEnd ? "mixed" : cueEndActionLabel(endAction)) + "  [X cycle]";
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {endBtn.x + 10, endBtn.y, endBtn.w - 20, endBtn.h},
                       fitInspectorText(fontSmall_, endLabel, endBtn.w - 24),
                       pal.deep);
          quickButtons_.push_back({endBtn, QuickAction::CycleEndAction, "Cycle end action for selected cues"});
          ry += kRowStep;

          drawQuickRow(ry, "repeats", QuickAction::LoopCountDec,
                       intMixedLabel([&](const Cue& cue) { return cue.loopCount; }, "x"),
                       QuickAction::LoopCountInc, QuickAction::ToggleLoop, false, false,
                       "Set repeat count for selected cues");
          ry += kRowStep;

          drawQuickRow(ry, "speed", QuickAction::SpeedDec,
                       doubleMixedLabel([&](const Cue& cue) { return cue.playbackSpeed; }, 2, "x"),
                       QuickAction::SpeedInc, QuickAction::ToggleLoop, false, false,
                       "Set playback speed for selected cues");
          ry += kRowStep;
        }

        if (allStillLike) {
          drawQuickRow(ry, "duration", QuickAction::DurDec,
                       stringMixedLabel([&](const Cue& cue) {
                         return cue.stillDurationSeconds > 0.0 ? formatSeconds(cue.stillDurationSeconds) : std::string("hold");
                       }),
                       QuickAction::DurInc, QuickAction::ToggleLoop, false, false,
                       "Set still/pattern/browser duration for selected cues");
          ry += kRowStep;
        }

        drawQuickRow(ry, "transition", QuickAction::TransDec,
                     stringMixedLabel([&](const Cue& cue) {
                       return cue.cueTransitionSeconds >= 0.0 ? formatSeconds(cue.cueTransitionSeconds) : std::string("deck");
                     }),
                     QuickAction::TransInc, QuickAction::ToggleLoop, false, false,
                     "Set per-cue transition duration");
        ry += kRowStep;
        // Transition style (own row)
        {
          int rx = ctrl.x + 10;
          int styleW = kCtrlW - 20;
          SDL_Rect styleBtn {rx, ry, styleW, kInspectorRowH};
          std::string styleLabel = stringMixedLabel([&](const Cue& cue) {
            return cue.cueTransitionStyle.empty() ? focusedDeck().transitionStyle : cue.cueTransitionStyle;
          }, "deck");
          styleLabel = "style: " + transitionStyleLabel(styleLabel);
          SDL_Color styleFill = pal.light;
          SDL_Color styleInk = pal.deep;
          Primitives::drawFramedPanel(controlRenderer_, styleBtn, styleFill, pal.deep, pal.mid);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {styleBtn.x + 6, styleBtn.y, styleBtn.w - 18, styleBtn.h},
                       fitInspectorText(fontSmall_, styleLabel, styleBtn.w - 22),
                       styleInk);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {styleBtn.x + styleBtn.w - 14, styleBtn.y, 14, styleBtn.h},
                       "\xe2\x96\xbc", styleInk);
          cueTransitionStyleDropdownRect_ = styleBtn;
        }
        ry += kRowStep;

        auto pauseBeginState = boolMixedState([&](const Cue& cue) { return cue.pauseAtBeginning; });
        drawQuickRow(ry, "pause begin", QuickAction::TogglePauseBegin,
                     pauseBeginState.first ? "mixed" : (pauseBeginState.second ? "on" : "off"),
                     QuickAction::TogglePauseBegin, QuickAction::TogglePauseBegin, true,
                     !pauseBeginState.first && pauseBeginState.second,
                     "Toggle pause-at-beginning for selected cues");
        ry += kRowStep;

        if (allHasAudio) {
          auto audioState = boolMixedState([&](const Cue& cue) { return cue.audioEnabled; });
          drawQuickRow(ry, "audio", QuickAction::ToggleCueAudio,
                       audioState.first ? "mixed" : (audioState.second ? "on" : "off"),
                       QuickAction::ToggleCueAudio, QuickAction::ToggleCueAudio, true,
                       !audioState.first && audioState.second,
                       "Toggle audio enable for selected cues");
          ry += kRowStep;
        }

        auto nextTransState = boolMixedState([&](const Cue& cue) { return cue.transitionToNext; });
        drawQuickRow(ry, "next xfade", QuickAction::ToggleNextTransition,
                     nextTransState.first ? "mixed" : (nextTransState.second ? "on" : "off"),
                     QuickAction::ToggleNextTransition, QuickAction::ToggleNextTransition, true,
                     !nextTransState.first && nextTransState.second,
                     "Toggle transition-to-next for selected cues");
        ry += kRowStep;

        SDL_Rect gotoBox {ctrl.x + 10, ry, kCtrlW - 80, 26};
        SDL_Rect gotoEdit {ctrl.x + kCtrlW - 64, ry, 54, 26};
        std::string gotoDisplay = stringMixedLabel([&](const Cue& cue) { return cue.gotoTarget; }, "(next)");
        Primitives::drawFramedPanel(controlRenderer_, gotoBox, pal.light,
                                    pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {gotoBox.x + 6, gotoBox.y, gotoBox.w - 12, gotoBox.h},
                     fitInspectorText(fontSmall_, gotoDisplay, gotoBox.w - 16),
                     pal.deep);
        Primitives::drawFramedPanel(controlRenderer_, gotoEdit, pal.dark,
                                    pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, gotoEdit, "goto", pal.light);
        quickButtons_.push_back({gotoEdit, QuickAction::EditGotoTarget, "Set goto target for selected cues"});
        ry += kRowStep;

        if (allLowerThird) {
          drawQuickRow(ry, "bg alpha", QuickAction::LowerBgDec,
                       intMixedLabel([&](const Cue& cue) { return cue.lowerThirdBgAlpha; }),
                       QuickAction::LowerBgInc, QuickAction::ToggleLoop, false, false,
                       "Set Lower Third background alpha");
          ry += kRowStep;
        }

        std::string tagStr = stringMixedLabel([&](const Cue& cue) { return cue.colorTag; }, "none");
        SDL_Rect tagBtn {ctrl.x + 10, ry, kCtrlW - 20, 28};
        SDL_Color tagFill = colorTagToSdl(tagStr == "mixed" ? std::string() : tagStr, 200);
        Primitives::drawFramedPanel(controlRenderer_, tagBtn, tagFill, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, tagBtn,
                             fitInspectorText(fontSmall_, "tag: " + tagStr + "  [K cycle]", tagBtn.w - 12),
                             pal.light);
        quickButtons_.push_back({tagBtn, QuickAction::CycleColorTag, "Cycle color tag for selected cues"});
        ry += kRowStep;

        SDL_Rect notesBox {ctrl.x + 10, ry, kCtrlW - 80, 26};
        SDL_Rect notesEdit {ctrl.x + kCtrlW - 64, ry, 54, 26};
        std::string notesDisplay = stringMixedLabel([&](const Cue& cue) { return cue.notes; }, "(no notes)");
        Primitives::drawFramedPanel(controlRenderer_, notesBox, pal.light,
                                    pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {notesBox.x + 6, notesBox.y, notesBox.w - 12, notesBox.h},
                     fitInspectorText(fontSmall_, notesDisplay, notesBox.w - 16),
                     colorFromRgba(notesDisplay == "(no notes)" ? kScreenInkSoftColor : kScreenDeepColor));
        Primitives::drawFramedPanel(controlRenderer_, notesEdit, pal.dark,
                                    pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, notesEdit, "edit", pal.light);
        quickButtons_.push_back({notesEdit, QuickAction::EditNotes, "Edit notes for selected cues"});
        ry += kRowStep;

        SDL_Rect cueIdBox {ctrl.x + 10, ry, kCtrlW - 80, 26};
        SDL_Rect cueIdEdit {ctrl.x + kCtrlW - 64, ry, 54, 26};
        std::string cueIdDisplay = stringMixedLabel([&](const Cue& cue) { return cue.cueId; }, "(none)");
        Primitives::drawFramedPanel(controlRenderer_, cueIdBox, pal.light,
                                    pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {cueIdBox.x + 6, cueIdBox.y, cueIdBox.w - 12, cueIdBox.h},
                     fitInspectorText(fontSmall_, cueIdDisplay, cueIdBox.w - 16),
                     pal.deep);
        Primitives::drawFramedPanel(controlRenderer_, cueIdEdit, pal.dark,
                                    pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, cueIdEdit, "edit", pal.light);
        quickButtons_.push_back({cueIdEdit, QuickAction::EditCueNumber, "Set cue ID for selected cues"});
        ry += kRowStep;
      }
      finishInspectorSection(playbackSection, ry);

      auto geometrySection = beginInspectorSection(ry, "GEOMETRY", cueSectionGeometryOpen_,
                                                   QuickAction::CueSectionGeometryToggle,
                                                   "Common geometry controls");
      ry = geometrySection.bodyStartY;
      if (cueSectionGeometryOpen_) {
        if (allSupportsGeometry) {
          ry = drawGeometryRows(ry, *selectedCue, true);
          ry = drawColorRows(ry, *selectedCue);
        } else {
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {ctrl.x + 10, ry, kCtrlW - 20, kInspectorRowH},
                       fitInspectorText(fontSmall_, "mixed selection: geometry unavailable", kCtrlW - 24),
                       pal.inkSoft);
          ry += kRowStep;
        }
      }
      finishInspectorSection(geometrySection, ry);

      auto keySection = beginInspectorSection(ry, "KEY", cueSectionKeyOpen_,
                                              QuickAction::CueSectionKeyToggle,
                                              "Common key controls");
      ry = keySection.bodyStartY;
      if (cueSectionKeyOpen_) {
        if (allSupportsKey) {
          ry = drawKeyRows(ry, *selectedCue);
        } else {
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {ctrl.x + 10, ry, kCtrlW - 20, kInspectorRowH},
                       fitInspectorText(fontSmall_, "mixed selection: key unavailable", kCtrlW - 24),
                       pal.inkSoft);
          ry += kRowStep;
        }
      }
      finishInspectorSection(keySection, ry);

    } else if (selectedCue && selectedCue->kind == CueKind::Video) {
      int volPct = static_cast<int>(std::round((engine ? engine->volume() : 1.0f) * 100.0f));
      int ry = ctrlSettingsY + 22 - cueSettingsScroll_;
      constexpr int kRowStep = kInspectorRowStep;
      auto playbackSection = beginInspectorSection(ry, "PLAYBACK", cueSectionPlaybackOpen_,
                                                   QuickAction::CueSectionPlaybackToggle,
                                                   "Collapse/expand playback settings");
      int playbackBodyY = playbackSection.bodyStartY;
      int playbackRowsUsed = 8;
      if (cueSectionPlaybackOpen_) {
        ry = playbackBodyY;
      drawQuickRow(ry,                "deck fader",  QuickAction::VolDec,     std::to_string(volPct) + "%",               QuickAction::VolInc,    QuickAction::ToggleLoop, false, false, "DECK playback level (live, not saved with the cue) - per-cue trim is gain in the AUDIO section");
      drawQuickRow(ry + kRowStep,     "fade in",  QuickAction::FadeInDec,  formatSeconds(selectedCue->fadeInSeconds),  QuickAction::FadeInInc, QuickAction::ToggleLoop, false, false, "[ / ] keys — fade-in duration");
      drawQuickRow(ry + kRowStep * 2, "fade out", QuickAction::FadeOutDec, formatSeconds(selectedCue->fadeOutSeconds), QuickAction::FadeOutInc,QuickAction::ToggleLoop, false, false, "Shift+[ / ] — fade-out duration");
      drawQuickRow(ry + kRowStep * 3, "in",       QuickAction::InDec,      formatSeconds(selectedCue->inPointSeconds), QuickAction::InInc,     QuickAction::ToggleLoop, false, false, "In-point: cue starts playback here");
      {
        double outVal = selectedCue->outPointSeconds > 0.0 ? selectedCue->outPointSeconds : selectedCue->duration;
        drawQuickRow(ry + kRowStep * 4, "out",    QuickAction::OutDec,     formatSeconds(outVal),                      QuickAction::OutInc,    QuickAction::ToggleLoop, false, false, "Out-point: cue stops playback here");
      }
      {
        // Per-cue transition: duration [-][+] on the left, style cycle button on the right
        bool hasCueTrans = selectedCue->cueTransitionSeconds >= 0.0;
        std::string tranVal = hasCueTrans
          ? formatSeconds(selectedCue->cueTransitionSeconds)
          : "deck";
        drawQuickRow(ry + kRowStep * 5, "transition", QuickAction::TransDec, tranVal, QuickAction::TransInc,
                     QuickAction::ToggleLoop, false, false, "Per-cue transition duration override");
        // Transition style (own row)
        {
          int rx = ctrl.x + 10;
          int styleW = kCtrlW - 20;
          SDL_Rect styleBtn {rx, ry + kRowStep * 6, styleW, kInspectorRowH};
          std::string curStyle = selectedCue->cueTransitionStyle.empty()
            ? focusedDeck().transitionStyle : selectedCue->cueTransitionStyle;
          std::string styleLabel = "style: " + transitionStyleLabel(curStyle);
          SDL_Color styleFill = hasCueTrans
            ? pal.dark : pal.light;
          SDL_Color styleInk = hasCueTrans
            ? pal.light : pal.deep;
          Primitives::drawFramedPanel(controlRenderer_, styleBtn, styleFill, pal.deep, pal.mid);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {styleBtn.x + 6, styleBtn.y, styleBtn.w - 18, styleBtn.h},
                       fitInspectorText(fontSmall_, styleLabel, styleBtn.w - 22),
                       styleInk);
          drawCenteredTextSafe(controlRenderer_, fontSmall_,
                               SDL_Rect {styleBtn.x + styleBtn.w - 14, styleBtn.y, 14, styleBtn.h},
                               "\xe2\x96\xbc", styleInk);
          cueTransitionStyleDropdownRect_ = styleBtn;
        }
      }
      // loop / hold toggles side by side
      {
        int rx = ctrl.x + 10;
        int ty = ry + kRowStep * 7;
        int halfW = (kCtrlW - 24) / 2;
        SDL_Rect loopBtn {rx, ty, halfW, 30};
        SDL_Rect holdBtn {rx + halfW + 4, ty, halfW, 30};
        SDL_Color loopFill = selectedCue->loop ? pal.dark : pal.light;
        SDL_Color holdFill = selectedCue->pauseOnLastFrame ? pal.dark : pal.light;
        SDL_Color loopInk  = selectedCue->loop ? pal.light : pal.deep;
        SDL_Color holdInk  = selectedCue->pauseOnLastFrame ? pal.light : pal.deep;
        Primitives::drawFramedPanel(controlRenderer_, loopBtn, loopFill, pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {loopBtn.x + 6, loopBtn.y, loopBtn.w - 12, loopBtn.h},
                     fitInspectorText(fontSmall_,
                                      std::string("loop: ") + (selectedCue->loop ? "on" : "off"),
                                      loopBtn.w - 16),
                     loopInk);
        quickButtons_.push_back({loopBtn, QuickAction::ToggleLoop, "L — loop this cue continuously"});
        Primitives::drawFramedPanel(controlRenderer_, holdBtn, holdFill, pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {holdBtn.x + 6, holdBtn.y, holdBtn.w - 12, holdBtn.h},
                     fitInspectorText(fontSmall_,
                                      std::string("hold: ") + (selectedCue->pauseOnLastFrame ? "on" : "off"),
                                      holdBtn.w - 16),
                     holdInk);
        quickButtons_.push_back({holdBtn, QuickAction::ToggleHold, "E — freeze on last frame instead of stopping"});
      }
      SDL_Rect endBtn {ctrl.x + 10, ry + kRowStep * 8, kCtrlW - 20, 30};
      Primitives::drawFramedPanel(controlRenderer_, endBtn, pal.light, pal.deep, pal.mid);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {endBtn.x + 10, endBtn.y, endBtn.w - 20, endBtn.h},
                   fitInspectorText(fontSmall_,
                                    "end: " + cueEndActionLabel(selectedCue->endAction) + "  [X cycle]",
                                    endBtn.w - 24),
                   pal.deep);
      quickButtons_.push_back({endBtn, QuickAction::CycleEndAction, "X — cycle end action: stop / next / loop"});
      {
        int rowCursor = 9;
        std::string loopStr = selectedCue->loopCount == 0 ? "inf" : std::to_string(selectedCue->loopCount) + "x";
        drawQuickRow(ry + kRowStep * rowCursor, "repeats", QuickAction::LoopCountDec, loopStr, QuickAction::LoopCountInc,
                     QuickAction::ToggleLoop, false, false, "Fixed repeat count — 0 = loop forever");
        rowCursor += 1;

        drawQuickRow(ry + kRowStep * rowCursor, "speed", QuickAction::SpeedDec, fmtFloat(selectedCue->playbackSpeed, 2) + "x", QuickAction::SpeedInc,
                     QuickAction::ToggleLoop, false, false, "Playback speed: 0.25–4.0×");
        rowCursor += 1;

        drawQuickRow(ry + kRowStep * rowCursor, "pause begin", QuickAction::TogglePauseBegin,
                     selectedCue->pauseAtBeginning ? "on" : "off",
                     QuickAction::TogglePauseBegin, QuickAction::TogglePauseBegin, true, selectedCue->pauseAtBeginning,
                     "Load the cue and hold first frame when taken");
        rowCursor += 1;

        // AUDIO is its own collapsible section: enable, trim, pan, mono,
        // independent audio fades, loudness normalize.
        {
          int audioSecY = ry + kRowStep * rowCursor;
          auto audioSection = beginInspectorSection(audioSecY, "AUDIO", cueSectionAudioOpen_,
                                                    QuickAction::CueSectionAudioToggle,
                                                    "Per-cue audio: enable, trim, pan, mono, fades, normalize");
          int ay = audioSection.bodyStartY;
          if (cueSectionAudioOpen_) {
            if (!selectedCue->hasAudio) {
              ay = drawInspectorMessageRow(ay, "no audio track", pal.light, pal.deep);
            } else {
              drawQuickRow(ay, "audio", QuickAction::ToggleCueAudio,
                           selectedCue->audioEnabled ? "on" : "off",
                           QuickAction::ToggleCueAudio, QuickAction::ToggleCueAudio, true,
                           selectedCue->audioEnabled, "Toggle cue audio track for this cue");
              ay += kRowStep;
              char gainBuf[24];
              std::snprintf(gainBuf, sizeof(gainBuf), "%+.1f dB", selectedCue->audioGainDb);
              drawQuickRow(ay, "gain", QuickAction::AudioGainDec, gainBuf, QuickAction::AudioGainInc,
                           QuickAction::ToggleLoop, false, false,
                           "Per-cue audio trim: -24 to +12 dB, applied live");
              ay += kRowStep;
              std::string panLabel = "center";
              if (selectedCue->audioPan < -0.024f) {
                panLabel = "L " + std::to_string(static_cast<int>(std::lround(-selectedCue->audioPan * 100.0f)));
              } else if (selectedCue->audioPan > 0.024f) {
                panLabel = "R " + std::to_string(static_cast<int>(std::lround(selectedCue->audioPan * 100.0f)));
              }
              drawQuickRow(ay, "pan", QuickAction::AudioPanDec, panLabel, QuickAction::AudioPanInc,
                           QuickAction::ToggleLoop, false, false,
                           "Stereo balance, applied live (snaps to center)");
              ay += kRowStep;
              drawQuickRow(ay, "mono", QuickAction::ToggleCueMono,
                           selectedCue->audioMono ? "on" : "off",
                           QuickAction::ToggleCueMono, QuickAction::ToggleCueMono, true,
                           selectedCue->audioMono, "Downmix this cue to mono (mono sources / mono PA)");
              ay += kRowStep;
              if (focusedDeck().audioOutputChannels > 2) {
                std::string outsLabel = std::to_string(selectedCue->audioOutputPair * 2 + 1)
                  + "-" + std::to_string(selectedCue->audioOutputPair * 2 + 2);
                drawQuickRow(ay, "outs", QuickAction::AudioOutPairDec, outsLabel,
                             QuickAction::AudioOutPairInc, QuickAction::ToggleLoop, false, false,
                             "Device output pair this cue's audio plays on");
                ay += kRowStep;
              }
              auto audioFadeLabel = [](float v) {
                return v < 0.0f ? std::string("follow")
                     : (v <= 0.001f ? std::string("none") : fmtFloat(v, 2) + "s");
              };
              drawQuickRow(ay, "a-fade in", QuickAction::AudioFadeInDec,
                           audioFadeLabel(selectedCue->audioFadeInSeconds), QuickAction::AudioFadeInInc,
                           QuickAction::ToggleLoop, false, false,
                           "Audio fade-in: follow visual fade, none, or explicit seconds");
              ay += kRowStep;
              drawQuickRow(ay, "a-fade out", QuickAction::AudioFadeOutDec,
                           audioFadeLabel(selectedCue->audioFadeOutSeconds), QuickAction::AudioFadeOutInc,
                           QuickAction::ToggleLoop, false, false,
                           "Audio fade-out: follow visual fade, none, or explicit seconds");
              ay += kRowStep;
              if (cueUsesFilesystemMedia(*selectedCue)) {
                SDL_Rect normBtn {ctrl.x + 10, ay, kCtrlW - 20, 26};
                Primitives::drawFramedPanel(controlRenderer_, normBtn, pal.dark, pal.deep, pal.mid);
                drawCenteredTextSafe(controlRenderer_, fontSmall_, normBtn,
                                     "normalize loudness (R128)", pal.light);
                quickButtons_.push_back({normBtn, QuickAction::NormalizeCueAudio,
                                         "Measure loudness and set gain for -16 LUFS playback"});
                ay += kRowStep;
              }
            }
          }
          finishInspectorSection(audioSection, ay);
          int audioSecEnd = (cueSectionAudioOpen_ ? ay : audioSection.bodyStartY) + 6;
          rowCursor += std::max(1, (audioSecEnd - audioSecY + kRowStep - 1) / kRowStep);
        }

        drawQuickRow(ry + kRowStep * rowCursor, "next xfade", QuickAction::ToggleNextTransition,
                     selectedCue->transitionToNext ? "on" : "off",
                     QuickAction::ToggleNextTransition, QuickAction::ToggleNextTransition, true,
                     selectedCue->transitionToNext,
                     "Use transition when auto-advancing or goto-taking next cue");
        rowCursor += 1;

        int gotoY = ry + kRowStep * rowCursor;
        SDL_Rect gotoBox {ctrl.x + 10, gotoY, kCtrlW - 80, 26};
        SDL_Rect gotoEdit {ctrl.x + kCtrlW - 64, gotoY, 54, 26};
        std::string gotoDisplay = selectedCue->gotoTarget.empty() ? "(next cue)" : selectedCue->gotoTarget;
        Primitives::drawFramedPanel(controlRenderer_, gotoBox, pal.light,
                                    pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {gotoBox.x + 6, gotoBox.y, gotoBox.w - 12, gotoBox.h},
                     fitInspectorText(fontSmall_, gotoDisplay, gotoBox.w - 16),
                     pal.deep);
        Primitives::drawFramedPanel(controlRenderer_, gotoEdit, pal.dark,
                                    pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, gotoEdit, "goto", pal.light);
        quickButtons_.push_back({gotoEdit, QuickAction::EditGotoTarget, "Set cue token to jump to when cue ends"});
        rowCursor += 1;

        std::string tagStr = selectedCue->colorTag.empty() ? "none" : selectedCue->colorTag;
        SDL_Rect tagBtn {ctrl.x + 10, ry + kRowStep * rowCursor, kCtrlW - 20, 28};
        SDL_Color tagFill = colorTagToSdl(selectedCue->colorTag, 200);
        Primitives::drawFramedPanel(controlRenderer_, tagBtn, tagFill, pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {tagBtn.x + 6, tagBtn.y, tagBtn.w - 12, tagBtn.h},
                     fitInspectorText(fontSmall_, "tag: " + tagStr + "  [K cycle]", tagBtn.w - 16),
                     pal.light);
        quickButtons_.push_back({tagBtn, QuickAction::CycleColorTag, "C — cycle cue color tag"});
        rowCursor += 1;

        int notesY = ry + kRowStep * rowCursor;
        SDL_Rect notesBox {ctrl.x + 10, notesY, kCtrlW - 80, 26};
        SDL_Rect notesEdit {ctrl.x + kCtrlW - 64, notesY, 54, 26};
        std::string notesDisplay = selectedCue->notes.empty() ? "(no notes)" : selectedCue->notes;
        Primitives::drawFramedPanel(controlRenderer_, notesBox, pal.light, pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {notesBox.x + 6, notesBox.y, notesBox.w - 12, notesBox.h},
                     fitInspectorText(fontSmall_, notesDisplay, notesBox.w - 16),
                     colorFromRgba(selectedCue->notes.empty() ? kScreenInkSoftColor : kScreenDeepColor));
        Primitives::drawFramedPanel(controlRenderer_, notesEdit, pal.dark, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, notesEdit, "edit", pal.light);
        quickButtons_.push_back({notesEdit, QuickAction::EditNotes, "Click to edit cue notes"});
        rowCursor += 1;

        int cnY = ry + kRowStep * rowCursor;
        SDL_Rect idLabel {ctrl.x + 10, cnY, 36, 26};
        SDL_Rect val {ctrl.x + 52, cnY, kCtrlW - 122, 26};
        SDL_Rect editBtn {ctrl.x + kCtrlW - 64, cnY, 54, 26};
        drawTextSafe(controlRenderer_, fontSmall_, idLabel, "id", pal.inkSoft);
        std::string cnDisplay = cueDisplayToken(*selectedCue, focusedDeck().selectedIndex);
        Primitives::drawFramedPanel(controlRenderer_, val, pal.light, pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {val.x + 6, val.y, val.w - 12, val.h},
                     fitInspectorText(fontSmall_, cnDisplay, val.w - 16),
                     pal.deep);
        Primitives::drawFramedPanel(controlRenderer_, editBtn, pal.dark, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, editBtn, "edit", pal.light);
        quickButtons_.push_back({editBtn, QuickAction::EditCueNumber, "Set short cue id for search/goto"});
        rowCursor += 1;

        int ppY = ry + kRowStep * rowCursor;
        int ppCount = static_cast<int>(selectedCue->pausePoints.size());
        constexpr int kPpBtnW = 46, kPpBtnGap = 8, kPpRightMargin = 10;
        SDL_Rect clrBtn {ctrl.x + kCtrlW - kPpRightMargin - kPpBtnW, ppY, kPpBtnW, 26};
        SDL_Rect addBtn {clrBtn.x - kPpBtnGap - kPpBtnW, ppY, kPpBtnW, 26};
        SDL_Rect ppLabel {ctrl.x + 20, ppY, std::max(10, addBtn.x - ctrl.x - 20 - 8), 26};
        drawTextSafe(controlRenderer_, fontSmall_, ppLabel,
                     "pause pts: " + std::to_string(ppCount), pal.inkSoft);
        Primitives::drawFramedPanel(controlRenderer_, addBtn, pal.dark, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, addBtn, "+now", pal.light);
        Primitives::drawFramedPanel(controlRenderer_, clrBtn, pal.deleteBezel, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, clrBtn, "clr", pal.light);
        quickButtons_.push_back({addBtn, QuickAction::AddPausePoint, "Add pause point at current position"});
        quickButtons_.push_back({clrBtn, QuickAction::ClearPausePoints, "Clear all pause points"});
        rowCursor += 1;

        playbackRowsUsed = rowCursor;
      }
      }
      finishInspectorSection(playbackSection, cueSectionPlaybackOpen_ ? (ry + kRowStep * playbackRowsUsed) : playbackBodyY);
      int metadataStartY = cueSectionPlaybackOpen_ ? (ry + kRowStep * playbackRowsUsed + kInspectorSectionGap)
                                                   : (playbackBodyY + kInspectorSectionGap);
      auto metadataSection = beginInspectorSection(metadataStartY, "MEDIA", cueSectionMetadataOpen_,
                                                   QuickAction::CueSectionMetadataToggle,
                                                   "Cue file details and trim summary");
      int metadataY = metadataSection.bodyStartY;
      if (cueSectionMetadataOpen_) {
        metadataY = drawCueTechnicalRows(metadataY, *selectedCue);
      }
      finishInspectorSection(metadataSection, metadataY);

      int geoY = cueSectionMetadataOpen_ ? (metadataY + kInspectorSectionGap)
                                         : (metadataSection.bodyStartY + kInspectorSectionGap);
      auto geometrySection = beginInspectorSection(geoY, "GEOMETRY", cueSectionGeometryOpen_,
                                                   QuickAction::CueSectionGeometryToggle,
                                                   "Collapse/expand geometry controls");
      geoY = geometrySection.bodyStartY;
      if (cueSectionGeometryOpen_) {
        geoY = drawGeometryRows(geoY, *selectedCue, true);
        geoY = drawColorRows(geoY, *selectedCue);
      }
      finishInspectorSection(geometrySection, geoY);

      auto keySection = beginInspectorSection(geoY, "KEY", cueSectionKeyOpen_,
                                              QuickAction::CueSectionKeyToggle,
                                              "Collapse/expand key controls");
      geoY = keySection.bodyStartY;
      if (cueSectionKeyOpen_) {
        geoY = drawKeyRows(geoY, *selectedCue);
      }
      finishInspectorSection(keySection, geoY);

    } else if (selectedCue && selectedCue->kind == CueKind::Composite) {
      int ry = ctrlSettingsY + 22 - cueSettingsScroll_;
      constexpr int kRowStep = kInspectorRowStep;
      auto playbackSection = beginInspectorSection(ry, "PLAYBACK", cueSectionPlaybackOpen_,
                                                   QuickAction::CueSectionPlaybackToggle,
                                                   "Composite scene playback settings");
      int playbackY = playbackSection.bodyStartY;
      if (cueSectionPlaybackOpen_) {
        playbackY = drawInspectorMessageRow(playbackY, "Composite scene cue",
                                            pal.mid,
                                            pal.deep);
        std::string durVal = selectedCue->stillDurationSeconds > 0.0
          ? formatSeconds(selectedCue->stillDurationSeconds)
          : "hold";
        drawQuickRow(playbackY, "duration", QuickAction::DurDec, durVal, QuickAction::DurInc,
                     QuickAction::ToggleLoop, false, false, "Scene dwell time — 0 = hold");
        playbackY += kRowStep;
        drawQuickRow(playbackY, "fade in", QuickAction::FadeInDec, formatSeconds(selectedCue->fadeInSeconds),
                     QuickAction::FadeInInc, QuickAction::ToggleLoop, false, false, "Scene fade-in duration");
        playbackY += kRowStep;
        drawQuickRow(playbackY, "fade out", QuickAction::FadeOutDec, formatSeconds(selectedCue->fadeOutSeconds),
                     QuickAction::FadeOutInc, QuickAction::ToggleLoop, false, false, "Scene fade-out duration");
        playbackY += kRowStep;
        drawQuickRow(playbackY, "hold", QuickAction::ToggleHold,
                     selectedCue->pauseOnLastFrame ? "on" : "off",
                     QuickAction::ToggleHold, QuickAction::ToggleHold, true,
                     selectedCue->pauseOnLastFrame,
                     "Hold on the last composite frame when the scene ends");
        playbackY += kRowStep;
        SDL_Rect endBtn {ctrl.x + 10, playbackY, kCtrlW - 20, 30};
        drawUIPanel(endBtn, pal.light, pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {endBtn.x + 10, endBtn.y, endBtn.w - 20, endBtn.h},
                     "end: " + cueEndActionLabel(selectedCue->endAction) + "  [X cycle]",
                     pal.deep);
        quickButtons_.push_back({endBtn, QuickAction::CycleEndAction, "X — cycle end action"});
        playbackY += kRowStep;
      }
      finishInspectorSection(playbackSection, playbackY);

      int sceneStartY = cueSectionPlaybackOpen_ ? (playbackY + kInspectorSectionGap)
                                                : (playbackSection.bodyStartY + kInspectorSectionGap);
      auto sceneSection = beginInspectorSection(sceneStartY, "SCENE", cueSectionMetadataOpen_,
                                                QuickAction::CueSectionMetadataToggle,
                                                "Composite slot sources and layout");
      int sceneY = sceneSection.bodyStartY;
      if (cueSectionMetadataOpen_) {
        sceneY = drawInspectorStatusRow(sceneY, "layout",
                                        compositeLayoutPresetLabel(selectedCue->compositeLayoutPreset), false);
        {
          int rx = ctrl.x + 10;
          int gap = 6;
          int btnW = std::max(64, (kCtrlW - 20 - gap * 2) / 3);
          SDL_Rect twoUpRect {rx, sceneY, btnW, kInspectorRowH};
          SDL_Rect ratioRect {rx + btnW + gap, sceneY, btnW, kInspectorRowH};
          SDL_Rect quadRect {rx + (btnW + gap) * 2, sceneY, btnW, kInspectorRowH};
          drawUIPanel(twoUpRect, pal.mid, pal.deep, pal.light);
          drawUIPanel(ratioRect, pal.mid, pal.deep, pal.light);
          drawUIPanel(quadRect, pal.mid, pal.deep, pal.light);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, twoUpRect, "2-UP", pal.deep);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, ratioRect, "70/30", pal.deep);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, quadRect, "QUAD", pal.deep);
          quickButtons_.push_back({twoUpRect, QuickAction::CompositePreset2Up, "Apply a side-by-side 2-up scene preset"});
          quickButtons_.push_back({ratioRect, QuickAction::CompositePreset7030, "Apply a big/small 70/30 scene preset"});
          quickButtons_.push_back({quadRect, QuickAction::CompositePresetQuad, "Apply a four-box scene preset"});
        }
        sceneY += kRowStep;

        const std::array<QuickAction, 4> slotActions {{
          QuickAction::EditCompositeSlot1Source,
          QuickAction::EditCompositeSlot2Source,
          QuickAction::EditCompositeSlot3Source,
          QuickAction::EditCompositeSlot4Source,
        }};
        for (size_t slotIndex = 0; slotIndex < selectedCue->compositeSlots.size() && slotIndex < slotActions.size(); ++slotIndex) {
          const CompositeSlot& slot = selectedCue->compositeSlots[slotIndex];
          std::string label = slot.name.empty() ? compositeSlotDefaultName(static_cast<int>(slotIndex)) : slot.name;
          std::string value = compositeSourceTypeLabel(slot.sourceType) + "  " + compositeSourceDisplayLabel(slot);
          SDL_Color valueColor = trim(slot.source).empty()
            ? pal.inkSoft
            : pal.deep;
          sceneY = drawInspectorEditableRow(sceneY, toLower(label), value,
                                            slotActions[slotIndex],
                                            "Set slot source as media:/path, browser:url, window:name, camera:name, or syphon:name",
                                            valueColor);
        }
        sceneY = drawInspectorEditableRow(sceneY, "audio",
                                          compositeAudioSummaryLabel(*selectedCue),
                                          QuickAction::CycleCompositeAudioSlot,
                                          "Cycle which slot drives scene audio");
        sceneY = drawCueTagRow(sceneY, *selectedCue, "K — cycle cue color tag");
        sceneY = drawInspectorEditableRow(sceneY, "notes",
                                          selectedCue->notes.empty() ? "(no notes)" : selectedCue->notes,
                                          QuickAction::EditNotes,
                                          "Click to edit cue notes",
                                          colorFromRgba(selectedCue->notes.empty() ? kScreenInkSoftColor : kScreenDeepColor));
        sceneY = drawInspectorMessageRow(sceneY,
                                         "Slots are self-contained sources in this first pass",
                                         pal.light,
                                         pal.dark);
      }
      finishInspectorSection(sceneSection, sceneY);
      int overlayStartY = (cueSectionMetadataOpen_ ? sceneY : sceneSection.bodyStartY) + kInspectorSectionGap;
      auto overlaySection = beginInspectorSection(overlayStartY, "OVERLAYS", cueSectionRoutingOpen_,
                                                  QuickAction::CueSectionRoutingToggle,
                                                  "Overlay bin items that fire when this cue is taken");
      int overlayY = overlaySection.bodyStartY;
      if (cueSectionRoutingOpen_) {
        auto attachedColor = [&](const std::string& value) {
          if (value == "(none)") {
            return pal.inkSoft;
          }
          if (value.find("(missing)") != std::string::npos) {
            return SDL_Color {140, 40, 20, 255};
          }
          return pal.deep;
        };
        std::string lowerThirdValue = attachedOverlaySummaryLabel(deck, *selectedCue, CueKind::LowerThird);
        overlayY = drawInspectorEditableRow(overlayY, "lower 3rd",
                                            lowerThirdValue,
                                            QuickAction::EditAttachedLowerThirdCue,
                                            "Choose a lower third from the overlay bin to fire on TAKE",
                                            attachedColor(lowerThirdValue));
        std::string pipValue = attachedOverlaySummaryLabel(deck, *selectedCue, CueKind::Pip);
        overlayY = drawInspectorEditableRow(overlayY, "pip",
                                            pipValue,
                                            QuickAction::EditAttachedPipCue,
                                            "Choose a PIP overlay from the overlay bin to fire on TAKE",
                                            attachedColor(pipValue));
        overlayY = drawInspectorMessageRow(overlayY, "Attached overlays fire on TAKE only",
                                           pal.light,
                                           pal.dark);
      }
      finishInspectorSection(overlaySection, overlayY);
    } else if (selectedCue && selectedCue->kind == CueKind::LowerThird) {
      int ry = ctrlSettingsY + 22 - cueSettingsScroll_;
      constexpr int kRowStep = kInspectorRowStep;
      auto playbackSection = beginInspectorSection(ry, "PLAYBACK", cueSectionPlaybackOpen_,
                                                   QuickAction::CueSectionPlaybackToggle,
                                                   "Lower Third playback and overlay controls");
      int playbackY = playbackSection.bodyStartY;
      if (cueSectionPlaybackOpen_) {
        playbackY = drawInspectorMessageRow(playbackY, "Lower Third overlay cue",
                                            pal.mid,
                                            pal.deep);

        SDL_Rect textPreview {ctrl.x + 10, playbackY, kCtrlW - 20, 30};
        drawUIPanel(textPreview, pal.mid, pal.deep, pal.light);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, textPreview,
                             selectedCue->lowerThirdText.empty() ? selectedCue->name : selectedCue->lowerThirdText,
                             pal.deep);
        playbackY += 34;

        SDL_Rect subPreview {ctrl.x + 10, playbackY, kCtrlW - 20, 28};
        drawUIPanel(subPreview, pal.light, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, subPreview,
                             selectedCue->lowerThirdSubtext.empty() ? "(no subtext)" : selectedCue->lowerThirdSubtext,
                             pal.dark);
        playbackY += 32;

        drawQuickRow(playbackY, "bg alpha", QuickAction::LowerBgDec,
                     std::to_string(selectedCue->lowerThirdBgAlpha), QuickAction::LowerBgInc,
                     QuickAction::ToggleLoop, false, false, "Lower Third background opacity (0-255)");
        playbackY += kRowStep;

        playbackY = drawInspectorEditableRow(playbackY, "title",
                                             selectedCue->lowerThirdText.empty() ? selectedCue->name : selectedCue->lowerThirdText,
                                             QuickAction::EditLowerThirdText,
                                             "Edit the main lower-third title");
        playbackY = drawInspectorEditableRow(playbackY, "sub",
                                             selectedCue->lowerThirdSubtext.empty() ? "(no subtext)" : selectedCue->lowerThirdSubtext,
                                             QuickAction::EditLowerThirdSubtext,
                                             "Edit the secondary lower-third line",
                                             colorFromRgba(selectedCue->lowerThirdSubtext.empty() ? kScreenInkSoftColor : kScreenDeepColor));

        std::string durVal = selectedCue->stillDurationSeconds > 0.0 ? formatSeconds(selectedCue->stillDurationSeconds) : "hold";
        drawQuickRow(playbackY, "duration", QuickAction::DurDec, durVal, QuickAction::DurInc,
                     QuickAction::ToggleLoop, false, false, "Auto-advance duration — 0 = hold until taken");
        playbackY += kRowStep;

        playbackY = drawInspectorActionRow(playbackY, "CLEAR OVERLAY  [Backspace]",
                                           QuickAction::ClearOverlay,
                                           "Clear the active Lower Third overlay now");
      }
      finishInspectorSection(playbackSection, playbackY);

      int metadataStartY = cueSectionPlaybackOpen_ ? (playbackY + kInspectorSectionGap)
                                                   : (playbackSection.bodyStartY + kInspectorSectionGap);
      auto metadataSection = beginInspectorSection(metadataStartY, "METADATA", cueSectionMetadataOpen_,
                                                   QuickAction::CueSectionMetadataToggle,
                                                   "Cue notes, tags, and control hints");
      int metadataY = metadataSection.bodyStartY;
      if (cueSectionMetadataOpen_) {
        metadataY = drawInspectorMessageRow(metadataY, "Edit title/subtitle here or fire it live with TAKE",
                                            pal.light,
                                            pal.dark);
        metadataY = drawCueTagRow(metadataY, *selectedCue, "K — cycle cue color tag");
        metadataY = drawInspectorEditableRow(metadataY, "notes",
                                             selectedCue->notes.empty() ? "(no notes)" : selectedCue->notes,
                                             QuickAction::EditNotes,
                                             "Click to edit cue notes",
                                             colorFromRgba(selectedCue->notes.empty() ? kScreenInkSoftColor : kScreenDeepColor));
        metadataY = drawInspectorEditableRow(metadataY, "cue id",
                                             cueDisplayToken(*selectedCue, focusedDeck().selectedIndex),
                                             QuickAction::EditCueNumber,
                                             "Set short cue label for search/goto");
      }
      finishInspectorSection(metadataSection, metadataY);
    } else if (selectedCue && (selectedCue->kind == CueKind::Image
                               || selectedCue->kind == CueKind::Pattern
                               || selectedCue->kind == CueKind::Browser
                               || isSourceCueKind(selectedCue->kind))) {
      int ry = ctrlSettingsY + 22 - cueSettingsScroll_;
      constexpr int kRowStep = kInspectorRowStep;
      auto playbackSection = beginInspectorSection(ry, "PLAYBACK", cueSectionPlaybackOpen_,
                                                   QuickAction::CueSectionPlaybackToggle,
                                                   "Still, pattern, browser, and source playback settings");
      int playbackY = playbackSection.bodyStartY;
      if (cueSectionPlaybackOpen_) {
        if (selectedCue->kind == CueKind::Pattern) {
          std::string typeId = normalizePatternTypeId(selectedCue->path);
          bool motionEnabled = endsWith(typeId, "-motion");
          std::string label = patternLabelForType(typeId);
          SDL_Rect patternTypeLabel {ctrl.x + 10, playbackY, 92, 30};
          SDL_Rect patternTypeBtn {ctrl.x + 104, playbackY, kCtrlW - 114, 30};
          drawTextSafe(controlRenderer_, fontSmall_, patternTypeLabel, "pattern", pal.deep);
          drawUIPanel(patternTypeBtn, pal.light, pal.deep, pal.mid);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {patternTypeBtn.x + 6, patternTypeBtn.y, patternTypeBtn.w - 18, patternTypeBtn.h},
                       label, pal.deep);
          drawCenteredTextSafe(controlRenderer_, fontSmall_,
                               SDL_Rect {patternTypeBtn.x + patternTypeBtn.w - 14, patternTypeBtn.y, 14, patternTypeBtn.h},
                               "v", pal.deep);
          cuePatternTypeDropdownRect_ = patternTypeBtn;
          playbackY += kRowStep;

          drawQuickRow(playbackY, "motion", QuickAction::TogglePatternMotion,
                       motionEnabled ? "on" : "off", QuickAction::TogglePatternMotion,
                       QuickAction::TogglePatternMotion, true, motionEnabled,
                       "Pattern motion toggle");
          playbackY += kRowStep;
        }

        std::string durVal = selectedCue->stillDurationSeconds > 0.0
          ? formatSeconds(selectedCue->stillDurationSeconds) : "hold";
        drawQuickRow(playbackY, "duration", QuickAction::DurDec, durVal, QuickAction::DurInc,
                     QuickAction::ToggleLoop, false, false, "Auto-advance duration — 0 = hold until taken");
        playbackY += kRowStep;

        bool hasCueTrans = selectedCue->cueTransitionSeconds >= 0.0;
        std::string tranVal = hasCueTrans ? formatSeconds(selectedCue->cueTransitionSeconds) : "deck";
        drawQuickRow(playbackY, "transition", QuickAction::TransDec, tranVal, QuickAction::TransInc,
                     QuickAction::ToggleLoop, false, false, "Per-cue transition duration override");
        playbackY += kRowStep;
        // Transition style (own row)
        {
          int rx = ctrl.x + 10;
          int styleW = kCtrlW - 20;
          SDL_Rect styleBtn {rx, playbackY, styleW, kInspectorRowH};
          std::string curStyle = selectedCue->cueTransitionStyle.empty()
            ? focusedDeck().transitionStyle : selectedCue->cueTransitionStyle;
          SDL_Color styleFill = hasCueTrans ? pal.dark : pal.light;
          SDL_Color styleInk = hasCueTrans ? pal.light : pal.deep;
          drawUIPanel(styleBtn, styleFill, pal.deep, pal.mid);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {styleBtn.x + 6, styleBtn.y, styleBtn.w - 18, styleBtn.h},
                       fitInspectorText(fontSmall_,
                                        "style: " + transitionStyleLabel(curStyle),
                                        styleBtn.w - 22),
                       styleInk);
          drawCenteredTextSafe(controlRenderer_, fontSmall_,
                               SDL_Rect {styleBtn.x + styleBtn.w - 14, styleBtn.y, 14, styleBtn.h},
                               "\xe2\x96\xbc", styleInk);
          cueTransitionStyleDropdownRect_ = styleBtn;
        }
        playbackY += kRowStep;

        drawQuickRow(playbackY, "fade in", QuickAction::FadeInDec, formatSeconds(selectedCue->fadeInSeconds),
                     QuickAction::FadeInInc, QuickAction::ToggleLoop, false, false, "Fade-in duration for this cue");
        playbackY += kRowStep;
        drawQuickRow(playbackY, "fade out", QuickAction::FadeOutDec, formatSeconds(selectedCue->fadeOutSeconds),
                     QuickAction::FadeOutInc, QuickAction::ToggleLoop, false, false, "Fade-out duration before next cue");
        playbackY += kRowStep;

        {
          int rx = ctrl.x + 10;
          int halfW = (kCtrlW - 24) / 2;
          SDL_Rect loopBtn {rx, playbackY, halfW, 30};
          SDL_Rect holdBtn {rx + halfW + 4, playbackY, halfW, 30};
          SDL_Color loopFill = selectedCue->loop ? pal.dark : pal.light;
          SDL_Color holdFill = selectedCue->pauseOnLastFrame ? pal.dark : pal.light;
          SDL_Color loopInk = selectedCue->loop ? pal.light : pal.deep;
          SDL_Color holdInk = selectedCue->pauseOnLastFrame ? pal.light : pal.deep;
          drawUIPanel(loopBtn, loopFill, pal.deep, pal.mid);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {loopBtn.x + 6, loopBtn.y, loopBtn.w - 12, loopBtn.h},
                       fitInspectorText(fontSmall_,
                                        std::string("loop: ") + (selectedCue->loop ? "on" : "off"),
                                        loopBtn.w - 16),
                       loopInk);
          quickButtons_.push_back({loopBtn, QuickAction::ToggleLoop, "L — loop this cue"});
          drawUIPanel(holdBtn, holdFill, pal.deep, pal.mid);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {holdBtn.x + 6, holdBtn.y, holdBtn.w - 12, holdBtn.h},
                       fitInspectorText(fontSmall_,
                                        std::string("hold: ") + (selectedCue->pauseOnLastFrame ? "on" : "off"),
                                        holdBtn.w - 16),
                       holdInk);
          quickButtons_.push_back({holdBtn, QuickAction::ToggleHold, "E — hold on this cue indefinitely"});
        }
        playbackY += kRowStep;

        SDL_Rect endBtn {ctrl.x + 10, playbackY, kCtrlW - 20, 30};
        drawUIPanel(endBtn, pal.light, pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {endBtn.x + 10, endBtn.y, endBtn.w - 20, endBtn.h},
                     fitInspectorText(fontSmall_,
                                      "end: " + cueEndActionLabel(selectedCue->endAction) + "  [X cycle]",
                                      endBtn.w - 24),
                     pal.deep);
        quickButtons_.push_back({endBtn, QuickAction::CycleEndAction, "X — cycle end action"});
        playbackY += kRowStep;

        std::string loopStr = selectedCue->loopCount == 0 ? "inf" : std::to_string(selectedCue->loopCount) + "x";
        drawQuickRow(playbackY, "repeats", QuickAction::LoopCountDec, loopStr, QuickAction::LoopCountInc,
                     QuickAction::ToggleLoop, false, false, "Fixed repeat count — 0 = loop forever");
        playbackY += kRowStep;
      }
      finishInspectorSection(playbackSection, playbackY);

      int metadataStartY = cueSectionPlaybackOpen_ ? (playbackY + kInspectorSectionGap)
                                                   : (playbackSection.bodyStartY + kInspectorSectionGap);
      auto metadataSection = beginInspectorSection(metadataStartY, "METADATA", cueSectionMetadataOpen_,
                                                   QuickAction::CueSectionMetadataToggle,
                                                   "Cue metadata and source configuration");
      int metadataY = metadataSection.bodyStartY;
      if (cueSectionMetadataOpen_) {
        if (selectedCue->kind == CueKind::Browser) {
          std::string browserState = browserCueStatusLabel(project_.focusedDeckIndex);
          bool browserWarn = browserState.rfind("failed:", 0) == 0;
          metadataY = drawInspectorStatusRow(metadataY, "state", browserState, browserWarn);
        }

        if (isSourceCueKind(selectedCue->kind)) {
          std::string sourceRef = sourceCueRefFromCue(*selectedCue);
          if (sourceRef.empty()) {
            sourceRef = defaultSourceRefForKind(selectedCue->kind);
          }
          // WindowSource: show a dropdown to pick from available windows
          if (selectedCue->kind == CueKind::WindowSource) {
            SDL_Rect sourceLabelRect {ctrl.x + 10, metadataY, 78, kInspectorRowH};
            SDL_Rect sourceBtn {sourceLabelRect.x + sourceLabelRect.w + 8, metadataY,
                                kCtrlW - 20 - sourceLabelRect.w - 8, kInspectorRowH};
            drawTextSafe(controlRenderer_, fontBase_, sourceLabelRect, "source", pal.inkSoft);
            drawUIPanel(sourceBtn, pal.light, pal.deep, pal.mid);
            drawTextSafe(controlRenderer_, fontBase_,
                         SDL_Rect {sourceBtn.x + 6, sourceBtn.y, sourceBtn.w - 18, sourceBtn.h},
                         ellipsizeToPixelWidth(fontBase_,
                           sourceCueRefFriendlyLabel(selectedCue->kind, sourceRef), sourceBtn.w - 18),
                         pal.deep);
            drawCenteredTextSafe(controlRenderer_, fontSmall_,
                                 SDL_Rect {sourceBtn.x + sourceBtn.w - 14, sourceBtn.y, 14, sourceBtn.h},
                                 "v", pal.deep);
            cueWindowSourceDropdownRect_ = sourceBtn;
            metadataY += kInspectorRowStep;
          } else {
            metadataY = drawInspectorEditableRow(metadataY, "source",
                                                 sourceCueRefFriendlyLabel(selectedCue->kind, sourceRef),
                                                 QuickAction::EditSourceRef,
                                                 "Set capture source from the cue menu");
          }
        }

        if (selectedCue->kind == CueKind::Browser) {
          metadataY = drawInspectorEditableRow(metadataY, "url",
                                               selectedCue->path.empty() ? "(unset)" : selectedCue->path,
                                               QuickAction::EditBrowserUrl,
                                               "Set browser URL/path");
          {
            SDL_Rect rfBtn {ctrl.x + 10, metadataY, kCtrlW - 20, 30};
            SDL_Color rfFill = selectedCue->refreshOnTake ? pal.dark : pal.light;
            SDL_Color rfInk  = selectedCue->refreshOnTake ? pal.light : pal.deep;
            drawUIPanel(rfBtn, rfFill, pal.deep, pal.mid);
            drawCenteredTextSafe(controlRenderer_, fontSmall_, rfBtn,
                                 std::string("refresh on take: ") + (selectedCue->refreshOnTake ? "on" : "off"),
                                 rfInk);
            quickButtons_.push_back({rfBtn, QuickAction::ToggleRefreshOnTake,
                                     "Reload page each time the browser cue is taken"});
            metadataY += kInspectorRowStep;
          }
        }

        metadataY = drawCueTechnicalRows(metadataY, *selectedCue);
        metadataY = drawCueTagRow(metadataY, *selectedCue, "K — cycle cue color tag");
        metadataY = drawInspectorEditableRow(metadataY, "notes",
                                             selectedCue->notes.empty() ? "(no notes)" : selectedCue->notes,
                                             QuickAction::EditNotes,
                                             "Click to edit cue notes",
                                             colorFromRgba(selectedCue->notes.empty() ? kScreenInkSoftColor : kScreenDeepColor));
        metadataY = drawInspectorEditableRow(metadataY, "cue id",
                                             cueDisplayToken(*selectedCue, focusedDeck().selectedIndex),
                                             QuickAction::EditCueNumber,
                                             "Set short cue label for search/goto");
      }
      finishInspectorSection(metadataSection, metadataY);

      int sectionY = cueSectionMetadataOpen_ ? (metadataY + kInspectorSectionGap)
                                             : (metadataSection.bodyStartY + kInspectorSectionGap);
      auto geometrySection = beginInspectorSection(sectionY, "GEOMETRY", cueSectionGeometryOpen_,
                                                   QuickAction::CueSectionGeometryToggle,
                                                   "Collapse/expand geometry controls");
      sectionY = geometrySection.bodyStartY;
      if (cueSectionGeometryOpen_) {
        sectionY = drawGeometryRows(sectionY, *selectedCue, true);
        sectionY = drawColorRows(sectionY, *selectedCue);
      }
      finishInspectorSection(geometrySection, sectionY);

      auto keySection = beginInspectorSection(sectionY, "KEY", cueSectionKeyOpen_,
                                              QuickAction::CueSectionKeyToggle,
                                              "Collapse/expand key controls");
      sectionY = keySection.bodyStartY;
      if (cueSectionKeyOpen_) {
        sectionY = drawKeyRows(sectionY, *selectedCue);
      }
      finishInspectorSection(keySection, sectionY);

    } else if (selectedCue && selectedCue->kind == CueKind::Audio) {
      // Audio-only cue settings
      int ry = ctrlSettingsY + 22 - cueSettingsScroll_;
      constexpr int kRowStep = kInspectorRowStep;
      auto playbackSection = beginInspectorSection(ry, "PLAYBACK", cueSectionPlaybackOpen_,
                                                   QuickAction::CueSectionPlaybackToggle,
                                                   "Audio cue playback settings");
      int playbackY = playbackSection.bodyStartY;
      int volPct = static_cast<int>(std::round((engine ? engine->volume() : 1.0f) * 100.0f));
      if (cueSectionPlaybackOpen_) {
        drawQuickRow(playbackY, "deck fader", QuickAction::VolDec, std::to_string(volPct) + "%", QuickAction::VolInc,
                     QuickAction::ToggleLoop, false, false, "DECK playback level (live, not saved with the cue) - per-cue trim is gain in the AUDIO section");
        playbackY += kRowStep;
        drawQuickRow(playbackY, "fade in", QuickAction::FadeInDec, formatSeconds(selectedCue->fadeInSeconds),
                     QuickAction::FadeInInc, QuickAction::ToggleLoop, false, false, "Fade-in duration");
        playbackY += kRowStep;
        drawQuickRow(playbackY, "fade out", QuickAction::FadeOutDec, formatSeconds(selectedCue->fadeOutSeconds),
                     QuickAction::FadeOutInc, QuickAction::ToggleLoop, false, false, "Fade-out duration");
        playbackY += kRowStep;
        {
          int rx = ctrl.x + 10;
          int ty = playbackY;
          int halfW = (kCtrlW - 24) / 2;
          SDL_Rect loopBtn {rx, ty, halfW, 30};
          SDL_Rect holdBtn {rx + halfW + 4, ty, halfW, 30};
          SDL_Color loopFill = selectedCue->loop ? pal.dark : pal.light;
          SDL_Color holdFill = selectedCue->pauseOnLastFrame ? pal.dark : pal.light;
          SDL_Color loopInk  = selectedCue->loop ? pal.light : pal.deep;
          SDL_Color holdInk  = selectedCue->pauseOnLastFrame ? pal.light : pal.deep;
          drawUIPanel(loopBtn, loopFill, pal.deep, pal.mid);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {loopBtn.x + 6, loopBtn.y, loopBtn.w - 12, loopBtn.h},
                       fitInspectorText(fontSmall_,
                                        std::string("loop: ") + (selectedCue->loop ? "on" : "off"),
                                        loopBtn.w - 16),
                       loopInk);
          quickButtons_.push_back({loopBtn, QuickAction::ToggleLoop, "L — loop this audio"});
          drawUIPanel(holdBtn, holdFill, pal.deep, pal.mid);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {holdBtn.x + 6, holdBtn.y, holdBtn.w - 12, holdBtn.h},
                       fitInspectorText(fontSmall_,
                                        std::string("hold: ") + (selectedCue->pauseOnLastFrame ? "on" : "off"),
                                        holdBtn.w - 16),
                       holdInk);
          quickButtons_.push_back({holdBtn, QuickAction::ToggleHold, "E — hold at end"});
        }
        playbackY += kRowStep;
        {
          SDL_Rect endBtn {ctrl.x + 10, playbackY, kCtrlW - 20, 30};
          drawUIPanel(endBtn, pal.light, pal.deep, pal.mid);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {endBtn.x + 10, endBtn.y, endBtn.w - 20, endBtn.h},
                       fitInspectorText(fontSmall_,
                                        "end: " + cueEndActionLabel(selectedCue->endAction) + "  [X cycle]",
                                        endBtn.w - 24),
                       pal.deep);
          quickButtons_.push_back({endBtn, QuickAction::CycleEndAction, "X — cycle end action"});
        }
        playbackY += kRowStep;
        {
          std::string loopStr = selectedCue->loopCount == 0 ? "inf" : std::to_string(selectedCue->loopCount) + "x";
          drawQuickRow(playbackY, "repeats", QuickAction::LoopCountDec, loopStr, QuickAction::LoopCountInc,
                       QuickAction::ToggleLoop, false, false, "Fixed repeat count — 0 = infinite");
        }
        playbackY += kRowStep;
      }
      finishInspectorSection(playbackSection, playbackY);

      int metadataStartY = cueSectionPlaybackOpen_ ? (playbackY + kInspectorSectionGap)
                                                   : (playbackSection.bodyStartY + kInspectorSectionGap);
      auto metadataSection = beginInspectorSection(metadataStartY, "METADATA", cueSectionMetadataOpen_,
                                                   QuickAction::CueSectionMetadataToggle,
                                                   "Audio cue metadata and pause point controls");
      int metadataY = metadataSection.bodyStartY;
      if (cueSectionMetadataOpen_) {
        metadataY = drawCueTechnicalRows(metadataY, *selectedCue);
        metadataY = drawCueTagRow(metadataY, *selectedCue, "K — cycle cue color tag");
        metadataY = drawInspectorEditableRow(metadataY, "notes",
                                             selectedCue->notes.empty() ? "(no notes)" : selectedCue->notes,
                                             QuickAction::EditNotes,
                                             "Click to edit cue notes",
                                             colorFromRgba(selectedCue->notes.empty() ? kScreenInkSoftColor : kScreenDeepColor));
        metadataY = drawInspectorEditableRow(metadataY, "cue id",
                                             cueDisplayToken(*selectedCue, focusedDeck().selectedIndex),
                                             QuickAction::EditCueNumber,
                                             "Set short cue label for search/goto");
        metadataY = drawPausePointsRow(metadataY, static_cast<int>(selectedCue->pausePoints.size()));
      }
      finishInspectorSection(metadataSection, metadataY);

    } else if (!selectedCue) {
      // Line rects derived from the live font — the old literal 16/24px
      // heights clipped descenders once scaled/HiDPI faces loaded taller.
      int lineH = textLineHeight(fontSmall_);
      int lineGap = 6;
      SDL_Rect emptyRect {ctrl.x + kInspectorInset, ctrlSettingsY + 18,
                          kCtrlW - kInspectorInset * 2, lineH * 2 + lineGap + 28};
      drawUIPanel(emptyRect, pal.light, pal.deep, pal.mid);
      int line1Y = emptyRect.y + 14;
      drawCenteredTextSafe(controlRenderer_, fontSmall_,
                           SDL_Rect {emptyRect.x + 8, line1Y, emptyRect.w - 16, lineH},
                           "NO CUE SELECTED", pal.deep);
      drawCenteredTextSafe(controlRenderer_, fontSmall_,
                           SDL_Rect {emptyRect.x + 8, line1Y + lineH + lineGap, emptyRect.w - 16, lineH},
                           "Select or import a cue to edit it here", pal.dark);
    } else {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {ctrl.x + 10, ctrlSettingsY + 24, kCtrlW - 20, textLineHeight(fontSmall_)},
                   "no per-cue settings for this type",
                   pal.inkSoft);
    }

    SDL_SetRenderClipRect(controlRenderer_, nullptr);
    int settingsContentLogicalBottom = settingsContentTopY;
    for (size_t i = cueSettingsQuickButtonStartIndex_; i < quickButtons_.size(); ++i) {
      settingsContentLogicalBottom = std::max(
        settingsContentLogicalBottom,
        quickButtons_[i].rect.y + quickButtons_[i].rect.h + cueSettingsScroll_);
    }
    int viewportBottom = cueSettingsViewportRect_.y + cueSettingsViewportRect_.h;
    cueSettingsScrollMax_ = std::max(0, settingsContentLogicalBottom - viewportBottom + 6);
    cueSettingsScroll_ = std::clamp(cueSettingsScroll_, 0, cueSettingsScrollMax_);
    if (cueSettingsScrollMax_ > 0 && cueSettingsViewportRect_.h > 10) {
      SDL_Rect rail {
        ctrl.x + kCtrlW - 8,
        cueSettingsViewportRect_.y + 2,
        4,
        cueSettingsViewportRect_.h - 4
      };
      Primitives::fillRect(controlRenderer_, rail, pal.mid);
      int thumbH = std::max(24, (cueSettingsViewportRect_.h * cueSettingsViewportRect_.h) /
                                 std::max(1, cueSettingsViewportRect_.h + cueSettingsScrollMax_));
      thumbH = std::min(thumbH, rail.h);
      int travel = std::max(1, rail.h - thumbH);
      int thumbOffset = static_cast<int>(std::lround(
        static_cast<double>(cueSettingsScroll_) / static_cast<double>(cueSettingsScrollMax_) * travel));
      SDL_Rect thumb {rail.x - 1, rail.y + thumbOffset, rail.w + 2, thumbH};
      Primitives::drawFramedPanel(controlRenderer_, thumb, pal.dark,
                      pal.deep, pal.light);
    }

    // Progress bar tip
    if (pointInRect(mouseX_, mouseY_, progressBarRect_)) {
      drawHoverTip("Click to seek — drag to scrub", progressBarRect_.x + progressBarRect_.w / 2, progressBarRect_.y);
    }
    // Quick button tips
    for (size_t i = 0; i < quickButtons_.size(); ++i) {
      const auto& qb = quickButtons_[i];
      bool isCueSettingsButton = i >= cueSettingsQuickButtonStartIndex_;
      if (isCueSettingsButton && !pointInRect(mouseX_, mouseY_, cueSettingsViewportRect_)) {
        continue;
      }
      if (!qb.tip.empty() && pointInRect(mouseX_, mouseY_, qb.rect)) {
        drawHoverTip(qb.tip, qb.rect.x + qb.rect.w / 2, qb.rect.y);
        break;
      }
    }
  }
