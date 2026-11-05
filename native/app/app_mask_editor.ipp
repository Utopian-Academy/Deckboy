// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// THE MASK EDITOR
// ============================================================================
//
// The board's two halves (https://claude.ai/artifact/X2N9qJe1ejpoVHPo2DqcDx):
// the mask's controls in the inspector (MASK, on every cue), and its points
// on the programme monitor -- the warp editor's place, from the MASK button
// beside WARP. One mask is in hand at a time: the one on the TARGET, which is
// the selected cue, the focused playlist on the focused output, that output's
// whole picture, or its matte.
//
// Pen: click for a corner, drag for a curve, click the first point to close.
// Points: drag a point or a handle (Alt breaks the pair), click the outline to
// add a point, Alt- or Shift-click a point (or Delete) to take it away.

  enum class MaskTarget { Cue = 0, Layer, Output, Matte, Count };
  enum class MaskTool { Points = 0, Pen };
  enum class MaskDrag { None, Anchor, In, Out, NewHandle };

  struct MaskEditorState {
    bool open = false;
    MaskTarget target = MaskTarget::Cue;
    MaskTool tool = MaskTool::Points;
    bool subtract = false;   // what the pen and the shapes make next
    int path = -1;
    int point = -1;
    MaskDrag drag = MaskDrag::None;
    bool drawing = false;    // the pen's path is the mask's last, still open
    bool undoTaken = false;  // one undo step per gesture, not per motion
  };
  MaskEditorState maskEditor_;
  SDL_Rect maskEditBtnRect_ {};
  SDL_Rect maskPickRect_ {};
  SDL_Rect maskShapeRect_ {};
  bool cueSectionMaskOpen_ = true;

  // ── WHAT IS BEING MASKED ──────────────────────────────────────────────────

  int maskTargetOutput() const {
    if (project_.outputs.empty()) return -1;
    return std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
  }

  // The id slot the target holds, or null where it has none (no cue selected;
  // a playlist that is not on the focused output). The matte is held as
  // "mask:<id>" and goes through maskTargetId / setMaskTargetId instead.
  std::string* maskTargetSlot() {
    const int out = maskTargetOutput();
    switch (maskEditor_.target) {
      case MaskTarget::Cue: {
        Cue* cue = selectedCueMutable();
        return cue ? &cue->maskId : nullptr;
      }
      case MaskTarget::Layer: {
        if (out < 0) return nullptr;
        OutputTarget& o = project_.outputs[static_cast<std::size_t>(out)];
        for (OutputLayer& layer : o.layerDecks) {
          if (layer.deckIndex == project_.focusedDeckIndex) return &layer.maskId;
        }
        return stackPositionFor(out, project_.focusedDeckIndex) == 0 ? &o.baseLayerMaskId : nullptr;
      }
      case MaskTarget::Output:
        return out < 0 ? nullptr : &project_.outputs[static_cast<std::size_t>(out)].maskId;
      default:
        return nullptr;
    }
  }

  std::string maskTargetId() {
    if (maskEditor_.target == MaskTarget::Matte) {
      const int out = maskTargetOutput();
      if (out < 0) return {};
      const std::string& aspect = project_.outputs[static_cast<std::size_t>(out)].matteAspect;
      return aspect.rfind("mask:", 0) == 0 ? aspect.substr(5) : std::string();
    }
    const std::string* slot = maskTargetSlot();
    return slot ? *slot : std::string();
  }

  bool maskTargetAvailable() {
    return maskEditor_.target == MaskTarget::Matte ? maskTargetOutput() >= 0 : maskTargetSlot() != nullptr;
  }

  void setMaskTargetId(const std::string& id) {
    if (maskEditor_.target == MaskTarget::Matte) {
      const int out = maskTargetOutput();
      if (out >= 0) project_.outputs[static_cast<std::size_t>(out)].matteAspect = id.empty() ? "off" : "mask:" + id;
    } else if (maskEditor_.target == MaskTarget::Cue) {
      // Every selected cue, as every other cue row does.
      forEachFocusedSelectedCueMutable([&](Cue& cue, int) { cue.maskId = id; });
    } else if (std::string* slot = maskTargetSlot()) {
      *slot = id;
    }
    markProjectDirty();
  }

  MaskModel* maskFocus() { return findMask(maskTargetId()); }

  std::string maskTargetLabel() {
    const int out = maskTargetOutput();
    const std::string outName = out >= 0 ? outputLabel(out) : std::string("no output");
    switch (maskEditor_.target) {
      case MaskTarget::Cue:    return "this cue";
      case MaskTarget::Layer:  return deckLabel(project_.focusedDeckIndex) + " on " + outName;
      case MaskTarget::Output: return "all of " + outName;
      default:                 return outName + " matte";
    }
  }

  void cycleMaskTarget() {
    if (maskEditor_.drawing) finishMaskPenPath();
    maskEditor_.target = static_cast<MaskTarget>((static_cast<int>(maskEditor_.target) + 1) %
                                                 static_cast<int>(MaskTarget::Count));
    maskEditor_.path = maskEditor_.point = -1;
    triggerToast("mask on: " + maskTargetLabel());
  }

  // The width over the height of what the target's mask lies on, so a new
  // circle is round there.
  double maskTargetAspect() {
    const SDL_FRect r = maskEditArea(SDL_Rect {0, 0, 1600, 900});
    if (r.w > 1 && r.h > 1) return static_cast<double>(r.w) / r.h;
    return outputAspectFor(maskTargetOutput());
  }

  // A new mask in the library, on the target: one shape, or none for the pen.
  MaskModel* newMaskOnTarget(deckboy::masks::MaskShape shape, bool withShape = true) {
    pushUndoSnapshot();
    MaskModel m;
    m.id = nextMaskId();
    m.name = withShape ? std::string(deckboy::masks::maskShapeLabel(shape)) : std::string("pen mask");
    if (withShape) {
      // A mask's first shape keeps: a subtract alone would cut everything.
      m.paths.push_back(deckboy::masks::makeMaskShape(shape, maskTargetAspect()));
    }
    project_.masks.push_back(m);
    setMaskTargetId(m.id);
    return findMask(m.id);
  }

  // ── WHERE THE MASK LIES ON THE MONITOR ────────────────────────────────────

  // The rectangle the target's mask spans inside `monitor`: the picture's own
  // rect (letterboxed into the monitor), or the selected cue's placed rect in
  // it -- the same placement the compositor uses (app_masks.ipp, cueMaskRect).
  SDL_FRect maskEditArea(const SDL_Rect& monitor) {
    const int out = maskTargetOutput();
    auto [outW, outH] = out >= 0 ? outputRenderSizeForOutput(out) : std::pair<int, int> {1920, 1080};
    outW = std::max(1, outW);
    outH = std::max(1, outH);
    const SDL_Rect pic = cuePlacementFor(nullptr, outW, outH, monitor).destination;
    SDL_FRect area {static_cast<float>(pic.x), static_cast<float>(pic.y), static_cast<float>(pic.w),
                    static_cast<float>(pic.h)};
    if (maskEditor_.target != MaskTarget::Cue) return area;
    const Cue* cue = selectedCuePtr();
    if (!cue || cue->kind == CueKind::Text) return area;
    int fw = cue->width > 0 ? cue->width : outW, fh = cue->height > 0 ? cue->height : outH;
    const int deck = project_.focusedDeckIndex;
    if (deck >= 0 && deck < static_cast<int>(project_.decks.size())) {
      const Deck& d = project_.decks[static_cast<std::size_t>(deck)];
      if (d.activeIndex >= 0 && d.activeIndex == d.selectedIndex) {
        if (DeckRuntime* rt = runtimeForDeck(deck); rt && rt->mediaEngine) {
          if (const DecodedFrame* f = rt->mediaEngine->currentFrame(); f && f->width > 0 && f->height > 0) {
            fw = f->width;
            fh = f->height;
          }
        }
      }
    }
    const SDL_Rect dest = cuePlacementFor(cue, fw, fh, SDL_Rect {0, 0, outW, outH}).destination;
    const float sx = area.w / static_cast<float>(outW), sy = area.h / static_cast<float>(outH);
    return SDL_FRect {area.x + dest.x * sx, area.y + dest.y * sy, dest.w * sx, dest.h * sy};
  }

  // A mask point (fractions) to the monitor, through the mask's move.
  static SDL_FPoint maskToScreen(const deckboy::masks::MaskTransform& t, const SDL_FRect& area, double fx,
                                 double fy) {
    const deckboy::masks::Pt p = deckboy::masks::applyMaskTransform(t, {fx * area.w, fy * area.h});
    return SDL_FPoint {area.x + static_cast<float>(p.x), area.y + static_cast<float>(p.y)};
  }

  bool maskEditorVisible() const { return maskEditor_.open && warpMonitorInner_.w > 0 && warpMonitorInner_.h > 0; }

  void openMaskEditor(bool open) {
    if (!open && maskEditor_.drawing) finishMaskPenPath();
    if (open) drawEditor_.open = false;   // they share the monitor; one at a time
    if (open) openInspectorSection(QuickAction::CueSectionMaskToggle);   // its controls, in view
    maskEditor_.open = open;
    maskEditor_.drag = MaskDrag::None;
    if (open) {
      warpEditMode_ = false;          // they share the monitor; one at a time
      presenterLayoutEditMode_ = false;
      cueSectionMaskOpen_ = true;
    }
  }

  // ── THE OVERLAY ───────────────────────────────────────────────────────────

  void drawMaskEditorOverlay() {
    if (!maskEditorVisible()) return;
    SDL_Renderer* ren = controlRenderer_;
    const SDL_Rect mi = warpMonitorInner_;
    SDL_Rect prevClip {};
    const bool hadClip = SDL_RenderClipEnabled(ren);
    if (hadClip) SDL_GetRenderClipRect(ren, &prevClip);
    SDL_SetRenderClipRect(ren, &mi);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    MaskModel* m = maskFocus();
    const SDL_FRect area = maskEditArea(mi);
    // What is being masked, in words, where the warp editor says what it warps.
    std::string what;
    if (!maskTargetAvailable()) {
      what = maskEditor_.target == MaskTarget::Cue ? "MASK: select a cue to mask it"
                                                   : "MASK: " + maskTargetLabel() + " is not on this output";
    } else if (m) {
      what = "MASKING " + maskTargetLabel() + ": " + m->name +
             (maskEditor_.tool == MaskTool::Pen ? "  [pen]" : "  [points]");
    } else {
      what = "MASKING " + maskTargetLabel() + ": choose a shape under MASK, or draw with the pen";
    }
    const int bannerH = textLineHeight(fontSmall_) + uiScaled(8);
    const SDL_Rect banner {mi.x, mi.y, mi.w, bannerH};
    Primitives::fillRect(ren, banner, SDL_Color {15, 15, 15, 190});
    drawCenteredTextSafe(ren, fontSmall_, banner, what, SDL_Color {124, 240, 255, 255});
    // The area the mask spans, so a cue mask's frame is visible.
    SDL_SetRenderDrawColor(ren, 124, 240, 255, 90);
    SDL_RenderRect(ren, &area);
    if (m && deckboy::masks::maskAnimates(*m)) {
      // WHERE THE CUT IS NOW, faintly, when an oscillator moves it. The points
      // below are the mask as SET -- what an edit changes -- and without this
      // they sat apart from the picture they were cutting, as if wrong.
      const auto clock = deckboy::effects::readLfoClock();
      const MaskModel live = deckboy::masks::resolveMask(*m, clock.seconds, clock.beats, clock.audio01);
      const auto lt = deckboy::masks::maskTransform(live, area.w, area.h);
      SDL_SetRenderDrawColor(ren, 255, 255, 255, 110);
      for (const auto& path : live.paths) {
        const auto flat = deckboy::masks::flattenMaskPath(path, area.w, area.h);
        for (std::size_t i = 0; i + 1 < flat.size() + (path.closed ? 1 : 0); ++i) {
          if (i % 2) continue;   // dashed
          const auto a = deckboy::masks::applyMaskTransform(lt, flat[i]);
          const auto b = deckboy::masks::applyMaskTransform(lt, flat[(i + 1) % flat.size()]);
          SDL_RenderLine(ren, area.x + static_cast<float>(a.x), area.y + static_cast<float>(a.y),
                         area.x + static_cast<float>(b.x), area.y + static_cast<float>(b.y));
        }
      }
    }
    if (m) {
      const auto t = deckboy::masks::maskTransform(*m, area.w, area.h);
      const float r = static_cast<float>(uiScaled(4));
      const int last = static_cast<int>(m->paths.size()) - 1;
      for (int pi = 0; pi <= last; ++pi) {
        const auto& path = m->paths[static_cast<std::size_t>(pi)];
        std::vector<SDL_FPoint> line;
        for (const auto& q : deckboy::masks::flattenMaskPath(path, area.w, area.h)) {
          const auto s = deckboy::masks::applyMaskTransform(t, q);
          line.push_back(SDL_FPoint {area.x + static_cast<float>(s.x), area.y + static_cast<float>(s.y)});
        }
        if (path.closed && !line.empty()) line.push_back(line.front());
        // Add in cyan, subtract in red, each over a dark shadow so the line
        // reads on any picture.
        const SDL_Color ink = path.subtract ? SDL_Color {255, 128, 128, 255} : SDL_Color {124, 240, 255, 255};
        for (int pass = 0; pass < 2; ++pass) {
          if (pass == 0) SDL_SetRenderDrawColor(ren, 0, 0, 0, 170);
          else SDL_SetRenderDrawColor(ren, ink.r, ink.g, ink.b, 255);
          const float o = pass == 0 ? 1.0f : 0.0f;
          for (std::size_t i = 1; i < line.size(); ++i) {
            SDL_RenderLine(ren, line[i - 1].x + o, line[i - 1].y + o, line[i].x + o, line[i].y + o);
          }
        }
        const bool penPath = maskEditor_.drawing && pi == last;
        for (int k = 0; k < static_cast<int>(path.points.size()); ++k) {
          const auto& p = path.points[static_cast<std::size_t>(k)];
          const SDL_FPoint a = maskToScreen(t, area, p.x, p.y);
          const bool selected = pi == maskEditor_.path && k == maskEditor_.point;
          if (selected || penPath) {
            for (int side = 0; side < 2; ++side) {
              const float hx = side ? p.outX : p.inX, hy = side ? p.outY : p.inY;
              if (hx == 0.0f && hy == 0.0f) continue;
              const SDL_FPoint h = maskToScreen(t, area, p.x + hx, p.y + hy);
              SDL_SetRenderDrawColor(ren, 255, 255, 255, 220);
              SDL_RenderLine(ren, a.x, a.y, h.x, h.y);
              const SDL_FRect hb {h.x - r * 0.75f, h.y - r * 0.75f, r * 1.5f, r * 1.5f};
              SDL_RenderFillRect(ren, &hb);
            }
          }
          // The pen's first point in yellow: click it to close the path.
          const SDL_Color fill = (penPath && k == 0) ? SDL_Color {255, 208, 64, 255}
                               : selected ? SDL_Color {255, 128, 128, 255} : ink;
          const SDL_FRect box {a.x - r, a.y - r, r * 2, r * 2};
          SDL_SetRenderDrawColor(ren, fill.r, fill.g, fill.b, 255);
          SDL_RenderFillRect(ren, &box);
          SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
          SDL_RenderRect(ren, &box);
        }
      }
    }
    SDL_SetRenderClipRect(ren, hadClip ? &prevClip : nullptr);
  }

  // ── THE POINTER ───────────────────────────────────────────────────────────

  void maskGestureUndo() {
    if (!maskEditor_.undoTaken) {
      pushUndoSnapshot();
      maskEditor_.undoTaken = true;
    }
  }

  // The pen's open path: closed if it can be, dropped if it cannot.
  void finishMaskPenPath() {
    MaskModel* m = maskFocus();
    maskEditor_.drawing = false;
    if (!m || m->paths.empty()) return;
    auto& path = m->paths.back();
    if (path.closed) return;
    if (path.points.size() >= 3) {
      path.closed = true;
      triggerToast("path closed");
    } else {
      m->paths.pop_back();
      triggerToast("a path needs three points: dropped");
    }
    maskEditor_.path = maskEditor_.point = -1;
    markProjectDirty();
  }

  void removeMaskPoint(int pi, int k) {
    MaskModel* m = maskFocus();
    if (!m || pi < 0 || pi >= static_cast<int>(m->paths.size())) return;
    auto& path = m->paths[static_cast<std::size_t>(pi)];
    if (k < 0 || k >= static_cast<int>(path.points.size())) return;
    pushUndoSnapshot();
    if (path.closed && path.points.size() <= 3) {
      // Fewer than three is not a shape: the whole path goes, and says so.
      m->paths.erase(m->paths.begin() + pi);
      triggerToast("path removed (a shape needs three points)");
    } else {
      path.points.erase(path.points.begin() + k);
      triggerToast("point removed");
    }
    maskEditor_.path = maskEditor_.point = -1;
    markProjectDirty();
  }

  // True when the press belonged to the editor.
  bool handleMaskEditorMouseDown(int x, int y) {
    if (!maskEditorVisible() || !pointInRect(x, y, warpMonitorInner_)) return false;
    const int bannerH = textLineHeight(fontSmall_) + uiScaled(8);
    if (y < warpMonitorInner_.y + bannerH) return true;
    if (!maskTargetAvailable()) {
      triggerToast("nothing to mask here: select a cue, or change MASK ON");
      return true;
    }
    maskEditor_.undoTaken = false;
    const SDL_FRect area = maskEditArea(warpMonitorInner_);
    if (area.w < 2 || area.h < 2) return true;
    const bool remove = (SDL_GetModState() & (SDL_KMOD_ALT | SDL_KMOD_SHIFT)) != 0;
    const float reach = static_cast<float>(uiScaled(8));
    const deckboy::masks::Pt press {x - area.x, y - area.y};
    MaskModel* m = maskFocus();

    if (maskEditor_.tool == MaskTool::Pen) {
      maskGestureUndo();
      if (!m) {
        m = newMaskOnTarget(deckboy::masks::MaskShape::Circle, false);
        if (!m) return true;
      }
      if (maskEditor_.drawing && !m->paths.empty() && !m->paths.back().closed) {
        auto& open = m->paths.back();
        if (open.points.size() >= 3) {
          const auto t = deckboy::masks::maskTransform(*m, area.w, area.h);
          const SDL_FPoint first = maskToScreen(t, area, open.points[0].x, open.points[0].y);
          if (std::hypot(first.x - static_cast<float>(x), first.y - static_cast<float>(y)) <= reach * 1.5f) {
            finishMaskPenPath();
            return true;
          }
        }
      } else {
        deckboy::masks::MaskPath path;
        path.closed = false;
        path.subtract = maskEditor_.subtract && !m->paths.empty();
        path.name = path.subtract ? "pen cut" : "pen path";
        m->paths.push_back(path);
        maskEditor_.drawing = true;
      }
      const auto t = deckboy::masks::maskTransform(*m, area.w, area.h);
      const auto q = deckboy::masks::invertMaskTransform(t, press);
      deckboy::masks::MaskPoint p;
      p.x = static_cast<float>(q.x / area.w);
      p.y = static_cast<float>(q.y / area.h);
      m->paths.back().points.push_back(p);
      maskEditor_.path = static_cast<int>(m->paths.size()) - 1;
      maskEditor_.point = static_cast<int>(m->paths.back().points.size()) - 1;
      maskEditor_.drag = MaskDrag::NewHandle;
      markProjectDirty();
      return true;
    }

    if (!m) return true;
    const auto t = deckboy::masks::maskTransform(*m, area.w, area.h);
    // The selected point's handles first: they sit over everything else.
    if (maskEditor_.path >= 0 && maskEditor_.path < static_cast<int>(m->paths.size())) {
      const auto& path = m->paths[static_cast<std::size_t>(maskEditor_.path)];
      if (maskEditor_.point >= 0 && maskEditor_.point < static_cast<int>(path.points.size())) {
        const auto& p = path.points[static_cast<std::size_t>(maskEditor_.point)];
        for (int side = 0; side < 2; ++side) {
          const float hx = side ? p.outX : p.inX, hy = side ? p.outY : p.inY;
          if (hx == 0.0f && hy == 0.0f) continue;
          const SDL_FPoint h = maskToScreen(t, area, p.x + hx, p.y + hy);
          if (std::hypot(h.x - static_cast<float>(x), h.y - static_cast<float>(y)) <= reach) {
            maskGestureUndo();
            maskEditor_.drag = side ? MaskDrag::Out : MaskDrag::In;
            return true;
          }
        }
      }
    }
    for (int pi = static_cast<int>(m->paths.size()) - 1; pi >= 0; --pi) {
      const auto& path = m->paths[static_cast<std::size_t>(pi)];
      for (int k = 0; k < static_cast<int>(path.points.size()); ++k) {
        const auto& p = path.points[static_cast<std::size_t>(k)];
        const SDL_FPoint a = maskToScreen(t, area, p.x, p.y);
        if (std::hypot(a.x - static_cast<float>(x), a.y - static_cast<float>(y)) > reach) continue;
        if (remove) {
          removeMaskPoint(pi, k);
          return true;
        }
        maskGestureUndo();
        maskEditor_.path = pi;
        maskEditor_.point = k;
        maskEditor_.drag = MaskDrag::Anchor;
        return true;
      }
    }
    for (int pi = static_cast<int>(m->paths.size()) - 1; pi >= 0; --pi) {
      std::size_t seg = 0;
      double along = 0.0;
      if (deckboy::masks::nearestMaskSegment(m->paths[static_cast<std::size_t>(pi)], t, area.w, area.h, press,
                                             reach, seg, along)) {
        maskGestureUndo();
        deckboy::masks::insertMaskPoint(m->paths[static_cast<std::size_t>(pi)], seg, along);
        maskEditor_.path = pi;
        maskEditor_.point = static_cast<int>(seg) + 1;
        maskEditor_.drag = MaskDrag::Anchor;
        markProjectDirty();
        triggerToast("point added");
        return true;
      }
    }
    maskEditor_.path = maskEditor_.point = -1;
    return true;
  }

  bool handleMaskEditorMouseMotion(int x, int y) {
    if (!maskEditor_.open || maskEditor_.drag == MaskDrag::None) return false;
    MaskModel* m = maskFocus();
    if (!m || maskEditor_.path < 0 || maskEditor_.path >= static_cast<int>(m->paths.size())) {
      maskEditor_.drag = MaskDrag::None;
      return false;
    }
    auto& path = m->paths[static_cast<std::size_t>(maskEditor_.path)];
    if (maskEditor_.point < 0 || maskEditor_.point >= static_cast<int>(path.points.size())) {
      maskEditor_.drag = MaskDrag::None;
      return false;
    }
    const SDL_FRect area = maskEditArea(warpMonitorInner_);
    if (area.w < 2 || area.h < 2) return true;
    const auto t = deckboy::masks::maskTransform(*m, area.w, area.h);
    const auto q = deckboy::masks::invertMaskTransform(t, {x - area.x, y - area.y});
    const float fx = static_cast<float>(q.x / area.w), fy = static_cast<float>(q.y / area.h);
    auto& p = path.points[static_cast<std::size_t>(maskEditor_.point)];
    const bool broken = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    switch (maskEditor_.drag) {
      case MaskDrag::Anchor:
        p.x = std::clamp(fx, -1.0f, 2.0f);
        p.y = std::clamp(fy, -1.0f, 2.0f);
        break;
      case MaskDrag::NewHandle:
      case MaskDrag::Out:
        p.outX = fx - p.x;
        p.outY = fy - p.y;
        if (!broken) {
          p.inX = -p.outX;
          p.inY = -p.outY;
        }
        break;
      case MaskDrag::In:
        p.inX = fx - p.x;
        p.inY = fy - p.y;
        if (!broken) {
          p.outX = -p.inX;
          p.outY = -p.inY;
        }
        break;
      default: break;
    }
    markProjectDirty();
    return true;
  }

  void endMaskEditorDrag() {
    maskEditor_.drag = MaskDrag::None;
    maskEditor_.undoTaken = false;
  }

  bool handleMaskEditorKey(SDL_Keycode key, bool keyRepeat) {
    if (!maskEditor_.open || keyRepeat) return false;
    // Escape only while the pen holds an open path. Otherwise it is the
    // show's own Escape, which nothing on the desk may take for itself.
    if (key == SDLK_ESCAPE && maskEditor_.drawing) {
      finishMaskPenPath();
      return true;
    }
    if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && maskEditor_.drawing) {
      finishMaskPenPath();
      return true;
    }
    if ((key == SDLK_DELETE || key == SDLK_BACKSPACE) && maskEditor_.point >= 0 && !maskEditor_.drawing) {
      removeMaskPoint(maskEditor_.path, maskEditor_.point);
      return true;
    }
    if (key == SDLK_P) {
      if (maskEditor_.drawing) finishMaskPenPath();
      maskEditor_.tool = maskEditor_.tool == MaskTool::Pen ? MaskTool::Points : MaskTool::Pen;
      triggerToast(maskEditor_.tool == MaskTool::Pen ? "pen" : "points");
      return true;
    }
    return false;
  }

  // ── THE INSPECTOR'S MASK SECTION ──────────────────────────────────────────

  int drawMaskRows(const InspectorCtx& ix, int y) {
    y = inspDrawChoiceRow(ix, y, "mask on", maskTargetLabel(), QuickAction::MaskTargetCycle,
                          "What the mask cuts: this cue wherever it plays, this playlist on the focused output, "
                          "that output's whole picture, or its matte");
    MaskModel* m = maskFocus();
    maskPickRect_ = {ix.ctrl.x + ix.inset, y, ix.ctrlW - ix.inset * 2, ix.rowH};
    y = inspDrawChoiceRow(ix, y, "mask", !maskTargetAvailable() ? std::string("--") : (m ? m->name : "none"),
                          QuickAction::MaskPick,
                          "Choose a mask from this show's library, or start a new one from a shape");
    y = inspDrawChoiceRow(ix, y, "points", maskEditor_.open ? "on the monitor" : "edit on the monitor",
                          QuickAction::MaskEditToggle,
                          "Show the mask's points on the programme monitor, to move, add and remove them (MASK)",
                          -1, maskEditor_.open);
    if (!m) return y;
    y = inspDrawChoiceRow(ix, y, "tool", maskEditor_.tool == MaskTool::Pen ? "pen" : "move points",
                          QuickAction::MaskToolCycle,
                          "Pen: click for corners, drag for curves, click the first point to close (P). "
                          "Points: drag, click the outline to add one, Alt-click or Delete to remove");
    y = inspDrawChoiceRow(ix, y, "new paths", maskEditor_.subtract ? "subtract" : "add",
                          QuickAction::MaskPaintToggle, "Whether the next shape or pen path adds or cuts");
    maskShapeRect_ = {ix.ctrl.x + ix.inset, y, ix.ctrlW - ix.inset * 2, ix.rowH};
    y = inspDrawChoiceRow(ix, y, "add shape", "circle, heart, star...", QuickAction::MaskAddShape,
                          "Add a library shape to this mask; its points are yours to move");
    y = inspDrawChoiceRow(ix, y, "invert", m->invert ? "on" : "off", QuickAction::MaskInvertToggle,
                          "Keep the outside and cut the inside instead");
    struct Row {
      const char* label;
      NumericParam id;
      int slot;
      const char* tip;
    };
    const Row kRows[] = {
      {"feather", NumericParam::MaskFeather, deckboy::masks::kMaskLfoFeather, "How soft the edge is, as % of the height"},
      {"grow", NumericParam::MaskGrow, deckboy::masks::kMaskLfoGrow, "Push the edge out (+) or in (-), as % of the height"},
      {"opacity", NumericParam::MaskOpacity, deckboy::masks::kMaskLfoOpacity, "How hard it cuts: 0 leaves the picture alone"},
      {"x", NumericParam::MaskX, deckboy::masks::kMaskLfoX, "Move it across, as % of the width"},
      {"y", NumericParam::MaskY, deckboy::masks::kMaskLfoY, "Move it down, as % of the height"},
      {"scale", NumericParam::MaskScale, deckboy::masks::kMaskLfoScale, "Size, about its middle"},
      {"rotation", NumericParam::MaskRotation, deckboy::masks::kMaskLfoRotation, "Turn, about its middle"},
    };
    for (const Row& row : kRows) {
      const double value = maskParamInOperatorUnits(*m, row.id);
      char text[24];
      if (row.id == NumericParam::MaskRotation) std::snprintf(text, sizeof(text), "%.0f deg", value);
      else std::snprintf(text, sizeof(text), "%.0f%%", value);
      const auto& lfo = m->lfo[static_cast<std::size_t>(row.slot)];
      inspDrawQuickRow(ix, y, row.label, QuickAction::NumericParamDec, text, QuickAction::NumericParamInc,
                       QuickAction::ToggleLoop, false, false, row.tip, true, QuickAction::EditNumericParam,
                       static_cast<int>(row.id), QuickAction::EffectLfoToggle, lfo.on, kMaskLfoPackBase + row.slot,
                       "~", "Put this control on an oscillator");
      y += ix.rowStep;
      if (lfo.on) y = inspDrawLfoRowFor(ix, y, lfo, kMaskLfoPackBase + row.slot);
    }
    const std::size_t paths = m->paths.size();
    y = inspDrawChoiceRow(ix, y, "paths",
                          std::to_string(paths) + (paths == 1 ? " path" : " paths") + (paths ? ": remove last" : ""),
                          QuickAction::MaskRemoveLast, "Take the newest shape or pen path out of this mask");
    y = inspDrawChoiceRow(ix, y, "export", "PNG matte", QuickAction::MaskExportPng,
                          "Save the mask as a greyscale PNG at the output's size: white keeps, black cuts");
    y = inspDrawChoiceRow(ix, y, "", "SVG paths", QuickAction::MaskExportSvg,
                          "Save the mask's paths as SVG, for Illustrator and anything else that reads them");
    const int uses = countMaskUses(m->id);
    if (uses > 1) {
      y = inspDrawMessageRow(ix, y, "\"" + m->name + "\" is in " + std::to_string(uses) +
                                        " places: an edit reaches them all", pal.tile, pal.fgSoft);
    }
    return y;
  }

  void openMaskPicker() {
    if (!maskTargetAvailable()) {
      triggerToast("nothing to mask here: select a cue, or change MASK ON");
      return;
    }
    std::vector<std::pair<std::string, std::string>> options {{"none", "none"}};
    for (const MaskModel& m : project_.masks) options.emplace_back("id:" + m.id, m.name);
    for (int i = 0; i < static_cast<int>(deckboy::masks::MaskShape::Count); ++i) {
      const auto shape = static_cast<deckboy::masks::MaskShape>(i);
      options.emplace_back(std::string("new:") + deckboy::masks::maskShapeToken(shape),
                           std::string("new ") + deckboy::masks::maskShapeLabel(shape));
    }
    const std::string current = maskTargetId();
    openDropdown("mask.pick", maskPickRect_, options, current.empty() ? "none" : "id:" + current,
                 [this](const std::string& choice) {
                   if (choice == "none") {
                     pushUndoSnapshot();
                     setMaskTargetId({});
                     triggerToast("mask off");
                   } else if (choice.rfind("id:", 0) == 0) {
                     pushUndoSnapshot();
                     setMaskTargetId(choice.substr(3));
                     if (const MaskModel* m = maskFocus()) triggerToast("mask: " + m->name);
                   } else if (choice.rfind("new:", 0) == 0) {
                     deckboy::masks::MaskShape shape;
                     if (deckboy::masks::maskShapeFromToken(choice.substr(4), shape)) {
                       if (MaskModel* m = newMaskOnTarget(shape)) triggerToast("new mask: " + m->name);
                     }
                   }
                 });
  }

  void openMaskShapePicker() {
    if (!maskFocus()) return;
    std::vector<std::pair<std::string, std::string>> options;
    for (int i = 0; i < static_cast<int>(deckboy::masks::MaskShape::Count); ++i) {
      const auto shape = static_cast<deckboy::masks::MaskShape>(i);
      options.emplace_back(deckboy::masks::maskShapeToken(shape), deckboy::masks::maskShapeLabel(shape));
    }
    openDropdown("mask.shape", maskShapeRect_, options, {}, [this](const std::string& choice) {
      deckboy::masks::MaskShape shape;
      MaskModel* m = maskFocus();
      if (!m || !deckboy::masks::maskShapeFromToken(choice, shape)) return;
      pushUndoSnapshot();
      auto path = deckboy::masks::makeMaskShape(shape, maskTargetAspect());
      path.subtract = maskEditor_.subtract && !m->paths.empty();
      m->paths.push_back(path);
      markProjectDirty();
      triggerToast(std::string(path.subtract ? "cut: " : "added: ") + deckboy::masks::maskShapeLabel(shape));
    });
  }

  void exportFocusedMask(bool svg) {
    const MaskModel* m = maskFocus();
    if (!m) return;
    // SDL reads the filter list after this returns, so it must outlive the
    // dialog: static, never a temporary (see File Dialogs in CLAUDE.md).
    static const std::vector<SDL_DialogFileFilter> kPng {{"PNG matte", "png"}};
    static const std::vector<SDL_DialogFileFilter> kSvg {{"SVG paths", "svg"}};
    const std::string id = m->id;
    const int out = std::max(0, maskTargetOutput());
    showSaveFileDialog(svg ? kSvg : kPng, [this, id, svg, out](std::vector<std::string> picked) {
      if (!picked.empty()) finishMaskExport(id, svg, out, picked[0]);
    });
  }

  // The dialog's answer. A member rather than the lambda's body: written
  // inline, that body crashed MSVC (C1001, VS 17.x).
  void finishMaskExport(const std::string& id, bool svg, int out, std::string path) {
    const MaskModel* mask = findMask(id);
    if (!mask || path.empty()) return;
    const std::string want = svg ? ".svg" : ".png";
    if (toLower(fs::path(path).extension().string()) != want) path += want;
    const std::pair<int, int> size = outputRenderSizeForOutput(out);
    std::string error;
    if (exportMask(*mask, path, std::max(1, size.first), std::max(1, size.second), error)) {
      triggerToast("mask saved: " + fs::path(path).filename().string());
    } else {
      triggerToast("mask not saved: " + error, ToastKind::Warning, kToastReadableMs);
    }
  }

  // True when the action was one of the mask's.
  bool handleMaskQuickAction(QuickAction action) {
    MaskModel* m = maskFocus();
    switch (action) {
      case QuickAction::MaskTargetCycle: cycleMaskTarget(); return true;
      case QuickAction::MaskPick: openMaskPicker(); return true;
      case QuickAction::MaskEditToggle: openMaskEditor(!maskEditor_.open); return true;
      case QuickAction::MaskToolCycle:
        if (maskEditor_.drawing) finishMaskPenPath();
        maskEditor_.tool = maskEditor_.tool == MaskTool::Pen ? MaskTool::Points : MaskTool::Pen;
        // The pen draws on the monitor, so choosing it opens the editor there.
        if (maskEditor_.tool == MaskTool::Pen && !maskEditor_.open) openMaskEditor(true);
        return true;
      case QuickAction::MaskPaintToggle: maskEditor_.subtract = !maskEditor_.subtract; return true;
      case QuickAction::MaskAddShape: openMaskShapePicker(); return true;
      case QuickAction::MaskInvertToggle:
        if (m) {
          pushUndoSnapshot();
          m->invert = !m->invert;
          markProjectDirty();
          triggerToast(m->invert ? "mask inverted" : "mask not inverted");
        }
        return true;
      case QuickAction::MaskRemoveLast:
        if (m && !m->paths.empty()) {
          pushUndoSnapshot();
          m->paths.pop_back();
          maskEditor_.drawing = false;
          maskEditor_.path = maskEditor_.point = -1;
          markProjectDirty();
          triggerToast("path removed");
        }
        return true;
      case QuickAction::MaskExportPng: exportFocusedMask(false); return true;
      case QuickAction::MaskExportSvg: exportFocusedMask(true); return true;
      case QuickAction::CueSectionMaskToggle: cueSectionMaskOpen_ = !cueSectionMaskOpen_; return true;
      default: return false;
    }
  }

  // For STATUS JSON: the editor, where its button and area are, and every
  // point where it is drawn -- so a check can press and drag them for real.
  std::string maskEditorStatusJson() {
    std::ostringstream out;
    const SDL_FRect area = maskEditArea(warpMonitorInner_);
    MaskModel* m = maskFocus();
    out << "\"maskEditor\":{\"open\":" << (maskEditor_.open ? "true" : "false")
        << ",\"tool\":\"" << (maskEditor_.tool == MaskTool::Pen ? "pen" : "points") << "\""
        << ",\"target\":" << static_cast<int>(maskEditor_.target)
        << ",\"drawing\":" << (maskEditor_.drawing ? "true" : "false")
        << ",\"mask\":\"" << (m ? m->id : std::string()) << "\""
        << ",\"button\":[" << maskEditBtnRect_.x << "," << maskEditBtnRect_.y << "," << maskEditBtnRect_.w << ","
        << maskEditBtnRect_.h << "]"
        << ",\"area\":[" << area.x << "," << area.y << "," << area.w << "," << area.h << "]"
        << ",\"paths\":[";
    if (m) {
      const auto t = deckboy::masks::maskTransform(*m, area.w, area.h);
      for (std::size_t pi = 0; pi < m->paths.size(); ++pi) {
        const auto& path = m->paths[pi];
        out << (pi ? "," : "") << "{\"closed\":" << (path.closed ? "true" : "false")
            << ",\"subtract\":" << (path.subtract ? "true" : "false") << ",\"points\":[";
        for (std::size_t k = 0; k < path.points.size(); ++k) {
          const auto& p = path.points[k];
          const SDL_FPoint s = maskToScreen(t, area, p.x, p.y);
          out << (k ? "," : "") << "[" << std::lround(s.x) << "," << std::lround(s.y) << "," << p.x << "," << p.y
              << "," << p.outX << "," << p.outY << "]";
        }
        out << "]}";
      }
    }
    out << "]},";
    return out.str();
  }
