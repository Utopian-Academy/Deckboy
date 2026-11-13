// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// pixel_effects.hpp — Per-pixel visual effects applied to decoded frames.
//
// These functions operate on raw RGBA pixel buffers (std::vector<uint8_t>)
// and are called by MediaEngine after decoding each frame, before the
// pixels are uploaded to an SDL_Texture.
//
// Two effect categories:
//   1. Chroma key — removes a target color (green screen) by zeroing alpha
//   2. Color controls — brightness, contrast, saturation, hue shift
//
// Performance note: these run on the CPU for every pixel of every frame.
// At 1920x1080@30fps that's ~62M pixels/second. The functions are kept
// inline and the inner loops are tight for auto-vectorization. A future
// optimization could move these to GPU shaders.
//
// Called by: MediaEngine's video decode thread (after readExact, before
// frameQueue push) and the pattern/still frame generators.
// ============================================================================

#pragma once

#include "core/sdl_compat.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "types.hpp"
#include "cue_effects.hpp"   // detail::parallelRows

// Check if any color control parameter deviates from its neutral value.
// Neutral = brightness 1.0, contrast 1.0, saturation 1.0, hue shift 0.0.
// Used as a fast bail-out to skip the per-pixel loop when no adjustment is needed.
inline bool colorControlsActive(float brightness, float contrast, float saturation, float hueShiftDegrees) {
  constexpr float kEpsilon = 0.001f;
  return std::fabs(brightness - 1.0f) > kEpsilon
    || std::fabs(contrast - 1.0f) > kEpsilon
    || std::fabs(saturation - 1.0f) > kEpsilon
    || std::fabs(hueShiftDegrees) > kEpsilon;
}

// Convenience: check if a cue has any non-neutral color controls.
inline bool cueHasColorControls(const Cue& cue) {
  return colorControlsActive(cue.brightness, cue.contrast, cue.saturation, cue.hueShift);
}

// Check if a cue has any active pixel effect (chroma key OR color controls).
// Used by the render pipeline to decide whether to run the per-pixel pass.
inline bool cueHasPixelEffects(const Cue& cue) {
  return cue.chromaKeyEnabled || cueHasColorControls(cue);
}

// ---------------------------------------------------------------------------
// applyChromaKeyToPixels — Green-screen (or any color) removal.
//
// For each pixel, computes the Euclidean distance in RGB space from the key
// color. Pixels within `tolerance` distance are fully transparent (alpha=0).
// Pixels between `tolerance-softness` and `tolerance+softness` get a smooth
// alpha falloff, creating soft edges around the keyed region.
//
// The softness gradient uses a linear ramp (not cosine or spline) for speed.
// ---------------------------------------------------------------------------
inline void applyChromaKeyToPixels(std::vector<std::uint8_t>& pixels,
                                   SDL_Color keyColor,
                                   float tolerance,
                                   float softness) {
  if (pixels.empty()) {
    return;
  }
  tolerance = std::clamp(tolerance, 0.0f, 441.0f);
  softness = std::clamp(softness, 0.0f, 200.0f);
  float inner = std::max(0.0f, tolerance - softness);
  float outer = tolerance + softness;
  float span = std::max(0.0001f, outer - inner);
  // In bands across the effect workers: a 4K frame took ~40 ms on one core
  // (tools/check_hotspots.py, 2026-11-12). The same arithmetic per pixel, so
  // the same bytes.
  const std::size_t count = pixels.size() / 4;
  constexpr int kBand = 8192;
  const int bands = static_cast<int>((count + kBand - 1) / kBand);
  deckboy::effects::detail::parallelRows(bands, kBand, [&](int firstBand, int lastBand) {
  const std::size_t end = std::min(count, static_cast<std::size_t>(lastBand) * kBand) * 4;
  for (size_t i = static_cast<std::size_t>(firstBand) * kBand * 4; i < end; i += 4) {
    float dr = static_cast<float>(pixels[i + 0]) - static_cast<float>(keyColor.r);
    float dg = static_cast<float>(pixels[i + 1]) - static_cast<float>(keyColor.g);
    float db = static_cast<float>(pixels[i + 2]) - static_cast<float>(keyColor.b);
    float distance = std::sqrt(dr * dr + dg * dg + db * db);
    float keep = 1.0f;
    if (distance <= inner) {
      keep = 0.0f;
    } else if (distance < outer) {
      keep = (distance - inner) / span;
    }
    pixels[i + 3] = static_cast<std::uint8_t>(std::clamp(
      static_cast<int>(std::lround(static_cast<float>(pixels[i + 3]) * keep)),
      0,
      255));
  }
  });
}

// ---------------------------------------------------------------------------
// applyColorControlsToPixels — Brightness, contrast, saturation, hue shift.
//
// Processing order (per pixel):
//   1. Brightness: multiply RGB by brightness factor (0–2)
//   2. Contrast:   scale distance from 0.5 gray (0–2)
//   3. Saturation: blend between luminance and color (0–2)
//   4. Hue shift:  rotate chrominance in YIQ color space (-180° to +180°)
//
// The hue shift uses the YIQ color model (NTSC) rather than HSV because
// YIQ rotation preserves perceptual luminance better for broadcast use.
// Alpha channel is not modified.
// ---------------------------------------------------------------------------
inline void applyColorControlsToPixels(std::vector<std::uint8_t>& pixels,
                                       float brightness,
                                       float contrast,
                                       float saturation,
                                       float hueShiftDegrees) {
  if (pixels.empty()) {
    return;
  }
  brightness = std::clamp(brightness, 0.0f, 2.0f);
  contrast = std::clamp(contrast, 0.0f, 2.0f);
  saturation = std::clamp(saturation, 0.0f, 2.0f);
  hueShiftDegrees = std::clamp(hueShiftDegrees, -180.0f, 180.0f);
  if (!colorControlsActive(brightness, contrast, saturation, hueShiftDegrees)) {
    return;
  }

  constexpr float kPi = 3.14159265358979323846f;
  float hueRadians = hueShiftDegrees * (kPi / 180.0f);
  float cosHue = std::cos(hueRadians);
  float sinHue = std::sin(hueRadians);
  bool applyHue = std::fabs(hueShiftDegrees) > 0.001f;

  auto toByte = [](float value) -> std::uint8_t {
    float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
  };

  // Brightness then contrast depend on the channel alone: a 256-entry table
  // of the very same float steps, so the result is the same to the bit. The
  // rest per pixel, in bands across the effect workers -- this whole function
  // took ~110 ms on a 4K frame on one core (check_hotspots.py, 2026-11-12).
  float lift[256];
  for (int v = 0; v < 256; ++v) {
    float x = static_cast<float>(v) / 255.0f;
    x *= brightness;
    x = (x - 0.5f) * contrast + 0.5f;
    lift[v] = x;
  }
  const std::size_t count = pixels.size() / 4;
  constexpr int kBand = 8192;
  const int bands = static_cast<int>((count + kBand - 1) / kBand);
  deckboy::effects::detail::parallelRows(bands, kBand, [&](int firstBand, int lastBand) {
  const std::size_t end = std::min(count, static_cast<std::size_t>(lastBand) * kBand) * 4;
  for (size_t i = static_cast<std::size_t>(firstBand) * kBand * 4; i < end; i += 4) {
    float r = lift[pixels[i + 0]];
    float g = lift[pixels[i + 1]];
    float b = lift[pixels[i + 2]];

    float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    r = luma + (r - luma) * saturation;
    g = luma + (g - luma) * saturation;
    b = luma + (b - luma) * saturation;

    if (applyHue) {
      float y = 0.299f * r + 0.587f * g + 0.114f * b;
      float iCh = 0.596f * r - 0.274f * g - 0.322f * b;
      float qCh = 0.211f * r - 0.523f * g + 0.312f * b;
      float iRot = iCh * cosHue - qCh * sinHue;
      float qRot = iCh * sinHue + qCh * cosHue;
      r = y + 0.956f * iRot + 0.621f * qRot;
      g = y - 0.272f * iRot - 0.647f * qRot;
      b = y - 1.106f * iRot + 1.703f * qRot;
    }

    pixels[i + 0] = toByte(r);
    pixels[i + 1] = toByte(g);
    pixels[i + 2] = toByte(b);
  }
  });
}

// ---------------------------------------------------------------------------
// applyCueVisualEffectsToPixels — Combined chroma key + color correction.
//
// Applies both effects in the correct order: chroma key first (so the key
// operates on the original colors), then color controls (which may shift
// colors away from the key target). This is the main entry point called
// by MediaEngine for each decoded frame.
//
// Two overloads: one with explicit parameters, one that reads from a Cue.
// ---------------------------------------------------------------------------
inline void applyCueVisualEffectsToPixels(std::vector<std::uint8_t>& pixels,
                                          bool chromaKeyEnabled,
                                          SDL_Color chromaKeyColor,
                                          float chromaKeyTolerance,
                                          float chromaKeySoftness,
                                          float brightness,
                                          float contrast,
                                          float saturation,
                                          float hueShiftDegrees) {
  if (pixels.empty()) {
    return;
  }
  if (chromaKeyEnabled) {
    applyChromaKeyToPixels(pixels, chromaKeyColor, chromaKeyTolerance, chromaKeySoftness);
  }
  applyColorControlsToPixels(pixels, brightness, contrast, saturation, hueShiftDegrees);
}

// Convenience overload: extract all effect parameters from a Cue struct.
inline void applyCueVisualEffectsToPixels(std::vector<std::uint8_t>& pixels, const Cue& cue) {
  applyCueVisualEffectsToPixels(
    pixels,
    cue.chromaKeyEnabled,
    cue.chromaKeyColor,
    cue.chromaKeyTolerance,
    cue.chromaKeySoftness,
    cue.brightness,
    cue.contrast,
    cue.saturation,
    cue.hueShift
  );
}
