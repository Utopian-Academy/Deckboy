// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// mini_keys.hpp — Deckboy Mini reads the keyboard from its terminal.
//
// Without this the keys only reached the video window, so a Mini running on a
// second display, or over SSH on a box with no keyboard of its own, could only
// be driven through the remote port. This puts the terminal in a raw,
// non-blocking mode for as long as Mini runs and hands back one Key at a
// time; the window's keys are mapped to the same Key, so both drive one set of
// handlers.
//
// Nothing happens unless standard input is a terminal: run as a service, or
// with input from a file, there is nothing to read and nothing is changed.
// ============================================================================

#pragma once

#include <cstdio>
#include <optional>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace mini {

struct Key {
  enum Kind { Char, Up, Down, Left, Right, Enter, Escape, Backspace, Tab, DeleteWord } kind = Char;
  char ch = 0;  // the character, for Kind::Char (printable ASCII)
};

class TerminalKeys {
 public:
  TerminalKeys() = default;
  TerminalKeys(const TerminalKeys&) = delete;
  TerminalKeys& operator=(const TerminalKeys&) = delete;
  ~TerminalKeys() { end(); }

  // True when the terminal is ours to read.
  bool begin() {
#ifdef _WIN32
    in_ = GetStdHandle(STD_INPUT_HANDLE);
    if (in_ == INVALID_HANDLE_VALUE || !_isatty(_fileno(stdin))) return false;
    if (!GetConsoleMode(in_, &savedMode_)) return false;
    // No line buffering, no echo; Ctrl+C still arrives as a signal.
    DWORD mode = (savedMode_ & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE)) |
                 ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS;
    if (!SetConsoleMode(in_, mode)) return false;
    active_ = true;
#else
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_) != 0) return false;
    termios raw = saved_;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));  // ISIG stays: Ctrl+C still quits
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return false;
    active_ = true;
#endif
    return true;
  }

  // Put the terminal back exactly as it was. Safe to call twice.
  void end() {
    if (!active_) return;
    active_ = false;
#ifdef _WIN32
    SetConsoleMode(in_, savedMode_);
#else
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
#endif
  }

  bool active() const { return active_; }

  // The next key, or nothing if none is waiting. Never blocks.
  std::optional<Key> poll() {
    if (!active_) return std::nullopt;
#ifdef _WIN32
    DWORD waiting = 0;
    while (GetNumberOfConsoleInputEvents(in_, &waiting) && waiting > 0) {
      INPUT_RECORD record;
      DWORD got = 0;
      if (!ReadConsoleInputW(in_, &record, 1, &got) || got == 0) return std::nullopt;
      if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) continue;
      const KEY_EVENT_RECORD& k = record.Event.KeyEvent;
      switch (k.wVirtualKeyCode) {
        case VK_UP: return Key {Key::Up};
        case VK_DOWN: return Key {Key::Down};
        case VK_LEFT: return Key {Key::Left};
        case VK_RIGHT: return Key {Key::Right};
        case VK_RETURN: return Key {Key::Enter};
        case VK_ESCAPE: return Key {Key::Escape};
        case VK_BACK: return Key {Key::Backspace};
        case VK_TAB: return Key {Key::Tab};
        default: break;
      }
      const wchar_t c = k.uChar.UnicodeChar;
      if (c == 0x17) return Key {Key::DeleteWord};   // Ctrl+W
      if (c >= 32 && c < 127) return Key {Key::Char, static_cast<char>(c)};
    }
    return std::nullopt;
#else
    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return std::nullopt;
    if (c == 27) {
      // An arrow arrives as ESC [ A..D. A lone ESC is the Escape key.
      unsigned char seq[2] = {0, 0};
      if (read(STDIN_FILENO, &seq[0], 1) != 1) return Key {Key::Escape};
      if ((seq[0] == '[' || seq[0] == 'O') && read(STDIN_FILENO, &seq[1], 1) == 1) {
        switch (seq[1]) {
          case 'A': return Key {Key::Up};
          case 'B': return Key {Key::Down};
          case 'C': return Key {Key::Right};
          case 'D': return Key {Key::Left};
          default: return std::nullopt;
        }
      }
      return Key {Key::Escape};
    }
    if (c == '\r' || c == '\n') return Key {Key::Enter};
    if (c == 127 || c == 8) return Key {Key::Backspace};
    if (c == '\t') return Key {Key::Tab};
    if (c == 0x17) return Key {Key::DeleteWord};   // Ctrl+W
    if (c >= 32 && c < 127) return Key {Key::Char, static_cast<char>(c)};
    return std::nullopt;
#endif
  }

 private:
  bool active_ = false;
#ifdef _WIN32
  HANDLE in_ = INVALID_HANDLE_VALUE;
  DWORD savedMode_ = 0;
#else
  termios saved_ {};
#endif
};

}  // namespace mini
