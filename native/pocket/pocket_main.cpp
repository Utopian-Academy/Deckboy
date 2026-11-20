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
#include "core/types.hpp"
#include "core/utils.hpp"
#include "engine/media_engine.hpp"
#if defined(DECKBOY_INPROC_DECODE)
#include "engine/libav_decoder.hpp"   // the rendered shells' JPEGs
#endif
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
    case CueKind::Pattern: return "MADE";
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
    // Then what Deckboy makes itself. Real shows (.deckboy files and bundles)
    // come next.
    const std::pair<const char*, const char*> kFirstShow[] = {
        {"smpte-bars", "SMPTE colour bars"}, {"pocket-test", "Pocket Test"}, {"illusion", ""},
        {"swirl", ""}, {"lumia", ""}, {"fireside", ""}, {"crosshatch", "Crosshatch"}};
    for (const auto& [type, label] : kFirstShow) {
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
      cues_.push_back(cue);
    }
    loadThemes();
    loadFonts();
    loadSyncSettings();
    loadShells();
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
      if (cue.kind == CueKind::Image) cue.stillDurationSeconds = 0.0;  // a still holds until the next GO
      cue.pauseOnLastFrame = true;
      cues_.push_back(std::move(cue));
    }
    SDL_Log("pocket: %zu media cues from %s", cues_.size(), mediaDir_.c_str());
  }

  void shutdown() {
    dropFollowSocket();
    if (shellTex_) SDL_DestroyTexture(shellTex_);
    for (RemoteClient& c : clients_) closeSocket(c.socket);
    if (listen_ != kInvalidSocket) closeSocket(listen_);
    engine_.reset();
    text_.clear();
    for (TTF_Font* f : {pixel_, pixelSmall_, sans_, mono_}) if (f) TTF_CloseFont(f);
    if (audio_) SDL_DestroyAudioStream(audio_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    TTF_Quit();
    SDL_Quit();
  }

  bool event(const SDL_Event& e) {
    switch (e.type) {
      case SDL_EVENT_QUIT:
        return false;
      case SDL_EVENT_TEXT_INPUT:
        if (hostEditing_) {
          for (const char* c = e.text.text; *c; ++c) {
            if ((std::isalnum(static_cast<unsigned char>(*c)) || *c == '.' || *c == ':' || *c == '-') && followHost_.size() < 64) {
              followHost_ += *c;
            }
          }
        }
        break;
      case SDL_EVENT_KEY_DOWN:
        if (hostEditing_) {
          if (e.key.key == SDLK_BACKSPACE && !followHost_.empty()) followHost_.pop_back();
          else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) { endHostEdit(); startFollowing(); }
          else if (e.key.key == SDLK_AC_BACK || e.key.key == SDLK_ESCAPE) endHostEdit();
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
      case SDL_EVENT_FINGER_DOWN:
        onDown(e.tfinger.x * pxW_, e.tfinger.y * pxH_);
        break;
      case SDL_EVENT_FINGER_MOTION:
        onMove(e.tfinger.x * pxW_, e.tfinger.y * pxH_);
        break;
      case SDL_EVENT_FINGER_UP:
      case SDL_EVENT_FINGER_CANCELED:
        drag_ = Drag::None;
        holdRow_ = -1;
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
    pollRemote();
    pollFollow();
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
    // Picture-in-picture and full screen: the program alone.
    if (gInPip.load() || phoneFull_) {
      drawProgramAlone(phoneFull_ && !gInPip.load());
      SDL_RenderPresent(r);
      return;
    }
    if (shellTex_) drawShell();
    else fillRect(r, kShell, {0, 0, pxW_, pxH_});
    drawProgram();
    if (hostEditing_) drawHostEditor();
    if (sheet_ >= 0) {
      drawSheet();
    } else if (menuOpen_) {
      drawMenu();
    } else {
      drawCueList();
      drawControls();
    }
    if (!snapPath_.empty()) {
      if (SDL_Surface* shot = SDL_RenderReadPixels(r, nullptr)) {
        if (!SDL_SaveBMP(shot, snapPath_.c_str())) SDL_Log("pocket: SNAP failed: %s", SDL_GetError());
        SDL_DestroySurface(shot);
      }
      snapPath_.clear();
    }
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
    unit_ = safe_.w / 72.0f;
    const float u = unit_;
    const std::string data = std::string(SDL_getenv("DECKBOY_ROOT") ? SDL_getenv("DECKBOY_ROOT") : ".") + "/data/";
    text_.clear();
    for (TTF_Font* f : {pixel_, pixelSmall_, sans_, mono_}) if (f) TTF_CloseFont(f);
    pixel_ = TTF_OpenFont((data + "PressStart2P.ttf").c_str(), 2.6f * u);
    pixelSmall_ = TTF_OpenFont((data + "PressStart2P.ttf").c_str(), 1.6f * u);
    sans_ = TTF_OpenFont((data + "fonts/LiberationSans-Regular.ttf").c_str(), 3.0f * u);
    mono_ = TTF_OpenFont((data + "fonts/LiberationMono-Regular.ttf").c_str(), 2.9f * u);
    if (!pixel_ || !mono_) SDL_Log("pocket: fonts missing under %s (%s)", data.c_str(), SDL_GetError());
  }

  void layout() {
    const float u = unit_, x0 = safe_.x, y0 = safe_.y;
    header_ = {x0 + 3 * u, y0 + 1.5f * u, 66 * u, 5 * u};
    // Program panel: a title line, the 16:9 picture, a name and clock line.
    const float picH = (68 - 5 - 2) * u * 9.0f / 16.0f;
    // ...then the scrub bar, the time line and PAUSE / SET IN / SET OUT.
    program_ = {x0 + 2 * u, y0 + 7 * u, 68 * u, 5 * u + u + picH + 1.2f * u + 4 * u + 4.5f * u + 4 * u + 6 * u + 2.5f * u};
    const float stripH = 50 * u;
    const float bottom = safe_.y + safe_.h;
    strip_ = {x0, bottom - stripH, safe_.w, stripH};
    list_ = {x0 + 2 * u, program_.y + program_.h + 2 * u, 68 * u, 0};
    list_.h = std::max(20 * u, strip_.y - list_.y - 1.5f * u);
    const float cy = strip_.y + 19 * u;
    padCentre_ = {x0 + 17 * u, cy};
    goCentre_ = {x0 + 57 * u, cy - 5 * u};
    backCentre_ = {x0 + 41 * u, cy + 8 * u};
    blackPill_ = {x0 + 18 * u, strip_.y + 39 * u, 12 * u, 4 * u};
    menuPill_ = {x0 + 36 * u, strip_.y + 39 * u, 12 * u, 4 * u};
  }

  void drawProgram() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    const std::string health = healthLabel();
    int hw = 0, hh = 0;
    if (pixelSmall_ && !health.empty()) TTF_GetStringSize(pixelSmall_, health.c_str(), 0, &hw, &hh);
    const SDL_FRect left {header_.x, header_.y, header_.w - static_cast<float>(hw) - 2 * u, header_.h};
    const Ink print = shellTex_ ? shellInk_ : kPrint;   // a rendered shell has its own print colour
    if (SDL_GetTicks() < noticeUntil_) {
      text_.draw(mono_, notice_, print, left);
    } else if (follow_ != Follow::Off) {
      text_.draw(pixel_, followHeadline(), follow_ == Follow::Following ? print : Ink {170, 40, 30}, left);
    } else {
      text_.draw(pixel_, "DECKBOY POCKET", print, left);
    }
    if (!health.empty()) {
      text_.draw(pixelSmall_, health, thermal_ >= 3 ? Ink {150, 30, 30} : print,
                 {header_.x + header_.w - static_cast<float>(hw), header_.y, static_cast<float>(hw) + 1, header_.h});
    }
    if (!shellTex_) fillRound(r, kBezel, program_, 3 * u);
    // the LED: lit while something is on air and not blacked out
    const bool live = active_ >= 0 && !black_;
    fillCircle(r, live ? Ink {255, 50, 30} : Ink {70, 20, 20}, {program_.x + 3 * u, program_.y + 2.8f * u}, 0.9f * u);
    text_.draw(pixelSmall_, live ? "LIVE" : (black_ ? "BLACK" : "READY"), Ink {170, 170, 180},
               {program_.x + 5 * u, program_.y + 1.2f * u, 20 * u, 3.2f * u});
    text_.draw(pixelSmall_, outputLabel_.empty() ? "NO OUTPUT" : outputLabel_, Ink {170, 170, 180},
               {program_.x + 24 * u, program_.y + 1.2f * u, program_.w - 27 * u, 3.2f * u});
    const SDL_FRect lcd {program_.x + 2.5f * u, program_.y + 5 * u, program_.w - 5 * u, program_.h - 7.5f * u};
    fillRect(r, kLight, lcd);
    // The picture: 16:9, the engine's own render.
    const float pw = lcd.w - 2 * u;
    const float ph = pw * 9.0f / 16.0f;
    const SDL_FRect pic {lcd.x + u, lcd.y + u, pw, ph};
    fillRect(r, kBlack, pic);
    if (programTex_) {
      const SDL_FRect fitted = fitProgram(pic);
      SDL_RenderTexture(r, programTex_, nullptr, &fitted);
    }
    // The name, then the scrub bar (the media's whole length, IN and OUT
    // marked, drag to seek), the time from IN and to OUT, and the buttons.
    float y = pic.y + pic.h + 1.2f * u;
    const Cue* onAir = active_ >= 0 ? &cues_[static_cast<std::size_t>(active_)] : nullptr;
    text_.draw(mono_, onAir ? onAir->name : "Nothing on air", kDeep, {lcd.x + u, y, lcd.w - 2 * u, 4 * u});
    y += 4.5f * u;
    scrub_ = {lcd.x + u, y, lcd.w - 2 * u, 3.4f * u};
    const double pos = onAir ? engine_->position() : 0.0;
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
    y += 4.5f * u;
    if (onAir && cueHasTimeline(*onAir)) {
      text_.draw(mono_, timecode(std::max(0.0, pos - cueIn(*onAir)), onAir->fps), kDeep, {lcd.x + u, y, lcd.w * 0.5f, 4 * u});
      const std::string remain = "-" + timecode(std::max(0.0, cueOut(*onAir) - pos), onAir->fps);
      int tw = 0, th = 0;
      if (mono_) TTF_GetStringSize(mono_, remain.c_str(), 0, &tw, &th);
      text_.draw(mono_, remain, kDeep, {lcd.x + lcd.w - u - static_cast<float>(tw), y, static_cast<float>(tw) + 1, 4 * u});
    } else {
      text_.draw(mono_, onAir ? "holds until GO" : "", kDark, {lcd.x + u, y, lcd.w - 2 * u, 4 * u});
    }
    y += 4.5f * u;
    const float bw = (lcd.w - 2 * u - 2 * u) / 3;
    const bool paused = onAir && engine_->state() == TransportState::Paused;
    const bool canTrim = onAir && cueHasTimeline(*onAir);
    button({lcd.x + u, y, bw, 5 * u}, paused ? "PLAY" : "PAUSE", [this, paused] { paused ? engine_->play() : engine_->pause(); }, onAir != nullptr);
    button({lcd.x + 2 * u + bw, y, bw, 5 * u}, "SET IN", [this] { setInOut(true); }, canTrim);
    button({lcd.x + 3 * u + 2 * bw, y, bw, 5 * u}, "SET OUT", [this] { setInOut(false); }, canTrim);
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
    if (turned && H > W) {
      // Rotated 90 degrees: the picture's width runs down the phone. Fitted
      // in the turned box (H x W), then rotated about its centre.
      const SDL_FRect turnedBox {(W - H) / 2, (H - W) / 2, H, W};
      const SDL_FRect dst = fitProgram(turnedBox);
      SDL_RenderTextureRotated(r, programTex_, nullptr, &dst, 90.0, nullptr, SDL_FLIP_NONE);
    } else {
      const SDL_FRect dst = fitProgram({0, 0, W, H});
      SDL_RenderTexture(r, programTex_, nullptr, &dst);
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
    const std::string title = (active_ >= 0 && !black_) ? cues_[static_cast<std::size_t>(active_)].name : std::string();
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
    const float x = safe_.x + 2 * u, w = 68 * u;
    float y = program_.y + program_.h + 2 * u;
    const SDL_FRect panel {x, y, w, safe_.y + safe_.h - u - y};
    fillRound(r, kBezel, panel, 3 * u);
    const SDL_FRect lcd {x + 2.5f * u, y + 2.5f * u, w - 5 * u, panel.h - 5 * u};
    fillRect(r, kLight, lcd);
    const float lx = lcd.x + u, lw = lcd.w - 2 * u;
    y = lcd.y + u;
    {
      const float cw = 14 * u;
      const float tw = (lw - cw - 5 * u) / 4;
      const char* kTabs[4] = {"ADD", "OUTPUT", "SYNC", "SKIN"};
      for (int t = 0; t < 4; ++t) {
        button({lx + static_cast<float>(t) * (tw + u), y, tw, 5 * u}, kTabs[t], [this, t] { menuTab_ = t; }, true, menuTab_ == t);
      }
      button({lx + lw - cw, y, cw, 5 * u}, "CLOSE", [this] { menuOpen_ = false; endHostEdit(); });
    }
    y += 7.5f * u;

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
          {"swirl", "SWIRL"}, {"lumia", "LUMIA"}, {"fireside", "FIRESIDE"}};
      const float bw = (lw - u) / 2;
      for (int i = 0; i < 6; ++i) {
        const char* type = kMade[i].first;
        button({lx + static_cast<float>(i % 2) * (bw + u), y, bw, 4.5f * u}, kMade[i].second, [this, type] { addGenerator(type); });
        if (i % 2 == 1) y += 5.5f * u;
      }
      y += 1.5f * u;
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
      item("PHONE FULL SCREEN", [this] { menuOpen_ = false; phoneFull_ = true; });
    }

    }
    if (menuTab_ == 2) {
      drawSyncTab(lx, lw, y);
    }
    if (menuTab_ == 3) {
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

    heading("PHONE");
    text_.draw(mono_, healthLabel().empty() ? "-" : "Battery " + healthLabel(), kDeep, {lx, y, lw, 4.5f * u});
    }
  }

  void addGenerator(const char* type) {
    Cue cue;
    cue.kind = CueKind::Pattern;
    cue.path = std::string("pattern://") + type;
    const std::string generated = generatorPatternName(type);
    cue.name = generated.empty() ? (std::string(type) == "smpte-bars" ? "SMPTE colour bars" : "Pocket Test") : generated;
    cue.width = programW_;
    cue.height = programH_;
    cue.formatName = "generated";
    cue.pauseOnLastFrame = true;
    cue.stillDurationSeconds = 0.0;
    cue.hasAudio = cue.audioEnabled = (std::string(type) == "pocket-test");
    cue.id = "pocket-" + std::to_string(++nextId_);
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

  // A cue set to NEXT takes the one after it when it ends.
  void followEnd() {
    if (active_ < 0) return;
    const Cue& c = cues_[static_cast<std::size_t>(active_)];
    if (endModeOf(c) != EndMode::Next || !engine_->reachedEnd()) return;
    if (active_ + 1 < static_cast<int>(cues_.size())) {
      take(active_ + 1);
      select(active_ + 1);
    }
  }

  void drawCueList() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    if (!shellTex_) fillRound(r, kBezel, list_, 3 * u);
    const SDL_FRect lcd {list_.x + 2.5f * u, list_.y + 2.5f * u, list_.w - 5 * u, list_.h - 5 * u};
    fillRect(r, kLight, lcd);
    rowH_ = 5.2f * u;
    const int visible = std::max(1, static_cast<int>(lcd.h / rowH_));
    // keep the selection in view
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visible) scroll_ = selected_ - visible + 1;
    listLcd_ = lcd;
    SDL_SetRenderClipRect(r, nullptr);
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
      text_.draw(mono_, c.name, ink, {row.x + 7 * u, row.y, row.w - 26 * u, row.h});
      text_.draw(pixelSmall_, kindTag(c), soft, {row.x + row.w - 18.5f * u, row.y, 9 * u, row.h}, true);
      const EndMode m = endModeOf(c);
      std::string right = m == EndMode::Loop ? "LOOP" : m == EndMode::Next ? "NEXT" : "HOLD";
      if (cueHasTimeline(c) && m != EndMode::Loop) {
        const int secs = static_cast<int>(std::lround(cueOut(c) - cueIn(c)));
        char len[16];
        std::snprintf(len, sizeof len, "%d:%02d", secs / 60, secs % 60);
        right = m == EndMode::Next ? std::string(len) + ">" : len;
      }
      text_.draw(mono_, right, ink, {row.x + row.w - 9 * u, row.y, 8.5f * u, row.h});
    }
  }

  void drawControls() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    if (shellTex_) {
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
    fillRound(r, black_ || pressed_ == Pressed::Black ? kDark : kPill, blackPill_, 2 * u);
    fillRound(r, pressed_ == Pressed::Menu ? kDark : kPill, menuPill_, 2 * u);
    text_.draw(pixelSmall_, "BLACK", kPrint, {blackPill_.x - 2 * u, blackPill_.y + 5 * u, blackPill_.w + 4 * u, 3 * u}, true);
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
             "SEEK <seconds> BLACK [ON|OFF] VOLUME <0-100> DECK 1 SYNC FOLLOW <desk> SYNC STOP SYNC STATUS\n";
    }
    if (verb == "STATUS" || verb == "STATE") return remoteStatus();
    if (verb == "DECK") return rest.empty() || rest == "1" ? ok() : err("Pocket has one deck");
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
      if (end == rest.c_str() || t < 0) return err("seconds, please");
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
      engine_->setVolume(static_cast<float>(volume_) / 100.0f);
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
      << " dur=" << (live && live->duration > 0 ? clock(live->duration) : std::string("--:--")) << "\n";
    o << "OUTPUT 1 name=\"" << (outputLabel_.empty() ? "phone" : outputLabel_) << "\" enabled=" << (black_ ? "off" : "on")
      << " raster=" << programW_ << "x" << programH_ << "\n";
    o << "SYNC " << syncStatusFields() << "\n";
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
                       fade > 0 ? TransitionStyle::Crossfade : TransitionStyle::Cut);
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
    line("Settings > Network: allow network control.", kDeep);
    line("Then SYNC LEAD ON. Media must be on both.", kDeep);
  }

  // While typing, the address sits over the picture: the keyboard covers the menu.
  void drawHostEditor() {
    const float u = unit_;
    const SDL_FRect box {program_.x, program_.y, program_.w, 16 * u};
    fillRound(renderer_, kBezel, box, 3 * u);
    text_.draw(pixelSmall_, "DESK ADDRESS", Ink {170, 170, 180}, {box.x + 3 * u, box.y + 1.5f * u, box.w - 6 * u, 3.2f * u});
    const SDL_FRect field {box.x + 2.5f * u, box.y + 5.5f * u, box.w - 5 * u, 6 * u};
    fillRect(renderer_, kLight, field);
    const bool caret = (SDL_GetTicks() / 500) % 2 == 0;
    text_.draw(mono_, followHost_ + (caret ? "_" : " "), kDeep, {field.x + u, field.y, field.w - 2 * u, field.h});
    text_.draw(pixelSmall_, "ENTER TO FOLLOW", Ink {170, 170, 180}, {box.x + 3 * u, box.y + 12.3f * u, box.w - 6 * u, 3.2f * u});
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
      SDL_PathInfo info;
      if (!SDL_GetPathInfo(file.c_str(), &info) || info.size == 0) {
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

  std::string shellName() const {
    for (const ShellInfo& s : shells_) if (s.slug == shell_) return s.name;
    return "Flat";
  }

  void drawShell() {
    SDL_Renderer* r = renderer_;
    fillRect(r, kBlack, {0, 0, pxW_, pxH_});   // the phone's own black round the shell
    const float u = unit_;
    const float k = static_cast<float>(shellW_) / 72.0f;   // shell pixels per mm
    const float tall = safe_.h / u;                         // the layout's height in mm
    auto part = [&](float fromMm, float toMm, float atMm, float heightMm) {
      const SDL_FRect src {0, fromMm * k, static_cast<float>(shellW_), (toMm - fromMm) * k};
      const SDL_FRect dst {safe_.x, safe_.y + atMm * u, safe_.w, heightMm * u};
      SDL_RenderTexture(r, shellTex_, &src, &dst);
    };
    const float plateMm = static_cast<float>(shellH_) / k;   // 156
    if (std::fabs(tall - plateMm) < 0.5f || tall < plateMm - 24.0f) {
      part(0, plateMm, 0, plateMm);   // the same height (or too short to cut): as it is
      return;
    }
    // the top to just inside the cue list, the bottom from just inside it, the band between stretched
    part(0, 76, 0, 76);
    part(plateMm - 56, plateMm, tall - 56, 56);
    part(76, plateMm - 56, 76, tall - 56 - 76);
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
    if (black_) fillRoundAlpha(r, Ink {220, 30, 30}, 0.6f, blackPill_, 2 * u);
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
    for (const std::string& uri : picks) addPicked(uri);
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
  }

  enum class Drag { None, Scrub, TrimIn, TrimOut, Volume };

  void onDown(float x, float y) {
    downX_ = x;
    downY_ = y;
    if (phoneFull_) {   // full screen: a tap brings the controls back
      phoneFull_ = false;
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
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
      if (inside(it->rect, x, y)) {
        auto fn = it->fn;   // the handler may rebuild hits_
        fn();
        return;
      }
    }
    if (sheet_ >= 0) return;
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
      }
    }
    tap(x, y);
  }

  void onMove(float x, float y) {
    if (holdRow_ >= 0 && (std::fabs(x - downX_) > 2 * unit_ || std::fabs(y - downY_) > 2 * unit_)) holdRow_ = -1;
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

  static SDL_FRect grow(const SDL_FRect& r, float by) { return {r.x - by, r.y - by, r.w + 2 * by, r.h + 2 * by}; }

  // ── The cue sheet: hold a cue ──
  void openSheet(int i) {
    if (i < 0 || i >= static_cast<int>(cues_.size())) return;
    sheet_ = i;
    select(i);
    confirmDelete_ = false;
  }

  void drawSheet() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    Cue& c = cues_[static_cast<std::size_t>(sheet_)];
    const float x = safe_.x + 2 * u, w = 68 * u;
    float y = program_.y + program_.h + 2 * u;
    const float bottom = safe_.y + safe_.h - u;
    const SDL_FRect panel {x, y, w, bottom - y};
    fillRound(r, kBezel, panel, 3 * u);
    const SDL_FRect lcd {x + 2.5f * u, y + 2.5f * u, w - 5 * u, panel.h - 5 * u};
    fillRect(r, kLight, lcd);
    const float lx = lcd.x + u, lw = lcd.w - 2 * u;
    y = lcd.y + u;
    // Title and DONE
    text_.draw(pixelSmall_, "CUE " + std::to_string(sheet_ + 1), kDark, {lx, y, 20 * u, 4.5f * u});
    button({lx + lw - 14 * u, y, 14 * u, 4.5f * u}, "DONE", [this] { sheet_ = -1; });
    y += 5.5f * u;
    text_.draw(mono_, c.name, kDeep, {lx, y, lw, 4 * u});
    y += 4.2f * u;
    char info[160];
    if (cueHasTimeline(c)) {
      std::snprintf(info, sizeof info, "%s%s%dx%d  %.4g fps  %s", c.videoCodec.c_str(), c.videoCodec.empty() ? "" : "  ",
                    c.width, c.height, c.fps, timecode(c.duration, c.fps).c_str());
    } else {
      std::snprintf(info, sizeof info, "%s  holds until GO", kindTag(c));
    }
    text_.draw(pixelSmall_, info, kDark, {lx, y, lw, 3.5f * u});
    y += 5 * u;

    if (cueHasTimeline(c)) {
      // TRIM: the whole media; drag the IN and OUT handles.
      text_.draw(pixelSmall_, "TRIM", kDark, {lx, y, 20 * u, 3.5f * u});
      y += 4 * u;
      trimBar_ = {lx, y, lw, 6 * u};
      fillRect(r, kDeep, trimBar_);
      const float xi = trimBar_.x + trimBar_.w * static_cast<float>(cueIn(c) / c.duration);
      const float xo = trimBar_.x + trimBar_.w * static_cast<float>(cueOut(c) / c.duration);
      fillRect(r, kMid, {xi, trimBar_.y + 0.6f * u, xo - xi, trimBar_.h - 1.2f * u});
      for (float hx : {xi, xo}) fillRect(r, kInOut, {hx - 0.8f * u, trimBar_.y - 0.8f * u, 1.6f * u, trimBar_.h + 1.6f * u});
      if (sheet_ == active_) {
        const float xp = trimBar_.x + trimBar_.w * static_cast<float>(std::clamp(engine_->position() / c.duration, 0.0, 1.0));
        fillRect(r, kLight, {xp - 0.25f * u, trimBar_.y, 0.5f * u, trimBar_.h});
      }
      y += 7.5f * u;
      const double f = cueFrame(c);
      auto stepRow = [&](bool in) {
        const float bw = 7 * u;
        button({lx, y, bw, 4.5f * u}, "<", [this, in, f] { nudgeTrim(in, -f); });
        text_.draw(mono_, std::string(in ? "IN  " : "OUT ") + timecode(in ? cueIn(c) : cueOut(c), c.fps), kDeep,
                   {lx + bw + u, y, lw - 3 * bw - 3 * u, 4.5f * u}, true);
        button({lx + lw - 2 * bw - u, y, bw, 4.5f * u}, ">", [this, in, f] { nudgeTrim(in, f); });
        button({lx + lw - bw, y, bw, 4.5f * u}, "SET", [this, in] { if (sheet_ == active_) setInOut(in); }, sheet_ == active_);
        y += 5.5f * u;
      };
      stepRow(true);
      stepRow(false);
      text_.draw(mono_, "LENGTH " + timecode(cueOut(c) - cueIn(c), c.fps), kDeep, {lx, y, lw * 0.55f, 4.5f * u});
      button({lx + lw * 0.58f, y, lw * 0.42f, 4.5f * u}, "PLAY FROM IN", [this] {
        const int i = sheet_;
        take(i);
      });
      y += 6.5f * u;
    } else {
      trimBar_ = {};
    }

    // Fades and transition: - value +
    auto stepper = [&](const char* label, std::string value, std::function<void()> less, std::function<void()> more) {
      text_.draw(pixelSmall_, label, kDark, {lx, y, 22 * u, 4.5f * u});
      button({lx + 23 * u, y, 7 * u, 4.5f * u}, "-", std::move(less));
      text_.draw(mono_, value, kDeep, {lx + 31 * u, y, lw - 46 * u, 4.5f * u}, true);
      button({lx + lw - 7 * u, y, 7 * u, 4.5f * u}, "+", std::move(more));
      y += 5.5f * u;
    };
    auto secs = [](double v) { char b[16]; std::snprintf(b, sizeof b, "%.1f s", v); return std::string(b); };
    stepper("FADE IN", secs(c.fadeInSeconds), [this] { bump(&Cue::fadeInSeconds, -0.5); }, [this] { bump(&Cue::fadeInSeconds, 0.5); });
    stepper("FADE OUT", secs(c.fadeOutSeconds), [this] { bump(&Cue::fadeOutSeconds, -0.5); }, [this] { bump(&Cue::fadeOutSeconds, 0.5); });
    stepper("CROSSFADE", c.cueTransitionSeconds < 0 ? "0.5 s (usual)" : secs(c.cueTransitionSeconds),
            [this] { bumpTransition(-0.5); }, [this] { bumpTransition(0.5); });

    // HOLD / LOOP / NEXT
    text_.draw(pixelSmall_, "AT THE END", kDark, {lx, y, 22 * u, 4.5f * u});
    const EndMode m = endModeOf(c);
    const float sw = (lw - 23 * u) / 3;
    const std::pair<EndMode, const char*> modes[] = {{EndMode::Hold, "HOLD"}, {EndMode::Loop, "LOOP"}, {EndMode::Next, "NEXT"}};
    for (int k = 0; k < 3; ++k) {
      const EndMode mode = modes[k].first;
      button({lx + 23 * u + static_cast<float>(k) * sw, y, sw - 0.5f * u, 4.5f * u}, modes[k].second,
             [this, mode] { Cue& cc = cues_[static_cast<std::size_t>(sheet_)]; setEndMode(cc, mode); cueEdited(sheet_, false); },
             true, m == mode);
    }
    y += 5.5f * u;

    // VOLUME: -40 dB to +6 dB
    if (c.hasAudio || c.kind == CueKind::Audio) {
      text_.draw(pixelSmall_, "VOLUME", kDark, {lx, y, 22 * u, 4.5f * u});
      volumeBar_ = {lx + 23 * u, y + 1.5f * u, lw - 36 * u, 1.5f * u};
      fillRect(r, kMid, volumeBar_);
      const float k = std::clamp((c.audioGainDb + 40.0f) / 46.0f, 0.0f, 1.0f);
      fillRect(r, kDeep, {volumeBar_.x, volumeBar_.y, volumeBar_.w * k, volumeBar_.h});
      fillRect(r, kDeep, {volumeBar_.x + volumeBar_.w * k - 0.6f * u, y + 0.3f * u, 1.2f * u, 3.9f * u});
      char db[16];
      std::snprintf(db, sizeof db, "%+.0f dB", static_cast<double>(c.audioGainDb));
      text_.draw(mono_, db, kDeep, {lx + lw - 12 * u, y, 12 * u, 4.5f * u}, true);
      y += 5.5f * u;
    } else {
      volumeBar_ = {};
    }

    // DUPLICATE, UP, DOWN, DELETE
    y = std::max(y + u, lcd.y + lcd.h - 6 * u);
    const float qw = (lw - 3 * u) / 4;
    button({lx, y, qw, 5 * u}, "COPY", [this] { duplicateSheetCue(); });
    button({lx + qw + u, y, qw, 5 * u}, "UP", [this] { moveSheetCue(-1); }, sheet_ > 0);
    button({lx + 2 * (qw + u), y, qw, 5 * u}, "DOWN", [this] { moveSheetCue(1); }, sheet_ + 1 < static_cast<int>(cues_.size()));
    button({lx + 3 * (qw + u), y, qw, 5 * u}, confirmDelete_ ? "SURE?" : "DELETE", [this] { deleteSheetCue(); }, true, false, true);
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

  void deleteSheetCue() {
    if (sheet_ < 0) return;
    if (!confirmDelete_) {
      confirmDelete_ = true;
      return;
    }
    if (sheet_ == active_) {
      engine_->clear();
      active_ = -1;
    } else if (active_ > sheet_) {
      --active_;
    }
    cues_.erase(cues_.begin() + sheet_);
    sheet_ = -1;
    confirmDelete_ = false;
    select(std::min(selected_, static_cast<int>(cues_.size()) - 1));
  }

  void tap(float x, float y) {
    const float u = unit_;
    auto within = [&](SDL_FPoint c, float rad) { return (x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= rad * rad; };
    if (within(goCentre_, 10 * u)) { press(Pressed::Go); go(); return; }
    if (within(backCentre_, 8.5f * u)) { press(Pressed::Back); back(); return; }
    const float dx = x - padCentre_.x, dy = y - padCentre_.y;
    if (std::fabs(dx) < 15 * u && std::fabs(dy) < 15 * u) {
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
    if (inside(blackHit, x, y)) { press(Pressed::Black); black_ = !black_; return; }
    if (inside(menuHit, x, y)) { press(Pressed::Menu); menuOpen_ = true; sheet_ = -1; return; }
    if (inside(listLcd_, x, y) && rowH_ > 0) {
      select(scroll_ + static_cast<int>((y - listLcd_.y) / rowH_));
    }
  }

  void select(int i) {
    if (cues_.empty()) return;
    selected_ = std::clamp(i, 0, static_cast<int>(cues_.size()) - 1);
  }

  // GO takes the selected cue and moves the selection on, as the desk does.
  void go() {
    if (followLocked()) return;
    if (cues_.empty()) return;
    take(selected_);
    select(selected_ + 1);
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
    const Cue& c = cues_[static_cast<std::size_t>(i)];
    const double fade = c.cueTransitionSeconds >= 0 ? c.cueTransitionSeconds : 0.5;
    engine_->loadCue(&c, true, fade, fade > 0 ? TransitionStyle::Crossfade : TransitionStyle::Cut);
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

  std::vector<Cue> cues_;
  std::string mediaDir_;
  int nextId_ = 0;
  std::mutex pickLock_;
  std::vector<std::string> picks_;
  bool pickerOpen_ = false;
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
  int menuTab_ = 0;   // ADD, OUTPUT, SYNC, SKIN
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
