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
  // `param` carries a payload for the actions that need one (the effect stack's
  // index, the numeric-parameter id). -1 for everything else, which is most.
  void dispatchQuickAction(QuickAction action, int param = -1) {
    switch (action) {
      case QuickAction::EditNumericParam: editNumericParam(param); return;
      case QuickAction::EffectAdd:        effectStackAdd(); return;
      case QuickAction::MotionDriverPick:  pickMotionDriver(); return;
      case QuickAction::MotionDriverClear: clearMotionDriver(); return;
      case QuickAction::MotionDriverSpeedDec: nudgeMotionDriverSpeed(-0.1f); return;
      case QuickAction::MotionDriverSpeedInc: nudgeMotionDriverSpeed(+0.1f); return;
      case QuickAction::MotionDriverPauseToggle: toggleMotionDriverPaused(); return;
      case QuickAction::MotionDriverRestart: restartSelectedMotionDriver(); return;
      case QuickAction::MotionDriverRestartOnTakeToggle:
        toggleMotionDriverRestartOnTake(); return;
      case QuickAction::EffectRemove:     effectStackRemove(param); return;
      case QuickAction::EffectCycleKind:  effectStackCycleKind(param); return;
      case QuickAction::EffectToggleBypass: effectStackToggleBypass(param); return;
      case QuickAction::EffectAmountDec:  effectStackNudge(param, -0.05f); return;
      case QuickAction::EffectAmountInc:  effectStackNudge(param, +0.05f); return;
      case QuickAction::EffectEditAmount: effectStackEditAmount(param); return;
      case QuickAction::EffectMoveUp:     effectStackMove(param, -1); return;
      case QuickAction::EffectMoveDown:   effectStackMove(param, +1); return;
      case QuickAction::EffectParamADec:  effectStackNudgeParam(param, 0, -0.05f); return;
      case QuickAction::EffectParamAInc:  effectStackNudgeParam(param, 0, +0.05f); return;
      case QuickAction::EffectParamAEdit: effectStackEditParam(param, 0); return;
      case QuickAction::EffectParamBDec:  effectStackNudgeParam(param, 1, -0.05f); return;
      case QuickAction::EffectParamBInc:  effectStackNudgeParam(param, 1, +0.05f); return;
      case QuickAction::EffectParamBEdit: effectStackEditParam(param, 1); return;
      case QuickAction::EffectParamCDec:  effectStackNudgeParam(param, 2, -0.05f); return;
      case QuickAction::EffectParamCInc:  effectStackNudgeParam(param, 2, +0.05f); return;
      case QuickAction::EffectParamCEdit: effectStackEditParam(param, 2); return;
      case QuickAction::EffectParamDDec:  effectStackNudgeParam(param, 3, -0.05f); return;
      case QuickAction::EffectParamDInc:  effectStackNudgeParam(param, 3, +0.05f); return;
      case QuickAction::EffectParamDEdit: effectStackEditParam(param, 3); return;
      case QuickAction::CueSectionCodeToggle:
        cueSectionCodeOpen_ = !cueSectionCodeOpen_; return;
      case QuickAction::CodeOpenEditor:     openCodeEditor(); return;
      case QuickAction::EffectLfoToggle:    effectLfoToggle(param); return;
      case QuickAction::EffectLfoShape:     effectLfoCycleShape(param); return;
      case QuickAction::EffectLfoRateDec:   effectLfoNudgeRate(param, -1); return;
      case QuickAction::EffectLfoRateInc:   effectLfoNudgeRate(param, +1); return;
      case QuickAction::EffectLfoDepthDec:  effectLfoNudgeDepth(param, -0.05f); return;
      case QuickAction::EffectLfoDepthInc:  effectLfoNudgeDepth(param, +0.05f); return;
      case QuickAction::EffectLfoSync:      effectLfoToggleSync(param); return;
      case QuickAction::VjCycleBlend:
        setVjBlend(project_.vjBlendMode == "dissolve" ? "add"
                   : project_.vjBlendMode == "add" ? "multiply" : "dissolve");
        return;
      case QuickAction::VjTapTempo:      tapVjTempo(); return;
      case QuickAction::VjToggleQuantise:
        project_.vjQuantiseTakes = !project_.vjQuantiseTakes;
        triggerToast(project_.vjQuantiseTakes ? "takes land on the beat"
                                              : "takes are immediate");
        markProjectDirty();
        return;
      case QuickAction::EffectChainCopy:  copySelectedEffectChain(); return;
      case QuickAction::EffectChainPaste: pasteSelectedEffectChain(); return;
      default: break;
    }
    dispatchQuickActionPlain(action);
  }

  void dispatchQuickActionPlain(QuickAction action) {
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
      case QuickAction::TimerChimeAmberToggle:
        toggleTimerFlag(&TimerSettings::chimeAtAmber, "chime amber"); break;
      case QuickAction::TimerChimeRedToggle:
        toggleTimerFlag(&TimerSettings::chimeAtRed, "chime red"); break;
      case QuickAction::TimerChimeZeroToggle:
        toggleTimerFlag(&TimerSettings::chimeAtZero, "chime zero"); break;
      case QuickAction::TimerCycleChimeSound:   cycleTimerChimeSound(); break;
      case QuickAction::TimerPickLogo:          pickTimerLogo(); break;
      case QuickAction::CueSectionToneToggle:
        cueSectionToneOpen_ = !cueSectionToneOpen_; break;
      case QuickAction::ToneCycleWaveform: cycleToneWaveform(); break;
      case QuickAction::ToneFreqDec:    adjustToneFrequency(-1); break;
      case QuickAction::ToneFreqInc:    adjustToneFrequency(1); break;
      case QuickAction::ToneLevelDec:   adjustToneLevel(-1.0); break;
      case QuickAction::ToneLevelInc:   adjustToneLevel(1.0); break;
      case QuickAction::ToneChannelDec: adjustToneChannel(-1); break;
      case QuickAction::ToneChannelInc: adjustToneChannel(1); break;
      case QuickAction::ToneCycleVisual: cycleToneVisual(); break;
      case QuickAction::FdsCycleCarrier:   cycleFdsCarrier(); break;
      case QuickAction::FdsCycleModulator: cycleFdsModulator(); break;
      case QuickAction::FdsDepthDec:  adjustFdsDepth(-4); break;
      case QuickAction::FdsDepthInc:  adjustFdsDepth(4); break;
      case QuickAction::FdsRatioDec:  adjustFdsRatio(-1); break;
      case QuickAction::FdsRatioInc:  adjustFdsRatio(1); break;
      case QuickAction::FdsNoteDec:   adjustFdsNote(-1); break;
      case QuickAction::FdsNoteInc:   adjustFdsNote(1); break;
      case QuickAction::FdsRetrigDec: adjustFdsRetrigger(-0.05); break;
      case QuickAction::FdsRetrigInc: adjustFdsRetrigger(0.05); break;
      case QuickAction::CueSectionVideoSynthToggle:
        cueSectionVideoSynthOpen_ = !cueSectionVideoSynthOpen_; break;
      case QuickAction::VsCycleShape:   cycleVsEnum(0); break;
      case QuickAction::VsCycleMirror:  cycleVsEnum(1); break;
      case QuickAction::VsCyclePalette: cycleVsEnum(2); break;
      case QuickAction::VsSpeedDec:    scaleVsSpeed(1.0 / 1.3); break;
      case QuickAction::VsSpeedInc:    scaleVsSpeed(1.3); break;
      case QuickAction::VsScaleDec:    adjustVs(&VideoSynthSettings::scale, -0.1, 0.1, 8.0, "scale"); break;
      case QuickAction::VsScaleInc:    adjustVs(&VideoSynthSettings::scale, 0.1, 0.1, 8.0, "scale"); break;
      case QuickAction::VsFeedbackDec: adjustVs(&VideoSynthSettings::feedbackAmount, -0.05, 0.0, 0.95, "feedback"); break;
      case QuickAction::VsFeedbackInc: adjustVs(&VideoSynthSettings::feedbackAmount, 0.05, 0.0, 0.95, "feedback"); break;
      case QuickAction::VsZoomDec:     adjustVs(&VideoSynthSettings::feedbackZoom, -0.01, 0.90, 1.15, "zoom"); break;
      case QuickAction::VsZoomInc:     adjustVs(&VideoSynthSettings::feedbackZoom, 0.01, 0.90, 1.15, "zoom"); break;
      case QuickAction::VsReactDec:    adjustVs(&VideoSynthSettings::audioReactivity, -0.1, 0.0, 1.0, "audio"); break;
      case QuickAction::VsReactInc:    adjustVs(&VideoSynthSettings::audioReactivity, 0.1, 0.0, 1.0, "audio"); break;
      case QuickAction::VsSortDec:    adjustVs(&VideoSynthSettings::pixelSort, -0.1, 0.0, 1.0, "smear"); break;
      case QuickAction::VsSortInc:    adjustVs(&VideoSynthSettings::pixelSort, 0.1, 0.0, 1.0, "smear"); break;
      case QuickAction::VsGlitchDec:  adjustVs(&VideoSynthSettings::glitch, -0.1, 0.0, 1.0, "glitch"); break;
      case QuickAction::VsGlitchInc:  adjustVs(&VideoSynthSettings::glitch, 0.1, 0.0, 1.0, "glitch"); break;
      case QuickAction::VsResDec:     adjustVsInt(&VideoSynthSettings::resolution, -1, 1, 5, "detail"); break;
      case QuickAction::VsResInc:     adjustVsInt(&VideoSynthSettings::resolution, 1, 1, 5, "detail"); break;
      case QuickAction::VsAsciiColsDec: adjustVsInt(&VideoSynthSettings::asciiCols, -10, 20, 200, "columns"); break;
      case QuickAction::VsAsciiColsInc: adjustVsInt(&VideoSynthSettings::asciiCols, 10, 20, 200, "columns"); break;
      case QuickAction::VsCrtDec: adjustVs(&VideoSynthSettings::crt, -0.1, 0.0, 1.0, "crt"); break;
      case QuickAction::VsCrtInc: adjustVs(&VideoSynthSettings::crt, 0.1, 0.0, 1.0, "crt"); break;
      case QuickAction::VsCharSetCycle:
        if (Cue* c = selectedVideoSynthCueMutable()) {
          // An explicit order rather than modulo arithmetic: sheet mode is
          // only offered once a sheet exists, so the cycle never lands on a
          // mode that cannot draw anything, and the sets that are always
          // available no longer have to be contiguous with it.
          static const int kWithoutSheet[] = {0, 1, 2, 3, 4, 6};
          static const int kWithSheet[]    = {0, 1, 2, 3, 4, 6, 5};
          const bool sheet = !c->videoSynth.spriteSheetPath.empty();
          const int* order = sheet ? kWithSheet : kWithoutSheet;
          const int count = sheet ? 7 : 6;
          // On a cue carrying the EFFECT the glyph set lives in paramC and the
          // cue's own field is overwritten before it reaches the renderer, so
          // this cycled a number nothing read. A sprite sheet is not reachable
          // from an effect either -- it has no sheet path -- so that arm cycles
          // the six sets that can actually draw.
          deckboy::effects::CueEffect* fx = textModeEffectMutable(*c);
          int current = c->videoSynth.asciiCharSet;
          if (fx) {
            VideoSynthSettings shown = c->videoSynth;
            applyTextModeParams(*fx, shown);
            current = shown.asciiCharSet;
            order = kWithoutSheet;
          }
          const int steps = fx ? 6 : count;
          int at = 0;
          for (int i = 0; i < steps; ++i) {
            if (order[i] == current) { at = i; break; }
          }
          const int next = order[(at + 1) % steps];
          if (fx) {
            fx->paramC = textModeParamForCharSet(next);
          } else {
            c->videoSynth.asciiCharSet = next;
          }
          markProjectDirty();
          triggerToast(std::string("characters: ") + vsCharSetLabel(next));
          playUiSound(UiSoundEffect::Toggle);
        }
        break;
      case QuickAction::VsShuffleCycle:
        if (Cue* c = selectedVideoSynthCueMutable()) {
          // Steps through seeds rather than randomising live: the same seed
          // must give the same look every time the show is opened.
          c->videoSynth.asciiShuffle = (c->videoSynth.asciiShuffle + 1) % 9;
          markProjectDirty();
          triggerToast(c->videoSynth.asciiShuffle == 0
            ? "glyphs: by density"
            : "glyphs: shuffle " + std::to_string(c->videoSynth.asciiShuffle));
          playUiSound(UiSoundEffect::Toggle);
        }
        break;
      case QuickAction::VsZalgoUpDec:
        adjustVs(&VideoSynthSettings::asciiZalgoUp, -0.05, 0.0, 1.0, "glitch up");
        break;
      case QuickAction::VsZalgoUpInc:
        adjustVs(&VideoSynthSettings::asciiZalgoUp, 0.05, 0.0, 1.0, "glitch up");
        break;
      case QuickAction::VsZalgoDownDec:
        adjustVs(&VideoSynthSettings::asciiZalgoDown, -0.05, 0.0, 1.0, "glitch down");
        break;
      case QuickAction::VsZalgoDownInc:
        adjustVs(&VideoSynthSettings::asciiZalgoDown, 0.05, 0.0, 1.0, "glitch down");
        break;
      case QuickAction::VsZalgoMidDec:
        adjustVs(&VideoSynthSettings::asciiZalgoMid, -0.02, 0.0, 1.0, "glitch through");
        break;
      case QuickAction::VsZalgoMidInc:
        adjustVs(&VideoSynthSettings::asciiZalgoMid, 0.02, 0.0, 1.0, "glitch through");
        break;
      case QuickAction::VsZalgoDriftDec:
        adjustVs(&VideoSynthSettings::asciiZalgoDrift, -0.05, 0.0, 1.0, "glitch drift");
        break;
      case QuickAction::VsZalgoDriftInc:
        adjustVs(&VideoSynthSettings::asciiZalgoDrift, 0.05, 0.0, 1.0, "glitch drift");
        break;
      case QuickAction::VsZalgoReachDec:
      case QuickAction::VsZalgoReachInc:
        if (Cue* c = selectedVideoSynthCueMutable()) {
          const int step = action == QuickAction::VsZalgoReachInc ? 1 : -1;
          c->videoSynth.asciiZalgoReach =
            std::clamp(c->videoSynth.asciiZalgoReach + step, 1, 6);
          markProjectDirty();
          triggerToast("glitch reach: " +
                       std::to_string(c->videoSynth.asciiZalgoReach));
        }
        break;
      case QuickAction::VsAsciiGlyphsEdit: editAsciiGlyphs(); break;
      case QuickAction::VsAsciiPhrasesEdit: editAsciiPhrases(); break;
      case QuickAction::VsAsciiHoldDec:
        adjustVs(&VideoSynthSettings::asciiPhraseHold, -0.5, 0.0, 60.0, "phrase hold");
        break;
      case QuickAction::VsAsciiHoldInc:
        adjustVs(&VideoSynthSettings::asciiPhraseHold, 0.5, 0.0, 60.0, "phrase hold");
        break;
      case QuickAction::VsSpriteSetPrev: cycleSpriteSet(-1); break;
      case QuickAction::VsSpriteSetNext: cycleSpriteSet(1); break;
      case QuickAction::VsRotateCycle:
        if (Cue* c = selectedVideoSynthCueMutable()) {
          c->videoSynth.spriteRotate = (c->videoSynth.spriteRotate + 1) % 6;
          markProjectDirty();
          triggerToast(std::string("rotate: ") + vsRotateLabel(c->videoSynth.spriteRotate));
          playUiSound(UiSoundEffect::Toggle);
        }
        break;
      case QuickAction::VsFlipCycle:
        if (Cue* c = selectedVideoSynthCueMutable()) {
          c->videoSynth.spriteFlip = (c->videoSynth.spriteFlip + 1) % 4;
          markProjectDirty();
          triggerToast(std::string("flip: ") + vsFlipLabel(c->videoSynth.spriteFlip));
          playUiSound(UiSoundEffect::Toggle);
        }
        break;
      case QuickAction::VsJitterDec: adjustVs(&VideoSynthSettings::spriteJitter, -0.1, 0.0, 1.0, "jitter"); break;
      case QuickAction::VsJitterInc: adjustVs(&VideoSynthSettings::spriteJitter, 0.1, 0.0, 1.0, "jitter"); break;
      case QuickAction::VsChaosDec:  adjustVs(&VideoSynthSettings::spriteChaos, -0.1, 0.0, 1.0, "chaos"); break;
      case QuickAction::VsChaosInc:  adjustVs(&VideoSynthSettings::spriteChaos, 0.1, 0.0, 1.0, "chaos"); break;
      case QuickAction::VsFreeAngleDec: adjustVs(&VideoSynthSettings::spriteFreeAngle, -15.0, -720.0, 720.0, "spin"); break;
      case QuickAction::VsFreeAngleInc: adjustVs(&VideoSynthSettings::spriteFreeAngle, 15.0, -720.0, 720.0, "spin"); break;
      case QuickAction::VsSheetPick:  pickSpriteSheet(); break;
      case QuickAction::VsSheetClear:
        if (Cue* c = selectedVideoSynthCueMutable()) {
          c->videoSynth.spriteSheetPath.clear();
          // Fall back to blocks rather than leaving the cue pointed at sheet
          // mode with no sheet, which would silently draw the fallback and
          // look like the clear did nothing.
          if (c->videoSynth.asciiCharSet == 5) c->videoSynth.asciiCharSet = 0;
          markProjectDirty();
          triggerToast("sprite sheet cleared");
        }
        break;
      case QuickAction::VsTileWDec:
        adjustVsInt(&VideoSynthSettings::spriteTileW, -8, 8, 128, "tile w");
        // The cached slice is keyed on tile size, so a change invalidates it.
        if (Cue* c = selectedVideoSynthCueMutable()) loadSpriteSetForCue(*c);
        break;
      case QuickAction::VsTileWInc: adjustVsInt(&VideoSynthSettings::spriteTileW, 8, 8, 128, "tile w"); break;
      case QuickAction::VsTileHDec: adjustVsInt(&VideoSynthSettings::spriteTileH, -8, 8, 128, "tile h"); break;
      case QuickAction::VsTileHInc: adjustVsInt(&VideoSynthSettings::spriteTileH, 8, 8, 128, "tile h"); break;
      case QuickAction::VsInkCycle:
        if (Cue* c = selectedVideoSynthCueMutable()) {
          // Ink lives in paramD on a cue carrying the effect. This wrote the
          // cue's field, which the renderer then overwrote -- so the row read
          // "green" while the picture came out in full colour, because paramD
          // was still sitting at its default.
          int nextInk = (c->videoSynth.asciiInk + 1) % 6;
          if (deckboy::effects::CueEffect* fx = textModeEffectMutable(*c)) {
            VideoSynthSettings shown = c->videoSynth;
            applyTextModeParams(*fx, shown);
            nextInk = (shown.asciiInk + 1) % 6;
            fx->paramD = textModeParamForInk(nextInk);
          } else {
            c->videoSynth.asciiInk = nextInk;
          }
          markProjectDirty();
          triggerToast(std::string("ink: ") + vsInkLabel(nextInk));
          playUiSound(UiSoundEffect::Toggle);
        }
        break;
      case QuickAction::VsAsciiToggle:
        if (Cue* c = selectedVideoSynthCueMutable()) {
          c->videoSynth.ascii = !c->videoSynth.ascii;
          markProjectDirty();
          triggerToast(c->videoSynth.ascii ? "text mode" : "pixels");
          playUiSound(UiSoundEffect::Toggle);
        }
        break;
      case QuickAction::CueSectionSynthToggle:
        cueSectionSynthOpen_ = !cueSectionSynthOpen_; break;
      case QuickAction::SynthCycleChip:      cycleSynthChip(); break;
      case QuickAction::SynthKeyboardToggle:
        project_.synthKeyboardEnabled = !project_.synthKeyboardEnabled;
        markProjectDirty();
        if (!project_.synthKeyboardEnabled) {
          // Release anything held, or a note sustains forever after the mode
          // is switched off mid-press.
          if (MediaEngine* e = liveSynthEngine()) e->synthAllNotesOff();
        }
        triggerToast(project_.synthKeyboardEnabled
          ? "computer keyboard: PLAYING (letter keys are notes)"
          : "computer keyboard off");
        playUiSound(UiSoundEffect::Toggle);
        break;
      case QuickAction::SynthMidiToggle:
        project_.midiToSynth = !project_.midiToSynth;
        markProjectDirty();
        triggerToast(project_.midiToSynth ? "MIDI plays the synth"
                                          : "MIDI fires cues");
        playUiSound(UiSoundEffect::Toggle);
        break;
      case QuickAction::SynthCycleTuning:
        if (Cue* c = selectedToneCueMutable()) {
          c->tone.synth.tuning = static_cast<SynthTuning>(
            (static_cast<int>(c->tone.synth.tuning) + 1) % 7);
          markProjectDirty();
          triggerToast(std::string("tuning: ") + synthTuningLabel(c->tone.synth.tuning));
          playUiSound(UiSoundEffect::Toggle);
        }
        break;
      case QuickAction::SynthRefDec:
        if (Cue* c = selectedToneCueMutable()) {
          c->tone.synth.referenceHz = std::clamp(c->tone.synth.referenceHz - 1.0, 380.0, 480.0);
          markProjectDirty();
          triggerToast("A = " + fmtFloat(c->tone.synth.referenceHz, 0) + " Hz");
        }
        break;
      case QuickAction::SynthRefInc:
        if (Cue* c = selectedToneCueMutable()) {
          c->tone.synth.referenceHz = std::clamp(c->tone.synth.referenceHz + 1.0, 380.0, 480.0);
          markProjectDirty();
          triggerToast("A = " + fmtFloat(c->tone.synth.referenceHz, 0) + " Hz");
        }
        break;
      case QuickAction::SynthCycleNesVoice:  cycleNesVoice(); break;
      case QuickAction::SynthCycleNesDuty:   cycleNesDuty(); break;
      case QuickAction::SynthToggleNoiseShort:
        if (Cue* c = selectedToneCueMutable()) {
          c->tone.synth.nesNoiseShort = !c->tone.synth.nesNoiseShort;
          markProjectDirty();
          triggerToast(c->tone.synth.nesNoiseShort ? "periodic noise" : "noise");
          playUiSound(UiSoundEffect::Toggle);
        }
        break;
      case QuickAction::SynthToggleQuantise:
        if (Cue* c = selectedToneCueMutable()) {
          c->tone.synth.nesQuantise = !c->tone.synth.nesQuantise;
          markProjectDirty();
          triggerToast(c->tone.synth.nesQuantise ? "4-bit steps" : "smooth");
          playUiSound(UiSoundEffect::Toggle);
        }
        break;
      case QuickAction::SynthAttackDec:  adjustSynthEnv(true, -0.01); break;
      case QuickAction::SynthAttackInc:  adjustSynthEnv(true, 0.01); break;
      case QuickAction::SynthReleaseDec: adjustSynthEnv(false, -0.05); break;
      case QuickAction::SynthReleaseInc: adjustSynthEnv(false, 0.05); break;
      case QuickAction::ToneVisualToggle:
        if (Cue* c = selectedToneCueMutable()) {
          c->tone.visualEnabled = !c->tone.visualEnabled;
          markProjectDirty();
          triggerToast(c->tone.visualEnabled ? "display on" : "display off");
          playUiSound(UiSoundEffect::Toggle);
        }
        break;
      case QuickAction::TimerClearLogo:         clearTimerLogo(); break;
      case QuickAction::TimerCycleColorNormal:
        cycleTimerColor(&TimerSettings::colorNormal, "colour"); break;
      case QuickAction::TimerCycleColorAmber:
        cycleTimerColor(&TimerSettings::colorAmber, "amber colour"); break;
      case QuickAction::TimerCycleColorRed:
        cycleTimerColor(&TimerSettings::colorRed, "red colour"); break;
      case QuickAction::TimerCycleColorBackground:
        cycleTimerColor(&TimerSettings::colorBackground, "backdrop"); break;
      case QuickAction::DatamoshLookPrev: cycleSelectedDatamoshLook(-1); break;
      case QuickAction::DatamoshLookNext: cycleSelectedDatamoshLook(+1); break;
      case QuickAction::CueSectionEffectsToggle:
        cueSectionEffectsOpen_ = !cueSectionEffectsOpen_;
        break;
      case QuickAction::CueSectionTimerToggle:
        cueSectionTimerOpen_ = !cueSectionTimerOpen_;
        break;
      case QuickAction::TimerRunToggle:   timerToggleRun(); break;
      case QuickAction::TimerResetAction: timerReset(); break;
      case QuickAction::TimerNudgeUp:     timerNudge(60.0); break;
      case QuickAction::TimerNudgeDown:   timerNudge(-60.0); break;
      case QuickAction::TimerNudgeSecUp:   timerNudge(10.0); break;
      case QuickAction::TimerNudgeSecDown: timerNudge(-10.0); break;
      case QuickAction::TimerDurDec:      adjustTimerField(&TimerSettings::durationSeconds, -30); break;
      case QuickAction::TimerDurInc:      adjustTimerField(&TimerSettings::durationSeconds, 30); break;
      case QuickAction::TimerAmberDec:    adjustTimerField(&TimerSettings::amberSeconds, -15); break;
      case QuickAction::TimerAmberInc:    adjustTimerField(&TimerSettings::amberSeconds, 15); break;
      case QuickAction::TimerRedDec:      adjustTimerField(&TimerSettings::redSeconds, -5); break;
      case QuickAction::TimerRedInc:      adjustTimerField(&TimerSettings::redSeconds, 5); break;
      case QuickAction::TimerCycleMode:   cycleTimerMode(); break;
      case QuickAction::TimerCycleFace:   cycleTimerFace(); break;
      case QuickAction::TimerCountUpToggle: toggleTimerCountUp(); break;
      case QuickAction::TimerEditMessage: {
        Cue* sel = selectedCueMutable();
        if (sel && sel->kind == CueKind::Timer) {
          openInlineTextEditor("timer.message", "Timer Message",
                               "Message shown under the clock:", sel->timer.message,
                               [this](const std::string& value) {
            if (Cue* c = selectedCueMutable()) {
              c->timer.message = value;
              markProjectDirty();
            }
          });
        }
        break;
      }
      case QuickAction::TimerUrgentToggle: {
        Cue* sel = selectedCueMutable();
        if (sel && sel->kind == CueKind::Timer) {
          sel->timer.messageIsUrgent = !sel->timer.messageIsUrgent;
          triggerToast(sel->timer.messageIsUrgent ? "message: URGENT (red)"
                                                  : "message: normal");
          playUiSound(UiSoundEffect::Toggle);
          markProjectDirty();
        }
        break;
      }
      case QuickAction::TimerProgressToggle: {
        Cue* sel = selectedCueMutable();
        if (sel && sel->kind == CueKind::Timer) {
          sel->timer.showProgressBar = !sel->timer.showProgressBar;
          triggerToast(sel->timer.showProgressBar ? "progress bar on" : "progress bar off");
          playUiSound(UiSoundEffect::Toggle);
          markProjectDirty();
        }
        break;
      }
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
