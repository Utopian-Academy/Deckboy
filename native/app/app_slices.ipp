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

  void presentOutputSlices(int outputIndex, OutputRuntime& rt, const OutputTarget& output, int windowW, int windowH) {
    (void) outputIndex;
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
      SDL_Texture* source = rt.compositorTexture;
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
          SDL_SetTextureBlendMode(rt.compositorTexture, SDL_BLENDMODE_NONE);
          SDL_RenderTexture(r, rt.compositorTexture, static_cast<const SDL_FRect*>(nullptr),
                            static_cast<const SDL_FRect*>(nullptr));
          SDL_SetTextureBlendMode(cut, sliceAlphaCutBlend());
          const SDL_FRect at {s.srcX * texW, s.srcY * texH, static_cast<float>(cw), static_cast<float>(ch)};
          SDL_RenderTexture(r, cut, nullptr, &at);
          SDL_SetRenderTarget(r, nullptr);
          SDL_SetTextureScaleMode(scratch, SDL_SCALEMODE_LINEAR);
          source = scratch;
        }
      }
      const SDL_FPoint uvTL {s.srcX, s.srcY};
      const SDL_FPoint uvTR {s.srcX + s.srcW, s.srcY};
      const SDL_FPoint uvBR {s.srcX + s.srcW, s.srcY + s.srcH};
      const SDL_FPoint uvBL {s.srcX, s.srcY + s.srcH};
      const float W = static_cast<float>(windowW), H = static_cast<float>(windowH);
      const SDL_FPoint p0 {(s.dstX + s.corners[0]) * W, (s.dstY + s.corners[1]) * H};
      const SDL_FPoint p1 {(s.dstX + s.dstW + s.corners[2]) * W, (s.dstY + s.corners[3]) * H};
      const SDL_FPoint p2 {(s.dstX + s.dstW + s.corners[4]) * W, (s.dstY + s.dstH + s.corners[5]) * H};
      const SDL_FPoint p3 {(s.dstX + s.corners[6]) * W, (s.dstY + s.dstH + s.corners[7]) * H};
      const EdgeBlendShape shape = sliceBlendShape(output, s, rt.hdr);
      const bool soft = deckboy::slices::soft(s);
      if (deckboy::slices::pinned(s) && s.perspective &&
          renderPerspectiveWarp(r, source, shape, uvTL, uvTR, uvBR, uvBL, p0, p1, p2, p3, soft)) {
        continue;
      }
      if (soft && renderFeatheredQuad(r, source, shape, uvTL, uvTR, uvBR, uvBL, p0, p1, p2, p3)) {
        continue;
      }
      const SDL_FColor opaque {1.0f, 1.0f, 1.0f, 1.0f};
      SDL_Vertex verts[4] {{p0, opaque, uvTL}, {p1, opaque, uvTR}, {p2, opaque, uvBR}, {p3, opaque, uvBL}};
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
    } else if (what == "ON" || what == "OFF") {
      next.enabled = what == "ON";
    } else if (what == "LINEAR" || what == "PERSPECTIVE") {
      next.perspective = what == "PERSPECTIVE";
    } else if (what == "NAME") {
      next.name.clear();
      for (std::size_t i = 3; i < parts.size(); ++i) next.name += (i > 3 ? " " : "") + parts[i];
    } else {
      failRemoteCommand("SLICE <n>: CROP, PLACE, CORNER, EDGE, MASK, ON, OFF, LINEAR, PERSPECTIVE or NAME");
      return;
    }
    pushUndoSnapshot();
    s = deckboy::slices::clampSlice(next);
    markProjectDirty();
    remoteCommandDetail_ = "slice " + std::to_string(static_cast<int>(n)) + " " + toLower(what);
  }
