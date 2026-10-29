// ============================================================================
// app_input_monitor.ipp -- the MIDI and OSC monitor (MENU > Network > MONITOR)
//
// Every message that arrives by MIDI, OSC, the remote port or the phone, and
// what Deckboy did with it: DID, NOTHING and why, or REFUSED and why. "My
// controller does nothing" is most of what controller support is, and until
// now the only answer was to guess: a note no cue claims, a CC with no
// mapping and an untrusted address all looked exactly like a broken cable.
// (Roadmap; the design board of 2026-10-28.)
//
// Part of class App -- included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  struct PendingRemoteCommand;   // the command queue's own, defined beside it in main.cpp

  struct InputMonitorEntry {
    std::uint64_t serial = 0;
    std::string clock;           // when it arrived, HH:MM:SS.mmm
    std::string kind;            // MIDI, OSC, REMOTE, PHONE
    std::string from;            // the port, or the address it came from
    std::string message;         // as the controller sent it
    std::string command;         // what it became, if it became one
    char outcome = 'D';          // D did, N nothing, R refused
    std::string what;            // and what, or why not
    int midiNote = -1;           // a note-on: what MAKE IT TAKE THE SELECTED CUE uses
    std::string oscAddress;      // an OSC message: likewise
  };

  static constexpr std::size_t kInputMonitorKept = 400;

  // When the message ARRIVED, read off a wall clock a person can match to
  // their controller -- not when the main thread got round to it.
  static std::string inputMonitorClock() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const long long millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now.time_since_epoch()).count() % 1000;
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char text[32];
    std::snprintf(text, sizeof(text), "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min,
                  local.tm_sec, static_cast<int>(millis));
    return text;
  }

  // An OSC message as a person would type it: the address, then its arguments.
  static std::string oscMonitorText(const OscMessage& osc) {
    std::string text = trim(osc.address);
    for (const auto& arg : osc.args) {
      text += ' ';
      if (const auto* i = std::get_if<std::int32_t>(&arg)) {
        text += std::to_string(*i);
      } else if (const auto* f = std::get_if<float>(&arg)) {
        std::ostringstream number;
        number << *f;
        text += number.str();
      } else if (const auto* s = std::get_if<std::string>(&arg)) {
        text += *s;
      } else if (const auto* b = std::get_if<bool>(&arg)) {
        text += *b ? "true" : "false";
      }
    }
    return text;
  }

  static std::string midiNoteName(int note) {
    static const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B"};
    note = std::clamp(note, 0, 127);
    return std::string(kNames[note % 12]) + std::to_string(note / 12 - 1);
  }

  // Any thread: the MIDI and network threads record what they drop themselves.
  void recordInputMonitor(InputMonitorEntry entry) {
    if (inputMonitorPaused_.load(std::memory_order_relaxed)) return;
    if (entry.clock.empty()) entry.clock = inputMonitorClock();
    std::lock_guard<std::mutex> lock(inputMonitorMutex_);
    entry.serial = ++inputMonitorSerial_;
    inputMonitor_.push_back(std::move(entry));
    while (inputMonitor_.size() > kInputMonitorKept) inputMonitor_.pop_front();
  }

  // The MIDI thread names its port from this copy, taken when the input
  // opened, and never reads the show's own field.
  void noteMidiMonitorPort() {
    std::lock_guard<std::mutex> lock(inputMonitorMutex_);
    midiMonitorPortName_ = midiDeviceInUse_;
  }

  // A queued command that remembers where it came from, so its row can say
  // what it did once the main thread has run it (recordInputMonitorOutcome).
  bool enqueueMonitoredCommand(std::string command, std::string kind, std::string from,
                               std::string message, SocketHandle replyTo = kInvalidSocket,
                               std::uint64_t replyGeneration = 0, bool fromNetworkPeer = false,
                               int midiNote = -1, std::string oscAddress = {},
                               std::shared_ptr<WebRemoteReply> webReply = {}) {
    PendingRemoteCommand pending;
    pending.text = std::move(command);
    pending.replyTo = replyTo;
    pending.replyGeneration = replyGeneration;
    pending.webReply = std::move(webReply);
    pending.fromNetworkPeer = fromNetworkPeer;
    pending.monitorKind = std::move(kind);
    pending.monitorFrom = std::move(from);
    pending.monitorMessage = std::move(message);
    pending.monitorClock = inputMonitorClock();
    pending.monitorMidiNote = midiNote;
    pending.monitorOscAddress = std::move(oscAddress);
    std::lock_guard<std::mutex> lock(remoteCommandMutex_);
    if (remoteCommands_.size() >= 512 || pending.text.size() > 65536) return false;
    remoteCommands_.push_back(std::move(pending));
    return true;
  }

  // Main thread, straight after the command ran. Pollers are left out: a
  // Companion page asks for STATUS several times a second, and a monitor full
  // of that shows nothing else.
  void recordInputMonitorOutcome(const PendingRemoteCommand& command, bool refusedBeforeRunning) {
    const auto words = splitWhitespace(command.text);
    const std::string verb = words.empty() ? std::string() : toUpper(words[0]);
    if (verb == "PING" || verb == "STATUS" || verb == "STATE" || verb == "MONITOR" ||
        verb == "HELP" || verb == "MIDIIN") {
      return;   // MIDIIN's own row is the MIDI one it makes
    }
    InputMonitorEntry entry;
    entry.clock = command.monitorClock;
    entry.kind = command.monitorKind;
    entry.from = command.monitorFrom;
    entry.message = command.monitorMessage.empty() ? command.text : command.monitorMessage;
    entry.command = command.text;
    entry.midiNote = command.monitorMidiNote;
    entry.oscAddress = command.monitorOscAddress;
    if (refusedBeforeRunning) {
      entry.outcome = 'R';
      entry.what = remoteCommandError_;
    } else if (!remoteCommandRecognized_) {
      entry.outcome = 'N';
      entry.what = "Deckboy has no command " + verb;
    } else if (!remoteCommandError_.empty()) {
      entry.outcome = 'N';
      entry.what = remoteCommandError_;
    } else if (verb == "OSCTRIGGER" && remoteCommandDetail_ == "0 cues") {
      entry.outcome = 'N';
      entry.what = "no cue has the address " + (words.size() > 1 ? words[1] : std::string());
    } else {
      entry.outcome = 'D';
      entry.what = remoteCommandDetail_.empty() ? command.text
                                                : command.text + ": " + remoteCommandDetail_;
    }
    recordInputMonitor(std::move(entry));
  }

  std::vector<InputMonitorEntry> inputMonitorSnapshot() {
    std::lock_guard<std::mutex> lock(inputMonitorMutex_);
    return std::vector<InputMonitorEntry>(inputMonitor_.begin(), inputMonitor_.end());
  }

  bool inputMonitorShows(const InputMonitorEntry& entry) const {
    switch (inputMonitorFilter_) {
      case 1: return entry.kind == "MIDI";
      case 2: return entry.kind == "OSC";
      case 3: return entry.kind == "REMOTE" || entry.kind == "PHONE";
      case 4: return entry.outcome != 'D';
      default: return true;
    }
  }

  std::string inputMonitorStatusLine() {
    std::string midi = "MIDI  off";
    if (midiEnabled_) {
      midi = "MIDI  " + (midiDeviceName_().empty() ? std::string("first port")
                                                   : midiDeviceName_()) + ", listening";
    }
    std::size_t clients = 0;
    {
      std::lock_guard<std::mutex> lock(companionClientsMutex_);
      clients = companionClients_.size();
    }
    return midi + "   -   OSC and REMOTE  port " + std::to_string(companionPort_) + ", " +
           std::to_string(clients) + (clients == 1 ? " controller" : " controllers") +
           " connected" + (inputMonitorPaused_.load() ? "   -   PAUSED" : "");
  }

  const InputMonitorEntry* findInputMonitorEntry(const std::vector<InputMonitorEntry>& entries,
                                                 std::uint64_t serial) const {
    for (const auto& entry : entries) {
      if (entry.serial == serial) return &entry;
    }
    return nullptr;
  }

  // COPY AS A COMMAND: what the message became, which is also what a script,
  // a DASH tile or Companion would send to do the same thing.
  void copySelectedInputAsCommand() {
    const auto entries = inputMonitorSnapshot();
    const InputMonitorEntry* entry = findInputMonitorEntry(entries, inputMonitorSelected_);
    if (!entry || entry->command.empty()) {
      triggerToast("monitor: pick a row first");
      return;
    }
    SDL_SetClipboardText(entry->command.c_str());
    triggerToast("copied: " + entry->command);
  }

  // MAKE IT DO SOMETHING: a note or an address that did nothing becomes the
  // selected cue's own trigger, the same field TRIGGER MIDI / TRIGGER OSC set.
  void makeSelectedInputTriggerCue() {
    const auto entries = inputMonitorSnapshot();
    const InputMonitorEntry* entry = findInputMonitorEntry(entries, inputMonitorSelected_);
    if (!entry) {
      triggerToast("monitor: pick a row first");
      return;
    }
    if (entry->midiNote < 0 && entry->oscAddress.empty()) {
      triggerToast("only a MIDI note or an OSC address can take a cue",
                   ToastKind::Warning, kToastReadableMs);
      return;
    }
    Cue* cue = selectedCueMutable();
    if (!cue) {
      triggerToast("select the cue it should take first", ToastKind::Warning, kToastReadableMs);
      return;
    }
    pushUndoSnapshot();
    if (entry->midiNote >= 0) {
      cue->triggerMidiNote = entry->midiNote;
      triggerToast("MIDI note " + std::to_string(entry->midiNote) + " (" +
                   midiNoteName(entry->midiNote) + ") now takes " + cue->name);
    } else {
      cue->triggerOscAddress = entry->oscAddress;
      triggerToast("OSC " + entry->oscAddress + " now takes " + cue->name);
    }
    markProjectDirty();
  }

  void clearInputMonitor() {
    std::lock_guard<std::mutex> lock(inputMonitorMutex_);
    inputMonitor_.clear();
    inputMonitorSelected_ = 0;
    inputMonitorScroll_ = 0;
  }

  // ── Drawing ─────────────────────────────────────────────────────────────
  void renderInputMonitor() {
    if (!inputMonitorOpen_) return;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(controlWindow_, &ww, &wh);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 180);
    SDL_Rect full {0, 0, ww, wh};
    SDL_RenderFillRect(controlRenderer_, &full);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    const int mw = std::min(uiScaled(1180), ww - uiScaled(40));
    const int mh = std::min(uiScaled(760), wh - uiScaled(40));
    const SDL_Rect modal {(ww - mw) / 2, (wh - mh) / 2, mw, mh};
    inputMonitorPanelRect_ = modal;
    Primitives::drawFramedPanel(controlRenderer_, modal, pal.shellInner, pal.deep, pal.shellOuter);
    const int pad = uiScaled(16);
    const int rowH = std::max(uiScaled(22), TTF_GetFontHeight(fontSmall_) + uiScaled(6));
    const int btnH = std::max(uiScaled(28), TTF_GetFontHeight(fontSmall_) + uiScaled(12));
    const int x0 = modal.x + pad;
    const int innerW = mw - pad * 2;
    int y = modal.y + uiScaled(10);

    const int hintW = uiScaled(160);
    drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {x0, y, innerW - hintW, uiScaled(26)},
                 "MIDI AND OSC MONITOR", pal.fg);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {modal.x + mw - pad - hintW, y + uiScaled(4), hintW, uiScaled(18)},
                 "Esc to close", pal.inkSoft);
    y += uiScaled(34);

    // Filters on the left, PAUSE and CLEAR on the right.
    auto button = [&](SDL_Rect rect, const std::string& label, bool lit) {
      drawUIPanel(rect, paletteToggleFill(lit), pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, rect, label, paletteToggleInk(lit));
    };
    static const char* kFilters[5] = {"ALL", "MIDI", "OSC", "REMOTE", "ONLY WHAT DID NOTHING"};
    int bx = x0;
    for (int i = 0; i < 5; ++i) {
      const int w = measuredTextWidth(fontSmall_, kFilters[i]) + uiScaled(24);
      inputMonitorFilterRects_[static_cast<std::size_t>(i)] = SDL_Rect {bx, y, w, btnH};
      button(inputMonitorFilterRects_[static_cast<std::size_t>(i)], kFilters[i],
             inputMonitorFilter_ == i);
      bx += w + uiScaled(6);
    }
    const bool paused = inputMonitorPaused_.load();
    const int clearW = measuredTextWidth(fontSmall_, "CLEAR") + uiScaled(24);
    const int pauseW = measuredTextWidth(fontSmall_, "RESUME") + uiScaled(24);
    inputMonitorClearRect_ = SDL_Rect {x0 + innerW - clearW, y, clearW, btnH};
    inputMonitorPauseRect_ = SDL_Rect {inputMonitorClearRect_.x - uiScaled(6) - pauseW, y, pauseW, btnH};
    button(inputMonitorPauseRect_, paused ? "RESUME" : "PAUSE", paused);
    button(inputMonitorClearRect_, "CLEAR", false);
    y += btnH + uiScaled(8);

    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x0, y, innerW, rowH},
                 inputMonitorStatusLine(), pal.inkSoft);
    y += rowH + uiScaled(4);

    // The table: TIME | FROM | MESSAGE | WHAT DECKBOY DID.
    const int timeW = measuredTextWidth(fontSmall_, "00:00:00.000") + uiScaled(16);
    const int fromW = innerW * 22 / 100;
    const int messageW = innerW * 26 / 100;
    const int whatX = x0 + timeW + fromW + messageW;
    const int whatW = x0 + innerW - whatX;
    auto cells = [&](int rowY, const std::string& t, const std::string& f, const std::string& m,
                     SDL_Color ink) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x0 + uiScaled(4), rowY, timeW - uiScaled(8), rowH}, t, ink);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x0 + timeW, rowY, fromW - uiScaled(8), rowH}, f, ink);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x0 + timeW + fromW, rowY, messageW - uiScaled(8), rowH}, m, ink);
    };
    cells(y, "TIME", "FROM", "MESSAGE", pal.inkSoft);
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {whatX, y, whatW, rowH}, "WHAT DECKBOY DID", pal.inkSoft);
    y += rowH;
    Primitives::fillRect(controlRenderer_, SDL_Rect {x0, y, innerW, std::max(1, uiScaled(2))}, pal.mid);
    y += uiScaled(4);

    const int footerH = btnH + uiScaled(16);
    const int tableBottom = modal.y + mh - footerH - uiScaled(8);
    const int visible = std::max(1, (tableBottom - y) / rowH);
    const auto entries = inputMonitorSnapshot();
    std::vector<const InputMonitorEntry*> shown;
    for (const auto& entry : entries) {
      if (inputMonitorShows(entry)) shown.push_back(&entry);
    }
    // Newest at the bottom, as a log reads; the wheel scrolls back.
    inputMonitorScroll_ = std::clamp(inputMonitorScroll_, 0,
                                     std::max(0, static_cast<int>(shown.size()) - visible));
    const int last = static_cast<int>(shown.size()) - inputMonitorScroll_;
    const int first = std::max(0, last - visible);
    inputMonitorRowRects_.clear();
    if (shown.empty()) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x0 + uiScaled(4), y, innerW, rowH},
                   entries.empty() && !paused
                     ? "Nothing has arrived yet. Press something on the controller."
                     : "Nothing here with this filter.",
                   pal.inkSoft);
    }
    for (int i = first; i < last; ++i) {
      const InputMonitorEntry& entry = *shown[static_cast<std::size_t>(i)];
      const SDL_Rect rowRect {x0, y, innerW, rowH};
      inputMonitorRowRects_.push_back({entry.serial, rowRect});
      const bool selected = entry.serial == inputMonitorSelected_;
      if (selected) {
        Primitives::fillRect(controlRenderer_, rowRect, pal.mid);
      } else if ((i % 2) == 1) {
        Primitives::fillRect(controlRenderer_, rowRect, pal.tile);
      }
      const SDL_Color ink = selected ? pal.deep : pal.fg;
      cells(y, entry.clock, entry.kind + "  " + entry.from, entry.message, ink);
      const char* tag = entry.outcome == 'D' ? "DID" : entry.outcome == 'R' ? "REFUSED" : "NOTHING";
      const SDL_Color tagFill = entry.outcome == 'D' ? pal.light
                              : entry.outcome == 'R' ? SDL_Color {217, 88, 79, 255} : pal.dark;
      const SDL_Color tagInk = entry.outcome == 'D' ? pal.deep
                             : entry.outcome == 'R' ? SDL_Color {26, 5, 4, 255} : pal.light;
      const int tagW = measuredTextWidth(fontSmall_, "REFUSED") + uiScaled(12);
      const SDL_Rect tagRect {whatX, y + uiScaled(2), tagW, rowH - uiScaled(4)};
      Primitives::fillRect(controlRenderer_, tagRect, tagFill);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, tagRect, tag, tagInk);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {whatX + tagW + uiScaled(8), y, whatW - tagW - uiScaled(8), rowH},
                   entry.what, ink);
      y += rowH;
    }

    // The selected row's two actions.
    const int fy = modal.y + mh - footerH;
    Primitives::fillRect(controlRenderer_, SDL_Rect {x0, fy, innerW, std::max(1, uiScaled(2))}, pal.mid);
    const int by = fy + uiScaled(8);
    const std::string selectedLabel = "SELECTED ROW";
    const int selW = measuredTextWidth(fontSmall_, selectedLabel) + uiScaled(12);
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {x0, by, selW, btnH}, selectedLabel, pal.inkSoft);
    const int copyW = measuredTextWidth(fontSmall_, "COPY AS A COMMAND") + uiScaled(24);
    const int makeW = measuredTextWidth(fontSmall_, "MAKE IT TAKE THE SELECTED CUE") + uiScaled(24);
    inputMonitorCopyRect_ = SDL_Rect {x0 + selW, by, copyW, btnH};
    inputMonitorMakeRect_ = SDL_Rect {inputMonitorCopyRect_.x + copyW + uiScaled(6), by, makeW, btnH};
    button(inputMonitorCopyRect_, "COPY AS A COMMAND", false);
    button(inputMonitorMakeRect_, "MAKE IT TAKE THE SELECTED CUE", true);
    const int hintX = inputMonitorMakeRect_.x + makeW + uiScaled(10);
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {hintX, by, x0 + innerW - hintX, btnH},
                 "a note or an address becomes that cue's trigger", pal.inkSoft);
  }

  // ── Input ──────────────────────────────────────────────────────────────
  void handleInputMonitorClick(int x, int y) {
    if (!pointInRect(x, y, inputMonitorPanelRect_)) {
      inputMonitorOpen_ = false;   // a click away puts it down, as the shortcuts sheet does
      return;
    }
    for (std::size_t i = 0; i < inputMonitorFilterRects_.size(); ++i) {
      if (pointInRect(x, y, inputMonitorFilterRects_[i])) {
        inputMonitorFilter_ = static_cast<int>(i);
        inputMonitorScroll_ = 0;
        return;
      }
    }
    if (pointInRect(x, y, inputMonitorPauseRect_)) {
      inputMonitorPaused_ = !inputMonitorPaused_.load();
      return;
    }
    if (pointInRect(x, y, inputMonitorClearRect_)) {
      clearInputMonitor();
      return;
    }
    if (pointInRect(x, y, inputMonitorCopyRect_)) {
      copySelectedInputAsCommand();
      return;
    }
    if (pointInRect(x, y, inputMonitorMakeRect_)) {
      makeSelectedInputTriggerCue();
      return;
    }
    for (const auto& [serial, rect] : inputMonitorRowRects_) {
      if (pointInRect(x, y, rect)) {
        inputMonitorSelected_ = serial;
        return;
      }
    }
  }

  void handleInputMonitorWheel(int wheelY) {
    inputMonitorScroll_ = std::max(0, inputMonitorScroll_ + wheelY * 3);
  }

  // MONITOR [n] -- the last n rows as JSON, for a script or a check;
  // MONITOR OPEN | CLOSE | CLEAR | PAUSE | RESUME.
  void handleInputMonitorCommand(const std::vector<std::string>& parts) {
    const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();
    if (sub == "OPEN" || sub == "CLOSE") {
      inputMonitorOpen_ = sub == "OPEN";
      remoteCommandDetail_ = inputMonitorOpen_ ? "open" : "closed";
      return;
    }
    if (sub == "CLEAR") {
      clearInputMonitor();
      remoteCommandDetail_ = "cleared";
      return;
    }
    if (sub == "PAUSE" || sub == "RESUME") {
      inputMonitorPaused_ = sub == "PAUSE";
      remoteCommandDetail_ = inputMonitorPaused_.load() ? "paused" : "listening";
      return;
    }
    int count = 20;
    if (!sub.empty()) {
      char* end = nullptr;
      const long parsed = std::strtol(parts[1].c_str(), &end, 10);
      if (!end || *end != '\0' || parsed < 1) {
        failRemoteCommand("MONITOR: a row count, OPEN, CLOSE, CLEAR, PAUSE or RESUME");
        return;
      }
      count = static_cast<int>(std::min<long>(parsed, static_cast<long>(kInputMonitorKept)));
    }
    const auto entries = inputMonitorSnapshot();
    const std::size_t from = entries.size() > static_cast<std::size_t>(count)
                               ? entries.size() - static_cast<std::size_t>(count) : 0;
    std::ostringstream json;
    json << '[';
    for (std::size_t i = from; i < entries.size(); ++i) {
      const auto& e = entries[i];
      json << (i == from ? "" : ",")
           << "{\"time\":\"" << escapeJson(e.clock) << "\",\"kind\":\"" << escapeJson(e.kind)
           << "\",\"from\":\"" << escapeJson(e.from) << "\",\"message\":\"" << escapeJson(e.message)
           << "\",\"outcome\":\""
           << (e.outcome == 'D' ? "DID" : e.outcome == 'R' ? "REFUSED" : "NOTHING")
           << "\",\"what\":\"" << escapeJson(e.what) << "\",\"command\":\"" << escapeJson(e.command)
           << "\"}";
    }
    json << ']';
    remoteCommandDetail_ = json.str();
  }
