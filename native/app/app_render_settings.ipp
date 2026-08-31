// ============================================================================
// app_render_settings.ipp — Settings modal dialog rendering and actions.
//
// Renders the settings modal (opened by the SETTINGS button or Ctrl+,):
//
//   renderSettingsModal()        — entry point: backdrop, tabs, tab content
//   renderSettingsTabGeneral()   — general settings (project name, BPM, etc.)
//   renderSettingsTabOutput()    — output configuration (resolution, displays)
//   renderSettingsTabNetwork()   — network settings (OSC, Companion, NMC, etc.)
//   renderSettingsTabIntegration() — integration backends (ATEM, NDI, LTC, etc.)
//   handleSettingsAction()       — dispatch button presses by action constant
//
// Settings actions are integer constants (kSettingsAction*) defined in the
// App class. Each button/toggle in the settings UI has a unique action ID.
// When clicked, handleSettingsAction() routes to the appropriate handler.
//
// Current highest action constant: 623 (kSettingsActionOutputAoiBDec).
// New actions should be allocated from 624+.
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Render the settings modal dialog over the main control window.
  // Shared settings chrome: a cartridge-style "label plate" header — dark
  // plate, light title — so every card/section has one strong, scannable
  // anchor (the old headers were dark-on-light text that blended into the
  // card fill). Geometry contract: the header band occupies the same ~50px
  // the old drawCard used (title ~y+6, subtitle ~y+34), so the hardcoded
  // control offsets inside every card are untouched.
  // AOI pixel-rect view of the focused output's crop fractions. The operator
  // edits X/Y/WIDTH/HEIGHT in pixels of the output raster; storage stays the
  // four edge fractions (serialization untouched).
  struct AoiRectPx {
    int x = 0, y = 0, w = 0, h = 0;
    int rasterW = 0, rasterH = 0;
  };

  // ── On-air indicator ───────────────────────────────────────────────────────
  // A little transmitter that actually transmits: a dot with rings pushing out
  // of it while the stream is live. Motion is the point — a static coloured
  // pill cannot distinguish "connected and sending" from "armed and stalled",
  // which is exactly the moment an operator needs to tell them apart. Colours
  // come from existing theme roles, so it stays legible in all 30 colourways
  // without touching a theme file.
  // activeTint MUST come from the caller's background: settings sections fill
  // with shell_inner (so pal.light reads), but the bottom-bar groups fill with
  // pal.tile, where pal.light is light-on-light and the badge vanishes. That is
  // the chrome contract in CLAUDE.md — structural panels ink with fg, small
  // raised controls with light — and a shared widget has to be told which it is
  // sitting on.
  // Whimsy while you wait. Same shape as the boot console pool: a hand of lines
  // picked from a bigger set, advanced on a timer rather than per frame so it
  // reads instead of strobing.
  static const char* encoderWhimsyLine(Uint64 nowMs) {
    static const char* kLines[] = {
      "reticulating macroblocks",
      "convincing B-frames to leave quietly",
      "teaching pixels to forget",
      "negotiating with the chroma planes",
      "aligning the flux capacitor to GOP boundaries",
      "this one has opinions about colour space",
      "counting keyframes, losing count",
      "politely asking the GPU",
      "rewrapping, restacking, re-something",
      "the gremlins are compressing",
    };
    const int n = static_cast<int>(sizeof(kLines) / sizeof(kLines[0]));
    return kLines[static_cast<int>(nowMs / 3200) % n];
  }

  // The encoder's busy surface: the mascot fronting whatever the queue is
  // chewing through, with a real bar per job. Progress < 0 means ffmpeg has not
  // reported yet (it emits roughly once a second), so that reads as a pulse
  // rather than a misleading 0%.
  void drawEncoderBusyPanel(const SDL_Rect& area, Uint64 nowMs) {
    drawUIPanel(area, pal.dark, pal.deep, pal.mid);
    const int pad = uiScaled(6);
    const int lineH = std::max(uiScaled(16), textLineHeight(fontSmall_));
    // The whimsy line runs the full width along the bottom, NOT inside the
    // face column: squeezed into the narrow column it ellipsized to nonsense
    // ("this one has opi..."). The mascot also refuses to draw its face below
    // 150x120 and silently falls back to a text line, so the column has to
    // clear that or you get the truncated text and no friend.
    SDL_Rect face {area.x + pad, area.y + pad,
                   std::max(uiScaled(164), area.w / 5),
                   area.h - pad * 2 - lineH};
    drawStartupMascot(face, nowMs, "");
    drawCenteredTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{area.x + pad, area.y + area.h - lineH - pad / 2,
                                  area.w - pad * 2, lineH},
                         encoderWhimsyLine(nowMs), pal.mid);

    int rx = face.x + face.w + uiScaled(10);
    int rw = area.x + area.w - rx - uiScaled(10);
    int ry = area.y + pad;
    const int rowH = 30;
    int shown = 0;
    const int xW = uiScaled(18);
    for (std::size_t jobIndex = 0; jobIndex < conversionJobs_.size(); ++jobIndex) {
      const auto& job = conversionJobs_[jobIndex];
      if (ry + rowH > face.y + face.h || shown >= 4) {
        break;
      }
      double pct = job.progress ? job.progress->load() : -1.0;
      bool running = job.state == ConversionState::Running;
      std::string head = (running ? "" : (job.held ? "held    " : "queued  ")) + job.label;
      const int pctW = uiScaled(42);
      const int textW = rw - pctW - xW * 3 - uiScaled(12);
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{rx, ry, textW, lineH},
                   ellipsizeToPixelWidth(fontSmall_, head, textW), pal.light);
      // Per-row cancel. Only the first four rows get one, matching what the
      // panel can show; the rest are reachable via CANCEL ALL.
      // Row controls, right to left: cancel, hold/release, move up. Only the
      // first four rows get them, matching what the panel can show; the rest
      // are reachable once these clear.
      SDL_Rect xBtn {rx + rw - xW, ry, xW, lineH};
      drawUIPanel(xBtn, pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, xBtn, "x", pal.light);
      settingsBtns_.push_back({xBtn,
                               kSettingsActionEncoderCancelRowBase + static_cast<int>(jobIndex),
                               running ? "cancel this encode" : "remove from queue"});

      SDL_Rect holdBtn {xBtn.x - xW - uiScaled(3), ry, xW, lineH};
      drawUIPanel(holdBtn, job.held ? pal.light : pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, holdBtn,
                           job.held ? ">" : "=",
                           job.held ? pal.deep : pal.light);
      settingsBtns_.push_back({holdBtn,
                               kSettingsActionEncoderHoldRowBase + static_cast<int>(jobIndex),
                               job.held ? "release this job" : "hold this job"});

      if (jobIndex > 0) {
        SDL_Rect upBtn {holdBtn.x - xW - uiScaled(3), ry, xW, lineH};
        drawUIPanel(upBtn, pal.mid, pal.deep, pal.light);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, upBtn, "^", pal.light);
        settingsBtns_.push_back({upBtn,
                                 kSettingsActionEncoderUpRowBase + static_cast<int>(jobIndex),
                                 "move earlier in the queue"});
      }
      if (pct >= 0.0) {
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{rx + rw - xW * 3 - uiScaled(52), ry, uiScaled(42), lineH},
                     std::to_string(static_cast<int>(pct * 100.0 + 0.5)) + "%", pal.light);
      }
      SDL_Rect bar {rx, ry + lineH + uiScaled(3), rw - xW * 3 - uiScaled(12), uiScaled(8)};
      Primitives::fillRect(controlRenderer_, bar, pal.deep);
      if (pct >= 0.0) {
        SDL_Rect fill {bar.x, bar.y, static_cast<int>(bar.w * std::clamp(pct, 0.0, 1.0)), bar.h};
        Primitives::fillRect(controlRenderer_, fill, pal.light);
      } else if (running) {
        // Indeterminate: a block sliding along the track.
        int bw = std::max(24, bar.w / 6);
        double u = std::fmod(static_cast<double>(nowMs) / 1400.0, 1.0);
        int bx = bar.x + static_cast<int>((bar.w + bw) * u) - bw;
        SDL_Rect fill {std::max(bar.x, bx), bar.y,
                       std::min(bw, bar.x + bar.w - std::max(bar.x, bx)), bar.h};
        if (fill.w > 0) Primitives::fillRect(controlRenderer_, fill, pal.mid);
      }
      ry += rowH;
      ++shown;
    }
    if (static_cast<int>(conversionJobs_.size()) > shown) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{rx, ry, rw, 14},
                   "+" + std::to_string(static_cast<int>(conversionJobs_.size()) - shown)
                     + " more queued", pal.mid);
    }
  }

  void drawStreamOnAirBadge(const SDL_Rect& rect, bool configured, bool armed,
                            OutputHealthState health, SDL_Color activeTint) {
    const Uint64 nowMs = SDL_GetTicks();
    const int bx = rect.x + rect.w / 2;
    const int by = rect.y + rect.h / 2;
    const int maxR = std::max(4, std::min(rect.w, rect.h) / 2 - 1);

    SDL_Color tint = pal.mid;                 // configured, idle
    if (!configured) {
      tint = pal.dark;                        // nothing set up yet
    } else if (armed) {
      tint = (health == OutputHealthState::Error ||
              health == OutputHealthState::Recovering) ? pal.deleteBezel : activeTint;
    }

    // Core dot. It breathes while live, so the badge is never fully static.
    int dotR = std::max(2, maxR / 3);
    if (armed) {
      const float breathe = 0.5f + 0.5f * std::sin(static_cast<float>(nowMs) * 0.006f);
      dotR = std::max(2, static_cast<int>(std::lround(dotR * (0.85f + 0.3f * breathe))));
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, tint.r, tint.g, tint.b, 255);
    for (int dy = -dotR; dy <= dotR; ++dy) {
      const int span = static_cast<int>(std::lround(std::sqrt(
        std::max(0.0, static_cast<double>(dotR) * dotR - static_cast<double>(dy) * dy))));
      SDL_RenderLine(controlRenderer_, static_cast<float>(bx - span), static_cast<float>(by + dy),
                     static_cast<float>(bx + span), static_cast<float>(by + dy));
    }
    if (!armed) {
      return;
    }
    // Two rings half a cycle apart, fading as they expand. Errors pulse faster —
    // a stream in trouble should feel agitated rather than calm.
    const float speed = (health == OutputHealthState::Error) ? 0.0030f : 0.0016f;
    for (int ring = 0; ring < 2; ++ring) {
      const float phase = std::fmod(static_cast<float>(nowMs) * speed + ring * 0.5f, 1.0f);
      const int r = static_cast<int>(std::lround(dotR + phase * (maxR - dotR)));
      if (r <= dotR) {
        continue;
      }
      const Uint8 alpha = static_cast<Uint8>(std::clamp(
        static_cast<int>(std::lround(210.0f * (1.0f - phase))), 0, 255));
      SDL_SetRenderDrawColor(controlRenderer_, tint.r, tint.g, tint.b, alpha);
      constexpr int kSegments = 24;
      float prevX = 0.0f, prevY = 0.0f;
      for (int s = 0; s <= kSegments; ++s) {
        const float a = static_cast<float>(s) / kSegments * 6.2831853f;
        const float px = static_cast<float>(bx) + std::cos(a) * r;
        const float py = static_cast<float>(by) + std::sin(a) * r;
        if (s > 0) {
          SDL_RenderLine(controlRenderer_, prevX, prevY, px, py);
        }
        prevX = px;
        prevY = py;
      }
    }
  }

  AoiRectPx focusedOutputAoiRectPx() {
    const OutputTarget& ot = focusedOutput();
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
    AoiRectPx rect;
    rect.rasterW = std::max(16, rasterW);
    rect.rasterH = std::max(16, rasterH);
    rect.x = static_cast<int>(std::lround(std::clamp(ot.aoiLeft, 0.0f, 0.95f) * rect.rasterW));
    rect.y = static_cast<int>(std::lround(std::clamp(ot.aoiTop, 0.0f, 0.95f) * rect.rasterH));
    rect.w = std::max(1, static_cast<int>(std::lround(
      std::clamp(1.0f - ot.aoiLeft - ot.aoiRight, 0.0f, 1.0f) * rect.rasterW)));
    rect.h = std::max(1, static_cast<int>(std::lround(
      std::clamp(1.0f - ot.aoiTop - ot.aoiBottom, 0.0f, 1.0f) * rect.rasterH)));
    return rect;
  }

  // Standard rasters an operator actually asks for by name. "Full" is the
  // output's own raster and is always first; anything larger than the current
  // raster is filtered out at build time so the list never offers a region the
  // output cannot contain.
  std::vector<std::pair<std::string, std::string>> aoiSizeChoices() {
    AoiRectPx cur = focusedOutputAoiRectPx();
    static const std::array<std::pair<int, int>, 10> kRasters {{
      {3840, 2160}, {2560, 1440}, {1920, 1080}, {1600, 1200}, {1280, 1024},
      {1280, 720}, {1024, 768}, {854, 480}, {800, 600}, {640, 480}
    }};
    std::vector<std::pair<std::string, std::string>> choices;
    choices.push_back({"full", "Full  " + std::to_string(cur.rasterW) + "x"
                                        + std::to_string(cur.rasterH)});
    for (const auto& [w, h] : kRasters) {
      if (w > cur.rasterW || h > cur.rasterH) {
        continue;
      }
      std::string token = std::to_string(w) + "x" + std::to_string(h);
      choices.push_back({token, token});
    }
    return choices;
  }

  // Resize the AOI about its own centre so picking a smaller raster keeps the
  // region where the operator put it instead of snapping back to a corner.
  void applyFocusedOutputAoiSizeToken(const std::string& token) {
    AoiRectPx cur = focusedOutputAoiRectPx();
    if (token == "full") {
      applyFocusedOutputAoiRectPx(0, 0, cur.rasterW, cur.rasterH);
      triggerToast("area of interest: full raster");
      return;
    }
    size_t xPos = token.find('x');
    if (xPos == std::string::npos) {
      return;
    }
    try {
      int w = std::stoi(token.substr(0, xPos));
      int h = std::stoi(token.substr(xPos + 1));
      int cxCentre = cur.x + cur.w / 2;
      int cyCentre = cur.y + cur.h / 2;
      applyFocusedOutputAoiRectPx(cxCentre - w / 2, cyCentre - h / 2, w, h);
      triggerToast("area of interest: " + token);
    } catch (...) {
    }
  }

  void centreFocusedOutputAoi() {
    AoiRectPx cur = focusedOutputAoiRectPx();
    applyFocusedOutputAoiRectPx((cur.rasterW - cur.w) / 2,
                                (cur.rasterH - cur.h) / 2, cur.w, cur.h);
    triggerToast("area of interest: centred");
  }

  void applyFocusedOutputAoiRectPx(int x, int y, int w, int h) {
    OutputTarget& ot = focusedOutputMutable();
    AoiRectPx cur = focusedOutputAoiRectPx();
    // Keep the region at least 5% of the raster in both dimensions so each
    // stored edge fraction stays within the serializer's 0–0.95 clamp.
    int minW = std::max(1, (cur.rasterW + 19) / 20);
    int minH = std::max(1, (cur.rasterH + 19) / 20);
    w = std::clamp(w, minW, cur.rasterW);
    h = std::clamp(h, minH, cur.rasterH);
    x = std::clamp(x, 0, cur.rasterW - w);
    y = std::clamp(y, 0, cur.rasterH - h);
    ot.aoiLeft   = static_cast<float>(x) / static_cast<float>(cur.rasterW);
    ot.aoiRight  = static_cast<float>(cur.rasterW - x - w) / static_cast<float>(cur.rasterW);
    ot.aoiTop    = static_cast<float>(y) / static_cast<float>(cur.rasterH);
    ot.aoiBottom = static_cast<float>(cur.rasterH - y - h) / static_cast<float>(cur.rasterH);
    markProjectDirty();
  }

  void openAoiValueEditor(const char* token, const char* title, int currentPx,
                          std::function<void(int)> applyPx) {
    openInlineTextEditor(token, title, "pixels (of the output raster)",
                         std::to_string(currentPx),
                         [this, applyPx](const std::string& rawValue) {
                           try {
                             applyPx(std::stoi(trim(rawValue)));
                           } catch (...) {
                             triggerToast("aoi: invalid number");
                           }
                         });
  }

  // ── Section header contract ───────────────────────────────────────────────
  // Every titled box in Settings — the System tab's cards and the Video tab's
  // sections — gets its title plate from here, so they are inset from the frame
  // by the same margin and sized from the same font metric. Previously the two
  // had independent geometry (26px plate/fontBase_ vs 22px plate/fontSmall_,
  // both with hardcoded text offsets), which is why headers sat at different
  // heights depending on which tab you were looking at.
  int settingsPlateInset() const { return uiScaled(5); }

  int settingsPlateHeight(TTF_Font* font) const {
    return std::max(uiScaled(20), textLineHeight(font ? font : fontSmall_) + uiScaled(6));
  }

  // Vertical space a titled box spends on its header before body content
  // starts. Callers sizing a section from its row count use this instead of
  // the old hardcoded 32 so the box still fits when the font changes.
  int settingsHeaderHeight(TTF_Font* font) const {
    return settingsPlateInset() + settingsPlateHeight(font) + uiScaled(6);
  }

  SDL_Rect settingsPlateRect(const SDL_Rect& rect, TTF_Font* font) const {
    int inset = settingsPlateInset();
    return SDL_Rect {rect.x + inset, rect.y + inset,
                     std::max(0, rect.w - inset * 2),
                     settingsPlateHeight(font)};
  }

  // Draws the plate and returns it, so callers that need to park a control on
  // the header (the CONNECTED DISPLAYS "IDENTIFY" button) can align to the
  // plate itself instead of guessing an offset from the card frame.
  SDL_Rect drawSettingsPlate(const SDL_Rect& rect, const std::string& title, TTF_Font* font) {
    SDL_Rect plate = settingsPlateRect(rect, font);
    Primitives::drawFramedPanel(controlRenderer_, plate, pal.dark, pal.deep, pal.mid);
    drawTextSafe(controlRenderer_, font ? font : fontSmall_, plate, title, pal.light);
    return plate;
  }

  // Thin scrollbar down the right edge of a scrolling settings viewport, so it
  // is obvious there is more below rather than the tab looking truncated.
  void drawSettingsScrollHint(const SDL_Rect& viewport, int scroll, int scrollMax) {
    if (scrollMax <= 0 || viewport.h <= 0) {
      return;
    }
    int trackW = uiScaled(4);
    SDL_Rect track {viewport.x + viewport.w - trackW - uiScaled(3), viewport.y + uiScaled(3),
                    trackW, viewport.h - uiScaled(6)};
    Primitives::fillRect(controlRenderer_, track, pal.mid);
    double visible = static_cast<double>(viewport.h) / (viewport.h + scrollMax);
    int thumbH = std::max(uiScaled(18), static_cast<int>(track.h * visible));
    int travel = std::max(0, track.h - thumbH);
    int thumbY = track.y + (scrollMax > 0 ? travel * scroll / scrollMax : 0);
    Primitives::fillRect(controlRenderer_, SDL_Rect{track.x, thumbY, track.w, thumbH}, pal.deep);
  }

  void drawSettingsCard(const SDL_Rect& rect, const std::string& title,
                        const std::string& subtitle = std::string()) {
    Primitives::drawFramedPanel(controlRenderer_, rect, pal.shellInner, pal.deep, pal.light);
    SDL_Rect plate = drawSettingsPlate(rect, title, fontBase_);
    if (!subtitle.empty()) {
      int subH = std::max(uiScaled(14), textLineHeight(fontSmall_));
      int subInset = settingsPlateInset() + uiScaled(7);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect {rect.x + subInset, plate.y + plate.h + uiScaled(4),
                             std::max(0, rect.w - subInset * 2), subH},
                   subtitle, pal.inkSoft);
    }
  }

  void renderSettingsModal() {
    if (!settingsOpen_) return;
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);

    // Dim backdrop
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 160);
    SDL_Rect full {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &full);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    // Modal panel
    SDL_Rect modal = settingsModalRect();
    Primitives::drawFramedPanel(controlRenderer_, modal, pal.shellInner, pal.deep, pal.shellOuter);

    // Title — pixel face for the Game Boy read
    TTF_Font* modalTitleFont = fontPixel_ ? fontPixel_ : fontBase_;
    int titleH = std::max(uiScaled(28), textLineHeight(modalTitleFont) + uiScaled(4));
    drawTextSafe(controlRenderer_, modalTitleFont,
                 SDL_Rect {modal.x + uiScaled(16), modal.y + uiScaled(8),
                           modal.w - uiScaled(64), titleH},
                 "SETTINGS", pal.fg);

    // Close button [X]
    int closeW = uiScaled(34);
    int closeH = uiScaled(30);
    settingsCloseBtn_ = {modal.x + modal.w - closeW - uiScaled(8), modal.y + uiScaled(6),
                         closeW, closeH};
    Primitives::drawFramedPanel(controlRenderer_, settingsCloseBtn_, pal.mid, pal.deep, pal.light);
    drawCenteredText(controlRenderer_, fontSmall_, "X", pal.deep, settingsCloseBtn_);

    // Tab bar — cartridge-shelf tabs: the active tab is full height and
    // "plugged in" (a joint bar bridges it to the content frame); inactive
    // tabs sit recessed. One glance shows where you are. Tabs size to their
    // labels (with a shared floor) so nothing ellipsizes.
    const int kTabH = std::max(uiScaled(40), textLineHeight(fontSmall_) + uiScaled(18));
    const int kTabGap = uiScaled(6);
    int tabY = modal.y + std::max(uiScaled(44), titleH + uiScaled(12));
    settingsBtns_.clear();
    const std::vector<std::string> tabs {"System", "Audio", "Network", "Video Outputs", "About", "Encoder"};
    int tabX = modal.x + uiScaled(16);
    for (int t = 0; t < (int)tabs.size(); ++t) {
      bool active = (t == settingsTab_);
      int recess = active ? 0 : uiScaled(6);
      int labelW = 0;
      int labelH = 0;
      if (fontSmall_) {
        TTF_GetStringSize(fontSmall_, tabs[t].c_str(), 0, &labelW, &labelH);
      }
      int tabW = std::max(uiScaled(88), labelW + uiScaled(24));
      SDL_Rect tab {tabX, tabY + recess, tabW, kTabH - recess};
      Primitives::drawFramedPanel(controlRenderer_, tab, active ? pal.dark : pal.light,
                      pal.deep, active ? pal.light : pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, tab, tabs[t],
                           active ? pal.light : pal.deep);
      if (active) {
        SDL_Rect joint {tab.x + 2, tab.y + tab.h - 2, tab.w - 4, uiScaled(8)};
        Primitives::fillRect(controlRenderer_, joint, pal.dark);
      }
      settingsBtns_.push_back({tab, 100 + t, tabs[t]});
      tabX += tabW + kTabGap;
    }

    // Content area — tile fill so it reads as a dark frame on terminal themes
    // (= screen_light on light themes, unchanged). All content sits inside
    // cards, so nothing draws text directly on this frame.
    int contentTop = tabY + kTabH + uiScaled(10);
    SDL_Rect content {modal.x + uiScaled(16), contentTop, modal.w - uiScaled(32),
                      std::max(uiScaled(80), modal.y + modal.h - contentTop - uiScaled(16))};
    Primitives::drawFramedPanel(controlRenderer_, content, pal.tile, pal.deep, pal.mid);

    int cx = content.x + uiScaled(12), cy = content.y + uiScaled(10);
    SDL_Color ink = pal.deep;
    SDL_Color soft = pal.inkSoft;

    // ── Shared layout metrics for every tab ─────────────────────────────────
    // Rows are sized from the font and every constant goes through uiScaled(),
    // so chrome grows with the type instead of staying at 1x and letting labels
    // collide. At uiScale 1.0 these reduce to the values the tabs were
    // originally authored with, so the 1x layout is unchanged.
    const int sPad = uiScaled(8);
    const int sGap = uiScaled(6);
    const int sLineH = std::max(uiScaled(16), textLineHeight(fontSmall_));
    const int sRowH = std::max(uiScaled(24), textLineHeight(fontSmall_) + uiScaled(9));
    const int sTallH = std::max(uiScaled(30), textLineHeight(fontSmall_) + uiScaled(15));
    const int sChipH = std::max(uiScaled(22), textLineHeight(fontSmall_) + uiScaled(7));
    // Header plate + the one-line subtitle the cards carry.
    const int sCardHeaderH = settingsHeaderHeight(fontBase_) + sLineH + sGap;
    auto cardBodyY = [&](const SDL_Rect& card) { return card.y + sCardHeaderH; };
    auto cardBodyX = [&](const SDL_Rect& card) { return card.x + sPad; };
    auto cardBodyW = [&](const SDL_Rect& card) { return card.w - sPad * 2; };

    if (settingsTab_ == 0) {
      const Deck& tcDeck = focusedDeck();
      auto drawPillToggle = [&](const SDL_Rect& rect, bool on, const std::string& onLabel, const std::string& offLabel) {
        Primitives::drawFramedPanel(controlRenderer_, rect,
                        on ? pal.dark : pal.mid,
                        pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, on ? onLabel : offLabel,
                         on ? pal.light : pal.deep, rect);
      };

      auto drawCard = [&](const SDL_Rect& rect, const std::string& title, const std::string& subtitle = std::string()) {
        drawSettingsCard(rect, title, subtitle);
      };

      // Card heights are computed from their contents rather than being magic
      // numbers, so a taller font pushes the card out instead of overflowing it.
      int colGap = uiScaled(12);
      int colH = content.h - uiScaled(20);
      int leftW = std::max(uiScaled(320), (content.w - uiScaled(12) - colGap) / 2);
      int rightW = std::max(uiScaled(320), content.w - uiScaled(12) - colGap - leftW);
      const int kCardGap = uiScaled(10);

      // APPEARANCE, in the order it is laid out below: theme (tall), sound
      // effects, mascot, creatures, UI scale (tall), Pocket 3. Six controls,
      // five gaps. This was one row short of the six for a while, which
      // clipped the Pocket 3 button off the bottom AND shortened the scroll
      // range so it could not be reached -- keep it in step with the rows.
      int appearanceH = sCardHeaderH + sTallH * 2 + sRowH * 4 + sGap * 5 + sPad;
      int safetyH = sCardHeaderH + sLineH + uiScaled(4) + sChipH * 2 + sGap + sPad;
      // SHOW FLOW: vj mode, jump mode + global crossfade, panic profile label
      // and its row. Grew by a row when VJ mode got a switch.
      int flowH = sCardHeaderH + sRowH * 4 + sLineH + uiScaled(4) + sGap * 3 + sPad;
      int cueToolsH = sCardHeaderH + sLineH * 2 + sGap + sRowH + sPad;
      int prefsH = sCardHeaderH + sRowH * 5 + sLineH + sGap * 2 + uiScaled(4) * 3 + sPad;

      // At large UI scales the cards are genuinely taller than the window can
      // show, so the tab scrolls rather than silently cropping the bottom card.
      // At 1x nothing overflows and the scroll is inert.
      int leftNeeded = appearanceH + kCardGap + safetyH + kCardGap + flowH;
      int rightNeeded = cueToolsH + kCardGap + prefsH;
      // MEASURED, not predicted. These heights are hand-computed constants
      // that must match the controls laid out under them, and when one of
      // them was too small the control that fell outside its card could not be
      // scrolled to either -- the same number produced both faults. The
      // measurement taken while drawing the previous frame wins whenever it is
      // larger, so a stale constant can make a card look cramped but can never
      // put a control out of reach. One frame of lag, which nobody can see.
      settingsSystemScrollMax_ = std::max(
        0, std::max(std::max(leftNeeded, rightNeeded),
                    settingsSystemDrawnH_) - colH);
      settingsSystemScroll_ = std::clamp(settingsSystemScroll_, 0, settingsSystemScrollMax_);
      settingsSystemViewport_ = content;
      // Rebuilt below as the columns are drawn. Read one line above, before
      // this reset, which is what gives the measurement its one-frame lag.
      settingsSystemDrawnH_ = 0;
      bool systemScrolls = settingsSystemScrollMax_ > 0;
      int colTop = cy - settingsSystemScroll_;

      SDL_Rect leftCol {cx, colTop, leftW, colH};
      SDL_Rect rightCol {cx + leftW + colGap, colTop, rightW, colH};

      // Keep cards inside the content frame while scrolled.
      SDL_Rect previousSettingsClip {};
      bool hadSettingsClip = SDL_RenderClipEnabled(controlRenderer_) == true;
      if (hadSettingsClip) {
        SDL_GetRenderClipRect(controlRenderer_, &previousSettingsClip);
      }
      SDL_SetRenderClipRect(controlRenderer_, &content);
      const std::size_t systemButtonStart = settingsBtns_.size();

      int leftY = leftCol.y;
      SDL_Rect appearanceRect {leftCol.x, leftY, leftCol.w, appearanceH};
      leftY += appearanceRect.h + kCardGap;
      SDL_Rect safetyRect {leftCol.x, leftY, leftCol.w, safetyH};
      leftY += safetyRect.h + kCardGap;
      SDL_Rect flowRect {leftCol.x, leftY, leftCol.w,
                         systemScrolls ? flowH
                                       : std::max(flowH, leftCol.y + leftCol.h - leftY)};

      int rightY = rightCol.y;
      SDL_Rect cueToolsRect {rightCol.x, rightY, rightCol.w, cueToolsH};
      rightY += cueToolsRect.h + kCardGap;
      SDL_Rect prefsRect {rightCol.x, rightY, rightCol.w,
                          systemScrolls ? prefsH
                                        : std::max(prefsH, rightCol.y + rightCol.h - rightY)};

      drawCard(appearanceRect, "APPEARANCE", "Theme and operator feedback");
      std::string themeName = currentThemeName_.empty() ? "gameboy" : currentThemeName_;
      int appY = cardBodyY(appearanceRect);
      const int appX = cardBodyX(appearanceRect);
      const int appW = cardBodyW(appearanceRect);
      SDL_Rect themeBtn {appX, appY, appW, sTallH};
      appY += sTallH + sGap;
      drawUIDropdown(themeBtn, "Theme", themeName, "settings.theme");
      settingsBtns_.push_back({themeBtn, kSettingsActionThemeDropdown, "theme"});
      SDL_Rect sfxBtn {appX, appY, appW, sRowH};
      appY += sRowH + sGap;
      drawPillToggle(sfxBtn, project_.uiSoundsEnabled, "SFX ON", "SFX OFF");
      settingsBtns_.push_back({sfxBtn, 201, "sfx_toggle"});
      // Mascot dropdown: deckbot (default) or deckgirl. Picks the splash
      // character; refreshSplashAsset re-resolves the art on change.
      SDL_Rect mascotBtn {appX, appY, appW, sRowH};
      appY += sRowH + sGap;
      std::string mascotLabel =
        (project_.splashCharacter == "deckgirl") ? "Deckgirl" : "Deckbot";
      drawUIDropdown(mascotBtn, "Mascot", mascotLabel, "settings.mascot");
      settingsBtns_.push_back({mascotBtn, kSettingsActionMascotToggle, "mascot_toggle"});
      // The theme's creatures. ALWAYS shown, even on a theme that has none.
      //
      // It was hidden in that case at first, on the "no control for something
      // that cannot happen" rule -- but this is a global PREFERENCE, not a
      // control over the current theme. Hiding it means an operator on the
      // default look, which deliberately has no animals, cannot find the
      // switch to decide about them before changing theme; and a setting that
      // appears and disappears as you browse themes is worse than one that is
      // simply always there.
      SDL_Rect critterBtn {appX, appY, appW, sRowH};
      appY += sRowH + sGap;
      // Three states, one button: off, out only when no output is live, or
      // out regardless. The middle one is the default and the safe one; the
      // last exists because an operator who always has an output armed would
      // otherwise never see them at all.
      drawPillToggle(critterBtn, project_.creaturesEnabled,
                     project_.creaturesWhileLive ? "CREATURES ALWAYS"
                                                 : "CREATURES WHEN IDLE",
                     "CREATURES OFF");
      settingsBtns_.push_back({critterBtn, kSettingsActionCreaturesToggle,
                               "creatures_toggle"});
      // UI scale dropdown — multiplies every font point size at load, and (as
      // of v0.81.0) the settings chrome scales with it too.
      SDL_Rect scaleBtn {appX, appY, appW, sTallH};
      appY += sTallH + sGap;
      char scaleLabel[16];
      snprintf(scaleLabel, sizeof(scaleLabel), "%.2fx", project_.uiScale);
      drawUIDropdown(scaleBtn, "UI Scale", scaleLabel, "settings.ui_scale");
      settingsBtns_.push_back({scaleBtn, kSettingsActionUiScaleDropdown, "ui_scale"});
      // Pocket 3 preset — one-click ergonomic bundle for the GPD Pocket 3
      // and other small high-DPI handhelds. Pushes uiScale to 2.0 and
      // refreshes fonts. Tap to apply; tap again to revert to 1.0.
      SDL_Rect pocketBtn {appX, appY, appW, sRowH};
      bool pocketActive = std::abs(project_.uiScale - 2.0) < 0.01 &&
                          project_.interactionMode == "touch";
      drawPillToggle(pocketBtn, pocketActive, "POCKET 3 / TOUCH", "POCKET 3 / TOUCH");
      settingsBtns_.push_back({pocketBtn, kSettingsActionPocket3Preset, "pocket3_preset"});

      // What APPEARANCE actually used, measured at the point its last control
      // was placed. Compared against the card it was given so an overflow
      // cannot silently clip.
      settingsSystemDrawnH_ = std::max(
        settingsSystemDrawnH_,
        (appY + sRowH + sPad) - colTop);

      drawCard(safetyRect, "SAFETY / TIMECODE", "Emergency fade and sync behavior");
      const int safetyX = cardBodyX(safetyRect);
      const int stepW = uiScaled(26);
      const int stepValW = uiScaled(72);
      const int panicLabelY = cardBodyY(safetyRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{safetyX, panicLabelY, cardBodyW(safetyRect), sLineH}, "panic fade", soft);
      const int panicRowY = panicLabelY + sLineH + uiScaled(4);
      SDL_Rect panicFadeDecBtn {safetyX, panicRowY, stepW, sChipH};
      SDL_Rect panicFadeValRect {panicFadeDecBtn.x + stepW + uiScaled(4), panicFadeDecBtn.y, stepValW, sChipH};
      SDL_Rect panicFadeIncBtn {panicFadeValRect.x + panicFadeValRect.w + uiScaled(4), panicFadeDecBtn.y, stepW, sChipH};
      Primitives::drawFramedPanel(controlRenderer_, panicFadeDecBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "-", ink, panicFadeDecBtn);
      settingsBtns_.push_back({panicFadeDecBtn, 208, "panic_fade_dec"});
      char panicFadeBuf[32];
      snprintf(panicFadeBuf, sizeof(panicFadeBuf), "%.1fs", project_.panicFadeSeconds);
      Primitives::drawFramedPanel(controlRenderer_, panicFadeValRect, pal.shellInner,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, panicFadeBuf, ink, panicFadeValRect);
      Primitives::drawFramedPanel(controlRenderer_, panicFadeIncBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "+", ink, panicFadeIncBtn);
      settingsBtns_.push_back({panicFadeIncBtn, 209, "panic_fade_inc"});
      const int panicRestoreX = panicFadeIncBtn.x + panicFadeIncBtn.w + sPad;
      SDL_Rect panicRestoreBtn {panicRestoreX, panicFadeDecBtn.y,
                                std::max(uiScaled(40), safetyRect.x + safetyRect.w - sPad - panicRestoreX),
                                sChipH};
      drawPillToggle(panicRestoreBtn, project_.panicAutoRestore, "AUTO RESTORE ON", "AUTO RESTORE OFF");
      settingsBtns_.push_back({panicRestoreBtn, 212, "panic_restore_toggle"});

      const int tcJamY = panicRowY + sChipH + sGap;
      SDL_Rect tcJamBtn {safetyX, tcJamY, uiScaled(140), sChipH};
      SDL_Rect tcFwDecBtn {tcJamBtn.x + tcJamBtn.w + sPad, tcJamBtn.y, stepW, sChipH};
      SDL_Rect tcFwValRect {tcFwDecBtn.x + stepW + uiScaled(4), tcFwDecBtn.y, stepValW, sChipH};
      SDL_Rect tcFwIncBtn {tcFwValRect.x + tcFwValRect.w + uiScaled(4), tcFwDecBtn.y, stepW, sChipH};
      drawPillToggle(tcJamBtn, tcDeck.timecodeJamSyncEnabled, "TC JAM ON", "TC JAM OFF");
      settingsBtns_.push_back({tcJamBtn, 213, "tc_jam_toggle"});
      Primitives::drawFramedPanel(controlRenderer_, tcFwDecBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "-", ink, tcFwDecBtn);
      settingsBtns_.push_back({tcFwDecBtn, 214, "tc_freewheel_dec"});
      char tcFwBuf[32];
      snprintf(tcFwBuf, sizeof(tcFwBuf), "%.1fs", tcDeck.timecodeFreewheelSeconds);
      Primitives::drawFramedPanel(controlRenderer_, tcFwValRect, pal.shellInner,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, tcFwBuf, ink, tcFwValRect);
      Primitives::drawFramedPanel(controlRenderer_, tcFwIncBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "+", ink, tcFwIncBtn);
      settingsBtns_.push_back({tcFwIncBtn, 215, "tc_freewheel_inc"});

      drawCard(flowRect, "SHOW FLOW", "Global jump and panic behavior");
      // VJ MODE. This changes what the application IS -- one deck and a
      // playlist, or two decks and a crossfader -- and until now the only way
      // to reach it was a line over the socket. It sits at the top of SHOW
      // FLOW because everything else in this card describes how the deck
      // behaves, and this decides how many decks there are.
      const int flowX = cardBodyX(flowRect);
      const int flowW = cardBodyW(flowRect);
      SDL_Rect vjModeBtn {flowX, cardBodyY(flowRect), flowW, sRowH};
      drawPillToggle(vjModeBtn, project_.vjModeEnabled,
                     "VJ MODE ON  (two decks + crossfader)",
                     "VJ MODE OFF  (cue deck)");
      settingsBtns_.push_back({vjModeBtn, kSettingsActionVjModeToggle, "vj_mode"});
      SDL_Rect jumpModeBtn {flowX, vjModeBtn.y + sRowH + sGap, uiScaled(150), sRowH};
      SDL_Rect jumpTransBtn {jumpModeBtn.x + jumpModeBtn.w + sPad, jumpModeBtn.y,
                             std::max(uiScaled(40), flowW - jumpModeBtn.w - sPad), sRowH};
      Primitives::drawFramedPanel(controlRenderer_, jumpModeBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, jumpModeLabelFromToken(project_.jumpMode), ink, jumpModeBtn);
      settingsBtns_.push_back({jumpModeBtn, 203, "jump_mode"});
      drawPillToggle(jumpTransBtn, project_.jumpTransitionEnabled, "GLOBAL XFADE ON", "GLOBAL XFADE OFF");
      settingsBtns_.push_back({jumpTransBtn, 204, "jump_transition"});

      const int profileLabelY = jumpModeBtn.y + sRowH + sGap;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{flowX, profileLabelY, flowW, sLineH}, "panic profile", soft);
      const int profileRowY = profileLabelY + sLineH + uiScaled(4);
      SDL_Rect panicPrevBtn {flowX, profileRowY, stepW, sRowH};
      SDL_Rect panicNextBtn {flowRect.x + flowRect.w - sPad - stepW, profileRowY, stepW, sRowH};
      SDL_Rect panicLabelRect {panicPrevBtn.x + panicPrevBtn.w + sGap, panicPrevBtn.y,
                               std::max(uiScaled(40),
                                        panicNextBtn.x - sGap - (panicPrevBtn.x + panicPrevBtn.w + sGap)),
                               sRowH};
      Primitives::drawFramedPanel(controlRenderer_, panicPrevBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "<", ink, panicPrevBtn);
      settingsBtns_.push_back({panicPrevBtn, 205, "panic_profile_prev"});
      Primitives::drawFramedPanel(controlRenderer_, panicLabelRect, pal.shellInner,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, panicProfileLabelFromToken(project_.panicProfile), ink, panicLabelRect);
      Primitives::drawFramedPanel(controlRenderer_, panicNextBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, ">", ink, panicNextBtn);
      settingsBtns_.push_back({panicNextBtn, 206, "panic_profile_next"});

      const int panicRunY = profileRowY + sRowH + sGap;
      SDL_Rect panicRunBtn {flowX, panicRunY, flowW, sRowH};
      Primitives::drawFramedPanel(controlRenderer_, panicRunBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Run Panic", ink, panicRunBtn);
      settingsBtns_.push_back({panicRunBtn, 207, "panic_run"});

      drawCard(cueToolsRect, "CUE TOOLS", "Find from the playlist, not from a modal");
      const int cueX = cardBodyX(cueToolsRect);
      const int cueW = cardBodyW(cueToolsRect);
      const int cueToolsLine1Y = cardBodyY(cueToolsRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{cueX, cueToolsLine1Y, cueW, sLineH},
                   "Use Ctrl+F or type a cue id/number to search.", soft);
      const int cueToolsLine2Y = cueToolsLine1Y + sLineH;
      std::string findStatus = "find: none";
      if (!lastCueFindToken_.empty() && !lastCueFindMatches_.empty()) {
        int cursor = std::clamp(lastCueFindCursor_, 0, static_cast<int>(lastCueFindMatches_.size()) - 1);
        findStatus = "find \"" + lastCueFindToken_ + "\" " + std::to_string(cursor + 1) + "/" + std::to_string(lastCueFindMatches_.size());
      }
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{cueX, cueToolsLine2Y, cueW, sLineH},
                   findStatus, soft);
      const int renumberY = cueToolsLine2Y + sLineH + sGap;
      SDL_Rect renumberBtn {cueX, renumberY, cueW, sRowH};
      Primitives::drawFramedPanel(controlRenderer_, renumberBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Renumber...", ink, renumberBtn);
      settingsBtns_.push_back({renumberBtn, 221, "cue_renumber"});

      drawCard(prefsRect, "PLAYLIST PREFERENCES", "Defaults for newly created cues");
      const Deck& prefDeck = focusedDeck();
      const int prefsX = cardBodyX(prefsRect);
      const int prefsW = cardBodyW(prefsRect);
      SDL_Rect prefsEditBtn {prefsX, cardBodyY(prefsRect), prefsW, sRowH};
      Primitives::drawFramedPanel(controlRenderer_, prefsEditBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Edit Timebase / Start / Fade / Duration...", ink, prefsEditBtn);
      settingsBtns_.push_back({prefsEditBtn, kSettingsActionPlaylistPrefsEdit, "playlist_prefs_edit"});

      std::string prefSummary = "tc " + playlistTimebaseLabel(prefDeck.playlistTimebaseFps)
        + "  start " + formatTimecode(prefDeck.playlistStartOffsetSeconds, prefDeck.playlistTimebaseFps)
        + "  fade " + formatSeconds(prefDeck.playlistDefaultCueFadeSeconds)
        + "  still " + formatSeconds(prefDeck.playlistDefaultStillDurationSeconds);
      const int prefSummaryY = prefsEditBtn.y + prefsEditBtn.h + sGap;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{prefsX, prefSummaryY, prefsW, sLineH},
                   prefSummary, soft);

      int toggleGap = sGap;
      int toggleW = std::max(uiScaled(120), (prefsW - toggleGap) / 2);
      int toggleH = sRowH;
      int toggleY = prefSummaryY + sLineH + sGap;
      SDL_Rect loopT {prefsX, toggleY, toggleW, toggleH};
      SDL_Rect fadeInT {loopT.x + toggleW + toggleGap, toggleY,
                        std::max(uiScaled(40), prefsRect.x + prefsRect.w - sPad - (loopT.x + toggleW + toggleGap)),
                        toggleH};
      drawPillToggle(loopT, prefDeck.playlistDefaultLoop, "LOOP ON", "LOOP OFF");
      drawPillToggle(fadeInT, prefDeck.playlistDefaultFadeInEnabled, "FADE IN ON", "FADE IN OFF");
      settingsBtns_.push_back({loopT, kSettingsActionPlaylistDefaultLoopToggle, "playlist_default_loop"});
      settingsBtns_.push_back({fadeInT, kSettingsActionPlaylistDefaultFadeInToggle, "playlist_default_fadein"});

      int toggleY2 = toggleY + toggleH + uiScaled(4);
      SDL_Rect fadeOutT {prefsX, toggleY2, toggleW, toggleH};
      SDL_Rect audioT {fadeOutT.x + toggleW + toggleGap, toggleY2,
                       std::max(uiScaled(40), prefsRect.x + prefsRect.w - sPad - (fadeOutT.x + toggleW + toggleGap)),
                       toggleH};
      drawPillToggle(fadeOutT, prefDeck.playlistDefaultFadeOutEnabled, "FADE OUT ON", "FADE OUT OFF");
      drawPillToggle(audioT, prefDeck.playlistDefaultAudioEnabled, "AUDIO ON", "AUDIO OFF");
      settingsBtns_.push_back({fadeOutT, kSettingsActionPlaylistDefaultFadeOutToggle, "playlist_default_fadeout"});
      settingsBtns_.push_back({audioT, kSettingsActionPlaylistDefaultAudioToggle, "playlist_default_audio"});

      int toggleY3 = toggleY2 + toggleH + uiScaled(4);
      SDL_Rect pauseBeginT {prefsX, toggleY3, toggleW, toggleH};
      SDL_Rect pauseEndT {pauseBeginT.x + toggleW + toggleGap, toggleY3,
                          std::max(uiScaled(40), prefsRect.x + prefsRect.w - sPad - (pauseBeginT.x + toggleW + toggleGap)),
                          toggleH};
      drawPillToggle(pauseBeginT, prefDeck.playlistDefaultPauseAtBeginning, "PAUSE BEGIN ON", "PAUSE BEGIN OFF");
      drawPillToggle(pauseEndT, prefDeck.playlistDefaultPauseAtEnd, "PAUSE END ON", "PAUSE END OFF");
      settingsBtns_.push_back({pauseBeginT, kSettingsActionPlaylistDefaultPauseBeginToggle, "playlist_default_pausebegin"});
      settingsBtns_.push_back({pauseEndT, kSettingsActionPlaylistDefaultPauseEndToggle, "playlist_default_pauseend"});

      int toggleY4 = toggleY3 + toggleH + uiScaled(4);
      SDL_Rect nextTransT {prefsX, toggleY4, prefsW, toggleH};
      drawPillToggle(nextTransT, prefDeck.playlistDefaultTransitionToNext, "NEXT TRANSITION ON", "NEXT TRANSITION OFF");
      settingsBtns_.push_back({nextTransT, kSettingsActionPlaylistDefaultNextTransitionToggle, "playlist_default_nexttrans"});

      SDL_SetRenderClipRect(controlRenderer_, hadSettingsClip ? &previousSettingsClip : nullptr);
      // Scrolled-away controls are painted outside the viewport by the clip, so
      // drop their hit rects too — otherwise an off-screen toggle keeps eating
      // clicks that land over whatever is actually visible there.
      if (systemScrolls) {
        settingsBtns_.erase(
          std::remove_if(settingsBtns_.begin() + static_cast<std::ptrdiff_t>(systemButtonStart),
                         settingsBtns_.end(),
                         [&](const SettingsButton& b) {
                           SDL_Rect clipped {};
                           return !SDL_GetRectIntersection(&b.rect, &content, &clipped);
                         }),
          settingsBtns_.end());
      }
      if (settingsSystemScrollMax_ > 0) {
        drawSettingsScrollHint(content, settingsSystemScroll_, settingsSystemScrollMax_);
      }

    } else if (settingsTab_ == 1) {
      auto drawCard = [&](const SDL_Rect& rect, const std::string& title, const std::string& subtitle = std::string()) {
        drawSettingsCard(rect, title, subtitle);
      };
      auto drawPillToggle = [&](const SDL_Rect& rect, bool on, const std::string& onLabel, const std::string& offLabel) {
        Primitives::drawFramedPanel(controlRenderer_, rect,
                        on ? pal.dark : pal.mid,
                        pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, on ? onLabel : offLabel,
                         on ? pal.light : pal.deep, rect);
      };

      int audioH = sCardHeaderH + sTallH + sGap + sRowH + sGap + sLineH + sPad;
      SDL_Rect audioRect {cx, cy, content.w - uiScaled(24), audioH};
      int midiTop = audioRect.y + audioRect.h + uiScaled(10);
      SDL_Rect midiRect {cx, midiTop, content.w - uiScaled(24),
                         std::max(uiScaled(180), content.y + content.h - uiScaled(10) - midiTop)};

      drawCard(audioRect, "AUDIO OUTPUT", "Device routing for cue playback");
      const int smallLineH = sLineH;
      const int audioX = cardBodyX(audioRect);
      // Says what was ASKED FOR, and says so when that is not what is playing.
      // The request used to be overwritten by the fallback, so a missing
      // interface looked like a deck that had always been on the default and
      // there was nothing to tell the operator otherwise.
      std::string devName = focusedDeck().audioOutputDeviceName.empty()
        ? std::string("(default system device)")
        : focusedDeck().audioOutputDeviceName;
      if (const DeckRuntime* devRt = runtimeForDeck(project_.focusedDeckIndex)) {
        if (!focusedDeck().audioOutputDeviceName.empty() &&
            devRt->audioDeviceInUse != focusedDeck().audioOutputDeviceName) {
          devName += "  (not found — on default)";
        }
      }
      SDL_Rect devBtn {audioX, cardBodyY(audioRect), cardBodyW(audioRect), sTallH};
      drawUIDropdown(devBtn, "Device", devName, "settings.audio_device");
      settingsBtns_.push_back({devBtn, 200, "audio_device"});
      {
        // Live input. Sits with the output device because they are the two ends
        // of the same question: where audio comes from, and where it goes.
        SDL_Rect inBtn {audioX, devBtn.y + devBtn.h + 4, cardBodyW(audioRect), sTallH};
        std::string inLabel = audioInputRunning()
          ? (audioInputActiveDevice_.empty() ? std::string("System default input")
                                             : audioInputActiveDevice_)
          : std::string("off");
        if (audioInputRunning()) {
          // A live meter: the only question an operator has about a microphone
          // is whether it is hearing anything.
          const int bars = static_cast<int>(audioInputPeak_ * 10.0);
          inLabel += "  ";
          for (int b = 0; b < 10; ++b) inLabel += (b < bars) ? "|" : ".";
        }
        drawUIDropdown(inBtn, "Input", inLabel, "settings.audio_input");
        settingsBtns_.push_back({inBtn, kSettingsActionAudioInputDropdown,
                                 "Microphone or line input. Drives the video "
                                 "synth's audio reactivity."});
        if (audioInputRunning()) {
          // Gain, with the value between the steps. This existed as a setting
          // with NO control at all -- saved, loaded, applied, and unreachable.
          SDL_Rect gainRow {audioX, inBtn.y + inBtn.h + 4, cardBodyW(audioRect), sRowH};
          const int gw = gainRow.w / 5;
          SDL_Rect gDec {gainRow.x, gainRow.y, gw, sRowH};
          SDL_Rect gVal {gDec.x + gw + 4, gainRow.y, gainRow.w - gw * 3 - 8, sRowH};
          SDL_Rect gInc {gVal.x + gVal.w + 4, gainRow.y, gw, sRowH};
          auto stepBtn = [&](const SDL_Rect& r, const char* label, int action) {
            drawUIPanel(r, pal.mid, pal.deep, pal.light);
            drawCenteredTextSafe(controlRenderer_, fontSmall_, r, label, ink);
            settingsBtns_.push_back({r, action, label});
          };
          stepBtn(gDec, "GAIN -", kSettingsActionAudioInputGainDec);
          {
            char g[48];
            std::snprintf(g, sizeof(g), "%+.1f dB", project_.audioInputGainDb);
            std::string gl = g;
            if (project_.audioInputClipLatch) gl += "   CLIP";
            drawUIPanel(gVal, project_.audioInputClipLatch ? pal.light : pal.mid,
                        pal.deep, pal.light);
            drawCenteredTextSafe(controlRenderer_, fontSmall_, gVal, gl,
                                 project_.audioInputClipLatch ? pal.deep : ink);
            // Clicking the readout clears the latch: the operator has seen it.
            settingsBtns_.push_back({gVal, kSettingsActionAudioInputClipClear,
                                     "Peak gain. CLIP latches until clicked -- "
                                     "a meter that has fallen back cannot tell "
                                     "you about the transient that distorted."});
          }
          stepBtn(gInc, "GAIN +", kSettingsActionAudioInputGainInc);

          SDL_Rect monoBtn {audioX, gainRow.y + sRowH + 4, cardBodyW(audioRect), sRowH};
          drawPillToggle(monoBtn, project_.audioInputMono,
                         "MONO (summed)", "STEREO");
          settingsBtns_.push_back({monoBtn, kSettingsActionAudioInputMono,
                                   "A microphone is a mono source. Captured as "
                                   "stereo it lands in one leg with silence in "
                                   "the other."});

          SDL_Rect progBtn {audioX, monoBtn.y + sRowH + 4, cardBodyW(audioRect), sRowH};
          drawPillToggle(progBtn, project_.audioInputToProgram,
                         "MIC -> RECORDING", "MIC NOT RECORDED");
          settingsBtns_.push_back({progBtn, kSettingsActionAudioInputToProgram,
                                   "Mix the input into what is streamed and "
                                   "recorded. It does NOT go to the speakers: "
                                   "monitoring a room mic through the machine "
                                   "driving the PA is a feedback loop."});
        }
      }
#if defined(DECKBOY_HAS_ASIO)
      {
        // ASIO sits directly under the system device, because it REPLACES it.
        // Enumerating drivers loads nothing and is safe mid-show; arming is
        // what touches hardware.
        SDL_Rect asioBtn {audioX, devBtn.y + devBtn.h + 4, cardBodyW(audioRect), sTallH};
        std::string label = project_.asioDriverName.empty()
          ? std::string("System audio (SDL)")
          : project_.asioDriverName;
        if (asioArmed()) {
          label += "  [" + std::to_string(asioOutput_->channels()) + "ch " +
                   std::to_string(asioOutput_->bufferFrames()) + " " +
                   std::to_string(static_cast<int>(
                     asioOutput_->outputLatencySeconds() * 1000.0)) + "ms]";
          if (asioOutput_->resampling()) {
            label += "  " + std::to_string(static_cast<int>(asioOutput_->sampleRate()))
                   + "Hz RESAMPLED";
          }
          const std::uint64_t under = asioUnderruns();
          if (under > 0) {
            // Said out loud. Underruns are audible damage and an operator who
            // is not told will blame the PA.
            label += "  " + std::to_string(under) + " DROPS";
          }
        }
        drawUIDropdown(asioBtn, "ASIO", label, "settings.asio");
        settingsBtns_.push_back({asioBtn, kSettingsActionAsioDropdown,
                                 "Play through an ASIO driver instead of the "
                                 "system device: lower latency and more "
                                 "channels. The driver comes from your interface."});
      }
#endif
      // Audio buffer size cycle button
      int bufSamples = project_.audioBufferSamples;
      std::string bufLabel = "Buffer: " + std::to_string(bufSamples) + " smp";
      const int bufBtnY = devBtn.y + devBtn.h + sGap;
      SDL_Rect bufBtn {audioX, bufBtnY, uiScaled(160), sRowH};
      Primitives::drawFramedPanel(controlRenderer_, bufBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, bufLabel, ink, bufBtn);
      settingsBtns_.push_back({bufBtn, kSettingsActionAudioBufferCycle, "audio_buffer_samples"});
      // Chain A/V offset: -/+ buttons around the current delay readout.
      SDL_Rect delayDecBtn {bufBtn.x + bufBtn.w + uiScaled(12), bufBtn.y, uiScaled(24), sRowH};
      SDL_Rect delayLabel {delayDecBtn.x + delayDecBtn.w + uiScaled(4), bufBtn.y, uiScaled(120), sRowH};
      SDL_Rect delayIncBtn {delayLabel.x + delayLabel.w + uiScaled(4), bufBtn.y, uiScaled(24), sRowH};
      Primitives::drawFramedPanel(controlRenderer_, delayDecBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "-", ink, delayDecBtn);
      Primitives::drawFramedPanel(controlRenderer_, delayLabel, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_,
                       "A/V delay: " + std::to_string(project_.audioDelayMs) + " ms",
                       ink, delayLabel);
      Primitives::drawFramedPanel(controlRenderer_, delayIncBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "+", ink, delayIncBtn);
      settingsBtns_.push_back({delayDecBtn, kSettingsActionAudioDelayDec, "audio_delay_dec"});
      settingsBtns_.push_back({delayIncBtn, kSettingsActionAudioDelayInc, "audio_delay_inc"});
      // Device channel count (2/4/6/8) — reopens the deck device; cues then
      // route their stereo onto a pair of these outs (AUDIO section: "outs").
      SDL_Rect chanBtn {delayIncBtn.x + delayIncBtn.w + uiScaled(12), bufBtn.y, uiScaled(110), sRowH};
      Primitives::drawFramedPanel(controlRenderer_, chanBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_,
                       "Outs: " + std::to_string(focusedDeck().audioOutputChannels) + " ch",
                       ink, chanBtn);
      settingsBtns_.push_back({chanBtn, kSettingsActionAudioChannelsCycle, "audio_channels"});
      const int audioFootY = bufBtn.y + bufBtn.h + sGap;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{audioX, audioFootY, cardBodyW(audioRect), sLineH},
                   "Smaller buffer = lower latency. A/V delay holds audio back for lagging displays — dial in with the Pocket Test beacon.", soft);

      drawCard(midiRect, "MIDI CONTROL", "Optional external transport and cue control");
      const int midiX = cardBodyX(midiRect);
      SDL_Rect midiEnBtn {midiX, cardBodyY(midiRect), uiScaled(120), sRowH};
      drawPillToggle(midiEnBtn, midiEnabled_, "MIDI ON", "MIDI OFF");
      settingsBtns_.push_back({midiEnBtn, 210, "midi_toggle"});
      SDL_Rect midiPortBtn {midiEnBtn.x + midiEnBtn.w + sPad, midiEnBtn.y,
                            std::max(uiScaled(40),
                                     midiRect.x + midiRect.w - sPad - (midiEnBtn.x + midiEnBtn.w + sPad)),
                            sRowH};
      Primitives::drawFramedPanel(controlRenderer_, midiPortBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_,
                       midiDeviceName_().empty() ? "Set MIDI Port..." : ellipsizeToPixelWidth(fontSmall_, midiDeviceName_(), midiPortBtn.w - 12),
                       ink, midiPortBtn);
      settingsBtns_.push_back({midiPortBtn, 211, "midi_port"});

      int mapY = midiEnBtn.y + midiEnBtn.h + sGap;
      int midiTextW = cardBodyW(midiRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{midiX, mapY, midiTextW, sLineH}, "Mappings", pal.fg);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{midiX, mapY + smallLineH, midiTextW, sLineH},
                   "Note 0-127 -> trigger cue index in the focused playlist", soft);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{midiX, mapY + smallLineH * 2, midiTextW, sLineH},
                   "CC 7 -> master volume   |   CC 20 -> playback speed", soft);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{midiX, mapY + smallLineH * 3, midiTextW, sLineH},
                   "MMC Play / Stop / Goto -> transport   |   MSC Trigger -> cue by id", soft);

      // ── LTC generator (timecode OUT) ─────────────────────────────────────
      // Deckboy can chase timecode; this is what lets it BE the master.
      // Individually routable on purpose: LTC is a control signal, so it gets
      // its own device and its own channel, with the rest held silent. Putting
      // it in the programme mix would broadcast a buzzsaw.
      {
        int ltcY = mapY + smallLineH * 4 + sGap;
        SDL_Rect ltcRect {midiX - sPad, ltcY, midiRect.w, sTallH * 3 + sLineH + sPad * 3};
        drawCard(ltcRect, "SMPTE LTC OUTPUT", "generate timecode for the rig to chase");
        int lx = cardBodyX(ltcRect);
        int lw = cardBodyW(ltcRect);
        int ly = cardBodyY(ltcRect);
        // The Audio tab has no drawActionBtn lambda (that one is local to the
        // Video Outputs tab), so mirror the idiom the MIDI port button uses.
        auto ltcBtn = [&](const SDL_Rect& r, const std::string& label, int action,
                          bool active = false) {
          Primitives::drawFramedPanel(controlRenderer_, r,
                                      active ? pal.dark : pal.mid, pal.deep, pal.light);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, r,
                               ellipsizeToPixelWidth(fontSmall_, label, r.w - 12),
                               active ? pal.light : ink);
          settingsBtns_.push_back({r, action, "ltc_out"});
        };

        SDL_Rect ltcEnBtn {lx, ly, std::max(uiScaled(120), lw / 3), sRowH};
        ltcBtn(ltcEnBtn,
                      project_.ltcOutputEnabled ? "LTC OUT: ON" : "LTC OUT: OFF",
                      kSettingsActionLtcOutToggle, project_.ltcOutputEnabled);
        SDL_Rect ltcFpsBtn {ltcEnBtn.x + ltcEnBtn.w + sPad, ly,
                            std::max(uiScaled(110), lw / 4), sRowH};
        ltcBtn(ltcFpsBtn, "Rate: " + fmtFloat(project_.ltcOutputFps, 2) + " fps",
                      kSettingsActionLtcOutFps);

        int ly2 = ly + sRowH + sPad;
        SDL_Rect ltcDevBtn {lx, ly2, lw, sRowH};
        ltcBtn(ltcDevBtn, "Device: " + (project_.ltcOutputDeviceName.empty()
                                               ? std::string("(system default)")
                                               : project_.ltcOutputDeviceName),
                      kSettingsActionLtcOutDevice);

        int ly3 = ly2 + sRowH + sPad;
        SDL_Rect ltcChBtn {lx, ly3, std::max(uiScaled(150), lw / 2 - sPad), sRowH};
        ltcBtn(ltcChBtn, "Channel: " + std::to_string(project_.ltcOutputChannel + 1)
                                 + " of " + std::to_string(project_.ltcOutputChannelCount),
                      kSettingsActionLtcOutChannel);
        SDL_Rect ltcChCntBtn {ltcChBtn.x + ltcChBtn.w + sPad, ly3,
                              lw - ltcChBtn.w - sPad, sRowH};
        ltcBtn(ltcChCntBtn, "Device channels: "
                                    + std::to_string(project_.ltcOutputChannelCount),
                      kSettingsActionLtcOutChannelCount);

        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect{lx, ly3 + sRowH + 2, lw, sLineH},
                     project_.ltcOutputEnabled
                       ? ("emitting " + formatTimecode(ltcOutputTimecodeSeconds(),
                            std::clamp(project_.ltcOutputFps, 23.0, 60.0))
                          + "  -  other channels held silent")
                       : std::string("off - all channels silent"),
                     soft);
      }

    } else if (settingsTab_ == 2) {
      auto drawCard = [&](const SDL_Rect& rect, const std::string& title, const std::string& subtitle = std::string()) {
        drawSettingsCard(rect, title, subtitle);
      };
      auto drawPill = [&](const SDL_Rect& rect, bool active, const std::string& onLabel, const std::string& offLabel, int action) {
        Primitives::drawFramedPanel(controlRenderer_, rect,
                        active ? pal.dark : pal.mid,
                        pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, active ? onLabel : offLabel,
                         active ? pal.light : pal.deep, rect);
        settingsBtns_.push_back({rect, action, onLabel});
      };

      int colGap = uiScaled(12);
      int leftW = std::max(uiScaled(320), (content.w - uiScaled(12) - colGap) / 2);
      int rightW = std::max(uiScaled(320), content.w - uiScaled(12) - colGap - leftW);
      SDL_Rect leftCol {cx, cy, leftW, content.h - uiScaled(20)};
      SDL_Rect rightCol {cx + leftW + colGap, cy, rightW, content.h - uiScaled(20)};
      const int kCardGap = uiScaled(10);

      int leftY = leftCol.y;
      int remoteH = sCardHeaderH + sLineH + uiScaled(2) + sRowH + sGap + sChipH + sPad;
      SDL_Rect remoteRect {leftCol.x, leftY, leftCol.w, remoteH};
      leftY += remoteRect.h + kCardGap;
      int oscH = sCardHeaderH + sLineH + uiScaled(4) + sChipH * 2 + sGap + sPad;
      SDL_Rect oscRect {leftCol.x, leftY, leftCol.w, oscH};
      leftY += oscRect.h + kCardGap;
      int notesH = sCardHeaderH + sLineH * 3 + sPad;
      SDL_Rect notesRect {leftCol.x, leftY, leftCol.w,
                          std::max(notesH, leftCol.y + leftCol.h - leftY)};
      SDL_Rect integrationRect {rightCol.x, rightCol.y, rightCol.w, rightCol.h};

      const int netLineH = sLineH;

      drawCard(remoteRect, "REMOTE CONTROL", "Companion / OSC ingress and HyperDeck emulation");
      const int remoteX = cardBodyX(remoteRect);
      const int remoteW = cardBodyW(remoteRect);
      const int remoteLabelY = cardBodyY(remoteRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{remoteX, remoteLabelY, remoteW, sLineH},
                   "Companion / OSC port", soft);
      const int remotePortY = remoteLabelY + sLineH + uiScaled(2);
      SDL_Rect portBtn {remoteX, remotePortY, uiScaled(176), sRowH};
      Primitives::drawFramedPanel(controlRenderer_, portBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Port " + std::to_string(companionPort_), ink, portBtn);
      settingsBtns_.push_back({portBtn, 220, "osc_port"});
      {
        // Note text sits beside the control and shares its vertical centre —
        // pass the control's own height so the helper centres both identically.
        int noteX = portBtn.x + portBtn.w + sPad;
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect{noteX, portBtn.y,
                              std::max(0, remoteRect.x + remoteRect.w - sPad - noteX), portBtn.h},
                     "HyperDeck emulation stays on at TCP 9992.", soft);
      }
      const int remoteToggleY = portBtn.y + portBtn.h + sGap;
      SDL_Rect remoteToggle {remoteX, remoteToggleY, uiScaled(176), sChipH};
      drawPill(remoteToggle, project_.allowRemoteNetwork, "REMOTE ON", "LOCAL ONLY", kSettingsActionAllowRemoteToggle);
      {
        int noteX = remoteToggle.x + remoteToggle.w + sPad;
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect{noteX, remoteToggle.y,
                              std::max(0, remoteRect.x + remoteRect.w - sPad - noteX), remoteToggle.h},
                     project_.allowRemoteNetwork ? "Listening on all interfaces"
                                                 : "Listening on localhost (127.0.0.1)", soft);
      }

      drawCard(oscRect, "OSC QUERY / FEEDBACK", "Discovery and mirrored state");
      std::string queryStatus = project_.oscQueryEnabled ? (oscQueryReady_ ? "running" : "error") : "off";
      const int oscX = cardBodyX(oscRect);
      const int oscStatusY = cardBodyY(oscRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{oscX, oscStatusY, cardBodyW(oscRect), sLineH},
                   "query status: " + queryStatus + "  http " + std::to_string(project_.oscQueryPort), soft);
      const int oscQueryRowY = oscStatusY + sLineH + uiScaled(4);
      SDL_Rect queryToggle {oscX, oscQueryRowY, uiScaled(144), sChipH};
      SDL_Rect queryPortBtn {queryToggle.x + queryToggle.w + sPad, queryToggle.y, uiScaled(164), sChipH};
      drawPill(queryToggle, project_.oscQueryEnabled, "QUERY ON", "QUERY OFF", kSettingsActionOscQueryToggle);
      Primitives::drawFramedPanel(controlRenderer_, queryPortBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Set HTTP Port...", ink, queryPortBtn);
      settingsBtns_.push_back({queryPortBtn, kSettingsActionOscQueryPortPrompt, "osc_query_port"});
      const int oscFbRowY = queryToggle.y + queryToggle.h + sGap;
      SDL_Rect fbToggle {oscX, oscFbRowY, uiScaled(156), sChipH};
      SDL_Rect fbRateBtn {fbToggle.x + fbToggle.w + sPad, fbToggle.y, uiScaled(164), sChipH};
      drawPill(fbToggle, project_.oscFeedbackMirrorEnabled, "MIRROR ON", "MIRROR OFF", kSettingsActionOscFeedbackMirrorToggle);
      Primitives::drawFramedPanel(controlRenderer_, fbRateBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_,
                       std::to_string(project_.oscFeedbackRateMs) + " ms",
                       ink, fbRateBtn);
      settingsBtns_.push_back({fbRateBtn, kSettingsActionOscFeedbackRatePrompt, "osc_feedback_rate"});

      drawCard(notesRect, "DISCOVERY / NOTES", "Network-facing runtime notes");
      const int notesX = cardBodyX(notesRect);
      const int notesLineY = cardBodyY(notesRect);
      int notesTextW = cardBodyW(notesRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{notesX, notesLineY, notesTextW, sLineH},
                   "OSC subscribe: send /deckboy/subscribe from your controller.", soft);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{notesX, notesLineY + netLineH, notesTextW, sLineH},
                   "NDI transport is configured per output in Video Outputs.", soft);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{notesX, notesLineY + netLineH * 2, notesTextW, sLineH},
                   "NMC mode / host / port are runtime-configurable for sync work.", soft);

      drawCard(integrationRect, "INTEGRATION ADAPTERS", "ATEM, NDI trigger, NMC, MTC, LTC, Art-Net, and Tally/TSL");
      IntegrationBackendRuntimeRoute integrationRoute = resolveIntegrationBackendRuntimeRoute();
      const int integX = cardBodyX(integrationRect);
      const int integrationLineY = cardBodyY(integrationRect);
      int integTextW = cardBodyW(integrationRect);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{integX, integrationLineY, integTextW, sLineH},
                   integrationRoute.summary, soft);
      int atemBridgePortDisplay = atemBridgeListenPort_;
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{integX, integrationLineY + netLineH, integTextW, sLineH},
                   "atem " + std::to_string(atemBridgePortDisplay) + "  artnet " + std::to_string(project_.artNetPort), soft);

      int pillGap = sPad;
      int pillW = std::max(uiScaled(120), (integTextW - pillGap) / 2);
      int pillH = sRowH;
      int pillX1 = integX;
      int pillX2 = pillX1 + pillW + pillGap;
      int pillY = integrationLineY + netLineH * 2 + sGap;
      SDL_Rect atemBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect ndiTrigBtn {pillX2, pillY, pillW, pillH};
      pillY += pillH + sGap;
      SDL_Rect nmcBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect mtcBtn {pillX2, pillY, pillW, pillH};
      pillY += pillH + sGap;
      SDL_Rect ltcBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect artNetBtn {pillX2, pillY, pillW, pillH};
      drawPill(atemBtn, project_.atemTriggerEnabled, "ATEM ON", "ATEM OFF", kSettingsActionIntegrationAtemToggle);
      drawPill(ndiTrigBtn, project_.ndiTriggerEnabled, "NDI TRIGGER ON", "NDI TRIGGER OFF", kSettingsActionIntegrationNdiTriggerToggle);
      drawPill(nmcBtn, project_.nmcSyncEnabled, "NMC ON", "NMC OFF", kSettingsActionIntegrationNmcToggle);
      drawPill(mtcBtn, project_.mtcIngestEnabled, "MTC ON", "MTC OFF", kSettingsActionIntegrationMtcToggle);
      drawPill(ltcBtn, project_.ltcIngestEnabled, "LTC ON", "LTC OFF", kSettingsActionIntegrationLtcToggle);
      drawPill(artNetBtn, project_.dmxArtNetEnabled, "ARTNET ON", "ARTNET OFF", kSettingsActionIntegrationArtNetToggle);
      pillY += pillH + sGap;
      SDL_Rect tslBtn {pillX1, pillY, pillW, pillH};
      SDL_Rect tcChaseBtn {pillX2, pillY, pillW, pillH};
      pillY += pillH + sGap;
      SDL_Rect tcRunBtn {pillX1, pillY, pillW, pillH};
      drawPill(tslBtn, project_.tslTallyEnabled, "TALLY ON", "TALLY OFF", kSettingsActionIntegrationTslToggle);
      drawPill(tcChaseBtn, focusedDeck().timecodeChaseEnabled, "TC CHASE ON", "TC CHASE OFF", kSettingsActionIntegrationTimecodeChaseToggle);
      drawPill(tcRunBtn, focusedDeck().timecodeRunEnabled, "TC RUN ON", "TC RUN OFF", kSettingsActionIntegrationTimecodeRunToggle);

      // Two footer rows pinned to the bottom of the card.
      const int integFooterRowH = sRowH;
      int footerY = integrationRect.y + integrationRect.h - sPad - integFooterRowH * 2 - sGap;
      SDL_Rect tslPortBtn {integX, footerY, uiScaled(148), integFooterRowH};
      Primitives::drawFramedPanel(controlRenderer_, tslPortBtn, pal.mid, pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Tally :" + std::to_string(project_.tslTallyPort), ink, tslPortBtn);
      settingsBtns_.push_back({tslPortBtn, kSettingsActionIntegrationTslPortPrompt, "integration_tsl_port"});
      SDL_Rect tslAddrBtn {tslPortBtn.x + tslPortBtn.w + sPad, footerY,
                           std::max(uiScaled(40),
                                    integrationRect.x + integrationRect.w - sPad
                                      - (tslPortBtn.x + tslPortBtn.w + sPad)),
                           integFooterRowH};
      Primitives::drawFramedPanel(controlRenderer_, tslAddrBtn, pal.mid, pal.deep, pal.light);
      std::string tslAddrLabel = project_.tslTallyAddress.empty() ? "255.255.255.255" : project_.tslTallyAddress;
      drawCenteredText(controlRenderer_, fontSmall_, tslAddrLabel, ink, tslAddrBtn);
      settingsBtns_.push_back({tslAddrBtn, kSettingsActionIntegrationTslAddrPrompt, "integration_tsl_address"});

      SDL_Rect artNetPortBtn {integX, footerY + integFooterRowH + sGap,
                              uiScaled(148), integFooterRowH};
      Primitives::drawFramedPanel(controlRenderer_, artNetPortBtn, pal.mid,
                                  pal.deep, pal.light);
      drawCenteredText(controlRenderer_, fontSmall_, "Art-Net " + std::to_string(project_.artNetPort), ink, artNetPortBtn);
      settingsBtns_.push_back({artNetPortBtn, kSettingsActionIntegrationArtNetPortPrompt, "integration_artnet_port"});
      bool allAdaptersEnabled = project_.atemTriggerEnabled && project_.ndiTriggerEnabled &&
                                project_.nmcSyncEnabled && project_.mtcIngestEnabled &&
                                project_.ltcIngestEnabled && project_.dmxArtNetEnabled;
      SDL_Rect allToggleBtn {artNetPortBtn.x + artNetPortBtn.w + sPad, artNetPortBtn.y,
                             std::max(uiScaled(40),
                                      integrationRect.x + integrationRect.w - sPad
                                        - (artNetPortBtn.x + artNetPortBtn.w + sPad)),
                             integFooterRowH};
      drawPill(allToggleBtn, allAdaptersEnabled, "ALL ON", "ALL OFF", kSettingsActionIntegrationAllToggle);

    } else if (settingsTab_ == 3) {
      // Video Outputs tab (simplified)
      const OutputTarget& outputTarget = focusedOutput();
      int focusedOutputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
      auto [nativeW, nativeH] = displayNativeRenderSize(outputDisplayIndex(project_.focusedOutputIndex));
      auto [targetW, targetH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
      std::string outputTypeLabel = normalizeOutputType(outputTarget.outputType);
      std::string mirrorLabel = outputTarget.mirrorSourceOutputIndex >= 0
        ? outputLabel(outputTarget.mirrorSourceOutputIndex)
        : "off";
      std::string streamProtocol = normalizeOutputStreamProtocol(outputTarget.streamProtocol);
      std::string streamUrl = trim(outputTarget.streamUrl);
      if (streamUrl.empty()) {
        streamUrl = defaultOutputStreamUrl(streamProtocol, focusedOutputIndex);
      }
        int outputDelayMs = std::clamp(outputTarget.outputDelayMs, 0, 5000);
      std::string outputColorSpace = normalizeOutputColorSpace(outputTarget.outputColorSpace);
      std::string outputLayoutMode = normalizeOutputLayoutMode(outputTarget.outputLayoutMode);
      int outputOrientation = normalizeOutputOrientationDegrees(outputTarget.outputOrientationDegrees);
      std::string ndiSource = trim(outputTarget.ndiSourceName).empty()
        ? defaultOutputNdiSourceName(outputTarget, focusedOutputIndex)
        : outputTarget.ndiSourceName;
      bool anyOutputTestCardsOn = false;
      bool anyOutputTestCardsOff = false;
      for (const auto& out : project_.outputs) {
        if (out.outputTestCardEnabled) {
          anyOutputTestCardsOn = true;
        } else {
          anyOutputTestCardsOff = true;
        }
      }
      auto drawActionBtn = [&](const SDL_Rect& rect, const std::string& label, int action, bool active = false) {
        SDL_Color fill = active ? pal.dark : pal.mid;
        SDL_Color txt = active ? pal.light : ink;
        Primitives::drawFramedPanel(controlRenderer_, rect, fill, pal.deep, pal.light);
        drawCenteredText(controlRenderer_, fontSmall_, label, txt, rect);
        settingsBtns_.push_back({rect, action, label});
      };

      // ═══════════════════════════════════════════════════════════════
      // VIDEO OUTPUTS — Sub-tabbed layout
      // ═══════════════════════════════════════════════════════════════

      int availW = content.w - uiScaled(24);
      const int kRowH = sTallH;
      constexpr int kLabelH = 16;
      const int kRowGap = uiScaled(5);
      constexpr int kSectionGap = 14;

      // Section frame helper — same cartridge label-plate treatment as
      // drawSettingsCard; body rect is unchanged so section content is
      // untouched.
      // Shares the plate contract with drawSettingsCard so a section header on
      // this tab sits exactly where a card header sits on the System tab.
      auto drawSectionFrame = [&](const SDL_Rect& rect, const std::string& title) {
        Primitives::drawFramedPanel(controlRenderer_, rect, pal.shellInner, pal.deep, pal.light);
        SDL_Rect hdr = drawSettingsPlate(rect, title, fontSmall_);
        int bodyTop = hdr.y + hdr.h + 6;
        return SDL_Rect {rect.x + 8, bodyTop, rect.w - 16,
                         std::max(0, rect.y + rect.h - bodyTop - 8)};
      };
      auto sectionHeaderRect = [&](const SDL_Rect& rect) {
        return settingsPlateRect(rect, fontSmall_);
      };

      // ─── Header: Output Selector + Enable Toggle ───
      int headerBtnW = std::min(uiScaled(260), (availW - uiScaled(12)) / 2);
      SDL_Rect outSelectRect {cx, cy, headerBtnW, std::max(sTallH, uiScaled(32))};
      drawUIDropdown(outSelectRect, "Output", outputLabel(focusedOutputIndex), "settings.focused_output");
      settingsBtns_.push_back({outSelectRect, 251, "cycle_output"});
      SDL_Rect enableBtn {cx + headerBtnW + uiScaled(12), cy, headerBtnW, std::max(sTallH, uiScaled(32))};
      drawActionBtn(enableBtn, outputTarget.enabled ? "OUTPUT ON" : "OUTPUT OFF", kSettingsActionOutputToggle, outputTarget.enabled);
      cy += std::max(sTallH, uiScaled(32)) + uiScaled(6);

      // ─── Sub-tab bar ───
      settingsVideoSubTab_ = std::clamp(settingsVideoSubTab_, 0, 3);
      // Standard AV desk vocabulary, the words that appear on a projector menu
      // or in Mitti/QLab/Resolume — not invented ones. "Screen" is the physical
      // panel this output drives, "Geometry" is the projector-menu term for
      // crop/edge shaping, "Devices" is where the frame is sent (NDI/SDI/Spout).
      // The earlier "Shaping"/"Destinations" pair was atypical and overlapping.
      const std::vector<std::string> subTabs {"Screen", "Geometry", "Devices", "Streaming"};
      {
        // Same cartridge-shelf treatment as the main tab bar, one size down.
        const int kSubTabH = std::max(uiScaled(30), textLineHeight(fontSmall_) + uiScaled(12));
        int subTabW = (availW - (static_cast<int>(subTabs.size()) - 1) * uiScaled(4)) / static_cast<int>(subTabs.size());
        for (int st = 0; st < static_cast<int>(subTabs.size()); ++st) {
          bool active = (st == settingsVideoSubTab_);
          int recess = active ? 0 : uiScaled(4);
          SDL_Rect stBtn {cx + st * (subTabW + uiScaled(4)), cy + recess, subTabW, kSubTabH - recess};
          Primitives::drawFramedPanel(controlRenderer_, stBtn,
                                      active ? pal.dark : pal.light, pal.deep,
                                      active ? pal.light : pal.mid);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, stBtn, subTabs[st],
                               active ? pal.light : pal.deep);
          settingsBtns_.push_back({stBtn, kSettingsActionVideoSubTabBase + st, subTabs[st]});
        }
        cy += kSubTabH + uiScaled(10);
      }

      // ─── Sub-tab content area ───
      int subContentW = availW;
      int subContentH = content.y + content.h - cy - 4;
      int sy = cy; // content Y cursor

      // ═══════════════════════════════════════════════════════════════
      if (settingsVideoSubTab_ == 0) {
      // ─── DISPLAY sub-tab ─────────────────────────────────────────
        int displayCount = deckboyGetNumVideoDisplays();

        // Display & Raster — 3 rows (display, resolution, fullscreen+orientation)
        // 4 rows now (display, resolution, raster/refresh/depth, fullscreen+orientation).
        int dispSectionH = settingsHeaderHeight(fontSmall_) + 4 * (kRowH + kRowGap) + 8;
        SDL_Rect displaySection {cx, sy, subContentW, dispSectionH};
        SDL_Rect dBody = drawSectionFrame(displaySection, "DISPLAY & RASTER");
        VerticalLayout dLayout(dBody, kRowGap);

        std::string displayLabel = displayCount <= 0 ? "None" : ("Display " + std::to_string(outputDisplayIndex(focusedOutputIndex) + 1));
        if (displayCount > 0) {
          const char* dName = deckboyGetDisplayName(outputDisplayIndex(focusedOutputIndex));
          if (dName && *dName) displayLabel += ": " + std::string(dName);
        }
        SDL_Rect dBtn = dLayout.takeFixed(kRowH);
        drawUIDropdown(dBtn, "Hardware Display", displayLabel, "settings.output_display");
        settingsBtns_.push_back({dBtn, kSettingsActionOutputDisplayDropdown, "output_display"});

        SDL_Rect rBtn = dLayout.takeFixed(kRowH);
        std::string resLabel = std::to_string(targetW) + "x" + std::to_string(targetH) + (project_.outputFollowDisplay ? " (Native)" : " (Fixed)");
        drawUIDropdown(rBtn, "Resolution", resLabel, "settings.output_raster");
        settingsBtns_.push_back({rBtn, 237, "custom_raster"});

        // Raster mode / refresh / bit depth. These handlers existed but had NO
        // UI at all — an audit found them reachable only as dead action ids.
        // That mattered twice over: refresh rate had no control anywhere, and
        // once a fixed raster was set there was no way back to display-native
        // (the Resolution row above only ever sets FIXED). Fixed raster and
        // refresh are also what qualify an output for exclusive fullscreen, so
        // losing them quietly cost a real capability.
        {
          SDL_Rect row = dLayout.takeFixed(kRowH);
          int thirdW = (row.w - 8) / 3;
          SDL_Rect modeBtn {row.x, row.y, thirdW, kRowH};
          const bool nativeMode = project_.outputFollowDisplay;
          // Native is action 230; FIXED is the custom-raster editor (237), so
          // the button offers whichever the operator is not currently in.
          drawActionBtn(modeBtn, nativeMode ? "Raster: NATIVE" : "Raster: FIXED",
                        nativeMode ? 237 : 230, nativeMode);
          SDL_Rect refreshBtn {row.x + thirdW + 4, row.y, thirdW, kRowH};
          drawActionBtn(refreshBtn, "Refresh: " + outputRefreshRateLabel(), 241);
          // Cycle auto -> 8 -> 10 -> auto by dispatching the existing per-value
          // handlers; no new action id needed.
          const int depth = project_.outputBitDepth;
          const int depthAction = (depth == 0) ? 243 : (depth == 8) ? 244 : 242;
          SDL_Rect depthBtn {row.x + (thirdW + 4) * 2, row.y, row.w - (thirdW + 4) * 2, kRowH};
          drawActionBtn(depthBtn, "Depth: " + outputBitDepthModeLabel(), depthAction);
        }

        // Fullscreen + orientation share a row — two half-width actions read
        // better than stacked full-width bars in a card this wide.
        SDL_Rect pairRow = dLayout.takeFixed(kRowH);
        SDL_Rect fsBtn {pairRow.x, pairRow.y, (pairRow.w - 8) / 2, pairRow.h};
        SDL_Rect orientBtn {pairRow.x + (pairRow.w - 8) / 2 + 8, pairRow.y,
                            pairRow.w - (pairRow.w - 8) / 2 - 8, pairRow.h};
        drawActionBtn(fsBtn, "Toggle Fullscreen", 236);
        std::string orientLabel = outputOrientation == 0 ? "0\xc2\xb0 (Normal)"
                                : std::to_string(outputOrientation) + "\xc2\xb0";
        drawActionBtn(orientBtn, "Orientation: " + orientLabel, kSettingsActionOutputOrientationCycle);
        sy += dispSectionH + kSectionGap;

        // Connected Displays — sized for EVERY display so the operator can
        // see and click all of them at once (this list used to collapse to a
        // row or two when the modal was short), clamped to remaining space.
        // Header height comes from the shared plate contract — hardcoding 32
        // here made the section too short once the header scaled with the
        // font, and the row loop silently dropped the last display.
        const int dispListHeaderH = settingsHeaderHeight(fontSmall_);
        int dispListNeededH = dispListHeaderH
                            + std::max(1, displayCount) * (kRowH + uiScaled(4)) + sPad;
        int dispListAvailH = subContentH - dispSectionH - kSectionGap;
        int dispListH = std::clamp(dispListAvailH,
                                   dispListHeaderH + (kRowH + uiScaled(4)) + sPad,
                                   dispListNeededH);
        SDL_Rect dispListSection {cx, sy, subContentW, dispListH};
        SDL_Rect dispListBody = drawSectionFrame(dispListSection, "CONNECTED DISPLAYS");
        {
          // Sits inside the header plate, sharing its vertical extent — it used
          // to be positioned from the card frame with its own height, which put
          // it over the plate's right end and through the card border.
          SDL_Rect plate = sectionHeaderRect(dispListSection);
          int idW = std::min(uiScaled(96), std::max(0, plate.w - uiScaled(12)));
          SDL_Rect idBtn {plate.x + plate.w - idW - uiScaled(3), plate.y + uiScaled(2),
                          idW, std::max(1, plate.h - uiScaled(4))};
          Primitives::drawFramedPanel(controlRenderer_, idBtn, pal.mid, pal.deep, pal.light);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, idBtn, "IDENTIFY", pal.deep);
          settingsBtns_.push_back({idBtn, kSettingsActionDisplayIdentify,
                                   "Show a number badge on every display"});
        }
        int dispRowY = dispListBody.y;
        int currentDispIdx = outputDisplayIndex(focusedOutputIndex);
        for (int di = 0; di < displayCount; ++di) {
          if (dispRowY + kRowH > dispListSection.y + dispListSection.h - 4) break;
          SDL_Rect dispRow {dispListBody.x, dispRowY, dispListBody.w, kRowH};
          bool selected = di == currentDispIdx;
          Primitives::drawFramedPanel(controlRenderer_, dispRow,
                                      selected ? pal.dark : pal.shellInner, pal.deep, pal.mid);
          const char* dNameRaw = deckboyGetDisplayName(di);
          std::string dNameStr = dNameRaw && *dNameRaw ? dNameRaw : ("Display " + std::to_string(di + 1));
          SDL_Rect dispBounds;
          deckboyGetDisplayBounds(di, &dispBounds);
          std::string dispInfo = std::to_string(di + 1) + ": " + dNameStr
            + "  " + std::to_string(dispBounds.w) + "x" + std::to_string(dispBounds.h);
          if (selected) dispInfo += "  [ASSIGNED]";
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect{dispRow.x + 6, dispRow.y + 7, dispRow.w - 12, 16},
                       dispInfo, selected ? pal.light : ink);
          settingsBtns_.push_back({dispRow, kSettingsActionOutputDisplaySelectBase + di, "display_select"});
          dispRowY += kRowH + 4;
        }

      // ═══════════════════════════════════════════════════════════════
      } else if (settingsVideoSubTab_ == 1) {
      // ─── PROCESSING sub-tab ──────────────────────────────────────

        // Edge Blending — shown in pixels of the output raster (same scheme
        // as AOI below; operators get px everywhere on this tab). Stored as
        // fractions of the raster dimension, unchanged.
        {
          const Deck& bd = focusedDeck();
          auto [ebRasterW, ebRasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
          ebRasterW = std::max(16, ebRasterW);
          ebRasterH = std::max(16, ebRasterH);
          // Sized from the shared header + row metrics, not a magic number: rows are
          // font-derived now, so a fixed height clipped the last control.
          int blendH = settingsHeaderHeight(fontSmall_) + 2 * (kRowH + kRowGap) + 8;
          SDL_Rect blendSection {cx, sy, subContentW, blendH};
          // "Feathering", not "blending": this softens THIS output's own edges.
          // True multi-projector edge blending (gamma-matched overlap between
          // two outputs) is a Super Deckboy job — see kSuperDeckboySpanningUi.
          SDL_Rect blendBody = drawSectionFrame(blendSection, "EDGE FEATHERING");
          int bx = blendBody.x + 2;
          int labelY = blendBody.y;
          int btnY = rowYBelowLabel(labelY, fontSmall_, 2);
          int bw = (blendBody.w - 18) / 4;
          int bgap = 6;
          auto drawBlendCtrl = [&](const char* label, float val, int raster,
                                   int decAction, int incAction) {
            int px = static_cast<int>(std::lround(val * raster));
            std::string valStr = std::string(label) + ": " + std::to_string(px) + "px";
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{bx, labelY, bw, 16}, valStr, soft);
            int btnW = (bw - 4) / 2;
            SDL_Rect decBtn {bx, btnY, btnW, 24};
            SDL_Rect incBtn {bx + btnW + 4, btnY, btnW, 24};
            Primitives::drawFramedPanel(controlRenderer_, decBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "-", ink, decBtn);
            settingsBtns_.push_back({decBtn, decAction, "blend"});
            Primitives::drawFramedPanel(controlRenderer_, incBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "+", ink, incBtn);
            settingsBtns_.push_back({incBtn, incAction, "blend"});
            bx += bw + bgap;
          };
          drawBlendCtrl("Left", bd.edgeBlendLeft, ebRasterW, kSettingsActionOutputEdgeBlendLDec, kSettingsActionOutputEdgeBlendLInc);
          drawBlendCtrl("Right", bd.edgeBlendRight, ebRasterW, kSettingsActionOutputEdgeBlendRDec, kSettingsActionOutputEdgeBlendRInc);
          drawBlendCtrl("Top", bd.edgeBlendTop, ebRasterH, kSettingsActionOutputEdgeBlendTDec, kSettingsActionOutputEdgeBlendTInc);
          drawBlendCtrl("Bottom", bd.edgeBlendBottom, ebRasterH, kSettingsActionOutputEdgeBlendBDec, kSettingsActionOutputEdgeBlendBInc);
          sy += blendH + kSectionGap;
        }

        // Area of Interest — edited as a pixel rect of the output raster
        // (a resolution + position, the way operators think about slices),
        // not four edge percentages. Fractions remain the storage format.
        {
          const OutputTarget& ot = focusedOutput();
          bool aoiActive = ot.aoiLeft > 0.001f || ot.aoiRight > 0.001f
                        || ot.aoiTop > 0.001f   || ot.aoiBottom > 0.001f;
          AoiRectPx aoi = focusedOutputAoiRectPx();
          // Height is DERIVED from the two label+control rows below, never a
          // magic number — a hardcoded section height is what silently clipped
          // DeckLink's 10-bit toggle once rows became font-derived (v0.81.3).
          const int aoiHdrH = 26;
          const int aoiLabelH = textLineHeight(fontSmall_);
          const int aoiCtrlH = 24;
          const int aoiRowH = aoiLabelH + 4 + aoiCtrlH;
          int aoiH = aoiHdrH + 6 + aoiRowH + kRowGap + aoiRowH + 10;
          SDL_Rect aoiSection {cx, sy, subContentW, aoiH};
          SDL_Color aoiFill = aoiActive ? pal.dark : pal.shellInner;
          SDL_Color aoiInk2 = aoiActive ? pal.light : ink;
          Primitives::drawFramedPanel(controlRenderer_, aoiSection, aoiFill, pal.deep, pal.light);
          SDL_Rect aoiHdr {aoiSection.x, aoiSection.y, aoiSection.w, 26};
          Primitives::drawFramedPanel(controlRenderer_, aoiHdr, aoiActive ? pal.dark : pal.mid, pal.deep, pal.light);
          std::string aoiTitle = aoiActive
            ? "AREA OF INTEREST  " + std::to_string(aoi.w) + "x" + std::to_string(aoi.h)
              + " @ " + std::to_string(aoi.x) + "," + std::to_string(aoi.y)
            : "AREA OF INTEREST  full " + std::to_string(aoi.rasterW) + "x" + std::to_string(aoi.rasterH);
          drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{aoiHdr.x + 8, aoiHdr.y + 6, aoiHdr.w - 72, aoiHdr.h - 8},
                       aoiTitle, aoiActive ? pal.light : pal.deep);
          SDL_Rect aoiResetBtn {aoiSection.x + aoiSection.w - 56, aoiSection.y + 4, 50, 18};
          Primitives::drawFramedPanel(controlRenderer_, aoiResetBtn, pal.mid, pal.deep, pal.light);
          drawCenteredText(controlRenderer_, fontSmall_, "FULL", ink, aoiResetBtn);
          settingsBtns_.push_back({aoiResetBtn, kSettingsActionOutputAoiReset, "aoi_reset"});
          // An area of interest is "send THIS resolution out of that raster",
          // so the primary control is a size, not four independent edges.
          // Row 1 picks the region size; row 2 places it. WIDTH/HEIGHT keep
          // typed entry via the size dropdown's custom path, and the nudge
          // actions all still exist for remote/Companion control.
          int abx = aoiSection.x + 16;
          int ably = aoiSection.y + aoiHdrH + 6;
          int aoBtnY = ably + aoiLabelH + 4;
          int aoiInnerW = aoiSection.w - 32;
          int aoiRow2LabelY = aoBtnY + aoiCtrlH + kRowGap;
          int aoiRow2Y = aoiRow2LabelY + aoiLabelH + 4;

          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect{abx, ably, aoiInnerW, aoiLabelH}, "REGION SIZE",
                       aoiActive ? pal.light : soft);
          int centreW = std::max(72, aoiInnerW / 4);
          SDL_Rect aoiSizeBtn {abx, aoBtnY, aoiInnerW - centreW - 6, aoiCtrlH};
          std::string sizeLabel = aoiActive
            ? std::to_string(aoi.w) + "x" + std::to_string(aoi.h)
            : "Full  " + std::to_string(aoi.rasterW) + "x" + std::to_string(aoi.rasterH);
          drawUIDropdown(aoiSizeBtn, "Size", sizeLabel, "settings.aoi_size");
          settingsBtns_.push_back({aoiSizeBtn, kSettingsActionOutputAoiSizeDropdown, "aoi_size"});
          SDL_Rect aoiCentreBtn {abx + aoiInnerW - centreW, aoBtnY, centreW, aoiCtrlH};
          Primitives::drawFramedPanel(controlRenderer_, aoiCentreBtn, pal.mid, pal.deep, pal.light);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, aoiCentreBtn, "CENTRE", aoiInk2);
          settingsBtns_.push_back({aoiCentreBtn, kSettingsActionOutputAoiCentre, "aoi_centre"});

          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect{abx, aoiRow2LabelY, aoiInnerW, aoiLabelH}, "POSITION",
                       aoiActive ? pal.light : soft);
          int posW = (aoiInnerW - 8) / 2;
          auto drawAoiPosCtrl = [&](const char* label, int px, int slotX,
                                    int decAct, int incAct, int editAct) {
            SDL_Rect lblRect {slotX, aoiRow2Y, 18, aoiCtrlH};
            drawCenteredTextSafe(controlRenderer_, fontSmall_, lblRect, label,
                                 aoiActive ? pal.light : soft);
            int nudgeW = 22;
            int fieldX = slotX + 20;
            int fieldW = posW - 20;
            SDL_Rect decBtn {fieldX, aoiRow2Y, nudgeW, aoiCtrlH};
            SDL_Rect valBtn {fieldX + nudgeW + 3, aoiRow2Y, fieldW - 2 * (nudgeW + 3), aoiCtrlH};
            SDL_Rect incBtn {fieldX + fieldW - nudgeW, aoiRow2Y, nudgeW, aoiCtrlH};
            Primitives::drawFramedPanel(controlRenderer_, decBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "-", aoiInk2, decBtn);
            settingsBtns_.push_back({decBtn, decAct, "aoi"});
            Primitives::drawFramedPanel(controlRenderer_, valBtn, pal.shellInner, pal.deep, pal.light);
            drawCenteredTextSafe(controlRenderer_, fontSmall_, valBtn, std::to_string(px),
                                 aoiActive ? pal.light : ink);
            settingsBtns_.push_back({valBtn, editAct, "aoi_edit"});
            Primitives::drawFramedPanel(controlRenderer_, incBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "+", aoiInk2, incBtn);
            settingsBtns_.push_back({incBtn, incAct, "aoi"});
          };
          drawAoiPosCtrl("X", aoi.x, abx, kSettingsActionOutputAoiXDec,
                         kSettingsActionOutputAoiXInc, kSettingsActionOutputAoiXEdit);
          drawAoiPosCtrl("Y", aoi.y, abx + posW + 8, kSettingsActionOutputAoiYDec,
                         kSettingsActionOutputAoiYInc, kSettingsActionOutputAoiYEdit);
          sy += aoiH + kSectionGap;
        }

        // Default Transition — and, once Super Deckboy lands, Canvas Mode
        // beside it. Canvas is a MULTI-OUTPUT SPANNING feature, so it is parked
        // and hidden until that mode exists (see kSuperDeckboySpanningUi).
        // With canvas hidden the transition panel takes the full width rather
        // than leaving a conspicuous empty half.
        {
          int halfW = kSuperDeckboySpanningUi ? (subContentW - 8) / 2 : subContentW;
          int pairH = 68;

          if (kSuperDeckboySpanningUi) {
            SDL_Rect canvasPanel {cx, sy, halfW, pairH};
            Primitives::drawFramedPanel(controlRenderer_, canvasPanel, pal.shellInner, pal.deep, pal.light);
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{canvasPanel.x + 8, canvasPanel.y + 4, canvasPanel.w - 16, 16},
                         "CANVAS MODE", ink);
            SDL_Rect canvasToggle {canvasPanel.x + 8, canvasPanel.y + 26, std::min(130, halfW - 16), kRowH};
            drawActionBtn(canvasToggle,
                          project_.outputCanvasEnabled ? "CANVAS: ON" : "CANVAS: OFF",
                          kSettingsActionCanvasToggle, project_.outputCanvasEnabled);
            if (project_.outputCanvasEnabled) {
              int cBtnX = canvasToggle.x + canvasToggle.w + 6;
              int cBtnW = std::max(56, (canvasPanel.x + canvasPanel.w - 8 - cBtnX - 4) / 2);
              SDL_Rect cwBtn {cBtnX, canvasPanel.y + 26, cBtnW, kRowH};
              Primitives::drawFramedPanel(controlRenderer_, cwBtn, pal.light, pal.deep, pal.mid);
              drawCenteredText(controlRenderer_, fontSmall_, "W:" + std::to_string(project_.outputCanvasWidth), ink, cwBtn);
              settingsBtns_.push_back({cwBtn, kSettingsActionCanvasWidthPrompt, "canvas_w"});
              SDL_Rect chBtn {cwBtn.x + cBtnW + 4, canvasPanel.y + 26, cBtnW, kRowH};
              Primitives::drawFramedPanel(controlRenderer_, chBtn, pal.light, pal.deep, pal.mid);
              drawCenteredText(controlRenderer_, fontSmall_, "H:" + std::to_string(project_.outputCanvasHeight), ink, chBtn);
              settingsBtns_.push_back({chBtn, kSettingsActionCanvasHeightPrompt, "canvas_h"});
            }
          }
          {
            const Deck& td = focusedDeck();
            int transX = kSuperDeckboySpanningUi ? cx + halfW + 8 : cx;
            SDL_Rect transPanel {transX, sy, halfW, pairH};
            Primitives::drawFramedPanel(controlRenderer_, transPanel, pal.shellInner, pal.deep, pal.light);
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{transPanel.x + 8, transPanel.y + 4, transPanel.w - 16, 16},
                         "DEFAULT TRANSITION", ink);
            int tx = transPanel.x + 8;
            int ty = transPanel.y + 26;
            int tBtnW = 32;
            SDL_Rect tDecBtn {tx, ty, tBtnW, kRowH};
            Primitives::drawFramedPanel(controlRenderer_, tDecBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "-", ink, tDecBtn);
            settingsBtns_.push_back({tDecBtn, kSettingsActionTransitionSecondsDec, "trans_dec"});
            SDL_Rect tValRect {tx + tBtnW + 4, ty, 76, kRowH};
            Primitives::drawFramedPanel(controlRenderer_, tValRect, pal.light, pal.deep, pal.mid);
            drawCenteredText(controlRenderer_, fontSmall_, formatSeconds(td.transitionSeconds), ink, tValRect);
            SDL_Rect tIncBtn {tValRect.x + tValRect.w + 4, ty, tBtnW, kRowH};
            Primitives::drawFramedPanel(controlRenderer_, tIncBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, "+", ink, tIncBtn);
            settingsBtns_.push_back({tIncBtn, kSettingsActionTransitionSecondsInc, "trans_inc"});
            int styleX = tIncBtn.x + tIncBtn.w + 6;
            int styleW = std::max(60, transPanel.x + transPanel.w - 8 - styleX);
            SDL_Rect tStyleBtn {styleX, ty, styleW, kRowH};
            std::string styleLabel = toUpper(td.transitionStyle.empty() ? "crossfade" : td.transitionStyle);
            Primitives::drawFramedPanel(controlRenderer_, tStyleBtn, pal.mid, pal.deep, pal.light);
            drawCenteredText(controlRenderer_, fontSmall_, styleLabel, ink, tStyleBtn);
            settingsBtns_.push_back({tStyleBtn, kSettingsActionTransitionStyleCycle, "trans_style"});
          }
        }

      // ═══════════════════════════════════════════════════════════════
      } else if (settingsVideoSubTab_ == 2) {
      // ─── BACKENDS sub-tab ────────────────────────────────────────

        // This Output — per-output delivery only. Layout (span/duplicate) and
        // Mirror are MULTI-OUTPUT SPANNING controls, parked until Super Deckboy
        // (kSuperDeckboySpanningUi); Color moved to the Streaming sub-tab,
        // because the stream encoder is the only thing that ever read it.
        {
          int osRows = kSuperDeckboySpanningUi ? 2 : 1;
          int osH = settingsHeaderHeight(fontSmall_) + osRows * (kRowH + kRowGap) + 8;
          SDL_Rect osSection {cx, sy, subContentW, osH};
          SDL_Rect osBody = drawSectionFrame(osSection, "THIS OUTPUT");
          VerticalLayout osLayout(osBody, kRowGap);
          {
            SDL_Rect row = osLayout.takeFixed(kRowH);
            int halfW2 = (row.w - 4) / 2;
            SDL_Rect alphaBtn {row.x, row.y, halfW2, kRowH};
            int alphaPct = static_cast<int>(std::lround(std::clamp(outputTarget.outputAlpha, 0.0f, 1.0f) * 100.0f));
            drawActionBtn(alphaBtn, "Alpha: " + std::to_string(alphaPct) + "%", kSettingsActionOutputAlphaPrompt);
            SDL_Rect delayBtn {row.x + halfW2 + 4, row.y, row.w - halfW2 - 4, kRowH};
            drawActionBtn(delayBtn, "Delay: " + std::to_string(outputDelayMs) + "ms", kSettingsActionOutputDelayPrompt);
          }
          if (kSuperDeckboySpanningUi) {
            SDL_Rect row = osLayout.takeFixed(kRowH);
            int halfW2 = (row.w - 4) / 2;
            SDL_Rect layoutBtn {row.x, row.y, halfW2, kRowH};
            drawActionBtn(layoutBtn, "Layout: " + toUpper(outputLayoutMode), kSettingsActionOutputLayoutSpan,
                          outputLayoutMode == "span");
            SDL_Rect mirrorBtn {row.x + halfW2 + 4, row.y, row.w - halfW2 - 4, kRowH};
            drawUIDropdown(mirrorBtn, "Mirror", mirrorLabel, "settings.output_mirror");
            settingsBtns_.push_back({mirrorBtn, kSettingsActionOutputMirrorDropdown, "output_mirror"});
          }
          sy += osH + kSectionGap;
        }

        // NDI Network
        {
          int ndiH = settingsHeaderHeight(fontSmall_) + 3 * (kRowH + kRowGap) + 8;
          SDL_Rect ndiSection {cx, sy, subContentW, ndiH};
          SDL_Rect nBody = drawSectionFrame(ndiSection, "NDI\xc2\xae NETWORK");
          VerticalLayout nLayout(nBody, kRowGap);
          drawActionBtn(nLayout.takeFixed(kRowH), outputTarget.ndiEnabled ? "NDI SENDING: ON" : "NDI SENDING: OFF", 271, outputTarget.ndiEnabled);
          SDL_Rect nnBtn = nLayout.takeFixed(kRowH);
          std::string ndiName = trim(outputTarget.ndiSourceName).empty() ? "Default" : outputTarget.ndiSourceName;
          drawUIDropdown(nnBtn, "Source", ndiName, "settings.ndi_name");
          settingsBtns_.push_back({nnBtn, 272, "ndi_name"});
          {
            // The key toggle had UI but the key SOURCE NAME did not — its
            // handler (274) was reachable only as a dead action id, so an
            // alpha-key NDI sender could be armed but never named.
            SDL_Rect row = nLayout.takeFixed(kRowH);
            int halfN = (row.w - 4) / 2;
            SDL_Rect ndiKeyBtn {row.x, row.y, halfN, kRowH};
            drawActionBtn(ndiKeyBtn, outputTarget.ndiKeyEnabled ? "NDI KEY: ON" : "NDI KEY: OFF",
                          273, outputTarget.ndiKeyEnabled);
            SDL_Rect keyNameBtn {row.x + halfN + 4, row.y, row.w - halfN - 4, kRowH};
            std::string keyName = trim(outputTarget.ndiKeySourceName);
            if (keyName.empty()) {
              keyName = defaultOutputNdiKeySourceName(outputTarget, focusedOutputIndex);
            }
            drawActionBtn(keyNameBtn, "Key name: " + keyName, 274);
          }
          sy += ndiH + kSectionGap;
        }

        // DeckLink + Spout side by side
        {
          int halfW = (subContentW - 8) / 2;

          // DeckLink
          int dlH = settingsHeaderHeight(fontSmall_) + 4 * (kRowH + kRowGap) + 8;
          SDL_Rect dlSection {cx, sy, halfW, dlH};
          SDL_Rect dlBody = drawSectionFrame(dlSection, "DECKLINK SDI / HDMI");
          VerticalLayout dlLayout(dlBody, kRowGap);
          SDL_Rect dlToggle = dlLayout.takeFixed(kRowH);
          drawActionBtn(dlToggle,
                        outputTarget.deckLinkEnabled ? "DECKLINK: ON" : "DECKLINK: OFF",
                        kSettingsActionDeckLinkToggle, outputTarget.deckLinkEnabled);
          {
            std::string devLabel = "None";
            if (outputTarget.deckLinkDeviceId >= 0) {
              auto devices = deckboy::platform::video::DeckLinkOutput::listDevices();
              for (const auto& d : devices) {
                if (d.id == outputTarget.deckLinkDeviceId) {
                  devLabel = d.modelName;
                  if (d.supportsSDI && d.supportsHDMI) devLabel += " (SDI+HDMI)";
                  else if (d.supportsSDI) devLabel += " (SDI)";
                  else if (d.supportsHDMI) devLabel += " (HDMI)";
                  break;
                }
              }
            }
            SDL_Rect devBtn = dlLayout.takeFixed(kRowH);
            drawUIDropdown(devBtn, "Device", devLabel, "settings.decklink_device");
            settingsBtns_.push_back({devBtn, kSettingsActionDeckLinkDeviceDropdown, "decklink_device"});
          }
          {
            auto mode = deckboy::platform::video::parseDeckLinkMode(outputTarget.deckLinkMode);
            std::string modeLabel2 = deckboy::platform::video::deckLinkModeLabel(mode);
            SDL_Rect modeBtn = dlLayout.takeFixed(kRowH);
            drawUIDropdown(modeBtn, "Mode", modeLabel2, "settings.decklink_mode");
            settingsBtns_.push_back({modeBtn, kSettingsActionDeckLinkModeDropdown, "decklink_mode"});
          }
          SDL_Rect bitBtn = dlLayout.takeFixed(kRowH);
          drawActionBtn(bitBtn, outputTarget.deckLink10Bit ? "10-BIT: ON" : "10-BIT: OFF",
                        kSettingsActionDeckLink10BitToggle, outputTarget.deckLink10Bit);

          // Spout
          int spH = dlH; // match DeckLink height
          SDL_Rect spSection {cx + halfW + 8, sy, halfW, spH};
          SDL_Rect spBody = drawSectionFrame(spSection, "SPOUT TEXTURE SHARE");
          VerticalLayout spLayout(spBody, kRowGap);
          SDL_Rect spToggle = spLayout.takeFixed(kRowH);
          drawActionBtn(spToggle,
                        outputTarget.spoutEnabled ? "SPOUT: ON" : "SPOUT: OFF",
                        kSettingsActionSpoutToggle, outputTarget.spoutEnabled);
          {
            std::string spName = trim(outputTarget.spoutSenderName);
            if (spName.empty()) {
              spName = "Deckboy Output " + std::to_string(focusedOutputIndex + 1);
            }
            SDL_Rect spNameBtn = spLayout.takeFixed(kRowH);
            drawUIDropdown(spNameBtn, "Sender", spName, "settings.spout_name");
            settingsBtns_.push_back({spNameBtn, kSettingsActionSpoutNamePrompt, "spout_name"});
          }
          sy += dlH + kSectionGap;
        }

        // ST 2110-20 — labelled EXPERIMENTAL on purpose. It is a real, correctly
        // packetised sender, but it is not PTP-locked and not narrow-model
        // paced, so it will not genlock in a broadcast plant. Saying so in the
        // header is cheaper than an operator discovering it at a venue.
        {
          // 3 control rows + 2 status lines. The status lines are the ones that
          // get clipped if this is under-sized, and they are the ones that
          // matter (pacing + clock traceability), so give them real room.
          int stH = settingsHeaderHeight(fontSmall_) + 3 * (kRowH + kRowGap)
                  + 2 * (kLabelH + 4) + 12;
          SDL_Rect stSection {cx, sy, subContentW, stH};
          SDL_Rect stBody = drawSectionFrame(stSection, "SMPTE ST 2110-20  (EXPERIMENTAL)");
          VerticalLayout stLayout(stBody, kRowGap);

          drawActionBtn(stLayout.takeFixed(kRowH),
                        outputTarget.st2110Enabled ? "ST 2110: ON" : "ST 2110: OFF",
                        kSettingsActionSt2110Toggle, outputTarget.st2110Enabled);
          {
            SDL_Rect row = stLayout.takeFixed(kRowH);
            int addrW = (row.w - 4) * 2 / 3;
            SDL_Rect addrBtn {row.x, row.y, addrW, kRowH};
            std::string addr = trim(outputTarget.st2110Address);
            if (addr.empty()) addr = "239.20.10.1";
            drawActionBtn(addrBtn, "Group: " + addr, kSettingsActionSt2110AddressPrompt);
            SDL_Rect portBtn {row.x + addrW + 4, row.y, row.w - addrW - 4, kRowH};
            drawActionBtn(portBtn, "Port: " + std::to_string(outputTarget.st2110Port),
                          kSettingsActionSt2110PortPrompt);
          }
          {
            SDL_Rect row = stLayout.takeFixed(kRowH);
            int halfSt = (row.w - 4) / 2;
            SDL_Rect depthBtn {row.x, row.y, halfSt, kRowH};
            drawActionBtn(depthBtn,
                          outputTarget.st2110TenBit ? "4:2:2  10-BIT" : "4:2:2  8-BIT",
                          kSettingsActionSt2110DepthToggle, outputTarget.st2110TenBit);
            SDL_Rect sdpBtn {row.x + halfSt + 4, row.y, row.w - halfSt - 4, kRowH};
            drawActionBtn(sdpBtn, "SHOW SDP", kSettingsActionSt2110CopySdp);
          }
          {
            SDL_Rect noteRect = stLayout.takeFixed(kLabelH);
            // Live sender telemetry when armed, the caveat when not. Pacing
            // error is the number that says whether the wide-model schedule is
            // actually being met — a burst-sending stream reads near zero here
            // because it never waits, so it is read together with drops.
            std::string note =
              "No PTP lock, wide-model pacing - will not genlock to plant sources";
            const OutputRuntime* strt = runtimeForOutput(focusedOutputIndex);
            if (strt && strt->st2110Sender && strt->st2110Sender->isOpen()) {
              char buf[200];
              // "skipped", not "dropped": the compositor presents at the
              // output's refresh rate while the stream is paced at its DECLARED
              // rate, so surplus arrivals are rate-limited away on purpose. A
              // 60 Hz output feeding a 30 fps stream skips about half, and that
              // is correct — calling it "dropped" reads as packet loss.
              std::snprintf(buf, sizeof(buf),
                            "sent %llu @ %.2f fps   skipped %llu (rate-limited)   pacing +/-%.0f us   no PTP, wide model",
                            static_cast<unsigned long long>(strt->st2110Sender->framesSent()),
                            strt->st2110Sender->config().frameRate,
                            static_cast<unsigned long long>(strt->st2110Sender->framesDropped()),
                            strt->st2110Sender->pacingErrorMicros());
              note = buf;
            }
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{stBody.x, noteRect.y, stBody.w, kLabelH}, note, soft);
          }
          {
            // Media-clock state on its own line. This is the difference between
            // a stream that merely decodes and one a switcher can cut to, so it
            // gets said plainly rather than buried in the caveat above.
            SDL_Rect clockRect = stLayout.takeFixed(kLabelH);
            const bool locked = ptpClient_.locked();
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{stBody.x, clockRect.y, stBody.w, kLabelH},
                         ptpStatusLabel(), locked ? pal.fg : soft);
          }
          sy += stH + kSectionGap;
        }

        // NMOS IS-04/05 — how the 2110 senders above become discoverable and
        // connectable. Sits directly under ST 2110 because it is meaningless
        // without it: with no 2110 output armed this node advertises nothing.
        {
          // 2 control rows + 1 status line.
          int nmH = settingsHeaderHeight(fontSmall_) + 2 * (kRowH + kRowGap)
                  + (kLabelH + 4) + 12;
          SDL_Rect nmSection {cx, sy, subContentW, nmH};
          SDL_Rect nmBody = drawSectionFrame(nmSection, "AMWA NMOS  IS-04 / IS-05");
          VerticalLayout nmLayout(nmBody, kRowGap);

          {
            SDL_Rect row = nmLayout.takeFixed(kRowH);
            int halfNm = (row.w - 4) / 2;
            SDL_Rect toggleBtn {row.x, row.y, halfNm, kRowH};
            drawActionBtn(toggleBtn, project_.nmosEnabled ? "NMOS: ON" : "NMOS: OFF",
                          kSettingsActionNmosToggle, project_.nmosEnabled);
            SDL_Rect portBtn {row.x + halfNm + 4, row.y, row.w - halfNm - 4, kRowH};
            drawActionBtn(portBtn, "Node port: " + std::to_string(project_.nmosPort),
                          kSettingsActionNmosPortPrompt);
          }
          {
            SDL_Rect row = nmLayout.takeFixed(kRowH);
            int regW = (row.w - 4) * 2 / 3;
            SDL_Rect regBtn {row.x, row.y, regW, kRowH};
            std::string registry = trim(project_.nmosRegistryUrl);
            // No mDNS, so there is nothing to auto-discover. Label the empty
            // state as a decision the operator still has to make, not as a
            // default that is quietly fine.
            drawActionBtn(regBtn,
                          registry.empty() ? std::string("Registry: NOT SET")
                                           : "Registry: " + registry,
                          kSettingsActionNmosRegistryPrompt);
            SDL_Rect ifBtn {row.x + regW + 4, row.y, row.w - regW - 4, kRowH};
            std::string ifName = trim(project_.nmosInterfaceName);
            if (ifName.empty()) ifName = "eth0";
            drawActionBtn(ifBtn, "NIC: " + ifName, kSettingsActionNmosInterfacePrompt);
          }
          {
            SDL_Rect statusRect = nmLayout.takeFixed(kLabelH);
            const bool good = project_.nmosEnabled && nmosStarted_ &&
                              (nmosNode_.registered() || trim(project_.nmosRegistryUrl).empty());
            drawTextSafe(controlRenderer_, fontSmall_,
                         SDL_Rect{nmBody.x, statusRect.y, nmBody.w, kLabelH},
                         nmosStatusLabel(), good ? pal.fg : soft);
            // The node URL is the fastest way to check what the plant sees, so
            // make it clickable rather than something to reconstruct by hand.
            if (nmosStarted_) {
              settingsBtns_.push_back({statusRect, kSettingsActionNmosShowUrl, "nmos_url"});
            }
          }
          sy += nmH + kSectionGap;
        }

      // ═══════════════════════════════════════════════════════════════
      } else if (settingsVideoSubTab_ == 3) {
      // ─── STREAMING sub-tab ───────────────────────────────────────

        // -- Two independent destinations ---------------------------------
        // Streaming used to be "whatever output is focused", with protocol as a
        // switch and one shared url/key/bitrate - so SRT and RTMP could never be
        // configured at the same time, let alone run together. The app's own
        // hint told the operator to go add a second output by hand.
        //
        // Each protocol now owns a dedicated stream output, created on demand
        // (ensureStreamOutputForProtocol). Both can be live at once, and each
        // shows only the controls its protocol actually has.
        {
          const char* kProtoIds[2] = {"srt", "rtmp"};
          const char* kProtoNames[2] = {"SRT", "RTMP / RTMPS"};
          int destH = (subContentH - kSectionGap) / 3;
          for (int dest = 0; dest < 2; ++dest) {
            const std::string protoId = kProtoIds[dest];
            const bool isSrt = (dest == 0);
            int outIdx = findStreamOutputForProtocol(protoId);
            const bool exists = outIdx >= 0;
            const OutputTarget* st = exists ? &project_.outputs[outIdx] : nullptr;
            const bool live = exists && st->enabled && st->streamEnabled;

            SDL_Rect destSection {cx, sy, subContentW, destH};
            SDL_Rect dBody2 = drawSectionFrame(destSection, kProtoNames[dest]);
            VerticalLayout dl(dBody2, kRowGap);
            auto act = [&](int field) {
              return kSettingsActionStreamDestBase
                   + dest * kSettingsActionStreamDestStride + field;
            };

            {
              SDL_Rect row = dl.takeFixed(kRowH);
              int halfR = (row.w - 4) / 2;
              SDL_Rect onBtn {row.x, row.y, halfR, kRowH};
              drawActionBtn(onBtn, live ? "STOP STREAMING" : "START STREAMING",
                            act(kStreamFieldToggle), live);
              // Animated on-air badge, then the words. The badge carries state
              // at a glance from across a room; the text carries the detail.
              SDL_Rect badge {row.x + halfR + 8, row.y + 2, kRowH - 4, kRowH - 4};
              drawStreamOnAirBadge(badge, exists, live,
                                   exists ? outputHealthStateForDisplay(outIdx)
                                          : OutputHealthState::Off,
                                   pal.light);  // on shell_inner
              std::string state = !exists ? "not configured"
                                : (live ? outputHealthLabel(outIdx) : "idle");
              if (exists && isSrt && srtPassphraseTooShort(*st)) {
                // SRT rejects short passphrases outright, so this would surface
                // as an unexplained connection failure. Say it before air.
                state = "passphrase must be 10+ chars";
              }
              drawTextSafe(controlRenderer_, fontSmall_,
                           SDL_Rect{badge.x + badge.w + 8, row.y + 4,
                                    row.w - halfR - badge.w - 16, kRowH},
                           state, soft);
            }
            {
              SDL_Rect row = dl.takeFixed(kRowH);
              std::string url = exists ? trim(st->streamUrl) : std::string();
              if (url.empty()) url = defaultOutputStreamUrl(protoId, 0);
              drawActionBtn(row, (isSrt ? "URL: " : "Server: ") + url,
                            act(kStreamFieldUrl));
            }
            {
              SDL_Rect row = dl.takeFixed(kRowH);
              int halfR = (row.w - 4) / 2;
              if (isSrt) {
                SDL_Rect modeBtn {row.x, row.y, halfR, kRowH};
                std::string mode =
                  (exists && st->srtMode == "listener") ? "LISTENER" : "CALLER";
                drawActionBtn(modeBtn, "Mode: " + mode, act(kStreamFieldSrtMode),
                              exists && st->srtMode == "listener");
                SDL_Rect latBtn {row.x + halfR + 4, row.y, row.w - halfR - 4, kRowH};
                drawActionBtn(latBtn, "Latency: "
                              + std::to_string(exists ? st->srtLatencyMs : 120) + " ms",
                              act(kStreamFieldSrtLatency));
              } else {
                // The stream key is a secret; never render it.
                std::string keyText = (exists && !trim(st->streamKey).empty())
                  ? std::string(std::min<std::size_t>(trim(st->streamKey).size(), 24), '*')
                  : std::string("click to set");
                drawActionBtn(row, "Stream key: " + keyText, act(kStreamFieldKey));
              }
            }
            if (isSrt) {
              SDL_Rect row = dl.takeFixed(kRowH);
              int halfR = (row.w - 4) / 2;
              SDL_Rect passBtn {row.x, row.y, halfR, kRowH};
              std::string pass = (exists && !trim(st->srtPassphrase).empty())
                ? std::string(std::min<std::size_t>(trim(st->srtPassphrase).size(), 24), '*')
                : std::string("none");
              drawActionBtn(passBtn, "Passphrase: " + pass,
                            act(kStreamFieldSrtPassphrase));
              SDL_Rect sidBtn {row.x + halfR + 4, row.y, row.w - halfR - 4, kRowH};
              std::string sid = exists ? trim(st->srtStreamId) : std::string();
              drawActionBtn(sidBtn, "Stream ID: " + (sid.empty() ? "none" : sid),
                            act(kStreamFieldSrtStreamId));
            }
            {
              SDL_Rect row = dl.takeFixed(kRowH);
              int halfR = (row.w - 4) / 2;
              SDL_Rect brBtn {row.x, row.y, halfR, kRowH};
              drawActionBtn(brBtn, "Bitrate: "
                            + std::to_string(exists ? st->streamBitrateKbps : 6000)
                            + " kbps", act(kStreamFieldBitrate));
              SDL_Rect kfBtn {row.x + halfR + 4, row.y, row.w - halfR - 4, kRowH};
              drawActionBtn(kfBtn, "Keyframe: "
                            + std::to_string(exists ? st->streamKeyframeSeconds : 2)
                            + "s", act(kStreamFieldKeyframe));
            }
            {
              // Audio bitrate. Was hardcoded at 160k for every stream and
              // recording -- thin for music, wasteful for a talk.
              SDL_Rect row = dl.takeFixed(kRowH);
              drawActionBtn(row, "Audio: "
                            + std::to_string(exists ? st->streamAudioBitrateKbps : 160)
                            + " kbps", act(kStreamFieldAudioBitrate));
            }
            sy += destH + kSectionGap;
          }

          // -- RECORD TO DISK -----------------------------------------------
          // Recording deserves its own destination rather than hiding as a
          // protocol on some output the operator has to go and find. It is the
          // one egress an operator starts and stops by hand mid-show.
          {
            const bool rec = recordingActive();
            SDL_Rect recSection {cx, sy, subContentW, destH};
            SDL_Rect rBody = drawSectionFrame(recSection, "RECORD TO DISK");
            VerticalLayout rl(rBody, kRowGap);
            {
              SDL_Rect row = rl.takeFixed(kRowH);
              int halfR = (row.w - 4) / 2;
              SDL_Rect recBtn {row.x, row.y, halfR, kRowH};
              drawActionBtn(recBtn, rec ? "STOP RECORDING" : "START RECORDING",
                            kSettingsActionRecordToggle, rec);
              SDL_Rect badge {row.x + halfR + 4, row.y, row.w - halfR - 4, kRowH};
              if (rec) {
                // Elapsed AND bytes on disk. Elapsed alone cannot tell you the
                // encoder is actually writing; a size that climbs can.
                const int secs = static_cast<int>(recordingElapsedSeconds());
                const std::uintmax_t mb = recordingBytesOnDisk() / (1024 * 1024);
                char t[64];
                std::snprintf(t, sizeof(t), "REC  %d:%02d:%02d   %llu MB",
                              secs / 3600, (secs / 60) % 60, secs % 60,
                              static_cast<unsigned long long>(mb));
                drawCenteredText(controlRenderer_, fontSmall_, t, pal.light, badge);
              } else {
                drawCenteredText(controlRenderer_, fontSmall_, "idle", pal.inkSoft, badge);
              }
            }
            {
              SDL_Rect row = rl.takeFixed(kRowH);
              int pickW = row.w / 4;
              SDL_Rect pathRect {row.x, row.y, row.w - pickW * 2 - 8, kRowH};
              drawTextSafe(controlRenderer_, fontSmall_, pathRect,
                           ellipsizeToPixelWidth(fontSmall_, recordingDirLabel(), pathRect.w),
                           ink);
              SDL_Rect pickBtn {pathRect.x + pathRect.w + 4, row.y, pickW, kRowH};
              drawActionBtn(pickBtn, "FOLDER...", kSettingsActionRecordDirPick,
                            !project_.recordingDir.empty());
              SDL_Rect clrBtn {pickBtn.x + pickW + 4, row.y, pickW, kRowH};
              drawActionBtn(clrBtn, "DEFAULT", kSettingsActionRecordDirClear);
            }
            if (recordingSharesAppVolume()) {
              // Stated, not enforced. It is a legitimate choice on a machine
              // with one fast disk -- but it should be a choice.
              SDL_Rect row = rl.takeFixed(kRowH);
              drawTextSafe(controlRenderer_, fontSmall_, row,
                           "same disk as Deckboy - a separate drive is safer",
                           pal.inkSoft);
            }
            sy += destH + kSectionGap;
          }
        }
      }

      // No scrollbar needed — each sub-tab fits in the viewport
      settingsVideoViewport_ = {};
      settingsVideoScrollMax_ = 0;
      settingsVideoScroll_ = 0;

    } else if (settingsTab_ == 4) {
      // About tab — masthead + credits. Two columns of labelled rows sharing
      // one label gutter, so every value starts on the same x. Content is
      // factual only: what this is, who owns it, what it is built on, and what
      // this particular build/session is doing.
      const int aboutLineH = textLineHeight(fontSmall_) + 4;
      int aboutColGap = 12;
      int aboutLeftW = std::max(300, (content.w - 24 - aboutColGap) / 2);
      int aboutRightW = std::max(300, content.w - 24 - aboutColGap - aboutLeftW);

      // ── Masthead: art + wordmark + version ──
      int mastH = std::clamp(content.h * 30 / 100, 96, 190);
      SDL_Rect mastRect {cx, cy, content.w - 24, mastH};
      Primitives::drawFramedPanel(controlRenderer_, mastRect, pal.shellInner, pal.deep, pal.light);
      int artW = std::min(mastRect.w / 2 - 16, 300);
      if (uiAboutLogo_.texture || !uiAboutLogo_.path.empty()) {
        ensureUiImageLoaded(uiAboutLogo_);
        drawUiImageContain(uiAboutLogo_,
                           SDL_Rect{mastRect.x + 10, mastRect.y + 8, artW, mastRect.h - 16}, 255);
      } else if (uiPackAvailable_) {
        drawUiImageContain(uiHeaderArt_,
                           SDL_Rect{mastRect.x + 10, mastRect.y + 10, artW, mastRect.h - 20}, 215);
      }
      {
        TTF_Font* titleFont = fontPixel_ ? fontPixel_ : fontLarge_;
        int textX = mastRect.x + artW + 24;
        int textW = std::max(0, mastRect.x + mastRect.w - 12 - textX);
        int blockH = textLineHeight(titleFont) + aboutLineH * 2 + 6;
        int ty = mastRect.y + std::max(8, (mastRect.h - blockH) / 2);
        drawTextSafe(controlRenderer_, titleFont,
                     SDL_Rect{textX, ty, textW, textLineHeight(titleFont)},
                     std::string(kAppTitle), ink);
        ty += textLineHeight(titleFont) + 4;
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{textX, ty, textW, aboutLineH},
                     "dot-matrix cue deck for live events", soft);
        ty += aboutLineH;
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{textX, ty, textW, aboutLineH},
                     std::string(kAppVersionTag) + "   ("
                       + std::string(__DATE__) + " build)", soft);
      }

      // Shared row renderer: fixed label gutter, value flush against it. Every
      // credit line on this page lands on the same two x positions.
      int aboutY2 = cy + mastH + 10;
      int colsH = std::max(120, content.y + content.h - aboutY2 - 8);
      SDL_Rect leftAbout {cx, aboutY2, aboutLeftW, colsH};
      SDL_Rect rightAbout {cx + aboutLeftW + aboutColGap, aboutY2, aboutRightW, colsH};
      auto aboutRow = [&](const SDL_Rect& card, int& rowY, const std::string& label,
                          const std::string& value) {
        int gutter = std::min(96, card.w / 3);
        int rowH = aboutLineH;
        if (rowY + rowH > card.y + card.h - 6) {
          return;
        }
        if (!label.empty()) {
          drawTextSafe(controlRenderer_, fontSmall_,
                       SDL_Rect{card.x + 12, rowY, gutter - 8, rowH}, label, soft);
        }
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect{card.x + 12 + gutter, rowY,
                              std::max(0, card.w - 24 - gutter), rowH},
                     value, ink);
        rowY += rowH;
      };

      drawSettingsCard(leftAbout, "PROJECT");
      int leftRowY = leftAbout.y + settingsHeaderHeight(fontBase_) + 2;
      aboutRow(leftAbout, leftRowY, "Copyright", "(C) 2026 Deckboy Contributors");
      aboutRow(leftAbout, leftRowY, "Licence", "GPL-3.0-or-later");
      aboutRow(leftAbout, leftRowY, "", "Free software. You may use, study, share");
      aboutRow(leftAbout, leftRowY, "", "and modify it; derivatives stay GPL.");
      aboutRow(leftAbout, leftRowY, "Source", "github.com/Utopian-Academy/Deckboy");
      leftRowY += 6;
      aboutRow(leftAbout, leftRowY, "Warranty", "None. See LICENSE sections 15-16.");
      leftRowY += 6;
      aboutRow(leftAbout, leftRowY, "Session", "Companion " + std::to_string(companionPort_)
                 + (companionReady_ ? " (up)" : " (down)"));
      aboutRow(leftAbout, leftRowY, "",
               project_.allowRemoteNetwork ? "Remote: all interfaces"
                                           : "Remote: localhost only");
      aboutRow(leftAbout, leftRowY, "", "HyperDeck 9992  |  OSC Query "
                 + std::to_string(project_.oscQueryPort));
      aboutRow(leftAbout, leftRowY, "Theme",
               currentThemeName_.empty() ? "gameboy" : currentThemeName_);

      drawSettingsCard(rightAbout, "BUILT WITH");
      int rightRowY = rightAbout.y + settingsHeaderHeight(fontBase_) + 2;
      aboutRow(rightAbout, rightRowY, "Platform",
#if defined(_WIN32)
               "Windows x64"
#elif defined(__APPLE__)
               "macOS"
#else
               "Linux"
#endif
               );
      aboutRow(rightAbout, rightRowY, "Toolkit", "SDL3  +  SDL3_ttf  (FreeType)");
      aboutRow(rightAbout, rightRowY, "Media", "FFmpeg / libav"
#if DECKBOY_INPROC_DECODE
               "  (in-process)"
#endif
               );
      // Only claim the optional SDKs this binary was actually built against —
      // an About page that lists what it cannot do is worse than no page.
      {
        std::vector<std::string> optional;
#if defined(DECKBOY_HAS_NDI_SDK)
        optional.push_back("NDI");
#endif
#if defined(DECKBOY_HAS_DECKLINK)
        optional.push_back("DeckLink");
#endif
#if defined(DECKBOY_HAS_SPOUT)
        optional.push_back("Spout");
#endif
#if defined(DECKBOY_HAS_SIPHON)
        optional.push_back("Syphon");
#endif
#if defined(DECKBOY_HAS_ASIO)
        optional.push_back("ASIO");
#endif
#if defined(DECKBOY_HAS_MIDI)
        optional.push_back("RtMidi");
#endif
#if defined(DECKBOY_HAS_WEBVIEW)
        optional.push_back("WebView2");
#endif
#if defined(DECKBOY_HAS_CEF)
        // Linked, but browser cues do not run on it yet — the off-screen
        // backend is phase 3 of docs/BROWSER_CEF_PLAN.md. Listing a bare "CEF"
        // here read as a working feature when nothing used it at all.
        optional.push_back("CEF (linked, not yet used)");
#endif
        std::string joined;
        for (std::size_t i = 0; i < optional.size(); ++i) {
          joined += (i ? "  " : "") + optional[i];
        }
        aboutRow(rightAbout, rightRowY, "Optional",
                 joined.empty() ? std::string("none compiled in") : joined);
      }
      aboutRow(rightAbout, rightRowY, "Timecode", "libltc (x42)");
      rightRowY += 6;
      aboutRow(rightAbout, rightRowY, "Thanks",
               "the FFmpeg, SDL and FreeType projects,");
      aboutRow(rightAbout, rightRowY, "", "Bitfocus Companion, and x42/libltc.");
      rightRowY += 6;
      aboutRow(rightAbout, rightRowY, "Fonts", "Press Start 2P (Cody Boisclair,");
      aboutRow(rightAbout, rightRowY, "", "SIL Open Font License 1.1)");
      rightRowY += 6;
      aboutRow(rightAbout, rightRowY, "Keys",
               "Enter Take  Space Play/Pause");
      aboutRow(rightAbout, rightRowY, "", "S Stop   C Clear   Esc Panic");
    } else if (settingsTab_ == 5) {
      // Encoder tab — the built-in media converter surface.
      SDL_Rect encRect {cx, cy, content.w - sPad * 3, content.h - sPad * 2};
      drawSettingsCard(encRect, "MEDIA ENCODER",
                       "Convert cues Deckboy can't play (or plays poorly) to H.264");
      // Body origin from the shared helpers, never a hardcoded header offset:
      // cy + 56 was authored at 1x, so at larger uiScale the first row sat
      // inside the card header. Same failure the display list had with its 32.
      const int ex = cardBodyX(encRect);
      int ey = cardBodyY(encRect);
      const int ew = cardBodyW(encRect);
      struct FlaggedCue { int deck; int cue; std::string label; std::string reason; std::string queueState; };
      std::vector<FlaggedCue> flagged;
      bool anyToConvert = false;
      for (int d = 0; d < static_cast<int>(project_.decks.size()); ++d) {
        const Deck& deck = project_.decks[d];
        for (int c = 0; c < static_cast<int>(deck.cues.size()); ++c) {
          const Cue& cue = deck.cues[c];
          if (auto r = cueConvertReason(cue)) {
            bool cv = isCueConverting(cue.path);
            flagged.push_back({d, c, cue.name, *r, cueQueueStateLabel(cue.path)});
            if (!cv) anyToConvert = true;
          }
        }
      }
      SDL_Rect convAllBtn {ex, ey, uiScaled(190), sTallH};
      drawUIPanel(convAllBtn, anyToConvert ? pal.dark : pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, convAllBtn,
                           "CONVERT ALL FLAGGED", anyToConvert ? pal.light : pal.inkSoft);
      if (anyToConvert) {
        settingsBtns_.push_back({convAllBtn, kSettingsActionEncoderConvertAll, "convert all flagged cues"});
      }
      SDL_Rect addBtn {ex + uiScaled(200), ey, uiScaled(120), sTallH};
      drawUIPanel(addBtn, pal.mid, pal.deep, pal.light);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, addBtn, "ADD FILE...", pal.deep);
      settingsBtns_.push_back({addBtn, kSettingsActionEncoderAddFile, "add file(s) to the show for conversion"});
      if (!conversionJobs_.empty()) {
        const char* pauseLabel = encoderQueuePaused_ ? "RESUME QUEUE" : "PAUSE QUEUE";
        int pw = 0, ph = 0;
        TTF_GetStringSize(fontSmall_, pauseLabel, 0, &pw, &ph);
        SDL_Rect pauseBtn {addBtn.x + addBtn.w + sGap, ey, pw + uiScaled(20), sTallH};
        drawUIPanel(pauseBtn, encoderQueuePaused_ ? pal.light : pal.mid, pal.deep, pal.light);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, pauseBtn, pauseLabel,
                             encoderQueuePaused_ ? pal.deep : pal.light);
        settingsBtns_.push_back({pauseBtn, kSettingsActionEncoderPauseToggle,
                                 "pause/resume the encode queue"});
        int cw = 0, ch = 0;
        TTF_GetStringSize(fontSmall_, "CANCEL ALL", 0, &cw, &ch);
        SDL_Rect cancelBtn {pauseBtn.x + pauseBtn.w + sGap, ey, cw + uiScaled(20), sTallH};
        drawUIPanel(cancelBtn, pal.mid, pal.deep, pal.light);
        drawCenteredTextSafe(controlRenderer_, fontSmall_, cancelBtn, "CANCEL ALL", pal.light);
        settingsBtns_.push_back({cancelBtn, kSettingsActionEncoderCancelAll,
                                 "cancel running job and clear the queue"});
      }
      ey += sTallH + sGap;
      {
        // Preset row. Chips size to their labels so a longer preset name never
        // ellipsizes into nonsense the way the queue whimsy line did.
        struct PresetChip { EncoderPreset preset; int action; };
        const PresetChip chips[] = {
          {EncoderPreset::DeliveryH264,     kSettingsActionEncoderPresetDelivery},
          {EncoderPreset::Proxy,            kSettingsActionEncoderPresetProxy},
          {EncoderPreset::MatchSource,      kSettingsActionEncoderPresetMatch},
          {EncoderPreset::DatamoshFriendly, kSettingsActionEncoderPresetDatamosh},
        };
        int px = ex;
        // Measure the label instead of guessing a width - uiScaled(56) clipped
        // it to "PRES..." at this font. Same trap as the >LIVE chip.
        int labelW = 0, labelH = 0;
        TTF_GetStringSize(fontSmall_, "PRESET", 0, &labelW, &labelH);
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect{px, ey + (sChipH - sLineH) / 2, labelW + uiScaled(10), sLineH},
                     "PRESET", soft);
        px += labelW + uiScaled(14);
        for (const PresetChip& chip : chips) {
          const char* label = encoderPresetLabel(chip.preset);
          int tw = 0, th = 0;
          TTF_GetStringSize(fontSmall_, label, 0, &tw, &th);
          SDL_Rect chipRect {px, ey, tw + uiScaled(18), sChipH};
          bool on = encoderPreset_ == chip.preset;
          drawUIPanel(chipRect, on ? pal.light : pal.mid, pal.deep, pal.light);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, chipRect, label,
                               on ? pal.deep : pal.light);
          settingsBtns_.push_back({chipRect, chip.action, "encode preset"});
          px += chipRect.w + sGap;
        }
        // Datamosh flavour. Both recipes mosh; they differ in how the smear
        // looks, so this is a look switch rather than another preset.
        {
          std::string lookLabel = std::string("MOSH: ") + moshLookLabel();
          int lw = 0, lh = 0;
          TTF_GetStringSize(fontSmall_, lookLabel.c_str(), 0, &lw, &lh);
          SDL_Rect lookRect {px, ey, lw + uiScaled(18), sChipH};
          bool moshActive = encoderPreset_ == EncoderPreset::DatamoshFriendly;
          drawUIPanel(lookRect, moshActive ? pal.mid : pal.dark, pal.deep, pal.light);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, lookRect, lookLabel,
                               moshActive ? pal.light : pal.inkSoft);
          settingsBtns_.push_back({lookRect, kSettingsActionEncoderMoshLook,
                                   "smooth (H.264) or chunky (MPEG-4 Part 2)"});
        }
        ey += sChipH + sGap;
      }
      {
        // The format matrix. Every catalog row gets a chip; rows whose encoder
        // this ffmpeg does not have are drawn dim and are not clickable, so an
        // unavailable format is visibly unavailable rather than a job that
        // fails later with nothing useful to say.
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{ex, ey, ew, sLineH},
                     "FORMAT", soft);
        ey += sLineH + sGap / 2;
        int fx = ex;
        for (const EncoderFormat& fmt : encoderFormatCatalog()) {
          const bool haveFmt = encoderFormatAvailable(fmt);
          // Mark unavailability in the TEXT, not only by dimming: across 30
          // colorways a colour-only signal is not reliably readable, and this
          // has to survive an OLED theme as well as a bright moulded one.
          const std::string chipLabel =
            haveFmt ? std::string(fmt.label) : std::string(fmt.label) + "  n/a";
          int tw = 0, th = 0;
          TTF_GetStringSize(fontSmall_, chipLabel.c_str(), 0, &tw, &th);
          int chipW = tw + uiScaled(16);
          if (fx + chipW > ex + ew) {          // wrap
            fx = ex;
            ey += sChipH + sGap / 2;
          }
          SDL_Rect r {fx, ey, chipW, sChipH};
          const bool on = encoderFormatId_ == fmt.id;
          drawUIPanel(r, on ? pal.light : (haveFmt ? pal.mid : pal.dark), pal.deep, pal.light);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, r, chipLabel,
                               on ? pal.deep : (haveFmt ? pal.light : pal.inkSoft));
          if (haveFmt) {
            settingsBtns_.push_back({r,
              kSettingsActionEncoderFormatBase + static_cast<int>(&fmt - encoderFormatCatalog().data()),
              fmt.note});
          }
          fx += chipW + sGap / 2;
        }
        ey += sChipH + sGap;
      }
      {
        // OUTPUT: the knobs that used to be hardcoded. Each chip shows its
        // CURRENT value, so "AUTO" states plainly that the format default is in
        // force rather than leaving the operator to guess.
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{ex, ey, ew, sLineH},
                     "OUTPUT", soft);
        ey += sLineH + sGap / 2;
        int ox = ex;
        auto chip = [&](const std::string& label, int action, const char* tip,
                        bool active) {
          int tw = 0, th = 0;
          TTF_GetStringSize(fontSmall_, label.c_str(), 0, &tw, &th);
          int cw = tw + uiScaled(16);
          if (ox + cw > ex + ew) { ox = ex; ey += sChipH + sGap / 2; }
          SDL_Rect r {ox, ey, cw, sChipH};
          drawUIPanel(r, active ? pal.light : pal.mid, pal.deep, pal.light);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, r, label,
                               active ? pal.deep : pal.light);
          settingsBtns_.push_back({r, action, tip});
          ox += cw + sGap / 2;
        };

        const EncoderOverrides& ov = encoderOverrides_;
        const bool rateOn = ov.rate != EncoderOverrides::Rate::Auto;
        chip(std::string("RATE: ") + encoderRateModeLabel(), kSettingsActionEncoderRateMode,
             "Auto uses the format's own default. Quality is constant-quality "
             "mapped onto whatever knob the codec has. Bitrate is constant -b:v.",
             rateOn);
        if (ov.rate == EncoderOverrides::Rate::Quality) {
          chip("Q -", kSettingsActionEncoderQualityDec, "Lower quality, smaller file", false);
          chip(std::to_string(ov.quality0to100), kSettingsActionEncoderQualityInc,
               "Quality 0-100. Mapped per codec; profile-only formats ignore it.", true);
          chip("Q +", kSettingsActionEncoderQualityInc, "Higher quality, bigger file", false);
        } else if (ov.rate == EncoderOverrides::Rate::Bitrate) {
          chip("BR -", kSettingsActionEncoderQualityDec, "Lower bitrate", false);
          chip(std::to_string(ov.videoBitrateKbps) + "k", kSettingsActionEncoderQualityInc,
               "Target video bitrate", true);
          chip("BR +", kSettingsActionEncoderQualityInc, "Higher bitrate", false);
        }
        chip(std::string("FPS: ") + encoderFpsLabel(), kSettingsActionEncoderFpsCycle,
             "Output frame rate. SOURCE keeps whatever the file has.", ov.fps > 0.0);
        chip(std::string("SIZE: ") + encoderSizeLabel(), kSettingsActionEncoderSizeCycle,
             "Output raster. SOURCE keeps the original. Merges with any filter "
             "the format already applies.", ov.width > 0);
        chip(std::string("AUDIO: ") + encoderAudioRateLabel(), kSettingsActionEncoderAudioRate,
             "Audio bitrate for the compressed audio codecs.", ov.audioBitrateKbps > 0);
        ey += sChipH + sGap / 2;
        ox = ex;
        chip("DEST...", kSettingsActionEncoderOutDirPick,
             "Choose where converted files are written.", !ov.outputDir.empty());
        if (!ov.outputDir.empty()) {
          chip("DEST: default", kSettingsActionEncoderOutDirClear,
               "Go back to _converted/ beside the show.", false);
        }
        ey += sChipH + sGap;
      }
      if (!conversionJobs_.empty()) {
        SDL_Rect busy {ex, ey, ew, std::max(uiScaled(164), sRowH * 4 + sLineH + sPad * 3)};
        drawEncoderBusyPanel(busy, animationNow_);
        ey += busy.h + sGap;
      }
      // Describe what is ACTUALLY selected. This line used to be hardcoded to
      // "H.264 MP4 -> _converted/ next to show", which stopped being true once
      // the format became selectable and the destination overridable.
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{ex, ey, ew, sLineH},
                   ellipsizeToPixelWidth(fontSmall_,
                     "Target: " + std::string(selectedEncoderFormat().label) +
                     "  ->  " + convertedMediaDir().string() +
                     "   |   active jobs: " +
                     std::to_string(static_cast<int>(conversionJobs_.size())), ew), soft);
      ey += sLineH + sGap;
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{ex, ey, ew, sLineH},
                   flagged.empty() ? "No cues need conversion." :
                     (std::to_string(static_cast<int>(flagged.size())) + " cue(s) flagged:"), pal.light);
      ey += sLineH + sGap;
      const int rowH = sRowH;
      int maxRows = std::max(0, (encRect.y + encRect.h - sPad - ey) / rowH);
      int shown = 0;
      for (const auto& f : flagged) {
        if (shown >= maxRows) break;
        std::string line = "D" + std::to_string(f.deck + 1) + " Q" + std::to_string(f.cue + 1)
                         + "  " + f.label + "   (" + f.reason + ")"
                         + f.queueState;
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{ex, ey, ew, sLineH},
                     ellipsizeToPixelWidth(fontSmall_, line, ew), f.queueState.empty() ? pal.light : soft);
        ey += rowH;
        ++shown;
      }
      if (static_cast<int>(flagged.size()) > shown) {
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect{ex, ey, ew, sLineH},
                     "  +" + std::to_string(static_cast<int>(flagged.size()) - shown) + " more", soft);
      }
    }
  }

  void handleSettingsClick(int mx, int my) {
    // Check close button
    if (pointInRect(mx, my, settingsCloseBtn_)) {
      settingsOpen_ = false;
      uiWatchdogPopupEvent("settings_modal", false);
      return;
    }
    if (!settingsOpen_) return;

    for (const auto& sb : settingsBtns_) {
      if (!pointInRect(mx, my, sb.rect)) continue;
      if (sb.action == kSettingsActionEncoderConvertAll) { convertAllFlaggedCues(); continue; }
      if (sb.action == kSettingsActionEncoderPresetDelivery) { setEncoderPreset(EncoderPreset::DeliveryH264); continue; }
      if (sb.action == kSettingsActionEncoderPresetProxy)    { setEncoderPreset(EncoderPreset::Proxy); continue; }
      if (sb.action == kSettingsActionEncoderPresetMatch)    { setEncoderPreset(EncoderPreset::MatchSource); continue; }
      if (sb.action == kSettingsActionEncoderPresetDatamosh) { setEncoderPreset(EncoderPreset::DatamoshFriendly); continue; }
      if (sb.action == kSettingsActionEncoderPauseToggle) { toggleEncoderQueuePaused(); continue; }
      if (sb.action == kSettingsActionEncoderCancelAll)   { cancelAllConversions(); continue; }
      if (sb.action == kSettingsActionEncoderMoshLook)    { toggleMoshLook(); continue; }
      if (sb.action == kSettingsActionRecordToggle)   { toggleRecording(); continue; }
      if (sb.action == kSettingsActionRecordDirPick)  { pickRecordingDir(); continue; }
      if (sb.action == kSettingsActionRecordDirClear) {
        project_.recordingDir.clear();
        markProjectDirty();
        triggerToast("record folder: recordings/ beside the show");
        continue;
      }
      if (sb.action == kSettingsActionEncoderRateMode)    { cycleEncoderRateMode(); continue; }
      if (sb.action == kSettingsActionEncoderQualityDec)  { nudgeEncoderRate(-1); continue; }
      if (sb.action == kSettingsActionEncoderQualityInc)  { nudgeEncoderRate(+1); continue; }
      if (sb.action == kSettingsActionEncoderFpsCycle)    { cycleEncoderFps(); continue; }
      if (sb.action == kSettingsActionEncoderSizeCycle)   { cycleEncoderSize(); continue; }
      if (sb.action == kSettingsActionEncoderAudioRate)   { cycleEncoderAudioRate(); continue; }
      if (sb.action == kSettingsActionEncoderOutDirPick)  { pickEncoderOutputDir(); continue; }
      if (sb.action == kSettingsActionEncoderOutDirClear) {
        encoderOverrides_.outputDir.clear();
        triggerToast("encoder destination: _converted beside the show");
        continue;
      }
      if (sb.action >= kSettingsActionEncoderFormatBase &&
          sb.action < kSettingsActionEncoderFormatBase +
                        static_cast<int>(encoderFormatCatalog().size())) {
        setEncoderFormat(
          encoderFormatCatalog()[sb.action - kSettingsActionEncoderFormatBase].id);
        continue;
      }
      if (sb.action >= kSettingsActionEncoderUpRowBase &&
          sb.action < kSettingsActionEncoderUpRowBase + 4) {
        moveConversionJob(static_cast<std::size_t>(sb.action - kSettingsActionEncoderUpRowBase), -1);
        continue;
      }
      if (sb.action >= kSettingsActionEncoderHoldRowBase &&
          sb.action < kSettingsActionEncoderHoldRowBase + 4) {
        toggleConversionHold(static_cast<std::size_t>(sb.action - kSettingsActionEncoderHoldRowBase));
        continue;
      }
      if (sb.action >= kSettingsActionEncoderCancelRowBase &&
          sb.action < kSettingsActionEncoderCancelRowBase + 4) {
        cancelConversionAt(static_cast<std::size_t>(sb.action - kSettingsActionEncoderCancelRowBase));
        continue;
      }
      if (sb.action == kSettingsActionEncoderAddFile) {
        importWithPicker();  // native async dialog, same as the IMPORT button
        continue;
      }
      if (sb.action >= 100 && sb.action <= 105) {
        // Tab switch
        settingsTab_ = sb.action - 100;
        settingsVideoSubTab_ = 0;
        settingsVideoScroll_ = 0;
        settingsVideoScrollMax_ = 0;
        settingsSystemScroll_ = 0;
        settingsSystemScrollMax_ = 0;
      } else if (sb.action == 200) {
        openDropdown(
          "settings.audio_device",
          sb.rect,
          audioOutputDeviceDropdownChoices(),
          focusedDeck().audioOutputDeviceName,
          [this](const std::string& value) {
            setAudioOutputDevice(value);
          });
        return;
      } else if (sb.action == 201) {
        project_.uiSoundsEnabled = !project_.uiSoundsEnabled;
        markProjectDirty();
      } else if (sb.action == 203) {
        toggleJumpMode();
      } else if (sb.action == 204) {
        setJumpTransitionEnabled(!project_.jumpTransitionEnabled);
      } else if (sb.action == 205) {
        cyclePanicProfile(-1);
      } else if (sb.action == 206) {
        cyclePanicProfile(1);
      } else if (sb.action == 207) {
        triggerPanicProfile();
      } else if (sb.action == 208) {
        adjustPanicFadeSeconds(-0.1);
      } else if (sb.action == 209) {
        adjustPanicFadeSeconds(0.1);
      } else if (sb.action == 212) {
        setPanicAutoRestoreEnabled(!project_.panicAutoRestore);
      } else if (sb.action == 213) {
        setTimecodeJamSyncEnabled(!focusedDeck().timecodeJamSyncEnabled);
      } else if (sb.action == 214) {
        setTimecodeFreewheelSeconds(focusedDeck().timecodeFreewheelSeconds - 0.1);
      } else if (sb.action == 215) {
        setTimecodeFreewheelSeconds(focusedDeck().timecodeFreewheelSeconds + 0.1);
      } else if (sb.action == 221) {
        settingsOpen_ = false;
        openInlineCueRenumberEditor(true);
      } else if (sb.action == kSettingsActionPlaylistPrefsEdit) {
        editFocusedDeckPlaylistPreferences();
      } else if (sb.action == kSettingsActionPlaylistDefaultLoopToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultLoop = !deck.playlistDefaultLoop;
        triggerToast(std::string("new cues loop: ") + (deck.playlistDefaultLoop ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultFadeInToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultFadeInEnabled = !deck.playlistDefaultFadeInEnabled;
        triggerToast(std::string("new cues fade in: ") + (deck.playlistDefaultFadeInEnabled ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultFadeOutToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultFadeOutEnabled = !deck.playlistDefaultFadeOutEnabled;
        triggerToast(std::string("new cues fade out: ") + (deck.playlistDefaultFadeOutEnabled ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultAudioToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultAudioEnabled = !deck.playlistDefaultAudioEnabled;
        triggerToast(std::string("new cues audio: ") + (deck.playlistDefaultAudioEnabled ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultPauseBeginToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultPauseAtBeginning = !deck.playlistDefaultPauseAtBeginning;
        triggerToast(std::string("new cues pause begin: ") + (deck.playlistDefaultPauseAtBeginning ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultPauseEndToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultPauseAtEnd = !deck.playlistDefaultPauseAtEnd;
        triggerToast(std::string("new cues pause end: ") + (deck.playlistDefaultPauseAtEnd ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultNextTransitionToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultTransitionToNext = !deck.playlistDefaultTransitionToNext;
        triggerToast(std::string("new cues next transition: ") + (deck.playlistDefaultTransitionToNext ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionAudioDelayDec ||
                 sb.action == kSettingsActionAudioDelayInc) {
        int step = sb.action == kSettingsActionAudioDelayInc ? 10 : -10;
        project_.audioDelayMs = std::clamp(project_.audioDelayMs + step, 0, 1000);
        // Applied live next tick (engines sync the atomic alongside master gain).
        triggerToast("A/V delay: " + std::to_string(project_.audioDelayMs) + " ms");
        markProjectDirty();
      } else if (sb.action == kSettingsActionAudioChannelsCycle) {
        // Cycle deck device channels: 2 → 4 → 6 → 8 → 2, reopening the
        // device so the new stream spec applies immediately.
        Deck& deck = focusedDeckMutable();
        int cur = deck.audioOutputChannels;
        deck.audioOutputChannels = (cur <= 2) ? 4 : (cur <= 4) ? 6 : (cur <= 6) ? 8 : 2;
        reopenDeckAudioOutput(project_.focusedDeckIndex, deck.audioOutputDeviceName);
        triggerToast("audio outs: " + std::to_string(deck.audioOutputChannels)
                     + " ch (" + std::to_string(deck.audioOutputChannels / 2) + " pairs)");
        markProjectDirty();
      } else if (sb.action == kSettingsActionAudioBufferCycle) {
        // Cycle audio buffer: 256 → 512 → 1024 → 2048 → 256
        int cur = project_.audioBufferSamples;
        int next = (cur <= 256) ? 512 : (cur <= 512) ? 1024 : (cur <= 1024) ? 2048 : 256;
        project_.audioBufferSamples = next;
        triggerToast("audio buffer: " + std::to_string(next) + " smp (restart to apply)");
        markProjectDirty();
      } else if (sb.action == 210) {
        // Toggle MIDI
        midiEnabled_ = !midiEnabled_;
        if (midiEnabled_) startMidiInput(); else stopMidiInput();
      } else if (sb.action == 211) {
        settingsOpen_ = false;
        openInlineMidiPortEditor();
      } else if (sb.action == 220) {
        settingsOpen_ = false;
        openInlineCompanionPortEditor();
      } else if (sb.action == kSettingsActionAllowRemoteToggle) {
        project_.allowRemoteNetwork = !project_.allowRemoteNetwork;
        // Restart all network listeners with new bind address
        stopCompanionControl();
        stopOscQueryServer();
        stopHyperDeckServer();
        if (project_.atemTriggerEnabled) stopAtemBridgeListener();
        if (project_.dmxArtNetEnabled) stopArtNetBridgeListener();
        if (project_.nmcSyncEnabled) stopNmcSyncBridge();
        startCompanionControl();
        if (project_.oscQueryEnabled) startOscQueryServer();
        startHyperDeckServer();
        if (project_.atemTriggerEnabled) startAtemBridgeListener();
        if (project_.dmxArtNetEnabled) startArtNetBridgeListener();
        if (project_.nmcSyncEnabled) startNmcSyncBridge();
        triggerToast(project_.allowRemoteNetwork ? "network: remote connections enabled" : "network: local only");
        markProjectDirty();
      } else if (sb.action == kSettingsActionOscQueryToggle) {
        setOscQueryEnabled(!project_.oscQueryEnabled);
      } else if (sb.action == kSettingsActionOscQueryPortPrompt) {
        settingsOpen_ = false;
        openInlineOscQueryPortEditor();
      } else if (sb.action == kSettingsActionOscFeedbackMirrorToggle) {
        setOscFeedbackMirrorEnabled(!project_.oscFeedbackMirrorEnabled);
      } else if (sb.action == kSettingsActionOscFeedbackRatePrompt) {
        settingsOpen_ = false;
        openInlineOscFeedbackRateEditor();
      } else if (sb.action == kSettingsActionIntegrationAtemToggle) {
        setIntegrationAdapterEnabled("ATEM", !project_.atemTriggerEnabled);
      } else if (sb.action == kSettingsActionIntegrationNdiTriggerToggle) {
        setIntegrationAdapterEnabled("NDI", !project_.ndiTriggerEnabled);
      } else if (sb.action == kSettingsActionIntegrationNmcToggle) {
        setIntegrationAdapterEnabled("NMC", !project_.nmcSyncEnabled);
      } else if (sb.action == kSettingsActionIntegrationMtcToggle) {
        setIntegrationAdapterEnabled("MTC", !project_.mtcIngestEnabled);
      } else if (sb.action == kSettingsActionIntegrationLtcToggle) {
        setIntegrationAdapterEnabled("LTC", !project_.ltcIngestEnabled);
      } else if (sb.action == kSettingsActionIntegrationArtNetToggle) {
        setIntegrationAdapterEnabled("ARTNET", !project_.dmxArtNetEnabled);
      } else if (sb.action == kSettingsActionIntegrationArtNetPortPrompt) {
        settingsOpen_ = false;
        openInlineArtNetPortEditor();
      } else if (sb.action == kSettingsActionIntegrationTslToggle) {
        project_.tslTallyEnabled = !project_.tslTallyEnabled;
        if (project_.tslTallyEnabled) {
          startTslTally();
          triggerToast("tally: enabled");
        } else {
          stopTslTally();
          triggerToast("tally: disabled");
        }
        markProjectDirty();
      } else if (sb.action == kSettingsActionIntegrationTslPortPrompt) {
        settingsOpen_ = false;
        openInlineTextEditor("tsl_port", "TSL Tally", "Tally port (default 5800)",
          std::to_string(project_.tslTallyPort),
          [this](const std::string& val) {
            try {
              int p = std::stoi(val);
              project_.tslTallyPort = std::clamp(p, 1, 65535);
            } catch (...) {}
            if (project_.tslTallyEnabled) {
              startTslTally(); // reopen socket on new port
            }
            markProjectDirty();
          });
      } else if (sb.action == kSettingsActionIntegrationTslAddrPrompt) {
        settingsOpen_ = false;
        openInlineTextEditor("tsl_address", "TSL Tally", "Target IP (or 255.255.255.255 to broadcast)",
          project_.tslTallyAddress,
          [this](const std::string& val) {
            project_.tslTallyAddress = val.empty() ? "255.255.255.255" : val;
            markProjectDirty();
          });
      } else if (sb.action == kSettingsActionIntegrationAllToggle) {
        bool allAdaptersEnabled = project_.atemTriggerEnabled && project_.ndiTriggerEnabled &&
                                  project_.nmcSyncEnabled && project_.mtcIngestEnabled &&
                                  project_.ltcIngestEnabled && project_.dmxArtNetEnabled;
        setAllIntegrationAdaptersEnabled(!allAdaptersEnabled);
      } else if (sb.action == kSettingsActionIntegrationTimecodeChaseToggle) {
        setTimecodeChaseEnabled(!focusedDeck().timecodeChaseEnabled);
      } else if (sb.action == kSettingsActionIntegrationTimecodeRunToggle) {
        setTimecodeRunEnabled(!focusedDeck().timecodeRunEnabled);
      } else if (sb.action == 230) {
        setOutputSizingModeDisplayNative();
      } else if (sb.action == 236) {
        toggleOutputFullscreen();
      } else if (sb.action == 237) {
        std::string initial = std::to_string(project_.outputRenderWidth) + "x" + std::to_string(project_.outputRenderHeight);
        openInlineTextEditor("settings.output_raster", "Custom Output Raster",
                             "WIDTHxHEIGHT (e.g. 2560x1080)", initial,
                             [this](const std::string& rawValue) {
                               std::string token = toUpper(trim(rawValue));
                               auto xPos = token.find('X');
                               if (xPos == std::string::npos || xPos == 0 || xPos + 1 >= token.size()) {
                                 triggerToast("raster: invalid");
                                 return;
                               }
                               try {
                                 int w = std::stoi(token.substr(0, xPos));
                                 int h = std::stoi(token.substr(xPos + 1));
                                 if (w > 0 && h > 0) {
                                   setOutputSizingModeFixed(w, h);
                                 } else {
                                   triggerToast("raster: invalid");
                                 }
                               } catch (...) {
                                 triggerToast("raster: invalid");
                               }
                             });
      } else if (sb.action == 238) {
        setOutputRefreshRate(0.0);
      } else if (sb.action == 239) {
        cycleOutputRefreshRate(-1);
      } else if (sb.action == 240) {
        cycleOutputRefreshRate(1);
      } else if (sb.action == 241) {
        openInlineTextEditor("settings.output_refresh", "Output Refresh",
                             "Hz or AUTO (e.g. 60 or 59.94)", outputRefreshRateLabel(),
                             [this](const std::string& rawValue) {
                               std::string token = toUpper(trim(rawValue));
                               if (token == "AUTO") {
                                 setOutputRefreshRate(0.0);
                                 return;
                               }
                               try {
                                 setOutputRefreshRate(std::stod(rawValue));
                               } catch (...) {
                                 triggerToast("refresh: invalid");
                               }
                             });
      } else if (sb.action == 242) {
        setOutputBitDepthMode(0);
      } else if (sb.action == 243) {
        setOutputBitDepthMode(8);
      } else if (sb.action == 244) {
        setOutputBitDepthMode(10);
      } else if (sb.action == 245) {
        setOutputCanvasMode(false);
      } else if (sb.action == 246) {
        setOutputCanvasMode(true, project_.outputCanvasWidth, project_.outputCanvasHeight);
      } else if (sb.action == 247) {
        std::string initial = std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight);
        openInlineTextEditor("settings.output_canvas", "Output Canvas",
                             "WIDTHxHEIGHT (e.g. 5760x2160)", initial,
                             [this](const std::string& rawValue) {
                               std::string token = toUpper(trim(rawValue));
                               auto xPos = token.find('X');
                               if (xPos == std::string::npos || xPos == 0 || xPos + 1 >= token.size()) {
                                 triggerToast("canvas: invalid");
                                 return;
                               }
                               try {
                                 int w = std::stoi(token.substr(0, xPos));
                                 int h = std::stoi(token.substr(xPos + 1));
                                 if (w > 0 && h > 0) {
                                   setOutputCanvasMode(true, w, h);
                                 } else {
                                   triggerToast("canvas: invalid");
                                 }
                               } catch (...) {
                                 triggerToast("canvas: invalid");
                               }
                             });
      } else if (sb.action == 248) {
        const Deck& deck = focusedDeck();
        std::string initial = std::to_string(deck.canvasViewX) + "," + std::to_string(deck.canvasViewY);
        openInlineTextEditor("settings.canvas_view", "Canvas View",
                             "X,Y offset in pixels", initial,
                             [this](const std::string& rawValue) {
                               std::string token = trim(rawValue);
                               size_t split = token.find(',');
                               if (split == std::string::npos) {
                                 split = token.find(' ');
                               }
                               if (split == std::string::npos || split == 0 || split + 1 >= token.size()) {
                                 triggerToast("view: invalid");
                                 return;
                               }
                               try {
                                 int x = std::stoi(token.substr(0, split));
                                 int y = std::stoi(token.substr(split + 1));
                                 setFocusedDeckCanvasView(x, y);
                               } catch (...) {
                                 triggerToast("view: invalid");
                               }
                             });
      } else if (sb.action == kSettingsActionOutputWarpModeCycle) {
        cycleFocusedDeckWarpMode(1);
      } else {
        handleSettingsClickPart2(sb);
      }
      return;
    }
    // Click outside modal = close
    SDL_Rect modal = settingsModalRect();
    if (!pointInRect(mx, my, modal)) settingsOpen_ = false;
  }

  // Second half of the settings-click handler, split off to keep the
  // if-else-if chain short enough for MSVC's block-nesting limit.
  void handleSettingsClickPart2(const SettingsButton& sb) {
    if (sb.action == 251) {
        cycleFocusedOutput(1);
      } else if (sb.action == kSettingsActionOutputRemove) {
        removeOutput(project_.focusedOutputIndex);
      } else if (sb.action == kSettingsActionStreamKeyPrompt) {
        settingsOpen_ = false;
        openInlineTextEditor("settings.stream_key", "Stream Key",
                             "paste your stream key here", focusedOutput().streamKey,
                             [this](const std::string& value) {
                               if (project_.focusedOutputIndex >= 0 &&
                                   project_.focusedOutputIndex < static_cast<int>(project_.outputs.size())) {
                                 project_.outputs[project_.focusedOutputIndex].streamKey = trim(value);
                                 markProjectDirty();
                               }
                             });
      } else if (sb.action == kSettingsActionOutputOverlayToggle) {
        toggleFocusedOutputTimeOverlayEnabled();
      } else if (sb.action == kSettingsActionOutputAlphaPrompt) {
        int initialPct = static_cast<int>(std::lround(std::clamp(focusedOutput().outputAlpha, 0.0f, 1.0f) * 100.0f));
        openInlineTextEditor("settings.output_alpha", "Output Alpha",
                             "Percent 0-100", std::to_string(initialPct),
                             [this](const std::string& value) {
                               try {
                                 setFocusedOutputAlpha(static_cast<float>(std::stod(trim(value)) / 100.0));
                               } catch (...) {
                                 triggerToast("alpha: invalid");
                               }
                             });
      } else if (sb.action == kSettingsActionOutputDelayPrompt) {
        openInlineTextEditor("settings.output_delay", "Output Delay",
                             "Milliseconds 0-5000", std::to_string(focusedOutput().outputDelayMs),
                             [this](const std::string& value) {
                               try {
                                 setFocusedOutputDelayMs(std::stoi(trim(value)));
                               } catch (...) {
                                 triggerToast("delay: invalid");
                               }
                             });
      } else if (sb.action == kSettingsActionOutputColorSpaceCycle) {
        cycleFocusedOutputColorSpace(1);
      } else if (sb.action == kSettingsActionOutputDelayInc) {
        setFocusedOutputDelayMs(focusedOutput().outputDelayMs + 100);
      } else if (sb.action == kSettingsActionOutputLayoutSpan) {
        setFocusedOutputLayoutMode("span");
      } else if (sb.action == kSettingsActionOutputLayoutDuplicate) {
        setFocusedOutputLayoutMode("duplicate");
      } else if (sb.action == kSettingsActionOutputOrientationCycle) {
        cycleFocusedOutputOrientation(1);
      } else if (sb.action == kSettingsActionOutputTestCardToggle) {
        toggleFocusedOutputTestCardEnabled();
      } else if (sb.action == kSettingsActionOutputTestCardAllToggle) {
        bool anyOff = false;
        for (const auto& out : project_.outputs) {
          if (!out.outputTestCardEnabled) {
            anyOff = true;
            break;
          }
        }
        setAllOutputsTestCardEnabled(anyOff);
      } else if (sb.action >= kSettingsActionOutputEdgeBlendLInc && sb.action <= kSettingsActionOutputEdgeBlendBDec) {
        Deck& bd = focusedDeckMutable();
        float step = 0.02f;
        switch (sb.action) {
          case kSettingsActionOutputEdgeBlendLInc: bd.edgeBlendLeft  = std::clamp(bd.edgeBlendLeft  + step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendLDec: bd.edgeBlendLeft  = std::clamp(bd.edgeBlendLeft  - step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendRInc: bd.edgeBlendRight = std::clamp(bd.edgeBlendRight + step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendRDec: bd.edgeBlendRight = std::clamp(bd.edgeBlendRight - step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendTInc: bd.edgeBlendTop   = std::clamp(bd.edgeBlendTop   + step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendTDec: bd.edgeBlendTop   = std::clamp(bd.edgeBlendTop   - step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendBInc: bd.edgeBlendBottom= std::clamp(bd.edgeBlendBottom+ step, 0.0f, 0.49f); break;
          case kSettingsActionOutputEdgeBlendBDec: bd.edgeBlendBottom= std::clamp(bd.edgeBlendBottom- step, 0.0f, 0.49f); break;
          default: break;
        }
        normalizeDeck(bd, project_.focusedDeckIndex);
        markProjectDirty();
      } else if (sb.action == kSettingsActionOutputAoiSizeDropdown) {
        AoiRectPx cur = focusedOutputAoiRectPx();
        bool isFull = cur.w >= cur.rasterW && cur.h >= cur.rasterH;
        openDropdown(
          "settings.aoi_size",
          sb.rect,
          aoiSizeChoices(),
          isFull ? std::string("full")
                 : std::to_string(cur.w) + "x" + std::to_string(cur.h),
          [this](const std::string& token) {
            applyFocusedOutputAoiSizeToken(token);
          });
      } else if (sb.action == kSettingsActionOutputAoiCentre) {
        centreFocusedOutputAoi();
      } else if (sb.action >= kSettingsActionOutputAoiXInc && sb.action <= kSettingsActionOutputAoiReset) {
        AoiRectPx aoi = focusedOutputAoiRectPx();
        // Nudge step: 16px snaps to common raster grids; Shift is not
        // routed here, so keep one predictable increment.
        constexpr int kAoiStepPx = 16;
        switch (sb.action) {
          case kSettingsActionOutputAoiXInc: applyFocusedOutputAoiRectPx(aoi.x + kAoiStepPx, aoi.y, aoi.w, aoi.h); break;
          case kSettingsActionOutputAoiXDec: applyFocusedOutputAoiRectPx(aoi.x - kAoiStepPx, aoi.y, aoi.w, aoi.h); break;
          case kSettingsActionOutputAoiYInc: applyFocusedOutputAoiRectPx(aoi.x, aoi.y + kAoiStepPx, aoi.w, aoi.h); break;
          case kSettingsActionOutputAoiYDec: applyFocusedOutputAoiRectPx(aoi.x, aoi.y - kAoiStepPx, aoi.w, aoi.h); break;
          case kSettingsActionOutputAoiWInc: applyFocusedOutputAoiRectPx(aoi.x, aoi.y, aoi.w + kAoiStepPx, aoi.h); break;
          case kSettingsActionOutputAoiWDec: applyFocusedOutputAoiRectPx(aoi.x, aoi.y, aoi.w - kAoiStepPx, aoi.h); break;
          case kSettingsActionOutputAoiHInc: applyFocusedOutputAoiRectPx(aoi.x, aoi.y, aoi.w, aoi.h + kAoiStepPx); break;
          case kSettingsActionOutputAoiHDec: applyFocusedOutputAoiRectPx(aoi.x, aoi.y, aoi.w, aoi.h - kAoiStepPx); break;
          case kSettingsActionOutputAoiReset: {
            OutputTarget& ot = focusedOutputMutable();
            ot.aoiLeft = ot.aoiRight = ot.aoiTop = ot.aoiBottom = 0.0f;
            markProjectDirty();
            triggerToast("aoi: full raster");
            break;
          }
          default: break;
        }
      } else if (sb.action == kSettingsActionOutputAoiXEdit) {
        AoiRectPx aoi = focusedOutputAoiRectPx();
        openAoiValueEditor("settings.aoi_x", "AOI X Position", aoi.x,
                           [this, aoi](int px) { applyFocusedOutputAoiRectPx(px, aoi.y, aoi.w, aoi.h); });
      } else if (sb.action == kSettingsActionOutputAoiYEdit) {
        AoiRectPx aoi = focusedOutputAoiRectPx();
        openAoiValueEditor("settings.aoi_y", "AOI Y Position", aoi.y,
                           [this, aoi](int px) { applyFocusedOutputAoiRectPx(aoi.x, px, aoi.w, aoi.h); });
      } else if (sb.action == kSettingsActionOutputAoiWEdit) {
        AoiRectPx aoi = focusedOutputAoiRectPx();
        openAoiValueEditor("settings.aoi_w", "AOI Width", aoi.w,
                           [this, aoi](int px) { applyFocusedOutputAoiRectPx(aoi.x, aoi.y, px, aoi.h); });
      } else if (sb.action == kSettingsActionOutputAoiHEdit) {
        AoiRectPx aoi = focusedOutputAoiRectPx();
        openAoiValueEditor("settings.aoi_h", "AOI Height", aoi.h,
                           [this, aoi](int px) { applyFocusedOutputAoiRectPx(aoi.x, aoi.y, aoi.w, px); });
      } else if (sb.action == kSettingsActionCanvasToggle) {
        project_.outputCanvasEnabled = !project_.outputCanvasEnabled;
        markProjectDirty();
        triggerToast(project_.outputCanvasEnabled ? "canvas on" : "canvas off");
      } else if (sb.action == kSettingsActionCanvasWidthPrompt) {
        settingsOpen_ = false;
        openInlineCanvasWidthEditor();
      } else if (sb.action == kSettingsActionCanvasHeightPrompt) {
        settingsOpen_ = false;
        openInlineCanvasHeightEditor();
      } else if (sb.action == kSettingsActionThemeDropdown) {
        // Scan for available themes
        std::vector<std::pair<std::string, std::string>> choices;
        fs::path themesDir = Paths::dataDir() / "themes";
        if (fs::is_directory(themesDir)) {
          for (auto& entry : fs::directory_iterator(themesDir)) {
            if (entry.is_directory() && fs::exists(entry.path() / "theme.txt")) {
              std::string name = entry.path().filename().string();
              choices.push_back({name, name});
            }
          }
        }
        std::sort(choices.begin(), choices.end());
        std::string current = currentThemeName_.empty() ? "gameboy" : currentThemeName_;
        openDropdown("settings.theme", sb.rect, choices, current,
          [this](const std::string& value) {
            loadTheme(value);
            project_.theme = value;
            markProjectDirty();
            triggerToast("theme: " + value);
          });
        return;
      } else if (sb.action == kSettingsActionTransitionSecondsDec) {
        setTransitionSeconds(std::max(0.0, focusedDeck().transitionSeconds - 0.1));
      } else if (sb.action == kSettingsActionTransitionSecondsInc) {
        setTransitionSeconds(focusedDeck().transitionSeconds + 0.1);
      } else if (sb.action == kSettingsActionTransitionStyleCycle) {
        const Deck& td = focusedDeck();
        TransitionStyle current = parseTransitionStyleToken(td.transitionStyle);
        TransitionStyle next = current == TransitionStyle::Cut ? TransitionStyle::Crossfade
                             : current == TransitionStyle::Crossfade ? TransitionStyle::DipBlack
                             : TransitionStyle::Cut;
        setTransitionStyle(next);
      } else if (sb.action == 260) {
        openDropdown(
          "settings.output_mirror",
          sb.rect,
          outputMirrorSourceDropdownChoices(),
          std::to_string(focusedOutput().mirrorSourceOutputIndex),
          [this](const std::string& value) {
            try {
              setFocusedOutputMirrorSource(std::stoi(trim(value)));
            } catch (...) {
              triggerToast("mirror: invalid");
            }
          });
        return;
      } else if (sb.action == 271) {
        // setFocusedOutputNdiEnabled gates on ndiRuntimeAvailable() and shows
        // the dependency prompt itself, so we can route through unconditionally.
        setFocusedOutputNdiEnabled(!focusedOutput().ndiEnabled);
      } else if (sb.action == 272) {
        const OutputTarget& output = focusedOutput();
        std::string initial = trim(output.ndiSourceName).empty()
          ? defaultOutputNdiSourceName(output, project_.focusedOutputIndex)
          : output.ndiSourceName;
        openInlineTextEditor("settings.ndi_name", "NDI Name",
                             "Sender source name", initial,
                             [this](const std::string& value) {
                               setFocusedOutputNdiName(value);
                             });
      } else if (sb.action == 273) {
        setFocusedOutputNdiKeyEnabled(!focusedOutput().ndiKeyEnabled);
      } else if (sb.action == 274) {
        const OutputTarget& output = focusedOutput();
        std::string initial = trim(output.ndiKeySourceName).empty()
          ? defaultOutputNdiKeySourceName(output, project_.focusedOutputIndex)
          : output.ndiKeySourceName;
        openInlineTextEditor("settings.ndi_key_name", "NDI Key Name",
                             "Key sender source name", initial,
                             [this](const std::string& value) {
                               setFocusedOutputNdiKeyName(value);
                             });
      } else {
        handleSettingsClickPart3(sb);
      }
  }

  // Third part of the settings-click handler, split off to keep the
  // if-else-if chain short enough for MSVC's block-nesting limit.
  void handleSettingsClickPart3(const SettingsButton& sb) {
    if (sb.action == kSettingsActionDisplayIdentify) {
        showDisplayIdentify();
        triggerToast("identifying displays");
        playUiSound(UiSoundEffect::Toggle);
      } else if (sb.action == kSettingsActionDeckLinkToggle) {
        // setFocusedOutputDeckLinkEnabled gates on deckLinkRuntimeAvailable()
        // and runs the toast / shutdown path internally.
        toggleFocusedOutputDeckLink();
      } else if (sb.action == kSettingsActionDeckLinkDeviceDropdown) {
        auto devices = deckboy::platform::video::DeckLinkOutput::listDevices();
        std::vector<std::pair<std::string, std::string>> choices;
        choices.push_back({"-1", "None"});
        for (const auto& d : devices) {
          std::string label = d.modelName;
          if (d.supportsSDI && d.supportsHDMI) label += " (SDI+HDMI)";
          else if (d.supportsSDI) label += " (SDI)";
          else if (d.supportsHDMI) label += " (HDMI)";
          if (d.supports4K) label += " 4K";
          choices.push_back({std::to_string(d.id), label});
        }
        openDropdown("settings.decklink_device", sb.rect, choices,
          std::to_string(focusedOutput().deckLinkDeviceId),
          [this](const std::string& value) {
            OutputTarget& output = focusedOutputMutable();
            int newId = -1;
            try { newId = std::stoi(trim(value)); } catch (...) {}
            if (output.deckLinkDeviceId != newId) {
              output.deckLinkDeviceId = newId;
              // Shutdown so it re-inits with the new device
              auto& rt = outputRuntimes_[project_.focusedOutputIndex];
              shutdownOutputDeckLink(rt);
              markProjectDirty();
            }
          });
        return;
      } else if (sb.action == kSettingsActionDeckLinkModeDropdown) {
        using DLM = deckboy::platform::video::DeckLinkMode;
        static constexpr DLM allModes[] = {
          DLM::HD720p50, DLM::HD720p5994, DLM::HD720p60,
          DLM::HD1080i50, DLM::HD1080i5994, DLM::HD1080i60,
          DLM::HD1080p2398, DLM::HD1080p24, DLM::HD1080p25,
          DLM::HD1080p2997, DLM::HD1080p30, DLM::HD1080p50,
          DLM::HD1080p5994, DLM::HD1080p60,
          DLM::UHD2160p2398, DLM::UHD2160p24, DLM::UHD2160p25,
          DLM::UHD2160p2997, DLM::UHD2160p30, DLM::UHD2160p50,
          DLM::UHD2160p5994, DLM::UHD2160p60,
        };
        std::vector<std::pair<std::string, std::string>> choices;
        for (auto m : allModes) {
          std::string token = deckboy::platform::video::deckLinkModeToken(m);
          std::string label = deckboy::platform::video::deckLinkModeLabel(m);
          choices.push_back({token, label});
        }
        openDropdown("settings.decklink_mode", sb.rect, choices,
          focusedOutput().deckLinkMode,
          [this](const std::string& value) {
            OutputTarget& output = focusedOutputMutable();
            if (output.deckLinkMode != value) {
              output.deckLinkMode = value;
              // Shutdown so it re-inits with the new mode
              auto& rt = outputRuntimes_[project_.focusedOutputIndex];
              shutdownOutputDeckLink(rt);
              markProjectDirty();
            }
          });
        return;
      } else if (sb.action == kSettingsActionDeckLink10BitToggle) {
        OutputTarget& output = focusedOutputMutable();
        output.deckLink10Bit = !output.deckLink10Bit;
        // Shutdown so it re-inits with the new bit depth
        auto& rt = outputRuntimes_[project_.focusedOutputIndex];
        shutdownOutputDeckLink(rt);
        triggerToast(std::string("decklink 10-bit: ") + (output.deckLink10Bit ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionSpoutToggle) {
        OutputTarget& output = focusedOutputMutable();
        output.spoutEnabled = !output.spoutEnabled;
        if (!output.spoutEnabled) {
          auto& rt = outputRuntimes_[project_.focusedOutputIndex];
          shutdownOutputSpout(rt);
        }
        triggerToast(std::string("spout: ") + (output.spoutEnabled ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionSpoutNamePrompt) {
        const OutputTarget& output = focusedOutput();
        std::string initial = trim(output.spoutSenderName);
        if (initial.empty()) {
          initial = "Deckboy Output " + std::to_string(project_.focusedOutputIndex + 1);
        }
        openInlineTextEditor("settings.spout_name", "Spout Sender Name",
                             "Name visible to receivers", initial,
                             [this](const std::string& value) {
                               OutputTarget& output = focusedOutputMutable();
                               output.spoutSenderName = trim(value);
                               // Shutdown so it re-inits with the new name
                               auto& rt = outputRuntimes_[project_.focusedOutputIndex];
                               shutdownOutputSpout(rt);
                               markProjectDirty();
                             });
      } else if (sb.action >= kSettingsActionStreamDestBase &&
                 sb.action < kSettingsActionStreamDestBase + 2 * kSettingsActionStreamDestStride) {
        const int rel = sb.action - kSettingsActionStreamDestBase;
        const int dest = rel / kSettingsActionStreamDestStride;
        const int field = rel % kSettingsActionStreamDestStride;
        const std::string protoId = (dest == 0) ? "srt" : "rtmp";
        // Every field edit materialises the destination's output, so the
        // operator can fill a form in before arming anything.
        const int outIdx = ensureStreamOutputForProtocol(protoId);
        if (outIdx < 0 || outIdx >= static_cast<int>(project_.outputs.size())) {
          triggerToast("stream: could not create destination");
        } else {
          OutputTarget& st = project_.outputs[outIdx];
          switch (field) {
            case kStreamFieldToggle: {
              const bool goingLive = !(st.enabled && st.streamEnabled);
              // A stream output is useless unless the output itself is armed —
              // arming both from one button is the whole point of this page.
              st.enabled = goingLive;
              st.streamEnabled = goingLive;
              if (!goingLive) {
                stopOutputStream(outIdx);
              }
              triggerToast(toUpper(protoId) + (goingLive ? " stream: on" : " stream: off"));
              playUiSound(UiSoundEffect::Toggle);
              break;
            }
            case kStreamFieldUrl:
              openInlineTextEditor("settings.stream_url_" + protoId,
                                   toUpper(protoId) + " destination",
                                   dest == 0 ? "srt://host:port" : "rtmp://host/app",
                                   trim(st.streamUrl).empty()
                                     ? defaultOutputStreamUrl(protoId, outIdx)
                                     : st.streamUrl,
                                   [this, outIdx](const std::string& v) {
                                     project_.outputs[outIdx].streamUrl = trim(v);
                                     stopOutputStream(outIdx);
                                     markProjectDirty();
                                   });
              break;
            case kStreamFieldKey:
              openInlineTextEditor("settings.stream_key_" + protoId, "Stream key",
                                   "Paste the key from your platform", trim(st.streamKey),
                                   [this, outIdx](const std::string& v) {
                                     project_.outputs[outIdx].streamKey = trim(v);
                                     stopOutputStream(outIdx);
                                     markProjectDirty();
                                   });
              break;
            case kStreamFieldBitrate:
              openInlineTextEditor("settings.stream_bitrate_" + protoId, "Video bitrate",
                                   "kbps (500-50000)", std::to_string(st.streamBitrateKbps),
                                   [this, outIdx](const std::string& v) {
                                     try {
                                       project_.outputs[outIdx].streamBitrateKbps =
                                         std::clamp(std::stoi(trim(v)), 500, 50000);
                                       stopOutputStream(outIdx);
                                       markProjectDirty();
                                     } catch (...) { triggerToast("bitrate: invalid"); }
                                   });
              break;
            case kStreamFieldAudioBitrate:
              openInlineTextEditor("settings.stream_abr_" + protoId, "Audio bitrate",
                                   "kbps (32-512; 160 is a sane default)",
                                   std::to_string(st.streamAudioBitrateKbps),
                                   [this, outIdx](const std::string& v) {
                                     try {
                                       project_.outputs[outIdx].streamAudioBitrateKbps =
                                         std::clamp(std::stoi(trim(v)), 32, 512);
                                       // Restart: the bitrate is baked into the
                                       // encoder command line at spawn.
                                       stopOutputStream(outIdx);
                                       markProjectDirty();
                                     } catch (...) { triggerToast("audio bitrate: invalid"); }
                                   });
              break;
            case kStreamFieldKeyframe:
              openInlineTextEditor("settings.stream_gop_" + protoId, "Keyframe interval",
                                   "seconds (1-10; most platforms want 2)",
                                   std::to_string(st.streamKeyframeSeconds),
                                   [this, outIdx](const std::string& v) {
                                     try {
                                       project_.outputs[outIdx].streamKeyframeSeconds =
                                         std::clamp(std::stoi(trim(v)), 1, 10);
                                       stopOutputStream(outIdx);
                                       markProjectDirty();
                                     } catch (...) { triggerToast("keyframe: invalid"); }
                                   });
              break;
            case kStreamFieldSrtLatency:
              openInlineTextEditor("settings.srt_latency", "SRT latency",
                                   "milliseconds (20-8000; ~4x your RTT)",
                                   std::to_string(st.srtLatencyMs),
                                   [this, outIdx](const std::string& v) {
                                     try {
                                       project_.outputs[outIdx].srtLatencyMs =
                                         std::clamp(std::stoi(trim(v)), 20, 8000);
                                       stopOutputStream(outIdx);
                                       markProjectDirty();
                                     } catch (...) { triggerToast("latency: invalid"); }
                                   });
              break;
            case kStreamFieldSrtPassphrase:
              openInlineTextEditor("settings.srt_passphrase", "SRT passphrase",
                                   "10-79 characters, or empty for none",
                                   trim(st.srtPassphrase),
                                   [this, outIdx](const std::string& v) {
                                     std::string p = trim(v);
                                     project_.outputs[outIdx].srtPassphrase = p;
                                     if (!p.empty() && p.size() < 10) {
                                       triggerToast("srt: passphrase needs 10+ characters");
                                     }
                                     stopOutputStream(outIdx);
                                     markProjectDirty();
                                   });
              break;
            case kStreamFieldSrtStreamId:
              openInlineTextEditor("settings.srt_streamid", "SRT stream ID",
                                   "Routing hint for the receiver", trim(st.srtStreamId),
                                   [this, outIdx](const std::string& v) {
                                     project_.outputs[outIdx].srtStreamId = trim(v);
                                     stopOutputStream(outIdx);
                                     markProjectDirty();
                                   });
              break;
            case kStreamFieldSrtMode:
              st.srtMode = (st.srtMode == "listener") ? "caller" : "listener";
              stopOutputStream(outIdx);
              triggerToast("srt mode: " + st.srtMode);
              break;
            default:
              break;
          }
          markProjectDirty();
        }
      } else if (sb.action == kSettingsActionLtcOutToggle) {
        project_.ltcOutputEnabled = !project_.ltcOutputEnabled;
        if (!project_.ltcOutputEnabled) {
          stopLtcOutput();
          triggerToast("ltc out: off");
        }
        markProjectDirty();
      } else if (sb.action == kSettingsActionLtcOutFps) {
        // The four rates a rig actually runs at.
        static const std::vector<std::pair<std::string, std::string>> kRates {
          {"24", "24 fps (film)"}, {"25", "25 fps (PAL/EBU)"},
          {"29.97", "29.97 fps (NTSC)"}, {"30", "30 fps"}
        };
        openDropdown("settings.ltc_fps", sb.rect, kRates,
                     fmtFloat(project_.ltcOutputFps, 2),
                     [this](const std::string& value) {
                       try {
                         project_.ltcOutputFps = std::clamp(std::stod(value), 23.0, 60.0);
                         stopLtcOutput();  // re-open at the new rate
                         markProjectDirty();
                       } catch (...) { triggerToast("ltc fps: invalid"); }
                     });
      } else if (sb.action == kSettingsActionLtcOutDevice) {
        // Same device list the decks use, plus an explicit default entry.
        // outputAudioDeviceChoices() already yields "" first for the default.
        std::vector<std::pair<std::string, std::string>> choices;
        for (const auto& name : outputAudioDeviceChoices()) {
          choices.push_back({name, name.empty() ? std::string("(system default)") : name});
        }
        openDropdown("settings.ltc_device", sb.rect, choices,
                     project_.ltcOutputDeviceName,
                     [this](const std::string& value) {
                       project_.ltcOutputDeviceName = value;
                       stopLtcOutput();
                       markProjectDirty();
                     });
      } else if (sb.action == kSettingsActionLtcOutChannel) {
        std::vector<std::pair<std::string, std::string>> choices;
        const int count = std::clamp(project_.ltcOutputChannelCount, 1, 8);
        for (int i = 0; i < count; ++i) {
          choices.push_back({std::to_string(i),
                             "Channel " + std::to_string(i + 1) +
                             (i == 0 ? " (L)" : (i == 1 ? " (R)" : ""))});
        }
        openDropdown("settings.ltc_channel", sb.rect, choices,
                     std::to_string(project_.ltcOutputChannel),
                     [this](const std::string& value) {
                       try {
                         project_.ltcOutputChannel = std::clamp(std::stoi(value), 0, 7);
                         stopLtcOutput();
                         markProjectDirty();
                       } catch (...) {}
                     });
      } else if (sb.action == kSettingsActionLtcOutChannelCount) {
        std::vector<std::pair<std::string, std::string>> choices;
        for (int n : {1, 2, 4, 6, 8}) {
          choices.push_back({std::to_string(n), std::to_string(n) + " channels"});
        }
        openDropdown("settings.ltc_channel_count", sb.rect, choices,
                     std::to_string(project_.ltcOutputChannelCount),
                     [this](const std::string& value) {
                       try {
                         project_.ltcOutputChannelCount = std::clamp(std::stoi(value), 1, 8);
                         project_.ltcOutputChannel =
                           std::clamp(project_.ltcOutputChannel, 0,
                                      project_.ltcOutputChannelCount - 1);
                         stopLtcOutput();
                         markProjectDirty();
                       } catch (...) {}
                     });
      } else if (sb.action == kSettingsActionSt2110Toggle) {
        OutputTarget& output = focusedOutputMutable();
        output.st2110Enabled = !output.st2110Enabled;
        if (!output.st2110Enabled) {
          shutdownOutputSt2110(outputRuntimes_[project_.focusedOutputIndex]);
          triggerToast("st 2110: off");
        } else {
          // Say the caveat at the moment of arming, not only in the section
          // header — this is when it matters.
          triggerToast("st 2110: on (experimental, no PTP lock)", {155, 188, 15, 220},
                       {15, 56, 15, 255}, 2600);
        }
        markProjectDirty();
      } else if (sb.action == kSettingsActionSt2110AddressPrompt) {
        std::string initial = trim(focusedOutput().st2110Address);
        if (initial.empty()) initial = "239.20.10.1";
        openInlineTextEditor("settings.st2110_address", "ST 2110 Destination",
                             "Multicast group (e.g. 239.20.10.1)", initial,
                             [this](const std::string& value) {
                               OutputTarget& output = focusedOutputMutable();
                               output.st2110Address = trim(value);
                               shutdownOutputSt2110(outputRuntimes_[project_.focusedOutputIndex]);
                               markProjectDirty();
                             });
      } else if (sb.action == kSettingsActionSt2110PortPrompt) {
        openInlineTextEditor("settings.st2110_port", "ST 2110 Port",
                             "Destination UDP port", std::to_string(focusedOutput().st2110Port),
                             [this](const std::string& value) {
                               try {
                                 int port = std::clamp(std::stoi(trim(value)), 1, 65535);
                                 OutputTarget& output = focusedOutputMutable();
                                 output.st2110Port = port;
                                 shutdownOutputSt2110(outputRuntimes_[project_.focusedOutputIndex]);
                                 markProjectDirty();
                               } catch (...) {
                                 triggerToast("st 2110: invalid port");
                               }
                             });
      } else if (sb.action == kSettingsActionSt2110DepthToggle) {
        OutputTarget& output = focusedOutputMutable();
        output.st2110TenBit = !output.st2110TenBit;
        shutdownOutputSt2110(outputRuntimes_[project_.focusedOutputIndex]);
        triggerToast(output.st2110TenBit ? "st 2110: 4:2:2 10-bit" : "st 2110: 4:2:2 8-bit");
        markProjectDirty();
      } else if (sb.action == kSettingsActionSt2110CopySdp) {
        // Until NMOS IS-05 exists the SDP is the ONLY way a receiver learns
        // about this stream, so it has to be reachable from the UI. Clipboard
        // beats a file dialog for something an operator pastes into a device.
        std::string sdp = focusedOutputSt2110Sdp();
        if (SDL_SetClipboardText(sdp.c_str())) {
          triggerToast("st 2110 SDP copied to clipboard", {155, 188, 15, 220},
                       {15, 56, 15, 255}, 2600);
        } else {
          triggerToast("st 2110: clipboard unavailable");
        }
      } else if (sb.action == kSettingsActionNmosToggle) {
        project_.nmosEnabled = !project_.nmosEnabled;
        if (!project_.nmosEnabled) {
          shutdownNmosNode();
          triggerToast("nmos: off");
        } else if (!project_.allowRemoteNetwork && !trim(project_.nmosRegistryUrl).empty()) {
          // Say it here, not only in the status line: this is the moment the
          // operator expects the plant to see them.
          triggerToast("nmos: network is LOCAL ONLY - not registering", {155, 188, 15, 220},
                       {15, 56, 15, 255}, 3200);
        } else if (trim(project_.nmosRegistryUrl).empty()) {
          // Arming with no registry is legitimate but it is NOT discovery, and
          // an operator who thinks it is will not find the node in a plant.
          triggerToast("nmos: on - node API only, no registry set", {155, 188, 15, 220},
                       {15, 56, 15, 255}, 2800);
        } else {
          triggerToast("nmos: on", {155, 188, 15, 220}, {15, 56, 15, 255}, 2200);
        }
        markProjectDirty();
        syncNmosNode();   // apply now rather than waiting for the next tick
      } else if (sb.action == kSettingsActionNmosRegistryPrompt) {
        openInlineTextEditor("settings.nmos_registry", "NMOS Registry",
                             "Registry URL (e.g. http://192.168.1.50:8010) - blank for none",
                             trim(project_.nmosRegistryUrl),
                             [this](const std::string& value) {
                               const std::string url = trim(value);
                               // Reject what the client genuinely cannot talk
                               // to, at entry, instead of failing silently in a
                               // background thread the operator never sees.
                               if (!url.empty()) {
                                 std::string host, path;
                                 int port = 0;
                                 if (!deckboy::platform::video::nmosParseUrl(url, host, port, path)) {
                                   triggerToast("nmos: need http://host:port (https not supported)");
                                   return;
                                 }
                               }
                               project_.nmosRegistryUrl = url;
                               markProjectDirty();
                               syncNmosNode();
                             });
      } else if (sb.action == kSettingsActionNmosPortPrompt) {
        openInlineTextEditor("settings.nmos_port", "NMOS Node Port",
                             "Port the Node + Connection API serve on",
                             std::to_string(project_.nmosPort),
                             [this](const std::string& value) {
                               try {
                                 project_.nmosPort = std::clamp(std::stoi(trim(value)), 1, 65535);
                                 markProjectDirty();
                                 syncNmosNode();
                               } catch (...) {
                                 triggerToast("nmos: invalid port");
                               }
                             });
      } else if (sb.action == kSettingsActionNmosInterfacePrompt) {
        openInlineTextEditor("settings.nmos_interface", "NMOS Interface Name",
                             "NIC name reported in interface_bindings (e.g. eth0)",
                             trim(project_.nmosInterfaceName),
                             [this](const std::string& value) {
                               project_.nmosInterfaceName = trim(value);
                               markProjectDirty();
                               syncNmosNode();
                             });
      } else if (sb.action == kSettingsActionNmosShowUrl) {
        const std::string url = nmosNode_.nodeApiUrl();
        if (url.empty()) {
          triggerToast("nmos: node not running");
        } else if (SDL_SetClipboardText(url.c_str())) {
          triggerToast("nmos node URL copied: " + url, {155, 188, 15, 220},
                       {15, 56, 15, 255}, 2800);
        } else {
          triggerToast("nmos: clipboard unavailable");
        }
      } else if (sb.action == kSettingsActionVjModeToggle) {
        // setVjMode does the real work -- it rebuilds the deck layout -- so
        // this only flips the intent and lets that decide what has to happen.
        setVjMode(!project_.vjModeEnabled);
        triggerToast(project_.vjModeEnabled ? "vj mode on" : "vj mode off");
      } else if (sb.action == kSettingsActionCreaturesToggle) {
        // off -> when idle -> always -> off
        if (!project_.creaturesEnabled) {
          project_.creaturesEnabled = true;
          project_.creaturesWhileLive = false;
          triggerToast("creatures: when idle");
        } else if (!project_.creaturesWhileLive) {
          project_.creaturesWhileLive = true;
          triggerToast("creatures: always, even during a show");
        } else {
          project_.creaturesEnabled = false;
          project_.creaturesWhileLive = false;
          triggerToast("creatures off");
        }
        markProjectDirty();
      } else if (sb.action == kSettingsActionMascotToggle) {
        // Dropdown: pick the splash mascot. refreshSplashAsset re-resolves the
        // path from the chosen character and lazy-reloads the texture.
        std::vector<std::pair<std::string, std::string>> choices = {
          {"deckbot",  "Deckbot  (default)"},
          {"deckgirl", "Deckgirl"},
        };
        openDropdown("settings.mascot", sb.rect, choices, project_.splashCharacter,
          [this](const std::string& value) {
            project_.splashCharacter = value;
            refreshSplashAsset();
            triggerToast(std::string("mascot: ") + project_.splashCharacter);
            markProjectDirty();
          });
      } else if (sb.action == kSettingsActionUiScaleDropdown) {
        std::vector<std::pair<std::string, std::string>> choices = {
          {"1.00", "1.00x  (default 1080p)"},
          {"1.25", "1.25x  (large desktop)"},
          {"1.50", "1.50x  (4K desktop)"},
          {"2.00", "2.00x  (Pocket 3 / 4K small)"},
        };
        char current[16];
        snprintf(current, sizeof(current), "%.2f", project_.uiScale);
        openDropdown("settings.ui_scale", sb.rect, choices, current,
          [this](const std::string& value) {
            double v = 1.0;
            try { v = std::stod(value); } catch (...) {}
            project_.uiScale = std::clamp(v, 0.75, 3.0);
            applyUiScale();
            triggerToast("ui scale: " + value + "x");
            markProjectDirty();
          });
      } else if (sb.action == kSettingsActionPocket3Preset) {
        // Toggle the Pocket 3 preset on/off. Currently bundles:
        //   - uiScale flip between 1.0 and 2.0
        //   - interactionMode flip between "mouse" and "touch" (skips hover
        //     affordances that don't translate to a tap-only input model)
        // Any future tap-friendly layout tweaks should land in this block so
        // one click flips the whole ergonomic profile in lockstep.
        bool active = std::abs(project_.uiScale - 2.0) < 0.01 &&
                      project_.interactionMode == "touch";
        if (active) {
          project_.uiScale = 1.0;
          project_.interactionMode = "mouse";
        } else {
          project_.uiScale = 2.0;
          project_.interactionMode = "touch";
        }
        applyUiScale();
        triggerToast(active ? "pocket 3 preset: off" : "pocket 3 preset: on");
        markProjectDirty();
      } else if (sb.action == kSettingsActionOutputAdvancedToggle) {
        videoOutputsAdvanced_ = !videoOutputsAdvanced_;
      } else if (sb.action == kSettingsActionOutputToggle) {
        toggleFocusedOutputEnabled();
      } else if (sb.action == kSettingsActionOutputDisplayPrev) {
        cycleOutputDisplay(-1);
      } else if (sb.action == kSettingsActionOutputDisplayNext) {
        cycleOutputDisplay(1);
      } else if (sb.action == kSettingsActionOutputDisplayRescan) {
        // Manual RESCAN is the operator saying "put the outputs back where
        // they belong" — force the re-home even when the fingerprints match.
        refreshDisplayTopology(true, true);
      } else if (sb.action == kSettingsActionOutputDisplayDropdown) {
        openDropdown(
          "settings.output_display",
          sb.rect,
          outputDisplayDropdownChoices(),
          std::to_string(outputDisplayIndex(project_.focusedOutputIndex)),
          [this](const std::string& value) {
            try {
              int displayIndex = std::stoi(trim(value));
              if (displayIndex >= 0) {
                setOutputDisplayIndex(displayIndex);
              }
            } catch (...) {
            }
          });
        return;
      } else if (sb.action >= kSettingsActionOutputDisplaySelectBase &&
                 sb.action < kSettingsActionOutputDisplaySelectBase + 32) {
        int selectedDisplay = sb.action - kSettingsActionOutputDisplaySelectBase;
        setOutputDisplayIndex(selectedDisplay);
      } else if (sb.action == kSettingsActionAudioInputGainDec) {
        project_.audioInputGainDb = std::clamp(project_.audioInputGainDb - 1.0, -40.0, 40.0);
        markProjectDirty();
        return;
      } else if (sb.action == kSettingsActionAudioInputGainInc) {
        project_.audioInputGainDb = std::clamp(project_.audioInputGainDb + 1.0, -40.0, 40.0);
        markProjectDirty();
        return;
      } else if (sb.action == kSettingsActionAudioInputClipClear) {
        project_.audioInputClipLatch = false;
        triggerToast("clip cleared");
        return;
      } else if (sb.action == kSettingsActionAudioInputMono) {
        project_.audioInputMono = !project_.audioInputMono;
        markProjectDirty();
        triggerToast(project_.audioInputMono ? "input: mono (summed)" : "input: stereo");
        return;
      } else if (sb.action == kSettingsActionAudioInputToProgram) {
        project_.audioInputToProgram = !project_.audioInputToProgram;
        markProjectDirty();
        triggerToast(project_.audioInputToProgram
          ? "mic mixed into recording"
          : "mic not recorded");
        return;
      } else if (sb.action == kSettingsActionAudioInputDropdown) {
        openDropdown(
          "settings.audio_input",
          sb.rect,
          audioInputDeviceDropdownChoices(),
          project_.audioInputEnabled ? project_.audioInputDeviceName
                                     : std::string("__off__"),
          [this](const std::string& value) {
            if (value == "__off__") {
              project_.audioInputEnabled = false;
              stopAudioInput();
              triggerToast("audio input off");
            } else {
              project_.audioInputDeviceName = value;
              project_.audioInputEnabled = true;
              if (!startAudioInput()) project_.audioInputEnabled = false;
            }
            markProjectDirty();
          });
        return;
      } else if (sb.action == kSettingsActionAsioDropdown) {
        openDropdown(
          "settings.asio",
          sb.rect,
          asioDriverDropdownChoices(),
          project_.asioDriverName,
          [this](const std::string& value) {
            project_.asioDriverName = value;
            markProjectDirty();
            if (value.empty()) {
              disarmAsioOutput("system audio");
            } else if (!armAsioOutput(value, project_.asioChannels)) {
              // Failed to open: fall back rather than leaving the operator
              // pointed at a device that is not playing anything.
              project_.asioDriverName.clear();
            }
          });
        return;
      } else if (sb.action == kSettingsActionOutputStreamProtocolDropdown) {
        openDropdown(
          "settings.stream_proto",
          sb.rect,
          outputStreamProtocolDropdownChoices(),
          normalizeOutputStreamProtocol(focusedOutput().streamProtocol),
          [this](const std::string& value) {
            setFocusedOutputStreamProtocol(value);
          });
        return;
      } else if (sb.action == kSettingsActionOutputMirrorDropdown) {
        openDropdown(
          "settings.output_mirror",
          sb.rect,
          outputMirrorSourceDropdownChoices(),
          std::to_string(focusedOutput().mirrorSourceOutputIndex),
          [this](const std::string& value) {
            try {
              setFocusedOutputMirrorSource(std::stoi(trim(value)));
            } catch (...) {
              triggerToast("mirror: invalid");
            }
          });
        return;
      } else if (sb.action >= kSettingsActionOutputDisplayFocusBase &&
                 sb.action < kSettingsActionOutputDisplayFocusBase + 64) {
        int displayIndex = sb.action - kSettingsActionOutputDisplayFocusBase;
        setOutputDisplayIndex(displayIndex);
      } else if (sb.action >= kSettingsActionRoutingTableOutputPrevBase &&
                 sb.action < kSettingsActionRoutingTableOutputPrevBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableOutputPrevBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        int nextOutput = (routeOutput - 1 + outputCount) % outputCount;
        moveDeckToOutput(deckIndex, nextOutput);
        setFocusedOutputIndex(nextOutput);
      } else if (sb.action >= kSettingsActionRoutingTableOutputNextBase &&
                 sb.action < kSettingsActionRoutingTableOutputNextBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableOutputNextBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        int nextOutput = (routeOutput + 1) % outputCount;
        moveDeckToOutput(deckIndex, nextOutput);
        setFocusedOutputIndex(nextOutput);
      } else if (sb.action >= kSettingsActionRoutingTableLayerDecBase &&
                 sb.action < kSettingsActionRoutingTableLayerDecBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableLayerDecBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (!assignmentIndex) {
          assignDeckToOutput(deckIndex, routeOutput);
          assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        }
        if (!assignmentIndex) {
          return;
        }
        int currentLayer = 0; // Single-deck: always layer 0
        setDeckOutputAssignmentLayer(deckIndex, routeOutput, currentLayer - 1);
      } else if (sb.action >= kSettingsActionRoutingTableLayerIncBase &&
                 sb.action < kSettingsActionRoutingTableLayerIncBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableLayerIncBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (!assignmentIndex) {
          assignDeckToOutput(deckIndex, routeOutput);
          assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        }
        if (!assignmentIndex) {
          return;
        }
        int currentLayer = 0; // Single-deck: always layer 0
        setDeckOutputAssignmentLayer(deckIndex, routeOutput, currentLayer + 1);
      } else if (sb.action >= kSettingsActionRoutingTableAssignToggleBase &&
                 sb.action < kSettingsActionRoutingTableAssignToggleBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableAssignToggleBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (assignmentIndex) {
          unassignDeckFromOutput(deckIndex, routeOutput);
        } else {
          assignDeckToOutput(deckIndex, routeOutput);
        }
      } else if (sb.action == kSettingsActionRoutingLayerDec ||
                 sb.action == kSettingsActionRoutingLayerInc) {
        int deckIndex = std::clamp(project_.focusedDeckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
        int outputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, outputIndex);
        if (!assignmentIndex) {
          triggerToast("assign route first");
        } else {
          int currentLayer = 0; // Single-deck: always layer 0
          bool shiftHeld = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
          bool ctrlHeld = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
          int step = ctrlHeld ? 10 : 1;
          int delta = (sb.action == kSettingsActionRoutingLayerDec) ? -step : step;
          if (shiftHeld) {
            delta = -delta;
          }
          setDeckOutputAssignmentLayer(deckIndex, outputIndex, currentLayer + delta);
        }
      } else if (sb.action == kSettingsActionRoutingAssignToggle) {
        int deckIndex = std::clamp(project_.focusedDeckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
        int outputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, outputIndex);
        if (assignmentIndex) {
          unassignDeckFromOutput(deckIndex, outputIndex);
        } else {
          assignDeckToOutput(deckIndex, outputIndex);
        }
      } else if (sb.action == kSettingsActionRoutingModeToggle) {
        routingMoveMode_ = !routingMoveMode_;
        triggerToast(std::string("routing mode: ") + (routingMoveMode_ ? "Move" : "Add"));
        playUiSound(UiSoundEffect::Toggle);
      } else if (sb.action >= kSettingsActionRoutingDeckFocusBase &&
                 sb.action < kSettingsActionRoutingDeckFocusBase + static_cast<int>(project_.decks.size())) {
        int deckIndex = sb.action - kSettingsActionRoutingDeckFocusBase;
        setFocusedDeckIndex(deckIndex);
      } else if (sb.action >= kSettingsActionRoutingOutputFocusBase &&
                 sb.action < kSettingsActionRoutingOutputFocusBase + static_cast<int>(project_.outputs.size())) {
        int outputIndex = sb.action - kSettingsActionRoutingOutputFocusBase;
        setFocusedOutputIndex(outputIndex);
      } else if (sb.action >= kSettingsActionRoutingCellBase) {
        int packed = sb.action - kSettingsActionRoutingCellBase;
        int deckIndex = packed / kSettingsActionRoutingCellStride;
        int outputIndex = packed % kSettingsActionRoutingCellStride;
        if (deckIndex >= 0 && deckIndex < static_cast<int>(project_.decks.size()) &&
            outputIndex >= 0 && outputIndex < static_cast<int>(project_.outputs.size())) {
          setFocusedDeckIndex(deckIndex);
          setFocusedOutputIndex(outputIndex);
          auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, outputIndex);
          bool shiftHeld = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
          bool ctrlHeld = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
          if (routingMoveMode_) {
            if (assignmentIndex) {
              int currentLayer = 0; // Single-deck: always layer 0
              int step = ctrlHeld ? 10 : 1;
              int delta = shiftHeld ? -step : step;
              setDeckOutputAssignmentLayer(deckIndex, outputIndex, currentLayer + delta);
            } else {
              moveDeckToOutput(deckIndex, outputIndex);
            }
          } else {
            if (assignmentIndex) {
              if (shiftHeld || ctrlHeld) {
                int currentLayer = 0; // Single-deck: always layer 0
                int step = ctrlHeld ? 10 : 1;
                int delta = shiftHeld ? -step : step;
                setDeckOutputAssignmentLayer(deckIndex, outputIndex, currentLayer + delta);
              } else {
                unassignDeckFromOutput(deckIndex, outputIndex);
              }
            } else {
              assignDeckToOutput(deckIndex, outputIndex);
            }
          }
        }
      } else if (sb.action >= kSettingsActionVideoSubTabBase &&
                 sb.action < kSettingsActionVideoSubTabBase + 4) {
        settingsVideoSubTab_ = sb.action - kSettingsActionVideoSubTabBase;
      }
  }
