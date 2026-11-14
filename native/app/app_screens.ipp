  // ── THE SCREENS WINDOW ──────────────────────────────────────────────────
  //
  // James, 2026-11-04 (Screens board 5Dsz7KuL, stage 3): the slices of one
  // output, seen and moved by hand. A window of its own, like Monitors, so it
  // can sit on a second display. INPUT above: the programme with each slice's
  // crop drawn on it; drag a crop to move it, its corners to size it. OUTPUT
  // below: each slice where it lands, with its picture; drag it to move it,
  // its corners to size it -- or, with PIN, to pin them -- and a mesh's
  // points to bend it. The selected slice's numbers down the right.

  SDL_Window* screensWindow_ = nullptr;
  SDL_Renderer* screensRenderer_ = nullptr;
  SDL_Texture* screensPic_ = nullptr;
  int screensPicW_ = 0, screensPicH_ = 0;
  std::uint64_t screensPicSerial_ = 0;
  int screensPicFrom_ = -1;
  int screensOutput_ = 0;
  int screensSlice_ = 0;
  bool screensPinMode_ = false;
  std::string screensSnapPath_;   // SCREENS SNAP: the next frame, to a .bmp
  struct ScreensButton {
    SDL_Rect r;
    int action;
    int param;
  };
  std::vector<ScreensButton> screensButtons_;
  SDL_FRect screensIn_ {}, screensOut_ {};
  enum class ScreensDragKind { None, CropMove, CropCorner, PlaceMove, PlaceCorner, PinCorner, MeshPoint };
  struct ScreensDrag {
    ScreensDragKind kind = ScreensDragKind::None;
    int index = -1;          // a corner (0 TL, 1 TR, 2 BR, 3 BL) or a mesh point
    float x0 = 0, y0 = 0;    // where the press was
    deckboy::slices::Slice start;
  } screensDrag_;

  enum ScreensAction {
    kScrOutputTab = 1, kScrAddSlice, kScrRemoveSlice, kScrPinMode, kScrClose, kScrSelect, kScrToggleOn,
    kScrDec, kScrInc, kScrSource, kScrFlipH, kScrMask, kScrMesh, kScrPerspective, kScrResetPins,
    // A slice's oscillators (param = kSliceLfo* slot): the ~ chip and its row.
    kScrLfoToggle, kScrLfoShape, kScrLfoRateDec, kScrLfoRateInc, kScrLfoDepthDec, kScrLfoDepthInc,
    // The tabs (board: Slices | Multiview | LED map), and what is on the other two.
    kScrTab, kScrMvLayout, kScrMvSelect, kScrMvFeed, kScrMvLabel, kScrMvMeter, kScrMvSafe, kScrMvAdd,
    kScrMvRemove, kScrMvReset, kScrMvDesk, kScrLedTileW, kScrLedTileH, kScrLedRows, kScrLedCols, kScrLedTiles,
    // The STAGE tab (app_stage.ipp).
    kScrStageDec, kScrStageInc, kScrStageUnits, kScrStagePixels, kScrStagePick, kScrStageApply, kScrStageView,
  };
  int screensTab_ = 0;        // 0 slices, 1 multiview, 2 LED map, 3 stage
  std::vector<std::string> screensMvFeeds_;   // the feed list as drawn, for its buttons
  int screensMvSel_ = 0;      // the multiview window chosen
  int screensLedW_ = 0;       // the LED map's tile size; 0 until first drawn
  int screensLedH_ = 0;
  // The numbers a stepper moves, and by how much.
  enum ScreensField { kFCropX, kFCropY, kFCropW, kFCropH, kFPlaceX, kFPlaceY, kFPlaceW, kFPlaceH, kFRotIn, kFRotOut,
                      kFOpacity, kFEdgeL, kFEdgeR, kFEdgeT, kFEdgeB, kFFieldCount };

  // Open or not, kept apart from the window's own flag: a measurement run
  // (DECKBOY_HIDE_WINDOWS) renders the window without ever showing it.
  bool screensOpen_ = false;
  bool screensVisible() const { return screensWindow_ && screensOpen_; }

  void setScreensVisible(bool on) {
    if (on && !screensWindow_) {
      screensWindow_ = SDL_CreateWindow("Deckboy Screens", 1400, 900, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
      if (screensWindow_) {
        applyDeckboyWindowIcon(screensWindow_);
        SDL_SetWindowMinimumSize(screensWindow_, 900, 600);
        screensRenderer_ = deckboyCreateRenderer(screensWindow_);
        if (!screensRenderer_) {
          SDL_DestroyWindow(screensWindow_);
          screensWindow_ = nullptr;
        }
      }
      if (!screensWindow_) {
        triggerToast("could not open the Screens window", ToastKind::Warning, kToastReadableMs);
        return;
      }
    }
    if (!screensWindow_) return;
    screensOpen_ = on;
    if (on) {
      screensOutput_ = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
      if (!windowsHiddenForMeasurement()) {
        SDL_ShowWindow(screensWindow_);
        SDL_RaiseWindow(screensWindow_);
      }
    } else {
      SDL_HideWindow(screensWindow_);
      screensDrag_ = ScreensDrag {};
    }
  }

  void shutdownScreens() {
    if (screensPic_) SDL_DestroyTexture(screensPic_);
    screensPic_ = nullptr;
    if (screensRenderer_) SDL_DestroyRenderer(screensRenderer_);
    screensRenderer_ = nullptr;
    if (screensWindow_) SDL_DestroyWindow(screensWindow_);
    screensWindow_ = nullptr;
  }

  OutputTarget* screensOutputTarget() {
    if (project_.outputs.empty()) return nullptr;
    screensOutput_ = std::clamp(screensOutput_, 0, static_cast<int>(project_.outputs.size()) - 1);
    return &project_.outputs[static_cast<std::size_t>(screensOutput_)];
  }

  deckboy::slices::Slice* screensSelected() {
    OutputTarget* out = screensOutputTarget();
    if (!out || out->slices.empty()) return nullptr;
    screensSlice_ = std::clamp(screensSlice_, 0, static_cast<int>(out->slices.size()) - 1);
    return &out->slices[static_cast<std::size_t>(screensSlice_)];
  }

  // The programme as this window's own texture: the output's small tap,
  // uploaded when it changes. Any output's tap will do when this one has
  // none (it is not on).
  void syncScreensPicture() {
    const OutputRuntime* from = nullptr;
    int fromIndex = -1;
    auto usable = [](const OutputRuntime& rt) {
      return rt.previewTapSerial != 0 && rt.previewTapW > 0 && rt.previewTapH > 0 && !rt.previewTapPixels.empty();
    };
    if (screensOutput_ >= 0 && screensOutput_ < static_cast<int>(outputRuntimes_.size()) &&
        usable(outputRuntimes_[static_cast<std::size_t>(screensOutput_)])) {
      from = &outputRuntimes_[static_cast<std::size_t>(screensOutput_)];
      fromIndex = screensOutput_;
    }
    for (std::size_t i = 0; !from && i < outputRuntimes_.size(); ++i) {
      if (usable(outputRuntimes_[i])) {
        from = &outputRuntimes_[i];
        fromIndex = static_cast<int>(i);
      }
    }
    if (!from || (from->previewTapSerial == screensPicSerial_ && fromIndex == screensPicFrom_)) return;
    if (!screensPic_ || screensPicW_ != from->previewTapW || screensPicH_ != from->previewTapH) {
      if (screensPic_) SDL_DestroyTexture(screensPic_);
      screensPic_ = deckboyCreateTexture(screensRenderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                         from->previewTapW, from->previewTapH);
      if (!screensPic_) return;
      SDL_SetTextureScaleMode(screensPic_, SDL_SCALEMODE_LINEAR);
      screensPicW_ = from->previewTapW;
      screensPicH_ = from->previewTapH;
    }
    SDL_UpdateTexture(screensPic_, nullptr, from->previewTapPixels.data(), from->previewTapW * 4);
    screensPicSerial_ = from->previewTapSerial;
    screensPicFrom_ = fromIndex;
  }

  // A rectangle of a given width over height fitted inside another.
  static SDL_FRect fitAspect(const SDL_Rect& box, double aspect) {
    float w = static_cast<float>(box.w), h = w / static_cast<float>(aspect);
    if (h > box.h) {
      h = static_cast<float>(box.h);
      w = h * static_cast<float>(aspect);
    }
    return SDL_FRect {box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w, h};
  }

  // Where a slice lands in the OUTPUT stage, and the crop's corners on the
  // picture -- the same arithmetic the output's present step does.
  void screensSliceQuad(const deckboy::slices::Slice& s, const SDL_FRect& stage, SDL_FPoint p[4], SDL_FPoint uv[4]) {
    const SDL_FPoint inMid {s.srcX + s.srcW * 0.5f, s.srcY + s.srcH * 0.5f};
    const double pw = std::max(1, screensPicW_), ph = std::max(1, screensPicH_);
    uv[0] = turnAbout({s.srcX, s.srcY}, inMid, s.inRotation, pw, ph);
    uv[1] = turnAbout({s.srcX + s.srcW, s.srcY}, inMid, s.inRotation, pw, ph);
    uv[2] = turnAbout({s.srcX + s.srcW, s.srcY + s.srcH}, inMid, s.inRotation, pw, ph);
    uv[3] = turnAbout({s.srcX, s.srcY + s.srcH}, inMid, s.inRotation, pw, ph);
    if (s.flipH) {
      std::swap(uv[0], uv[1]);
      std::swap(uv[3], uv[2]);
    }
    if (s.flipV) {
      std::swap(uv[0], uv[3]);
      std::swap(uv[1], uv[2]);
    }
    const float x0 = stage.x + s.dstX * stage.w, y0 = stage.y + s.dstY * stage.h;
    const float x1 = x0 + s.dstW * stage.w, y1 = y0 + s.dstH * stage.h;
    const SDL_FPoint mid {(x0 + x1) / 2, (y0 + y1) / 2};
    p[0] = turnAbout({x0, y0}, mid, s.outRotation);
    p[1] = turnAbout({x1, y0}, mid, s.outRotation);
    p[2] = turnAbout({x1, y1}, mid, s.outRotation);
    p[3] = turnAbout({x0, y1}, mid, s.outRotation);
    for (int k = 0; k < 4; ++k) {
      p[k].x += s.corners[static_cast<std::size_t>(k * 2)] * stage.w;
      p[k].y += s.corners[static_cast<std::size_t>(k * 2 + 1)] * stage.h;
    }
  }

  // A mesh point's place in the OUTPUT stage.
  SDL_FPoint screensMeshPoint(const deckboy::slices::Slice& s, const SDL_FPoint p[4], const SDL_FRect& stage, int c,
                              int r) {
    const float u = static_cast<float>(c) / static_cast<float>(s.gridCols - 1);
    const float v = static_cast<float>(r) / static_cast<float>(s.gridRows - 1);
    const SDL_FPoint base = bilerpPoint(p[0], p[1], p[2], p[3], u, v);
    const std::size_t i = static_cast<std::size_t>(r * s.gridCols + c) * 2;
    return SDL_FPoint {base.x + s.gridOffsets[i] * stage.w, base.y + s.gridOffsets[i + 1] * stage.h};
  }

  void screensButton(const SDL_Rect& r, const std::string& text, int action, int param, bool on = false) {
    drawUIPanel(r, paletteToggleFill(on), pal.deep, pal.light);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, r, text, paletteToggleInk(on));
    screensButtons_.push_back({r, action, param});
  }

  static void outlineF(SDL_Renderer* r, const SDL_FPoint p[4], SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    for (int k = 0; k < 4; ++k) SDL_RenderLine(r, p[k].x, p[k].y, p[(k + 1) % 4].x, p[(k + 1) % 4].y);
  }

  static void handleF(SDL_Renderer* r, SDL_FPoint p, float size, SDL_Color c) {
    const SDL_FRect box {p.x - size, p.y - size, size * 2, size * 2};
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderRect(r, &box);
  }

  void renderScreensWindow() {
    if (!screensVisible() || !screensRenderer_) return;
    SDL_Renderer* savedRenderer = controlRenderer_;
    SDL_Window* savedWindow = controlWindow_;
    controlRenderer_ = screensRenderer_;
    controlWindow_ = screensWindow_;
    screensButtons_.clear();
    syncScreensPicture();

    int W = 0, H = 0;
    SDL_GetWindowSize(screensWindow_, &W, &H);
    SDL_SetRenderDrawColor(controlRenderer_, red(kShellShadowColor), green(kShellShadowColor),
                           blue(kShellShadowColor), 255);
    SDL_RenderClear(controlRenderer_);
    Primitives::beginSurfaceFrame(controlRenderer_, paletteColorFromRgba(kShellShadowColor));
    const int pad = 10;
    const int rowH = std::max(28, textLineHeight(fontSmall_) + 10);
    SDL_Rect shell {pad / 2, pad / 2, W - pad, H - pad};
    drawUIPanel(shell, pal.shellOuter, pal.deep, pal.shellInner);

    // ── The header: the outputs, and what to do to this one's slices.
    OutputTarget* out = screensOutputTarget();
    int x = shell.x + pad;
    const int hy = shell.y + pad;
    drawTextSafe(controlRenderer_, fontPixelSmall_ ? fontPixelSmall_ : fontBase_, SDL_Rect {x, hy, 120, rowH},
                 "SCREENS", pal.fg);
    x += 130;
    {
      const char* tabs[4] = {"SLICES", "MULTIVIEW", "LED MAP", "STAGE"};
      for (int t = 0; t < 4; ++t) {
        const int bw = std::max(90, measuredTextWidth(fontSmall_, tabs[t]) + 20);
        screensButton(SDL_Rect {x, hy, bw, rowH}, tabs[t], kScrTab, t, t == screensTab_);
        x += bw + 4;
      }
      x += 16;
    }
    for (int o = 0; o < static_cast<int>(project_.outputs.size()) && o < 8 && screensTab_ != 1 && screensTab_ != 3;
         ++o) {
      const std::string label = outputLabel(o);
      const int bw = std::max(90, measuredTextWidth(fontSmall_, label) + 20);
      screensButton(SDL_Rect {x, hy, bw, rowH}, label, kScrOutputTab, o, o == screensOutput_);
      x += bw + 4;
    }
    int rx = shell.x + shell.w - pad;
    auto rightButton = [&](const std::string& text, int action, bool on) {
      const int bw = std::max(80, measuredTextWidth(fontSmall_, text) + 20);
      rx -= bw;
      screensButton(SDL_Rect {rx, hy, bw, rowH}, text, action, 0, on);
      rx -= 4;
    };
    rightButton("CLOSE", kScrClose, false);
    if (screensTab_ == 0) {
      rightButton(screensPinMode_ ? "CORNERS: PIN" : "CORNERS: SIZE", kScrPinMode, screensPinMode_);
      rightButton("- SLICE", kScrRemoveSlice, false);
      rightButton("+ SLICE", kScrAddSlice, false);
    }

    const int top = hy + rowH + pad;
    if (screensTab_ == 1) {
      renderScreensMultiview(SDL_Rect {shell.x + pad, top, shell.w - 2 * pad, shell.y + shell.h - pad - top}, rowH);
    } else if (screensTab_ == 2) {
      renderScreensLedMap(SDL_Rect {shell.x + pad, top, shell.w - 2 * pad, shell.y + shell.h - pad - top}, rowH);
    } else if (screensTab_ == 3) {
      renderScreensStage(SDL_Rect {shell.x + pad, top, shell.w - 2 * pad, shell.y + shell.h - pad - top}, rowH);
    }
    if (screensTab_ == 0) {
    const int listW = 190, sideW = 300;
    const SDL_Rect list {shell.x + pad, top, listW, shell.y + shell.h - pad - top};
    const SDL_Rect side {shell.x + shell.w - pad - sideW, top, sideW, list.h};
    const SDL_Rect middle {list.x + list.w + pad, top, side.x - pad - (list.x + list.w + pad), list.h};

    // ── The slices of this output.
    drawUIPanel(list, pal.tile, pal.deep, pal.mid);
    int ly = list.y + 6;
    if (out) {
      if (out->slices.empty()) {
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {list.x + 8, ly, list.w - 16, rowH * 3},
                     "No slices: this output shows its one picture. + SLICE to start.", pal.fgSoft);
      }
      for (int i = 0; i < static_cast<int>(out->slices.size()); ++i) {
        const auto& s = out->slices[static_cast<std::size_t>(i)];
        const SDL_Rect row {list.x + 6, ly, list.w - 12 - rowH, rowH};
        screensButton(row, s.name.empty() ? "slice " + std::to_string(i + 1) : s.name, kScrSelect, i, i == screensSlice_);
        screensButton(SDL_Rect {row.x + row.w + 2, ly, rowH - 2, rowH}, s.enabled ? "on" : "off", kScrToggleOn, i,
                      s.enabled);
        ly += rowH + 4;
      }
    }

    // ── INPUT above, OUTPUT below.
    const int labelH = textLineHeight(fontSmall_) + 4;
    const SDL_Rect inBox {middle.x, middle.y + labelH, middle.w, (middle.h - labelH * 2 - pad) / 2};
    const SDL_Rect outBox {middle.x, inBox.y + inBox.h + pad + labelH, middle.w, inBox.h};
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {middle.x, middle.y, middle.w, labelH},
                 "INPUT: the programme. Drag a crop to move it, its corners to size it.", pal.fg);
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {middle.x, outBox.y - labelH, middle.w, labelH},
                 screensPinMode_ ? "OUTPUT: drag a slice to move it, its corners to pin them."
                                 : "OUTPUT: drag a slice to move it, its corners to size it.", pal.fg);
    const double picAspect = screensPicW_ > 0 && screensPicH_ > 0
                               ? static_cast<double>(screensPicW_) / screensPicH_ : 16.0 / 9.0;
    auto outSize = out ? outputRenderSizeForOutput(screensOutput_) : std::pair<int, int> {1920, 1080};
    const double outAspect = outSize.second > 0 ? static_cast<double>(outSize.first) / outSize.second : 16.0 / 9.0;
    screensIn_ = fitAspect(inBox, picAspect);
    screensOut_ = fitAspect(outBox, outAspect);
    SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 255);
    SDL_RenderFillRect(controlRenderer_, &screensIn_);
    SDL_RenderFillRect(controlRenderer_, &screensOut_);
    if (screensPic_) SDL_RenderTexture(controlRenderer_, screensPic_, nullptr, &screensIn_);
    const SDL_Color kIdle {120, 200, 140, 255}, kHot {242, 179, 61, 255};
    if (out) {
      for (int i = 0; i < static_cast<int>(out->slices.size()); ++i) {
        const auto& s = out->slices[static_cast<std::size_t>(i)];
        const bool hot = i == screensSlice_;
        // The crop on the input.
        SDL_FPoint q[4] = {{screensIn_.x + s.srcX * screensIn_.w, screensIn_.y + s.srcY * screensIn_.h},
                           {screensIn_.x + (s.srcX + s.srcW) * screensIn_.w, screensIn_.y + s.srcY * screensIn_.h},
                           {screensIn_.x + (s.srcX + s.srcW) * screensIn_.w,
                            screensIn_.y + (s.srcY + s.srcH) * screensIn_.h},
                           {screensIn_.x + s.srcX * screensIn_.w, screensIn_.y + (s.srcY + s.srcH) * screensIn_.h}};
        outlineF(controlRenderer_, q, hot ? kHot : kIdle);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {static_cast<int>(q[0].x) + 4, static_cast<int>(q[0].y) + 2, 160, labelH},
                     s.name.empty() ? "slice " + std::to_string(i + 1) : s.name, hot ? kHot : kIdle);
        if (hot) {
          for (const auto& c : q) handleF(controlRenderer_, c, 5, kHot);
        }
        // The slice where it lands, with its picture.
        SDL_FPoint p[4], uv[4];
        screensSliceQuad(s, screensOut_, p, uv);
        if (screensPic_ && s.enabled && s.source.empty()) {
          // Drawn by the helpers the output's present step uses, so the
          // pin, the soft edges and the mesh look here as they will there.
          const EdgeBlendShape shape = sliceBlendShape(*out, s, false);
          const bool soft = deckboy::slices::soft(s);
          bool drawn = false;
          if (deckboy::slices::gridActive(s)) {
            static thread_local OutputTarget gridHolder;
            gridHolder.warpGridCols = s.gridCols;
            gridHolder.warpGridRows = s.gridRows;
            gridHolder.warpGridSmooth = s.gridSmooth;
            gridHolder.warpGridOffsets.resize(s.gridOffsets.size());
            for (std::size_t k = 0; k < s.gridOffsets.size(); ++k) {
              gridHolder.warpGridOffsets[k] = s.gridOffsets[k] * (k % 2 == 0 ? screensOut_.w : screensOut_.h);
            }
            drawn = renderGridWarp(controlRenderer_, screensPic_, shape, gridHolder, s.perspective, uv[0], uv[1],
                                   uv[2], uv[3], p[0], p[1], p[2], p[3], soft, s.opacity);
          }
          if (!drawn && (deckboy::slices::pinned(s) || s.outRotation != 0.0f) && s.perspective) {
            drawn = renderPerspectiveWarp(controlRenderer_, screensPic_, shape, uv[0], uv[1], uv[2], uv[3], p[0], p[1],
                                          p[2], p[3], soft, s.opacity);
          }
          if (!drawn && soft) {
            drawn = renderFeatheredQuad(controlRenderer_, screensPic_, shape, uv[0], uv[1], uv[2], uv[3], p[0], p[1],
                                        p[2], p[3], s.opacity);
          }
          if (!drawn) {
            const SDL_FColor tint {1, 1, 1, s.opacity};
            SDL_Vertex v[4] {{p[0], tint, uv[0]}, {p[1], tint, uv[1]}, {p[2], tint, uv[2]}, {p[3], tint, uv[3]}};
            const int idx[6] {0, 1, 2, 0, 2, 3};
            SDL_SetTextureBlendMode(screensPic_, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(controlRenderer_, screensPic_, v, 4, idx, 6);
          }
        }
        outlineF(controlRenderer_, p, hot ? kHot : kIdle);
        if (hot) {
          for (const auto& c : p) handleF(controlRenderer_, c, 6, kHot);
          if (deckboy::slices::gridActive(s)) {
            for (int r = 0; r < s.gridRows; ++r) {
              for (int c = 0; c < s.gridCols; ++c) {
                handleF(controlRenderer_, screensMeshPoint(s, p, screensOut_, c, r), 4, SDL_Color {255, 255, 255, 255});
              }
            }
          }
        }
      }
    }

    // ── The selected slice's numbers.
    drawUIPanel(side, pal.tile, pal.deep, pal.mid);
    int sy = side.y + 6;
    deckboy::slices::Slice* sel = screensSelected();
    if (sel) {
      auto rowLabel = [&](const std::string& label) {
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, 90, rowH}, label, pal.fg);
      };
      auto stepper = [&](const std::string& label, int field, const std::string& value, int lfoSlot = -1) {
        rowLabel(label);
        const int bx = side.x + 100;
        // The ~ chip, where the value takes an oscillator, at the row's end.
        // Its column is kept on every row, so the + buttons line up.
        const int chipW = rowH + 2;
        const int bw = side.w - 108 - chipW;
        screensButton(SDL_Rect {bx, sy, rowH, rowH}, "-", kScrDec, field);
        drawUIValueControl(SDL_Rect {bx + rowH + 2, sy, bw - 2 * rowH - 4, rowH}, value);
        screensButton(SDL_Rect {bx + bw - rowH, sy, rowH, rowH}, "+", kScrInc, field);
        if (lfoSlot >= 0) {
          const auto& lfo = sel->lfo[static_cast<std::size_t>(lfoSlot)];
          screensButton(SDL_Rect {bx + bw + 2, sy, rowH, rowH}, "~", kScrLfoToggle, lfoSlot, lfo.on);
        }
        sy += rowH + 3;
        // Its oscillator, under it, while it is on: shape, rate, depth -- the
        // same three the inspector's ~ row has.
        if (lfoSlot >= 0 && sel->lfo[static_cast<std::size_t>(lfoSlot)].on) {
          const auto& lfo = sel->lfo[static_cast<std::size_t>(lfoSlot)];
          const int x0 = side.x + 100, w = side.w - 108;
          rowLabel("  ~ shape");
          screensButton(SDL_Rect {x0, sy, w, rowH}, deckboy::effects::lfoShapeToken(lfo.shape), kScrLfoShape,
                        lfoSlot);
          sy += rowH + 3;
          char rate[32];
          if (deckboy::effects::lfoShapeIsMotion(lfo.shape)) {
            std::snprintf(rate, sizeof rate, "%s", lfo.motionZone <= 0 ? "whole picture"
                                                   : ("zone " + std::to_string(lfo.motionZone)).c_str());
          } else if (deckboy::effects::lfoShapeIsMidi(lfo.shape)) {
            std::snprintf(rate, sizeof rate, lfo.shape == deckboy::effects::LfoShape::MidiCc ? "CC %d" : "channel %d",
                          lfo.shape == deckboy::effects::LfoShape::MidiCc ? lfo.midiNumber : lfo.midiChannel);
          } else if (lfo.beatSync) {
            std::snprintf(rate, sizeof rate, "%.2g beats", static_cast<double>(lfo.beats));
          } else {
            std::snprintf(rate, sizeof rate, "%.2f hz", static_cast<double>(lfo.rateHz));
          }
          char depth[24];
          std::snprintf(depth, sizeof depth, "depth %.0f%%", lfo.depth * 100.0);
          // Rate and depth across the panel: - value + , - value +.
          const int fx = side.x + 8, fw = side.w - 16;
          const int rateW = (fw - 6) * 55 / 100, depthW = fw - 6 - rateW;
          screensButton(SDL_Rect {fx, sy, rowH, rowH}, "-", kScrLfoRateDec, lfoSlot);
          drawUIValueControl(SDL_Rect {fx + rowH + 2, sy, rateW - 2 * rowH - 4, rowH}, rate);
          screensButton(SDL_Rect {fx + rateW - rowH, sy, rowH, rowH}, "+", kScrLfoRateInc, lfoSlot);
          const int dx = fx + rateW + 6;
          screensButton(SDL_Rect {dx, sy, rowH, rowH}, "-", kScrLfoDepthDec, lfoSlot);
          drawUIValueControl(SDL_Rect {dx + rowH + 2, sy, depthW - 2 * rowH - 4, rowH}, depth);
          screensButton(SDL_Rect {dx + depthW - rowH, sy, rowH, rowH}, "+", kScrLfoDepthInc, lfoSlot);
          sy += rowH + 3;
        }
      };
      auto toggleRow = [&](const std::string& label, const std::string& value, int action, bool on) {
        rowLabel(label);
        screensButton(SDL_Rect {side.x + 100, sy, side.w - 108, rowH}, value, action, 0, on);
        sy += rowH + 3;
      };
      char t[48];
      auto pct = [&](float v) {
        std::snprintf(t, sizeof t, "%.1f%%", v * 100.0f);
        return std::string(t);
      };
      auto deg = [&](float v) {
        std::snprintf(t, sizeof t, "%.0f deg", v);
        return std::string(t);
      };
      const std::string src = sel->source.empty() ? std::string("programme")
                              : deckLabel(std::atoi(sel->source.c_str() + 5));
      toggleRow("source", src, kScrSource, false);
      stepper("crop x", kFCropX, pct(sel->srcX));
      stepper("crop y", kFCropY, pct(sel->srcY));
      stepper("crop w", kFCropW, pct(sel->srcW));
      stepper("crop h", kFCropH, pct(sel->srcH));
      stepper("place x", kFPlaceX, pct(sel->dstX), deckboy::slices::kSliceLfoX);
      stepper("place y", kFPlaceY, pct(sel->dstY), deckboy::slices::kSliceLfoY);
      stepper("place w", kFPlaceW, pct(sel->dstW));
      stepper("place h", kFPlaceH, pct(sel->dstH));
      stepper("turn in", kFRotIn, deg(sel->inRotation));
      stepper("turn out", kFRotOut, deg(sel->outRotation), deckboy::slices::kSliceLfoRotation);
      stepper("opacity", kFOpacity, pct(sel->opacity), deckboy::slices::kSliceLfoOpacity);
      stepper("edge L", kFEdgeL, pct(sel->edgeL));
      stepper("edge R", kFEdgeR, pct(sel->edgeR));
      stepper("edge T", kFEdgeT, pct(sel->edgeT));
      stepper("edge B", kFEdgeB, pct(sel->edgeB));
      toggleRow("flip", sel->flipH ? (sel->flipV ? "across and down" : "across") : (sel->flipV ? "down" : "none"),
                kScrFlipH, sel->flipH || sel->flipV);
      const MaskModel* m = sel->maskId.empty() ? nullptr : findMask(sel->maskId);
      toggleRow("mask", m ? m->name : std::string("none"), kScrMask, m != nullptr);
      toggleRow("mesh", deckboy::slices::gridActive(*sel)
                          ? std::to_string(sel->gridCols) + " x " + std::to_string(sel->gridRows) : std::string("off"),
                kScrMesh, deckboy::slices::gridActive(*sel));
      toggleRow("pins", deckboy::slices::pinned(*sel) ? "reset corners" : "none", kScrResetPins,
                deckboy::slices::pinned(*sel));
      toggleRow("pin as", sel->perspective ? "perspective" : "linear", kScrPerspective, sel->perspective);
    }
    }  // the Slices tab

    if (scanlineOverlay_ && pal.scanlineAlpha > 0) {
      int ww, wh;
      SDL_GetCurrentRenderOutputSize(controlRenderer_, &ww, &wh);
      const SDL_Rect dst {0, 0, ww, wh};
      SDL_RenderTexture(controlRenderer_, scanlineOverlay_, nullptr, &dst);
    }
    if (!screensSnapPath_.empty()) {
      if (SDL_Surface* shot = SDL_RenderReadPixels(controlRenderer_, nullptr)) {
        SDL_SaveBMP(shot, screensSnapPath_.c_str());
        SDL_DestroySurface(shot);
      }
      screensSnapPath_.clear();
    }
    presentTraced(controlRenderer_, "screens");
    controlRenderer_ = savedRenderer;
    controlWindow_ = savedWindow;
  }

  // ── THE MULTIVIEW TAB ───────────────────────────────────────────────────
  //
  // The board's second mock: layouts on the left with the feeds under them,
  // the windows in the middle -- each saying what it shows, red for what is
  // on air and green for what is next, and an empty one asking for a feed --
  // and the chosen window's switches on the right. Click a window, then a
  // feed: no hidden right-click. The arrangement is the one the desk's MULTI
  // monitor and any MULTIVIEW output show.

  void screensMvMaterialise() {
    // Arranging one window makes the whole set explicit and saved, as the
    // desk's window menu does, so it cannot rearrange itself later.
    if (project_.multiviewTiles.empty()) project_.multiviewTiles = multiviewTilePlan();
  }

  void renderScreensMultiview(const SDL_Rect& area, int rowH) {
    const int pad = 10, listW = 230, sideW = 280;
    const SDL_Rect left {area.x, area.y, listW, area.h};
    const SDL_Rect side {area.x + area.w - sideW, area.y, sideW, area.h};
    const SDL_Rect mid {left.x + left.w + pad, area.y, side.x - pad - (left.x + left.w + pad), area.h};
    const std::vector<MultiviewTile> plan = multiviewTilePlan();
    screensMvSel_ = std::clamp(screensMvSel_, 0, std::max(0, static_cast<int>(plan.size()) - 1));

    // Layouts, then feeds.
    drawUIPanel(left, pal.tile, pal.deep, pal.mid);
    int ly = left.y + 6;
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {left.x + 8, ly, left.w - 16, rowH}, "LAYOUT", pal.fg);
    ly += rowH;
    for (int l = 0; l < static_cast<int>(deckboy::multiview::Layout::Count); ++l) {
      const auto layout = static_cast<deckboy::multiview::Layout>(l);
      screensButton(SDL_Rect {left.x + 6, ly, left.w - 12, rowH}, deckboy::multiview::layoutLabel(layout),
                    kScrMvLayout, l, project_.multiviewLayout == layout);
      ly += rowH + 3;
    }
    ly += 8;
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {left.x + 8, ly, left.w - 16, rowH},
                 "FEEDS: pick a window, then one of these", pal.fg);
    ly += rowH;
    const std::string current = screensMvSel_ < static_cast<int>(plan.size())
                                  ? plan[static_cast<std::size_t>(screensMvSel_)].source : std::string();
    screensMvFeeds_.clear();
    auto feed = [&](const std::string& id, const std::string& label) {
      if (ly + rowH > left.y + left.h - 4) return;
      screensMvFeeds_.push_back(id);
      screensButton(SDL_Rect {left.x + 6, ly, left.w - 12, rowH}, label, kScrMvFeed,
                    static_cast<int>(screensMvFeeds_.size()) - 1, id == current);
      ly += rowH + 3;
    };
    feed("programme", "the programme");
    for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) feed("deck:" + std::to_string(d), deckLabel(d));
    for (int o = 0; o < static_cast<int>(project_.outputs.size()); ++o) {
      const std::string& id = project_.outputs[static_cast<std::size_t>(o)].outputId;
      if (!id.empty()) feed("output:" + id, "output: " + outputLabel(o));
    }
    feed("next", "the next cue (what GO plays)");
    feed("clock", "the time of day");
    feed("", "nothing - leave it empty");

    // The windows, laid out as the desk and the outputs lay them out.
    drawUIPanel(mid, SDL_Color {0, 0, 0, 255}, pal.deep, pal.mid);
    const auto placed = deckboy::multiview::layoutWindows(project_.multiviewLayout, static_cast<int>(plan.size()),
                                                          {mid.x + 6, mid.y + 6, mid.w - 12, mid.h - 12}, 6);
    const SDL_Color kRed {226, 85, 75, 255}, kGreen {51, 215, 92, 255}, kHot {242, 179, 61, 255};
    for (int i = 0; i < static_cast<int>(plan.size()) && i < static_cast<int>(placed.size()); ++i) {
      const auto& t = plan[static_cast<std::size_t>(i)];
      const SDL_Rect r {placed[static_cast<std::size_t>(i)].x, placed[static_cast<std::size_t>(i)].y,
                        placed[static_cast<std::size_t>(i)].w, placed[static_cast<std::size_t>(i)].h};
      Primitives::fillRect(controlRenderer_, r, pal.tile);
      // The programme window shows the programme: the one picture this
      // window already has. The others are named, not pictured.
      if (t.source == "programme" && screensPic_) {
        const SDL_FRect fr = fitAspect(r, screensPicW_ > 0 && screensPicH_ > 0
                                            ? static_cast<double>(screensPicW_) / screensPicH_ : 16.0 / 9.0);
        SDL_RenderTexture(controlRenderer_, screensPic_, nullptr, &fr);
      }
      if (t.source.empty()) {
        drawCenteredTextSafe(controlRenderer_, fontSmall_, r, "+ CHOOSE A FEED", pal.fg);
      } else {
        const int labH = textLineHeight(fontSmall_) + 4;
        const SDL_Rect lab {r.x, r.y + r.h - labH, r.w, labH};
        Primitives::fillRect(controlRenderer_, lab, SDL_Color {0, 0, 0, 170});
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {lab.x + 6, lab.y, lab.w - 12, lab.h},
                     multiviewTileSourceLabel(t) + (t.vuMeter ? "  · meter" : "") + (t.safeAreas ? "  · safe" : ""),
                     SDL_Color {200, 240, 210, 255});
      }
      // Tally: red on air, green next -- every switcher's colours.
      const bool onAir = multiviewTileOnAir(t);
      if (onAir || t.source == "next") {
        const SDL_Color c = onAir ? kRed : kGreen;
        for (int k = 0; k < 3; ++k) {
          Primitives::strokeRect(controlRenderer_, SDL_Rect {r.x + k, r.y + k, r.w - 2 * k, r.h - 2 * k}, c);
        }
      }
      if (i == screensMvSel_) {
        Primitives::strokeRect(controlRenderer_, SDL_Rect {r.x - 3, r.y - 3, r.w + 6, r.h + 6}, kHot);
      }
      screensButtons_.push_back({r, kScrMvSelect, i});   // the window itself is the button
    }

    // The chosen window's switches.
    drawUIPanel(side, pal.tile, pal.deep, pal.mid);
    int sy = side.y + 6;
    auto row = [&](const std::string& label, const std::string& value, int action, bool on) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, 100, rowH}, label, pal.fg);
      screensButton(SDL_Rect {side.x + 110, sy, side.w - 118, rowH}, value, action, 0, on);
      sy += rowH + 3;
    };
    if (screensMvSel_ < static_cast<int>(plan.size())) {
      const auto& t = plan[static_cast<std::size_t>(screensMvSel_)];
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, side.w - 16, rowH},
                   "WINDOW " + std::to_string(screensMvSel_ + 1), pal.fg);
      sy += rowH;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, side.w - 16, rowH},
                   "shows " + (t.source.empty() ? std::string("nothing yet") : multiviewTileSourceLabel(t)),
                   pal.fgSoft);
      sy += rowH + 4;
      row("label", t.label ? "ON" : "OFF", kScrMvLabel, t.label);
      row("audio meter", t.vuMeter ? "ON" : "OFF", kScrMvMeter, t.vuMeter);
      row("safe areas", t.safeAreas ? "ON" : "OFF", kScrMvSafe, t.safeAreas);
      sy += 6;
      row("windows", "+ ONE AFTER THIS", kScrMvAdd, false);
      if (plan.size() > 1) row("", "- REMOVE THIS ONE", kScrMvRemove, false);
      row("", "ONE PER PLAYLIST", kScrMvReset, false);
    }
    sy += 6;
    row("on the desk", project_.multiviewMode != 0 ? "MULTI ON" : "MULTI OFF", kScrMvDesk, project_.multiviewMode != 0);
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, side.w - 16, rowH},
                 "An output whose Shows is", pal.fgSoft);
    sy += rowH;
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, side.w - 16, rowH},
                 "MULTIVIEW carries this too.", pal.fgSoft);
  }

  void handleScreensMultiviewButton(const ScreensButton& b) {
    switch (b.action) {
      case kScrMvSelect:
        screensMvSel_ = b.param;
        return;
      case kScrMvLayout:
        pushUndoSnapshot();
        project_.multiviewLayout = static_cast<deckboy::multiview::Layout>(
          std::clamp(b.param, 0, static_cast<int>(deckboy::multiview::Layout::Count) - 1));
        markProjectDirty();
        return;
      case kScrMvDesk:
        project_.multiviewMode = project_.multiviewMode != 0 ? 0 : 1;
        markProjectDirty();
        return;
      case kScrMvReset:
        pushUndoSnapshot();
        project_.multiviewTiles.clear();   // empty means automatic again
        screensMvSel_ = 0;
        markProjectDirty();
        triggerToast("multiview: one window per playlist again");
        return;
      default:
        break;
    }
    pushUndoSnapshot();
    screensMvMaterialise();
    auto& tiles = project_.multiviewTiles;
    if (screensMvSel_ < 0 || screensMvSel_ >= static_cast<int>(tiles.size())) return;
    auto& t = tiles[static_cast<std::size_t>(screensMvSel_)];
    switch (b.action) {
      case kScrMvFeed:
        if (b.param >= 0 && b.param < static_cast<int>(screensMvFeeds_.size())) {
          t.source = screensMvFeeds_[static_cast<std::size_t>(b.param)];
        }
        break;
      case kScrMvLabel: t.label = !t.label; break;
      case kScrMvMeter: t.vuMeter = !t.vuMeter; break;
      case kScrMvSafe: t.safeAreas = !t.safeAreas; break;
      case kScrMvAdd:
        if (static_cast<int>(tiles.size()) < kMaxMultiviewTiles) {
          MultiviewTile fresh;
          fresh.source = "";
          tiles.insert(tiles.begin() + screensMvSel_ + 1, fresh);
          ++screensMvSel_;
        }
        break;
      case kScrMvRemove:
        if (tiles.size() > 1) {
          tiles.erase(tiles.begin() + screensMvSel_);
          screensMvSel_ = std::max(0, screensMvSel_ - 1);
        }
        break;
      default:
        break;
    }
    markProjectDirty();
  }

  // ── THE LED MAP TAB ─────────────────────────────────────────────────────
  //
  // The board's "later": the Panel Map as a tab. The output's tiles, numbered
  // in reading order as the Panel Map pattern numbers them, over the picture,
  // and slices made from them -- one per row, per column or per tile -- so a
  // strip or a tile cabled out of order can be moved to where it really is.
  // The tile size starts at the Panel Map cue on air (or 128) and steps here.

  std::pair<int, int> screensLedTileFromShow() {
    for (const Deck& deck : project_.decks) {
      if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) continue;
      const Cue& c = deck.cues[static_cast<std::size_t>(deck.activeIndex)];
      if (c.kind == CueKind::Pattern && stripPatternMotionSuffix(normalizePatternTypeId(c.path)) == "panel-map") {
        return {c.ledPanelWidth, c.ledPanelHeight};
      }
    }
    return {128, 128};
  }

  void renderScreensLedMap(const SDL_Rect& area, int rowH) {
    if (screensLedW_ <= 0 || screensLedH_ <= 0) {
      const auto t = screensLedTileFromShow();
      screensLedW_ = t.first;
      screensLedH_ = t.second;
    }
    const int pad = 10, sideW = 300;
    const SDL_Rect side {area.x + area.w - sideW, area.y, sideW, area.h};
    const SDL_Rect mid {area.x, area.y, side.x - pad - area.x, area.h};
    const auto outSize = outputRenderSizeForOutput(screensOutput_);
    const int ow = std::max(1, outSize.first), oh = std::max(1, outSize.second);
    const int cols = (ow + screensLedW_ - 1) / screensLedW_;
    const int rows = (oh + screensLedH_ - 1) / screensLedH_;
    const SDL_FRect stage = fitAspect(mid, static_cast<double>(ow) / oh);
    SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 255);
    SDL_RenderFillRect(controlRenderer_, &stage);
    if (screensPic_) SDL_RenderTexture(controlRenderer_, screensPic_, nullptr, &stage);
    const float tw = stage.w * screensLedW_ / ow, th = stage.h * screensLedH_ / oh;
    const SDL_Color line {120, 200, 140, 255};
    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        const SDL_Rect cell {static_cast<int>(stage.x + c * tw), static_cast<int>(stage.y + r * th),
                             static_cast<int>(std::ceil(tw)), static_cast<int>(std::ceil(th))};
        Primitives::strokeRect(controlRenderer_, cell, line);
        if (tw > 18 && th > 14) {
          drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {cell.x + 3, cell.y + 1, cell.w - 4, cell.h - 2},
                       std::to_string(r * cols + c + 1), SDL_Color {200, 240, 210, 255});
        }
      }
    }

    drawUIPanel(side, pal.tile, pal.deep, pal.mid);
    int sy = side.y + 6;
    auto text = [&](const std::string& t, SDL_Color c) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, side.w - 16, rowH}, t, c);
      sy += rowH;
    };
    auto stepper = [&](const std::string& label, int value, int action) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, 90, rowH}, label, pal.fg);
      const int bx = side.x + 100, bw = side.w - 108;
      screensButton(SDL_Rect {bx, sy, rowH, rowH}, "-", action, -1);
      drawUIValueControl(SDL_Rect {bx + rowH + 2, sy, bw - 2 * rowH - 4, rowH}, std::to_string(value) + " px");
      screensButton(SDL_Rect {bx + bw - rowH, sy, rowH, rowH}, "+", action, 1);
      sy += rowH + 3;
    };
    text(outputLabel(screensOutput_) + "  " + std::to_string(ow) + " x " + std::to_string(oh), pal.fg);
    stepper("tile width", screensLedW_, kScrLedTileW);
    stepper("tile height", screensLedH_, kScrLedTileH);
    text(std::to_string(cols) + " x " + std::to_string(rows) + " tiles  (" + std::to_string(cols * rows) + ")",
         pal.fgSoft);
    sy += 6;
    text("MAKE SLICES (replaces this output's):", pal.fg);
    auto button = [&](const std::string& label, int action, bool enabled) {
      // One over the limit says so in its own label rather than going grey:
      // it still answers, with the reason.
      screensButton(SDL_Rect {side.x + 8, sy, side.w - 16, rowH}, enabled ? label : label + "  too many", action, 0,
                    false);
      sy += rowH + 3;
    };
    const int maxS = deckboy::slices::kMaxSlices;
    button("ONE PER ROW  (" + std::to_string(rows) + ")", kScrLedRows, rows <= maxS);
    button("ONE PER COLUMN  (" + std::to_string(cols) + ")", kScrLedCols, cols <= maxS);
    button("ONE PER TILE  (" + std::to_string(cols * rows) + ")", kScrLedTiles, cols * rows <= maxS);
    if (cols * rows > maxS) {
      text("An output takes 16 slices: a wall", pal.fgSoft);
      text("this size goes by rows or columns.", pal.fgSoft);
    }
    text("Then on SLICES, move one to where", pal.fgSoft);
    text("that tile or strip really is.", pal.fgSoft);
  }

  void handleScreensLedButton(const ScreensButton& b) {
    if (b.action == kScrLedTileW || b.action == kScrLedTileH) {
      int& v = b.action == kScrLedTileW ? screensLedW_ : screensLedH_;
      v = std::clamp(v + (b.param > 0 ? 8 : -8), 16, 2048);
      return;
    }
    OutputTarget* out = screensOutputTarget();
    if (!out) return;
    const auto outSize = outputRenderSizeForOutput(screensOutput_);
    const int ow = std::max(1, outSize.first), oh = std::max(1, outSize.second);
    const int cols = (ow + screensLedW_ - 1) / screensLedW_;
    const int rows = (oh + screensLedH_ - 1) / screensLedH_;
    const int count = b.action == kScrLedRows ? rows : b.action == kScrLedCols ? cols : cols * rows;
    if (count > deckboy::slices::kMaxSlices) {
      triggerToast(std::to_string(count) + " slices: an output takes 16. Rows or columns instead.",
                   ToastKind::Warning, kToastReadableMs);
      return;
    }
    pushUndoSnapshot();
    out->slices.clear();
    auto add = [&](const std::string& name, int px, int py, int pw, int ph) {
      deckboy::slices::Slice s;
      s.name = name;
      // The same rectangle in and out: the wall looks the same until a slice
      // is moved, which is the point -- nothing changes until you mean it to.
      s.srcX = s.dstX = static_cast<float>(px) / ow;
      s.srcY = s.dstY = static_cast<float>(py) / oh;
      s.srcW = s.dstW = static_cast<float>(std::min(pw, ow - px)) / ow;
      s.srcH = s.dstH = static_cast<float>(std::min(ph, oh - py)) / oh;
      out->slices.push_back(s);
    };
    if (b.action == kScrLedRows) {
      for (int r = 0; r < rows; ++r) add("row " + std::to_string(r + 1), 0, r * screensLedH_, ow, screensLedH_);
    } else if (b.action == kScrLedCols) {
      for (int c = 0; c < cols; ++c) add("column " + std::to_string(c + 1), c * screensLedW_, 0, screensLedW_, oh);
    } else {
      for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
          add("tile " + std::to_string(r * cols + c + 1), c * screensLedW_, r * screensLedH_, screensLedW_,
              screensLedH_);
        }
      }
    }
    screensSlice_ = 0;
    markProjectDirty();
    triggerToast(std::to_string(out->slices.size()) + " slices made: move them on SLICES");
  }

  // ── PRESSING AND DRAGGING ───────────────────────────────────────────────

  void screensStep(deckboy::slices::Slice& s, int field, int dir) {
    const float pctStep = 0.01f * static_cast<float>(dir);
    switch (field) {
      case kFCropX: s.srcX += pctStep; break;
      case kFCropY: s.srcY += pctStep; break;
      case kFCropW: s.srcW += pctStep; break;
      case kFCropH: s.srcH += pctStep; break;
      case kFPlaceX: s.dstX += pctStep; break;
      case kFPlaceY: s.dstY += pctStep; break;
      case kFPlaceW: s.dstW += pctStep; break;
      case kFPlaceH: s.dstH += pctStep; break;
      case kFRotIn: s.inRotation += static_cast<float>(dir); break;
      case kFRotOut: s.outRotation += static_cast<float>(dir); break;
      case kFOpacity: s.opacity += 0.05f * static_cast<float>(dir); break;
      case kFEdgeL: s.edgeL += pctStep; break;
      case kFEdgeR: s.edgeR += pctStep; break;
      case kFEdgeT: s.edgeT += pctStep; break;
      case kFEdgeB: s.edgeB += pctStep; break;
      default: break;
    }
    s = deckboy::slices::clampSlice(s);
  }

  void handleScreensButton(const ScreensButton& b) {
    if (b.action == kScrTab) {
      screensTab_ = std::clamp(b.param, 0, 3);
      return;
    }
    if (b.action >= kScrStageDec && b.action <= kScrStageView) {
      handleScreensStageButton(b);
      return;
    }
    if (b.action >= kScrMvLayout && b.action <= kScrMvDesk) {
      handleScreensMultiviewButton(b);
      return;
    }
    if (b.action >= kScrLedTileW && b.action <= kScrLedTiles) {
      handleScreensLedButton(b);
      return;
    }
    OutputTarget* out = screensOutputTarget();
    if (!out) return;
    deckboy::slices::Slice* sel = screensSelected();
    switch (b.action) {
      case kScrOutputTab:
        screensOutput_ = b.param;
        screensSlice_ = 0;
        return;
      case kScrClose: setScreensVisible(false); return;
      case kScrPinMode: screensPinMode_ = !screensPinMode_; return;
      case kScrSelect: screensSlice_ = b.param; return;
      case kScrAddSlice: {
        if (static_cast<int>(out->slices.size()) >= deckboy::slices::kMaxSlices) {
          triggerToast("16 slices is the most an output takes", ToastKind::Warning, kToastReadableMs);
          return;
        }
        pushUndoSnapshot();
        deckboy::slices::Slice s;
        s.name = "slice " + std::to_string(out->slices.size() + 1);
        // A second slice arrives as the right half of everything: the start
        // of the usual split across two screens.
        if (!out->slices.empty()) {
          s.srcX = 0.5f; s.srcW = 0.5f;
          s.dstX = 0.5f; s.dstW = 0.5f;
        }
        out->slices.push_back(s);
        screensSlice_ = static_cast<int>(out->slices.size()) - 1;
        markProjectDirty();
        return;
      }
      case kScrRemoveSlice:
        if (sel) {
          pushUndoSnapshot();
          out->slices.erase(out->slices.begin() + screensSlice_);
          screensSlice_ = std::max(0, screensSlice_ - 1);
          markProjectDirty();
        }
        return;
      default: break;
    }
    if (b.action == kScrToggleOn) {
      if (b.param >= 0 && b.param < static_cast<int>(out->slices.size())) {
        pushUndoSnapshot();
        auto& s = out->slices[static_cast<std::size_t>(b.param)];
        s.enabled = !s.enabled;
        markProjectDirty();
      }
      return;
    }
    if (!sel) return;
    pushUndoSnapshot();
    switch (b.action) {
      case kScrDec: screensStep(*sel, b.param, -1); break;
      case kScrInc: screensStep(*sel, b.param, 1); break;
      case kScrLfoToggle:
      case kScrLfoShape:
      case kScrLfoRateDec:
      case kScrLfoRateInc:
      case kScrLfoDepthDec:
      case kScrLfoDepthInc: {
        if (b.param < 0 || b.param >= static_cast<int>(sel->lfo.size())) break;
        auto& lfo = sel->lfo[static_cast<std::size_t>(b.param)];
        if (b.action == kScrLfoToggle) {
          lfo.on = !lfo.on;
          triggerToast(lfo.on ? "lfo on" : "lfo off");
        } else if (b.action == kScrLfoShape) {
          const int count = static_cast<int>(deckboy::effects::LfoShape::Count);
          lfo.shape = static_cast<deckboy::effects::LfoShape>((static_cast<int>(lfo.shape) + 1) % count);
          triggerToast(deckboy::effects::lfoShapeToken(lfo.shape));
        } else if (b.action == kScrLfoRateDec || b.action == kScrLfoRateInc) {
          nudgeParamLfoRate(lfo, b.action == kScrLfoRateInc ? 1 : -1);
        } else {
          lfo.depth = std::clamp(lfo.depth + (b.action == kScrLfoDepthInc ? 0.05f : -0.05f), 0.0f, 1.0f);
        }
        break;
      }
      case kScrSource: {
        // Programme, then each playlist, round again.
        const int decks = static_cast<int>(project_.decks.size());
        int at = sel->source.empty() ? -1 : std::atoi(sel->source.c_str() + 5);
        at = at + 1 >= decks ? -1 : at + 1;
        sel->source = at < 0 ? std::string() : "deck:" + std::to_string(at);
        break;
      }
      case kScrFlipH: {
        // None, across, down, both.
        const int state = (sel->flipH ? 1 : 0) + (sel->flipV ? 2 : 0);
        const int next = (state + 1) % 4;
        sel->flipH = (next & 1) != 0;
        sel->flipV = (next & 2) != 0;
        break;
      }
      case kScrMask: {
        // None, then each mask in the library.
        int at = -1;
        for (int i = 0; i < static_cast<int>(project_.masks.size()); ++i) {
          if (project_.masks[static_cast<std::size_t>(i)].id == sel->maskId) at = i;
        }
        at = at + 1 >= static_cast<int>(project_.masks.size()) ? -1 : at + 1;
        sel->maskId = at < 0 ? std::string() : project_.masks[static_cast<std::size_t>(at)].id;
        if (project_.masks.empty()) triggerToast("no masks yet: make one in a cue's MASK section");
        break;
      }
      case kScrMesh: {
        // Off, 3 x 3, 4 x 4, 5 x 5.
        const int n = deckboy::slices::gridActive(*sel) ? sel->gridCols : 0;
        const int next = n == 0 ? 3 : n >= 5 ? 0 : n + 1;
        sel->gridCols = sel->gridRows = next;
        sel->gridOffsets.assign(static_cast<std::size_t>(next * next * 2), 0.0f);
        break;
      }
      case kScrPerspective: sel->perspective = !sel->perspective; break;
      case kScrResetPins:
        sel->corners = {};
        break;
      default: break;
    }
    markProjectDirty();
  }

  void handleScreensMouseDown(int mx, int my) {
    for (auto it = screensButtons_.rbegin(); it != screensButtons_.rend(); ++it) {
      if (pointInRect(mx, my, it->r)) {
        handleScreensButton(*it);
        return;
      }
    }
    if (screensTab_ == 3) {
      stageMouseDown(mx, my);
      return;
    }
    if (screensTab_ != 0) return;
    OutputTarget* out = screensOutputTarget();
    if (!out || out->slices.empty()) return;
    const float fx = static_cast<float>(mx), fy = static_cast<float>(my);
    const float reach = 10.0f;
    auto closeTo = [&](SDL_FPoint p) { return std::abs(p.x - fx) <= reach && std::abs(p.y - fy) <= reach; };
    auto inside = [](const SDL_FPoint q[4], float px, float py) {
      // The quad as two triangles, by the sign of the cross products.
      auto side = [](SDL_FPoint a, SDL_FPoint b, float x0, float y0) {
        return (b.x - a.x) * (y0 - a.y) - (b.y - a.y) * (x0 - a.x);
      };
      bool neg = false, pos = false;
      for (int k = 0; k < 4; ++k) {
        const float d = side(q[k], q[(k + 1) % 4], px, py);
        neg = neg || d < 0;
        pos = pos || d > 0;
      }
      return !(neg && pos);
    };
    // The selected slice's handles first, then any slice's body, input then output.
    auto begin = [&](ScreensDragKind kind, int index, int slice) {
      screensSlice_ = slice;
      screensDrag_ = ScreensDrag {kind, index, fx, fy, out->slices[static_cast<std::size_t>(slice)]};
      pushUndoSnapshot();
    };
    if (deckboy::slices::Slice* sel = screensSelected()) {
      SDL_FPoint p[4], uv[4];
      screensSliceQuad(*sel, screensOut_, p, uv);
      if (deckboy::slices::gridActive(*sel)) {
        for (int r = 0; r < sel->gridRows; ++r) {
          for (int c = 0; c < sel->gridCols; ++c) {
            if (closeTo(screensMeshPoint(*sel, p, screensOut_, c, r))) {
              begin(ScreensDragKind::MeshPoint, r * sel->gridCols + c, screensSlice_);
              return;
            }
          }
        }
      }
      for (int k = 0; k < 4; ++k) {
        if (closeTo(p[k])) {
          begin(screensPinMode_ ? ScreensDragKind::PinCorner : ScreensDragKind::PlaceCorner, k, screensSlice_);
          return;
        }
      }
      const SDL_FPoint q[4] = {{screensIn_.x + sel->srcX * screensIn_.w, screensIn_.y + sel->srcY * screensIn_.h},
                               {screensIn_.x + (sel->srcX + sel->srcW) * screensIn_.w, screensIn_.y + sel->srcY * screensIn_.h},
                               {screensIn_.x + (sel->srcX + sel->srcW) * screensIn_.w,
                                screensIn_.y + (sel->srcY + sel->srcH) * screensIn_.h},
                               {screensIn_.x + sel->srcX * screensIn_.w, screensIn_.y + (sel->srcY + sel->srcH) * screensIn_.h}};
      for (int k = 0; k < 4; ++k) {
        if (closeTo(q[k])) {
          begin(ScreensDragKind::CropCorner, k, screensSlice_);
          return;
        }
      }
    }
    for (int i = static_cast<int>(out->slices.size()) - 1; i >= 0; --i) {
      const auto& s = out->slices[static_cast<std::size_t>(i)];
      const SDL_FPoint q[4] = {{screensIn_.x + s.srcX * screensIn_.w, screensIn_.y + s.srcY * screensIn_.h},
                               {screensIn_.x + (s.srcX + s.srcW) * screensIn_.w, screensIn_.y + s.srcY * screensIn_.h},
                               {screensIn_.x + (s.srcX + s.srcW) * screensIn_.w, screensIn_.y + (s.srcY + s.srcH) * screensIn_.h},
                               {screensIn_.x + s.srcX * screensIn_.w, screensIn_.y + (s.srcY + s.srcH) * screensIn_.h}};
      if (inside(q, fx, fy)) {
        begin(ScreensDragKind::CropMove, -1, i);
        return;
      }
      SDL_FPoint p[4], uv[4];
      screensSliceQuad(s, screensOut_, p, uv);
      if (inside(p, fx, fy)) {
        begin(ScreensDragKind::PlaceMove, -1, i);
        return;
      }
    }
  }

  void handleScreensMouseMotion(int mx, int my) {
    if (screensTab_ == 3) {
      stageMouseMotion(mx, my);
      return;
    }
    if (screensDrag_.kind == ScreensDragKind::None) return;
    deckboy::slices::Slice* sel = screensSelected();
    if (!sel) return;
    const auto& st = screensDrag_.start;
    deckboy::slices::Slice s = st;
    const float inDx = (mx - screensDrag_.x0) / std::max(1.0f, screensIn_.w);
    const float inDy = (my - screensDrag_.y0) / std::max(1.0f, screensIn_.h);
    const float outDx = (mx - screensDrag_.x0) / std::max(1.0f, screensOut_.w);
    const float outDy = (my - screensDrag_.y0) / std::max(1.0f, screensOut_.h);
    // A corner of a rectangle: it moves, the opposite one stays.
    auto dragCorner = [](float& x, float& y, float& w, float& h, int corner, float dx, float dy) {
      const bool left = corner == 0 || corner == 3, topSide = corner == 0 || corner == 1;
      if (left) { x += dx; w -= dx; } else { w += dx; }
      if (topSide) { y += dy; h -= dy; } else { h += dy; }
      if (w < 0.01f) { if (left) x -= 0.01f - w; w = 0.01f; }
      if (h < 0.01f) { if (topSide) y -= 0.01f - h; h = 0.01f; }
    };
    switch (screensDrag_.kind) {
      case ScreensDragKind::CropMove:
        s.srcX = std::clamp(st.srcX + inDx, 0.0f, 1.0f - st.srcW);
        s.srcY = std::clamp(st.srcY + inDy, 0.0f, 1.0f - st.srcH);
        break;
      case ScreensDragKind::CropCorner:
        dragCorner(s.srcX, s.srcY, s.srcW, s.srcH, screensDrag_.index, inDx, inDy);
        break;
      case ScreensDragKind::PlaceMove:
        s.dstX = st.dstX + outDx;
        s.dstY = st.dstY + outDy;
        break;
      case ScreensDragKind::PlaceCorner:
        dragCorner(s.dstX, s.dstY, s.dstW, s.dstH, screensDrag_.index, outDx, outDy);
        break;
      case ScreensDragKind::PinCorner:
        s.corners[static_cast<std::size_t>(screensDrag_.index * 2)] = st.corners[static_cast<std::size_t>(screensDrag_.index * 2)] + outDx;
        s.corners[static_cast<std::size_t>(screensDrag_.index * 2 + 1)] = st.corners[static_cast<std::size_t>(screensDrag_.index * 2 + 1)] + outDy;
        break;
      case ScreensDragKind::MeshPoint: {
        const std::size_t i = static_cast<std::size_t>(screensDrag_.index) * 2;
        if (i + 1 < s.gridOffsets.size()) {
          s.gridOffsets[i] = std::clamp(st.gridOffsets[i] + outDx, -1.0f, 1.0f);
          s.gridOffsets[i + 1] = std::clamp(st.gridOffsets[i + 1] + outDy, -1.0f, 1.0f);
        }
        break;
      }
      default: break;
    }
    *sel = deckboy::slices::clampSlice(s);
    markProjectDirty();
  }

  void endScreensDrag() {
    screensDrag_ = ScreensDrag {};
    stageDragging_ = false;
  }

  // ── SCREENS over the wire (for tests: scripted input does not reach SDL) ──
  //
  //   SCREENS                     open?, output, slice, the stages' rects
  //   SCREENS ON | OFF
  //   SCREENS CLICK x y [HOLD] | HOVER x y | UP     in the window's own pixels
  //   SCREENS BUTTONS             each button as action,param@x,y,w,h
  void handleScreensCommand(const std::vector<std::string>& parts) {
    const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();
    auto num = [&](std::size_t i) { return parts.size() > i ? std::atoi(parts[i].c_str()) : 0; };
    if (sub == "ON" || sub == "OFF") {
      setScreensVisible(sub == "ON");
      remoteCommandDetail_ = screensVisible() ? "open" : "closed";
      return;
    }
    if (sub == "CLICK" && parts.size() >= 4) {
      handleScreensMouseDown(num(2), num(3));
      if (parts.size() < 5 || toUpper(parts[4]) != "HOLD") endScreensDrag();
      remoteCommandDetail_ = "clicked";
      return;
    }
    if (sub == "HOVER" && parts.size() >= 4) {
      handleScreensMouseMotion(num(2), num(3));
      remoteCommandDetail_ = "moved";
      return;
    }
    if (sub == "UP") {
      endScreensDrag();
      remoteCommandDetail_ = "released";
      return;
    }
    if (sub == "SNAP" && parts.size() >= 3) {
      screensSnapPath_ = joinParts(parts, 2);
      remoteCommandDetail_ = "next frame to " + screensSnapPath_;
      return;
    }
    if (sub == "TAB" && parts.size() >= 3) {
      const std::string t = toUpper(parts[2]);
      screensTab_ = t == "MULTIVIEW" ? 1 : t == "LED" || t == "LEDMAP" ? 2 : t == "STAGE" ? 3 : 0;
      remoteCommandDetail_ = "tab " + std::to_string(screensTab_);
      return;
    }
    if (sub == "STAGE") {
      handleScreensStageCommand(parts);
      return;
    }
    if (sub == "BUTTONS") {
      std::ostringstream o;
      for (const auto& b : screensButtons_) {
        o << b.action << "," << b.param << "@" << b.r.x << "," << b.r.y << "," << b.r.w << "," << b.r.h << " ";
      }
      remoteCommandDetail_ = o.str();
      return;
    }
    if (!sub.empty()) {
      failRemoteCommand("SCREENS: ON, OFF, TAB <SLICES|MULTIVIEW|LED|STAGE>, STAGE ..., CLICK x y [HOLD], HOVER x y, "
                        "UP, BUTTONS or SNAP file.bmp");
      return;
    }
    std::ostringstream o;
    o << (screensVisible() ? "open" : "closed") << " output=" << screensOutput_ + 1 << " slice=" << screensSlice_ + 1
      << " input=" << static_cast<int>(screensIn_.x) << "," << static_cast<int>(screensIn_.y) << ","
      << static_cast<int>(screensIn_.w) << "," << static_cast<int>(screensIn_.h)
      << " output_stage=" << static_cast<int>(screensOut_.x) << "," << static_cast<int>(screensOut_.y) << ","
      << static_cast<int>(screensOut_.w) << "," << static_cast<int>(screensOut_.h)
      << " picture=" << (screensPic_ ? "yes" : "no");
    remoteCommandDetail_ = o.str();
  }
