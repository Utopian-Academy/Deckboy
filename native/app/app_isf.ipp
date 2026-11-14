// ============================================================================
// app_isf.ipp -- SHADERS AS A SOURCE (ISF).
//
// A shader cue is a pattern cue whose path is "pattern://isf:<file.fs>". The
// file's own inputs are the cue's controls (inspector rows, the SHADER remote
// verb); each is kept by name in Cue::isf. The picture is made on the graphics
// card (gpu_effects: SourcePlan::Isf), so a shader needs an output that runs
// GPU effects. See native/engine/isf.hpp for what is supported.
//
// Part of class App -- included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // A numeric parameter id for one component of one input: base + input * 4 +
  // component (a point has two, a float one).
  static constexpr int kIsfParamBase = 4000;
  static int isfParamId(int input, int comp) { return kIsfParamBase + input * 4 + comp; }
  static bool isfParamIs(NumericParam id) {
    const int v = static_cast<int>(id);
    return v >= kIsfParamBase && v < kIsfParamBase + kIsfMaxInputs * 4;
  }

  static bool isShaderCue(const Cue& cue) {
    return cue.kind == CueKind::Pattern && normalizePatternTypeId(cue.path) == "isf";
  }

  static std::shared_ptr<const deckboy::isf::Shader> shaderOfCue(const Cue& cue) {
    return deckboy::isf::loadShader(deckboy::isf::pathFromCuePath(cue.path));
  }

  // Shaders shipped with Deckboy, and the folder the operator's own live in.
  static dbfs::path shippedShaderFolder() { return Paths::dataDir() / "shaders"; }
  static dbfs::path userShaderFolder() { return Paths::stateDir() / "shaders"; }

  struct ShaderEntry {
    std::string name;
    std::string path;
  };

  std::vector<ShaderEntry> listShaders() const {
    std::vector<ShaderEntry> out;
    for (const dbfs::path& folder : {shippedShaderFolder(), userShaderFolder()}) {
      std::error_code ec;
      for (dbfs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".fs") continue;
        out.push_back({it->path().stem().string(), it->path().string()});
      }
    }
    std::sort(out.begin(), out.end(), [](const ShaderEntry& a, const ShaderEntry& b) { return a.name < b.name; });
    return out;
  }

  // A new shader cue on the focused playlist.
  void addShaderCue(const std::string& file) {
    addPatternCue("isf");
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) return;
    Cue& cue = deck.cues.back();
    if (!isShaderCue(cue)) return;
    cue.path = deckboy::isf::cuePathFor(file);
    cue.name = dbfs::path(file).stem().string();
    cue.color = {200, 90, 40, 255};
    markProjectDirty();
    const auto shader = shaderOfCue(cue);
    if (shader && !shader->problem.empty()) {
      triggerToast("shader: " + shader->problem, ToastKind::Warning, kToastReadableMs);
    }
  }

  // Point the selected shader cue at another file (or make a cue for it).
  bool loadShaderFile(const std::string& file) {
    std::error_code ec;
    if (!dbfs::is_regular_file(dbfs::u8path(file), ec)) return false;
    Cue* cue = selectedCueMutable();
    if (cue && isShaderCue(*cue)) {
      cue->path = deckboy::isf::cuePathFor(file);
      if (cue->name.empty() || cue->name == "Shader") cue->name = dbfs::path(file).stem().string();
      markProjectDirty();
    } else {
      addShaderCue(file);
    }
    return true;
  }

  void chooseShaderFile() {
    static const std::vector<SDL_DialogFileFilter> kFilters = {
      {"ISF shaders", "fs;vs;frag"},
      {"All files", "*"},
    };
    showOpenFileDialog(kFilters, /*allowMany=*/false, [this](std::vector<std::string> files) {
      if (!files.empty() && !loadShaderFile(files.front())) triggerToast("shader: cannot read that file", ToastKind::Warning);
    });
  }

  void openShaderFolder() {
    std::error_code ec;
    dbfs::create_directories(userShaderFolder(), ec);
    deckboy::platform::openExternalUrl(userShaderFolder().string());
  }

  void editShaderFile() {
    const Cue* cue = selectedCueMutable();
    if (!cue || !isShaderCue(*cue)) return;
    const std::string file = deckboy::isf::pathFromCuePath(cue->path);
    deckboy::isf::forgetShaders();   // the next look reads what the editor saved
    if (!deckboy::platform::openExternalUrl(file)) deckboy::platform::revealFileInFileManager(file);
  }

  // Word wrap to a pixel width, for a message under a row.
  std::vector<std::string> wrapForWidth(TTF_Font* font, const std::string& text, int width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t pos = 0;
    while (pos <= text.size()) {
      const std::size_t space = text.find(' ', pos);
      const std::string word = text.substr(pos, space == std::string::npos ? std::string::npos : space - pos);
      const std::string trial = line.empty() ? word : line + " " + word;
      int tw = 0;
      textSizeShown(font, trial.c_str(), 0, &tw, nullptr);
      if (tw > width && !line.empty()) {
        lines.push_back(line);
        line = word;
      } else {
        line = trial;
      }
      if (space == std::string::npos) break;
      pos = space + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
  }

  // ---- numeric rows: a float input's value, a point's two ----------------------
  const NumericParamSpec* isfParamSpec(NumericParam id) {
    static NumericParamSpec specs[kIsfMaxInputs * 4];
    static std::string texts[kIsfMaxInputs * 4][3];
    if (!isfParamIs(id)) return nullptr;
    const int n = static_cast<int>(id) - kIsfParamBase;
    const int input = n / 4, comp = n % 4;
    const Cue* cue = selectedCueMutable();
    if (!cue || !isShaderCue(*cue)) return nullptr;
    const auto shader = shaderOfCue(*cue);
    if (!shader || input >= static_cast<int>(shader->inputs.size())) return nullptr;
    const auto& in = shader->inputs[static_cast<std::size_t>(input)];
    if (in.type != deckboy::isf::InputType::Float && in.type != deckboy::isf::InputType::Point2D) return nullptr;
    if (comp >= in.comps) return nullptr;
    auto& t = texts[n];
    t[0] = "isf." + in.name + "." + std::to_string(comp);
    t[1] = in.label + (in.type == deckboy::isf::InputType::Point2D ? (comp == 0 ? " x" : " y") : "");
    char range[64];
    std::snprintf(range, sizeof(range), "%g to %g", static_cast<double>(in.mn[comp]), static_cast<double>(in.mx[comp]));
    t[2] = range;
    const double lo = in.mn[comp], hi = in.mx[comp];
    specs[n] = NumericParamSpec {t[0].c_str(), t[1].c_str(), t[2].c_str(), lo, hi > lo ? hi : lo + 1.0, 3, false,
                                 (hi - lo) / 50.0};
    return &specs[n];
  }

  bool readIsfParam(const Cue& cue, NumericParam id, double& out) {
    if (!isfParamIs(id) || !isShaderCue(cue)) return false;
    const int n = static_cast<int>(id) - kIsfParamBase;
    const auto shader = shaderOfCue(cue);
    if (!shader || n / 4 >= static_cast<int>(shader->inputs.size())) return false;
    const auto& in = shader->inputs[static_cast<std::size_t>(n / 4)];
    if (n % 4 >= in.comps) return false;
    const IsfSetting* set = deckboy::isf::findSetting(cue, in.name);
    out = set ? set->v[n % 4] : in.def[n % 4];
    return true;
  }

  void writeIsfParam(Cue& cue, NumericParam id, double value) {
    if (!isfParamIs(id) || !isShaderCue(cue)) return;
    const int n = static_cast<int>(id) - kIsfParamBase;
    const auto shader = shaderOfCue(cue);
    if (!shader || n / 4 >= static_cast<int>(shader->inputs.size())) return;
    const auto& in = shader->inputs[static_cast<std::size_t>(n / 4)];
    if (n % 4 >= in.comps) return;
    deckboy::isf::ensureSetting(cue, in).v[n % 4] =
      static_cast<float>(std::clamp<double>(value, in.mn[n % 4], std::max(in.mx[n % 4], in.mn[n % 4])));
  }

  // The oscillator behind a float row's ~ chip (the packed id is the input's index).
  deckboy::effects::ParamLfo* isfLfoAt(int input) {
    Cue* cue = selectedCueMutable();
    if (!cue || !isShaderCue(*cue)) return nullptr;
    const auto shader = shaderOfCue(*cue);
    if (!shader || input < 0 || input >= static_cast<int>(shader->inputs.size())) return nullptr;
    const auto& in = shader->inputs[static_cast<std::size_t>(input)];
    if (in.type != deckboy::isf::InputType::Float) return nullptr;
    return &deckboy::isf::ensureSetting(*cue, in).lfo;
  }

  // The few actions a shader's rows fire; the input's index rides in `param`.
  bool handleIsfQuickAction(QuickAction action, int param) {
    switch (action) {
      case QuickAction::IsfLoad: chooseShaderFile(); return true;
      case QuickAction::IsfFolder: openShaderFolder(); return true;
      case QuickAction::IsfEdit: editShaderFile(); return true;
      case QuickAction::IsfToggle:
      case QuickAction::IsfChoicePrev:
      case QuickAction::IsfChoiceNext: {
        Cue* cue = selectedCueMutable();
        if (!cue || !isShaderCue(*cue)) return true;
        const auto shader = shaderOfCue(*cue);
        if (!shader || param < 0 || param >= static_cast<int>(shader->inputs.size())) return true;
        const auto& in = shader->inputs[static_cast<std::size_t>(param)];
        auto& set = deckboy::isf::ensureSetting(*cue, in);
        if (action == QuickAction::IsfToggle) {
          set.v[0] = set.v[0] >= 0.5f ? 0.0f : 1.0f;
        } else if (!in.values.empty()) {
          int at = 0;
          for (std::size_t i = 0; i < in.values.size(); ++i)
            if (static_cast<float>(in.values[i]) == std::round(set.v[0])) at = static_cast<int>(i);
          const int n = static_cast<int>(in.values.size());
          at = (at + (action == QuickAction::IsfChoiceNext ? 1 : n - 1)) % n;
          set.v[0] = static_cast<float>(in.values[static_cast<std::size_t>(at)]);
        }
        markProjectDirty();
        return true;
      }
      case QuickAction::IsfColour: {
        Cue* cue = selectedCueMutable();
        if (!cue || !isShaderCue(*cue)) return true;
        const bool same = colourPicker_.open && colourPicker_.isfInput == param && colourPicker_.cue == cue;
        closeColourPicker();
        if (same) return true;
        colourPicker_.open = true;
        colourPicker_.cue = cue;
        colourPicker_.isfInput = param;
        colourPicker_.whole = true;
        colourPicker_.anchor = lastInlineEditorAnchorRect_;
        lastInlineEditorAnchorRect_ = SDL_Rect {};
        std::error_code madeEc;
        dbfs::create_directories(colourLibraryFolder(), madeEc);
        colourLibraries_ = deckboy::colours::loadColourLibraries(colourLibraryFolder());
        return true;
      }
      default: return false;
    }
  }

  // The cue's colour input the picker is on, or null.
  IsfSetting* isfColourTarget() {
    if (!colourPicker_.open || colourPicker_.isfInput < 0) return nullptr;
    Cue* cue = selectedCueMutable();
    if (!cue || cue != colourPicker_.cue || !isShaderCue(*cue)) return nullptr;
    const auto shader = shaderOfCue(*cue);
    if (!shader || colourPicker_.isfInput >= static_cast<int>(shader->inputs.size())) return nullptr;
    const auto& in = shader->inputs[static_cast<std::size_t>(colourPicker_.isfInput)];
    if (in.type != deckboy::isf::InputType::Color) return nullptr;
    return &deckboy::isf::ensureSetting(*cue, in);
  }

  static std::string isfValueText(const deckboy::isf::Input& in, const IsfSetting* set) {
    const float* v = set ? set->v : in.def;
    char text[64];
    switch (in.type) {
      case deckboy::isf::InputType::Float: std::snprintf(text, sizeof(text), "%.3g", static_cast<double>(v[0])); break;
      case deckboy::isf::InputType::Point2D:
        std::snprintf(text, sizeof(text), "%.3g %.3g", static_cast<double>(v[0]), static_cast<double>(v[1]));
        break;
      case deckboy::isf::InputType::Color:
        std::snprintf(text, sizeof(text), "#%02x%02x%02x", static_cast<int>(std::lround(v[0] * 255.0f)),
                      static_cast<int>(std::lround(v[1] * 255.0f)), static_cast<int>(std::lround(v[2] * 255.0f)));
        break;
      case deckboy::isf::InputType::Long: {
        std::string label = std::to_string(static_cast<int>(std::lround(v[0])));
        for (std::size_t i = 0; i < in.values.size(); ++i)
          if (static_cast<float>(in.values[i]) == std::round(v[0])) label = in.labels[i];
        return label;
      }
      default: return v[0] >= 0.5f ? "on" : "off";
    }
    return text;
  }

  // SHADER LOAD <file> | SET <input> <value...> | LFO <input> ... | STATUS.
  void handleShaderCommand(const std::vector<std::string>& parts) {
    const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : "STATUS";
    if (sub == "LOAD") {
      if (parts.size() < 3) {
        failRemoteCommand("SHADER LOAD <path to a .fs file>");
        return;
      }
      const std::string file = joinParts(parts, 2);
      if (!loadShaderFile(file)) {
        failRemoteCommand("SHADER LOAD: cannot read " + file);
        return;
      }
      const auto shader = deckboy::isf::loadShader(file);
      remoteCommandDetail_ = shader && !shader->problem.empty() ? "loaded, but " + shader->problem : "loaded " + file;
      return;
    }
    Cue* cue = selectedCueMutable();
    if (!cue || !isShaderCue(*cue)) {
      failRemoteCommand("select a shader cue first (SHADER LOAD <path> makes one)");
      return;
    }
    const auto shader = shaderOfCue(*cue);
    if (sub == "STATUS" && parts.size() <= 2) {
      std::ostringstream status;
      status << "shader file=" << deckboy::isf::pathFromCuePath(cue->path);
      if (!shader || !shader->problem.empty()) {
        status << " problem=" << (shader ? shader->problem : std::string("none")) << " inputs=0";
      } else {
        status << " ok inputs=" << shader->inputs.size();
        for (const auto& in : shader->inputs)
          status << ' ' << in.name << '=' << isfValueText(in, deckboy::isf::findSetting(*cue, in.name));
      }
      remoteCommandDetail_ = status.str();
      return;
    }
    if (!shader || !shader->ok()) {
      failRemoteCommand("this shader cannot run: " + (shader ? shader->problem : std::string("not loaded")));
      return;
    }
    if ((sub != "SET" && sub != "LFO") || parts.size() < 3) {
      failRemoteCommand("STATUS | LOAD <path> | SET <input> <value...> | LFO <input> ON|OFF|SHAPE|RATE|DEPTH|PHASE|SYNC [value]");
      return;
    }
    const deckboy::isf::Input* input = nullptr;
    int index = -1;
    for (std::size_t i = 0; i < shader->inputs.size(); ++i) {
      if (toLower(shader->inputs[i].name) == toLower(parts[2])) {
        input = &shader->inputs[i];
        index = static_cast<int>(i);
      }
    }
    if (!input) {
      std::string names;
      for (const auto& in : shader->inputs) names += (names.empty() ? "" : ", ") + in.name;
      failRemoteCommand("this shader has no input \"" + parts[2] + "\" (it has: " + (names.empty() ? "none" : names) + ")");
      return;
    }
    if (sub == "LFO") {
      deckboy::effects::ParamLfo* lfo = isfLfoAt(index);
      if (!lfo) {
        failRemoteCommand("LFO: only a float input has an oscillator");
        return;
      }
      if (setLfoFromRemote(*lfo, parts, 3, "LFO")) {
        markProjectDirty();
        remoteCommandDetail_ = input->name + (lfo->on ? " moving" : " still");
      }
      return;
    }
    // SET
    const std::size_t count = parts.size() - 3;
    auto number = [&](std::size_t at, float& out) {
      char* end = nullptr;
      const double v = std::strtod(parts[at].c_str(), &end);
      if (end == parts[at].c_str() || *end != '\0' || !std::isfinite(v)) return false;
      out = static_cast<float>(v);
      return true;
    };
    float v[4] = {0, 0, 0, 1};
    bool ok = false;
    std::string why;
    switch (input->type) {
      case deckboy::isf::InputType::Float:
        ok = count == 1 && number(3, v[0]);
        if (ok && (v[0] < input->mn[0] || v[0] > input->mx[0])) {
          ok = false;
          why = input->name + " runs from " + std::to_string(input->mn[0]) + " to " + std::to_string(input->mx[0]);
        }
        break;
      case deckboy::isf::InputType::Point2D:
        ok = count == 2 && number(3, v[0]) && number(4, v[1]);
        break;
      case deckboy::isf::InputType::Color: {
        if (count == 1 && parts[3].size() == 7 && parts[3][0] == '#') {
          const unsigned rgb = static_cast<unsigned>(std::strtoul(parts[3].c_str() + 1, nullptr, 16));
          v[0] = static_cast<float>((rgb >> 16) & 255) / 255.0f;
          v[1] = static_cast<float>((rgb >> 8) & 255) / 255.0f;
          v[2] = static_cast<float>(rgb & 255) / 255.0f;
          ok = true;
        } else if ((count == 3 || count == 4) && number(3, v[0]) && number(4, v[1]) && number(5, v[2]) &&
                   (count == 3 || number(6, v[3]))) {
          ok = std::all_of(v, v + 4, [](float c) { return c >= 0.0f && c <= 1.0f; });
          if (!ok) why = "colour channels run from 0 to 1";
        }
        break;
      }
      case deckboy::isf::InputType::Long: {
        if (count >= 1) {
          const std::string word = toLower(joinParts(parts, 3));
          for (std::size_t i = 0; i < input->values.size(); ++i)
            if (toLower(input->labels[i]) == word || std::to_string(input->values[i]) == word) {
              v[0] = static_cast<float>(input->values[i]);
              ok = true;
            }
          if (!ok) why = "not one of its choices";
        }
        break;
      }
      default: {
        const std::string word = count == 1 ? toUpper(parts[3]) : std::string();
        if (word == "ON" || word == "1" || word == "TRUE") { v[0] = 1.0f; ok = true; }
        else if (word == "OFF" || word == "0" || word == "FALSE") { v[0] = 0.0f; ok = true; }
        break;
      }
    }
    if (!ok) {
      failRemoteCommand("SET " + input->name + ": " + (why.empty() ? "the value does not fit this input" : why));
      return;
    }
    IsfSetting& set = deckboy::isf::ensureSetting(*cue, *input);
    for (int c = 0; c < 4; ++c) set.v[c] = v[c];
    markProjectDirty();
    remoteCommandDetail_ = input->name + "=" + isfValueText(*input, &set);
  }
