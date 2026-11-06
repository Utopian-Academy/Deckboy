  // ── THE MIDI MAP, wired to the desk ───────────────────────────────────────
  //
  // The engine (matching, knobs, holds, pickup, coalescing) is
  // core/midi_map.hpp and is tested on its own (CTest midi_map). This half
  // owns what the engine must not: the threads, the show, aiming a command at
  // a cue without moving the operator's focus, the monitor, and saving.
  //
  // MIDI and OSC threads only QUEUE messages. Everything that reads the map or
  // the show happens here, on the main thread, once a frame -- which is also
  // what lets forty values of one knob in one frame become one command.

  struct MidiMapQueued {
    deckboy::midimap::Message msg;
    bool fromPeer = false;       // OSC from another machine: local-only verbs refused
    std::string from;            // the port, or the OSC sender
  };

  // ── Threads ──

  // Every channel message from the MIDI thread (RtMidi or ALSA), and MIDIIN.
  void onMidiChannelMessage(int status, int channel, int data1, int data2) {
    namespace mm = deckboy::midimap;
    mm::Message m;
    m.channel = std::clamp(channel, 1, 16);
    // The port's real name, or empty when it is unknown -- never the
    // monitor's "first port" placeholder, which no row could match.
    {
      std::lock_guard<std::mutex> lock(inputMonitorMutex_);
      m.port = midiMonitorPortName_;
    }
    switch (status) {
      case 0x90: m.source = mm::Source::Note; m.number = data1; m.value = data2; m.on = data2 > 0; break;
      case 0x80: m.source = mm::Source::Note; m.number = data1; m.value = 0; m.on = false; break;
      case 0xB0: m.source = mm::Source::Cc; m.number = data1; m.value = data2; break;
      case 0xE0: m.source = mm::Source::PitchBend; m.value = std::clamp(data1, 0, 16383); break;
      case 0xC0: m.source = mm::Source::Program; m.number = data1; break;
      case 0xD0: m.source = mm::Source::Pressure; m.value = data1; break;
      case 0xA0: m.source = mm::Source::PolyPressure; m.number = data1; m.value = data2; break;
      default: return;
    }
    m.number = std::clamp(m.number, 0, 127);
    if (m.source != mm::Source::PitchBend) m.value = std::clamp(m.value, 0, 127);
    // Synth mode, as before: every note plays the live synth. (A map row of
    // the PLAY kind does the same for one channel or one range of keys.)
    if (project_.midiToSynth && m.source == mm::Source::Note) {
      queueMidiCommand(m.on ? ("SYNTHNOTEON " + std::to_string(m.number) + " " + std::to_string(m.value))
                            : ("SYNTHNOTEOFF " + std::to_string(m.number)),
                       mm::describeMessage(m));
      return;
    }
    MidiMapQueued q;
    q.from = m.port.empty() ? midiMonitorPort() : m.port;
    q.msg = std::move(m);
    queueMidiMapMessage(std::move(q));
  }

  void queueMidiMapMessage(MidiMapQueued q) {
    std::lock_guard<std::mutex> lock(midiMapQueueMutex_);
    // A runaway sender cannot grow this without bound. Keys are what matter
    // most and a knob is coalesced anyway, so the oldest knob values go first.
    if (midiMapQueue_.size() >= 4096) {
      auto it = std::find_if(midiMapQueue_.begin(), midiMapQueue_.end(), [](const MidiMapQueued& e) {
        return deckboy::midimap::continuousSource(e.msg.source) ||
               e.msg.source == deckboy::midimap::Source::Cc;
      });
      midiMapQueue_.erase(it != midiMapQueue_.end() ? it : midiMapQueue_.begin());
    }
    midiMapQueue_.push_back(std::move(q));
  }

  // The network thread asks before it treats an address as a built-in verb:
  // a mapped address belongs to the map. Rebuilt whenever the maps change.
  bool oscAddressIsMapped(const std::string& address) {
    const std::string a = deckboy::midimap::normalizedOsc(address);
    std::lock_guard<std::mutex> lock(midiMapOscMutex_);
    for (const std::string& mapped : midiMapOscAddresses_) {
      if (mapped.empty() || mapped == a) return true;
    }
    return false;
  }

  void queueOscForMidiMap(const OscMessage& osc, bool fromPeer, const std::string& from) {
    MidiMapQueued q;
    q.msg.source = deckboy::midimap::Source::Osc;
    q.msg.oscAddress = trim(osc.address);
    for (const auto& arg : osc.args) {
      if (auto n = oscArgAsNumber(arg)) {
        q.msg.hasOscValue = true;
        q.msg.oscValue = *n;
        break;
      }
    }
    q.fromPeer = fromPeer;
    q.from = from;
    queueMidiMapMessage(std::move(q));
  }

  // ── Saving ──

  static fs::path midiMapComputerPath() {
    return Paths::stateDir() / "midi_map.tsv";
  }

  void ensureMidiMapLoaded() {
    if (midiMapLoaded_) return;
    midiMapLoaded_ = true;
    midiMapComputerRows_.clear();
    std::error_code ec;
    const auto path = midiMapComputerPath();
    if (!fs::exists(path, ec)) {
      // Never edited on this computer: today's behaviour, as rows. Nothing is
      // written until someone changes something.
      midiMapComputerRows_ = deckboy::midimap::builtInRows();
      ++midiMapComputerVersion_;
      return;
    }
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line[0] == '#') continue;
      if (auto row = deckboy::midimap::rowFromLine(line)) {
        midiMapComputerRows_.push_back(*row);
      } else {
        showLog("MIDI-MAP", "skipped a row it could not read: " + line.substr(0, 80));
      }
    }
    ++midiMapComputerVersion_;
  }

  bool saveComputerMidiMap() {
    std::error_code ec;
    const auto path = midiMapComputerPath();
    fs::create_directories(path.parent_path(), ec);
    const auto temp = path.string() + ".tmp";
    {
      std::ofstream out(temp, std::ios::binary | std::ios::trunc);
      if (!out) return false;
      out << "# Deckboy MIDI map for this computer. One row per line; see MIDIMAP.\n";
      for (const auto& row : midiMapComputerRows_) out << deckboy::midimap::rowToLine(row) << '\n';
      if (!out) return false;
    }
    fs::rename(temp, path, ec);
    if (ec) {
      fs::remove(path, ec);
      ec.clear();
      fs::rename(temp, path, ec);
    }
    ++midiMapComputerVersion_;
    return !ec;
  }

  std::vector<deckboy::midimap::Row> showMidiMapRows() const {
    std::vector<deckboy::midimap::Row> rows;
    for (const std::string& line : project_.midiMapRows) {
      if (auto row = deckboy::midimap::rowFromLine(line)) rows.push_back(*row);
    }
    return rows;
  }

  // The engine is rebuilt only when a map actually changed (a new show, an
  // edit), because rebuilding forgets where every knob was.
  void refreshMidiMapEngine() {
    ensureMidiMapLoaded();
    std::string sig = std::to_string(midiMapComputerVersion_);
    for (const std::string& line : project_.midiMapRows) {
      sig += '\x1f';
      sig += line;
    }
    if (sig == midiMapSignature_) return;
    midiMapSignature_ = std::move(sig);
    midiMap_.setRows(showMidiMapRows(), midiMapComputerRows_);
    std::vector<std::string> osc;
    for (auto origin : {deckboy::midimap::Origin::Show, deckboy::midimap::Origin::Computer}) {
      for (const auto& row : midiMap_.rows(origin)) {
        if (row.enabled && row.source == deckboy::midimap::Source::Osc) {
          osc.push_back(deckboy::midimap::normalizedOsc(row.oscAddress));
        }
      }
    }
    std::lock_guard<std::mutex> lock(midiMapOscMutex_);
    midiMapOscAddresses_ = std::move(osc);
  }

  // ── Aiming ──

  struct MidiMapAim {
    int deck = -1;               // -1: the desk, nothing aimed
    int select = -1;             // the cue to stand in as "selected"; -1 leave it
    std::string error;
  };

  MidiMapAim resolveMidiMapAim(const deckboy::midimap::Row& row) {
    using deckboy::midimap::Target;
    MidiMapAim aim;
    if (row.target == Target::Desk) return aim;
    const int decks = static_cast<int>(project_.decks.size());
    const int deckIndex = row.playlist > 0 ? row.playlist - 1 : project_.focusedDeckIndex;
    if (row.target != Target::Named && (deckIndex < 0 || deckIndex >= decks)) {
      aim.error = "there is no playlist " + std::to_string(deckIndex + 1);
      return aim;
    }
    switch (row.target) {
      case Target::Selected:
        aim.deck = deckIndex;
        break;
      case Target::OnAir: {
        const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
        if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
          aim.error = "nothing on air in playlist " + std::to_string(deckIndex + 1);
          return aim;
        }
        aim.deck = deckIndex;
        aim.select = deck.activeIndex;
        break;
      }
      case Target::Named: {
        // The playlist named, or the focused one first and then the rest.
        std::vector<int> order;
        if (row.playlist > 0) {
          if (row.playlist - 1 < decks) order.push_back(row.playlist - 1);
        } else {
          if (project_.focusedDeckIndex >= 0 && project_.focusedDeckIndex < decks) {
            order.push_back(project_.focusedDeckIndex);
          }
          for (int d = 0; d < decks; ++d) {
            if (d != project_.focusedDeckIndex) order.push_back(d);
          }
        }
        for (int d : order) {
          const Deck& deck = project_.decks[static_cast<std::size_t>(d)];
          std::optional<int> index = cueIndexByToken(deck, row.cue);
          if (!index) {
            for (int c = 0; c < static_cast<int>(deck.cues.size()); ++c) {
              if (toUpper(deck.cues[static_cast<std::size_t>(c)].name) == toUpper(row.cue)) {
                index = c;
                break;
              }
            }
          }
          if (index) {
            aim.deck = d;
            aim.select = *index;
            return aim;
          }
        }
        aim.error = "no cue \"" + row.cue + "\"";
        break;
      }
      case Target::Desk:
        break;
    }
    return aim;
  }

  const Cue* midiMapAimedCue(const deckboy::midimap::Row& row) {
    const MidiMapAim aim = resolveMidiMapAim(row);
    if (aim.deck < 0 || !aim.error.empty()) {
      return row.target == deckboy::midimap::Target::Desk ? selectedCuePtr() : nullptr;
    }
    const Deck& deck = project_.decks[static_cast<std::size_t>(aim.deck)];
    const int index = aim.select >= 0 ? aim.select : deck.selectedIndex;
    if (index < 0 || index >= static_cast<int>(deck.cues.size())) return nullptr;
    return &deck.cues[static_cast<std::size_t>(index)];
  }

  // Run a command AT the row's target. The operator's focus and selection are
  // put back exactly as they were: a knob turning never moves the desk.
  bool runMidiMapCommand(const deckboy::midimap::Row& row, const std::string& command, bool fromPeer,
                         std::string& outcome) {
    const MidiMapAim aim = resolveMidiMapAim(row);
    if (!aim.error.empty()) {
      outcome = aim.error;
      return false;
    }
    const int savedFocus = project_.focusedDeckIndex;
    const int savedOutput = project_.focusedOutputIndex;
    int savedSelect = -1;
    if (aim.deck >= 0) {
      Deck& deck = project_.decks[static_cast<std::size_t>(aim.deck)];
      savedSelect = deck.selectedIndex;
      project_.focusedDeckIndex = aim.deck;
      if (aim.select >= 0) deck.selectedIndex = aim.select;
    }
    const bool savedNetwork = remoteCallerIsNetwork_;
    remoteCallerIsNetwork_ = fromPeer;
    remoteCommandDetail_.clear();
    handleRemoteCommand(command);
    remoteCallerIsNetwork_ = savedNetwork;
    if (aim.deck >= 0) {
      if (aim.deck < static_cast<int>(project_.decks.size())) {
        Deck& deck = project_.decks[static_cast<std::size_t>(aim.deck)];
        if (aim.select >= 0 && savedSelect < static_cast<int>(deck.cues.size())) {
          deck.selectedIndex = savedSelect;
        }
      }
      if (savedFocus >= 0 && savedFocus < static_cast<int>(project_.decks.size())) {
        project_.focusedDeckIndex = savedFocus;
      }
      project_.focusedOutputIndex = savedOutput;
    }
    if (!remoteCommandRecognized_) {
      outcome = "no command called " + toUpper(splitWhitespace(command).empty()
                                                   ? std::string() : splitWhitespace(command)[0]);
      return false;
    }
    if (!remoteCommandError_.empty()) {
      outcome = remoteCommandError_;
      return false;
    }
    outcome = remoteCommandDetail_;
    return true;
  }

  std::string midiMapTargetLabel(const deckboy::midimap::Row& row) const {
    using deckboy::midimap::Target;
    const std::string pl = row.playlist > 0 ? "playlist " + std::to_string(row.playlist)
                                            : std::string("the focused playlist");
    switch (row.target) {
      case Target::Selected: return "the selected cue in " + pl;
      case Target::OnAir: return "on air in " + pl;
      case Target::Named: return "cue \"" + row.cue + "\"";
      case Target::Desk: break;
    }
    return "the desk";
  }

  static std::string midiMapRowLabel(deckboy::midimap::Origin origin, int row) {
    return std::string(origin == deckboy::midimap::Origin::Show ? "show" : "computer") +
           " map row " + std::to_string(row + 1);
  }

  // What a row's target is worth now, for pickup and for an encoder's start.
  std::optional<double> midiMapCurrentValue(const deckboy::midimap::Row& row) {
    const auto words = splitWhitespace(row.command);
    if (words.empty()) return std::nullopt;
    const std::string verb = toUpper(words[0]);
    if (verb == "MASTERVOL" || verb == "MASTERVOLUME") return project_.masterVolume * 100.0;
    if (verb == "SPEED") {
      if (const Cue* cue = midiMapAimedCue(row)) return cue->playbackSpeed;
    }
    return std::nullopt;
  }

  // ── Once a frame ──

  // A cue's own TRIGGERS note, and TRIGGERS learn, come before the map: they
  // are the cue's claim. True when the note was taken.
  bool midiMapClaimNoteForCue(const MidiMapQueued& q) {
    const int note = q.msg.number;
    if (!triggerLearnMidiCueId_.empty()) {
      handleMidiTriggerNote(note);  // learns it for the cue waiting
      InputMonitorEntry e;
      e.kind = "MIDI";
      e.from = q.from;
      e.message = deckboy::midimap::describeMessage(q.msg);
      e.what = "learned for the cue's TRIGGERS";
      e.midiNote = note;
      recordInputMonitor(std::move(e));
      return true;
    }
    const int fired = fireCuesWhere([&](const Cue& cue) { return cue.triggerMidiNote == note; },
                                    "MIDI " + std::to_string(note));
    if (fired <= 0) return false;
    InputMonitorEntry e;
    e.kind = "MIDI";
    e.from = q.from;
    e.message = deckboy::midimap::describeMessage(q.msg);
    e.what = "took " + std::to_string(fired) +
             (fired == 1 ? " cue that claims note " : " cues that claim note ") + std::to_string(note);
    e.midiNote = note;
    recordInputMonitor(std::move(e));
    return true;
  }

  void serviceMidiMap() {
    namespace mm = deckboy::midimap;
    refreshMidiMapEngine();
    std::vector<MidiMapQueued> batch;
    {
      std::lock_guard<std::mutex> lock(midiMapQueueMutex_);
      batch.swap(midiMapQueue_);
    }
    const double now = SDL_GetTicks() / 1000.0;
    if (!batch.empty()) {
      // LEARN: the next real message is the answer, and nothing else happens
      // with it -- learning a key must not also fire what it used to do.
      if (midiMapLearnArmed_) {
        for (const auto& q : batch) {
          const bool noise = q.msg.source == mm::Source::Note && !q.msg.on;
          if (noise) continue;
          midiMapLearnArmed_ = false;
          midiMapLearned_ = q.msg;
          midiMapLearnedValues_.clear();
          triggerToast("learned: " + mm::describeMessage(q.msg));
          break;
        }
        // The turns that follow tell an encoder from a knob.
        if (!midiMapLearnArmed_ && midiMapLearned_) {
          for (const auto& q : batch) {
            if (q.msg.source == midiMapLearned_->source && q.msg.number == midiMapLearned_->number &&
                midiMapLearnedValues_.size() < 16) {
              midiMapLearnedValues_.push_back(q.msg.value);
            }
          }
        }
        return;
      }
      if (midiMapLearned_ && midiMapLearnedValues_.size() < 16) {
        for (const auto& q : batch) {
          if (q.msg.source == midiMapLearned_->source && q.msg.number == midiMapLearned_->number) {
            midiMapLearnedValues_.push_back(q.msg.value);
          }
        }
      }
      std::vector<int> claimed;
      std::vector<mm::Message> messages;
      messages.reserve(batch.size());
      for (const auto& q : batch) {
        if (q.msg.source == mm::Source::Note && q.msg.on && midiMapClaimNoteForCue(q)) {
          claimed.push_back(q.msg.number);
        }
        messages.push_back(q.msg);
      }
      const auto actions = midiMap_.process(
        messages, now,
        [this](mm::Origin, int, const mm::Row& row) { return midiMapCurrentValue(row); }, claimed);
      for (const auto& action : actions) {
        const MidiMapQueued* q =
          action.message >= 0 && action.message < static_cast<int>(batch.size())
            ? &batch[static_cast<std::size_t>(action.message)] : nullptr;
        runMidiMapAction(action, q);
      }
    }
    for (const auto& action : midiMap_.tick(now)) runMidiMapAction(action, nullptr);
  }

  void runMidiMapAction(const deckboy::midimap::Action& action, const MidiMapQueued* q) {
    namespace mm = deckboy::midimap;
    InputMonitorEntry e;
    e.kind = q && q->msg.source == mm::Source::Osc ? "OSC" : "MIDI";
    e.from = q ? q->from : midiMonitorPort();
    const mm::Row* row = nullptr;
    if (action.row >= 0 && action.row < static_cast<int>(midiMap_.rows(action.origin).size())) {
      row = &midiMap_.rows(action.origin)[static_cast<std::size_t>(action.row)];
    }
    e.message = q ? mm::describeMessage(q->msg) : (row ? mm::describeSource(*row) : std::string());
    if (q && q->msg.source == mm::Source::Note && q->msg.on) e.midiNote = q->msg.number;
    if (q && q->msg.source == mm::Source::Osc) e.oscAddress = q->msg.oscAddress;
    const std::string where = row ? " \xC2\xB7 " + midiMapRowLabel(action.origin, action.row) : std::string();
    if (action.command.empty() || !row) {
      e.outcome = 'N';
      e.what = action.note + where;
      if (!e.what.empty()) recordInputMonitor(std::move(e));
      return;
    }
    e.command = action.command;
    std::string outcome;
    const bool ok = runMidiMapCommand(*row, action.command, q && q->fromPeer, outcome);
    e.outcome = ok ? 'D' : 'R';
    std::string what = outcome;
    if (!action.note.empty()) what = what.empty() ? action.note : action.note + ", " + what;
    if (row->target != mm::Target::Desk) {
      what += (what.empty() ? "" : " \xC2\xB7 ") + midiMapTargetLabel(*row);
    }
    e.what = what + where;
    recordInputMonitor(std::move(e));
  }

  // ── MIDIMAP over the wire ──
  //
  //   MIDIMAP [LIST]                       every row of both maps, as JSON
  //   MIDIMAP ADD COMPUTER|SHOW <fields>   fields as "src=cc | num=16 | cmd=SPEED {value} | ..."
  //   MIDIMAP SET COMPUTER|SHOW <n> <fields>
  //   MIDIMAP REMOVE COMPUTER|SHOW <n>
  //   MIDIMAP CLEAR COMPUTER|SHOW
  //   MIDIMAP RESET                        this computer's map back to the built-in rows
  //   MIDIMAP LEARN | LEARNED | LEARN OFF  arm learn; read what was learned, as fields
  static std::string midiMapFieldsToLine(const std::string& text) {
    std::string line;
    std::size_t start = 0;
    while (start <= text.size()) {
      std::size_t bar = text.find('|', start);
      if (bar == std::string::npos) bar = text.size();
      std::string field = trim(text.substr(start, bar - start));
      if (!field.empty()) {
        if (!line.empty()) line += '\t';
        line += field;
      }
      start = bar + 1;
    }
    return line;
  }

  static std::string restAfterWords(const std::string& raw, int words) {
    std::size_t i = 0;
    for (int w = 0; w < words; ++w) {
      while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
      while (i < raw.size() && !std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    }
    while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    return raw.substr(i);
  }

  std::string midiMapListJson() {
    refreshMidiMapEngine();
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (auto origin : {deckboy::midimap::Origin::Show, deckboy::midimap::Origin::Computer}) {
      const auto& rows = midiMap_.rows(origin);
      for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        bool overlap = false;
        for (auto o2 : {deckboy::midimap::Origin::Show, deckboy::midimap::Origin::Computer}) {
          const auto& others = midiMap_.rows(o2);
          for (int j = 0; j < static_cast<int>(others.size()); ++j) {
            if (o2 == origin && j == i) continue;
            if (deckboy::midimap::overlaps(r, others[static_cast<std::size_t>(j)])) overlap = true;
          }
        }
        // A cue's own TRIGGERS note answering the same key.
        std::string cueClaim;
        if (r.source == deckboy::midimap::Source::Note) {
          for (const Deck& deck : project_.decks) {
            for (const Cue& cue : deck.cues) {
              if (cue.triggerMidiNote >= 0 && deckboy::midimap::rowClaimsNote(r, cue.triggerMidiNote)) {
                cueClaim = cue.name;
              }
            }
          }
        }
        if (!first) out << ',';
        first = false;
        out << "{\"map\":\"" << (origin == deckboy::midimap::Origin::Show ? "show" : "computer")
            << "\",\"row\":" << (i + 1)
            << ",\"message\":\"" << escapeJson(deckboy::midimap::describeSource(r))
            << "\",\"read\":\"" << escapeJson(deckboy::midimap::readLabel(r.read))
            << "\",\"command\":\"" << escapeJson(r.command)
            << "\",\"on\":\"" << escapeJson(midiMapTargetLabel(r))
            << "\",\"builtin\":" << (r.builtIn ? "true" : "false")
            << ",\"enabled\":" << (r.enabled ? "true" : "false")
            << ",\"overlap\":" << (overlap ? "true" : "false")
            << ",\"cue_claims\":\"" << escapeJson(cueClaim)
            << "\",\"line\":\"" << escapeJson(deckboy::midimap::rowToLine(r)) << "\"}";
      }
    }
    out << ']';
    return out.str();
  }

  void handleMidiMapCommand(const std::string& raw) {
    namespace mm = deckboy::midimap;
    ensureMidiMapLoaded();
    const auto parts = splitWhitespace(raw);
    const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string("LIST");
    auto originOf = [&](std::size_t at) -> std::optional<mm::Origin> {
      if (parts.size() <= at) return std::nullopt;
      const std::string w = toUpper(parts[at]);
      if (w == "COMPUTER") return mm::Origin::Computer;
      if (w == "SHOW") return mm::Origin::Show;
      return std::nullopt;
    };
    // The map is one more place a command is STORED to run later from this
    // computer's own MIDI port, so another machine may not plant a verb it
    // could not run itself -- the DASH SET rule.
    auto refusedFromPeer = [&](const mm::Row& row) {
      if (!remoteCallerIsNetwork_) return false;
      return remoteVerbIsLocalOnly(row.command) ||
             (!row.releaseCommand.empty() && remoteVerbIsLocalOnly(row.releaseCommand));
    };
    auto commit = [&](mm::Origin origin, std::vector<mm::Row> rows) {
      if (origin == mm::Origin::Computer) {
        midiMapComputerRows_ = std::move(rows);
        if (!saveComputerMidiMap()) {
          failRemoteCommand("MIDIMAP: could not save " + midiMapComputerPath().string());
          return false;
        }
      } else {
        pushUndoSnapshot();
        project_.midiMapRows.clear();
        for (const auto& r : rows) project_.midiMapRows.push_back(mm::rowToLine(r));
        markProjectDirty();
      }
      refreshMidiMapEngine();
      return true;
    };
    auto rowsOf = [&](mm::Origin origin) {
      return origin == mm::Origin::Computer ? midiMapComputerRows_ : showMidiMapRows();
    };
    if (sub == "LIST") {
      remoteCommandDetail_ = midiMapListJson();
      return;
    }
    if (sub == "ADD" || sub == "SET") {
      const auto origin = originOf(2);
      const bool set = sub == "SET";
      if (!origin || (set && parts.size() < 5) || (!set && parts.size() < 4)) {
        failRemoteCommand("MIDIMAP " + sub + ": COMPUTER|SHOW " + (set ? "<row> " : "") +
                          "src=... | cmd=... | ...");
        return;
      }
      auto row = mm::rowFromLine(midiMapFieldsToLine(restAfterWords(raw, set ? 4 : 3)));
      if (!row) {
        failRemoteCommand("MIDIMAP " + sub + ": a row needs src= (note, cc, pitch, program, pressure, "
                          "polypressure or osc) and cmd=");
        return;
      }
      if (refusedFromPeer(*row)) {
        failRemoteCommand("MIDIMAP: that command is only accepted from this computer");
        return;
      }
      auto rows = rowsOf(*origin);
      int index = static_cast<int>(rows.size());
      if (set) {
        index = std::atoi(parts[3].c_str()) - 1;
        if (index < 0 || index >= static_cast<int>(rows.size())) {
          failRemoteCommand("MIDIMAP SET: there is no row " + parts[3]);
          return;
        }
        rows[static_cast<std::size_t>(index)] = *row;
      } else {
        rows.push_back(*row);
      }
      if (commit(*origin, std::move(rows))) {
        remoteCommandDetail_ = midiMapRowLabel(*origin, index) + ": " + mm::describeSource(*row) +
                               " -> " + row->command;
      }
      return;
    }
    if (sub == "REMOVE") {
      const auto origin = originOf(2);
      if (!origin || parts.size() < 4) {
        failRemoteCommand("MIDIMAP REMOVE: COMPUTER|SHOW <row>");
        return;
      }
      auto rows = rowsOf(*origin);
      const int index = std::atoi(parts[3].c_str()) - 1;
      if (index < 0 || index >= static_cast<int>(rows.size())) {
        failRemoteCommand("MIDIMAP REMOVE: there is no row " + parts[3]);
        return;
      }
      rows.erase(rows.begin() + index);
      if (commit(*origin, std::move(rows))) remoteCommandDetail_ = "removed";
      return;
    }
    if (sub == "CLEAR") {
      const auto origin = originOf(2);
      if (!origin) {
        failRemoteCommand("MIDIMAP CLEAR: COMPUTER or SHOW");
        return;
      }
      if (commit(*origin, {})) remoteCommandDetail_ = "cleared";
      return;
    }
    if (sub == "RESET") {
      if (commit(mm::Origin::Computer, mm::builtInRows())) remoteCommandDetail_ = "built-in rows";
      return;
    }
    if (sub == "LEARN") {
      const bool off = parts.size() > 2 && toUpper(parts[2]) == "OFF";
      midiMapLearnArmed_ = !off;
      if (!off) {
        midiMapLearned_.reset();
        midiMapLearnedValues_.clear();
      }
      remoteCommandDetail_ = off ? "off" : "move a knob or press a key";
      return;
    }
    if (sub == "LEARNED") {
      if (!midiMapLearned_) {
        remoteCommandDetail_ = midiMapLearnArmed_ ? "waiting" : "nothing learned";
        return;
      }
      // As fields, ready for MIDIMAP ADD: the message, and a guess at how to
      // read it from the values that followed.
      const mm::Message& m = *midiMapLearned_;
      std::string f = std::string("src=") + mm::sourceToken(m.source);
      if (m.source == mm::Source::Osc) {
        f += " | osc=" + m.oscAddress;
      } else {
        if (!m.port.empty()) f += " | port=" + m.port;
        f += " | ch=" + std::to_string(m.channel);
        if (m.source == mm::Source::Note || m.source == mm::Source::Cc ||
            m.source == mm::Source::Program || m.source == mm::Source::PolyPressure) {
          f += " | num=" + std::to_string(m.number);
        }
      }
      std::string read = "press";
      if (m.source == mm::Source::Cc) {
        const auto kind = mm::guessEncoder(midiMapLearnedValues_);
        bool spread = false;
        for (int v : midiMapLearnedValues_) {
          if (v > 15 && v < 49) spread = true;
          if (v > 79 && v < 113) spread = true;
        }
        read = (!spread && kind != mm::EncoderMode::Auto && midiMapLearnedValues_.size() >= 3)
                 ? "encoder | enc=" + std::string(mm::encoderToken(kind)) : "knob";
      } else if (mm::continuousSource(m.source) || (m.source == mm::Source::Osc && m.hasOscValue)) {
        read = "knob";
      }
      f += " | read=" + read;
      remoteCommandDetail_ = f;
      return;
    }
    failRemoteCommand("MIDIMAP: LIST, ADD, SET, REMOVE, CLEAR, RESET, LEARN or LEARNED");
  }
