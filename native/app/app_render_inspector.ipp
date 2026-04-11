// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
  void renderCueInspectorPanel(const SDL_Rect& shell) {
    const Deck& deck = focusedDeck();
    const MediaEngine* engine = focusedMediaEngine();
    const Cue* selectedCue = selectedCuePtr();
    const Cue* activeCue = activeCuePtr();
    int nextCueIndex = nextCueIndexForDeck(project_.focusedDeckIndex);
    const Cue* nextCue = (nextCueIndex >= 0 && nextCueIndex < static_cast<int>(deck.cues.size()))
      ? &deck.cues[nextCueIndex]
      : nullptr;
    (void)activeCue; (void)nextCue;

    quickButtons_.clear();
    cueSettingsQuickButtonStartIndex_ = 0;
    cueSettingsViewportRect_ = SDL_Rect {};
    cueSourceTypeDropdownRect_ = SDL_Rect {};
    cuePatternTypeDropdownRect_ = SDL_Rect {};
    cueTransitionStyleDropdownRect_ = SDL_Rect {};

    // Simple panel chrome (drawOperationalPanel removed)
    {
      constexpr int kOpHeaderH = 28;
      SDL_Rect hdr {shell.x, shell.y, shell.w, kOpHeaderH};
      drawUIPanel(hdr, pal.dark, pal.deep, pal.mid);
      drawText(controlRenderer_, fontPixelSmall_ ? fontPixelSmall_ : fontSmall_, "CUE INSPECTOR", pal.light, hdr.x + 6, hdr.y + 6);
      // Activity sparkle in header when a cue is selected
      if (selectedCue) {
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
        double phase = static_cast<double>(animationNow_) * 0.0015;
        SDL_Color hdrStar = pal.light;
        hdrStar.a = static_cast<Uint8>(60 + 120 * std::abs(std::sin(phase)));
        drawStar(controlRenderer_, hdr.x + hdr.w - 16, hdr.y + kOpHeaderH / 2, 2, hdrStar);
        if (engine && engine->state() == TransportState::Playing) {
          hdrStar.a = static_cast<Uint8>(80 + 140 * std::abs(std::sin(phase + 1.57)));
          drawStar(controlRenderer_, hdr.x + hdr.w - 30, hdr.y + kOpHeaderH / 2, 2, hdrStar);
        }
        SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
      }
    }
    constexpr int kOpHeaderH_ci = 28;
    SDL_Rect ctrl {shell.x, shell.y + kOpHeaderH_ci, shell.w, std::max(0, shell.h - kOpHeaderH_ci)};
    int kCtrlW = ctrl.w;
    constexpr int kDetailAreaH = 108;


    // Thumbnail of selected cue (top portion)
    constexpr int kThumbAreaH = 110;
    SDL_Rect thumbArea {ctrl.x + 4, ctrl.y + 4, kCtrlW - 8, kThumbAreaH};
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
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {thumbArea.x + 6, thumbArea.y + 4, thumbArea.w - 12, 16},
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
      SDL_RenderCopy(controlRenderer_, selectedThumbnailTex_, nullptr, &dst);
    } else if (selectedCue) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {thumbArea.x + 6, thumbArea.y + 8, thumbArea.w - 12, 16},
                   selectedCue->name, pal.mid);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {thumbArea.x + 6, thumbArea.y + 28, thumbArea.w - 12, 16},
                   "loading preview...", pal.mid);
    } else {
      SDL_RenderSetClipRect(controlRenderer_, &thumbArea);
      SDL_Rect emptyTitle {thumbArea.x + 10, thumbArea.y + thumbArea.h / 2 - 22, thumbArea.w - 20, 20};
      SDL_Rect emptyLineA {thumbArea.x + 10, thumbArea.y + thumbArea.h / 2 + 2, thumbArea.w - 20, 16};
      SDL_Rect emptyLineB {thumbArea.x + 10, thumbArea.y + thumbArea.h / 2 + 22, thumbArea.w - 20, 16};
      drawCenteredTextSafe(controlRenderer_, fontBase_, emptyTitle,
                           ellipsizeToPixelWidth(fontBase_, "NO CUE SELECTED", emptyTitle.w), pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, emptyLineA,
                           ellipsizeToPixelWidth(fontSmall_, "Select a cue or import media", emptyLineA.w), pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, emptyLineB,
                           ellipsizeToPixelWidth(fontSmall_, "Preview appears here before TAKE", emptyLineB.w), pal.mid);
      SDL_RenderSetClipRect(controlRenderer_, nullptr);
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
      if (!peaks.empty() || pending)
        drawWaveform(controlRenderer_, waveRect, peaks, selectedCue->audioChannels >= 2, playFrac, inFrac, outFrac,
                     selectedCue->pausePoints, dur);
    }

    // Label "cue panel" below thumb
    int ctrlSettingsY = ctrl.y + kThumbAreaH + 14;
    TTF_Font* inspectorHeaderFont = fontBase_ ? fontBase_ : fontSmall_;
    TTF_Font* inspectorValueFont = fontBase_ ? fontBase_ : fontSmall_;
    TTF_Font* inspectorLabelFont = fontSmall_;
    constexpr int kInspectorRowH = 36;
    constexpr int kInspectorRowStep = 46;
    constexpr int kInspectorSectionGap = 12;
    drawTextSafe(controlRenderer_, inspectorHeaderFont,
                 SDL_Rect {ctrl.x + 10, ctrlSettingsY, kCtrlW - 20, 24},
                 "CUE PANEL", pal.deep);

    // -- Shared inspector context for docked panel --
    InspectorCtx ix {ctrl, kCtrlW, 12, kInspectorRowH, kInspectorRowStep,
                     32, kInspectorSectionGap, inspectorHeaderFont, inspectorValueFont, inspectorLabelFont, false};

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
      int infoW = std::max(80, tableRect.w - 80 - 46 - 12);
      UITable table(tableRect, {80, 46, infoW}, ix.rowH, 6);
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

    int settingsContentTopY = ctrlSettingsY + 22;
    int settingsContentBottomY = ctrl.y + ctrl.h - 10;
    cueSettingsViewportRect_ = {
      ctrl.x + 6,
      settingsContentTopY - 2,
      kCtrlW - 12,
      std::max(0, settingsContentBottomY - settingsContentTopY + 2)
    };
    cueSettingsScroll_ = std::clamp(cueSettingsScroll_, 0, cueSettingsScrollMax_);
    cueSettingsQuickButtonStartIndex_ = quickButtons_.size();
    SDL_RenderSetClipRect(controlRenderer_,
      cueSettingsViewportRect_.h > 0 ? &cueSettingsViewportRect_ : nullptr);

    auto formatFloat = [](float v, int d = 2) { return fmtFloat(v, d); };
    auto formatPercent = [](float v) { return fmtPercent(v); };
    auto formatScaleMode = [](ScaleMode m) { return fmtScaleMode(m); };
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

    auto drawOverlayAttachmentSection = [&](int startY, const Cue& cue) {
      auto overlaysSection = beginInspectorSection(startY, "OVERLAYS", cueSectionRoutingOpen_,
                                                   QuickAction::CueSectionRoutingToggle,
                                                   "Overlay bin items that fire when this cue is taken");
      int overlayY = overlaysSection.bodyStartY;
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
        std::string lowerThirdValue = attachedOverlaySummaryLabel(deck, cue, CueKind::LowerThird);
        overlayY = drawInspectorEditableRow(overlayY, "lower 3rd",
                                            lowerThirdValue,
                                            QuickAction::EditAttachedLowerThirdCue,
                                            "Choose a lower third from the overlay bin to fire on TAKE",
                                            attachedColor(lowerThirdValue));
        std::string pipValue = attachedOverlaySummaryLabel(deck, cue, CueKind::Pip);
        overlayY = drawInspectorEditableRow(overlayY, "pip",
                                            pipValue,
                                            QuickAction::EditAttachedPipCue,
                                            "Choose a PIP overlay from the overlay bin to fire on TAKE",
                                            attachedColor(pipValue));
        overlayY = drawInspectorMessageRow(overlayY, "Attached overlays fire on TAKE only",
                                           pal.light,
                                           pal.dark);
      }
      finishInspectorSection(overlaysSection, overlayY);
      return cueSectionRoutingOpen_ ? overlayY : overlaysSection.bodyStartY;
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
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
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
        drawText(controlRenderer_, fontSmall_,
                 std::to_string(panelSelectedCues.size()) + " cues: " + kindSummary,
                 pal.inkSoft, ctrl.x + 10, ry + 4);
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
          drawText(controlRenderer_, fontSmall_, "end: " + std::string(mixedEnd ? "mixed" : cueEndActionLabel(endAction)) + "  [X cycle]",
                   pal.deep, endBtn.x + 10, endBtn.y + 8);
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
          drawText(controlRenderer_, fontSmall_,
                   ellipsizeToPixelWidth(fontSmall_, styleLabel, styleBtn.w - 18),
                   styleInk, styleBtn.x + 6, styleBtn.y + 6);
          drawText(controlRenderer_, fontSmall_, "\xe2\x96\xbc", styleInk, styleBtn.x + styleBtn.w - 14, styleBtn.y + 6);
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
        if (gotoDisplay.size() > 28) gotoDisplay = gotoDisplay.substr(0, 25) + "...";
        Primitives::drawFramedPanel(controlRenderer_, gotoBox, pal.light,
                                    pal.deep, pal.mid);
        drawText(controlRenderer_, fontSmall_, gotoDisplay, pal.deep, gotoBox.x + 6, gotoBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, gotoEdit, pal.dark,
                                    pal.deep, pal.mid);
        drawCenteredText(controlRenderer_, fontSmall_, "goto", pal.light, gotoEdit);
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
        drawCenteredText(controlRenderer_, fontSmall_, "tag: " + tagStr + "  [K cycle]",
                         pal.light, tagBtn);
        quickButtons_.push_back({tagBtn, QuickAction::CycleColorTag, "Cycle color tag for selected cues"});
        ry += kRowStep;

        SDL_Rect notesBox {ctrl.x + 10, ry, kCtrlW - 80, 26};
        SDL_Rect notesEdit {ctrl.x + kCtrlW - 64, ry, 54, 26};
        std::string notesDisplay = stringMixedLabel([&](const Cue& cue) { return cue.notes; }, "(no notes)");
        if (notesDisplay.size() > 28) notesDisplay = notesDisplay.substr(0, 25) + "...";
        Primitives::drawFramedPanel(controlRenderer_, notesBox, pal.light,
                                    pal.deep, pal.mid);
        drawText(controlRenderer_, fontSmall_, notesDisplay,
                 colorFromRgba(notesDisplay == "(no notes)" ? kScreenInkSoftColor : kScreenDeepColor),
                 notesBox.x + 6, notesBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, notesEdit, pal.dark,
                                    pal.deep, pal.mid);
        drawCenteredText(controlRenderer_, fontSmall_, "edit", pal.light, notesEdit);
        quickButtons_.push_back({notesEdit, QuickAction::EditNotes, "Edit notes for selected cues"});
        ry += kRowStep;

        SDL_Rect cueIdBox {ctrl.x + 10, ry, kCtrlW - 80, 26};
        SDL_Rect cueIdEdit {ctrl.x + kCtrlW - 64, ry, 54, 26};
        std::string cueIdDisplay = stringMixedLabel([&](const Cue& cue) { return cue.cueId; }, "(none)");
        Primitives::drawFramedPanel(controlRenderer_, cueIdBox, pal.light,
                                    pal.deep, pal.mid);
        drawText(controlRenderer_, fontSmall_, cueIdDisplay, pal.deep, cueIdBox.x + 6, cueIdBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, cueIdEdit, pal.dark,
                                    pal.deep, pal.mid);
        drawCenteredText(controlRenderer_, fontSmall_, "edit", pal.light, cueIdEdit);
        quickButtons_.push_back({cueIdEdit, QuickAction::EditCueNumber, "Set cue ID for selected cues"});
        ry += kRowStep;
      }
      finishInspectorSection(playbackSection, ry);

      auto geometrySection = beginInspectorSection(ry + kInspectorSectionGap, "GEOMETRY", cueSectionGeometryOpen_,
                                                   QuickAction::CueSectionGeometryToggle,
                                                   "Common geometry controls");
      ry = geometrySection.bodyStartY;
      if (cueSectionGeometryOpen_) {
        if (allSupportsGeometry) {
          ry = drawGeometryRows(ry, *selectedCue, true);
          ry = drawColorRows(ry, *selectedCue);
        } else {
          drawText(controlRenderer_, fontSmall_, "mixed selection: geometry unavailable",
                   pal.inkSoft, ctrl.x + 10, ry + 4);
          ry += kRowStep;
        }
      }
      finishInspectorSection(geometrySection, ry);

      auto keySection = beginInspectorSection(ry + kInspectorSectionGap, "KEY", cueSectionKeyOpen_,
                                              QuickAction::CueSectionKeyToggle,
                                              "Common key controls");
      ry = keySection.bodyStartY;
      if (cueSectionKeyOpen_) {
        if (allSupportsKey) {
          ry = drawKeyRows(ry, *selectedCue);
        } else {
          drawText(controlRenderer_, fontSmall_, "mixed selection: key unavailable",
                   pal.inkSoft, ctrl.x + 10, ry + 4);
          ry += kRowStep;
        }
      }
      finishInspectorSection(keySection, ry);

    } else if (selectedCue && selectedCue->kind == CueKind::Video) {
      int volPct = static_cast<int>(std::round((engine ? engine->volume() : 1.0f) * 100.0f));
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      constexpr int kRowStep = kInspectorRowStep;
      auto playbackSection = beginInspectorSection(ry, "PLAYBACK", cueSectionPlaybackOpen_,
                                                   QuickAction::CueSectionPlaybackToggle,
                                                   "Collapse/expand playback settings");
      int playbackBodyY = playbackSection.bodyStartY;
      int playbackRowsUsed = 8;
      if (cueSectionPlaybackOpen_) {
        ry = playbackBodyY;
      drawQuickRow(ry,                "volume",      QuickAction::VolDec,     std::to_string(volPct) + "%",               QuickAction::VolInc,    QuickAction::ToggleLoop, false, false, "Volume: +/- keys or click to adjust");
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
          drawText(controlRenderer_, fontSmall_,
                   ellipsizeToPixelWidth(fontSmall_, styleLabel, styleBtn.w - 18),
                   styleInk, styleBtn.x + 6, styleBtn.y + 6);
          drawText(controlRenderer_, fontSmall_, "\xe2\x96\xbc", styleInk, styleBtn.x + styleBtn.w - 14, styleBtn.y + 6);
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
        drawCenteredText(controlRenderer_, fontSmall_, std::string("loop: ") + (selectedCue->loop ? "on" : "off"), loopInk, loopBtn);
        quickButtons_.push_back({loopBtn, QuickAction::ToggleLoop, "L — loop this cue continuously"});
        Primitives::drawFramedPanel(controlRenderer_, holdBtn, holdFill, pal.deep, pal.mid);
        drawCenteredText(controlRenderer_, fontSmall_, std::string("hold: ") + (selectedCue->pauseOnLastFrame ? "on" : "off"), holdInk, holdBtn);
        quickButtons_.push_back({holdBtn, QuickAction::ToggleHold, "E — freeze on last frame instead of stopping"});
      }
      SDL_Rect endBtn {ctrl.x + 10, ry + kRowStep * 8, kCtrlW - 20, 30};
      Primitives::drawFramedPanel(controlRenderer_, endBtn, pal.light, pal.deep, pal.mid);
      drawText(controlRenderer_, fontSmall_, "end: " + cueEndActionLabel(selectedCue->endAction) + "  [X cycle]",
               pal.deep, endBtn.x + 10, endBtn.y + 8);
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

        std::string cueAudioLabel = selectedCue->hasAudio
          ? (selectedCue->audioEnabled ? "on" : "off")
          : "n/a";
        drawQuickRow(ry + kRowStep * rowCursor, "audio", QuickAction::ToggleCueAudio,
                     cueAudioLabel,
                     QuickAction::ToggleCueAudio, QuickAction::ToggleCueAudio, true,
                     selectedCue->hasAudio && selectedCue->audioEnabled,
                     "Toggle cue audio track for this cue");
        rowCursor += 1;

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
        if (gotoDisplay.size() > 28) {
          gotoDisplay = gotoDisplay.substr(0, 25) + "...";
        }
        Primitives::drawFramedPanel(controlRenderer_, gotoBox, pal.light,
                                    pal.deep, pal.mid);
        drawText(controlRenderer_, fontSmall_, gotoDisplay, pal.deep, gotoBox.x + 6, gotoBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, gotoEdit, pal.dark,
                                    pal.deep, pal.mid);
        drawCenteredText(controlRenderer_, fontSmall_, "goto", pal.light, gotoEdit);
        quickButtons_.push_back({gotoEdit, QuickAction::EditGotoTarget, "Set cue token to jump to when cue ends"});
        rowCursor += 1;

        std::string tagStr = selectedCue->colorTag.empty() ? "none" : selectedCue->colorTag;
        SDL_Rect tagBtn {ctrl.x + 10, ry + kRowStep * rowCursor, kCtrlW - 20, 28};
        SDL_Color tagFill = colorTagToSdl(selectedCue->colorTag, 200);
        Primitives::drawFramedPanel(controlRenderer_, tagBtn, tagFill, pal.deep, pal.mid);
        drawCenteredText(controlRenderer_, fontSmall_, "tag: " + tagStr + "  [K cycle]", pal.light, tagBtn);
        quickButtons_.push_back({tagBtn, QuickAction::CycleColorTag, "C — cycle cue color tag"});
        rowCursor += 1;

        int notesY = ry + kRowStep * rowCursor;
        SDL_Rect notesBox {ctrl.x + 10, notesY, kCtrlW - 80, 26};
        SDL_Rect notesEdit {ctrl.x + kCtrlW - 64, notesY, 54, 26};
        std::string notesDisplay = selectedCue->notes.empty() ? "(no notes)" : selectedCue->notes;
        if (notesDisplay.size() > 28) notesDisplay = notesDisplay.substr(0, 25) + "...";
        Primitives::drawFramedPanel(controlRenderer_, notesBox, pal.light, pal.deep, pal.mid);
        drawText(controlRenderer_, fontSmall_, notesDisplay, colorFromRgba(selectedCue->notes.empty() ? kScreenInkSoftColor : kScreenDeepColor), notesBox.x + 6, notesBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, notesEdit, pal.dark, pal.deep, pal.mid);
        drawCenteredText(controlRenderer_, fontSmall_, "edit", pal.light, notesEdit);
        quickButtons_.push_back({notesEdit, QuickAction::EditNotes, "Click to edit cue notes"});
        rowCursor += 1;

        int cnY = ry + kRowStep * rowCursor;
        SDL_Rect idLabel {ctrl.x + 10, cnY, 36, 26};
        SDL_Rect val {ctrl.x + 52, cnY, kCtrlW - 122, 26};
        SDL_Rect editBtn {ctrl.x + kCtrlW - 64, cnY, 54, 26};
        drawText(controlRenderer_, fontSmall_, "id", pal.inkSoft, idLabel.x + 4, idLabel.y + 6);
        std::string cnDisplay = cueDisplayToken(*selectedCue, focusedDeck().selectedIndex);
        Primitives::drawFramedPanel(controlRenderer_, val, pal.light, pal.deep, pal.mid);
        drawText(controlRenderer_, fontSmall_, cnDisplay, pal.deep, val.x + 6, val.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, editBtn, pal.dark, pal.deep, pal.mid);
        drawCenteredText(controlRenderer_, fontSmall_, "edit", pal.light, editBtn);
        quickButtons_.push_back({editBtn, QuickAction::EditCueNumber, "Set short cue id for search/goto"});
        rowCursor += 1;

        int ppY = ry + kRowStep * rowCursor;
        int ppCount = static_cast<int>(selectedCue->pausePoints.size());
        SDL_Rect ppLabel {ctrl.x + 10, ppY, 72, 26};
        SDL_Rect addBtn {ctrl.x + 88, ppY, 46, 26};
        SDL_Rect clrBtn {ctrl.x + 140, ppY, 46, 26};
        drawText(controlRenderer_, fontSmall_, "pause pts: " + std::to_string(ppCount),
                 pal.inkSoft, ppLabel.x + 4, ppLabel.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, addBtn, pal.dark, pal.deep, pal.mid);
        drawCenteredText(controlRenderer_, fontSmall_, "+now", pal.light, addBtn);
        Primitives::drawFramedPanel(controlRenderer_, clrBtn, pal.deleteBezel, pal.deep, pal.mid);
        drawCenteredText(controlRenderer_, fontSmall_, "clr", pal.light, clrBtn);
        quickButtons_.push_back({addBtn, QuickAction::AddPausePoint, "Add pause point at current position"});
        quickButtons_.push_back({clrBtn, QuickAction::ClearPausePoints, "Clear all pause points"});
        rowCursor += 1;

        playbackRowsUsed = rowCursor;
      }
      }
      finishInspectorSection(playbackSection, cueSectionPlaybackOpen_ ? (ry + kRowStep * playbackRowsUsed) : playbackBodyY);
      int geoY = cueSectionPlaybackOpen_ ? (ry + kRowStep * playbackRowsUsed + kInspectorSectionGap)
                                         : (playbackBodyY + kInspectorSectionGap);
      auto geometrySection = beginInspectorSection(geoY, "GEOMETRY", cueSectionGeometryOpen_,
                                                   QuickAction::CueSectionGeometryToggle,
                                                   "Collapse/expand geometry controls");
      geoY = geometrySection.bodyStartY;
      if (cueSectionGeometryOpen_) {
        geoY = drawGeometryRows(geoY, *selectedCue, true);
        geoY = drawColorRows(geoY, *selectedCue);
      }
      finishInspectorSection(geometrySection, geoY);

      auto keySection = beginInspectorSection(geoY + kInspectorSectionGap, "KEY", cueSectionKeyOpen_,
                                              QuickAction::CueSectionKeyToggle,
                                              "Collapse/expand key controls");
      geoY = keySection.bodyStartY;
      if (cueSectionKeyOpen_) {
        geoY = drawKeyRows(geoY, *selectedCue);
      }
      finishInspectorSection(keySection, geoY);

      drawOverlayAttachmentSection(geoY + kInspectorSectionGap, *selectedCue);

    } else if (selectedCue && selectedCue->kind == CueKind::Pip) {
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      constexpr int kRowStep = kInspectorRowStep;
      auto playbackSection = beginInspectorSection(ry, "PLAYBACK", cueSectionPlaybackOpen_,
                                                   QuickAction::CueSectionPlaybackToggle,
                                                   "Picture-in-picture overlay controls");
      int playbackY = playbackSection.bodyStartY;
      if (cueSectionPlaybackOpen_) {
        auto drawPipPresetButtonsRow = [&](int rowY,
                                           const std::string& label,
                                           const std::vector<std::pair<std::string, QuickAction>>& presets,
                                           const std::string& tip) {
          constexpr int kLabelW = 64;
          constexpr int kGap = 6;
          SDL_Rect labelRect {ctrl.x + 10, rowY, kLabelW, kInspectorRowH};
          drawTextSafe(controlRenderer_, inspectorLabelFont, labelRect, label,
                       pal.inkSoft);
          int buttonsX = labelRect.x + labelRect.w + kGap;
          int buttonsW = std::max(80, (kCtrlW - 20) - kLabelW - kGap);
          int btnW = std::max(38, (buttonsW - kGap * static_cast<int>(presets.size() - 1)) /
                                    std::max(1, static_cast<int>(presets.size())));
          for (size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex) {
            SDL_Rect btn {
              buttonsX + static_cast<int>(presetIndex) * (btnW + kGap),
              rowY,
              btnW,
              kInspectorRowH
            };
            drawUIPanel(btn, pal.light, pal.deep,
                        pal.mid);
            drawCenteredTextSafe(controlRenderer_, fontSmall_, btn, presets[presetIndex].first,
                                 pal.deep);
            quickButtons_.push_back({btn, presets[presetIndex].second, tip});
          }
          return rowY + kInspectorRowStep;
        };

        std::string sourceType = pipSourceTypeTokenFromCue(*selectedCue);
        bool legacyMode = sourceType == "legacy";
        Cue resolvedPipCue;
        bool sourceReady = buildResolvedPipSourceCue(deck, *selectedCue, resolvedPipCue, nullptr);
        playbackY = drawInspectorMessageRow(playbackY, "PIP overlay cue",
                                            pal.mid,
                                            pal.deep);
        SDL_Rect sourceTypeLabelRect {ctrl.x + 10, playbackY, 72, kInspectorRowH};
        SDL_Rect sourceTypeBtn {sourceTypeLabelRect.x + sourceTypeLabelRect.w + 8, playbackY,
                                kCtrlW - 20 - sourceTypeLabelRect.w - 8, kInspectorRowH};
        drawTextSafe(controlRenderer_, inspectorLabelFont, sourceTypeLabelRect, "type",
                     pal.inkSoft);
        drawUIPanel(sourceTypeBtn, pal.light, pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, inspectorValueFont,
                     SDL_Rect {sourceTypeBtn.x + 6, sourceTypeBtn.y, sourceTypeBtn.w - 18, sourceTypeBtn.h},
                     ellipsizeToPixelWidth(inspectorValueFont, pipSourceTypeLabel(sourceType), sourceTypeBtn.w - 18),
                     pal.deep);
        drawCenteredTextSafe(controlRenderer_, fontSmall_,
                             SDL_Rect {sourceTypeBtn.x + sourceTypeBtn.w - 14, sourceTypeBtn.y, 14, sourceTypeBtn.h},
                             "v", pal.deep);
        cueSourceTypeDropdownRect_ = sourceTypeBtn;
        playbackY += kInspectorRowStep;
        std::string sourceLabel = legacyMode
          ? "target"
          : (sourceType == "browser" ? "url"
             : (pipSourceTypeUsesSourceRef(sourceType) ? "source" : "media"));
        QuickAction sourceAction = legacyMode ? QuickAction::EditPipTarget : QuickAction::EditPipSourcePath;
        playbackY = drawInspectorEditableRow(
          playbackY,
          sourceLabel,
          pipSourceDisplayLabel(*selectedCue),
          sourceAction,
          legacyMode
            ? "Choose the legacy cue token to show inside the PIP window"
            : "Set the self-contained PIP source for this overlay",
          sourceReady ? pal.deep : SDL_Color {140, 40, 20, 255});
        playbackY = drawInspectorStatusRow(playbackY, "state",
                                           sourceReady ? "ready" : "source missing",
                                           !sourceReady);
        if (legacyMode) {
          playbackY = drawInspectorMessageRow(playbackY, "Legacy target mode — switch type to convert",
                                              pal.light,
                                              pal.dark);
        }
        playbackY = drawPipPresetButtonsRow(playbackY, "corner",
                                            {
                                              {"TL", QuickAction::PipPresetCornerTL},
                                              {"TR", QuickAction::PipPresetCornerTR},
                                              {"BL", QuickAction::PipPresetCornerBL},
                                              {"BR", QuickAction::PipPresetCornerBR},
                                            },
                                            "Snap the PIP window to a corner");
        playbackY = drawPipPresetButtonsRow(playbackY, "size",
                                            {
                                              {"SM", QuickAction::PipPresetSmall},
                                              {"BIG", QuickAction::PipPresetBig},
                                              {"70/30", QuickAction::PipPreset7030},
                                            },
                                            "Apply a common PIP size preset");
        playbackY = drawInspectorMessageRow(playbackY, "Use GEOMETRY for exact placement",
                                            pal.light,
                                            pal.dark);
        playbackY = drawInspectorActionRow(playbackY, "CLEAR OVERLAY  [Backspace]",
                                           QuickAction::ClearOverlay,
                                           "Clear the live PIP overlay now");
      }
      finishInspectorSection(playbackSection, playbackY);

      int geometryStartY = cueSectionPlaybackOpen_ ? (playbackY + kInspectorSectionGap)
                                                   : (playbackSection.bodyStartY + kInspectorSectionGap);
      auto geometrySection = beginInspectorSection(geometryStartY, "GEOMETRY", cueSectionGeometryOpen_,
                                                   QuickAction::CueSectionGeometryToggle,
                                                   "PIP size, placement, and color controls");
      int geometryY = geometrySection.bodyStartY;
      if (cueSectionGeometryOpen_) {
        geometryY = drawGeometryRows(geometryY, *selectedCue, true);
        geometryY = drawColorRows(geometryY, *selectedCue);
      }
      finishInspectorSection(geometrySection, geometryY);

      auto keySection = beginInspectorSection(geometryY + kInspectorSectionGap, "KEY", cueSectionKeyOpen_,
                                              QuickAction::CueSectionKeyToggle,
                                              "PIP chroma key controls");
      int keyY = keySection.bodyStartY;
      if (cueSectionKeyOpen_) {
        keyY = drawKeyRows(keyY, *selectedCue);
      }
      finishInspectorSection(keySection, keyY);

      int metadataStartY = keyY + kInspectorSectionGap;
      auto metadataSection = beginInspectorSection(metadataStartY, "METADATA", cueSectionMetadataOpen_,
                                                   QuickAction::CueSectionMetadataToggle,
                                                   "Cue notes and tags");
      int metadataY = metadataSection.bodyStartY;
      if (cueSectionMetadataOpen_) {
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
    } else if (selectedCue && selectedCue->kind == CueKind::LowerThird) {
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
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
                               || selectedCue->kind == CueKind::SrtStream
                               || selectedCue->kind == CueKind::NdiSource
                               || isSourceCueKind(selectedCue->kind))) {
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
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

        if (selectedCue->kind != CueKind::SrtStream && selectedCue->kind != CueKind::NdiSource) {
          std::string durVal = selectedCue->stillDurationSeconds > 0.0
            ? formatSeconds(selectedCue->stillDurationSeconds) : "hold";
          drawQuickRow(playbackY, "duration", QuickAction::DurDec, durVal, QuickAction::DurInc,
                       QuickAction::ToggleLoop, false, false, "Auto-advance duration — 0 = hold until taken");
          playbackY += kRowStep;
        }

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
                       "style: " + transitionStyleLabel(curStyle), styleInk);
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
          drawCenteredTextSafe(controlRenderer_, fontSmall_, loopBtn,
                               std::string("loop: ") + (selectedCue->loop ? "on" : "off"), loopInk);
          quickButtons_.push_back({loopBtn, QuickAction::ToggleLoop, "L — loop this cue"});
          drawUIPanel(holdBtn, holdFill, pal.deep, pal.mid);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, holdBtn,
                               std::string("hold: ") + (selectedCue->pauseOnLastFrame ? "on" : "off"), holdInk);
          quickButtons_.push_back({holdBtn, QuickAction::ToggleHold, "E — hold on this cue indefinitely"});
        }
        playbackY += kRowStep;

        SDL_Rect endBtn {ctrl.x + 10, playbackY, kCtrlW - 20, 30};
        drawUIPanel(endBtn, pal.light, pal.deep, pal.mid);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {endBtn.x + 10, endBtn.y, endBtn.w - 20, endBtn.h},
                     "end: " + cueEndActionLabel(selectedCue->endAction) + "  [X cycle]",
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
          std::string sourceTypeLabel = sourceCueLabelForType(sourceCueTokenForKind(selectedCue->kind));
          SDL_Rect sourceTypeLabelRect {ctrl.x + 10, metadataY, 78, kInspectorRowH};
          SDL_Rect sourceTypeBtn {sourceTypeLabelRect.x + sourceTypeLabelRect.w + 8, metadataY,
                                  kCtrlW - 20 - sourceTypeLabelRect.w - 8, kInspectorRowH};
          drawTextSafe(controlRenderer_, fontBase_, sourceTypeLabelRect, "type", pal.inkSoft);
          drawUIPanel(sourceTypeBtn, pal.light, pal.deep, pal.mid);
          drawTextSafe(controlRenderer_, fontBase_,
                       SDL_Rect {sourceTypeBtn.x + 6, sourceTypeBtn.y, sourceTypeBtn.w - 18, sourceTypeBtn.h},
                       ellipsizeToPixelWidth(fontBase_, sourceTypeLabel, sourceTypeBtn.w - 18),
                       pal.deep);
          drawCenteredTextSafe(controlRenderer_, fontSmall_,
                               SDL_Rect {sourceTypeBtn.x + sourceTypeBtn.w - 14, sourceTypeBtn.y, 14, sourceTypeBtn.h},
                               "v", pal.deep);
          cueSourceTypeDropdownRect_ = sourceTypeBtn;
          metadataY += kInspectorRowStep;

          std::string sourceRef = sourceCueRefFromCue(*selectedCue);
          if (sourceRef.empty()) {
            sourceRef = defaultSourceRefForKind(selectedCue->kind);
          }
          metadataY = drawInspectorEditableRow(metadataY, "source",
                                               sourceCueRefFriendlyLabel(selectedCue->kind, sourceRef),
                                               QuickAction::EditSourceRef,
                                               "Set capture source from the cue menu");
        }

        if (selectedCue->kind == CueKind::SrtStream) {
          metadataY = drawInspectorEditableRow(metadataY, "url",
                                               selectedCue->path.empty() ? "(unset)" : selectedCue->path,
                                               QuickAction::EditBrowserUrl,
                                               "Set stream URL (srt://, rtmp://, rtsp://...)");
        }

        if (selectedCue->kind == CueKind::NdiSource) {
          std::string ndiDisplay = selectedCue->path;
          if (ndiDisplay.rfind("ndi://", 0) == 0) ndiDisplay = ndiDisplay.substr(6);
          metadataY = drawInspectorEditableRow(metadataY, "source",
                                               ndiDisplay.empty() ? "(unset)" : ndiDisplay,
                                               QuickAction::EditBrowserUrl,
                                               "Set NDI source name");
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

      auto keySection = beginInspectorSection(sectionY + kInspectorSectionGap, "KEY", cueSectionKeyOpen_,
                                              QuickAction::CueSectionKeyToggle,
                                              "Collapse/expand key controls");
      sectionY = keySection.bodyStartY;
      if (cueSectionKeyOpen_) {
        sectionY = drawKeyRows(sectionY, *selectedCue);
      }
      finishInspectorSection(keySection, sectionY);

      drawOverlayAttachmentSection(sectionY + kInspectorSectionGap, *selectedCue);

    } else if (selectedCue && selectedCue->kind == CueKind::Audio) {
      // Audio-only cue settings
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      constexpr int kRowStep = kInspectorRowStep;
      auto playbackSection = beginInspectorSection(ry, "PLAYBACK", cueSectionPlaybackOpen_,
                                                   QuickAction::CueSectionPlaybackToggle,
                                                   "Audio cue playback settings");
      int playbackY = playbackSection.bodyStartY;
      int volPct = static_cast<int>(std::round((engine ? engine->volume() : 1.0f) * 100.0f));
      if (cueSectionPlaybackOpen_) {
        drawQuickRow(playbackY, "volume", QuickAction::VolDec, std::to_string(volPct) + "%", QuickAction::VolInc,
                     QuickAction::ToggleLoop, false, false, "Volume: +/- keys or click to adjust");
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
          drawCenteredTextSafe(controlRenderer_, fontSmall_, loopBtn, std::string("loop: ") + (selectedCue->loop ? "on" : "off"), loopInk);
          quickButtons_.push_back({loopBtn, QuickAction::ToggleLoop, "L — loop this audio"});
          drawUIPanel(holdBtn, holdFill, pal.deep, pal.mid);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, holdBtn, std::string("hold: ") + (selectedCue->pauseOnLastFrame ? "on" : "off"), holdInk);
          quickButtons_.push_back({holdBtn, QuickAction::ToggleHold, "E — hold at end"});
        }
        playbackY += kRowStep;
        {
          SDL_Rect endBtn {ctrl.x + 10, playbackY, kCtrlW - 20, 30};
          drawUIPanel(endBtn, pal.light, pal.deep, pal.mid);
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect {endBtn.x + 10, endBtn.y, endBtn.w - 20, endBtn.h},
                       "end: " + cueEndActionLabel(selectedCue->endAction) + "  [X cycle]",
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

      drawOverlayAttachmentSection(metadataY + kInspectorSectionGap, *selectedCue);

    } else if (!selectedCue) {
      int emptyW = kCtrlW - 20;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {ctrl.x + 10, ctrlSettingsY + 18, emptyW, 16},
                   ellipsizeToPixelWidth(fontSmall_, "select a cue to edit settings", emptyW),
                   pal.mid);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {ctrl.x + 10, ctrlSettingsY + 38, emptyW, 16},
                   ellipsizeToPixelWidth(fontSmall_, "import, source, or pattern to get started", emptyW),
                   pal.mid);
    } else {
      int emptyW = kCtrlW - 20;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {ctrl.x + 10, ctrlSettingsY + 24, emptyW, 16},
                   ellipsizeToPixelWidth(fontSmall_, "no per-cue settings for this type", emptyW),
                   pal.mid);
    }

    SDL_RenderSetClipRect(controlRenderer_, nullptr);
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

    // --- Cart details --- (anchored to bottom of panel, always kDetailAreaH px tall)
    int detailX = ctrl.x;
    int detailY = ctrl.y + ctrl.h - kDetailAreaH;
    int detailBottom = detailY + kDetailAreaH;
    SDL_Rect detailRect {detailX, detailY, ctrl.w, kDetailAreaH};
    drawUIPanel(detailRect, pal.light, pal.deep, pal.mid);
    SDL_Rect detailLabelRect {detailRect.x + 4, detailRect.y + 4, detailRect.w - 8, 16};
    SDL_Rect detailTitleRect {detailRect.x + 4, detailRect.y + 20, detailRect.w - 8, 18};
    drawTextSafe(controlRenderer_, fontSmall_, detailLabelRect, "Cue Details", pal.deep);
    drawTextSafe(controlRenderer_, fontBase_, detailTitleRect,
                 selectedCue ? selectedCue->name : "Drop or import media",
                 pal.deep);

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

    if (!selectedCue) {
      if (detailY + 40 < detailBottom)
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {detailX, detailY + 40, ctrl.w, 16},
                     "Drop files here or press Import to add cues.",
                     pal.inkSoft);
      if (detailY + 56 < detailBottom)
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {detailX, detailY + 56, ctrl.w, 16},
                     "Shift + arrows shuffles the selected cue up or down.",
                     pal.inkSoft);
      return;
    }

    // Path/URL line — rendered directly with clip rect for full width, no ellipsis truncation
    int detailLineY = detailY + 38;
    {
      std::string pathPrefix = selectedCue->kind == CueKind::Browser ? "URL: " : "Path: ";
      std::string fullPath = pathPrefix + selectedCue->path;
      int availW = ctrl.w - 8;
      int pathTextW = 0;
      TTF_SizeUTF8(fontSmall_, fullPath.c_str(), &pathTextW, nullptr);
      SDL_Rect pathClip {detailX, detailLineY, availW, 34};
      SDL_RenderSetClipRect(controlRenderer_, &pathClip);
      if (pathTextW > availW) {
        // Two lines: prefix on first, path on second
        drawText(controlRenderer_, fontSmall_, pathPrefix, pal.inkSoft, detailX, detailLineY);
        drawText(controlRenderer_, fontSmall_, selectedCue->path, pal.inkSoft, detailX, detailLineY + 16);
        detailLineY += 34;
      } else {
        drawText(controlRenderer_, fontSmall_, fullPath, pal.inkSoft, detailX, detailLineY);
        detailLineY += 18;
      }
      SDL_RenderSetClipRect(controlRenderer_, nullptr);
    }

    std::vector<std::string> infoLines {
      "Kind: " + cueKindLabel(selectedCue->kind) + "   " + std::to_string(selectedCue->width) + "x" + std::to_string(selectedCue->height) + "   Duration: " + formatSeconds(selectedCue->duration),
      "Format: " + selectedCue->formatName + "   Video: " + selectedCue->videoCodec + "   Audio: " + (selectedCue->audioCodec.empty() ? "none" : selectedCue->audioCodec),
      "In: " + formatSeconds(selectedCue->inPointSeconds) + "   Out: " + formatSeconds(selectedCue->outPointSeconds > 0.0 ? selectedCue->outPointSeconds : selectedCue->duration) + "   Size: " + std::to_string(static_cast<unsigned long long>(selectedCue->sizeBytes / 1024)) + " KB",
    };

    for (size_t i = 0; i < infoLines.size(); ++i) {
      if (detailLineY + 14 > detailBottom) break;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {detailX, detailLineY, ctrl.w, 14},
                   infoLines[i], pal.inkSoft);
      detailLineY += 18;
    }
    if (selectedCue && !selectedCue->notes.empty()) {
      int notesLineY = detailLineY;
      if (notesLineY + 14 <= detailBottom) {
        std::string notesStr = "\xe2\x80\x9c" + selectedCue->notes + "\xe2\x80\x9d";
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {detailX, notesLineY, ctrl.w, 14},
                     notesStr, pal.deep);
      }
    }
  }
