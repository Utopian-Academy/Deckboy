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

using NumericParam = deckboy::params::NumericParam;

// Which oscillator a source row's ~ drives: core/numeric_params.hpp, so
// Pocket's rows find the same ones.
int moireLfoSlotFor(NumericParam id) const { return deckboy::params::moireLfoSlotFor(id); }
int illusionLfoSlotFor(NumericParam id) const { return deckboy::params::illusionLfoSlotFor(id); }
int lumiaLfoSlotFor(NumericParam id) const { return deckboy::params::lumiaLfoSlotFor(id); }

using NumericParamSpec = deckboy::params::NumericParamSpec;

const NumericParamSpec* numericParamSpec(NumericParam id) const {
  return deckboy::params::numericParamSpec(id, selectedCuePtr());
}

// Read the live value. Returns false when the selected cue does not carry it,
// which is how a stale row (the selection changed mid-click) is refused rather
// than writing into the wrong cue.
bool readNumericParam(const Cue& cue, NumericParam id, double& out) const {
  return deckboy::params::readNumericParam(cue, id, out);
}

void writeNumericParam(Cue& cue, NumericParam id, double value) {
  deckboy::params::writeNumericParam(cue, id, value);
  if (id == NumericParam::MoirePalette) {
      if (cue.moire.palette == 2) {
        const SDL_Color inks[4] = {pal.deep, pal.mid, pal.tile, pal.light};
        for (std::size_t i = 0; i < 4; ++i) {
          cue.moire.themeInks[i] = (static_cast<std::uint32_t>(inks[i].r) << 16) |
                                   (static_cast<std::uint32_t>(inks[i].g) << 8) | inks[i].b;
        }
      }
  }
  if (id == NumericParam::IllusionPalette) {
      if (cue.illusion.palette == 1) {
        const SDL_Color inks[4] = {pal.deep, pal.mid, pal.tile, pal.light};
        for (std::size_t i = 0; i < 4; ++i) {
          cue.illusion.themeInks[i] = (static_cast<std::uint32_t>(inks[i].r) << 16) |
                                      (static_cast<std::uint32_t>(inks[i].g) << 8) | inks[i].b;
        }
      }
  }
}

// The - and + of any row in the table: one step, clamped to the same range
// typed entry uses. Every selected cue that carries the value moves together,
// the way typed entry already does.
// THE MASK'S ROWS edit ONE mask -- the one in hand -- however many cues are
// selected, so they skip the per-cue loops below, which would step a shared
// mask once for every selected cue.
static int maskSlotForParam(NumericParam id) {
  switch (id) {
    case NumericParam::MaskX:        return deckboy::masks::kMaskLfoX;
    case NumericParam::MaskY:        return deckboy::masks::kMaskLfoY;
    case NumericParam::MaskScale:    return deckboy::masks::kMaskLfoScale;
    case NumericParam::MaskRotation: return deckboy::masks::kMaskLfoRotation;
    case NumericParam::MaskFeather:  return deckboy::masks::kMaskLfoFeather;
    case NumericParam::MaskGrow:     return deckboy::masks::kMaskLfoGrow;
    case NumericParam::MaskOpacity:  return deckboy::masks::kMaskLfoOpacity;
    default:                         return -1;
  }
}

static double maskParamUnit(NumericParam id) {
  return id == NumericParam::MaskRotation ? 1.0 : 100.0;   // stored -> shown
}

double maskParamInOperatorUnits(const deckboy::masks::Mask& m, NumericParam id) {
  const int slot = maskSlotForParam(id);
  return slot < 0 ? 0.0 : deckboy::masks::maskParam(m, slot) * maskParamUnit(id);
}

void setMaskParamFromOperatorUnits(NumericParam id, double shown, const NumericParamSpec& spec) {
  deckboy::masks::Mask* m = maskFocus();
  const int slot = maskSlotForParam(id);
  if (!m || slot < 0) return;
  const double next = std::clamp(shown, spec.lo, spec.hi);
  deckboy::masks::maskParam(*m, slot) = static_cast<float>(next / maskParamUnit(id));
  deckboy::masks::clampMaskParams(*m);
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(spec.precision) << next;
  triggerToast(std::string(spec.label) + " " + ss.str());
  markProjectDirty();
}

void nudgeNumericParam(int rawId, int direction) {
  const NumericParam id = static_cast<NumericParam>(rawId);
  const NumericParamSpec* spec = numericParamSpec(id);
  if (!spec) {
    return;
  }
  if (drawBrushParam(id)) {
    const double step = spec->step > 0.0 ? spec->step : (spec->hi - spec->lo) / 50.0;
    setDrawBrushShown(id, drawBrushShown(id) + step * direction, *spec);
    return;
  }
  if (maskSlotForParam(id) >= 0) {
    if (const deckboy::masks::Mask* m = maskFocus()) {
      const double step = spec->step > 0.0 ? spec->step : (spec->hi - spec->lo) / 50.0;
      setMaskParamFromOperatorUnits(id, maskParamInOperatorUnits(*m, id) + step * direction, *spec);
    }
    return;
  }
  const double step = spec->step > 0.0 ? spec->step : (spec->hi - spec->lo) / 50.0;
  bool changed = false;
  double shown = 0.0;
  forEachFocusedSelectedCueMutable([&](Cue& each, int) {
    double current = 0.0;
    if (!readNumericParam(each, id, current)) {
      return;
    }
    const double next = std::clamp(current + step * direction, spec->lo, spec->hi);
    writeNumericParam(each, id, next);
    shown = next;
    changed = true;
  });
  if (!changed) {
    return;
  }
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(spec->precision) << shown;
  triggerToast(std::string(spec->label) + " " + ss.str());
  markProjectDirty();
  if (id == NumericParam::LedPanelWidth || id == NumericParam::LedPanelHeight) {
    redrawLivePanelMap();
  }
}

// The one handler behind QuickAction::EditNumericParam.
void editNumericParam(int rawId) {
  const NumericParam id = static_cast<NumericParam>(rawId);
  const NumericParamSpec* spec = numericParamSpec(id);
  if (!spec) {
    return;
  }
  if (drawBrushParam(id)) {
    std::ostringstream shown;
    shown << std::fixed << std::setprecision(spec->precision) << drawBrushShown(id);
    openInlineNumericExpressionEditor(spec->token, spec->label,
                                      std::string(spec->hint) + " (supports + - * / and ())", shown.str(),
                                      [this, id, spec](double value) { setDrawBrushShown(id, value, *spec); });
    return;
  }
  if (maskSlotForParam(id) >= 0) {
    const deckboy::masks::Mask* m = maskFocus();
    if (!m) return;
    std::ostringstream shown;
    shown << std::fixed << std::setprecision(spec->precision) << maskParamInOperatorUnits(*m, id);
    openInlineNumericExpressionEditor(spec->token, spec->label,
                                      std::string(spec->hint) + " (supports + - * / and ())", shown.str(),
                                      [this, id, spec](double value) {
                                        pushUndoSnapshot();
                                        setMaskParamFromOperatorUnits(id, value, *spec);
                                      });
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
      if (id == NumericParam::LedPanelWidth || id == NumericParam::LedPanelHeight) {
        redrawLivePanelMap();
      }
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
         deckboy::effects::cueEffectStackActive(cue.effects) ||
         // AN AUDIO EFFECT CAN NEED THE PICTURE TOO. The Picture effect reads
         // the frame's brightness, and on the zero-copy path there is no frame
         // in system memory to read -- that is what makes it fast. So a cue
         // whose audio chain follows the picture asks for CPU frames, the same
         // way one with a colour grade does. It is the operator's trade and
         // the inspector says so; the alternative was an effect that silently
         // did nothing on the fast path, which is the failure this codebase
         // keeps having.
         cueAudioChainNeedsPicture(cue);
}

// Whether anything in the cue's audio chain follows the picture.
bool cueAudioChainNeedsPicture(const Cue& cue) {
  return deckboy::audiofx::audioChainNeedsPicture(cue.audioEffects);
}

// Hand the live cue its new look when that answer CHANGES, so the effects get
// pixels they can act on. Only on a change: nothing needs doing on every amount
// nudge. This used to re-take the cue, which froze the picture for the length
// of a decoder reopen on every such edit -- see adoptLookWithoutRestart.
void refreshLiveCueIfPixelPathChanged(bool wasNeeded) {
  const Cue* cue = selectedCueMutable();
  if (!cue) {
    return;
  }
  if (cueNeedsCpuPixelPath(*cue) != wasNeeded) {
    adoptFocusedLiveCueLook();
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
  // THE BASICS FIRST. The list is otherwise in the order effects were added,
  // which put levels and curves -- what an operator reaches for most -- under
  // forty-odd effects nobody has heard of.
  using deckboy::effects::CueEffectKind;
  const CueEffectKind basics[] = {
    CueEffectKind::Levels, CueEffectKind::Curves, CueEffectKind::Colour,
    CueEffectKind::ColourReplace, CueEffectKind::Blur, CueEffectKind::Sharpen,
    CueEffectKind::Glow, CueEffectKind::Mirror, CueEffectKind::Mosaic,
    CueEffectKind::Edges, CueEffectKind::LumaKey, CueEffectKind::Strobe};
  for (CueEffectKind kind : basics) {
    choices.push_back({deckboy::effects::cueEffectToken(kind),
                       deckboy::effects::cueEffectLabel(kind)});
  }
  for (int i = 1; i < static_cast<int>(CueEffectKind::Count); ++i) {
    const auto kind = static_cast<CueEffectKind>(i);
    if (std::find(std::begin(basics), std::end(basics), kind) != std::end(basics)) {
      continue;
    }
    choices.push_back({deckboy::effects::cueEffectToken(kind),
                       deckboy::effects::cueEffectLabel(kind)});
  }
  return choices;
}

// ── Code source ─────────────────────────────────────────────────────────────

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
    deckboy::effects::applyCueEffectArrivalDefaults(fx);
    Cue* liveCue = selectedCueMutable();
    const bool wasNeeded = liveCue ? cueNeedsCpuPixelPath(*liveCue) : false;
    live->push_back(fx);
    triggerToastTranslated(deckboy::core::i18n::format("added {1}", {deckboy::core::i18n::translate(deckboy::effects::cueEffectLabel(fx.kind))}));
    markProjectDirty();
    syncDatamoshFromStack();
    refreshLiveCueIfPixelPathChanged(wasNeeded);
  });
}

// An effect's choice row: the list of what it can be, the current one picked.
void effectStackOpenChoice(int index, int which) {
  auto* stack = selectedEffectStack();
  if (!stack || index < 0 || index >= static_cast<int>(stack->size())) return;
  const auto kind = (*stack)[static_cast<std::size_t>(index)].kind;
  const auto options = deckboy::effects::cueEffectChoiceOptions(kind, which);
  if (options.empty()) return;
  SDL_Rect anchor {};
  const QuickAction action = which == 0 ? QuickAction::EffectChoiceA : QuickAction::EffectChoiceB;
  for (const auto& qb : quickButtons_) {
    if (qb.action == action && qb.param == index && qb.rect.w > anchor.w) anchor = qb.rect;
  }
  std::vector<std::pair<std::string, std::string>> choices;
  for (int k = 0; k < static_cast<int>(options.size()); ++k) choices.emplace_back(std::to_string(k), options[static_cast<std::size_t>(k)]);
  const auto& fx = (*stack)[static_cast<std::size_t>(index)];
  openDropdown("effect.choice", anchor, choices, std::to_string(which == 0 ? fx.choiceA : fx.choiceB),
               [this, index, which](const std::string& value) {
                 auto* live = selectedEffectStack();
                 if (!live || index >= static_cast<int>(live->size())) return;
                 pushUndoSnapshot();
                 auto& target = (*live)[static_cast<std::size_t>(index)];
                 (which == 0 ? target.choiceA : target.choiceB) = std::atoi(value.c_str());
                 markProjectDirty();
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
// ── Parameter LFOs ─────────────────────────────────────────────────────────
//
// Every one of these takes the PACKED id described on the QuickAction enum:
// effectIndex * 8 + slot, slot 0-3 for paramA-D and 4 for the amount.
deckboy::effects::ParamLfo* effectLfoAt(int packed) {
  // An Illusion cue's oscillators: the highest range of all.
  if (packed >= kIllusionLfoPackBase) {
    const int slot = packed - kIllusionLfoPackBase;
    Cue* cue = selectedCueMutable();
    if (!cue || slot < 0 || slot >= kIllusionLfoSlots) return nullptr;
    return &cue->illusion.lfo[static_cast<std::size_t>(slot)];
  }
  // A shader cue's float inputs: the next range down.
  if (packed >= kIsfLfoPackBase) return isfLfoAt(packed - kIsfLfoPackBase);
  // A Lumia cue's oscillators: the highest range, so it is tested first.
  if (packed >= kLumiaLfoPackBase) {
    const int slot = packed - kLumiaLfoPackBase;
    Cue* cue = selectedCueMutable();
    if (!cue || slot < 0 || slot >= kLumiaLfoCount) return nullptr;
    return &cue->lumia.lfo[static_cast<std::size_t>(slot)];
  }
  // The draw brush's size and opacity: the next range down.
  if (packed >= kDrawLfoPackBase) {
    const int slot = packed - kDrawLfoPackBase;
    if (slot < 0 || slot > 1) return nullptr;
    return &project_.drawBrushLfo[static_cast<std::size_t>(slot)];
  }
  // A tone cue's pitch and level: the next range down.
  if (packed >= kToneLfoPackBase) {
    const int slot = packed - kToneLfoPackBase;
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Tone || slot < 0 || slot >= kToneLfoCount) return nullptr;
    return &cue->toneLfo[static_cast<std::size_t>(slot)];
  }
  // The mask in hand: the next range down.
  if (packed >= kMaskLfoPackBase) {
    const int slot = packed - kMaskLfoPackBase;
    deckboy::masks::Mask* m = maskFocus();
    if (!m || slot < 0 || slot >= deckboy::masks::kMaskLfoCount) return nullptr;
    return &m->lfo[static_cast<std::size_t>(slot)];
  }
  // A Moire cue's oscillators: the next range down.
  if (packed >= kMoireLfoPackBase) {
    const int slot = packed - kMoireLfoPackBase;
    Cue* cue = selectedCueMutable();
    if (!cue || slot < 0 || slot >= kMoireLfoCount) return nullptr;
    return &cue->moire.lfo[static_cast<std::size_t>(slot)];
  }
  // A DMX cue's fixture oscillators: their own range, the same controls.
  if (packed >= kFixtureLfoPackBase) {
    const int row = (packed - kFixtureLfoPackBase) / 2;
    const int which = (packed - kFixtureLfoPackBase) % 2;
    CueFixtureLevel* level = selectedDmxFixtureLevel(row);
    if (!level) return nullptr;
    return which == 0 ? &level->intensityLfo : &level->colourLfo;
  }
  // The GEOMETRY oscillators share every LFO control -- shape, rate, depth,
  // sync, the scribble pad -- by living in their own range of the same packed
  // number. One set of handlers rather than a second set that could drift.
  if (packed >= kGeometryLfoPackBase) {
    const int slot = packed - kGeometryLfoPackBase;
    Cue* cue = selectedCueMutable();
    if (!cue || slot < 0 || slot >= kGeoLfoCount) {
      return nullptr;
    }
    return &cue->geometryLfo[static_cast<std::size_t>(slot)];
  }
  auto* stack = selectedEffectStack();
  const int index = packed / 8;
  const int slot = packed % 8;
  if (!effectIndexValid(stack, index) || slot < 0 || slot > 6) {
    return nullptr;
  }
  return &(*stack)[index].lfo[slot];
}

void effectLfoToggle(int packed) {
  if (auto* lfo = effectLfoAt(packed)) {
    lfo->on = !lfo->on;
    // Said out loud, because an LFO is the one control whose effect you might
    // not notice for several seconds -- a slow one at low depth looks exactly
    // like nothing happening until it has had time to move.
    triggerToast(lfo->on ? "lfo on" : "lfo off");
    markProjectDirty();
  }
}

void effectLfoCycleShape(int packed) {
  if (auto* lfo = effectLfoAt(packed)) {
    const int count = static_cast<int>(deckboy::effects::LfoShape::Count);
    lfo->shape = static_cast<deckboy::effects::LfoShape>(
      (static_cast<int>(lfo->shape) + 1) % count);
    triggerToast(deckboy::effects::lfoShapeToken(lfo->shape));
    markProjectDirty();
  }
}

void effectLfoNudgeRate(int packed, int direction) {
  if (auto* lfo = effectLfoAt(packed)) nudgeParamLfoRate(*lfo, direction);
}

// What the rate cell steps, for any oscillator: the CC or channel a MIDI
// shape listens to, the zone a Motion shape follows, beats when synced, or
// hertz. Shared by the inspector's ~ rows and the Screens window's.
void nudgeParamLfoRate(deckboy::effects::ParamLfo& lfoRef, int direction) {
  deckboy::effects::ParamLfo* lfo = &lfoRef;
  if (deckboy::effects::lfoShapeIsAudio(lfo->shape)) {
    // A sound has no rate: this cell says what it listens to.
    lfo->listen = ((lfo->listen + direction) % 3 + 3) % 3;
    triggerToast(lfo->listen == 1   ? std::string("listens to the playlist")
                 : lfo->listen == 2 ? std::string("listens to the live input")
                                    : std::string("listens to the live input while one is open, else the playlist"));
    markProjectDirty();
    return;
  }
  if (lfo->shape == deckboy::effects::LfoShape::MidiCc) {
    lfo->midiNumber = ((lfo->midiNumber + direction) % 128 + 128) % 128;
    triggerToast("follows CC " + std::to_string(lfo->midiNumber));
    markProjectDirty();
    return;
  }
  if (deckboy::effects::lfoShapeIsMidi(lfo->shape)) {
    lfo->midiChannel = ((lfo->midiChannel + direction) % 17 + 17) % 17;
    triggerToast(lfo->midiChannel == 0 ? std::string("listens to any channel")
                                       : "listens to channel " + std::to_string(lfo->midiChannel));
    markProjectDirty();
    return;
  }
  if (deckboy::effects::lfoShapeIsMotion(lfo->shape)) {
    // The zone it follows: the whole picture, then each zone drawn. Wraps,
    // so one button reaches them all.
    const int zones = static_cast<int>(project_.motion.zones.size());
    lfo->motionZone = ((lfo->motionZone + direction) % (zones + 1) + zones + 1) % (zones + 1);
    triggerToast(lfo->motionZone == 0 ? std::string("follows the whole picture")
                                      : "follows zone " + std::to_string(lfo->motionZone));
    markProjectDirty();
    return;
  }
  if (lfo->beatSync) {
    // In beats, the useful values are musical ones. Stepping through the list
    // rather than by a fixed amount means every stop is a length someone would
    // actually choose, and half a beat is reachable without twenty clicks.
    static const float kBeats[] = {0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f,
                                   6.0f, 8.0f, 12.0f, 16.0f, 32.0f};
    const int n = static_cast<int>(sizeof(kBeats) / sizeof(kBeats[0]));
    int at = 0;
    for (int i = 0; i < n; ++i) {
      if (std::fabs(kBeats[i] - lfo->beats) < 0.01f) { at = i; break; }
      if (kBeats[i] < lfo->beats) at = i;
    }
    lfo->beats = kBeats[std::clamp(at + direction, 0, n - 1)];
  } else {
    // Multiplied, not added: the interesting range runs from one cycle a
    // minute to several a second, and a fixed step would take a hundred
    // clicks at one end and skip the whole useful part at the other.
    lfo->rateHz = std::clamp(lfo->rateHz * (direction > 0 ? 1.25f : 0.8f),
                             0.01f, 20.0f);
  }
  markProjectDirty();
}

void effectLfoNudgeDepth(int packed, float delta) {
  if (auto* lfo = effectLfoAt(packed)) {
    lfo->depth = std::clamp(lfo->depth + delta, 0.0f, 1.0f);
    markProjectDirty();
  }
}

// CHLADNI THAT LISTENS (James, 2026-11-06). One switch: the mode's ~ on,
// following the sound's pitch at full depth from the middle, so the figure
// walks through every mode as the music climbs. Off puts the mode back.
void effectChladniListen(int index) {
  auto* stack = selectedEffectStack();
  if (!effectIndexValid(stack, index)) return;
  auto& fx = (*stack)[static_cast<std::size_t>(index)];
  auto& lfo = fx.lfo[0];
  const bool listening = lfo.on && lfo.shape == deckboy::effects::LfoShape::AudioPitch;
  pushUndoSnapshot();
  if (listening) {
    lfo.on = false;
    triggerToast("Chladni stops listening");
  } else {
    lfo.on = true;
    lfo.shape = deckboy::effects::LfoShape::AudioPitch;
    lfo.depth = 1.0f;
    fx.paramA = 0.5f;
    triggerToast("Chladni listens: the figure follows the pitch of the sound");
  }
  markProjectDirty();
}

void effectLfoToggleSync(int packed) {
  if (auto* lfo = effectLfoAt(packed)) {
    // On MIDI CC this row is the channel (the cell above is the CC).
    if (lfo->shape == deckboy::effects::LfoShape::MidiCc) {
      lfo->midiChannel = (lfo->midiChannel + 1) % 17;
      triggerToast(lfo->midiChannel == 0 ? std::string("listens to any channel")
                                         : "listens to channel " + std::to_string(lfo->midiChannel));
      markProjectDirty();
      return;
    }
    lfo->beatSync = !lfo->beatSync;
    triggerToast(lfo->beatSync ? "lfo follows the tempo" : "lfo runs free");
    markProjectDirty();
  }
}

// An effect's (or audio effect's) amount and dials: 5% a press of - or +,
// 1% a step while the value is being dragged.
float effectDialStep() const { return valueScrubEngaged_ ? 0.01f : 0.05f; }

static bool isEffectDialAction(QuickAction a) {
  switch (a) {
    case QuickAction::EffectAmountInc: case QuickAction::EffectAmountDec:
    case QuickAction::EffectParamAInc: case QuickAction::EffectParamADec:
    case QuickAction::EffectParamBInc: case QuickAction::EffectParamBDec:
    case QuickAction::EffectParamCInc: case QuickAction::EffectParamCDec:
    case QuickAction::EffectParamDInc: case QuickAction::EffectParamDDec:
    case QuickAction::EffectParamEInc: case QuickAction::EffectParamEDec:
    case QuickAction::EffectParamFInc: case QuickAction::EffectParamFDec:
    case QuickAction::AudioEffectAmountInc: case QuickAction::AudioEffectAmountDec:
    case QuickAction::AudioEffectParamAInc: case QuickAction::AudioEffectParamADec:
    case QuickAction::AudioEffectParamBInc: case QuickAction::AudioEffectParamBDec:
    case QuickAction::AudioEffectParamCInc: case QuickAction::AudioEffectParamCDec:
    case QuickAction::AudioEffectParamDInc: case QuickAction::AudioEffectParamDDec:
      return true;
    default:
      return false;
  }
}

void effectStackNudgeParam(int index, int which, float delta) {
  auto* stack = selectedEffectStack();
  if (!effectIndexValid(stack, index)) {
    return;
  }
  auto& fx = (*stack)[index];
  float& value = deckboy::effects::cueEffectParam(fx, which);
  value = std::clamp(value + delta, 0.0f, 1.0f);
  markProjectDirty();
}

void effectStackEditParam(int index, int which) {
  auto* stack = selectedEffectStack();
  if (!effectIndexValid(stack, index)) {
    return;
  }
  const auto& fx = (*stack)[index];
  const char* label = deckboy::effects::cueEffectParamLabelFor(fx, which);
  if (!label) {
    return;   // this effect has no such parameter; nothing to type into
  }
  // SAME UNITS THE ROW IS SHOWING. inspEffectParamValueText draws every
  // parameter as a percentage except text mode's three selector slots, which
  // read as "100 cols", a glyph set name and an ink name. Opening a 0-1 editor
  // over a row that says 40% is how someone types 45, means 45%, and gets the
  // parameter pinned to its maximum.
  const bool percentUnits =
    !(fx.kind == deckboy::effects::CueEffectKind::TextMode &&
      (which == 0 || which == 2 || which == 3));
  const float raw = deckboy::effects::cueEffectParam(fx, which);
  std::ostringstream current;
  if (percentUnits) {
    current << static_cast<int>(std::lround(raw * 100.0f));
  } else {
    current << std::fixed << std::setprecision(2) << raw;
  }
  openInlineNumericExpressionEditor(
    which == 0 ? "cue.effect.paramA" : which == 1 ? "cue.effect.paramB"
    : which == 2 ? "cue.effect.paramC" : which == 3 ? "cue.effect.paramD"
    : which == 4 ? "cue.effect.paramE" : "cue.effect.paramF", label,
    percentUnits ? "0-100% (supports + - * / and ())"
                 : "0-1 (supports + - * / and ())",
    current.str(),
    [this, index, which, percentUnits](double value) {
      auto* live = selectedEffectStack();
      if (!effectIndexValid(live, index)) {
        return;   // the selection moved while the editor was open
      }
      auto& target = (*live)[index];
      deckboy::effects::cueEffectParam(target, which) =
        std::clamp(static_cast<float>(percentUnits ? value / 100.0 : value),
                   0.0f, 1.0f);
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
  // PERCENT, BECAUSE THAT IS WHAT THE ROW SAYS.
  //
  // The row reads "35%" and this editor used to open on "0.35" and ask for a
  // number between 0 and 1. So the operator who saw 35%, wanted 37, and typed
  // 37 got the value clamped to 1.0 -- the effect jumped to FULL. Two notations
  // for one quantity, a keystroke apart, and the failure looks exactly like a
  // control that refuses to take a specific value.
  //
  // The nudge buttons and the scrub step in fives, which is right for a thumb
  // on a fader; typing is how an exact value is set, so it has to accept the
  // units it is showing.
  std::ostringstream current;
  current << static_cast<int>(std::lround((*stack)[index].amount * 100.0f));
  openInlineNumericExpressionEditor(
    "cue.effect.amount", deckboy::effects::cueEffectLabel((*stack)[index].kind),
    "Amount 0-100% (supports + - * / and ())", current.str(),
    [this, index](double value) {
      auto* live = selectedEffectStack();
      if (!effectIndexValid(live, index)) {
        return;   // the selection moved while the editor was open
      }
      (*live)[index].amount =
        std::clamp(static_cast<float>(value / 100.0), 0.0f, 1.0f);
      markProjectDirty();
    });
}

// ---------------------------------------------------------------------------
// The audio effect stack.
// ---------------------------------------------------------------------------
//
// The same shape as the picture stack above, and deliberately so: an operator
// who has learned one has learned the other. The differences are all about
// what audio is:
//
//   - NO CPU-PATH REFRESH. A picture effect can require the decoder to be
//     reopened in a format the effect can act on, which is why every edit up
//     there checks. Audio is always decoded to PCM, so an edit here reaches
//     the very next chunk with nothing to restart.
//   - THE EDIT REACHES EVERY SELECTED CUE, the way gain, pan and mono already
//     do. Same reason: levels and room want to agree across a set of cues, and
//     doing that one cue at a time is how they end up not agreeing.
//   - A CAP OF EIGHT. The chain runs per sample on the audio thread, where
//     overrunning does not drop a frame -- it clicks.

std::vector<deckboy::audiofx::AudioEffect>* selectedAudioEffectStack() {
  Cue* cue = selectedCueMutable();
  return cue ? &cue->audioEffects : nullptr;
}

bool audioEffectIndexValid(const std::vector<deckboy::audiofx::AudioEffect>* stack,
                           int index) {
  return stack && index >= 0 && index < static_cast<int>(stack->size());
}

// Apply an edit to the same set of cues every other audio edit reaches, and
// report whether it landed anywhere. The callback is handed one stack at a
// time. A cue whose chain is SHORTER than the edited position is skipped
// rather than grown: inventing an effect in somebody else's cue so it has
// somewhere to receive an edit is not what "apply to the selection" means.
bool forEachSelectedAudioStack(
    const std::function<void(std::vector<deckboy::audiofx::AudioEffect>&)>& edit) {
  // COUNT THE CUES EDITED, not the decks visited.
  //
  // forEachSelectedCueEverywhere returns how many DECKS it reached a selected
  // cue in, which is the right number for the "(2 decks)" toast suffix and the
  // wrong one for "did this do anything". Every cue in the selection can be
  // skipped for having no audio and the deck count is still 1 -- so the
  // "no cue with audio selected" message below could never appear, and adding
  // an effect to a silent cue would have reported success.
  int edited = 0;
  lastAudioEditDeckCount_ = forEachSelectedCueEverywhere([&](Cue& each, int) {
    if (cueCarriesAudio(each)) {
      edit(each.audioEffects);
      ++edited;
    }
  });
  if (edited > 0) {
    markProjectDirty();
  }
  return edited > 0;
}

// Every audio effect, as dropdown choices, built FROM the enum rather than
// from a second list beside it that can fall behind.
std::vector<std::pair<std::string, std::string>> audioEffectChoices() {
  std::vector<std::pair<std::string, std::string>> choices;
  for (int i = 1; i < static_cast<int>(deckboy::audiofx::AudioEffectKind::Count); ++i) {
    const auto kind = static_cast<deckboy::audiofx::AudioEffectKind>(i);
    choices.push_back({deckboy::audiofx::audioEffectToken(kind),
                       deckboy::audiofx::audioEffectLabel(kind)});
  }
  return choices;
}

void audioEffectStackAdd() {
  auto* stack = selectedAudioEffectStack();
  if (!stack) {
    return;
  }
  if (stack->size() >= 8) {
    triggerToast("audio chain full (8)");
    return;
  }
  // PICK from the list, like the picture stack does. Appending a default and
  // making the operator cycle to what they wanted was the worst part of the
  // first version of that one.
  const auto choices = audioEffectChoices();
  openDropdown("cue.audiofx.add", lastInlineEditorAnchorRect_, choices,
               choices.front().first,
               [this](const std::string& token) {
    const auto kind = deckboy::audiofx::audioEffectKindFromToken(token);
    if (kind == deckboy::audiofx::AudioEffectKind::None) {
      return;
    }
    // audioEffectDefaults, NOT the struct's own: those are the neutral values
    // an old show carries, and for half of these that means a compressor at
    // 1:1 and a tilt that is flat. An effect that arrives doing nothing at all
    // looks exactly like an effect that is broken.
    const deckboy::audiofx::AudioEffect fx =
      deckboy::audiofx::audioEffectDefaults(kind);
    const Cue* before = selectedCueMutable();
    const bool wasNeeded = before ? cueNeedsCpuPixelPath(*before) : false;
    const bool any = forEachSelectedAudioStack(
      [&fx](std::vector<deckboy::audiofx::AudioEffect>& s) {
        if (s.size() < 8) {
          s.push_back(fx);
        }
      });
    // Adding a Picture effect to a LIVE cue changes which format the decoder
    // has to be open in, and the decode format is chosen at TAKE. Without this
    // the effect would do nothing at all until the next one -- which looks
    // exactly like a broken effect and is how the picture stack's version of
    // this was reported.
    refreshLiveCueIfPixelPathChanged(wasNeeded);
    if (any) {
      triggerToastTranslated(deckboy::core::i18n::format(
                     "added {1}",
                     {deckboy::core::i18n::translate(deckboy::audiofx::audioEffectLabel(kind))}) +
                   deckboy::core::i18n::translate(audioEditScopeSuffix()));
    } else {
      triggerToast("no cue with audio selected");
    }
  });
}

// ── THE OPERATOR'S OWN PLUGINS ──────────────────────────────────────────────
//
// Scanned once and kept: the scan is filesystem work rather than module
// loading, but a machine with a hundred and twenty plugins across two folders
// on a slow drive should still only pay for it once. Refreshed when the
// operator asks for the list and it came back empty, because the usual reason
// for that is a plugin installed while Deckboy was already running.
std::vector<deckboy::platform::audioplugin::PluginDescriptor> audioPluginCatalog_;
bool audioPluginCatalogScanned_ = false;

const std::vector<deckboy::platform::audioplugin::PluginDescriptor>&
audioPluginCatalog(bool rescan = false) {
  if (!audioPluginCatalogScanned_ || rescan || audioPluginCatalog_.empty()) {
    audioPluginCatalog_ = deckboy::platform::audioplugin::scanAudioPlugins();
    audioPluginCatalogScanned_ = true;
  }
  return audioPluginCatalog_;
}

// What a plugin slot calls itself in the UI: the plugin's name where there is
// one, the id where the plugin is not installed on this machine (so a touring
// show says WHICH plugin it is missing rather than "none"), and an invitation
// where nothing has been chosen yet.
std::string audioPluginSlotLabel(const deckboy::audiofx::AudioEffect& fx) {
  if (fx.pluginId.empty()) {
    return "choose...";
  }
  for (const auto& d : audioPluginCatalog_) {
    if (d.id == fx.pluginId) {
      return d.vendor.empty() ? d.name : (d.vendor + " " + d.name);
    }
  }
  // Not in the catalog: show the tail of the id, which is the plugin's own
  // file name and the thing an operator would recognise.
  const std::size_t slash = fx.pluginId.find_last_of("/\\");
  const std::string tail = slash == std::string::npos
                             ? fx.pluginId : fx.pluginId.substr(slash + 1);
  return tail + "  (not installed)";
}

// The plugin actually LOADED in a chain position, or null. Only the live cue
// on the focused deck has instances -- a cue sitting in the playlist has an id
// and some saved state and nothing running -- so this answers null unless the
// cue the inspector is showing is the one playing. The rows then fall back to
// the generic control names, which is the honest answer: nothing is loaded, so
// nothing can be asked what its knobs are called.
std::shared_ptr<deckboy::platform::audioplugin::AudioPluginInstance>
audioPluginInstanceForRow(int index) {
  if (project_.focusedDeckIndex < 0 ||
      project_.focusedDeckIndex >= static_cast<int>(project_.decks.size())) {
    return nullptr;
  }
  const Deck& deck = focusedDeck();
  if (deck.activeIndex < 0 ||
      deck.activeIndex >= static_cast<int>(deck.cues.size()) ||
      !cueIndexSelected(deck, deck.activeIndex)) {
    return nullptr;
  }
  MediaEngine* engine = focusedMediaEngine();
  return engine ? engine->audioPluginForSlot(index) : nullptr;
}

void audioEffectStackChoosePlugin(int index) {
  auto* stack = selectedAudioEffectStack();
  if (!audioEffectIndexValid(stack, index)) {
    return;
  }
  if (!deckboy::platform::audioplugin::audioPluginsSupported()) {
    // Never an empty list: "you own none" and "this build cannot load them"
    // look identical to an operator, and only one of them is fixable by them.
    triggerToast("this build has no plugin support");
    return;
  }
  const auto& catalog = audioPluginCatalog(true);
  if (catalog.empty()) {
    const auto paths = deckboy::platform::audioplugin::audioPluginSearchPaths();
    triggerToast(paths.empty() ? "no plugins found"
                               : ("no plugins found in " + paths.front()));
    return;
  }
  std::vector<std::pair<std::string, std::string>> choices;
  choices.reserve(catalog.size() + 1);
  choices.push_back({"", "(none)"});
  for (const auto& d : catalog) {
    choices.push_back({d.id, d.vendor.empty() ? d.name : (d.vendor + " " + d.name)});
  }
  openDropdown("cue.audiofx.plugin", lastInlineEditorAnchorRect_, choices,
               (*stack)[index].pluginId,
               [this, index](const std::string& id) {
    // THE PARAMETERS COME FROM THE PLUGIN. Deckboy's own effects arrive at
    // audioEffectDefaults, but a plugin's idea of neutral is its own -- a
    // compressor at 50% threshold is not the same claim as a filter at 50%.
    // So the four mapped controls are seeded by opening the plugin once, here,
    // on the main thread, and reading what it says its defaults are.
    float seeded[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    std::string name = "none";
    if (!id.empty()) {
      auto probe = deckboy::platform::audioplugin::openAudioPlugin(id, 48000.0, 2048);
      if (!probe) {
        triggerToast("could not load that plugin");
        return;
      }
      int taken = 0;
      for (const auto& p : probe->parameters()) {
        if (!p.automatable) {
          continue;
        }
        if (taken >= 4) {
          break;
        }
        seeded[taken++] = static_cast<float>(
          std::clamp(p.defaultValue, 0.0, 1.0));
      }
      name = probe->descriptor().name;
    }
    forEachSelectedAudioStack(
      [index, &id, &seeded](std::vector<deckboy::audiofx::AudioEffect>& s) {
        if (index >= static_cast<int>(s.size()) ||
            s[index].kind != deckboy::audiofx::AudioEffectKind::Plugin) {
          return;
        }
        s[index].pluginId = id;
        // A different plugin's state means nothing to this one, and handing it
        // over is how a plugin ends up in a state its own author never saw.
        s[index].pluginState.clear();
        s[index].paramA = seeded[0];
        s[index].paramB = seeded[1];
        s[index].paramC = seeded[2];
        s[index].paramD = seeded[3];
      });
    triggerToast(id.empty() ? "plugin slot cleared" : ("loaded " + name));
  });
}

void audioEffectStackRemove(int index) {
  auto* stack = selectedAudioEffectStack();
  if (!audioEffectIndexValid(stack, index)) {
    return;
  }
  const Cue* beforeCue = selectedCueMutable();
  const bool wasNeeded = beforeCue ? cueNeedsCpuPixelPath(*beforeCue) : false;
  const std::string gone =
    deckboy::audiofx::audioEffectLabel((*stack)[index].kind);
  forEachSelectedAudioStack([index](std::vector<deckboy::audiofx::AudioEffect>& s) {
    if (index < static_cast<int>(s.size())) {
      s.erase(s.begin() + index);
    }
  });
  triggerToast("audio effect removed: " + gone);
  refreshLiveCueIfPixelPathChanged(wasNeeded);
}

void audioEffectStackCycleKind(int index) {
  auto* stack = selectedAudioEffectStack();
  if (!audioEffectIndexValid(stack, index)) {
    return;
  }
  const auto choices = audioEffectChoices();
  openDropdown("cue.audiofx.kind", lastInlineEditorAnchorRect_, choices,
               deckboy::audiofx::audioEffectToken((*stack)[index].kind),
               [this, index](const std::string& token) {
    const auto kind = deckboy::audiofx::audioEffectKindFromToken(token);
    if (kind == deckboy::audiofx::AudioEffectKind::None) {
      return;
    }
    // AND ITS PARAMETERS, because slot 0 means "frequency" on a filter and
    // "threshold" on a compressor: carrying 30% across turns an 80Hz high pass
    // into a compressor at -42 dBFS, which is not what anyone asked for.
    // Bypass and position are kept -- those are about the CHAIN, not the
    // effect.
    // INSIDE the callback, because that is where the edit happens -- the call
    // that opens the dropdown returns long before the operator has chosen.
    const Cue* beforeCue = selectedCueMutable();
    const bool wasNeeded = beforeCue ? cueNeedsCpuPixelPath(*beforeCue) : false;
    forEachSelectedAudioStack(
      [index, kind](std::vector<deckboy::audiofx::AudioEffect>& s) {
        if (index < static_cast<int>(s.size())) {
          const bool wasBypassed = s[index].bypassed;
          s[index] = deckboy::audiofx::audioEffectDefaults(kind);
          s[index].bypassed = wasBypassed;
        }
      });
    refreshLiveCueIfPixelPathChanged(wasNeeded);
  });
}

void audioEffectStackToggleBypass(int index) {
  auto* stack = selectedAudioEffectStack();
  if (!audioEffectIndexValid(stack, index)) {
    return;
  }
  const Cue* beforeCue = selectedCueMutable();
  const bool wasNeeded = beforeCue ? cueNeedsCpuPixelPath(*beforeCue) : false;
  // Read the NEW state off the selected cue and write that same state
  // everywhere, rather than flipping each in turn: a mixed selection would
  // otherwise get further apart with every click instead of converging.
  const bool next = !(*stack)[index].bypassed;
  const std::string name =
    deckboy::audiofx::audioEffectLabel((*stack)[index].kind);
  forEachSelectedAudioStack(
    [index, next](std::vector<deckboy::audiofx::AudioEffect>& s) {
      if (index < static_cast<int>(s.size())) {
        s[index].bypassed = next;
      }
    });
  // Bypass RETURNS the setting; turning the amount to zero throws it away.
  triggerToast(name + (next ? " bypassed" : " active"));
  refreshLiveCueIfPixelPathChanged(wasNeeded);
}

void audioEffectStackNudge(int index, float delta) {
  auto* stack = selectedAudioEffectStack();
  if (!audioEffectIndexValid(stack, index)) {
    return;
  }
  const Cue* beforeCue = selectedCueMutable();
  const bool wasNeeded = beforeCue ? cueNeedsCpuPixelPath(*beforeCue) : false;
  const float next = std::clamp((*stack)[index].amount + delta, 0.0f, 1.0f);
  forEachSelectedAudioStack(
    [index, next](std::vector<deckboy::audiofx::AudioEffect>& s) {
      if (index < static_cast<int>(s.size())) {
        s[index].amount = next;
      }
    });
  refreshLiveCueIfPixelPathChanged(wasNeeded);
}

// The value is computed once from the selected cue and written to all of them,
// for the same reason bypass is: a relative nudge applied to a mixed selection
// keeps them mixed forever.
void audioEffectStackNudgeParam(int index, int which, float delta) {
  auto* stack = selectedAudioEffectStack();
  if (!audioEffectIndexValid(stack, index)) {
    return;
  }
  const auto& fx = (*stack)[index];
  const float current = which == 0 ? fx.paramA : which == 1 ? fx.paramB
                      : which == 2 ? fx.paramC : fx.paramD;
  const float next = std::clamp(current + delta, 0.0f, 1.0f);
  forEachSelectedAudioStack(
    [index, which, next](std::vector<deckboy::audiofx::AudioEffect>& s) {
      if (index >= static_cast<int>(s.size())) {
        return;
      }
      auto& target = s[index];
      (which == 0 ? target.paramA : which == 1 ? target.paramB
       : which == 2 ? target.paramC : target.paramD) = next;
    });
}

void audioEffectStackEditParam(int index, int which) {
  auto* stack = selectedAudioEffectStack();
  if (!audioEffectIndexValid(stack, index)) {
    return;
  }
  const auto& fx = (*stack)[index];
  const char* label = deckboy::audiofx::audioEffectParamLabel(fx.kind, which);
  if (!label) {
    return;   // this effect has no such parameter; nothing to type into
  }
  // PERCENT, because that is what the row shows. The picture stack already
  // fell into the other version of this: a row reading 35% opening an editor
  // that wanted 0.35, so a typed 37 pinned the parameter to its maximum.
  const float raw = which == 0 ? fx.paramA : which == 1 ? fx.paramB
                  : which == 2 ? fx.paramC : fx.paramD;
  std::ostringstream current;
  current << static_cast<int>(std::lround(raw * 100.0f));
  openInlineNumericExpressionEditor(
    which == 0 ? "cue.audiofx.paramA" : which == 1 ? "cue.audiofx.paramB"
    : which == 2 ? "cue.audiofx.paramC" : "cue.audiofx.paramD", label,
    "0-100% (supports + - * / and ())", current.str(),
    [this, index, which](double value) {
      const float next =
        std::clamp(static_cast<float>(value / 100.0), 0.0f, 1.0f);
      forEachSelectedAudioStack(
        [index, which, next](std::vector<deckboy::audiofx::AudioEffect>& s) {
          if (index >= static_cast<int>(s.size())) {
            return;
          }
          auto& target = s[index];
          (which == 0 ? target.paramA : which == 1 ? target.paramB
           : which == 2 ? target.paramC : target.paramD) = next;
        });
    });
}

void audioEffectStackEditAmount(int index) {
  auto* stack = selectedAudioEffectStack();
  if (!audioEffectIndexValid(stack, index)) {
    return;
  }
  std::ostringstream current;
  current << static_cast<int>(std::lround((*stack)[index].amount * 100.0f));
  openInlineNumericExpressionEditor(
    "cue.audiofx.amount",
    deckboy::audiofx::audioEffectLabel((*stack)[index].kind),
    "Amount 0-100% (supports + - * / and ())", current.str(),
    [this, index](double value) {
      const float next =
        std::clamp(static_cast<float>(value / 100.0), 0.0f, 1.0f);
      forEachSelectedAudioStack(
        [index, next](std::vector<deckboy::audiofx::AudioEffect>& s) {
          if (index < static_cast<int>(s.size())) {
            s[index].amount = next;
          }
        });
    });
}

void audioEffectStackMove(int index, int direction) {
  auto* stack = selectedAudioEffectStack();
  if (!audioEffectIndexValid(stack, index)) {
    return;
  }
  const int target = index + direction;
  if (target < 0 || target >= static_cast<int>(stack->size())) {
    return;
  }
  // ORDER IS THE SOUND. A gate before a compressor and a compressor before a
  // gate are two different results, and both are things people want -- so this
  // is a real edit, not a cosmetic reshuffle.
  forEachSelectedAudioStack(
    [index, target](std::vector<deckboy::audiofx::AudioEffect>& s) {
      if (index < static_cast<int>(s.size()) &&
          target < static_cast<int>(s.size())) {
        std::swap(s[index], s[target]);
      }
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

// The sound's pitch for the audio-pitch ~ shape: the focused playlist's
// played sound, read only while some oscillator asks for it. Held through
// silence and smoothed a little, so a breath between notes does not throw
// the figure about.
void serviceAudioPitch() {
  auto& heard = deckboy::effects::lfoAudioReadings();
  // The playlist's: the focused playlist's played sound, from the engine's
  // recent copy.
  if (heard[1].pitchWanted.exchange(false, std::memory_order_relaxed)) {
    MediaEngine* engine = mediaEngineForDeck(project_.focusedDeckIndex);
    if (engine && engine->copyRecentProgramAudio(audioPitchScratch_) >= 1024) {
      // The newest third of a second is enough for C2 and keeps it responsive.
      const std::size_t n = std::min<std::size_t>(audioPitchScratch_.size(), 2048);
      // The recent copy is decimated by 8.
      followAudioPitch(1, audioPitchScratch_.data() + (audioPitchScratch_.size() - n), n,
                       engine->audioRate() / 8.0);
    }
  }
  // The live input's: its own history, brought down to the same rate.
  if (heard[2].pitchWanted.exchange(false, std::memory_order_relaxed) && lfoInputHistory_.size() >= 8192) {
    const std::size_t n = std::min<std::size_t>(lfoInputHistory_.size() / 8, 2048);
    audioPitchScratch_.resize(n);
    const float* from = lfoInputHistory_.data() + (lfoInputHistory_.size() - n * 8);
    for (std::size_t i = 0; i < n; ++i) {
      float sum = 0.0f;
      for (std::size_t k = 0; k < 8; ++k) sum += from[i * 8 + k];
      audioPitchScratch_[i] = sum / 8.0f;
    }
    followAudioPitch(2, audioPitchScratch_.data(), n, static_cast<double>(kAudioRate) / 8.0);
  }
}

// Held through silence and noise (no clear pitch keeps the last one), and
// smoothed by half, so a breath between notes does not throw the figure about.
void followAudioPitch(int source, const float* x, std::size_t n, double rate) {
  const float unit = deckboy::effects::pitchToLfoUnit(deckboy::effects::detectPitchHz(x, n, rate));
  if (unit < 0.0f) return;
  auto& sig = deckboy::effects::lfoAudioReadings()[static_cast<std::size_t>(source)].pitch;
  const float last = sig.load(std::memory_order_relaxed);
  sig.store(last + (unit - last) * 0.5f, std::memory_order_relaxed);
}

// ── WHAT THE ~ AUDIO SHAPES HEAR ────────────────────────────────────────────
//
// James, 2026-11-15, from Ponderosa ("i love 1 2 and 3", then "1 and 2 this
// release"): the sound in three bands, and a ~ that can listen to a live
// input instead of the playlist. Each source keeps a short mono history,
// newest last: the playlist's is filled on the engine's audio thread, the
// input's here. A band is analysed only while some ~ has read it in the last
// two seconds (engine threads read at their own pace, so not "this frame").

static constexpr std::size_t kLfoPlaylistHistoryFrames = 4096;
static constexpr std::size_t kLfoInputHistoryFrames = 16384;  // pitch reads a third of a second

static void appendLfoHistory(std::vector<float>& history, const std::int16_t* stereo,
                             std::size_t frames, std::size_t keep) {
  history.reserve(keep + frames);
  for (std::size_t i = 0; i < frames; ++i) {
    history.push_back(static_cast<float>((static_cast<int>(stereo[2 * i]) + stereo[2 * i + 1]) / 65536.0));
  }
  if (history.size() > keep) {
    history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - keep));
  }
}

// What an audio shape's middle cell says: what it listens to. AUTO says which
// of the two it hears right now.
std::string lfoListenLabel(const deckboy::effects::ParamLfo& lfo) const {
  if (lfo.listen == 1) return "playlist";
  if (lfo.listen == 2) return "live input";
  return audioInputRunning() ? "auto: input" : "auto: playlist";
}

// The live input for a ~ that listens to it while Settings > AUDIO INPUT has
// none open: the Settings device (or the system's), opened to LISTEN ONLY.
// Nothing it hears reaches the speakers, the programme, a stream or a
// recording, and it closes three seconds after the last ~ stops reading it.
bool openLfoInput() {
  closeLfoInput();
  SDL_AudioSpec desired {};
  desired.freq = kAudioRate;
  desired.format = SDL_AUDIO_S16;
  desired.channels = 2;
  std::string device = project_.audioInputDeviceName;
  SDL_AudioDeviceID target = audioRecordingDeviceIdForName(device);
  if (target == 0) {
    device.clear();
    target = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
  }
  SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(target, &desired, nullptr, nullptr);
  if (!stream && target != SDL_AUDIO_DEVICE_DEFAULT_RECORDING) {
    device.clear();
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &desired, nullptr, nullptr);
  }
  if (!stream) {
    // Tried again every ten seconds, so a mic plugged in later is found; said once.
    lfoInputRetryAtMs_ = SDL_GetTicks() + 10000;
    if (!lfoInputFailSaid_) {
      lfoInputFailSaid_ = true;
      triggerToast(deckboy::core::i18n::format("~ cannot open the live input: {1}", {std::string(SDL_GetError())}),
                   ToastKind::Warning, kToastReadableMs);
    }
    showLog("LFO INPUT FAIL", SDL_GetError());
    return false;
  }
  // A recording stream opens paused, like any other.
  deckboySetAudioPaused(stream, false);
  lfoInputStream_ = stream;
  lfoInputPeak_ = 0.0;
  lfoInputFailSaid_ = false;
  const std::string shown = device.empty() ? "(system default)" : device;
  triggerToast(deckboy::core::i18n::format("~ listens to the live input: {1}", {shown}));
  showLog("LFO INPUT", shown);
  return true;
}

void closeLfoInput() {
  if (!lfoInputStream_) return;
  SDL_DestroyAudioStream(lfoInputStream_);
  lfoInputStream_ = nullptr;
  lfoInputPeak_ = 0.0;
}

// What the listen-only input caught since the last frame, at the Settings
// input's gain, into the ~ history; its level moves like the input meter's.
void pumpLfoInput() {
  if (!lfoInputStream_) return;
  const int avail = SDL_GetAudioStreamAvailable(lfoInputStream_);
  if (avail <= 0) return;
  lfoInputScratch_.resize(static_cast<std::size_t>(avail) / sizeof(std::int16_t));
  const int got = SDL_GetAudioStreamData(lfoInputStream_, lfoInputScratch_.data(), avail);
  if (got <= 0) return;
  const std::size_t n = static_cast<std::size_t>(got) / sizeof(std::int16_t);
  const double gain = std::pow(10.0, std::clamp(project_.audioInputGainDb, -40.0, 40.0) / 20.0);
  double peak = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double scaled = std::clamp(lfoInputScratch_[i] * gain, -32768.0, 32767.0);
    lfoInputScratch_[i] = static_cast<std::int16_t>(scaled);
    peak = std::max(peak, std::abs(scaled) / 32768.0);
  }
  appendLfoHistory(lfoInputHistory_, lfoInputScratch_.data(), n / 2, kLfoInputHistoryFrames);
  lfoInputPeak_ = peak > lfoInputPeak_ ? peak : lfoInputPeak_ + (peak - lfoInputPeak_) * 0.08;
}

void followLfoBands(int source, const std::vector<float>& history, double dt) {
  const std::size_t n = std::min(history.size(), deckboy::audiofx::kBandFrames);
  const auto amplitude = deckboy::audiofx::threeBandAmplitudes(
    n == 0 ? nullptr : history.data() + (history.size() - n), n, static_cast<double>(kAudioRate));
  auto& heard = deckboy::effects::lfoAudioReadings()[static_cast<std::size_t>(source)];
  auto& follow = lfoBandFollowers_[static_cast<std::size_t>(source)];
  for (std::size_t b = 0; b < 3; ++b) {
    heard.band[b].store(static_cast<float>(follow[b].step(amplitude[b], dt)), std::memory_order_relaxed);
  }
}

void serviceLfoAudio() {
  auto& heard = deckboy::effects::lfoAudioReadings();
  deckboy::effects::lfoAudioAutoHearsInput().store(audioInputRunning(), std::memory_order_relaxed);
  const Uint64 now = SDL_GetTicks();
  const double dt = lfoAudioLastMs_ != 0
                      ? std::clamp(static_cast<double>(now - lfoAudioLastMs_) / 1000.0, 0.0, 0.25)
                      : 1.0 / 60.0;
  lfoAudioLastMs_ = now;
  const auto readLately = [now](bool stamped, Uint64& at, Uint64 windowMs) {
    if (stamped) at = now;
    return at != 0 && now - at < windowMs;
  };

  // THE PLAYLIST. Its level is the reactive level's playlist half, kept
  // whether an input is open or not.
  heard[1].level.store(static_cast<float>(playlistLevel_), std::memory_order_relaxed);
  heard[1].levelWanted.store(false, std::memory_order_relaxed);
  const bool playlistBands =
    readLately(heard[1].bandsWanted.exchange(false, std::memory_order_relaxed), lfoPlaylistBandsAtMs_, 2000);
  lfoPlaylistHistoryWanted_.store(playlistBands, std::memory_order_relaxed);
  if (playlistBands) {
    {
      std::lock_guard<std::mutex> lock(vuSamplesMutex_);
      lfoAudioScratch_ = lfoPlaylistHistory_;
    }
    followLfoBands(1, lfoAudioScratch_, dt);
  }

  // THE LIVE INPUT: Settings' own input while it is open, otherwise one
  // opened to listen only, while a ~ reads it.
  const bool levelAsked = heard[2].levelWanted.exchange(false, std::memory_order_relaxed);
  const bool bandsAsked = heard[2].bandsWanted.exchange(false, std::memory_order_relaxed);
  const bool pitchAsked = heard[2].pitchWanted.load(std::memory_order_relaxed);  // serviceAudioPitch clears it
  const bool inputWanted = readLately(levelAsked || bandsAsked || pitchAsked, lfoInputWantedAtMs_, 3000);
  lfoInputHistoryWanted_ = inputWanted;
  if (audioInputRunning() || !inputWanted) {
    closeLfoInput();
  } else if (!lfoInputStream_ && now >= lfoInputRetryAtMs_) {
    openLfoInput();
  }
  pumpLfoInput();
  if (!inputWanted || (!audioInputRunning() && !lfoInputStream_)) lfoInputHistory_.clear();
  heard[2].level.store(static_cast<float>(audioInputRunning() ? audioInputPeak_ : lfoInputPeak_),
                       std::memory_order_relaxed);
  if (inputWanted) followLfoBands(2, lfoInputHistory_, dt);
}

// STATUS: what the ~ audio shapes hear, per source.
std::string lfoAudioHeardJson() const {
  const auto& heard = deckboy::effects::lfoAudioReadings();
  std::ostringstream j;
  j << std::fixed << std::setprecision(3);
  j << "{\"auto\":\"" << (audioInputRunning() ? "input" : "playlist") << "\"";
  for (std::size_t s = 1; s <= 2; ++s) {
    const auto& r = heard[s];
    j << ",\"" << (s == 1 ? "playlist" : "input") << "\":{"
      << "\"level\":" << r.level.load(std::memory_order_relaxed)
      << ",\"low\":" << r.band[0].load(std::memory_order_relaxed)
      << ",\"mid\":" << r.band[1].load(std::memory_order_relaxed)
      << ",\"high\":" << r.band[2].load(std::memory_order_relaxed)
      << ",\"pitch\":" << r.pitch.load(std::memory_order_relaxed) << "}";
  }
  j << ",\"inputOpen\":\"" << (audioInputRunning() ? "settings" : lfoInputStream_ ? "listening" : "no") << "\"}";
  return j.str();
}
