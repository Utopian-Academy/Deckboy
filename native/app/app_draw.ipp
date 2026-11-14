  // ── THE DRAW SOURCE ─────────────────────────────────────────────────────
  //
  // James, 2026-11-04 (board X2N9qJe1): "a source which gives you tools and a
  // pallete to draw on the output, and save what you drew as stills or even
  // possibly animations" -- both a flipbook and stroke replay. The strokes
  // and how they turn into pixels are core/drawing.hpp; the cue renders as
  // the "draw" generator (media_engine.cpp). This file is the brush, the
  // editor on the programme monitor, the inspector's DRAW rows, the saves,
  // and DRAW over the wire.

  struct DrawEditorState {
    bool open = false;
    bool stroking = false;        // a brush or eraser stroke is being drawn
    bool shaping = false;         // a line, box or oval is being dragged
    float shapeX0 = 0, shapeY0 = 0, shapeX1 = 0, shapeY1 = 0;   // fractions, while shaping
    float lastScreenX = 0, lastScreenY = 0;
  };
  DrawEditorState drawEditor_;
  SDL_Rect drawEditBtnRect_ {};
  // The pen: SDL3 reports pressure on its own axis events, and mouse events
  // made from the pen carry SDL_PEN_MOUSEID. A mouse always presses at 1.
  float penPressure_ = 1.0f;
  bool pointerIsPen_ = false;
  // The frame before, faintly, while drawing a flipbook (onion skin).
  deckboy::draw::Raster drawOnionRaster_;
  SDL_Texture* drawOnionTex_ = nullptr;
  int drawOnionTexW_ = 0, drawOnionTexH_ = 0;
  std::string drawOnionKey_;
  bool drawExporting_ = false;    // an animation is being encoded

  // The selected cue when it is a Draw cue.
  Cue* drawCue() {
    Cue* cue = selectedCueMutable();
    return cue && cue->kind == CueKind::Pattern && normalizePatternTypeId(cue->path) == "draw" ? cue : nullptr;
  }
  const Cue* drawCue() const {
    const Cue* cue = selectedCuePtr();
    return cue && cue->kind == CueKind::Pattern && normalizePatternTypeId(cue->path) == "draw" ? cue : nullptr;
  }

  void touchDrawing(Cue& cue) {
    ++cue.drawing.revision;
    markProjectDirty();
  }

  // Where the Draw cue's picture lies on the monitor. Its frame is the
  // output's raster, placed by the cue's own size and position.
  SDL_FRect drawEditArea(const SDL_Rect& monitor) {
    const int out = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
    auto [outW, outH] = project_.outputs.empty() ? std::pair<int, int> {1920, 1080} : outputRenderSizeForOutput(out);
    outW = std::max(1, outW);
    outH = std::max(1, outH);
    const SDL_Rect pic = cuePlacementFor(nullptr, outW, outH, monitor).destination;
    const Cue* cue = drawCue();
    if (!cue) return SDL_FRect {float(pic.x), float(pic.y), float(pic.w), float(pic.h)};
    const SDL_Rect dest = cuePlacementFor(cue, outW, outH, SDL_Rect {0, 0, outW, outH}).destination;
    const float sx = pic.w / static_cast<float>(outW), sy = pic.h / static_cast<float>(outH);
    return SDL_FRect {pic.x + dest.x * sx, pic.y + dest.y * sy, dest.w * sx, dest.h * sy};
  }

  bool drawEditorVisible() const {
    return drawEditor_.open && drawCue() && warpMonitorInner_.w > 0 && warpMonitorInner_.h > 0;
  }

  void openDrawEditor(bool open) {
    if (open && !drawCue()) {
      triggerToast("select a Draw cue first (SOURCE > MADE BY DECKBOY > Draw)");
      return;
    }
    if (open) openMaskEditor(false);   // they share the monitor; one at a time
    if (open) openInspectorSection(QuickAction::CueSectionDrawToggle);   // its controls, in view
    drawEditor_.open = open;
    drawEditor_.stroking = drawEditor_.shaping = false;
    if (open) triggerToast("draw on the monitor: " + std::string(deckboy::draw::toolLabel(project_.drawBrush.tool)));
  }

  // The brush's size and opacity this moment, through their oscillators.
  float brushSizeNow() const {
    const auto& b = project_.drawBrush;
    using deckboy::draw::kMaxBrushSize;
    using deckboy::draw::kMinBrushSize;
    const float at = (b.size - kMinBrushSize) / (kMaxBrushSize - kMinBrushSize);
    const float out = deckboy::effects::lfoApply(project_.drawBrushLfo[0], std::clamp(at, 0.0f, 1.0f), lfoSeconds_,
                                                 lfoBeats_, reactiveAudioLevel_);
    return kMinBrushSize + out * (kMaxBrushSize - kMinBrushSize);
  }
  float brushOpacityNow() const {
    return deckboy::effects::lfoApply(project_.drawBrushLfo[1], std::clamp(project_.drawBrush.opacity, 0.0f, 1.0f),
                                      lfoSeconds_, lfoBeats_, reactiveAudioLevel_);
  }

  static float drawClockNow() { return static_cast<float>(SDL_GetTicks() / 1000.0); }

  deckboy::draw::Frame* drawEditFrame(Cue& cue) {
    auto& d = cue.drawing;
    if (d.frames.empty()) d.frames.emplace_back();
    d.editFrame = std::clamp(d.editFrame, 0, static_cast<int>(d.frames.size()) - 1);
    return &d.frames[static_cast<std::size_t>(d.editFrame)];
  }

  // A stroke as the brush says, with its first point. Fill is whole at once.
  void startDrawStroke(Cue& cue, float fx, float fy) {
    pushUndoSnapshot();
    const auto& b = project_.drawBrush;
    deckboy::draw::Stroke s;
    s.tool = b.tool;
    s.rgba = b.rgba;
    s.size = brushSizeNow();
    s.opacity = brushOpacityNow();
    s.points.push_back({fx, fy, pointerIsPen_ ? penPressure_ : 1.0f, drawClockNow()});
    drawEditFrame(cue)->strokes.push_back(std::move(s));
    touchDrawing(cue);
  }

  void addDrawPoint(Cue& cue, float fx, float fy) {
    auto* frame = drawEditFrame(cue);
    if (frame->strokes.empty()) return;
    frame->strokes.back().points.push_back({fx, fy, pointerIsPen_ ? penPressure_ : 1.0f, drawClockNow()});
    touchDrawing(cue);
  }

  // ── THE MOUSE (and the pen, which SDL3 also reports as the mouse) ────────

  bool handleDrawEditorMouseDown(int x, int y) {
    if (!drawEditorVisible() || !pointInRect(x, y, warpMonitorInner_)) return false;
    Cue* cue = drawCue();
    const SDL_FRect area = drawEditArea(warpMonitorInner_);
    if (!cue || area.w < 2 || area.h < 2) return true;
    const float fx = (x - area.x) / area.w, fy = (y - area.y) / area.h;
    const auto tool = project_.drawBrush.tool;
    if (tool == deckboy::draw::Tool::Fill) {
      startDrawStroke(*cue, fx, fy);
      return true;
    }
    if (tool == deckboy::draw::Tool::Line || tool == deckboy::draw::Tool::Box || tool == deckboy::draw::Tool::Oval) {
      drawEditor_.shaping = true;
      drawEditor_.shapeX0 = drawEditor_.shapeX1 = fx;
      drawEditor_.shapeY0 = drawEditor_.shapeY1 = fy;
      return true;
    }
    startDrawStroke(*cue, fx, fy);
    drawEditor_.stroking = true;
    drawEditor_.lastScreenX = static_cast<float>(x);
    drawEditor_.lastScreenY = static_cast<float>(y);
    return true;
  }

  bool handleDrawEditorMouseMotion(int x, int y) {
    if (!drawEditor_.stroking && !drawEditor_.shaping) return false;
    Cue* cue = drawCue();
    const SDL_FRect area = drawEditArea(warpMonitorInner_);
    if (!cue || area.w < 2 || area.h < 2) return true;
    const float fx = (x - area.x) / area.w, fy = (y - area.y) / area.h;
    if (drawEditor_.shaping) {
      drawEditor_.shapeX1 = fx;
      drawEditor_.shapeY1 = fy;
      return true;
    }
    // A point every pixel and a half on the monitor: smooth, and not a
    // thousand points for a short line.
    const float dx = x - drawEditor_.lastScreenX, dy = y - drawEditor_.lastScreenY;
    if (dx * dx + dy * dy < 2.25f) return true;
    drawEditor_.lastScreenX = static_cast<float>(x);
    drawEditor_.lastScreenY = static_cast<float>(y);
    addDrawPoint(*cue, fx, fy);
    return true;
  }

  void endDrawEditorDrag() {
    if (drawEditor_.shaping) {
      drawEditor_.shaping = false;
      Cue* cue = drawCue();
      const float dx = drawEditor_.shapeX1 - drawEditor_.shapeX0, dy = drawEditor_.shapeY1 - drawEditor_.shapeY0;
      if (cue && dx * dx + dy * dy > 1e-6f) {
        startDrawStroke(*cue, drawEditor_.shapeX0, drawEditor_.shapeY0);
        addDrawPoint(*cue, drawEditor_.shapeX1, drawEditor_.shapeY1);
      }
    }
    drawEditor_.stroking = false;
  }

  // SDL3's pen pressure, from the event loop.
  void noteDrawPenEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_PEN_AXIS && event.paxis.axis == SDL_PEN_AXIS_PRESSURE) {
      penPressure_ = std::clamp(event.paxis.value, 0.0f, 1.0f);
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
      pointerIsPen_ = event.motion.which == SDL_PEN_MOUSEID;
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      pointerIsPen_ = event.button.which == SDL_PEN_MOUSEID;
    }
  }

  bool handleDrawEditorKey(SDL_Keycode key) {
    if (!drawEditorVisible()) return false;
    if (key == SDLK_ESCAPE) {
      openDrawEditor(false);
      return true;
    }
    return false;
  }

  // ── WHAT IS DRAWN OVER THE MONITOR ──────────────────────────────────────

  void drawDrawEditorOverlay() {
    if (!drawEditorVisible()) return;
    const Cue* cue = drawCue();
    const SDL_FRect area = drawEditArea(warpMonitorInner_);
    // The frame before, faintly, for a flipbook.
    if (cue && project_.drawBrush.onion && cue->drawing.editFrame > 0 &&
        cue->drawing.editFrame < static_cast<int>(cue->drawing.frames.size())) {
      const int w = std::max(16, static_cast<int>(area.w)), h = std::max(9, static_cast<int>(area.h));
      const std::string key = cue->id + "|" + std::to_string(cue->drawing.revision) + "|" +
                              std::to_string(cue->drawing.editFrame) + "|" + std::to_string(w) + "x" + std::to_string(h);
      if (key != drawOnionKey_) {
        const auto& before = cue->drawing.frames[static_cast<std::size_t>(cue->drawing.editFrame - 1)];
        drawOnionRaster_.update(before, before.strokes.size(),
                                before.strokes.empty() ? 0 : before.strokes.back().points.size(), w, h,
                                cue->drawing.revision, cue->drawing.editFrame - 1);
        if (!drawOnionTex_ || drawOnionTexW_ != w || drawOnionTexH_ != h) {
          if (drawOnionTex_) SDL_DestroyTexture(drawOnionTex_);
          drawOnionTex_ = deckboyCreateTexture(controlRenderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
          drawOnionTexW_ = w;
          drawOnionTexH_ = h;
          if (drawOnionTex_) SDL_SetTextureBlendMode(drawOnionTex_, SDL_BLENDMODE_BLEND);
        }
        if (drawOnionTex_ && drawOnionRaster_.width() == w) {
          SDL_UpdateTexture(drawOnionTex_, nullptr, drawOnionRaster_.pixels().data(), w * 4);
        }
        drawOnionKey_ = key;
      }
      if (drawOnionTex_) {
        SDL_SetTextureAlphaMod(drawOnionTex_, 80);
        SDL_RenderTexture(controlRenderer_, drawOnionTex_, nullptr, &area);
      }
    }
    // A line, box or oval being dragged, as a thin outline until let go.
    if (drawEditor_.shaping) {
      const SDL_Color ink = SDL_Color {static_cast<Uint8>(project_.drawBrush.rgba >> 24),
                                       static_cast<Uint8>(project_.drawBrush.rgba >> 16),
                                       static_cast<Uint8>(project_.drawBrush.rgba >> 8), 255};
      SDL_SetRenderDrawColor(controlRenderer_, ink.r, ink.g, ink.b, 255);
      const float ax = area.x + drawEditor_.shapeX0 * area.w, ay = area.y + drawEditor_.shapeY0 * area.h;
      const float bx = area.x + drawEditor_.shapeX1 * area.w, by = area.y + drawEditor_.shapeY1 * area.h;
      const auto tool = project_.drawBrush.tool;
      if (tool == deckboy::draw::Tool::Line) {
        SDL_RenderLine(controlRenderer_, ax, ay, bx, by);
      } else if (tool == deckboy::draw::Tool::Box) {
        const SDL_FRect r {std::min(ax, bx), std::min(ay, by), std::abs(bx - ax), std::abs(by - ay)};
        SDL_RenderRect(controlRenderer_, &r);
      } else {
        const float cx = (ax + bx) / 2, cy = (ay + by) / 2, rx = std::abs(bx - ax) / 2, ry = std::abs(by - ay) / 2;
        for (int i = 0; i < 64; ++i) {
          const float t0 = 6.2831853f * i / 64, t1 = 6.2831853f * (i + 1) / 64;
          SDL_RenderLine(controlRenderer_, cx + rx * std::cos(t0), cy + ry * std::sin(t0), cx + rx * std::cos(t1),
                         cy + ry * std::sin(t1));
        }
      }
    }
    // A banner saying what the mouse does here.
    const int bannerH = textLineHeight(fontSmall_) + uiScaled(8);
    const SDL_Rect banner {warpMonitorInner_.x, warpMonitorInner_.y, warpMonitorInner_.w, bannerH};
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    Primitives::fillRect(controlRenderer_, banner, SDL_Color {0, 0, 0, 170});
    std::string text = "DRAW: " + std::string(deckboy::draw::toolLabel(project_.drawBrush.tool));
    if (cue && cue->drawing.frames.size() > 1) {
      text += "  -  frame " + std::to_string(cue->drawing.editFrame + 1) + " of " +
              std::to_string(cue->drawing.frames.size());
    }
    text += "  -  drag on the picture. Esc stops drawing";
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {banner.x + uiScaled(6), banner.y, banner.w - uiScaled(12), banner.h},
                 ellipsizeToPixelWidth(fontSmall_, text, banner.w - uiScaled(12)), SDL_Color {255, 255, 255, 255});
  }

  // ── THE INSPECTOR'S DRAW ROWS ───────────────────────────────────────────

  int drawDrawRows(const InspectorCtx& ix, int y) {
    Cue* cue = drawCue();
    if (!cue) return y;
    const auto& b = project_.drawBrush;
    const auto& d = cue->drawing;
    y = inspDrawChoiceRow(ix, y, "draw", drawEditor_.open ? "on the monitor" : "draw on the monitor",
                          QuickAction::DrawEditToggle,
                          "Draw on the programme monitor with the mouse or a pen (DRAW). Esc stops", -1,
                          drawEditor_.open);
    // The six tools in one row of chips, then the eight colours.
    auto chipRow = [&](int count, auto&& draw) {
      const int x0 = ix.ctrl.x + ix.inset;
      const int w = ix.ctrlW - ix.inset * 2;
      const int gap = uiScaled(3);
      const int cw = std::max(1, (w - gap * (count - 1)) / count);
      for (int i = 0; i < count; ++i) draw(i, SDL_Rect {x0 + i * (cw + gap), y, cw, ix.rowH});
      y += ix.rowStep;
    };
    static const char* kToolShort[] = {"brush", "erase", "line", "box", "oval", "fill"};
    chipRow(static_cast<int>(deckboy::draw::Tool::Count), [&](int i, const SDL_Rect& r) {
      const bool on = static_cast<int>(b.tool) == i;
      drawUIPanel(r, paletteToggleFill(on), pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, r, kToolShort[i], paletteToggleInk(on));
      quickButtons_.push_back({r, QuickAction::DrawToolSet,
                               std::string("Draw with the ") + deckboy::draw::toolLabel(static_cast<deckboy::draw::Tool>(i)),
                               i});
    });
    chipRow(9, [&](int i, const SDL_Rect& r) {
      if (i == 8) {
        // The ninth chip: any colour, typed.
        drawUIPanel(r, pal.tile, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, r, "#", pal.fg);
        quickButtons_.push_back({r, QuickAction::DrawColourCustom, "Type a colour: #rrggbb, or a name", -1});
        return;
      }
      const std::uint32_t c = deckboy::draw::kPalette[i];
      const SDL_Color fill {static_cast<Uint8>(c >> 24), static_cast<Uint8>(c >> 16), static_cast<Uint8>(c >> 8), 255};
      const bool on = (b.rgba | 0xFFu) == c;
      Primitives::fillRect(controlRenderer_, r, fill);
      if (on) {
        // A frame round the colour in hand, in the toggle's own ink.
        const SDL_Color ring = paletteToggleInk(false);
        const int t = std::max(2, uiScaled(2));
        Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y, r.w, t}, ring);
        Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y + r.h - t, r.w, t}, ring);
        Primitives::fillRect(controlRenderer_, SDL_Rect {r.x, r.y, t, r.h}, ring);
        Primitives::fillRect(controlRenderer_, SDL_Rect {r.x + r.w - t, r.y, t, r.h}, ring);
      }
      quickButtons_.push_back({r, QuickAction::DrawColourSet, "Draw in this colour", i});
    });
    char text[48];
    std::snprintf(text, sizeof text, "%.1f%%", b.size * 100.0f);
    inspDrawQuickRow(ix, y, "size", QuickAction::NumericParamDec, text, QuickAction::NumericParamInc,
                     QuickAction::ToggleLoop, false, false, "The brush's width, as % of the picture's height", true,
                     QuickAction::EditNumericParam, static_cast<int>(NumericParam::DrawSize), QuickAction::EffectLfoToggle,
                     project_.drawBrushLfo[0].on, kDrawLfoPackBase + 0, "~", "Put the brush's size on an oscillator");
    y += ix.rowStep;
    if (project_.drawBrushLfo[0].on) y = inspDrawLfoRowFor(ix, y, project_.drawBrushLfo[0], kDrawLfoPackBase + 0);
    std::snprintf(text, sizeof text, "%.0f%%", b.opacity * 100.0f);
    inspDrawQuickRow(ix, y, "opacity", QuickAction::NumericParamDec, text, QuickAction::NumericParamInc,
                     QuickAction::ToggleLoop, false, false, "How solid the ink is", true, QuickAction::EditNumericParam,
                     static_cast<int>(NumericParam::DrawOpacity), QuickAction::EffectLfoToggle,
                     project_.drawBrushLfo[1].on, kDrawLfoPackBase + 1, "~", "Put the brush's opacity on an oscillator");
    y += ix.rowStep;
    if (project_.drawBrushLfo[1].on) y = inspDrawLfoRowFor(ix, y, project_.drawBrushLfo[1], kDrawLfoPackBase + 1);

    // Frames: which one, and the frames around it.
    const std::string frameText = "frame " + std::to_string(d.editFrame + 1) + " of " + std::to_string(d.frames.size());
    inspDrawQuickRow(ix, y, "frame", QuickAction::DrawFramePrev, frameText, QuickAction::DrawFrameNext,
                     QuickAction::ToggleLoop, false, false, "Which frame you draw on");
    y += ix.rowStep;
    chipRow(3, [&](int i, const SDL_Rect& r) {
      static const char* kLabels[] = {"+ frame", "- frame", ""};
      if (i == 2) {
        drawUIPanel(r, paletteToggleFill(b.onion), pal.deep, pal.light);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, r, b.onion ? "onion on" : "onion off", paletteToggleInk(b.onion));
        quickButtons_.push_back({r, QuickAction::DrawOnionToggle, "Show the frame before, faintly, while drawing", -1});
        return;
      }
      drawUIPanel(r, pal.tile, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, r, kLabels[i], pal.fg);
      quickButtons_.push_back({r, i == 0 ? QuickAction::DrawFrameAdd : QuickAction::DrawFrameDelete,
                               i == 0 ? "A new empty frame after this one" : "Take this frame out", -1});
    });
    static const char* kPlayback[] = {"still: this frame", "flipbook: every frame", "replay: as it was drawn"};
    y = inspDrawChoiceRow(ix, y, "plays", kPlayback[static_cast<int>(d.playback)], QuickAction::DrawPlaybackCycle,
                          "Still holds the frame you are on. Flipbook plays the frames in turn. Replay draws the "
                          "first frame stroke by stroke, the way it was drawn, then holds and starts again");
    if (d.playback == deckboy::draw::Playback::Flipbook) {
      std::snprintf(text, sizeof text, "%.0f fps", d.fps);
      inspDrawQuickRow(ix, y, "fps", QuickAction::NumericParamDec, text, QuickAction::NumericParamInc,
                       QuickAction::ToggleLoop, false, false, "Frames a second", true, QuickAction::EditNumericParam,
                       static_cast<int>(NumericParam::DrawFps));
      y += ix.rowStep;
    } else if (d.playback == deckboy::draw::Playback::Replay) {
      std::snprintf(text, sizeof text, "%.1f s", d.holdSeconds);
      inspDrawQuickRow(ix, y, "hold", QuickAction::NumericParamDec, text, QuickAction::NumericParamInc,
                       QuickAction::ToggleLoop, false, false, "How long the finished drawing stays before it is drawn "
                       "again", true, QuickAction::EditNumericParam, static_cast<int>(NumericParam::DrawHold));
      y += ix.rowStep;
    }
    chipRow(2, [&](int i, const SDL_Rect& r) {
      drawUIPanel(r, pal.tile, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, r, i == 0 ? "undo stroke" : "clear frame", pal.fg);
      quickButtons_.push_back({r, i == 0 ? QuickAction::DrawUndoStroke : QuickAction::DrawClearFrame,
                               i == 0 ? "Take the last stroke back" : "Wipe this frame", -1});
    });
    chipRow(2, [&](int i, const SDL_Rect& r) {
      drawUIPanel(r, pal.tile, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, r,
                           i == 0 ? "save still" : (drawExporting_ ? "saving..." : "save animation"), pal.fg);
      quickButtons_.push_back({r, i == 0 ? QuickAction::DrawSaveStill : QuickAction::DrawSaveAnimation,
                               i == 0 ? "This frame as a PNG, added to the playlist as an image cue"
                                      : "The flipbook or replay as a video with transparency, added as a video cue",
                               -1});
    });
    y = inspDrawMessageRow(ix, y, "transparent outside the ink: put it on a layer", pal.tile, pal.fgSoft);
    return y;
  }

  // ── ACTIONS ─────────────────────────────────────────────────────────────

  bool handleDrawQuickAction(QuickAction action, int param) {
    switch (action) {
      case QuickAction::DrawEditToggle: openDrawEditor(!drawEditor_.open); return true;
      case QuickAction::CueSectionDrawToggle: cueSectionDrawOpen_ = !cueSectionDrawOpen_; return true;
      case QuickAction::DrawToolSet:
        if (param >= 0 && param < static_cast<int>(deckboy::draw::Tool::Count)) {
          project_.drawBrush.tool = static_cast<deckboy::draw::Tool>(param);
          markProjectDirty();
          triggerToast(std::string("draw: ") + deckboy::draw::toolLabel(project_.drawBrush.tool));
        }
        return true;
      case QuickAction::DrawColourSet:
        if (param >= 0 && param < 8) {
          project_.drawBrush.rgba = deckboy::draw::kPalette[param];
          markProjectDirty();
        }
        return true;
      case QuickAction::DrawColourCustom: {
        char hex[16];
        std::snprintf(hex, sizeof hex, "#%06x", static_cast<unsigned>(project_.drawBrush.rgba >> 8));
        openInlineTextEditor("draw_colour", "Draw colour", "#rrggbb, or a name (red, teal...)", hex,
                             [this](const std::string& value) {
                               std::uint32_t rgba = 0;
                               if (parseDrawColour(value, rgba)) {
                                 project_.drawBrush.rgba = rgba;
                                 markProjectDirty();
                               } else {
                                 triggerToast("not a colour: " + value, ToastKind::Warning, kToastReadableMs);
                               }
                             });
        return true;
      }
      case QuickAction::DrawOnionToggle:
        project_.drawBrush.onion = !project_.drawBrush.onion;
        markProjectDirty();
        return true;
      default: break;
    }
    Cue* cue = drawCue();
    switch (action) {
      case QuickAction::DrawFramePrev:
      case QuickAction::DrawFrameNext:
        if (cue) {
          auto& d = cue->drawing;
          d.editFrame = std::clamp(d.editFrame + (action == QuickAction::DrawFrameNext ? 1 : -1), 0,
                                   static_cast<int>(d.frames.size()) - 1);
          touchDrawing(*cue);
        }
        return true;
      case QuickAction::DrawFrameAdd:
        if (cue) {
          pushUndoSnapshot();
          auto& d = cue->drawing;
          d.frames.insert(d.frames.begin() + std::clamp(d.editFrame + 1, 0, static_cast<int>(d.frames.size())),
                          deckboy::draw::Frame {});
          ++d.editFrame;
          touchDrawing(*cue);
          triggerToast("frame " + std::to_string(d.editFrame + 1) + " of " + std::to_string(d.frames.size()));
        }
        return true;
      case QuickAction::DrawFrameDelete:
        if (cue) {
          auto& d = cue->drawing;
          pushUndoSnapshot();
          if (d.frames.size() <= 1) {
            d.frames[0].strokes.clear();
          } else {
            d.frames.erase(d.frames.begin() + d.editFrame);
            d.editFrame = std::min(d.editFrame, static_cast<int>(d.frames.size()) - 1);
          }
          touchDrawing(*cue);
        }
        return true;
      case QuickAction::DrawPlaybackCycle:
        if (cue) {
          auto& d = cue->drawing;
          d.playback = static_cast<deckboy::draw::Playback>((static_cast<int>(d.playback) + 1) %
                                                             static_cast<int>(deckboy::draw::Playback::Count));
          touchDrawing(*cue);
        }
        return true;
      case QuickAction::DrawUndoStroke:
        if (cue && !drawEditFrame(*cue)->strokes.empty()) {
          pushUndoSnapshot();
          drawEditFrame(*cue)->strokes.pop_back();
          touchDrawing(*cue);
        }
        return true;
      case QuickAction::DrawClearFrame:
        if (cue && !drawEditFrame(*cue)->strokes.empty()) {
          pushUndoSnapshot();
          drawEditFrame(*cue)->strokes.clear();
          touchDrawing(*cue);
        }
        return true;
      case QuickAction::DrawSaveStill: {
        std::string path, error;
        if (cue && saveDrawStill(*cue, std::string(), path, error)) {
          importPaths({path});
          triggerToast("still saved and added: " + fs::path(path).filename().string());
        } else if (cue) {
          triggerToast("still not saved: " + error, ToastKind::Warning, kToastReadableMs);
        }
        return true;
      }
      case QuickAction::DrawSaveAnimation:
        if (cue) startDrawAnimationExport(*cue, std::string(), true);
        return true;
      default:
        return false;
    }
  }

  static bool parseDrawColour(const std::string& raw, std::uint32_t& rgba) {
    std::string s = trim(raw);
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if ((s.size() == 6 || s.size() == 8) && s.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
      const unsigned long v = std::strtoul(s.c_str(), nullptr, 16);
      rgba = s.size() == 6 ? (static_cast<std::uint32_t>(v) << 8) | 0xFFu : static_cast<std::uint32_t>(v);
      return true;
    }
    static const std::pair<const char*, std::uint32_t> kNames[] = {
      {"white", 0xFFFFFFFFu}, {"black", 0x101010FFu}, {"red", 0xE03030FFu}, {"orange", 0xF08020FFu},
      {"yellow", 0xF0E040FFu}, {"green", 0x30C060FFu}, {"blue", 0x3080F0FFu}, {"purple", 0xB050E0FFu},
      {"pink", 0xF070B0FFu}, {"teal", 0x20A0A0FFu}, {"grey", 0x808080FFu}, {"gray", 0x808080FFu},
    };
    const std::string lower = toLower(s);
    for (const auto& [name, value] : kNames) {
      if (lower == name) {
        rgba = value;
        return true;
      }
    }
    return false;
  }

  // ── SAVING ──────────────────────────────────────────────────────────────

  // Beside the show, so a bundled show carries them; the state folder when
  // no show is open.
  fs::path drawingsDir() const {
    const fs::path base = (!currentProjectFile_.empty() && currentProjectFile_.has_parent_path())
                            ? currentProjectFile_.parent_path() : Paths::stateDir();
    return base / "drawings";
  }

  std::string drawFileStem(const Cue& cue) const {
    std::string name;
    for (char c : cue.name.empty() ? std::string("drawing") : cue.name) {
      name += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    }
    return name + "-" + std::to_string(static_cast<long long>(std::time(nullptr)));
  }

  std::pair<int, int> drawRasterSize() const {
    if (project_.outputs.empty()) return {1920, 1080};
    const int out = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    auto size = outputRenderSizeForOutput(out);
    return {std::max(16, size.first), std::max(16, size.second)};
  }

  static bool writeRgbaPng(const std::string& path, const std::vector<std::uint8_t>& rgba, int w, int h,
                           std::string& error) {
    std::string png;
    stbi_write_png_to_func(
      [](void* sink, void* data, int size) {
        static_cast<std::string*>(sink)->append(static_cast<const char*>(data), static_cast<std::size_t>(size));
      },
      &png, w, h, 4, rgba.data(), w * 4);
    std::ofstream file(fs::path(path), std::ios::binary | std::ios::trunc);
    if (png.empty() || !file) {
      error = "cannot write " + path;
      return false;
    }
    file.write(png.data(), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(file);
  }

  // The frame being drawn on, at the output's raster, as a PNG with
  // transparency. An empty `path` means a new file in drawings/.
  bool saveDrawStill(const Cue& cue, std::string path, std::string& written, std::string& error) {
    const auto [w, h] = drawRasterSize();
    const auto& d = cue.drawing;
    if (d.frames.empty()) {
      error = "nothing drawn";
      return false;
    }
    const auto& frame = d.frames[static_cast<std::size_t>(std::clamp(d.editFrame, 0, static_cast<int>(d.frames.size()) - 1))];
    deckboy::draw::Raster raster;
    raster.update(frame, frame.strokes.size(), frame.strokes.empty() ? 0 : frame.strokes.back().points.size(), w, h,
                  d.revision, d.editFrame);
    if (path.empty()) {
      std::error_code ec;
      fs::create_directories(drawingsDir(), ec);
      path = (drawingsDir() / (drawFileStem(cue) + ".png")).string();
    }
    if (!writeRgbaPng(path, raster.pixels(), w, h, error)) return false;
    written = path;
    return true;
  }

  // The drawing as it plays, rendered frame by frame and encoded by ffmpeg
  // as ProRes 4444 with alpha. Off the main thread: an encode takes seconds.
  // `addCue` puts the finished file on the focused playlist.
  void startDrawAnimationExport(const Cue& cue, std::string path, bool addCue) {
    if (drawExporting_) {
      triggerToast("an animation is already being saved");
      return;
    }
    const auto [w, h] = drawRasterSize();
    const deckboy::draw::Drawing d = cue.drawing;
    // Flipbook: every frame once at its rate. Replay: the drawing as it is
    // drawn, then its hold, at 25 frames a second. Still: one second of the
    // frame being drawn on.
    double fps = 25.0, seconds = 1.0;
    if (d.playback == deckboy::draw::Playback::Flipbook) {
      fps = std::clamp(static_cast<double>(d.fps), 1.0, 60.0);
      seconds = d.frames.size() / fps;
    } else if (d.playback == deckboy::draw::Playback::Replay && !d.frames.empty()) {
      std::vector<std::vector<float>> times;
      seconds = deckboy::draw::replayTimes(d.frames[0], times) + std::max(0.0f, d.holdSeconds);
      seconds = std::max(seconds, 1.0 / fps);
    }
    const int count = std::clamp(static_cast<int>(std::lround(seconds * fps)), 1, 3000);
    if (path.empty()) {
      std::error_code ec;
      fs::create_directories(drawingsDir(), ec);
      path = (drawingsDir() / (drawFileStem(cue) + ".mov")).string();
    }
    drawExporting_ = true;
    triggerToast("saving the animation: " + std::to_string(count) + " frames");
    App* app = this;
    std::thread([app, d, w = w, h = h, fps, count, path, addCue]() {
      // Raw frames straight into ffmpeg's stdin: no picture files between,
      // which at 4K would be most of the time spent.
      std::string error;
      char rate[32];
      std::snprintf(rate, sizeof rate, "%g", fps);
      const std::string size = std::to_string(w) + "x" + std::to_string(h);
      ChildProcess ffmpeg;
      bool ok = spawnProcess(ffmpeg, {"ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgba", "-s", size,
                                      "-framerate", rate, "-i", "-", "-c:v", "prores_ks", "-profile:v", "4444",
                                      "-pix_fmt", "yuva444p10le", "-alpha_bits", "16", path},
                             SpawnOptions::pipedStdin()) && ffmpeg.writeFd >= 0;
      if (!ok) error = "could not start ffmpeg";
      deckboy::draw::Raster raster;
      for (int i = 0; i < count && ok; ++i) {
        const auto m = deckboy::draw::momentAt(d, i / fps);
        const auto& frame = d.frames[static_cast<std::size_t>(std::clamp(m.frame, 0, static_cast<int>(d.frames.size()) - 1))];
        raster.update(frame, m.strokes, m.lastPoints, w, h, d.revision, m.frame);
        ok = writeAllToFd(ffmpeg.writeFd, raster.pixels().data(), raster.pixels().size());
        if (!ok) error = "ffmpeg stopped taking frames";
      }
      ffmpeg.finish(120000);
      std::error_code exists;
      if (ok) {
        ok = fs::exists(fs::path(path), exists) && fs::file_size(fs::path(path), exists) > 0;
        if (!ok) error = "ffmpeg made no file";
      }
      std::lock_guard<std::mutex> lock(app->sdlDialogMutex_);
      app->sdlDialogActions_.emplace_back([app, ok, path, error, addCue]() {
        app->drawExporting_ = false;
        app->drawExportResult_ = ok ? "saved " + path : "failed: " + error;
        if (!ok) {
          app->triggerToast("animation not saved: " + error, ToastKind::Warning, kToastReadableMs);
          return;
        }
        if (addCue) app->importPaths({path});
        app->triggerToast("animation saved" + std::string(addCue ? " and added: " : ": ") +
                          fs::path(path).filename().string());
      });
    }).detach();
  }
  std::string drawExportResult_;   // what the last animation save did, for DRAW

  // ── THE BRUSH'S NUMBERS (size, opacity) ─────────────────────────────────
  //
  // The brush is the show's, so its rows edit one value however many cues are
  // selected, the way the mask's rows do.
  bool drawBrushParam(NumericParam id) const {
    return id == NumericParam::DrawSize || id == NumericParam::DrawOpacity;
  }
  double drawBrushShown(NumericParam id) const {
    return id == NumericParam::DrawSize ? project_.drawBrush.size * 100.0 : project_.drawBrush.opacity * 100.0;
  }
  void setDrawBrushShown(NumericParam id, double shown, const NumericParamSpec& spec) {
    const double v = std::clamp(shown, spec.lo, spec.hi);
    if (id == NumericParam::DrawSize) project_.drawBrush.size = static_cast<float>(v / 100.0);
    else project_.drawBrush.opacity = static_cast<float>(v / 100.0);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(spec.precision) << v;
    triggerToast(std::string(spec.label) + " " + ss.str() + "%");
    markProjectDirty();
  }

  // ── DRAW over the wire ──────────────────────────────────────────────────
  //
  //   DRAW                                 the selected Draw cue and the brush
  //   DRAW EDITOR ON|OFF
  //   DRAW TOOL brush|erase|line|box|oval|fill
  //   DRAW COLOUR <#rrggbb[aa]|name|1-8>  DRAW SIZE <% of height>  DRAW OPACITY <%>
  //   DRAW STROKE x,y[,pressure] x,y ...  percentages; a stroke with the brush
  //   DRAW UNDO | CLEAR | FRAME ADD|DELETE|NEXT|PREV|<n> | ONION ON|OFF
  //   DRAW PLAYBACK still|flipbook|replay | FPS <n> | HOLD <s>
  //   DRAW SAVE STILL|ANIMATION [file]     local only; adds the saved file as a cue
  void handleDrawCommand(const std::vector<std::string>& parts) {
    const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();
    auto& b = project_.drawBrush;
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
    if (sub == "TOOL") {
      deckboy::draw::Tool tool;
      if (parts.size() < 3 || !deckboy::draw::toolFromToken(toLower(parts[2]), tool)) {
        failRemoteCommand("DRAW TOOL: brush, erase, line, box, oval or fill");
        return;
      }
      b.tool = tool;
      markProjectDirty();
      remoteCommandDetail_ = deckboy::draw::toolToken(tool);
      return;
    }
    if (sub == "COLOUR" || sub == "COLOR") {
      std::uint32_t rgba = 0;
      double n = 0;
      if (number(2, 1, 8, n) && n == std::floor(n)) {
        rgba = deckboy::draw::kPalette[static_cast<int>(n) - 1];
      } else if (parts.size() < 3 || !parseDrawColour(parts[2], rgba)) {
        failRemoteCommand("DRAW COLOUR: #rrggbb, #rrggbbaa, a name, or 1-8 for the palette");
        return;
      }
      b.rgba = rgba;
      markProjectDirty();
      char hex[16];
      std::snprintf(hex, sizeof hex, "#%08x", static_cast<unsigned>(rgba));
      remoteCommandDetail_ = hex;
      return;
    }
    if (sub == "SIZE" || sub == "OPACITY") {
      double v = 0;
      if (!number(2, sub == "SIZE" ? 0.1 : 1.0, sub == "SIZE" ? 20.0 : 100.0, v)) {
        failRemoteCommand(sub == "SIZE" ? "DRAW SIZE: 0.1-20 (% of the picture's height)" : "DRAW OPACITY: 1-100 (%)");
        return;
      }
      (sub == "SIZE" ? b.size : b.opacity) = static_cast<float>(v / 100.0);
      markProjectDirty();
      remoteCommandDetail_ = sub + " " + parts[2];
      return;
    }
    if (sub == "ONION") {
      const std::string state = parts.size() > 2 ? toUpper(parts[2]) : std::string();
      if (state != "ON" && state != "OFF") {
        failRemoteCommand("DRAW ONION: ON or OFF");
        return;
      }
      b.onion = state == "ON";
      markProjectDirty();
      remoteCommandDetail_ = "onion " + state;
      return;
    }
    Cue* cue = drawCue();
    if (sub.empty()) {
      std::ostringstream out;
      out << "tool=" << deckboy::draw::toolToken(b.tool) << " size=" << b.size * 100.0f << " editor="
          << (drawEditor_.open ? "on" : "off");
      if (cue) {
        std::size_t strokes = 0;
        for (const auto& f : cue->drawing.frames) strokes += f.strokes.size();
        out << " frames=" << cue->drawing.frames.size() << " frame=" << cue->drawing.editFrame + 1
            << " strokes=" << strokes << " here=" << cue->drawing.frames[static_cast<std::size_t>(
                 std::clamp(cue->drawing.editFrame, 0, static_cast<int>(cue->drawing.frames.size()) - 1))].strokes.size()
            << " plays=" << deckboy::draw::playbackToken(cue->drawing.playback);
      } else {
        out << " cue=none";
      }
      if (drawEditorVisible()) {
        const SDL_FRect a = drawEditArea(warpMonitorInner_);
        out << " area=" << static_cast<int>(a.x) << "," << static_cast<int>(a.y) << "," << static_cast<int>(a.w) << ","
            << static_cast<int>(a.h);
      }
      if (!drawExportResult_.empty()) out << " last_save=\"" << drawExportResult_ << "\"";
      if (drawExporting_) out << " saving=1";
      remoteCommandDetail_ = out.str();
      return;
    }
    if (sub == "EDITOR") {
      const std::string state = parts.size() > 2 ? toUpper(parts[2]) : std::string();
      if (state != "ON" && state != "OFF") {
        failRemoteCommand("DRAW EDITOR: ON or OFF");
        return;
      }
      openDrawEditor(state == "ON");
      if (state == "ON" && !drawEditor_.open) {
        failRemoteCommand("DRAW EDITOR: select a Draw cue first");
        return;
      }
      remoteCommandDetail_ = drawEditor_.open ? "on" : "off";
      return;
    }
    if (!cue) {
      failRemoteCommand("DRAW " + sub + ": select a Draw cue first");
      return;
    }
    auto& d = cue->drawing;
    if (sub == "STROKE") {
      if (parts.size() < 3) {
        failRemoteCommand("DRAW STROKE: x,y[,pressure] ... in percent");
        return;
      }
      std::vector<deckboy::draw::Point> points;
      float t = drawClockNow();
      for (std::size_t i = 2; i < parts.size(); ++i) {
        float v[3] = {0, 0, 100};
        int got = 0;
        const char* c = parts[i].c_str();
        for (; got < 3 && *c; ++got) {
          char* stop = nullptr;
          v[got] = std::strtof(c, &stop);
          if (stop == c) break;
          c = stop;
          if (*c == ',') ++c;
        }
        if (got < 2) {
          failRemoteCommand("DRAW STROKE: each point is x,y or x,y,pressure, in percent");
          return;
        }
        points.push_back({v[0] / 100.0f, v[1] / 100.0f, std::clamp(v[2] / 100.0f, 0.0f, 1.0f), t});
        t += 0.02f;   // fifty points a second, for replay
      }
      pushUndoSnapshot();
      deckboy::draw::Stroke s;
      s.tool = b.tool;
      s.rgba = b.rgba;
      s.size = brushSizeNow();
      s.opacity = brushOpacityNow();
      s.points = std::move(points);
      if (s.tool != deckboy::draw::Tool::Brush && s.tool != deckboy::draw::Tool::Erase &&
          s.tool != deckboy::draw::Tool::Fill && s.points.size() > 2) {
        s.points = {s.points.front(), s.points.back()};   // a shape is where it began and ended
      }
      drawEditFrame(*cue)->strokes.push_back(std::move(s));
      touchDrawing(*cue);
      remoteCommandDetail_ = "strokes here: " + std::to_string(drawEditFrame(*cue)->strokes.size());
      return;
    }
    if (sub == "UNDO" || sub == "CLEAR") {
      dispatchQuickAction(sub == "UNDO" ? QuickAction::DrawUndoStroke : QuickAction::DrawClearFrame);
      remoteCommandDetail_ = "strokes here: " + std::to_string(drawEditFrame(*cue)->strokes.size());
      return;
    }
    if (sub == "FRAME") {
      const std::string op = parts.size() > 2 ? toUpper(parts[2]) : std::string();
      double n = 0;
      if (op == "ADD") dispatchQuickAction(QuickAction::DrawFrameAdd);
      else if (op == "DELETE" || op == "REMOVE") dispatchQuickAction(QuickAction::DrawFrameDelete);
      else if (op == "NEXT") dispatchQuickAction(QuickAction::DrawFrameNext);
      else if (op == "PREV" || op == "PREVIOUS") dispatchQuickAction(QuickAction::DrawFramePrev);
      else if (number(2, 1, static_cast<double>(d.frames.size()), n) && n == std::floor(n)) {
        d.editFrame = static_cast<int>(n) - 1;
        touchDrawing(*cue);
      } else {
        failRemoteCommand("DRAW FRAME: ADD, DELETE, NEXT, PREV or 1-" + std::to_string(d.frames.size()));
        return;
      }
      remoteCommandDetail_ = "frame " + std::to_string(d.editFrame + 1) + " of " + std::to_string(d.frames.size());
      return;
    }
    if (sub == "PLAYBACK" || sub == "PLAYS") {
      deckboy::draw::Playback p;
      if (parts.size() < 3 || !deckboy::draw::playbackFromToken(toLower(parts[2]), p)) {
        failRemoteCommand("DRAW PLAYBACK: still, flipbook or replay");
        return;
      }
      d.playback = p;
      touchDrawing(*cue);
      remoteCommandDetail_ = deckboy::draw::playbackToken(p);
      return;
    }
    if (sub == "FPS" || sub == "HOLD") {
      double v = 0;
      if (!number(2, sub == "FPS" ? 1.0 : 0.0, sub == "FPS" ? 60.0 : 60.0, v)) {
        failRemoteCommand(sub == "FPS" ? "DRAW FPS: 1-60" : "DRAW HOLD: seconds, 0-60");
        return;
      }
      (sub == "FPS" ? d.fps : d.holdSeconds) = static_cast<float>(v);
      touchDrawing(*cue);
      remoteCommandDetail_ = sub + " " + parts[2];
      return;
    }
    if (sub == "SAVE") {
      const std::string what = parts.size() > 2 ? toUpper(parts[2]) : std::string();
      std::string file;
      for (std::size_t i = 3; i < parts.size(); ++i) file += (i > 3 ? " " : "") + parts[i];
      if (what == "STILL") {
        std::string written, error;
        if (!saveDrawStill(*cue, file, written, error)) {
          failRemoteCommand("DRAW SAVE STILL: " + error);
          return;
        }
        importPaths({written});
        remoteCommandDetail_ = "saved " + written;
        return;
      }
      if (what == "ANIMATION") {
        startDrawAnimationExport(*cue, file, true);
        remoteCommandDetail_ = "saving";
        return;
      }
      failRemoteCommand("DRAW SAVE: STILL or ANIMATION [file]");
      return;
    }
    failRemoteCommand("DRAW: TOOL, COLOUR, SIZE, OPACITY, STROKE, UNDO, CLEAR, FRAME, ONION, PLAYBACK, FPS, HOLD, "
                      "EDITOR or SAVE");
  }

  // ── WHICH INSPECTOR SECTIONS ARE OPEN ───────────────────────────────────
  //
  // James, 2026-11-04 ("yes to the fold thing"): every section starts folded
  // except the one that is the cue's own -- TONE on a tone, PLAYBACK on a
  // clip -- and a section you open or fold stays that way for the session,
  // on every cue. Untouched sections follow the rule as the selection moves.
  std::set<QuickAction> inspectorSectionsTouched_;

  std::vector<std::pair<QuickAction, bool*>> inspectorSectionFlags() {
    return {
      {QuickAction::CueSectionSequenceToggle, &cueSectionSequenceOpen_},
      {QuickAction::CueSectionMasterToggle, &cueSectionMasterOpen_},
      {QuickAction::CueSectionTargetToggle, &cueSectionTargetOpen_},
      {QuickAction::CueSectionTriggersToggle, &cueSectionTriggersOpen_},
      {QuickAction::CueSectionCaptionsToggle, &cueSectionCaptionsOpen_},
      {QuickAction::CueSectionFadeToggle, &cueSectionFadeOpen_},
      {QuickAction::CueSectionMidiToggle, &cueSectionMidiOpen_},
      {QuickAction::CueSectionNetworkToggle, &cueSectionNetworkOpen_},
      {QuickAction::CueSectionTimecodeToggle, &cueSectionTimecodeOpen_},
      {QuickAction::CueSectionScriptToggle, &cueSectionScriptOpen_},
      {QuickAction::CueSectionDmxToggle, &cueSectionDmxOpen_},
      {QuickAction::CueSectionMatrixToggle, &cueSectionMatrixOpen_},
      {QuickAction::CueSectionTextToggle, &cueSectionTextOpen_},
      {QuickAction::CueSectionMidiFileToggle, &cueSectionMidiFileOpen_},
      {QuickAction::CueSectionFiresideToggle, &cueSectionFiresideOpen_},
      {QuickAction::CueSectionPortalToggle, &cueSectionPortalOpen_},
      {QuickAction::CueSectionDrawToggle, &cueSectionDrawOpen_},
      {QuickAction::CueSectionExtraToggle, &cueSectionExtraOpen_},
      {QuickAction::CueSectionSwirlToggle, &cueSectionSwirlOpen_},
      {QuickAction::CueSectionMoireToggle, &cueSectionMoireOpen_},
      {QuickAction::CueSectionPlaybackToggle, &cueSectionPlaybackOpen_},
      {QuickAction::CueSectionMetadataToggle, &cueSectionMetadataOpen_},
      {QuickAction::CueSectionGeometryToggle, &cueSectionGeometryOpen_},
      {QuickAction::CueSectionKeyToggle, &cueSectionKeyOpen_},
      {QuickAction::CueSectionEffectsToggle, &cueSectionEffectsOpen_},
      {QuickAction::CueSectionAudioFxToggle, &cueSectionAudioFxOpen_},
      {QuickAction::CueSectionCodeToggle, &cueSectionCodeOpen_},
      {QuickAction::CueSectionTimerToggle, &cueSectionTimerOpen_},
      {QuickAction::CueSectionToneToggle, &cueSectionToneOpen_},
      {QuickAction::CueSectionSynthToggle, &cueSectionSynthOpen_},
      {QuickAction::CueSectionVideoSynthToggle, &cueSectionVideoSynthOpen_},
      {QuickAction::CueSectionRoutingToggle, &cueSectionRoutingOpen_},
      {QuickAction::CueSectionAudioToggle, &cueSectionAudioOpen_},
      {QuickAction::CueSectionMicToggle, &cueSectionMicOpen_},
      {QuickAction::CueSectionMaskToggle, &cueSectionMaskOpen_},
    };
  }

  // The section that is this cue's own, which starts open.
  QuickAction inspectorOwnSection(const Cue& cue) const {
    switch (cue.kind) {
      case CueKind::Tone:       return QuickAction::CueSectionToneToggle;
      case CueKind::Timer:      return QuickAction::CueSectionTimerToggle;
      case CueKind::Text:       return QuickAction::CueSectionTextToggle;
      case CueKind::VideoSynth: return QuickAction::CueSectionVideoSynthToggle;
      case CueKind::Master:     return QuickAction::CueSectionMasterToggle;
      case CueKind::Target:     return QuickAction::CueSectionTargetToggle;
      case CueKind::Fade:       return QuickAction::CueSectionFadeToggle;
      case CueKind::Midi:       return QuickAction::CueSectionMidiToggle;
      case CueKind::MidiFile:   return QuickAction::CueSectionMidiFileToggle;
      case CueKind::Network:    return QuickAction::CueSectionNetworkToggle;
      case CueKind::Timecode:   return QuickAction::CueSectionTimecodeToggle;
      case CueKind::Script:     return QuickAction::CueSectionScriptToggle;
      case CueKind::Dmx:        return QuickAction::CueSectionDmxToggle;
      case CueKind::Mic:        return QuickAction::CueSectionMicToggle;
      case CueKind::Audio:      return QuickAction::CueSectionAudioToggle;
      case CueKind::Pattern: {
        const std::string t = stripPatternMotionSuffix(normalizePatternTypeId(cue.path));
        if (t == "code") return QuickAction::CueSectionCodeToggle;
        if (t == "fireside") return QuickAction::CueSectionFiresideToggle;
        if (t == "portal") return QuickAction::CueSectionPortalToggle;
        if (t == "draw") return QuickAction::CueSectionDrawToggle;
        if (t == "swirl") return QuickAction::CueSectionSwirlToggle;
        if (t == "moire-field") return QuickAction::CueSectionMoireToggle;
        // Lumia shares the Moire section flag: a cue is only ever one of them,
        // and the toggle enum lives in types.hpp.
        if (t == "lumia") return QuickAction::CueSectionMoireToggle;
        if (deckboy::extras::findGenerator(t)) return QuickAction::CueSectionExtraToggle;
        return QuickAction::CueSectionPlaybackToggle;
      }
      default:
        return QuickAction::CueSectionPlaybackToggle;
    }
  }

  // Before the inspector draws: untouched sections follow the rule.
  void applyInspectorFolding(const Cue* cue) {
    const QuickAction own = cue ? inspectorOwnSection(*cue) : QuickAction::CueSectionPlaybackToggle;
    for (const auto& [action, flag] : inspectorSectionFlags()) {
      if (!inspectorSectionsTouched_.count(action)) *flag = action == own;
    }
  }

  // A section's header was clicked (or a button opened it): from now on it
  // keeps the state it is given.
  void noteInspectorSectionTouched(QuickAction action) {
    for (const auto& [known, flag] : inspectorSectionFlags()) {
      (void) flag;
      if (known == action) {
        inspectorSectionsTouched_.insert(action);
        return;
      }
    }
  }

  void openInspectorSection(QuickAction action) {
    for (const auto& [known, flag] : inspectorSectionFlags()) {
      if (known == action) {
        *flag = true;
        inspectorSectionsTouched_.insert(action);
        return;
      }
    }
  }
