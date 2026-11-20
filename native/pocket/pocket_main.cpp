// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// pocket_main.cpp -- Deckboy Pocket: the phone app.
//
// The desk's own MediaEngine behind a one-handed portrait screen: the program
// picture on top, the cue list under it, the button strip at the bottom (a pad
// that picks a cue, GO and BACK, BLACK and MENU). A clicker's Right/Left are
// GO/BACK exactly as on the desk.
//
// Built for Android by Gradle (android/), as the libmain.so SDL's activity
// loads. Until the Android FFmpeg build is in, it plays what Deckboy makes
// itself (bars, Pocket Test, Illusion and the other generators).
// ============================================================================
#define SDL_MAIN_USE_CALLBACKS 1
#undef SDL_MAIN_HANDLED   // the desk's sources bring it; Pocket's main is SDL's
#include "core/sdl_compat.hpp"
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "core/constants.hpp"
#include "core/fs.hpp"
#include "core/media_probe.hpp"
#include "core/pattern_helpers.hpp"
#include "core/show_file.hpp"
#include "deckboy_version.hpp"
#include "core/types.hpp"
#include "core/utils.hpp"
#include "engine/media_engine.hpp"
#if defined(DECKBOY_INPROC_DECODE)
#include "engine/libav_decoder.hpp"   // the rendered shells' JPEGs
#endif
#include "engine/direction_inproc.hpp"
#include "core/cue_helpers.hpp"
#include "core/expression_parser.hpp"
#include <optional>
#include "platform/network.hpp"
using namespace deckboy::platform;   // the remote port (sockets)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <cctype>
#include <cerrno>
#include <mutex>
#include <thread>
#include <sstream>
#include <iomanip>
#ifndef _WIN32
#include <fcntl.h>
#endif

#if defined(SDL_PLATFORM_ANDROID)
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#endif
#if defined(SDL_PLATFORM_ANDROID) && defined(DECKBOY_INPROC_DECODE)
extern "C" {
#include <libavcodec/jni.h>
}
#endif

namespace {

// The inks, set from a Deckboy theme (data/themes/<name>/theme.txt) by
// applyTheme; these are the Pocket theme's, for before one is read.
struct Ink { Uint8 r, g, b; };
Ink kLight {196, 207, 161};
Ink kMid {140, 148, 106};
Ink kDark {74, 80, 56};
Ink kDeep {26, 30, 20};
Ink kShell {184, 184, 176};
Ink kShellShade {150, 150, 142};
Ink kBezel {44, 44, 52};
Ink kPad {30, 30, 34};
Ink kButton {120, 28, 62};
Ink kButtonLit {176, 60, 104};
Ink kPill {96, 96, 104};
Ink kPrint {46, 46, 70};
constexpr Ink kBlack {0, 0, 0};
constexpr Ink kInOut {35, 73, 127};   // IN and OUT marks: blue, apart from the four inks

double gThemeHueCycle = 0.0;              // seconds per turn; 0 = still
std::array<Ink, 10> gThemeBase {};          // the theme's own inks, before any turn

Ink mixInk(Ink a, Ink b, float t) {
  auto m = [t](Uint8 x, Uint8 y) { return static_cast<Uint8>(std::lround(x + (y - x) * t)); };
  return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b)};
}
float lumaOf(Ink c) { return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.0f; }

// A desk theme's colours, mapped onto Pocket's parts. The screen keeps the
// theme's four LCD inks; the shell, its print and the buttons come from the
// theme's case colours, so every one of the desk's themes is a Pocket skin.
bool applyTheme(const std::string& dataDir, const std::string& name) {
  const std::string path = dataDir + "themes/" + name + "/theme.txt";
  size_t size = 0;
  char* text = static_cast<char*>(SDL_LoadFile(path.c_str(), &size));
  if (!text) return false;
  std::map<std::string, Ink> c;
  std::string all(text, size);
  SDL_free(text);
  std::size_t at = 0;
  while (at < all.size()) {
    std::size_t end = all.find('\n', at);
    if (end == std::string::npos) end = all.size();
    std::string line = all.substr(at, end - at);
    at = end + 1;
    if (line.empty() || line[0] == '#') continue;
    const std::size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;
    const std::string key = line.substr(0, tab);
    std::string hex = line.substr(tab + 1);
    while (!hex.empty() && (hex.back() == '\r' || hex.back() == ' ')) hex.pop_back();
    if (key == "hue_cycle") {   // seconds for one turn of the colour wheel (Rainbow Road)
      gThemeHueCycle = std::atof(hex.c_str());
      continue;
    }
    if (hex.size() < 6) continue;
    const unsigned long v = std::strtoul(hex.substr(0, 6).c_str(), nullptr, 16);
    c[key] = Ink {static_cast<Uint8>(v >> 16), static_cast<Uint8>(v >> 8), static_cast<Uint8>(v)};
  }
  if (!c.count("screen_light") || !c.count("screen_deep") || !c.count("shell_outer")) return false;
  gThemeHueCycle = 0.0;
  for (std::size_t at2 = 0; at2 < all.size();) {   // hue_cycle, read again now the theme is known good
    std::size_t end2 = all.find('\n', at2);
    if (end2 == std::string::npos) end2 = all.size();
    const std::string line2 = all.substr(at2, end2 - at2);
    at2 = end2 + 1;
    if (line2.rfind("hue_cycle\t", 0) == 0) gThemeHueCycle = std::atof(line2.c_str() + 10);
  }
  auto get = [&](const char* k, Ink fallback) { auto it = c.find(k); return it == c.end() ? fallback : it->second; };
  kLight = c["screen_light"];
  kDeep = c["screen_deep"];
  kMid = get("screen_mid", mixInk(kLight, kDeep, 0.35f));
  kDark = get("screen_dark", mixInk(kLight, kDeep, 0.7f));
  kShell = c["shell_outer"];
  const Ink shadow = get("shell_shadow", mixInk(kShell, Ink {0, 0, 0}, 0.45f));
  kShellShade = mixInk(kShell, shadow, 0.35f);
  kBezel = mixInk(shadow, Ink {0, 0, 0}, 0.35f);
  kPad = mixInk(shadow, Ink {0, 0, 0}, 0.6f);
  kButton = get("delete_bezel", Ink {120, 28, 62});
  kButtonLit = mixInk(kButton, Ink {255, 255, 255}, 0.3f);
  kPill = get("button_bezel", mixInk(kShell, shadow, 0.6f));
  // Print on the shell: the theme's dark on a light case, light on a dark one.
  kPrint = lumaOf(kShell) > 0.45f ? mixInk(shadow, Ink {0, 0, 40}, 0.4f) : mixInk(kShell, Ink {255, 255, 255}, 0.75f);
  gThemeBase = {kLight, kMid, kDark, kDeep, kShell, kShellShade, kBezel, kPad, kPill, kPrint};
  return true;
}

// A cycling theme (hue_cycle): every ink but the buttons' (danger stays red)
// turned round the wheel, keeping its brightness, as the desk does.
Ink rotateHue(Ink c, double degrees) {
  const double a = degrees * 3.14159265358979 / 180.0, co = std::cos(a), si = std::sin(a);
  const double m[9] = {
    0.2126 + co * 0.7874 - si * 0.2126, 0.7152 - co * 0.7152 - si * 0.7152, 0.0722 - co * 0.0722 + si * 0.9278,
    0.2126 - co * 0.2126 + si * 0.1430, 0.7152 + co * 0.2848 + si * 0.1400, 0.0722 - co * 0.0722 - si * 0.2830,
    0.2126 - co * 0.2126 - si * 0.7874, 0.7152 - co * 0.7152 + si * 0.7152, 0.0722 + co * 0.9278 + si * 0.0722,
  };
  auto ch = [](double v) { return static_cast<Uint8>(std::clamp(std::lround(v), 0L, 255L)); };
  return {ch(m[0] * c.r + m[1] * c.g + m[2] * c.b), ch(m[3] * c.r + m[4] * c.g + m[5] * c.b), ch(m[6] * c.r + m[7] * c.g + m[8] * c.b)};
}

void turnTheme(double degrees) {
  Ink* targets[10] = {&kLight, &kMid, &kDark, &kDeep, &kShell, &kShellShade, &kBezel, &kPad, &kPill, &kPrint};
  for (int i = 0; i < 10; ++i) *targets[i] = rotateHue(gThemeBase[static_cast<std::size_t>(i)], degrees);
}

// What a first launch copies out of the APK, so Deckboy's file-based code
// (Paths::dataDir) finds its data where it always does.
const char* const kBundledData[] = {
    "PressStart2P.ttf",
    "fonts/LiberationSans-Regular.ttf",
    "fonts/LiberationMono-Regular.ttf",
    "themes/pocket/theme.txt",
};

std::string joinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  return a.back() == '/' ? a + b : a + "/" + b;
}

#if defined(SDL_PLATFORM_ANDROID)
// -- Android's Java side (PocketBridge.java) --
// Called on SDL's main thread, which Java started, so FindClass sees the
// app's own classes.
jobject appContext() {
  static jobject context = nullptr;
  if (!context) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject local = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env && local) {
      context = env->NewGlobalRef(local);
      env->DeleteLocalRef(local);
    }
  }
  return context;
}

jclass bridgeClass(JNIEnv* env) {
  static jclass cls = nullptr;
  if (!cls) {
    jclass local = env->FindClass("io/github/utopian_academy/deckboy/pocket/PocketBridge");
    if (!local) {
      env->ExceptionClear();
      return nullptr;
    }
    cls = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
  }
  return cls;
}

std::string bridgeString(const char* method, const std::string& uri) {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  jclass cls = env ? bridgeClass(env) : nullptr;
  if (!cls) return "";
  jmethodID m = env->GetStaticMethodID(cls, method, "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;");
  if (!m) { env->ExceptionClear(); return ""; }
  jstring arg = env->NewStringUTF(uri.c_str());
  auto* result = static_cast<jstring>(env->CallStaticObjectMethod(cls, m, appContext(), arg));
  env->DeleteLocalRef(arg);
  if (env->ExceptionCheck()) { env->ExceptionClear(); return ""; }
  std::string out;
  if (result) {
    const char* chars = env->GetStringUTFChars(result, nullptr);
    out = chars ? chars : "";
    env->ReleaseStringUTFChars(result, chars);
    env->DeleteLocalRef(result);
  }
  return out;
}

int bridgeInt(const char* method) {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  jclass cls = env ? bridgeClass(env) : nullptr;
  if (!cls) return -1;
  jmethodID m = env->GetStaticMethodID(cls, method, "(Landroid/content/Context;)I");
  if (!m) { env->ExceptionClear(); return -1; }
  const jint v = env->CallStaticIntMethod(cls, m, appContext());
  if (env->ExceptionCheck()) { env->ExceptionClear(); return -1; }
  return v;
}

void bridgeKeepAccess(const std::string& uri) {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  jclass cls = env ? bridgeClass(env) : nullptr;
  if (!cls) return;
  jmethodID m = env->GetStaticMethodID(cls, "keepAccess", "(Landroid/content/Context;Ljava/lang/String;)Z");
  if (!m) { env->ExceptionClear(); return; }
  jstring arg = env->NewStringUTF(uri.c_str());
  env->CallStaticBooleanMethod(cls, m, appContext(), arg);
  env->DeleteLocalRef(arg);
  if (env->ExceptionCheck()) env->ExceptionClear();
}

int bridgeRenderPdf(const std::string& uri, const std::string& outDir, int width) {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  jclass cls = env ? bridgeClass(env) : nullptr;
  if (!cls) return -1;
  jmethodID m = env->GetStaticMethodID(cls, "renderPdf", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;I)I");
  if (!m) { env->ExceptionClear(); return -1; }
  jstring a = env->NewStringUTF(uri.c_str());
  jstring b = env->NewStringUTF(outDir.c_str());
  const jint pages = env->CallStaticIntMethod(cls, m, appContext(), a, b, static_cast<jint>(width));
  env->DeleteLocalRef(a);
  env->DeleteLocalRef(b);
  if (env->ExceptionCheck()) { env->ExceptionClear(); return -1; }
  return pages;
}
#endif

// ── The show output: a second screen's surface, from PocketActivity ──
//
// Java's UI thread hands it over (or takes it away); SDL's main thread draws
// into it. The lock is held only to swap the pointer or to post one frame.
struct OutputScreen {
  std::mutex lock;
#if defined(SDL_PLATFORM_ANDROID)
  ANativeWindow* window = nullptr;
#endif
  int width = 0, height = 0;      // the screen's own size
  float refreshHz = 0;
  std::string name;
  int generation = 0;             // bumps on every change, so the app re-sizes
};
OutputScreen& outputScreen() {
  static OutputScreen screen;
  return screen;
}

// Picture-in-picture, shared with Java's UI thread.
std::atomic<bool> gWantsPip {false};    // a cue is live: leaving Pocket keeps it up
std::atomic<bool> gInPip {false};
std::mutex gPipLock;
std::vector<int> gPipActions;          // 1 GO, 2 BACK, 3 BLACK from the floating window

}  // namespace

#if defined(SDL_PLATFORM_ANDROID)
extern "C" JNIEXPORT void JNICALL
Java_io_github_utopian_1academy_deckboy_pocket_PocketActivity_nativeSetOutputSurface(
    JNIEnv* env, jclass, jobject surface, jint width, jint height, jfloat refreshHz, jstring name) {
  OutputScreen& out = outputScreen();
  std::lock_guard<std::mutex> hold(out.lock);
  if (out.window) {
    ANativeWindow_release(out.window);
    out.window = nullptr;
  }
  out.width = out.height = 0;
  out.refreshHz = 0;
  out.name.clear();
  if (surface && width > 0 && height > 0) {
    out.window = ANativeWindow_fromSurface(env, surface);
    out.width = width;
    out.height = height;
    out.refreshHz = refreshHz;
    if (name) {
      const char* chars = env->GetStringUTFChars(name, nullptr);
      out.name = chars ? chars : "";
      env->ReleaseStringUTFChars(name, chars);
    }
  }
  ++out.generation;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_utopian_1academy_deckboy_pocket_PocketActivity_nativeWantsPip(JNIEnv*, jclass) {
  return gWantsPip.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_utopian_1academy_deckboy_pocket_PocketActivity_nativeSetPip(JNIEnv*, jclass, jboolean on) {
  gInPip.store(on == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_utopian_1academy_deckboy_pocket_PocketActivity_nativePipAction(JNIEnv*, jclass, jint action) {
  std::lock_guard<std::mutex> hold(gPipLock);
  gPipActions.push_back(static_cast<int>(action));
}
#endif

namespace {

bool endsWithNoCase(const std::string& s, const std::string& tail) {
  if (s.size() < tail.size()) return false;
  for (std::size_t i = 0; i < tail.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(s[s.size() - tail.size() + i])) != tail[i]) return false;
  }
  return true;
}

// Copy the bundled data into app storage and point Deckboy's paths at it.
std::string prepareDataDir() {
  const char* base = nullptr;
#if defined(SDL_PLATFORM_ANDROID)
  base = SDL_GetAndroidInternalStoragePath();
#endif
  std::string root = base ? joinPath(base, "deckboy") : std::string(SDL_GetPrefPath("Deckboy", "Pocket"));
  const std::string data = joinPath(root, "data");
  for (const char* rel : kBundledData) {
    const std::string to = joinPath(data, rel);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(to.c_str(), &info) && info.size > 0) continue;
    size_t size = 0;
    void* bytes = SDL_LoadFile((std::string("data/") + rel).c_str(), &size);  // from the APK's assets
    if (!bytes) {
      SDL_Log("pocket: no bundled %s (%s)", rel, SDL_GetError());
      continue;
    }
    SDL_CreateDirectory(to.substr(0, to.find_last_of('/')).c_str());
    if (!SDL_SaveFile(to.c_str(), bytes, size)) SDL_Log("pocket: cannot write %s (%s)", to.c_str(), SDL_GetError());
    SDL_free(bytes);
  }
  // Every desk theme: Gradle writes themes/index.txt, one folder name a line.
  size_t indexSize = 0;
  if (char* index = static_cast<char*>(SDL_LoadFile("data/themes/index.txt", &indexSize))) {
    std::string names(index, indexSize);
    SDL_free(index);
    std::size_t at = 0;
    while (at < names.size()) {
      std::size_t end = names.find('\n', at);
      if (end == std::string::npos) end = names.size();
      std::string name = names.substr(at, end - at);
      at = end + 1;
      while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
      if (name.empty()) continue;
      const std::string rel = "themes/" + name + "/theme.txt";
      const std::string to = joinPath(data, rel);
      SDL_PathInfo info;
      if (SDL_GetPathInfo(to.c_str(), &info) && info.size > 0) continue;
      size_t n = 0;
      if (void* bytes = SDL_LoadFile(("data/" + rel).c_str(), &n)) {
        SDL_CreateDirectory(to.substr(0, to.find_last_of('/')).c_str());
        SDL_SaveFile(to.c_str(), bytes, n);
        SDL_free(bytes);
      }
    }
    const std::string indexTo = joinPath(data, "themes/index.txt");
    SDL_SaveFile(indexTo.c_str(), names.data(), names.size());
  }
  SDL_setenv_unsafe("DECKBOY_ROOT", root.c_str(), 1);
  const std::string state = joinPath(root, "state");
  SDL_CreateDirectory(state.c_str());
  SDL_setenv_unsafe("DECKBOY_STATE_DIR", state.c_str(), 1);
  return data;
}

// ── Drawing ──────────────────────────────────────────────────────────────────

void setInk(SDL_Renderer* r, Ink c, Uint8 a = 255) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a); }

void fillRect(SDL_Renderer* r, Ink c, const SDL_FRect& rect) {
  setInk(r, c);
  SDL_RenderFillRect(r, &rect);
}

// A filled shape from its outline (a triangle fan around the centre).
void fillOutline(SDL_Renderer* r, Ink c, const std::vector<SDL_FPoint>& outline, SDL_FPoint centre, float alpha = 1.0f) {
  if (outline.size() < 3) return;
  std::vector<SDL_Vertex> v;
  std::vector<int> idx;
  const SDL_FColor col {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha};
  v.push_back({centre, col, {0, 0}});
  for (const SDL_FPoint& p : outline) v.push_back({p, col, {0, 0}});
  const int n = static_cast<int>(outline.size());
  for (int i = 0; i < n; ++i) {
    idx.push_back(0);
    idx.push_back(1 + i);
    idx.push_back(1 + (i + 1) % n);
  }
  SDL_RenderGeometry(r, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
}

// Corners in order round the shape (y grows downward): top-left 180-270
// degrees, top-right 270-360, bottom-right 0-90, bottom-left 90-180.
std::vector<SDL_FPoint> roundRectOutline(const SDL_FRect& b, float radius) {
  const float rad = std::max(0.0f, std::min({radius, b.w / 2, b.h / 2}));
  std::vector<SDL_FPoint> pts;
  const float cx[4] = {b.x + rad, b.x + b.w - rad, b.x + b.w - rad, b.x + rad};
  const float cy[4] = {b.y + rad, b.y + rad, b.y + b.h - rad, b.y + b.h - rad};
  const float start[4] = {180, 270, 0, 90};
  for (int k = 0; k < 4; ++k) {
    for (int i = 0; i <= 8; ++i) {
      const float a = (start[k] + 90.0f * static_cast<float>(i) / 8.0f) * 3.14159265f / 180.0f;
      pts.push_back({cx[k] + rad * std::cos(a), cy[k] + rad * std::sin(a)});
    }
  }
  return pts;
}

void fillRound(SDL_Renderer* r, Ink c, const SDL_FRect& b, float radius) {
  fillOutline(r, c, roundRectOutline(b, radius), {b.x + b.w / 2, b.y + b.h / 2});
}

// See-through, for marks laid over a rendered shell (a press, BLACK on).
void fillRoundAlpha(SDL_Renderer* r, Ink c, float alpha, const SDL_FRect& b, float radius) {
  fillOutline(r, c, roundRectOutline(b, radius), {b.x + b.w / 2, b.y + b.h / 2}, alpha);
}

void fillCircle(SDL_Renderer* r, Ink c, SDL_FPoint centre, float radius) {
  std::vector<SDL_FPoint> pts;
  for (int i = 0; i < 48; ++i) {
    const float a = static_cast<float>(i) * 2.0f * 3.14159265f / 48.0f;
    pts.push_back({centre.x + radius * std::cos(a), centre.y + radius * std::sin(a)});
  }
  fillOutline(r, c, pts, centre);
}

bool inside(const SDL_FRect& r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

// Text through SDL_ttf, cached by (font, text, ink). Labels that do not fit
// are cut with an ellipsis rather than run off the edge.
class Text {
 public:
  void init(SDL_Renderer* r) { renderer_ = r; }
  ~Text() { clear(); }
  void clear() {
    for (auto& [k, t] : cache_) SDL_DestroyTexture(t.tex);
    cache_.clear();
  }

  // Draw in box (left, or centred when centre is set), vertically centred.
  void draw(TTF_Font* font, const std::string& s, Ink c, const SDL_FRect& box, bool centre = false) {
    if (!font || s.empty()) return;
    const std::string fitted = fit(font, s, box.w);
    Entry& e = entry(font, fitted, c);
    if (!e.tex) return;
    const float x = centre ? box.x + (box.w - e.w) / 2 : box.x;
    const float y = box.y + (box.h - e.h) / 2;
    SDL_FRect dst {std::round(x), std::round(y), e.w, e.h};
    SDL_RenderTexture(renderer_, e.tex, nullptr, &dst);
  }

 private:
  struct Entry { SDL_Texture* tex = nullptr; float w = 0, h = 0; };

  std::string fit(TTF_Font* font, const std::string& s, float maxW) {
    int w = 0, h = 0;
    TTF_GetStringSize(font, s.c_str(), 0, &w, &h);
    if (static_cast<float>(w) <= maxW) return s;
    std::string cut = s;
    while (!cut.empty()) {
      cut.pop_back();
      const std::string withDots = cut + "...";
      TTF_GetStringSize(font, withDots.c_str(), 0, &w, &h);
      if (static_cast<float>(w) <= maxW) return withDots;
    }
    return "";
  }

  Entry& entry(TTF_Font* font, const std::string& s, Ink c) {
    char key[48];
    std::snprintf(key, sizeof key, "%p|%02x%02x%02x|", static_cast<void*>(font), c.r, c.g, c.b);
    auto [it, fresh] = cache_.try_emplace(key + s);
    if (fresh) {
      if (cache_.size() > 400) {  // a long show scrolls through many labels
        clear();
        it = cache_.try_emplace(key + s).first;
      }
      SDL_Surface* surf = TTF_RenderText_Blended(font, s.c_str(), 0, SDL_Color {c.r, c.g, c.b, 255});
      if (surf) {
        it->second.tex = SDL_CreateTextureFromSurface(renderer_, surf);
        it->second.w = static_cast<float>(surf->w);
        it->second.h = static_cast<float>(surf->h);
        SDL_DestroySurface(surf);
      }
    }
    return it->second;
  }

  SDL_Renderer* renderer_ = nullptr;
  std::map<std::string, Entry> cache_;
};

std::string clock(double seconds) {
  if (!(seconds >= 0)) seconds = 0;
  const int s = static_cast<int>(seconds);
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
  return buf;
}

// mm:ss.ff, frames at the cue's own rate (hh: only when there are hours).
std::string timecode(double seconds, double fps) {
  if (!(seconds >= 0)) seconds = 0;
  if (!(fps > 1)) fps = 30;
  const long frames = static_cast<long>(std::floor(seconds * fps + 1e-6));
  const long perSec = static_cast<long>(std::lround(fps));
  const long total = frames / std::max(1L, perSec);
  const long ff = frames % std::max(1L, perSec);
  char buf[32];
  if (total >= 3600) {
    std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld.%02ld", total / 3600, (total / 60) % 60, total % 60, ff);
  } else {
    std::snprintf(buf, sizeof buf, "%02ld:%02ld.%02ld", total / 60, total % 60, ff);
  }
  return buf;
}

// A cue's played range: in to out (out 0 = the end of the media).
double cueIn(const Cue& c) { return std::max(0.0, c.inPointSeconds); }
double cueOut(const Cue& c) {
  return (c.outPointSeconds > 0.0 && (c.duration <= 0.0 || c.outPointSeconds < c.duration)) ? c.outPointSeconds : c.duration;
}
double cueFrame(const Cue& c) { return 1.0 / (c.fps > 1.0 ? c.fps : 30.0); }
bool cueHasTimeline(const Cue& c) {
  return (c.kind == CueKind::Video || c.kind == CueKind::Audio) && c.duration > 0.0;
}

// What a cue does at its end, as the three choices Pocket offers.
enum class EndMode { Hold, Loop, Next };
EndMode endModeOf(const Cue& c) {
  if (c.loop || c.endAction == CueEndAction::Loop) return EndMode::Loop;
  if (c.endAction == CueEndAction::AutoNext) return EndMode::Next;
  return EndMode::Hold;
}
void setEndMode(Cue& c, EndMode m) {
  c.loop = (m == EndMode::Loop);
  c.pauseOnLastFrame = true;   // the last frame holds through any change
  c.endAction = m == EndMode::Loop ? CueEndAction::Loop : m == EndMode::Next ? CueEndAction::AutoNext : CueEndAction::PauseOnLast;
}

const char* kindTag(const Cue& c) {
  switch (c.kind) {
    case CueKind::Video: return "VIDEO";
    case CueKind::Image: return "STILL";
    case CueKind::Audio: return "SOUND";
    // James 11-20: PATTERN for patterns (bars, test cards), SYNTH for synths
    // (the moving generators and the desk's Video Synth). Was MADE for all.
    case CueKind::Pattern:
      return generatorPatternName(normalizePatternTypeId(c.path.rfind("pattern://", 0) == 0 ? c.path.substr(10) : c.path)).empty()
                 ? "PATTERN" : "SYNTH";
    case CueKind::VideoSynth: return "SYNTH";
    case CueKind::Text: return "TEXT";
    case CueKind::LowerThird: return "LOWER THIRD";
    case CueKind::Timer: return "TIMER";
    case CueKind::Tone: return "TONE";
    case CueKind::Camera: return "CAMERA";
    case CueKind::Browser: return "WEB";
    default: return "CUE";
  }
}

// ── The app ──────────────────────────────────────────────────────────────────

class Pocket {
 public:
  bool init() {
    const std::string data = prepareDataDir();
    // Keep running when Pocket is not in front: picture-in-picture and the
    // HDMI output both need the show to go on while another app has the screen.
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    // Turn with the phone: portrait for one hand, landscape for the Advance-style layout.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait LandscapeLeft LandscapeRight");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
      SDL_Log("pocket: SDL_Init failed: %s", SDL_GetError());
      return false;
    }
    if (!TTF_Init()) SDL_Log("pocket: TTF_Init failed: %s", SDL_GetError());
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    window_ = SDL_CreateWindow("Deckboy Pocket", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#else
    // A desktop build is for working on Pocket: a phone-shaped window.
    window_ = SDL_CreateWindow("Deckboy Pocket", 480, 1040, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#endif
    if (!window_) {
      SDL_Log("pocket: no window: %s", SDL_GetError());
      return false;
    }
    // The GPU renderer (Vulkan on Android, Metal on iOS) is where Deckboy's
    // GPU effects run; SDL's own choice is the fallback.
    renderer_ = SDL_CreateRenderer(window_, "gpu");
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
      SDL_Log("pocket: no renderer: %s", SDL_GetError());
      return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    text_.init(renderer_);
    SDL_Log("pocket: renderer %s, data %s", SDL_GetRendererName(renderer_), data.c_str());

    SDL_AudioSpec spec {};
    spec.freq = kAudioRate;
    spec.format = SDL_AUDIO_S16;
    spec.channels = kAudioChannels;
    audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!audio_) SDL_Log("pocket: no audio (%s); picture only", SDL_GetError());

#if defined(SDL_PLATFORM_ANDROID) && defined(DECKBOY_INPROC_DECODE)
    // The phone's hardware decoders (MediaCodec) are Java objects underneath;
    // ffmpeg reaches them through the VM.
    if (auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv())) {
      JavaVM* vm = nullptr;
      if (env->GetJavaVM(&vm) == JNI_OK && vm) av_jni_set_java_vm(vm, nullptr);
      // ...and files the system picker hands over (content://) through the app.
      if (jobject ctx = appContext()) av_jni_set_android_app_ctx(ctx, nullptr);
    }
#endif
    // The engine places its picture for the OUTPUT's shape. Pocket's program is
    // a 1920x1080 picture (drawProgramTexture); without this the engine took
    // the portrait phone screen as the output and squeezed the picture.
    engine_ = std::make_unique<MediaEngine>(renderer_, audio_, MediaEngine::AudioTapCallback {},
                                            MediaEngine::CuePathResolver {}, MediaEngine::DecodeDeviceProvider {},
                                            [this] { return MediaEngine::OutputModeHint {programW_, programH_, programHz_}; });
    engine_->setDrawsOwnTexture(true);
    engine_->setVolume(0.8f);

    // The desk's remote protocol on the desk's port, so Companion, scripts and
    // the desk itself drive Pocket as deck 1 (the same shape Mini answers in).
    listen_ = createBoundSocket(SOCK_STREAM, kRemotePort, true, false);
    if (listen_ == kInvalidSocket) SDL_Log("pocket: remote port %d is taken; remote control is off", kRemotePort);
    loadMediaFolder();
    // The list starts with what is in the media folder and nothing else
    // (James, 11-20: it need not open with a row of patterns). Tests ask for
    // the demo list with DECKBOY_POCKET_DEMO=1.
    const char* demo = SDL_getenv("DECKBOY_POCKET_DEMO");
    const bool demoList = demo && demo[0] == '1';
    const std::pair<const char*, const char*> kFirstShow[] = {
        {"smpte-bars", "SMPTE colour bars"}, {"pocket-test", "Pocket Test"}, {"illusion", ""},
        {"swirl", ""}, {"lumia", ""}, {"fireside", ""}, {"crosshatch", "Crosshatch"}};
    for (const auto& [type, label] : kFirstShow) {
      if (!demoList) break;
      Cue cue;
      cue.kind = CueKind::Pattern;
      cue.path = std::string("pattern://") + type;
      const std::string generated = generatorPatternName(type);
      cue.name = generated.empty() ? label : generated;
      cue.width = 1920;
      cue.height = 1080;
      cue.formatName = "generated";
      cue.pauseOnLastFrame = true;
      cue.stillDurationSeconds = 0.0;
      cue.hasAudio = cue.audioEnabled = (std::string(type) == "pocket-test");
      cue.id = "pocket-" + std::to_string(++nextId_);   // the queue, the shuffle and SYNC find cues by id
      cues_.push_back(cue);
    }
    if (!demoList) restoreList();
    loadThemes();
    loadFonts();
    loadSyncSettings();
    loadShells();
    loadDefaults();
    loadUnlocks();
    {
      size_t n = 0;
      const char* state = SDL_getenv("DECKBOY_STATE_DIR");
      if (char* saved = static_cast<char*>(SDL_LoadFile((std::string(state ? state : ".") + "/pocket_pill.txt").c_str(), &n))) {
        const int v = std::atoi(std::string(saved, n).c_str());
        if (v >= 0 && v < static_cast<int>(Pill::Count)) pill_ = static_cast<Pill>(v);
        SDL_free(saved);
      }
    }
    {
      size_t n = 0;
      const char* state = SDL_getenv("DECKBOY_STATE_DIR");
      if (char* saved = static_cast<char*>(SDL_LoadFile((std::string(state ? state : ".") + "/pocket_haptics.txt").c_str(), &n))) {
        haptics_ = !(n > 0 && saved[0] == '0');
        SDL_free(saved);
      }
    }
    return true;
  }

  // The desk's themes, and the one chosen last time (state/pocket.txt).
  void loadThemes() {
    const std::string data = std::string(SDL_getenv("DECKBOY_ROOT") ? SDL_getenv("DECKBOY_ROOT") : ".") + "/data/";
    size_t n = 0;
    if (char* index = static_cast<char*>(SDL_LoadFile((data + "themes/index.txt").c_str(), &n))) {
      std::string names(index, n);
      SDL_free(index);
      std::size_t at = 0;
      while (at < names.size()) {
        std::size_t end = names.find('\n', at);
        if (end == std::string::npos) end = names.size();
        std::string name = names.substr(at, end - at);
        at = end + 1;
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
        if (!name.empty()) themes_.push_back(name);
      }
    }
    std::sort(themes_.begin(), themes_.end());
    std::string want = "pocket";
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    if (state) {
      if (char* saved = static_cast<char*>(SDL_LoadFile((std::string(state) + "/pocket.txt").c_str(), &n))) {
        std::string line(saved, n);
        SDL_free(saved);
        if (line.rfind("theme\t", 0) == 0) {
          want = line.substr(6, line.find('\n') == std::string::npos ? std::string::npos : line.find('\n') - 6);
          while (!want.empty() && (want.back() == '\r' || want.back() == ' ')) want.pop_back();
        }
      }
    }
    setTheme(want);
  }

  // Where this phone starts on the wheel: picked once per install and kept,
  // so two phones (or a phone and a desk) on Rainbow Road look different at a
  // glance -- the point of a cycling theme. (Every phone calls itself
  // "localhost", so the desk's computer-name rule cannot work here.)
  double huePhase() {
    if (huePhase_ >= 0.0) return huePhase_;
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    const std::string file = std::string(state ? state : ".") + "/pocket_phase.txt";
    size_t n = 0;
    if (char* saved = static_cast<char*>(SDL_LoadFile(file.c_str(), &n))) {
      huePhase_ = std::atof(std::string(saved, n).c_str());
      SDL_free(saved);
    }
    if (!(huePhase_ >= 0.0 && huePhase_ < 1.0)) {
      huePhase_ = static_cast<double>(SDL_rand(3600)) / 3600.0;
      const std::string text = std::to_string(huePhase_);
      SDL_SaveFile(file.c_str(), text.data(), text.size());
    }
    return huePhase_;
  }

  void serviceThemeHue() {
    if (gThemeHueCycle <= 0.0) return;
    const double turns = static_cast<double>(SDL_GetTicks()) / 1000.0 / gThemeHueCycle + huePhase();
    const int step = static_cast<int>((turns - std::floor(turns)) * 180.0) % 180;
    if (step == hueStep_) return;
    hueStep_ = step;
    turnTheme(step * 2.0);
    text_.clear();
  }

  void setTheme(const std::string& name) {
    const std::string data = std::string(SDL_getenv("DECKBOY_ROOT") ? SDL_getenv("DECKBOY_ROOT") : ".") + "/data/";
    if (!applyTheme(data, name)) return;
    theme_ = name;
    hueStep_ = -1;
    text_.clear();   // cached labels carry the old inks
    if (const char* state = SDL_getenv("DECKBOY_STATE_DIR")) {
      const std::string line = "theme\t" + name + "\n";
      SDL_SaveFile((std::string(state) + "/pocket.txt").c_str(), line.data(), line.size());
    }
  }

  void stepTheme(int by) {
    if (themes_.empty()) return;
    auto it = std::find(themes_.begin(), themes_.end(), theme_);
    int i = it == themes_.end() ? 0 : static_cast<int>(it - themes_.begin());
    i = (i + by + static_cast<int>(themes_.size())) % static_cast<int>(themes_.size());
    setTheme(themes_[static_cast<std::size_t>(i)]);
  }

  // Every playable file in the app's media folder, in name order:
  // Android/data/<app>/files/media on the phone (copy files there over USB).
  void loadMediaFolder() {
    std::string base;
#if defined(SDL_PLATFORM_ANDROID)
    if (const char* ext = SDL_GetAndroidExternalStoragePath()) base = ext;
#endif
    if (base.empty()) base = SDL_GetPrefPath("Deckboy", "Pocket");
    mediaDir_ = joinPath(base, "media");
    SDL_CreateDirectory(mediaDir_.c_str());
    showsDir_ = joinPath(base, "shows");
    SDL_CreateDirectory(showsDir_.c_str());
    std::vector<dbfs::path> files;
    std::error_code ec;
    for (const auto& entry : dbfs::directory_iterator(dbfs::path(mediaDir_), ec)) {
      if (entry.is_regular_file(ec) && deckboy::core::media::isAcceptableMediaPath(entry.path())) {
        files.push_back(entry.path());
      }
    }
    std::sort(files.begin(), files.end());
    for (const dbfs::path& f : files) {
      std::optional<Cue> probed = deckboy::core::media::probeCue(f);
      if (!probed) {
        SDL_Log("pocket: could not read %s", f.string().c_str());
        continue;
      }
      Cue cue = std::move(*probed);
      cue.id = "pocket-" + std::to_string(++nextId_);
      applyDefaults(cue);   // what new cues start with (MENU > SHOW)
      if (cue.kind == CueKind::Image) cue.stillDurationSeconds = 0.0;  // a still holds until the next GO
      cue.pauseOnLastFrame = true;
      cues_.push_back(std::move(cue));
    }
    SDL_Log("pocket: %zu media cues from %s", cues_.size(), mediaDir_.c_str());
  }

  void shutdown() {
    if (stillsThread_.joinable()) stillsThread_.join();
    for (auto& [id, list] : stillsTex_) for (SDL_Texture* t : list) if (t) SDL_DestroyTexture(t);
    dirCancel_ = true;
    if (dirThread_.joinable()) dirThread_.join();
    dropFollowSocket();
    if (shellTex_) SDL_DestroyTexture(shellTex_);
    for (RemoteClient& c : clients_) closeSocket(c.socket);
    if (listen_ != kInvalidSocket) closeSocket(listen_);
    engine_.reset();
    text_.clear();
    for (TTF_Font* f : {pixel_, pixelSmall_, sans_, mono_, printFont_}) if (f) TTF_CloseFont(f);
    if (audio_) SDL_DestroyAudioStream(audio_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    TTF_Quit();
    SDL_Quit();
  }

  bool event(const SDL_Event& e) {
    switch (e.type) {
      case SDL_EVENT_QUIT:
        autosaveList();
        return false;
      case SDL_EVENT_WILL_ENTER_BACKGROUND:
      case SDL_EVENT_TERMINATING:
        autosaveList();   // Android may end Pocket from here without asking
        break;
      case SDL_EVENT_TEXT_INPUT:
        if (hostEditing_) {
          for (const char* c = e.text.text; *c; ++c) {
            if ((std::isalnum(static_cast<unsigned char>(*c)) || *c == '.' || *c == ':' || *c == '-') && followHost_.size() < 64) {
              followHost_ += *c;
            }
          }
        } else if (renameIndex_ != -1 && renameText_.size() < 200) {
          renameText_ += e.text.text;   // any language: SDL hands over UTF-8
        }
        break;
      case SDL_EVENT_KEY_DOWN:
        if (hostEditing_) {
          if (e.key.key == SDLK_BACKSPACE && !followHost_.empty()) followHost_.pop_back();
          else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) { endHostEdit(); startFollowing(); }
          else if (e.key.key == SDLK_AC_BACK || e.key.key == SDLK_ESCAPE) endHostEdit();
          return true;
        }
        if (renameIndex_ != -1) {
          if (e.key.key == SDLK_BACKSPACE && !renameText_.empty()) {
            // one whole character, however many bytes it took
            std::size_t cut = renameText_.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(renameText_[cut]) & 0xC0) == 0x80) --cut;
            renameText_.erase(cut);
          } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
            endRename(true);
          } else if (e.key.key == SDLK_AC_BACK || e.key.key == SDLK_ESCAPE) {
            endRename(false);
          }
          return true;
        }
        switch (e.key.key) {
          case SDLK_RIGHT: case SDLK_PAGEDOWN: case SDLK_SPACE: go(); break;
          case SDLK_LEFT: case SDLK_PAGEUP: back(); break;
          case SDLK_UP: select(selected_ - 1); break;
          case SDLK_DOWN: select(selected_ + 1); break;
          case SDLK_B: black_ = !black_; break;
          case SDLK_MENU: menuOpen_ = !menuOpen_; break;
          case SDLK_A: openPicker(); break;
          case SDLK_AC_BACK:   // Back closes what is open; it never quits mid-show
            if (phoneFull_) phoneFull_ = false;
            else if (sheet_ >= 0) sheet_ = -1;
            else if (menuOpen_) menuOpen_ = false;
            return true;
          default: break;
        }
        break;
      case SDL_EVENT_FINGER_DOWN: {
        const SDL_FPoint pt {e.tfinger.x * pxW_, e.tfinger.y * pxH_};
        fingers_[e.tfinger.fingerID] = pt;
        if (gesture_ || (sheet_ >= 0 && page_ == Page::Place && !calcOpen_ && inside(picRect_, pt.x, pt.y))) {
          beginGesture();
        } else if (fingers_.size() == 1) {
          onDown(pt.x, pt.y);
        }
        break;
      }
      case SDL_EVENT_FINGER_MOTION: {
        const SDL_FPoint pt {e.tfinger.x * pxW_, e.tfinger.y * pxH_};
        fingers_[e.tfinger.fingerID] = pt;
        if (gesture_) moveGesture();
        else if (fingers_.size() == 1) onMove(pt.x, pt.y);
        break;
      }
      case SDL_EVENT_FINGER_UP:
      case SDL_EVENT_FINGER_CANCELED:
        fingers_.erase(e.tfinger.fingerID);
        if (gesture_) {
          if (fingers_.empty()) gesture_ = false;
          else beginGesture();   // one finger lifted: carry on from here with the rest
        }
        drag_ = Drag::None;
        activeSlider_ = -1;
        holdRow_ = -1;
        if (swiping_ && swipeRow_ >= 0 && -swipeDx_ >= kSwipeDelete * unit_) {
          if (swipeRow_ == active_) {
            notice_ = "On air: take another cue first, or DELETE it in its sheet";
            noticeUntil_ = SDL_GetTicks() + 3000;
          } else {
            deleteCueAt(swipeRow_);
          }
        }
        swipeRow_ = -1;
        swiping_ = false;
        swipeDx_ = 0;
        picHoldSince_ = 0;
        if (pillDownAt_ && fingers_.empty()) {   // a tap on the left pill, not a hold
          pillDownAt_ = 0;
          doPill();
        }
        break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        loadFonts();
        break;
      default:
        break;
    }
    return true;
  }

  void frame() {
    const Uint64 t0 = SDL_GetPerformanceCounter();
    takePicks();
    if (SDL_GetTicks() >= autosaveAt_) {
      autosaveAt_ = SDL_GetTicks() + 4000;
      autosaveList();
    }
    pollRemote();
    pollFollow();
    serviceDirection();
    serviceStills();
    serviceMeter();
    // holding the left pill opens its choices (MENU > SKIN > LEFT BUTTON)
    if (pillDownAt_ && SDL_GetTicks() - pillDownAt_ > 600) {
      pillDownAt_ = 0;
      menuOpen_ = true;
      menuTab_ = 4;
      sheet_ = -1;
      buzz("lock");
      notice_ = "Choose what the left button does";
      noticeUntil_ = SDL_GetTicks() + 2500;
    }
    // holding the picture during a show opens PLACE for the live cue
    if (picHoldSince_ && SDL_GetTicks() - picHoldSince_ > 500) {
      picHoldSince_ = 0;
      if (active_ >= 0 && sheet_ < 0) {
        openSheet(active_);
        page_ = Page::Place;
        buzz("lock");
      }
    }
    if (follow_ != loggedFollow_) {
      SDL_Log("pocket: sync %s -> %s %s", followHeadline(loggedFollow_).c_str(), followHeadline(follow_).c_str(),
              followError_.c_str());
      loggedFollow_ = follow_;
    }
    engine_->update();
    // Animated generators (Swirl, Fireside, Lumia...) are redrawn every tick
    // from the wall clock, as the desk's update loop does; without this they
    // show their first frame and stop.
    if (active_ >= 0) {
      const Cue& c = cues_[static_cast<std::size_t>(active_)];
      if (c.kind == CueKind::Pattern && patternTypeIsAnimated(c.path) && engine_->hasPictureToShow()) {
        engine_->rebuildPatternFrame(c, static_cast<double>(SDL_GetTicks()) / 1000.0);
      } else if (c.kind == CueKind::VideoSynth) {
        engine_->rebuildVideoSynthFrame(c, static_cast<double>(SDL_GetTicks()) / 1000.0, engine_->programAudioLevel01());
      }
    }
    const Uint64 t1 = SDL_GetPerformanceCounter();
    followEnd();
    takePipActions();
    pollHealth();
    serviceThemeHue();
    gWantsPip.store(active_ >= 0 && !black_);
    updateShowRunning();
    // Hold a cue for its sheet.
    if (holdRow_ >= 0 && SDL_GetTicks() - holdStart_ > 450) {
      openSheet(holdRow_);
      holdRow_ = -1;
    }
    drawProgramTexture();
    const Uint64 t2 = SDL_GetPerformanceCounter();
    drawOutputScreen();
    const Uint64 t3 = SDL_GetPerformanceCounter();
    layout();
    SDL_Renderer* r = renderer_;
    hits_.clear();
    sliders_.clear();
    // Picture-in-picture and full screen: the program alone.
    if (gInPip.load() || phoneFull_) {
      drawProgramAlone(phoneFull_ && !gInPip.load());
      takeSnap();
      SDL_RenderPresent(r);
      return;
    }
    // The shell stays behind every portrait screen, menus and pages included:
    // a flat sheet behind MENU read as a white page (James, 11-20). Only the
    // show screen lets the shell's own windows and buttons stand in for
    // Pocket's (shellFace); editing draws its panels over the shell.
    if (shellTex_ && !landscape_) drawShell();
    else fillRect(r, kShell, {0, 0, pxW_, pxH_});
    drawProgram();
    if (hostEditing_) drawTextEditor("DESK ADDRESS", followHost_, "ENTER TO FOLLOW");
    if (renameIndex_ >= 0) drawTextEditor("CUE NAME", renameText_, "ENTER TO KEEP, BACK TO LEAVE IT");
    if (renameIndex_ == kNamingShow) drawTextEditor("SHOW NAME", renameText_, "ENTER TO SAVE, BACK TO LEAVE IT");
    if (sheet_ >= 0) {
      drawSheet();
    } else if (menuOpen_) {
      drawMenu();
      if (aboutPage()) drawControls();
    } else {
      drawCueList();
      drawControls();
    }
    takeSnap();
    SDL_RenderPresent(r);
    traceFrame(t0, t1, t2, t3, SDL_GetPerformanceCounter());
    if (pressedUntil_ && SDL_GetTicks() > pressedUntil_) pressed_ = Pressed::None, pressedUntil_ = 0;
  }

 private:
  enum class Pressed { None, Go, Back, Up, Down, Left, Right, Black, Menu };

  void loadFonts() {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(renderer_, &w, &h);
    pxW_ = static_cast<float>(w);
    pxH_ = static_cast<float>(h);
    SDL_Rect safe {0, 0, w, h};
    SDL_GetWindowSafeArea(window_, &safe);
    // The safe area is in window points; the renderer draws in pixels.
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    const float sx = ww > 0 ? pxW_ / static_cast<float>(ww) : 1.0f;
    const float sy = wh > 0 ? pxH_ / static_cast<float>(wh) : 1.0f;
    safe_ = {safe.x * sx, safe.y * sy, safe.w * sx, safe.h * sy};
    unit_ = std::min(safe_.w, safe_.h) / 72.0f;   // the short side is 72 units, turned either way
    const float u = unit_;
    const std::string data = std::string(SDL_getenv("DECKBOY_ROOT") ? SDL_getenv("DECKBOY_ROOT") : ".") + "/data/";
    text_.clear();
    for (TTF_Font* f : {pixel_, pixelSmall_, sans_, mono_, printFont_}) if (f) TTF_CloseFont(f);
    pixel_ = TTF_OpenFont((data + "PressStart2P.ttf").c_str(), 2.6f * u);
    pixelSmall_ = TTF_OpenFont((data + "PressStart2P.ttf").c_str(), 1.6f * u);
    sans_ = TTF_OpenFont((data + "fonts/LiberationSans-Regular.ttf").c_str(), 3.0f * u);
    mono_ = TTF_OpenFont((data + "fonts/LiberationMono-Regular.ttf").c_str(), 2.9f * u);
    // the faces' own print: Liberation Mono at 2 mm (pocket-skins text size 2.0)
    printFont_ = TTF_OpenFont((data + "fonts/LiberationMono-Regular.ttf").c_str(), 2.0f * u);
    if (!pixel_ || !mono_) SDL_Log("pocket: fonts missing under %s (%s)", data.c_str(), SDL_GetError());
  }

  // Where everything goes, from the phone's own shape.
  //
  // PORTRAIT: the program panel on top (its title strip carries LIVE, the
  // output, notices, the meter and the battery), the cue list taking every
  // millimetre left, the buttons at the bottom. A short phone (status bar and
  // gesture bar taking their share) gets a shorter button strip before the
  // list loses a row. Editing (a cue sheet or MENU open), the program shrinks
  // to its picture and the page takes the rest.
  //
  // LANDSCAPE (Advance-style): the pad and BLACK / MENU on the left, the
  // picture and the cue list in the middle, GO and BACK on the right.
  // Editing: the picture on the left, the page on the right.
  // The portrait strip of buttons (pad, GO, BACK, the two pills), where the
  // rendered faces draw theirs. Placed in every portrait layout: the show
  // screen uses them, and ABOUT keeps them in reach for the codes.
  void placeButtons(float H) {
    const float u = unit_, x0 = safe_.x, y0 = safe_.y;
    auto at = [&](float x, float y) { return SDL_FPoint {x0 + x * u, y0 + y * u}; };
    const float stripH = 44.0f;   // the rendered faces are drawn with this strip
    strip_ = {x0, y0 + (H - stripH) * u, safe_.w, stripH * u};
    const float cy = (H - stripH) + (stripH - 10) / 2;
    padCentre_ = at(17, cy);
    goCentre_ = at(57, cy - 5);
    backCentre_ = at(41, cy + 8);
    blackPill_ = {x0 + 18 * u, y0 + (H - 9.5f) * u, 12 * u, 4 * u};
    menuPill_ = {x0 + 36 * u, y0 + (H - 9.5f) * u, 12 * u, 4 * u};
  }

  // MENU > PHONE > ABOUT, upright: the buttons below it do nothing but listen.
  bool aboutPage() const { return menuOpen_ && menuTab_ == 6 && !landscape_; }

  void layout() {
    const float u = unit_, x0 = safe_.x, y0 = safe_.y;
    landscape_ = safe_.w > safe_.h;
    editing_ = sheet_ >= 0 || menuOpen_;
    const float W = safe_.w / u, H = safe_.h / u;   // the layout's size, in units
    auto at = [&](float x, float y) { return SDL_FPoint {x0 + x * u, y0 + y * u}; };
    if (!landscape_) {
      if (editing_) {
        const float picH = (68 - 7) * 9.0f / 16.0f;
        program_ = {x0 + 2 * u, y0 + 1.5f * u, 68 * u, (5 + 1 + picH + 1 + 2.5f) * u};
        picRect_ = {program_.x + 3.5f * u, program_.y + 6 * u, 61 * u, picH * u};
        sheetRect_ = {x0 + 2 * u, program_.y + program_.h + 1.5f * u, 68 * u, 0};
        sheetRect_.h = y0 + safe_.h - u - sheetRect_.y;
        placeButtons(H);
        if (aboutPage()) sheetRect_.h = strip_.y - u - sheetRect_.y;   // the buttons stay in reach
      } else {
        const float picH = (68 - 7) * 9.0f / 16.0f;
        // title 5, picture, name and time 4.5, scrub 4, buttons 6, margins
        program_ = {x0 + 2 * u, y0 + 1.5f * u, 68 * u, (5 + 1 + picH + 1.2f + 4.5f + 4 + 6 + 2.5f) * u};
        picRect_ = {program_.x + 3.5f * u, program_.y + 6 * u, 61 * u, picH * u};
        placeButtons(H);
        list_ = {x0 + 2 * u, program_.y + program_.h + 1.5f * u, 68 * u, 0};
        list_.h = std::max(16 * u, strip_.y - list_.y - 1.5f * u);
      }
    } else {
      if (editing_) {
        // the picture on the left at the height's use, the page on the right
        const float pw = std::min(W * 0.48f, (H - 12) * 16.0f / 9.0f + 7);
        const float picW = pw - 7, picH = picW * 9.0f / 16.0f;
        program_ = {x0 + 1.5f * u, y0 + 1.5f * u, pw * u, (5 + 1 + picH + 1 + 2.5f) * u};
        picRect_ = {program_.x + 3.5f * u, program_.y + 6 * u, picW * u, picH * u};
        sheetRect_ = {program_.x + program_.w + 1.5f * u, y0 + 1.5f * u, 0, (H - 3) * u};
        sheetRect_.w = x0 + safe_.w - 1.5f * u - sheetRect_.x;
      } else {
        // the middle: the program (title, picture, buttons) over the cue list
        const float side = 34;
        const float cw = W - 2 * side;
        const float listRows = 3;
        const float listH = listRows * 5.2f + 5;
        const float maxPicH = H - 3 - 5 - 1 - 1.2f - 6 - 2.5f - 1.5f - listH;
        const float picW = std::min(cw - 7, maxPicH * 16.0f / 9.0f);
        const float picH = picW * 9.0f / 16.0f;
        const float pw = picW + 7;
        program_ = {x0 + (W - pw) / 2 * u, y0 + 1.5f * u, pw * u, (5 + 1 + picH + 1.2f + 6 + 2.5f) * u};
        picRect_ = {program_.x + 3.5f * u, program_.y + 6 * u, picW * u, picH * u};
        list_ = {x0 + (side + 1) * u, program_.y + program_.h + 1.5f * u, (cw - 2) * u, 0};
        list_.h = y0 + safe_.h - 1.5f * u - list_.y;
        strip_ = {};
        padCentre_ = at(17, 26);
        blackPill_ = {x0 + 4 * u, y0 + (H - 13) * u, 12 * u, 4 * u};
        menuPill_ = {x0 + 18 * u, y0 + (H - 13) * u, 12 * u, 4 * u};
        goCentre_ = at(W - 13, 24);
        backCentre_ = at(W - 26, 41);
      }
    }
    header_ = {program_.x + 5 * u, program_.y + 1.2f * u, program_.w - 21 * u, 3.2f * u};
  }

  void drawProgram() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    if (!shellFace()) fillRound(r, kBezel, program_, 3 * u);
    // the title strip: the LED, then a notice (or what this phone follows, or
    // LIVE and the output), the meter and the battery at the right
    const bool live = active_ >= 0 && !black_;
    fillCircle(r, live ? Ink {255, 50, 30} : Ink {70, 20, 20}, {program_.x + 3 * u, program_.y + 2.8f * u}, 0.9f * u);
    const std::string health = healthLabel();
    int hw = 0, hh = 0;
    if (pixelSmall_ && !health.empty()) TTF_GetStringSize(pixelSmall_, health.c_str(), 0, &hw, &hh);
    const float right = program_.x + program_.w - 2.5f * u;
    if (!health.empty()) {
      text_.draw(pixelSmall_, health, thermal_ >= 3 ? Ink {230, 80, 70} : Ink {170, 170, 180},
                 {right - static_cast<float>(hw), program_.y + 1.2f * u, static_cast<float>(hw) + 1, 3.2f * u});
    }
    const float meterW = 11 * u;
    const float meterX = right - static_cast<float>(hw) - u - meterW;
    drawMeter({meterX, program_.y + 1.0f * u, meterW, 3.4f * u}, false);
    const SDL_FRect title {program_.x + 5 * u, program_.y + 1.2f * u, meterX - program_.x - 6 * u, 3.2f * u};
    if (SDL_GetTicks() < noticeUntil_) {
      text_.draw(pixelSmall_, notice_, Ink {235, 225, 160}, title);
    } else if (follow_ != Follow::Off) {
      text_.draw(pixelSmall_, followHeadline(), follow_ == Follow::Following ? Ink {170, 170, 180} : Ink {230, 80, 70}, title);
    } else {
      text_.draw(pixelSmall_, std::string(live ? "LIVE" : (black_ ? "BLACK" : "READY")) + "   " +
                                  (outputLabel_.empty() ? std::string("NO OUTPUT") : outputLabel_),
                 Ink {170, 170, 180}, title);
    }
    const SDL_FRect lcd {program_.x + 2.5f * u, program_.y + 5 * u, program_.w - 5 * u, program_.h - 7.5f * u};
    fillRect(r, kLight, lcd);
    const SDL_FRect pic = picRect_;
    fillRect(r, kBlack, pic);
    if (programTex_) {
      const SDL_FRect fitted = fitProgram(pic);
      SDL_RenderTexture(r, programTex_, nullptr, &fitted);
    }
    if (editing_) return;   // editing: the picture alone
    // the name and the time on one line, the scrub bar (portrait), the buttons
    float y = pic.y + pic.h + 1.2f * u;
    const Cue* onAir = active_ >= 0 ? &cues_[static_cast<std::size_t>(active_)] : nullptr;
    const double pos = onAir ? engine_->position() : 0.0;
    std::string clockText = onAir ? "holds until GO" : "";
    if (onAir && cueHasTimeline(*onAir)) {
      clockText = timecode(std::max(0.0, pos - cueIn(*onAir)), onAir->fps) + "  -" +
                  timecode(std::max(0.0, cueOut(*onAir) - pos), onAir->fps);
    }
    int cw = 0, ch = 0;
    if (mono_ && !clockText.empty()) TTF_GetStringSize(mono_, clockText.c_str(), 0, &cw, &ch);
    text_.draw(mono_, onAir ? onAir->name : "Nothing on air", kDeep, {lcd.x + u, y, lcd.w - 3 * u - static_cast<float>(cw), 4 * u});
    if (!clockText.empty()) {
      text_.draw(mono_, clockText, kDeep, {lcd.x + lcd.w - u - static_cast<float>(cw), y, static_cast<float>(cw) + 1, 4 * u});
    }
    y += 4.5f * u;
    if (!landscape_) {
      scrub_ = {lcd.x + u, y, lcd.w - 2 * u, 3.4f * u};
      fillRect(r, kMid, {scrub_.x, scrub_.y + 1.2f * u, scrub_.w, 1.0f * u});
      if (onAir && cueHasTimeline(*onAir)) {
        const double len = onAir->duration;
        auto xAt = [&](double t) { return scrub_.x + scrub_.w * static_cast<float>(std::clamp(t / len, 0.0, 1.0)); };
        const float xi = xAt(cueIn(*onAir)), xo = xAt(cueOut(*onAir)), xp = xAt(pos);
        fillRect(r, kDeep, {xi, scrub_.y + 1.2f * u, std::max(0.0f, xp - xi), 1.0f * u});
        fillRect(r, kInOut, {xi - 0.3f * u, scrub_.y, 0.6f * u, scrub_.h});
        fillRect(r, kInOut, {xo - 0.3f * u, scrub_.y, 0.6f * u, scrub_.h});
        fillRect(r, kDeep, {xp - 0.4f * u, scrub_.y - 0.3f * u, 0.8f * u, scrub_.h + 0.6f * u});
      }
      y += 4 * u;
    } else {
      scrub_ = {};
    }
    const bool paused = onAir && engine_->state() == TransportState::Paused;
    const bool canTrim = onAir && cueHasTimeline(*onAir);
    const float gap = 0.8f * u;
    const float small = (lcd.w - 2 * u - 4 * gap) * 0.17f;
    const float wide = (lcd.w - 2 * u - 4 * gap - 4 * small);
    float bx = lcd.x + u;
    button({bx, y, small, 5 * u}, "-10", [this] { skipBy(-10); }, canTrim); bx += small + gap;
    button({bx, y, wide, 5 * u}, paused ? "PLAY" : "PAUSE", [this, paused] { paused ? engine_->play() : engine_->pause(); }, onAir != nullptr); bx += wide + gap;
    button({bx, y, small, 5 * u}, "+10", [this] { skipBy(10); }, canTrim); bx += small + gap;
    button({bx, y, small, 5 * u}, "IN", [this] { setInOut(true); }, canTrim); bx += small + gap;
    button({bx, y, small, 5 * u}, "OUT", [this] { setInOut(false); }, canTrim);
  }

  // The program alone, filling the screen: picture-in-picture, or full screen
  // on the phone (turned on its side, so a 16:9 picture fills a portrait phone).
  void drawProgramAlone(bool turned) {
    SDL_Renderer* r = renderer_;
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(r, &w, &h);
    const float W = static_cast<float>(w), H = static_cast<float>(h);
    fillRect(r, kBlack, {0, 0, W, H});
    if (!programTex_) return;
    const bool turn = turned && H > W;
    if (turn) {
      // Rotated 90 degrees: the picture's width runs down the phone. Fitted
      // in the turned box (H x W), then rotated about its centre.
      const SDL_FRect turnedBox {(W - H) / 2, (H - W) / 2, H, W};
      const SDL_FRect dst = fitProgram(turnedBox);
      SDL_RenderTextureRotated(r, programTex_, nullptr, &dst, 90.0, nullptr, SDL_FLIP_NONE);
    } else {
      const SDL_FRect dst = fitProgram({0, 0, W, H});
      SDL_RenderTexture(r, programTex_, nullptr, &dst);
    }
    if (turned) drawGhost(W, H, turn);
  }

  // ── Ghost controls over the full-screen picture ──
  //
  // A tap in full screen shows translucent, softly lit controls floating over
  // the picture: the cue and its time, a hairline scrub, BACK, -10, PLAY /
  // PAUSE, +10, a big GO, the left pill's action, and EXIT (the only way out).
  // They fade after 3.5 s untouched. They are drawn into a layer the right way
  // up for the picture and turned with it, so they read correctly whichever
  // way the phone is held; a tap is turned back the same way to find them.

  SDL_FPoint ghostPoint(float x, float y) const {
    if (!ghostTurned_) return {x, y};
    // the layer is turned 90 degrees clockwise about the screen's centre
    const float dx = y - pxH_ / 2, dy = pxW_ / 2 - x;
    return {ghostW_ / 2 + dx, ghostH_ / 2 + dy};
  }

  void ghostTap(float x, float y) {
    const Uint64 now = SDL_GetTicks();
    if (now >= ghostUntil_) {   // hidden: the first tap only shows them
      ghostUntil_ = now + 3500;
      buzz("tap");
      return;
    }
    const SDL_FPoint p = ghostPoint(x, y);
    const float reach = 48.0f * std::max(1.0f, SDL_GetWindowDisplayScale(window_));
    std::size_t best = ghostHits_.size();
    float bestD = 1e9f;
    for (std::size_t k = 0; k < ghostHits_.size(); ++k) {
      const SDL_FRect& b = ghostHits_[k].rect;
      const float gw = std::max(0.0f, (reach - b.w) / 2), gh = std::max(0.0f, (reach - b.h) / 2);
      if (!inside({b.x - gw, b.y - gh, b.w + 2 * gw, b.h + 2 * gh}, p.x, p.y)) continue;
      const float ddx = std::max({b.x - p.x, 0.0f, p.x - (b.x + b.w)});
      const float ddy = std::max({b.y - p.y, 0.0f, p.y - (b.y + b.h)});
      if (ddx * ddx + ddy * ddy < bestD) { bestD = ddx * ddx + ddy * ddy; best = k; }
    }
    if (best < ghostHits_.size()) {
      auto fn = ghostHits_[best].fn;
      ghostPressed_ = static_cast<int>(best);
      ghostPressedAt_ = now;
      ghostUntil_ = now + 3500;
      fn();
    } else {
      ghostUntil_ = 0;   // a tap on the picture itself hides them again
    }
  }

  // The ghost's outline: rounded top corners, straight sides, and a hem of
  // scallops that ripple with `wave`. `grow` widens it all round (the halo).
  std::vector<SDL_FPoint> ghostOutline(const SDL_FRect& b0, float u, float wave, float grow) const {
    const SDL_FRect b {b0.x - grow, b0.y - grow, b0.w + 2 * grow, b0.h + 2 * grow};
    const float pi = 3.14159265f;
    const float rad = std::min(b.w / 2, b.h * 0.55f);
    std::vector<SDL_FPoint> pts;
    for (int i = 0; i <= 16; ++i) {   // top left corner, then top right
      const float a = pi + (pi / 2) * static_cast<float>(i) / 16;
      pts.push_back({b.x + rad + rad * std::cos(a), b.y + rad + rad * std::sin(a)});
    }
    for (int i = 0; i <= 16; ++i) {
      const float a = 1.5f * pi + (pi / 2) * static_cast<float>(i) / 16;
      pts.push_back({b.x + b.w - rad + rad * std::cos(a), b.y + rad + rad * std::sin(a)});
    }
    const int scallops = std::max(2, static_cast<int>(std::lround(b.w / (7 * u))));
    const float amp = std::min(1.9f * u, b.h * 0.2f);
    const int steps = scallops * 20;
    for (int k = 0; k <= steps; ++k) {   // the hem, right to left
      const float f = static_cast<float>(k) / static_cast<float>(steps);
      const float y = b.y + b.h - amp * (0.5f + 0.5f * std::cos(2 * pi * f * static_cast<float>(scallops) + wave));
      pts.push_back({b.x + b.w - b.w * f, y});
    }
    // Liquid: the body sways like jelly, still at the crown and most at the
    // hem, with the sway rolling down it.
    const float sway = std::min(1.1f * u, b.w * 0.08f);
    for (SDL_FPoint& q : pts) {
      const float down = std::clamp((q.y - b.y) / std::max(1.0f, b.h), 0.0f, 1.0f);
      q.x += sway * down * down * std::sin(wave * 0.7f - down * 2.4f);
    }
    return pts;
  }

  void ghostBlob(const SDL_FRect& b, float u, float wave, float grow, Ink ink, float alpha, float centreAlpha = -1) {
    const std::vector<SDL_FPoint> pts = ghostOutline(b, u, wave, grow);
    const SDL_FColor c {ink.r / 255.0f, ink.g / 255.0f, ink.b / 255.0f, alpha};
    const SDL_FColor mid {c.r, c.g, c.b, centreAlpha < 0 ? alpha : centreAlpha};
    std::vector<SDL_Vertex> v;
    v.reserve(pts.size() * 3);
    const SDL_FPoint centre {b.x + b.w / 2, b.y + b.h * 0.5f};
    for (std::size_t i = 0; i < pts.size(); ++i) {
      const SDL_FPoint& p0 = pts[i];
      const SDL_FPoint& p1 = pts[(i + 1) % pts.size()];
      v.push_back({centre, mid, {0, 0}});
      v.push_back({p0, c, {0, 0}});
      v.push_back({p1, c, {0, 0}});
    }
    SDL_RenderGeometry(renderer_, nullptr, v.data(), static_cast<int>(v.size()), nullptr, 0);
  }

  // Gloss: a bright crescent across the crown, inside the edge, fading out
  // at both ends, as light catches the top of a drop.
  void ghostGloss(const SDL_FRect& b, float u) {
    const float pi = 3.14159265f;
    const float rad = std::min(b.w / 2, b.h * 0.55f);
    const float inset = 0.9f * u;
    const float cx = b.x + b.w / 2, cy = b.y + rad;
    const float rx = b.w / 2 - inset, ry = rad - inset;
    std::vector<SDL_Vertex> v;
    const int n = 24;
    for (int i = 0; i < n; ++i) {
      const float f0 = static_cast<float>(i) / n, f1 = static_cast<float>(i + 1) / n;
      const float a0 = pi * (1.12f + 0.42f * f0), a1 = pi * (1.12f + 0.42f * f1);   // the upper left of the crown
      const float w0 = std::sin(pi * f0) * 0.75f * u, w1 = std::sin(pi * f1) * 0.75f * u;
      const SDL_FColor c0 {1, 1, 1, 0.8f * std::sin(pi * f0)}, c1 {1, 1, 1, 0.8f * std::sin(pi * f1)};
      const SDL_FPoint o0 {cx + rx * std::cos(a0), cy + ry * std::sin(a0)}, o1 {cx + rx * std::cos(a1), cy + ry * std::sin(a1)};
      const SDL_FPoint i0 {cx + (rx - w0) * std::cos(a0), cy + (ry - w0) * std::sin(a0)}, i1 {cx + (rx - w1) * std::cos(a1), cy + (ry - w1) * std::sin(a1)};
      for (const auto& [q, c] : {std::pair{o0, c0}, std::pair{i0, c0}, std::pair{o1, c1}, std::pair{i0, c0}, std::pair{i1, c1}, std::pair{o1, c1}})
        v.push_back({q, c, {0, 0}});
    }
    SDL_RenderGeometry(renderer_, nullptr, v.data(), static_cast<int>(v.size()), nullptr, 0);
  }

  // Melting: from each tip of the hem a drip grows, swells, and lets go as a
  // drop that falls and fades, each on its own clock (like the Melt effect).
  void ghostDrips(const SDL_FRect& b, float u, float wave, double t, float phase, float alpha) {
    const std::vector<SDL_FPoint> pts = ghostOutline(b, u, wave, 0);
    const SDL_FColor c {1, 1, 1, alpha};
    std::vector<SDL_Vertex> v;
    auto oval = [&](SDL_FPoint o, float rx, float ry, float a) {
      const SDL_FColor cc {1, 1, 1, a};
      for (int i = 0; i < 14; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / 14, a1 = 6.2831853f * static_cast<float>(i + 1) / 14;
        v.push_back({o, cc, {0, 0}});
        v.push_back({{o.x + rx * std::cos(a0), o.y + ry * std::sin(a0)}, cc, {0, 0}});
        v.push_back({{o.x + rx * std::cos(a1), o.y + ry * std::sin(a1)}, cc, {0, 0}});
      }
    };
    int tip = 0;
    for (std::size_t i = 34; i + 1 < pts.size(); ++i) {   // the hem starts after the two crown corners (17 + 17 points)
      if (!(pts[i].y >= pts[i - 1].y && pts[i].y > pts[i + 1].y)) continue;
      const float own = phase + static_cast<float>(tip) * 1.37f;
      const double cyc = t * (0.16 + 0.05 * std::sin(own)) + own * 0.159;
      const float f = static_cast<float>(cyc - std::floor(cyc));
      const SDL_FPoint o = pts[i];
      const float maxL = 2.4f * u;
      float len, bulb;
      if (f < 0.75f) {   // growing
        const float g = f / 0.75f;
        len = maxL * g * g;
        bulb = (0.25f + 0.3f * g) * u;
      } else {           // let go: the stem draws back up, the drop falls
        const float g = (f - 0.75f) / 0.25f;
        len = maxL * (1 - g) * 0.6f;
        bulb = 0.3f * u * (1 - g);
        const float fall = maxL + g * g * 9 * u;
        oval({o.x, o.y + fall}, 0.42f * u, 0.55f * u, alpha * (1 - g));
      }
      // the stem: a narrow ribbon, wide at the hem, thin at the bulb
      const float w0 = 0.55f * u, w1 = 0.2f * u;
      const SDL_FPoint a {o.x - w0, o.y - 0.3f * u}, bb {o.x + w0, o.y - 0.3f * u}, cc {o.x - w1, o.y + len}, d {o.x + w1, o.y + len};
      for (const SDL_FPoint& q : {a, bb, cc, bb, d, cc}) v.push_back({q, c, {0, 0}});
      if (bulb > 0.05f * u) oval({o.x, o.y + len}, bulb, bulb * 1.2f, alpha);
      ++tip;
    }
    if (!v.empty()) SDL_RenderGeometry(renderer_, nullptr, v.data(), static_cast<int>(v.size()), nullptr, 0);
  }

  // The bright edge: the outline as a ribbon a third of a unit wide.
  void ghostRim(const SDL_FRect& b, float u, float wave, float alpha) {
    const std::vector<SDL_FPoint> pts = ghostOutline(b, u, wave, 0);
    const SDL_FColor c {1, 1, 1, alpha};
    const float t = 0.32f * u;
    std::vector<SDL_Vertex> v;
    for (std::size_t i = 0; i < pts.size(); ++i) {
      const SDL_FPoint p0 = pts[i], p1 = pts[(i + 1) % pts.size()];
      const float dx = p1.x - p0.x, dy = p1.y - p0.y;
      const float len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
      const float nx = -dy / len * t / 2, ny = dx / len * t / 2;
      const SDL_FPoint a {p0.x + nx, p0.y + ny}, bb {p0.x - nx, p0.y - ny}, cc {p1.x + nx, p1.y + ny}, d {p1.x - nx, p1.y - ny};
      for (const SDL_FPoint& q : {a, bb, cc, bb, d, cc}) v.push_back({q, c, {0, 0}});
    }
    SDL_RenderGeometry(renderer_, nullptr, v.data(), static_cast<int>(v.size()), nullptr, 0);
  }

  void ghostOval(SDL_FPoint centre, float rx, float ry, Ink ink, float alpha) {
    const SDL_FColor c {ink.r / 255.0f, ink.g / 255.0f, ink.b / 255.0f, alpha};
    std::vector<SDL_Vertex> v;
    const int n = 20;
    for (int i = 0; i < n; ++i) {
      const float a0 = 6.2831853f * static_cast<float>(i) / n, a1 = 6.2831853f * static_cast<float>(i + 1) / n;
      v.push_back({centre, c, {0, 0}});
      v.push_back({{centre.x + rx * std::cos(a0), centre.y + ry * std::sin(a0)}, c, {0, 0}});
      v.push_back({{centre.x + rx * std::cos(a1), centre.y + ry * std::sin(a1)}, c, {0, 0}});
    }
    SDL_RenderGeometry(renderer_, nullptr, v.data(), static_cast<int>(v.size()), nullptr, 0);
  }

  void drawGhost(float W, float H, bool turn) {
    const Uint64 now = SDL_GetTicks();
    ghostHits_.clear();
    if (now >= ghostUntil_) { ghostWasShown_ = false; return; }
    SDL_Renderer* r = renderer_;
    ghostTurned_ = turn;
    ghostW_ = turn ? H : W;
    ghostH_ = turn ? W : H;
    const int tw = static_cast<int>(ghostW_), th = static_cast<int>(ghostH_);
    if (!ghostTex_ || ghostTexW_ != tw || ghostTexH_ != th) {
      if (ghostTex_) SDL_DestroyTexture(ghostTex_);
      ghostTex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, tw, th);
      ghostTexW_ = tw;
      ghostTexH_ = th;
      if (!ghostTex_) return;
    }
    SDL_Texture* previous = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, ghostTex_);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const float u = std::min(ghostW_, ghostH_) / 72.0f;   // the short side is 72 units, as everywhere
    const Ink white {255, 255, 255};
    const double secs = static_cast<double>(now) / 1000.0;
    if (!ghostWasShown_) ghostShownAt_ = now;   // just appeared: they rise into place
    ghostWasShown_ = true;
    const float rise = std::clamp(static_cast<float>(now - ghostShownAt_) / 450.0f, 0.0f, 1.0f);
    // A little ghost: round on top, a hem that ripples like something liquid,
    // and see-through, with a glowing rim, a glassy shine and dot eyes. Each
    // one bobs at its own pace and squishes like jelly when pressed. Labels
    // carry a soft shadow so they read over a bright picture.
    auto ghost = [&](const SDL_FRect& hit, const std::string& label, std::function<void()> fn, bool lit = false, bool big = false) {
      const std::size_t k = ghostHits_.size();
      // Each ghost keeps its own time: its own speed (golden-ratio spread, so
      // no two match) and its own start, and a bob of two slow waves, so they
      // never fall into step with each other.
      const float kf = static_cast<float>(k);
      const float speed = 0.7f + 0.6f * (kf * 0.618034f - std::floor(kf * 0.618034f));
      const float phase = kf * 2.39996f;
      const double t = secs * speed;
      SDL_FRect b = hit;
      b.y += static_cast<float>(std::sin(t * 0.9 + phase) * 0.65 + std::sin(t * 0.37 + phase * 1.9) * 0.35) * 1.0f * u +
             (1.0f - rise) * 4 * u;
      b.x += static_cast<float>(std::sin(t * 0.43 + phase * 0.7)) * 0.35f * u;
      if (static_cast<int>(k) == ghostPressed_ && now - ghostPressedAt_ < 520) {
        const float q = std::sin(3.14159f * static_cast<float>(now - ghostPressedAt_) / 520.0f);
        const float sx = 1 + 0.12f * q, sy = 1 - 0.14f * q;
        b = {b.x + b.w * (1 - sx) / 2, b.y + b.h * (1 - sy), b.w * sx, b.h * sy};
      }
      ghostBlob(b, u, static_cast<float>(t * 1.3) + phase, 0.9f * u, white, 0.07f);   // halo
      ghostDrips(b, u, static_cast<float>(t * 1.3) + phase, t, phase, lit ? 0.38f : 0.26f);
      ghostBlob(b, u, static_cast<float>(t * 1.3) + phase, 0.0f, white, lit ? 0.42f : 0.28f, lit ? 0.2f : 0.07f);   // body: clear middle, dense edge
      ghostRim(b, u, static_cast<float>(t * 1.3) + phase, 0.6f);
      ghostGloss(b, u);
      ghostOval({b.x + b.w * 0.74f, b.y + std::min(b.h * 0.22f, 2.4f * u)}, 0.45f * u, 0.45f * u, white, 0.85f);   // sparkle
      const float eyeY = b.y + b.h * (big ? 0.34f : 0.26f);
      const float eyeR = big ? 1.1f * u : 0.45f * u;
      if (b.h >= 8 * u) {
        const Ink ink {40, 30, 70};
        ghostOval({b.x + b.w * 0.5f - (big ? 3.2f : 1.6f) * u, eyeY}, eyeR * 0.8f, eyeR, ink, 0.85f);
        ghostOval({b.x + b.w * 0.5f + (big ? 3.2f : 1.6f) * u, eyeY}, eyeR * 0.8f, eyeR, ink, 0.85f);
        if (big) {   // blush
          ghostOval({b.x + b.w * 0.5f - 5.2f * u, eyeY + 1.6f * u}, 1.3f * u, 0.6f * u, Ink {255, 140, 180}, 0.55f);
          ghostOval({b.x + b.w * 0.5f + 5.2f * u, eyeY + 1.6f * u}, 1.3f * u, 0.6f * u, Ink {255, 140, 180}, 0.55f);
        }
      }
      const SDL_FRect words {b.x, b.h >= 8 * u ? b.y + b.h * (big ? 0.42f : 0.3f) : b.y, b.w, b.h >= 8 * u ? b.h * (big ? 0.45f : 0.62f) : b.h};
      text_.draw(big ? pixel_ : pixelSmall_, label, Ink {30, 20, 60}, {words.x + 0.2f * u, words.y + 0.2f * u, words.w, words.h}, true);
      text_.draw(big ? pixel_ : pixelSmall_, label, white, words, true);
      ghostHits_.push_back({hit, std::move(fn)});
    };
    // the cue and its time, top left; EXIT top right
    const Cue* onAir = active_ >= 0 ? &cues_[static_cast<std::size_t>(active_)] : nullptr;
    const double pos = onAir ? engine_->position() : 0.0;
    std::string title = onAir ? onAir->name : std::string("Nothing on air");
    if (onAir && cueHasTimeline(*onAir)) {
      title += "   " + timecode(std::max(0.0, pos - cueIn(*onAir)), onAir->fps) + "  -" +
               timecode(std::max(0.0, cueOut(*onAir) - pos), onAir->fps);
    }
    ghostBlob({3 * u, 3 * u, ghostW_ * 0.6f, 6 * u}, u, static_cast<float>(secs * 2.0), 0.0f, white, 0.12f);
    text_.draw(mono_, title, Ink {30, 20, 60}, {5.2f * u, 3.2f * u, ghostW_ * 0.6f - 4 * u, 6 * u});
    text_.draw(mono_, title, white, {5 * u, 3 * u, ghostW_ * 0.6f - 4 * u, 6 * u});
    ghost({ghostW_ - 22 * u, 3 * u, 19 * u, 7 * u}, "EXIT", [this] { phoneFull_ = false; ghostUntil_ = 0; });
    // the transport along the bottom, GO the biggest and furthest right
    const float by = ghostH_ - 16 * u;
    const float bh = 11 * u;
    float bx = 4 * u;
    ghost({bx, by + 1 * u, 18 * u, bh - 2 * u}, pillName(pill_), [this] { doPill(); }, pillLit());
    bx += 22 * u;
    // Gaps wide enough for each ghost's halo, sway and squish: they move on
    // their own clocks and must never touch.
    const float gap = 3.5f * u;
    const float midW = ghostW_ - bx - 32 * u;
    const float k = (midW - 3 * gap) / 5.0f;
    const bool paused = onAir && engine_->state() == TransportState::Paused;
    ghost({bx, by + 1 * u, k * 1.2f, bh - 2 * u}, "BACK", [this] { back(); buzz("back"); });
    bx += k * 1.2f + gap;
    ghost({bx, by + 1 * u, k, bh - 2 * u}, "-10", [this] { skipBy(-10); });
    bx += k + gap;
    ghost({bx, by + 1 * u, k * 1.6f, bh - 2 * u}, paused ? "PLAY" : "PAUSE",
          [this, paused] { if (active_ >= 0) { paused ? engine_->play() : engine_->pause(); } });
    bx += k * 1.6f + gap;
    ghost({bx, by + 1 * u, k, bh - 2 * u}, "+10", [this] { skipBy(10); });
    ghost({ghostW_ - 26 * u, by - 6 * u, 22 * u, 17 * u}, "GO", [this] { go(); buzz("go"); }, false, true);
    // the hairline scrub above it all
    if (onAir && cueHasTimeline(*onAir) && onAir->duration > 0) {
      const SDL_FRect line {4 * u, by - 4 * u, ghostW_ - 34 * u, 0.5f * u};
      fillRoundAlpha(r, white, 0.35f, line, 0);
      const float f = static_cast<float>(std::clamp(pos / onAir->duration, 0.0, 1.0));
      fillRoundAlpha(r, white, 0.9f, {line.x, line.y - 0.25f * u, line.w * f, u}, 0);
    }
    SDL_SetRenderTarget(r, previous);
    // fade in the last half second
    const float left = static_cast<float>(ghostUntil_ - now);
    const float alpha = std::clamp(left / 500.0f, 0.0f, 1.0f);
    SDL_SetTextureBlendMode(ghostTex_, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    SDL_SetTextureColorModFloat(ghostTex_, alpha, alpha, alpha);   // premultiplied: fade the colour with the alpha
    SDL_SetTextureAlphaModFloat(ghostTex_, alpha);
    if (turn) {
      const SDL_FRect box {(W - H) / 2, (H - W) / 2, H, W};
      SDL_RenderTextureRotated(r, ghostTex_, nullptr, &box, 90.0, nullptr, SDL_FLIP_NONE);
    } else {
      SDL_RenderTexture(r, ghostTex_, static_cast<const SDL_FRect*>(nullptr), static_cast<const SDL_FRect*>(nullptr));
    }
  }

  // DECKBOY_POCKET_PERF=1: frames per second and where the time goes
  // (engine update + pattern rebuild, program draw, output, phone UI), every
  // two seconds in the log. How the 87 ms -O0 frame was found (11-19).
  void traceFrame(Uint64 t0, Uint64 t1, Uint64 t2, Uint64 t3, Uint64 t4) {
    const char* trace = SDL_getenv("DECKBOY_POCKET_PERF");
    if (!trace || trace[0] != '1') return;
    const double f = 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    perf_[0] += (t1 - t0) * f;
    perf_[1] += (t2 - t1) * f;
    perf_[2] += (t3 - t2) * f;
    perf_[3] += (t4 - t3) * f;
    ++perfFrames_;
    const Uint64 now = SDL_GetTicks();
    if (perfSince_ == 0) perfSince_ = now;
    if (now - perfSince_ >= 2000) {
      const double n = static_cast<double>(perfFrames_);
      SDL_Log("pocketperf fps=%.1f update=%.1f program=%.1f output=%.1f ui+present=%.1f ms", n * 1000.0 / static_cast<double>(now - perfSince_),
              perf_[0] / n, perf_[1] / n, perf_[2] / n, perf_[3] / n);
      perf_[0] = perf_[1] = perf_[2] = perf_[3] = 0;
      perfFrames_ = 0;
      perfSince_ = now;
    }
  }
  double perf_[4] {};
  int perfFrames_ = 0;
  Uint64 perfSince_ = 0;

  void takePipActions() {
#if defined(SDL_PLATFORM_ANDROID)
    std::vector<int> actions;
    {
      std::lock_guard<std::mutex> hold(gPipLock);
      actions.swap(gPipActions);
    }
    for (int a : actions) {
      if (a == 1) go();
      else if (a == 2) back();
      else if (a == 3) black_ = !black_;
      else if (a == 4 && active_ >= 0) {
        engine_->state() == TransportState::Paused ? engine_->play() : engine_->pause();
      }
    }
#endif
  }

  // "Show running": tell Android (PocketActivity.setShowRunning) whenever the
  // live cue changes, so its foreground service keeps the show playing
  // behind other apps. Only on a change: the Java side posts to its thread.
  void updateShowRunning() {
#if defined(SDL_PLATFORM_ANDROID)
    // The show runs (and the phone stays awake) while a cue is live, while it
    // is held in BLACK, and while Pocket follows a desk: a long show parked in
    // black between segments must not be frozen to save battery.
    std::string title;
    if (active_ >= 0) title = cues_[static_cast<std::size_t>(active_)].name + (black_ ? " (in black)" : " live");
    else if (follow_ != Follow::Off) title = "Following " + followHost_;
    if (title == showRunningTitle_) return;
    showRunningTitle_ = title;
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env) return;
    jclass cls = env->FindClass("io/github/utopian_academy/deckboy/pocket/PocketActivity");
    if (!cls) { env->ExceptionClear(); return; }
    jmethodID m = env->GetStaticMethodID(cls, "setShowRunning", "(Ljava/lang/String;)V");
    if (m) {
      jstring t = env->NewStringUTF(title.c_str());
      env->CallStaticVoidMethod(cls, m, t);
      env->DeleteLocalRef(t);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
#endif
  }

  // Battery and heat, every five seconds: what can end a show on a phone.
  void pollHealth() {
#if defined(SDL_PLATFORM_ANDROID)
    const Uint64 now = SDL_GetTicks();
    if (now < nextHealth_) return;
    nextHealth_ = now + 5000;
    const int b = bridgeInt("battery");
    charging_ = b >= 1000;
    battery_ = b >= 1000 ? b - 1000 : b;
    thermal_ = bridgeInt("thermal");
#endif
  }

  // The header's right side: heat first (it is what slows a show down), then battery.
  std::string healthLabel() const {
    std::string s;
    if (thermal_ >= 3) s = "TOO HOT  ";
    else if (thermal_ == 2) s = "WARM  ";
    if (battery_ >= 0) s += std::to_string(battery_) + (charging_ ? "% +" : "%");
    return s;
  }

  // -- MENU --
  void drawMenu() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    const float x = sheetRect_.x, w = sheetRect_.w;
    float y = sheetRect_.y;
    const SDL_FRect panel = sheetRect_;
    fillRound(r, kBezel, panel, 3 * u);
    const SDL_FRect lcd {x + 2.5f * u, y + 2.5f * u, w - 5 * u, panel.h - 5 * u};
    fillRect(r, kLight, lcd);
    const float lx = lcd.x + u, lw = lcd.w - 2 * u;
    y = lcd.y + u;
    if (menuTab_ < 0) {   // the six tiles
      drawTiles(lcd);
      return;
    }
    {
      // a page: its name at the top, back to the tiles and CLOSE along the bottom (thumb reach)
      static const char* kNames[9] = {"SOURCES", "OUTPUT", "LIST", "SYNC", "PHONE", "SHOW", "ABOUT", "NEW CUES", "OPEN A SHOW"};
      text_.draw(pixel_, kNames[std::clamp(menuTab_, 0, 8)], kDark, {lx, y, lw, 5 * u});
      const float bh = 7 * u, by = lcd.y + lcd.h - bh - 0.5f * u;
      const float bw = (lw - u) / 2;
      const int up = menuTab_ == 6 ? 4 : (menuTab_ == 7 || menuTab_ == 8) ? 5 : -1;
      button({lx, by, bw, bh}, up == 4 ? "< PHONE" : up == 5 ? "< SHOW" : "< MENU", [this, up] { menuTab_ = up; confirmOpen_.clear(); endHostEdit(); });
      button({lx + bw + u, by, bw, bh}, "CLOSE", [this] { menuOpen_ = false; endHostEdit(); });
    }
    y += 7 * u;

    auto heading = [&](const char* t) {
      text_.draw(pixelSmall_, t, kDark, {lx, y, lw, 3.5f * u});
      y += 4.2f * u;
    };
    auto item = [&](const std::string& label, std::function<void()> fn, bool on = false) {
      button({lx, y, lw, 5 * u}, label, std::move(fn), true, on);
      y += 6 * u;
    };

    if (menuTab_ == 0) {
    heading("ADD");
    item("CLIPS, PICTURES, SOUND, PDF", [this] { menuOpen_ = false; openPicker(); });
    {
      // Made by Deckboy: one button per generator, two to a row.
      static const std::pair<const char*, const char*> kMade[] = {
          {"smpte-bars", "BARS"}, {"pocket-test", "POCKET TEST"}, {"illusion", "ILLUSION"},
          {"swirl", "SWIRL"}, {"lumia", "LUMIA"}, {"fireside", "FIRESIDE"}, {"crosshatch", "CROSSHATCH"},
          {"video-synth", "VIDEO SYNTH"}};
      std::vector<std::pair<const char*, const char*>> made(std::begin(kMade), std::end(kMade));
      if (terrariumUnlocked_) made.push_back({"terrarium", "TERRARIUM"});
      if (tabletopUnlocked_) made.push_back({"tabletop", "TABLETOP"});
      const float bw = (lw - u) / 2;
      for (int i = 0; i < static_cast<int>(made.size()); ++i) {
        const char* type = made[static_cast<std::size_t>(i)].first;
        button({lx + static_cast<float>(i % 2) * (bw + u), y, bw, 4.5f * u}, made[static_cast<std::size_t>(i)].second,
               [this, type] { if (std::string(type) == "video-synth") addVideoSynth(); else addGenerator(type); });
        if (i % 2 == 1) y += 5.5f * u;
      }
      y += made.size() % 2 == 1 ? 7 * u : 1.5f * u;
    }

    }
    if (menuTab_ == 1) {
    heading("OUTPUT");
    {
      OutputScreen& out = outputScreen();
      std::string line = "No screen connected. Plug in USB-C to HDMI.";
      {
        std::lock_guard<std::mutex> hold(out.lock);
#if defined(SDL_PLATFORM_ANDROID)
        if (out.window) {
          char b[160];
          std::snprintf(b, sizeof b, "%s  %dx%d  %.4g Hz", out.name.empty() ? "Screen" : out.name.c_str(),
                        out.width, out.height, static_cast<double>(out.refreshHz));
          line = b;
        }
#endif
      }
      text_.draw(mono_, line, kDeep, {lx, y, lw, 4.5f * u});
      y += 5.5f * u;
      const float bw = (lw - u) / 2;
      button({lx, y, bw, 5 * u}, testPattern_ ? "TEST PATTERN ON" : "TEST PATTERN", [this] { testPattern_ = !testPattern_; }, true, testPattern_);
      button({lx + bw + u, y, bw, 5 * u}, "IDENTIFY", [this] { identifyUntil_ = SDL_GetTicks() + 4000; });
      y += 6.5f * u;
      item("PHONE FULL SCREEN", [this] { menuOpen_ = false; phoneFull_ = true; ghostUntil_ = SDL_GetTicks() + 3500; });
    }

    }
    if (menuTab_ == 2) {
      drawListTab(lx, lw, y);
    }
    if (menuTab_ == 3) {
      drawSyncTab(lx, lw, y);
    }
    if (menuTab_ == 5) {
      drawShowPage(lx, lw, y);
    }
    if (menuTab_ == 7) {
      drawDefaultsPage(lx, lw, y);
    }
    if (menuTab_ == 8) {
      drawOpenPage(lx, lw, y);
    }
    if (menuTab_ == 4) {
    heading("SHELL");
    {
      const float bw = 9 * u;
      button({lx, y, bw, 5 * u}, "<", [this] { stepShell(-1); });
      text_.draw(mono_, shells_.empty() ? std::string("No shells in this build") : shellName(), kDeep,
                 {lx + bw + u, y, lw - 2 * bw - 2 * u, 5 * u}, true);
      button({lx + lw - bw, y, bw, 5 * u}, ">", [this] { stepShell(1); });
      y += 6.5f * u;
    }
    heading("SCREEN COLOURS");
    {
      const float bw = 9 * u;
      button({lx, y, bw, 5 * u}, "<", [this] { stepTheme(-1); });
      char label[96];
      std::snprintf(label, sizeof label, "%s  %d/%d", theme_.c_str(),
                    themes_.empty() ? 0 : static_cast<int>(std::find(themes_.begin(), themes_.end(), theme_) - themes_.begin()) + 1,
                    static_cast<int>(themes_.size()));
      text_.draw(mono_, label, kDeep, {lx + bw + u, y, lw - 2 * bw - 2 * u, 5 * u}, true);
      button({lx + lw - bw, y, bw, 5 * u}, ">", [this] { stepTheme(1); });
      y += 6.5f * u;
    }

    heading("LEFT BUTTON");
    {
      const float bw = 9 * u;
      button({lx, y, bw, 5 * u}, "<", [this] { setPill(static_cast<Pill>((static_cast<int>(pill_) + static_cast<int>(Pill::Count) - 1) % static_cast<int>(Pill::Count))); });
      text_.draw(mono_, pillName(pill_), kDeep, {lx + bw + u, y, lw - 2 * bw - 2 * u, 5 * u}, true);
      button({lx + lw - bw, y, bw, 5 * u}, ">", [this] { setPill(static_cast<Pill>((static_cast<int>(pill_) + 1) % static_cast<int>(Pill::Count))); });
      y += 6.5f * u;
    }
    heading("PHONE");
    button({lx, y, lw, 5 * u}, haptics_ ? "BUTTONS YOU CAN FEEL: ON" : "BUTTONS YOU CAN FEEL: OFF",
           [this] { setHaptics(!haptics_); }, true, haptics_);
    y += 6.5f * u;
    text_.draw(mono_, healthLabel().empty() ? "-" : "Battery " + healthLabel(), kDeep, {lx, y, lw, 4.5f * u});
    y += 5.5f * u;
    button({lx, y, lw, 5 * u}, "ABOUT", [this] { menuTab_ = 6; });
    }
    if (menuTab_ == 6) {
      drawAboutPage(lx, lw, y);
    }
  }

  // ── MENU's six tiles, each with its live state (board: MENU) ──
  void drawTiles(const SDL_FRect& lcd) {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    const float gap = u;
    const float bh = 7 * u;
    const float area = lcd.h - bh - 2.5f * u;
    const int cols = landscape_ ? 3 : 2, rows = landscape_ ? 2 : 3;
    const float tw = (lcd.w - 2 * u - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float th = (area - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);
    struct Tile { int page; const char* name; };
    const Tile tiles[6] = {{5, "SHOW"}, {2, "LIST"}, {1, "OUTPUT"}, {0, "SOURCES"}, {3, "SYNC"}, {4, "PHONE"}};
    for (int k = 0; k < 6; ++k) {
      const SDL_FRect t {lcd.x + u + static_cast<float>(k % cols) * (tw + gap), lcd.y + u + static_cast<float>(k / cols) * (th + gap), tw, th};
      const int page = tiles[k].page;
      // the whole tile opens its page
      fillRect(r, kMid, t);
      hits_.push_back({t, [this, page] { menuTab_ = page; buzz("tap"); }});
      text_.draw(pixel_, tiles[k].name, kDeep, {t.x + u, t.y + 0.8f * u, t.w - 2 * u, 4.5f * u});
      const SDL_FRect body {t.x + u, t.y + 6 * u, t.w - 2 * u, t.h - 7 * u};
      drawTileBody(page, body);
    }
    button({lcd.x + u, lcd.y + lcd.h - bh - 0.5f * u, lcd.w - 2 * u, bh}, "CLOSE", [this] { menuOpen_ = false; });
  }

  void drawTileBody(int page, const SDL_FRect& b) {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    const float lh = 4.2f * u;
    float y = b.y;
    auto line = [&](const std::string& t, bool strong = false) {
      if (y + lh > b.y + b.h) return;
      if (strong) fillRect(r, kDeep, {b.x, y, b.w, lh - 0.4f * u});
      text_.draw(mono_, t, strong ? kLight : kDeep, {b.x + 0.5f * u, y, b.w - u, lh});
      y += lh;
    };
    switch (page) {
      case 5:   // SHOW
        line(showName(), true);
        if (show_.decks.size() > 1) line("playlist " + std::to_string(showDeck_ + 1) + " of " + std::to_string(show_.decks.size()));
        line(std::to_string(cues_.size()) + " cues");
        break;
      case 2: {  // LIST: what is on air and what comes next
        if (active_ >= 0) line(std::to_string(active_ + 1) + " " + cues_[static_cast<std::size_t>(active_)].name, true);
        for (int k = 0, i = selected_; k < 3 && i < static_cast<int>(cues_.size()); ++k, ++i) {
          if (i == active_) { --k; continue; }
          line(std::to_string(i + 1) + " " + cues_[static_cast<std::size_t>(i)].name);
        }
        std::string modes;
        if (shuffle_) modes += "shuffle ";
        if (loopList_) modes += "loop ";
        if (!queueIds_.empty()) modes += "next " + std::to_string(queueIds_.size());
        if (!modes.empty()) line(modes);
        break;
      }
      case 1: {  // OUTPUT: the picture as it goes out
        line(outputLabel_.empty() ? "the phone" : outputLabel_);
        if (programTex_) {
          const float ph = std::min(b.h - (y - b.y), b.w * 9.0f / 16.0f);
          const SDL_FRect pic {b.x, y, ph * 16.0f / 9.0f, ph};
          fillRect(r, kBlack, pic);
          const SDL_FRect fitted = fitProgram(pic);
          SDL_RenderTexture(r, programTex_, nullptr, &fitted);
        }
        break;
      }
      case 0:   // SOURCES
        line("files from the phone");
        line("made by Deckboy");
        line(std::to_string(cues_.size()) + " in the list");
        break;
      case 3:   // SYNC
        if (follow_ == Follow::Off) {
          line("not following");
        } else {
          line(followHost_);
          line(followHeadline(), follow_ != Follow::Following);
          if (follow_ == Follow::Following) {
            char d[48];
            std::snprintf(d, sizeof d, "%.1f frames", driftFrames_);
            line(d);
          }
        }
        break;
      case 4: {  // PHONE
        line(shellName());
        line(std::string("left: ") + pillName(pill_));
        line(std::string("feel ") + (haptics_ ? "on" : "off"));
        const std::string health = healthLabel();
        if (!health.empty()) line(health);
        break;
      }
      default:
        break;
    }
  }

  // ── What a new cue starts with (the list's defaults) ──
  struct Defaults {
    double fadeIn = 0.0, fadeOut = 0.0, still = 0.0;   // still: 0 = until GO
    EndMode end = EndMode::Hold;
  };

  void applyDefaults(Cue& c) const {
    c.fadeInSeconds = defaults_.fadeIn;
    c.fadeOutSeconds = c.kind == CueKind::Image ? 0.0 : defaults_.fadeOut;   // a held still never dips to black
    if (c.kind == CueKind::Image || c.kind == CueKind::Pattern) c.stillDurationSeconds = defaults_.still;
    setEndMode(c, defaults_.end);
  }

  void saveDefaults() {
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    std::ostringstream o;
    o << "fade_in\t" << defaults_.fadeIn << "\nfade_out\t" << defaults_.fadeOut << "\nstill\t" << defaults_.still
      << "\nend\t" << static_cast<int>(defaults_.end) << "\n";
    const std::string text = o.str();
    SDL_SaveFile((std::string(state ? state : ".") + "/pocket_defaults.txt").c_str(), text.data(), text.size());
  }

  void loadDefaults() {
    size_t n = 0;
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    char* saved = static_cast<char*>(SDL_LoadFile((std::string(state ? state : ".") + "/pocket_defaults.txt").c_str(), &n));
    if (!saved) return;
    std::istringstream in(std::string(saved, n));
    SDL_free(saved);
    std::string key;
    double v = 0;
    while (in >> key >> v) {
      if (key == "fade_in") defaults_.fadeIn = std::clamp(v, 0.0, 10.0);
      else if (key == "fade_out") defaults_.fadeOut = std::clamp(v, 0.0, 10.0);
      else if (key == "still") defaults_.still = std::clamp(v, 0.0, 120.0);
      else if (key == "end") defaults_.end = static_cast<EndMode>(std::clamp(static_cast<int>(v), 0, 2));
    }
  }

  // MENU > SHOW: what new cues start with. (Opening and saving .deckboy
  // shows joins this page with the desk's own show code.)
  void drawDefaultsPage(float lx, float lw, float& y) {
    const float u = unit_;
    text_.draw(pixelSmall_, "NEW CUES START WITH", kDark, {lx, y, lw, 3.5f * u});
    y += 4.5f * u;
    const float rowH = 9 * u;
    auto secs = [this](const char* label, double Defaults::*field, double max) {
      NumSpec n;
      n.label = label;
      n.unit = "s";
      n.decimals = 1;
      n.min = 0;
      n.max = max;
      n.step = 0.1;
      n.presets = {{"NONE", 0}, {"HALF", 0.5}, {"1 S", 1}};
      n.get = [this, field] { return defaults_.*field; };
      n.set = [this, field](double v) { defaults_.*field = v; saveDefaults(); };
      return n;
    };
    numberRow({lx, y, lw, rowH}, secs("FADE IN", &Defaults::fadeIn, 10));
    y += rowH;
    numberRow({lx, y, lw, rowH}, secs("FADE OUT", &Defaults::fadeOut, 10));
    y += rowH;
    {
      NumSpec n = secs("STILLS FOR", &Defaults::still, 120);
      n.zeroText = "until GO";
      n.presets = {{"UNTIL GO", 0}, {"5 S", 5}, {"10 S", 10}};
      numberRow({lx, y, lw, rowH}, n);
      y += rowH;
    }
    text_.draw(pixelSmall_, "AT THE END", kDark, {lx, y + u, lw, 3.5f * u});
    y += 4.5f * u;
    auto setEnd = [this](EndMode m) { return [this, m] { defaults_.end = m; saveDefaults(); }; };
    chipRow({lx, y, lw, 7 * u}, {{"HOLD", setEnd(EndMode::Hold)}, {"LOOP", setEnd(EndMode::Loop)}, {"NEXT", setEnd(EndMode::Next)}},
            {defaults_.end == EndMode::Hold, defaults_.end == EndMode::Loop, defaults_.end == EndMode::Next});
    y += 8 * u;
    chipRow({lx, y, lw, 7 * u}, {{"GIVE THESE TO EVERY CUE", [this] {
                                    for (Cue& c : cues_) applyDefaults(c);
                                    if (active_ >= 0) cueEdited(active_, false);
                                    notice_ = "Every cue now starts as the defaults say";
                                    noticeUntil_ = SDL_GetTicks() + 2500;
                                  }}}, {false});
    y += 8 * u;
  }

  // ── Shows: the desk's .deckboy files, read and written by the desk's own
  // code (deckboy::showfile) ──
  //
  // Pocket plays one playlist of a show at a time. show_ keeps the whole show
  // (every playlist, its outputs, its settings), so saving writes back what
  // Pocket did not touch exactly as it came; cues_ is the playlist in hand.
  // The list in hand is also saved every few seconds (state/pocket_list.deckboy)
  // and comes back when Pocket starts again.

  static constexpr int kNamingShow = -2;   // renameIndex_: the name editor is naming the show

  std::string showName() const {
    if (showFile_.empty()) return cues_.empty() ? "New list" : "Unsaved list";
    const std::string f = showFile_.substr(showFile_.find_last_of("/\\") + 1);
    return f.substr(0, f.rfind('.'));
  }

  // The show as it stands: the playlist in hand written back into the rest.
  Project currentShow() const {
    Project p = show_;
    if (p.decks.empty()) p.decks.resize(1);
    const std::size_t d = static_cast<std::size_t>(std::clamp(showDeck_, 0, static_cast<int>(p.decks.size()) - 1));
    p.decks[d].cues = cues_;
    p.decks[d].selectedIndex = cues_.empty() ? -1 : selected_;
    p.focusedDeckIndex = static_cast<int>(d);
    return p;
  }

  std::string statePath(const char* name) const {
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    return std::string(state ? state : ".") + "/" + name;
  }

  // Only when something changed: the bytes are compared with the last save.
  void autosaveList() {
    const std::string path = statePath("pocket_list.deckboy");
    const std::string tmp = path + ".tmp";
    if (!deckboy::showfile::save(tmp, currentShow())) return;
    size_t n = 0;
    char* bytes = static_cast<char*>(SDL_LoadFile(tmp.c_str(), &n));
    if (!bytes) return;
    std::string now(bytes, n);
    SDL_free(bytes);
    if (now == lastAutosave_) {
      SDL_RemovePath(tmp.c_str());
      return;
    }
    SDL_RemovePath(path.c_str());
    if (SDL_RenamePath(tmp.c_str(), path.c_str())) lastAutosave_ = std::move(now);
    const std::string where = "file\t" + showFile_ + "\ndeck\t" + std::to_string(showDeck_) + "\n";
    SDL_SaveFile(statePath("pocket_show.txt").c_str(), where.data(), where.size());
  }

  // At start: the list as it was left, with anything new in the media folder after it.
  void restoreList() {
    const std::string path = statePath("pocket_list.deckboy");
    std::error_code ec;
    if (!dbfs::exists(dbfs::path(path), ec)) return;
    const std::vector<Cue> folder = cues_;
    show_ = deckboy::showfile::load(path);
    size_t n = 0;
    if (char* saved = static_cast<char*>(SDL_LoadFile(statePath("pocket_show.txt").c_str(), &n))) {
      std::istringstream in(std::string(saved, n));
      SDL_free(saved);
      std::string line;
      while (std::getline(in, line)) {
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        if (line.compare(0, tab, "file") == 0) showFile_ = line.substr(tab + 1);
        if (line.compare(0, tab, "deck") == 0) showDeck_ = std::atoi(line.c_str() + tab + 1);
      }
    }
    takePlaylist(std::clamp(showDeck_, 0, std::max(0, static_cast<int>(show_.decks.size()) - 1)));
    for (const Cue& c : folder) {
      const bool known = std::any_of(cues_.begin(), cues_.end(), [&](const Cue& k) { return k.path == c.path; });
      if (!known) cues_.push_back(c);
    }
    size_t m = 0;
    if (char* bytes = static_cast<char*>(SDL_LoadFile(path.c_str(), &m))) {
      lastAutosave_.assign(bytes, m);
      SDL_free(bytes);
    }
  }

  // A file the show names that is not on this phone: the same file name in
  // Pocket's media folder stands in (a show made on the desk names the
  // desk's own disks).
  void relinkMedia(Cue& c) const {
    const std::string& p = c.path;
    if (p.empty() || p.find("://") != std::string::npos) return;   // made by Deckboy, a stream, a phone link
    std::error_code ec;
    if (dbfs::exists(dbfs::path(p), ec)) return;
    const std::string file = p.substr(p.find_last_of("/\\") + 1);
    const std::string here = joinPath(mediaDir_, file);
    if (dbfs::exists(dbfs::path(here), ec)) c.path = here;
  }

  // The playlist in hand becomes playlist `d` of the open show.
  void takePlaylist(int d) {
    if (show_.decks.empty()) show_.decks.resize(1);
    showDeck_ = std::clamp(d, 0, static_cast<int>(show_.decks.size()) - 1);
    cues_ = show_.decks[static_cast<std::size_t>(showDeck_)].cues;
    for (Cue& c : cues_) {
      relinkMedia(c);
      if (c.id.empty()) c.id = "pocket-" + std::to_string(++nextId_);
      if (c.id.rfind("pocket-", 0) == 0) nextId_ = std::max(nextId_, std::atoi(c.id.c_str() + 7));
    }
    engine_->clear();
    active_ = -1;
    queueIds_.clear();
    listEnded_ = false;
    endHandled_ = false;
    undoCue_.reset();
    sheet_ = -1;
    scroll_ = 0;
    selected_ = 0;
    const int sel = show_.decks[static_cast<std::size_t>(showDeck_)].selectedIndex;
    if (sel >= 0 && sel < static_cast<int>(cues_.size())) selected_ = sel;
    if (shuffle_) refillShuffle(-1);
  }

  void switchPlaylist(int by) {
    if (show_.decks.size() < 2) return;
    show_ = currentShow();   // keep what was done to this one
    const int n = static_cast<int>(show_.decks.size());
    takePlaylist(((showDeck_ + by) % n + n) % n);
    notice_ = "Playlist " + std::to_string(showDeck_ + 1) + ": " + show_.decks[static_cast<std::size_t>(showDeck_)].name;
    noticeUntil_ = SDL_GetTicks() + 2500;
  }

  bool saveShowTo(const std::string& path) {
    const Project p = currentShow();
    if (!deckboy::showfile::save(path, p)) {
      notice_ = "Could not save the show";
      noticeUntil_ = SDL_GetTicks() + 4000;
      return false;
    }
    show_ = p;
    showFile_ = path;
    autosaveList();
    return true;
  }

  void saveShow() {
    if (showFile_.empty()) { beginSaveAs(); return; }
    if (saveShowTo(showFile_)) {
      notice_ = "Saved " + showName();
      noticeUntil_ = SDL_GetTicks() + 2500;
      buzz("go");
    }
  }

  void beginSaveAs() {
    endHostEdit();
    renameIndex_ = kNamingShow;
    renameText_ = showFile_.empty() ? std::string() : showName();
    SDL_StartTextInput(window_);
  }

  void saveShowAs(std::string name) {
    for (char& ch : name) if (std::strchr("/\\:*?\"<>|", ch)) ch = '-';
    const std::string path = joinPath(showsDir_, name + ".deckboy");
    show_.title = name;
    if (saveShowTo(path)) {
      notice_ = "Saved as " + name;
      noticeUntil_ = SDL_GetTicks() + 2500;
      buzz("go");
    }
  }

  // Opening replaces the list in hand: a saved show is saved first, and an
  // unsaved list asks (OPEN pressed twice).
  void openShow(const std::string& path) {
    std::error_code ec;
    if (!dbfs::exists(dbfs::path(path), ec)) {
      notice_ = "That show is not there any more";
      noticeUntil_ = SDL_GetTicks() + 3000;
      return;
    }
    if (!showFile_.empty() && showFile_ != path) saveShowTo(showFile_);
    show_ = deckboy::showfile::load(path);
    showFile_ = path;
    takePlaylist(show_.focusedDeckIndex);
    confirmOpen_.clear();
    menuOpen_ = false;
    notice_ = "Opened " + showName() + (show_.decks.size() > 1 ? " (" + std::to_string(show_.decks.size()) + " playlists)" : "");
    noticeUntil_ = SDL_GetTicks() + 3000;
    buzz("go");
    autosaveList();
  }

  void newShow() {
    if (!showFile_.empty()) saveShowTo(showFile_);
    show_ = Project {};
    show_.decks.resize(1);
    show_.decks[0].name = "Pocket";
    showFile_.clear();
    takePlaylist(0);
    cues_.clear();
    confirmNew_ = false;
    notice_ = "A new list";
    noticeUntil_ = SDL_GetTicks() + 2500;
    autosaveList();
  }

  std::vector<std::string> savedShows() const {
    std::vector<std::pair<dbfs::file_time_type, std::string>> found;
    std::error_code ec;
    for (const auto& e : dbfs::directory_iterator(dbfs::path(showsDir_), ec)) {
      if (e.is_regular_file(ec) && endsWithNoCase(e.path().string(), ".deckboy")) found.push_back({e.last_write_time(ec), e.path().string()});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });   // newest first
    std::vector<std::string> out;
    for (auto& f : found) out.push_back(std::move(f.second));
    return out;
  }

  // A show from anywhere on the phone (Downloads, a drive, a message): copied
  // into Pocket's shows folder, then opened.
  void importShow(const std::string& uri) {
    std::string name;
#if defined(SDL_PLATFORM_ANDROID)
    name = bridgeString("displayName", uri);
#endif
    if (name.empty()) name = uri.substr(uri.find_last_of("/\\") + 1);
    if (!endsWithNoCase(name, ".deckboy")) {
      notice_ = name + " is not a Deckboy show";
      noticeUntil_ = SDL_GetTicks() + 3500;
      return;
    }
    size_t n = 0;
    char* bytes = static_cast<char*>(SDL_LoadFile(uri.c_str(), &n));
    if (!bytes) {
      notice_ = "Could not read " + name;
      noticeUntil_ = SDL_GetTicks() + 3500;
      return;
    }
    const std::string path = joinPath(showsDir_, name);
    const bool ok = SDL_SaveFile(path.c_str(), bytes, n);
    SDL_free(bytes);
    if (ok) openShow(path);
  }

  void openShowPicker() {
    {
      std::lock_guard<std::mutex> hold(pickLock_);
      if (pickerOpen_) return;
      pickerOpen_ = true;
      pickingShow_ = true;
    }
    SDL_ShowOpenFileDialog(&Pocket::picked, this, window_, nullptr, 0, nullptr, false);
  }

  void drawShowPage(float lx, float lw, float& y) {
    const float u = unit_;
    text_.draw(pixel_, showName(), kDeep, {lx, y, lw, 5 * u});
    y += 5.5f * u;
    text_.draw(mono_, std::to_string(cues_.size()) + (cues_.size() == 1 ? " cue" : " cues") +
                          (showFile_.empty() ? std::string(", not saved yet") : std::string()),
               kDark, {lx, y, lw, 4.5f * u});
    y += 6 * u;
    if (show_.decks.size() > 1) {
      text_.draw(pixelSmall_, "PLAYLIST", kDark, {lx, y, lw, 3.5f * u});
      y += 4.2f * u;
      const float bw = 9 * u;
      button({lx, y, bw, 6 * u}, "<", [this] { switchPlaylist(-1); });
      text_.draw(mono_, std::to_string(showDeck_ + 1) + "/" + std::to_string(show_.decks.size()) + "  " +
                            show_.decks[static_cast<std::size_t>(showDeck_)].name,
                 kDeep, {lx + bw + u, y, lw - 2 * bw - 2 * u, 6 * u}, true);
      button({lx + lw - bw, y, bw, 6 * u}, ">", [this] { switchPlaylist(1); });
      y += 7.5f * u;
    }
    const float bw = (lw - u) / 2;
    button({lx, y, bw, 7 * u}, "SAVE", [this] { saveShow(); });
    button({lx + bw + u, y, bw, 7 * u}, "SAVE AS", [this] { beginSaveAs(); });
    y += 8 * u;
    button({lx, y, bw, 7 * u}, "OPEN", [this] { menuTab_ = 8; confirmOpen_.clear(); });
    button({lx + bw + u, y, bw, 7 * u}, confirmNew_ ? "SURE? NEW" : "NEW",
           [this] { if (confirmNew_ || (showFile_.empty() && cues_.empty()) || !showFile_.empty()) newShow(); else confirmNew_ = true; },
           true, false, confirmNew_);
    y += 9 * u;
    button({lx, y, lw, 7 * u}, "NEW CUES START WITH  >", [this] { menuTab_ = 7; });
    y += 8 * u;
  }

  void drawOpenPage(float lx, float lw, float& y) {
    const float u = unit_;
    button({lx, y, lw, 7 * u}, "FROM THE PHONE'S FILES", [this] { openShowPicker(); });
    y += 8.5f * u;
    text_.draw(pixelSmall_, "IN POCKET", kDark, {lx, y, lw, 3.5f * u});
    y += 4.5f * u;
    const std::vector<std::string> shows = savedShows();
    if (shows.empty()) text_.draw(mono_, "No saved shows yet.", kDark, {lx, y, lw, 4.5f * u});
    const bool unsaved = showFile_.empty() && !cues_.empty();
    for (const std::string& path : shows) {
      if (y + 6.5f * u > sheetRect_.y + sheetRect_.h - 12 * u) break;   // room left above < SHOW / CLOSE
      std::string name = path.substr(path.find_last_of("/\\") + 1);
      name = name.substr(0, name.rfind('.'));
      const bool asking = confirmOpen_ == path;
      button({lx, y, lw, 6 * u}, asking ? "THE LIST IS NOT SAVED. OPEN " + name + "?" : name,
             [this, path, unsaved] { if (!unsaved || confirmOpen_ == path) openShow(path); else confirmOpen_ = path; },
             true, path == showFile_, asking);
      y += 7 * u;
    }
  }

  // The desk's Video Synth (oscillators, feedback, mirroring), as the desk
  // makes it: its own cue kind, not a pattern; the engine draws it.
  void addVideoSynth() {
    Cue cue;
    cue.kind = CueKind::VideoSynth;
    cue.path = "vsynth://";
    cue.name = "Video Synth";
    cue.width = programW_;
    cue.height = programH_;
    cue.color = {90, 50, 120, 255};
    cue.formatName = "generated";
    cue.pauseOnLastFrame = true;
    cue.stillDurationSeconds = 0.0;
    cue.id = "pocket-" + std::to_string(++nextId_);
    applyDefaults(cue);   // what new cues start with (MENU > SHOW)
    const int at = std::min(selected_ + 1, static_cast<int>(cues_.size()));
    cues_.insert(cues_.begin() + at, cue);
    if (active_ >= at) ++active_;
    select(at);
    notice_ = "Added Video Synth";
    noticeUntil_ = SDL_GetTicks() + 2500;
  }

  // MENU > PHONE > ABOUT: what this is, and the way to the big Deckboy.
  void drawAboutPage(float lx, float lw, float& y) {
    const float u = unit_;
    text_.draw(pixel_, std::string("DECKBOY POCKET ") + deckboy::core::version::kVersion, kDeep, {lx, y, lw, 5 * u});
    y += 6 * u;
    text_.draw(mono_, "Free software: GPL-3.0 or later.", kDark, {lx, y, lw, 4.5f * u});
    y += 4.5f * u;
    text_.draw(mono_, "The phone half of Deckboy.", kDark, {lx, y, lw, 4.5f * u});
    y += 5.5f * u;
    const float bw = (lw - 2 * u) / 3;
    button({lx, y, bw, 6 * u}, "DECKBOY", [] { SDL_OpenURL("https://utopian-academy.github.io/Deckboy/"); });
    button({lx + bw + u, y, bw, 6 * u}, "MANUAL", [] { SDL_OpenURL("https://utopian-academy.github.io/Deckboy/manual"); });
    button({lx + 2 * (bw + u), y, bw, 6 * u}, "SOURCE", [] { SDL_OpenURL("https://github.com/Utopian-Academy/Deckboy"); });
    y += 7 * u;
  }

  void addGenerator(const char* type) {
    Cue cue;
    cue.kind = CueKind::Pattern;
    cue.path = std::string("pattern://") + type;
    const std::string generated = generatorPatternName(type);
    const std::string t = type;
    cue.name = !generated.empty() ? generated : t == "smpte-bars" ? "SMPTE colour bars" : t == "pocket-test" ? "Pocket Test"
             : t == "terrarium" ? "Terrarium" : t == "tabletop" ? "Tabletop" : t;
    cue.width = programW_;
    cue.height = programH_;
    cue.formatName = "generated";
    cue.pauseOnLastFrame = true;
    cue.stillDurationSeconds = 0.0;
    cue.hasAudio = cue.audioEnabled = (std::string(type) == "pocket-test");
    cue.id = "pocket-" + std::to_string(++nextId_);
    applyDefaults(cue);   // what new cues start with (MENU > SHOW)
    const int at = std::min(selected_ + 1, static_cast<int>(cues_.size()));
    cues_.insert(cues_.begin() + at, cue);
    if (active_ >= at) ++active_;
    select(at);
    notice_ = "Added " + cue.name;
    noticeUntil_ = SDL_GetTicks() + 2500;
  }

  // A button on the LCD: drawn, and registered for this frame's taps.
  void button(const SDL_FRect& b, const std::string& label, std::function<void()> fn, bool enabled = true,
              bool on = false, bool warn = false) {
    SDL_Renderer* r = renderer_;
    // ON reads as lit: the button turns light with a heavy dark frame, the
    // opposite of its neighbours, rather than a shade that is hard to tell.
    if (on && enabled) {
      fillRound(r, kDeep, b, 0.8f * unit_);
      const float t = 0.5f * unit_;
      fillRound(r, kLight, {b.x + t, b.y + t, b.w - 2 * t, b.h - 2 * t}, 0.5f * unit_);
      text_.draw(pixelSmall_, label, kDeep, b, true);
    } else {
      const Ink face = !enabled ? kMid : warn ? Ink {122, 46, 46} : kDeep;
      fillRound(r, face, b, 0.8f * unit_);
      text_.draw(pixelSmall_, label, enabled ? kLight : kDark, b, true);
    }
    if (enabled) hits_.push_back({b, std::move(fn)});
  }

  // SET IN / SET OUT: the playhead's frame on the cue on air.
  void setInOut(bool in) {
    if (active_ < 0) return;
    Cue& c = cues_[static_cast<std::size_t>(active_)];
    if (!cueHasTimeline(c)) return;
    const double f = cueFrame(c);
    const double at = std::floor(engine_->position() / f + 1e-6) * f;
    if (in) {
      c.inPointSeconds = std::clamp(at, 0.0, cueOut(c) - f);
    } else {
      c.outPointSeconds = std::clamp(at, cueIn(c) + f, c.duration);
    }
    cueEdited(active_, true);
    notice_ = (in ? "IN " : "OUT ") + timecode(at, c.fps);
    noticeUntil_ = SDL_GetTicks() + 2000;
  }

  // After any edit: the cue on air takes it now (trim without a restart).
  void cueEdited(int i, bool trim) {
    if (i != active_ || i < 0) return;
    const Cue& c = cues_[static_cast<std::size_t>(i)];
    engine_->syncActiveCueSnapshot(c);
    if (trim) engine_->applyActiveCueTrimEdit(c);
  }

  // A cue set to NEXT takes what is selected when it ends (the next cue, or
  // what the queue, the shuffle or the loop chose) -- unless STOP AFTER THIS
  // was asked for, or the list has run out. Handled once per take.
  void followEnd() {
    if (active_ < 0 || endHandled_) return;
    const Cue& c = cues_[static_cast<std::size_t>(active_)];
    if (endModeOf(c) != EndMode::Next || !engine_->reachedEnd()) return;
    endHandled_ = true;
    if (endAfter_) {
      endAfter_ = false;
      notice_ = "Stopped after that cue, as asked";
      noticeUntil_ = SDL_GetTicks() + 2500;
      return;
    }
    if (listEnded_ && selected_ == active_) return;   // the end of the list: hold here
    take(selected_);
    advanceFrom(active_);
  }

  // The swiped row: DELETE grows in from the right under the finger, and
  // fills the row once letting go would delete it.
  void drawSwipe(const SDL_FRect& row, bool isLive) {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    const float w = std::min(row.w, -swipeDx_);
    const bool armed = -swipeDx_ >= kSwipeDelete * u;
    const SDL_FRect bar {row.x + row.w - w, row.y, w, row.h};
    fillRect(r, armed ? kDeep : kDark, bar);
    text_.draw(pixelSmall_, isLive ? "ON AIR" : armed ? "LET GO TO DELETE" : "DELETE", kLight,
               {bar.x + u, bar.y, std::max(0.0f, bar.w - 2 * u), bar.h}, true);
  }

  // After a delete: the cue's name and UNDO along the foot of the list.
  void drawUndoBar(const SDL_FRect& lcd) {
    if (!undoCue_ || SDL_GetTicks() >= undoUntil_) return;
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    const float h = 6.5f * u;
    const SDL_FRect bar {lcd.x, lcd.y + lcd.h - h, lcd.w, h};
    fillRect(r, kDeep, bar);
    text_.draw(mono_, "Deleted " + undoCue_->name, kLight, {bar.x + 1.5f * u, bar.y, bar.w - 22 * u, h});
    button({bar.x + bar.w - 19 * u, bar.y + 0.75f * u, 18 * u, h - 1.5f * u}, "UNDO", [this] { undoDelete(); });
  }

  void drawCueList() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    if (!shellFace()) fillRound(r, kBezel, list_, 3 * u);
    const SDL_FRect lcd {list_.x + 2.5f * u, list_.y + 2.5f * u, list_.w - 5 * u, list_.h - 5 * u};
    fillRect(r, kLight, lcd);
    rowH_ = 5.2f * u;
    const int visible = std::max(1, static_cast<int>(lcd.h / rowH_));
    // keep the selection in view
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visible) scroll_ = selected_ - visible + 1;
    listLcd_ = lcd;
    SDL_SetRenderClipRect(r, nullptr);
    if (cues_.empty()) {
      text_.draw(mono_, "No cues yet.", kDark, {lcd.x + 2 * u, lcd.y + 2 * u, lcd.w - 4 * u, 5 * u});
      text_.draw(mono_, "MENU > SOURCES adds them.", kDark, {lcd.x + 2 * u, lcd.y + 7 * u, lcd.w - 4 * u, 5 * u});
    }
    for (int i = scroll_; i < static_cast<int>(cues_.size()) && i < scroll_ + visible; ++i) {
      const SDL_FRect row {lcd.x, lcd.y + static_cast<float>(i - scroll_) * rowH_, lcd.w, rowH_};
      const bool isLive = (i == active_);
      const bool isSel = (i == selected_);
      // LIVE is the row filled dark. SELECTED (what GO takes) is filled
      // mid-green with a solid bar down its left edge and a heavy frame, so
      // it cannot be mistaken for anything else -- a hairline was invisible.
      if (isLive) fillRect(r, kDeep, row);
      if (isSel) {
        const float t = 0.55f * u;
        if (!isLive) fillRect(r, kMid, row);
        const Ink frame = isLive ? kMid : kDeep;
        fillRect(r, frame, {row.x, row.y, row.w, t});
        fillRect(r, frame, {row.x, row.y + row.h - t, row.w, t});
        fillRect(r, frame, {row.x + row.w - t, row.y, t, row.h});
        fillRect(r, frame, {row.x, row.y, 1.3f * u, row.h});
      }
      const Ink ink = isLive ? kLight : kDeep;
      const Cue& c = cues_[static_cast<std::size_t>(i)];
      // Secondary ink: soft on the plain rows, full strength on the mid-green
      // selected row, where the soft ink would sink into the fill.
      const Ink soft = isLive ? kMid : isSel ? kDeep : kDark;
      text_.draw(mono_, std::to_string(i + 1), isSel && !isLive ? kLight : soft, {row.x + 1.5f * u, row.y, 5 * u, row.h});
      text_.draw(mono_, c.name, ink, {row.x + 7 * u, row.y, row.w - 30 * u, row.h});
      const int qp = queuePlace(c);
      text_.draw(pixelSmall_, qp ? "NEXT " + std::to_string(qp) : std::string(kindTag(c)), soft,
                 {row.x + row.w - 22.5f * u, row.y, 13 * u, row.h}, true);
      const EndMode m = endModeOf(c);
      std::string right = m == EndMode::Loop ? "LOOP" : m == EndMode::Next ? "NEXT" : "HOLD";
      const bool swiped = swiping_ && i == swipeRow_;
      if (cueHasTimeline(c) && m != EndMode::Loop) {
        const int secs = static_cast<int>(std::lround(cueOut(c) - cueIn(c)));
        char len[16];
        std::snprintf(len, sizeof len, "%d:%02d", secs / 60, secs % 60);
        right = m == EndMode::Next ? std::string(len) + ">" : len;
      }
      text_.draw(mono_, right, ink, {row.x + row.w - 9 * u, row.y, 8.5f * u, row.h});
      if (swiped) drawSwipe(row, isLive);
    }
    drawUndoBar(lcd);
  }

  void drawControls() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    if (shellTex_ && !landscape_ && (!editing_ || aboutPage())) {
      drawShellPresses();
      return;
    }
    // the pad: up/down pick a cue, left/right are BACK/GO (as a clicker's are)
    const float arm = 13 * u, half = 4.6f * u;
    const SDL_FPoint c = padCentre_;
    fillRound(r, kShellShade, {c.x - half - 0.8f * u, c.y - arm - 0.8f * u, 2 * half + 1.6f * u, 2 * arm + 1.6f * u}, 1.6f * u);
    fillRound(r, kShellShade, {c.x - arm - 0.8f * u, c.y - half - 0.8f * u, 2 * arm + 1.6f * u, 2 * half + 1.6f * u}, 1.6f * u);
    fillRound(r, kPad, {c.x - half, c.y - arm, 2 * half, 2 * arm}, 1.2f * u);
    fillRound(r, kPad, {c.x - arm, c.y - half, 2 * arm, 2 * half}, 1.2f * u);
    const Ink arrow {78, 78, 86}, lit {150, 150, 160};
    auto tri = [&](float ax, float ay, float dx, float dy, bool on) {
      const float s = 2.0f * u;
      fillOutline(r, on ? lit : arrow,
                  {{ax + dx * s, ay + dy * s}, {ax - dy * s * 0.8f - dx * s * 0.4f, ay + dx * s * 0.8f - dy * s * 0.4f},
                   {ax + dy * s * 0.8f - dx * s * 0.4f, ay - dx * s * 0.8f - dy * s * 0.4f}},
                  {ax, ay});
    };
    tri(c.x, c.y - 8.5f * u, 0, -1, pressed_ == Pressed::Up);
    tri(c.x, c.y + 8.5f * u, 0, 1, pressed_ == Pressed::Down);
    tri(c.x - 8.5f * u, c.y, -1, 0, pressed_ == Pressed::Left);
    tri(c.x + 8.5f * u, c.y, 1, 0, pressed_ == Pressed::Right);
    // GO and BACK
    fillCircle(r, kShellShade, goCentre_, 9.0f * u);
    fillCircle(r, pressed_ == Pressed::Go ? kButtonLit : kButton, goCentre_, 8.0f * u);
    fillCircle(r, kShellShade, backCentre_, 7.3f * u);
    fillCircle(r, pressed_ == Pressed::Back ? kButtonLit : kButton, backCentre_, 6.4f * u);
    text_.draw(pixel_, "GO", {240, 225, 232}, {goCentre_.x - 8 * u, goCentre_.y - 3 * u, 16 * u, 6 * u}, true);
    text_.draw(pixelSmall_, "BACK", {240, 225, 232}, {backCentre_.x - 6 * u, backCentre_.y - 2 * u, 12 * u, 4 * u}, true);
    // BLACK and MENU
    fillRound(r, pillLit() || pressed_ == Pressed::Black ? kDark : kPill, blackPill_, 2 * u);
    fillRound(r, pressed_ == Pressed::Menu ? kDark : kPill, menuPill_, 2 * u);
    text_.draw(pixelSmall_, pillName(pill_), kPrint, {blackPill_.x - 4 * u, blackPill_.y + 5 * u, blackPill_.w + 8 * u, 3 * u}, true);
    text_.draw(pixelSmall_, "MENU", kPrint, {menuPill_.x - 2 * u, menuPill_.y + 5 * u, menuPill_.w + 4 * u, 3 * u}, true);
  }

  // The program picture, 16:9, as the engine draws an output: into a texture of
  // its own, because the engine fills its whole target (bars included).
  // The program takes the shape and size of where it is going, as the desk's
  // output raster does: a connected screen's own size (to 4K), else the
  // phone's own landscape pixels while it is full screen, else 1920x1080.
  // A 16:9 clip then fits a 19.5:9 phone as cue geometry, and Deckboy's
  // generators fill the screen edge to edge.
  void chooseProgramRaster() {
    int w = 1920, h = 1080;
    float hz = 60.0f;
#if defined(SDL_PLATFORM_ANDROID)
    {
      OutputScreen& out = outputScreen();
      std::lock_guard<std::mutex> hold(out.lock);
      if (out.window && out.width > 0 && out.height > 0) {
        const float k = std::min(1.0f, std::min(3840.0f / static_cast<float>(out.width), 2160.0f / static_cast<float>(out.height)));
        w = static_cast<int>(static_cast<float>(out.width) * k) & ~1;
        h = static_cast<int>(static_cast<float>(out.height) * k) & ~1;
        if (out.refreshHz > 1.0f) hz = out.refreshHz;
      } else if (phoneFull_) {
        w = static_cast<int>(std::max(pxW_, pxH_)) & ~1;
        h = static_cast<int>(std::min(pxW_, pxH_)) & ~1;
      }
    }
#endif
    if (w == programW_ && h == programH_) return;
    programW_ = w;
    programH_ = h;
    programHz_ = hz;
    if (programTex_) {
      SDL_DestroyTexture(programTex_);
      programTex_ = nullptr;
    }
  }

  // Where a picture of the program's shape sits inside a box: fitted, centred.
  SDL_FRect fitProgram(const SDL_FRect& box) const {
    const float aspect = static_cast<float>(programW_) / static_cast<float>(std::max(1, programH_));
    float w = box.w, h = w / aspect;
    if (h > box.h) { h = box.h; w = h * aspect; }
    return {box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w, h};
  }

  void drawProgramTexture() {
    chooseProgramRaster();
    if (!programTex_) {
      programTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, programW_, programH_);
      if (programTex_) SDL_SetTextureScaleMode(programTex_, SDL_SCALEMODE_LINEAR);
    }
    if (!programTex_) return;
    SDL_SetRenderTarget(renderer_, programTex_);
    SDL_SetRenderViewport(renderer_, nullptr);
    SDL_SetRenderClipRect(renderer_, nullptr);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    if (active_ >= 0 && !black_) engine_->render(SDL_Rect {0, 0, programW_, programH_});
    SDL_SetRenderTarget(renderer_, nullptr);
    // Whatever the engine left set must not reach the phone's own drawing.
    SDL_SetRenderViewport(renderer_, nullptr);
    SDL_SetRenderClipRect(renderer_, nullptr);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
  }

  // -- The show output --
  //
  // The program picture alone, full screen, at up to 1920x1080 (the screen's
  // compositor scales a 4K screen; a GPU path straight to the surface can
  // come later). Drawn into a texture, read back and posted to the surface.
  void drawOutputScreen() {
#if defined(SDL_PLATFORM_ANDROID)
    OutputScreen& out = outputScreen();
    int ow = 0, oh = 0, gen = 0;
    {
      std::lock_guard<std::mutex> hold(out.lock);
      if (!out.window) {
        outputLabel_.clear();
        return;
      }
      ow = out.width;
      oh = out.height;
      gen = out.generation;
      if (gen != outputGeneration_) {
        char label[96];
        std::snprintf(label, sizeof label, "OUT %dx%d %.4g", ow, oh, static_cast<double>(out.refreshHz));
        outputLabel_ = label;
      }
    }
    // Buffer size: the screen's shape, no larger than 1080p.
    const float scale = std::min(1.0f, std::min(1920.0f / static_cast<float>(ow), 1080.0f / static_cast<float>(oh)));
    const int bw = std::max(16, static_cast<int>(static_cast<float>(ow) * scale)) & ~1;
    const int bh = std::max(16, static_cast<int>(static_cast<float>(oh) * scale)) & ~1;
    if (!outputTex_ || outputTexW_ != bw || outputTexH_ != bh) {
      if (outputTex_) SDL_DestroyTexture(outputTex_);
      outputTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, bw, bh);
      outputTexW_ = bw;
      outputTexH_ = bh;
    }
    if (!outputTex_) return;
    SDL_SetRenderTarget(renderer_, outputTex_);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    if (testPattern_) {
      // Bars across the whole screen, the screen's own size written on them:
      // the operator sees the edges, the shape and what the screen reports.
      static const Ink kBars[7] = {{192, 192, 192}, {192, 192, 0}, {0, 192, 192}, {0, 192, 0}, {192, 0, 192}, {192, 0, 0}, {0, 0, 192}};
      const float barW = static_cast<float>(bw) / 7.0f;
      for (int i = 0; i < 7; ++i) fillRect(renderer_, kBars[i], {static_cast<float>(i) * barW, 0, barW + 1, static_cast<float>(bh) * 0.7f});
      fillRect(renderer_, kBlack, {0, static_cast<float>(bh) * 0.7f, static_cast<float>(bw), static_cast<float>(bh) * 0.3f});
      SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
      const SDL_FRect edge {1, 1, static_cast<float>(bw) - 2, static_cast<float>(bh) - 2};
      SDL_RenderRect(renderer_, &edge);
    } else if (programTex_) {
      // The program has the screen's own shape, so this fills it.
      const SDL_FRect dst = fitProgram({0, 0, static_cast<float>(bw), static_cast<float>(bh)});
      SDL_RenderTexture(renderer_, programTex_, nullptr, &dst);
    }
    if (testPattern_ || SDL_GetTicks() < identifyUntil_) {
      char id[160];
      std::snprintf(id, sizeof id, "DECKBOY POCKET  OUTPUT 1  %dx%d", ow, oh);
      if (!identifyFont_ || identifyFontH_ != bh) {
        if (identifyFont_) TTF_CloseFont(identifyFont_);
        const char* root = SDL_getenv("DECKBOY_ROOT");
        identifyFont_ = TTF_OpenFont((std::string(root ? root : ".") + "/data/PressStart2P.ttf").c_str(), static_cast<float>(bh) / 24.0f);
        identifyFontH_ = bh;
      }
      if (identifyFont_) {
        SDL_Surface* t = TTF_RenderText_Blended(identifyFont_, id, 0, SDL_Color {255, 255, 255, 255});
        if (t) {
          SDL_Texture* tt = SDL_CreateTextureFromSurface(renderer_, t);
          const SDL_FRect box {(static_cast<float>(bw) - static_cast<float>(t->w)) / 2 - 20, static_cast<float>(bh) * 0.82f - 16,
                               static_cast<float>(t->w) + 40, static_cast<float>(t->h) + 32};
          fillRect(renderer_, kBlack, box);
          const SDL_FRect dst {box.x + 20, box.y + 16, static_cast<float>(t->w), static_cast<float>(t->h)};
          if (tt) SDL_RenderTexture(renderer_, tt, nullptr, &dst);
          if (tt) SDL_DestroyTexture(tt);
          SDL_DestroySurface(t);
        }
      }
    }
    SDL_Surface* shot = SDL_RenderReadPixels(renderer_, nullptr);
    SDL_SetRenderTarget(renderer_, nullptr);
    if (!shot) return;
    SDL_Surface* rgba = shot->format == SDL_PIXELFORMAT_RGBA32 ? shot : SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
    if (rgba) {
      std::lock_guard<std::mutex> hold(out.lock);
      if (out.window && out.generation == gen) {
        if (gen != outputGeneration_) {
          ANativeWindow_setBuffersGeometry(out.window, rgba->w, rgba->h, WINDOW_FORMAT_RGBA_8888);
          outputGeneration_ = gen;
        }
        ANativeWindow_Buffer buf;
        if (ANativeWindow_lock(out.window, &buf, nullptr) == 0) {
          const int rows = std::min(buf.height, rgba->h);
          const int bytes = std::min(buf.width, rgba->w) * 4;
          for (int y = 0; y < rows; ++y) {
            std::memcpy(static_cast<Uint8*>(buf.bits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(buf.stride) * 4,
                        static_cast<const Uint8*>(rgba->pixels) + static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->pitch),
                        static_cast<std::size_t>(bytes));
          }
          ANativeWindow_unlockAndPost(out.window);
        }
      }
      if (rgba != shot) SDL_DestroySurface(rgba);
    }
    SDL_DestroySurface(shot);
#endif
  }

  // -- The remote: the desk's protocol --
  //
  // One line in, one reply out: OK <VERB>, ERR <VERB>: <why>, or the STATUS
  // report, the same shapes the desk and Mini use, so a client written for
  // either drives Pocket as deck 1.

  struct RemoteClient { SocketHandle socket; std::string buffer; };

  void pollRemote() {
    if (listen_ == kInvalidSocket) return;
    fd_set readable;
    FD_ZERO(&readable);
    SocketHandle maxFd = listen_;
    watchFd(listen_, &readable, maxFd);
    for (RemoteClient& c : clients_) watchFd(c.socket, &readable, maxFd);
    timeval none {0, 0};
    if (::select(selectNfds(maxFd), &readable, nullptr, nullptr, &none) <= 0) return;
    if (readyFd(listen_, &readable)) {
      sockaddr_in from {};
      socklen_t len = sizeof(from);
      SocketHandle sock = static_cast<SocketHandle>(accept(listen_, reinterpret_cast<sockaddr*>(&from), &len));
      if (sock != kInvalidSocket && clients_.size() < 16) clients_.push_back({sock, {}});
      else if (sock != kInvalidSocket) closeSocket(sock);
    }
    for (std::size_t i = 0; i < clients_.size();) {
      RemoteClient& c = clients_[i];
      bool alive = true;
      if (readyFd(c.socket, &readable)) {
        char buf[2048];
        const int n = static_cast<int>(recv(c.socket, buf, sizeof(buf), 0));
        if (n <= 0) alive = false;
        else c.buffer.append(buf, static_cast<std::size_t>(n));
        std::size_t eol;
        while (alive && (eol = c.buffer.find('\n')) != std::string::npos) {
          std::string line = c.buffer.substr(0, eol);
          c.buffer.erase(0, eol + 1);
          if (!line.empty() && line.back() == '\r') line.pop_back();
          const std::string answer = remoteCommand(line);
          if (!answer.empty()) send(c.socket, answer.data(), static_cast<int>(answer.size()), kSocketSendFlags);
        }
        if (c.buffer.size() > 65536) alive = false;
      }
      if (alive) { ++i; continue; }
      closeSocket(c.socket);
      clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }

  std::string remoteCommand(const std::string& raw) {
    std::istringstream in(raw);
    std::string verb;
    in >> verb;
    for (char& ch : verb) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    std::string rest;
    std::getline(in, rest);
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
    auto ok = [&] { return "OK " + verb + "\n"; };
    auto err = [&](const std::string& why) { return "ERR " + verb + ": " + why + "\n"; };
    const int count = static_cast<int>(cues_.size());
    auto cueNumber = [&](int& out) {
      char* end = nullptr;
      const long n = std::strtol(rest.c_str(), &end, 10);
      if (end == rest.c_str() || n < 1 || n > count) return false;
      out = static_cast<int>(n) - 1;
      return true;
    };
    if (verb.empty()) return "";
    if (verb == "PING") return ok();
    if (verb == "HELP") {
      return "OK HELP\nPocket answers: PING STATUS GO BACK TAKE <n> SELECT <n> NEXT PREV PLAY PAUSE STOP\n"
             "SEEK <seconds|+s|-s> BLACK [ON|OFF] VOLUME <0-100> MUTE DECK 1 SKIP SKIPBACK\n"
             "SHUFFLE LOOP ENDAFTER [ON|OFF|TOGGLE] CROSSFADE <s> QUEUE <n>|CLEAR RENAME <n> <name>\n"
             "DIRECTION <n> FORWARD|REVERSE|BOUNCE SHELL <name>|FLAT SNAP <file.bmp>\n"
             "SYNC FOLLOW <desk> SYNC STOP SYNC STATUS\n";
    }
    if (verb == "STATUS" || verb == "STATE") return remoteStatus();
    if (verb == "DECK") return rest.empty() || rest == "1" ? ok() : err("Pocket has one deck");
    // Playing the list (Mini's verbs, so one Companion page drives both).
    auto onOff = [&](bool current, bool& out) {
      std::string a = rest;
      for (char& ch : a) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (a == "ON") out = true;
      else if (a == "OFF") out = false;
      else if (a.empty() || a == "TOGGLE") out = !current;
      else return false;
      return true;
    };
    if (verb == "VIEW") {   // VIEW HOME | MENU <ADD|OUT|LIST|SYNC|SKIN> | SHEET <cue>: what the screen shows
      std::istringstream args(rest);
      std::string what, arg;
      args >> what >> arg;
      for (char& ch : what) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      for (char& ch : arg) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (what == "HOME") { menuOpen_ = false; sheet_ = -1; phoneFull_ = false; return ok(); }
      if (what == "FULL") { menuOpen_ = false; sheet_ = -1; phoneFull_ = true; ghostUntil_ = SDL_GetTicks() + 3500; return ok(); }
      if (what == "MENU") {
        const char* tabs[] = {"SOURCES", "OUTPUT", "LIST", "SYNC", "PHONE", "SHOW"};
        menuTab_ = -1;   // MENU alone: the tiles
        for (int t = 0; t < 6; ++t) if (arg == tabs[t]) menuTab_ = t;
        if (arg == "ADD") menuTab_ = 0;
        if (arg == "OUT") menuTab_ = 1;
        if (arg == "SKIN") menuTab_ = 4;
        if (arg == "ABOUT") menuTab_ = 6;
        menuOpen_ = true;
        sheet_ = -1;
        phoneFull_ = false;
        return ok();
      }
      if (what == "SHEET") {
        const int n = std::atoi(arg.c_str());
        if (n < 1 || n > count) return err("no cue " + arg);
        openSheet(n - 1);
        phoneFull_ = false;
        std::string pg;
        args >> pg;
        for (char& ch : pg) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (pg == "PLACE") page_ = Page::Place;
        else if (pg == "LOOK") page_ = Page::Look;
        else if (pg == "SOUND") page_ = Page::Sound;
        else if (pg == "CALC" && !sliders_.empty()) openCalc(sliders_.front().spec);
        else if (pg == "LFO") { page_ = Page::Place; lfoSlot_ = kGeoLfoRotation; }
        return ok();
      }
      return err("VIEW HOME, VIEW MENU <tab> or VIEW SHEET <cue>");
    }
    if (verb == "SHOW") {   // SHOW SAVE [name] | SHOW OPEN <name or path> | SHOW NEW | SHOW PLAYLIST <n> | SHOW LIST
      std::istringstream a(rest);
      std::string sub;
      a >> sub;
      std::string arg;
      std::getline(a, arg);
      while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
      for (char& ch : sub) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (sub == "SAVE") { if (arg.empty()) { if (showFile_.empty()) return err("SHOW SAVE <name>: not saved yet"); saveShow(); } else saveShowAs(arg); return ok(); }
      if (sub == "OPEN") {
        if (arg.empty()) return err("SHOW OPEN <name>");
        std::string path = arg.find_first_of("/\\") != std::string::npos ? arg : joinPath(showsDir_, endsWithNoCase(arg, ".deckboy") ? arg : arg + ".deckboy");
        openShow(path);
        return ok();
      }
      if (sub == "NEW") { newShow(); return ok(); }
      if (sub == "PLAYLIST") {
        const int n = std::atoi(arg.c_str());
        if (n < 1 || n > static_cast<int>(show_.decks.size())) return err("SHOW PLAYLIST <1.." + std::to_string(show_.decks.size()) + ">");
        show_ = currentShow();
        takePlaylist(n - 1);
        return ok();
      }
      if (sub == "LIST") {
        std::string out = "OK SHOW";
        for (const std::string& f : savedShows()) out += " | " + f.substr(f.find_last_of("/\\") + 1);
        return out + "\n";
      }
      return err("SHOW SAVE|OPEN|NEW|PLAYLIST|LIST");
    }
    if (verb == "DELETE") {   // DELETE <cue>: as a swipe does (the cue on air is refused)
      const int n = std::atoi(rest.c_str());
      if (n < 1 || n > static_cast<int>(cues_.size())) return err("DELETE <cue number>");
      if (n - 1 == active_) return err("that cue is on air");
      deleteCueAt(n - 1);
      return ok();
    }
    if (verb == "UNDO") { if (!undoCue_) return err("nothing to undo"); undoDelete(); return ok(); }
    if (verb == "ADD") {   // ADD <generator type> (smpte-bars, swirl, lumia...) or ADD video-synth
      if (rest.empty()) return err("ADD <generator> or ADD video-synth");
      if (rest == "video-synth" || rest == "VIDEO-SYNTH") addVideoSynth();
      else addGenerator(rest.c_str());
      return ok();
    }
    if (verb == "SHUFFLE") { bool v; if (!onOff(shuffle_, v)) return err("ON, OFF or TOGGLE"); setShuffle(v); return ok(); }
    if (verb == "LOOP") { bool v; if (!onOff(loopList_, v)) return err("ON, OFF or TOGGLE"); loopList_ = v; return ok(); }
    if (verb == "ENDAFTER") { bool v; if (!onOff(endAfter_, v)) return err("ON, OFF or TOGGLE"); endAfter_ = v; return ok(); }
    if (verb == "MUTE") { bool v; if (!onOff(muted_, v)) return err("ON, OFF or TOGGLE"); setMuted(v); return ok(); }
    if (verb == "TRANSITION") {   // TRANSITION <cue|LIST> <style> [seconds]
      std::istringstream args(rest);
      std::string which, token;
      double seconds = -1;
      args >> which >> token >> seconds;
      bool known = false;
      const TransitionStyle style = deckboy::core::utils::parseTransitionStyleToken(token, &known);
      if (!known) return err("a style: cut crossfade dipblack dipwhite pushleft... wipeleft... iris");
      std::string w = which;
      for (char& ch : w) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (w == "LIST") {
        listStyle_ = drawableStyle(style);
        if (seconds >= 0) glideSeconds_ = std::min(seconds, 10.0);
        return ok();
      }
      const int n = std::atoi(which.c_str());
      if (n < 1 || n > count) return err("no cue " + which);
      Cue& c = cues_[static_cast<std::size_t>(n - 1)];
      c.cueTransitionStyle = deckboy::core::utils::transitionStyleToken(style);
      if (seconds >= 0) c.cueTransitionSeconds = std::min(seconds, 10.0);
      return ok();
    }
    if (verb == "CROSSFADE" || verb == "GLIDE") {
      char* end = nullptr;
      const double v = std::strtod(rest.c_str(), &end);
      if (end == rest.c_str() || v < 0 || v > 10) return err("seconds, 0-10 (0 is a cut)");
      glideSeconds_ = v;
      return ok();
    }
    if (verb == "QUEUE") {
      std::string a = rest;
      for (char& ch : a) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (a == "CLEAR") { queueIds_.clear(); return ok(); }
      if (a.empty()) {
        std::string list;
        for (const std::string& id : queueIds_) {
          const int q = indexOfCue(id);
          if (q >= 0) list += (list.empty() ? "" : " ") + std::to_string(q + 1);
        }
        return "OK QUEUE: " + (list.empty() ? std::string("empty") : list) + "\n";
      }
      int n = 0;
      if (!cueNumber(n)) return err("no cue " + rest);
      toggleQueued(n);
      return ok();
    }
    if (verb == "SKIP") { if (followLocked()) return err("following a desk"); go(); return ok(); }
    if (verb == "SKIPBACK") { if (followLocked()) return err("following a desk"); back(); return ok(); }
    if (verb == "RENAME") {
      std::istringstream args(rest);
      int n = 0;
      args >> n;
      std::string name;
      std::getline(args, name);
      while (!name.empty() && name.front() == ' ') name.erase(name.begin());
      if (n < 1 || n > count || name.empty()) return err("RENAME <cue> <name>");
      cues_[static_cast<std::size_t>(n - 1)].name = name;
      cueEdited(n - 1, false);
      return ok();
    }
    if (verb == "LFO") {   // LFO <cue> X|Y|SCALE|ROTATION ON|OFF [sine|tri|saw|ramp|square|steps|sound] [rate Hz] [depth %]
      std::istringstream args(rest);
      int n = 0;
      std::string what, onoff, shape;
      double rate = -1, depth = -1;
      args >> n >> what >> onoff >> shape >> rate >> depth;
      for (char& ch : what) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      for (char& ch : onoff) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      for (char& ch : shape) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      if (n < 1 || n > count) return err("LFO <cue> X|Y|SCALE|ROTATION ON|OFF [shape] [rate] [depth]");
      const int slot = what == "X" ? kGeoLfoOffsetX : what == "Y" ? kGeoLfoOffsetY : what == "SCALE" ? kGeoLfoScaleX
                     : what == "ROTATION" ? kGeoLfoRotation : -1;
      if (slot < 0) return err("X, Y, SCALE or ROTATION");
      using deckboy::effects::LfoShape;
      auto& l = cues_[static_cast<std::size_t>(n - 1)].geometryLfo[static_cast<std::size_t>(slot)];
      l.on = onoff != "OFF";
      if (!shape.empty()) {
        l.shape = shape == "tri" ? LfoShape::Triangle : shape == "saw" ? LfoShape::Saw : shape == "ramp" ? LfoShape::Ramp
                : shape == "square" ? LfoShape::Square : shape == "steps" ? LfoShape::Sample : shape == "sound" ? LfoShape::Audio
                : LfoShape::Sine;
      }
      if (rate > 0) l.rateHz = static_cast<float>(std::min(rate, 8.0));
      if (depth >= 0) l.depth = static_cast<float>(std::min(depth, 100.0) / 100.0);
      cueEdited(n - 1, false);   // the engine plays its own copy of the live cue
      return ok();
    }
    if (verb == "DIRECTION") {   // DIRECTION <cue> FORWARD|REVERSE|BOUNCE (the desk's verb)
      std::istringstream args(rest);
      int n = 0;
      std::string d;
      args >> n >> d;
      for (char& ch : d) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (n < 1 || n > count) return err("DIRECTION <cue> FORWARD|REVERSE|BOUNCE");
      const int k = d == "FORWARD" ? 0 : d == "REVERSE" ? 1 : d == "BOUNCE" ? 2 : -1;
      if (k < 0) return err("FORWARD, REVERSE or BOUNCE");
      Cue& c = cues_[static_cast<std::size_t>(n - 1)];
      if (k != 0 && !cueDirectionApplies(c)) return err("only a video cue plays backwards");
      c.playDirection = k;
      cueEdited(n - 1, false);
      return ok();
    }
    if (verb == "SHELL") {   // a rendered shell by its slug, or FLAT
      std::string want = rest;
      for (char& ch : want) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      if (want == "flat" || want.empty()) { setShell(""); return ok(); }
      setShell(want);
      return shell_ == want ? ok() : err("no shell " + rest);
    }
    if (verb == "SNAP") {    // the next frame as a BMP file, for checking the screen without touching it
      if (rest.empty()) return err("SNAP <file.bmp>");
      snapPath_ = rest;
      return ok();
    }
    if (verb == "SYNC") {
      std::istringstream args(rest);
      std::string sub, host;
      args >> sub >> host;
      for (char& ch : sub) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (sub == "FOLLOW") {
        if (host.empty()) return err("SYNC FOLLOW <desk address>");
        followHost_ = host;
        startFollowing();
        if (!followError_.empty() && follow_ == Follow::Off) return err(followError_);
        return ok();
      }
      if (sub == "STOP") { stopFollowing(); return ok(); }
      if (sub.empty() || sub == "STATUS") return "OK SYNC: " + syncStatusFields() + "\n";
      return err("SYNC FOLLOW <desk address>, SYNC STOP or SYNC STATUS");
    }
    if ((verb == "GO" || verb == "BACK" || verb == "TAKE") && follow_ != Follow::Off) {
      return err("following the desk at " + followHost_ + " (SYNC STOP first)");
    }
    if (verb == "GO") { go(); return ok(); }
    if (verb == "BACK") { back(); return ok(); }
    if (verb == "TAKE") {
      int n = selected_;
      if (!rest.empty() && !cueNumber(n)) return err("no cue " + rest);
      take(n);
      select(n + 1);
      return ok();
    }
    if (verb == "SELECT") {
      int n = 0;
      if (!cueNumber(n)) return err("no cue " + rest);
      select(n);
      return ok();
    }
    if (verb == "NEXT") { select(selected_ + 1); return ok(); }
    if (verb == "PREV" || verb == "PREVIOUS") { select(selected_ - 1); return ok(); }
    if (verb == "PLAY") { if (active_ < 0) go(); else engine_->play(); return ok(); }
    if (verb == "PAUSE") { if (active_ >= 0) engine_->pause(); return ok(); }
    if (verb == "STOP" || verb == "CLEAR" || verb == "PANIC") {
      engine_->clear();
      active_ = -1;
      return ok();
    }
    if (verb == "SEEK") {
      if (active_ < 0) return err("nothing is live");
      char* end = nullptr;
      const double t = std::strtod(rest.c_str(), &end);
      if (end == rest.c_str()) return err("seconds, please");
      if (!rest.empty() && (rest[0] == '+' || rest[0] == '-')) { skipBy(t); return ok(); }   // SEEK +10 / SEEK -10
      if (t < 0) return err("seconds, please");
      const Cue& c = cues_[static_cast<std::size_t>(active_)];
      if (cueHasTimeline(c) && t > c.duration) return err("past the end");
      engine_->seek(t);
      return ok();
    }
    if (verb == "BLACK" || verb == "BLACKOUT") {
      std::string a = rest;
      for (char& ch : a) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (a.empty() || a == "TOGGLE") black_ = !black_;
      else if (a == "ON") black_ = true;
      else if (a == "OFF") black_ = false;
      else return err("ON, OFF or nothing");
      return ok();
    }
    if (verb == "VOLUME" || verb == "MASTERVOL") {
      char* end = nullptr;
      const double v = std::strtod(rest.c_str(), &end);
      if (end == rest.c_str() || v < 0 || v > 100) return err("a percent, 0-100");   // never clamped silently
      volume_ = static_cast<int>(std::lround(v));
      setMuted(muted_);   // the new level, still silent while muted
      return ok();
    }
    return "ERR unknown command: " + verb + "\n";
  }

  std::string remoteStatus() const {
    const Cue* live = active_ >= 0 ? &cues_[static_cast<std::size_t>(active_)] : nullptr;
    const char* state = !live ? "Stopped" : engine_->state() == TransportState::Playing ? "Playing" : "Paused";
    std::ostringstream o;
    o << "DECKBOY_0.01 app=pocket focus=1 decks=1 outputs=1 master_vol=" << volume_
      << " blackout=" << (black_ ? "on" : "off") << "\n";
    o << "DECK 1 name=\"Pocket\" status=" << state << " selected=" << (cues_.empty() ? 0 : selected_ + 1)
      << " active=" << active_ + 1 << " cues=" << cues_.size() << " cue=\"" << (live ? live->name : std::string()) << "\""
      << " pos=" << (live ? clock(engine_->position()) : std::string("--:--"))
      << " dur=" << (live && live->duration > 0 ? clock(live->duration) : std::string("--:--"))
      << " shuffle=" << (shuffle_ ? "on" : "off") << " loop=" << (loopList_ ? "on" : "off")
      << " endafter=" << (endAfter_ ? "on" : "off") << " crossfade=" << listGlide() << " queued=" << queueIds_.size()
      << " muted=" << (muted_ ? "on" : "off") << "\n";
    o << "OUTPUT 1 name=\""<< (outputLabel_.empty() ? "phone" : outputLabel_) << "\" enabled=" << (black_ ? "off" : "on")
      << " raster=" << programW_ << "x" << programH_ << "\n";
    o << "SYNC " << syncStatusFields() << "\n";
    o << "SHOW name=\"" << showName() << "\" file=\"" << showFile_ << "\" playlist=" << showDeck_ + 1 << "/" << std::max<std::size_t>(1, show_.decks.size())
      << "\n";
    for (std::size_t i = 0; i < cues_.size(); ++i) {   // one line per cue: what the list shows
      const EndMode m = endModeOf(cues_[i]);
      o << "CUE " << i + 1 << " kind=" << kindTag(cues_[i])
        << " end=" << (m == EndMode::Loop ? "loop" : m == EndMode::Next ? "next" : "hold") << " name=\"" << cues_[i].name << "\"\n";
    }
    return o.str();
  }

  // -- SYNC: following a desk (private-notes/SYNC_DESIGN.md) --
  //
  // Pocket connects OUT to the desk's remote port (a desk cannot reliably
  // reach a phone behind a hotspot) and sends FOLLOW. The desk then streams
  // its focused deck four times a second:
  //   SYNC deck= cue=<id> num= state= pos= at=<desk ns> speed= trans=<style>/<s> name="<cue name>"
  // TIME probes give the offset between the desk's clock and this phone's.
  // Pocket takes the same cue (by id, then name, then the media's file name),
  // and seeks when it is more than two frames from where the desk is.

  enum class Follow { Off, Connecting, Joining, Following, Lost };

  struct SyncLine {
    std::string cue = "-", file, name, state = "stopped", transStyle = "crossfade";
    int num = 0;
    double pos = 0.0, speed = 1.0, transSeconds = 0.5;
    Uint64 at = 0;
  };

  struct ClockSample { Sint64 offset; Uint64 rtt; };

  static bool parseSyncLine(const std::string& line, SyncLine& out) {
    if (line.rfind("SYNC ", 0) != 0) return false;
    // key=value pairs; a value in quotes may hold spaces (file=, name=).
    const std::string body = line.substr(5);
    std::size_t at = 0;
    while (at < body.size()) {
      while (at < body.size() && body[at] == ' ') ++at;
      const std::size_t eq = body.find('=', at);
      if (eq == std::string::npos) break;
      const std::string k = body.substr(at, eq - at);
      std::string v;
      if (eq + 1 < body.size() && body[eq + 1] == '"') {
        const std::size_t close = body.find('"', eq + 2);
        v = body.substr(eq + 2, (close == std::string::npos ? body.size() : close) - eq - 2);
        at = close == std::string::npos ? body.size() : close + 1;
      } else {
        const std::size_t sp = body.find(' ', eq + 1);
        v = body.substr(eq + 1, (sp == std::string::npos ? body.size() : sp) - eq - 1);
        at = sp == std::string::npos ? body.size() : sp + 1;
      }
      if (k == "cue") out.cue = v;
      else if (k == "file") out.file = v;
      else if (k == "name") out.name = v;
      else if (k == "num") out.num = std::atoi(v.c_str());
      else if (k == "state") out.state = v;
      else if (k == "pos") out.pos = std::atof(v.c_str());
      else if (k == "at") out.at = std::strtoull(v.c_str(), nullptr, 10);
      else if (k == "speed") out.speed = std::atof(v.c_str());
      else if (k == "trans") {
        const std::size_t slash = v.find('/');
        out.transStyle = v.substr(0, slash);
        out.transSeconds = slash == std::string::npos ? 0.0 : std::atof(v.c_str() + slash + 1);
      }
    }
    return out.at != 0;
  }

  static void setNonBlocking(SocketHandle sock) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(static_cast<SOCKET>(sock), FIONBIO, &on);
#else
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
  }

  static bool wouldBlock() {
#ifdef _WIN32
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
  }

  std::string syncFile() const {
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    return std::string(state ? state : ".") + "/pocket_sync.txt";
  }

  void saveSyncSettings() {
    const std::string text = "host\t" + followHost_ + "\nfollow\t" + (followWanted_ ? "1" : "0") + "\n";
    SDL_SaveFile(syncFile().c_str(), text.data(), text.size());
  }

  // The desk typed last time, and whether Pocket was following it: a phone
  // that restarts mid-show rejoins by itself.
  void loadSyncSettings() {
    size_t n = 0;
    char* saved = static_cast<char*>(SDL_LoadFile(syncFile().c_str(), &n));
    if (!saved) return;
    std::istringstream in(std::string(saved, n));
    SDL_free(saved);
    std::string line;
    bool resume = false;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.rfind("host\t", 0) == 0) followHost_ = line.substr(5);
      if (line == "follow\t1") resume = true;
    }
    if (resume && !followHost_.empty()) startFollowing();
  }

  void beginHostEdit() {
    hostEditing_ = true;
    SDL_StartTextInput(window_);
  }

  void endHostEdit() {
    if (!hostEditing_) return;
    hostEditing_ = false;
    SDL_StopTextInput(window_);
  }

  void startFollowing() {
    dropFollowSocket();
    followError_.clear();
    std::string host = followHost_;
    while (!host.empty() && host.back() == ' ') host.pop_back();
    int port = kRemotePort;
    const std::size_t colon = host.find(':');
    if (colon != std::string::npos) {
      port = std::atoi(host.c_str() + colon + 1);
      host.erase(colon);
    }
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (host.empty() || port <= 0 || port > 65535 || inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
      followError_ = "Type the desk's IP address, like 192.168.1.20";
      follow_ = Follow::Off;
      followWanted_ = false;
      return;
    }
    followWanted_ = true;
    saveSyncSettings();
    followAddr_ = addr;
    connectFollow();
  }

  void connectFollow() {
    dropFollowSocket();
    const SocketHandle sock = static_cast<SocketHandle>(socket(AF_INET, SOCK_STREAM, 0));
    if (sock == kInvalidSocket) {
      followError_ = "No network socket";
      follow_ = Follow::Lost;
      followRetryAt_ = SDL_GetTicks() + 3000;
      return;
    }
    setNonBlocking(sock);
    const int rc = connect(sock, reinterpret_cast<const sockaddr*>(&followAddr_), sizeof(followAddr_));
    if (rc != 0 && !wouldBlock()) {
      closeSocket(sock);
      followError_ = "Cannot reach the desk";
      follow_ = Follow::Lost;
      followRetryAt_ = SDL_GetTicks() + 3000;
      return;
    }
    followSock_ = sock;
    follow_ = Follow::Connecting;
    followSince_ = SDL_GetTicks();
  }

  void dropFollowSocket() {
    if (followSock_ != kInvalidSocket) {
      const char bye[] = "UNFOLLOW\n";
      send(followSock_, bye, static_cast<int>(sizeof(bye) - 1), kSocketSendFlags);
      closeSocket(followSock_);
    }
    followSock_ = kInvalidSocket;
    followBuffer_.clear();
    clockSamples_.clear();
    clockReady_ = false;
    nextTimeProbe_ = 0;
  }

  void stopFollowing() {
    dropFollowSocket();
    followWanted_ = false;
    follow_ = Follow::Off;
    followError_.clear();
    syncCue_.clear();
    missingCue_.clear();
    saveSyncSettings();
  }

  // The desk went quiet or the connection closed: say so, keep playing what
  // is on air (a fleet screen does not go dark because Wi-Fi blinked), and
  // try again.
  void loseDesk(const std::string& why) {
    SDL_Log("pocket: sync lost (%s); last line %llu ms ago, state %s", why.c_str(),
            static_cast<unsigned long long>(SDL_GetTicks() - lastHeard_), followHeadline().c_str());
    if (followSock_ != kInvalidSocket) closeSocket(followSock_);
    followSock_ = kInvalidSocket;
    followBuffer_.clear();
    clockSamples_.clear();
    clockReady_ = false;
    if (!why.empty()) followError_ = why;
    follow_ = followWanted_ ? Follow::Lost : Follow::Off;
    followRetryAt_ = SDL_GetTicks() + 2000;
  }

  bool followSend(const std::string& line) {
    if (followSock_ == kInvalidSocket) return false;
    return send(followSock_, line.data(), static_cast<int>(line.size()), kSocketSendFlags) == static_cast<int>(line.size());
  }

  std::string phoneName() {
    std::string name;
#if defined(SDL_PLATFORM_ANDROID)
    name = bridgeString("deviceName", "");
#endif
    if (name.empty()) name = "Pocket";
    for (char& c : name) if (c == ' ' || c == '\t') c = '-';
    return name;
  }

  void pollFollow() {
    const Uint64 now = SDL_GetTicks();
    if (follow_ == Follow::Off) return;
    if (followSock_ == kInvalidSocket) {
      if (followWanted_ && now >= followRetryAt_) connectFollow();
      return;
    }
    if (follow_ == Follow::Connecting) {
      fd_set writable;
      FD_ZERO(&writable);
      SocketHandle maxFd = followSock_;
      watchFd(followSock_, &writable, maxFd);
      timeval none {0, 0};
      if (::select(selectNfds(maxFd), nullptr, &writable, nullptr, &none) > 0 && readyFd(followSock_, &writable)) {
        int soError = 0;
        socklen_t len = sizeof(soError);
        getsockopt(followSock_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
        if (soError != 0) { loseDesk("The desk did not answer on port " + std::to_string(ntohs(followAddr_.sin_port))); return; }
        follow_ = Follow::Joining;
        followSend("FOLLOW " + phoneName() + " pocket fleet\n");
        nextTimeProbe_ = 0;
      } else if (now - followSince_ > 4000) {
        loseDesk("No desk at that address (is network control on?)");
        return;
      }
      return;
    }
    // Clock probes: quick at first, then every two seconds.
    if (now >= nextTimeProbe_) {
      followSend("TIME " + std::to_string(SDL_GetTicksNS()) + "\n");
      nextTimeProbe_ = now + (clockSamples_.size() < 8 ? 150 : 2000);
    }
    char buf[4096];
    for (;;) {
      const int n = static_cast<int>(recv(followSock_, buf, sizeof(buf), 0));
      if (n > 0) { followBuffer_.append(buf, static_cast<std::size_t>(n)); continue; }
      if (n == 0) { loseDesk("The desk closed the connection"); return; }
      if (!wouldBlock()) { loseDesk("Lost the connection to the desk"); return; }
      break;
    }
    std::size_t eol;
    while ((eol = followBuffer_.find('\n')) != std::string::npos) {
      std::string line = followBuffer_.substr(0, eol);
      followBuffer_.erase(0, eol + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      takeFollowLine(line);
      if (followSock_ == kInvalidSocket) return;
    }
    if (followBuffer_.size() > 65536) { loseDesk("The desk sent too much"); return; }
    // Read the clock again: a line just taken set lastHeard_ after `now`
    // was read, and `now - lastHeard_` would wrap to a huge number -- which
    // dropped a working connection as "the desk went quiet".
    const Uint64 after = SDL_GetTicks();
    const Uint64 heard = std::max(lastHeard_, followSince_);
    const Uint64 quiet = after > heard ? after - heard : 0;
    if (follow_ == Follow::Following && quiet > 1000) {
      follow_ = Follow::Lost;   // the 4 Hz heartbeat stopped
    }
    if ((follow_ == Follow::Lost || follow_ == Follow::Joining) && quiet > 6000) {
      loseDesk(follow_ == Follow::Joining ? "The desk did not take FOLLOW" : "The desk went quiet");
    }
  }

  void takeFollowLine(const std::string& line) {
    const Uint64 now = SDL_GetTicks();
    if (line.rfind("OK FOLLOW", 0) == 0) {
      if (follow_ != Follow::Following) buzz("lock");
      follow_ = Follow::Following;
      followError_.clear();
      lastHeard_ = now;
      return;
    }
    if (line.rfind("ERR FOLLOW", 0) == 0) {
      // Usually "this desk is not leading": keep asking, the operator may turn it on.
      const std::size_t colon = line.find(": ");
      loseDesk(colon == std::string::npos ? line : "Desk: " + line.substr(colon + 2));
      followRetryAt_ = now + 3000;
      return;
    }
    if (line.rfind("OK TIME: ", 0) == 0) {
      std::istringstream in(line.substr(9));
      Uint64 sent = 0, desk = 0;
      if (in >> sent >> desk && sent) {
        const Uint64 back = SDL_GetTicksNS();
        if (back >= sent) {
          ClockSample c;
          c.rtt = back - sent;
          c.offset = static_cast<Sint64>(desk) - static_cast<Sint64>(sent + c.rtt / 2);
          clockSamples_.push_back(c);
          if (clockSamples_.size() > 8) clockSamples_.erase(clockSamples_.begin());
          // The sample with the shortest round trip has the least room for error.
          const ClockSample* best = &clockSamples_.front();
          for (const ClockSample& k : clockSamples_) if (k.rtt < best->rtt) best = &k;
          clockOffset_ = best->offset;
          clockRtt_ = best->rtt;
          clockReady_ = clockSamples_.size() >= 3;
        }
      }
      return;
    }
    SyncLine sync;
    if (parseSyncLine(line, sync)) {
      lastHeard_ = now;
      if (follow_ != Follow::Following && follow_ != Follow::Off) follow_ = Follow::Following;
      lastSync_ = sync;
      if (clockReady_) applySync(sync);
    }
  }

  Uint64 deskNowNs() const {
    return static_cast<Uint64>(static_cast<Sint64>(SDL_GetTicksNS()) + clockOffset_);
  }

  static std::string lowerStem(const std::string& path) {
    std::string stem = dbfs::path(path).stem().string();
    for (char& c : stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return stem;
  }

  static std::string lower(std::string t) {
    for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t;
  }

  // This phone's copy of the desk's cue: the same id (the same show file on
  // both), else the same media (a file of the same name, the same generator),
  // else the same cue name.
  int localCueFor(const SyncLine& sync) const {
    for (std::size_t i = 0; i < cues_.size(); ++i) if (cues_[i].id == sync.cue) return static_cast<int>(i);
    if (!sync.file.empty()) {
      const bool pattern = sync.file.rfind("pattern://", 0) == 0;
      const std::string want = lower(pattern ? sync.file : dbfs::path(sync.file).filename().string());
      for (std::size_t i = 0; i < cues_.size(); ++i) {
        const std::string& path = cues_[i].path;
        if (path.empty()) continue;
        const std::string have = lower(pattern ? path : dbfs::path(path).filename().string());
        if (have == want) return static_cast<int>(i);
      }
      // A file the picker added keeps its content:// address; its shown name is the file's.
      if (!pattern) {
        const std::string stem = lowerStem(sync.file);
        for (std::size_t i = 0; i < cues_.size(); ++i) if (lower(cues_[i].name) == stem) return static_cast<int>(i);
      }
    }
    if (!sync.name.empty()) {
      const std::string want = lower(sync.name);
      for (std::size_t i = 0; i < cues_.size(); ++i) if (lower(cues_[i].name) == want) return static_cast<int>(i);
      for (std::size_t i = 0; i < cues_.size(); ++i) {
        if (!cues_[i].path.empty() && lowerStem(cues_[i].path) == lowerStem(sync.name)) return static_cast<int>(i);
      }
    }
    return -1;
  }

  void applySync(const SyncLine& sync) {
    const Uint64 now = SDL_GetTicks();
    if (sync.cue == "-") {
      if (active_ >= 0) { engine_->clear(); active_ = -1; }
      syncCue_ = "-";
      missingCue_.clear();
      black_ = sync.state == "black";
      return;
    }
    if (sync.cue != syncCue_) {
      syncCue_ = sync.cue;
      const int local = localCueFor(sync);
      if (local < 0) {
        // Never show the wrong picture: go dark and say which cue is missing.
        missingCue_ = !sync.name.empty() ? sync.name : !sync.file.empty() ? sync.file : "cue " + std::to_string(sync.num);
        if (active_ >= 0) { engine_->clear(); active_ = -1; }
        return;
      }
      missingCue_.clear();
      active_ = local;
      const Cue& c = cues_[static_cast<std::size_t>(local)];
      const TransitionStyle style = deckboy::core::utils::parseTransitionStyleToken(sync.transStyle);
      const double fade = style == TransitionStyle::Cut ? 0.0 : std::max(0.0, sync.transSeconds);
      engine_->loadCue(&c, sync.state == "playing" || sync.state == "black", fade,
                       fade > 0 ? drawableStyle(style) : TransitionStyle::Cut);
      selected_ = std::min(local + 1, static_cast<int>(cues_.size()) - 1);
      seekHoldUntil_ = 0;
    }
    black_ = sync.state == "black";
    if (active_ < 0 || !missingCue_.empty()) return;
    const Cue& c = cues_[static_cast<std::size_t>(active_)];
    const bool playing = sync.state == "playing" || sync.state == "black";
    if (!playing && engine_->state() == TransportState::Playing) engine_->pause();
    if (playing && engine_->state() != TransportState::Playing) engine_->play();
    if (!cueHasTimeline(c)) return;
    // Where the desk is now: its position when it wrote the line, plus the
    // time since in the desk's own clock.
    double expected = sync.pos;
    if (playing) {
      const double since = static_cast<double>(static_cast<Sint64>(deskNowNs() - sync.at)) / 1e9;
      expected += std::max(0.0, since) * (sync.speed > 0 ? sync.speed : 1.0);
    }
    const double in = cueIn(c), out = cueOut(c);
    if (expected > out && endModeOf(c) == EndMode::Loop && out > in) expected = in + std::fmod(expected - in, out - in);
    expected = std::clamp(expected, in, out);
    const double frame = cueFrame(c);
    const double drift = engine_->position() - expected;
    driftFrames_ = drift / frame;
    if (now < seekHoldUntil_) return;   // a seek is still landing
    // The first look after a playing seek says how late it landed: learn
    // that, so the next seek aims ahead by as much (a phone's decoder takes
    // longer to land than a desk's, and each phone differs).
    if (seekLanding_ && playing && std::fabs(drift) < 0.5) {
      seekLead_ = std::clamp(seekLead_ - 0.7 * drift, 0.0, 0.5);
    }
    seekLanding_ = false;
    if (std::fabs(drift) > 2.0 * frame) {
      engine_->seek(expected + (playing ? seekLead_ : 0.0));
      seekHoldUntil_ = now + 600;   // let the decoder land before judging again
      seekLanding_ = playing;
      ++syncSeeks_;
    }
  }

  bool followLocked() {
    if (follow_ == Follow::Off) return false;
    notice_ = "The desk has GO while following. MENU > SYNC to stop.";
    noticeUntil_ = SDL_GetTicks() + 2500;
    return true;
  }

  std::string followHeadline() const { return followHeadline(follow_); }
  std::string followHeadline(Follow state) const {
    switch (state) {
      case Follow::Connecting: return "FINDING DESK";
      case Follow::Joining: return "JOINING DESK";
      case Follow::Following: return missingCue_.empty() ? "FOLLOWING DESK" : "MISSING CUE";
      case Follow::Lost: return "LOST THE DESK";
      default: return "DECKBOY POCKET";
    }
  }

  std::string syncStatusFields() const {
    const char* names[] = {"off", "connecting", "joining", "following", "lost"};
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    o << "follow=" << names[static_cast<int>(follow_)] << " desk=" << (followHost_.empty() ? "-" : followHost_)
      << " clock=" << (clockReady_ ? "ok" : "no") << " offset_ms=" << static_cast<double>(clockOffset_) / 1e6
      << " rtt_ms=" << static_cast<double>(clockRtt_) / 1e6 << " drift_frames=" << driftFrames_
      << " seeks=" << syncSeeks_ << " cue=" << (syncCue_.empty() ? "-" : syncCue_)
      << " missing=\"" << missingCue_ << "\" error=\"" << followError_ << "\"";
    return o.str();
  }

  // MENU > LIST: how the list plays.
  void drawListTab(float lx, float lw, float& y) {
    const float u = unit_;
    auto heading = [&](const char* t) {
      text_.draw(pixelSmall_, t, kDark, {lx, y, lw, 3.5f * u});
      y += 4.2f * u;
    };
    const float bw = (lw - u) / 2;
    heading("PLAYING THE LIST");
    button({lx, y, bw, 5 * u}, shuffle_ ? "SHUFFLE ON" : "SHUFFLE", [this] { setShuffle(!shuffle_); }, true, shuffle_);
    button({lx + bw + u, y, bw, 5 * u}, loopList_ ? "LOOP LIST ON" : "LOOP LIST", [this] {
      loopList_ = !loopList_;
      if (loopList_ && listEnded_ && active_ >= 0) advanceFrom(active_);   // the end was reached: go round
    }, true, loopList_);
    y += 6 * u;
    button({lx, y, lw, 5 * u}, endAfter_ ? "STOPS AFTER THIS CUE" : "STOP AFTER THIS CUE", [this] { endAfter_ = !endAfter_; },
           active_ >= 0, endAfter_);
    y += 6.5f * u;
    heading("BETWEEN CUES");
    {
      const double steps[] = {0.0, 0.5, 1.0, 2.0};
      const char* names[] = {"CUT", "0.5 S", "1 S", "2 S"};
      const float qw = (lw - 3 * u) / 4;
      for (int k = 0; k < 4; ++k) {
        const double v = steps[k];
        button({lx + static_cast<float>(k) * (qw + u), y, qw, 5 * u}, names[k], [this, v] { glideSeconds_ = v; },
               true, std::fabs(listGlide() - v) < 1e-6);
      }
      y += 6 * u;
      const float sw = 9 * u;
      button({lx, y, sw, 5 * u}, "<", [this] { listStyle_ = stepStyle(listStyle_, -1); });
      text_.draw(mono_, styleName(listStyle_), kDeep, {lx + sw + u, y, lw - 2 * sw - 2 * u, 5 * u}, true);
      button({lx + lw - sw, y, sw, 5 * u}, ">", [this] { listStyle_ = stepStyle(listStyle_, 1); });
      y += 6 * u;
      text_.draw(mono_, "A cue's own TRANSITION and STYLE win.", kDark, {lx, y, lw, 4 * u});
      y += 5 * u;
    }
    heading("PLAY NEXT");
    {
      std::string line = "Nothing queued. PLAY NEXT is on each cue's sheet.";
      if (!queueIds_.empty()) {
        const int q = indexOfCue(queueIds_.front());
        line = (q >= 0 ? std::to_string(q + 1) + " " + cues_[static_cast<std::size_t>(q)].name : std::string("?")) +
               (queueIds_.size() > 1 ? " +" + std::to_string(queueIds_.size() - 1) + " more" : std::string());
      }
      text_.draw(mono_, line, kDeep, {lx, y, lw - 18 * u, 5 * u});
      button({lx + lw - 17 * u, y, 17 * u, 5 * u}, "CLEAR", [this] { queueIds_.clear(); }, !queueIds_.empty());
      y += 6.5f * u;
    }
    heading("SOUND");
    button({lx, y, lw, 5 * u}, muted_ ? "MUTED: TAP FOR SOUND" : "MUTE", [this] { setMuted(!muted_); }, true, muted_);
    y += 6 * u;
  }

  void drawSyncTab(float lx, float lw, float& y) {
    const float u = unit_;
    text_.draw(pixelSmall_, "FOLLOW A DESK", kDark, {lx, y, lw, 3.5f * u});
    y += 4.2f * u;
    // The desk's address: tap to type.
    const SDL_FRect field {lx, y, lw, 5 * u};
    fillRect(renderer_, kDeep, field);
    fillRect(renderer_, kLight, {field.x + 0.4f * u, field.y + 0.4f * u, field.w - 0.8f * u, field.h - 0.8f * u});
    text_.draw(mono_, followHost_.empty() ? "Tap, then type the desk's IP" : followHost_, kDeep,
               {field.x + u, field.y, field.w - 2 * u, field.h});
    hits_.push_back({field, [this] { beginHostEdit(); }});
    y += 6 * u;
    const float bw = (lw - u) / 2;
    button({lx, y, bw, 5 * u}, follow_ == Follow::Off ? "FOLLOW" : "FOLLOW AGAIN", [this] { endHostEdit(); startFollowing(); });
    button({lx + bw + u, y, bw, 5 * u}, "STOP", [this] { stopFollowing(); }, true, follow_ == Follow::Off);
    y += 6.5f * u;
    auto line = [&](const std::string& t, Ink ink) {
      text_.draw(mono_, t, ink, {lx, y, lw, 4 * u});
      y += 4.4f * u;
    };
    std::string state;
    switch (follow_) {
      case Follow::Off: state = "Not following"; break;
      case Follow::Connecting: state = "Reaching " + followHost_; break;
      case Follow::Joining: state = "Asking " + followHost_ + " to lead"; break;
      case Follow::Following: state = "Following " + followHost_; break;
      case Follow::Lost: state = "Lost " + followHost_ + ", trying again"; break;
    }
    line(state, kDeep);
    if (!followError_.empty()) line(followError_, Ink {170, 40, 30});
    if (follow_ == Follow::Following) {
      char b[96];
      std::snprintf(b, sizeof b, "Clock %+.1f ms, round trip %.1f ms", static_cast<double>(clockOffset_) / 1e6,
                    static_cast<double>(clockRtt_) / 1e6);
      line(clockReady_ ? b : "Setting the clock...", kDeep);
      if (!missingCue_.empty()) {
        line("Missing: " + missingCue_, Ink {170, 40, 30});
      } else {
        std::snprintf(b, sizeof b, "%s %.1f frames from the desk", std::fabs(driftFrames_) <= 2.0 ? "In step," : "Catching up,",
                      driftFrames_);
        line(b, kDeep);
      }
    }
    y += u;
    text_.draw(pixelSmall_, "ON THE DESK", kDark, {lx, y, lw, 3.5f * u});
    y += 4.2f * u;
    line("Settings > Network: Listen on ALL INTERFACES,", kDeep);
    line("Lead other machines: LEADING.", kDeep);
    line("Last refused: ALLOW this phone.", kDeep);
    line("Put the same media on both.", kDeep);
  }

  // While typing, the address sits over the picture: the keyboard covers the menu.
  void beginRename(int i) {
    if (i < 0 || i >= static_cast<int>(cues_.size())) return;
    endHostEdit();
    renameIndex_ = i;
    renameText_ = cues_[static_cast<std::size_t>(i)].name;
    SDL_StartTextInput(window_);
  }

  void endRename(bool keep) {
    if (renameIndex_ == -1) return;
    std::string name = renameText_;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (renameIndex_ == kNamingShow) {
      renameIndex_ = -1;
      SDL_StopTextInput(window_);
      if (keep && !name.empty()) saveShowAs(name);
      return;
    }
    if (keep && !name.empty() && renameIndex_ < static_cast<int>(cues_.size())) {
      cues_[static_cast<std::size_t>(renameIndex_)].name = name;
      cueEdited(renameIndex_, false);
    }
    renameIndex_ = -1;
    SDL_StopTextInput(window_);
  }

  // While typing, the text sits over the picture: the keyboard covers the lower half.
  void drawTextEditor(const char* title, const std::string& value, const char* hint) {
    const float u = unit_;
    const SDL_FRect box {program_.x, program_.y, program_.w, 16 * u};
    fillRound(renderer_, kBezel, box, 3 * u);
    text_.draw(pixelSmall_, title, Ink {170, 170, 180}, {box.x + 3 * u, box.y + 1.5f * u, box.w - 6 * u, 3.2f * u});
    const SDL_FRect field {box.x + 2.5f * u, box.y + 5.5f * u, box.w - 5 * u, 6 * u};
    fillRect(renderer_, kLight, field);
    const bool caret = (SDL_GetTicks() / 500) % 2 == 0;
    text_.draw(mono_, value + (caret ? "_" : " "), kDeep, {field.x + u, field.y, field.w - 2 * u, field.h});
    text_.draw(pixelSmall_, hint, Ink {170, 170, 180}, {box.x + 3 * u, box.y + 12.3f * u, box.w - 6 * u, 3.2f * u});
  }

  // -- Direction: reversed and bounced copies, made in the background --
  //
  // engine/direction_inproc.cpp makes them with libav (Pocket has no ffmpeg
  // program): the range decoded once, every frame a JPEG, written last-first.
  // One copy at a time; a cue starts its copy once its direction and trim
  // have held still for 1.5 s (dragging a trim would otherwise start a dozen).
  // Until the copy lands, the cue plays forwards, as the desk's does.

  struct DirectionJob { std::string cueId, source, dest, key; double in = 0, out = 0; bool bounce = false; };

  std::string directionFile(const Cue& c, const std::string& key) const {
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    std::size_t h = std::hash<std::string> {}(c.path + "#" + key);
    char name[64];
    std::snprintf(name, sizeof name, "/direction/%016llx.mov", static_cast<unsigned long long>(h));
    return std::string(state ? state : ".") + name;
  }

  bool directionReady(const Cue& c) const {
    if (!cueDirectionCopyFits(c)) return false;
    SDL_PathInfo info;
    return SDL_GetPathInfo(c.directionPath.c_str(), &info) && info.size > 0;
  }

  std::string directionStatus(const Cue& c) const {
    if (directionReady(c)) return c.playDirection == kCueBounce ? "Bounces, copy ready" : "Reversed, copy ready";
    if (dirBusy_.load() && dirJob_.cueId == c.id && dirJob_.key == cueDirectionKey(c)) {
      char b[64];
      std::snprintf(b, sizeof b, "Making the copy... %d%%", static_cast<int>(dirProgress_.load() * 100));
      return b;
    }
    if (!dirFailed_.empty() && dirFailedKey_ == c.id + "|" + cueDirectionKey(c)) return "No copy: " + dirFailed_;
    return "Plays forwards until the copy is made";
  }

  void serviceDirection() {
    // A finished job: give the cue its copy if it still wants that one.
    if (dirFinished_.load()) {
      if (dirThread_.joinable()) dirThread_.join();
      dirFinished_ = false;
      dirBusy_ = false;
      for (std::size_t i = 0; i < cues_.size(); ++i) {
        Cue& c = cues_[i];
        if (c.id != dirJob_.cueId) continue;
        if (dirResult_ == deckboy::engine::RemuxResult::Done && cueDirectionKey(c) == dirJob_.key) {
          c.directionPath = dirJob_.dest;
          c.directionKey = dirJob_.key;
          c.directionSeconds = dirMade_.seconds;
          notice_ = (c.playDirection == kCueBounce ? "Bounce ready: " : "Reverse ready: ") + c.name;
          noticeUntil_ = SDL_GetTicks() + 2500;
          // On air: start the copy now, from its beginning, with a short mix.
          if (static_cast<int>(i) == active_) engine_->loadCue(&c, engine_->state() == TransportState::Playing, 0.2,
                                                                TransitionStyle::Crossfade);
        } else if (dirResult_ != deckboy::engine::RemuxResult::Done && dirResult_ != deckboy::engine::RemuxResult::Cancelled) {
          dirFailed_ = dirWhy_;
          dirFailedKey_ = c.id + "|" + dirJob_.key;
          SDL_Log("pocket: direction copy failed for %s: %s", c.name.c_str(), dirWhy_.c_str());
        }
      }
    }
    if (dirBusy_.load()) {
      // A copy nobody wants any more (direction or trim changed): stop it.
      bool wanted = false;
      for (const Cue& c : cues_) wanted |= c.id == dirJob_.cueId && cueDirectionKey(c) == dirJob_.key && c.playDirection != kCueForward;
      if (!wanted) dirCancel_ = true;
      return;
    }
    // The next cue that needs a copy, once it has held still.
    const Uint64 now = SDL_GetTicks();
    for (const Cue& c : cues_) {
      if (c.playDirection == kCueForward || !cueDirectionApplies(c) || directionReady(c)) continue;
      const std::string want = c.id + "|" + cueDirectionKey(c);
      if (want == dirFailedKey_) continue;   // failed once: not again until it changes
      if (want != dirWaitKey_) { dirWaitKey_ = want; dirWaitSince_ = now; return; }
      if (now - dirWaitSince_ < 1500) return;
      DirectionJob job;
      job.cueId = c.id;
      job.source = c.path;
      job.key = cueDirectionKey(c);
      job.dest = directionFile(c, job.key);
      job.in = cueDirectionIn(c);
      job.out = cueDirectionOut(c);
      job.bounce = c.playDirection == kCueBounce;
      SDL_CreateDirectory(job.dest.substr(0, job.dest.find_last_of('/')).c_str());
      dirJob_ = job;
      dirProgress_ = 0.0;
      dirCancel_ = false;
      dirBusy_ = true;
      dirThread_ = std::thread([this, job] {
        std::string why;
        deckboy::engine::DirectionCopy made;
        const auto result = deckboy::engine::bakeDirectionInProcess(job.source, job.dest, job.in, job.out, job.bounce,
                                                                    1920, &dirProgress_, &dirCancel_, &why, &made);
        dirResult_ = result;
        dirWhy_ = why;
        dirMade_ = made;
        dirFinished_ = true;
      });
      return;
    }
  }

  static std::string speedLabel(double s) {
    char b[24];
    std::snprintf(b, sizeof b, "%.2gx", s);
    return std::fabs(s - 1.0) < 1e-6 ? std::string("1x (normal)") : std::string(b);
  }

  void stepSpeed(int by) {
    if (sheet_ < 0) return;
    static const double kSteps[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0};
    Cue& c = cues_[static_cast<std::size_t>(sheet_)];
    int i = 3;
    for (int k = 0; k < 9; ++k) if (std::fabs(kSteps[k] - c.playbackSpeed) < 1e-6) i = k;
    c.playbackSpeed = kSteps[std::clamp(i + by, 0, 8)];
    if (sheet_ == active_) engine_->refreshActiveCueRuntime(&c);
  }

  // -- Rendered shells (pocket-skins/devices.py, TALL family) --
  //
  // A shell is a photograph of the device straight down, rendered in Blender
  // from Pocket's own layout (72 mm wide, 156 tall at u = 1 mm), with the
  // screens left dark. Pocket draws the picture, the cue list, the header and
  // button presses into it. A phone taller or shorter than 156 mm of layout
  // stretches only a band through the middle of the cue list, which holds
  // nothing but the bezel's straight sides.

  struct ShellInfo { std::string slug, name; Ink ink; };

  void loadShells() {
    size_t n = 0;
    char* index = static_cast<char*>(SDL_LoadFile("data/pocket-shells/index.txt", &n));
    if (!index) return;
    std::istringstream in(std::string(index, n));
    SDL_free(index);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const std::size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
      if (b == std::string::npos) continue;
      ShellInfo info;
      info.slug = line.substr(0, a);
      info.name = line.substr(a + 1, b - a - 1);
      int cr = 46, cg = 46, cb = 70;
      std::sscanf(line.c_str() + b + 1, "%d %d %d", &cr, &cg, &cb);
      info.ink = {static_cast<Uint8>(cr), static_cast<Uint8>(cg), static_cast<Uint8>(cb)};
      shells_.push_back(info);
    }
    size_t m = 0;
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    if (char* saved = static_cast<char*>(SDL_LoadFile((std::string(state ? state : ".") + "/pocket_shell.txt").c_str(), &m))) {
      std::string want(saved, m);
      SDL_free(saved);
      while (!want.empty() && (want.back() == '\n' || want.back() == '\r' || want.back() == ' ')) want.pop_back();
      setShell(want);
    }
  }

  // "" is the flat drawn chrome (the theme's own colours).
  void setShell(const std::string& slug) {
    if (shellTex_) {
      SDL_DestroyTexture(shellTex_);
      shellTex_ = nullptr;
    }
    shell_.clear();
    const auto it = std::find_if(shells_.begin(), shells_.end(), [&](const ShellInfo& s) { return s.slug == slug; });
    if (!slug.empty() && it != shells_.end()) {
#if defined(DECKBOY_INPROC_DECODE)
      // libav reads files, and an Android app's assets are not files: copy it out once.
      const char* root = SDL_getenv("DECKBOY_ROOT");
      const std::string file = std::string(root ? root : ".") + "/data/pocket-shells/" + slug + ".jpg";
      // Copied every time it is chosen (150 KB): a copy kept from an older
      // install showed the old face after an update.
      {
        size_t n = 0;
        if (void* bytes = SDL_LoadFile(("data/pocket-shells/" + slug + ".jpg").c_str(), &n)) {
          SDL_CreateDirectory(file.substr(0, file.find_last_of('/')).c_str());
          SDL_SaveFile(file.c_str(), bytes, n);
          SDL_free(bytes);
        }
      }
      int w = 0, h = 0;
      std::vector<std::uint8_t> rgba;
      if (deckboy::libav::decodeImageRgba(file, w, h, rgba) && w > 0 && h > 0) {
        shellTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (shellTex_) {
          SDL_UpdateTexture(shellTex_, nullptr, rgba.data(), w * 4);
          SDL_SetTextureScaleMode(shellTex_, SDL_SCALEMODE_LINEAR);   // a photograph, drawn smaller than it is
          shellW_ = w;
          shellH_ = h;
          shell_ = slug;
          shellInk_ = it->ink;
        }
      } else {
        SDL_Log("pocket: could not read the shell %s", file.c_str());
      }
#endif
    }
    text_.clear();   // the header's ink changed
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    const std::string text = shell_ + "\n";
    SDL_SaveFile((std::string(state ? state : ".") + "/pocket_shell.txt").c_str(), text.data(), text.size());
  }

  void stepShell(int by) {
    // -1 is the flat chrome, then each shell in order
    int i = -1;
    for (std::size_t k = 0; k < shells_.size(); ++k) if (shells_[k].slug == shell_) i = static_cast<int>(k);
    const int count = static_cast<int>(shells_.size()) + 1;
    i = ((i + 1 + by) % count + count) % count - 1;
    setShell(i < 0 ? std::string() : shells_[static_cast<std::size_t>(i)].slug);
  }

  // The rendered face covers the portrait show screen; editing pages and
  // landscape have their own shapes and draw the flat chrome.
  bool shellFace() const { return shellTex_ && !landscape_ && !editing_; }

  std::string shellName() const {
    for (const ShellInfo& s : shells_) if (s.slug == shell_) return s.name;
    return "Flat";
  }

  void drawShell() {
    SDL_Renderer* r = renderer_;
    fillRect(r, kBlack, {0, 0, pxW_, pxH_});
    const float u = unit_;
    // The plate is the app's face (pocket-skins TALLAPP): 72 wide, Pocket's
    // 156-unit show layout with plastic running on past its top and bottom,
    // which fills a camera cut-out and a gesture bar. The band through the
    // cue list (66 to 105.5 in the layout) stretches to fit the phone.
    const float k = static_cast<float>(shellW_) / 72.0f;          // plate pixels per unit
    const float plateUnits = static_cast<float>(shellH_) / k;     // 184
    const float margin = std::max(0.0f, (plateUnits - 156.0f) / 2);
    const float tall = safe_.h / u;
    auto part = [&](float fromLayout, float toLayout, float atLayout, float heightLayout) {
      const SDL_FRect src {0, (fromLayout + margin) * k, static_cast<float>(shellW_), (toLayout - fromLayout) * k};
      const SDL_FRect dst {safe_.x, safe_.y + atLayout * u, safe_.w, heightLayout * u};
      SDL_RenderTexture(r, shellTex_, &src, &dst);
    };
    const float cutTop = 66.0f, cutBottom = 105.5f;   // the stretchable band, in the layout's units
    const float below = 156.0f - cutBottom;            // 50.5: the list's foot and the button strip
    if (tall < cutTop + below) {
      part(-margin, 156.0f + margin, -margin, tall + 2 * margin);   // too short to cut: squeeze it whole
      return;
    }
    part(-margin, cutTop, -margin, cutTop + margin);
    part(cutBottom, 156.0f + margin, tall - below, below + margin);
    part(cutTop, cutBottom, cutTop, tall - below - cutTop);
  }

  // The shell's buttons are in the picture; a press darkens the one pressed,
  // and BLACK shows red while the output is blacked out.
  void drawShellPresses() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    auto dim = [&](const SDL_FRect& rect, float radius) { fillRoundAlpha(r, kBlack, 0.35f, rect, radius); };
    const float arm = 13 * u, half = 4.6f * u;
    const SDL_FPoint c = padCentre_;
    switch (pressed_) {
      case Pressed::Up: dim({c.x - half, c.y - arm, 2 * half, arm - half}, 1.2f * u); break;
      case Pressed::Down: dim({c.x - half, c.y + half, 2 * half, arm - half}, 1.2f * u); break;
      case Pressed::Left: dim({c.x - arm, c.y - half, arm - half, 2 * half}, 1.2f * u); break;
      case Pressed::Right: dim({c.x + half, c.y - half, arm - half, 2 * half}, 1.2f * u); break;
      case Pressed::Go: dim({goCentre_.x - 7.5f * u, goCentre_.y - 7.5f * u, 15 * u, 15 * u}, 7.5f * u); break;
      case Pressed::Back: dim({backCentre_.x - 6 * u, backCentre_.y - 6 * u, 12 * u, 12 * u}, 6 * u); break;
      case Pressed::Menu: dim(menuPill_, 2 * u); break;
      case Pressed::Black: dim(blackPill_, 2 * u); break;
      default: break;
    }
    if (pillLit()) fillRoundAlpha(r, Ink {220, 30, 30}, 0.6f, blackPill_, 2 * u);
    // the face leaves this pill unlabelled: it does what you chose
    // ...printed as the face prints MENU: the same type, size, ink and place
    // (centred 4.4 mm below the pill's centre)
    text_.draw(printFont_ ? printFont_ : pixelSmall_, pillName(pill_), shellInk_,
               {blackPill_.x - 4 * u, blackPill_.y + blackPill_.h / 2 + 4.4f * u - 1.5f * u, blackPill_.w + 8 * u, 3 * u}, true);
  }

  // -- Adding media: the system picker --
  //
  // Android's own picker (Gallery, Files, Drive): the phone gives Pocket the
  // files picked and nothing else, so Pocket never asks to read every photo.

  static void SDLCALL picked(void* self, const char* const* files, int) {
    auto* p = static_cast<Pocket*>(self);
    std::lock_guard<std::mutex> hold(p->pickLock_);
    p->pickerOpen_ = false;
    if (!files) return;   // an error; nothing picked
    for (const char* const* f = files; *f; ++f) p->picks_.emplace_back(*f);
  }

  void openPicker() {
    {
      std::lock_guard<std::mutex> hold(pickLock_);
      if (pickerOpen_) return;
      pickerOpen_ = true;
    }
    static const SDL_DialogFileFilter kFilters[] = {
        {"Video, pictures, sound and PDF decks",
         "mp4;mov;m4v;mkv;webm;avi;mxf;jpg;jpeg;png;heic;heif;webp;gif;mp3;wav;m4a;aac;flac;ogg;pdf"},
    };
    SDL_ShowOpenFileDialog(&Pocket::picked, this, window_, kFilters, 1, nullptr, true);
  }

  // Main thread: turn what was picked into cues.
  void takePicks() {
    std::vector<std::string> picks;
    {
      std::lock_guard<std::mutex> hold(pickLock_);
      picks.swap(picks_);
    }
    bool show = false;
    {
      std::lock_guard<std::mutex> hold(pickLock_);
      if (!pickerOpen_ && !picks.empty()) { show = pickingShow_; pickingShow_ = false; }
      else if (!pickerOpen_) pickingShow_ = false;
    }
    for (const std::string& uri : picks) {
      if (show) importShow(uri);
      else addPicked(uri);
    }
  }

  void addPicked(const std::string& uri) {
    std::string name, mime;
#if defined(SDL_PLATFORM_ANDROID)
    name = bridgeString("displayName", uri);
    mime = bridgeString("mimeType", uri);
    bridgeKeepAccess(uri);
#endif
    if (name.empty()) name = uri.substr(uri.find_last_of('/') + 1);
    const std::string stem = name.substr(0, name.find_last_of('.'));
    const std::size_t before = cues_.size();
    if (mime == "application/pdf" || endsWithNoCase(name, ".pdf")) {
      addPdf(uri, stem);
    } else if (std::optional<Cue> probed = deckboy::core::media::probeCue(dbfs::path(uri))) {
      Cue cue = std::move(*probed);
      cue.path = uri;
      cue.name = stem;
      if (mime.rfind("image/", 0) == 0) cue.kind = CueKind::Image;
      else if (mime.rfind("audio/", 0) == 0) cue.kind = CueKind::Audio;
      if (cue.kind == CueKind::Image) cue.stillDurationSeconds = 0.0;
      cue.pauseOnLastFrame = true;
      cue.id = "pocket-" + std::to_string(++nextId_);
      applyDefaults(cue);   // what new cues start with (MENU > SHOW)
      cues_.push_back(std::move(cue));
    } else {
      SDL_Log("pocket: could not read %s (%s)", name.c_str(), uri.c_str());
      notice_ = "Could not read " + name;
      noticeUntil_ = SDL_GetTicks() + 4000;
    }
    if (cues_.size() > before) {
      notice_ = cues_.size() - before == 1 ? "Added " + stem
                                          : "Added " + std::to_string(cues_.size() - before) + " cues from " + stem;
      noticeUntil_ = SDL_GetTicks() + 3000;
      select(static_cast<int>(before));
    }
  }

  // A PDF deck: every page rendered once, now, by Android's own PDF engine,
  // into an image cue per page -- the desk's import, on the phone.
  void addPdf(const std::string& uri, const std::string& stem) {
#if defined(SDL_PLATFORM_ANDROID)
    const char* root = SDL_getenv("DECKBOY_STATE_DIR");
    const std::string dir = joinPath(root ? root : ".", "slides/deck-" + std::to_string(++nextId_));
    const int pages = bridgeRenderPdf(uri, dir, 1920);
    if (pages <= 0) {
      notice_ = "Could not open the PDF " + stem;
      noticeUntil_ = SDL_GetTicks() + 4000;
      return;
    }
    for (int i = 1; i <= pages; ++i) {
      char file[32];
      std::snprintf(file, sizeof file, "page-%03d.png", i);
      const std::string path = joinPath(dir, file);
      std::optional<Cue> probed = deckboy::core::media::probeCue(dbfs::path(path));
      Cue cue = probed ? std::move(*probed) : Cue {};
      cue.kind = CueKind::Image;
      cue.path = path;
      cue.name = stem + "  " + std::to_string(i) + "/" + std::to_string(pages);
      cue.stillDurationSeconds = 0.0;
      cue.pauseOnLastFrame = true;
      cue.id = "pocket-" + std::to_string(++nextId_);
      applyDefaults(cue);   // what new cues start with (MENU > SHOW)
      cues_.push_back(std::move(cue));
    }
#else
    (void) uri;
    notice_ = "PDF decks open on the phone: " + stem;
    noticeUntil_ = SDL_GetTicks() + 3000;
#endif
  }

  // -- Input --

  void press(Pressed p) {
    pressed_ = p;
    pressedUntil_ = SDL_GetTicks() + 160;
    switch (p) {   // each control its own feel (PocketBridge.haptic)
      case Pressed::Go: buzz("go"); break;
      case Pressed::Back: buzz("back"); break;
      case Pressed::Up: case Pressed::Down: case Pressed::Left: case Pressed::Right: buzz("pad"); break;
      case Pressed::Black: buzz("black"); break;
      case Pressed::Menu: buzz("menu"); break;
      default: break;
    }
  }

  // Returns true when the press must do nothing else.
  bool codeKey(Pressed p) {
    const bool other = aboutPage() && codeKey2(p);
    return codeKey1(p) || other;
  }

  bool codeKey2(Pressed p) {
    static constexpr Pressed kCode[] = {Pressed::Go, Pressed::Back, Pressed::Go, Pressed::Up, Pressed::Go, Pressed::Back, Pressed::Back};
    constexpr std::size_t kLen = sizeof(kCode) / sizeof(kCode[0]);
    const Uint64 now = SDL_GetTicks();
    if (now - code2LastMs_ > 3000) code2Seen_.clear();
    code2LastMs_ = now;
    code2Seen_.push_back(p);
    if (code2Seen_.size() > kLen) code2Seen_.erase(code2Seen_.begin());
    if (code2Seen_.size() == kLen && std::equal(code2Seen_.begin(), code2Seen_.end(), kCode)) {
      code2Seen_.clear();
      unlockTabletop();
      return true;
    }
    return false;
  }

  void unlockTabletop() {
    if (tabletopUnlocked_) {
      notice_ = "Tabletop is in MENU > SOURCES";
      noticeUntil_ = SDL_GetTicks() + 3000;
      return;
    }
    tabletopUnlocked_ = true;
    saveUnlocks();
    addGenerator("tabletop");
    notice_ = "TABLETOP UNLOCKED";
    noticeUntil_ = SDL_GetTicks() + 4000;
    buzz("go");
  }

  bool codeKey1(Pressed p) {
    static constexpr Pressed kCode[] = {Pressed::Up, Pressed::Up, Pressed::Down, Pressed::Down, Pressed::Left, Pressed::Right,
                                        Pressed::Left, Pressed::Right, Pressed::Back, Pressed::Go, Pressed::Menu};
    constexpr int kLen = static_cast<int>(sizeof(kCode) / sizeof(kCode[0]));
    const Uint64 now = SDL_GetTicks();
    if (now - codeLastMs_ > 3000) codeIndex_ = 0;
    codeLastMs_ = now;
    if (p == kCode[codeIndex_]) {
      ++codeIndex_;
      if (codeIndex_ >= kLen) {
        codeIndex_ = 0;
        unlockTerrarium();
        return true;
      }
      return codeIndex_ > 4;
    }
    codeIndex_ = p == kCode[0] ? (codeIndex_ == 2 ? 2 : 1) : 0;
    return false;
  }

  void unlockTerrarium() {
    terrariumUnlocked_ = true;
    saveUnlocks();
    addGenerator("terrarium");
    if (selected_ >= 0 && selected_ < static_cast<int>(cues_.size())) cues_[static_cast<std::size_t>(selected_)].name = "TERRARIUM (secret)";
    notice_ = "TERRARIUM UNLOCKED";
    noticeUntil_ = SDL_GetTicks() + 4000;
    buzz("go");
  }

  void saveUnlocks() {
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    const std::string text = std::string("terrarium\t") + (terrariumUnlocked_ ? "1" : "0") + "\ntabletop\t" + (tabletopUnlocked_ ? "1" : "0") + "\n";
    SDL_SaveFile((std::string(state ? state : ".") + "/pocket_unlocks.txt").c_str(), text.data(), text.size());
  }

  void loadUnlocks() {
    size_t n = 0;
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    char* saved = static_cast<char*>(SDL_LoadFile((std::string(state ? state : ".") + "/pocket_unlocks.txt").c_str(), &n));
    if (!saved) return;
    const std::string text(saved, n);
    SDL_free(saved);
    terrariumUnlocked_ = text.find("terrarium\t1") != std::string::npos;
    tabletopUnlocked_ = text.find("tabletop\t1") != std::string::npos;
  }

  // The left pill (SELECT on a handheld): BLACK unless you choose otherwise.
  enum class Pill { Black, PlayPause, Mute, PlayNext, StopAfter, Shuffle, FullScreen, Identify, Count };

  static const char* pillName(Pill p) {
    switch (p) {
      case Pill::Black: return "BLACK";
      case Pill::PlayPause: return "PLAY/PAUSE";
      case Pill::Mute: return "MUTE";
      case Pill::PlayNext: return "PLAY NEXT";
      case Pill::StopAfter: return "STOP AFTER";
      case Pill::Shuffle: return "SHUFFLE";
      case Pill::FullScreen: return "FULL SCREEN";
      case Pill::Identify: return "IDENTIFY";
      default: return "";
    }
  }

  bool pillLit() const {
    switch (pill_) {
      case Pill::Black: return black_;
      case Pill::Mute: return muted_;
      case Pill::StopAfter: return endAfter_;
      case Pill::Shuffle: return shuffle_;
      case Pill::PlayPause: return active_ >= 0 && engine_->state() == TransportState::Paused;
      default: return false;
    }
  }

  void doPill() {
    switch (pill_) {
      case Pill::Black: black_ = !black_; buzz("black"); break;
      case Pill::PlayPause:
        if (active_ < 0) go();
        else if (engine_->state() == TransportState::Playing) engine_->pause();
        else engine_->play();
        break;
      case Pill::Mute: setMuted(!muted_); break;
      case Pill::PlayNext:
        toggleQueued(selected_);
        notice_ = queuePlace(cues_[static_cast<std::size_t>(selected_)]) ? "Plays next: " + cues_[static_cast<std::size_t>(selected_)].name : "Off the queue";
        noticeUntil_ = SDL_GetTicks() + 2000;
        break;
      case Pill::StopAfter: endAfter_ = !endAfter_; break;
      case Pill::Shuffle: setShuffle(!shuffle_); break;
      case Pill::FullScreen: phoneFull_ = true; ghostUntil_ = SDL_GetTicks() + 3500; break;
      case Pill::Identify: identifyUntil_ = SDL_GetTicks() + 4000; break;
      default: break;
    }
  }

  void setPill(Pill p) {
    pill_ = p;
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    const std::string text = std::to_string(static_cast<int>(p)) + "\n";
    SDL_SaveFile((std::string(state ? state : ".") + "/pocket_pill.txt").c_str(), text.data(), text.size());
  }

  // SNAP <file>: the frame about to be shown, as a BMP (every screen, full screen too)
  void takeSnap() {
    if (snapPath_.empty()) return;
    if (SDL_Surface* shot = SDL_RenderReadPixels(renderer_, nullptr)) {
      if (!SDL_SaveBMP(shot, snapPath_.c_str())) SDL_Log("pocket: SNAP failed: %s", SDL_GetError());
      SDL_DestroySurface(shot);
    }
    snapPath_.clear();
  }

  void buzz(const char* kind) {
    if (!haptics_) return;
#if defined(SDL_PLATFORM_ANDROID)
    bridgeString("haptic", kind);
#else
    (void)kind;
#endif
  }

  void setHaptics(bool on) {
    haptics_ = on;
    const char* state = SDL_getenv("DECKBOY_STATE_DIR");
    const std::string text = on ? "1\n" : "0\n";
    SDL_SaveFile((std::string(state ? state : ".") + "/pocket_haptics.txt").c_str(), text.data(), text.size());
    if (on) buzz("go");
  }

  enum class Drag { None, Scrub, TrimIn, TrimOut, Volume, Slider };

  void onDown(float x, float y) {
    downX_ = x;
    downY_ = y;
    if (phoneFull_) {   // full screen: a tap shows the ghost controls (EXIT leaves)
      ghostTap(x, y);
      return;
    }
    // Sheet controls and LCD buttons first (the last drawn sit on top).
    if (sheet_ >= 0) {
      Cue& c = cues_[static_cast<std::size_t>(sheet_)];
      if (cueHasTimeline(c) && inside(grow(trimBar_, unit_ * 1.5f), x, y)) {
        const float xi = trimBar_.x + trimBar_.w * static_cast<float>(cueIn(c) / c.duration);
        const float xo = trimBar_.x + trimBar_.w * static_cast<float>(cueOut(c) / c.duration);
        drag_ = std::fabs(x - xi) <= std::fabs(x - xo) ? Drag::TrimIn : Drag::TrimOut;
        onMove(x, y);
        return;
      }
      if (inside(grow(volumeBar_, unit_), x, y)) {
        drag_ = Drag::Volume;
        onMove(x, y);
        return;
      }
    }
    // A slider's lower half grabs it. Touching does not change the value; only
    // dragging does (onMove), so a thumb landing on a row is harmless.
    for (std::size_t k = sliders_.size(); k-- > 0;) {
      if (inside(sliders_[k].area, x, y)) {
        activeSlider_ = static_cast<int>(k);
        drag_ = Drag::Slider;
        sliderStartX_ = x;
        sliderStartY_ = y;
        sliderStartValue_ = sliders_[k].spec.get();
        return;
      }
    }
    // Holding the picture during a show (no sheet, no menu) opens PLACE for the live cue.
    if (sheet_ < 0 && !menuOpen_ && active_ >= 0 && inside(picRect_, x, y)) picHoldSince_ = SDL_GetTicks();
    // A finger is not a mouse: every button answers across at least Android's
    // 48 dp touch target (about 9 mm), even where it is drawn smaller. A touch
    // inside a button is that button; a touch near several goes to the
    // nearest one, so growing the targets never makes a neighbour unreachable.
    {
      auto fire = [this](std::size_t k) {
        auto fn = hits_[k].fn;   // the handler may rebuild hits_
        buzz("tap");
        fn();
      };
      for (std::size_t k = hits_.size(); k-- > 0;) {
        if (inside(hits_[k].rect, x, y)) { fire(k); return; }
      }
      const float minPx = 48.0f * std::max(1.0f, SDL_GetWindowDisplayScale(window_));
      std::size_t best = hits_.size();
      float bestD = 1e9f;
      for (std::size_t k = 0; k < hits_.size(); ++k) {
        const SDL_FRect& b = hits_[k].rect;
        const float gw = std::max(0.0f, (minPx - b.w) / 2), gh = std::max(0.0f, (minPx - b.h) / 2);
        if (!inside({b.x - gw, b.y - gh, b.w + 2 * gw, b.h + 2 * gh}, x, y)) continue;
        const float dx = std::max({b.x - x, 0.0f, x - (b.x + b.w)});
        const float dy = std::max({b.y - y, 0.0f, y - (b.y + b.h)});
        const float d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = k; }
      }
      if (best < hits_.size()) { fire(best); return; }
    }
    if (sheet_ >= 0) return;
    if (menuOpen_) { tap(x, y); return; }
    if (active_ >= 0 && cueHasTimeline(cues_[static_cast<std::size_t>(active_)]) && inside(grow(scrub_, unit_), x, y)) {
      drag_ = Drag::Scrub;
      onMove(x, y);
      return;
    }
    if (inside(listLcd_, x, y) && rowH_ > 0) {
      const int row = scroll_ + static_cast<int>((y - listLcd_.y) / rowH_);
      if (row >= 0 && row < static_cast<int>(cues_.size())) {
        holdRow_ = row;
        holdStart_ = SDL_GetTicks();
        swipeRow_ = row;
        swiping_ = false;
        swipeDx_ = 0;
      }
    }
    tap(x, y);
  }

  void onMove(float x, float y) {
    if (holdRow_ >= 0 && (std::fabs(x - downX_) > 2 * unit_ || std::fabs(y - downY_) > 2 * unit_)) holdRow_ = -1;
    if (swipeRow_ >= 0) {
      const float dx = x - downX_, dy = y - downY_;
      if (!swiping_ && std::fabs(dx) > 2.5f * unit_ && std::fabs(dx) > std::fabs(dy) * 1.5f) swiping_ = true;
      else if (!swiping_ && std::fabs(dy) > 2.5f * unit_) swipeRow_ = -1;   // a scroll, not a swipe
      if (swiping_) {
        const bool wasArmed = -swipeDx_ >= kSwipeDelete * unit_;
        swipeDx_ = std::min(0.0f, dx);
        if (!wasArmed && -swipeDx_ >= kSwipeDelete * unit_) buzz("tap");   // you can feel where it will delete
        return;
      }
    }
    if (picHoldSince_ && (std::fabs(x - downX_) > 2 * unit_ || std::fabs(y - downY_) > 2 * unit_)) picHoldSince_ = 0;
    if (drag_ == Drag::Slider && activeSlider_ >= 0 && activeSlider_ < static_cast<int>(sliders_.size())) {
      // By how far the finger moved, not where it is. A finger dragged
      // further above or below the row moves the value more finely: one
      // tenth at about 4 cm away (the way a phone's media scrubber works).
      const Slider& sl = sliders_[static_cast<std::size_t>(activeSlider_)];
      const float away = std::fabs(y - (sl.track.y + sl.track.h / 2));
      const double fine = std::clamp(1.0 - static_cast<double>(away / (40 * unit_)) * 0.9, 0.1, 1.0);
      const double perPx = (sl.spec.max - sl.spec.min) / std::max(1.0f, sl.track.w);
      setNum(sl.spec, sliderStartValue_ + (x - sliderStartX_) * perPx * fine);
      // re-anchor, so changing fineness mid-drag does not jump the value
      sliderStartValue_ = sl.spec.get();
      sliderStartX_ = x;
      return;
    }
    if (drag_ == Drag::Scrub && active_ >= 0) {
      const Cue& c = cues_[static_cast<std::size_t>(active_)];
      const double t = c.duration * std::clamp(static_cast<double>((x - scrub_.x) / scrub_.w), 0.0, 1.0);
      engine_->seek(std::clamp(t, cueIn(c), cueOut(c)));
    } else if ((drag_ == Drag::TrimIn || drag_ == Drag::TrimOut) && sheet_ >= 0) {
      Cue& c = cues_[static_cast<std::size_t>(sheet_)];
      const double f = cueFrame(c);
      double t = c.duration * std::clamp(static_cast<double>((x - trimBar_.x) / trimBar_.w), 0.0, 1.0);
      t = std::round(t / f) * f;
      if (drag_ == Drag::TrimIn) c.inPointSeconds = std::clamp(t, 0.0, cueOut(c) - f);
      else c.outPointSeconds = std::clamp(t, cueIn(c) + f, c.duration);
      cueEdited(sheet_, true);
    } else if (drag_ == Drag::Volume && sheet_ >= 0) {
      Cue& c = cues_[static_cast<std::size_t>(sheet_)];
      const float k = std::clamp((x - volumeBar_.x) / volumeBar_.w, 0.0f, 1.0f);
      c.audioGainDb = std::round(-40.0f + 46.0f * k);   // -40 dB to +6 dB, whole dB
      cueEdited(sheet_, false);
    }
  }

  // ── The picture as the control (PLACE): drag moves it, pinch scales it, two
  // fingers turn it, a double-tap puts it back. Distances on the phone are
  // turned into the program's own pixels, so a drag across the picture
  // moves it across the output.
  void beginGesture() {
    if (sheet_ < 0) return;
    const Cue& c = cues_[static_cast<std::size_t>(sheet_)];
    if (!gesture_ && fingers_.size() == 1) {
      const Uint64 now = SDL_GetTicks();
      if (now - lastPicTap_ < 300) { resetPlace(sheet_); buzz("spin"); lastPicTap_ = 0; }
      else lastPicTap_ = now;
    }
    gesture_ = true;
    gestureFingers_ = fingers_.size();
    SDL_FPoint c0 {0, 0};
    for (const auto& [id, p] : fingers_) { c0.x += p.x; c0.y += p.y; }
    c0.x /= static_cast<float>(fingers_.size());
    c0.y /= static_cast<float>(fingers_.size());
    gestureC0_ = c0;
    gestureD0_ = 0;
    gestureA0_ = 0;
    if (fingers_.size() >= 2) {
      auto it = fingers_.begin();
      const SDL_FPoint a = it->second, b = (++it)->second;
      gestureD0_ = std::hypot(b.x - a.x, b.y - a.y);
      gestureA0_ = std::atan2(b.y - a.y, b.x - a.x);
    }
    gestureScale0_ = c.outputScaleX;
    gestureScaleY0_ = c.outputScaleY;
    gestureOffX0_ = c.outputOffsetX;
    gestureOffY0_ = c.outputOffsetY;
    gestureRot0_ = c.outputRotationDegrees;
  }

  void moveGesture() {
    if (sheet_ < 0 || fingers_.empty()) return;
    if (fingers_.size() != gestureFingers_) { beginGesture(); return; }
    Cue& c = cues_[static_cast<std::size_t>(sheet_)];
    SDL_FPoint cn {0, 0};
    for (const auto& [id, p] : fingers_) { cn.x += p.x; cn.y += p.y; }
    cn.x /= static_cast<float>(fingers_.size());
    cn.y /= static_cast<float>(fingers_.size());
    const float k = static_cast<float>(programW_) / std::max(1.0f, picRect_.w);   // program pixels per phone pixel
    c.outputOffsetX = std::clamp(gestureOffX0_ + (cn.x - gestureC0_.x) * k, -static_cast<float>(programW_), static_cast<float>(programW_));
    c.outputOffsetY = std::clamp(gestureOffY0_ + (cn.y - gestureC0_.y) * k, -static_cast<float>(programH_), static_cast<float>(programH_));
    if (fingers_.size() >= 2 && gestureD0_ > 1) {
      auto it = fingers_.begin();
      const SDL_FPoint a = it->second, b = (++it)->second;
      const float ratio = std::hypot(b.x - a.x, b.y - a.y) / gestureD0_;
      c.outputScaleX = std::clamp(gestureScale0_ * ratio, 0.05f, 4.0f);
      c.outputScaleY = linkScale_ ? c.outputScaleX : std::clamp(gestureScaleY0_ * ratio, 0.05f, 4.0f);
      float turn = gestureRot0_ + (std::atan2(b.y - a.y, b.x - a.x) - gestureA0_) * 57.29578f;
      while (turn > 180) turn -= 360;
      while (turn < -180) turn += 360;
      // within 2 degrees of level or a quarter turn, it settles there
      for (float snap : {-180.0f, -90.0f, 0.0f, 90.0f, 180.0f}) if (std::fabs(turn - snap) < 2.0f) turn = snap;
      c.outputRotationDegrees = turn;
    }
    cueEdited(sheet_, false);
  }

  static SDL_FRect grow(const SDL_FRect& r, float by) { return {r.x - by, r.y - by, r.w + 2 * by, r.h + 2 * by}; }

  // ── The cue sheet: hold a cue ──
  void openSheet(int i) {
    if (i < 0 || i >= static_cast<int>(cues_.size())) return;
    if (sheet_ != i) page_ = Page::Play;
    calcOpen_ = false;
    lfoSlot_ = -1;
    sheet_ = i;
    select(i);
    confirmDelete_ = false;
  }

  // ── The cue sheet, in pages (board SDX2ThoeT8cixkEYepyLBd, approved 11-19) ──
  //
  // PLAY, PLACE, LOOK, SOUND: each page fills the panel below the picture,
  // its rows share the height (about 9 mm each, a thumb's width), and nothing
  // scrolls, so a drag is never mistaken for a scroll. The page tabs and DONE
  // sit along the bottom, where a thumb holding the phone reaches.
  //
  // A number is a row: its name and value over a slider. Touching the slider
  // does not change anything; only dragging does, by how far the finger
  // moves, and a finger dragged further from the row moves the value more
  // finely. Tap the value to type it on a calculator (the desk's own sums).

  enum class Page { Play, Place, Look, Sound };

  struct NumSpec {
    std::string label, unit;
    double min = 0, max = 1, zero = 0, step = 0, reset = 0;
    int decimals = 0;
    double percentOf = 0;   // what "25%" means when typed (0: a plain hundredth)
    int lfoSlot = -1;       // a Cue::geometryLfo slot: the row gets a ~ chip
    std::string zeroText;   // what 0 says, when 0 means something ("until GO")
    std::function<double()> get;
    std::function<void(double)> set;
    std::vector<std::pair<std::string, double>> presets;
  };

  struct Slider { SDL_FRect area, track; NumSpec spec; };

  std::string numText(const NumSpec& n, double v) const {
    if (!n.zeroText.empty() && std::fabs(v) < 1e-9) return n.zeroText;
    char b[48];
    std::snprintf(b, sizeof b, "%+.*f", n.decimals, v);
    std::string t = (n.min >= 0) ? std::string(b + (b[0] == '+' ? 1 : 0)) : std::string(b);
    return n.unit.empty() ? t : t + " " + n.unit;
  }

  void setNum(const NumSpec& n, double v) {
    if (!(v == v)) return;
    v = std::clamp(v, n.min, n.max);
    if (n.step > 0) v = std::round(v / n.step) * n.step;
    n.set(v);
  }

  // One number row: name, value (tap: calculator), slider under them.
  void numberRow(const SDL_FRect& row, NumSpec n) {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    const double v = n.get();
    const float half = row.h * 0.5f;
    text_.draw(pixelSmall_, n.label, kDark, {row.x, row.y, row.w * (n.lfoSlot >= 0 ? 0.30f : 0.46f), half});
    if (n.lfoSlot >= 0 && sheet_ >= 0) {
      const int slot = n.lfoSlot;
      const bool on = cues_[static_cast<std::size_t>(sheet_)].geometryLfo[static_cast<std::size_t>(slot)].on;
      button({row.x + row.w * 0.33f, row.y + 0.3f * u, row.w * 0.12f, half - 0.3f * u}, "~", [this, slot] { openLfo(slot); }, true, on);
    }
    const std::string vt = numText(n, v);
    const SDL_FRect vbox {row.x + row.w * 0.48f, row.y + 0.3f * u, row.w * 0.52f, half - 0.3f * u};
    button(vbox, vt, [this, n] { openCalc(n); });
    // the slider: a line, a mark at zero, the stretch from zero to the value, the knob
    const SDL_FRect track {row.x + 1.5f * u, row.y + half + 0.6f * u, row.w - 3 * u, half - 1.2f * u};
    const float mid = track.y + track.h / 2;
    fillRect(r, kDark, {track.x, mid - 0.15f * u, track.w, 0.3f * u});
    auto at = [&](double val) {
      return track.x + track.w * static_cast<float>(std::clamp((val - n.min) / (n.max - n.min), 0.0, 1.0));
    };
    const float xz = at(n.zero), xv = at(v);
    fillRect(r, kDark, {xz - 0.15f * u, track.y + 0.4f * u, 0.3f * u, track.h - 0.8f * u});
    fillRect(r, kDeep, {std::min(xz, xv), mid - 0.5f * u, std::fabs(xv - xz), u});
    fillRect(r, kDeep, {xv - 1.2f * u, track.y, 2.4f * u, track.h});
    fillRect(r, kMid, {row.x, row.y + row.h - 0.15f * u, row.w, 0.3f * u});
    sliders_.push_back({{row.x, row.y + half, row.w, half}, track, std::move(n)});
  }

  // Chips: one row of choices, the chosen one lit.
  void chipRow(const SDL_FRect& row, const std::vector<std::pair<std::string, std::function<void()>>>& chips,
               const std::vector<bool>& on) {
    const float u = unit_;
    const float gap = 0.8f * u;
    const float cw = (row.w - gap * static_cast<float>(chips.size() - 1)) / static_cast<float>(chips.size());
    for (std::size_t k = 0; k < chips.size(); ++k) {
      button({row.x + static_cast<float>(k) * (cw + gap), row.y + 0.5f * u, cw, row.h - u}, chips[k].first, chips[k].second,
             true, k < on.size() && on[k]);
    }
  }

  void drawSheet() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    Cue& c = cues_[static_cast<std::size_t>(sheet_)];
    const SDL_FRect panel = sheetRect_;
    const float top = panel.y;
    const float w = panel.w;
    const float x = panel.x;
    fillRound(r, kBezel, panel, 3 * u);
    const SDL_FRect lcd {x + 2 * u, top + 2 * u, w - 4 * u, panel.h - 4 * u};
    fillRect(r, kLight, lcd);
    const float lx = lcd.x + u, lw = lcd.w - 2 * u;
    if (calcOpen_) {   // the calculator takes the whole panel while it is open
      drawCalc(lcd);
      return;
    }
    if (lfoSlot_ >= 0) {   // ...and so does an LFO
      drawLfo(lcd, c);
      return;
    }
    // the cue, at the top (information only)
    std::string title = std::to_string(sheet_ + 1) + " " + c.name;
    if (sheet_ == active_) title += "  (LIVE)";
    text_.draw(mono_, title, kDeep, {lx, lcd.y + 0.5f * u, lw, 4.5f * u});
    // the tabs and DONE, along the bottom where the thumb is
    const float barH = 7 * u;
    const float barY = lcd.y + lcd.h - barH - 0.5f * u;
    {
      const std::vector<std::pair<const char*, Page>> tabs = {
          {"PLAY", Page::Play}, {"PLACE", Page::Place}, {"LOOK", Page::Look}, {"SOUND", Page::Sound}};
      const float dw = 12 * u, gap = 0.8f * u;
      const float tw = (lw - dw - gap * static_cast<float>(tabs.size())) / static_cast<float>(tabs.size());
      for (std::size_t k = 0; k < tabs.size(); ++k) {
        const Page pg = tabs[k].second;
        button({lx + static_cast<float>(k) * (tw + gap), barY, tw, barH}, tabs[k].first, [this, pg] { page_ = pg; }, true, page_ == pg);
      }
      button({lx + lw - dw, barY, dw, barH}, "DONE", [this] { sheet_ = -1; });
    }
    // the page's rows share what is left
    struct Item { float weight; std::function<void(const SDL_FRect&)> draw; };
    std::vector<Item> items;
    switch (page_) {
      case Page::Play: playItems(c, items); break;
      case Page::Place: placeItems(c, items); break;
      case Page::Look: lookItems(c, items); break;
      case Page::Sound: soundItems(c, items); break;
    }
    float total = 0;
    for (const Item& it : items) total += it.weight;
    const float y0 = lcd.y + 5.5f * u, y1 = barY - u;
    // rows share the height: never more than 12 units, and never past the tabs
    const float rowH = std::min((y1 - y0) / std::max(1.0f, total), 12.0f * u);
    float y = y0;
    for (const Item& it : items) {
      const float h = rowH * it.weight;
      it.draw({lx, y, lw, h});
      y += h;
    }
  }

  template <class Items>
  void playItems(Cue& c, Items& items) {
    const float u = unit_;
    const int i = sheet_;
    auto secs = [](double v) { char b[16]; std::snprintf(b, sizeof b, "%.1f s", v); return std::string(b); };
    (void)secs;
    if (cueHasTimeline(c)) {
      // the filmstrip: the clip's own frames, IN and OUT handles to drag
      items.push_back({1.6f, [this, &c](const SDL_FRect& row) { drawFilmstrip(c, row); }});
      auto trimRow = [this, &c, i](bool in) {
        return [this, &c, i, in](const SDL_FRect& row) {
          const float uu = unit_;
          const double f = cueFrame(c);
          text_.draw(pixelSmall_, in ? "IN" : "OUT", kDark, {row.x, row.y, 8 * uu, row.h});
          NumSpec n;
          n.label = in ? "IN" : "OUT";
          n.unit = "s";
          n.decimals = 2;
          n.min = in ? 0.0 : cueIn(c) + f;
          n.max = in ? cueOut(c) - f : c.duration;
          n.zero = n.min;
          n.reset = in ? 0.0 : c.duration;
          n.get = [this, i, in] { const Cue& cc = cues_[static_cast<std::size_t>(i)]; return in ? cueIn(cc) : cueOut(cc); };
          n.set = [this, i, in](double t) {
            Cue& cc = cues_[static_cast<std::size_t>(i)];
            if (in) cc.inPointSeconds = t; else cc.outPointSeconds = t;
            cueEdited(i, true);
          };
          const float bw = 8 * uu, g = 0.8f * uu;
          button({row.x + 9 * uu, row.y + 0.5f * uu, row.w - 9 * uu - 3 * (bw + g), row.h - uu},
                 timecode(in ? cueIn(c) : cueOut(c), c.fps), [this, n] { openCalc(n); });
          button({row.x + row.w - 3 * bw - 2 * g, row.y + 0.5f * uu, bw, row.h - uu}, "<", [this, in, f] { nudgeTrim(in, -f); });
          button({row.x + row.w - 2 * bw - g, row.y + 0.5f * uu, bw, row.h - uu}, ">", [this, in, f] { nudgeTrim(in, f); });
          button({row.x + row.w - bw, row.y + 0.5f * uu, bw, row.h - uu}, "SET", [this, in] { if (sheet_ == active_) setInOut(in); },
                 sheet_ == active_);
        };
      };
      items.push_back({1.0f, trimRow(true)});
      items.push_back({1.0f, trimRow(false)});
    } else {
      trimBar_ = {};
      items.push_back({1.0f, [this, i](const SDL_FRect& row) {
        NumSpec n;
        n.label = "SHOW FOR";
        n.zeroText = "until GO";
        n.unit = "s";
        n.decimals = 1;
        n.min = 0;
        n.max = 120;
        n.step = 0.5;
        n.presets = {{"UNTIL GO", 0}, {"5 S", 5}, {"10 S", 10}};
        n.get = [this, i] { return cues_[static_cast<std::size_t>(i)].stillDurationSeconds; };
        n.set = [this, i](double v) { cues_[static_cast<std::size_t>(i)].stillDurationSeconds = v; cueEdited(i, false); };
        numberRow(row, n);
      }});
    }
    items.push_back({1.0f, [this, &c, i](const SDL_FRect& row) {
      const EndMode m = endModeOf(c);
      auto set = [this, i](EndMode md) { return [this, i, md] { setEndMode(cues_[static_cast<std::size_t>(i)], md); cueEdited(i, false); }; };
      chipRow(row, {{"HOLD", set(EndMode::Hold)}, {"LOOP", set(EndMode::Loop)}, {"NEXT", set(EndMode::Next)}},
              {m == EndMode::Hold, m == EndMode::Loop, m == EndMode::Next});
    }});
    if (cueDirectionApplies(c)) {
      items.push_back({1.0f, [this, &c, i](const SDL_FRect& row) {
        auto set = [this, i](int k) { return [this, i, k] { cues_[static_cast<std::size_t>(i)].playDirection = k; cueEdited(i, false); if (k) buzz("spin"); }; };
        std::string rev = "REVERSE", bou = "BOUNCE";
        if (c.playDirection != kCueForward && !directionReady(c)) {
          const int pct = dirBusy_.load() && dirJob_.cueId == c.id ? static_cast<int>(dirProgress_.load() * 100) : 0;
          (c.playDirection == kCueReverse ? rev : bou) += " " + std::to_string(pct) + "%";
        }
        chipRow(row, {{"FORWARD", set(0)}, {rev, set(1)}, {bou, set(2)}},
                {c.playDirection == 0, c.playDirection == 1, c.playDirection == 2});
      }});
      items.push_back({1.0f, [this, i](const SDL_FRect& row) {
        NumSpec n;
        n.label = "SPEED";
        n.unit = "x";
        n.decimals = 2;
        n.min = 0.25;
        n.max = 4;
        n.zero = 1;
        n.step = 0.05;
        n.reset = 1;
        n.presets = {{"HALF", 0.5}, {"NORMAL", 1}, {"DOUBLE", 2}};
        n.get = [this, i] { return cues_[static_cast<std::size_t>(i)].playbackSpeed; };
        n.set = [this, i](double v) {
          Cue& cc = cues_[static_cast<std::size_t>(i)];
          cc.playbackSpeed = v;
          if (i == active_) engine_->refreshActiveCueRuntime(&cc);
        };
        numberRow(row, n);
      }});
    }
    auto secondsRow = [this, i](const char* label, double Cue::*field) {
      return [this, i, label, field](const SDL_FRect& row) {
        NumSpec n;
        n.label = label;
        n.unit = "s";
        n.decimals = 1;
        n.min = 0;
        n.max = 10;
        n.step = 0.1;
        n.presets = {{"NONE", 0}, {"HALF", 0.5}, {"1 S", 1}};
        n.get = [this, i, field] { return cues_[static_cast<std::size_t>(i)].*field; };
        n.set = [this, i, field](double v) { cues_[static_cast<std::size_t>(i)].*field = v; cueEdited(i, false); };
        numberRow(row, n);
      };
    };
    items.push_back({1.0f, secondsRow("FADE IN", &Cue::fadeInSeconds)});
    items.push_back({1.0f, secondsRow("FADE OUT", &Cue::fadeOutSeconds)});
    items.push_back({1.0f, [this, &c, i](const SDL_FRect& row) {
      const float uu = unit_;
      text_.draw(pixelSmall_, "TRANSITION", kDark, {row.x, row.y, 20 * uu, row.h});
      const float bw = 8 * uu;
      button({row.x + 21 * uu, row.y + 0.5f * uu, bw, row.h - uu}, "<", [this] { stepCueStyle(-1); });
      const std::string label = (c.cueTransitionStyle.empty() ? styleName(listStyle_) : styleName(cueStyle(c))) + ", " +
                                (c.cueTransitionSeconds < 0 ? std::to_string(listGlide()).substr(0, 3) : std::to_string(c.cueTransitionSeconds).substr(0, 3)) + " s";
      NumSpec n;
      n.label = "TRANSITION";
      n.unit = "s";
      n.decimals = 1;
      n.min = 0;
      n.max = 10;
      n.step = 0.1;
      n.presets = {{"CUT", 0}, {"HALF", 0.5}, {"1 S", 1}};
      n.get = [this, i] { const Cue& cc = cues_[static_cast<std::size_t>(i)]; return cc.cueTransitionSeconds < 0 ? listGlide() : cc.cueTransitionSeconds; };
      n.set = [this, i](double v) { cues_[static_cast<std::size_t>(i)].cueTransitionSeconds = v; };
      button({row.x + 22 * uu + bw, row.y + 0.5f * uu, row.w - 23 * uu - 2 * bw - uu, row.h - uu}, label, [this, n] { openCalc(n); });
      button({row.x + row.w - bw, row.y + 0.5f * uu, bw, row.h - uu}, ">", [this] { stepCueStyle(1); });
    }});
    // what you do with the cue itself
    items.push_back({1.0f, [this, i](const SDL_FRect& row) {
      const int qp = queuePlace(cues_[static_cast<std::size_t>(i)]);
      chipRow(row, {{qp ? "NEXT " + std::to_string(qp) : std::string("PLAY NEXT"), [this, i] { toggleQueued(i); }},
                    {"RENAME", [this, i] { beginRename(i); }},
                    {"COPY", [this] { duplicateSheetCue(); }},
                    {"UP", [this] { moveSheetCue(-1); }},
                    {"DOWN", [this] { moveSheetCue(1); }},
                    {confirmDelete_ ? "ON AIR. SURE?" : "DELETE", [this] { deleteSheetCue(); }}},
              {qp > 0, false, false, false, false, confirmDelete_});
    }});
    (void)u;
  }

  template <class Items>
  void placeItems(Cue& c, Items& items) {
    const int i = sheet_;
    const double W = static_cast<double>(programW_), H = static_cast<double>(programH_);
    if (i != active_) {
      items.push_back({0.6f, [this](const SDL_FRect& row) {
        text_.draw(mono_, "Not on air: the picture shows it once taken.", kDark, row);
      }});
    }
    items.push_back({1.0f, [this, &c, i](const SDL_FRect& row) {
      auto set = [this, i](ScaleMode m) { return [this, i, m] { cues_[static_cast<std::size_t>(i)].scaleMode = m; cueEdited(i, false); }; };
      chipRow(row, {{"FIT", set(ScaleMode::Fit)}, {"FILL", set(ScaleMode::Fill)}, {"STRETCH", set(ScaleMode::Stretch)}, {"1:1", set(ScaleMode::Unscaled)}},
              {c.scaleMode == ScaleMode::Fit, c.scaleMode == ScaleMode::Fill, c.scaleMode == ScaleMode::Stretch, c.scaleMode == ScaleMode::Unscaled});
    }});
    items.push_back({1.0f, [this, i](const SDL_FRect& row) {
      NumSpec n;
      n.label = linkScale_ ? "SCALE" : "WIDTH";
      n.lfoSlot = kGeoLfoScaleX;
      n.unit = "%";
      n.min = 5;
      n.max = 400;
      n.zero = 100;
      n.step = 1;
      n.reset = 100;
      n.presets = {{"HALF", 50}, {"100 %", 100}, {"DOUBLE", 200}};
      n.get = [this, i] { return cues_[static_cast<std::size_t>(i)].outputScaleX * 100.0; };
      n.set = [this, i](double v) {
        Cue& cc = cues_[static_cast<std::size_t>(i)];
        cc.outputScaleX = static_cast<float>(v / 100.0);
        if (linkScale_) cc.outputScaleY = cc.outputScaleX;
        cueEdited(i, false);
      };
      numberRow(row, n);
    }});
    if (!linkScale_) {
      items.push_back({1.0f, [this, i](const SDL_FRect& row) {
        NumSpec n;
        n.label = "HEIGHT";
        n.unit = "%";
        n.min = 5;
        n.max = 400;
        n.zero = 100;
        n.step = 1;
        n.reset = 100;
        n.get = [this, i] { return cues_[static_cast<std::size_t>(i)].outputScaleY * 100.0; };
        n.set = [this, i](double v) { cues_[static_cast<std::size_t>(i)].outputScaleY = static_cast<float>(v / 100.0); cueEdited(i, false); };
        numberRow(row, n);
      }});
    }
    auto offsetRow = [this, i](bool horizontal, double extent) {
      return [this, i, horizontal, extent](const SDL_FRect& row) {
        NumSpec n;
        n.label = horizontal ? "X" : "Y";
        n.lfoSlot = horizontal ? kGeoLfoOffsetX : kGeoLfoOffsetY;
        n.unit = "px";
        n.min = -extent;
        n.max = extent;
        n.step = 1;
        n.percentOf = extent;
        n.presets = {{"CENTRE", 0}, {horizontal ? "LEFT HALF" : "UP HALF", -extent / 4}, {horizontal ? "RIGHT HALF" : "DOWN HALF", extent / 4}};
        n.get = [this, i, horizontal] { const Cue& cc = cues_[static_cast<std::size_t>(i)]; return static_cast<double>(horizontal ? cc.outputOffsetX : cc.outputOffsetY); };
        n.set = [this, i, horizontal](double v) {
          Cue& cc = cues_[static_cast<std::size_t>(i)];
          (horizontal ? cc.outputOffsetX : cc.outputOffsetY) = static_cast<float>(v);
          cueEdited(i, false);
        };
        numberRow(row, n);
      };
    };
    items.push_back({1.0f, offsetRow(true, W)});
    items.push_back({1.0f, offsetRow(false, H)});
    items.push_back({1.0f, [this, i](const SDL_FRect& row) {
      NumSpec n;
      n.label = "ROTATION";
      n.lfoSlot = kGeoLfoRotation;
      n.unit = "deg";
      n.decimals = 1;
      n.min = -180;
      n.max = 180;
      n.step = 0.5;
      n.presets = {{"LEVEL", 0}, {"90 LEFT", -90}, {"90 RIGHT", 90}};
      n.get = [this, i] { return static_cast<double>(cues_[static_cast<std::size_t>(i)].outputRotationDegrees); };
      n.set = [this, i](double v) { cues_[static_cast<std::size_t>(i)].outputRotationDegrees = static_cast<float>(v); cueEdited(i, false); };
      numberRow(row, n);
    }});
    items.push_back({1.0f, [this, &c, i](const SDL_FRect& row) {
      chipRow(row, {{"FLIP H", [this, i] { Cue& cc = cues_[static_cast<std::size_t>(i)]; cc.flipHorizontal = !cc.flipHorizontal; cueEdited(i, false); }},
                    {"FLIP V", [this, i] { Cue& cc = cues_[static_cast<std::size_t>(i)]; cc.flipVertical = !cc.flipVertical; cueEdited(i, false); }},
                    {"LINK W/H", [this, i] {
                       linkScale_ = !linkScale_;
                       if (linkScale_) { Cue& cc = cues_[static_cast<std::size_t>(i)]; cc.outputScaleY = cc.outputScaleX; cueEdited(i, false); }
                     }}},
              {c.flipHorizontal, c.flipVertical, linkScale_});
    }});
    auto cropPair = [this, i](const char* a, float Cue::*fa, const char* b, float Cue::*fb) {
      return [this, i, a, fa, b, fb](const SDL_FRect& row) {
        const float g = unit_;
        for (int k = 0; k < 2; ++k) {
          float Cue::*f = k ? fb : fa;
          NumSpec n;
          n.label = k ? b : a;
          n.unit = "%";
          n.min = 0;
          n.max = 90;
          n.step = 1;
          n.get = [this, i, f] { return cues_[static_cast<std::size_t>(i)].*f * 100.0; };
          n.set = [this, i, f](double v) { cues_[static_cast<std::size_t>(i)].*f = static_cast<float>(v / 100.0); cueEdited(i, false); };
          numberRow({row.x + static_cast<float>(k) * (row.w / 2 + g / 2), row.y, row.w / 2 - g / 2, row.h}, n);
        }
      };
    };
    items.push_back({1.0f, cropPair("CROP L", &Cue::cropLeft, "CROP R", &Cue::cropRight)});
    items.push_back({1.0f, cropPair("CROP TOP", &Cue::cropTop, "CROP BOT", &Cue::cropBottom)});
    items.push_back({1.0f, [this, i](const SDL_FRect& row) {
      chipRow(row, {{"RESET PLACE", [this, i] { resetPlace(i); }}}, {false});
    }});
  }

  void resetPlace(int i) {
    Cue& cc = cues_[static_cast<std::size_t>(i)];
    cc.outputScaleX = cc.outputScaleY = 1.0f;
    cc.outputOffsetX = cc.outputOffsetY = 0.0f;
    cc.outputRotationDegrees = 0.0f;
    cc.cropLeft = cc.cropRight = cc.cropTop = cc.cropBottom = 0.0f;
    cc.flipHorizontal = cc.flipVertical = false;
    cueEdited(i, false);
  }

  template <class Items>
  void lookItems(Cue& c, Items& items) {
    const int i = sheet_;
    auto pctRow = [this, i](const char* label, float Cue::*f) {
      return [this, i, label, f](const SDL_FRect& row) {
        NumSpec n;
        n.label = label;
        n.unit = "%";
        n.min = 0;
        n.max = 200;
        n.zero = 100;
        n.step = 1;
        n.reset = 100;
        n.presets = {{"NONE", 0}, {"NORMAL", 100}, {"DOUBLE", 200}};
        n.get = [this, i, f] { return cues_[static_cast<std::size_t>(i)].*f * 100.0; };
        n.set = [this, i, f](double v) { cues_[static_cast<std::size_t>(i)].*f = static_cast<float>(v / 100.0); cueEdited(i, false); };
        numberRow(row, n);
      };
    };
    items.push_back({1.0f, pctRow("BRIGHTNESS", &Cue::brightness)});
    items.push_back({1.0f, pctRow("CONTRAST", &Cue::contrast)});
    items.push_back({1.0f, pctRow("SATURATION", &Cue::saturation)});
    items.push_back({1.0f, [this, i](const SDL_FRect& row) {
      NumSpec n;
      n.label = "HUE";
      n.unit = "deg";
      n.min = -180;
      n.max = 180;
      n.step = 1;
      n.get = [this, i] { return static_cast<double>(cues_[static_cast<std::size_t>(i)].hueShift); };
      n.set = [this, i](double v) { cues_[static_cast<std::size_t>(i)].hueShift = static_cast<float>(v); cueEdited(i, false); };
      numberRow(row, n);
    }});
    items.push_back({1.0f, [this, &c, i](const SDL_FRect& row) {
      chipRow(row, {{c.chromaKeyEnabled ? "GREEN SCREEN ON" : "GREEN SCREEN", [this, i] {
                       Cue& cc = cues_[static_cast<std::size_t>(i)];
                       cc.chromaKeyEnabled = !cc.chromaKeyEnabled;
                       cueEdited(i, false);
                     }}}, {c.chromaKeyEnabled});
    }});
    if (c.chromaKeyEnabled) {
      auto keyRow = [this, i](const char* label, float Cue::*f, double max) {
        return [this, i, label, f, max](const SDL_FRect& row) {
          NumSpec n;
          n.label = label;
          n.min = 0;
          n.max = max;
          n.step = 1;
          n.get = [this, i, f] { return static_cast<double>(cues_[static_cast<std::size_t>(i)].*f); };
          n.set = [this, i, f](double v) { cues_[static_cast<std::size_t>(i)].*f = static_cast<float>(v); cueEdited(i, false); };
          numberRow(row, n);
        };
      };
      items.push_back({1.0f, keyRow("KEY REACH", &Cue::chromaKeyTolerance, 255)});
      items.push_back({1.0f, keyRow("KEY SOFTNESS", &Cue::chromaKeySoftness, 128)});
    }
    items.push_back({1.0f, [this, i](const SDL_FRect& row) {
      chipRow(row, {{"RESET LOOK", [this, i] {
                       Cue& cc = cues_[static_cast<std::size_t>(i)];
                       cc.brightness = cc.contrast = cc.saturation = 1.0f;
                       cc.hueShift = 0.0f;
                       cc.chromaKeyEnabled = false;
                       cueEdited(i, false);
                     }}}, {false});
    }});
  }

  template <class Items>
  void soundItems(Cue& c, Items& items) {
    const int i = sheet_;
    items.push_back({1.4f, [this](const SDL_FRect& row) { drawMeter(row, true); }});
    if (!(c.hasAudio || c.kind == CueKind::Audio)) {
      items.push_back({1.0f, [this](const SDL_FRect& row) { text_.draw(mono_, "This cue has no sound.", kDark, row); }});
      return;
    }
    items.push_back({1.0f, [this, &c, i](const SDL_FRect& row) {
      chipRow(row, {{c.audioEnabled ? "SOUND ON" : "SOUND OFF", [this, i] {
                       Cue& cc = cues_[static_cast<std::size_t>(i)];
                       cc.audioEnabled = !cc.audioEnabled;
                       if (i == active_) engine_->refreshActiveCueRuntime(&cc);
                     }},
                    {"MONO", [this, i] { Cue& cc = cues_[static_cast<std::size_t>(i)]; cc.audioMono = !cc.audioMono; cueEdited(i, false); }}},
              {c.audioEnabled, c.audioMono});
    }});
    items.push_back({1.0f, [this, i](const SDL_FRect& row) {
      NumSpec n;
      n.label = "VOLUME";
      n.unit = "dB";
      n.min = -40;
      n.max = 6;
      n.step = 1;
      n.presets = {{"-12 DB", -12}, {"0 DB", 0}, {"+6 DB", 6}};
      n.get = [this, i] { return static_cast<double>(cues_[static_cast<std::size_t>(i)].audioGainDb); };
      n.set = [this, i](double v) { cues_[static_cast<std::size_t>(i)].audioGainDb = static_cast<float>(v); cueEdited(i, false); };
      numberRow(row, n);
    }});
    items.push_back({1.0f, [this, i](const SDL_FRect& row) {
      NumSpec n;
      n.label = "PAN";
      n.min = -100;
      n.max = 100;
      n.step = 1;
      n.presets = {{"LEFT", -100}, {"CENTRE", 0}, {"RIGHT", 100}};
      n.get = [this, i] { return cues_[static_cast<std::size_t>(i)].audioPan * 100.0; };
      n.set = [this, i](double v) { cues_[static_cast<std::size_t>(i)].audioPan = static_cast<float>(v / 100.0); cueEdited(i, false); };
      numberRow(row, n);
    }});
    if (c.audioTrackCount > 1) {
      items.push_back({1.0f, [this, &c, i](const SDL_FRect& row) {
        std::vector<std::pair<std::string, std::function<void()>>> chips;
        std::vector<bool> on;
        for (int k = 0; k < std::min(c.audioTrackCount, 6); ++k) {
          chips.push_back({"TRACK " + std::to_string(k + 1), [this, i, k] {
                             Cue& cc = cues_[static_cast<std::size_t>(i)];
                             cc.audioTrack = k;
                             if (i == active_) engine_->refreshActiveCueRuntime(&cc);
                           }});
          on.push_back(c.audioTrack == k);
        }
        chipRow(row, chips, on);
      }});
    }
  }

  // The programme's sound, as Mini shows it: two small bars, green to amber
  // to red, a peak that hangs a moment. big = the SOUND page's own row.
  void drawMeter(const SDL_FRect& area, bool big) {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    const float gap = big ? u : 0.4f * u;
    const float bh = (area.h - gap * 3) / 2;
    for (int ch = 0; ch < 2; ++ch) {
      const SDL_FRect bar {area.x + (big ? 6 * u : 0), area.y + gap + static_cast<float>(ch) * (bh + gap), area.w - (big ? 6 * u : 0), bh};
      if (big) text_.draw(pixelSmall_, ch ? "R" : "L", kDark, {area.x, bar.y, 5 * u, bar.h});
      fillRect(r, kDeep, bar);
      const int segs = big ? 30 : 12;
      const float sw = bar.w / segs;
      const int lit = static_cast<int>(std::round(meterLevel_[ch] * segs));
      const int peak = static_cast<int>(std::round(meterPeak_[ch] * segs)) - 1;
      for (int k = 0; k < segs; ++k) {
        const float f = static_cast<float>(k) / segs;
        const Ink on = f > 0.9f ? Ink {230, 60, 50} : f > 0.7f ? Ink {230, 180, 40} : Ink {120, 210, 90};
        if (k < lit || k == peak) fillRect(r, on, {bar.x + k * sw + 0.15f * sw, bar.y + 0.15f * bar.h, sw * 0.7f, bar.h * 0.7f});
      }
    }
  }

  // Levels from the last 50 ms of programme sound, with a meter's ballistics:
  // up at once, down gently; the peak holds a second, then falls.
  void serviceMeter() {
    static std::vector<float> recent;
    recent.clear();
    const std::size_t n = engine_->copyRecentProgramAudio(recent);
    float pk[2] = {0, 0};
    const std::size_t take = std::min<std::size_t>(n, 2400 * 2);
    for (std::size_t k = n - take; k + 1 < n; k += 2) {
      pk[0] = std::max(pk[0], std::fabs(recent[k]));
      pk[1] = std::max(pk[1], std::fabs(recent[k + 1]));
    }
    const Uint64 now = SDL_GetTicks();
    for (int ch = 0; ch < 2; ++ch) {
      // a dB scale: -48 dB at the left, 0 dB full
      const float db = pk[ch] > 1e-5f ? 20.0f * std::log10(pk[ch]) : -96.0f;
      const float level = std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
      meterLevel_[ch] = level > meterLevel_[ch] ? level : std::max(level, meterLevel_[ch] - 0.04f);
      if (level >= meterPeak_[ch]) { meterPeak_[ch] = level; meterPeakAt_[ch] = now; }
      else if (now - meterPeakAt_[ch] > 1000) meterPeak_[ch] = std::max(level, meterPeak_[ch] - 0.02f);
    }
  }

  // ── The calculator: typing a number ──
  //
  // The desk's own sums (core/expression_parser.hpp: + - x / and brackets,
  // "1920x2", "960px*2"), plus NOW (the value as it stands), % (a share of
  // the output for a position, else a hundredth), and the setting's presets.
  // The picture follows as you type; CANCEL puts the value back.

  void openCalc(const NumSpec& n) {
    calcSpec_ = n;
    calcBefore_ = n.get();
    calcExpr_.clear();
    calcOpen_ = true;
  }

  std::optional<double> calcValue() const {
    std::string e;
    for (std::size_t k = 0; k < calcExpr_.size(); ++k) {
      if (calcExpr_.compare(k, 3, "NOW") == 0) {
        e += "(" + std::to_string(calcBefore_) + ")";
        k += 2;
      } else if (calcExpr_[k] == '%') {
        e += calcSpec_.percentOf > 0 ? "*" + std::to_string(calcSpec_.percentOf / 100.0) : std::string("/100");
      } else {
        e += calcExpr_[k];
      }
    }
    if (e.empty()) return std::nullopt;
    return parseNumericExpression(e);
  }

  void calcKey(const std::string& key) {
    if (key == "DEL") {
      if (calcExpr_.size() >= 3 && calcExpr_.compare(calcExpr_.size() - 3, 3, "NOW") == 0) calcExpr_.resize(calcExpr_.size() - 3);
      else if (!calcExpr_.empty()) calcExpr_.pop_back();
    } else if (key == "-X") {
      calcExpr_ = calcExpr_.empty() ? std::string("-NOW") : "-(" + calcExpr_ + ")";
    } else if (calcExpr_.size() < 40) {
      calcExpr_ += key == "x" ? std::string("*") : key;
    }
    if (const auto v = calcValue()) setNum(calcSpec_, *v);   // the picture follows
  }

  void drawCalc(const SDL_FRect& lcd) {
    const float u = unit_;
    const float lx = lcd.x + u, lw = lcd.w - 2 * u;
    float y = lcd.y + u;
    // the sum and its answer
    const SDL_FRect shown {lx, y, lw, 11 * u};
    fillRect(renderer_, kDeep, shown);
    text_.draw(pixelSmall_, calcSpec_.label, kMid, {shown.x + u, shown.y + 0.5f * u, lw - 2 * u, 3 * u});
    text_.draw(mono_, calcExpr_.empty() ? std::string("type a value or a sum") : calcExpr_, kLight,
               {shown.x + u, shown.y + 3.5f * u, lw - 2 * u, 4 * u});
    const auto v = calcValue();
    const std::string answer = calcExpr_.empty() ? "now " + numText(calcSpec_, calcBefore_)
                               : v ? "= " + numText(calcSpec_, std::clamp(*v, calcSpec_.min, calcSpec_.max)) + "   (was " + numText(calcSpec_, calcBefore_) + ")"
                                   : std::string("not a sum yet");
    text_.draw(mono_, answer, kMid, {shown.x + u, shown.y + 7 * u, lw - 2 * u, 3.6f * u});
    y += 12 * u;
    // the keys: 5 across, rows sharing the height; presets and the answer keys last
    std::vector<std::vector<std::string>> rows = {
        {"7", "8", "9", "/", "("}, {"4", "5", "6", "x", ")"}, {"1", "2", "3", "-", "%"}, {"0", ".", "DEL", "+", "NOW"}};
    const float gap = 0.8f * u;
    const float bottom = lcd.y + lcd.h - u;
    const int keyRows = 6;
    const float kh = (bottom - y - gap * (keyRows - 1)) / keyRows;
    const float kw = (lw - gap * 4) / 5;
    for (const auto& row : rows) {
      for (int k = 0; k < 5; ++k) {
        const std::string key = row[static_cast<std::size_t>(k)];
        button({lx + static_cast<float>(k) * (kw + gap), y, kw, kh}, key, [this, key] { calcKey(key); });
      }
      y += kh + gap;
    }
    // presets and -X
    {
      const auto& ps = calcSpec_.presets;
      const int count = static_cast<int>(std::min<std::size_t>(ps.size(), 3));
      const float pw = (lw - kw - gap * static_cast<float>(count)) / std::max(1, count);
      for (int k = 0; k < count; ++k) {
        const double pv = ps[static_cast<std::size_t>(k)].second;
        button({lx + static_cast<float>(k) * (pw + gap), y, pw, kh}, ps[static_cast<std::size_t>(k)].first,
               [this, pv] { calcExpr_ = numText({calcSpec_.label, "", 0, 0, 0, 0, 0, calcSpec_.decimals}, pv); setNum(calcSpec_, pv); });
      }
      button({lx + lw - kw, y, kw, kh}, "-X", [this] { calcKey("-X"); });
      y += kh + gap;
    }
    const float tw = (lw - 2 * gap) / 3;
    button({lx, y, tw, kh}, "RESET", [this] { setNum(calcSpec_, calcSpec_.reset); calcOpen_ = false; });
    button({lx + tw + gap, y, tw, kh}, "CANCEL", [this] { calcSpec_.set(calcBefore_); calcOpen_ = false; });
    button({lx + 2 * (tw + gap), y, tw, kh}, "OK", [this] {
      if (const auto val = calcValue()) setNum(calcSpec_, *val);
      calcOpen_ = false;
      buzz("tap");
    }, true, true);
  }

  // ── An LFO: a number that moves by itself ──
  //
  // The cue's own oscillators (Cue::geometryLfo), as the desk keeps them:
  // the same show file, the same arithmetic in the engine's drawing. ON, the
  // shape, how fast (free in seconds, or on the beat), how far. The centre is
  // the value you set, so turning one on never jumps the picture.

  void openLfo(int slot) {
    lfoSlot_ = slot;
    buzz("tap");
  }

  static const char* lfoSlotName(int slot) {
    switch (slot) {
      case kGeoLfoOffsetX: return "X";
      case kGeoLfoOffsetY: return "Y";
      case kGeoLfoScaleX: return "SCALE";
      case kGeoLfoScaleY: return "HEIGHT";
      case kGeoLfoRotation: return "ROTATION";
      default: return "LFO";
    }
  }

  void drawLfo(const SDL_FRect& lcd, Cue& c) {
    using deckboy::effects::LfoShape;
    const float u = unit_;
    const int i = sheet_;
    const int slot = lfoSlot_;
    auto& L = c.geometryLfo[static_cast<std::size_t>(slot)];
    // the engine plays its own copy of the live cue: hand it this one's LFOs
    if (i == active_) engine_->syncActiveCueSnapshot(c);
    const float lx = lcd.x + u, lw = lcd.w - 2 * u;
    float y = lcd.y + u;
    text_.draw(pixelSmall_, std::string("~ ") + lfoSlotName(slot) + " MOVES BY ITSELF", kDark, {lx, y, lw, 4 * u});
    y += 5 * u;
    // a small picture of it moving: the wave, and where it is now
    {
      const SDL_FRect wave {lx, y, lw, 8 * u};
      fillRect(renderer_, kDeep, wave);
      const double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
      const double rate = L.beatSync ? 2.0 / std::max(0.25f, L.beats) : std::max(0.01f, L.rateHz);
      SDL_SetRenderDrawColor(renderer_, L.on ? 120 : 110, L.on ? 210 : 110, L.on ? 90 : 110, 255);
      SDL_FPoint prev {};
      for (int k = 0; k <= 60; ++k) {
        const double t = now - 2.0 + 2.0 * k / 60.0;   // the last two seconds
        deckboy::effects::ParamLfo probe = L;
        probe.on = true;
        const double v = L.shape == LfoShape::Audio ? engine_->programAudioLevel01()
                                                    : deckboy::effects::lfoUnitValue(probe, t, t * 2.0, 0.0);
        const SDL_FPoint pt {wave.x + wave.w * k / 60.0f, wave.y + wave.h * (1.0f - static_cast<float>(v)) * 0.85f + wave.h * 0.075f};
        if (k) SDL_RenderLine(renderer_, prev.x, prev.y, pt.x, pt.y);
        prev = pt;
      }
      (void)rate;
      y += 9 * u;
    }
    const float bottom = lcd.y + lcd.h - u;
    const float rowH = std::min(9.0f * u, (bottom - y) / (slot == kGeoLfoRotation ? 8.0f : 7.0f));
    auto row = [&](float weight) { const SDL_FRect rr {lx, y, lw, rowH * weight}; y += rowH * weight; return rr; };
    chipRow(row(1), {{L.on ? "MOVING" : "STILL", [this, i, slot] {
                        auto& l = cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)];
                        l.on = !l.on;
                        if (l.on) buzz("spin");
                      }}}, {L.on});
    const std::vector<std::pair<const char*, LfoShape>> shapes = {
        {"SINE", LfoShape::Sine}, {"TRI", LfoShape::Triangle}, {"SAW", LfoShape::Saw}, {"RAMP", LfoShape::Ramp},
        {"SQUARE", LfoShape::Square}, {"STEPS", LfoShape::Sample}, {"SOUND", LfoShape::Audio}};
    for (int line = 0; line < 2; ++line) {
      std::vector<std::pair<std::string, std::function<void()>>> chips;
      std::vector<bool> on;
      for (std::size_t k = line ? 4 : 0; k < (line ? shapes.size() : 4); ++k) {
        const LfoShape sh = shapes[k].second;
        chips.push_back({shapes[k].first, [this, i, slot, sh] {
                           auto& l = cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)];
                           l.shape = sh;
                           l.on = true;
                         }});
        on.push_back(L.shape == sh);
      }
      chipRow(row(1), chips, on);
    }
    if (slot == kGeoLfoRotation) {
      // A whole turn every cycle, round and round: a ramp across the full
      // range, whose two ends are the same angle, so it never jumps.
      auto spin = [this, i, slot](bool right) {
        return [this, i, slot, right] {
          auto& l = cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)];
          l.on = true;
          l.shape = right ? deckboy::effects::LfoShape::Saw : deckboy::effects::LfoShape::Ramp;
          l.depth = 1.0f;
          buzz("spin");
        };
      };
      const bool spinning = L.on && L.depth >= 0.999f;
      chipRow(row(1), {{"SPIN RIGHT", spin(true)}, {"SPIN LEFT", spin(false)}},
              {spinning && L.shape == deckboy::effects::LfoShape::Saw, spinning && L.shape == deckboy::effects::LfoShape::Ramp});
    }
    chipRow(row(1), {{"FREE", [this, i, slot] { cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)].beatSync = false; }},
                     {"ON THE BEAT", [this, i, slot] { cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)].beatSync = true; }}},
            {!L.beatSync, L.beatSync});
    {
      NumSpec n;
      if (L.beatSync) {
        n.label = "EVERY";
        n.unit = "beats";
        n.decimals = 2;
        n.min = 0.25;
        n.max = 32;
        n.step = 0.25;
        n.reset = 4;
        n.presets = {{"1 BEAT", 1}, {"1 BAR", 4}, {"4 BARS", 16}};
        n.get = [this, i, slot] { return static_cast<double>(cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)].beats); };
        n.set = [this, i, slot](double v) { cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)].beats = static_cast<float>(v); };
      } else {
        const bool spinning = slot == kGeoLfoRotation && L.depth >= 0.999f &&
                              (L.shape == deckboy::effects::LfoShape::Saw || L.shape == deckboy::effects::LfoShape::Ramp);
        n.label = spinning ? "TURNS" : "RATE";
        n.unit = spinning ? "/ s" : "Hz";
        n.decimals = 2;
        n.min = 0.01;
        n.max = 8;
        n.step = 0.01;
        n.reset = 0.25;
        n.presets = {{"SLOW", 0.1}, {"BREATH", 0.25}, {"FAST", 2}};
        n.get = [this, i, slot] { return static_cast<double>(cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)].rateHz); };
        n.set = [this, i, slot](double v) { cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)].rateHz = static_cast<float>(v); };
      }
      numberRow(row(1.3f), n);
    }
    {
      NumSpec n;
      n.label = "HOW FAR";
      n.unit = "%";
      n.min = 0;
      n.max = 100;
      n.step = 1;
      n.reset = 50;
      n.presets = {{"A LITTLE", 10}, {"HALF", 50}, {"ALL", 100}};
      n.get = [this, i, slot] { return cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)].depth * 100.0; };
      n.set = [this, i, slot](double v) { cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)].depth = static_cast<float>(v / 100.0); };
      numberRow(row(1.3f), n);
    }
    // back to the page
    const float bw = lw / 2 - u / 2;
    button({lx, bottom - 6 * u, bw, 6 * u}, "STOP IT", [this, i, slot] {
      cues_[static_cast<std::size_t>(i)].geometryLfo[static_cast<std::size_t>(slot)].on = false;
      lfoSlot_ = -1;
    }, true, false, true);
    button({lx + bw + u, bottom - 6 * u, bw, 6 * u}, "DONE", [this] { lfoSlot_ = -1; }, true, true);
    (void)c;
  }

  // ── The filmstrip: the clip's own frames along the trim (Final Cut style) ──
  //
  // Ten stills across the media, decoded once in the background with libav
  // (engine/direction_inproc's decodeStills) and kept per cue and trim-free
  // (the strip is the whole file; IN and OUT are drawn over it).

  void drawFilmstrip(const Cue& c, const SDL_FRect& row) {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    trimBar_ = {row.x, row.y + 0.6f * u, row.w, row.h - 1.2f * u};
    fillRect(r, kDeep, trimBar_);
    requestStills(c);
    const auto it = stillsTex_.find(c.id);
    if (it != stillsTex_.end() && !it->second.empty()) {
      const float fw = trimBar_.w / static_cast<float>(it->second.size());
      for (std::size_t k = 0; k < it->second.size(); ++k) {
        SDL_Texture* t = it->second[k];
        if (!t) continue;
        float tw = 0, th = 0;
        SDL_GetTextureSize(t, &tw, &th);
        // each still fills its slot, centred, cropped to the slot's shape
        const float slotAspect = fw / trimBar_.h, aspect = tw / std::max(1.0f, th);
        SDL_FRect src {0, 0, tw, th};
        if (aspect > slotAspect) { src.w = th * slotAspect; src.x = (tw - src.w) / 2; }
        else { src.h = tw / slotAspect; src.y = (th - src.h) / 2; }
        const SDL_FRect dst {trimBar_.x + static_cast<float>(k) * fw + 0.15f * u, trimBar_.y, fw - 0.3f * u, trimBar_.h};
        SDL_RenderTexture(r, t, &src, &dst);
      }
    }
    // outside IN..OUT dimmed, the handles, the playhead
    const float xi = trimBar_.x + trimBar_.w * static_cast<float>(cueIn(c) / c.duration);
    const float xo = trimBar_.x + trimBar_.w * static_cast<float>(cueOut(c) / c.duration);
    fillRoundAlpha(r, kBlack, 0.6f, {trimBar_.x, trimBar_.y, xi - trimBar_.x, trimBar_.h}, 0);
    fillRoundAlpha(r, kBlack, 0.6f, {xo, trimBar_.y, trimBar_.x + trimBar_.w - xo, trimBar_.h}, 0);
    for (float hx : {xi, xo}) fillRect(r, kInOut, {hx - 0.8f * u, trimBar_.y - 0.6f * u, 1.6f * u, trimBar_.h + 1.2f * u});
    if (sheet_ == active_) {
      const float xp = trimBar_.x + trimBar_.w * static_cast<float>(std::clamp(engine_->position() / c.duration, 0.0, 1.0));
      fillRect(r, kLight, {xp - 0.25f * u, trimBar_.y, 0.5f * u, trimBar_.h});
    }
  }

  void requestStills(const Cue& c) {
    if (stillsTex_.count(c.id) || stillsBusy_.load()) return;
    if (stillsFor_ == c.id && stillsFailed_) return;
    if (stillsThread_.joinable()) stillsThread_.join();
    stillsFor_ = c.id;
    stillsFailed_ = false;
    stillsBusy_ = true;
    const std::string path = c.path;
    const double dur = c.duration;
    stillsThread_ = std::thread([this, path, dur] {
      std::vector<deckboy::engine::Still> got;
      std::vector<double> at;
      for (int k = 0; k < 10; ++k) at.push_back(dur * (k + 0.5) / 10.0);
      const bool ok = deckboy::engine::decodeStills(path, at, 192, got);
      std::lock_guard<std::mutex> hold(stillsLock_);
      stillsReady_ = std::move(got);
      stillsFailed_ = !ok;
      stillsBusy_ = false;
    });
  }

  // On the main thread: the decoded stills become textures.
  void serviceStills() {
    if (stillsBusy_.load() || stillsFor_.empty() || stillsTex_.count(stillsFor_)) return;
    std::vector<deckboy::engine::Still> got;
    {
      std::lock_guard<std::mutex> hold(stillsLock_);
      got.swap(stillsReady_);
    }
    if (got.empty()) return;
    std::vector<SDL_Texture*> texes;
    for (const auto& st : got) {
      SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, st.width, st.height);
      if (t) {
        SDL_UpdateTexture(t, nullptr, st.rgba.data(), st.width * 4);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
      }
      texes.push_back(t);
    }
    stillsTex_[stillsFor_] = std::move(texes);
  }

  void nudgeTrim(bool in, double by) {
    if (sheet_ < 0) return;
    Cue& c = cues_[static_cast<std::size_t>(sheet_)];
    const double f = cueFrame(c);
    if (in) c.inPointSeconds = std::clamp(cueIn(c) + by, 0.0, cueOut(c) - f);
    else c.outPointSeconds = std::clamp(cueOut(c) + by, cueIn(c) + f, c.duration);
    cueEdited(sheet_, true);
  }

  void bump(double Cue::*field, double by) {
    if (sheet_ < 0) return;
    Cue& c = cues_[static_cast<std::size_t>(sheet_)];
    c.*field = std::clamp(c.*field + by, 0.0, 30.0);
    cueEdited(sheet_, false);
  }

  void stepCueStyle(int by) {
    if (sheet_ < 0) return;
    Cue& c = cues_[static_cast<std::size_t>(sheet_)];
    c.cueTransitionStyle = deckboy::core::utils::transitionStyleToken(stepStyle(cueStyle(c), by));
  }

  void bumpTransition(double by) {
    if (sheet_ < 0) return;
    Cue& c = cues_[static_cast<std::size_t>(sheet_)];
    const double now = c.cueTransitionSeconds < 0 ? 0.5 : c.cueTransitionSeconds;
    c.cueTransitionSeconds = std::clamp(now + by, 0.0, 10.0);
  }

  void duplicateSheetCue() {
    if (sheet_ < 0) return;
    Cue copy = cues_[static_cast<std::size_t>(sheet_)];
    copy.id = "pocket-" + std::to_string(++nextId_);
    copy.name += " copy";
    cues_.insert(cues_.begin() + sheet_ + 1, copy);
    if (active_ > sheet_) ++active_;
    openSheet(sheet_ + 1);
  }

  void moveSheetCue(int by) {
    const int to = sheet_ + by;
    if (sheet_ < 0 || to < 0 || to >= static_cast<int>(cues_.size())) return;
    std::swap(cues_[static_cast<std::size_t>(sheet_)], cues_[static_cast<std::size_t>(to)]);
    if (active_ == sheet_) active_ = to;
    else if (active_ == to) active_ = sheet_;
    sheet_ = to;
    select(to);
  }

  // The sheet's DELETE: one tap, with UNDO after. Only the cue on air asks
  // first, because deleting it clears the picture.
  void deleteSheetCue() {
    if (sheet_ < 0) return;
    if (sheet_ == active_ && !confirmDelete_) {
      confirmDelete_ = true;
      return;
    }
    const int i = sheet_;
    sheet_ = -1;
    confirmDelete_ = false;
    deleteCueAt(i);
  }

  // Removes a cue, keeping it for UNDO for six seconds.
  void deleteCueAt(int i) {
    if (i < 0 || i >= static_cast<int>(cues_.size())) return;
    if (i == active_) {
      engine_->clear();
      active_ = -1;
    } else if (active_ > i) {
      --active_;
    }
    undoCue_ = cues_[static_cast<std::size_t>(i)];
    undoAt_ = i;
    undoUntil_ = SDL_GetTicks() + 6000;
    queueIds_.erase(std::remove(queueIds_.begin(), queueIds_.end(), undoCue_->id), queueIds_.end());
    cues_.erase(cues_.begin() + i);
    if (selected_ > i) --selected_;
    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(cues_.size()) - 1));
    buzz("back");
  }

  void undoDelete() {
    if (!undoCue_) return;
    const int at = std::clamp(undoAt_, 0, static_cast<int>(cues_.size()));
    cues_.insert(cues_.begin() + at, *undoCue_);
    if (active_ >= at) ++active_;
    undoCue_.reset();
    undoUntil_ = 0;
    select(at);
    buzz("go");
  }

  void tap(float x, float y) {
    const float u = unit_;
    auto within = [&](SDL_FPoint c, float rad) { return (x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= rad * rad; };
    if (menuOpen_ || sheet_ >= 0) {
      if (!aboutPage()) return;   // under a page the buttons are covered: a tap there is not a press
      Pressed p = Pressed::None;
      const float dx = x - padCentre_.x, dy = y - padCentre_.y;
      if (within(goCentre_, 10 * u)) p = Pressed::Go;
      else if (within(backCentre_, 8.5f * u)) p = Pressed::Back;
      else if (std::fabs(dx) < 15 * u && std::fabs(dy) < 15 * u)
        p = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? Pressed::Right : Pressed::Left) : (dy > 0 ? Pressed::Down : Pressed::Up);
      else if (inside(grow(menuPill_, 2 * u), x, y)) p = Pressed::Menu;
      else if (inside(grow(blackPill_, 2 * u), x, y)) p = Pressed::Black;
      if (p != Pressed::None) { press(p); codeKey(p); }
      return;
    }
    if (within(goCentre_, 10 * u)) { press(Pressed::Go); if (!codeKey(Pressed::Go)) go(); return; }
    if (within(backCentre_, 8.5f * u)) { press(Pressed::Back); if (!codeKey(Pressed::Back)) back(); return; }
    const float dx = x - padCentre_.x, dy = y - padCentre_.y;
    if (std::fabs(dx) < 15 * u && std::fabs(dy) < 15 * u) {
      const Pressed pad = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? Pressed::Right : Pressed::Left) : (dy > 0 ? Pressed::Down : Pressed::Up);
      if (codeKey(pad)) { press(pad); return; }
      if (std::fabs(dx) > std::fabs(dy)) {
        const bool paused = active_ >= 0 && engine_->state() == TransportState::Paused &&
                            cueHasTimeline(cues_[static_cast<std::size_t>(active_)]);
        if (paused) {
          // Paused: the pad's left/right step one frame (a clicker stays GO/BACK).
          const Cue& c = cues_[static_cast<std::size_t>(active_)];
          const double f = cueFrame(c);
          press(dx > 0 ? Pressed::Right : Pressed::Left);
          engine_->seek(std::clamp(engine_->position() + (dx > 0 ? f : -f), cueIn(c), cueOut(c)));
        } else if (dx > 0) { press(Pressed::Right); go(); } else { press(Pressed::Left); back(); }
      } else {
        if (dy > 0) { press(Pressed::Down); select(selected_ + 1); } else { press(Pressed::Up); select(selected_ - 1); }
      }
      return;
    }
    const SDL_FRect blackHit {blackPill_.x - 2 * u, blackPill_.y - 3 * u, blackPill_.w + 4 * u, blackPill_.h + 9 * u};
    const SDL_FRect menuHit {menuPill_.x - 2 * u, menuPill_.y - 3 * u, menuPill_.w + 4 * u, menuPill_.h + 9 * u};
    if (inside(blackHit, x, y)) {   // acts on release (a hold chooses what it does)
      pillDownAt_ = SDL_GetTicks();
      press(Pressed::Black);
      return;
    }
    if (inside(menuHit, x, y)) {
      press(Pressed::Menu);
      if (codeKey(Pressed::Menu)) return;
      menuOpen_ = true; menuTab_ = -1; sheet_ = -1;
      return;
    }
    if (inside(listLcd_, x, y) && rowH_ > 0) {
      select(scroll_ + static_cast<int>((y - listLcd_.y) / rowH_));
    }
  }

  void select(int i) {
    if (cues_.empty()) return;
    selected_ = std::clamp(i, 0, static_cast<int>(cues_.size()) - 1);
    listEnded_ = false;
  }

  // GO takes the selected cue and moves the selection on (advanceFrom: the
  // queue, the shuffle, the next cue, the loop), as the desk does.
  void go() {
    if (followLocked()) return;
    if (cues_.empty()) return;
    if (listEnded_ && selected_ == active_) {
      notice_ = "End of the list. LOOP LIST in MENU > LIST plays it again.";
      noticeUntil_ = SDL_GetTicks() + 2500;
      buzz("end");
      return;
    }
    take(selected_);
    advanceFrom(active_);
  }

  void back() {
    if (followLocked()) return;
    if (cues_.empty()) return;
    const int to = active_ > 0 ? active_ - 1 : 0;
    take(to);
    select(to + 1);
  }

  void take(int i) {
    if (i < 0 || i >= static_cast<int>(cues_.size())) return;
    active_ = i;
    black_ = false;
    endHandled_ = false;
    const Cue& c = cues_[static_cast<std::size_t>(i)];
    const double fade = c.cueTransitionSeconds >= 0 ? c.cueTransitionSeconds : listGlide();
    const TransitionStyle style = cueStyle(c);
    engine_->loadCue(&c, true, style == TransitionStyle::Cut ? 0.0 : fade, fade > 0 ? style : TransitionStyle::Cut);
  }

  // -- Playing the list: shuffle, loop, stop after this, play next --
  //
  // Every mode answers one question -- what is selected after a take -- so
  // the list always shows what GO (and a NEXT cue's end) takes, and the two
  // cannot disagree. The order: anything queued with PLAY NEXT, then the
  // shuffle (every cue once before any repeats), then simply the next cue;
  // at the end of the list LOOP starts it again, or the list stops there.

  double listGlide() const { return glideSeconds_ >= 0 ? glideSeconds_ : 0.5; }

  // The styles Pocket's own drawing has (engine: drawTransitionOverlay and
  // drawGeometricTransition). Portal, Shatter, Clouds and Ghastly are drawn by
  // the desk's output compositor; here they play as a crossfade for now.
  static constexpr TransitionStyle kPocketStyles[] = {
      TransitionStyle::Crossfade, TransitionStyle::Cut, TransitionStyle::DipBlack, TransitionStyle::DipWhite,
      TransitionStyle::PushLeft, TransitionStyle::PushRight, TransitionStyle::PushUp, TransitionStyle::PushDown,
      TransitionStyle::WipeLeft, TransitionStyle::WipeRight, TransitionStyle::WipeUp, TransitionStyle::WipeDown,
      TransitionStyle::Iris};

  static TransitionStyle drawableStyle(TransitionStyle s) {
    for (TransitionStyle k : kPocketStyles) if (k == s) return s;
    return TransitionStyle::Crossfade;
  }

  TransitionStyle cueStyle(const Cue& c) const {
    if (c.cueTransitionStyle.empty()) return listStyle_;
    return drawableStyle(deckboy::core::utils::parseTransitionStyleToken(c.cueTransitionStyle));
  }

  static TransitionStyle stepStyle(TransitionStyle now, int by) {
    constexpr int n = static_cast<int>(sizeof(kPocketStyles) / sizeof(kPocketStyles[0]));
    int i = 0;
    for (int k = 0; k < n; ++k) if (kPocketStyles[k] == now) i = k;
    return kPocketStyles[((i + by) % n + n) % n];
  }

  static std::string styleName(TransitionStyle s) { return deckboy::core::utils::transitionStyleLabel(s); }

  int indexOfCue(const std::string& id) const {
    for (std::size_t k = 0; k < cues_.size(); ++k) if (cues_[k].id == id) return static_cast<int>(k);
    return -1;
  }

  void refillShuffle(int except) {
    shuffleBag_.clear();
    for (std::size_t k = 0; k < cues_.size(); ++k) {
      if (static_cast<int>(k) != except) shuffleBag_.push_back(cues_[k].id);
    }
  }

  void advanceFrom(int took) {
    listEnded_ = false;
    if (took >= 0 && took < static_cast<int>(cues_.size())) {   // just played: out of this round's draw
      const std::string& id = cues_[static_cast<std::size_t>(took)].id;
      shuffleBag_.erase(std::remove(shuffleBag_.begin(), shuffleBag_.end(), id), shuffleBag_.end());
    }
    const int n = static_cast<int>(cues_.size());
    while (!queueIds_.empty()) {
      const int q = indexOfCue(queueIds_.front());
      queueIds_.erase(queueIds_.begin());
      if (q >= 0) { selected_ = q; return; }
    }
    if (shuffle_) {
      for (;;) {
        if (shuffleBag_.empty() && loopList_) refillShuffle(took);
        if (shuffleBag_.empty()) break;
        const std::size_t k = static_cast<std::size_t>(SDL_rand(static_cast<Sint32>(shuffleBag_.size())));
        const int pick = indexOfCue(shuffleBag_[k]);
        shuffleBag_.erase(shuffleBag_.begin() + static_cast<std::ptrdiff_t>(k));
        if (pick >= 0) { selected_ = pick; return; }
      }
    } else if (took + 1 < n) {
      selected_ = took + 1;
      return;
    } else if (loopList_ && n > 0) {
      selected_ = 0;
      return;
    }
    selected_ = std::clamp(took, 0, std::max(0, n - 1));
    listEnded_ = true;
  }

  void setShuffle(bool on) {
    shuffle_ = on;
    if (on) refillShuffle(-1); else shuffleBag_.clear();   // a new round: every cue in the draw
    notice_ = on ? "Shuffle: every cue once before any repeats" : "Shuffle off";
    noticeUntil_ = SDL_GetTicks() + 2000;
    if (on) buzz("spin");
  }

  void toggleQueued(int i) {
    if (i < 0 || i >= static_cast<int>(cues_.size())) return;
    const std::string& id = cues_[static_cast<std::size_t>(i)].id;
    auto at = std::find(queueIds_.begin(), queueIds_.end(), id);
    if (at != queueIds_.end()) queueIds_.erase(at);
    else queueIds_.push_back(id);
  }

  int queuePlace(const Cue& c) const {
    auto at = std::find(queueIds_.begin(), queueIds_.end(), c.id);
    return at == queueIds_.end() ? 0 : static_cast<int>(at - queueIds_.begin()) + 1;
  }

  void setMuted(bool on) {
    muted_ = on;
    engine_->setVolume(on ? 0.0f : static_cast<float>(volume_) / 100.0f);
  }

  // Seconds forwards or back in the cue on air, inside its IN and OUT.
  void skipBy(double seconds) {
    if (active_ < 0) return;
    const Cue& c = cues_[static_cast<std::size_t>(active_)];
    if (!cueHasTimeline(c)) return;
    engine_->seek(std::clamp(engine_->position() + seconds, cueIn(c), cueOut(c) - cueFrame(c)));
  }

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_AudioStream* audio_ = nullptr;
  std::unique_ptr<MediaEngine> engine_;
  Text text_;
  TTF_Font* pixel_ = nullptr;
  TTF_Font* pixelSmall_ = nullptr;
  TTF_Font* sans_ = nullptr;
  TTF_Font* mono_ = nullptr;
  TTF_Font* printFont_ = nullptr;

  std::vector<Cue> cues_;
  std::string mediaDir_;
  int nextId_ = 0;
  std::mutex pickLock_;
  std::vector<std::string> picks_;
  bool pickerOpen_ = false;
  bool pickingShow_ = false;   // the open picker is choosing a show, not media
  // the open show (see currentShow)
  Project show_;
  int showDeck_ = 0;
  std::string showFile_, showsDir_, lastAutosave_, confirmOpen_;
  bool confirmNew_ = false;
  Uint64 autosaveAt_ = 0;
  std::string notice_;
  Uint64 noticeUntil_ = 0;
  int programW_ = 1920, programH_ = 1080;
  float programHz_ = 60.0f;
  SDL_Texture* programTex_ = nullptr;
  struct Hit { SDL_FRect rect; std::function<void()> fn; };
  std::vector<Hit> hits_;
  Drag drag_ = Drag::None;
  float downX_ = 0, downY_ = 0;
  int holdRow_ = -1;
  Uint64 holdStart_ = 0;
  int sheet_ = -1;
  bool menuOpen_ = false;
  int menuTab_ = -1;   // -1 the tiles; 0 SOURCES, 1 OUTPUT, 2 LIST, 3 SYNC, 4 PHONE, 5 SHOW
  bool phoneFull_ = false;
  bool testPattern_ = false;
  Uint64 identifyUntil_ = 0;
  TTF_Font* identifyFont_ = nullptr;
  int identifyFontH_ = 0;
  int battery_ = -1, thermal_ = 0;
  std::string showRunningTitle_;
  static constexpr int kRemotePort = 5510;   // the desk's
  SocketHandle listen_ = kInvalidSocket;
  std::vector<RemoteClient> clients_;
  int volume_ = 80;
  // SYNC follower
  Follow follow_ = Follow::Off, loggedFollow_ = Follow::Off;
  bool followWanted_ = false;
  std::string followHost_, followError_, followBuffer_;
  sockaddr_in followAddr_ {};
  SocketHandle followSock_ = kInvalidSocket;
  Uint64 followSince_ = 0, followRetryAt_ = 0, lastHeard_ = 0, nextTimeProbe_ = 0, seekHoldUntil_ = 0;
  std::vector<ClockSample> clockSamples_;
  Sint64 clockOffset_ = 0;
  Uint64 clockRtt_ = 0;
  bool clockReady_ = false;
  SyncLine lastSync_;
  std::string syncCue_, missingCue_;
  double driftFrames_ = 0.0;
  double seekLead_ = 0.0;   // seconds a seek takes to land; measured, then set
  int syncSeeks_ = 0;
  bool seekLanding_ = false;
  bool hostEditing_ = false;
  std::string snapPath_;
  bool haptics_ = true;
  Defaults defaults_;
  Pill pill_ = Pill::Black;
  int codeIndex_ = 0;
  Uint64 codeLastMs_ = 0;
  bool terrariumUnlocked_ = false, tabletopUnlocked_ = false;
  std::vector<Pressed> code2Seen_;
  Uint64 code2LastMs_ = 0;
  // ghost controls over full screen
  Uint64 ghostUntil_ = 0;
  std::vector<Hit> ghostHits_;
  SDL_Texture* ghostTex_ = nullptr;
  int ghostTexW_ = 0, ghostTexH_ = 0;
  float ghostW_ = 0, ghostH_ = 0;
  bool ghostTurned_ = false;
  bool ghostWasShown_ = false;
  Uint64 ghostShownAt_ = 0, ghostPressedAt_ = 0;
  int ghostPressed_ = -1;
  Uint64 pillDownAt_ = 0;
  // the cue sheet's pages and numbers
  Page page_ = Page::Play;
  std::vector<Slider> sliders_;
  int activeSlider_ = -1;
  float sliderStartX_ = 0, sliderStartY_ = 0;
  double sliderStartValue_ = 0;
  bool linkScale_ = true;
  bool calcOpen_ = false;
  int lfoSlot_ = -1;   // the cue sheet's LFO page, for this Cue::geometryLfo slot
  NumSpec calcSpec_;
  std::string calcExpr_;
  double calcBefore_ = 0;
  // the meter
  float meterLevel_[2] {0, 0}, meterPeak_[2] {0, 0};
  Uint64 meterPeakAt_[2] {0, 0};
  // the filmstrip's stills
  std::thread stillsThread_;
  std::atomic<bool> stillsBusy_ {false};
  std::mutex stillsLock_;
  std::vector<deckboy::engine::Still> stillsReady_;
  std::map<std::string, std::vector<SDL_Texture*>> stillsTex_;
  std::string stillsFor_;
  bool stillsFailed_ = false;
  // the picture as a control (PLACE), and holding it during a show
  SDL_FRect picRect_ {};
  SDL_FRect sheetRect_ {};
  bool landscape_ = false, editing_ = false;
  std::map<SDL_FingerID, SDL_FPoint> fingers_;
  bool gesture_ = false;
  std::size_t gestureFingers_ = 0;
  SDL_FPoint gestureC0_ {};
  float gestureD0_ = 0, gestureA0_ = 0;
  float gestureScale0_ = 1, gestureScaleY0_ = 1, gestureOffX0_ = 0, gestureOffY0_ = 0, gestureRot0_ = 0;
  Uint64 lastPicTap_ = 0;
  Uint64 picHoldSince_ = 0;
  int renameIndex_ = -1;
  std::string renameText_;
  // playing the list
  bool shuffle_ = false, loopList_ = false, endAfter_ = false, listEnded_ = false, endHandled_ = false, muted_ = false;
  std::vector<std::string> queueIds_, shuffleBag_;
  double glideSeconds_ = -1.0;   // between cues that set none; -1 = 0.5 s
  TransitionStyle listStyle_ = TransitionStyle::Crossfade;
  // direction copies (one worker)
  std::thread dirThread_;
  std::atomic<bool> dirBusy_ {false}, dirCancel_ {false}, dirFinished_ {false};
  std::atomic<double> dirProgress_ {0.0};
  DirectionJob dirJob_;
  deckboy::engine::RemuxResult dirResult_ = deckboy::engine::RemuxResult::Failed;
  deckboy::engine::DirectionCopy dirMade_;
  std::string dirWhy_, dirFailed_, dirFailedKey_, dirWaitKey_;
  Uint64 dirWaitSince_ = 0;
  // rendered shells
  std::vector<ShellInfo> shells_;
  std::string shell_;
  SDL_Texture* shellTex_ = nullptr;
  int shellW_ = 0, shellH_ = 0;
  Ink shellInk_ {46, 46, 70};
  std::vector<std::string> themes_;
  double huePhase_ = -1.0;
  int hueStep_ = -1;
  std::string theme_ = "pocket";
  bool charging_ = false;
  Uint64 nextHealth_ = 0;
  bool confirmDelete_ = false;
  // swipe a row left to delete it; UNDO for a while after
  static constexpr float kSwipeDelete = 24.0f;   // units: a third of the row
  int swipeRow_ = -1;
  bool swiping_ = false;
  float swipeDx_ = 0;
  std::optional<Cue> undoCue_;
  int undoAt_ = 0;
  Uint64 undoUntil_ = 0;
  SDL_FRect scrub_ {}, trimBar_ {}, volumeBar_ {};
  SDL_Texture* outputTex_ = nullptr;
  int outputTexW_ = 0, outputTexH_ = 0, outputGeneration_ = -1;
  std::string outputLabel_;
  int selected_ = 0;
  int active_ = -1;
  int scroll_ = 0;
  bool black_ = false;
  Pressed pressed_ = Pressed::None;
  Uint64 pressedUntil_ = 0;

  float pxW_ = 0, pxH_ = 0, unit_ = 1, rowH_ = 0;
  SDL_FRect safe_ {}, header_ {}, program_ {}, list_ {}, listLcd_ {}, strip_ {}, blackPill_ {}, menuPill_ {};
  SDL_FPoint padCentre_ {}, goCentre_ {}, backCentre_ {};
};

}  // namespace

SDL_AppResult SDL_AppInit(void** state, int, char**) {
  SDL_SetAppMetadata("Deckboy Pocket", "0.1", "io.github.utopian_academy.deckboy.pocket");
  auto* app = new Pocket;
  *state = app;
  return app->init() ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* e) {
  return static_cast<Pocket*>(state)->event(*e) ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppIterate(void* state) {
  static_cast<Pocket*>(state)->frame();
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* state, SDL_AppResult) {
  auto* app = static_cast<Pocket*>(state);
  if (app) {
    app->shutdown();
    delete app;
  }
}
