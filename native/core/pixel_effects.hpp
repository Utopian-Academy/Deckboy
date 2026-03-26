// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "types.hpp"

inline bool colorControlsActive(float brightness, float contrast, float saturation, float hueShiftDegrees) {
  constexpr float kEpsilon = 0.001f;
  return std::fabs(brightness - 1.0f) > kEpsilon
    || std::fabs(contrast - 1.0f) > kEpsilon
    || std::fabs(saturation - 1.0f) > kEpsilon
    || std::fabs(hueShiftDegrees) > kEpsilon;
}

inline bool cueHasColorControls(const Cue& cue) {
  return colorControlsActive(cue.brightness, cue.contrast, cue.saturation, cue.hueShift);
}

inline bool cueHasPixelEffects(const Cue& cue) {
  return cue.chromaKeyEnabled || cueHasColorControls(cue);
}

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
  for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
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
}

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

  for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
    float r = static_cast<float>(pixels[i + 0]) / 255.0f;
    float g = static_cast<float>(pixels[i + 1]) / 255.0f;
    float b = static_cast<float>(pixels[i + 2]) / 255.0f;

    r *= brightness;
    g *= brightness;
    b *= brightness;

    r = (r - 0.5f) * contrast + 0.5f;
    g = (g - 0.5f) * contrast + 0.5f;
    b = (b - 0.5f) * contrast + 0.5f;

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
}

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
