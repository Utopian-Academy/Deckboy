// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// THE EFFECT COLOUR PICKER
// ============================================================================
//
// James, 2026-10-30: "colour replace really need colour pickers", "make sure
// it can grab from any window, not just deckboy", "or be able to enter
// specific hex/pantone etc values". The design he approved ("love it"):
// https://claude.ai/artifact/ScAQvKx8E7nBfJZnVHVFNV
//
// A chip on every colour row of an effect opens this panel beside it. It sets
// one of two things (cue_effects.hpp, "THE COLOUR CHIPS"):
//   - a HUE slot (Colour's tint colour, Colour Replace's from and to): the
//     slot becomes the picked colour's hue, so its slider and LFO keep
//     working, and a grey -- which has no hue -- is refused with the reason;
//   - the effect's WHOLE colour (Strobe's flash, the Edges line).
//
// PICK FROM SCREEN is platform/screen_colour: any window on any display.
// Where the system draws no loupe of its own (Windows, X11) a small window
// follows the cursor with the 9x9 pixels under it, the centre ringed.
//
// Typed colours go through deckboy::colours::findColour: a hex, three
// numbers, a CSS name, a RAL number, or a name from the operator's own files
// in Paths::stateDir()/colours (where a Pantone table goes; none is bundled).

  struct ColourPickerState {
    bool open = false;
    const Cue* cue = nullptr;   // the cue it opened on: another selection closes it
    int effect = -1;            // index in that cue's stack
    int slot = -1;              // the parameter row the chip sits on
    bool whole = false;         // CueEffect::colour, not a hue slot
    bool backdrop = false;      // the cue's own backdrop (cueBackdropField), not an effect
    SDL_Rect anchor {};         // the chip that opened it
    SDL_Rect panel {};
    bool sampling = false;
    bool systemLoupe = false;
    bool hasPatch = false;
    std::array<std::uint32_t, 81> patch {};
    SDL_Window* loupeWindow = nullptr;
    SDL_Renderer* loupeRenderer = nullptr;
    struct Hit {
      SDL_Rect rect;
      int what;
      std::uint32_t colour;
    };
    std::vector<Hit> hits;
    SDL_Rect hueStrip {};
    std::optional<std::uint32_t> testPick;   // DECKBOY_TEST_SCREEN_PICK, while it stands in
  };
  ColourPickerState colourPicker_;
  std::vector<std::uint32_t> recentColours_;   // this session's picks, newest first
  std::vector<deckboy::colours::LibraryColour> colourLibraries_;

  enum ColourPickerHit {
    kColourHitClose,
    kColourHitScreen,
    kColourHitHue,
    kColourHitHex,
    kColourHitRgb,
    kColourHitDegrees,
    kColourHitNamed,
    kColourHitSwatch,
    kColourHitPanel,
  };

  static SDL_Color sdlColourOf(std::uint32_t rgb) {
    return SDL_Color {static_cast<Uint8>((rgb >> 16) & 0xFF), static_cast<Uint8>((rgb >> 8) & 0xFF),
                      static_cast<Uint8>(rgb & 0xFF), 255};
  }

  static std::string hexOfColour(std::uint32_t rgb) {
    char text[8];
    std::snprintf(text, sizeof(text), "#%06x", static_cast<unsigned>(rgb & 0xFFFFFF));
    return text;
  }

  // The chip's colour for a row, or nothing for a row that has none.
  std::optional<std::uint32_t> effectChipColour(const deckboy::effects::CueEffect& fx, int slot) const {
    if (deckboy::effects::cueEffectParamIsHue(fx.kind, slot)) {
      const float v = slot == 0 ? fx.paramA : slot == 1 ? fx.paramB : slot == 2 ? fx.paramC : fx.paramD;
      return deckboy::effects::hueColour(v);
    }
    if (deckboy::effects::cueEffectColourSlot(fx.kind) == slot) return fx.colour & 0xFFFFFF;
    return std::nullopt;
  }

  deckboy::effects::CueEffect* colourPickerEffect() {
    if (!colourPicker_.open || selectedCueMutable() != colourPicker_.cue) return nullptr;
    auto* stack = selectedEffectStack();
    if (!effectIndexValid(stack, colourPicker_.effect)) return nullptr;
    auto& fx = (*stack)[colourPicker_.effect];
    const bool fits = colourPicker_.whole
                        ? deckboy::effects::cueEffectColourSlot(fx.kind) == colourPicker_.slot
                        : deckboy::effects::cueEffectParamIsHue(fx.kind, colourPicker_.slot);
    return fits ? &fx : nullptr;
  }

  // WHERE A CUE KEEPS ITS BACKDROP, in the timer's encoding: -1 the kind's
  // default, kTimerBackdropTransparent for none, else 0xRRGGBB. Null for a
  // kind that has none. (Board H856G24w, James 2026-10-30: a solid colour to
  // key out, on every cue meant to sit over another.)
  int* cueBackdropField(Cue& cue) const {
    if (cue.kind == CueKind::Timer) return &cue.timer.colorBackground;
    return nullptr;
  }

  // The picker is still pointing at something that exists.
  bool colourPickerTargetValid() {
    if (!colourPicker_.open) return false;
    if (colourPicker_.backdrop) {
      Cue* cue = selectedCueMutable();
      return cue && cue == colourPicker_.cue && cueBackdropField(*cue);
    }
    return colourPickerEffect() != nullptr;
  }

  std::string colourPickerLabel() {
    if (colourPicker_.backdrop) return "backdrop";
    const deckboy::effects::CueEffect* fx = colourPickerEffect();
    const char* label = fx ? deckboy::effects::cueEffectParamLabel(fx->kind, colourPicker_.slot) : nullptr;
    return label ? label : "colour";
  }

  std::string colourPickerTitle() {
    if (colourPicker_.backdrop) {
      const Cue* cue = selectedCueMutable();
      return (cue ? cueKindLabel(cue->kind) : std::string("cue")) + " - backdrop";
    }
    const deckboy::effects::CueEffect* fx = colourPickerEffect();
    return fx ? std::string(deckboy::effects::cueEffectLabel(fx->kind)) + " - " + colourPickerLabel()
              : std::string("colour");
  }

  // The backdrop chip.
  void openBackdropPicker() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueBackdropField(*cue)) return;
    const bool same = colourPicker_.open && colourPicker_.backdrop && colourPicker_.cue == cue;
    closeColourPicker();
    if (same) return;
    colourPicker_.open = true;
    colourPicker_.cue = cue;
    colourPicker_.backdrop = true;
    colourPicker_.whole = true;
    colourPicker_.anchor = lastInlineEditorAnchorRect_;
    lastInlineEditorAnchorRect_ = SDL_Rect {};
    std::error_code madeEc;
    dbfs::create_directories(colourLibraryFolder(), madeEc);
    colourLibraries_ = deckboy::colours::loadColourLibraries(colourLibraryFolder());
  }

  // The four presets beside the chip: transparent, black, key green, key blue.
  void setCueBackdropPreset(int preset) {
    Cue* cue = selectedCueMutable();
    int* field = cue ? cueBackdropField(*cue) : nullptr;
    if (!field) return;
    static const int kPresets[4] = {kTimerBackdropTransparent, 0x000000, 0x00b140, 0x0047bb};
    static const char* kNames[4] = {"transparent", "black", "key green", "key blue"};
    const int at = std::clamp(preset, 0, 3);
    pushUndoSnapshot();
    *field = kPresets[at];
    markProjectDirty();
    triggerToast(std::string("backdrop: ") + kNames[at]);
  }

  void closeColourPicker() {
    if (colourPicker_.sampling && !colourPicker_.testPick) deckboy::platform::cancelScreenPick();
    closeColourLoupe();
    colourPicker_ = ColourPickerState {};
  }

  // The chip: packed = effect index * 8 + slot, as the LFO actions are.
  void openColourPicker(int packed) {
    auto* stack = selectedEffectStack();
    const int index = packed / 8;
    const int slot = packed % 8;
    if (!effectIndexValid(stack, index)) return;
    const auto kind = (*stack)[index].kind;
    const bool hue = deckboy::effects::cueEffectParamIsHue(kind, slot);
    const bool whole = deckboy::effects::cueEffectColourSlot(kind) == slot;
    if (!hue && !whole) return;
    // The chip that opened it closes it again.
    const bool same = colourPicker_.open && colourPicker_.effect == index && colourPicker_.slot == slot;
    closeColourPicker();
    if (same) return;
    colourPicker_.open = true;
    colourPicker_.cue = selectedCueMutable();
    colourPicker_.effect = index;
    colourPicker_.slot = slot;
    colourPicker_.whole = !hue;
    colourPicker_.anchor = lastInlineEditorAnchorRect_;
    lastInlineEditorAnchorRect_ = SDL_Rect {};
    // Read on every open, so a table dropped into the folder is there the
    // next time without a restart. Made if it is not there, so the folder the
    // typing box names is one the operator can find and drop a file into.
    std::error_code madeEc;
    dbfs::create_directories(colourLibraryFolder(), madeEc);
    colourLibraries_ = deckboy::colours::loadColourLibraries(colourLibraryFolder());
  }

  dbfs::path colourLibraryFolder() const { return Paths::stateDir() / "colours"; }

  // Sets what the picker is editing. False, with a toast saying why, when the
  // colour cannot go there.
  bool applyPickedColour(std::uint32_t rgb, const std::string& name = {}) {
    if (!colourPickerTargetValid()) {
      closeColourPicker();
      return false;
    }
    rgb &= 0xFFFFFF;
    const std::string labelText = colourPickerLabel();
    const char* label = labelText.c_str();
    const std::string said = name.empty() ? hexOfColour(rgb) : name + " " + hexOfColour(rgb);
    deckboy::effects::CueEffect* fx = colourPicker_.backdrop ? nullptr : colourPickerEffect();
    if (colourPicker_.backdrop) {
      pushUndoSnapshot();
      *cueBackdropField(*selectedCueMutable()) = static_cast<int>(rgb);
    } else if (colourPicker_.whole) {
      fx->colour = rgb;
    } else {
      const double hue = deckboy::effects::colourHue01(rgb);
      if (hue < 0.0) {
        triggerToast(std::string(label ? label : "colour") + ": " + said +
                       " is a grey, and a grey has no hue. Pick a colour.",
                     ToastKind::Warning, kToastReadableMs);
        return false;
      }
      float* slots[4] = {&fx->paramA, &fx->paramB, &fx->paramC, &fx->paramD};
      *slots[colourPicker_.slot] = static_cast<float>(hue);
    }
    recentColours_.erase(std::remove(recentColours_.begin(), recentColours_.end(), rgb), recentColours_.end());
    recentColours_.insert(recentColours_.begin(), rgb);
    if (recentColours_.size() > 10) recentColours_.resize(10);
    triggerToast(std::string(label ? label : "colour") + " " + said);
    markProjectDirty();
    return true;
  }

  // What the picker's swatch shows: the effect's colour now.
  std::uint32_t colourPickerCurrent() {
    if (colourPicker_.backdrop) {
      Cue* cue = selectedCueMutable();
      const int* field = cue ? cueBackdropField(*cue) : nullptr;
      return field && *field >= 0 ? static_cast<std::uint32_t>(*field) : 0x000000;
    }
    const deckboy::effects::CueEffect* fx = colourPickerEffect();
    if (!fx) return 0xFFFFFF;
    return effectChipColour(*fx, colourPicker_.slot).value_or(0xFFFFFF);
  }

  // Recent picks, then the colours already in this show, ten at most.
  std::vector<std::uint32_t> colourPickerSwatches() const {
    std::vector<std::uint32_t> out = recentColours_;
    auto add = [&](std::uint32_t c) {
      if (out.size() < 10 && std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
    };
    for (const auto& deck : project_.decks) {
      for (const auto& cue : deck.cues) {
        for (const auto& fx : cue.effects) {
          for (int slot = 0; slot < 4; ++slot) {
            if (deckboy::effects::cueEffectColourSlot(fx.kind) == slot && (fx.colour & 0xFFFFFF) != 0xFFFFFF) {
              add(fx.colour & 0xFFFFFF);
            } else if (deckboy::effects::cueEffectParamIsHue(fx.kind, slot)) {
              if (auto c = effectChipColour(fx, slot)) add(*c);
            }
          }
        }
      }
    }
    return out;
  }

  // ── Typing a colour ─────────────────────────────────────────────────────

  void editColourPickerText(const char* title, const std::string& prompt, const std::string& initial) {
    lastInlineEditorAnchorRect_ = colourPicker_.panel;
    openInlineTextEditor("colour.picker", title, prompt, initial, [this](const std::string& typed) {
      const auto match = deckboy::colours::findColour(typed, colourLibraries_);
      if (!match) {
        triggerToast("no colour called \"" + typed + "\". A hex, three numbers 0-255, a CSS name, a RAL number, " +
                       "or a name from a table in " + colourLibraryFolder().string(),
                     ToastKind::Warning, kToastReadableMs);
        return;
      }
      applyPickedColour(match->rgb, match->exact ? match->name : "took " + match->name);
    });
  }

  void editColourPickerDegrees() {
    const double hue = deckboy::effects::colourHue01(colourPickerCurrent());
    lastInlineEditorAnchorRect_ = colourPicker_.panel;
    openInlineTextEditor("colour.picker", "Hue", "Degrees round the wheel from red, 0-360:",
                         std::to_string(hue < 0.0 ? 0 : static_cast<int>(std::lround(hue * 360.0))),
                         [this](const std::string& typed) {
      char* end = nullptr;
      const double degrees = std::strtod(typed.c_str(), &end);
      if (end == typed.c_str() || degrees < 0.0 || degrees > 360.0) {
        triggerToast("hue: expected 0-360, got " + typed, ToastKind::Warning, kToastReadableMs);
        return;
      }
      applyPickedColour(deckboy::effects::hueColour(degrees / 360.0));
    });
  }

  // ── Pick from screen ────────────────────────────────────────────────────

  void beginColourScreenPick() {
    // DECKBOY_TEST_SCREEN_PICK=#rrggbb stands in for the screen, for the check
    // that runs where there is no desktop to read (tools/check_colour_picker.py):
    // the pick then waits for Enter, which takes that colour, or Esc.
    if (const char* fake = std::getenv("DECKBOY_TEST_SCREEN_PICK"); fake && *fake) {
      colourPicker_.testPick = deckboy::colours::parseHexColour(fake);
      if (colourPicker_.testPick) {
        colourPicker_.sampling = true;
        return;
      }
    }
    std::string why;
    if (!deckboy::platform::beginScreenPick(&why)) {
      triggerToast("pick from screen: " + why, ToastKind::Warning, kToastReadableMs);
      return;
    }
    colourPicker_.sampling = true;
    colourPicker_.hasPatch = false;
    triggerToast("click anywhere on any screen to take its colour. Esc cancels");
  }

  void closeColourLoupe() {
    if (colourPicker_.loupeRenderer) {
      purgeTextTextureCache(colourPicker_.loupeRenderer);
      SDL_DestroyRenderer(colourPicker_.loupeRenderer);
    }
    if (colourPicker_.loupeWindow) SDL_DestroyWindow(colourPicker_.loupeWindow);
    colourPicker_.loupeRenderer = nullptr;
    colourPicker_.loupeWindow = nullptr;
  }

  // Once a frame while the picker is open.
  void serviceColourPicker() {
    if (!colourPicker_.open) return;
    if (!colourPickerTargetValid()) {
      closeColourPicker();
      return;
    }
    if (!colourPicker_.sampling || colourPicker_.testPick) return;
    const auto frame = deckboy::platform::pollScreenPick();
    colourPicker_.systemLoupe = frame.systemLoupe;
    if (frame.hasPatch) {
      colourPicker_.hasPatch = true;
      colourPicker_.patch = frame.patch;
    }
    switch (frame.state) {
      case deckboy::platform::ScreenPickState::Picked:
        colourPicker_.sampling = false;
        closeColourLoupe();
        applyPickedColour(frame.colour);
        return;
      case deckboy::platform::ScreenPickState::Cancelled:
        colourPicker_.sampling = false;
        closeColourLoupe();
        triggerToast("pick from screen cancelled");
        return;
      case deckboy::platform::ScreenPickState::Failed:
        colourPicker_.sampling = false;
        closeColourLoupe();
        triggerToast("pick from screen: " + frame.error, ToastKind::Warning, kToastReadableMs);
        return;
      case deckboy::platform::ScreenPickState::Idle:
        colourPicker_.sampling = false;
        closeColourLoupe();
        return;
      case deckboy::platform::ScreenPickState::Picking:
        break;
    }
    if (!colourPicker_.systemLoupe) drawColourLoupe();
  }

  // The magnifier beside the cursor, over whatever window it is on. It sits
  // down and to the right of the pointer -- up or left at a screen's edge --
  // so it is never inside the 9x9 it is showing.
  void drawColourLoupe() {
    const int cell = uiScaled(14);
    const int pad = uiScaled(6);
    const int textH = fontSmall_ ? TTF_GetFontHeight(fontSmall_) : uiScaled(14);
    const int w = cell * 9 + pad * 2;
    const int h = w + textH + pad * 2;
    if (!colourPicker_.loupeWindow) {
      colourPicker_.loupeWindow =
        SDL_CreateWindow("Deckboy colour", w, h,
                         SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_UTILITY |
                           SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_HIDDEN);
      if (!colourPicker_.loupeWindow) return;
      colourPicker_.loupeRenderer = SDL_CreateRenderer(colourPicker_.loupeWindow, nullptr);
      if (!colourPicker_.loupeRenderer) {
        closeColourLoupe();
        return;
      }
    }
    float gx = 0.0f, gy = 0.0f;
    SDL_GetGlobalMouseState(&gx, &gy);
    int x = static_cast<int>(gx) + uiScaled(24);
    int y = static_cast<int>(gy) + uiScaled(24);
    SDL_Rect bounds {};
    const SDL_Point at {static_cast<int>(gx), static_cast<int>(gy)};
    const SDL_DisplayID display = SDL_GetDisplayForPoint(&at);
    if (display != 0 && SDL_GetDisplayBounds(display, &bounds)) {
      if (x + w > bounds.x + bounds.w) x = static_cast<int>(gx) - uiScaled(24) - w;
      if (y + h > bounds.y + bounds.h) y = static_cast<int>(gy) - uiScaled(24) - h;
    }
    SDL_SetWindowPosition(colourPicker_.loupeWindow, x, y);
    SDL_Renderer* r = colourPicker_.loupeRenderer;
    SDL_SetRenderDrawColor(r, pal.deep.r, pal.deep.g, pal.deep.b, 255);
    SDL_RenderClear(r);
    for (int j = 0; j < 9; ++j) {
      for (int i = 0; i < 9; ++i) {
        const SDL_Color c = sdlColourOf(colourPicker_.patch[static_cast<std::size_t>(j * 9 + i)]);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        const SDL_FRect cellRect {static_cast<float>(pad + i * cell), static_cast<float>(pad + j * cell),
                                  static_cast<float>(cell), static_cast<float>(cell)};
        SDL_RenderFillRect(r, &cellRect);
      }
    }
    // The centre pixel, ringed in black and white so it shows on anything.
    const float cx = static_cast<float>(pad + 4 * cell);
    const float cy = static_cast<float>(pad + 4 * cell);
    const SDL_FRect outer {cx - 1.0f, cy - 1.0f, static_cast<float>(cell + 2), static_cast<float>(cell + 2)};
    const SDL_FRect inner {cx + 1.0f, cy + 1.0f, static_cast<float>(cell - 2), static_cast<float>(cell - 2)};
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderRect(r, &outer);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderRect(r, &inner);
    const std::uint32_t centre = colourPicker_.patch[40];
    const double hue = deckboy::effects::colourHue01(centre);
    std::string reading = hexOfColour(centre);
    if (hue >= 0.0) reading += "  " + std::to_string(static_cast<int>(std::lround(hue * 360.0))) + " deg";
    if (fontSmall_ && colourPicker_.hasPatch) {
      drawTextRaw(r, fontSmall_, reading, pal.light, pad, pad * 2 + cell * 9);
    }
    SDL_RenderPresent(r);
    // Shown once there is something to show, not as an empty box.
    if (colourPicker_.hasPatch) SDL_ShowWindow(colourPicker_.loupeWindow);
  }

  // ── The panel ───────────────────────────────────────────────────────────

  void renderColourPicker(int width, int height) {
    serviceColourPicker();
    if (!colourPicker_.open || !colourPickerTargetValid()) return;
    auto& hits = colourPicker_.hits;
    hits.clear();
    const int pad = uiScaled(12);
    const int gap = uiScaled(8);
    const int rowH = uiScaled(28);
    const int smallH = fontSmall_ ? TTF_GetFontHeight(fontSmall_) : uiScaled(14);
    const int w = uiScaled(310);
    const int swatchSize = uiScaled(66);
    const int contentW = w - pad * 2;
    const int h = pad + rowH + gap + swatchSize + gap + smallH + uiScaled(4) + uiScaled(18) + gap +
                  smallH + uiScaled(4) + rowH + gap + smallH + uiScaled(4) + rowH + gap + smallH + uiScaled(4) +
                  uiScaled(24) + gap + smallH * 2 + pad;
    // Level with the chip that opened it, and clear of the inspector it sits
    // in: covering the effect rows hid the very sliders the colour drives.
    const SDL_Rect a = colourPicker_.anchor;
    const int clearOf = cueSettingsViewportRect_.w > 0 && a.w > 0
                          ? std::min(a.x, cueSettingsViewportRect_.x) : a.x;
    int x = a.w > 0 ? clearOf - w - gap : (width - w) / 2;
    if (x < gap) x = std::min(width - w - gap, a.x + a.w + gap);
    int y = a.h > 0 ? a.y - rowH : (height - h) / 2;
    x = std::clamp(x, gap, std::max(gap, width - w - gap));
    y = std::clamp(y, gap, std::max(gap, height - h - gap));
    const SDL_Rect panel {x, y, w, h};
    colourPicker_.panel = panel;
    hits.push_back({panel, kColourHitPanel, 0});
    drawUIPanel(panel, pal.tile, pal.deep, pal.mid);

    int cy = y + pad;
    const int left = x + pad;
    const std::uint32_t current = colourPickerCurrent();
    // Title and close.
    const std::string title = colourPickerTitle();
    drawTextSafe(controlRenderer_, fontBase_ ? fontBase_ : fontSmall_,
                 SDL_Rect {left, cy, contentW - rowH - gap, rowH}, title, pal.fg);
    const SDL_Rect closeRect {x + w - pad - rowH, cy, rowH, rowH};
    drawUIPanel(closeRect, pal.tile, pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, closeRect, "X", pal.fg);
    hits.push_back({closeRect, kColourHitClose, 0});
    cy += rowH + gap;

    // The colour, and PICK FROM SCREEN beside it.
    const SDL_Rect swatch {left, cy, swatchSize, swatchSize};
    drawUIPanel(swatch, sdlColourOf(current), pal.deep, pal.mid);
    const int sideX = left + swatchSize + gap;
    const int sideW = contentW - swatchSize - gap;
    const SDL_Rect pickRect {sideX, cy, sideW, uiScaled(34)};
    const bool sampling = colourPicker_.sampling;
    drawUIPanel(pickRect, paletteToggleFill(sampling), pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, pickRect,
                         sampling ? "CLICK ANYWHERE..." : "PICK FROM SCREEN", paletteToggleInk(sampling));
    hits.push_back({pickRect, kColourHitScreen, 0});
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {sideX, pickRect.y + pickRect.h + uiScaled(4), sideW, smallH},
                 "any window, any screen", pal.inkSoft);
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect {sideX, pickRect.y + pickRect.h + uiScaled(4) + smallH, sideW, smallH},
                 sampling ? "Esc cancels, arrows nudge" : "Esc cancels", pal.inkSoft);
    cy += swatchSize + gap;

    // The hue strip: a click sets the hue.
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {left, cy, contentW, smallH}, "hue", pal.fgSoft);
    cy += smallH + uiScaled(4);
    const SDL_Rect strip {left, cy, contentW, uiScaled(18)};
    for (int i = 0; i < strip.w; i += 2) {
      const SDL_Color c = sdlColourOf(deckboy::effects::hueColour(static_cast<double>(i) / strip.w));
      SDL_SetRenderDrawColor(controlRenderer_, c.r, c.g, c.b, 255);
      const SDL_FRect bar {static_cast<float>(strip.x + i), static_cast<float>(strip.y), 2.0f,
                           static_cast<float>(strip.h)};
      SDL_RenderFillRect(controlRenderer_, &bar);
    }
    const double hue = deckboy::effects::colourHue01(current);
    if (hue >= 0.0) {
      const int mx = strip.x + static_cast<int>(hue * strip.w);
      const SDL_FRect mark {static_cast<float>(mx - uiScaled(3)), static_cast<float>(strip.y - uiScaled(3)),
                            static_cast<float>(uiScaled(6)), static_cast<float>(strip.h + uiScaled(6))};
      SDL_SetRenderDrawColor(controlRenderer_, 255, 255, 255, 255);
      SDL_RenderRect(controlRenderer_, &mark);
      const SDL_FRect mark2 {mark.x + 1.0f, mark.y + 1.0f, mark.w - 2.0f, mark.h - 2.0f};
      SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 255);
      SDL_RenderRect(controlRenderer_, &mark2);
    }
    colourPicker_.hueStrip = strip;
    hits.push_back({strip, kColourHitHue, 0});
    cy += strip.h + gap;

    // Hex, R G B, degrees: each one opens the typing box.
    const int third = (contentW - gap * 2) / 3;
    const char* names[3] = {"hex", "R G B", "hue deg"};
    char rgbText[16];
    std::snprintf(rgbText, sizeof(rgbText), "%u %u %u", (current >> 16) & 0xFF, (current >> 8) & 0xFF,
                  current & 0xFF);
    const std::string values[3] = {
      hexOfColour(current), rgbText,
      hue >= 0.0 ? std::to_string(static_cast<int>(std::lround(hue * 360.0))) : std::string("grey")};
    const int kinds[3] = {kColourHitHex, kColourHitRgb, kColourHitDegrees};
    for (int i = 0; i < 3; ++i) {
      const int fx0 = left + i * (third + gap);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {fx0, cy, third, smallH}, names[i], pal.fgSoft);
      const SDL_Rect cell {fx0, cy + smallH + uiScaled(4), third, rowH};
      drawUIPanel(cell, pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, cell, values[i], pal.deep);
      hits.push_back({cell, kinds[i], 0});
    }
    cy += smallH + uiScaled(4) + rowH + gap;

    // A named colour.
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {left, cy, contentW, smallH}, "find a named colour",
                 pal.fgSoft);
    const SDL_Rect named {left, cy + smallH + uiScaled(4), contentW, rowH};
    // An empty field with an example in it, not a value: the tile's fill
    // and the soft ink, where the three above are filled cells.
    drawUIPanel(named, pal.tile, pal.deep, pal.mid);
    const std::string example = colourLibraries_.empty()
                                  ? std::string("e.g. crimson, RAL 3020")
                                  : "e.g. crimson, RAL 3020, " + colourLibraries_.front().name;
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {named.x + uiScaled(8), named.y, named.w - uiScaled(16),
                                                         named.h},
                 example, pal.inkSoft);
    hits.push_back({named, kColourHitNamed, 0});
    cy += smallH + uiScaled(4) + rowH + gap;

    // Recent, and in this show.
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {left, cy, contentW, smallH}, "recent and in this show",
                 pal.fgSoft);
    cy += smallH + uiScaled(4);
    const auto swatches = colourPickerSwatches();
    const int chip = uiScaled(24);
    const int chipGap = std::max(2, (contentW - chip * 10) / 9);
    if (swatches.empty()) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {left, cy, contentW, chip}, "none yet", pal.inkSoft);
    }
    for (std::size_t i = 0; i < swatches.size(); ++i) {
      const SDL_Rect r {left + static_cast<int>(i) * (chip + chipGap), cy, chip, chip};
      drawUIPanel(r, sdlColourOf(swatches[i]), pal.deep, pal.mid);
      hits.push_back({r, kColourHitSwatch, swatches[i]});
    }
    cy += chip + gap;

    // What this row does with a colour.
    const char* note1 = colourPicker_.backdrop ? "the colour behind it: key it out"
                        : colourPicker_.whole ? "uses the whole colour" : "uses the hue only, so the";
    const char* note2 = colourPicker_.backdrop ? "on a mixer, or pick transparent"
                        : colourPicker_.whole ? "" : "picture keeps its own shading";
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {left, cy, contentW, smallH}, note1, pal.inkSoft);
    if (*note2) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {left, cy + smallH, contentW, smallH}, note2,
                   pal.inkSoft);
    }
  }

  bool handleColourPickerMouseDown(int x, int y) {
    if (!colourPicker_.open) return false;
    // While picking, the click belongs to the platform picker (which takes it
    // before it reaches any window, Deckboy's included).
    if (colourPicker_.sampling) return true;
    const SDL_Point at {x, y};
    // Last pushed wins: everything sits on the panel, which is pushed first.
    for (auto it = colourPicker_.hits.rbegin(); it != colourPicker_.hits.rend(); ++it) {
      if (!SDL_PointInRect(&at, &it->rect)) continue;
      switch (it->what) {
        case kColourHitClose: closeColourPicker(); break;
        case kColourHitScreen: beginColourScreenPick(); break;
        case kColourHitHue: {
          const SDL_Rect s = colourPicker_.hueStrip;
          const double t = std::clamp(static_cast<double>(x - s.x) / std::max(1, s.w), 0.0, 0.9999);
          applyPickedColour(deckboy::effects::hueColour(t));
          break;
        }
        case kColourHitHex:
          editColourPickerText("Colour", "A hex (#rrggbb), or any colour by name:", hexOfColour(colourPickerCurrent()));
          break;
        case kColourHitRgb: {
          const std::uint32_t c = colourPickerCurrent();
          editColourPickerText("Colour", "Red, green and blue, 0-255 each:",
                               std::to_string((c >> 16) & 0xFF) + " " + std::to_string((c >> 8) & 0xFF) + " " +
                                 std::to_string(c & 0xFF));
          break;
        }
        case kColourHitDegrees: editColourPickerDegrees(); break;
        case kColourHitNamed:
          editColourPickerText("Find a colour",
                               "A CSS name, a RAL number, or a name from your own tables (" +
                                 std::to_string(colourLibraries_.size()) + " colours in " +
                                 colourLibraryFolder().string() + "):",
                               "");
          break;
        case kColourHitSwatch: applyPickedColour(it->colour); break;
        default: break;   // the panel itself: nothing under it fires
      }
      return true;
    }
    // A click off the panel puts it away, and goes no further: it was aimed
    // at the panel's edge as often as at what is under it.
    closeColourPicker();
    return true;
  }

  bool handleColourPickerKey(SDL_Keycode key, bool keyRepeat) {
    if (!colourPicker_.open) return false;
    if (colourPicker_.sampling && colourPicker_.testPick) {
      if (keyRepeat) return true;
      if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        const std::uint32_t taken = *colourPicker_.testPick;
        colourPicker_.sampling = false;
        colourPicker_.testPick.reset();
        applyPickedColour(taken);
        return true;
      }
      if (key == SDLK_ESCAPE) {
        colourPicker_.sampling = false;
        colourPicker_.testPick.reset();
        triggerToast("pick from screen cancelled");
        return true;
      }
      return true;
    }
    if (colourPicker_.sampling) {
      switch (key) {
        case SDLK_ESCAPE: if (!keyRepeat) deckboy::platform::cancelScreenPick(); return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: if (!keyRepeat) deckboy::platform::takeScreenPick(); return true;
        case SDLK_LEFT: deckboy::platform::nudgeScreenPick(-1, 0); return true;
        case SDLK_RIGHT: deckboy::platform::nudgeScreenPick(1, 0); return true;
        case SDLK_UP: deckboy::platform::nudgeScreenPick(0, -1); return true;
        case SDLK_DOWN: deckboy::platform::nudgeScreenPick(0, 1); return true;
        default: return false;
      }
    }
    if (key == SDLK_ESCAPE && !keyRepeat) {
      closeColourPicker();
      return true;
    }
    return false;
  }

  // For STATUS JSON: the chips the inspector drew this frame, and the picker.
  // "chips": [[packed, x, y, w, h], ...]; "hits": [[what, x, y, w, h], ...] in
  // ColourPickerHit's order.
  std::string colourPickerStatusJson() const {
    std::ostringstream out;
    out << "\"colourChips\":[";
    bool first = true;
    for (const auto& qb : quickButtons_) {
      // The backdrop chip is listed as -1: it has no effect slot.
      if (qb.action != QuickAction::EffectColourPick && qb.action != QuickAction::CueBackdropPick) continue;
      const int packed = qb.action == QuickAction::CueBackdropPick ? -1 : qb.param;
      out << (first ? "" : ",") << "[" << packed << "," << qb.rect.x << "," << qb.rect.y << ","
          << qb.rect.w << "," << qb.rect.h << "]";
      first = false;
    }
    out << "],\"colourPicker\":{\"open\":" << (colourPicker_.open ? "true" : "false")
        << ",\"sampling\":" << (colourPicker_.sampling ? "true" : "false")
        << ",\"effect\":" << colourPicker_.effect << ",\"slot\":" << colourPicker_.slot
        << ",\"whole\":" << (colourPicker_.whole ? "true" : "false")
        << ",\"backdrop\":" << (colourPicker_.backdrop ? "true" : "false") << ",\"hits\":[";
    first = true;
    for (const auto& hit : colourPicker_.hits) {
      out << (first ? "" : ",") << "[" << hit.what << "," << hit.rect.x << "," << hit.rect.y << ","
          << hit.rect.w << "," << hit.rect.h << "]";
      first = false;
    }
    out << "]},\"inspectorViewport\":[" << cueSettingsViewportRect_.x << "," << cueSettingsViewportRect_.y << ","
        << cueSettingsViewportRect_.w << "," << cueSettingsViewportRect_.h << "],";
    return out.str();
  }
