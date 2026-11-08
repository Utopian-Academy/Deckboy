// ============================================================================
// LEARN THE BASICS, PART TWO: the lessons.
//
// James, 2026-11-05, tutorials board Cihkd2Se ("i dig"; tracks "perf"). At
// the end of part one the operator chooses whom to follow -- Dr. Pixel into
// running the show, or Prof. Video into the signal -- and picks lessons from
// a list that ticks off the ones done. The choice is never final.
//
// A lesson runs on part one's own card (app_tutorial.ipp: the dim with a hole
// round the control, SHOW ME / SKIP / EXIT, resumable), in a practice show of
// its own, so it never touches the operator's. Each step is data: its lines,
// what the pointer points at, what counts as done, and what SHOW ME does.
// Some lessons run inside a small show with a guest -- original characters
// in a genre's spirit, never copies.
//
// Built so far: the four lessons for what was new (Patterns/synths/Draw,
// Masks, Warp/blend/slices, Multiview), Effects (the art school crit), and
// Bug Swat. The list shows only
// lessons that exist.
//
// Part of class App -- included inside the class body in main.cpp.
// ============================================================================

  struct LessonStep {
    const char* title;
    const char* key;          // key chip, or nullptr
    const char* pixel;
    const char* video;
    const char* guestName;    // nullptr for none
    const char* guest;
    std::function<bool()> done;      // null: only START / SHOW ME moves on
    std::function<void()> showMe;    // null: SHOW ME moves on
    std::function<SDL_Rect()> target;
    std::function<void()> enter;     // on arriving at the step
    // A settings control the step needs that may be below the page's fold.
    // A button registers only while it is on screen, so the page is paged
    // down until it does.
    int seekSetting = 0;
  };

  struct LessonDef {
    const char* id;
    bool videoTrack;          // Prof. Video's (the signal) or Dr. Pixel's (running the show)
    const char* title;
    const char* about;        // one line in the list
    const char* show;         // the practice show's story, or "" for a plain lesson
    bool game = false;        // Bug Swat: the list starts the game instead
    std::function<void()> setup;   // builds the practice show the first time
    std::vector<LessonStep> steps;
  };

  std::vector<LessonDef> lessons_;
  bool lessonsListOpen_ = false;
  bool lessonsTrackVideo_ = true;    // whom the operator follows: the list's open column
  int lessonCount_ = 0;              // a step's baseline, set by its enter
  SDL_Rect lessonsCardRect_ {}, lessonsPixelTabRect_ {}, lessonsVideoTabRect_ {}, lessonsBasicsRect_ {},
           lessonsCloseRect_ {};
  std::vector<std::pair<SDL_Rect, int>> lessonsRowRects_;
  // SUPER DECKBOY during a lesson (James, 2026-11-05: "Make it a joke
  // instead" ... "It IS part of deckboy"): the power-up fires when a
  // practice show gets its second playlist, and the teachers celebrate it in
  // the card's header -- it is Deckboy, not a glitch to apologise for.
  Uint64 lessonEvolveSeen_ = 0;
  Uint64 lessonQuipUntilMs_ = 0;
  int lessonQuips_ = 0;

  std::string lessonQuipNow() const {
    if (!tutorial_.active || tutorial_.lesson <= 0 || SDL_GetTicks() >= lessonQuipUntilMs_) return {};
    static const char* kQuips[] = {
      "DR. PIXEL: Two playlists: Deckboy becomes SUPER DECKBOY. It earned it, skapi.",
      "PROF. VIDEO: It evolved! My oscillators never got a fanfare.",
      "DR. PIXEL: SUPER DECKBOY! Every show deserves one. Bravo.",
      "PROF. VIDEO: The power-up! More playlists, more layers, more super.",
    };
    return kQuips[(lessonQuips_ - 1 + 4) % 4];
  }

  // ── Progress: whom they follow, what is done, where a lesson was left ────

  fs::path lessonsProgressFile() const { return Paths::stateDir() / "lessons.txt"; }
  fs::path lessonPracticeFile(const LessonDef& def) const {
    return Paths::stateDir() / (std::string("lesson-") + def.id + ".deckboy");
  }

  struct LessonsProgress {
    bool trackVideo = true;
    bool trackChosen = false;
    std::set<std::string> done;
    std::map<std::string, int> at;   // lesson id -> step it was left on
  };

  LessonsProgress readLessonsProgress() const {
    LessonsProgress p;
    std::ifstream in(lessonsProgressFile());
    std::string word;
    while (in >> word) {
      if (word == "track") {
        std::string who;
        if (in >> who) {
          p.trackVideo = who != "pixel";
          p.trackChosen = true;
        }
      } else if (word == "done") {
        std::string id;
        if (in >> id) p.done.insert(id);
      } else if (word == "at") {
        std::string id;
        int step = 0;
        if (in >> id >> step) p.at[id] = std::max(0, step);
      }
    }
    return p;
  }

  void writeLessonsProgress(const LessonsProgress& p) const {
    std::error_code ec;
    fs::create_directories(Paths::stateDir(), ec);
    std::ofstream out(lessonsProgressFile(), std::ios::trunc);
    if (!out) return;
    if (p.trackChosen) out << "track " << (p.trackVideo ? "video" : "pixel") << '\n';
    for (const auto& id : p.done) out << "done " << id << '\n';
    for (const auto& [id, step] : p.at) out << "at " << id << ' ' << step << '\n';
  }

  void markLessonDone(const std::string& id) {
    LessonsProgress p = readLessonsProgress();
    p.done.insert(id);
    p.at.erase(id);
    writeLessonsProgress(p);
  }

  void chooseLessonsTrack(bool video) {
    lessonsTrackVideo_ = video;
    LessonsProgress p = readLessonsProgress();
    p.trackVideo = video;
    p.trackChosen = true;
    writeLessonsProgress(p);
  }

  // ── Where things are, for the pointer ────────────────────────────────────

  SDL_Rect lessonQuickRect(QuickAction action, int param = -1) const {
    for (const auto& qb : quickButtons_) {
      if (qb.action == action && qb.rect.w > 0 && (param < 0 || qb.param == param)) return qb.rect;
    }
    return SDL_Rect {};
  }

  SDL_Rect lessonSettingRect(int action) const {
    if (!settingsOpen_) return SDL_Rect {};
    for (const auto& sb : settingsBtns_) {
      if (sb.action == action) return sb.rect;
    }
    return SDL_Rect {};
  }

  int lessonFindCue(const std::function<bool(const Cue&)>& match, int* deckOut = nullptr) const {
    for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
      const auto& cues = project_.decks[static_cast<std::size_t>(d)].cues;
      for (int i = 0; i < static_cast<int>(cues.size()); ++i) {
        if (match(cues[static_cast<std::size_t>(i)])) {
          if (deckOut) *deckOut = d;
          return i;
        }
      }
    }
    return -1;
  }

  int lessonCountCues(const std::function<bool(const Cue&)>& match) const {
    int n = 0;
    for (const Deck& deck : project_.decks) {
      for (const Cue& cue : deck.cues) n += match(cue) ? 1 : 0;
    }
    return n;
  }

  // A matching cue sits in playlist minDeck or later. A new playlist is not
  // the focused one, so a cue added before its tab is clicked lands in
  // playlist 1, and a count over every playlist passed anyway.
  bool lessonCueInDeckFrom(const std::function<bool(const Cue&)>& match, int minDeck) const {
    for (int d = std::max(0, minDeck); d < static_cast<int>(project_.decks.size()); ++d) {
      for (const Cue& cue : project_.decks[static_cast<std::size_t>(d)].cues) {
        if (match(cue)) return true;
      }
    }
    return false;
  }

  // A cue's row, or -- while its playlist is not the one shown -- that
  // playlist's tab: only the selected playlist's list is on screen, so a row
  // in another one has nowhere to be pointed at (multiview's ON SCREEN and
  // the magic show's TA-DA pointed at nothing, 2026-11-07).
  SDL_Rect lessonCueRowRect(int deck, int cue) const {
    const SDL_Rect row = tutorialCueRowRect(deck, cue);
    if (row.w > 0 || deck == project_.focusedDeckIndex) return row;
    return lessonDeckTabRect(deck);
  }

  SDL_Rect lessonDeckTabRect(int deck) const {
    if (deck < 0 || deck >= static_cast<int>(deckTabRects_.size())) return SDL_Rect {};
    return deckTabRects_[static_cast<std::size_t>(deck)];
  }

  // Point at a button while playlist 2 or later is focused; otherwise back at
  // the Deck 2 tab, because the button would add to the wrong playlist.
  SDL_Rect lessonOnSecondDeck(const SDL_Rect& button) const {
    return project_.focusedDeckIndex >= 1 ? button : lessonDeckTabRect(1);
  }

  // The step after the + (James, 2026-11-07: the lessons never said to select
  // the new playlist before adding to it). Same check in every lesson, its
  // own lines in each.
  LessonStep lessonSelectSecondDeckStep(const char* pixel, const char* video,
                                        const char* guestName, const char* guest) {
    return {"SELECT DECK 2", nullptr, pixel, video, guestName, guest,
            [this] { return project_.decks.size() >= 2 && project_.focusedDeckIndex >= 1; },
            [this] {
              if (project_.decks.size() < 2) addDeck();
              setFocusedDeckIndex(1);
            },
            [this] { return lessonDeckTabRect(1); }, nullptr};
  }

  // The cue a match finds is live on its playlist.
  bool lessonCueLive(const std::function<bool(const Cue&)>& match) const {
    for (const Deck& deck : project_.decks) {
      if (deck.activeIndex >= 0 && deck.activeIndex < static_cast<int>(deck.cues.size()) &&
          match(deck.cues[static_cast<std::size_t>(deck.activeIndex)])) {
        return true;
      }
    }
    return false;
  }

  // Point at the cue's row until it is selected, then at TAKE.
  SDL_Rect lessonTakeTarget(const std::function<bool(const Cue&)>& match) const {
    int deck = -1;
    const int index = lessonFindCue(match, &deck);
    if (index < 0) return SDL_Rect {};
    const Deck& d = project_.decks[static_cast<std::size_t>(deck)];
    if (project_.focusedDeckIndex != deck || d.selectedIndex != index) return lessonCueRowRect(deck, index);
    return tutorialButtonRect("TAKE");
  }

  void lessonTake(const std::function<bool(const Cue&)>& match) {
    int deck = -1;
    const int index = lessonFindCue(match, &deck);
    if (index < 0) return;
    project_.focusedDeckIndex = deck;
    selectCueInDeck(deck, index, false, false);
    jumpSelectedCue();
  }

  static bool lessonIsDraw(const Cue& c) {
    return c.kind == CueKind::Pattern && normalizePatternTypeId(c.path) == "draw";
  }
  static bool lessonIsTestPattern(const Cue& c) {
    if (c.kind != CueKind::Pattern) return false;
    const std::string type = normalizePatternTypeId(c.path);
    return type != "draw" && type != "code";
  }
  static bool lessonIsSynth(const Cue& c) { return c.kind == CueKind::VideoSynth; }

  // A code-source cue of the practice show's own, on the focused playlist.
  void lessonAddCodeCue(const char* name, const char* expression) {
    addGeneratorCue("code");
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) return;
    Cue& cue = deck.cues.back();
    cue.name = name;
    cue.codeExpression = expression;
    cue.width = 960;
    cue.height = 540;
    markProjectDirty();
  }

  // ── The guests' faces ────────────────────────────────────────────────────
  //
  // James, 2026-11-05: "they ALL need faces with personality", then "mostly
  // women", "where is the scully?" and "not close enough allusions": each is
  // now an unmistakable parody, original names, no copied lines -- Mr. Salty
  // (the critic Jerry Saltz: "Use jerry", "Mr salty"), Marv (John Madden),
  // Hollis and Brandt (Mulder and Scully), Rook (Willem Dafoe in The
  // Lighthouse), and on the multiview lesson's starship Lt. Haila (Uhura) and
  // Ensign Jinkley (Velma: "Uhura!", "Maybe uhura and velma", "mystery on
  // the enterprise", "jinkys!"). Madden is only in the telestrator booth
  // ("do we NEED a 2nd sports themed thing?"); Donna Wheel (Vanna White),
  // June O'Neil (April O'Neil), Wallis and Grommet (Wallace and Gromit; a
  // grommet is also what a cable goes through), Turnip Top (Carrot Top),
  // Chef Bork (the Swedish Chef), Optimus Primetime (Optimus Prime) and Deft
  // Funk (Daft Punk, who speak only in visor text). Sixteen by
  // twenty, in the teachers' letters and the theme's inks, so they sit on the
  // card beside Dr. Pixel and the Professor on every colourway. A off-white
  // (Rook's beard, Dot's headset), R red and Y yellow are fixed colours, as
  // the teachers' lipstick and sparkle are. Each blinks out of step with the
  // others and talks for a moment when their line arrives.
  struct GuestFace {
    const char* name;
    const char* rows[20];
    int eyeRow;
    int eyeCols[6];
    int mouthRow;
    int mouthCols[4];
  };

  static const GuestFace* guestFace(const char* name) {
    static const GuestFace kFaces[] = {
      {"MR. SALTY",
        {
         "................",
         "....AAAAAAAA....",
         "...AAAAAAAAAA...",
         "...AAAAAAAAAAA..",
         "...ASSSSSSSSAA..",
         "...SSSSSSSSSSS..",
         "...SHHSSSSHHSS..",
         "..GGGGGSSGGGGG..",
         "..GSWEGSSGSWEG..",
         "..GGGGGDDGGGGG..",
         "...SSSSDDSSSSS..",
         "...SSSSSSSSSSS..",
         "....SSMMMMMSS...",
         ".....SSSSSSS....",
         "......SSSSS.....",
         "...KKKCCCCKKK...",
         "..KKKKCCCCKKKK..",
         ".KKKKKCCCCKKKKK.",
         ".KKKKKCCCCKKKKK.",
         ".KKKKKKKKKKKKKK."},
        8, {4, 5, 11, 12, -1, -1}, 12, {7, 8, -1, -1}},
      {"COACH MARV",
        {
         "................",
         "....HHHHHHHH....",
         "...HHHHHHHHHH...",
         "..HHSSSSSSSSHH..",
         "..HSSSSSSSSSSH..",
         "..SSHHHSSHHHSS..",
         "..SSSWESSWESSS..",
         "..SSSSSSSSSSSS..",
         ".SSSSSSDDSSSSSS.",
         ".SSSSSSDDSSSSSS.",
         ".SSSSMMMMMMSSSS.",
         ".SSSSMSSSSMSSSS.",
         ".SSSSSMMMMSSSSS.",
         "..SSSSSSSSSSSS..",
         "...SSSSSSSSSS..Y",
         "..KKKKCCCCKKKKY.",
         ".KKKKKCCCCKKKY..",
         "KKKKKKCRRCKKAKKK",
         "KKKKKKCRRCKKKKKK",
         "KKKKKKKKKKKKKKKK"},
        6, {5, 6, 9, 10, -1, -1}, 11, {6, 7, 8, 9}},
      {"AGENT HOLLIS",
        {
         "................",
         "....HHHHHHHH....",
         "...HHHHHHHHHH...",
         "...HHHHHHH.HHH..",
         "...HHSSSSSSSHH..",
         "...HSSSSSSSSSH..",
         "...SHHSSSSHHSS..",
         "...SWESSSSWESS..",
         "...SSSSDDSSSSS..",
         "...SSSSDDSSSSS..",
         "...SSSSSSSSSSS..",
         "....SSMMMMSSS...",
         ".....SSSSSSS....",
         "......SSSSS.....",
         "...KKKCCRCCKKK..",
         "..KKKKCCYCCKKKK.",
         ".KKKKKCRRCKKKKK.",
         ".KKKKKKCYCKKKKK.",
         ".KKKKKKCRCKKKKK.",
         ".KKKKKKKKKKKKKK."},
        7, {4, 5, 10, 11, -1, -1}, 11, {7, 8, -1, -1}},
      {"AGENT BRANDT",
        {
         "....RRRRRRRR....",
         "...RRRRRRRRRR...",
         "..RRRRRRRRRRRR..",
         "..RRRRSSSSRRRR..",
         "..RRRSSSSSSSRR..",
         "..RRSHHSSHHSRR..",
         "..RRSWESSWESRR..",
         "..RRSSSSSSSSRR..",
         "..RRSSSSDSSSRR..",
         "..RRSSSSSSSSRR..",
         "..RRSSSMMSSSRR..",
         "...RRSSSSSSRR...",
         "...RR.SSSS.RR...",
         "......SSSS......",
         "...KKKCCCCKKK...",
         "..KKKKCCCCKKKK..",
         ".KKKKKKCCKKKKKKY",
         ".KKKKKKKKKKKKAAY",
         ".KKKKKKKKKKKKAA.",
         ".KKKKKKKKKKKKKK."},
        6, {5, 6, 9, 10, -1, -1}, 10, {7, 8, -1, -1}},
      {"KEEPER ROOK",
        {
         "................",
         "....KKKKKKKK....",
         "...KKKKKKKKKK...",
         "..KKKKKKKKKKKK..",
         "..DDDDDDDDDDDD..",
         "...SSSSSSSSSS...",
         "...SHHSSSSHHS...",
         "...SFESSSSEFS...",
         "...SSSSDDSSSS...",
         "..ASSSSDDSSSSA..",
         "..AAASMMMMSAAAK.",
         "..AAAAAAAAAAAKKK",
         "..AAAAAAAAAAAA.Y",
         "...AAAAAAAAAA...",
         "....AAAAAAAA....",
         "..KKKKAAAAKKKK..",
         ".KKKKKKAAKKKKKK.",
         "KKCKKKKKKKKKKCKK",
         "KKKKKKKKKKKKKKKK",
         "KKCKKKKKKKKKKCKK"},
        7, {5, 10, -1, -1, -1, -1}, 10, {6, 7, 8, 9}},
      {"LT. HAILA",
        {
         "....HHHHHHHH....",
         "...HHHHHHHHHH...",
         "..HHHHHHHHHHHH..",
         "..HHHNNNNNNHHH..",
         "..HHNNNNNNNNHH..",
         "..HHNHHNNHHNHH..",
         "..HHNWENNWENHH..",
         "..HYNNNNNNNNYH..",
         "..HYNNNNDNNNNH..",
         "...HNNNNNNNNH...",
         "...HNNNRRNNNH...",
         "....NNNNNNNN....",
         ".....NNNNNN.....",
         "......NNNN......",
         "....KKKKKKKK....",
         "...RRRRRRRRRR...",
         "..RRRRRRRRRYRR..",
         ".RRRRRRRRRYYYRR.",
         ".RRRRRRRRRRRRRR.",
         ".RRRRRRRRRRRRRR."},
        6, {5, 6, 9, 10, -1, -1}, 10, {7, 8, -1, -1}},
      {"ENSIGN JINKLEY",
        {
         "....NNNNNNNN....",
         "...NNNNNNNNNN...",
         "..NNNNNNNNNNNN..",
         "..NNNNNNNNNNNN..",
         "..NNSSSSSSSSNN..",
         "..NGGGGSSGGGGN..",
         "..NGWEGSSGWEGN..",
         "..NGGGGDDGGGGN..",
         "..NNSSSSSSSSNN..",
         "..NNSBSSSSBSNN..",
         "..NNSSSMMSSSNN..",
         "..NNNSSSSSSNNN..",
         "...NN.SSSS.NN...",
         "......SSSS......",
         "....OOOOOOOO....",
         "...UUUOOOOUUU...",
         "..UUUUUUUUUUYU..",
         ".UUUUUUUUUUYYYU.",
         ".UUUUUUUUUUUUUU.",
         ".UUUUUUUUUUUUUU."},
        6, {4, 5, 10, 11, -1, -1}, 10, {7, 8, -1, -1}},
      {"DONNA WHEEL",
        {
         "...YYYYYYYYYY...",
         "..YYYYYYYYYYYY..",
         ".YYYYYYYYYYYYYY.",
         ".YYYSSSSSSSSYYY.",
         ".YYSSSSSSSSSSYY.",
         ".YYSHHSSSSHHSYY.",
         ".YYSWESSSSWESYY.",
         ".YYSSSSSSSSSSYY.",
         ".YYSSSSDDSSSSYY.",
         ".YYSBSSSSSSBSYY.",
         "..YSSMWWWWMSSY..",
         "..YYSSMMMMSSYY..",
         "...YY.SSSS.YY...",
         "......SSSS......",
         "....RYRRRRYR....",
         "...RRRRYRRRRRR..",
         "..RRYRRRRRRYRRR.",
         ".RRRRRRYRRRRRRR.",
         ".RRYRRRRRRRYRRR.",
         ".RRRRRRRRRRRRRR."},
        6, {4, 5, 10, 11, -1, -1}, 11, {7, 8, -1, -1}},
      {"JUNE O'NEIL",
        {
         "....RRRRRRRR....",
         "...RRRRRRRRRR...",
         "..RRRRRRRRRRRR..",
         "..RRRRRRRRRRRR..",
         "..RRSSSSSSSSRR..",
         "..RRSHHSSHHSRR..",
         "..RRSWESSWESRR..",
         "..RRSSSSSSSSRR..",
         "..RRSSSSDSSSRR..",
         "..RRSSSSSSSSRR..",
         "..RRRSSMMSSRRR..",
         "...RRSSSSSSRR...",
         "...RR.SSSS.RR.K.",
         "......SSSS...KK.",
         "....YYYYYYYYKY..",
         "...YYYYYYYYKYY..",
         "..YYYYYYYYYKYYY.",
         ".YYYYYYKKYYYYYY.",
         ".YYYYYYKKYYYYYY.",
         ".YYYYYYYYYYYYYY."},
        6, {5, 6, 9, 10, -1, -1}, 10, {7, 8, -1, -1}},
      {"WALLIS",
        {
         "................",
         ".....SSSSSS.....",
         "...SSSSSSSSSS...",
         "..SSSSSSSSSSSS..",
         ".NSSSSSSSSSSSSN.",
         ".NSSSWESSWESSSN.",
         "..SSSSSSDDSSSS..",
         "..SSSSSSDDSSSS..",
         ".SSSSSSSSSSSSSS.",
         ".SSMMMMMMMMMMSS.",
         ".SSMAAAAAAAAMSS.",
         ".SSSMMMMMMMMSSS.",
         "..SSSSSSSSSSSS..",
         "....SSSSSSSS....",
         "...AAAAARRAAAA..",
         "..CCCCAARRAACCC.",
         ".CCCCCCARRACCCC.",
         "CCCCCCCARRACCCCC",
         "CCCCCCCCRRCCCCCC",
         "CCCCCCCCCCCCCCCC"},
        5, {5, 6, 9, 10, -1, -1}, 10, {6, 7, 8, 9}},
      {"GROMMET",
        {
         "................",
         "..NN........NN..",
         ".NNNN......NNNN.",
         ".NNNSSSSSSSSNNN.",
         ".NNSSSSSSSSSSNN.",
         "..NSHHHHHHHHSN..",
         "..SSWESSSSWESS..",
         "..SSSSSSSSSSSS..",
         "..SSSSSKKSSSSS..",
         "...SSSSKKSSSS...",
         "...SSSSSSSSSS...",
         "....SSSSSSSS....",
         ".....SSSSSS.....",
         "......SSSS......",
         "....SSSSSSSS....",
         "...SSSSSSSSSS...",
         "..SSSSSSSSSSSS..",
         "..SSSSSSSSSSSS..",
         "..SSSSSSSSSSSS..",
         "..SSSSSSSSSSSS.."},
        6, {4, 5, 10, 11, -1, -1}, -1, {-1, -1, -1, -1}},
      {"TURNIP TOP",
        {
         ".OO.OO.OO.OO.OO.",
         "OOOOOOOOOOOOOOOO",
         "OOOOOOOOOOOOOOOO",
         "OOOOSSSSSSSSOOOO",
         "OOOSSSSSSSSSSOOO",
         ".OOSHHSSSSHHSOO.",
         "..OSWESSSSWESO..",
         "...SSSSSSSSSS...",
         "...SSSSDDSSSSS..",
         "...SBSSSSSSBSS..",
         "...SMWWWWWWMSS..",
         "....SMMMMMMSS...",
         ".....SSSSSSS....",
         "......SSSSS.....",
         "...KKKKKKKKKK...",
         "..KKKKKYKKKKKK..",
         ".KKKKKYYYKKKKKK.",
         ".KKKKKKYKKKKKKK.",
         ".KKKKKKKKKKKKKK.",
         ".KKKKKKKKKKKKKK."},
        6, {4, 5, 10, 11, -1, -1}, 10, {6, 7, 8, 9}},
      {"CHEF BORK",
        {
         "....AAAAAAAA....",
         "...AAAAAAAAAA...",
         "...AAAAAAAAAA...",
         "...AAAAAAAAAA...",
         "....AAAAAAAA....",
         "....DDDDDDDD....",
         "...SSSSSSSSSS...",
         "...HHHHSSHHHH...",
         "...HHHHSSHHHH...",
         "...SSSSDDSSSS...",
         "...SSSDDDDSSS...",
         "..HHHHHHHHHHHH..",
         "..HHHHHHHHHHHH..",
         "...SSSSSSSSSS...",
         "....SSSSSSSS....",
         "...AAAARRAAAA...",
         "..AAAAARRAAAAA..",
         ".AAAAAAAAAAAAAA.",
         ".AAAKAAAAAAKAAA.",
         ".AAAAAAAAAAAAAA."},
        -1, {-1, -1, -1, -1, -1, -1}, 12, {6, 7, 8, 9}},
      {"OPTIMUS PRIMETIME",
        {
         "..U..........U..",
         "..UU...UU...UU..",
         "..UUUUUUUUUUUU..",
         ".UUUUUUUUUUUUUU.",
         ".UUAAAAAAAAAAUU.",
         ".UUAYYAAAAYYAUU.",
         ".UUAYYAAAAYYAUU.",
         ".UUAAAAAAAAAAUU.",
         ".UUAAAAKKAAAAUU.",
         ".UUAKAKAAKAKAUU.",
         ".UUAKAKAAKAKAUU.",
         ".UUAKAKAAKAKAUU.",
         "..UUAAAAAAAAUU..",
         "...UUUUUUUUUU...",
         "....RRRRRRRR....",
         "..RRRRRRRRRRRR..",
         ".RRAAARRRRAAARR.",
         ".RRAAARRRRAAARR.",
         ".RRRRRRRRRRRRRR.",
         ".RRRRRRRRRRRRRR."},
        5, {4, 5, 10, 11, -1, -1}, 9, {7, 8, -1, -1}},
      {"DEFT FUNK (SILVER)",
        {
         ".....AAAAAA.....",
         "...AAAAAAAAAA...",
         "..AAAAAAAAAAAA..",
         ".AAAAAAAAAAAAAA.",
         ".AAKKKKKKKKKKAA.",
         ".AKKKKKKKKKKKKA.",
         ".AKKYKYKYKYKKKA.",
         ".AKKKKKKKKKKKKA.",
         ".AAKKKKKKKKKKAA.",
         ".AAAAAAAAAAAAAA.",
         "..AAAAAAAAAAAA..",
         "...AAAAAAAAAA...",
         ".....AAAAAA.....",
         "......AAAA......",
         "....KKKKKKKK....",
         "..KKKKKKKKKKKK..",
         ".KKKKKKAAKKKKKK.",
         ".KKKKKKAAKKKKKK.",
         ".KKKKKKKKKKKKKK.",
         ".KKKKKKKKKKKKKK."},
        -1, {-1, -1, -1, -1, -1, -1}, 6, {4, 6, 8, 10}},
      {"DEFT FUNK (GOLD)",
        {
         ".....YYYYYY.....",
         "...YYYYYYYYYY...",
         "..YYYYYYYYYYYY..",
         ".YYYYYYYYYYYYYY.",
         ".YYKKKKKKKKKKYY.",
         ".YYKKKKKKKKKKYY.",
         ".YYKRKRKRKRKKYY.",
         ".YYKKKKKKKKKKYY.",
         ".YYKKKKKKKKKKYY.",
         ".YYYYYYYYYYYYYY.",
         "..YYYYYYYYYYYY..",
         "...YYYYYYYYYY...",
         ".....YYYYYY.....",
         "......YYYY......",
         "....KKKKKKKK....",
         "..KKKKKKKKKKKK..",
         ".KKKKKKYYKKKKKK.",
         ".KKKKKKYYKKKKKK.",
         ".KKKKKKKKKKKKKK.",
         ".KKKKKKKKKKKKKK."},
        -1, {-1, -1, -1, -1, -1, -1}, 6, {4, 6, 8, 10}},
    };
    if (!name) return nullptr;
    for (const GuestFace& face : kFaces) {
      if (std::strcmp(face.name, name) == 0) return &face;
    }
    return nullptr;
  }

  std::string guestLineSeen_;
  Uint64 guestTalkUntilMs_ = 0;
  Uint64 guestTalkFromMs_ = 0;      // the guest waits for the teacher to finish
  std::vector<DmgNote> guestPendingNotes_;   // the guest's line, held until the teacher stops
  Uint64 guestPendingAtMs_ = 0;

  // False when there is no face for that name (the card then draws none).
  bool drawGuestPortrait(const SDL_Rect& box, const char* name, bool speaking) {
    const GuestFace* face = guestFace(name);
    if (!face) return false;
    char rows[20][17];
    for (int r = 0; r < 20; ++r) std::memcpy(rows[r], face->rows[r], 17);
    const std::size_t seed = std::hash<std::string> {}(name);
    const bool blinking = (animationNow_ + seed % 3100) % 4300 < 140;
    const bool mouthOpen = speaking && ((animationNow_ / 110) % 2) == 0;
    if (blinking && face->eyeRow >= 0 && face->eyeRow < 20) {
      for (int c : face->eyeCols) {
        if (c >= 0 && c < 16 && rows[face->eyeRow][c] != '.') rows[face->eyeRow][c] = 'F';
      }
    }
    if (mouthOpen && face->mouthRow >= 0 && face->mouthRow < 20) {
      for (int c : face->mouthCols) {
        if (c >= 0 && c < 16) rows[face->mouthRow][c] = 'D';
      }
    }
    Primitives::fillRect(controlRenderer_, box, speaking ? pal.mid : pal.dark);
    const int cell = std::max(1, std::min(box.w / 16, box.h / 20));
    const int ox = box.x + (box.w - cell * 16) / 2;
    const int bob = speaking ? static_cast<int>((animationNow_ / 260) % 2) : 0;
    const int oy = box.y + (box.h - cell * 20) / 2 - bob;
    for (int r = 0; r < 20; ++r) {
      for (int c = 0; c < 16; ++c) {
        SDL_Color color;
        switch (rows[r][c]) {
          case 'H': case 'G': case 'M': case 'E': case 'K': color = pal.deep; break;
          case 'S': case 'W': case 'C': color = pal.light; break;
          case 'D': case 'F': color = pal.dark; break;
          case 'R': color = SDL_Color {200, 36, 60, 255}; break;
          case 'Y': color = SDL_Color {255, 220, 0, 255}; break;
          case 'B': color = SDL_Color {255, 143, 168, 255}; break;
          case 'A': color = SDL_Color {232, 232, 220, 255}; break;
          case 'N': color = SDL_Color {150, 98, 66, 255}; break;    // Haila's skin, Jinkley's hair
          case 'O': color = SDL_Color {236, 128, 32, 255}; break;   // Jinkley's turtleneck
          case 'U': color = SDL_Color {50, 100, 210, 255}; break;   // Jinkley's science tunic
          default: continue;
        }
        Primitives::fillRect(controlRenderer_, SDL_Rect {ox + c * cell, oy + r * cell, cell, cell}, color);
      }
    }
    return true;
  }

  // ── The lessons ──────────────────────────────────────────────────────────

  const std::vector<LessonDef>& lessonDefs() {
    if (lessons_.empty()) buildLessons();
    return lessons_;
  }
  const LessonDef* lessonDef(int index) const {
    auto& self = const_cast<App&>(*this);
    const auto& defs = self.lessonDefs();
    return index >= 0 && index < static_cast<int>(defs.size()) ? &defs[static_cast<std::size_t>(index)] : nullptr;
  }
  int lessonIndex(const std::string& id) {
    const auto& defs = lessonDefs();
    for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
      if (id == defs[static_cast<std::size_t>(i)].id) return i;
    }
    return -1;
  }

  int lessonStepCount(int index) const {
    const LessonDef* def = lessonDef(index);
    return def ? std::max(1, static_cast<int>(def->steps.size())) : 1;
  }

  TutorialLine lessonLine(int index, int step) const {
    const LessonDef* def = lessonDef(index);
    if (!def || def->steps.empty()) return TutorialLine {"", nullptr, "", ""};
    const LessonStep& s = def->steps[static_cast<std::size_t>(std::clamp(step, 0, static_cast<int>(def->steps.size()) - 1))];
    return TutorialLine {s.title, s.key, s.pixel, s.video, s.guestName, s.guest};
  }

  const LessonStep* currentLessonStep() const {
    const LessonDef* def = lessonDef(tutorial_.lesson - 1);
    if (!def || tutorial_.step < 0 || tutorial_.step >= static_cast<int>(def->steps.size())) return nullptr;
    return &def->steps[static_cast<std::size_t>(tutorial_.step)];
  }

  void buildLessons() {
    lessons_.clear();
    auto none = std::function<bool()> {};
    auto noTarget = [] { return SDL_Rect {}; };

    // ── Dr. Pixel: Taking it on the road (a plain lesson) ─────────────────
    // CHECK only appears when a show has a problem, so this one starts with
    // one: a video left on another computer.
    {
      LessonDef l;
      l.id = "road";
      l.videoTrack = false;
      l.title = "Taking it on the road";
      l.about = "CHECK a show before it travels, fix what it finds, and bundle it with its media.";
      l.show = "";
      l.setup = [this] {
        addGeneratorCue("swirl");
        Deck& deck = focusedDeckMutable();
        if (!deck.cues.empty()) deck.cues.back().name = "Opener";
        Cue lost;
        lost.kind = CueKind::Video;
        lost.name = "Intro video (left on the office PC)";
        lost.path = (Paths::stateDir() / "lesson-road" / "intro-video-on-another-computer.mp4").string();
        lost.formatName = "mp4";
        applyDeckDefaultsToCue(lost, deck);
        deck.cues.push_back(lost);
        scanProjectMediaPresence();
        markProjectDirty();
      };
      auto isLost = [](const Cue& c) { return c.name == "Intro video (left on the office PC)"; };
      l.steps = {
        {"ON THE ROAD", nullptr,
         "A show you built here, skapi, running on a laptop somewhere else tonight. Press START.",
         "Before it travels: check it, fix it, bundle it.",
         nullptr, nullptr, std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
        {"CHECK", nullptr,
         "See CHECK in the top bar? It only appears when something is wrong. Press it.",
         "It lists everything a show would trip over: missing files, empty cues, unreachable outputs.",
         nullptr, nullptr,
         [this] { return showProblemListOpens_ > lessonCount_; },
         [this] { openShowProblemList(); },
         [this] { return fileCheckBtnRect_; },
         [this] {
           lessonCount_ = showProblemListOpens_;
           lastShowCheckMs_ = 0;   // CHECK appears now, not on the next rescan
         }},
        {"FIX IT", "DEL",
         "The intro video is on another computer. Select it and delete it, or RELINK it if you have the file.",
         "Fix it at home, where it is easy. Not at the venue, where it is not.",
         nullptr, nullptr,
         [this, isLost] {
           int d = -1;
           const int i = lessonFindCue(isLost, &d);
           return i < 0 || !project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].mediaMissing;
         },
         [this, isLost] {
           int d = -1;
           const int i = lessonFindCue(isLost, &d);
           if (i < 0) return;
           project_.focusedDeckIndex = d;
           selectCueInDeck(d, i, false, false);
           deleteSelected();
         },
         [this, isLost] {
           int d = -1;
           const int i = lessonFindCue(isLost, &d);
           return i < 0 ? SDL_Rect {} : lessonCueRowRect(d, i);
         }, nullptr},
        {"BUNDLE IT", nullptr,
         "Press BUNDLE and choose where: a USB stick, a shared folder. The show and all its media, together.",
         "The copy finds its media beside it, wherever the folder goes.",
         nullptr, nullptr,
         [this] { return bundlesWritten_ > lessonCount_; },
         [this] {
           const fs::path dir = Paths::stateDir() / "lesson-road-bundle";
           std::error_code ec;
           fs::create_directories(dir, ec);
           exportProjectBundleTo(dir / "on-the-road.deckboy", true);
         },
         [this] { return fileBundleBtnRect_; },
         [this] { lessonCount_ = bundlesWritten_; }},
        {"SAFE TRAVELS", nullptr,
         "Checked, fixed, bundled. On the other machine, open the bundle's show: everything is where it should be.",
         "A show file is only text, so it travels light. The media is the heavy part.",
         nullptr, nullptr, std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Prof. Video: Effects, oscillators and colour (the art school crit) ─
    // James picked this show, 2026-11-05: the critic hates every picture
    // until it has a ~ on it. The critic is Mr. Salty (Jerry Saltz).
    {
      LessonDef l;
      l.id = "effects";
      l.videoTrack = true;
      l.title = "Effects, oscillators and colour";
      l.about = "An effect, a ~ on its knob, and the colour picker.";
      l.show = "The art school crit, with Mr. Salty";
      l.setup = [this] {
        lessonAddCodeCue("Student work no. 4 (untitled)",
                         "sq = (1 - step(0.18, abs(x - 0.5)*1.78))*(1 - step(0.18, abs(y - 0.5)));"
                         "0.86 - sq*0.36, 0.80 - sq*0.30, 0.68 - sq*0.18");
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        jumpSelectedCue();
      };
      auto selected = [this]() -> Cue* { return selectedCueMutable(); };
      auto anyEffect = [this] {
        const Cue* cue = selectedCueMutable();
        if (!cue) return false;
        for (const auto& fx : cue->effects) {
          if (fx.kind != deckboy::effects::CueEffectKind::None) return true;
        }
        return false;
      };
      auto openEffects = [this] {
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        openInspectorSection(QuickAction::CueSectionEffectsToggle);
      };
      l.steps = {
        {"THE CRIT", nullptr,
         "An art school, skapi. One student, one picture, one critic who has seen it all. Press START.",
         "Effects change a picture; an oscillator keeps changing it. The critic only likes the second kind.",
         "MR. SALTY", "A grey square on beige. I have seen this at every graduate show since 1987.",
         std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
        {"AN EFFECT", nullptr,
         "In the inspector's EFFECTS section, + adds one. Edges is a good start: it draws the outlines.",
         "There are dozens. Every one runs on the CPU in a few milliseconds. Mostly.",
         "MR. SALTY", "Take a risk! Bad art is fine. Boring art is a crime.",
         anyEffect,
         [this] { handleRemoteCommand("FX ADD edges 1"); },
         [this] { return lessonQuickRect(QuickAction::EffectAdd); }, openEffects},
        {"A ~ ON ITS KNOB", nullptr,
         "Every control with a ~ beside it can follow an oscillator. Press one. The knob starts to breathe.",
         "A low-frequency oscillator: slow, so the eye can follow it. A quarter of a hertz is a four-second breath.",
         "MR. SALTY", "Now it MOVES. Okay. Okay. I'm listening.",
         [selected] {
           const Cue* cue = selected();
           if (!cue) return false;
           for (const auto& fx : cue->effects) {
             for (const auto& lfo : fx.lfo) {
               if (lfo.on) return true;
             }
           }
           return false;
         },
         [this] {
           const Cue* cue = selectedCueMutable();
           if (!cue || cue->effects.empty()) handleRemoteCommand("FX ADD edges 1");
           handleRemoteCommand("FX LFO 1 A ON");
         },
         [this] { return lessonQuickRect(QuickAction::EffectLfoToggle); }, openEffects},
        {"A COLOUR OF ITS OWN", nullptr,
         "Effects that draw a colour, like Edges' line, have a colour chip. It opens the picker: a wheel, typed names, or a colour off the screen.",
         "Every colour has a name somewhere. Mine is 'oscilloscope green'. Nobody else uses it.",
         "MR. SALTY", "That colour is doing something. I don't know what. I love it.",
         [selected] {
           const Cue* cue = selected();
           if (!cue) return false;
           for (const auto& fx : cue->effects) {
             if (deckboy::effects::cueEffectColourSlot(fx.kind) >= 0 && fx.colour != 0xFFFFFFu) return true;
           }
           return false;
         },
         [this] {
           Cue* cue = selectedCueMutable();
           int at = -1;
           if (cue) {
             for (int i = 0; i < static_cast<int>(cue->effects.size()); ++i) {
               if (deckboy::effects::cueEffectColourSlot(cue->effects[static_cast<std::size_t>(i)].kind) >= 0) at = i;
             }
           }
           if (at < 0) {
             handleRemoteCommand("FX ADD edges 1");
             cue = selectedCueMutable();
             at = cue ? static_cast<int>(cue->effects.size()) - 1 : -1;
           }
           if (at >= 0) handleRemoteCommand("FX COLOUR " + std::to_string(at + 1) + " #ff3c8c");
         },
         [this] { return lessonQuickRect(QuickAction::EffectColourPick); }, openEffects},
        {"THE VERDICT", nullptr,
         "An effect, a ~ and a colour. The critic is crying, skapi. Good crying, I think.",
         "Stack a few. Each has its own ~ on every knob. That is how a look becomes a performance.",
         "MR. SALTY", "It's still a square. But it's YOUR square. Post it.",
         std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Prof. Video: Patterns, synths and Draw (the telestrator booth) ────
    {
      LessonDef l;
      l.id = "draw";
      l.videoTrack = true;
      l.title = "Patterns, synths and Draw";
      l.about = "A test pattern, the video synth, and drawing on the picture, saved as a still.";
      l.show = "The telestrator booth, with Coach Marv";
      l.setup = [] {};
      l.steps = {
        {"THE TELESTRATOR BOOTH", nullptr,
         "Coach Marv explains sport by drawing circles on it. Once he circled the sun. Ten minutes, skapi.",
         "Tonight: test patterns, the video synth, and drawing on the picture. Press START.",
         "COACH MARV", "Get me a picture and a pen. BOOM.", none, nullptr, noTarget, nullptr},
        {"A TEST PATTERN", nullptr,
         "Line up the screens BEFORE the show. Not during. I learned this in Plovdiv.",
         "Every desk needs a test pattern. PATTERN adds Pocket Test: checked against real meters.",
         nullptr, nullptr,
         [this] { return lessonCountCues(lessonIsTestPattern) > 0; },
         [this] { addKawaiiPatternCue(); },
         [this] { return tutorialButtonRect("PATTERN"); }, nullptr},
        {"A VIDEO SYNTH", nullptr,
         "He says 'no camera needed' like it is a feature. It is a feature.",
         "My favourite. SOURCE, then Video Synth: oscillators, mirrors and feedback. No camera needed.",
         nullptr, nullptr,
         [this] { return lessonCountCues(lessonIsSynth) > 0; },
         [this] { addVideoSynthCue(); },
         [this] { return tutorialButtonRect("SOURCE"); }, nullptr},
        {"TAKE THE SYNTH", "ENTER",
         "Select it, then TAKE. Confident operators press Enter.",
         "That's our replay. Every replay needs a pundit.",
         "COACH MARV", "Now here's a replay. And here's a guy. And BOOM, he's in it.",
         [this] { return lessonCueLive(lessonIsSynth); },
         [this] { lessonTake(lessonIsSynth); },
         [this] { return lessonTakeTarget(lessonIsSynth); }, nullptr},
        {"A SECOND PLAYLIST", nullptr,
         "Ink goes on top, on its own layer. Press the + at the end of the playlist tabs.",
         "A second playlist is a layer over the first. Like a second oscillator, mixed on top.",
         nullptr, nullptr,
         [this] { return project_.decks.size() >= 2; },
         [this] { addDeck(); },
         [this] { return deckAddTabRect_; }, nullptr},
        lessonSelectSecondDeckStep(
          "Now click its tab, Deck 2. A new playlist is not the selected one until you click it.",
          "The lit tab is where SOURCE puts things. Skip this and the ink lands under the synth.",
          "COACH MARV", "See that tab? Click it. Now you're on Deck 2. BOOM. Fundamentals."),
        {"A DRAW CUE", nullptr,
         "In the new playlist: SOURCE, then Draw. Clear everywhere you have not drawn.",
         "A Draw cue is transparent outside the ink, so the synth shows through.",
         nullptr, nullptr,
         [this] { return lessonCueInDeckFrom(lessonIsDraw, 1); },
         [this] {
           if (project_.decks.size() < 2) addDeck();
           setFocusedDeckIndex(1);
           addGeneratorCue("draw");
         },
         [this] { return lessonOnSecondDeck(tutorialButtonRect("SOURCE")); }, nullptr},
        {"TAKE THE INK", "ENTER",
         "Select the Draw cue and TAKE it. Two playlists, two layers, one picture.",
         "Now both are live: the synth underneath, the ink on top.",
         nullptr, nullptr,
         [this] { return lessonCueLive(lessonIsDraw); },
         [this] { lessonTake(lessonIsDraw); },
         [this] { return lessonTakeTarget(lessonIsDraw); }, nullptr},
        {"CIRCLE SOMETHING", nullptr,
         "Press DRAW in the inspector, then draw on the programme monitor. Mouse, or a pen if you have one.",
         "A pen's pressure makes the line thicker. A mouse presses at one strength. Like me.",
         "COACH MARV", "Circle him! No, bigger. BOOM. That's a cloud. Fine. Circle the cloud.",
         [this] {
           int d = -1;
           const int i = lessonFindCue(lessonIsDraw, &d);
           if (i < 0) return false;
           for (const auto& frame : project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].drawing.frames) {
             if (!frame.strokes.empty()) return true;
           }
           return false;
         },
         [this] {
           int d = -1;
           const int i = lessonFindCue(lessonIsDraw, &d);
           if (i < 0) return;
           project_.focusedDeckIndex = d;
           selectCueInDeck(d, i, false, false);
           openDrawEditor(true);
           Cue& cue = project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)];
           deckboy::draw::Stroke stroke;
           stroke.rgba = 0xFFDC00FFu;
           stroke.size = 0.014f;
           for (int k = 0; k <= 48; ++k) {
             const double a = k / 48.0 * 6.283185307;
             stroke.points.push_back({static_cast<float>(0.5 + 0.13 * std::cos(a)),
                                      static_cast<float>(0.5 + 0.22 * std::sin(a)), 1.0f, k * 0.02f});
           }
           auto& frames = cue.drawing.frames;
           if (frames.empty()) frames.emplace_back();
           frames[static_cast<std::size_t>(std::clamp(cue.drawing.editFrame, 0, static_cast<int>(frames.size()) - 1))]
             .strokes.push_back(stroke);
           ++cue.drawing.revision;
           markProjectDirty();
         },
         [this] { return drawEditor_.open ? SDL_Rect {} : lessonQuickRect(QuickAction::DrawEditToggle); },
         [this] { openInspectorSection(QuickAction::CueSectionDrawToggle); }},
        {"SAVE A STILL", nullptr,
         "SAVE STILL keeps the drawing as a picture with a clear background, and adds it to the playlist.",
         "For next week's show. Coach Marv will want the cloud again.",
         nullptr, nullptr,
         [this] { return lessonCountCues([](const Cue& c) { return c.kind == CueKind::Image; }) > lessonCount_; },
         [this] {
           int d = -1;
           const int i = lessonFindCue(lessonIsDraw, &d);
           if (i < 0) return;
           project_.focusedDeckIndex = d;
           selectCueInDeck(d, i, false, false);
           handleDrawQuickAction(QuickAction::DrawSaveStill, -1);
         },
         [this] { return lessonQuickRect(QuickAction::DrawSaveStill); },
         [this] {
           lessonCount_ = lessonCountCues([](const Cue& c) { return c.kind == CueKind::Image; });
           openInspectorSection(QuickAction::CueSectionDrawToggle);
         }},
        {"THAT'S THE BOOTH", nullptr,
         "Patterns, the synth, a layer, ink, a still. Nazdrave, skapi.",
         "Try Replay in the Draw section: it draws your lines back the way you drew them.",
         "COACH MARV", "Same time next week. Bring a bigger pen. And a turducken.", none, nullptr, noTarget, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Prof. Video: Masks (The Signal Files) ─────────────────────────────
    {
      LessonDef l;
      l.id = "masks";
      l.videoTrack = true;
      l.title = "Masks";
      l.about = "A heart from the library, the pen, invert and feather.";
      l.show = "The Signal Files: redact the evidence";
      l.setup = [this] {
        lessonAddCodeCue("Saucer photo (CLASSIFIED)",
                         "sx = (x - 0.62)*1.78; sy = y - 0.4;"
                         "d = sqrt(sx*sx/0.04 + sy*sy/0.004);"
                         "dome = sqrt(sx*sx + (sy + 0.035)*(sy + 0.035));"
                         "s = 1 - smoothstep(0.7, 1.3, d); g = 1 - smoothstep(0.03, 0.09, dome);"
                         "lights = s*(0.5 + 0.5*sin(x*90 + t*4)); sky = 0.08 + y*0.25;"
                         "c = max(s*0.75, g*0.6);"
                         "sky*0.6 + c + lights*0.3, sky*0.8 + c*0.95, sky*1.3 + c*0.8");
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        jumpSelectedCue();
      };
      l.steps = {
        {"THE SIGNAL FILES", nullptr,
         "The Bureau of Unexplained Signals. Two agents, one photo, and it must not air. Press START.",
         "Masks cut a picture into shapes. Tonight we cut evidence.",
         "AGENT HOLLIS", "That's a craft, Brandt. Look at the lights. I want to believe.", none, nullptr, noTarget, nullptr},
        {"A MASK FROM THE LIBRARY", nullptr,
         "In the inspector's MASK section: mask, then 'new heart'. Any shape will do. The heart is funnier.",
         "The library has circles, stars, hearts. Each one is points you can move.",
         "AGENT BRANDT", "A heart, Hollis? The Bureau redacts with black bars.",
         [this] {
           const MaskModel* m = maskFocus();
           if (!m) return false;
           for (const auto& path : m->paths) {
             if (path.name == deckboy::masks::maskShapeLabel(deckboy::masks::MaskShape::Heart)) return true;
           }
           return !m->paths.empty();
         },
         [this] { newMaskOnTarget(deckboy::masks::MaskShape::Heart); },
         [this] { return lessonQuickRect(QuickAction::MaskPick); },
         [this] {
           selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
           openInspectorSection(QuickAction::CueSectionMaskToggle);
         }},
        {"THE PEN", "P",
         "Set the tool to pen, then click round the saucer on the monitor. Click the first point to close it.",
         "Click for corners, drag for curves. The way the Bezier brothers intended.",
         "AGENT HOLLIS", "Draw round it carefully. It might be watching.",
         [this] {
           const MaskModel* m = maskFocus();
           if (!m) return false;
           for (const auto& path : m->paths) {
             if (path.closed && path.name.rfind("pen", 0) == 0) return true;
           }
           return false;
         },
         [this] {
           MaskModel* m = maskFocus();
           if (!m) m = newMaskOnTarget(deckboy::masks::MaskShape::Heart);
           if (!m) return;
           pushUndoSnapshot();
           deckboy::masks::MaskPath path;
           path.name = "pen path";
           path.points = {{0.50f, 0.30f}, {0.75f, 0.30f}, {0.78f, 0.49f}, {0.48f, 0.49f}};
           m->paths.push_back(path);
           markProjectDirty();
         },
         [this] {
           return maskEditor_.tool == MaskTool::Pen && maskEditor_.open ? SDL_Rect {}
                                                                         : lessonQuickRect(QuickAction::MaskToolCycle);
         },
         [this] { openInspectorSection(QuickAction::CueSectionMaskToggle); }},
        {"INVERT", nullptr,
         "Invert keeps the outside and cuts the inside. The saucer goes. The sky stays.",
         "The same shape, the other way round. Very useful. Slightly spooky.",
         "AGENT BRANDT", "Hollis, there's a heart-shaped hole in the sky.",
         [this] {
           const MaskModel* m = maskFocus();
           return m && m->invert;
         },
         [this] {
           if (MaskModel* m = maskFocus()) {
             pushUndoSnapshot();
             m->invert = true;
             markProjectDirty();
           }
         },
         [this] { return lessonQuickRect(QuickAction::MaskInvertToggle); },
         [this] { openInspectorSection(QuickAction::CueSectionMaskToggle); }},
        {"FEATHER", nullptr,
         "Feather softens the edge, so nobody sees the cut. Press + on feather.",
         "A hard edge says 'someone was here'. A soft one says nothing at all.",
         "AGENT HOLLIS", "They'll never know.",
         [this] {
           const MaskModel* m = maskFocus();
           return m && m->feather > 0.004f;
         },
         [this] {
           if (MaskModel* m = maskFocus()) {
             pushUndoSnapshot();
             m->feather = 0.04f;
             markProjectDirty();
           }
         },
         [this] {
           const SDL_Rect inc = lessonQuickRect(QuickAction::NumericParamInc, static_cast<int>(NumericParam::MaskFeather));
           return inc.w > 0 ? inc : lessonQuickRect(QuickAction::EditNumericParam, static_cast<int>(NumericParam::MaskFeather));
         },
         [this] { openInspectorSection(QuickAction::CueSectionMaskToggle); }},
        {"CASE CLOSED", nullptr,
         "A library shape, the pen, invert and feather. The Bureau thanks you. The Bureau was never here.",
         "Masks also go on a layer, an output, or its matte: MASK ON chooses. And every control takes a ~.",
         "AGENT BRANDT", "The truth is in the waveform, Hollis. Next time, the scopes.", none, nullptr, noTarget, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Prof. Video: Warp, blend and slices (the haunted lighthouse) ──────
    {
      LessonDef l;
      l.id = "warp";
      l.videoTrack = true;
      l.title = "Warp, blend and slices";
      l.about = "A corner pin, two soft edges, then three slices in the Screens window.";
      l.show = "The haunted lighthouse, with Keeper Rook";
      l.setup = [this] {
        lessonAddCodeCue("The ghost",
                         "gx = (x - 0.5)*1.78; gy = y - 0.5 + 0.03*sin(t*1.3);"
                         "body = 1 - smoothstep(0.16, 0.24, sqrt(gx*gx + gy*gy*0.6));"
                         "e1 = 1 - smoothstep(0.015, 0.025, sqrt((gx - 0.06)*(gx - 0.06) + (gy + 0.06)*(gy + 0.06)));"
                         "e2 = 1 - smoothstep(0.015, 0.025, sqrt((gx + 0.06)*(gx + 0.06) + (gy + 0.06)*(gy + 0.06)));"
                         "glow = body*(0.8 + 0.2*sin(t*2)) - e1 - e2;"
                         "0.05 + glow*0.75, 0.08 + glow*0.9, 0.18 + glow*0.95");
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        jumpSelectedCue();
      };
      auto menuOr = [this](int action) {
        if (!settingsOpen_) return tutorialButtonRect("MENU");
        return lessonSettingRect(action);
      };
      l.steps = {
        {"THE HAUNTED LIGHTHOUSE", nullptr,
         "A lighthouse, a ghost, and a keeper who has been alone with the lamp far too long. Press START.",
         "Projection mapping: bending the picture to fit the building. Warp, blend, slices.",
         "KEEPER ROOK", "Ye'll be wanting the ghost on the windows. Mind the lamp. The lamp is MINE.", none, nullptr, noTarget, nullptr},
        {"THE OUTPUT'S GEOMETRY", nullptr,
         "MENU, then Video Outputs, then Geometry. Where an output's shape lives.",
         "Geometry: the projector-menu word for 'what shape is the picture'.",
         nullptr, nullptr,
         [this] { return settingsOpen_ && settingsTab_ == 3 && settingsVideoSubTab_ == 1; },
         [this] { debugOpenSettings(3, 1); },
         [this] { return settingsOpen_ ? SDL_Rect {} : tutorialButtonRect("MENU"); }, nullptr},
        {"PIN A CORNER", nullptr,
         "Press warp edit, then drag a corner on the programme monitor. The window is not square. Neither is the keeper.",
         "A corner pin moves the four corners. Perspective keeps the lines straight, like a real window.",
         "KEEPER ROOK", "LEFT! Ye call that a corner? I've seen straighter gulls.",
         [this] {
           const OutputTarget& o = focusedOutput();
           return o.warpEnabled && (o.warpTopLeftX != 0.0f || o.warpTopLeftY != 0.0f || o.warpTopRightX != 0.0f ||
                                    o.warpTopRightY != 0.0f || o.warpBottomRightX != 0.0f ||
                                    o.warpBottomRightY != 0.0f || o.warpBottomLeftX != 0.0f ||
                                    o.warpBottomLeftY != 0.0f);
         },
         [this] {
           OutputTarget& o = focusedOutputMutable();
           pushUndoSnapshot();
           o.warpEnabled = true;
           o.warpTopLeftX = 0.06f;
           o.warpTopLeftY = 0.04f;
           o.warpTopRightX = -0.03f;
           o.warpBottomRightX = -0.06f;
           o.warpBottomRightY = -0.02f;
           markProjectDirty();
         },
         [this, menuOr] { return warpEditMode_ ? SDL_Rect {} : menuOr(kSettingsActionOutputWarpEdit); }, nullptr,
         kSettingsActionOutputWarpEdit},
        {"TWO SOFT EDGES", nullptr,
         "Back in Geometry: edge blend, + on the left and + on the right.",
         "Two projectors overlap; each fades its edge so the overlap is not twice as bright.",
         nullptr, nullptr,
         [this] { return focusedOutput().edgeBlendLeft > 0.0f && focusedOutput().edgeBlendRight > 0.0f; },
         [this] {
           OutputTarget& o = focusedOutputMutable();
           pushUndoSnapshot();
           o.edgeBlendLeft = std::max(o.edgeBlendLeft, 0.12f);
           o.edgeBlendRight = std::max(o.edgeBlendRight, 0.12f);
           markProjectDirty();
         },
         [this, menuOr] {
           return menuOr(focusedOutput().edgeBlendLeft <= 0.0f ? kSettingsActionOutputEdgeBlendLInc
                                                               : kSettingsActionOutputEdgeBlendRInc);
         }, nullptr, kSettingsActionOutputEdgeBlendLInc},
        {"THE SCREENS WINDOW", nullptr,
         "Geometry, SLICES, open. The Screens window is where slices are cut and placed.",
         "A slice is a piece of the picture, put anywhere on the output.",
         nullptr, nullptr,
         [this] { return screensOpen_; },
         [this] { setScreensVisible(true); },
         [this, menuOr] { return menuOr(kSettingsActionOpenScreens); }, nullptr, kSettingsActionOpenScreens},
        {"THREE SLICES", nullptr,
         "In the Screens window, + SLICE three times, and drag each crop to a third of the picture.",
         "Three windows, three slices: left, middle and right of one picture.",
         "KEEPER ROOK", "Three windows. The lamp gets the middle one. The lamp ALWAYS gets the middle one.",
         [this] { return focusedOutput().slices.size() >= 3; },
         [this] {
           const char* names[3] = {"left window", "middle window", "right window"};
           for (int i = static_cast<int>(focusedOutput().slices.size()); i < 3; ++i) {
             handleRemoteCommand(std::string("SLICE ADD ") + names[i]);
             const int n = static_cast<int>(focusedOutput().slices.size());
             char crop[96];
             std::snprintf(crop, sizeof crop, "SLICE %d CROP %d 0 33 100", n, i * 33);
             handleRemoteCommand(crop);
             std::snprintf(crop, sizeof crop, "SLICE %d PLACE %d 10 30 80", n, 2 + i * 33);
             handleRemoteCommand(crop);
           }
           setScreensVisible(true);
         },
         noTarget, nullptr},
        {"THE GHOST IS PLEASED", nullptr,
         "Warp, blend, slices. The keeper will finally sleep. The gulls will not.",
         "Slices have their own warp and mesh in the Screens window. And a ~ on the place and the turn.",
         "KEEPER ROOK", "Ye did alright, lad. Now get away from the lamp.", none, nullptr, noTarget, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Dr. Pixel: GO from anywhere (the game show, with Donna Wheel) ─────
    {
      LessonDef l;
      l.id = "go";
      l.videoTrack = false;
      l.title = "GO from anywhere";
      l.about = "Cue numbers, GOTO, a hotkey, and where Companion, MIDI and OSC come in.";
      l.show = "The game show, with Donna Wheel";
      l.setup = [this] {
        const char* gens[3] = {"swirl", "fireside", "pocket-test"};
        for (int i = 0; i < 3; ++i) {
          addGeneratorCue(gens[i]);
          Deck& deck = focusedDeckMutable();
          if (!deck.cues.empty()) deck.cues.back().name = "Contestant " + std::to_string(i + 1);
        }
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
      };
      auto contestant = [](int n) {
        const std::string name = "Contestant " + std::to_string(n);
        return [name](const Cue& c) { return c.name == name; };
      };
      auto selectContestant = [this, contestant](int n, QuickAction section) {
        int d = -1;
        const int i = lessonFindCue(contestant(n), &d);
        if (i < 0) return;
        project_.focusedDeckIndex = d;
        selectCueInDeck(d, i, false, false);
        openInspectorSection(section);
      };
      l.steps = {
        {"THE GAME SHOW", nullptr,
         "A game show, skapi. Three contestants, one buzzer, and cues that fire from anywhere. Press START.",
         "Cue numbers, hotkeys, and GO from other machines. Like a sequencer you can play from any key.",
         "DONNA WHEEL", "Welcome to the show! Let's spin that wheel!", std::function<bool()> {}, nullptr,
         [] { return SDL_Rect {}; }, nullptr},
        {"A CUE NUMBER", nullptr,
         "Contestant 3 is selected. In the inspector's metadata, give it a cue number: 3.",
         "Theatre numbers its cues. Q3, Q3.5, Q12A. Deckboy reads them all.",
         "DONNA WHEEL", "I'd like to buy a number, please.",
         [this, contestant] {
           int d = -1;
           const int i = lessonFindCue(contestant(3), &d);
           return i >= 0 && !project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].cueNumber.empty();
         },
         [this, contestant] {
           int d = -1;
           const int i = lessonFindCue(contestant(3), &d);
           if (i < 0) return;
           project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].cueNumber = "3";
           markProjectDirty();
         },
         [this] { return lessonQuickRect(QuickAction::EditCueNumber); },
         [selectContestant] { selectContestant(3, QuickAction::CueSectionMetadataToggle); }},
        {"GO TO IT", "CTRL+G",
         "Ctrl+G, type the number, Enter. It goes live wherever the list happens to be standing.",
         "GOTO. The fastest way across a long list. Faster than scrolling. Much faster than me.",
         "DONNA WHEEL", "And the number is... three!",
         [this, contestant] { return lessonCueLive(contestant(3)); },
         [this, contestant] {
           int d = -1;
           const int i = lessonFindCue(contestant(3), &d);
           if (i < 0) return;
           const std::string number = project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].cueNumber;
           project_.focusedDeckIndex = d;
           handleRemoteCommand("GOTO " + (number.empty() ? std::string("3") : number));
         },
         [] { return SDL_Rect {}; },
         [this] { selectCueInDeck(project_.focusedDeckIndex, 0, false, false); }},
        {"A BUZZER", "F5",
         "Contestant 1 is selected. In TRIGGERS, press key, then F5. That key now fires this cue.",
         "F1 to F10, F12, or the number pad. Each one, a buzzer.",
         "DONNA WHEEL", "Every contestant gets a buzzer.",
         [this] { return lessonCountCues([](const Cue& c) { return !c.triggerHotkey.empty(); }) > 0; },
         [this, contestant] {
           int d = -1;
           const int i = lessonFindCue(contestant(1), &d);
           if (i < 0) return;
           project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].triggerHotkey = "F5";
           markProjectDirty();
         },
         [this] { return lessonQuickRect(QuickAction::TriggerLearnKey); },
         [selectContestant] { selectContestant(1, QuickAction::CueSectionTriggersToggle); }},
        {"BUZZ IN", nullptr,
         "Press the key. The cue fires, wherever the list is standing.",
         "No mouse, no list, no looking. Just the key.",
         "DONNA WHEEL", "Buzz! We have a winner!",
         [this] { return lessonCueLive([](const Cue& c) { return !c.triggerHotkey.empty(); }); },
         [this] { lessonTake([](const Cue& c) { return !c.triggerHotkey.empty(); }); },
         [] { return SDL_Rect {}; }, nullptr},
        {"FROM ANYWHERE", nullptr,
         "Companion, a MIDI note, an OSC message: MENU, then Network, says where each one connects.",
         "A Stream Deck in the next room can press GO. So can a keyboard synth. I prefer the synth.",
         "DONNA WHEEL", "And for our viewers at home: you can play along too.",
         [this] { return settingsOpen_ && settingsTab_ == 2; },
         [this] { debugOpenSettings(2); },
         [this] { return settingsOpen_ ? SDL_Rect {} : tutorialButtonRect("MENU"); }, nullptr},
        {"THAT'S OUR SHOW", nullptr,
         "Numbers, GOTO, a buzzer, and GO from anywhere. You are a game show now, skapi.",
         "A cue can have a key, a MIDI note and an OSC address at once. Belt, braces and a second belt.",
         "DONNA WHEEL", "Goodnight, everybody! Keep spinning!", std::function<bool()> {}, nullptr,
         [] { return SDL_Rect {}; }, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Dr. Pixel: Words on screen (the kaiju weather report) ─────────────
    {
      LessonDef l;
      l.id = "words";
      l.videoTrack = false;
      l.title = "Words on screen";
      l.about = "A Lower Third with a name on it, then a title card with a key-green backdrop.";
      l.show = "The kaiju weather report, with June O'Neil";
      l.setup = [this] {
        lessonAddCodeCue("Weather map",
                         "land = step(0.55, sin(x*7.0 + 1.3)*0.5 + sin(y*5.0 + x*2.0)*0.5 + 0.5);"
                         "kx = 0.2 + fract(t*0.03)*0.7; ky = 0.55 + 0.03*sin(t*3.0);"
                         "k = 1 - smoothstep(0.05, 0.08, sqrt((x - kx)*(x - kx)*3.16 + (y - ky)*(y - ky)));"
                         "0.10 + land*0.25 - k*0.1, 0.25 + land*0.35 - k*0.2, 0.55 - land*0.3 - k*0.4");
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        jumpSelectedCue();
      };
      auto isLowerThird = [](const Cue& c) { return c.kind == CueKind::Text && c.lowerThird.on; };
      auto isTitle = [](const Cue& c) { return c.kind == CueKind::Text && !c.lowerThird.on; };
      l.steps = {
        {"THE KAIJU WEATHER", nullptr,
         "Channel 6 weather, skapi. A map, a presenter, and a very large lizard. Press START.",
         "Words on screen: Lower Thirds, text, and backdrops.",
         "JUNE O'NEIL", "I'm June O'Neil, and nothing about this forecast is normal.", std::function<bool()> {},
         nullptr, [] { return SDL_Rect {}; }, nullptr},
        {"A LAYER FOR WORDS", nullptr,
         "Words go on their own layer: press the + at the end of the playlist tabs.",
         "A second playlist is a layer over the map. The lizard stays underneath. Mostly.",
         "JUNE O'NEIL", "Words on top. Always on top.",
         [this] { return project_.decks.size() >= 2; },
         [this] { addDeck(); },
         [this] { return deckAddTabRect_; }, nullptr},
        lessonSelectSecondDeckStep(
          "Click its tab, Deck 2. The lit tab is the playlist SOURCE adds to.",
          "Skip this and the words go under the map. With the lizard.",
          "JUNE O'NEIL", "Nine years of live weather. Always click the tab."),
        {"A LOWER THIRD", nullptr,
         "In the new playlist: SOURCE, then Lower Third.",
         "A name and a title, low on the screen, out of the lizard's way.",
         nullptr, nullptr,
         [this, isLowerThird] { return lessonCueInDeckFrom(isLowerThird, 1); },
         [this] {
           if (project_.decks.size() < 2) addDeck();
           setFocusedDeckIndex(1);
           addLowerThirdTextCue();
         },
         [this] { return lessonOnSecondDeck(tutorialButtonRect("SOURCE")); }, nullptr},
        {"HER NAME", nullptr,
         "In the inspector, title: type the presenter's name. The second line is her job.",
         "Spelling counts. The audience reads it at the speed of light.",
         "JUNE O'NEIL", "Spell it right. Last week it said 'Jane O'Meal'.",
         [this, isLowerThird] {
           int d = -1;
           const int i = lessonFindCue(isLowerThird, &d);
           return i >= 0 && project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].textBody !=
                                "Name Surname\nTitle or role";
         },
         [this, isLowerThird] {
           int d = -1;
           const int i = lessonFindCue(isLowerThird, &d);
           if (i < 0) return;
           project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].textBody =
             "June O'Neil\nChannel 6 Weather";
           markProjectDirty();
         },
         [this] { return lessonQuickRect(QuickAction::LowerThirdEditTitle); },
         [this, isLowerThird] {
           int d = -1;
           const int i = lessonFindCue(isLowerThird, &d);
           if (i < 0) return;
           project_.focusedDeckIndex = d;
           selectCueInDeck(d, i, false, false);
           openInspectorSection(QuickAction::CueSectionTextToggle);
         }},
        {"ON AIR", "ENTER",
         "TAKE the Lower Third. Her name slides in over the map.",
         "Two layers live: the weather, and the words on top of it.",
         "JUNE O'NEIL", "Thank you. Now, about that lizard...",
         [this, isLowerThird] { return lessonCueLive(isLowerThird); },
         [this, isLowerThird] { lessonTake(isLowerThird); },
         [this, isLowerThird] { return lessonTakeTarget(isLowerThird); }, nullptr},
        {"BREAKING NEWS", nullptr,
         "SOURCE, then Text: a title card. Then its backdrop chip: key green, for the vision mixer to key out.",
         "A backdrop fills behind the words. Key green or key blue, and the mixer does the rest.",
         "JUNE O'NEIL", "Breaking news: it's standing on the five-day forecast.",
         [this, isTitle] {
           int d = -1;
           const int i = lessonFindCue(isTitle, &d);
           if (i < 0) return false;
           const int bd = project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].backdrop;
           return bd != -1 && bd != kTimerBackdropTransparent;
         },
         [this, isTitle] {
           if (lessonCountCues(isTitle) == 0) addTextCue();
           int d = -1;
           const int i = lessonFindCue(isTitle, &d);
           if (i < 0) return;
           Cue& cue = project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)];
           cue.textBody = "BREAKING: KAIJU";
           cue.backdrop = 0x00b140;
           markProjectDirty();
         },
         [this, isTitle] {
           return lessonCountCues(isTitle) == 0 ? tutorialButtonRect("SOURCE") : lessonQuickRect(QuickAction::CueBackdropPreset);
         }, nullptr},
        {"BACK TO YOU", nullptr,
         "A Lower Third, a name, a title card, a backdrop. The lizard has left the studio, skapi.",
         "Lower Thirds can wait, slide and leave on their own. Their section has every setting.",
         "JUNE O'NEIL", "That's the weather. Back to you, guys. Run.", std::function<bool()> {}, nullptr,
         [] { return SDL_Rect {}; }, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Dr. Pixel: Keeping time (a grand day out, with Wallis and Grommet) ─
    {
      LessonDef l;
      l.id = "timers";
      l.videoTrack = false;
      l.title = "Keeping time";
      l.about = "A stage timer counting down, held, and a presenter view for the person on stage.";
      l.show = "A grand day out, with Wallis and Grommet";
      l.setup = [] {};
      auto isTimer = [](const Cue& c) { return c.kind == CueKind::Timer; };
      l.steps = {
        {"A GRAND DAY OUT", nullptr,
         "A rocket built in a basement, skapi, and an inventor who wants cheese. Press START.",
         "Stage timers: countdowns, clocks, and a presenter view for the person on stage.",
         "WALLIS", "Cheese, Grommet! The moon's made of it. We'll need a countdown.", std::function<bool()> {},
         nullptr, [] { return SDL_Rect {}; }, nullptr},
        {"A COUNTDOWN", nullptr,
         "SOURCE, then Stage Timer. A clock that counts down, in big friendly numbers.",
         "Its colours change as it runs out: white, amber, red. Like a VU meter for time.",
         "GROMMET", "*raises an eyebrow*",
         [this, isTimer] { return lessonCountCues(isTimer) > 0; },
         [this] { addTimerCue(10); },
         [this] { return tutorialButtonRect("SOURCE"); }, nullptr},
        {"LIFT OFF", "ENTER",
         "Select the timer and TAKE it. The countdown starts.",
         "Past zero it keeps going, counting the overtime. Very honest. Slightly rude.",
         "WALLIS", "Ten... nine... ooh, I do love a countdown.",
         [this, isTimer] { return lessonCueLive(isTimer); },
         [this, isTimer] { lessonTake(isTimer); },
         [this, isTimer] { return lessonTakeTarget(isTimer); }, nullptr},
        {"HOLD!", "SPACE",
         "Something's loose! Space holds the countdown. The clock shows it is held.",
         "A hold is not a stop. Space again, and it carries on from where it was.",
         "GROMMET", "*holds up a spanner*",
         [this, isTimer] {
           for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
             const Cue* cue = activeCuePtr(d);
             const MediaEngine* engine = mediaEngineForDeck(d);
             if (cue && isTimer(*cue) && engine && engine->state() == TransportState::Paused) return true;
           }
           return false;
         },
         [this, isTimer] {
           int d = -1;
           if (lessonFindCue(isTimer, &d) < 0) return;
           project_.focusedDeckIndex = d;
           handleRemoteCommand("PAUSE");
         },
         [] { return SDL_Rect {}; }, nullptr},
        {"THE PILOT'S VIEW", nullptr,
         "MENU, Video Outputs: make an output's type PRESENTER. The person on stage gets the timer, the next cue and their notes.",
         "A presenter view is an output like any other. Point it at the confidence monitor.",
         "WALLIS", "So the pilot can see the time. The pilot is Grommet.",
         [this] {
           for (const OutputTarget& o : project_.outputs) {
             if (normalizeOutputType(o.outputType) == "presenter") return true;
           }
           return false;
         },
         [this] {
           if (project_.outputs.empty()) return;
           pushUndoSnapshot();
           focusedOutputMutable().outputType = "presenter";
           markProjectDirty();
         },
         [this] {
           if (!settingsOpen_) return tutorialButtonRect("MENU");
           return lessonSettingRect(kSettingsActionOutputTypeCycle);
         }, nullptr},
        {"TO THE MOON", nullptr,
         "A countdown, a hold, and a view for the pilot. Nazdrave, skapi. Bring crackers.",
         "Timers can also count up, or show the time of day. Their section has the lot.",
         "WALLIS", "A grand day out, Grommet! Pass the crackers.", std::function<bool()> {}, nullptr,
         [] { return SDL_Rect {}; }, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Dr. Pixel: Two playlists and TAKE ALL (the magic show) ────────────
    {
      LessonDef l;
      l.id = "layers";
      l.videoTrack = false;
      l.title = "Two playlists and TAKE ALL";
      l.about = "A second playlist as a layer, and TAKE ALL so two things change on one beat.";
      l.show = "The magic show, with Turnip Top";
      l.setup = [this] {
        addGeneratorCue("pocket-test");
        addGeneratorCue("swirl");
        Deck& deck = focusedDeckMutable();
        if (deck.cues.size() >= 2) {
          deck.cues[0].name = "The trunk (closed)";
          deck.cues[1].name = "The trunk (open)";
        }
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        jumpSelectedCue();
      };
      auto isOpenTrunk = [](const Cue& c) { return c.name == "The trunk (open)"; };
      l.steps = {
        {"THE MAGIC SHOW", nullptr,
         "A magic show, skapi. A trunk, a prop, and one beat where everything changes at once. Press START.",
         "Two playlists are two layers. TAKE ALL changes both together.",
         "TURNIP TOP", "Every trick needs two things happening at once. And a rubber chicken.",
         std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
        {"A SECOND PLAYLIST", nullptr,
         "The prop goes on its own layer. Press the + at the end of the playlist tabs.",
         "Playlist 2 sits on top of playlist 1. Like a transparency on an overhead projector.",
         "TURNIP TOP", "Nothing up my sleeve. Everything in my trunk.",
         [this] { return project_.decks.size() >= 2; },
         [this] { addDeck(); },
         [this] { return deckAddTabRect_; }, nullptr},
        lessonSelectSecondDeckStep(
          "Click its tab, Deck 2. A new playlist waits until you choose it.",
          "The lit tab gets the next cue. Like choosing which slide goes in the projector.",
          "TURNIP TOP", "Pick a deck. Any deck. ...Deck 2. Pick Deck 2."),
        {"THE PROP", nullptr,
         "In the new playlist, add anything: PATTERN will do. That is the prop.",
         "Whatever it is, it will appear the instant the trunk opens.",
         "TURNIP TOP", "Is it a rubber chicken? It's always a rubber chicken.",
         [this] { return project_.decks.size() >= 2 && !project_.decks[1].cues.empty(); },
         [this] {
           if (project_.decks.size() < 2) addDeck();
           project_.focusedDeckIndex = 1;
           addGeneratorCue("fireside");
           Deck& deck = focusedDeckMutable();
           if (!deck.cues.empty()) deck.cues.back().name = "The prop (rubber chicken)";
         },
         [this] { return lessonOnSecondDeck(tutorialButtonRect("PATTERN")); }, nullptr},
        {"TA-DA", "CTRL+ENTER",
         "Select 'The trunk (open)' in playlist 1 and the prop in playlist 2. Then TAKE ALL: both on one beat.",
         "One press, every playlist's selected cue. The audience never sees the seam.",
         "TURNIP TOP", "Ta-daaa! ...Hold for applause. Hold. Hold.",
         [this, isOpenTrunk] {
           if (project_.decks.size() < 2) return false;
           const Deck& one = project_.decks[0];
           const bool trunk = one.activeIndex >= 0 && one.activeIndex < static_cast<int>(one.cues.size()) &&
                              isOpenTrunk(one.cues[static_cast<std::size_t>(one.activeIndex)]);
           return trunk && project_.decks[1].activeIndex >= 0;
         },
         [this, isOpenTrunk] {
           if (project_.decks.size() < 2 || project_.decks[1].cues.empty()) return;
           int d = -1;
           const int i = lessonFindCue(isOpenTrunk, &d);
           if (i < 0) return;
           selectCueInDeck(0, i, false, false);
           selectCueInDeck(1, 0, false, false);
           takeAllDecks(true);
         },
         [this, isOpenTrunk] {
           int d = -1;
           const int i = lessonFindCue(isOpenTrunk, &d);
           if (i >= 0 && project_.decks[0].selectedIndex != i) return lessonCueRowRect(0, i);
           if (project_.decks.size() >= 2 && !project_.decks[1].cues.empty() && project_.decks[1].selectedIndex < 0) {
             return lessonCueRowRect(1, 0);
           }
           return tutorialButtonRect("TAKE ALL");
         }, nullptr},
        {"THE PRESTIGE", nullptr,
         "Two layers, one beat. Every playlist has its own opacity and can sit on any output. Nazdrave, skapi.",
         "A magician never reveals. A playlist never minds being revealed.",
         "TURNIP TOP", "Thank you! I'm here all week! The chicken is here forever.",
         std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Dr. Pixel: When it goes wrong (the cooking show, Chef Bork) ───────
    {
      LessonDef l;
      l.id = "wrong";
      l.videoTrack = false;
      l.title = "When it goes wrong";
      l.about = "BLACK against PANIC, and getting a show back.";
      l.show = "The cooking show, with Chef Bork";
      l.setup = [this] {
        addGeneratorCue("fireside");
        addGeneratorCue("swirl");
        Deck& deck = focusedDeckMutable();
        if (deck.cues.size() >= 2) {
          deck.cues[0].name = "The kitchen (on fire)";
          deck.cues[1].name = "A lovely souffle";
        }
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        jumpSelectedCue();
      };
      auto isSouffle = [](const Cue& c) { return c.name == "A lovely souffle"; };
      l.steps = {
        {"THE COOKING SHOW", nullptr,
         "A cooking show, skapi. Live. Everything is on fire. Press START.",
         "BLACK hides the picture and keeps the show running. PANIC stops everything. Know which you need.",
         "CHEF BORK", "Bork bork! Hurdy-flurdy, de kitchen is fire-a-flurdy!", std::function<bool()> {}, nullptr,
         [] { return SDL_Rect {}; }, nullptr},
        {"BLACK", "B",
         "The audience must not see this. Press BLACK, or B.",
         "The picture goes. The cue keeps playing underneath, ready to come back.",
         "CHEF BORK", "Bork? Bork. Oh, de picture is gun.",
         [this] { return masterDimmerTarget_ < 0.5; },
         [this] { masterDimmerTarget_ = 0.0; },
         [this] { return tutorialButtonRect("BLACK"); }, nullptr},
        {"AND BACK", "B",
         "The fire's out. Press B again: the picture comes back, still running.",
         "Nothing was lost. A mute on a mixer, for pictures.",
         "CHEF BORK", "Ta-da! Still cookin'!",
         [this] { return masterDimmerTarget_ > 0.5; },
         [this] { masterDimmerTarget_ = 1.0; },
         [this] { return tutorialButtonRect("BLACK"); }, nullptr},
        {"PANIC", nullptr,
         "Worse: MENU, System, the SAFETY card: PANIC. Everything stops, the way the panic setting says.",
         "PANIC is the big red button. Use it when BLACK is not enough. Then breathe.",
         "CHEF BORK", "Bork bork BORK!",
         [this] {
           for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
             if (deckIsRunning(d)) return false;
           }
           return true;
         },
         [this] { triggerPanicProfile(); },
         [this] {
           if (!settingsOpen_) return tutorialButtonRect("MENU");
           return lessonSettingRect(207);
         }, nullptr},
        {"GET IT BACK", "ENTER",
         "Close MENU, select the souffle, and TAKE it. The show is back on.",
         "After a panic, the show is still there. Pick up where you meant to be.",
         "CHEF BORK", "Voila! De souffle, he is risen!",
         [this, isSouffle] { return lessonCueLive(isSouffle); },
         [this, isSouffle] {
           settingsOpen_ = false;
           lessonTake(isSouffle);
         },
         [this, isSouffle] { return settingsOpen_ ? SDL_Rect {} : lessonTakeTarget(isSouffle); }, nullptr},
        {"BON APPETIT", nullptr,
         "BLACK to hide, PANIC to stop, TAKE to come back. You will survive any kitchen, skapi.",
         "The panic setting chooses what PANIC does: fade out, cut, or just the outputs.",
         "CHEF BORK", "Bork bork bork! Goodnight!", std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; },
         nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Dr. Pixel: Presets (a plain lesson, so the face drops in) ─────────
    {
      LessonDef l;
      l.id = "presets";
      l.videoTrack = false;
      l.title = "Presets";
      l.about = "Capture a look on a DASH button, change everything, and get it back with one press.";
      l.show = "";
      l.setup = [this] {
        addGeneratorCue("swirl");
        addGeneratorCue("fireside");
        Deck& deck = focusedDeckMutable();
        if (deck.cues.size() >= 2) {
          deck.cues[0].name = "Look one";
          deck.cues[1].name = "Look two";
        }
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        jumpSelectedCue();
      };
      auto presetSlot = [this]() -> int {
        for (int i = 0; i < static_cast<int>(project_.dashboard.size()); ++i) {
          if (project_.dashboard[static_cast<std::size_t>(i)].command.rfind("PRESET RECALL ", 0) == 0) return i;
        }
        return -1;
      };
      auto dashRect = [this](QuickAction action, int param) {
        for (const auto& b : dashButtons_) {
          if (b.action == action && b.rect.w > 0 && (param < 0 || b.param == param)) return b.rect;
        }
        return SDL_Rect {};
      };
      l.steps = {
        {"PRESETS", nullptr,
         "Presets, skapi. A whole look, remembered, and back with one button. Press START.",
         "Like the patch memory on a synth: store, then recall.",
         nullptr, nullptr, std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
        {"THE DASH", "CTRL+D",
         "Press DASH: buttons of your own, for anything at all.",
         "Ctrl+D also works. Very fast. Very professional.",
         nullptr, nullptr,
         [this] { return dashboardOverlayOpen_; },
         [this] { dashboardOverlayOpen_ = true; },
         [this] { return dashboardOverlayOpen_ ? SDL_Rect {} : tutorialButtonRect("DASH"); }, nullptr},
        {"A PRESET BUTTON", nullptr,
         "Press +, then 'preset - a new preset from what is on now'. The button remembers this look.",
         "Store. Like saving the patch before the drummer leans on the synth.",
         nullptr, nullptr,
         [this, presetSlot] { return !project_.presets.empty() && presetSlot() >= 0; },
         [this] {
           dashboardOverlayOpen_ = true;
           const ShowPreset& preset = savePresetFromNow("Look one");
           DashboardSlot slot;
           slot.label = preset.name;
           slot.command = presetRecallCommand(preset);
           slot.glyph = "*";
           project_.dashboard.push_back(slot);
           markProjectDirty();
         },
         [dashRect] { return dashRect(QuickAction::DashSlotAdd, -1); }, nullptr},
        {"CHANGE EVERYTHING", "ENTER",
         "Close the DASH (Esc), then select Look two and TAKE it. A different look.",
         "Change it all. Don't worry. It remembers.",
         nullptr, nullptr,
         [this] { return focusedDeck().activeIndex >= 0 && focusedDeck().activeIndex != lessonCount_; },
         [this] {
           dashboardOverlayOpen_ = false;
           const Deck& deck = focusedDeck();
           for (int i = 0; i < static_cast<int>(deck.cues.size()); ++i) {
             if (i != deck.activeIndex) {
               selectCueInDeck(project_.focusedDeckIndex, i, false, false);
               jumpSelectedCue();
               break;
             }
           }
         },
         [this] {
           if (dashboardOverlayOpen_) return dashCloseRect_;
           const Deck& deck = focusedDeck();
           for (int i = 0; i < static_cast<int>(deck.cues.size()); ++i) {
             if (i != deck.activeIndex) {
               return deck.selectedIndex == i ? tutorialButtonRect("TAKE") : lessonCueRowRect(project_.focusedDeckIndex, i);
             }
           }
           return SDL_Rect {};
         },
         [this] { lessonCount_ = focusedDeck().activeIndex; }},
        {"ONE PRESS", nullptr,
         "Open the DASH and press your preset button. The first look comes back.",
         "Recall. The patch memory, returning. Beautiful.",
         nullptr, nullptr,
         [this] { return focusedDeck().activeIndex == lessonCount_; },
         [this, presetSlot] {
           const int at = presetSlot();
           if (at >= 0) handleRemoteCommand(project_.dashboard[static_cast<std::size_t>(at)].command);
         },
         [this, presetSlot, dashRect] {
           if (!dashboardOverlayOpen_) return tutorialButtonRect("DASH");
           return dashRect(QuickAction::DashSlotFire, presetSlot());
         }, nullptr},
        {"REMEMBERED", nullptr,
         "A look, stored and recalled. A preset can keep cues, positions, effects, levels and routing; its button's menu chooses which.",
         "And a DASH button can run any command at all. Any. Even the exciting ones.",
         nullptr, nullptr, std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Prof. Video: Transitions (the trailer, Optimus Primetime) ─────────
    {
      LessonDef l;
      l.id = "transitions";
      l.videoTrack = true;
      l.title = "Transitions";
      l.about = "The styles beyond the crossfade: an iris, a shatter, and their times.";
      l.show = "The space-opera trailer, with Optimus Primetime";
      l.setup = [this] {
        addGeneratorCue("pocket-test");
        addGeneratorCue("swirl");
        addGeneratorCue("fireside");
        Deck& deck = focusedDeckMutable();
        if (deck.cues.size() >= 3) {
          deck.cues[0].name = "Opening titles";
          deck.cues[1].name = "The planet";
          deck.cues[2].name = "The battle";
        }
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        jumpSelectedCue();
      };
      auto named = [](const char* name) {
        const std::string n = name;
        return [n](const Cue& c) { return c.name == n; };
      };
      auto fancy = [](const std::string& style) {
        return !style.empty() && style != "cut" && style != "crossfade";
      };
      auto selectNamed = [this, named](const char* name) {
        int d = -1;
        const int i = lessonFindCue(named(name), &d);
        if (i < 0) return;
        project_.focusedDeckIndex = d;
        selectCueInDeck(d, i, false, false);
        openInspectorSection(QuickAction::CueSectionPlaybackToggle);
      };
      auto setStyle = [this, named](const char* name, const char* style) {
        int d = -1;
        const int i = lessonFindCue(named(name), &d);
        if (i < 0) return;
        Cue& cue = project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)];
        pushUndoSnapshot();
        cue.cueTransitionStyle = style;
        if (cue.cueTransitionSeconds <= 0.0) cue.cueTransitionSeconds = 1.0;
        markProjectDirty();
      };
      l.steps = {
        {"THE TRAILER", nullptr,
         "A space-opera trailer, skapi. A narrator with a very deep voice and opinions about transitions. Press START.",
         "A transition is how one cue becomes the next. There are far more than the crossfade.",
         "OPTIMUS PRIMETIME", "In a galaxy of content... one operator... must choose a transition.",
         std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
        {"A STYLE", nullptr,
         "'The planet' is selected. In the inspector, its transition style: choose iris.",
         "Pushes, wipes, iris, dip to white. Even a portal. I wrote half of them. On a Tuesday.",
         "OPTIMUS PRIMETIME", "The planet deserves... a dramatic entrance.",
         [this, named, fancy] {
           int d = -1;
           const int i = lessonFindCue(named("The planet"), &d);
           return i >= 0 && fancy(project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].cueTransitionStyle);
         },
         [setStyle] { setStyle("The planet", "iris"); },
         [this] { return cueTransitionStyleDropdownRect_; },
         [selectNamed] { selectNamed("The planet"); }},
        {"A TIME", nullptr,
         "Give it a time: + on transition until it says a second or more.",
         "No time, no transition. A style needs a duration to do anything at all.",
         nullptr, nullptr,
         [this, named] {
           int d = -1;
           const int i = lessonFindCue(named("The planet"), &d);
           return i >= 0 && project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].cueTransitionSeconds > 0.0;
         },
         [setStyle] { setStyle("The planet", "iris"); },
         [this] { return tutorialTransitionRowRect(); },
         [selectNamed] { selectNamed("The planet"); }},
        {"ROLL IT", "ENTER",
         "TAKE the planet. Watch it arrive.",
         "The transition belongs to the cue arriving. Each cue chooses its own entrance.",
         "OPTIMUS PRIMETIME", "Autobots... roll the transition.",
         [this, named] { return lessonCueLive(named("The planet")); },
         [this, named] { lessonTake(named("The planet")); },
         [this, named] { return lessonTakeTarget(named("The planet")); }, nullptr},
        {"THE BATTLE", "ENTER",
         "Now 'The battle': choose shatter, give it a time, and TAKE it.",
         "Shatter drops the old picture like a pane of glass. Please do not do this at a funeral.",
         "OPTIMUS PRIMETIME", "Transform... and shatter out!",
         [this, named, fancy] {
           int d = -1;
           const int i = lessonFindCue(named("The battle"), &d);
           return i >= 0 && lessonCueLive(named("The battle")) &&
                  fancy(project_.decks[static_cast<std::size_t>(d)].cues[static_cast<std::size_t>(i)].cueTransitionStyle);
         },
         [this, setStyle, named] {
           setStyle("The battle", "shatter");
           lessonTake(named("The battle"));
         },
         [this, named] {
           int d = -1;
           const int i = lessonFindCue(named("The battle"), &d);
           if (i < 0) return SDL_Rect {};
           if (project_.decks[static_cast<std::size_t>(d)].selectedIndex != i) return lessonCueRowRect(d, i);
           return cueTransitionStyleDropdownRect_;
         },
         [selectNamed] { selectNamed("The battle"); }},
        {"IN CINEMAS NOW", nullptr,
         "A style, a time, and two very dramatic entrances. Nazdrave, skapi.",
         "A playlist has a default style too, for every cue that does not choose its own.",
         "OPTIMUS PRIMETIME", "Coming soon... to a desk near you.",
         std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Prof. Video: Sound (the robot DJs, Deft Funk) ─────────────────────
    {
      LessonDef l;
      l.id = "sound";
      l.videoTrack = true;
      l.title = "Sound";
      l.about = "An audio effect on a cue, a bend that damages it beautifully, and where the patch lives.";
      l.show = "The robot DJs, with Deft Funk";
      l.setup = [this] {
        addToneCue();
        Deck& deck = focusedDeckMutable();
        if (!deck.cues.empty()) deck.cues.back().name = "The beat";
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        jumpSelectedCue();
      };
      auto effectCount = [this]() -> int {
        const Cue* cue = selectedCuePtr();
        return cue ? static_cast<int>(cue->audioEffects.size()) : 0;
      };
      auto openFx = [this] {
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        openInspectorSection(QuickAction::CueSectionAudioFxToggle);
      };
      l.steps = {
        {"THE ROBOT DJS", nullptr,
         "Two robot DJs, skapi. They do not talk. Their helmets do. Press START.",
         "Every cue has its own chain of audio effects, and some of them break the sound on purpose.",
         "DEFT FUNK (SILVER)", "> HELLO. WE ARE HERE TO FIX YOUR MIX.",
         std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
        {"AN EFFECT", nullptr,
         "The beat is selected. In its AUDIO EFFECTS section, + adds one. Try the delay.",
         "Delay, reverb, filters, a compressor. Amount always means 'less of this'.",
         "DEFT FUNK (GOLD)", "> ONE MORE TIME. WITH ECHO. ECHO. ECHO.",
         [effectCount] { return effectCount() >= 1; },
         [this] { handleRemoteCommand("AUDIOFX ADD delay 40"); },
         [this] { return lessonQuickRect(QuickAction::AudioEffectAdd); }, openFx},
        {"A BEND", nullptr,
         "Add a second: a bend, like crush. It damages the sound the way a broken file would.",
         "The bends are my favourite. Like a tape left in the sun. On purpose.",
         "DEFT FUNK (SILVER)", "> HARDER. BETTER. CRUNCHIER. LOUDER.",
         [effectCount] { return effectCount() >= 2; },
         [this] { handleRemoteCommand("AUDIOFX ADD crush 60"); },
         [this] { return lessonQuickRect(QuickAction::AudioEffectAdd); }, openFx},
        {"THE PATCH", nullptr,
         "MENU, then Audio: where each playlist's sound goes, and the patch for named outputs.",
         "Speakers, monitors, an in-ear for the drummer. The patch sends each its own.",
         "DEFT FUNK (GOLD)", "> AROUND THE MIX. AROUND THE MIX. AROUND THE MIX.",
         [this] { return settingsOpen_ && settingsTab_ == 1; },
         [this] { debugOpenSettings(1); },
         [this] { return settingsOpen_ ? SDL_Rect {} : tutorialButtonRect("MENU"); }, nullptr},
        {"ENCORE", nullptr,
         "An effect, a bend and the patch. The robots approve, skapi. I think. Their faces are helmets.",
         "Every audio effect can follow a ~ too, even Motion. Wave at the delay. It's wonderful.",
         "DEFT FUNK (SILVER)", "> THANK YOU. GOOD NIGHT. WE ARE ROBOTS.",
         std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Prof. Video: Reading the scopes (The Signal Files, again) ─────────
    {
      LessonDef l;
      l.id = "scopes";
      l.videoTrack = true;
      l.title = "Reading the scopes";
      l.about = "The waveform, the vectorscope, and false colour: is the glow real, or just too bright?";
      l.show = "The Signal Files: is the glow on the horizon a craft?";
      l.setup = [this] {
        lessonAddCodeCue("The glow on the horizon (evidence #2)",
                         "g = 1 - smoothstep(0.0, 0.25, sqrt((x - 0.7)*(x - 0.7)*3.16 + (y - 0.62)*(y - 0.62)*4.0));"
                         "sky = 0.05 + (1 - y)*0.12; land = step(0.72, y);"
                         "(sky + g*1.3)*(1 - land*0.85), (sky*0.5 + g*0.05)*(1 - land*0.85), (sky*1.6 + g*1.4)*(1 - land*0.85)");
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
        jumpSelectedCue();
      };
      l.steps = {
        {"EVIDENCE #2", nullptr,
         "The Bureau again, skapi. A glow on the horizon, and two agents who disagree about it. Press START.",
         "The scopes measure a picture: brightness, colour, levels. Numbers, not opinions.",
         "AGENT HOLLIS", "It's them, Brandt. It's a craft. Look how it glows.", std::function<bool()> {}, nullptr,
         [] { return SDL_Rect {}; }, nullptr},
        {"THE REAL SIGNAL", nullptr,
         "The scopes read only what really goes out, never a preview. Turn the output on: MENU, Video Outputs, OUTPUT OFF.",
         "Measuring a scaled-down preview would be measuring a photocopy. We measure the original.",
         "AGENT BRANDT", "Chain of evidence, Hollis. The real signal or nothing.",
         [this] { return tutorialAnyOutputOn(); },
         [this] { setFocusedOutputEnabled(true, false); },
         [this] {
           if (!settingsOpen_) return tutorialButtonRect("MENU");
           return lessonSettingRect(kSettingsActionOutputToggle);
         },
         [this] {
           tutorialAimOutputs(tutorialScreens());
           if (settingsOpen_) settingsTab_ = 3;
         }},
        {"THE SCOPES", nullptr,
         "Close MENU. Above the monitor, the PROGRAM button: press it until it says SCOPES or BOTH.",
         "Both shows the picture and its scopes side by side. Very civilised.",
         "AGENT BRANDT", "Let the scopes decide, Hollis.",
         [this] { return monitorView_ != MonitorView::Program; },
         [this] { monitorView_ = MonitorView::Both; },
         [this] { return scopesViewBtnRect_; }, nullptr},
        {"THE WAVEFORM", nullptr,
         "Click the waveform to make it big. Bright things sit high, dark things low, left to right as in the picture.",
         "The glow is that spike near the top. Very bright. Bright is not the same as alien.",
         "AGENT BRANDT", "Look at the peak. Nothing a streetlight couldn't do.",
         [this] { return scopesBig_ == kScopeWaveform; },
         [this] { setScopeBig(kScopeWaveform); },
         [this] { return scopesBig_ == kScopeWaveform ? SDL_Rect {} : scopesPanelRects_[kScopeWaveform]; }, nullptr},
        {"THE VECTORSCOPE", nullptr,
         "Click it again to go back, then click the vectorscope: colour round a circle. Saturated runs to the edge.",
         "Magenta, far out by the rim. A strong colour. Strong is not illegal. Yet.",
         "AGENT HOLLIS", "It's off the chart, Brandt! Literally off the chart!",
         [this] { return scopesBig_ == kScopeVector; },
         [this] { setScopeBig(kScopeVector); },
         [this] { return scopesBig_ == kScopeVector ? SDL_Rect {} : scopesPanelRects_[scopesBig_ >= 0 ? scopesBig_ : kScopeVector]; }, nullptr},
        {"FALSE COLOUR", nullptr,
         "And false colour: red is clipped, yellow nearly. Is the glow red?",
         "Clipped means the brightest the signal can carry, and then some lost. Detail gone.",
         "AGENT BRANDT", "Red. Clipped. Not alien, Hollis. Just too bright. Case closed.",
         [this] { return scopesBig_ == kScopeFalseColour; },
         [this] { setScopeBig(kScopeFalseColour); },
         [this] { return scopesBig_ == kScopeFalseColour ? SDL_Rect {} : scopesPanelRects_[scopesBig_ >= 0 ? scopesBig_ : kScopeFalseColour]; },
         nullptr},
        {"THE TRUTH", nullptr,
         "Waveform, vectorscope, false colour. The scopes settled it, skapi. The agents did not.",
         "SCOPES SOURCE reads any output, and the matrix can be 709 or 2020. For the curious.",
         "AGENT HOLLIS", "Too bright for a streetlight, Brandt. Too bright for anything. ...I want to believe.",
         std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Prof. Video: Out of the building (a plain lesson) ─────────────────
    // Offers only what this computer has: NDI is a step where its runtime is
    // installed, and SHOW ME moves on where it is not.
    {
      LessonDef l;
      l.id = "outside";
      l.videoTrack = true;
      l.title = "Out of the building";
      l.about = "Where NDI, SDI and ST 2110 live, and NDI switched on where this computer has it.";
      l.show = "";
      l.setup = [this] {
        addGeneratorCue("pocket-test");
        selectCueInDeck(project_.focusedDeckIndex, 0, false, false);
      };
      l.steps = {
        {"OUT OF THE BUILDING", nullptr,
         "Pictures that leave by cable or network, skapi, not only a window on a screen. Press START.",
         "NDI over the network, SDI from a DeckLink card, ST 2110 for broadcast plants.",
         nullptr, nullptr, std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
        {"THE DEVICES PAGE", nullptr,
         "MENU, Video Outputs, then Devices: every way an output can leave this computer.",
         "Each one says whether this machine can do it, and what it needs if not.",
         nullptr, nullptr,
         [this] { return settingsOpen_ && settingsTab_ == 3 && settingsVideoSubTab_ == 2; },
         [this] { debugOpenSettings(3, 2); },
         [this] { return settingsOpen_ ? SDL_Rect {} : tutorialButtonRect("MENU"); }, nullptr},
        {"NDI", nullptr,
         "If this computer has NDI, switch NDI SENDING on: the picture is on the network. If not, SHOW ME moves on.",
         "NDI finds itself on the network. No cables, only a good switch and patience.",
         nullptr, nullptr,
         [this] { return focusedOutput().ndiEnabled; },
         [this] {
           if (ndiRuntimeAvailable()) {
             setFocusedOutputNdiEnabled(true);
           } else {
             advanceTutorial();
           }
         },
         [this] { return lessonSettingRect(271); }, nullptr},
        {"OUT THERE", nullptr,
         "The output's own page has the rest: a DeckLink card's SDI, ST 2110 with NMOS, Spout and Syphon.",
         "Every one is just an output. The show does not know or care how it leaves.",
         nullptr, nullptr, std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
      };
      lessons_.push_back(std::move(l));
    }

    // ── Prof. Video: Motion, the game ─────────────────────────────────────
    {
      LessonDef l;
      l.id = "bugswat";
      l.videoTrack = true;
      l.title = "Motion: Bug Swat";
      l.about = "The webcam as a controller, as a game. Five rounds, a high score.";
      l.show = "A handheld, and bugs";
      l.game = true;
      lessons_.push_back(std::move(l));
    }

    // ── Dr. Pixel: The multiview (mystery on the starship Cathode) ────────
    {
      LessonDef l;
      l.id = "multiview";
      l.videoTrack = false;
      l.title = "The multiview";
      l.about = "Windows, the ... menu, what each one shows, layouts, and tally.";
      l.show = "Mystery on the starship Cathode, with Lt. Haila and Ensign Jinkley";
      l.setup = [this] {
        const char* gens[4] = {"swirl", "fireside", "checkerboard-motion", "pocket-test"};
        const char* feeds[4] = {"Nebula ahead", "Engine room", "Deck 3: something moving", "Sickbay"};
        for (int d = 0; d < 4; ++d) {
          if (d > 0) addDeck();
          project_.focusedDeckIndex = d;
          addGeneratorCue(gens[d]);
          Deck& deck = focusedDeckMutable();
          if (!deck.cues.empty()) deck.cues.back().name = feeds[d];
        }
        project_.focusedDeckIndex = 0;
        selectCueInDeck(0, 0, false, false);
        jumpSelectedCue();
      };
      auto tileShows = [this](const char* source) {
        for (const auto& tile : project_.multiviewTiles) {
          if (tile.source == source) return true;
        }
        return false;
      };
      auto chip = [this] { return multiviewChipRects_.empty() ? SDL_Rect {} : multiviewChipRects_.back(); };
      l.steps = {
        {"THE BRIDGE", nullptr,
         "The bridge of the starship Cathode, skapi. Four feeds, one main viewer, one mystery. Press START.",
         "The multiview shows every playlist at once. Like a wall of oscilloscopes, but in space.",
         "LT. HAILA", "Frequencies open, Captain. All of them. That's the problem.", none, nullptr, noTarget, nullptr},
        {"MULTI ON", nullptr,
         "MULTI, above the monitor: every playlist in its own window.",
         "One window per playlist, the programme first.",
         "ENSIGN JINKLEY", "Jinkys! Four feeds. And one of them has a monster in it.",
         [this] { return project_.multiviewMode != 0; },
         [this] { handleRemoteCommand("MULTIVIEW ON"); },
         [this] { return multiviewBtnRect_; }, nullptr},
        {"WHAT'S NEXT", nullptr,
         "Add a window (+), then its ... menu: make it show NEXT. What goes live when you press TAKE.",
         "Every window has a ... button. It chooses what that window shows.",
         "LT. HAILA", "I need to see who's calling before they reach the main viewer.",
         [tileShows] { return tileShows("next"); },
         [this] {
           handleRemoteCommand("MULTIVIEW WINDOW ADD");
           handleRemoteCommand("MULTIVIEW WINDOW " + std::to_string(project_.multiviewTiles.size()) + " NEXT");
         },
         chip, nullptr},
        {"A CLOCK", nullptr,
         "And one more window, showing the clock. Operators who watch a clock are never late. Mostly.",
         "Time of day, big. Stardates are just very long timecode.",
         "ENSIGN JINKLEY", "The monster only shows up at midnight. We're going to need a clock.",
         [tileShows] { return tileShows("clock"); },
         [this] {
           handleRemoteCommand("MULTIVIEW WINDOW ADD");
           handleRemoteCommand("MULTIVIEW WINDOW " + std::to_string(project_.multiviewTiles.size()) + " CLOCK");
         },
         chip, nullptr},
        {"ONE BIG ONE", nullptr,
         "Any window's ... menu, then a layout: one big, the rest small.",
         "The big window is window one. Usually the programme. Usually.",
         "LT. HAILA", "Main viewer big. The captain likes it big.",
         [this] { return project_.multiviewLayout != deckboy::multiview::Layout::Grid; },
         [this] { handleRemoteCommand("MULTIVIEW LAYOUT BIG"); },
         chip, nullptr},
        {"ON SCREEN", "ENTER",
         "Select Deck 3 in playlist 3 and TAKE. Watch its window.",
         "The red border is TALLY: the window that is on air. On screen, as they say on starships.",
         "ENSIGN JINKLEY", "There! Feed three! Put it on screen!",
         [this] { return project_.decks.size() > 2 && project_.decks[2].activeIndex >= 0; },
         [this] {
           if (project_.decks.size() <= 2 || project_.decks[2].cues.empty()) return;
           project_.focusedDeckIndex = 2;
           selectCueInDeck(2, 0, false, false);
           jumpSelectedCue();
         },
         [this] {
           if (project_.decks.size() <= 2 || project_.decks[2].cues.empty()) return SDL_Rect {};
           if (project_.focusedDeckIndex != 2 || project_.decks[2].selectedIndex != 0) return lessonCueRowRect(2, 0);
           return tutorialButtonRect("TAKE");
         }, nullptr},
        {"THAT'S A WRAP", nullptr,
         "Windows, the ... menu, NEXT, the clock, a layout, tally. Mystery solved, skapi.",
         "A window can also show an output, with its warp and slices. Handy when it's a lighthouse.",
         "ENSIGN JINKLEY", "And the monster was... the ship's cook, all along. Jinkys.", none, nullptr, noTarget, nullptr},
      };
      lessons_.push_back(std::move(l));
    }
  }

  // ── The face drops in (plain lessons only) ───────────────────────────────
  //
  // James, 2026-11-05 (board question 4: "yes, and maybe in the lessons
  // without themes"). Now and then the desk's own face pops into a corner
  // with something to say about how Deckboy is being run. Only in a lesson
  // with no show, so it never talks over a guest.
  Uint64 faceDropUntilMs_ = 0;
  std::string faceDropLine_;
  int faceDrops_ = 0;

  bool lessonIsPlain() const {
    const LessonDef* def = tutorial_.active && tutorial_.lesson > 0 ? lessonDef(tutorial_.lesson - 1) : nullptr;
    return def && (!def->show || !*def->show);
  }

  void faceDropIn(const std::string& line) {
    if (!lessonIsPlain()) return;
    faceDropLine_ = line;
    faceDropUntilMs_ = SDL_GetTicks() + 5200;
    ++faceDrops_;
  }

  void renderFaceDropIn(int width, int /*height*/) {
    if (!lessonIsPlain() || SDL_GetTicks() >= faceDropUntilMs_) return;
    const int w = std::min(uiScaled(340), width / 3);
    const int pad = uiScaled(10);
    const int faceH = uiScaled(130);
    const auto lines = codeWrapText(fontBase_, lessonText(faceDropLine_), w - pad * 2);
    const int lineH = textLineHeight(fontBase_);
    const int h = pad + faceH + static_cast<int>(lines.size()) * lineH + pad;
    // Slides down from the top-right corner, and back up at the end.
    const Uint64 left = faceDropUntilMs_ - SDL_GetTicks();
    const double in = std::clamp((5200.0 - static_cast<double>(left)) / 300.0, 0.0, 1.0);
    const double out = std::clamp(static_cast<double>(left) / 300.0, 0.0, 1.0);
    const int y = uiScaled(8) - static_cast<int>((1.0 - std::min(in, out)) * (h + uiScaled(16)));
    const SDL_Rect box {width - w - uiScaled(12), y, w, h};
    Primitives::drawFramedPanel(controlRenderer_, box, pal.deep, pal.deep, pal.light);
    // The face with nothing of its own to say; the line goes underneath,
    // wrapped, so a long one is read whole.
    drawStartupMascot(SDL_Rect {box.x + pad, box.y + pad, box.w - pad * 2, faceH}, animationNow_, "");
    int ty = box.y + pad + faceH;
    for (const auto& text : lines) {
      drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {box.x + pad, ty, box.w - pad * 2, lineH}, text, pal.light);
      ty += lineH;
    }
  }

  // ── Running one ──────────────────────────────────────────────────────────

  // Starts a lesson, or says why not.
  bool beginLesson(int index, std::string* why = nullptr) {
    auto refuse = [&](const std::string& reason) {
      if (why) *why = reason;
      triggerToast("lesson: " + reason, ToastKind::Warning, kToastReadableMs);
      return false;
    };
    const LessonDef* def = lessonDef(index);
    if (!def) return refuse("there is no such lesson");
    if (tutorial_.active) return refuse("finish or leave the lesson you are in first");
    if (bugSwat_.active) return refuse("leave Bug Swat first");
    lessonsListOpen_ = false;
    if (def->game) return beginBugSwat(why);
    TutorialState next;
    DeskLoan loan;
    std::string reason;
    if (!lendDesk(loan, reason)) return refuse(reason);
    next.returnTo = loan.returnTo;
    next.outputsWere = loan.outputsWere;
    next.dimmerWas = loan.dimmerWas;
    // A power-up from before this lesson is not this lesson's to joke about.
    lessonEvolveSeen_ = superDeckboyEvolveAt_;
    lessonQuipUntilMs_ = 0;
    const fs::path practice = lessonPracticeFile(*def);
    LessonsProgress progress = readLessonsProgress();
    int step = 0;
    const auto at = progress.at.find(def->id);
    std::error_code ec;
    if (at != progress.at.end() && at->second > 0 && fs::exists(practice, ec)) {
      openProjectFromPath(practice);
      if (!fs::equivalent(currentProjectFile_, practice, ec) || ec) {
        handDeskBack(loan);
        return refuse("the practice show would not open");
      }
      step = std::clamp(at->second, 0, static_cast<int>(def->steps.size()) - 1);
    } else {
      if (!startNewShow(false)) {
        handDeskBack(loan);
        return refuse("could not start the practice show");
      }
      currentProjectFile_ = practice;
      if (def->setup) def->setup();
      if (!saveProjectNow(false)) {
        handDeskBack(loan);
        return refuse("could not write the practice show to " + practice.parent_path().string());
      }
    }
    dressPracticeLikeDesk(loan);
    showStartupDialog_ = false;
    showSplashOverlay_ = false;
    settingsOpen_ = false;
    tutorial_ = next;
    tutorial_.active = true;
    tutorial_.lesson = index + 1;
    enterTutorialStep(step);
    triggerToast(std::string("lesson: ") + def->title + ". Esc leaves, and it picks up here next time",
                 ToastKind::Help);
    return true;
  }

  // From enterTutorialStep: the step's own setup, and where the lesson is.
  void enterLessonStep() {
    const LessonDef* def = lessonDef(tutorial_.lesson - 1);
    if (!def) return;
    LessonsProgress p = readLessonsProgress();
    p.at[def->id] = tutorial_.step;
    writeLessonsProgress(p);
    if (const LessonStep* s = currentLessonStep()) {
      if (s->enter) s->enter();
    }
    // The face drops in on the second step and on the one before the end.
    static const char* kHello[] = {
      "Psst. It's me, the desk. I'll be watching. Supportively.",
      "Hi! I'm the one doing all of this, by the way.",
      "Ctrl+D opens the DASH. I'm just saying.",
    };
    static const char* kNearly[] = {
      "Nearly there. I can feel it in my pixels.",
      "You're good at this. I can tell. I'm the one doing it.",
      "Every click, I heard. Every single one.",
    };
    const int count = tutorialStepCount();
    if (tutorial_.step == 1) faceDropIn(kHello[faceDrops_ % 3]);
    else if (count > 3 && tutorial_.step == count - 2) faceDropIn(kNearly[faceDrops_ % 3]);
  }

  // A target on the settings page's scrolled Video Outputs content.
  bool lessonRectInSettingsVideo(const SDL_Rect& r) const {
    const SDL_Rect& v = settingsVideoViewport_;
    return settingsOpen_ && settingsTab_ == 3 && v.w > 0 && r.w > 0 && r.x >= v.x && r.x + r.w <= v.x + v.w;
  }
  bool lessonRectInsideSettingsVideo(const SDL_Rect& r) const {
    const SDL_Rect& v = settingsVideoViewport_;
    return r.y >= v.y && r.y + r.h <= v.y + v.h;
  }

  // A target in the inspector's column: in its scrolled viewport, or not.
  bool lessonRectInInspectorColumn(const SDL_Rect& r) const {
    const SDL_Rect& v = cueSettingsViewportRect_;
    if (v.w <= 0 || r.w <= 0 || r.x < v.x || r.x + r.w > v.x + v.w) return false;
    // The bars' own buttons (MENU, DASH...) sit straight below the inspector
    // and are never scrolled. Counted as inspector rows, they looked
    // scrolled out and lost their pointer: twelve steps in eight lessons
    // pointed at nothing (2026-11-07).
    for (const auto& button : buttons_) {
      if (SDL_RectsEqual(&button.rect, &r)) return false;
    }
    return true;
  }

  void serviceLesson() {
    // Clicking all round the lit control: the face has something to say.
    if (tutorial_.missClicks == 3 && SDL_GetTicks() >= faceDropUntilMs_) {
      faceDropIn("The glowing one. I can see it from here. It's glowing for you.");
      ++tutorial_.missClicks;
    }
    if (superDeckboyEvolveAt_ != 0 && superDeckboyEvolveAt_ != lessonEvolveSeen_) {
      lessonEvolveSeen_ = superDeckboyEvolveAt_;
      ++lessonQuips_;
      lessonQuipUntilMs_ = SDL_GetTicks() + 6000;
    }
    const LessonStep* s = currentLessonStep();
    if (s && s->done && s->done()) {
      advanceTutorial();
      return;
    }
    // The control a step is about can sit below the inspector's fold (the
    // MASK section under a tall CODE one). Bring it into view, a few frames
    // at most, then leave the scroll to the operator -- as part one does.
    const SDL_Rect target = s && s->target ? s->target() : SDL_Rect {};
    if (lessonRectInInspectorColumn(target) && !tutorialRectInsideInspector(target) && tutorial_.inspectorNudges < 12) {
      const SDL_Rect& v = cueSettingsViewportRect_;
      const int margin = uiScaled(24);
      const int delta = target.y + target.h > v.y + v.h ? (target.y + target.h) - (v.y + v.h) + margin
                                                         : target.y - v.y - margin;
      cueSettingsScroll_ = std::clamp(cueSettingsScroll_ + delta, 0, std::max(0, cueSettingsScrollMax_));
      ++tutorial_.inspectorNudges;
    }
    // A settings control not on screen yet: page down until it registers,
    // and from the bottom start again at the top. Paging only down never
    // found a control ABOVE the view: the step before TWO SOFT EDGES leaves
    // Geometry scrolled to WARP, below EDGE BLEND (2026-11-07).
    if (s && s->seekSetting && settingsOpen_ && settingsTab_ == 3 && lessonSettingRect(s->seekSetting).w == 0 &&
        tutorial_.inspectorNudges < 24) {
      if (settingsVideoScroll_ < settingsVideoScrollMax_) {
        settingsVideoScroll_ = std::min(settingsVideoScrollMax_,
                                        settingsVideoScroll_ + std::max(uiScaled(60), settingsVideoViewport_.h / 2));
      } else {
        settingsVideoScroll_ = 0;
      }
      ++tutorial_.inspectorNudges;
    }
    // The same for a control further down a settings page (warp edit sits
    // below EDGE BLEND and MATTE on Geometry).
    if (lessonRectInSettingsVideo(target) && !lessonRectInsideSettingsVideo(target) && tutorial_.inspectorNudges < 24) {
      const SDL_Rect& v = settingsVideoViewport_;
      const int margin = uiScaled(24);
      const int delta = target.y + target.h > v.y + v.h ? (target.y + target.h) - (v.y + v.h) + margin
                                                         : target.y - v.y - margin;
      settingsVideoScroll_ = std::clamp(settingsVideoScroll_ + delta, 0, std::max(0, settingsVideoScrollMax_));
      ++tutorial_.inspectorNudges;
    }
  }

  void lessonShowMe() {
    const LessonStep* s = currentLessonStep();
    if (!s || !s->showMe) {
      advanceTutorial();
      return;
    }
    s->showMe();
  }

  SDL_Rect lessonTargetRect() const {
    const LessonStep* s = currentLessonStep();
    const SDL_Rect r = s && s->target ? s->target() : SDL_Rect {};
    // Not pointed at while it is scrolled out of the inspector's view.
    if (lessonRectInInspectorColumn(r) && !tutorialRectInsideInspector(r)) return SDL_Rect {};
    if (lessonRectInSettingsVideo(r) && !lessonRectInsideSettingsVideo(r)) return SDL_Rect {};
    return r;
  }

  void endLesson(bool finished) {
    const LessonDef* def = lessonDef(tutorial_.lesson - 1);
    tutorial_.active = false;
    closeDropdown(false);
    if (def && finished) markLessonDone(def->id);
    if (projectDirty_ && project_.loadedCleanly && !soakMode_) saveProjectNow(false);
    if (screensOpen_) setScreensVisible(false);
    warpEditMode_ = false;
    if (drawEditor_.open) openDrawEditor(false);
    if (maskEditor_.open) openMaskEditor(false);
    stopAllDecksForLesson();
    handDeskBack({tutorial_.returnTo, tutorial_.outputsWere, tutorial_.dimmerWas});
    tutorial_.lesson = 0;
    if (finished) {
      lessonsListOpen_ = true;
      triggerToast(std::string("lesson done: ") + (def ? def->title : "") + ". Your show is back", ToastKind::Help);
    } else {
      triggerToast("lesson paused - MENU > LESSONS picks up where you left off", ToastKind::Help);
    }
  }

  // ── The list ─────────────────────────────────────────────────────────────

  void openLessonsList() {
    if (tutorial_.active || bugSwat_.active) return;
    const LessonsProgress p = readLessonsProgress();
    lessonsTrackVideo_ = p.trackVideo;
    settingsOpen_ = false;
    lessonsListOpen_ = true;
  }

  bool handleLessonsListClick(int x, int y) {
    if (!lessonsListOpen_) return false;
    if (pointInRect(x, y, lessonsCloseRect_) || !pointInRect(x, y, lessonsCardRect_)) {
      lessonsListOpen_ = false;
    } else if (pointInRect(x, y, lessonsPixelTabRect_)) {
      chooseLessonsTrack(false);
      playUiSound(UiSoundEffect::Navigate);
    } else if (pointInRect(x, y, lessonsVideoTabRect_)) {
      chooseLessonsTrack(true);
      playUiSound(UiSoundEffect::Navigate);
    } else if (pointInRect(x, y, lessonsBasicsRect_)) {
      lessonsListOpen_ = false;
      beginTutorial();
    } else {
      for (const auto& [rect, index] : lessonsRowRects_) {
        if (pointInRect(x, y, rect)) {
          beginLesson(index);
          break;
        }
      }
    }
    return true;
  }

  bool handleLessonsListKey(SDL_Keycode key) {
    if (!lessonsListOpen_) return false;
    if (key == SDLK_ESCAPE) lessonsListOpen_ = false;
    return true;
  }

  void renderLessonsList(int width, int height) {
    if (!lessonsListOpen_) return;
    const auto& defs = lessonDefs();
    const LessonsProgress progress = readLessonsProgress();
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 6, 22, 6, 170);
    SDL_FRect all {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
    SDL_RenderFillRect(controlRenderer_, &all);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    TTF_Font* titleFont = fontPixelSmall_ ? fontPixelSmall_ : fontSmall_;
    const int pad = uiScaled(14);
    const int cardW = std::min(width - uiScaled(32), uiScaled(820));
    const int tabH = uiScaled(72);
    const int rowH = uiScaled(54);
    const int lineH = textLineHeight(fontBase_);
    std::vector<int> rows;
    for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
      if (defs[static_cast<std::size_t>(i)].videoTrack == lessonsTrackVideo_) rows.push_back(i);
    }
    const std::string intro = lessonsTrackVideo_
      ? "PROF. VIDEO:  The signal: what the picture is made of, and how to bend it. Pick a lesson."
      : "DR. PIXEL:  Running the show, skapi. Fast hands, calm face. Pick a lesson.";
    const auto introLines = codeWrapText(fontBase_, lessonText(intro), cardW - pad * 2);
    const int headH = std::max(uiScaled(22), textLineHeight(titleFont));
    const int buttonH = uiScaled(40);
    const int cardH = pad + headH + uiScaled(10) + tabH + uiScaled(10) + static_cast<int>(introLines.size()) * lineH +
                      uiScaled(10) + static_cast<int>(rows.size()) * (rowH + uiScaled(6)) + uiScaled(8) + buttonH + pad;
    const SDL_Rect card {(width - cardW) / 2, std::max(uiScaled(8), (height - cardH) / 2), cardW, cardH};
    lessonsCardRect_ = card;
    Primitives::drawFramedPanel(controlRenderer_, card, pal.shellInner, pal.deep, pal.light);
    int y = card.y + pad;
    drawTextSafe(controlRenderer_, titleFont, SDL_Rect {card.x + pad, y, cardW - pad * 2, headH},
                 lessonText("LESSONS  -  PART TWO  -  WHO DO YOU FOLLOW?"), pal.inkSoft);
    y += headH + uiScaled(10);

    // The two teachers, as tabs. The one followed is lit.
    const int tabW = (cardW - pad * 2 - uiScaled(10)) / 2;
    for (int t = 0; t < 2; ++t) {
      const bool video = t == 1;
      const bool lit = video == lessonsTrackVideo_;
      const SDL_Rect tab {card.x + pad + t * (tabW + uiScaled(10)), y, tabW, tabH};
      (video ? lessonsVideoTabRect_ : lessonsPixelTabRect_) = tab;
      Primitives::drawFramedPanel(controlRenderer_, tab, lit ? pal.light : pal.tile, pal.deep, pal.mid);
      const int portrait = tabH - uiScaled(12);
      drawTutorialPortrait(SDL_Rect {tab.x + uiScaled(6), tab.y + uiScaled(6), portrait, portrait}, !video, false);
      const SDL_Color ink = lit ? pal.deep : pal.fg;
      const int tx = tab.x + portrait + uiScaled(16);
      drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {tx, tab.y + uiScaled(10), tab.w - (tx - tab.x) - uiScaled(6), lineH},
                   video ? "FOLLOW PROF. VIDEO" : "FOLLOW DR. PIXEL", ink);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {tx, tab.y + uiScaled(12) + lineH, tab.w - (tx - tab.x) - uiScaled(6), textLineHeight(fontSmall_)},
                   video ? "the signal" : "running the show", ink);
    }
    y += tabH + uiScaled(10);
    for (const auto& text : introLines) {
      drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {card.x + pad, y, cardW - pad * 2, lineH}, text, pal.fg);
      y += lineH;
    }
    y += uiScaled(10);

    lessonsRowRects_.clear();
    for (int index : rows) {
      const LessonDef& def = defs[static_cast<std::size_t>(index)];
      const SDL_Rect row {card.x + pad, y, cardW - pad * 2, rowH};
      lessonsRowRects_.push_back({row, index});
      Primitives::drawFramedPanel(controlRenderer_, row, pal.tile, pal.deep, pal.mid);
      const bool done = progress.done.count(def.id) > 0;
      const bool started = !done && progress.at.count(def.id) > 0 && progress.at.at(def.id) > 0;
      const char* tag = done ? "DONE" : started ? "PICK UP" : "NEW";
      const int chipW = measuredTextWidth(titleFont, tag) + uiScaled(20);
      const SDL_Rect chip {row.x + row.w - chipW - uiScaled(10), row.y + (rowH - uiScaled(26)) / 2, chipW, uiScaled(26)};
      Primitives::fillRect(controlRenderer_, chip, done ? pal.light : SDL_Color {255, 220, 0, 255});
      drawCenteredText(controlRenderer_, titleFont, tag, SDL_Color {15, 56, 15, 255}, chip);
      const int textW = chip.x - row.x - uiScaled(20);
      drawTextSafe(controlRenderer_, fontBase_, SDL_Rect {row.x + uiScaled(10), row.y + uiScaled(6), textW, lineH},
                   lessonText(def.title) + (def.show && *def.show ? std::string("   -   ") + lessonText(def.show) : std::string()),
                   pal.fg);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {row.x + uiScaled(10), row.y + uiScaled(8) + lineH, textW, textLineHeight(fontSmall_)},
                   def.about, pal.fgSoft);
      y += rowH + uiScaled(6);
    }
    y += uiScaled(2);

    auto button = [&](SDL_Rect& out, int right, const char* label) {
      const int w = std::max(uiScaled(96), measuredTextWidth(fontBase_, label) + uiScaled(28));
      out = SDL_Rect {right - w, y, w, buttonH};
      Primitives::drawFramedPanel(controlRenderer_, out, pal.dark, pal.deep, pal.mid);
      drawCenteredText(controlRenderer_, fontBase_, label, pal.light, out);
      return out.x;
    };
    int right = card.x + cardW - pad;
    right = button(lessonsCloseRect_, right, "CLOSE (Esc)") - uiScaled(10);
    button(lessonsBasicsRect_, right, "PART ONE: THE BASICS");
  }

  // ── LESSONS over the wire ────────────────────────────────────────────────
  // LESSONS [LIST|OPEN|CLOSE|START <id>|TRACK PIXEL|VIDEO|STATUS]
  void handleLessonsCommand(const std::vector<std::string>& parts) {
    const std::string arg = parts.size() > 1 ? toUpper(parts[1]) : std::string("OPEN");
    if (arg == "OPEN") {
      if (tutorial_.active || bugSwat_.active) {
        failRemoteCommand("LESSONS: a lesson is running");
        return;
      }
      openLessonsList();
    } else if (arg == "CLOSE") {
      lessonsListOpen_ = false;
    } else if (arg == "START") {
      const int index = parts.size() > 2 ? lessonIndex(toLower(parts[2])) : -1;
      if (index < 0) {
        std::string ids;
        for (const auto& def : lessonDefs()) ids += (ids.empty() ? "" : ", ") + std::string(def.id);
        failRemoteCommand("LESSONS START: expected one of " + ids);
        return;
      }
      std::string why;
      if (!beginLesson(index, &why)) {
        failRemoteCommand("LESSONS START: " + why);
        return;
      }
    } else if (arg == "TRACK") {
      const std::string who = parts.size() > 2 ? toUpper(parts[2]) : std::string();
      if (who != "PIXEL" && who != "VIDEO") {
        failRemoteCommand("LESSONS TRACK: PIXEL or VIDEO");
        return;
      }
      chooseLessonsTrack(who == "VIDEO");
    } else if (arg != "LIST" && arg != "STATUS") {
      failRemoteCommand("LESSONS: expected OPEN, CLOSE, START <id>, TRACK PIXEL|VIDEO, LIST or STATUS");
      return;
    }
    const LessonsProgress p = readLessonsProgress();
    std::string list;
    for (const auto& def : lessonDefs()) {
      const char* state = p.done.count(def.id) ? "done" : p.at.count(def.id) && p.at.at(def.id) > 0 ? "started" : "new";
      list += (list.empty() ? "" : " ") + std::string(def.id) + ":" + state;
    }
    const LessonDef* running = tutorial_.active && tutorial_.lesson > 0 ? lessonDef(tutorial_.lesson - 1) : nullptr;
    remoteCommandDetail_ = std::string("open=") + (lessonsListOpen_ ? "1" : "0") +
                           " track=" + (lessonsTrackVideo_ ? "video" : "pixel") +
                           " running=" + (running ? running->id : "none") + " " + list;
  }
