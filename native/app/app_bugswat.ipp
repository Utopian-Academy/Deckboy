// ============================================================================
// BUG SWAT -- the Motion lesson, as a game (core/bugswat.hpp has the rules).
//
// James, 2026-11-05, tutorials board Cihkd2Se ("i dig", after "more gameboy",
// "more software bug coded", "make them cute"). A handheld in the Game Boy's
// four greens over the desk. Each round starts with a briefing that asks for
// one real Motion control -- on, two zones, sensitivity, fall, Motion X on a
// tone -- and waits until the operator has set it (or pressed SHOW ME, which
// sets it for them and says so). Then the bugs come, and each one can only
// be fixed with that control set right.
//
// Like every lesson it runs in a practice show (lendDesk, app_tutorial.ipp):
// the operator's show is saved, the game plays in its own, and the desk is
// handed back as it was. The briefings point at Settings > Audio > MOTION
// CONTROL (SELECT opens it); while that is open the handheld steps aside.
//
// Part of class App -- included inside the class body in main.cpp.
// ============================================================================

  struct BugSwatState {
    enum class Phase { Brief, Play, Over };
    bool active = false;
    Phase phase = Phase::Brief;
    deckboy::bugswat::Game game;
    DeskLoan loan;
    int round = 0;               // the round the briefing or the play is about
    Uint64 lastMs = 0;
    int highScore = 0;
    bool newHigh = false;
    bool shownMe = false;        // SHOW ME did this round's job
    bool clearedAll = true;      // no round skipped or lost to the clock
    int fixedSeen = 0;           // for the sound and the praise on a fix
    int escapedSeen = 0;
    Uint64 lineUntilMs = 0;      // a remark (a fix, an escape) holds the dialogue box
    std::string remark;
    bool remarkFromPixel = false;
    Uint64 tickAtMs = 0;         // the boss's ticking
    std::string babbled;         // the line last said aloud
    // Hit boxes, set each frame by renderBugSwat.
    SDL_Rect bodyRect {}, screenRect {}, dpadRect {}, showMeRect {}, skipRect {}, startRect {}, selectRect {}, exitRect {};
    int buttonJokes = 0;         // how many times they have pressed a button at a bug
  };
  BugSwatState bugSwat_;

  fs::path bugSwatPracticeFile() const { return Paths::stateDir() / "bugswat-practice.deckboy"; }
  fs::path bugSwatHighScoreFile() const { return Paths::stateDir() / "bugswat-highscore.txt"; }

  int readBugSwatHighScore() const {
    std::ifstream in(bugSwatHighScoreFile());
    int score = 0;
    if (in >> score) return std::max(0, score);
    return 0;
  }

  void writeBugSwatHighScore(int score) const {
    std::error_code ec;
    fs::create_directories(Paths::stateDir(), ec);
    std::ofstream out(bugSwatHighScoreFile(), std::ios::trunc);
    if (out) out << score << '\n';
  }

  // Starts the game, or says why not and leaves the desk exactly as it was.
  bool beginBugSwat(std::string* why = nullptr) {
    if (bugSwat_.active) return true;
    auto refuse = [&](const std::string& reason) {
      if (why) *why = reason;
      triggerToast("bug swat: " + reason, ToastKind::Warning, kToastReadableMs);
      return false;
    };
    if (tutorial_.active) return refuse("finish or leave LEARN THE BASICS first");
    BugSwatState next;
    std::string reason;
    if (!lendDesk(next.loan, reason)) return refuse(reason);
    if (!startNewShow(false)) {
      handDeskBack(next.loan);
      return refuse("could not start the practice show");
    }
    currentProjectFile_ = bugSwatPracticeFile();
    dressPracticeLikeDesk(next.loan);
    if (!saveProjectNow(false)) {
      handDeskBack(next.loan);
      return refuse("could not write the practice show to " + bugSwatPracticeFile().parent_path().string());
    }
    showStartupDialog_ = false;
    showSplashOverlay_ = false;
    settingsOpen_ = false;
    next.active = true;
    next.highScore = readBugSwatHighScore();
    next.game.start(0, static_cast<std::uint32_t>(SDL_GetTicks()) | 1u);
    next.lastMs = SDL_GetTicks();
    bugSwat_ = std::move(next);
    enterBugSwatBrief(0);
    triggerToast("bug swat: Esc leaves, and your show comes back as it was", ToastKind::Help);
    return true;
  }

  void endBugSwat() {
    if (!bugSwat_.active) return;
    bugSwat_.active = false;
    if (bugSwat_.game.score() > bugSwat_.highScore) writeBugSwatHighScore(bugSwat_.game.score());
    // The practice show goes; Motion follows the show it hands back to.
    project_.motionEnabled = false;
    stopAllDecksForLesson();
    handDeskBack(bugSwat_.loan);
    triggerToast("bug swat: your show is back. Motion's controls are in Settings > Audio", ToastKind::Help);
  }

  // The tone the last round makes is still playing when the game ends:
  // stopped before the operator's show comes back, so it does not carry over.
  void stopAllDecksForLesson() {
    for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
      if (deckIsRunning(d)) {
        if (auto* engine = mediaEngineForDeck(d)) engine->stop();
      }
    }
  }

  void enterBugSwatBrief(int round) {
    bugSwat_.round = std::clamp(round, 0, deckboy::bugswat::kRounds - 1);
    bugSwat_.phase = BugSwatState::Phase::Brief;
    bugSwat_.shownMe = false;
    bugSwat_.remark.clear();
    bugSwat_.lineUntilMs = 0;
  }

  // ── What each briefing asks for ──────────────────────────────────────────

  struct BugSwatBrief {
    const char* job;       // the one thing to do, on the screen
    const char* video;     // Prof. Video teaches this track
    const char* pixel;     // Dr. Pixel heckles
    const char* playing;   // said while the round is on
  };

  static const BugSwatBrief& bugSwatBrief(int round) {
    static const BugSwatBrief kBriefs[deckboy::bugswat::kRounds] = {
      {"TURN MOTION ON",
       "Bugs are crawling to the cue list! Swat them by waving at your camera. First, Motion: in MOTION CONTROL "
       "turn it ON and choose what it READS.",
       "This one is NullPointer. Hollow, points at nothing. A finger goes straight through. Use the whole hand, skapi.",
       "Wave like you are hailing a cab! A small, polite cab. Not the buttons. The cab."},
      {"DRAW TWO ZONES",
       "Race Condition: twins, each sure it got there first. One reading cannot tell your hands apart, so drag two "
       "ZONES on the Motion picture, one each side.",
       "Hit both together. Hit one, and they swap. Like my cousins at a wedding.",
       "One hand each side. Together! Together!"},
      {"SENSITIVITY 5%",
       "Off By One. The bug is not the face, it is the tiny extra segment, and a little wiggle needs a finer reading. "
       "Turn SENSITIVITY down to 5%.",
       "Lower number, finer reading. Yes, I also think it is backwards.",
       "Not the face. The little one on the end. Wiggle a finger."},
      {"FALL 1.5 S OR MORE",
       "Memory Leak. It only lets go while the reading stays up. Make FALL longer, 1.5 seconds or more, so your "
       "sweeps join up into one.",
       "Slow and steady, skapi. Like paying back a loan.",
       "Sweep, and sweep again. Let the fall carry you between."},
      {"MOTION X ON A TONE",
       "The Heisenbug! It fades whenever you move. Put a tone cue's pitch on Motion X and follow it by ear: the "
       "ticking speeds up as you close in.",
       "SHOW ME makes the tone for you. High is right. Low is left. Stand still and it comes back.",
       "Follow it. Listen. Faster ticking, closer bug."},
    };
    return kBriefs[std::clamp(round, 0, deckboy::bugswat::kRounds - 1)];
  }

  bool bugSwatToneReady() const {
    for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
      if (!deckIsRunning(d)) continue;
      for (const Cue& cue : project_.decks[static_cast<std::size_t>(d)].cues) {
        const auto& lfo = cue.toneLfo[static_cast<std::size_t>(kToneLfoFreq)];
        if (cue.kind == CueKind::Tone && lfo.on && lfo.shape == deckboy::effects::LfoShape::MotionX) return true;
      }
    }
    return false;
  }

  bool bugSwatRoundReady(int round) const {
    const bool reading = project_.motionEnabled && motionFramesRead_ > 5;
    switch (round) {
      case 0: return reading;
      case 1: return reading && project_.motion.zones.size() >= 2;
      case 2: return reading && project_.motion.sensitivity <= 0.055f;
      case 3: return reading && project_.motion.fallSeconds >= 1.5f;
      case 4: return reading && bugSwatToneReady();
      default: return reading;
    }
  }

  // A cartoon paw that waves across a dark picture, for a computer with no
  // camera: Motion reads it from a playlist, so nobody is locked out.
  static const char* bugSwatPawExpression() {
    return "px = 0.5 + 0.34*sin(t*0.7); py = 0.5 + 0.26*sin(t*1.6 + 0.6);"
           "dx = (x - px)*1.78; dy = y - py;"
           "pad = 1 - smoothstep(0.13, 0.15, sqrt(dx*dx + (dy - 0.03)*(dy - 0.03)));"
           "t1 = 1 - smoothstep(0.045, 0.06, sqrt((dx + 0.12)*(dx + 0.12) + (dy + 0.13)*(dy + 0.13)));"
           "t2 = 1 - smoothstep(0.045, 0.06, sqrt(dx*dx + (dy + 0.18)*(dy + 0.18)));"
           "t3 = 1 - smoothstep(0.045, 0.06, sqrt((dx - 0.12)*(dx - 0.12) + (dy + 0.13)*(dy + 0.13)));"
           "c = max(pad, max(t1, max(t2, t3)));"
           "0.06 + c*0.55, 0.22 + c*0.52, 0.06";
  }

  // SHOW ME: does the round's job, the way the operator would.
  void bugSwatShowMe(bool forcePaw = false) {
    switch (bugSwat_.round) {
      case 0: {
        const bool camera = !forcePaw && !motionCameras(true).empty();
        if (camera) {
          project_.motionSource = "camera:default";
        } else {
          // The paw, on playlist 1, taken so it plays.
          addGeneratorCue("code");
          Deck& deck = focusedDeckMutable();
          if (!deck.cues.empty()) {
            Cue& paw = deck.cues.back();
            paw.name = "Waving paw";
            paw.codeExpression = bugSwatPawExpression();
            paw.width = 640;
            paw.height = 360;
          }
          takeSelected(true);
          project_.motionSource = "deck:" + std::to_string(project_.focusedDeckIndex);
        }
        project_.motionEnabled = true;
        break;
      }
      case 1:
        // As seen: the left half and the right half.
        project_.motion.zones = {{0.02f, 0.05f, 0.46f, 0.9f}, {0.52f, 0.05f, 0.46f, 0.9f}};
        break;
      case 2:
        project_.motion.sensitivity = 0.05f;
        break;
      case 3:
        project_.motion.fallSeconds = 2.0f;
        break;
      case 4: {
        addToneCue();
        if (Cue* tone = selectedCueMutable()) {
          auto& lfo = tone->toneLfo[static_cast<std::size_t>(kToneLfoFreq)];
          lfo.on = true;
          lfo.shape = deckboy::effects::LfoShape::MotionX;
          lfo.depth = 0.35f;
          lfo.motionZone = 0;
          tone->name = "Heisenbug finder";
        }
        takeSelected(true);
        break;
      }
      default:
        break;
    }
    project_.motion = deckboy::motion::clampSettings(project_.motion);
    markProjectDirty();
    bugSwat_.shownMe = true;
    playUiSound(UiSoundEffect::Toggle);
  }

  void bugSwatStart() {
    if (bugSwat_.phase == BugSwatState::Phase::Over) {
      bugSwat_.game.start(0, static_cast<std::uint32_t>(SDL_GetTicks()) | 1u);
      bugSwat_.newHigh = false;
      bugSwat_.clearedAll = true;
      bugSwat_.fixedSeen = bugSwat_.escapedSeen = 0;
      enterBugSwatBrief(0);
      return;
    }
    if (bugSwat_.phase == BugSwatState::Phase::Play) {
      bugSwatButtonJoke();
      return;
    }
    if (bugSwat_.phase != BugSwatState::Phase::Brief || !bugSwatRoundReady(bugSwat_.round)) {
      playUiSound(UiSoundEffect::Error);
      return;
    }
    if (bugSwat_.game.round() != bugSwat_.round) bugSwat_.game.skipToRound(bugSwat_.round);
    bugSwat_.phase = BugSwatState::Phase::Play;
    bugSwat_.lastMs = SDL_GetTicks();
    bugSwat_.fixedSeen = bugSwat_.game.fixedThisRound();
    bugSwat_.escapedSeen = bugSwat_.game.escaped();
    playUiSound(UiSoundEffect::Take);
  }

  void bugSwatSkip() {
    if (bugSwat_.phase == BugSwatState::Phase::Over) return;
    bugSwat_.clearedAll = false;
    const int next = bugSwat_.round + 1;
    if (next >= deckboy::bugswat::kRounds) {
      bugSwat_.game.skipToRound(next);
      finishBugSwat();
    } else {
      bugSwat_.game.skipToRound(next);
      enterBugSwatBrief(next);
    }
  }

  void finishBugSwat() {
    bugSwat_.phase = BugSwatState::Phase::Over;
    markLessonDone("bugswat");
    bugSwat_.clearedAll = bugSwat_.clearedAll && bugSwat_.game.roundsCleared() == deckboy::bugswat::kRounds;
    if (bugSwat_.game.score() > bugSwat_.highScore) {
      bugSwat_.highScore = bugSwat_.game.score();
      bugSwat_.newHigh = true;
      writeBugSwatHighScore(bugSwat_.highScore);
    }
    playUiSound(UiSoundEffect::PowerUp);
  }

  // Once per tick, after serviceMotion.
  void serviceBugSwat() {
    if (!bugSwat_.active) return;
    const Uint64 now = SDL_GetTicks();
    const double dt = std::min(0.1, (now - bugSwat_.lastMs) / 1000.0);
    bugSwat_.lastMs = now;
    if (bugSwat_.phase != BugSwatState::Phase::Play || settingsOpen_) return;
    const deckboy::bugswat::MotionView view = deckboy::bugswat::viewOf(motionAnalyzer_, project_.motion);
    const int roundWas = bugSwat_.game.round();
    bugSwat_.game.update(dt, view);
    if (bugSwat_.game.over()) {
      finishBugSwat();
      return;
    }
    if (bugSwat_.game.round() != roundWas) {
      playUiSound(UiSoundEffect::PowerUp);
      enterBugSwatBrief(bugSwat_.game.round());
      return;
    }
    if (bugSwat_.game.fixedThisRound() > bugSwat_.fixedSeen) {
      bugSwat_.fixedSeen = bugSwat_.game.fixedThisRound();
      static const char* kFixed[] = {"FIXED! Lovely signal flow.", "*shakes head* That means yes. Bravo.",
                                     "Closed as fixed. No regressions. Probably.", "Squashed! Ship it."};
      bugSwat_.remark = kFixed[bugSwat_.fixedSeen % 4];
      bugSwat_.remarkFromPixel = (bugSwat_.fixedSeen % 2) == 1;
      bugSwat_.lineUntilMs = now + 2200;
      playUiSound(UiSoundEffect::Clear);
    }
    if (bugSwat_.game.escaped() > bugSwat_.escapedSeen) {
      bugSwat_.escapedSeen = bugSwat_.game.escaped();
      bugSwat_.remark = "One reached the cue list. It has filed a bug report about you.";
      bugSwat_.remarkFromPixel = true;
      bugSwat_.lineUntilMs = now + 2600;
      playUiSound(UiSoundEffect::Error);
    }
    // The boss ticks faster as the reading closes in on it.
    if (bugSwat_.round == 4) {
      const float nearness = bugSwat_.game.heisenNearness(view);
      if (nearness > 0.05f) {
        const Uint64 every = static_cast<Uint64>(700.0f - 620.0f * nearness);
        if (now - bugSwat_.tickAtMs >= every) {
          bugSwat_.tickAtMs = now;
          playUiSound(UiSoundEffect::Navigate);
        }
      }
    }
  }

  // James, 2026-11-05: jokes about using your HANDS, not the handheld's
  // buttons. Said when the operator clicks the screen, the D-pad or a dead
  // button, or hammers the keyboard, instead of waving.
  void bugSwatButtonJoke() {
    static const struct { bool pixel; const char* line; } kJokes[] = {
      {true, "Skapi. It is a handheld. You do not press it. You wave at it."},
      {false, "The buttons are for menus. The bugs are for flailing."},
      {true, "The D-pad is decorative. Like my diploma."},
      {false, "In 1989 we also pressed the buttons. Then we discovered waving. Never looked back."},
      {true, "Pressing harder does not help. The bugs cannot feel buttons. Only arms."},
      {false, "It's called a handheld because your hands are held... up. In the air. Waving."},
      {true, "The mouse? You are swatting bugs with a MOUSE? In Bulgaria the mouse would be on the bugs' side."},
      {false, "Lovely clicking. Very crisp. The camera saw none of it."},
    };
    const auto& joke = kJokes[bugSwat_.buttonJokes % (sizeof kJokes / sizeof kJokes[0])];
    ++bugSwat_.buttonJokes;
    bugSwat_.remark = joke.line;
    bugSwat_.remarkFromPixel = joke.pixel;
    bugSwat_.lineUntilMs = SDL_GetTicks() + 3800;
    playUiSound(UiSoundEffect::Error);
  }

  // ── Input ────────────────────────────────────────────────────────────────

  // Every click while the handheld is up is the handheld's; with the settings
  // modal open it steps aside so MOTION CONTROL can be used.
  bool handleBugSwatClick(int x, int y) {
    if (!bugSwat_.active || settingsOpen_) return false;
    if (pointInRect(x, y, bugSwat_.exitRect)) {
      endBugSwat();
    } else if (pointInRect(x, y, bugSwat_.showMeRect)) {
      if (bugSwat_.phase == BugSwatState::Phase::Brief) bugSwatShowMe();
      else if (bugSwat_.phase == BugSwatState::Phase::Play) bugSwatButtonJoke();
    } else if (pointInRect(x, y, bugSwat_.dpadRect) ||
               (bugSwat_.phase == BugSwatState::Phase::Play &&
                (pointInRect(x, y, bugSwat_.screenRect) || pointInRect(x, y, bugSwat_.startRect)))) {
      bugSwatButtonJoke();
    } else if (pointInRect(x, y, bugSwat_.skipRect)) {
      bugSwatSkip();
    } else if (pointInRect(x, y, bugSwat_.startRect)) {
      bugSwatStart();
    } else if (pointInRect(x, y, bugSwat_.selectRect)) {
      openBugSwatMotionCard();
    }
    return true;
  }

  void openBugSwatMotionCard() {
    debugOpenSettings(1);   // the Audio tab; MOTION CONTROL is its last card
    settingsAudioScroll_ = std::max(settingsAudioScroll_, settingsAudioScrollMax_);
    playUiSound(UiSoundEffect::Navigate);
  }

  bool handleBugSwatKey(SDL_Keycode key, bool keyRepeat) {
    if (!bugSwat_.active || settingsOpen_) return false;
    if (keyRepeat) return true;
    if (key == SDLK_ESCAPE) {
      endBugSwat();
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
      bugSwatStart();
    } else if (key == SDLK_A) {
      if (bugSwat_.phase == BugSwatState::Phase::Brief) bugSwatShowMe();
    } else if (key == SDLK_B) {
      bugSwatSkip();
    } else if (key == SDLK_M) {
      openBugSwatMotionCard();
    } else if (bugSwat_.phase == BugSwatState::Phase::Play && SDL_GetTicks() >= bugSwat_.lineUntilMs) {
      bugSwatButtonJoke();   // typing at the bugs
    }
    return true;
  }

  // ── Drawing ──────────────────────────────────────────────────────────────

  // The Game Boy's four greens, darkest first.
  static SDL_Color bugSwatInk(int shade) {
    static const SDL_Color kGreens[4] = {{15, 56, 15, 255}, {48, 98, 48, 255}, {139, 172, 15, 255}, {155, 188, 15, 255}};
    return kGreens[std::clamp(shade, 0, 3)];
  }

  // Sprites as drawn on the board: 3 outline, 2 shade and blush, 1 body,
  // 0 light, '.' nothing. Each is anchored on its face's centre.
  struct BugSwatSprite {
    std::vector<const char*> rows;
    int cx, cy;
  };

  static const BugSwatSprite& bugSwatSprite(deckboy::bugswat::Kind kind) {
    using deckboy::bugswat::Kind;
    static const BugSwatSprite kNull {{"...3....3...", "....3..3....", "..33333333..", ".3000000003.",
                                       "300330033003", "300330033003", "300000000003", "320003300023",
                                       ".3000000003.", "..33333333..", ".3.3....3.3."}, 6, 6};
    static const BugSwatSprite kTwin {{"..3..3..", ".333333.", "31111113", "31011013", "31311313",
                                       "32133123", ".333333.", ".3.33.3."}, 4, 4};
    static const BugSwatSprite kOffBy1 {{"...3....3.....................", "....3..3......................",
                                         "..33333333....................", ".3111111113.333..333..333.....",
                                         "31001111001311133111331113....", "31031111031311113111131111333.",
                                         "3111111111131111311113111132223", "32111331112311113111131111332.",
                                         ".3111111113.333..333..333.....", "..33333333....................",
                                         "...3....3....3....3....3......"}, 5, 6};
    static const BugSwatSprite kLeak {{"..............3333....", "...3....3....311113...", "....3..3....31133113..",
                                       "..33333333.3113113113.", ".3111111113113113113..", "31001111001311331113..",
                                       "31031111031311111113..", "3111111111133333333...", "32111331112311111113..",
                                       ".33333333333333333333."}, 5, 6};
    static const BugSwatSprite kHeisen {{"...3....3...", "....3..3....", "..33333333..", ".3111111113.",
                                         "310011110013", "310311110313", "311111111113", "321113311123",
                                         ".3111111113.", "..33333333..", ".3.3....3.3."}, 6, 6};
    switch (kind) {
      case Kind::NullPointer: return kNull;
      case Kind::Twin: return kTwin;
      case Kind::OffByOne: return kOffBy1;
      case Kind::Leak: return kLeak;
      case Kind::Heisen: return kHeisen;
    }
    return kNull;
  }

  static const BugSwatSprite& bugSwatPaw() {
    static const BugSwatSprite kPaw {{"..33..33..33..", ".3113311331133", ".3113113113113", "33111111111113",
                                      "31111111111113", "31121111112113", ".311111111113.", "..3333333333.."}, 7, 4};
    return kPaw;
  }

  // One sprite at a point given in handheld pixels. `dither` 0 draws every
  // pixel, 1 every other one in a checker, 2 one in four: how the boss fades
  // on a screen with no alpha.
  void drawBugSwatSprite(const BugSwatSprite& s, int originX, int originY, int gx, int gy, int k, int dither = 0) {
    for (std::size_t r = 0; r < s.rows.size(); ++r) {
      const char* row = s.rows[r];
      for (int c = 0; row[c]; ++c) {
        const char ch = row[c];
        if (ch < '0' || ch > '3') continue;
        const int px = gx - s.cx + c, py = gy - s.cy + static_cast<int>(r);
        if (dither == 1 && ((px + py) & 1)) continue;
        if (dither == 2 && ((px & 1) || (py & 1))) continue;
        // '3' is the darkest ink, '0' the lightest.
        Primitives::fillRect(controlRenderer_, SDL_Rect {originX + px * k, originY + py * k, k, k},
                             bugSwatInk(3 - (ch - '0')));
      }
    }
  }

  void renderBugSwat(int width, int height) {
    if (!bugSwat_.active) return;
    const Uint64 now = SDL_GetTicks();
    TTF_Font* pixelFont = fontPixelSmall_ ? fontPixelSmall_ : fontSmall_;
    TTF_Font* lineFont = fontSmall_;
    if (settingsOpen_) {
      // Stepped aside: one strip says what the round wants and how to get back.
      const std::string job = std::string("BUG SWAT - ROUND ") + std::to_string(bugSwat_.round + 1) + ": " +
                              bugSwatBrief(bugSwat_.round).job +
                              (bugSwatRoundReady(bugSwat_.round) ? "  -  DONE! CLOSE THE MENU TO PLAY"
                                                                 : "  -  CLOSE THE MENU WHEN IT IS SET");
      const int stripH = textLineHeight(pixelFont) + uiScaled(12);
      const SDL_Rect strip {0, height - stripH, width, stripH};
      Primitives::fillRect(controlRenderer_, strip, bugSwatInk(0));
      drawCenteredTextSafe(controlRenderer_, pixelFont, strip, job, bugSwatInk(3));
      return;
    }

    // The desk dims behind the handheld.
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 170);
    SDL_FRect all {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
    SDL_RenderFillRect(controlRenderer_, &all);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    // THE HANDHELD, in its own pixels: a body 200 x 250 round a 160 x 144
    // screen. k whole screen pixels to one of its pixels, so nothing smears.
    constexpr int kBodyW = 200, kBodyH = 250;
    const int k = std::max(2, std::min((width - uiScaled(24)) / kBodyW, (height - uiScaled(24)) / kBodyH));
    const SDL_Rect body {(width - kBodyW * k) / 2, (height - kBodyH * k) / 2, kBodyW * k, kBodyH * k};
    bugSwat_.bodyRect = body;
    const SDL_Color shell {196, 192, 184, 255}, shellDark {150, 146, 140, 255}, bezel {84, 86, 108, 255};
    Primitives::fillRect(controlRenderer_, body, shell);
    Primitives::fillRect(controlRenderer_, SDL_Rect {body.x, body.y + body.h - k * 2, body.w, k * 2}, shellDark);
    const SDL_Rect bezelRect {body.x + 10 * k, body.y + 10 * k, 180 * k, 162 * k};
    Primitives::fillRect(controlRenderer_, bezelRect, bezel);
    const int sx = body.x + 20 * k, sy = body.y + 16 * k;   // the screen's top-left
    const SDL_Rect screen {sx, sy, 160 * k, 144 * k};
    bugSwat_.screenRect = screen;
    Primitives::fillRect(controlRenderer_, screen, bugSwatInk(3));
    drawTextSafe(controlRenderer_, pixelFont, SDL_Rect {bezelRect.x + 4 * k, bezelRect.y + 146 * k, 120 * k, 14 * k},
                 "DECKBOY  POCKET", SDL_Color {200, 200, 220, 255});

    // Header: the round and the score.
    const SDL_Rect header {sx, sy, 160 * k, 12 * k};
    Primitives::fillRect(controlRenderer_, header, bugSwatInk(0));
    const auto& game = bugSwat_.game;
    char left[64];
    std::snprintf(left, sizeof left, "BUG SWAT  R%d", bugSwat_.round + 1);
    drawTextSafe(controlRenderer_, pixelFont, SDL_Rect {sx + 3 * k, sy, 90 * k, 12 * k}, left, bugSwatInk(3));
    char right[48];
    std::snprintf(right, sizeof right, "SCORE %04d", game.score());
    const int rw = measuredTextWidth(pixelFont, right);
    drawTextSafe(controlRenderer_, pixelFont, SDL_Rect {sx + 157 * k - rw, sy, rw + k, 12 * k}, right, bugSwatInk(2));

    // The play area: 160 x 90, exactly Motion's grid, one cell a pixel.
    const int ax = sx, ay = sy + 13 * k;
    const int aw = deckboy::motion::kGridW, ah = deckboy::motion::kGridH;
    const auto& moved = motionAnalyzer_.moved();
    const bool playing = bugSwat_.phase == BugSwatState::Phase::Play;
    if (project_.motionEnabled && playing) {
      // What Motion sees, as a faint shadow: the player's own movement.
      for (int gy = 0; gy < ah; ++gy) {
        int run = -1;
        for (int gx = 0; gx <= aw; ++gx) {
          const bool on = gx < aw && moved[static_cast<std::size_t>(gy * aw + gx)];
          if (on && run < 0) run = gx;
          if (!on && run >= 0) {
            Primitives::fillRect(controlRenderer_, SDL_Rect {ax + run * k, ay + gy * k, (gx - run) * k, k}, bugSwatInk(2));
            run = -1;
          }
        }
      }
    }
    // The zones, dotted, numbered: in play, and on the briefing that asks
    // for them. Anywhere else they would run through the words.
    const bool showZones = playing || (bugSwat_.phase == BugSwatState::Phase::Brief && bugSwat_.round == 1);
    for (std::size_t z = 0; showZones && z < project_.motion.zones.size() && z < 8; ++z) {
      const auto& zone = project_.motion.zones[z];
      const int zx = ax + static_cast<int>(zone.x * aw) * k, zy = ay + static_cast<int>(zone.y * ah) * k;
      const int zw = static_cast<int>(zone.w * aw) * k, zh = static_cast<int>(zone.h * ah) * k;
      for (int d = 0; d < zw; d += 4 * k) {
        Primitives::fillRect(controlRenderer_, SDL_Rect {zx + d, zy, 2 * k, k}, bugSwatInk(1));
        Primitives::fillRect(controlRenderer_, SDL_Rect {zx + d, zy + zh - k, 2 * k, k}, bugSwatInk(1));
      }
      for (int d = 0; d < zh; d += 4 * k) {
        Primitives::fillRect(controlRenderer_, SDL_Rect {zx, zy + d, k, 2 * k}, bugSwatInk(1));
        Primitives::fillRect(controlRenderer_, SDL_Rect {zx + zw - k, zy + d, k, 2 * k}, bugSwatInk(1));
      }
      drawTextSafe(controlRenderer_, pixelFont, SDL_Rect {zx + 2 * k, zy + 2 * k, 30 * k, 10 * k},
                   "ZONE " + std::to_string(z + 1), bugSwatInk(1));
    }
    // The cue list's edge, where they are headed.
    for (int d = 0; d < ah; d += 3) {
      Primitives::fillRect(controlRenderer_, SDL_Rect {ax, ay + d * k, k, 2 * k}, bugSwatInk(0));
    }

    const bool bob = ((now / 300) % 2) == 0;
    if (bugSwat_.phase == BugSwatState::Phase::Play) {
      for (const auto& b : game.bugs()) {
        const int gx = static_cast<int>(b.x * aw), gy = static_cast<int>(b.y * ah) + (bob ? 0 : 1);
        if (b.escaped) continue;
        if (b.fixed) {
          if (b.fixedFor > 0.7f) continue;
          drawBugSwatSprite(bugSwatPaw(), ax, ay, gx, gy, k);
          drawCenteredTextSafe(controlRenderer_, pixelFont,
                               SDL_Rect {ax + (gx - 20) * k, ay + (gy - 16) * k, 40 * k, 8 * k}, "FIXED!", bugSwatInk(0));
          continue;
        }
        int dither = 0;
        if (b.kind == deckboy::bugswat::Kind::Heisen) dither = b.seen > 0.66f ? 0 : b.seen > 0.33f ? 1 : 2;
        // A Twin hit and waiting for its partner blinks.
        if (b.kind == deckboy::bugswat::Kind::Twin && b.charge > 0.0f && ((now / 120) % 2)) continue;
        drawBugSwatSprite(bugSwatSprite(b.kind), ax, ay, gx, gy, k, dither);
        if (b.kind == deckboy::bugswat::Kind::Twin && b.zone > static_cast<int>(project_.motion.zones.size())) {
          drawCenteredTextSafe(controlRenderer_, pixelFont, SDL_Rect {ax + (gx - 6) * k, ay + (gy - 16) * k, 12 * k, 8 * k},
                               "?", bugSwatInk(0));
        }
        if ((b.kind == deckboy::bugswat::Kind::Leak || b.kind == deckboy::bugswat::Kind::Heisen) && b.charge > 0.0f) {
          const SDL_Rect bar {ax + (gx - 7) * k, ay + (gy - 10) * k, 14 * k, 2 * k};
          Primitives::fillRect(controlRenderer_, bar, bugSwatInk(1));
          Primitives::fillRect(controlRenderer_, SDL_Rect {bar.x, bar.y, static_cast<int>(bar.w * b.charge), bar.h},
                               bugSwatInk(0));
        }
      }
      // Fixed so far, and the clock.
      char hud[64];
      std::snprintf(hud, sizeof hud, "FIXED %d/%d   %02d S", game.fixedThisRound(), game.toFixThisRound(),
                    static_cast<int>(std::ceil(game.roundSecondsLeft())));
      drawTextSafe(controlRenderer_, pixelFont, SDL_Rect {ax + 4 * k, ay + (ah - 10) * k, 150 * k, 10 * k}, hud,
                   bugSwatInk(0));
    } else if (bugSwat_.phase == BugSwatState::Phase::Brief) {
      // The round's bug, big, and its one job.
      const auto& round = deckboy::bugswat::rounds()[static_cast<std::size_t>(bugSwat_.round)];
      drawCenteredTextSafe(controlRenderer_, pixelFont, SDL_Rect {ax, ay + 6 * k, aw * k, 10 * k},
                           std::string("ROUND ") + std::to_string(bugSwat_.round + 1) + ": " + round.name, bugSwatInk(0));
      const BugSwatSprite& sprite = bugSwatSprite(round.kind);
      const int big = k * 2;
      const int spriteW = static_cast<int>(std::strlen(sprite.rows[0]));
      drawBugSwatSprite(sprite, ax + (aw * k - spriteW * big) / 2 + sprite.cx * big, ay + 22 * k + sprite.cy * big,
                        0, 0, big);
      const bool ready = bugSwatRoundReady(bugSwat_.round);
      drawCenteredTextSafe(controlRenderer_, pixelFont, SDL_Rect {ax, ay + 56 * k, aw * k, 10 * k},
                           bugSwatBrief(bugSwat_.round).job, bugSwatInk(0));
      drawCenteredTextSafe(controlRenderer_, pixelFont, SDL_Rect {ax, ay + 70 * k, aw * k, 10 * k},
                           ready ? (bugSwat_.shownMe ? "DONE FOR YOU - PRESS START" : "DONE! PRESS START")
                                 : "SELECT OPENS MOTION CONTROL",
                           ready && ((now / 400) % 2) ? bugSwatInk(1) : bugSwatInk(0));
    } else {
      drawCenteredTextSafe(controlRenderer_, pixelFont, SDL_Rect {ax, ay + 10 * k, aw * k, 12 * k},
                           bugSwat_.clearedAll ? "ALL BUGS FIXED" : "GAME OVER", bugSwatInk(0));
      drawBugSwatSprite(bugSwatSprite(deckboy::bugswat::Kind::Heisen), ax, ay, aw / 2, 40 + (bob ? 0 : 1), k * 1);
      char line[64];
      std::snprintf(line, sizeof line, "SCORE %d   HIGH SCORE %d", game.score(), bugSwat_.highScore);
      drawCenteredTextSafe(controlRenderer_, pixelFont, SDL_Rect {ax, ay + 56 * k, aw * k, 10 * k}, line, bugSwatInk(0));
      drawCenteredTextSafe(controlRenderer_, pixelFont, SDL_Rect {ax, ay + 70 * k, aw * k, 10 * k},
                           bugSwat_.newHigh ? "NEW HIGH SCORE!" : "START PLAYS AGAIN", bugSwatInk(1));
    }

    // The dialogue box, as on a handheld RPG: the teacher whose turn it is.
    const SDL_Rect box {sx + 2 * k, sy + 104 * k, 156 * k, 38 * k};
    Primitives::fillRect(controlRenderer_, box, bugSwatInk(0));
    const SDL_Rect inner {box.x + k, box.y + k, box.w - 2 * k, box.h - 2 * k};
    Primitives::fillRect(controlRenderer_, inner, bugSwatInk(3));
    bool fromPixel = false;
    std::string said;
    const BugSwatBrief& brief = bugSwatBrief(bugSwat_.round);
    if (now < bugSwat_.lineUntilMs && !bugSwat_.remark.empty()) {
      said = bugSwat_.remark;
      fromPixel = bugSwat_.remarkFromPixel;
    } else if (bugSwat_.phase == BugSwatState::Phase::Over) {
      fromPixel = ((now / 6000) % 2) == 1;
      said = fromPixel ? "Nazdrave, skapi. Now go and fix real bugs."
                       : "Every one of those is a real Motion control. Put them on any ~ knob you like.";
    } else if (bugSwat_.phase == BugSwatState::Phase::Play) {
      said = brief.playing;
      fromPixel = (bugSwat_.round % 2) == 1;
    } else {
      fromPixel = ((now / 7000) % 2) == 1;
      said = fromPixel ? brief.pixel : brief.video;
    }
    const std::string who = fromPixel ? "DR. PIXEL: " : "PROF. VIDEO: ";
    const int textPad = 3 * k;
    const auto lines = codeWrapText(lineFont, who + lessonText(said), inner.w - textPad * 2);
    const int lineH = textLineHeight(lineFont);
    int ty = inner.y + textPad / 2;
    for (const auto& text : lines) {
      if (ty + lineH > inner.y + inner.h) break;
      drawTextSafe(controlRenderer_, lineFont, SDL_Rect {inner.x + textPad, ty, inner.w - textPad * 2, lineH}, text,
                   bugSwatInk(0));
      ty += lineH;
    }
    if (said != bugSwat_.babbled) {
      bugSwat_.babbled = said;
      tutorialBabble(fromPixel, said);
    }

    // The buttons: a D-pad for show, A (SHOW ME) and B (SKIP), SELECT
    // (MOTION) and START (PLAY), and EXIT on the shell.
    const SDL_Color ink {40, 40, 60, 255};
    const int cy = body.y + 190 * k;
    bugSwat_.dpadRect = SDL_Rect {body.x + 22 * k, cy - 4 * k, 30 * k, 30 * k};
    Primitives::fillRect(controlRenderer_, SDL_Rect {body.x + 22 * k, cy + 6 * k, 30 * k, 10 * k}, ink);
    Primitives::fillRect(controlRenderer_, SDL_Rect {body.x + 32 * k, cy - 4 * k, 10 * k, 30 * k}, ink);
    const SDL_Color berry {150, 30, 80, 255};
    bugSwat_.skipRect = SDL_Rect {body.x + 124 * k, cy + 6 * k, 22 * k, 16 * k};
    bugSwat_.showMeRect = SDL_Rect {body.x + 154 * k, cy - 4 * k, 22 * k, 16 * k};
    const bool canShow = bugSwat_.phase == BugSwatState::Phase::Brief;
    Primitives::fillRect(controlRenderer_, bugSwat_.skipRect, berry);
    Primitives::fillRect(controlRenderer_, bugSwat_.showMeRect, canShow ? berry : shellDark);
    drawCenteredTextSafe(controlRenderer_, pixelFont, bugSwat_.skipRect, "B", SDL_Color {240, 220, 230, 255});
    drawCenteredTextSafe(controlRenderer_, pixelFont, bugSwat_.showMeRect, "A", SDL_Color {240, 220, 230, 255});
    drawCenteredTextSafe(controlRenderer_, pixelFont,
                         SDL_Rect {bugSwat_.skipRect.x - 4 * k, bugSwat_.skipRect.y + 17 * k, 30 * k, 8 * k}, "SKIP", ink);
    drawCenteredTextSafe(controlRenderer_, pixelFont,
                         SDL_Rect {bugSwat_.showMeRect.x - 6 * k, bugSwat_.showMeRect.y + 17 * k, 34 * k, 8 * k},
                         "SHOW ME", ink);
    bugSwat_.selectRect = SDL_Rect {body.x + 64 * k, body.y + 226 * k, 30 * k, 7 * k};
    bugSwat_.startRect = SDL_Rect {body.x + 104 * k, body.y + 226 * k, 30 * k, 7 * k};
    const bool startLit = bugSwat_.phase == BugSwatState::Phase::Over ||
                          (bugSwat_.phase == BugSwatState::Phase::Brief && bugSwatRoundReady(bugSwat_.round));
    Primitives::fillRect(controlRenderer_, bugSwat_.selectRect, ink);
    Primitives::fillRect(controlRenderer_, bugSwat_.startRect,
                         startLit && ((now / 400) % 2) ? SDL_Color {200, 160, 0, 255} : ink);
    drawCenteredTextSafe(controlRenderer_, pixelFont,
                         SDL_Rect {bugSwat_.selectRect.x - 6 * k, bugSwat_.selectRect.y + 8 * k, 42 * k, 8 * k},
                         "SELECT: MOTION", ink);
    drawCenteredTextSafe(controlRenderer_, pixelFont,
                         SDL_Rect {bugSwat_.startRect.x - 6 * k, bugSwat_.startRect.y + 8 * k, 42 * k, 8 * k},
                         bugSwat_.phase == BugSwatState::Phase::Over ? "START: AGAIN" : "START: PLAY", ink);
    bugSwat_.exitRect = SDL_Rect {body.x + body.w - 46 * k, body.y + 2 * k, 44 * k, 7 * k};
    drawCenteredTextSafe(controlRenderer_, pixelFont, bugSwat_.exitRect, "EXIT  Esc", ink);
  }

  // ── BUGSWAT over the wire ────────────────────────────────────────────────
  // BUGSWAT [START|SHOWME|SKIP|PLAY|EXIT|STATUS]; bare BUGSWAT starts it.
  // SHOWME PAW uses the waving paw even where there is a camera (the check).
  void handleBugSwatCommand(const std::vector<std::string>& parts) {
    const std::string arg = parts.size() > 1 ? toUpper(parts[1]) : std::string("START");
    auto needActive = [&]() {
      if (bugSwat_.active) return true;
      failRemoteCommand("BUGSWAT: not running");
      return false;
    };
    if (arg == "START") {
      std::string why;
      if (!beginBugSwat(&why)) {
        failRemoteCommand("BUGSWAT: " + why);
        return;
      }
    } else if (arg == "SHOWME" || arg == "SHOW") {
      if (!needActive()) return;
      if (bugSwat_.phase != BugSwatState::Phase::Brief) {
        failRemoteCommand("BUGSWAT SHOWME: only in a briefing");
        return;
      }
      bugSwatShowMe(parts.size() > 2 && toUpper(parts[2]) == "PAW");
    } else if (arg == "SKIP") {
      if (!needActive()) return;
      bugSwatSkip();
    } else if (arg == "PLAY") {
      if (!needActive()) return;
      if (bugSwat_.phase == BugSwatState::Phase::Brief && !bugSwatRoundReady(bugSwat_.round)) {
        failRemoteCommand(std::string("BUGSWAT PLAY: not set yet: ") + bugSwatBrief(bugSwat_.round).job);
        return;
      }
      bugSwatStart();
    } else if (arg == "EXIT" || arg == "CLOSE") {
      if (!needActive()) return;
      endBugSwat();
    } else if (arg != "STATUS") {
      failRemoteCommand("BUGSWAT: expected START, SHOWME, SKIP, PLAY, EXIT or STATUS");
      return;
    }
    if (!bugSwat_.active) {
      remoteCommandDetail_ = "off high=" + std::to_string(readBugSwatHighScore());
      return;
    }
    const auto& g = bugSwat_.game;
    const char* phase = bugSwat_.phase == BugSwatState::Phase::Brief ? "brief"
                      : bugSwat_.phase == BugSwatState::Phase::Play ? "play" : "over";
    int live = 0;
    for (const auto& b : g.bugs()) live += (!b.fixed && !b.escaped) ? 1 : 0;
    remoteCommandDetail_ = std::string("phase=") + phase + " round=" + std::to_string(bugSwat_.round + 1) +
                           " ready=" + (bugSwatRoundReady(bugSwat_.round) ? "1" : "0") +
                           " score=" + std::to_string(g.score()) + " high=" + std::to_string(bugSwat_.highScore) +
                           " fixed=" + std::to_string(g.fixedThisRound()) + "/" + std::to_string(g.toFixThisRound()) +
                           " bugs=" + std::to_string(live) + " escaped=" + std::to_string(g.escaped()) +
                           " time=" + std::to_string(static_cast<int>(g.roundTime())) +
                           " motion_frames=" + std::to_string(motionFramesRead_) +
                           " screen=" + std::to_string(bugSwat_.screenRect.x) + "," +
                           std::to_string(bugSwat_.screenRect.y) + "," + std::to_string(bugSwat_.screenRect.w) + "," +
                           std::to_string(bugSwat_.screenRect.h) +
                           (SDL_GetTicks() < bugSwat_.lineUntilMs ? " says: " + bugSwat_.remark : std::string());
  }
