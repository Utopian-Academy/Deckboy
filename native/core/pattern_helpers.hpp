// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <algorithm>
#include <string>
#include "utils.hpp"

inline bool endsWith(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string normalizePatternTypeId(std::string value) {
  value = deckboy::core::utils::toLower(deckboy::core::utils::trim(std::move(value)));
  if (value.rfind("pattern://", 0) == 0) {
    value = value.substr(10);
  }
  std::replace(value.begin(), value.end(), '_', '-');
  if (value == "colourbars" || value == "colorbars" || value == "smpte75") {
    value = "smpte-bars";
  } else if (value == "kawaii" || value == "kawaii-pocket") {
    value = "pocket-test";
  } else if (value == "pocket-daytime" || value == "pocket-sunny") {
    value = "pocket-day";
  } else if (value == "pocket-dusk" || value == "pocket-golden") {
    value = "pocket-sunset";
  } else if (value == "pocket-moon" || value == "pocket-evening") {
    value = "pocket-night";
  } else if (value == "pocket-rain" || value == "pocket-tempest") {
    value = "pocket-storm";
  } else if (value == "smpte-bars-animated") {
    value = "smpte-bars-motion";
  } else if (value == "crosshatch-animated") {
    value = "crosshatch-motion";
  } else if (value == "checkerboard-animated") {
    value = "checkerboard-motion";
  } else if (value == "full-white-animated") {
    value = "full-white-motion";
  } else if (value == "full-black-animated") {
    value = "full-black-motion";
  } else if (value == "full-red-animated") {
    value = "full-red-motion";
  } else if (value == "full-green-animated") {
    value = "full-green-motion";
  } else if (value == "full-blue-animated") {
    value = "full-blue-motion";
  }
  return value;
}

inline std::string stripPatternMotionSuffix(std::string typeId) {
  typeId = normalizePatternTypeId(std::move(typeId));
  if (endsWith(typeId, "-motion")) {
    typeId = typeId.substr(0, typeId.size() - 7);
  }
  return typeId;
}

inline bool patternTypeSupportsMotion(const std::string& typeId) {
  std::string base = stripPatternMotionSuffix(typeId);
  return base == "smpte-bars" || base == "crosshatch" || base == "checkerboard" ||
         base == "full-white" || base == "full-black" || base == "full-red" ||
         base == "full-green" || base == "full-blue";
}

inline bool patternTypeIsAnimated(const std::string& typeId) {
  std::string normalized = normalizePatternTypeId(typeId);
  return normalized.rfind("pocket-", 0) == 0 ||
         normalized.find("kawaii") != std::string::npos ||
         endsWith(normalized, "-motion");
}
