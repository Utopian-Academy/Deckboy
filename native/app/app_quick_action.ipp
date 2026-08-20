// ============================================================================
// app_quick_action.ipp — Quick-action command palette (Ctrl+K).
//
// Implements the quick-action system for rapid keyboard-driven operations:
//
//   dispatchQuickAction()     — execute a QuickAction enum value
//   renderQuickActionPalette() — render the searchable command palette overlay
//   filterQuickActions()      — filter available actions by search query
//
// Quick actions provide fast access to common operations without navigating
// menus: toggle loop, toggle hold, copy/paste cue settings, set transition
// style, change cue kind, add patterns, etc.
//
// The palette is opened with Ctrl+K and supports fuzzy search filtering.
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Execute a quick action. Most actions modify the selected cue's properties.
  void dispatchQuickAction(QuickAction action) {
    if (action == QuickAction::CopyCueSettings) {
      copySelectedCueSettings();
      return;
    }
    if (action == QuickAction::PasteCueSettings) {
      pasteSelectedCueSettings();
      return;
    }
    if (action == QuickAction::ResetCueSettings) {
      resetSelectedCueSettings();
      return;
    }
    if (action == QuickAction::ConvertCueMedia) {
      convertSelectedCueMedia();
      return;
    }
    pushUndoSnapshot();
    switch (action) {
      case QuickAction::ToggleLoop:      toggleSelectedLoop(); break;
      case QuickAction::ToggleHold:      toggleSelectedPauseOnLastFrame(); break;
      case QuickAction::TogglePauseBegin: toggleSelectedPauseAtBeginning(); break;
      case QuickAction::ToggleCueAudio:   toggleSelectedAudioEnabled(); break;
      case QuickAction::ToggleNextTransition: toggleSelectedTransitionToNext(); break;
      case QuickAction::ToggleFadeIn:    toggleSelectedFadeEnabled(true); break;
      case QuickAction::ToggleFadeOut:   toggleSelectedFadeEnabled(false); break;
      case QuickAction::EditGotoTarget: {
        Cue* sel = selectedCueMutable();
        if (!sel) {
          break;
        }
        openInlineTextEditor("cue.goto_target", "Cue Goto",
                             "Target cue token on end (blank = next):", sel->gotoTarget,
                             [this](const std::string& value) {
                               setSelectedGotoTarget(value);
                             });
        break;
      }
      case QuickAction::CycleEndAction:  cycleSelectedEndAction(); break;
      case QuickAction::FadeInDec:       adjustSelectedFade(true,  -0.25); break;
      case QuickAction::FadeInInc:       adjustSelectedFade(true,   0.25); break;
      case QuickAction::FadeOutDec:      adjustSelectedFade(false, -0.25); break;
      case QuickAction::FadeOutInc:      adjustSelectedFade(false,  0.25); break;
      case QuickAction::InDec:           adjustSelectedIn(-0.5); break;
      case QuickAction::InInc:           adjustSelectedIn( 0.5); break;
      case QuickAction::OutDec:          adjustSelectedOut(-0.5); break;
      case QuickAction::OutInc:          adjustSelectedOut( 0.5); break;
      case QuickAction::TransDec:        adjustSelectedCueTransition(-0.25); break;
      case QuickAction::TransInc:        adjustSelectedCueTransition( 0.25); break;
      case QuickAction::CycleTransStyle: cycleSelectedCueTransStyle(); break;
      case QuickAction::LowerBgDec:      adjustSelectedLowerAlpha(-16); break;
      case QuickAction::LowerBgInc:      adjustSelectedLowerAlpha( 16); break;
      case QuickAction::DurDec:          adjustSelectedStillDuration(-1.0); break;
      case QuickAction::DurInc:          adjustSelectedStillDuration( 1.0); break;
      case QuickAction::LoopCountDec:    adjustSelectedLoopCount(-1); break;
      case QuickAction::LoopCountInc:    adjustSelectedLoopCount( 1); break;
      case QuickAction::SpeedDec:        adjustSelectedSpeed(-0.25); break;
      case QuickAction::SpeedInc:        adjustSelectedSpeed( 0.25); break;
      case QuickAction::AudioGainDec:    adjustSelectedAudioGain(-1.0); break;
      case QuickAction::AudioGainInc:    adjustSelectedAudioGain( 1.0); break;
      case QuickAction::AudioPanDec:     adjustSelectedAudioPan(-0.05); break;
      case QuickAction::AudioPanInc:     adjustSelectedAudioPan( 0.05); break;
      case QuickAction::ToggleCueMono:   toggleSelectedCueMono(); break;
      case QuickAction::NormalizeCueAudio: normalizeSelectedCueAudio(); break;
      case QuickAction::AudioFadeInDec:  adjustSelectedAudioFade(true,  -0.25); break;
      case QuickAction::AudioFadeInInc:  adjustSelectedAudioFade(true,   0.25); break;
      case QuickAction::AudioFadeOutDec: adjustSelectedAudioFade(false, -0.25); break;
      case QuickAction::AudioFadeOutInc: adjustSelectedAudioFade(false,  0.25); break;
      case QuickAction::AudioOutPairDec: adjustSelectedAudioOutPair(-1); break;
      case QuickAction::AudioOutPairInc: adjustSelectedAudioOutPair(1); break;
      case QuickAction::CycleColorTag:   cycleSelectedColorTag(); break;
      case QuickAction::CycleScaleMode:  cycleSelectedScaleMode(); break;
      case QuickAction::EditNotes: {
        Cue* sel = selectedCueMutable();
        if (sel) {
          openInlineTextEditor("cue.notes", "Cue Notes",
                               "Enter notes for selected cue(s):", sel->notes,
                               [this](const std::string& value) {
            if (forEachFocusedSelectedCueMutable([&](Cue& cue, int) { cue.notes = value; })) {
              markProjectDirty();
            }
          });
        }
        break;
      }
      case QuickAction::EditSourceRef: {
        Cue* sel = selectedCueMutable();
        if (!sel || !isSourceCueKind(sel->kind)) {
          break;
        }
        std::string initial = sourceCueRefFromCue(*sel);
        initial = sourceCueEditorInputDefault(sel->kind, initial);
        std::string title = cueKindLabel(sel->kind) + " Source";
        std::string prompt = sourceCueEditorPrompt(sel->kind);
        openInlineTextEditor(
          "cue.source_ref",
          title,
          prompt,
          initial,
          [this](const std::string& value) {
            setSelectedSourceCueRef(value);
          });
        break;
      }
      case QuickAction::EditBrowserUrl: {
        Cue* sel = selectedCueMutable();
        if (!sel) {
          break;
        }
        if (sel->kind == CueKind::SrtStream) {
          openInlineTextEditor("cue.stream_url", "Stream URL",
                               "Stream URL (srt://, rtmp://, rtsp://...):", sel->path,
                               [this](const std::string& value) {
                                 std::string url = trim(value);
                                 if (url.empty()) { triggerToast("stream url: required"); return; }
                                 forEachFocusedSelectedCueMutable([&](Cue& c, int) {
                                   if (c.kind == CueKind::SrtStream) c.path = url;
                                 });
                                 markProjectDirty();
                               });
        } else if (sel->kind == CueKind::NdiSource) {
          std::string current = sel->path.rfind("ndi://", 0) == 0 ? sel->path.substr(6) : sel->path;
          openInlineTextEditor("cue.ndi_source", "NDI Source Name",
                               "NDI source name (e.g. LAPTOP (source 1)):", current,
                               [this](const std::string& value) {
                                 std::string name = trim(value);
                                 forEachFocusedSelectedCueMutable([&](Cue& c, int) {
                                   if (c.kind == CueKind::NdiSource) {
                                     c.path = "ndi://" + name;
                                     if (c.name == "NDI Source" || c.name.empty()) c.name = name.empty() ? "NDI Source" : name;
                                   }
                                 });
                                 markProjectDirty();
                               });
        } else if (sel->kind == CueKind::Browser) {
          openInlineTextEditor("cue.browser_url", "Browser URL",
                               "Enter URL or local file path:", sel->path,
                               [this](const std::string& value) {
                                 setSelectedBrowserCueUrl(value);
                               });
        }
        break;
      }
      case QuickAction::ToggleRefreshOnTake: {
        Cue* sel = selectedCueMutable();
        if (!sel || sel->kind != CueKind::Browser) {
          break;
        }
        sel->refreshOnTake = !sel->refreshOnTake;
        markProjectDirty();
        break;
      }
      case QuickAction::GotoMinus10:
      case QuickAction::GotoMinus20:
      case QuickAction::GotoMinus30: {
        MediaEngine* eng = focusedMediaEngine();
        if (eng) {
          double dur = eng->duration();
          double offset = (action == QuickAction::GotoMinus10) ? 10.0
                        : (action == QuickAction::GotoMinus20) ? 20.0 : 30.0;
          double target = std::max(0.0, dur - offset);
          eng->seek(target);
          triggerToast("jumped to -" + std::to_string(static_cast<int>(offset)) + "s");
        }
        break;
      }
      case QuickAction::TransportSkipStart: {
        MediaEngine* eng = focusedMediaEngine();
        if (eng) { eng->seek(0.0); triggerToast("start"); }
        break;
      }
      case QuickAction::TransportSkipBack: {
        MediaEngine* eng = focusedMediaEngine();
        if (eng) { eng->seek(std::max(0.0, eng->position() - 10.0)); triggerToast("-10s"); }
        break;
      }
      case QuickAction::TransportPlayPause:
        toggleTransport();
        break;
      case QuickAction::TransportSkipForward: {
        MediaEngine* eng = focusedMediaEngine();
        if (eng) { eng->seek(std::min(eng->duration(), eng->position() + 10.0)); triggerToast("+10s"); }
        break;
      }
      case QuickAction::TransportSkipEnd: {
        MediaEngine* eng = focusedMediaEngine();
        if (eng && eng->duration() > 0.0) { eng->seek(eng->duration() - 0.1); triggerToast("end"); }
        break;
      }
      case QuickAction::TransportSkipNext:
        skipToNextCue();
        break;
      case QuickAction::TransportSkipPrev:
        skipToPrevCue();
        break;
      case QuickAction::TrimReset: {
        if (Cue* cue = activeCueMutable()) {
          cue->inPointSeconds = 0.0;
          cue->outPointSeconds = 0.0;
          triggerToast("trim cleared");
          markProjectDirty();
        }
        break;
      }
      case QuickAction::ToggleAspectLink:
        project_.geometryAspectLinked = !project_.geometryAspectLinked;
        triggerToast(project_.geometryAspectLinked ? "aspect link on" : "aspect link off");
        playUiSound(UiSoundEffect::Toggle);
        markProjectDirty();
        break;
      case QuickAction::ScaleXDec:
        adjustSelectedScaleX(-0.05f);
        break;
      case QuickAction::ScaleXInc:
        adjustSelectedScaleX(0.05f);
        break;
      case QuickAction::ScaleYDec:
        adjustSelectedScaleY(-0.05f);
        break;
      case QuickAction::ScaleYInc:
        adjustSelectedScaleY(0.05f);
        break;
      case QuickAction::EditScaleX:
        editSelectedScaleX();
        break;
      case QuickAction::EditScaleY:
        editSelectedScaleY();
        break;
      case QuickAction::OffsetXDec:
        adjustSelectedOffsetX(-1.0f);
        break;
      case QuickAction::OffsetXInc:
        adjustSelectedOffsetX(1.0f);
        break;
      case QuickAction::OffsetYDec:
        adjustSelectedOffsetY(-1.0f);
        break;
      case QuickAction::OffsetYInc:
        adjustSelectedOffsetY(1.0f);
        break;
      case QuickAction::EditOffsetX:
        editSelectedOffsetX();
        break;
      case QuickAction::EditOffsetY:
        editSelectedOffsetY();
        break;
      case QuickAction::RotDec:
        adjustSelectedRotation(-1.0f);
        break;
      case QuickAction::RotInc:
        adjustSelectedRotation(1.0f);
        break;
      case QuickAction::EditRotation:
        editSelectedRotation();
        break;
      case QuickAction::CropLDec:
        adjustSelectedCrop('L', -0.01f);
        break;
      case QuickAction::CropLInc:
        adjustSelectedCrop('L', 0.01f);
        break;
      case QuickAction::CropRDec:
        adjustSelectedCrop('R', -0.01f);
        break;
      case QuickAction::CropRInc:
        adjustSelectedCrop('R', 0.01f);
        break;
      case QuickAction::CropTDec:
        adjustSelectedCrop('T', -0.01f);
        break;
      case QuickAction::CropTInc:
        adjustSelectedCrop('T', 0.01f);
        break;
      case QuickAction::CropBDec:
        adjustSelectedCrop('B', -0.01f);
        break;
      case QuickAction::CropBInc:
        adjustSelectedCrop('B', 0.01f);
        break;
      case QuickAction::KeyToggle:
        toggleSelectedChromaKey();
        break;
      case QuickAction::DatamoshToggle:
        toggleSelectedDatamosh();
        break;
      case QuickAction::CueSectionEffectsToggle:
        cueSectionEffectsOpen_ = !cueSectionEffectsOpen_;
        break;
      case QuickAction::KeyTolDec:
        adjustSelectedKeyTolerance(-5.0f);
        break;
      case QuickAction::KeyTolInc:
        adjustSelectedKeyTolerance(5.0f);
        break;
      case QuickAction::KeySoftDec:
        adjustSelectedKeySoftness(-2.0f);
        break;
      case QuickAction::KeySoftInc:
        adjustSelectedKeySoftness(2.0f);
        break;
      case QuickAction::EditKeyColor:
        editSelectedKeyColor();
        break;
      case QuickAction::PickKeyColor:
        armSelectedKeyColorPicker();
        break;
      case QuickAction::BrightnessDec:
        adjustSelectedBrightness(-0.05f);
        break;
      case QuickAction::BrightnessInc:
        adjustSelectedBrightness(0.05f);
        break;
      case QuickAction::ContrastDec:
        adjustSelectedContrast(-0.05f);
        break;
      case QuickAction::ContrastInc:
        adjustSelectedContrast(0.05f);
        break;
      case QuickAction::SaturationDec:
        adjustSelectedSaturation(-0.05f);
        break;
      case QuickAction::SaturationInc:
        adjustSelectedSaturation(0.05f);
        break;
      case QuickAction::HueShiftDec:
        adjustSelectedHueShift(-5.0f);
        break;
      case QuickAction::HueShiftInc:
        adjustSelectedHueShift(5.0f);
        break;
      case QuickAction::PatternTypePrev:
        cycleSelectedPatternType(-1);
        break;
      case QuickAction::PatternTypeNext:
        cycleSelectedPatternType(1);
        break;
      case QuickAction::TogglePatternMotion:
        toggleSelectedPatternMotion();
        break;
      case QuickAction::CueSectionPlaybackToggle:
        cueSectionPlaybackOpen_ = !cueSectionPlaybackOpen_;
        break;
      case QuickAction::CueSectionMetadataToggle:
        cueSectionMetadataOpen_ = !cueSectionMetadataOpen_;
        break;
      case QuickAction::CueSectionGeometryToggle:
        cueSectionGeometryOpen_ = !cueSectionGeometryOpen_;
        break;
      case QuickAction::CueSectionKeyToggle:
        cueSectionKeyOpen_ = !cueSectionKeyOpen_;
        break;
      case QuickAction::CueSectionAudioToggle:
        cueSectionAudioOpen_ = !cueSectionAudioOpen_;
        break;
      case QuickAction::CueSectionRoutingToggle:
        cueSectionRoutingOpen_ = !cueSectionRoutingOpen_;
        break;
      case QuickAction::ClearOverlay:
        clearOverlay();
        break;
      case QuickAction::EditLowerThirdText: {
        Cue* sel = selectedCueMutable();
        if (sel && sel->kind == CueKind::LowerThird) {
          std::string initial = sel->lowerThirdText.empty() ? sel->name : sel->lowerThirdText;
          openInlineTextEditor("cue.lowertext", "Lower Third Title",
                               "Text shown on the main lower-third line:", initial,
                               [this](const std::string& value) {
            if (forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
                  if (cue.kind == CueKind::LowerThird) {
                    cue.lowerThirdText = trim(value);
                  }
                })) {
              markProjectDirty();
            }
          });
        }
        break;
      }
      case QuickAction::EditLowerThirdSubtext: {
        Cue* sel = selectedCueMutable();
        if (sel && sel->kind == CueKind::LowerThird) {
          openInlineTextEditor("cue.lowersub", "Lower Third Subtext",
                               "Optional second line under the title:", sel->lowerThirdSubtext,
                               [this](const std::string& value) {
            if (forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
                  if (cue.kind == CueKind::LowerThird) {
                    cue.lowerThirdSubtext = trim(value);
                  }
                })) {
              markProjectDirty();
            }
          });
        }
        break;
      }
      case QuickAction::EditPipTarget: {
        Cue* sel = selectedCueMutable();
        if (sel && sel->kind == CueKind::Pip) {
          openInlineTextEditor("cue.piptarget", "PIP Target Cue",
                               "Cue token/id/number/name to show in the PIP window:",
                               sel->pipTargetCue,
                               [this](const std::string& value) {
            setSelectedPipCueTarget(value);
          });
        }
        break;
      }
      case QuickAction::EditPipSourcePath: {
        Cue* sel = selectedCueMutable();
        if (sel && sel->kind == CueKind::Pip) {
          std::string sourceType = pipSourceTypeTokenFromCue(*sel);
          std::string title = "PIP Source";
          std::string prompt = "Set the source for this PIP overlay:";
          std::string initialValue;
          if (sourceType == "browser") {
            title = "PIP Browser URL";
            prompt = "URL or local page path to show in the PIP window:";
            initialValue = sel->path;
          } else if (pipSourceTypeUsesSourceRef(sourceType)) {
            Cue tempCue;
            tempCue.kind = sourceCueKindFromToken(sourceType);
            tempCue.path = sel->path;
            title = "PIP Source Input";
            prompt = "Source name/id for this live PIP input:";
            initialValue = sourceCueRefFromCue(tempCue);
          } else {
            title = "PIP Media File";
            prompt = "Path to a video clip or still image for this PIP:";
            initialValue = sel->path == "graphic://pip"
              ? std::string()
              : resolvedCueFilesystemPathString(*sel, currentProjectFile_);
          }
          openInlineTextEditor("cue.pipsource", title, prompt, initialValue,
                               [this](const std::string& value) {
            setSelectedPipSourceValue(value);
          });
        }
        break;
      }
      case QuickAction::EditAttachedLowerThirdCue:
      case QuickAction::EditAttachedPipCue: {
        Cue* sel = selectedCueMutable();
        if (!sel || cueIsOverlayOnly(*sel)) {
          break;
        }
        bool lowerThird = action == QuickAction::EditAttachedLowerThirdCue;
        std::string initialValue = lowerThird ? sel->attachedLowerThirdCue : sel->attachedPipCue;
        openInlineTextEditor(lowerThird ? "cue.attach_lower" : "cue.attach_pip",
                             lowerThird ? "Attach Lower Third" : "Attach PIP Overlay",
                             lowerThird
                               ? "Overlay cue token/id/number/name to fire with TAKE:"
                               : "PIP overlay cue token/id/number/name to fire with TAKE:",
                             initialValue,
                             [this, lowerThird](const std::string& value) {
            setSelectedAttachedOverlayCue(lowerThird ? CueKind::LowerThird : CueKind::Pip, value);
          });
        break;
      }
      case QuickAction::EditCompositeSlot1Source:
      case QuickAction::EditCompositeSlot2Source:
      case QuickAction::EditCompositeSlot3Source:
      case QuickAction::EditCompositeSlot4Source: {
        Cue* sel = selectedCueMutable();
        if (!sel || sel->kind != CueKind::Composite) {
          break;
        }
        int slotIndex =
          action == QuickAction::EditCompositeSlot1Source ? 0 :
          action == QuickAction::EditCompositeSlot2Source ? 1 :
          action == QuickAction::EditCompositeSlot3Source ? 2 : 3;
        if (slotIndex < 0 || slotIndex >= static_cast<int>(sel->compositeSlots.size())) {
          break;
        }
        const CompositeSlot& slot = sel->compositeSlots[slotIndex];
        std::string initialValue;
        if (!trim(slot.source).empty()) {
          initialValue = trim(slot.sourceType).empty() ? slot.source : (slot.sourceType + ":" + slot.source);
        }
        openInlineTextEditor(
          "cue.composite.slot" + std::to_string(slotIndex + 1),
          slot.name.empty() ? compositeSlotDefaultName(slotIndex) : slot.name,
          "Enter media path or browser:/window:/camera:/syphon: source",
          initialValue,
          [this, slotIndex](const std::string& value) {
            setSelectedCompositeSlotSource(slotIndex, value);
          });
        break;
      }
      case QuickAction::CompositePreset2Up:
        applySelectedCompositePreset("2up", "2-up");
        break;
      case QuickAction::CompositePreset7030:
        applySelectedCompositePreset("7030", "70/30");
        break;
      case QuickAction::CompositePresetQuad:
        applySelectedCompositePreset("quad", "quad");
        break;
      case QuickAction::CycleCompositeAudioSlot:
        cycleSelectedCompositeAudioSlot();
        break;
      case QuickAction::PipPresetCornerTL:
        applySelectedPipCornerPreset(-1, -1, "top left");
        break;
      case QuickAction::PipPresetCornerTR:
        applySelectedPipCornerPreset(1, -1, "top right");
        break;
      case QuickAction::PipPresetCornerBL:
        applySelectedPipCornerPreset(-1, 1, "bottom left");
        break;
      case QuickAction::PipPresetCornerBR:
        applySelectedPipCornerPreset(1, 1, "bottom right");
        break;
      case QuickAction::PipPresetSmall:
        applySelectedPipSizePreset(0.26f, "small");
        break;
      case QuickAction::PipPresetBig:
        applySelectedPipSizePreset(0.42f, "big");
        break;
      case QuickAction::PipPreset7030:
        applySelectedPipSizePreset(0.30f, "70/30");
        break;
      case QuickAction::EditCueNumber: {
        Cue* sel = selectedCueMutable();
        if (sel) {
          std::string initial = sel->cueId.empty() ? cueDisplayToken(*sel, focusedDeck().selectedIndex) : sel->cueId;
          openInlineTextEditor("cue.id", "Cue ID",
                               "Short cue id (max 6 chars, letters/numbers):", initial,
                               [this](const std::string& value) {
            std::string normalized = normalizeCueIdShort(value);
            if (forEachFocusedSelectedCueMutable([&](Cue& cue, int) { cue.cueId = normalized; })) {
              markProjectDirty();
            }
          });
        }
        break;
      }
      case QuickAction::CopyCueSettings:
      case QuickAction::PasteCueSettings:
      case QuickAction::ResetCueSettings:
      case QuickAction::ConvertCueMedia:
        break;
      case QuickAction::AddPausePoint: {
        Cue* sel = selectedCueMutable();
        if (sel) {
          MediaEngine* eng = focusedMediaEngine();
          double pos = eng ? eng->position() : 0.0;
          sel->pausePoints.push_back(pos);
          std::sort(sel->pausePoints.begin(), sel->pausePoints.end());
          // Update engine's live pause points
          if (eng) eng->setPausePoints(sel->pausePoints);
          triggerToast("pause point added at " + formatSeconds(pos));
          markProjectDirty();
        }
        break;
      }
      case QuickAction::ClearPausePoints: {
        Cue* sel = selectedCueMutable();
        if (sel && !sel->pausePoints.empty()) {
          sel->pausePoints.clear();
          if (MediaEngine* eng = focusedMediaEngine()) eng->setPausePoints({});
          triggerToast("pause points cleared");
          markProjectDirty();
        }
        break;
      }
      case QuickAction::VolDec:
        if (MediaEngine* eng = focusedMediaEngine()) {
          eng->setVolume(eng->volume() - 0.05f);
          triggerToast("volume " + std::to_string(static_cast<int>(std::round(eng->volume() * 100.0f))) + "%");
        }
        break;
      case QuickAction::VolInc:
        if (MediaEngine* eng = focusedMediaEngine()) {
          eng->setVolume(eng->volume() + 0.05f);
          triggerToast("volume " + std::to_string(static_cast<int>(std::round(eng->volume() * 100.0f))) + "%");
        }
        break;
    }
  }
