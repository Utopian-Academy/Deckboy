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
    float aoiL = std::clamp(output.aoiLeft,   0.0f, kAoiMaxEdge);
    float aoiR = std::clamp(output.aoiRight,  0.0f, kAoiMaxEdge);
    float aoiT = std::clamp(output.aoiTop,    0.0f, kAoiMaxEdge);
    float aoiB = std::clamp(output.aoiBottom, 0.0f, kAoiMaxEdge);
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

    // PIXEL FOR PIXEL: the region goes out at its own size at the window's
    // top-left, one raster pixel to one output pixel, instead of being scaled
    // to fill. Warp and orientation still apply, to that smaller quad.
    const bool pixelForPixel = hasAoi && output.aoiPixelForPixel;
    const float destW = pixelForPixel ? static_cast<float>(std::min(src.w, windowW))
                                      : static_cast<float>(windowW);
    const float destH = pixelForPixel ? static_cast<float>(std::min(src.h, windowH))
                                      : static_cast<float>(windowH);
    // FROM THE OUTPUT, not from its host deck. Warp and edge blend correct
    // for the screen this destination lands on, so one deck feeding a warped
    // projector and a clean stream no longer warps both.
    bool hasBlend = output.edgeBlendLeft > 0.0001f || output.edgeBlendRight > 0.0001f
      || output.edgeBlendTop > 0.0001f || output.edgeBlendBottom > 0.0001f;
    bool hasWarp = output.warpEnabled;
    std::string warpMode = normalizeWarpMode(output.warpMode);
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
      SDL_FPoint p1 {destW, 0.0f};
      SDL_FPoint p2 {destW, destH};
      SDL_FPoint p3 {0.0f, destH};
      if (hasWarp) {
        p0.x += output.warpTopLeftX;      p0.y += output.warpTopLeftY;
        p1.x += output.warpTopRightX;     p1.y += output.warpTopRightY;
        p2.x += output.warpBottomRightX;  p2.y += output.warpBottomRightY;
        p3.x += output.warpBottomLeftX;   p3.y += output.warpBottomLeftY;
      }

      if (hasWarp && warpGridActive(output) &&
          renderGridWarp(runtime->outputRenderer, runtime->compositorTexture, output, output,
                         usePerspectiveWarp, uvTL, uvTR, uvBR, uvBL, p0, p1, p2, p3, hasBlend)) {
        return;
      }
      if (usePerspectiveWarp) {
        if (renderPerspectiveWarp(runtime->outputRenderer, runtime->compositorTexture, output,
                                  uvTL, uvTR, uvBR, uvBL, p0, p1, p2, p3, hasBlend)) {
          return;
        }
      }

      // Feathering must be evaluated ACROSS the quad, not at its corners: four
      // vertices let SDL stretch a narrow edge ramp into a full-image fade.
      if (hasBlend && renderFeatheredQuad(runtime->outputRenderer, runtime->compositorTexture,
                                          output, uvTL, uvTR, uvBR, uvBL, p0, p1, p2, p3)) {
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

    const SDL_FRect destRect {0.0f, 0.0f, destW, destH};
    const SDL_FRect* dest = pixelForPixel ? &destRect : nullptr;
    const SDL_FRect srcF {static_cast<float>(src.x), static_cast<float>(src.y),
                          static_cast<float>(src.w), static_cast<float>(src.h)};
    if (hasOrientation) {
      SDL_RenderTextureRotated(runtime->outputRenderer, runtime->compositorTexture, &srcF, dest,
                       static_cast<double>(orientationDegrees), nullptr, SDL_FLIP_NONE);
    } else {
      SDL_RenderTexture(runtime->outputRenderer, runtime->compositorTexture, &srcF, dest);
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
                                        Uint32 format, SDL_Colorspace colorspace = SDL_COLORSPACE_SRGB) {
    if (width <= 0 || height <= 0) {
      return nullptr;
    }
    auto texIt = outputRuntime.layerBridgeTextures.find(sourceDeckIndex);
    bool needsRecreate = texIt == outputRuntime.layerBridgeTextures.end();
    if (!needsRecreate) {
      int prevW = outputRuntime.layerBridgeTextureWidths[sourceDeckIndex];
      int prevH = outputRuntime.layerBridgeTextureHeights[sourceDeckIndex];
      Uint32 prevFmt = outputRuntime.layerBridgeTextureFormats[sourceDeckIndex];
      needsRecreate = prevW != width || prevH != height || prevFmt != format ||
                      deckboyTextureColorspace(texIt->second) != colorspace;
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
        height, colorspace
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
      outputRuntime.layerBridgePixelPointers.erase(sourceDeckIndex);
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
                                     FramePixelFormat format, SDL_Colorspace colorspace) {
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
          fmtIt->second == format && deckboyTextureColorspace(texIt->second) == colorspace) {
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
      outputRuntime.outputRenderer, width, height, format, &texture2D, colorspace);
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
                                          Uint32 format, SDL_Colorspace colorspace = SDL_COLORSPACE_SRGB) {
    if (width <= 0 || height <= 0) {
      return nullptr;
    }
    auto texIt = outputRuntime.overlayBridgeTextures.find(overlayKey);
    bool needsRecreate = texIt == outputRuntime.overlayBridgeTextures.end();
    if (!needsRecreate) {
      int prevW = outputRuntime.overlayBridgeTextureWidths[overlayKey];
      int prevH = outputRuntime.overlayBridgeTextureHeights[overlayKey];
      Uint32 prevFmt = outputRuntime.overlayBridgeTextureFormats[overlayKey];
      needsRecreate = prevW != width || prevH != height || prevFmt != format ||
                      deckboyTextureColorspace(texIt->second) != colorspace;
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
        height, colorspace
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
      outputRuntime.transitionUploadStamps.erase(overlayKey);
      return texture;
    }
    return texIt->second;
  }

  // A geometry oscillator's swing this frame, as a fraction of its range:
  // -depth/2..+depth/2, or 0..depth for the one-sided Audio shape (silence
  // leaves the picture where the operator put it). 0 when it is off.
  double geoLfoSwing(const deckboy::effects::ParamLfo& lfo) const {
    if (!lfo.on) {
      return 0.0;
    }
    const double unit = deckboy::effects::lfoUnitValue(lfo, lfoSeconds_, lfoBeats_,
                                                       reactiveAudioLevel_);
    return lfo.shape == deckboy::effects::LfoShape::Audio
      ? unit * static_cast<double>(lfo.depth)
      : (unit - 0.5) * static_cast<double>(lfo.depth);
  }

  // For the 0-1 geometry (the crops): the effect parameters' own rule.
  float lfoApplyGeo(const deckboy::effects::ParamLfo& lfo, float base) const {
    return deckboy::effects::lfoApply(lfo, base, lfoSeconds_, lfoBeats_, reactiveAudioLevel_);
  }

  // WHERE A CUE'S PICTURE LANDS inside a target: the crop, the scale mode, the
  // cue's size, offset and rotation, and the geometry oscillators. One function,
  // because the compositor draws with it and the corner-pin editor puts its
  // handles with it -- two copies would put the handles where the picture is not.
  struct CuePlacement {
    SDL_Rect source {};
    SDL_Rect destination {};
    float rotationDegrees = 0.0f;
  };
  CuePlacement cuePlacementFor(const Cue* cue, int textureWidth, int textureHeight,
                               const SDL_Rect& target) const {
    float cropLeft = cue ? cue->cropLeft : 0.0f;
    float cropRight = cue ? cue->cropRight : 0.0f;
    float cropTop = cue ? cue->cropTop : 0.0f;
    float cropBottom = cue ? cue->cropBottom : 0.0f;
    // THE GEOMETRY OSCILLATORS, evaluated here because every picture of a cue
    // -- the output, a second screen, the preview -- is placed by this one
    // function. Same clock the effect LFOs read, so a size pulse and an
    // effect parameter on one cue stay in step.
    const bool geoLfo = cue && cueHasGeometryLfo(*cue);
    if (geoLfo) {
      const auto& L = cue->geometryLfo;
      cropLeft = lfoApplyGeo(L[kGeoLfoCropLeft], cropLeft);
      cropRight = lfoApplyGeo(L[kGeoLfoCropRight], cropRight);
      cropTop = lfoApplyGeo(L[kGeoLfoCropTop], cropTop);
      cropBottom = lfoApplyGeo(L[kGeoLfoCropBottom], cropBottom);
    }
    int cropL = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureWidth) * cropLeft)), 0, textureWidth - 1);
    int cropR = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureWidth) * cropRight)), 0, textureWidth - 1);
    int cropT = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureHeight) * cropTop)), 0, textureHeight - 1);
    int cropB = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureHeight) * cropBottom)), 0, textureHeight - 1);
    int srcW = std::max(1, textureWidth - cropL - cropR);
    int srcH = std::max(1, textureHeight - cropT - cropB);
    const SDL_Rect source {cropL, cropT, srcW, srcH};
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
    if (geoLfo) {
      const auto& L = cue->geometryLfo;
      // Position: depth 1 is half the frame either way.
      offsetX += static_cast<float>(geoLfoSwing(L[kGeoLfoOffsetX]) * target.w);
      offsetY += static_cast<float>(geoLfoSwing(L[kGeoLfoOffsetY]) * target.h);
      // Size, in OCTAVES: depth 1 is half size to double size. A linear swing
      // would shrink further than it grows and read as lopsided.
      const auto& sx = L[kGeoLfoScaleX];
      const auto& sy = L[kGeoLfoScaleY];
      const float fx = sx.on ? static_cast<float>(std::pow(2.0, geoLfoSwing(sx) * 2.0)) : 1.0f;
      const float fy = sy.on ? static_cast<float>(std::pow(2.0, geoLfoSwing(sy) * 2.0)) : fx;
      outputScaleX *= fx;
      outputScaleY *= fy;
      // Rotation: depth 1 is half a turn either way, so a saw spins it round.
      rotationDegrees += static_cast<float>(geoLfoSwing(L[kGeoLfoRotation]) * 360.0);
    }
    int drawW = std::max(1, static_cast<int>(std::round(srcW * baseScaleX * static_cast<double>(outputScaleX))));
    int drawH = std::max(1, static_cast<int>(std::round(srcH * baseScaleY * static_cast<double>(outputScaleY))));
    SDL_Rect destination {
      target.x + (target.w - drawW) / 2 + static_cast<int>(offsetX),
      target.y + (target.h - drawH) / 2 + static_cast<int>(offsetY),
      drawW,
      drawH
    };
    return CuePlacement {source, destination, rotationDegrees};
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
                                    SDL_BlendMode blendMode = SDL_BLENDMODE_BLEND,
                                    // The LAYER's own corner pin, when it has
                                    // one. Null for every caller that is not
                                    // compositing a mapped layer, which is
                                    // all of them but one.
                                    const OutputLayer* layerWarp = nullptr) {
    if (!renderer || !texture || textureWidth <= 0 || textureHeight <= 0) {
      return;
    }
    const CuePlacement placed = cuePlacementFor(cue, textureWidth, textureHeight, target);
    const SDL_Rect& source = placed.source;
    const SDL_Rect& destination = placed.destination;
    const float rotationDegrees = placed.rotationDegrees;
    SDL_SetTextureBlendMode(texture, blendMode);
    // Clip to target so Fill/Unscaled modes don't overflow into other UI elements
    SDL_Rect prevClip;
    bool hadClip = SDL_RenderClipEnabled(renderer);
    if (hadClip) SDL_GetRenderClipRect(renderer, &prevClip);
    // Keep the caller's reveal mask. Replacing it with target made wipes and
    // iris draw the whole outgoing picture until the last frame, then snap.
    SDL_Rect drawClip = target;
    if (hadClip && !SDL_GetRectIntersection(&prevClip, &target, &drawClip)) return;
    SDL_SetRenderClipRect(renderer, &drawClip);
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
    // -- THE LAYER'S CORNER PIN ------------------------------------------
    //
    // Four vertices instead of a rect, so a layer can be pinned onto a
    // surface that is not square-on to the projector. The OUTPUT's warp is
    // applied later over the finished raster; this one is about where the
    // content sits inside it, and the two compose.
    //
    // The offsets are fractions of the DESTINATION, so a pin survives the
    // layer being moved or resized -- which is the whole reason they are not
    // stored in pixels the way the output's are.
    //
    // Rotation is not applied here. A cue rotation and a corner pin are two
    // ways of saying the same thing and composing them silently produces a
    // quad nobody asked for; the pin wins, because it is the more specific
    // instruction. A rotated cue on a pinned layer is warned about below.
    if (layerWarp && layerWarp->warpEnabled) {
      const float dx = static_cast<float>(destination.x);
      const float dy = static_cast<float>(destination.y);
      const float dw = static_cast<float>(destination.w);
      const float dh = static_cast<float>(destination.h);
      SDL_FPoint p0 {dx + layerWarp->warpTopLeftX * dw,
                     dy + layerWarp->warpTopLeftY * dh};
      SDL_FPoint p1 {dx + dw + layerWarp->warpTopRightX * dw,
                     dy + layerWarp->warpTopRightY * dh};
      SDL_FPoint p2 {dx + dw + layerWarp->warpBottomRightX * dw,
                     dy + dh + layerWarp->warpBottomRightY * dh};
      SDL_FPoint p3 {dx + layerWarp->warpBottomLeftX * dw,
                     dy + dh + layerWarp->warpBottomLeftY * dh};
      const float u0 = static_cast<float>(source.x) / static_cast<float>(textureWidth);
      const float v0 = static_cast<float>(source.y) / static_cast<float>(textureHeight);
      const float u1 = static_cast<float>(source.x + source.w) /
                       static_cast<float>(textureWidth);
      const float v1 = static_cast<float>(source.y + source.h) /
                       static_cast<float>(textureHeight);
      const SDL_FColor kOpaque {1.0f, 1.0f, 1.0f, 1.0f};
      SDL_Vertex verts[4] {
        {p0, kOpaque, SDL_FPoint {u0, v0}},
        {p1, kOpaque, SDL_FPoint {u1, v0}},
        {p2, kOpaque, SDL_FPoint {u1, v1}},
        {p3, kOpaque, SDL_FPoint {u0, v1}},
      };
      // PERSPECTIVE: the output warp's projective mesh, fed this layer's quad.
      // No edge blend on a layer, so the deck it would read blends from is
      // never looked at.
      if (layerWarp->warpPerspective) {
        static const Deck kNoBlend {};
        if (renderPerspectiveWarp(renderer, texture, kNoBlend,
                                  SDL_FPoint {u0, v0}, SDL_FPoint {u1, v0},
                                  SDL_FPoint {u1, v1}, SDL_FPoint {u0, v1},
                                  p0, p1, p2, p3, false)) {
          SDL_SetRenderClipRect(renderer, hadClip ? &prevClip : nullptr);
          return;
        }
      }
      const int indices[6] {0, 1, 2, 0, 2, 3};
      if (SDL_RenderGeometry(renderer, texture, verts, 4, indices, 6)) {
        SDL_SetRenderClipRect(renderer, hadClip ? &prevClip : nullptr);
        return;
      }
      // Geometry can fail on a renderer that cannot do it. Falling through
      // to the flat blit shows the picture unmapped, which is wrong but
      // visible -- and visible beats a black layer nobody can diagnose.
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
  // THE CROSSFADER, as the compositor sees it: what to multiply a stack
  // entry's opacity by, and what to blend it with.
  //
  // Takes the STACK POSITION rather than a deck, because an output's
  // crossfader is about its own layers -- the same playlist can sit on two
  // outputs and be faded on one of them only. vjLayerGain could not express
  // that: it knew about decks and about one output.
  double outputCrossfadeGain(int outputIndex, int stackIndex,
                             SDL_BlendMode& blendOut) const {
    blendOut = SDL_BLENDMODE_BLEND;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return 1.0;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    if (!output.crossfadeEnabled) {
      return 1.0;
    }
    const int stackSize = static_cast<int>(output.layerDecks.size()) + 1;
    const int from = std::clamp(output.crossfadeFrom, 0, stackSize - 1);
    const int to = std::clamp(output.crossfadeTo, 0, stackSize - 1);
    if (from == to) {
      return 1.0;   // a crossfader with one end is not a crossfader
    }
    const double mix = std::clamp(output.crossfadeMix, 0.0, 1.0);
    // The blend belongs to the layer being faded IN. Index 0 is the base,
    // which has no OutputLayer record and therefore no blend of its own.
    std::string mode = "dissolve";
    if (to > 0 && to - 1 < static_cast<int>(output.layerDecks.size())) {
      const std::string& layerMode = output.layerDecks[to - 1].blendMode;
      if (!layerMode.empty()) {
        mode = layerMode;
      }
    }
    // ONLY DISSOLVE FADES THE OUTGOING SIDE. Every other mode leaves it at
    // full and brings the incoming one in over it, which is what makes add
    // and multiply look like themselves rather than like a crossfade
    // wearing a costume. Carried over from VJ mode intact.
    const bool dissolve = mode == "dissolve";
    if (stackIndex == to) {
      blendOut = vjBlendModeFor(mode);
      return mix;
    }
    if (stackIndex == from) {
      return dissolve ? (1.0 - mix) : 1.0;
    }
    return 1.0;
  }

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
    // The label helper insets the rect it is given before it measures, so the
    // pill is grown by that inset as well -- sized to the text alone, the
    // longest caption ("PREVIOUS") came out as "PREVIO...".
    const SDL_Rect probe {at.x, at.y, measuredTextWidth(font, text) + padX * 2, at.h};
    const int inset = std::max(0, probe.w - safeTextRect(probe).w);
    const int width = std::min(at.w, probe.w + inset);
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
      renderDeckWithTransitionIntoOutput(outputIndex, deckIndex, inner);
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

  // ── A LOWER THIRD ─────────────────────────────────────────────────────
  //
  // Drawn straight into the output's renderer, like the rest of a text cue,
  // over NOTHING: no card, so whatever sits under this playlist in the
  // output's layer stack shows round it.
  //
  // One progress value from 0 (not there) to 1 (fully on) drives every move,
  // in and out alike: IN counts up from the cue's start, OUT counts down from
  // the moment it was told to go -- `outStartedAt`, on the same transport
  // clock, or its time on screen running out. So OUT with "slide from left"
  // leaves the way it came.
  // WHERE EACH CHARACTER ENDS, in bytes: a letter with the marks and joiners
  // riding on it. A typewriter reveals these, never bytes -- an Arabic letter
  // is two bytes and a Japanese one three, so counting bytes cut letters in
  // half and typed those scripts two and three times faster than English.
  static std::vector<std::size_t> characterEnds(const std::string& s) {
    namespace bidi = deckboy::core::bidi;
    std::vector<std::size_t> ends;
    for (std::size_t at = 0; at < s.size();) {
      char32_t cp = 0;
      at += bidi::decodeUtf8(s, at, cp);
      while (at < s.size()) {
        char32_t next = 0;
        const std::size_t len = bidi::decodeUtf8(s, at, next);
        const auto cls = bidi::classOf(next);
        if (cls != bidi::BidiClass::NSM && cls != bidi::BidiClass::BN) break;
        at += len;
      }
      ends.push_back(at);
    }
    return ends;
  }

  // THE PIECES A WOBBLING LINE MOVES AS, left to right as they are seen.
  //
  // A character with its marks, in the scripts whose letters stand alone:
  // Latin, Greek, Cyrillic, Han, kana, Hangul. A WORD everywhere else -- in
  // Arabic, Persian, the Indic scripts, Thai -- because letters drawn one at a
  // time there lose their joins and conjuncts, and a wobbling name that has
  // come apart is not the same name. Right-to-left runs are reversed so the
  // line wobbles in place rather than backwards.
  static std::vector<std::string> wobbleUnits(const std::string& line) {
    namespace bidi = deckboy::core::bidi;
    auto standsAlone = [](char32_t cp) {
      return cp < 0x0590 ||                       // Latin, Greek, Cyrillic, IPA, marks
             (cp >= 0x1E00 && cp <= 0x1FFF) ||    // Latin and Greek extended
             (cp >= 0x2000 && cp <= 0x2BFF) ||    // punctuation and symbols
             (cp >= 0x2E80 && cp <= 0x9FFF) ||    // CJK, kana
             (cp >= 0xAC00 && cp <= 0xD7AF) ||    // Hangul
             (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
             (cp >= 0x20000 && cp <= 0x3FFFF);
    };
    std::vector<std::string> out;
    for (const bidi::Run& run : bidi::visualRuns(line)) {
      const std::string text = line.substr(run.begin, run.end - run.begin);
      bool simple = true;
      for (std::size_t at = 0; at < text.size();) {
        char32_t cp = 0;
        at += bidi::decodeUtf8(text, at, cp);
        if (!standsAlone(cp)) simple = false;
      }
      std::vector<std::string> units;
      for (std::size_t at = 0; at < text.size();) {
        const std::size_t start = at;
        char32_t cp = 0;
        at += bidi::decodeUtf8(text, at, cp);
        if (simple || cp == ' ') {
          // The character, and any marks or joiners riding on it.
          while (at < text.size()) {
            char32_t next = 0;
            const std::size_t len = bidi::decodeUtf8(text, at, next);
            const auto cls = bidi::classOf(next);
            if (cls != bidi::BidiClass::NSM && cls != bidi::BidiClass::BN) break;
            at += len;
          }
        } else {
          // The word.
          while (at < text.size() && text[at] != ' ') {
            char32_t next = 0;
            at += bidi::decodeUtf8(text, at, next);
          }
        }
        units.push_back(text.substr(start, at - start));
      }
      if (run.rightToLeft()) std::reverse(units.begin(), units.end());
      out.insert(out.end(), units.begin(), units.end());
    }
    return out;
  }

  void renderLowerThirdIntoOutput(SDL_Renderer* renderer, const Cue& cue,
                                  const SDL_Rect& target, double seconds,
                                  double outStartedAt) {
    const LowerThirdDesign& d = cue.lowerThird;
    // The words: the first two lines of the body.
    std::string title = cue.textBody;
    std::string subtitle;
    if (const std::size_t nl = title.find('\n'); nl != std::string::npos) {
      subtitle = title.substr(nl + 1);
      title = title.substr(0, nl);
      if (const std::size_t nl2 = subtitle.find('\n'); nl2 != std::string::npos) {
        subtitle = subtitle.substr(0, nl2);
      }
    }
    for (std::string* s : {&title, &subtitle}) {
      if (!s->empty() && s->back() == '\r') s->pop_back();
    }
    if (title.empty() && subtitle.empty()) {
      return;
    }
    // A NAME WRITTEN RIGHT TO LEFT STARTS AT THE RIGHT. An Arabic or Persian
    // strap is laid out as the mirror of a Latin one -- words against the
    // right edge of their box, the accent on the side the eye starts from --
    // wherever on the frame the side setting puts it.
    const bool rtlText =
      deckboy::render::shaping::resolvesRightToLeft(title.empty() ? subtitle : title);

    // ── WHERE IT IS IN ITS LIFE ─────────────────────────────────────────
    double outAt = outStartedAt;
    if (outAt < 0.0 && d.holdSeconds > 0.0) {
      outAt = std::max(0.0, d.inSeconds) + d.holdSeconds;
    }
    const bool leaving = outAt >= 0.0 && seconds >= outAt;
    const LowerThirdMove move = leaving ? d.moveOut : d.moveIn;
    double p = 1.0;
    if (leaving) {
      p = d.outSeconds > 0.0 ? 1.0 - std::clamp((seconds - outAt) / d.outSeconds, 0.0, 1.0)
                             : 0.0;
    } else {
      p = d.inSeconds > 0.0 ? std::clamp(seconds / d.inSeconds, 0.0, 1.0) : 1.0;
    }
    if (move == LowerThirdMove::None) {
      p = p > 0.0 ? 1.0 : 0.0;
    }
    if (p <= 0.0) {
      return;
    }
    auto easeOut = [](double x) { return 1.0 - std::pow(1.0 - x, 3.0); };
    // The subtitle trails the title a touch on the moves that travel, so the
    // two arrive as two things rather than one slab.
    const double pSub = std::clamp((p - 0.12) / 0.88, 0.0, 1.0);

    // ── MEASURE ─────────────────────────────────────────────────────────
    const double unit = static_cast<double>(target.h);
    const double titleH = unit * 0.058 * std::clamp(d.size, 0.5, 2.0);
    // The pixel face reads larger than the sans at one height, so ARCADE's
    // mono subtitle sits nearer the title's size or it looks like a footnote.
    const double subH = titleH * (d.look == LowerThirdLook::Arcade ? 0.78 : 0.62);
    const double pad = titleH * 0.45;
    const SDL_Color barColour = lowerThirdColour(d.bar);
    const SDL_Color accent = lowerThirdColour(d.accent);
    auto inkOn = [](SDL_Color fill) {
      const int lum = (fill.r * 299 + fill.g * 587 + fill.b * 114) / 1000;
      return lum > 150 ? SDL_Color {18, 22, 30, 255} : SDL_Color {250, 250, 250, 255};
    };
    const bool boxless = d.look == LowerThirdLook::Line || d.look == LowerThirdLook::Hairline;
    const bool arcade = d.look == LowerThirdLook::Arcade;
    // The materials some looks bring with them rather than take from the
    // palette: a scroll is parchment whatever colour the bar is set to, and a
    // neon sign is dark so the tube can glow.
    const SDL_Color parchment {238, 224, 184, 255};
    const SDL_Color parchmentDark {196, 170, 112, 255};
    const SDL_Color sepia {74, 46, 20, 255};
    const SDL_Color neonBody {static_cast<Uint8>(barColour.r * 0.22 + 8),
                              static_cast<Uint8>(barColour.g * 0.22 + 8),
                              static_cast<Uint8>(barColour.b * 0.22 + 12), 255};
    const SDL_Color neonInk {static_cast<Uint8>(accent.r + (255 - accent.r) * 0.55),
                             static_cast<Uint8>(accent.g + (255 - accent.g) * 0.55),
                             static_cast<Uint8>(accent.b + (255 - accent.b) * 0.55), 255};
    // ARCADE writes the title in the accent -- the border's colour -- the way a
    // game writes its headings, and the subtitle in whichever ink reads on the box.
    SDL_Color titleInk = boxless ? SDL_Color {250, 250, 250, 255}
                       : arcade ? accent
                       : inkOn(d.look == LowerThirdLook::Tag ? accent : barColour);
    SDL_Color subInk = boxless ? SDL_Color {225, 228, 235, 255}
                     : inkOn(d.look == LowerThirdLook::Boxes ? accent : barColour);
    switch (d.look) {
      case LowerThirdLook::Split:  titleInk = inkOn(accent); break;
      case LowerThirdLook::Scroll: titleInk = sepia;
                                   subInk = SDL_Color {112, 78, 40, 255}; break;
      case LowerThirdLook::Neon:   titleInk = neonInk;
                                   subInk = SDL_Color {220, 224, 232, 255}; break;
      case LowerThirdLook::Comic:  titleInk = inkOn(barColour);
                                   subInk = inkOn(barColour); break;
      default: break;
    }
    // And its faces are the desk's own: the pixel face for the title, mono under it.
    TTF_Font* titleFont = arcade && fontPixelTitle_ ? fontPixelTitle_ : fontLarge_;
    TTF_Font* subFont = arcade && fontMono_ ? fontMono_ : fontLarge_;

    // TYPEWRITER shows the title a letter at a time once the bar is in --
    // a LETTER, not a byte: an Arabic letter is two bytes and a Japanese one
    // three, and cutting between them drew a broken character every frame.
    // Marks stay with the letter they sit on.
    std::string shownTitle = title;
    if (move == LowerThirdMove::Typewriter) {
      const double letters = std::clamp((p - 0.3) / 0.7, 0.0, 1.0);
      const std::vector<std::size_t> ends = characterEnds(title);
      const std::size_t keep = std::min(ends.size(), static_cast<std::size_t>(
        std::lround(letters * static_cast<double>(ends.size()))));
      shownTitle = title.substr(0, keep == 0 ? 0 : ends[keep - 1]);
    }
    const TextTextureEntry* titleTex = title.empty() ? nullptr
      : cachedTextTexture(renderer, titleFont, title, titleInk);
    const TextTextureEntry* shownTex = shownTitle.empty() ? nullptr
      : (shownTitle == title ? titleTex
                             : cachedTextTexture(renderer, titleFont, shownTitle, titleInk));
    const TextTextureEntry* subTex = subtitle.empty() ? nullptr
      : cachedTextTexture(renderer, subFont, subtitle, subInk);
    auto widthAt = [](const TextTextureEntry* t, double h) {
      return (t && t->h > 0) ? scaledLabelWidth(*t, h) : 0.0;
    };
    const double titleW = widthAt(titleTex, titleH);   // the FULL title: the box
    const double subW = widthAt(subTex, subH);          // does not grow as it types

    // ── LAY IT OUT, at rest ─────────────────────────────────────────────
    struct Box { double x, y, w, h; SDL_Color fill; int alpha; };
    std::vector<Box> boxes;
    std::vector<std::size_t> scrollRolls;   // the rolled ends, which travel as it unrolls
    double scrollRollW = 0.0;
    double titleX = 0.0, titleY = 0.0, subX = 0.0, subY = 0.0;
    double blockW = 0.0, blockH = 0.0;
    const double accentW = titleH * 0.16;
    // Two different questions with one answer each. WHERE the strap sits is
    // the side setting's alone. Which way it is laid out INSIDE mirrors for a
    // right-hand strap (accent on the outer edge) and for right-to-left words.
    const bool placedRight = d.side == 2;
    const bool rightSide = placedRight || rtlText;
    switch (d.look) {
      case LowerThirdLook::Boxes: {
        const double tw = titleW + pad * 2.0;
        const double th = titleH + pad;
        const double sw = subTex ? subW + pad * 1.6 : 0.0;
        const double sh = subTex ? subH + pad * 0.7 : 0.0;
        const double indent = pad * 0.8;
        blockW = std::max(tw, indent + sw);
        blockH = th + sh;
        const double tx = rightSide ? blockW - tw : 0.0;
        const double sx = rightSide ? blockW - indent - sw : indent;
        boxes.push_back({tx, 0.0, tw, th, barColour, 255});
        if (subTex) boxes.push_back({sx, th, sw, sh, accent, 255});
        titleX = tx + pad; titleY = pad * 0.5;
        subX = sx + pad * 0.8; subY = th + pad * 0.35;
        break;
      }
      case LowerThirdLook::Line: {
        const double rule = std::max(2.0, titleH * 0.11);
        blockW = std::max(titleW, subW);
        blockH = titleH + pad * 0.35 + rule + (subTex ? pad * 0.35 + subH : 0.0);
        titleX = rightSide ? blockW - titleW : 0.0; titleY = 0.0;
        boxes.push_back({0.0, titleH + pad * 0.35, blockW, rule, accent, 255});
        subX = rightSide ? blockW - subW : 0.0;
        subY = titleH + pad * 0.7 + rule;
        break;
      }
      case LowerThirdLook::Tag: {
        const double tw = titleW + pad * 2.0;
        const double th = titleH + pad;
        const double sw = subTex ? subW + pad * 1.6 : 0.0;
        const double sh = subTex ? subH + pad * 0.7 : 0.0;
        blockW = tw + sw;
        blockH = th;
        const double tx = rightSide ? sw : 0.0;
        const double sx = rightSide ? 0.0 : tw;
        boxes.push_back({tx, 0.0, tw, th, accent, 255});
        if (subTex) boxes.push_back({sx, (th - sh) / 2.0, sw, sh, barColour, 235});
        titleX = tx + pad; titleY = pad * 0.5;
        subX = sx + pad * 0.8; subY = (th - sh) / 2.0 + pad * 0.35;
        break;
      }
      case LowerThirdLook::Glass: {
        blockW = static_cast<double>(target.w);
        blockH = pad * 1.4 + titleH + (subTex ? subH + pad * 0.25 : 0.0);
        boxes.push_back({0.0, 0.0, blockW, blockH, barColour, 175});
        boxes.push_back({0.0, 0.0, blockW, std::max(2.0, titleH * 0.06), accent, 255});
        const double margin = target.w * 0.055;
        const double textW = std::max(titleW, subW);
        const double tx = d.side == 0 ? margin
                        : d.side == 2 ? blockW - margin - textW
                                      : (blockW - textW) / 2.0;
        // The full-width look places the words by the side setting itself,
        // so right to left only decides which edge of that block the two
        // lines share.
        titleX = d.side == 1 ? (blockW - titleW) / 2.0
               : rightSide ? tx + textW - titleW : tx;
        subX = d.side == 1 ? (blockW - subW) / 2.0
             : rightSide ? tx + textW - subW : tx;
        titleY = pad * 0.7;
        subY = titleY + titleH + pad * 0.25;
        break;
      }
      case LowerThirdLook::Arcade: {
        // Three boxes, back to front: a hard shadow down and right in a dark
        // shade of the accent, the accent as a thick border, the colour inside.
        const double border = std::max(2.0, titleH * 0.10);
        const double drop = titleH * 0.22;
        const double gap = subTex ? pad * 0.45 : 0.0;
        const double innerW = std::max(titleW, subW) + pad * 2.0;
        const double innerH = pad * 1.1 + titleH + (subTex ? gap + subH : 0.0) + pad * 0.9;
        blockW = innerW + border * 2.0;
        blockH = innerH + border * 2.0;
        const SDL_Color shade {static_cast<Uint8>(accent.r * 0.42), static_cast<Uint8>(accent.g * 0.42),
                               static_cast<Uint8>(accent.b * 0.42), 255};
        boxes.push_back({drop, drop, blockW, blockH, shade, 255});
        boxes.push_back({0.0, 0.0, blockW, blockH, accent, 255});
        boxes.push_back({border, border, innerW, innerH, barColour, 255});
        const double inX = border + pad;
        titleX = rightSide ? blockW - border - pad - titleW : inX;
        subX = rightSide ? blockW - border - pad - subW : inX;
        titleY = border + pad * 1.1;
        subY = titleY + titleH + gap;
        break;
      }
      // ── CLEAN ─────────────────────────────────────────────────────────
      case LowerThirdLook::Split: {
        // A news strap: the name on a solid accent block, the role on a
        // strip of the bar colour under it, square and flush.
        const double tw = titleW + pad * 2.0;
        const double th = titleH + pad;
        const double sw = subTex ? std::max(tw, subW + pad * 2.0) : 0.0;
        const double sh = subTex ? subH + pad * 0.8 : 0.0;
        blockW = std::max(tw, sw);
        blockH = th + sh;
        const double tx = rightSide ? blockW - tw : 0.0;
        boxes.push_back({tx, 0.0, tw, th, accent, 255});
        if (subTex) boxes.push_back({rightSide ? blockW - sw : 0.0, th, sw, sh, barColour, 245});
        titleX = tx + pad; titleY = pad * 0.5;
        subX = rightSide ? blockW - pad - subW : pad;
        subY = th + pad * 0.4;
        break;
      }
      case LowerThirdLook::Card: {
        // A card with its corners taken off, a soft shadow under it, and a
        // small accent dot at the name. Picked from the desk it arrives on
        // paper, which is the look; the bar colour still decides it.
        const double dot = titleH * 0.34;
        const double textW = std::max(titleW, subW);
        blockW = pad * 2.6 + dot + textW;
        blockH = pad * 1.6 + titleH + (subTex ? subH + pad * 0.2 : 0.0);
        const double cut = std::min(pad * 0.5, blockH * 0.2);
        const double off = titleH * 0.10;
        const SDL_Color shadow {0, 0, 0, 255};
        boxes.push_back({off + cut, off, blockW - cut * 2.0, blockH, shadow, 60});
        boxes.push_back({off, off + cut, blockW, blockH - cut * 2.0, shadow, 60});
        boxes.push_back({cut, 0.0, blockW - cut * 2.0, blockH, barColour, 250});
        boxes.push_back({0.0, cut, cut, blockH - cut * 2.0, barColour, 250});
        boxes.push_back({blockW - cut, cut, cut, blockH - cut * 2.0, barColour, 250});
        const double dotX = rightSide ? blockW - pad - dot : pad;
        titleY = pad * 0.8;
        boxes.push_back({dotX, titleY + (titleH - dot) / 2.0, dot, dot, accent, 255});
        const double tx = rightSide ? pad : pad * 1.6 + dot;
        titleX = rightSide ? blockW - pad * 1.6 - dot - titleW : tx;
        subX = rightSide ? blockW - pad * 1.6 - dot - subW : tx;
        subY = titleY + titleH + pad * 0.2;
        break;
      }
      case LowerThirdLook::Hairline: {
        // No box: a fine rule above the words in the accent, a square at its
        // start, and room to breathe.
        const double rule = std::max(1.0, titleH * 0.035);
        const double sq = titleH * 0.22;
        blockW = std::max(titleW, subW) + sq * 1.6;
        blockH = sq + pad * 0.5 + titleH + (subTex ? pad * 0.2 + subH : 0.0);
        boxes.push_back({rightSide ? blockW - sq : 0.0, 0.0, sq, sq, accent, 255});
        boxes.push_back({0.0, (sq - rule) / 2.0, blockW, rule, accent, 230});
        titleY = sq + pad * 0.5;
        titleX = rightSide ? blockW - titleW : 0.0;
        subX = rightSide ? blockW - subW : 0.0;
        subY = titleY + titleH + pad * 0.2;
        break;
      }
      // ── MAGICAL ───────────────────────────────────────────────────────
      case LowerThirdLook::Sparkle: {
        // The bar, and stars round it that twinkle on the cue's clock -- so
        // they scrub, and two outputs twinkle together.
        const double textW = std::max(titleW, subW);
        blockW = textW + pad * 2.0 + accentW;
        blockH = pad * 1.4 + titleH + (subTex ? subH + pad * 0.25 : 0.0);
        boxes.push_back({0.0, 0.0, blockW, blockH, barColour, 240});
        boxes.push_back({rightSide ? blockW - accentW : 0.0, 0.0, accentW, blockH, accent, 255});
        const double tx = rightSide ? pad : accentW + pad;
        titleX = rightSide ? blockW - accentW - pad - titleW : tx;
        subX = rightSide ? blockW - accentW - pad - subW : tx;
        titleY = pad * 0.7;
        subY = titleY + titleH + pad * 0.25;
        // Places round the edge, fixed so a star does not wander; only its
        // size and brightness move.
        static const double kSpots[][2] = {
          {0.06, -0.30}, {0.30, -0.18}, {0.55, -0.34}, {0.82, -0.22}, {1.04, 0.10},
          {0.96, 1.16}, {0.70, 1.28}, {0.42, 1.14}, {0.16, 1.24}, {-0.05, 0.62},
        };
        int k = 0;
        for (const auto& spot : kSpots) {
          const double phase = seconds * 1.4 + k * 0.37;
          const double tw = 0.5 + 0.5 * std::sin(phase * 2.0 * 3.14159265358979);
          const double r = titleH * (0.14 + 0.24 * tw);
          const double sx = spot[0] * blockW;
          const double sy = spot[1] * blockH;
          const SDL_Color star = (k % 3 == 0) ? accent : SDL_Color {255, 250, 225, 255};
          const int a = static_cast<int>(110 + 145 * tw);
          const double thin = std::max(1.5, r * 0.26);
          boxes.push_back({sx - r, sy - thin / 2.0, r * 2.0, thin, star, a});
          boxes.push_back({sx - thin / 2.0, sy - r, thin, r * 2.0, star, a});
          const double d2 = r * 0.45;
          boxes.push_back({sx - d2 / 2.0, sy - d2 / 2.0, d2, d2, star, a});
          ++k;
        }
        break;
      }
      case LowerThirdLook::Neon: {
        // A dark sign, an accent tube round it drawn as layers of glow.
        const double tube = std::max(2.0, titleH * 0.07);
        const double textW = std::max(titleW, subW);
        blockW = textW + pad * 2.4;
        blockH = pad * 1.6 + titleH + (subTex ? subH + pad * 0.3 : 0.0);
        boxes.push_back({0.0, 0.0, blockW, blockH, neonBody, 225});
        for (int layer = 4; layer >= 0; --layer) {
          const double o = tube * (layer * 1.1);
          const int a = layer == 0 ? 255 : static_cast<int>(150 / layer);
          const SDL_Color c = layer == 0 ? neonInk : accent;
          const double t = tube + o * 0.5;
          boxes.push_back({-o, -o, blockW + o * 2.0, t, c, a});
          boxes.push_back({-o, blockH + o - t, blockW + o * 2.0, t, c, a});
          boxes.push_back({-o, -o, t, blockH + o * 2.0, c, a});
          boxes.push_back({blockW + o - t, -o, t, blockH + o * 2.0, c, a});
        }
        titleX = rightSide ? blockW - pad * 1.2 - titleW : pad * 1.2;
        subX = rightSide ? blockW - pad * 1.2 - subW : pad * 1.2;
        titleY = pad * 0.8;
        subY = titleY + titleH + pad * 0.3;
        break;
      }
      case LowerThirdLook::Comic: {
        // A heavy black outline, a halftone shadow down and right, and the
        // whole panel tilted (below, with the move).
        const double line = std::max(3.0, titleH * 0.12);
        const double textW = std::max(titleW, subW);
        const double innerW = textW + pad * 2.2;
        const double innerH = pad * 1.4 + titleH + (subTex ? subH + pad * 0.2 : 0.0);
        blockW = innerW + line * 2.0;
        blockH = innerH + line * 2.0;
        const double drop = titleH * 0.30;
        const double step = std::max(4.0, titleH * 0.17);
        int row = 0;
        for (double y = drop; y < blockH + drop; y += step, ++row) {
          for (double x = drop + ((row & 1) ? step / 2.0 : 0.0); x < blockW + drop; x += step) {
            if (x < blockW - line && y < blockH - line) continue;   // hidden behind the panel
            const double dotR = step * 0.32;
            boxes.push_back({x - dotR, y - dotR, dotR * 2.0, dotR * 2.0,
                             SDL_Color {12, 12, 12, 255}, 200});
          }
        }
        boxes.push_back({0.0, 0.0, blockW, blockH, SDL_Color {12, 12, 12, 255}, 255});
        boxes.push_back({line, line, innerW, innerH, barColour, 255});
        boxes.push_back({line, line + innerH - line * 0.8, innerW, line * 0.8, accent, 255});
        titleX = rightSide ? blockW - line - pad * 1.1 - titleW : line + pad * 1.1;
        subX = rightSide ? blockW - line - pad * 1.1 - subW : line + pad * 1.1;
        titleY = line + pad * 0.6;
        subY = titleY + titleH + pad * 0.2;
        break;
      }
      case LowerThirdLook::Scroll: {
        // Parchment, with a rolled end each side standing proud of it and a
        // ribbon of the accent under the name.
        const double roll = titleH * 0.42;
        const double textW = std::max(titleW, subW);
        const double bodyW = textW + pad * 3.0;
        const double bodyH = pad * 1.6 + titleH + (subTex ? subH + pad * 0.3 : 0.0);
        const double over = titleH * 0.22;
        blockW = bodyW + roll * 2.0;
        blockH = bodyH + over * 2.0;
        boxes.push_back({roll, over, bodyW, bodyH, parchment, 250});
        boxes.push_back({roll, over, bodyW, std::max(1.0, titleH * 0.04), parchmentDark, 255});
        boxes.push_back({roll, over + bodyH - std::max(1.0, titleH * 0.04), bodyW,
                         std::max(1.0, titleH * 0.04), parchmentDark, 255});
        scrollRollW = roll;
        for (int side = 0; side < 2; ++side) {
          const double rx = side == 0 ? 0.0 : blockW - roll;
          for (const Box& part : {Box {rx, 0.0, roll, blockH, parchmentDark, 255},
                                  Box {rx + roll * 0.18, 0.0, roll * 0.28, blockH, parchment, 200},
                                  Box {rx + roll * 0.70, 0.0, roll * 0.12, blockH, sepia, 90}}) {
            scrollRolls.push_back(boxes.size());
            boxes.push_back(part);
          }
        }
        titleX = roll + (bodyW - titleW) / 2.0;
        titleY = over + pad * 0.7;
        boxes.push_back({roll + (bodyW - titleW) / 2.0, titleY + titleH + pad * 0.05,
                         titleW, std::max(1.5, titleH * 0.06), accent, 220});
        subX = roll + (bodyW - subW) / 2.0;
        subY = titleY + titleH + pad * 0.3;
        break;
      }
      case LowerThirdLook::Bar:
      default: {
        const double textW = std::max(titleW, subW);
        blockW = textW + pad * 2.0 + accentW;
        blockH = pad * 1.4 + titleH + (subTex ? subH + pad * 0.25 : 0.0);
        boxes.push_back({0.0, 0.0, blockW, blockH, barColour, 240});
        boxes.push_back({rightSide ? blockW - accentW : 0.0, 0.0, accentW, blockH, accent, 255});
        const double tx = rightSide ? pad : accentW + pad;
        titleX = rightSide ? blockW - accentW - pad - titleW : tx;
        subX = rightSide ? blockW - accentW - pad - subW : tx;
        titleY = pad * 0.7;
        subY = titleY + titleH + pad * 0.25;
        break;
      }
    }

    // Where the block sits on the frame.
    const double marginX = target.w * 0.055;
    const double bottom = target.h * std::clamp(d.height, 0.0, 0.8);
    double blockX = target.x + marginX;
    if (d.look == LowerThirdLook::Glass) {
      blockX = target.x;
    } else if (d.side == 1) {
      blockX = target.x + (target.w - blockW) / 2.0;
    } else if (placedRight) {
      blockX = target.x + target.w - marginX - blockW;
    }
    const double blockY = target.y + target.h - bottom - blockH;

    // ── THE MOVE ────────────────────────────────────────────────────────
    const double e = easeOut(p);
    double alpha = 1.0;
    double dx = 0.0, dy = 0.0, scale = 1.0;
    double barFrac = 1.0;       // Grow / Typewriter: how much of each box is drawn
    double textAlpha = 1.0;
    double wipe = 1.0;
    switch (move) {
      case LowerThirdMove::Fade:
        alpha = p;
        break;
      case LowerThirdMove::SlideLeft:
        dx = -(1.0 - e) * (blockX - target.x + blockW + 8.0);
        break;
      case LowerThirdMove::SlideRight:
        dx = (1.0 - e) * (target.x + target.w - blockX + 8.0);
        break;
      case LowerThirdMove::SlideUp:
        dy = (1.0 - e) * (target.y + target.h - blockY + 8.0);
        break;
      case LowerThirdMove::Wipe:
        wipe = e;
        break;
      case LowerThirdMove::Grow:
        barFrac = easeOut(std::clamp(p / 0.6, 0.0, 1.0));
        textAlpha = std::clamp((p - 0.5) / 0.5, 0.0, 1.0);
        break;
      case LowerThirdMove::Typewriter:
        barFrac = easeOut(std::clamp(p / 0.35, 0.0, 1.0));
        textAlpha = 1.0;
        break;
      case LowerThirdMove::Pop: {
        // Back-out: past 1 and back, the overshoot that makes it spring.
        const double c1 = 1.70158;
        const double c3 = c1 + 1.0;
        const double x = p - 1.0;
        scale = std::max(0.0, 1.0 + c3 * x * x * x + c1 * x * x);
        alpha = std::clamp(p * 3.0, 0.0, 1.0);
        break;
      }
      default:
        break;
    }
    // ARCADE BREATHES. Once it is fully in and until it starts to leave, a slow
    // bob and a small tilt, on the cue's own clock -- so it scrubs, and two
    // outputs showing it tilt together. Measured from the end of the move in,
    // so the move itself is untouched.
    double angle = 0.0;
    if (arcade && !leaving && p >= 1.0) {
      const double idle = std::max(0.0, seconds - std::max(0.0, d.inSeconds));
      const double w = 2.0 * 3.14159265358979 * idle / 1.2;
      const double ramp = std::clamp(idle / 0.3, 0.0, 1.0);   // eases into the wobble
      angle = ramp * (-1.5 + 1.0 * std::sin(w));
      dy += ramp * -titleH * 0.08 * (0.5 - 0.5 * std::cos(w));
    }
    // COMIC sits at a jaunty angle from the start, through every move.
    if (d.look == LowerThirdLook::Comic) {
      angle = -3.0;
    }
    // NEON STRIKES. For half a second after it arrives the sign stutters on
    // and off the way a tube does catching, then holds -- on the cue clock,
    // so it scrubs.
    if (d.look == LowerThirdLook::Neon && !leaving) {
      const double since = seconds - std::max(0.0, d.inSeconds) * 0.6;
      if (since > 0.0 && since < 0.55) {
        static const bool kPattern[] = {true, false, true, true, false, true, false, true, true, true, false};
        const int slot = static_cast<int>(since / 0.05);
        if (!kPattern[std::min(slot, 10)]) {
          alpha *= 0.25;
        }
      }
    }
    const double cx = blockX + blockW / 2.0;
    const double cy = blockY + blockH / 2.0;
    auto place = [&](double x, double y, double w, double h) {
      SDL_FRect r;
      r.x = static_cast<float>(cx + (blockX + x - cx) * scale + dx);
      r.y = static_cast<float>(cy + (blockY + y - cy) * scale + dy);
      r.w = static_cast<float>(w * scale);
      r.h = static_cast<float>(h * scale);
      return r;
    };

    // Clipped to the frame, and to the wipe when there is one.
    SDL_Rect previousClip {};
    const bool hadClip = SDL_RenderClipEnabled(renderer);
    if (hadClip) {
      SDL_GetRenderClipRect(renderer, &previousClip);
    }
    SDL_Rect clip = target;
    if (wipe < 1.0) {
      const int shown = static_cast<int>(std::lround(blockW * wipe));
      clip = SDL_Rect {static_cast<int>(std::floor(blockX)), target.y,
                       std::max(0, shown), target.h};
      SDL_GetRectIntersection(&clip, &target, &clip);
    }
    SDL_SetRenderClipRect(renderer, &clip);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // THE SCROLL UNROLLS. Its two rolled ends start together in the middle and
    // travel out to the edges as it comes in, the parchment and the words
    // showing only between them, and roll back up on the way out -- the same
    // progress the move runs on, so it scrubs. Fully open it is drawn exactly
    // as it always was.
    double unroll = 1.0;
    if (!scrollRolls.empty() && p < 1.0) {
      unroll = p * p * (3.0 - 2.0 * p);
    }
    const bool unrolling = unroll < 1.0;
    const double openHalf = (blockW / 2.0 - scrollRollW) * unroll;
    if (unrolling) {
      const SDL_FRect span = place(blockW / 2.0 - openHalf, 0.0, openHalf * 2.0, blockH);
      SDL_Rect open {static_cast<int>(std::floor(span.x)), clip.y,
                     static_cast<int>(std::ceil(span.w)), clip.h};
      SDL_GetRectIntersection(&open, &clip, &open);
      SDL_SetRenderClipRect(renderer, &open);
    }
    auto isRoll = [&](std::size_t i) {
      return std::find(scrollRolls.begin(), scrollRolls.end(), i) != scrollRolls.end();
    };

    // Where the block's centre lands after the move: everything tilts about it.
    const float pivotX = static_cast<float>(cx + dx);
    const float pivotY = static_cast<float>(cy + dy);
    const double rad = angle * 3.14159265358979 / 180.0;
    const float cosA = static_cast<float>(std::cos(rad));
    const float sinA = static_cast<float>(std::sin(rad));
    for (std::size_t bi = 0; bi < boxes.size(); ++bi) {
      const Box& b = boxes[bi];
      if (unrolling && isRoll(bi)) {
        continue;   // drawn last, over the words, where they have travelled to
      }
      double w = b.w * barFrac;
      double x = rightSide ? b.x + (b.w - w) : b.x;
      SDL_FRect r = place(x, b.y, w, b.h);
      const Uint8 a8 = static_cast<Uint8>(std::clamp(b.alpha * alpha, 0.0, 255.0));
      if (angle == 0.0) {
        SDL_SetRenderDrawColor(renderer, b.fill.r, b.fill.g, b.fill.b, a8);
        SDL_RenderFillRect(renderer, &r);
        continue;
      }
      // A tilted box is two triangles; FillRect can only draw it square.
      const SDL_FColor col {b.fill.r / 255.0f, b.fill.g / 255.0f, b.fill.b / 255.0f, a8 / 255.0f};
      const float xs[4] = {r.x, r.x + r.w, r.x + r.w, r.x};
      const float ys[4] = {r.y, r.y, r.y + r.h, r.y + r.h};
      SDL_Vertex v[4];
      for (int k = 0; k < 4; ++k) {
        const float ox = xs[k] - pivotX, oy = ys[k] - pivotY;
        v[k].position = SDL_FPoint {pivotX + ox * cosA - oy * sinA, pivotY + ox * sinA + oy * cosA};
        v[k].color = col;
        v[k].tex_coord = SDL_FPoint {0.0f, 0.0f};
      }
      const int idx[6] = {0, 1, 2, 0, 2, 3};
      SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
    }

    auto drawWords = [&](const TextTextureEntry* tex, double x, double y, double h,
                         double a) {
      if (!tex || !tex->texture || tex->h <= 0 || a <= 0.0) {
        return;
      }
      const SDL_FRect scaled = scaledLabelRect(*tex, x, y, h);
      const double w = scaled.w;
      const double drawY = scaled.y;
      const double drawH = scaled.h;
      if (boxless) {
        // A soft shadow, because a line of words with no box behind it has to
        // read over a white shirt as well as a dark stage.
        SDL_FRect shadow = place(x + h * 0.05, drawY + h * 0.05, w, drawH);
        SDL_SetTextureColorMod(tex->texture, 0, 0, 0);
        SDL_SetTextureAlphaMod(tex->texture, static_cast<Uint8>(std::clamp(150.0 * a, 0.0, 255.0)));
        SDL_RenderTexture(renderer, tex->texture, nullptr, &shadow);
        SDL_SetTextureColorMod(tex->texture, 255, 255, 255);
      }
      SDL_FRect dst = place(x, drawY, w, drawH);
      SDL_SetTextureAlphaMod(tex->texture, static_cast<Uint8>(std::clamp(255.0 * a, 0.0, 255.0)));
      if (angle == 0.0) {
        SDL_RenderTexture(renderer, tex->texture, nullptr, &dst);
      } else {
        const SDL_FPoint about {pivotX - dst.x, pivotY - dst.y};
        SDL_RenderTextureRotated(renderer, tex->texture, nullptr, &dst, angle, &about, SDL_FLIP_NONE);
      }
      SDL_SetTextureAlphaMod(tex->texture, 255);
    };
    const bool travels = move == LowerThirdMove::SlideLeft || move == LowerThirdMove::SlideRight ||
                         move == LowerThirdMove::SlideUp;
    // A right-to-left title types from the right: the part shown so far
    // keeps to the title's right edge rather than its left.
    const double shownX = (rtlText && shownTex && shownTex != titleTex)
                            ? titleX + titleW - widthAt(shownTex, titleH)
                            : titleX;
    drawWords(shownTex, shownX, titleY, titleH, alpha * textAlpha);
    // The subtitle trails on the moves that travel: drawn a little further
    // back along the same path.
    // Only where the subtitle has no box of its own to stay inside: on the
    // boxed looks the box travels with the block, and words trailing out of
    // it look like a fault rather than a flourish.
    if (travels && boxless && pSub < 1.0) {
      // The title's offset is (1 - e) of the way along the path; the
      // subtitle's is (1 - eSub), so it sits the difference further back.
      const double eSub = easeOut(pSub);
      double extraX = 0.0;
      double extraY = 0.0;
      if (move == LowerThirdMove::SlideLeft) {
        extraX = (eSub - e) * (blockX - target.x + blockW + 8.0);
      } else if (move == LowerThirdMove::SlideRight) {
        extraX = (e - eSub) * (target.x + target.w - blockX + 8.0);
      } else {
        extraY = (e - eSub) * (target.y + target.h - blockY + 8.0);
      }
      drawWords(subTex, subX + extraX, subY + extraY, subH, alpha);
    } else {
      drawWords(subTex, subX, subY, subH,
                alpha * (move == LowerThirdMove::Typewriter
                           ? std::clamp((p - 0.85) / 0.15, 0.0, 1.0) : textAlpha));
    }

    if (unrolling) {
      SDL_SetRenderClipRect(renderer, &clip);
      const double mid = blockW / 2.0;
      for (std::size_t i : scrollRolls) {
        const Box& b = boxes[i];
        // The left end's inner edge sits at mid - openHalf, the right end's at
        // mid + openHalf.
        const double shift = b.x < mid ? (mid - openHalf - scrollRollW)
                                       : (mid + openHalf) - (blockW - scrollRollW);
        SDL_FRect rr = place(b.x + shift, b.y, b.w, b.h);
        SDL_SetRenderDrawColor(renderer, b.fill.r, b.fill.g, b.fill.b,
                               static_cast<Uint8>(std::clamp(b.alpha * alpha, 0.0, 255.0)));
        SDL_RenderFillRect(renderer, &rr);
      }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderClipRect(renderer, hadClip ? &previousClip : nullptr);
  }

  // ── TEXT AS A SOURCE ──────────────────────────────────────────────────
  //
  // Words on the screen as the deck's PICTURE, not an overlay on someone
  // else's: a title card, a holding slide, a scrolling notice, a crawl.
  //
  // Everything moves on the cue's TRANSPORT position rather than a wall
  // clock. That is what makes a text cue scrub, pause and loop like any other
  // cue -- and it is why two outputs showing the same deck cannot drift apart,
  // which a wall clock would guarantee they eventually did.
  void renderTextCueIntoOutput(SDL_Renderer* renderer, const Cue& cue,
                               const SDL_Rect& target, double seconds,
                               double lowerThirdOutAt = -1.0) {
    if (!renderer || !fontLarge_ || target.w <= 0 || target.h <= 0) {
      return;
    }
    if (cue.lowerThird.on) {
      renderLowerThirdIntoOutput(renderer, cue, target, seconds, lowerThirdOutAt);
      return;
    }
    const double speed = std::clamp(cue.textSpeed, 0.05, 20.0);
    const double t = std::max(0.0, seconds) * speed;

    // The card behind the words. Alpha 0 means the text sits over whatever is
    // already on the output, which is how a text cue becomes a caption.
    if (cue.textBgAlpha > 0) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0,
                             static_cast<Uint8>(std::clamp(cue.textBgAlpha, 0, 255)));
      SDL_RenderFillRect(renderer, &target);
    }

    std::vector<std::string> lines;
    {
      std::string body = cue.textBody;
      std::size_t start = 0;
      while (start <= body.size()) {
        std::size_t nlPos = body.find('\n', start);
        if (nlPos == std::string::npos) nlPos = body.size();
        std::string line = body.substr(start, nlPos - start);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        lines.push_back(line);
        start = nlPos + 1;
      }
    }
    if (lines.empty()) {
      return;
    }

    // TYPEWRITER REVEALS CHARACTERS, so it is applied before anything is
    // measured -- the block has to be laid out from what is actually visible,
    // or the text would jump as each letter arrived.
    if (cue.textAnimation == CueTextAnimation::Typewriter) {
      std::size_t budget = static_cast<std::size_t>(std::max(0.0, t * 18.0));
      for (std::string& line : lines) {
        const std::vector<std::size_t> ends = characterEnds(line);
        if (budget >= ends.size()) {
          budget -= ends.size();
        } else {
          line = line.substr(0, budget == 0 ? 0 : ends[budget - 1]);
          budget = 0;
        }
      }
    }

    // SIZED AS A FRACTION OF THE RASTER, never in points: a card that reads on
    // a 1080 screen has to read on a 2160 one, and a point size cannot promise
    // that. The font is rendered once at its own size and scaled, which is
    // also what keeps one cached texture per line rather than one per size.
    const double lineH = std::max(1.0, target.h * std::clamp(cue.textSizePct, 1.0, 100.0) / 100.0);
    const double lineStep = lineH * 1.18;
    double blockH = lineStep * static_cast<double>(lines.size());

    double alpha = 1.0;
    double originY = target.y + (target.h - blockH) / 2.0;
    double crawlX = 0.0;

    switch (cue.textAnimation) {
      case CueTextAnimation::FadeIn:
        alpha = std::clamp(t, 0.0, 1.0);
        break;
      case CueTextAnimation::ScrollUp:
        // Starts below the frame and leaves above it, so a credit roll runs
        // clean off both edges rather than popping.
        originY = target.y + target.h - (t * lineStep * 2.0);
        break;
      case CueTextAnimation::Crawl:
        // One line, right to left. The whole block width has to clear the
        // frame before it wraps, so nothing is ever half on screen at the
        // start of a pass.
        crawlX = target.w - std::fmod(t * target.w * 0.25,
                                      static_cast<double>(target.w) * 2.0);
        blockH = lineStep;
        originY = target.y + (target.h - blockH) / 2.0;
        break;
      case CueTextAnimation::Pulse:
        // Never all the way out: a holding slide that vanishes reads as a
        // fault, and the point of this one is to prove the machine is alive.
        alpha = 0.72 + 0.28 * std::sin(t * 2.2);
        break;
      case CueTextAnimation::Wobble:
        // Nothing to set up: wobble is per CHARACTER, handled in the draw
        // loop below. The block itself stays exactly where a still card
        // would be, so turning it on never moves the words off their mark.
        break;
      case CueTextAnimation::Typewriter:
      case CueTextAnimation::None:
        break;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // ── WOBBLE: EVERY CHARACTER ON ITS OWN ORBIT ──────────────────────────
    //
    // The one animation here that moves the letters AGAINST each other rather
    // than the block as a whole, which is why it cannot ride the cached
    // per-line texture the others use: each glyph is drawn on its own, at a
    // phase taken from its position in the line.
    //
    // The line is MEASURED FIRST and only then drawn, so alignment is the
    // alignment of the resting line -- a right-aligned wobble that took its
    // width from the wobbling glyphs would breathe in and out from the edge.
    if (cue.textAnimation == CueTextAnimation::Wobble) {
      const double amp = lineH * 0.14;
      for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty()) {
          continue;
        }
        // The pieces that move -- see wobbleUnits -- in the order they are
        // seen. These used to be BYTES, so every letter outside plain ASCII
        // came out as two or three broken boxes, an accented Latin one too.
        const std::vector<std::string> units = wobbleUnits(lines[i]);
        std::vector<const TextTextureEntry*> glyphs;
        std::vector<double> widths;
        double lineW = 0.0;
        double scale = 1.0;
        for (const std::string& unit : units) {
          const bool blank = unit.find_first_not_of(" \t") == std::string::npos;
          const TextTextureEntry* g =
            blank ? nullptr : cachedTextTexture(renderer, fontLarge_, unit, cue.textColor);
          glyphs.push_back(g);
          if (!g || g->h <= 0) {
            widths.push_back(lineH * 0.3 * static_cast<double>(std::max<std::size_t>(1, unit.size())));
            lineW += widths.back();
            continue;
          }
          scale = lineH / static_cast<double>(g->lineH > 0 ? g->lineH : g->h);
          widths.push_back(g->w * scale);
          lineW += widths.back();
        }
        // Same fit-both-ways correction as the non-wobble loop above, and
        // the same reason: lineW so far is sized from lineH alone and can
        // run past target.w on a long line. Rescale the glyph widths (and
        // the shared per-glyph scale the draw loop below reads) together so
        // the whole line — and the wobble amplitude, in step — shrinks
        // rather than spilling off the frame.
        double lineAmp = amp;
        if (lineW > target.w && lineW > 0.0) {
          const double corr = target.w / lineW;
          scale *= corr;
          lineW *= corr;
          lineAmp *= corr;
          for (double& wd : widths) {
            wd *= corr;
          }
        }
        double x = target.x + (target.w - lineW) / 2.0;
        if (cue.textAlign == 0) {
          x = target.x + target.w / 24.0;
        } else if (cue.textAlign == 2) {
          x = target.x + target.w - lineW - target.w / 24.0;
        }
        const double baseY = originY + lineStep * static_cast<double>(i);
        for (std::size_t c = 0; c < glyphs.size(); ++c) {
          const TextTextureEntry* g = glyphs[c];
          if (g && g->texture && g->h > 0) {
            const double phase = t * 3.1 + static_cast<double>(c) * 0.7 +
                                 static_cast<double>(i) * 1.3;
            const int w = std::max(1, static_cast<int>(std::lround(g->w * scale)));
            const int h = std::max(1, static_cast<int>(std::lround(g->h * scale)));
            SDL_FRect dst {
              static_cast<float>(x + std::cos(phase * 0.8) * lineAmp * 0.5),
              static_cast<float>(baseY - g->top * scale + std::sin(phase) * lineAmp),
              static_cast<float>(w), static_cast<float>(h)};
            SDL_SetTextureAlphaMod(g->texture, 255);
            SDL_RenderTexture(renderer, g->texture, nullptr, &dst);
          }
          x += widths[c];
        }
      }
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
      return;
    }

    for (std::size_t i = 0; i < lines.size(); ++i) {
      if (lines[i].empty()) {
        continue;
      }
      const TextTextureEntry* entry =
        cachedTextTexture(renderer, fontLarge_, lines[i], cue.textColor);
      if (!entry || !entry->texture || entry->h <= 0) {
        continue;
      }
      // FIT BOTH WAYS, not just the line height. Scaling purely to lineH
      // (a fraction of target.h) sizes the text correctly top-to-bottom but
      // leaves its width wherever a long line naturally lands -- nothing
      // clamped that to target.w, so a long enough line ran off both edges
      // of whatever was drawing it, preview and output alike. Reported as
      // the text preview escaping the preview window at a smaller UI scale,
      // which is exactly when a line that used to just barely fit stops
      // fitting.
      double scale = lineH / static_cast<double>(entry->lineH > 0 ? entry->lineH : entry->h);
      const double maxW = std::max(1.0, static_cast<double>(target.w));
      if (entry->w * scale > maxW) {
        scale = maxW / static_cast<double>(entry->w);
      }
      const int w = std::max(1, static_cast<int>(std::lround(entry->w * scale)));
      const int h = std::max(1, static_cast<int>(std::lround(entry->h * scale)));
      int x = target.x + (target.w - w) / 2;              // centre
      if (cue.textAlign == 0) {
        x = target.x + target.w / 24;                     // left, with a margin
      } else if (cue.textAlign == 2) {
        x = target.x + target.w - w - target.w / 24;      // right
      }
      if (cue.textAnimation == CueTextAnimation::Crawl) {
        x = target.x + static_cast<int>(std::lround(crawlX));
      }
      const int y = static_cast<int>(std::lround(originY + lineStep * static_cast<double>(i) -
                                                 entry->top * scale));
      // Off the frame entirely: nothing to draw, and nothing to pay for.
      if (y + h < target.y || y > target.y + target.h) {
        continue;
      }
      SDL_SetTextureAlphaMod(entry->texture,
                             static_cast<Uint8>(std::clamp(alpha, 0.0, 1.0) * 255.0));
      SDL_FRect dst {static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(w), static_cast<float>(h)};
      SDL_RenderTexture(renderer, entry->texture, nullptr, &dst);
      // PUT BACK, because the texture is SHARED: the cache hands the same one
      // to the control window and to every other output, and a leftover alpha
      // would follow it there.
      SDL_SetTextureAlphaMod(entry->texture, 255);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  }

  // ── A MULTIVIEW ON A SCREEN ───────────────────────────────────────────
  //
  // One tile per playlist, each drawn by the same function that draws a layer
  // -- so every cue kind works in a tile because every cue kind works in a
  // layer, and nothing is downloaded from the GPU to get here.
  //
  // NO PROGRAMME TILE. A multiview output showing "the programme" while being
  // an output itself is a question with no good answer (which programme? this
  // one?), and a switcher's wall shows SOURCES. The programme has its own
  // screen.
  // THE SAME WALL AS THE DESK'S. The tiles are the operator's multiview
  // plan -- the windows arranged in the preview's MULTI view, programme tile,
  // labels and meters included -- not a second, fixed grid of its own. A
  // multiview sent to a screen or down the Web Monitor shows what was
  // arranged, which is the only reason to send it anywhere.
  void renderMultiviewIntoOutput(int outputIndex, const SDL_Rect& bounds) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer) {
      return;
    }
    const std::vector<MultiviewTile> plan = multiviewTilePlan();
    const int tileCount = static_cast<int>(plan.size());
    if (tileCount <= 0 || bounds.w <= 0 || bounds.h <= 0) {
      return;
    }
    int cols = 1;
    while (cols * cols < tileCount) {
      ++cols;
    }
    const int rows = (tileCount + cols - 1) / cols;
    const int gap = std::max(2, bounds.w / 240);
    const int tileW = (bounds.w - gap * (cols - 1)) / cols;
    const int tileH = (bounds.h - gap * (rows - 1)) / rows;
    if (tileW <= 8 || tileH <= 8) {
      return;
    }
    // The programme tile is the programme output's own stack, base first.
    const int programmeOutput = primaryProgrammeOutputIndex(outputIndex);
    for (int i = 0; i < tileCount; ++i) {
      const MultiviewTile& spec = plan[static_cast<std::size_t>(i)];
      const int col = i % cols;
      const int row = i / cols;
      SDL_Rect tile {bounds.x + col * (tileW + gap), bounds.y + row * (tileH + gap),
                     tileW, tileH};
      const int d = multiviewTileDeck(spec);   // -1 programme, -2 empty
      // AUDITION AND PRELOAD STILL HOLD A DECK OFF, the same rule the
      // compositor follows -- a deck being looked at privately must not
      // appear on a wall in the room.
      if (d >= 0) {
        if (!deckIsHeldOffOutput(d)) {
          renderDeckLayerIntoOutput(outputIndex, d, tile);
        }
      } else if (d == -1 && programmeOutput >= 0 &&
                 programmeOutput < static_cast<int>(project_.outputs.size())) {
        const OutputTarget& prog = project_.outputs[static_cast<std::size_t>(programmeOutput)];
        const int host = std::clamp(prog.hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
        if (!deckIsHeldOffOutput(host)) {
          renderDeckLayerIntoOutput(outputIndex, host, tile);
        }
        for (const OutputLayer& layer : prog.layerDecks) {
          if (layer.deckIndex >= 0 && layer.deckIndex < static_cast<int>(project_.decks.size()) &&
              !deckIsHeldOffOutput(layer.deckIndex)) {
            const SDL_Rect placed {
              tile.x + static_cast<int>(std::lround(layer.x * tile.w)),
              tile.y + static_cast<int>(std::lround(layer.y * tile.h)),
              std::max(1, static_cast<int>(std::lround(layer.w * tile.w))),
              std::max(1, static_cast<int>(std::lround(layer.h * tile.h)))};
            renderDeckLayerIntoOutput(outputIndex, layer.deckIndex, placed);
          }
        }
      }
      // A border, and the name, so the wall says which is which.
      SDL_SetRenderDrawColor(runtime->outputRenderer, 40, 40, 48, 255);
      SDL_Rect edges[] = {
        {tile.x, tile.y, tile.w, 1},
        {tile.x, tile.y + tile.h - 1, tile.w, 1},
        {tile.x, tile.y, 1, tile.h},
        {tile.x + tile.w - 1, tile.y, 1, tile.h},
      };
      for (const SDL_Rect& edge : edges) {
        SDL_FRect r {static_cast<float>(edge.x), static_cast<float>(edge.y),
                     static_cast<float>(edge.w), static_cast<float>(edge.h)};
        SDL_RenderFillRect(runtime->outputRenderer, &r);
      }
      // SIZED FROM THE TILE, not from uiScaled().
      //
      // This is drawn into an OUTPUT -- a projector, a card, a stream -- and
      // the operator's desktop scale has nothing to do with that raster. A
      // label that is 18 UI pixels tall is a different fraction of a 720p
      // tile than of a 2160p one. Proportional to the tile is the only
      // measure that means the same thing on every screen.
      const int stripH = std::clamp(tileH / 8, 12, 40);
      if (spec.label && fontSmall_ && tileH > stripH + 10) {
        SDL_Rect strip {tile.x, tile.y + tile.h - stripH, tile.w, stripH};
        SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0, 170);
        SDL_FRect sr {static_cast<float>(strip.x), static_cast<float>(strip.y),
                      static_cast<float>(strip.w), static_cast<float>(strip.h)};
        SDL_RenderFillRect(runtime->outputRenderer, &sr);
        SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_NONE);
        drawTextSafe(runtime->outputRenderer, fontSmall_,
                     SDL_Rect {strip.x + 4, strip.y + 1, strip.w - 8, strip.h - 2},
                     d >= 0 ? deckLabel(d) : multiviewTileSourceLabel(spec), pal.light);
      }
      // THE METER, where the desk's tile has one: a bar up the right edge,
      // proportional to the tile like the label, in the desk's colours.
      if (spec.vuMeter && tileW > 20 && tileH > 20) {
        const int meterW = std::max(3, tileW / 60);
        const SDL_Rect meter {tile.x + tile.w - meterW - 2, tile.y + 2, meterW, tile.h - 4};
        Primitives::fillRect(runtime->outputRenderer, meter, SDL_Color {0, 0, 0, 200});
        const double level = std::clamp(multiviewTileLevel01(spec), 0.0, 1.0);
        const int litH = static_cast<int>(std::lround(meter.h * level));
        if (litH > 0) {
          Primitives::fillRect(runtime->outputRenderer,
            SDL_Rect {meter.x, meter.y + meter.h - litH, meter.w, litH},
            level > 0.92 ? SDL_Color {220, 60, 50, 255}
            : level > 0.75 ? SDL_Color {230, 180, 60, 255} : SDL_Color {70, 200, 110, 255});
        }
      }
    }
  }

  // The layer record for a deck on an output, or null when this deck is the
  // output's HOST rather than one of its layers. The host has no OutputLayer,
  // so it has no corner pin of its own -- the output's own warp is its
  // mapping, which is exactly the right split.
  // 0 is the base, 1..N are the layers over it -- the same numbering the
  // crossfader and the routing menu use. -1 when this deck is not on that
  // output at all, which a crossfader must treat as neither of its ends.
  int stackPositionFor(int outputIndex, int deckIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return -1;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    if (output.hostDeckIndex == deckIndex) {
      return 0;
    }
    for (std::size_t i = 0; i < output.layerDecks.size(); ++i) {
      if (output.layerDecks[i].deckIndex == deckIndex) {
        return static_cast<int>(i) + 1;
      }
    }
    return -1;
  }

  const OutputLayer* layerRecordFor(int outputIndex, int deckIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return nullptr;
    }
    for (const OutputLayer& layer : project_.outputs[outputIndex].layerDecks) {
      if (layer.deckIndex == deckIndex) {
        return &layer;
      }
    }
    return nullptr;
  }

  // DIAGNOSTIC ONLY, added chasing a live report that SEEK/TAKE on one layer
  // deck blanks the OTHER decks sharing its output. Logs once on a
  // TRANSITION to or from "this layer drew nothing this frame" rather than
  // every frame it stays that way, so it stays quiet in normal operation and
  // only speaks when a layer actually drops. See
  // OutputRuntime::layerWasVisibleForDiagnostics.
  void noteLayerVisibility(OutputRuntime& rt, int outputIndex, int deckIndex,
                           bool visible, const char* reason) {
    auto it = rt.layerWasVisibleForDiagnostics.find(deckIndex);
    const bool was = it == rt.layerWasVisibleForDiagnostics.end() ? true : it->second;
    if (was != visible) {
      renderDiagnosticLog("output " + std::to_string(outputIndex + 1) + " deck " +
                          std::to_string(deckIndex + 1) +
                          (visible ? " resumed drawing"
                                   : (std::string(" stopped drawing: ") + reason)));
    }
    rt.layerWasVisibleForDiagnostics[deckIndex] = visible;
  }

  // `layerSourceOutputIndex` is the output whose LAYER LIST this draw belongs
  // to, which is not always the output being drawn. A mirroring destination --
  // a recording, a stream, an NDI sender, a second screen showing the
  // programme -- composites the SOURCE output's stack onto its own raster. Its
  // own layerDecks is empty, so looking the layer record up by the rendering
  // index found nothing and silently dropped the layer's corner pin: the
  // programme window was mapped and everything downstream of it was not.
  // -1 means 'the same output', which is every non-mirroring case.
  void renderDeckLayerIntoOutput(int outputIndex, int sourceDeckIndex, const SDL_Rect& target,
                                 int layerSourceOutputIndex = -1) {
    OutputRuntime* outputRuntime = runtimeForOutput(outputIndex);
    if (!outputRuntime || !outputRuntime->outputRenderer) {
      return;
    }
    if (sourceDeckIndex < 0 || sourceDeckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Cue* sourceCue = activeCuePtr(sourceDeckIndex);
    if (!sourceCue) {
      noteLayerVisibility(*outputRuntime, outputIndex, sourceDeckIndex, false, "no active cue");
      return;
    }
    // Resolved once and passed to whichever of the three draw paths this
    // frame takes -- CPU, GPU bridge or wrapped pixel buffer. Looking it up
    // in only one of them is how a feature comes to work on one machine and
    // not another.
    const int stackOutputIndex =
      layerSourceOutputIndex >= 0 ? layerSourceOutputIndex : outputIndex;
    const OutputLayer* layerWarp = layerRecordFor(stackOutputIndex, sourceDeckIndex);
    if (auto old = outputRuntime->layerLookFrames.find(sourceDeckIndex);
        old != outputRuntime->layerLookFrames.end() && old->second.first != cuePreviewCacheKey(*sourceCue)) {
      outputRuntime->heldLookFrames[sourceDeckIndex] = std::move(old->second);
      outputRuntime->layerLookFrames.erase(old);
    }
    // WHERE THIS DECK SITS IN THAT OUTPUT'S STACK, which is what the
    // crossfader works on. Taken from the COMPOSITION output for the same
    // reason the corner pin is: a mirroring destination draws the source
    // output's stack, so it must fade by the source output's crossfader.
    const int stackIndex = stackPositionFor(stackOutputIndex, sourceDeckIndex);
    DeckRuntime* sourceRuntime = runtimeForDeck(sourceDeckIndex);
    if (!sourceRuntime || !sourceRuntime->mediaEngine) {
      noteLayerVisibility(*outputRuntime, outputIndex, sourceDeckIndex, false,
                          "no deck runtime or engine");
      return;
    }
    // A TEXT CUE HAS NO DECODED FRAME, so it must be drawn before the frame
    // check below returns. It is the deck's picture, not an overlay on one.
    if (sourceCue->kind == CueKind::Text) {
      // A STOPPED text cue is off. Its deck keeps the cue as its active one
      // after STOP, and nothing here asked whether it was still playing --
      // which went unnoticed only because a text cue's engine never left
      // Stopped at all until it was given a clock (see loadCue).
      if (sourceRuntime->mediaEngine->state() == TransportState::Stopped) {
        noteLayerVisibility(*outputRuntime, outputIndex, sourceDeckIndex, false,
                            "text cue stopped");
        return;
      }
      noteLayerVisibility(*outputRuntime, outputIndex, sourceDeckIndex, true, "");
      renderTextCueIntoOutput(outputRuntime->outputRenderer, *sourceCue, target,
                              sourceRuntime->mediaEngine->position(),
                              lowerThirdOutAtFor(sourceDeckIndex));
      return;
    }
    const DecodedFrame* sourceFrame = sourceRuntime->mediaEngine->currentFrame();
    // Until the incoming cue has a frame, currentFrame returns the outgoing
    // hold. The transition draws that hold once; blending it as both pictures
    // raises the opacity and briefly restores the wrong cue's look/geometry.
    if (sourceRuntime->mediaEngine->outgoingSeconds() > 0.0001 &&
        sourceFrame == sourceRuntime->mediaEngine->outgoingFrame()) return;
    DecodedFrame cachedGpuDescriptor;
    if (!sourceFrame || sourceFrame->width <= 0 || sourceFrame->height <= 0 ||
        (sourceFrame->pixels.empty() && !sourceFrame->isGpu())) {
      noteLayerVisibility(*outputRuntime, outputIndex, sourceDeckIndex, false,
                          !sourceFrame ? "currentFrame() null"
                                       : "currentFrame() has no pixels");
      return;
    }
    noteLayerVisibility(*outputRuntime, outputIndex, sourceDeckIndex, true, "");
#if DECKBOY_INPROC_DECODE
    if (sourceFrame->isGpu()) {
      // ── macOS: THE FRAME IS THE TEXTURE ─────────────────────────────
      //
      // No device to match and nothing to copy: an IOSurface-backed
      // CVPixelBuffer can be wrapped by any Metal device, so this output wraps
      // the decoder's own buffer. One wrap per frame advance -- cheap, because
      // no pixel memory is allocated, only a Metal view of a surface that
      // already exists.
      if (sourceFrame->gpuKind == DecodedFrame::GpuKind::CVPixelBuffer) {
        auto frameIt = outputRuntime->layerPixelBufferFrameIndices.find(sourceDeckIndex);
        SDL_Texture* wrapped = nullptr;
        auto texIt = outputRuntime->layerPixelBufferTextures.find(sourceDeckIndex);
        if (texIt != outputRuntime->layerPixelBufferTextures.end() &&
            frameIt != outputRuntime->layerPixelBufferFrameIndices.end() &&
            frameIt->second == sourceFrame->index) {
          wrapped = texIt->second;      // same frame again: reuse the wrap
        } else {
          if (texIt != outputRuntime->layerPixelBufferTextures.end() && texIt->second) {
            SDL_DestroyTexture(texIt->second);
          }
          wrapped = deckboy::libav::wrapPixelBufferTexture(
            outputRuntime->outputRenderer, *sourceFrame);
          outputRuntime->layerPixelBufferTextures[sourceDeckIndex] = wrapped;
          outputRuntime->layerPixelBufferFrameIndices[sourceDeckIndex] =
            sourceFrame->index;
        }
        if (wrapped) {
          float pbOpacity = std::clamp(project_.decks[sourceDeckIndex].playlistOpacity, 0.0f, 1.0f);
          const float pbFade = static_cast<float>(sourceRuntime->mediaEngine->currentVisualFadeGain());
          SDL_BlendMode pbBlend = SDL_BLENDMODE_BLEND;
          pbOpacity *= static_cast<float>(
            outputCrossfadeGain(stackOutputIndex, stackIndex, pbBlend));
          const Uint8 pbAlpha = static_cast<Uint8>(std::lround(pbOpacity * pbFade * 255.0f));
          SDL_SetTextureBlendMode(wrapped, pbBlend);
          SDL_SetTextureAlphaMod(wrapped, pbAlpha);
          renderTextureWithCueGeometry(outputRuntime->outputRenderer, wrapped,
                                       sourceFrame->width, sourceFrame->height,
                                       sourceCue, target, pbBlend, layerWarp);
          SDL_SetTextureBlendMode(wrapped, SDL_BLENDMODE_BLEND);
          SDL_SetTextureAlphaMod(wrapped, 255);
          return;
        }
        // The wrap failed (not the Metal backend, or an unexpected buffer
        // type): fall through to the download below, which works for
        // everything.
      }
      if (sourceFrame->gpuDevice && sourceFrame->gpuDevice == outputRuntime->rendererD3DDevice) {
        // Zero-copy: GPU-copy the decoded slice into this output's wrapped
        // NV12 texture on frame advance, then composite it like any texture.
        SDL_Texture* gpuTexture = ensureLayerGpuTexture(
          *outputRuntime, sourceDeckIndex, sourceFrame->width, sourceFrame->height,
          sourceFrame->format, frameColorspace(*sourceFrame));
        if (gpuTexture) {
          auto gpuFrameIt = outputRuntime->layerGpuFrameIndices.find(sourceDeckIndex);
          if (gpuFrameIt == outputRuntime->layerGpuFrameIndices.end() ||
              gpuFrameIt->second != sourceFrame->index) {
            // External D3D writes must retire SDL's queued reads of this texture.
            SDL_FlushRenderer(outputRuntime->outputRenderer);
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
          gpuDeckOpacity *= static_cast<float>(
            outputCrossfadeGain(stackOutputIndex, stackIndex, gpuBlend));
          Uint8 gpuAlpha = static_cast<Uint8>(std::lround(gpuDeckOpacity * gpuFadeGain * 255.0f));
          SDL_SetTextureBlendMode(gpuTexture, gpuBlend);
          SDL_SetTextureAlphaMod(gpuTexture, gpuAlpha);
          renderTextureWithCueGeometry(outputRuntime->outputRenderer, gpuTexture,
                                       sourceFrame->width, sourceFrame->height, sourceCue,
                                       target, gpuBlend, layerWarp);
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
        gpuKeyIt->second != gpuCueKey ||
        outputRuntime->layerBridgeTextureWidths[sourceDeckIndex] != sourceFrame->width ||
        outputRuntime->layerBridgeTextureHeights[sourceDeckIndex] != sourceFrame->height ||
        deckboyTextureColorspace(outputRuntime->layerBridgeTextures[sourceDeckIndex]) != frameColorspace(*sourceFrame);
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
      } else {
        // The cached CPU bridge is NV12 even when its GPU source is P010.
        // Keep that layout on repeated draws; there are no GPU CPU bytes to
        // upload and recreating a P010 bridge would discard the valid picture.
        cachedGpuDescriptor.width = sourceFrame->width;
        cachedGpuDescriptor.height = sourceFrame->height;
        cachedGpuDescriptor.index = sourceFrame->index;
        cachedGpuDescriptor.format = FramePixelFormat::NV12;
        cachedGpuDescriptor.colorspace = frameColorspace(*sourceFrame);
        sourceFrame = &cachedGpuDescriptor;
      }
    }
#endif
    const Uint32 sourceFormat = sdlPixelFormat(sourceFrame->format);
    SDL_Texture* bridgeTexture = ensureLayerBridgeTexture(
      *outputRuntime, sourceDeckIndex,
      sourceFrame->width, sourceFrame->height, sourceFormat, frameColorspace(*sourceFrame));
    if (!bridgeTexture) {
      return;
    }
    std::string cueKey = cuePreviewCacheKey(*sourceCue);
    auto frameIt = outputRuntime->layerBridgeFrameIndices.find(sourceDeckIndex);
    auto cueIt = outputRuntime->layerBridgeCueKeys.find(sourceDeckIndex);
    const auto pixelsAddress = reinterpret_cast<std::uintptr_t>(sourceFrame->pixels.data());
    const auto pixelsIt = outputRuntime->layerBridgePixelPointers.find(sourceDeckIndex);
    // A held outgoing still and the newly decoded incoming still can both
    // have index zero under the incoming cue's key. Arrival must refresh it.
    const bool newPixels = !sourceFrame->pixels.empty() &&
      (pixelsIt == outputRuntime->layerBridgePixelPointers.end() || pixelsIt->second != pixelsAddress);
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
      newPixels ||
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
            // The deck's own played sound, for the picture effects that DRAW
            // with it. Copied per frame only when the chain actually contains
            // one: it is a few thousand floats, and every other cue would pay
            // for it otherwise.
            std::vector<float>& audioTrace = deckAudioTraceScratch(sourceDeckIndex);
            if (sourceRuntime && sourceRuntime->mediaEngine &&
                deckboy::effects::cueEffectStackDrawsWithAudio(sourceCue->effects)) {
              sourceRuntime->mediaEngine->copyRecentProgramAudio(audioTrace);
              fxCtx.audioSamples = audioTrace.empty() ? nullptr : audioTrace.data();
              fxCtx.audioSampleCount = audioTrace.size();
            }
            // Any armed LFO, evaluated for this frame. Returns false and costs
            // nothing when the cue has none, which is almost every cue.
            std::vector<deckboy::effects::CueEffect> modulated;
            // The deck's own sound feeds any LFO set to Audio -- the picture
            // half of the loop the Ouroboros audio effect closes.
            const bool moving = deckboy::effects::modulateCueEffectStack(
              sourceCue->effects, lfoSeconds_, lfoBeats_, modulated,
              sourceRuntime->mediaEngine->programAudioLevel01());
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
            // And read the finished picture back to the deck's audio, for
            // Ouroboros. Only by the FIRST consumer this frame -- the same rule
            // stateHold keeps for the effects -- or two outputs showing one
            // deck would each publish, and the second would read as no motion.
            if (!fxCtx.stateHold) {
              sourceRuntime->mediaEngine->publishPostEffectStats(
                outputRuntime->layerBridgeScratchPixels.data(),
                sourceFrame->width, sourceFrame->height);
            }
          }
          uploadPixels = outputRuntime->layerBridgeScratchPixels.data();
        }
        SDL_UpdateTexture(bridgeTexture, nullptr, uploadPixels, sourceFrame->width * 4);
        if (cueHasPixelEffects(*sourceCue) || wantsStack) {
          auto& saved = outputRuntime->layerLookFrames[sourceDeckIndex];
          saved.first = cuePreviewCacheKey(*sourceCue);
          saved.second.width = sourceFrame->width;
          saved.second.height = sourceFrame->height;
          saved.second.index = sourceFrame->index;
          saved.second.colorspace = SDL_COLORSPACE_SRGB;
          saved.second.format = FramePixelFormat::RGBA32;
          saved.second.pixels.swap(outputRuntime->layerBridgeScratchPixels);
        } else {
          outputRuntime->layerLookFrames.erase(sourceDeckIndex);
        }
      }
      outputRuntime->layerBridgeFrameIndices[sourceDeckIndex] = sourceFrame->index;
      outputRuntime->layerBridgePixelPointers[sourceDeckIndex] = pixelsAddress;
      outputRuntime->layerBridgeCueKeys[sourceDeckIndex] = std::move(cueKey);
    }
    float deckOpacity = std::clamp(project_.decks[sourceDeckIndex].playlistOpacity, 0.0f, 1.0f);
    float fadeGain = static_cast<float>(sourceRuntime->mediaEngine->currentVisualFadeGain());
    // THE CROSSFADER, folded into the opacity this deck already had rather than
    // replacing it -- a deck faded down or mid cue-fade must stay faded down.
    // Outside VJ mode the multiplier is 1 and every existing show renders
    // exactly as before, through the same call.
    SDL_BlendMode layerBlend = SDL_BLENDMODE_BLEND;
    deckOpacity *= static_cast<float>(
      outputCrossfadeGain(stackOutputIndex, stackIndex, layerBlend));
    Uint8 alpha = static_cast<Uint8>(std::lround(deckOpacity * fadeGain * 255.0f));
    SDL_SetTextureBlendMode(bridgeTexture, layerBlend);
    SDL_SetTextureAlphaMod(bridgeTexture, alpha);
    renderTextureWithCueGeometry(outputRuntime->outputRenderer, bridgeTexture, sourceFrame->width, sourceFrame->height, sourceCue, target, layerBlend, layerWarp);
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
                                      const SDL_Rect& target, int layerSourceOutputIndex = -1) {
    OutputRuntime* outputRuntime = runtimeForOutput(outputIndex);
    if (!outputRuntime || !outputRuntime->outputRenderer) return;
    if (sourceDeckIndex < 0 ||
        sourceDeckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    DeckRuntime* deckRuntime = runtimeForDeck(sourceDeckIndex);
    if (!deckRuntime || !deckRuntime->mediaEngine) return;
    MediaEngine& engine = *deckRuntime->mediaEngine;
    const int compositionIndex = layerSourceOutputIndex >= 0 ? layerSourceOutputIndex : outputIndex;
    SDL_BlendMode transitionBlend = SDL_BLENDMODE_BLEND;
    const float deckGain = std::clamp(project_.decks[sourceDeckIndex].playlistOpacity, 0.0f, 1.0f) *
      static_cast<float>(outputCrossfadeGain(compositionIndex,
        stackPositionFor(compositionIndex, sourceDeckIndex), transitionBlend));
    if (deckGain <= 0.0f) return;
    const float heldGain = deckGain * engine.outgoingSourceGain();
    const OutputLayer* layerWarp = layerRecordFor(
      layerSourceOutputIndex >= 0 ? layerSourceOutputIndex : outputIndex, sourceDeckIndex);
    auto drawHeld = [&](const DecodedFrame& frame, const SDL_Rect& rect, Uint8 alpha,
                        const char* key = "transition") {
      alpha = static_cast<Uint8>(std::lround(alpha * heldGain));
      renderTransitionFrame(*outputRuntime, frame, sourceDeckIndex, rect, alpha, key, layerWarp,
                            transitionBlend);
    };

    const double seconds = engine.outgoingSeconds();
    if (seconds <= 0.0001) return;                  // a cut has nothing to draw
    const DecodedFrame* out = cpuFrameForTransition(*outputRuntime, engine.outgoingFrame(),
                                                    sourceDeckIndex, "out");
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
    // incoming layer has already been drawn translated by the composition
    // wrapper. Only the outgoing hold remains to draw here.
    if (isPushStyle(style)) {
      const SDL_Point shift = pushTransitionOffset(style, progress, target);
      SDL_Rect outRect = target;
      outRect.x += shift.x;
      outRect.y += shift.y;
      SDL_Rect previousClip {};
      const bool hadClip = SDL_RenderClipEnabled(outputRuntime->outputRenderer);
      if (hadClip) SDL_GetRenderClipRect(outputRuntime->outputRenderer, &previousClip);
      SDL_Rect clip = target;
      if (hadClip && !SDL_GetRectIntersection(&previousClip, &target, &clip)) return;
      SDL_SetRenderClipRect(outputRuntime->outputRenderer, &clip);
      drawHeld(*out, outRect, 255);
      SDL_SetRenderClipRect(outputRuntime->outputRenderer, hadClip ? &previousClip : nullptr);
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
      drawHeld(*out, target, 255);
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
          drawHeld(*out, target, 255);
          continue;
        }
        const SDL_Rect left {target.x, y, std::max(0, cx - half - target.x), kBand};
        const SDL_Rect right {cx + half, y,
                              std::max(0, target.x + target.w - (cx + half)), kBand};
        for (const SDL_Rect& part : {left, right}) {
          if (part.w <= 0) continue;
          SDL_SetRenderClipRect(outputRuntime->outputRenderer, &part);
          drawHeld(*out, target, 255);
        }
      }
      SDL_SetRenderClipRect(outputRuntime->outputRenderer,
                            hadClip ? &previousClip : nullptr);
      return;
    }

    // ── PORTAL ──────────────────────────────────────────────────────────
    //
    // The Portal source's blobs, melting open through the outgoing picture
    // until nothing of it is left. Built on the CPU as one RGBA picture with
    // the holes already in it -- SDL has no shaped clip, and the iris's band
    // trick cannot draw a rim -- then drawn over the incoming cue like any
    // other held frame. A held frame that is not CPU RGBA dissolves instead,
    // rather than drawing nothing.
    // FOUR STYLES, ONE PATH. Portal, Shatter, Clouds and Ghastly all build the
    // outgoing layer on the CPU as RGBA with alpha in it, and all four are
    // drawn the same way: upload once, blend over the incoming cue that is
    // already on the output. Only the builder differs, so only the builder is
    // chosen here -- and an empty result falls through to the dissolve below
    // rather than drawing nothing, which is what a held frame that is neither
    // RGBA nor NV12 gets.
    {
      using Builder = void (*)(const DecodedFrame&, std::vector<std::uint8_t>&,
                               double, double, std::uint64_t);
      Builder build = nullptr;
      const char* cacheKey = nullptr;
      switch (style) {
        case TransitionStyle::Portal:
          build = &MediaEngine::buildPortalTransition;  cacheKey = "portal"; break;
        case TransitionStyle::Shatter:
          build = &MediaEngine::buildShatterTransition; cacheKey = "shatter"; break;
        case TransitionStyle::Clouds:
          build = &MediaEngine::buildCloudsTransition;  cacheKey = "clouds"; break;
        case TransitionStyle::Ghastly:
          build = &MediaEngine::buildGhastlyTransition; cacheKey = "ghastly"; break;
        default: break;
      }
      if (build) {
        std::vector<std::uint8_t>& pixels = outputRuntime->portalTransitionPixels;
        build(*out, pixels, progress, progress * seconds,
              static_cast<std::uint64_t>(out->index));
        if (!pixels.empty()) {
          // The style is in the cache key: two styles on two decks must not
          // share one bridge texture, and a style change mid-show must not
          // reuse the other one's.
          const std::string key =
            std::string(cacheKey) + ":" + std::to_string(sourceDeckIndex);
          SDL_Texture* tex = ensureOverlayBridgeTexture(
            *outputRuntime, key, out->width, out->height, SDL_PIXELFORMAT_RGBA32);
          if (tex) {
            SDL_UpdateTexture(tex, nullptr, pixels.data(), out->width * 4);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(std::lround(255 * heldGain)));
            Cue outgoingGeometry;
            const Cue* geometry = nullptr;
            if (DeckRuntime* rt = runtimeForDeck(sourceDeckIndex); rt && rt->mediaEngine) {
              outgoingGeometry = rt->mediaEngine->outgoingGeometryCue();
              geometry = &outgoingGeometry;
            }
            renderTextureWithCueGeometry(outputRuntime->outputRenderer, tex,
                                         out->width, out->height, geometry,
                                          target, transitionBlend, layerWarp);
            return;
          }
        }
      }
    }

    if (style == TransitionStyle::DipWhite) {
      // The same shape as dip-to-black, through white: a flash rather than a
      // blink, and the one an operator reaches for on a camera cut.
      SDL_SetRenderDrawBlendMode(outputRuntime->outputRenderer, SDL_BLENDMODE_BLEND);
      if (progress < 0.5) {
        drawHeld(*out, target, 255);
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
        drawHeld(*out, target, 255);
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
    // ITS BARS FADE WITH IT. On the deck that owns the output, a letterboxed
    // picture's bars are black, and they are part of what is fading out --
    // without this the incoming cue appeared in them at once. A layer's bars
    // are transparent, so only the base gets them.
    if (outputIndex >= 0 && outputIndex < static_cast<int>(project_.outputs.size()) &&
        project_.outputs[static_cast<std::size_t>(outputIndex)].hostDeckIndex == sourceDeckIndex) {
      // Only WHERE THE BARS ARE: the target outside the outgoing picture's
      // placed rectangle. Filling under the picture too darkened the middle
      // of every dissolve.
      const Cue geometry = engine.outgoingGeometryCue();
      const SDL_Rect pic = cuePlacementFor(&geometry, out->width, out->height, target).destination;
      if (std::abs(geometry.outputRotationDegrees) < 0.01f) {
        SDL_SetRenderDrawBlendMode(outputRuntime->outputRenderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(outputRuntime->outputRenderer, 0, 0, 0,
          static_cast<Uint8>(std::lround(alpha * heldGain)));
        const int picL = std::clamp(pic.x, target.x, target.x + target.w);
        const int picR = std::clamp(pic.x + pic.w, target.x, target.x + target.w);
        const int picT = std::clamp(pic.y, target.y, target.y + target.h);
        const int picB = std::clamp(pic.y + pic.h, target.y, target.y + target.h);
        const SDL_Rect bars[4] = {
          {target.x, target.y, target.w, picT - target.y},                  // above
          {target.x, picB, target.w, target.y + target.h - picB},           // below
          {target.x, picT, picL - target.x, picB - picT},                   // left
          {picR, picT, target.x + target.w - picR, picB - picT},            // right
        };
        for (const SDL_Rect& bar : bars) {
          if (bar.w > 0 && bar.h > 0) SDL_RenderFillRect(outputRuntime->outputRenderer, &bar);
        }
        SDL_SetRenderDrawBlendMode(outputRuntime->outputRenderer, SDL_BLENDMODE_NONE);
      }
    }
    drawHeld(*out, target, alpha);
  }

  // The frame a transition draws, as CPU pixels. A zero-copy decode hands out
  // GPU surfaces with no pixels, and a transition from one drew NOTHING -- so
  // every transition was silently a cut wherever GPU decode was on. Downloaded
  // once per frame and kept: a held outgoing frame does not change.
  const DecodedFrame* cpuFrameForTransition(OutputRuntime& runtime, const DecodedFrame* frame,
                                            int deckIndex, const char* role) {
    if (frame) {
      const bool outgoing = std::string(role) == "out";
      const Cue* look = activeCuePtr(deckIndex);
      Cue held;
      if (outgoing) {
        if (DeckRuntime* rt = runtimeForDeck(deckIndex); rt && rt->mediaEngine) {
          held = rt->mediaEngine->outgoingGeometryCue();
          look = &held;
        }
      }
      auto matchingLook = [&](const auto& frames) -> const DecodedFrame* {
        const auto saved = frames.find(deckIndex);
        if (look && saved != frames.end() && saved->second.first == cuePreviewCacheKey(*look) &&
            saved->second.second.index == frame->index &&
            saved->second.second.width == frame->width && saved->second.second.height == frame->height)
          return &saved->second.second;
        return nullptr;
      };
      if (outgoing) {
        if (const auto* saved = matchingLook(runtime.heldLookFrames)) return saved;
      }
      if (const auto* saved = matchingLook(runtime.layerLookFrames)) return saved;
    }
    if (!frame || !frame->isGpu()) {
      return frame;
    }
#if DECKBOY_INPROC_DECODE
    const std::string key = std::string(role) + ":" + std::to_string(deckIndex);
    auto& cached = runtime.transitionCpuFrames[key];
    const std::uintptr_t stamp = reinterpret_cast<std::uintptr_t>(frame->gpuTexture) ^
                                 (static_cast<std::uintptr_t>(frame->index) << 1) ^
                                 (static_cast<std::uintptr_t>(frame->gpuSubresource) << 20);
    if (cached.first != stamp) {
      DecodedFrame cpu;
      if (deckboy::libav::downloadGpuFrameNV12(*frame, cpu)) {
        cpu.index = frame->index;
        cached = {stamp, std::move(cpu)};
      } else {
        cached = {stamp, DecodedFrame{}};
      }
    }
    return cached.second.pixels.empty() ? nullptr : &cached.second;
#else
    (void)runtime; (void)deckIndex; (void)role;
    return nullptr;
#endif
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

  static SDL_Point pushTransitionOffset(TransitionStyle style, double progress,
                                        const SDL_Rect& target) {
    const double eased = progress * progress * (3.0 - 2.0 * progress);
    switch (style) {
      case TransitionStyle::PushLeft:  return {-static_cast<int>(eased * target.w), 0};
      case TransitionStyle::PushRight: return { static_cast<int>(eased * target.w), 0};
      case TransitionStyle::PushUp:    return {0, -static_cast<int>(eased * target.h)};
      default:                        return {0,  static_cast<int>(eased * target.h)};
    }
  }

  void renderDeckWithTransitionIntoOutput(int outputIndex, int deckIndex,
                                          const SDL_Rect& target,
                                          int compositionIndex = -1) {
    OutputRuntime* output = runtimeForOutput(outputIndex);
    DeckRuntime* deck = runtimeForDeck(deckIndex);
    if (!output || !output->outputRenderer || !deck || !deck->mediaEngine) return;
    MediaEngine& engine = *deck->mediaEngine;
    SDL_Rect incoming = target;
    // Pushes draw the incoming layer once, through its ordinary processed
    // path, translated into place. Drawing it at rest as well exposed a
    // second copy and made a differently shaped cue appear to resize.
    if (engine.outgoingSeconds() > 0.0001 && engine.outgoingFrame() &&
        isPushStyle(engine.outgoingStyle()) && engine.outgoingProgress01() < 1.0) {
      const TransitionStyle style = engine.outgoingStyle();
      const SDL_Point shift = pushTransitionOffset(style, engine.outgoingProgress01(), target);
      incoming.x += shift.x;
      incoming.y += shift.y;
      switch (style) {
        case TransitionStyle::PushLeft:  incoming.x += target.w; break;
        case TransitionStyle::PushRight: incoming.x -= target.w; break;
        case TransitionStyle::PushUp:    incoming.y += target.h; break;
        default:                        incoming.y -= target.h; break;
      }
    }
    SDL_Rect previousClip {};
    const bool hadClip = SDL_RenderClipEnabled(output->outputRenderer);
    if (hadClip) SDL_GetRenderClipRect(output->outputRenderer, &previousClip);
    SDL_Rect clip = target;
    if (hadClip && !SDL_GetRectIntersection(&previousClip, &target, &clip)) return;
    SDL_SetRenderClipRect(output->outputRenderer, &clip);
    renderDeckLayerIntoOutput(outputIndex, deckIndex, incoming, compositionIndex);
    renderDeckTransitionIntoOutput(outputIndex, deckIndex, target, compositionIndex);
    SDL_SetRenderClipRect(output->outputRenderer, hadClip ? &previousClip : nullptr);
  }

  // UPLOAD ONCE PER FRAME, not once per draw.
  //
  // Iris draws the outgoing picture sixty times behind different clips, and
  // each call was re-uploading the whole thing: a 4K frame pushed across the
  // bus sixty times a frame measured 1.3fps. The pixels have not changed
  // between those draws -- only the clip has -- so the upload is skipped when
  // the frame is the one already sitting in the texture.
  void renderTransitionFrame(OutputRuntime& outputRuntime, const DecodedFrame& frame,
                             int deckIndex, const SDL_Rect& target, Uint8 alpha,
                              const char* key = "transition", const OutputLayer* layerWarp = nullptr,
                              SDL_BlendMode blendMode = SDL_BLENDMODE_BLEND) {
    const Uint32 format = sdlPixelFormat(frame.format);
    const std::string bridgeKey = std::string(key) + ":" + std::to_string(deckIndex);
    SDL_Texture* tex = ensureOverlayBridgeTexture(
      outputRuntime, bridgeKey, frame.width, frame.height, format, frameColorspace(frame));
    if (!tex) return;
    // Keyed on the frame's own address and index: a held frame does not move
    // while it is being drawn, and the index changes when it is replaced.
    auto& stamp = outputRuntime.transitionUploadStamps[bridgeKey];
    const std::uintptr_t nowStamp =
      reinterpret_cast<std::uintptr_t>(frame.pixels.data()) ^
      (static_cast<std::uintptr_t>(frame.index) << 1);
    const bool needUpload = stamp != nowStamp;
    stamp = nowStamp;
    // WITH THE CUE'S GEOMETRY. The outgoing picture takes the geometry its own
    // cue had (the engine kept it at beginTransition). Drawn stretched to the
    // raster, a letterboxed cue snapped to full frame as the transition began.
    Cue outgoingGeometry;
    const Cue* geometry = nullptr;
    if (DeckRuntime* rt = runtimeForDeck(deckIndex); rt && rt->mediaEngine) {
      outgoingGeometry = rt->mediaEngine->outgoingGeometryCue();
      geometry = &outgoingGeometry;
    }
    auto drawPlaced = [&]() {
      SDL_SetTextureAlphaMod(tex, alpha);
      renderTextureWithCueGeometry(outputRuntime.outputRenderer, tex, frame.width, frame.height,
                                   geometry, target, blendMode, layerWarp);
      SDL_SetTextureAlphaMod(tex, 255);
    };
    if (!needUpload) {
      drawPlaced();
      return;
    }
    // THE HELD FRAME IS WHATEVER THE DECODER MADE, and a video cue that needs
    // no CPU work decodes to NV12. This used to upload every held frame as
    // RGBA -- four bytes a pixel over a buffer of one and a half -- so every
    // transition OUT of such a video drew the old picture as scrambled tiles
    // (and read past the end of the buffer doing it). Two planes, as the
    // overlay path already uploads them.
    if (frame.format == FramePixelFormat::NV12 || frame.format == FramePixelFormat::P010) {
      const int bytesPerSample = frame.format == FramePixelFormat::P010 ? 2 : 1;
      const std::size_t lumaBytes = static_cast<std::size_t>(frame.width) *
                                    static_cast<std::size_t>(frame.height) * bytesPerSample;
      if (frame.pixels.size() < lumaBytes + lumaBytes / 2) {
        stamp = 0;   // nothing went in, so nothing may be drawn from it later
        return;
      }
      const std::uint8_t* y = frame.pixels.data();
      SDL_UpdateNVTexture(tex, nullptr, y, frame.width * bytesPerSample,
                          y + lumaBytes, frame.width * bytesPerSample);
    } else {
      SDL_UpdateTexture(tex, nullptr, frame.pixels.data(), frame.width * 4);
    }
    drawPlaced();
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
      outputRuntime, overlayKey, sourceFrame.width, sourceFrame.height, sourceFormat,
      frameColorspace(sourceFrame));
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

  // 0 at the instant it is asked for, 1 once it has fully arrived, and back
  // down to 0 as it leaves. Everything the styles do is a function of this
  // one number, so they cannot disagree about how far along it is.
  //
  // 1.0 for a style of `none`, for a time of 0, and for anything that is not
  // a lower third -- all of which must draw exactly as they always have.
  double overlayMoveProgress(int deckIndex, int cueIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return 1.0;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return 1.0;
    }
    const Cue& cue = deck.cues[cueIndex];
    if (cue.kind != CueKind::LowerThird || cue.lowerThirdStyle == 0) {
      return 1.0;
    }
    const double seconds = std::clamp(cue.lowerThirdAnimSeconds, 0.0, 5.0);
    if (seconds <= 0.001) {
      return 1.0;
    }
    const Uint64 now = SDL_GetTicks();
    const auto key = std::make_pair(deckIndex, cueIndex);
    auto leaving = overlayLeavingAtMs_.find(key);
    if (leaving != overlayLeavingAtMs_.end()) {
      const double gone = static_cast<double>(now - leaving->second) / 1000.0;
      return std::clamp(1.0 - gone / seconds, 0.0, 1.0);
    }
    auto shown = overlayShownAtMs_.find(key);
    if (shown == overlayShownAtMs_.end()) {
      return 1.0;   // already up when the show opened; no arrival to play
    }
    const double up = static_cast<double>(now - shown->second) / 1000.0;
    return std::clamp(up / seconds, 0.0, 1.0);
  }

  // Eased, because a lower third that moves linearly reads as a value
  // changing and one that eases reads as a hand. Smoothstep both ways.
  static double overlayEase(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
  }

  void renderOutputWindow(int outputIndex) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    const Uint64 outputPassStartNs = SDL_GetTicksNS();
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
    // WHAT TO DRAW COMES FROM THE OUTPUT BEING MIRRORED, not from this one.
    //
    // outputType is this destination's own kind and governs EGRESS -- whether
    // there is a window or an encoder on the end of it. What gets drawn is a
    // separate question, and when this output mirrors another the honest
    // answer is "whatever that one shows". It was taken from outputType
    // regardless, so a stream mirroring a presenter view, a prompter or a
    // multiview recorded the PROGRAMME instead of the thing it was pointed
    // at -- silently, and only discoverable by watching the file afterwards.
    //
    // Without a mirror the two are the same output, so this changes nothing
    // for every show that has one destination.
    const std::string viewType = normalizeOutputType(compositionOutput.outputType);
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
    // TRANSPARENT IN KEY+FILL MODE, opaque black otherwise.
    //
    // This one line is the whole reason key+fill is a mode on the output and
    // not just a second device setting. Cleared to opaque black, every pixel
    // of the composite has alpha 255 and a key built from it is solid white --
    // a key that keys nothing. Cleared to transparent, the layers accumulate
    // real coverage (dstA = srcA + dstA*(1-srcA), which is what
    // SDL_BLENDMODE_BLEND onto a transparent target already does), and the
    // alpha channel becomes the matte a downstream keyer needs.
    //
    // Everything sampled from this output sees it: the preview tap, a
    // recording, an NDI send. That is correct -- in this mode the output
    // genuinely is a graphic with holes in it rather than a picture.
    const bool keyFillMode = outputIndex >= 0
      && outputIndex < static_cast<int>(project_.outputs.size())
      && project_.outputs[static_cast<std::size_t>(outputIndex)].deckLinkKeyFill;
    SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0,
                           keyFillMode ? 0 : 255);
    SDL_RenderClear(runtime->outputRenderer);

    SDL_Rect bounds {0, 0, renderW, renderH};
    auto outputLayers = layeredDeckEntriesForOutput(compositionOutputIndex);
    if (outputLayers.empty()) {
      outputLayers.emplace_back(0, hostDeckIndex);
    }
    if (output.outputTestCardEnabled) {
      renderOutputTestCard(outputIndex, runtime->outputRenderer, renderW, renderH);
    } else if (viewType == "prompter") {
      // The talent's screen, not the programme.
      renderPrompterView(outputIndex, hostDeckIndex, bounds);
    } else if (viewType == "multiview") {
      // EVERY PLAYLIST, ON A SCREEN. Drawn with the same per-deck path the
      // compositor uses for layers, so a GPU-decoded frame stays on the GPU
      // and a tile costs what a layer costs.
      renderMultiviewIntoOutput(outputIndex, bounds);
    } else if (viewType == "presenter") {
      // Not the programme: what the OPERATOR needs to see. Same window, same
      // display picker, same arming -- a different picture.
      renderPresenterView(outputIndex, hostDeckIndex, bounds);
    } else {
      for (const auto& entry : outputLayers) {
        // AUDITION: this deck is being looked at, not shown. Skipped here
        // rather than by dropping it from outputLayers, so the transition
        // below and everything downstream still sees the same list and one
        // condition governs the whole thing.
        if (deckIsHeldOffOutput(entry.second)) {
          continue;
        }
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
        renderDeckWithTransitionIntoOutput(outputIndex, entry.second, bounds,
                                          compositionOutputIndex);
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

          // -- HOW IT ARRIVES AND LEAVES ------------------------------
          //
          // One progress number drives all of it. A style of `none`, a time
          // of 0, or an overlay that was already up when the show opened all
          // give 1.0, which is the picture this used to draw always.
          const double moved = overlayMoveProgress(hostDeckIndex, ovIdx);
          const double eased = overlayEase(moved);
          double alphaScale = 1.0;
          if (lc.lowerThirdStyle == 1) {            // fade
            alphaScale = eased;
          } else if (lc.lowerThirdStyle == 2) {     // rise, up from below
            bar.y += static_cast<int>(std::lround((1.0 - eased) * barH));
            alphaScale = eased;
          } else if (lc.lowerThirdStyle == 3) {     // slide, in from the left
            bar.x -= static_cast<int>(std::lround((1.0 - eased) * renderW));
          } else if (lc.lowerThirdStyle == 4) {     // wipe, out from the strip
            bar.w = std::max(8, static_cast<int>(std::lround(bar.w * eased)));
          }
          // Nothing to draw yet, and nothing left to draw.
          if (alphaScale <= 0.003 || bar.w <= 0) {
            ++overlaySlot;
            continue;
          }
          // THE TEXT IS CLIPPED TO THE BAR. Without this a wipe reveals the
          // bar while the words sit there in full from the first frame,
          // which is not a wipe -- it is a bar growing behind finished text.
          SDL_Rect priorClip;
          const bool hadClip = SDL_RenderClipEnabled(runtime->outputRenderer);
          if (hadClip) {
            SDL_GetRenderClipRect(runtime->outputRenderer, &priorClip);
          }
          SDL_SetRenderClipRect(runtime->outputRenderer, &bar);

          SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(runtime->outputRenderer, 8, 16, 24,
            static_cast<Uint8>(std::lround(lc.lowerThirdBgAlpha * alphaScale)));
          SDL_RenderFillRect(runtime->outputRenderer, &bar);

          // Coloured accent strip (hue shifts per slot for differentiation)
          static constexpr std::array<SDL_Color, 4> accentColors {{
            {155, 188,  15, 220},
            { 15, 155, 188, 220},
            {188,  15, 155, 220},
            {188, 155,  15, 220},
          }};
          SDL_Color acc = accentColors[static_cast<size_t>(overlaySlot) % accentColors.size()];
          SDL_SetRenderDrawColor(runtime->outputRenderer, acc.r, acc.g, acc.b,
            static_cast<Uint8>(std::lround(acc.a * alphaScale)));
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
          // The clip goes back before the next overlay in the stack, which
          // has its own bar and its own progress.
          SDL_SetRenderClipRect(runtime->outputRenderer,
                                hadClip ? &priorClip : nullptr);
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
            // SIZED TO THE PICTURE, not to the desk. This drew with the UI
            // font at its UI size and a fixed 28px line -- on a 2160-line
            // output, captions one percent of the frame high that nobody in
            // the room could read. Now a line is 4.5% of the frame, drawn
            // from the cached text texture scaled up, on a box that fits the
            // words rather than a band across the whole picture.
            const float lineH = std::max(18.0f, static_cast<float>(renderH) * 0.045f);
            const float padX = lineH * 0.45f;
            const float padY = lineH * 0.2f;
            std::vector<std::pair<const TextTextureEntry*, const TextTextureEntry*>> rows;
            float widest = 0.0f;
            for (const std::string& line : lines) {
              if (line.empty()) continue;
              const TextTextureEntry* ink = cachedTextTexture(runtime->outputRenderer, fontBase_, line,
                                                              SDL_Color {255, 255, 255, 255});
              const TextTextureEntry* shade = cachedTextTexture(runtime->outputRenderer, fontBase_, line,
                                                                SDL_Color {0, 0, 0, 255});
              if (!ink || !ink->texture || ink->h <= 0) continue;
              rows.emplace_back(ink, shade);
              widest = std::max(widest, static_cast<float>(scaledLabelWidth(*ink, lineH)));
            }
            if (!rows.empty()) {
              const float boxW = std::min(static_cast<float>(renderW), widest + padX * 2.0f);
              const float boxH = rows.size() * lineH + padY * 2.0f;
              const float boxY = static_cast<float>(renderH) - boxH - static_cast<float>(renderH) * 0.06f;
              SDL_FRect box {(renderW - boxW) / 2.0f, boxY, boxW, boxH};
              SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_BLEND);
              SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0, 160);
              SDL_RenderFillRect(runtime->outputRenderer, &box);
              const float shadowOff = std::max(1.0f, lineH * 0.05f);
              for (std::size_t li = 0; li < rows.size(); ++li) {
                const auto* ink = rows[li].first;
                const auto* shade = rows[li].second;
                const SDL_FRect at = scaledLabelRect(*ink, 0.0, boxY + padY + li * lineH, lineH);
                SDL_FRect dst {(renderW - at.w) / 2.0f, at.y, at.w, at.h};
                if (shade && shade->texture) {
                  SDL_FRect drop {dst.x + shadowOff, dst.y + shadowOff, dst.w, dst.h};
                  SDL_RenderTexture(runtime->outputRenderer, shade->texture, nullptr, &drop);
                }
                SDL_RenderTexture(runtime->outputRenderer, ink->texture, nullptr, &dst);
              }
              SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_NONE);
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
    // A browser watching this output on the web monitor needs the picture
    // too -- but only while one is: an idle monitor costs nothing.
    const bool webMonitorRouteActive = webMonitorWatching(outputIndex);
    bool needsEgressCapture =
      streamRouteActive || ndiRouteActive || deckLinkRouteActive || spoutRouteActive ||
      st2110RouteActive || webMonitorRouteActive ||
      std::clamp(output.outputDelayMs, 0, 5000) > 0;
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
    const Uint64 beforeTapNs = SDL_GetTicksNS();
    if (usingCompositor && tapOutput && *tapOutput == outputIndex) {
      captureOutputPreviewTap(*runtime, egressRect);
    } else if (runtime->previewTapSerial != 0) {
      releaseOutputPreviewTap(*runtime);
    }
    if (webMonitorRouteActive) {
      feedWebMonitor(outputIndex, *runtime);
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
    const Uint64 afterTapNs = SDL_GetTicksNS();
    if (!outputSnapPath_.empty() && outputIndex == outputSnapIndex_ && !streamType) {
      if (SDL_Surface* shot = SDL_RenderReadPixels(runtime->outputRenderer, nullptr)) {
        SDL_SaveBMP(shot, outputSnapPath_.c_str());
        SDL_DestroySurface(shot);
      }
      outputSnapPath_.clear();
    }
    if (!streamType) {
      SDL_RenderPresent(runtime->outputRenderer);
    }
    recordOutputFramePresented(outputIndex);
    // WHERE AN OUTPUT FRAME'S TIME GOES, under DECKBOY_UI_PROFILE: composite,
    // the control monitor's tap, the sends, the present.
    if (uiProfileEnabled_) {
      const Uint64 endNs = SDL_GetTicksNS();
      const double totalMs = static_cast<double>(endNs - outputPassStartNs) / 1.0e6;
      if (totalMs > uiProfileSlowFrameMs_ * 0.6) {
        std::ostringstream line;
        line << std::fixed << std::setprecision(2) << "output " << (outputIndex + 1)
             << " pass=" << totalMs << "ms composite+egress="
             << static_cast<double>(beforeTapNs - outputPassStartNs) / 1.0e6
             << "ms tap+sends=" << static_cast<double>(afterTapNs - beforeTapNs) / 1.0e6
             << "ms present=" << static_cast<double>(endNs - afterTapNs) / 1.0e6 << "ms";
        uiProfileLog(line.str());
      }
    }
  }
