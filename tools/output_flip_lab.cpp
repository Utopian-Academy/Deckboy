// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// output_flip_lab -- which way of putting a full-screen output on a second
// screen gets its frames shown on THAT screen's clock? (Windows only)
//
// WHY THIS EXISTS. On 2026-11-01 Deckboy's output, full screen on a 59.94 Hz
// screen beside a 60 Hz desk, presented at the compositor's 60.001 Hz: Windows
// composed it rather than flipping it straight to its screen, so the screen
// dropped or doubled a frame every 15-17 s. The present trace found Windows'
// taskbars above it. Two quick fixes failed on the real screens: raising the
// output once (the taskbar came back over it) and an explicit 59.94 Hz mode
// (SDL's "exclusive" full screen is a display-mode change on the same window;
// no SDL renderer ever calls IDXGISwapChain::SetFullscreenState).
//
// This puts a window exactly where Deckboy puts its output -- a borderless
// popup over the whole screen, a Direct3D 11 flip chain built the way SDL 3.4
// builds it (two buffers, FLIP_SEQUENTIAL, ALLOW_TEARING, one frame in
// flight), presenting with vsync -- and a small "desk" window on another screen
// that holds the focus, as the operator's desk does. Then it tries each way out
// in turn and measures, for each, the rate the presents kept between hitches,
// against the screen's own vblank (timed with IDXGIOutput::WaitForVBlank) and
// DWM's clock:
//
//   plain        what Deckboy does now
//   topmost      HWND_TOPMOST once (what failed: kept as the control)
//   held         HWND_TOPMOST, put back on top whenever a window is over it
//   marked       ITaskbarList2::MarkFullscreenWindow, so the shell treats it
//                as full screen
//   marked-held  both
//   exclusive    IDXGISwapChain::SetFullscreenState(TRUE) on that screen
//
// Run it on the machine with the screens, with nothing else on the output
// screen. It takes that screen over for about two minutes and makes no sound:
//
//   deckboy-output-flip-lab.exe [--monitor N] [--seconds S] [--only a,b,...]
//
// N is the screen as Windows numbers them (Settings > Display), default the
// first that is not the main one. The verdict per variant is "its screen's
// clock" (independent flip or exclusive: the fix works) or "the compositor's
// clock" (still composed).

#ifndef _WIN32
int main() { return 0; }
#else

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_5.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace {

template <typename T>
void release(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

std::string narrow(const wchar_t* text) {
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 1) return {};
  std::string out(static_cast<std::size_t>(bytes - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
  return out;
}

double qpcSeconds() {
  static LARGE_INTEGER frequency = [] {
    LARGE_INTEGER f {};
    QueryPerformanceFrequency(&f);
    return f;
  }();
  LARGE_INTEGER now {};
  QueryPerformanceCounter(&now);
  return static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
}

void pumpMessages() {
  MSG msg {};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_ERASEBKGND) return 1;   // the swap chain draws every pixel
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

// ── the screens ─────────────────────────────────────────────────────────────

struct Screen {
  HMONITOR monitor = nullptr;
  RECT rect {};
  bool primary = false;
  std::wstring device;   // \\.\DISPLAYn
};

BOOL CALLBACK collectScreen(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
  MONITORINFOEXW info {};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor, &info)) {
    Screen screen;
    screen.monitor = monitor;
    screen.rect = info.rcMonitor;
    screen.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    screen.device = info.szDevice;
    reinterpret_cast<std::vector<Screen>*>(param)->push_back(screen);
  }
  return TRUE;
}

// The number Windows shows for it: \\.\DISPLAY3 is screen 3.
int displayNumber(const Screen& screen) {
  const std::wstring& d = screen.device;
  std::size_t i = d.size();
  while (i > 0 && iswdigit(d[i - 1])) --i;
  return i < d.size() ? _wtoi(d.c_str() + i) : 0;
}

// The refresh the screen is set to, exactly (60000/1001, not 59).
double configuredHz(const Screen& screen) {
  UINT32 paths = 0, modes = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths, &modes) != ERROR_SUCCESS) return 0.0;
  std::vector<DISPLAYCONFIG_PATH_INFO> pathInfo(paths);
  std::vector<DISPLAYCONFIG_MODE_INFO> modeInfo(modes);
  if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths, pathInfo.data(), &modes, modeInfo.data(), nullptr) !=
      ERROR_SUCCESS) {
    return 0.0;
  }
  for (UINT32 p = 0; p < paths; ++p) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source {};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = pathInfo[p].sourceInfo.adapterId;
    source.header.id = pathInfo[p].sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS && screen.device == source.viewGdiDeviceName) {
      const DISPLAYCONFIG_RATIONAL& r = pathInfo[p].targetInfo.refreshRate;
      return r.Denominator ? static_cast<double>(r.Numerator) / r.Denominator : 0.0;
    }
  }
  return 0.0;
}

double compositorHz() {
  DWM_TIMING_INFO info {};
  info.cbSize = sizeof(info);
  if (FAILED(DwmGetCompositionTimingInfo(nullptr, &info)) || info.rateRefresh.uiDenominator == 0) return 0.0;
  return static_cast<double>(info.rateRefresh.uiNumerator) / info.rateRefresh.uiDenominator;
}

// ── who is over the output ──────────────────────────────────────────────────

struct Above {
  HWND self = nullptr;
  RECT screen {};
  bool reachedSelf = false;
  int count = 0;
  std::string names;
};

BOOL CALLBACK aboveProc(HWND hwnd, LPARAM param) {
  auto* above = reinterpret_cast<Above*>(param);
  if (hwnd == above->self) {
    above->reachedSelf = true;
    return FALSE;
  }
  if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
  DWORD cloaked = 0;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
  RECT bounds {};
  if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))) {
    GetWindowRect(hwnd, &bounds);
  }
  RECT overlap {};
  if (!IntersectRect(&overlap, &bounds, &above->screen)) return TRUE;
  ++above->count;
  if (above->names.size() < 200) {
    wchar_t title[64] = L"";
    wchar_t cls[64] = L"";
    GetWindowTextW(hwnd, title, 64);
    GetClassNameW(hwnd, cls, 64);
    if (!above->names.empty()) above->names += " | ";
    above->names += title[0] ? narrow(title) : "[" + narrow(cls) + "]";
  }
  return TRUE;
}

Above windowsAbove(HWND self, const RECT& screen) {
  Above above;
  above.self = self;
  above.screen = screen;
  EnumWindows(aboveProc, reinterpret_cast<LPARAM>(&above));
  if (!above.reachedSelf) above.count = -1;
  return above;
}

// ── the output: a window and a swap chain the way Deckboy has them ──────────

struct Output {
  HWND hwnd = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  IDXGISwapChain1* chain = nullptr;
  IDXGIOutput* output = nullptr;   // the screen, on the adapter that drives it
  UINT chainFlags = 0;
  int width = 0, height = 0;
  std::string adapterName;
};

// The adapter whose outputs include `monitor`, so the frame never has to cross
// GPUs (a frame that does is always composed, which would spoil the test).
bool findScreenOutput(HMONITOR monitor, IDXGIFactory2* factory, IDXGIAdapter1** adapterOut,
                      IDXGIOutput** outputOut) {
  for (UINT a = 0;; ++a) {
    IDXGIAdapter1* adapter = nullptr;
    if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND || !adapter) return false;
    for (UINT o = 0;; ++o) {
      IDXGIOutput* output = nullptr;
      if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND || !output) break;
      DXGI_OUTPUT_DESC desc {};
      if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) {
        *adapterOut = adapter;
        *outputOut = output;
        return true;
      }
      output->Release();
    }
    adapter->Release();
  }
}

bool createOutput(const Screen& screen, Output& out) {
  WNDCLASSW wc {};
  wc.lpfnWndProc = windowProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"DeckboyFlipLabOutput";
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  RegisterClassW(&wc);
  out.width = screen.rect.right - screen.rect.left;
  out.height = screen.rect.bottom - screen.rect.top;
  // SDL 3.4's full-screen style: WS_POPUP | WS_MINIMIZEBOX, not topmost.
  out.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Deckboy flip lab - output", WS_POPUP | WS_MINIMIZEBOX,
                             screen.rect.left, screen.rect.top, out.width, out.height, nullptr, nullptr,
                             wc.hInstance, nullptr);
  if (!out.hwnd) return false;

  IDXGIFactory2* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)))) return false;
  IDXGIAdapter1* adapter = nullptr;
  if (!findScreenOutput(screen.monitor, factory, &adapter, &out.output)) {
    std::printf("no DXGI output for that screen\n");
    release(factory);
    return false;
  }
  DXGI_ADAPTER_DESC1 adapterDesc {};
  adapter->GetDesc1(&adapterDesc);
  out.adapterName = narrow(adapterDesc.Description);
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 levels, 3, D3D11_SDK_VERSION, &out.device, nullptr, &out.context);
  release(adapter);
  if (FAILED(hr)) {
    release(factory);
    return false;
  }
  // As SDL: one frame in flight.
  IDXGIDevice1* dxgiDevice = nullptr;
  if (SUCCEEDED(out.device->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice)))) {
    dxgiDevice->SetMaximumFrameLatency(1);
    release(dxgiDevice);
  }
  // As SDL: ALLOW_TEARING on the chain where the system has it.
  IDXGIFactory5* factory5 = nullptr;
  if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&factory5)))) {
    BOOL tearing = FALSE;
    if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) &&
        tearing) {
      out.chainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    release(factory5);
  }
  DXGI_SWAP_CHAIN_DESC1 desc {};
  desc.Width = static_cast<UINT>(out.width);
  desc.Height = static_cast<UINT>(out.height);
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_NONE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.Flags = out.chainFlags;
  hr = factory->CreateSwapChainForHwnd(out.device, out.hwnd, &desc, nullptr, nullptr, &out.chain);
  if (SUCCEEDED(hr)) factory->MakeWindowAssociation(out.hwnd, DXGI_MWA_NO_WINDOW_CHANGES);
  release(factory);
  if (FAILED(hr)) {
    std::printf("CreateSwapChainForHwnd failed 0x%08lx\n", static_cast<unsigned long>(hr));
    return false;
  }
  ShowWindow(out.hwnd, SW_SHOWNOACTIVATE);
  return true;
}

void destroyOutput(Output& out) {
  if (out.chain) {
    BOOL fullscreen = FALSE;
    if (SUCCEEDED(out.chain->GetFullscreenState(&fullscreen, nullptr)) && fullscreen) {
      out.chain->SetFullscreenState(FALSE, nullptr);
    }
  }
  release(out.chain);
  release(out.output);
  release(out.context);
  release(out.device);
  if (out.hwnd) DestroyWindow(out.hwnd);
  out.hwnd = nullptr;
}

// One frame: a grey that steps every frame, so every present is new content.
HRESULT drawAndPresent(Output& out, unsigned frame) {
  ID3D11Texture2D* buffer = nullptr;
  if (FAILED(out.chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) {
    return E_FAIL;
  }
  ID3D11RenderTargetView* view = nullptr;
  out.device->CreateRenderTargetView(buffer, nullptr, &view);
  release(buffer);
  if (!view) return E_FAIL;
  const float level = 0.15f + 0.1f * static_cast<float>(frame % 8) / 8.0f;
  const float colour[4] = {level, level, level, 1.0f};
  out.context->ClearRenderTargetView(view, colour);
  release(view);
  return out.chain->Present(1, 0);
}

// The screen's own vblank, timed: the clock a flipped output would keep.
double measuredScreenHz(Output& out) {
  out.output->WaitForVBlank();
  const double start = qpcSeconds();
  constexpr int kVblanks = 120;
  for (int i = 0; i < kVblanks; ++i) out.output->WaitForVBlank();
  const double elapsed = qpcSeconds() - start;
  return elapsed > 0.0 ? kVblanks / elapsed : 0.0;
}

// ── the desk: holds the focus on another screen, as the operator's does ────

HWND createDesk(const Screen& screen) {
  WNDCLASSW wc {};
  wc.lpfnWndProc = windowProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"DeckboyFlipLabDesk";
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH));
  RegisterClassW(&wc);
  const int w = 480, h = 200;
  const int x = screen.rect.left + (screen.rect.right - screen.rect.left - w) / 2;
  const int y = screen.rect.top + (screen.rect.bottom - screen.rect.top - h) / 2;
  HWND desk = CreateWindowExW(0, wc.lpszClassName, L"Deckboy flip lab - desk (holds the focus)",
                              WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, wc.hInstance, nullptr);
  ShowWindow(desk, SW_SHOW);
  return desk;
}

void focusDesk(HWND desk) {
  SetForegroundWindow(desk);
  SetActiveWindow(desk);
  pumpMessages();
}

// ── the variants ────────────────────────────────────────────────────────────

struct Result {
  std::string name;
  double pacedHz = 0.0;
  int hitches = 0;
  int above = -1;
  std::string aboveNames;
  int reasserts = 0;
  std::string mode = "n/a";
  std::string note;
};

void raiseTopmost(HWND hwnd) {
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void markFullscreen(HWND hwnd, BOOL on) {
  ITaskbarList2* taskbar = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, __uuidof(ITaskbarList2),
                                 reinterpret_cast<void**>(&taskbar))) &&
      taskbar) {
    if (SUCCEEDED(taskbar->HrInit())) taskbar->MarkFullscreenWindow(hwnd, on);
    taskbar->Release();
  }
}

const char* compositionModeName(Output& out) {
  IDXGISwapChainMedia* media = nullptr;
  const char* name = "n/a";
  if (SUCCEEDED(out.chain->QueryInterface(__uuidof(IDXGISwapChainMedia), reinterpret_cast<void**>(&media))) &&
      media) {
    DXGI_FRAME_STATISTICS_MEDIA stats {};
    if (SUCCEEDED(media->GetFrameStatisticsMedia(&stats))) {
      switch (stats.CompositionMode) {
        case DXGI_FRAME_PRESENTATION_MODE_COMPOSED: name = "composed"; break;
        case DXGI_FRAME_PRESENTATION_MODE_OVERLAY: name = "overlay"; break;
        case DXGI_FRAME_PRESENTATION_MODE_NONE: name = "none"; break;
        default: name = "failure"; break;
      }
    }
    media->Release();
  }
  return name;
}

Result runVariant(const std::string& name, const Screen& screen, HWND desk, double seconds, double screenHz) {
  Result result;
  result.name = name;
  Output out;
  if (!createOutput(screen, out)) {
    result.note = "could not create the output";
    destroyOutput(out);
    return result;
  }
  const bool topmost = name == "topmost" || name == "held" || name == "marked-held";
  const bool hold = name == "held" || name == "marked-held";
  const bool marked = name == "marked" || name == "marked-held";
  if (topmost) raiseTopmost(out.hwnd);
  if (marked) markFullscreen(out.hwnd, TRUE);
  if (name == "exclusive") {
    // Exclusive needs the focus to enter; the desk takes it back after, as an
    // operator clicking the desk would.
    SetForegroundWindow(out.hwnd);
    pumpMessages();
    const HRESULT hr = out.chain->SetFullscreenState(TRUE, out.output);
    if (SUCCEEDED(hr)) {
      out.chain->ResizeBuffers(0, static_cast<UINT>(out.width), static_cast<UINT>(out.height), DXGI_FORMAT_UNKNOWN,
                               out.chainFlags);
    } else {
      char text[64];
      std::snprintf(text, sizeof(text), "SetFullscreenState failed 0x%08lx", static_cast<unsigned long>(hr));
      result.note = text;
    }
  }
  focusDesk(desk);

  // Settle (Windows takes a few frames to pick a path), then measure.
  unsigned frame = 0;
  const double settleUntil = qpcSeconds() + 2.0;
  while (qpcSeconds() < settleUntil) {
    drawAndPresent(out, frame++);
    pumpMessages();
  }
  const double framePeriod = screenHz > 1.0 ? 1.0 / screenHz : 1.0 / 60.0;
  double last = qpcSeconds(), regularSum = 0.0, nextCheck = last + 0.25;
  int regularCount = 0, occluded = 0;
  const double end = last + seconds;
  while (last < end) {
    const HRESULT hr = drawAndPresent(out, frame++);
    if (hr == DXGI_STATUS_OCCLUDED) ++occluded;
    const double now = qpcSeconds();
    const double gap = now - last;
    last = now;
    if (gap > framePeriod * 1.5) {
      ++result.hitches;
    } else {
      regularSum += gap;
      ++regularCount;
    }
    pumpMessages();
    if (hold && now >= nextCheck) {
      nextCheck = now + 0.25;
      if (windowsAbove(out.hwnd, screen.rect).count > 0) {
        raiseTopmost(out.hwnd);
        ++result.reasserts;
      }
    }
  }
  result.pacedHz = regularSum > 0.0 ? regularCount / regularSum : 0.0;
  const Above above = windowsAbove(out.hwnd, screen.rect);
  result.above = above.count;
  result.aboveNames = above.names;
  result.mode = compositionModeName(out);
  if (name == "exclusive") {
    BOOL fullscreen = FALSE;
    out.chain->GetFullscreenState(&fullscreen, nullptr);
    char text[96];
    std::snprintf(text, sizeof(text), "%sstill exclusive at the end: %s, occluded presents %d",
                  result.note.empty() ? "" : (result.note + "; ").c_str(), fullscreen ? "yes" : "no", occluded);
    result.note = text;
  }
  if (marked) markFullscreen(out.hwnd, FALSE);
  destroyOutput(out);
  pumpMessages();
  return result;
}

bool sameClock(double a, double b) {
  return a > 0.0 && b > 0.0 && std::abs(a - b) < std::max(a, b) * 0.0003;
}

}  // namespace

int main(int argc, char** argv) {
  int wanted = 0;
  double seconds = 12.0;
  std::vector<std::string> variants = {"plain", "topmost", "held", "marked", "marked-held", "exclusive"};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--monitor" && i + 1 < argc) {
      wanted = std::atoi(argv[++i]);
    } else if (arg == "--seconds" && i + 1 < argc) {
      seconds = std::max(3.0, std::atof(argv[++i]));
    } else if (arg == "--only" && i + 1 < argc) {
      variants.clear();
      std::string list = argv[++i];
      std::size_t start = 0;
      while (start <= list.size()) {
        const std::size_t comma = list.find(',', start);
        variants.push_back(list.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
    } else {
      std::printf("usage: deckboy-output-flip-lab [--monitor N] [--seconds S] [--only plain,held,...]\n");
      return 2;
    }
  }
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  std::vector<Screen> screens;
  EnumDisplayMonitors(nullptr, nullptr, collectScreen, reinterpret_cast<LPARAM>(&screens));
  const Screen* target = nullptr;
  const Screen* deskScreen = nullptr;
  for (const Screen& s : screens) {
    if (wanted ? displayNumber(s) == wanted : !s.primary) {
      if (!target) target = &s;
    }
  }
  for (const Screen& s : screens) {
    if (&s != target && (!deskScreen || s.primary)) deskScreen = &s;
  }
  if (!target || !deskScreen) {
    std::printf("needs two screens: the output's (--monitor N) and another for the desk\n");
    return 2;
  }
  const double setHz = configuredHz(*target);
  const double deskHz = configuredHz(*deskScreen);
  const double dwmHz = compositorHz();
  std::printf("output screen: %d, %ldx%ld, set to %.3f Hz; desk screen: %d at %.3f Hz; compositor %.3f Hz\n",
              displayNumber(*target), target->rect.right - target->rect.left, target->rect.bottom - target->rect.top,
              setHz, displayNumber(*deskScreen), deskHz, dwmHz);

  HWND desk = createDesk(*deskScreen);
  double screenHz = setHz;
  {
    Output probe;
    if (createOutput(*target, probe)) {
      screenHz = measuredScreenHz(probe);
      std::printf("output screen's vblank, timed: %.3f Hz (drawn on %s)\n", screenHz, probe.adapterName.c_str());
    }
    destroyOutput(probe);
  }
  if (sameClock(screenHz, dwmHz)) {
    std::printf("note: this screen and the compositor keep the same clock, so the test cannot tell them apart.\n"
                "      Run it with the output's screen at a different rate from the desk's (59.94 vs 60).\n");
  }

  std::vector<Result> results;
  for (const std::string& name : variants) {
    std::printf("running %-12s ...\n", name.c_str());
    std::fflush(stdout);
    results.push_back(runVariant(name, *target, desk, seconds, screenHz));
  }
  DestroyWindow(desk);
  CoUninitialize();

  std::printf("\n%-12s %10s %8s %10s %6s %9s  %s\n", "variant", "paced Hz", "hitches", "dxgi mode", "above",
              "reraised", "verdict");
  for (const Result& r : results) {
    const char* verdict = r.pacedHz <= 0.0                 ? "no data"
                          : sameClock(r.pacedHz, screenHz) ? "ITS SCREEN'S CLOCK"
                          : sameClock(r.pacedHz, dwmHz)    ? "the compositor's clock"
                                                           : "another clock";
    std::printf("%-12s %10.3f %8d %10s %6d %9d  %s\n", r.name.c_str(), r.pacedHz, r.hitches, r.mode.c_str(),
                r.above, r.reasserts, verdict);
    if (!r.aboveNames.empty()) std::printf("%14s over it: %s\n", "", r.aboveNames.c_str());
    if (!r.note.empty()) std::printf("%14s %s\n", "", r.note.c_str());
  }
  return 0;
}

#endif
