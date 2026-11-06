// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// MASKS ON THE PICTURE
// ============================================================================
//
// What a mask IS lives in core/masks.hpp. This is where one cuts:
//
//   - on a CUE: over the cue's own placed rectangle, wherever it plays;
//   - on a LAYER: over the output's raster, that playlist only;
//   - on an OUTPUT: the whole finished picture, to black -- and to
//     transparent on a key signal;
//   - in the MATTE slot ("mask:<id>"): painted black at the matte's opacity,
//     under the house overlay, like the aspect bars it sits beside.
//
// A masked layer is drawn into a scratch texture first, cut there, and laid on
// the compositor with the PREMULTIPLIED form of its blend: drawing onto clear
// premultiplies, so the straight-alpha blend would apply the alpha twice and
// darken every soft edge.
//
// Rasters are shared by every output (maskRasters_) and redrawn only when the
// mask changes -- every frame while an oscillator moves it, which is why a
// moving mask is drawn at no more than 1920 across and scaled up.

  using MaskModel = deckboy::masks::Mask;

  enum class MaskUse { Cut, Matte };

  struct MaskRaster {
    std::string signature;
    std::vector<std::uint8_t> rgba;   // white, alpha = the mask
    int w = 0;
    int h = 0;
    std::uint64_t serial = 0;
    Uint64 usedAtMs = 0;
  };
  std::map<std::string, MaskRaster> maskRasters_;
  std::uint64_t maskRasterSerial_ = 0;
  bool maskRendererWarned_ = false;
  // While a masked layer is drawn into its scratch, the blend the crossfader
  // asked for is recorded here and the draw itself made plain (see
  // outputCrossfadeGain): the blend is applied once, when the scratch is laid
  // on the compositor.
  mutable bool maskBlendCapture_ = false;
  mutable std::string maskCapturedMode_;

  MaskModel* findMask(const std::string& id) {
    if (id.empty()) return nullptr;
    for (MaskModel& m : project_.masks) {
      if (m.id == id) return &m;
    }
    return nullptr;
  }
  const MaskModel* findMask(const std::string& id) const {
    return const_cast<App*>(this)->findMask(id);
  }

  // The lowest "m<n>" no mask has. Deleting a mask clears every reference to
  // it first (deleteMask), so a reused id can never revive a stale one.
  std::string nextMaskId() const {
    for (int n = 1;; ++n) {
      const std::string id = "m" + std::to_string(n);
      if (!findMask(id)) return id;
    }
  }

  double outputAspectFor(int outputIndex) const {
    if (project_.outputs.empty()) return 16.0 / 9.0;
    const int i = std::clamp(outputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    const auto [w, h] = outputRenderSizeForOutput(i);
    return w > 0 && h > 0 ? static_cast<double>(w) / h : 16.0 / 9.0;
  }

  // Everything that refers to a mask, so deleting one leaves nothing pointing
  // at an id a later mask may be given.
  template <class Fn>
  void forEachMaskReference(Fn fn) {
    for (Deck& deck : project_.decks) {
      for (Cue& cue : deck.cues) fn(cue.maskId);
    }
    for (Cue& step : project_.trackerSteps) fn(step.maskId);
    for (OutputTarget& out : project_.outputs) {
      fn(out.maskId);
      fn(out.baseLayerMaskId);
      for (OutputLayer& layer : out.layerDecks) fn(layer.maskId);
    }
  }

  int countMaskUses(const std::string& id) {
    int uses = 0;
    forEachMaskReference([&](std::string& ref) { uses += ref == id; });
    for (const OutputTarget& out : project_.outputs) uses += out.matteAspect == "mask:" + id;
    return uses;
  }

  void deleteMask(const std::string& id) {
    forEachMaskReference([&](std::string& ref) {
      if (ref == id) ref.clear();
    });
    for (OutputTarget& out : project_.outputs) {
      if (out.matteAspect == "mask:" + id) out.matteAspect = "off";
    }
    project_.masks.erase(std::remove_if(project_.masks.begin(), project_.masks.end(),
                                        [&](const MaskModel& m) { return m.id == id; }),
                         project_.masks.end());
  }

  // ── THE RASTER CACHE ──────────────────────────────────────────────────────

  static void maskWorkSize(int w, int h, bool moving, int& rw, int& rh) {
    const int cap = moving ? 1920 : 4096;
    const int longest = std::max(w, h);
    if (longest <= cap) {
      rw = w;
      rh = h;
      return;
    }
    const double k = static_cast<double>(cap) / longest;
    rw = std::max(1, static_cast<int>(std::lround(w * k)));
    rh = std::max(1, static_cast<int>(std::lround(h * k)));
  }

  // The mask as it stands this frame, for a w x h area. `key` comes back for
  // the texture cache, which is keyed the same way.
  const MaskRaster* maskRasterFor(const MaskModel& m, int w, int h, MaskUse use, double matteOpacity,
                                  std::string& key) {
    if (w <= 0 || h <= 0) return nullptr;
    const auto clock = deckboy::effects::readLfoClock();
    const MaskModel live = deckboy::masks::resolveMask(m, clock.seconds, clock.beats, clock.audio01);
    int rw = 0, rh = 0;
    maskWorkSize(w, h, deckboy::masks::maskAnimates(m), rw, rh);
    key = m.id + '@' + std::to_string(w) + 'x' + std::to_string(h) + (use == MaskUse::Matte ? ":matte" : ":cut");
    std::string signature = deckboy::masks::serializeMask(live, std::string()) + '|' + std::to_string(rw) + 'x' +
                            std::to_string(rh);
    if (use == MaskUse::Matte) signature += "|" + std::to_string(std::lround(matteOpacity * 1000.0));
    const Uint64 now = SDL_GetTicks();
    // Forget sizes nobody has drawn for a while: a cue mask follows its cue's
    // rectangle, and a resize leaves the old size behind.
    for (auto it = maskRasters_.begin(); it != maskRasters_.end();) {
      it = now - it->second.usedAtMs > 5000 && it->first != key ? maskRasters_.erase(it) : std::next(it);
    }
    MaskRaster& r = maskRasters_[key];
    r.usedAtMs = now;
    if (r.signature != signature) {
      const std::vector<std::uint8_t> alpha = deckboy::masks::rasterizeMask(live, rw, rh);
      r.rgba.assign(alpha.size() * 4, 255);
      const double strength = std::clamp(matteOpacity, 0.0, 1.0);
      for (std::size_t i = 0; i < alpha.size(); ++i) {
        std::uint8_t a = alpha[i];
        if (use == MaskUse::Matte) {
          // Black where the mask cuts, at the matte's opacity.
          a = static_cast<std::uint8_t>(std::lround(255.0 * (1.0 - strength * (1.0 - a / 255.0))));
        }
        r.rgba[i * 4 + 3] = a;
      }
      r.w = rw;
      r.h = rh;
      r.signature = std::move(signature);
      r.serial = ++maskRasterSerial_;
    }
    return &r;
  }

  // THE MONITOR WITH NO OUTPUT ARMED (2026-11-05). Masks are applied by the
  // output's compositor, and with no output the monitor shows the decoder's
  // picture, so a cue's mask did not show at all -- found by the Masks lesson,
  // whose saucer stayed whole under a heart. After the picture is drawn, this
  // lays the monitor's black over it through the inverse of the cue's mask:
  // what the mask cuts reads as the output would show it, for every kind of
  // source (hardware-decoded frames never reach the CPU, so the cut is drawn,
  // not applied to pixels). Layer, output and matte masks stay the output's.
  SDL_Texture* monitorMaskCoverTex_ = nullptr;
  int monitorMaskCoverW_ = 0, monitorMaskCoverH_ = 0;
  std::string monitorMaskCoverKey_;

  void drawMonitorCueMaskCover(int deckIndex, const Cue& cue, const SDL_Rect& inner) {
    const MaskModel* found = cue.maskId.empty() ? nullptr : findMask(cue.maskId);
    if (!found || !controlRenderer_) return;
    const MaskModel m = deckboy::masks::resolveMask(*found, lfoSeconds_, lfoBeats_, 0.0);
    const SDL_Rect r = cueMaskRect(deckIndex, cue, inner);
    if (r.w <= 0 || r.h <= 0) return;
    // Drawn smaller and scaled up smoothly: the cut's edge is soft anyway.
    const int w = std::min(r.w, 960);
    const int h = std::max(1, static_cast<int>(std::lround(static_cast<double>(r.h) * w / r.w)));
    const std::string key = deckboy::masks::serializeMask(m, std::string()) + "|" + std::to_string(w) + "x" +
                            std::to_string(h);
    if (key != monitorMaskCoverKey_ || !monitorMaskCoverTex_) {
      const std::vector<std::uint8_t> alpha = deckboy::masks::rasterizeMask(m, w, h);
      std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 0);
      for (std::size_t i = 0; i < alpha.size() && i * 4 + 3 < px.size(); ++i) {
        px[i * 4 + 3] = static_cast<std::uint8_t>(255 - alpha[i]);
      }
      if (!monitorMaskCoverTex_ || monitorMaskCoverW_ != w || monitorMaskCoverH_ != h) {
        if (monitorMaskCoverTex_) SDL_DestroyTexture(monitorMaskCoverTex_);
        monitorMaskCoverTex_ = deckboyCreateTexture(controlRenderer_, SDL_PIXELFORMAT_RGBA32,
                                                    SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!monitorMaskCoverTex_) return;
        SDL_SetTextureScaleMode(monitorMaskCoverTex_, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(monitorMaskCoverTex_, SDL_BLENDMODE_BLEND);
        monitorMaskCoverW_ = w;
        monitorMaskCoverH_ = h;
      }
      SDL_UpdateTexture(monitorMaskCoverTex_, nullptr, px.data(), w * 4);
      monitorMaskCoverKey_ = key;
    }
    const SDL_FRect where {static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w),
                           static_cast<float>(r.h)};
    SDL_RenderTexture(controlRenderer_, monitorMaskCoverTex_, nullptr, &where);
    // Outside the cue's rectangle: the mask's own value there, as on an output.
    const float outside = deckboy::masks::maskValueOutside(m);
    if (outside < 0.999f) {
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, static_cast<Uint8>(std::lround((1.0f - outside) * 255.0f)));
      const float x0 = static_cast<float>(inner.x), y0 = static_cast<float>(inner.y);
      const float x1 = static_cast<float>(inner.x + inner.w), y1 = static_cast<float>(inner.y + inner.h);
      const SDL_FRect bands[4] {
        {x0, y0, x1 - x0, std::max(0.0f, where.y - y0)},
        {x0, where.y + where.h, x1 - x0, std::max(0.0f, y1 - (where.y + where.h))},
        {x0, where.y, std::max(0.0f, where.x - x0), where.h},
        {where.x + where.w, where.y, std::max(0.0f, x1 - (where.x + where.w)), where.h},
      };
      for (const SDL_FRect& band : bands) {
        if (band.w > 0.0f && band.h > 0.0f) SDL_RenderFillRect(controlRenderer_, &band);
      }
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    }
  }

  SDL_Texture* maskTextureFor(OutputRuntime& rt, const MaskModel& m, int w, int h, MaskUse use,
                              double matteOpacity = 1.0) {
    std::string key;
    const MaskRaster* r = maskRasterFor(m, w, h, use, matteOpacity, key);
    if (!r || !rt.outputRenderer) return nullptr;
    const Uint64 now = SDL_GetTicks();
    for (auto it = rt.maskTextures.begin(); it != rt.maskTextures.end();) {
      if (now - it->second.usedAtMs > 5000 && it->first != key) {
        if (it->second.texture) SDL_DestroyTexture(it->second.texture);
        it = rt.maskTextures.erase(it);
      } else {
        ++it;
      }
    }
    OutputRuntime::MaskTexture& slot = rt.maskTextures[key];
    slot.usedAtMs = now;
    if (slot.texture) {
      float tw = 0.0f, th = 0.0f;
      SDL_GetTextureSize(slot.texture, &tw, &th);
      if (static_cast<int>(tw) != r->w || static_cast<int>(th) != r->h) {
        SDL_DestroyTexture(slot.texture);
        slot.texture = nullptr;
      }
    }
    if (!slot.texture) {
      slot.texture = deckboyCreateTexture(rt.outputRenderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                          r->w, r->h);
      if (!slot.texture) return nullptr;
      // A moving mask is drawn small and scaled up; nearest would step it.
      SDL_SetTextureScaleMode(slot.texture, SDL_SCALEMODE_LINEAR);
      slot.serial = 0;
    }
    if (slot.serial != r->serial) {
      SDL_UpdateTexture(slot.texture, nullptr, r->rgba.data(), r->w * 4);
      slot.serial = r->serial;
    }
    return slot.texture;
  }

  void releaseMaskTextures(OutputRuntime& rt) {
    for (auto& [key, slot] : rt.maskTextures) {
      if (slot.texture) SDL_DestroyTexture(slot.texture);
    }
    rt.maskTextures.clear();
    if (rt.maskScratch) SDL_DestroyTexture(rt.maskScratch);
    rt.maskScratch = nullptr;
    rt.maskScratchW = rt.maskScratchH = 0;
    if (rt.sliceScratch) SDL_DestroyTexture(rt.sliceScratch);
    rt.sliceScratch = nullptr;
    rt.sliceScratchW = rt.sliceScratchH = 0;
  }

  // ── BLENDS ────────────────────────────────────────────────────────────────

  // Multiplies what is under it -- colour and alpha -- by the mask's alpha.
  // The target is premultiplied (a layer drawn onto clear), so scaling both
  // is the correct cut.
  static SDL_BlendMode maskCutBlend() {
    static const SDL_BlendMode mode = SDL_ComposeCustomBlendMode(
      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    return mode;
  }

  // The matte darkens the colour only and leaves alpha as it was, as the
  // aspect bars do.
  static SDL_BlendMode maskMatteBlend() {
    static const SDL_BlendMode mode = SDL_ComposeCustomBlendMode(
      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    return mode;
  }

  // vjBlendModeFor, for a source that is already premultiplied: every factor
  // that scaled the source by its own alpha becomes ONE.
  static SDL_BlendMode premultipliedBlendFor(const std::string& mode) {
    auto compose = [](SDL_BlendFactor srcF, SDL_BlendFactor dstF, SDL_BlendOperation op) {
      return SDL_ComposeCustomBlendMode(srcF, dstF, op, SDL_BLENDFACTOR_ONE,
                                        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    };
    static const SDL_BlendMode kScreen = compose(SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR, SDL_BLENDFACTOR_ONE,
                                                 SDL_BLENDOPERATION_ADD);
    static const SDL_BlendMode kMultiply = compose(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                                   SDL_BLENDOPERATION_ADD);
    static const SDL_BlendMode kLighten = compose(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
                                                  SDL_BLENDOPERATION_MAXIMUM);
    static const SDL_BlendMode kDarken = compose(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
                                                 SDL_BLENDOPERATION_MINIMUM);
    static const SDL_BlendMode kSubtract = compose(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
                                                   SDL_BLENDOPERATION_SUBTRACT);
    static const SDL_BlendMode kUndercut = compose(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
                                                   SDL_BLENDOPERATION_REV_SUBTRACT);
    static const SDL_BlendMode kInfiltrate = compose(SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR,
                                                     SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    static const SDL_BlendMode kEmberIn = compose(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE,
                                                  SDL_BLENDOPERATION_ADD);
    if (mode == "screen")     return kScreen;
    if (mode == "multiply")   return kMultiply;
    if (mode == "lighten")    return kLighten;
    if (mode == "darken")     return kDarken;
    if (mode == "subtract")   return kSubtract;
    if (mode == "undercut")   return kUndercut;
    if (mode == "infiltrate") return kInfiltrate;
    if (mode == "ember")      return kEmberIn;
    if (mode == "add")        return SDL_BLENDMODE_ADD_PREMULTIPLIED;
    return SDL_BLENDMODE_BLEND_PREMULTIPLIED;
  }

  // True where the renderer can do the cut as a blend. SDL's software
  // renderer (a PC with no graphics card, a virtual machine, some remote
  // desktops) has no custom blends; there the cut is done on the processor
  // instead (cpuMaskCut), and only if THAT fails is the operator told.
  static bool maskBlendSupported(SDL_Texture* tex, SDL_BlendMode mode) {
    return tex && SDL_SetTextureBlendMode(tex, mode);
  }

  void warnMaskCannotCut() {
    if (!maskRendererWarned_) {
      maskRendererWarned_ = true;
      triggerToast("this output's renderer could not cut the mask", ToastKind::Warning, kToastReadableMs);
    }
  }

  // The cut on the processor: the render target's pixels under the mask are
  // read back, multiplied by the mask (its alpha, sampled bilinearly, so a
  // mask drawn small and scaled up is as smooth as on the graphics card), and
  // written back. `where` is the mask's rectangle on the target; outside it
  // the value is `outside` (1 keeps). `alphaToo` cuts alpha as well as colour
  // (a layer, or a keyed output); otherwise only the colour darkens (the
  // matte, an opaque output). Coordinates are the current target's.
  bool cpuMaskCut(OutputRuntime& rt, const MaskModel& m, int rasterW, int rasterH, MaskUse use,
                  double matteOpacity, const SDL_Rect& where, bool alphaToo, float outside, const SDL_Rect& frame,
                  bool colourToo = true) {
    std::string key;
    const MaskRaster* r = maskRasterFor(m, rasterW, rasterH, use, matteOpacity, key);
    if (!r || r->w <= 0 || r->h <= 0 || !rt.outputRenderer || where.w <= 0 || where.h <= 0) return false;
    SDL_Renderer* ren = rt.outputRenderer;
    const SDL_Rect wanted = outside < 0.999f ? frame : where;
    SDL_Rect region {};
    if (!SDL_GetRectIntersection(&wanted, &frame, &region)) return true;
    SDL_Surface* got = SDL_RenderReadPixels(ren, &region);
    if (!got) return false;
    SDL_Surface* px = SDL_ConvertSurface(got, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(got);
    if (!px) return false;
    const auto alphaAt = [r](int x, int y) {
      x = std::clamp(x, 0, r->w - 1);
      y = std::clamp(y, 0, r->h - 1);
      return r->rgba[(static_cast<std::size_t>(y) * r->w + x) * 4 + 3] / 255.0f;
    };
    const float sx = static_cast<float>(r->w) / where.w, sy = static_cast<float>(r->h) / where.h;
    for (int y = 0; y < region.h; ++y) {
      Uint8* row = static_cast<Uint8*>(px->pixels) + static_cast<std::size_t>(y) * px->pitch;
      const int gy = region.y + y;
      const bool rowIn = gy >= where.y && gy < where.y + where.h;
      const float v = (gy - where.y + 0.5f) * sy - 0.5f;
      const int v0 = static_cast<int>(std::floor(v));
      const float fv = v - v0;
      for (int x = 0; x < region.w; ++x) {
        const int gx = region.x + x;
        float a = outside;
        if (rowIn && gx >= where.x && gx < where.x + where.w) {
          const float u = (gx - where.x + 0.5f) * sx - 0.5f;
          const int u0 = static_cast<int>(std::floor(u));
          const float fu = u - u0;
          a = (alphaAt(u0, v0) * (1 - fu) + alphaAt(u0 + 1, v0) * fu) * (1 - fv) +
              (alphaAt(u0, v0 + 1) * (1 - fu) + alphaAt(u0 + 1, v0 + 1) * fu) * fv;
        }
        if (a >= 0.999f) continue;
        Uint8* p = row + x * 4;
        if (colourToo) {
          p[0] = static_cast<Uint8>(std::lround(p[0] * a));
          p[1] = static_cast<Uint8>(std::lround(p[1] * a));
          p[2] = static_cast<Uint8>(std::lround(p[2] * a));
        }
        if (alphaToo) p[3] = static_cast<Uint8>(std::lround(p[3] * a));
      }
    }
    SDL_Texture* back = deckboyCreateTextureFromSurface(ren, px);
    SDL_DestroySurface(px);
    if (!back) return false;
    SDL_SetTextureBlendMode(back, SDL_BLENDMODE_NONE);
    const SDL_FRect dst {static_cast<float>(region.x), static_cast<float>(region.y), static_cast<float>(region.w),
                         static_cast<float>(region.h)};
    SDL_RenderTexture(ren, back, nullptr, &dst);
    SDL_DestroyTexture(back);
    return true;
  }

  SDL_Texture* ensureMaskScratch(OutputRuntime& rt) {
    const int w = rt.compositorWidth, h = rt.compositorHeight;
    if (w <= 0 || h <= 0 || !rt.outputRenderer) return nullptr;
    if (rt.maskScratch && rt.maskScratchW == w && rt.maskScratchH == h) return rt.maskScratch;
    if (rt.maskScratch) SDL_DestroyTexture(rt.maskScratch);
    rt.maskScratch = nullptr;
    // The compositor's own format and colourspace, so an HDR layer is cut in
    // linear float like the rest of its picture.
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
    rt.maskScratch = SDL_CreateTextureWithProperties(rt.outputRenderer, props);
    SDL_DestroyProperties(props);
    if (!rt.maskScratch) return nullptr;
    SDL_SetTextureScaleMode(rt.maskScratch, SDL_SCALEMODE_NEAREST);
    rt.maskScratchW = w;
    rt.maskScratchH = h;
    return rt.maskScratch;
  }

  // The cue's placed rectangle on a raster, as the compositor will draw it.
  SDL_Rect cueMaskRect(int deckIndex, const Cue& cue, const SDL_Rect& bounds) {
    int fw = bounds.w, fh = bounds.h;
    if (DeckRuntime* rt = runtimeForDeck(deckIndex); rt && rt->mediaEngine) {
      if (const DecodedFrame* f = rt->mediaEngine->currentFrame(); f && f->width > 0 && f->height > 0) {
        fw = f->width;
        fh = f->height;
      }
    }
    if (cue.kind == CueKind::Text) return bounds;
    return cuePlacementFor(&cue, fw, fh, bounds).destination;
  }

  // The masks on one playlist on one output: its layer's, and its active cue's.
  void masksForDeckLayer(int stackOutput, int deckIndex, const MaskModel*& layerMask, const MaskModel*& cueMask,
                         const Cue*& cue) {
    layerMask = nullptr;
    cueMask = nullptr;
    cue = activeCuePtr(deckIndex);
    if (stackOutput >= 0 && stackOutput < static_cast<int>(project_.outputs.size())) {
      if (const OutputLayer* layer = layerRecordFor(stackOutput, deckIndex)) {
        layerMask = findMask(layer->maskId);
      } else if (stackPositionFor(stackOutput, deckIndex) == 0) {
        layerMask = findMask(project_.outputs[static_cast<std::size_t>(stackOutput)].baseLayerMaskId);
      }
    }
    if (cue) cueMask = findMask(cue->maskId);
  }

  // Draws one playlist into the compositor THROUGH its masks. False when it
  // has none, or cannot here, and the caller draws it the ordinary way.
  bool renderMaskedDeckLayer(int outputIndex, int deckIndex, const SDL_Rect& bounds, int compositionIndex) {
    OutputRuntime* rt = runtimeForOutput(outputIndex);
    if (!rt || !rt->outputRenderer || !rt->compositorTexture) return false;
    const int stackOutput = compositionIndex >= 0 ? compositionIndex : outputIndex;
    const MaskModel* layerMask = nullptr;
    const MaskModel* cueMask = nullptr;
    const Cue* cue = nullptr;
    masksForDeckLayer(stackOutput, deckIndex, layerMask, cueMask, cue);
    if (!layerMask && !cueMask) return false;
    SDL_Texture* scratch = ensureMaskScratch(*rt);
    if (!scratch) return false;
    SDL_Renderer* ren = rt->outputRenderer;
    SDL_Texture* previous = SDL_GetRenderTarget(ren);
    SDL_SetRenderTarget(ren, scratch);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
    SDL_RenderClear(ren);
    maskBlendCapture_ = true;
    maskCapturedMode_ = "dissolve";
    renderDeckWithTransitionIntoOutput(outputIndex, deckIndex, bounds, compositionIndex);
    maskBlendCapture_ = false;
    const SDL_FRect whole {0.0f, 0.0f, static_cast<float>(rt->compositorWidth),
                           static_cast<float>(rt->compositorHeight)};
    const SDL_Rect frame {0, 0, rt->compositorWidth, rt->compositorHeight};
    if (layerMask) {
      SDL_Texture* tex = maskTextureFor(*rt, *layerMask, rt->compositorWidth, rt->compositorHeight, MaskUse::Cut);
      if (maskBlendSupported(tex, maskCutBlend())) {
        SDL_RenderTexture(ren, tex, nullptr, &whole);
      } else if (!cpuMaskCut(*rt, *layerMask, rt->compositorWidth, rt->compositorHeight, MaskUse::Cut, 1.0,
                             frame, /*alphaToo=*/true, 1.0f, frame)) {
        warnMaskCannotCut();
      }
    }
    if (cueMask && cue) {
      const SDL_Rect r = cueMaskRect(deckIndex, *cue, bounds);
      SDL_Texture* tex = r.w > 0 && r.h > 0 ? maskTextureFor(*rt, *cueMask, r.w, r.h, MaskUse::Cut) : nullptr;
      if (maskBlendSupported(tex, maskCutBlend())) {
        const SDL_FRect where {static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w),
                               static_cast<float>(r.h)};
        SDL_RenderTexture(ren, tex, nullptr, &where);
        // Outside the cue's rectangle no path reaches: the mask's own value
        // there (cut, or kept when inverted), so a backdrop is cut the same.
        const float outside = deckboy::masks::maskValueOutside(*cueMask);
        if (outside < 0.999f) {
          SDL_SetRenderDrawBlendMode(ren, maskCutBlend());
          SDL_SetRenderDrawColor(ren, 255, 255, 255, static_cast<Uint8>(std::lround(outside * 255.0f)));
          const float x0 = where.x, y0 = where.y, x1 = where.x + where.w, y1 = where.y + where.h;
          const SDL_FRect bands[4] {
            {0.0f, 0.0f, whole.w, std::max(0.0f, y0)},
            {0.0f, y1, whole.w, std::max(0.0f, whole.h - y1)},
            {0.0f, y0, std::max(0.0f, x0), where.h},
            {x1, y0, std::max(0.0f, whole.w - x1), where.h},
          };
          for (const SDL_FRect& band : bands) {
            if (band.w > 0.0f && band.h > 0.0f) SDL_RenderFillRect(ren, &band);
          }
          SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }
      } else if (r.w <= 0 || r.h <= 0 ||
                 !cpuMaskCut(*rt, *cueMask, r.w, r.h, MaskUse::Cut, 1.0, r, /*alphaToo=*/true,
                             deckboy::masks::maskValueOutside(*cueMask), frame)) {
        warnMaskCannotCut();
      }
    }
    SDL_SetRenderTarget(ren, previous);
    // Laid on with the blend the crossfader asked for, premultiplied. Even a
    // failed cut is laid on: the layer must not vanish because its mask did.
    if (!SDL_SetTextureBlendMode(scratch, premultipliedBlendFor(maskCapturedMode_))) {
      SDL_SetTextureBlendMode(scratch, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    }
    SDL_SetTextureAlphaMod(scratch, 255);
    SDL_RenderTexture(ren, scratch, nullptr, &whole);
    return true;
  }

  // The output's own mask over the whole finished picture. On an opaque
  // compositor it darkens the colour and leaves alpha alone: the window lays
  // the compositor on black with its alpha, and cutting both applied the cut
  // twice (a 25% mask came out at 44%). Where the compositor carries a key
  // (`keyed`: cleared to clear for key + fill) the alpha IS the cut.
  void applyOutputMask(int outputIndex, int compositionIndex, OutputRuntime& rt, int renderW, int renderH,
                       bool keyed) {
    const int source = compositionIndex >= 0 ? compositionIndex : outputIndex;
    if (source < 0 || source >= static_cast<int>(project_.outputs.size())) return;
    const MaskModel* mask = findMask(project_.outputs[static_cast<std::size_t>(source)].maskId);
    if (!mask || !rt.compositorTexture) return;
    SDL_Texture* tex = maskTextureFor(rt, *mask, renderW, renderH, MaskUse::Cut);
    if (maskBlendSupported(tex, keyed ? maskCutBlend() : maskMatteBlend())) {
      const SDL_FRect whole {0.0f, 0.0f, static_cast<float>(renderW), static_cast<float>(renderH)};
      SDL_RenderTexture(rt.outputRenderer, tex, nullptr, &whole);
      return;
    }
    const SDL_Rect frame {0, 0, renderW, renderH};
    if (!cpuMaskCut(rt, *mask, renderW, renderH, MaskUse::Cut, 1.0, frame, keyed, 1.0f, frame)) warnMaskCannotCut();
  }

  // The matte slot holding a mask. True when it does, whether or not it drew.
  bool drawMaskMatte(int outputIndex, OutputRuntime& rt, int renderW, int renderH) {
    const OutputTarget& out = project_.outputs[static_cast<std::size_t>(outputIndex)];
    if (out.matteAspect.rfind("mask:", 0) != 0) return false;
    const MaskModel* mask = findMask(out.matteAspect.substr(5));
    if (!mask || out.matteOpacity <= 0.001) return true;
    SDL_Texture* tex = maskTextureFor(rt, *mask, renderW, renderH, MaskUse::Matte, out.matteOpacity);
    if (maskBlendSupported(tex, maskMatteBlend())) {
      const SDL_FRect whole {0.0f, 0.0f, static_cast<float>(renderW), static_cast<float>(renderH)};
      SDL_RenderTexture(rt.outputRenderer, tex, nullptr, &whole);
      return true;
    }
    const SDL_Rect frame {0, 0, renderW, renderH};
    if (!cpuMaskCut(rt, *mask, renderW, renderH, MaskUse::Matte, out.matteOpacity, frame, false, 1.0f, frame)) {
      warnMaskCannotCut();
    }
    return true;
  }

  // ── EXPORT ────────────────────────────────────────────────────────────────

  // PNG: a greyscale matte, white keeps. SVG: the paths (core/masks.hpp).
  // The mask as SET -- its oscillators at rest -- because an export is a
  // file, not a moment.
  bool exportMask(const MaskModel& m, const std::string& path, int w, int h, std::string& error) {
    std::string ext = toLower(fs::path(path).extension().string());
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
      error = "size out of range";
      return false;
    }
    if (ext == ".svg") {
      std::ofstream file(fs::path(path), std::ios::binary | std::ios::trunc);
      if (!file) {
        error = "cannot write " + path;
        return false;
      }
      file << deckboy::masks::maskToSvg(m, w, h);
      return static_cast<bool>(file);
    }
    if (ext == ".png") {
      const std::vector<std::uint8_t> alpha = deckboy::masks::rasterizeMask(m, w, h);
      // Encoded to memory (stb is built without stdio here) and written
      // through fs::path, which takes a UTF-8 name on every platform.
      std::string png;
      stbi_write_png_to_func(
        [](void* sink, void* data, int size) {
          static_cast<std::string*>(sink)->append(static_cast<const char*>(data), static_cast<std::size_t>(size));
        },
        &png, w, h, 1, alpha.data(), w);
      std::ofstream file(fs::path(path), std::ios::binary | std::ios::trunc);
      if (png.empty() || !file) {
        error = "cannot write " + path;
        return false;
      }
      file.write(png.data(), static_cast<std::streamsize>(png.size()));
      return static_cast<bool>(file);
    }
    error = "export as .png or .svg";
    return false;
  }

  // ── THE NETWORK VERB ──────────────────────────────────────────────────────
  //
  // MASK LIST | SHAPES
  // MASK NEW [shape] [name...]               the new id comes back
  // MASK SHAPE <id> <shape> [SUB]            another path from the library
  // MASK PATH <id> [SUB] x,y[,ix,iy,ox,oy] ...   a path, in % of the frame
  // MASK CLEAR <id> | DELETE <id> | RENAME <id> <name...>
  // MASK SET <id> INVERT on|off | FEATHER <%h> | GROW <%h> | OPACITY <%>
  //               | X <%w> | Y <%h> | SCALE <%> | ROTATION <deg>
  // MASK LFO <id> <param> ON|OFF [rate Hz] [depth %]
  // MASK ASSIGN <id|NONE> CUE | LAYER <output> <playlist> | OUTPUT <n> | MATTE <n>
  // MASK EXPORT <id> <file.png|file.svg> [WxH]     (this computer only)
  void handleMaskCommand(const std::vector<std::string>& parts) {
    using namespace deckboy::masks;
    const std::string sub = parts.size() > 1 ? toUpper(parts[1]) : std::string("LIST");
    auto number = [&](std::size_t at, double& out) {
      if (at >= parts.size()) return false;
      char* end = nullptr;
      out = std::strtod(parts[at].c_str(), &end);
      return end && *end == '\0' && std::isfinite(out);
    };
    auto maskArg = [&](std::size_t at) -> MaskModel* {
      MaskModel* m = at < parts.size() ? findMask(parts[at]) : nullptr;
      if (!m) failRemoteCommand("MASK " + sub + ": no mask " + (at < parts.size() ? parts[at] : std::string("given")));
      return m;
    };
    if (sub == "LIST") {
      std::ostringstream out;
      for (const MaskModel& m : project_.masks) {
        out << m.id << '\t' << m.name << '\t' << m.paths.size() << " paths\t" << countMaskUses(m.id) << " uses\n";
      }
      remoteCommandDetail_ = project_.masks.empty() ? std::string("no masks") : out.str();
      return;
    }
    if (sub == "SHAPES") {
      std::string list;
      for (int i = 0; i < static_cast<int>(MaskShape::Count); ++i) {
        list += std::string(i ? " " : "") + maskShapeToken(static_cast<MaskShape>(i));
      }
      remoteCommandDetail_ = list;
      return;
    }
    if (sub == "NEW") {
      MaskShape shape = MaskShape::Circle;
      std::size_t nameAt = 2;
      if (parts.size() > 2 && maskShapeFromToken(parts[2], shape)) nameAt = 3;
      pushUndoSnapshot();
      MaskModel m;
      m.id = nextMaskId();
      m.name = nameAt < parts.size() ? joinParts(parts, nameAt) : std::string(maskShapeLabel(shape));
      m.paths.push_back(makeMaskShape(shape, outputAspectFor(project_.focusedOutputIndex)));
      project_.masks.push_back(m);
      markProjectDirty();
      remoteCommandDetail_ = m.id;
      return;
    }
    if (sub == "SHAPE") {
      MaskModel* m = maskArg(2);
      if (!m) return;
      MaskShape shape;
      if (parts.size() < 4 || !maskShapeFromToken(parts[3], shape)) {
        failRemoteCommand("MASK SHAPE: expected a shape (MASK SHAPES lists them)");
        return;
      }
      pushUndoSnapshot();
      MaskPath path = makeMaskShape(shape, outputAspectFor(project_.focusedOutputIndex));
      path.subtract = parts.size() > 4 && toUpper(parts[4]) == "SUB";
      m->paths.push_back(path);
      markProjectDirty();
      return;
    }
    if (sub == "PATH") {
      MaskModel* m = maskArg(2);
      if (!m) return;
      MaskPath path;
      path.name = "path";
      std::size_t at = 3;
      if (at < parts.size() && toUpper(parts[at]) == "SUB") {
        path.subtract = true;
        ++at;
      }
      for (; at < parts.size(); ++at) {
        std::vector<double> v;
        for (const std::string& bit : splitByChar(parts[at], ',')) {
          char* end = nullptr;
          const double d = std::strtod(bit.c_str(), &end);
          if (!end || *end != '\0' || !std::isfinite(d)) {
            failRemoteCommand("MASK PATH: " + parts[at] + " is not x,y or x,y,ix,iy,ox,oy");
            return;
          }
          v.push_back(d / 100.0);
        }
        if (v.size() != 2 && v.size() != 6) {
          failRemoteCommand("MASK PATH: " + parts[at] + " is not x,y or x,y,ix,iy,ox,oy");
          return;
        }
        MaskPoint p;
        p.x = static_cast<float>(v[0]);
        p.y = static_cast<float>(v[1]);
        if (v.size() == 6) {
          p.inX = static_cast<float>(v[2]); p.inY = static_cast<float>(v[3]);
          p.outX = static_cast<float>(v[4]); p.outY = static_cast<float>(v[5]);
        }
        path.points.push_back(p);
      }
      if (path.points.size() < 3) {
        failRemoteCommand("MASK PATH: a path needs at least three points");
        return;
      }
      pushUndoSnapshot();
      m->paths.push_back(path);
      markProjectDirty();
      return;
    }
    if (sub == "CLEAR" || sub == "DELETE") {
      MaskModel* m = maskArg(2);
      if (!m) return;
      pushUndoSnapshot();
      if (sub == "CLEAR") m->paths.clear();
      else deleteMask(m->id);
      markProjectDirty();
      return;
    }
    if (sub == "RENAME") {
      MaskModel* m = maskArg(2);
      if (!m) return;
      if (parts.size() < 4) {
        failRemoteCommand("MASK RENAME: expected a name");
        return;
      }
      m->name = joinParts(parts, 3);
      markProjectDirty();
      return;
    }
    // Operator units in, stored units out. Out of range is an error, never a
    // silent clamp.
    struct Param {
      const char* word;
      int slot;
      double scale;   // operator unit -> stored
    };
    static const Param kParams[] = {
      {"X", kMaskLfoX, 0.01},          {"Y", kMaskLfoY, 0.01},
      {"SCALE", kMaskLfoScale, 0.01},  {"ROTATION", kMaskLfoRotation, 1.0},
      {"FEATHER", kMaskLfoFeather, 0.01}, {"GROW", kMaskLfoGrow, 0.01},
      {"OPACITY", kMaskLfoOpacity, 0.01},
    };
    auto findParam = [&](const std::string& word) -> const Param* {
      const std::string w = toUpper(word);
      for (const Param& p : kParams) {
        if (w == p.word || (w == "ROTATE" && p.slot == kMaskLfoRotation)) return &p;
      }
      return nullptr;
    };
    if (sub == "SET") {
      MaskModel* m = maskArg(2);
      if (!m) return;
      if (parts.size() < 5) {
        failRemoteCommand("MASK SET: expected <id> <param> <value>");
        return;
      }
      if (toUpper(parts[3]) == "INVERT") {
        const std::string v = toUpper(parts[4]);
        m->invert = v == "ON" || v == "1" || v == "TRUE" || (v == "TOGGLE" && !m->invert);
        markProjectDirty();
        return;
      }
      const Param* p = findParam(parts[3]);
      double value = 0.0;
      if (!p || !number(4, value)) {
        failRemoteCommand("MASK SET: expected INVERT, X, Y, SCALE, ROTATION, FEATHER, GROW or OPACITY and a number");
        return;
      }
      const MaskParamRange range = maskParamRange(p->slot);
      const double stored = value * p->scale;
      if (stored < range.lo - 1e-6 || stored > range.hi + 1e-6) {
        std::ostringstream why;
        why << "MASK SET " << p->word << ": " << value << " is outside " << range.lo / p->scale << " to "
            << range.hi / p->scale;
        failRemoteCommand(why.str());
        return;
      }
      maskParam(*m, p->slot) = static_cast<float>(stored);
      markProjectDirty();
      return;
    }
    if (sub == "LFO") {
      MaskModel* m = maskArg(2);
      if (!m) return;
      const Param* p = parts.size() > 3 ? findParam(parts[3]) : nullptr;
      if (!p || parts.size() < 5) {
        failRemoteCommand("MASK LFO: expected <id> <param> ON|OFF [rate Hz] [depth %]");
        return;
      }
      auto& lfo = m->lfo[static_cast<std::size_t>(p->slot)];
      lfo.on = toUpper(parts[4]) == "ON";
      double v = 0.0;
      if (number(5, v)) lfo.rateHz = static_cast<float>(std::clamp(v, 0.01, 30.0));
      if (number(6, v)) lfo.depth = static_cast<float>(std::clamp(v / 100.0, 0.0, 1.0));
      markProjectDirty();
      return;
    }
    if (sub == "ASSIGN") {
      if (parts.size() < 4) {
        failRemoteCommand("MASK ASSIGN: expected <id|NONE> CUE | LAYER <output> <playlist> | OUTPUT <n> | MATTE <n>");
        return;
      }
      const bool none = toUpper(parts[2]) == "NONE";
      const MaskModel* m = none ? nullptr : maskArg(2);
      if (!none && !m) return;
      const std::string id = m ? m->id : std::string();
      const std::string where = toUpper(parts[3]);
      auto outputArg = [&](std::size_t at, int& index) {
        double v = 0.0;
        if (!number(at, v) || v < 1 || v > static_cast<double>(project_.outputs.size())) {
          failRemoteCommand("MASK ASSIGN " + where + ": no output " + (at < parts.size() ? parts[at] : std::string()));
          return false;
        }
        index = static_cast<int>(v) - 1;
        return true;
      };
      if (where == "CUE") {
        pushUndoSnapshot();
        bool any = false;
        forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
          cue.maskId = id;
          any = true;
        });
        if (!any) {
          failRemoteCommand("MASK ASSIGN CUE: no cue selected");
          return;
        }
      } else if (where == "OUTPUT" || where == "MATTE") {
        int index = 0;
        if (!outputArg(4, index)) return;
        pushUndoSnapshot();
        OutputTarget& out = project_.outputs[static_cast<std::size_t>(index)];
        if (where == "OUTPUT") out.maskId = id;
        else out.matteAspect = id.empty() ? std::string("off") : "mask:" + id;
      } else if (where == "LAYER") {
        int index = 0;
        double deck = 0.0;
        if (!outputArg(4, index)) return;
        if (!number(5, deck) || deck < 1 || deck > static_cast<double>(project_.decks.size())) {
          failRemoteCommand("MASK ASSIGN LAYER: no playlist " + (parts.size() > 5 ? parts[5] : std::string()));
          return;
        }
        const int deckIndex = static_cast<int>(deck) - 1;
        OutputTarget& out = project_.outputs[static_cast<std::size_t>(index)];
        OutputLayer* layer = nullptr;
        for (OutputLayer& candidate : out.layerDecks) {
          if (candidate.deckIndex == deckIndex) layer = &candidate;
        }
        pushUndoSnapshot();
        if (layer) {
          layer->maskId = id;
        } else if (stackPositionFor(index, deckIndex) == 0) {
          out.baseLayerMaskId = id;
        } else {
          failRemoteCommand("MASK ASSIGN LAYER: playlist " + parts[5] + " is not on output " + parts[4]);
          return;
        }
      } else {
        failRemoteCommand("MASK ASSIGN: expected CUE, LAYER, OUTPUT or MATTE");
        return;
      }
      markProjectDirty();
      return;
    }
    // IMPORT <file.svg> [name]: a new mask from an SVG's filled shapes, on the
    // target MASK ON names (this computer only: it reads a file).
    if (sub == "IMPORT") {
      if (parts.size() < 3) {
        failRemoteCommand("MASK IMPORT: expected <file.svg> [name]");
        return;
      }
      std::string error;
      const MaskModel* m = importMaskSvg(parts[2], parts.size() > 3 ? joinParts(parts, 3) : std::string(), error);
      if (!m) {
        failRemoteCommand("MASK IMPORT: " + error);
        return;
      }
      remoteCommandDetail_ = m->id + " " + m->name + " paths=" + std::to_string(m->paths.size()) +
                             (m->invert ? " invert" : "");
      return;
    }
    if (sub == "EXPORT") {
      const MaskModel* m = maskArg(2);
      if (!m) return;
      if (parts.size() < 4) {
        failRemoteCommand("MASK EXPORT: expected <id> <file.png|file.svg> [WxH]");
        return;
      }
      auto [w, h] = outputRenderSizeForOutput(std::clamp(project_.focusedOutputIndex, 0,
                                                         std::max(0, static_cast<int>(project_.outputs.size()) - 1)));
      if (parts.size() > 4) {
        const auto x = toLower(parts[4]).find('x');
        if (x == std::string::npos) {
          failRemoteCommand("MASK EXPORT: size is WxH, e.g. 1920x1080");
          return;
        }
        w = std::atoi(parts[4].substr(0, x).c_str());
        h = std::atoi(parts[4].substr(x + 1).c_str());
      }
      std::string error;
      if (!exportMask(*m, parts[3], w, h, error)) failRemoteCommand("MASK EXPORT: " + error);
      else remoteCommandDetail_ = parts[3];
      return;
    }
    // INFO <id>: the controls in the units SET takes, and which oscillators run.
    if (sub == "INFO") {
      const MaskModel* m = maskArg(2);
      if (!m) return;
      std::ostringstream out;
      out << "name=" << m->name << " paths=" << m->paths.size() << " invert=" << (m->invert ? "on" : "off");
      for (const Param& p : kParams) {
        out << ' ' << toLower(p.word) << '=' << maskParam(*m, p.slot) / p.scale;
      }
      out << " lfo=";
      bool any = false;
      for (const Param& p : kParams) {
        if (!m->lfo[static_cast<std::size_t>(p.slot)].on) continue;
        out << (any ? "," : "") << toLower(p.word);
        any = true;
      }
      if (!any) out << "none";
      remoteCommandDetail_ = out.str();
      return;
    }
    failRemoteCommand("MASK: expected LIST, SHAPES, INFO, NEW, SHAPE, PATH, SET, LFO, ASSIGN, EXPORT, CLEAR, DELETE or RENAME");
  }
