// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// mini_main.cpp — Deckboy Mini: one deck, one output, no desk.
//
// The same MediaEngine the desk plays through, driven from the command line,
// the keyboard and Deckboy's plain-text remote protocol. Everything the desk
// has around the engine -- the cue list, inspector, settings, integrations --
// is left out on purpose. If Mini needs it, the desk is the program to run.
//
//   deckboy-mini [options] <file|folder>...
//
// Remote control speaks the desk's protocol on the desk's port, so the
// Companion module and any script written for Deckboy drive Mini as deck 1.
// ============================================================================

#include "core/sdl_compat.hpp"
#include <SDL3_ttf/SDL_ttf.h>

#include "core/constants.hpp"
#include "core/types.hpp"
#include "deckboy_version.hpp"
#include "core/media_probe.hpp"
#include "engine/media_engine.hpp"
#include "mini/mini_hud.hpp"
#include "platform/network.hpp"
#include "platform/pdf_import.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace deckboy::platform;

namespace {

std::atomic<bool> gQuit {false};

// The desk defines this in main.cpp; the engine queues s16 stereo at 48k.
constexpr SDL_AudioFormat kAudioFormat = SDL_AUDIO_S16;

const char* kUsage =
  "Deckboy Mini - one deck, one output, no desk.\n"
  "\n"
  "  deckboy-mini [options] <file|folder>...\n"
  "\n"
  "Options:\n"
  "  --display N    output on display N (1 = first). Default 1.\n"
  "  --window       a window instead of fullscreen\n"
  "  --loop         start again after the last cue\n"
  "  --hold         hold the last frame after the last cue (default: black)\n"
  "  --paused       load the first cue but do not play it\n"
  "  --still S      seconds each image stays up. Default 5.\n"
  "  --volume P     volume, 0-100. Default 100.\n"
  "  --port N       remote control port. Default 5510 (the desk's).\n"
  "  --remote       accept remote control from the network, not only this machine\n"
  "  --plain        plain log lines instead of the status panel\n"
  "  --overlay      start with the status bar shown on the output (H toggles it)\n"
  "  --version      print the version and exit\n"
  "\n"
  "Keys: Space play/pause, Right next, Left previous, S stop, B blackout,\n"
  "      F fullscreen, H status bar, Q quit. Drop files on the output to add them.\n"
  "Remote: send HELP for the commands.\n";

const char* kRemoteHelp =
  "GO | TAKE [n] | SELECT n | NEXT | PREV | SKIP | SKIPBACK | PLAY | PAUSE | STOP | CLEAR | PANIC | "
  "SEEK +-s | SEEKPOS s | VOLUME 0-100 | LOOP ON|OFF|TOGGLE | BLACKOUT ON|OFF|TOGGLE | "
  "OVERLAY ON|OFF|TOGGLE | ADD <file or folder> | DECK 1 <command> | STATUS | PING | QUIT\n";

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

// What Mini can play: anything the desk imports as a video, still or sound.
// PDF decks and MIDI files are the desk's business.
bool playable(const fs::path& p) {
  return deckboy::core::media::isAcceptableMediaPath(p) && !deckboy::platform::isPdfDocumentPath(p);
}

std::string clock(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
  const int tenths = static_cast<int>(std::floor(seconds * 10.0));
  char out[32];
  std::snprintf(out, sizeof(out), "%02d:%02d.%d", tenths / 600, (tenths / 10) % 60, tenths % 10);
  return out;
}

struct Options {
  int display = 1;
  bool windowed = false;
  bool loop = false;
  bool hold = false;
  bool paused = false;
  double stillSeconds = 5.0;
  int volume = 100;
  int port = 5510;
  bool remote = false;
  bool plain = false;
  bool overlay = false;
  std::vector<fs::path> inputs;
};

// Exits with a message on anything it does not understand: a typo in a kiosk's
// start script should stop the script, not run something else quietly.
Options parseArgs(int argc, char** argv) {
  Options o;
  auto need = [&](int& i, const char* flag) -> std::string {
    if (i + 1 >= argc) {
      std::cerr << "deckboy-mini: " << flag << " needs a value\n";
      std::exit(2);
    }
    return argv[++i];
  };
  auto number = [&](const std::string& text, const char* flag) {
    try {
      size_t used = 0;
      double v = std::stod(text, &used);
      if (used == text.size() && std::isfinite(v)) return v;
    } catch (...) {}
    std::cerr << "deckboy-mini: " << flag << " expects a number, got '" << text << "'\n";
    std::exit(2);
  };
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--help" || a == "-h") { std::cout << kUsage; std::exit(0); }
    else if (a == "--version") { std::cout << "deckboy-mini " << deckboy::core::version::kVersion << "\n"; std::exit(0); }
    else if (a == "--display") o.display = static_cast<int>(number(need(i, "--display"), "--display"));
    else if (a == "--window") o.windowed = true;
    else if (a == "--loop") o.loop = true;
    else if (a == "--hold") o.hold = true;
    else if (a == "--paused") o.paused = true;
    else if (a == "--still") o.stillSeconds = number(need(i, "--still"), "--still");
    else if (a == "--volume") o.volume = static_cast<int>(number(need(i, "--volume"), "--volume"));
    else if (a == "--port") o.port = static_cast<int>(number(need(i, "--port"), "--port"));
    else if (a == "--remote") o.remote = true;
    else if (a == "--plain") o.plain = true;
    else if (a == "--overlay") o.overlay = true;
    else if (a.rfind("--", 0) == 0) { std::cerr << "deckboy-mini: unknown option " << a << "\n\n" << kUsage; std::exit(2); }
    else o.inputs.emplace_back(a);
  }
  if (o.inputs.empty()) { std::cerr << kUsage; std::exit(2); }
  o.volume = std::clamp(o.volume, 0, 100);
  o.stillSeconds = std::max(0.1, o.stillSeconds);
  if (o.display < 1) o.display = 1;
  return o;
}

// Files in the order given; a folder contributes its media files, sorted by
// name, which is how a kiosk folder of 01_, 02_, 03_ plays in order. Each is
// probed the way the desk's import probes it -- size, rate, length, sound --
// because the engine plays from those, not from the file name.
std::vector<Cue> cuesFor(const std::vector<fs::path>& inputs, double stillSeconds, std::size_t firstId) {
  std::vector<fs::path> files;
  for (const fs::path& in : inputs) {
    std::error_code ec;
    if (fs::is_directory(in, ec)) {
      std::vector<fs::path> found;
      for (const auto& entry : fs::directory_iterator(in, ec)) {
        if (entry.is_regular_file(ec) && playable(entry.path())) found.push_back(entry.path());
      }
      std::sort(found.begin(), found.end());
      files.insert(files.end(), found.begin(), found.end());
    } else if (fs::is_regular_file(in, ec)) {
      if (playable(in)) files.push_back(in);
      else std::cerr << "deckboy-mini: skipping " << in.string() << " (not a media file)\n";
    } else {
      std::cerr << "deckboy-mini: " << in.string() << " not found\n";
    }
  }
  std::vector<Cue> cues;
  for (const fs::path& f : files) {
    std::optional<Cue> probed = deckboy::core::media::probeCue(fs::absolute(f));
    if (!probed) {
      std::cerr << "deckboy-mini: skipping " << f.string() << " (could not read it)\n";
      continue;
    }
    Cue cue = std::move(*probed);
    cue.id = "mini-" + std::to_string(firstId + cues.size());
    if (cue.kind == CueKind::Image) cue.stillDurationSeconds = stillSeconds;
    cues.push_back(std::move(cue));
  }
  return cues;
}

std::vector<Cue> buildPlaylist(const Options& o) { return cuesFor(o.inputs, o.stillSeconds, 1); }

// ── The player ──────────────────────────────────────────────────────────────

class Mini {
 public:
  Mini(Options options, std::vector<Cue> cues) : opt_(std::move(options)), cues_(std::move(cues)) {}

  int run() {
    if (!open()) return 1;
    overlay_ = opt_.overlay;
    nextId_ = cues_.size() + 1;
    hud_.begin(opt_.plain);
    hud_.boot(bootFacts());
    take(0, !opt_.paused);
    while (!gQuit.load()) {
      pumpEvents();
      pollRemote();
      engine_->update();
      if (engine_->reachedEnd()) cueEnded();
      draw();
      hud_.frame(hudState());
    }
    hud_.log("goodbye");
    hud_.frame(hudState());
    hud_.end();
    shutdown();
    return 0;
  }

 private:
  bool open() {
    // The desk's two load-bearing hints. Without the first a fullscreen output
    // minimises when focus moves; without the second the in-process decoder
    // shares a single-threaded D3D11 device and crashes at random.
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D_THREADSAFE, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
      std::cerr << "deckboy-mini: SDL failed to start: " << SDL_GetError() << "\n";
      return false;
    }
    TTF_Init();

    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    SDL_DisplayID display = (displays && count > 0) ? displays[std::min(opt_.display, count) - 1] : 0;
    if (opt_.display > count) {
      std::cerr << "deckboy-mini: there is no display " << opt_.display << " (" << count
                << " found), using display " << count << "\n";
    }
    SDL_free(displays);

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Deckboy Mini");
    // A fullscreen output is CREATED at the display's own size and position.
    // Made at 1280x720 and switched afterwards, it went fullscreen at 1920x1080
    // on a second display while the renderer kept drawing 1280x720 in the
    // corner: SDL changes fullscreen asynchronously.
    SDL_Rect bounds {0, 0, 1280, 720};
    const bool startFullscreen = !opt_.windowed && display != 0 && SDL_GetDisplayBounds(display, &bounds);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER,
                          startFullscreen ? bounds.x : SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER,
                          startFullscreen ? bounds.y : SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, startFullscreen ? bounds.w : 1280);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, startFullscreen ? bounds.h : 720);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, startFullscreen);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    window_ = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!window_) {
      std::cerr << "deckboy-mini: no window: " << SDL_GetError() << "\n";
      return false;
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
      std::cerr << "deckboy-mini: no renderer: " << SDL_GetError() << "\n";
      return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    setFullscreen(!opt_.windowed);

    SDL_AudioSpec spec {};
    spec.freq = kAudioRate;
    spec.format = kAudioFormat;
    spec.channels = kAudioChannels;
    audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!audio_) std::cerr << "deckboy-mini: no audio device (" << SDL_GetError() << "); picture only\n";

    engine_ = std::make_unique<MediaEngine>(renderer_, audio_);
    engine_->setVolume(static_cast<float>(opt_.volume) / 100.0f);

    listen_ = createBoundSocket(SOCK_STREAM, opt_.port, true, !opt_.remote);
    if (listen_ == kInvalidSocket) {
      std::cerr << "deckboy-mini: port " << opt_.port
                << " is taken (is the desk running?). Remote control is off; use --port to pick another.\n";
    }
    return true;
  }

  void shutdown() {
    for (Client& c : clients_) closeSocket(c.socket);
    if (listen_ != kInvalidSocket) closeSocket(listen_);
    engine_.reset();
    if (audio_) SDL_DestroyAudioStream(audio_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    TTF_Quit();
    SDL_Quit();
  }

  void setFullscreen(bool on) {
    fullscreen_ = on;
    SDL_SetWindowFullscreen(window_, on);
    SDL_SyncWindow(window_);  // wait for the change, so the next frame is drawn at the new size
    if (on) SDL_HideCursor(); else SDL_ShowCursor();
  }

  // ── Transport ──

  bool validIndex(int i) const { return i >= 0 && i < static_cast<int>(cues_.size()); }

  void take(int index, bool autoplay = true) {
    if (!validIndex(index)) return;
    selected_ = active_ = index;
    stopped_ = false;
    engine_->loadCue(&cues_[index], autoplay);
    hud_.log("take " + std::to_string(index + 1) + "/" + std::to_string(cues_.size()) + "  " + cues_[index].name);
  }

  void stop() {
    engine_->clear();
    active_ = -1;
    stopped_ = true;
  }

  void cueEnded() {
    const int next = active_ + 1;
    if (validIndex(next)) { take(next); return; }
    if (opt_.loop && !cues_.empty()) { take(0); return; }
    // End of the list: hold the frame or go to black, and stay put.
    engine_->finalizeReachedEnd(opt_.hold);
    if (!opt_.hold) stop();
    hud_.log(opt_.hold ? "end of list, holding the last frame" : "end of list");
  }

  void go() {
    if (active_ < 0) { take(selected_); return; }
    if ((engine_->state() == TransportState::Playing)) engine_->pause(); else engine_->play();
  }

  // ── Output ──

  void draw() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    if (!blackout_ && active_ >= 0) {
      int w = 0, h = 0;
      SDL_GetRenderOutputSize(renderer_, &w, &h);
      engine_->render(SDL_Rect {0, 0, w, h});
    }
    if (overlay_) drawOverlay();
    SDL_RenderPresent(renderer_);
  }

  void pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) gQuit = true;
      if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) addInputs({fs::path(e.drop.data)}, "dropped");
      if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) continue;
      switch (e.key.key) {
        case SDLK_SPACE: go(); break;
        case SDLK_RIGHT: take(std::min(active_ + 1, static_cast<int>(cues_.size()) - 1)); break;
        case SDLK_LEFT: take(std::max(active_ - 1, 0)); break;
        case SDLK_S: stop(); break;
        case SDLK_B: blackout_ = !blackout_; break;
        case SDLK_F: setFullscreen(!fullscreen_); break;
        case SDLK_H: case SDLK_TAB: overlay_ = !overlay_; break;
        case SDLK_Q: case SDLK_ESCAPE: gQuit = true; break;
        default: break;
      }
    }
  }

  // ── Remote control ──

  struct Client {
    SocketHandle socket = kInvalidSocket;
    std::string buffer;
  };

  void pollRemote() {
    if (listen_ == kInvalidSocket) return;
    fd_set readable;
    FD_ZERO(&readable);
    SocketHandle maxFd = listen_;
    watchFd(listen_, &readable, maxFd);
    for (Client& c : clients_) watchFd(c.socket, &readable, maxFd);
    timeval none {0, 0};
    if (select(selectNfds(maxFd), &readable, nullptr, nullptr, &none) <= 0) return;

    if (readyFd(listen_, &readable)) {
      sockaddr_in from {};
      socklen_t len = sizeof(from);
      SocketHandle s = static_cast<SocketHandle>(accept(listen_, reinterpret_cast<sockaddr*>(&from), &len));
      if (s != kInvalidSocket && clients_.size() < 16) clients_.push_back({s, {}});
      else if (s != kInvalidSocket) closeSocket(s);
    }
    for (size_t i = 0; i < clients_.size();) {
      Client& c = clients_[i];
      bool alive = true;
      if (readyFd(c.socket, &readable)) {
        char buf[2048];
        const int n = static_cast<int>(recv(c.socket, buf, sizeof(buf), 0));
        if (n <= 0) alive = false;
        else c.buffer.append(buf, static_cast<size_t>(n));
        size_t eol;
        while (alive && (eol = c.buffer.find('\n')) != std::string::npos) {
          std::string line = c.buffer.substr(0, eol);
          c.buffer.erase(0, eol + 1);
          if (!line.empty() && line.back() == '\r') line.pop_back();
          reply(c.socket, handle(line));
        }
        if (c.buffer.size() > 65536) alive = false;
      }
      if (alive) { ++i; continue; }
      closeSocket(c.socket);
      clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }

  static void reply(SocketHandle s, const std::string& text) {
    if (text.empty()) return;
    send(s, text.data(), static_cast<int>(text.size()), kSocketSendFlags);
  }

  // Same shape as the desk's replies: OK <VERB>, ERR <VERB>: <why>, and STATUS
  // as the report itself, so a client written for the desk reads Mini too.
  std::string handle(const std::string& raw) {
    std::istringstream in(raw);
    std::vector<std::string> parts;
    for (std::string w; in >> w;) parts.push_back(w);
    if (parts.empty()) return {};
    std::string verb = upper(parts[0]);

    // Mini is deck 1. "DECK 1 TAKE" is "TAKE"; any other deck does not exist.
    if (verb == "DECK") {
      if (parts.size() < 2 || parts[1] != "1") return "ERR DECK: Mini has one deck\n";
      if (parts.size() == 2) return "OK DECK\n";
      parts.erase(parts.begin(), parts.begin() + 2);
      verb = upper(parts[0]);
    }
    const std::string arg = parts.size() > 1 ? parts[1] : std::string();
    auto ok = [&] { return "OK " + verb + "\n"; };
    auto err = [&](const std::string& why) { return "ERR " + verb + ": " + why + "\n"; };
    auto cueArg = [&]() -> std::optional<int> {
      try {
        size_t used = 0;
        const int n = std::stoi(arg, &used);
        if (used == arg.size() && validIndex(n - 1)) return n - 1;
      } catch (...) {}
      return std::nullopt;
    };
    auto noCue = [&] { return err("no cue " + arg + " (" + std::to_string(cues_.size()) + " cues)"); };
    auto onOff = [&](bool current) -> std::optional<bool> {
      const std::string a = upper(arg);
      if (a == "ON") return true;
      if (a == "OFF") return false;
      if (a.empty() || a == "TOGGLE") return !current;
      return std::nullopt;
    };

    if (verb == "STATUS" || verb == "STATE") return status();
    if (verb == "HELP") return kRemoteHelp;
    if (verb == "PING") return ok();
    hud_.log("remote " + raw);
    if (verb == "GO") { go(); return ok(); }
    if (verb == "TAKE") {
      if (arg.empty()) { take(selected_); return ok(); }
      auto n = cueArg();
      if (!n) return noCue();
      take(*n);
      return ok();
    }
    if (verb == "SELECT") {
      auto n = cueArg();
      if (!n) return noCue();
      selected_ = *n;
      return ok();
    }
    if (verb == "NEXT") { selected_ = std::min(selected_ + 1, static_cast<int>(cues_.size()) - 1); return ok(); }
    if (verb == "PREV" || verb == "PREVIOUS") { selected_ = std::max(selected_ - 1, 0); return ok(); }
    if (verb == "SKIP") { take(std::min(std::max(active_, selected_ - 1) + 1, static_cast<int>(cues_.size()) - 1)); return ok(); }
    if (verb == "SKIPBACK") { take(std::max(active_ - 1, 0)); return ok(); }
    if (verb == "PLAY") { if (active_ < 0) take(selected_); else engine_->play(); return ok(); }
    if (verb == "PAUSE") { engine_->pause(); return ok(); }
    if (verb == "STOP" || verb == "CLEAR" || verb == "PANIC") { stop(); return ok(); }
    if (verb == "SEEK" || verb == "SEEKPOS") {
      double s = 0.0;
      try { s = std::stod(arg); } catch (...) { return err("expected seconds"); }
      if (active_ < 0) return err("nothing is playing");
      engine_->seek(std::max(0.0, verb == "SEEK" ? engine_->position() + s : s));
      return ok();
    }
    if (verb == "VOLUME") {
      int v = 0;
      try { v = std::stoi(arg); } catch (...) { return err("expected 0-100"); }
      if (v < 0 || v > 100) return err("expected 0-100");
      opt_.volume = v;
      engine_->setVolume(static_cast<float>(v) / 100.0f);
      return ok();
    }
    if (verb == "LOOP") {
      auto v = onOff(opt_.loop);
      if (!v) return err("ON, OFF or TOGGLE");
      opt_.loop = *v;
      return ok();
    }
    if (verb == "BLACKOUT") {
      auto v = onOff(blackout_);
      if (!v) return err("ON, OFF or TOGGLE");
      blackout_ = *v;
      return ok();
    }
    if (verb == "OVERLAY") {
      auto v = onOff(overlay_);
      if (!v) return err("ON, OFF or TOGGLE");
      overlay_ = *v;
      return ok();
    }
    if (verb == "ADD") {
      // The rest of the line is the path, spaces and all; quotes are optional.
      const std::size_t at = upper(raw).find("ADD");
      std::string path = at == std::string::npos ? std::string() : raw.substr(at + 3);
      const std::size_t first = path.find_first_not_of(" \t\"");
      path = first == std::string::npos ? std::string() : path.substr(first);
      while (!path.empty() && (path.back() == ' ' || path.back() == '\t' || path.back() == '"')) path.pop_back();
      if (path.empty()) return err("expected a file or folder");
      const int added = addInputs({fs::path(path)}, "added");
      if (added == 0) return err("nothing playable at " + path);
      return ok();
    }
    if (verb == "QUIT") { gQuit = true; return ok(); }
    return "ERR " + verb + ": unknown command (send HELP)\n";
  }

  // The desk's STATUS shape, trimmed to what one deck has.
  std::string status() const {
    const std::string status = active_ < 0 ? "Stopped" : ((engine_->state() == TransportState::Playing) ? "Playing" : "Paused");
    const Cue* live = active_ >= 0 ? &cues_[active_] : nullptr;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    std::ostringstream s;
    s << "DECKBOY_0.01 app=mini focus=1 decks=1 outputs=1 master_vol=" << opt_.volume
      << " blackout=" << (blackout_ ? "on" : "off") << " loop=" << (opt_.loop ? "on" : "off") << "\n";
    s << "DECK 1 name=\"Deck 1\" status=" << status
      << " selected=" << (cues_.empty() ? 0 : selected_ + 1) << " active=" << active_ + 1
      << " selected_num=\"" << (cues_.empty() ? 0 : selected_ + 1) << "\""
      << " active_num=\"" << (active_ < 0 ? 0 : active_ + 1) << "\""
      << " cues=" << cues_.size()
      << " cue=\"" << (live ? live->name : std::string()) << "\""
      << " pos=" << (live ? clock(engine_->position()) : std::string("--:--"))
      << " dur=" << (live && engine_->duration() > 0.0 ? clock(engine_->duration()) : std::string("--:--"))
      << " vol=" << opt_.volume << "\n";
    s << "OUTPUT 1 name=\"Output 1\" enabled=" << (blackout_ ? "off" : "on")
      << " health=live display=" << opt_.display << " raster=" << w << "x" << h
      << " fullscreen=" << (fullscreen_ ? "on" : "off") << "\n";
    return s.str();
  }

  // New cues go on the end of the list; nothing already playing is touched.
  // Shared by a file dropped on the output and the remote ADD.
  int addInputs(const std::vector<fs::path>& inputs, const char* how) {
    std::vector<Cue> more = cuesFor(inputs, opt_.stillSeconds, nextId_);
    nextId_ += more.size();
    for (Cue& c : more) cues_.push_back(std::move(c));
    if (!more.empty()) {
      hud_.log(std::string(how) + " " + std::to_string(more.size()) + (more.size() == 1 ? " cue" : " cues") +
               ", " + std::to_string(cues_.size()) + " in the list");
    }
    return static_cast<int>(more.size());
  }

  // The status bar on the OUTPUT, for a screen with no terminal beside it.
  // Off unless asked for, because the output is what the room sees. SDL's
  // built-in 8x8 font, scaled to the raster: pixel text, no font files.
  void drawOverlay() {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(renderer_, &w, &h);
    if (w <= 0 || h <= 0) return;
    const float scale = std::max(1.0f, std::floor(static_cast<float>(h) / 360.0f));
    const float vw = w / scale, vh = h / scale;   // the canvas in font pixels
    const float pad = 6.0f, line = 8.0f, gap = 5.0f;
    const float barH = pad * 2 + line * 2 + gap * 2 + 2.0f;
    const float top = vh - barH;
    const mini::HudState st = hudState();
    auto ascii = [](std::string text) {
      for (char& c : text) if (static_cast<unsigned char>(c) > 126 || static_cast<unsigned char>(c) < 32) c = '?';
      return text;
    };
    auto clip = [&](std::string text, float room) {
      const std::size_t fits = static_cast<std::size_t>(std::max(0.0f, room / 8.0f));
      if (text.size() > fits) text = fits > 1 ? text.substr(0, fits - 1) + "~" : std::string();
      return text;
    };

    SDL_SetRenderScale(renderer_, scale, scale);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 15, 56, 15, 238);              // deep LCD green
    SDL_FRect bar {0.0f, top, vw, barH};
    SDL_RenderFillRect(renderer_, &bar);
    SDL_SetRenderDrawColor(renderer_, 48, 98, 48, 255);
    SDL_FRect rule {0.0f, top, vw, 1.0f};
    SDL_RenderFillRect(renderer_, &rule);

    const std::string state = st.blackout ? "BLACKOUT" : st.status == "Playing" ? "> PLAYING" : st.status == "Paused" ? "|| PAUSED" : "# STOPPED";
    const std::string times = st.cue > 0 ? clock(st.position) + " / " + (st.duration > 0.0 ? clock(st.duration) : std::string("--:--.-")) : std::string();
    const std::string cue = st.cue > 0 ? std::to_string(st.cue) + "/" + std::to_string(st.cueCount) + "  " + ascii(st.cueName)
                                       : "-/" + std::to_string(st.cueCount);
    const float y1 = top + pad, y2 = y1 + line + gap * 2 + 2.0f;
    const float timesX = vw - pad - times.size() * 8.0f;

    SDL_SetRenderDrawColor(renderer_, 155, 188, 15, 255);              // bright LCD
    SDL_RenderDebugText(renderer_, pad, y1, state.c_str());
    SDL_RenderDebugText(renderer_, pad + 11 * 8.0f, y1, clip(cue, timesX - (pad + 11 * 8.0f) - 16.0f).c_str());
    SDL_SetRenderDrawColor(renderer_, 139, 172, 15, 255);
    SDL_RenderDebugText(renderer_, timesX, y1, times.c_str());

    // Progress, a two-pixel line between the rows.
    const float py = y1 + line + gap - 1.0f;
    SDL_SetRenderDrawColor(renderer_, 48, 98, 48, 255);
    SDL_FRect track {pad, py, vw - pad * 2, 2.0f};
    SDL_RenderFillRect(renderer_, &track);
    if (st.cue > 0 && st.duration > 0.0) {
      SDL_SetRenderDrawColor(renderer_, 155, 188, 15, 255);
      SDL_FRect done {pad, py, static_cast<float>((vw - pad * 2) * std::clamp(st.position / st.duration, 0.0, 1.0)), 2.0f};
      SDL_RenderFillRect(renderer_, &done);
    }

    const std::string next = st.next > 0 ? "NEXT " + std::to_string(st.next) + "  " + ascii(st.nextName)
                                         : std::string(st.loop ? "NEXT back to 1" : "NEXT end of list");
    std::string right = std::string(st.loop ? "LOOP  " : "") + "VOL " + std::to_string(st.volume);
    if (st.listening) right += "  :" + std::to_string(st.port);
    const float rightX = vw - pad - right.size() * 8.0f;
    SDL_SetRenderDrawColor(renderer_, 139, 172, 15, 255);
    SDL_RenderDebugText(renderer_, pad, y2, clip(next, rightX - pad - 16.0f).c_str());
    SDL_RenderDebugText(renderer_, rightX, y2, right.c_str());
    SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
  }

  // What the boot sequence reports: the real state of this run.
  std::vector<std::pair<std::string, std::string>> bootFacts() const {
    int video = 0, stills = 0, sound = 0;
    for (const Cue& c : cues_) {
      if (c.kind == CueKind::Image) ++stills;
      else if (c.kind == CueKind::Audio) ++sound;
      else ++video;
    }
    std::string parts;
    auto add = [&](int n, const char* what) {
      if (n) parts += (parts.empty() ? "" : ", ") + std::to_string(n) + " " + what;
    };
    add(video, "video");
    add(stills, stills == 1 ? "still" : "stills");
    add(sound, "sound");
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    return {
      {"cartridge", std::to_string(cues_.size()) + (cues_.size() == 1 ? " cue (" : " cues (") + parts + ")"},
      {"output", "display " + std::to_string(opt_.display) + ", " + std::to_string(w) + "x" + std::to_string(h) +
                 (fullscreen_ ? ", fullscreen" : ", window")},
      {"sound", audio_ ? "default device" : "none, picture only"},
      {"link", listen_ != kInvalidSocket
                 ? ":" + std::to_string(opt_.port) + (opt_.remote ? " (network)" : " (this machine)")
                 : "off, port " + std::to_string(opt_.port) + " is taken"},
    };
  }

  mini::HudState hudState() const {
    mini::HudState s;
    s.version = deckboy::core::version::kVersion;
    s.status = active_ < 0 ? "Stopped" : (engine_->state() == TransportState::Playing ? "Playing" : "Paused");
    s.cue = active_ + 1;
    s.cueCount = static_cast<int>(cues_.size());
    if (active_ >= 0) s.cueName = cues_[active_].name;
    int nextIndex = active_ < 0 ? selected_ : active_ + 1;
    if (!validIndex(nextIndex) && opt_.loop) nextIndex = 0;
    if (validIndex(nextIndex)) {
      s.next = nextIndex + 1;
      s.nextName = cues_[nextIndex].name;
    }
    s.position = active_ >= 0 ? engine_->position() : 0.0;
    s.duration = active_ >= 0 ? engine_->duration() : 0.0;
    s.volume = opt_.volume;
    s.loop = opt_.loop;
    s.blackout = blackout_;
    s.display = opt_.display;
    SDL_GetWindowSizeInPixels(window_, &s.width, &s.height);
    s.fullscreen = fullscreen_;
    s.port = opt_.port;
    s.listening = listen_ != kInvalidSocket;
    s.network = opt_.remote;
    s.controllers = static_cast<int>(clients_.size());
    s.audioLevel = engine_->programAudioLevel01();
    return s;
  }

  mini::Hud hud_;
  Options opt_;
  std::vector<Cue> cues_;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_AudioStream* audio_ = nullptr;
  std::unique_ptr<MediaEngine> engine_;
  SocketHandle listen_ = kInvalidSocket;
  std::vector<Client> clients_;
  int selected_ = 0;
  int active_ = -1;
  bool stopped_ = true;
  bool blackout_ = false;
  bool fullscreen_ = false;
  bool overlay_ = false;
  std::size_t nextId_ = 1;
};

}  // namespace

int main(int argc, char** argv) {
  Options options = parseArgs(argc, argv);
  std::vector<Cue> cues = buildPlaylist(options);
  if (cues.empty()) {
    std::cerr << "deckboy-mini: nothing to play\n";
    return 2;
  }
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::cerr << "deckboy-mini: networking failed to start\n";
    return 1;
  }
#endif
  std::signal(SIGINT, [](int) { gQuit = true; });
  std::signal(SIGTERM, [](int) { gQuit = true; });
  const int code = Mini(std::move(options), std::move(cues)).run();
#ifdef _WIN32
  WSACleanup();
#endif
  return code;
}
