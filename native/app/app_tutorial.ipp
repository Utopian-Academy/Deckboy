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
    // which outputs were on (and fullscreen), and whether BLACK was.
    struct OutputWas { bool on = false; bool fullscreen = false; };
    std::vector<OutputWas> outputsWere;
    double dimmerWas = 1.0;
    std::size_t cueBaseline = 0;
    int saveBaseline = 0;
    bool tabNudged = false;     // MENU opened on the outputs tab once
    int inspectorNudges = 0;    // frames spent scrolling the transition row into view
    // THE CROSSFADE STEP watches what the operator did, not an engine flag:
    // the cue that was live when it began, and the crossfade time the next
    // TAKE carried. transitionRunning() missed every playing video (the
    // outgoing clip moves to a tail engine and the new one has no texture to
    // fade from) and every hardware-decoded one, so step 6 waited for ever
    // with the second clip on screen (audit 2026-10-30, T6-01).
    std::string crossfadeFrom;       // the live cue's id; empty: nothing live yet
    double crossfadeSeconds = 0.0;   // the pending TAKE's crossfade, as last seen
    bool crossfadeWasCut = false;    // the last change was a cut, not a crossfade
    int missClicks = 0;         // clicks that hit nothing the step wanted
    // ONE SCREEN: OUTPUT ON fills the screen the desk is on, card and all.
    // The output that did it, held until a key or a click gives the desk
    // back; then the teachers get to talk about it for one step.
    int deskEatenOutput = -1;
    bool deskJoke = false;
    int deskEatenWhy = 0;       // TutorialDeskEaten: what the caption and the joke say
    // THE SCREENS, as last seen (tutorialScreens), so a screen plugged in,
    // pulled out or switched to mirrored mid-tour is noticed and answered.
    int screensSeen = -1;
    bool screensSeenMirrored = false;
    Uint64 screenLostMs = 0;    // when one went away: the desk eaten soon after says why
    std::string screenNote;     // Dr. Pixel's answer to a change, for a few seconds
    Uint64 screenNoteUntilMs = 0;
    // A tour resumed past the output step with the output off goes back to
    // that step first; this is the step it returns to once the output is on.
    int resumeAt = -1;
    Uint64 praiseUntilMs = 0;
    int praiseIndex = 0;
    SDL_Rect cardRect {};
    SDL_Rect showMeRect {};
    SDL_Rect skipRect {};
    SDL_Rect exitRect {};
    SDL_Rect targetRect {};
    // The teachers talk: each line is said aloud once (tutorialBabble), the
    // speaker's mouth moving while it is, and a click on the Professor is
    // the joke (drawTutorialPortrait).
    std::string pixelBabbled;
    std::string videoBabbled;
    Uint64 babbleUntilMs = 0;
    bool babbleIsPixel = true;
    SDL_Rect videoPortraitRect {};
    // THE RAKIA (James, 2026-10-30: "hide a ridiculous easter egg somewhere in
    // the tutorial"). Three quick clicks on Dr. Pixel pour one: the card sways,
    // the Professor goes rosy and sentimental, and Rat Dad (app_rat_dad.ipp)
    // walks off with the glass. Secret: never in CHANGES, the manual or the site.
    SDL_Rect pixelPortraitRect {};
    int pixelClicks = 0;
    Uint64 pixelClickAtMs = 0;
    Uint64 rakiaAtMs = 0;       // when the last one was poured
    int rakiaPoured = 0;
    Uint64 profCuteUntilMs = 0;
    int profCuteQuip = 0;
  };
  TutorialState tutorial_;
  // TESTSCREENS (local-only): the screens the tour believes are there, for
  // the check that walks it on a runner with one -- "1", "2", "3",
  // "mirrored", or empty for the real ones.
  std::string tutorialScreensOverride_;
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
       "Cuts are for amateurs. In the inspector, press + on transition, so it shows a time, not deck. Then TAKE.",
       "A crossfade! Portamento, but for pictures. Any time will do. A few more presses and it's a second. Then TAKE."},
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

  fs::path tutorialPracticeFile() const { return Paths::stateDir() / "tutorial-practice.deckboy"; }
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

  // Starts the tour, or says why not and leaves the operator's desk exactly as
  // it was. The audit of 0.99.419 (2026-10-24) found it starting after a
  // failed save, which lost an unsaved edit: the operator's work comes first.
  bool beginTutorial(std::string* why = nullptr) {
    if (tutorial_.active) return true;
    auto refuse = [&](const std::string& reason) {
      if (why) *why = reason;
      triggerToast("learn the basics: " + reason, ToastKind::Warning, kToastReadableMs);
      return false;
    };
    const fs::path practice = tutorialPracticeFile();
    std::error_code ec;
    const bool alreadyPractice = !currentProjectFile_.empty() &&
                                 fs::equivalent(currentProjectFile_, practice, ec) && !ec;
    TutorialState next;
    if (!alreadyPractice) {
      // A running programme cannot be handed back as it was: opening a show
      // clears what is playing. So the tour waits until nothing is.
      for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
        if (deckIsRunning(d)) return refuse("stop playback first - the tour cannot hand a running show back as it was");
      }
      if (!pendingProjectRecoveryFile_.empty()) return refuse("choose RECOVER or KEEP SAVED first");
      // THE OPERATOR'S SHOW FIRST: written to its own file, or no tour.
      if (projectDirty_ && (!project_.loadedCleanly || soakMode_ || !saveProjectNow(false))) {
        return refuse("your show could not be saved, so the tour has not started. Save it (SAVE AS) and try again");
      }
      next.returnTo = currentProjectFile_;
      for (std::size_t i = 0; i < project_.outputs.size(); ++i) {
        const OutputRuntime* runtime = runtimeForOutput(static_cast<int>(i));
        next.outputsWere.push_back({project_.outputs[i].enabled, runtime && runtime->fullscreenIntended});
      }
      next.dimmerWas = masterDimmerTarget_;
    }
    auto backOut = [&](const std::string& reason) {
      if (!next.returnTo.empty() && fs::exists(next.returnTo, ec)) openProjectFromPath(next.returnTo);
      return refuse(reason);
    };
    int step = readTutorialProgress();
    if (step > 0 && fs::exists(practice)) {
      if (!alreadyPractice) {
        openProjectFromPath(practice);
        if (!fs::equivalent(currentProjectFile_, practice, ec) || ec) return backOut("the practice show would not open");
      }
    } else {
      step = 0;
      if (!startNewShow(false)) return backOut("could not start the practice show");
      currentProjectFile_ = practice;
      if (!saveProjectNow(false)) return backOut("could not write the practice show to " + practice.parent_path().string());
    }
    // Opening the practice show turns its outputs off, and the lessons after
    // the output step need the picture: BLACK on an output that is off
    // changes nothing anyone can see. So a resumed tour goes back to the
    // output step first, then on to where it was. It never turns an output
    // on by itself.
    if (step >= 2 && step <= 6 && !tutorialAnyOutputOn()) {
      next.resumeAt = step;
      step = 1;
    }
    showStartupDialog_ = false;
    showSplashOverlay_ = false;
    settingsOpen_ = false;
    tutorial_ = next;
    tutorial_.active = true;
    // On a screen the desk is not on, where there is one.
    tutorialAimOutputs(tutorialScreens());
    enterTutorialStep(step);
    triggerToast(tutorial_.resumeAt > 0
                   ? "learn the basics: the output is off. Turn it on, and the tour carries on from step " +
                       std::to_string(tutorial_.resumeAt + 1)
                   : std::string("learn the basics: Esc leaves, and it picks up here next time"),
                 ToastKind::Help);
    return true;
  }

  // The step after this one: the saved step, when a resumed tour went back
  // for the output.
  int nextTutorialStep() const { return tutorial_.resumeAt > 0 ? tutorial_.resumeAt : tutorial_.step + 1; }

  void enterTutorialStep(int step) {
    tutorial_.step = std::clamp(step, 0, kTutorialSteps - 1);
    if (tutorial_.step != 1) tutorial_.resumeAt = -1;
    tutorial_.cueBaseline = focusedDeck().cues.size();
    tutorial_.saveBaseline = projectSaveCount_;
    tutorial_.tabNudged = false;
    tutorial_.inspectorNudges = 0;
    tutorial_.crossfadeFrom = tutorialLiveCueId();
    tutorial_.crossfadeSeconds = 0.0;
    tutorial_.crossfadeWasCut = false;
    tutorial_.missClicks = 0;
    tutorial_.deskJoke = false;
    // Left during the detour, the tour still comes back to the saved step.
    writeTutorialProgress(tutorial_.resumeAt > 0 ? tutorial_.resumeAt : tutorial_.step);
  }

  void advanceTutorial() {
    tutorial_.praiseUntilMs = SDL_GetTicks() + 3500;
    ++tutorial_.praiseIndex;
    playUiSound(UiSoundEffect::Toggle);
    if (tutorial_.step + 1 < kTutorialSteps) {
      enterTutorialStep(nextTutorialStep());
    }
  }

  // finished: the tour reached its end, so the next one starts from the top.
  // openExamples: leave for the example shows instead of the operator's show.
  void endTutorial(bool finished, bool openExamples = false) {
    if (!tutorial_.active) return;
    tutorial_.active = false;
    writeTutorialProgress(finished ? 0 : tutorial_.resumeAt > 0 ? tutorial_.resumeAt : tutorial_.step);
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
      // The desk handed back as it was lent: outputs the tour switched on go
      // off, outputs that were on come back on (opening a show disarms them,
      // so each is re-armed, fullscreen or not as it was), and BLACK goes
      // back to how it was.
      const int focusWas = project_.focusedOutputIndex;
      for (std::size_t i = 0; i < project_.outputs.size(); ++i) {
        const TutorialState::OutputWas was = i < tutorial_.outputsWere.size() ? tutorial_.outputsWere[i]
                                                                             : TutorialState::OutputWas {};
        if (project_.outputs[i].enabled == was.on) continue;
        project_.focusedOutputIndex = static_cast<int>(i);
        setFocusedOutputEnabled(was.on, was.fullscreen);
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

  // The id of the focused playlist's live cue, or empty.
  std::string tutorialLiveCueId() const {
    const Deck& deck = focusedDeck();
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) return {};
    return deck.cues[static_cast<std::size_t>(deck.activeIndex)].id;
  }

  bool tutorialAnyOutputOn() const {
    for (const auto& output : project_.outputs) {
      if (output.enabled) return true;
    }
    return false;
  }

  // An output that is on, full screen, on the display the desk is on.
  bool tutorialOutputCoversDesk(int outputIndex) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) return false;
    const OutputTarget& output = project_.outputs[static_cast<std::size_t>(outputIndex)];
    if (!output.enabled || normalizeOutputType(output.outputType) != "window") return false;
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputWindow || !runtime->fullscreenIntended || !controlWindow_) return false;
    return deckboyGetNumVideoDisplays() <= 1 ||
           outputDisplayIndex(outputIndex) == deckboyGetWindowDisplayIndex(controlWindow_);
  }

  // ── The screens in front of the operator ──────────────────────────────────
  //
  // James, 2026-10-30, on a PC with two screens: the tour said "One screen,
  // so the output took all of it", because a new show's output opens on the
  // first display -- usually the one the desk is on. "Make it adaptable and
  // reactable to whatever hardware config is being used." So the tour looks
  // at what is really there: how many screens, which one has the desk,
  // whether the desk's screen is mirrored onto another, and says and does
  // what suits that; and when a screen is plugged in, pulled out or
  // switched to mirrored mid-tour, it notices and answers.
  struct TutorialScreens {
    int count = 1;              // desktops (mirrored panels count as one)
    int desk = 0;               // the one the desk is on
    int spare = -1;             // where the picture should go: not the desk's
    std::string spareName;
    bool mirrored = false;      // the desk's desktop shows on more than one panel
    std::string mirroredWith;   // that other panel, when the OS says
    bool fake = false;          // TESTSCREENS is standing in
  };

  enum TutorialDeskEaten { kDeskEatenOneScreen, kDeskEatenMirrored, kDeskEatenWrongScreen, kDeskEatenScreenGone };

  TutorialScreens tutorialScreens() const {
    TutorialScreens s;
    if (!tutorialScreensOverride_.empty()) {
      s.fake = true;
      if (tutorialScreensOverride_ == "mirrored") {
        s.mirrored = true;
        s.mirroredWith = "TEST TV";
      } else {
        s.count = std::clamp(std::atoi(tutorialScreensOverride_.c_str()), 1, 8);
      }
      if (s.count > 1) {
        s.spare = 1;
        s.spareName = "TEST SCREEN 2";
      }
      return s;
    }
    s.count = std::max(1, deckboyGetNumVideoDisplays());
    s.desk = controlWindow_ ? std::clamp(deckboyGetWindowDisplayIndex(controlWindow_), 0, s.count - 1) : 0;
    for (int i = 0; i < s.count; ++i) {
      if (i != s.desk) {
        s.spare = i;
        break;
      }
    }
    if (s.spare >= 0) {
      const char* name = deckboyGetDisplayName(s.spare);
      s.spareName = name && *name ? std::string(name) : "screen " + std::to_string(s.spare + 1);
    }
    if (const auto* info = displayMirrorInfo(s.desk); info && info->known && info->panelCount > 1) {
      s.mirrored = true;
      if (!info->otherPanels.empty()) s.mirroredWith = info->otherPanels.front();
    }
    return s;
  }

  static std::string tutorialScreensWord(int count) {
    static const char* kWords[] = {"No", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight"};
    return count >= 0 && count <= 8 ? kWords[count] : std::to_string(count);
  }

  // How to stop mirroring, in the words of the system in front of them.
  static const char* tutorialExtendHow() {
#if defined(_WIN32)
    return "Press Win+P and choose Extend";
#elif defined(__APPLE__)
    return "In System Settings > Displays, use it as an extended display";
#else
    return "Set them to extend in your display settings";
#endif
  }

  // Step 1's instruction, for the screens there are.
  std::string tutorialOutputStepLine(const TutorialScreens& s) const {
    if (s.count >= 2) {
      // James's wording for two (2026-10-30), counted up for more.
      return tutorialScreensWord(s.count) + " screens, skapi. The desk stays on this one, the picture goes on " +
             (s.count == 2 ? "the other: " : "") + s.spareName +
             ". Open MENU and make OUTPUT OFF say OUTPUT ON.";
    }
    if (s.mirrored) {
      return "Your screens are mirrored, skapi: " +
             (s.mirroredWith.empty() ? std::string("the other one") : s.mirroredWith) +
             " shows what this one shows, so the picture will cover the desk on both. " + tutorialExtendHow() +
             ", and I put the picture on its own screen. Or open MENU and make OUTPUT OFF say OUTPUT ON.";
    }
    return tutorialLine(1).pixel;
  }

  // Why the output has the desk, for the caption drawn into it.
  static const char* tutorialDeskEatenCaption(int why) {
    switch (why) {
      case kDeskEatenMirrored: return "Mirrored screens, skapi. Press any key or click, and Dr. Pixel gives it back.";
      case kDeskEatenWrongScreen: return "Wrong screen, skapi. Press any key or click, and Dr. Pixel gives it back.";
      case kDeskEatenScreenGone:
        return "The other screen went away, skapi. Press any key or click, and Dr. Pixel gives it back.";
      default: return "One screen, skapi. Press any key or click, and Dr. Pixel gives it back.";
    }
  }

  // What Dr. Pixel says once the desk is back.
  std::string tutorialDeskEatenJoke(int why) const {
    const TutorialScreens s = tutorialScreens();
    switch (why) {
      case kDeskEatenMirrored:
        return std::string("It ate the desk, skapi. Mirrored screens show the picture on both, so I put it in a "
                           "little window, behind. ") + tutorialExtendHow() + ", and it gets a screen of its own.";
      case kDeskEatenWrongScreen:
        return "It ate the desk, skapi. You have " + tutorialScreensWord(s.count) +
               " screens: the output's DISPLAY in MENU puts it on " + (s.count == 2 ? "the other one." : "another.");
      case kDeskEatenScreenGone:
        return "The other screen went away, skapi, so the picture came back to this one. I put it in a little "
               "window, behind. Plug it back in and the picture goes back on it.";
      default:
        return "It ate the desk, skapi. One screen, so the output took all of it. I put it in a little window, "
               "behind. A real show gets a second screen.";
    }
  }

  int tutorialDeskEatenWhy() const {
    const TutorialScreens s = tutorialScreens();
    if (s.count >= 2) return kDeskEatenWrongScreen;
    if (s.mirrored) return kDeskEatenMirrored;
    if (tutorial_.screenLostMs != 0 && SDL_GetTicks() - tutorial_.screenLostMs < 20000) return kDeskEatenScreenGone;
    return kDeskEatenOneScreen;
  }

  void tutorialSayAboutScreens(const std::string& note) {
    tutorial_.screenNote = note;
    tutorial_.screenNoteUntilMs = SDL_GetTicks() + 9000;
  }

  // An output on, or about to be, as a window on the desk's screen.
  bool tutorialOutputOnDeskScreen(int outputIndex, const TutorialScreens& s) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) return false;
    const OutputTarget& output = project_.outputs[static_cast<std::size_t>(outputIndex)];
    if (normalizeOutputType(output.outputType) != "window") return false;
    return resolveOutputDisplayIndex(output, s.count) == s.desk;
  }

  // The practice show's output, aimed at a screen the desk is not on. Quietly
  // while it is off; on, it moves there and fills it, the way turning it on
  // there would have.
  bool tutorialAimOutputs(const TutorialScreens& s) {
    if (s.fake || s.count < 2 || s.spare < 0) return false;
    bool moved = false;
    for (int i = 0; i < static_cast<int>(project_.outputs.size()); ++i) {
      if (!tutorialOutputOnDeskScreen(i, s)) continue;
      OutputTarget& output = project_.outputs[static_cast<std::size_t>(i)];
      if (!output.enabled) {
        output.displayIndex = s.spare;
        recordOutputDisplayName(output);
        markProjectDirty();
        moved = true;
        continue;
      }
      const int focusWas = project_.focusedOutputIndex;
      project_.focusedOutputIndex = i;
      setOutputRecoveryPausedByEscape(i, false);
      moved = setOutputDisplayIndex(s.spare) || moved;
      project_.focusedOutputIndex = focusWas;
    }
    return moved;
  }

  // Once a frame: a screen plugged in, pulled out, or the desk's switched to
  // mirrored or back, answered as it happens.
  void serviceTutorialScreens() {
    const TutorialScreens s = tutorialScreens();
    const int seen = tutorial_.screensSeen;
    const bool wasMirrored = tutorial_.screensSeenMirrored;
    tutorial_.screensSeen = s.count;
    tutorial_.screensSeenMirrored = s.mirrored;
    if (seen < 0 || (seen == s.count && wasMirrored == s.mirrored)) return;
    if (s.count > seen) {
      const bool anyOn = tutorialAnyOutputOn();
      // A stand-in screen cannot take a window, so it is only talked about.
      const bool moved = s.fake ? s.spare >= 0 : tutorialAimOutputs(s);
      const std::string which = seen == 1 ? "A second screen, skapi" : "Another screen, skapi";
      if (moved && anyOn) {
        if (!s.fake) tutorial_.deskEatenOutput = -1;   // it has a screen of its own now
        tutorialSayAboutScreens(which + "! The picture is on it now: " + s.spareName + ". The desk is all yours.");
        if (controlWindow_ && !s.fake) SDL_RaiseWindow(controlWindow_);
      } else if (moved) {
        tutorialSayAboutScreens(which + ". When the output goes on, the picture goes there: " + s.spareName + ".");
      }
      return;
    }
    tutorial_.screenLostMs = SDL_GetTicks();
    if (s.mirrored && !wasMirrored) {
      tutorialSayAboutScreens(std::string("Your screens are mirrored now, skapi, so the picture shows on both. ") +
                              tutorialExtendHow() + ", and it gets its own again.");
    } else {
      tutorialSayAboutScreens(std::string("The other screen went away, skapi. ") +
                              (tutorialAnyOutputOn() ? "The picture is on this one now."
                                                     : "The picture will open on this one."));
    }
  }

  // The desk back: the output in a small window in the corner of the screen,
  // still on, and the desk in front of it. The tour's lessons need the desk;
  // the programme monitor on it shows what the output shows.
  void tutorialGiveDeskBack() {
    const int outputIndex = tutorial_.deskEatenOutput;
    tutorial_.deskEatenOutput = -1;
    if (OutputRuntime* runtime = runtimeForOutput(outputIndex); runtime && runtime->outputWindow) {
      SDL_SetWindowFullscreen(runtime->outputWindow, false);
      // A Mac leaves full screen with an animation, and a size set during it
      // is dropped.
      SDL_SyncWindow(runtime->outputWindow);
      setOutputRecoveryPausedByEscape(outputIndex, true);
      runtime->fullscreenIntended = false;
      SDL_Rect usable {};
      if (const SDL_DisplayID display = SDL_GetDisplayForWindow(controlWindow_);
          display != 0 && SDL_GetDisplayUsableBounds(display, &usable) && usable.w > 0 && usable.h > 0) {
        auto [rasterW, rasterH] = outputRenderSizeForOutput(outputIndex);
        const int w = std::max(160, usable.w / 3);
        const int h = std::max(90, rasterW > 0 ? w * std::max(1, rasterH) / rasterW : w * 9 / 16);
        const int margin = uiScaled(24);
        SDL_SetWindowSize(runtime->outputWindow, w, h);
        SDL_SetWindowPosition(runtime->outputWindow, usable.x + usable.w - w - margin,
                              usable.y + usable.h - h - margin);
      }
      showOutputWindow(runtime->outputWindow);
    }
    if (controlWindow_) SDL_RaiseWindow(controlWindow_);
    playUiSound(UiSoundEffect::Toggle);
    // Step 1's lesson was the output; the desk coming back is its end. Later,
    // the lesson carries on where it was, and Dr. Pixel says what happened.
    if (tutorial_.step == 1) {
      advanceTutorial();
      tutorial_.deskJoke = true;
    } else {
      tutorialSayAboutScreens(tutorialDeskEatenJoke(tutorial_.deskEatenWhy));
    }
  }

  // A click in the output window that ate the desk.
  bool handleTutorialOutputClick(Uint32 windowId) {
    if (!tutorial_.active || tutorial_.deskEatenOutput < 0) return false;
    if (outputIndexForWindowId(windowId) != std::optional<int>(tutorial_.deskEatenOutput)) return false;
    tutorialGiveDeskBack();
    return true;
  }

  // Drawn INTO the picture of the output that ate the desk: it is the only
  // thing on the screen, so it is the only place the hint can go.
  void drawTutorialDeskEatenCaption(int outputIndex, SDL_Renderer* renderer, int width, int height) {
    if (!tutorial_.active || outputIndex != tutorial_.deskEatenOutput || !renderer || width <= 0 || height <= 0) return;
    TTF_Font* headFont = fontAtSize(fontPixel_, std::clamp(height / 12, 12, 400));
    TTF_Font* hintFont = fontAtSize(fontBase_, std::clamp(height / 30, 8, 160));
    if (!headFont) headFont = fontPixel_;
    if (!hintFont) hintFont = fontBase_;
    if (!headFont || !hintFont) return;
    const std::string head = "IT ATE THE DESK";
    const std::string hint = tutorialDeskEatenCaption(tutorial_.deskEatenWhy);
    const int headW = measuredWidthIn(headFont, head);
    const int hintW = measuredWidthIn(hintFont, hint);
    const int gap = height / 40;
    const int padX = height / 20;
    const int plateW = std::min(width, std::max(headW, hintW) + padX * 2);
    const int plateH = textLineHeight(headFont) + gap + textLineHeight(hintFont) + gap * 4;
    const SDL_FRect plate {static_cast<float>((width - plateW) / 2), static_cast<float>((height - plateH) / 2),
                           static_cast<float>(plateW), static_cast<float>(plateH)};
    SDL_SetRenderDrawColor(renderer, 15, 56, 15, 255);
    SDL_RenderFillRect(renderer, &plate);
    SDL_SetRenderDrawColor(renderer, 255, 220, 0, 255);
    for (int i = 0; i < std::max(2, height / 270); ++i) {
      const SDL_FRect edge {plate.x + i, plate.y + i, plate.w - 2.0f * i, plate.h - 2.0f * i};
      SDL_RenderRect(renderer, &edge);
    }
    int y = static_cast<int>(plate.y) + gap * 2;
    drawTextRaw(renderer, headFont, head, SDL_Color {255, 220, 0, 255}, (width - headW) / 2, y);
    y += textLineHeight(headFont) + gap;
    drawTextRaw(renderer, hintFont, hint, SDL_Color {214, 240, 160, 255}, (width - hintW) / 2, y);
  }

  void serviceTutorial() {
    if (!tutorial_.active) return;
    serviceTutorialScreens();
    // After the output lesson, an output that takes the desk's screen -- a
    // screen pulled out, the screens mirrored, a full screen asked for -- is
    // held the same way: the lesson waits behind it until a key or a click
    // gives the desk back.
    if (tutorial_.step >= 2) {
      if (tutorial_.deskEatenOutput >= 0) {
        if (!tutorialOutputCoversDesk(tutorial_.deskEatenOutput)) tutorial_.deskEatenOutput = -1;
      } else {
        for (int i = 0; i < static_cast<int>(project_.outputs.size()); ++i) {
          if (tutorialOutputCoversDesk(i)) {
            tutorial_.deskEatenOutput = i;
            tutorial_.deskEatenWhy = tutorialDeskEatenWhy();
            break;
          }
        }
      }
      if (tutorial_.deskEatenOutput >= 0) return;
    }
    const Deck& deck = focusedDeck();
    switch (tutorial_.step) {
      case 1:
        // MENU opens wherever it was last; once, during this step, it opens on
        // the outputs page, where the button the step is about lives.
        if (settingsOpen_ && settingsTab_ != 3 && !tutorial_.tabNudged) {
          settingsTab_ = 3;
          tutorial_.tabNudged = true;
        }
        if (tutorial_.deskEatenOutput >= 0) {
          // Waiting for the key or click. An output switched off some other
          // way has given the desk back on its own.
          if (!tutorialOutputCoversDesk(tutorial_.deskEatenOutput)) tutorial_.deskEatenOutput = -1;
          break;
        }
        if (tutorialAnyOutputOn()) {
          settingsOpen_ = false;
          for (int i = 0; i < static_cast<int>(project_.outputs.size()); ++i) {
            if (tutorialOutputCoversDesk(i)) {
              tutorial_.deskEatenOutput = i;
              tutorial_.deskEatenWhy = tutorialDeskEatenWhy();
              break;
            }
          }
          if (tutorial_.deskEatenOutput < 0) advanceTutorial();
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
        {
          // Passed by a DIFFERENT cue going live with a crossfade time. Not by
          // the first TAKE after a resume (nothing was live to fade from; it
          // only makes something live), and not by taking the same cue again.
          const std::string live = tutorialLiveCueId();
          if (tutorial_.crossfadeFrom.empty()) {
            tutorial_.crossfadeFrom = live;
          } else if (!live.empty() && live != tutorial_.crossfadeFrom) {
            if (tutorial_.crossfadeSeconds > 0.0) {
              advanceTutorial();
              break;
            }
            tutorial_.crossfadeFrom = live;   // a cut: the lesson is still to come
            tutorial_.crossfadeWasCut = true;
          } else {
            tutorial_.crossfadeSeconds = tutorialSelectedTransitionSeconds();
          }
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
        if (tutorial_.deskEatenOutput >= 0) {
          tutorialGiveDeskBack();
        } else {
          setFocusedOutputEnabled(true);
        }
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
        if (d.cues.size() < 2) addGeneratorCue("fireside");
        if (focusedDeck().cues.size() < 2) addGeneratorCue("swirl");
        // Nothing live (a resumed tour, or a skipped step): one cue goes live
        // first, so there is something to crossfade FROM.
        if (focusedDeck().activeIndex < 0) {
          selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
          jumpSelectedCue();
        }
        tutorial_.crossfadeFrom = tutorialLiveCueId();
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
        tutorial_.crossfadeSeconds = tutorialSelectedTransitionSeconds();
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
        enterTutorialStep(nextTutorialStep());
      }
      return true;
    }
    if (pointInRect(x, y, tutorial_.pixelPortraitRect)) {
      const Uint64 now = SDL_GetTicks();
      tutorial_.pixelClicks = now - tutorial_.pixelClickAtMs < 700 ? tutorial_.pixelClicks + 1 : 1;
      tutorial_.pixelClickAtMs = now;
      if (tutorial_.pixelClicks >= 3 && (tutorial_.rakiaAtMs == 0 || now - tutorial_.rakiaAtMs > 13000)) {
        tutorial_.pixelClicks = 0;
        tutorial_.rakiaAtMs = now;
        ++tutorial_.rakiaPoured;
        playUiSound(UiSoundEffect::Take);
      }
      return true;
    }
    if (pointInRect(x, y, tutorial_.videoPortraitRect)) {
      tutorial_.profCuteUntilMs = SDL_GetTicks() + 2600;   // the joke, see renderTutorial
      ++tutorial_.profCuteQuip;
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
    // ANY key while the output has the screen, Esc included: Esc would
    // otherwise leave the tour from behind a screen with no tour on it.
    if (tutorial_.deskEatenOutput >= 0) {
      tutorialGiveDeskBack();
      return true;
    }
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
  // they belong on every colourway; the lipstick, the sparkle and the blush
  // are the fixed colours. Dr. Pixel is the "a tad cuter" one: round lenses
  // (F) with big eyes (E) and a glint (W), and a blush (B). James kept the
  // Professor as he was ("I love the new Dr Pixel but prefer the old prof
  // video. Maybe use the cute him as a joke?", 2026-10-28): the cute one,
  // bow tie and all, is what a click on him shows for a moment.
  //
  // And they move: a blink now and then, out of step with each other, and a
  // mouth that opens and shuts while the line is being said aloud.
  void drawTutorialPortrait(const SDL_Rect& box, bool pixelTeacher, bool speaking) {
    static const char* kPixel[20] = {
      "....HHHHHHHH....",
      "...HHHHHHHHHH...",
      "..HHHHHHHHHHHH..",
      "..HHHHHHHSSSHH..",
      "..HHHSSSSSSSSH..",
      "..HFFFFSSFFFFH..",
      "..HFWEFFFFWEFH..",
      "..HFEEFSSFEEFH..",
      "..HSFFSSSSFFSH..",
      "..HBBSSSSSSBBH..",
      "..HSSSSLLSSSSH..",
      "..HHSSSSSSSSHHY.",
      "..HHH.SSSS.HHH..",
      "...HH.SSSS.HH...",
      "....CCCDDCCC....",
      "...CCCCDDCCCC...",
      "..CCCCCDDCCCCC..",
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
    static const char* kVideoCute[20] = {
      "...HH.HHHH.HH...",
      "..HHHHHHHHHHHH..",
      ".HHHHHHHHHHHHHH.",
      ".HHHSSSSSSSSHHH.",
      ".HHSSSSSSSSSSHH.",
      "..SFFFFSSFFFFS..",
      "..SFWEFFFFWEFS..",
      "..SFEEFSSFEEFS..",
      "..SSFFSSSSFFSS..",
      "..SBBSSDDSSBBS..",
      "..SSMMMMMMMMSS..",
      "..SSMSSDDSSMSS..",
      "...SSSSSSSSSS...",
      "....SSSSSSSS....",
      "......SSSS......",
      "..KKKKLLLLKKKK..",
      ".KKKKKKLLKKKKKK.",
      "KKKKKKKKKKKKKKKK",
      "KKKKKKKKKKKKKKKK",
      "KKKKKKKKKKKKKKKK",
    };
    const bool cute = !pixelTeacher && SDL_GetTicks() < tutorial_.profCuteUntilMs;
    const char* const* source = pixelTeacher ? kPixel : cute ? kVideoCute : kVideo;
    char rows[20][17];
    for (int r = 0; r < 20; ++r) {
      std::memcpy(rows[r], source[r], 17);
    }
    // A blink: 140 ms in every 3.9 s, the Professor 1.7 s behind.
    const bool blinking = (animationNow_ + (pixelTeacher ? 0 : 1700)) % 3900 < 140;
    // Talking: the mouth opens and shuts every 90 ms while the line is said.
    const bool talking = speaking && SDL_GetTicks() < tutorial_.babbleUntilMs &&
                         tutorial_.babbleIsPixel == pixelTeacher;
    const bool mouthOpen = talking && ((animationNow_ / 90) % 2) == 0;
    if (pixelTeacher || cute) {
      if (blinking) {
        for (int c = 0; c < 16; ++c) {
          if (rows[6][c] == 'W' || rows[6][c] == 'E') rows[6][c] = 'F';
        }
      }
      if (mouthOpen) {
        const int below = pixelTeacher ? 11 : 12;   // under the lips, or the smile
        rows[below][7] = 'D';
        rows[below][8] = 'D';
      }
    } else {
      if (blinking) {
        rows[6][4] = rows[6][5] = rows[6][10] = rows[6][11] = 'G';   // the pupils shut
      }
      if (mouthOpen) {
        rows[11][7] = 'D';
        rows[11][8] = 'D';
      }
    }
    Primitives::fillRect(controlRenderer_, box, speaking ? pal.mid : pal.dark);
    const int cell = std::max(1, std::min(box.w / 16, box.h / 20));
    const int ox = box.x + (box.w - cell * 16) / 2;
    // The one speaking bobs a pixel; the other holds still.
    const int bob = speaking ? static_cast<int>((animationNow_ / 260) % 2) : 0;
    const int oy = box.y + (box.h - cell * 20) / 2 - bob;
    const SDL_Color lipstick {200, 36, 60, 255};
    const SDL_Color sparkle {255, 220, 0, 255};
    const SDL_Color blush {255, 143, 168, 255};
    for (int r = 0; r < 20; ++r) {
      for (int c = 0; c < 16; ++c) {
        SDL_Color color;
        switch (rows[r][c]) {
          case 'H': case 'G': case 'M': case 'E': color = pal.deep; break;
          case 'S': case 'W': color = pal.light; break;
          case 'D': case 'F': color = pal.dark; break;
          case 'C': color = pal.light; break;
          case 'K': color = pal.deep; break;
          case 'L': color = lipstick; break;
          case 'Y': color = sparkle; break;
          case 'B': color = blush; break;
          default: continue;
        }
        Primitives::fillRect(controlRenderer_, SDL_Rect {ox + c * cell, oy + r * cell, cell, cell}, color);
      }
    }
  }

  // ANIMAL CROSSING ON A GAME BOY (James, 2026-10-28: "animal crossing esque
  // voices"). A blip a letter, pitched from the letter, so a line sounds like
  // itself every time it is said: Dr. Pixel bright and high, the Professor
  // an octave down and rounder. Vowels are the round pulse, consonants the
  // thin one. Through the desk's own DMG synth, so the Sound effects switch
  // turns it off, and only ever in the tour, which runs in its practice show.
  void tutorialBabble(bool pixel, const std::string& line) {
    if (!tutorial_.active) return;
    static const int kScale[6] = {0, 2, 4, 7, 9, 12};
    const double base = pixel ? 659.25 : 329.63;   // E5, E4
    std::vector<DmgNote> notes;
    int at = 0;
    int letters = 0;
    for (std::size_t i = 0; i < line.size() && letters < 26; ++i) {
      const int c = std::tolower(static_cast<unsigned char>(line[i]));
      if (c < 'a' || c > 'z') {
        if (c == '.' || c == '!' || c == '?') at += 110;
        else if (c == ' ' || c == ',') at += 30;
        continue;
      }
      const bool vowel = std::strchr("aeiouy", c) != nullptr;
      const double freq = base * std::pow(2.0, kScale[(c - 'a') % 6] / 12.0);
      notes.push_back(DmgNote {letters % 2, freq, at, vowel ? 58 : 46, vowel ? 0.5 : 0.25,
                               vowel ? 11 : 9});
      at += vowel ? 64 : 52;
      ++letters;
    }
    if (notes.empty()) return;
    queueDmgPattern(notes, 0.09f);
    tutorial_.babbleUntilMs = SDL_GetTicks() + static_cast<Uint64>(at);
    tutorial_.babbleIsPixel = pixel;
  }

  // ── Drawing ───────────────────────────────────────────────────────────────

  // Dr. Pixel's line now: the step's, fitted to the screens there are and to
  // what has just happened, before any roast takes it over. The card draws it
  // and TUTORIAL STATUS reports it.
  std::string tutorialPixelSays() const {
    std::string says = tutorialLine(tutorial_.step).pixel;
    if (tutorial_.step == 1) {
      says = settingsOpen_ ? std::string("There. The button that says OUTPUT OFF. Make it say ON, skapi.")
                           : tutorialOutputStepLine(tutorialScreens());
    }
    if (tutorial_.step == 2 && tutorial_.deskJoke) {
      says = tutorialDeskEatenJoke(tutorial_.deskEatenWhy) + " Now a picture: press I, or SHOW ME.";
    }
    // The crossfade step says what is missing, rather than "TAKE" again.
    if (tutorial_.step == 5 && tutorial_.crossfadeFrom.empty()) {
      says = "Nothing is live to fade from, skapi. TAKE one cue first, then pick the other and give it a time.";
    } else if (tutorial_.step == 5 && tutorial_.crossfadeWasCut && tutorialSelectedTransitionSeconds() <= 0.0) {
      says = "That was a cut, skapi. Fast, but not a crossfade. Pick the other cue, press + on transition, then TAKE.";
    } else if (tutorial_.step == 5 && tutorialSelectedTransitionSeconds() > 0.0) {
      says = "Good. " + formatSeconds(tutorialSelectedTransitionSeconds()) + " of crossfade. Now TAKE, skapi.";
    }
    // A screen plugged in or pulled out is answered for a few seconds, then
    // the step's own instruction comes back.
    if (SDL_GetTicks() < tutorial_.screenNoteUntilMs) says = tutorial_.screenNote;
    return says;
  }

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

    std::string pixelSays = tutorialPixelSays();
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
    if (tutorial_.step == 2 && tutorial_.deskJoke) {
      videoSays = "A feedback loop! Output patched straight back into the desk. Happens to the best of us. "
                  "Mostly to me. 1983. We don't talk about 1983.";
    }
    // A roast takes over the speaker's line; praise goes in the header, so the
    // next step's instructions are never hidden behind the last one's applause.
    if (roast) (roastFromPixel ? pixelSays : videoSays) = roast;
    // THE JOKE: a click on the Professor shows the cuter him for a moment,
    // and he has something to say about it.
    // The rakia: twelve seconds of it, in three acts. Twice a session; after
    // that Dr. Pixel cuts everyone off.
    const double rakiaT = tutorial_.rakiaAtMs ? (SDL_GetTicks() - tutorial_.rakiaAtMs) / 1000.0 : 1e9;
    const bool rakiaDry = tutorial_.rakiaPoured > 2;
    const bool rakia = rakiaT < (rakiaDry ? 4.0 : 12.0);
    if (rakia && rakiaDry) {
      pixelSays = "No more rakia. The rat has a problem.";
    } else if (rakia && rakiaT < 4.0) {
      pixelSays = "Nazdrave, skapi! Baba's rakia, from the Rhodopes. One glass, for the lesson.";
      videoSays = "Is this... is this allowed during a tutorial?";
    } else if (rakia && rakiaT < 8.0) {
      pixelSays = "*shakes head* That means one more.";
      videoSays = "I love you all. I love the oscillator. I love the OUTPUT. It's video. I know. I KNOW.";
    } else if (rakia) {
      pixelSays = "...and the rat took the rakia. Every time, skapi. Every single time.";
      videoSays = "Was that a rat? In a hat? With a pizza?";
    }
    const bool profJoke = SDL_GetTicks() < tutorial_.profCuteUntilMs;
    if (profJoke) {
      static const char* kQuips[4] = {
        "Stop that. I am a serious man.",
        "Do not tell Dr. Pixel about this.",
        "This never happened. Back to the patch bay.",
        "It was the eighties. Everyone had a bow tie.",
      };
      videoSays = kQuips[(tutorial_.profCuteQuip + 3) % 4];
    }

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
    // Rakia sways the card: gently at first, then less gently.
    if (rakia && !rakiaDry) {
      const double sway = std::min(1.0, rakiaT / 3.0) * (rakiaT > 10.0 ? (12.0 - rakiaT) / 2.0 : 1.0);
      cardX += static_cast<int>(std::sin(rakiaT * 2.6) * uiScaled(8) * sway);
      cardY += static_cast<int>(std::sin(rakiaT * 1.3) * uiScaled(3) * sway);
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

    const bool pixelSpeaking = profJoke ? false
                             : roast ? roastFromPixel : ((animationNow_ / 4000) % 2) == 0;
    // Said aloud once a line: when a teacher's turn comes round with a line
    // not yet said -- a new step, a praise, a roast, the joke -- and silent
    // after that however long the step is left open.
    {
      const std::string& spoken = pixelSpeaking ? pixelSays : videoSays;
      std::string& said = pixelSpeaking ? tutorial_.pixelBabbled : tutorial_.videoBabbled;
      if (said != spoken) {
        said = spoken;
        tutorialBabble(pixelSpeaking, spoken);
      }
    }
    tutorial_.pixelPortraitRect = SDL_Rect {card.x + pad, y, portrait, portrait};
    drawTutorialPortrait(tutorial_.pixelPortraitRect, true, pixelSpeaking);
    if (rakia && !rakiaDry && rakiaT < 8.0) {
      // The glass, raised beside the doctor: clear, with the good stuff in it.
      const int g = std::max(2, portrait / 16);
      const SDL_Rect glass {card.x + pad + portrait - g * 3, y + portrait - g * 7, g * 4, g * 6};
      Primitives::fillRect(controlRenderer_, glass, SDL_Color {230, 240, 240, 255});
      Primitives::fillRect(controlRenderer_, SDL_Rect {glass.x + g, glass.y + g * 2, g * 2, g * 3},
                           SDL_Color {250, 225, 150, 255});
    }
    for (std::size_t i = 0; i < pixelLines.size(); ++i) {
      drawTextSafe(controlRenderer_, fontBase_,
                   SDL_Rect {card.x + textX, y + static_cast<int>(i) * lineH, textW, lineH},
                   pixelLines[i], pal.fg);
    }
    y += pixelH + blockGap;
    tutorial_.videoPortraitRect = SDL_Rect {card.x + pad, y, portrait, portrait};
    drawTutorialPortrait(tutorial_.videoPortraitRect, false, !pixelSpeaking);
    if (rakia && !rakiaDry && rakiaT >= 4.0) {
      // The Professor goes rosy.
      const SDL_Rect& v = tutorial_.videoPortraitRect;
      const int c = std::max(1, std::min(v.w / 16, v.h / 20));
      const int ox = v.x + (v.w - c * 16) / 2;
      const int oy = v.y + (v.h - c * 20) / 2;
      const SDL_Color rosy {255, 120, 140, 255};
      Primitives::fillRect(controlRenderer_, SDL_Rect {ox + c * 3, oy + c * 9, c * 2, c}, rosy);
      Primitives::fillRect(controlRenderer_, SDL_Rect {ox + c * 11, oy + c * 9, c * 2, c}, rosy);
    }
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
    // Act three: Rat Dad walks along the top of the card, right to left, and
    // leaves with the glass.
    if (rakia && !rakiaDry && rakiaT >= 8.0) {
      const int cell = std::max(2, uiScaled(3));
      const int ratW = kRatDadW * cell;
      const int ratH = kRatDadH * cell;
      const double walk = std::clamp((rakiaT - 8.0) / 3.6, 0.0, 1.0);
      const int rx = card.x + card.w - static_cast<int>(walk * (card.w + ratW));
      const int ry = card.y - ratH + static_cast<int>((animationNow_ / 140) % 2) * cell;
      const Uint64 now = SDL_GetTicks();
      drawRatDadSprite(controlRenderer_, rx, ry, cell, ((now / 120) % 2) == 0, false);
      const SDL_Rect glass {rx + cell * 5, ry - cell * 4, cell * 3, cell * 4};
      Primitives::fillRect(controlRenderer_, glass, SDL_Color {230, 240, 240, 255});
      Primitives::fillRect(controlRenderer_, SDL_Rect {glass.x + cell / 2, glass.y + cell, cell * 2, cell * 2},
                           SDL_Color {250, 225, 150, 255});
    }
  }
