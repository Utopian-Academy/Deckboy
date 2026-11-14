  // ── THE STAGE TAB: a projection simulator, and a blend planner ─────────────
  //
  // Ghost Arcade board CSuzNz1f, item 2, approved by James 2026-11-14, with
  // BlendCalc's job merged in at his ask: say how big the screen is and what
  // the projectors are, and it works out how many it takes, where they hang,
  // how many pixels the row makes, how bright the screen is -- and, being in
  // Deckboy, sets the outputs' areas and blends to match (VIDEO BLEND ROW).
  //
  // The picture: the programme across the row's whole canvas, drawn on a flat
  // screen in a room, each projector's picture outlined in its own colour, the
  // overlaps shaded, and each projector's cone of light drawn from its lens.
  // Drag to turn round it, the wheel (or + / -) to come closer. First stage: a
  // flat screen and a row of projectors on the lens axis; a venue model comes
  // after James has looked at this one.

  struct StageFit {
    int count = 1;
    double imageW = 0, imageH = 0;   // one projector's picture on the screen, metres
    double rowW = 0;                 // the whole row's picture, metres
    double distance = 0;             // lens to screen, metres
    int canvasW = 0, canvasH = 0;    // the row's pixel space
    int overlapPx = 0;
    double lux = 0, nits = 0;        // on the screen
    double spillW = 0, spillH = 0;   // the share of the canvas that falls off the screen
  };

  // The row that covers the screen: each picture as wide as the row needs,
  // and if that is too short for the screen, as tall as the screen (and the
  // row wider than it). All of a projector's light lands on its own picture,
  // and the blend keeps the seams as bright as the rest, so the screen's lux
  // is one projector's lumens over one projector's picture.
  static StageFit stageFitFor(const StagePlan& p, int count) {
    StageFit f;
    f.count = std::max(1, count);
    const double o = std::clamp(p.overlap, 0.0, 0.45);
    const double perRow = f.count - (f.count - 1) * o;   // pictures' worth across the row
    const double aspect = static_cast<double>(std::max(1, p.projH)) / std::max(1, p.projW);
    f.imageW = p.screenW / perRow;
    f.imageH = f.imageW * aspect;
    if (f.imageH < p.screenH) {   // too short: as tall as the screen, wider than it
      f.imageH = p.screenH;
      f.imageW = f.imageH / aspect;
    }
    f.rowW = f.imageW * perRow;
    f.spillW = f.rowW > 0 ? std::max(0.0, (f.rowW - p.screenW) / f.rowW) : 0.0;
    f.spillH = f.imageH > 0 ? std::max(0.0, (f.imageH - p.screenH) / f.imageH) : 0.0;
    f.distance = f.imageW * p.throwRatio;
    f.overlapPx = static_cast<int>(std::lround(o * p.projW));
    f.canvasW = f.count * p.projW - (f.count - 1) * f.overlapPx;
    f.canvasH = p.projH;
    f.lux = f.imageW * f.imageH > 0 ? p.lumens / (f.imageW * f.imageH) : 0.0;
    f.nits = f.lux * p.gain / 3.14159265358979;
    return f;
  }

  // A length in the plan's units.
  std::string stageLength(double metres) const {
    char t[32];
    if (project_.stagePlan.feet) std::snprintf(t, sizeof(t), "%.1f ft", metres / 0.3048);
    else std::snprintf(t, sizeof(t), "%.2f m", metres);
    return t;
  }

  // The camera, turning round the middle of the screen.
  float stageYaw_ = 0.45f, stagePitch_ = 0.28f, stageDist_ = 0.0f;
  bool stageDragging_ = false;
  float stageDragX_ = 0, stageDragY_ = 0, stageYaw0_ = 0, stagePitch0_ = 0;
  SDL_FRect stageView_ {};
  enum StageField { kStScreenW, kStScreenH, kStLumens, kStThrow, kStOverlap, kStGain, kStCount, kStFieldCount };

  struct StageCamera {
    double ex, ey, ez;        // the eye
    double rx, ry, rz;        // right
    double ux, uy, uz;        // up
    double fx, fy, fz;        // forward
    double focal, cx, cy;
  };

  StageCamera stageCamera(const SDL_FRect& view, double targetY, double reach) {
    if (stageDist_ <= 0.0f) stageDist_ = static_cast<float>(reach * 1.7);
    StageCamera c {};
    const double cp = std::cos(stagePitch_), sp = std::sin(stagePitch_);
    const double cyw = std::cos(stageYaw_), syw = std::sin(stageYaw_);
    c.ex = stageDist_ * cp * syw;
    c.ey = targetY + stageDist_ * sp;
    c.ez = stageDist_ * cp * cyw;
    double fx = -c.ex, fy = targetY - c.ey, fz = -c.ez;
    const double fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    c.fx = fx / fl; c.fy = fy / fl; c.fz = fz / fl;
    // right = forward x up(0,1,0), then up = right x forward
    double rx = -c.fz, rz = c.fx;
    const double rl = std::max(1e-9, std::sqrt(rx * rx + rz * rz));
    c.rx = rx / rl; c.ry = 0; c.rz = rz / rl;
    c.ux = c.ry * c.fz - c.rz * c.fy;
    c.uy = c.rz * c.fx - c.rx * c.fz;
    c.uz = c.rx * c.fy - c.ry * c.fx;
    c.focal = (view.h * 0.5) / std::tan(0.45);   // about 52 degrees top to bottom
    c.cx = view.x + view.w * 0.5;
    c.cy = view.y + view.h * 0.5;
    return c;
  }

  // A point in the room (metres: x across, y up, z towards the audience) on
  // the screen, or false behind the eye.
  static bool stageProject(const StageCamera& c, double x, double y, double z, SDL_FPoint& out) {
    const double dx = x - c.ex, dy = y - c.ey, dz = z - c.ez;
    const double depth = dx * c.fx + dy * c.fy + dz * c.fz;
    if (depth < 0.05) return false;
    const double sx = dx * c.rx + dy * c.ry + dz * c.rz;
    const double sy = dx * c.ux + dy * c.uy + dz * c.uz;
    out.x = static_cast<float>(c.cx + sx / depth * c.focal);
    out.y = static_cast<float>(c.cy - sy / depth * c.focal);
    return true;
  }

  void stageLine(const StageCamera& c, double x0, double y0, double z0, double x1, double y1, double z1,
                 SDL_Color col) {
    SDL_FPoint a, b;
    if (!stageProject(c, x0, y0, z0, a) || !stageProject(c, x1, y1, z1, b)) return;
    SDL_SetRenderDrawColor(controlRenderer_, col.r, col.g, col.b, col.a);
    SDL_RenderLine(controlRenderer_, a.x, a.y, b.x, b.y);
  }

  // A rectangle on the screen's plane (z = 0) filled with part of a texture
  // (or a flat colour when tex is null), cut into small cells so the picture
  // keeps its perspective.
  void stageQuad(const StageCamera& c, double x0, double y0, double x1, double y1, SDL_Texture* tex, float u0,
                 float u1, float v0, float v1, SDL_FColor tint) {
    constexpr int kCols = 16, kRows = 8;
    std::vector<SDL_Vertex> verts;
    std::vector<int> idx;
    verts.reserve((kCols + 1) * (kRows + 1));
    bool ok = true;
    for (int r = 0; r <= kRows && ok; ++r) {
      for (int k = 0; k <= kCols; ++k) {
        const double fx = static_cast<double>(k) / kCols, fy = static_cast<double>(r) / kRows;
        SDL_FPoint p;
        if (!stageProject(c, x0 + (x1 - x0) * fx, y1 - (y1 - y0) * fy, 0.0, p)) {
          ok = false;
          break;
        }
        SDL_Vertex v {};
        v.position = p;
        v.color = tint;
        v.tex_coord = SDL_FPoint {static_cast<float>(u0 + (u1 - u0) * fx), static_cast<float>(v0 + (v1 - v0) * fy)};
        verts.push_back(v);
      }
    }
    if (!ok) return;
    for (int r = 0; r < kRows; ++r) {
      for (int k = 0; k < kCols; ++k) {
        const int a = r * (kCols + 1) + k, b = a + 1, d = a + kCols + 1, e = d + 1;
        idx.insert(idx.end(), {a, b, d, b, e, d});
      }
    }
    SDL_RenderGeometry(controlRenderer_, tex, verts.data(), static_cast<int>(verts.size()), idx.data(),
                       static_cast<int>(idx.size()));
  }

  void renderScreensStage(const SDL_Rect& area, int rowH) {
    StagePlan& p = project_.stagePlan;
    const StageFit fit = stageFitFor(p, p.count);
    const int pad = 10, sideW = 340;
    const SDL_Rect side {area.x + area.w - sideW, area.y, sideW, area.h};
    const SDL_Rect mid {area.x, area.y, side.x - pad - area.x, area.h};
    stageView_ = SDL_FRect {static_cast<float>(mid.x), static_cast<float>(mid.y), static_cast<float>(mid.w),
                            static_cast<float>(mid.h)};
    SDL_SetRenderDrawColor(controlRenderer_, 14, 18, 16, 255);
    SDL_RenderFillRect(controlRenderer_, &stageView_);
    SDL_Rect clip = mid;
    SDL_SetRenderClipRect(controlRenderer_, &clip);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);

    // The room: the screen's bottom edge a metre up, centred on x = 0.
    const double base = 1.0, W = p.screenW, H = p.screenH;
    const double rowH0 = base + H * 0.5 - fit.imageH * 0.5;   // the row's picture, top and bottom
    const double rowH1 = rowH0 + fit.imageH;
    const double reach = std::max({fit.distance, W, fit.rowW}) + 2.0;
    const StageCamera cam = stageCamera(stageView_, base + H * 0.5, reach);

    // The floor: a metre grid.
    const int half = static_cast<int>(std::ceil(std::max(W, fit.rowW) * 0.5)) + 3;
    const int deep = static_cast<int>(std::ceil(fit.distance)) + 3;
    for (int i = -half; i <= half; ++i) stageLine(cam, i, 0, -2, i, 0, deep, SDL_Color {40, 60, 46, 255});
    for (int k = -2; k <= deep; ++k) stageLine(cam, -half, 0, k, half, 0, k, SDL_Color {40, 60, 46, 255});

    // The light that falls off the screen, dim; the screen, full.
    const double rx0 = -fit.rowW * 0.5, rx1 = fit.rowW * 0.5;
    stageQuad(cam, rx0, rowH0, rx1, rowH1, screensPic_, 0, 1, 0, 1, SDL_FColor {0.35f, 0.35f, 0.35f, 1.0f});
    {
      const float u0 = static_cast<float>((-W * 0.5 - rx0) / fit.rowW), u1 = static_cast<float>((W * 0.5 - rx0) / fit.rowW);
      const float v0 = static_cast<float>((rowH1 - (base + H)) / fit.imageH), v1 = static_cast<float>((rowH1 - base) / fit.imageH);
      stageQuad(cam, -W * 0.5, base, W * 0.5, base + H, screensPic_, std::max(0.0f, u0), std::min(1.0f, u1),
                std::max(0.0f, v0), std::min(1.0f, v1), SDL_FColor {1, 1, 1, 1});
    }
    const SDL_Color screenEdge {230, 230, 230, 255};
    stageLine(cam, -W * 0.5, base, 0, W * 0.5, base, 0, screenEdge);
    stageLine(cam, -W * 0.5, base + H, 0, W * 0.5, base + H, 0, screenEdge);
    stageLine(cam, -W * 0.5, base, 0, -W * 0.5, base + H, 0, screenEdge);
    stageLine(cam, W * 0.5, base, 0, W * 0.5, base + H, 0, screenEdge);
    stageLine(cam, -W * 0.5, base, 0, -W * 0.5, 0, 0, SDL_Color {120, 120, 120, 255});
    stageLine(cam, W * 0.5, base, 0, W * 0.5, 0, 0, SDL_Color {120, 120, 120, 255});

    // Each projector: its picture outlined, its overlaps shaded, its cone.
    static const SDL_Color kColours[8] = {{255, 90, 160, 255}, {90, 200, 255, 255}, {255, 200, 60, 255},
                                          {120, 230, 120, 255}, {200, 140, 255, 255}, {255, 140, 90, 255},
                                          {90, 255, 210, 255}, {240, 240, 120, 255}};
    const double step = fit.imageW * (1.0 - std::clamp(p.overlap, 0.0, 0.45));
    for (int i = 0; i < fit.count; ++i) {
      const SDL_Color col = kColours[i % 8];
      const double x0 = rx0 + i * step, x1 = x0 + fit.imageW;
      if (i > 0) {   // the strip it shares with the one before
        stageQuad(cam, x0, rowH0, std::min(x1, rx0 + (i - 1) * step + fit.imageW), rowH1, nullptr, 0, 1, 0, 1,
                  SDL_FColor {1.0f, 1.0f, 1.0f, 0.18f});
      }
      stageLine(cam, x0, rowH0, 0.02, x1, rowH0, 0.02, col);
      stageLine(cam, x0, rowH1, 0.02, x1, rowH1, 0.02, col);
      stageLine(cam, x0, rowH0, 0.02, x0, rowH1, 0.02, col);
      stageLine(cam, x1, rowH0, 0.02, x1, rowH1, 0.02, col);
      const double lx = (x0 + x1) * 0.5, ly = (rowH0 + rowH1) * 0.5, lz = fit.distance;
      SDL_Color cone = col;
      cone.a = 150;
      stageLine(cam, lx, ly, lz, x0, rowH0, 0, cone);
      stageLine(cam, lx, ly, lz, x1, rowH0, 0, cone);
      stageLine(cam, lx, ly, lz, x0, rowH1, 0, cone);
      stageLine(cam, lx, ly, lz, x1, rowH1, 0, cone);
      // The projector: a small box at the lens, and its stand to the floor.
      const double bw = 0.25, bh = 0.12, bd = 0.35;
      for (int e = 0; e < 4; ++e) {
        const double ax = lx + ((e & 1) ? bw : -bw), ay = ly + ((e & 2) ? bh : -bh);
        stageLine(cam, ax, ay, lz, ax, ay, lz + bd, col);
      }
      stageLine(cam, lx - bw, ly - bh, lz, lx + bw, ly - bh, lz, col);
      stageLine(cam, lx - bw, ly + bh, lz, lx + bw, ly + bh, lz, col);
      stageLine(cam, lx - bw, ly - bh, lz, lx - bw, ly + bh, lz, col);
      stageLine(cam, lx + bw, ly - bh, lz, lx + bw, ly + bh, lz, col);
      stageLine(cam, lx, ly - bh, lz + bd * 0.5, lx, 0, lz + bd * 0.5, SDL_Color {120, 120, 120, 255});
      SDL_FPoint tag;
      if (stageProject(cam, lx, ly + bh + 0.15, lz, tag)) {
        drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {static_cast<int>(tag.x) - 40, static_cast<int>(tag.y) - rowH,
                                                             80, rowH},
                     "P" + std::to_string(i + 1), col);
      }
    }
    SDL_SetRenderClipRect(controlRenderer_, nullptr);
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {mid.x + 8, mid.y + mid.h - rowH - 4, mid.w - 16, rowH},
                 "drag to turn round it · wheel to come closer", pal.fgSoft);

    // ── The planner.
    drawUIPanel(side, pal.tile, pal.deep, pal.mid);
    int sy = side.y + 6;
    auto text = [&](const std::string& t, SDL_Color c) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, side.w - 16, rowH}, t, c);
      sy += rowH;
    };
    auto stepper = [&](const std::string& label, const std::string& value, int field) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, 120, rowH}, label, pal.fg);
      const int bx = side.x + 130, bw = side.w - 138;
      screensButton(SDL_Rect {bx, sy, rowH, rowH}, "-", kScrStageDec, field);
      drawUIValueControl(SDL_Rect {bx + rowH + 2, sy, bw - 2 * rowH - 4, rowH}, value);
      screensButton(SDL_Rect {bx + bw - rowH, sy, rowH, rowH}, "+", kScrStageInc, field);
      sy += rowH + 3;
    };
    // A label (a whole phrase, so it translates) and its value (numbers and
    // units, drawn as they are).
    auto pair = [&](const char* label, const std::string& value) {
      drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, 130, rowH}, label, pal.fgSoft);
      drawTextRaw(controlRenderer_, fontSmall_, value, pal.fg, side.x + 140, sy);
      sy += rowH;
    };
    auto button = [&](const std::string& label, int action, int param, bool on) {
      screensButton(SDL_Rect {side.x + 8, sy, side.w - 16, rowH}, label, action, param, on);
      sy += rowH + 3;
    };
    char t[96];
    text("SCREEN", pal.fg);
    stepper("width", stageLength(p.screenW), kStScreenW);
    stepper("height", stageLength(p.screenH), kStScreenH);
    std::snprintf(t, sizeof(t), "%.1f", p.gain);
    stepper("gain", t, kStGain);
    button(p.feet ? "UNITS: FEET" : "UNITS: METRES", kScrStageUnits, 0, p.feet);
    sy += 4;
    text("PROJECTORS", pal.fg);
    std::snprintf(t, sizeof(t), "%d x %d", p.projW, p.projH);
    drawTextSafe(controlRenderer_, fontSmall_, SDL_Rect {side.x + 8, sy, 120, rowH}, "pixels", pal.fg);
    screensButton(SDL_Rect {side.x + 130, sy, side.w - 138, rowH}, t, kScrStagePixels, 0, false);
    sy += rowH + 3;
    std::snprintf(t, sizeof(t), "%.0f lm", p.lumens);
    stepper("brightness", t, kStLumens);
    std::snprintf(t, sizeof(t), "%.2f : 1", p.throwRatio);
    stepper("throw ratio", t, kStThrow);
    std::snprintf(t, sizeof(t), "%.0f %%", p.overlap * 100.0);
    stepper("overlap", t, kStOverlap);
    stepper("how many", std::to_string(p.count), kStCount);
    sy += 4;
    text("THE ROW", pal.fg);
    std::snprintf(t, sizeof(t), "%d x %d px", fit.canvasW, fit.canvasH);
    pair("all the pixels", t);
    pair("each overlap", std::to_string(fit.overlapPx) + " px");
    pair("each picture", stageLength(fit.imageW) + " x " + stageLength(fit.imageH));
    pair("lens to screen", stageLength(fit.distance));
    std::snprintf(t, sizeof(t), "%.0f lux", fit.lux);
    pair("on the screen", t);
    if (p.feet) std::snprintf(t, sizeof(t), "%.0f nits, %.1f fL", fit.nits, fit.nits / 3.426);
    else std::snprintf(t, sizeof(t), "%.0f nits", fit.nits);
    pair("brightness", t);
    if (fit.spillW < 0.005 && fit.spillH < 0.005) {
      text("It covers the screen exactly.", pal.fg);
    } else if (fit.spillW >= 0.005) {
      std::snprintf(t, sizeof(t), "%.0f %%", fit.spillW * 100.0);
      pair("off the sides", t);
    } else {
      std::snprintf(t, sizeof(t), "%.0f %%", fit.spillH * 100.0);
      pair("off top and bottom", t);
    }
    sy += 4;
    text("OR", pal.fg);
    for (int n = std::max(1, p.count - 1); n <= std::min(8, p.count + 1); ++n) {
      if (n == p.count) continue;
      const StageFit alt = stageFitFor(p, n);
      std::snprintf(t, sizeof(t), "%d: %d px, %.0f lux, %s", n, alt.canvasW, alt.lux, stageLength(alt.distance).c_str());
      button(t, kScrStagePick, n, false);
    }
    sy += 4;
    const int have = static_cast<int>(project_.outputs.size());
    if (have >= p.count) {
      button("SET THE OUTPUTS' AREAS AND BLENDS", kScrStageApply, 0, false);
    } else {
      pair("outputs needed", std::to_string(p.count) + " (" + std::to_string(have) + ")");
      text("Add them in Settings > Video Outputs.", pal.fgSoft);
    }
    button("RESET VIEW", kScrStageView, 0, false);
  }

  void stageStep(int field, int dir) {
    StagePlan& p = project_.stagePlan;
    const double lengthStep = p.feet ? 0.3048 : 0.25;
    switch (field) {
      case kStScreenW: p.screenW = std::clamp(p.screenW + dir * lengthStep, 0.5, 100.0); break;
      case kStScreenH: p.screenH = std::clamp(p.screenH + dir * lengthStep, 0.3, 50.0); break;
      case kStLumens: p.lumens = std::clamp(p.lumens + dir * 500.0, 500.0, 60000.0); break;
      case kStThrow: p.throwRatio = std::clamp(p.throwRatio + dir * 0.05, 0.2, 10.0); break;
      case kStOverlap: p.overlap = std::clamp(p.overlap + dir * 0.01, 0.0, 0.45); break;
      case kStGain: p.gain = std::clamp(p.gain + dir * 0.1, 0.1, 5.0); break;
      case kStCount: p.count = std::clamp(p.count + dir, 1, 8); break;
      default: return;
    }
    markProjectDirty();
  }

  // The outputs' areas and blends for the row: VIDEO BLEND ROW, from output 1.
  void stageApply() {
    const StagePlan& p = project_.stagePlan;
    const StageFit fit = stageFitFor(p, p.count);
    handleRemoteCommand("VIDEO BLEND ROW 1 " + std::to_string(p.count) + " " + std::to_string(fit.overlapPx));
  }

  void handleScreensStageButton(const ScreensButton& b) {
    StagePlan& p = project_.stagePlan;
    switch (b.action) {
      case kScrStageDec: stageStep(b.param, -1); return;
      case kScrStageInc: stageStep(b.param, 1); return;
      case kScrStageUnits: p.feet = !p.feet; markProjectDirty(); return;
      case kScrStagePixels: {
        static const int kSizes[][2] = {{1920, 1080}, {1920, 1200}, {2560, 1600}, {3840, 2160}, {1280, 800}};
        int at = 0;
        for (int i = 0; i < 5; ++i) {
          if (kSizes[i][0] == p.projW && kSizes[i][1] == p.projH) at = i;
        }
        at = (at + 1) % 5;
        p.projW = kSizes[at][0];
        p.projH = kSizes[at][1];
        markProjectDirty();
        return;
      }
      case kScrStagePick: p.count = std::clamp(b.param, 1, 8); markProjectDirty(); return;
      case kScrStageApply: stageApply(); return;
      case kScrStageView:
        stageYaw_ = 0.45f;
        stagePitch_ = 0.28f;
        stageDist_ = 0.0f;
        return;
      default: return;
    }
  }

  void stageMouseDown(int mx, int my) {
    if (mx < stageView_.x || my < stageView_.y || mx >= stageView_.x + stageView_.w || my >= stageView_.y + stageView_.h) {
      return;
    }
    stageDragging_ = true;
    stageDragX_ = static_cast<float>(mx);
    stageDragY_ = static_cast<float>(my);
    stageYaw0_ = stageYaw_;
    stagePitch0_ = stagePitch_;
  }

  void stageMouseMotion(int mx, int my) {
    if (!stageDragging_) return;
    stageYaw_ = stageYaw0_ - (mx - stageDragX_) * 0.008f;
    stagePitch_ = std::clamp(stagePitch0_ + (my - stageDragY_) * 0.006f, -0.2f, 1.35f);
  }

  void stageWheel(int dy) {
    if (stageDist_ <= 0.0f) return;
    stageDist_ = std::clamp(stageDist_ * (dy > 0 ? 0.9f : 1.0f / 0.9f), 1.5f, 400.0f);
  }

  // SCREENS STAGE ... over the wire.
  void handleScreensStageCommand(const std::vector<std::string>& parts) {
    StagePlan& p = project_.stagePlan;
    const std::string what = parts.size() > 2 ? toUpper(parts[2]) : std::string("STATUS");
    if (what == "SET" && parts.size() >= 5) {
      const std::string key = toUpper(parts[3]);
      const double v = std::atof(parts[4].c_str());
      if (key == "WIDTH") p.screenW = std::clamp(v, 0.5, 100.0);
      else if (key == "HEIGHT") p.screenH = std::clamp(v, 0.3, 50.0);
      else if (key == "LUMENS") p.lumens = std::clamp(v, 500.0, 60000.0);
      else if (key == "THROW") p.throwRatio = std::clamp(v, 0.2, 10.0);
      else if (key == "OVERLAP") p.overlap = std::clamp(v / 100.0, 0.0, 0.45);
      else if (key == "GAIN") p.gain = std::clamp(v, 0.1, 5.0);
      else if (key == "COUNT") p.count = std::clamp(static_cast<int>(v), 1, 8);
      else if (key == "PIXELS" && parts.size() >= 6) {
        p.projW = std::clamp(std::atoi(parts[4].c_str()), 320, 8192);
        p.projH = std::clamp(std::atoi(parts[5].c_str()), 200, 8192);
      } else if (key == "UNITS") p.feet = toUpper(parts[4]) == "FEET";
      else {
        failRemoteCommand("SCREENS STAGE SET: WIDTH|HEIGHT <m>, LUMENS, THROW, OVERLAP <%>, GAIN, COUNT, PIXELS <w> <h> or UNITS METRES|FEET");
        return;
      }
      markProjectDirty();
    } else if (what == "APPLY") {
      stageApply();
      return;
    } else if (what != "STATUS") {
      failRemoteCommand("SCREENS STAGE: STATUS, SET <what> <value> or APPLY");
      return;
    }
    const StageFit f = stageFitFor(p, p.count);
    char t[320];
    std::snprintf(t, sizeof(t),
                  "count=%d canvas=%dx%d overlap_px=%d image=%.3fx%.3f distance=%.3f lux=%.1f nits=%.1f spill_w=%.4f "
                  "spill_h=%.4f",
                  f.count, f.canvasW, f.canvasH, f.overlapPx, f.imageW, f.imageH, f.distance, f.lux, f.nits, f.spillW,
                  f.spillH);
    remoteCommandDetail_ = t;
  }
