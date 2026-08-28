// app_numeric_params.ipp — typed entry for the inspector's numeric rows.
//
// Every dec/inc row in the cue inspector can be dragged to scrub, but only the
// geometry rows could be CLICKED to type an exact number, because each of those
// carried its own QuickAction (EditScaleX, EditRotation, ...). Adding that to
// the timer, tone, chip synth and video synth rows one action at a time would
// have meant twenty more enum values and twenty more near-identical handlers.
//
// So the row carries a parameter id instead (QuickButton::param) and there is
// one action, one handler, and one table. A new numeric control needs a line
// here and a paramId on its row.
//
// Ranges below MUST match the clamps the dec/inc actions and the generator
// apply. Where they disagree the operator can type a value the buttons cannot
// reach, which reads as the field being broken.

enum class NumericParam : int {
  None = -1,

  // Video synth
  VsSpeed = 0,
  VsScale,
  VsFeedback,
  VsZoom,
  VsReact,
  VsResolution,
  VsPixelSort,
  VsGlitch,
  VsCrt,
  VsFreeAngle,

  // Tone generator
  ToneLevel,
  ToneFreq,

  // Motion driver
  MotionDriverSpeed,

  // Chip synth
  SynthNote,
  SynthAttack,
  SynthRelease,
  FdsDepth,
  FdsRatio,
  FdsRetrig,
};

struct NumericParamSpec {
  const char* token;      // inline editor identity, so it can restore position
  const char* label;      // shown in the editor
  const char* hint;       // range, in the operator's words
  double lo = 0.0;
  double hi = 1.0;
  int precision = 2;
  bool integral = false;
};

const NumericParamSpec* numericParamSpec(NumericParam id) {
  switch (id) {
    case NumericParam::VsSpeed: {
      static const NumericParamSpec s {"vs.speed", "Speed",
                                       "Master oscillator rate, 0.01-8", 0.01, 8.0, 2, false};
      return &s;
    }
    case NumericParam::VsScale: {
      static const NumericParamSpec s {"vs.scale", "Scale",
                                       "Feature size, 0.1-8", 0.1, 8.0, 2, false};
      return &s;
    }
    case NumericParam::VsFeedback: {
      static const NumericParamSpec s {"vs.feedback", "Feedback",
                                       "How much of each frame feeds the next, 0-0.95",
                                       0.0, 0.95, 2, false};
      return &s;
    }
    case NumericParam::VsZoom: {
      static const NumericParamSpec s {"vs.zoom", "Feedback zoom",
                                       "Per-frame zoom of the feedback path, 0.90-1.15",
                                       0.90, 1.15, 3, false};
      return &s;
    }
    case NumericParam::VsReact: {
      static const NumericParamSpec s {"vs.react", "Audio reactivity",
                                       "How hard the audio drives it, 0-1", 0.0, 1.0, 2, false};
      return &s;
    }
    case NumericParam::VsResolution: {
      static const NumericParamSpec s {"vs.resolution", "Detail",
                                       "Internal resolution, 1 (chunky) to 5 (fine)",
                                       1.0, 5.0, 0, true};
      return &s;
    }
    case NumericParam::VsPixelSort: {
      static const NumericParamSpec s {"vs.smear", "Smear",
                                       "Pixel sort amount, 0-1", 0.0, 1.0, 2, false};
      return &s;
    }
    case NumericParam::VsGlitch: {
      static const NumericParamSpec s {"vs.glitch", "Glitch",
                                       "Block displacement and channel tearing, 0-1",
                                       0.0, 1.0, 2, false};
      return &s;
    }
    case NumericParam::VsCrt: {
      static const NumericParamSpec s {"vs.crt", "CRT",
                                       "Scanlines, bloom and gun misalignment, 0-1",
                                       0.0, 1.0, 2, false};
      return &s;
    }
    case NumericParam::VsFreeAngle: {
      static const NumericParamSpec s {"vs.spin", "Sprite spin",
                                       "Degrees per second, -720 to 720",
                                       -720.0, 720.0, 0, false};
      return &s;
    }
    case NumericParam::ToneLevel: {
      static const NumericParamSpec s {"tone.level", "Level",
                                       "dBFS, -60 to 0 (-18 is EBU alignment)",
                                       -60.0, 0.0, 1, false};
      return &s;
    }
    case NumericParam::ToneFreq: {
      static const NumericParamSpec s {"tone.freq", "Frequency",
                                       "Hertz, 20-20000", 20.0, 20000.0, 1, false};
      return &s;
    }
    case NumericParam::MotionDriverSpeed: {
      static const NumericParamSpec s {"motion.speed", "Driver speed",
                                       "Fields per frame, 0-4 (1 = one field "
                                       "per rendered frame)", 0.0, 4.0, 2, false};
      return &s;
    }
    case NumericParam::SynthNote: {
      static const NumericParamSpec s {"synth.note", "Note",
                                       "Pitch in hertz, 20-8000", 20.0, 8000.0, 2, false};
      return &s;
    }
    case NumericParam::SynthAttack: {
      static const NumericParamSpec s {"synth.attack", "Attack",
                                       "Seconds, 0-4", 0.0, 4.0, 3, false};
      return &s;
    }
    case NumericParam::SynthRelease: {
      static const NumericParamSpec s {"synth.release", "Release",
                                       "Seconds, 0-8", 0.0, 8.0, 3, false};
      return &s;
    }
    case NumericParam::FdsDepth: {
      static const NumericParamSpec s {"fds.depth", "Mod depth",
                                       "FDS hardware gain range, 0-63", 0.0, 63.0, 0, true};
      return &s;
    }
    case NumericParam::FdsRatio: {
      static const NumericParamSpec s {"fds.ratio", "Mod ratio",
                                       "Modulator as a ratio of the note, 0.05-16",
                                       0.05, 16.0, 2, false};
      return &s;
    }
    case NumericParam::FdsRetrig: {
      static const NumericParamSpec s {"fds.retrig", "Retrigger",
                                       "Seconds between retriggers, 0-8", 0.0, 8.0, 2, false};
      return &s;
    }
    default:
      return nullptr;
  }
}

// Read the live value. Returns false when the selected cue does not carry it,
// which is how a stale row (the selection changed mid-click) is refused rather
// than writing into the wrong cue.
bool readNumericParam(const Cue& cue, NumericParam id, double& out) {
  const VideoSynthSettings& v = cue.videoSynth;
  switch (id) {
    case NumericParam::VsSpeed:      out = v.speed; return true;
    case NumericParam::VsScale:      out = v.scale; return true;
    case NumericParam::VsFeedback:   out = v.feedbackAmount; return true;
    case NumericParam::VsZoom:       out = v.feedbackZoom; return true;
    case NumericParam::VsReact:      out = v.audioReactivity; return true;
    case NumericParam::VsResolution: out = v.resolution; return true;
    case NumericParam::VsPixelSort:  out = v.pixelSort; return true;
    case NumericParam::VsGlitch:     out = v.glitch; return true;
    case NumericParam::VsCrt:        out = v.crt; return true;
    case NumericParam::VsFreeAngle:  out = v.spriteFreeAngle; return true;
    case NumericParam::ToneLevel:    out = cue.tone.levelDbfs; return true;
    case NumericParam::ToneFreq:     out = cue.tone.frequencyHz; return true;
    case NumericParam::MotionDriverSpeed: out = cue.motionDriverSpeed; return true;
    case NumericParam::SynthNote:    out = cue.tone.synth.noteHz; return true;
    case NumericParam::SynthAttack:  out = cue.tone.synth.attackSeconds; return true;
    case NumericParam::SynthRelease: out = cue.tone.synth.releaseSeconds; return true;
    case NumericParam::FdsDepth:     out = cue.tone.synth.modDepth; return true;
    case NumericParam::FdsRatio:     out = cue.tone.synth.modRatio; return true;
    case NumericParam::FdsRetrig:    out = cue.tone.synth.retriggerSeconds; return true;
    default: return false;
  }
}

void writeNumericParam(Cue& cue, NumericParam id, double value) {
  VideoSynthSettings& v = cue.videoSynth;
  switch (id) {
    case NumericParam::VsSpeed:      v.speed = value; break;
    case NumericParam::VsScale:      v.scale = value; break;
    case NumericParam::VsFeedback:   v.feedbackAmount = value; break;
    case NumericParam::VsZoom:       v.feedbackZoom = value; break;
    case NumericParam::VsReact:      v.audioReactivity = value; break;
    case NumericParam::VsResolution: v.resolution = static_cast<int>(std::lround(value)); break;
    case NumericParam::VsPixelSort:  v.pixelSort = value; break;
    case NumericParam::VsGlitch:     v.glitch = value; break;
    case NumericParam::VsCrt:        v.crt = value; break;
    case NumericParam::VsFreeAngle:  v.spriteFreeAngle = value; break;
    case NumericParam::ToneLevel:    cue.tone.levelDbfs = value; break;
    case NumericParam::ToneFreq:     cue.tone.frequencyHz = value; break;
    case NumericParam::MotionDriverSpeed:
      cue.motionDriverSpeed = static_cast<float>(value); break;
    case NumericParam::SynthNote:    cue.tone.synth.noteHz = value; break;
    case NumericParam::SynthAttack:  cue.tone.synth.attackSeconds = value; break;
    case NumericParam::SynthRelease: cue.tone.synth.releaseSeconds = value; break;
    case NumericParam::FdsDepth:     cue.tone.synth.modDepth = static_cast<int>(std::lround(value)); break;
    case NumericParam::FdsRatio:     cue.tone.synth.modRatio = value; break;
    case NumericParam::FdsRetrig:    cue.tone.synth.retriggerSeconds = value; break;
    default: break;
  }
}

// The one handler behind QuickAction::EditNumericParam.
void editNumericParam(int rawId) {
  const NumericParam id = static_cast<NumericParam>(rawId);
  const NumericParamSpec* spec = numericParamSpec(id);
  if (!spec) {
    return;
  }
  Cue* cue = selectedCueMutable();
  double current = 0.0;
  if (!cue || !readNumericParam(*cue, id, current)) {
    return;
  }
  std::ostringstream shown;
  shown << std::fixed << std::setprecision(spec->precision) << current;
  openInlineNumericExpressionEditor(
    spec->token, spec->label,
    std::string(spec->hint) + " (supports + - * / and ())", shown.str(),
    [this, id, spec](double value) {
      const double next = std::clamp(value, spec->lo, spec->hi);
      bool changed = false;
      forEachFocusedSelectedCueMutable([&](Cue& each, int) {
        double ignored = 0.0;
        if (!readNumericParam(each, id, ignored)) {
          return;   // a cue of another kind in the selection: leave it alone
        }
        writeNumericParam(each, id, next);
        changed = true;
      });
      if (!changed) {
        return;
      }
      std::ostringstream ss;
      ss << std::fixed << std::setprecision(spec->precision) << next;
      triggerToast(std::string(spec->label) + " " + ss.str());
      markProjectDirty();
    });
}

// ---------------------------------------------------------------------------
// Effect stack editing. Every one of these takes the effect's INDEX, which the
// row supplied through QuickButton::param -- that is what lets eight actions
// serve a list of any length rather than needing eight per slot.
// ---------------------------------------------------------------------------


// Whether this cue currently needs the CPU pixel path at all. The decode
// format is chosen at TAKE and FROZEN there, so a cue taken with an empty
// stack is decoding NV12 -- on Windows, zero-copy on the GPU -- and there are
// no CPU pixels for an effect to run on. Adding an effect to a LIVE cue
// therefore did nothing at all until the next take, which looks exactly like a
// broken effect and is how it was reported.
bool cueNeedsCpuPixelPath(const Cue& cue) {
  return cue.chromaKeyEnabled ||
         cueHasColorControls(cue) ||
         deckboy::effects::cueEffectStackActive(cue.effects);
}

// Re-take the live cue when that answer CHANGES, so the decoder is reopened in
// a format the effects can act on. Only on a change: doing it on every amount
// nudge would restart playback under the operator's hand mid-scrub.
void refreshLiveCueIfPixelPathChanged(bool wasNeeded) {
  const Cue* cue = selectedCueMutable();
  if (!cue) {
    return;
  }
  if (cueNeedsCpuPixelPath(*cue) != wasNeeded) {
    refreshFocusedLiveCueRuntimeIfSelected();
  }
}

std::vector<deckboy::effects::CueEffect>* selectedEffectStack() {
  Cue* cue = selectedCueMutable();
  return cue ? &cue->effects : nullptr;
}

// Every effect, as dropdown choices. One list, built once, used both for
// adding and for changing an existing entry -- so the operator always picks
// from a visible list and never cycles blind through options they cannot see.
std::vector<std::pair<std::string, std::string>> cueEffectChoices() {
  std::vector<std::pair<std::string, std::string>> choices;
  for (int i = 1; i < static_cast<int>(deckboy::effects::CueEffectKind::Count); ++i) {
    const auto kind = static_cast<deckboy::effects::CueEffectKind>(i);
    choices.push_back({deckboy::effects::cueEffectToken(kind),
                       deckboy::effects::cueEffectLabel(kind)});
  }
  return choices;
}

void effectStackAdd() {
  auto* stack = selectedEffectStack();
  if (!stack) {
    return;
  }
  if (stack->size() >= 12) {
    // A cap, because the stack runs per pixel per frame and an operator who
    // stacks thirty of them at 4K will blame the app rather than the stack.
    triggerToast("effect stack full (12)");
    return;
  }
  // PICK from the list rather than appending a default and making the operator
  // cycle to what they wanted. Adding "invert" to everyone who asked for an
  // effect was the worst part of the first version of this.
  const auto choices = cueEffectChoices();
  openDropdown("cue.effect.add", lastInlineEditorAnchorRect_, choices,
               choices.front().first,
               [this](const std::string& token) {
    auto* live = selectedEffectStack();
    if (!live || live->size() >= 12) {
      return;
    }
    deckboy::effects::CueEffect fx;
    fx.kind = deckboy::effects::cueEffectFromToken(token);
    if (fx.kind == deckboy::effects::CueEffectKind::None) {
      return;
    }
    fx.amount = 1.0f;
    fx.paramA = 0.5f;
    Cue* liveCue = selectedCueMutable();
    const bool wasNeeded = liveCue ? cueNeedsCpuPixelPath(*liveCue) : false;
    live->push_back(fx);
    triggerToast(std::string("added ") + deckboy::effects::cueEffectLabel(fx.kind));
    markProjectDirty();
    syncDatamoshFromStack();
    refreshLiveCueIfPixelPathChanged(wasNeeded);
  });
}

bool effectIndexValid(const std::vector<deckboy::effects::CueEffect>* stack, int index) {
  return stack && index >= 0 && index < static_cast<int>(stack->size());
}

void effectStackRemove(int index) {
  auto* stack = selectedEffectStack();
  if (!effectIndexValid(stack, index)) {
    return;
  }
  const Cue* beforeCue = selectedCueMutable();
  const bool wasNeeded = beforeCue ? cueNeedsCpuPixelPath(*beforeCue) : false;
  const std::string gone = deckboy::effects::cueEffectLabel((*stack)[index].kind);
  stack->erase(stack->begin() + index);
  triggerToast("effect removed: " + gone);
  markProjectDirty();
  syncDatamoshFromStack();
  pruneUnusedMotionDriver();
  refreshLiveCueIfPixelPathChanged(wasNeeded);
}

void effectStackCycleKind(int index) {
  auto* stack = selectedEffectStack();
  if (!effectIndexValid(stack, index)) {
    return;
  }
  // Named "cycle" for historical reasons; it opens the PICKER. Cycling through
  // ten effects with a button, unable to see what the options are or what you
  // have, is not a way to choose anything.
  const auto choices = cueEffectChoices();
  openDropdown("cue.effect.kind", lastInlineEditorAnchorRect_, choices,
               deckboy::effects::cueEffectToken((*stack)[index].kind),
               [this, index](const std::string& token) {
    auto* live = selectedEffectStack();
    if (!effectIndexValid(live, index)) {
      return;
    }
    const auto kind = deckboy::effects::cueEffectFromToken(token);
    if (kind == deckboy::effects::CueEffectKind::None) {
      return;
    }
    (*live)[index].kind = kind;
    markProjectDirty();
    syncDatamoshFromStack();
    // Changing the last puppet into something else leaves the driver with
    // nothing to drive, exactly as removing it would.
    pruneUnusedMotionDriver();
  });
}

void effectStackToggleBypass(int index) {
  auto* stack = selectedEffectStack();
  if (!effectIndexValid(stack, index)) {
    return;
  }
  const Cue* beforeCue = selectedCueMutable();
  const bool wasNeeded = beforeCue ? cueNeedsCpuPixelPath(*beforeCue) : false;
  auto& fx = (*stack)[index];
  fx.bypassed = !fx.bypassed;
  // Bypass RETURNS the setting; turning the amount to zero throws it away.
  triggerToast(std::string(deckboy::effects::cueEffectLabel(fx.kind)) +
               (fx.bypassed ? " bypassed" : " active"));
  markProjectDirty();
  syncDatamoshFromStack();
  refreshLiveCueIfPixelPathChanged(wasNeeded);
}

void effectStackNudge(int index, float delta) {
  auto* stack = selectedEffectStack();
  if (!effectIndexValid(stack, index)) {
    return;
  }
  const Cue* beforeCue = selectedCueMutable();
  const bool wasNeeded = beforeCue ? cueNeedsCpuPixelPath(*beforeCue) : false;
  auto& fx = (*stack)[index];
  fx.amount = std::clamp(fx.amount + delta, 0.0f, 1.0f);
  markProjectDirty();
  // An amount crossing zero flips whether the CPU path is needed at all.
  refreshLiveCueIfPixelPathChanged(wasNeeded);
}

// paramA and paramB, by index rather than by name: the two are handled
// identically and the only thing that differs is which float is touched.
// Neither changes whether the CPU pixel path is needed -- only amount does
// that -- so there is no refresh here.
void effectStackNudgeParam(int index, int which, float delta) {
  auto* stack = selectedEffectStack();
  if (!effectIndexValid(stack, index)) {
    return;
  }
  auto& fx = (*stack)[index];
  float& value = which == 0 ? fx.paramA : fx.paramB;
  value = std::clamp(value + delta, 0.0f, 1.0f);
  markProjectDirty();
}

void effectStackEditParam(int index, int which) {
  auto* stack = selectedEffectStack();
  if (!effectIndexValid(stack, index)) {
    return;
  }
  const auto& fx = (*stack)[index];
  const char* label = deckboy::effects::cueEffectParamLabel(fx.kind, which);
  if (!label) {
    return;   // this effect has no such parameter; nothing to type into
  }
  std::ostringstream current;
  current << std::fixed << std::setprecision(2)
          << (which == 0 ? fx.paramA : fx.paramB);
  openInlineNumericExpressionEditor(
    which == 0 ? "cue.effect.paramA" : "cue.effect.paramB", label,
    "0-1 (supports + - * / and ())", current.str(),
    [this, index, which](double value) {
      auto* live = selectedEffectStack();
      if (!effectIndexValid(live, index)) {
        return;   // the selection moved while the editor was open
      }
      auto& target = (*live)[index];
      (which == 0 ? target.paramA : target.paramB) =
        std::clamp(static_cast<float>(value), 0.0f, 1.0f);
      markProjectDirty();
    });
}

void effectStackMove(int index, int direction) {
  auto* stack = selectedEffectStack();
  if (!effectIndexValid(stack, index)) {
    return;
  }
  const int target = index + direction;
  if (target < 0 || target >= static_cast<int>(stack->size())) {
    return;
  }
  // ORDER IS THE EFFECT. Posterise then invert is not invert then posterise,
  // so moving an entry is a real edit and not a cosmetic reshuffle.
  std::swap((*stack)[index], (*stack)[target]);
  markProjectDirty();
}

void effectStackEditAmount(int index) {
  auto* stack = selectedEffectStack();
  if (!effectIndexValid(stack, index)) {
    return;
  }
  std::ostringstream current;
  current << std::fixed << std::setprecision(2) << (*stack)[index].amount;
  openInlineNumericExpressionEditor(
    "cue.effect.amount", deckboy::effects::cueEffectLabel((*stack)[index].kind),
    "Amount 0-1 (supports + - * / and ())", current.str(),
    [this, index](double value) {
      auto* live = selectedEffectStack();
      if (!effectIndexValid(live, index)) {
        return;   // the selection moved while the editor was open
      }
      (*live)[index].amount = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
      markProjectDirty();
    });
}

// ---------------------------------------------------------------------------
// Motion driver: the clip whose movement puppeteers this cue.
// ---------------------------------------------------------------------------

void pickMotionDriver() {
  if (!selectedCueMutable()) {
    return;
  }
  // Video only. The driver is decoded for its MOTION VECTORS, which a still
  // does not have and an audio file certainly does not -- offering them would
  // be a picker that cannot produce a working answer.
  static const SDL_DialogFileFilter kDriverFilters[] = {
    {"Video", "mp4;mov;mkv;avi;m4v;webm;mpg;mpeg;ts;m2ts"},
    {"All files", "*"},
  };
  showOpenFileDialog(
    std::vector<SDL_DialogFileFilter>(std::begin(kDriverFilters), std::end(kDriverFilters)),
    /*allowMany=*/false,
    [this](std::vector<std::string> files) {
      if (files.empty()) {
        return;
      }
      Cue* cue = selectedCueMutable();
      if (!cue) {
        return;   // selection moved while the dialog was open
      }
      cue->motionDriverPath = files.front();
      // Arm the effect too if it is not already in the stack. Choosing a driver
      // and then finding nothing happens because the effect was never added is
      // the sort of two-step that makes a feature feel broken.
      bool haveEffect = false;
      for (const auto& fx : cue->effects) {
        if (fx.kind == deckboy::effects::CueEffectKind::MotionPuppet) {
          haveEffect = true;
          break;
        }
      }
      if (!haveEffect) {
        deckboy::effects::CueEffect fx;
        fx.kind = deckboy::effects::CueEffectKind::MotionPuppet;
        fx.amount = 1.0f;
        cue->effects.push_back(fx);
      }
      triggerToast("motion driver set");
      markProjectDirty();
    });
}

// Drop the driver when nothing is left that could use it.
//
// A driver with no puppet is a clip being decoded every frame for a field
// nothing reads, and an inspector row for a control that cannot do anything --
// which is this codebase's signature bug. Called after any edit that can
// remove the last puppet.
void pruneUnusedMotionDriver() {
  Cue* cue = selectedCueMutable();
  if (!cue || cue->motionDriverPath.empty()) {
    return;
  }
  if (deckboy::effects::cueEffectStackNeedsDriver(cue->effects)) {
    return;
  }
  cue->motionDriverPath.clear();
  // Said out loud: the driver was something the operator chose, and taking it
  // away silently would look like the app losing it.
  triggerToast("driver cleared - nothing left to puppet");
  markProjectDirty();
}

void clearMotionDriver() {
  Cue* cue = selectedCueMutable();
  if (!cue || cue->motionDriverPath.empty()) {
    return;
  }
  cue->motionDriverPath.clear();
  triggerToast("motion driver cleared");
  markProjectDirty();
}

void nudgeMotionDriverSpeed(float delta) {
  Cue* cue = selectedCueMutable();
  if (!cue) return;
  cue->motionDriverSpeed = std::clamp(cue->motionDriverSpeed + delta, 0.0f, 4.0f);
  markProjectDirty();
}

void toggleMotionDriverPaused() {
  Cue* cue = selectedCueMutable();
  if (!cue) return;
  cue->motionDriverPaused = !cue->motionDriverPaused;
  // Paused HOLDS the last field rather than stopping the effect: the picture
  // stays displaced by whatever the driver was doing, which is a look. Saying
  // "held" rather than "paused" is the honest word for that.
  triggerToast(cue->motionDriverPaused ? "driver held" : "driver running");
  markProjectDirty();
}

void toggleMotionDriverRestartOnTake() {
  Cue* cue = selectedCueMutable();
  if (!cue) return;
  cue->motionDriverRestartOnTake = !cue->motionDriverRestartOnTake;
  triggerToast(cue->motionDriverRestartOnTake ? "driver restarts on take"
                                              : "driver free-runs");
  markProjectDirty();
}

void restartSelectedMotionDriver() {
  restartMotionDriver(project_.focusedDeckIndex);
  triggerToast("driver restarted");
}

// Datamosh lives in the effect stack like everything else, but the machinery
// behind it is not a pixel pass -- it withholds keyframes at decode and needs a
// background transcode first. So the stack entry is the UI and `datamoshEnabled`
// remains the thing the engine reads; this keeps the two in step.
//
// Reuses toggleSelectedDatamosh() rather than reimplementing it, which means
// the refusal on cues that cannot support it, the prepare-on-enable and the
// swap back to the original clip all behave exactly as they always did.
void syncDatamoshFromStack() {
  Cue* cue = selectedCueMutable();
  if (!cue) {
    return;
  }
  bool want = false;
  int entry = -1;
  for (int i = 0; i < static_cast<int>(cue->effects.size()); ++i) {
    const auto& fx = cue->effects[i];
    if (fx.kind == deckboy::effects::CueEffectKind::Datamosh) {
      entry = i;
      if (!fx.bypassed && fx.amount > 0.0005f) {
        want = true;
      }
      break;
    }
  }
  if (want == cue->datamoshEnabled) {
    return;
  }
  toggleSelectedDatamosh();
  // If it REFUSED -- a still, a camera, a synth, anything not file-backed
  // video -- the flag will not have moved. Bypass the entry so the stack
  // stops asking every frame, and leave it visible so the operator can see
  // what was refused rather than having it silently vanish.
  cue = selectedCueMutable();
  if (cue && want && !cue->datamoshEnabled && entry >= 0 &&
      entry < static_cast<int>(cue->effects.size())) {
    cue->effects[entry].bypassed = true;
  }
}
