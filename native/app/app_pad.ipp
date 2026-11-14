// app_pad.ipp — the X/Y pad: one finger, two controls.
//
// The phone remote shows a square. A finger on it sends a point, x and y from 0
// to 1 (y = 1 is the top edge), and every control stacked on the pad follows:
// x drives one control and y another, each across its own full range.
//
// WHAT CAN GO ON AN AXIS (a "target", one word with no spaces, so a phone, a
// Companion button or a script all name it the same way):
//   OPACITY            the deck's layer fader
//   FX<n>.AMOUNT|A-F   a slider of effect n in the deck's cue
//   NUM.<token>        a generated source's number, the token the inspector's
//                      typed-entry machinery already has (NumericParam, in
//                      app_numeric_params.ipp): lumia.flow, moire.density ...
// The cue is the one that is LIVE on the deck, else the one selected, so the
// pad plays what the audience sees and can still be tried on a cue not yet
// taken.
//
// HOLD (the default): let go and every control springs back to where it was
// when the finger came down. LATCH: let go and they stay. A phone that goes
// silent with a finger down (wifi lost, pocket) is let go after a few seconds,
// so a control can never be left stuck mid-gesture.
//
// Pad assignments are kept for the session, not saved in the show.

struct PadBinding {
  std::string x;      // target word for the x axis
  std::string y;      // target word for the y axis
  bool on = true;     // follows the pad
};

struct PadDeckState {
  bool latch = false;
  std::vector<PadBinding> bindings;
  bool engaged = false;                    // a finger is down
  int cueIndex = -1;                       // the cue the homes were read from
  std::string cueId;
  std::vector<std::array<double, 2>> homes;   // one pair per binding
  std::vector<std::array<bool, 2>> homeOk;
  Uint64 lastSetMs = 0;
};
std::vector<PadDeckState> pads_;

static constexpr int kPadMaxChips = 8;
static constexpr Uint64 kPadSilenceMs = 4000;

struct PadTarget {
  enum class Kind { None, Opacity, Fx, Num } kind = Kind::None;
  int fx = 0;        // Fx: 0-based place in the stack
  int knob = 0;      // Fx: 0 amount, 1-6 = A-F
  NumericParam num = NumericParam::None;
};

PadDeckState& padState(int deckIndex) {
  if (pads_.size() < project_.decks.size()) pads_.resize(project_.decks.size());
  return pads_[static_cast<std::size_t>(deckIndex)];
}

// Which generated-source numbers the pad takes: the continuous ones. A picker
// (palette, shape, mode), a switch, or a size that rebuilds the picture
// (resolution, panel width) is not something to drag a finger across.
static bool padNumericAllowed(NumericParam id) {
  switch (id) {
    case NumericParam::VsSpeed: case NumericParam::VsScale: case NumericParam::VsFeedback:
    case NumericParam::VsZoom: case NumericParam::VsReact: case NumericParam::VsPixelSort:
    case NumericParam::VsGlitch: case NumericParam::VsCrt: case NumericParam::VsFreeAngle:
    case NumericParam::SwirlSpeed: case NumericParam::SwirlTwist: case NumericParam::SwirlHue:
    case NumericParam::SwirlZoom: case NumericParam::SwirlFlow: case NumericParam::SwirlCentreX:
    case NumericParam::SwirlCentreY:
    case NumericParam::MoireDensity: case NumericParam::MoireSpeed: case NumericParam::MoireWarp:
    case NumericParam::MoireTurn: case NumericParam::MoireCentreX: case NumericParam::MoireCentreY:
    case NumericParam::MoireGrey: case NumericParam::MoireHue:
    case NumericParam::LumiaFlow: case NumericParam::LumiaWater: case NumericParam::LumiaRipple:
    case NumericParam::LumiaJet: case NumericParam::LumiaPrism: case NumericParam::LumiaSplit:
    case NumericParam::LumiaFocus: case NumericParam::LumiaShimmer: case NumericParam::LumiaMirrors:
    case NumericParam::LumiaAngle: case NumericParam::LumiaSpin: case NumericParam::LumiaLens:
    case NumericParam::LumiaGlow: case NumericParam::LumiaBright: case NumericParam::LumiaTrail:
    case NumericParam::PortalSize: case NumericParam::PortalBlend: case NumericParam::PortalOutline:
    case NumericParam::PortalSpeed: case NumericParam::PortalHue:
      return true;
    default:
      return false;
  }
}

static std::string padFxKnobName(int knob) {
  return knob == 0 ? std::string("AMOUNT") : std::string(1, static_cast<char>('A' + knob - 1));
}

bool padParseTarget(const std::string& word, PadTarget& out) const {
  out = PadTarget {};
  if (word.empty() || word.size() > 40) return false;
  const std::string up = toUpper(word);
  if (up == "OPACITY") { out.kind = PadTarget::Kind::Opacity; return true; }
  if (up.rfind("FX", 0) == 0) {
    const std::size_t dot = up.find('.');
    if (dot == std::string::npos || dot < 3 || dot > 4) return false;
    const std::string n = up.substr(2, dot - 2);
    if (!std::all_of(n.begin(), n.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) return false;
    const int place = std::atoi(n.c_str());
    if (place < 1 || place > 12) return false;
    const std::string knob = up.substr(dot + 1);
    int k = -1;
    if (knob == "AMOUNT") k = 0;
    else if (knob.size() == 1 && knob[0] >= 'A' && knob[0] <= 'F') k = 1 + (knob[0] - 'A');
    if (k < 0) return false;
    out.kind = PadTarget::Kind::Fx; out.fx = place - 1; out.knob = k;
    return true;
  }
  if (up.rfind("NUM.", 0) == 0) {
    const std::string token = toLower(word.substr(4));
    for (int i = 0; i <= static_cast<int>(NumericParam::DrawHold); ++i) {
      const NumericParam id = static_cast<NumericParam>(i);
      const NumericParamSpec* spec = numericParamSpec(id);
      if (spec && token == spec->token && padNumericAllowed(id)) {
        out.kind = PadTarget::Kind::Num; out.num = id;
        return true;
      }
    }
  }
  return false;
}

// The cue the pad plays on: live, else selected.
const Cue* padCue(int deckIndex) const {
  if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return nullptr;
  const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
  const int at = deck.activeIndex >= 0 ? deck.activeIndex : deck.selectedIndex;
  return at >= 0 && at < static_cast<int>(deck.cues.size()) ? &deck.cues[static_cast<std::size_t>(at)] : nullptr;
}
Cue* padCueMutable(int deckIndex) { return const_cast<Cue*>(padCue(deckIndex)); }
int padCueIndex(int deckIndex) const {
  const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
  return deck.activeIndex >= 0 ? deck.activeIndex : deck.selectedIndex;
}

// The value and the span it moves in, in the control's own units. False when
// the control is not there now (no such effect, a source the cue is not).
bool padRead(int deckIndex, const PadTarget& t, double& value, double& lo, double& hi) const {
  if (t.kind == PadTarget::Kind::Opacity) {
    value = std::clamp(static_cast<double>(project_.decks[static_cast<std::size_t>(deckIndex)].playlistOpacity), 0.0, 1.0);
    lo = 0.0; hi = 1.0;
    return true;
  }
  const Cue* cue = padCue(deckIndex);
  if (!cue) return false;
  if (t.kind == PadTarget::Kind::Fx) {
    if (t.fx >= static_cast<int>(cue->effects.size())) return false;
    const deckboy::effects::CueEffect& fx = cue->effects[static_cast<std::size_t>(t.fx)];
    if (t.knob == 0) value = fx.amount;
    else if (deckboy::effects::cueEffectParamLabelFor(fx, t.knob - 1)) value = deckboy::effects::cueEffectParam(fx, t.knob - 1);
    else return false;
    lo = 0.0; hi = 1.0;
    return true;
  }
  if (t.kind == PadTarget::Kind::Num) {
    const NumericParamSpec* spec = numericParamSpec(t.num);
    if (!spec || !readNumericParam(*cue, t.num, value)) return false;
    lo = spec->lo; hi = spec->hi;
    return true;
  }
  return false;
}

// The same write the fade cue makes: straight into the cue's own field, so the
// picture picks it up on its next sync, with no toast for every finger move.
void padWrite(int deckIndex, const PadTarget& t, double value) {
  double now = 0.0, lo = 0.0, hi = 1.0;
  if (!padRead(deckIndex, t, now, lo, hi)) return;
  value = std::clamp(value, lo, hi);
  if (t.kind == PadTarget::Kind::Opacity) {
    project_.decks[static_cast<std::size_t>(deckIndex)].playlistOpacity = static_cast<float>(value);
    setDeckPlaylistOpacityTarget(deckIndex, static_cast<float>(value));
    return;
  }
  Cue* cue = padCueMutable(deckIndex);
  if (!cue) return;
  if (t.kind == PadTarget::Kind::Fx) {
    deckboy::effects::CueEffect& fx = cue->effects[static_cast<std::size_t>(t.fx)];
    (t.knob == 0 ? fx.amount : deckboy::effects::cueEffectParam(fx, t.knob - 1)) = static_cast<float>(value);
  } else if (t.kind == PadTarget::Kind::Num) {
    writeNumericParam(*cue, t.num, value);
  }
}

// Short words for an axis and a chip, from the same labels the desk shows.
std::string padAxisLabel(int deckIndex, const PadTarget& t) const {
  if (t.kind == PadTarget::Kind::Opacity) return "opacity";
  const Cue* cue = padCue(deckIndex);
  if (t.kind == PadTarget::Kind::Fx) {
    if (t.knob == 0) return "amount";
    if (cue && t.fx < static_cast<int>(cue->effects.size())) {
      if (const char* label = deckboy::effects::cueEffectParamLabelFor(cue->effects[static_cast<std::size_t>(t.fx)], t.knob - 1)) {
        return label;
      }
    }
    return padFxKnobName(t.knob);
  }
  if (t.kind == PadTarget::Kind::Num) {
    const NumericParamSpec* spec = numericParamSpec(t.num);
    return spec ? toLower(spec->label) : std::string();
  }
  return std::string();
}
std::string padChipLabel(int deckIndex, const PadTarget& t) const {
  if (t.kind == PadTarget::Kind::Opacity) return "Opacity";
  const Cue* cue = padCue(deckIndex);
  if (t.kind == PadTarget::Kind::Fx) {
    if (cue && t.fx < static_cast<int>(cue->effects.size())) {
      return deckboy::effects::cueEffectLabel(cue->effects[static_cast<std::size_t>(t.fx)].kind);
    }
    return "Effect " + std::to_string(t.fx + 1);
  }
  if (t.kind == PadTarget::Kind::Num) {
    return cue && !cue->name.empty() ? cue->name : std::string("Source");
  }
  return std::string();
}

// Put every control back where the finger found it, when the cue is still the
// one they were read from.
void padRestoreHomes(int deckIndex, PadDeckState& pad) {
  const bool same = padCueIndex(deckIndex) == pad.cueIndex && padCue(deckIndex) &&
                    padCue(deckIndex)->id == pad.cueId;
  if (!same) return;
  for (std::size_t i = 0; i < pad.bindings.size() && i < pad.homes.size(); ++i) {
    const PadBinding& b = pad.bindings[i];
    PadTarget tx, ty;
    if (pad.homeOk[i][0] && padParseTarget(b.x, tx)) padWrite(deckIndex, tx, pad.homes[i][0]);
    if (pad.homeOk[i][1] && padParseTarget(b.y, ty)) padWrite(deckIndex, ty, pad.homes[i][1]);
  }
}

// A finger lifted. Returns what happened, in words.
std::string padRelease(int deckIndex) {
  PadDeckState& pad = padState(deckIndex);
  if (!pad.engaged) return "nothing held";
  pad.engaged = false;
  if (pad.latch) return "stays";
  padRestoreHomes(deckIndex, pad);
  markProjectDirty();
  return "springs back";
}

void padPoint(int deckIndex, double px, double py) {
  PadDeckState& pad = padState(deckIndex);
  const Cue* cue = padCue(deckIndex);
  const int index = padCueIndex(deckIndex);
  if (pad.engaged && (index != pad.cueIndex || !cue || cue->id != pad.cueId)) {
    pad.engaged = false;   // the live cue changed under the finger: start again on the new one
  }
  if (!pad.engaged) {
    if (pad.latch) pushUndoSnapshot();
    pad.homes.assign(pad.bindings.size(), {0.0, 0.0});
    pad.homeOk.assign(pad.bindings.size(), {false, false});
    for (std::size_t i = 0; i < pad.bindings.size(); ++i) {
      if (!pad.bindings[i].on) continue;
      PadTarget tx, ty;
      double lo = 0.0, hi = 1.0;
      pad.homeOk[i][0] = padParseTarget(pad.bindings[i].x, tx) && padRead(deckIndex, tx, pad.homes[i][0], lo, hi);
      pad.homeOk[i][1] = padParseTarget(pad.bindings[i].y, ty) && padRead(deckIndex, ty, pad.homes[i][1], lo, hi);
    }
    pad.engaged = true;
    pad.cueIndex = index;
    pad.cueId = cue ? cue->id : std::string();
  }
  pad.lastSetMs = SDL_GetTicks();
  for (std::size_t i = 0; i < pad.bindings.size(); ++i) {
    if (!pad.bindings[i].on) continue;
    PadTarget tx, ty;
    double now = 0.0, lo = 0.0, hi = 1.0;
    if (pad.homeOk[i][0] && padParseTarget(pad.bindings[i].x, tx) && padRead(deckIndex, tx, now, lo, hi)) {
      padWrite(deckIndex, tx, lo + px * (hi - lo));
    }
    if (pad.homeOk[i][1] && padParseTarget(pad.bindings[i].y, ty) && padRead(deckIndex, ty, now, lo, hi)) {
      padWrite(deckIndex, ty, lo + py * (hi - lo));
    }
  }
  markProjectDirty();
}

// Once a frame: a finger that went quiet is let go.
void padWatchdog() {
  const Uint64 now = SDL_GetTicks();
  for (std::size_t d = 0; d < pads_.size() && d < project_.decks.size(); ++d) {
    if (pads_[d].engaged && now - pads_[d].lastSetMs > kPadSilenceMs) {
      padRelease(static_cast<int>(d));
    }
  }
}

// The state a phone draws the pad from: this deck's chips, and every control
// that can be put on an axis right now.
void padStateJson(std::ostringstream& out, int deckIndex) const {
  static const PadDeckState kEmpty;
  const PadDeckState& pad = static_cast<std::size_t>(deckIndex) < pads_.size() ? pads_[static_cast<std::size_t>(deckIndex)] : kEmpty;
  out << "\"pad\":{\"latch\":" << (pad.latch ? "true" : "false")
      << ",\"engaged\":" << (pad.engaged ? "true" : "false") << ",\"chips\":[";
  for (std::size_t i = 0; i < pad.bindings.size(); ++i) {
    const PadBinding& b = pad.bindings[i];
    PadTarget tx, ty;
    const bool okx = padParseTarget(b.x, tx), oky = padParseTarget(b.y, ty);
    double v = 0.0, lo = 0.0, hi = 1.0;
    const bool here = okx && oky && padRead(deckIndex, tx, v, lo, hi) && padRead(deckIndex, ty, v, lo, hi);
    if (i) out << ',';
    out << "{\"slot\":" << i + 1 << ",\"x\":\"" << escapeJson(b.x) << "\",\"y\":\"" << escapeJson(b.y)
        << "\",\"xl\":\"" << escapeJson(okx ? padAxisLabel(deckIndex, tx) : std::string())
        << "\",\"yl\":\"" << escapeJson(oky ? padAxisLabel(deckIndex, ty) : std::string())
        << "\",\"name\":\"" << escapeJson(okx ? padChipLabel(deckIndex, tx) : std::string())
        << "\",\"on\":" << (b.on ? "true" : "false") << ",\"here\":" << (here ? "true" : "false") << '}';
  }
  out << "],\"controls\":[";
  bool first = true;
  const auto add = [&](const std::string& word, const std::string& group, const std::string& label) {
    if (!first) out << ',';
    first = false;
    out << "{\"t\":\"" << escapeJson(word) << "\",\"g\":\"" << escapeJson(group) << "\",\"l\":\""
        << escapeJson(label) << "\"}";
  };
  add("OPACITY", "Deck", "opacity");
  if (const Cue* cue = padCue(deckIndex)) {
    for (std::size_t k = 0; k < cue->effects.size() && k < 12; ++k) {
      const deckboy::effects::CueEffect& fx = cue->effects[k];
      const std::string group = std::to_string(k + 1) + " " + deckboy::effects::cueEffectLabel(fx.kind);
      add("FX" + std::to_string(k + 1) + ".AMOUNT", group, "amount");
      for (int s = 0; s < deckboy::effects::kCueEffectParamSlots; ++s) {
        if (const char* label = deckboy::effects::cueEffectParamLabelFor(fx, s)) {
          add("FX" + std::to_string(k + 1) + "." + padFxKnobName(s + 1), group, label);
        }
      }
    }
    for (int i = 0; i <= static_cast<int>(NumericParam::DrawHold); ++i) {
      const NumericParam id = static_cast<NumericParam>(i);
      const NumericParamSpec* spec = numericParamSpec(id);
      double v = 0.0;
      if (spec && padNumericAllowed(id) && readNumericParam(*cue, id, v)) {
        add(std::string("NUM.") + spec->token, cue->name.empty() ? std::string("Source") : cue->name, toLower(spec->label));
      }
    }
  }
  out << "]}";
}

// PAD <deck> SET <x> <y> | RELEASE | MODE HOLD|LATCH | ASSIGN <slot> <x> <y> |
//     REMOVE <slot> | CHIP <slot> ON|OFF | LIST
void handlePadCommand(const std::vector<std::string>& parts) {
  if (parts.size() < 3) {
    failRemoteCommand("PAD: expected PAD <deck> SET <x> <y> | RELEASE | MODE | ASSIGN | REMOVE | CHIP | LIST");
    return;
  }
  const int deckIndex = std::atoi(parts[1].c_str()) - 1;
  if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
    failRemoteCommand("PAD: no deck " + parts[1]);
    return;
  }
  PadDeckState& pad = padState(deckIndex);
  const std::string sub = toUpper(parts[2]);
  const auto number = [](const std::string& text, double& out) {
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return !text.empty() && end && *end == '\0' && std::isfinite(out);
  };
  const auto slotOf = [&](const std::string& text, bool mayAppend) -> int {
    const int slot = std::atoi(text.c_str());
    const int most = static_cast<int>(pad.bindings.size()) + (mayAppend ? 1 : 0);
    return slot >= 1 && slot <= std::min(most, kPadMaxChips) ? slot : 0;
  };
  if (sub == "SET" && parts.size() == 5) {
    double x = 0.0, y = 0.0;
    if (!number(parts[3], x) || !number(parts[4], y) || x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0) {
      failRemoteCommand("PAD SET: x and y are numbers from 0 to 1 (y = 1 is the top)");
      return;
    }
    const bool any = std::any_of(pad.bindings.begin(), pad.bindings.end(), [](const PadBinding& b) { return b.on; });
    if (!any) {
      failRemoteCommand("PAD SET: no control is on the pad; PAD " + parts[1] + " ASSIGN one first");
      return;
    }
    if (!padCue(deckIndex)) {
      failRemoteCommand("PAD SET: deck " + parts[1] + " has no cue to play on");
      return;
    }
    padPoint(deckIndex, x, y);
    char shown[48];
    std::snprintf(shown, sizeof(shown), "x %.2f y %.2f", x, y);
    remoteCommandDetail_ = shown;
    return;
  }
  if (sub == "RELEASE" && parts.size() == 3) {
    remoteCommandDetail_ = padRelease(deckIndex);
    return;
  }
  if (sub == "MODE" && parts.size() == 4) {
    const std::string mode = toUpper(parts[3]);
    if (mode != "HOLD" && mode != "LATCH") {
      failRemoteCommand("PAD MODE: HOLD or LATCH");
      return;
    }
    padRelease(deckIndex);
    pad.latch = mode == "LATCH";
    remoteCommandDetail_ = pad.latch ? "latch: values stay when you let go" : "hold: values spring back when you let go";
    return;
  }
  if (sub == "ASSIGN" && parts.size() == 6) {
    const int slot = slotOf(parts[3], true);
    PadTarget tx, ty;
    if (!slot) {
      failRemoteCommand("PAD ASSIGN: slot 1 to " + std::to_string(std::min<int>(static_cast<int>(pad.bindings.size()) + 1, kPadMaxChips)));
      return;
    }
    if (!padParseTarget(parts[4], tx) || !padParseTarget(parts[5], ty)) {
      failRemoteCommand("PAD ASSIGN: a control is OPACITY, FX<n>.AMOUNT, FX<n>.A to F, or NUM.<name> such as NUM.lumia.flow");
      return;
    }
    double v = 0.0, lo = 0.0, hi = 1.0;
    if (!padRead(deckIndex, tx, v, lo, hi) || !padRead(deckIndex, ty, v, lo, hi)) {
      failRemoteCommand("PAD ASSIGN: this cue does not have that control");
      return;
    }
    padRelease(deckIndex);
    PadBinding binding;
    binding.x = toUpper(parts[4]).rfind("NUM.", 0) == 0 ? "NUM." + toLower(parts[4].substr(4)) : toUpper(parts[4]);
    binding.y = toUpper(parts[5]).rfind("NUM.", 0) == 0 ? "NUM." + toLower(parts[5].substr(4)) : toUpper(parts[5]);
    if (slot > static_cast<int>(pad.bindings.size())) pad.bindings.push_back(binding);
    else pad.bindings[static_cast<std::size_t>(slot - 1)] = binding;
    remoteCommandDetail_ = "slot " + std::to_string(slot) + ": x " + padAxisLabel(deckIndex, tx) + ", y " + padAxisLabel(deckIndex, ty);
    return;
  }
  if (sub == "REMOVE" && parts.size() == 4) {
    const int slot = slotOf(parts[3], false);
    if (!slot) {
      failRemoteCommand("PAD REMOVE: no slot " + parts[3]);
      return;
    }
    padRelease(deckIndex);
    pad.bindings.erase(pad.bindings.begin() + (slot - 1));
    remoteCommandDetail_ = "removed slot " + std::to_string(slot);
    return;
  }
  if (sub == "CHIP" && parts.size() == 5) {
    const int slot = slotOf(parts[3], false);
    const std::string word = toUpper(parts[4]);
    std::optional<bool> state;
    if (word == "ON") state = true; else if (word == "OFF") state = false;
    if (!slot || !state) {
      failRemoteCommand("PAD CHIP: <slot> ON|OFF");
      return;
    }
    padRelease(deckIndex);
    pad.bindings[static_cast<std::size_t>(slot - 1)].on = *state;
    remoteCommandDetail_ = "slot " + std::to_string(slot) + (*state ? " follows the pad" : " is off the pad");
    return;
  }
  if (sub == "LIST" && parts.size() == 3) {
    std::string text = pad.latch ? "latch" : "hold";
    for (std::size_t i = 0; i < pad.bindings.size(); ++i) {
      text += "; " + std::to_string(i + 1) + (pad.bindings[i].on ? " on " : " off ") + pad.bindings[i].x + " " + pad.bindings[i].y;
    }
    remoteCommandDetail_ = text;
    return;
  }
  failRemoteCommand("PAD: expected PAD <deck> SET <x> <y> | RELEASE | MODE HOLD|LATCH | ASSIGN <slot> <x> <y> | REMOVE <slot> | CHIP <slot> ON|OFF | LIST");
}
