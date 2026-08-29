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
  // Get-or-create the per-deck zero-copy bridge: a persistent NV12
  // ID3D11Texture2D on this output renderer's device, wrapped once as an
  // SDL_Texture. Decoded d3d11va texture-array slices are GPU-copied into it
  // each frame advance — no CPU download, no re-upload.
  SDL_Texture* ensureLayerGpuTexture(OutputRuntime& outputRuntime,
                                     int sourceDeckIndex,
                                     int width,
                                     int height) {
    width &= ~1;
    height &= ~1;
    if (width <= 0 || height <= 0) {
      return nullptr;
    }
    auto texIt = outputRuntime.layerGpuTextures.find(sourceDeckIndex);
    if (texIt != outputRuntime.layerGpuTextures.end()) {
      auto sizeIt = outputRuntime.layerGpuTextureSizes.find(sourceDeckIndex);
      if (sizeIt != outputRuntime.layerGpuTextureSizes.end() &&
          sizeIt->second == std::make_pair(width, height)) {
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
      outputRuntime.layerGpuFrameIndices.erase(sourceDeckIndex);
    }
    void* texture2D = nullptr;
    SDL_Texture* wrapped = deckboy::libav::createWrappedNV12Texture(
      outputRuntime.outputRenderer, width, height, &texture2D);
    if (!wrapped) {
      return nullptr;
    }
    outputRuntime.layerGpuTextures[sourceDeckIndex] = wrapped;
    outputRuntime.layerGpuTexture2Ds[sourceDeckIndex] = texture2D;
    outputRuntime.layerGpuTextureSizes[sourceDeckIndex] = {width, height};
    outputRuntime.layerGpuFrameIndices.erase(sourceDeckIndex);
    return wrapped;
  }
#endif

  // Get-or-create the per-overlay bridge texture. Same format-aware rebuild
  // rule as ensureLayerBridgeTexture, keyed by overlay identity instead of
  // deck index.
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
    const bool dissolve = project_.vjBlendMode == "dissolve";
    if (deckIndex == deckB) {
      if (project_.vjBlendMode == "add") blendOut = SDL_BLENDMODE_ADD;
      else if (project_.vjBlendMode == "multiply") blendOut = SDL_BLENDMODE_MOD;
      return mix;
    }
    if (deckIndex == deckA) {
      return dissolve ? (1.0 - mix) : 1.0;
    }
    return 1.0;
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
          *outputRuntime, sourceDeckIndex, sourceFrame->width, sourceFrame->height);
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
            fxCtx.feedback = feedbackBufferForDeck(sourceDeckIndex);
            fxCtx.feedbackHold = !claimDeckFeedbackAdvance(sourceDeckIndex);
            // Any armed LFO, evaluated for this frame. Returns false and costs
            // nothing when the cue has none, which is almost every cue.
            std::vector<deckboy::effects::CueEffect> modulated;
            const bool moving = deckboy::effects::modulateCueEffectStack(
              sourceCue->effects, lfoSeconds_, lfoBeats_, modulated);
            deckboy::effects::applyCueEffectStack(
              outputRuntime->layerBridgeScratchPixels,
              moving ? modulated : sourceCue->effects, fxCtx);
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
    } else {
      for (const auto& entry : outputLayers) {
        renderDeckLayerIntoOutput(outputIndex, entry.second, bounds);
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
        bool _wfPending = false;
        WaveformPeaks peaks = getWaveformPeaks(resolvedCueFilesystemPathString(*activeCue, currentProjectFile_), _wfPending);
        double dur = activeCue->duration > 0.0 ? activeCue->duration : 1.0;
        const MediaEngine* eng = mediaEngineForDeck(hostDeckIndex);
        float playFrac = eng ? static_cast<float>(std::clamp(eng->position() / dur, 0.0, 1.0)) : -1.0f;
        float inFrac  = static_cast<float>(activeCue->inPointSeconds / dur);
        float outFrac = activeCue->outPointSeconds > 0.0
                      ? static_cast<float>(activeCue->outPointSeconds / dur) : 1.0f;
        drawWaveform(runtime->outputRenderer, wfRect, peaks, activeCue->audioChannels >= 2, playFrac, inFrac, outFrac,
                     activeCue->pausePoints, dur, waveformGainScale(*activeCue));
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
            int padX = 16;
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
