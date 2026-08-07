// ============================================================================
// app_cue_mgmt.ipp — Cue list management operations.
//
// Implements operations for managing the cue list:
//
//   Cue properties:
//     toggleShuffle()          — toggle deck shuffle mode
//     toggleSelectedLoop()     — toggle loop on selected cues
//     setSelectedCueEndAction() — set what happens when a cue finishes
//
//   Cue list operations:
//     addCue() / addCueAtIndex()   — add new cues (video, image, pattern, etc.)
//     deleteCue()                   — remove cue from the list
//     duplicateCue()                — clone the selected cue
//     moveCueUp() / moveCueDown()   — reorder cues in the list
//     renameCue()                   — rename via inline text editor
//
//   Import/export:
//     importWithPicker()        — open file picker to import media
//     addBrowserCueFromPrompt() — create a Browser cue from URL input
//     addKawaiiPatternCue()     — create a Pattern cue with random type
//     openSourceTypeMenu()      — open the source type selection menu
//
//   Multi-selection:
//     forEachFocusedSelectedCueMutable() — iterate over selected cues
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Toggle shuffle mode on the focused deck.
  void toggleShuffle() {
    Deck& deck = focusedDeckMutable();
    deck.shuffle = !deck.shuffle;
    triggerToast(deck.shuffle ? "shuffle on" : "shuffle off");
    playUiSound(deck.shuffle ? UiSoundEffect::Shuffle : UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void selectAllCuesInFocusedDeck() {
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) {
      return;
    }
    deck.selectedIndices.clear();
    for (int i = 0; i < static_cast<int>(deck.cues.size()); ++i) {
      deck.selectedIndices.push_back(i);
    }
    if (deck.selectedIndex < 0) {
      deck.selectedIndex = 0;
    }
    onSelectionChanged();
    triggerToast("selected all " + std::to_string(deck.cues.size()) + " cues");
    playUiSound(UiSoundEffect::Navigate);
  }

  void toggleSelectedLoop() {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    bool next = !cue->loop;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.loop = next;
    });
    triggerToast(next ? "loop on" : "loop off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedLoop(bool enabled) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->loop == enabled) {
      return;
    }
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.loop = enabled;
    });
    triggerToast(enabled ? "loop on" : "loop off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleSelectedPauseOnLastFrame() {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    bool next = !cue->pauseOnLastFrame;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.pauseOnLastFrame = next;
    });
    triggerToast(next ? "hold on" : "hold off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedPauseOnLastFrame(bool enabled) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->pauseOnLastFrame == enabled) {
      return;
    }
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.pauseOnLastFrame = enabled;
    });
    triggerToast(enabled ? "hold on" : "hold off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleSelectedPauseAtBeginning() {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    bool next = !cue->pauseAtBeginning;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.pauseAtBeginning = next;
    });
    triggerToast(next ? "pause at begin: on" : "pause at begin: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedPauseAtBeginning(bool enabled) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->pauseAtBeginning == enabled) {
      return;
    }
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.pauseAtBeginning = enabled;
    });
    triggerToast(enabled ? "pause at begin: on" : "pause at begin: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleSelectedAudioEnabled() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.hasAudio;
    });
    if (!cue) {
      return;
    }
    bool next = !cue->audioEnabled;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.hasAudio) {
        each.audioEnabled = next;
      }
    });
    triggerToast(next ? "cue audio: on" : "cue audio: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedAudioEnabled(bool enabled) {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.hasAudio;
    });
    if (!cue || cue->audioEnabled == enabled) {
      return;
    }
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.hasAudio) {
        each.audioEnabled = enabled;
      }
    });
    triggerToast(enabled ? "cue audio: on" : "cue audio: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  // ── Per-cue audio trim/pan/mono (v0.78.9) ──────────────────────────────
  // Single write path shared by the inspector quick rows (scrubbable) and
  // the remote AUDIOGAIN/AUDIOPAN/AUDIOMONO commands. Edits apply LIVE:
  // markProjectDirty → engine snapshot sync → the audio thread's atomic
  // mirrors pick them up next tick — no decode restart.

  bool setSelectedAudioGainDb(double db) {
    double clamped = std::clamp(db, static_cast<double>(kCueAudioGainMinDb),
                                static_cast<double>(kCueAudioGainMaxDb));
    bool any = forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.hasAudio) {
        each.audioGainDb = static_cast<float>(clamped);
      }
    });
    if (any) {
      markProjectDirty();
    }
    return any;
  }

  void adjustSelectedAudioGain(double deltaDb) {
    const Cue* cue = selectedCuePtr();
    if (!cue || !cue->hasAudio) {
      return;
    }
    setSelectedAudioGainDb(cue->audioGainDb + deltaDb);
  }

  bool setSelectedAudioPan(double pan) {
    double clamped = std::clamp(pan, -1.0, 1.0);
    if (std::abs(clamped) < 0.025) {
      clamped = 0.0;  // snap to center
    }
    bool any = forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.hasAudio) {
        each.audioPan = static_cast<float>(clamped);
      }
    });
    if (any) {
      markProjectDirty();
    }
    return any;
  }

  void adjustSelectedAudioPan(double delta) {
    const Cue* cue = selectedCuePtr();
    if (!cue || !cue->hasAudio) {
      return;
    }
    setSelectedAudioPan(cue->audioPan + delta);
  }

  bool setSelectedAudioMono(bool mono) {
    bool any = forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.hasAudio) {
        each.audioMono = mono;
      }
    });
    if (any) {
      markProjectDirty();
    }
    return any;
  }

  // Independent audio fade steps: below zero snaps to -1 "follow visual";
  // stepping up from follow lands on 0 ("none"), then explicit seconds.
  void adjustSelectedAudioFade(bool fadeIn, double delta) {
    const Cue* cue = selectedCuePtr();
    if (!cue || !cue->hasAudio) {
      return;
    }
    float current = fadeIn ? cue->audioFadeInSeconds : cue->audioFadeOutSeconds;
    float next;
    if (current < 0.0f) {
      next = delta > 0.0 ? 0.0f : -1.0f;         // follow → none (or stay follow)
    } else {
      next = current + static_cast<float>(delta);
      if (next < 0.0f) {
        next = current > 0.0f ? 0.0f : -1.0f;     // explicit → none → follow
      }
    }
    next = std::min(next, 60.0f);
    bool any = forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.hasAudio) {
        if (fadeIn) { each.audioFadeInSeconds = next; } else { each.audioFadeOutSeconds = next; }
      }
    });
    if (any) {
      markProjectDirty();
    }
  }

  void adjustSelectedAudioOutPair(int delta) {
    const Cue* cue = selectedCuePtr();
    if (!cue || !cue->hasAudio) {
      return;
    }
    // Pairs available on the deck's device as opened (outs 1-2 .. N-1-N).
    int maxPair = std::max(0, focusedDeck().audioOutputChannels / 2 - 1);
    int next = std::clamp(cue->audioOutputPair + delta, 0, maxPair);
    bool any = forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.hasAudio) {
        each.audioOutputPair = next;
      }
    });
    if (any) {
      markProjectDirty();
      triggerToast("audio outs " + std::to_string(next * 2 + 1) + "-" + std::to_string(next * 2 + 2));
    }
  }

  void toggleSelectedCueMono() {
    const Cue* cue = selectedCuePtr();
    if (!cue || !cue->hasAudio) {
      return;
    }
    bool next = !cue->audioMono;
    if (setSelectedAudioMono(next)) {
      triggerToast(next ? "cue audio: mono" : "cue audio: stereo");
      playUiSound(UiSoundEffect::Toggle);
    }
  }

  // ── Loudness normalize (v0.78.9) ────────────────────────────────────────
  // One-shot: measure the file's EBU R128 integrated loudness on a worker
  // thread (ffmpeg ebur128 — analysis stays on the CLI like waveforms) and
  // set the cue's gain trim so playback lands at the target. The operator
  // can still nudge the trim afterwards; normalize is a starting point,
  // not a lock.
  //
  // v0.81.0 lifted the old fixed +12 dB ceiling and instead capped the boost by
  // true-peak headroom. v0.81.5 removes that cap too, because it defeated the
  // feature: TV/film material runs an 18-22 dB peak-to-loudness ratio, so the
  // headroom cap bound on EVERY real clip — a -26.8 LUFS cartoon that wants
  // +10.8 dB got +3.9 and stayed obviously quiet, and a clip already sitting on
  // target got pulled DOWN because its inter-sample peak read above 0 dBFS.
  //
  // Normalize is now what its name says: gain = target - measured, bounded only
  // by kCueAudioGainMinDb/Max. Peaks are the limiter's job now (see
  // MediaEngine::applyPeakLimiter) rather than something to back the gain off
  // for. True peak is still measured, and the toast reports where the boost
  // lands so the operator knows when the limiter will be working.
  static constexpr double kNormalizeTargetLufs = kNormalizeTargetLufsDefault;

  void normalizeSelectedCueAudio() {
    int launched = 0;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cue.hasAudio || !cueUsesFilesystemMedia(cue)) {
        return;
      }
      std::string path = resolvedCueFilesystemPathString(cue, currentProjectFile_);
      if (path.empty()) {
        return;
      }
      std::string cueId = cue.id;
      ++launched;
      std::thread([this, path, cueId]() {
        NormalizeResult result;
        result.cueId = cueId;
        // peak=true adds a "True peak: / Peak: -x.x dBFS" block to the summary,
        // which is what lets the boost be limited by real headroom rather than
        // by an arbitrary number.
        auto out = readAllText({
          "ffmpeg", "-hide_banner", "-nostats",
          "-i", path, "-map", "a:0", "-af", "ebur128=peak=true", "-f", "null", "-"
        });
        if (out) {
          // The ebur128 summary block ends with lines like "I: -21.3 LUFS".
          // Both markers are matched with rfind so the per-frame progress
          // lines above the summary can't win.
          size_t pos = out->rfind("I:");
          if (pos != std::string::npos) {
            double lufs = std::strtod(out->c_str() + pos + 2, nullptr);
            if (std::isfinite(lufs) && lufs < 0.0 && lufs > -70.0) {
              result.measuredLufs = lufs;
              double gain = std::clamp(kNormalizeTargetLufs - lufs,
                                       static_cast<double>(kCueAudioGainMinDb),
                                       static_cast<double>(kCueAudioGainMaxDb));

              // "Peak:" (capital P) is the summary value; the "True peak:"
              // heading above it is lower-case, so rfind lands on the number.
              // Measured for REPORTING only — it no longer holds the gain back.
              size_t peakPos = out->rfind("Peak:");
              if (peakPos != std::string::npos) {
                double peakDb = std::strtod(out->c_str() + peakPos + 5, nullptr);
                // True peak legitimately exceeds 0 dBFS on hot masters
                // (inter-sample peaks), so the sanity window must not assume <= 0.
                if (std::isfinite(peakDb) && peakDb < 24.0 && peakDb > -120.0) {
                  result.measuredPeakDb = peakDb;
                  result.hasPeak = true;
                  // Where the peaks will actually land once the trim is applied.
                  // Above the ceiling just means the limiter has work to do.
                  result.projectedPeakDb = peakDb + gain;
                  result.peakLimited = result.projectedPeakDb > kNormalizeTruePeakCeilingDb;
                }
              }
              result.gainDb = gain;
              result.ok = true;
            }
          }
        }
        std::lock_guard<std::mutex> lock(normalizeResultsMutex_);
        normalizeResults_.push_back(std::move(result));
      }).detach();
    });
    if (launched > 0) {
      triggerToast(launched == 1 ? "analyzing loudness..."
                                 : "analyzing loudness (" + std::to_string(launched) + " cues)...");
    } else {
      // Previously a silent no-op — the operator clicked NORMALIZE and
      // nothing visibly happened.
      triggerToast("normalize: selection has no file-backed audio");
    }
  }

  // Drained once per tick on the main thread.
  void drainNormalizeResults() {
    std::vector<NormalizeResult> results;
    {
      std::lock_guard<std::mutex> lock(normalizeResultsMutex_);
      if (normalizeResults_.empty()) {
        return;
      }
      results.swap(normalizeResults_);
    }
    for (const NormalizeResult& result : results) {
      if (!result.ok) {
        triggerToast("normalize: no measurable audio");
        continue;
      }
      bool applied = false;
      for (Deck& deck : project_.decks) {
        for (Cue& cue : deck.cues) {
          if (cue.id == result.cueId) {
            cue.audioGainDb = static_cast<float>(result.gainDb);
            applied = true;
          }
        }
      }
      if (applied) {
        char buf[128];
        if (result.peakLimited) {
          // Target IS hit; this just tells the operator the deck limiter will
          // be catching transients on this clip. Held longer — it's the case
          // most worth reading.
          std::snprintf(buf, sizeof(buf),
                        "normalized: %+.1f dB (was %.1f LUFS) - peaks %+.1f dBFS, limiter active",
                        result.gainDb, result.measuredLufs, result.projectedPeakDb);
          triggerToast(buf, {155, 188, 15, 220}, {15, 56, 15, 255}, 3200);
        } else {
          std::snprintf(buf, sizeof(buf), "normalized: %+.1f dB (was %.1f LUFS)",
                        result.gainDb, result.measuredLufs);
          triggerToast(buf, {155, 188, 15, 220}, {15, 56, 15, 255}, 2600);
        }
        markProjectDirty();
      }
    }
  }

  void toggleSelectedTransitionToNext() {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    bool next = !cue->transitionToNext;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.transitionToNext = next;
    });
    triggerToast(next ? "next transition: on" : "next transition: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedTransitionToNext(bool enabled) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->transitionToNext == enabled) {
      return;
    }
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.transitionToNext = enabled;
    });
    triggerToast(enabled ? "next transition: on" : "next transition: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedGotoTarget(const std::string& token) {
    std::string trimmed = trim(token);
    if (!forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.gotoTarget = trimmed;
    })) {
      return;
    }
    triggerToast(trimmed.empty() ? "goto target: cleared" : ("goto target: " + trimmed));
    markProjectDirty();
  }

  void adjustSelectedFade(bool fadeIn, double deltaSeconds) {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    double sampleTarget = std::clamp((fadeIn ? cue->fadeInSeconds : cue->fadeOutSeconds) + deltaSeconds, 0.0, 10.0);
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      double& target = fadeIn ? each.fadeInSeconds : each.fadeOutSeconds;
      target = std::clamp(target + deltaSeconds, 0.0, 10.0);
    });
    triggerToast(std::string(fadeIn ? "fade in " : "fade out ") + formatSeconds(sampleTarget));
    markProjectDirty();
  }

  void adjustSelectedIn(double delta) {
    double sample = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video) {
        return;
      }
      cue.inPointSeconds = std::clamp(cue.inPointSeconds + delta, 0.0, cue.duration > 0.0 ? cue.duration : 3600.0);
      if (!changed) {
        sample = cue.inPointSeconds;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("in " + formatSeconds(sample));
    markProjectDirty();
  }

  void adjustSelectedOut(double delta) {
    double sample = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video) {
        return;
      }
      double cur = cue.outPointSeconds > 0.0 ? cue.outPointSeconds : cue.duration;
      cur = std::clamp(cur + delta, 0.0, cue.duration > 0.0 ? cue.duration : 3600.0);
      cue.outPointSeconds = cur;
      if (!changed) {
        sample = cue.outPointSeconds;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("out " + formatSeconds(sample));
    markProjectDirty();
  }

  void adjustSelectedCueTransition(double delta) {
    double sample = 0.0;
    bool changed = false;
    double deckDefault = focusedDeck().transitionSeconds;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.cueTransitionSeconds < 0.0) {
        cue.cueTransitionSeconds = deckDefault;
      }
      cue.cueTransitionSeconds = std::clamp(cue.cueTransitionSeconds + delta, 0.0, 10.0);
      if (!changed) {
        sample = cue.cueTransitionSeconds;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("cue trans " + formatSeconds(sample));
    markProjectDirty();
  }

  void cycleSelectedCueTransStyle() {
    Cue* cue = selectedCueMutable();
    if (!cue) return;
    static const std::vector<std::string> kStyles = {"cut", "crossfade", "dip"};
    std::string cur = cue->cueTransitionStyle.empty() ? focusedDeck().transitionStyle : cue->cueTransitionStyle;
    auto it = std::find(kStyles.begin(), kStyles.end(), cur);
    std::string nextStyle = (it == kStyles.end() || std::next(it) == kStyles.end())
      ? kStyles.front()
      : *std::next(it);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.cueTransitionStyle = nextStyle;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("cue style: " + nextStyle);
    markProjectDirty();
  }

  void adjustSelectedLowerAlpha(int delta) {
    int sample = 0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::LowerThird) {
        return;
      }
      cue.lowerThirdBgAlpha = std::clamp(cue.lowerThirdBgAlpha + delta, 0, 255);
      if (!changed) {
        sample = cue.lowerThirdBgAlpha;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("overlay alpha " + std::to_string(sample));
    markProjectDirty();
  }

  void adjustSelectedStillDuration(double delta) {
    double sample = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind == CueKind::Video || cue.kind == CueKind::Audio) {
        return;
      }
      cue.stillDurationSeconds = std::max(0.0, cue.stillDurationSeconds + delta);
      if (cue.stillDurationSeconds < 0.5 && delta < 0) cue.stillDurationSeconds = 0.0;
      if (!changed) {
        sample = cue.stillDurationSeconds;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast(sample > 0.0
      ? "still dur " + formatSeconds(sample)
      : "still dur: hold");
    markProjectDirty();
  }

  void adjustSelectedLoopCount(int delta) {
    int sample = 0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video && cue.kind != CueKind::Audio) {
        return;
      }
      cue.loopCount = std::max(0, cue.loopCount + delta);
      if (!changed) {
        sample = cue.loopCount;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast(sample == 0 ? "repeats: inf" : "repeats: " + std::to_string(sample) + "x");
    markProjectDirty();
  }

  void adjustSelectedSpeed(double delta) {
    double sample = 1.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video && cue.kind != CueKind::Audio) {
        return;
      }
      cue.playbackSpeed = std::clamp(std::round((cue.playbackSpeed + delta) * 4.0) / 4.0, 0.25, 4.0);
      if (!changed) {
        sample = cue.playbackSpeed;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    refreshFocusedLiveCueRuntimeIfSelected();
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << sample;
    triggerToast("speed: " + ss.str() + "x");
    markProjectDirty();
  }

  void cycleSelectedColorTag() {
    Cue* cue = selectedCueMutable();
    if (!cue) return;
    std::string next = nextColorTag(cue->colorTag);
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.colorTag = next;
    });
    triggerToast("tag: " + (next.empty() ? "none" : next));
    markProjectDirty();
  }

  void clearOverlay() {
    Deck& deck = focusedDeckMutable();
    deck.overlayActiveIndices.clear();
    syncPipOverlayRuntimesForDeck(project_.focusedDeckIndex, SDL_GetTicks());
    triggerToast("overlay cleared");
    markProjectDirty();
  }

  void popOverlay() {
    Deck& deck = focusedDeckMutable();
    if (!deck.overlayActiveIndices.empty()) {
      deck.overlayActiveIndices.pop_back();
      syncPipOverlayRuntimesForDeck(project_.focusedDeckIndex, SDL_GetTicks());
      triggerToast("overlay popped");
      markProjectDirty();
    }
  }

  void setSelectedFade(bool fadeIn, double seconds) {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    double next = std::clamp(seconds, 0.0, 10.0);
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      double& target = fadeIn ? each.fadeInSeconds : each.fadeOutSeconds;
      target = next;
    });
    triggerToast(std::string(fadeIn ? "fade in " : "fade out ") + formatSeconds(next));
    markProjectDirty();
  }

  void toggleSelectedFadeEnabled(bool fadeIn) {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    double current = fadeIn ? cue->fadeInSeconds : cue->fadeOutSeconds;
    double defaultFade = std::clamp(focusedDeck().playlistDefaultCueFadeSeconds, 0.25, 2.0);
    double next = current > 0.001 ? 0.0 : defaultFade;
    setSelectedFade(fadeIn, next);
    playUiSound(UiSoundEffect::Toggle);
  }

  void clearPendingLiveDeleteConfirmation() {
    pendingLiveDeleteConfirmDeckIndex_ = -1;
    pendingLiveDeleteConfirmSignature_.clear();
    pendingLiveDeleteConfirmMessage_.clear();
    pendingLiveDeleteConfirmUntilMs_ = 0;
  }

  std::string cueDeleteSignatureForDeck(const Deck& deck, const std::vector<int>& indices) const {
    std::ostringstream sig;
    sig << indices.size();
    for (int index : indices) {
      sig << '|';
      if (index >= 0 && index < static_cast<int>(deck.cues.size())) {
        sig << deck.cues[index].id;
      } else {
        sig << index;
      }
    }
    return sig.str();
  }

  bool cueIndicesIncludeLiveCue(const Deck& deck, const std::vector<int>& indices) const {
    for (int index : indices) {
      if (index == deck.activeIndex) {
        return true;
      }
      if (std::find(deck.overlayActiveIndices.begin(), deck.overlayActiveIndices.end(), index) !=
          deck.overlayActiveIndices.end()) {
        return true;
      }
    }
    return false;
  }

  int remapCueIndexAfterDeletion(int index, const std::vector<int>& deletedIndices) const {
    if (index < 0) {
      return -1;
    }
    auto it = std::lower_bound(deletedIndices.begin(), deletedIndices.end(), index);
    if (it != deletedIndices.end() && *it == index) {
      return -1;
    }
    return index - static_cast<int>(std::distance(deletedIndices.begin(), it));
  }

  bool requestDeleteCueIndices(int deckIndex, std::vector<int> indices) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    Deck& deck = project_.decks[deckIndex];
    indices.erase(std::remove_if(indices.begin(), indices.end(), [&](int index) {
      return index < 0 || index >= static_cast<int>(deck.cues.size());
    }), indices.end());
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices.empty()) {
      clearPendingLiveDeleteConfirmation();
      return false;
    }

    if (cueIndicesIncludeLiveCue(deck, indices)) {
      Uint64 now = SDL_GetTicks();
      std::string signature = cueDeleteSignatureForDeck(deck, indices);
      bool confirmed = pendingLiveDeleteConfirmDeckIndex_ == deckIndex &&
                       pendingLiveDeleteConfirmSignature_ == signature &&
                       now <= pendingLiveDeleteConfirmUntilMs_;
      if (!confirmed) {
        pendingLiveDeleteConfirmDeckIndex_ = deckIndex;
        pendingLiveDeleteConfirmSignature_ = signature;
        pendingLiveDeleteConfirmMessage_ = indices.size() == 1
          ? "DELETE LIVE CUE?  PRESS DELETE AGAIN"
          : ("DELETE " + std::to_string(indices.size()) + " LIVE CUES?  PRESS DELETE AGAIN");
        pendingLiveDeleteConfirmUntilMs_ = now + 2500;
        triggerToast(indices.size() == 1
          ? "delete live cue: press delete again"
          : ("delete " + std::to_string(indices.size()) + " live cues: press delete again"));
        return false;
      }
    }

    clearPendingLiveDeleteConfirmation();
    pushUndoSnapshot();

    std::sort(indices.begin(), indices.end());
    bool removedActive = std::find(indices.begin(), indices.end(), deck.activeIndex) != indices.end();
    int firstDeleted = indices.front();
    if (removedActive) {
      stopBrowserCue(deckIndex);
      if (MediaEngine* engine = mediaEngineForDeck(deckIndex)) {
        engine->clear();
      }
    }

    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
      deck.cues.erase(deck.cues.begin() + *it);
    }
    std::vector<int> remappedOverlay;
    remappedOverlay.reserve(deck.overlayActiveIndices.size());
    for (int overlayIndex : deck.overlayActiveIndices) {
      int remapped = remapCueIndexAfterDeletion(overlayIndex, indices);
      if (remapped >= 0) {
        remappedOverlay.push_back(remapped);
      }
    }
    deck.overlayActiveIndices = std::move(remappedOverlay);

    if (deck.cues.empty()) {
      deck.selectedIndex = -1;
      deck.activeIndex = -1;
      deck.selectedIndices.clear();
    } else {
      deck.selectedIndex = std::min(firstDeleted, static_cast<int>(deck.cues.size()) - 1);
      if (removedActive) {
        deck.activeIndex = -1;
      } else if (deck.activeIndex >= 0) {
        deck.activeIndex = remapCueIndexAfterDeletion(deck.activeIndex, indices);
      }
      deck.selectedIndices.clear();
      deck.selectedIndices.push_back(deck.selectedIndex);
    }
    if (deckIndex == project_.focusedDeckIndex) {
      onSelectionChanged();
    }
    syncPipOverlayRuntimesForDeck(deckIndex, SDL_GetTicks());
    if (indices.size() == 1) {
      triggerToast("cue deleted");
    } else {
      triggerToast(std::to_string(indices.size()) + " cues deleted");
    }
    playUiSound(UiSoundEffect::Delete);
    markProjectDirty();
    return true;
  }

  void deleteSelected() {
    Deck& deck = focusedDeckMutable();
    auto indices = selectedCueIndices(deck);
    requestDeleteCueIndices(project_.focusedDeckIndex, std::move(indices));
  }

  void handleDropFile(const char* rawPath) {
    if (!rawPath) {
      return;
    }
    importPaths({rawPath});
  }

  void importWithPicker() {
    pushUndoSnapshot();
    if (pendingImport_.valid() &&
        pendingImport_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      return;  // already picking
    }
    pendingImport_ = std::async(std::launch::async, [this] {
      return pickFiles();
    });
  }

  std::optional<fs::path> pickProjectPath(bool saveMode,
                                          std::string initialPath = {},
                                          std::string dialogTitle = {}) {
    if (dialogTitle.empty()) {
      dialogTitle = saveMode ? "Save Deckboy playlist" : "Open Deckboy playlist";
    }
#ifdef _WIN32
    // TopMost owner form forces the native dialog to the front of the z-order
    // (it runs in a separate powershell process with no parent, so otherwise it
    // can open behind the Deckboy window). See pickFiles() for the same fix.
    const std::string ownerPrelude =
      "Add-Type -AssemblyName System.Windows.Forms;"
      "$o=New-Object System.Windows.Forms.Form;$o.TopMost=$true;$o.ShowInTaskbar=$false;$o.Opacity=0;$o.Show()|Out-Null;$o.Activate()|Out-Null;";
    std::string script = saveMode
      ? ownerPrelude + "$dialog = New-Object System.Windows.Forms.SaveFileDialog;$dialog.Title = '" + dialogTitle + "';$dialog.Filter = 'Deckboy Playlist (*.deckboy)|*.deckboy';$r=$dialog.ShowDialog($o);$o.Close();if ($r -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 };$dialog.FileName"
      : ownerPrelude + "$dialog = New-Object System.Windows.Forms.OpenFileDialog;$dialog.Title = '" + dialogTitle + "';$dialog.Filter = 'Deckboy Playlist (*.deckboy)|*.deckboy';$r=$dialog.ShowDialog($o);$o.Close();if ($r -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 };$dialog.FileName";
    auto text = readAllText({"powershell.exe", "-NoProfile", "-Command", script});
#elif __APPLE__
    auto text = saveMode
      ? readAllText({
          "osascript",
          "-e",
          "set targetFile to choose file name with prompt \"" + dialogTitle + "\"",
          "-e",
          "return POSIX path of targetFile"
        })
      : readAllText({
          "osascript",
          "-e",
          "set pickedFile to choose file with prompt \"" + dialogTitle + "\"",
          "-e",
          "return POSIX path of pickedFile"
        });
#else
    auto text = saveMode
      ? readAllText({
          "zenity",
          "--file-selection",
          "--save",
          "--confirm-overwrite",
          "--title=" + dialogTitle,
          "--filename",
          initialPath
        })
      : readAllText({
          "zenity",
          "--file-selection",
          "--title=" + dialogTitle,
          "--filename",
          initialPath
        });
#endif
    if (!text) {
      return std::nullopt;
    }
    std::string value = trim(*text);
    if (value.empty()) {
      return std::nullopt;
    }
    return normalizeProjectPath(fs::absolute(value));
  }

  void openProjectFromPath(const fs::path& projectPath) {
    fs::path normalized = normalizeProjectPath(projectPath);
    resetTransientPreviewState();
    currentProjectFile_ = normalized;
    rememberLastOpenedProjectFile(currentProjectFile_);
    // Big shows (1,500+ cues off a USB drive) parse and build runtimes with the
    // render loop stopped. Drive a loading overlay from inside that work so the
    // window shows progress instead of appearing hung. It self-hides for opens
    // that finish inside 250 ms, which is most of them.
    beginLoadingOverlay("OPENING SHOW", normalized.filename().string());
    project_ = loadProject(normalized, [this](double frac) {
      // Parsing is the bulk of the wait, so give it most of the bar.
      loadingOverlayProgress(frac * 0.8);
    });
    loadingOverlayProgress(0.82, "checking the show");
    normalizeProject(project_);
    // Apply the show's saved color theme. Empty = leave the current theme
    // as-is (older theme-less shows don't stomp the operator's pick).
    if (!project_.theme.empty()) {
      loadTheme(project_.theme);
    }
    disarmAllOutputsForStartup();
    // Open lands on a neutral "nothing live" state, same as a fresh launch:
    // clear any saved active cue so the timeline and preview agree (the saved
    // index otherwise leaves a ghost active cue in the timeline with a blank
    // preview) and the operator explicitly takes the first cue. Also lets the
    // startup mascot show. The selected cue is preserved for prepping/taking.
    for (auto& deck : project_.decks) { deck.activeIndex = -1; }
    timecodeTriggeredCueIds_.clear();
    cueRowDisplayCache_.clear();
    resetTimecodeFollowerState();
    selectionChangedAt_ = SDL_GetTicks();
    loadingOverlayProgress(0.88, "building decks");
    if (!rebuildDeckRuntimes()) {
      std::cerr << "Deck runtime creation failed: " << SDL_GetError() << '\n';
    }
    loadingOverlayProgress(0.95, "wiring outputs");
    if (!rebuildOutputRuntimes()) {
      std::cerr << "Output runtime creation failed: " << SDL_GetError() << '\n';
    }
    loadingOverlayProgress(1.0, "ready");
    endLoadingOverlay();
    triggerToast("playlist: " + currentProjectLabel());
    // Presence scan runs async (seconds of frozen UI on big USB playlists);
    // the RELINK toast follows when it lands.
    startMediaPresenceScanAsync(true);
    queueAudioMetadataRepairProbes();
  }

  // Cues saved before audioChannels/audioSampleRate existed load with 0 in
  // those fields, which quietly downgrades the stereo waveform lane to the
  // mono view. Re-probe them in the background and backfill (the update()
  // probe poll has a matching repair branch).
  void queueAudioMetadataRepairProbes() {
    constexpr int kMaxRepairProbes = 64;
    int queued = 0;
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      for (const Cue& cue : project_.decks[deckIndex].cues) {
        if (queued >= kMaxRepairProbes) {
          return;
        }
        bool fileBacked = cue.kind == CueKind::Video || cue.kind == CueKind::Audio;
        if (!fileBacked || !cue.hasAudio || cue.audioChannels > 0 || cue.path.empty()) {
          continue;
        }
        fs::path mediaPath(resolvedCueFilesystemPathString(cue, currentProjectFile_));
        std::error_code existsEc;
        if (mediaPath.empty() || !fs::exists(mediaPath, existsEc)) {
          continue;
        }
        PendingProbe pp;
        pp.deckIndex = deckIndex;
        pp.path = cue.path;
        std::string probePathStr = mediaPath.string();
        pp.future = std::async(std::launch::async, [probePathStr]() {
          return probeCue(fs::path(probePathStr));
        });
        probeFutures_.push_back(std::move(pp));
        ++queued;
      }
    }
  }

  void openProjectFromPicker() {
    if (pendingProjectOpen_.valid() &&
        pendingProjectOpen_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      return;
    }
    std::string ip = currentProjectFile_.string();  // capture on main thread — no race
    pendingProjectOpen_ = std::async(std::launch::async, [this, ip = std::move(ip)] {
      return pickProjectPath(false, ip);
    });
  }

  void saveProjectAsFromPicker() {
    if (pendingProjectSaveAs_.valid() &&
        pendingProjectSaveAs_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      return;
    }
    std::string ip = currentProjectFile_.string();  // capture on main thread — no race
    pendingProjectSaveAs_ = std::async(std::launch::async, [this, ip = std::move(ip)] {
      return pickProjectPath(true, ip);
    });
  }

  std::vector<std::string> pickFiles() {
#ifdef _WIN32
    auto text = readAllText({
      "powershell.exe",
      "-NoProfile",
      "-Command",
      "Add-Type -AssemblyName System.Windows.Forms;"
      // Owner form is TopMost so the native dialog opens IN FRONT of Deckboy
      // instead of behind it (the picker runs in a separate powershell process
      // with no parent window, so without this it lands at the back of the
      // z-order and looks like nothing happened).
      "$o=New-Object System.Windows.Forms.Form;$o.TopMost=$true;$o.ShowInTaskbar=$false;$o.Opacity=0;$o.Show()|Out-Null;$o.Activate()|Out-Null;"
      "$dialog = New-Object System.Windows.Forms.OpenFileDialog;"
      "$dialog.Multiselect = $true;"
      "$r=$dialog.ShowDialog($o);$o.Close();"
      "if ($r -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 };"
      "$dialog.FileNames -join \"`n\""
    });
#elif __APPLE__
    auto text = readAllText({
      "osascript",
      "-e",
      "set filesPicked to choose file with multiple selections allowed true",
      "-e",
      "set outputLines to {}",
      "-e",
      "repeat with currentFile in filesPicked",
      "-e",
      "set end of outputLines to POSIX path of currentFile",
      "-e",
      "end repeat",
      "-e",
      "set AppleScript's text item delimiters to linefeed",
      "-e",
      "return outputLines as text"
    });
#else
    auto text = readAllText({
      "zenity",
      "--file-selection",
      "--multiple",
      "--separator=|",
      "--title=Import media into Deckboy Native"
    });
#endif

    if (!text) {
      return {};
    }

    std::vector<std::string> paths;
#ifdef __linux__
    for (const auto& value : splitByChar(*text, '|')) {
      if (!trim(value).empty()) {
        paths.push_back(trim(value));
      }
    }
#else
    for (const auto& line : splitLines(*text)) {
      if (!trim(line).empty()) {
        paths.push_back(trim(line));
      }
    }
#endif
    return paths;
  }

  std::optional<fs::path> pickFolder(std::string dialogTitle) {
#ifdef _WIN32
    // Same TopMost owner-form trick as pickFiles() — see the comment there.
    auto text = readAllText({
      "powershell.exe",
      "-NoProfile",
      "-Command",
      "Add-Type -AssemblyName System.Windows.Forms;"
      "$o=New-Object System.Windows.Forms.Form;$o.TopMost=$true;$o.ShowInTaskbar=$false;$o.Opacity=0;$o.Show()|Out-Null;$o.Activate()|Out-Null;"
      "$dialog = New-Object System.Windows.Forms.FolderBrowserDialog;"
      "$dialog.Description = '" + dialogTitle + "';"
      "$dialog.ShowNewFolderButton = $false;"
      "$r=$dialog.ShowDialog($o);$o.Close();"
      "if ($r -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 };"
      "$dialog.SelectedPath"
    });
#elif __APPLE__
    auto text = readAllText({
      "osascript",
      "-e",
      "set pickedFolder to choose folder with prompt \"" + dialogTitle + "\"",
      "-e",
      "return POSIX path of pickedFolder"
    });
#else
    auto text = readAllText({
      "zenity",
      "--file-selection",
      "--directory",
      "--title=" + dialogTitle
    });
#endif
    if (!text) {
      return std::nullopt;
    }
    std::string picked = trim(*text);
    if (picked.empty()) {
      return std::nullopt;
    }
    return fs::path(picked);
  }

  void applyDeckDefaultsToCue(Cue& cue, const Deck& deck) {
    cue.loop = deck.playlistDefaultLoop;
    cue.pauseAtBeginning = deck.playlistDefaultPauseAtBeginning;
    cue.pauseOnLastFrame = deck.playlistDefaultPauseAtEnd;
    cue.transitionToNext = deck.playlistDefaultTransitionToNext;
    cue.audioEnabled = cue.hasAudio ? deck.playlistDefaultAudioEnabled : false;

    double fadeDefault = std::clamp(deck.playlistDefaultCueFadeSeconds, 0.0, 10.0);
    cue.fadeInSeconds = deck.playlistDefaultFadeInEnabled ? fadeDefault : 0.0;
    cue.fadeOutSeconds = deck.playlistDefaultFadeOutEnabled ? fadeDefault : 0.0;

    if (isDefaultStillDurationCueKind(cue.kind)) {
      cue.stillDurationSeconds = std::clamp(deck.playlistDefaultStillDurationSeconds, 0.0, 3600.0);
      // Stills hold at the end of their duration by default, so a fade-out would
      // dip the held frame to black — almost never what you want for a static
      // graphic. Default still-type cues to NO fade-out; the operator can still
      // turn fade-out on per cue (and it will then ramp as configured).
      cue.fadeOutSeconds = 0.0;
    }
  }

  void editFocusedDeckPlaylistPreferences() {
    if (project_.decks.empty()) {
      return;
    }
    struct PlaylistPrefsDraft {
      int deckIndex = -1;
      double playlistTimebaseFps = 30.0;
      double playlistStartOffsetSeconds = 0.0;
      double playlistDefaultCueFadeSeconds = 1.5;
      double playlistDefaultStillDurationSeconds = 5.0;
      bool changed = false;
    };
    Deck& deck = focusedDeckMutable();
    auto draft = std::make_shared<PlaylistPrefsDraft>();
    draft->deckIndex = std::clamp(project_.focusedDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    draft->playlistTimebaseFps = deck.playlistTimebaseFps;
    draft->playlistStartOffsetSeconds = deck.playlistStartOffsetSeconds;
    draft->playlistDefaultCueFadeSeconds = deck.playlistDefaultCueFadeSeconds;
    draft->playlistDefaultStillDurationSeconds = deck.playlistDefaultStillDurationSeconds;

    auto applyDraft = [this, draft]() {
      if (draft->deckIndex < 0 || draft->deckIndex >= static_cast<int>(project_.decks.size())) {
        return;
      }
      Deck& target = project_.decks[draft->deckIndex];
      if (draft->changed) {
        target.playlistTimebaseFps = draft->playlistTimebaseFps;
        target.timecodeFps = draft->playlistTimebaseFps;
        target.playlistStartOffsetSeconds = draft->playlistStartOffsetSeconds;
        target.playlistDefaultCueFadeSeconds = draft->playlistDefaultCueFadeSeconds;
        target.playlistDefaultStillDurationSeconds = draft->playlistDefaultStillDurationSeconds;
        markProjectDirty();
        triggerToast(
          "playlist prefs: "
          + playlistTimebaseLabel(target.playlistTimebaseFps)
          + " start "
          + formatTimecode(target.playlistStartOffsetSeconds, target.playlistTimebaseFps));
      }
    };

    auto openStillStep = std::make_shared<std::function<void()>>();
    auto openFadeStep = std::make_shared<std::function<void()>>();
    auto openStartStep = std::make_shared<std::function<void()>>();
    auto openFpsStep = std::make_shared<std::function<void()>>();

    *openStillStep = [this, draft, applyDraft]() {
      std::ostringstream ss;
      ss << std::fixed << std::setprecision(2) << draft->playlistDefaultStillDurationSeconds;
      openInlineTextEditor("playlist.still_duration",
                           "Default Non-Movie Duration",
                           "Seconds for new image/pattern/browser/Lower Third cues (0 = hold)",
                           ss.str(),
                           [this, draft, applyDraft](const std::string& value) {
                             try {
                               double next = std::clamp(std::stod(trim(value)), 0.0, 3600.0);
                               if (std::fabs(draft->playlistDefaultStillDurationSeconds - next) > 0.001) {
                                 draft->playlistDefaultStillDurationSeconds = next;
                                 draft->changed = true;
                               }
                             } catch (...) {
                               triggerToast("still duration: invalid");
                             }
                             applyDraft();
                           });
    };

    *openFadeStep = [this, draft, openStillStep]() {
      std::ostringstream ss;
      ss << std::fixed << std::setprecision(2) << draft->playlistDefaultCueFadeSeconds;
      openInlineTextEditor("playlist.fade_default",
                           "Default Cue Fade",
                           "Seconds for new-cue fade in/out defaults",
                           ss.str(),
                           [this, draft, openStillStep](const std::string& value) {
                             try {
                               double next = std::clamp(std::stod(trim(value)), 0.0, 10.0);
                               if (std::fabs(draft->playlistDefaultCueFadeSeconds - next) > 0.001) {
                                 draft->playlistDefaultCueFadeSeconds = next;
                                 draft->changed = true;
                               }
                             } catch (...) {
                               triggerToast("cue fade: invalid");
                             }
                             (*openStillStep)();
                           });
    };

    *openStartStep = [this, draft, openFadeStep]() {
      std::string startDefault = formatTimecode(draft->playlistStartOffsetSeconds, draft->playlistTimebaseFps);
      openInlineTextEditor("playlist.start_tc",
                           "Playlist Start TC",
                           "SMPTE hh:mm:ss:ff or seconds (blank = 00:00:00:00)",
                           startDefault,
                           [this, draft, openFadeStep](const std::string& value) {
                             std::string token = trim(value);
                             double parsed = 0.0;
                             bool valid = false;
                             if (token.empty()) {
                               valid = true;
                             } else if (auto tc = parseTimecodeSeconds(token, draft->playlistTimebaseFps)) {
                               parsed = *tc;
                               valid = true;
                             } else {
                               try {
                                 parsed = std::max(0.0, std::stod(token));
                                 valid = true;
                               } catch (...) {
                                 triggerToast("playlist start: invalid");
                               }
                             }
                             if (valid) {
                               parsed = std::clamp(parsed, 0.0, 24.0 * 60.0 * 60.0);
                               if (std::fabs(draft->playlistStartOffsetSeconds - parsed) > 0.001) {
                                 draft->playlistStartOffsetSeconds = parsed;
                                 draft->changed = true;
                               }
                             }
                             (*openFadeStep)();
                           });
    };

    *openFpsStep = [this, draft, openStartStep]() {
      openInlineTextEditor("playlist.timebase",
                           "Playlist Timebase",
                           "Enter playlist SMPTE FPS: 24 / 25 / 29.97 / 30",
                           playlistTimebaseLabel(draft->playlistTimebaseFps),
                           [this, draft, openStartStep](const std::string& value) {
                             try {
                               double next = normalizePlaylistTimebaseFps(std::stod(trim(value)));
                               if (std::fabs(draft->playlistTimebaseFps - next) > 0.001) {
                                 draft->playlistTimebaseFps = next;
                                 draft->changed = true;
                               }
                             } catch (...) {
                               triggerToast("playlist fps: invalid");
                             }
                             (*openStartStep)();
                           });
    };

    (*openFpsStep)();
  }

  void addBrowserCue(const std::string& rawUrl) {
    std::string url = normalizeBrowserUrl(rawUrl);
    if (url.empty()) {
      return;
    }
#if defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
    // Browser cues on Windows render through WebView2. Without the runtime,
    // the cue would author OK but render as a black frame at TAKE — surface
    // the requirement at creation instead so the operator can install once.
    if (!webView2RuntimeAvailable()) {
      promptForWebView2Runtime();
      return;
    }
#endif
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);

    Cue cue;
    cue.kind = CueKind::Browser;
    cue.path = url;
    cue.name = browserCueNameForUrl(url);
    cue.width = rasterW;
    cue.height = rasterH;
    cue.color = SDL_Color {139, 172, 15, 255};
    cue.formatName = "browser";
    cue.videoCodec = "chromium";
    cue.audioCodec = "system";
    cue.hasAudio = true;
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("browser cue added");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  void addBrowserCueFromPrompt() {
    openInlineTextEditor("tool.add_browser",
                         "Add Browser Cue",
                         "URL or local page path",
                         "https://duckduckgo.com",
                         [this](const std::string& value) {
                           std::string normalized = normalizeBrowserUrl(trim(value));
                           if (normalized.empty()) {
                             triggerToast("browser url: invalid");
                             return;
                           }
                           addBrowserCue(normalized);
                         });
  }

  void addSrtStreamCue(const std::string& url) {
    std::string trimmedUrl = trim(url);
    if (trimmedUrl.empty()) {
      return;
    }
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
    Cue cue;
    cue.kind = CueKind::SrtStream;
    cue.path = trimmedUrl;
    cue.name = "Stream";
    cue.width = rasterW;
    cue.height = rasterH;
    cue.duration = 0.0;
    cue.stillDurationSeconds = 0.0;
    cue.hasAudio = true;
    cue.formatName = "stream";
    cue.videoCodec = "stream";
    cue.audioCodec = "stream";
    cue.color = SDL_Color {72, 130, 180, 255};
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("stream cue added");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  void addSrtStreamCueFromPrompt() {
    openInlineTextEditor("tool.add_stream",
                         "Add Stream Cue",
                         "Stream URL (srt://, rtmp://, rtsp://...)",
                         "srt://127.0.0.1:9000?mode=listener",
                         [this](const std::string& value) {
                           std::string url = trim(value);
                           if (url.empty()) {
                             triggerToast("stream url: required");
                             return;
                           }
                           addSrtStreamCue(url);
                         });
  }

  void addNdiSourceCue(const std::string& sourceName) {
    std::string name = trim(sourceName);
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
    Cue cue;
    cue.kind = CueKind::NdiSource;
    cue.path = "ndi://" + name;
    cue.name = name.empty() ? "NDI Source" : name;
    cue.width = rasterW;
    cue.height = rasterH;
    cue.duration = 0.0;
    cue.stillDurationSeconds = 0.0;
    cue.hasAudio = true;
    cue.formatName = "ndi";
    cue.videoCodec = "ndi";
    cue.audioCodec = "ndi";
    cue.color = SDL_Color {180, 72, 72, 255};
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("NDI source cue added");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  void addNdiSourceCueFromPrompt() {
    openInlineTextEditor("tool.add_ndi",
                         "Add NDI Source Cue",
                         "NDI source name (e.g. LAPTOP (source 1))",
                         "",
                         [this](const std::string& value) {
                           addNdiSourceCue(trim(value));
                         });
  }

  void openInlineCueFindEditor(bool takeAfterFind, const std::string& initialValue = {}) {
    std::string seed = initialValue.empty() ? lastCueFindToken_ : initialValue;
    openInlineTextEditor(takeAfterFind ? "tool.find_take" : "tool.find",
                         takeAfterFind ? "Find+Take Cue" : "Find Cue",
                         "Cue number, cue id, or name contains:",
                         seed,
                         [this, takeAfterFind](const std::string& value) {
                           std::string token = trim(value);
                           if (token.empty()) {
                             return;
                           }
                           findCueToken(token, 1, takeAfterFind);
                         });
  }

  void applyRenumberCueInput(const std::string& rawInput, bool allowOptionalStart) {
    std::string trimmedInput = trim(rawInput);
    if (trimmedInput.empty()) {
      renumberFocusedDeckCueNumbers("", 1);
      return;
    }
    auto parts = splitWhitespace(trimmedInput);
    std::string prefix = parts.empty() ? "" : parts[0];
    int start = 1;
    bool numericOnly = false;
    if (allowOptionalStart && parts.size() == 1) {
      try {
        start = std::stoi(parts[0]);
        prefix.clear();
        numericOnly = true;
      } catch (...) {
      }
    }
    if (allowOptionalStart && !numericOnly && parts.size() > 1) {
      try {
        start = std::stoi(parts[1]);
      } catch (...) {
      }
    }
    renumberFocusedDeckCueNumbers(prefix, start);
  }

  void openInlineCueRenumberEditor(bool allowOptionalStart) {
    openInlineTextEditor("tool.renumber_cues",
                         "Renumber Cues",
                         allowOptionalStart
                           ? "Prefix and optional start (example: Q 100)"
                           : "Prefix (optional). Example: Q",
                         "",
                         [this, allowOptionalStart](const std::string& value) {
                           applyRenumberCueInput(value, allowOptionalStart);
                         });
  }

  void openInlineMasterSceneRenameEditor() {
    // Group preset functions removed — no-op
    triggerToast("master scene rename: removed");
  }

  void openInlineGotoCueEditor() {
    openInlineTextEditor("tool.goto_cue",
                         "GOTO Cue",
                         "Jump to cue number:",
                         "",
                         [this](const std::string& value) {
                           std::string token = trim(value);
                           if (!token.empty()) {
                             handleRemoteCommand("GOTO " + token);
                           }
                         });
  }

  void openInlineMidiPortEditor() {
    openInlineTextEditor("settings.midi_port",
                         "ALSA MIDI Port",
                         "e.g. 20:0 or client name",
                         midiDeviceName_,
                         [this](const std::string& value) {
                           midiDeviceName_ = trim(value);
                           if (midiEnabled_) {
                             stopMidiInput();
                             startMidiInput();
                           }
                         });
  }

  void openInlineCompanionPortEditor() {
    openInlineTextEditor("settings.companion_port",
                         "Companion/OSC Port",
                         "port number (default 5510)",
                         std::to_string(companionPort_),
                         [this](const std::string& value) {
                           try {
                             int p = std::stoi(trim(value));
                             if (p > 0 && p < 65536 && p != companionPort_) {
                               companionPort_ = p;
                               stopCompanionControl();
                               startCompanionControl();
                               triggerToast("companion port: " + std::to_string(companionPort_));
                             }
                           } catch (...) {
                             triggerToast("port: invalid");
                           }
                         });
  }

  void openInlineOscQueryPortEditor() {
    openInlineTextEditor("settings.osc_query_port",
                         "OSC Query HTTP Port",
                         "port number (default 5511)",
                         std::to_string(project_.oscQueryPort),
                         [this](const std::string& value) {
                           try {
                             int p = std::stoi(trim(value));
                             if (p > 0 && p < 65536) {
                               setOscQueryPort(p);
                             } else {
                               triggerToast("osc query port: invalid");
                             }
                           } catch (...) {
                             triggerToast("osc query port: invalid");
                           }
                         });
  }

  void openInlineOscFeedbackRateEditor() {
    openInlineTextEditor("settings.osc_feedback_rate",
                         "OSC Feedback Mirror Rate",
                         "milliseconds (40..2000)",
                         std::to_string(project_.oscFeedbackRateMs),
                         [this](const std::string& value) {
                           try {
                             setOscFeedbackRateMs(std::stoi(trim(value)));
                           } catch (...) {
                             triggerToast("osc feedback rate: invalid");
                           }
                         });
  }

  void openInlineArtNetPortEditor() {
    openInlineTextEditor("settings.artnet_port",
                         "Art-Net Port",
                         "port number (default 6454)",
                         std::to_string(project_.artNetPort),
                         [this](const std::string& value) {
                           try {
                             setArtNetPort(std::stoi(trim(value)));
                           } catch (...) {
                             triggerToast("art-net port: invalid");
                           }
                         });
  }

  void openInlineCanvasWidthEditor() {
    openInlineTextEditor("settings.canvas_width",
                         "Canvas Width",
                         "pixels",
                         std::to_string(project_.outputCanvasWidth),
                         [this](const std::string& value) {
                           try {
                             project_.outputCanvasWidth = std::clamp(std::stoi(trim(value)), 320, 7680);
                             markProjectDirty();
                           } catch (...) {
                             triggerToast("canvas width: invalid");
                           }
                         });
  }

  void openInlineCanvasHeightEditor() {
    openInlineTextEditor("settings.canvas_height",
                         "Canvas Height",
                         "pixels",
                         std::to_string(project_.outputCanvasHeight),
                         [this](const std::string& value) {
                           try {
                             project_.outputCanvasHeight = std::clamp(std::stoi(trim(value)), 240, 4320);
                             markProjectDirty();
                           } catch (...) {
                             triggerToast("canvas height: invalid");
                           }
                         });
  }

#ifdef _WIN32
  // Enumerate visible top-level window titles for the window-source picker.
  // Skips unowned invisible windows, empty titles, and Deckboy's own
  // windows (capturing yourself is feedback, not a source).
  std::vector<std::string> listCaptureWindowTitles() {
    std::vector<std::string> titles;
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
      auto* out = reinterpret_cast<std::vector<std::string>*>(lp);
      if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
      }
      int len = GetWindowTextLengthW(hwnd);
      if (len <= 0) {
        return TRUE;
      }
      std::wstring wide(static_cast<size_t>(len) + 1, L'\0');
      GetWindowTextW(hwnd, wide.data(), len + 1);
      wide.resize(static_cast<size_t>(len));
      int need = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), len, nullptr, 0, nullptr, nullptr);
      if (need <= 0) {
        return TRUE;
      }
      std::string title(static_cast<size_t>(need), '\0');
      WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), len, title.data(), need, nullptr, nullptr);
      if (!title.empty() && title.rfind("Deckboy", 0) != 0) {
        out->push_back(std::move(title));
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(&titles));
    return titles;
  }

  // Enumerate DirectShow video devices (webcams, HDMI capture sticks,
  // Blackmagic WDM, virtual cameras) by parsing ffmpeg's device listing.
  // Lines look like: [dshow @ 0x...] "HD WebCam" (video)
  std::vector<std::string> listDshowVideoDevices() {
    std::vector<std::string> devices;
    auto listingText = readAllText({
      "ffmpeg", "-hide_banner", "-list_devices", "true", "-f", "dshow", "-i", "dummy"});
    std::string listing = listingText ? *listingText : std::string();
    size_t pos = 0;
    while ((pos = listing.find("\" (video)", pos)) != std::string::npos) {
      size_t lineStart = listing.rfind('\n', pos);
      size_t open = listing.find('"', lineStart == std::string::npos ? 0 : lineStart);
      if (open != std::string::npos && open < pos) {
        std::string name = listing.substr(open + 1, pos - open - 1);
        if (!name.empty()) {
          devices.push_back(std::move(name));
        }
      }
      ++pos;
    }
    return devices;
  }
#endif

  void addSourceCue(CueKind kind, const std::string& rawRef) {
    if (!isSourceCueKind(kind)) {
      return;
    }
    std::string sourceRef = trim(rawRef);
#ifdef _WIN32
    // A camera cue with the placeholder ref opens the device picker —
    // webcams and capture devices are all DirectShow video devices, so one
    // picker covers both. The cue is created bound to a real device; TAKE
    // starts capture (dshow devices are exclusive-open, so we don't grab
    // the device just to preview it).
    // A window cue with the placeholder ref opens a window picker — the old
    // default silently captured the ENTIRE desktop, which read as "window
    // cues only partially work". gdigrab matches titles exactly, so apps
    // that retitle (browsers per tab) need re-picking after a title change.
    if (kind == CueKind::WindowSource) {
      std::string refLower = toLower(sourceRef);
      if (sourceRef.empty() || refLower == "active-window" || refLower == "desktop") {
        auto titles = listCaptureWindowTitles();
        std::vector<std::pair<std::string, std::string>> choices;
        choices.push_back({"region:0,0", "Entire Desktop"});
        for (const auto& title : titles) {
          choices.push_back({"title:" + title, title});
        }
        if (choices.size() == 1) {
          addSourceCue(kind, "region:0,0");
          return;
        }
        openDropdown("source.window_target", lastInlineEditorAnchorRect_, choices,
                     choices.front().first,
                     [this](const std::string& windowRef) {
          addSourceCue(CueKind::WindowSource, windowRef);
        });
        return;
      }
    }
    if (kind == CueKind::Camera) {
      std::string refLower = toLower(sourceRef);
      if (sourceRef.empty() || refLower == "default-camera" || refLower == "default") {
        auto devices = listDshowVideoDevices();
        if (devices.empty()) {
          triggerToast("no capture devices found (dshow)");
          return;
        }
        if (devices.size() == 1) {
          addSourceCue(kind, devices.front());
          return;
        }
        std::vector<std::pair<std::string, std::string>> choices;
        for (const auto& name : devices) {
          choices.push_back({name, name});
        }
        openDropdown("source.camera_device", lastInlineEditorAnchorRect_, choices,
                     devices.front(),
                     [this](const std::string& deviceName) {
          addSourceCue(CueKind::Camera, deviceName);
        });
        return;
      }
    }
#endif
    if (sourceRef.empty()) {
      if (kind == CueKind::WindowSource) {
        sourceRef = "active-window";
      } else if (kind == CueKind::Camera) {
        sourceRef = "default-camera";
      } else {
        sourceRef = "default-bus";
      }
    }
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
    Cue cue;
    cue.kind = kind;
    cue.path = "source://" + sourceCueTokenForKind(kind) + "/" + sourceRef;
    cue.name = cueKindLabel(kind) + " · " + sourceRef;
    cue.width = rasterW;
    cue.height = rasterH;
    cue.duration = 0.0;
    cue.stillDurationSeconds = 0.0;
    cue.hasAudio = (kind == CueKind::Camera);
    cue.formatName = "source";
    cue.videoCodec = sourceCueTokenForKind(kind);
    cue.audioCodec = cue.hasAudio ? "source" : "";
    cue.color = SDL_Color {139, 172, 15, 255};
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("source cue added");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  // Konami-code easter egg: adds a secret NATIVE Terrarium pattern cue —
  // the ecosystem sim runs in-process as a generator (pattern://terrarium,
  // v0.78.4), no companion exe or window capture involved. The pattern is
  // also available openly in the pattern picker; the egg keeps the ritual.
  // ↑↑↓↓←→←→BA + Start on the control window.
  void unlockTerrariumSource() {
    project_.terrariumUnlocked = true;  // persists with the save; picker lists it now
    addPatternCue("terrarium");
    Deck& deck = focusedDeckMutable();
    if (!deck.cues.empty()) {
      Cue& secret = deck.cues.back();
      secret.name = "TERRARIUM (secret)";
      secret.colorTag = "purple";
    }
    triggerToast("* KONAMI * terrarium unlocked");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  void addSourceCueFromMenu() {
    CueKind kind = sourceCueKindFromToken(sourceDefaultTypeId_);
    addSourceCue(kind, defaultSourceRefForKind(kind));
  }

  void openSourceTypeMenu() {
    contextMenuOpen_ = true;
    contextMenuDeckIdx_ = project_.focusedDeckIndex;
    contextMenuCueIdx_ = -1;
    contextItems_.clear();

    static const std::vector<std::pair<std::string, std::string>> kSourceTypes = {
      {"window",  "Window Source"},
      {"camera",  "Camera Source"},
      {"syphon",  "Syphon/Spout Source"},
    };

    for (const auto& [token, label] : kSourceTypes) {
      bool isDefault = (token == sourceDefaultTypeId_);
      contextItems_.push_back({
        (isDefault ? "* " : "  ") + label,
        {0, 0, 0, 0},
        [this, token]() {
          sourceDefaultTypeId_ = token;
          CueKind kind = sourceCueKindFromToken(token);
          addSourceCue(kind, defaultSourceRefForKind(kind));
        }
      });
    }

    contextItems_.push_back({
      "  Browser / URL Cue",
      {0, 0, 0, 0},
      [this]() { addBrowserCueFromPrompt(); }
    });
    contextItems_.push_back({
      "  Stream Cue (SRT / RTMP / RTSP)",
      {0, 0, 0, 0},
      [this]() { addSrtStreamCueFromPrompt(); }
    });
    contextItems_.push_back({
      "  NDI Source Cue",
      {0, 0, 0, 0},
      [this]() { addNdiSourceCueFromPrompt(); }
    });

    // Anchor menu above the SOURCE button (index 1 in buttons_)
    int winW = 0, winH = 0;
    SDL_GetWindowSize(controlWindow_, &winW, &winH);
    constexpr int kItemH = 32;
    // Size menu to fit the widest label
    int kMenuW = 212;
    if (fontSmall_) {
      for (const auto& item : contextItems_) {
        int tw = 0, th = 0;
        if (TTF_GetStringSize(fontSmall_, item.label.c_str(), 0, &tw, &th)) {
          kMenuW = std::max(kMenuW, tw + 36); // 18px left pad + 18px right pad
        }
      }
    }
    int menuH = static_cast<int>(contextItems_.size()) * kItemH + 8;
    int mx = 0, my = 0;
    if (buttons_.size() > 1 && buttons_[1].label == "SOURCE") {
      mx = buttons_[1].rect.x;
      my = buttons_[1].rect.y - menuH - 4;
    } else {
      mx = winW / 2 - kMenuW / 2;
      my = winH / 2 - menuH / 2;
    }
    mx = std::clamp(mx, 4, std::max(4, winW - kMenuW - 4));
    my = std::clamp(my, 4, std::max(4, winH - menuH - 4));
    contextMenuRect_ = {mx, my, kMenuW, menuH};
    int iy = my + 4;
    for (auto& item : contextItems_) {
      item.rect = {mx + 4, iy, kMenuW - 8, kItemH - 2};
      iy += kItemH;
    }
    uiWatchdogPopupEvent("context_menu", true, static_cast<int>(contextItems_.size()));
  }

  void addLowerThirdCue() {
    Cue cue;
    cue.kind = CueKind::LowerThird;
    cue.path = "graphic://lower-third";
    cue.name = "Lower Third";
    cue.lowerThirdText = "Lower Third Title";
    cue.lowerThirdSubtext = "";
    cue.lowerThirdBgAlpha = 180;
    cue.color = pal.deep;
    cue.formatName = "graphic";
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("lower third cue added");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  void addPipCue() {
    Cue cue;
    cue.kind = CueKind::Pip;
    cue.path = "graphic://pip";
    cue.name = "PIP";
    cue.formatName = "overlay";
    cue.color = SDL_Color {155, 188, 15, 255};
    cue.outputScaleX = 0.35f;
    cue.outputScaleY = 0.35f;
    cue.outputOffsetX = 360.0f;
    cue.outputOffsetY = -200.0f;
    cue.scaleMode = ScaleMode::Fit;
    cue.audioEnabled = false;
    cue.pauseOnLastFrame = true;
    cue.stillDurationSeconds = 0.0;
    cue.pipSourceType = "media";

    if (const Cue* sourceCue = selectedCuePtr()) {
      int selectedIndex = focusedDeck().selectedIndex;
      if (sourceCue->kind != CueKind::Pip && sourceCue->kind != CueKind::LowerThird &&
          sourceCue->kind != CueKind::Audio && cueCanBePipSource(*sourceCue)) {
        if (sourceCue->kind == CueKind::Browser) {
          applyPipSourceToCue(cue, "browser", sourceCue->path);
        } else if (isSourceCueKind(sourceCue->kind)) {
          applyPipSourceToCue(cue, sourceCueTokenForKind(sourceCue->kind), sourceCueRefFromCue(*sourceCue));
        } else if (sourceCue->kind == CueKind::Video || sourceCue->kind == CueKind::Image) {
          applyPipSourceToCue(cue, "media", resolvedCueFilesystemPathString(*sourceCue, currentProjectFile_));
        }
      }
    } else if (const Cue* sourceCue = activeCuePtr()) {
      if (sourceCue->kind != CueKind::Pip && sourceCue->kind != CueKind::LowerThird &&
          sourceCue->kind != CueKind::Audio && cueCanBePipSource(*sourceCue)) {
        if (sourceCue->kind == CueKind::Browser) {
          applyPipSourceToCue(cue, "browser", sourceCue->path);
        } else if (isSourceCueKind(sourceCue->kind)) {
          applyPipSourceToCue(cue, sourceCueTokenForKind(sourceCue->kind), sourceCueRefFromCue(*sourceCue));
        } else if (sourceCue->kind == CueKind::Video || sourceCue->kind == CueKind::Image) {
          applyPipSourceToCue(cue, "media", resolvedCueFilesystemPathString(*sourceCue, currentProjectFile_));
        }
      }
    }
    if (cue.path != "graphic://pip") {
      cue.name = "PIP · " + pipSourceTypeLabel(pipSourceTypeTokenFromCue(cue));
    }

    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    cue.outputScaleX = 0.35f;
    cue.outputScaleY = 0.35f;
    cue.outputOffsetX = 360.0f;
    cue.outputOffsetY = -200.0f;
    cue.audioEnabled = false;
    cue.pauseOnLastFrame = true;
    cue.stillDurationSeconds = 0.0;
    cue.formatName = "overlay";
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("pip cue added");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  void addCompositeCue() {
    Cue cue;
    cue.kind = CueKind::Composite;
    cue.path = "graphic://composite";
    cue.name = "Composite";
    cue.formatName = "scene";
    cue.color = SDL_Color {155, 188, 15, 255};
    cue.audioEnabled = false;
    cue.pauseOnLastFrame = true;
    cue.stillDurationSeconds = 0.0;
    cue.compositeBackgroundColor = SDL_Color {18, 24, 18, 255};
    applyCompositePresetToCue(cue, "2up");

    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    cue.audioEnabled = false;
    cue.pauseOnLastFrame = true;
    cue.stillDurationSeconds = 0.0;
    cue.formatName = "scene";
    cue.path = "graphic://composite";
    cue.compositeBackgroundColor = SDL_Color {18, 24, 18, 255};
    applyCompositePresetToCue(cue, cue.compositeLayoutPreset.empty() ? "2up" : cue.compositeLayoutPreset);
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("composite cue added");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  // Named pattern types and their pretty labels.
  static const std::vector<std::pair<std::string, std::string>>& patternBaseTypes() {
    // Operator-facing picker list. One pocket entry only — it cycles
    // day/sunset/night/storm itself; the forced-scene ids stay loadable as
    // legacy (see patternTypes). Motion belongs to the toggle, not the list.
    // Terrarium is deliberately absent: it's the Konami secret, listed only
    // once unlocked in the current save (see patternPickerTypes).
    static const std::vector<std::pair<std::string, std::string>> types {
      {"pocket-test",   "Pocket Test (test card + scene cycle)"},
      {"test-bars",    "Test Bars (motion diagnostics)"},
      {"test-clock",   "Test Clock (sync + latency)"},
      {"smpte-bars",   "SMPTE 75% Colour Bars"},
      {"crosshatch",   "Crosshatch"},
      {"checkerboard", "Checkerboard"},
      {"full-white",   "Full White"},
      {"full-black",   "Full Black"},
      {"full-red",     "Full Red"},
      {"full-green",   "Full Green"},
      {"full-blue",    "Full Blue"},
    };
    return types;
  }

  // What the operator actually sees in pickers: the base list, plus the
  // Terrarium secret once the Konami code has unlocked it in this save.
  std::vector<std::pair<std::string, std::string>> patternPickerTypes() const {
    std::vector<std::pair<std::string, std::string>> list = patternBaseTypes();
    if (project_.terrariumUnlocked) {
      list.insert(list.begin() + 1, {"terrarium", "Terrarium (living ecosystem)"});
      // The Pi panel's picture: same world, one pixel per cell.
      list.insert(list.begin() + 2, {"terrarium-pico", "Terrarium Pico (1px per cell)"});
    }
    return list;
  }

  // Full validation list: base types + legacy ids ("-motion" variants and
  // the forced-scene pocket types). Kept so old show files and remote
  // PATTERN commands stay valid — but pickers list patternBaseTypes() only.
  static const std::vector<std::pair<std::string, std::string>>& patternTypes() {
    static const std::vector<std::pair<std::string, std::string>> types = [] {
      std::vector<std::pair<std::string, std::string>> list = patternBaseTypes();
      // Always VALID (old saves, remote commands) even while picker-hidden.
      list.emplace_back("terrarium",      "Terrarium (living ecosystem)");
      list.emplace_back("terrarium-pico", "Terrarium Pico (1px per cell)");
      list.emplace_back("pocket-day",    "Pocket Test (day)");
      list.emplace_back("pocket-sunset", "Pocket Test (sunset)");
      list.emplace_back("pocket-night",  "Pocket Test (night)");
      list.emplace_back("pocket-storm",  "Pocket Test (storm)");
      list.emplace_back("smpte-bars-motion", "SMPTE 75% Colour Bars (motion)");
      list.emplace_back("crosshatch-motion", "Crosshatch (motion)");
      list.emplace_back("checkerboard-motion", "Checkerboard (motion)");
      list.emplace_back("full-white-motion", "Full White (motion)");
      list.emplace_back("full-black-motion", "Full Black (motion)");
      list.emplace_back("full-red-motion", "Full Red (motion)");
      list.emplace_back("full-green-motion", "Full Green (motion)");
      list.emplace_back("full-blue-motion", "Full Blue (motion)");
      return list;
    }();
    return types;
  }

  bool isKnownPatternType(const std::string& rawTypeId) const {
    std::string typeId = normalizePatternTypeId(rawTypeId);
    if (typeId == "checker") {
      typeId = "checkerboard";
    }
    const auto& types = patternTypes();
    return std::any_of(types.begin(), types.end(), [&](const auto& item) {
      return item.first == typeId;
    });
  }

  std::string patternLabelForType(const std::string& rawTypeId) const {
    std::string typeId = normalizePatternTypeId(rawTypeId);
    if (typeId == "checker") {
      typeId = "checkerboard";
    }
    const auto& types = patternTypes();
    for (const auto& [id, lbl] : types) {
      if (id == typeId) {
        return lbl;
      }
    }
    return typeId;
  }

  bool applyPatternTypeToSelectedCue(const std::string& rawTypeId, bool announce) {
    Deck& deck = focusedDeckMutable();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return false;
    }
    Cue& cue = deck.cues[deck.selectedIndex];
    if (cue.kind != CueKind::Pattern) {
      return false;
    }

    std::string typeId = normalizePatternTypeId(rawTypeId);
    if (typeId == "checker") {
      typeId = "checkerboard";
    }
    if (typeId.empty() || !isKnownPatternType(typeId)) {
      triggerToast("pattern: invalid");
      return false;
    }

    std::string previousType = normalizePatternTypeId(cue.path);
    std::string previousLabel = patternLabelForType(previousType);
    std::string nextLabel = patternLabelForType(typeId);
    cue.path = "pattern://" + typeId;
    if (cue.name.empty() || cue.name == previousLabel || cue.name == previousType) {
      cue.name = nextLabel;
    }
    patternDefaultTypeId_ = typeId;
    markProjectDirty();

    if (deck.activeIndex == deck.selectedIndex) {
      if (MediaEngine* engine = focusedMediaEngine()) {
        bool autoplay = engine->state() == TransportState::Playing;
        engine->loadCue(&cue, autoplay);
      }
    }
    if (announce) {
      triggerToast("pattern: " + nextLabel);
    }
    return true;
  }

  void cycleSelectedPatternType(int direction) {
    const Cue* cue = selectedCuePtr();
    if (!cue || cue->kind != CueKind::Pattern) {
      return;
    }
    std::string currentType = normalizePatternTypeId(cue->path);
    bool motion = endsWith(currentType, "-motion");
    std::string baseType = stripPatternMotionSuffix(currentType);
    const auto bases = patternPickerTypes();
    if (bases.empty()) {
      return;
    }
    int currentIndex = 0;
    for (int i = 0; i < static_cast<int>(bases.size()); ++i) {
      if (bases[i].first == baseType) {
        currentIndex = i;
        break;
      }
    }
    int step = direction < 0 ? -1 : 1;
    int nextIndex = (currentIndex + step + static_cast<int>(bases.size())) % static_cast<int>(bases.size());
    std::string nextType = bases[nextIndex].first;
    if (motion && patternTypeSupportsMotion(nextType)) {
      nextType += "-motion";
    }
    applyPatternTypeToSelectedCue(nextType, true);
  }

  void toggleSelectedPatternMotion() {
    const Cue* cue = selectedCuePtr();
    if (!cue || cue->kind != CueKind::Pattern) {
      return;
    }
    std::string currentType = normalizePatternTypeId(cue->path);
    std::string baseType = stripPatternMotionSuffix(currentType);
    if (!patternTypeSupportsMotion(baseType)) {
      triggerToast("pattern motion: n/a");
      return;
    }
    bool motion = endsWith(currentType, "-motion");
    applyPatternTypeToSelectedCue(motion ? baseType : (baseType + "-motion"), true);
  }

  void addPatternCue(const std::string& rawTypeId) {
    std::string typeId = normalizePatternTypeId(rawTypeId);
    if (typeId.empty()) {
      typeId = patternDefaultTypeId_;
    }
    if (typeId == "checker") {
      typeId = "checkerboard";
    }
    if (!isKnownPatternType(typeId)) {
      triggerToast("pattern: invalid");
      return;
    }
    patternDefaultTypeId_ = typeId;
    std::string label = patternLabelForType(typeId);
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
    Cue cue;
    cue.kind = CueKind::Pattern;
    cue.path = "pattern://" + typeId;
    cue.name = label;
    cue.width = rasterW;
    cue.height = rasterH;
    cue.color = {50, 50, 120, 255};
    cue.formatName = "generated";
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    // Engineering patterns should hold by default instead of inheriting
    // still-duration auto-advance from playlist defaults.
    cue.pauseOnLastFrame = true;
    cue.stillDurationSeconds = 0.0;
    // Pocket Test carries the A/V sync pop — it HAS audio (which also makes
    // the inspector's audio controls appear) and defaults audible; mute via
    // the cue's audio toggle.
    cue.hasAudio = (typeId == "pocket-test");
    cue.audioEnabled = (typeId == "pocket-test");
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("pattern: " + label);
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  void addPatternCueFromMenu() {
    addPatternCue(patternDefaultTypeId_);
  }

  void addKawaiiPatternCue() {
    addPatternCue(patternDefaultTypeId_);
  }

  void importPaths(const std::vector<std::string>& rawPaths) {
    int deckIndex = project_.focusedDeckIndex;
    Deck& deck = focusedDeckMutable();

    // Expand any dropped/imported folders into the acceptable media they
    // contain, recursively and in name order. Individual files are taken as-is.
    std::vector<fs::path> files;
    for (const auto& raw : rawPaths) {
      std::error_code ec;
      fs::path path = fs::absolute(trim(raw), ec);
      if (ec || !fs::exists(path, ec)) {
        continue;
      }
      if (fs::is_directory(path, ec)) {
        std::vector<fs::path> found;
        std::error_code itEc;
        for (fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, itEc), end;
             !itEc && it != end; it.increment(itEc)) {
          std::error_code fileEc;
          if (it->is_regular_file(fileEc) && isAcceptableMediaPath(it->path())) {
            found.push_back(it->path());
          }
        }
        std::sort(found.begin(), found.end());
        files.insert(files.end(), found.begin(), found.end());
      } else {
        files.push_back(path);
      }
    }

    bool changed = false;
    int addedCount = 0;
    for (const auto& path : files) {
      std::string pathStr = path.string();

      // Create placeholder cue (metadata filled in async)
      Cue placeholder;
      placeholder.path = pathStr;
      placeholder.name = path.stem().string();
      placeholder.kind = isImagePath(path) ? CueKind::Image
                       : isAudioPath(path) ? CueKind::Audio
                       : CueKind::Video;
      applyDeckDefaultsToCue(placeholder, deck);
      deck.cues.push_back(std::move(placeholder));
      changed = true;
      addedCount += 1;

      // Launch async probe
      PendingProbe pp;
      pp.deckIndex = deckIndex;
      pp.path = pathStr;
      pp.future = std::async(std::launch::async, [pathStr]() {
        return probeCue(fs::path(pathStr));
      });
      probeFutures_.push_back(std::move(pp));
    }

    if (!changed) {
      return;
    }

    if (deck.selectedIndex < 0 && !deck.cues.empty()) {
      deck.selectedIndex = 0;
      onSelectionChanged();
    }
    triggerToast(addedCount == 1 ? "1 cue imported (probing...)" : std::to_string(addedCount) + " cues imported (probing...)");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  // ── Built-in media converter ─────────────────────────────────────────────
  // Reason a video cue should be offered conversion (unplayable or heavy), or
  // nullopt if it's fine as-is.
  std::optional<std::string> cueConvertReason(const Cue& cue) const {
    if (cue.kind != CueKind::Video) {
      return std::nullopt;  // only file-backed video is a transcode candidate
    }
    if (unreadablePaths_.count(cue.path)) {
      return std::string("unreadable");
    }
    std::string c = cue.videoCodec;
    std::transform(c.begin(), c.end(), c.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (c.find("hevc") != std::string::npos || c.find("h265") != std::string::npos ||
        c.find("av1") != std::string::npos || c.find("prores") != std::string::npos ||
        c.find("dnxhd") != std::string::npos) {
      return std::string("heavy codec (" + cue.videoCodec + ")");
    }
    if (cue.width > 1920 || cue.height > 1080) {
      return std::string("large frame (" + std::to_string(cue.width) + "x" +
                         std::to_string(cue.height) + ")");
    }
    return std::nullopt;
  }

  fs::path convertedMediaDir() const {
    fs::path base = (!currentProjectFile_.empty() && currentProjectFile_.has_parent_path())
      ? currentProjectFile_.parent_path()   // portable: lives next to the show
      : Paths::dataDir();
    return base / "_converted";
  }

  bool isCueConverting(const std::string& path) const {
    for (const auto& job : conversionJobs_) {
      if (job.sourcePath == path) {
        return true;
      }
    }
    return false;
  }

  void convertCueMedia(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    Cue& cue = deck.cues[cueIndex];
    if (cue.path.empty() || cue.kind != CueKind::Video || isCueConverting(cue.path)) {
      return;
    }
    std::error_code ec;
    fs::path src(cue.path);
    fs::path outDir = convertedMediaDir();
    fs::create_directories(outDir, ec);
    fs::path dst = outDir / (src.stem().string() + ".mp4");
    if (fs::exists(dst, ec) && fs::equivalent(dst, src, ec)) {
      dst = outDir / (src.stem().string() + "_conv.mp4");
    }
    std::string srcStr = src.string();
    std::string dstStr = dst.string();
    ConversionJob job;
    job.deckIndex = deckIndex;
    job.sourcePath = cue.path;
    job.destPath = dstStr;
    job.future = std::async(std::launch::async, [srcStr, dstStr]() -> bool {
      auto produced = [&]() {
        std::error_code e;
        return fs::exists(dstStr, e) && fs::file_size(dstStr, e) > 1024;
      };
      // GPU (NVENC) first with a CPU decode (robust on odd inputs); fall back
      // to libx264 if NVENC is unavailable or produced nothing.
      readAllText({"ffmpeg", "-y", "-i", srcStr, "-c:v", "h264_nvenc",
                   "-preset", "p4", "-cq", "23", "-pix_fmt", "yuv420p",
                   "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", dstStr});
      if (produced()) {
        return true;
      }
      readAllText({"ffmpeg", "-y", "-i", srcStr, "-c:v", "libx264",
                   "-preset", "veryfast", "-crf", "20", "-pix_fmt", "yuv420p",
                   "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", dstStr});
      return produced();
    });
    conversionJobs_.push_back(std::move(job));
    triggerToast("converting " + src.filename().string() + " ...");
    playUiSound(UiSoundEffect::Import);
  }

  // Convert every flagged cue across all decks (used by the Encoder tab).
  int convertAllFlaggedCues() {
    int started = 0;
    for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
      Deck& deck = project_.decks[d];
      for (int c = 0; c < static_cast<int>(deck.cues.size()); ++c) {
        if (cueConvertReason(deck.cues[c]) && !isCueConverting(deck.cues[c].path)) {
          convertCueMedia(d, c);
          ++started;
        }
      }
    }
    if (started == 0) {
      triggerToast("encoder: nothing to convert");
    }
    return started;
  }

  void convertSelectedCueMedia() {
    Deck& deck = focusedDeckMutable();
    auto indices = selectedCueIndices(deck);
    int started = 0;
    for (int idx : indices) {
      if (idx >= 0 && idx < static_cast<int>(deck.cues.size()) &&
          deck.cues[idx].kind == CueKind::Video && !deck.cues[idx].path.empty() &&
          !isCueConverting(deck.cues[idx].path)) {
        convertCueMedia(project_.focusedDeckIndex, idx);
        ++started;
      }
    }
    if (started == 0) {
      triggerToast("convert: select a file cue");
    }
  }

  void toggleOutputFullscreen() {
    if (project_.focusedOutputIndex >= 0 &&
        project_.focusedOutputIndex < static_cast<int>(project_.outputs.size())) {
      const OutputTarget& output = project_.outputs[project_.focusedOutputIndex];
      if (normalizeOutputType(output.outputType) == "stream") {
        triggerToast("fullscreen: stream output");
        return;
      }
      if (!output.enabled) {
        setFocusedOutputEnabled(true);
        playUiSound(UiSoundEffect::Toggle);
        return;  // setFocusedOutputEnabled already handles fullscreen
      }
    }
    OutputRuntime* runtime = runtimeForOutput(project_.focusedOutputIndex);
    if (!runtime || !runtime->outputWindow) {
      return;
    }
    SDL_WindowFlags flags = SDL_GetWindowFlags(runtime->outputWindow);
    bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    if (fullscreen) {
      SDL_SetWindowFullscreen(runtime->outputWindow, false);
      runtime->recoveryPausedByEscape = true;
      runtime->fullscreenIntended = false;
    } else {
      runtime->recoveryPausedByEscape = false;
      runtime->fullscreenIntended = true;
      if (enableOutputFullscreen(project_.focusedOutputIndex, true)) {
        SDL_ShowWindow(runtime->outputWindow);
        SDL_RaiseWindow(runtime->outputWindow);
      } else {
        runtime->fullscreenIntended = false;
        triggerToast("fullscreen failed");
      }
    }
    playUiSound(UiSoundEffect::Toggle);
  }

  std::optional<int> outputIndexForWindowId(Uint32 windowId) const {
    if (windowId == 0) {
      return std::nullopt;
    }
    for (int outputIndex = 0; outputIndex < static_cast<int>(outputRuntimes_.size()); ++outputIndex) {
      const OutputRuntime* runtime = runtimeForOutput(outputIndex);
      if (!runtime || !runtime->outputWindow) {
        continue;
      }
      if (SDL_GetWindowID(runtime->outputWindow) == windowId) {
        return outputIndex;
      }
    }
    return std::nullopt;
  }

  bool escapeOutputFullscreen(Uint32 sourceWindowId) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return false;
    }

    int targetOutputIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    std::optional<int> sourceOutputIndex = outputIndexForWindowId(sourceWindowId);
    if (sourceOutputIndex) {
      targetOutputIndex = *sourceOutputIndex;
    }

    auto tryExitFullscreen = [&](int outputIndex) -> bool {
      if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
        return false;
      }
      OutputTarget& output = project_.outputs[outputIndex];
      if (!output.enabled || normalizeOutputType(output.outputType) != "window") {
        return false;
      }
      OutputRuntime* runtime = runtimeForOutput(outputIndex);
      if (!runtime || !runtime->outputWindow) {
        return false;
      }
      SDL_WindowFlags flags = SDL_GetWindowFlags(runtime->outputWindow);
      bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
      if (!fullscreen) {
        return false;
      }
      SDL_SetWindowFullscreen(runtime->outputWindow, false);
      setOutputRecoveryPausedByEscape(outputIndex, true);
      runtime->fullscreenIntended = false;
      SDL_ShowWindow(runtime->outputWindow);
      SDL_RaiseWindow(controlWindow_);
      triggerToast("escape: " + outputLabel(outputIndex) + " windowed");
      playUiSound(UiSoundEffect::Toggle);
      project_.focusedOutputIndex = outputIndex;
      return true;
    };

    if (tryExitFullscreen(targetOutputIndex)) {
      return true;
    }

    if (sourceOutputIndex &&
        *sourceOutputIndex >= 0 &&
        *sourceOutputIndex < static_cast<int>(project_.outputs.size())) {
      OutputTarget& sourceOutput = project_.outputs[*sourceOutputIndex];
      if (sourceOutput.enabled && normalizeOutputType(sourceOutput.outputType) == "window") {
        if (OutputRuntime* runtime = runtimeForOutput(*sourceOutputIndex); runtime && runtime->outputWindow) {
          // Esc from an output window should remain an output-safety action,
          // not fall through to app quit confirmation.
          setOutputRecoveryPausedByEscape(*sourceOutputIndex, true);
          SDL_ShowWindow(runtime->outputWindow);
          SDL_RaiseWindow(controlWindow_);
          project_.focusedOutputIndex = *sourceOutputIndex;
          return true;
        }
      }
    }

    int controlDisplay = deckboyGetWindowDisplayIndex(controlWindow_);
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (!project_.outputs[outputIndex].enabled ||
          normalizeOutputType(project_.outputs[outputIndex].outputType) != "window") {
        continue;
      }
      OutputRuntime* runtime = runtimeForOutput(outputIndex);
      if (!runtime || !runtime->outputWindow) {
        continue;
      }
      SDL_WindowFlags flags = SDL_GetWindowFlags(runtime->outputWindow);
      bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
      if (!fullscreen) {
        continue;
      }
      int outputDisplay = deckboyGetWindowDisplayIndex(runtime->outputWindow);
      if (controlDisplay >= 0 && outputDisplay >= 0 && outputDisplay != controlDisplay) {
        continue;
      }
      if (tryExitFullscreen(outputIndex)) {
        return true;
      }
    }

    return false;
  }

  void layoutButtons(int windowWidth, int windowHeight) {
    buttons_.clear();
    bottomBarRect_ = SDL_Rect {};
    mediaGroupRect_ = SDL_Rect {};
    transportGroupRect_ = SDL_Rect {};
    outputGroupRect_ = SDL_Rect {};
    sourceDefaultDropdownRect_ = SDL_Rect {};
    patternDefaultDropdownRect_ = SDL_Rect {};

    int barX = snapDownToGrid(kLayoutPanelPadding);
    int barW = snapDownToGrid(std::max(540, windowWidth - kLayoutPanelPadding * 2));
    int barH = kLayoutBottomBarHeight;
    int barY = snapDownToGrid(std::max(kLayoutSpacingUnit, windowHeight - barH - kLayoutPanelPadding));
    bottomBarRect_ = {barX, barY, barW, barH};

    SDL_Rect groupBounds {barX + kLayoutSpacingUnit, barY + 8, barW - kLayoutSpacingUnit * 2, barH - 16};
    GridLayout groups(groupBounds, 3, 1, kLayoutPanelGap);
    mediaGroupRect_ = groups.cell(0, 0);
    transportGroupRect_ = groups.cell(1, 0);
    outputGroupRect_ = groups.cell(2, 0);

    int buttonY = barY + 46;
    int buttonH = kLayoutButtonHeight;
    int buttonW = std::clamp((std::min(mediaGroupRect_.w, transportGroupRect_.w) - (kLayoutButtonGap * 4)) / 3, 96, 144);

    auto push = [&](std::string label, SDL_Color fill, std::string tip = "") {
      Button button;
      button.label = std::move(label);
      button.tip   = std::move(tip);
      button.fill = fill;
      button.outline = pal.deep;
      button.text = pal.deep;
      buttons_.push_back(button);
    };
    push("IMPORT",     pal.mid, "I — import media files");
    push("SOURCE",     pal.mid, "Add a source cue and refine it in the cue inspector");
    push("PATTERN",    pal.mid, "Add a pattern cue and refine it in the cue inspector");
    push("TAKE",       pal.light, "Enter — take selected cue live");
    push("STOP",       pal.mid, "S — stop active cue");
    push("RERACK",     pal.mid, "R — rewind to start");
    // BLACKOUT sits with CLEAR because they are the two "kill the picture"
    // actions and an operator is choosing between them under pressure. It had
    // no button at all before — reachable only from Companion — which is
    // backwards: it is the FASTEST and most REVERSIBLE of the family (picture
    // gone, playback untouched, one press to restore), so it should be the
    // easiest to hit, not the hardest.
    //
    // The tips spell out what each one leaves behind, because that is the
    // whole decision and the labels hide it.
    push("BLACKOUT",   pal.mid, "B — picture off instantly, playback keeps running (reversible)");
    push("CLEAR",      pal.mid, "C — fade out, drop overlays, stop playback");
    push("SETTINGS",   pal.mid, "Open settings");

    auto placeGroupButtons = [&](int startIndex, int count, const SDL_Rect& groupRect, int overrideW = 0) {
      int bw = overrideW > 0 ? overrideW : buttonW;
      int totalW = count * bw + (count - 1) * kLayoutButtonGap;
      int x = groupRect.x + std::max(kLayoutSpacingUnit, (groupRect.w - totalW) / 2);
      int y = groupRect.y + 36;
      for (int i = 0; i < count; ++i) {
        buttons_[startIndex + i].rect = {x, y, bw, buttonH};
        x += bw + kLayoutButtonGap;
      }
    };
    if (buttons_.size() == 9) {
      placeGroupButtons(0, 3, mediaGroupRect_);
      placeGroupButtons(3, 3, transportGroupRect_);
      // OUTPUT now holds three: BLACKOUT, CLEAR, SETTINGS.
      int outBtnW = std::min(buttonW + 12, (outputGroupRect_.w - kLayoutButtonGap * 4) / 3);
      placeGroupButtons(6, 3, outputGroupRect_, outBtnW);
    }
  }

  const Cue* selectedCuePtr(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return nullptr;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.selectedIndex];
  }

  const Cue* selectedCuePtr() const {
    const Deck& deck = focusedDeck();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.selectedIndex];
  }

  const Cue* activeCuePtr(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return nullptr;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.activeIndex];
  }

  const Cue* activeCuePtr() const {
    const Deck& deck = focusedDeck();
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.activeIndex];
  }

  const Cue* previewCuePtr(int deckIndex, int* cueIndexOut = nullptr) const {
    if (cueIndexOut) {
      *cueIndexOut = -1;
    }
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return nullptr;
    }
    const Deck& deck = project_.decks[deckIndex];
    const Cue* selectedCue = selectedCuePtr(deckIndex);
    const Cue* activeCue = activeCuePtr(deckIndex);
    if (selectedCue && selectedCue != activeCue &&
        deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
      if (cueIndexOut) {
        *cueIndexOut = deck.selectedIndex;
      }
      return selectedCue;
    }
    int nextIndex = nextCueIndexForDeck(deckIndex);
    if (nextIndex >= 0 && nextIndex < static_cast<int>(deck.cues.size())) {
      if (cueIndexOut) {
        *cueIndexOut = nextIndex;
      }
      return &deck.cues[nextIndex];
    }
    return nullptr;
  }

  bool cueSupportsMonitorPreview(const Cue& cue) const {
    return cue.kind == CueKind::Video ||
           cue.kind == CueKind::Image ||
           cue.kind == CueKind::Pattern;
  }

  double cueTimelineDurationSeconds(const Cue& cue, const MediaEngine* engine = nullptr) const {
    if (cue.kind == CueKind::Video || cue.kind == CueKind::Audio) {
      return std::max(0.0, cue.duration);
    }
    if (cue.stillDurationSeconds > 0.0) {
      return cue.stillDurationSeconds;
    }
    return engine ? std::max(0.0, engine->duration()) : 0.0;
  }

  double cueAbsolutePlayheadSeconds(const Cue& cue, const MediaEngine& engine) const {
    double cueDuration = cueTimelineDurationSeconds(cue, &engine);
    if (cue.kind == CueKind::Video || cue.kind == CueKind::Audio) {
      double cueIn = std::clamp(cue.inPointSeconds, 0.0, cueDuration);
      double cueOut = cue.outPointSeconds > 0.0
        ? std::clamp(cue.outPointSeconds, cueIn, cueDuration)
        : cueDuration;
      return std::clamp(cueIn + engine.position(), cueIn, cueOut);
    }
    return std::clamp(engine.position(), 0.0, cueDuration);
  }

  void refreshFocusedLiveCueRuntimeIfSelected() {
    if (project_.focusedDeckIndex < 0 || project_.focusedDeckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Deck& deck = focusedDeck();
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    if (!cueIndexSelected(deck, deck.activeIndex)) {
      return;
    }
    if (MediaEngine* engine = focusedMediaEngine()) {
      engine->refreshActiveCueRuntime(&deck.cues[deck.activeIndex]);
    }
  }

  // Push the app's current active cue into each deck engine's owned snapshot.
  // The engine never keeps pointers into Deck::cues (import reallocates,
  // delete shifts), so after any project edit the snapshot must be refreshed
  // for live-editable fields (fades, still duration) to stay current.
  // Triggered from markProjectDirty via engineCueSyncPending_ and drained
  // once per update() tick.
  void syncEngineCueSnapshots() {
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      Deck& deck = project_.decks[deckIndex];
      if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      if (MediaEngine* engine = mediaEngineForDeck(deckIndex)) {
        engine->syncActiveCueSnapshot(deck.cues[deck.activeIndex]);
      }
    }
  }

  bool isAnyOutputFullscreen() const {
    for (int i = 0; i < static_cast<int>(project_.outputs.size()); ++i) {
      // A disabled output can still be sitting on a stale fullscreen window
      // (New Show / relaunch disarms outputs without exiting the window). Don't
      // let that latch the fullscreen button — reflect only live outputs, so a
      // single click re-arms fullscreen instead of needing an off/on toggle.
      if (!project_.outputs[i].enabled) {
        continue;
      }
      const OutputRuntime* runtime = runtimeForOutput(i);
      if (runtime && runtime->outputWindow) {
        SDL_WindowFlags flags = SDL_GetWindowFlags(runtime->outputWindow);
        if (flags & SDL_WINDOW_FULLSCREEN) {
          return true;
        }
      }
    }
    return false;
  }

  Cue* activeCueMutable() {
    Deck& deck = focusedDeckMutable();
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.activeIndex];
  }

  void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_Color color, int x, int y) {
    if (!font || text.empty()) {
      return;
    }
    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), 0, color);
    if (!surface) {
      return;
    }
    SDL_Texture* texture = deckboyCreateTextureFromSurface(renderer, surface);
    if (!texture) {
      SDL_DestroySurface(surface);
      return;
    }
    SDL_Rect dst {x, y, surface->w, surface->h};
    SDL_DestroySurface(surface);
    SDL_RenderTexture(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
  }

  void drawTextSafe(SDL_Renderer* renderer, TTF_Font* font, const SDL_Rect& rect,
                    const std::string& text, SDL_Color color) {
    if (!font || text.empty() || rect.w <= 0 || rect.h <= 0) {
      return;
    }
    SDL_Rect safe = safeTextRect(rect);
    if (safe.w <= 0 || safe.h <= 0) {
      return;
    }
    // Panels now paint the caller's rect verbatim, so the label's usable width
    // is simply the inset rect — no grid-snapped right edge to compensate for.
    std::string clipped = ellipsizeToPixelWidth(font, text, safe.w);
    if (clipped.empty()) {
      return;
    }
    int textW = 0;
    int textH = 0;
    if (!TTF_GetStringSize(font, clipped.c_str(), 0, &textW, &textH)) {
      return;
    }
    // Always vertically center on the rect's midline. When the text is
    // taller than the rect (tight h<=18 rects drawn with fontBase_, or
    // retina-scaled fonts), this yields symmetric overflow above and
    // below instead of top-aligning inside a clip that chops descenders.
    int textY = rect.y + (rect.h - textH) / 2;
    bool hadClip = SDL_RenderClipEnabled(renderer) == true;
    SDL_Rect previousClip {};
    if (hadClip) {
      SDL_GetRenderClipRect(renderer, &previousClip);
    }
    int clipTop = std::min(rect.y, textY);
    int clipBottom = std::max(rect.y + rect.h, textY + textH);
    SDL_Rect textClip { rect.x, clipTop, rect.w, std::max(0, clipBottom - clipTop) };
    if (hadClip) {
      SDL_Rect intersect {};
      if (!SDL_GetRectIntersection(&previousClip, &textClip, &intersect)) {
        return;
      }
      textClip = intersect;
    }
    SDL_SetRenderClipRect(renderer, &textClip);
    drawText(renderer, font, clipped, color, safe.x, textY);
    SDL_SetRenderClipRect(renderer, hadClip ? &previousClip : nullptr);
  }

  void drawCenteredTextSafe(SDL_Renderer* renderer, TTF_Font* font, const SDL_Rect& rect,
                            const std::string& text, SDL_Color color) {
    if (!font || text.empty() || rect.w <= 0 || rect.h <= 0) {
      return;
    }
    SDL_Rect safe = safeTextRect(rect);
    if (safe.w <= 0 || safe.h <= 0) {
      return;
    }
    std::string clipped = ellipsizeToPixelWidth(font, text, safe.w);
    if (clipped.empty()) {
      return;
    }
    int textW = 0, textH = 0;
    if (!TTF_GetStringSize(font, clipped.c_str(), 0, &textW, &textH)) {
      return;
    }
    // Center X within the safe (inset) width; center Y on the original
    // rect's midline (not the snapped rect) so text with textH > rect.h
    // overflows symmetrically rather than top-aligning into a clip.
    int textX = safe.x + (safe.w - textW) / 2;
    int textY = rect.y + (rect.h - textH) / 2;
    bool hadClip = SDL_RenderClipEnabled(renderer) == true;
    SDL_Rect previousClip {};
    if (hadClip) {
      SDL_GetRenderClipRect(renderer, &previousClip);
    }
    int clipTop = std::min(rect.y, textY);
    int clipBottom = std::max(rect.y + rect.h, textY + textH);
    SDL_Rect textClip { rect.x, clipTop, rect.w, std::max(0, clipBottom - clipTop) };
    if (hadClip) {
      SDL_Rect intersect {};
      if (!SDL_GetRectIntersection(&previousClip, &textClip, &intersect)) {
        return;
      }
      textClip = intersect;
    }
    SDL_SetRenderClipRect(renderer, &textClip);
    drawText(renderer, font, clipped, color, textX, textY);
    SDL_SetRenderClipRect(renderer, hadClip ? &previousClip : nullptr);
  }

  // Centre a label in its container. Kept as a distinct name because ~60 call
  // sites read better with the argument order, but it is no longer a separate
  // behaviour: it used to centre on snapRectToGrid(rect) with no ellipsizing
  // and no clip, so a button labelled with this sat up to a grid unit away from
  // an identical neighbour labelled with drawCenteredTextSafe, and long labels
  // spilled out of their pill instead of truncating. Both are now impossible.
  void drawCenteredText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_Color color, const SDL_Rect& rect) {
    drawCenteredTextSafe(renderer, font, rect, text, color);
  }

  void drawCenteredTextUnclipped(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_Color color, const SDL_Rect& rect) {
    if (!font || text.empty()) {
      return;
    }
    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), 0, color);
    if (!surface) {
      return;
    }
    SDL_Texture* texture = deckboyCreateTextureFromSurface(renderer, surface);
    if (!texture) {
      SDL_DestroySurface(surface);
      return;
    }
    SDL_Rect dst {
      rect.x + (rect.w - surface->w) / 2,
      rect.y + (rect.h - surface->h) / 2,
      surface->w,
      surface->h
    };
    SDL_DestroySurface(surface);
    SDL_RenderTexture(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
  }

  // -----------------------------------------------------------------------
  // Shared inspector draw helpers (used by both docked and floating paths)
  // -----------------------------------------------------------------------
