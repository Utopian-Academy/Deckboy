// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.
#include "pocket/android_camera.hpp"

#if defined(__ANDROID__)

#include <SDL3/SDL.h>
#include <android/native_window.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImageReader.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace pocketcam {
namespace {

std::mutex gLock;                      // the newest frame and the error text
std::vector<std::uint8_t> gFrame;      // upright RGBA
int gFrameW = 0, gFrameH = 0;
bool gFresh = false;
std::string gError;

std::atomic<int> gPermission {-2};     // -2 never asked
std::atomic<int> gDisplayDeg {0};
std::atomic<float> gFocusNow {0.0f};   // the lens's focus distance, from the last result

ACameraManager* gManager = nullptr;
ACameraDevice* gDevice = nullptr;
ACameraCaptureSession* gSession = nullptr;
ACaptureRequest* gRequest = nullptr;
ACameraOutputTarget* gTarget = nullptr;
ACaptureSessionOutput* gOutput = nullptr;
ACaptureSessionOutputContainer* gOutputs = nullptr;
AImageReader* gReader = nullptr;
std::string gOpenId;
Caps gCaps;
int gSensorDeg = 0;
bool gFront = false;
int gArray[4] = {0, 0, 0, 0};          // active array: x, y, w, h
Controls gControls;
bool gTapped = false;
float gTapX = 0.5f, gTapY = 0.5f;

void fail(const std::string& what) {
  std::lock_guard<std::mutex> hold(gLock);
  gError = what;
  SDL_Log("pocket camera: %s", what.c_str());
}

ACameraManager* manager() {
  if (!gManager) gManager = ACameraManager_create();
  return gManager;
}

// How far the sensor's picture turns (clockwise) to stand upright.
int uprightDegrees() {
  const int dev = gDisplayDeg.load();
  return gFront ? (gSensorDeg + dev) % 360 : (gSensorDeg - dev + 360) % 360;
}

// YUV 4:2:0 (any strides) to RGBA, turned upright, into the shared frame.
void onImage(void*, AImageReader* reader) {
  AImage* image = nullptr;
  if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK || !image) return;
  int32_t w = 0, h = 0;
  AImage_getWidth(image, &w);
  AImage_getHeight(image, &h);
  uint8_t *py = nullptr, *pu = nullptr, *pv = nullptr;
  int ly = 0, lu = 0, lv = 0;
  int32_t sy = 0, su = 0, sv = 0, pxu = 1, pxv = 1;
  AImage_getPlaneData(image, 0, &py, &ly);
  AImage_getPlaneData(image, 1, &pu, &lu);
  AImage_getPlaneData(image, 2, &pv, &lv);
  AImage_getPlaneRowStride(image, 0, &sy);
  AImage_getPlaneRowStride(image, 1, &su);
  AImage_getPlaneRowStride(image, 2, &sv);
  AImage_getPlanePixelStride(image, 1, &pxu);
  AImage_getPlanePixelStride(image, 2, &pxv);
  if (!py || !pu || !pv || w <= 0 || h <= 0) {
    AImage_delete(image);
    return;
  }
  const int rot = uprightDegrees();
  const int ow = (rot == 90 || rot == 270) ? h : w;
  const int oh = (rot == 90 || rot == 270) ? w : h;
  std::vector<std::uint8_t> out(static_cast<std::size_t>(ow) * static_cast<std::size_t>(oh) * 4u);
  auto* dst = reinterpret_cast<std::uint32_t*>(out.data());
  for (int y = 0; y < h; ++y) {
    const uint8_t* yr = py + static_cast<std::ptrdiff_t>(y) * sy;
    const uint8_t* ur = pu + static_cast<std::ptrdiff_t>(y / 2) * su;
    const uint8_t* vr = pv + static_cast<std::ptrdiff_t>(y / 2) * sv;
    for (int x = 0; x < w; ++x) {
      // full-range BT.601, as a camera's YUV is
      const int Y = yr[x];
      const int U = ur[(x / 2) * pxu] - 128;
      const int V = vr[(x / 2) * pxv] - 128;
      const int r = std::clamp(Y + ((359 * V) >> 8), 0, 255);
      const int g = std::clamp(Y - ((88 * U + 183 * V) >> 8), 0, 255);
      const int b = std::clamp(Y + ((454 * U) >> 8), 0, 255);
      const std::uint32_t px = static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) |
                               (static_cast<std::uint32_t>(b) << 16) | 0xff000000u;
      std::size_t to;
      switch (rot) {
        case 90: to = static_cast<std::size_t>(x) * static_cast<std::size_t>(ow) + static_cast<std::size_t>(h - 1 - y); break;
        case 180: to = static_cast<std::size_t>(h - 1 - y) * static_cast<std::size_t>(ow) + static_cast<std::size_t>(w - 1 - x); break;
        case 270: to = static_cast<std::size_t>(w - 1 - x) * static_cast<std::size_t>(ow) + static_cast<std::size_t>(y); break;
        default: to = static_cast<std::size_t>(y) * static_cast<std::size_t>(ow) + static_cast<std::size_t>(x);
      }
      dst[to] = px;
    }
  }
  AImage_delete(image);
  std::lock_guard<std::mutex> hold(gLock);
  gFrame.swap(out);
  gFrameW = ow;
  gFrameH = oh;
  gFresh = true;
}

void onCaptureCompleted(void*, ACameraCaptureSession*, ACaptureRequest*, const ACameraMetadata* result) {
  ACameraMetadata_const_entry e {};
  if (ACameraMetadata_getConstEntry(result, ACAMERA_LENS_FOCUS_DISTANCE, &e) == ACAMERA_OK && e.count > 0) {
    gFocusNow.store(e.data.f[0]);
  }
}

ACameraCaptureSession_captureCallbacks gCaptureCallbacks = {nullptr, nullptr, nullptr, onCaptureCompleted, nullptr, nullptr, nullptr, nullptr};

void onDisconnected(void*, ACameraDevice*) { fail("the camera was taken by another app"); }
void onError(void*, ACameraDevice*, int error) { fail("camera error " + std::to_string(error)); }
ACameraDevice_StateCallbacks gDeviceCallbacks = {nullptr, onDisconnected, onError};

void sessionClosed(void*, ACameraCaptureSession*) {}
void sessionReady(void*, ACameraCaptureSession*) {}
void sessionActive(void*, ACameraCaptureSession*) {}
ACameraCaptureSession_stateCallbacks gSessionCallbacks = {nullptr, sessionClosed, sessionReady, sessionActive};

void setU8(ACaptureRequest* r, uint32_t tag, uint8_t v) { ACaptureRequest_setEntry_u8(r, tag, 1, &v); }

// The request from the controls, then repeat it.
void applyControls() {
  if (!gRequest || !gSession) return;
  ACaptureRequest* r = gRequest;
  setU8(r, ACAMERA_CONTROL_MODE, ACAMERA_CONTROL_MODE_AUTO);
  if (gCaps.zoomMax > gCaps.zoomMin) {
    const float z = std::clamp(gControls.zoom, gCaps.zoomMin, gCaps.zoomMax);
    ACaptureRequest_setEntry_float(r, ACAMERA_CONTROL_ZOOM_RATIO, 1, &z);
  }
  // focus: held, by hand, at a tapped point, or following the picture
  const bool canSetDistance = gCaps.minFocusDiopters > 0.0f;
  if (canSetDistance && (gControls.lock || gControls.manualFocus)) {
    setU8(r, ACAMERA_CONTROL_AF_MODE, ACAMERA_CONTROL_AF_MODE_OFF);
    const float d = gControls.lock && !gControls.manualFocus ? gFocusNow.load()
                                                             : std::clamp(gControls.focus, 0.0f, 1.0f) * gCaps.minFocusDiopters;
    ACaptureRequest_setEntry_float(r, ACAMERA_LENS_FOCUS_DISTANCE, 1, &d);
  } else if (gTapped) {
    setU8(r, ACAMERA_CONTROL_AF_MODE, ACAMERA_CONTROL_AF_MODE_AUTO);
  } else {
    setU8(r, ACAMERA_CONTROL_AF_MODE, ACAMERA_CONTROL_AF_MODE_CONTINUOUS_VIDEO);
  }
  setU8(r, ACAMERA_CONTROL_AF_TRIGGER, ACAMERA_CONTROL_AF_TRIGGER_IDLE);
  // exposure, the locks, the flashlight
  setU8(r, ACAMERA_CONTROL_AE_MODE, ACAMERA_CONTROL_AE_MODE_ON);
  const int32_t ev = std::clamp(gControls.exposure, gCaps.exposureMin, gCaps.exposureMax);
  ACaptureRequest_setEntry_i32(r, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &ev);
  setU8(r, ACAMERA_CONTROL_AE_LOCK, gControls.lock ? ACAMERA_CONTROL_AE_LOCK_ON : ACAMERA_CONTROL_AE_LOCK_OFF);
  setU8(r, ACAMERA_CONTROL_AWB_LOCK, gControls.lock ? ACAMERA_CONTROL_AWB_LOCK_ON : ACAMERA_CONTROL_AWB_LOCK_OFF);
  setU8(r, ACAMERA_FLASH_MODE, gControls.flashlight && gCaps.flash ? ACAMERA_FLASH_MODE_TORCH : ACAMERA_FLASH_MODE_OFF);
  ACameraCaptureSession_setRepeatingRequest(gSession, &gCaptureCallbacks, 1, &gRequest, nullptr);
}

}  // namespace

bool available() { return true; }

std::vector<CameraInfo> cameras() {
  std::vector<CameraInfo> out;
  ACameraIdList* ids = nullptr;
  if (!manager() || ACameraManager_getCameraIdList(gManager, &ids) != ACAMERA_OK || !ids) return out;
  int backs = 0, fronts = 0, others = 0;
  for (int i = 0; i < ids->numCameras; ++i) {
    ACameraMetadata* ch = nullptr;
    if (ACameraManager_getCameraCharacteristics(gManager, ids->cameraIds[i], &ch) != ACAMERA_OK || !ch) continue;
    ACameraMetadata_const_entry e {};
    std::string key;
    bool front = false;
    if (ACameraMetadata_getConstEntry(ch, ACAMERA_LENS_FACING, &e) == ACAMERA_OK && e.count > 0) {
      if (e.data.u8[0] == ACAMERA_LENS_FACING_FRONT) {
        front = true;
        key = ++fronts == 1 ? "front" : "front " + std::to_string(fronts);
      } else if (e.data.u8[0] == ACAMERA_LENS_FACING_BACK) {
        key = ++backs == 1 ? "back" : "back " + std::to_string(backs);
      }
    }
    if (key.empty()) key = "camera " + std::to_string(++others);
    ACameraMetadata_free(ch);
    out.push_back({ids->cameraIds[i], key, front});
  }
  ACameraManager_deleteCameraIdList(ids);
  return out;
}

int permission() {
  const int now = gPermission.load();
  if (now == -2) {
    gPermission.store(0);
    SDL_RequestAndroidPermission("android.permission.CAMERA",
                                 [](void*, const char*, bool granted) { gPermission.store(granted ? 1 : -1); }, nullptr);
    return 0;
  }
  return now;
}

bool open(const std::string& id, int wantW, int wantH) {
  close();
  {
    std::lock_guard<std::mutex> hold(gLock);
    gError.clear();
    gFresh = false;
  }
  if (!manager()) {
    fail("no camera manager");
    return false;
  }
  ACameraMetadata* ch = nullptr;
  if (ACameraManager_getCameraCharacteristics(gManager, id.c_str(), &ch) != ACAMERA_OK || !ch) {
    fail("no camera " + id);
    return false;
  }
  gCaps = Caps {};
  ACameraMetadata_const_entry e {};
  gSensorDeg = ACameraMetadata_getConstEntry(ch, ACAMERA_SENSOR_ORIENTATION, &e) == ACAMERA_OK && e.count ? e.data.i32[0] : 90;
  gFront = ACameraMetadata_getConstEntry(ch, ACAMERA_LENS_FACING, &e) == ACAMERA_OK && e.count && e.data.u8[0] == ACAMERA_LENS_FACING_FRONT;
  if (ACameraMetadata_getConstEntry(ch, ACAMERA_CONTROL_ZOOM_RATIO_RANGE, &e) == ACAMERA_OK && e.count >= 2) {
    gCaps.zoomMin = e.data.f[0];
    gCaps.zoomMax = e.data.f[1];
  }
  if (ACameraMetadata_getConstEntry(ch, ACAMERA_LENS_INFO_MINIMUM_FOCUS_DISTANCE, &e) == ACAMERA_OK && e.count) gCaps.minFocusDiopters = e.data.f[0];
  if (ACameraMetadata_getConstEntry(ch, ACAMERA_CONTROL_AE_COMPENSATION_RANGE, &e) == ACAMERA_OK && e.count >= 2) {
    gCaps.exposureMin = e.data.i32[0];
    gCaps.exposureMax = e.data.i32[1];
  }
  if (ACameraMetadata_getConstEntry(ch, ACAMERA_CONTROL_AE_COMPENSATION_STEP, &e) == ACAMERA_OK && e.count && e.data.r[0].denominator) {
    gCaps.exposureStep = static_cast<float>(e.data.r[0].numerator) / static_cast<float>(e.data.r[0].denominator);
  }
  gCaps.flash = ACameraMetadata_getConstEntry(ch, ACAMERA_FLASH_INFO_AVAILABLE, &e) == ACAMERA_OK && e.count && e.data.u8[0];
  if (ACameraMetadata_getConstEntry(ch, ACAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE, &e) == ACAMERA_OK && e.count >= 4) {
    for (int k = 0; k < 4; ++k) gArray[k] = e.data.i32[k];
  }
  // the YUV size nearest what was asked for, 16:9 first
  int bestW = 1280, bestH = 720;
  long bestScore = -1;
  if (ACameraMetadata_getConstEntry(ch, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &e) == ACAMERA_OK) {
    for (uint32_t k = 0; k + 3 < e.count; k += 4) {
      const int32_t fmt = e.data.i32[k], w = e.data.i32[k + 1], h = e.data.i32[k + 2], input = e.data.i32[k + 3];
      if (input || fmt != AIMAGE_FORMAT_YUV_420_888 || w <= 0 || h <= 0) continue;
      const long score = std::labs(static_cast<long>(w) * h - static_cast<long>(wantW) * wantH) +
                         (std::abs(w * 9 - h * 16) > w / 50 ? 3000000L : 0L);
      if (bestScore < 0 || score < bestScore) {
        bestScore = score;
        bestW = w;
        bestH = h;
      }
    }
  }
  ACameraMetadata_free(ch);

  if (AImageReader_new(bestW, bestH, AIMAGE_FORMAT_YUV_420_888, 4, &gReader) != AMEDIA_OK || !gReader) {
    fail("no image reader");
    close();
    return false;
  }
  AImageReader_ImageListener listener {nullptr, onImage};
  AImageReader_setImageListener(gReader, &listener);
  ANativeWindow* window = nullptr;
  AImageReader_getWindow(gReader, &window);
  if (ACameraManager_openCamera(gManager, id.c_str(), &gDeviceCallbacks, &gDevice) != ACAMERA_OK || !gDevice) {
    fail("the camera did not open (is another app using it?)");
    close();
    return false;
  }
  ACaptureSessionOutputContainer_create(&gOutputs);
  ACaptureSessionOutput_create(window, &gOutput);
  ACaptureSessionOutputContainer_add(gOutputs, gOutput);
  ACameraOutputTarget_create(window, &gTarget);
  ACameraDevice_createCaptureRequest(gDevice, TEMPLATE_RECORD, &gRequest);
  ACaptureRequest_addTarget(gRequest, gTarget);
  if (ACameraDevice_createCaptureSession(gDevice, gOutputs, &gSessionCallbacks, &gSession) != ACAMERA_OK) {
    fail("the camera session did not start");
    close();
    return false;
  }
  gOpenId = id;
  gTapped = false;
  applyControls();
  SDL_Log("pocket camera: %s open at %dx%d, zoom %.2f-%.2f, focus %.1f dpt, ev %d..%d, flash %d", id.c_str(), bestW, bestH,
          gCaps.zoomMin, gCaps.zoomMax, gCaps.minFocusDiopters, gCaps.exposureMin, gCaps.exposureMax, gCaps.flash ? 1 : 0);
  return true;
}

void close() {
  if (gSession) {
    ACameraCaptureSession_stopRepeating(gSession);
    ACameraCaptureSession_close(gSession);
    gSession = nullptr;
  }
  if (gRequest) {
    if (gTarget) ACaptureRequest_removeTarget(gRequest, gTarget);
    ACaptureRequest_free(gRequest);
    gRequest = nullptr;
  }
  if (gTarget) {
    ACameraOutputTarget_free(gTarget);
    gTarget = nullptr;
  }
  if (gOutputs) {
    if (gOutput) ACaptureSessionOutputContainer_remove(gOutputs, gOutput);
    ACaptureSessionOutputContainer_free(gOutputs);
    gOutputs = nullptr;
  }
  if (gOutput) {
    ACaptureSessionOutput_free(gOutput);
    gOutput = nullptr;
  }
  if (gDevice) {
    ACameraDevice_close(gDevice);
    gDevice = nullptr;
  }
  if (gReader) {
    AImageReader_delete(gReader);
    gReader = nullptr;
  }
  gOpenId.clear();
}

bool isOpen() { return gSession != nullptr; }
std::string openId() { return gOpenId; }
Caps caps() { return gCaps; }

void setControls(const Controls& c) {
  const bool changed = c.zoom != gControls.zoom || c.manualFocus != gControls.manualFocus || c.focus != gControls.focus ||
                       c.exposure != gControls.exposure || c.lock != gControls.lock || c.flashlight != gControls.flashlight;
  if (c.manualFocus || c.lock) gTapped = false;
  gControls = c;
  if (changed) applyControls();
}

void tapFocus(float x, float y) {
  if (!gRequest || !gSession || gControls.manualFocus || gControls.lock) return;
  // the upright point back onto the sensor, which the picture was turned from
  float sx = x, sy = y;
  switch (uprightDegrees()) {
    case 90: sx = y; sy = 1.0f - x; break;
    case 180: sx = 1.0f - x; sy = 1.0f - y; break;
    case 270: sx = 1.0f - y; sy = x; break;
    default: break;
  }
  const int aw = gArray[2] > 0 ? gArray[2] : 4000, ah = gArray[3] > 0 ? gArray[3] : 3000;
  const int cx = gArray[0] + static_cast<int>(std::clamp(sx, 0.0f, 1.0f) * static_cast<float>(aw));
  const int cy = gArray[1] + static_cast<int>(std::clamp(sy, 0.0f, 1.0f) * static_cast<float>(ah));
  const int half = std::max(aw, ah) / 16;
  const int32_t region[5] = {std::max(gArray[0], cx - half), std::max(gArray[1], cy - half),
                             std::min(gArray[0] + aw - 1, cx + half), std::min(gArray[1] + ah - 1, cy + half), 1000};
  ACaptureRequest_setEntry_i32(gRequest, ACAMERA_CONTROL_AF_REGIONS, 5, region);
  ACaptureRequest_setEntry_i32(gRequest, ACAMERA_CONTROL_AE_REGIONS, 5, region);
  gTapped = true;
  applyControls();
  // one request that starts the focus scan, then back to repeating
  setU8(gRequest, ACAMERA_CONTROL_AF_TRIGGER, ACAMERA_CONTROL_AF_TRIGGER_START);
  ACameraCaptureSession_capture(gSession, &gCaptureCallbacks, 1, &gRequest, nullptr);
  setU8(gRequest, ACAMERA_CONTROL_AF_TRIGGER, ACAMERA_CONTROL_AF_TRIGGER_IDLE);
}

void setDisplayRotation(int degrees) { gDisplayDeg.store(((degrees % 360) + 360) % 360); }

bool takeFrame(std::vector<std::uint8_t>& rgba, int& w, int& h) {
  std::lock_guard<std::mutex> hold(gLock);
  if (!gFresh) return false;
  rgba.swap(gFrame);
  w = gFrameW;
  h = gFrameH;
  gFresh = false;
  return true;
}

std::string lastError() {
  std::lock_guard<std::mutex> hold(gLock);
  return gError;
}

}  // namespace pocketcam

#else   // a desktop: SDL's camera does the work (pocket_main.cpp)

namespace pocketcam {
bool available() { return false; }
std::vector<CameraInfo> cameras() { return {}; }
int permission() { return 1; }
bool open(const std::string&, int, int) { return false; }
void close() {}
bool isOpen() { return false; }
std::string openId() { return {}; }
Caps caps() { return {}; }
void setControls(const Controls&) {}
void tapFocus(float, float) {}
void setDisplayRotation(int) {}
bool takeFrame(std::vector<std::uint8_t>&, int&, int&) { return false; }
std::string lastError() { return {}; }
}  // namespace pocketcam

#endif
