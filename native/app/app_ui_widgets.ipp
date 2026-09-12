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

    // ── RENAME ──────────────────────────────────────────────────────────────
    //
    // First, because it is the thing you most often want from a cue and the
    // one that was missing. A cue has carried a `name` since the beginning --
    // it is what the playlist row draws -- and nothing in the program could
    // change it. The file header of app_cue_mgmt.ipp has listed renameCue()
    // among its contents the whole time; there was no such function.
    //
    // Blank means "go back to the filename", which is where an imported cue's
    // name came from, so there is a way back from a rename you regret.
    contextItems_.push_back({
      "  rename...",
      {0, 0, 0, 0},
      [this, deckIdx, cueIdx]() {
        if (deckIdx < 0 || deckIdx >= static_cast<int>(project_.decks.size())) return;
        Deck& d = project_.decks[deckIdx];
        if (cueIdx < 0 || cueIdx >= static_cast<int>(d.cues.size())) return;
        const std::string current = d.cues[cueIdx].name;
        openInlineTextEditor("cue_rename", "Rename cue",
                             "Cue name (blank restores the file name)", current,
          [this, deckIdx, cueIdx](const std::string& value) {
            if (deckIdx >= static_cast<int>(project_.decks.size())) return;
            Deck& dd = project_.decks[deckIdx];
            if (cueIdx >= static_cast<int>(dd.cues.size())) return;
            Cue& c = dd.cues[cueIdx];
            const std::string trimmed = trim(value);
            if (trimmed.empty()) {
              const fs::path p = fs::path(c.path);
              c.name = p.has_stem() ? p.stem().string() : c.path;
            } else {
              c.name = trimmed;
            }
            markProjectDirty();
            triggerToast("renamed: " + c.name);
          });
      }
    });

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
  // ── A MENU IS OPAQUE, AND IT BELONGS TO THE THEME ───────────────────────
  //
  // This filled with a hardcoded dark green at alpha 245 -- so it ignored the
  // theme entirely (a dark green box on a light colourway), and at 96% opacity
  // whatever was behind it read straight through: with the menu open over the
  // timeline you could read "The timeline can be scrubbed" through the list of
  // source types.
  //
  // It now uses the chrome roles CLAUDE.md names for a structural panel --
  // pal.tile filled, pal.fg inked -- at full opacity, and the hovered row goes
  // BRIGHTER with dark ink like every other lit control in the program.
  void renderContextMenu() {
    if (!contextMenuOpen_) return;
    drawUIPanel(contextMenuRect_, pal.tile, pal.deep, pal.mid);
    const int swatchW = uiScaled(12);
    const int textX = uiScaled(18);
    for (const auto& item : contextItems_) {
      const bool hover = !inTouchMode() && pointInRect(mouseX_, mouseY_, item.rect);
      if (hover) {
        Primitives::fillRect(controlRenderer_, item.rect, pal.light);
      }
      // Colour swatch (small square on the left).
      if (item.swatch.a > 0) {
        SDL_Rect sw {item.rect.x, item.rect.y + uiScaled(5),
                     swatchW, item.rect.h - uiScaled(10)};
        Primitives::fillRect(controlRenderer_, sw, item.swatch);
      }
      // Into a rect, so a long source name ellipsizes inside the menu instead
      // of running out of its right edge.
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {item.rect.x + textX, item.rect.y,
                             std::max(uiScaled(40), item.rect.w - textX - uiScaled(6)),
                             item.rect.h},
                   item.label, hover ? pal.deep : pal.fg);
    }
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
    // Listing all three everywhere was theatre: a user picked "Camera" and
    // nothing happened. macOS window and camera capture are real now
    // (ScreenCaptureKit and AVFoundation), so they appear there; and
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
          choices.emplace_back("window", "Window");
          break;
        case deckboy::platform::CaptureBackendKind::Camera:
          choices.emplace_back("camera", "Camera");
          break;
        case deckboy::platform::CaptureBackendKind::AppTexture:
          choices.emplace_back("syphon", "Syphon / Spout");
          break;
      }
    }
    return choices;
  }

  std::string sourceCueLabelForType(std::string token) const {
    token = toLower(trim(token));
    if (token == "camera") {
      return "Camera";
    }
    if (token == "spout" || token == "syphon") {
      return "Syphon / Spout";
    }
    return "Window";
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
      {"window", "Window"},
      {"camera", "Camera"},
      {"syphon", "Syphon / Spout"},
    };
  }

  std::string pipSourceTypeLabel(std::string token) const {
    token = toLower(trim(token));
    if (token == "browser") {
      return "Browser URL";
    }
    if (token == "window") {
      return "Window";
    }
    if (token == "camera") {
      return "Camera";
    }
    if (token == "syphon" || token == "spout") {
      return "Syphon / Spout";
    }
    if (token == "legacy") {
      return "Legacy Cue Link";
    }
    return "Media File / Still";
  }

  // EVERY STYLE, FROM THE ENUM, in the order it is declared.
  //
  // This was a hand-written list of three, so the nine added after it would
  // have been reachable over the wire and invisible in the interface -- the
  // same way the LFO oscillators were. A list built from the enum cannot drift
  // from what the program can actually do.
  //
  // Pick from a list, set a length: that is the whole interaction, and it is
  // the one every presentation tool has settled on.
  std::vector<std::pair<std::string, std::string>> transitionStyleChoices() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (int i = 0; i < static_cast<int>(TransitionStyle::Count); ++i) {
      const auto style = static_cast<TransitionStyle>(i);
      out.push_back({transitionStyleToken(style), ::transitionStyleLabel(style)});
    }
    return out;
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

  // Installed ASIO drivers, with the SDL device as an explicit first choice
  // rather than an empty row -- "none" reads as broken, "system audio" reads
  // as a decision.
  // Input devices, with the system default as an explicit first entry.
  std::vector<std::pair<std::string, std::string>> audioInputDeviceDropdownChoices() const {
    std::vector<std::pair<std::string, std::string>> out;
    out.emplace_back("__off__", "Off");
    out.emplace_back("", "System default input");
    int count = 0;
    if (SDL_AudioDeviceID* ids = SDL_GetAudioRecordingDevices(&count)) {
      for (int i = 0; i < count; ++i) {
        if (const char* n = SDL_GetAudioDeviceName(ids[i])) {
          out.emplace_back(n, n);
        }
      }
      SDL_free(ids);
    }
    return out;
  }

  std::vector<std::pair<std::string, std::string>> asioDriverDropdownChoices() const {
    std::vector<std::pair<std::string, std::string>> out;
    out.emplace_back("", "System audio (SDL)");
    for (const auto& dev : deckboy::platform::audio::listAsioDevices()) {
      out.emplace_back(dev.name, dev.name);
    }
    return out;
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

  // The inspector's label for a cue's transition, including the two words that
  // are not styles at all: "deck" (inherit) and "mixed" (a multi-selection that
  // does not agree).
  //
  // Everything else defers to the one in core, so a style added to the enum is
  // named correctly here without this being touched. It used to answer
  // "crossfade" for anything it did not recognise, which would have quietly
  // mislabelled all nine of the new ones.
  std::string transitionStyleLabel(std::string token) const {
    token = toLower(trim(token));
    if (token == "mixed") return "mixed";
    if (token == "deck") return "deck";
    return ::transitionStyleLabel(parseTransitionStyleToken(token));
  }

  void setSelectedCueTransitionStyle(const std::string& rawStyle) {
    std::string style = toLower(trim(rawStyle));
    // ANY STYLE THE PARSER KNOWS. This accepted exactly three and silently
    // returned for anything else, so every style added after it would have
    // been offered in the menu and refused on the way in.
    if (style != "deck" &&
        transitionStyleToken(parseTransitionStyleToken(style)) != style) {
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
    inlineEditor_.caret = initialValue.size();
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

  // ---- A REAL TEXT FIELD -------------------------------------------------
  //
  // This used to be append-and-backspace-from-the-end: no caret, no arrow
  // keys, and no paste. Which is survivable for a number and hopeless for a
  // URL -- you could not put one in without retyping it, and could not fix a
  // typo in the middle without deleting everything after it.
  //
  // Now: a caret, Left/Right/Home/End, Ctrl+Left/Right by word, Delete and
  // Backspace either side of it, and clipboard through SDL. `freshEntry`
  // still means "the whole value is selected", which is what makes typing
  // over a pre-filled value work.

  // UTF-8: step to the previous/next character boundary, never into the
  // middle of a multi-byte sequence.
  static std::size_t utf8Prev(const std::string& s, std::size_t at) {
    if (at == 0) return 0;
    --at;
    while (at > 0 && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) {
      --at;
    }
    return at;
  }

  static std::size_t utf8Next(const std::string& s, std::size_t at) {
    if (at >= s.size()) return s.size();
    ++at;
    while (at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) {
      ++at;
    }
    return at;
  }

  static std::size_t wordLeft(const std::string& s, std::size_t at) {
    while (at > 0 && std::isspace(static_cast<unsigned char>(s[at - 1]))) --at;
    while (at > 0 && !std::isspace(static_cast<unsigned char>(s[at - 1]))) --at;
    return at;
  }

  static std::size_t wordRight(const std::string& s, std::size_t at) {
    while (at < s.size() && !std::isspace(static_cast<unsigned char>(s[at]))) ++at;
    while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) ++at;
    return at;
  }

  void inlineEditorInsert(const std::string& text) {
    if (text.empty()) {
      return;
    }
    if (inlineEditor_.freshEntry) {
      inlineEditor_.value.clear();
      inlineEditor_.caret = 0;
      inlineEditor_.freshEntry = false;
    }
    inlineEditor_.caret = std::min(inlineEditor_.caret, inlineEditor_.value.size());
    // Room left before the 180-byte cap, so a long paste truncates instead of
    // being dropped: half a URL you can finish beats nothing happening.
    const std::size_t room = inlineEditor_.value.size() >= 180
                           ? 0 : (180 - inlineEditor_.value.size());
    const std::string piece = text.substr(0, room);
    if (piece.empty()) {
      return;
    }
    inlineEditor_.value.insert(inlineEditor_.caret, piece);
    inlineEditor_.caret += piece.size();
  }

  bool handleInlineTextEditorKey(SDL_Keycode key, Uint16 mod) {
    if (!inlineEditor_.open) {
      return false;
    }
    const bool ctrl = deckboyShortcutHeld(mod);
    std::string& value = inlineEditor_.value;
    inlineEditor_.caret = std::min(inlineEditor_.caret, value.size());

    if (key == SDLK_ESCAPE) {
      closeInlineTextEditor(false);
      return true;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      closeInlineTextEditor(true);
      return true;
    }

    // ---- Clipboard -------------------------------------------------------
    if (ctrl && key == SDLK_V) {
      if (char* text = SDL_GetClipboardText()) {
        // One line only: a pasted newline would otherwise sit invisibly in a
        // URL and fail the load with nothing to see.
        std::string pasted(text);
        SDL_free(text);
        std::string flat;
        flat.reserve(pasted.size());
        for (char ch : pasted) {
          if (ch != '\n' && ch != '\r' && ch != '\t') {
            flat += ch;
          }
        }
        inlineEditorInsert(trim(flat));
      }
      return true;
    }
    if (ctrl && (key == SDLK_C || key == SDLK_X)) {
      SDL_SetClipboardText(value.c_str());
      if (key == SDLK_X) {
        value.clear();
        inlineEditor_.caret = 0;
        inlineEditor_.freshEntry = false;
      }
      return true;
    }
    if (ctrl && key == SDLK_A) {
      inlineEditor_.freshEntry = true;   // "all selected": the next key replaces it
      inlineEditor_.caret = value.size();
      return true;
    }

    // ---- Moving ----------------------------------------------------------
    if (key == SDLK_LEFT) {
      inlineEditor_.caret = ctrl ? wordLeft(value, inlineEditor_.caret)
                                 : utf8Prev(value, inlineEditor_.caret);
      inlineEditor_.freshEntry = false;
      return true;
    }
    if (key == SDLK_RIGHT) {
      inlineEditor_.caret = ctrl ? wordRight(value, inlineEditor_.caret)
                                 : utf8Next(value, inlineEditor_.caret);
      inlineEditor_.freshEntry = false;
      return true;
    }
    if (key == SDLK_HOME) {
      inlineEditor_.caret = 0;
      inlineEditor_.freshEntry = false;
      return true;
    }
    if (key == SDLK_END) {
      inlineEditor_.caret = value.size();
      inlineEditor_.freshEntry = false;
      return true;
    }

    // ---- Erasing ---------------------------------------------------------
    if (key == SDLK_BACKSPACE) {
      if (inlineEditor_.freshEntry) {   // the pre-filled value is selected
        value.clear();
        inlineEditor_.caret = 0;
        inlineEditor_.freshEntry = false;
      } else if (inlineEditor_.caret > 0) {
        const std::size_t from = ctrl ? wordLeft(value, inlineEditor_.caret)
                                      : utf8Prev(value, inlineEditor_.caret);
        value.erase(from, inlineEditor_.caret - from);
        inlineEditor_.caret = from;
      }
      return true;
    }
    if (key == SDLK_DELETE) {
      if (inlineEditor_.freshEntry) {
        value.clear();
        inlineEditor_.caret = 0;
        inlineEditor_.freshEntry = false;
      } else if (inlineEditor_.caret < value.size()) {
        const std::size_t to = ctrl ? wordRight(value, inlineEditor_.caret)
                                    : utf8Next(value, inlineEditor_.caret);
        value.erase(inlineEditor_.caret, to - inlineEditor_.caret);
      }
      return true;
    }
    return true;
  }

  void handleInlineTextEditorTextInput(const std::string& text) {
    if (!inlineEditor_.open || text.empty()) {
      return;
    }
    inlineEditorInsert(text);
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
    // ---- THE CARET, AND KEEPING IT IN VIEW -------------------------------
    //
    // This used to append an underscore to the END of the value and ellipsize
    // from the end, so on anything longer than the box -- a URL, always -- you
    // saw the beginning of the string and a cursor that was nowhere near where
    // you were typing. Now the text scrolls under a caret drawn at the real
    // insertion point, and the whole value is shown highlighted while it
    // counts as selected, so "type to replace" is visible rather than a
    // surprise.
    {
      TTF_Font* editFont = fontMono_ ? fontMono_ : fontSmall_;
      const std::string& text = inlineEditor_.value;
      const std::size_t caret = std::min(inlineEditor_.caret, text.size());
      const int viewW = inputRect.w - 12;

      auto widthOf = [&](const std::string& s) {
        int w = 0;
        if (editFont && !s.empty()) {
          TTF_GetStringSize(editFont, s.c_str(), 0, &w, nullptr);
        }
        return w;
      };
      const int caretPx = widthOf(text.substr(0, caret));
      const int fullPx = widthOf(text);
      // Scroll only as far as needed to keep the caret inside, and never past
      // the end of the string.
      int scroll = 0;
      if (caretPx > viewW - 8) {
        scroll = caretPx - (viewW - 8);
      }
      scroll = std::min(scroll, std::max(0, fullPx - viewW));

      // Same save/restore idiom as the VJ bar: ask whether a clip is enabled
      // first -- SDL_GetRenderClipRect answers "did the call work", not
      // "was there one".
      const bool hadClip = SDL_RenderClipEnabled(controlRenderer_);
      SDL_Rect prevClip {};
      if (hadClip) {
        SDL_GetRenderClipRect(controlRenderer_, &prevClip);
      }
      SDL_Rect clip {inputRect.x + 3, inputRect.y + 2, inputRect.w - 6, inputRect.h - 4};
      SDL_SetRenderClipRect(controlRenderer_, &clip);

      const int textX = inputRect.x + 6 - scroll;
      const int textY = inputRect.y + 8;
      if (inlineEditor_.freshEntry && !text.empty()) {
        // Selected: a filled band behind the text, dark ink on it.
        SDL_Rect sel {textX - 1, inputRect.y + 5, fullPx + 2, inputRect.h - 10};
        Primitives::fillRect(controlRenderer_, sel, pal.light);
        drawText(controlRenderer_, editFont, text, pal.deep, textX, textY);
      } else {
        drawText(controlRenderer_, editFont, text, pal.light, textX, textY);
        // A bar, not a trailing underscore: it has to be able to sit BETWEEN
        // two characters.
        if ((animationNow_ / 450) % 2 == 0) {
          Primitives::fillRect(controlRenderer_,
                               SDL_Rect{textX + caretPx, inputRect.y + 5,
                                        std::max(1, uiScaled(2)), inputRect.h - 10},
                               pal.light);
        }
      }

      if (hadClip) {
        SDL_SetRenderClipRect(controlRenderer_, &prevClip);
      } else {
        SDL_SetRenderClipRect(controlRenderer_, nullptr);
      }
    }

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

