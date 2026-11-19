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
#include "core/sdl_compat.hpp"
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "core/constants.hpp"
#include "core/fs.hpp"
#include "core/media_probe.hpp"
#include "core/pattern_helpers.hpp"
#include "core/types.hpp"
#include "engine/media_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <cctype>
#include <mutex>

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

// The Pocket theme's four LCD inks (data/themes/pocket/theme.txt) and its shell.
struct Ink { Uint8 r, g, b; };
constexpr Ink kLight {196, 207, 161};
constexpr Ink kMid {140, 148, 106};
constexpr Ink kDark {74, 80, 56};
constexpr Ink kDeep {26, 30, 20};
constexpr Ink kShell {184, 184, 176};
constexpr Ink kShellShade {150, 150, 142};
constexpr Ink kBezel {44, 44, 52};
constexpr Ink kPad {30, 30, 34};
constexpr Ink kButton {120, 28, 62};
constexpr Ink kButtonLit {176, 60, 104};
constexpr Ink kPill {96, 96, 104};
constexpr Ink kPrint {46, 46, 70};
constexpr Ink kBlack {0, 0, 0};

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
void fillOutline(SDL_Renderer* r, Ink c, const std::vector<SDL_FPoint>& outline, SDL_FPoint centre) {
  if (outline.size() < 3) return;
  std::vector<SDL_Vertex> v;
  std::vector<int> idx;
  const SDL_FColor col {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f};
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

std::vector<SDL_FPoint> roundRectOutline(const SDL_FRect& b, float radius) {
  const float rad = std::min({radius, b.w / 2, b.h / 2});
  std::vector<SDL_FPoint> pts;
  const float cx[4] = {b.x + b.w - rad, b.x + rad, b.x + rad, b.x + b.w - rad};
  const float cy[4] = {b.y + rad, b.y + rad, b.y + b.h - rad, b.y + b.h - rad};
  const float start[4] = {-90, 180, 90, 0};
  for (int k = 0; k < 4; ++k) {
    for (int i = 0; i <= 8; ++i) {
      const float a = (start[k] - 90.0f * static_cast<float>(i) / 8.0f) * 3.14159265f / 180.0f;
      pts.push_back({cx[k] + rad * std::cos(a), cy[k] + rad * std::sin(a)});
    }
  }
  return pts;
}

void fillRound(SDL_Renderer* r, Ink c, const SDL_FRect& b, float radius) {
  fillOutline(r, c, roundRectOutline(b, radius), {b.x + b.w / 2, b.y + b.h / 2});
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

// ── The app ──────────────────────────────────────────────────────────────────

class Pocket {
 public:
  bool init() {
    const std::string data = prepareDataDir();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
      SDL_Log("pocket: SDL_Init failed: %s", SDL_GetError());
      return false;
    }
    if (!TTF_Init()) SDL_Log("pocket: TTF_Init failed: %s", SDL_GetError());
    window_ = SDL_CreateWindow("Deckboy Pocket", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
    engine_ = std::make_unique<MediaEngine>(renderer_, audio_);
    engine_->setDrawsOwnTexture(true);
    engine_->setVolume(0.8f);

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
    loadFonts();
    return true;
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
      case SDL_EVENT_KEY_DOWN:
        switch (e.key.key) {
          case SDLK_RIGHT: case SDLK_PAGEDOWN: case SDLK_SPACE: go(); break;
          case SDLK_LEFT: case SDLK_PAGEUP: back(); break;
          case SDLK_UP: select(selected_ - 1); break;
          case SDLK_DOWN: select(selected_ + 1); break;
          case SDLK_B: black_ = !black_; break;
          case SDLK_MENU: case SDLK_A: openPicker(); break;
          case SDLK_AC_BACK: return true;   // the system Back gesture does not quit mid-show
          default: break;
        }
        break;
      case SDL_EVENT_FINGER_DOWN:
        tap(e.tfinger.x * pxW_, e.tfinger.y * pxH_);
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
    takePicks();
    engine_->update();
    drawOutputScreen();
    layout();
    SDL_Renderer* r = renderer_;
    fillRect(r, kShell, {0, 0, pxW_, pxH_});
    drawProgram();
    drawCueList();
    drawControls();
    SDL_RenderPresent(r);
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
    program_ = {x0 + 2 * u, y0 + 7 * u, 68 * u, 52 * u};
    list_ = {x0 + 2 * u, program_.y + program_.h + 2.5f * u, 68 * u, 0};
    const float stripH = 58 * u;
    const float bottom = safe_.y + safe_.h;
    list_.h = std::max(20 * u, bottom - stripH - list_.y - 2 * u);
    strip_ = {x0, bottom - stripH, safe_.w, stripH};
    const float cy = strip_.y + 22 * u;
    padCentre_ = {x0 + 18 * u, cy};
    goCentre_ = {x0 + 57 * u, cy - 6 * u};
    backCentre_ = {x0 + 41 * u, cy + 9 * u};
    blackPill_ = {x0 + 18 * u, strip_.y + 44 * u, 12 * u, 4 * u};
    menuPill_ = {x0 + 36 * u, strip_.y + 44 * u, 12 * u, 4 * u};
  }

  void drawProgram() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    if (SDL_GetTicks() < noticeUntil_) {
      text_.draw(mono_, notice_, kPrint, header_);
    } else {
      text_.draw(pixel_, "DECKBOY POCKET", kPrint, header_);
    }
    fillRound(r, kBezel, program_, 3 * u);
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
    if (active_ >= 0 && !black_) {
      engine_->render(SDL_Rect {static_cast<int>(pic.x), static_cast<int>(pic.y), static_cast<int>(pic.w), static_cast<int>(pic.h)});
    }
    // name and clock under the picture
    const float ty = pic.y + pic.h + 1.2f * u;
    const std::string name = active_ >= 0 ? cues_[static_cast<std::size_t>(active_)].name : "Nothing on air";
    text_.draw(mono_, name, kDeep, {lcd.x + u, ty, lcd.w * 0.6f, 4 * u});
    const double pos = active_ >= 0 ? engine_->position() : 0.0;
    text_.draw(mono_, clock(pos), kDeep, {lcd.x + lcd.w * 0.6f, ty, lcd.w * 0.4f - u, 4 * u}, false);
  }

  void drawCueList() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
    fillRound(r, kBezel, list_, 3 * u);
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
      if (isLive) fillRect(r, kDeep, row);
      if (isSel && !isLive) {
        setInk(r, kDeep);
        SDL_FRect outline {row.x + 0.3f * u, row.y + 0.3f * u, row.w - 0.6f * u, row.h - 0.6f * u};
        SDL_RenderRect(r, &outline);
      }
      const Ink ink = isLive ? kLight : kDeep;
      text_.draw(mono_, std::to_string(i + 1), isLive ? kMid : kDark, {row.x + 1.5f * u, row.y, 5 * u, row.h});
      text_.draw(mono_, cues_[static_cast<std::size_t>(i)].name, ink, {row.x + 7 * u, row.y, row.w - 8.5f * u, row.h});
    }
  }

  void drawControls() {
    SDL_Renderer* r = renderer_;
    const float u = unit_;
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
    text_.draw(pixelSmall_, "ADD", kPrint, {menuPill_.x - 2 * u, menuPill_.y + 5 * u, menuPill_.w + 4 * u, 3 * u}, true);
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
    if (active_ >= 0 && !black_) {
      // 16:9 picture, letterboxed into the screen's shape.
      float pw = static_cast<float>(bw), ph = pw * 9.0f / 16.0f;
      if (ph > static_cast<float>(bh)) { ph = static_cast<float>(bh); pw = ph * 16.0f / 9.0f; }
      engine_->render(SDL_Rect {static_cast<int>((static_cast<float>(bw) - pw) / 2), static_cast<int>((static_cast<float>(bh) - ph) / 2),
                                static_cast<int>(pw), static_cast<int>(ph)});
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

  void tap(float x, float y) {
    const float u = unit_;
    auto within = [&](SDL_FPoint c, float rad) { return (x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= rad * rad; };
    if (within(goCentre_, 10 * u)) { press(Pressed::Go); go(); return; }
    if (within(backCentre_, 8.5f * u)) { press(Pressed::Back); back(); return; }
    const float dx = x - padCentre_.x, dy = y - padCentre_.y;
    if (std::fabs(dx) < 15 * u && std::fabs(dy) < 15 * u) {
      if (std::fabs(dx) > std::fabs(dy)) {
        if (dx > 0) { press(Pressed::Right); go(); } else { press(Pressed::Left); back(); }
      } else {
        if (dy > 0) { press(Pressed::Down); select(selected_ + 1); } else { press(Pressed::Up); select(selected_ - 1); }
      }
      return;
    }
    const SDL_FRect blackHit {blackPill_.x - 2 * u, blackPill_.y - 3 * u, blackPill_.w + 4 * u, blackPill_.h + 9 * u};
    const SDL_FRect menuHit {menuPill_.x - 2 * u, menuPill_.y - 3 * u, menuPill_.w + 4 * u, menuPill_.h + 9 * u};
    if (inside(blackHit, x, y)) { press(Pressed::Black); black_ = !black_; return; }
    if (inside(menuHit, x, y)) { press(Pressed::Menu); openPicker(); return; }
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
    if (cues_.empty()) return;
    take(selected_);
    select(selected_ + 1);
  }

  void back() {
    if (cues_.empty()) return;
    const int to = active_ > 0 ? active_ - 1 : 0;
    take(to);
    select(to + 1);
  }

  void take(int i) {
    if (i < 0 || i >= static_cast<int>(cues_.size())) return;
    active_ = i;
    black_ = false;
    engine_->loadCue(&cues_[static_cast<std::size_t>(i)], true, 0.5, TransitionStyle::Crossfade);
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
