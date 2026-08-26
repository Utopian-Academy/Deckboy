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
