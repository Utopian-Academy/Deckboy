// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// mini_hud.hpp — Deckboy Mini's terminal face.
//
// In a real terminal: a small LCD-green status panel redrawn in place, a strip
// of text symbols that shimmers with the sound, and a short event log. Piped,
// run as a service, NO_COLOR, TERM=dumb, too narrow, or --plain: one plain
// line per event, nothing else, which is what a log file wants.
//
// Every non-ASCII character is written as UTF-8 byte escapes with its name
// beside it. Literal arrows once went into this codebase double-encoded and
// drew as mojibake, and nothing but a screenshot caught it.
// ============================================================================

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace mini {

// ── Glyphs ─────────────────────────────────────────────────────────────────
namespace glyph {
constexpr const char* kTopLeft = "\xE2\x95\xAD";      // U+256D rounded corner
constexpr const char* kTopRight = "\xE2\x95\xAE";     // U+256E
constexpr const char* kBottomLeft = "\xE2\x95\xB0";   // U+2570
constexpr const char* kBottomRight = "\xE2\x95\xAF";  // U+256F
constexpr const char* kHorizontal = "\xE2\x94\x80";   // U+2500 box horizontal
constexpr const char* kVertical = "\xE2\x94\x82";     // U+2502 box vertical
constexpr const char* kFull = "\xE2\x96\x93";         // U+2593 dark shade
constexpr const char* kEmpty = "\xE2\x96\x91";        // U+2591 light shade
constexpr const char* kPip = "\xE2\x96\xAE";          // U+25AE black vertical rectangle
constexpr const char* kPipOff = "\xE2\x96\xAF";       // U+25AF white vertical rectangle
constexpr const char* kPlay = "\xE2\x96\xB6";         // U+25B6 play triangle
constexpr const char* kPause = "\xE2\x9D\x9A";        // U+275A heavy vertical bar
constexpr const char* kStop = "\xE2\x96\xA0";         // U+25A0 black square
constexpr const char* kNext = "\xE2\x96\xB8";         // U+25B8 small right triangle
constexpr const char* kLog = "\xC2\xBB";              // U+00BB right guillemet
constexpr const char* kSpark = "\xE2\x9C\xA6";        // U+2726 four-pointed star
constexpr const char* kDot = "\xC2\xB7";              // U+00B7 middle dot
constexpr const char* kRing = "\xCB\x9A";             // U+02DA ring above
constexpr const char* kLogo = "\xE2\x96\x97\xE2\x96\x84\xE2\x96\x96";  // U+2597 U+2584 U+2596 tiny cartridge
}  // namespace glyph

// Game Boy LCD greens, darkest to lightest.
namespace ink {
constexpr const char* kDeep = "\x1b[38;2;15;56;15m";
constexpr const char* kDim = "\x1b[38;2;48;98;48m";
constexpr const char* kMid = "\x1b[38;2;139;172;15m";
constexpr const char* kBright = "\x1b[38;2;155;188;15m";
constexpr const char* kWarn = "\x1b[38;2;230;180;60m";
constexpr const char* kBold = "\x1b[1m";
constexpr const char* kReset = "\x1b[0m";
}  // namespace ink

struct HudState {
  std::string version;
  std::string status = "Stopped";  // Playing / Paused / Stopped
  int cue = 0;                     // 1-based; 0 = nothing live
  int cueCount = 0;
  std::string cueName;
  int next = 0;
  std::string nextName;
  double position = 0.0;
  double duration = 0.0;
  int volume = 100;
  bool loop = false;
  bool blackout = false;
  int display = 1;
  int width = 0;
  int height = 0;
  bool fullscreen = true;
  int port = 0;
  bool listening = false;
  bool network = false;
  int controllers = 0;
  double audioLevel = 0.0;  // 0..1
};

class Hud {
 public:
  // Decides once, at start, whether this terminal gets the panel.
  void begin(bool forcePlain) {
    fancy_ = !forcePlain && wantsPanel();
    if (fancy_) std::fputs("\x1b[?25l", stdout);  // hide the cursor while we own it
    start_ = std::chrono::steady_clock::now();
  }

  bool fancy() const { return fancy_; }

  // A boot sequence in the desk's voice: real values, a little nonsense, gone
  // in under a second. Plain mode prints the real values only.
  void boot(const std::vector<std::pair<std::string, std::string>>& facts) {
    if (!fancy_) {
      for (const auto& f : facts) line(f.first + ": " + f.second);
      return;
    }
    std::printf("\n  %s%s%s %sDECKBOY MINI%s %s%s booting%s\n", ink::kBright, glyph::kLogo, ink::kReset,
                kTitleInk, ink::kReset, ink::kDim, glyph::kNext, ink::kReset);
    std::vector<std::pair<std::string, std::string>> lines = facts;
    lines.insert(lines.begin() + std::min<std::size_t>(lines.size(), 2), {"flux capacitor", "charged"});
    lines.push_back({"gremlin containment", "nominal"});
    for (const auto& [label, value] : lines) {
      std::string dots(std::max<int>(3, 22 - static_cast<int>(label.size())), '.');
      std::printf("  %s%s%s %s %s%s%s %s%s%s\n", ink::kDim, glyph::kLog, ink::kReset, label.c_str(), ink::kDeep,
                  dots.c_str(), ink::kReset, ink::kMid, value.c_str(), ink::kReset);
      std::fflush(stdout);
      std::this_thread::sleep_for(std::chrono::milliseconds(70));
    }
    std::printf("  %s%s ready.%s\n\n", ink::kBright, glyph::kSpark, ink::kReset);
    std::fflush(stdout);
  }

  // An event: the log feed in the panel, or one plain line.
  void log(const std::string& text) {
    if (!fancy_) { line(text); return; }
    log_.push_back(text);
    while (log_.size() > kLogLines) log_.pop_front();
    dirty_ = true;
  }

  // Called every frame; redraws about ten times a second, or at once after a
  // log line, so it never costs the picture anything.
  void frame(const HudState& s) {
    if (!fancy_) return;
    const auto now = std::chrono::steady_clock::now();
    if (!dirty_ && now - lastDraw_ < std::chrono::milliseconds(100)) return;
    lastDraw_ = now;
    dirty_ = false;
    draw(s, std::chrono::duration<double>(now - start_).count());
  }

  void end() {
    if (fancy_) std::printf("%s\x1b[?25h\n", ink::kReset);  // give the cursor back
    std::fflush(stdout);
  }

 private:
  // Bold WITH a colour: bold on its own takes whatever the terminal's theme
  // gives bold text, which on one operator's theme was magenta.
  static constexpr const char* kTitleInk = "\x1b[1;38;2;155;188;15m";
  static constexpr std::size_t kLogLines = 4;
  static constexpr int kInner = 58;  // characters between the box sides

  static void line(const std::string& text) {
    std::fputs(text.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
  }

  static int terminalColumns() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
      return info.srWindow.Right - info.srWindow.Left + 1;
    }
    return 0;
#else
    winsize ws {};
    return ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 ? ws.ws_col : 0;
#endif
  }

  static bool wantsPanel() {
    if (std::getenv("NO_COLOR")) return false;
    if (const char* term = std::getenv("TERM"); term && std::string(term) == "dumb") return false;
#ifdef _WIN32
    if (!_isatty(_fileno(stdout))) return false;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(out, &mode)) return false;
    if (!SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return false;
    SetConsoleOutputCP(CP_UTF8);
#else
    if (!isatty(STDOUT_FILENO)) return false;
#endif
    return terminalColumns() >= kInner + 6;
  }

  // Visible width of a UTF-8 string: count every byte that does not continue a
  // character. Every glyph here is one column wide.
  static int columns(const std::string& s) {
    int n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
  }

  static std::string fit(const std::string& s, int width) {
    if (columns(s) <= width) return s + std::string(static_cast<std::size_t>(width - columns(s)), ' ');
    std::string out;
    int n = 0;
    for (std::size_t i = 0; i < s.size() && n < width - 1; ++i) {
      out += s[i];
      if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
        // copy the rest of this character's bytes before counting the next
        while (i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) out += s[++i];
        ++n;
      }
    }
    return out + "~";
  }

  static std::string clock(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return "--:--.-";
    const int tenths = static_cast<int>(std::floor(seconds * 10.0));
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02d:%02d.%d", tenths / 600, (tenths / 10) % 60, tenths % 10);
    return buf;
  }

  static std::string repeat(const char* g, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += g;
    return out;
  }

  // One row of the box: the coloured content is passed with its visible width
  // so the right edge stays put however many escape codes it carries.
  static void row(const std::string& content, int visible) {
    std::printf("\x1b[2K  %s%s%s %s%s %s%s%s\n", ink::kDim, glyph::kVertical, ink::kReset, content.c_str(),
                std::string(static_cast<std::size_t>(std::max(0, kInner - 2 - visible)), ' ').c_str(),
                ink::kDim, glyph::kVertical, ink::kReset);
  }

  void draw(const HudState& s, double t) {
    // Back to the top of the panel, then repaint it in place.
    if (drawnLines_ > 0) std::printf("\x1b[%dF", drawnLines_);
    int lines = 0;

    // Header
    std::string title = std::string("DECKBOY MINI ");
    std::string tail = " v" + s.version;
    int dots = kInner + 2 - columns(title) - columns(tail) - 4;
    std::printf("\x1b[2K  %s%s%s %s%s%s%s%s%s%s%s\n", ink::kBright, glyph::kLogo, ink::kReset, kTitleInk,
                title.c_str(), ink::kReset, ink::kDeep, repeat(glyph::kDot, std::max(3, dots)).c_str(), ink::kReset,
                ink::kDim, tail.c_str());
    std::printf("%s", ink::kReset);
    ++lines;

    std::printf("\x1b[2K  %s%s%s%s%s\n", ink::kDim, glyph::kTopLeft, repeat(glyph::kHorizontal, kInner).c_str(),
                glyph::kTopRight, ink::kReset);
    ++lines;

    // Transport
    const char* icon = s.status == "Playing" ? glyph::kPlay : s.status == "Paused" ? glyph::kPause : glyph::kStop;
    const char* tone = s.status == "Playing" ? ink::kBright : s.status == "Paused" ? ink::kMid : ink::kDim;
    std::string state = s.blackout ? "BLACKOUT" : s.status == "Playing" ? "PLAYING" : s.status == "Paused" ? "PAUSED" : "STOPPED";
    std::string cue = s.cue > 0 ? "CUE " + std::to_string(s.cue) + "/" + std::to_string(s.cueCount) : "CUE -/" + std::to_string(s.cueCount);
    std::string name = fit(s.cue > 0 ? s.cueName : std::string("(nothing live)"), kInner - 28);
    std::string a = std::string(s.blackout ? ink::kWarn : tone) + icon + " " + fit(state, 9) + ink::kReset + " " +
                    fit(cue, 11) + " " + kTitleInk + name + ink::kReset;
    row(a, 2 + 9 + 1 + 11 + 1 + columns(name));
    ++lines;

    // Progress
    const int barWidth = 26;
    const double frac = s.duration > 0.0 ? std::clamp(s.position / s.duration, 0.0, 1.0) : 0.0;
    const int filled = s.cue > 0 ? static_cast<int>(std::round(frac * barWidth)) : 0;
    std::string times = clock(s.cue > 0 ? s.position : -1) + " / " + (s.duration > 0.0 ? clock(s.duration) : std::string("--:--.-"));
    std::string b = std::string(ink::kMid) + repeat(glyph::kFull, filled) + ink::kDeep + repeat(glyph::kEmpty, barWidth - filled) +
                    ink::kReset + "  " + times;
    row(b, barWidth + 2 + columns(times));
    ++lines;

    // Next
    std::string next = s.next > 0 ? std::to_string(s.next) + "  " + fit(s.nextName, kInner - 14) : std::string(s.loop ? "back to 1" : "end of list");
    std::string c = std::string(ink::kDim) + "NEXT " + glyph::kNext + ink::kReset + " " + next;
    row(c, 7 + columns(next));
    ++lines;

    // Volume, loop, output
    const int pips = static_cast<int>(std::round(s.volume / 10.0));
    std::string out = "OUT " + std::to_string(s.display) + (s.width > 0 ? " " + std::to_string(s.width) + "x" + std::to_string(s.height) : std::string()) +
                      (s.fullscreen ? "" : " window");
    std::string volText = " " + std::to_string(s.volume);
    std::string d = std::string(ink::kDim) + "VOL " + ink::kMid + repeat(glyph::kPip, pips) + ink::kDeep + repeat(glyph::kPipOff, 10 - pips) +
                    ink::kReset + fit(volText, 5) + ink::kDim + "LOOP " + ink::kReset + (s.loop ? "on " : "off") + "  " + ink::kDim + out + ink::kReset;
    row(d, 4 + 10 + 5 + 5 + 3 + 2 + columns(out));
    ++lines;

    // Link
    std::string link = s.listening
      ? ":" + std::to_string(s.port) + (s.network ? " network" : " this machine") + " " + glyph::kDot + " " +
        std::to_string(s.controllers) + (s.controllers == 1 ? " controller" : " controllers")
      : std::string("off (port ") + std::to_string(s.port) + " busy)";
    std::string e = std::string(ink::kDim) + "LINK " + ink::kReset + link;
    row(e, 5 + columns(link));
    ++lines;

    std::printf("\x1b[2K  %s%s%s%s%s\n", ink::kDim, glyph::kBottomLeft, repeat(glyph::kHorizontal, kInner).c_str(),
                glyph::kBottomRight, ink::kReset);
    ++lines;

    // The shimmer: a ribbon of symbols whose height follows the sound, with a
    // slow idle drift so a silent picture still looks alive.
    static const char kRamp[] = " .:-=+*#%@";
    const int width = 40;
    std::string ribbon;
    const double level = std::clamp(s.audioLevel * 1.6, 0.0, 1.0);
    for (int i = 0; i < width; ++i) {
      const double centre = 1.0 - std::abs(i - width / 2.0) / (width / 2.0);
      const double wave = 0.5 + 0.5 * std::sin(t * 5.0 + i * 0.55) * std::cos(t * 1.7 - i * 0.21);
      const double idle = s.status == "Playing" ? 0.18 : 0.08;
      const double v = std::clamp((idle + level * centre) * wave + idle * 0.5 * centre, 0.0, 0.999);
      ribbon += kRamp[static_cast<int>(v * 10)];
    }
    const bool twinkle = std::fmod(t, 1.4) < 0.7;
    std::printf("\x1b[2K   %s%s %s  %s%s%s%s  %s %s%s\n", ink::kDim, twinkle ? glyph::kRing : glyph::kDot,
                twinkle ? glyph::kDot : glyph::kSpark, ink::kMid, ribbon.c_str(), ink::kReset, ink::kDim,
                twinkle ? glyph::kSpark : glyph::kDot, twinkle ? glyph::kDot : glyph::kRing, ink::kReset);
    ++lines;

    // The log feed, newest last, padded so the panel never changes height.
    for (std::size_t i = 0; i < kLogLines; ++i) {
      const std::size_t pad = kLogLines - log_.size();
      if (i < pad) { std::printf("\x1b[2K\n"); ++lines; continue; }
      const std::string& entry = log_[i - pad];
      const bool latest = i == kLogLines - 1;
      std::printf("\x1b[2K  %s%s%s %s%s\n", ink::kDim, glyph::kLog, latest ? ink::kBright : ink::kMid,
                  fit(entry, kInner).c_str(), ink::kReset);
      ++lines;
    }
    std::fflush(stdout);
    drawnLines_ = lines;
  }

  bool fancy_ = false;
  bool dirty_ = true;
  int drawnLines_ = 0;
  std::deque<std::string> log_;
  std::chrono::steady_clock::time_point start_;
  std::chrono::steady_clock::time_point lastDraw_;
};

}  // namespace mini
