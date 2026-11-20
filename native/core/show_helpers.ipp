// Shared show helpers. Include inside an anonymous namespace after the core headers.
// Function bodies are moved unchanged from main.cpp; keep the desk and API identical.

constexpr int kDefaultArtNetPort = 6454;                 // Art-Net default UDP port (IANA registered)

// Normalize user-entered composite layout names to canonical tokens.
// Accepts various aliases: "split", "pip", "four" → "2up", "7030", "quad".
std::string normalizeCompositeLayoutPresetToken(std::string token) {
  token = toLower(trim(token));
  if (token == "split" || token == "split2" || token == "two" || token == "2") {
    return "2up";
  }
  if (token == "bigsmall" || token == "pip" || token == "pictureinpicture" || token == "70-30") {
    return "7030";
  }
  if (token == "4" || token == "four") {
    return "quad";
  }
  if (token == "2up" || token == "7030" || token == "quad") {
    return token;
  }
  return "2up";
}

std::string compositeSlotDefaultId(int index) {
  int slotNumber = std::max(0, index) + 1;
  return "slot" + std::to_string(slotNumber);
}

std::string compositeSlotDefaultName(int index) {
  static constexpr char kNames[] = {'A', 'B', 'C', 'D'};
  if (index >= 0 && index < static_cast<int>(sizeof(kNames))) {
    return std::string("Slot ") + kNames[index];
  }
  return "Slot " + std::to_string(std::max(0, index) + 1);
}

void ensureCompositeSlotIdentity(CompositeSlot& slot, int index) {
  if (trim(slot.id).empty()) {
    slot.id = compositeSlotDefaultId(index);
  }
  if (trim(slot.name).empty()) {
    slot.name = compositeSlotDefaultName(index);
  }
  slot.sourceType = trim(slot.sourceType).empty() ? "media" : toLower(trim(slot.sourceType));
  slot.normX = std::clamp(slot.normX, 0.0f, 1.0f);
  slot.normY = std::clamp(slot.normY, 0.0f, 1.0f);
  slot.normW = std::clamp(slot.normW, 0.05f, 1.0f);
  slot.normH = std::clamp(slot.normH, 0.05f, 1.0f);
}

void applyCompositePresetToCue(Cue& cue, const std::string& rawPreset) {
  std::string preset = normalizeCompositeLayoutPresetToken(rawPreset);
  size_t slotCount = preset == "quad" ? 4u : 2u;
  std::vector<CompositeSlot> previous = cue.compositeSlots;
  cue.compositeSlots.assign(slotCount, CompositeSlot {});
  for (size_t i = 0; i < slotCount; ++i) {
    if (i < previous.size()) {
      cue.compositeSlots[i] = previous[i];
    }
    ensureCompositeSlotIdentity(cue.compositeSlots[i], static_cast<int>(i));
  }

  cue.compositeLayoutPreset = preset;
  if (cue.compositeBackgroundColor.a == 0) {
    cue.compositeBackgroundColor = SDL_Color {18, 24, 18, 255};
  }

  if (preset == "7030") {
    cue.compositeSlots[0].normX = 0.02f;
    cue.compositeSlots[0].normY = 0.02f;
    cue.compositeSlots[0].normW = 0.68f;
    cue.compositeSlots[0].normH = 0.96f;
    cue.compositeSlots[1].normX = 0.73f;
    cue.compositeSlots[1].normY = 0.08f;
    cue.compositeSlots[1].normW = 0.24f;
    cue.compositeSlots[1].normH = 0.24f;
  } else if (preset == "quad") {
    static constexpr std::array<std::array<float, 4>, 4> kQuad {{
      {{0.02f, 0.02f, 0.46f, 0.46f}},
      {{0.52f, 0.02f, 0.46f, 0.46f}},
      {{0.02f, 0.52f, 0.46f, 0.46f}},
      {{0.52f, 0.52f, 0.46f, 0.46f}},
    }};
    for (size_t i = 0; i < cue.compositeSlots.size(); ++i) {
      cue.compositeSlots[i].normX = kQuad[i][0];
      cue.compositeSlots[i].normY = kQuad[i][1];
      cue.compositeSlots[i].normW = kQuad[i][2];
      cue.compositeSlots[i].normH = kQuad[i][3];
    }
  } else {
    cue.compositeSlots[0].normX = 0.02f;
    cue.compositeSlots[0].normY = 0.02f;
    cue.compositeSlots[0].normW = 0.46f;
    cue.compositeSlots[0].normH = 0.96f;
    cue.compositeSlots[1].normX = 0.52f;
    cue.compositeSlots[1].normY = 0.02f;
    cue.compositeSlots[1].normW = 0.46f;
    cue.compositeSlots[1].normH = 0.96f;
  }

  if (trim(cue.compositeAudioSlotId).empty()) {
    cue.compositeAudioSlotId = cue.compositeSlots.empty() ? std::string() : cue.compositeSlots.front().id;
  }
  bool audioSlotFound = false;
  for (const CompositeSlot& slot : cue.compositeSlots) {
    if (slot.id == cue.compositeAudioSlotId) {
      audioSlotFound = true;
      break;
    }
  }
  if (!audioSlotFound) {
    cue.compositeAudioSlotId = cue.compositeSlots.empty() ? std::string() : cue.compositeSlots.front().id;
  }
}

std::string normalizeJumpModeToken(std::string token) {
  token = trim(token);
  std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (token == "LOAD" || token == "ARM" || token == "READY") {
    return "load";
  }
  return "trigger";
}

std::string normalizePanicProfileToken(std::string token) {
  token = trim(token);
  std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (token == "FADE_PAUSE" || token == "FADEPAUSE" || token == "PAUSE") {
    return "fade_pause";
  }
  if (token == "FADE_REWIND" || token == "FADEREWIND" || token == "REWIND" || token == "STOP") {
    return "fade_rewind";
  }
  if (token == "FADE_LOAD_NEXT" || token == "FADELOADNEXT" || token == "LOAD_NEXT" || token == "LOADNEXT" || token == "NEXT") {
    return "fade_load_next";
  }
  return "outputs_off";
}

std::string normalizeCueIdShort(std::string value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') {
      out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
  }
  if (out.size() > 6) {
    out.resize(6);
  }
  return out;
}

double normalizePlaylistTimebaseFps(double fps) {
  static constexpr std::array<double, 4> kChoices {24.0, 25.0, 29.97, 30.0};
  double candidate = std::isfinite(fps) ? fps : 30.0;
  double best = kChoices.front();
  double bestDiff = std::fabs(candidate - best);
  for (double choice : kChoices) {
    double diff = std::fabs(candidate - choice);
    if (diff < bestDiff) {
      best = choice;
      bestDiff = diff;
    }
  }
  return best;
}

// Markers serialize as comma-separated lists in two parallel fields. Comma
// rather than tab because tab is the record separator; the names field is
// escaped like any other string.
std::string joinMarkerTimes(const Cue& cue) {
  std::string out;
  for (double t : cue.markerSeconds) {
    if (!out.empty()) out += ",";
    out += std::to_string(t);
  }
  return out;
}

std::string joinMarkerNames(const Cue& cue) {
  std::string out;
  for (const std::string& n : cue.markerNames) {
    if (!out.empty()) out += ",";
    std::string safe = n;
    std::replace(safe.begin(), safe.end(), ',', ';');   // commas are our separator
    out += safe;
  }
  return out;
}

void parseMarkerTimes(Cue& cue, const std::string& field) {
  cue.markerSeconds.clear();
  if (field.empty()) return;
  std::size_t start = 0;
  while (start <= field.size()) {
    std::size_t comma = field.find(',', start);
    std::string tok = field.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!tok.empty()) {
      try { cue.markerSeconds.push_back(std::stod(tok)); } catch (...) {}
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  std::sort(cue.markerSeconds.begin(), cue.markerSeconds.end());
}

void parseMarkerNames(Cue& cue, const std::string& field) {
  cue.markerNames.clear();
  std::size_t start = 0;
  while (start <= field.size() && !field.empty()) {
    std::size_t comma = field.find(',', start);
    cue.markerNames.push_back(
      field.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  // Names must stay parallel to times or the pairing silently shifts.
  cue.markerNames.resize(cue.markerSeconds.size());
}

// Effect stack <-> one tab field. Colons inside an entry, pipes between them,
// so a stack of any length occupies a single column.
// A master cue's assignments as one field: "deck:cueId:bypassed", entries
// joined by '|'. Same shape as the effect stack above -- pipe between entries,
// colon within one -- because the record is tab-delimited and a variable-length
// list has to live inside a single field.
//
// The cue is referenced BY ID. Ids are generated hex ("cue-158f9d9e1a117c37"),
// so they carry neither separator; a path would have carried both.
// The audio crosspoint matrix, "source:dest:gain|source:dest:gain". Sparse, so
// a cue with a plain stereo pair on outs 7-8 writes two cells rather than a
// 2x64 grid of zeroes -- and a cue with no matrix at all writes nothing.
std::string serializeAudioMatrix(const std::vector<AudioCrosspoint>& list) {
  std::string out;
  for (const auto& p : list) {
    if (p.source < 0 || p.dest < 0 || p.gain <= 0.0f) {
      continue;                 // a zero cell is the absence of a cell
    }
    if (!out.empty()) out += '|';
    out += std::to_string(p.source);
    out += ':';
    out += std::to_string(p.dest);
    out += ':';
    // Three decimals: enough for a dB step, short enough not to bloat a show
    // file with a matrix on every cue.
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.3f", p.gain);
    out += buf;
    // A fourth part names the output, on a playlist in the patch. A build
    // from before names reads the gain and stops at the colon.
    if (!p.output.empty()) {
      out += ':';
      out += p.output;
    }
  }
  return out;
}

std::vector<AudioCrosspoint> parseAudioMatrix(const std::string& text) {
  std::vector<AudioCrosspoint> out;
  if (text.empty()) return out;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t bar = text.find('|', start);
    if (bar == std::string::npos) bar = text.size();
    const std::string cell = text.substr(start, bar - start);
    start = bar + 1;
    if (cell.empty()) continue;
    const std::size_t c1 = cell.find(':');
    if (c1 == std::string::npos) continue;
    const std::size_t c2 = cell.find(':', c1 + 1);
    if (c2 == std::string::npos) continue;
    AudioCrosspoint p;
    const std::size_t c3 = cell.find(':', c2 + 1);
    try {
      p.source = std::stoi(cell.substr(0, c1));
      p.dest = std::stoi(cell.substr(c1 + 1, c2 - c1 - 1));
      p.gain = std::stof(cell.substr(c2 + 1, c3 == std::string::npos ? std::string::npos : c3 - c2 - 1));
    } catch (...) {
      continue;                 // one bad cell is skipped, not fatal
    }
    if (c3 != std::string::npos) {
      p.output = cleanAudioOutputName(cell.substr(c3 + 1));
      if (p.output.empty() || p.dest > 1) continue;   // a named point is channel 0 or 1 of its output
    }
    // Clamped on the way IN, so a hand-edited or corrupted show cannot put a
    // gain of 900 on the audio thread.
    if (p.source < 0 || p.source > 1 || p.dest < 0 || p.dest >= kMaxAudioOutputs) continue;
    p.gain = p.gain < 0.0f ? 0.0f : (p.gain > 4.0f ? 4.0f : p.gain);
    if (p.gain <= 0.0f) continue;
    out.push_back(p);
  }
  return out;
}

std::string serializeMasterAssignments(const std::vector<MasterAssignment>& list) {
  std::string out;
  for (const auto& a : list) {
    if (a.deckIndex < 0 || (a.action == 'G' && a.cueId.empty())) continue;
    if (!out.empty()) out += '|';
    out += std::to_string(a.deckIndex);
    out += ':';
    out += a.cueId;
    out += ':';
    out += (a.bypassed ? '1' : '0');
    // The action and its value, only when they say more than "take the cue":
    // a plain take is written exactly as it always was.
    if (a.action != 'G' || a.value >= 0) {
      out += ':';
      out += a.action;
      out += ':';
      out += std::to_string(a.value);
    }
  }
  return out;
}

std::vector<MasterAssignment> parseMasterAssignments(const std::string& text) {
  std::vector<MasterAssignment> out;
  if (text.empty()) return out;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t bar = text.find('|', start);
    const std::string entry = text.substr(start, bar == std::string::npos
                                                 ? std::string::npos : bar - start);
    if (!entry.empty()) {
      const std::size_t c1 = entry.find(':');
      const std::size_t c2 = (c1 == std::string::npos)
                             ? std::string::npos : entry.find(':', c1 + 1);
      if (c1 != std::string::npos && c2 != std::string::npos) {
        MasterAssignment a;
        try {
          a.deckIndex = std::stoi(entry.substr(0, c1));
        } catch (...) {
          a.deckIndex = -1;
        }
        a.cueId = entry.substr(c1 + 1, c2 - c1 - 1);
        // deck:cue:bypassed, then (0.99.417) :action:value.
        const std::size_t c3 = entry.find(':', c2 + 1);
        a.bypassed = entry.substr(c2 + 1, c3 == std::string::npos ? std::string::npos : c3 - c2 - 1) == "1";
        if (c3 != std::string::npos) {
          const std::size_t c4 = entry.find(':', c3 + 1);
          const std::string act = entry.substr(c3 + 1, c4 == std::string::npos ? std::string::npos : c4 - c3 - 1);
          a.action = act.empty() ? 'G' : act[0];
          if (a.action != 'G' && a.action != 'S' && a.action != 'F' && a.action != 'V' && a.action != 'O') {
            a.action = 'G';
          }
          if (c4 != std::string::npos) {
            try {
              a.value = std::stoi(entry.substr(c4 + 1));
            } catch (...) {
              a.value = -1;
            }
          }
        }
        if (a.deckIndex >= 0 && (a.action != 'G' || !a.cueId.empty())) {
          out.push_back(std::move(a));
        }
      }
    }
    if (bar == std::string::npos) break;
    start = bar + 1;
  }
  return out;
}

// ONE OSCILLATOR, as the show file spells it: commas inside, and the caller
// chooses what separates one from the next. Shared by the effect parameters
// and the geometry, so the two cannot drift into two spellings of one thing.
void writeParamLfo(std::ostream& one, const deckboy::effects::ParamLfo& lfo) {
  one << (lfo.on ? 1 : 0) << ',' << static_cast<int>(lfo.shape) << ','
      << lfo.rateHz << ',' << lfo.depth << ',' << lfo.phase << ','
      << (lfo.beatSync ? 1 : 0) << ',' << lfo.beats;
  // The drawn curve rides along ONLY when it is the shape in use, and
  // only when it is not still the default ramp. Thirty-two numbers on
  // every LFO of every effect of every cue would be most of a show file
  // spent on curves nobody drew.
  //
  // Appended after `beats`, so a build that predates it reads the seven
  // fields it knows and stops -- and a curve written by a newer build
  // degrades to the shape token, which is the honest fallback.
  if (lfo.shape == deckboy::effects::LfoShape::Drawn) {
    for (int c = 0; c < deckboy::effects::ParamLfo::kLfoCurvePoints; ++c) {
      one << ',' << lfo.curve[static_cast<std::size_t>(c)];
    }
  }
  // A Motion shape's zone, in the place the curve would take: the two are
  // never both in use. A build without Motion reads the shape as Audio and
  // stops at the seventh field.
  if (deckboy::effects::lfoShapeIsMotion(lfo.shape)) {
    one << ',' << lfo.motionZone;
  }
  // A MIDI shape's channel and CC, in the same place, for the same reason.
  if (deckboy::effects::lfoShapeIsMidi(lfo.shape)) {
    one << ',' << lfo.midiChannel << ',' << lfo.midiNumber;
  }
  // An audio shape's source, in the same place, only when it is not AUTO:
  // a build without it reads the shape and stops.
  if (deckboy::effects::lfoShapeIsAudio(lfo.shape) && lfo.listen != 0) {
    one << ',' << lfo.listen;
  }
}

void readParamLfo(const std::string& one, deckboy::effects::ParamLfo& lfo) {
  std::vector<std::string> f;
  std::size_t c0 = 0;
  for (;;) {
    const std::size_t comma = one.find(',', c0);
    f.push_back(one.substr(c0, comma == std::string::npos
                                 ? std::string::npos : comma - c0));
    if (comma == std::string::npos) break;
    c0 = comma + 1;
  }
  if (f.size() > 0) lfo.on = std::atoi(f[0].c_str()) != 0;
  if (f.size() > 1) {
    const int shape = std::atoi(f[1].c_str());
    lfo.shape = static_cast<deckboy::effects::LfoShape>(
      std::clamp(shape, 0,
                 static_cast<int>(deckboy::effects::LfoShape::Count) - 1));
  }
  if (f.size() > 2) lfo.rateHz = static_cast<float>(std::atof(f[2].c_str()));
  if (f.size() > 3) lfo.depth = static_cast<float>(std::atof(f[3].c_str()));
  if (f.size() > 4) lfo.phase = static_cast<float>(std::atof(f[4].c_str()));
  if (f.size() > 5) lfo.beatSync = std::atoi(f[5].c_str()) != 0;
  if (f.size() > 6) lfo.beats = static_cast<float>(std::atof(f[6].c_str()));
  if (deckboy::effects::lfoShapeIsMotion(lfo.shape)) {
    lfo.motionZone = f.size() > 7 ? std::clamp(std::atoi(f[7].c_str()), 0, 8) : 0;
    return;
  }
  if (deckboy::effects::lfoShapeIsMidi(lfo.shape)) {
    lfo.midiChannel = f.size() > 7 ? std::clamp(std::atoi(f[7].c_str()), 0, 16) : 0;
    lfo.midiNumber = f.size() > 8 ? std::clamp(std::atoi(f[8].c_str()), 0, 127) : 1;
    return;
  }
  if (deckboy::effects::lfoShapeIsAudio(lfo.shape)) {
    lfo.listen = f.size() > 7 ? std::clamp(std::atoi(f[7].c_str()), 0, 2) : 0;
    return;
  }
  // The drawn curve, if this show carries one. A partial curve is
  // read as far as it goes and the rest keeps the default ramp,
  // rather than being rejected: half a curve still oscillates.
  for (int c = 0; c < deckboy::effects::ParamLfo::kLfoCurvePoints; ++c) {
    const std::size_t curveAt = static_cast<std::size_t>(7 + c);
    if (f.size() <= curveAt) break;
    lfo.curve[static_cast<std::size_t>(c)] =
      std::clamp(static_cast<float>(std::atof(f[curveAt].c_str())), 0.0f, 1.0f);
  }
}

// A cue's geometry oscillators, one field. EMPTY when none is armed, which is
// every cue that has never had one -- so the field costs a tab and nothing
// else, and an older build reading a newer show sees one more field it skips.
//
// EMPTY ONLY WHEN NOTHING WAS EVER SET, not when nothing is switched on. An
// oscillator the operator shaped and then switched off still carries its
// rate, depth, shape and drawn curve; writing nothing for it handed back the
// defaults on reopening, so switching it on again gave a different look
// (Codex audit #3, found on the Moire cue, which shares this field format).
template <std::size_t N>
std::string serializeGeometryLfos(const std::array<deckboy::effects::ParamLfo, N>& lfos) {
  const deckboy::effects::ParamLfo fresh;
  bool any = false;
  for (const auto& lfo : lfos) {
    any = any || lfo.on || lfo.shape != fresh.shape || lfo.rateHz != fresh.rateHz ||
          lfo.depth != fresh.depth || lfo.phase != fresh.phase ||
          lfo.beatSync != fresh.beatSync || lfo.beats != fresh.beats || lfo.curve != fresh.curve ||
          lfo.motionZone != fresh.motionZone;
  }
  if (!any) {
    return std::string();
  }
  std::ostringstream out;
  for (std::size_t i = 0; i < lfos.size(); ++i) {
    if (i) out << ';';
    writeParamLfo(out, lfos[i]);
  }
  return out.str();
}

// A Moire cue's theme inks, one field: four RRGGBB hex values split by ','.
std::string serializeMoireInks(const std::array<std::uint32_t, 4>& inks) {
  std::string out;
  for (std::size_t i = 0; i < inks.size(); ++i) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06X", static_cast<unsigned>(inks[i] & 0xFFFFFFu));
    if (i) out += ',';
    out += buf;
  }
  return out;
}

std::array<std::uint32_t, 4> parseMoireInks(const std::string& text,
                                            const std::array<std::uint32_t, 4>& fallback) {
  std::array<std::uint32_t, 4> inks = fallback;
  std::size_t at = 0;
  for (std::size_t i = 0; i < inks.size() && at <= text.size(); ++i) {
    const std::size_t comma = text.find(',', at);
    const std::string part = text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    if (part.size() == 6 && part.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
      inks[i] = static_cast<std::uint32_t>(std::strtoul(part.c_str(), nullptr, 16));
    }
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  return inks;
}

// A DMX cue's fixture levels, one field: entries split by ';', each
// name|intensity|colour|intensity lfo|colour lfo. The name is %-escaped so a
// fixture called "Wash; left" cannot break the record.
static std::string escapeFixtureName(const std::string& name) {
  std::string out;
  for (const char ch : name) {
    if (ch == '%' || ch == ';' || ch == '|' || ch == '\t' || ch == '\n') {
      char buf[4];
      std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(ch));
      out += buf;
    } else {
      out += ch;
    }
  }
  return out;
}

static std::string unescapeFixtureName(const std::string& text) {
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size()) {
      out += static_cast<char>(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += text[i];
    }
  }
  return out;
}

std::string serializeFixtureLevels(const std::vector<CueFixtureLevel>& levels) {
  std::ostringstream out;
  for (std::size_t i = 0; i < levels.size(); ++i) {
    const CueFixtureLevel& l = levels[i];
    if (i) out << ';';
    out << escapeFixtureName(l.fixture) << '|' << l.intensity << '|' << l.colour << '|';
    writeParamLfo(out, l.intensityLfo);
    out << '|';
    writeParamLfo(out, l.colourLfo);
  }
  return out.str();
}

std::vector<CueFixtureLevel> parseFixtureLevels(const std::string& text) {
  std::vector<CueFixtureLevel> levels;
  std::size_t at = 0;
  while (!text.empty() && at <= text.size() && levels.size() < 512) {
    const std::size_t semi = text.find(';', at);
    const std::string entry = text.substr(at, semi == std::string::npos ? std::string::npos : semi - at);
    std::vector<std::string> f;
    std::size_t p = 0;
    for (;;) {
      const std::size_t bar = entry.find('|', p);
      f.push_back(entry.substr(p, bar == std::string::npos ? std::string::npos : bar - p));
      if (bar == std::string::npos) break;
      p = bar + 1;
    }
    if (!f.empty() && !f[0].empty()) {
      CueFixtureLevel l;
      l.fixture = unescapeFixtureName(f[0]);
      if (f.size() > 1) l.intensity = std::clamp(static_cast<float>(std::atof(f[1].c_str())), 0.0f, 1.0f);
      if (f.size() > 2) l.colour = std::clamp(std::atoi(f[2].c_str()), -1, deckboy::fixtures::kColourCount - 1);
      if (f.size() > 3) readParamLfo(f[3], l.intensityLfo);
      if (f.size() > 4) readParamLfo(f[4], l.colourLfo);
      levels.push_back(l);
    }
    if (semi == std::string::npos) break;
    at = semi + 1;
  }
  return levels;
}

template <std::size_t N>
std::array<deckboy::effects::ParamLfo, N> parseLfoList(const std::string& text) {
  std::array<deckboy::effects::ParamLfo, N> lfos {};
  std::size_t at = 0;
  for (std::size_t i = 0; i < lfos.size() && !text.empty() && at != std::string::npos; ++i) {
    const std::size_t semi = text.find(';', at);
    readParamLfo(text.substr(at, semi == std::string::npos ? std::string::npos : semi - at),
                 lfos[i]);
    at = semi == std::string::npos ? std::string::npos : semi + 1;
  }
  return lfos;
}

std::array<deckboy::effects::ParamLfo, 9> parseGeometryLfos(const std::string& text) {
  return parseLfoList<9>(text);
}

// A Lumia cue's fifteen oscillators, in the same spelling.
std::array<deckboy::effects::ParamLfo, kLumiaLfoCount> parseLumiaLfos(const std::string& text) {
  return parseLfoList<kLumiaLfoCount>(text);
}

// A SHADER cue's values: name|v0,v1,v2,v3|oscillator, entries split by ';'.
std::string serializeIsfSettings(const std::vector<IsfSetting>& settings) {
  std::ostringstream out;
  bool first = true;
  for (const auto& s : settings) {
    if (s.name.empty()) continue;
    if (!first) out << ';';
    first = false;
    out << s.name << '|' << s.v[0] << ',' << s.v[1] << ',' << s.v[2] << ',' << s.v[3] << '|';
    writeParamLfo(out, s.lfo);
  }
  return out.str();
}

std::vector<IsfSetting> parseIsfSettings(const std::string& text) {
  std::vector<IsfSetting> out;
  std::size_t at = 0;
  while (at < text.size() && out.size() < static_cast<std::size_t>(kIsfMaxInputs)) {
    std::size_t semi = text.find(';', at);
    const std::string entry = text.substr(at, semi == std::string::npos ? std::string::npos : semi - at);
    at = semi == std::string::npos ? text.size() : semi + 1;
    const std::size_t a = entry.find('|');
    const std::size_t b = a == std::string::npos ? a : entry.find('|', a + 1);
    if (b == std::string::npos) continue;
    IsfSetting s;
    s.name = entry.substr(0, a);
    std::stringstream nums(entry.substr(a + 1, b - a - 1));
    std::string num;
    for (int i = 0; i < 4 && std::getline(nums, num, ','); ++i) s.v[i] = static_cast<float>(std::atof(num.c_str()));
    readParamLfo(entry.substr(b + 1), s.lfo);
    if (!s.name.empty()) out.push_back(std::move(s));
  }
  return out;
}

// A tone cue's two oscillators, in the geometry field's spelling (the first
// two of nine). Empty when neither was ever touched, like that field.
std::string serializeToneLfos(const std::array<deckboy::effects::ParamLfo, 2>& lfos) {
  std::array<deckboy::effects::ParamLfo, 9> all {};
  all[0] = lfos[0];
  all[1] = lfos[1];
  return serializeGeometryLfos(all);
}

// Every slice's oscillators, one slice to a '|', each in the geometry
// field's spelling (its first four). Empty when none was ever touched.
std::string serializeSliceLfos(const std::vector<deckboy::slices::Slice>& slices) {
  std::string out;
  bool any = false;
  for (std::size_t i = 0; i < slices.size(); ++i) {
    std::array<deckboy::effects::ParamLfo, 9> all {};
    for (std::size_t k = 0; k < 4; ++k) all[k] = slices[i].lfo[k];
    const std::string one = serializeGeometryLfos(all);
    any = any || !one.empty();
    if (i) out += '|';
    out += one;
  }
  return any ? out : std::string();
}

void parseSliceLfos(const std::string& text, std::vector<deckboy::slices::Slice>& slices) {
  std::size_t at = 0;
  for (auto& s : slices) {
    if (at > text.size()) break;
    const std::size_t end = std::min(text.find('|', at), text.size());
    const auto all = parseGeometryLfos(text.substr(at, end - at));
    for (std::size_t k = 0; k < 4; ++k) s.lfo[k] = all[k];
    at = end + 1;
  }
}

// A Draw cue's strokes, or nothing for every other cue: the field costs a tab.
std::string serializeCueDrawing(const Cue& cue) {
  if (cue.kind != CueKind::Pattern || normalizePatternTypeId(cue.path) != "draw") return std::string();
  return deckboy::draw::serialize(cue.drawing);
}

std::array<deckboy::effects::ParamLfo, 2> parseToneLfos(const std::string& text) {
  const auto all = parseGeometryLfos(text);
  return {all[0], all[1]};
}

std::string serializeCueEffects(const std::vector<deckboy::effects::CueEffect>& stack) {
  std::string out;
  for (const auto& fx : stack) {
    if (fx.kind == deckboy::effects::CueEffectKind::None) continue;
    if (!out.empty()) out += '|';
    std::ostringstream one;
    // C and D go AFTER bypassed, not between B and it. Appending keeps every
    // show ever saved readable, and keeps a show saved here readable by a build
    // that predates them -- it just ignores the two extra fields.
    one << deckboy::effects::cueEffectToken(fx.kind) << ':' << fx.amount
        << ':' << fx.paramA << ':' << fx.paramB << ':' << (fx.bypassed ? 1 : 0)
        << ':' << fx.paramC << ':' << fx.paramD;
    // The LFOs go on the END, and only when at least one is armed.
    //
    // Appending keeps every show ever saved readable and keeps a show saved
    // here readable by a build that predates them -- it stops at paramD and
    // ignores the rest, which is exactly right, because an older build has no
    // oscillators to run. Written only when armed so the format does not grow
    // by seven numbers per parameter on every cue that never touched one.
    //
    // Semicolons between the LFOs and commas inside them: the colon and the
    // pipe are already spoken for one and two levels up.
    // ITS OWN LFOs, bypassed or not. This asked cueEffectStackHasLfo, which
    // skips a bypassed effect (right for running the stack, wrong here), so a
    // bypassed effect lost its LFOs at every save -- while BYPASS promises to
    // keep the settings. Found by the round-trip smoke test, 2026-11-13.
    const bool hasLfo = std::any_of(std::begin(fx.lfo), std::end(fx.lfo),
                                    [](const deckboy::effects::ParamLfo& l) { return l.on; });
    const bool hasColour = (fx.colour & 0xFFFFFF) != 0xFFFFFF;
    // E and F (and their LFOs) after the colour, when either is in use. A
    // build that predates them stops at the colour; one that predates the
    // colour ignores an empty field there.
    // The choices after E and F, so a choice also writes E and F.
    const bool hasChoice = fx.choiceA != 0 || fx.choiceB != 0;
    const bool hasEF = fx.paramE != 0.0f || fx.paramF != 0.0f || fx.lfo[5].on || fx.lfo[6].on || hasChoice;
    if (hasLfo || hasColour || hasEF) {
      one << ':';
      if (hasLfo) {
        // Seven when E or F carries one, five otherwise (as every show before them).
        const int count = fx.lfo[5].on || fx.lfo[6].on ? 7 : 5;
        for (int i = 0; i < count; ++i) {
          if (i) one << ';';
          writeParamLfo(one, fx.lfo[i]);
        }
      }
    }
    // The picked colour after the LFOs, and only when it is not the white
    // every effect had before it. An empty LFO field before it reads as "no
    // oscillators" here and in every build that predates the colour.
    if (hasColour || hasEF) {
      one << ':';
      if (hasColour) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "%06x", static_cast<unsigned>(fx.colour & 0xFFFFFF));
        one << hex;
      }
    }
    if (hasEF) one << ':' << fx.paramE << ':' << fx.paramF;
    if (hasChoice) one << ':' << fx.choiceA << ':' << fx.choiceB;
    out += one.str();
  }
  return out;
}

std::vector<deckboy::effects::CueEffect> parseCueEffects(const std::string& text) {
  std::vector<deckboy::effects::CueEffect> stack;
  if (text.empty()) return stack;
  std::size_t start = 0;
  for (;;) {
    const std::size_t bar = text.find('|', start);
    const std::string item =
      text.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
    if (!item.empty()) {
      std::vector<std::string> parts;
      std::size_t p0 = 0;
      for (;;) {
        const std::size_t colon = item.find(':', p0);
        parts.push_back(
          item.substr(p0, colon == std::string::npos ? std::string::npos : colon - p0));
        if (colon == std::string::npos) break;
        p0 = colon + 1;
      }
      deckboy::effects::CueEffect fx;
      fx.kind = deckboy::effects::cueEffectFromToken(parts[0]);
      // An unknown token means a show saved by a NEWER build. Skipping it
      // keeps the rest of the stack rather than refusing the whole cue.
      if (fx.kind != deckboy::effects::CueEffectKind::None) {
        if (parts.size() > 1) fx.amount = static_cast<float>(std::atof(parts[1].c_str()));
        if (parts.size() > 2) fx.paramA = static_cast<float>(std::atof(parts[2].c_str()));
        if (parts.size() > 3) fx.paramB = static_cast<float>(std::atof(parts[3].c_str()));
        if (parts.size() > 4) fx.bypassed = std::atoi(parts[4].c_str()) != 0;
        if (parts.size() > 5) fx.paramC = static_cast<float>(std::atof(parts[5].c_str()));
        if (parts.size() > 6) fx.paramD = static_cast<float>(std::atof(parts[6].c_str()));
        // The LFO block, when the show that wrote this had one. Absent on every
        // older save, which leaves every oscillator off -- which is what those
        // shows meant.
        if (parts.size() > 7 && !parts[7].empty()) {
          std::size_t at = 0;
          for (int i = 0; i < 7 && at != std::string::npos; ++i) {
            const std::size_t semi = parts[7].find(';', at);
            readParamLfo(parts[7].substr(
                           at, semi == std::string::npos ? std::string::npos : semi - at),
                         fx.lfo[i]);
            at = semi == std::string::npos ? std::string::npos : semi + 1;
          }
        }
        if (parts.size() > 8 && parts[8].size() == 6) {
          fx.colour = static_cast<std::uint32_t>(std::strtoul(parts[8].c_str(), nullptr, 16)) & 0xFFFFFF;
        }
        if (parts.size() > 9) fx.paramE = static_cast<float>(std::atof(parts[9].c_str()));
        if (parts.size() > 10) fx.paramF = static_cast<float>(std::atof(parts[10].c_str()));
        if (parts.size() > 11) fx.choiceA = std::max(0, std::atoi(parts[11].c_str()));
        if (parts.size() > 12) fx.choiceB = std::max(0, std::atoi(parts[12].c_str()));
        stack.push_back(fx);
      }
    }
    if (bar == std::string::npos) break;
    start = bar + 1;
  }
  return stack;
}

// A TAB IS THE SEPARATOR AND A NEWLINE IS THE RECORD, so a field containing
// either has to say so without using one.
//
// This used to write a backslash and then the character ITSELF -- so a field
// with a newline in it emitted a real newline, the record ran onto a second
// physical line, and the loader (which reads a line at a time) dropped
// everything after the break. Nothing hit it while every field was a single
// line of text; per-slide speaker notes are the first thing that is not, and
// they arrived with every paragraph after the first missing.
//
// Now the escape NAMES the character -- backslash-n, backslash-t -- and the
// file stays one record per line whatever is in it.
std::string escapeField(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '\t': out += "\\t"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;   // a CR belongs to the line ending, never to the value
      default: out.push_back(ch); break;
    }
  }
  return out;
}

std::vector<std::string> splitEscapedTabs(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  bool escaping = false;
  // Splits on UNESCAPED tabs and decodes the escapes as it goes, so a field
  // carrying a tab or a newline in its text does not split the record. Accepts
  // the older "backslash then the character" form too -- see unescapeField.
  for (char ch : line) {
    if (escaping) {
      switch (ch) {
        case 'n': current.push_back('\n'); break;
        case 't': current.push_back('\t'); break;
        default:  current.push_back(ch); break;
      }
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    if (ch == '\t') {
      fields.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  fields.push_back(current);
  return fields;
}

std::string deckDefaultName(int index) {
  return "Deck " + std::to_string(index + 1);
}

std::string makeCueId(const Cue& cue, int deckIndex, int cueIndex) {
  std::string seed =
    cue.path + "|" +
    cue.name + "|" +
    std::to_string(deckIndex) + "|" +
    std::to_string(cueIndex) + "|" +
    std::to_string(cue.width) + "x" + std::to_string(cue.height);
  size_t hashValue = std::hash<std::string> {}(seed);
  std::ostringstream output;
  output << "cue-" << std::hex << std::nouppercase << hashValue;
  return output.str();
}

void normalizeCueTiming(Cue& cue) {
  if (!std::isfinite(cue.triggerTimecodeSeconds) || cue.triggerTimecodeSeconds < 0.0) {
    cue.triggerTimecodeSeconds = -1.0;
  }

  if (!std::isfinite(cue.inPointSeconds)) {
    cue.inPointSeconds = 0.0;
  }
  if (!std::isfinite(cue.outPointSeconds)) {
    cue.outPointSeconds = 0.0;
  }

  cue.inPointSeconds = std::max(0.0, cue.inPointSeconds);
  cue.outPointSeconds = std::max(0.0, cue.outPointSeconds);

  // THE AUTHORITATIVE GATE, and it runs constantly: focusedDeckMutable() calls
  // normalizeProject() on every call, so this executes after any edit and any
  // access. It was the reason trim was Video-only everywhere -- the adjusters
  // and the remote setters each tested for Video as well, but even with those
  // opened up an audio cue's in-point was written, reported set, and zeroed
  // again before anything could read it. A show file carrying a trim on an
  // audio cue had it erased at load, too.
  if (!cueSupportsTrimPoints(cue.kind) || cue.duration <= 0.0) {
    cue.inPointSeconds = 0.0;
    cue.outPointSeconds = 0.0;
    return;
  }

  if (cue.outPointSeconds <= 0.0) {
    cue.outPointSeconds = cue.duration;
  }

  cue.inPointSeconds = std::clamp(cue.inPointSeconds, 0.0, cue.duration);
  cue.outPointSeconds = std::clamp(cue.outPointSeconds, cue.inPointSeconds, cue.duration);
  if (cue.outPointSeconds - cue.inPointSeconds < 0.01) {
    cue.outPointSeconds = std::min(cue.duration, cue.inPointSeconds + 0.01);
  }
}

std::string defaultNdiSourceName(const Deck& deck, int index) {
  std::string base = deck.name.empty() ? deckDefaultName(index) : deck.name;
  return "Deckboy - " + base;
}

std::string defaultNdiKeySourceName(const Deck& deck, int index) {
  return defaultNdiSourceName(deck, index) + " Key";
}

std::string defaultOutputNdiSourceName(const OutputTarget& output, int index) {
  std::string base = trim(output.name).empty()
    ? ("Output " + std::to_string(index + 1))
    : output.name;
  return "Deckboy Out - " + base;
}

std::string defaultOutputNdiKeySourceName(const OutputTarget& output, int index) {
  return defaultOutputNdiSourceName(output, index) + " Key";
}

std::string normalizeWarpMode(std::string mode);

void normalizeDeck(Deck& deck, int index) {
  if (deck.name.empty()) {
    deck.name = deckDefaultName(index);
  }
  if (deck.cues.empty()) {
    deck.selectedIndex = -1;
    deck.activeIndex = -1;
  } else {
    deck.selectedIndex = std::clamp(deck.selectedIndex, 0, static_cast<int>(deck.cues.size()) - 1);
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      deck.activeIndex = -1;
    }

    std::unordered_set<std::string> usedIds;
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      Cue& cue = deck.cues[cueIndex];
      normalizeCueTiming(cue);
      cue.cueId = normalizeCueIdShort(cue.cueId);
      if (cue.cueId.empty()) {
        cue.cueId = normalizeCueIdShort(cue.cueNumber);
      }
      // Repair hasAudio if audioChannels/audioSampleRate say audio exists but the flag
      // was stored incorrectly (e.g. from the probeCue codec field-order bug).
      if (!cue.hasAudio && (cue.audioChannels > 0 || cue.audioSampleRate > 0)) {
        cue.hasAudio = true;
      }
      if (!cue.hasAudio) {
        cue.audioEnabled = false;
      }
      cue.outputScaleX = std::clamp(cue.outputScaleX, 0.25f, 4.0f);
      cue.outputScaleY = std::clamp(cue.outputScaleY, 0.25f, 4.0f);
      cue.outputRotationDegrees = std::clamp(cue.outputRotationDegrees, -180.0f, 180.0f);
      cue.cropLeft = std::clamp(cue.cropLeft, 0.0f, 0.90f);
      cue.cropRight = std::clamp(cue.cropRight, 0.0f, 0.90f);
      cue.cropTop = std::clamp(cue.cropTop, 0.0f, 0.90f);
      cue.cropBottom = std::clamp(cue.cropBottom, 0.0f, 0.90f);
      float maxHorizontal = std::max(0.0f, 0.95f - cue.cropLeft);
      cue.cropRight = std::min(cue.cropRight, maxHorizontal);
      float maxVertical = std::max(0.0f, 0.95f - cue.cropTop);
      cue.cropBottom = std::min(cue.cropBottom, maxVertical);
      cue.chromaKeyTolerance = std::clamp(cue.chromaKeyTolerance, 0.0f, 441.0f);
      cue.chromaKeySoftness = std::clamp(cue.chromaKeySoftness, 0.0f, 200.0f);
      cue.brightness = std::clamp(cue.brightness, 0.0f, 2.0f);
      cue.contrast = std::clamp(cue.contrast, 0.0f, 2.0f);
      cue.saturation = std::clamp(cue.saturation, 0.0f, 2.0f);
      cue.hueShift = std::clamp(cue.hueShift, -180.0f, 180.0f);
      cue.gotoTarget = trim(cue.gotoTarget);
      if (cue.kind == CueKind::Composite) {
        applyCompositePresetToCue(cue, cue.compositeLayoutPreset);
      }
      if (cue.id.empty()) {
        cue.id = makeCueId(cue, index, cueIndex);
      }
      std::string baseId = cue.id;
      int dedupe = 2;
      while (usedIds.find(cue.id) != usedIds.end()) {
        cue.id = baseId + "-" + std::to_string(dedupe++);
      }
      usedIds.insert(cue.id);
    }
    std::vector<int> normalizedSelection;
    normalizedSelection.reserve(deck.selectedIndices.size());
    std::unordered_set<int> seenIndices;
    for (int selected : deck.selectedIndices) {
      if (selected < 0 || selected >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      if (seenIndices.insert(selected).second) {
        normalizedSelection.push_back(selected);
      }
    }
    if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
      if (seenIndices.insert(deck.selectedIndex).second) {
        normalizedSelection.push_back(deck.selectedIndex);
      }
    }
    if (normalizedSelection.empty() && !deck.cues.empty()) {
      normalizedSelection.push_back(deck.selectedIndex);
    }
    std::sort(normalizedSelection.begin(), normalizedSelection.end());
    deck.selectedIndices = std::move(normalizedSelection);
  }
  deck.playlistOpacity = std::clamp(deck.playlistOpacity, 0.0f, 1.0f);
  deck.playlistFadeSeconds = std::clamp(
    std::isfinite(deck.playlistFadeSeconds) ? deck.playlistFadeSeconds : 0.8,
    0.05, 10.0);
  deck.playlistTimebaseFps = normalizePlaylistTimebaseFps(deck.playlistTimebaseFps);
  deck.playlistStartOffsetSeconds = std::clamp(
    std::isfinite(deck.playlistStartOffsetSeconds) ? deck.playlistStartOffsetSeconds : 0.0,
    0.0, 24.0 * 60.0 * 60.0);
  deck.playlistDefaultCueFadeSeconds = std::clamp(
    std::isfinite(deck.playlistDefaultCueFadeSeconds) ? deck.playlistDefaultCueFadeSeconds : 1.5,
    0.0, 10.0);
  deck.playlistDefaultStillDurationSeconds = std::clamp(
    std::isfinite(deck.playlistDefaultStillDurationSeconds) ? deck.playlistDefaultStillDurationSeconds : 8.0,
    0.0, 3600.0);
  deck.outputDisplayIndex = std::max(0, deck.outputDisplayIndex);
  if (deck.ndiSourceName.empty()) {
    deck.ndiSourceName = defaultNdiSourceName(deck, index);
  }
  if (deck.ndiKeySourceName.empty()) {
    deck.ndiKeySourceName = defaultNdiKeySourceName(deck, index);
  }
  deck.canvasViewX = std::clamp(deck.canvasViewX, 0, 32768);
  deck.canvasViewY = std::clamp(deck.canvasViewY, 0, 32768);
  deck.warpTopLeftX = std::clamp(deck.warpTopLeftX, -4096.0f, 4096.0f);
  deck.warpTopLeftY = std::clamp(deck.warpTopLeftY, -4096.0f, 4096.0f);
  deck.warpTopRightX = std::clamp(deck.warpTopRightX, -4096.0f, 4096.0f);
  deck.warpTopRightY = std::clamp(deck.warpTopRightY, -4096.0f, 4096.0f);
  deck.warpBottomRightX = std::clamp(deck.warpBottomRightX, -4096.0f, 4096.0f);
  deck.warpBottomRightY = std::clamp(deck.warpBottomRightY, -4096.0f, 4096.0f);
  deck.warpBottomLeftX = std::clamp(deck.warpBottomLeftX, -4096.0f, 4096.0f);
  deck.warpBottomLeftY = std::clamp(deck.warpBottomLeftY, -4096.0f, 4096.0f);
  deck.warpMode = normalizeWarpMode(deck.warpMode);
  deck.edgeBlendLeft = std::clamp(deck.edgeBlendLeft, 0.0f, 0.49f);
  deck.edgeBlendRight = std::clamp(deck.edgeBlendRight, 0.0f, 0.49f);
  deck.edgeBlendTop = std::clamp(deck.edgeBlendTop, 0.0f, 0.49f);
  deck.edgeBlendBottom = std::clamp(deck.edgeBlendBottom, 0.0f, 0.49f);
  deck.edgeBlendRight = std::min(deck.edgeBlendRight, std::max(0.0f, 0.95f - deck.edgeBlendLeft));
  deck.edgeBlendBottom = std::min(deck.edgeBlendBottom, std::max(0.0f, 0.95f - deck.edgeBlendTop));
  deck.transitionSeconds = std::clamp(deck.transitionSeconds, 0.0, 10.0);
  deck.transitionStyle = transitionStyleToken(parseTransitionStyleToken(deck.transitionStyle));
  if (!std::isfinite(deck.timecodeFps) || deck.timecodeFps < 1.0) {
    deck.timecodeFps = 30.0;
  }
  deck.timecodeFreewheelSeconds = std::clamp(
    std::isfinite(deck.timecodeFreewheelSeconds) ? deck.timecodeFreewheelSeconds : 1.0,
    0.0, 10.0);
  if (!std::isfinite(deck.timecodeCurrentSeconds) || deck.timecodeCurrentSeconds < 0.0) {
    deck.timecodeCurrentSeconds = 0.0;
  }
  if (!std::isfinite(deck.timecodeLastSeconds) || deck.timecodeLastSeconds < 0.0) {
    deck.timecodeLastSeconds = deck.timecodeCurrentSeconds;
  }
  deck.timecodeDirty = false;
}

std::string outputDefaultName(int index) {
  return "Output " + std::to_string(index + 1);
}

std::string makeOutputId(const OutputTarget& output, int outputIndex) {
  std::string seed =
    output.name + "|" +
    std::to_string(output.hostDeckIndex) + "|" +
    std::to_string(output.displayIndex) + "|" +
    std::to_string(outputIndex);
  size_t hashValue = std::hash<std::string> {}(seed);
  std::ostringstream outputId;
  outputId << "out-" << std::hex << std::nouppercase << hashValue;
  return outputId.str();
}

std::string normalizeOutputStreamProtocol(std::string protocol) {
  std::transform(protocol.begin(), protocol.end(), protocol.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (protocol == "rtmp") {
    return "rtmp";
  }
  // "rtmps" used to fall through to "srt" here, which silently made every
  // rtmps branch in the settings UI and in buildOutputStreamArgs unreachable —
  // selecting RTMPS gave you an SRT stream. It is its own protocol.
  if (protocol == "rtmps") {
    return "rtmps";
  }
  // "file" records the finished program to disk instead of sending it. Same
  // pipeline, different sink -- the compositor already hands finished frames to
  // ffmpeg's stdin, so recording is a muxer and a target away from streaming.
  if (protocol == "file") {
    return "file";
  }
  // "web" sends the output, picture AND sound, to the browsers watching the
  // web monitor: fragmented MP4 into a private local socket the monitor's
  // server relays (see app_network.ipp, web ingest).
  if (protocol == "web") {
    return "web";
  }
  return "srt";
}

std::string normalizeOutputType(std::string outputType) {
  std::transform(outputType.begin(), outputType.end(), outputType.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (outputType == "stream") {
    return "stream";
  }
  // PRESENTER is a WINDOW that shows the operator something else.
  //
  // It is an output type rather than a separate kind of window because
  // everything an output already knows how to do is what a presenter view
  // needs: pick a display, go fullscreen on it, be armed and disarmed with the
  // rest of the show. The only thing that differs is what gets drawn in it.
  if (outputType == "presenter") {
    return "presenter";
  }
  // PROMPTER is the same trick again, for the person in front of the camera
  // rather than the one behind it: a screen under a beamsplitter showing the
  // script scrolling at a set pace, MIRRORED, because the glass reverses it
  // on the way to their eye.
  //
  // Also an output rather than a cue kind, and for a reason beyond convenience:
  // prompter text is drawn with real fonts at whatever size the talent needs,
  // and the engine's frame generators only have a three-by-five digit table.
  // A script belongs where the text renderer is.
  if (outputType == "prompter" || outputType == "teleprompter") {
    return "prompter";
  }
  // MULTIVIEW is the same trick a third time: a screen showing every playlist
  // at once, which is what a switcher's monitor wall is for. Not a cue kind
  // for the same reason the other two are not -- it is a thing a DESTINATION
  // shows, not a thing a playlist plays.
  if (outputType == "multiview" || outputType == "multi") {
    return "multiview";
  }
  // SCOPES is the same trick a fourth time: a screen that shows the picture
  // scopes (waveform, parade, vectorscope, diamond, histogram) of the
  // programme, so a colourist or a camera shader has them on a display of
  // their own, and so NDI, a stream or the Web Monitor can carry them.
  if (outputType == "scopes" || outputType == "scope") {
    return "scopes";
  }
  return "window";
}

std::string normalizeOutputColorSpace(std::string colorSpace) {
  colorSpace = toLower(trim(colorSpace));
  if (colorSpace == "709" || colorSpace == "bt709" || colorSpace == "rec709" || colorSpace == "rec-709") {
    return "bt709";
  }
  if (colorSpace == "srgb" || colorSpace == "rgb") {
    return "srgb";
  }
  return "auto";
}

std::string normalizeOutputLayoutMode(std::string mode) {
  mode = toLower(trim(mode));
  if (mode == "duplicate" || mode == "dup" || mode == "clone") {
    return "duplicate";
  }
  return "span";
}

std::string normalizeWarpMode(std::string mode) {
  mode = toLower(trim(mode));
  if (mode == "perspective" || mode == "persp" || mode == "projective") {
    return "perspective";
  }
  return "linear";
}

// An output's grid warp as one field: "cols,rows,smooth,dx,dy,dx,dy,..." --
// commas only, so it cannot collide with the record's tabs. Empty is no grid.
std::string serializeWarpGrid(const OutputTarget& out) {
  if (out.warpGridCols < 2 || out.warpGridRows < 2 ||
      out.warpGridOffsets.size() != static_cast<std::size_t>(out.warpGridCols * out.warpGridRows * 2)) {
    return std::string();
  }
  std::ostringstream s;
  // Every bit of each float: at the stream's default six digits a nudge
  // drifted a little on every save, and an alignment should not erode.
  s << std::setprecision(std::numeric_limits<float>::max_digits10);
  s << out.warpGridCols << ',' << out.warpGridRows << ',' << (out.warpGridSmooth ? 1 : 0);
  for (float v : out.warpGridOffsets) {
    s << ',' << v;
  }
  return s.str();
}

// The reverse. Anything that does not add up -- a size out of range, a point
// count that does not match it -- leaves the output with no grid rather than
// a half-loaded one that bends the picture somewhere nobody put it.
void parseWarpGrid(OutputTarget& out, const std::string& text) {
  out.warpGridCols = out.warpGridRows = 0;
  out.warpGridOffsets.clear();
  if (text.empty()) {
    return;
  }
  std::vector<double> v;
  std::stringstream ss(text);
  std::string part;
  while (std::getline(ss, part, ',')) {
    try {
      v.push_back(std::stod(part));
    } catch (...) {
      return;
    }
  }
  if (v.size() < 3) {
    return;
  }
  const int cols = static_cast<int>(v[0]);
  const int rows = static_cast<int>(v[1]);
  if (cols < 2 || cols > 9 || rows < 2 || rows > 9 ||
      v.size() != 3 + static_cast<std::size_t>(cols * rows * 2)) {
    return;
  }
  out.warpGridCols = cols;
  out.warpGridRows = rows;
  out.warpGridSmooth = v[2] != 0.0;
  out.warpGridOffsets.reserve(static_cast<std::size_t>(cols * rows * 2));
  for (std::size_t i = 3; i < v.size(); ++i) {
    out.warpGridOffsets.push_back(static_cast<float>(v[i]));
  }
}

int normalizeOutputOrientationDegrees(int degrees) {
  int normalized = degrees % 360;
  if (normalized < 0) {
    normalized += 360;
  }
  if (normalized < 45) {
    return 0;
  }
  if (normalized < 135) {
    return 90;
  }
  if (normalized < 225) {
    return 180;
  }
  if (normalized < 315) {
    return 270;
  }
  return 0;
}

int normalizeOscQueryPort(int port) {
  return std::clamp(port, 1, 65535);
}

int normalizeOscFeedbackRateMs(int value) {
  return std::clamp(value, 40, 2000);
}

int normalizeArtNetPort(int value) {
  return std::clamp(value, 1, 65535);
}

std::string defaultOutputStreamUrl(const std::string& protocol, int outputIndex) {
  int normalizedIndex = std::max(0, outputIndex) + 1;
  const std::string normalized = normalizeOutputStreamProtocol(protocol);
  if (normalized == "rtmp") {
    return "rtmp://127.0.0.1/live/output" + std::to_string(normalizedIndex);
  }
  if (normalized == "rtmps") {
    // RTMPS had no default at all (it normalized to SRT), so choosing it left
    // the operator with an srt:// URL and no clue why nothing connected.
    return "rtmps://127.0.0.1:443/live/output" + std::to_string(normalizedIndex);
  }
  if (normalized == "file") {
    // A NAME, not a final path: buildOutputStreamArgs stamps the time onto the
    // stem at spawn so a second recording can never overwrite the first. The
    // folder is resolved there too, because it depends on whether a show is
    // open and this function does not know.
    return "program-output" + std::to_string(normalizedIndex) + ".mp4";
  }
  // Bare host:port — the transport parameters are added from the SRT fields by
  // applySrtUrlParameters, so they are no longer baked into the default string.
  return "srt://127.0.0.1:9000";
}

int resolveLegacyOutputHostIndexForProject(const Project& project, int deckIndex) {
  if (deckIndex < 0 || deckIndex >= static_cast<int>(project.decks.size())) {
    return deckIndex;
  }
  int current = deckIndex;
  std::vector<bool> visited(project.decks.size(), false);
  while (current >= 0 && current < static_cast<int>(project.decks.size())) {
    if (visited[current]) {
      return deckIndex;
    }
    visited[current] = true;
    int next = project.decks[current].outputRouteDeckIndex;
    if (next < 0 || next >= static_cast<int>(project.decks.size()) || next == current) {
      return current;
    }
    current = next;
  }
  return deckIndex;
}

void normalizeProjectOutputsAndLayers(Project& project) {
  int deckCount = static_cast<int>(project.decks.size());
  if (deckCount <= 0) {
    project.outputs.clear();
    project.focusedOutputIndex = 0;
    return;
  }

  if (project.outputs.empty()) {
    std::vector<int> hostDeckOrder;
    hostDeckOrder.reserve(project.decks.size());
    auto addHost = [&](int hostDeckIndex) {
      if (std::find(hostDeckOrder.begin(), hostDeckOrder.end(), hostDeckIndex) == hostDeckOrder.end()) {
        hostDeckOrder.push_back(hostDeckIndex);
      }
    };
    for (int deckIndex = 0; deckIndex < deckCount; ++deckIndex) {
      addHost(resolveLegacyOutputHostIndexForProject(project, deckIndex));
    }
    if (hostDeckOrder.empty()) {
      hostDeckOrder.push_back(0);
    }
    project.outputs.clear();
    for (size_t i = 0; i < hostDeckOrder.size(); ++i) {
      OutputTarget output;
      output.name = outputDefaultName(static_cast<int>(i));
      output.hostDeckIndex = std::clamp(hostDeckOrder[i], 0, deckCount - 1);
      output.displayIndex = std::max(0, project.decks[output.hostDeckIndex].outputDisplayIndex);
      output.enabled = false;
      project.outputs.push_back(output);
    }
  }

  auto dedupeOutputId = [](std::unordered_set<std::string>& usedIds, const std::string& preferred) {
    std::string normalized = trim(preferred);
    if (normalized.empty()) {
      normalized = "out";
    }
    std::string base = normalized;
    int dedupe = 2;
    while (usedIds.find(normalized) != usedIds.end()) {
      normalized = base + "-" + std::to_string(dedupe++);
    }
    usedIds.insert(normalized);
    return normalized;
  };

  for (size_t i = 0; i < project.outputs.size(); ++i) {
    OutputTarget& output = project.outputs[i];
    if (output.name.empty()) {
      output.name = outputDefaultName(static_cast<int>(i));
    }
    output.hostDeckIndex = std::clamp(output.hostDeckIndex, 0, deckCount - 1);
    // ── PROJECTION MAPPING MOVES UP FROM THE HOST DECK, ONCE ────────────
    //
    // Warp and edge blend used to live on the Deck and were applied through
    // whichever deck hosted this output. A show saved that way has them on
    // the deck and nothing on the output, so they are lifted here and the
    // deck's are cleared -- which reproduces exactly what that show did, and
    // cannot run twice because the deck no longer has anything to give.
    //
    // Only from the HOST: a deck that is merely a layer on this output never
    // supplied its warp, so taking one now would invent an alignment nobody
    // set.
    {
      Deck& host = project.decks[output.hostDeckIndex];
      const bool outputHasNone =
        !output.warpEnabled && output.warpTopLeftX == 0.0f && output.warpTopLeftY == 0.0f &&
        output.warpTopRightX == 0.0f && output.warpTopRightY == 0.0f &&
        output.warpBottomRightX == 0.0f && output.warpBottomRightY == 0.0f &&
        output.warpBottomLeftX == 0.0f && output.warpBottomLeftY == 0.0f &&
        output.edgeBlendLeft == 0.0f && output.edgeBlendRight == 0.0f &&
        output.edgeBlendTop == 0.0f && output.edgeBlendBottom == 0.0f;
      const bool deckHasSome =
        host.warpEnabled || host.warpTopLeftX != 0.0f || host.warpTopLeftY != 0.0f ||
        host.warpTopRightX != 0.0f || host.warpTopRightY != 0.0f ||
        host.warpBottomRightX != 0.0f || host.warpBottomRightY != 0.0f ||
        host.warpBottomLeftX != 0.0f || host.warpBottomLeftY != 0.0f ||
        host.edgeBlendLeft != 0.0f || host.edgeBlendRight != 0.0f ||
        host.edgeBlendTop != 0.0f || host.edgeBlendBottom != 0.0f;
      if (outputHasNone && deckHasSome) {
        output.warpEnabled = host.warpEnabled;
        output.warpMode = host.warpMode;
        output.warpTopLeftX = host.warpTopLeftX;
        output.warpTopLeftY = host.warpTopLeftY;
        output.warpTopRightX = host.warpTopRightX;
        output.warpTopRightY = host.warpTopRightY;
        output.warpBottomRightX = host.warpBottomRightX;
        output.warpBottomRightY = host.warpBottomRightY;
        output.warpBottomLeftX = host.warpBottomLeftX;
        output.warpBottomLeftY = host.warpBottomLeftY;
        output.edgeBlendLeft = host.edgeBlendLeft;
        output.edgeBlendRight = host.edgeBlendRight;
        output.edgeBlendTop = host.edgeBlendTop;
        output.edgeBlendBottom = host.edgeBlendBottom;
        host.warpEnabled = false;
        host.warpTopLeftX = host.warpTopLeftY = 0.0f;
        host.warpTopRightX = host.warpTopRightY = 0.0f;
        host.warpBottomRightX = host.warpBottomRightY = 0.0f;
        host.warpBottomLeftX = host.warpBottomLeftY = 0.0f;
        host.edgeBlendLeft = host.edgeBlendRight = 0.0f;
        host.edgeBlendTop = host.edgeBlendBottom = 0.0f;
      }
    }
    output.warpMode = normalizeWarpMode(output.warpMode);

    // -- VJ MODE BECOMES THIS OUTPUT'S CROSSFADER -----------------------
    //
    // VJ mode was a global that claimed the programme output and replaced
    // whatever stack it had with an A/B pair. That is the layer stack again,
    // in a weaker form: one output, exactly two decks, and every other
    // routing decision silently discarded while it was on.
    //
    // So it migrates, the way warp did. The programme output gets deck A as
    // its base and deck B as a layer over it carrying VJ's blend mode, and
    // the crossfader takes the mix position. Then the old fields are CLEARED
    // -- warp's migration left its originals in place and half the interface
    // went on reading them for two releases.
    if (project.vjModeEnabled && project.decks.size() > 1 &&
        i == 0 && !output.crossfadeEnabled) {
      // deckCount is already in scope from the top of this function.
      const int deckA = std::clamp(project.vjDeckA, 0, deckCount - 1);
      const int deckB = std::clamp(project.vjDeckB, 0, deckCount - 1);
      if (deckA != deckB) {
        output.hostDeckIndex = deckA;
        // B goes on top, keeping whatever blend the crossfader was using.
        OutputLayer over;
        over.deckIndex = deckB;
        over.blendMode = project.vjBlendMode.empty() ? std::string("dissolve")
                                                     : project.vjBlendMode;
        output.layerDecks.clear();
        output.layerDecks.push_back(over);
        output.crossfadeEnabled = true;
        output.crossfadeFrom = 0;   // the base, which is deck A
        output.crossfadeTo = 1;     // the layer, which is deck B
        output.crossfadeMix = std::clamp(project.vjMixPosition, 0.0, 1.0);
      }
      // vjModeEnabled STAYS: it is the operator's switch for showing the
      // A/B previews and the fader bar, and it is already wired to a
      // settings button and a verb. Only the DATA moved onto the output.
      project.vjMixPosition = 0.0;   // migrated onto the output above
    }
    // SUPER DECKBOY'S LAYER STACK, cleaned once here so nothing downstream
    // has to. A deck that no longer exists, or the host repeated above
    // itself, would each composite the same picture twice at a cost and to
    // no effect -- and a show that once had four decks and now has two must
    // still open.
    {
      std::vector<OutputLayer> cleaned;
      for (const OutputLayer& layer : output.layerDecks) {
        if (layer.deckIndex < 0 || layer.deckIndex >= deckCount ||
            layer.deckIndex == output.hostDeckIndex) {
          continue;
        }
        bool already = false;
        for (const OutputLayer& kept : cleaned) {
          already = already || kept.deckIndex == layer.deckIndex;
        }
        if (already) {
          continue;
        }
        OutputLayer fixed = layer;
        // Geometry kept inside the frame and never inverted: a layer with a
        // negative width is a layer nobody can see and nothing to explain it.
        fixed.w = std::clamp(fixed.w, 0.02f, 1.0f);
        fixed.h = std::clamp(fixed.h, 0.02f, 1.0f);
        fixed.x = std::clamp(fixed.x, 0.0f, 1.0f - fixed.w);
        fixed.y = std::clamp(fixed.y, 0.0f, 1.0f - fixed.h);
        if (fixed.blendMode.empty()) {
          fixed.blendMode = "dissolve";
        }
        cleaned.push_back(fixed);
      }
      output.layerDecks.swap(cleaned);
    }
    output.displayIndex = std::max(0, output.displayIndex);
    output.outputType = normalizeOutputType(output.outputType);
    output.mirrorSourceOutputIndex = std::clamp(output.mirrorSourceOutputIndex, -1, static_cast<int>(project.outputs.size()) - 1);
    if (output.outputType != "stream") {
      output.mirrorSourceOutputIndex = -1;
    }
    if (output.mirrorSourceOutputIndex == static_cast<int>(i)) {
      output.mirrorSourceOutputIndex = -1;
    }
    output.streamProtocol = normalizeOutputStreamProtocol(output.streamProtocol);
    output.streamBitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    output.outputAlpha = std::clamp(output.outputAlpha, 0.0f, 1.0f);
    output.outputDelayMs = std::clamp(output.outputDelayMs, 0, 5000);
    output.aoiLeft   = std::clamp(output.aoiLeft,   0.0f, kAoiMaxEdge);
    output.aoiRight  = std::clamp(output.aoiRight,  0.0f, kAoiMaxEdge);
    output.aoiTop    = std::clamp(output.aoiTop,    0.0f, kAoiMaxEdge);
    output.aoiBottom = std::clamp(output.aoiBottom, 0.0f, kAoiMaxEdge);
    output.outputColorSpace = normalizeOutputColorSpace(output.outputColorSpace);
    output.outputLayoutMode = normalizeOutputLayoutMode(output.outputLayoutMode);
    output.outputOrientationDegrees = normalizeOutputOrientationDegrees(output.outputOrientationDegrees);
    if (trim(output.streamUrl).empty()) {
      output.streamUrl = defaultOutputStreamUrl(output.streamProtocol, static_cast<int>(i));
    }
    if (output.ndiSourceName.empty()) {
      output.ndiSourceName = defaultOutputNdiSourceName(output, static_cast<int>(i));
    }
    if (output.ndiKeySourceName.empty()) {
      output.ndiKeySourceName = defaultOutputNdiKeySourceName(output, static_cast<int>(i));
    }
    if (!output.ndiEnabled) {
      output.ndiKeyEnabled = false;
    }
    if (trim(output.outputId).empty()) {
      output.outputId = makeOutputId(output, static_cast<int>(i));
    }
  }

  {
    std::unordered_set<std::string> usedOutputIds;
    for (size_t i = 0; i < project.outputs.size(); ++i) {
      OutputTarget& output = project.outputs[i];
      output.outputId = dedupeOutputId(usedOutputIds, output.outputId);
    }
  }
  project.focusedOutputIndex = std::clamp(project.focusedOutputIndex, 0, static_cast<int>(project.outputs.size()) - 1);

  std::vector<bool> hostDisplayAssigned(deckCount, false);
  for (const auto& output : project.outputs) {
    if (!output.enabled) {
      continue;
    }
    int hostDeckIndex = std::clamp(output.hostDeckIndex, 0, deckCount - 1);
    if (!hostDisplayAssigned[hostDeckIndex]) {
      project.decks[hostDeckIndex].outputDisplayIndex = std::max(0, output.displayIndex);
      hostDisplayAssigned[hostDeckIndex] = true;
    }
  }

  // Single-deck: route deck 0 to output 0 directly.
  for (int deckIndex = 0; deckIndex < deckCount; ++deckIndex) {
    project.decks[deckIndex].outputRouteDeckIndex = deckIndex;
  }
}

bool outputHasExplicitNdiSettings(const OutputTarget& output, int outputIndex) {
  std::string fillName = trim(output.ndiSourceName);
  std::string keyName = trim(output.ndiKeySourceName);
  std::string defaultFill = defaultOutputNdiSourceName(output, outputIndex);
  std::string defaultKey = defaultOutputNdiKeySourceName(output, outputIndex);
  bool fillCustom = !fillName.empty() && fillName != defaultFill;
  bool keyCustom = !keyName.empty() && keyName != defaultKey;
  return output.ndiEnabled || output.ndiKeyEnabled || fillCustom || keyCustom;
}

void migrateLegacyDeckNdiToOutputs(Project& project) {
  int deckCount = static_cast<int>(project.decks.size());
  int outputCount = static_cast<int>(project.outputs.size());
  if (deckCount <= 0 || outputCount <= 0) {
    return;
  }

  for (int outputIndex = 0; outputIndex < outputCount; ++outputIndex) {
    OutputTarget& output = project.outputs[outputIndex];
    if (outputHasExplicitNdiSettings(output, outputIndex)) {
      continue;
    }

    int sourceDeckIndex = std::clamp(output.hostDeckIndex, 0, deckCount - 1);
    const Deck& deck = project.decks[sourceDeckIndex];
    std::string legacyFillName = trim(deck.ndiSourceName);
    std::string legacyKeyName = trim(deck.ndiKeySourceName);
    bool hasLegacy = deck.ndiEnabled || deck.ndiKeyEnabled || !legacyFillName.empty() || !legacyKeyName.empty();
    if (!hasLegacy) {
      continue;
    }

    output.ndiEnabled = deck.ndiEnabled || deck.ndiKeyEnabled;
    output.ndiKeyEnabled = deck.ndiKeyEnabled;
    if (!legacyFillName.empty()) {
      output.ndiSourceName = legacyFillName;
    }
    if (!legacyKeyName.empty()) {
      output.ndiKeySourceName = legacyKeyName;
    }
    if (trim(output.ndiSourceName).empty()) {
      output.ndiSourceName = defaultOutputNdiSourceName(output, outputIndex);
    }
    if (trim(output.ndiKeySourceName).empty()) {
      output.ndiKeySourceName = defaultOutputNdiKeySourceName(output, outputIndex);
    }
    if (!output.ndiEnabled) {
      output.ndiKeyEnabled = false;
    }
  }
}

void normalizeProject(Project& project) {
  // EVERY PLAYLIST HAS ITS OWN KEY (Deck::runtimeKey). A new or opened one has
  // none yet; a playlist copied whole has its original's, and the copy gets
  // one of its own -- the first keeps the engine. Keys only ever come from
  // here, counting up, so one from an old undo step is never handed out again.
  {
    static std::uint64_t nextRuntimeKey = 1;
    for (std::size_t i = 0; i < project.decks.size(); ++i) {
      Deck& deck = project.decks[i];
      bool taken = deck.runtimeKey == 0;
      for (std::size_t j = 0; j < i && !taken; ++j) {
        taken = project.decks[j].runtimeKey == deck.runtimeKey;
      }
      if (taken) {
        deck.runtimeKey = nextRuntimeKey++;
      } else if (deck.runtimeKey >= nextRuntimeKey) {
        nextRuntimeKey = deck.runtimeKey + 1;
      }
    }
  }
  // A MASTER CUE BELONGS TO THE TRACKER, NEVER TO A PLAYLIST. James,
  // 2026-10-07: "as of now, I don't think master cues should exist as cues in
  // a deck." Any found in one -- a show from before the tracker kept its own
  // steps, a paste, an undo -- moves into the tracker here, in deck then cue
  // order and ahead of the tracker's own steps, which is the order the
  // tracker already showed them in. Every index into the playlist is moved
  // with it, so the cue on air keeps its identity.
  {
    std::vector<Cue> migrated;
    for (Deck& deck : project.decks) {
      std::vector<Cue> fromDeck;
      for (int c = static_cast<int>(deck.cues.size()) - 1; c >= 0; --c) {
        if (deck.cues[c].kind != CueKind::Master) {
          continue;
        }
        auto shiftIndex = [c](int& index) {
          if (index == c) index = -1;
          else if (index > c) index -= 1;
        };
        shiftIndex(deck.activeIndex);
        shiftIndex(deck.standbyIndex);
        const bool wasSelected = deck.selectedIndex == c;
        shiftIndex(deck.selectedIndex);
        if (wasSelected && !deck.cues.empty()) {
          deck.selectedIndex = std::max(0, c - 1);
        }
        std::vector<int> kept;
        for (int index : deck.selectedIndices) {
          shiftIndex(index);
          if (index >= 0) kept.push_back(index);
        }
        deck.selectedIndices.swap(kept);
        fromDeck.insert(fromDeck.begin(), std::move(deck.cues[c]));
        deck.cues.erase(deck.cues.begin() + c);
      }
      deck.isMasterDeck = false;
      for (Cue& cue : fromDeck) {
        migrated.push_back(std::move(cue));
      }
    }
    if (!migrated.empty()) {
      project.trackerSteps.insert(project.trackerSteps.begin(),
                                  std::make_move_iterator(migrated.begin()),
                                  std::make_move_iterator(migrated.end()));
    }
  }
  if (project.decks.empty()) {
    project.decks.push_back(Deck {});
  }
  for (size_t index = 0; index < project.decks.size(); ++index) {
    normalizeDeck(project.decks[index], static_cast<int>(index));
  }
  // Migration (v0.78.6): Pocket Test carries the A/V sync pop, but pattern
  // cues created before v0.78.5 were hard-muted by addPatternCue and had
  // hasAudio=false (which hides the audio controls, so the mute couldn't
  // even be lifted). hasAudio=false is the "never migrated" marker: flip
  // such cues to audible once; operator mutes made afterwards persist
  // because they leave hasAudio=true.
  for (Deck& deck : project.decks) {
    for (Cue& cue : deck.cues) {
      if (cue.kind == CueKind::Pattern &&
          stripPatternMotionSuffix(normalizePatternTypeId(cue.path)) == "pocket-test" &&
          !cue.hasAudio) {
        cue.hasAudio = true;
        cue.audioEnabled = true;
      }
    }
  }
  for (size_t index = 0; index < project.decks.size(); ++index) {
    Deck& deck = project.decks[index];
    if (deck.outputRouteDeckIndex < 0 ||
        deck.outputRouteDeckIndex >= static_cast<int>(project.decks.size())) {
      deck.outputRouteDeckIndex = static_cast<int>(index);
    }
  }
  // The tracker's own steps: always masters, each with an id of its own --
  // the playhead and every open menu find a step by id.
  {
    std::unordered_set<std::string> usedStepIds;
    for (int s = 0; s < static_cast<int>(project.trackerSteps.size()); ++s) {
      Cue& step = project.trackerSteps[s];
      step.kind = CueKind::Master;
      if (step.id.empty()) {
        step.id = makeCueId(step, -1, s);
      }
      const std::string baseId = step.id;
      int dedupe = 2;
      while (usedStepIds.find(step.id) != usedStepIds.end()) {
        step.id = baseId + "-" + std::to_string(dedupe++);
      }
      usedStepIds.insert(step.id);
    }
  }
  normalizeProjectOutputsAndLayers(project);
  migrateLegacyDeckNdiToOutputs(project);
  project.focusedDeckIndex = std::clamp(project.focusedDeckIndex, 0, static_cast<int>(project.decks.size()) - 1);
  project.outputRenderWidth = std::clamp(project.outputRenderWidth, 320, 7680);
  project.outputRenderHeight = std::clamp(project.outputRenderHeight, 180, 4320);
  if (!std::isfinite(project.outputRefreshRateHz) || project.outputRefreshRateHz < 0.0) {
    project.outputRefreshRateHz = 0.0;
  }
  project.outputRefreshRateHz = std::min(project.outputRefreshRateHz, 240.0);
  if (project.outputBitDepth != 0 && project.outputBitDepth != 8 && project.outputBitDepth != 10) {
    project.outputBitDepth = 0;
  }
  project.outputCanvasWidth = std::clamp(project.outputCanvasWidth, 320, 16384);
  project.outputCanvasHeight = std::clamp(project.outputCanvasHeight, 180, 16384);
  project.advancedOutputMode = project.advancedOutputMode || project.decks.size() > 1;
  // ── ONE PLAYLIST IS PLAIN DECKBOY AGAIN ────────────────────────────────
  //
  // "when going from two or more decks back to one deck, deckboy should morph
  // back to deckboy. no need for multiview or other multi out complications."
  //
  // The multiview of a single playlist is the programme and one tile of the
  // same picture, which is a worse monitor than the monitor. Turned off here
  // rather than in removeDeck so it is also true for a show that arrives with
  // one deck and the flag set.
  if (project.decks.size() <= 1) {
    project.multiviewMode = 0;
  }
  project.jumpMode = normalizeJumpModeToken(project.jumpMode);
  project.panicProfile = normalizePanicProfileToken(project.panicProfile);
  project.uiTransitionsEnabled = true;
  project.oscQueryPort = normalizeOscQueryPort(project.oscQueryPort);
  project.oscFeedbackRateMs = normalizeOscFeedbackRateMs(project.oscFeedbackRateMs);
  project.artNetPort = normalizeArtNetPort(project.artNetPort);
  project.panicFadeSeconds = std::clamp(
    std::isfinite(project.panicFadeSeconds) ? project.panicFadeSeconds : 0.9,
    0.1, 5.0);
}
