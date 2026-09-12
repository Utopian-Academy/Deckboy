// ============================================================================
// app_render_output.ipp — Output window compositor and egress rendering.
//
// Renders the composited deck output to output windows and external sinks:
//
//   presentOutputCompositorToWindow() — blit compositor texture to SDL window
//     Handles Area of Interest (AOI) cropping, perspective warp (corner-pin),
//     edge blending (soft overlap zones), and scan line overlay.
//
//   renderOutputWindow()    — full output rendering pipeline:
//     1. Ensure compositor texture is sized to output resolution
//     2. Clear to black, render all deck layers
//     3. Render audio visualization for audio-only cues
//     4. Render lower-third overlays
//     5. Render timecode overlay
//     6. Apply master dimmer
//     7. Capture frame for streaming/NDI/DeckLink sinks
//     8. Present to SDL window
//
//   captureCompositorPixels() — read back compositor pixels for stream/NDI
//   renderMonitorTile()       — render a scaled preview in the monitors window
//
// Part of class App — included inside the class body in main.cpp.
// Do NOT compile this file separately.
// ============================================================================

  // Present the compositor texture to an output window, applying AOI crop,
  // perspective warp, edge blending, and scan line overlay as configured.
  void presentOutputCompositorToWindow(int outputIndex, int windowW, int windowH) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer || !runtime->compositorTexture) {
      return;
    }
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    int hostDeckIndex = std::clamp(output.hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    const Deck& deck = project_.decks[hostDeckIndex];
    int texW = runtime->compositorWidth;
    int texH = runtime->compositorHeight;
    if (texW <= 0 || texH <= 0 || windowW <= 0 || windowH <= 0) {
      return;
    }

    // Area of Interest: if any AOI crop is set, use that region of the compositor
    // (scaled to fill the full output window). Otherwise use canvas/span offset.
    float aoiL = std::clamp(output.aoiLeft,   0.0f, 0.95f);
    float aoiR = std::clamp(output.aoiRight,  0.0f, 0.95f);
    float aoiT = std::clamp(output.aoiTop,    0.0f, 0.95f);
    float aoiB = std::clamp(output.aoiBottom, 0.0f, 0.95f);
    bool hasAoi = aoiL > 0.001f || aoiR > 0.001f || aoiT > 0.001f || aoiB > 0.001f;

    SDL_Rect src;
    if (hasAoi) {
      int aoiX = static_cast<int>(aoiL * texW);
      int aoiY = static_cast<int>(aoiT * texH);
      int aoiW = std::max(1, static_cast<int>((1.0f - aoiL - aoiR) * texW));
      int aoiH = std::max(1, static_cast<int>((1.0f - aoiT - aoiB) * texH));
      src = {aoiX, aoiY, aoiW, aoiH};
    } else {
      src = {0, 0, std::min(windowW, texW), std::min(windowH, texH)};
      std::string layoutMode = normalizeOutputLayoutMode(output.outputLayoutMode);
      if (project_.outputCanvasEnabled && layoutMode == "span") {
        src.x = std::clamp(deck.canvasViewX, 0, std::max(0, texW - src.w));
        src.y = std::clamp(deck.canvasViewY, 0, std::max(0, texH - src.h));
      }
    }

    bool hasBlend = deck.edgeBlendLeft > 0.0001f || deck.edgeBlendRight > 0.0001f
      || deck.edgeBlendTop > 0.0001f || deck.edgeBlendBottom > 0.0001f;
    bool hasWarp = deck.warpEnabled;
    std::string warpMode = normalizeWarpMode(deck.warpMode);
    bool usePerspectiveWarp = hasWarp && warpMode == "perspective";
    int orientationDegrees = normalizeOutputOrientationDegrees(output.outputOrientationDegrees);
    bool hasOrientation = orientationDegrees != 0;

#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (hasWarp || hasBlend || hasOrientation) {
      float u0 = static_cast<float>(src.x) / static_cast<float>(texW);
      float v0 = static_cast<float>(src.y) / static_cast<float>(texH);
      float u1 = static_cast<float>(src.x + src.w) / static_cast<float>(texW);
      float v1 = static_cast<float>(src.y + src.h) / static_cast<float>(texH);

      SDL_FPoint uvTL {u0, v0};
      SDL_FPoint uvTR {u1, v0};
      SDL_FPoint uvBR {u1, v1};
      SDL_FPoint uvBL {u0, v1};
      if (orientationDegrees == 90) {
        uvTL = SDL_FPoint {u0, v1};
        uvTR = SDL_FPoint {u0, v0};
        uvBR = SDL_FPoint {u1, v0};
        uvBL = SDL_FPoint {u1, v1};
      } else if (orientationDegrees == 180) {
        uvTL = SDL_FPoint {u1, v1};
        uvTR = SDL_FPoint {u0, v1};
        uvBR = SDL_FPoint {u0, v0};
        uvBL = SDL_FPoint {u1, v0};
      } else if (orientationDegrees == 270) {
        uvTL = SDL_FPoint {u1, v0};
        uvTR = SDL_FPoint {u1, v1};
        uvBR = SDL_FPoint {u0, v1};
        uvBL = SDL_FPoint {u0, v0};
      }

      SDL_FPoint p0 {0.0f, 0.0f};
      SDL_FPoint p1 {static_cast<float>(windowW), 0.0f};
      SDL_FPoint p2 {static_cast<float>(windowW), static_cast<float>(windowH)};
      SDL_FPoint p3 {0.0f, static_cast<float>(windowH)};
      if (hasWarp) {
        p0.x += deck.warpTopLeftX;      p0.y += deck.warpTopLeftY;
        p1.x += deck.warpTopRightX;     p1.y += deck.warpTopRightY;
        p2.x += deck.warpBottomRightX;  p2.y += deck.warpBottomRightY;
        p3.x += deck.warpBottomLeftX;   p3.y += deck.warpBottomLeftY;
      }

      if (usePerspectiveWarp) {
        if (renderPerspectiveWarp(runtime->outputRenderer, runtime->compositorTexture, deck,
                                  uvTL, uvTR, uvBR, uvBL, p0, p1, p2, p3, hasBlend)) {
          return;
        }
      }

      // Feathering must be evaluated ACROSS the quad, not at its corners: four
      // vertices let SDL stretch a narrow edge ramp into a full-image fade.
      if (hasBlend && renderFeatheredQuad(runtime->outputRenderer, runtime->compositorTexture,
                                          deck, uvTL, uvTR, uvBR, uvBL, p0, p1, p2, p3)) {
        return;
      }
      // No feather (warp and/or orientation only): a plain opaque quad is exact.
      // SDL3: SDL_Vertex carries a float SDL_FColor.
      const SDL_FColor kOpaque {1.0f, 1.0f, 1.0f, 1.0f};
      SDL_Vertex verts[4] {
        {p0, kOpaque, uvTL},
        {p1, kOpaque, uvTR},
        {p2, kOpaque, uvBR},
        {p3, kOpaque, uvBL},
      };
      const int indices[6] {0, 1, 2, 0, 2, 3};
      SDL_SetTextureBlendMode(runtime->compositorTexture, SDL_BLENDMODE_BLEND);
      if (SDL_RenderGeometry(runtime->outputRenderer, runtime->compositorTexture, verts, 4, indices, 6)) {
        return;
      }
    }
#endif

    if (hasOrientation) {
      SDL_RenderTextureRotated(runtime->outputRenderer, runtime->compositorTexture, &src, nullptr,
                       static_cast<double>(orientationDegrees), nullptr, SDL_FLIP_NONE);
    } else {
      SDL_RenderTexture(runtime->outputRenderer, runtime->compositorTexture, &src, nullptr);
    }
  }

  // Get-or-create the per-deck bridge texture at this output. Recreates the
  // texture when width, height, OR SDL pixel format changes — needed because
  // a cue switch can flip the source frame between RGBA32 and NV12, and an
  // NV12 sampler cannot be fed RGBA bytes (or vice versa).
  SDL_Texture* ensureLayerBridgeTexture(OutputRuntime& outputRuntime,
                                        int sourceDeckIndex,
                                        int width,
                                        int height,
                                        Uint32 format) {
    if (width <= 0 || height <= 0) {
      return nullptr;
    }
    auto texIt = outputRuntime.layerBridgeTextures.find(sourceDeckIndex);
    bool needsRecreate = texIt == outputRuntime.layerBridgeTextures.end();
    if (!needsRecreate) {
      int prevW = outputRuntime.layerBridgeTextureWidths[sourceDeckIndex];
      int prevH = outputRuntime.layerBridgeTextureHeights[sourceDeckIndex];
      Uint32 prevFmt = outputRuntime.layerBridgeTextureFormats[sourceDeckIndex];
      needsRecreate = prevW != width || prevH != height || prevFmt != format;
    }
    if (needsRecreate) {
      if (texIt != outputRuntime.layerBridgeTextures.end() && texIt->second) {
        SDL_DestroyTexture(texIt->second);
      }
      SDL_Texture* texture = deckboyCreateTexture(
        outputRuntime.outputRenderer,
        format,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
      );
      if (!texture) {
        outputRuntime.layerBridgeTextures.erase(sourceDeckIndex);
        outputRuntime.layerBridgeTextureWidths.erase(sourceDeckIndex);
        outputRuntime.layerBridgeTextureHeights.erase(sourceDeckIndex);
        outputRuntime.layerBridgeTextureFormats.erase(sourceDeckIndex);
        outputRuntime.layerBridgeFrameIndices.erase(sourceDeckIndex);
        outputRuntime.layerBridgeCueKeys.erase(sourceDeckIndex);
        return nullptr;
      }
      SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
      outputRuntime.layerBridgeTextures[sourceDeckIndex] = texture;
      outputRuntime.layerBridgeTextureWidths[sourceDeckIndex] = width;
      outputRuntime.layerBridgeTextureHeights[sourceDeckIndex] = height;
      outputRuntime.layerBridgeTextureFormats[sourceDeckIndex] = format;
      outputRuntime.layerBridgeFrameIndices.erase(sourceDeckIndex);
      outputRuntime.layerBridgeCueKeys.erase(sourceDeckIndex);
      return texture;
    }
    return texIt->second;
  }

#if DECKBOY_INPROC_DECODE
  // Get-or-create the per-deck zero-copy bridge: a persistent NV12 or P010
  // ID3D11Texture2D on this output renderer's device, wrapped once as an
  // SDL_Texture. Decoded d3d11va texture-array slices are GPU-copied into it
  // each frame advance — no CPU download, no re-upload. `format` is the
  // decoded surface's layout and must be carried through: taking a 10-bit cue
  // after an 8-bit one has to rebuild the wrap, exactly as a size change does.
  SDL_Texture* ensureLayerGpuTexture(OutputRuntime& outputRuntime,
                                     int sourceDeckIndex,
                                     int width,
                                     int height,
                                     FramePixelFormat format) {
    width &= ~1;
    height &= ~1;
    if (width <= 0 || height <= 0) {
      return nullptr;
    }
    auto texIt = outputRuntime.layerGpuTextures.find(sourceDeckIndex);
    if (texIt != outputRuntime.layerGpuTextures.end()) {
      auto sizeIt = outputRuntime.layerGpuTextureSizes.find(sourceDeckIndex);
      auto fmtIt = outputRuntime.layerGpuTextureFormats.find(sourceDeckIndex);
      if (sizeIt != outputRuntime.layerGpuTextureSizes.end() &&
          sizeIt->second == std::make_pair(width, height) &&
          fmtIt != outputRuntime.layerGpuTextureFormats.end() &&
          fmtIt->second == format) {
        return texIt->second;
      }
      if (texIt->second) {
        SDL_DestroyTexture(texIt->second);
      }
      auto d3dIt = outputRuntime.layerGpuTexture2Ds.find(sourceDeckIndex);
      if (d3dIt != outputRuntime.layerGpuTexture2Ds.end()) {
        deckboy::libav::releaseD3D11Texture(d3dIt->second);
        outputRuntime.layerGpuTexture2Ds.erase(d3dIt);
      }
      outputRuntime.layerGpuTextures.erase(texIt);
      outputRuntime.layerGpuTextureSizes.erase(sourceDeckIndex);
      outputRuntime.layerGpuTextureFormats.erase(sourceDeckIndex);
      outputRuntime.layerGpuFrameIndices.erase(sourceDeckIndex);
    }
    void* texture2D = nullptr;
    SDL_Texture* wrapped = deckboy::libav::createWrappedVideoTexture(
      outputRuntime.outputRenderer, width, height, format, &texture2D);
    if (!wrapped) {
      return nullptr;
    }
    outputRuntime.layerGpuTextures[sourceDeckIndex] = wrapped;
    outputRuntime.layerGpuTexture2Ds[sourceDeckIndex] = texture2D;
    outputRuntime.layerGpuTextureSizes[sourceDeckIndex] = {width, height};
    outputRuntime.layerGpuTextureFormats[sourceDeckIndex] = format;
    outputRuntime.layerGpuFrameIndices.erase(sourceDeckIndex);
    return wrapped;
  }
#endif

  // Get-or-create the per-overlay bridge texture. Same format-aware rebuild
  // rule as ensureLayerBridgeTexture, keyed by overlay identity instead of
  // deck index.
  // A RENDER TARGET of the given size, cached per key on the runtime. The
  // bridge texture beside this is STATIC-access and is uploaded into; this one
  // is drawn into, which needs TARGET access and so cannot share it.
  SDL_Texture* ensureOverlayBridgeTargetTexture(OutputRuntime& outputRuntime,
                                                const std::string& key,
                                                int width, int height) {
    if (!outputRuntime.outputRenderer || width <= 0 || height <= 0) {
      return nullptr;
    }
    auto& slot = outputRuntime.overlayBridgeTargets[key];
    if (slot.texture && (slot.width != width || slot.height != height)) {
      SDL_DestroyTexture(slot.texture);
      slot.texture = nullptr;
    }
    if (!slot.texture) {
      slot.texture = deckboyCreateTexture(outputRuntime.outputRenderer,
                                          SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_TARGET, width, height);
      slot.width = width;
      slot.height = height;
    }
    return slot.texture;
  }

  SDL_Texture* ensureOverlayBridgeTexture(OutputRuntime& outputRuntime,
                                          const std::string& overlayKey,
                                          int width,
                                          int height,
                                          Uint32 format) {
    if (width <= 0 || height <= 0) {
      return nullptr;
    }
    auto texIt = outputRuntime.overlayBridgeTextures.find(overlayKey);
    bool needsRecreate = texIt == outputRuntime.overlayBridgeTextures.end();
    if (!needsRecreate) {
      int prevW = outputRuntime.overlayBridgeTextureWidths[overlayKey];
      int prevH = outputRuntime.overlayBridgeTextureHeights[overlayKey];
      Uint32 prevFmt = outputRuntime.overlayBridgeTextureFormats[overlayKey];
      needsRecreate = prevW != width || prevH != height || prevFmt != format;
    }
    if (needsRecreate) {
      if (texIt != outputRuntime.overlayBridgeTextures.end() && texIt->second) {
        SDL_DestroyTexture(texIt->second);
      }
      SDL_Texture* texture = deckboyCreateTexture(
        outputRuntime.outputRenderer,
        format,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
      );
      if (!texture) {
        outputRuntime.overlayBridgeTextures.erase(overlayKey);
        outputRuntime.overlayBridgeTextureWidths.erase(overlayKey);
        outputRuntime.overlayBridgeTextureHeights.erase(overlayKey);
        outputRuntime.overlayBridgeTextureFormats.erase(overlayKey);
        outputRuntime.overlayBridgeFrameIndices.erase(overlayKey);
        outputRuntime.overlayBridgeCueKeys.erase(overlayKey);
        return nullptr;
      }
      SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
      outputRuntime.overlayBridgeTextures[overlayKey] = texture;
      outputRuntime.overlayBridgeTextureWidths[overlayKey] = width;
      outputRuntime.overlayBridgeTextureHeights[overlayKey] = height;
      outputRuntime.overlayBridgeTextureFormats[overlayKey] = format;
      outputRuntime.overlayBridgeFrameIndices.erase(overlayKey);
      outputRuntime.overlayBridgeCueKeys.erase(overlayKey);
      return texture;
    }
    return texIt->second;
  }

  void renderTextureWithCueGeometry(SDL_Renderer* renderer,
                                    SDL_Texture* texture,
                                    int textureWidth,
                                    int textureHeight,
                                    const Cue* cue,
                                    const SDL_Rect& target,
                                    // The caller's blend mode. Defaulted, so
                                    // every existing call site is unchanged --
                                    // but no longer forced, because this
                                    // function used to overwrite whatever the
                                    // caller had just set and that silently
                                    // discarded the VJ mixer's add and
                                    // multiply while dissolve appeared to work.
                                    SDL_BlendMode blendMode = SDL_BLENDMODE_BLEND) {
    if (!renderer || !texture || textureWidth <= 0 || textureHeight <= 0) {
      return;
    }
    float cropLeft = cue ? cue->cropLeft : 0.0f;
    float cropRight = cue ? cue->cropRight : 0.0f;
    float cropTop = cue ? cue->cropTop : 0.0f;
    float cropBottom = cue ? cue->cropBottom : 0.0f;
    int cropL = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureWidth) * cropLeft)), 0, textureWidth - 1);
    int cropR = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureWidth) * cropRight)), 0, textureWidth - 1);
    int cropT = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureHeight) * cropTop)), 0, textureHeight - 1);
    int cropB = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureHeight) * cropBottom)), 0, textureHeight - 1);
    int srcW = std::max(1, textureWidth - cropL - cropR);
    int srcH = std::max(1, textureHeight - cropT - cropB);
    SDL_Rect source {cropL, cropT, srcW, srcH};
    ScaleMode scaleMode = cue ? cue->scaleMode : ScaleMode::Fit;
    double baseScaleX = 1.0;
    double baseScaleY = 1.0;
    if (scaleMode == ScaleMode::Fit) {
      double fit = std::min(
        static_cast<double>(target.w) / static_cast<double>(srcW),
        static_cast<double>(target.h) / static_cast<double>(srcH)
      );
      baseScaleX = fit;
      baseScaleY = fit;
    } else if (scaleMode == ScaleMode::Fill) {
      double fill = std::max(
        static_cast<double>(target.w) / static_cast<double>(srcW),
        static_cast<double>(target.h) / static_cast<double>(srcH)
      );
      baseScaleX = fill;
      baseScaleY = fill;
    } else if (scaleMode == ScaleMode::Stretch) {
      baseScaleX = static_cast<double>(target.w) / static_cast<double>(srcW);
      baseScaleY = static_cast<double>(target.h) / static_cast<double>(srcH);
    }
    float outputScaleX = cue ? cue->outputScaleX : 1.0f;
    float outputScaleY = cue ? cue->outputScaleY : 1.0f;
    float offsetX = cue ? cue->outputOffsetX : 0.0f;
    float offsetY = cue ? cue->outputOffsetY : 0.0f;
    float rotationDegrees = cue ? cue->outputRotationDegrees : 0.0f;
    int drawW = std::max(1, static_cast<int>(std::round(srcW * baseScaleX * static_cast<double>(outputScaleX))));
    int drawH = std::max(1, static_cast<int>(std::round(srcH * baseScaleY * static_cast<double>(outputScaleY))));
    SDL_Rect destination {
      target.x + (target.w - drawW) / 2 + static_cast<int>(offsetX),
      target.y + (target.h - drawH) / 2 + static_cast<int>(offsetY),
      drawW,
      drawH
    };
    SDL_SetTextureBlendMode(texture, blendMode);
    // Clip to target so Fill/Unscaled modes don't overflow into other UI elements
    SDL_Rect prevClip;
    bool hadClip = SDL_RenderClipEnabled(renderer);
    if (hadClip) SDL_GetRenderClipRect(renderer, &prevClip);
    SDL_SetRenderClipRect(renderer, &target);
    // THE ONE PLACE THIS IS NOT A FLAT QUAD.
    //
    // A cue with the mesh armed is drawn as a displaced grid instead of a
    // rectangle: same texture, same destination, more vertices, and a height
    // taken from the picture. Falls back to the ordinary blit whenever the
    // brightness field is not available, so a mesh cue on a source that
    // cannot be sampled still shows its picture rather than nothing.
    // NOT filtered linearly. Setting SDL_SCALEMODE_LINEAR on this texture
    // for the mesh draw -- an obvious-looking fix for the stipple that
    // point sampling gives a warped surface -- made the whole layer render
    // BLACK. Measured: mesh off 16, mesh on 0. Left nearest until that is
    // understood; a stippled surface beats no surface.
    if (cue && cue->meshEnabled && cue->meshHeight > 0.001f &&
        renderDisplacementMesh(renderer, texture, destination,
                               meshLumaField_, meshLumaW_, meshLumaH_,
                               cue->meshHeight, cue->meshTiltX,
                               static_cast<float>(meshDriftYaw(*cue)),
                               cue->meshGrid, 1.0f)) {
      SDL_SetRenderClipRect(renderer, hadClip ? &prevClip : nullptr);
      return;
    }
    SDL_Point center {destination.w / 2, destination.h / 2};
    SDL_RenderTextureRotated(renderer, texture, &source, &destination, rotationDegrees, &center, SDL_FLIP_NONE);
    SDL_SetRenderClipRect(renderer, hadClip ? &prevClip : nullptr);
  }

  SDL_Rect compositeSlotRectForTarget(const CompositeSlot& slot, const SDL_Rect& target) const {
    int x = target.x + static_cast<int>(std::lround(slot.normX * static_cast<float>(target.w)));
    int y = target.y + static_cast<int>(std::lround(slot.normY * static_cast<float>(target.h)));
    int w = static_cast<int>(std::lround(slot.normW * static_cast<float>(target.w)));
    int h = static_cast<int>(std::lround(slot.normH * static_cast<float>(target.h)));
    w = std::max(24, std::min(w, target.w));
    h = std::max(24, std::min(h, target.h));
    if (x + w > target.x + target.w) {
      x = target.x + target.w - w;
    }
    if (y + h > target.y + target.h) {
      y = target.y + target.h - h;
    }
    x = std::max(target.x, x);
    y = std::max(target.y, y);
    return SDL_Rect {x, y, w, h};
  }

  void renderCompositeCuePlaceholder(SDL_Renderer* renderer,
                                     const SDL_Rect& target,
                                     const Cue& cue,
                                     bool liveContext) {
    if (!renderer || target.w <= 0 || target.h <= 0) {
      return;
    }
    SDL_Color background = cue.compositeBackgroundColor.a == 0
      ? SDL_Color {18, 24, 18, 255}
      : cue.compositeBackgroundColor;
    Primitives::fillRect(renderer, target, background);
    Primitives::strokeRect(renderer, target, pal.dark);

    static constexpr std::array<SDL_Color, 4> kSlotFills {{
      SDL_Color {139, 172, 15, 220},
      SDL_Color {104, 136, 15, 220},
      SDL_Color {72, 96, 16, 220},
      SDL_Color {48, 80, 24, 220},
    }};

    int drawnSlots = 0;
    for (size_t i = 0; i < cue.compositeSlots.size(); ++i) {
      const CompositeSlot& slot = cue.compositeSlots[i];
      if (!slot.visible) {
        continue;
      }
      SDL_Rect slotRect = compositeSlotRectForTarget(slot, target);
      SDL_Color fill = kSlotFills[i % kSlotFills.size()];
      SDL_Color stroke = pal.deep;
      Primitives::fillRect(renderer, slotRect, fill);
      Primitives::strokeRect(renderer, slotRect, stroke);
      if (slotRect.w > 2 && slotRect.h > 2) {
        Primitives::strokeRect(renderer, insetRect(slotRect, 1), pal.light);
      }

      SDL_Rect titleRect {slotRect.x + 6, slotRect.y + 6, slotRect.w - 12, 14};
      SDL_Rect typeRect {slotRect.x + 6, slotRect.y + 22, slotRect.w - 12, 12};
      SDL_Rect sourceRect {slotRect.x + 6, slotRect.y + 38, slotRect.w - 12, std::max(12, slotRect.h - 52)};
      drawTextSafe(renderer, fontSmall_, titleRect,
                   slot.name.empty() ? compositeSlotDefaultName(static_cast<int>(i)) : slot.name,
                   pal.deep);
      drawTextSafe(renderer, fontSmall_, typeRect,
                   compositeSourceTypeLabel(slot.sourceType),
                   pal.dark);
      drawTextSafe(renderer, fontSmall_, sourceRect,
                   compositeSourceDisplayLabel(slot),
                   pal.deep);
      ++drawnSlots;
    }

    if (drawnSlots == 0) {
      drawCenteredTextSafe(renderer, fontBase_, target,
                           "COMPOSITE CUE", pal.light);
      SDL_Rect hintRect {target.x + 18, target.y + target.h / 2 + 10, target.w - 36, 18};
      drawCenteredTextSafe(renderer, fontSmall_, hintRect,
                           "Add slot sources in the cue inspector", pal.mid);
    } else {
      SDL_Rect footerRect {target.x + 8, target.y + target.h - 22, target.w - 16, 14};
      std::string footer = std::string(liveContext ? "LIVE " : "PREVIEW ")
        + "COMPOSITE · " + compositeLayoutPresetLabel(cue.compositeLayoutPreset);
      drawTextSafe(renderer, fontSmall_, footerRect, footer, pal.light);
    }
  }

  // How much of this deck the crossfader is letting through, and how it
  // combines with what is under it.
  //
  // Both decks fade rather than only the incoming one, because they are drawn
  // over black: holding A at full until B covered it would be a wipe, not a
  // dissolve. Add and multiply are ways of COMBINING two pictures, so there
  // the base stays at full and only the incoming deck rides the fader.
  double vjLayerGain(int deckIndex, SDL_BlendMode& blendOut) const {
    blendOut = SDL_BLENDMODE_BLEND;
    if (!project_.vjModeEnabled || project_.decks.size() < 2) {
      return 1.0;
    }
    const int deckCount = static_cast<int>(project_.decks.size());
    const int deckA = std::clamp(project_.vjDeckA, 0, deckCount - 1);
    const int deckB = std::clamp(project_.vjDeckB, 0, deckCount - 1);
    if (deckA == deckB) {
      return 1.0;
    }
    const double mix = std::clamp(project_.vjMixPosition, 0.0, 1.0);
    const std::string& mode = project_.vjBlendMode;
    // Only DISSOLVE fades A out as B comes in. Every other mode leaves A at
    // full and brings B in over it, which is what makes them look like
    // themselves rather than like a crossfade wearing a costume.
    const bool dissolve = mode == "dissolve";
    if (deckIndex == deckB) {
      blendOut = vjBlendModeFor(mode);
      return mix;
    }
    if (deckIndex == deckA) {
      return dissolve ? (1.0 - mix) : 1.0;
    }
    return 1.0;
  }

  // The blend the B deck is drawn with.
  //
  // There were three modes: dissolve, add, and a "multiply" that used
  // SDL_BLENDMODE_MOD -- which ignores the source alpha entirely, so it
  // snapped to full the moment the fader left zero instead of mixing in. The
  // rest are composed from SDL's blend factors and operations, which is enough
  // for the classic set and for several that a video mixer rarely offers:
  // MINIMUM and MAXIMUM blending are in every paint program and almost no VJ
  // desk.
  //
  // Composed once each and cached: SDL_ComposeCustomBlendMode is cheap but
  // this runs per deck per output per frame.
  static SDL_BlendMode vjBlendModeFor(const std::string& mode) {
    auto compose = [](SDL_BlendFactor srcF, SDL_BlendFactor dstF,
                      SDL_BlendOperation op) {
      // Alpha is left alone -- these are colour operations, and letting the
      // operation loose on alpha is what turns "darken" into "vanish".
      return SDL_ComposeCustomBlendMode(srcF, dstF, op,
                                        SDL_BLENDFACTOR_ONE,
                                        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                        SDL_BLENDOPERATION_ADD);
    };
    // src is scaled by its own alpha throughout, so every mode still RESPONDS
    // TO THE FADER instead of snapping on at the first touch.
    static const SDL_BlendMode kScreen = compose(
      SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR, SDL_BLENDFACTOR_ONE,
      SDL_BLENDOPERATION_ADD);
    static const SDL_BlendMode kMultiply = compose(
      SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      SDL_BLENDOPERATION_ADD);
    static const SDL_BlendMode kLighten = compose(
      SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE,
      SDL_BLENDOPERATION_MAXIMUM);
    static const SDL_BlendMode kDarken = compose(
      SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE,
      SDL_BLENDOPERATION_MINIMUM);
    static const SDL_BlendMode kSubtract = compose(
      SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE,
      SDL_BLENDOPERATION_SUBTRACT);
    static const SDL_BlendMode kUndercut = compose(
      SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE,
      SDL_BLENDOPERATION_REV_SUBTRACT);
    // B is admitted only where A is already dark, so the incoming picture
    // grows out of the shadows of the outgoing one.
    static const SDL_BlendMode kInfiltrate = compose(
      SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      SDL_BLENDOPERATION_ADD);
    // The opposite: B is admitted only where A is already bright, so it burns
    // in through the highlights.
    static const SDL_BlendMode kEmberIn = compose(
      SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE,
      SDL_BLENDOPERATION_ADD);

    if (mode == "screen")     return kScreen;
    if (mode == "multiply")   return kMultiply;
    if (mode == "lighten")    return kLighten;
    if (mode == "darken")     return kDarken;
    if (mode == "subtract")   return kSubtract;
    if (mode == "undercut")   return kUndercut;
    if (mode == "infiltrate") return kInfiltrate;
    if (mode == "ember")      return kEmberIn;
    if (mode == "add")        return SDL_BLENDMODE_ADD;
    return SDL_BLENDMODE_BLEND;   // dissolve
  }

  // ══ PRESENTER VIEW ═══════════════════════════════════════════════════════
  //
  // What the person running the show needs, on a screen the audience cannot
  // see: the slide that is up, the one before it, the one after it, the notes
  // for the one that is up, and the time.
  //
  // It is an OUTPUT rather than a second control window, which means it
  // inherits the display picker, fullscreen, arming and disarming, and the
  // health reporting -- all of which a presenter screen needs and none of
  // which had to be written again. Put programme on the projector and the
  // presenter view on the laptop, exactly as a slide deck does it.
  //
  // EVERY PART OF IT IS THE OPERATOR'S TO SET: three layouts, each panel
  // switchable on its own, the three colours, and the size of the notes. A
  // presenter screen is usually somebody else's laptop in somebody else's
  // room, and "can you make the notes bigger" is a request that arrives four
  // minutes before doors.
  //
  // The CURRENT picture is drawn by the ordinary layer path into a smaller
  // rect, so it is the real live frame with the cue's own geometry and
  // effects -- not an approximation of it. PREVIOUS and NEXT come from the
  // same thumbnail cache the playlist rows use, so they cost nothing extra.

  // The five colours a presenter screen draws with, from the three the
  // operator chose. The two derived ones are MIXES TOWARDS THE BACKGROUND
  // rather than fixed greys: on a white presenter screen the soft ink has to
  // get darker and on a black one lighter, and one fixed grey disappears on
  // whichever of the two it was not chosen for.
  struct PresenterInk {
    SDL_Color background {12, 14, 12, 255};
    SDL_Color ink {232, 240, 228, 255};
    SDL_Color soft {150, 168, 148, 255};
    SDL_Color accent {143, 191, 96, 255};
    SDL_Color rule {52, 62, 52, 255};
  };

  static SDL_Color presenterMix(SDL_Color a, SDL_Color b, double t) {
    const auto channel = [t](std::uint8_t from, std::uint8_t to) {
      return static_cast<std::uint8_t>(
        std::clamp<long>(std::lround(from + (to - from) * t), 0, 255));
    };
    return SDL_Color {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), 255};
  }

  static PresenterInk presenterInkFor(const OutputTarget::PresenterOptions& opt) {
    PresenterInk screen;
    // An unreadable presenter screen is worse than a plain one, so a colour
    // that does not parse keeps the default rather than becoming black.
    if (const auto c = tryParseColor(opt.background)) screen.background = *c;
    if (const auto c = tryParseColor(opt.ink)) screen.ink = *c;
    if (const auto c = tryParseColor(opt.accent)) screen.accent = *c;
    screen.soft = presenterMix(screen.ink, screen.background, 0.42);
    screen.rule = presenterMix(screen.ink, screen.background, 0.80);
    return screen;
  }

  // ── WHERE THE FOUR PANELS GO ─────────────────────────────────────────────
  //
  // Everything below produces the same thing -- four rectangles -- and the
  // drawing code takes them without caring which route they came by. The three
  // named layouts REFLOW: switch a panel off and the others take its room,
  // which is what you want from a preset. A custom layout does not reflow,
  // because it is the arrangement somebody chose and rearranging it under them
  // would be a bug rather than a courtesy.
  struct PresenterPlacement {
    SDL_Rect live {0, 0, 0, 0};
    SDL_Rect previous {0, 0, 0, 0};
    SDL_Rect next {0, 0, 0, 0};
    SDL_Rect notes {0, 0, 0, 0};
  };

  // One panel's place as fractions of the body, which is how a custom layout is
  // stored: a layout laid out on a 1080 laptop is then the same shape on the 4K
  // screen it ends up on.
  struct PresenterFrac {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
    bool valid() const { return w > 0.001 && h > 0.001; }
  };

  struct PresenterFracs {
    PresenterFrac live, previous, next, notes;
  };

  // "live:0,0,0.72,0.68|prev:0.74,0,0.26,0.33|next:...|notes:..."
  static PresenterFracs parsePresenterLayout(const std::string& text) {
    PresenterFracs out;
    std::size_t at = 0;
    while (at < text.size()) {
      const std::size_t bar = text.find('|', at);
      const std::string chunk =
        text.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
      at = (bar == std::string::npos) ? text.size() : bar + 1;
      const std::size_t colon = chunk.find(':');
      if (colon == std::string::npos) continue;
      const std::string name = trim(chunk.substr(0, colon));
      double v[4] = {0, 0, 0, 0};
      int count = 0;
      std::size_t from = colon + 1;
      while (count < 4 && from <= chunk.size()) {
        const std::size_t comma = chunk.find(',', from);
        const std::string field = chunk.substr(
          from, comma == std::string::npos ? std::string::npos : comma - from);
        try {
          v[count++] = std::stod(trim(field));
        } catch (...) {
          break;
        }
        if (comma == std::string::npos) break;
        from = comma + 1;
      }
      if (count < 4) continue;
      // Clamped on the way in, not on the way out: a layout that puts a panel
      // off the screen is unrecoverable from the presenter screen itself,
      // which is often the only one anybody is looking at.
      PresenterFrac frac {std::clamp(v[0], 0.0, 0.98), std::clamp(v[1], 0.0, 0.98),
                          std::clamp(v[2], 0.02, 1.0), std::clamp(v[3], 0.02, 1.0)};
      frac.w = std::min(frac.w, 1.0 - frac.x);
      frac.h = std::min(frac.h, 1.0 - frac.y);
      if (name == "live") out.live = frac;
      else if (name == "prev" || name == "previous") out.previous = frac;
      else if (name == "next") out.next = frac;
      else if (name == "notes") out.notes = frac;
    }
    return out;
  }

  static std::string formatPresenterLayout(const PresenterFracs& f) {
    auto one = [](const char* name, const PresenterFrac& r) {
      char buf[96];
      std::snprintf(buf, sizeof(buf), "%s:%.4g,%.4g,%.4g,%.4g", name, r.x, r.y,
                    r.w, r.h);
      return std::string(buf);
    };
    std::string out;
    if (f.live.valid()) out += one("live", f.live);
    if (f.previous.valid()) out += (out.empty() ? "" : "|") + one("prev", f.previous);
    if (f.next.valid()) out += (out.empty() ? "" : "|") + one("next", f.next);
    if (f.notes.valid()) out += (out.empty() ? "" : "|") + one("notes", f.notes);
    return out;
  }

  // Fractions back to pixels, inset by the padding so neighbouring panels do
  // not touch. A fraction that rounds to nothing comes back empty rather than
  // one pixel wide, so the drawing code's "is this panel here?" test is the
  // same for hidden and for vanishingly small.
  static SDL_Rect presenterFracToRect(const PresenterFrac& f, const SDL_Rect& body,
                                      int pad) {
    if (!f.valid()) return SDL_Rect {0, 0, 0, 0};
    const int x = body.x + static_cast<int>(std::lround(f.x * body.w));
    const int y = body.y + static_cast<int>(std::lround(f.y * body.h));
    const int w = static_cast<int>(std::lround(f.w * body.w)) - pad;
    const int h = static_cast<int>(std::lround(f.h * body.h)) - pad;
    if (w < 16 || h < 16) return SDL_Rect {0, 0, 0, 0};
    return SDL_Rect {x, y, w, h};
  }

  // What a named layout looks like, BEFORE anything is switched off. These are
  // also what "copy the current layout" hands the operator to start editing
  // from, which is why they live here as data rather than as arithmetic
  // scattered through the drawing code.
  static PresenterFracs presenterPresetFracs(const std::string& layout) {
    PresenterFracs f;
    if (layout == "filmstrip") {
      f.previous = {0.00, 0.00, 0.26, 0.44};
      f.live     = {0.27, 0.00, 0.46, 0.44};
      f.next     = {0.74, 0.00, 0.26, 0.44};
      f.notes    = {0.00, 0.45, 1.00, 0.55};
    } else if (layout == "notes") {
      f.previous = {0.00, 0.00, 0.26, 0.23};
      f.live     = {0.27, 0.00, 0.46, 0.23};
      f.next     = {0.74, 0.00, 0.26, 0.23};
      f.notes    = {0.00, 0.24, 1.00, 0.76};
    } else {                                   // wide, and anything unknown
      f.live     = {0.00, 0.00, 0.72, 0.68};
      f.previous = {0.73, 0.00, 0.27, 0.33};
      f.next     = {0.73, 0.34, 0.27, 0.34};
      f.notes    = {0.00, 0.70, 1.00, 0.30};
    }
    return f;
  }

  // A rounded card. There is no rounded-rect primitive in the renderer and one
  // is not worth adding for this: a radius's worth of one-pixel bands, each
  // inset by the corner circle's own width at that row, draws it exactly.
  static void presenterRounded(SDL_Renderer* ren, const SDL_Rect& box, int radius,
                               SDL_Color color) {
    const int r = std::clamp(radius, 0, std::min(box.w, box.h) / 2);
    if (r <= 0) {
      Primitives::fillRect(ren, box, color);
      return;
    }
    Primitives::fillRect(ren, SDL_Rect {box.x, box.y + r, box.w, box.h - r * 2},
                         color);
    for (int i = 0; i < r; ++i) {
      const double dy = r - i - 0.5;
      const int inset = r - static_cast<int>(std::lround(
        std::sqrt(std::max(0.0, static_cast<double>(r) * r - dy * dy))));
      Primitives::fillRect(ren, SDL_Rect {box.x + inset, box.y + i,
                                          box.w - inset * 2, 1}, color);
      Primitives::fillRect(ren, SDL_Rect {box.x + inset, box.y + box.h - 1 - i,
                                          box.w - inset * 2, 1}, color);
    }
  }

  // A caption in a soft pill rather than bare text on the background. It costs
  // nothing, it groups the label with the thing it names, and it is the one
  // place on this screen where the accent colour earns its keep.
  SDL_Rect presenterPill(SDL_Renderer* ren, const SDL_Rect& at,
                         const std::string& text, TTF_Font* font,
                         SDL_Color fill, SDL_Color ink) {
    const int padX = std::max(4, at.h / 3);
    const int width = std::min(at.w,
                               measuredTextWidth(font, text) + padX * 2);
    const SDL_Rect pill {at.x, at.y, std::max(8, width), at.h};
    presenterRounded(ren, pill, at.h / 2, fill);
    drawCenteredTextSafe(ren, font, pill, text, ink);
    return pill;
  }

  // Where the speaker is in the deck, as the slides themselves.
  //
  // The same little cards the slide importer fills in, for the same reason:
  // "42 of 109" is a fact you have to do arithmetic on, and a row with a lit
  // card two-thirds along is one you read at a glance from a lectern. Sampled
  // to whatever fits, so a hundred-slide deck does not become a row of
  // one-pixel slivers.
  void presenterDeckStrip(SDL_Renderer* ren, const SDL_Rect& bar, int liveIndex,
                          int total, const PresenterInk& screen) {
    if (bar.w <= 0 || bar.h <= 0 || total <= 0) {
      return;
    }
    const int gap = std::max(2, bar.h / 5);
    const int cardH = std::max(4, bar.h);
    const int cardW = std::max(6, (cardH * 4) / 3);
    const int cards = std::clamp((bar.w + gap) / (cardW + gap), 1, total);
    const int rowW = cards * cardW + (cards - 1) * gap;
    const int x0 = bar.x + (bar.w - rowW) / 2;
    // Which card stands for the live cue: its position in the deck, scaled
    // into the row. With one card per cue this is the identity.
    const int lit = (total <= 1) ? 0
      : std::clamp((liveIndex * (cards - 1)) / std::max(1, total - 1), 0, cards - 1);
    for (int i = 0; i < cards; ++i) {
      const SDL_Rect card {x0 + i * (cardW + gap), bar.y, cardW, cardH};
      if (i == lit) {
        presenterRounded(ren, card, std::max(1, cardH / 4), screen.accent);
      } else if (i < lit) {
        presenterRounded(ren, card, std::max(1, cardH / 4), screen.rule);
      } else {
        presenterRounded(ren, card, std::max(1, cardH / 4),
                         presenterMix(screen.rule, screen.background, 0.55));
      }
    }
  }

  // Split a row between the pictures that are switched on, giving the live one
  // the larger share. A slot that is off comes back zero-width, and the last
  // slot on the row absorbs the rounding so the right edge always lands on the
  // right edge.
  static void presenterSplitRow(const SDL_Rect& strip, int gap, bool wantPrev,
                                bool wantNext, double liveWeight, SDL_Rect& prev,
                                SDL_Rect& live, SDL_Rect& next) {
    prev = live = next = SDL_Rect {0, 0, 0, 0};
    const double weight =
      (wantPrev ? 1.0 : 0.0) + liveWeight + (wantNext ? 1.0 : 0.0);
    const int gaps = (wantPrev ? 1 : 0) + (wantNext ? 1 : 0);
    const int usable = std::max(1, strip.w - gap * gaps);
    int x = strip.x;
    if (wantPrev) {
      prev = SDL_Rect {x, strip.y, static_cast<int>(std::lround(usable / weight)),
                       strip.h};
      x += prev.w + gap;
    }
    live = SDL_Rect {x, strip.y,
                     static_cast<int>(std::lround(usable * liveWeight / weight)),
                     strip.h};
    if (wantNext) {
      x += live.w + gap;
      next = SDL_Rect {x, strip.y, std::max(1, strip.x + strip.w - x), strip.h};
    } else {
      live.w = std::max(1, strip.x + strip.w - live.x);
    }
  }

  // One still on the presenter screen: a caption, the picture letterboxed so
  // the slide keeps its own shape, and the cue's number and name under it.
  void presenterSlot(OutputRuntime& runtime, const SDL_Rect& box,
                     const std::string& caption, const Cue* cue, int deckIndex,
                     int cueIndex, const PresenterInk& screen, TTF_Font* font,
                     const char* bridgeName, const char* emptyText) {
    if (box.w <= 0 || box.h <= 0) {
      return;
    }
    SDL_Renderer* ren = runtime.outputRenderer;
    const int labelH = std::max(12, textLineHeight(font));
    const SDL_Rect picture {box.x, box.y + labelH, box.w,
                            std::max(16, box.h - labelH * 2)};
    const int radius = std::max(2, labelH / 2);
    presenterPill(ren, SDL_Rect {box.x, box.y, box.w, labelH}, caption, font,
                  presenterMix(screen.rule, screen.background, 0.35), screen.soft);
    // A card, not a hole: the mount is rounded and the picture sits on it, so
    // an empty slot still reads as somewhere a slide goes.
    presenterRounded(ren, picture, radius,
                     presenterMix(screen.rule, screen.background, 0.25));
    const SDL_Rect inner {picture.x + 2, picture.y + 2, std::max(1, picture.w - 4),
                          std::max(1, picture.h - 4)};
    presenterRounded(ren, inner, std::max(1, radius - 1), SDL_Color {0, 0, 0, 255});
    if (!cue) {
      drawCenteredTextSafe(ren, font, picture, emptyText, screen.soft);
      return;
    }
    // THE SAME CACHE THE PLAYLIST ROWS USE, and under the same lock -- it is
    // filled by a decode running on another thread. Reading it unlocked was a
    // race that happened to be quiet because the map is usually warm.
    const std::string key = cueVisualCacheKey(*cue);
    bool drawn = false;
    {
      std::lock_guard<std::mutex> lk(thumbnailMutex_);
      const auto found = selectedThumbnailCache_.find(key);
      if (found != selectedThumbnailCache_.end() && !found->second.pixels.empty()) {
        const DecodedFrame& thumb = found->second;
        SDL_Texture* tex = ensureOverlayBridgeTexture(runtime, bridgeName,
                                                      thumb.width, thumb.height,
                                                      sdlPixelFormat(thumb.format));
        if (tex) {
          SDL_UpdateTexture(tex, nullptr, thumb.pixels.data(), thumb.width * 4);
          const double k = std::min(static_cast<double>(picture.w) / thumb.width,
                                    static_cast<double>(picture.h) / thumb.height);
          const SDL_Rect dst {
            picture.x + static_cast<int>((picture.w - thumb.width * k) / 2),
            picture.y + static_cast<int>((picture.h - thumb.height * k) / 2),
            std::max(1, static_cast<int>(thumb.width * k)),
            std::max(1, static_cast<int>(thumb.height * k))};
          SDL_RenderTexture(ren, tex, nullptr, &dst);
          drawn = true;
        }
      }
    }
    if (!drawn) {
      // ASK FOR IT, rather than waiting for the playlist to happen to draw
      // that row. A presenter screen is very often the only thing anybody is
      // looking at, and the cue it wants a picture of may be nowhere near the
      // visible part of a hundred-slide list. One request per frame, through
      // the queue the rows already use, so this cannot start a stampede.
      if (rowThumbWantedKey_.empty()) {
        rowThumbWantedKey_ = key;
        rowThumbWantedDeck_ = deckIndex;
        rowThumbWantedCue_ = cueIndex;
      }
      // Said out loud, because "no preview yet" and "no such slide" are
      // different facts and an empty black box reports them identically.
      drawCenteredTextSafe(ren, font, picture, "preview pending", screen.soft);
    }
    drawTextSafe(ren, font,
                 SDL_Rect {box.x + labelH / 2, picture.y + picture.h, box.w, labelH},
                 cueDisplayToken(*cue, cueIndex) + "  " + cue->name, screen.ink);
  }

  // The notes panel: a SCROLLING DOCUMENT, including builds.
  //
  // A cue's notes split on a line of exactly "---". The pane shows every part
  // up to the one being spoken, the current one in full ink and the parts
  // already said dimmed, and scrolls to keep the speaker's place. That makes
  // it a note SCROLL rather than a note switch: what has already been said
  // stays in front of them, which is how a lectern works.
  //
  // The scroll is in PIXELS and EASED. It used to stop at a line boundary with
  // a "... more below" marker, which named the problem instead of solving it --
  // the words the speaker still had to say were three lines under the bottom of
  // the panel and nothing could reach them. Now the clicker scrolls, and the
  // text glides under the eye rather than being replaced by different text.
  void presenterNotes(SDL_Renderer* ren, const SDL_Rect& box, const Cue* cue,
                      int deckIndex, const PresenterInk& screen,
                      TTF_Font* headFont, TTF_Font* bodyFont) {
    if (box.w <= 0 || box.h <= 0) {
      return;
    }
    const int headH = std::max(12, textLineHeight(headFont));
    const std::vector<std::string> parts =
      noteBuildParts(cue ? cue->notes : std::string());
    const int step = std::clamp(presenterNoteStepFor(deckIndex), 0,
                                std::max(0, static_cast<int>(parts.size()) - 1));

    // The heading, and the builds as DOTS -- filled for said, ringed for the
    // one being said, hollow for still to come. "build 3 of 4" is a fact to do
    // arithmetic on; four dots is one you read.
    const SDL_Rect headRect {box.x, box.y, box.w, headH};
    const SDL_Rect pill = presenterPill(
      ren, headRect, "NOTES", headFont,
      presenterMix(screen.rule, screen.background, 0.35), screen.soft);
    if (parts.size() > 1) {
      const int dot = std::max(4, headH / 3);
      const int gap = std::max(2, dot / 2);
      int dx = pill.x + pill.w + dot;
      for (std::size_t i = 0; i < parts.size() && dx + dot < box.x + box.w; ++i) {
        const SDL_Rect at {dx, box.y + (headH - dot) / 2, dot, dot};
        const int part = static_cast<int>(i);
        if (part == step) {
          presenterRounded(ren,
                           SDL_Rect {at.x - gap / 2, at.y - gap / 2,
                                     dot + gap, dot + gap},
                           (dot + gap) / 2, screen.accent);
        } else {
          presenterRounded(ren, at, dot / 2,
                           part < step
                             ? screen.soft
                             : presenterMix(screen.rule, screen.background, 0.4));
        }
        dx += dot + gap * 2;
      }
    }

    const int textY = box.y + headH + headH / 3;
    const SDL_Rect text {box.x, textY, box.w,
                         std::max(16, box.y + box.h - textY)};
    const int lineH = std::max(14, textLineHeight(bodyFont));
    if (!cue || cue->notes.empty()) {
      presenterNoteMetrics_[deckIndex] = PresenterNoteMetrics {};
      drawTextSafe(ren, bodyFont, SDL_Rect {text.x, text.y, text.w, lineH},
                   cue ? "(no notes for this cue)" : "", screen.soft);
      return;
    }

    // Wrapped on spaces here: the shared text helpers draw exactly one line,
    // and a presenter's notes are the one thing on this screen that is prose
    // rather than a label.
    //
    // MEASURED AGAINST THE RECT THE TEXT ACTUALLY GETS. drawTextSafe insets
    // through safeTextRect before drawing, so a line wrapped to the full box
    // width is a few pixels too long and comes back ellipsized -- the wrap and
    // the draw have to agree about what "fits" or every long line loses its
    // last word to an ellipsis.
    const int wrapW = std::max(
      16, safeTextRect(SDL_Rect {text.x, text.y, text.w, lineH}).w);
    struct NoteLine {
      std::string text;
      bool current = false;
    };
    std::vector<NoteLine> lines;
    int currentFirst = 0;
    for (int part = 0; part <= step; ++part) {
      const bool isCurrent = (part == step);
      if (isCurrent) {
        currentFirst = static_cast<int>(lines.size());
      }
      if (part > 0) {
        lines.push_back(NoteLine {std::string(), isCurrent});
      }
      std::istringstream paragraphs(parts[static_cast<std::size_t>(part)]);
      std::string paragraph;
      while (std::getline(paragraphs, paragraph)) {
        std::istringstream words(paragraph);
        std::string word;
        std::string line;
        while (words >> word) {
          const std::string attempt = line.empty() ? word : (line + " " + word);
          if (measuredTextWidth(bodyFont, attempt) > wrapW && !line.empty()) {
            lines.push_back(NoteLine {line, isCurrent});
            line = word;
          } else {
            line = attempt;
          }
        }
        lines.push_back(NoteLine {line, isCurrent});
      }
    }

    // PUBLISHED FOR THE TRANSPORT. The wrap depends on the font and the box,
    // so only the renderer can say how many lines there are or how many fit,
    // and the clicker needs both to know whether "forward" means scroll this
    // build or move to the next one.
    const int rows = std::max(1, text.h / lineH);
    presenterNoteMetrics_[deckIndex] =
      PresenterNoteMetrics {static_cast<int>(lines.size()), rows, currentFirst};

    // Eased in real time, so the speed is the same whatever the frame rate.
    const double target =
      static_cast<double>(presenterNoteTopRow(deckIndex)) * lineH;
    double& scroll = presenterNoteScroll_[deckIndex];
    Uint64& clock = presenterNoteScrollClock_[deckIndex];
    if (clock == 0) {
      scroll = target;                     // first frame: start where we are
    } else if (animationNow_ > clock) {
      const double dt = static_cast<double>(animationNow_ - clock) / 1000.0;
      // A ten-per-second exponential: about a fifth of a second to cross most
      // of the gap, which reads as "it moved" rather than as an animation to
      // sit through. Snapped at the end so it does not creep for ever.
      scroll += (target - scroll) * (1.0 - std::exp(-10.0 * std::min(dt, 0.25)));
      if (std::fabs(target - scroll) < 0.5) {
        scroll = target;
      }
    }
    clock = animationNow_;

    // Clipped, because a scrolling document has rows half in and half out.
    SDL_Rect previousClip {};
    const bool hadClip = SDL_RenderClipEnabled(ren) == true;
    if (hadClip) {
      SDL_GetRenderClipRect(ren, &previousClip);
    }
    SDL_SetRenderClipRect(ren, &text);
    const int firstRow = std::max(0, static_cast<int>(scroll / lineH) - 1);
    const int lastRow = std::min(static_cast<int>(lines.size()),
                                 firstRow + rows + 3);
    for (int row = firstRow; row < lastRow; ++row) {
      const NoteLine& line = lines[static_cast<std::size_t>(row)];
      if (line.text.empty()) {
        continue;
      }
      const int y = text.y + static_cast<int>(std::lround(row * lineH - scroll));
      drawTextSafe(ren, bodyFont, SDL_Rect {text.x, y, text.w, lineH}, line.text,
                   line.current ? screen.ink : screen.soft);
    }
    if (hadClip) {
      SDL_SetRenderClipRect(ren, &previousClip);
    } else {
      SDL_SetRenderClipRect(ren, nullptr);
    }

    // A soft fade at whichever edge still has text beyond it, instead of a line
    // of words saying so. It costs no row, it cannot be mistaken for part of
    // the note, and it goes on meaning the same thing while the text is moving.
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    const int fadeH = std::max(6, lineH);
    const int bands = 10;
    const int bandH = std::max(1, fadeH / bands);
    const bool fadeTop = scroll > 0.5;
    const bool fadeBottom = presenterNoteMoreBelow(deckIndex);
    for (int i = 0; i < bands; ++i) {
      if (fadeTop) {
        SDL_SetRenderDrawColor(
          ren, screen.background.r, screen.background.g, screen.background.b,
          static_cast<std::uint8_t>(std::lround(
            (1.0 - static_cast<double>(i) / bands) * 235.0)));
        const SDL_FRect top {static_cast<float>(text.x),
                             static_cast<float>(text.y + i * bandH),
                             static_cast<float>(text.w),
                             static_cast<float>(bandH)};
        SDL_RenderFillRect(ren, &top);
      }
      if (fadeBottom) {
        SDL_SetRenderDrawColor(
          ren, screen.background.r, screen.background.g, screen.background.b,
          static_cast<std::uint8_t>(std::lround(
            (static_cast<double>(i + 1) / bands) * 235.0)));
        const SDL_FRect bottom {
          static_cast<float>(text.x),
          static_cast<float>(text.y + text.h - fadeH + i * bandH),
          static_cast<float>(text.w), static_cast<float>(bandH)};
        SDL_RenderFillRect(ren, &bottom);
      }
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
  }

  void renderPresenterView(int outputIndex, int deckIndex, const SDL_Rect& bounds) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer) return;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) return;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return;
    SDL_Renderer* ren = runtime->outputRenderer;
    const Deck& deck = project_.decks[deckIndex];
    const OutputTarget::PresenterOptions& opt =
      project_.outputs[static_cast<std::size_t>(outputIndex)].presenter;
    const PresenterInk screen = presenterInkFor(opt);

    SDL_SetRenderDrawColor(ren, screen.background.r, screen.background.g,
                           screen.background.b, 255);
    SDL_RenderClear(ren);

    // Sized from the WINDOW, not from the operator's UI scale: this screen is
    // usually a different size from theirs and is read from further away.
    const int pad = std::max(8, bounds.w / 80);
    const int headerH = std::max(28, bounds.h / 14);
    const int footerH = opt.showTimers ? std::max(24, bounds.h / 18) : 0;
    auto pick = [this](TTF_Font* base, int size) {
      TTF_Font* sized = fontAtSize(base, size);
      return sized ? sized : base;
    };
    TTF_Font* titleFont = pick(fontBase_, std::clamp(bounds.h / 26, 12, 72));
    TTF_Font* labelFont = pick(fontSmall_, std::clamp(bounds.h / 46, 10, 36));
    // The notes size is the operator's, against the screen rather than against
    // the interface -- 1.0 is already comfortable from a lectern and most
    // people want more.
    TTF_Font* notesFont = pick(
      fontBase_, std::clamp(static_cast<int>(std::lround(bounds.h / 38.0 *
                                                         opt.notesScale)),
                            11, 96));

    const Cue* liveCue = activeCuePtr(deckIndex);
    const int nextIndex = nextCueIndexForDeck(deckIndex);
    const Cue* nextCue = (nextIndex >= 0 && nextIndex < static_cast<int>(deck.cues.size()))
                           ? &deck.cues[static_cast<std::size_t>(nextIndex)] : nullptr;
    const int prevIndex = presenterPreviousCueIndex(deckIndex);
    const Cue* prevCue = (prevIndex >= 0 && prevIndex < static_cast<int>(deck.cues.size()))
                           ? &deck.cues[static_cast<std::size_t>(prevIndex)] : nullptr;

    // ── header: what is live, and the time of day ────────────────────────
    {
      const SDL_Rect header {bounds.x + pad, bounds.y + pad, bounds.w - pad * 2,
                             headerH};
      int titleW = header.w;
      if (opt.showClock) {
        const std::time_t now = std::time(nullptr);
        std::tm local {};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        char clock[16];
        std::snprintf(clock, sizeof(clock), "%02d:%02d:%02d", local.tm_hour,
                      local.tm_min, local.tm_sec);
        const int clockW = std::max(headerH * 3, bounds.w / 6);
        drawTextSafe(ren, titleFont,
                     SDL_Rect {header.x + header.w - clockW, header.y, clockW,
                               header.h},
                     clock, screen.soft);
        titleW = header.w - clockW - pad;
      }
      const std::string title =
        liveCue ? (cueDisplayToken(*liveCue, deck.activeIndex) + "   " + liveCue->name)
                : std::string("- nothing live -");
      drawTextSafe(ren, titleFont,
                   SDL_Rect {header.x, header.y, std::max(1, titleW), header.h},
                   title, screen.ink);
    }

    // ── body: where each panel goes, per layout ──────────────────────────
    const int bodyLeft = bounds.x + pad;
    const int bodyW = bounds.w - pad * 2;
    const int bodyTop = bounds.y + pad + headerH + pad;
    const int bodyBottom =
      bounds.y + bounds.h - pad - (footerH > 0 ? footerH + pad : 0);
    const int bodyH = std::max(80, bodyBottom - bodyTop);

    SDL_Rect liveBox {}, prevBox {}, nextBox {}, notesBox {};
    // HOW MUCH OF THE SCREEN THE WORDS GET. Each layout has its own sensible
    // share and this scales it, so "give the notes more room" is one control
    // rather than a choice between three fixed arrangements. Past 1.0 -- and
    // with the pictures switched off -- the notes take the lot, which is what
    // somebody reading a long script from a lectern actually wants.
    const bool anyPicture = opt.showLive || opt.showPrevious || opt.showNext;
    auto notesHeightFrom = [&](double baseShare) {
      if (!opt.showNotes) return 0;
      if (!anyPicture) return bodyH;
      const double share = std::clamp(baseShare * opt.notesShare, 0.05, 0.92);
      return std::clamp(static_cast<int>(std::lround(bodyH * share)),
                        std::max(32, bodyH / 12), bodyH - std::max(48, bodyH / 8));
    };

    if (opt.layout == "custom" && !opt.customLayout.empty()) {
      // EXACTLY WHERE THEY WERE PUT. No reflow, no share: those are what the
      // presets do for somebody who has not arranged the screen themselves,
      // and applying them here would move panels the operator had placed.
      // Switching a panel off leaves its space empty, which is the honest
      // result of switching a panel off in a layout you laid out.
      const SDL_Rect body {bodyLeft, bodyTop, bodyW, bodyH};
      const PresenterFracs f = parsePresenterLayout(opt.customLayout);
      if (opt.showLive) liveBox = presenterFracToRect(f.live, body, pad);
      if (opt.showPrevious) prevBox = presenterFracToRect(f.previous, body, pad);
      if (opt.showNext) nextBox = presenterFracToRect(f.next, body, pad);
      if (opt.showNotes) notesBox = presenterFracToRect(f.notes, body, pad);
    } else if (opt.layout == "filmstrip") {
      // Previous / current / next across the top, notes large below.
      const int notesH = notesHeightFrom(0.55);
      const int stripH = std::max(0, bodyH - notesH - (notesH > 0 ? pad : 0));
      if (stripH > 0 && anyPicture) {
        presenterSplitRow(SDL_Rect {bodyLeft, bodyTop, bodyW, stripH}, pad,
                          opt.showPrevious, opt.showNext, opt.showLive ? 1.8 : 0.0,
                          prevBox, liveBox, nextBox);
      }
      if (notesH > 0) {
        notesBox = SDL_Rect {bodyLeft, bodyTop + stripH + (stripH > 0 ? pad : 0),
                             bodyW, notesH};
      }
    } else if (opt.layout == "notes") {
      // Notes dominate; the pictures ride a thin strip on top.
      const int notesH = notesHeightFrom(0.76);
      const int stripH = std::max(0, bodyH - notesH - (notesH > 0 ? pad : 0));
      if (stripH > 0 && anyPicture) {
        presenterSplitRow(SDL_Rect {bodyLeft, bodyTop, bodyW, stripH}, pad,
                          opt.showPrevious, opt.showNext, opt.showLive ? 1.35 : 0.0,
                          prevBox, liveBox, nextBox);
      }
      if (notesH > 0) {
        notesBox = SDL_Rect {bodyLeft, bodyTop + stripH + (stripH > 0 ? pad : 0),
                             bodyW, notesH};
      }
    } else {
      // "wide", and anything unrecognised: current large on the left, the
      // other two stacked beside it, notes across the bottom.
      const int notesH = notesHeightFrom(0.30);
      const int picsH = std::max(0, bodyH - notesH - (notesH > 0 ? pad : 0));
      if (picsH > 0 && anyPicture) {
        const bool side = opt.showPrevious || opt.showNext;
        // With the live picture off there is no "large one", so the two
        // remaining slides share the row instead of hugging one edge.
        if (!opt.showLive) {
          presenterSplitRow(SDL_Rect {bodyLeft, bodyTop, bodyW, picsH}, pad,
                            opt.showPrevious, opt.showNext, 0.0, prevBox, liveBox,
                            nextBox);
          liveBox = SDL_Rect {0, 0, 0, 0};
        } else {
          const int sideW = side ? std::max(120, (bodyW * 26) / 100) : 0;
          liveBox = SDL_Rect {bodyLeft, bodyTop,
                              std::max(80, bodyW - sideW - (side ? pad : 0)), picsH};
          if (side) {
            const int sideX = bodyLeft + bodyW - sideW;
            if (opt.showPrevious && opt.showNext) {
              const int half = std::max(40, (picsH - pad) / 2);
              prevBox = SDL_Rect {sideX, bodyTop, sideW, half};
              nextBox = SDL_Rect {sideX, bodyTop + half + pad, sideW,
                                  std::max(40, picsH - half - pad)};
            } else if (opt.showPrevious) {
              prevBox = SDL_Rect {sideX, bodyTop, sideW, picsH};
            } else {
              nextBox = SDL_Rect {sideX, bodyTop, sideW, picsH};
            }
          }
        }
      }
      if (notesH > 0) {
        notesBox = SDL_Rect {bodyLeft, bodyTop + picsH + (picsH > 0 ? pad : 0),
                             bodyW, notesH};
      }
    }

    // ── the live picture, through the ordinary layer path ────────────────
    if (opt.showLive && liveBox.w > 0 && liveBox.h > 0) {
      const int labelH = std::max(12, textLineHeight(labelFont));
      const SDL_Rect picture {liveBox.x, liveBox.y + labelH, liveBox.w,
                              std::max(16, liveBox.h - labelH)};
      presenterPill(ren, SDL_Rect {liveBox.x, liveBox.y, liveBox.w, labelH},
                    "LIVE", labelFont, screen.accent,
                    presenterMix(screen.accent, screen.background, 0.86));
      const int radius = std::max(2, labelH / 2);
      presenterRounded(ren, picture, radius, screen.accent);
      const SDL_Rect inner {picture.x + 2, picture.y + 2,
                            std::max(1, picture.w - 4), std::max(1, picture.h - 4)};
      presenterRounded(ren, inner, std::max(1, radius - 1),
                       SDL_Color {0, 0, 0, 255});
      renderDeckLayerIntoOutput(outputIndex, deckIndex, inner);
      renderDeckTransitionIntoOutput(outputIndex, deckIndex, inner);
    }
    presenterSlot(*runtime, prevBox, "PREVIOUS", prevCue, deckIndex, prevIndex,
                  screen, labelFont, "presenter_prev", "nothing before this");
    presenterSlot(*runtime, nextBox, "NEXT", nextCue, deckIndex, nextIndex,
                  screen, labelFont, "presenter_next", "end of list");
    if (opt.showNotes) {
      presenterNotes(ren, notesBox, liveCue, deckIndex, screen, labelFont, notesFont);
    }

    // ── footer: where you are in the deck, how long this has been up ─────
    if (footerH > 0) {
      const SDL_Rect footer {bodyLeft, bounds.y + bounds.h - footerH - pad, bodyW,
                             footerH};
      std::string left = "--:--";
      std::string right;
      if (const DeckRuntime* rt = runtimeForDeck(deckIndex)) {
        if (rt->mediaEngine) {
          const double pos = rt->mediaEngine->position();
          const double dur = rt->mediaEngine->duration();
          left = "elapsed  " + formatSeconds(pos);
          if (dur > 0.0) {
            right = "remaining  " + formatSeconds(std::max(0.0, dur - pos));
          }
        }
      }
      // What the clicker will do next, when it is going to scroll or reveal
      // rather than change the slide. Nobody should have to find that out by
      // pressing it in front of a room.
      const int builds = presenterBuildsRemaining(deckIndex);
      if (opt.buildsConsumeAdvance && builds > 0) {
        right = std::to_string(builds) + " more before the next cue";
      }
      const int sideW = std::max(uiScaled(90), bodyW / 5);
      drawTextSafe(ren, labelFont,
                   SDL_Rect {footer.x, footer.y, sideW, footer.h}, left,
                   screen.soft);
      if (!right.empty()) {
        drawTextSafe(ren, labelFont,
                     SDL_Rect {footer.x + footer.w - sideW, footer.y, sideW,
                               footer.h},
                     right, screen.soft);
      }
      // The deck itself, between the two readouts: a row of little slides with
      // the live one lit. "42 of 109" is a fact to do arithmetic on; a lit card
      // two-thirds along the row is one you read at a glance from a lectern.
      const int stripH = std::max(4, footer.h / 3);
      presenterDeckStrip(
        ren,
        SDL_Rect {footer.x + sideW + pad, footer.y + (footer.h - stripH) / 2,
                  std::max(1, footer.w - (sideW + pad) * 2), stripH},
        deck.activeIndex, static_cast<int>(deck.cues.size()), screen);
    }
  }

  // ══ PROMPTER ═════════════════════════════════════════════════════════════
  //
  // The talent's screen: the script, very large, scrolling up through a fixed
  // reading line at a pace measured in lines per minute -- and MIRRORED,
  // because a teleprompter's beamsplitter reverses the picture on its way to
  // the reader's eye.
  //
  // An output rather than a cue kind, like the presenter view, and for one
  // reason beyond convenience: prompter text is real typography at whatever
  // size the reader needs, and the engine's frame generators have a three-by-
  // five digit table. A script belongs where the text renderer lives.
  //
  // The script comes from the output's own `script` when it has one, and from
  // the LIVE CUE'S NOTES when it does not -- so a deck-driven show prompts
  // from the same words the presenter view is showing the operator, and a talk
  // with no slides can carry its own.

  // Where the script is scrolled to, per output, in pixels, and when it was
  // last advanced. Per OUTPUT rather than per deck: two prompters on one deck
  // are two readers, and they do not have to be in the same place.
  std::map<int, double> prompterScroll_;
  std::map<int, Uint64> prompterClock_;
  // Jog requests in LINES, waiting for a frame that knows how tall a line is.
  std::map<int, double> prompterJog_;

  double prompterScrollFor(int outputIndex) const {
    const auto at = prompterScroll_.find(outputIndex);
    return at == prompterScroll_.end() ? 0.0 : at->second;
  }

  // The words this output is prompting: its own script, or the live cue's
  // notes when it has none.
  std::string prompterScriptFor(const OutputTarget& output, int deckIndex) const {
    if (!output.prompter.script.empty()) {
      return output.prompter.script;
    }
    const Cue* live = activeCuePtr(deckIndex);
    return live ? live->notes : std::string();
  }

  void renderPrompterView(int outputIndex, int deckIndex, const SDL_Rect& bounds) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer) return;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) return;
    SDL_Renderer* ren = runtime->outputRenderer;
    const OutputTarget& output = project_.outputs[static_cast<std::size_t>(outputIndex)];
    const OutputTarget::PrompterOptions& opt = output.prompter;

    PresenterInk screen;
    if (const auto c = tryParseColor(opt.background)) screen.background = *c;
    if (const auto c = tryParseColor(opt.ink)) screen.ink = *c;
    if (const auto c = tryParseColor(opt.accent)) screen.accent = *c;
    screen.soft = presenterMix(screen.ink, screen.background, 0.45);
    screen.rule = presenterMix(screen.ink, screen.background, 0.78);

    // MIRRORED OUTPUT IS A FLIP OF THE WHOLE PICTURE, so it is drawn once into
    // a target texture and blitted back reversed. Mirroring each string as it
    // is drawn would reverse the glyphs but not their order, which is not the
    // same thing and is unreadable in the glass.
    const bool mirror = opt.mirrorHorizontal || opt.mirrorVertical;
    SDL_Texture* target = nullptr;
    SDL_Texture* savedTarget = nullptr;
    if (mirror) {
      target = ensureOverlayBridgeTargetTexture(*runtime, "prompter", bounds.w,
                                                bounds.h);
      if (target) {
        savedTarget = SDL_GetRenderTarget(ren);
        SDL_SetRenderTarget(ren, target);
      }
    }
    const SDL_Rect frame = mirror && target
                             ? SDL_Rect {0, 0, bounds.w, bounds.h}
                             : bounds;

    SDL_SetRenderDrawColor(ren, screen.background.r, screen.background.g,
                           screen.background.b, 255);
    SDL_RenderClear(ren);

    // Type sized from the SCREEN, scaled by the reader's own setting. A
    // prompter is read from two or three metres away by somebody who must not
    // look like they are reading, so the default is much larger than anything
    // else in this application.
    auto pick = [this](TTF_Font* base, int size) {
      TTF_Font* sized = fontAtSize(base, size);
      return sized ? sized : base;
    };
    const int pt = std::clamp(
      static_cast<int>(std::lround(frame.h / 18.0 * opt.fontScale)), 12, 200);
    TTF_Font* font = pick(fontBase_, pt);
    const int lineH = std::max(16, textLineHeight(font));
    const int pad = std::max(16, frame.w / 20);
    const SDL_Rect column {frame.x + pad, frame.y, std::max(32, frame.w - pad * 2),
                           frame.h};

    // Wrapped to the column, measured against the rect the text is actually
    // drawn into -- the same inset trap the presenter's notes fell into.
    const int wrapW = std::max(
      16, safeTextRect(SDL_Rect {column.x, column.y, column.w, lineH}).w);
    std::vector<std::string> lines;
    {
      std::istringstream paragraphs(prompterScriptFor(output, deckIndex));
      std::string paragraph;
      while (std::getline(paragraphs, paragraph)) {
        // A line of "---" is a note BUILD separator everywhere else in this
        // application; on a prompter it is simply a break in the script, and
        // showing the dashes would have the reader say them.
        if (trim(paragraph) == "---") {
          lines.push_back(std::string());
          continue;
        }
        std::istringstream words(paragraph);
        std::string word;
        std::string line;
        while (words >> word) {
          const std::string attempt = line.empty() ? word : (line + " " + word);
          if (measuredTextWidth(font, attempt) > wrapW && !line.empty()) {
            lines.push_back(line);
            line = word;
          } else {
            line = attempt;
          }
        }
        lines.push_back(line);
      }
    }

    // ── the pace ────────────────────────────────────────────────────────
    //
    // Lines per minute, not pixels per second: a reading pace belongs to the
    // READER and has to mean the same thing when the type size or the screen
    // changes. Advanced from the wall clock so it is honest at any frame rate.
    const int readingY = frame.y + static_cast<int>(
      std::lround(std::clamp(opt.readingLineFraction, 0.05, 0.95) * frame.h));
    double& scroll = prompterScroll_[outputIndex];
    Uint64& clock = prompterClock_[outputIndex];
    if (clock != 0 && opt.running && animationNow_ > clock) {
      const double dt = static_cast<double>(animationNow_ - clock) / 1000.0;
      scroll += (opt.linesPerMinute / 60.0) * lineH * std::min(dt, 0.5);
    }
    clock = animationNow_;
    // Spend any jog the operator asked for, now that a line has a height.
    if (auto jog = prompterJog_.find(outputIndex); jog != prompterJog_.end()) {
      scroll += jog->second * lineH;
      prompterJog_.erase(jog);
    }
    // Never past the end: the last line comes to REST ON the reading line
    // rather than sailing off above it and leaving the reader looking at an
    // empty screen with no way to tell whether the script had finished or the
    // prompter had broken. One line short of the full height is what parks it
    // there -- the full height scrolls it away.
    const double maxScroll =
      std::max(0.0, (static_cast<double>(lines.size()) - 1.0) * lineH);
    scroll = std::clamp(scroll, 0.0, maxScroll);

    // ── the script ──────────────────────────────────────────────────────
    SDL_Rect previousClip {};
    const bool hadClip = SDL_RenderClipEnabled(ren) == true;
    if (hadClip) SDL_GetRenderClipRect(ren, &previousClip);
    SDL_SetRenderClipRect(ren, &frame);
    const int firstRow = std::max(
      0, static_cast<int>((scroll - (readingY - frame.y)) / lineH) - 1);
    const int rows = frame.h / lineH + 3;
    for (int row = firstRow; row < firstRow + rows &&
                             row < static_cast<int>(lines.size()); ++row) {
      const std::string& line = lines[static_cast<std::size_t>(row)];
      if (line.empty()) continue;
      const int y = readingY + static_cast<int>(std::lround(row * lineH - scroll));
      if (y + lineH < frame.y || y > frame.y + frame.h) continue;
      // The line being read is in full ink; what is coming is dimmer. A
      // reader's eye finds the bright line without hunting for it.
      const bool onTheLine = y <= readingY && y + lineH > readingY;
      drawTextSafe(ren, font, SDL_Rect {column.x, y, column.w, lineH}, line,
                   onTheLine ? screen.ink : screen.soft);
    }
    if (hadClip) SDL_SetRenderClipRect(ren, &previousClip);
    else SDL_SetRenderClipRect(ren, nullptr);

    // ── the reading line ────────────────────────────────────────────────
    if (opt.showReadingLine) {
      // WEDGES AT THE EDGES, NOT A RULE THROUGH THE WORDS. A full-width line
      // struck straight through the sentence the reader is saying, which is
      // the one line on the screen that has to be easy to read. The marker is
      // in the margins, where every real prompter puts it, with a short stub
      // reaching in from each side so the eye still lands on the right row.
      const int thick = std::max(2, lineH / 14);
      const int wedge = std::max(8, lineH / 2);
      const int stub = std::max(wedge, pad - wedge / 2);
      Primitives::fillRect(ren, SDL_Rect {frame.x, readingY - thick / 2, stub,
                                          thick}, screen.accent);
      Primitives::fillRect(ren, SDL_Rect {frame.x + frame.w - stub,
                                          readingY - thick / 2, stub, thick},
                           screen.accent);
      for (int i = 0; i < wedge; ++i) {
        // Half a wedge tall at the edge, tapering to nothing. Scaling this by
        // the rule's THICKNESS as well made each marker a triangle a hundred
        // pixels high filling the margin.
        const int h = std::max(1, (wedge - i) / 2);
        Primitives::fillRect(ren, SDL_Rect {frame.x + i, readingY - h, 1, h * 2},
                             screen.accent);
        Primitives::fillRect(ren,
                             SDL_Rect {frame.x + frame.w - 1 - i, readingY - h, 1,
                                       h * 2},
                             screen.accent);
      }
    }

    // Paused is worth saying: a stopped prompter and a prompter that has run
    // out of script look identical to the person reading it.
    if (!opt.running) {
      // NOT `small`: it is a macro in the Windows headers, along with near,
      // far, min and max, and a local with that name silently stops being C++.
      TTF_Font* status = pick(fontSmall_, std::clamp(frame.h / 40, 10, 40));
      const int h = std::max(14, textLineHeight(status));
      drawTextSafe(ren, status,
                   SDL_Rect {frame.x + pad / 2, frame.y + frame.h - h * 2,
                             frame.w - pad, h},
                   lines.empty() ? "no script" : "paused", screen.accent);
    }

    // ── into the glass ──────────────────────────────────────────────────
    if (mirror && target) {
      SDL_SetRenderTarget(ren, savedTarget);
      const SDL_FRect dst {static_cast<float>(bounds.x), static_cast<float>(bounds.y),
                           static_cast<float>(bounds.w), static_cast<float>(bounds.h)};
      SDL_FlipMode flip = SDL_FLIP_NONE;
      if (opt.mirrorHorizontal && opt.mirrorVertical) {
        flip = static_cast<SDL_FlipMode>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL);
      } else if (opt.mirrorHorizontal) {
        flip = SDL_FLIP_HORIZONTAL;
      } else if (opt.mirrorVertical) {
        flip = SDL_FLIP_VERTICAL;
      }
      SDL_RenderTextureRotated(ren, target, nullptr, &dst, 0.0, nullptr, flip);
    }
  }

  void renderDeckLayerIntoOutput(int outputIndex, int sourceDeckIndex, const SDL_Rect& target) {
    OutputRuntime* outputRuntime = runtimeForOutput(outputIndex);
    if (!outputRuntime || !outputRuntime->outputRenderer) {
      return;
    }
    if (sourceDeckIndex < 0 || sourceDeckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Cue* sourceCue = activeCuePtr(sourceDeckIndex);
    if (!sourceCue) {
      return;
    }
    DeckRuntime* sourceRuntime = runtimeForDeck(sourceDeckIndex);
    if (!sourceRuntime || !sourceRuntime->mediaEngine) {
      return;
    }
    const DecodedFrame* sourceFrame = sourceRuntime->mediaEngine->currentFrame();
    if (!sourceFrame || sourceFrame->width <= 0 || sourceFrame->height <= 0 ||
        (sourceFrame->pixels.empty() && !sourceFrame->isGpu())) {
      return;
    }
#if DECKBOY_INPROC_DECODE
    if (sourceFrame->isGpu()) {
      if (sourceFrame->gpuDevice && sourceFrame->gpuDevice == outputRuntime->rendererD3DDevice) {
        // Zero-copy: GPU-copy the decoded slice into this output's wrapped
        // NV12 texture on frame advance, then composite it like any texture.
        SDL_Texture* gpuTexture = ensureLayerGpuTexture(
          *outputRuntime, sourceDeckIndex, sourceFrame->width, sourceFrame->height,
          sourceFrame->format);
        if (gpuTexture) {
          auto gpuFrameIt = outputRuntime->layerGpuFrameIndices.find(sourceDeckIndex);
          if (gpuFrameIt == outputRuntime->layerGpuFrameIndices.end() ||
              gpuFrameIt->second != sourceFrame->index) {
            if (deckboy::libav::copyGpuFrameToTexture(
                  *sourceFrame, outputRuntime->layerGpuTexture2Ds[sourceDeckIndex])) {
              outputRuntime->layerGpuFrameIndices[sourceDeckIndex] = sourceFrame->index;
            }
          }
          float gpuDeckOpacity = std::clamp(project_.decks[sourceDeckIndex].playlistOpacity, 0.0f, 1.0f);
          float gpuFadeGain = static_cast<float>(sourceRuntime->mediaEngine->currentVisualFadeGain());
          // THE CROSSFADER, on the zero-copy path as well.
          //
          // This is the branch an ordinary H.264 clip actually takes, and
          // patching only the CPU bridge below left the mixer half-built: the
          // dissolve appeared to work because both decks happened to inherit
          // the same wrong alpha, and add and multiply did nothing at all.
          SDL_BlendMode gpuBlend = SDL_BLENDMODE_BLEND;
          gpuDeckOpacity *= static_cast<float>(vjLayerGain(sourceDeckIndex, gpuBlend));
          Uint8 gpuAlpha = static_cast<Uint8>(std::lround(gpuDeckOpacity * gpuFadeGain * 255.0f));
          SDL_SetTextureBlendMode(gpuTexture, gpuBlend);
          SDL_SetTextureAlphaMod(gpuTexture, gpuAlpha);
          renderTextureWithCueGeometry(outputRuntime->outputRenderer, gpuTexture,
                                       sourceFrame->width, sourceFrame->height, sourceCue,
                                       target, gpuBlend);
          SDL_SetTextureBlendMode(gpuTexture, SDL_BLENDMODE_BLEND);
          SDL_SetTextureAlphaMod(gpuTexture, 255);
          return;
        }
      }
      // Different device (secondary output) or wrap failure: download the
      // frame once per advance and continue down the classic CPU bridge.
      // Download only when the bridge below will actually re-upload —
      // repeated ticks on an unchanged frame render the existing bridge
      // texture without touching the scratch (which other decks share).
      std::string gpuCueKey = cuePreviewCacheKey(*sourceCue);
      auto gpuUpIt = outputRuntime->layerBridgeFrameIndices.find(sourceDeckIndex);
      auto gpuKeyIt = outputRuntime->layerBridgeCueKeys.find(sourceDeckIndex);
      bool gpuWillUpload =
        gpuUpIt == outputRuntime->layerBridgeFrameIndices.end() ||
        gpuKeyIt == outputRuntime->layerBridgeCueKeys.end() ||
        gpuUpIt->second != sourceFrame->index ||
        gpuKeyIt->second != gpuCueKey;
      if (gpuWillUpload) {
        bool scratchCurrent =
          outputRuntime->gpuDownloadScratchDeck == sourceDeckIndex &&
          !outputRuntime->gpuDownloadScratch.pixels.empty() &&
          outputRuntime->gpuDownloadScratch.index == sourceFrame->index;
        if (!scratchCurrent) {
          if (!deckboy::libav::downloadGpuFrameNV12(*sourceFrame, outputRuntime->gpuDownloadScratch)) {
            return;
          }
          outputRuntime->gpuDownloadScratchDeck = sourceDeckIndex;
        }
        sourceFrame = &outputRuntime->gpuDownloadScratch;
      }
    }
#endif
    const Uint32 sourceFormat = sdlPixelFormat(sourceFrame->format);
    SDL_Texture* bridgeTexture = ensureLayerBridgeTexture(
      *outputRuntime, sourceDeckIndex,
      sourceFrame->width, sourceFrame->height, sourceFormat);
    if (!bridgeTexture) {
      return;
    }
    std::string cueKey = cuePreviewCacheKey(*sourceCue);
    auto frameIt = outputRuntime->layerBridgeFrameIndices.find(sourceDeckIndex);
    auto cueIt = outputRuntime->layerBridgeCueKeys.find(sourceDeckIndex);
    // A STILL CUE DECODES ONE FRAME, and this gate then never fires again --
    // so an effect that advances with time ran exactly once and froze. Grain
    // that does not move, a ripple standing still, and caustics and feedback,
    // whose whole subject is motion, reduced to one arbitrary frame. The gate
    // is right for what it was written for; it cannot know about these.
    // An LFO counts as animation for this purpose: a parameter moving on its
    // own needs the stack re-run each frame exactly as much as an effect that
    // advances with the frame index does, and on a still nothing else will
    // trigger it.
    const bool stackAnimates =
      deckboy::effects::cueEffectStackAnimates(sourceCue->effects) ||
      deckboy::effects::cueEffectStackHasLfo(sourceCue->effects);
    bool needsUpload =
      frameIt == outputRuntime->layerBridgeFrameIndices.end() ||
      cueIt == outputRuntime->layerBridgeCueKeys.end() ||
      frameIt->second != sourceFrame->index ||
      cueIt->second != cueKey ||
      stackAnimates;
    if (needsUpload) {
      if (sourceFrame->format == FramePixelFormat::NV12) {
        // NV12 cues never carry CPU effects — MediaEngine only chooses NV12
        // when the cue has no chroma key or color controls. Upload directly.
        const std::uint8_t* y = sourceFrame->pixels.data();
        const std::uint8_t* uv = y + static_cast<std::size_t>(sourceFrame->width) *
                                     static_cast<std::size_t>(sourceFrame->height);
        SDL_UpdateNVTexture(bridgeTexture, nullptr,
                            y, sourceFrame->width,
                            uv, sourceFrame->width);
      } else {
        const std::uint8_t* uploadPixels = sourceFrame->pixels.data();
        const bool wantsStack = deckboy::effects::cueEffectStackActive(sourceCue->effects);
        if (cueHasPixelEffects(*sourceCue) || wantsStack) {
          outputRuntime->layerBridgeScratchPixels = sourceFrame->pixels;
          applyCueVisualEffectsToPixels(outputRuntime->layerBridgeScratchPixels, *sourceCue);
          if (wantsStack) {
            // Effects run AFTER the colour controls, on the graded picture --
            // grading a posterised image would quantise first and then push the
            // few remaining levels around, which is not what either control is
            // for. The frame index drives anything that advances per frame.
            deckboy::effects::CueEffectContext fxCtx;
            fxCtx.width = sourceFrame->width;
            fxCtx.height = sourceFrame->height;
            // The SOURCE frame drives the look, so a given frame of a clip
            // always grades the same way and a recording is reproducible. A
            // still has no frame progression to offer, so an animating stack on
            // one is driven by the app's own frame counter instead -- the only
            // clock available when the picture itself never moves.
            fxCtx.frameIndex =
              (stackAnimates && isDefaultStillDurationCueKind(sourceCue->kind))
                ? motionDriverFrameCounter_
                : sourceFrame->index;
            // Only advance a driver when something will actually read it --
            // decoding a clip nobody is puppeteering would be a cost with no
            // picture to show for it.
            if (!sourceCue->motionDriverPath.empty()) {
              fxCtx.motion = advanceMotionDriver(sourceDeckIndex, *sourceCue);
            }
            fxCtx.effectState =
              effectStateForDeck(sourceDeckIndex, sourceCue->effects.size());
            fxCtx.stateHold = !claimDeckFeedbackAdvance(sourceDeckIndex);
            fxCtx.textMode =
              textModeRendererFor(sourceDeckIndex, *sourceCue);
            // Any armed LFO, evaluated for this frame. Returns false and costs
            // nothing when the cue has none, which is almost every cue.
            std::vector<deckboy::effects::CueEffect> modulated;
            const bool moving = deckboy::effects::modulateCueEffectStack(
              sourceCue->effects, lfoSeconds_, lfoBeats_, modulated);
            // Timed, because "why is it stuttering" is a question an operator
            // should not have to answer by deleting effects one at a time. This
            // is the REAL cost on this machine at this raster, not an estimate.
            const auto fxBegan = std::chrono::steady_clock::now();
            deckboy::effects::applyCueEffectStack(
              outputRuntime->layerBridgeScratchPixels,
              moving ? modulated : sourceCue->effects, fxCtx);
            noteEffectChainCost(
              sourceDeckIndex,
              std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - fxBegan).count());
          }
          uploadPixels = outputRuntime->layerBridgeScratchPixels.data();
        }
        SDL_UpdateTexture(bridgeTexture, nullptr, uploadPixels, sourceFrame->width * 4);
      }
      outputRuntime->layerBridgeFrameIndices[sourceDeckIndex] = sourceFrame->index;
      outputRuntime->layerBridgeCueKeys[sourceDeckIndex] = std::move(cueKey);
    }
    float deckOpacity = std::clamp(project_.decks[sourceDeckIndex].playlistOpacity, 0.0f, 1.0f);
    float fadeGain = static_cast<float>(sourceRuntime->mediaEngine->currentVisualFadeGain());
    // THE CROSSFADER, folded into the opacity this deck already had rather than
    // replacing it -- a deck faded down or mid cue-fade must stay faded down.
    // Outside VJ mode the multiplier is 1 and every existing show renders
    // exactly as before, through the same call.
    SDL_BlendMode layerBlend = SDL_BLENDMODE_BLEND;
    deckOpacity *= static_cast<float>(vjLayerGain(sourceDeckIndex, layerBlend));
    Uint8 alpha = static_cast<Uint8>(std::lround(deckOpacity * fadeGain * 255.0f));
    SDL_SetTextureBlendMode(bridgeTexture, layerBlend);
    SDL_SetTextureAlphaMod(bridgeTexture, alpha);
    renderTextureWithCueGeometry(outputRuntime->outputRenderer, bridgeTexture, sourceFrame->width, sourceFrame->height, sourceCue, target, layerBlend);
    // Left as found: this texture is reused, and a mix must not leak into
    // whatever draws with it next.
    SDL_SetTextureBlendMode(bridgeTexture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(bridgeTexture, 255);
  }

  // Draw the OUTGOING picture over the incoming one, at whatever the chosen
  // transition says this instant should look like.
  //
  // Kind-agnostic on purpose: it takes the frame the engine held when the cue
  // changed and blends it, so a pattern crossfades into a camera into a slide
  // without any of them being special-cased.
  void renderDeckTransitionIntoOutput(int outputIndex, int sourceDeckIndex,
                                      const SDL_Rect& target) {
    OutputRuntime* outputRuntime = runtimeForOutput(outputIndex);
    if (!outputRuntime || !outputRuntime->outputRenderer) return;
    if (sourceDeckIndex < 0 ||
        sourceDeckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    DeckRuntime* deckRuntime = runtimeForDeck(sourceDeckIndex);
    if (!deckRuntime || !deckRuntime->mediaEngine) return;
    MediaEngine& engine = *deckRuntime->mediaEngine;

    const double seconds = engine.outgoingSeconds();
    if (seconds <= 0.0001) return;                  // a cut has nothing to draw
    const DecodedFrame* out = engine.outgoingFrame();
    if (!out || out->width <= 0 || out->height <= 0 || out->pixels.empty()) return;

    const double progress = engine.outgoingProgress01();
    if (progress >= 1.0) {
      engine.releaseHeldFrameIfTransitionDone();
      return;
    }

    const TransitionStyle style = engine.outgoingStyle();

    // ── PUSH ────────────────────────────────────────────────────────────
    //
    // The outgoing picture slides off and the incoming one follows it in. The
    // incoming picture has already been drawn by the layer above at rest, so
    // the honest way to move it is to draw the outgoing one over the top,
    // travelling -- and to move the still-visible part of the incoming picture
    // with it. Deckboy composites into a single target, so "move the layer
    // below" means redrawing it offset, which is exactly what this does.
    if (isPushStyle(style)) {
      const double eased = progress * progress * (3.0 - 2.0 * progress);
      int dx = 0, dy = 0;
      switch (style) {
        case TransitionStyle::PushLeft:  dx = -static_cast<int>(eased * target.w); break;
        case TransitionStyle::PushRight: dx =  static_cast<int>(eased * target.w); break;
        case TransitionStyle::PushUp:    dy = -static_cast<int>(eased * target.h); break;
        default:                         dy =  static_cast<int>(eased * target.h); break;
      }
      // The incoming picture, shifted in from the opposite side. Drawn from the
      // engine's CURRENT frame so it is the real thing rather than a guess.
      if (const DecodedFrame* in = engine.currentFrame()) {
        if (in->width > 0 && !in->pixels.empty()) {
          SDL_Rect inRect = target;
          inRect.x += dx + (dx ? (dx > 0 ? -target.w : target.w) : 0);
          inRect.y += dy + (dy ? (dy > 0 ? -target.h : target.h) : 0);
          renderTransitionFrame(*outputRuntime, *in, sourceDeckIndex, inRect, 255,
                                "push-in");
        }
      }
      SDL_Rect outRect = target;
      outRect.x += dx;
      outRect.y += dy;
      renderTransitionFrame(*outputRuntime, *out, sourceDeckIndex, outRect, 255);
      return;
    }

    // ── WIPE ────────────────────────────────────────────────────────────
    //
    // A hard edge travels across a still frame: neither picture moves, the
    // outgoing one is simply revealed less and less. Done by clipping the
    // outgoing draw to the part it still owns.
    if (isWipeStyle(style)) {
      SDL_Rect keep = target;
      const int travelled = static_cast<int>(progress * target.w);
      const int travelledY = static_cast<int>(progress * target.h);
      switch (style) {
        case TransitionStyle::WipeLeft:
          keep.x += travelled; keep.w = std::max(0, target.w - travelled); break;
        case TransitionStyle::WipeRight:
          keep.w = std::max(0, target.w - travelled); break;
        case TransitionStyle::WipeUp:
          keep.y += travelledY; keep.h = std::max(0, target.h - travelledY); break;
        default:
          keep.h = std::max(0, target.h - travelledY); break;
      }
      if (keep.w <= 0 || keep.h <= 0) return;
      SDL_Rect previousClip {};
      const bool hadClip = SDL_RenderClipEnabled(outputRuntime->outputRenderer);
      if (hadClip) SDL_GetRenderClipRect(outputRuntime->outputRenderer, &previousClip);
      SDL_SetRenderClipRect(outputRuntime->outputRenderer, &keep);
      renderTransitionFrame(*outputRuntime, *out, sourceDeckIndex, target, 255);
      SDL_SetRenderClipRect(outputRuntime->outputRenderer,
                            hadClip ? &previousClip : nullptr);
      return;
    }

    // ── IRIS ────────────────────────────────────────────────────────────
    //
    // A circle opens from the centre. SDL has no circular clip, so the
    // outgoing frame is drawn as a ring of horizontal bands with a growing
    // hole punched through the middle -- which is the same picture and needs
    // no shader.
    if (style == TransitionStyle::Iris) {
      const double radius = progress *
        std::sqrt(static_cast<double>(target.w * target.w + target.h * target.h)) * 0.5;
      const int cx = target.x + target.w / 2;
      const int cy = target.y + target.h / 2;
      SDL_Rect previousClip {};
      const bool hadClip = SDL_RenderClipEnabled(outputRuntime->outputRenderer);
      if (hadClip) SDL_GetRenderClipRect(outputRuntime->outputRenderer, &previousClip);
      // BANDS SIZED TO THE RASTER, not to a constant. Four pixels at 4K is
      // five hundred and forty clipped draws a frame, which measured 54fps on
      // a 60fps output -- an effect that costs frames during a transition is
      // an effect that shows as a stutter at the worst moment.
      //
      // Sixty bands is enough that the edge reads as a curve at any size, and
      // sixty draws is nothing.
      // THIRTY-TWO BANDS, and the number was measured rather than guessed.
      //
      // Each band is a clip change and a draw the GPU pays for in full, so the
      // count is the cost. Against an idle baseline of 46-54fps on this
      // machine, thirty-two costs about six frames a second while a transition
      // is running, and reads as a curve at any raster.
      //
      // The expensive mistake was not the count: renderTransitionFrame used to
      // re-upload the whole picture on every one of these draws, which took a
      // 4K iris to 1.3fps. It uploads once a frame now.
      const int kBand = std::max(6, target.h / 32);
      for (int y = target.y; y < target.y + target.h; y += kBand) {
        const double dy = (y + kBand * 0.5) - cy;
        const double inside = radius * radius - dy * dy;
        const int half = inside > 0.0 ? static_cast<int>(std::sqrt(inside)) : -1;
        if (half >= target.w / 2) continue;          // fully inside the hole
        if (half < 0) {
          SDL_Rect band {target.x, y, target.w, kBand};
          SDL_SetRenderClipRect(outputRuntime->outputRenderer, &band);
          renderTransitionFrame(*outputRuntime, *out, sourceDeckIndex, target, 255);
          continue;
        }
        const SDL_Rect left {target.x, y, std::max(0, cx - half - target.x), kBand};
        const SDL_Rect right {cx + half, y,
                              std::max(0, target.x + target.w - (cx + half)), kBand};
        for (const SDL_Rect& part : {left, right}) {
          if (part.w <= 0) continue;
          SDL_SetRenderClipRect(outputRuntime->outputRenderer, &part);
          renderTransitionFrame(*outputRuntime, *out, sourceDeckIndex, target, 255);
        }
      }
      SDL_SetRenderClipRect(outputRuntime->outputRenderer,
                            hadClip ? &previousClip : nullptr);
      return;
    }

    if (style == TransitionStyle::DipWhite) {
      // The same shape as dip-to-black, through white: a flash rather than a
      // blink, and the one an operator reaches for on a camera cut.
      SDL_SetRenderDrawBlendMode(outputRuntime->outputRenderer, SDL_BLENDMODE_BLEND);
      if (progress < 0.5) {
        renderTransitionFrame(*outputRuntime, *out, sourceDeckIndex, target, 255);
        SDL_SetRenderDrawColor(outputRuntime->outputRenderer, 255, 255, 255,
          static_cast<Uint8>(std::clamp(progress * 2.0, 0.0, 1.0) * 255.0));
      } else {
        SDL_SetRenderDrawColor(outputRuntime->outputRenderer, 255, 255, 255,
          static_cast<Uint8>(std::clamp(1.0 - (progress - 0.5) * 2.0, 0.0, 1.0) * 255.0));
      }
      SDL_RenderFillRect(outputRuntime->outputRenderer, nullptr);
      SDL_SetRenderDrawBlendMode(outputRuntime->outputRenderer, SDL_BLENDMODE_NONE);
      return;
    }

    if (style == TransitionStyle::DipBlack) {
      // Two halves: the outgoing picture falls into black, then black lifts off
      // the incoming one. Nothing of the outgoing frame is drawn in the second
      // half -- it has already gone.
      SDL_SetRenderDrawBlendMode(outputRuntime->outputRenderer, SDL_BLENDMODE_BLEND);
      if (progress < 0.5) {
        renderTransitionFrame(*outputRuntime, *out, sourceDeckIndex, target, 255);
        const Uint8 black = static_cast<Uint8>(
          std::clamp(progress * 2.0, 0.0, 1.0) * 255.0);
        SDL_SetRenderDrawColor(outputRuntime->outputRenderer, 0, 0, 0, black);
      } else {
        const Uint8 black = static_cast<Uint8>(
          std::clamp(1.0 - (progress - 0.5) * 2.0, 0.0, 1.0) * 255.0);
        SDL_SetRenderDrawColor(outputRuntime->outputRenderer, 0, 0, 0, black);
      }
      SDL_RenderFillRect(outputRuntime->outputRenderer, nullptr);
      SDL_SetRenderDrawBlendMode(outputRuntime->outputRenderer, SDL_BLENDMODE_NONE);
      return;
    }

    // Crossfade: the outgoing picture thins out over the incoming one.
    const Uint8 alpha = static_cast<Uint8>(
      std::clamp(1.0 - progress, 0.0, 1.0) * 255.0);
    if (alpha == 0) return;
    renderTransitionFrame(*outputRuntime, *out, sourceDeckIndex, target, alpha);
  }

  // Blit one held frame at a given alpha, through its own bridge texture so it
  // cannot disturb the live layer's.
  static bool isPushStyle(TransitionStyle s) {
    return s == TransitionStyle::PushLeft || s == TransitionStyle::PushRight ||
           s == TransitionStyle::PushUp   || s == TransitionStyle::PushDown;
  }
  static bool isWipeStyle(TransitionStyle s) {
    return s == TransitionStyle::WipeLeft || s == TransitionStyle::WipeRight ||
           s == TransitionStyle::WipeUp   || s == TransitionStyle::WipeDown;
  }

  // `key` distinguishes the two pictures a push needs to hold at once: the
  // outgoing one travelling out and the incoming one travelling in. One shared
  // bridge texture would have each overwriting the other every frame.
  // UPLOAD ONCE PER FRAME, not once per draw.
  //
  // Iris draws the outgoing picture sixty times behind different clips, and
  // each call was re-uploading the whole thing: a 4K frame pushed across the
  // bus sixty times a frame measured 1.3fps. The pixels have not changed
  // between those draws -- only the clip has -- so the upload is skipped when
  // the frame is the one already sitting in the texture.
  void renderTransitionFrame(OutputRuntime& outputRuntime, const DecodedFrame& frame,
                             int deckIndex, const SDL_Rect& target, Uint8 alpha,
                             const char* key = "transition") {
    const Uint32 format = sdlPixelFormat(frame.format);
    const std::string bridgeKey = std::string(key) + ":" + std::to_string(deckIndex);
    SDL_Texture* tex = ensureOverlayBridgeTexture(
      outputRuntime, bridgeKey, frame.width, frame.height, format);
    if (!tex) return;
    // Keyed on the frame's own address and index: a held frame does not move
    // while it is being drawn, and the index changes when it is replaced.
    auto& stamp = outputRuntime.transitionUploadStamps[bridgeKey];
    const std::uintptr_t nowStamp =
      reinterpret_cast<std::uintptr_t>(frame.pixels.data()) ^
      (static_cast<std::uintptr_t>(frame.index) << 1);
    const bool needUpload = stamp != nowStamp;
    stamp = nowStamp;
    if (!needUpload) {
      SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
      SDL_SetTextureAlphaMod(tex, alpha);
      const SDL_Rect dstCached = target;
      SDL_RenderTexture(outputRuntime.outputRenderer, tex, nullptr, &dstCached);
      SDL_SetTextureAlphaMod(tex, 255);
      return;
    }
    // RGBA32 four bytes a pixel, like every other CPU blit on this path. An
    // NV12 held frame would need the two-plane update; the held frame always
    // comes from displayFrame_, which the compositor has already proven it can
    // draw, so this matches what the layer above it just did.
    SDL_UpdateTexture(tex, nullptr, frame.pixels.data(), frame.width * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex, alpha);
    const SDL_Rect dst = target;
    SDL_RenderTexture(outputRuntime.outputRenderer, tex, nullptr, &dst);
    SDL_SetTextureAlphaMod(tex, 255);
  }

  void renderOverlayFrameIntoOutput(OutputRuntime& outputRuntime,
                                    const std::string& overlayKey,
                                    const DecodedFrame& sourceFrame,
                                    const Cue& renderCue,
                                    const SDL_Rect& target) {
    if (!outputRuntime.outputRenderer || sourceFrame.width <= 0 || sourceFrame.height <= 0 ||
        sourceFrame.pixels.empty()) {
      return;
    }
    const Uint32 sourceFormat = sdlPixelFormat(sourceFrame.format);
    SDL_Texture* bridgeTexture = ensureOverlayBridgeTexture(
      outputRuntime, overlayKey, sourceFrame.width, sourceFrame.height, sourceFormat);
    if (!bridgeTexture) {
      return;
    }
    std::string cueKey = cuePreviewCacheKey(renderCue);
    auto frameIt = outputRuntime.overlayBridgeFrameIndices.find(overlayKey);
    auto cueIt = outputRuntime.overlayBridgeCueKeys.find(overlayKey);
    bool needsUpload =
      frameIt == outputRuntime.overlayBridgeFrameIndices.end() ||
      cueIt == outputRuntime.overlayBridgeCueKeys.end() ||
      frameIt->second != sourceFrame.index ||
      cueIt->second != cueKey;
    if (needsUpload) {
      if (sourceFrame.format == FramePixelFormat::NV12) {
        const std::uint8_t* y = sourceFrame.pixels.data();
        const std::uint8_t* uv = y + static_cast<std::size_t>(sourceFrame.width) *
                                     static_cast<std::size_t>(sourceFrame.height);
        SDL_UpdateNVTexture(bridgeTexture, nullptr,
                            y, sourceFrame.width,
                            uv, sourceFrame.width);
      } else {
        const std::uint8_t* uploadPixels = sourceFrame.pixels.data();
        if (cueHasPixelEffects(renderCue)) {
          outputRuntime.layerBridgeScratchPixels = sourceFrame.pixels;
          applyCueVisualEffectsToPixels(outputRuntime.layerBridgeScratchPixels, renderCue);
          uploadPixels = outputRuntime.layerBridgeScratchPixels.data();
        }
        SDL_UpdateTexture(bridgeTexture, nullptr, uploadPixels, sourceFrame.width * 4);
      }
      outputRuntime.overlayBridgeFrameIndices[overlayKey] = sourceFrame.index;
      outputRuntime.overlayBridgeCueKeys[overlayKey] = std::move(cueKey);
    }
    SDL_SetTextureAlphaMod(bridgeTexture, 255);
  }

  void renderOutputTestCard(int outputIndex, SDL_Renderer* renderer, int width, int height) {
    if (!renderer || width <= 0 || height <= 0) {
      return;
    }
    const OutputTarget& output = project_.outputs[std::clamp(outputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1))];
    auto fill = [&](int x, int y, int w, int h, SDL_Color color) {
      SDL_Rect rect {x, y, w, h};
      SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
      SDL_RenderFillRect(renderer, &rect);
    };

    const std::array<SDL_Color, 7> bars {{
      SDL_Color {191, 191, 191, 255},
      SDL_Color {191, 191,   0, 255},
      SDL_Color {  0, 191, 191, 255},
      SDL_Color {  0, 191,   0, 255},
      SDL_Color {191,   0, 191, 255},
      SDL_Color {191,   0,   0, 255},
      SDL_Color {  0,   0, 191, 255},
    }};
    int topH = height * 2 / 3;
    int barW = std::max(1, width / static_cast<int>(bars.size()));
    for (int i = 0; i < static_cast<int>(bars.size()); ++i) {
      int x = i * barW;
      int w = (i == static_cast<int>(bars.size()) - 1) ? (width - x) : barW;
      fill(x, 0, w, topH, bars[static_cast<size_t>(i)]);
    }

    int midY = topH;
    int midH = std::max(10, height / 10);
    fill(0, midY, width, midH, SDL_Color {18, 18, 18, 255});
    for (int i = 0; i < 8; ++i) {
      int x = i * width / 8;
      int w = (i == 7) ? (width - x) : (width / 8);
      Uint8 gray = static_cast<Uint8>(i * 255 / 7);
      fill(x, midY + 2, w, midH - 4, SDL_Color {gray, gray, gray, 255});
    }

    int botY = midY + midH;
    int botH = std::max(1, height - botY);
    fill(0, botY, width / 4, botH, SDL_Color {0, 0, 0, 255});
    fill(width / 4, botY, width / 4, botH, SDL_Color {255, 255, 255, 255});
    fill(width / 2, botY, width / 4, botH, SDL_Color {18, 18, 18, 255});
    fill((width * 3) / 4, botY, width - (width * 3) / 4, botH, SDL_Color {36, 36, 36, 255});

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 190);
    SDL_RenderLine(renderer, width / 2, 0, width / 2, height);
    SDL_RenderLine(renderer, 0, height / 2, width, height / 2);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 130);
    SDL_Rect safe80 {width / 10, height / 10, width - (width / 10) * 2, height - (height / 10) * 2};
    SDL_RenderRect(renderer, &safe80);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    std::string outName = output.name.empty() ? outputDefaultName(outputIndex) : output.name;
    std::string line1 = "TEST CARD OVERRIDE";
    std::string line2 = "Output " + std::to_string(outputIndex + 1) + " - " + outName;
    std::string line3 = "layout: " + normalizeOutputLayoutMode(output.outputLayoutMode)
      + "  rot: " + outputOrientationLabel(output.outputOrientationDegrees);
    drawText(renderer, fontLarge_, line1, SDL_Color {245, 245, 245, 255}, 22, 20);
    drawText(renderer, fontSmall_, line2, SDL_Color {245, 245, 245, 240}, 24, 52);
    drawText(renderer, fontSmall_, line3, SDL_Color {220, 220, 220, 220}, 24, 70);
  }

  // Paint a disabled output's still-visible window black exactly once. A
  // just-disabled output (New Show, or the output toggled off) otherwise leaves
  // its last frame frozen on the display because the render loop stops touching
  // it. Latched via blackedWhileDisabled so we don't do a vsync-blocking present
  // every frame; a hidden window has nothing on screen so it just latches.
  void clearDisabledOutputWindow(int outputIndex) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer || !runtime->outputWindow) {
      return;
    }
    if (runtime->blackedWhileDisabled) {
      return;
    }
    if ((SDL_GetWindowFlags(runtime->outputWindow) & SDL_WINDOW_HIDDEN) != 0) {
      runtime->blackedWhileDisabled = true;  // hidden: nothing to clear
      return;
    }
    SDL_SetRenderTarget(runtime->outputRenderer, nullptr);
    SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0, 255);
    SDL_RenderClear(runtime->outputRenderer);
    SDL_RenderPresent(runtime->outputRenderer);
    runtime->blackedWhileDisabled = true;
  }

  void renderOutputWindow(int outputIndex) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer) {
      return;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    if (!output.enabled) {
      // Egress teardown for a disabled output happens in the render loop (see
      // stopEgressForDisabledOutput) -- this function is never called for one.
      return;
    }
    runtime->blackedWhileDisabled = false;  // active again — re-black on next disable
    OutputBackendRuntimeRoute backendRoute = resolveOutputBackendRuntimeRoute(outputIndex);
    std::string outputType = normalizeOutputType(output.outputType);
    bool streamType = outputType == "stream";
    int compositionOutputIndex = outputIndex;
    if (streamType &&
        output.mirrorSourceOutputIndex >= 0 &&
        output.mirrorSourceOutputIndex < static_cast<int>(project_.outputs.size()) &&
        output.mirrorSourceOutputIndex != outputIndex) {
      compositionOutputIndex = output.mirrorSourceOutputIndex;
    }
    const OutputTarget& compositionOutput = project_.outputs[compositionOutputIndex];
    int hostDeckIndex = std::clamp(compositionOutput.hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    const Deck& hostDeck = project_.decks[hostDeckIndex];

    int width = 0;
    int height = 0;
    if (!streamType && runtime->outputWindow) {
      SDL_GetWindowSize(runtime->outputWindow, &width, &height);
    } else {
      auto [rasterW, rasterH] = outputRenderSizeForOutput(compositionOutputIndex);
      width = rasterW;
      height = rasterH;
    }
    width = std::max(1, width);
    height = std::max(1, height);
    if (project_.outputCanvasEnabled) {
      clampDeckCanvasViewToWindow(hostDeckIndex, width, height);
    }
    int targetCompositorW = width;
    int targetCompositorH = height;
    if (project_.outputCanvasEnabled) {
      auto [canvasW, canvasH] = outputCanvasRenderSize();
      targetCompositorW = canvasW;
      targetCompositorH = canvasH;
    }
    if (!runtime->compositorTexture
        || runtime->compositorWidth != targetCompositorW
        || runtime->compositorHeight != targetCompositorH) {
      configureOutputCompositor(outputIndex, targetCompositorW, targetCompositorH);
    }
    bool usingCompositor = runtime->compositorTexture != nullptr;
    if (usingCompositor) {
      SDL_SetRenderTarget(runtime->outputRenderer, runtime->compositorTexture);
    }
    int renderW = usingCompositor ? runtime->compositorWidth : width;
    int renderH = usingCompositor ? runtime->compositorHeight : height;
    SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0, 255);
    SDL_RenderClear(runtime->outputRenderer);

    SDL_Rect bounds {0, 0, renderW, renderH};
    auto outputLayers = layeredDeckEntriesForOutput(compositionOutputIndex);
    if (outputLayers.empty()) {
      outputLayers.emplace_back(0, hostDeckIndex);
    }
    if (output.outputTestCardEnabled) {
      renderOutputTestCard(outputIndex, runtime->outputRenderer, renderW, renderH);
    } else if (outputType == "prompter") {
      // The talent's screen, not the programme.
      renderPrompterView(outputIndex, hostDeckIndex, bounds);
    } else if (outputType == "presenter") {
      // Not the programme: what the OPERATOR needs to see. Same window, same
      // display picker, same arming -- a different picture.
      renderPresenterView(outputIndex, hostDeckIndex, bounds);
    } else {
      for (const auto& entry : outputLayers) {
        renderDeckLayerIntoOutput(outputIndex, entry.second, bounds);
        // ── THE TRANSITION, ON TOP OF THE INCOMING PICTURE ────────────────
        //
        // Here, in the compositor, because this is where every cue kind meets:
        // video, stills, patterns, browser, camera, NDI, capture -- they all
        // arrive as a frame. A transition written here works for all of them
        // without knowing what any of them are.
        //
        // It used to live in MediaEngine::render(), which nothing calls, so
        // every transition in the program was silently a cut no matter what
        // the operator chose.
        renderDeckTransitionIntoOutput(outputIndex, entry.second, bounds);
      }

      // Output window is always clean black — no status overlays or decorations.
      // The only things drawn here are the media content itself, cue overlays, and
      // the optional time/ID overlay that the operator explicitly enables.
      const Cue* activeCue = activeCuePtr(hostDeckIndex);

      if (activeCue && activeCue->kind == CueKind::Composite) {
        renderCompositeCuePlaceholder(runtime->outputRenderer, bounds, *activeCue, true);
      }

      // Audio-only cue: draw a centred waveform + info on the output window
      if (activeCue && activeCue->kind == CueKind::Audio) {
        int margin = renderW / 10;
        SDL_Rect wfRect {margin, renderH / 4, renderW - margin * 2, renderH / 3};
        const MediaEngine* eng = mediaEngineForDeck(hostDeckIndex);
        drawAudioCueVisual(runtime->outputRenderer, wfRect, *activeCue, eng);
        // Cue name
        drawText(runtime->outputRenderer, fontBase_, activeCue->name,
                 pal.light, wfRect.x, wfRect.y - 36);
        // Transport position + duration
        std::string posStr = (eng ? formatSeconds(eng->position()) : "0:00")
                           + "  /  " + formatSeconds(activeCue->duration);
        drawText(runtime->outputRenderer, fontSmall_, posStr,
                 pal.mid, wfRect.x, wfRect.y + wfRect.h + 10);
        // State badge
        std::string stateLbl = !eng ? "stopped"
                             : eng->state() == TransportState::Playing ? "playing"
                             : eng->state() == TransportState::Paused  ? "paused" : "stopped";
        drawText(runtime->outputRenderer, fontSmall_, stateLbl,
                 pal.dark, wfRect.x + wfRect.w - 60, wfRect.y - 36);
      }

      // Overlay layer stack — rendered bottom to top in push order.
      int overlaySlot = 0;
      for (int ovIdx : hostDeck.overlayActiveIndices) {
        if (ovIdx < 0 || ovIdx >= static_cast<int>(hostDeck.cues.size())) continue;
        const Cue& lc = hostDeck.cues[ovIdx];
        if (lc.kind == CueKind::LowerThird) {
          // Stack lower-thirds bottom-up: first slot at bottom, each extra one steps up.
          int barH = renderH / 6;
          int barY = renderH - barH - renderH / 20 - overlaySlot * (barH + 8);
          SDL_Rect bar {0, barY, renderW, barH};

          SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(runtime->outputRenderer, 8, 16, 24, static_cast<Uint8>(lc.lowerThirdBgAlpha));
          SDL_RenderFillRect(runtime->outputRenderer, &bar);

          // Coloured accent strip (hue shifts per slot for differentiation)
          static constexpr std::array<SDL_Color, 4> accentColors {{
            {155, 188,  15, 220},
            { 15, 155, 188, 220},
            {188,  15, 155, 220},
            {188, 155,  15, 220},
          }};
          SDL_Color acc = accentColors[static_cast<size_t>(overlaySlot) % accentColors.size()];
          SDL_SetRenderDrawColor(runtime->outputRenderer, acc.r, acc.g, acc.b, acc.a);
          SDL_Rect strip {bar.x, bar.y, 8, bar.h};
          SDL_RenderFillRect(runtime->outputRenderer, &strip);
          SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_NONE);

          std::string mainTxt = lc.lowerThirdText.empty() ? lc.name : lc.lowerThirdText;
          drawText(runtime->outputRenderer, fontLarge_, mainTxt,
                   {255, 255, 255, 255}, bar.x + 24, bar.y + 14);
          if (!lc.lowerThirdSubtext.empty()) {
            drawText(runtime->outputRenderer, fontBase_, lc.lowerThirdSubtext,
                     {200, 220, 200, 255}, bar.x + 26, bar.y + barH - 36);
          }
          ++overlaySlot;
        } else if (lc.kind == CueKind::Pip) {
          PipOverlayRuntime* pipRuntime = pipOverlayRuntimeForCue(hostDeckIndex, ovIdx);
          const DecodedFrame* pipFrame =
            (pipRuntime && pipRuntime->mediaEngine) ? pipRuntime->mediaEngine->currentFrame() : nullptr;
          if (pipFrame && pipFrame->width > 0 && pipFrame->height > 0 && !pipFrame->pixels.empty()) {
            renderOverlayFrameIntoOutput(*runtime, pipOverlayRuntimeKey(hostDeckIndex, ovIdx),
                                         *pipFrame, lc, bounds);
          }
        }
      }

      // Subtitle overlay
      if (activeCue && activeCue->subtitleEnabled &&
          (!activeCue->subtitlePath.empty() || !activeCue->subtitleStreamId.empty())) {
        const MediaEngine* subEngine = mediaEngineForDeck(hostDeckIndex);
        double playheadSec = subEngine ? subEngine->position() : 0.0;
        std::string subtitleKey = activeCue->subtitlePath.empty()
          ? (activeCue->path + "::" + activeCue->subtitleStreamId)
          : activeCue->subtitlePath;
        auto cacheIt = subtitleCache_.find(subtitleKey);
        if (cacheIt != subtitleCache_.end()) {
          const auto* entry = cacheIt->second.entryAtTime(playheadSec);
          if (entry) {
            std::string cleanText = deckboy::core::stripSubtitleTags(entry->text);
            auto lines = splitLines(cleanText);
            int lineH = 28;
                    int padY = 8;
            int totalTextH = static_cast<int>(lines.size()) * lineH;
            int bgH = totalTextH + padY * 2;
            int bgY = renderH - bgH - 40;
            SDL_Rect bgRect {0, bgY, renderW, bgH};
            SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0, 160);
            SDL_RenderFillRect(runtime->outputRenderer, &bgRect);
            SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_NONE);
            for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
              if (lines[li].empty()) continue;
              int tw = 0, th = 0;
              TTF_GetStringSize(fontBase_, lines[li].c_str(), 0, &tw, &th);
              int tx = (renderW - tw) / 2;
              int ty = bgY + padY + li * lineH;
              // Shadow
              drawText(runtime->outputRenderer, fontBase_, lines[li],
                       {0, 0, 0, 255}, tx + 2, ty + 2);
              // Text
              drawText(runtime->outputRenderer, fontBase_, lines[li],
                       {255, 255, 255, 255}, tx, ty);
            }
          }
        }
      }

      if (output.outputTimeOverlayEnabled || hostDeck.timeOverlayEnabled) {
        const MediaEngine* engine = mediaEngineForDeck(hostDeckIndex);
        std::string position = formatSeconds(engine ? engine->position() : 0.0);
        std::string total = formatSeconds(engine ? engine->duration() : 0.0);
        std::string timeLine = position + " / " + total;
        std::string cueIdLine = activeCue ? ("id: " + activeCue->id) : "id: --";
        std::string tcLine = "tc: " + formatTimecode(hostDeck.timecodeCurrentSeconds, hostDeck.timecodeFps);
        SDL_Rect overlay {26, 26, std::max(300, renderW / 3), 72};
        Primitives::drawFramedPanel(runtime->outputRenderer, overlay, {15, 56, 15, 204}, pal.light, pal.mid);
        drawText(runtime->outputRenderer, fontMono_, timeLine, pal.light, overlay.x + 14, overlay.y + 9);
        drawText(runtime->outputRenderer, fontSmall_, cueIdLine, pal.mid, overlay.x + 14, overlay.y + 34);
        drawText(runtime->outputRenderer, fontSmall_, tcLine, pal.mid, overlay.x + 14, overlay.y + 50);
      }
    }

    // Master video dimmer overlay
    if (project_.masterDimmer < 0.999) {
      SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0,
        static_cast<Uint8>((1.0 - project_.masterDimmer) * 255.0));
      SDL_RenderFillRect(runtime->outputRenderer, nullptr);
      SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_NONE);
    }
    float outputAlpha = std::clamp(output.outputAlpha, 0.0f, 1.0f);
    if (outputAlpha < 0.999f) {
      SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0,
        static_cast<Uint8>((1.0f - outputAlpha) * 255.0f));
      SDL_RenderFillRect(runtime->outputRenderer, nullptr);
      SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_NONE);
    }
    // THE HOUSE FRAME GOES ON LAST, AND INSIDE. Still the compositor's render
    // target here, so the matte and the bug are part of the one picture that
    // the recording, the stream, NDI and the program monitor are all taken
    // from -- rather than something painted on the window afterwards that only
    // the operator would ever see.
    if (usingCompositor) {
      drawOutputMatteAndOverlay(outputIndex, *runtime, renderW, renderH);
    }
    if (usingCompositor) {
      SDL_SetRenderTarget(runtime->outputRenderer, nullptr);
      if (!streamType) {
        SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0, 255);
        SDL_RenderClear(runtime->outputRenderer);
        presentOutputCompositorToWindow(outputIndex, width, height);
      }
    }
    bool streamRouteActive = output.streamEnabled && backendRoute.streamSupported;
    bool ndiRouteActive = (output.ndiEnabled || output.ndiKeyEnabled) && backendRoute.ndiSupported;
    bool deckLinkRouteActive = output.deckLinkEnabled && backendRoute.deckLinkSupported;
    bool spoutRouteActive = output.spoutEnabled && backendRoute.spoutSupported;
    if (output.streamEnabled && !backendRoute.streamSupported) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "stream backend unavailable");
    } else if ((output.ndiEnabled || output.ndiKeyEnabled) && !backendRoute.ndiSupported) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "ndi backend unavailable");
    } else if (output.deckLinkEnabled && !backendRoute.deckLinkSupported) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "decklink backend unavailable");
    } else if (output.spoutEnabled && !backendRoute.spoutSupported) {
      setOutputHealthState(outputIndex, OutputHealthState::Error, "spout backend unavailable");
    }
    if (!streamRouteActive) {
      stopOutputStreamRuntime(*runtime);
      resetOutputStreamFpsTelemetry(*runtime);
    }
    // ST 2110 needs no SDK, so it has no "supported" gate — the socket either
    // opens or it reports why.
    bool st2110RouteActive = output.st2110Enabled;
    bool needsEgressCapture =
      streamRouteActive || ndiRouteActive || deckLinkRouteActive || spoutRouteActive ||
      st2110RouteActive || std::clamp(output.outputDelayMs, 0, 5000) > 0;
    double fpsHint = 30.0;
    for (auto it = outputLayers.rbegin(); it != outputLayers.rend(); ++it) {
      const Cue* layerCue = activeCuePtr(it->second);
      if (layerCue && layerCue->kind == CueKind::Video) {
        fpsHint = std::max(1.0, layerCue->fps);
        break;
      }
    }
    // Hold the rate steady while a network stream is up. Done here, before
    // anything reads fpsHint, so the encoder's declared rate, the egress
    // capture interval and the samples-per-frame the audio is cut into cannot
    // disagree with each other -- they are all derived from this one number.
    if (runtime->streamLockedFps > 1.0 && streamRouteActive && !runtime->streamToFile) {
      fpsHint = runtime->streamLockedFps;
    }
    SDL_Rect egressRect {0, 0, renderW, renderH};
    if (usingCompositor) {
      egressRect.w = std::max(1, std::min(width, renderW));
      egressRect.h = std::max(1, std::min(height, renderH));
      if (project_.outputCanvasEnabled &&
          normalizeOutputLayoutMode(output.outputLayoutMode) == "span") {
        egressRect.x = std::clamp(hostDeck.canvasViewX, 0, std::max(0, renderW - egressRect.w));
        egressRect.y = std::clamp(hostDeck.canvasViewY, 0, std::max(0, renderH - egressRect.h));
      }
    }
    if (needsEgressCapture) {
      captureOutputFrameForEgress(outputIndex, *runtime, egressRect, fpsHint);
    } else {
      runtime->latestCapturedFrame = {};
      runtime->delayFrames.clear();
    }
    // Program-monitor tap for the control window, sampled from the same
    // composite this pass is about to present so the preview stays locked to
    // the output (see captureOutputPreviewTap).
    std::optional<int> tapOutput = previewTapOutputIndex();
    if (usingCompositor && tapOutput && *tapOutput == outputIndex) {
      captureOutputPreviewTap(*runtime, egressRect);
    } else if (runtime->previewTapSerial != 0) {
      releaseOutputPreviewTap(*runtime);
    }
    if (ndiRouteActive) {
      sendOutputNdiFrame(outputIndex, *runtime, width, height, fpsHint);
      // After the frame, not before: a receiver's tally refers to the picture
      // it is already showing, and polling first would act on the state that
      // preceded the frame we are about to send.
      pollOutputNdiTally(outputIndex, *runtime);
    }
    if (streamRouteActive) {
      sendOutputStreamFrame(outputIndex, width, height, fpsHint);
      recordOutputStreamFrameWritten(outputIndex);
    } else {
      resetOutputStreamFpsTelemetry(*runtime);
    }
    if (deckLinkRouteActive) {
      sendOutputDeckLinkFrame(outputIndex, *runtime, width, height, fpsHint);
    } else {
      shutdownOutputDeckLink(*runtime);
    }
    if (spoutRouteActive) {
      sendOutputSpoutFrame(outputIndex, *runtime, width, height);
    } else {
      shutdownOutputSpout(*runtime);
    }
    if (st2110RouteActive) {
      sendOutputSt2110Frame(outputIndex, *runtime, width, height, fpsHint);
    } else {
      shutdownOutputSt2110(*runtime);
    }
    if (!streamType) {
      SDL_RenderPresent(runtime->outputRenderer);
    }
    recordOutputFramePresented(outputIndex);
  }
