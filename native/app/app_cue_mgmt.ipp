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

  // alreadyConfirmed: the caller has ITSELF obtained a deliberate confirmation,
  // so skip the press-again guard. The guard exists to stop a stray Delete
  // keystroke wiping what is on air, and "press delete again" only makes sense
  // for a key you can press twice. From a right-click menu the menu has already
  // closed by then, so the user would have to right-click and re-pick inside a
  // 2.5 s window — impractical, and it read as the delete being broken.
  // A context menu says which cue it will delete and requires an explicit pick;
  // that IS the confirmation, so the label carries the warning instead.
  bool requestDeleteCueIndices(int deckIndex, std::vector<int> indices,
                               bool alreadyConfirmed = false) {
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

    if (cueIndicesIncludeLiveCue(deck, indices) && !alreadyConfirmed) {
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

  // ── Native file dialogs (SDL3) ──────────────────────────────────────────
  // SDL_ShowOpenFileDialog and friends deliver their result through a C
  // callback. It fires on the main thread during event pumping, but we do NOT
  // act on it inline — a picked file kicks off importing or project loading,
  // and running that re-entrantly inside SDL's event pump is asking for
  // trouble. Instead the callback marshals a closure into sdlDialogActions_,
  // which drainSdlDialogActions() (update loop) runs at a safe point.
  //
  // filelist == nullptr means the dialog errored; a non-null list whose first
  // entry is nullptr means the user cancelled. Both collapse to an empty vector
  // here, and every result handler treats "no files" as "do nothing".
  struct SdlDialogRequest {
    App* app = nullptr;
    std::function<void(std::vector<std::string>)> onResult;
  };

  static void sdlDialogTrampoline(void* userdata, const char* const* filelist, int /*filter*/) {
    std::unique_ptr<SdlDialogRequest> req(static_cast<SdlDialogRequest*>(userdata));
    std::vector<std::string> files;
    if (filelist) {
      for (int i = 0; filelist[i] != nullptr; ++i) {
        if (filelist[i][0] != '\0') {
          files.emplace_back(filelist[i]);
        }
      }
    }
    App* app = req->app;
    auto cb = std::move(req->onResult);
    {
      std::lock_guard<std::mutex> lock(app->sdlDialogMutex_);
      app->sdlDialogActions_.emplace_back(
        [cb = std::move(cb), files = std::move(files)]() mutable { cb(files); });
    }
    // Released outside the queue lock; the flag has its own atomicity. Cleared
    // LAST so a fresh dialog cannot be opened until this result is fully queued.
    app->sdlDialogOpen_.store(false, std::memory_order_release);
  }

  // Run every queued dialog result. Main thread, called from the update loop.
  void drainSdlDialogActions() {
    std::vector<std::function<void()>> actions;
    {
      std::lock_guard<std::mutex> lock(sdlDialogMutex_);
      if (sdlDialogActions_.empty()) {
        return;
      }
      actions.swap(sdlDialogActions_);
    }
    for (auto& action : actions) {
      action();
    }
  }

  // IMPORTANT for all three helpers: `filters` MUST outlive the async call —
  // SDL's docs require the filter array stay valid "until the callback is
  // invoked", which can be much later (the dialog is modal to the user, not to
  // us). Every caller passes a `static const` filter list for exactly this
  // reason; never pass a local/temporary. The dialog is main-thread-only to
  // open; the result callback may land on another thread, which is why
  // sdlDialogOpen_ is atomic and results are marshalled through a locked queue.
  void showOpenFileDialog(const std::vector<SDL_DialogFileFilter>& filters, bool allowMany,
                          std::function<void(std::vector<std::string>)> onResult) {
    if (sdlDialogOpen_.load(std::memory_order_acquire)) {
      return;  // one native dialog at a time; a second click is a no-op
    }
    sdlDialogOpen_.store(true, std::memory_order_release);
    auto* req = new SdlDialogRequest{this, std::move(onResult)};
    SDL_ShowOpenFileDialog(&App::sdlDialogTrampoline, req, controlWindow_,
                           filters.empty() ? nullptr : filters.data(),
                           static_cast<int>(filters.size()), nullptr, allowMany);
  }

  void showSaveFileDialog(const std::vector<SDL_DialogFileFilter>& filters,
                          std::function<void(std::vector<std::string>)> onResult) {
    if (sdlDialogOpen_.load(std::memory_order_acquire)) {
      return;
    }
    sdlDialogOpen_.store(true, std::memory_order_release);
    auto* req = new SdlDialogRequest{this, std::move(onResult)};
    SDL_ShowSaveFileDialog(&App::sdlDialogTrampoline, req, controlWindow_,
                           filters.empty() ? nullptr : filters.data(),
                           static_cast<int>(filters.size()), nullptr);
  }

  void showFolderDialog(std::function<void(std::vector<std::string>)> onResult) {
    if (sdlDialogOpen_.load(std::memory_order_acquire)) {
      return;
    }
    sdlDialogOpen_.store(true, std::memory_order_release);
    auto* req = new SdlDialogRequest{this, std::move(onResult)};
    SDL_ShowOpenFolderDialog(&App::sdlDialogTrampoline, req, controlWindow_, nullptr, false);
  }

  void importWithPicker() {
    pushUndoSnapshot();
    // The media filter is a convenience, not a restriction — "All files" stays
    // available in the native dialog, and the previous osascript picker had no
    // filter at all, so nothing the operator could pick before is blocked now.
    static const std::vector<SDL_DialogFileFilter> kMediaFilters = {
      {"Media files", "mp4;mov;m4v;mkv;avi;webm;mpg;mpeg;m2v;ts;wmv;flv;"
                      "png;jpg;jpeg;bmp;gif;tif;tiff;webp;avif;heic;heif;"
                      "wav;mp3;aac;m4a;flac;ogg;aiff"},
      {"All files", "*"},
    };
    showOpenFileDialog(kMediaFilters, /*allowMany=*/true,
                       [this](std::vector<std::string> files) {
                         if (!files.empty()) {
                           importPaths(files);
                         }
                       });
  }


  void openProjectFromPath(const fs::path& projectPath) {
    showLog("SHOW-OPEN", projectPath.filename().string());
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

  // One shared filter for the .deckboy project/playlist pickers.
  static const std::vector<SDL_DialogFileFilter>& deckboyProjectFilters() {
    static const std::vector<SDL_DialogFileFilter> filters = {
      {"Deckboy playlist", "deckboy"},
      {"All files", "*"},
    };
    return filters;
  }

  void openProjectFromPicker() {
    showOpenFileDialog(deckboyProjectFilters(), /*allowMany=*/false,
                       [this](std::vector<std::string> files) {
                         if (!files.empty()) {
                           openProjectFromPath(normalizeProjectPath(fs::path(files[0])));
                         }
                       });
  }

  void saveProjectAsFromPicker() {
    showSaveFileDialog(deckboyProjectFilters(),
                       [this](std::vector<std::string> files) {
                         if (files.empty()) {
                           return;
                         }
                         fs::path chosen = normalizeProjectPath(fs::path(files[0]));
                         // The native save dialog does not force an extension;
                         // add .deckboy if the operator did not type one, so a
                         // saved show is always openable by the .deckboy filter.
                         if (chosen.extension() != ".deckboy") {
                           chosen += ".deckboy";
                         }
                         currentProjectFile_ = chosen;
                         saveProjectNow(true);
                       });
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

    // Only the capture sources whose backend works on this platform (same
    // catalog-driven list the inspector's type dropdown uses). On macOS every
    // capture backend is a scaffold, so this list is empty there and the menu
    // falls through to the stream/NDI entries below rather than offering three
    // cues that do nothing.
    for (const auto& [token, label] : sourceCueTypeChoices()) {
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

    // Browser cues run on Windows (WebView2/Edge) and Linux (Xvfb+Chromium) but
    // the macOS backend is a scaffold — do not offer what cannot run.
#ifndef __APPLE__
    contextItems_.push_back({
      "  Browser / URL",
      {0, 0, 0, 0},
      [this]() { addBrowserCueFromPrompt(); }
    });
#endif
    contextItems_.push_back({
      "  Stream (SRT / RTMP / RTSP)",
      {0, 0, 0, 0},
      [this]() { addSrtStreamCueFromPrompt(); }
    });
    contextItems_.push_back({
      "  NDI",
      {0, 0, 0, 0},
      [this]() { addNdiSourceCueFromPrompt(); }
    });
    // Stage timer. Without this the Timer cue was only reachable over the wire
    // (TIMERCUE), i.e. not reachable at all from inside the app.
    contextItems_.push_back({
      "  Stage Timer",
      {0, 0, 0, 0},
      [this]() { addTimerCue(300); }
    });
    // Test tone. The audio counterpart of a test pattern: line-up tone, pink
    // noise for ringing out a PA, and a channel walk for proving which output
    // feeds which speaker.
    contextItems_.push_back({
      "  Test Tone",
      {0, 0, 0, 0},
      [this]() { addToneCue(); }
    });
    // Video synth: oscillators, mirrors and feedback, after Atari Video Music
    // and Sleepy Circuits Hypno.
    contextItems_.push_back({
      "  Video Synth",
      {0, 0, 0, 0},
      [this]() { addVideoSynthCue(); }
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

  // Stage/speaker timer. Held by default: a countdown that auto-advanced to the
  // next cue the moment it hit zero would be actively dangerous on a show.
  static const char* toneWaveformLabel(ToneWaveform w) {
    switch (w) {
      case ToneWaveform::Pink:     return "pink noise";
      case ToneWaveform::White:    return "white noise";
      case ToneWaveform::Sweep:    return "sweep";
      case ToneWaveform::Identify: return "identify";
      case ToneWaveform::Fds:      return "FDS synth";
      default:                     return "sine";
    }
  }

  Cue* selectedToneCueMutable() {
    Cue* cue = selectedCueMutable();
    return (cue && cue->kind == CueKind::Tone) ? cue : nullptr;
  }

  static const char* toneVisualLabel(ToneVisual v) {
    switch (v) {
      case ToneVisual::Scope:     return "scope";
      case ToneVisual::Lissajous: return "lissajous";
      case ToneVisual::Spectrum:  return "spectrum";
      default:                    return "off";
    }
  }

  static const char* fdsCarrierLabel(FdsCarrier c) {
    switch (c) {
      case FdsCarrier::Triangle: return "triangle";
      case FdsCarrier::Pulse25:  return "pulse 25%";
      case FdsCarrier::Saw:      return "saw";
      case FdsCarrier::Additive: return "additive";
      default:                   return "sine";
    }
  }

  static const char* fdsModulatorLabel(FdsModulator m) {
    switch (m) {
      case FdsModulator::Ramp:    return "ramp";
      case FdsModulator::Square:  return "square";
      case FdsModulator::Vibrato: return "vibrato";
      case FdsModulator::Growl:   return "growl";
      default:                    return "off";
    }
  }

  // Note names rather than raw Hz. A musician thinks in notes, and a synth
  // whose pitch control reads "233 Hz" is a test generator wearing a hat.
  static std::string fdsNoteName(double hz) {
    if (hz <= 0.0) return "-";
    const double semis = 12.0 * std::log2(hz / 440.0);
    const int n = static_cast<int>(std::lround(semis)) + 57;   // A4 = index 57
    static const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B"};
    const int octave = n / 12;
    const int step = ((n % 12) + 12) % 12;
    return std::string(kNames[step]) + std::to_string(octave);
  }

  static const char* synthChipLabel(SynthChip c) {
    return c == SynthChip::Nes ? "2A03 (NES)" : "FDS";
  }
  static const char* nesVoiceLabel(NesVoice v) {
    switch (v) {
      case NesVoice::Triangle: return "triangle";
      case NesVoice::Noise:    return "noise";
      default:                 return "pulse";
    }
  }
  static const char* nesDutyLabel(NesDuty d) {
    switch (d) {
      case NesDuty::Eighth:       return "12.5%";
      case NesDuty::Quarter:      return "25%";
      case NesDuty::ThreeQuarter: return "75%";
      default:                    return "50%";
    }
  }

  void cycleSynthChip() {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    cue->tone.synth.chip = (cue->tone.synth.chip == SynthChip::Fds)
      ? SynthChip::Nes : SynthChip::Fds;
    markProjectDirty();
    triggerToast(std::string("chip: ") + synthChipLabel(cue->tone.synth.chip));
    playUiSound(UiSoundEffect::Toggle);
  }

  void cycleNesVoice() {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    cue->tone.synth.nesVoice = static_cast<NesVoice>(
      (static_cast<int>(cue->tone.synth.nesVoice) + 1) % 3);
    markProjectDirty();
    triggerToast(std::string("voice: ") + nesVoiceLabel(cue->tone.synth.nesVoice));
    playUiSound(UiSoundEffect::Toggle);
  }

  void cycleNesDuty() {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    cue->tone.synth.nesDuty = static_cast<NesDuty>(
      (static_cast<int>(cue->tone.synth.nesDuty) + 1) % 4);
    markProjectDirty();
    triggerToast(std::string("duty: ") + nesDutyLabel(cue->tone.synth.nesDuty));
    playUiSound(UiSoundEffect::Toggle);
  }

  void adjustSynthEnv(bool attack, double delta) {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    if (attack) {
      cue->tone.synth.attackSeconds =
        std::clamp(cue->tone.synth.attackSeconds + delta, 0.0, 2.0);
      triggerToast("attack " + fmtFloat(cue->tone.synth.attackSeconds, 2) + "s");
    } else {
      cue->tone.synth.releaseSeconds =
        std::clamp(cue->tone.synth.releaseSeconds + delta, 0.01, 4.0);
      triggerToast("release " + fmtFloat(cue->tone.synth.releaseSeconds, 2) + "s");
    }
    markProjectDirty();
  }

  void cycleFdsCarrier() {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    cue->tone.synth.carrier = static_cast<FdsCarrier>(
      (static_cast<int>(cue->tone.synth.carrier) + 1) % 5);
    markProjectDirty();
    triggerToast(std::string("carrier: ") + fdsCarrierLabel(cue->tone.synth.carrier));
    playUiSound(UiSoundEffect::Toggle);
  }

  void cycleFdsModulator() {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    cue->tone.synth.modulator = static_cast<FdsModulator>(
      (static_cast<int>(cue->tone.synth.modulator) + 1) % 5);
    markProjectDirty();
    triggerToast(std::string("modulator: ") + fdsModulatorLabel(cue->tone.synth.modulator));
    playUiSound(UiSoundEffect::Toggle);
  }

  void adjustFdsDepth(int delta) {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    cue->tone.synth.modDepth = std::clamp(cue->tone.synth.modDepth + delta, 0, 63);
    markProjectDirty();
    triggerToast("depth " + std::to_string(cue->tone.synth.modDepth));
  }

  // Ratio steps through MUSICAL intervals, not linear increments: the
  // interesting settings are the simple ratios, and a linear sweep walks past
  // all of them.
  void adjustFdsRatio(int direction) {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    static const double kRatios[] = {0.0, 0.125, 0.25, 0.5, 0.75, 1.0,
                                     1.5, 2.0, 3.0, 4.0};
    const int n = static_cast<int>(sizeof(kRatios) / sizeof(kRatios[0]));
    int i = 0;
    for (; i < n; ++i) {
      if (std::abs(kRatios[i] - cue->tone.synth.modRatio) < 0.001) break;
    }
    i = std::clamp((i >= n ? 3 : i) + direction, 0, n - 1);
    cue->tone.synth.modRatio = kRatios[i];
    markProjectDirty();
    triggerToast("ratio " + fmtFloat(cue->tone.synth.modRatio, 3));
  }

  void adjustFdsNote(int semitones) {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    const double hz = cue->tone.synth.noteHz * std::pow(2.0, semitones / 12.0);
    cue->tone.synth.noteHz = std::clamp(hz, 20.0, 8000.0);
    markProjectDirty();
    triggerToast("note " + fdsNoteName(cue->tone.synth.noteHz));
  }

  void adjustFdsRetrigger(double delta) {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    double v = cue->tone.synth.retriggerSeconds + delta;
    if (v < 0.05) v = 0.0;   // below the smallest step means HOLD
    cue->tone.synth.retriggerSeconds = std::clamp(v, 0.0, 4.0);
    markProjectDirty();
    triggerToast(cue->tone.synth.retriggerSeconds <= 0.0
      ? "retrigger: hold"
      : "retrigger " + fmtFloat(cue->tone.synth.retriggerSeconds, 2) + "s");
  }

  void cycleToneVisual() {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    cue->tone.visual = static_cast<ToneVisual>(
      (static_cast<int>(cue->tone.visual) + 1) % 4);
    markProjectDirty();
    triggerToast(std::string("display: ") + toneVisualLabel(cue->tone.visual));
    playUiSound(UiSoundEffect::Toggle);
  }

  void cycleToneWaveform() {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    const int next = (static_cast<int>(cue->tone.waveform) + 1) % 6;
    cue->tone.waveform = static_cast<ToneWaveform>(next);
    markProjectDirty();
    triggerToast(std::string("tone: ") + toneWaveformLabel(cue->tone.waveform));
    playUiSound(UiSoundEffect::Toggle);
  }

  // Frequency steps in THIRD-OCTAVES rather than a fixed number of Hz. 100Hz
  // steps are uselessly fine at the bottom and uselessly coarse at the top;
  // the ear works in ratios, so the control should too.
  void adjustToneFrequency(int direction) {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    const double factor = std::pow(2.0, 1.0 / 3.0);
    double hz = cue->tone.frequencyHz * (direction > 0 ? factor : 1.0 / factor);
    cue->tone.frequencyHz = std::clamp(hz, 20.0, 20000.0);
    markProjectDirty();
    triggerToast(std::to_string(static_cast<int>(cue->tone.frequencyHz)) + " Hz");
  }

  void adjustToneLevel(double deltaDb) {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    // Never reaches 0 dBFS: this feeds a live PA and a generated signal has no
    // reason to be at full scale.
    cue->tone.levelDbfs = std::clamp(cue->tone.levelDbfs + deltaDb, -60.0, -1.0);
    markProjectDirty();
    triggerToast(fmtFloat(cue->tone.levelDbfs, 1) + " dBFS");
  }

  void adjustToneChannel(int direction) {
    Cue* cue = selectedToneCueMutable();
    if (!cue) return;
    // -1 is a real value meaning ALL, so the range starts one below zero.
    cue->tone.channel = std::clamp(cue->tone.channel + direction, -1, 15);
    markProjectDirty();
    triggerToast(cue->tone.channel < 0
      ? "tone: all channels"
      : "tone: channel " + std::to_string(cue->tone.channel + 1));
  }

  static const char* vsShapeLabel(VideoSynthShape s) {
    switch (s) {
      case VideoSynthShape::Diamond: return "diamond";
      case VideoSynthShape::Rings:   return "rings";
      case VideoSynthShape::Grid:    return "grid";
      case VideoSynthShape::Moire:   return "moire";
      default:                       return "plasma";
    }
  }
  static const char* vsMirrorLabel(VideoSynthMirror m) {
    switch (m) {
      case VideoSynthMirror::Horizontal: return "horizontal";
      case VideoSynthMirror::Quad:       return "quad";
      case VideoSynthMirror::Kaleido:    return "kaleido";
      default:                           return "none";
    }
  }
  static const char* vsPaletteLabel(VideoSynthPalette p) {
    switch (p) {
      case VideoSynthPalette::Amber: return "amber";
      case VideoSynthPalette::Ice:   return "ice";
      case VideoSynthPalette::Fire:  return "fire";
      case VideoSynthPalette::Mono:  return "mono";
      default:                       return "spectrum";
    }
  }

  Cue* selectedVideoSynthCueMutable() {
    Cue* cue = selectedCueMutable();
    return (cue && cue->kind == CueKind::VideoSynth) ? cue : nullptr;
  }

  void cycleVsEnum(int which) {
    Cue* cue = selectedVideoSynthCueMutable();
    if (!cue) return;
    VideoSynthSettings& v = cue->videoSynth;
    if (which == 0) {
      v.shape = static_cast<VideoSynthShape>((static_cast<int>(v.shape) + 1) % 5);
      triggerToast(std::string("shape: ") + vsShapeLabel(v.shape));
    } else if (which == 1) {
      v.mirror = static_cast<VideoSynthMirror>((static_cast<int>(v.mirror) + 1) % 4);
      triggerToast(std::string("mirror: ") + vsMirrorLabel(v.mirror));
    } else {
      v.palette = static_cast<VideoSynthPalette>((static_cast<int>(v.palette) + 1) % 5);
      triggerToast(std::string("palette: ") + vsPaletteLabel(v.palette));
    }
    markProjectDirty();
    playUiSound(UiSoundEffect::Toggle);
  }

  void adjustVs(double VideoSynthSettings::*field, double delta,
                double lo, double hi, const char* label) {
    Cue* cue = selectedVideoSynthCueMutable();
    if (!cue) return;
    double& v = cue->videoSynth.*field;
    v = std::clamp(v + delta, lo, hi);
    markProjectDirty();
    triggerToast(std::string(label) + " " + fmtFloat(v, 2));
  }

  void addVideoSynthCue() {
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
    Cue cue;
    cue.kind = CueKind::VideoSynth;
    cue.path = "vsynth://";
    cue.name = "Video Synth";
    cue.width = rasterW;
    cue.height = rasterH;
    cue.color = {90, 50, 120, 255};
    cue.formatName = "generated";
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    // Runs until stopped, like the other generated sources.
    cue.pauseOnLastFrame = true;
    cue.stillDurationSeconds = 0.0;
    cue.endAction = CueEndAction::Stop;
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("video synth added");
    playUiSound(UiSoundEffect::Import);
  }

  void addToneCue() {
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
    Cue cue;
    cue.kind = CueKind::Tone;
    cue.path = "tone://";
    cue.name = "Test Tone 1kHz";
    cue.width = rasterW;
    cue.height = rasterH;
    cue.color = {60, 110, 120, 255};
    cue.formatName = "generated";
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    // A line-up tone runs until it is STOPPED. Auto-advancing off the end of
    // the playlist part way through a sound check would be actively unhelpful.
    cue.pauseOnLastFrame = true;
    cue.stillDurationSeconds = 0.0;
    cue.endAction = CueEndAction::Stop;
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("tone cue added");
    playUiSound(UiSoundEffect::Import);
  }

  void addTimerCue(int durationSeconds = 300) {
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
    Cue cue;
    cue.kind = CueKind::Timer;
    cue.path = "timer://";
    cue.timer.durationSeconds = std::max(1, durationSeconds);
    const int mins = cue.timer.durationSeconds / 60;
    const int secs = cue.timer.durationSeconds % 60;
    char label[64];
    std::snprintf(label, sizeof(label), "Timer %d:%02d", mins, secs);
    cue.name = label;
    cue.width = rasterW;
    cue.height = rasterH;
    cue.color = {120, 60, 50, 255};
    cue.formatName = "generated";
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    cue.pauseOnLastFrame = true;
    cue.stillDurationSeconds = 0.0;
    cue.endAction = CueEndAction::Stop;
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast(std::string("timer: ") + cue.name);
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
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
    // An explicit destination wins. Without one the output stays beside the
    // show so it travels with it, falling back to stateDir() when no show is
    // open -- never inside a macOS bundle, which the signature seals.
    if (!encoderOverrides_.outputDir.empty()) {
      return fs::path(encoderOverrides_.outputDir);
    }
    fs::path base = (!currentProjectFile_.empty() && currentProjectFile_.has_parent_path())
      ? currentProjectFile_.parent_path()
      : Paths::stateDir();
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

  // Cancel one job. A running job is asked to stop (the worker kills ffmpeg
  // and removes the half-written file); a queued one is simply dropped, since
  // nothing has been started for it yet.
  // Move a QUEUED job earlier or later. A running job cannot be reordered --
  // it is already the one encoding -- so it acts as a floor.
  void moveConversionJob(std::size_t index, int delta) {
    if (index >= conversionJobs_.size()) {
      return;
    }
    const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(index) + delta;
    if (target < 0 || target >= static_cast<std::ptrdiff_t>(conversionJobs_.size())) {
      return;
    }
    if (conversionJobs_[index].state == ConversionState::Running ||
        conversionJobs_[static_cast<std::size_t>(target)].state == ConversionState::Running) {
      triggerToast("cannot reorder a running job");
      return;
    }
    std::swap(conversionJobs_[index], conversionJobs_[static_cast<std::size_t>(target)]);
    playUiSound(UiSoundEffect::Navigate);
  }

  // Park or release a single job. A running job is cancelled back to Queued so
  // it can be resumed later; the partial output is removed by the worker.
  void toggleConversionHold(std::size_t index) {
    if (index >= conversionJobs_.size()) {
      return;
    }
    ConversionJob& job = conversionJobs_[index];
    if (job.state == ConversionState::Running) {
      if (job.cancel) job.cancel->store(true);
      job.held = true;
      triggerToast("holding " + job.label + " (restarts from the top)");
      return;
    }
    job.held = !job.held;
    triggerToast(job.held ? ("held " + job.label) : ("released " + job.label));
    playUiSound(UiSoundEffect::Toggle);
    pumpConversionQueue();
  }

  void cancelConversionAt(std::size_t index) {
    if (index >= conversionJobs_.size()) {
      return;
    }
    ConversionJob& job = conversionJobs_[index];
    if (job.state == ConversionState::Running) {
      if (job.cancel) job.cancel->store(true);
      triggerToast("cancelling " + job.label);
      return;   // the update poll retires it once the worker returns
    }
    triggerToast("removed " + job.label);
    conversionJobs_.erase(conversionJobs_.begin() + static_cast<std::ptrdiff_t>(index));
  }

  void cancelAllConversions() {
    int running = 0;
    for (auto& job : conversionJobs_) {
      if (job.state == ConversionState::Running) {
        if (job.cancel) job.cancel->store(true);
        ++running;
      }
    }
    // Drop everything not yet started; running jobs retire themselves.
    conversionJobs_.erase(
      std::remove_if(conversionJobs_.begin(), conversionJobs_.end(),
                     [](const ConversionJob& j) {
                       return j.state != ConversionState::Running;
                     }),
      conversionJobs_.end());
    triggerToast(running > 0 ? "encoder: cancelling, queue cleared"
                             : "encoder: queue cleared");
    playUiSound(UiSoundEffect::Clear);
  }

  void toggleEncoderQueuePaused() {
    encoderQueuePaused_ = !encoderQueuePaused_;
    triggerToast(encoderQueuePaused_ ? "encoder queue paused"
                                     : "encoder queue running");
    playUiSound(UiSoundEffect::Toggle);
    pumpConversionQueue();
  }

  // Build the ffmpeg args for a catalog format. Quality and stream handling are
  // per-codec because there is no universal knob: x264/x265 take -crf, NVENC
  // takes -cq, ProRes and DNxHD take profiles, VP9/AV1 want -b:v 0 alongside
  // -crf, and the still/audio formats have to explicitly drop the stream they
  // do not carry or ffmpeg errors out.
  // Which rate-control knob a codec actually accepts. nullptr means the codec
  // is PROFILE-driven (ProRes, DNxHR, HAP, FFV1, QT RLE, PNG) and has no
  // continuous quality axis, so a quality/bitrate override cannot be honoured
  // and must be reported rather than silently dropped.
  static const char* rateFlagForFormat(const std::string& id) {
    if (id == "h264" || id == "h265" || id == "proxy" || id == "datamosh" ||
        id == "vp9" || id == "av1") return "-crf";
    if (id == "h264_gpu" || id == "h265_gpu") return "-cq";
    if (id == "mpeg4" || id == "mjpeg" || id == "datamosh_classic" ||
        id == "datamosh_extreme") return "-qscale:v";
    return nullptr;
  }

  // Map the operator's 0..100 intent onto the codec's own scale. Every one of
  // these is inverted (lower number = better) and they do not share a range,
  // which is exactly why the UI shows an intent rather than a raw number.
  static int mapQualityForFlag(const std::string& id, const char* flag, int q) {
    q = std::clamp(q, 0, 100);
    auto lerp = [q](int worst, int best) {
      return worst + (best - worst) * q / 100;
    };
    if (std::string(flag) == "-qscale:v") return lerp(31, 2);   // MPEG-4 / MJPEG
    if (id == "vp9" || id == "av1")       return lerp(50, 10);  // wider CRF scale
    return lerp(34, 10);                                        // x264/x265/NVENC
  }

  // Drop a flag and its value wherever it appears, so an override replaces the
  // format default instead of sitting next to it. Two rate knobs on one command
  // line is undefined at best and an ffmpeg error at worst.
  static void stripArg(std::vector<std::string>& a, const std::string& flag) {
    for (std::size_t i = 0; i + 1 < a.size();) {
      if (a[i] == flag) a.erase(a.begin() + i, a.begin() + i + 2);
      else ++i;
    }
  }

  static std::vector<std::string> encodeArgsForFormat(const EncoderFormat& fmt,
                                                      const std::string& src,
                                                      const std::string& dst,
                                                      const EncoderOverrides& ov) {
    std::vector<std::string> a {"ffmpeg", "-y", "-i", src};
    const std::string id = fmt.id;
    auto add = [&](std::initializer_list<const char*> more) {
      for (const char* s : more) a.push_back(s);
    };

    if (!fmt.encoder || !*fmt.encoder) {
      add({"-vn"});                       // audio-only target
    } else {
      a.push_back("-c:v");
      a.push_back(fmt.encoder);
      if (id == "h264" || id == "h265")            add({"-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p"});
      else if (id == "proxy")                      add({"-preset", "veryfast", "-crf", "26", "-vf", "scale=-2:720", "-pix_fmt", "yuv420p"});
      else if (id == "h264_gpu" || id == "h265_gpu") add({"-preset", "p4", "-cq", "21", "-pix_fmt", "yuv420p"});
      else if (id == "prores")                     add({"-profile:v", "3", "-pix_fmt", "yuv422p10le"});
      else if (id == "prores4444")                 add({"-profile:v", "4444", "-pix_fmt", "yuva444p10le"});
      else if (id == "dnxhr")                      add({"-profile:v", "dnxhr_hq", "-pix_fmt", "yuv422p"});
      else if (id == "qtrle")                      add({"-pix_fmt", "argb"});
      else if (id == "vp9")                        add({"-crf", "30", "-b:v", "0", "-pix_fmt", "yuv420p"});
      else if (id == "av1")                        add({"-crf", "30", "-b:v", "0", "-cpu-used", "6", "-pix_fmt", "yuv420p"});
      else if (id == "ffv1")                       add({"-level", "3", "-pix_fmt", "yuv422p"});
      else if (id == "mpeg4")                      add({"-qscale:v", "3", "-bf", "0", "-g", "120", "-pix_fmt", "yuv420p"});
      else if (id == "mjpeg")                      add({"-qscale:v", "3", "-pix_fmt", "yuvj420p"});
      else if (id == "gif")                        add({"-vf", "fps=12,scale=480:-1:flags=lanczos", "-loop", "0"});
      else if (id == "datamosh")
        // Regular keyframes stay in the FILE for seeking; the decoder drops
        // them at playback. -vsync cfr keeps this frame-for-frame swappable
        // with the original.
        add({"-preset", "medium", "-crf", "18", "-bf", "0", "-sc_threshold", "0",
             "-refs", "1", "-g", "120", "-pix_fmt", "yuv420p", "-vsync", "cfr"});
      else if (id == "datamosh_classic")
        // MPEG-4 ASP has no deblocking to soften the blocks, which is the
        // whole point of this variant.
        add({"-qscale:v", "3", "-bf", "0", "-sc_threshold", "0", "-g", "120",
             "-pix_fmt", "yuv420p", "-vsync", "cfr"});
      else if (id == "datamosh_extreme")
        // Two knobs over CLASSIC, both of which the operator can SEE:
        //   -g 24    one keyframe per ~second instead of per five, so a fresh
        //            smear starts constantly rather than occasionally. This is
        //            the setting that makes the effect feel continuous; a long
        //            GOP is why the gentler recipes look like nothing is
        //            happening for seconds at a time.
        //   qscale 8 a coarse quantiser, so residuals are large and blocky and
        //            the drag reads as hard 16x16 chunks.
        // Do NOT push -g higher for "more" effect: the decoder keeps the first
        // keyframe and drops the rest, so a file with only one keyframe has
        // nothing to drop and does not mosh at all.
        add({"-qscale:v", "8", "-bf", "0", "-sc_threshold", "0", "-g", "24",
             "-pix_fmt", "yuv420p", "-vsync", "cfr"});
      else if (id == "hap")                        add({"-format", "hap", "-chunks", "4"});
      else if (id == "hap_alpha")                  add({"-format", "hap_alpha", "-chunks", "4"});
      else if (id == "hap_q")                      add({"-format", "hap_q", "-chunks", "4"});
      else if (id == "png_seq")                    add({"-pix_fmt", "rgba"});

      // ---- operator overrides --------------------------------------------
      // Applied AFTER the defaults so an untouched encoder produces byte-for-
      // byte the same command line it always did.
      if (const char* rateFlag = rateFlagForFormat(id)) {
        if (ov.rate == EncoderOverrides::Rate::Quality) {
          for (const char* f : {"-crf", "-cq", "-qscale:v", "-b:v"}) stripArg(a, f);
          a.push_back(rateFlag);
          a.push_back(std::to_string(mapQualityForFlag(id, rateFlag, ov.quality0to100)));
          // VP9/AV1 read -crf as a CAP unless the bitrate is explicitly zeroed,
          // so constant quality needs both.
          if (id == "vp9" || id == "av1") add({"-b:v", "0"});
        } else if (ov.rate == EncoderOverrides::Rate::Bitrate) {
          for (const char* f : {"-crf", "-cq", "-qscale:v", "-b:v"}) stripArg(a, f);
          a.push_back("-b:v");
          a.push_back(std::to_string(std::max(1, ov.videoBitrateKbps)) + "k");
        }
      }
      if (ov.fps > 0.0) {
        a.push_back("-r");
        a.push_back(fmtFloat(ov.fps, 3));
      }
      if (ov.width > 0 && ov.height > 0) {
        // Merge with any filter the format already set (proxy scales, gif builds
        // a palette) rather than replacing it -- a second -vf silently wins and
        // would drop the format's own filtering.
        std::string scale = "scale=" + std::to_string(ov.width) + ":" +
                            std::to_string(ov.height) + ":flags=lanczos";
        std::string existing;
        for (std::size_t i = 0; i + 1 < a.size(); ++i) {
          if (a[i] == "-vf") { existing = a[i + 1]; break; }
        }
        stripArg(a, "-vf");
        a.push_back("-vf");
        a.push_back(existing.empty() ? scale : (scale + "," + existing));
      }
    }

    if (!fmt.audioEncoder || !*fmt.audioEncoder) {
      add({"-an"});                       // GIF / image sequence carry no audio
    } else {
      a.push_back("-c:a");
      a.push_back(fmt.audioEncoder);
      const std::string ac = fmt.audioEncoder;
      if (ac == "aac" || ac == "libmp3lame" || ac == "libopus") {
        const int kbps = ov.audioBitrateKbps > 0 ? ov.audioBitrateKbps : 192;
        a.push_back("-b:a");
        a.push_back(std::to_string(kbps) + "k");
      }
    }

    if (id == "png_seq") {
      // image2 needs a numbered pattern, not a single filename.
      fs::path stem = fs::path(dst).parent_path() / fs::path(dst).stem();
      a.push_back(stem.string() + "_%05d.png");
      return a;
    }
    if (std::string(fmt.container) == "mp4" || std::string(fmt.container) == "mov") {
      add({"-movflags", "+faststart"});
    }
    a.push_back(dst);
    return a;
  }

  // Which encoders this ffmpeg actually has. Probed once, lazily, off the
  // Encoder tab rather than at boot - it costs an ffmpeg spawn and most
  // sessions never open the tab.
  const std::set<std::string>& availableEncoders() {
    if (!encoderProbeDone_) {
      encoderProbeDone_ = true;
      if (auto out = readAllText({"ffmpeg", "-hide_banner", "-encoders"})) {
        for (const std::string& raw : splitLines(*out)) {
          // Rows look like " V....D libx264   H.264 ..." - the flags column,
          // then the encoder name. Anything without at least two tokens is a
          // header line.
          auto parts = splitWhitespace(raw);
          if (parts.size() < 2) continue;
          const std::string& flags = parts[0];
          if (flags.empty() || (flags[0] != 'V' && flags[0] != 'A')) continue;
          encoderProbeResult_.insert(parts[1]);
        }
      }
    }
    return encoderProbeResult_;
  }

  // A format is usable when every encoder it names is present. An empty name
  // means "no stream of that kind" (audio-only, or GIF/PNG which carry none).
  bool encoderFormatAvailable(const EncoderFormat& fmt) {
    const auto& have = availableEncoders();
    auto ok = [&](const char* name) {
      return !name || !*name || have.count(name) > 0;
    };
    return ok(fmt.encoder) && ok(fmt.audioEncoder);
  }

  // The format matrix. Order is roughly "most likely to least".
  static const std::vector<EncoderFormat>& encoderFormatCatalog() {
    static const std::vector<EncoderFormat> kFormats {
      {"h264", "H.264 / MP4", "libx264", "aac", "mp4", "universal delivery", false},
      {"proxy", "Proxy 720p", "libx264", "aac", "mp4", "fast, for scrubbing", false},
      {"h265", "H.265 / MP4", "libx265", "aac", "mp4", "half the size, fussier playback", false},
      {"h264_gpu", "H.264 (NVENC)", "h264_nvenc", "aac", "mp4", "fast GPU encode", false},
      {"h265_gpu", "H.265 (NVENC)", "hevc_nvenc", "aac", "mp4", "fast GPU encode", false},
      {"prores", "ProRes 422 / MOV", "prores_ks", "pcm_s16le", "mov", "edit + interchange master", false},
      {"prores4444", "ProRes 4444 / MOV", "prores_ks", "pcm_s16le", "mov", "master WITH alpha", true},
      {"dnxhr", "DNxHR / MXF", "dnxhd", "pcm_s16le", "mxf", "Avid + broadcast delivery", false},
      {"qtrle", "QuickTime RLE / MOV", "qtrle", "pcm_s16le", "mov", "lossless alpha, big files", true},
      {"vp9", "VP9 / WebM", "libvpx-vp9", "libopus", "webm", "web delivery", false},
      {"av1", "AV1 / MKV", "libaom-av1", "libopus", "matroska", "smallest, slow to encode", false},
      {"ffv1", "FFV1 / MKV", "ffv1", "flac", "matroska", "lossless archival", false},
      // Three datamosh flavours, weakest to strongest. All are prepared for
      // I-frame dropping at decode; see kDatamoshLook* in types.hpp for why
      // they differ, which is measured rather than stylistic.
      {"datamosh", "Datamosh - subtle (H.264)", "libx264", "aac", "mp4", "gentler; self-heals fast", false},
      {"datamosh_classic", "Datamosh - classic (MPEG-4)", "mpeg4", "libmp3lame", "avi", "the full smeary effect", false},
      {"datamosh_extreme", "Datamosh - extreme (MPEG-4)", "mpeg4", "libmp3lame", "avi", "chunkiest, smears constantly", false},
      {"mpeg4", "MPEG-4 Part 2 / AVI", "mpeg4", "libmp3lame", "avi", "the classic datamosh look", false},
      {"mjpeg", "Motion JPEG / AVI", "mjpeg", "pcm_s16le", "avi", "every frame a keyframe", false},
      {"gif", "Animated GIF", "gif", "", "gif", "no audio, palette limited", false},
      {"png_seq", "PNG sequence", "png", "", "image2", "frames as stills, alpha", true},
      // HAP: GPU-native DXT/BC texture video, the live/VJ standard. ffmpeg
      // DECODES it in most builds but only ENCODES with --enable-libsnappy, so
      // these usually probe as unavailable - which is exactly why availability
      // is probed rather than assumed.
      // WARNING: playing HAP through the ordinary decode path decompresses DXT
      // to RGB on the CPU and is SLOWER than H.264, for much larger files. Do
      // not advertise HAP as fast until the GPU path in
      // docs/HAP_PLAYBACK_PLAN.md exists.
      {"hap", "HAP / MOV", "hap", "pcm_s16le", "mov", "GPU-native, many layers", false},
      {"hap_alpha", "HAP Alpha / MOV", "hap", "pcm_s16le", "mov", "GPU-native WITH alpha", true},
      {"hap_q", "HAP Q / MOV", "hap", "pcm_s16le", "mov", "GPU-native, higher quality", false},
      {"wav", "WAV (audio only)", "", "pcm_s24le", "wav", "audio stem, 24-bit", false},
      {"mp3", "MP3 (audio only)", "", "libmp3lame", "mp3", "audio stem, compressed", false},
    };
    return kFormats;
  }

  // Format id for a per-cue look. Clamps, because the value is serialized and
  // an older or newer show file may carry something outside the range.
  static const char* moshFormatIdForLook(int look) {
    switch (look) {
      case kDatamoshLookSubtle:  return "datamosh";
      case kDatamoshLookExtreme: return "datamosh_extreme";
      default:                   return "datamosh_classic";
    }
  }

  // ---- Encoder override controls -------------------------------------------
  // Does this ffmpeg have the HAP encoder? Probed, not assumed -- suggesting a
  // conversion the machine cannot perform is worse than staying quiet.
  bool encoderHasHapSupport() {
    for (const EncoderFormat& fmt : encoderFormatCatalog()) {
      if (std::string(fmt.id) == "hap") return encoderFormatAvailable(fmt);
    }
    return false;
  }

  // Queue every listed cue for HAP conversion. Uses the datamosh-style prep so
  // the ORIGINAL survives: a show mid-run must not lose its media because a
  // conversion was accepted, and HAP files are large enough that an operator
  // may well want to undo the decision.
  void convertCuesToHap(const std::vector<std::pair<int, int>>& targets) {
    const EncoderPreset savedPreset = encoderPreset_;
    const std::string savedFormat = encoderFormatId_;
    encoderFormatId_ = "hap";
    int queued = 0;
    for (const auto& [deckIndex, cueIndex] : targets) {
      convertCueMedia(deckIndex, cueIndex);
      ++queued;
    }
    encoderPreset_ = savedPreset;
    encoderFormatId_ = savedFormat;
    triggerToast("queued " + std::to_string(queued) + " HAP conversion(s)");
    showLog("HAP CONVERT", std::to_string(queued) + " cue(s) queued");
  }

  // ---- ASIO playback -------------------------------------------------------
  // Arming replaces the SDL sink on every deck. Disarming restores it. Both
  // are safe mid-show: MediaEngine takes its audio lock, so a swap cannot race
  // a write already in flight.
  bool asioArmed() const { return asioOutput_ && asioOutput_->running(); }

  void disarmAsioOutput(const char* reason = nullptr) {
    if (!asioOutput_) return;
    for (auto& runtime : deckRuntimes_) {
      if (runtime.mediaEngine) {
        runtime.mediaEngine->setExternalAudioSink(MediaEngine::ExternalAudioSink{});
      }
    }
    asioOutput_->close();
    asioOutput_.reset();
    if (reason) {
      triggerToast(std::string("ASIO off: ") + reason);
      showLog("ASIO STOP", reason);
    }
  }

  bool armAsioOutput(const std::string& driverName, int channels) {
    disarmAsioOutput();
    if (driverName.empty()) return false;
    auto out = std::make_unique<deckboy::platform::audio::AsioOutput>();
    std::string err;
    if (!out->open(driverName, std::max(2, channels), static_cast<double>(kAudioRate), err)) {
      failRemoteCommand("ASIO: " + err);
      showLog("ASIO FAIL", driverName + ": " + err);
      return false;
    }
    // A driver clocked to external word clock will refuse 48k, and an
    // interface running the rest of a rig at 44.1 or 96 is a normal setup
    // rather than an error. AsioOutput converts between the rates, so this
    // reports the situation instead of refusing to open.
    asioOutput_ = std::move(out);

    MediaEngine::ExternalAudioSink sink;
    sink.channels = asioOutput_->channels();
    deckboy::platform::audio::AsioOutput* raw = asioOutput_.get();
    sink.write = [raw](const std::int16_t* data, std::size_t frames) {
      return raw->write(data, frames);
    };
    sink.queued = [raw]() { return raw->queuedFrames(); };
    for (auto& runtime : deckRuntimes_) {
      if (runtime.mediaEngine) runtime.mediaEngine->setExternalAudioSink(sink);
    }
    const int latMs = static_cast<int>(asioOutput_->outputLatencySeconds() * 1000.0);
    triggerToast("ASIO: " + driverName + "  " +
                 std::to_string(asioOutput_->channels()) + "ch  " +
                 std::to_string(asioOutput_->bufferFrames()) + " frames  " +
                 std::to_string(latMs) + "ms");
    // Logged because it is the number that decides A/V sync: video slaves to
    // the audio clock, so swapping SDL for ASIO shifts lip sync by the
    // DIFFERENCE in output latency. An operator chasing a sync problem needs
    // this written down, not guessed at.
    showLog("ASIO START", driverName + "  latency=" + std::to_string(latMs) + "ms" +
            (asioOutput_->resampling()
               ? "  resampled to " + std::to_string(static_cast<int>(asioOutput_->sampleRate())) + "Hz"
               : ""));
    if (asioOutput_->resampling()) {
      // Worth saying: conversion costs a little quality and adds a little
      // latency, and an operator who chose the device rate deliberately should
      // know Deckboy is not fighting them about it.
      triggerToast("ASIO running at " +
                   std::to_string(static_cast<int>(asioOutput_->sampleRate())) +
                   " Hz - audio resampled");
    }
    return true;
  }

  // Surfaced so the operator learns about glitching from the app rather than
  // from the room.
  std::uint64_t asioUnderruns() const {
    return asioOutput_ ? asioOutput_->underruns() : 0;
  }

  void cycleEncoderRateMode() {
    using Rate = EncoderOverrides::Rate;
    switch (encoderOverrides_.rate) {
      case Rate::Auto:    encoderOverrides_.rate = Rate::Quality; break;
      case Rate::Quality: encoderOverrides_.rate = Rate::Bitrate; break;
      default:            encoderOverrides_.rate = Rate::Auto;    break;
    }
    // Say plainly when the choice cannot reach the selected format, rather than
    // letting the operator set a number that will be discarded.
    const EncoderFormat& fmt = selectedEncoderFormat();
    if (encoderOverrides_.rate != Rate::Auto && !rateFlagForFormat(fmt.id)) {
      triggerToast(std::string(fmt.label) + " is profile-based - rate ignored");
    } else {
      triggerToast(std::string("encoder rate: ") + encoderRateModeLabel());
    }
    playUiSound(UiSoundEffect::Toggle);
  }

  void nudgeEncoderRate(int direction) {
    using Rate = EncoderOverrides::Rate;
    if (encoderOverrides_.rate == Rate::Quality) {
      encoderOverrides_.quality0to100 =
        std::clamp(encoderOverrides_.quality0to100 + direction * 5, 0, 100);
      triggerToast("quality " + std::to_string(encoderOverrides_.quality0to100));
    } else if (encoderOverrides_.rate == Rate::Bitrate) {
      // Step proportionally: 500k steps are meaningless at 50Mbps and far too
      // coarse at 1Mbps.
      const int step = encoderOverrides_.videoBitrateKbps >= 20000 ? 5000
                     : encoderOverrides_.videoBitrateKbps >= 5000  ? 1000
                                                                   : 250;
      encoderOverrides_.videoBitrateKbps =
        std::clamp(encoderOverrides_.videoBitrateKbps + direction * step, 250, 200000);
      triggerToast(std::to_string(encoderOverrides_.videoBitrateKbps) + "k");
    }
  }

  void cycleEncoderFps() {
    // 0 means "keep the source". The rest are the rates a show actually runs
    // at, including the film rates, so nobody has to type 23.976.
    static const double kRates[] = {0.0, 23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0};
    const int n = static_cast<int>(sizeof(kRates) / sizeof(kRates[0]));
    int i = 0;
    for (; i < n; ++i) {
      if (std::abs(kRates[i] - encoderOverrides_.fps) < 0.001) break;
    }
    encoderOverrides_.fps = kRates[(i + 1) % n];
    triggerToast("encoder fps: " + encoderFpsLabel());
    playUiSound(UiSoundEffect::Toggle);
  }

  void cycleEncoderSize() {
    static const int kSizes[][2] = {
      {0, 0}, {3840, 2160}, {2560, 1440}, {1920, 1080}, {1280, 720}, {854, 480},
    };
    const int n = static_cast<int>(sizeof(kSizes) / sizeof(kSizes[0]));
    int i = 0;
    for (; i < n; ++i) {
      if (kSizes[i][0] == encoderOverrides_.width &&
          kSizes[i][1] == encoderOverrides_.height) break;
    }
    const int next = (i >= n ? 0 : (i + 1) % n);
    encoderOverrides_.width  = kSizes[next][0];
    encoderOverrides_.height = kSizes[next][1];
    triggerToast("encoder size: " + encoderSizeLabel());
    playUiSound(UiSoundEffect::Toggle);
  }

  void cycleEncoderAudioRate() {
    static const int kRates[] = {0, 96, 128, 192, 256, 320};
    const int n = static_cast<int>(sizeof(kRates) / sizeof(kRates[0]));
    int i = 0;
    for (; i < n; ++i) {
      if (kRates[i] == encoderOverrides_.audioBitrateKbps) break;
    }
    encoderOverrides_.audioBitrateKbps = kRates[(i >= n ? 0 : (i + 1) % n)];
    triggerToast("audio: " + encoderAudioRateLabel());
    playUiSound(UiSoundEffect::Toggle);
  }

  void pickEncoderOutputDir() {
    showFolderDialog([this](std::vector<std::string> chosen) {
      if (chosen.empty() || chosen.front().empty()) return;   // cancelled
      encoderOverrides_.outputDir = chosen.front();
      triggerToast("encoder destination set");
    });
  }

  // ---- Encoder override labels ---------------------------------------------
  // Each returns what is CURRENTLY in force, so a chip reading AUTO or SOURCE
  // is stating a fact rather than offering a choice.
  const char* encoderRateModeLabel() const {
    switch (encoderOverrides_.rate) {
      case EncoderOverrides::Rate::Quality: return "QUALITY";
      case EncoderOverrides::Rate::Bitrate: return "BITRATE";
      default:                              return "AUTO";
    }
  }

  std::string encoderFpsLabel() const {
    if (encoderOverrides_.fps <= 0.0) return "SOURCE";
    return fmtFloat(encoderOverrides_.fps, 3);
  }

  std::string encoderSizeLabel() const {
    if (encoderOverrides_.width <= 0 || encoderOverrides_.height <= 0) return "SOURCE";
    return std::to_string(encoderOverrides_.width) + "x" +
           std::to_string(encoderOverrides_.height);
  }

  std::string encoderAudioRateLabel() const {
    if (encoderOverrides_.audioBitrateKbps <= 0) return "DEFAULT";
    return std::to_string(encoderOverrides_.audioBitrateKbps) + "k";
  }

  static const char* moshLookLabelFor(int look) {
    switch (look) {
      case kDatamoshLookSubtle:  return "SUBTLE";
      case kDatamoshLookExtreme: return "EXTREME";
      default:                   return "CLASSIC";
    }
  }

  // The datamosh recipe the ENCODER TAB will use for a manual convert. Cue
  // datamosh does not read this -- it reads the cue's own look, so that a show
  // reopened tomorrow moshes exactly as it did today.
  const char* activeMoshFormatId() const {
    return moshClassicLook_ ? "datamosh_classic" : "datamosh";
  }

  const char* moshLookLabel() const {
    return moshClassicLook_ ? "CLASSIC" : "SUBTLE";
  }

  void toggleMoshLook() {
    moshClassicLook_ = !moshClassicLook_;
    // If datamosh is the active format, follow the toggle immediately so the
    // next queued job uses the flavour actually shown.
    if (encoderFormatId_ == "datamosh" || encoderFormatId_ == "datamosh_classic") {
      encoderFormatId_ = activeMoshFormatId();
    }
    triggerToast(moshClassicLook_ ? "datamosh: classic, full effect (MPEG-4)"
                                  : "datamosh: subtle, self-heals (H.264)");
    playUiSound(UiSoundEffect::Toggle);
  }

  // Attempts for a queued job. The datamosh preset resolves through the
  // SMOOTH/CHUNKY toggle rather than a fixed recipe, so the chip and the encode
  // can never disagree.
  // What the queue actually runs. Resolves the FORMAT, always -- there is no
  // second path that can quietly ignore it.
  std::vector<std::vector<std::string>> attemptsForJob(const std::string& formatId,
                                                       const std::string& src,
                                                       const std::string& dst) {
    const EncoderFormat* fmt = encoderFormatById(formatId);
    if (!fmt) {
      fmt = encoderFormatById("h264");
    }
    if (!fmt) {
      return {};
    }
    if (std::string(fmt->id) == "h264_gpu") {
      // The one format with a fallback: NVENC first for speed, libx264 if the
      // machine has no NVIDIA encoder.
      const EncoderFormat* cpu = encoderFormatById("h264");
      std::vector<std::vector<std::string>> attempts {
        encodeArgsForFormat(*fmt, src, dst, encoderOverrides_)};
      if (cpu) {
        attempts.push_back(encodeArgsForFormat(*cpu, src, dst, encoderOverrides_));
      }
      return attempts;
    }
    return { encodeArgsForFormat(*fmt, src, dst, encoderOverrides_) };
  }

  // Mastering codecs are enormous - ProRes 422 measured ~167 MB for 3 seconds,
  // QuickTime RLE ~424 MB. Queueing one across a big playlist fills a drive,
  // and the operator finds out when the disk dies mid-show rather than now.
  static bool formatIsMastering(const std::string& id) {
    return id == "prores" || id == "prores4444" || id == "dnxhr" ||
           id == "qtrle"  || id == "ffv1"       || id == "png_seq";
  }

  void warnIfBulkMasteringEncode(int queuedCount) {
    if (queuedCount < 5 || !formatIsMastering(encoderFormatId_)) {
      return;
    }
    triggerToast(std::to_string(queuedCount) + " cues to " +
                 selectedEncoderFormat().label + " - check free disk space");
    playUiSound(UiSoundEffect::Error);
  }

  const EncoderFormat* encoderFormatById(const std::string& id) {
    for (const EncoderFormat& f : encoderFormatCatalog()) {
      if (id == f.id) return &f;
    }
    return nullptr;
  }

  const EncoderFormat& selectedEncoderFormat() {
    if (const EncoderFormat* f = encoderFormatById(encoderFormatId_)) return *f;
    return encoderFormatCatalog().front();
  }

  bool setEncoderFormat(const std::string& id) {
    const EncoderFormat* f = encoderFormatById(id);
    if (!f) {
      return false;
    }
    if (!encoderFormatAvailable(*f)) {
      // Say why rather than letting the encode fail later with nothing useful.
      triggerToast(std::string("format unavailable in this ffmpeg: ") + f->label);
      playUiSound(UiSoundEffect::Error);
      return false;
    }
    encoderFormatId_ = f->id;
    triggerToast(std::string("format: ") + f->label);
    playUiSound(UiSoundEffect::Toggle);
    return true;
  }

  // Presets are named entry points into the format matrix, not a parallel
  // system. Selecting one selects a format, so there is exactly ONE thing that
  // decides what the encoder runs. They used to be independent, which made the
  // whole format picker a dead control: ENCODEFORMAT set a value nothing read,
  // and every job silently encoded H.264 regardless.
  const char* formatIdForPreset(EncoderPreset preset) const {
    switch (preset) {
      case EncoderPreset::Proxy:            return "proxy";
      case EncoderPreset::MatchSource:      return "h264";
      case EncoderPreset::DatamoshFriendly: return activeMoshFormatId();
      case EncoderPreset::DeliveryH264:
      default:                              return "h264_gpu";
    }
  }

  void setEncoderPreset(EncoderPreset preset) {
    encoderPreset_ = preset;
    encoderFormatId_ = formatIdForPreset(preset);
    triggerToast(std::string("encode preset: ") + encoderPresetLabel(preset));
    playUiSound(UiSoundEffect::Toggle);
  }

  static const char* encoderPresetLabel(EncoderPreset preset) {
    switch (preset) {
      case EncoderPreset::Proxy:            return "PROXY 720p";
      case EncoderPreset::MatchSource:      return "MATCH SOURCE";
      case EncoderPreset::DatamoshFriendly: return "DATAMOSH";
      case EncoderPreset::DeliveryH264:
      default:                              return "DELIVERY H.264";
    }
  }

  // Datamosh output sits BESIDE the original: the effect toggles between the
  // two, so both have to survive.
  static bool formatKeepsOriginal(const EncoderFormat& fmt) {
    const std::string id = fmt.id;
    return id.rfind("datamosh", 0) == 0;
  }

  static bool presetKeepsOriginal(EncoderPreset preset) {
    return preset == EncoderPreset::DatamoshFriendly;
  }

  // Attempts in order; the queue keeps the first that produces a file.
  static std::vector<std::vector<std::string>> encodeAttemptsFor(
      EncoderPreset preset, const std::string& src, const std::string& dst) {
    const std::vector<std::string> head {"ffmpeg", "-y", "-i", src};
    auto build = [&](std::vector<std::string> mid) {
      std::vector<std::string> args = head;
      args.insert(args.end(), mid.begin(), mid.end());
      args.push_back("-movflags");
      args.push_back("+faststart");
      args.push_back(dst);
      return args;
    };
    switch (preset) {
      case EncoderPreset::DatamoshFriendly:
        // Deliberately hostile to normal encoding practice, because the point
        // is a stream that smears when its keyframes are dropped at decode:
        //   -bf 0          B-frames reference both directions and break worst
        //   -sc_threshold 0 no keyframe at every scene change - those cuts are
        //                  exactly the moments the effect should carry through
        //   -refs 1        motion vectors read only the previous frame
        //   -g 120         regular keyframes for SEEKING only; the decoder
        //                  drops them at playback, so this is seek granularity,
        //                  not an effect parameter
        //   -vsync cfr     the moshed copy must stay frame-for-frame
        //                  interchangeable with the original, or swapping
        //                  between them shifts sync
        // libx264 ONLY: NVENC ignores or constrains refs/scenecut and injects
        // its own IDR frames, which would quietly undo all of this.
        return { build({"-c:v", "libx264", "-preset", "medium", "-crf", "18",
                        "-bf", "0", "-sc_threshold", "0", "-refs", "1", "-g", "120",
                        "-pix_fmt", "yuv420p", "-vsync", "cfr",
                        "-c:a", "aac", "-b:a", "192k"}) };
      case EncoderPreset::Proxy:
        return { build({"-c:v", "libx264", "-preset", "veryfast", "-crf", "26",
                        "-vf", "scale=-2:720", "-pix_fmt", "yuv420p",
                        "-c:a", "aac", "-b:a", "128k"}) };
      case EncoderPreset::MatchSource:
        return { build({"-c:v", "libx264", "-preset", "slow", "-crf", "18",
                        "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k"}) };
      case EncoderPreset::DeliveryH264:
      default:
        // GPU first with a CPU decode (robust on odd inputs); libx264 if NVENC
        // is unavailable or produced nothing.
        return { build({"-c:v", "h264_nvenc", "-preset", "p4", "-cq", "23",
                        "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k"}),
                 build({"-c:v", "libx264", "-preset", "veryfast", "-crf", "20",
                        "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k"}) };
    }
  }

  // "" when this cue has no job, otherwise whether it is actually encoding or
  // still waiting behind the concurrency cap. The list used to call every job
  // "[converting...]", which was a lie for 30 of 31 of them.
  const char* cueQueueStateLabel(const std::string& path) const {
    for (const auto& job : conversionJobs_) {
      if (job.sourcePath != path) continue;
      return job.state == ConversionState::Running ? "   [converting...]" : "   [queued]";
    }
    return "";
  }

  // Queue a datamosh prep for one cue WITHOUT disturbing the operator's
  // selected encoder format. The toggle drives this, so it must not silently
  // repoint the Encoder tab at DATAMOSH for the next manual convert.
  void queueDatamoshPrepForCue(int deckIndex, int cueIndex) {
    // The recipe comes from the CUE, not the Encoder tab's chip: this prep is
    // for this clip's look and must survive a save/reload.
    int look = kDatamoshLookClassic;
    if (deckIndex >= 0 && deckIndex < static_cast<int>(project_.decks.size())) {
      const Deck& deck = project_.decks[deckIndex];
      if (cueIndex >= 0 && cueIndex < static_cast<int>(deck.cues.size())) {
        look = deck.cues[cueIndex].datamoshLook;
      }
    }
    const EncoderPreset savedPreset = encoderPreset_;
    const std::string savedFormat = encoderFormatId_;
    encoderPreset_ = EncoderPreset::DatamoshFriendly;
    encoderFormatId_ = moshFormatIdForLook(look);
    convertCueMedia(deckIndex, cueIndex);
    encoderPreset_ = savedPreset;
    encoderFormatId_ = savedFormat;
  }

  // Is a prep already running or queued for this cue?
  bool datamoshPrepInFlight(const std::string& sourcePath) const {
    for (const auto& job : conversionJobs_) {
      if (job.sourcePath == sourcePath && job.keepsOriginal) {
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
    // Extension comes from the SELECTED FORMAT, not a hardcoded ".mp4" -- HAP
    // and ProRes need .mov, VP9 .webm, FFV1 .mkv, and writing them all as .mp4
    // produced files ffmpeg would refuse or mislabel.
    const EncoderFormat& chosen = selectedEncoderFormat();
    std::string container = chosen.container;
    if (container == "matroska") container = "mkv";
    if (container == "image2")   container = "png";
    const std::string suffix = presetKeepsOriginal(encoderPreset_)
                                 ? ("_mosh." + container)
                                 : ("." + container);
    fs::path dst = outDir / (src.stem().string() + suffix);
    if (fs::exists(dst, ec) && fs::equivalent(dst, src, ec)) {
      dst = outDir / (src.stem().string() + "_conv.mp4");
    }
    std::string srcStr = src.string();
    std::string dstStr = dst.string();
    ConversionJob job;
    job.deckIndex = deckIndex;
    job.sourcePath = cue.path;
    job.destPath = dstStr;
    job.label = cue.name.empty() ? src.filename().string() : cue.name;
    job.progress = std::make_shared<std::atomic<double>>(-1.0);
    job.cancel = std::make_shared<std::atomic<bool>>(false);
    job.sourceSeconds = cue.duration > 0.0 ? cue.duration : 0.0;
    job.state = ConversionState::Queued;
    job.preset = encoderPreset_;
    job.keepsOriginal = presetKeepsOriginal(encoderPreset_);
    job.formatId = encoderFormatId_;
    conversionJobs_.push_back(std::move(job));
    triggerToast("queued " + src.filename().string());
    playUiSound(UiSoundEffect::Import);
    pumpConversionQueue();
  }

  // Start queued jobs up to the concurrency cap. Called on enqueue and once per
  // update tick, so a finishing job immediately pulls the next one in.
  void pumpConversionQueue() {
    if (encoderQueuePaused_) {
      return;
    }
    int running = 0;
    for (const auto& job : conversionJobs_) {
      if (job.state == ConversionState::Running) ++running;
    }
    for (std::size_t i = 0; i < conversionJobs_.size(); ++i) {
      if (running >= std::max(1, encoderConcurrency_)) {
        break;
      }
      if (conversionJobs_[i].state != ConversionState::Queued ||
          conversionJobs_[i].held) {
        continue;
      }
      startConversionJob(i);
      ++running;
    }
  }

  // Takes an index rather than a reference: ConversionJob is declared further
  // down the class body than this include, so it cannot name the type in a
  // parameter list (function bodies are fine - they are a complete-class
  // context, parameter types are not).
  void startConversionJob(std::size_t index) {
    auto& job = conversionJobs_[index];
    const std::string srcStr = job.sourcePath;
    const std::string dstStr = job.destPath;
    auto progress = job.progress;
    auto cancel = job.cancel;
    const double totalSeconds = job.sourceSeconds;
    const auto attempts = attemptsForJob(job.formatId, srcStr, dstStr);
    job.state = ConversionState::Running;
    job.future = std::async(std::launch::async, [dstStr, progress, cancel, totalSeconds, attempts]() -> bool {
      auto produced = [&]() {
        std::error_code e;
        return fs::exists(dstStr, e) && fs::file_size(dstStr, e) > 1024;
      };
      // Run one encode, streaming ffmpeg's own progress report back into the
      // shared counter. `-progress pipe:1` emits key=value lines on stdout; the
      // one that matters is out_time_us, which divided by the probed duration
      // is the fraction encoded. Without a duration there is nothing to divide
      // by, so the job stays indeterminate and the UI spins instead.
      auto runEncode = [&](std::vector<std::string> args) {
        args.insert(args.begin() + 1, "-nostdin");
        args.insert(args.end() - 1, "-progress");
        args.insert(args.end() - 1, "pipe:1");
        ChildProcess proc;
        if (!spawnPipeProcess(proc, args)) {
          return;
        }
        std::string pending;
        std::array<char, 4096> chunk {};
        while (true) {
          if (cancel->load()) {
            proc.stop();
            std::error_code ce;
            fs::remove(dstStr, ce);  // a half-written file is not a usable cue
            return;
          }
          int n = readSome(proc.readFd, chunk.data(), chunk.size());
          if (n <= 0) {
            break;  // ffmpeg closed stdout — it has exited or is about to
          }
          pending.append(chunk.data(), static_cast<size_t>(n));
          size_t nl;
          while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = trim(pending.substr(0, nl));
            pending.erase(0, nl + 1);
            if (totalSeconds <= 0.0 || line.rfind("out_time_us=", 0) != 0) {
              continue;
            }
            double us = std::atof(line.c_str() + 12);
            if (us >= 0.0) {
              progress->store(std::clamp((us / 1000000.0) / totalSeconds, 0.0, 1.0));
            }
          }
        }
        proc.stop();
      };

      // Try each attempt for this preset in turn; keep the first that lands.
      for (std::size_t a = 0; a < attempts.size(); ++a) {
        if (a > 0) {
          progress->store(-1.0);   // a fallback restarts from zero
        }
        runEncode(attempts[a]);
        if (cancel->load()) {
          return false;
        }
        if (produced()) {
          progress->store(1.0);
          return true;
        }
      }
      return false;
    });
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
    } else {
      warnIfBulkMasteringEncode(started);
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
