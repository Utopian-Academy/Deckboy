// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// system_browser.hpp — Open a URL in the operator's default browser.
//
// Used by the dependency prompt to send the operator to the official vendor
// download page (NDI Tools, Blackmagic Desktop Video, WebView2 Runtime).
// We do NOT auto-download installers — the vendor's own page tracks the
// current build, signs it, and lays out the install instructions. Pointing
// at the official page keeps Deckboy out of the middle of that chain.
//
// Header-only. No .cpp counterpart.
// ============================================================================

#pragma once

#include <string>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <shellapi.h>
#else
  #include <cstdlib>
#endif

namespace deckboy::platform {

// Launch the operator's default browser pointed at `url`. Returns true on
// best-effort success — we cannot tell whether the user actually saw the
// page, only that the OS handed off the URL. Silently no-ops on an empty
// URL so callers can use it unconditionally.
inline bool openExternalUrl(const std::string& url) {
  if (url.empty()) {
    return false;
  }
#if defined(_WIN32)
  // ShellExecuteW with a UTF-16 URL hands off to the registered HTTP handler
  // (typically Edge, Chrome, or Firefox). SW_SHOWNORMAL is the default
  // window state — same as launching the browser by hand.
  int wlen = ::MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
  if (wlen <= 0) {
    return false;
  }
  std::wstring wurl(wlen, L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], wlen);
  HINSTANCE result = ::ShellExecuteW(nullptr, L"open", wurl.c_str(),
                                      nullptr, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
#elif defined(__APPLE__)
  std::string cmd = std::string("open \"") + url + "\" >/dev/null 2>&1 &";
  return std::system(cmd.c_str()) == 0;
#else
  std::string cmd = std::string("xdg-open \"") + url + "\" >/dev/null 2>&1 &";
  return std::system(cmd.c_str()) == 0;
#endif
}

// Reveal a file in the OS file manager (Explorer / Finder / whatever handles
// the folder on Linux), selecting it where the platform supports selection.
// Best-effort like openExternalUrl. No-ops on an empty path.
inline bool revealFileInFileManager(const std::string& path) {
  if (path.empty()) {
    return false;
  }
#if defined(_WIN32)
  // explorer.exe /select,"C:\path\file.mp4" opens the parent folder with the
  // file highlighted. Backslashes required — forward slashes make Explorer
  // fall back to the Documents folder.
  std::string native = path;
  for (char& c : native) {
    if (c == '/') c = '\\';
  }
  std::string args = "/select,\"" + native + "\"";
  int wlen = ::MultiByteToWideChar(CP_UTF8, 0, args.c_str(), -1, nullptr, 0);
  if (wlen <= 0) {
    return false;
  }
  std::wstring wargs(wlen, L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, args.c_str(), -1, &wargs[0], wlen);
  HINSTANCE result = ::ShellExecuteW(nullptr, L"open", L"explorer.exe",
                                      wargs.c_str(), nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
#elif defined(__APPLE__)
  std::string cmd = std::string("open -R \"") + path + "\" >/dev/null 2>&1 &";
  return std::system(cmd.c_str()) == 0;
#else
  // No portable "select" on Linux; open the containing directory.
  std::string dir = path;
  auto slash = dir.find_last_of('/');
  if (slash != std::string::npos) {
    dir = dir.substr(0, slash);
  }
  std::string cmd = std::string("xdg-open \"") + dir + "\" >/dev/null 2>&1 &";
  return std::system(cmd.c_str()) == 0;
#endif
}

}  // namespace deckboy::platform
