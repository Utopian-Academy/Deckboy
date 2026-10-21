  // ── LIVE CAPTIONS ──────────────────────────────────────────────────────────
  //
  // What is being said, on the chosen outputs, about two seconds behind. The
  // engine (core/live_captions.hpp) listens in its own process; this half
  // feeds it the audio the operator picked, draws what it hears into the
  // outputs' composite (so the recording, the stream, NDI and SDI carry it),
  // and answers the CAPTIONS LIVE commands and the CAPTIONS card.
  //
  // The audio is read from the same rings the outputs mux from, with this
  // reader's own positions, so listening takes nothing from a recording.

  deckboy::captioning::LiveCaptioner liveCaptioner_;
  std::map<int, std::uint64_t> liveCaptionsRead_;
  std::map<int, std::uint64_t> liveCaptionsInputRead_;
  std::string liveCaptionsTapKey_;       // the source the read positions belong to
  std::string liveCaptionsRunningModel_;  // what the running engine was started with
  bool liveCaptionsRunningTranscript_ = false;
  std::future<deckboy::captioning::Result> liveCaptionsDownload_;
  Uint64 liveCaptionsDownloadArmedMs_ = 0;
  Uint64 liveCaptionsLastFedMs_ = 0;
  std::string liveCaptionsError_;
  std::vector<std::int32_t> liveCaptionsMix_;

  struct LiveCaptionsSource {
    enum Kind { Input, Deck, Output } kind = Output;
    int index = 0;   // zero-based deck or output
  };

  static LiveCaptionsSource parseLiveCaptionsSource(const std::string& text) {
    LiveCaptionsSource source;
    const auto words = splitWhitespace(toLower(text));
    if (!words.empty() && words[0] == "input") {
      source.kind = LiveCaptionsSource::Input;
    } else if (words.size() == 2 && (words[0] == "deck" || words[0] == "output")) {
      source.kind = words[0] == "deck" ? LiveCaptionsSource::Deck : LiveCaptionsSource::Output;
      source.index = std::max(0, std::atoi(words[1].c_str()) - 1);
    }
    return source;
  }

  std::string liveCaptionsSourceLabel() const {
    const LiveCaptionsSource s = parseLiveCaptionsSource(project_.liveCaptionsSource);
    if (s.kind == LiveCaptionsSource::Input) {
      return "Audio input" + (project_.audioInputDeviceName.empty()
                                ? std::string() : " (" + project_.audioInputDeviceName + ")");
    }
    if (s.kind == LiveCaptionsSource::Deck) {
      const bool known = s.index < static_cast<int>(project_.decks.size());
      return (known ? project_.decks[static_cast<std::size_t>(s.index)].name
                    : "Deck " + std::to_string(s.index + 1)) + " programme";
    }
    return outputLabel(s.index) + " mix";
  }

  bool liveCaptionsListensToInput() const {
    return project_.liveCaptionsEnabled &&
           parseLiveCaptionsSource(project_.liveCaptionsSource).kind == LiveCaptionsSource::Input;
  }

  static std::string liveCaptionsLanguage(const std::string& model) {
    return model.size() > 3 && model.compare(model.size() - 3, 3, ".en") == 0 ? "en" : "auto";
  }

  // The order the "listen to" control steps through: the input, each deck,
  // each output's mix.
  std::vector<std::string> liveCaptionsSourceChoices() const {
    std::vector<std::string> choices {"input"};
    for (std::size_t d = 0; d < project_.decks.size(); ++d) choices.push_back("deck " + std::to_string(d + 1));
    for (std::size_t o = 0; o < project_.outputs.size(); ++o) choices.push_back("output " + std::to_string(o + 1));
    return choices;
  }

  void cycleLiveCaptionsSource() {
    const auto choices = liveCaptionsSourceChoices();
    auto at = std::find(choices.begin(), choices.end(), toLower(project_.liveCaptionsSource));
    project_.liveCaptionsSource = (at == choices.end() || at + 1 == choices.end()) ? choices.front() : *(at + 1);
    markProjectDirty();
    triggerToast("live captions listen to " + liveCaptionsSourceLabel());
  }

  // ON may need the model first: the same two presses as captions made from a
  // file, so nobody downloads 142 MB by brushing a button.
  bool setLiveCaptionsEnabled(bool on) {
    if (!on) {
      project_.liveCaptionsEnabled = false;
      liveCaptioner_.stop();
      markProjectDirty();
      triggerToast("live captions off");
      return true;
    }
    if (const char* why = deckboy::captioning::captionsNeedNewerSystem()) {
      failRemoteCommand(std::string("live captions: ") + why);
      return false;
    }
    if (deckboy::captioning::listenPath().empty()) {
      failRemoteCommand("live captions: deckboy-listen is not beside Deckboy - see the manual, Live captions");
      return false;
    }
    const std::string model = project_.captionsModel;
    if (!deckboy::captioning::modelPresent(model)) {
      if (liveCaptionsDownload_.valid()) {
        remoteCommandDetail_ = "still downloading the speech model";
        return true;
      }
      const Uint64 now = SDL_GetTicks();
      if (liveCaptionsDownloadArmedMs_ == 0 || now - liveCaptionsDownloadArmedMs_ > 8000) {
        liveCaptionsDownloadArmedMs_ = now;
        triggerToast("live captions need a one-time " +
                       std::to_string(deckboy::captioning::modelChoice(model).megabytes) +
                       " MB download (stays on this computer) - press again to fetch it",
                     ToastKind::Help, kToastReadableMs);
        remoteCommandDetail_ = "needs the model: ask again to download it";
        return true;
      }
      liveCaptionsDownloadArmedMs_ = 0;
      liveCaptionsDownload_ = std::async(std::launch::async, [model]() {
        return deckboy::captioning::downloadModel(model);
      });
      triggerToast("fetching the speech model - captions start when it arrives");
    }
    project_.liveCaptionsEnabled = true;
    liveCaptionsError_.clear();
    markProjectDirty();
    remoteCommandDetail_ = "on, listening to " + liveCaptionsSourceLabel();
    return true;
  }

  // SPEECH MODELS, fetched from Settings one at a time: its own slot, so an
  // extra model failing to arrive never switches off captions that are running.
  std::future<deckboy::captioning::Result> captionModelFetch_;
  std::string captionModelFetching_;

  void fetchCaptionModel(const std::string& model) {
    namespace cap = deckboy::captioning;
    const auto& choice = cap::modelChoice(model);
    if (cap::modelPresent(model)) {
      triggerToast(std::string(choice.label) + ": already on this computer");
      return;
    }
    if (captionModelFetch_.valid()) {
      triggerToast("still downloading " + std::string(cap::modelChoice(captionModelFetching_).label));
      return;
    }
    captionModelFetching_ = model;
    captionModelFetch_ = std::async(std::launch::async, [model]() { return cap::downloadModel(model); });
    triggerToast("downloading " + std::string(choice.label) + " (" + std::to_string(choice.megabytes) + " MB)");
  }

  // Main thread, every tick.
  void serviceLiveCaptions() {
    namespace cap = deckboy::captioning;
    if (captionModelFetch_.valid() &&
        captionModelFetch_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      const cap::Result got = captionModelFetch_.get();
      const std::string label = cap::modelChoice(captionModelFetching_).label;
      captionModelFetching_.clear();
      triggerToast(got.ok ? label + ": ready" : label + ": " + got.message,
                   got.ok ? ToastKind::Help : ToastKind::Warning, kToastReadableMs);
    }
    if (liveCaptionsDownload_.valid() &&
        liveCaptionsDownload_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      const cap::Result got = liveCaptionsDownload_.get();
      if (!got.ok) {
        liveCaptionsError_ = got.message;
        project_.liveCaptionsEnabled = false;
        triggerToast("live captions: " + got.message, ToastKind::Warning, kToastReadableMs);
      }
    }
    const std::string model = project_.captionsModel;
    const bool want = project_.liveCaptionsEnabled && !liveCaptionsDownload_.valid() &&
                      cap::modelPresent(model);
    // A new language or transcript setting takes a fresh engine.
    if (liveCaptioner_.running() &&
        (!want || model != liveCaptionsRunningModel_ ||
         project_.liveCaptionsTranscript != liveCaptionsRunningTranscript_)) {
      liveCaptioner_.stop();
    }
    if (want && !liveCaptioner_.running()) {
      fs::path transcript;
      if (project_.liveCaptionsTranscript) {
        const std::time_t now = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H%M%S", std::localtime(&now));
        std::error_code ec;
        fs::create_directories(deckboy::core::Paths::stateDir() / "captions", ec);
        transcript = deckboy::core::Paths::stateDir() / "captions" / (std::string("live ") + stamp + ".srt");
      }
      std::string why;
      if (!liveCaptioner_.start(cap::listenPath(), cap::modelPath(model), liveCaptionsLanguage(model),
                                transcript, why)) {
        liveCaptionsError_ = why;
        project_.liveCaptionsEnabled = false;
        triggerToast("live captions: " + why, ToastKind::Warning, kToastReadableMs);
        return;
      }
      liveCaptionsRunningModel_ = model;
      liveCaptionsRunningTranscript_ = project_.liveCaptionsTranscript;
      liveCaptionsTapKey_.clear();
      liveCaptionsError_.clear();
      if (!transcript.empty()) showLog("CAPTIONS", "live transcript: " + transcript.string());
    }
    if (!liveCaptioner_.running()) return;

    // The audio, from this reader's own place in the rings.
    const LiveCaptionsSource source = parseLiveCaptionsSource(project_.liveCaptionsSource);
    const bool fromInput = source.kind == LiveCaptionsSource::Input;
    std::vector<int> decks;
    if (source.kind == LiveCaptionsSource::Deck) decks = {source.index};
    else if (source.kind == LiveCaptionsSource::Output) decks = streamAudioDecksForOutput(source.index);
    const bool withInput = fromInput || (source.kind == LiveCaptionsSource::Output && project_.audioInputToProgram);
    const Uint64 now = SDL_GetTicks();
    if (project_.liveCaptionsSource != liveCaptionsTapKey_) {
      deckAudioRing_.primeEndPositions(decks, liveCaptionsRead_);
      inputAudioRing_.primeEndPositions({0}, liveCaptionsInputRead_);
      liveCaptionsTapKey_ = project_.liveCaptionsSource;
      liveCaptionsLastFedMs_ = now;
      return;
    }
    using Ring = deckboy::core::DeckAudioRing;
    std::size_t take = Ring::kUnlimited;
    if (!fromInput) take = deckAudioRing_.mixableSamples(decks, liveCaptionsRead_);
    if (withInput) take = std::min(take, inputAudioRing_.mixableSamples({0}, liveCaptionsInputRead_));
    if (take == Ring::kUnlimited || take == 0) {
      // Nothing is playing: that is silence, and the engine has to hear it to
      // know the speaker has stopped (it is what commits the last words).
      if (now - liveCaptionsLastFedMs_ >= 250) {
        const std::size_t frames = std::min<Uint64>(now - liveCaptionsLastFedMs_, 1000) * 48;
        liveCaptionsMix_.assign(frames * 2, 0);
        liveCaptioner_.feed(liveCaptionsMix_.data(), liveCaptionsMix_.size());
        liveCaptionsLastFedMs_ = now;
      }
      return;
    }
    take = std::min<std::size_t>(take, 48000 * 2 * 2);
    take -= take % 2;
    liveCaptionsMix_.assign(take, 0);
    if (!fromInput) deckAudioRing_.mixDecks(decks, liveCaptionsRead_, liveCaptionsMix_, take, project_.decks.size());
    if (withInput) inputAudioRing_.mixDecks({0}, liveCaptionsInputRead_, liveCaptionsMix_, take, 1);
    liveCaptioner_.feed(liveCaptionsMix_.data(), liveCaptionsMix_.size());
    liveCaptionsLastFedMs_ = now;
  }

  // For the card's status row and CAPTIONS LIVE STATUS.
  std::string liveCaptionsStatusText() const {
    if (liveCaptionsDownload_.valid()) return "fetching the speech model (once)";
    if (!project_.liveCaptionsEnabled) {
      return liveCaptionsError_.empty() ? std::string("off") : liveCaptionsError_;
    }
    if (!deckboy::captioning::modelPresent(project_.captionsModel)) {
      return "this language's model is not here yet - turn live captions off and on to fetch it";
    }
    const auto st = liveCaptioner_.status();
    if (!st.error.empty()) return st.error;
    if (!st.ready) return "starting the speech engine";
    if (st.behindSeconds <= 0.0) return "listening";  // nothing heard yet to time
    char text[96];
    std::snprintf(text, sizeof(text), "listening - %.1f s behind", st.behindSeconds);
    return text;
  }

  bool liveCaptionsShownOn(int outputIndex) const {
    return project_.liveCaptionsEnabled && outputIndex >= 0 && outputIndex < 32 &&
           (project_.liveCaptionsOutputs >> outputIndex & 1u) != 0;
  }

  // Into the output's composite, after the dimmer (captions stay readable in a
  // fade to black, which is when they are most needed) and before the house
  // frame. Rolling lines in a box of fixed width, so the box does not jump as
  // the words change; sure words in white, the unsure tail in grey.
  void drawLiveCaptions(int outputIndex, SDL_Renderer* renderer, int renderW, int renderH) {
    if (!liveCaptionsShownOn(outputIndex) || !liveCaptioner_.running() || !renderer) return;
    const auto words = liveCaptioner_.recentWords(80);
    if (words.empty()) return;
    const double scale = std::clamp(project_.liveCaptionsSize, 50, 200) / 100.0;
    // The cue captions' size: a line is 4.5% of the picture, here as a face
    // rendered at that size rather than a small one scaled up.
    const int pt = std::clamp(static_cast<int>(std::lround(renderH * 0.034 * scale)), 10, 220);
    TTF_Font* font = fontAtSize(fontBase_, pt);
    if (!font) font = fontBase_;
    const int lineH = std::max(12, textLineHeight(font));
    const int padX = lineH / 2, padY = lineH / 5;
    const int boxW = std::min(renderW - 2 * padX, static_cast<int>(renderW * 0.84));
    const int wrapW = boxW - 2 * padX;

    // Wrap, keeping where the unsure tail starts.
    struct Line { std::string sure, unsure; };
    std::vector<Line> lines(1);
    std::string current;
    for (const auto& w : words) {
      const std::string attempt = current.empty() ? w.text : current + " " + w.text;
      if (!current.empty() && measuredTextWidth(font, attempt) > wrapW) {
        lines.emplace_back();
        current = w.text;
      } else {
        current = attempt;
      }
      Line& line = lines.back();
      std::string& part = w.sure && line.unsure.empty() ? line.sure : line.unsure;
      part += (part.empty() ? "" : " ") + w.text;
    }
    const std::size_t shown = std::min<std::size_t>(lines.size(), std::clamp(project_.liveCaptionsLines, 1, 3));
    lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(shown));

    const int boxH = static_cast<int>(shown) * lineH + 2 * padY;
    const int margin = static_cast<int>(renderH * 0.06);
    const SDL_FRect box {static_cast<float>((renderW - boxW) / 2),
                         static_cast<float>(project_.liveCaptionsTop ? margin : renderH - boxH - margin),
                         static_cast<float>(boxW), static_cast<float>(boxH)};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(renderer, &box);
    const SDL_Color sureInk {255, 255, 255, 255};
    const SDL_Color unsureInk {150, 150, 150, 255};
    for (std::size_t i = 0; i < lines.size(); ++i) {
      float x = box.x + padX;
      const float y = box.y + padY + static_cast<float>(i * lineH);
      for (const auto& [text, ink] : {std::pair<std::string, SDL_Color>{lines[i].sure, sureInk},
                                      std::pair<std::string, SDL_Color>{lines[i].unsure, unsureInk}}) {
        if (text.empty()) continue;
        const std::string drawn = x > box.x + padX ? " " + text : text;  // the unsure tail after sure words
        const TextTextureEntry* t = cachedTextTexture(renderer, font, drawn, ink);
        if (!t || !t->texture) continue;
        const SDL_FRect dst {x, y, static_cast<float>(t->w), static_cast<float>(t->h)};
        SDL_RenderTexture(renderer, t->texture, nullptr, &dst);
        x += static_cast<float>(t->w);
      }
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  }
