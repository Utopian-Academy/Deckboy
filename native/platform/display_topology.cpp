// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "platform/display_topology.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#else
#include "platform/dynamic_library.hpp"
#if __has_include(<X11/extensions/Xrandr.h>)
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#define DECKBOY_DISPLAY_TOPOLOGY_XRANDR 1
#endif
#endif

namespace deckboy::platform {

namespace {

// Every panel showing the display, minus the one SDL already calls it by. When
// none of them carries SDL's name, drop the first so the count still adds up.
void fillOtherPanels(DisplayMirrorInfo& info, std::vector<std::string> panels,
                     const char* sdlName) {
  info.panelCount = std::max(1, static_cast<int>(panels.size()));
  if (panels.empty()) {
    return;
  }
  auto self = sdlName ? std::find(panels.begin(), panels.end(), std::string(sdlName))
                      : panels.end();
  panels.erase(self != panels.end() ? self : panels.begin());
  info.otherPanels = std::move(panels);
}

#if defined(_WIN32)

std::string narrow(const wchar_t* text) {
  int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 1) {
    return {};
  }
  std::string out(static_cast<std::size_t>(bytes - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
  return out;
}

// Windows mirrors by giving several TARGETS (monitors) one SOURCE (desktop).
// The SDL display is a monitor handle, which names its source by GDI device
// ("\\.\DISPLAY3"); every active path with that source is a screen showing it.
DisplayMirrorInfo queryPlatform(int x, int y, int w, int h, const char* sdlName) {
  DisplayMirrorInfo info;
  POINT centre {x + w / 2, y + h / 2};
  HMONITOR monitor = MonitorFromPoint(centre, MONITOR_DEFAULTTONULL);
  MONITORINFOEXW monitorInfo {};
  monitorInfo.cbSize = sizeof(monitorInfo);
  if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
    return info;
  }

  UINT32 pathCount = 0;
  UINT32 modeCount = 0;
  std::vector<DISPLAYCONFIG_PATH_INFO> paths;
  std::vector<DISPLAYCONFIG_MODE_INFO> modes;
  LONG status = ERROR_INSUFFICIENT_BUFFER;
  // The topology can change between sizing and querying; that is the one
  // error worth a retry.
  for (int attempt = 0; attempt < 3 && status == ERROR_INSUFFICIENT_BUFFER; ++attempt) {
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
      return info;
    }
    paths.resize(pathCount);
    modes.resize(modeCount);
    status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                                &modeCount, modes.data(), nullptr);
  }
  if (status != ERROR_SUCCESS) {
    return info;
  }
  paths.resize(pathCount);

  std::vector<std::string> panels;
  for (const DISPLAYCONFIG_PATH_INFO& path : paths) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source {};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
        wcscmp(source.viewGdiDeviceName, monitorInfo.szDevice) != 0) {
      continue;
    }
    DISPLAYCONFIG_TARGET_DEVICE_NAME target {};
    target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    target.header.size = sizeof(target);
    target.header.adapterId = path.targetInfo.adapterId;
    target.header.id = path.targetInfo.id;
    std::string name;
    if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) {
      name = narrow(target.monitorFriendlyDeviceName);
      if (name.empty() &&
          target.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL) {
        name = "built-in display";
      }
    }
    panels.push_back(name.empty() ? std::string("unnamed display") : name);
  }
  if (panels.empty()) {
    return info;
  }
  info.known = true;
  fillOtherPanels(info, std::move(panels), sdlName);
  return info;
}

#elif defined(__APPLE__)

// macOS lists every screen in a mirror set as online; all but the set's
// master answer CGDisplayMirrorsDisplay with the master's id. SDL's display
// is the master, and its bounds are CoreGraphics' global coordinates.
DisplayMirrorInfo queryPlatform(int x, int y, int w, int h, const char* sdlName) {
  DisplayMirrorInfo info;
  CGDirectDisplayID ids[32];
  uint32_t count = 0;
  if (CGGetOnlineDisplayList(32, ids, &count) != kCGErrorSuccess || count == 0) {
    return info;
  }
  CGPoint centre = CGPointMake(x + w / 2.0, y + h / 2.0);
  CGDirectDisplayID master = kCGNullDirectDisplay;
  for (uint32_t i = 0; i < count; ++i) {
    if (CGDisplayMirrorsDisplay(ids[i]) == kCGNullDirectDisplay &&
        CGRectContainsPoint(CGDisplayBounds(ids[i]), centre)) {
      master = ids[i];
      break;
    }
  }
  if (master == kCGNullDirectDisplay) {
    return info;
  }
  // CoreGraphics has no display names (those live on NSScreen, which lists
  // only one screen per mirror set), so the panels are named by kind. SDL's
  // name is kept for the master so it is not repeated as its own mirror.
  std::vector<std::string> panels;
  for (uint32_t i = 0; i < count; ++i) {
    if (ids[i] == master) {
      panels.push_back(sdlName ? sdlName : "");
    } else if (CGDisplayMirrorsDisplay(ids[i]) == master) {
      panels.push_back(CGDisplayIsBuiltin(ids[i]) ? "built-in display" : "external display");
    }
  }
  info.known = true;
  fillOtherPanels(info, std::move(panels), sdlName);
  return info;
}

#elif defined(DECKBOY_DISPLAY_TOPOLOGY_XRANDR)

// X11 mirrors two ways: several outputs driven by one CRTC (`--same-as` on a
// single GPU), or several CRTCs scanning out the same rectangle. Both count.
// libX11/libXrandr are loaded at runtime -- SDL loads them the same way, and a
// link dependency here would break Wayland-only and headless builds.
DisplayMirrorInfo queryPlatform(int x, int y, int w, int h, const char* sdlName) {
  DisplayMirrorInfo info;
  // Under a Wayland compositor RandR is XWayland's reconstruction, not the
  // compositor's own arrangement. XDG_SESSION_TYPE is not always set (WSLg
  // leaves it empty), so WAYLAND_DISPLAY counts too.
  const char* session = std::getenv("XDG_SESSION_TYPE");
  const char* wayland = std::getenv("WAYLAND_DISPLAY");
  if ((session && std::strcmp(session, "wayland") == 0) || (wayland && *wayland)) {
    return info;
  }
  static DynamicLibrary x11({"libX11.so.6", "libX11.so"});
  static DynamicLibrary xrandr({"libXrandr.so.2", "libXrandr.so"});
  if ((!x11.isLoaded() && !x11.load()) || (!xrandr.isLoaded() && !xrandr.load())) {
    return info;
  }
  auto openDisplay = x11.loadSymbol<Display* (*)(const char*)>("XOpenDisplay");
  auto closeDisplay = x11.loadSymbol<int (*)(Display*)>("XCloseDisplay");
  auto rootWindow = x11.loadSymbol<Window (*)(Display*)>("XDefaultRootWindow");
  auto getResources = xrandr.loadSymbol<XRRScreenResources* (*)(Display*, Window)>(
    "XRRGetScreenResourcesCurrent");
  auto freeResources = xrandr.loadSymbol<void (*)(XRRScreenResources*)>("XRRFreeScreenResources");
  auto getCrtc = xrandr.loadSymbol<XRRCrtcInfo* (*)(Display*, XRRScreenResources*, RRCrtc)>(
    "XRRGetCrtcInfo");
  auto freeCrtc = xrandr.loadSymbol<void (*)(XRRCrtcInfo*)>("XRRFreeCrtcInfo");
  auto getOutput = xrandr.loadSymbol<XRROutputInfo* (*)(Display*, XRRScreenResources*, RROutput)>(
    "XRRGetOutputInfo");
  auto freeOutput = xrandr.loadSymbol<void (*)(XRROutputInfo*)>("XRRFreeOutputInfo");
  if (!openDisplay || !closeDisplay || !rootWindow || !getResources || !freeResources ||
      !getCrtc || !freeCrtc || !getOutput || !freeOutput) {
    return info;
  }
  Display* display = openDisplay(nullptr);
  if (!display) {
    return info;
  }
  int cx = x + w / 2;
  int cy = y + h / 2;
  std::vector<std::string> panels;
  if (XRRScreenResources* resources = getResources(display, rootWindow(display))) {
    // The CRTC whose rectangle holds the SDL display's centre defines the
    // picture; every output on any CRTC with that same rectangle shows it.
    int rx = 0, ry = 0;
    unsigned int rw = 0, rh = 0;
    bool found = false;
    for (int i = 0; i < resources->ncrtc && !found; ++i) {
      if (XRRCrtcInfo* crtc = getCrtc(display, resources, resources->crtcs[i])) {
        if (crtc->mode != None && crtc->noutput > 0 && cx >= crtc->x && cy >= crtc->y &&
            cx < crtc->x + static_cast<int>(crtc->width) &&
            cy < crtc->y + static_cast<int>(crtc->height)) {
          rx = crtc->x; ry = crtc->y; rw = crtc->width; rh = crtc->height;
          found = true;
        }
        freeCrtc(crtc);
      }
    }
    for (int i = 0; i < resources->ncrtc && found; ++i) {
      XRRCrtcInfo* crtc = getCrtc(display, resources, resources->crtcs[i]);
      if (!crtc) {
        continue;
      }
      if (crtc->mode != None && crtc->x == rx && crtc->y == ry &&
          crtc->width == rw && crtc->height == rh) {
        for (int o = 0; o < crtc->noutput; ++o) {
          if (XRROutputInfo* output = getOutput(display, resources, crtc->outputs[o])) {
            panels.emplace_back(output->name, static_cast<std::size_t>(output->nameLen));
            freeOutput(output);
          }
        }
      }
      freeCrtc(crtc);
    }
    freeResources(resources);
  }
  closeDisplay(display);
  if (panels.empty()) {
    return info;
  }
  info.known = true;
  fillOtherPanels(info, std::move(panels), sdlName);
  return info;
}

#else

DisplayMirrorInfo queryPlatform(int, int, int, int, const char*) {
  return {};
}

#endif

}  // namespace

DisplayMirrorInfo queryDisplayMirrorInfo(int x, int y, int w, int h, const char* sdlName) {
  if (w <= 0 || h <= 0) {
    return {};
  }
  return queryPlatform(x, y, w, h, sdlName);
}

}  // namespace deckboy::platform
