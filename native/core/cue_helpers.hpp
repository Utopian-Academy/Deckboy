// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <string>
#include "types.hpp"
#include "utils.hpp"

inline bool isSourceCueKind(CueKind kind) {
  return kind == CueKind::WindowSource
    || kind == CueKind::Camera
    || kind == CueKind::Syphon;
}

inline std::string defaultSourceRefForKind(CueKind kind) {
  switch (kind) {
    case CueKind::WindowSource: return "active-window";
    case CueKind::Camera:       return "default-camera";
    case CueKind::Syphon:       return "default-bus";
    default:                    return "source";
  }
}

inline std::string sourceCueRefFromCue(const Cue& cue) {
  if (!isSourceCueKind(cue.kind)) {
    return "";
  }
  std::string path = deckboy::core::utils::trim(cue.path);
  if (path.rfind("source://", 0) == 0) {
    size_t kindStart = std::string("source://").size();
    size_t slash = path.find('/', kindStart);
    if (slash != std::string::npos && slash + 1 < path.size()) {
      return deckboy::core::utils::trim(path.substr(slash + 1));
    }
  }
  return "";
}

inline CueEndAction resolvedCueEndAction(const Cue& cue) {
  if (cue.endAction != CueEndAction::Inherit) {
    return cue.endAction;
  }
  if (cue.loop) {
    return CueEndAction::Loop;
  }
  if (cue.pauseOnLastFrame) {
    return CueEndAction::PauseOnLast;
  }
  return CueEndAction::AutoNext;
}

inline bool cueAdvancesWhenFinished(const Cue& cue) {
  return resolvedCueEndAction(cue) == CueEndAction::AutoNext;
}
