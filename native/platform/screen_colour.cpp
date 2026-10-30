// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// screen_colour.cpp -- pick from screen on Windows and Linux. The Mac's is in
// screen_colour_mac.mm. See screen_colour.hpp for what each one uses and why.

#include "platform/screen_colour.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <type_traits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__APPLE__)
#include <dlfcn.h>
#if __has_include(<X11/Xlib.h>)
#include <X11/Xlib.h>
#define DECKBOY_SCREEN_PICK_X11 1
#endif
#endif

namespace deckboy::platform {

#ifdef _WIN32

namespace {

// The hook runs on the thread that installed it, which is the main thread,
// inside SDL's message pump: no locking against poll, which runs there too.
HHOOK gHook = nullptr;
bool gPicking = false;
bool gTaken = false;
bool gCancelled = false;
bool gSwallowUp = false;   // the release of the click that took the colour
POINT gTakenAt {};

LRESULT CALLBACK pickMouseHook(int code, WPARAM message, LPARAM data) {
  if (code == HC_ACTION && gPicking) {
    const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
    // The click is the picker's, not the app's under it: a pick over a
    // browser must not also follow the link it was pointing at.
    if (message == WM_LBUTTONDOWN) {
      gTaken = true;
      gTakenAt = info->pt;
      gSwallowUp = true;
      return 1;
    }
    if (message == WM_RBUTTONDOWN) {
      gCancelled = true;
      gSwallowUp = true;
      return 1;
    }
  }
  if (code == HC_ACTION && gSwallowUp && (message == WM_LBUTTONUP || message == WM_RBUTTONUP)) {
    gSwallowUp = false;
    return 1;
  }
  return CallNextHookEx(gHook, code, message, data);
}

// The 9x9 pixels round a point, from the desktop itself: every window on
// every display, whoever drew it.
bool readPatch(POINT at, std::array<std::uint32_t, 81>& out) {
  HDC screen = GetDC(nullptr);
  if (!screen) return false;
  HDC memory = CreateCompatibleDC(screen);
  BITMAPINFO bi {};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = 9;
  bi.bmiHeader.biHeight = -9;   // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(memory, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  bool ok = false;
  if (bitmap && bits) {
    HGDIOBJ old = SelectObject(memory, bitmap);
    ok = BitBlt(memory, 0, 0, 9, 9, screen, at.x - 4, at.y - 4, SRCCOPY | CAPTUREBLT) != 0;
    SelectObject(memory, old);
    if (ok) {
      const auto* px = static_cast<const std::uint32_t*>(bits);   // 0x00RRGGBB as BGRA bytes
      for (int i = 0; i < 81; ++i) out[static_cast<std::size_t>(i)] = px[i] & 0xFFFFFF;
    }
  }
  if (bitmap) DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(nullptr, screen);
  return ok;
}

void endPick() {
  if (gHook) UnhookWindowsHookEx(gHook);
  gHook = nullptr;
  gPicking = false;
}

}  // namespace

bool beginScreenPick(std::string* why) {
  if (gPicking) return true;
  gHook = SetWindowsHookExW(WH_MOUSE_LL, pickMouseHook, GetModuleHandleW(nullptr), 0);
  if (!gHook) {
    if (why) *why = "Windows refused the mouse hook (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  gPicking = true;
  gTaken = gCancelled = false;
  return true;
}

ScreenPickFrame pollScreenPick() {
  ScreenPickFrame frame;
  if (!gPicking) return frame;
  if (gCancelled) {
    endPick();
    frame.state = ScreenPickState::Cancelled;
    return frame;
  }
  POINT at {};
  if (gTaken) {
    at = gTakenAt;
  } else if (!GetCursorPos(&at)) {
    frame.state = ScreenPickState::Picking;
    return frame;
  }
  frame.hasPatch = readPatch(at, frame.patch);
  frame.colour = frame.patch[40];
  if (gTaken) {
    endPick();
    if (!frame.hasPatch) {
      frame.state = ScreenPickState::Failed;
      frame.error = "the screen could not be read there";
    } else {
      frame.state = ScreenPickState::Picked;
    }
    return frame;
  }
  frame.state = ScreenPickState::Picking;
  return frame;
}

void cancelScreenPick() {
  if (gPicking) gCancelled = true;
}

void nudgeScreenPick(int dx, int dy) {
  POINT at {};
  if (gPicking && GetCursorPos(&at)) SetCursorPos(at.x + dx, at.y + dy);
}

void takeScreenPick() {
  POINT at {};
  if (gPicking && GetCursorPos(&at)) {
    gTaken = true;
    gTakenAt = at;
  }
}

#elif !defined(__APPLE__)

// ── Linux ────────────────────────────────────────────────────────────────
namespace {

std::mutex gMutex;
ScreenPickState gState = ScreenPickState::Idle;
bool gWayland = false;
std::uint32_t gResult = 0;
std::string gError;

// ── Wayland: the desktop portal ─────────────────────────────────────────
//
// libdbus is opened at run time, as libX11 is below. Only the handful of
// calls the portal needs, declared with the ABI libdbus has kept since 1.0.
// DBusMessageIter is a caller-allocated struct whose fields are private; a
// buffer at least as large and as aligned stands in for it.
struct DBusErr { const char* name; const char* message; unsigned bits; void* pad; };
struct alignas(8) DBusIter { unsigned char storage[128]; };
using DBusConnection = void;
using DBusMessage = void;

struct DBusApi {
  void* lib = nullptr;
  DBusConnection* (*bus_get_private)(int, DBusErr*) = nullptr;
  void (*error_init)(DBusErr*) = nullptr;
  void (*error_free)(DBusErr*) = nullptr;
  int (*threads_init_default)() = nullptr;
  DBusMessage* (*message_new_method_call)(const char*, const char*, const char*, const char*) = nullptr;
  void (*message_iter_init_append)(DBusMessage*, DBusIter*) = nullptr;
  int (*message_iter_append_basic)(DBusIter*, int, const void*) = nullptr;
  int (*message_iter_open_container)(DBusIter*, int, const char*, DBusIter*) = nullptr;
  int (*message_iter_close_container)(DBusIter*, DBusIter*) = nullptr;
  DBusMessage* (*send_with_reply_and_block)(DBusConnection*, DBusMessage*, int, DBusErr*) = nullptr;
  void (*message_unref)(DBusMessage*) = nullptr;
  int (*message_iter_init)(DBusMessage*, DBusIter*) = nullptr;
  int (*message_iter_get_arg_type)(DBusIter*) = nullptr;
  void (*message_iter_get_basic)(DBusIter*, void*) = nullptr;
  int (*message_iter_next)(DBusIter*) = nullptr;
  void (*message_iter_recurse)(DBusIter*, DBusIter*) = nullptr;
  void (*bus_add_match)(DBusConnection*, const char*, DBusErr*) = nullptr;
  int (*connection_read_write)(DBusConnection*, int) = nullptr;
  DBusMessage* (*connection_pop_message)(DBusConnection*) = nullptr;
  int (*message_is_signal)(DBusMessage*, const char*, const char*) = nullptr;
  const char* (*message_get_path)(DBusMessage*) = nullptr;
  const char* (*bus_get_unique_name)(DBusConnection*) = nullptr;
  void (*connection_close)(DBusConnection*) = nullptr;
  void (*connection_unref)(DBusConnection*) = nullptr;

  bool load() {
    if (lib) return true;
    lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    bool ok = true;
    auto get = [&](auto& fn, const char* name) {
      fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
      ok = ok && fn;
    };
    get(bus_get_private, "dbus_bus_get_private");
    get(error_init, "dbus_error_init");
    get(error_free, "dbus_error_free");
    get(threads_init_default, "dbus_threads_init_default");
    get(message_new_method_call, "dbus_message_new_method_call");
    get(message_iter_init_append, "dbus_message_iter_init_append");
    get(message_iter_append_basic, "dbus_message_iter_append_basic");
    get(message_iter_open_container, "dbus_message_iter_open_container");
    get(message_iter_close_container, "dbus_message_iter_close_container");
    get(send_with_reply_and_block, "dbus_connection_send_with_reply_and_block");
    get(message_unref, "dbus_message_unref");
    get(message_iter_init, "dbus_message_iter_init");
    get(message_iter_get_arg_type, "dbus_message_iter_get_arg_type");
    get(message_iter_get_basic, "dbus_message_iter_get_basic");
    get(message_iter_next, "dbus_message_iter_next");
    get(message_iter_recurse, "dbus_message_iter_recurse");
    get(bus_add_match, "dbus_bus_add_match");
    get(connection_read_write, "dbus_connection_read_write");
    get(connection_pop_message, "dbus_connection_pop_message");
    get(message_is_signal, "dbus_message_is_signal");
    get(message_get_path, "dbus_message_get_path");
    get(bus_get_unique_name, "dbus_bus_get_unique_name");
    get(connection_close, "dbus_connection_close");
    get(connection_unref, "dbus_connection_unref");
    if (!ok) {
      dlclose(lib);
      lib = nullptr;
    }
    return ok;
  }
};

constexpr int kDBusSession = 0;
constexpr int kTypeString = 's', kTypeArray = 'a', kTypeDictEntry = 'e', kTypeVariant = 'v',
              kTypeStruct = 'r', kTypeDouble = 'd', kTypeUint32 = 'u';

void finishPortal(ScreenPickState state, std::uint32_t colour, std::string error) {
  std::lock_guard<std::mutex> lock(gMutex);
  gState = state;
  gResult = colour;
  gError = std::move(error);
}

// Runs on its own thread with its own connection: the answer arrives when the
// operator has picked, however long that takes.
void runPortalPick() {
  static DBusApi api;
  if (!api.load()) return finishPortal(ScreenPickState::Failed, 0, "libdbus is not installed");
  api.threads_init_default();
  DBusErr err;
  api.error_init(&err);
  DBusConnection* bus = api.bus_get_private(kDBusSession, &err);
  if (!bus) {
    api.error_free(&err);
    return finishPortal(ScreenPickState::Failed, 0, "no session bus for the desktop portal");
  }
  // The request's path is known before the call (the portal's handle_token
  // contract), so the match is in place before the answer can be sent.
  std::string sender = api.bus_get_unique_name(bus);
  sender.erase(0, sender.find_first_not_of(':'));
  std::replace(sender.begin(), sender.end(), '.', '_');
  const std::string token = "deckboy" + std::to_string(std::rand() % 100000);
  const std::string path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
  api.bus_add_match(bus, ("type='signal',interface='org.freedesktop.portal.Request',member='Response',path='" +
                          path + "'").c_str(), &err);
  DBusMessage* call = api.message_new_method_call("org.freedesktop.portal.Desktop",
                                                  "/org/freedesktop/portal/desktop",
                                                  "org.freedesktop.portal.Screenshot", "PickColor");
  DBusIter args, dict, entry, variant;
  api.message_iter_init_append(call, &args);
  const char* parent = "";
  api.message_iter_append_basic(&args, kTypeString, &parent);
  api.message_iter_open_container(&args, kTypeArray, "{sv}", &dict);
  api.message_iter_open_container(&dict, kTypeDictEntry, nullptr, &entry);
  const char* key = "handle_token";
  const char* value = token.c_str();
  api.message_iter_append_basic(&entry, kTypeString, &key);
  api.message_iter_open_container(&entry, kTypeVariant, "s", &variant);
  api.message_iter_append_basic(&variant, kTypeString, &value);
  api.message_iter_close_container(&entry, &variant);
  api.message_iter_close_container(&dict, &entry);
  api.message_iter_close_container(&args, &dict);
  DBusMessage* reply = api.send_with_reply_and_block(bus, call, 5000, &err);
  api.message_unref(call);
  if (!reply) {
    const std::string why = err.message ? err.message : "the desktop portal did not answer";
    api.error_free(&err);
    api.connection_close(bus);
    api.connection_unref(bus);
    return finishPortal(ScreenPickState::Failed, 0, "desktop portal: " + why);
  }
  api.message_unref(reply);
  ScreenPickState state = ScreenPickState::Cancelled;
  std::uint32_t colour = 0;
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(gMutex);
      if (gState != ScreenPickState::Picking) break;   // cancelled from the app
    }
    if (!api.connection_read_write(bus, 200)) {
      state = ScreenPickState::Failed;
      break;
    }
    DBusMessage* message = api.connection_pop_message(bus);
    if (!message) continue;
    const char* at = api.message_get_path(message);
    if (api.message_is_signal(message, "org.freedesktop.portal.Request", "Response") && at && path == at) {
      // (u response, a{sv} results): 0 is success, and results["color"] is
      // a (ddd) in sRGB 0-1.
      DBusIter it;
      std::uint32_t code = 2;
      if (api.message_iter_init(message, &it) && api.message_iter_get_arg_type(&it) == kTypeUint32) {
        api.message_iter_get_basic(&it, &code);
      }
      if (code == 0 && api.message_iter_next(&it) && api.message_iter_get_arg_type(&it) == kTypeArray) {
        DBusIter items;
        api.message_iter_recurse(&it, &items);
        while (api.message_iter_get_arg_type(&items) == kTypeDictEntry) {
          DBusIter kv;
          api.message_iter_recurse(&items, &kv);
          const char* name = nullptr;
          api.message_iter_get_basic(&kv, &name);
          if (name && std::strcmp(name, "color") == 0 && api.message_iter_next(&kv)) {
            DBusIter v, rgb;
            api.message_iter_recurse(&kv, &v);
            if (api.message_iter_get_arg_type(&v) == kTypeStruct) {
              api.message_iter_recurse(&v, &rgb);
              double c[3] = {0, 0, 0};
              for (int k = 0; k < 3 && api.message_iter_get_arg_type(&rgb) == kTypeDouble; ++k) {
                api.message_iter_get_basic(&rgb, &c[k]);
                api.message_iter_next(&rgb);
              }
              const auto byte = [](double x) {
                return static_cast<std::uint32_t>(std::clamp(x, 0.0, 1.0) * 255.0 + 0.5);
              };
              colour = (byte(c[0]) << 16) | (byte(c[1]) << 8) | byte(c[2]);
              state = ScreenPickState::Picked;
            }
          }
          api.message_iter_next(&items);
        }
      }
      api.message_unref(message);
      break;
    }
    api.message_unref(message);
  }
  api.connection_close(bus);
  api.connection_unref(bus);
  std::lock_guard<std::mutex> lock(gMutex);
  if (gState == ScreenPickState::Picking) {
    gState = state;
    gResult = colour;
    if (state == ScreenPickState::Failed) gError = "lost the session bus";
  }
}

#ifdef DECKBOY_SCREEN_PICK_X11
// ── X11: the root window, read directly ─────────────────────────────────
struct X11Api {
  void* lib = nullptr;
  Display* (*openDisplay)(const char*) = nullptr;
  int (*closeDisplay)(Display*) = nullptr;
  Cursor (*createFontCursor)(Display*, unsigned) = nullptr;
  int (*freeCursor)(Display*, Cursor) = nullptr;
  int (*grabPointer)(Display*, Window, Bool, unsigned, int, int, Window, Cursor, Time) = nullptr;
  int (*ungrabPointer)(Display*, Time) = nullptr;
  int (*pending)(Display*) = nullptr;
  int (*nextEvent)(Display*, XEvent*) = nullptr;
  Bool (*queryPointer)(Display*, Window, Window*, Window*, int*, int*, int*, int*, unsigned*) = nullptr;
  XImage* (*getImage)(Display*, Drawable, int, int, unsigned, unsigned, unsigned long, int) = nullptr;
  int (*destroyImage)(XImage*) = nullptr;
  int (*warpPointer)(Display*, Window, Window, int, int, unsigned, unsigned, int, int) = nullptr;
  int (*flush)(Display*) = nullptr;

  bool load() {
    if (lib) return true;
    lib = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    bool ok = true;
    auto get = [&](auto& fn, const char* name) {
      fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
      ok = ok && fn;
    };
    get(openDisplay, "XOpenDisplay");
    get(closeDisplay, "XCloseDisplay");
    get(createFontCursor, "XCreateFontCursor");
    get(freeCursor, "XFreeCursor");
    get(grabPointer, "XGrabPointer");
    get(ungrabPointer, "XUngrabPointer");
    get(pending, "XPending");
    get(nextEvent, "XNextEvent");
    get(queryPointer, "XQueryPointer");
    get(getImage, "XGetImage");
    get(destroyImage, "XDestroyImage");
    get(warpPointer, "XWarpPointer");
    get(flush, "XFlush");
    if (!ok) {
      dlclose(lib);
      lib = nullptr;
    }
    return ok;
  }
};

X11Api gX;
Display* gDisplay = nullptr;
Cursor gCursor = 0;
bool gXTake = false;

void endX11() {
  if (!gDisplay) return;
  gX.ungrabPointer(gDisplay, CurrentTime);
  if (gCursor) gX.freeCursor(gDisplay, gCursor);
  gX.closeDisplay(gDisplay);
  gDisplay = nullptr;
  gCursor = 0;
}

bool x11Patch(int x, int y, std::array<std::uint32_t, 81>& out) {
  const Window root = DefaultRootWindow(gDisplay);
  const int sw = DisplayWidth(gDisplay, DefaultScreen(gDisplay));
  const int sh = DisplayHeight(gDisplay, DefaultScreen(gDisplay));
  // XGetImage fails outright on a rectangle that leaves the screen, so the
  // patch is read from inside it and an edge pixel repeats.
  const int left = std::clamp(x - 4, 0, std::max(0, sw - 9));
  const int top = std::clamp(y - 4, 0, std::max(0, sh - 9));
  XImage* image = gX.getImage(gDisplay, root, left, top, 9, 9, AllPlanes, ZPixmap);
  if (!image) return false;
  for (int j = 0; j < 9; ++j) {
    for (int i = 0; i < 9; ++i) {
      const int px = std::clamp(x - 4 + i - left, 0, 8);
      const int py = std::clamp(y - 4 + j - top, 0, 8);
      const unsigned long v = image->f.get_pixel(image, px, py);
      // The masks say where each channel sits; true-colour displays only.
      auto channel = [&](unsigned long mask) -> std::uint32_t {
        if (!mask) return 0;
        int shift = 0;
        while (!((mask >> shift) & 1)) ++shift;
        const unsigned long bits = mask >> shift;
        return static_cast<std::uint32_t>(((v & mask) >> shift) * 255 / bits);
      };
      out[static_cast<std::size_t>(j * 9 + i)] =
        (channel(image->red_mask) << 16) | (channel(image->green_mask) << 8) | channel(image->blue_mask);
    }
  }
  gX.destroyImage(image);
  return true;
}
#endif

}  // namespace

bool beginScreenPick(std::string* why) {
  {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gState == ScreenPickState::Picking) return true;
  }
  const char* wayland = std::getenv("WAYLAND_DISPLAY");
  gWayland = wayland && *wayland;
  if (gWayland) {
    {
      std::lock_guard<std::mutex> lock(gMutex);
      gState = ScreenPickState::Picking;
      gError.clear();
    }
    std::thread(runPortalPick).detach();
    return true;
  }
#ifdef DECKBOY_SCREEN_PICK_X11
  if (!gX.load()) {
    if (why) *why = "libX11 is not installed";
    return false;
  }
  gDisplay = gX.openDisplay(nullptr);
  if (!gDisplay) {
    if (why) *why = "no X display to read";
    return false;
  }
  constexpr unsigned kCrosshair = 34;   // XC_crosshair
  gCursor = gX.createFontCursor(gDisplay, kCrosshair);
  if (gX.grabPointer(gDisplay, DefaultRootWindow(gDisplay), False, ButtonPressMask | ButtonReleaseMask,
                     GrabModeAsync, GrabModeAsync, None, gCursor, CurrentTime) != GrabSuccess) {
    endX11();
    if (why) *why = "another app is holding the pointer";
    return false;
  }
  gXTake = false;
  std::lock_guard<std::mutex> lock(gMutex);
  gState = ScreenPickState::Picking;
  return true;
#else
  if (why) *why = "this build has no X11 support";
  return false;
#endif
}

ScreenPickFrame pollScreenPick() {
  ScreenPickFrame frame;
  if (gWayland) {
    std::lock_guard<std::mutex> lock(gMutex);
    frame.state = gState;
    frame.systemLoupe = true;
    frame.colour = gResult;
    frame.error = gError;
    if (gState != ScreenPickState::Picking) gState = ScreenPickState::Idle;
    return frame;
  }
#ifdef DECKBOY_SCREEN_PICK_X11
  if (!gDisplay) return frame;
  bool taken = gXTake;
  bool cancelled = false;
  int x = 0, y = 0;
  while (gX.pending(gDisplay)) {
    XEvent event;
    gX.nextEvent(gDisplay, &event);
    if (event.type == ButtonPress) {
      if (event.xbutton.button == Button1) {
        taken = true;
        x = event.xbutton.x_root;
        y = event.xbutton.y_root;
      } else {
        cancelled = true;
      }
    }
  }
  {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gState == ScreenPickState::Cancelled) cancelled = true;
  }
  if (cancelled) {
    endX11();
    std::lock_guard<std::mutex> lock(gMutex);
    gState = ScreenPickState::Idle;
    frame.state = ScreenPickState::Cancelled;
    return frame;
  }
  if (!taken || gXTake) {
    Window rootOut, childOut;
    int wx, wy;
    unsigned mask;
    gX.queryPointer(gDisplay, DefaultRootWindow(gDisplay), &rootOut, &childOut, &x, &y, &wx, &wy, &mask);
  }
  frame.hasPatch = x11Patch(x, y, frame.patch);
  frame.colour = frame.patch[40];
  if (taken) {
    endX11();
    std::lock_guard<std::mutex> lock(gMutex);
    gState = ScreenPickState::Idle;
    frame.state = frame.hasPatch ? ScreenPickState::Picked : ScreenPickState::Failed;
    if (!frame.hasPatch) frame.error = "the screen could not be read there";
    return frame;
  }
  frame.state = ScreenPickState::Picking;
#endif
  return frame;
}

void cancelScreenPick() {
  std::lock_guard<std::mutex> lock(gMutex);
  if (gState == ScreenPickState::Picking) gState = ScreenPickState::Cancelled;
}

void nudgeScreenPick(int dx, int dy) {
#ifdef DECKBOY_SCREEN_PICK_X11
  if (gDisplay) {
    gX.warpPointer(gDisplay, None, None, 0, 0, 0, 0, dx, dy);
    gX.flush(gDisplay);
  }
#else
  (void)dx;
  (void)dy;
#endif
}

void takeScreenPick() {
#ifdef DECKBOY_SCREEN_PICK_X11
  if (gDisplay) gXTake = true;
#endif
}

#endif

}  // namespace deckboy::platform
