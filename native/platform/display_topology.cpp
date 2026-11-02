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
#include <d3d11.h>
#include <dxgi1_3.h>   // IDXGISwapChainMedia
#include <dwmapi.h>    // DWM_TIMING_INFO; the functions are loaded at run time
#include <shobjidl.h>  // ITaskbarList2
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

const char* dxgiPresentationModeName(int mode) {
  // DXGI_FRAME_PRESENTATION_MODE's values, spelled out so this compiles where
  // there is no DXGI.
  switch (mode) {
    case 0: return "composed";
    case 1: return "overlay";
    case 2: return "none";
    case 3: return "failure";
    default: return "unknown";
  }
}

#if defined(_WIN32)

namespace {

// dwmapi is loaded, not linked: only the WebView2 build links it (see
// windowFrameBounds in capture_backend.cpp).
HMODULE dwmLibrary() {
  static HMODULE lib = LoadLibraryW(L"dwmapi.dll");
  return lib;
}

using DwmGetCompositionTimingInfoFn = HRESULT (WINAPI*)(HWND, DWM_TIMING_INFO*);
using DwmGetWindowAttributeFn = HRESULT (WINAPI*)(HWND, DWORD, PVOID, DWORD);

DwmGetCompositionTimingInfoFn dwmTimingInfo() {
  static DwmGetCompositionTimingInfoFn fn = dwmLibrary()
    ? reinterpret_cast<DwmGetCompositionTimingInfoFn>(GetProcAddress(dwmLibrary(), "DwmGetCompositionTimingInfo"))
    : nullptr;
  return fn;
}

DwmGetWindowAttributeFn dwmWindowAttribute() {
  static DwmGetWindowAttributeFn fn = dwmLibrary()
    ? reinterpret_cast<DwmGetWindowAttributeFn>(GetProcAddress(dwmLibrary(), "DwmGetWindowAttribute"))
    : nullptr;
  return fn;
}

using DwmSetWindowAttributeFn = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

DwmSetWindowAttributeFn dwmSetWindowAttribute() {
  static DwmSetWindowAttributeFn fn = dwmLibrary()
    ? reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwmLibrary(), "DwmSetWindowAttribute"))
    : nullptr;
  return fn;
}

// The GPU a Direct3D 11 device draws on, and whether it is the one whose
// outputs include `monitor`. A frame drawn on one GPU and shown by another
// has to be copied across, and Windows composes every such frame.
int adapterDrivesMonitor(ID3D11Device* device, HMONITOR monitor, std::string& drawName,
                         std::string& screenName) {
  IDXGIDevice* dxgiDevice = nullptr;
  if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) ||
      !dxgiDevice) {
    return -1;
  }
  IDXGIAdapter* drawAdapter = nullptr;
  const HRESULT adapterResult = dxgiDevice->GetAdapter(&drawAdapter);
  dxgiDevice->Release();
  if (FAILED(adapterResult) || !drawAdapter) {
    return -1;
  }
  DXGI_ADAPTER_DESC drawDesc {};
  drawAdapter->GetDesc(&drawDesc);
  drawName = narrow(drawDesc.Description);
  IDXGIFactory1* factory = nullptr;
  const HRESULT factoryResult =
    drawAdapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
  drawAdapter->Release();
  if (FAILED(factoryResult) || !factory) {
    return -1;
  }
  int verdict = -1;
  for (UINT a = 0; verdict < 0; ++a) {
    IDXGIAdapter1* adapter = nullptr;
    if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND || !adapter) {
      break;
    }
    for (UINT o = 0;; ++o) {
      IDXGIOutput* output = nullptr;
      if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND || !output) {
        break;
      }
      DXGI_OUTPUT_DESC outputDesc {};
      const bool matches = SUCCEEDED(output->GetDesc(&outputDesc)) && outputDesc.Monitor == monitor;
      output->Release();
      if (matches) {
        DXGI_ADAPTER_DESC1 desc {};
        adapter->GetDesc1(&desc);
        screenName = narrow(desc.Description);
        verdict = (desc.AdapterLuid.LowPart == drawDesc.AdapterLuid.LowPart &&
                   desc.AdapterLuid.HighPart == drawDesc.AdapterLuid.HighPart) ? 1 : 0;
        break;
      }
    }
    adapter->Release();
  }
  factory->Release();
  return verdict;
}

// Visible windows ABOVE `self` in z-order that overlap `screen`. Any one of
// them -- a notification, an overlay, another window's edge -- keeps a
// fullscreen window composed. EnumWindows walks top-level windows from the
// top of the z-order down, so the walk stops at `self`.
struct WindowsAboveSearch {
  HWND self = nullptr;
  RECT screen {};
  int count = 0;
  bool reachedSelf = false;
  std::string titles;
};

BOOL CALLBACK windowsAboveProc(HWND hwnd, LPARAM param) {
  auto* search = reinterpret_cast<WindowsAboveSearch*>(param);
  if (hwnd == search->self) {
    search->reachedSelf = true;
    return FALSE;
  }
  if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
    return TRUE;
  }
  RECT bounds {};
  if (DwmGetWindowAttributeFn attribute = dwmWindowAttribute()) {
    constexpr DWORD kCloaked = 14;               // DWMWA_CLOAKED
    constexpr DWORD kExtendedFrameBounds = 9;    // DWMWA_EXTENDED_FRAME_BOUNDS
    DWORD cloaked = 0;
    if (SUCCEEDED(attribute(hwnd, kCloaked, &cloaked, sizeof(cloaked))) && cloaked != 0) {
      return TRUE;   // on another virtual desktop, or a suspended app's: not drawn
    }
    if (FAILED(attribute(hwnd, kExtendedFrameBounds, &bounds, sizeof(bounds)))) {
      GetWindowRect(hwnd, &bounds);
    }
  } else {
    GetWindowRect(hwnd, &bounds);
  }
  RECT overlap {};
  if (!IntersectRect(&overlap, &bounds, &search->screen)) {
    return TRUE;
  }
  ++search->count;
  if (search->titles.size() < 240) {
    wchar_t title[96] = L"";
    GetWindowTextW(hwnd, title, 96);
    wchar_t className[64] = L"";
    GetClassNameW(hwnd, className, 64);
    if (!search->titles.empty()) search->titles += " | ";
    std::string name = narrow(title);
    search->titles += name.empty() ? ("[" + narrow(className) + "]") : name;
  }
  return TRUE;
}

}  // namespace

CompositorTiming queryCompositorTiming() {
  CompositorTiming timing;
  DwmGetCompositionTimingInfoFn query = dwmTimingInfo();
  if (!query) {
    return timing;
  }
  DWM_TIMING_INFO info {};
  info.cbSize = sizeof(info);
  // NULL: from Windows 8.1 it must be, and the answer is DWM's own clock.
  if (FAILED(query(nullptr, &info))) {
    return timing;
  }
  timing.valid = true;
  if (info.rateRefresh.uiNumerator > 0 && info.rateRefresh.uiDenominator > 0) {
    timing.hz = static_cast<double>(info.rateRefresh.uiNumerator) / info.rateRefresh.uiDenominator;
  } else {
    LARGE_INTEGER frequency {};
    if (info.qpcRefreshPeriod > 0 && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
      timing.hz = static_cast<double>(frequency.QuadPart) / static_cast<double>(info.qpcRefreshPeriod);
    }
  }
  timing.vblankQpc = static_cast<std::int64_t>(info.qpcVBlank);
  timing.refreshCount = info.cRefresh;
  timing.frameCount = info.cFrame;
  timing.framesDropped = info.cFramesDropped;
  timing.framesMissed = info.cFramesMissed;
  return timing;
}

double compositorRefreshHz() {
  return queryCompositorTiming().hz;
}

PresentPathInfo queryPresentPath(void* hwndPointer, void* d3d11Device, void* dxgiSwapChain,
                                 bool windowsAbove) {
  PresentPathInfo info;
  info.compositorHz = compositorRefreshHz();
  HWND hwnd = static_cast<HWND>(hwndPointer);
  if (!hwnd) {
    return info;
  }
  info.known = true;
  HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitorInfo {};
  monitorInfo.cbSize = sizeof(monitorInfo);
  if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
    RECT windowRect {};
    if (GetWindowRect(hwnd, &windowRect)) {
      info.coversScreen = EqualRect(&windowRect, &monitorInfo.rcMonitor) ? 1 : 0;
    }
    if (windowsAbove) {
      WindowsAboveSearch search;
      search.self = hwnd;
      search.screen = monitorInfo.rcMonitor;
      EnumWindows(windowsAboveProc, reinterpret_cast<LPARAM>(&search));
      if (search.reachedSelf) {
        info.windowsAbove = search.count;
        info.windowsAboveTitles = std::move(search.titles);
      }
    }
  }
  if (d3d11Device && monitor) {
    info.sameAdapter = adapterDrivesMonitor(static_cast<ID3D11Device*>(d3d11Device), monitor,
                                            info.drawAdapter, info.screenAdapter);
  }
  if (dxgiSwapChain) {
    auto* swapChain = static_cast<IDXGISwapChain*>(dxgiSwapChain);
    IDXGISwapChainMedia* media = nullptr;
    if (SUCCEEDED(swapChain->QueryInterface(__uuidof(IDXGISwapChainMedia), reinterpret_cast<void**>(&media))) &&
        media) {
      DXGI_FRAME_STATISTICS_MEDIA stats {};
      if (SUCCEEDED(media->GetFrameStatisticsMedia(&stats))) {
        info.dxgiMode = static_cast<int>(stats.CompositionMode);
      }
      media->Release();
    }
  }
  return info;
}

PresentStats queryPresentStats(void* dxgiSwapChain) {
  PresentStats stats;
  if (!dxgiSwapChain) {
    return stats;
  }
  auto* swapChain = static_cast<IDXGISwapChain*>(dxgiSwapChain);
  UINT last = 0;
  if (SUCCEEDED(swapChain->GetLastPresentCount(&last))) {
    stats.lastPresentCount = last;
  }
  DXGI_FRAME_STATISTICS frame {};
  // DXGI_ERROR_FRAME_STATISTICS_DISJOINT after a mode change, and not every
  // windowed chain reports at all: either way the sample is just not valid.
  if (SUCCEEDED(swapChain->GetFrameStatistics(&frame))) {
    stats.valid = true;
    stats.presentCount = frame.PresentCount;
    stats.presentRefreshCount = frame.PresentRefreshCount;
    stats.syncRefreshCount = frame.SyncRefreshCount;
    stats.syncQpcTime = frame.SyncQPCTime.QuadPart;
  }
  return stats;
}

bool windowCoveredOnItsScreen(void* hwndPointer) {
  HWND hwnd = static_cast<HWND>(hwndPointer);
  MONITORINFO monitorInfo {};
  monitorInfo.cbSize = sizeof(monitorInfo);
  if (!hwnd || !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
    return false;
  }
  const DWORD self = GetCurrentProcessId();
  DwmGetWindowAttributeFn attribute = dwmWindowAttribute();
  int steps = 0;
  // GW_HWNDPREV walks UP the z-order from the window: exactly what is over it.
  for (HWND above = GetWindow(hwnd, GW_HWNDPREV); above && steps < 256;
       above = GetWindow(above, GW_HWNDPREV), ++steps) {
    if (!IsWindowVisible(above) || IsIconic(above)) {
      continue;
    }
    DWORD owner = 0;
    GetWindowThreadProcessId(above, &owner);
    if (owner == self) {
      continue;
    }
    RECT bounds {};
    if (attribute) {
      constexpr DWORD kCloaked = 14;               // DWMWA_CLOAKED
      constexpr DWORD kExtendedFrameBounds = 9;    // DWMWA_EXTENDED_FRAME_BOUNDS
      DWORD cloaked = 0;
      if (SUCCEEDED(attribute(above, kCloaked, &cloaked, sizeof(cloaked))) && cloaked != 0) {
        continue;
      }
      if (FAILED(attribute(above, kExtendedFrameBounds, &bounds, sizeof(bounds)))) {
        GetWindowRect(above, &bounds);
      }
    } else {
      GetWindowRect(above, &bounds);
    }
    RECT overlap {};
    if (IntersectRect(&overlap, &bounds, &monitorInfo.rcMonitor)) {
      return true;
    }
  }
  return false;
}

void raiseOutputWindow(void* hwndPointer) {
  if (HWND hwnd = static_cast<HWND>(hwndPointer)) {
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  }
}

void resetFullscreenDwmAttributes(void* hwndPointer) {
  HWND hwnd = static_cast<HWND>(hwndPointer);
  DwmSetWindowAttributeFn set = dwmSetWindowAttribute();
  if (!hwnd || !set) {
    return;
  }
  // By number, so this builds against SDKs older than Windows 11's (22000).
  constexpr DWORD kCornerPreference = 33;     // DWMWA_WINDOW_CORNER_PREFERENCE
  constexpr DWORD kBorderColour = 34;         // DWMWA_BORDER_COLOR
  const DWORD cornerDefault = 0;              // DWMWCP_DEFAULT
  const COLORREF colourDefault = 0xFFFFFFFF;  // DWMWA_COLOR_DEFAULT
  set(hwnd, kCornerPreference, &cornerDefault, sizeof(cornerDefault));
  set(hwnd, kBorderColour, &colourDefault, sizeof(colourDefault));
}

void refreshWindowFrame(void* hwndPointer) {
  if (HWND hwnd = static_cast<HWND>(hwndPointer)) {
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
}

void markFullscreenWindow(void* hwndPointer, bool on) {
  HWND hwnd = static_cast<HWND>(hwndPointer);
  if (!hwnd) {
    return;
  }
  // COM for this thread, once; SDL has usually done it already (S_FALSE),
  // and a thread already in the multithreaded apartment still has COM.
  static const bool comReady = [] {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
  }();
  if (!comReady) {
    return;
  }
  // CLSID_TaskbarList, spelled out so nothing has to link uuid.lib for it.
  static const CLSID kTaskbarList = {0x56FDF344, 0xFD6D, 0x11d0, {0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90}};
  ITaskbarList2* taskbar = nullptr;
  if (SUCCEEDED(CoCreateInstance(kTaskbarList, nullptr, CLSCTX_INPROC_SERVER, __uuidof(ITaskbarList2),
                                 reinterpret_cast<void**>(&taskbar))) &&
      taskbar) {
    if (SUCCEEDED(taskbar->HrInit())) {
      taskbar->MarkFullscreenWindow(hwnd, on ? TRUE : FALSE);
    }
    taskbar->Release();
  }
}

bool setDeviceFrameLatency(void* d3d11Device, unsigned frames) {
  if (!d3d11Device) {
    return false;
  }
  IDXGIDevice1* dxgiDevice = nullptr;
  if (FAILED(static_cast<ID3D11Device*>(d3d11Device)->QueryInterface(
        __uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice))) || !dxgiDevice) {
    return false;
  }
  const bool ok = SUCCEEDED(dxgiDevice->SetMaximumFrameLatency(frames));
  dxgiDevice->Release();
  return ok;
}

#else

bool windowCoveredOnItsScreen(void*) {
  return false;
}

void raiseOutputWindow(void*) {}

void resetFullscreenDwmAttributes(void*) {}

void refreshWindowFrame(void*) {}

void markFullscreenWindow(void*, bool) {}

bool setDeviceFrameLatency(void*, unsigned) {
  return false;
}

CompositorTiming queryCompositorTiming() {
  return {};
}

double compositorRefreshHz() {
  return 0.0;
}

PresentPathInfo queryPresentPath(void*, void*, void*, bool) {
  return {};
}

PresentStats queryPresentStats(void*) {
  return {};
}

#endif

}  // namespace deckboy::platform
