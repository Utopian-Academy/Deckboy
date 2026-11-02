// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// output_flip_lab -- what keeps a full-screen output on a second screen from
// being shown on THAT screen's clock? (Windows only)
//
// WHY THIS EXISTS. On 2026-11-01 Deckboy's output, full screen on a 59.94 Hz
// screen beside a 60 Hz desk, presented at the compositor's 60.001 Hz:
// Windows composed it instead of flipping it straight to its screen, and the
// screen dropped or doubled a frame every 15-17 s.
//
// MEASURED 2026-11-02 on that PC, first version of this lab: a bare popup
// with a flip chain built as SDL builds it kept the SCREEN's clock (59.95)
// with both taskbars above it in z-order, while the same window made
// topmost kept the compositor's (60.04-60.08). Deckboy's own output, kept
// topmost with nothing above it, covering its screen, on the GPU that drives
// that screen, still kept 60.001. So the taskbar is not what composes it, and
// something Deckboy's window has that the bare one does not is. The variants
// below add those things one at a time:
//
//   plain        a bare popup over the screen, chain as SDL 3.4 makes it
//                (two buffers, FLIP_SEQUENTIAL, ALLOW_TEARING, one frame in
//                flight), presenting with vsync. The control.
//   topmost      plain, made HWND_TOPMOST
//   sdl-attrs    what SDL sets on Windows 11 when a window goes full screen:
//                DWMWA_WINDOW_CORNER_PREFERENCE = DONOTROUND and
//                DWMWA_BORDER_COLOR = DWMWA_COLOR_NONE
//   owndc        SDL's window class style, CS_OWNDC | CS_BYTEALIGNCLIENT
//   colorspace   IDXGISwapChain3::SetColorSpace1(G22_NONE_P709), as SDL does
//   moved        window and chain made small on the DESK's screen, then moved
//                and resized onto the output's -- as Deckboy creates outputs
//   desk-chain   the desk window gets a flip chain of its own on adapter 0,
//                presenting every pass with sync 0 + ALLOW_TEARING, as
//                Deckboy's desk does with vsync off
//   deckboy      all five of the above together (not topmost)
//
// The variants above imitate SDL. These USE it (built with SDL, the default),
// making the output the way Deckboy does: SDL_CreateWindow hidden and
// resizable on the desk's screen, SDL's Direct3D 11 renderer created at once,
// then moved onto the output's screen and made borderless full screen, vsync
// on, drawn and presented through SDL_RenderPresent. Each but the first adds
// one candidate fix, so a run that finds the fault also tries the cure:
//
//   sdl          the output exactly as Deckboy makes it
//   sdl-dwm      sdl, then the window's DWM corner and border attributes put
//                back to their defaults after SDL set them for full screen
//   sdl-frame    sdl, then SetWindowPos(SWP_FRAMECHANGED): SDL swaps the
//                captioned style for WS_POPUP without asking Windows to
//                recompute the frame
//   sdl-late     the renderer (and so the swap chain) made only once the
//                window is full screen on the output's screen
//   sdl-fixed    all three fixes together
//
// and three crosses, so that one run says which half is at fault when no fix
// above is enough:
//
//   sdl-ownchain       SDL's window, made as Deckboy makes it, with no SDL
//                      renderer: the lab's own chain on it instead
//   plain-sdlrenderer  the lab's bare popup, wrapped by SDL, with SDL's
//                      renderer (its device and chain) on it
//   sdl-full           sdl, beside an SDL desk that has the focus and presents
//                      every pass with vsync off and two frames in flight, and
//                      two hidden per-deck renderers -- what Deckboy runs
//
// and, on request (--only), the earlier ones: held (topmost, put back on top
// whenever a window is over it), marked (ITaskbarList2::MarkFullscreenWindow),
// marked-held, exclusive (IDXGISwapChain::SetFullscreenState).
//
// THE PATH Windows took is printed beside the clock: the swap chain's
// presentation mode (IDXGISwapChainMedia), most frequent first -- "overlay"
// is a plane of its own, flipped on the screen's clock; "composed" is DWM.
//
// THE CLOCK each variant's frames were shown on comes from the swap chain's
// own frame statistics: (SyncRefreshCount, SyncQPCTime) pairs, fitted over
// their longest steady run. That is exact. The rate of the presents is shown
// too, as a check, with the interval after each hitch left out (a present that
// follows a stall returns at once, and counting it read 60.05 for a 60.00
// clock in the first version).
//
// Run it on the machine with the screens, with nothing else on the output
// screen. It takes that screen over for about four minutes, silently:
//
//   deckboy-output-flip-lab.exe [--monitor N] [--seconds S] [--only a,b,...]
//
// N is the number in the screen's \\.\DISPLAYn name; leave it out and it
// takes the first screen that is not the main one.

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
#include <dxgi1_3.h>   // IDXGISwapChainMedia
#include <dxgi1_5.h>
#include <shobjidl.h>

#if DECKBOY_FLIP_LAB_SDL
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>
#include <utility>
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

double qpcFrequency() {
  static const double frequency = [] {
    LARGE_INTEGER f {};
    QueryPerformanceFrequency(&f);
    return static_cast<double>(f.QuadPart);
  }();
  return frequency;
}

double qpcSeconds() {
  LARGE_INTEGER now {};
  QueryPerformanceCounter(&now);
  return static_cast<double>(now.QuadPart) / qpcFrequency();
}

void pumpMessages() {
  MSG msg {};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
#if DECKBOY_FLIP_LAB_SDL
  // SDL's window procedure queues an event for what was just dispatched;
  // nothing here reads them.
  if (SDL_WasInit(SDL_INIT_VIDEO)) SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
#endif
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

// The number in its \\.\DISPLAYn name.
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

// ── a variant: what the output window and its process have ─────────────────

struct Variant {
  std::string name;
  bool topmost = false;
  bool hold = false;
  bool marked = false;
  bool exclusive = false;
  bool sdlAttrs = false;
  bool ownDc = false;
  bool colorSpace = false;
  bool moved = false;
  bool deskChain = false;
  // Through SDL itself, as Deckboy makes its output, and the fixes to try.
  bool sdl = false;
  bool sdlDwmDefault = false;
  bool sdlFrameChanged = false;
  bool sdlLate = false;
  // The crosses that say which half is at fault, and Deckboy's other windows.
  bool sdlOwnChain = false;        // SDL's window, the lab's own chain on it
  bool plainSdlRenderer = false;   // the lab's window, SDL's renderer on it
  bool sdlFull = false;            // sdl, plus an SDL desk and hidden deck renderers
};

bool makeVariant(const std::string& name, Variant& v) {
  v = Variant {};
  v.name = name;
  if (name == "plain") return true;
  if (name == "topmost") { v.topmost = true; return true; }
  if (name == "held") { v.topmost = v.hold = true; return true; }
  if (name == "marked") { v.marked = true; return true; }
  if (name == "marked-held") { v.marked = v.topmost = v.hold = true; return true; }
  if (name == "exclusive") { v.exclusive = true; return true; }
  if (name == "sdl-attrs") { v.sdlAttrs = true; return true; }
  if (name == "owndc") { v.ownDc = true; return true; }
  if (name == "colorspace") { v.colorSpace = true; return true; }
  if (name == "moved") { v.moved = true; return true; }
  if (name == "desk-chain") { v.deskChain = true; return true; }
  if (name == "deckboy") {
    v.sdlAttrs = v.ownDc = v.colorSpace = v.moved = v.deskChain = true;
    return true;
  }
#if DECKBOY_FLIP_LAB_SDL
  if (name == "sdl") { v.sdl = true; return true; }
  if (name == "sdl-dwm") { v.sdl = v.sdlDwmDefault = true; return true; }
  if (name == "sdl-frame") { v.sdl = v.sdlFrameChanged = true; return true; }
  if (name == "sdl-late") { v.sdl = v.sdlLate = true; return true; }
  if (name == "sdl-fixed") { v.sdl = v.sdlDwmDefault = v.sdlFrameChanged = v.sdlLate = true; return true; }
  if (name == "sdl-ownchain") { v.sdl = v.sdlOwnChain = true; return true; }
  if (name == "plain-sdlrenderer") { v.sdl = v.plainSdlRenderer = true; return true; }
  if (name == "sdl-full") { v.sdl = v.sdlFull = true; return true; }
#endif
  return false;
}

// ── a flip chain on a window, the way SDL 3.4's Direct3D 11 renderer makes it

struct Chain {
  HWND hwnd = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  IDXGISwapChain1* chain = nullptr;
  IDXGIOutput* output = nullptr;   // the output's screen, on the adapter that drives it
  UINT chainFlags = 0;
  int width = 0, height = 0;
  std::string adapterName;
};

// The adapter whose outputs include `monitor` (or adapter 0 when `monitor` is
// null, as SDL picks), so the output's frame never has to cross GPUs.
bool pickAdapter(HMONITOR monitor, IDXGIFactory2* factory, IDXGIAdapter1** adapterOut, IDXGIOutput** outputOut) {
  if (!monitor) {
    return factory->EnumAdapters1(0, adapterOut) != DXGI_ERROR_NOT_FOUND && *adapterOut;
  }
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

// `monitor` null: adapter 0 and no output (the desk's chain).
bool createChain(Chain& c, HMONITOR monitor, UINT latency, bool colorSpace) {
  IDXGIFactory2* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)))) return false;
  IDXGIAdapter1* adapter = nullptr;
  if (!pickAdapter(monitor, factory, &adapter, &c.output)) {
    release(factory);
    return false;
  }
  DXGI_ADAPTER_DESC1 adapterDesc {};
  adapter->GetDesc1(&adapterDesc);
  c.adapterName = narrow(adapterDesc.Description);
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 levels, 3, D3D11_SDK_VERSION, &c.device, nullptr, &c.context);
  release(adapter);
  if (FAILED(hr)) {
    release(factory);
    return false;
  }
  IDXGIDevice1* dxgiDevice = nullptr;
  if (SUCCEEDED(c.device->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice)))) {
    dxgiDevice->SetMaximumFrameLatency(latency);
    release(dxgiDevice);
  }
  IDXGIFactory5* factory5 = nullptr;
  if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&factory5)))) {
    BOOL tearing = FALSE;
    if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) &&
        tearing) {
      c.chainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    release(factory5);
  }
  DXGI_SWAP_CHAIN_DESC1 desc {};
  desc.Width = static_cast<UINT>(c.width);
  desc.Height = static_cast<UINT>(c.height);
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_NONE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.Flags = c.chainFlags;
  hr = factory->CreateSwapChainForHwnd(c.device, c.hwnd, &desc, nullptr, nullptr, &c.chain);
  if (SUCCEEDED(hr)) factory->MakeWindowAssociation(c.hwnd, DXGI_MWA_NO_WINDOW_CHANGES);
  release(factory);
  if (FAILED(hr)) {
    std::printf("CreateSwapChainForHwnd failed 0x%08lx\n", static_cast<unsigned long>(hr));
    return false;
  }
  if (colorSpace) {
    IDXGISwapChain3* chain3 = nullptr;
    if (SUCCEEDED(c.chain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&chain3))) && chain3) {
      chain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
      chain3->Release();
    }
  }
  return true;
}

void destroyChain(Chain& c) {
  if (c.chain) {
    BOOL fullscreen = FALSE;
    if (SUCCEEDED(c.chain->GetFullscreenState(&fullscreen, nullptr)) && fullscreen) {
      c.chain->SetFullscreenState(FALSE, nullptr);
    }
  }
  release(c.chain);
  release(c.output);
  release(c.context);
  release(c.device);
}

// One frame: a grey that steps every frame, so every present is new content.
HRESULT drawAndPresent(Chain& c, unsigned frame, UINT syncInterval, UINT flags) {
  ID3D11Texture2D* buffer = nullptr;
  if (FAILED(c.chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) {
    return E_FAIL;
  }
  ID3D11RenderTargetView* view = nullptr;
  c.device->CreateRenderTargetView(buffer, nullptr, &view);
  release(buffer);
  if (!view) return E_FAIL;
  const float level = 0.15f + 0.1f * static_cast<float>(frame % 8) / 8.0f;
  const float colour[4] = {level, level, level, 1.0f};
  c.context->ClearRenderTargetView(view, colour);
  release(view);
  return c.chain->Present(syncInterval, flags);
}

// ── the output and the desk ─────────────────────────────────────────────────

const wchar_t* outputClass(bool ownDc) {
  static bool registered[2] = {false, false};
  const wchar_t* name = ownDc ? L"DeckboyFlipLabOutputOwnDC" : L"DeckboyFlipLabOutput";
  if (!registered[ownDc ? 1 : 0]) {
    WNDCLASSW wc {};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = name;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    if (ownDc) wc.style = CS_OWNDC | CS_BYTEALIGNCLIENT;   // SDL's class style
    RegisterClassW(&wc);
    registered[ownDc ? 1 : 0] = true;
  }
  return name;
}

bool createOutput(const Variant& v, const Screen& screen, const Screen& deskScreen, Chain& out) {
  const int fullW = screen.rect.right - screen.rect.left;
  const int fullH = screen.rect.bottom - screen.rect.top;
  // SDL 3.4's full-screen style: WS_POPUP | WS_MINIMIZEBOX. "moved" starts it
  // small on the desk's screen, as Deckboy's hidden output window starts.
  RECT start = screen.rect;
  if (v.moved) {
    start = {deskScreen.rect.left + 40, deskScreen.rect.top + 40, deskScreen.rect.left + 680, deskScreen.rect.top + 400};
  }
  out.width = start.right - start.left;
  out.height = start.bottom - start.top;
  out.hwnd = CreateWindowExW(0, outputClass(v.ownDc), L"Deckboy flip lab - output", WS_POPUP | WS_MINIMIZEBOX,
                             start.left, start.top, out.width, out.height, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
  if (!out.hwnd) return false;
  if (!createChain(out, screen.monitor, 1, v.colorSpace)) return false;
  if (v.moved) {
    out.width = fullW;
    out.height = fullH;
    SetWindowPos(out.hwnd, HWND_NOTOPMOST, screen.rect.left, screen.rect.top, fullW, fullH, SWP_NOACTIVATE);
    out.chain->ResizeBuffers(0, static_cast<UINT>(fullW), static_cast<UINT>(fullH), DXGI_FORMAT_UNKNOWN,
                             out.chainFlags);
  }
  if (v.sdlAttrs) {
    // What SDL's WIN_SetWindowFullscreen sets on Windows 11 (numbers, so this
    // builds against SDKs older than 22000).
    constexpr DWORD kCornerPreference = 33;     // DWMWA_WINDOW_CORNER_PREFERENCE
    constexpr DWORD kBorderColour = 34;         // DWMWA_BORDER_COLOR
    const DWORD doNotRound = 1;                 // DWMWCP_DONOTROUND
    const COLORREF colourNone = 0xFFFFFFFE;     // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(out.hwnd, kCornerPreference, &doNotRound, sizeof(doNotRound));
    DwmSetWindowAttribute(out.hwnd, kBorderColour, &colourNone, sizeof(colourNone));
  }
  ShowWindow(out.hwnd, SW_SHOWNOACTIVATE);
  return true;
}

void destroyWindowAndChain(Chain& c) {
  destroyChain(c);
  if (c.hwnd) DestroyWindow(c.hwnd);
  c.hwnd = nullptr;
}

// ── the path Windows took: the swap chain's presentation mode ──────────────

struct Modes {
  int composed = 0, overlay = 0, none = 0, failure = 0, unknown = 0;

  void add(IDXGISwapChain1* chain) {
    IDXGISwapChainMedia* media = nullptr;
    if (!chain ||
        FAILED(chain->QueryInterface(__uuidof(IDXGISwapChainMedia), reinterpret_cast<void**>(&media))) ||
        !media) {
      ++unknown;
      return;
    }
    DXGI_FRAME_STATISTICS_MEDIA stats {};
    if (SUCCEEDED(media->GetFrameStatisticsMedia(&stats))) {
      switch (stats.CompositionMode) {
        case DXGI_FRAME_PRESENTATION_MODE_COMPOSED: ++composed; break;
        case DXGI_FRAME_PRESENTATION_MODE_OVERLAY: ++overlay; break;
        case DXGI_FRAME_PRESENTATION_MODE_NONE: ++none; break;
        case DXGI_FRAME_PRESENTATION_MODE_COMPOSITION_FAILURE: ++failure; break;
        default: ++unknown; break;
      }
    } else {
      ++unknown;
    }
    media->Release();
  }

  // "composed 97%, overlay 3%", most frequent first.
  std::string text() const {
    std::vector<std::pair<int, const char*>> counts = {
      {composed, "composed"}, {overlay, "overlay"}, {none, "none"}, {failure, "composition failure"},
      {unknown, "unknown"}};
    std::sort(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    const int total = composed + overlay + none + failure + unknown;
    std::string out;
    for (const auto& [count, label] : counts) {
      if (count <= 0 || total <= 0) continue;
      char part[64];
      std::snprintf(part, sizeof(part), "%s%s %d%%", out.empty() ? "" : ", ", label, count * 100 / total);
      out += part;
    }
    return out.empty() ? "no data" : out;
  }
};

#if DECKBOY_FLIP_LAB_SDL
// ── the output through SDL, as Deckboy makes it ─────────────────────────────

struct SdlOutput {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  HWND hwnd = nullptr;
  IDXGISwapChain1* chain = nullptr;   // SDL's, or own.chain: not released through this
  std::string adapterName;
  std::string shape;                  // style, rectangles and chain, for the report
  Chain own;                          // sdl-ownchain: the lab's chain on SDL's window
  HWND foreign = nullptr;             // plain-sdlrenderer: the lab's window, SDL wrapping it
  // sdl-full: the rest of what Deckboy runs beside an output.
  SDL_Window* deskWindow = nullptr;
  SDL_Renderer* deskRenderer = nullptr;
  HWND deskHwnd = nullptr;
  std::vector<std::pair<SDL_Window*, SDL_Renderer*>> decks;
};

bool makeSdlRenderer(SdlOutput& out) {
  // Deckboy's order on Windows (deckboyCreateRenderer): Direct3D 11 first.
  out.renderer = SDL_CreateRenderer(out.window, "direct3d11");
  if (!out.renderer) return false;
  SDL_SetRenderVSync(out.renderer, 1);   // as createOutputRuntime sets it
  const SDL_PropertiesID props = SDL_GetRendererProperties(out.renderer);
  out.chain = static_cast<IDXGISwapChain1*>(
    SDL_GetPointerProperty(props, SDL_PROP_RENDERER_D3D11_SWAPCHAIN_POINTER, nullptr));
  auto* device =
    static_cast<ID3D11Device*>(SDL_GetPointerProperty(props, SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr));
  IDXGIDevice* dxgiDevice = nullptr;
  if (device && SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)))) {
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter) {
      DXGI_ADAPTER_DESC desc {};
      adapter->GetDesc(&desc);
      out.adapterName = narrow(desc.Description);
      adapter->Release();
    }
    dxgiDevice->Release();
  }
  return out.chain != nullptr;
}

HWND sdlWindowHandle(SDL_Window* window) {
  return static_cast<HWND>(
    SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

// sdl-full: the desk (focused, presenting every pass with vsync off and two
// frames in flight, as this build of Deckboy's desk does) and two hidden
// per-deck renderers (createDeckRuntime), which never present.
bool createSdlNeighbours(const Screen& deskScreen, SdlOutput& out) {
  const int w = 1280, h = 720;
  out.deskWindow = SDL_CreateWindow("Deckboy flip lab - SDL desk", w, h, SDL_WINDOW_RESIZABLE);
  if (!out.deskWindow) return false;
  SDL_SetWindowPosition(out.deskWindow, deskScreen.rect.left + (deskScreen.rect.right - deskScreen.rect.left - w) / 2,
                        deskScreen.rect.top + (deskScreen.rect.bottom - deskScreen.rect.top - h) / 2);
  out.deskHwnd = sdlWindowHandle(out.deskWindow);
  out.deskRenderer = SDL_CreateRenderer(out.deskWindow, "direct3d11");
  if (!out.deskRenderer) return false;
  SDL_SetRenderVSync(out.deskRenderer, 0);
  auto* device = static_cast<ID3D11Device*>(
    SDL_GetPointerProperty(SDL_GetRendererProperties(out.deskRenderer), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr));
  IDXGIDevice1* dxgiDevice = nullptr;
  if (device && SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice)))) {
    dxgiDevice->SetMaximumFrameLatency(2);   // applyDeskFrameLatency
    dxgiDevice->Release();
  }
  for (int d = 0; d < 2; ++d) {
    SDL_Window* deckWindow = SDL_CreateWindow("Deckboy flip lab - deck runtime", 1920, 1080, SDL_WINDOW_HIDDEN);
    SDL_Renderer* deckRenderer = deckWindow ? SDL_CreateRenderer(deckWindow, "direct3d11") : nullptr;
    out.decks.emplace_back(deckWindow, deckRenderer);
    if (!deckRenderer) return false;
  }
  return true;
}

bool createSdlOutput(const Variant& v, const Screen& screen, const Screen& deskScreen, SdlOutput& out) {
  const int fullW = screen.rect.right - screen.rect.left;
  const int fullH = screen.rect.bottom - screen.rect.top;
  if (v.plainSdlRenderer) {
    // The lab's own bare popup, full size on its screen, with SDL wrapping it
    // and SDL's renderer (and so SDL's device and chain) on it.
    out.foreign = CreateWindowExW(0, outputClass(false), L"Deckboy flip lab - output", WS_POPUP | WS_MINIMIZEBOX,
                                  screen.rect.left, screen.rect.top, fullW, fullH, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    if (!out.foreign) return false;
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, out.foreign);
    out.window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!out.window) return false;
    out.hwnd = out.foreign;
    if (!makeSdlRenderer(out)) return false;
    ShowWindow(out.foreign, SW_SHOWNOACTIVATE);
  } else {
    // createOutputRuntime: hidden and resizable, at the output's raster, on the
    // screen SDL picks for a new window (the main one, the desk's)...
    out.window = SDL_CreateWindow("Deckboy flip lab - SDL output", fullW, fullH,
                                  SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if (!out.window) return false;
    out.hwnd = sdlWindowHandle(out.window);
    // ...with its renderer, and so its swap chain, made there and then...
    const bool rendererFirst = !v.sdlLate && !v.sdlOwnChain;
    if (rendererFirst && !makeSdlRenderer(out)) return false;
    // ...then moved onto the output's screen, shown and made borderless full
    // screen (applyOutputDisplaySelection, then enableOutputFullscreen).
    SDL_SetWindowPosition(out.window, screen.rect.left, screen.rect.top);
    SDL_ShowWindow(out.window);
    SDL_SetWindowFullscreenMode(out.window, nullptr);
    if (!SDL_SetWindowFullscreen(out.window, true)) return false;
    SDL_SyncWindow(out.window);
    if (v.sdlLate && !makeSdlRenderer(out)) return false;
    if (v.sdlOwnChain) {
      // No SDL renderer at all: the lab's chain, on the monitor's own adapter,
      // at full size, made once the window is full screen there.
      out.own.hwnd = out.hwnd;
      out.own.width = fullW;
      out.own.height = fullH;
      if (!createChain(out.own, screen.monitor, 1, false)) return false;
      out.chain = out.own.chain;
      out.adapterName = out.own.adapterName;
    }
  }
  if (v.sdlFull && !createSdlNeighbours(deskScreen, out)) return false;
  if (v.sdlDwmDefault) {
    constexpr DWORD kCornerPreference = 33;     // DWMWA_WINDOW_CORNER_PREFERENCE
    constexpr DWORD kBorderColour = 34;         // DWMWA_BORDER_COLOR
    const DWORD cornerDefault = 0;              // DWMWCP_DEFAULT
    const COLORREF colourDefault = 0xFFFFFFFF;  // DWMWA_COLOR_DEFAULT
    DwmSetWindowAttribute(out.hwnd, kCornerPreference, &cornerDefault, sizeof(cornerDefault));
    DwmSetWindowAttribute(out.hwnd, kBorderColour, &colourDefault, sizeof(colourDefault));
  }
  if (v.sdlFrameChanged) {
    SetWindowPos(out.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  RECT window {}, client {};
  GetWindowRect(out.hwnd, &window);
  GetClientRect(out.hwnd, &client);
  DXGI_SWAP_CHAIN_DESC1 desc {};
  if (out.chain) out.chain->GetDesc1(&desc);
  char text[320];
  std::snprintf(text, sizeof(text),
                "style %08lx ex %08lx; window %ld,%ld %ldx%ld; client %ldx%ld; chain %ux%u scaling %d effect %d "
                "flags %x",
                static_cast<unsigned long>(GetWindowLongW(out.hwnd, GWL_STYLE)),
                static_cast<unsigned long>(GetWindowLongW(out.hwnd, GWL_EXSTYLE)), window.left, window.top,
                window.right - window.left, window.bottom - window.top, client.right, client.bottom, desc.Width,
                desc.Height, static_cast<int>(desc.Scaling), static_cast<int>(desc.SwapEffect), desc.Flags);
  out.shape = text;
  return true;
}

void destroySdlOutput(SdlOutput& out) {
  for (auto& [deckWindow, deckRenderer] : out.decks) {
    if (deckRenderer) SDL_DestroyRenderer(deckRenderer);
    if (deckWindow) SDL_DestroyWindow(deckWindow);
  }
  if (out.deskRenderer) SDL_DestroyRenderer(out.deskRenderer);
  if (out.deskWindow) SDL_DestroyWindow(out.deskWindow);
  destroyChain(out.own);   // before the window it is on
  if (out.renderer) SDL_DestroyRenderer(out.renderer);
  if (out.window) SDL_DestroyWindow(out.window);
  if (out.foreign) DestroyWindow(out.foreign);   // SDL does not destroy a window it only wrapped
  out = SdlOutput {};
}

// The same stepping grey as drawAndPresent, through SDL's renderer (or the
// lab's chain on SDL's window); sdl-full's desk first, as Deckboy's loop.
void presentSdl(SdlOutput& out, unsigned frame) {
  const Uint8 level = static_cast<Uint8>(38 + 3 * (frame % 8));
  if (out.deskRenderer) {
    SDL_SetRenderDrawColor(out.deskRenderer, level, 40, 40, 255);
    SDL_RenderClear(out.deskRenderer);
    SDL_RenderPresent(out.deskRenderer);
  }
  if (out.own.chain) {
    drawAndPresent(out.own, frame, 1, 0);
    return;
  }
  SDL_SetRenderDrawColor(out.renderer, level, level, level, 255);
  SDL_RenderClear(out.renderer);
  SDL_RenderPresent(out.renderer);
}
#endif

double measuredScreenHz(Chain& out) {
  out.output->WaitForVBlank();
  const double start = qpcSeconds();
  constexpr int kVblanks = 120;
  for (int i = 0; i < kVblanks; ++i) out.output->WaitForVBlank();
  const double elapsed = qpcSeconds() - start;
  return elapsed > 0.0 ? kVblanks / elapsed : 0.0;
}

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

// ── the clock the frames were shown on ─────────────────────────────────────

// Hz of the longest steady run of (refresh count, seconds) samples, by least
// squares. A windowed chain reports zeros, then counts that can restart.
double fitClock(std::vector<std::pair<double, double>> samples) {
  std::sort(samples.begin(), samples.end());
  samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
  std::vector<double> steps;
  for (std::size_t i = 1; i < samples.size(); ++i) {
    const double dc = samples[i].first - samples[i - 1].first;
    const double dt = samples[i].second - samples[i - 1].second;
    if (dc > 0 && dt > 0) steps.push_back(dt / dc);
  }
  if (steps.size() < 10) return 0.0;
  std::vector<double> sorted = steps;
  std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
  const double period = sorted[sorted.size() / 2];
  std::size_t bestStart = 0, bestLength = 0, runStart = 0;
  for (std::size_t i = 1; i <= samples.size(); ++i) {
    bool steady = false;
    if (i < samples.size()) {
      const double dc = samples[i].first - samples[i - 1].first;
      const double dt = samples[i].second - samples[i - 1].second;
      steady = dc > 0 && dt > 0 && std::abs(dt / dc - period) <= period * 0.1;
    }
    if (!steady) {
      if (i - runStart > bestLength) {
        bestLength = i - runStart;
        bestStart = runStart;
      }
      runStart = i;
    }
  }
  if (bestLength < 10) return 0.0;
  double mx = 0, my = 0;
  for (std::size_t i = bestStart; i < bestStart + bestLength; ++i) {
    mx += samples[i].first;
    my += samples[i].second;
  }
  const double n = static_cast<double>(bestLength);
  mx /= n;
  my /= n;
  double sxx = 0, sxy = 0;
  for (std::size_t i = bestStart; i < bestStart + bestLength; ++i) {
    sxx += (samples[i].first - mx) * (samples[i].first - mx);
    sxy += (samples[i].first - mx) * (samples[i].second - my);
  }
  return sxx > 0 && sxy > 0 ? sxx / sxy : 0.0;
}

// ── one variant ─────────────────────────────────────────────────────────────

struct Result {
  std::string name;
  double statsHz = 0.0;     // the clock its frames were shown on
  double pacedHz = 0.0;     // its presents, hitch and catch-up left out
  double longRunHz = 0.0;   // presents over the whole time
  int hitches = 0;
  int above = -1;
  std::string aboveNames;
  int reasserts = 0;
  std::string modes;        // the presentation modes its frames took
  std::string note;
};

Result runVariant(const Variant& v, const Screen& screen, const Screen& deskScreen, HWND desk, double seconds,
                  double screenHz) {
  Result result;
  result.name = v.name;
  Chain out;
  HWND outHwnd = nullptr;
  IDXGISwapChain1* outChain = nullptr;
#if DECKBOY_FLIP_LAB_SDL
  SdlOutput sdlOut;
  if (v.sdl) {
    if (!createSdlOutput(v, screen, deskScreen, sdlOut)) {
      result.note = std::string("could not create the output through SDL: ") + SDL_GetError();
      destroySdlOutput(sdlOut);
      return result;
    }
    outHwnd = sdlOut.hwnd;
    outChain = sdlOut.chain;
    result.note = "drawn on " + sdlOut.adapterName + "; " + sdlOut.shape + "; ";
  }
#endif
  if (!v.sdl) {
    if (!createOutput(v, screen, deskScreen, out)) {
      result.note = "could not create the output";
      destroyWindowAndChain(out);
      return result;
    }
    outHwnd = out.hwnd;
    outChain = out.chain;
  }
  Chain deskChain;
  if (v.deskChain) {
    RECT deskRect {};
    GetClientRect(desk, &deskRect);
    deskChain.hwnd = desk;
    deskChain.width = static_cast<int>(std::max(1L, deskRect.right - deskRect.left));
    deskChain.height = static_cast<int>(std::max(1L, deskRect.bottom - deskRect.top));
    // SDL's desk: adapter 0; this build of Deckboy gives it two frames in flight.
    if (!createChain(deskChain, nullptr, 2, v.colorSpace)) result.note = "no desk chain; ";
  }
  if (v.topmost) raiseTopmost(outHwnd);
  if (v.marked) markFullscreen(outHwnd, TRUE);
  if (v.exclusive && out.chain) {
    SetForegroundWindow(out.hwnd);
    pumpMessages();
    const HRESULT hr = out.chain->SetFullscreenState(TRUE, out.output);
    if (SUCCEEDED(hr)) {
      out.chain->ResizeBuffers(0, static_cast<UINT>(out.width), static_cast<UINT>(out.height), DXGI_FORMAT_UNKNOWN,
                               out.chainFlags);
    } else {
      char text[64];
      std::snprintf(text, sizeof(text), "SetFullscreenState failed 0x%08lx; ", static_cast<unsigned long>(hr));
      result.note += text;
    }
  }
  focusDesk(desk);
#if DECKBOY_FLIP_LAB_SDL
  if (sdlOut.deskHwnd) focusDesk(sdlOut.deskHwnd);   // sdl-full: Deckboy's desk has the focus
#endif

  const UINT deskFlags = (deskChain.chainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) ? DXGI_PRESENT_ALLOW_TEARING : 0;
  unsigned frame = 0;
  auto pass = [&]() {
    if (deskChain.chain) drawAndPresent(deskChain, frame, 0, deskFlags);   // the desk first, as Deckboy's loop
#if DECKBOY_FLIP_LAB_SDL
    if (v.sdl) {
      presentSdl(sdlOut, frame++);
      return;
    }
#endif
    drawAndPresent(out, frame++, 1, 0);
  };
  Modes modes;
  // Settle: Windows takes a few frames to pick a path.
  const double settleUntil = qpcSeconds() + 2.0;
  while (qpcSeconds() < settleUntil) {
    pass();
    pumpMessages();
  }
  const double framePeriod = screenHz > 1.0 ? 1.0 / screenHz : 1.0 / 60.0;
  std::vector<std::pair<double, double>> syncs;
  double last = qpcSeconds(), regularSum = 0.0, nextCheck = last + 0.25;
  const double begin = last, end = last + seconds;
  int regularCount = 0, presents = 0;
  bool afterHitch = false;
  while (last < end) {
    pass();
    ++presents;
    const double now = qpcSeconds();
    const double gap = now - last;
    last = now;
    if (gap > framePeriod * 1.5) {
      ++result.hitches;
      afterHitch = true;
    } else if (afterHitch) {
      afterHitch = false;   // the catch-up present after a stall returns at once
    } else {
      regularSum += gap;
      ++regularCount;
    }
    DXGI_FRAME_STATISTICS stats {};
    if (SUCCEEDED(outChain->GetFrameStatistics(&stats)) && stats.SyncRefreshCount > 0 &&
        stats.SyncQPCTime.QuadPart > 0) {
      syncs.emplace_back(static_cast<double>(stats.SyncRefreshCount),
                         static_cast<double>(stats.SyncQPCTime.QuadPart) / qpcFrequency());
    }
    modes.add(outChain);
    pumpMessages();
    if (v.hold && now >= nextCheck) {
      nextCheck = now + 0.25;
      if (windowsAbove(outHwnd, screen.rect).count > 0) {
        raiseTopmost(outHwnd);
        ++result.reasserts;
      }
    }
  }
  result.modes = modes.text();
  result.statsHz = fitClock(std::move(syncs));
  result.pacedHz = regularSum > 0.0 ? regularCount / regularSum : 0.0;
  result.longRunHz = last > begin ? presents / (last - begin) : 0.0;
  const Above above = windowsAbove(outHwnd, screen.rect);
  result.above = above.count;
  result.aboveNames = above.names;
  if (v.exclusive && out.chain) {
    BOOL fullscreen = FALSE;
    out.chain->GetFullscreenState(&fullscreen, nullptr);
    result.note += std::string("still exclusive at the end: ") + (fullscreen ? "yes" : "no");
  }
  if (v.marked) markFullscreen(outHwnd, FALSE);
  if (deskChain.chain) {
    destroyChain(deskChain);
    InvalidateRect(desk, nullptr, TRUE);   // back to the plain GDI desk
  }
#if DECKBOY_FLIP_LAB_SDL
  if (v.sdl) destroySdlOutput(sdlOut);
#endif
  destroyWindowAndChain(out);
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
  std::vector<std::string> names = {"plain",      "topmost", "sdl-attrs",  "owndc",
                                    "colorspace", "moved",   "desk-chain", "deckboy"};
#if DECKBOY_FLIP_LAB_SDL
  for (const char* name : {"sdl", "sdl-dwm", "sdl-frame", "sdl-late", "sdl-fixed", "sdl-ownchain",
                           "plain-sdlrenderer", "sdl-full"}) {
    names.push_back(name);
  }
#endif
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--monitor" && i + 1 < argc) {
      wanted = std::atoi(argv[++i]);
    } else if (arg == "--seconds" && i + 1 < argc) {
      seconds = std::max(3.0, std::atof(argv[++i]));
    } else if (arg == "--only" && i + 1 < argc) {
      names.clear();
      std::string list = argv[++i];
      std::size_t start = 0;
      while (start <= list.size()) {
        const std::size_t comma = list.find(',', start);
        names.push_back(list.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
    } else {
      std::printf("usage: deckboy-output-flip-lab [--monitor N] [--seconds S] [--only plain,deckboy,...]\n");
      return 2;
    }
  }
  std::vector<Variant> variants;
  for (const std::string& name : names) {
    Variant v;
    if (!makeVariant(name, v)) {
      std::printf("unknown variant: %s\n", name.c_str());
      return 2;
    }
    variants.push_back(v);
  }
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#if DECKBOY_FLIP_LAB_SDL
  const bool anySdl = std::any_of(variants.begin(), variants.end(), [](const Variant& v) { return v.sdl; });
  if (anySdl) {
    // The two hints Deckboy sets before SDL_Init (CLAUDE.md: both load-bearing).
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D_THREADSAFE, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      std::printf("SDL_Init failed: %s\n", SDL_GetError());
      return 2;
    }
  }
#endif

  std::vector<Screen> screens;
  EnumDisplayMonitors(nullptr, nullptr, collectScreen, reinterpret_cast<LPARAM>(&screens));
  const Screen* target = nullptr;
  const Screen* deskScreen = nullptr;
  for (const Screen& s : screens) {
    if (!target && (wanted ? displayNumber(s) == wanted : !s.primary)) target = &s;
  }
  for (const Screen& s : screens) {
    if (&s != target && (!deskScreen || s.primary)) deskScreen = &s;
  }
  if (!target || !deskScreen) {
    std::printf("needs two screens: the output's and another for the desk. Screens:");
    for (const Screen& s : screens) std::printf(" DISPLAY%d%s", displayNumber(s), s.primary ? " (main)" : "");
    std::printf("\n");
    return 2;
  }
  const double setHz = configuredHz(*target);
  const double deskHz = configuredHz(*deskScreen);
  const double dwmHz = compositorHz();
  std::printf("output screen: DISPLAY%d, %ldx%ld, set to %.3f Hz; desk screen: DISPLAY%d at %.3f Hz; "
              "compositor %.3f Hz\n",
              displayNumber(*target), target->rect.right - target->rect.left, target->rect.bottom - target->rect.top,
              setHz, displayNumber(*deskScreen), deskHz, dwmHz);

  HWND desk = createDesk(*deskScreen);
  double screenHz = setHz;
  {
    Variant plain;
    makeVariant("plain", plain);
    Chain probe;
    if (createOutput(plain, *target, *deskScreen, probe)) {
      screenHz = measuredScreenHz(probe);
      std::printf("output screen's vblank, timed: %.3f Hz (drawn on %s)\n", screenHz, probe.adapterName.c_str());
    }
    destroyWindowAndChain(probe);
  }

  std::vector<Result> results;
  for (const Variant& v : variants) {
    std::printf("running %-12s ...\n", v.name.c_str());
    std::fflush(stdout);
    results.push_back(runVariant(v, *target, *deskScreen, desk, seconds, screenHz));
  }
  DestroyWindow(desk);
#if DECKBOY_FLIP_LAB_SDL
  if (anySdl) SDL_Quit();
#endif
  CoUninitialize();

  std::printf("\n%-12s %9s %9s %9s %8s %6s %8s  %s\n", "variant", "shown Hz", "paced Hz", "long Hz", "hitches",
              "above", "reraised", "verdict (by the clock its frames were shown on)");
  for (const Result& r : results) {
    const double clock = r.statsHz > 0.0 ? r.statsHz : r.pacedHz;
    const char* verdict = clock <= 0.0                 ? "no data"
                          : sameClock(clock, screenHz) ? "ITS SCREEN'S CLOCK"
                          : sameClock(clock, dwmHz)    ? "the compositor's clock"
                                                       : "another clock";
    std::printf("%-12s %9.3f %9.3f %9.3f %8d %6d %8d  %s%s\n", r.name.c_str(), r.statsHz, r.pacedHz, r.longRunHz,
                r.hitches, r.above, r.reasserts, verdict, r.statsHz > 0.0 ? "" : " (no frame statistics)");
    std::printf("%14s path: %s\n", "", r.modes.c_str());
    if (!r.aboveNames.empty()) std::printf("%14s over it: %s\n", "", r.aboveNames.c_str());
    if (!r.note.empty()) std::printf("%14s %s\n", "", r.note.c_str());
  }
  return 0;
}

#endif
