// ============================================================================
// app_tutorial.ipp — LEARN THE BASICS: the nine-step guided tour.
//
// Approved by James 2026-10-23 (design canvas "Deckboy tutorial", boards Main,
// Startup, StepOutput, StepTake, Finish, Teacher). Taught by a double act:
// Dr. Pixel and Professor Video take a line each per step.
//
// Four rules, all from the approved design:
//
//   - IT RUNS IN A PRACTICE SHOW. The operator's own show is saved first and
//     reopened when the tour ends; the practice show is its own file in the
//     state folder, so nothing the tour does can touch the real one.
//   - IT NEVER MOVES ON BY ITSELF. Each step watches for the REAL action (an
//     output that is on, a cue in the playlist, a take, a crossfade that ran,
//     a save) and only then advances. No timers.
//   - SHOW ME does the step for you; SKIP passes it; EXIT (or Esc) leaves.
//   - IT RESUMES. The step is written to the state folder, so the next LEARN
//     THE BASICS picks up where the last one stopped.
//
// The pointer is a dimmed window with a hole cut round the one control the
// step is about. The dimming is drawn, not modal: every control still works,
// because the tour is waiting for the operator to use them.
//
// Part of class App — included inside the class body in main.cpp.
// ============================================================================

  struct TutorialState {
    bool active = false;
    int step = 0;
    fs::path returnTo;          // the operator's show, reopened at the end
    // The desk as the operator left it, put back when the tour hands it back:
    // which outputs were on, and whether BLACK was.
    std::vector<bool> outputsWereOn;
    double dimmerWas = 1.0;
    std::size_t cueBaseline = 0;
    int saveBaseline = 0;
    bool tabNudged = false;     // MENU opened on the outputs tab once
    int inspectorNudges = 0;    // frames spent scrolling the transition row into view
    int missClicks = 0;         // clicks that hit nothing the step wanted
    Uint64 praiseUntilMs = 0;
    int praiseIndex = 0;
    SDL_Rect cardRect {};
    SDL_Rect showMeRect {};
    SDL_Rect skipRect {};
    SDL_Rect exitRect {};
    SDL_Rect targetRect {};
  };
  TutorialState tutorial_;
  // Counted by saveProjectNow on every save that landed; the save step
  // watches it rather than the dirty flag, which autosave also clears.
  int projectSaveCount_ = 0;

  static constexpr int kTutorialSteps = 9;

  struct TutorialLine {
    const char* title;
    const char* key;      // the key chip beside the pointer, or nullptr
    const char* pixel;    // Dr. Pixel's line
    const char* video;    // Professor Video's line
  };

  static const TutorialLine& tutorialLine(int step) {
    static const TutorialLine kLines[kTutorialSteps] = {
      {"HELLO", nullptr,
       "Dobar den, skapi. I am Dr. Pixel. Five minutes, and you will run a show. Less, if you listen to me.",
       "And I'm Professor Video. Synths first, then video. Mostly video now. Press START and we'll patch... we'll begin."},
      {"TURN THE OUTPUT ON", nullptr,
       "Skapi, the screen is empty. Don't keep them waiting. Open MENU and make OUTPUT OFF say OUTPUT ON. Aide!",
       "Lesson one: nothing comes out until you patch the oscillator to the... the OUTPUT. It's video. I know."},
      {"BRING IN A CLIP", "I",
       "Now a picture. Press I and pick a clip from your computer. No clip? SHOW ME, and I lend you one of mine.",
       "Every clip becomes a cue in the playlist. Like loading a sample. A sample with pictures in it."},
      {"TAKE IT LIVE", "ENTER",
       "Your clip waits in the playlist. Select it, then TAKE. Or Enter. Confident operators press Enter.",
       "TAKE is the envelope trigger. The, ah, the GO button. Same thing, it just has pictures in it."},
      {"LINE UP THE NEXT ONE", nullptr,
       "Click another cue. It waits, ready, until you say. Only one cue? Import another, or SHOW ME.",
       "Selecting is not playing. The selected one is the next note in the sequence. The next CUE."},
      {"CROSSFADE TO IT", "ENTER",
       "Cuts are for amateurs. In the inspector, press + on transition until it reads 1.00s. Then TAKE.",
       "A crossfade! Portamento, but for pictures. One second is plenty. Then TAKE."},
      {"CLEAR THE SCREEN", "B",
       "Something goes wrong on stage? Press BLACK, or B. The picture goes. The show keeps running underneath.",
       "Nothing is lost. Press B again and it comes back. Like the mute on a mixer. Very calming."},
      {"SAVE THE SHOW", "CTRL+S",
       "Now save. Ctrl+S, or SAVE. Some programs lose your work and then blame you. I would never.",
       "Save your patch. Your SHOW. Show. This practice one lives in Deckboy's own folder; yours is safe."},
      {"THAT'S A SHOW", nullptr,
       "That's a show! Output, cue, TAKE, crossfade, BLACK, save. Nazdrave, skapi. Rakia is on me.",
       "Class dismissed. The example shows teach layers, titles and effects. MENU > LEARN THE BASICS runs this again."},
    };
    return kLines[std::clamp(step, 0, kTutorialSteps - 1)];
  }

  static const char* tutorialPraise(int index, bool& fromPixel) {
    static const char* kPraise[] = {
      "*shakes head* That means yes. Bravo.",
      "Wonderful! Lovely signal flow.",
      "Mm. I knew you had it in you.",
      "Yes! Have a gold star. It's a dot.",
    };
    fromPixel = (index % 2) == 0;
    return kPraise[index % 4];
  }

  static const char* tutorialRoast(int step, bool& fromPixel) {
    fromPixel = (step % 2) == 1;
    return fromPixel
      ? "Ready for the next step? *nods* ...That means no, skapi. The glowing one. It is glowing for you."
      : "You've clicked everything but the glowing one. Even my oscillators find the glowing one.";
  }

  fs::path tutorialPracticeFile() const { return Paths::stateDir() / "Tutorial practice.deckboy"; }
  fs::path tutorialProgressFile() const { return Paths::stateDir() / "tutorial-progress.txt"; }

  int readTutorialProgress() const {
    std::ifstream in(tutorialProgressFile());
    int step = 0;
    if (in >> step) return std::clamp(step, 0, kTutorialSteps - 1);
    return 0;
  }

  void writeTutorialProgress(int step) const {
    std::error_code ec;
    fs::create_directories(Paths::stateDir(), ec);
    std::ofstream out(tutorialProgressFile(), std::ios::trunc);
    if (out) out << step << '\n';
  }

  void beginTutorial() {
    if (tutorial_.active) return;
    showStartupDialog_ = false;
    showSplashOverlay_ = false;
    settingsOpen_ = false;
    // THE OPERATOR'S SHOW FIRST. Whatever is open is written to its own file
    // before the practice show replaces it, and reopened at the end.
    const fs::path practice = tutorialPracticeFile();
    std::error_code ec;
    const bool alreadyPractice = !currentProjectFile_.empty() &&
                                 fs::equivalent(currentProjectFile_, practice, ec) && !ec;
    tutorial_ = TutorialState {};
    if (!alreadyPractice) {
      if (projectDirty_ && project_.loadedCleanly && !soakMode_) {
        saveProjectNow(false);
      }
      tutorial_.returnTo = currentProjectFile_;
      for (const auto& output : project_.outputs) tutorial_.outputsWereOn.push_back(output.enabled);
      tutorial_.dimmerWas = masterDimmerTarget_;
    }
    int step = readTutorialProgress();
    if (step > 0 && fs::exists(practice)) {
      if (!alreadyPractice) openProjectFromPath(practice);
    } else {
      step = 0;
      startNewShow(false);
      currentProjectFile_ = practice;
      saveProjectNow(false);
    }
    tutorial_.active = true;
    enterTutorialStep(step);
    triggerToast("learn the basics: Esc leaves, and it picks up here next time", ToastKind::Help);
  }

  void enterTutorialStep(int step) {
    tutorial_.step = std::clamp(step, 0, kTutorialSteps - 1);
    tutorial_.cueBaseline = focusedDeck().cues.size();
    tutorial_.saveBaseline = projectSaveCount_;
    tutorial_.tabNudged = false;
    tutorial_.inspectorNudges = 0;
    tutorial_.missClicks = 0;
    writeTutorialProgress(tutorial_.step);
  }

  void advanceTutorial() {
    tutorial_.praiseUntilMs = SDL_GetTicks() + 3500;
    ++tutorial_.praiseIndex;
    playUiSound(UiSoundEffect::Toggle);
    if (tutorial_.step + 1 < kTutorialSteps) {
      enterTutorialStep(tutorial_.step + 1);
    }
  }

  // finished: the tour reached its end, so the next one starts from the top.
  // openExamples: leave for the example shows instead of the operator's show.
  void endTutorial(bool finished, bool openExamples = false) {
    if (!tutorial_.active) return;
    tutorial_.active = false;
    writeTutorialProgress(finished ? 0 : tutorial_.step);
    if (finished) {
      std::error_code ec;
      fs::remove(tutorialProgressFile(), ec);
    }
    // The practice show is saved as it stands, so a resumed tour finds it.
    if (projectDirty_ && project_.loadedCleanly && !soakMode_) saveProjectNow(false);
    if (openExamples) {
      masterDimmerTarget_ = tutorial_.dimmerWas;   // the examples start lit
      openExampleShows();
      return;
    }
    std::error_code ec;
    if (!tutorial_.returnTo.empty() && fs::exists(tutorial_.returnTo, ec)) {
      openProjectFromPath(tutorial_.returnTo);
      // Outputs the tour switched on go off again, and BLACK goes back to
      // how it was: the operator's desk is handed back as it was lent.
      const int focusWas = project_.focusedOutputIndex;
      for (std::size_t i = 0; i < project_.outputs.size(); ++i) {
        const bool wasOn = i < tutorial_.outputsWereOn.size() && tutorial_.outputsWereOn[i];
        if (project_.outputs[i].enabled && !wasOn) {
          project_.focusedOutputIndex = static_cast<int>(i);
          setFocusedOutputEnabled(false);
        }
      }
      project_.focusedOutputIndex = focusWas;
      masterDimmerTarget_ = tutorial_.dimmerWas;
    }
    triggerToast(finished ? "that's the basics - MENU > LEARN THE BASICS runs it again"
                          : "tour paused - LEARN THE BASICS picks up where you left off",
                 ToastKind::Help);
  }

  // ── What each step is waiting for ─────────────────────────────────────────

  double tutorialSelectedTransitionSeconds() const {
    const Deck& deck = focusedDeck();
    if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
      const Cue& cue = deck.cues[static_cast<std::size_t>(deck.selectedIndex)];
      if (cue.cueTransitionSeconds >= 0.0) return cue.cueTransitionSeconds;
    }
    return deck.transitionSeconds;
  }

  bool tutorialAnyOutputOn() const {
    for (const auto& output : project_.outputs) {
      if (output.enabled) return true;
    }
    return false;
  }

  void serviceTutorial() {
    if (!tutorial_.active) return;
    const Deck& deck = focusedDeck();
    switch (tutorial_.step) {
      case 1:
        // MENU opens wherever it was last; once, during this step, it opens on
        // the outputs page, where the button the step is about lives.
        if (settingsOpen_ && settingsTab_ != 3 && !tutorial_.tabNudged) {
          settingsTab_ = 3;
          tutorial_.tabNudged = true;
        }
        if (tutorialAnyOutputOn()) {
          settingsOpen_ = false;
          advanceTutorial();
        }
        break;
      case 2:
        if (deck.cues.size() > tutorial_.cueBaseline) advanceTutorial();
        break;
      case 3:
        if (deck.activeIndex >= 0) advanceTutorial();
        break;
      case 4:
        if (deck.selectedIndex >= 0 && deck.selectedIndex != deck.activeIndex) advanceTutorial();
        break;
      case 5:
        // The transition row sits low in the inspector, often below its fold.
        // Bring it into view, a few frames at most, and then leave the scroll
        // to the operator.
        if (tutorialSelectedTransitionSeconds() <= 0.0 && tutorial_.inspectorNudges < 12) {
          const SDL_Rect row = tutorialTransitionRowRect();
          if (row.w > 0 && !tutorialRectInsideInspector(row)) {
            const SDL_Rect& v = cueSettingsViewportRect_;
            const int margin = uiScaled(24);
            const int delta = row.y + row.h > v.y + v.h ? (row.y + row.h) - (v.y + v.h) + margin
                                                        : row.y - v.y - margin;
            cueSettingsScroll_ = std::clamp(cueSettingsScroll_ + delta, 0, std::max(0, cueSettingsScrollMax_));
            ++tutorial_.inspectorNudges;
          }
        }
        if (const MediaEngine* engine = focusedMediaEngine(); engine && engine->transitionRunning()) {
          advanceTutorial();
        }
        break;
      case 6:
        if (masterDimmerTarget_ < 0.5) advanceTutorial();
        break;
      case 7:
        if (projectSaveCount_ > tutorial_.saveBaseline) advanceTutorial();
        break;
      default:
        break;
    }
  }

  void tutorialShowMe() {
    Deck& deck = focusedDeckMutable();
    switch (tutorial_.step) {
      case 0:
        advanceTutorial();
        break;
      case 1:
        setFocusedOutputEnabled(true);
        break;
      case 2:
        addGeneratorCue("swirl");
        break;
      case 3:
        if (deck.cues.empty()) {
          addGeneratorCue("swirl");
        }
        if (focusedDeck().selectedIndex < 0) selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        jumpSelectedCue();
        break;
      case 4: {
        if (deck.cues.size() < 2) {
          addGeneratorCue("fireside");
        }
        const Deck& now = focusedDeck();
        for (int i = 0; i < static_cast<int>(now.cues.size()); ++i) {
          if (i != now.activeIndex) {
            selectCueInDeck(project_.focusedDeckIndex, i, false, false);
            break;
          }
        }
        break;
      }
      case 5: {
        Deck& d = focusedDeckMutable();
        if (d.selectedIndex < 0 || d.selectedIndex == d.activeIndex) {
          for (int i = 0; i < static_cast<int>(d.cues.size()); ++i) {
            if (i != d.activeIndex) {
              selectCueInDeck(project_.focusedDeckIndex, i, false, false);
              break;
            }
          }
        }
        if (d.selectedIndex >= 0 && d.selectedIndex < static_cast<int>(d.cues.size()) &&
            tutorialSelectedTransitionSeconds() <= 0.0) {
          pushUndoSnapshot();
          d.cues[static_cast<std::size_t>(d.selectedIndex)].cueTransitionSeconds = 1.0;
          markProjectDirty();
        }
        jumpSelectedCue();
        break;
      }
      case 6:
        masterDimmerTarget_ = 0.0;
        triggerToast("BLACKOUT");
        break;
      case 7:
        saveProjectNow(true);
        break;
      default:
        break;
    }
  }

  // ── Where the pointer goes ────────────────────────────────────────────────

  SDL_Rect tutorialButtonRect(const char* label) const {
    for (const auto& button : buttons_) {
      if (button.label == label) return button.rect;
    }
    return SDL_Rect {};
  }

  // The same arithmetic the click handler uses to find a row, so the pointer
  // lands on the row a click would select. Empty when the row is scrolled out.
  SDL_Rect tutorialCueRowRect(int deckIndex, int cueIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size()) ||
        deckIndex >= static_cast<int>(deckListClipRects_.size()) ||
        deckIndex >= static_cast<int>(deckScrolls_.size())) {
      return SDL_Rect {};
    }
    const SDL_Rect& frame = deckListClipRects_[static_cast<std::size_t>(deckIndex)];
    const SDL_Rect clip {frame.x + 8, frame.y + 30, frame.w - 16, frame.h - 38};
    int listY = clip.y - deckScrolls_[static_cast<std::size_t>(deckIndex)];
    for (int index : cueIndicesForOverlayRole(project_.decks[static_cast<std::size_t>(deckIndex)], false)) {
      if (index == cueIndex) {
        if (listY < clip.y || listY + kRowHeight > clip.y + clip.h) return SDL_Rect {};
        return SDL_Rect {clip.x, listY, clip.w, kRowHeight};
      }
      listY += kRowHeight + 8;
    }
    return SDL_Rect {};
  }

  // The selected cue's transition row in the inspector: its - and + and the
  // value between them. Empty when the inspector is not showing it.
  SDL_Rect tutorialTransitionRowRect() const {
    SDL_Rect dec {}, inc {};
    for (const auto& qb : quickButtons_) {
      if (qb.action == QuickAction::TransDec && qb.rect.w > 0 && dec.w == 0) dec = qb.rect;
      if (qb.action == QuickAction::TransInc && qb.rect.w > 0 && inc.w == 0) inc = qb.rect;
    }
    if (dec.w == 0 || inc.w == 0) return SDL_Rect {};
    const int x0 = std::min(dec.x, inc.x), y0 = std::min(dec.y, inc.y);
    const int x1 = std::max(dec.x + dec.w, inc.x + inc.w), y1 = std::max(dec.y + dec.h, inc.y + inc.h);
    return SDL_Rect {x0, y0, x1 - x0, y1 - y0};
  }

  bool tutorialRectInsideInspector(const SDL_Rect& r) const {
    const SDL_Rect& v = cueSettingsViewportRect_;
    return v.h <= 0 || (r.y >= v.y && r.y + r.h <= v.y + v.h);
  }

  SDL_Rect tutorialTargetRect() const {
    const Deck& deck = focusedDeck();
    const int deckIndex = project_.focusedDeckIndex;
    switch (tutorial_.step) {
      case 1:
        if (settingsOpen_) {
          for (const auto& sb : settingsBtns_) {
            if (sb.action == kSettingsActionOutputToggle) return sb.rect;
          }
          return SDL_Rect {};
        }
        return tutorialButtonRect("MENU");
      case 2:
        return tutorialButtonRect("IMPORT");
      case 3:
        if (deck.selectedIndex < 0 && !deck.cues.empty()) return tutorialCueRowRect(deckIndex, 0);
        return tutorialButtonRect("TAKE");
      case 4:
        if (deck.cues.size() < 2) return tutorialButtonRect("IMPORT");
        for (int i = 0; i < static_cast<int>(deck.cues.size()); ++i) {
          if (i != deck.activeIndex) return tutorialCueRowRect(deckIndex, i);
        }
        return SDL_Rect {};
      case 5:
        if (tutorialSelectedTransitionSeconds() <= 0.0) {
          const SDL_Rect row = tutorialTransitionRowRect();
          if (row.w > 0 && tutorialRectInsideInspector(row)) return row;
          if (deck.selectedIndex >= 0) return tutorialCueRowRect(deckIndex, deck.selectedIndex);
        }
        return tutorialButtonRect("TAKE");
      case 6:
        return tutorialButtonRect("BLACK");
      case 7:
        return fileSaveBtnRect_;
      default:
        return SDL_Rect {};
    }
  }

  // ── Input ─────────────────────────────────────────────────────────────────

  // True when the click was the tour's own (a card button). Every other click
  // is passed on: the tour is waiting for the operator to use the desk.
  bool handleTutorialClick(int x, int y) {
    if (!tutorial_.active) return false;
    if (pointInRect(x, y, tutorial_.exitRect)) {
      endTutorial(tutorial_.step == kTutorialSteps - 1);
      return true;
    }
    if (pointInRect(x, y, tutorial_.showMeRect)) {
      if (tutorial_.step == kTutorialSteps - 1) {
        endTutorial(true, /*openExamples=*/true);
      } else {
        tutorialShowMe();
      }
      return true;
    }
    if (pointInRect(x, y, tutorial_.skipRect)) {
      if (tutorial_.step + 1 < kTutorialSteps) {
        enterTutorialStep(tutorial_.step + 1);
      }
      return true;
    }
    if (pointInRect(x, y, tutorial_.cardRect)) {
      return true;   // the card is not a hole into the desk under it
    }
    const SDL_Rect target = tutorialTargetRect();
    if (target.w > 0 && !pointInRect(x, y, target) && tutorial_.step > 0) {
      ++tutorial_.missClicks;
    }
    return false;
  }

  // Esc leaves the tour, but only when nothing else is waiting for it: an
  // open menu or a text field takes it first, as it always has.
  bool handleTutorialKey(SDL_Keycode key, bool keyRepeat) {
    if (!tutorial_.active || keyRepeat) return false;
    if (key == SDLK_ESCAPE && !settingsOpen_ && !inlineEditor_.open && !codeEditor_.open &&
        !confirmQuit_) {
      endTutorial(tutorial_.step == kTutorialSteps - 1);
      return true;
    }
    if (tutorial_.step == 0 && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
      advanceTutorial();
      return true;
    }
    return false;
  }

  // ── The teachers ──────────────────────────────────────────────────────────
  //
  // Sixteen by twenty, from the Teacher board. Drawn in the THEME's inks so
  // they belong on every colourway; the lipstick and the sparkle are the two
  // fixed colours, as on the board.
  void drawTutorialPortrait(const SDL_Rect& box, bool pixelTeacher, bool speaking) {
    static const char* kPixel[20] = {
      "....HHHHHHHH....",
      "...HHHHHHHHHH...",
      "..HHHSSSSSSHHH..",
      "..HHSSSSSSSSHH..",
      "..HHGGGSSGGGHH..",
      "..HHGDGSSGGGHH..",
      "..HHGGGSSGGGHH..",
      "..HHSSSSSSSSHH..",
      "..HHSSSSSSSSHH..",
      "..HHSSLLLLSSHH..",
      "..HHHSSSSSSHHH..",
      "..HHH.SSSS.HHHY.",
      "..HH..SSSS..HH..",
      ".HH.CCCDDCCC.HH.",
      ".H.CCCCDDCCCC.H.",
      "..CCCCCDDCCCCC..",
      ".CCCCCCDDCCCCCC.",
      ".CCCCCCDDCCCCCC.",
      ".CCCCCCDDCCCCCC.",
      ".CCCCCCDDCCCCCC.",
    };
    static const char* kVideo[20] = {
      "..H.HH.HH.HH.H..",
      ".HHHHHHHHHHHHHH.",
      ".HHHHHHHHHHHHHH.",
      "HHHHSSSSSSSSHHHH",
      ".HHSSSSSSSSSSHH.",
      "..SGGGGSSGGGGS..",
      "..SGDDGGGGDDGS..",
      "..SGGGGSSGGGGS..",
      "..SSSSSSSSSSSS..",
      "..SSSSSDDSSSSS..",
      "..SDSMMMMMMSDS..",
      "..SSDMSSSSMDSS..",
      "...SDSMMMMSDS...",
      "....SSSSSSSS....",
      ".....SSSSSS.....",
      "..KKKKKSSKKKKK..",
      ".KKKKKKKKKKKKKK.",
      "KKKKKKKKKKKKKKKK",
      "KKKKKKKKKKKKKKKK",
      "KKKKKKKKKKKKKKKK",
    };
    Primitives::fillRect(controlRenderer_, box, speaking ? pal.mid : pal.dark);
    const char* const* rows = pixelTeacher ? kPixel : kVideo;
    const int cell = std::max(1, std::min(box.w / 16, box.h / 20));
    const int ox = box.x + (box.w - cell * 16) / 2;
    // The one speaking bobs a pixel; the other holds still.
    const int bob = speaking ? static_cast<int>((animationNow_ / 260) % 2) : 0;
    const int oy = box.y + (box.h - cell * 20) / 2 - bob;
    const SDL_Color lipstick {200, 36, 60, 255};
    const SDL_Color sparkle {255, 220, 0, 255};
    for (int r = 0; r < 20; ++r) {
      for (int c = 0; c < 16; ++c) {
        SDL_Color color;
        switch (rows[r][c]) {
          case 'H': case 'G': case 'M': color = pal.deep; break;
          case 'S': color = pal.light; break;
          case 'D': color = pal.dark; break;
          case 'C': color = pal.light; break;
          case 'K': color = pal.deep; break;
          case 'L': color = lipstick; break;
          case 'Y': color = sparkle; break;
          default: continue;
        }
        Primitives::fillRect(controlRenderer_, SDL_Rect {ox + c * cell, oy + r * cell, cell, cell}, color);
      }
    }
  }

  // ── Drawing ───────────────────────────────────────────────────────────────

  void renderTutorial(int width, int height) {
    serviceTutorial();
    if (!tutorial_.active) return;
    const TutorialLine& line = tutorialLine(tutorial_.step);
    const SDL_Rect target = tutorialTargetRect();
    tutorial_.targetRect = target;

    // The dim, with a hole round the target.
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 6, 22, 6, 150);
    if (target.w > 0) {
      const int pad = uiScaled(4);
      const SDL_Rect hole {target.x - pad, target.y - pad, target.w + pad * 2, target.h + pad * 2};
      const SDL_FRect parts[4] = {
        {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(std::max(0, hole.y))},
        {0.0f, static_cast<float>(hole.y + hole.h), static_cast<float>(width),
         static_cast<float>(std::max(0, height - hole.y - hole.h))},
        {0.0f, static_cast<float>(hole.y), static_cast<float>(std::max(0, hole.x)), static_cast<float>(hole.h)},
        {static_cast<float>(hole.x + hole.w), static_cast<float>(hole.y),
         static_cast<float>(std::max(0, width - hole.x - hole.w)), static_cast<float>(hole.h)},
      };
      SDL_RenderFillRects(controlRenderer_, parts, 4);
      // The glow: yellow, breathing, so the eye finds it from across a room.
      const double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_) * 0.006);
      const Uint8 a = static_cast<Uint8>(150 + 105 * pulse);
      for (int i = 0; i < uiScaled(4); ++i) {
        SDL_SetRenderDrawColor(controlRenderer_, 255, 220, 0, static_cast<Uint8>(a - i * 25));
        const SDL_FRect ring {static_cast<float>(hole.x - i), static_cast<float>(hole.y - i),
                              static_cast<float>(hole.w + i * 2), static_cast<float>(hole.h + i * 2)};
        SDL_RenderRect(controlRenderer_, &ring);
      }
    } else if (tutorial_.step == 0 || tutorial_.step == kTutorialSteps - 1) {
      SDL_FRect all {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
      SDL_RenderFillRect(controlRenderer_, &all);
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    // The card: two portraits, two lines, the progress and the three buttons.
    TTF_Font* titleFont = fontPixelSmall_ ? fontPixelSmall_ : fontSmall_;
    const int pad = uiScaled(14);
    const int cardW = std::min(width - uiScaled(32), uiScaled(780));
    const int portrait = uiScaled(64);
    const int textX = pad + portrait + uiScaled(12);
    const int textW = cardW - textX - pad;

    bool praiseFromPixel = true;
    const bool praising = SDL_GetTicks() < tutorial_.praiseUntilMs && tutorial_.step > 0;
    const char* praise = praising ? tutorialPraise(tutorial_.praiseIndex, praiseFromPixel) : nullptr;
    bool roastFromPixel = true;
    const char* roast = (tutorial_.missClicks >= 4 && !praising)
                          ? tutorialRoast(tutorial_.step, roastFromPixel) : nullptr;

    std::string pixelSays = line.pixel;
    std::string videoSays = line.video;
#ifdef __APPLE__
    // A Mac saves with Cmd, and the tour says what the keyboard in front of
    // the operator says.
    for (std::string* said : {&pixelSays, &videoSays}) {
      for (std::size_t at = said->find("Ctrl+"); at != std::string::npos; at = said->find("Ctrl+", at)) {
        said->replace(at, 4, "Cmd");
      }
    }
#endif
    if (tutorial_.step == 1 && settingsOpen_) {
      pixelSays = "There. The button that says OUTPUT OFF. Make it say ON, skapi.";
    }
    if (tutorial_.step == 5 && tutorialSelectedTransitionSeconds() > 0.0) {
      pixelSays = "Good. " + formatSeconds(tutorialSelectedTransitionSeconds()) +
                  " of crossfade. Now TAKE, skapi.";
    }
    // A roast takes over the speaker's line; praise goes in the header, so the
    // next step's instructions are never hidden behind the last one's applause.
    if (roast) (roastFromPixel ? pixelSays : videoSays) = roast;

    const auto pixelLines = codeWrapText(fontBase_, "DR. PIXEL:  " + pixelSays, textW);
    const auto videoLines = codeWrapText(fontBase_, "PROF. VIDEO:  " + videoSays, textW);
    const int lineH = textLineHeight(fontBase_);
    const int headH = std::max(uiScaled(22), textLineHeight(titleFont));
    const int blockGap = uiScaled(10);
    const int pixelH = std::max(portrait, static_cast<int>(pixelLines.size()) * lineH);
    const int videoH = std::max(portrait, static_cast<int>(videoLines.size()) * lineH);
    const int buttonH = uiScaled(40);
    const int cardH = pad + headH + uiScaled(8) + pixelH + blockGap + videoH + uiScaled(12) + buttonH + pad;

    // Away from the target: above it when it is in the lower half, below it
    // when it is in the upper, centred when there is none.
    int cardY = (height - cardH) / 2;
    if (target.w > 0) {
      const int gap = uiScaled(18);
      if (target.y + target.h / 2 > height / 2) {
        cardY = target.y - gap - cardH;
      } else {
        cardY = target.y + target.h + gap;
      }
      cardY = std::clamp(cardY, uiScaled(8), std::max(uiScaled(8), height - cardH - uiScaled(8)));
    }
    int cardX = (width - cardW) / 2;
    if (target.w > 0) {
      cardX = std::clamp(target.x + target.w / 2 - cardW / 2, uiScaled(8),
                         std::max(uiScaled(8), width - cardW - uiScaled(8)));
    }
    const SDL_Rect card {cardX, cardY, cardW, cardH};
    tutorial_.cardRect = card;
    Primitives::drawFramedPanel(controlRenderer_, card, pal.shellInner, pal.deep, pal.light);

    int y = card.y + pad;
    char head[96];
    std::snprintf(head, sizeof(head), "STEP %d OF %d  -  %s", tutorial_.step + 1, kTutorialSteps, line.title);
    drawTextSafe(controlRenderer_, titleFont, SDL_Rect {card.x + pad, y, cardW - pad * 2, headH},
                 head, pal.inkSoft);
    if (praise) {
      const std::string said = std::string(praiseFromPixel ? "DR. PIXEL: " : "PROF. VIDEO: ") + praise;
      const int headLeftW = measuredTextWidth(titleFont, head) + uiScaled(24);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {card.x + pad + headLeftW, y, cardW - pad * 2 - headLeftW, headH},
                   said, pal.light);
    } else if (line.key && target.w > 0) {
#ifdef __APPLE__
      const std::string keyLabel = std::string(line.key) == "CTRL+S" ? "CMD+S" : line.key;
#else
      const std::string keyLabel = line.key;
#endif
      const int chipW = measuredTextWidth(titleFont, keyLabel) + uiScaled(20);
      const SDL_Rect chip {card.x + cardW - pad - chipW, y, chipW, headH};
      Primitives::fillRect(controlRenderer_, chip, SDL_Color {255, 220, 0, 255});
      drawCenteredText(controlRenderer_, titleFont, keyLabel.c_str(), SDL_Color {15, 56, 15, 255}, chip);
    }
    y += headH + uiScaled(8);

    const bool pixelSpeaking = roast ? roastFromPixel : ((animationNow_ / 4000) % 2) == 0;
    drawTutorialPortrait(SDL_Rect {card.x + pad, y, portrait, portrait}, true, pixelSpeaking);
    for (std::size_t i = 0; i < pixelLines.size(); ++i) {
      drawTextSafe(controlRenderer_, fontBase_,
                   SDL_Rect {card.x + textX, y + static_cast<int>(i) * lineH, textW, lineH},
                   pixelLines[i], pal.fg);
    }
    y += pixelH + blockGap;
    drawTutorialPortrait(SDL_Rect {card.x + pad, y, portrait, portrait}, false, !pixelSpeaking);
    for (std::size_t i = 0; i < videoLines.size(); ++i) {
      drawTextSafe(controlRenderer_, fontBase_,
                   SDL_Rect {card.x + textX, y + static_cast<int>(i) * lineH, textW, lineH},
                   videoLines[i], pal.fg);
    }
    y += videoH + uiScaled(12);

    // Progress: done, here, to come.
    const int dot = uiScaled(12);
    for (int i = 0; i < kTutorialSteps; ++i) {
      const SDL_Rect d {card.x + pad + i * (dot + uiScaled(5)), y + (buttonH - dot) / 2, dot, dot};
      if (i < tutorial_.step) {
        Primitives::fillRect(controlRenderer_, d, pal.light);
      } else if (i == tutorial_.step) {
        Primitives::fillRect(controlRenderer_, d, SDL_Color {255, 220, 0, 255});
      } else {
        Primitives::drawFramedPanel(controlRenderer_, d, pal.shellInner, pal.mid, pal.mid);
      }
    }

    // Buttons, right-aligned. The first and last steps have their own pair.
    const bool first = tutorial_.step == 0;
    const bool last = tutorial_.step == kTutorialSteps - 1;
    const char* primary = first ? "START" : (last ? "OPEN EXAMPLE SHOWS" : "SHOW ME");
    const char* exitLabel = last ? "CLOSE" : "EXIT (Esc)";
    auto buttonW = [&](const char* label) {
      return std::max(uiScaled(96), measuredTextWidth(fontBase_, label) + uiScaled(28));
    };
    int bx = card.x + cardW - pad;
    const int exitW = buttonW(exitLabel);
    bx -= exitW;
    tutorial_.exitRect = SDL_Rect {bx, y, exitW, buttonH};
    tutorial_.skipRect = SDL_Rect {};
    if (!first && !last) {
      const int skipW = buttonW("SKIP");
      bx -= uiScaled(10) + skipW;
      tutorial_.skipRect = SDL_Rect {bx, y, skipW, buttonH};
    }
    const int primaryW = buttonW(primary);
    bx -= uiScaled(10) + primaryW;
    tutorial_.showMeRect = SDL_Rect {bx, y, primaryW, buttonH};

    Primitives::drawFramedPanel(controlRenderer_, tutorial_.showMeRect, pal.light, pal.deep, pal.mid);
    drawCenteredText(controlRenderer_, fontBase_, primary, pal.deep, tutorial_.showMeRect);
    if (tutorial_.skipRect.w > 0) {
      Primitives::drawFramedPanel(controlRenderer_, tutorial_.skipRect, pal.dark, pal.deep, pal.mid);
      drawCenteredText(controlRenderer_, fontBase_, "SKIP", pal.light, tutorial_.skipRect);
    }
    Primitives::drawFramedPanel(controlRenderer_, tutorial_.exitRect, pal.dark, pal.deep, pal.mid);
    drawCenteredText(controlRenderer_, fontBase_, exitLabel, pal.light, tutorial_.exitRect);
  }
