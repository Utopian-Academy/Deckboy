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
    if (project_.focusedDeckIndex != deck || d.selectedIndex != index) return tutorialCueRowRect(deck, index);
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

    // ── Prof. Video: Effects, oscillators and colour (the art school crit) ─
    // James picked this show, 2026-11-05: Professor Gristle hates every
    // picture until it has a ~ on it.
    {
      LessonDef l;
      l.id = "effects";
      l.videoTrack = true;
      l.title = "Effects, oscillators and colour";
      l.about = "An effect, a ~ on its knob, and the colour picker.";
      l.show = "The art school crit, with Professor Gristle";
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
         "An art school, skapi. One student, one picture, one professor who hates everything. Press START.",
         "Effects change a picture; an oscillator keeps changing it. The professor only likes the second kind.",
         "PROF. GRISTLE", "A grey square on beige. I have seen more feeling in a parking ticket.",
         std::function<bool()> {}, nullptr, [] { return SDL_Rect {}; }, nullptr},
        {"AN EFFECT", nullptr,
         "In the inspector's EFFECTS section, + adds one. Edges is a good start: it draws the outlines.",
         "There are dozens. Every one runs on the CPU in a few milliseconds. Mostly.",
         "PROF. GRISTLE", "Do something. Anything. Even a mistake would be a decision.",
         anyEffect,
         [this] { handleRemoteCommand("FX ADD edges 1"); },
         [this] { return lessonQuickRect(QuickAction::EffectAdd); }, openEffects},
        {"A ~ ON ITS KNOB", nullptr,
         "Every control with a ~ beside it can follow an oscillator. Press one. The knob starts to breathe.",
         "A low-frequency oscillator: slow, so the eye can follow it. A quarter of a hertz is a four-second breath.",
         "PROF. GRISTLE", "It moves. It... moves. Go on.",
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
         "PROF. GRISTLE", "Not that colour. ...Actually, that colour.",
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
         "An effect, a ~ and a colour. The professor is crying, skapi. Good crying, I think.",
         "Stack a few. Each has its own ~ on every knob. That is how a look becomes a performance.",
         "PROF. GRISTLE", "It breathes. It glows. It is still a square. ...A-plus.",
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
         "COACH MARV", "Get me a picture and a pen and nobody gets hurt.", none, nullptr, noTarget, nullptr},
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
         "COACH MARV", "Beautiful. Now I need to circle something.",
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
        {"A DRAW CUE", nullptr,
         "In the new playlist: SOURCE, then Draw. Clear everywhere you have not drawn.",
         "A Draw cue is transparent outside the ink, so the synth shows through.",
         nullptr, nullptr,
         [this] { return lessonCountCues(lessonIsDraw) > 0; },
         [this] {
           if (project_.decks.size() < 2) addDeck();
           project_.focusedDeckIndex = static_cast<int>(project_.decks.size()) - 1;
           addGeneratorCue("draw");
         },
         [this] { return tutorialButtonRect("SOURCE"); }, nullptr},
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
         "COACH MARV", "Circle him! No, bigger. That's a cloud. Fine, circle the cloud.",
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
         "COACH MARV", "Same time next week. Bring a bigger pen.", none, nullptr, noTarget, nullptr},
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
         "AGENT HOLLIS", "That's a craft, Brandt. Look at the lights. LOOK at them.", none, nullptr, noTarget, nullptr},
        {"A MASK FROM THE LIBRARY", nullptr,
         "In the inspector's MASK section: mask, then 'new heart'. Any shape will do. The heart is funnier.",
         "The library has circles, stars, hearts. Each one is points you can move.",
         "AGENT BRANDT", "A heart? The Bureau redacts with black bars, Hollis.",
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
         "AGENT BRANDT", "The waveform doesn't lie, Hollis. Next time, the scopes.", none, nullptr, noTarget, nullptr},
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
         "A ghost, three curved windows, and a keeper who has not slept since 1974. Press START.",
         "Projection mapping: bending the picture to fit the building. Warp, blend, slices.",
         "KEEPER ROOK", "She only haunts on a Tuesday. It's Tuesday.", none, nullptr, noTarget, nullptr},
        {"THE OUTPUT'S GEOMETRY", nullptr,
         "MENU, then Video Outputs, then Geometry. Where an output's shape lives.",
         "Geometry: the projector-menu word for 'what shape is the picture'.",
         nullptr, nullptr,
         [this] { return settingsOpen_ && settingsTab_ == 3 && settingsVideoSubTab_ == 1; },
         [this] { debugOpenSettings(3, 1); },
         [this] { return settingsOpen_ ? SDL_Rect {} : tutorialButtonRect("MENU"); }, nullptr},
        {"PIN A CORNER", nullptr,
         "Press warp edit, then drag a corner on the programme monitor. The window is not square. Neither is she.",
         "A corner pin moves the four corners. Perspective keeps the lines straight, like a real window.",
         "KEEPER ROOK", "Left a bit. Her left. No, the ghost's left.",
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
         }, nullptr},
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
         "KEEPER ROOK", "She says the middle one makes her look pale.",
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
         "Warp, blend, slices. The keeper will finally sleep. The ghost will not.",
         "Slices have their own warp and mesh in the Screens window. And a ~ on the place and the turn.",
         "KEEPER ROOK", "Same again Tuesday.", none, nullptr, noTarget, nullptr},
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

    // ── Dr. Pixel: The multiview (the sports truck) ───────────────────────
    {
      LessonDef l;
      l.id = "multiview";
      l.videoTrack = false;
      l.title = "The multiview";
      l.about = "Windows, the ... menu, what each one shows, layouts, and tally.";
      l.show = "The sports truck, with Director Dot";
      l.setup = [this] {
        const char* gens[4] = {"swirl", "fireside", "pocket-test", "checkerboard-motion"};
        for (int d = 0; d < 4; ++d) {
          if (d > 0) addDeck();
          project_.focusedDeckIndex = d;
          addGeneratorCue(gens[d]);
          Deck& deck = focusedDeckMutable();
          if (!deck.cues.empty()) deck.cues.back().name = "CAM " + std::to_string(d + 1);
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
        {"THE SPORTS TRUCK", nullptr,
         "Four cameras, one director, and a truck that smells of hot dogs. Press START, skapi.",
         "In a truck you see every camera at once. That's the multiview.",
         "DIRECTOR DOT", "Headsets on. We're live in thirty. Where's my coffee.", none, nullptr, noTarget, nullptr},
        {"MULTI ON", nullptr,
         "MULTI, above the monitor: every playlist in its own window.",
         "One window per camera. Like a wall of oscilloscopes, but with football.",
         nullptr, nullptr,
         [this] { return project_.multiviewMode != 0; },
         [this] { handleRemoteCommand("MULTIVIEW ON"); },
         [this] { return multiviewBtnRect_; }, nullptr},
        {"WHAT'S NEXT", nullptr,
         "Add a window (+), then its ... menu: make it show NEXT. What goes live when you press TAKE.",
         "Every window has a ... button. It chooses what that window shows.",
         "DIRECTOR DOT", "I need to see what's coming before it comes. That's the whole job.",
         [tileShows] { return tileShows("next"); },
         [this] {
           handleRemoteCommand("MULTIVIEW WINDOW ADD");
           handleRemoteCommand("MULTIVIEW WINDOW " + std::to_string(project_.multiviewTiles.size()) + " NEXT");
         },
         chip, nullptr},
        {"A CLOCK", nullptr,
         "And one more window, showing the clock. Directors are always late. That is why.",
         "Time of day, big. Never trust a wristwatch in a truck.",
         nullptr, nullptr,
         [tileShows] { return tileShows("clock"); },
         [this] {
           handleRemoteCommand("MULTIVIEW WINDOW ADD");
           handleRemoteCommand("MULTIVIEW WINDOW " + std::to_string(project_.multiviewTiles.size()) + " CLOCK");
         },
         chip, nullptr},
        {"ONE BIG ONE", nullptr,
         "Any window's ... menu, then a layout: one big, the rest small.",
         "The big window is window one. Usually the programme. Usually.",
         "DIRECTOR DOT", "Make camera one big. It's the expensive one.",
         [this] { return project_.multiviewLayout != deckboy::multiview::Layout::Grid; },
         [this] { handleRemoteCommand("MULTIVIEW LAYOUT BIG"); },
         chip, nullptr},
        {"TAKE CAMERA 3", "ENTER",
         "Select CAM 3 in its playlist and TAKE. Watch its window.",
         "The red border is TALLY: the window that is on air.",
         "DIRECTOR DOT", "Ready three... TAKE three!",
         [this] { return project_.decks.size() > 2 && project_.decks[2].activeIndex >= 0; },
         [this] {
           if (project_.decks.size() <= 2 || project_.decks[2].cues.empty()) return;
           project_.focusedDeckIndex = 2;
           selectCueInDeck(2, 0, false, false);
           jumpSelectedCue();
         },
         [this] {
           if (project_.decks.size() <= 2 || project_.decks[2].cues.empty()) return SDL_Rect {};
           if (project_.focusedDeckIndex != 2 || project_.decks[2].selectedIndex != 0) return tutorialCueRowRect(2, 0);
           return tutorialButtonRect("TAKE");
         }, nullptr},
        {"THAT'S A WRAP", nullptr,
         "Windows, the ... menu, NEXT, the clock, a layout, tally. You can run a truck, skapi.",
         "A window can also show an output, with its warp and slices. Handy when it's a lighthouse.",
         "DIRECTOR DOT", "Good show, everybody. Somebody find my coffee.", none, nullptr, noTarget, nullptr},
      };
      lessons_.push_back(std::move(l));
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
    return v.w > 0 && r.w > 0 && r.x >= v.x && r.x + r.w <= v.x + v.w;
  }

  void serviceLesson() {
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
    // A settings control not on screen yet: page down until it registers.
    if (s && s->seekSetting && settingsOpen_ && settingsTab_ == 3 && lessonSettingRect(s->seekSetting).w == 0 &&
        settingsVideoScroll_ < settingsVideoScrollMax_ && tutorial_.inspectorNudges < 24) {
      settingsVideoScroll_ = std::min(settingsVideoScrollMax_,
                                      settingsVideoScroll_ + std::max(uiScaled(60), settingsVideoViewport_.h / 2));
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
    const auto introLines = codeWrapText(fontBase_, intro, cardW - pad * 2);
    const int headH = std::max(uiScaled(22), textLineHeight(titleFont));
    const int buttonH = uiScaled(40);
    const int cardH = pad + headH + uiScaled(10) + tabH + uiScaled(10) + static_cast<int>(introLines.size()) * lineH +
                      uiScaled(10) + static_cast<int>(rows.size()) * (rowH + uiScaled(6)) + uiScaled(8) + buttonH + pad;
    const SDL_Rect card {(width - cardW) / 2, std::max(uiScaled(8), (height - cardH) / 2), cardW, cardH};
    lessonsCardRect_ = card;
    Primitives::drawFramedPanel(controlRenderer_, card, pal.shellInner, pal.deep, pal.light);
    int y = card.y + pad;
    drawTextSafe(controlRenderer_, titleFont, SDL_Rect {card.x + pad, y, cardW - pad * 2, headH},
                 "LESSONS  -  PART TWO  -  WHO DO YOU FOLLOW?", pal.inkSoft);
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
                   std::string(def.title) + (def.show && *def.show ? std::string("   -   ") + def.show : std::string()),
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
