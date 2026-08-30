// ═══════════════════════════════════════════════════════════════════════════════
// app_code_editor.ipp — the live-coding editor
//
// The code source shipped with its expression on ONE inline row, in the
// inspector column, ellipsized. That is the right widget for a number and the
// wrong one for a program: you could not see the whole expression, could not
// put the caret anywhere but the end, and every character was the same colour,
// so a mistyped function name looked exactly like a correct one right up until
// the picture stopped changing.
//
// This is a proper editor, and it is deliberately BIG — it takes most of the
// window, because while it is open that is the job in hand. It carries:
//
//   - the text, wrapped, in the mono font, SYNTAX COLOURED
//   - a caret you can move, and place by clicking into the text
//   - the compile result, live, on the line under the field
//   - every variable and function as a chip that inserts itself, with its
//     meaning shown under the pointer
//   - the worked examples as a picker rather than a cycle
//
// The colouring comes from deckboy::code::highlight(), which reads the same
// tables the compiler reads — so it cannot fall out of step with the language,
// and a name shown in the "unknown" colour is exactly one the compiler will
// refuse.
// ═══════════════════════════════════════════════════════════════════════════════

  // A fixed palette on a fixed dark field.
  //
  // Every other surface here takes its colours from the theme and this one must
  // not. Syntax colour is MEANING: eight roles that shifted with the colourway
  // would say something different in every theme, and could not be held legible
  // across all 25 of the OLED terminals at once. So the field is its own well —
  // the same decision the boot console makes — and these are constant
  // everywhere.
  static SDL_Color codeSyntaxColour(deckboy::code::Syntax kind) {
    using deckboy::code::Syntax;
    switch (kind) {
      case Syntax::Number:   return SDL_Color {248, 194, 106, 255};   // warm
      case Syntax::Variable: return SDL_Color {126, 216, 255, 255};   // cool
      case Syntax::Function: return SDL_Color {186, 240, 150, 255};
      case Syntax::Operator: return SDL_Color {226, 226, 226, 255};
      case Syntax::Bracket:  return SDL_Color {170, 170, 186, 255};
      case Syntax::Comma:    return SDL_Color {255, 148, 196, 255};   // channel split
      case Syntax::Unknown:  return SDL_Color {255, 108, 96, 255};    // will not compile
      default:               return SDL_Color {200, 200, 200, 255};
    }
  }

// Worked expressions to start from.
//
// A blank box and a list of variables is a poor welcome. These are things
// worth putting on a screen, and each one shows a different corner of the
// language: interference between two waves, polar coordinates, a folded angle,
// a warp fed by another wave, two distance fields beating against each other.
// Every one was rendered and looked at before it went in here.
struct CodeExample { const char* name; const char* expression; };

static const std::vector<CodeExample>& codeExamples() {
  static const std::vector<CodeExample> kExamples = {
    // The first several NAME their values, because that is the shape the
    // language is now and the examples are how anybody finds that out. Each
    // one is also a reason to bother: a distance used three times is computed
    // once here and three times in the old one-line form.
    {"spotlight",
     "d = length(cx, cy);\n"
     "fall = exp(-d*d*4);\n"
     "fall, fall*0.7, fall*0.35"},
    {"rings",
     "d = length(cx, cy);\n"
     "ring = abs(sin(d*12 - t*2));\n"
     "ring, ring*0.5, 1-ring"},
    {"orbit",
     "ox = sin(t)*0.55;\n"
     "oy = cos(t*0.8)*0.4;\n"
     "d = length(cx-ox, cy-oy);\n"
     "glow = smoothstep(0.45, 0.0, d);\n"
     "glow, glow*0.35, 1-glow*0.6"},
    {"grid lines",
     "u = fract(x*8);\n"
     "v = fract(y*8);\n"
     "edge = min(min(u, 1-u), min(v, 1-v));\n"
     "line = smoothstep(0.09, 0.0, edge);\n"
     "line, line*0.6, 0.35"},
    {"two suns",
     "a1 = length(cx-0.45, cy);\n"
     "a2 = length(cx+0.45, cy);\n"
     "beat = abs(sin(a1*14-t) - sin(a2*14-t));\n"
     "beat, beat*0.4, 1-beat"},
    {"inside out",
     "d = length(cx, cy);\n"
     "band = if(fract(d*4 - t*0.5) < 0.5, 1, 0);\n"
     "band, 1-band, fract(a/pi*2)"},
    {"plasma",
     "sin(x*8+t)*0.5+0.5, sin(y*8+t*1.3)*0.5+0.5, sin((x+y)*8-t)*0.5+0.5"},
    {"tunnel",
     "fract(r*4-t), fract(a/pi*3+t*0.2), 1-r"},
    {"kaleido rings",
     "abs(sin(a*8+sin(r*6-t)*2)), fract(r*5-t*0.5), 0.4"},
    {"checker drift",
     "step(0.5,fract(x*8+sin(y*4+t)*0.3)), step(0.5,fract(y*8+t*0.2)), 0.6"},
    {"starburst",
     "abs(sin(a*12))*step(r,0.9), abs(sin(a*12+t))*0.6, r*0.5"},
    {"ripple grid",
     "sin(r*24-t*3)*0.5+0.5, fract(x*10), fract(y*10)"},
    {"interference",
     "abs(sin(r*20-t*2)), abs(sin(sqrt((cx-0.4)^2+cy^2)*20-t*2)), 0.3"},
    {"warp bands",
     "fract(x*6+sin(y*3+t)*0.8), 0.5, fract(y*6-sin(x*3-t)*0.8)"},
    {"vortex",
     "fract(a/pi*4+r*6-t), r, 1-fract(r*3+t*0.3)"},
    {"pulse",
     "abs(sin(t))*step(r,0.7), r*abs(cos(t*0.7)), fract(a/pi*2)"},
  };
  return kExamples;
}

  bool codeEditorOpen() const { return codeEditor_.open; }

  // Where every character sits, and where the caret sits after the last one.
  //
  // Returns cells[i] = (column, row) for character i, plus one extra entry for
  // the end of the text so the caret always has somewhere to be. A newline
  // ends its row and is itself given the cell it occupies, so clicking just
  // past the end of a line puts the caret before the break rather than after
  // it.
  //
  // Shared by the drawing, the caret and click-to-place. They used to each
  // divide by the column count themselves, which agreed only for as long as
  // the answer was i / cols.
  std::vector<std::pair<int, int>> codeEditorCells(const std::string& text,
                                                   int cols) const {
    std::vector<std::pair<int, int>> cells;
    cells.reserve(text.size() + 1);
    int col = 0;
    int row = 0;
    for (char ch : text) {
      cells.emplace_back(col, row);
      if (ch == 0x0A) {
        col = 0;
        ++row;
        continue;
      }
      ++col;
      if (cols > 0 && col >= cols) {
        col = 0;
        ++row;
      }
    }
    cells.emplace_back(col, row);
    return cells;
  }

  // Break a line to a width. The label helpers all ellipsize -- which is right
  // for a control whose text must not change the layout, and wrong for the
  // friend, whose whole job is to finish a sentence.
  std::vector<std::string> codeWrapText(TTF_Font* font, const std::string& text,
                                        int maxWidth) const {
    std::vector<std::string> lines;
    if (!font || text.empty() || maxWidth <= 0) {
      return lines;
    }
    std::string line;
    std::size_t i = 0;
    while (i <= text.size()) {
      const std::size_t space = text.find(' ', i);
      const std::string word = text.substr(i, space == std::string::npos
                                                ? std::string::npos : space - i);
      const std::string candidate = line.empty() ? word : line + " " + word;
      int w = 0;
      TTF_GetStringSize(font, candidate.c_str(), 0, &w, nullptr);
      if (w > maxWidth && !line.empty()) {
        lines.push_back(line);
        line = word;
      } else {
        line = candidate;
      }
      if (space == std::string::npos) break;
      i = space + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
  }

  // The same measure for the examples, which carry a different struct. Two
  // small functions rather than one template: the call sites read better and
  // there are exactly two of them.
  int codeChipRows2(const std::vector<CodeExample>& entries, int width) const {
    int rows = 1;
    int used = 0;
    for (const auto& entry : entries) {
      int textW = 0;
      TTF_GetStringSize(fontSmall_, entry.name, 0, &textW, nullptr);
      const int chipW = textW + 20;
      if (used > 0 && used + chipW > width) {
        ++rows;
        used = 0;
      }
      used += chipW + 6;
    }
    return rows;
  }

  // How many rows a set of chips needs at a given width.
  //
  // Measured BEFORE anything is drawn, because the panel has to be tall enough
  // for its contents and it is drawn first. Sized to a fixed clamp instead, it
  // came out with a third of its height empty under the controls.
  int codeChipRows(const std::vector<deckboy::code::LanguageEntry>& entries,
                   int width) const {
    int rows = 1;
    int used = 0;
    for (const auto& entry : entries) {
      int textW = 0;
      TTF_GetStringSize(fontSmall_, entry.name, 0, &textW, nullptr);
      const int chipW = textW + 18;
      if (used > 0 && used + chipW > width) {
        ++rows;
        used = 0;
      }
      used += chipW + 6;
    }
    return rows;
  }

  void openCodeEditor() {
    const Cue* cue = selectedCuePtr();
    if (!cue || !cueIsCodeSource(*cue)) {
      return;
    }
    closeDropdown(true);
    codeEditor_ = CodeEditorState {};
    codeEditor_.open = true;
    codeEditor_.text = cue->codeExpression;
    codeEditor_.caret = codeEditor_.text.size();
    codeEditor_.deckIndex = project_.focusedDeckIndex;
    codeEditor_.cueIndex = focusedDeck().selectedIndex;
    if (controlWindow_) {
      SDL_ShowWindow(controlWindow_);
      SDL_RaiseWindow(controlWindow_);
    }
    SDL_StartTextInput(controlWindow_);
    // The keystroke that opened this is already in the queue; without the flush
    // it arrives as the first character typed.
    SDL_FlushEvent(SDL_EVENT_TEXT_INPUT);
    uiWatchdogPopupEvent("code_editor", true);
  }

  void closeCodeEditor(bool apply) {
    if (!codeEditor_.open) {
      return;
    }
    if (apply) {
      applyCodeEditorText();
    }
    codeEditor_ = CodeEditorState {};
    SDL_StopTextInput(controlWindow_);
    uiWatchdogPopupEvent("code_editor", false);
  }

  // Written through to the cue as it is typed, so the picture follows the
  // typing — which is the whole point of a live-coded source.
  //
  // A failure is NOT written back: the cue keeps the last expression that
  // compiled and the editor says what is wrong. Someone editing live is
  // mid-keystroke most of the time, and a source that blacks out on every
  // half-typed function is unusable on a stage.
  void applyCodeEditorText() {
    if (codeEditor_.deckIndex < 0 ||
        codeEditor_.deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[codeEditor_.deckIndex];
    if (codeEditor_.cueIndex < 0 ||
        codeEditor_.cueIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    if (!deckboy::code::compile(codeEditor_.text).ok()) {
      return;
    }
    deck.cues[codeEditor_.cueIndex].codeExpression = codeEditor_.text;
    markProjectDirty();
  }

  void codeEditorInsert(const std::string& text, int caretBackFromEnd = 0) {
    if (codeEditor_.text.size() + text.size() > kCodeEditorMaxChars) {
      triggerToast("expression is at its limit");
      return;
    }
    codeEditor_.caret = std::min(codeEditor_.caret, codeEditor_.text.size());
    codeEditor_.text.insert(codeEditor_.caret, text);
    codeEditor_.caret += text.size() - static_cast<std::size_t>(caretBackFromEnd);
    applyCodeEditorText();
  }

  bool handleCodeEditorKey(SDL_Keycode key, Uint16 mod) {
    if (!codeEditor_.open) {
      return false;
    }
    const std::size_t len = codeEditor_.text.size();
    codeEditor_.caret = std::min(codeEditor_.caret, len);
    switch (key) {
      case SDLK_ESCAPE:
        closeCodeEditor(false);
        return true;
      case SDLK_RETURN:
      case SDLK_KP_ENTER:
        // SHIFT+ENTER breaks the line; ENTER applies and closes, as every
        // other editor here does.
        //
        // This used to be Enter-only, on the reasoning that a source was one
        // line by definition. That stopped being true when it gained
        // statements: a source that names three values and then uses them is
        // four lines of code crammed onto one, and unreadable for it. Text
        // input never delivers a newline of its own, so without this there is
        // no way to type one at all.
        if ((mod & SDL_KMOD_SHIFT) != 0) {
          codeEditorInsert("\n");
          return true;
        }
        closeCodeEditor(true);
        return true;
      case SDLK_LEFT:
        if (codeEditor_.caret > 0) --codeEditor_.caret;
        return true;
      case SDLK_RIGHT:
        if (codeEditor_.caret < len) ++codeEditor_.caret;
        return true;
      case SDLK_HOME:
        codeEditor_.caret = 0;
        return true;
      case SDLK_END:
        codeEditor_.caret = len;
        return true;
      case SDLK_BACKSPACE:
        if ((mod & SDL_KMOD_CTRL) != 0) {
          codeEditor_.text.erase(0, codeEditor_.caret);
          codeEditor_.caret = 0;
        } else if (codeEditor_.caret > 0) {
          codeEditor_.text.erase(codeEditor_.caret - 1, 1);
          --codeEditor_.caret;
        }
        applyCodeEditorText();
        return true;
      case SDLK_DELETE:
        if (codeEditor_.caret < len) {
          codeEditor_.text.erase(codeEditor_.caret, 1);
        }
        applyCodeEditorText();
        return true;
      default:
        return true;   // the editor owns the keyboard while it is open
    }
  }

  void handleCodeEditorTextInput(const std::string& text) {
    if (!codeEditor_.open || text.empty()) {
      return;
    }
    codeEditorInsert(text);
  }

  bool handleCodeEditorClick(int x, int y) {
    if (!codeEditor_.open) {
      return false;
    }
    if (pointInRect(x, y, codeEditor_.applyRect)) {
      closeCodeEditor(true);
      return true;
    }
    if (pointInRect(x, y, codeEditor_.cancelRect)) {
      closeCodeEditor(false);
      return true;
    }
    if (pointInRect(x, y, codeEditor_.clearRect)) {
      codeEditor_.text.clear();
      codeEditor_.caret = 0;
      return true;
    }
    // Click INTO the text to put the caret there. That the caret could only
    // ever sit at the end is half of what was wrong with the one-line field;
    // this is the other half of fixing it.
    if (pointInRect(x, y, codeEditor_.fieldRect) && codeEditorCellW_ > 0) {
      const int col = (x - (codeEditor_.fieldRect.x + kCodeFieldPad)) / codeEditorCellW_;
      const int row = (y - (codeEditor_.fieldRect.y + kCodeFieldPad)) / codeEditorLineH_;
      const auto cells = codeEditorCells(codeEditor_.text,
                                         std::max(1, codeEditorCols_));
      // The closest cell ON THAT ROW, so clicking past the end of a short line
      // lands at its end rather than somewhere on the line below.
      std::size_t best = cells.size() - 1;
      long long bestScore = -1;
      for (std::size_t i = 0; i < cells.size(); ++i) {
        const long long rowGap =
          std::llabs(static_cast<long long>(cells[i].second) - row);
        const long long colGap =
          std::llabs(static_cast<long long>(cells[i].first) - col);
        const long long score = rowGap * 10000 + colGap;
        if (bestScore < 0 || score < bestScore) {
          bestScore = score;
          best = i;
        }
      }
      codeEditor_.caret = best;
      return true;
    }
    for (std::size_t i = 0; i < codeEditor_.chipRects.size(); ++i) {
      if (pointInRect(x, y, codeEditor_.chipRects[i]) && i < codeEditor_.chips.size()) {
        const auto& chip = codeEditor_.chips[i];
        if (chip.callable) {
          // Inserted with its brackets and the caret left INSIDE them, because
          // what you want next is the argument.
          codeEditorInsert(std::string(chip.name) + "()", 1);
        } else {
          codeEditorInsert(chip.name);
        }
        return true;
      }
    }
    for (std::size_t i = 0; i < codeEditor_.exampleRects.size(); ++i) {
      if (pointInRect(x, y, codeEditor_.exampleRects[i])) {
        const auto& examples = codeExamples();
        if (i < examples.size()) {
          codeEditor_.text = examples[i].expression;
          codeEditor_.caret = codeEditor_.text.size();
          applyCodeEditorText();
          triggerToast(examples[i].name);
        }
        return true;
      }
    }
    return true;   // a click anywhere else stays in the editor
  }

  void renderCodeEditor() {
    if (!codeEditor_.open) {
      return;
    }
    int winW = 0, winH = 0;
    SDL_GetWindowSize(controlWindow_, &winW, &winH);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 168);
    SDL_Rect shade {0, 0, winW, winH};
    SDL_RenderFillRect(controlRenderer_, &shade);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    TTF_Font* mono = fontMono_ ? fontMono_ : fontSmall_;
    int cellW = 0, cellH = 0;
    if (mono) {
      TTF_GetStringSize(mono, "M", 0, &cellW, &cellH);
    }
    cellW = std::max(1, cellW);
    cellH = std::max(1, cellH);
    codeEditorCellW_ = cellW;
    codeEditorLineH_ = cellH + 4;

    const int panelW = std::clamp(winW - 160, 560, 1180);
    // How much of the width the friend takes, when there is room for them at
    // all. Below that the editor is the only thing that matters and they sit
    // this one out rather than crowd it.
    // Wider than it was, because the tip is now set in the base face and a
    // narrow column in a bigger font is a column of two-word lines.
    const int friendW = panelW >= 760 ? std::clamp(panelW / 4, 260, 360) : 0;
    const int bodyW = panelW - friendW - (friendW > 0 ? 12 : 0);

    // The panel is exactly as tall as what goes in it.
    //
    // Sized to a clamp instead, it stood a third empty below the controls,
    // which reads as something failing to load rather than as a layout. The
    // chip rows are the only part whose height depends on the width, so they
    // are measured here with the same arithmetic that lays them out.
    const int chipsW = bodyW - 32;
    // Tall enough for the LINES, not just for the characters.
    //
    // The height was the character count divided by the column count, which is
    // right for one long wrapped run and short by a line for every break in a
    // source that has them. A five-line source came out with three lines
    // visible and the rest below the bottom of the field.
    const int fieldCols = std::max(1, (bodyW - 32 - kCodeFieldPad * 2) / cellW);
    int fieldLines = 1;
    {
      int used = 0;
      for (char ch : codeEditor_.text) {
        if (ch == 0x0A) { ++fieldLines; used = 0; continue; }
        if (++used >= fieldCols) { ++fieldLines; used = 0; }
      }
    }
    const int fieldH = std::clamp(fieldLines + 2, 4, 16) * codeEditorLineH_;
    const int valueRows = codeChipRows(deckboy::code::languageVariables(), chipsW);
    const int fnRows = codeChipRows(deckboy::code::languageFunctions(), chipsW);
    const int exampleRows = codeChipRows2(codeExamples(), chipsW);
    const int contentH =
        58                                   // title and the how-to line
      + fieldH + 8
      + 26                                   // the compile result
      + (20 + valueRows * 26) + 2            // VALUES
      + (20 + fnRows * 26) + 2               // FUNCTIONS
      + 26                                   // the hint line
      + (20 + exampleRows * 26)              // EXAMPLES
      + 52;                                  // the buttons
    const int panelH = std::clamp(contentH, 360, std::max(360, winH - 100));
    SDL_Rect panel {(winW - panelW) / 2, (winH - panelH) / 2, panelW, panelH};
    codeEditor_.panelRect = panel;
    Primitives::drawFramedPanel(controlRenderer_, panel, pal.light, pal.deep, pal.mid);

    drawText(controlRenderer_, fontBase_, "CODE SOURCE", pal.deep,
             panel.x + 16, panel.y + 12);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {panel.x + 16, panel.y + 36, bodyW - 32, 18},
                 "name values with  d = ...;  end with one expression, or three "
                 "for red green blue  |  Shift+Enter: new line  |  Enter: apply",
                 pal.dark);

    // ── The field ────────────────────────────────────────────────────────────
    SDL_Rect field {panel.x + 16, panel.y + 58, bodyW - 32, fieldH};
    codeEditor_.fieldRect = field;
    Primitives::fillRect(controlRenderer_, field, SDL_Color {16, 17, 22, 255});
    Primitives::strokeRect(controlRenderer_, field, pal.mid);

    const int cols = std::max(1, (field.w - kCodeFieldPad * 2) / cellW);
    codeEditorCols_ = cols;
    // Laid out on a CHARACTER GRID rather than by measuring each run.
    //
    // The font is monospaced, so one advance measured once places every glyph,
    // the caret lands exactly between two characters, and a click maps back to
    // an index by dividing. Measuring run by run instead accumulates a
    // fraction of a pixel per run, and the caret slowly stops agreeing with the
    // text under it.
    const auto cells = codeEditorCells(codeEditor_.text, cols);
    const auto runs = deckboy::code::highlight(codeEditor_.text);
    for (const auto& run : runs) {
      const SDL_Color ink = codeSyntaxColour(run.kind);
      for (std::size_t i = run.begin; i < run.end && i < codeEditor_.text.size(); ++i) {
        const char ch = codeEditor_.text[i];
        if (ch == ' ' || ch == 0x0A) continue;
        const int col = cells[i].first;
        const int row = cells[i].second;
        const int gy = field.y + kCodeFieldPad + row * codeEditorLineH_;
        if (gy + codeEditorLineH_ > field.y + field.h) break;
        const char glyph[2] = {ch, '\0'};
        drawText(controlRenderer_, mono, glyph, ink,
                 field.x + kCodeFieldPad + col * cellW, gy);
      }
    }
    if ((animationNow_ / 450) % 2 == 0) {
      const std::size_t at = std::min(codeEditor_.caret, cells.size() - 1);
      const int col = cells[at].first;
      const int row = cells[at].second;
      SDL_Rect caret {field.x + kCodeFieldPad + col * cellW,
                      field.y + kCodeFieldPad + row * codeEditorLineH_,
                      std::max(2, cellW / 5), cellH};
      if (caret.y + caret.h <= field.y + field.h) {
        Primitives::fillRect(controlRenderer_, caret, SDL_Color {255, 232, 120, 255});
      }
    }

    // ── Does it compile ──────────────────────────────────────────────────────
    int y = field.y + field.h + 8;
    const deckboy::code::CompiledSource compiled =
      deckboy::code::compile(codeEditor_.text);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {panel.x + 16, y, bodyW - 32, 20},
                 compiled.ok()
                   ? std::string("compiles  —  ")
                       + (codeEditor_.text.find(',') == std::string::npos
                            ? "one expression: the same value in red, green and blue"
                            : "three expressions: red, green, blue")
                   : compiled.error,
                 compiled.ok() ? SDL_Color {150, 220, 140, 255}
                               : SDL_Color {236, 110, 96, 255});
    y += 26;

    // ── Helpers ──────────────────────────────────────────────────────────────
    codeEditor_.chipRects.clear();
    codeEditor_.chips.clear();
    std::string hoverHint;
    auto drawChipRow = [&](const char* heading,
                           const std::vector<deckboy::code::LanguageEntry>& entries) {
      drawText(controlRenderer_, fontSmall_, heading, pal.dark, panel.x + 16, y);
      y += 20;
      int cx = panel.x + 16;
      for (const auto& entry : entries) {
        int textW = 0;
        TTF_GetStringSize(fontSmall_, entry.name, 0, &textW, nullptr);
        const int chipW = textW + 18;
        if (cx + chipW > panel.x + bodyW - 16) {
          cx = panel.x + 16;
          y += 26;
        }
        SDL_Rect chip {cx, y, chipW, 22};
        const bool hover = pointInRect(mouseX_, mouseY_, chip);
        drawUIPanel(chip, hover ? pal.light : pal.tile, pal.deep, pal.mid);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, chip, entry.name,
                             hover ? pal.deep : pal.fg);
        codeEditor_.chipRects.push_back(chip);
        codeEditor_.chips.push_back(entry);
        if (hover) {
          hoverHint = std::string(entry.name) + " — " + entry.hint;
        }
        cx += chipW + 6;
      }
      y += 28;
    };
    drawChipRow("VALUES", deckboy::code::languageVariables());
    drawChipRow("FUNCTIONS", deckboy::code::languageFunctions());

    // What the name under the pointer MEANS, in place, rather than in a manual
    // somewhere else. When the friend is on screen they say it instead, so the
    // explanation comes from a face rather than from a status line.
    if (friendW == 0) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {panel.x + 16, y, bodyW - 32, 20}, hoverHint, pal.dark);
    }
    y += 26;

    // ── Examples, as a picker ────────────────────────────────────────────────
    //
    // They were one button that CYCLED: ten presses to reach the tenth, no way
    // back, and no way to know what you were about to get. A list you can read
    // and click is the same information without the guessing.
    drawText(controlRenderer_, fontSmall_, "EXAMPLES", pal.dark, panel.x + 16, y);
    y += 20;
    codeEditor_.exampleRects.clear();
    int ex = panel.x + 16;
    for (const auto& example : codeExamples()) {
      int textW = 0;
      TTF_GetStringSize(fontSmall_, example.name, 0, &textW, nullptr);
      const int exW = textW + 20;
      if (ex + exW > panel.x + bodyW - 16) {
        ex = panel.x + 16;
        y += 26;
      }
      SDL_Rect box {ex, y, exW, 22};
      const bool current = codeEditor_.text == example.expression;
      const bool hover = pointInRect(mouseX_, mouseY_, box);
      drawUIPanel(box, current ? pal.dark : (hover ? pal.light : pal.tile),
                  pal.deep, current ? pal.light : pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, box, example.name,
                           current ? pal.light : (hover ? pal.deep : pal.fg));
      codeEditor_.exampleRects.push_back(box);
      ex += exW + 6;
    }

    // ── Apply / Cancel / Clear ───────────────────────────────────────────────
    SDL_Rect applyRect {panel.x + bodyW - 194, panel.y + panel.h - 42, 88, 30};
    SDL_Rect cancelRect {panel.x + bodyW - 98, panel.y + panel.h - 42, 88, 30};
    SDL_Rect clearRect {panel.x + 16, panel.y + panel.h - 42, 88, 30};
    codeEditor_.applyRect = applyRect;
    codeEditor_.cancelRect = cancelRect;
    codeEditor_.clearRect = clearRect;
    drawUIPanel(applyRect, pal.dark, pal.deep, pal.light);
    drawUIPanel(cancelRect, pal.mid, pal.deep, pal.light);
    drawUIPanel(clearRect, pal.tile, pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, applyRect, "APPLY", pal.light);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, cancelRect, "CANCEL", pal.deep);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, clearRect, "CLEAR", pal.fg);

    // ── The friend ───────────────────────────────────────────────────────────
    //
    // The same face that waits in the empty program monitor, here to say what
    // things mean. A syntax reference is a wall of names; a character who tells
    // you what the one under your finger DOES is the same information from
    // someone rather than from a table, and it is a nicer thing to sit next to
    // while you are stuck.
    //
    // They are given their own dark well: the mascot inks itself in pal.light,
    // which is exactly the fill this panel uses, so on the panel itself they
    // would be invisible.
    if (friendW > 0) {
      SDL_Rect nook {panel.x + bodyW + 12, panel.y + 58,
                     friendW - 16, panel.h - 58 - 16};
      Primitives::fillRect(controlRenderer_, nook, SDL_Color {16, 17, 22, 255});
      Primitives::strokeRect(controlRenderer_, nook, pal.mid);

      // What they say, in order of what is most worth saying:
      //   the name under the pointer, then the error, then an idle line.
      std::string say = hoverHint;
      if (say.empty() && !compiled.ok()) {
        say = compiled.error;
      }
      if (say.empty()) {
        // Idle: nothing is wrong and nothing is hovered, so they offer
        // something to try rather than filling the space with a slogan.
        static const char* kIdle[] = {
          "t is seconds. put it in a sin and it moves",
          "commas split red, green and blue",
          "r is distance from the middle",
          "fract() of a rising number is a sawtooth",
          "step(0.5, x) makes a hard edge",
          "hover a name and i'll tell you what it does",
          "red names won't compile yet",
          "mix(a, b, x) fades one into the other",
        };
        const int n = static_cast<int>(sizeof(kIdle) / sizeof(kIdle[0]));
        say = kIdle[(animationNow_ / 5200) % n];
      }
      // WRAPPED, and drawn here rather than by the mascot.
      //
      // The mascot puts its tip on one centred line through a helper that
      // ellipsizes, which is right for a four-word hint in an empty monitor and
      // wrong for a sentence explaining what mix() does -- it came out as
      // "commas split red, gree...". So the face is given the space above and
      // the words are laid out underneath it.
      // BASE, not small. This is the one piece of prose in the editor -- it
      // explains what a function does while you are reaching for it -- and it
      // was set in the smallest face in the application, which made the thing
      // whose entire job is to be read the hardest thing on screen to read.
      TTF_Font* sayFont = fontBase_ ? fontBase_ : fontSmall_;
      const auto lines = codeWrapText(sayFont, say, nook.w - 20);
      const int lineH = std::max(16, textLineHeight(sayFont) + 2);
      const int sayH = static_cast<int>(lines.size()) * lineH + 8;
      SDL_Rect faceArea {nook.x, nook.y, nook.w, std::max(80, nook.h - sayH)};
      drawStartupMascot(faceArea, animationNow_, "");
      int sayY = faceArea.y + faceArea.h;
      for (const auto& line : lines) {
        // pal.light, matching the mascot above it. The well is a fixed near
        // black rather than a theme colour, so pal.fg -- which is a dark ink
        // on any light theme -- would have been invisible on exactly the
        // themes where the rest of the editor reads best.
        drawCenteredTextSafe(controlRenderer_, sayFont,
                             SDL_Rect {nook.x + 10, sayY, nook.w - 20, lineH},
                             line, pal.light);
        sayY += lineH;
      }
    }
  }
