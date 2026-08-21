// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
//
// ═══════════════════════════════════════════════════════════════════════════════
// app_ui_widgets.ipp — Reusable UI Widget Functions
// ═══════════════════════════════════════════════════════════════════════════════
//
// Shared widget infrastructure used across the control window. Every popup,
// dropdown, inline editor, and context menu lives here so that the render and
// input files can stay focused on layout and domain logic.
//
// ─── Context Menu ────────────────────────────────────────────────────────────
//   handleRightClick()        — entry point: right-click on trim handles
//                               (clears in/out points) or on cue rows (opens
//                               context menu via hit-test against clip rects).
//   openContextMenu()         — builds the context item list: color tag palette
//                               (8 entries with swatch colors) plus a "delete
//                               cue" action. Positions the menu to fit on
//                               screen and logs via uiWatchdogPopupEvent().
//   handleContextMenuClick()  — dispatches click to matching item action or
//                               dismisses if click is outside the menu rect.
//   renderContextMenu()       — draws the popup: filled panel, hover highlight,
//                               color swatches, and item labels.
//
// ─── UI Profiling ────────────────────────────────────────────────────────────
//   uiProfileLog()            — conditional stderr log gated by
//                               DECKBOY_UI_PROFILE env flag; timestamps with
//                               SDL_GetTicks.
//   uiWatchdogPopupEvent()    — specialized log for popup open/close events
//                               with optional item count.
//
// ─── Key-to-Character Mapping ────────────────────────────────────────────────
//   dropdownFilterCharFromKey() — converts SDL keycodes to printable chars for
//                                 the dropdown type-ahead filter. Supports
//                                 a-z, 0-9, and common punctuation; respects
//                                 Shift but blocks Ctrl/Alt/GUI modifiers.
//   inlineEditorCharFromKey()   — broader mapping for the inline text editor,
//                                 adding shifted number-row symbols (!@#$%^&*)
//                                 and extra punctuation (=, +, <, >, :, ;, etc.).
//
// ─── Choice List Builders ────────────────────────────────────────────────────
//   sourceCueTypeChoices()    — returns token/label pairs for source cue kinds
//                               (window, camera, syphon/spout).
//   sourceCueLabelForType()   — resolves a source type token to its display
//                               label string.
//   sourceCueKindFromToken()  — resolves a source type token to its CueKind
//                               enum value (with common aliases like "cam").
//   pipSourceTypeChoices()    — PiP overlay source kinds (media, browser,
//                               window, camera, syphon).
//   pipSourceTypeLabel()      — display label for a PiP source type token.
//   transitionStyleChoices()  — transition style options (cut, crossfade, dip).
//   transitionStyleLabel()    — display label for a transition style token.
//   setSelectedCueTransitionStyle() — applies a style to all focused-deck
//                               selected cues.
//   audioOutputDeviceDropdownChoices() — enumerates available audio output
//                               devices from the platform audio subsystem.
//   displayChoiceLabel()      — "Display N: <name>" label for a display index.
//   outputDisplayDropdownChoices() — enumerates SDL video displays.
//   outputMirrorSourceDropdownChoices() — lists other outputs as mirror sources.
//   outputStreamProtocolDropdownChoices() — SRT and RTMP options.
//
// ─── Dropdown Widget ─────────────────────────────────────────────────────────
//   openDropdown()            — initializes dropdown state: options list,
//                               type-ahead filter, anchor rect, highlight
//                               position, and selection callback. Measures text
//                               widths for popover sizing.
//   closeDropdown()           — tears down dropdown state, stops text input.
//   dropdownVisibleRowCount() — visible row count clamped to maxVisibleRows.
//   ensureDropdownHighlightVisible() — scrolls the dropdown list to keep the
//                               highlighted item in view.
//   rebuildDropdownFilteredIndices() — re-filters options against the current
//                               type-ahead filter string (case-insensitive).
//   refreshDropdownPopoverRect() — repositions/resizes the popover to fit in
//                               the window, flipping above the anchor if needed.
//   handleDropdownMouseDown() — click dispatch: select item, dismiss, or anchor
//                               toggle.
//   handleDropdownMouseWheel() — scrolls the filtered list.
//   handleDropdownKey()       — keyboard nav: arrows, enter to select, escape
//                               to dismiss, backspace to trim filter, and
//                               printable chars to extend filter.
//   renderDropdownPopover()   — draws the popover: framed panel, filter text,
//                               clipped scrollable item list with highlight.
//
// ─── Inline Text Editor ──────────────────────────────────────────────────────
//   openInlineTextEditor()    — opens a small text input panel (docked inside
//                               the cue inspector or floating center-screen).
//                               Flushes any stale SDL_EVENT_TEXT_INPUT events from the
//                               key that triggered the editor.
//   closeInlineTextEditor()   — tears down editor, optionally invoking the
//                               onSubmit callback with the entered value.
//   handleInlineTextEditorMouseDown() — click on Apply/Cancel buttons or
//                               outside the panel to dismiss.
//   handleInlineTextEditorKey() — keyboard: Escape = cancel, Enter = apply,
//                               Backspace/Delete = edit text.
//   handleInlineTextEditorTextInput() — appends SDL text input (clamped to 180
//                               chars).
//   renderInlineTextEditor()  — draws the editor panel with title, prompt,
//                               blinking cursor, Apply/Cancel buttons. When the
//                               owner starts with "cue." and the inspector is
//                               visible, docks the panel near the anchor rect
//                               inside the inspector viewport.
//
// ─── Settings Modal ──────────────────────────────────────────────────────────
//   settingsModalRect()       — computes the settings modal bounding rect,
//                               scaling to fit the window with tab-specific
//                               minimum/maximum dimensions (video tab is widest,
//                               network tab is tallest).
// ═══════════════════════════════════════════════════════════════════════════════

  void handleRightClick(int x, int y) {
    // Right-click on trim handles to clear them
    if (trimInHandleRect_.w > 0 && pointInRect(x, y, trimInHandleRect_)) {
      if (Cue* cue = activeCueMutable()) {
        cue->inPointSeconds = 0.0;
        triggerToast("in point cleared");
        markProjectDirty();
      }
      return;
    }
    if (trimOutHandleRect_.w > 0 && pointInRect(x, y, trimOutHandleRect_)) {
      if (Cue* cue = activeCueMutable()) {
        cue->outPointSeconds = 0.0;
        triggerToast("out point cleared");
        markProjectDirty();
      }
      return;
    }
    // Determine which cue was right-clicked
    for (int di = 0; di < static_cast<int>(deckListClipRects_.size()); ++di) {
      const Deck& deck = project_.decks[di];
      const SDL_Rect& primaryFrame = deckListClipRects_[di];
      SDL_Rect primaryClip {primaryFrame.x + 8, primaryFrame.y + 30, primaryFrame.w - 16, primaryFrame.h - 38};
      if (pointInRect(x, y, primaryClip)) {
        int listY = primaryClip.y - deckScrolls_[di];
        for (int ci : cueIndicesForOverlayRole(deck, false)) {
          SDL_Rect row {primaryClip.x, listY, primaryClip.w, kRowHeight};
          if (pointInRect(x, y, row)) {
            openContextMenu(di, ci, x, y);
            return;
          }
          listY += kRowHeight + 8;
        }
      }
      const SDL_Rect& overlayFrame = deckOverlayClipRects_[di];
      SDL_Rect overlayClip {overlayFrame.x + 8, overlayFrame.y + 42, overlayFrame.w - 16, overlayFrame.h - 50};
      if (!pointInRect(x, y, overlayClip)) continue;
      int overlayY = overlayClip.y - (di < static_cast<int>(deckOverlayScrolls_.size()) ? deckOverlayScrolls_[di] : 0);
      for (int ci : cueIndicesForOverlayRole(deck, true)) {
        SDL_Rect row {overlayClip.x, overlayY, overlayClip.w, kRowHeight};
        if (pointInRect(x, y, row)) {
          openContextMenu(di, ci, x, y);
          return;
        }
        overlayY += kRowHeight + 8;
      }
    }
    if (contextMenuOpen_) {
      contextMenuOpen_ = false;
      uiWatchdogPopupEvent("context_menu", false);
    }
  }

  void openContextMenu(int deckIdx, int cueIdx, int mx, int my) {
    contextMenuOpen_ = true;
    contextMenuDeckIdx_ = deckIdx;
    contextMenuCueIdx_ = cueIdx;
    contextItems_.clear();

    Deck& deck = project_.decks[deckIdx];
    Cue& cue = deck.cues[cueIdx];

    // Color tag items
    static const std::vector<std::pair<std::string, SDL_Color>> kTagOpts = {
      {"no color",  {48,  98,  48,  255}},
      {"red",       {180, 40,  40,  255}},
      {"orange",    {190, 100, 20,  255}},
      {"yellow",    {160, 145, 10,  255}},
      {"cyan",      {15,  140, 140, 255}},
      {"blue",      {20,  60,  175, 255}},
      {"purple",    {110, 30,  150, 255}},
      {"pink",      {175, 45,  115, 255}},
    };
    for (const auto& [label, col] : kTagOpts) {
      std::string tag = label == "no color" ? "" : label;
      bool isCurrent = cue.colorTag == tag;
      contextItems_.push_back({
        (isCurrent ? "* " : "  ") + label,
        col,
        [this, deckIdx, cueIdx, tag]() {
          project_.decks[deckIdx].cues[cueIdx].colorTag = tag;
          triggerToast("tag: " + (tag.empty() ? "none" : tag));
          markProjectDirty();
        }
      });
    }
    // File-backed cues get a "reveal in the OS file manager" entry — the
    // fastest answer to "which file is this cue actually playing?"
    {
      std::string mediaPath = resolvedCueFilesystemPathString(cue, currentProjectFile_);
      bool fileBacked = !mediaPath.empty() && !pathLooksLikeUri(cue.path) &&
                        (cue.kind == CueKind::Video || cue.kind == CueKind::Audio ||
                         cue.kind == CueKind::Image);
      if (fileBacked) {
        // The reveal itself is cross-platform (Explorer /select, Finder via
        // `open -R`, the containing directory on Linux) but the label and toast
        // said "explorer" everywhere, which is wrong on two of the three
        // platforms it runs on.
#if defined(_WIN32)
        static constexpr const char* kRevealLabel = "  show in explorer";
        static constexpr const char* kRevealOk = "opened in explorer";
        static constexpr const char* kRevealFail = "couldn't open explorer";
#elif defined(__APPLE__)
        static constexpr const char* kRevealLabel = "  show in finder";
        static constexpr const char* kRevealOk = "revealed in finder";
        static constexpr const char* kRevealFail = "couldn't open finder";
#else
        static constexpr const char* kRevealLabel = "  show in file manager";
        static constexpr const char* kRevealOk = "opened in file manager";
        static constexpr const char* kRevealFail = "couldn't open file manager";
#endif
        contextItems_.push_back({kRevealLabel, {0, 0, 0, 0}, [this, mediaPath]() {
          if (deckboy::platform::revealFileInFileManager(mediaPath)) {
            triggerToast(kRevealOk);
          } else {
            triggerToast(kRevealFail);
          }
        }});
      }
    }
    // Deleting the cue that is ON AIR is worth a warning, but the warning
    // belongs in the LABEL, not in a second click the menu cannot deliver.
    // Picking a named item out of a right-click menu is already deliberate, so
    // this deletes on the first click and says plainly what it is about to do.
    {
      const bool isLive = cueIdx == deck.activeIndex ||
        std::find(deck.overlayActiveIndices.begin(), deck.overlayActiveIndices.end(), cueIdx) !=
          deck.overlayActiveIndices.end();
      contextItems_.push_back({
        isLive ? "— delete LIVE cue" : "— delete cue",
        isLive ? SDL_Color{140, 30, 30, 255} : SDL_Color{80, 30, 30, 255},
        [this, deckIdx, cueIdx]() {
          requestDeleteCueIndices(deckIdx, {cueIdx}, /*alreadyConfirmed=*/true);
        }});
    }

    // Position menu so it fits on screen
    int winW = 0, winH = 0;
    SDL_GetWindowSize(controlWindow_, &winW, &winH);
    constexpr int kItemH = 32;
    constexpr int kMenuW = 212;
    int menuH = static_cast<int>(contextItems_.size()) * kItemH + 8;
    int mx2 = std::min(mx, winW - kMenuW - 4);
    int my2 = std::min(my, winH - menuH - 4);
    contextMenuRect_ = {mx2, my2, kMenuW, menuH};
    int iy = my2 + 4;
    for (auto& item : contextItems_) {
      item.rect = {mx2 + 4, iy, kMenuW - 8, kItemH - 2};
      iy += kItemH;
    }
    uiWatchdogPopupEvent("context_menu", true, static_cast<int>(contextItems_.size()));
  }

  void handleContextMenuClick(int x, int y) {
    if (!contextMenuOpen_) return;
    if (!pointInRect(x, y, contextMenuRect_)) {
      contextMenuOpen_ = false;
      uiWatchdogPopupEvent("context_menu", false);
      return;
    }
    for (auto& item : contextItems_) {
      if (pointInRect(x, y, item.rect)) {
        if (item.action) item.action();
        contextMenuOpen_ = false;
        uiWatchdogPopupEvent("context_menu", false);
        return;
      }
    }
    contextMenuOpen_ = false;
    uiWatchdogPopupEvent("context_menu", false);
  }

  void renderContextMenu() {
    if (!contextMenuOpen_) return;
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_Color bg {20, 50, 20, 245};
    Primitives::fillRect(controlRenderer_, contextMenuRect_, bg);
    Primitives::strokeRect(controlRenderer_, contextMenuRect_, pal.dark);
    for (const auto& item : contextItems_) {
      bool hover = !inTouchMode() && pointInRect(mouseX_, mouseY_, item.rect);
      if (hover) {
        SDL_Color hov {48, 90, 48, 200};
        Primitives::fillRect(controlRenderer_, item.rect, hov);
      }
      // Color swatch (small square on left)
      if (item.swatch.a > 0) {
        SDL_Rect sw {item.rect.x, item.rect.y + 5, 12, item.rect.h - 10};
        Primitives::fillRect(controlRenderer_, sw, item.swatch);
      }
      drawText(controlRenderer_, fontSmall_, item.label,
               hover ? pal.light : pal.mid,
               item.rect.x + 18, item.rect.y + 7);
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
  }

  void uiProfileLog(const std::string& message) const {
    if (!uiProfileEnabled_) {
      return;
    }
    std::cerr << "[DECKBOY_UI_PROFILE " << SDL_GetTicks() << "ms] " << message << '\n';
  }

  void uiWatchdogPopupEvent(const std::string& popupName, bool opening, int itemCount = -1) const {
    if (!uiProfileEnabled_) {
      return;
    }
    std::ostringstream line;
    line << (opening ? "open " : "close ") << popupName;
    if (itemCount >= 0) {
      line << " items=" << itemCount;
    }
    uiProfileLog(line.str());
  }

  static std::optional<char> dropdownFilterCharFromKey(SDL_Keycode key, Uint16 mod) {
    if ((mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0) {
      return std::nullopt;
    }
    bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    if (key >= SDLK_A && key <= SDLK_Z) {
      char base = static_cast<char>('a' + (key - SDLK_A));
      return shift ? static_cast<char>(std::toupper(static_cast<unsigned char>(base))) : base;
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
      return static_cast<char>('0' + (key - SDLK_0));
    }
    switch (key) {
      case SDLK_MINUS: return shift ? '_' : '-';
      case SDLK_UNDERSCORE: return '_';
      case SDLK_PERIOD: return '.';
      case SDLK_SPACE: return ' ';
      case SDLK_SLASH: return '/';
      default: break;
    }
    return std::nullopt;
  }

  static std::optional<char> inlineEditorCharFromKey(SDL_Keycode key, Uint16 mod) {
    if ((mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0) {
      return std::nullopt;
    }
    bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    if (key >= SDLK_A && key <= SDLK_Z) {
      char base = static_cast<char>('a' + (key - SDLK_A));
      return shift ? static_cast<char>(std::toupper(static_cast<unsigned char>(base))) : base;
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
      static const std::string shifted = ")!@#$%^&*(";
      int idx = static_cast<int>(key - SDLK_0);
      return shift ? shifted[idx] : static_cast<char>('0' + idx);
    }
    switch (key) {
      case SDLK_SPACE: return ' ';
      case SDLK_MINUS: return shift ? '_' : '-';
      case SDLK_UNDERSCORE: return '_';
      case SDLK_EQUALS: return shift ? '+' : '=';
      case SDLK_PLUS: return '+';
      case SDLK_PERIOD: return shift ? '>' : '.';
      case SDLK_COMMA: return shift ? '<' : ',';
      case SDLK_COLON: return ':';
      case SDLK_SEMICOLON: return shift ? ':' : ';';
      case SDLK_SLASH: return shift ? '?' : '/';
      case SDLK_BACKSLASH: return shift ? '|' : '\\';
      default: break;
    }
    return std::nullopt;
  }

  std::vector<std::pair<std::string, std::string>> sourceCueTypeChoices() const {
    // Only offer capture sources whose backend actually works on THIS platform.
    // Listing all three everywhere was theatre: on macOS every capture backend
    // is a scaffold (a user picked "Camera Source" and nothing happened), and
    // Syphon/Spout *capture* is a scaffold on Windows too (Spout OUTPUT works;
    // capturing a Spout sender as input does not). The self-check already knows
    // this per platform — drive the menu from the same catalog so the two can
    // never disagree.
    std::vector<std::pair<std::string, std::string>> choices;
    auto catalog = deckboy::platform::createCaptureBackendCatalog();
    for (const auto& info : catalog->list()) {
      if (!info.supported) {
        continue;
      }
      switch (info.kind) {
        case deckboy::platform::CaptureBackendKind::Window:
          choices.emplace_back("window", "Window Source");
          break;
        case deckboy::platform::CaptureBackendKind::Camera:
          choices.emplace_back("camera", "Camera Source");
          break;
        case deckboy::platform::CaptureBackendKind::AppTexture:
          choices.emplace_back("syphon", "Syphon/Spout Source");
          break;
      }
    }
    return choices;
  }

  std::string sourceCueLabelForType(std::string token) const {
    token = toLower(trim(token));
    if (token == "camera") {
      return "Camera Source";
    }
    if (token == "spout" || token == "syphon") {
      return "Syphon/Spout Source";
    }
    return "Window Source";
  }

  CueKind sourceCueKindFromToken(std::string token) const {
    token = toLower(trim(token));
    if (token == "camera" || token == "cam") {
      return CueKind::Camera;
    }
    if (token == "syphon" || token == "spout" || token == "siphon") {
      return CueKind::Syphon;
    }
    return CueKind::WindowSource;
  }

  std::vector<std::pair<std::string, std::string>> pipSourceTypeChoices() const {
    return {
      {"media", "Media File / Still"},
      {"browser", "Browser URL"},
      {"window", "Window Source"},
      {"camera", "Camera Source"},
      {"syphon", "Syphon/Spout Source"},
    };
  }

  std::string pipSourceTypeLabel(std::string token) const {
    token = toLower(trim(token));
    if (token == "browser") {
      return "Browser URL";
    }
    if (token == "window") {
      return "Window Source";
    }
    if (token == "camera") {
      return "Camera Source";
    }
    if (token == "syphon" || token == "spout") {
      return "Syphon/Spout Source";
    }
    if (token == "legacy") {
      return "Legacy Cue Link";
    }
    return "Media File / Still";
  }

  std::vector<std::pair<std::string, std::string>> transitionStyleChoices() const {
    return {
      {"cut", "cut"},
      {"crossfade", "crossfade"},
      {"dip", "dip black"},
    };
  }

  std::vector<std::pair<std::string, std::string>> audioOutputDeviceDropdownChoices() const {
    std::vector<std::pair<std::string, std::string>> choices;
    for (const auto& deviceName : outputAudioDeviceChoices()) {
      choices.push_back({deviceName, deviceName.empty() ? "(default audio)" : deviceName});
    }
    return choices;
  }

  std::string displayChoiceLabel(int displayIndex) const {
    int displayCount = deckboyGetNumVideoDisplays();
    if (displayCount <= 0 || displayIndex < 0 || displayIndex >= displayCount) {
      return "Display none";
    }
    std::string label = "Display " + std::to_string(displayIndex + 1);
    const char* displayName = deckboyGetDisplayName(displayIndex);
    if (displayName && *displayName) {
      label += ": ";
      label += displayName;
    }
    return label;
  }

  std::vector<std::pair<std::string, std::string>> outputDisplayDropdownChoices() const {
    std::vector<std::pair<std::string, std::string>> choices;
    int displayCount = deckboyGetNumVideoDisplays();
    for (int displayIndex = 0; displayIndex < displayCount; ++displayIndex) {
      choices.push_back({std::to_string(displayIndex), displayChoiceLabel(displayIndex)});
    }
    if (choices.empty()) {
      choices.push_back({"-1", "Display none"});
    }
    return choices;
  }

  std::vector<std::pair<std::string, std::string>> outputMirrorSourceDropdownChoices() const {
    std::vector<std::pair<std::string, std::string>> choices;
    choices.push_back({"-1", "Off (render own assignments)"});
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (outputIndex == project_.focusedOutputIndex) {
        continue;
      }
      choices.push_back({
        std::to_string(outputIndex),
        "Output " + std::to_string(outputIndex + 1) + "  " + outputLabel(outputIndex)
      });
    }
    return choices;
  }

  std::string outputMirrorSourceDropdownLabel(int sourceOutputIndex) const {
    if (sourceOutputIndex < 0 || sourceOutputIndex >= static_cast<int>(project_.outputs.size())) {
      return "Off";
    }
    return "Output " + std::to_string(sourceOutputIndex + 1) + "  " + outputLabel(sourceOutputIndex);
  }

  std::vector<std::pair<std::string, std::string>> outputStreamProtocolDropdownChoices() const {
    // RTMPS was fully implemented -- its own default URL, the FLV muxer, TLS --
    // but appeared in neither this list nor the cycle, so it was unreachable
    // from the UI. FILE is the program recorder.
    return {
      {"srt", "SRT"},
      {"rtmp", "RTMP"},
      {"rtmps", "RTMPS"},
      {"file", "RECORD TO FILE"},
    };
  }

  std::string transitionStyleLabel(std::string token) const {
    token = toLower(trim(token));
    if (token == "mixed") return "mixed";
    if (token == "deck") return "deck";
    if (token == "cut") return "cut";
    if (token == "dip" || token == "dipblack" || token == "dip_black") return "dip black";
    return "crossfade";
  }

  void setSelectedCueTransitionStyle(const std::string& rawStyle) {
    std::string style = toLower(trim(rawStyle));
    if (style != "cut" && style != "crossfade" && style != "dip") {
      return;
    }
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.cueTransitionStyle = style;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("cue style: " + style);
    markProjectDirty();
  }

  void closeDropdown(bool announceClose = true) {
    if (!dropdown_.open) {
      return;
    }
    if (announceClose) {
      uiWatchdogPopupEvent("dropdown:" + dropdown_.owner, false);
    }
    dropdown_ = DropdownState {};
    dropdownLastRenderedItemCount_ = -1;
    SDL_StopTextInput(controlWindow_);
  }

  int dropdownVisibleRowCount() const {
    if (!dropdown_.open) {
      return 0;
    }
    int count = static_cast<int>(dropdown_.filteredIndices.size());
    return std::max(1, std::min(dropdown_.maxVisibleRows, std::max(1, count)));
  }

  void ensureDropdownHighlightVisible() {
    int itemCount = static_cast<int>(dropdown_.filteredIndices.size());
    if (itemCount <= 0) {
      dropdown_.highlightedFilteredIndex = 0;
      dropdown_.scrollRow = 0;
      return;
    }
    dropdown_.highlightedFilteredIndex = std::clamp(dropdown_.highlightedFilteredIndex, 0, itemCount - 1);
    int visibleRows = dropdownVisibleRowCount();
    if (dropdown_.highlightedFilteredIndex < dropdown_.scrollRow) {
      dropdown_.scrollRow = dropdown_.highlightedFilteredIndex;
    } else if (dropdown_.highlightedFilteredIndex >= dropdown_.scrollRow + visibleRows) {
      dropdown_.scrollRow = dropdown_.highlightedFilteredIndex - visibleRows + 1;
    }
    int maxScroll = std::max(0, itemCount - visibleRows);
    dropdown_.scrollRow = std::clamp(dropdown_.scrollRow, 0, maxScroll);
  }

  void rebuildDropdownFilteredIndices() {
    dropdown_.filteredIndices.clear();
    std::string filterToken = toLower(trim(dropdown_.filter));
    for (int index = 0; index < static_cast<int>(dropdown_.options.size()); ++index) {
      const auto& item = dropdown_.options[index];
      if (filterToken.empty() || item.searchLabel.find(filterToken) != std::string::npos) {
        dropdown_.filteredIndices.push_back(index);
      }
    }
    if (dropdown_.filteredIndices.empty()) {
      dropdown_.highlightedFilteredIndex = 0;
      dropdown_.scrollRow = 0;
    }
    ensureDropdownHighlightVisible();
  }

  void refreshDropdownPopoverRect() {
    if (!dropdown_.open) {
      return;
    }
    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(controlWindow_, &winW, &winH);

    int widest = dropdown_.anchorRect.w;
    for (const auto& item : dropdown_.options) {
      widest = std::max(widest, item.textWidth + 28);
    }
    widest = std::clamp(widest, 160, std::max(220, winW - 24));

    int filterH = dropdown_.filter.empty() ? 0 : 18;
    int visibleRows = dropdownVisibleRowCount();
    int popH = 8 + filterH + visibleRows * dropdown_.rowHeight;
    int popX = std::clamp(dropdown_.anchorRect.x, 6, std::max(6, winW - widest - 6));
    int popY = dropdown_.anchorRect.y + dropdown_.anchorRect.h + 2;
    if (popY + popH > winH - 6) {
      popY = dropdown_.anchorRect.y - popH - 2;
      if (popY < 6) {
        popY = 6;
      }
    }
    dropdown_.popoverRect = {popX, popY, widest, popH};
  }

  void openDropdown(const std::string& owner,
                    const SDL_Rect& anchorRect,
                    const std::vector<std::pair<std::string, std::string>>& options,
                    const std::string& selectedId,
                    std::function<void(const std::string&)> onSelect) {
    if (dropdown_.open && dropdown_.owner == owner) {
      closeDropdown(true);
      return;
    }

    DropdownState next;
    next.open = true;
    next.owner = owner;
    next.anchorRect = anchorRect;
    next.options.reserve(options.size());
    for (const auto& [id, label] : options) {
      DropdownOptionItem item;
      item.id = id;
      item.label = label;
      item.searchLabel = toLower(label + " " + id);
      if (fontSmall_) {
        int textW = 0;
        TTF_GetStringSize(fontSmall_, label.c_str(), 0, &textW, nullptr);
        item.textWidth = textW;
      }
      next.options.push_back(std::move(item));
    }
    next.onSelect = std::move(onSelect);
    dropdown_ = std::move(next);
    dropdown_.highlightedFilteredIndex = 0;
    for (int i = 0; i < static_cast<int>(dropdown_.options.size()); ++i) {
      if (dropdown_.options[i].id == selectedId) {
        dropdown_.highlightedFilteredIndex = i;
        break;
      }
    }
    rebuildDropdownFilteredIndices();
    refreshDropdownPopoverRect();
    dropdownLastRenderedItemCount_ = -1;
    SDL_StartTextInput(controlWindow_);
    uiWatchdogPopupEvent("dropdown:" + owner, true, static_cast<int>(dropdown_.options.size()));
  }

  bool handleDropdownMouseDown(int x, int y) {
    if (!dropdown_.open) {
      return false;
    }
    if (pointInRect(x, y, dropdown_.anchorRect)) {
      closeDropdown(true);
      return true;
    }
    if (!pointInRect(x, y, dropdown_.popoverRect)) {
      closeDropdown(true);
      return true;
    }
    int filterH = dropdown_.filter.empty() ? 0 : 18;
    int listY = dropdown_.popoverRect.y + 4 + filterH;
    int relativeY = y - listY;
    if (relativeY < 0) {
      return true;
    }
    int row = relativeY / dropdown_.rowHeight;
    int filteredIndex = dropdown_.scrollRow + row;
    if (filteredIndex < 0 || filteredIndex >= static_cast<int>(dropdown_.filteredIndices.size())) {
      return true;
    }
    int optionIndex = dropdown_.filteredIndices[filteredIndex];
    std::string selectedId = dropdown_.options[optionIndex].id;
    auto onSelect = dropdown_.onSelect;
    closeDropdown(true);
    if (onSelect) {
      onSelect(selectedId);
    }
    return true;
  }

  bool handleDropdownMouseWheel(int wheelY) {
    if (!dropdown_.open || !pointInRect(mouseX_, mouseY_, dropdown_.popoverRect)) {
      return false;
    }
    int visibleRows = dropdownVisibleRowCount();
    int itemCount = static_cast<int>(dropdown_.filteredIndices.size());
    int maxScroll = std::max(0, itemCount - visibleRows);
    dropdown_.scrollRow = std::clamp(dropdown_.scrollRow - wheelY, 0, maxScroll);
    if (itemCount > 0) {
      dropdown_.highlightedFilteredIndex = std::clamp(dropdown_.highlightedFilteredIndex,
                                                      dropdown_.scrollRow,
                                                      std::min(maxScroll + visibleRows - 1, itemCount - 1));
    }
    return true;
  }

  bool handleDropdownKey(SDL_Keycode key, Uint16 mod) {
    if (!dropdown_.open) {
      return false;
    }
    int itemCount = static_cast<int>(dropdown_.filteredIndices.size());
    if (key == SDLK_ESCAPE) {
      closeDropdown(true);
      return true;
    }
    if (key == SDLK_UP && itemCount > 0) {
      dropdown_.highlightedFilteredIndex =
        std::max(0, dropdown_.highlightedFilteredIndex - 1);
      ensureDropdownHighlightVisible();
      return true;
    }
    if (key == SDLK_DOWN && itemCount > 0) {
      dropdown_.highlightedFilteredIndex =
        std::min(itemCount - 1, dropdown_.highlightedFilteredIndex + 1);
      ensureDropdownHighlightVisible();
      return true;
    }
    if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && itemCount > 0) {
      int optionIndex = dropdown_.filteredIndices[dropdown_.highlightedFilteredIndex];
      std::string selectedId = dropdown_.options[optionIndex].id;
      auto onSelect = dropdown_.onSelect;
      closeDropdown(true);
      if (onSelect) {
        onSelect(selectedId);
      }
      return true;
    }
    if (key == SDLK_BACKSPACE) {
      if (!dropdown_.filter.empty()) {
        dropdown_.filter.pop_back();
        rebuildDropdownFilteredIndices();
        refreshDropdownPopoverRect();
      }
      return true;
    }
    if (auto typed = dropdownFilterCharFromKey(key, mod); typed) {
      dropdown_.filter.push_back(*typed);
      rebuildDropdownFilteredIndices();
      refreshDropdownPopoverRect();
      return true;
    }
    return false;
  }

  void renderDropdownPopover() {
    if (!dropdown_.open) {
      return;
    }
    refreshDropdownPopoverRect();
    Primitives::drawFramedPanel(controlRenderer_, dropdown_.popoverRect,
                                pal.light,
                                pal.deep,
                                pal.mid);
    int filterH = dropdown_.filter.empty() ? 0 : 18;
    if (filterH > 0) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {dropdown_.popoverRect.x + 6, dropdown_.popoverRect.y + 3,
                             dropdown_.popoverRect.w - 12, filterH},
                   "filter: " + dropdown_.filter,
                   pal.dark);
    }

    SDL_Rect listRect {
      dropdown_.popoverRect.x + 3,
      dropdown_.popoverRect.y + 4 + filterH,
      dropdown_.popoverRect.w - 6,
      dropdown_.popoverRect.h - 7 - filterH
    };
    SDL_SetRenderClipRect(controlRenderer_, &listRect);
    int drawY = listRect.y;
    int visibleRows = dropdownVisibleRowCount();
    for (int row = 0; row < visibleRows; ++row) {
      int filteredIndex = dropdown_.scrollRow + row;
      if (filteredIndex >= static_cast<int>(dropdown_.filteredIndices.size())) {
        break;
      }
      int optionIndex = dropdown_.filteredIndices[filteredIndex];
      bool highlighted = filteredIndex == dropdown_.highlightedFilteredIndex;
      SDL_Rect rowRect {listRect.x, drawY, listRect.w, dropdown_.rowHeight};
      SDL_Color rowFill = highlighted ? pal.dark : pal.light;
      SDL_Color rowInk = highlighted ? pal.light : pal.deep;
      Primitives::fillRect(controlRenderer_, rowRect, rowFill);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {rowRect.x + 6, rowRect.y, rowRect.w - 12, rowRect.h},
                   dropdown_.options[optionIndex].label,
                   rowInk);
      drawY += dropdown_.rowHeight;
    }
    if (dropdown_.filteredIndices.empty()) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {listRect.x + 6, listRect.y, listRect.w - 12, 18},
                   "(no matches)",
                   pal.inkSoft);
    }
    SDL_SetRenderClipRect(controlRenderer_, nullptr);

    int renderedItems = static_cast<int>(dropdown_.filteredIndices.size());
    if (renderedItems != dropdownLastRenderedItemCount_) {
      dropdownLastRenderedItemCount_ = renderedItems;
      uiProfileLog("popup render " + dropdown_.owner
        + " items=" + std::to_string(renderedItems)
        + " visible=" + std::to_string(visibleRows));
    }
  }

  void openInlineTextEditor(const std::string& owner,
                            const std::string& title,
                            const std::string& prompt,
                            const std::string& initialValue,
                            std::function<void(const std::string&)> onSubmit) {
    closeDropdown(true);
    inlineEditor_ = InlineTextEditorState {};
    inlineEditor_.open = true;
    inlineEditor_.owner = owner;
    inlineEditor_.title = title;
    inlineEditor_.prompt = prompt;
    inlineEditor_.value = initialValue;
    inlineEditor_.freshEntry = true;  // old value acts selected: first keystroke replaces it
    inlineEditor_.anchorRect = lastInlineEditorAnchorRect_;
    inlineEditor_.onSubmit = std::move(onSubmit);
    lastInlineEditorAnchorRect_ = SDL_Rect {};
    if (controlWindow_) {
      SDL_ShowWindow(controlWindow_);
      SDL_RaiseWindow(controlWindow_);
    }
    SDL_StartTextInput(controlWindow_);
    // Discard any SDL_EVENT_TEXT_INPUT event that was generated by the keyboard
    // shortcut that opened this editor (e.g. "b" from the B-key shortcut).
    // The event is already in the queue at this point; flushing it here
    // prevents it from appearing as the first character in the field.
    SDL_FlushEvent(SDL_EVENT_TEXT_INPUT);
    uiWatchdogPopupEvent("inline_text:" + owner, true);
  }

  void closeInlineTextEditor(bool apply) {
    if (!inlineEditor_.open) {
      return;
    }
    auto owner = inlineEditor_.owner;
    auto submit = inlineEditor_.onSubmit;
    std::string value = inlineEditor_.value;
    inlineEditor_ = InlineTextEditorState {};
    SDL_StopTextInput(controlWindow_);
    uiWatchdogPopupEvent("inline_text:" + owner, false);
    if (apply && submit) {
      submit(value);
    }
  }

  bool handleInlineTextEditorMouseDown(int x, int y) {
    if (!inlineEditor_.open) {
      return false;
    }
    if (pointInRect(x, y, inlineEditor_.applyRect)) {
      closeInlineTextEditor(true);
      return true;
    }
    if (pointInRect(x, y, inlineEditor_.cancelRect)) {
      closeInlineTextEditor(false);
      return true;
    }
    if (!pointInRect(x, y, inlineEditor_.panelRect)) {
      closeInlineTextEditor(false);
      return true;
    }
    return true;
  }

  bool handleInlineTextEditorKey(SDL_Keycode key, Uint16 mod) {
    if (!inlineEditor_.open) {
      return false;
    }
    if (key == SDLK_ESCAPE) {
      closeInlineTextEditor(false);
      return true;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      closeInlineTextEditor(true);
      return true;
    }
    if (key == SDLK_BACKSPACE) {
      if (inlineEditor_.freshEntry) {   // treat the pre-filled value as selected: clear it
        inlineEditor_.value.clear();
        inlineEditor_.freshEntry = false;
      } else if (!inlineEditor_.value.empty()) {
        inlineEditor_.value.pop_back();
      }
      return true;
    }
    if (key == SDLK_DELETE) {
      inlineEditor_.value.clear();
      inlineEditor_.freshEntry = false;
      return true;
    }
    return true;
  }

  void handleInlineTextEditorTextInput(const std::string& text) {
    if (!inlineEditor_.open || text.empty()) {
      return;
    }
    if (inlineEditor_.freshEntry) {   // first character replaces the pre-filled value
      inlineEditor_.value.clear();
      inlineEditor_.freshEntry = false;
    }
    inlineEditor_.value += text;
    if (inlineEditor_.value.size() > 180) {
      inlineEditor_.value.resize(180);
    }
  }

  void renderInlineTextEditor() {
    if (!inlineEditor_.open) {
      return;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);
    bool cueDocked = inlineEditor_.owner.rfind("cue.", 0) == 0 && cueSettingsViewportRect_.w > 80;
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    if (cueDocked && cueSettingsViewportRect_.w > 0 && cueSettingsViewportRect_.h > 0) {
      SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 46);
      SDL_RenderFillRect(controlRenderer_, &cueSettingsViewportRect_);
      Primitives::strokeRect(controlRenderer_, cueSettingsViewportRect_, pal.mid);
    } else {
      SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 140);
      SDL_Rect shade {0, 0, width, height};
      SDL_RenderFillRect(controlRenderer_, &shade);
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    SDL_Rect panel {};
    if (cueDocked) {
      int dockW = std::max(280, cueSettingsViewportRect_.w - 14);
      dockW = std::min(dockW, cueSettingsViewportRect_.w - 8);
      int dockH = 118;
      int dockX = cueSettingsViewportRect_.x + (cueSettingsViewportRect_.w - dockW) / 2;
      if (inlineEditor_.anchorRect.w > 0) {
        int preferredX = inlineEditor_.anchorRect.x + inlineEditor_.anchorRect.w / 2 - dockW / 2;
        dockX = std::clamp(preferredX,
                           cueSettingsViewportRect_.x + 4,
                           cueSettingsViewportRect_.x + cueSettingsViewportRect_.w - dockW - 4);
      }
      int dockY = cueSettingsViewportRect_.y + 12;
      if (inlineEditor_.anchorRect.w > 0) {
        dockY = inlineEditor_.anchorRect.y + inlineEditor_.anchorRect.h + 6;
        if (dockY + dockH > cueSettingsViewportRect_.y + cueSettingsViewportRect_.h - 4) {
          dockY = inlineEditor_.anchorRect.y - dockH - 6;
        }
      }
      if (dockY + dockH > cueSettingsViewportRect_.y + cueSettingsViewportRect_.h) {
        dockY = std::max(cueSettingsViewportRect_.y + 4,
                         cueSettingsViewportRect_.y + cueSettingsViewportRect_.h - dockH - 4);
      }
      dockY = std::max(dockY, cueSettingsViewportRect_.y + 4);
      panel = SDL_Rect {dockX, dockY, dockW, dockH};
    } else {
      panel = SDL_Rect {width / 2 - 280, height / 2 - 82, 560, 164};
    }
    inlineEditor_.panelRect = panel;
    Primitives::drawFramedPanel(controlRenderer_, panel,
                                pal.light,
                                pal.deep,
                                pal.mid);
    int titleY = cueDocked ? panel.y + 8 : panel.y + 12;
    int promptY = cueDocked ? panel.y + 28 : panel.y + 44;
    int inputY = cueDocked ? panel.y + 48 : panel.y + 68;
    drawText(controlRenderer_, cueDocked ? fontSmall_ : fontBase_,
             ellipsizeToPixelWidth(cueDocked ? fontSmall_ : fontBase_, inlineEditor_.title, panel.w - 28),
             pal.deep, panel.x + 14, titleY);
    drawText(controlRenderer_, fontSmall_,
             ellipsizeToPixelWidth(fontSmall_, inlineEditor_.prompt, panel.w - 28),
             pal.dark, panel.x + 14, promptY);

    SDL_Rect inputRect {panel.x + 14, inputY, panel.w - 28, 34};
    inlineEditor_.inputRect = inputRect;
    Primitives::drawFramedPanel(controlRenderer_, inputRect,
                                pal.deep,
                                pal.dark,
                                pal.dark);
    std::string shown = inlineEditor_.value;
    if ((animationNow_ / 450) % 2 == 0) {
      shown += "_";
    }
    drawText(controlRenderer_, fontMono_,
             ellipsizeToPixelWidth(fontMono_, shown, inputRect.w - 12),
             pal.light, inputRect.x + 6, inputRect.y + 8);

    SDL_Rect applyRect {panel.x + panel.w - 136, panel.y + panel.h - 38, 58, 28};
    SDL_Rect cancelRect {panel.x + panel.w - 72, panel.y + panel.h - 38, 58, 28};
    inlineEditor_.applyRect = applyRect;
    inlineEditor_.cancelRect = cancelRect;
    Primitives::drawFramedPanel(controlRenderer_, applyRect,
                                pal.dark,
                                pal.deep,
                                pal.light);
    Primitives::drawFramedPanel(controlRenderer_, cancelRect,
                                pal.mid,
                                pal.deep,
                                pal.light);
    drawCenteredText(controlRenderer_, fontSmall_, "Apply",
                     pal.light, applyRect);
    drawCenteredText(controlRenderer_, fontSmall_, "Cancel",
                     pal.deep, cancelRect);
  }

  SDL_Rect settingsModalRect() const {
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);
    // One envelope for every tab. The modal used to pick per-tab min/max
    // sizes (video widest, network tallest), which made it jump around on
    // every tab switch; the union of those envelopes keeps the busiest tab
    // comfortable and the dialog rock-steady.
    // The envelope scales with the UI scale: at 2x every card, row and label
    // inside is twice the size, so a fixed 1320x940 cap would simply crop the
    // content. Still bounded by the window below, so a small screen wins.
    const int kMargin = uiScaled(10);
    const int kMinW = uiScaled(980);
    const int kMinH = uiScaled(700);
    const int kMaxW = uiScaled(1320);
    const int kMaxH = uiScaled(940);
    int modalW = std::clamp(width - kMargin * 2, std::min(kMinW, width), kMaxW);
    int modalH = std::clamp(height - kMargin * 2, std::min(kMinH, height), kMaxH);
    modalW = std::min(modalW, std::max(320, width - 12));
    modalH = std::min(modalH, std::max(260, height - 12));
    return SDL_Rect {(width - modalW) / 2, (height - modalH) / 2, modalW, modalH};
  }

