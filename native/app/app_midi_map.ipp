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
    // The ~ chip's MIDI shapes read this the moment it arrives.
    deckboy::effects::publishMidiToLfo(status, channel, data1, data2);
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

  // ── LED feedback ──
  //
  // A row lights the controller it came from: the port the MIDI input is on,
  // or the row's own port -- never every MIDI device on the machine. Worked
  // out again every 50 ms from the show itself, so a cue taken with the mouse,
  // a playlist change or a new show all reach the pads. Only changes are sent,
  // and everything is sent again when the port, the map or the show changes.

  // Is a key's thing "on" now? Unknown commands light nothing (nullopt).
  std::optional<bool> midiMapRowLit(const deckboy::midimap::Row& r) {
    using deckboy::midimap::Target;
    const auto words = splitWhitespace(r.command);
    if (words.empty()) return std::nullopt;
    const std::string verb = toUpper(words[0]);
    if (verb == "BLACKOUT") return masterDimmerTarget_ < 0.5;
    const MidiMapAim aim = resolveMidiMapAim(r);
    if (!aim.error.empty()) return false;
    const int deckIndex = aim.deck >= 0 ? aim.deck : project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return std::nullopt;
    const Deck& deck = project_.decks[static_cast<std::size_t>(deckIndex)];
    if (r.target == Target::Named) return deck.activeIndex == aim.select;
    if ((verb == "GOTO" || verb == "TAKE") && words.size() > 1) {
      const auto index = cueIndexByToken(deck, joinParts(words, 1));
      return index && deck.activeIndex == *index;
    }
    // A playlist's own TAKE and STOP: lit while something there is on air.
    if ((verb == "TAKE" || verb == "STOP") && r.target != Target::Desk) return deck.activeIndex >= 0;
    if (verb == "LOOP") {
      if (const Cue* cue = midiMapAimedCue(r)) return cue->loop;
    }
    return std::nullopt;
  }

  void serviceMidiMapFeedback() {
    namespace mm = deckboy::midimap;
    const Uint64 now = SDL_GetTicks();
    if (now - midiMapFeedbackAtMs_ < 50) return;
    midiMapFeedbackAtMs_ = now;
    std::string port;
    {
      std::lock_guard<std::mutex> lock(inputMonitorMutex_);
      port = midiEnabled_ ? midiMonitorPortName_ : std::string();
    }
    if (port != midiMapFeedbackPort_) {
      midiMapFeedbackPort_ = port;
      midiMapOut_.close();
      if (!port.empty() && !midiMapOut_.openByName(port)) {
        showLog("MIDI-MAP", "no MIDI output called " + port + ": the controller's lights stay as they are");
      }
      midiMapFeedbackSent_.clear();
    }
    const std::string sig = midiMapSignature_ + '\x1e' + currentProjectFile_.string();
    if (sig != midiMapFeedbackSig_) {
      midiMapFeedbackSig_ = sig;
      midiMapFeedbackSent_.clear();
    }
    // One answer per key: several rows can share a pad (amber on the MAP
    // page), and sending each row's own answer in turn flashed the pad at
    // 20 Hz. A key is lit when any of its rows is; a knob takes its first row.
    std::map<int, int> want;
    for (auto origin : {mm::Origin::Show, mm::Origin::Computer}) {
      for (const mm::Row& r : midiMap_.rows(origin)) {
        if (!r.enabled || !r.feedback || r.unclaimed) continue;
        if (!r.port.empty() && r.port != port) continue;
        if (r.source != mm::Source::Note && r.source != mm::Source::Cc) continue;
        if (r.number < 0 || r.numberTo > r.number) continue;  // a keyboard has no lights
        int value = -1;
        if (mm::isContinuous(r.read)) {
          const auto current = midiMapCurrentValue(r);
          if (!current) continue;
          value = mm::value7(mm::positionOf(r, *current));
        } else {
          const auto lit = midiMapRowLit(r);
          if (!lit) continue;
          value = *lit ? r.ledOn : r.ledOff;
        }
        const int channel = (r.channel > 0 ? r.channel : 1) - 1;
        const int status = (r.source == mm::Source::Note ? 0x90 : 0xB0) | channel;
        const int key = (status << 8) | r.number;
        auto have = want.find(key);
        if (have == want.end()) {
          want[key] = value;
        } else if (!mm::isContinuous(r.read) && value == r.ledOn) {
          have->second = value;  // lit wins
        }
      }
    }
    for (const auto& [key, value] : want) {
      auto it = midiMapFeedbackSent_.find(key);
      if (it != midiMapFeedbackSent_.end() && it->second == value) continue;
      midiMapFeedbackSent_[key] = value;
      const int status = key >> 8;
      const int number = key & 0xFF;
      if (midiMapOut_.isOpen()) {
        midiMapOut_.send({static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(number),
                          static_cast<std::uint8_t>(std::clamp(value, 0, 127))});
      }
      midiMapFeedbackLog_.push_back(std::string((status & 0xF0) == 0x90 ? "note " : "CC ") +
                                    std::to_string(number) + " = " + std::to_string(value) +
                                    " ch " + std::to_string((status & 0x0F) + 1));
      if (midiMapFeedbackLog_.size() > 64) midiMapFeedbackLog_.erase(midiMapFeedbackLog_.begin());
    }
  }

  // Add a ready-made controller's rows to this computer's map (rows it
  // already has are not added twice).
  bool addMidiMapPreset(const std::string& id, std::string& detail) {
    for (const auto& map : deckboy::midimap::controllerMaps()) {
      if (map.id != id) continue;
      auto rows = midiMapRowsOf(deckboy::midimap::Origin::Computer);
      int added = 0;
      for (const auto& r : map.rows) {
        const std::string line = deckboy::midimap::rowToLine(r);
        bool have = false;
        for (const auto& existing : rows) {
          if (deckboy::midimap::rowToLine(existing) == line) have = true;
        }
        if (!have) {
          rows.push_back(r);
          ++added;
        }
      }
      // The controller now owns its messages: a built-in row on one of them
      // (CC 7 is a fader on most of these) is switched off, not left to
      // answer the same fader as well. Off, not deleted: one click back.
      int quieted = 0;
      for (auto& existing : rows) {
        if (!existing.builtIn || !existing.enabled) continue;
        for (const auto& r : map.rows) {
          if (deckboy::midimap::overlaps(existing, r)) {
            existing.enabled = false;
            ++quieted;
            break;
          }
        }
      }
      midiMapStore(deckboy::midimap::Origin::Computer, std::move(rows));
      detail = std::string(map.label) + ": " + std::to_string(added) + " rows added";
      if (quieted > 0) {
        detail += ", " + std::to_string(quieted) + (quieted == 1 ? " built-in row" : " built-in rows") +
                  " on the same controls switched off";
      }
      return true;
    }
    detail = "no ready-made map called " + id;
    return false;
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
    if (verb == "PLAYLISTOPACITY" || verb == "DECKOPACITY") {
      const MidiMapAim aim = resolveMidiMapAim(row);
      const int d = aim.deck >= 0 ? aim.deck : project_.focusedDeckIndex;
      if (aim.error.empty() && d >= 0 && d < static_cast<int>(project_.decks.size())) {
        return project_.decks[static_cast<std::size_t>(d)].playlistOpacity * 100.0;
      }
    }
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
          if (midiMapLearnInto_ != 0) midiMapLearnHeard(q.msg);
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
    serviceMidiMapFeedback();
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
    if (row) midiMapRowHitMs_[{static_cast<int>(action.origin), action.row}] = SDL_GetTicks();
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
    if (sub == "PRESET") {
      if (parts.size() < 3) {
        std::string ids;
        for (const auto& map : mm::controllerMaps()) ids += (ids.empty() ? "" : ", ") + std::string(map.id);
        remoteCommandDetail_ = ids;
        return;
      }
      std::string detail;
      if (!addMidiMapPreset(toLower(parts[2]), detail)) {
        failRemoteCommand("MIDIMAP PRESET: " + detail);
        return;
      }
      remoteCommandDetail_ = detail;
      return;
    }
    if (sub == "FEEDBACK") {
      // What the lights were last told, newest last: for tests and for an
      // operator asking why a pad is dark.
      std::string out;
      for (const auto& line : midiMapFeedbackLog_) out += (out.empty() ? "" : " | ") + line;
      remoteCommandDetail_ = (midiMapOut_.isOpen() ? "to " + midiMapFeedbackPort_ : std::string("no port open")) +
                             (out.empty() ? std::string() : ": " + out);
      return;
    }
    failRemoteCommand("MIDIMAP: LIST, ADD, SET, REMOVE, CLEAR, RESET, LEARN, LEARNED, PRESET or FEEDBACK");
  }

  // ── THE MAP PAGE (Settings, the MIDI CONTROL card's OPEN THE MAP) ─────────
  //
  // Drawn to the approved board: one row per binding -- what comes in, what
  // it runs, what it runs on, and its value now -- with a dot that lights
  // while the row's message arrives and an amber (the theme's danger colour)
  // outline where two things answer one message. Choosing a row opens the
  // three steps beneath: what comes in, what it runs, what it runs on.

  struct MidiMapEdit {
    deckboy::midimap::Origin origin = deckboy::midimap::Origin::Computer;
    int row = -1;
  };

  std::vector<deckboy::midimap::Row> midiMapRowsOf(deckboy::midimap::Origin origin) {
    ensureMidiMapLoaded();
    return origin == deckboy::midimap::Origin::Computer ? midiMapComputerRows_ : showMidiMapRows();
  }

  void midiMapStore(deckboy::midimap::Origin origin, std::vector<deckboy::midimap::Row> rows) {
    if (origin == deckboy::midimap::Origin::Computer) {
      midiMapComputerRows_ = std::move(rows);
      if (!saveComputerMidiMap()) {
        triggerToast("MIDI map: could not save " + midiMapComputerPath().string(),
                     ToastKind::Warning, kToastReadableMs);
      }
    } else {
      pushUndoSnapshot();
      project_.midiMapRows.clear();
      for (const auto& r : rows) project_.midiMapRows.push_back(deckboy::midimap::rowToLine(r));
      markProjectDirty();
    }
    refreshMidiMapEngine();
  }

  // Change the row being edited, and keep it selected.
  void midiMapEditRow(const std::function<void(deckboy::midimap::Row&)>& change) {
    auto rows = midiMapRowsOf(midiMapEdit_.origin);
    if (midiMapEdit_.row < 0 || midiMapEdit_.row >= static_cast<int>(rows.size())) return;
    change(rows[static_cast<std::size_t>(midiMapEdit_.row)]);
    midiMapStore(midiMapEdit_.origin, std::move(rows));
  }

  std::optional<deckboy::midimap::Row> midiMapEditedRow() {
    auto rows = midiMapRowsOf(midiMapEdit_.origin);
    if (midiMapEdit_.row < 0 || midiMapEdit_.row >= static_cast<int>(rows.size())) return std::nullopt;
    return rows[static_cast<std::size_t>(midiMapEdit_.row)];
  }

  // A row from a learned message: what came in, read the way that kind of
  // message is usually meant. Its command is asked for straight away.
  static deckboy::midimap::Row midiMapRowFromMessage(const deckboy::midimap::Message& m) {
    namespace mm = deckboy::midimap;
    mm::Row r;
    r.source = m.source;
    if (m.source == mm::Source::Osc) {
      r.oscAddress = m.oscAddress;
      r.read = m.hasOscValue ? mm::Read::Knob : mm::Read::Press;
    } else {
      r.port = m.port;
      r.channel = m.channel;
      if (m.source != mm::Source::PitchBend && m.source != mm::Source::Pressure) r.number = m.number;
      r.read = (m.source == mm::Source::Cc || mm::continuousSource(m.source)) ? mm::Read::Knob
                                                                              : mm::Read::Press;
    }
    if (mm::isContinuous(r.read) || mm::continuousSource(r.source)) {
      r.command = "MASTERVOL {value}";
      r.from = 0.0;
      r.to = 200.0;
      r.decimals = 0;
    } else {
      r.command = "GO";
    }
    return r;
  }

  // Ask for the command of the row being edited. A new row stays OFF until
  // it has one, so learning a knob never changes the show by itself.
  void midiMapAskCommand(bool turnOnAfter) {
    const auto row = midiMapEditedRow();
    if (!row) return;
    openInlineTextEditor("midimap.command", "What it runs",
                         "A command line, as a DASH button runs it. {value} is the knob's number, "
                         "{note} the key.",
                         row->command, [this, turnOnAfter](const std::string& value) {
                           const std::string cmd = trim(value);
                           if (cmd.empty()) return;
                           midiMapEditRow([&](deckboy::midimap::Row& r) {
                             r.command = cmd;
                             if (turnOnAfter) r.enabled = true;
                           });
                         });
  }

  void midiMapAddRow(const deckboy::midimap::Row& row) {
    auto rows = midiMapRowsOf(deckboy::midimap::Origin::Computer);
    deckboy::midimap::Row r = row;
    r.enabled = false;
    rows.push_back(r);
    midiMapStore(deckboy::midimap::Origin::Computer, std::move(rows));
    midiMapEdit_.origin = deckboy::midimap::Origin::Computer;
    midiMapEdit_.row = static_cast<int>(midiMapComputerRows_.size()) - 1;
    midiMapScroll_ = std::max(0, static_cast<int>(midiMapListed_.size()));
    midiMapAskCommand(true);
  }

  // Called by serviceMidiMap the moment learn hears something.
  void midiMapLearnHeard(const deckboy::midimap::Message& m) {
    if (midiMapLearnInto_ == 1) {
      midiMapAddRow(midiMapRowFromMessage(m));
    } else if (midiMapLearnInto_ == 2) {
      const auto fresh = midiMapRowFromMessage(m);
      midiMapEditRow([&](deckboy::midimap::Row& r) {
        r.source = fresh.source;
        r.port = fresh.port;
        r.channel = fresh.channel;
        r.number = fresh.number;
        r.numberTo = -1;
        r.oscAddress = fresh.oscAddress;
        if (deckboy::midimap::isContinuous(r.read) != deckboy::midimap::isContinuous(fresh.read)) {
          r.read = fresh.read;
        }
      });
    }
    midiMapLearnInto_ = 0;
  }

  std::string midiMapNowText(deckboy::midimap::Origin origin, int index, const deckboy::midimap::Row& r) {
    namespace mm = deckboy::midimap;
    switch (r.read) {
      case mm::Read::Hold: return "hold " + mm::formatNumber(r.holdSeconds) + " s";
      case mm::Read::PressRelease: return "press + release";
      case mm::Read::Play: return "plays";
      case mm::Read::Press:
        if (!mm::continuousSource(r.source) && !(r.source == mm::Source::Osc)) return "on press";
        break;
      default: break;
    }
    const double p = midiMap_.position(origin, index);
    if (p < 0.0) return "-";
    if (!midiMap_.caught(origin, index)) return "pickup";
    return mm::formatValue(r, mm::mapPosition(r, p));
  }

  void drawMidiMapPage(const SDL_Rect& content) {
    namespace mm = deckboy::midimap;
    refreshMidiMapEngine();

    const SDL_Color ink = pal.fg;
    const SDL_Color soft = pal.fgSoft;
    const int pad = uiScaled(12);
    const int gap = uiScaled(6);
    const int lineH = std::max(uiScaled(16), textLineHeight(fontSmall_));
    const int rowH = std::max(uiScaled(24), textLineHeight(fontSmall_) + uiScaled(9));
    const int x = content.x + pad;
    const int w = content.w - pad * 2;
    int y = content.y + uiScaled(10);

    // Title, and the three things to do.
    const int btnW = uiScaled(110);
    SDL_Rect backBtn {x + w - btnW, y, btnW, rowH};
    SDL_Rect learnBtn {backBtn.x - gap - btnW, y, btnW, rowH};
    SDL_Rect addBtn {learnBtn.x - gap - btnW, y, btnW, rowH};
    SDL_Rect presetBtn {addBtn.x - gap - btnW - uiScaled(30), y, btnW + uiScaled(30), rowH};
    drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {x, y, presetBtn.x - x - gap, rowH},
                 "MIDI MAP", ink);
    drawUIValueControl(presetBtn, "READY-MADE...");
    settingsBtns_.push_back({presetBtn, kSettingsActionMidiMapPreset, "midimap_preset"});
    drawUIValueControl(addBtn, "+ ADD");
    settingsBtns_.push_back({addBtn, kSettingsActionMidiMapAdd, "midimap_add"});
    drawUIValueControl(learnBtn, midiMapLearnArmed_ && midiMapLearnInto_ == 1 ? "LISTENING..." : "LEARN");
    if (midiMapLearnArmed_ && midiMapLearnInto_ == 1) drawSettingsStateFill(learnBtn, true);
    settingsBtns_.push_back({learnBtn, kSettingsActionMidiMapLearn, "midimap_learn"});
    drawUIValueControl(backBtn, "BACK");
    settingsBtns_.push_back({backBtn, kSettingsActionMidiMapBack, "midimap_back"});
    y += rowH + gap;

    // Where the messages come from.
    std::string where;
    if (!midiEnabled_) {
      where = "MIDI input is off: turn it on in MIDI CONTROL. OSC rows still answer.";
    } else {
      where = (midiDeviceInUse_.empty() ? midiMonitorPort() : midiDeviceInUse_) + " \xC2\xB7 connected";
    }
    if (midiMapLearnArmed_) where = "LEARN: move a knob or press a key on the controller...";
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x, y, w, lineH}, where, soft);
    y += lineH + gap;

    // The table.
    const auto rowsShow = midiMapRowsOf(mm::Origin::Show);
    const auto rowsComputer = midiMapRowsOf(mm::Origin::Computer);
    midiMapListed_.clear();
    for (int i = 0; i < static_cast<int>(rowsShow.size()); ++i) midiMapListed_.push_back({mm::Origin::Show, i});
    for (int i = 0; i < static_cast<int>(rowsComputer.size()); ++i) {
      midiMapListed_.push_back({mm::Origin::Computer, i});
    }
    const bool editing = midiMapEditedRow().has_value();
    const int editorH = editing ? std::min(content.h / 2, rowH * 7 + lineH * 3 + gap * 8) : 0;
    const int tableBottom = content.y + content.h - uiScaled(10) - editorH - (editing ? gap * 2 : 0);
    const int dotW = uiScaled(14);
    const int colMsg = static_cast<int>(w * 0.26);
    const int colRuns = static_cast<int>(w * 0.30);
    const int colOn = static_cast<int>(w * 0.22);
    const int colNow = w - dotW - colMsg - colRuns - colOn;
    auto columns = [&](int rowY, int h) {
      const int a = x + dotW;
      return std::array<SDL_Rect, 4> {SDL_Rect {a, rowY, colMsg - gap, h},
                                      SDL_Rect {a + colMsg, rowY, colRuns - gap, h},
                                      SDL_Rect {a + colMsg + colRuns, rowY, colOn - gap, h},
                                      SDL_Rect {a + colMsg + colRuns + colOn, rowY, colNow, h}};
    };
    {
      const auto head = columns(y, lineH);
      drawTextSafe(controlRenderer_, fontSmall_, head[0], "Message", soft);
      drawTextSafe(controlRenderer_, fontSmall_, head[1], "Runs", soft);
      drawTextSafe(controlRenderer_, fontSmall_, head[2], "On", soft);
      drawTextSafe(controlRenderer_, fontSmall_, head[3], "Now", soft);
      y += lineH + uiScaled(2);
    }
    const int visible = std::max(1, (tableBottom - y - lineH - gap) / (rowH + uiScaled(2)));
    midiMapScroll_ = std::clamp(midiMapScroll_, 0,
                                std::max(0, static_cast<int>(midiMapListed_.size()) - visible));
    const Uint64 nowMs = SDL_GetTicks();
    for (int v = 0; v < visible && v < kMidiMapVisibleRows; ++v) {
      const int listIndex = midiMapScroll_ + v;
      if (listIndex >= static_cast<int>(midiMapListed_.size())) break;
      const auto [origin, index] = midiMapListed_[static_cast<std::size_t>(listIndex)];
      const auto& rows = origin == mm::Origin::Show ? rowsShow : rowsComputer;
      const mm::Row& r = rows[static_cast<std::size_t>(index)];
      const SDL_Rect rowRect {x, y, w, rowH};
      const bool selected = editing && midiMapEdit_.origin == origin && midiMapEdit_.row == index;
      Primitives::fillRect(controlRenderer_, rowRect, selected ? pal.mid : pal.tile);
      bool overlap = false;
      for (int o = 0; o < 2 && !overlap; ++o) {
        const auto& others = o == 0 ? rowsShow : rowsComputer;
        const mm::Origin oo = o == 0 ? mm::Origin::Show : mm::Origin::Computer;
        for (int j = 0; j < static_cast<int>(others.size()); ++j) {
          if (oo == origin && j == index) continue;
          if (mm::overlaps(r, others[static_cast<std::size_t>(j)])) {
            overlap = true;
            break;
          }
        }
      }
      std::string cueClaim;
      if (r.source == mm::Source::Note) {
        for (const Deck& deck : project_.decks) {
          for (const Cue& cue : deck.cues) {
            if (cue.triggerMidiNote >= 0 && mm::rowClaimsNote(r, cue.triggerMidiNote)) cueClaim = cue.name;
          }
        }
      }
      if (overlap || !cueClaim.empty()) Primitives::strokeRect(controlRenderer_, rowRect, pal.deleteBezel);
      // The dot: lit while this row's message is arriving.
      const auto hit = midiMapRowHitMs_.find({static_cast<int>(origin), index});
      const bool lit = hit != midiMapRowHitMs_.end() && nowMs - hit->second < 250;
      const int d = uiScaled(7);
      Primitives::fillRect(controlRenderer_,
                           SDL_Rect {x + (dotW - d) / 2, y + (rowH - d) / 2, d, d}, lit ? ink : pal.dark);
      const SDL_Color rowInk = !r.enabled ? soft : ink;
      const auto cells = columns(y, rowH);
      std::string msg = mm::describeSource(r);
      if (r.builtIn) msg += "  built in";
      if (origin == mm::Origin::Show) msg += "  (show)";
      std::string runs = r.command;
      if (!cueClaim.empty()) runs += "  - cue \"" + cueClaim + "\" claims it too";
      if (!r.enabled) runs = "OFF  " + runs;
      std::string on = midiMapTargetLabel(r);
      drawTextSafe(controlRenderer_, fontSmall_, cells[0], msg, rowInk);
      drawTextSafe(controlRenderer_, fontSmall_, cells[1], runs, rowInk);
      drawTextSafe(controlRenderer_, fontSmall_, cells[2], on, rowInk);
      drawTextSafe(controlRenderer_, fontSmall_, cells[3], midiMapNowText(origin, index, r), rowInk);
      settingsBtns_.push_back({rowRect, kSettingsActionMidiMapRowBase + v, "midimap_row"});
      y += rowH + uiScaled(2);
    }
    // Scrolling, when there are more rows than fit.
    if (static_cast<int>(midiMapListed_.size()) > visible) {
      SDL_Rect up {x, y, uiScaled(60), lineH + uiScaled(6)};
      SDL_Rect down {x + uiScaled(66), y, uiScaled(60), lineH + uiScaled(6)};
      drawUIValueControl(up, "UP");
      drawUIValueControl(down, "DOWN");
      settingsBtns_.push_back({up, kSettingsActionMidiMapScrollUp, "midimap_up"});
      settingsBtns_.push_back({down, kSettingsActionMidiMapScrollDown, "midimap_down"});
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {down.x + down.w + gap, y, w / 2, lineH + uiScaled(6)},
                   std::to_string(midiMapScroll_ + 1) + "-" +
                     std::to_string(std::min<int>(midiMapScroll_ + visible,
                                                  static_cast<int>(midiMapListed_.size()))) +
                     " of " + std::to_string(midiMapListed_.size()),
                   soft);
    } else if (midiMapListed_.empty()) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x, y, w, lineH},
                   "No rows. + ADD or LEARN makes one; MIDIMAP RESET brings the built-in rows back.", soft);
    }

    if (!editing) return;
    drawMidiMapEditor(SDL_Rect {content.x + pad, tableBottom + gap, w, editorH});
  }

  void drawMidiMapEditor(const SDL_Rect& area) {
    namespace mm = deckboy::midimap;
    const auto edited = midiMapEditedRow();
    if (!edited) return;
    const mm::Row& r = *edited;

    const SDL_Color soft = pal.fgSoft;
    const int gap = uiScaled(6);
    const int lineH = std::max(uiScaled(16), textLineHeight(fontSmall_));
    const int rowH = std::max(uiScaled(24), textLineHeight(fontSmall_) + uiScaled(9));
    const int colGap = uiScaled(10);
    const int colW = (area.w - colGap * 2) / 3;
    const SDL_Rect c1 {area.x, area.y, colW, area.h};
    const SDL_Rect c2 {area.x + colW + colGap, area.y, colW, area.h};
    const SDL_Rect c3 {area.x + (colW + colGap) * 2, area.y, area.w - (colW + colGap) * 2, area.h};
    drawSettingsCard(c1, "1 \xC2\xB7 WHAT COMES IN", "learn it, or say how to read it");
    drawSettingsCard(c2, "2 \xC2\xB7 WHAT IT RUNS", "any command, {value} is the number");
    drawSettingsCard(c3, "3 \xC2\xB7 WHAT IT RUNS ON", "aimed here; the desk never moves");
    const int headerH = settingsHeaderHeight(fontBase_) + lineH + gap;
    const int pad = uiScaled(8);
    auto body = [&](const SDL_Rect& c) { return std::make_pair(c.x + pad, c.w - pad * 2); };

    // 1 · what comes in
    {
      auto [bx, bw] = body(c1);
      int by = c1.y + headerH;
      const bool relearning = midiMapLearnArmed_ && midiMapLearnInto_ == 2;
      SDL_Rect msgBtn = settingsRow(bx, bw, by, rowH, "Message", gap);
      drawUIValueControl(msgBtn, relearning ? std::string("LISTENING...") : mm::describeSource(r));
      settingsBtns_.push_back({msgBtn, kSettingsActionMidiMapRelearn, "midimap_relearn"});
      if (r.source != mm::Source::Osc) {
        SDL_Rect chBtn = settingsRow(bx, bw, by, rowH, "Channel", gap);
        drawUIValueControl(chBtn, r.channel <= 0 ? std::string("any") : std::to_string(r.channel));
        settingsBtns_.push_back({chBtn, kSettingsActionMidiMapChannel, "midimap_channel"});
        SDL_Rect portBtn = settingsRow(bx, bw, by, rowH, "Port", gap);
        drawUIValueControl(portBtn, r.port.empty() ? std::string("any port") : r.port);
        settingsBtns_.push_back({portBtn, kSettingsActionMidiMapPort, "midimap_port"});
      }
      SDL_Rect readBtn = settingsRow(bx, bw, by, rowH, "Read as", gap);
      drawUIValueControl(readBtn, mm::readLabel(r.read));
      settingsBtns_.push_back({readBtn, kSettingsActionMidiMapRead, "midimap_read"});
      if (r.read == mm::Read::Encoder) {
        SDL_Rect encBtn = settingsRow(bx, bw, by, rowH, "Encoder", gap);
        drawUIValueControl(encBtn, mm::encoderToken(r.encoder));
        settingsBtns_.push_back({encBtn, kSettingsActionMidiMapEncoder, "midimap_encoder"});
      }
      if (r.read == mm::Read::Hold) {
        SDL_Rect holdBtn = settingsRow(bx, bw, by, rowH, "Hold for", gap);
        drawUIValueControl(holdBtn, mm::formatNumber(r.holdSeconds) + " s");
        settingsBtns_.push_back({holdBtn, kSettingsActionMidiMapHold, "midimap_hold"});
      }
    }

    // 2 · what it runs
    {
      auto [bx, bw] = body(c2);
      int by = c2.y + headerH;
      SDL_Rect cmdBtn = settingsRow(bx, bw, by, rowH, "Command", gap);
      drawUIValueControl(cmdBtn, r.command);
      settingsBtns_.push_back({cmdBtn, kSettingsActionMidiMapCommand, "midimap_command"});
      const bool value = mm::isContinuous(r.read) || mm::continuousSource(r.source) ||
                         (r.source == mm::Source::Osc && mm::isContinuous(r.read));
      if (value) {
        SDL_Rect fromBtn = settingsRow(bx, bw, by, rowH, "From", gap);
        drawUIValueControl(fromBtn, mm::formatNumber(r.from));
        settingsBtns_.push_back({fromBtn, kSettingsActionMidiMapFrom, "midimap_from"});
        SDL_Rect centreBtn = settingsRow(bx, bw, by, rowH, "Centre", gap);
        drawUIValueControl(centreBtn, r.hasCentre ? mm::formatNumber(r.centre) : std::string("none"));
        settingsBtns_.push_back({centreBtn, kSettingsActionMidiMapCentre, "midimap_centre"});
        SDL_Rect toBtn = settingsRow(bx, bw, by, rowH, "To", gap);
        drawUIValueControl(toBtn, mm::formatNumber(r.to));
        settingsBtns_.push_back({toBtn, kSettingsActionMidiMapTo, "midimap_to"});
        if (r.read != mm::Read::Encoder) {
          SDL_Rect pickBtn = settingsRow(bx, bw, by, rowH, "Pickup", gap);
          drawSettingsToggle(pickBtn, r.pickup, "ON", "OFF");
          settingsBtns_.push_back({pickBtn, kSettingsActionMidiMapPickup, "midimap_pickup"});
        }
      }
      if (r.read == mm::Read::PressRelease || r.read == mm::Read::Play) {
        SDL_Rect relBtn = settingsRow(bx, bw, by, rowH, "On release", gap);
        drawUIValueControl(relBtn, r.releaseCommand.empty() ? std::string("nothing") : r.releaseCommand);
        settingsBtns_.push_back({relBtn, kSettingsActionMidiMapRelease, "midimap_release"});
      }
      // The test line: the last value and what it became.
      const double p = midiMap_.position(midiMapEdit_.origin, midiMapEdit_.row);
      std::string test = "Test: move it on the controller";
      if (value && p >= 0.0) {
        const std::string v = mm::formatValue(r, mm::mapPosition(r, p));
        test = "last value " + std::to_string(mm::value7(p)) + " -> " +
               mm::replaceAll(r.command, "{value}", v);
      }
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {bx, by, bw, lineH}, test, soft);
    }

    // 3 · what it runs on
    {
      auto [bx, bw] = body(c3);
      int by = c3.y + headerH;
      SDL_Rect onBtn = settingsRow(bx, bw, by, rowH, "Runs on", gap);
      const char* onLabel = r.target == mm::Target::Selected ? "the selected cue"
                          : r.target == mm::Target::OnAir ? "what is on air"
                          : r.target == mm::Target::Named ? "one named cue" : "the desk";
      drawUIValueControl(onBtn, onLabel);
      settingsBtns_.push_back({onBtn, kSettingsActionMidiMapTarget, "midimap_target"});
      if (r.target != mm::Target::Desk) {
        SDL_Rect plBtn = settingsRow(bx, bw, by, rowH, "Playlist", gap);
        drawUIValueControl(plBtn, r.playlist > 0 ? std::to_string(r.playlist) : std::string("the focused one"));
        settingsBtns_.push_back({plBtn, kSettingsActionMidiMapPlaylist, "midimap_playlist"});
      }
      if (r.target == mm::Target::Named) {
        SDL_Rect cueBtn = settingsRow(bx, bw, by, rowH, "Cue", gap);
        drawUIValueControl(cueBtn, r.cue.empty() ? std::string("name or number...") : r.cue);
        settingsBtns_.push_back({cueBtn, kSettingsActionMidiMapCue, "midimap_cue"});
      }
      SDL_Rect mapBtn = settingsRow(bx, bw, by, rowH, "Saved with", gap);
      drawSettingsToggle(mapBtn, midiMapEdit_.origin == mm::Origin::Show, "THE SHOW", "THIS COMPUTER");
      settingsBtns_.push_back({mapBtn, kSettingsActionMidiMapOrigin, "midimap_origin"});
      SDL_Rect enBtn = settingsRow(bx, bw, by, rowH, "Row", gap);
      drawSettingsToggle(enBtn, r.enabled, "ON", "OFF");
      settingsBtns_.push_back({enBtn, kSettingsActionMidiMapEnabled, "midimap_enabled"});
      const int half = (bw - gap) / 2;
      SDL_Rect delBtn {bx, by, half, rowH};
      SDL_Rect doneBtn {bx + half + gap, by, bw - half - gap, rowH};
      drawUIValueControl(delBtn, "DELETE");
      Primitives::strokeRect(controlRenderer_, delBtn, pal.deleteBezel);
      settingsBtns_.push_back({delBtn, kSettingsActionMidiMapDelete, "midimap_delete"});
      drawUIValueControl(doneBtn, "DONE");
      settingsBtns_.push_back({doneBtn, kSettingsActionMidiMapDone, "midimap_done"});
    }
  }

  // Numbers typed into the editor.
  void midiMapAskNumber(const std::string& owner, const std::string& title, double current,
                        std::function<void(deckboy::midimap::Row&, double)> apply,
                        bool allowNone = false) {
    openInlineTextEditor(owner, title, allowNone ? "A number, or blank for none" : "A number",
                         deckboy::midimap::formatNumber(current),
                         [this, apply, allowNone](const std::string& text) {
                           const std::string t = trim(text);
                           if (t.empty() && allowNone) {
                             midiMapEditRow([&](deckboy::midimap::Row& r) { r.hasCentre = false; });
                             return;
                           }
                           char* end = nullptr;
                           const double v = std::strtod(t.c_str(), &end);
                           if (end == t.c_str()) {
                             triggerToast("not a number: " + t);
                             return;
                           }
                           midiMapEditRow([&](deckboy::midimap::Row& r) { apply(r, v); });
                         });
  }

  bool handleMidiMapSettingsClick(const SettingsButton& sb) {
    namespace mm = deckboy::midimap;
    const int a = sb.action;
    if (a == kSettingsActionMidiMapOpen) {
      settingsMidiMapOpen_ = true;
      midiMapEdit_ = MidiMapEdit {};
      return true;
    }
    if (a < kSettingsActionMidiMapBack || a >= kSettingsActionMidiMapRowBase + kMidiMapVisibleRows) {
      return false;
    }
    if (a >= kSettingsActionMidiMapRowBase) {
      const int listIndex = midiMapScroll_ + (a - kSettingsActionMidiMapRowBase);
      if (listIndex >= 0 && listIndex < static_cast<int>(midiMapListed_.size())) {
        midiMapEdit_.origin = midiMapListed_[static_cast<std::size_t>(listIndex)].first;
        midiMapEdit_.row = midiMapListed_[static_cast<std::size_t>(listIndex)].second;
      }
      return true;
    }
    switch (a) {
      case kSettingsActionMidiMapBack:
        settingsMidiMapOpen_ = false;
        midiMapLearnArmed_ = false;
        midiMapLearnInto_ = 0;
        return true;
      case kSettingsActionMidiMapAdd: {
        mm::Row r;
        r.source = mm::Source::Note;
        r.number = 36;
        r.command = "GO";
        midiMapAddRow(r);
        return true;
      }
      case kSettingsActionMidiMapLearn:
        if (midiMapLearnArmed_ && midiMapLearnInto_ == 1) {
          midiMapLearnArmed_ = false;
          midiMapLearnInto_ = 0;
        } else {
          midiMapLearnArmed_ = true;
          midiMapLearnInto_ = 1;
          midiMapLearned_.reset();
        }
        return true;
      case kSettingsActionMidiMapPreset: {
        std::vector<std::pair<std::string, std::string>> choices;
        for (const auto& map : mm::controllerMaps()) choices.push_back({map.id, map.label});
        openDropdown("midimap.preset", sb.rect, choices, std::string(), [this](const std::string& id) {
          std::string detail;
          addMidiMapPreset(id, detail);
          triggerToast(detail);
        });
        return true;
      }
      case kSettingsActionMidiMapScrollUp:
        midiMapScroll_ = std::max(0, midiMapScroll_ - 4);
        return true;
      case kSettingsActionMidiMapScrollDown:
        midiMapScroll_ += 4;
        return true;
      case kSettingsActionMidiMapDone:
        midiMapEdit_ = MidiMapEdit {};
        midiMapLearnArmed_ = false;
        midiMapLearnInto_ = 0;
        return true;
      default:
        break;
    }
    const auto row = midiMapEditedRow();
    if (!row) return true;
    switch (a) {
      case kSettingsActionMidiMapRelearn:
        midiMapLearnArmed_ = true;
        midiMapLearnInto_ = 2;
        midiMapLearned_.reset();
        return true;
      case kSettingsActionMidiMapChannel: {
        std::vector<std::pair<std::string, std::string>> choices {{"0", "any channel"}};
        for (int c = 1; c <= 16; ++c) choices.push_back({std::to_string(c), "channel " + std::to_string(c)});
        openDropdown("midimap.channel", sb.rect, choices, std::to_string(row->channel),
                     [this](const std::string& v) {
                       midiMapEditRow([&](mm::Row& r) { r.channel = std::atoi(v.c_str()); });
                     });
        return true;
      }
      case kSettingsActionMidiMapPort: {
        std::vector<std::pair<std::string, std::string>> choices {{"", "any port"}};
        const std::string current = midiDeviceInUse_.empty() ? midiDeviceName_() : midiDeviceInUse_;
        if (!current.empty()) choices.push_back({current, current});
        if (!row->port.empty() && row->port != current) choices.push_back({row->port, row->port});
        openDropdown("midimap.port", sb.rect, choices, row->port, [this](const std::string& v) {
          midiMapEditRow([&](mm::Row& r) { r.port = v; });
        });
        return true;
      }
      case kSettingsActionMidiMapRead: {
        std::vector<std::pair<std::string, std::string>> choices;
        for (mm::Read k : {mm::Read::Knob, mm::Read::Encoder, mm::Read::Fine, mm::Read::Press,
                           mm::Read::Hold, mm::Read::PressRelease, mm::Read::Play}) {
          choices.push_back({mm::readToken(k), mm::readLabel(k)});
        }
        openDropdown("midimap.read", sb.rect, choices, mm::readToken(row->read), [this](const std::string& v) {
          midiMapEditRow([&](mm::Row& r) {
            if (auto k = mm::readFromToken(v)) {
              r.read = *k;
              // Play is a keyboard: give it the synth's two commands.
              if (r.read == mm::Read::Play && r.releaseCommand.empty()) {
                r.command = "SYNTHNOTEON {note} {velocity}";
                r.releaseCommand = "SYNTHNOTEOFF {note}";
                if (r.number >= 0 && r.numberTo < 0) {
                  r.number = 0;
                  r.numberTo = 127;
                }
              }
            }
          });
        });
        return true;
      }
      case kSettingsActionMidiMapEncoder: {
        std::vector<std::pair<std::string, std::string>> choices {
          {"auto", "auto (reads the common two)"}, {"twos", "1 = right, 127 = left"},
          {"offset", "65 = right, 63 = left"}, {"signbit", "1 = right, 65 = left"}};
        openDropdown("midimap.encoder", sb.rect, choices, mm::encoderToken(row->encoder),
                     [this](const std::string& v) {
                       midiMapEditRow([&](mm::Row& r) {
                         if (auto e = mm::encoderFromToken(v)) r.encoder = *e;
                       });
                     });
        return true;
      }
      case kSettingsActionMidiMapHold:
        midiMapAskNumber("midimap.hold", "Hold for (seconds)", row->holdSeconds,
                         [](mm::Row& r, double v) { r.holdSeconds = std::clamp(v, 0.1, 10.0); });
        return true;
      case kSettingsActionMidiMapCommand:
        midiMapAskCommand(false);
        return true;
      case kSettingsActionMidiMapRelease:
        openInlineTextEditor("midimap.release", "On release", "A command line, or blank for nothing",
                             row->releaseCommand, [this](const std::string& value) {
                               midiMapEditRow([&](mm::Row& r) { r.releaseCommand = trim(value); });
                             });
        return true;
      case kSettingsActionMidiMapFrom:
        midiMapAskNumber("midimap.from", "From", row->from, [](mm::Row& r, double v) { r.from = v; });
        return true;
      case kSettingsActionMidiMapCentre:
        midiMapAskNumber("midimap.centre", "The knob's centre", row->hasCentre ? row->centre : (row->from + row->to) / 2,
                         [](mm::Row& r, double v) {
                           r.hasCentre = true;
                           r.centre = v;
                         },
                         true);
        return true;
      case kSettingsActionMidiMapTo:
        midiMapAskNumber("midimap.to", "To", row->to, [](mm::Row& r, double v) { r.to = v; });
        return true;
      case kSettingsActionMidiMapPickup:
        midiMapEditRow([](mm::Row& r) { r.pickup = !r.pickup; });
        return true;
      case kSettingsActionMidiMapTarget: {
        std::vector<std::pair<std::string, std::string>> choices {
          {"desk", "the desk"}, {"selected", "the selected cue"},
          {"onair", "what is on air in a playlist"}, {"cue", "one named cue"}};
        openDropdown("midimap.target", sb.rect, choices, mm::targetToken(row->target),
                     [this](const std::string& v) {
                       midiMapEditRow([&](mm::Row& r) {
                         if (auto t = mm::targetFromToken(v)) r.target = *t;
                       });
                     });
        return true;
      }
      case kSettingsActionMidiMapPlaylist: {
        std::vector<std::pair<std::string, std::string>> choices {{"0", "the focused one"}};
        for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
          choices.push_back({std::to_string(d + 1), std::to_string(d + 1) + "  " +
                                                      project_.decks[static_cast<std::size_t>(d)].name});
        }
        openDropdown("midimap.playlist", sb.rect, choices, std::to_string(row->playlist),
                     [this](const std::string& v) {
                       midiMapEditRow([&](mm::Row& r) { r.playlist = std::max(0, std::atoi(v.c_str())); });
                     });
        return true;
      }
      case kSettingsActionMidiMapCue:
        openInlineTextEditor("midimap.cue", "Which cue", "Its name, number or id", row->cue,
                             [this](const std::string& value) {
                               midiMapEditRow([&](mm::Row& r) { r.cue = trim(value); });
                             });
        return true;
      case kSettingsActionMidiMapOrigin: {
        // Move the row between this computer's map and the show's.
        auto from = midiMapRowsOf(midiMapEdit_.origin);
        const mm::Origin to = midiMapEdit_.origin == mm::Origin::Show ? mm::Origin::Computer : mm::Origin::Show;
        auto dest = midiMapRowsOf(to);
        mm::Row moved = from[static_cast<std::size_t>(midiMapEdit_.row)];
        from.erase(from.begin() + midiMapEdit_.row);
        dest.push_back(moved);
        const int newIndex = static_cast<int>(dest.size()) - 1;
        midiMapStore(midiMapEdit_.origin, std::move(from));
        midiMapStore(to, std::move(dest));
        midiMapEdit_.origin = to;
        midiMapEdit_.row = newIndex;
        return true;
      }
      case kSettingsActionMidiMapEnabled:
        midiMapEditRow([](mm::Row& r) { r.enabled = !r.enabled; });
        return true;
      case kSettingsActionMidiMapDelete: {
        auto rows = midiMapRowsOf(midiMapEdit_.origin);
        rows.erase(rows.begin() + midiMapEdit_.row);
        midiMapStore(midiMapEdit_.origin, std::move(rows));
        midiMapEdit_ = MidiMapEdit {};
        triggerToast("row deleted");
        return true;
      }
      default:
        break;
    }
    return true;
  }
