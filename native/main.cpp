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
//          app_render_inspector.ipp — cue inspector panel rendering
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
#include "platform/capture_backend.hpp"
#include "platform/dynamic_library.hpp"
#include "platform/ltc_api.hpp"
#include "platform/ndi_api.hpp"
#include "platform/ndi_trigger_api.hpp"
#include "platform/network.hpp"
#include "platform/integration_backend.hpp"
#include "platform/output_backend.hpp"
#include "platform/browser.hpp"
#include "platform/decklink.hpp"
#include "platform/siphon_spout.hpp"
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
#include <shellapi.h>
#include <objbase.h>
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
    case CueKind::Video:
    default:                  return "Video";
  }
}

// Returns the serialization token for a CueKind (used in .deckboy project files).
std::string cueKindToken(CueKind kind) {
  switch (kind) {
    case CueKind::Image:      return "image";
    case CueKind::Pattern:    return "pattern";
    case CueKind::Browser:    return "browser";
    case CueKind::WindowSource: return "window_source";
    case CueKind::Camera:     return "camera";
    case CueKind::Syphon:     return "syphon";
    case CueKind::SrtStream:  return "srt_stream";
    case CueKind::NdiSource:  return "ndi_source";
    case CueKind::Pip:        return "pip";
    case CueKind::LowerThird: return "lower_third";
    case CueKind::Composite:  return "composite";
    case CueKind::Audio:      return "audio";
    case CueKind::Video:
    default:                  return "video";
  }
}

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
  std::vector<std::uint8_t> videoBytes;       // Raw RGBA pixel data
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
  int audioPipeFd = -1;             // Pipe fd to ffmpeg audio stdin
  bool stop = false;                // Signal the writer thread to exit
  bool failed = false;              // Writer encountered a fatal error
  std::string failureReason;
  bool hasPendingPacket = false;    // A new packet is ready to write
  OutputStreamPacket pendingPacket;
  std::uint64_t packetsQueued = 0;  // Total packets queued by main thread
  std::uint64_t packetsWritten = 0; // Total packets written by writer thread
  std::uint64_t videoBytesWritten = 0;
  std::uint64_t audioBytesWritten = 0;
};

// Per-output runtime state: SDL window/renderer, compositor, stream writer,
// NDI sender, DeckLink output, and FPS telemetry.
struct OutputRuntime {
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
  bool recoveryPausedByEscape = false;
  bool fullscreenIntended = false;  // user explicitly wants fullscreen — re-assert if dropped
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
  return "srt";
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
  if (normalizeOutputStreamProtocol(protocol) == "rtmp") {
    return "rtmp://127.0.0.1/live/output" + std::to_string(normalizedIndex);
  }
  return "srt://127.0.0.1:9000?mode=caller&transtype=live";
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
  static const std::array<std::string, 9> kImageExts {
    ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", ".tif", ".tiff", ".avif"
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
  output << "ui_sounds\t" << (project.uiSoundsEnabled ? 1 : 0) << '\n';
  output << "ui_transitions\t" << (project.uiTransitionsEnabled ? 1 : 0) << '\n';
  output << "splash_character\t" << escapeField(project.splashCharacter) << '\n';
  output << "theme\t" << escapeField(project.theme) << '\n';
  output << "geometry_aspect_link\t" << (project.geometryAspectLinked ? 1 : 0) << '\n';
  output << "ui_scale\t" << project.uiScale << '\n';
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
      << (deck.playlistDefaultTransitionToNext ? 1 : 0)
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
        << '\n';
    }
  }

  return true;
}

Project loadProject(const fs::path& projectFile) {
  Project project;
  fs::path resolved = projectFile.empty() ? Paths::defaultProjectFile() : projectFile;
  std::ifstream input(resolved, std::ios::binary);
  if (!input) {
    return project;
  }

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
    } else if (fields[0] == "splash_character") {
      std::string v = safeString(fields, 1);
      project.splashCharacter = v.empty() ? std::string("deckbot") : v;
    } else if (fields[0] == "theme") {
      project.theme = safeString(fields, 1);
    } else if (fields[0] == "geometry_aspect_link") {
      project.geometryAspectLinked = safeBool(fields, 1, true);
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
        CueKind::Video;
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
  fs::path canonical = fs::weakly_canonical(path, ec);
  if (!ec && !canonical.empty()) {
    return canonical;
  }
  fs::path absolute = fs::absolute(path, ec);
  if (!ec && !absolute.empty()) {
    return absolute;
  }
  return path;
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
    if (samples.size() > 4000u * 600u) break; // cap at 10 min
  }
#else
  ssize_t bytesRead = 0;
  while ((bytesRead = ::read(proc.readFd, buf, sizeof(buf))) > 0) {
    size_t sampleCount = static_cast<size_t>(bytesRead) / sizeof(int16_t);
    samples.insert(samples.end(), buf, buf + sampleCount);
    if (samples.size() > 4000u * 600u) break; // cap at 10 min
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
  for (int b = 0; b < numBuckets; ++b) {
    size_t startFrame = static_cast<size_t>(b) * perBucket;
    size_t endFrame = std::min(startFrame + perBucket, frameCount);
    float mxL = 0.0f;
    float mxR = 0.0f;
    for (size_t frame = startFrame; frame < endFrame; ++frame) {
      size_t sampleIndex = frame * 2u;
      mxL = std::max(mxL, std::abs(samples[sampleIndex + 0]) / 32768.0f);
      mxR = std::max(mxR, std::abs(samples[sampleIndex + 1]) / 32768.0f);
    }
    peaks.left[static_cast<size_t>(b)] = mxL;
    peaks.right[static_cast<size_t>(b)] = mxR;
  }
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

    controlWindow_ = SDL_CreateWindow(
      kAppTitle.data(),
      kControlWidth,
      kControlHeight,
      SDL_WINDOW_RESIZABLE
    );
    if (!controlWindow_) {
      std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    SDL_SetWindowPosition(controlWindow_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    applyDeckboyWindowIcon(controlWindow_);
    SDL_SetWindowMinimumSize(controlWindow_, 1500, 900);

    controlRenderer_ = SDL_CreateRenderer(controlWindow_, nullptr);
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
      monitorsRenderer_ = SDL_CreateRenderer(monitorsWindow_, nullptr);
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
    project_ = loadProject(currentProjectFile_);
    normalizeProject(project_);
    // Project may override the boot-time splash character (deckbot default).
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
    layoutButtons(kControlWidth, kControlHeight);
    return true;
  }

  void shutdown() {
    stopIntegrationBridges();
    stopHyperDeckServer();
    stopMidiInput();
    stopOscQueryServer();
    stopCompanionControl();
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

  void drainPickers() {
    if (pendingImport_.valid() &&
        pendingImport_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      auto paths = pendingImport_.get();
      if (!paths.empty()) {
        importPaths(paths);
      }
    }
    if (pendingProjectOpen_.valid() &&
        pendingProjectOpen_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      auto path = pendingProjectOpen_.get();
      if (path) {
        openProjectFromPath(*path);
      }
    }
    if (pendingProjectSaveAs_.valid() &&
        pendingProjectSaveAs_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      auto path = pendingProjectSaveAs_.get();
      if (path) {
        currentProjectFile_ = *path;
        saveProjectNow(true);
      }
    }
    if (pendingProjectBundleExport_.valid() &&
        pendingProjectBundleExport_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      auto path = pendingProjectBundleExport_.get();
      if (path) {
        exportProjectBundleTo(*path, true);
      }
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
  // Render: cue inspector panel (properties, waveform, geometry)
#include "app/app_render_inspector.ipp"
  // Render: main control window layout and drawing
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
                        bool valueEditable = false, QuickAction valueAction = QuickAction::ToggleLoop) {
    constexpr int kBtnW = 28;
    int gap = ix.ellipsize ? 8 : 6;
    int rx = ix.ctrl.x + ix.inset;
    int contentW = ix.ctrlW - ix.inset * 2;

    if (isToggle) {
      SDL_Rect btn {rx, rowY, contentW, ix.rowH};
      SDL_Color fill = toggleOn ? pal.dark : pal.light;
      SDL_Color ink  = toggleOn ? pal.light : pal.deep;
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

      drawTextSafe(controlRenderer_, ix.labelFont, labelRect, label, pal.deep);
      drawUIPanel(decBtn, pal.light, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, decBtn, "-", pal.deep);
      quickButtons_.push_back({decBtn, decAction, tip});

      drawUIPanel(valRect, pal.mid, pal.deep, pal.light);
      std::string displayValue = ix.ellipsize
        ? ellipsizeToPixelWidth(ix.valueFont, value, valRect.w - 12) : value;
      drawCenteredTextSafe(controlRenderer_, ix.valueFont, valRect, displayValue, pal.deep);
      if (valueEditable) {
        std::string valueTip = tip.empty()
          ? "Drag value to scrub | click to type an exact number"
          : tip + " | drag value to scrub, click to type exact";
        quickButtons_.push_back({valRect, valueAction, valueTip});
      }
      // Every dec/inc row's value cell is scrubbable; the zone is checked
      // before quickButtons_ on mouse-down so a click without drag still
      // opens the exact-entry editor (valueAction) on release.
      if (decAction != incAction) {
        valueScrubZones_.push_back({valRect, decAction, incAction,
                                    valueEditable ? valueAction : QuickAction::ToggleLoop,
                                    valueEditable});
      }

      drawUIPanel(incBtn, pal.light, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, incBtn, "+", pal.deep);
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
    SDL_Color fill = open ? pal.mid : pal.light;
    SDL_Color ink = open ? pal.deep : pal.dark;
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
    SDL_Rect snapped = snapRectToGrid(rect);
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
      else if (key == "button_bezel")    kButtonBezelColor   = c;
      else if (key == "delete_bezel")    kDeleteBezelColor   = c;
      else if (key == "scanline_alpha") {
        try { pal.scanlineAlpha = static_cast<Uint8>(std::clamp(std::stoi(val), 0, 255)); }
        catch (...) { pal.scanlineAlpha = 18; }
      }
    }
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
    fontLarge_      = TTF_OpenFont(sans.c_str(),  pt(32));
    fontBase_       = TTF_OpenFont(sans.c_str(),  pt(21));
    fontSmall_      = TTF_OpenFont(sans.c_str(),  pt(17));
    fontMono_       = TTF_OpenFont(mono.c_str(),  pt(18));
    fontPixel_      = TTF_OpenFont(pixel.c_str(), pt(24));
    fontPixelSmall_ = TTF_OpenFont(pixel.c_str(), pt(12));
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
  void refreshSplashAsset() {
    if (!uiPackAvailable_) {
      return;
    }
    fs::path chosen;
    std::error_code ec;
    // In the DEFAULT (gameboy) theme, boot the branded DECKBOY-wordmark splash.
    // Other themes cycle the grayscale scene pool, tinted to the colorway.
    // (currentThemeName_ once a theme is loaded, else the project's saved theme.)
    std::string activeTheme = !currentThemeName_.empty() ? currentThemeName_ : project_.theme;
    bool defaultTheme = activeTheme.empty() || activeTheme == "gameboy";
    fs::path cycleDir = uiPackRoot_ / "splash" / "cycle";
    std::vector<fs::path> pool;
    if (!defaultTheme && fs::is_directory(cycleDir, ec)) {
      for (const auto& e : fs::directory_iterator(cycleDir, ec)) {
        if (e.is_regular_file() && e.path().extension() == ".png") {
          pool.push_back(e.path());
        }
      }
    }
    splashTintable_ = !pool.empty();
    if (splashTintable_) {
      std::sort(pool.begin(), pool.end());
      size_t idx = static_cast<size_t>(SDL_GetPerformanceCounter() % pool.size());
      chosen = pool[idx];
    } else {
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
  static constexpr int kSettingsActionOutputAoiLInc  = 615;
  static constexpr int kSettingsActionOutputAoiLDec  = 616;
  static constexpr int kSettingsActionOutputAoiRInc  = 617;
  static constexpr int kSettingsActionOutputAoiRDec  = 618;
  static constexpr int kSettingsActionOutputAoiTInc  = 619;
  static constexpr int kSettingsActionOutputAoiTDec  = 620;
  static constexpr int kSettingsActionOutputAoiBInc  = 621;
  static constexpr int kSettingsActionOutputAoiBDec  = 622;
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
  TTF_Font* fontPixelSmall_ = nullptr;  // smaller pixel font for UI labels
  SDL_AudioStream* uiAudioStream_ = nullptr;
  fs::path currentProjectFile_;
  std::string currentThemeName_;
  Project project_;
  std::vector<DeckRuntime> deckRuntimes_;
  std::vector<OutputRuntime> outputRuntimes_;
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
  std::optional<Cue> cueSettingsClipboard_;
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
  bool splashTintable_ = false;  // true when the splash is a grayscale cycle master (tint to theme)
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
  int cueSettingsScrollMax_ = 0;
  SDL_Rect settingsVideoViewport_ {};
  int settingsVideoScroll_ = 0;
  int settingsVideoScrollMax_ = 0;
  bool cueSectionPlaybackOpen_ = true;
  bool cueSectionMetadataOpen_ = true;
  bool cueSectionGeometryOpen_ = true;
  bool cueSectionKeyOpen_ = false;
  bool cueSectionRoutingOpen_ = true;
  struct TimelineStripCacheEntry {
    DecodedFrame frame;
    int readyTiles = 0;
  };
  static constexpr size_t kThumbnailCacheLimit = 24;
  static constexpr size_t kTimelineStripCacheLimit = 24;
  static constexpr int kTimelineStripThumbCount = 5;
  static constexpr int kTimelineStripThumbW = 128;
  static constexpr int kTimelineStripThumbH = 72;
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
  std::unordered_map<std::string, PipOverlayRuntime> pipOverlayRuntimes_;
  std::vector<int> deckScrolls_;
  std::vector<int> deckScrollMax_;                  // per-deck clamp bound, set at render
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
  std::future<std::vector<std::string>> pendingImport_;
  std::future<std::optional<fs::path>> pendingProjectOpen_;
  std::future<std::optional<fs::path>> pendingProjectSaveAs_;
  std::future<std::optional<fs::path>> pendingProjectBundleExport_;
  DragState drag_;
  enum class TrimDragMode { None, In, Out };
  enum class LayoutDragMode { None, Playlist, Inspector };
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
  Uint64 selectionChangedAt_ = 0;
  bool uiProfileEnabled_ = false;
  double lastUiLayoutMs_ = 0.0;
  double lastUiRenderMs_ = 0.0;
  Uint64 lastUpdateTickMs_ = 0;
  Uint64 lastDisplayPollMs_ = 0;
  Uint64 lastOutputRecoveryPollMs_ = 0;
  Uint64 lastEscapeKeyMs_ = 0;
  int escapePressStreak_ = 0;
  int observedDisplayCount_ = -1;
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
  std::deque<std::string> remoteCommands_;
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
  struct ConversionJob {
    int deckIndex;
    std::string sourcePath;
    std::string destPath;
    std::future<bool> future;
  };
  std::vector<ConversionJob> conversionJobs_;
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

// Shared entry point for both main() and WinMain(). Handles:
//   --version       → print version and exit
//   --self-check    → run self-check diagnostics and exit
//   --smoke         → run smoke tests and exit
//   --decode-bench <file> [seconds] [cli] → measure decode throughput and exit
//   --pattern-dump <pattern-id> <out.ppm> [WxH] [t] → render a pattern frame and exit
//   --no-inproc-decode → force the ffmpeg CLI decode path (break-glass)
//   --allow-multi-instance → skip single-instance lock
// Otherwise: acquire instance lock → App::init() → App::run() → App::shutdown()
int runDeckboyMain(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--version") {
    printDeckboyVersion(std::cout);
    return 0;
  }
  if (argc > 1 && std::string_view(argv[1]) == "--self-check") {
    return App::runSelfCheck();
  }
  if (argc > 1 && std::string_view(argv[1]) == "--smoke") {
    return App::runSmoke();
  }
  if (argc > 3 && std::string_view(argv[1]) == "--pattern-dump") {
    int dumpW = 1280;
    int dumpH = 720;
    double dumpT = 30.0;
    for (int i = 4; i < argc; ++i) {
      std::string_view arg(argv[i]);
      auto xPos = arg.find('x');
      if (xPos != std::string_view::npos) {
        int w = std::atoi(std::string(arg.substr(0, xPos)).c_str());
        int h = std::atoi(std::string(arg.substr(xPos + 1)).c_str());
        if (w > 0 && h > 0) {
          dumpW = w;
          dumpH = h;
        }
      } else {
        double parsed = std::atof(argv[i]);
        if (parsed >= 0.0) {
          dumpT = parsed;
        }
      }
    }
    return App::runPatternDump(argv[2], argv[3], dumpW, dumpH, dumpT);
  }
  if (argc > 2 && std::string_view(argv[1]) == "--decode-bench") {
    double benchSeconds = 10.0;
    bool forceCli = false;
    for (int i = 3; i < argc; ++i) {
      std::string_view arg(argv[i]);
      if (arg == "cli") {
        forceCli = true;
      } else {
        double parsed = std::atof(argv[i]);
        if (parsed > 0.0) {
          benchSeconds = parsed;
        }
      }
    }
    return App::runDecodeBench(argv[2], benchSeconds, forceCli);
  }

  bool allowMultiInstance = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--allow-multi-instance") {
      allowMultiInstance = true;
    }
    if (std::string_view(argv[i]) == "--no-inproc-decode") {
      // Operator break-glass: keep every decode on the ffmpeg CLI pipe path
      // for this run (robustness over Pocket performance).
      MediaEngine::setInprocDecodeDisabled(true);
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
