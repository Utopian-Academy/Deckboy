// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// main.cpp — Deckboy application entry point and monolithic App class.
//
// This is the largest file in the codebase (~5300 lines) and contains:
//
//   1. Free-standing helper functions (lines ~100–3450):
//      - Windows-specific utilities (UTF-8 conversion, icon loading)
//      - UI text helpers (ellipsize, cue kind labels, timecode formatting)
//      - OSC protocol parser and query server
//      - Network master clock (NMC) sync packet handling
//      - ATEM tally/trigger protocol parser
//      - Art-Net DMX packet handling
//      - Project serialization helpers (escape/unescape tab-delimited fields)
//      - Audio waveform peak analysis
//      - Runtime structs: OutputRuntime, DeckRuntime, PipOverlayRuntime
//
//   2. class App (line ~3453 onward):
//      - SDL window/renderer lifecycle (init, run, shutdown)
//      - Main loop: event pump → update → render → present
//      - Project state (load/save .deckboy files)
//      - Multi-deck management with output routing
//      - Integration thread management (ATEM, NDI trigger, NMC, MTC, LTC, Art-Net)
//      - .ipp file includes for modular organization of App methods:
//          app_smoke.ipp        — startup self-test / smoke test
//          app_accessors.ipp    — getters for deck/cue/output state
//          app_network.ipp      — OSC server, Companion integration
//          app_output_mgmt.ipp  — output window lifecycle, stream writer
//          app_project_state.ipp — save/load project, import/export
//          app_remote_command.ipp — remote command handler (OSC, Companion)
//          app_update.ipp       — per-frame update logic
//          app_overlays.ipp     — lower-third overlay management
//          app_render_control.ipp — transport control panel rendering
//          app_render_main.ipp  — main control window rendering
//          app_geometry.ipp     — cue/output geometry calculations
//          app_render_output.ipp — output window compositor + NDI/DeckLink
//          app_ui_widgets.ipp   — reusable UI widget functions
//          app_render_settings.ipp — settings modal dialog rendering
//          app_input.ipp        — keyboard/mouse input handling
//          app_cue_transport.ipp — cue playback transport (play/stop/seek)
//          app_quick_action.ipp — quick-action command palette
//          app_cue_mgmt.ipp     — cue list management (add/remove/reorder)
//
//   3. main() / WinMain() entry points (line ~5315)
//
// Threading model:
//   Main thread: SDL event loop, UI rendering, settings, cue management
//   Per-deck: video decode thread, audio decode thread (in MediaEngine)
//   Per-output: stream writer thread (ffmpeg pipe)
//   Integration: ATEM, NDI trigger, NMC, MTC, LTC, Art-Net threads
//   OSC server: listener thread for incoming OSC/Companion messages
// ============================================================================

#include "core/sdl_compat.hpp"
#include <SDL3_ttf/SDL_ttf.h>
#ifdef _WIN32
#endif

#include "core/constants.hpp"
#include "core/types.hpp"
#include "core/utils.hpp"
#include "deckboy_version.hpp"
#include "core/paths.hpp"
#include "core/subprocess.hpp"
#include "core/palette.hpp"
#include "core/expression_parser.hpp"
#include "core/single_instance_guard.hpp"
#include "core/cue_helpers.hpp"
#include "core/pixel_effects.hpp"
#include "core/pattern_helpers.hpp"
#include "core/io_utils.hpp"
#include "core/subtitle_parser.hpp"
#include "core/system_browser.hpp"
#include "engine/media_engine.hpp"
#include "engine/hap_decoder.hpp"
#include "engine/gpu_readback.hpp"
#include "engine/motion_field.hpp"
#include "platform/capture_backend.hpp"
#include "platform/dynamic_library.hpp"
#include "platform/ltc_api.hpp"
#include "platform/midi.hpp"
#include "platform/ndi_api.hpp"
#include "platform/ndi_trigger_api.hpp"
#include "platform/network.hpp"
#include "platform/integration_backend.hpp"
#include "platform/output_backend.hpp"
#include "platform/browser.hpp"
#include "platform/pdf_import.hpp"
#include "platform/decklink.hpp"
#include "platform/siphon_spout.hpp"
#include "platform/st2110_output.hpp"
#include "platform/ptp_client.hpp"
#include "platform/asio_audio.hpp"
#include "platform/nmos_node.hpp"
#include "render/primitives.hpp"
#include "render/layout.hpp"
#include "render/texture_helpers.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <set>
#include <string>
#include <system_error>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <dlfcn.h>
#include <execinfo.h>  // backtrace / backtrace_symbols_fd in deckboyPosixCrashHandler
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>  // K32GetProcessMemoryInfo — soak-mode RSS logging
#include <shellapi.h>
#include <objbase.h>
#include <dbghelp.h>  // symbolised stack in deckboyCrashHandler
#pragma comment(lib, "dbghelp.lib")
#endif

#if defined(DECKBOY_HAS_NDI_SDK)
#include <Processing.NDI.Lib.h>
#endif

#if defined(DECKBOY_HAS_ALSA)
#include <alsa/asoundlib.h>
#endif

namespace fs = std::filesystem;

using deckboy::core::Paths;

// ════════════════════════════════════════════════════════════════════════════
// Anonymous namespace: free-standing helpers, constants, and runtime structs
// used by the App class. Everything here is file-local.
// ════════════════════════════════════════════════════════════════════════════
namespace {
  using deckboy::render::Primitives;
  using deckboy::platform::browser::BrowserStartPhase;
  using namespace deckboy::core::utils;

// ── Audio and network constants ─────────────────────────────────────────────
constexpr SDL_AudioFormat kAudioFormat = SDL_AUDIO_S16;           // SDL audio format: signed 16-bit native endian
constexpr int kDefaultAtemBridgePort = 9910;             // ATEM switcher default UDP port
constexpr int kDefaultArtNetPort = 6454;                 // Art-Net default UDP port (IANA registered)
constexpr int kDefaultNmcSyncPort = 51010;               // Network master clock sync port
constexpr int kDefaultNmcLocateIntervalMs = 250;         // NMC peer discovery interval
constexpr int kDmxTriggerThreshold = 127;                // DMX value threshold for trigger activation
const fs::path kUiPackRelativePathV3 = fs::path("ui") / "deckboy_ui_pack_v3";  // UI font/asset pack path
const fs::path kUiPackRelativePathV2 = fs::path("ui") / "deckboy_ui_pack_v2";  // Legacy UI pack path

std::atomic<bool> gShouldQuit = false;  // Global quit flag (set by signal handlers)

// ── Version and platform utilities ──────────────────────────────────────────

void printDeckboyVersion(std::ostream& out) {
  out << "Deckboy " << deckboy::core::version::kVersionTag << '\n';
}

#ifdef _WIN32
// Convert a Windows wide string (UTF-16) to a UTF-8 std::string.
// Used for command-line argument conversion on Windows (WinMain receives wchar_t*).
std::string utf8FromWide(const wchar_t* text) {
  if (!text || !*text) {
    return {};
  }
  int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (needed <= 1) {
    return {};
  }
  std::string utf8(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), needed, nullptr, nullptr);
  utf8.pop_back();
  return utf8;
}

// Parse the Windows command line into UTF-8 arguments (replaces argv from WinMain).
std::vector<std::string> windowsCommandLineArgsUtf8() {
  int argc = 0;
  LPWSTR* argvWide = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argvWide || argc <= 0) {
    if (argvWide) {
      LocalFree(argvWide);
    }
    return {};
  }

  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    args.push_back(utf8FromWide(argvWide[i]));
  }
  LocalFree(argvWide);
  return args;
}

// Load the Deckboy app icon from the embedded Windows resource at the given size.
HICON loadDeckboyAppIconHandle(int width, int height) {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!instance) {
    return nullptr;
  }
  return static_cast<HICON>(LoadImageW(
    instance,
    L"IDI_DECKBOY_APP_ICON",
    IMAGE_ICON,
    width,
    height,
    LR_DEFAULTCOLOR));
}

// Set the Deckboy icon on an SDL window via Windows WM_SETICON messages.
// Loads both large (taskbar) and small (title bar) icon sizes from resources.
void applyDeckboyWindowIcon(SDL_Window* window) {
  if (!window) {
    return;
  }
  HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
    SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
  if (!hwnd) {
    return;
  }
  HICON bigIcon = loadDeckboyAppIconHandle(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
  HICON smallIcon = loadDeckboyAppIconHandle(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
  if (bigIcon) {
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
  }
  if (smallIcon) {
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
  } else if (bigIcon) {
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(bigIcon));
  }
}
#else
void applyDeckboyWindowIcon(SDL_Window*) {}
#endif

// ── UI text helpers ─────────────────────────────────────────────────────────

// Truncate text to fit within maxWidth pixels, appending "..." if clipped.
// Used throughout the UI for labels, cue names, and file paths that may
// exceed their allocated display width.
std::string ellipsizeToPixelWidth(TTF_Font* font, const std::string& text, int maxWidth) {
  if (!font || maxWidth <= 0 || text.empty()) {
    return "";
  }

  int textW = 0;
  int textH = 0;
  if (TTF_GetStringSize(font, text.c_str(), 0, &textW, &textH) && textW <= maxWidth) {
    return text;
  }

  const std::string kEllipsis = "...";
  int ellipsisW = 0;
  if (!TTF_GetStringSize(font, kEllipsis.c_str(), 0, &ellipsisW, &textH)) {
    return text;
  }
  if (ellipsisW > maxWidth) {
    return "";
  }

  std::string clipped = text;
  while (!clipped.empty()) {
    clipped.pop_back();
    std::string candidate = clipped + kEllipsis;
    if (TTF_GetStringSize(font, candidate.c_str(), 0, &textW, &textH) && textW <= maxWidth) {
      return candidate;
    }
  }

  return kEllipsis;
}

// Returns the human-readable display label for a CueKind (shown in UI).
std::string cueKindLabel(CueKind kind) {
  switch (kind) {
    case CueKind::Image:      return "Still";
    case CueKind::Pattern:    return "Pattern";
    case CueKind::Browser:    return "Browser";
    case CueKind::WindowSource: return "Window Source";
    case CueKind::Camera:     return "Camera Source";
    case CueKind::Syphon:     return "Syphon/Spout Source";
    case CueKind::SrtStream:  return "Stream";
    case CueKind::NdiSource:  return "NDI Source";
    case CueKind::Pip:        return "PIP";
    case CueKind::LowerThird: return "Lower Third";
    case CueKind::Composite:  return "Composite";
    case CueKind::Audio:      return "Audio";
    case CueKind::Timer:      return "Timer";
    case CueKind::Video:
    default:                  return "Video";
  }
}

// Returns the serialization token for a CueKind (used in .deckboy project files).
// cueKindToken lives in core/utils.cpp -- it is declared in core/utils.hpp and
// this file had a SECOND definition of it with a different set of cases. Two
// non-static definitions of one function is an ODR violation; the linker
// picked one and the other silently did nothing.

// Normalize user-entered composite layout names to canonical tokens.
// Accepts various aliases: "split", "pip", "four" → "2up", "7030", "quad".
std::string normalizeCompositeLayoutPresetToken(std::string token) {
  token = toLower(trim(token));
  if (token == "split" || token == "split2" || token == "two" || token == "2") {
    return "2up";
  }
  if (token == "bigsmall" || token == "pip" || token == "pictureinpicture" || token == "70-30") {
    return "7030";
  }
  if (token == "4" || token == "four") {
    return "quad";
  }
  if (token == "2up" || token == "7030" || token == "quad") {
    return token;
  }
  return "2up";
}

std::string compositeLayoutPresetLabel(const std::string& rawToken) {
  std::string token = normalizeCompositeLayoutPresetToken(rawToken);
  if (token == "7030") {
    return "70/30";
  }
  if (token == "quad") {
    return "Quad";
  }
  return "2-Up";
}

// ── Composite cue helpers ────────────────────────────────────────────────────
// Composite cues split the output into multiple slots (2-up, quad, 70/30),
// each showing a different source. These helpers manage slot identities,
// layout presets, and source resolution.

std::string compositeSlotDefaultId(int index) {
  int slotNumber = std::max(0, index) + 1;
  return "slot" + std::to_string(slotNumber);
}

std::string compositeSlotDefaultName(int index) {
  static constexpr char kNames[] = {'A', 'B', 'C', 'D'};
  if (index >= 0 && index < static_cast<int>(sizeof(kNames))) {
    return std::string("Slot ") + kNames[index];
  }
  return "Slot " + std::to_string(std::max(0, index) + 1);
}

void ensureCompositeSlotIdentity(CompositeSlot& slot, int index) {
  if (trim(slot.id).empty()) {
    slot.id = compositeSlotDefaultId(index);
  }
  if (trim(slot.name).empty()) {
    slot.name = compositeSlotDefaultName(index);
  }
  slot.sourceType = trim(slot.sourceType).empty() ? "media" : toLower(trim(slot.sourceType));
  slot.normX = std::clamp(slot.normX, 0.0f, 1.0f);
  slot.normY = std::clamp(slot.normY, 0.0f, 1.0f);
  slot.normW = std::clamp(slot.normW, 0.05f, 1.0f);
  slot.normH = std::clamp(slot.normH, 0.05f, 1.0f);
}

void applyCompositePresetToCue(Cue& cue, const std::string& rawPreset) {
  std::string preset = normalizeCompositeLayoutPresetToken(rawPreset);
  size_t slotCount = preset == "quad" ? 4u : 2u;
  std::vector<CompositeSlot> previous = cue.compositeSlots;
  cue.compositeSlots.assign(slotCount, CompositeSlot {});
  for (size_t i = 0; i < slotCount; ++i) {
    if (i < previous.size()) {
      cue.compositeSlots[i] = previous[i];
    }
    ensureCompositeSlotIdentity(cue.compositeSlots[i], static_cast<int>(i));
  }

  cue.compositeLayoutPreset = preset;
  if (cue.compositeBackgroundColor.a == 0) {
    cue.compositeBackgroundColor = SDL_Color {18, 24, 18, 255};
  }

  if (preset == "7030") {
    cue.compositeSlots[0].normX = 0.02f;
    cue.compositeSlots[0].normY = 0.02f;
    cue.compositeSlots[0].normW = 0.68f;
    cue.compositeSlots[0].normH = 0.96f;
    cue.compositeSlots[1].normX = 0.73f;
    cue.compositeSlots[1].normY = 0.08f;
    cue.compositeSlots[1].normW = 0.24f;
    cue.compositeSlots[1].normH = 0.24f;
  } else if (preset == "quad") {
    static constexpr std::array<std::array<float, 4>, 4> kQuad {{
      {{0.02f, 0.02f, 0.46f, 0.46f}},
      {{0.52f, 0.02f, 0.46f, 0.46f}},
      {{0.02f, 0.52f, 0.46f, 0.46f}},
      {{0.52f, 0.52f, 0.46f, 0.46f}},
    }};
    for (size_t i = 0; i < cue.compositeSlots.size(); ++i) {
      cue.compositeSlots[i].normX = kQuad[i][0];
      cue.compositeSlots[i].normY = kQuad[i][1];
      cue.compositeSlots[i].normW = kQuad[i][2];
      cue.compositeSlots[i].normH = kQuad[i][3];
    }
  } else {
    cue.compositeSlots[0].normX = 0.02f;
    cue.compositeSlots[0].normY = 0.02f;
    cue.compositeSlots[0].normW = 0.46f;
    cue.compositeSlots[0].normH = 0.96f;
    cue.compositeSlots[1].normX = 0.52f;
    cue.compositeSlots[1].normY = 0.02f;
    cue.compositeSlots[1].normW = 0.46f;
    cue.compositeSlots[1].normH = 0.96f;
  }

  if (trim(cue.compositeAudioSlotId).empty()) {
    cue.compositeAudioSlotId = cue.compositeSlots.empty() ? std::string() : cue.compositeSlots.front().id;
  }
  bool audioSlotFound = false;
  for (const CompositeSlot& slot : cue.compositeSlots) {
    if (slot.id == cue.compositeAudioSlotId) {
      audioSlotFound = true;
      break;
    }
  }
  if (!audioSlotFound) {
    cue.compositeAudioSlotId = cue.compositeSlots.empty() ? std::string() : cue.compositeSlots.front().id;
  }
}

// isSourceCueKind → core/cue_helpers.hpp

// ── Source cue helpers ──────────────────────────────────────────────────────
// Source cues (WindowSource, Camera, Syphon) capture live input.
// These helpers manage source type tokens, reference formatting, and
// user-facing labels for the source input editor.

std::string sourceCueTokenForKind(CueKind kind) {
  switch (kind) {
    case CueKind::WindowSource: return "window";
    case CueKind::Camera:       return "camera";
    case CueKind::Syphon:       return "syphon";
    default:                    return "source";
  }
}

// defaultSourceRefForKind, sourceCueRefFromCue → core/cue_helpers.hpp

std::string sourceCueRefFriendlyLabel(CueKind kind, const std::string& rawRef) {
  std::string ref = trim(rawRef);
  if (ref.empty()) {
    ref = defaultSourceRefForKind(kind);
  }
  std::string lower = ref;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (kind == CueKind::WindowSource) {
    if (lower == "active-window") {
      return "Focused Window (recommended)";
    }
    if (lower == "desktop") {
      return "Desktop (full screen)";
    }
    if (lower.rfind("title:", 0) == 0 && ref.size() > 6) {
      return ref.substr(6);  // Show just the window title
    }
    if (lower.rfind("id:", 0) == 0) {
      return "Specific Window (advanced)";
    }
    if (lower.rfind("region:", 0) == 0) {
      return "Screen Region (advanced)";
    }
    if (!lower.empty() && lower.front() == ':') {
      return "Screen Region (advanced)";
    }
  } else if (kind == CueKind::Camera) {
    if (lower == "default-camera" || lower == "default") {
      return "Default Camera";
    }
  } else if (kind == CueKind::Syphon) {
    if (lower == "default-bus" || lower == "default") {
      return "Default App Feed";
    }
  }
  return ref;
}

std::string sourceCueRefFromAlias(CueKind kind, const std::string& rawRef) {
  std::string ref = trim(rawRef);
  std::string lower = ref;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (ref.empty() || lower == "default") {
    return defaultSourceRefForKind(kind);
  }
  if (kind == CueKind::WindowSource) {
    if (lower == "focused"
        || lower == "recommended"
        || lower == "focused window"
        || lower == "window"
        || lower == "active") {
      return "active-window";
    }
    if (lower == "screen" || lower == "full screen" || lower == "fullscreen") {
      return "desktop";
    }
    // Preserve title: prefix as-is (window picker selections)
    if (lower.rfind("title:", 0) == 0) {
      return ref;
    }
  } else if (kind == CueKind::Camera) {
    if (lower == "camera"
        || lower == "cam"
        || lower == "recommended") {
      return "default-camera";
    }
  } else if (kind == CueKind::Syphon) {
    if (lower == "syphon"
        || lower == "spout"
        || lower == "app"
        || lower == "feed"
        || lower == "bus"
        || lower == "recommended") {
      return "default-bus";
    }
  }
  return ref;
}

std::string sourceCueEditorInputDefault(CueKind kind, const std::string& rawRef) {
  std::string resolved = sourceCueRefFromAlias(kind, rawRef);
  std::string lower = resolved;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (kind == CueKind::WindowSource && lower == "active-window") {
    return "focused";
  }
  if ((kind == CueKind::Camera && lower == "default-camera")
      || (kind == CueKind::Syphon && lower == "default-bus")) {
    return "default";
  }
  return resolved;
}

std::string sourceCueEditorPrompt(CueKind kind) {
  switch (kind) {
    case CueKind::WindowSource:
      return "Type focused, then press Enter.";
    case CueKind::Camera:
      return "Type default, then press Enter.";
    case CueKind::Syphon:
      return "Type default, then press Enter.";
    default:
      return "Set source";
  }
}

// resolvedCueEndAction, cueAdvancesWhenFinished → core/cue_helpers.hpp

// ── Cue display and formatting helpers ──────────────────────────────────────

// Returns the transport status label for the cue's end action (shown in UI).
std::string cueEndStatusLabel(const Cue& cue) {
  switch (resolvedCueEndAction(cue)) {
    case CueEndAction::Loop: return "END LOOP";
    case CueEndAction::PauseOnLast: return "END HOLD";
    case CueEndAction::Stop: return "END STOP";
    case CueEndAction::AutoNext:
    case CueEndAction::Inherit:
    default: return "END NEXT";
  }
}

std::string normalizeJumpModeToken(std::string token) {
  token = trim(token);
  std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (token == "LOAD" || token == "ARM" || token == "READY") {
    return "load";
  }
  return "trigger";
}

std::string jumpModeLabelFromToken(const std::string& token) {
  return normalizeJumpModeToken(token) == "load" ? "Load" : "Trigger";
}

std::string normalizePanicProfileToken(std::string token) {
  token = trim(token);
  std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (token == "FADE_PAUSE" || token == "FADEPAUSE" || token == "PAUSE") {
    return "fade_pause";
  }
  if (token == "FADE_REWIND" || token == "FADEREWIND" || token == "REWIND" || token == "STOP") {
    return "fade_rewind";
  }
  if (token == "FADE_LOAD_NEXT" || token == "FADELOADNEXT" || token == "LOAD_NEXT" || token == "LOADNEXT" || token == "NEXT") {
    return "fade_load_next";
  }
  return "outputs_off";
}

std::string panicProfileLabelFromToken(const std::string& token) {
  std::string normalized = normalizePanicProfileToken(token);
  if (normalized == "fade_pause") {
    return "Fade+Pause";
  }
  if (normalized == "fade_rewind") {
    return "Fade+Rewind";
  }
  if (normalized == "fade_load_next") {
    return "Fade+LoadNext";
  }
  return "Outputs Off";
}

std::optional<SDL_Color> tryParseColor(std::string_view input) {
  std::string value(input);
  if ((value.size() != 7 && value.size() != 9) || value[0] != '#') {
    return std::nullopt;
  }
  auto fromHex = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') {
      return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
      return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
      return ch - 'A' + 10;
    }
    return -1;
  };

  auto readByte = [&](int offset) -> int {
    int hi = fromHex(value[offset]);
    int lo = fromHex(value[offset + 1]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    return hi * 16 + lo;
  };

  int r = readByte(1);
  int g = readByte(3);
  int b = readByte(5);
  int a = value.size() == 9 ? readByte(7) : 255;
  if (r < 0 || g < 0 || b < 0 || a < 0) {
    return std::nullopt;
  }
  return SDL_Color {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), static_cast<Uint8>(a)};
}

SDL_Color parseColor(std::string_view input) {
  if (auto parsed = tryParseColor(input)) {
    return *parsed;
  }
  return {48, 98, 48, 255};
}

std::string normalizeCueIdShort(std::string value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') {
      out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
  }
  if (out.size() > 6) {
    out.resize(6);
  }
  return out;
}

std::string cueDisplayToken(const Cue& cue, int index) {
  if (!trim(cue.cueId).empty()) {
    return cue.cueId;
  }
  if (!trim(cue.cueNumber).empty()) {
    return cue.cueNumber;
  }
  return std::to_string(index + 1);
}

double normalizePlaylistTimebaseFps(double fps) {
  static constexpr std::array<double, 4> kChoices {24.0, 25.0, 29.97, 30.0};
  double candidate = std::isfinite(fps) ? fps : 30.0;
  double best = kChoices.front();
  double bestDiff = std::fabs(candidate - best);
  for (double choice : kChoices) {
    double diff = std::fabs(candidate - choice);
    if (diff < bestDiff) {
      best = choice;
      bestDiff = diff;
    }
  }
  return best;
}

std::string playlistTimebaseLabel(double fps) {
  double normalized = normalizePlaylistTimebaseFps(fps);
  if (std::fabs(normalized - 29.97) < 0.01) {
    return "29.97";
  }
  int whole = static_cast<int>(std::lround(normalized));
  return std::to_string(whole);
}

bool isDefaultStillDurationCueKind(CueKind kind) {
  return kind == CueKind::Image
    || kind == CueKind::Pattern
    || kind == CueKind::Browser
    || kind == CueKind::Pip
    || kind == CueKind::Composite
    || kind == CueKind::LowerThird;
}

bool cueCanBePipSource(const Cue& cue) {
  return cue.kind == CueKind::Video
    || cue.kind == CueKind::Image
    || cue.kind == CueKind::Pattern
    || cue.kind == CueKind::Browser
    || cue.kind == CueKind::SrtStream
    || cue.kind == CueKind::NdiSource
    || isSourceCueKind(cue.kind);
}

std::string pipSourceTypeTokenFromCue(const Cue& cue) {
  std::string token = trim(cue.pipSourceType);
  std::transform(token.begin(), token.end(), token.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (!token.empty()) {
    return token;
  }
  std::string rawPath = trim(cue.path);
  if (!trim(cue.pipTargetCue).empty() && (rawPath.empty() || rawPath == "graphic://pip")) {
    return "legacy";
  }
  if (rawPath.rfind("source://window/", 0) == 0) {
    return "window";
  }
  if (rawPath.rfind("source://camera/", 0) == 0) {
    return "camera";
  }
  if (rawPath.rfind("source://syphon/", 0) == 0) {
    return "syphon";
  }
  if (rawPath.rfind("source://spout/", 0) == 0) {
    return "syphon";
  }
  if (rawPath.rfind("http://", 0) == 0 || rawPath.rfind("https://", 0) == 0 ||
      rawPath.rfind("file://", 0) == 0 || rawPath.rfind("about:", 0) == 0 ||
      rawPath.rfind("data:", 0) == 0) {
    return "browser";
  }
  if (rawPath.empty() || rawPath == "graphic://pip") {
    return "media";
  }
  return "media";
}

bool pipSourceTypeUsesSourceRef(std::string token) {
  token = trim(token);
  std::transform(token.begin(), token.end(), token.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return token == "window" || token == "camera" || token == "syphon" || token == "spout";
}

std::pair<std::string, std::string> parseCompositeSourceSpec(const std::string& rawSpec) {
  std::string spec = trim(rawSpec);
  if (spec.empty()) {
    return {"media", ""};
  }
  auto colon = spec.find(':');
  if (colon != std::string::npos) {
    std::string prefix = toLower(trim(spec.substr(0, colon)));
    std::string value = trim(spec.substr(colon + 1));
    if (prefix == "media" || prefix == "browser" || prefix == "window" ||
        prefix == "camera" || prefix == "syphon" || prefix == "spout") {
      return {prefix == "spout" ? "syphon" : prefix, value};
    }
  }
  std::string lower = toLower(spec);
  if (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0 ||
      lower.rfind("file://", 0) == 0 || lower.rfind("about:", 0) == 0 ||
      lower.rfind("data:", 0) == 0) {
    return {"browser", spec};
  }
  return {"media", spec};
}

std::string compositeSourceTypeLabel(const std::string& rawType) {
  std::string type = toLower(trim(rawType));
  if (type == "browser") {
    return "Browser";
  }
  if (type == "window") {
    return "Window";
  }
  if (type == "camera") {
    return "Camera";
  }
  if (type == "syphon" || type == "spout") {
    return "Syphon/Spout";
  }
  return "Media";
}

std::string compositeSourceDisplayLabel(const CompositeSlot& slot) {
  std::string type = toLower(trim(slot.sourceType));
  std::string source = trim(slot.source);
  if (type == "browser") {
    return source.empty() ? std::string("(set url)") : source;
  }
  if (type == "window" || type == "camera" || type == "syphon" || type == "spout") {
    CueKind kind = type == "window" ? CueKind::WindowSource
                 : type == "camera" ? CueKind::Camera
                 : CueKind::Syphon;
    std::string sourceRef = source;
    if (sourceRef.rfind("source://", 0) == 0) {
      Cue tempCue;
      tempCue.kind = kind;
      tempCue.path = sourceRef;
      sourceRef = sourceCueRefFromCue(tempCue);
    }
    if (sourceRef.empty()) {
      sourceRef = defaultSourceRefForKind(kind);
    }
    return sourceCueRefFriendlyLabel(kind, sourceRef);
  }
  if (source.empty()) {
    return "(set media file)";
  }
  return fs::path(source).filename().string();
}

std::string pipCueTargetDisplayToken(const Cue& cue) {
  return trim(cue.pipTargetCue);
}

// colorControlsActive, cueHasColorControls, cueHasPixelEffects,
// applyChromaKeyToPixels, applyColorControlsToPixels,
// applyCueVisualEffectsToPixels → core/pixel_effects.hpp

using deckboy::platform::SocketHandle;
using deckboy::platform::kInvalidSocket;
using deckboy::platform::kSocketSendFlags;
using deckboy::platform::closeSocket;
using deckboy::platform::setCloseOnExec;
using deckboy::platform::createBoundSocket;
using deckboy::platform::createDatagramSocket;
using deckboy::platform::socketAddressToString;
using deckboy::platform::selectNfds;

// ── Network Master Clock (NMC) sync ─────────────────────────────────────────
// NMC provides transport synchronization between Deckboy instances over UDP.
// One instance is the "output" (master) that broadcasts position, others
// are "input" (followers) that chase the master's timecode.

struct NmcSyncPacket {
  std::string command;                 // "play", "stop", "seek", "locate"
  std::optional<double> seconds;       // Position in seconds (for seek/locate)
};

std::string normalizeNmcSyncModeToken(const std::string& raw) {
  std::string token = toUpper(trim(raw));
  if (token == "OUT" || token == "OUTPUT" || token == "SEND" || token == "MASTER") {
    return "output";
  }
  return "input";
}

std::optional<double> parseLooseSecondsToken(const std::string& token) {
  if (token.empty()) {
    return std::nullopt;
  }
  try {
    double value = std::stod(token);
    if (!std::isfinite(value)) {
      return std::nullopt;
    }
    return std::max(0.0, value);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<NmcSyncPacket> parseNmcSyncPacket(const std::string& raw) {
  auto parts = splitWhitespace(trim(raw));
  if (parts.empty()) {
    return std::nullopt;
  }

  std::string command = toUpper(parts[0]);
  size_t valueIndex = 1;
  if (command == "DECKBOY_NMC1" || command == "NMC1" || command == "NMC") {
    if (parts.size() < 2) {
      return std::nullopt;
    }
    command = toUpper(parts[1]);
    valueIndex = 2;
  }

  if (command == "ROLL" || command == "GO") {
    command = "PLAY";
  } else if (command == "SEEK" || command == "POSITION" || command == "POS" || command == "JUMP") {
    command = "LOCATE";
  }

  if (command != "PLAY" && command != "PAUSE" && command != "STOP" && command != "LOCATE") {
    return std::nullopt;
  }

  NmcSyncPacket packet;
  packet.command = command;
  if (valueIndex < parts.size()) {
    packet.seconds = parseLooseSecondsToken(parts[valueIndex]);
  }
  return packet;
}

std::string formatNmcSyncPacket(const std::string& command, std::optional<double> seconds = std::nullopt) {
  std::ostringstream output;
  output << "DECKBOY_NMC1 " << toUpper(trim(command));
  if (seconds) {
    output << ' ' << std::fixed << std::setprecision(6) << std::max(0.0, *seconds);
  }
  return output.str();
}

// endsWith, normalizePatternTypeId, stripPatternMotionSuffix,
// patternTypeSupportsMotion, patternTypeIsAnimated → core/pattern_helpers.hpp

// ── OSC (Open Sound Control) protocol ───────────────────────────────────────
// Deckboy implements an OSC server for remote control from external tools
// (Bitfocus Companion, TouchOSC, mixing consoles, etc.). Messages use
// standard OSC binary format with typed arguments.

using OscArg = std::variant<std::int32_t, float, std::string, bool>;

struct OscMessage {
  std::string address;            // OSC address pattern (e.g. "/deck/1/go")
  std::vector<OscArg> args;       // Typed arguments (int32, float, string, bool)
};

struct OscQueryEndpointDoc {
  const char* path;
  const char* command;
  const char* args;
  const char* notes;
};

constexpr std::array<OscQueryEndpointDoc, 41> kOscQueryEndpoints {{
  {"/play", "PLAY", "", "Start focused deck"},
  {"/pause", "PAUSE", "", "Pause focused deck"},
  {"/stop", "STOP", "", "Stop focused deck"},
  {"/go", "GO", "", "Toggle play/pause"},
  {"/toggle", "GO", "", "Alias for GO"},
  {"/take", "TAKE [cue]", "string", "Take selected or cue token"},
  {"/takeid", "TAKEID <cue-id>", "string", "Take by operator cue ID"},
  {"/goto", "GOTO <cue>", "string", "Load/play cue index/token"},
  {"/next", "NEXT", "", "Select next cue"},
  {"/prev", "PREV", "", "Select previous cue"},
  {"/select", "SELECT <cue>", "string", "Select cue without taking"},
  {"/selectid", "SELECTID <cue-id>", "string", "Select by cue ID"},
  {"/deck", "DECK <index>", "int/string", "Focus deck"},
  {"/deck/next", "DECKNEXT", "", "Cycle focused deck forward"},
  {"/deck/prev", "DECKPREV", "", "Cycle focused deck backward"},
  {"/deck/opacity", "DECKOPACITY [0..100]", "number", "Set/query deck opacity"},
  {"/deck/autofade", "DECKAUTOFADE [on|off]", "toggle", "Toggle deck auto fade"},
  {"/deck/fade", "DECKFADE [seconds]", "number", "Set/query deck auto-fade time"},
  {"/route", "ROUTE <output>", "string", "Route focused deck"},
  {"/layer", "LAYER <index>", "int", "Set deck layer index"},
  {"/cue/audio", "CUEAUDIO [on|off]", "toggle", "Per-cue audio enable"},
  {"/cue/pausebegin", "CUEPAUSEBEGIN [on|off]", "toggle", "Per-cue pause at beginning"},
  {"/cue/pauseend", "CUEPAUSEEND [on|off]", "toggle", "Per-cue pause at end"},
  {"/cue/transition", "CUENEXTTRANS [on|off]", "toggle", "Per-cue transition-to-next"},
  {"/cue/goto", "CUEGOTO [target]", "string", "Per-cue goto target token"},
  {"/jumpmode", "JUMPMODE [TRIGGER|LOAD]", "string", "Jump behavior"},
  {"/panic", "PANIC [profile]", "string", "Run panic profile"},
  {"/output", "OUTPUT <index> ...", "string", "Output command namespace"},
  {"/status", "STATUS", "", "Request status snapshot"},
  {"/state", "STATE", "", "Request status snapshot"},
  {"/oscquery", "OSCQUERY [on|off]", "toggle", "Enable OSC Query HTTP server"},
  {"/oscquery/port", "OSCQUERYPORT [port]", "number", "Set/query OSC Query HTTP port"},
  {"/osc/feedback", "OSCFEEDBACK [on|off]", "toggle", "Enable canonical OSC feedback mirror"},
  {"/atem", "ATEM [on|off]", "toggle", "Toggle ATEM trigger adapter"},
  {"/ndi/trigger", "NDITRIGGER [on|off]", "toggle", "Toggle NDI metadata trigger adapter"},
  {"/nmc", "NMC [on|off]", "toggle", "Toggle NMC transport sync adapter"},
  {"/mtc", "MTC [on|off]", "toggle", "Toggle MTC ingest adapter"},
  {"/ltc", "LTC [on|off]", "toggle", "Toggle LTC ingest adapter"},
  {"/artnet", "ARTNET [on|off]", "toggle", "Toggle DMX/Art-Net adapter"},
  {"/artnet/port", "ARTNETPORT [port]", "number", "Set/query Art-Net adapter port"},
  {"/integration", "INTEGRATIONS [STATUS]", "string", "Show integration adapter route status"}
}};

size_t alignOscOffset(size_t offset) {
  return (offset + 3u) & ~size_t(3u);
}

bool readOscString(const std::uint8_t* bytes, size_t size, size_t& offset, std::string& out) {
  if (offset >= size) {
    return false;
  }
  size_t end = offset;
  while (end < size && bytes[end] != 0) {
    ++end;
  }
  if (end >= size) {
    return false;
  }
  out.assign(reinterpret_cast<const char*>(bytes + offset), end - offset);
  offset = alignOscOffset(end + 1u);
  return offset <= size;
}

std::uint32_t readOscU32(const std::uint8_t* bytes, size_t offset) {
  return
    (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
    (static_cast<std::uint32_t>(bytes[offset + 1]) << 16u) |
    (static_cast<std::uint32_t>(bytes[offset + 2]) << 8u) |
    (static_cast<std::uint32_t>(bytes[offset + 3]));
}

void appendOscU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void appendOscString(std::vector<std::uint8_t>& out, const std::string& value) {
  out.insert(out.end(), value.begin(), value.end());
  out.push_back(0);
  while (out.size() % 4u != 0u) {
    out.push_back(0);
  }
}

std::vector<std::uint8_t> buildOscStringMessage(const std::string& address, const std::string& value) {
  std::vector<std::uint8_t> bytes;
  appendOscString(bytes, address);
  appendOscString(bytes, ",s");
  appendOscString(bytes, value);
  return bytes;
}

std::optional<OscMessage> parseOscMessage(const std::string& payload) {
  if (payload.empty() || payload[0] != '/') {
    return std::nullopt;
  }
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
  size_t size = payload.size();
  size_t offset = 0;

  OscMessage message;
  if (!readOscString(bytes, size, offset, message.address) || message.address.empty()) {
    return std::nullopt;
  }

  std::string typeTags;
  if (!readOscString(bytes, size, offset, typeTags) || typeTags.empty() || typeTags[0] != ',') {
    return std::nullopt;
  }

  for (size_t index = 1; index < typeTags.size(); ++index) {
    char tag = typeTags[index];
    switch (tag) {
      case 'i': {
        if (offset + 4u > size) {
          return std::nullopt;
        }
        message.args.emplace_back(static_cast<std::int32_t>(readOscU32(bytes, offset)));
        offset += 4u;
        break;
      }
      case 'f': {
        if (offset + 4u > size) {
          return std::nullopt;
        }
        std::uint32_t raw = readOscU32(bytes, offset);
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(float));
        message.args.emplace_back(value);
        offset += 4u;
        break;
      }
      case 's': {
        std::string value;
        if (!readOscString(bytes, size, offset, value)) {
          return std::nullopt;
        }
        message.args.emplace_back(value);
        break;
      }
      case 'T':
        message.args.emplace_back(true);
        break;
      case 'F':
        message.args.emplace_back(false);
        break;
      default:
        return std::nullopt;
    }
  }

  return message;
}

std::vector<OscMessage> parseOscPacket(const std::string& payload, int depth = 0) {
  std::vector<OscMessage> messages;
  if (payload.empty()) {
    return messages;
  }

  const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
  size_t size = payload.size();
  if (size >= 16 && std::memcmp(bytes, "#bundle\0", 8) == 0) {
    constexpr int kMaxOscBundleDepth = 8;
    if (depth >= kMaxOscBundleDepth) {
      return messages;  // refuse deeply nested bundles (stack overflow prevention)
    }
    size_t offset = 16;  // "#bundle\0" + timetag (8 bytes)
    while (offset + 4u <= size) {
      std::uint32_t elementSize = readOscU32(bytes, offset);
      offset += 4u;
      if (elementSize == 0 || offset + elementSize > size) {
        break;
      }
      std::string element(payload.data() + offset, payload.data() + offset + elementSize);
      auto nested = parseOscPacket(element, depth + 1);
      messages.insert(messages.end(), nested.begin(), nested.end());
      offset += elementSize;
    }
    return messages;
  }

  auto single = parseOscMessage(payload);
  if (single) {
    messages.push_back(*single);
  }
  return messages;
}

std::optional<double> oscArgAsNumber(const OscArg& arg) {
  if (std::holds_alternative<std::int32_t>(arg)) {
    return static_cast<double>(std::get<std::int32_t>(arg));
  }
  if (std::holds_alternative<float>(arg)) {
    return static_cast<double>(std::get<float>(arg));
  }
  if (std::holds_alternative<bool>(arg)) {
    return std::get<bool>(arg) ? 1.0 : 0.0;
  }
  if (std::holds_alternative<std::string>(arg)) {
    try {
      return std::stod(std::get<std::string>(arg));
    } catch (...) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

std::optional<std::string> oscArgAsString(const OscArg& arg) {
  if (std::holds_alternative<std::string>(arg)) {
    return std::get<std::string>(arg);
  }
  if (std::holds_alternative<std::int32_t>(arg)) {
    return std::to_string(std::get<std::int32_t>(arg));
  }
  if (std::holds_alternative<float>(arg)) {
    return std::to_string(std::get<float>(arg));
  }
  if (std::holds_alternative<bool>(arg)) {
    return std::get<bool>(arg) ? "1" : "0";
  }
  return std::nullopt;
}

std::optional<std::string> mapOscToRemoteCommand(const OscMessage& message) {
  std::string path = toUpper(trim(message.address));
  auto argNumber = [&](size_t index) -> std::optional<double> {
    if (index >= message.args.size()) {
      return std::nullopt;
    }
    return oscArgAsNumber(message.args[index]);
  };
  auto argString = [&](size_t index) -> std::optional<std::string> {
    if (index >= message.args.size()) {
      return std::nullopt;
    }
    return oscArgAsString(message.args[index]);
  };
  auto argToggleWord = [&](size_t index) -> std::optional<std::string> {
    auto number = argNumber(index);
    if (!number) {
      return std::nullopt;
    }
    return *number >= 0.5 ? "ON" : "OFF";
  };

  if (path == "/GO" || path == "/TOGGLE") {
    return "GO";
  }
  if (path == "/PLAY") {
    return "PLAY";
  }
  if (path == "/PAUSE") {
    return "PAUSE";
  }
  if (path == "/STOP") {
    return "STOP";
  }
  if (path == "/CLEAR") {
    return "CLEAR";
  }
  if (path == "/NEXT") {
    return "NEXT";
  }
  if (path == "/PREV" || path == "/PREVIOUS") {
    return "PREV";
  }
  if (path == "/FULLSCREEN") {
    return "FULLSCREEN";
  }
  if (path == "/VIDEO" || path == "/OUTPUTMODE") {
    std::string output = "VIDEO";
    for (size_t i = 0; i < message.args.size(); ++i) {
      auto value = argString(i);
      if (!value) {
        continue;
      }
      output += " ";
      output += *value;
    }
    return output;
  }
  if (path == "/SELECT") {
    if (auto value = argString(0)) {
      return "SELECT " + *value;
    }
    return std::nullopt;
  }
  if (path == "/SELECTID" || path == "/CUEID") {
    if (auto value = argString(0)) {
      return "SELECTID " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TAKE") {
    if (auto value = argString(0)) {
      return "TAKE " + *value;
    }
    return "TAKE";
  }
  if (path == "/TAKEID") {
    if (auto value = argString(0)) {
      return "TAKEID " + *value;
    }
    return std::nullopt;
  }
  if (path == "/GOTO") {
    if (auto value = argString(0)) {
      return "GOTO " + *value;
    }
    return std::nullopt;
  }
  if (path == "/FIND" || path == "/CUE/FIND") {
    if (auto value = argString(0)) {
      return "FIND " + *value;
    }
    return "FIND";
  }
  if (path == "/FIND/NEXT" || path == "/CUE/FIND/NEXT") {
    return "FINDNEXT";
  }
  if (path == "/FIND/PREV" || path == "/CUE/FIND/PREV") {
    return "FINDPREV";
  }
  if (path == "/FIND/TAKE" || path == "/CUE/FIND/TAKE") {
    if (auto value = argString(0)) {
      return "FINDTAKE " + *value;
    }
    return "FINDTAKE";
  }
  if (path == "/FIND/CLEAR" || path == "/CUE/FIND/CLEAR") {
    return "FINDCLEAR";
  }
  if (path == "/RENUMBER" || path == "/CUE/RENUMBER" || path == "/CUE/AUTOID") {
    if (message.args.empty()) {
      return "RENUMBER";
    }
    std::string output = "RENUMBER";
    for (size_t i = 0; i < message.args.size(); ++i) {
      if (auto value = argString(i)) {
        output += " " + *value;
      }
    }
    return output;
  }
  if (path == "/DECK") {
    if (auto value = argString(0)) {
      return "DECK " + *value;
    }
    return std::nullopt;
  }
  if (path == "/DECK/OPACITY" || path == "/PLAYLIST/OPACITY") {
    if (auto value = argString(0)) {
      return "DECKOPACITY " + *value;
    }
    return "DECKOPACITY";
  }
  if (path == "/DECK/AUTOFADE" || path == "/PLAYLIST/AUTOFADE") {
    if (auto value = argToggleWord(0)) {
      return "DECKAUTOFADE " + *value;
    }
    return "DECKAUTOFADE";
  }
  if (path == "/DECK/FADE" || path == "/PLAYLIST/FADE") {
    if (auto value = argString(0)) {
      return "DECKFADE " + *value;
    }
    return "DECKFADE";
  }
  if (path == "/OSCQUERY" || path == "/DECKBOY/OSCQUERY") {
    if (auto value = argToggleWord(0)) {
      return "OSCQUERY " + *value;
    }
    return "OSCQUERY";
  }
  if (path == "/OSCQUERY/PORT" || path == "/DECKBOY/OSCQUERY/PORT") {
    if (auto value = argString(0)) {
      return "OSCQUERYPORT " + *value;
    }
    return "OSCQUERYPORT";
  }
  if (path == "/OSC/FEEDBACK" || path == "/OSCFEEDBACK" || path == "/DECKBOY/OSCFEEDBACK") {
    if (auto value = argToggleWord(0)) {
      return "OSCFEEDBACK " + *value;
    }
    return "OSCFEEDBACK";
  }
  if (path == "/OSC/FEEDBACK/RATE" || path == "/OSCFEEDBACK/RATE" || path == "/DECKBOY/OSCFEEDBACK/RATE") {
    if (auto value = argString(0)) {
      return "OSCFEEDBACKRATE " + *value;
    }
    return "OSCFEEDBACKRATE";
  }
  if (path == "/ATEM" || path == "/DECKBOY/ATEM") {
    if (auto value = argToggleWord(0)) {
      return "ATEM " + *value;
    }
    return "ATEM";
  }
  if (path == "/NDI/TRIGGER" || path == "/NDITRIGGER" || path == "/DECKBOY/NDITRIGGER") {
    if (auto value = argToggleWord(0)) {
      return "NDITRIGGER " + *value;
    }
    return "NDITRIGGER";
  }
  if (path == "/NMC" || path == "/DECKBOY/NMC") {
    if (auto value = argToggleWord(0)) {
      return "NMC " + *value;
    }
    return "NMC";
  }
  if (path == "/MTC" || path == "/DECKBOY/MTC") {
    if (auto value = argToggleWord(0)) {
      return "MTC " + *value;
    }
    return "MTC";
  }
  if (path == "/LTC" || path == "/DECKBOY/LTC") {
    if (auto value = argToggleWord(0)) {
      return "LTC " + *value;
    }
    return "LTC";
  }
  if (path == "/ARTNET" || path == "/DMX/ARTNET" || path == "/DECKBOY/ARTNET") {
    if (auto value = argToggleWord(0)) {
      return "ARTNET " + *value;
    }
    return "ARTNET";
  }
  if (path == "/ARTNET/PORT" || path == "/DECKBOY/ARTNET/PORT") {
    if (auto value = argString(0)) {
      return "ARTNETPORT " + *value;
    }
    return "ARTNETPORT";
  }
  if (path == "/INTEGRATION" || path == "/INTEGRATIONS" || path == "/DECKBOY/INTEGRATION") {
    if (auto value = argString(0)) {
      return "INTEGRATIONS " + *value;
    }
    return "INTEGRATIONS";
  }
  if (path == "/ROUTE") {
    if (auto value = argString(0)) {
      return "ROUTE " + *value;
    }
    return "ROUTE";
  }
  if (path == "/LAYER") {
    if (auto value = argString(0)) {
      return "LAYER " + *value;
    }
    return "LAYER";
  }
  if (path == "/DECK/NEXT") {
    return "DECKNEXT";
  }
  if (path == "/DECK/PREV") {
    return "DECKPREV";
  }
  if (path == "/VOLUME") {
    if (auto value = argString(0)) {
      return "VOLUME " + *value;
    }
    return std::nullopt;
  }
  if (path == "/CUE/ID" || path == "/CUE/SHORTID") {
    if (auto value = argString(0)) {
      return "CUEIDSHORT " + *value;
    }
    return "CUEIDSHORT";
  }
  if (path == "/CUE/AUDIO") {
    if (auto value = argToggleWord(0)) {
      return "CUEAUDIO " + *value;
    }
    return "CUEAUDIO";
  }
  if (path == "/CUE/PAUSEBEGIN") {
    if (auto value = argToggleWord(0)) {
      return "PAUSEBEGIN " + *value;
    }
    return "PAUSEBEGIN";
  }
  if (path == "/CUE/PAUSEEND") {
    if (auto value = argToggleWord(0)) {
      return "PAUSEEND " + *value;
    }
    return "PAUSEEND";
  }
  if (path == "/CUE/NEXTTRANS") {
    if (auto value = argToggleWord(0)) {
      return "NEXTTRANS " + *value;
    }
    return "NEXTTRANS";
  }
  if (path == "/CUE/GOTO") {
    if (auto value = argString(0)) {
      return "CUEGOTO " + *value;
    }
    return "CUEGOTO";
  }
  if (path == "/SEEK") {
    if (auto value = argString(0)) {
      return "SEEK " + *value;
    }
    return std::nullopt;
  }
  if (path == "/AUTONEXT") {
    if (auto value = argToggleWord(0)) {
      return "AUTONEXT " + *value;
    }
    return std::nullopt;
  }
  if (path == "/PLAYLISTLOOP") {
    if (auto value = argToggleWord(0)) {
      return "PLAYLISTLOOP " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TRANSITION") {
    if (auto value = argString(0)) {
      return "TRANSITION " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TRANSITION/STYLE") {
    if (auto value = argString(0)) {
      return "TRANSITION STYLE " + *value;
    }
    return std::nullopt;
  }
  if (path == "/OVERLAY" || path == "/TIMEOVERLAY") {
    if (auto value = argToggleWord(0)) {
      return "OVERLAY " + *value;
    }
    return "OVERLAY";
  }
  if (path == "/NDI") {
    if (auto value = argToggleWord(0)) {
      return "NDI " + *value;
    }
    return "NDI";
  }
  if (path == "/NDI/NAME") {
    if (auto value = argString(0)) {
      return "NDI NAME " + *value;
    }
    return std::nullopt;
  }
  if (path == "/SOURCE") {
    if (auto value = argString(0)) {
      return "SOURCE " + *value;
    }
    return "SOURCE";
  }
  if (path == "/SOURCE/WINDOW") {
    if (auto value = argString(0)) {
      return "SOURCE WINDOW " + *value;
    }
    return "SOURCE WINDOW";
  }
  if (path == "/SOURCE/CAMERA") {
    if (auto value = argString(0)) {
      return "SOURCE CAMERA " + *value;
    }
    return "SOURCE CAMERA";
  }
  if (path == "/SOURCE/SYPHON" || path == "/SOURCE/SPOUT") {
    if (auto value = argString(0)) {
      return "SOURCE SYPHON " + *value;
    }
    return "SOURCE SYPHON";
  }
  if (path == "/IN") {
    if (auto value = argString(0)) {
      return "IN " + *value;
    }
    return std::nullopt;
  }
  if (path == "/OUT") {
    if (auto value = argString(0)) {
      return "OUT " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TRIM/CLEAR") {
    return "TRIM CLEAR";
  }
  if (path == "/TIMECODE") {
    if (auto value = argString(0)) {
      return "TIMECODE " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/CHASE") {
    if (auto value = argToggleWord(0)) {
      return "TIMECODE CHASE " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/RUN") {
    if (auto value = argToggleWord(0)) {
      return "TIMECODE RUN " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/FPS") {
    if (auto value = argString(0)) {
      return "TIMECODE FPS " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/JAM") {
    if (auto value = argToggleWord(0)) {
      return "TIMECODE JAM " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/FREEWHEEL") {
    if (auto value = argString(0)) {
      return "TIMECODE FREEWHEEL " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/MARK") {
    if (auto value = argString(0)) {
      return "TCMARK " + *value;
    }
    return std::nullopt;
  }

  return std::nullopt;
}

std::string normalizeBrowserUrl(std::string value) {
  value = trim(value);
  if (value.empty()) {
    return value;
  }
  std::error_code error;
  fs::path localPath(value);
  if (fs::exists(localPath, error)) {
    return "file://" + fs::absolute(localPath, error).string();
  }
  if (value.find("://") != std::string::npos) {
    return value;
  }
  if (value.rfind("about:", 0) == 0 || value.rfind("file:", 0) == 0 || value.rfind("data:", 0) == 0) {
    return value;
  }
  return "https://" + value;
}

std::string browserCueNameForUrl(const std::string& url) {
  std::string value = normalizeBrowserUrl(url);
  size_t scheme = value.find("://");
  size_t begin = scheme == std::string::npos ? 0 : scheme + 3;
  size_t end = value.find('/', begin);
  std::string host = value.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
  if (host.empty()) {
    return "Browser Cue";
  }
  return "Browser: " + host;
}

#ifndef _WIN32
bool executableOnPath(const std::string& name) {
  if (name.empty()) {
    return false;
  }
  if (name.find('/') != std::string::npos) {
    return access(name.c_str(), X_OK) == 0;
  }

  const char* pathEnv = std::getenv("PATH");
  if (!pathEnv) {
    return false;
  }

  for (const auto& part : splitByChar(pathEnv, ':')) {
    if (part.empty()) {
      continue;
    }
    fs::path candidate = fs::path(part) / name;
    if (access(candidate.c_str(), X_OK) == 0) {
      return true;
    }
  }
  return false;
}
#endif

// NdiApi, LtcApi, NdiTriggerApi → platform/*.hpp

// MediaEngine is now defined in engine/media_engine.hpp

static std::string browserPhaseLabel(BrowserStartPhase phase) {
  switch (phase) {
    case BrowserStartPhase::None: return "idle";
    case BrowserStartPhase::WaitXvfb:
#if defined(_WIN32) && defined(DECKBOY_HAS_WEBVIEW)
      return "initializing webview";
#else
      return "starting xvfb";
#endif
    case BrowserStartPhase::WaitChrome: return "starting browser";
    case BrowserStartPhase::WaitCapture: return "starting capture";
    case BrowserStartPhase::Live: return "live";
  }
  return "idle";
}

static std::string browserCueStatusSummary(BrowserStartPhase phase, bool live, const std::string& lastError) {
  if (!lastError.empty()) {
    return "failed: " + lastError;
  }
  if (live || phase == BrowserStartPhase::Live) {
    return "live";
  }
  return browserPhaseLabel(phase);
}

// ── Output runtime structs ──────────────────────────────────────────────────
// These structs track the state of each output destination (window, stream,
// NDI, DeckLink). Each output has its own SDL window/renderer, compositor
// texture, and optional stream writer thread.

// Health state machine for output windows and streams.
enum class OutputHealthState {
  Off,           // Output not enabled
  Armed,         // Enabled but not yet rendering (waiting for first frame)
  Live,          // Actively rendering frames
  Recovering,    // Recovering from an error (auto-restart)
  Error,         // Failed — requires manual intervention
};

// One captured frame + audio chunk queued for the stream writer thread.
struct OutputStreamPacket {
  int width = 0;
  int height = 0;
  Uint64 capturedAtMs = 0;                   // SDL tick when frame was captured
  // SHARED, not copied. This was a plain vector, so handing a frame to the
  // writer copied the whole raster on the render thread -- 33MB at 4K, every
  // frame, and again for every frame the CFR pacer repeats. The writer only
  // ever reads it, so one immutable buffer can be handed to as many packets as
  // the pacer emits.
  std::shared_ptr<const std::vector<std::uint8_t>> videoBytes;
  std::vector<std::int16_t> audioSamples;     // Interleaved 16-bit PCM
};

// Thread-safe state for the ffmpeg stream writer background thread.
// The main thread pushes OutputStreamPackets via the condition variable;
// the writer thread pops them and pipes to ffmpeg's stdin.
struct OutputStreamWriterState {
  std::mutex mutex;
  std::condition_variable cv;
  std::thread thread;
  int videoPipeFd = -1;             // Pipe fd to ffmpeg video stdin
  // Atomic: on Windows this is assigned by the named-pipe connect thread
  // AFTER the writer starts, because ffmpeg only opens its end once running.
  std::atomic<int> audioPipeFd {-1};   // Pipe fd to ffmpeg audio input
  bool stop = false;                // Signal the writer thread to exit
  bool failed = false;              // Writer encountered a fatal error
  std::string failureReason;
  // A QUEUE, not a mailbox. This was a single pendingPacket slot, so a second
  // frame pushed before the writer drained the first SILENTLY REPLACED it --
  // while the pacer counted both as written. That is why a recording ran short
  // whenever capture outpaced the writer, and why the two attempts at filling
  // the cadence with repeats did nothing: every repeat landed in the same slot
  // and only one was ever written.
  //
  // Bounded, because the alternative to dropping under sustained overload is
  // growing without limit during a show. Depth is in FRAMES and deliberately
  // small: at 4K a frame is 33MB, and a deep queue would mean a recording that
  // lags seconds behind the programme before anyone notices.
  static constexpr std::size_t kMaxQueuedPackets = 8;
  std::deque<OutputStreamPacket> queue;
  std::uint64_t packetsDropped = 0;  // queue was full: a REAL lost frame
  // Audio rides its OWN thread and mailbox. Both pipes used to be fed from one
  // thread, audio first and then a BLOCKING video write -- which deadlocks: the
  // mp4 muxer will not drain video until it has audio covering the same
  // timestamps, so the video write blocked, which stopped the only thread that
  // could have supplied that audio. MEASURED: every recording froze after
  // exactly 170 frames with ffmpeg reporting frame=0, and the same ffmpeg
  // command driven by hand ran fine. Two pipes drained in an order the reader
  // chooses need two writers.
  std::mutex audioMutex;
  std::condition_variable audioCv;
  std::thread audioThread;
  std::vector<std::int16_t> pendingAudio;
  std::uint64_t packetsQueued = 0;  // Total packets queued by main thread
  std::uint64_t packetsWritten = 0; // Total packets written by writer thread
  std::uint64_t videoBytesWritten = 0;
  std::uint64_t audioBytesWritten = 0;
};

// Per-output runtime state: SDL window/renderer, compositor, stream writer,
// NDI sender, DeckLink output, and FPS telemetry.
struct OutputRuntime {
#ifdef _WIN32
  // Server end of the audio named pipe. Windows children get one piped
  // stdin and video already uses it, so audio needs its own channel.
  void* streamAudioPipeHandle = nullptr;
#endif
  // Pixel buffer snapshot for streaming/NDI/DeckLink sinks.
  struct CapturedFrame {
    int width = 0;
    int height = 0;
    Uint64 capturedAtMs = 0;
    std::vector<std::uint8_t> pixels;    // RGBA pixel data
  };

  // SDL output window and renderer (one per output destination)
  SDL_Window* outputWindow = nullptr;
  SDL_Renderer* outputRenderer = nullptr;
  // One-shot latch: while an output is disabled we paint its still-visible
  // window black exactly once (not every frame — presenting is vsync-blocking).
  // Reset whenever the output renders again, so re-disabling re-blacks it.
  bool blackedWhileDisabled = false;
  SDL_Texture* compositorTexture = nullptr;  // Offscreen compositor target
  // Scratch target at the RECORDING raster. The composite is blitted into this
  // on the GPU and this is what gets read back, so a 1080 recording off a 4K
  // programme moves a quarter of the bytes across the bus.
  SDL_Texture* egressScaleTexture = nullptr;
  int egressScaleW = 0;
  int egressScaleH = 0;
  // Staging ring for the asynchronous readback (see gpu_readback.hpp). Null on
  // a non-D3D11 renderer, where the path falls back to SDL_RenderReadPixels.
  void* egressReadback = nullptr;
  int egressReadbackW = 0;
  int egressReadbackH = 0;
  // Latched when the renderer turns out to have no asynchronous readback, so
  // the creation is not retried on the render thread every single frame.
  bool egressReadbackUnavailable = false;
  // The captured picture, published once as an immutable buffer. The CFR pacer
  // repeats the last picture to cover a gap, and every repeat used to copy the
  // whole raster again -- 33MB at 4K, on the render thread, for pixels that had
  // not changed.
  std::shared_ptr<const std::vector<std::uint8_t>> egressPublished;
  Uint64 egressPublishedAtMs = 0;
  // Which capture path this output's recording is on, so the choice is
  // reportable rather than deduced. -1 = not yet logged.
  int egressPathLogged = -1;
  // CFR pacer for a file recording. A broadcast deliverable must contain
  // exactly rate x elapsed frames; the encoder stamps by ARRIVAL ORDER at the
  // declared rate, so delivering fewer frames than promised does not slow the
  // file down, it SHORTENS it. Counting what is owed and repeating the last
  // frame to cover a gap makes the duration correct by construction rather
  // than dependent on the capture keeping up.
  Uint64 recordPacerStartMs = 0;
  std::uint64_t recordFramesWritten = 0;
  // Frames written across the WHOLE take, surviving segment rolls, so each
  // segment's timecode continues where the previous one stopped rather than
  // restarting at the take's start value.
  std::uint64_t recordTakeFrames = 0;
  // Frames carrying a NEW picture, as opposed to frames delivered. The pacer
  // repeats the last picture to keep the duration right, and those repeats
  // must not be allowed to satisfy the dropped-frame alarm -- otherwise a
  // capture that has stalled completely produces a duration-correct file of
  // one still image and reports itself healthy.
  std::uint64_t recordFreshFrames = 0;
  Uint64 lastFreshCaptureMs = 0;     // when the picture last actually changed
  Uint64 lastSegmentSizeCheckMs = 0;   // segment size is stat'd ~1Hz, not per frame
  Uint64 lastDropWarnMs = 0;           // dropped-frame alarm, rate limited to 1Hz
  std::uint64_t recordDroppedFrames = 0;
  int compositorWidth = 0;
  int compositorHeight = 0;
  Uint32 compositorFormat = SDL_PIXELFORMAT_UNKNOWN;
  int compositorBitDepth = 8;
  // Per-deck bridge texture for compositing the source frame at this output.
  // Format tracks SDL_PixelFormat so an RGBA→NV12 (or vice-versa) cue
  // switch rebuilds the texture instead of silently corrupting its sampler.
  std::map<int, SDL_Texture*> layerBridgeTextures;
  std::map<int, int> layerBridgeTextureWidths;
  std::map<int, int> layerBridgeTextureHeights;
  std::map<int, Uint32> layerBridgeTextureFormats;
  std::map<int, std::uint64_t> layerBridgeFrameIndices;
  std::map<int, std::string> layerBridgeCueKeys;
  // Per-overlay bridge texture (same rationale, keyed by overlay identity).
  std::map<std::string, SDL_Texture*> overlayBridgeTextures;
  std::map<std::string, int> overlayBridgeTextureWidths;
  std::map<std::string, int> overlayBridgeTextureHeights;
  std::map<std::string, Uint32> overlayBridgeTextureFormats;
  std::map<std::string, std::uint64_t> overlayBridgeFrameIndices;
  std::map<std::string, std::string> overlayBridgeCueKeys;
  std::vector<std::uint8_t> layerBridgeScratchPixels;
#if DECKBOY_INPROC_DECODE
  // Zero-copy compositing (in-process d3d11va decode): per-deck persistent
  // NV12 D3D11 texture wrapped as an SDL_Texture. Decoded texture-array
  // slices are GPU-copied into it — the frame never touches the CPU. Only
  // used when the frame's decode device IS this renderer's device; other
  // devices (secondary outputs) fall back to a CPU download into the
  // classic bridge texture.
  void* rendererD3DDevice = nullptr;                 // cached ID3D11Device*
  std::map<int, SDL_Texture*> layerGpuTextures;      // wrapped SDL textures
  std::map<int, void*> layerGpuTexture2Ds;           // backing ID3D11Texture2D*
  std::map<int, std::pair<int, int>> layerGpuTextureSizes;
  std::map<int, std::uint64_t> layerGpuFrameIndices;
  DecodedFrame gpuDownloadScratch;                   // device-mismatch fallback
  int gpuDownloadScratchDeck = -1;                   // deck the scratch holds
#endif
#ifdef _WIN32
  ChildProcess streamProcess;         // Windows: ffmpeg subprocess with stdin pipe
#else
  pid_t streamPid = -1;
  int streamPipeFd = -1;
  int streamAudioPipeFd = -1;
  std::string streamVideoPipePath;
#endif
  std::map<int, std::uint64_t> streamAudioReadSamplesByDeck;
  double streamAudioSampleRemainder = 0.0;
  std::map<int, std::uint64_t> ndiAudioReadSamplesByDeck;
  double ndiAudioSampleRemainder = 0.0;
  std::string streamSpec;
  std::string streamCommand;
  std::vector<std::uint8_t> streamFrameBuffer;
  int streamFrameWidth = 0;
  int streamFrameHeight = 0;
  bool streamStartFailed = false;
  // A file sink has to FINALIZE — flush the muxer and write its trailer. A
  // network sink has nothing to finalize, so the two get different shutdown
  // budgets; see stopOutputStreamRuntime.
  bool streamToFile = false;
  Uint64 streamRestartBlockedUntilMs = 0;
  std::shared_ptr<OutputStreamWriterState> streamWriter;
  CapturedFrame latestCapturedFrame;
  std::deque<CapturedFrame> delayFrames;
  Uint64 lastEgressCaptureAtMs = 0;
  Uint64 lastStreamCaptureSentAtMs = 0;
#if defined(DECKBOY_HAS_NDI_SDK)
  NDIlib_send_instance_t ndiSender = nullptr;
  std::string ndiSenderName;
  std::vector<std::uint8_t> ndiFrameBuffer;
  NDIlib_send_instance_t ndiKeySender = nullptr;
  std::string ndiKeySenderName;
  std::vector<std::uint8_t> ndiKeyFrameBuffer;
#endif
#if defined(DECKBOY_HAS_DECKLINK)
  std::unique_ptr<deckboy::platform::video::DeckLinkOutput> deckLinkOutput;
  std::vector<std::uint8_t> deckLinkFrameBuffer;
#endif
#if defined(DECKBOY_HAS_SPOUT)
  std::unique_ptr<deckboy::platform::video::SiphonSpoutSender> spoutSender;
#endif
  // ST 2110-20 sender. No SDK and no platform guard — plain sockets, so this
  // exists on every build.
  std::unique_ptr<deckboy::platform::video::St2110Output> st2110Sender;
  std::unique_ptr<deckboy::platform::video::St2110AudioOutput> st2110AudioSender;
  // Program-monitor tap: a small copy of this output's finished composite,
  // sampled on the output's own render pass so the control-window preview
  // advances in lockstep with what actually leaves the machine instead of
  // trailing it. Small on purpose — the readback cost scales with area.
  SDL_Texture* previewTapTexture = nullptr;
  int previewTapTextureW = 0;
  int previewTapTextureH = 0;
  std::vector<std::uint8_t> previewTapPixels;  // RGBA32
  int previewTapW = 0;
  int previewTapH = 0;
  std::uint64_t previewTapSerial = 0;  // bumped per successful tap; 0 = nothing captured
  bool recoveryPausedByEscape = false;
  bool fullscreenIntended = false;  // user explicitly wants fullscreen — re-assert if dropped
  // The display this output is pinned to (by name) is not currently attached.
  // Parks recovery instead of slamming the program feed fullscreen onto
  // whatever monitor inherited the index — unplugging a projector must not
  // take over the operator's control screen. Cleared when the panel returns.
  bool awaitingDisplayReturn = false;
  Uint64 lastFullscreenRequestMs = 0;
  Uint64 lastRecoveryAttemptMs = 0;
  bool pendingDisplayRuntimeRebuild = false;
  bool pendingDisplayMoveFullscreen = false;
  Uint64 displayMoveRetryAtMs = 0;
  Uint64 suppressRecoveryUntilMs = 0;
  // Recovery strike backoff: if recovery keeps firing for the same output,
  // something structural is wrong (SDL and the WM disagree about placement).
  // Repeated exit-fullscreen/move/re-enter/raise cycles steal keyboard focus
  // from the control window every pass — the operator experiences a fight.
  // After kMaxRecoveryStrikes within the strike window, recovery pauses.
  int recoveryStrikeCount = 0;
  Uint64 recoveryStrikeWindowStartMs = 0;
  OutputHealthState healthState = OutputHealthState::Off;
  std::string healthReason;
  Uint64 healthUpdatedAtMs = 0;
  Uint64 fpsSampleStartedAtMs = 0;
  Uint32 fpsFrameCount = 0;
  double fpsMeasured = 0.0;
  Uint64 streamFpsSampleStartedAtMs = 0;
  std::uint64_t streamFpsPacketsAtSampleStart = 0;
  double streamFpsMeasured = 0.0;
};

// Audio buffer for streaming: accumulates PCM samples from the deck's
// audio callback and packages them into stream packets.
struct DeckStreamAudioBuffer {
  std::vector<std::int16_t> samples;
  std::uint64_t droppedSamples = 0;     // Samples dropped due to buffer overflow
};

// Encode presets. Each builds its own ffmpeg args; the queue tries them in
// order and keeps the first that produces a file, so a GPU-first preset can
// fall back to libx264 on machines without NVENC.
//
// File scope, not a class member: the .ipp files are included into the class
// body ABOVE where members are declared, and a parameter type has to be
// visible at declaration (function bodies are a complete-class context,
// parameter lists are not).
// One row of the encode format matrix. The catalog below is curated (ffmpeg
// exposes ~200 encoders, most meaningless for a cue deck) but AVAILABILITY is
// probed from the local ffmpeg at runtime, never assumed: a portable build, a
// distro package and a Homebrew build all ship different encoder sets, and a
// format that is offered but cannot run just fails silently into "conversion
// failed" with no explanation.
struct EncoderFormat {
  const char* id;          // stable token, used by the remote verb
  const char* label;       // what the operator sees
  const char* encoder;     // ffmpeg -c:v name, "" for audio-only formats
  const char* audioEncoder;// ffmpeg -c:a name
  const char* container;   // output extension, without the dot
  const char* note;        // one line of why you would pick it
  bool alpha;              // carries an alpha channel
};

// Live state of one Timer cue. File scope for the same reason EncoderPreset is:
// the .ipp files are included into the class body above where members are
// declared, so a type used as a RETURN or PARAMETER type there must already be
// visible (function bodies are a complete-class context; signatures are not).
struct TimerRuntime {
  bool running = false;
  double elapsedSeconds = 0.0;
  Uint64 lastTickMs = 0;
  // Highest chime already sounded: 0 none, 1 amber, 2 red, 3 zero. Latched so
  // a chime fires ONCE on crossing rather than every frame it is past the
  // threshold, and resets when the clock is reset or wound back.
  int chimedStage = 0;
};

// MIDI note number to frequency under a chosen tuning. Note 69 is A above
// middle C and is the reference in every system here, so they all agree on
// that one pitch and diverge from it.
inline double synthNoteToHz(int midiNote, SynthTuning tuning, double referenceHz) {
  const double ref = (referenceHz > 0.0) ? referenceHz : 440.0;
  const int rel = midiNote - 69;
  switch (tuning) {
    case SynthTuning::Equal19:
      // Nineteen steps to the octave. The keyboard still sends twelve per
      // octave, so this stretches those twelve across the 19-step grid rather
      // than pretending extra keys exist.
      return ref * std::pow(2.0, (rel * 19.0 / 12.0) / 19.0);
    case SynthTuning::Equal24:
      return ref * std::pow(2.0, (rel * 24.0 / 12.0) / 24.0);
    case SynthTuning::BohlenPierce:
      // Thirteen equal divisions of 3:1 rather than 2:1. There is no octave in
      // this system at all, which is the point.
      return ref * std::pow(3.0, rel / 13.0);
    case SynthTuning::Just: {
      // 5-limit just intonation. Pure ratios inside the octave, transposed by
      // whole octaves outside it.
      static const double kRatios[12] = {
        1.0, 16.0/15, 9.0/8, 6.0/5, 5.0/4, 4.0/3,
        45.0/32, 3.0/2, 8.0/5, 5.0/3, 9.0/5, 15.0/8
      };
      const int oct = static_cast<int>(std::floor(rel / 12.0));
      const int step = ((rel % 12) + 12) % 12;
      return ref * kRatios[step] * std::pow(2.0, oct);
    }
    case SynthTuning::Pythagorean: {
      // Built from stacked perfect fifths, so fifths are exact and thirds are
      // wide and bright.
      static const double kRatios[12] = {
        1.0, 256.0/243, 9.0/8, 32.0/27, 81.0/64, 4.0/3,
        729.0/512, 3.0/2, 128.0/81, 27.0/16, 16.0/9, 243.0/128
      };
      const int oct = static_cast<int>(std::floor(rel / 12.0));
      const int step = ((rel % 12) + 12) % 12;
      return ref * kRatios[step] * std::pow(2.0, oct);
    }
    case SynthTuning::Meantone: {
      // Quarter-comma meantone: fifths narrowed so that thirds come out pure.
      static const double kCents[12] = {
        0.0, 76.0, 193.2, 310.3, 386.3, 503.4,
        579.5, 696.6, 772.6, 889.7, 1006.8, 1082.9
      };
      const int oct = static_cast<int>(std::floor(rel / 12.0));
      const int step = ((rel % 12) + 12) % 12;
      return ref * std::pow(2.0, kCents[step] / 1200.0) * std::pow(2.0, oct);
    }
    default:
      return ref * std::pow(2.0, rel / 12.0);
  }
}

// Ableton's layout: the home row is the white keys and the row above holds the
// sharps where they physically sit on a piano. Anyone who has used a tracker
// or a DAW already knows it, so it needs no explanation.
inline int synthKeyToSemitone(SDL_Keycode key) {
  switch (key) {
    case SDLK_A: return 0;   case SDLK_W: return 1;
    case SDLK_S: return 2;   case SDLK_E: return 3;
    case SDLK_D: return 4;   case SDLK_F: return 5;
    case SDLK_T: return 6;   case SDLK_G: return 7;
    case SDLK_Y: return 8;   case SDLK_H: return 9;
    case SDLK_U: return 10;  case SDLK_J: return 11;
    case SDLK_K: return 12;  case SDLK_O: return 13;
    case SDLK_L: return 14;
    default: return -1;
  }
}

enum class EncoderPreset {
  DeliveryH264,      // the historical behaviour: NVENC, libx264 fallback
  Proxy,             // 720p, fast, for scrubbing on weak machines
  MatchSource,       // same raster, high quality, CPU only
  DatamoshFriendly,  // no B-frames, no scene-cut keyframes, single reference
};

// Operator overrides for a conversion. Everything here defaults to "leave it
// alone", so the encoder behaves exactly as before until something is changed.
//
// File scope for the same reason EncoderPreset is: the .ipp files are included
// into the class body above where members are declared, so a type used as a
// RETURN or PARAMETER type there must already be visible.
struct EncoderOverrides {
  // There is no universal quality knob -- x264/x265 take -crf, NVENC -cq,
  // MPEG-4/MJPEG -qscale:v, VP9/AV1 -crf alongside -b:v 0, and ProRes/DNxHR
  // only take profiles. So the operator sets an intent and each codec maps it
  // onto whatever it actually has; a single "CRF" field would be a lie on
  // more than half the catalogue.
  enum class Rate {
    Auto,     // the per-format default
    Quality,  // constant quality, mapped per codec from quality0to100
    Bitrate,  // constant bitrate, -b:v
  };
  Rate rate = Rate::Auto;
  int quality0to100 = 65;        // higher = better picture, bigger file
  int videoBitrateKbps = 8000;
  int audioBitrateKbps = 0;      // 0 = the format's own default
  double fps = 0.0;              // 0 = keep the source rate
  int width = 0;                 // 0 = keep the source raster
  int height = 0;
  std::string outputDir;         // empty = _converted beside the show

  bool touchesVideoRate() const { return rate != Rate::Auto; }
};

// Per-deck runtime state: the playback engine, audio device, and browser renderer.
// Each deck has its own MediaEngine instance that handles video/audio decode
// and frame upload independently.
struct DeckRuntime {
  SDL_Window* outputWindow = nullptr;    // Legacy (unused — outputs moved to OutputRuntime)
  SDL_Renderer* outputRenderer = nullptr;
  SDL_AudioStream* audioStream = nullptr;  // device-bound SDL3 stream for this deck's audio output
  std::unique_ptr<MediaEngine> mediaEngine;   // Core playback engine
  std::unique_ptr<deckboy::platform::browser::BrowserRenderer> browserRenderer;  // For Browser/LowerThird cues
  bool browserCueLive = false;           // Whether a browser cue is currently active
};

// Runtime state for a PIP (Picture-in-Picture) overlay layer.
// Each PIP has its own MediaEngine for independent playback.
struct PipOverlayRuntime {
  std::unique_ptr<MediaEngine> mediaEngine;
  std::string loadedCueKey;              // Key of the currently loaded cue
  int targetCueIndex = -1;              // Index into the cue list
  Cue resolvedCue;                       // Resolved cue data (after alias/reference resolution)
  bool resolvedCueValid = false;
};

// spawnDetachedProcess is now provided by native/core/subprocess.hpp/cpp
// readExact is now provided by native/core/io_utils.hpp

// ── Project file serialization helpers ──────────────────────────────────────
// .deckboy project files use tab-delimited fields. These helpers escape/unescape
// special characters (backslash, tab, newline) so field values can contain
// arbitrary text without breaking the delimiter format.

// Markers serialize as comma-separated lists in two parallel fields. Comma
// rather than tab because tab is the record separator; the names field is
// escaped like any other string.
std::string joinMarkerTimes(const Cue& cue) {
  std::string out;
  for (double t : cue.markerSeconds) {
    if (!out.empty()) out += ",";
    out += std::to_string(t);
  }
  return out;
}

std::string joinMarkerNames(const Cue& cue) {
  std::string out;
  for (const std::string& n : cue.markerNames) {
    if (!out.empty()) out += ",";
    std::string safe = n;
    std::replace(safe.begin(), safe.end(), ',', ';');   // commas are our separator
    out += safe;
  }
  return out;
}

void parseMarkerTimes(Cue& cue, const std::string& field) {
  cue.markerSeconds.clear();
  if (field.empty()) return;
  std::size_t start = 0;
  while (start <= field.size()) {
    std::size_t comma = field.find(',', start);
    std::string tok = field.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!tok.empty()) {
      try { cue.markerSeconds.push_back(std::stod(tok)); } catch (...) {}
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  std::sort(cue.markerSeconds.begin(), cue.markerSeconds.end());
}

void parseMarkerNames(Cue& cue, const std::string& field) {
  cue.markerNames.clear();
  std::size_t start = 0;
  while (start <= field.size() && !field.empty()) {
    std::size_t comma = field.find(',', start);
    cue.markerNames.push_back(
      field.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  // Names must stay parallel to times or the pairing silently shifts.
  cue.markerNames.resize(cue.markerSeconds.size());
}

// Effect stack <-> one tab field. Colons inside an entry, pipes between them,
// so a stack of any length occupies a single column.
std::string serializeCueEffects(const std::vector<deckboy::effects::CueEffect>& stack) {
  std::string out;
  for (const auto& fx : stack) {
    if (fx.kind == deckboy::effects::CueEffectKind::None) continue;
    if (!out.empty()) out += '|';
    std::ostringstream one;
    // C and D go AFTER bypassed, not between B and it. Appending keeps every
    // show ever saved readable, and keeps a show saved here readable by a build
    // that predates them -- it just ignores the two extra fields.
    one << deckboy::effects::cueEffectToken(fx.kind) << ':' << fx.amount
        << ':' << fx.paramA << ':' << fx.paramB << ':' << (fx.bypassed ? 1 : 0)
        << ':' << fx.paramC << ':' << fx.paramD;
    out += one.str();
  }
  return out;
}

std::vector<deckboy::effects::CueEffect> parseCueEffects(const std::string& text) {
  std::vector<deckboy::effects::CueEffect> stack;
  if (text.empty()) return stack;
  std::size_t start = 0;
  for (;;) {
    const std::size_t bar = text.find('|', start);
    const std::string item =
      text.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
    if (!item.empty()) {
      std::vector<std::string> parts;
      std::size_t p0 = 0;
      for (;;) {
        const std::size_t colon = item.find(':', p0);
        parts.push_back(
          item.substr(p0, colon == std::string::npos ? std::string::npos : colon - p0));
        if (colon == std::string::npos) break;
        p0 = colon + 1;
      }
      deckboy::effects::CueEffect fx;
      fx.kind = deckboy::effects::cueEffectFromToken(parts[0]);
      // An unknown token means a show saved by a NEWER build. Skipping it
      // keeps the rest of the stack rather than refusing the whole cue.
      if (fx.kind != deckboy::effects::CueEffectKind::None) {
        if (parts.size() > 1) fx.amount = static_cast<float>(std::atof(parts[1].c_str()));
        if (parts.size() > 2) fx.paramA = static_cast<float>(std::atof(parts[2].c_str()));
        if (parts.size() > 3) fx.paramB = static_cast<float>(std::atof(parts[3].c_str()));
        if (parts.size() > 4) fx.bypassed = std::atoi(parts[4].c_str()) != 0;
        if (parts.size() > 5) fx.paramC = static_cast<float>(std::atof(parts[5].c_str()));
        if (parts.size() > 6) fx.paramD = static_cast<float>(std::atof(parts[6].c_str()));
        stack.push_back(fx);
      }
    }
    if (bar == std::string::npos) break;
    start = bar + 1;
  }
  return stack;
}

std::string escapeField(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (ch == '\\' || ch == '\t' || ch == '\n') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

std::string unescapeField(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  bool escaping = false;
  for (char ch : value) {
    if (escaping) {
      out.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    out.push_back(ch);
  }
  return out;
}

std::vector<std::string> splitEscapedTabs(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  bool escaping = false;
  for (char ch : line) {
    if (escaping) {
      current.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    if (ch == '\t') {
      fields.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  fields.push_back(current);
  return fields;
}

std::string safeString(const std::vector<std::string>& fields, size_t index) {
  if (index >= fields.size()) {
    return "";
  }
  return fields[index];
}

double safeDouble(const std::vector<std::string>& fields, size_t index, double fallback = 0.0) {
  if (index >= fields.size()) {
    return fallback;
  }
  try {
    return std::stod(fields[index]);
  } catch (...) {
    return fallback;
  }
}

int safeInt(const std::vector<std::string>& fields, size_t index, int fallback = 0) {
  if (index >= fields.size()) {
    return fallback;
  }
  try {
    return std::stoi(fields[index]);
  } catch (...) {
    return fallback;
  }
}

std::uintmax_t safeSize(const std::vector<std::string>& fields, size_t index, std::uintmax_t fallback = 0) {
  if (index >= fields.size()) {
    return fallback;
  }
  try {
    return static_cast<std::uintmax_t>(std::stoull(fields[index]));
  } catch (...) {
    return fallback;
  }
}

bool safeBool(const std::vector<std::string>& fields, size_t index, bool fallback = false) {
  if (index >= fields.size()) {
    return fallback;
  }
  return fields[index] == "1" || fields[index] == "true";
}

std::string escapeJson(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          std::ostringstream escaped;
          escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                  << static_cast<int>(static_cast<unsigned char>(ch));
          out += escaped.str();
        } else {
          out.push_back(ch);
        }
        break;
    }
  }
  return out;
}

std::string escapeHtml(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 16);
  for (char ch : value) {
    switch (ch) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out.push_back(ch); break;
    }
  }
  return out;
}

std::string deckDefaultName(int index) {
  return "Deck " + std::to_string(index + 1);
}

std::string makeCueId(const Cue& cue, int deckIndex, int cueIndex) {
  std::string seed =
    cue.path + "|" +
    cue.name + "|" +
    std::to_string(deckIndex) + "|" +
    std::to_string(cueIndex) + "|" +
    std::to_string(cue.width) + "x" + std::to_string(cue.height);
  size_t hashValue = std::hash<std::string> {}(seed);
  std::ostringstream output;
  output << "cue-" << std::hex << std::nouppercase << hashValue;
  return output.str();
}

void normalizeCueTiming(Cue& cue) {
  if (!std::isfinite(cue.triggerTimecodeSeconds) || cue.triggerTimecodeSeconds < 0.0) {
    cue.triggerTimecodeSeconds = -1.0;
  }

  if (!std::isfinite(cue.inPointSeconds)) {
    cue.inPointSeconds = 0.0;
  }
  if (!std::isfinite(cue.outPointSeconds)) {
    cue.outPointSeconds = 0.0;
  }

  cue.inPointSeconds = std::max(0.0, cue.inPointSeconds);
  cue.outPointSeconds = std::max(0.0, cue.outPointSeconds);

  if (cue.kind != CueKind::Video || cue.duration <= 0.0) {
    cue.inPointSeconds = 0.0;
    cue.outPointSeconds = 0.0;
    return;
  }

  if (cue.outPointSeconds <= 0.0) {
    cue.outPointSeconds = cue.duration;
  }

  cue.inPointSeconds = std::clamp(cue.inPointSeconds, 0.0, cue.duration);
  cue.outPointSeconds = std::clamp(cue.outPointSeconds, cue.inPointSeconds, cue.duration);
  if (cue.outPointSeconds - cue.inPointSeconds < 0.01) {
    cue.outPointSeconds = std::min(cue.duration, cue.inPointSeconds + 0.01);
  }
}

std::string defaultNdiSourceName(const Deck& deck, int index) {
  std::string base = deck.name.empty() ? deckDefaultName(index) : deck.name;
  return "Deckboy - " + base;
}

std::string defaultNdiKeySourceName(const Deck& deck, int index) {
  return defaultNdiSourceName(deck, index) + " Key";
}

std::string defaultOutputNdiSourceName(const OutputTarget& output, int index) {
  std::string base = trim(output.name).empty()
    ? ("Output " + std::to_string(index + 1))
    : output.name;
  return "Deckboy Out - " + base;
}

std::string defaultOutputNdiKeySourceName(const OutputTarget& output, int index) {
  return defaultOutputNdiSourceName(output, index) + " Key";
}

std::string normalizeWarpMode(std::string mode);

void normalizeDeck(Deck& deck, int index) {
  if (deck.name.empty()) {
    deck.name = deckDefaultName(index);
  }
  if (deck.cues.empty()) {
    deck.selectedIndex = -1;
    deck.activeIndex = -1;
  } else {
    deck.selectedIndex = std::clamp(deck.selectedIndex, 0, static_cast<int>(deck.cues.size()) - 1);
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      deck.activeIndex = -1;
    }

    std::unordered_set<std::string> usedIds;
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      Cue& cue = deck.cues[cueIndex];
      normalizeCueTiming(cue);
      cue.cueId = normalizeCueIdShort(cue.cueId);
      if (cue.cueId.empty()) {
        cue.cueId = normalizeCueIdShort(cue.cueNumber);
      }
      // Repair hasAudio if audioChannels/audioSampleRate say audio exists but the flag
      // was stored incorrectly (e.g. from the probeCue codec field-order bug).
      if (!cue.hasAudio && (cue.audioChannels > 0 || cue.audioSampleRate > 0)) {
        cue.hasAudio = true;
      }
      if (!cue.hasAudio) {
        cue.audioEnabled = false;
      }
      cue.outputScaleX = std::clamp(cue.outputScaleX, 0.25f, 4.0f);
      cue.outputScaleY = std::clamp(cue.outputScaleY, 0.25f, 4.0f);
      cue.outputRotationDegrees = std::clamp(cue.outputRotationDegrees, -180.0f, 180.0f);
      cue.cropLeft = std::clamp(cue.cropLeft, 0.0f, 0.90f);
      cue.cropRight = std::clamp(cue.cropRight, 0.0f, 0.90f);
      cue.cropTop = std::clamp(cue.cropTop, 0.0f, 0.90f);
      cue.cropBottom = std::clamp(cue.cropBottom, 0.0f, 0.90f);
      float maxHorizontal = std::max(0.0f, 0.95f - cue.cropLeft);
      cue.cropRight = std::min(cue.cropRight, maxHorizontal);
      float maxVertical = std::max(0.0f, 0.95f - cue.cropTop);
      cue.cropBottom = std::min(cue.cropBottom, maxVertical);
      cue.chromaKeyTolerance = std::clamp(cue.chromaKeyTolerance, 0.0f, 441.0f);
      cue.chromaKeySoftness = std::clamp(cue.chromaKeySoftness, 0.0f, 200.0f);
      cue.brightness = std::clamp(cue.brightness, 0.0f, 2.0f);
      cue.contrast = std::clamp(cue.contrast, 0.0f, 2.0f);
      cue.saturation = std::clamp(cue.saturation, 0.0f, 2.0f);
      cue.hueShift = std::clamp(cue.hueShift, -180.0f, 180.0f);
      cue.gotoTarget = trim(cue.gotoTarget);
      if (cue.kind == CueKind::Composite) {
        applyCompositePresetToCue(cue, cue.compositeLayoutPreset);
      }
      if (cue.id.empty()) {
        cue.id = makeCueId(cue, index, cueIndex);
      }
      std::string baseId = cue.id;
      int dedupe = 2;
      while (usedIds.find(cue.id) != usedIds.end()) {
        cue.id = baseId + "-" + std::to_string(dedupe++);
      }
      usedIds.insert(cue.id);
    }
    std::vector<int> normalizedSelection;
    normalizedSelection.reserve(deck.selectedIndices.size());
    std::unordered_set<int> seenIndices;
    for (int selected : deck.selectedIndices) {
      if (selected < 0 || selected >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      if (seenIndices.insert(selected).second) {
        normalizedSelection.push_back(selected);
      }
    }
    if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
      if (seenIndices.insert(deck.selectedIndex).second) {
        normalizedSelection.push_back(deck.selectedIndex);
      }
    }
    if (normalizedSelection.empty() && !deck.cues.empty()) {
      normalizedSelection.push_back(deck.selectedIndex);
    }
    std::sort(normalizedSelection.begin(), normalizedSelection.end());
    deck.selectedIndices = std::move(normalizedSelection);
  }
  deck.playlistOpacity = std::clamp(deck.playlistOpacity, 0.0f, 1.0f);
  deck.playlistFadeSeconds = std::clamp(
    std::isfinite(deck.playlistFadeSeconds) ? deck.playlistFadeSeconds : 0.8,
    0.05, 10.0);
  deck.playlistTimebaseFps = normalizePlaylistTimebaseFps(deck.playlistTimebaseFps);
  deck.playlistStartOffsetSeconds = std::clamp(
    std::isfinite(deck.playlistStartOffsetSeconds) ? deck.playlistStartOffsetSeconds : 0.0,
    0.0, 24.0 * 60.0 * 60.0);
  deck.playlistDefaultCueFadeSeconds = std::clamp(
    std::isfinite(deck.playlistDefaultCueFadeSeconds) ? deck.playlistDefaultCueFadeSeconds : 1.5,
    0.0, 10.0);
  deck.playlistDefaultStillDurationSeconds = std::clamp(
    std::isfinite(deck.playlistDefaultStillDurationSeconds) ? deck.playlistDefaultStillDurationSeconds : 8.0,
    0.0, 3600.0);
  deck.outputDisplayIndex = std::max(0, deck.outputDisplayIndex);
  if (deck.ndiSourceName.empty()) {
    deck.ndiSourceName = defaultNdiSourceName(deck, index);
  }
  if (deck.ndiKeySourceName.empty()) {
    deck.ndiKeySourceName = defaultNdiKeySourceName(deck, index);
  }
  deck.canvasViewX = std::clamp(deck.canvasViewX, 0, 32768);
  deck.canvasViewY = std::clamp(deck.canvasViewY, 0, 32768);
  deck.warpTopLeftX = std::clamp(deck.warpTopLeftX, -4096.0f, 4096.0f);
  deck.warpTopLeftY = std::clamp(deck.warpTopLeftY, -4096.0f, 4096.0f);
  deck.warpTopRightX = std::clamp(deck.warpTopRightX, -4096.0f, 4096.0f);
  deck.warpTopRightY = std::clamp(deck.warpTopRightY, -4096.0f, 4096.0f);
  deck.warpBottomRightX = std::clamp(deck.warpBottomRightX, -4096.0f, 4096.0f);
  deck.warpBottomRightY = std::clamp(deck.warpBottomRightY, -4096.0f, 4096.0f);
  deck.warpBottomLeftX = std::clamp(deck.warpBottomLeftX, -4096.0f, 4096.0f);
  deck.warpBottomLeftY = std::clamp(deck.warpBottomLeftY, -4096.0f, 4096.0f);
  deck.warpMode = normalizeWarpMode(deck.warpMode);
  deck.edgeBlendLeft = std::clamp(deck.edgeBlendLeft, 0.0f, 0.49f);
  deck.edgeBlendRight = std::clamp(deck.edgeBlendRight, 0.0f, 0.49f);
  deck.edgeBlendTop = std::clamp(deck.edgeBlendTop, 0.0f, 0.49f);
  deck.edgeBlendBottom = std::clamp(deck.edgeBlendBottom, 0.0f, 0.49f);
  deck.edgeBlendRight = std::min(deck.edgeBlendRight, std::max(0.0f, 0.95f - deck.edgeBlendLeft));
  deck.edgeBlendBottom = std::min(deck.edgeBlendBottom, std::max(0.0f, 0.95f - deck.edgeBlendTop));
  deck.transitionSeconds = std::clamp(deck.transitionSeconds, 0.0, 10.0);
  deck.transitionStyle = transitionStyleToken(parseTransitionStyleToken(deck.transitionStyle));
  if (!std::isfinite(deck.timecodeFps) || deck.timecodeFps < 1.0) {
    deck.timecodeFps = 30.0;
  }
  deck.timecodeFreewheelSeconds = std::clamp(
    std::isfinite(deck.timecodeFreewheelSeconds) ? deck.timecodeFreewheelSeconds : 1.0,
    0.0, 10.0);
  if (!std::isfinite(deck.timecodeCurrentSeconds) || deck.timecodeCurrentSeconds < 0.0) {
    deck.timecodeCurrentSeconds = 0.0;
  }
  if (!std::isfinite(deck.timecodeLastSeconds) || deck.timecodeLastSeconds < 0.0) {
    deck.timecodeLastSeconds = deck.timecodeCurrentSeconds;
  }
  deck.timecodeDirty = false;
}

std::string outputDefaultName(int index) {
  return "Output " + std::to_string(index + 1);
}

std::string makeOutputId(const OutputTarget& output, int outputIndex) {
  std::string seed =
    output.name + "|" +
    std::to_string(output.hostDeckIndex) + "|" +
    std::to_string(output.displayIndex) + "|" +
    std::to_string(outputIndex);
  size_t hashValue = std::hash<std::string> {}(seed);
  std::ostringstream outputId;
  outputId << "out-" << std::hex << std::nouppercase << hashValue;
  return outputId.str();
}


std::string normalizeOutputStreamProtocol(std::string protocol) {
  std::transform(protocol.begin(), protocol.end(), protocol.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (protocol == "rtmp") {
    return "rtmp";
  }
  // "rtmps" used to fall through to "srt" here, which silently made every
  // rtmps branch in the settings UI and in buildOutputStreamArgs unreachable —
  // selecting RTMPS gave you an SRT stream. It is its own protocol.
  if (protocol == "rtmps") {
    return "rtmps";
  }
  // "file" records the finished program to disk instead of sending it. Same
  // pipeline, different sink -- the compositor already hands finished frames to
  // ffmpeg's stdin, so recording is a muxer and a target away from streaming.
  if (protocol == "file") {
    return "file";
  }
  return "srt";
}

// True when the egress target is a file on disk rather than a network sink.
// The streaming-only muxer flags (mpegts header resend, zero mux delay) are
// wrong for a recording and some are rejected outright by the mp4 muxer.
inline bool outputStreamProtocolIsFile(const std::string& normalizedProtocol) {
  return normalizedProtocol == "file";
}

// True for the RTMP family, which is what takes a stream key and the FLV muxer.
inline bool outputStreamProtocolIsRtmp(const std::string& normalizedProtocol) {
  return normalizedProtocol == "rtmp" || normalizedProtocol == "rtmps";
}

std::string normalizeOutputType(std::string outputType) {
  std::transform(outputType.begin(), outputType.end(), outputType.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (outputType == "stream") {
    return "stream";
  }
  return "window";
}

std::string normalizeOutputColorSpace(std::string colorSpace) {
  colorSpace = toLower(trim(colorSpace));
  if (colorSpace == "709" || colorSpace == "bt709" || colorSpace == "rec709" || colorSpace == "rec-709") {
    return "bt709";
  }
  if (colorSpace == "srgb" || colorSpace == "rgb") {
    return "srgb";
  }
  return "auto";
}

std::string normalizeOutputLayoutMode(std::string mode) {
  mode = toLower(trim(mode));
  if (mode == "duplicate" || mode == "dup" || mode == "clone") {
    return "duplicate";
  }
  return "span";
}

std::string normalizeWarpMode(std::string mode) {
  mode = toLower(trim(mode));
  if (mode == "perspective" || mode == "persp" || mode == "projective") {
    return "perspective";
  }
  return "linear";
}

std::string warpModeLabel(std::string mode) {
  std::string normalized = normalizeWarpMode(std::move(mode));
  if (normalized == "perspective") {
    return "Perspective";
  }
  return "Linear";
}

int normalizeOutputOrientationDegrees(int degrees) {
  int normalized = degrees % 360;
  if (normalized < 0) {
    normalized += 360;
  }
  if (normalized < 45) {
    return 0;
  }
  if (normalized < 135) {
    return 90;
  }
  if (normalized < 225) {
    return 180;
  }
  if (normalized < 315) {
    return 270;
  }
  return 0;
}

std::string outputOrientationLabel(int degrees) {
  return std::to_string(normalizeOutputOrientationDegrees(degrees)) + "°";
}

int normalizeOscQueryPort(int port) {
  return std::clamp(port, 1, 65535);
}

int normalizeOscFeedbackRateMs(int value) {
  return std::clamp(value, 40, 2000);
}

int normalizeArtNetPort(int value) {
  return std::clamp(value, 1, 65535);
}

std::string defaultOutputStreamUrl(const std::string& protocol, int outputIndex) {
  int normalizedIndex = std::max(0, outputIndex) + 1;
  const std::string normalized = normalizeOutputStreamProtocol(protocol);
  if (normalized == "rtmp") {
    return "rtmp://127.0.0.1/live/output" + std::to_string(normalizedIndex);
  }
  if (normalized == "rtmps") {
    // RTMPS had no default at all (it normalized to SRT), so choosing it left
    // the operator with an srt:// URL and no clue why nothing connected.
    return "rtmps://127.0.0.1:443/live/output" + std::to_string(normalizedIndex);
  }
  if (normalized == "file") {
    // A NAME, not a final path: buildOutputStreamArgs stamps the time onto the
    // stem at spawn so a second recording can never overwrite the first. The
    // folder is resolved there too, because it depends on whether a show is
    // open and this function does not know.
    return "program-output" + std::to_string(normalizedIndex) + ".mp4";
  }
  // Bare host:port — the transport parameters are added from the SRT fields by
  // applySrtUrlParameters, so they are no longer baked into the default string.
  return "srt://127.0.0.1:9000";
}

int resolveLegacyOutputHostIndexForProject(const Project& project, int deckIndex) {
  if (deckIndex < 0 || deckIndex >= static_cast<int>(project.decks.size())) {
    return deckIndex;
  }
  int current = deckIndex;
  std::vector<bool> visited(project.decks.size(), false);
  while (current >= 0 && current < static_cast<int>(project.decks.size())) {
    if (visited[current]) {
      return deckIndex;
    }
    visited[current] = true;
    int next = project.decks[current].outputRouteDeckIndex;
    if (next < 0 || next >= static_cast<int>(project.decks.size()) || next == current) {
      return current;
    }
    current = next;
  }
  return deckIndex;
}

void normalizeProjectOutputsAndLayers(Project& project) {
  int deckCount = static_cast<int>(project.decks.size());
  if (deckCount <= 0) {
    project.outputs.clear();
    project.focusedOutputIndex = 0;
    return;
  }

  if (project.outputs.empty()) {
    std::vector<int> hostDeckOrder;
    hostDeckOrder.reserve(project.decks.size());
    auto addHost = [&](int hostDeckIndex) {
      if (std::find(hostDeckOrder.begin(), hostDeckOrder.end(), hostDeckIndex) == hostDeckOrder.end()) {
        hostDeckOrder.push_back(hostDeckIndex);
      }
    };
    for (int deckIndex = 0; deckIndex < deckCount; ++deckIndex) {
      addHost(resolveLegacyOutputHostIndexForProject(project, deckIndex));
    }
    if (hostDeckOrder.empty()) {
      hostDeckOrder.push_back(0);
    }
    project.outputs.clear();
    for (size_t i = 0; i < hostDeckOrder.size(); ++i) {
      OutputTarget output;
      output.name = outputDefaultName(static_cast<int>(i));
      output.hostDeckIndex = std::clamp(hostDeckOrder[i], 0, deckCount - 1);
      output.displayIndex = std::max(0, project.decks[output.hostDeckIndex].outputDisplayIndex);
      output.enabled = false;
      project.outputs.push_back(output);
    }
  }

  auto dedupeOutputId = [](std::unordered_set<std::string>& usedIds, const std::string& preferred) {
    std::string normalized = trim(preferred);
    if (normalized.empty()) {
      normalized = "out";
    }
    std::string base = normalized;
    int dedupe = 2;
    while (usedIds.find(normalized) != usedIds.end()) {
      normalized = base + "-" + std::to_string(dedupe++);
    }
    usedIds.insert(normalized);
    return normalized;
  };

  for (size_t i = 0; i < project.outputs.size(); ++i) {
    OutputTarget& output = project.outputs[i];
    if (output.name.empty()) {
      output.name = outputDefaultName(static_cast<int>(i));
    }
    output.hostDeckIndex = std::clamp(output.hostDeckIndex, 0, deckCount - 1);
    output.displayIndex = std::max(0, output.displayIndex);
    output.outputType = normalizeOutputType(output.outputType);
    output.mirrorSourceOutputIndex = std::clamp(output.mirrorSourceOutputIndex, -1, static_cast<int>(project.outputs.size()) - 1);
    if (output.outputType != "stream") {
      output.mirrorSourceOutputIndex = -1;
    }
    if (output.mirrorSourceOutputIndex == static_cast<int>(i)) {
      output.mirrorSourceOutputIndex = -1;
    }
    output.streamProtocol = normalizeOutputStreamProtocol(output.streamProtocol);
    output.streamBitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    output.outputAlpha = std::clamp(output.outputAlpha, 0.0f, 1.0f);
    output.outputDelayMs = std::clamp(output.outputDelayMs, 0, 5000);
    output.aoiLeft   = std::clamp(output.aoiLeft,   0.0f, 0.95f);
    output.aoiRight  = std::clamp(output.aoiRight,  0.0f, 0.95f);
    output.aoiTop    = std::clamp(output.aoiTop,    0.0f, 0.95f);
    output.aoiBottom = std::clamp(output.aoiBottom, 0.0f, 0.95f);
    output.outputColorSpace = normalizeOutputColorSpace(output.outputColorSpace);
    output.outputLayoutMode = normalizeOutputLayoutMode(output.outputLayoutMode);
    output.outputOrientationDegrees = normalizeOutputOrientationDegrees(output.outputOrientationDegrees);
    if (trim(output.streamUrl).empty()) {
      output.streamUrl = defaultOutputStreamUrl(output.streamProtocol, static_cast<int>(i));
    }
    if (output.ndiSourceName.empty()) {
      output.ndiSourceName = defaultOutputNdiSourceName(output, static_cast<int>(i));
    }
    if (output.ndiKeySourceName.empty()) {
      output.ndiKeySourceName = defaultOutputNdiKeySourceName(output, static_cast<int>(i));
    }
    if (!output.ndiEnabled) {
      output.ndiKeyEnabled = false;
    }
    if (trim(output.outputId).empty()) {
      output.outputId = makeOutputId(output, static_cast<int>(i));
    }
  }

  {
    std::unordered_set<std::string> usedOutputIds;
    for (size_t i = 0; i < project.outputs.size(); ++i) {
      OutputTarget& output = project.outputs[i];
      output.outputId = dedupeOutputId(usedOutputIds, output.outputId);
    }
  }
  project.focusedOutputIndex = std::clamp(project.focusedOutputIndex, 0, static_cast<int>(project.outputs.size()) - 1);

  std::vector<bool> hostDisplayAssigned(deckCount, false);
  for (const auto& output : project.outputs) {
    if (!output.enabled) {
      continue;
    }
    int hostDeckIndex = std::clamp(output.hostDeckIndex, 0, deckCount - 1);
    if (!hostDisplayAssigned[hostDeckIndex]) {
      project.decks[hostDeckIndex].outputDisplayIndex = std::max(0, output.displayIndex);
      hostDisplayAssigned[hostDeckIndex] = true;
    }
  }

  // Single-deck: route deck 0 to output 0 directly.
  for (int deckIndex = 0; deckIndex < deckCount; ++deckIndex) {
    project.decks[deckIndex].outputRouteDeckIndex = deckIndex;
  }
}

bool outputHasExplicitNdiSettings(const OutputTarget& output, int outputIndex) {
  std::string fillName = trim(output.ndiSourceName);
  std::string keyName = trim(output.ndiKeySourceName);
  std::string defaultFill = defaultOutputNdiSourceName(output, outputIndex);
  std::string defaultKey = defaultOutputNdiKeySourceName(output, outputIndex);
  bool fillCustom = !fillName.empty() && fillName != defaultFill;
  bool keyCustom = !keyName.empty() && keyName != defaultKey;
  return output.ndiEnabled || output.ndiKeyEnabled || fillCustom || keyCustom;
}

void migrateLegacyDeckNdiToOutputs(Project& project) {
  int deckCount = static_cast<int>(project.decks.size());
  int outputCount = static_cast<int>(project.outputs.size());
  if (deckCount <= 0 || outputCount <= 0) {
    return;
  }

  for (int outputIndex = 0; outputIndex < outputCount; ++outputIndex) {
    OutputTarget& output = project.outputs[outputIndex];
    if (outputHasExplicitNdiSettings(output, outputIndex)) {
      continue;
    }

    int sourceDeckIndex = std::clamp(output.hostDeckIndex, 0, deckCount - 1);
    const Deck& deck = project.decks[sourceDeckIndex];
    std::string legacyFillName = trim(deck.ndiSourceName);
    std::string legacyKeyName = trim(deck.ndiKeySourceName);
    bool hasLegacy = deck.ndiEnabled || deck.ndiKeyEnabled || !legacyFillName.empty() || !legacyKeyName.empty();
    if (!hasLegacy) {
      continue;
    }

    output.ndiEnabled = deck.ndiEnabled || deck.ndiKeyEnabled;
    output.ndiKeyEnabled = deck.ndiKeyEnabled;
    if (!legacyFillName.empty()) {
      output.ndiSourceName = legacyFillName;
    }
    if (!legacyKeyName.empty()) {
      output.ndiKeySourceName = legacyKeyName;
    }
    if (trim(output.ndiSourceName).empty()) {
      output.ndiSourceName = defaultOutputNdiSourceName(output, outputIndex);
    }
    if (trim(output.ndiKeySourceName).empty()) {
      output.ndiKeySourceName = defaultOutputNdiKeySourceName(output, outputIndex);
    }
    if (!output.ndiEnabled) {
      output.ndiKeyEnabled = false;
    }
  }
}


void normalizeProject(Project& project) {
  if (project.decks.empty()) {
    project.decks.push_back(Deck {});
  }
  for (size_t index = 0; index < project.decks.size(); ++index) {
    normalizeDeck(project.decks[index], static_cast<int>(index));
  }
  // Migration (v0.78.6): Pocket Test carries the A/V sync pop, but pattern
  // cues created before v0.78.5 were hard-muted by addPatternCue and had
  // hasAudio=false (which hides the audio controls, so the mute couldn't
  // even be lifted). hasAudio=false is the "never migrated" marker: flip
  // such cues to audible once; operator mutes made afterwards persist
  // because they leave hasAudio=true.
  for (Deck& deck : project.decks) {
    for (Cue& cue : deck.cues) {
      if (cue.kind == CueKind::Pattern &&
          stripPatternMotionSuffix(normalizePatternTypeId(cue.path)) == "pocket-test" &&
          !cue.hasAudio) {
        cue.hasAudio = true;
        cue.audioEnabled = true;
      }
    }
  }
  for (size_t index = 0; index < project.decks.size(); ++index) {
    Deck& deck = project.decks[index];
    if (deck.outputRouteDeckIndex < 0 ||
        deck.outputRouteDeckIndex >= static_cast<int>(project.decks.size())) {
      deck.outputRouteDeckIndex = static_cast<int>(index);
    }
  }
  normalizeProjectOutputsAndLayers(project);
  migrateLegacyDeckNdiToOutputs(project);
  project.focusedDeckIndex = std::clamp(project.focusedDeckIndex, 0, static_cast<int>(project.decks.size()) - 1);
  project.outputRenderWidth = std::clamp(project.outputRenderWidth, 320, 7680);
  project.outputRenderHeight = std::clamp(project.outputRenderHeight, 180, 4320);
  if (!std::isfinite(project.outputRefreshRateHz) || project.outputRefreshRateHz < 0.0) {
    project.outputRefreshRateHz = 0.0;
  }
  project.outputRefreshRateHz = std::min(project.outputRefreshRateHz, 240.0);
  if (project.outputBitDepth != 0 && project.outputBitDepth != 8 && project.outputBitDepth != 10) {
    project.outputBitDepth = 0;
  }
  project.outputCanvasWidth = std::clamp(project.outputCanvasWidth, 320, 16384);
  project.outputCanvasHeight = std::clamp(project.outputCanvasHeight, 180, 16384);
  project.advancedOutputMode = project.advancedOutputMode || project.decks.size() > 1;
  project.jumpMode = normalizeJumpModeToken(project.jumpMode);
  project.panicProfile = normalizePanicProfileToken(project.panicProfile);
  project.uiTransitionsEnabled = true;
  project.oscQueryPort = normalizeOscQueryPort(project.oscQueryPort);
  project.oscFeedbackRateMs = normalizeOscFeedbackRateMs(project.oscFeedbackRateMs);
  project.artNetPort = normalizeArtNetPort(project.artNetPort);
  project.panicFadeSeconds = std::clamp(
    std::isfinite(project.panicFadeSeconds) ? project.panicFadeSeconds : 0.9,
    0.1, 5.0);
}

bool isImagePath(const fs::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  static const std::array<std::string, 12> kImageExts {
    ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", ".tif", ".tiff", ".avif",
    // Apple stills. An iPhone photo is HEIC (a single-frame "Main Still
    // Picture" HEVC in a HEIF container); without these it was classified as a
    // VIDEO cue and the transport waited forever for a stream that only ever
    // yields one frame — the "clip stuck loading" an operator hits the first
    // time they drop a photo straight off a phone. ffmpeg decodes HEIC/HEIF on
    // every platform, and the still path already uses ffmpeg, so this works
    // everywhere, not just macOS.
    ".heic", ".heif"
  };
  return std::find(kImageExts.begin(), kImageExts.end(), ext) != kImageExts.end();
}

bool isAudioPath(const fs::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  static const std::array<std::string, 10> kAudioExts {
    ".mp3", ".wav", ".flac", ".aac", ".ogg", ".oga", ".m4a", ".opus", ".wma", ".aiff"
  };
  return std::find(kAudioExts.begin(), kAudioExts.end(), ext) != kAudioExts.end();
}

// True for files we accept when a folder is dropped/imported (video/image/audio).
bool isAcceptableMediaPath(const fs::path& path) {
  if (isImagePath(path) || isAudioPath(path)) {
    return true;
  }
  // A PDF is acceptable to IMPORT even though it never becomes a cue itself --
  // importPaths turns it into one still per page. Without this, dropping a
  // folder of show material silently skipped the slide decks in it.
  if (deckboy::platform::isPdfDocumentPath(path)) {
    return true;
  }
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  static const std::array<std::string, 14> kVideoExts {
    ".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".mpg", ".mpeg",
    ".wmv", ".flv", ".ogv", ".ts", ".m2ts", ".mxf"
  };
  return std::find(kVideoExts.begin(), kVideoExts.end(), ext) != kVideoExts.end();
}

std::optional<double> parseFps(const std::string& rate) {
  auto slash = rate.find('/');
  if (slash == std::string::npos) {
    return std::nullopt;
  }
  try {
    double numerator = std::stod(rate.substr(0, slash));
    double denominator = std::stod(rate.substr(slash + 1));
    if (denominator == 0.0) {
      return std::nullopt;
    }
    return numerator / denominator;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<Cue> probeCue(const fs::path& mediaPath) {
  auto output = readAllText({
    "ffprobe",
    "-v",
    "error",
    "-show_entries",
    "format=duration,format_name,size:stream=codec_type,codec_name,width,height,r_frame_rate,channels,sample_rate",
    "-of",
    "default=noprint_wrappers=1",
    mediaPath.string()
  });

  if (!output) {
    return std::nullopt;
  }

  Cue cue;
  cue.path = mediaPath.string();
  cue.name = mediaPath.stem().string();
  cue.kind = isImagePath(mediaPath) ? CueKind::Image
           : isAudioPath(mediaPath) ? CueKind::Audio
           : CueKind::Video;
  cue.fps = cue.kind == CueKind::Image ? 0.0 : 30.0;

  // ffprobe may emit codec_name before or after codec_type depending on version/format.
  // Buffer the codec_name and apply it once we know the stream's codec_type.
  std::string lastCodecType;
  std::string pendingCodecName;
  bool pendingApplied = false;
  auto tryApplyCodec = [&]() {
    if (pendingCodecName.empty() || lastCodecType.empty() || pendingApplied) return;
    pendingApplied = true;
    if (lastCodecType == "video" && cue.videoCodec.empty()) {
      cue.videoCodec = pendingCodecName;
    } else if (lastCodecType == "audio" && cue.audioCodec.empty()) {
      cue.audioCodec = pendingCodecName;
      cue.hasAudio = true;
    } else if (lastCodecType == "subtitle" && cue.subtitleStreamId.empty()) {
      cue.subtitleStreamId = "0:s:0";
    }
  };
  for (const auto& line : splitLines(*output)) {
    auto sep = line.find('=');
    if (sep == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, sep);
    std::string value = line.substr(sep + 1);

    if (key == "codec_type") {
      // If the previous stream's pair was already applied, this codec_type
      // marks a new stream — clear any stale pendingCodecName left over from
      // the previous stream so we don't mis-pair (e.g. audio-first mp4 where
      // codec_name=h264 arrives while lastCodecType is still "audio").
      if (pendingApplied) {
        pendingCodecName.clear();
        pendingApplied = false;
      }
      lastCodecType = value;
      tryApplyCodec();
    } else if (key == "codec_name") {
      // Symmetric: if the previous stream was paired, this codec_name is a
      // new stream boundary — clear the stale lastCodecType.
      if (pendingApplied) {
        lastCodecType.clear();
      }
      pendingCodecName = value;
      pendingApplied = false;
      tryApplyCodec();
    } else if (key == "width") {
      if (lastCodecType == "video" && cue.width == 0) {
        cue.width = std::max(0, std::atoi(value.c_str()));
      }
    } else if (key == "height") {
      if (lastCodecType == "video" && cue.height == 0) {
        cue.height = std::max(0, std::atoi(value.c_str()));
      }
    } else if (key == "r_frame_rate" && cue.kind == CueKind::Video) {
      auto fps = parseFps(value);
      if (fps && *fps > 1.0) {
        cue.fps = *fps;
      }
    } else if (key == "channels" && lastCodecType == "audio") {
      cue.audioChannels = std::max(0, std::atoi(value.c_str()));
    } else if (key == "sample_rate" && lastCodecType == "audio") {
      cue.audioSampleRate = std::max(0, std::atoi(value.c_str()));
    } else if (key == "duration" && (cue.kind == CueKind::Video || cue.duration == 0.0)) {
      double d = std::atof(value.c_str());
      if (d > 0.0) cue.duration = d;
    } else if (key == "format_name") {
      cue.formatName = value;
    } else if (key == "size") {
      cue.sizeBytes = static_cast<std::uintmax_t>(std::strtoull(value.c_str(), nullptr, 10));
    }
  }

  // Detect rotation from side_data (phone videos) and swap width/height if needed
  if (cue.width > 0 && cue.height > 0 && !cue.videoCodec.empty()) {
    auto sideData = readAllText({
      "ffprobe", "-v", "error", "-select_streams", "v:0",
      "-show_entries", "stream_side_data=rotation",
      "-of", "default=noprint_wrappers=1",
      mediaPath.string()
    });
    if (sideData) {
      for (const auto& line : splitLines(*sideData)) {
        auto sep = line.find('=');
        if (sep != std::string::npos && line.substr(0, sep) == "rotation") {
          int rot = std::abs(std::atoi(line.substr(sep + 1).c_str()));
          if (rot == 90 || rot == 270) {
            std::swap(cue.width, cue.height);
          }
        }
      }
    }
  }

  // Auto-detect audio-only: no video stream but has audio
  if (cue.videoCodec.empty() && cue.hasAudio) {
    cue.kind = CueKind::Audio;
    // Audio cues don't have video dimensions — give them nominal size
    if (cue.width <= 0) cue.width = 1;
    if (cue.height <= 0) cue.height = 1;
  }
  if (cue.width <= 0 || cue.height <= 0) {
    return std::nullopt;
  }
  if ((cue.kind == CueKind::Video || cue.kind == CueKind::Audio) && cue.duration <= 0.0) {
    cue.duration = 0.0;
  }
  return cue;
}

bool saveProject(const fs::path& projectFile, const Project& project) {
  fs::path resolved = projectFile.empty() ? Paths::defaultProjectFile() : projectFile;
  if (resolved.has_parent_path()) {
    fs::create_directories(resolved.parent_path());
  }
  std::ofstream output(resolved, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }

  output << "title\t" << escapeField(project.title) << '\n';
  output << "focused_deck\t" << project.focusedDeckIndex << '\n';
  output << "focused_output\t" << project.focusedOutputIndex << '\n';
  output << "advanced_mode\t" << (project.advancedOutputMode ? 1 : 0) << '\n';
  output << "ptp_domain\t" << project.ptpDomain << '\n';
  output << "nmos_enabled\t" << (project.nmosEnabled ? 1 : 0) << '\n';
  output << "nmos_registry\t" << escapeField(project.nmosRegistryUrl) << '\n';
  output << "nmos_port\t" << project.nmosPort << '\n';
  output << "nmos_interface\t" << escapeField(project.nmosInterfaceName) << '\n';
  output << "ltc_out\t" << (project.ltcOutputEnabled ? 1 : 0) << '\n';
  output << "ltc_out_device\t" << escapeField(project.ltcOutputDeviceName) << '\n';
  output << "ltc_out_fps\t" << project.ltcOutputFps << '\n';
  output << "ui_sounds\t" << (project.uiSoundsEnabled ? 1 : 0) << '\n';
  output << "ui_transitions\t" << (project.uiTransitionsEnabled ? 1 : 0) << '\n';
  output << "splash_character\t" << escapeField(project.splashCharacter) << '\n';
  output << "recording_dir\t" << escapeField(project.recordingDir) << '\n';
  // The recording FORMAT is part of the show. An operator who set 1080p25
  // ProRes with drop-frame timecode must get it back tomorrow, not the
  // defaults — a setting that does not round-trip is a setting that does not
  // exist, which is exactly what the cue-kind bug taught.
  output << "recording_width\t" << project.recordingWidth << '\n';
  output << "recording_height\t" << project.recordingHeight << '\n';
  output << "recording_fps\t" << project.recordingFps << '\n';
  output << "recording_codec\t" << escapeField(project.recordingCodec) << '\n';
  output << "recording_tc_mode\t" << escapeField(project.recordingTimecodeMode) << '\n';
  output << "recording_tc_start\t" << escapeField(project.recordingTimecodeStart) << '\n';
  output << "recording_tc_df\t" << escapeField(project.recordingTimecodeDropFrame) << '\n';
  output << "recording_segment_minutes\t" << project.recordingSegmentMinutes << '\n';
  output << "recording_segment_mb\t" << project.recordingSegmentMegabytes << '\n';
  output << "recording_remux\t" << (project.recordingRemuxOnStop ? 1 : 0) << '\n';
  output << "asio_driver\t" << escapeField(project.asioDriverName) << '\n';
  output << "audio_input_device\t" << escapeField(project.audioInputDeviceName) << '\n';
  output << "synth_keyboard\t" << (project.synthKeyboardEnabled ? 1 : 0) << '\n';
  output << "synth_octave\t" << project.synthKeyboardOctave << '\n';
  output << "midi_to_synth\t" << (project.midiToSynth ? 1 : 0) << '\n';
  output << "audio_input_enabled\t" << (project.audioInputEnabled ? 1 : 0) << '\n';
  output << "audio_input_gain_db\t" << project.audioInputGainDb << '\n';
  output << "audio_input_to_program\t" << (project.audioInputToProgram ? 1 : 0) << '\n';
  output << "audio_input_mono\t" << (project.audioInputMono ? 1 : 0) << '\n';
  output << "asio_channels\t" << project.asioChannels << '\n';
  output << "hap_suggestion_dismissed\t" << (project.hapSuggestionDismissed ? 1 : 0) << '\n';
  output << "theme\t" << escapeField(project.theme) << '\n';
  output << "terrarium_unlocked\t" << (project.terrariumUnlocked ? 1 : 0) << '\n';
  output << "geometry_aspect_link\t" << (project.geometryAspectLinked ? 1 : 0) << '\n';
  output << "ui_scale\t" << project.uiScale << '\n';
  // VJ mode. Written as project scalars so a show that never turns it on
  // carries the defaults and behaves exactly as it always did.
  output << "vj_mode\t" << (project.vjModeEnabled ? 1 : 0) << '\n';
  output << "vj_deck_a\t" << project.vjDeckA << '\n';
  output << "vj_deck_b\t" << project.vjDeckB << '\n';
  output << "vj_mix\t" << project.vjMixPosition << '\n';
  output << "vj_blend\t" << project.vjBlendMode << '\n';
  output << "vj_bpm\t" << project.vjTempoBpm << '\n';
  output << "vj_quantise\t" << (project.vjQuantiseTakes ? 1 : 0) << '\n';
  output << "interaction_mode\t" << escapeField(project.interactionMode) << '\n';
  output << "allow_remote_network\t" << (project.allowRemoteNetwork ? 1 : 0) << '\n';
  output << "osc_query_enabled\t" << (project.oscQueryEnabled ? 1 : 0) << '\n';
  output << "osc_query_port\t" << project.oscQueryPort << '\n';
  output << "osc_feedback_mirror\t" << (project.oscFeedbackMirrorEnabled ? 1 : 0) << '\n';
  output << "osc_feedback_rate_ms\t" << project.oscFeedbackRateMs << '\n';
  output << "integration_atem_trigger\t" << (project.atemTriggerEnabled ? 1 : 0) << '\n';
  output << "integration_ndi_trigger\t" << (project.ndiTriggerEnabled ? 1 : 0) << '\n';
  output << "integration_nmc_sync\t" << (project.nmcSyncEnabled ? 1 : 0) << '\n';
  output << "integration_mtc_ingest\t" << (project.mtcIngestEnabled ? 1 : 0) << '\n';
  output << "integration_ltc_ingest\t" << (project.ltcIngestEnabled ? 1 : 0) << '\n';
  output << "integration_dmx_artnet\t" << (project.dmxArtNetEnabled ? 1 : 0) << '\n';
  output << "integration_artnet_port\t" << project.artNetPort << '\n';
  output << "integration_tsl_tally\t" << (project.tslTallyEnabled ? 1 : 0) << '\n';
  output << "integration_tsl_port\t" << project.tslTallyPort << '\n';
  output << "integration_tsl_address\t" << escapeField(project.tslTallyAddress) << '\n';
  output << "audio_buffer_samples\t" << project.audioBufferSamples << '\n';
  output << "audio_delay_ms\t" << project.audioDelayMs << '\n';
  output << "jump_mode\t" << escapeField(project.jumpMode) << '\n';
  output << "jump_transition\t" << (project.jumpTransitionEnabled ? 1 : 0) << '\n';
  output << "panic_profile\t" << escapeField(project.panicProfile) << '\n';
  output << "panic_fade_seconds\t" << project.panicFadeSeconds << '\n';
  output << "panic_auto_restore\t" << (project.panicAutoRestore ? 1 : 0) << '\n';
  output << "master_volume\t" << project.masterVolume << '\n';
  output << "master_dimmer\t" << project.masterDimmer << '\n';
  output << "output_follow_display\t" << (project.outputFollowDisplay ? 1 : 0) << '\n';
  output << "output_render_width\t" << project.outputRenderWidth << '\n';
  output << "output_render_height\t" << project.outputRenderHeight << '\n';
  output << "output_refresh_hz\t" << project.outputRefreshRateHz << '\n';
  output << "output_bit_depth\t" << project.outputBitDepth << '\n';
  output << "output_canvas_enabled\t" << (project.outputCanvasEnabled ? 1 : 0) << '\n';
  output << "output_canvas_width\t" << project.outputCanvasWidth << '\n';
  output << "output_canvas_height\t" << project.outputCanvasHeight << '\n';
  for (size_t outputIndex = 0; outputIndex < project.outputs.size(); ++outputIndex) {
    const auto& outputTarget = project.outputs[outputIndex];
    output
      << "output_target\t"
      << outputIndex << '\t'
      << escapeField(outputTarget.name) << '\t'
      << outputTarget.hostDeckIndex << '\t'
      << outputTarget.displayIndex << '\t'
      << (outputTarget.enabled ? 1 : 0) << '\t'
      << (outputTarget.streamEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.streamProtocol) << '\t'
      << escapeField(outputTarget.streamUrl) << '\t'
      << outputTarget.streamBitrateKbps << '\t'
      << (outputTarget.ndiEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.ndiSourceName) << '\t'
      << (outputTarget.ndiKeyEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.ndiKeySourceName) << '\t'
      << escapeField(outputTarget.outputType) << '\t'
      << outputTarget.mirrorSourceOutputIndex << '\t'
      << escapeField(outputTarget.outputId) << '\t'
      << outputTarget.outputAlpha << '\t'
      << outputTarget.outputDelayMs << '\t'
      << (outputTarget.outputTimeOverlayEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.outputColorSpace) << '\t'
      << escapeField(outputTarget.outputLayoutMode) << '\t'
      << outputTarget.outputOrientationDegrees << '\t'
      << (outputTarget.outputTestCardEnabled ? 1 : 0) << '\t'
      << (outputTarget.deckLinkEnabled ? 1 : 0) << '\t'
      << outputTarget.deckLinkDeviceId << '\t'
      << escapeField(outputTarget.deckLinkMode) << '\t'
      << (outputTarget.deckLink10Bit ? 1 : 0)
      << '\t' << outputTarget.aoiLeft
      << '\t' << outputTarget.aoiRight
      << '\t' << outputTarget.aoiTop
      << '\t' << outputTarget.aoiBottom
      << '\t' << (outputTarget.spoutEnabled ? 1 : 0)
      << '\t' << escapeField(outputTarget.spoutSenderName)
      << '\t' << escapeField(outputTarget.streamKey)
      << '\t' << escapeField(outputTarget.displayName)
      // ST 2110-20 (fields 36-40) — appended, guarded on load as always
      << '\t' << (outputTarget.st2110Enabled ? 1 : 0)
      << '\t' << escapeField(outputTarget.st2110Address)
      << '\t' << escapeField(outputTarget.st2110Interface)
      << '\t' << outputTarget.st2110Port
      << '\t' << (outputTarget.st2110TenBit ? 1 : 0)
      // SRT transport + encoder (fields 41-45)
      << '\t' << outputTarget.srtLatencyMs
      << '\t' << escapeField(outputTarget.srtPassphrase)
      << '\t' << escapeField(outputTarget.srtStreamId)
      << '\t' << escapeField(outputTarget.srtMode)
      << '\t' << outputTarget.streamKeyframeSeconds
      << '\t' << outputTarget.streamAudioBitrateKbps
      << '\n';
  }
  for (size_t deckIndex = 0; deckIndex < project.decks.size(); ++deckIndex) {
    const auto& deck = project.decks[deckIndex];
    output
      << "deck\t"
      << deckIndex << '\t'
      << escapeField(deck.name) << '\t'
      << deck.selectedIndex << '\t'
      << deck.activeIndex << '\t'
      << 0 << '\t' // legacy auto-advance placeholder: cue endings are now per-cue
      << (deck.playlistLoop ? 1 : 0) << '\t'
      << escapeField(deck.audioOutputDeviceName) << '\t'
      << deck.outputDisplayIndex << '\t'
      << (deck.ndiEnabled ? 1 : 0) << '\t'
      << escapeField(deck.ndiSourceName) << '\t'
      << (deck.timeOverlayEnabled ? 1 : 0) << '\t'
      << deck.transitionSeconds << '\t'
      << escapeField(deck.transitionStyle) << '\t'
      << (deck.timecodeChaseEnabled ? 1 : 0) << '\t'
      << (deck.timecodeRunEnabled ? 1 : 0) << '\t'
      << (deck.timecodeTriggerEnabled ? 1 : 0) << '\t'
      << deck.timecodeFps << '\t'
      << deck.timecodeCurrentSeconds << '\t'
      << (deck.shuffle ? 1 : 0) << '\t'
      << (deck.ndiKeyEnabled ? 1 : 0) << '\t'
      << escapeField(deck.ndiKeySourceName) << '\t'
      << deck.canvasViewX << '\t'
      << deck.canvasViewY << '\t'
      << (deck.warpEnabled ? 1 : 0) << '\t'
      << escapeField(deck.warpMode) << '\t'
      << deck.warpTopLeftX << '\t'
      << deck.warpTopLeftY << '\t'
      << deck.warpTopRightX << '\t'
      << deck.warpTopRightY << '\t'
      << deck.warpBottomRightX << '\t'
      << deck.warpBottomRightY << '\t'
      << deck.warpBottomLeftX << '\t'
      << deck.warpBottomLeftY << '\t'
      << deck.edgeBlendLeft << '\t'
      << deck.edgeBlendRight << '\t'
      << deck.edgeBlendTop << '\t'
      << deck.edgeBlendBottom << '\t'
      << deck.outputRouteDeckIndex << '\t'
      << 0 << '\t'
      << deck.timecodeFreewheelSeconds << '\t'
      << (deck.timecodeJamSyncEnabled ? 1 : 0) << '\t'
      << deck.playlistOpacity << '\t'
      << (deck.playlistAutoFade ? 1 : 0) << '\t'
      << deck.playlistFadeSeconds << '\t'
      << deck.playlistTimebaseFps << '\t'
      << deck.playlistStartOffsetSeconds << '\t'
      << deck.playlistDefaultCueFadeSeconds << '\t'
      << deck.playlistDefaultStillDurationSeconds << '\t'
      << (deck.playlistDefaultLoop ? 1 : 0) << '\t'
      << (deck.playlistDefaultFadeInEnabled ? 1 : 0) << '\t'
      << (deck.playlistDefaultFadeOutEnabled ? 1 : 0) << '\t'
      << (deck.playlistDefaultAudioEnabled ? 1 : 0) << '\t'
      << (deck.playlistDefaultPauseAtBeginning ? 1 : 0) << '\t'
      << (deck.playlistDefaultPauseAtEnd ? 1 : 0) << '\t'
      << (deck.playlistDefaultTransitionToNext ? 1 : 0) << '\t'
      << deck.audioOutputChannels
      << '\n';

    for (const auto& cue : deck.cues) {
      output
        << "cue\t"
        << deckIndex << '\t'
        << escapeField(cue.path) << '\t'
        << escapeField(cue.name) << '\t'
        << cueKindToken(cue.kind) << '\t'
        << cue.duration << '\t'
        << cue.width << '\t'
        << cue.height << '\t'
        << cue.fps << '\t'
        << escapeField(cue.formatName) << '\t'
        << escapeField(cue.videoCodec) << '\t'
        << escapeField(cue.audioCodec) << '\t'
        << (cue.hasAudio ? "1" : "0") << '\t'
        << cue.sizeBytes << '\t'
        << colorToHex(cue.color) << '\t'
        << cue.fadeInSeconds << '\t'
        << cue.fadeOutSeconds << '\t'
        << (cue.loop ? "1" : "0") << '\t'
        << (cue.pauseOnLastFrame ? "1" : "0") << '\t'
        << escapeField(cue.id) << '\t'
        << cue.inPointSeconds << '\t'
        << cue.outPointSeconds << '\t'
        << cue.triggerTimecodeSeconds << '\t'
        << cueEndActionToken(cue.endAction) << '\t'
        << cue.cueTransitionSeconds << '\t'
        << escapeField(cue.cueTransitionStyle) << '\t'
        << escapeField(cue.lowerThirdText) << '\t'
        << escapeField(cue.lowerThirdSubtext) << '\t'
        << cue.lowerThirdBgAlpha << '\t'
        << cue.stillDurationSeconds << '\t'
        << cue.loopCount << '\t'
        << cue.playbackSpeed << '\t'
        << escapeField(cue.colorTag)
        << '\t' << escapeField(cue.notes)
        << '\t' << cue.outputScaleX
        << '\t' << cue.outputScaleY
        << '\t' << static_cast<int>(cue.scaleMode)
        << '\t' << cue.outputOffsetX
        << '\t' << cue.outputOffsetY
        << '\t' << escapeField(cue.cueNumber)
        << '\t' << [&]() {
             std::ostringstream pp;
             for (size_t i = 0; i < cue.pausePoints.size(); ++i) {
               if (i) pp << ',';
               pp << cue.pausePoints[i];
             }
             return pp.str();
           }()
        << '\t' << cue.outputRotationDegrees
        << '\t' << cue.cropLeft
        << '\t' << cue.cropRight
        << '\t' << cue.cropTop
        << '\t' << cue.cropBottom
        << '\t' << (cue.chromaKeyEnabled ? "1" : "0")
        << '\t' << colorToHex(cue.chromaKeyColor)
        << '\t' << cue.chromaKeyTolerance
        << '\t' << cue.chromaKeySoftness
        << '\t' << cue.brightness
        << '\t' << cue.contrast
        << '\t' << cue.saturation
        << '\t' << cue.hueShift
        << '\t' << escapeField(cue.cueId)
        << '\t' << (cue.audioEnabled ? "1" : "0")
        << '\t' << (cue.pauseAtBeginning ? "1" : "0")
        << '\t' << (cue.transitionToNext ? "1" : "0")
        << '\t' << escapeField(cue.gotoTarget)
        << '\t' << cue.audioChannels
        << '\t' << cue.audioSampleRate
        << '\t' << escapeField(cue.pipTargetCue)
        << '\t' << escapeField(cue.pipSourceType)
        << '\t' << escapeField(cue.attachedLowerThirdCue)
        << '\t' << escapeField(cue.attachedPipCue)
        << '\t' << escapeField(cue.compositeLayoutPreset)
        << '\t' << escapeField(cue.compositeAudioSlotId)
        << '\t' << colorToHex(cue.compositeBackgroundColor)
        << '\t' << cue.compositeSlots.size();
      for (const CompositeSlot& slot : cue.compositeSlots) {
        output
          << '\t' << escapeField(slot.id)
          << '\t' << escapeField(slot.name)
          << '\t' << escapeField(slot.sourceType)
          << '\t' << escapeField(slot.source)
          << '\t' << (slot.visible ? "1" : "0")
          << '\t' << (slot.audioEnabled ? "1" : "0")
          << '\t' << static_cast<int>(slot.scaleMode)
          << '\t' << slot.normX
          << '\t' << slot.normY
          << '\t' << slot.normW
          << '\t' << slot.normH;
      }
      // Subtitle fields (appended after composite slots for backward compat)
      output
        << '\t' << escapeField(cue.subtitlePath)
        << '\t' << escapeField(cue.subtitleStreamId)
        << '\t' << (cue.subtitleEnabled ? "1" : "0")
        << '\t' << (cue.refreshOnTake ? "1" : "0")
        << '\t' << cue.audioGainDb
        << '\t' << cue.audioPan
        << '\t' << (cue.audioMono ? "1" : "0")
        << '\t' << cue.audioFadeInSeconds
        << '\t' << cue.audioFadeOutSeconds
        << '\t' << cue.audioOutputPair
        << '\t' << (cue.datamoshEnabled ? "1" : "0")
        << '\t' << escapeField(cue.moshPath)
        << '\t' << cue.timer.durationSeconds
        << '\t' << cue.timer.amberSeconds
        << '\t' << cue.timer.redSeconds
        << '\t' << (cue.timer.countUpAfterZero ? "1" : "0")
        << '\t' << (cue.timer.blinkAtZero ? "1" : "0")
        << '\t' << escapeField(cue.timer.message)
        << '\t' << static_cast<int>(cue.timer.mode)
        << '\t' << static_cast<int>(cue.timer.face)
        << '\t' << (cue.timer.showProgressBar ? "1" : "0")
        << '\t' << (cue.timer.messageIsUrgent ? "1" : "0")
        << '\t' << cue.scheduledStartSeconds
        << '\t' << joinMarkerTimes(cue)
        << '\t' << escapeField(joinMarkerNames(cue))
        << '\t' << cue.datamoshLook
        << '\t' << cue.timer.colorNormal
        << '\t' << cue.timer.colorAmber
        << '\t' << cue.timer.colorRed
        << '\t' << cue.timer.colorBackground
        << '\t' << (cue.timer.chimeAtAmber ? "1" : "0")
        << '\t' << (cue.timer.chimeAtRed ? "1" : "0")
        << '\t' << (cue.timer.chimeAtZero ? "1" : "0")
        << '\t' << cue.timer.chimeSound
        << '\t' << static_cast<int>(cue.tone.waveform)
        << '\t' << cue.tone.frequencyHz
        << '\t' << cue.tone.levelDbfs
        << '\t' << cue.tone.channel
        << '\t' << static_cast<int>(cue.tone.visual)
        << '\t' << (cue.tone.visualEnabled ? "1" : "0")
        << '\t' << static_cast<int>(cue.tone.synth.chip)
        << '\t' << cue.tone.synth.noteHz
        << '\t' << cue.tone.synth.attackSeconds
        << '\t' << cue.tone.synth.releaseSeconds
        << '\t' << cue.tone.synth.retriggerSeconds
        << '\t' << static_cast<int>(cue.tone.synth.carrier)
        << '\t' << static_cast<int>(cue.tone.synth.modulator)
        << '\t' << cue.tone.synth.modDepth
        << '\t' << cue.tone.synth.modRatio
        << '\t' << static_cast<int>(cue.tone.synth.nesVoice)
        << '\t' << static_cast<int>(cue.tone.synth.nesDuty)
        << '\t' << (cue.tone.synth.nesNoiseShort ? "1" : "0")
        << '\t' << (cue.tone.synth.nesQuantise ? "1" : "0")
        << '\t' << static_cast<int>(cue.tone.synth.tuning)
        << '\t' << cue.tone.synth.referenceHz
        << '\t' << static_cast<int>(cue.videoSynth.shape)
        << '\t' << static_cast<int>(cue.videoSynth.mirror)
        << '\t' << static_cast<int>(cue.videoSynth.palette)
        << '\t' << cue.videoSynth.speed
        << '\t' << cue.videoSynth.scale
        << '\t' << cue.videoSynth.warp
        << '\t' << cue.videoSynth.feedbackAmount
        << '\t' << cue.videoSynth.feedbackZoom
        << '\t' << cue.videoSynth.feedbackRotate
        << '\t' << cue.videoSynth.audioReactivity
        << '\t' << cue.videoSynth.resolution
        << '\t' << cue.videoSynth.pixelSort
        << '\t' << cue.videoSynth.glitch
        << '\t' << (cue.videoSynth.ascii ? "1" : "0")
        << '\t' << cue.videoSynth.asciiCols
        << '\t' << (cue.videoSynth.asciiGreen ? "1" : "0")
        << '\t' << cue.videoSynth.crt
        << '\t' << cue.videoSynth.asciiCharSet
        << '\t' << cue.videoSynth.asciiShuffle
        << '\t' << cue.videoSynth.asciiInk
        << '\t' << escapeField(cue.videoSynth.spriteSheetPath)
        << '\t' << cue.videoSynth.spriteTileW
        << '\t' << cue.videoSynth.spriteTileH
        << '\t' << cue.videoSynth.spriteRotate
        << '\t' << cue.videoSynth.spriteFreeAngle
        << '\t' << cue.videoSynth.spriteFlip
        << '\t' << cue.videoSynth.spriteJitter
        << '\t' << cue.videoSynth.spriteChaos
        << '\t' << escapeField(cue.timer.logoPath)
        << '\t' << cue.timer.logoHeightPercent
        // Effect stack, appended at the END like every other addition.
        // Packed into ONE field as "token:amount:a:b|..." so a stack of any
        // length costs a single column -- a variable number of columns would
        // shift every positional index after it, which is the trap the loader
        // offsets already carry scars from.
        << '\t' << escapeField(serializeCueEffects(cue.effects))
        << '\t' << escapeField(cue.motionDriverPath)
        << '\t' << cue.motionDriverSpeed
        << '\t' << (cue.motionDriverPaused ? 1 : 0)
        << '\t' << (cue.motionDriverRestartOnTake ? 1 : 0)
        << '\n';
    }
  }

  return true;
}

// onProgress (optional) receives 0..1 as the file is consumed, so a caller can
// draw a loading overlay while this blocks. Progress is measured in BYTES READ
// rather than lines, because a show's line count isn't known until it has been
// read — and byte position is exact and free.
Project loadProject(const fs::path& projectFile,
                    const std::function<void(double)>& onProgress = {}) {
  Project project;
  fs::path resolved = projectFile.empty() ? Paths::defaultProjectFile() : projectFile;
  std::ifstream input(resolved, std::ios::binary);
  if (!input) {
    return project;
  }
  std::uintmax_t totalBytes = 0;
  {
    std::error_code ec;
    totalBytes = fs::file_size(resolved, ec);
    if (ec) {
      totalBytes = 0;
    }
  }
  std::size_t lineCounter = 0;

  project.decks.clear();
  project.decks.push_back(Deck {});
  project.outputs.clear();

  auto ensureDeck = [&](int deckIndex) -> Deck& {
    int normalizedIndex = std::max(0, deckIndex);
    while (normalizedIndex >= static_cast<int>(project.decks.size())) {
      Deck deck;
      deck.name = deckDefaultName(static_cast<int>(project.decks.size()));
      project.decks.push_back(deck);
    }
    return project.decks[normalizedIndex];
  };

  std::string line;
  while (std::getline(input, line)) {
    // Report every 64 lines: often enough to animate, rare enough that tellg()
    // and the callback cost nothing on a small show.
    if (onProgress && totalBytes > 0 && (++lineCounter & 63u) == 0u) {
      const std::streampos pos = input.tellg();
      if (pos >= 0) {
        onProgress(static_cast<double>(pos) / static_cast<double>(totalBytes));
      }
    }
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    auto fields = splitEscapedTabs(line);
    if (fields.empty()) {
      continue;
    }

    if (fields[0] == "title") {
      project.title = safeString(fields, 1);
    } else if (fields[0] == "focused_deck") {
      project.focusedDeckIndex = safeInt(fields, 1, 0);
    } else if (fields[0] == "focused_output") {
      project.focusedOutputIndex = safeInt(fields, 1, 0);
    } else if (fields[0] == "focused_group" || fields[0] == "layer_names") {
      // Legacy fields — ignored (single-deck, no layer assignments or group presets).
    } else if (fields[0] == "advanced_mode") {
      project.advancedOutputMode = safeBool(fields, 1, false);
    } else if (fields[0] == "ptp_domain") {
      project.ptpDomain = std::clamp(safeInt(fields, 1, 127), 0, 127);
    } else if (fields[0] == "nmos_enabled") {
      project.nmosEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "nmos_registry") {
      project.nmosRegistryUrl = safeString(fields, 1);
    } else if (fields[0] == "nmos_port") {
      project.nmosPort = std::clamp(safeInt(fields, 1, 3210), 1, 65535);
    } else if (fields[0] == "nmos_interface") {
      project.nmosInterfaceName = safeString(fields, 1);
    } else if (fields[0] == "ltc_out") {
      project.ltcOutputEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "ltc_out_device") {
      project.ltcOutputDeviceName = safeString(fields, 1);
    } else if (fields[0] == "ltc_out_fps") {
      project.ltcOutputFps = std::clamp(safeDouble(fields, 1, 30.0), 23.0, 60.0);
    } else if (fields[0] == "ltc_out_channel") {
      project.ltcOutputChannel = std::clamp(safeInt(fields, 1, 0), 0, 7);
    } else if (fields[0] == "ltc_out_channels") {
      project.ltcOutputChannelCount = std::clamp(safeInt(fields, 1, 2), 1, 8);
    } else if (fields[0] == "selected") {
      ensureDeck(0).selectedIndex = safeInt(fields, 1, -1);
    } else if (fields[0] == "active") {
      ensureDeck(0).activeIndex = safeInt(fields, 1, -1);
    } else if (fields[0] == "auto_advance") {
      // Legacy field: cue endings now follow per-cue hold/end settings.
    } else if (fields[0] == "playlist_loop") {
      ensureDeck(0).playlistLoop = safeBool(fields, 1, false);
    } else if (fields[0] == "ui_sounds") {
      project.uiSoundsEnabled = safeBool(fields, 1, true);
    } else if (fields[0] == "ui_transitions") {
      project.uiTransitionsEnabled = safeBool(fields, 1, true);
    } else if (fields[0] == "hap_suggestion_dismissed") {
      project.hapSuggestionDismissed = safeBool(fields, 1, false);
    } else if (fields[0] == "synth_keyboard") {
      project.synthKeyboardEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "synth_octave") {
      project.synthKeyboardOctave = std::clamp(safeInt(fields, 1, 4), 0, 8);
    } else if (fields[0] == "midi_to_synth") {
      project.midiToSynth = safeBool(fields, 1, false);
    } else if (fields[0] == "audio_input_device") {
      project.audioInputDeviceName = safeString(fields, 1);
    } else if (fields[0] == "audio_input_enabled") {
      project.audioInputEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "audio_input_mono") {
      project.audioInputMono = safeBool(fields, 1, true);
    } else if (fields[0] == "audio_input_to_program") {
      project.audioInputToProgram = safeBool(fields, 1, true);
    } else if (fields[0] == "audio_input_gain_db") {
      project.audioInputGainDb = std::clamp(safeDouble(fields, 1, 0.0), -40.0, 40.0);
    } else if (fields[0] == "asio_driver") {
      project.asioDriverName = safeString(fields, 1);
    } else if (fields[0] == "asio_channels") {
      project.asioChannels = std::clamp(safeInt(fields, 1, 2), 2, 64);
    } else if (fields[0] == "recording_dir") {
      project.recordingDir = safeString(fields, 1);
    } else if (fields[0] == "recording_width") {
      project.recordingWidth = safeInt(fields, 1, 0);
    } else if (fields[0] == "recording_height") {
      project.recordingHeight = safeInt(fields, 1, 0);
    } else if (fields[0] == "recording_fps") {
      project.recordingFps = safeDouble(fields, 1, 0.0);
    } else if (fields[0] == "recording_codec") {
      project.recordingCodec = safeString(fields, 1);
    } else if (fields[0] == "recording_tc_mode") {
      project.recordingTimecodeMode = safeString(fields, 1);
    } else if (fields[0] == "recording_tc_start") {
      project.recordingTimecodeStart = safeString(fields, 1);
    } else if (fields[0] == "recording_tc_df") {
      project.recordingTimecodeDropFrame = safeString(fields, 1);
    } else if (fields[0] == "recording_segment_minutes") {
      project.recordingSegmentMinutes = safeInt(fields, 1, 0);
    } else if (fields[0] == "recording_segment_mb") {
      project.recordingSegmentMegabytes = safeInt(fields, 1, 0);
    } else if (fields[0] == "recording_remux") {
      project.recordingRemuxOnStop = safeBool(fields, 1, true);
    } else if (fields[0] == "splash_character") {
      std::string v = safeString(fields, 1);
      project.splashCharacter = v.empty() ? std::string("deckbot") : v;
    } else if (fields[0] == "theme") {
      project.theme = safeString(fields, 1);
    } else if (fields[0] == "terrarium_unlocked") {
      project.terrariumUnlocked = safeBool(fields, 1, false);
    } else if (fields[0] == "geometry_aspect_link") {
      project.geometryAspectLinked = safeBool(fields, 1, true);
    } else if (fields[0] == "vj_mode") {
      project.vjModeEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "vj_deck_a") {
      project.vjDeckA = std::max(0, safeInt(fields, 1, 0));
    } else if (fields[0] == "vj_deck_b") {
      project.vjDeckB = std::max(0, safeInt(fields, 1, 1));
    } else if (fields[0] == "vj_mix") {
      project.vjMixPosition = std::clamp(safeDouble(fields, 1, 0.0), 0.0, 1.0);
    } else if (fields[0] == "vj_blend") {
      const std::string mode = safeString(fields, 1);
      project.vjBlendMode = (mode == "add" || mode == "multiply") ? mode : "dissolve";
    } else if (fields[0] == "vj_bpm") {
      project.vjTempoBpm = std::clamp(safeDouble(fields, 1, 120.0), 20.0, 300.0);
    } else if (fields[0] == "vj_quantise") {
      project.vjQuantiseTakes = safeBool(fields, 1, false);
    } else if (fields[0] == "ui_scale") {
      project.uiScale = std::clamp(safeDouble(fields, 1, 1.0), 0.75, 3.0);
    } else if (fields[0] == "interaction_mode") {
      std::string v = safeString(fields, 1);
      project.interactionMode = (v == "touch") ? "touch" : "mouse";
    } else if (fields[0] == "allow_remote_network") {
      project.allowRemoteNetwork = safeBool(fields, 1, false);
    } else if (fields[0] == "osc_query_enabled") {
      project.oscQueryEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "osc_query_port") {
      project.oscQueryPort = safeInt(fields, 1, 5511);
    } else if (fields[0] == "osc_feedback_mirror") {
      project.oscFeedbackMirrorEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "osc_feedback_rate_ms") {
      project.oscFeedbackRateMs = safeInt(fields, 1, 120);
    } else if (fields[0] == "integration_atem_trigger") {
      project.atemTriggerEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "integration_ndi_trigger") {
      project.ndiTriggerEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "integration_nmc_sync") {
      project.nmcSyncEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "integration_mtc_ingest") {
      project.mtcIngestEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "integration_ltc_ingest") {
      project.ltcIngestEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "integration_dmx_artnet") {
      project.dmxArtNetEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "integration_artnet_port") {
      project.artNetPort = safeInt(fields, 1, 6454);
    } else if (fields[0] == "integration_tsl_tally") {
      project.tslTallyEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "integration_tsl_port") {
      project.tslTallyPort = std::clamp(safeInt(fields, 1, 5800), 1, 65535);
    } else if (fields[0] == "integration_tsl_address") {
      { std::string v = safeString(fields, 1); project.tslTallyAddress = v.empty() ? "255.255.255.255" : v; }
    } else if (fields[0] == "audio_buffer_samples") {
      int v = safeInt(fields, 1, 1024);
      // Snap to valid sizes only
      if (v <= 256) v = 256; else if (v <= 512) v = 512; else if (v <= 1024) v = 1024; else v = 2048;
      project.audioBufferSamples = v;
    } else if (fields[0] == "audio_delay_ms") {
      project.audioDelayMs = std::clamp(safeInt(fields, 1, 0), 0, 1000);
    } else if (fields[0] == "jump_mode") {
      project.jumpMode = normalizeJumpModeToken(safeString(fields, 1));
    } else if (fields[0] == "jump_transition") {
      project.jumpTransitionEnabled = safeBool(fields, 1, true);
    } else if (fields[0] == "panic_profile") {
      project.panicProfile = normalizePanicProfileToken(safeString(fields, 1));
    } else if (fields[0] == "panic_fade_seconds") {
      project.panicFadeSeconds = safeDouble(fields, 1, 0.9);
    } else if (fields[0] == "panic_auto_restore") {
      project.panicAutoRestore = safeBool(fields, 1, false);
    } else if (fields[0] == "master_volume") {
      // Range is 0..2 (values above 1.0 are boost) — the old 0..1 clamp here
      // silently flattened saved boost levels on load.
      project.masterVolume = std::clamp(safeDouble(fields, 1, 1.0), 0.0, 2.0);
    } else if (fields[0] == "master_dimmer") {
      project.masterDimmer = std::clamp(safeDouble(fields, 1, 1.0), 0.0, 1.0);
    } else if (fields[0] == "output_follow_display") {
      project.outputFollowDisplay = safeBool(fields, 1, true);
    } else if (fields[0] == "output_render_width") {
      project.outputRenderWidth = safeInt(fields, 1, 1920);
    } else if (fields[0] == "output_render_height") {
      project.outputRenderHeight = safeInt(fields, 1, 1080);
    } else if (fields[0] == "output_refresh_hz") {
      project.outputRefreshRateHz = std::max(0.0, safeDouble(fields, 1, 0.0));
    } else if (fields[0] == "output_bit_depth") {
      project.outputBitDepth = safeInt(fields, 1, 0);
    } else if (fields[0] == "output_canvas_enabled") {
      project.outputCanvasEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "output_canvas_width") {
      project.outputCanvasWidth = safeInt(fields, 1, 3840);
    } else if (fields[0] == "output_canvas_height") {
      project.outputCanvasHeight = safeInt(fields, 1, 2160);
    } else if (fields[0] == "output_target") {
      int outputIndex = safeInt(fields, 1, static_cast<int>(project.outputs.size()));
      int normalizedIndex = std::max(0, outputIndex);
      while (normalizedIndex >= static_cast<int>(project.outputs.size())) {
        project.outputs.push_back(OutputTarget {});
      }
      OutputTarget& outputTarget = project.outputs[normalizedIndex];
      outputTarget.name = safeString(fields, 2);
      outputTarget.hostDeckIndex = safeInt(fields, 3, 0);
      outputTarget.displayIndex = safeInt(fields, 4, 0);
      outputTarget.enabled = safeBool(fields, 5, false);
      outputTarget.streamEnabled = safeBool(fields, 6, false);
      outputTarget.streamProtocol = safeString(fields, 7);
      outputTarget.streamUrl = safeString(fields, 8);
      outputTarget.streamBitrateKbps = safeInt(fields, 9, 6000);
      if (fields.size() >= 17) {
        outputTarget.ndiEnabled = safeBool(fields, 10, false);
        outputTarget.ndiSourceName = safeString(fields, 11);
        outputTarget.ndiKeyEnabled = safeBool(fields, 12, false);
        outputTarget.ndiKeySourceName = safeString(fields, 13);
        outputTarget.outputType = safeString(fields, 14);
        outputTarget.mirrorSourceOutputIndex = safeInt(fields, 15, -1);
        outputTarget.outputId = safeString(fields, 16);
        if (fields.size() >= 21) {
          outputTarget.outputAlpha = static_cast<float>(safeDouble(fields, 17, 1.0));
          outputTarget.outputDelayMs = safeInt(fields, 18, 0);
          outputTarget.outputTimeOverlayEnabled = safeBool(fields, 19, false);
          outputTarget.outputColorSpace = safeString(fields, 20);
          if (fields.size() >= 24) {
            outputTarget.outputLayoutMode = safeString(fields, 21);
            outputTarget.outputOrientationDegrees = safeInt(fields, 22, 0);
            outputTarget.outputTestCardEnabled = safeBool(fields, 23, false);
            if (fields.size() >= 28) {
              outputTarget.deckLinkEnabled = safeBool(fields, 24, false);
              outputTarget.deckLinkDeviceId = safeInt(fields, 25, -1);
              outputTarget.deckLinkMode = safeString(fields, 26);
              outputTarget.deckLink10Bit = safeBool(fields, 27, true);
              if (fields.size() >= 32) {
                outputTarget.aoiLeft   = static_cast<float>(safeDouble(fields, 28, 0.0));
                outputTarget.aoiRight  = static_cast<float>(safeDouble(fields, 29, 0.0));
                outputTarget.aoiTop    = static_cast<float>(safeDouble(fields, 30, 0.0));
                outputTarget.aoiBottom = static_cast<float>(safeDouble(fields, 31, 0.0));
                if (fields.size() >= 34) {
                  outputTarget.spoutEnabled = safeBool(fields, 32, false);
                  outputTarget.spoutSenderName = safeString(fields, 33);
                  if (fields.size() >= 35) {
                    outputTarget.streamKey = safeString(fields, 34);
                    if (fields.size() >= 36) {
                      outputTarget.displayName = safeString(fields, 35);
                      if (fields.size() >= 41) {
                        outputTarget.st2110Enabled = safeBool(fields, 36, false);
                        outputTarget.st2110Address = safeString(fields, 37);
                        outputTarget.st2110Interface = safeString(fields, 38);
                        outputTarget.st2110Port = safeInt(fields, 39, 20000);
                        outputTarget.st2110TenBit = safeBool(fields, 40, true);
                        if (trim(outputTarget.st2110Address).empty()) {
                          outputTarget.st2110Address = "239.20.10.1";
                        }
                        if (fields.size() >= 46) {
                          outputTarget.srtLatencyMs = std::clamp(safeInt(fields, 41, 120), 20, 8000);
                          outputTarget.srtPassphrase = safeString(fields, 42);
                          outputTarget.srtStreamId = safeString(fields, 43);
                          outputTarget.srtMode =
                            (safeString(fields, 44) == "listener") ? "listener" : "caller";
                          outputTarget.streamKeyframeSeconds =
                            std::clamp(safeInt(fields, 45, 2), 1, 10);
                          // Appended after the keyframe field; older
                          // shows take the previous hardcoded 160.
                          outputTarget.streamAudioBitrateKbps =
                            std::clamp(safeInt(fields, 46, 160), 32, 512);
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      } else {
        // Backward compatibility with older 13-column output_target lines.
        outputTarget.outputType = safeString(fields, 10);
        outputTarget.mirrorSourceOutputIndex = safeInt(fields, 11, -1);
        outputTarget.outputId = safeString(fields, 12);
      }
    } else if (fields[0] == "layer_assignment" || fields[0] == "group_preset" ||
               fields[0] == "group_slot" || fields[0] == "group_preset_meta") {
      // Legacy fields — ignored (single-deck, no layer assignments or group presets).
    } else if (fields[0] == "audio_output") {
      ensureDeck(0).audioOutputDeviceName = safeString(fields, 1);
    } else if (fields[0] == "display_index") {
      ensureDeck(0).outputDisplayIndex = safeInt(fields, 1, 0);
    } else if (fields[0] == "ndi_enabled") {
      ensureDeck(0).ndiEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "ndi_name") {
      ensureDeck(0).ndiSourceName = safeString(fields, 1);
    } else if (fields[0] == "ndi_key_enabled") {
      ensureDeck(0).ndiKeyEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "ndi_key_name") {
      ensureDeck(0).ndiKeySourceName = safeString(fields, 1);
    } else if (fields[0] == "time_overlay") {
      ensureDeck(0).timeOverlayEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "transition_seconds") {
      ensureDeck(0).transitionSeconds = std::max(0.0, safeDouble(fields, 1, 0.0));
    } else if (fields[0] == "transition_style") {
      ensureDeck(0).transitionStyle = safeString(fields, 1);
    } else if (fields[0] == "timecode_chase") {
      ensureDeck(0).timecodeChaseEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "timecode_run") {
      ensureDeck(0).timecodeRunEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "timecode_trigger") {
      ensureDeck(0).timecodeTriggerEnabled = safeBool(fields, 1, true);
    } else if (fields[0] == "timecode_jam") {
      ensureDeck(0).timecodeJamSyncEnabled = safeBool(fields, 1, true);
    } else if (fields[0] == "timecode_freewheel") {
      ensureDeck(0).timecodeFreewheelSeconds = safeDouble(fields, 1, 1.0);
    } else if (fields[0] == "timecode_fps") {
      ensureDeck(0).timecodeFps = safeDouble(fields, 1, 30.0);
    } else if (fields[0] == "timecode_current") {
      ensureDeck(0).timecodeCurrentSeconds = std::max(0.0, safeDouble(fields, 1, 0.0));
    } else if (fields[0] == "deck") {
      int deckIndex = safeInt(fields, 1, static_cast<int>(project.decks.size()) - 1);
      Deck& deck = ensureDeck(deckIndex);
      deck.name = safeString(fields, 2);
      deck.selectedIndex = safeInt(fields, 3, -1);
      deck.activeIndex = safeInt(fields, 4, -1);
      deck.playlistLoop = safeBool(fields, 6, false);
      deck.audioOutputDeviceName = safeString(fields, 7);
      deck.outputDisplayIndex = safeInt(fields, 8, 0);
      deck.ndiEnabled = safeBool(fields, 9, false);
      deck.ndiSourceName = safeString(fields, 10);
      deck.timeOverlayEnabled = safeBool(fields, 11, false);
      deck.transitionSeconds = std::max(0.0, safeDouble(fields, 12, 0.0));
      deck.transitionStyle = safeString(fields, 13);
      deck.timecodeChaseEnabled = safeBool(fields, 14, false);
      deck.timecodeRunEnabled = safeBool(fields, 15, false);
      deck.timecodeTriggerEnabled = safeBool(fields, 16, true);
      deck.timecodeFps = safeDouble(fields, 17, 30.0);
      deck.timecodeCurrentSeconds = std::max(0.0, safeDouble(fields, 18, 0.0));
      deck.timecodeLastSeconds = deck.timecodeCurrentSeconds;
      deck.shuffle = safeBool(fields, 19, false);
      deck.ndiKeyEnabled = safeBool(fields, 20, false);
      deck.ndiKeySourceName = safeString(fields, 21);
      deck.canvasViewX = safeInt(fields, 22, 0);
      deck.canvasViewY = safeInt(fields, 23, 0);
      deck.warpEnabled = safeBool(fields, 24, false);
      size_t warpFieldOffset = 0;
      if (fields.size() >= 56) {
        deck.warpMode = safeString(fields, 25);
        warpFieldOffset = 1;
      }
      deck.warpTopLeftX = static_cast<float>(safeDouble(fields, 25 + warpFieldOffset, 0.0));
      deck.warpTopLeftY = static_cast<float>(safeDouble(fields, 26 + warpFieldOffset, 0.0));
      deck.warpTopRightX = static_cast<float>(safeDouble(fields, 27 + warpFieldOffset, 0.0));
      deck.warpTopRightY = static_cast<float>(safeDouble(fields, 28 + warpFieldOffset, 0.0));
      deck.warpBottomRightX = static_cast<float>(safeDouble(fields, 29 + warpFieldOffset, 0.0));
      deck.warpBottomRightY = static_cast<float>(safeDouble(fields, 30 + warpFieldOffset, 0.0));
      deck.warpBottomLeftX = static_cast<float>(safeDouble(fields, 31 + warpFieldOffset, 0.0));
      deck.warpBottomLeftY = static_cast<float>(safeDouble(fields, 32 + warpFieldOffset, 0.0));
      deck.edgeBlendLeft = static_cast<float>(safeDouble(fields, 33 + warpFieldOffset, 0.0));
      deck.edgeBlendRight = static_cast<float>(safeDouble(fields, 34 + warpFieldOffset, 0.0));
      deck.edgeBlendTop = static_cast<float>(safeDouble(fields, 35 + warpFieldOffset, 0.0));
      deck.edgeBlendBottom = static_cast<float>(safeDouble(fields, 36 + warpFieldOffset, 0.0));
      deck.outputRouteDeckIndex = safeInt(fields, 37 + warpFieldOffset, deckIndex);
      (void)safeInt(fields, 38 + warpFieldOffset, 0);
      deck.timecodeFreewheelSeconds = safeDouble(fields, 39 + warpFieldOffset, 1.0);
      deck.timecodeJamSyncEnabled = safeBool(fields, 40 + warpFieldOffset, true);
      deck.playlistOpacity = static_cast<float>(safeDouble(fields, 41 + warpFieldOffset, 1.0));
      deck.playlistAutoFade = safeBool(fields, 42 + warpFieldOffset, false);
      deck.playlistFadeSeconds = safeDouble(fields, 43 + warpFieldOffset, 0.8);
      deck.playlistTimebaseFps = safeDouble(fields, 44 + warpFieldOffset, deck.timecodeFps);
      deck.playlistStartOffsetSeconds = safeDouble(fields, 45 + warpFieldOffset, 0.0);
      deck.playlistDefaultCueFadeSeconds = safeDouble(fields, 46 + warpFieldOffset, 1.5);
      deck.playlistDefaultStillDurationSeconds = safeDouble(fields, 47 + warpFieldOffset, 8.0);
      deck.playlistDefaultLoop = safeBool(fields, 48 + warpFieldOffset, false);
      deck.playlistDefaultFadeInEnabled = safeBool(fields, 49 + warpFieldOffset, true);
      deck.playlistDefaultFadeOutEnabled = safeBool(fields, 50 + warpFieldOffset, true);
      deck.playlistDefaultAudioEnabled = safeBool(fields, 51 + warpFieldOffset, true);
      deck.playlistDefaultPauseAtBeginning = safeBool(fields, 52 + warpFieldOffset, false);
      deck.playlistDefaultPauseAtEnd = safeBool(fields, 53 + warpFieldOffset, true);
      deck.playlistDefaultTransitionToNext = safeBool(fields, 54 + warpFieldOffset, true);
      deck.audioOutputChannels = std::clamp(safeInt(fields, 55 + warpFieldOffset, 2), 2, 8);
    } else if (fields[0] == "cue") {
      int deckIndex = 0;
      size_t offset = 1;
      if (fields.size() >= 19) {
        try {
          deckIndex = std::stoi(fields[1]);
          offset = 2;
        } catch (...) {
          deckIndex = 0;
          offset = 1;
        }
      }

      Cue cue;
      cue.path = safeString(fields, offset + 0);
      cue.name = safeString(fields, offset + 1);
      std::string kind = safeString(fields, offset + 2);
      cue.kind =
        kind == "image" ? CueKind::Image :
        kind == "pattern" ? CueKind::Pattern :
        kind == "browser" ? CueKind::Browser :
        (kind == "window_source" || kind == "window") ? CueKind::WindowSource :
        kind == "camera" ? CueKind::Camera :
        (kind == "syphon" || kind == "spout") ? CueKind::Syphon :
        kind == "srt_stream" ? CueKind::SrtStream :
        kind == "ndi_source" ? CueKind::NdiSource :
        kind == "pip" ? CueKind::Pip :
        kind == "lower_third" ? CueKind::LowerThird :
        kind == "composite" ? CueKind::Composite :
        kind == "audio" ? CueKind::Audio :
        // Timer, Tone and VideoSynth were missing here as well as in
        // cueKindToken, so even a correctly-written show came back with all
        // three demoted to Video. Keep this list in step with that function --
        // a kind that only one side knows about does not survive a round trip.
        kind == "timer" ? CueKind::Timer :
        kind == "tone" ? CueKind::Tone :
        (kind == "video_synth" || kind == "vsynth") ? CueKind::VideoSynth :
        CueKind::Video;
      // Repair shows written while the round trip was broken. cueKindToken was
      // missing SEVEN kinds, so each was saved as "video" while keeping its
      // real path -- and a Video cue pointed at timer:// can never play, it
      // just racks. v0.84.0 shipped this way, which means every Timer, Pip,
      // Composite, Camera, Window and Syphon cue in a show saved by it comes
      // back dead. The path is the surviving evidence of what the cue was, and
      // every affected kind has its own scheme, so all of them recover.
      if (cue.kind == CueKind::Video) {
        const std::string& p = cue.path;
        if (p.rfind("tone://", 0) == 0)               cue.kind = CueKind::Tone;
        else if (p.rfind("timer://", 0) == 0)         cue.kind = CueKind::Timer;
        else if (p.rfind("vsynth://", 0) == 0)        cue.kind = CueKind::VideoSynth;
        else if (p.rfind("graphic://pip", 0) == 0)    cue.kind = CueKind::Pip;
        else if (p.rfind("graphic://composite", 0) == 0) cue.kind = CueKind::Composite;
        else if (p.rfind("source://camera/", 0) == 0) cue.kind = CueKind::Camera;
        else if (p.rfind("source://window/", 0) == 0) cue.kind = CueKind::WindowSource;
        else if (p.rfind("source://syphon/", 0) == 0) cue.kind = CueKind::Syphon;
      }
      cue.duration = safeDouble(fields, offset + 3, 0.0);
      cue.width = safeInt(fields, offset + 4, 0);
      cue.height = safeInt(fields, offset + 5, 0);
      cue.fps = safeDouble(fields, offset + 6, cue.kind == CueKind::Video ? 30.0 : 0.0);
      cue.formatName = safeString(fields, offset + 7);
      cue.videoCodec = safeString(fields, offset + 8);
      cue.audioCodec = safeString(fields, offset + 9);
      cue.hasAudio = safeBool(fields, offset + 10, false);
      cue.sizeBytes = safeSize(fields, offset + 11, 0);
      cue.color = parseColor(safeString(fields, offset + 12));
      cue.fadeInSeconds = std::max(0.0, safeDouble(fields, offset + 13, 0.0));
      cue.fadeOutSeconds = std::max(0.0, safeDouble(fields, offset + 14, 0.0));
      cue.loop = safeBool(fields, offset + 15, false);
      cue.pauseOnLastFrame = safeBool(fields, offset + 16, false);
      cue.id = safeString(fields, offset + 17);
      cue.inPointSeconds = std::max(0.0, safeDouble(fields, offset + 18, 0.0));
      cue.outPointSeconds = std::max(0.0, safeDouble(fields, offset + 19, 0.0));
      cue.triggerTimecodeSeconds = safeDouble(fields, offset + 20, -1.0);
      cue.endAction = parseCueEndAction(safeString(fields, offset + 21));
      cue.cueTransitionSeconds = safeDouble(fields, offset + 22, -1.0);
      cue.cueTransitionStyle = safeString(fields, offset + 23);
      cue.lowerThirdText = safeString(fields, offset + 24);
      cue.lowerThirdSubtext = safeString(fields, offset + 25);
      cue.lowerThirdBgAlpha = safeInt(fields, offset + 26, 180);
      cue.stillDurationSeconds = std::max(0.0, safeDouble(fields, offset + 27, 0.0));
      cue.loopCount = safeInt(fields, offset + 28, 0);
      cue.playbackSpeed = std::clamp(safeDouble(fields, offset + 29, 1.0), 0.25, 4.0);
      cue.colorTag = safeString(fields, offset + 30);
      cue.notes = safeString(fields, offset + 31);
      cue.outputScaleX = static_cast<float>(std::clamp(safeDouble(fields, offset + 32, 1.0), 0.25, 4.0));
      cue.outputScaleY = static_cast<float>(std::clamp(safeDouble(fields, offset + 33, 1.0), 0.25, 4.0));
      cue.scaleMode = static_cast<ScaleMode>(safeInt(fields, offset + 34, 0));
      cue.outputOffsetX = static_cast<float>(safeDouble(fields, offset + 35, 0.0));
      cue.outputOffsetY = static_cast<float>(safeDouble(fields, offset + 36, 0.0));
      cue.cueNumber = safeString(fields, offset + 37);
      {
        std::string ppStr = safeString(fields, offset + 38);
        if (!ppStr.empty()) {
          std::istringstream ss(ppStr);
          std::string tok;
          while (std::getline(ss, tok, ',')) {
            try { cue.pausePoints.push_back(std::stod(tok)); } catch (...) {}
          }
          std::sort(cue.pausePoints.begin(), cue.pausePoints.end());
        }
      }
      cue.outputRotationDegrees = static_cast<float>(safeDouble(fields, offset + 39, 0.0));
      cue.cropLeft = static_cast<float>(safeDouble(fields, offset + 40, 0.0));
      cue.cropRight = static_cast<float>(safeDouble(fields, offset + 41, 0.0));
      cue.cropTop = static_cast<float>(safeDouble(fields, offset + 42, 0.0));
      cue.cropBottom = static_cast<float>(safeDouble(fields, offset + 43, 0.0));
      cue.chromaKeyEnabled = safeBool(fields, offset + 44, false);
      cue.chromaKeyColor = parseColor(safeString(fields, offset + 45));
      cue.chromaKeyTolerance = static_cast<float>(safeDouble(fields, offset + 46, 60.0));
      cue.chromaKeySoftness = static_cast<float>(safeDouble(fields, offset + 47, 20.0));
      cue.brightness = std::clamp(static_cast<float>(safeDouble(fields, offset + 48, 1.0)), 0.0f, 2.0f);
      cue.contrast = std::clamp(static_cast<float>(safeDouble(fields, offset + 49, 1.0)), 0.0f, 2.0f);
      cue.saturation = std::clamp(static_cast<float>(safeDouble(fields, offset + 50, 1.0)), 0.0f, 2.0f);
      cue.hueShift = std::clamp(static_cast<float>(safeDouble(fields, offset + 51, 0.0)), -180.0f, 180.0f);
      cue.cueId = normalizeCueIdShort(safeString(fields, offset + 52));
      cue.audioEnabled = safeBool(fields, offset + 53, true);
      cue.pauseAtBeginning = safeBool(fields, offset + 54, false);
      cue.transitionToNext = safeBool(fields, offset + 55, true);
      cue.gotoTarget = safeString(fields, offset + 56);
      cue.audioChannels = safeInt(fields, offset + 57, 0);
      cue.audioSampleRate = safeInt(fields, offset + 58, 0);
      cue.pipTargetCue = safeString(fields, offset + 59);
      cue.pipSourceType = safeString(fields, offset + 60);
      cue.attachedLowerThirdCue = safeString(fields, offset + 61);
      cue.attachedPipCue = safeString(fields, offset + 62);
      cue.compositeLayoutPreset = safeString(fields, offset + 63);
      cue.compositeAudioSlotId = safeString(fields, offset + 64);
      cue.compositeBackgroundColor = parseColor(safeString(fields, offset + 65));
      int compositeSlotCount = safeInt(fields, offset + 66, 0);
      size_t compositeBase = offset + 67;
      cue.compositeSlots.clear();
      cue.compositeSlots.reserve(std::max(0, compositeSlotCount));
      for (int slotIndex = 0; slotIndex < compositeSlotCount; ++slotIndex) {
        size_t slotOffset = compositeBase + static_cast<size_t>(slotIndex) * 11;
        if (slotOffset + 10 >= fields.size()) {
          break;
        }
        CompositeSlot slot;
        slot.id = safeString(fields, slotOffset + 0);
        slot.name = safeString(fields, slotOffset + 1);
        slot.sourceType = safeString(fields, slotOffset + 2);
        slot.source = safeString(fields, slotOffset + 3);
        slot.visible = safeBool(fields, slotOffset + 4, true);
        slot.audioEnabled = safeBool(fields, slotOffset + 5, false);
        slot.scaleMode = static_cast<ScaleMode>(safeInt(fields, slotOffset + 6, static_cast<int>(ScaleMode::Fit)));
        slot.normX = static_cast<float>(safeDouble(fields, slotOffset + 7, 0.0));
        slot.normY = static_cast<float>(safeDouble(fields, slotOffset + 8, 0.0));
        slot.normW = static_cast<float>(safeDouble(fields, slotOffset + 9, 0.5));
        slot.normH = static_cast<float>(safeDouble(fields, slotOffset + 10, 0.5));
        ensureCompositeSlotIdentity(slot, slotIndex);
        cue.compositeSlots.push_back(slot);
      }
      if (cue.kind == CueKind::Composite) {
        applyCompositePresetToCue(cue, cue.compositeLayoutPreset);
      }
      // Subtitle fields (after composite slots)
      size_t subtitleBase = compositeBase + static_cast<size_t>(compositeSlotCount) * 11;
      cue.subtitlePath = safeString(fields, subtitleBase + 0);
      cue.subtitleStreamId = safeString(fields, subtitleBase + 1);
      cue.subtitleEnabled = safeBool(fields, subtitleBase + 2, true);
      cue.refreshOnTake = safeBool(fields, subtitleBase + 3, false);
      cue.audioGainDb = std::clamp(static_cast<float>(safeDouble(fields, subtitleBase + 4, 0.0)),
                                   kCueAudioGainMinDb, kCueAudioGainMaxDb);
      cue.audioPan = std::clamp(static_cast<float>(safeDouble(fields, subtitleBase + 5, 0.0)), -1.0f, 1.0f);
      cue.audioMono = safeBool(fields, subtitleBase + 6, false);
      cue.audioFadeInSeconds = std::clamp(static_cast<float>(safeDouble(fields, subtitleBase + 7, -1.0)), -1.0f, 60.0f);
      cue.audioFadeOutSeconds = std::clamp(static_cast<float>(safeDouble(fields, subtitleBase + 8, -1.0)), -1.0f, 60.0f);
      cue.audioOutputPair = std::clamp(safeInt(fields, subtitleBase + 9, 0), 0, 7);
      // Appended after audioOutputPair; older saves simply lack them.
      cue.datamoshEnabled = safeBool(fields, subtitleBase + 10, false);
      cue.moshPath = safeString(fields, subtitleBase + 11);
      // Timer settings, appended after moshPath. Older saves lack them and
      // fall back to the struct defaults, which are a sane 5:00 / 60 / 15.
      {
        const std::size_t tb = subtitleBase + 12;
        cue.timer.durationSeconds = safeInt(fields, tb + 0, cue.timer.durationSeconds);
        cue.timer.amberSeconds    = safeInt(fields, tb + 1, cue.timer.amberSeconds);
        cue.timer.redSeconds      = safeInt(fields, tb + 2, cue.timer.redSeconds);
        cue.timer.countUpAfterZero = safeBool(fields, tb + 3, cue.timer.countUpAfterZero);
        cue.timer.blinkAtZero      = safeBool(fields, tb + 4, cue.timer.blinkAtZero);
        cue.timer.message          = safeString(fields, tb + 5);
        // Appended after message. These four were rendering but NOT persisting,
        // so a saved show lost its timer mode and face on reload.
        cue.timer.mode = static_cast<TimerMode>(std::clamp(safeInt(fields, tb + 6, 0), 0, 2));
        cue.timer.face = static_cast<TimerFace>(std::clamp(safeInt(fields, tb + 7, 0), 0, 1));
        cue.timer.showProgressBar = safeBool(fields, tb + 8, true);
        cue.timer.messageIsUrgent = safeBool(fields, tb + 9, false);
        cue.scheduledStartSeconds = safeDouble(fields, tb + 10, -1.0);
        parseMarkerTimes(cue, safeString(fields, tb + 11));
        parseMarkerNames(cue, safeString(fields, tb + 12));
        // Appended after the markers. Shows saved before the look existed were
        // all prepared with the CLASSIC recipe, which is this field's default,
        // so an old show keeps playing exactly as it did.
        cue.datamoshLook = std::clamp(safeInt(fields, tb + 13, kDatamoshLookClassic),
                                      0, kDatamoshLookCount - 1);
        // Timer colours and chimes, appended last. -1 keeps the built-in
        // colour, so a show saved before these existed looks unchanged.
        cue.timer.colorNormal     = safeInt(fields, tb + 14, -1);
        cue.timer.colorAmber      = safeInt(fields, tb + 15, -1);
        cue.timer.colorRed        = safeInt(fields, tb + 16, -1);
        cue.timer.colorBackground = safeInt(fields, tb + 17, -1);
        cue.timer.chimeAtAmber    = safeBool(fields, tb + 18, false);
        cue.timer.chimeAtRed      = safeBool(fields, tb + 19, false);
        cue.timer.chimeAtZero     = safeBool(fields, tb + 20, true);
        cue.timer.chimeSound      = std::clamp(safeInt(fields, tb + 21, 0), 0, 5);
        // Tone settings. Appended after the timer block; a show saved before
        // tone cues existed simply gets the defaults.
        cue.tone.waveform = static_cast<ToneWaveform>(
          std::clamp(safeInt(fields, tb + 22, 0), 0, 4));
        cue.tone.frequencyHz = std::clamp(safeDouble(fields, tb + 23, 1000.0), 20.0, 20000.0);
        cue.tone.levelDbfs = std::clamp(safeDouble(fields, tb + 24, -18.0), -60.0, -1.0);
        cue.tone.channel = std::clamp(safeInt(fields, tb + 25, -1), -1, 15);
        cue.tone.visual = static_cast<ToneVisual>(
          std::clamp(safeInt(fields, tb + 26, 1), 0, 3));
        cue.tone.visualEnabled = safeBool(fields, tb + 27, true);
        // Chip voice. Appended after the tone block; a show saved before the
        // synth existed takes the defaults. This is the THIRD time a cue field
        // shipped wired to state and effect but not to storage -- it works
        // perfectly until the show is reopened, which is the worst moment to
        // find out. Worth checking deliberately, not eventually.
        cue.tone.synth.chip = static_cast<SynthChip>(
          std::clamp(safeInt(fields, tb + 28, 0), 0, 1));
        cue.tone.synth.noteHz = std::clamp(safeDouble(fields, tb + 29, 220.0), 20.0, 8000.0);
        cue.tone.synth.attackSeconds = std::clamp(safeDouble(fields, tb + 30, 0.01), 0.0, 2.0);
        cue.tone.synth.releaseSeconds = std::clamp(safeDouble(fields, tb + 31, 0.30), 0.01, 4.0);
        cue.tone.synth.retriggerSeconds = std::clamp(safeDouble(fields, tb + 32, 0.0), 0.0, 4.0);
        cue.tone.synth.carrier = static_cast<FdsCarrier>(
          std::clamp(safeInt(fields, tb + 33, 0), 0, 4));
        cue.tone.synth.modulator = static_cast<FdsModulator>(
          std::clamp(safeInt(fields, tb + 34, 1), 0, 4));
        cue.tone.synth.modDepth = std::clamp(safeInt(fields, tb + 35, 16), 0, 63);
        cue.tone.synth.modRatio = std::clamp(safeDouble(fields, tb + 36, 0.5), 0.0, 8.0);
        cue.tone.synth.nesVoice = static_cast<NesVoice>(
          std::clamp(safeInt(fields, tb + 37, 0), 0, 2));
        cue.tone.synth.nesDuty = static_cast<NesDuty>(
          std::clamp(safeInt(fields, tb + 38, 2), 0, 3));
        cue.tone.synth.nesNoiseShort = safeBool(fields, tb + 39, false);
        cue.tone.synth.nesQuantise = safeBool(fields, tb + 40, true);
        cue.tone.synth.tuning = static_cast<SynthTuning>(
          std::clamp(safeInt(fields, tb + 64, 0), 0, 6));
        cue.tone.synth.referenceHz =
          std::clamp(safeDouble(fields, tb + 65, 440.0), 380.0, 480.0);
        // Video synth. Written at the same time as the feature rather than
        // discovered missing on reload, which is how the last three went.
        cue.videoSynth.shape = static_cast<VideoSynthShape>(
          std::clamp(safeInt(fields, tb + 41, 0), 0, 4));
        cue.videoSynth.mirror = static_cast<VideoSynthMirror>(
          std::clamp(safeInt(fields, tb + 42, 2), 0, 3));
        cue.videoSynth.palette = static_cast<VideoSynthPalette>(
          std::clamp(safeInt(fields, tb + 43, 0), 0, 4));
        cue.videoSynth.speed = std::clamp(safeDouble(fields, tb + 44, 1.0), 0.05, 8.0);
        cue.videoSynth.scale = std::clamp(safeDouble(fields, tb + 45, 1.0), 0.1, 8.0);
        cue.videoSynth.warp = std::clamp(safeDouble(fields, tb + 46, 0.35), 0.0, 2.0);
        cue.videoSynth.feedbackAmount = std::clamp(safeDouble(fields, tb + 47, 0.55), 0.0, 0.95);
        cue.videoSynth.feedbackZoom = std::clamp(safeDouble(fields, tb + 48, 1.02), 0.90, 1.15);
        cue.videoSynth.feedbackRotate = std::clamp(safeDouble(fields, tb + 49, 0.6), -10.0, 10.0);
        cue.videoSynth.audioReactivity = std::clamp(safeDouble(fields, tb + 50, 0.5), 0.0, 1.0);
        cue.videoSynth.resolution = std::clamp(safeInt(fields, tb + 51, 2), 1, 5);
        cue.videoSynth.pixelSort = std::clamp(safeDouble(fields, tb + 52, 0.0), 0.0, 1.0);
        cue.videoSynth.glitch = std::clamp(safeDouble(fields, tb + 53, 0.0), 0.0, 1.0);
        cue.videoSynth.ascii = safeBool(fields, tb + 54, false);
        cue.videoSynth.asciiCols = std::clamp(safeInt(fields, tb + 55, 80), 20, 200);
        cue.videoSynth.asciiGreen = safeBool(fields, tb + 56, true);
        cue.videoSynth.crt = std::clamp(safeDouble(fields, tb + 57, 0.0), 0.0, 1.0);
        cue.videoSynth.asciiCharSet = std::clamp(safeInt(fields, tb + 58, 0), 0, 5);
        cue.videoSynth.asciiShuffle = std::clamp(safeInt(fields, tb + 59, 0), 0, 8);
        // Older shows carry only the green boolean; map it onto the ink mode
        // so they reopen looking the way they were left.
        cue.videoSynth.asciiInk =
          std::clamp(safeInt(fields, tb + 60, cue.videoSynth.asciiGreen ? 1 : 0), 0, 5);
        cue.videoSynth.spriteSheetPath = safeString(fields, tb + 61);
        cue.videoSynth.spriteTileW = std::clamp(safeInt(fields, tb + 62, 16), 8, 128);
        cue.videoSynth.spriteTileH = std::clamp(safeInt(fields, tb + 63, 16), 8, 128);
        cue.videoSynth.spriteRotate = std::clamp(safeInt(fields, tb + 66, 0), 0, 5);
        cue.videoSynth.spriteFreeAngle =
          std::clamp(safeDouble(fields, tb + 67, 0.0), -720.0, 720.0);
        cue.videoSynth.spriteFlip = std::clamp(safeInt(fields, tb + 68, 0), 0, 3);
        cue.videoSynth.spriteJitter =
          std::clamp(safeDouble(fields, tb + 69, 0.0), 0.0, 1.0);
        cue.videoSynth.spriteChaos =
          std::clamp(safeDouble(fields, tb + 70, 0.0), 0.0, 1.0);
        // APPENDED, so they read from the END of the record — which is where
        // saveProject writes them. They were read from tb+22/tb+23 instead,
        // i.e. inserted into the MIDDLE, which silently shifted the loader's
        // view of every field after them by two: the whole tone block and all
        // ~47 video-synth fields. Symptom: a cue named "Test Tone 1kHz" came
        // back as 20Hz at -1.0dBFS, because frequencyHz was reading the
        // channel column (-1, clamped up to 20) and levelDbfs was reading the
        // visual column (1, clamped down to -1).
        //
        // Append new cue fields at the END and read them at the END. Inserting
        // mid-record corrupts everything downstream of the insertion.
        cue.timer.logoPath          = safeString(fields, tb + 71);
        cue.timer.logoHeightPercent = std::clamp(safeInt(fields, tb + 72, 18), 2, 40);
        // At the END, per the warning above. Absent on every show saved before
        // effects existed, which safeString reports as empty and parses to an
        // empty stack -- so an old show simply has no effects, which is right.
        cue.effects = parseCueEffects(safeString(fields, tb + 73));
        cue.motionDriverPath = safeString(fields, tb + 74);
        cue.motionDriverSpeed = std::clamp(
          static_cast<float>(safeDouble(fields, tb + 75, 1.0)), 0.0f, 4.0f);
        cue.motionDriverPaused = safeBool(fields, tb + 76, false);
        cue.motionDriverRestartOnTake = safeBool(fields, tb + 77, true);
      }
      if (!cue.path.empty()) {
        if (cue.name.empty()) {
          cue.name = fs::path(cue.path).stem().string();
        }
        ensureDeck(deckIndex).cues.push_back(cue);
      }
    }
  }

  normalizeProject(project);
  return project;
}

bool cueUsesFilesystemMedia(const Cue& cue) {
  return cue.kind == CueKind::Video ||
         cue.kind == CueKind::Image ||
         cue.kind == CueKind::Audio ||
         (cue.kind == CueKind::Pip && pipSourceTypeTokenFromCue(cue) == "media");
}

// ── Cue path resolution helpers ──────────────────────────────────────────────
// These helpers resolve cue file paths relative to the project file location,
// handling both absolute and relative paths, URIs, and edge cases.

// Returns true if the path looks like a URI (contains "://", or starts with
// "file:", "about:", "data:"). Used to skip filesystem resolution for URIs.
bool pathLooksLikeUri(const std::string& value) {
  std::string trimmed = trim(value);
  if (trimmed.empty()) {
    return false;
  }
  if (trimmed.find("://") != std::string::npos) {
    return true;
  }
  return trimmed.rfind("file:", 0) == 0 ||
         trimmed.rfind("about:", 0) == 0 ||
         trimmed.rfind("data:", 0) == 0;
}

// Resolve a cue's media path to an absolute filesystem path. Relative paths
// are resolved against the project file's parent directory. Returns nullopt
// for non-filesystem cues (Browser, Pattern, etc.) and URI-based paths.
std::optional<fs::path> resolveCueFilesystemPath(const Cue& cue, const fs::path& projectFile) {
  if (!cueUsesFilesystemMedia(cue)) {
    return std::nullopt;
  }
  std::string raw = trim(cue.path);
  if (raw.empty() || pathLooksLikeUri(raw)) {
    return std::nullopt;
  }
  // weakly_canonical stats every path component, and the update tick resolves
  // the selected/active cues' paths every frame — against show media that
  // often lives on slow USB drives already saturated by the decoder. Memoize
  // per (raw path, project file): the result only depends on those inputs,
  // and a RELINK rewrites cue.path itself, which lands on a different key.
  // Shared with the engines' path-resolver callbacks, hence the mutex.
  static std::mutex cacheMutex;
  static std::unordered_map<std::string, fs::path> cache;
  std::string cacheKey = raw + '\n' + projectFile.string();
  {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto it = cache.find(cacheKey);
    if (it != cache.end()) {
      return it->second;
    }
  }
  fs::path path(raw);
  if (!path.is_absolute()) {
    std::error_code cwdEc;
    fs::path base = projectFile.has_parent_path()
      ? projectFile.parent_path()
      : fs::current_path(cwdEc);
    if (base.empty()) {
      base = fs::path(".");
    }
    path = base / path;
  }
  std::error_code ec;
  fs::path resolved = fs::weakly_canonical(path, ec);
  if (ec || resolved.empty()) {
    resolved = fs::absolute(path, ec);
    if (ec || resolved.empty()) {
      resolved = path;
    }
  }
  {
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (cache.size() > 8192) {  // bound: big shows are ~1.5k cues
      cache.clear();
    }
    cache.emplace(std::move(cacheKey), resolved);
  }
  return resolved;
}

std::string resolvedCueFilesystemPathString(const Cue& cue, const fs::path& projectFile) {
  if (auto resolved = resolveCueFilesystemPath(cue, projectFile)) {
    return resolved->string();
  }
  return cue.path;
}

std::string sanitizeBundleFilenameStem(std::string value) {
  value = trim(value);
  std::string out;
  out.reserve(value.size());
  for (unsigned char ch : value) {
    if (std::isalnum(ch) || ch == '-' || ch == '_') {
      out.push_back(static_cast<char>(ch));
    } else if (ch == ' ' || ch == '.') {
      out.push_back('_');
    }
  }
  if (out.empty()) {
    return "cue";
  }
  return out;
}


// ── Waveform analysis ───────────────────────────────────────────────────────
// Peak data for the audio waveform display in the cue inspector.
// Computed offline by running ffmpeg to extract PCM, then bucketing
// into per-channel peak arrays (typically 512 buckets).
struct WaveformPeaks {
  std::vector<float> left;       // Left channel peak amplitudes (0.0–1.0)
  std::vector<float> right;      // Right channel peak amplitudes (0.0–1.0)
  // True when the two channels are measurably different — i.e. the source is
  // really stereo, not mono upmixed by the "-ac 2" analysis decode. Drives
  // the split L/R waveform view even when cue metadata (audioChannels) is
  // missing, as it is on cues saved by older Deckboy versions.
  bool distinct = false;

  bool empty() const {
    return left.empty() && right.empty();
  }
};

// Extract embedded subtitles from a media file using ffmpeg, returning SRT text.
static std::string extractEmbeddedSubtitles(const std::string& mediaPath, const std::string& streamId) {
  std::string mapArg = streamId.empty() ? "0:s:0" : streamId;
  auto result = readAllText({
    "ffmpeg", "-v", "error", "-i", mediaPath,
    "-map", mapArg, "-f", "srt", "pipe:1"
  });
  return result.value_or("");
}

// Load subtitle track for a cue: external .srt file or embedded stream.
static deckboy::core::SubtitleTrack loadSubtitleTrack(const Cue& cue) {
  // Prefer external SRT file
  if (!cue.subtitlePath.empty()) {
    return deckboy::core::parseSrtFile(cue.subtitlePath);
  }
  // Fall back to embedded subtitle stream
  if (!cue.subtitleStreamId.empty() && !cue.path.empty()) {
    std::string srtText = extractEmbeddedSubtitles(cue.path, cue.subtitleStreamId);
    if (!srtText.empty()) {
      return deckboy::core::parseSrtText(srtText);
    }
  }
  return {};
}

// Offline waveform analysis: runs ffmpeg to extract stereo PCM and compute per-bucket peaks.
// Mono sources will simply produce identical left/right lanes after ffmpeg upmix.
static WaveformPeaks computeWaveformPeaks(const std::string& path, int numBuckets = 512) {
  // Use spawnProcess instead of popen to avoid shell interpolation and improve
  // portability.  The args vector passes the file path directly to ffmpeg with
  // no shell quoting needed.
  std::vector<std::string> args = {
    "ffmpeg", "-i", path,
    "-ac", "2", "-ar", "4000",
    "-f", "s16le", "-vn",
    "-loglevel", "quiet",
    "pipe:1"
  };

  ChildProcess proc;
  if (!spawnProcess(proc, args, SpawnOptions::pipedStdout())) return {};

  std::vector<int16_t> samples;
  constexpr size_t kChunk = 4096;
  int16_t buf[kChunk];
#ifdef _WIN32
  int bytesRead = 0;
  while ((bytesRead = _read(proc.readFd,
                            reinterpret_cast<char*>(buf),
                            static_cast<unsigned int>(sizeof(buf)))) > 0) {
    size_t sampleCount = static_cast<size_t>(bytesRead) / sizeof(int16_t);
    samples.insert(samples.end(), buf, buf + sampleCount);
    // Cap at 10 minutes of AUDIO. The old bound was `samples.size() > 4000*600`,
    // but the decode is forced to 2 channels, so that counted stereo SAMPLES —
    // 1.2M frames — and actually cut off at FIVE minutes. Every waveform for a
    // longer file silently showed only its first half.
    if (samples.size() > 4000u * 600u * 2u) break;
  }
#else
  ssize_t bytesRead = 0;
  while ((bytesRead = ::read(proc.readFd, buf, sizeof(buf))) > 0) {
    size_t sampleCount = static_cast<size_t>(bytesRead) / sizeof(int16_t);
    samples.insert(samples.end(), buf, buf + sampleCount);
    // Cap at 10 minutes of AUDIO. The old bound was `samples.size() > 4000*600`,
    // but the decode is forced to 2 channels, so that counted stereo SAMPLES —
    // 1.2M frames — and actually cut off at FIVE minutes. Every waveform for a
    // longer file silently showed only its first half.
    if (samples.size() > 4000u * 600u * 2u) break;
  }
#endif
  // ChildProcess destructor closes the pipe fd and reaps the child process.
  proc.stop();

  if (samples.size() < 2) return {};
  WaveformPeaks peaks;
  peaks.left.assign(numBuckets, 0.0f);
  peaks.right.assign(numBuckets, 0.0f);
  size_t frameCount = samples.size() / 2u;
  size_t perBucket = std::max<size_t>(1, frameCount / numBuckets);
  // Stereo-ness is judged on the raw samples, not the bucket peaks: a
  // centre-heavy music mix has near-identical L/R peak envelopes yet clearly
  // different waveforms, and the peaks alone under-report that.
  double sumAbsDiff = 0.0;
  double sumAbs = 0.0;
  for (int b = 0; b < numBuckets; ++b) {
    size_t startFrame = static_cast<size_t>(b) * perBucket;
    size_t endFrame = std::min(startFrame + perBucket, frameCount);
    float mxL = 0.0f;
    float mxR = 0.0f;
    for (size_t frame = startFrame; frame < endFrame; ++frame) {
      size_t sampleIndex = frame * 2u;
      float l = samples[sampleIndex + 0] / 32768.0f;
      float r = samples[sampleIndex + 1] / 32768.0f;
      mxL = std::max(mxL, std::abs(l));
      mxR = std::max(mxR, std::abs(r));
      sumAbsDiff += std::abs(l - r);
      sumAbs += std::abs(l) + std::abs(r);
    }
    peaks.left[static_cast<size_t>(b)] = mxL;
    peaks.right[static_cast<size_t>(b)] = mxR;
  }
  // Side-signal energy above 1% of total = real stereo content. Mono
  // upmixes measure exactly 0; even subtle stereo reverb tails clear 1%.
  peaks.distinct = sumAbs > 0.0 && (sumAbsDiff / sumAbs) > 0.01;
  return peaks;
}

// ════════════════════════════════════════════════════════════════════════════
// class App — The main Deckboy application class.
//
// Owns all SDL resources (windows, renderers, fonts, audio devices),
// project state, deck runtimes, output runtimes, integration threads,
// and the entire UI rendering pipeline. Methods are organized across
// .ipp files included within the class body for modularity.
//
// Lifecycle: App::init() → App::run() → App::shutdown()
// ════════════════════════════════════════════════════════════════════════════
class App {
 public:
  // Initialize SDL, create windows/renderers, load fonts, set up audio.
  bool init() {
#ifdef _WIN32
    // Restrict DLL search order to System32 by default — prevents DLL hijacking
    // via CWD, application directory, or user PATH for implicitly loaded libraries.
    // Explicit loads (NDI, LTC) use LoadLibraryW with candidates that include
    // absolute paths; the bare-name fallbacks still search the restricted set.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);

    // (SDL3 is per-monitor-v2 DPI aware on Windows by default; the SDL2
    // SDL_HINT_WINDOWS_DPI_AWARENESS hint is gone.)
    // Never minimize a fullscreen output when it loses focus. SDL's default
    // minimizes exclusive-fullscreen windows on focus loss, which turned the
    // operator's normal workflow into a fight: click the control window →
    // program output minimizes ("output frozen") → recovery re-raises it and
    // steals keyboard focus → repeat. A playout output must stay on the
    // program screen no matter where the operator's focus is.
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
#if DECKBOY_INPROC_DECODE
    // Create D3D11 renderer devices WITHOUT D3D11_CREATE_DEVICE_SINGLETHREADED
    // (SDL's default). The in-process d3d11va decoder shares the program
    // output's device from a decode thread; that requires the device to
    // accept multithread protection — on a single-threaded device the
    // ID3D10/11Multithread QI fails and zero-copy decode degrades to CPU
    // output (and actually sharing one would crash). Must be set before any
    // renderer is created.
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D_THREADSAFE, "1");
#endif
    // Opt out of background throttling. Windows 11 applies EcoQoS (reduced
    // CPU scheduling) and timer-resolution coalescing to processes whose
    // windows aren't focused — SDL_Delay(4) in the decode/audio loops
    // degrades to ~15ms, so playback stutters the moment the operator
    // focuses anything else. A playout engine must run full speed in the
    // background, always.
    {
      PROCESS_POWER_THROTTLING_STATE throttling {};
      throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
      throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
      throttling.StateMask = 0;  // 0 with the mask set = explicitly OFF
      SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
                            &throttling, sizeof(throttling));
#ifdef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
      throttling.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
      throttling.StateMask = 0;  // keep 1ms timers while unfocused (Win11)
      SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
                            &throttling, sizeof(throttling));
#endif
    }
    {
      WSADATA wsaData {};
      if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return false;
      }
    }
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
      std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
      return false;
    }
    if (!TTF_Init()) {
      std::cerr << "TTF_Init failed: " << SDL_GetError() << '\n';
      return false;
    }

    // Nearest-neighbour scaling is applied per texture via deckboyCreateTexture*
    // (SDL3 removed the global SDL_HINT_RENDER_SCALE_QUALITY hint).
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    if (const char* env = std::getenv("DECKBOY_UI_PROFILE"); env && *env) {
      std::string token = toLower(trim(env));
      uiProfileEnabled_ = !(token == "0" || token == "false" || token == "off" || token == "no");
    }
    if (uiProfileEnabled_) {
      uiProfileLog("ui profiling enabled (DECKBOY_UI_PROFILE)");
    }

    // Pre-clamp the initial size to the primary display so we never create a
    // window larger than a screen. The defaults (1760x1020) overflow a laptop
    // panel — a MacBook Air's logical screen is only ~1470-1710 points wide, so
    // the window opened "a tad big" and spilled off. SDL sizes and usable bounds
    // are both in points, so they compare directly.
    int startW = kControlWidth, startH = kControlHeight;
    {
      SDL_Rect usable{};
      SDL_DisplayID disp = SDL_GetPrimaryDisplay();
      if (disp != 0 && SDL_GetDisplayUsableBounds(disp, &usable) &&
          usable.w > 0 && usable.h > 0) {
        startW = std::min(startW, usable.w);
        startH = std::min(startH, usable.h);
      }
    }
    // CREATED HIDDEN, shown once there is a frame in it.
    //
    // A window exists the moment it is created, but it has nothing in it until
    // something is presented -- and everything between here and the first
    // present (renderer, fonts, themes, splash art, the show itself) takes long
    // enough to see. The operator got a black rectangle first and the splash
    // afterwards, which reads as the app hanging on launch.
    //
    // revealControlWindow() puts it up after the first present, so the first
    // thing on screen is the first thing drawn.
    controlWindow_ = SDL_CreateWindow(
      kAppTitle.data(),
      startW,
      startH,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
    );
    if (!controlWindow_) {
      std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    SDL_SetWindowPosition(controlWindow_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    applyDeckboyWindowIcon(controlWindow_);

    // Multi-display robust: after centering, re-clamp to the display the window
    // ACTUALLY landed on — the primary and the target can differ wildly (a small
    // Retina laptop panel plus a large external). Shrink-and-recenter if it
    // overflows, and cap the minimum size so the window can always fit this
    // screen (a fixed 1500x900 minimum could exceed a 13" panel outright).
    int minW = 1500, minH = 900;
    {
      SDL_Rect ub{};
      SDL_DisplayID wd = SDL_GetDisplayForWindow(controlWindow_);
      if (wd == 0) wd = SDL_GetPrimaryDisplay();
      if (wd != 0 && SDL_GetDisplayUsableBounds(wd, &ub) && ub.w > 0 && ub.h > 0) {
        int cw = 0, ch = 0;
        SDL_GetWindowSize(controlWindow_, &cw, &ch);
        int nw = std::min(cw, ub.w), nh = std::min(ch, ub.h);
        if (nw != cw || nh != ch) {
          SDL_SetWindowSize(controlWindow_, nw, nh);
          SDL_SetWindowPosition(controlWindow_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
        minW = std::min(minW, ub.w);
        minH = std::min(minH, ub.h);
      }
    }
    SDL_SetWindowMinimumSize(controlWindow_, minW, minH);

    controlRenderer_ = deckboyCreateRenderer(controlWindow_);
    if (!controlRenderer_) {
      std::cerr << "Renderer creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    // vsync is a per-renderer runtime property in SDL3 (was a creation flag).
    SDL_SetRenderVSync(controlRenderer_, 1);
    // NOTE: do NOT set SDL_RenderSetLogicalSize here. The control UI reflows to
    // the live window size every frame (layoutButtons + the SDL_GetWindowSize
    // calls in the render/overlay code), so a fixed logical size would clamp the
    // drawable canvas while the layout spread past it — pushing the bottom-right
    // controls off-screen. Reflow handles resize/fullscreen on its own.

    // Scanline overlay texture (1x4 pattern: 2 clear rows + 2 tinted rows)
    scanlineOverlay_ = deckboyCreateTexture(controlRenderer_,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, 1, 4);
    if (scanlineOverlay_) {
      Uint8 a = pal.scanlineAlpha;
      uint32_t pixels[4] = {0, 0, (uint32_t(a) << 24), (uint32_t(a) << 24)};
      SDL_UpdateTexture(scanlineOverlay_, nullptr, pixels, sizeof(uint32_t));
      SDL_SetTextureBlendMode(scanlineOverlay_, SDL_BLENDMODE_BLEND);
    }

    monitorsWindow_ = SDL_CreateWindow(
      "Deckboy Monitors",
      1280,
      800,
      SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE
    );
    if (monitorsWindow_) {
      applyDeckboyWindowIcon(monitorsWindow_);
      SDL_SetWindowMinimumSize(monitorsWindow_, 640, 400);
      monitorsRenderer_ = deckboyCreateRenderer(monitorsWindow_);
      if (!monitorsRenderer_) {
        SDL_DestroyWindow(monitorsWindow_);
        monitorsWindow_ = nullptr;
      }
    }

    // Fonts are loaded through loadFonts() so the same code path runs at
    // boot AND when the operator changes the UI scale at runtime.
    if (!loadFonts(1.0)) {
      std::cerr << "Font load failed: " << SDL_GetError() << '\n';
      return false;
    }

    Paths::ensureDataDir();
    loadThemeFromEnv();
    rebuildPalette();
    initUiAssetPackPaths();
    preloadUiAssets();
    currentProjectFile_ = startupProjectFile();
    // Startup is the case that matters most for the loading overlay — a big
    // show is usually opened by launching into it, not by using OPEN — and it
    // is the one path that does NOT go through openProjectFromPath. The
    // renderer exists by now (fonts and palette are up), so the overlay can
    // draw and present its own frames while this blocks.
    beginLoadingOverlay("OPENING SHOW", currentProjectFile_.filename().string());
    project_ = loadProject(currentProjectFile_, [this](double frac) {
      loadingOverlayProgress(frac * 0.9);
    });
    loadingOverlayProgress(0.95, "checking the show");
    normalizeProject(project_);
    endLoadingOverlay();
    // Presence scan runs async — a big playlist on a USB drive used to hold
    // the first frame hostage for seconds of black window before the splash.
    startMediaPresenceScanAsync(true);
    // Project may override the boot-time splash character (deckbot default).
    // Restore the saved audio devices. Both settings PERSISTED but neither was
    // ever re-applied, so an operator who armed ASIO or a microphone found it
    // silently back on the system device next launch -- the setting remembered,
    // the effect forgotten.
    if (project_.audioInputEnabled && !startAudioInput()) {
      project_.audioInputEnabled = false;
    }
    if (!project_.asioDriverName.empty() &&
        !armAsioOutput(project_.asioDriverName, project_.asioChannels)) {
      // Interface not plugged in yet: keep the CHOICE so it comes back when the
      // hardware does, rather than quietly forgetting what they picked.
      showLog("ASIO", "saved driver unavailable at boot: " + project_.asioDriverName);
    }
    resolveFirstRunFlag();
    refreshSplashAsset();
    // Project may also carry a non-1.0 UI scale (HiDPI / 4K / Pocket 3).
    applyUiScale();
    // Project may carry a saved color theme; apply it unless DECKBOY_THEME
    // was set (an explicit env override always wins at boot).
    {
      const char* themeEnv = std::getenv("DECKBOY_THEME");
      if ((!themeEnv || !*themeEnv) && !project_.theme.empty()) {
        loadTheme(project_.theme);
      }
    }
    disarmAllOutputsForStartup();
    // Output starts black — no cue is active until the operator takes one
    for (auto& deck : project_.decks) { deck.activeIndex = -1; }
    // Show startup dialog so operator can choose to load or start fresh
    showStartupDialog_ = true;
    showSplashOverlay_ = true;
    splashStartedAt_ = SDL_GetTicks();
    ensureUiAudioDevice();
    // Boot chiptune — rides the splash overlay; honors the bloops toggle.
    playStartupJingle();
    previewMediaEngine_ = std::make_unique<MediaEngine>(
      controlRenderer_,
      nullptr,
      MediaEngine::AudioTapCallback {},
      [this](const Cue& cue) {
        return resolvedCueFilesystemPathString(cue, currentProjectFile_);
      }
    );
    if (!rebuildDeckRuntimes()) {
      std::cerr << "Deck runtime creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    if (!rebuildOutputRuntimes()) {
      std::cerr << "Output runtime creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    onSelectionChanged();
    observedDisplayCount_ = deckboyGetNumVideoDisplays();
    refreshDisplayTopology(false);
    selectionChangedAt_ = SDL_GetTicks();
    lastUpdateTickMs_ = selectionChangedAt_;
    startCompanionControl();
    if (project_.oscQueryEnabled) {
      startOscQueryServer();
    }
    startHyperDeckServer();
    startIntegrationBridges();
    // Lay out against the actual (possibly display-clamped) window size, not the
    // nominal constant, so the first frame is already correct on small screens.
    {
      int lw = 0, lh = 0;
      SDL_GetWindowSize(controlWindow_, &lw, &lh);
      if (lw <= 0 || lh <= 0) { lw = kControlWidth; lh = kControlHeight; }
      layoutButtons(lw, lh);
    }
    return true;
  }

  void shutdown() {
    mediaScanGeneration_.fetch_add(1);  // signal a running scan worker to bail
    if (mediaScanThread_.joinable()) {
      mediaScanThread_.join();
    }
    stopIntegrationBridges();
    stopHyperDeckServer();
    stopMidiInput();
    stopOscQueryServer();
    // Before the output runtimes go away: this releases any IS-05 caller parked
    // in the patch handler, then joins the node's threads.
    shutdownNmosNode();
    stopCompanionControl();
    // PIP overlay engines hold textures on the control renderer. They live in a
    // member map, so without this they were destroyed by ~App — i.e. AFTER
    // SDL_DestroyRenderer and SDL_Quit below — and their destructors ran
    // SDL_DestroyTexture against a torn-down SDL.
    for (auto& [key, runtime] : pipOverlayRuntimes_) {
      (void) key;
      if (runtime.mediaEngine) {
        runtime.mediaEngine->detachAudioDevice();
        runtime.mediaEngine->stopAll();
      }
    }
    pipOverlayRuntimes_.clear();
    for (auto& runtime : deckRuntimes_) {
      destroyDeckRuntime(runtime);
    }
    deckRuntimes_.clear();
    for (auto& runtime : outputRuntimes_) {
      destroyOutputRuntime(runtime);
    }
    outputRuntimes_.clear();
#if defined(DECKBOY_HAS_NDI_SDK)
    ndiApi_.shutdown();
#endif
    ltcApi_.shutdown();
    if (uiAudioStream_) {
      SDL_DestroyAudioStream(uiAudioStream_);
      uiAudioStream_ = nullptr;
    }
    releaseFonts();
    if (thumbnailThread_.joinable()) {
      thumbnailProcess_.killProcessOnly();
      thumbnailThread_.join();
      thumbnailProcess_.stop();
    }
    if (selectedThumbnailTex_) {
      SDL_DestroyTexture(selectedThumbnailTex_);
      selectedThumbnailTex_ = nullptr;
    }
    if (timelineStripThread_.joinable()) {
      timelineStripProcess_.killProcessOnly();
      timelineStripThread_.join();
      timelineStripProcess_.stop();
    }
    if (timelineStripTex_) {
      SDL_DestroyTexture(timelineStripTex_);
      timelineStripTex_ = nullptr;
    }
    if (previewMediaEngine_) {
      previewMediaEngine_->detachAudioDevice();
      previewMediaEngine_->stopAll();
      previewMediaEngine_.reset();
    }
    clearPreviewCueTexture();
    previewCueKey_.clear();
    if (controlPreviewTex_) {
      SDL_DestroyTexture(controlPreviewTex_);
      controlPreviewTex_ = nullptr;
      controlPreviewTexW_ = 0;
      controlPreviewTexH_ = 0;
      controlPreviewTexFormat_ = 0;
    }
    releaseUiAssets();
    for (auto* t : monitorsOutputTextures_) { if (t) SDL_DestroyTexture(t); }
    monitorsOutputTextures_.clear();
    monitorsOutputTexW_.clear();
    monitorsOutputTexH_.clear();
    if (monitorsRenderer_) {
      SDL_DestroyRenderer(monitorsRenderer_);
      monitorsRenderer_ = nullptr;
    }
    closeDisplayIdentify();
    if (monitorsWindow_) {
      SDL_DestroyWindow(monitorsWindow_);
      monitorsWindow_ = nullptr;
    }
    if (scanlineOverlay_) {
      SDL_DestroyTexture(scanlineOverlay_);
      scanlineOverlay_ = nullptr;
    }
    if (controlRenderer_) {
      SDL_DestroyRenderer(controlRenderer_);
      controlRenderer_ = nullptr;
    }
    if (controlWindow_) {
      SDL_DestroyWindow(controlWindow_);
      controlWindow_ = nullptr;
    }
    TTF_Quit();
    // Last thing before the subsystem goes away: anything torn down after this
    // (a MediaEngine still owned by an App member, destroyed by ~App) must not
    // call into SDL. A field crash on macOS was exactly that — ~MediaEngine ->
    // stopAll -> SDL_ClearAudioStream, from runDeckboyMain's frame, i.e. after
    // shutdown() had already returned.
    MediaEngine::setSdlTornDown(true);
    SDL_Quit();
#ifdef _WIN32
    WSACleanup();
#endif
  }

  void run() {
    while (!gShouldQuit.load()) {
      auto frameStart = std::chrono::steady_clock::now();
      auto ms = [](const auto& duration) {
        return std::chrono::duration<double, std::milli>(duration).count();
      };
      processEvents();
      auto afterEvents = std::chrono::steady_clock::now();
      drainPickers();
      auto afterPickers = std::chrono::steady_clock::now();
      update();
      tickSoak();
      auto afterUpdate = std::chrono::steady_clock::now();
      render();
      auto afterRender = std::chrono::steady_clock::now();
      if (uiProfileEnabled_) {
        double frameMs = ms(afterRender - frameStart);
        if (frameMs > 50.0) {
          std::ostringstream line;
          line << std::fixed << std::setprecision(2)
               << "frame dt=" << frameMs << "ms"
               << " events=" << ms(afterEvents - frameStart) << "ms"
               << " update=" << ms(afterUpdate - afterPickers) << "ms"
               << " layout=" << lastUiLayoutMs_ << "ms"
               << " render=" << lastUiRenderMs_ << "ms";
          uiProfileLog(line.str());
        }
      }
      // Prevent CPU spin when vsync isn't gating (hidden window, browser cue, etc.)
      // The control window present is vsync-locked, so on a normal display this
      // floor never engages — it only bounds the loop when nothing is blocking
      // on a vblank. Keep it high (240 Hz) so high-refresh monitors (144/165/
      // 240 Hz) render stills and transitions at their full native rate via
      // vsync instead of being clamped down to 120.
      auto frameElapsed = std::chrono::steady_clock::now() - frameStart;
      constexpr auto kMinFrameTime = std::chrono::microseconds(1000000 / 240);
      if (frameElapsed < kMinFrameTime) {
        auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(kMinFrameTime - frameElapsed);
        Uint32 delayMs = static_cast<Uint32>(std::max<long long>(
          1, (remaining.count() + 999) / 1000));
        SDL_Delay(delayMs);
      }
    }
  }

  // Native SDL dialog results run their handlers here (importing, project load,
  // save, relink). A handler can throw — a filesystem error, a bad_alloc — and
  // an uncaught exception on the main thread is std::terminate, which Windows
  // reports as 0xC0000409 in ucrtbase: the app vanishes with no message.
  // Deckboy has crashed that way before. A dialog that fails must lose the
  // picked path, not the show.
  void drainPickers() {
    try {
      drainSdlDialogActions();
    } catch (const std::exception& e) {
      triggerToast(std::string("file dialog failed: ") + e.what());
    } catch (...) {
      triggerToast("file dialog failed");
    }
  }

  // ── Soak mode ─────────────────────────────────────────────────────────────
  static double processRssMb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc {};
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
      return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
    return 0.0;
#else
    std::ifstream statm("/proc/self/statm");
    long pages = 0;
    long resident = 0;
    if (statm >> pages >> resident) {
      return static_cast<double>(resident) * 4096.0 / (1024.0 * 1024.0);
    }
    return 0.0;
#endif
  }

  void enableSoakMode(double minutes) {
    soakMode_ = true;
    soakMinutes_ = minutes;
    // State dir, not the working directory: launched from Finder or a shortcut
    // the cwd is somewhere arbitrary (or unwritable) and the log vanishes.
    soakLogFile_.open(Paths::stateDir() / "deckboy-soak.log", std::ios::app);
  }

  // Dev/test conveniences (CLI: --import <file>, --settings [tab]) — import
  // media as if dropped on the window, and open the settings modal at boot
  // so scripted screenshots can reach it.
  void debugImportPath(const std::string& path) {
    // Scripted import goes straight to the deck — no splash, no startup menu.
    showStartupDialog_ = false;
    showSplashOverlay_ = false;
    handleDropFile(path.c_str());
  }

  // A show named on the command line — what the .deckboy file association and
  // "open with" hand us. Opening it is the whole point of the launch, so the
  // startup menu (which would offer to open something else) is skipped.
  void openProjectFromCommandLine(const fs::path& projectPath) {
    showStartupDialog_ = false;
    showSplashOverlay_ = false;
    std::error_code ec;
    fs::path resolved = fs::absolute(projectPath, ec);
    if (ec) {
      resolved = projectPath;
    }
    if (!fs::exists(resolved)) {
      triggerToast("show not found: " + resolved.filename().string());
      return;
    }
    openProjectFromPath(normalizeProjectPath(resolved));
  }

  // Scroll the cue inspector from the command line.
  //
  // Everything below the first screenful of the inspector -- effect
  // parameters, the chain clipboard, the motion driver's preview -- was
  // unverifiable from a script, because scripted input does not reach SDL3
  // (PostMessage clicks and keys were already known not to; synthesised wheel
  // events turn out not to either). The app already carries dev flags for
  // exactly this reason: --settings opens a settings tab, --pattern-dump
  // renders a pattern headless. This is the same idea for the inspector.
  void debugScrollInspector(int pixels) {
    // Held, not applied. The scroll is clamped to cueSettingsScrollMax_ every
    // frame, and that maximum is only known once the inspector has measured
    // its own content -- so a value set before the first frame is clamped
    // straight back to zero and the flag looks like it does nothing.
    pendingInspectorScroll_ = std::max(0, pixels);
  }

  void applyPendingInspectorScroll() {
    if (pendingInspectorScroll_ < 0 || cueSettingsScrollMax_ <= 0) {
      return;
    }
    cueSettingsScroll_ = std::min(pendingInspectorScroll_, cueSettingsScrollMax_);
    pendingInspectorScroll_ = -1;
  }

  void debugOpenSettings(int tab, int videoSubTab = 0) {
    showStartupDialog_ = false;
    showSplashOverlay_ = false;
    settingsOpen_ = true;
    settingsTab_ = std::clamp(tab, 0, 5);
    settingsVideoSubTab_ = std::clamp(videoSubTab, 0, 3);
  }

  // ---- Show log ------------------------------------------------------------
  // Append-only record of what actually happened, so that after a show goes
  // wrong the operator can answer "what fired, and when?". Deckboy could not
  // answer that at all before this. Borrowed from QLab, whose show log is the
  // reason people trust it with real events.
  //
  // Deliberately dumb: line-per-event, opened once, flushed every write. A log
  // that buffers is a log that loses the last few seconds -- which is exactly
  // the part you need after a crash. It lives in stateDir(), never inside a
  // macOS .app bundle.
  void showLog(const std::string& event, const std::string& detail = "") {
    if (!showLogEnabled_) {
      return;
    }
    if (!showLogFile_.is_open()) {
      std::error_code ec;
      fs::create_directories(Paths::stateDir(), ec);
      showLogFile_.open(Paths::stateDir() / "deckboy-show.log",
                        std::ios::app);
      if (!showLogFile_.is_open()) {
        showLogEnabled_ = false;   // do not retry every event
        return;
      }
      std::time_t t = std::time(nullptr);
      char stamp[32] = "";
      if (std::tm* lt = std::localtime(&t)) {
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", lt);
      }
      showLogFile_ << "\n=== show log opened " << stamp
                   << "  " << kAppTitle << " " << kAppVersionTag << " ===\n";
    }
    // Wall clock AND running milliseconds: the first tells you when in the day,
    // the second gives exact spacing between events, which is what matters when
    // reconstructing a sequence that went wrong.
    std::time_t t = std::time(nullptr);
    char stamp[16] = "";
    if (std::tm* lt = std::localtime(&t)) {
      std::strftime(stamp, sizeof(stamp), "%H:%M:%S", lt);
    }
    showLogFile_ << stamp << "  +" << SDL_GetTicks() << "ms  " << event;
    if (!detail.empty()) {
      showLogFile_ << "  " << detail;
    }
    showLogFile_ << '\n';
    showLogFile_.flush();
  }

  // Cue identity as it will read back weeks later: number, name, and the deck it
  // was on. An index alone is useless once the playlist has been edited.
  std::string showLogCueRef(int deckIndex, int cueIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "(no deck)";
    }
    const Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return "(no cue)";
    }
    const Cue& cue = deck.cues[cueIndex];
    return "deck" + std::to_string(deckIndex + 1) +
           " q" + std::to_string(cueIndex + 1) +
           " \"" + cue.name + "\"";
  }

  void soakLog(const std::string& line) {
    std::time_t t = std::time(nullptr);
    char stamp[32] = "";
    if (std::tm* local = std::localtime(&t)) {
      std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", local);
    }
    std::string full = std::string(stamp) + "  " + line;
    std::cout << full << std::endl;
    if (soakLogFile_.is_open()) {
      soakLogFile_ << full << '\n';
      soakLogFile_.flush();
    }
  }

  void tickSoak() {
    if (!soakMode_) {
      return;
    }
    Uint64 now = SDL_GetTicks();
    if (soakStartMs_ == 0) {
      soakStartMs_ = now;
      soakLastLogMs_ = now;
      if (focusedDeck().cues.empty()) {
        // Nothing loaded: synthesize a pattern loop so the render/audio path
        // still gets exercised. Point DECKBOY_PROJECT (or open a show before
        // launching) to soak real media instead.
        addPatternCue("pocket-test");
        addPatternCueFromMenu();
      }
      Deck& deck = focusedDeckMutable();
      deck.playlistLoop = true;
      for (auto& cue : deck.cues) {
        cue.endAction = CueEndAction::AutoNext;
        cue.pauseAtBeginning = false;
        cue.pauseOnLastFrame = false;
        cue.loop = false;
        if (cue.kind != CueKind::Video && cue.kind != CueKind::Audio &&
            cue.stillDurationSeconds <= 0.0) {
          cue.stillDurationSeconds = 10.0;
        }
      }
      deck.selectedIndex = deck.cues.empty() ? -1 : 0;
      if (deck.selectedIndex >= 0) {
        takeSelected(true);
      }
      std::ostringstream start;
      start << "SOAK START cues=" << deck.cues.size()
            << " planned=" << soakMinutes_ << "min rss=" << std::fixed
            << std::setprecision(1) << processRssMb() << "MB";
      soakLog(start.str());
      return;
    }
    if (now - soakLastLogMs_ >= 60000) {
      soakLastLogMs_ = now;
      const Deck& deck = focusedDeck();
      std::ostringstream line;
      line << "soak +" << ((now - soakStartMs_) / 60000) << "min"
           << " rss=" << std::fixed << std::setprecision(1) << processRssMb() << "MB"
           << " stalls=" << decodeStallTotal_
           << " missing=" << missingMediaCount_
           << " active=" << deck.activeIndex
           << "/" << deck.cues.size();
      soakLog(line.str());
    }
    if (soakMinutes_ > 0.0 &&
        now - soakStartMs_ >= static_cast<Uint64>(soakMinutes_ * 60000.0)) {
      std::ostringstream done;
      done << "SOAK COMPLETE " << soakMinutes_ << "min"
           << " rss=" << std::fixed << std::setprecision(1) << processRssMb() << "MB"
           << " stalls=" << decodeStallTotal_
           << " missing=" << missingMediaCount_;
      soakLog(done.str());
      gShouldQuit.store(true);
    }
  }

  // ── Startup self-test ─────────────────────────────────────────────────────
#include "app/app_smoke.ipp"

 private:
  // Declared here so ipp-file helper functions can use it as a parameter type.
  struct SettingsButton { SDL_Rect rect; int action; std::string label; };

  // ── App method modules (.ipp includes) ──────────────────────────────────
  // Each .ipp file adds member functions to the App class. They are included
  // directly in the class body so they can access private members.

  // State accessors: getters for deck/cue/output/project state
#include "app/app_accessors.ipp"
  // Network: OSC server, Companion integration, tally reporting
#include "app/app_network.ipp"
  // Output management: window lifecycle, stream writer, NDI/DeckLink
#include "app/app_output_mgmt.ipp"
  // Project state: save/load .deckboy files, import/export
#include "app/app_project_state.ipp"
  // Remote command handler: processes OSC and Companion messages
#include "app/app_remote_command.ipp"
  // Per-frame update: timers, transitions, auto-advance, integration poll
#include "app/app_update.ipp"
  // Lower-third overlay management
#include "app/app_overlays.ipp"
  // Render: transport control panel (play/stop/seek buttons, timeline)
#include "app/app_render_control.ipp"
  // Render: main control window layout and drawing
  // (The live cue inspector is rendered by renderMainPanel below. A separate
  // app_render_inspector.ipp once held a second, unreachable copy of it; it was
  // deleted in v0.81.5 after ~1,646 lines of it had silently drifted out of sync
  // with the real one and been edited by mistake more than once.)
#include "app/app_render_main.ipp"
  // Geometry: cue/output geometry calculations (crop, scale, rotation)
#include "app/app_geometry.ipp"
  // Render: output window compositor → NDI/DeckLink/Siphon blit
#include "app/app_render_output.ipp"
  // Reusable UI widget functions (buttons, sliders, dropdowns, toggles)
#include "app/app_ui_widgets.ipp"
  // Render: settings modal dialog (all tabs)
#include "app/app_render_settings.ipp"

  void handleMonitorsMouseDown(int x, int y) {
    for (const auto& btn : outputMenuButtons_) {
      if (!pointInRect(x, y, btn.rect)) continue;
      if (btn.action == kOutputMenuActionAddOutput) {
        addOutput(project_.focusedDeckIndex);
        return;
      }
      if (btn.action == kOutputMenuActionToggleFps) {
        outputFpsCounterEnabled_ = !outputFpsCounterEnabled_;
        triggerToast(std::string("output fps ") + (outputFpsCounterEnabled_ ? "on" : "off"));
        return;
      }
      if (btn.action == kOutputMenuActionFocus) {
        if (btn.outputIndex >= 0) setFocusedOutputIndex(btn.outputIndex);
        return;
      }
      if (btn.action == kOutputMenuActionRecover) {
        if (btn.outputIndex >= 0) { setFocusedOutputIndex(btn.outputIndex); setFocusedOutputEnabled(true); }
        return;
      }
      if (btn.action == kOutputMenuActionDisarm) {
        if (btn.outputIndex >= 0) { setFocusedOutputIndex(btn.outputIndex); setFocusedOutputEnabled(false, false); }
        return;
      }
    }
  }

  // Input: keyboard/mouse event handling (key bindings, click dispatch)
#include "app/app_input.ipp"
  // Cue transport: play/stop/seek/fade operations
#include "app/app_cue_transport.ipp"
  // Quick-action command palette (Ctrl+K search)
#include "app/app_numeric_params.ipp"
#include "app/app_quick_action.ipp"
  // Cue list management: add/remove/reorder/duplicate cues
#include "app/app_cue_mgmt.ipp"
  struct InspectorCtx {
    SDL_Rect ctrl;          // inspector body rect
    int ctrlW;              // ctrl.w shorthand
    int inset;              // left/right inset (10 docked, 12 floating)
    int rowH;               // row height (28 both)
    int rowStep;            // row step (34 docked, 38 floating)
    int sectionHeaderH;     // section header height (28 both)
    int sectionGap;         // gap between sections
    TTF_Font* headerFont;   // section header font
    TTF_Font* valueFont;    // value / label font
    TTF_Font* labelFont;    // small label font
    bool ellipsize;         // whether to ellipsize text to pixel width
  };

  static std::string fmtFloat(float value, int decimals = 2) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(value));
    return buf;
  }

  static std::string fmtPercent(float value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f%%", static_cast<double>(value * 100.0f));
    return buf;
  }

  static const char* fmtScaleMode(ScaleMode mode) {
    switch (mode) {
      case ScaleMode::Fit: return "Fit";
      case ScaleMode::Fill: return "Fill";
      case ScaleMode::Stretch: return "Stretch";
      case ScaleMode::Unscaled: return "Unscaled";
    }
    return "?";
  }

  struct InspectorSectionScope {
    SDL_Rect headerRect {};
    bool open = false;
    int bodyStartY = 0;
  };

  // Draw the cue's EFFECTIVE audio fade envelope over a waveform rect: ramp
  // up across the fade-in after the in-point, ramp down into the out-point.
  // Uses the audio fades when the cue overrides them (a-fade in/out), else
  // the visual fades — the same resolution the audio thread applies.
  void drawAudioFadeEnvelope(const SDL_Rect& rect, const Cue& cue) {
    double dur = cue.duration > 0.0 ? cue.duration : 1.0;
    double inPos = std::clamp(cue.inPointSeconds, 0.0, dur);
    double outPos = cue.outPointSeconds > 0.0 ? std::clamp(cue.outPointSeconds, inPos, dur) : dur;
    double fadeIn = cue.audioFadeInSeconds >= 0.0f
      ? static_cast<double>(cue.audioFadeInSeconds) : cue.fadeInSeconds;
    double fadeOut = cue.audioFadeOutSeconds >= 0.0f
      ? static_cast<double>(cue.audioFadeOutSeconds) : cue.fadeOutSeconds;
    if (fadeIn <= 0.001 && fadeOut <= 0.001) {
      return;
    }
    auto xAt = [&](double sec) {
      return static_cast<float>(rect.x)
           + static_cast<float>(std::clamp(sec / dur, 0.0, 1.0) * (rect.w - 1));
    };
    float top = static_cast<float>(rect.y + 2);
    float bottom = static_cast<float>(rect.y + rect.h - 2);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, pal.light.r, pal.light.g, pal.light.b, 220);
    if (fadeIn > 0.001) {
      float x0 = xAt(inPos);
      float x1 = xAt(std::min(inPos + fadeIn, outPos));
      SDL_RenderLine(controlRenderer_, x0, bottom, x1, top);
      SDL_RenderLine(controlRenderer_, x0 + 1.0f, bottom, x1 + 1.0f, top);
    }
    if (fadeOut > 0.001) {
      float x0 = xAt(std::max(outPos - fadeOut, inPos));
      float x1 = xAt(outPos);
      SDL_RenderLine(controlRenderer_, x0, top, x1, bottom);
      SDL_RenderLine(controlRenderer_, x0 - 1.0f, top, x1 - 1.0f, bottom);
    }
  }

  // Shared label column width so every inspector row type aligns its value
  // box to the same grid. Quick/editable/status rows previously picked
  // 108/98/98 (docked) and 88/70/70 (floating) independently, so the value
  // column zig-zagged down the panel.
  int inspLabelColumnWidth(const InspectorCtx& ix, int contentW, int reservedW) const {
    int minValueW = ix.ellipsize ? 76 : 68;
    int preferred = ix.ellipsize ? 104 : 84;
    return std::clamp(preferred, 64, std::max(64, contentW - reservedW - minValueW));
  }

  void inspDrawQuickRow(const InspectorCtx& ix, int rowY, const std::string& label,
                        QuickAction decAction, const std::string& value,
                        QuickAction incAction, QuickAction toggleAction = QuickAction::ToggleLoop,
                        bool isToggle = false, bool toggleOn = false, std::string tip = "",
                        bool valueEditable = false, QuickAction valueAction = QuickAction::ToggleLoop,
                        int paramId = -1) {
    // A paramId is the modern way to make a value typeable: it needs no
    // per-control action, just a row in the numeric-parameter table.
    if (paramId >= 0) {
      valueEditable = true;
      valueAction = QuickAction::EditNumericParam;
    }
    constexpr int kBtnW = 28;
    int gap = ix.ellipsize ? 8 : 6;
    int rx = ix.ctrl.x + ix.inset;
    int contentW = ix.ctrlW - ix.inset * 2;

    if (isToggle) {
      SDL_Rect btn {rx, rowY, contentW, ix.rowH};
      SDL_Color fill = toggleOn ? pal.dark : pal.tile;
      SDL_Color ink  = toggleOn ? pal.light : pal.fg;
      drawUIPanel(btn, fill, pal.deep, pal.mid);
      SDL_Rect labelRect {btn.x + 8, btn.y, btn.w - 16, btn.h};
      std::string text = label + ": " + value;
      if (ix.ellipsize) text = ellipsizeToPixelWidth(ix.valueFont, text, labelRect.w);
      drawTextSafe(controlRenderer_, ix.valueFont, labelRect, text, ink);
      quickButtons_.push_back({btn, toggleAction, tip});
    } else {
      int fixedW = kBtnW * 2 + gap * 3;
      int minValueW = ix.ellipsize ? 76 : 68;
      int labelW = inspLabelColumnWidth(ix, contentW, fixedW);
      int valueW = std::max(minValueW, contentW - labelW - fixedW);
      SDL_Rect labelRect {rx, rowY, labelW, ix.rowH};
      SDL_Rect decBtn {labelRect.x + labelRect.w + gap, rowY, kBtnW, ix.rowH};
      SDL_Rect valRect {decBtn.x + decBtn.w + gap, rowY, valueW, ix.rowH};
      SDL_Rect incBtn {valRect.x + valRect.w + gap, rowY, kBtnW, ix.rowH};

      drawTextSafe(controlRenderer_, ix.labelFont, labelRect, label, pal.fg);
      drawUIPanel(decBtn, pal.tile, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, decBtn, "-", pal.fg);
      quickButtons_.push_back({decBtn, decAction, tip});

      drawUIPanel(valRect, pal.mid, pal.deep, pal.light);
      std::string displayValue = ix.ellipsize
        ? ellipsizeToPixelWidth(ix.valueFont, value, valRect.w - 12) : value;
      drawCenteredTextSafe(controlRenderer_, ix.valueFont, valRect, displayValue, pal.deep);
      if (valueEditable) {
        std::string valueTip = tip.empty()
          ? "Drag value to scrub | click to type an exact number"
          : tip + " | drag to scrub (shift = fine), click to type exact";
        quickButtons_.push_back({valRect, valueAction, valueTip, paramId});
      }
      // Every dec/inc row's value cell is scrubbable; the zone is checked
      // before quickButtons_ on mouse-down so a click without drag still
      // opens the exact-entry editor (valueAction) on release.
      if (decAction != incAction) {
        valueScrubZones_.push_back({valRect, decAction, incAction,
                                    valueEditable ? valueAction : QuickAction::ToggleLoop,
                                    valueEditable, paramId});
      }

      drawUIPanel(incBtn, pal.tile, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, incBtn, "+", pal.fg);
      quickButtons_.push_back({incBtn, incAction, tip});
    }
  }

  int inspDrawMessageRow(const InspectorCtx& ix, int rowY, const std::string& text,
                         SDL_Color fill, SDL_Color ink) {
    SDL_Rect msgRect {ix.ctrl.x + ix.inset, rowY, ix.ctrlW - ix.inset * 2, ix.rowH};
    drawUIPanel(msgRect, fill, pal.deep, pal.mid);
    SDL_Rect textRect {msgRect.x + 6, msgRect.y, msgRect.w - 12, msgRect.h};
    drawTextSafe(controlRenderer_, ix.valueFont, textRect, text, ink);
    return rowY + ix.rowStep;
  }

  int inspDrawActionRow(const InspectorCtx& ix, int rowY, const std::string& label,
                        QuickAction action, const std::string& tip,
                        SDL_Color fill, SDL_Color ink) {
    int h = ix.ellipsize ? 34 : ix.rowH;
    int step = ix.ellipsize ? 46 : ix.rowStep;
    SDL_Rect btnRect {ix.ctrl.x + ix.inset, rowY, ix.ctrlW - ix.inset * 2, h};
    drawUIPanel(btnRect, fill, pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, ix.valueFont, btnRect, label, ink);
    quickButtons_.push_back({btnRect, action, tip});
    return rowY + step;
  }

  int inspDrawEditableRow(const InspectorCtx& ix, int rowY,
                          const std::string& label, const std::string& value,
                          QuickAction action, const std::string& tip,
                          SDL_Color valueColor) {
    int kEditW = ix.ellipsize ? 60 : 56;
    int kGap = ix.ellipsize ? 8 : 6;
    int contentW = ix.ctrlW - ix.inset * 2;
    int minValueW = ix.ellipsize ? 84 : 72;
    int labelW = inspLabelColumnWidth(ix, contentW, kEditW + kGap * 2);
    int valueW = std::max(minValueW, contentW - labelW - kEditW - kGap * 2);
    SDL_Rect labelRect {ix.ctrl.x + ix.inset, rowY, labelW, ix.rowH};
    SDL_Rect valueRect {labelRect.x + labelRect.w + kGap, rowY, valueW, ix.rowH};
    SDL_Rect editRect {valueRect.x + valueRect.w + kGap, rowY, kEditW, ix.rowH};
    drawTextSafe(controlRenderer_, ix.labelFont, labelRect, label, pal.inkSoft);
    drawUIPanel(valueRect, pal.light, pal.deep, pal.mid);
    SDL_Rect valueTextRect {valueRect.x + 6, valueRect.y, valueRect.w - 12, valueRect.h};
    std::string displayValue = ix.ellipsize
      ? ellipsizeToPixelWidth(ix.valueFont, value, valueTextRect.w) : value;
    drawTextSafe(controlRenderer_, ix.valueFont, valueTextRect, displayValue, valueColor);
    drawUIPanel(editRect, pal.dark, pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, editRect, "edit", pal.light);
    quickButtons_.push_back({editRect, action, tip});
    return rowY + ix.rowStep;
  }

  int inspDrawStatusRow(const InspectorCtx& ix, int rowY,
                        const std::string& label, const std::string& value, bool warning) {
    int gap = ix.ellipsize ? 8 : 6;
    int contentW = ix.ctrlW - ix.inset * 2;
    int labelW = inspLabelColumnWidth(ix, contentW, gap);
    int valueW = std::max(ix.ellipsize ? 84 : 72, contentW - labelW - gap);
    SDL_Rect labelRect {ix.ctrl.x + ix.inset, rowY, labelW, ix.rowH};
    SDL_Rect valueRect {labelRect.x + labelRect.w + gap, rowY, valueW, ix.rowH};
    drawTextSafe(controlRenderer_, ix.labelFont, labelRect, label, pal.inkSoft);
    drawUIPanel(valueRect, pal.light, pal.deep, pal.mid);
    SDL_Rect valueTextRect {valueRect.x + 6, valueRect.y, valueRect.w - 12, valueRect.h};
    SDL_Color valueColor = warning ? SDL_Color {140, 40, 20, 255} : pal.deep;
    std::string displayValue = ix.ellipsize
      ? ellipsizeToPixelWidth(ix.valueFont, value, valueTextRect.w) : value;
    drawTextSafe(controlRenderer_, ix.valueFont, valueTextRect, displayValue, valueColor);
    return rowY + ix.rowStep;
  }

  void inspDrawKeyColorRow(const InspectorCtx& ix, int rowY, const Cue& cue) {
    constexpr int kSwatchW = 34;
    constexpr int kEditW = 50;
    constexpr int kPickW = 56;
    constexpr int kGap = 8;
    int rx = ix.ctrl.x + ix.inset;
    int contentW = ix.ctrlW - ix.inset * 2;
    int hexW = std::max(88, contentW - kSwatchW - kEditW - kPickW - kGap * 3);
    SDL_Rect swatchRect {rx, rowY, kSwatchW, ix.rowH};
    SDL_Rect hexRect {swatchRect.x + swatchRect.w + kGap, rowY, hexW, ix.rowH};
    SDL_Rect editRect {hexRect.x + hexRect.w + kGap, rowY, kEditW, ix.rowH};
    SDL_Rect pickRect {editRect.x + editRect.w + kGap, rowY, kPickW, ix.rowH};
    drawUIPanel(swatchRect, cue.chromaKeyColor, pal.deep, pal.mid);
    drawUIPanel(hexRect, pal.light, pal.deep, pal.mid);
    drawTextSafe(controlRenderer_, ix.valueFont,
                 SDL_Rect {hexRect.x + 6, hexRect.y, hexRect.w - 12, hexRect.h},
                 colorToHex(cue.chromaKeyColor), pal.deep);
    drawUIPanel(editRect, pal.dark, pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, editRect, "HEX", pal.light);
    SDL_Color pickFill = keyColorPickerArmed_ ? pal.dark : pal.mid;
    SDL_Color pickInk = keyColorPickerArmed_ ? pal.light : pal.deep;
    drawUIPanel(pickRect, pickFill, pal.deep, pal.light);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, pickRect, keyColorPickerArmed_ ? "ARM" : "PICK", pickInk);
    quickButtons_.push_back({swatchRect, QuickAction::EditKeyColor, "Type exact key color"});
    quickButtons_.push_back({hexRect, QuickAction::EditKeyColor, "Type exact key color"});
    quickButtons_.push_back({editRect, QuickAction::EditKeyColor, "Type exact key color"});
    quickButtons_.push_back({pickRect, QuickAction::PickKeyColor, "Sample key color from program or preview monitor"});
  }

  int inspDrawGeometryRows(const InspectorCtx& ix, int startY, const Cue& cue, bool includeScaleOffset) {
    int rowY = startY;
    if (includeScaleOffset) {
      inspDrawQuickRow(ix, rowY, "mode", QuickAction::CycleScaleMode, fmtScaleMode(cue.scaleMode), QuickAction::CycleScaleMode,
                       QuickAction::ToggleLoop, false, false, "Fit/Fill/Stretch/Unscaled");
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "link aspect", QuickAction::ToggleAspectLink,
                       project_.geometryAspectLinked ? "on" : "off",
                       QuickAction::ToggleAspectLink, QuickAction::ToggleAspectLink,
                       true, project_.geometryAspectLinked,
                       "Changing width also scales height (and vice versa)");
      rowY += ix.rowStep;
      {
        auto [baseW, baseH] = cueBaseRenderSize(cue);
        int finalW = std::max(1, static_cast<int>(std::lround(baseW * cue.outputScaleX)));
        int finalH = std::max(1, static_cast<int>(std::lround(baseH * cue.outputScaleY)));
        inspDrawQuickRow(ix, rowY, "width", QuickAction::ScaleXDec, std::to_string(finalW) + "px",
                         QuickAction::ScaleXInc, QuickAction::ToggleLoop, false, false,
                         "Output width in pixels | click value to type exact px", true, QuickAction::EditScaleX);
        rowY += ix.rowStep;
        inspDrawQuickRow(ix, rowY, "height", QuickAction::ScaleYDec, std::to_string(finalH) + "px",
                         QuickAction::ScaleYInc, QuickAction::ToggleLoop, false, false,
                         "Output height in pixels | click value to type exact px", true, QuickAction::EditScaleY);
      }
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "offset X", QuickAction::OffsetXDec, std::to_string(static_cast<int>(cue.outputOffsetX)) + "px", QuickAction::OffsetXInc,
                       QuickAction::ToggleLoop, false, false, "Horizontal output offset in pixels", true, QuickAction::EditOffsetX);
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "offset Y", QuickAction::OffsetYDec, std::to_string(static_cast<int>(cue.outputOffsetY)) + "px", QuickAction::OffsetYInc,
                       QuickAction::ToggleLoop, false, false, "Vertical output offset in pixels", true, QuickAction::EditOffsetY);
      rowY += ix.rowStep;
    }
    inspDrawQuickRow(ix, rowY, "rotation", QuickAction::RotDec, fmtFloat(cue.outputRotationDegrees, 1) + " deg", QuickAction::RotInc,
                     QuickAction::ToggleLoop, false, false, "Output rotation angle (-180..180)", true, QuickAction::EditRotation);
    rowY += ix.rowStep;
    auto fmtCropPx = [&](float cropFrac, int sourceDim) -> std::string {
      if (sourceDim <= 0) return fmtPercent(cropFrac);
      int px = static_cast<int>(std::lround(cropFrac * sourceDim));
      return std::to_string(px) + "px";
    };
    inspDrawQuickRow(ix, rowY, "crop left", QuickAction::CropLDec, fmtCropPx(cue.cropLeft, cue.width), QuickAction::CropLInc,
                     QuickAction::ToggleLoop, false, false, "Crop from left");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "crop right", QuickAction::CropRDec, fmtCropPx(cue.cropRight, cue.width), QuickAction::CropRInc,
                     QuickAction::ToggleLoop, false, false, "Crop from right");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "crop top", QuickAction::CropTDec, fmtCropPx(cue.cropTop, cue.height), QuickAction::CropTInc,
                     QuickAction::ToggleLoop, false, false, "Crop from top");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "crop bottom", QuickAction::CropBDec, fmtCropPx(cue.cropBottom, cue.height), QuickAction::CropBInc,
                     QuickAction::ToggleLoop, false, false, "Crop from bottom");
    rowY += ix.rowStep;
    return rowY;
  }

  int inspDrawColorRows(const InspectorCtx& ix, int startY, const Cue& cue) {
    int rowY = startY;
    if (!cueSupportsColorControls(&cue)) return rowY;
    inspDrawQuickRow(ix, rowY, "brightness", QuickAction::BrightnessDec, fmtFloat(cue.brightness, 2) + "x", QuickAction::BrightnessInc,
                     QuickAction::ToggleLoop, false, false, "Brightness multiplier (0.0-2.0x)");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "contrast", QuickAction::ContrastDec, fmtFloat(cue.contrast, 2) + "x", QuickAction::ContrastInc,
                     QuickAction::ToggleLoop, false, false, "Contrast multiplier (0.0-2.0x)");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "saturation", QuickAction::SaturationDec, fmtFloat(cue.saturation, 2) + "x", QuickAction::SaturationInc,
                     QuickAction::ToggleLoop, false, false, "Saturation multiplier (0.0-2.0x)");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "hue", QuickAction::HueShiftDec, fmtFloat(cue.hueShift, 0) + " deg", QuickAction::HueShiftInc,
                     QuickAction::ToggleLoop, false, false, "Hue rotation (-180 to +180 degrees)");
    rowY += ix.rowStep;
    return rowY;
  }

  // Per-cue effects. The status line matters more than usual here: datamosh has
  // three states and, without it, the only way to learn which one a cue is in
  // was to hit the toggle and read a toast.
  // Per-cue effects. The status line matters more than usual: datamosh has
  // three states and, without it, the only way to learn which one a cue is in
  // was to hit the toggle and read a toast.
  // Stage timer controls. These act on the CLOCK, not the transport: the cue
  // stays on air on the stage screen while the operator runs, holds, resets or
  // nudges it. That separation is the whole point of the section existing.
  int inspDrawTimerRows(const InspectorCtx& ix, int startY, const Cue& cue) {
    int rowY = startY;
    auto it = timerRuntimes_.find(cue.id);
    const bool running = it != timerRuntimes_.end() && it->second.running;
    const double elapsed = it != timerRuntimes_.end() ? it->second.elapsedSeconds : 0.0;

    auto mmss = [](double seconds) {
      const bool neg = seconds < 0.0;
      int t = static_cast<int>(std::floor(std::abs(seconds)));
      char buf[32];
      if (t >= 3600) {
        std::snprintf(buf, sizeof(buf), "%s%d:%02d:%02d", neg ? "+" : "",
                      t / 3600, (t % 3600) / 60, t % 60);
      } else {
        std::snprintf(buf, sizeof(buf), "%s%d:%02d", neg ? "+" : "", t / 60, t % 60);
      }
      return std::string(buf);
    };

    // Live readout first: what the stage screen is showing right now.
    const double remaining = static_cast<double>(cue.timer.durationSeconds) - elapsed;
    // An explicit transport row. The clock row below TOGGLES on click, but it
    // reads as a time display rather than a button, so an operator looking for
    // start/stop did not find one -- which is what happened.
    inspDrawQuickRow(ix, rowY, running ? "STOP" : "START",
                     QuickAction::TimerRunToggle,
                     running ? "running" : "held",
                     QuickAction::TimerRunToggle, QuickAction::TimerRunToggle,
                     true, running,
                     "Start or stop the clock. The timer runs its own clock, "
                     "so this does not touch playback.");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "clock", QuickAction::TimerRunToggle,
                     mmss(remaining), QuickAction::TimerRunToggle,
                     QuickAction::TimerRunToggle, true, running,
                     running ? "Hold the clock" : "Run the clock");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "nudge min", QuickAction::TimerNudgeDown,
                     "+/- 1 min", QuickAction::TimerNudgeUp,
                     QuickAction::ToggleLoop, false, false,
                     "Give or take a minute without stopping the clock");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "nudge sec", QuickAction::TimerNudgeSecDown,
                     "+/- 10 sec", QuickAction::TimerNudgeSecUp,
                     QuickAction::ToggleLoop, false, false,
                     "Finer adjustment, for trimming a countdown mid-talk "
                     "rather than reshaping it.");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "reset", QuickAction::TimerResetAction,
                     "back to start", QuickAction::TimerResetAction,
                     QuickAction::TimerResetAction, false, false,
                     "Reset the clock to its full duration");
    rowY += ix.rowStep;

    const char* modeLabel =
      cue.timer.mode == TimerMode::CountUp   ? "count up"
      : cue.timer.mode == TimerMode::TimeOfDay ? "time of day"
                                               : "countdown";
    inspDrawQuickRow(ix, rowY, "mode", QuickAction::TimerCycleMode, modeLabel,
                     QuickAction::TimerCycleMode, QuickAction::ToggleLoop, false,
                     false, "Countdown, count up, or wall clock");
    rowY += ix.rowStep;

    const char* faceLabel = cue.timer.face == TimerFace::Blocky ? "blocky" : "7-segment";
    inspDrawQuickRow(ix, rowY, "face", QuickAction::TimerCycleFace, faceLabel,
                     QuickAction::TimerCycleFace, QuickAction::ToggleLoop, false,
                     false, "Clock typeface");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "duration", QuickAction::TimerDurDec,
                     mmss(cue.timer.durationSeconds), QuickAction::TimerDurInc,
                     QuickAction::ToggleLoop, false, false, "Total time");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "amber at", QuickAction::TimerAmberDec,
                     mmss(cue.timer.amberSeconds), QuickAction::TimerAmberInc,
                     QuickAction::ToggleLoop, false, false,
                     "Seconds REMAINING when the clock turns amber");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "red at", QuickAction::TimerRedDec,
                     mmss(cue.timer.redSeconds), QuickAction::TimerRedInc,
                     QuickAction::ToggleLoop, false, false,
                     "Seconds REMAINING when the clock turns red");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "overtime", QuickAction::TimerCountUpToggle,
                     cue.timer.countUpAfterZero ? "count up" : "stop at 0",
                     QuickAction::TimerCountUpToggle,
                     QuickAction::TimerCountUpToggle, true,
                     cue.timer.countUpAfterZero,
                     "Keep counting past zero, or stop dead");
    rowY += ix.rowStep;

    // Chimes. A speaker facing the audience is not watching the clock, which
    // is the whole reason a stage timer makes noise.
    inspDrawQuickRow(ix, rowY, "chime amber", QuickAction::TimerChimeAmberToggle,
                     cue.timer.chimeAtAmber ? "on" : "off",
                     QuickAction::TimerChimeAmberToggle,
                     QuickAction::TimerChimeAmberToggle, true,
                     cue.timer.chimeAtAmber, "Sound when the clock turns amber");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "chime red", QuickAction::TimerChimeRedToggle,
                     cue.timer.chimeAtRed ? "on" : "off",
                     QuickAction::TimerChimeRedToggle,
                     QuickAction::TimerChimeRedToggle, true,
                     cue.timer.chimeAtRed, "Sound when the clock turns red");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "chime zero", QuickAction::TimerChimeZeroToggle,
                     cue.timer.chimeAtZero ? "on" : "off",
                     QuickAction::TimerChimeZeroToggle,
                     QuickAction::TimerChimeZeroToggle, true,
                     cue.timer.chimeAtZero, "Sound when time is up");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "logo", QuickAction::TimerPickLogo,
                     cue.timer.logoPath.empty()
                       ? std::string("none")
                       : fs::path(cue.timer.logoPath).filename().string(),
                     QuickAction::TimerPickLogo, QuickAction::TimerClearLogo,
                     false, false,
                     "Image drawn above the clock. Right-click clears it.");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "chime", QuickAction::TimerCycleChimeSound,
                     timerChimeName(cue.timer.chimeSound),
                     QuickAction::TimerCycleChimeSound,
                     QuickAction::ToggleLoop, false, false,
                     "Which chime. Cycling PLAYS it, so it can be chosen by ear "
                     "rather than by name.");
    rowY += ix.rowStep;

    // Colours. "default" is a real state, not a colour -- it means the
    // built-in, so an untouched timer says so rather than naming a swatch.
    inspDrawQuickRow(ix, rowY, "colour", QuickAction::TimerCycleColorNormal,
                     timerColorLabel(cue.timer.colorNormal),
                     QuickAction::TimerCycleColorNormal,
                     QuickAction::ToggleLoop, false, false,
                     "Digit colour while there is time in hand");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "amber col", QuickAction::TimerCycleColorAmber,
                     timerColorLabel(cue.timer.colorAmber),
                     QuickAction::TimerCycleColorAmber,
                     QuickAction::ToggleLoop, false, false,
                     "Digit colour once amber is reached");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "red col", QuickAction::TimerCycleColorRed,
                     timerColorLabel(cue.timer.colorRed),
                     QuickAction::TimerCycleColorRed,
                     QuickAction::ToggleLoop, false, false,
                     "Digit colour once red is reached, and in overtime");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "backdrop", QuickAction::TimerCycleColorBackground,
                     timerColorLabel(cue.timer.colorBackground),
                     QuickAction::TimerCycleColorBackground,
                     QuickAction::ToggleLoop, false, false,
                     "Screen behind the clock");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "prog bar", QuickAction::TimerProgressToggle,
                     cue.timer.showProgressBar ? "on" : "off",
                     QuickAction::TimerProgressToggle,
                     QuickAction::TimerProgressToggle, true, cue.timer.showProgressBar,
                     "Bar across the foot - readable further back than digits");
    rowY += ix.rowStep;

    // The message was renderable and saveable but had no way to SET it, and the
    // urgent flag had no setter at all, so the red "wrap up NOW" state could
    // never fire. Both are editable here now.
    rowY = inspDrawEditableRow(ix, rowY, "message",
                               cue.timer.message.empty() ? "(none)" : cue.timer.message,
                               QuickAction::TimerEditMessage,
                               "Line shown under the clock",
                               cue.timer.message.empty() ? pal.inkSoft : pal.deep);
    inspDrawQuickRow(ix, rowY, "urgent", QuickAction::TimerUrgentToggle,
                     cue.timer.messageIsUrgent ? "RED" : "normal",
                     QuickAction::TimerUrgentToggle,
                     QuickAction::TimerUrgentToggle, true, cue.timer.messageIsUrgent,
                     "Show the message in red - the wrap up NOW state");
    rowY += ix.rowStep;
    return rowY;
  }

  // Tone generator rows. Frequency only appears for SINE and the sweep range
  // only for SWEEP, because a control that does nothing for the current
  // waveform is worse than no control -- it invites the operator to change it
  // and wonder why nothing happened.
  int inspDrawToneRows(const InspectorCtx& ix, int startY, const Cue& cue) {
    int rowY = startY;
    if (cue.kind != CueKind::Tone) {
      return rowY;
    }
    inspDrawQuickRow(ix, rowY, "signal", QuickAction::ToneCycleWaveform,
                     toneWaveformLabel(cue.tone.waveform),
                     QuickAction::ToneCycleWaveform, QuickAction::ToggleLoop,
                     false, false,
                     "Sine for line-up, pink to ring out a PA, white to find "
                     "rattles, sweep to hear where a room rings, identify to "
                     "walk the outputs one at a time.");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "level", QuickAction::ToneLevelDec,
                     fmtFloat(cue.tone.levelDbfs, 1) + " dBFS",
                     QuickAction::ToneLevelInc, QuickAction::ToggleLoop,
                     false, false,
                     "-18 dBFS is EBU alignment, -20 is SMPTE. Capped below 0: "
                     "this feeds a live PA.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::ToneLevel));
    rowY += ix.rowStep;

    if (cue.tone.waveform == ToneWaveform::Sine) {
      inspDrawQuickRow(ix, rowY, "freq", QuickAction::ToneFreqDec,
                       std::to_string(static_cast<int>(cue.tone.frequencyHz)) + " Hz",
                       QuickAction::ToneFreqInc, QuickAction::ToggleLoop,
                       false, false,
                       "Steps in third-octaves, because the ear works in "
                       "ratios. 1kHz is the convention.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::ToneFreq));
      rowY += ix.rowStep;
    } else if (cue.tone.waveform == ToneWaveform::Sweep) {
      rowY = inspDrawMessageRow(ix, rowY,
        std::to_string(static_cast<int>(cue.tone.sweepLowHz)) + "-" +
        std::to_string(static_cast<int>(cue.tone.sweepHighHz)) + " Hz log sweep",
        pal.mid, pal.deep);
    }

    if (cue.tone.waveform == ToneWaveform::Identify) {
      rowY = inspDrawMessageRow(ix, rowY, "walks every output in turn",
                                pal.mid, pal.deep);
    } else {
      inspDrawQuickRow(ix, rowY, "output", QuickAction::ToneChannelDec,
                       cue.tone.channel < 0
                         ? std::string("all")
                         : ("ch " + std::to_string(cue.tone.channel + 1)),
                       QuickAction::ToneChannelInc, QuickAction::ToggleLoop,
                       false, false,
                       "Which output channel carries the signal. 'all' feeds "
                       "every one.");
      rowY += ix.rowStep;
    }
    inspDrawQuickRow(ix, rowY, "visuals", QuickAction::ToneVisualToggle,
                     cue.tone.visualEnabled ? "on" : "off",
                     QuickAction::ToneVisualToggle, QuickAction::ToneVisualToggle,
                     true, cue.tone.visualEnabled,
                     "Draw the diagnostic over the card, or just the text.");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "display", QuickAction::ToneCycleVisual,
                     toneVisualLabel(cue.tone.visual),
                     QuickAction::ToneCycleVisual, QuickAction::ToggleLoop,
                     false, false,
                     "Scope shows shape and clipping. Lissajous shows phase "
                     "and polarity between the first two channels -- a "
                     "diagonal the wrong way means one is inverted. Spectrum "
                     "is third-octave bars.");
    rowY += ix.rowStep;
    return rowY;
  }

  // Chip voice rows. Its own section rather than more of TONE: a test signal
  // and a synth voice are different jobs, and by the time both chips have their
  // parameters the combined list is too long to scan mid-show.
  int inspDrawSynthRows(const InspectorCtx& ix, int startY, const Cue& cue) {
    int rowY = startY;
    if (cue.kind != CueKind::Tone || cue.tone.waveform != ToneWaveform::Fds) {
      return rowY;
    }
    const SynthSettings& s = cue.tone.synth;

    inspDrawQuickRow(ix, rowY, "chip", QuickAction::SynthCycleChip,
                     synthChipLabel(s.chip),
                     QuickAction::SynthCycleChip, QuickAction::ToggleLoop,
                     false, false,
                     "FDS is a wavetable bent by a modulator. 2A03 is the "
                     "pulse/triangle/noise set most chiptune is made of.");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "note", QuickAction::FdsNoteDec,
                     fdsNoteName(s.noteHz),
                     QuickAction::FdsNoteInc, QuickAction::ToggleLoop,
                     false, false, "Pitch, in semitones.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::SynthNote));
    rowY += ix.rowStep;

    if (s.chip == SynthChip::Nes) {
      inspDrawQuickRow(ix, rowY, "voice", QuickAction::SynthCycleNesVoice,
                       nesVoiceLabel(s.nesVoice),
                       QuickAction::SynthCycleNesVoice, QuickAction::ToggleLoop,
                       false, false,
                       "The chip's three channels. They differ in kind: "
                       "triangle has no volume control on the hardware, and "
                       "noise is a shift register rather than an oscillator.");
      rowY += ix.rowStep;
      if (s.nesVoice == NesVoice::Pulse) {
        inspDrawQuickRow(ix, rowY, "duty", QuickAction::SynthCycleNesDuty,
                         nesDutyLabel(s.nesDuty),
                         QuickAction::SynthCycleNesDuty, QuickAction::ToggleLoop,
                         false, false,
                         "12.5 and 25 are thin and reedy, 50 is hollow. 75 "
                         "sounds identical to 25 -- it is here because "
                         "trackers expose it.");
        rowY += ix.rowStep;
      } else if (s.nesVoice == NesVoice::Noise) {
        inspDrawQuickRow(ix, rowY, "periodic", QuickAction::SynthToggleNoiseShort,
                         s.nesNoiseShort ? "on" : "off",
                         QuickAction::SynthToggleNoiseShort,
                         QuickAction::SynthToggleNoiseShort, true, s.nesNoiseShort,
                         "Short LFSR mode. The period is brief enough to read "
                         "as pitched metal rather than hiss.");
        rowY += ix.rowStep;
      }
      inspDrawQuickRow(ix, rowY, "4-bit", QuickAction::SynthToggleQuantise,
                       s.nesQuantise ? "on" : "off",
                       QuickAction::SynthToggleQuantise,
                       QuickAction::SynthToggleQuantise, true, s.nesQuantise,
                       "Quantise the output like the hardware. The steps ARE "
                       "the sound; smoothing them makes it a synth imitating "
                       "a chip.");
      rowY += ix.rowStep;
    } else {
      inspDrawQuickRow(ix, rowY, "carrier", QuickAction::FdsCycleCarrier,
                       fdsCarrierLabel(s.carrier),
                       QuickAction::FdsCycleCarrier, QuickAction::ToggleLoop,
                       false, false, "The 64-step wavetable, quantised to 6 "
                       "bits like the hardware.");
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "modulator", QuickAction::FdsCycleModulator,
                       fdsModulatorLabel(s.modulator),
                       QuickAction::FdsCycleModulator, QuickAction::ToggleLoop,
                       false, false, "Bends the carrier's PITCH rather than "
                       "mixing with it. This is what makes FDS growl.");
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "depth", QuickAction::FdsDepthDec,
                       std::to_string(s.modDepth),
                       QuickAction::FdsDepthInc, QuickAction::ToggleLoop,
                       false, false, "How far the modulator bends the pitch. "
                       "0-63, the hardware's own gain range.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::FdsDepth));
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "ratio", QuickAction::FdsRatioDec,
                       fmtFloat(s.modRatio, 3),
                       QuickAction::FdsRatioInc, QuickAction::ToggleLoop,
                       false, false, "Modulator speed as a ratio of the note, "
                       "so the bend tracks pitch.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::FdsRatio));
      rowY += ix.rowStep;
    }

    inspDrawQuickRow(ix, rowY, "tuning", QuickAction::SynthCycleTuning,
                     synthTuningLabel(s.tuning),
                     QuickAction::SynthCycleTuning, QuickAction::ToggleLoop,
                     false, false,
                     "Equal temperament buys free modulation at the cost of "
                     "every interval being slightly wrong. Just and "
                     "pythagorean are exactly in tune in one key. "
                     "Bohlen-Pierce divides a twelfth, so it has no octaves.");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "A =", QuickAction::SynthRefDec,
                     fmtFloat(s.referenceHz, 0) + " Hz",
                     QuickAction::SynthRefInc, QuickAction::ToggleLoop,
                     false, false,
                     "Reference pitch. 440 is modern standard, 432 the common "
                     "alternative, and older instruments sat lower still.");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "keyboard", QuickAction::SynthKeyboardToggle,
                     project_.synthKeyboardEnabled ? "PLAYING" : "off",
                     QuickAction::SynthKeyboardToggle,
                     QuickAction::SynthKeyboardToggle, true,
                     project_.synthKeyboardEnabled,
                     "Play from the computer keyboard, Ableton layout: A W S E "
                     "D F T G Y H U J, Z and X for octave. While this is ON "
                     "those keys make notes instead of firing cues.");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "midi in", QuickAction::SynthMidiToggle,
                     project_.midiToSynth ? "plays synth" : "fires cues",
                     QuickAction::SynthMidiToggle,
                     QuickAction::SynthMidiToggle, true, project_.midiToSynth,
                     "Whether incoming MIDI notes play this synth or trigger "
                     "cues as they always have.");
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "attack", QuickAction::SynthAttackDec,
                     fmtFloat(s.attackSeconds, 2) + "s",
                     QuickAction::SynthAttackInc, QuickAction::ToggleLoop,
                     false, false, "How long the note takes to reach full level.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::SynthAttack));
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "release", QuickAction::SynthReleaseDec,
                     fmtFloat(s.releaseSeconds, 2) + "s",
                     QuickAction::SynthReleaseInc, QuickAction::ToggleLoop,
                     false, false, "How long it takes to fall away after the "
                     "attack. Only audible when retrigger is set.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::SynthRelease));
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "retrigger", QuickAction::FdsRetrigDec,
                     s.retriggerSeconds <= 0.0
                       ? std::string("hold")
                       : fmtFloat(s.retriggerSeconds, 2) + "s",
                     QuickAction::FdsRetrigInc, QuickAction::ToggleLoop,
                     false, false, "Re-strike the envelope this often. Hold "
                     "sustains one note, which is what a drone wants.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::FdsRetrig));
    rowY += ix.rowStep;
    return rowY;
  }

  // Effect stack rows. One row per entry: kind (click to cycle), amount
  // (dec/inc, drag to scrub, click to type), and the reorder/remove controls.
  // Each carries its INDEX through QuickButton::param, which is what lets a
  // list of arbitrary length work without an action per slot.
  // Which cues can carry an effect stack: anything that produces pixels. An
  // audio-only cue has nothing for an effect to act on, and offering one there
  // would be a control that cannot do anything -- this codebase's signature
  // bug, and not one worth adding on purpose.
  static bool cueSupportsEffectStack(const Cue& cue) {
    switch (cue.kind) {
      case CueKind::Audio:
        return false;
      case CueKind::Tone:
        return cue.tone.visualEnabled;   // a tone cue only has pixels if asked
      default:
        return true;
    }
  }

  int inspDrawEffectRows(const InspectorCtx& ix, int startY, const Cue& cue) {
    int rowY = startY;
    // Cleared every pass and set again only if the bar is actually drawn. A
    // stale rect left behind by a cue that no longer has a driver would be an
    // invisible control that still swallowed clicks.
    motionDriverScrubRect_ = {0, 0, 0, 0};
    const auto& stack = cue.effects;
    for (int i = 0; i < static_cast<int>(stack.size()); ++i) {
      const auto& fx = stack[i];
      // NAME OPENS THE PICKER. The label doubles as the control that changes
      // it, so the effect's identity and the way to change it are the same
      // object -- there is no separate "kind" button to hunt for.
      {
        SDL_Rect nameRect {ix.ctrl.x + ix.inset, rowY,
                           ix.ctrlW - ix.inset * 2, ix.rowH};
        drawUIPanel(nameRect, fx.bypassed ? pal.mid : pal.tile, pal.deep, pal.mid);
        std::string name = std::to_string(i + 1);
        name += ". ";
        name += deckboy::effects::cueEffectLabel(fx.kind);
        if (fx.bypassed) {
          name += "  (bypassed)";
        }
        drawTextSafe(controlRenderer_, fontSmall_,
                     SDL_Rect {nameRect.x + 6, nameRect.y, nameRect.w - 20, nameRect.h},
                     name, fx.bypassed ? pal.inkSoft : pal.fg);
        drawCenteredTextSafe(controlRenderer_, fontSmall_,
                             SDL_Rect {nameRect.x + nameRect.w - 16, nameRect.y, 14, nameRect.h},
                             "v", pal.inkSoft);
        quickButtons_.push_back({nameRect, QuickAction::EffectCycleKind,
                                 "Choose which effect this is", i});
        rowY += ix.rowStep;
      }
      // Datamosh carries its own state instead of an amount: it is a decode
      // behaviour, not a per-pixel strength, and showing it a 0-1 slider it
      // does not use would be a control that does nothing.
      if (fx.kind == deckboy::effects::CueEffectKind::Datamosh) {
        const bool fileBacked = cue.kind == CueKind::Video && !cue.path.empty();
        if (!fileBacked) {
          rowY = inspDrawMessageRow(ix, rowY,
                                    "needs file-backed video", pal.mid, pal.deep);
        } else {
          std::error_code moshEc;
          const bool prepared =
            !cue.moshPath.empty() && fs::exists(fs::path(cue.moshPath), moshEc);
          rowY = inspDrawMessageRow(
            ix, rowY,
            cue.datamoshEnabled ? (prepared ? "ready" : "transcoding - plays clean until ready")
                                : "off",
            pal.tile, cue.datamoshEnabled ? pal.fg : pal.inkSoft);
          inspDrawQuickRow(ix, rowY, "look", QuickAction::DatamoshLookPrev,
                           moshLookLabelFor(cue.datamoshLook),
                           QuickAction::DatamoshLookNext, QuickAction::ToggleLoop,
                           false, false,
                           "SUBTLE self-heals (H.264). CLASSIC is the real smear "
                           "(MPEG-4). EXTREME is chunkier and smears constantly. "
                           "Changing this re-prepares the cue.");
          rowY += ix.rowStep;
        }
      } else {
      std::ostringstream amount;
      amount << std::fixed << std::setprecision(2) << fx.amount;
      inspDrawQuickRow(ix, rowY, "amount",
                       QuickAction::EffectAmountDec, amount.str(),
                       QuickAction::EffectAmountInc, QuickAction::ToggleLoop,
                       false, false,
                       "Drag to scrub (shift = fine), click to type exact",
                       true, QuickAction::EffectEditAmount, i);
      rowY += ix.rowStep;
      // The extra parameters, named by the effect itself. An effect that does
      // not use one draws no row for it, so the inspector never shows a
      // control that cannot do anything -- and one that DOES use it is no
      // longer stuck at whatever the default happened to be.
      for (int which = 0; which < 4; ++which) {
        const char* paramLabel =
          deckboy::effects::cueEffectParamLabel(fx.kind, which);
        if (!paramLabel) {
          continue;
        }
        std::ostringstream value;
        value << std::fixed << std::setprecision(2)
              << (which == 0 ? fx.paramA : which == 1 ? fx.paramB
                 : which == 2 ? fx.paramC : fx.paramD);
        inspDrawQuickRow(ix, rowY, paramLabel,
                         which == 0 ? QuickAction::EffectParamADec
                         : which == 1 ? QuickAction::EffectParamBDec
                         : which == 2 ? QuickAction::EffectParamCDec
                                      : QuickAction::EffectParamDDec,
                         value.str(),
                         which == 0 ? QuickAction::EffectParamAInc
                         : which == 1 ? QuickAction::EffectParamBInc
                         : which == 2 ? QuickAction::EffectParamCInc
                                      : QuickAction::EffectParamDInc,
                         QuickAction::ToggleLoop, false, false,
                         deckboy::effects::cueEffectParamTip(fx.kind, which),
                         true,
                         which == 0 ? QuickAction::EffectParamAEdit
                         : which == 1 ? QuickAction::EffectParamBEdit
                         : which == 2 ? QuickAction::EffectParamCEdit
                                      : QuickAction::EffectParamDEdit,
                         i);
        rowY += ix.rowStep;
      }
      }
      // Second row: what it is, where it sits, and getting rid of it. Split
      // from the amount row because cramming six controls onto one line is how
      // the inspector's text placement went wrong before.
      {
        const int gap = 4;
        const int cellW = (ix.ctrlW - ix.inset * 2 - gap * 3) / 4;
        int cx = ix.ctrl.x + ix.inset;
        struct Cell { const char* label; QuickAction action; const char* tip; bool lit; };
        const Cell cells[4] = {
          {"B", QuickAction::EffectToggleBypass,
           "Bypass: take it out of the chain but KEEP its settings. Turning the "
           "amount to zero throws them away.", fx.bypassed},
          {"^", QuickAction::EffectMoveUp,   "Earlier in the stack", false},
          {"v", QuickAction::EffectMoveDown, "Later in the stack", false},
          {"X", QuickAction::EffectRemove,      "Remove this effect", false},
        };
        for (const Cell& cell : cells) {
          SDL_Rect r {cx, rowY, cellW, ix.rowH};
          // A bypassed effect reads as OFF at a glance, which is the whole
          // point of having the control at all.
          drawUIPanel(r, cell.lit ? pal.dark : pal.tile, pal.deep, pal.mid);
          drawCenteredTextSafe(controlRenderer_, fontSmall_, r, cell.label,
                               cell.lit ? pal.light : pal.fg);
          quickButtons_.push_back({r, cell.action, cell.tip, i});
          cx += cellW + gap;
        }
        rowY += ix.rowStep;
      }
    }
    if (stack.empty()) {
      rowY = inspDrawMessageRow(ix, rowY, "no effects", pal.tile, pal.inkSoft);
    }
    // The driver row only appears when the stack actually contains a puppet:
    // a "motion driver" control on a cue with no motion-puppet effect is a
    // control that cannot do anything.
    bool hasPuppet = false;
    for (const auto& fx : stack) {
      if (fx.kind == deckboy::effects::CueEffectKind::MotionPuppet) {
        hasPuppet = true;
        break;
      }
    }
    if (hasPuppet || !cue.motionDriverPath.empty()) {
      const std::string driver = cue.motionDriverPath.empty()
        ? std::string("none - click to choose")
        : fs::path(cue.motionDriverPath).filename().string();
      rowY = inspDrawActionRow(ix, rowY, "driver: " + driver,
                               QuickAction::MotionDriverPick,
                               "The clip whose MOTION drives this one. Its "
                               "pictures are never shown -- only the movement "
                               "its codec already measured.",
                               pal.tile, pal.fg);
      if (!cue.motionDriverPath.empty()) {
        // The driver is not a cue and never reaches the screen, so it has no
        // transport of its own. These are it.
        std::ostringstream speed;
        speed << std::fixed << std::setprecision(2) << cue.motionDriverSpeed;
        inspDrawQuickRow(ix, rowY, "driver speed", QuickAction::MotionDriverSpeedDec,
                         speed.str(), QuickAction::MotionDriverSpeedInc,
                         QuickAction::ToggleLoop, false, false,
                         "Fields per rendered frame. 1 is one for one, 0.5 holds "
                         "each field for two frames, 0 freezes it.",
                         false, QuickAction::ToggleLoop,
                         static_cast<int>(NumericParam::MotionDriverSpeed));
        rowY += ix.rowStep;
        inspDrawQuickRow(ix, rowY, "driver", QuickAction::MotionDriverPauseToggle,
                         cue.motionDriverPaused ? "HELD" : "running",
                         QuickAction::MotionDriverPauseToggle,
                         QuickAction::MotionDriverPauseToggle,
                         true, cue.motionDriverPaused,
                         "Held keeps the last displacement rather than removing "
                         "it -- the picture stays bent the way the driver left it.");
        rowY += ix.rowStep;
        inspDrawQuickRow(ix, rowY, "restart on take",
                         QuickAction::MotionDriverRestartOnTakeToggle,
                         cue.motionDriverRestartOnTake ? "on" : "off",
                         QuickAction::MotionDriverRestartOnTakeToggle,
                         QuickAction::MotionDriverRestartOnTakeToggle,
                         true, cue.motionDriverRestartOnTake,
                         "Every take starts the puppetry the same way, so a "
                         "rehearsed look repeats instead of depending on how "
                         "long the app has been open.");
        rowY += ix.rowStep;
      // A PREVIEW AND A TRANSPORT FOR THE DRIVER.
      //
      // The driver is not a cue: it never reaches the screen, nothing else in
      // the app reports on it, and until now an operator who armed one had no
      // way to tell whether it was running, where it had got to, or even
      // whether the clip they picked was the one they meant. Two controls fix
      // that -- a thumbnail so it can be recognised, and a bar so it can be
      // placed.
      //
      // The picture costs nothing: the decoder produced it on the way to the
      // vectors and was throwing it away.
      if (const MotionDriver* driver = motionDriverForDeck(project_.focusedDeckIndex)) {
        deckboy::motion::MotionSourceStatus status;
        if (driver->handle &&
            deckboy::motion::motionSourceStatus(driver->handle, status)) {
          const int previewH = ix.rowH * 2;
          SDL_Rect previewRect {ix.ctrl.x + ix.inset, rowY,
                                previewH * 16 / 9, previewH};
          drawUIPanel(previewRect, pal.deep, pal.deep, pal.mid);
          if (const std::uint8_t* luma =
                deckboy::motion::motionSourceThumbnail(driver->handle)) {
            if (status.frameIndex != motionDriverThumbFrame_ ||
                !motionDriverThumbTex_) {
              motionDriverThumbFrame_ = status.frameIndex;
              std::vector<std::uint8_t> rgba(
                static_cast<std::size_t>(status.thumbWidth) * status.thumbHeight * 4);
              for (std::size_t i = 0; i + 0 < rgba.size() / 4; ++i) {
                rgba[i * 4 + 0] = luma[i];
                rgba[i * 4 + 1] = luma[i];
                rgba[i * 4 + 2] = luma[i];
                rgba[i * 4 + 3] = 255;
              }
              syncTexture(controlRenderer_, motionDriverThumbTex_,
                          motionDriverThumbW_, motionDriverThumbH_,
                          status.thumbWidth, status.thumbHeight,
                          rgba.data(), status.thumbWidth * 4);
            }
            if (motionDriverThumbTex_) {
              SDL_Rect inner {previewRect.x + 2, previewRect.y + 2,
                              previewRect.w - 4, previewRect.h - 4};
              SDL_RenderTexture(controlRenderer_, motionDriverThumbTex_, nullptr, &inner);
            }
          }
          // The numbers next to it, because a thumbnail says "running" and
          // nothing else. Seconds and the field count are what an operator
          // needs to rehearse against.
          std::ostringstream where;
          where << std::fixed << std::setprecision(1) << status.positionSeconds;
          if (status.durationSeconds > 0.0) {
            where << " / " << status.durationSeconds;
          }
          where << "s  field " << status.frameIndex;
          SDL_Rect textRect {previewRect.x + previewRect.w + 6, rowY,
                             ix.ctrlW - ix.inset * 2 - previewRect.w - 6, ix.rowH};
          drawTextSafe(controlRenderer_, fontSmall_, textRect, where.str(),
                       pal.fgSoft);
          // The bar. Registered as a scrub rect rather than a quick button:
          // where along it you pressed IS the value, and a quick button only
          // knows that it was hit.
          SDL_Rect barRect {textRect.x, rowY + ix.rowH + 2,
                            std::max(20, textRect.w), ix.rowH - 4};
          drawUIPanel(barRect, pal.tile, pal.deep, pal.mid);
          if (status.durationSeconds > 0.0) {
            const double frac = std::clamp(
              status.positionSeconds / status.durationSeconds, 0.0, 1.0);
            SDL_Rect fill {barRect.x + 2, barRect.y + 2,
                           std::max(2, static_cast<int>((barRect.w - 4) * frac)),
                           barRect.h - 4};
            Primitives::fillRect(controlRenderer_, fill, pal.fg);
          } else {
            // A container that will not report a duration cannot be scrubbed
            // against one, and drawing a bar that goes nowhere would be a lie.
            drawCenteredTextSafe(controlRenderer_, fontSmall_, barRect,
                                 "no duration", pal.inkSoft);
          }
          motionDriverScrubRect_ = barRect;
          motionDriverScrubDuration_ = status.durationSeconds;
          motionDriverScrubDeck_ = project_.focusedDeckIndex;
          rowY += previewH + 4;
        }
      }
        rowY = inspDrawActionRow(ix, rowY, "restart driver now",
                                 QuickAction::MotionDriverRestart,
                                 "Jump the driver back to its first frame",
                                 pal.tile, pal.fg);
        rowY = inspDrawActionRow(ix, rowY, "clear driver",
                                 QuickAction::MotionDriverClear,
                                 "Stop puppeteering this cue", pal.tile, pal.inkSoft);
      }
    }
    rowY = inspDrawActionRow(ix, rowY, "+ add effect", QuickAction::EffectAdd,
                             "Append an effect to this cue's stack. Order is the "
                             "effect: posterise then invert is not invert then "
                             "posterise.",
                             pal.tile, pal.fg);
    // Moving a LOOK between cues, without dragging geometry and fades along
    // with it. The whole-cue COPY above brings everything; this brings the
    // chain and the driver and nothing else.
    {
      const int gap = 4;
      const int cellW = (ix.ctrlW - ix.inset * 2 - gap) / 2;
      SDL_Rect copyRect {ix.ctrl.x + ix.inset, rowY, cellW, ix.rowH};
      SDL_Rect pasteRect {copyRect.x + cellW + gap, rowY, cellW, ix.rowH};
      const bool haveChain = !effectChainClipboard_.empty();
      drawUIPanel(copyRect, pal.tile, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, copyRect,
                           "copy chain", stack.empty() ? pal.inkSoft : pal.fg);
      quickButtons_.push_back({copyRect, QuickAction::EffectChainCopy,
                               "Copy this cue's whole effect chain", 0});
      // Lit when there is something to paste, so the button says whether it
      // will do anything before it is pressed.
      drawUIPanel(pasteRect, haveChain ? pal.dark : pal.tile, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, pasteRect,
                           haveChain ? ("paste " + std::to_string(effectChainClipboard_.size())).c_str()
                                     : "paste chain",
                           haveChain ? pal.light : pal.inkSoft);
      quickButtons_.push_back({pasteRect, QuickAction::EffectChainPaste,
                               "Replace the effect chain on every selected cue "
                               "with the copied one. Geometry, fades and colour "
                               "are left alone.", 0});
      rowY += ix.rowStep;
    }
    return rowY;
  }

  int inspDrawVideoSynthRows(const InspectorCtx& ix, int startY, const Cue& cue) {
    int rowY = startY;
    if (cue.kind != CueKind::VideoSynth) return rowY;
    const VideoSynthSettings& v = cue.videoSynth;

    inspDrawQuickRow(ix, rowY, "shape", QuickAction::VsCycleShape,
                     vsShapeLabel(v.shape), QuickAction::VsCycleShape,
                     QuickAction::ToggleLoop, false, false,
                     "Diamond is the hard-edged Atari Video Music lattice; "
                     "rings tunnel well with feedback zoom.");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "mirror", QuickAction::VsCycleMirror,
                     vsMirrorLabel(v.mirror), QuickAction::VsCycleMirror,
                     QuickAction::ToggleLoop, false, false,
                     "Folding is what turns an arbitrary pattern into "
                     "something that reads as designed.");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "palette", QuickAction::VsCyclePalette,
                     vsPaletteLabel(v.palette), QuickAction::VsCyclePalette,
                     QuickAction::ToggleLoop, false, false,
                     "Amber is closest to the 1976 single-phosphor look.");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "speed", QuickAction::VsSpeedDec,
                     fmtFloat(v.speed, 2), QuickAction::VsSpeedInc,
                     QuickAction::ToggleLoop, false, false, "Master oscillator rate.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::VsSpeed));
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "scale", QuickAction::VsScaleDec,
                     fmtFloat(v.scale, 2), QuickAction::VsScaleInc,
                     QuickAction::ToggleLoop, false, false,
                     "How many features fit on screen.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::VsScale));
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "feedback", QuickAction::VsFeedbackDec,
                     fmtFloat(v.feedbackAmount, 2), QuickAction::VsFeedbackInc,
                     QuickAction::ToggleLoop, false, false,
                     "Each frame blended into the next. This is the Hypno "
                     "signature -- at 0 it is a pattern generator, above it "
                     "the picture starts feeding on itself.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::VsFeedback));
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "zoom", QuickAction::VsZoomDec,
                     fmtFloat(v.feedbackZoom, 3), QuickAction::VsZoomInc,
                     QuickAction::ToggleLoop, false, false,
                     "Above 1 tunnels inward, below 1 blooms outward.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::VsZoom));
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "audio", QuickAction::VsReactDec,
                     fmtFloat(v.audioReactivity, 2), QuickAction::VsReactInc,
                     QuickAction::ToggleLoop, false, false,
                     "How much playing audio widens and brightens the "
                     "pattern. 0 free-runs.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::VsReact));
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "detail", QuickAction::VsResDec,
                     std::to_string(v.resolution), QuickAction::VsResInc,
                     QuickAction::ToggleLoop, false, false,
                     "Internal resolution, 1 chunkiest to 5 finest. This is an "
                     "aesthetic control as much as a speed one -- the 8-bit "
                     "look comes from big pixels.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::VsResolution));
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "smear", QuickAction::VsSortDec,
                     fmtFloat(v.pixelSort, 2), QuickAction::VsSortInc,
                     QuickAction::ToggleLoop, false, false,
                     "Pixel sort: runs of pixels sorted by brightness within a "
                     "row, which is what makes datamosh look melted.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::VsPixelSort));
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "glitch", QuickAction::VsGlitchDec,
                     fmtFloat(v.glitch, 2), QuickAction::VsGlitchInc,
                     QuickAction::ToggleLoop, false, false,
                     "Torn scanline bands and RGB separation. In text mode it "
                     "also corrupts CELLS -- whole rows lock to one repeating "
                     "character, the way a real text screen loses sync.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::VsGlitch));
    rowY += ix.rowStep;

    inspDrawQuickRow(ix, rowY, "crt", QuickAction::VsCrtDec,
                     fmtFloat(v.crt, 2), QuickAction::VsCrtInc,
                     QuickAction::ToggleLoop, false, false,
                     "Scanlines, phosphor bloom and RGB fringing. The bloom is "
                     "the part that matters -- a phosphor spills sideways, "
                     "which is why a CRT looks like it is emitting light.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::VsCrt));
    rowY += ix.rowStep;
    // Sheet picking sits OUTSIDE the text-mode gate. It was inside, so a
    // sprite sheet could not be found without first toggling an unrelated
    // control -- the same discoverability trap the chip synths had, and the
    // owner hit it twice. Picking a sheet turns text mode on by itself.
    inspDrawQuickRow(ix, rowY, "sprites", QuickAction::VsSpriteSetPrev,
                     currentSpriteSetLabel(cue),
                     QuickAction::VsSpriteSetNext, QuickAction::ToggleLoop,
                     false, false,
                     "Sprite sets installed in data/sprites. Each folder there "
                     "is a set; cycling picks one and wraps back to none.");
    rowY += ix.rowStep;
    if (!v.spriteSheetPath.empty()) {
      inspDrawQuickRow(ix, rowY, "rotate", QuickAction::VsRotateCycle,
                       vsRotateLabel(v.spriteRotate),
                       QuickAction::VsRotateCycle, QuickAction::ToggleLoop,
                       false, false,
                       "Quarter turns are exact and stay crisp. Spinning "
                       "samples at an angle, which tears pixel art -- "
                       "sometimes that is what you want.");
      rowY += ix.rowStep;
      if (v.spriteRotate == 5) {
        inspDrawQuickRow(ix, rowY, "spin", QuickAction::VsFreeAngleDec,
                         fmtFloat(v.spriteFreeAngle, 0) + " deg/s",
                         QuickAction::VsFreeAngleInc, QuickAction::ToggleLoop,
                         false, false, "Rotation speed, degrees per second.",
                     false, QuickAction::ToggleLoop,
                     static_cast<int>(NumericParam::VsFreeAngle));
        rowY += ix.rowStep;
      }
      inspDrawQuickRow(ix, rowY, "flip", QuickAction::VsFlipCycle,
                       vsFlipLabel(v.spriteFlip),
                       QuickAction::VsFlipCycle, QuickAction::ToggleLoop,
                       false, false,
                       "Alternating mirrors by cell position, which reads as "
                       "pattern where random flipping reads as noise.");
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "jitter", QuickAction::VsJitterDec,
                       fmtFloat(v.spriteJitter, 2),
                       QuickAction::VsJitterInc, QuickAction::ToggleLoop,
                       false, false,
                       "Vary tile size per cell, so the grid stops looking "
                       "like a grid.");
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "chaos", QuickAction::VsChaosDec,
                       fmtFloat(v.spriteChaos, 2),
                       QuickAction::VsChaosInc, QuickAction::ToggleLoop,
                       false, false,
                       "0 picks tiles by brightness so the picture reads. 1 "
                       "picks at random so the grid becomes texture.");
      rowY += ix.rowStep;
    }

    inspDrawQuickRow(ix, rowY, "sheet", QuickAction::VsSheetPick,
                     v.spriteSheetPath.empty()
                       ? std::string("none")
                       : fs::path(v.spriteSheetPath).filename().string(),
                     QuickAction::VsSheetPick, QuickAction::VsSheetClear,
                     false, false,
                     "Load a sprite sheet and use its tiles as the alphabet. "
                     "Tiles are picked by brightness like glyphs are. "
                     "Right-click clears.");
    rowY += ix.rowStep;
    if (!v.spriteSheetPath.empty()) {
      inspDrawQuickRow(ix, rowY, "tile w", QuickAction::VsTileWDec,
                       std::to_string(v.spriteTileW),
                       QuickAction::VsTileWInc, QuickAction::ToggleLoop,
                       false, false,
                       "Tile width in pixels. Must match the sheet's grid or "
                       "the slices land across neighbouring sprites.");
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "tile h", QuickAction::VsTileHDec,
                       std::to_string(v.spriteTileH),
                       QuickAction::VsTileHInc, QuickAction::ToggleLoop,
                       false, false, "Tile height in pixels.");
      rowY += ix.rowStep;
    }
    inspDrawQuickRow(ix, rowY, "text mode", QuickAction::VsAsciiToggle,
                     v.ascii ? "on" : "off",
                     QuickAction::VsAsciiToggle, QuickAction::VsAsciiToggle,
                     true, v.ascii,
                     "Render as a character grid with a 16-colour indexed "
                     "palette, rather than as pixels.");
    rowY += ix.rowStep;
    if (v.ascii) {
      inspDrawQuickRow(ix, rowY, "columns", QuickAction::VsAsciiColsDec,
                       std::to_string(v.asciiCols), QuickAction::VsAsciiColsInc,
                       QuickAction::ToggleLoop, false, false,
                       "Characters across. Fewer means bigger cells.");
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "glyphs", QuickAction::VsCharSetCycle,
                       vsCharSetLabel(v.asciiCharSet),
                       QuickAction::VsCharSetCycle, QuickAction::ToggleLoop,
                       false, false,
                       "Blocks and dithers read as density; the ASCII ramp "
                       "reads as text; symbols read as wreckage.");
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "shuffle", QuickAction::VsShuffleCycle,
                       v.asciiShuffle == 0 ? std::string("by density")
                                           : std::to_string(v.asciiShuffle),
                       QuickAction::VsShuffleCycle, QuickAction::ToggleLoop,
                       false, false,
                       "Scramble which glyph means which brightness. Same set, "
                       "different handwriting. Seeded, so it stays put.");
      rowY += ix.rowStep;
      inspDrawQuickRow(ix, rowY, "ink", QuickAction::VsInkCycle,
                       vsInkLabel(v.asciiInk),
                       QuickAction::VsInkCycle, QuickAction::ToggleLoop,
                       false, false,
                       "Picture takes colour from the image. Green, amber and "
                       "cyan are terminal phosphors. Palette locks the text to "
                       "whichever hardware palette is selected above.");
      rowY += ix.rowStep;
    }
    return rowY;
  }

  int inspDrawEffectsRows(const InspectorCtx& ix, int startY, const Cue& cue) {
    // ONE LIST. Datamosh used to be permanently present here while every other
    // effect had to be added, which meant the section had two different rules
    // in it and no way to see what was on offer. It is an entry in the stack
    // now like anything else, and refuses with a message on cues it cannot
    // work on rather than being silently absent.
    if (!cueSupportsEffectStack(cue)) {
      return inspDrawMessageRow(ix, startY, "no effects for this cue type",
                                pal.mid, pal.deep);
    }
    return inspDrawEffectRows(ix, startY, cue);
  }

  int inspDrawKeyRows(const InspectorCtx& ix, int startY, const Cue& cue) {
    int rowY = startY;
    if (!cueSupportsKeying(&cue)) return rowY;
    inspDrawQuickRow(ix, rowY, "key", QuickAction::KeyToggle,
                     cue.chromaKeyEnabled ? "on" : "off",
                     QuickAction::KeyToggle, QuickAction::KeyToggle, true, cue.chromaKeyEnabled,
                     "Toggle chroma key");
    rowY += ix.rowStep;
    inspDrawKeyColorRow(ix, rowY, cue);
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "tolerance", QuickAction::KeyTolDec, fmtFloat(cue.chromaKeyTolerance, 1), QuickAction::KeyTolInc,
                     QuickAction::ToggleLoop, false, false, "Key tolerance (RGB distance)");
    rowY += ix.rowStep;
    inspDrawQuickRow(ix, rowY, "soft", QuickAction::KeySoftDec, fmtFloat(cue.chromaKeySoftness, 1), QuickAction::KeySoftInc,
                     QuickAction::ToggleLoop, false, false, "Key edge softness");
    rowY += ix.rowStep;
    return rowY;
  }

  InspectorSectionScope inspBeginSection(const InspectorCtx& ix, int rowY, const std::string& title,
                                          bool open, QuickAction toggleAction, const std::string& tip) {
    SDL_Rect hdr {ix.ctrl.x + ix.inset, rowY, ix.ctrlW - ix.inset * 2, ix.sectionHeaderH};
    SDL_Color fill = open ? pal.mid : pal.tile;
    SDL_Color ink = open ? pal.deep : pal.fg;
    drawUIPanel(hdr, fill, pal.deep, pal.dark);
    SDL_Rect titleRect {hdr.x + 4, hdr.y, hdr.w - 24, hdr.h};
    SDL_Rect stateRect {hdr.x + hdr.w - 18, hdr.y, 14, hdr.h};
    drawTextSafe(controlRenderer_, ix.headerFont, titleRect, title, ink);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, stateRect, open ? "-" : "+", ink);
    quickButtons_.push_back({hdr, toggleAction, tip});
    return InspectorSectionScope {hdr, open, rowY + ix.rowStep};
  }

  void inspFinishSection(const InspectorSectionScope& section, int bodyBottom) {
    if (!section.open) return;
    int shellBottom = std::max(section.headerRect.y + section.headerRect.h, bodyBottom + 2);
    // Track the deepest section bottom so the cue-settings scroll range covers
    // trailing non-interactive rows (status/message rows push no quickButtons_,
    // so the button-based scroll-max alone can leave them unreachable).
    inspectorSectionBottomMax_ = std::max(inspectorSectionBottomMax_, shellBottom);
    SDL_Rect shell {
      section.headerRect.x,
      section.headerRect.y,
      section.headerRect.w,
      std::max(section.headerRect.h, shellBottom - section.headerRect.y)
    };
    SDL_Rect bodyFill {
      shell.x + 2,
      section.bodyStartY - 4,
      std::max(0, shell.w - 4),
      std::max(0, shellBottom - section.bodyStartY + 2)
    };
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    Primitives::fillRect(controlRenderer_, bodyFill, SDL_Color {15, 56, 15, 28});
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    Primitives::strokeRect(controlRenderer_, shell, pal.dark);
    // (An accent "rail" used to be drawn 8px left of the shell here. Floating
    // outside the frame it read as a stray vertical green bar, not chrome —
    // the shell stroke + body tint already delimit the open section.)
  }

  void drawUIPanel(const SDL_Rect& rect, SDL_Color fill, SDL_Color border, SDL_Color accent) {
    if (rect.w <= 0 || rect.h <= 0) {
      return;
    }
    // Draw exactly the rect the caller computed. This used to paint
    // snapRectToGrid(rect) — an 8px grid snap — while Primitives::drawFramedPanel
    // (the other half of the UI) painted the raw rect, and the text helpers were
    // split the same way. A label could therefore sit up to a full grid unit off
    // centre relative to the box around it, and two neighbouring controls drawn
    // with different helper pairs disagreed visibly. One coordinate space now:
    // the box and its label both use the caller's rect. See safeTextRect.
    SDL_Rect snapped = rect;
    Primitives::fillRect(controlRenderer_, snapped, fill);
    Primitives::strokeRect(controlRenderer_, snapped, border);
    if (snapped.w > 4 && snapped.h > 4) {
      // Bevel: accent brighter than fill → raised, darker → inset
      int fillLuma  = fill.r + fill.g + fill.b;
      int accentLuma = accent.r + accent.g + accent.b;
      bool raised = (accentLuma >= fillLuma);
      SDL_Color hi = raised ? accent : fill;
      SDL_Color lo = {
        static_cast<Uint8>(std::min(255, (raised ? fill.r : accent.r) * 2 / 3)),
        static_cast<Uint8>(std::min(255, (raised ? fill.g : accent.g) * 2 / 3)),
        static_cast<Uint8>(std::min(255, (raised ? fill.b : accent.b) * 2 / 3)),
        (raised ? fill : accent).a
      };
      int x1 = snapped.x + 1, y1 = snapped.y + 1;
      int x2 = snapped.x + snapped.w - 2, y2 = snapped.y + snapped.h - 2;
      // Top + left highlight
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(controlRenderer_, hi.r, hi.g, hi.b, hi.a);
      SDL_RenderLine(controlRenderer_, x1, y1, x2, y1);
      SDL_RenderLine(controlRenderer_, x1, y1, x1, y2);
      // Bottom + right shadow
      SDL_SetRenderDrawColor(controlRenderer_, lo.r, lo.g, lo.b, lo.a);
      SDL_RenderLine(controlRenderer_, x1, y2, x2, y2);
      SDL_RenderLine(controlRenderer_, x2, y1, x2, y2);
    }
  }

  void drawUILabel(const SDL_Rect& rect, const std::string& text, SDL_Color color, TTF_Font* font = nullptr) {
    drawTextSafe(controlRenderer_, font ? font : fontSmall_, rect, text, color);
  }

  void drawUIButton(const SDL_Rect& rect, const std::string& text, SDL_Color fill, SDL_Color ink,
                    bool emphasized = false, const std::string& sublabel = "") {
    if (rect.w <= 0 || rect.h <= 0) {
      return;
    }
    SDL_Color accent = emphasized ? pal.light : pal.dark;
    drawUIPanel(rect, fill, pal.deep, accent);
    SDL_Rect topBand {rect.x + 2, rect.y + 2, std::max(0, rect.w - 4), std::min(6, std::max(3, rect.h / 6))};
    SDL_Color bandFill = emphasized ? pal.light : pal.mid;
    Primitives::fillRect(controlRenderer_, topBand, bandFill);
    TTF_Font* titleFont = (rect.h < 34 || rect.w < 112 || text.size() > 7) ? fontSmall_ : fontBase_;
    std::string clippedTitle = ellipsizeToPixelWidth(titleFont, text, std::max(0, rect.w - 10));
    if (sublabel.empty()) {
      SDL_Rect titleRect {rect.x + 4, rect.y + 8, rect.w - 8, std::max(14, rect.h - 14)};
      drawCenteredTextSafe(controlRenderer_, titleFont, titleRect, clippedTitle, ink);
      return;
    }
    SDL_Rect titleRect {rect.x, rect.y + 6, rect.w, std::max(14, rect.h / 2 - 2)};
    SDL_Rect hintRect {rect.x, rect.y + rect.h / 2 + 2, rect.w, std::max(10, rect.h / 2 - 4)};
    drawCenteredTextSafe(controlRenderer_, titleFont, titleRect, clippedTitle, ink);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, hintRect, sublabel, ink);
  }

  void drawUIDropdown(const SDL_Rect& rect, const std::string& label, const std::string& value,
                      const std::string& owner) {
    bool active = dropdown_.open && dropdown_.owner == owner;
    SDL_Color fill = active ? pal.dark : pal.light;
    SDL_Color ink = active ? pal.light : pal.deep;
    drawUIPanel(rect, fill, pal.deep, pal.mid);
    SDL_Rect labelRect {rect.x + 4, rect.y, rect.w - 20, rect.h};
    SDL_Rect chevronRect {rect.x + rect.w - 20, rect.y, 16, rect.h};
    drawUILabel(labelRect, label + ": " + value, ink, fontSmall_);
    drawCenteredTextSafe(controlRenderer_, fontSmall_, chevronRect, "\xe2\x96\xbc", ink);
  }

  struct UiImageAsset {
    fs::path path;
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
    bool attemptedLoad = false;
  };

  std::optional<std::pair<int, int>> probeStillImageSize(const fs::path& path) const {
    auto output = readAllText({
      "ffprobe", "-v", "error",
      "-select_streams", "v:0",
      "-show_entries", "stream=width,height",
      "-of", "csv=p=0:s=x",
      path.string()
    });
    if (!output) {
      return std::nullopt;
    }
    std::string text = trim(*output);
    size_t sep = text.find('x');
    if (sep == std::string::npos) {
      return std::nullopt;
    }
    try {
      int w = std::stoi(text.substr(0, sep));
      int h = std::stoi(text.substr(sep + 1));
      if (w <= 0 || h <= 0) {
        return std::nullopt;
      }
      return std::make_pair(w, h);
    } catch (...) {
      return std::nullopt;
    }
  }

  bool decodeStillImageRgba(const fs::path& path, int width, int height, std::vector<std::uint8_t>& outPixels) const {
    if (width <= 0 || height <= 0) {
      return false;
    }
    constexpr size_t kMaxUiDecodeBytes = 64u * 1024u * 1024u;
    size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (byteCount == 0 || byteCount > kMaxUiDecodeBytes) {
      return false;
    }

    ChildProcess process;
    if (!spawnPipeProcess(process, {
      "ffmpeg", "-hide_banner", "-loglevel", "error",
      "-i", path.string(),
      "-frames:v", "1",
      "-f", "rawvideo",
      "-pix_fmt", "rgba",
      "pipe:1"
    })) {
      return false;
    }

    outPixels.resize(byteCount);
    bool ok = readExact(process.readFd, outPixels.data(), outPixels.size());
    process.stop();
    return ok;
  }

  void releaseUiImage(UiImageAsset& asset) {
    if (asset.texture) {
      SDL_DestroyTexture(asset.texture);
      asset.texture = nullptr;
    }
    asset.width = 0;
    asset.height = 0;
    asset.attemptedLoad = false;
  }

  bool ensureUiImageLoaded(UiImageAsset& asset) {
    if (asset.texture) {
      return true;
    }
    if (asset.attemptedLoad) {
      return false;
    }
    asset.attemptedLoad = true;
    if (asset.path.empty() || !fs::exists(asset.path)) {
      return false;
    }
    auto size = probeStillImageSize(asset.path);
    if (!size) {
      return false;
    }
    std::vector<std::uint8_t> rgba;
    if (!decodeStillImageRgba(asset.path, size->first, size->second, rgba)) {
      return false;
    }
    SDL_Texture* texture = deckboyCreateTexture(controlRenderer_, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STATIC, size->first, size->second);
    if (!texture) {
      return false;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(texture, nullptr, rgba.data(), size->first * 4);
    asset.texture = texture;
    asset.width = size->first;
    asset.height = size->second;
    return true;
  }

  bool drawUiImageContain(UiImageAsset& asset, const SDL_Rect& bounds, Uint8 alpha = 255,
                          SDL_Color tint = {255, 255, 255, 255}) {
    if (bounds.w <= 0 || bounds.h <= 0) {
      return false;
    }
    if (!ensureUiImageLoaded(asset) || !asset.texture || asset.width <= 0 || asset.height <= 0) {
      return false;
    }
    double sx = static_cast<double>(bounds.w) / static_cast<double>(asset.width);
    double sy = static_cast<double>(bounds.h) / static_cast<double>(asset.height);
    double scale = std::min(sx, sy);
    int drawW = std::max(1, static_cast<int>(std::lround(static_cast<double>(asset.width) * scale)));
    int drawH = std::max(1, static_cast<int>(std::lround(static_cast<double>(asset.height) * scale)));
    SDL_Rect dst {
      bounds.x + (bounds.w - drawW) / 2,
      bounds.y + (bounds.h - drawH) / 2,
      drawW,
      drawH
    };
    SDL_SetTextureColorMod(asset.texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(asset.texture, alpha);
    SDL_RenderTexture(controlRenderer_, asset.texture, nullptr, &dst);
    SDL_SetTextureAlphaMod(asset.texture, 255);
    SDL_SetTextureColorMod(asset.texture, 255, 255, 255);
    return true;
  }

  bool drawUiImageContainTinted(UiImageAsset& asset, const SDL_Rect& bounds, Uint8 alpha = 255) {
    return drawUiImageContain(asset, bounds, alpha, pal.deep);
  }

  bool drawUiImageCover(UiImageAsset& asset, const SDL_Rect& bounds, Uint8 alpha = 255) {
    if (bounds.w <= 0 || bounds.h <= 0) return false;
    if (!ensureUiImageLoaded(asset) || !asset.texture || asset.width <= 0 || asset.height <= 0) return false;
    double sx = static_cast<double>(bounds.w) / static_cast<double>(asset.width);
    double sy = static_cast<double>(bounds.h) / static_cast<double>(asset.height);
    double scale = std::max(sx, sy);
    int drawW = std::max(1, static_cast<int>(std::lround(static_cast<double>(asset.width) * scale)));
    int drawH = std::max(1, static_cast<int>(std::lround(static_cast<double>(asset.height) * scale)));
    SDL_Rect dst {
      bounds.x + (bounds.w - drawW) / 2,
      bounds.y + (bounds.h - drawH) / 2,
      drawW, drawH
    };
    SDL_SetTextureAlphaMod(asset.texture, alpha);
    SDL_SetRenderClipRect(controlRenderer_, &bounds);
    SDL_RenderTexture(controlRenderer_, asset.texture, nullptr, &dst);
    SDL_SetRenderClipRect(controlRenderer_, nullptr);
    SDL_SetTextureAlphaMod(asset.texture, 255);
    return true;
  }

  // ─── Theme system ─────────────────────────────────────────────────────────
  // Reads DECKBOY_THEME env var and applies the matching theme file.
  // Theme files live at data/themes/<name>/theme.txt.
  // Keys map directly onto the kScreen*/kShell* color inline globals.

  void applyThemeFromFile(const fs::path& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    auto parseHex = [](const std::string& s) -> std::uint32_t {
      try { return static_cast<std::uint32_t>(std::stoul(s, nullptr, 16)); }
      catch (...) { return 0xFFFFFFFFu; }
    };
    bool sawFg = false;
    bool sawTile = false;
    bool sawFgSoft = false;
    while (std::getline(f, line)) {
      if (line.empty() || line[0] == '#') continue;
      auto tab = line.find('\t');
      if (tab == std::string::npos) continue;
      std::string key = line.substr(0, tab);
      std::string val = line.substr(tab + 1);
      // trim trailing whitespace
      while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' '))
        val.pop_back();
      std::uint32_t c = parseHex(val);
      if      (key == "shell_outer")     kShellOuterColor    = c;
      else if (key == "shell_inner")     kShellInnerColor    = c;
      else if (key == "shell_shadow")    kShellShadowColor   = c;
      else if (key == "screen_light")    kScreenLightColor   = c;
      else if (key == "screen_mid")      kScreenMidColor     = c;
      else if (key == "screen_dark")     kScreenDarkColor    = c;
      else if (key == "screen_deep")     kScreenDeepColor    = c;
      else if (key == "screen_ink_soft") kScreenInkSoftColor = c;
      else if (key == "screen_fg")     { kScreenFgColor      = c; sawFg = true; }
      else if (key == "screen_tile")   { kScreenTileColor    = c; sawTile = true; }
      else if (key == "screen_fg_soft"){ kScreenFgSoftColor  = c; sawFgSoft = true; }
      else if (key == "button_bezel")    kButtonBezelColor   = c;
      else if (key == "delete_bezel")    kDeleteBezelColor   = c;
      else if (key == "scanline_alpha") {
        try { pal.scanlineAlpha = static_cast<Uint8>(std::clamp(std::stoi(val), 0, 255)); }
        catch (...) { pal.scanlineAlpha = 18; }
      }
    }
    // Back-compat: a theme without screen_fg uses its screen_deep as on-body
    // ink, and without screen_tile uses its screen_light as tile fill —
    // exactly the old behavior, so every existing theme is untouched.
    if (!sawFg) kScreenFgColor = kScreenDeepColor;
    if (!sawTile) kScreenTileColor = kScreenLightColor;
    if (!sawFgSoft) kScreenFgSoftColor = kScreenDarkColor;
  }

  void loadTheme(const std::string& name) {
    fs::path themePath = Paths::dataDir() / "themes" / name / "theme.txt";
    if (!fs::exists(themePath)) {
      std::cerr << "Theme not found: " << themePath << '\n';
      return;
    }
    applyThemeFromFile(themePath);
    rebuildPalette();
    currentThemeName_ = name;
  }

  void loadThemeFromEnv() {
    const char* env = std::getenv("DECKBOY_THEME");
    if (!env || std::string(env).empty()) return;
    loadTheme(std::string(env));
  }

  // Splash candidate chain for a given character name. The pick() helper in
  // initUiAssetPackPaths takes the first that exists on disk. The .gif/.mp4
  // entries are placeholders — when the splash overlay learns to play an
  // animated form, these get tried before the static .png. The legacy v2
  // filename (splash_boot_deckgirl@1x.png) and the v074 plain art stay at
  // the tail so a stripped pack still boots without a missing-asset hole.
  std::vector<fs::path> pickSplashCandidates(const std::string& character) {
    std::string name = character.empty() ? std::string("deckbot") : character;
    return {
      fs::path("splash") / (std::string("deckboy_splash_") + name + ".mp4"),
      fs::path("splash") / (std::string("deckboy_splash_") + name + ".gif"),
      fs::path("splash") / (std::string("deckboy_splash_") + name + ".png"),
      fs::path("splash") / "splash_boot_deckgirl@1x.png",
      fs::path("splash") / "deckboy_splash_v074.png"
    };
  }

  // Free every TTF_Font we hold. Used during shutdown and any time we need
  // to reload fonts at a different point size (UI scale change).
  void releaseFonts() {
    auto close = [](TTF_Font*& f) { if (f) { TTF_CloseFont(f); f = nullptr; } };
    close(fontLarge_);
    close(fontBase_);
    close(fontSmall_);
    close(fontMono_);
    close(fontPixel_);
    close(fontPixelSmall_);
    close(fontPixelTitle_);
  }

  // Load (or reload) the six UI fonts at sizes derived from the linux
  // baseline (large 32, base 21, small 17, mono 18, pixel 24, pixel-small
  // 12) multiplied by the operator's UI scale. Windows historically renders
  // ~2pt larger than the layout expects because of how its DPI baseline
  // differs, so we apply a small platform nudge before the scale.
  bool loadFonts(double scale) {
    if (!std::isfinite(scale) || scale <= 0.0) {
      scale = 1.0;
    }
    scale = std::clamp(scale, 0.75, 3.0);
#ifdef _WIN32
    const double platformNudge = 0.90;  // see comment above
#else
    const double platformNudge = 1.00;
#endif
    const double k = scale * platformNudge;
    auto pt = [&](int base) {
      return std::max(6, static_cast<int>(std::lround(base * k)));
    };
    const auto sans  = Paths::fontPath(Paths::FontName::Sans).string();
    const auto mono  = Paths::fontPath(Paths::FontName::Mono).string();
    const auto pixel = Paths::fontPath(Paths::FontName::Pixel).string();
    // HYBRID (the owner, 2026-08-19): pixel face on the CHROME, readable sans for
    // user text. Press Start 2P looks right and reads fine on short strings you
    // already know -- button labels, panel headers, section titles. It reads
    // badly on arbitrary filenames: at UI sizes 0/O and 6/G are near-identical,
    // so "S06E01" scanned as "S0GE0" in the cue list.
    //
    // So these three stay SANS. Chrome sites use fontPixel_/fontPixelSmall_.
    // Do not "unify" them back to one face; the split is the whole point.
    fontLarge_      = TTF_OpenFont(sans.c_str(),  pt(32));
    fontBase_       = TTF_OpenFont(sans.c_str(),  pt(21));
    fontSmall_      = TTF_OpenFont(sans.c_str(),  pt(17));
    fontMono_       = TTF_OpenFont(mono.c_str(),  pt(18));
    fontPixel_      = TTF_OpenFont(pixel.c_str(), pt(24));
    fontPixelSmall_ = TTF_OpenFont(pixel.c_str(), pt(12));
    fontPixelTitle_ = TTF_OpenFont(pixel.c_str(), pt(42));  // splash/startup headline
    // Kerning OFF for every UI font. At these pixel sizes a negative kern pair
    // rounds to a whole pixel or two, which tucks the second glyph under the
    // first hard enough that the word visibly splits — "Target URL" rendered as
    // "Ta rget URL", "Top" as "To p". Typographically the kern is "correct";
    // at 17px in a control label it just reads as a broken word, and it made
    // label widths depend on which letters happened to be adjacent. Even
    // advances keep text placement predictable, which is the contract this UI
    // holds itself to. TTF_GetStringSize measures with the same setting, so
    // layout and paint stay in agreement.
    for (TTF_Font* f : {fontLarge_, fontBase_, fontSmall_, fontMono_,
                        fontPixel_, fontPixelSmall_, fontPixelTitle_}) {
      if (f) {
        TTF_SetFontKerning(f, false);
      }
    }
    return fontLarge_ && fontBase_ && fontSmall_ && fontMono_;
  }

  // True when the operator is in touch mode (Pocket 3 preset or a future
  // touch-explicit setting). Used by render code to skip hover-only
  // affordances that don't translate to a tap-only input model.
  bool inTouchMode() const { return project_.interactionMode == "touch"; }

  // Recompute the live layout-metric globals (kLayoutHeaderHeight etc.) from
  // their immutable base values × the given scale. Snaps each result to at
  // least 1px to avoid divide-by-zero in snapDownToGrid. Callers should hit
  // this before the next render frame so panel/button geometry matches the
  // new metrics.
  // Scale a pixel constant that was authored at 1x UI scale.
  //
  // Fonts and the kLayout* metrics already follow Project::uiScale, but panel
  // geometry inside the settings modal is written as literal pixels. Routing
  // those literals through here keeps chrome and type growing together. At
  // uiScale 1.0 this is the identity, so the 1x layout is bit-identical — that
  // property is what makes the sweep safe to apply widely.
  int uiScaled(int base) const {
    double scale = project_.uiScale;
    if (!std::isfinite(scale) || scale <= 0.0) {
      scale = 1.0;
    }
    if (scale == 1.0) {
      return base;
    }
    int scaled = static_cast<int>(std::lround(base * scale));
    return base > 0 ? std::max(1, scaled) : scaled;
  }

  void rebuildLayoutMetrics(double scale) {
    if (!std::isfinite(scale) || scale <= 0.0) scale = 1.0;
    auto sc = [scale](int base) {
      return std::max(1, static_cast<int>(std::lround(base * scale)));
    };
    kLayoutSpacingUnit     = sc(kLayoutSpacingUnitBase);
    kLayoutPanelPadding    = sc(kLayoutPanelPaddingBase);
    kLayoutPanelGap        = sc(kLayoutPanelGapBase);
    kLayoutPanelBorder     = std::max(1, sc(kLayoutPanelBorderBase));
    kLayoutTextInset       = sc(kLayoutTextInsetBase);
    kLayoutHeaderHeight    = sc(kLayoutHeaderHeightBase);
    kLayoutBottomBarHeight = sc(kLayoutBottomBarHeightBase);
    kLayoutButtonHeight    = sc(kLayoutButtonHeightBase);
    kLayoutButtonPadding   = sc(kLayoutButtonPaddingBase);
    kLayoutButtonGap       = sc(kLayoutButtonGapBase);
  }

  // Re-open every font AND rebuild every layout metric at the project's
  // current UI scale. Safe to call from the settings handler — releaseFonts
  // drops the old textures and the existing rendering code picks the new
  // fonts and metrics up on the next frame.
  void applyUiScale() {
    releaseFonts();
    if (!loadFonts(project_.uiScale)) {
      // Fall back to 1.0× so the UI isn't fontless. The new value is still
      // persisted; the operator can try again or pick a smaller scale.
      releaseFonts();
      loadFonts(1.0);
      rebuildLayoutMetrics(1.0);
      return;
    }
    rebuildLayoutMetrics(project_.uiScale);
  }

  // Re-resolve and reload the splash texture using the current project
  // setting. Safe to call any time the character changes — releaseUiImage
  // drops the cached texture so the next render frame re-decodes from disk.
  // True only on the very first launch on this machine. Determined ONCE at
  // boot and remembered for the session, so the answer cannot change halfway
  // through (refreshSplashAsset runs again whenever the mascot changes).
  //
  // The marker lives in stateDir() because it is written state, not a bundled
  // resource -- inside a macOS .app the resource dir is sealed by the code
  // signature and writing there breaks `codesign --verify`.
  void resolveFirstRunFlag() {
    std::error_code ec;
    const fs::path marker = Paths::stateDir() / "deckboy-first-run";
    if (fs::exists(marker, ec)) {
      firstRunEver_ = false;
      return;
    }
    firstRunEver_ = true;
    fs::create_directories(Paths::stateDir(), ec);
    std::ofstream out(marker);
    // Nothing reads the contents; a human finding the file should be able to
    // tell what it is and that deleting it is harmless.
    out << "Deckboy has been launched on this machine.\n"
        << "Delete this file to see the first-run splash again.\n";
    // If the write fails the flag still holds for THIS session -- the operator
    // gets the branded splash once more next boot, which is harmless. Better
    // than refusing to boot over a splash.
  }

  void refreshSplashAsset() {
    if (!uiPackAvailable_) {
      return;
    }
    fs::path chosen;
    std::error_code ec;
    // Every boot draws a different splash, on EVERY theme. This used to be
    // gated so the default colorway always showed the branded wordmark, which
    // meant most operators never saw the pool at all.
    //
    // Two pools, differing only in how much theme tint they can take:
    //   splash/cycle/       grayscale masters  -> full accent tint
    //   splash/cycle_color/ finished colour art -> light tint only
    std::vector<fs::path> gray, colour;
    auto gather = [&](const fs::path& dir, std::vector<fs::path>& into) {
      if (!fs::is_directory(dir, ec)) return;
      for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file() && e.path().extension() == ".png") {
          into.push_back(e.path());
        }
      }
      std::sort(into.begin(), into.end());
    };
    gather(uiPackRoot_ / "splash" / "cycle", gray);
    gather(uiPackRoot_ / "splash" / "cycle_color", colour);

    // The branded wordmark splash joins the rotation rather than being a
    // special case that suppressed it. This is also what keeps the Mascot
    // setting HONEST: once every boot draws from the pool, a setting that only
    // fed the old branded-only path would change nothing an operator could
    // ever see -- a dead control.
    std::vector<fs::path> branded;
    for (const auto& rel : pickSplashCandidates(project_.splashCharacter)) {
      fs::path candidate = uiPackRoot_ / rel;
      if (fs::exists(candidate, ec)) { branded.push_back(candidate); break; }
    }

    // The VERY FIRST launch on this machine always shows the green branded
    // splash. A first impression should be the wordmark, not a random scene --
    // the rotation is a reward for coming back, not the introduction. Every
    // later boot draws from the pool.
    std::vector<fs::path> pool;
    if (firstRunEver_ && !branded.empty()) {
      pool.push_back(branded.front());   // a pool of exactly one: the wordmark
    } else {
      pool = gray;
      pool.insert(pool.end(), colour.begin(), colour.end());
      pool.insert(pool.end(), branded.begin(), branded.end());
    }
    if (!pool.empty()) {
      // Seeded from random_device, not the performance counter: the counter is
      // read at almost the same point in every boot, so its low bits are not
      // reliably spread across a small pool.
      static std::mt19937 rng{std::random_device{}()};
      std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
      std::size_t idx = pick(rng);
      // Avoid repeating the previous boot's pick when there is a choice, so
      // "random" does not visibly stutter on the same image twice running.
      if (pool.size() > 1 && pool[idx] == lastSplashChoice_) {
        idx = (idx + 1) % pool.size();
      }
      chosen = pool[idx];
      lastSplashChoice_ = chosen;
      // Grayscale masters take the accent fully; finished colour art takes a
      // little; the branded wordmark takes none, since it is already drawn in
      // the house colours and tinting it would fight its own artwork.
      if (std::find(branded.begin(), branded.end(), chosen) != branded.end()) {
        splashTintStrength_ = 0.0f;
      } else if (std::find(colour.begin(), colour.end(), chosen) != colour.end()) {
        splashTintStrength_ = 0.35f;
      } else {
        splashTintStrength_ = 1.0f;
      }
    } else {
      splashTintStrength_ = 0.0f;
    }
    if (chosen.empty()) {
      auto candidates = pickSplashCandidates(project_.splashCharacter);
      chosen = uiPackRoot_ / candidates.front();
      for (const auto& rel : candidates) {
        fs::path candidate = uiPackRoot_ / rel;
        if (fs::exists(candidate)) {
          chosen = candidate;
          break;
        }
      }
    }
    if (uiSplashArt_.path != chosen) {
      releaseUiImage(uiSplashArt_);
      uiSplashArt_.path = chosen;
    }
    ensureUiImageLoaded(uiSplashArt_);
  }

  void initUiAssetPackPaths() {
    fs::path dataDir = Paths::dataDir();
    fs::path root = dataDir / kUiPackRelativePathV3;
    if (!fs::exists(root)) {
      root = dataDir / kUiPackRelativePathV2;
    }
    uiPackRoot_ = root;
    uiPackAvailable_ = fs::exists(root);
    if (!uiPackAvailable_) {
      return;
    }

    auto pick = [&](const std::vector<fs::path>& relativeCandidates) -> fs::path {
      for (const auto& rel : relativeCandidates) {
        fs::path candidate = root / rel;
        if (fs::exists(candidate)) {
          return candidate;
        }
      }
      return root / relativeCandidates.front();
    };

    uiHeaderArt_.path = pick({
      fs::path("header") / "header_default.png",
      fs::path("header") / "header_default@1x.png"
    });
    uiAboutLogo_.path = pick({
      fs::path("header") / "about_logo.png"
    });
    // Splash is selected from project_.splashCharacter ("deckbot" or
    // "deckgirl"). The named PNGs live next to a legacy fallback; future
    // animated forms (.gif/.mp4) will be picked up by extending the
    // pickSplashCandidates helper rather than by patching this list.
    uiSplashArt_.path = pick(pickSplashCandidates(project_.splashCharacter));
    uiMonitorFrameArt_.path = pick({
      fs::path("monitor") / "monitor_frame.png",
      fs::path("monitor") / "monitor_frame@1x.png"
    });
    uiOutputChipIdleArt_.path = pick({
      fs::path("outputs") / "chip_idle.png",
      fs::path("outputs") / "output_chip_idle@1x.png"
    });
    uiOutputChipArmedArt_.path = pick({
      fs::path("outputs") / "chip_armed.png",
      fs::path("outputs") / "output_chip_armed@1x.png"
    });
    uiOutputChipLiveArt_.path = pick({
      fs::path("outputs") / "chip_live.png",
      fs::path("outputs") / "output_chip_live@1x.png"
    });
    uiOutputChipWarnArt_.path = pick({
      fs::path("outputs") / "chip_warning.png",
      fs::path("outputs") / "output_chip_warn@1x.png"
    });
    uiOutputChipOffArt_.path = pick({
      fs::path("outputs") / "chip_offline.png",
      fs::path("outputs") / "output_chip_off@1x.png"
    });
    uiCueIconVideo_.path = pick({fs::path("cue_icons") / "video.png"});
    uiCueIconImage_.path = pick({fs::path("cue_icons") / "image.png"});
    uiCueIconBrowser_.path = pick({fs::path("cue_icons") / "browser.png"});
    uiCueIconPattern_.path = pick({fs::path("cue_icons") / "pattern.png"});
    uiCueIconPip_.path = pick({fs::path("cue_icons") / "pip.png",
                               fs::path("cue_icons") / "source.png"});
    uiCueIconLowerThird_.path = pick({fs::path("cue_icons") / "lowerthird.png"});
    uiCueIconSource_.path = pick({fs::path("cue_icons") / "source.png"});
    uiCueIconAudio_.path = pick({fs::path("cue_icons") / "audio.png"});
    // Control / toolbar button icons
    uiBtnImport_.path   = pick({fs::path("toolbar") / "import.png"});
    uiBtnPlay_.path     = pick({fs::path("controls") / "play.png"});
    uiBtnPause_.path    = pick({fs::path("controls") / "pause.png"});
    uiBtnTake_.path     = pick({fs::path("controls") / "take.png"});
    uiBtnStop_.path     = pick({fs::path("controls") / "stop.png"});
    uiBtnRerack_.path   = pick({fs::path("controls") / "rerack.png"});
    uiBtnClear_.path    = pick({fs::path("controls") / "clear.png"});
    uiBtnSettings_.path = pick({fs::path("toolbar") / "prefs.png"});
    uiBtnFullscreen_.path = pick({fs::path("controls") / "fullscreen.png"});
    uiBtnWindow_.path     = pick({fs::path("controls") / "window.png"});
    uiBtnBlackout_.path   = pick({fs::path("controls") / "blackout.png"});
    uiModeLoopOn_.path    = pick({fs::path("mode_icons") / "loop_on.png"});
    uiModeOnce_.path      = pick({fs::path("mode_icons") / "once.png"});
    uiModeShuffleOn_.path = pick({fs::path("mode_icons") / "shuffle_on.png"});
    uiModeOrder_.path     = pick({fs::path("mode_icons") / "order.png"});
  }

  void preloadUiAssets() {
    if (!uiPackAvailable_) {
      return;
    }
    ensureUiImageLoaded(uiHeaderArt_);
    ensureUiImageLoaded(uiAboutLogo_);
    ensureUiImageLoaded(uiSplashArt_);
    ensureUiImageLoaded(uiMonitorFrameArt_);
    ensureUiImageLoaded(uiOutputChipIdleArt_);
    ensureUiImageLoaded(uiOutputChipArmedArt_);
    ensureUiImageLoaded(uiOutputChipLiveArt_);
    ensureUiImageLoaded(uiOutputChipWarnArt_);
    ensureUiImageLoaded(uiOutputChipOffArt_);
    ensureUiImageLoaded(uiCueIconVideo_);
    ensureUiImageLoaded(uiCueIconImage_);
    ensureUiImageLoaded(uiCueIconBrowser_);
    ensureUiImageLoaded(uiCueIconPattern_);
    ensureUiImageLoaded(uiCueIconPip_);
    ensureUiImageLoaded(uiCueIconLowerThird_);
    ensureUiImageLoaded(uiCueIconSource_);
    ensureUiImageLoaded(uiCueIconAudio_);
    ensureUiImageLoaded(uiBtnImport_);
    ensureUiImageLoaded(uiBtnPlay_);
    ensureUiImageLoaded(uiBtnPause_);
    ensureUiImageLoaded(uiBtnTake_);
    ensureUiImageLoaded(uiBtnStop_);
    ensureUiImageLoaded(uiBtnRerack_);
    ensureUiImageLoaded(uiBtnClear_);
    ensureUiImageLoaded(uiBtnSettings_);
    ensureUiImageLoaded(uiBtnFullscreen_);
    ensureUiImageLoaded(uiBtnWindow_);
    ensureUiImageLoaded(uiBtnBlackout_);
    ensureUiImageLoaded(uiModeLoopOn_);
    ensureUiImageLoaded(uiModeOnce_);
    ensureUiImageLoaded(uiModeShuffleOn_);
    ensureUiImageLoaded(uiModeOrder_);
  }

  void releaseUiAssets() {
    releaseUiImage(uiHeaderArt_);
    releaseUiImage(uiAboutLogo_);
    releaseUiImage(uiSplashArt_);
    releaseUiImage(uiMonitorFrameArt_);
    releaseUiImage(uiOutputChipIdleArt_);
    releaseUiImage(uiOutputChipArmedArt_);
    releaseUiImage(uiOutputChipLiveArt_);
    releaseUiImage(uiOutputChipWarnArt_);
    releaseUiImage(uiOutputChipOffArt_);
    releaseUiImage(uiCueIconVideo_);
    releaseUiImage(uiCueIconImage_);
    releaseUiImage(uiCueIconBrowser_);
    releaseUiImage(uiCueIconPattern_);
    releaseUiImage(uiCueIconPip_);
    releaseUiImage(uiCueIconLowerThird_);
    releaseUiImage(uiCueIconSource_);
    releaseUiImage(uiCueIconAudio_);
    releaseUiImage(uiBtnImport_);
    releaseUiImage(uiBtnPlay_);
    releaseUiImage(uiBtnPause_);
    releaseUiImage(uiBtnTake_);
    releaseUiImage(uiBtnStop_);
    releaseUiImage(uiBtnRerack_);
    releaseUiImage(uiBtnClear_);
    releaseUiImage(uiBtnSettings_);
    releaseUiImage(uiBtnFullscreen_);
    releaseUiImage(uiBtnWindow_);
    releaseUiImage(uiBtnBlackout_);
    releaseUiImage(uiModeLoopOn_);
    releaseUiImage(uiModeOnce_);
    releaseUiImage(uiModeShuffleOn_);
    releaseUiImage(uiModeOrder_);
  }

  UiImageAsset* cueIconAssetForKind(CueKind kind) {
    switch (kind) {
      case CueKind::Video:      return &uiCueIconVideo_;
      case CueKind::Image:      return &uiCueIconImage_;
      case CueKind::Browser:    return &uiCueIconBrowser_;
      case CueKind::Pattern:    return &uiCueIconPattern_;
      case CueKind::Pip:        return &uiCueIconPip_;
      case CueKind::LowerThird: return &uiCueIconLowerThird_;
      case CueKind::Composite:  return &uiCueIconPattern_;
      case CueKind::Audio:      return &uiCueIconAudio_;
      case CueKind::Timer:      return &uiCueIconPattern_;
      case CueKind::WindowSource:
      case CueKind::Camera:
      case CueKind::Syphon:
      case CueKind::NdiSource:  return &uiCueIconSource_;
      case CueKind::SrtStream:  return &uiCueIconVideo_;
      default:                  return nullptr;
    }
  }


  struct MasterCueSidebarButtonHit {
    SDL_Rect rect {};
    int action = 0;
    int presetIndex = -1;
  };

  struct MasterCueSidebarRowHit {
    int presetIndex = -1;
    SDL_Rect rowRect {};
    SDL_Rect fireRect {};
  };

  struct MasterCueSidebarProgramHit {
    int deckIndex = -1;
    SDL_Rect rowRect {};
    SDL_Rect selectedRect {};
    SDL_Rect activeRect {};
    SDL_Rect bypassRect {};
    SDL_Rect prevRect {};
    SDL_Rect nextRect {};
  };

  struct MonitorsTileHit {
    int outputIndex = -1;
    SDL_Rect rect {};
  };

  struct DecksPanelCueHit {
    int deckIndex = -1;
    int cueIndex = -1;
    SDL_Rect rowRect {};
  };

  struct DecksPanelDeckButtonHit {
    int deckIndex = -1;
    int action = 0;
    SDL_Rect rect {};
  };


  struct MasterCueRowHit {
    int presetIndex = -1;
    SDL_Rect rowRect {};
    SDL_Rect fireRect {};
    std::vector<SDL_Rect> slotRects;
  };

  struct OutputMenuButton {
    SDL_Rect rect {};
    int deckIndex = -1;
    int outputIndex = -1;
    int action = 0;
  };

  struct CueRowActionHit {
    SDL_Rect rect {};
    int deckIndex = -1;
    int cueIndex = -1;
    QuickAction action = QuickAction::ToggleLoop;
    bool enabled = true;
    std::string tip;
  };

  static constexpr int kDecksPanelDeckActionTake = 200;
  static constexpr int kDecksPanelDeckActionStop = 201;
  static constexpr int kDecksPanelDeckActionPlay = 202;
  static constexpr int kOutputMenuActionFocus = 1;
  static constexpr int kOutputMenuActionAddOutput = 3;
  static constexpr int kOutputMenuActionRouteFocusDeck = 4;
  static constexpr int kOutputMenuActionRouteAssignToggle = 5;
  static constexpr int kOutputMenuActionRouteLayerDec = 6;
  static constexpr int kOutputMenuActionRouteLayerInc = 7;
  static constexpr int kOutputMenuActionRouteOutputPrev = 8;
  static constexpr int kOutputMenuActionRouteOutputNext = 9;
  static constexpr int kOutputMenuActionToggleFps = 10;
  static constexpr int kOutputMenuActionRecover = 11;
  static constexpr int kOutputMenuActionDisarm = 12;
  static constexpr int kOutputMenuActionRouteLayerCycle = 13;
  static constexpr int kOutputMenuActionSelectDisplay = 14;
  static constexpr int kSettingsActionOutputRemove = 269;
  static constexpr int kSettingsActionOutputToggle = 262;
  static constexpr int kSettingsActionOutputDisplayPrev = 263;
  static constexpr int kSettingsActionOutputDisplayNext = 264;
  static constexpr int kSettingsActionOutputDisplayRescan = 265;
  static constexpr int kSettingsActionRoutingLayerDec = 266;
  static constexpr int kSettingsActionRoutingLayerInc = 267;
  static constexpr int kSettingsActionRoutingAssignToggle = 268;
  static constexpr int kSettingsActionOutputOverlayToggle = 283;
  static constexpr int kSettingsActionOutputAlphaPrompt = 284;
  static constexpr int kSettingsActionOutputDelayPrompt = 285;
  static constexpr int kSettingsActionOutputColorSpaceCycle = 286;
  static constexpr int kSettingsActionOutputDelayInc = 287;
  static constexpr int kSettingsActionOutputLayoutSpan = 288;
  static constexpr int kSettingsActionOutputLayoutDuplicate = 289;
  static constexpr int kSettingsActionOutputOrientationCycle = 290;
  static constexpr int kSettingsActionOutputTestCardToggle = 291;
  static constexpr int kSettingsActionOutputTestCardAllToggle = 292;
  static constexpr int kSettingsActionOutputWarpModeCycle = 293;
  static constexpr int kSettingsActionOutputDisplayDropdown = 294;
  static constexpr int kSettingsActionOutputDisplaySelectBase = 800; // 800 + displayIndex
  static constexpr int kSettingsActionOutputStreamProtocolDropdown = 295;
  static constexpr int kSettingsActionOutputMirrorDropdown = 296;
  static constexpr int kSettingsActionPlaylistPrefsEdit = 500;
  static constexpr int kSettingsActionPlaylistDefaultLoopToggle = 501;
  static constexpr int kSettingsActionPlaylistDefaultFadeInToggle = 502;
  static constexpr int kSettingsActionPlaylistDefaultFadeOutToggle = 503;
  static constexpr int kSettingsActionPlaylistDefaultAudioToggle = 504;
  static constexpr int kSettingsActionPlaylistDefaultPauseBeginToggle = 505;
  static constexpr int kSettingsActionPlaylistDefaultPauseEndToggle = 506;
  static constexpr int kSettingsActionPlaylistDefaultNextTransitionToggle = 507;
  static constexpr int kSettingsActionOscQueryToggle = 508;
  static constexpr int kSettingsActionOscQueryPortPrompt = 509;
  static constexpr int kSettingsActionOscFeedbackMirrorToggle = 510;
  static constexpr int kSettingsActionOscFeedbackRatePrompt = 511;
  static constexpr int kSettingsActionIntegrationAtemToggle = 512;
  static constexpr int kSettingsActionIntegrationNdiTriggerToggle = 513;
  static constexpr int kSettingsActionIntegrationNmcToggle = 514;
  static constexpr int kSettingsActionIntegrationMtcToggle = 515;
  static constexpr int kSettingsActionIntegrationLtcToggle = 516;
  static constexpr int kSettingsActionIntegrationArtNetToggle = 517;
  static constexpr int kSettingsActionIntegrationArtNetPortPrompt = 518;
  static constexpr int kSettingsActionIntegrationAllToggle = 519;
  static constexpr int kSettingsActionIntegrationTimecodeChaseToggle = 520;
  static constexpr int kSettingsActionIntegrationTimecodeRunToggle = 521;
  static constexpr int kSettingsActionOutputEdgeBlendLInc = 600;
  static constexpr int kSettingsActionOutputEdgeBlendLDec = 601;
  static constexpr int kSettingsActionOutputEdgeBlendRInc = 602;
  static constexpr int kSettingsActionOutputEdgeBlendRDec = 603;
  static constexpr int kSettingsActionOutputEdgeBlendTInc = 604;
  static constexpr int kSettingsActionOutputEdgeBlendTDec = 605;
  static constexpr int kSettingsActionOutputEdgeBlendBInc = 606;
  static constexpr int kSettingsActionOutputEdgeBlendBDec = 607;
  static constexpr int kSettingsActionCanvasToggle = 608;
  static constexpr int kSettingsActionCanvasWidthPrompt = 609;
  static constexpr int kSettingsActionCanvasHeightPrompt = 610;
  static constexpr int kSettingsActionTransitionSecondsDec = 611;
  static constexpr int kSettingsActionTransitionSecondsInc = 612;
  static constexpr int kSettingsActionTransitionStyleCycle = 613;
  static constexpr int kSettingsActionThemeDropdown = 614;
  // AOI is edited as a pixel rect (X/Y position + WxH size) of the output
  // raster; the fractions in OutputTarget stay the storage format.
  static constexpr int kSettingsActionOutputAoiXInc  = 615;
  static constexpr int kSettingsActionOutputAoiXDec  = 616;
  static constexpr int kSettingsActionOutputAoiYInc  = 617;
  static constexpr int kSettingsActionOutputAoiYDec  = 618;
  static constexpr int kSettingsActionOutputAoiWInc  = 619;
  static constexpr int kSettingsActionOutputAoiWDec  = 620;
  static constexpr int kSettingsActionOutputAoiHInc  = 621;
  static constexpr int kSettingsActionOutputAoiHDec  = 622;
  static constexpr int kSettingsActionOutputAoiReset = 623;
  static constexpr int kSettingsActionIntegrationTslToggle    = 624;
  static constexpr int kSettingsActionIntegrationTslPortPrompt = 625;
  static constexpr int kSettingsActionIntegrationTslAddrPrompt = 626;
  static constexpr int kSettingsActionAudioBufferCycle = 627;
  static constexpr int kSettingsActionDeckLinkToggle = 628;
  static constexpr int kSettingsActionDeckLinkDeviceDropdown = 629;
  static constexpr int kSettingsActionDeckLinkModeDropdown = 630;
  static constexpr int kSettingsActionDeckLink10BitToggle = 631;
  static constexpr int kSettingsActionSpoutToggle = 632;
  static constexpr int kSettingsActionSpoutNamePrompt = 633;
  static constexpr int kSettingsActionMascotToggle = 634;
  static constexpr int kSettingsActionUiScaleDropdown = 635;
  static constexpr int kSettingsActionPocket3Preset = 636;
  static constexpr int kSettingsActionDisplayIdentify = 637;
  // NOTE: 634–637 were double-allocated at one point (AllowRemote/StreamKey/
  // VideoSubTab collided with Mascot/UiScale/Pocket3/Identify), which silently
  // killed whichever button's handler ran second — the "Processing sub-tab
  // does nothing" bug. Keep every id unique; next free: 647+.
  static constexpr int kSettingsActionAllowRemoteToggle = 640;
  static constexpr int kSettingsActionStreamKeyPrompt = 641;
  static constexpr int kSettingsActionVideoSubTabBase = 642; // 642–645 for 4 sub-tabs
  static constexpr int kSettingsActionEncoderConvertAll = 647;
  static constexpr int kSettingsActionEncoderAddFile = 648;
  // Preset chips. 715-718 (656-701 and 715+ were the free blocks; 720 and 750
  // are taken, so this sits in the 715 gap). Grep before allocating more.
  static constexpr int kSettingsActionEncoderPresetDelivery = 715;
  static constexpr int kSettingsActionEncoderPresetProxy = 716;
  static constexpr int kSettingsActionEncoderPresetMatch = 717;
  static constexpr int kSettingsActionEncoderPresetDatamosh = 718;
  static constexpr int kSettingsActionEncoderPauseToggle = 719;
  static constexpr int kSettingsActionEncoderCancelAll = 730;
  // Per-row cancel for the (max 4) jobs the busy panel shows: 731..734.
  static constexpr int kSettingsActionEncoderCancelRowBase = 731;
  // Per-row queue controls for the four jobs the panel shows.
  static constexpr int kSettingsActionEncoderUpRowBase = 736;    // 736..739
  static constexpr int kSettingsActionEncoderHoldRowBase = 741;  // 741..744
  static constexpr int kSettingsActionEncoderMoshLook = 721;
  // Encoder output overrides. 760-768; 750 was already taken, so this block
  // starts clear of it. Next free after this is 769+.
  static constexpr int kSettingsActionEncoderRateMode   = 760;
  static constexpr int kSettingsActionEncoderQualityDec = 761;
  static constexpr int kSettingsActionEncoderQualityInc = 762;
  static constexpr int kSettingsActionEncoderFpsCycle   = 763;
  static constexpr int kSettingsActionEncoderSizeCycle  = 764;
  static constexpr int kSettingsActionEncoderOutDirPick = 765;
  static constexpr int kSettingsActionEncoderOutDirClear = 766;
  static constexpr int kSettingsActionEncoderAudioRate  = 767;
  // One id per catalog row. 1400 block: clear of 715-734, 750 and the 800/20000
  // ranges. Grep before allocating near it.
  static constexpr int kSettingsActionEncoderFormatBase = 1400;
  static constexpr int kSettingsActionAudioDelayDec = 649;
  static constexpr int kSettingsActionAudioDelayInc = 650;
  static constexpr int kSettingsActionAudioChannelsCycle = 651;
  static constexpr int kSettingsActionOutputAoiXEdit = 652;
  static constexpr int kSettingsActionOutputAoiYEdit = 653;
  static constexpr int kSettingsActionOutputAoiWEdit = 654;
  static constexpr int kSettingsActionOutputAoiHEdit = 655;
  // AOI as a raster, not four edges: pick a standard size, then place it.
  static constexpr int kSettingsActionOutputAoiSizeDropdown = 656;
  static constexpr int kSettingsActionOutputAoiCentre = 657;
  // ST 2110-20 output (Devices sub-tab).
  static constexpr int kSettingsActionSt2110Toggle = 658;
  static constexpr int kSettingsActionSt2110AddressPrompt = 659;
  static constexpr int kSettingsActionSt2110PortPrompt = 660;
  static constexpr int kSettingsActionSt2110DepthToggle = 661;
  static constexpr int kSettingsActionSt2110CopySdp = 662;
  // Streaming destinations. Two of them (0 = SRT, 1 = RTMP), each with its own
  // full control set, so the ids are allocated as base + dest*stride + field
  // rather than as twenty separate constants. Next free block: 670 + 2*16 = 702.
  // Recording gets its OWN block rather than a third stream-destination slot:
  // dest base 670 with stride 16 would put slot 2 at 702-717, straight through
  // the LTC generator (702-706), NMOS (710-714) and the encoder presets
  // (715-717). A double-allocated id silently kills whichever handler runs
  // second, which is exactly how the Processing sub-tab died in v0.76.24.
  // Recording runtime. Mutable because buildOutputStreamArgs is const and is
  // where the timestamped path is minted.
  std::unique_ptr<deckboy::platform::audio::AsioOutput> asioOutput_;
  bool hapSuggestionShown_ = false;   // once per session, whatever the answer
  bool hapStallSeen_ = false;        // a decode actually struggled
  mutable std::string lastRecordingPath_;
  Uint64 recordingStartedMs_ = 0;
  static constexpr int kSettingsActionAudioInputDropdown = 778;
  static constexpr int kSettingsActionAudioInputToProgram = 779;
  static constexpr int kSettingsActionAudioInputGainDec = 780;
  static constexpr int kSettingsActionAudioInputGainInc = 781;
  static constexpr int kSettingsActionAudioInputClipClear = 782;
  static constexpr int kSettingsActionAudioInputMono = 783;
  static constexpr int kSettingsActionAsioDropdown   = 775;
  static constexpr int kSettingsActionAsioChannelsDec = 776;
  static constexpr int kSettingsActionAsioChannelsInc = 777;
  static constexpr int kSettingsActionRecordToggle   = 770;
  static constexpr int kSettingsActionRecordDirPick  = 771;
  static constexpr int kSettingsActionRecordDirClear = 772;
  static constexpr int kSettingsActionStreamDestBase = 670;
  static constexpr int kSettingsActionStreamDestStride = 16;
  static constexpr int kStreamFieldToggle = 0;
  static constexpr int kStreamFieldUrl = 1;
  static constexpr int kStreamFieldKey = 2;
  static constexpr int kStreamFieldBitrate = 3;
  static constexpr int kStreamFieldKeyframe = 4;
  static constexpr int kStreamFieldSrtLatency = 5;
  static constexpr int kStreamFieldSrtPassphrase = 6;
  static constexpr int kStreamFieldSrtStreamId = 7;
  static constexpr int kStreamFieldSrtMode = 8;
  static constexpr int kStreamFieldAudioBitrate = 9;
  // LTC generator (Audio tab).
  static constexpr int kSettingsActionLtcOutToggle = 702;
  static constexpr int kSettingsActionLtcOutDevice = 703;
  static constexpr int kSettingsActionLtcOutFps = 704;
  static constexpr int kSettingsActionLtcOutChannel = 705;
  static constexpr int kSettingsActionLtcOutChannelCount = 706;
  // NMOS IS-04/05 (Video Outputs > Devices sub-tab, under ST 2110).
  // Next free after these: 715+.
  static constexpr int kSettingsActionNmosToggle = 710;
  static constexpr int kSettingsActionNmosRegistryPrompt = 711;
  static constexpr int kSettingsActionNmosPortPrompt = 712;
  static constexpr int kSettingsActionNmosInterfacePrompt = 713;
  static constexpr int kSettingsActionNmosShowUrl = 714;
  static constexpr int kSettingsActionOutputDisplayFocusBase = 32000;
  static constexpr int kSettingsActionOutputAdvancedToggle = 270;
  static constexpr int kSettingsActionRoutingModeToggle = 261;
  static constexpr int kSettingsActionRoutingCellBase = 20000;
  static constexpr int kSettingsActionRoutingCellStride = 256;
  static constexpr int kSettingsActionRoutingDeckFocusBase = 26000;
  static constexpr int kSettingsActionRoutingOutputFocusBase = 28000;
  static constexpr int kSettingsActionRoutingTableOutputPrevBase = 34000;
  static constexpr int kSettingsActionRoutingTableOutputNextBase = 35000;
  static constexpr int kSettingsActionRoutingTableLayerDecBase = 36000;
  static constexpr int kSettingsActionRoutingTableLayerIncBase = 37000;
  static constexpr int kSettingsActionRoutingTableAssignToggleBase = 38000;

  SDL_Window* controlWindow_ = nullptr;
  SDL_Renderer* controlRenderer_ = nullptr;
  SDL_Texture* scanlineOverlay_ = nullptr;
  SDL_Window* monitorsWindow_ = nullptr;
  // Screen-identify overlay: one small always-on-top window per connected
  // display showing its number + name (like the OS "Identify" button), so
  // the operator can tell which settings index is which physical screen.
  struct IdentifyWindow {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    int displayIndex = 0;
  };
  std::vector<IdentifyWindow> identifyWindows_;
  Uint64 identifyUntilMs_ = 0;
  SDL_Renderer* monitorsRenderer_ = nullptr;
  std::vector<SDL_Texture*> monitorsOutputTextures_;
  std::vector<int> monitorsOutputTexW_;
  std::vector<int> monitorsOutputTexH_;
  TTF_Font* fontLarge_ = nullptr;
  TTF_Font* fontBase_ = nullptr;
  TTF_Font* fontSmall_ = nullptr;
  TTF_Font* fontMono_ = nullptr;
  TTF_Font* fontPixel_ = nullptr;
  TTF_Font* fontPixelTitle_ = nullptr;  // big pixel headline (startup prompt)
  TTF_Font* fontPixelSmall_ = nullptr;  // smaller pixel font for UI labels
  SDL_AudioStream* uiAudioStream_ = nullptr;
  // While the boot jingle plays, ordinary bloops are dropped — queueing one
  // would SDL_ClearAudioStream the jingle mid-phrase.
  Uint64 uiJingleUntilMs_ = 0;
  fs::path currentProjectFile_;
  std::string currentThemeName_;
  Project project_;
  std::vector<DeckRuntime> deckRuntimes_;
  std::vector<OutputRuntime> outputRuntimes_;
  // Async loudness-normalize results (worker threads → main tick). Keyed by
  // Cue::id; drained in drainNormalizeResults() each update.
  struct NormalizeResult {
    std::string cueId;
    double gainDb = 0.0;
    double measuredLufs = 0.0;
    double measuredPeakDb = 0.0;
    // True peak once the trim is applied (measuredPeakDb + gainDb).
    double projectedPeakDb = 0.0;
    bool hasPeak = false;
    // True when the normalized clip will peak above the ceiling, i.e. the deck
    // limiter will be doing real work on it. Target loudness is still hit —
    // this is a heads-up, not a shortfall.
    bool peakLimited = false;
    bool ok = false;
  };
  std::mutex normalizeResultsMutex_;
  std::vector<NormalizeResult> normalizeResults_;

#if DECKBOY_INPROC_DECODE
  // Output topology changed: re-check every playing deck's zero-copy decode
  // device against the current program output next tick (app_output_mgmt).
  bool decodeDeviceReconcilePending_ = false;
  // Throttle for control-preview CPU downloads of GPU-resident frames.
  Uint64 controlPreviewGpuLastMs_ = 0;
  DecodedFrame controlPreviewGpuScratch_;
#endif
  std::mutex streamAudioMutex_;
  std::vector<DeckStreamAudioBuffer> deckStreamAudioBuffers_;
#if defined(DECKBOY_HAS_NDI_SDK)
  NdiApi ndiApi_;
#endif
  LtcApi ltcApi_;
  std::vector<Button> buttons_;
  std::vector<SDL_Rect> deckColumnRects_;
  std::vector<SDL_Rect> deckListClipRects_;
  std::vector<SDL_Rect> deckOverlayClipRects_;
  SDL_Rect progressBarRect_ {};
  SDL_Rect audioProgressBarRect_ {};  // audio lane, also click-to-seek like the video lane

  // Context menu
  bool contextMenuOpen_ = false;
  int contextMenuDeckIdx_ = 0;
  int contextMenuCueIdx_ = 0;
  SDL_Rect contextMenuRect_ {};
  struct ContextItem {
    std::string label;
    SDL_Color swatch {0,0,0,0};
    std::function<void()> action;
    SDL_Rect rect {};
  };
  std::vector<ContextItem> contextItems_;

  struct DropdownOptionItem {
    std::string id;
    std::string label;
    std::string searchLabel;
    int textWidth = 0;
  };

  struct DropdownState {
    bool open = false;
    std::string owner;
    SDL_Rect anchorRect {};
    SDL_Rect popoverRect {};
    std::vector<DropdownOptionItem> options;
    std::vector<int> filteredIndices;
    int highlightedFilteredIndex = 0;
    int scrollRow = 0;
    int rowHeight = 24;
    int maxVisibleRows = 10;
    std::string filter;
    std::function<void(const std::string&)> onSelect;
  };

  DropdownState dropdown_;
  int dropdownLastRenderedItemCount_ = -1;
  struct InlineTextEditorState {
    bool open = false;
    std::string owner;
    std::string title;
    std::string prompt;
    std::string value;
    SDL_Rect anchorRect {};
    SDL_Rect panelRect {};
    SDL_Rect inputRect {};
    SDL_Rect applyRect {};
    SDL_Rect cancelRect {};
    // On open the existing value is treated as "selected": the first typed
    // character (or backspace) replaces it, so the operator can just type the
    // new value and hit enter without clearing the old one first.
    bool freshEntry = false;
    std::function<void(const std::string&)> onSubmit;
  };
  InlineTextEditorState inlineEditor_;
  SDL_Rect lastInlineEditorAnchorRect_ {};
  SDL_Rect bottomBarRect_ {};
  SDL_Rect mediaGroupRect_ {};
  SDL_Rect transportGroupRect_ {};
  SDL_Rect outputGroupRect_ {};
  SDL_Rect sourceDefaultDropdownRect_ {};
  SDL_Rect patternDefaultDropdownRect_ {};
  SDL_Rect cueSourceTypeDropdownRect_ {};
  SDL_Rect cueWindowSourceDropdownRect_ {};
  SDL_Rect cuePatternTypeDropdownRect_ {};
  SDL_Rect cueTransitionStyleDropdownRect_ {};
  int inspectorSectionBottomMax_ = 0;  // deepest open-section bottom this frame (scrolled space)
  std::string sourceDefaultTypeId_ = "window";
  std::string patternDefaultTypeId_ = "pocket-test";

  // Deck settings modal
  bool deckSettingsOpen_ = false;
  int deckSettingsDeckIdx_ = 0;
  SDL_Rect deckSettingsCloseBtn_ {};
  std::vector<SDL_Rect> deckGearBtns_;
  SDL_Rect dsTransDurMinus_ {}, dsTransDurPlus_ {};
  std::vector<SDL_Rect> dsTransStyleBtns_;
  SDL_Rect dsTcFpsCycle_ {}, dsTcChaseBtn_ {}, dsTcRunBtn_ {}, dsTcTrigBtn_ {};
  SDL_Rect dsTcSetBtn_ {};
  SDL_Rect dsNdiToggle_ {}, dsNdiRename_ {};

  // Keyboard shortcuts overlay
  bool shortcutsOverlayOpen_ = false;

  // Undo/redo state snapshots
  std::vector<Project> undoStack_;
  std::vector<Project> redoStack_;
  static constexpr int kMaxUndoLevels = 40;

  // Warp presets (named corner configurations)
  struct WarpPreset {
    std::string name;
    float tlx, tly, trx, try_, brx, bry, blx, bly;
    std::string mode;
  };
  std::vector<WarpPreset> warpPresets_;
  // Put the control window up, once, after the first frame has been presented.
  // Called from every path that presents it, because which one draws first
  // depends on whether a show is loading, and a window that never appears is a
  // worse bug than one that flashes.
  void revealControlWindow() {
    if (controlWindowRevealed_ || !controlWindow_) {
      return;
    }
    controlWindowRevealed_ = true;
    SDL_ShowWindow(controlWindow_);
    SDL_RaiseWindow(controlWindow_);
  }
  bool controlWindowRevealed_ = false;

  std::optional<Cue> cueSettingsClipboard_;
  // The effect chain on its own, separate from the whole-cue clipboard above:
  // copying a cue also brings its geometry, fades and crop, which is not what
  // is wanted when only the LOOK is worth keeping.
  std::vector<deckboy::effects::CueEffect> effectChainClipboard_;
  std::string effectChainClipboardDriver_;
  std::optional<Deck> warpSettingsClipboard_;

  // Master fader
  SDL_Rect masterFaderRect_ {};

  // HyperDeck server
  int hyperDeckPort_ = 9992;
  std::thread hyperDeckThread_;
  std::atomic<bool> hyperDeckRunning_ {false};
  SocketHandle hyperDeckListenFd_ = kInvalidSocket;

  fs::path uiPackRoot_;
  bool uiPackAvailable_ = false;
  UiImageAsset uiHeaderArt_;
  UiImageAsset uiAboutLogo_;
  UiImageAsset uiSplashArt_;
  // How much theme accent the current splash takes: 1.0 for the grayscale
  // masters, ~0.35 for finished colour art, 0 for the branded wordmark.
  bool firstRunEver_ = false;   // very first launch on this machine
  float splashTintStrength_ = 0.0f;
  fs::path lastSplashChoice_;    // so two boots running do not pick the same art
  UiImageAsset uiMonitorFrameArt_;
  UiImageAsset uiOutputChipIdleArt_;
  UiImageAsset uiOutputChipArmedArt_;
  UiImageAsset uiOutputChipLiveArt_;
  UiImageAsset uiOutputChipWarnArt_;
  UiImageAsset uiOutputChipOffArt_;
  UiImageAsset uiCueIconVideo_;
  UiImageAsset uiCueIconImage_;
  UiImageAsset uiCueIconBrowser_;
  UiImageAsset uiCueIconPattern_;
  UiImageAsset uiCueIconPip_;
  UiImageAsset uiCueIconLowerThird_;
  UiImageAsset uiCueIconSource_;
  UiImageAsset uiCueIconAudio_;
  // Control / toolbar button icons
  UiImageAsset uiBtnImport_;
  UiImageAsset uiBtnPlay_;
  UiImageAsset uiBtnPause_;
  UiImageAsset uiBtnTake_;
  UiImageAsset uiBtnStop_;
  UiImageAsset uiBtnRerack_;
  UiImageAsset uiBtnClear_;
  UiImageAsset uiBtnSettings_;
  UiImageAsset uiBtnFullscreen_;
  UiImageAsset uiBtnWindow_;
  UiImageAsset uiBtnBlackout_;
  UiImageAsset uiModeLoopOn_;
  UiImageAsset uiModeOnce_;
  UiImageAsset uiModeShuffleOn_;
  UiImageAsset uiModeOrder_;

  std::unique_ptr<MediaEngine> previewMediaEngine_;
  // Live preview textures track SDL pixel format so an RGBA→NV12 cue switch
  // (or vice versa) rebuilds the texture instead of reusing a stale sampler.
  SDL_Texture* previewCueTex_ = nullptr;
  int previewCueTexW_ = 0;
  int previewCueTexH_ = 0;
  Uint32 previewCueTexFormat_ = 0;
  std::uint64_t previewCueFrameIdx_ = static_cast<std::uint64_t>(-1);
  std::string previewCueKey_;
  Cue previewResolvedCue_;
  bool previewResolvedCueValid_ = false;
  SDL_Texture* controlPreviewTex_ = nullptr;
  int controlPreviewTexW_ = 0;
  int controlPreviewTexH_ = 0;
  Uint32 controlPreviewTexFormat_ = 0;
  std::uint64_t controlPreviewFrameIdx_ = static_cast<std::uint64_t>(-1);
  // Program-monitor tap bookkeeping: which output composite the control
  // preview currently holds (vs. a raw decoder frame), and the last tap serial
  // uploaded. Composite frames already carry the cue's geometry, so the
  // monitor must draw them plainly instead of re-applying it.
  bool controlPreviewIsComposite_ = false;
  // Scratch for the preview's graded/effected copy, so the decoded frame
  // itself is never mutated -- other consumers still need it clean.
  DecodedFrame controlPreviewLookFrame_;
  std::uint64_t controlPreviewTapSerial_ = 0;
  std::vector<QuickButton> quickButtons_;
  // Value scrubbing: click-hold-drag horizontally on an inspector value cell
  // steps the row's dec/inc actions (like number scrubbing in AE/Resolve);
  // a plain click (released before the drag threshold) opens the exact-entry
  // popup as before. Zones are rebuilt each frame next to quickButtons_.
  struct ValueScrubZone {
    SDL_Rect rect {};
    QuickAction decAction = QuickAction::ToggleLoop;
    QuickAction incAction = QuickAction::ToggleLoop;
    QuickAction clickAction = QuickAction::ToggleLoop;
    bool hasClickAction = false;
    int param = -1;              // see QuickButton::param
  };
  std::vector<ValueScrubZone> valueScrubZones_;
  size_t cueSettingsScrubZoneStartIndex_ = 0;  // zones past this obey the settings viewport clip
  bool valueScrubPending_ = false;   // button down on a zone, threshold not yet crossed
  bool valueScrubEngaged_ = false;   // actively scrubbing
  ValueScrubZone activeValueScrub_ {};
  int valueScrubStartX_ = 0;
  int valueScrubLastStepX_ = 0;
  std::vector<MonitorsTileHit> monitorsTileHits_;
  std::vector<DecksPanelCueHit> decksPanelCueHits_;
  std::vector<DecksPanelDeckButtonHit> decksPanelDeckButtonHits_;
  std::vector<CueRowActionHit> cueRowActionHits_;
  std::vector<OutputMenuButton> outputMenuButtons_;
  bool outputFpsCounterEnabled_ = true;
  size_t cueSettingsQuickButtonStartIndex_ = 0;
  SDL_Rect cueSettingsViewportRect_ {};
  int cueSettingsScroll_ = 0;
  int pendingInspectorScroll_ = -1;   // --inspector-scroll, applied once measurable
  int cueSettingsScrollMax_ = 0;
  SDL_Rect settingsVideoViewport_ {};
  int settingsVideoScroll_ = 0;
  // System tab scroll: only engages when the cards are taller than the modal,
  // which happens at large UI scales.
  int settingsSystemScroll_ = 0;
  int settingsSystemScrollMax_ = 0;
  SDL_Rect settingsSystemViewport_ {0, 0, 0, 0};
  int settingsVideoScrollMax_ = 0;
  bool cueSectionPlaybackOpen_ = true;
  bool cueSectionMetadataOpen_ = true;
  bool cueSectionGeometryOpen_ = true;
  bool cueSectionKeyOpen_ = false;
  bool cueSectionEffectsOpen_ = true;   // datamosh lives here
  bool cueSectionTimerOpen_ = true;     // stage timer controls
  bool cueSectionToneOpen_ = true;      // test tone generator controls
  bool cueSectionSynthOpen_ = true;    // chip voice controls
  bool cueSectionVideoSynthOpen_ = true;  // video synth controls

  // Motion drivers, one per deck. The driver clip is decoded ONLY for the
  // vectors its codec already computed -- 900fps for 720p, measured -- so this
  // costs a fraction of a millisecond a frame and its pictures are discarded.
  struct MotionDriver {
    std::string path;
    void* handle = nullptr;
    deckboy::motion::MotionField field;
    bool exhausted = false;
    bool haveField = false;
    // Fractional advance. Speed is fields per RENDERED frame, so 0.5 holds
    // each field for two frames and 2.0 skips one. Accumulating rather than
    // rounding per frame keeps a slow driver smooth instead of stuttering
    // between "advance" and "do not".
    double advanceCredit = 0.0;
    // The driver has more than one consumer now -- the output composite and
    // the control preview both ask for it, and an NDI-only show has the first
    // without the second. Advancing per ASKER would run the driver at a
    // multiple of its speed depending on what happened to be armed, so the
    // first ask of each frame advances and the rest are served the same field.
    std::uint64_t lastAdvancedFrame = 0;
  };
  std::uint64_t motionDriverFrameCounter_ = 0;
  std::unordered_map<int, MotionDriver> motionDrivers_;

  // The inspector's driver preview and scrub bar. The driver has no cue, no
  // engine and no transport of its own, so this is the only place any of its
  // state is visible.
  const MotionDriver* motionDriverForDeck(int deckIndex) const {
    auto it = motionDrivers_.find(deckIndex);
    return it == motionDrivers_.end() ? nullptr : &it->second;
  }

  void seekMotionDriver(int deckIndex, double seconds) {
    auto it = motionDrivers_.find(deckIndex);
    if (it == motionDrivers_.end() || !it->second.handle) {
      return;
    }
    deckboy::motion::seekMotionSource(it->second.handle, std::max(0.0, seconds));
    it->second.advanceCredit = 0.0;
    it->second.exhausted = false;
  }

  SDL_Texture* motionDriverThumbTex_ = nullptr;
  int motionDriverThumbW_ = 0;
  int motionDriverThumbH_ = 0;
  std::uint64_t motionDriverThumbFrame_ = UINT64_MAX;
  // Set while the bar is on screen and cleared when it is not, so a press
  // cannot scrub a driver that is no longer being shown.
  SDL_Rect motionDriverScrubRect_ {0, 0, 0, 0};
  double motionDriverScrubDuration_ = 0.0;
  int motionDriverScrubDeck_ = -1;
  bool motionDriverScrubActive_ = false;

  // Opens or re-points the deck's driver, advances it one frame, and returns
  // the field to displace by -- or null when nothing is armed. Loops, because
  // the driver has no transport of its own and a puppet that stops moving when
  // its driver ends is a puppet that looks broken.
  const deckboy::motion::MotionField* advanceMotionDriver(int deckIndex,
                                                          const Cue& cue) {
    const std::string& want = cue.motionDriverPath;
    MotionDriver& driver = motionDrivers_[deckIndex];
    if (driver.path != want) {
      if (driver.handle) {
        deckboy::motion::closeMotionSource(driver.handle);
        driver.handle = nullptr;
      }
      driver.path = want;
      driver.field = {};
      driver.exhausted = false;
      driver.haveField = false;
      driver.advanceCredit = 0.0;
      if (!want.empty()) {
        driver.handle = deckboy::motion::openMotionSource(want);
        if (!driver.handle) {
          // Said once, not every frame: a driver that will not open is an
          // operator problem, and a toast per frame is unusable.
          triggerToast("motion driver would not open");
          driver.exhausted = true;
        }
      }
    }
    if (!driver.handle || driver.exhausted) {
      return nullptr;
    }
    if (driver.lastAdvancedFrame == motionDriverFrameCounter_) {
      return driver.haveField ? &driver.field : nullptr;   // already this frame
    }
    driver.lastAdvancedFrame = motionDriverFrameCounter_;
    // PAUSED holds the current field rather than stopping the effect: the
    // picture stays displaced by whatever the driver was doing, which is a
    // usable look in itself. Stopping would just snap back to undisplaced.
    if (cue.motionDriverPaused) {
      return driver.haveField ? &driver.field : nullptr;
    }
    driver.advanceCredit +=
      std::clamp(static_cast<double>(cue.motionDriverSpeed), 0.0, 4.0);
    int steps = static_cast<int>(driver.advanceCredit);
    if (steps <= 0) {
      // Slower than one field per frame: hold what we have until enough
      // credit accrues.
      return driver.haveField ? &driver.field : nullptr;
    }
    driver.advanceCredit -= steps;
    steps = std::min(steps, 4);   // a speed spike must not decode unbounded
    for (int i = 0; i < steps; ++i) {
      if (!deckboy::motion::readMotionField(driver.handle, driver.field)) {
        deckboy::motion::rewindMotionSource(driver.handle);
        if (!deckboy::motion::readMotionField(driver.handle, driver.field)) {
          driver.exhausted = true;   // yields nothing twice: it is done
          return nullptr;
        }
      }
      driver.haveField = true;
    }
    return &driver.field;
  }

  // Back to the driver's first frame. Called on TAKE when the cue asks for it,
  // so a rehearsed look repeats exactly rather than depending on how long the
  // app has been open.
  void restartMotionDriver(int deckIndex) {
    auto it = motionDrivers_.find(deckIndex);
    if (it == motionDrivers_.end() || !it->second.handle) {
      return;
    }
    deckboy::motion::rewindMotionSource(it->second.handle);
    it->second.advanceCredit = 0.0;
    it->second.exhausted = false;
  }

  void closeMotionDrivers() {
    for (auto& [deckIndex, driver] : motionDrivers_) {
      (void) deckIndex;
      if (driver.handle) {
        deckboy::motion::closeMotionSource(driver.handle);
        driver.handle = nullptr;
      }
    }
    motionDrivers_.clear();
  }

  // Smoothed output level, 0..1, for anything that reacts to audio.
  // Smoothed because a raw peak makes a visualiser twitch rather than move.
  double reactiveAudioLevel_ = 0.0;
  SDL_AudioStream* audioInputStream_ = nullptr;
  std::string audioInputActiveDevice_;   // what actually opened, not what was asked for
  double audioInputPeak_ = 0.0;          // 0..1, decays; drives the meter
  std::vector<std::int16_t> audioInputScratch_;
  // Captured audio waiting to be muxed. Guarded because the capture pump runs
  // on the main thread while the encoder collection runs from the output path.
  std::mutex audioInputMixMutex_;
  std::vector<std::int16_t> audioInputMixBuffer_;
  bool cueSectionRoutingOpen_ = true;
  bool cueSectionAudioOpen_ = true;
  struct TimelineStripCacheEntry {
    DecodedFrame frame;
    int readyTiles = 0;
  };
  static constexpr size_t kThumbnailCacheLimit = 24;
  static constexpr size_t kTimelineStripCacheLimit = 24;
  // 9 stills per clip (was 5) so the filmstrip reads as a continuous strip
  // and samples the clip densely enough now that the timeline lane can be
  // enlarged via the program/timeline splitter — fewer, wider-stretched tiles
  // looked sparse and seamy when expanded.
  static constexpr int kTimelineStripThumbCount = 9;
  // 2x thumbnail resolution so the filmstrip stays sharp when the timeline
  // lane is enlarged. Rendered with linear filtering (see app_render_main.ipp)
  // rather than the UI's default nearest scale, since photographic thumbnails
  // upscale badly point-sampled.
  static constexpr int kTimelineStripThumbW = 256;
  static constexpr int kTimelineStripThumbH = 144;
  static constexpr int kTimelineStripPadding = 2;
  // Per-selection thumbnail (decoded from the selected cue via ffmpeg)
  ChildProcess thumbnailProcess_;
  std::thread thumbnailThread_;
  std::mutex thumbnailMutex_;
  std::optional<DecodedFrame> pendingThumbnail_;
  std::atomic<bool> thumbnailPending_ {false};
  std::atomic<bool> thumbnailLoading_ {false};
  SDL_Texture* selectedThumbnailTex_ = nullptr;
  int selectedThumbnailTexW_ = 0;
  int selectedThumbnailTexH_ = 0;
  std::string selectedThumbnailCueKey_;
  std::map<std::string, DecodedFrame> selectedThumbnailCache_;
  std::deque<std::string> selectedThumbnailCacheOrder_;
  ChildProcess timelineStripProcess_;
  std::thread timelineStripThread_;
  std::mutex timelineStripMutex_;
  std::optional<DecodedFrame> pendingTimelineStrip_;
  int pendingTimelineStripReadyTiles_ = 0;
  std::atomic<bool> timelineStripPending_ {false};
  std::atomic<bool> timelineStripLoading_ {false};
  SDL_Texture* timelineStripTex_ = nullptr;
  int timelineStripTexW_ = 0;
  int timelineStripTexH_ = 0;
  int timelineStripTexReadyTiles_ = 0;
  std::string timelineStripCueKey_;
  std::string timelineStripCueId_;
  std::string timelineStripFailedCueKey_;
  Uint64 timelineStripFailedAtMs_ = 0;
  std::map<std::string, TimelineStripCacheEntry> timelineStripCache_;
  std::deque<std::string> timelineStripCacheOrder_;
  std::atomic<std::uint64_t> timelineStripJobSerial_ {0};
  // Timer cues run their OWN clock, deliberately not the transport (the owner,
  // 2026-08-20). The cue stays live on the stage screen while the operator
  // starts, pauses, resets or jumps the countdown -- tying it to transport
  // meant pausing the clock also took the display off air.
  //
  // Keyed by cue id rather than index so it survives reordering, insertion and
  // deletion in the cue list.
  std::map<std::string, TimerRuntime> timerRuntimes_;
  Uint64 lastTimerPruneSec_ = 0;
  // Show log: append-only record of what fired and when. See showLog().
  std::ofstream showLogFile_;
  bool showLogEnabled_ = true;
  double lastScheduleCheckSeconds_ = -1.0;  // see processScheduledStarts()

  std::unordered_map<std::string, PipOverlayRuntime> pipOverlayRuntimes_;
  std::vector<int> deckScrolls_;
  std::vector<int> deckScrollMax_;                  // per-deck clamp bound, set at render
  std::vector<int> deckFollowedActive_;             // auto-follow: last live index we revealed
  std::vector<Uint64> deckScrollSettleMs_;          // per-deck last-frame time for the spring dt
  Uint64 lastDeckScrollMs_ = 0;                     // last wheel input, for rubber-band settle
  static constexpr int kDeckScrollOverscroll = 44;  // px of springy over-scroll past the bottom
  std::vector<int> deckOverlayScrolls_;
  int mouseX_ = 0;
  int mouseY_ = 0;
  bool confirmQuit_ = false;
  SDL_Rect quitYesBtn_ {};
  SDL_Rect quitNoBtn_ {};

  // Modal prompt shown when the operator tries to enable an output backend or
  // create a cue kind whose runtime dependency is not installed on this
  // machine (NDI Runtime, Blackmagic Desktop Video, WebView2 Runtime). The
  // CTA button opens the official vendor download page in the default
  // browser; Close just dismisses. The prompt is lazy — it only appears
  // when the operator actually attempts the gated action.
  struct DependencyPromptState {
    bool active = false;
    std::string title;       // e.g. "NDI Runtime required"
    std::string body;        // e.g. one or two sentences of context
    std::string url;         // vendor download page (https://...)
    std::string ctaLabel;    // e.g. "Open NDI Tools download page"
    // When set, the CTA RUNS this instead of opening url. Lets the same modal
    // carry an offer to act (convert these cues) as well as a download link,
    // rather than growing a second prompt system beside this one.
    std::function<void()> onCta;
    SDL_Rect ctaRect {};
    SDL_Rect closeRect {};
  };
  DependencyPromptState depPrompt_;
  int pendingLiveDeleteConfirmDeckIndex_ = -1;
  std::string pendingLiveDeleteConfirmSignature_;
  std::string pendingLiveDeleteConfirmMessage_;
  Uint64 pendingLiveDeleteConfirmUntilMs_ = 0;
  bool showStartupDialog_ = false;
  bool showSplashOverlay_ = true;
  Uint64 splashStartedAt_ = 0;
  SDL_Rect startupLoadBtn_ {};
  SDL_Rect startupNewBtn_ {};
  SDL_Rect startupOpenSavedBtn_ {};
  // SDL3 native file-dialog results, marshalled from the dialog callback onto
  // the main thread. See the async dialog helpers in app_cue_mgmt.ipp — this
  // replaces the osascript/powershell/zenity subprocess pickers, which had to
  // run on a background thread and could not fork a GUI subprocess from a
  // non-main thread on macOS (why IMPORT silently did nothing there).
  std::mutex sdlDialogMutex_;
  std::vector<std::function<void()>> sdlDialogActions_;
  // ATOMIC on purpose: SDL's docs say the dialog callback "may be invoked from
  // the same thread or from a different thread", so this flag is set true on the
  // main thread (showXDialog) and cleared from the callback, which may be
  // another thread. A plain bool would be a data race.
  std::atomic<bool> sdlDialogOpen_ {false};   // one native dialog at a time
  // Async media-presence scan (boot / project open). The worker stats every
  // file cue off-thread; results land on the update tick by cue id. The
  // generation counter supersedes stale workers (they bail at their next
  // check, so joining before a restart is bounded by ~one fs::exists).
  std::thread mediaScanThread_;
  std::atomic<std::uint64_t> mediaScanGeneration_ {0};
  std::mutex mediaScanMutex_;
  std::vector<std::pair<std::string, bool>> mediaScanResults_;  // cue id → missing
  std::uint64_t mediaScanResultsGeneration_ = 0;
  std::atomic<bool> mediaScanReady_ {false};
  bool mediaScanAnnounce_ = false;

  // Count from the last scanProjectMediaPresence() — drives the toolbar
  // RELINK button (only visible when > 0).
  int missingMediaCount_ = 0;

  // -- Soak mode (--soak [minutes]) -------------------------------------------
  // Long-run stability harness: loops the loaded show (or synthesized
  // patterns) through the REAL app loop, logging RSS/stall counters once a
  // minute to stdout + deckboy-soak.log, then quits after the duration.
  // Never persists project changes (flushDirtyProject is gated) so the
  // AutoNext rewiring below can't leak into the operator's show file.
  // Shuffle RNG — seeded once from a real entropy source so the "random" next
  // cue differs run to run. std::rand() (the old call) is seeded to 1 by
  // default and Deckboy never called srand, so every launch shuffled to the
  // exact same order.
  std::mt19937 shuffleRng_{std::random_device{}()};

  bool soakMode_ = false;
  double soakMinutes_ = 0.0;
  Uint64 soakStartMs_ = 0;
  Uint64 soakLastLogMs_ = 0;
  int decodeStallTotal_ = 0;   // watchdog trips this run (counted always, logged by soak)
  std::ofstream soakLogFile_;
  DragState drag_;
  enum class TrimDragMode { None, In, Out };
  enum class LayoutDragMode { None, Playlist, Inspector, Timeline };
  LayoutDragMode layoutDragMode_ = LayoutDragMode::None;
  TrimDragMode trimDragMode_ = TrimDragMode::None;
  bool timelineScrubActive_ = false;
  bool scrubWasPlaying_ = false;
  SDL_Rect trimInHandleRect_ {};
  SDL_Rect trimOutHandleRect_ {};
  SDL_Rect contentAreaRect_ {};
  SDL_Rect mainPanelLayoutRect_ {};
  SDL_Rect playlistSplitterRect_ {};
  SDL_Rect inspectorSplitterRect_ {};
  int playlistPaneWidth_ = 0;
  int inspectorPaneWidth_ = 0;
  // Program-monitor <-> timeline vertical split. timelineExtraH_ is the extra
  // height (px) the operator has stolen from the preview to enlarge the
  // timeline lanes, dragged via timelineSplitterRect_. programAreaRect_ and
  // programFullMonitorH_ are captured each render so the drag handler can
  // clamp. Runtime-only, like the pane widths above.
  SDL_Rect timelineSplitterRect_ {};
  SDL_Rect programAreaRect_ {};
  int programFullMonitorH_ = 0;
  int timelineExtraH_ = 0;
  static constexpr int kProgramMonitorMinH = 200;
  // Startup mascot: an animated kawaii Deckboy face with rotating tips fills
  // the empty program monitor until the first clip is loaded into it this
  // session. Once a clip loads, it retires for the rest of the run.
  bool firstClipLoadedThisSession_ = false;
  // Warp editor state
  bool warpEditMode_ = false;
  int warpDragCorner_ = -1;  // -1=none, 0=TL, 1=TR, 2=BR, 3=BL
  SDL_Rect warpEditBtnRect_ {};
  SDL_Rect warpModeBtnRect_ {};
  SDL_Rect warpResetBtnRect_ {};
  SDL_Rect warpSaveBtnRect_ {};
  SDL_Rect warpCopyBtnRect_ {};
  SDL_Rect warpPasteBtnRect_ {};
  SDL_Rect warpRecallBtnRect_ {};
  SDL_Rect shuffleBtnRect_ {};
  SDL_Rect warpMonitorInner_ {};  // the inner video area used for mapping warp coords
  SDL_Rect previewMonitorInner_ {};
  bool keyColorPickerArmed_ = false;
  ToastState toast_;
  Uint64 animationNow_ = 0;
  // UI vsync is dropped while anything is being recorded or streamed, so the
  // programme output alone paces the loop (see App::render).
  int controlVsyncApplied_ = 1;
  Uint64 lastControlDrawMs_ = 0;
  Uint64 selectionChangedAt_ = 0;
  bool uiProfileEnabled_ = false;
  double lastUiLayoutMs_ = 0.0;
  double lastUiRenderMs_ = 0.0;
  Uint64 lastUpdateTickMs_ = 0;
  Uint64 lastDisplayPollMs_ = 0;
  Uint64 lastOutputRecoveryPollMs_ = 0;
  Uint64 lastEscapeKeyMs_ = 0;
  int escapePressStreak_ = 0;
  // One PTP client for the machine — there is one clock, and binding ports
  // 319/320 twice would fail. Started lazily the first time an ST 2110 output
  // actually needs it, so a show that never streams 2110 puts no PTP traffic
  // on the network and never touches those ports.
  deckboy::platform::video::PtpClient ptpClient_;
  bool ptpStartAttempted_ = false;

  // One NMOS node for the machine, advertising every armed ST 2110 sender.
  // Like PTP this binds a port, so there can only be one. Its IS-05 patch
  // handler runs on the node's HTTP thread and hands work to the main thread
  // through nmosPendingPatches_ — see applyPendingNmosPatches().
  //
  // The handler BLOCKS until the main thread has really applied the change (or
  // a timeout expires and it reports failure). Returning success the instant a
  // patch is queued would tell a broadcast controller the route moved before it
  // had, which is the exact class of lie that makes a plant untrustworthy.
  struct PendingNmosPatch {
    deckboy::platform::video::NmosSenderPatch patch;
    bool done = false;
    bool applied = false;
  };
  deckboy::platform::video::NmosNode nmosNode_;
  bool nmosStarted_ = false;
  // True when NMOS is on and a registry is configured but the network is set to
  // LOCAL ONLY, so we are deliberately withholding registration rather than
  // publishing an href nothing can reach.
  bool nmosLocalOnlyBlocked_ = false;
  bool nmosLastEnabled_ = false;
  int nmosLastPort_ = 0;
  std::string nmosLastRegistry_;
  std::mutex nmosPatchMutex_;
  std::condition_variable nmosPatchCv_;
  std::vector<std::shared_ptr<PendingNmosPatch>> nmosPendingPatches_;

  // LTC generator (timecode OUT). Main-thread only: pumped from the update tick
  // and torn down there too, so no locking is needed.
  SDL_AudioStream* ltcOutStream_ = nullptr;
  void* ltcEncoder_ = nullptr;
  std::string ltcOutDeviceName_;
  double ltcOutFps_ = 30.0;
  int ltcOutChannels_ = 2;
  int ltcOutChannel_ = 0;
  double ltcOutBaseSeconds_ = 0.0;
  long long ltcOutEmittedFrames_ = 0;
  bool ltcOutPrimed_ = false;
  std::vector<std::uint8_t> ltcOutFrameBuf_;
  std::vector<std::int16_t> ltcOutPcm_;

  // ST 2110-30 senders, reachable from the AUDIO THREAD.
  // The tap that feeds them runs on the audio thread, and walking
  // project_.outputs/outputRuntimes_ from there would race the main thread
  // creating and destroying outputs. Main thread publishes raw pointers here
  // under the mutex; the audio thread only ever reads this list.
  std::mutex st2110AudioMutex_;
  std::vector<deckboy::platform::video::St2110AudioOutput*> st2110AudioSenders_;

  // Loading overlay (see app_overlays.ipp). Only used while the main render
  // loop is stopped, so it owns its own present.
  bool loadingActive_ = false;
  std::string loadingTitle_;
  std::string loadingDetail_;
  double loadingFrac_ = 0.0;
  Uint64 loadingStartMs_ = 0;
  Uint64 loadingLastPresentMs_ = 0;
  unsigned loadingQuipSeed_ = 0;
  int observedDisplayCount_ = -1;
  // Per-display "name@x,y,w,h" fingerprints from the last topology scan.
  // Compared entry-by-entry so a hot-plug re-homes only the outputs whose
  // display actually moved, changed size, or was swapped for another panel —
  // count alone misses same-count swaps and resolution changes.
  std::vector<std::string> displaySignatureEntries_;
  Uint64 displayTopologyRecheckAtMs_ = 0;  // debounce deadline for the display-event burst
  bool projectDirty_ = false;
  std::chrono::steady_clock::time_point projectDirtyAt_;
  bool engineCueSyncPending_ = false;  // refresh engine cue snapshots next tick (set by markProjectDirty)
  bool masterFaderDragActive_ = false; // header master fader is being dragged
  int konamiIndex_ = 0;                // progress through the Konami sequence (easter egg)
  std::vector<std::string> startupBootLog_;  // splash boot-console lines (built once per boot)
  std::vector<Uint64> startupBootLogAtMs_;   // per-line randomized reveal times (ms from splash start)
  int companionPort_ = 5510;
  bool companionReady_ = false;
  std::atomic<bool> companionStop_ {false};
  std::thread companionThread_;
  std::mutex remoteCommandMutex_;
  // A queued command, plus the Companion client (if any) waiting to be told
  // what happened to it. Commands arriving from OSC/MIDI/ATEM/NDI have no
  // reply socket and carry kInvalidSocket.
  struct PendingRemoteCommand {
    std::string text;
    SocketHandle replyTo = kInvalidSocket;
  };
  std::deque<PendingRemoteCommand> remoteCommands_;
  // Set true at the top of handleRemoteCommand and cleared only by falling off
  // its end, which is what an unrecognized verb does. Read straight after the
  // call in processRemoteCommands to answer OK or ERR.
  bool remoteCommandRecognized_ = true;
  // Set by failRemoteCommand() when a verb was understood but its arguments
  // were not — an ERR with a reason, not a bare OK.
  std::string remoteCommandError_;
  // Set by a verb that ran and has something to report back. Appended to the
  // OK reply, so a query answers with its answer instead of only a toast.
  std::string remoteCommandDetail_;
  std::mutex statusSnapshotMutex_;
  std::string statusSnapshot_;
  std::string statusSnapshotJson_;
  std::string statusCueSnapshot_;
  std::vector<std::string> statusDeckSnapshots_;
  // Structured snapshot for the HyperDeck server. Its handlers run on the
  // HyperDeck TCP thread, which must never read project_/decks directly
  // (main thread mutates them) and must not infer state by substring-
  // matching human-readable snapshot text. Guarded by statusSnapshotMutex_.
  struct HyperDeckSnapshot {
    std::string transport = "stopped";  // "play" | "paused" | "stopped"
    int clipId = 0;                      // 1-based active cue (0 = none)
    bool loop = false;
    std::vector<std::pair<std::string, std::string>> clips;  // {name, duration label}
  };
  HyperDeckSnapshot hyperDeckSnapshot_;
  std::map<int, std::unordered_set<std::string>> timecodeTriggeredCueIds_;
  std::vector<Uint64> deckTimecodeLastExternalMs_;
  std::vector<double> deckTimecodeLastExternalSeconds_;
  std::vector<bool> deckTimecodeHasExternal_;
  double lastMtcIngestSeconds_ = -1.0;
  double lastMtcIngestFps_ = 0.0;
  std::string lastCueFindToken_;
  std::vector<int> lastCueFindMatches_;
  int lastCueFindCursor_ = -1;
  int lastCueFindDeckIndex_ = -1;
  std::string typedCueSearchBuffer_;
  Uint64 typedCueSearchLastKeyAtMs_ = 0;
  std::vector<float> deckPlaylistOpacityTargets_;
  std::vector<SDL_Rect> deckOpacityFaderRects_;
  std::vector<std::int16_t> vuSamples_;
  std::mutex vuSamplesMutex_;
  Uint64 vuSamplesUpdatedAtMs_ = 0;
  Uint64 vuDisplayUpdatedAtMs_ = 0;
  float vuDisplayRmsLeft_ = 0.0f;
  float vuDisplayRmsRight_ = 0.0f;
  float vuDisplayPeakLeft_ = 0.0f;
  float vuDisplayPeakRight_ = 0.0f;
  // Per-cue row display cache — avoids TTF_SizeUTF8 loop in ellipsizeToPixelWidth every frame
  struct CueRowDisplayCache {
    // Inputs (for staleness check)
    std::string name;
    int nameW = 0;
    std::string cueId;
    std::string cueNumber;
    int index = -1;
    CueKind kind = CueKind::Video;
    double duration = 0.0;
    double stillDurationSeconds = 0.0;
    CueEndAction endAction = CueEndAction::Inherit;
    int width = 0;
    int height = 0;
    bool pathEmpty = true;
    // Cached outputs
    std::string token;
    std::string kindUpper;
    std::string ellipsizedName;
    std::string meta;
  };
  std::unordered_map<std::string, CueRowDisplayCache> cueRowDisplayCache_;
  // Async cue probe futures (path → probed Cue)
  struct PendingProbe {
    int deckIndex;
    std::string path;  // filesystem path, used to find cue in deck
    std::future<std::optional<Cue>> future;
  };
  std::vector<PendingProbe> probeFutures_;
  // ── Built-in media converter ───────────────────────────────────────────
  // Async ffmpeg transcode of cues Deckboy can't play (or would play poorly)
  // into a compatible H.264 MP4, kept portable in <show>/_converted/.
  // A queued encode. Jobs are ENQUEUED by convertCueMedia and only started by
  // pumpConversionQueue, at most kEncoderMaxConcurrent at a time. Before this
  // every flagged cue span its own ffmpeg the instant it was added, so
  // "convert all" on a big show launched one encoder per cue simultaneously.
  enum class ConversionState { Queued, Running, Done, Failed };


  struct ConversionJob {
    int deckIndex;
    std::string sourcePath;
    std::string destPath;
    std::string label;    // cue name, shown on the busy panel
    ConversionState state = ConversionState::Queued;
    EncoderPreset preset = EncoderPreset::DeliveryH264;
    // Datamosh output sits BESIDE the original (the effect swaps between the
    // two), so completion sets cue.moshPath instead of repointing cue.path.
    bool keepsOriginal = false;
    std::string formatId;   // catalog row this job encodes with
    // Held jobs are skipped by the scheduler until released. Distinct from the
    // whole-queue pause: this parks ONE job while the rest continue.
    bool held = false;
    // Set by the UI to ask a running encode to stop; the worker checks it
    // between pipe reads and kills ffmpeg.
    std::shared_ptr<std::atomic<bool>> cancel;
    double sourceSeconds = 0.0;   // probed duration, the divisor for progress
    // 0..1 while encoding, <0 until ffmpeg reports anything. Shared rather than
    // held by value because the job is moved into the vector while the worker
    // thread writes it from the other side.
    std::shared_ptr<std::atomic<double>> progress;
    std::future<bool> future;
  };
  std::vector<ConversionJob> conversionJobs_;
  // One at a time by default: encoding must never outbid playback for CPU
  // or for the drive the media is streaming off during a show.
  int encoderConcurrency_ = 1;
  EncoderPreset encoderPreset_ = EncoderPreset::DeliveryH264;
  // Lazily probed from "ffmpeg -encoders"; see availableEncoders().
  bool encoderProbeDone_ = false;
  std::set<std::string> encoderProbeResult_;
  std::string encoderFormatId_ = "h264";   // selected row of the format matrix
  // Which datamosh flavour the DATAMOSH preset prepares. Both are moshable;
  // they differ only in how the smear looks (see the catalog comment).
  bool moshClassicLook_ = false;   // Encoder-tab manual converts only; cue
                                   // datamosh reads Cue::datamoshLook instead
  EncoderOverrides encoderOverrides_;  // quality/bitrate/fps/size/destination
  bool encoderQueuePaused_ = false;
  std::set<std::string> unreadablePaths_;  // probe returned nothing → offer convert
  // Waveform analysis cache (path → peaks vector)
  std::map<std::string, WaveformPeaks> waveformCache_;
  std::map<std::string, std::future<WaveformPeaks>> waveformFutures_;
  std::mutex waveformMutex_;
  // Subtitle track cache (path → parsed SRT)
  std::map<std::string, deckboy::core::SubtitleTrack> subtitleCache_;
  // Settings modal
  bool settingsOpen_ = false;
  int settingsTab_ = 0; // 0=System 1=Audio 2=Network 3=Video Outputs 4=About
  int settingsVideoSubTab_ = 0; // 0=Display 1=Processing 2=Backends 3=Streaming
  bool videoOutputsAdvanced_ = false;
  bool routingMoveMode_ = true; // true=single-output move, false=add/remove fan-out
  SDL_Rect settingsCloseBtn_ {};
  SDL_Rect settingsGearRect_ {};
  SDL_Rect deckSidebarToggleRect_ {};
  SDL_Rect blackoutBtnRect_ {};
  SDL_Rect fileNewBtnRect_ {};
  SDL_Rect fileOpenBtnRect_ {};
  SDL_Rect fileSaveBtnRect_ {};
  SDL_Rect fileBundleBtnRect_ {};
  SDL_Rect fileRelinkBtnRect_ {};
  SDL_Rect playlistJumpBtnRect_ {};   // "jump to live cue" button in the playlist header
  SDL_Rect fileSaveAsBtnRect_ {};
  SDL_Rect deckLoopBtnRect_ {};
  SDL_Rect deckShuffleBtnRect_ {};
  SDL_Rect fullscreenBtnRect_ {};
  double masterDimmerTarget_ = 1.0;  // target for animated masterDimmer (0=black, 1=full)
  bool pendingClearAfterFade_ = false; // clear output after dimmer fades to black
  bool panicProfilePending_ = false;
  std::string pendingPanicProfileToken_;
  Uint64 panicProfileRequestedAt_ = 0;
  double panicRestoreDimmerTarget_ = 1.0;
  std::vector<SettingsButton> settingsBtns_;
  bool midiEnabled_ = false;
#if !defined(DECKBOY_HAS_ALSA) && defined(DECKBOY_HAS_MIDI)
  // Cross-platform MIDI input (Windows/macOS). ALSA builds use midiSeq_ below.
  deckboy::platform::midi::MidiInput midiRt_;
#endif
  std::string midiDeviceName_;
#if defined(DECKBOY_HAS_ALSA)
  snd_seq_t* midiSeq_ = nullptr;
  int midiSeqPort_ = -1;
  std::thread midiThread_;
  std::atomic<bool> midiStop_ {false};
  std::array<int, 8> midiMtcQuarterFrameNibbles_ {{-1, -1, -1, -1, -1, -1, -1, -1}};
  double midiMtcLastSentSeconds_ = -1.0;
  double midiMtcLastSentFps_ = 0.0;
#endif

  // Companion / OSC / integration bridge state (cross-platform)
  SocketHandle companionTcpListen_ = kInvalidSocket;
  SocketHandle companionUdpSocket_ = kInvalidSocket;
  SocketHandle oscQueryTcpListen_ = kInvalidSocket;
  bool oscQueryReady_ = false;
  std::atomic<bool> oscQueryStop_ {false};
  std::thread oscQueryThread_;
  std::mutex companionClientsMutex_;  // protects companionClients_ + companionClientBuffers_
  std::vector<SocketHandle> companionClients_;
  std::map<SocketHandle, std::string> companionClientBuffers_;
  // Clients that half-closed (sent EOF) but are still owed an OK/ERR for a
  // command already queued. Value is the SDL_GetTicks deadline after which the
  // socket is closed regardless. See the linger note in the reader loop.
  std::map<SocketHandle, Uint64> companionDrainingClients_;
  static constexpr Uint64 kCompanionDrainMs = 750;
  std::map<std::string, std::pair<sockaddr_in, Uint64>> oscSubscribers_;
  Uint64 lastOscFeedbackBroadcastMs_ = 0;
  std::string lastOscFeedbackPayload_;
  Uint64 lastOscMirrorFeedbackBroadcastMs_ = 0;
  std::string lastOscMirrorFeedbackPayload_;
  SocketHandle atemBridgeSocket_ = kInvalidSocket;
  SocketHandle artNetSocket_ = kInvalidSocket;
  std::thread atemBridgeThread_;
  std::thread artNetBridgeThread_;
  std::thread nmcSyncThread_;
  std::thread ndiTriggerThread_;
  std::atomic<bool> atemBridgeStop_ {false};
  std::atomic<bool> artNetBridgeStop_ {false};
  std::atomic<bool> nmcSyncStop_ {false};
  std::atomic<bool> nmcSyncRunning_ {false};
  std::atomic<bool> ndiTriggerStop_ {false};
  std::atomic<bool> ndiTriggerRunning_ {false};
  SocketHandle nmcSyncSocket_ = kInvalidSocket;
  int atemBridgeListenPort_ = kDefaultAtemBridgePort;
  int artNetListenPort_ = kDefaultArtNetPort;
  std::array<std::uint8_t, 512> artNetLastDmx_ {};
  sockaddr_in nmcSyncTargetAddress_ {};
  std::string nmcSyncActiveMode_;
  int nmcSyncActivePort_ = 0;
  std::string nmcSyncActiveHost_;
  std::string nmcSyncActiveSourceFilter_;
  std::string nmcSyncLastError_;
  std::string nmcSyncLastAnnouncedError_;
  Uint64 nmcSyncRestartBlockedUntilMs_ = 0;
  TransportState nmcSyncLastSentState_ = TransportState::Stopped;
  double nmcSyncLastSentSeconds_ = -1.0;
  Uint64 nmcSyncLastLocateSentMs_ = 0;
  bool nmcSyncOutputStateInitialized_ = false;
  NdiTriggerApi ndiTriggerApi_;
  std::string ndiTriggerConnectedSource_;
  std::string ndiTriggerLastError_;
  std::string ndiTriggerLastAnnouncedError_;
  Uint64 ndiTriggerRestartBlockedUntilMs_ = 0;

  // LTC ingest state (cross-platform — libltc loaded dynamically at runtime)
  std::thread ltcThread_;
  std::atomic<bool> ltcStop_ {false};
  SDL_AudioStream* ltcCaptureStream_ = nullptr;  // SDL3 recording stream for LTC ingest
  int ltcCaptureSampleRate_ = 0;
  int ltcCaptureChannels_ = 0;
  std::string ltcCaptureDeviceName_;
  std::string ltcLastError_;
  std::string ltcLastAnnouncedError_;
  Uint64 ltcRestartBlockedUntilMs_ = 0;
  // TSL/Tally socket — cross-platform (UDP send-only)
  deckboy::platform::SocketHandle tslTallySocket_ = deckboy::platform::kInvalidSocket;
};

}  // namespace

// ════════════════════════════════════════════════════════════════════════════
// Entry points
// ════════════════════════════════════════════════════════════════════════════

// Shared entry point for both main() and WinMain(). Parses the command line
// (see the block comment above runDeckboyMain for the rules and `--help` for
// the flags), runs a mode flag headless if one is present, otherwise:
// acquire instance lock → App::init() → App::run() → App::shutdown()
#ifdef _WIN32
// ── Crash logger ────────────────────────────────────────────────────────────
// Deckboy has been dying silently: Windows Error Reporting keeps a dump and a
// module name, but the app leaves nothing behind, so a crash mid-show tells the
// operator (and the next debugging session) nothing at all. Several crashes are
// on record with only "faulting module SDL3.dll" to go on.
//
// This writes a symbolised stack to deckboy-crash.log next to the data dir the
// moment an unhandled SEH exception fires — before the process is gone. It is
// diagnosis, not recovery: the app still dies, it just says why.
LONG WINAPI deckboyCrashHandler(EXCEPTION_POINTERS* info) {
  static std::atomic<bool> handled {false};
  bool expected = false;
  if (!handled.compare_exchange_strong(expected, true)) {
    return EXCEPTION_EXECUTE_HANDLER;  // a second fault while logging the first
  }
  fs::path logPath = Paths::stateDir() / "deckboy-crash.log";
  std::ofstream log(logPath, std::ios::app);
  if (log) {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    log << "\n=== Deckboy crash " << deckboy::core::version::kVersionTag << " ===\n";
    log << "time: " << now << "\n";
    if (info && info->ExceptionRecord) {
      log << "code: 0x" << std::hex << info->ExceptionRecord->ExceptionCode << std::dec
          << "  address: " << info->ExceptionRecord->ExceptionAddress << "\n";
    }
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    void* frames[62] {};
    const USHORT captured = CaptureStackBackTrace(0, 62, frames, nullptr);
    alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 512] {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    for (USHORT i = 0; i < captured; ++i) {
      const DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
      log << "  [" << i << "] " << frames[i];
      // Log module + RVA even when no PDB is loaded. Deckboy's own frames have
      // no runtime symbols, so SymFromAddr falls back to the nearest EXPORT
      // (wrong — e.g. "RtMidiError::what" for an App:: method). module+0xRVA is
      // exact and resolves offline against the matching build's .pdb/.map, which
      // is the only way to read a release crash. Format: Deckboy.exe+0x231b07.
      HMODULE mod = nullptr;
      if (GetModuleHandleExW(
              GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
              reinterpret_cast<LPCWSTR>(addr), &mod) && mod) {
        wchar_t modPath[MAX_PATH] {};
        if (GetModuleFileNameW(mod, modPath, MAX_PATH)) {
          const wchar_t* base = wcsrchr(modPath, L'\\');
          const wchar_t* name = base ? base + 1 : modPath;
          char nameA[MAX_PATH] {};
          WideCharToMultiByte(CP_UTF8, 0, name, -1, nameA, MAX_PATH, nullptr, nullptr);
          const DWORD64 rva = addr - reinterpret_cast<DWORD64>(mod);
          log << "  " << nameA << "+0x" << std::hex << rva << std::dec;
        }
      }
      DWORD64 displacement = 0;
      if (SymFromAddr(process, addr, &displacement, symbol)) {
        log << "  " << symbol->Name;
      }
      IMAGEHLP_LINE64 line {};
      line.SizeOfStruct = sizeof(line);
      DWORD lineDisp = 0;
      if (SymGetLineFromAddr64(process, addr, &lineDisp, &line) && line.FileName) {
        log << "  (" << line.FileName << ":" << line.LineNumber << ")";
      }
      log << "\n";
    }
    log.flush();
  }
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#ifndef _WIN32
// POSIX counterpart to deckboyCrashHandler above. Without it a crash on Linux
// or macOS leaves NOTHING behind — no faulting module, no stack, nothing — so
// the platform where we have the least debugging history was also the platform
// that told us the least. Same contract as the Windows handler: this is
// diagnosis, not recovery. The process still dies, it just says why.
//
// Everything here runs in a signal handler, so it is restricted to
// async-signal-safe calls: open/write/_exit and backtrace_symbols_fd. That is
// why the log path is resolved ONCE at startup into a fixed buffer rather than
// being built with std::filesystem when the fault arrives — allocating inside a
// SIGSEGV handler is how a crash reporter becomes a second crash.
static char g_crashLogPath[1024] = {0};

extern "C" void deckboyPosixCrashHandler(int sig) {
  static volatile sig_atomic_t handled = 0;
  if (handled) {
    _exit(128 + sig);           // a second fault while logging the first
  }
  handled = 1;

  if (g_crashLogPath[0] != '\0') {
    const int fd = open(g_crashLogPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
      auto emit = [fd](const char* text) {
        ssize_t ignored = write(fd, text, std::strlen(text));
        (void) ignored;
      };
      emit("\n=== Deckboy crash ");
      emit(deckboy::core::version::kVersionTag);
      emit(" ===\nsignal: ");
      switch (sig) {
        case SIGSEGV: emit("SIGSEGV (invalid memory access)"); break;
        case SIGBUS:  emit("SIGBUS (bad address / misaligned)"); break;
        case SIGFPE:  emit("SIGFPE (arithmetic fault)"); break;
        case SIGILL:  emit("SIGILL (illegal instruction)"); break;
        case SIGABRT: emit("SIGABRT (abort / uncaught exception)"); break;
        default:      emit("unknown"); break;
      }
      emit("\nstack:\n");
      void* frames[62];
      const int captured = backtrace(frames, 62);
      backtrace_symbols_fd(frames, captured, fd);
      close(fd);
    }
  }

  // Re-raise with the default handler so the shell and any supervisor still see
  // a genuine crash (and a core file is still produced if enabled), rather than
  // a tidy exit code that hides the fault.
  signal(sig, SIG_DFL);
  raise(sig);
}

static void installPosixCrashHandler() {
  const std::string path = (Paths::stateDir() / "deckboy-crash.log").string();
  if (path.size() < sizeof(g_crashLogPath)) {
    std::memcpy(g_crashLogPath, path.c_str(), path.size() + 1);
  }
  for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) {
    struct sigaction sa {};
    sa.sa_handler = deckboyPosixCrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(sig, &sa, nullptr);
  }
}
#endif

// Put the executable's own directory on PATH, ahead of everything else.
//
// Deckboy spawns helpers by bare name ("ffmpeg", "ffprobe"), and on POSIX that
// goes through execvp, which searches PATH and nothing else. A portable bundle
// therefore ships an ffmpeg that the app can never actually find: it falls
// through to a system copy if one exists, and simply fails to decode on a clean
// machine — which is exactly the machine a portable build is for.
//
// Windows does not need this (CreateProcess searches the application directory
// first), but doing it everywhere keeps one behaviour to reason about.
//
// In a dev build the executable's directory holds no ffmpeg, so the lookup
// falls straight through to the system one and nothing changes.
static void prependExecutableDirToPath() {
  const std::filesystem::path exe = deckboy::core::Paths::executablePath();
  if (exe.empty()) {
    return;
  }
  const std::string dir = exe.parent_path().string();
  if (dir.empty()) {
    return;
  }
#ifdef _WIN32
  const char kSep = ';';
#else
  const char kSep = ':';
#endif
  const char* existing = std::getenv("PATH");
  std::string updated = dir;
  if (existing && existing[0] != '\0') {
    updated += kSep;
    updated += existing;
  }
#ifdef _WIN32
  _putenv_s("PATH", updated.c_str());
#else
  setenv("PATH", updated.c_str(), 1);
#endif
}

// ---------------------------------------------------------------------------
// Command line
//
// Flags come in two shapes:
//   MODE flags take the process over: they run headless and exit (--smoke,
//     --pattern-dump, --decode-bench, …). At most one may appear.
//   OPTION flags modify a run (--import, --settings, --soak) or apply to any
//     run at all (--no-inproc-decode, --allow-multi-instance).
//
// Three rules this parser enforces, each of which used to fail SILENTLY:
//   * option flags are read wherever they sit on the line, not only in argv[1]
//     ("--decode-bench clip.mp4 --no-inproc-decode" ignored the modifier, and
//     swapping the order launched the GUI instead);
//   * "--flag=value" is accepted as well as "--flag value"
//     ("--pattern-bench=terrarium" launched the GUI);
//   * an unknown flag, or a mode flag missing its operands, is an ERROR with a
//     message — never a fall-through into the GUI.
// A bare path is the show (or media) to open, which is what the Windows
// .deckboy file association and a Finder/Explorer "open with" actually pass.
// ---------------------------------------------------------------------------
namespace {

struct CliFlagHelp {
  const char* usage;
  const char* text;
};

constexpr CliFlagHelp kCliModeHelp[] = {
  {"--help, -h", "print this help and exit"},
  {"--version", "print the version and exit"},
  {"--self-check", "report ffmpeg / SDK / runtime availability and exit"},
  {"--smoke", "run the built-in smoke suite and exit"},
  {"--sync-pop-test", "run the A/V sync pop test and exit"},
  {"--hap-probe <file.mov>", "demux a HAP file to blocks and report, no window"},
  {"--asio-probe [name]", "list ASIO drivers; with a name, load and report its channels"},
  {"--sheet-probe <png> [tw] [th]", "slice a sprite sheet and report the tile grid"},
  {"--asio-tone <name> [secs] [ch]", "play a quiet 440Hz tone through an ASIO driver"},
  {"--timer-dump <out.ppm> [dur] [elapsed]", "render one stage-timer frame to a PPM"},
  {"--pattern-bench <pattern> [WxH] [frames]", "time pattern generation, no window or IO"},
  {"--pattern-dump <pattern> <out.ppm> [WxH] [seconds]", "render one pattern frame to a PPM file"},
  {"--effect-dump <token[:amt[:a[:b]]]> <in.ppm> <out.ppm> [frame]",
     "apply one effect to one picture, no window"},
    {"--effect-bench <token[:amt[:a[:b]]]> [WxH] [frames]",
     "time one effect per frame at a raster"},
    {"--pdf-probe <file.pdf> [outdir] [width]",
     "rasterise a slide deck, no window"},
    {"--decode-bench <file> [seconds] [cli]", "decode a file, report gpu/cpu frame counts"},
  {"--motion-probe <file> [frames]", "read the clip's motion vectors, report coverage"},
  {"--ltc-generate <out.wav> [tc] [fps] [seconds]", "write a SMPTE LTC timecode WAV"},
};

constexpr CliFlagHelp kCliOptionHelp[] = {
  {"--import <path>", "import media at launch (skips splash and startup menu)"},
  {"--settings [tab[.subtab]]", "open the settings modal at boot, e.g. --settings 3.1"},
  {"--soak [minutes]", "long-run stability harness (default 1440); logs to deckboy-soak.log"},
  {"--no-inproc-decode", "keep every decode on the ffmpeg CLI pipe path"},
  {"--allow-multi-instance", "bypass the single-instance lock"},
};

constexpr const char* kCliModeFlags[] = {
  "--version", "--self-check", "--smoke", "--sync-pop-test",
  "--pattern-bench", "--pattern-dump", "--effect-dump", "--effect-bench",
  "--decode-bench", "--ltc-generate",
  "--hap-probe", "--asio-probe", "--asio-tone", "--sheet-probe", "--timer-dump",
  "--motion-probe", "--pdf-probe",
};

constexpr CliFlagHelp kCliEnvHelp[] = {
  {"DECKBOY_ROOT", "project root holding data/ (also makes that dir the writable one)"},
  {"DECKBOY_STATE_DIR", "where shows/state/crash logs are written"},
  {"DECKBOY_PROJECT", "show file to open instead of the remembered one"},
  {"DECKBOY_COMPANION_PORT", "Companion control port (default 5510)"},
  {"DECKBOY_FFMPEG / DECKBOY_FFPROBE", "explicit paths to the ffmpeg binaries"},
  {"DECKBOY_EGRESS_READBACK", "sync = force the portable recording readback (diagnostic)"},
};

bool isCliModeFlag(const std::string& token) {
  for (const char* flag : kCliModeFlags) {
    if (token == flag) return true;
  }
  return false;
}

void printDeckboyUsage(std::ostream& out) {
  printDeckboyVersion(out);
  out << "\nUsage:\n"
      << "  Deckboy [show.deckboy | media...] [options]\n"
      << "  Deckboy <mode> [arguments]\n"
      << "\nModes (run headless and exit):\n";
  for (const CliFlagHelp& help : kCliModeHelp) {
    out << "  " << std::left << std::setw(52) << help.usage << help.text << '\n';
  }
  out << "\nOptions:\n";
  for (const CliFlagHelp& help : kCliOptionHelp) {
    out << "  " << std::left << std::setw(52) << help.usage << help.text << '\n';
  }
  out << "\nEnvironment:\n";
  for (const CliFlagHelp& help : kCliEnvHelp) {
    out << "  " << std::left << std::setw(52) << help.usage << help.text << '\n';
  }
  out << "\nBoth spellings work everywhere: --flag value and --flag=value.\n"
      << std::right;
}

void printCliError(const std::string& message) {
  std::cerr << "deckboy: " << message << "\nTry 'Deckboy --help'.\n";
}

// Normalize argv for parsing: split "--flag=value", and drop the process-serial
// argument macOS's Finder appends when it launches a bundle (it is not ours and
// must not be reported as an unknown flag).
std::vector<std::string> normalizeCliArgs(int argc, char** argv) {
  std::vector<std::string> out;
  out.reserve(static_cast<size_t>(argc > 1 ? argc - 1 : 0));
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i] ? argv[i] : "";
    if (arg.empty() || arg.rfind("-psn_", 0) == 0) {
      continue;
    }
    if (arg.rfind("--", 0) == 0) {
      const auto eq = arg.find('=');
      if (eq != std::string::npos && eq > 2) {
        out.push_back(arg.substr(0, eq));
        out.push_back(arg.substr(eq + 1));
        continue;
      }
    }
    out.push_back(std::move(arg));
  }
  return out;
}

// "1920x1080" → raster. Shared by the operand lists of --pattern-bench and
// --pattern-dump, where sizes and counts may appear in either order.
bool parseCliRaster(const std::string& token, int& width, int& height) {
  const auto xPos = token.find('x');
  if (xPos == std::string::npos || xPos == 0) {
    return false;
  }
  const int w = std::atoi(token.substr(0, xPos).c_str());
  const int h = std::atoi(token.substr(xPos + 1).c_str());
  if (w <= 0 || h <= 0) {
    return false;
  }
  width = w;
  height = h;
  return true;
}

// Run a mode flag. `ops` is everything after it, with the global modifiers
// already removed, so a modifier may sit anywhere on the line.
int runDeckboyCliMode(const std::string& mode, const std::vector<std::string>& ops) {
  auto missing = [&](const char* operands) {
    printCliError(mode + " needs " + operands);
    return 2;
  };

  if (mode == "--version") {
    printDeckboyVersion(std::cout);
    return 0;
  }
  if (mode == "--self-check") {
    return App::runSelfCheck();
  }
  if (mode == "--smoke") {
    return App::runSmoke();
  }
  if (mode == "--timer-dump") {
    // Render one timer frame headlessly. ops: <out.ppm> [duration] [elapsed]
    if (ops.empty()) return missing("<out.ppm> [durationSeconds] [elapsedSeconds]");
    TimerSettings cfg;
    if (ops.size() > 1) cfg.durationSeconds = std::atoi(ops[1].c_str());
    if (ops.size() > 3) cfg.message = ops[3];
    // ops[4]: 0 countdown, 1 count-up, 2 time-of-day. Lets the modes be diffed
    // headlessly, which is how the dead-control bug in them was caught.
    if (ops.size() > 4) {
      cfg.mode = static_cast<TimerMode>(std::clamp(std::atoi(ops[4].c_str()), 0, 2));
    }
    const double elapsed = ops.size() > 2 ? std::atof(ops[2].c_str()) : 0.0;
    cfg.blinkAtZero = false;
    DecodedFrame f;
    f.width = 960; f.height = 540;
    f.pixels.assign(static_cast<std::size_t>(f.width) * f.height * 4, 0);
    MediaEngine::buildTimerFrame(f, cfg, elapsed, true);
    std::ofstream ppm(ops[0], std::ios::binary);
    ppm << "P6" << '\n' << f.width << " " << f.height << " 255" << '\n';
    for (std::size_t i = 0; i + 3 < f.pixels.size(); i += 4) {
      ppm.put(static_cast<char>(f.pixels[i]));
      ppm.put(static_cast<char>(f.pixels[i + 1]));
      ppm.put(static_cast<char>(f.pixels[i + 2]));
    }
    std::cout << "timer-dump: " << ops[0] << '\n';
    return 0;
  }
  if (mode == "--asio-tone") {
    // Deliberately opt-in and time-limited. This OPENS the operator's audio
    // interface and makes noise through it, so it is never run automatically
    // and never runs unbounded.
    if (ops.empty()) return missing("<driver-name> [seconds] [channels]");
    const double seconds = ops.size() > 1 ? std::max(0.5, std::atof(ops[1].c_str())) : 2.0;
    const int chans = ops.size() > 2 ? std::max(1, std::atoi(ops[2].c_str())) : 2;
    deckboy::platform::audio::AsioOutput out;
    std::string err;
    if (!out.open(ops[0], chans, 48000.0, err)) {
      std::cerr << "asio-tone: " << err << "\n";
      return 1;
    }
    std::cout << "asio-tone: " << ops[0]
              << "  channels=" << out.channels()
              << " rate=" << out.sampleRate()
              << " buffer=" << out.bufferFrames() << " frames\n";
    // A quiet 440Hz sine. Quiet on purpose: this may be going to a PA.
    const double rate = out.sampleRate() > 0.0 ? out.sampleRate() : 48000.0;
    const std::size_t total = static_cast<std::size_t>(rate * seconds);
    std::vector<std::int16_t> chunk(static_cast<std::size_t>(chans) * 256);
    std::size_t written = 0;
    double phase = 0.0;
    const double step = 2.0 * 3.14159265358979 * 440.0 / rate;
    while (written < total) {
      const std::size_t frames = std::min<std::size_t>(256, total - written);
      for (std::size_t f = 0; f < frames; ++f) {
        const std::int16_t v =
          static_cast<std::int16_t>(std::sin(phase) * 3000.0);
        phase += step;
        for (int c = 0; c < chans; ++c) chunk[f * chans + c] = v;
      }
      std::size_t done = 0;
      while (done < frames) {
        const std::size_t n = out.write(chunk.data() + done * chans, frames - done);
        done += n;
        if (n == 0) SDL_Delay(1);   // ring full: let the driver drain
      }
      written += frames;
    }
    // Let the tail play out before tearing the driver down.
    while (out.queuedFrames() > 0) SDL_Delay(1);
    SDL_Delay(50);
    const std::uint64_t under = out.underruns();
    out.close();
    std::cout << "asio-tone: done, underruns=" << under << "\n";
    return under == 0 ? 0 : 3;
  }

  if (mode == "--sheet-probe") {
    // Reports every stage of the slice so a sheet that produces no tiles can
    // be diagnosed without opening the app: decode, grid, and how many tiles
    // survived the coverage filter.
    if (ops.empty()) return missing("<sheet.png> [tileW] [tileH]");
    const int tw = ops.size() > 1 ? std::max(2, std::atoi(ops[1].c_str())) : 16;
    const int th = ops.size() > 2 ? std::max(2, std::atoi(ops[2].c_str())) : tw;
    MediaEngine probe(nullptr, nullptr, MediaEngine::AudioTapCallback {},
                      [](const Cue&) { return std::string(); });
    const bool ok = probe.ensureSpriteSheet(ops[0], tw, th);
    std::cout << "sheet: " << ops[0] << '\n'
              << "  decoded: " << probe.spriteSheetWidth() << "x"
              << probe.spriteSheetHeight() << '\n'
              << "  tile: " << tw << "x" << th << '\n'
              << "  grid: " << probe.spriteSheetCols() << " x "
              << probe.spriteSheetRows() << " = "
              << (probe.spriteSheetCols() * probe.spriteSheetRows())
              << " tiles" << '\n'
              << "  usable after coverage filter: "
              << probe.spriteUsableTiles() << '\n'
              << "  result: " << (ok ? "OK" : "FAILED") << '\n';
    return ok ? 0 : 1;
  }

  if (mode == "--asio-probe") {
    // No operand: list what is installed, which is cheap and touches no
    // driver. With a name: LOAD that driver and report its real capabilities,
    // which is intrusive enough that it must stay opt-in.
    if (!deckboy::platform::audio::asioSupportCompiled()) {
      std::cerr << "asio-probe: this build has no ASIO support\n";
      return 1;
    }
    auto devices = deckboy::platform::audio::listAsioDevices();
    if (ops.empty()) {
      std::cout << "asio drivers installed: " << devices.size() << '\n';
      for (const auto& dev : devices) {
        std::cout << "  " << dev.name << '\n';
      }
      return devices.empty() ? 2 : 0;
    }
    deckboy::platform::audio::AsioDeviceInfo info;
    if (!deckboy::platform::audio::probeAsioDevice(ops[0], info)) {
      std::cerr << "asio-probe: " << (info.error.empty() ? "failed" : info.error) << '\n';
      return 1;
    }
    std::cout << "asio driver: " << info.name << '\n'
              << "  inputs=" << info.inputChannels
              << " outputs=" << info.outputChannels << '\n'
              << "  preferred-buffer=" << info.preferredBufferFrames
              << " frames" << '\n'
              << "  sample-rate=" << info.sampleRate << '\n'
              << "  output-latency=" << info.outputLatencyFrames << " frames" << '\n';
    return 0;
  }

  if (mode == "--hap-probe") {
    if (ops.empty()) return missing("<file.mov>");
#if DECKBOY_INPROC_DECODE
    deckboy::libav::HapProbeResult r {};
    std::string err;
    if (!deckboy::libav::probeHapFile(ops[0], r, err)) {
      std::cerr << "hap-probe: " << err << '\n';
      return 1;
    }
    // Optional second operand: dump frame 1 as a PPM so the CPU expansion can
    // be diffed against a reference decoder pixel for pixel.
    if (ops.size() > 1 && !r.rgbaFirstFrame.empty()) {
      std::ofstream ppm(ops[1], std::ios::binary);
      ppm << "P6" << '\n' << r.width << " " << r.height << " 255" << '\n';
      for (std::size_t px = 0; px + 3 < r.rgbaFirstFrame.size(); px += 4) {
        ppm.put(static_cast<char>(r.rgbaFirstFrame[px]));
        ppm.put(static_cast<char>(r.rgbaFirstFrame[px + 1]));
        ppm.put(static_cast<char>(r.rgbaFirstFrame[px + 2]));
      }
      std::cout << "  wrote " << ops[1] << '\n';
    }
    std::cout << "hap-probe: " << ops[0] << '\n'
              << "  stream=hap " << r.width << "x" << r.height
              << " fps=" << r.fps << '\n'
              << "  frames-decoded=" << r.frames
              << " texture=" << r.textureFormat
              << " block-bytes=" << r.blockBytes << '\n';
    return 0;
#else
    std::cerr << "hap-probe: this build has no in-process decoder" << '\n';
    return 2;
#endif
  }
  if (mode == "--sync-pop-test") {
    return App::runSyncPopTest();
  }
  if (mode == "--pattern-bench") {
    if (ops.empty()) return missing("<pattern> [WxH] [frames]");
    int bw = 3840, bh = 2160, bframes = 60;
    for (size_t i = 1; i < ops.size(); ++i) {
      if (parseCliRaster(ops[i], bw, bh)) continue;
      const int n = std::atoi(ops[i].c_str());
      if (n > 0) bframes = n;
    }
    return App::runPatternBench(ops[0], bw, bh, bframes);
  }
  if (mode == "--pattern-dump") {
    if (ops.size() < 2) return missing("<pattern> <out.ppm> [WxH] [seconds]");
    int dumpW = 1280, dumpH = 720;
    double dumpT = 30.0;
    for (size_t i = 2; i < ops.size(); ++i) {
      if (parseCliRaster(ops[i], dumpW, dumpH)) continue;
      const double parsed = std::atof(ops[i].c_str());
      if (parsed >= 0.0) dumpT = parsed;
    }
    return App::runPatternDump(ops[0], ops[1], dumpW, dumpH, dumpT);
  }
  if (mode == "--effect-bench") {
    if (ops.empty()) return missing("<token[:amount[:a[:b]]]> [WxH] [frames]");
    int bw = 1920, bh = 1080, bframes = 30;
    for (size_t i = 1; i < ops.size(); ++i) {
      if (parseCliRaster(ops[i], bw, bh)) continue;
      const int n = std::atoi(ops[i].c_str());
      if (n > 0) bframes = n;
    }
    return App::runEffectBench(ops[0], bw, bh, bframes);
  }
  if (mode == "--effect-dump") {
    if (ops.size() < 3) return missing("<token[:amount[:a[:b]]]> <in.ppm> <out.ppm> [frame]");
    const int dumpFrame = ops.size() > 3 ? std::atoi(ops[3].c_str()) : 0;
    return App::runEffectDump(ops[0], ops[1], ops[2], dumpFrame);
  }
  if (mode == "--pdf-probe") {
    if (ops.empty()) return missing("<file.pdf> [outdir] [width]");
    const std::string outDir = ops.size() > 1 ? ops[1] : std::string();
    const int width = ops.size() > 2 ? std::atoi(ops[2].c_str()) : 3840;
    return App::runPdfProbe(ops[0], outDir, width > 0 ? width : 3840);
  }
  if (mode == "--motion-probe") {
    if (ops.empty()) return missing("<file> [frames]");
    int wanted = 60;
    if (ops.size() > 1) {
      wanted = std::max(1, std::atoi(ops[1].c_str()));
    }
    void* src = deckboy::motion::openMotionSource(ops[0]);
    if (!src) {
      std::cerr << "motion-probe: could not open " << ops[0]
                << " (built without in-process decode?)\n";
      return 1;
    }
    deckboy::motion::MotionField field;
    int frames = 0, withVectors = 0, emptyFrames = 0;
    double sumMagnitude = 0.0, peakMagnitude = 0.0;
    std::size_t movingCells = 0, totalCells = 0;
    const Uint64 startMs = SDL_GetTicks();
    while (frames < wanted && deckboy::motion::readMotionField(src, field)) {
      ++frames;
      if (field.empty()) {
        ++emptyFrames;
        continue;
      }
      ++withVectors;
      totalCells += field.dx.size();
      for (std::size_t i = 0; i < field.dx.size(); ++i) {
        const double m = std::sqrt(static_cast<double>(field.dx[i]) * field.dx[i] +
                                   static_cast<double>(field.dy[i]) * field.dy[i]);
        if (m > 0.01) ++movingCells;
        sumMagnitude += m;
        peakMagnitude = std::max(peakMagnitude, m);
      }
    }
    const Uint64 elapsedMs = SDL_GetTicks() - startMs;
    deckboy::motion::closeMotionSource(src);

    std::cout << "motion-probe " << ops[0] << "\n";
    std::cout << "  frames read      " << frames << "\n";
    std::cout << "  with vectors     " << withVectors << "\n";
    std::cout << "  empty (I-frames) " << emptyFrames << "\n";
    if (withVectors > 0 && totalCells > 0) {
      std::cout << "  grid             " << field.cols << "x" << field.rows
                << " cells over " << field.sourceWidth << "x" << field.sourceHeight << "\n";
      std::cout << "  cells in motion  "
                << (100.0 * static_cast<double>(movingCells) /
                    static_cast<double>(totalCells)) << "%\n";
      std::cout << "  mean magnitude   "
                << (sumMagnitude / static_cast<double>(totalCells)) << " px\n";
      std::cout << "  peak magnitude   " << peakMagnitude << " px\n";
    }
    std::cout << "  decode time      " << elapsedMs << "ms for " << frames
              << " frames (" << (frames > 0 ? (1000.0 * frames / std::max<Uint64>(1, elapsedMs)) : 0.0)
              << " fps)\n";
    // The question this probe exists to answer.
    const bool usable = withVectors > 0 && movingCells > 0;
    std::cout << "  VERDICT          "
              << (usable ? "vectors usable" : "NO USABLE VECTORS") << "\n";
    return usable ? 0 : 1;
  }

  if (mode == "--decode-bench") {
    if (ops.empty()) return missing("<file> [seconds] [cli]");
    double benchSeconds = 10.0;
    // --no-inproc-decode is the documented way to force the pipe path, so it
    // has to reach the bench too — otherwise the CLI decode path could not be
    // benchmarked at all. "cli" stays as the positional spelling.
    bool forceCli = MediaEngine::inprocDecodeDisabled();
    for (size_t i = 1; i < ops.size(); ++i) {
      if (ops[i] == "cli") {
        forceCli = true;
        continue;
      }
      const double parsed = std::atof(ops[i].c_str());
      if (parsed > 0.0) benchSeconds = parsed;
    }
    return App::runDecodeBench(ops[0], benchSeconds, forceCli);
  }
  if (mode == "--ltc-generate") {
    if (ops.empty()) return missing("<out.wav> [tc] [fps] [seconds]");
    const std::string startTc = ops.size() > 1 ? ops[1] : "10:00:00:00";
    const double fps = ops.size() > 2 ? std::atof(ops[2].c_str()) : 30.0;
    const double secs = ops.size() > 3 ? std::atof(ops[3].c_str()) : 2.0;
    return App::runLtcGenerate(ops[0], startTc, fps > 0.0 ? fps : 30.0,
                               secs > 0.0 ? secs : 2.0);
  }

  printCliError("unhandled mode " + mode);  // unreachable while kCliModeFlags matches
  return 2;
}

}  // namespace

int runDeckboyMain(int argc, char** argv) {
#ifdef _WIN32
  SetUnhandledExceptionFilter(deckboyCrashHandler);
#else
  installPosixCrashHandler();
#endif
  // Before anything that might spawn ffmpeg — including --self-check, which
  // reports whether ffmpeg is reachable and must therefore report the truth
  // about THIS build's lookup path.
  prependExecutableDirToPath();

  const std::vector<std::string> args = normalizeCliArgs(argc, argv);
  for (const std::string& arg : args) {
    if (arg == "--help" || arg == "-h" || arg == "-?" || arg == "/?") {
      printDeckboyUsage(std::cout);
      return 0;
    }
  }

  // Global modifiers first, and removed from the line, so they may appear
  // before or after a mode flag and its operands.
  bool allowMultiInstance = false;
  std::vector<std::string> rest;
  rest.reserve(args.size());
  for (const std::string& arg : args) {
    if (arg == "--no-inproc-decode") {
      // Operator break-glass: keep every decode on the ffmpeg CLI pipe path
      // for this run (robustness over Pocket performance).
      MediaEngine::setInprocDecodeDisabled(true);
      continue;
    }
    if (arg == "--allow-multi-instance") {
      allowMultiInstance = true;
      continue;
    }
    rest.push_back(arg);
  }

  for (size_t i = 0; i < rest.size(); ++i) {
    if (!isCliModeFlag(rest[i])) {
      continue;
    }
    if (i != 0) {
      printCliError(rest[i] + " must come before its arguments (saw '" + rest[0] + "' first)");
      return 2;
    }
    return runDeckboyCliMode(rest[0], {rest.begin() + 1, rest.end()});
  }

  double soakMinutes = -1.0;
  std::vector<std::string> importPathsArg;
  fs::path startupProjectArg;
  int openSettingsTab = -1;
  int inspectorScrollArg = -1;
  int openSettingsSubTab = 0;
  for (size_t i = 0; i < rest.size(); ++i) {
    const std::string& arg = rest[i];
    if (arg == "--import") {
      if (i + 1 >= rest.size()) {
        printCliError("--import needs a path");
        return 2;
      }
      importPathsArg.push_back(rest[++i]);
      continue;
    }
    if (arg == "--inspector-scroll") {
      if (i + 1 >= rest.size()) {
        printCliError("--inspector-scroll needs a pixel offset");
        return 2;
      }
      inspectorScrollArg = std::atoi(rest[++i].c_str());
      continue;
    }
    if (arg == "--settings") {
      // Optional "tab" or "tab.subtab" (video sub-tab), e.g. --settings 3.1
      openSettingsTab = 0;
      if (i + 1 < rest.size()) {
        char* end = nullptr;
        const long parsed = std::strtol(rest[i + 1].c_str(), &end, 10);
        if (end && end != rest[i + 1].c_str() && (*end == '\0' || *end == '.')) {
          openSettingsTab = static_cast<int>(parsed);
          if (*end == '.') {
            openSettingsSubTab = static_cast<int>(std::strtol(end + 1, nullptr, 10));
          }
          ++i;
        }
      }
      continue;
    }
    if (arg == "--soak") {
      // Long-run stability harness on the real app loop. Optional minutes
      // argument; default 24 h. Loops the loaded show (see tickSoak).
      soakMinutes = 1440.0;
      if (i + 1 < rest.size()) {
        const double parsed = std::atof(rest[i + 1].c_str());
        if (parsed > 0.0) {
          soakMinutes = parsed;
          ++i;
        }
      }
      continue;
    }
    if (arg.rfind("-", 0) == 0) {
      printCliError("unknown flag " + arg);
      return 2;
    }
    // Bare path: a show opens as the project, anything else is imported. This
    // is the shape the .deckboy file association hands us.
    fs::path candidate(arg);
    if (startupProjectArg.empty() && candidate.extension() == ".deckboy") {
      startupProjectArg = candidate;
    } else {
      importPathsArg.push_back(arg);
    }
  }
#ifndef _WIN32
  SingleInstanceGuard instanceGuard;
  if (!allowMultiInstance) {
    fs::path lockPath = "/tmp/deckboy-native.instance.lock";
    try {
      lockPath = fs::temp_directory_path() / "deckboy-native.instance.lock";
    } catch (...) {
    }
    if (!instanceGuard.acquire(lockPath)) {
      std::cerr << "Refusing to launch another instance (" << lockPath.string() << "): "
                << instanceGuard.lastError() << '\n';
      std::cerr << "Use --allow-multi-instance to bypass this safety lock.\n";
      return 2;
    }
  }
#endif

  App app;
  if (!app.init()) {
    app.shutdown();
    return 1;
  }

  if (soakMinutes > 0.0) {
    app.enableSoakMode(soakMinutes);
  }
  if (!startupProjectArg.empty()) {
    app.openProjectFromCommandLine(startupProjectArg);
  }
  for (const std::string& importPath : importPathsArg) {
    app.debugImportPath(importPath);
  }
  if (openSettingsTab >= 0) {
    app.debugOpenSettings(openSettingsTab, openSettingsSubTab);
  }
  if (inspectorScrollArg >= 0) {
    app.debugScrollInspector(inspectorScrollArg);
  }
  app.run();
  app.shutdown();
  return 0;
}

int main(int argc, char** argv) {
  return runDeckboyMain(argc, argv);
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  auto argvStorage = windowsCommandLineArgsUtf8();
  if (argvStorage.empty()) {
    argvStorage.emplace_back("Deckboy");
  }

  std::vector<char*> argv;
  argv.reserve(argvStorage.size());
  for (auto& arg : argvStorage) {
    argv.push_back(arg.data());
  }
  return runDeckboyMain(static_cast<int>(argv.size()), argv.data());
}
#endif
