// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.
#pragma once

// POCKET'S CAMERA ON ANDROID, with its controls. SDL's camera gives frames
// and nothing else, so on a phone Pocket talks to Camera2 itself (the NDK's
// ACameraManager / ACameraDevice / AImageReader) for what an operator needs
// from a camera on a show: zoom across every back lens (a logical camera's
// zoom ratio reaches the ultrawide and the telephoto), focus (auto, a tap on
// the picture, or a distance), exposure, LOCK (focus, exposure and white
// balance held so the picture does not pump when someone walks past) and the
// flashlight. Frames come out as upright RGBA. Android only; on a desktop
// Pocket keeps SDL's camera.

#include <cstdint>
#include <string>
#include <vector>

namespace pocketcam {

struct CameraInfo {
  std::string id;      // Camera2's id
  std::string key;     // "back", "front", "back 2"...
  bool front = false;
};

// What a camera can do, read once it is open.
struct Caps {
  float zoomMin = 1.0f, zoomMax = 1.0f;
  float minFocusDiopters = 0.0f;   // 0: fixed focus
  int exposureMin = 0, exposureMax = 0;
  float exposureStep = 0.0f;       // EV per step
  bool flash = false;
};

struct Controls {
  float zoom = 1.0f;
  bool manualFocus = false;
  float focus = 0.0f;         // 0 far (infinity) .. 1 the nearest it can
  int exposure = 0;           // steps of Caps::exposureStep
  bool lock = false;
  bool flashlight = false;
};

bool available();                          // built for Android
std::vector<CameraInfo> cameras();
// 1 allowed, 0 asked and waiting, -1 refused. Asks the first time.
int permission();
bool open(const std::string& id, int wantW, int wantH);
void close();
bool isOpen();
std::string openId();
Caps caps();
void setControls(const Controls& c);
// Focus (and meter) on a point of the upright picture, 0-1 each way.
void tapFocus(float x, float y);
// The device's rotation in degrees (0, 90, 180, 270), for turning frames upright.
void setDisplayRotation(int degrees);
// The newest frame, upright, if one arrived since the last call.
bool takeFrame(std::vector<std::uint8_t>& rgba, int& w, int& h);
std::string lastError();

}  // namespace pocketcam
