  // ── SLICES ──────────────────────────────────────────────────────────────
  //
  // Pieces of the programme placed on an output (core/slices.hpp). James,
  // 2026-11-04, Screens board 5Dsz7KuL: Resolume's Advanced Output, built from
  // what an output already does for its one picture -- the crop, the corner
  // pin, the soft edges -- done once per slice. Like the area of interest and
  // the warp, slices shape what the output's WINDOW shows: the recording, the
  // stream and NDI carry the programme as it is.

  // An edge-blend shape for one slice: the output's curve, gamma and black
  // lift, with the slice's own edges.
  static EdgeBlendShape sliceBlendShape(const OutputTarget& output, const deckboy::slices::Slice& s, bool hdr) {
    EdgeBlendShape shape = edgeBlendShapeFor(output, hdr);
    shape.edgeBlendLeft = s.edgeL;
    shape.edgeBlendRight = s.edgeR;
    shape.edgeBlendTop = s.edgeT;
    shape.edgeBlendBottom = s.edgeB;
    return shape;
  }

  // Keeps the colour and takes the mask as alpha: a slice cut this way is
  // still straight alpha, so the quad helpers' ordinary blend lays it on.
  static SDL_BlendMode sliceAlphaCutBlend() {
    static const SDL_BlendMode mode = SDL_ComposeCustomBlendMode(
      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    return mode;
  }

  // A target the size of the compositor, in its format, for a slice that
  // takes one playlist.
  SDL_Texture* ensureSliceScratch(OutputRuntime& rt) {
    const int w = rt.compositorWidth, h = rt.compositorHeight;
    if (w <= 0 || h <= 0 || !rt.outputRenderer) return nullptr;
    if (rt.sliceScratch && rt.sliceScratchW == w && rt.sliceScratchH == h) return rt.sliceScratch;
    if (rt.sliceScratch) SDL_DestroyTexture(rt.sliceScratch);
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, rt.compositorFormat);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    if (rt.hdr) {
      SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB_LINEAR);
      SDL_SetFloatProperty(props, SDL_PROP_TEXTURE_CREATE_SDR_WHITE_POINT_FLOAT, 1.0f);
      SDL_SetFloatProperty(props, SDL_PROP_TEXTURE_CREATE_HDR_HEADROOM_FLOAT, 1.0f);
    }
    rt.sliceScratch = SDL_CreateTextureWithProperties(rt.outputRenderer, props);
    SDL_DestroyProperties(props);
    rt.sliceScratchW = rt.sliceScratch ? w : 0;
    rt.sliceScratchH = rt.sliceScratch ? h : 0;
    return rt.sliceScratch;
  }

  // A point turned about a centre by `degrees`.
  static SDL_FPoint turnAbout(SDL_FPoint p, SDL_FPoint c, double degrees, double sx = 1.0, double sy = 1.0) {
    if (degrees == 0.0) return p;
    const double a = degrees * 3.14159265358979 / 180.0;
    const double dx = (p.x - c.x) * sx, dy = (p.y - c.y) * sy;
    return SDL_FPoint {static_cast<float>(c.x + (dx * std::cos(a) - dy * std::sin(a)) / sx),
                       static_cast<float>(c.y + (dx * std::sin(a) + dy * std::cos(a)) / sy)};
  }

  void presentOutputSlices(int outputIndex, OutputRuntime& rt, const OutputTarget& output, int windowW, int windowH) {
    const int texW = rt.compositorWidth, texH = rt.compositorHeight;
    SDL_Renderer* r = rt.outputRenderer;
    if (!r || !rt.compositorTexture || texW <= 0 || texH <= 0) return;
    // Smooth sampling for anything scaled or pinned, restored afterwards so
    // everything else that reads the compositor stays sharp.
    SDL_ScaleMode was = SDL_SCALEMODE_NEAREST;
    SDL_GetTextureScaleMode(rt.compositorTexture, &was);
    SDL_SetTextureScaleMode(rt.compositorTexture, SDL_SCALEMODE_LINEAR);
    for (const auto& s : output.slices) {
      if (!s.enabled) continue;
      // The oscillators: across and down in output widths and heights, the
      // turn in whole turns, the opacity on its own 0-1.
      const double moveX = geoLfoSwing(s.lfo[deckboy::slices::kSliceLfoX]);
      const double moveY = geoLfoSwing(s.lfo[deckboy::slices::kSliceLfoY]);
      const double turn = s.outRotation + geoLfoSwing(s.lfo[deckboy::slices::kSliceLfoRotation]) * 360.0;
      const float opacity = std::clamp(lfoApplyGeo(s.lfo[deckboy::slices::kSliceLfoOpacity], s.opacity), 0.0f, 1.0f);
      if (opacity <= 0.001f) continue;
      SDL_Texture* source = rt.compositorTexture;
      // ONE PLAYLIST as the source: drawn on its own into the scratch, where
      // the output would place it, and sliced from there.
      if (s.source.rfind("deck:", 0) == 0) {
        const int deck = std::atoi(s.source.c_str() + 5);
        SDL_Texture* scratch = ensureSliceScratch(rt);
        if (!scratch || deck < 0 || deck >= static_cast<int>(project_.decks.size())) continue;
        SDL_SetRenderTarget(r, scratch);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        if (!deckIsHeldOffOutput(deck)) renderDeckLayerIntoOutput(outputIndex, deck, SDL_Rect {0, 0, texW, texH});
        SDL_SetRenderTarget(r, nullptr);
        SDL_SetTextureScaleMode(scratch, SDL_SCALEMODE_LINEAR);
        source = scratch;
      }
      // A slice with a mask: the programme copied to the scratch, its alpha
      // cut by the mask laid over the slice's crop, so the cut belongs to the
      // slice alone and never touches its neighbours.
      if (!s.maskId.empty()) {
        const MaskModel* mask = findMask(s.maskId);
        SDL_Texture* scratch = mask ? ensureMaskScratch(rt) : nullptr;
        const int cw = std::max(1, static_cast<int>(std::lround(s.srcW * texW)));
        const int ch = std::max(1, static_cast<int>(std::lround(s.srcH * texH)));
        SDL_Texture* cut = scratch ? maskTextureFor(rt, *mask, cw, ch, MaskUse::Cut) : nullptr;
        if (scratch && cut) {
          SDL_SetRenderTarget(r, scratch);
          SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
          SDL_RenderClear(r);
          SDL_SetTextureBlendMode(source, SDL_BLENDMODE_NONE);
          SDL_RenderTexture(r, source, static_cast<const SDL_FRect*>(nullptr),
                            static_cast<const SDL_FRect*>(nullptr));
          const SDL_FRect at {s.srcX * texW, s.srcY * texH, static_cast<float>(cw), static_cast<float>(ch)};
          if (SDL_SetTextureBlendMode(cut, sliceAlphaCutBlend())) {
            SDL_RenderTexture(r, cut, nullptr, &at);
          } else {
            // No custom blends (SDL's software renderer): the processor cuts
            // the alpha under the crop, and leaves the colour, as the blend
            // would have.
            const SDL_Rect where {static_cast<int>(std::lround(at.x)), static_cast<int>(std::lround(at.y)), cw, ch};
            if (!cpuMaskCut(rt, *mask, cw, ch, MaskUse::Cut, 1.0, where, /*alphaToo=*/true, 1.0f,
                            SDL_Rect {0, 0, texW, texH}, /*colourToo=*/false)) {
              warnMaskCannotCut();
            }
          }
          SDL_SetRenderTarget(r, nullptr);
          SDL_SetTextureScaleMode(scratch, SDL_SCALEMODE_LINEAR);
          source = scratch;
        }
      }
      // The crop's corners, turned about its middle in the picture's own
      // pixels (so a turn stays square on a wide frame), then flipped.
      const SDL_FPoint inMid {s.srcX + s.srcW * 0.5f, s.srcY + s.srcH * 0.5f};
      SDL_FPoint uvTL = turnAbout({s.srcX, s.srcY}, inMid, s.inRotation, texW, texH);
      SDL_FPoint uvTR = turnAbout({s.srcX + s.srcW, s.srcY}, inMid, s.inRotation, texW, texH);
      SDL_FPoint uvBR = turnAbout({s.srcX + s.srcW, s.srcY + s.srcH}, inMid, s.inRotation, texW, texH);
      SDL_FPoint uvBL = turnAbout({s.srcX, s.srcY + s.srcH}, inMid, s.inRotation, texW, texH);
      if (s.flipH) {
        std::swap(uvTL, uvTR);
        std::swap(uvBL, uvBR);
      }
      if (s.flipV) {
        std::swap(uvTL, uvBL);
        std::swap(uvTR, uvBR);
      }
      const float W = static_cast<float>(windowW), H = static_cast<float>(windowH);
      const float x0 = static_cast<float>((s.dstX + moveX) * W), y0 = static_cast<float>((s.dstY + moveY) * H);
      const float x1 = x0 + s.dstW * W, y1 = y0 + s.dstH * H;
      const SDL_FPoint outMid {(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};
      // Turned about the placed picture's middle, then each corner's pin.
      SDL_FPoint p0 = turnAbout({x0, y0}, outMid, turn);
      SDL_FPoint p1 = turnAbout({x1, y0}, outMid, turn);
      SDL_FPoint p2 = turnAbout({x1, y1}, outMid, turn);
      SDL_FPoint p3 = turnAbout({x0, y1}, outMid, turn);
      p0.x += s.corners[0] * W; p0.y += s.corners[1] * H;
      p1.x += s.corners[2] * W; p1.y += s.corners[3] * H;
      p2.x += s.corners[4] * W; p2.y += s.corners[5] * H;
      p3.x += s.corners[6] * W; p3.y += s.corners[7] * H;
      const EdgeBlendShape shape = sliceBlendShape(output, s, rt.hdr);
      const bool soft = deckboy::slices::soft(s);
      // A mesh: the output's own grid renderer, handed a stand-in carrying
      // this slice's grid in pixels. One stand-in, reused frame to frame.
      if (deckboy::slices::gridActive(s)) {
        static thread_local OutputTarget gridHolder;
        gridHolder.warpGridCols = s.gridCols;
        gridHolder.warpGridRows = s.gridRows;
        gridHolder.warpGridSmooth = s.gridSmooth;
        gridHolder.warpGridOffsets.resize(s.gridOffsets.size());
        for (std::size_t k = 0; k < s.gridOffsets.size(); ++k) {
          gridHolder.warpGridOffsets[k] = s.gridOffsets[k] * (k % 2 == 0 ? W : H);
        }
        if (renderGridWarp(r, source, shape, gridHolder, s.perspective, uvTL, uvTR, uvBR, uvBL, p0, p1, p2, p3,
                           soft, opacity)) {
          continue;
        }
      }
      if ((deckboy::slices::pinned(s) || turn != 0.0) && s.perspective &&
          renderPerspectiveWarp(r, source, shape, uvTL, uvTR, uvBR, uvBL, p0, p1, p2, p3, soft, opacity)) {
        continue;
      }
      if (soft && renderFeatheredQuad(r, source, shape, uvTL, uvTR, uvBR, uvBL, p0, p1, p2, p3, opacity)) {
        continue;
      }
      const SDL_FColor tint {1.0f, 1.0f, 1.0f, opacity};
      SDL_Vertex verts[4] {{p0, tint, uvTL}, {p1, tint, uvTR}, {p2, tint, uvBR}, {p3, tint, uvBL}};
      const int indices[6] {0, 1, 2, 0, 2, 3};
      SDL_SetTextureBlendMode(source, SDL_BLENDMODE_BLEND);
      SDL_RenderGeometry(r, source, verts, 4, indices, 6);
    }
    SDL_SetTextureScaleMode(rt.compositorTexture, was);
  }

  // ── SLICE over the wire ─────────────────────────────────────────────────
  //
  // On the focused output (OUTPUT SELECT n). Positions and sizes in percent.
  //   SLICE                              this output's slices
  //   SLICE ADD [name]                   a new slice: the whole programme, the whole output
  //   SLICE <n> CROP x y w h             where in the programme
  //   SLICE <n> PLACE x y w h            where on the output
  //   SLICE <n> CORNER TL|TR|BR|BL dx dy  pin a corner off its place
  //   SLICE <n> EDGE L|R|T|B <%>         soft edge, as % of the slice
  //   SLICE <n> MASK <id|NONE>  SLICE <n> ON|OFF  SLICE <n> NAME <text>
  //   SLICE <n> LINEAR|PERSPECTIVE
  //   SLICE REMOVE <n>  SLICE CLEAR      (CLEAR: back to the output's one picture)
  void handleSliceCommand(const std::vector<std::string>& parts) {
    if (project_.outputs.empty()) {
      failRemoteCommand("SLICE: there is no output");
      return;
    }
    OutputTarget& out = focusedOutputMutable();
    auto& slices = out.slices;
    auto number = [&](std::size_t at, double lo, double hi, double& v) {
      if (parts.size() <= at) return false;
      try {
        std::size_t used = 0;
        v = std::stod(parts[at], &used);
        return used == parts[at].size() && std::isfinite(v) && v >= lo && v <= hi;
      } catch (...) {
        return false;
      }
    };
    const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string();
    if (sub.empty()) {
      std::ostringstream o;
      o.setf(std::ios::fixed);
      o.precision(1);
      for (std::size_t i = 0; i < slices.size(); ++i) {
        const auto& s = slices[i];
        o << (i ? "; " : "") << i + 1 << "=" << (s.name.empty() ? "slice" : s.name) << (s.enabled ? "" : "(off)")
          << " crop " << s.srcX * 100 << "," << s.srcY * 100 << "," << s.srcW * 100 << "," << s.srcH * 100
          << " place " << s.dstX * 100 << "," << s.dstY * 100 << "," << s.dstW * 100 << "," << s.dstH * 100;
        if (deckboy::slices::pinned(s)) o << " pinned";
        if (deckboy::slices::soft(s)) o << " soft";
        if (!s.maskId.empty()) o << " mask " << s.maskId;
        if (!s.source.empty()) o << " source " << s.source;
        if (s.inRotation != 0.0f || s.outRotation != 0.0f) o << " turned " << s.inRotation << "/" << s.outRotation;
        if (s.flipH || s.flipV) o << " flipped";
        if (deckboy::slices::gridActive(s)) o << " mesh " << s.gridCols << "x" << s.gridRows;
        if (s.opacity < 0.999f) o << " opacity " << s.opacity * 100;
      }
      remoteCommandDetail_ = slices.empty() ? std::string("no slices: the output shows its one picture") : o.str();
      return;
    }
    if (sub == "CLEAR") {
      pushUndoSnapshot();
      slices.clear();
      markProjectDirty();
      remoteCommandDetail_ = "no slices";
      return;
    }
    if (sub == "ADD") {
      if (static_cast<int>(slices.size()) >= deckboy::slices::kMaxSlices) {
        failRemoteCommand("SLICE ADD: already 16 slices on this output");
        return;
      }
      pushUndoSnapshot();
      deckboy::slices::Slice s;
      for (std::size_t i = 2; i < parts.size(); ++i) s.name += (i > 2 ? " " : "") + parts[i];
      if (s.name.empty()) s.name = "slice " + std::to_string(slices.size() + 1);
      slices.push_back(deckboy::slices::clampSlice(s));
      markProjectDirty();
      remoteCommandDetail_ = "slices: " + std::to_string(slices.size());
      return;
    }
    if (sub == "REMOVE" || sub == "DELETE") {
      double n = 0;
      if (!number(2, 1, static_cast<double>(slices.size()), n) || n != std::floor(n)) {
        failRemoteCommand("SLICE REMOVE: a slice number, 1-" + std::to_string(slices.size()));
        return;
      }
      pushUndoSnapshot();
      slices.erase(slices.begin() + static_cast<std::ptrdiff_t>(n) - 1);
      markProjectDirty();
      remoteCommandDetail_ = "slices: " + std::to_string(slices.size());
      return;
    }
    double n = 0;
    if (!number(1, 1, static_cast<double>(std::max<std::size_t>(1, slices.size())), n) || n != std::floor(n) ||
        slices.empty()) {
      failRemoteCommand("SLICE: ADD, REMOVE, CLEAR, or a slice number (this output has " +
                        std::to_string(slices.size()) + ")");
      return;
    }
    auto& s = slices[static_cast<std::size_t>(n) - 1];
    const std::string what = parts.size() > 2 ? toUpper(parts[2]) : std::string();
    deckboy::slices::Slice next = s;
    if (what == "CROP" || what == "PLACE") {
      double v[4];
      const double lo = what == "CROP" ? 0.0 : -100.0, hi = what == "CROP" ? 100.0 : 300.0;
      for (int i = 0; i < 4; ++i) {
        if (!number(3 + static_cast<std::size_t>(i), lo, hi, v[i])) {
          failRemoteCommand("SLICE " + what + ": x y w h, in percent");
          return;
        }
      }
      if (v[2] <= 0 || v[3] <= 0 || (what == "CROP" && (v[0] + v[2] > 100.001 || v[1] + v[3] > 100.001))) {
        failRemoteCommand("SLICE " + what + (what == "CROP" ? ": the crop must lie inside the programme"
                                                             : ": a place needs a size"));
        return;
      }
      if (what == "CROP") {
        next.srcX = static_cast<float>(v[0] / 100); next.srcY = static_cast<float>(v[1] / 100);
        next.srcW = static_cast<float>(v[2] / 100); next.srcH = static_cast<float>(v[3] / 100);
      } else {
        next.dstX = static_cast<float>(v[0] / 100); next.dstY = static_cast<float>(v[1] / 100);
        next.dstW = static_cast<float>(v[2] / 100); next.dstH = static_cast<float>(v[3] / 100);
      }
    } else if (what == "CORNER") {
      static const char* kCorners[] = {"TL", "TR", "BR", "BL"};
      const std::string which = parts.size() > 3 ? toUpper(parts[3]) : std::string();
      int c = -1;
      for (int i = 0; i < 4; ++i) {
        if (which == kCorners[i]) c = i;
      }
      double dx = 0, dy = 0;
      if (c < 0 || !number(4, -100, 100, dx) || !number(5, -100, 100, dy)) {
        failRemoteCommand("SLICE CORNER: TL, TR, BR or BL, then dx dy in percent of the output");
        return;
      }
      next.corners[static_cast<std::size_t>(c * 2)] = static_cast<float>(dx / 100);
      next.corners[static_cast<std::size_t>(c * 2 + 1)] = static_cast<float>(dy / 100);
    } else if (what == "EDGE") {
      const std::string side = parts.size() > 3 ? toUpper(parts[3]) : std::string();
      double v = 0;
      if ((side != "L" && side != "R" && side != "T" && side != "B") || !number(4, 0, 50, v)) {
        failRemoteCommand("SLICE EDGE: L, R, T or B, then 0-50 (% of the slice)");
        return;
      }
      float& e = side == "L" ? next.edgeL : side == "R" ? next.edgeR : side == "T" ? next.edgeT : next.edgeB;
      e = static_cast<float>(v / 100);
    } else if (what == "MASK") {
      const std::string id = parts.size() > 3 ? parts[3] : std::string();
      if (toUpper(id) == "NONE") {
        next.maskId.clear();
      } else if (!findMask(id)) {
        failRemoteCommand("SLICE MASK: no mask " + id + " (MASK LIST)");
        return;
      } else {
        next.maskId = id;
      }
    } else if (what == "SOURCE") {
      const std::string kind = parts.size() > 3 ? toUpper(parts[3]) : std::string();
      double d = 0;
      if (kind == "PROGRAMME" || kind == "PROGRAM") {
        next.source.clear();
      } else if ((kind == "DECK" || kind == "PLAYLIST") &&
                 number(4, 1, static_cast<double>(project_.decks.size()), d) && d == std::floor(d)) {
        next.source = "deck:" + std::to_string(static_cast<int>(d) - 1);
      } else {
        failRemoteCommand("SLICE SOURCE: PROGRAMME, or DECK 1-" + std::to_string(project_.decks.size()));
        return;
      }
    } else if (what == "ROTATE") {
      const std::string side = parts.size() > 3 ? toUpper(parts[3]) : std::string();
      double deg = 0;
      if ((side != "IN" && side != "OUT") || !number(4, -360, 360, deg)) {
        failRemoteCommand("SLICE ROTATE: IN (the crop) or OUT (the placed picture), then degrees");
        return;
      }
      (side == "IN" ? next.inRotation : next.outRotation) = static_cast<float>(deg);
    } else if (what == "FLIP") {
      const std::string axis = parts.size() > 3 ? toUpper(parts[3]) : std::string();
      const std::string state = parts.size() > 4 ? toUpper(parts[4]) : std::string();
      if ((axis != "H" && axis != "V") || (state != "ON" && state != "OFF")) {
        failRemoteCommand("SLICE FLIP: H or V, then ON or OFF");
        return;
      }
      (axis == "H" ? next.flipH : next.flipV) = state == "ON";
    } else if (what == "OPACITY") {
      double v = 0;
      if (!number(3, 0, 100, v)) {
        failRemoteCommand("SLICE OPACITY: 0-100");
        return;
      }
      next.opacity = static_cast<float>(v / 100);
    } else if (what == "LFO") {
      static const std::pair<const char*, int> kLfos[] = {
        {"X", deckboy::slices::kSliceLfoX}, {"Y", deckboy::slices::kSliceLfoY},
        {"ROTATION", deckboy::slices::kSliceLfoRotation}, {"OPACITY", deckboy::slices::kSliceLfoOpacity}};
      const std::string which = parts.size() > 3 ? toUpper(parts[3]) : std::string();
      int slot = -1;
      for (const auto& [name, index] : kLfos) {
        if (which == name) slot = index;
      }
      if (slot < 0) {
        failRemoteCommand("SLICE LFO: X, Y, ROTATION or OPACITY, then ON, OFF, SHAPE, RATE, DEPTH...");
        return;
      }
      if (!setLfoFromRemote(next.lfo[static_cast<std::size_t>(slot)], parts, 4, "SLICE LFO")) return;
    } else if (what == "GRID") {
      // GRID <cols> <rows> | GRID OFF | GRID SMOOTH ON|OFF
      // GRID POINT <col> <row> <dx> <dy>   (1-based; nudge in % of the output)
      const std::string op = parts.size() > 3 ? toUpper(parts[3]) : std::string();
      double a = 0, b = 0, dx = 0, dy = 0;
      if (op == "OFF") {
        next.gridCols = next.gridRows = 0;
        next.gridOffsets.clear();
      } else if (op == "SMOOTH") {
        const std::string state = parts.size() > 4 ? toUpper(parts[4]) : std::string();
        if (state != "ON" && state != "OFF") {
          failRemoteCommand("SLICE GRID SMOOTH: ON or OFF");
          return;
        }
        next.gridSmooth = state == "ON";
      } else if (op == "POINT") {
        if (!deckboy::slices::gridActive(next) || !number(4, 1, next.gridCols, a) || !number(5, 1, next.gridRows, b) ||
            a != std::floor(a) || b != std::floor(b) || !number(6, -100, 100, dx) || !number(7, -100, 100, dy)) {
          failRemoteCommand("SLICE GRID POINT: col row (from 1, on a grid that is on) then dx dy in percent");
          return;
        }
        const std::size_t i = static_cast<std::size_t>((static_cast<int>(b) - 1) * next.gridCols +
                                                       (static_cast<int>(a) - 1)) * 2;
        next.gridOffsets[i] = static_cast<float>(dx / 100);
        next.gridOffsets[i + 1] = static_cast<float>(dy / 100);
      } else if (number(3, 2, 9, a) && number(4, 2, 9, b) && a == std::floor(a) && b == std::floor(b)) {
        next.gridCols = static_cast<int>(a);
        next.gridRows = static_cast<int>(b);
        next.gridOffsets.assign(static_cast<std::size_t>(next.gridCols * next.gridRows * 2), 0.0f);
      } else {
        failRemoteCommand("SLICE GRID: <cols> <rows> (2-9), OFF, SMOOTH ON|OFF, or POINT col row dx dy");
        return;
      }
    } else if (what == "ON" || what == "OFF") {
      next.enabled = what == "ON";
    } else if (what == "LINEAR" || what == "PERSPECTIVE") {
      next.perspective = what == "PERSPECTIVE";
    } else if (what == "NAME") {
      next.name.clear();
      for (std::size_t i = 3; i < parts.size(); ++i) next.name += (i > 3 ? " " : "") + parts[i];
    } else {
      failRemoteCommand("SLICE <n>: CROP, PLACE, CORNER, EDGE, MASK, SOURCE, ROTATE, FLIP, OPACITY, LFO, ON, OFF, "
                        "LINEAR, PERSPECTIVE or NAME");
      return;
    }
    pushUndoSnapshot();
    s = deckboy::slices::clampSlice(next);
    markProjectDirty();
    remoteCommandDetail_ = "slice " + std::to_string(static_cast<int>(n)) + " " + toLower(what);
  }
