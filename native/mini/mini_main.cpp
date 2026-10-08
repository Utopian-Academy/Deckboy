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
#include "core/caption_formats.hpp"
#include "core/paths.hpp"
#include "core/subtitle_parser.hpp"
#include "engine/media_engine.hpp"
#include "engine/stage_timings.hpp"
#include "mini/mini_hud.hpp"
#include "mini/mini_keys.hpp"
#include "platform/network.hpp"
#include "platform/pdf_import.hpp"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <functional>
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
  "  deckboy-mini [options] <file|folder|playlist.m3u8>...\n"
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
  "Keys, in this terminal or the output window:\n"
  "  Space go (play/pause, or take)   Enter take the selected or typed cue\n"
  "  Up/Down pick a cue   Left/Right take previous/next   0-9 type a cue number\n"
  "  [ ] seek 10s   - + volume   S stop   B blackout   L loop   F fullscreen\n"
  "  H status bar on the output   A add files   : command line   ? help   Q Q quit\n"
  "Editing the list, in this terminal:\n"
  "  < > move the selected cue   X X remove it   R rename   T still time\n"
  "  Shift+L loop this cue   W save the list   O open a list\n"
  "Output and sound:\n"
  "  D next display   V output on/off   P next sound device\n"
  "Playing a file (as in mpv):\n"
  "  , . a frame back / on   { } a second back / on   ( ) slower / faster\n"
  "  M mute   K A-B loop (A, B, off)   J subtitles beside the file (cycle, off)\n"
  "  # next sound track of the file\n"
  "Drop files on the output to add them.\n"
  "Remote: send HELP for the commands.\n";

const char* kRemoteHelp =
  "GO | TAKE [n] | SELECT n | NEXT | PREV | SKIP | SKIPBACK | PLAY | PAUSE | STOP | CLEAR | PANIC | "
  "SEEK +-s | SEEKPOS s | VOLUME 0-100 | LOOP ON|OFF|TOGGLE | BLACKOUT ON|OFF|TOGGLE | "
  "OVERLAY ON|OFF|TOGGLE | ADD <file or folder> | DECK 1 <command> | STATUS | PING | QUIT | "
  "MOVE n to | REMOVE n | RENAME n name | STILL n s | CUELOOP n ON|OFF|TOGGLE | SAVE [file] | OPEN file | "
  "DISPLAYS | DISPLAY n|NEXT | OUTPUT ON|OFF|TOGGLE | AUDIO LIST|NEXT|DEFAULT|<name> | "
  "SPEED 0.25-4 | MUTE ON|OFF|TOGGLE | FRAME [BACK] | ABLOOP [a b|OFF] | SUBS [ON|OFF] | AUDIOTRACK n|NEXT\n";

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
using Report = std::function<void(const std::string&)>;

void toStderr(const std::string& text) { std::cerr << "deckboy-mini: " << text << "\n"; }

std::vector<Cue> cuesFor(const std::vector<fs::path>& inputs, double stillSeconds, std::size_t firstId,
                         const Report& report = toStderr) {
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
      else report("skipping " + in.string() + " (not a media file)");
    } else {
      report(in.string() + " not found");
    }
  }
  std::vector<Cue> cues;
  for (const fs::path& f : files) {
    std::optional<Cue> probed = deckboy::core::media::probeCue(fs::absolute(f));
    if (!probed) {
      report("skipping " + f.string() + " (could not read it)");
      continue;
    }
    Cue cue = std::move(*probed);
    cue.id = "mini-" + std::to_string(firstId + cues.size());
    if (cue.kind == CueKind::Image) cue.stillDurationSeconds = stillSeconds;
    cues.push_back(std::move(cue));
  }
  return cues;
}

// ── PLAYLIST FILES ──
//
// An extended M3U8, so any player that reads a playlist reads this one, and a
// person can read it too. Deckboy's own settings ride in a comment line other
// players skip:
//
//   #EXTM3U
//   #EXTINF:12.5,Opening titles
//   #DECKBOY:loop=1;still=8
//   media/opening.mp4
//
// Paths are written relative to the playlist when the file sits under its
// folder, so a show folder can be copied to another machine whole.
// UTF-8 both ways, explicitly: a playlist is UTF-8 on every platform, and
// C++20's u8string is char8_t, which a stream will not take as text.
std::string utf8Of(const fs::path& p) {
  const std::u8string u = p.generic_u8string();
  return std::string(u.begin(), u.end());
}

fs::path pathFromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool isPlaylistPath(const fs::path& p) {
  std::string ext = p.extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".m3u8" || ext == ".m3u";
}

struct PlaylistEntry {
  fs::path path;
  std::string name;
  bool loop = false;
  double still = -1.0;
};

std::vector<PlaylistEntry> readPlaylist(const fs::path& file, const Report& report) {
  std::vector<PlaylistEntry> out;
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    report("cannot open " + file.string());
    return out;
  }
  PlaylistEntry pending;
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (first && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line.erase(0, 3);  // BOM
    first = false;
    if (line.empty()) continue;
    if (line.rfind("#EXTINF:", 0) == 0) {
      const std::size_t comma = line.find(',');
      if (comma != std::string::npos) pending.name = line.substr(comma + 1);
      continue;
    }
    if (line.rfind("#DECKBOY:", 0) == 0) {
      std::istringstream fields(line.substr(9));
      for (std::string kv; std::getline(fields, kv, ';');) {
        const std::size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = kv.substr(0, eq), value = kv.substr(eq + 1);
        if (key == "loop") pending.loop = value == "1";
        else if (key == "still") pending.still = std::atof(value.c_str());
      }
      continue;
    }
    if (line[0] == '#') continue;
    fs::path p = pathFromUtf8(line);
    if (p.is_relative()) p = file.parent_path() / p;
    pending.path = p.lexically_normal();
    out.push_back(pending);
    pending = PlaylistEntry {};
  }
  return out;
}

bool writePlaylist(const fs::path& file, const std::vector<Cue>& cues) {
  const fs::path dir = fs::absolute(file).parent_path();
  const fs::path temp = file.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "#EXTM3U\n";
    for (const Cue& c : cues) {
      out << "#EXTINF:" << (c.duration > 0.0 ? c.duration : -1.0) << "," << c.name << "\n";
      if (c.loop || c.kind == CueKind::Image) {
        out << "#DECKBOY:loop=" << (c.loop ? 1 : 0);
        if (c.kind == CueKind::Image) out << ";still=" << c.stillDurationSeconds;
        out << "\n";
      }
      fs::path p = fs::path(c.path);
      const fs::path rel = p.lexically_relative(dir);
      const bool inside = !rel.empty() && rel.native().find(fs::path("..").native()) != 0;
      out << utf8Of(inside ? rel : p) << "\n";
    }
    out.close();
    if (!out) return false;
  }
  std::error_code ec;
  fs::rename(temp, file, ec);
  if (ec) {
    fs::remove(file, ec);
    fs::rename(temp, file, ec);
  }
  return !ec;
}

// Inputs with any playlists opened up into their entries, and the playlist
// settings kept with each, so cuesFor treats the lot alike.
std::vector<Cue> cuesForWithPlaylists(const std::vector<fs::path>& inputs, double stillSeconds,
                                      std::size_t firstId, const Report& report = toStderr) {
  std::vector<Cue> all;
  for (const fs::path& in : inputs) {
    if (isPlaylistPath(in)) {
      for (const PlaylistEntry& e : readPlaylist(in, report)) {
        std::vector<Cue> one = cuesFor({e.path}, e.still > 0.0 ? e.still : stillSeconds,
                                       firstId + all.size(), report);
        for (Cue& c : one) {
          if (!e.name.empty()) c.name = e.name;
          c.loop = e.loop;
          all.push_back(std::move(c));
        }
      }
    } else {
      std::vector<Cue> more = cuesFor({in}, stillSeconds, firstId + all.size(), report);
      for (Cue& c : more) all.push_back(std::move(c));
    }
  }
  return all;
}

std::vector<Cue> buildPlaylist(const Options& o) { return cuesForWithPlaylists(o.inputs, o.stillSeconds, 1); }

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
    if (keys_.begin()) hud_.log("keys live in this terminal  (? for help)");
    take(0, !opt_.paused);
    while (!gQuit.load()) {
      pumpEvents();
      while (auto key = keys_.poll()) onKey(*key, true);
      pollRemote();
      {
        // update() is where a new frame is uploaded to the GPU, so it is
        // counted with drawing as the cost of putting a frame on screen.
        const auto updateStarted = std::chrono::steady_clock::now();
        engine_->update();
        deckboy::libav::stageTimings().drawNs += static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - updateStarted).count());
      }
      collectEmbeddedSubtitles();
      // A-B LOOP: at B, back to A. Checked before the end, so a B at the very
      // end of the clip still loops rather than advancing.
      if (abA_ >= 0.0 && abB_ > abA_ && active_ >= 0 && engine_->position() >= abB_) {
        engine_->seek(abA_);
      } else if (engine_->reachedEnd()) {
        cueEnded();
      }
      draw();
      hud_.frame(hudState());
    }
    keys_.end();
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
    soundName_ = audio_ ? "default" : "";

    engine_ = std::make_unique<MediaEngine>(renderer_, audio_);
    // Mini draws only what its engine renders, so it can show a decoder's
    // frames straight from the decoder's buffers where the platform allows --
    // the difference between keeping up and not on a Raspberry Pi. OPT-IN
    // (DECKBOY_ZERO_COPY=1) until proven on a real display: the first real-
    // display run on a Pi 3 coincided with the Pi locking up, cause not yet known.
    engine_->setZeroCopyImport(zeroCopyRequested());
    engine_->setVolume(static_cast<float>(opt_.volume) / 100.0f);

    listen_ = createBoundSocket(SOCK_STREAM, opt_.port, true, !opt_.remote);
    if (listen_ == kInvalidSocket) {
      std::cerr << "deckboy-mini: port " << opt_.port
                << " is taken (is the desk running?). Remote control is off; use --port to pick another.\n";
    }
    return true;
  }

  void shutdown() {
    if (subTex_) SDL_DestroyTexture(subTex_);
    if (subFont_) TTF_CloseFont(subFont_);
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

  // followSelection false is for the list running itself (auto-advance): the
  // selection stays where the operator put it unless they were sitting on the
  // cue that just ended -- picking cue 7 to edit must not jump to cue 4 under
  // the next keypress because cue 3 finished.
  void take(int index, bool autoplay = true, bool followSelection = true) {
    if (!validIndex(index)) return;
    if (followSelection || selected_ == active_ || active_ < 0) selected_ = index;
    active_ = index;
    atEnd_ = false;
    stopped_ = false;
    abA_ = abB_ = -1.0;                // A-B belongs to the cue it was set on
    cues_[static_cast<std::size_t>(index)].playbackSpeed = speed_;
    loadSubtitlesFor(cues_[static_cast<std::size_t>(index)]);
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
    if (validIndex(next)) { take(next, true, false); return; }
    if (opt_.loop && !cues_.empty()) { take(0, true, false); return; }
    // End of the list: hold the frame or go to black, and stay put.
    engine_->finalizeReachedEnd(opt_.hold);
    atEnd_ = opt_.hold;
    if (!opt_.hold) stop();
    hud_.log(opt_.hold ? "end of list, holding the last frame" : "end of list");
  }

  void go() {
    // At the end of a held list the cue on screen has finished: GO takes the
    // picked cue rather than "playing" one with nothing left to play.
    if (active_ < 0 || atEnd_) { take(selected_); return; }
    if ((engine_->state() == TransportState::Playing)) engine_->pause(); else engine_->play();
  }

  // ── Editing the list ──
  //
  // Every edit keeps the cue on air on air: indices move with the cues, and
  // the engine plays its own copy of the cue, so nothing reloads.

  void markDirty() { listDirty_ = true; }

  bool moveCue(int from, int to) {
    if (!validIndex(from) || !validIndex(to) || from == to) return false;
    Cue moving = std::move(cues_[static_cast<std::size_t>(from)]);
    cues_.erase(cues_.begin() + from);
    cues_.insert(cues_.begin() + to, std::move(moving));
    auto follow = [&](int& i) {
      if (i == from) i = to;
      else if (from < to && i > from && i <= to) --i;
      else if (from > to && i >= to && i < from) ++i;
    };
    follow(active_);
    follow(selected_);
    markDirty();
    return true;
  }

  bool removeCue(int i) {
    if (!validIndex(i)) return false;
    const std::string name = cues_[static_cast<std::size_t>(i)].name;
    if (i == active_) stop();
    cues_.erase(cues_.begin() + i);
    if (active_ > i) --active_;
    if (selected_ >= static_cast<int>(cues_.size())) selected_ = std::max(0, static_cast<int>(cues_.size()) - 1);
    hud_.log("removed " + name);
    markDirty();
    return true;
  }

  void setCueLoop(int i, bool on) {
    if (!validIndex(i)) return;
    cues_[static_cast<std::size_t>(i)].loop = on;
    // The cue on air plays a copy; give it the change now, not at the next take.
    if (i == active_) engine_->syncActiveCueSnapshot(cues_[static_cast<std::size_t>(i)]);
    hud_.log(std::string("cue ") + std::to_string(i + 1) + (on ? " loops" : " plays once"));
    markDirty();
  }

  // Where a first save goes: beside the media, which is where a show folder
  // is -- not Mini's working folder, which for an installed copy is the
  // program's own and not writable.
  fs::path defaultSavePath() const {
    if (!listFile_.empty()) return listFile_;
    if (!cues_.empty()) return fs::path(cues_.front().path).parent_path() / "show.m3u8";
    return fs::current_path() / "show.m3u8";
  }

  bool savePlaylist(fs::path file) {
    if (file.extension().empty()) file += ".m3u8";
    if (!writePlaylist(file, cues_)) {
      hud_.log("could not save " + file.string());
      return false;
    }
    listFile_ = fs::absolute(file);
    listDirty_ = false;
    hud_.log("saved " + std::to_string(cues_.size()) + " cues to " + file.filename().string());
    return true;
  }

  bool openPlaylist(const fs::path& file) {
    std::vector<Cue> fresh = cuesForWithPlaylists({file}, opt_.stillSeconds, nextId_,
                                                  [this](const std::string& t) { hud_.log(t); });
    if (fresh.empty()) {
      hud_.log("nothing playable in " + file.string());
      return false;
    }
    stop();
    nextId_ += fresh.size();
    cues_ = std::move(fresh);
    selected_ = 0;
    listFile_ = isPlaylistPath(file) ? fs::absolute(file) : fs::path();
    listDirty_ = !isPlaylistPath(file);
    hud_.log("opened " + file.filename().string() + ", " + std::to_string(cues_.size()) + " cues");
    return true;
  }

  // ── The things mpv does to a playing file ──

  void applyVolume() { engine_->setVolume(muted_ ? 0.0f : static_cast<float>(opt_.volume) / 100.0f); }

  void setMuted(bool on) {
    muted_ = on;
    applyVolume();
    hud_.log(on ? "muted" : "sound on");
  }

  // Speed carries to every cue, as in mpv. The engine sets a cue's speed when
  // it loads it, so a change reloads the live cue where it stands.
  void setSpeed(double v) {
    speed_ = std::clamp(std::round(v * 100.0) / 100.0, 0.25, 4.0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "speed x%.2f", speed_);
    hud_.log(buf);
    if (active_ < 0) return;
    Cue& c = cues_[static_cast<std::size_t>(active_)];
    if (std::abs(c.playbackSpeed - speed_) < 1e-6) return;
    const double at = engine_->position();
    const bool playing = engine_->state() == TransportState::Playing;
    c.playbackSpeed = speed_;
    engine_->loadCue(&c, playing);
    if (at > 0.0) engine_->seek(at);
  }

  // One frame, paused -- the way mpv's , and . step.
  void frameStep(int direction) {
    if (active_ < 0) return;
    engine_->pause();
    const Cue& c = cues_[static_cast<std::size_t>(active_)];
    const double fps = c.fps > 1.0 ? c.fps : 25.0;
    engine_->seek(std::max(0.0, engine_->position() + direction / fps));
  }

  // K: A, then B, then off -- mpv's l.
  void abStep() {
    if (active_ < 0) return;
    const double now = engine_->position();
    if (abA_ < 0.0) {
      abA_ = now;
      hud_.log("A-B: A at " + clock(abA_) + "  (K again sets B)");
    } else if (abB_ < 0.0 && now > abA_) {
      abB_ = now;
      hud_.log("A-B loop " + clock(abA_) + " - " + clock(abB_) + "  (K clears)");
      engine_->seek(abA_);
    } else {
      abA_ = abB_ = -1.0;
      hud_.log("A-B loop off");
    }
  }

  // Subtitles beside the file: same name, any caption format Deckboy reads
  // (and name.en.srt style language suffixes), the way mpv finds them.
  void loadSubtitlesFor(const Cue& cue) {
    subTracks_.clear();
    subIndex_ = -1;
    const fs::path media(cue.path);
    std::error_code ec;
    if (cue.path.empty() || !fs::exists(media, ec)) return;
    const std::string stem = media.stem().string();
    std::vector<fs::path> found;
    for (const auto& entry : fs::directory_iterator(media.parent_path(), ec)) {
      if (!entry.is_regular_file(ec)) continue;
      const fs::path p = entry.path();
      if (p == media) continue;
      const std::string name = p.filename().string();
      if (name.rfind(stem + ".", 0) != 0) continue;
      if (deckboy::captions::formatForPath(p.string()) == deckboy::captions::Format::Unknown) continue;
      found.push_back(p);
    }
    std::sort(found.begin(), found.end());
    for (const fs::path& p : found) {
      deckboy::core::SubtitleTrack track;
      const auto format = deckboy::captions::formatForPath(p.string());
      if (format == deckboy::captions::Format::Srt) {
        track = deckboy::core::parseSrtFile(p.string());
      } else {
        std::ifstream in(p, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        track = deckboy::captions::parseText(text.str(), format);
      }
      if (!track.entries.empty()) subTracks_.emplace_back(p.filename().string(), std::move(track));
    }
    // And the ones inside the file, in the background.
    embeddedFor_ = cue.id;
    if (embeddedSubs_.valid()) parkedSubJobs_.push_back(std::move(embeddedSubs_));
    if (cue.subtitleTrackCount > 0) {
      const std::string path = cue.path;
      const int count = cue.subtitleTrackCount;
      embeddedSubs_ = std::async(std::launch::async, [path, count]() {
        std::vector<std::pair<std::string, deckboy::core::SubtitleTrack>> out;
        for (int i = 0; i < count; ++i) {
          const std::string srt = deckboy::core::media::extractEmbeddedSubtitleSrt(path, "0:s:" + std::to_string(i));
          auto track = deckboy::core::parseSrtText(srt);
          if (!track.entries.empty()) out.emplace_back("track " + std::to_string(i + 1) + " (in the file)", std::move(track));
        }
        return out;
      });
    }
    if (!subTracks_.empty() && subsWanted_) subIndex_ = 0;
    if (!subTracks_.empty()) {
      hud_.log("subtitles: " + subTracks_.front().first +
               (subTracks_.size() > 1 ? " (+" + std::to_string(subTracks_.size() - 1) + " more, J cycles)" : ""));
    }
  }

  // #: the next sound track of the live file -- mpv's #. Reloaded where it
  // stands, playing or paused as it was.
  void cycleAudioTrack() {
    if (active_ < 0) return;
    Cue& c = cues_[static_cast<std::size_t>(active_)];
    if (c.audioTrackCount <= 1) {
      hud_.log(c.audioTrackCount == 1 ? "this file has one sound track" : "this file has no sound track");
      return;
    }
    setAudioTrack((c.audioTrack + 1) % c.audioTrackCount);
  }

  void setAudioTrack(int track) {
    if (active_ < 0) return;
    Cue& c = cues_[static_cast<std::size_t>(active_)];
    const double at = engine_->position();
    const bool playing = engine_->state() == TransportState::Playing;
    c.audioTrack = track;
    engine_->loadCue(&c, playing);
    if (at > 0.0) engine_->seek(at);
    hud_.log("sound track " + std::to_string(track + 1) + " of " + std::to_string(std::max(1, c.audioTrackCount)));
  }

  void collectEmbeddedSubtitles() {
    parkedSubJobs_.erase(std::remove_if(parkedSubJobs_.begin(), parkedSubJobs_.end(), [](auto& f) {
      return !f.valid() || f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }), parkedSubJobs_.end());
    if (!embeddedSubs_.valid() ||
        embeddedSubs_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto found = embeddedSubs_.get();
    // Only for the cue they were read from: a take since then has its own.
    if (active_ < 0 || cues_[static_cast<std::size_t>(active_)].id != embeddedFor_ || found.empty()) return;
    const bool hadNone = subTracks_.empty();
    for (auto& t : found) subTracks_.push_back(std::move(t));
    if (hadNone && subsWanted_) subIndex_ = 0;
    hud_.log(std::to_string(found.size()) + " subtitle track(s) in the file" + (subTracks_.size() > 1 ? ", J cycles" : ""));
  }

  // J: through the tracks, then off -- mpv's j and v in one key.
  void cycleSubtitles() {
    if (subTracks_.empty()) { hud_.log("no subtitles beside this file"); return; }
    subIndex_ = subIndex_ + 1 >= static_cast<int>(subTracks_.size()) ? -1 : subIndex_ + 1;
    subsWanted_ = subIndex_ >= 0;
    hud_.log(subIndex_ < 0 ? "subtitles off" : "subtitles: " + subTracks_[static_cast<std::size_t>(subIndex_)].first);
  }

  // Drawn over the picture, low and centred, white with a black edge so it
  // reads over anything. The font is the bundled Liberation Sans, sized to
  // the output.
  void drawSubtitles() {
    if (subIndex_ < 0 || active_ < 0 || subIndex_ >= static_cast<int>(subTracks_.size())) return;
    const auto* entry = subTracks_[static_cast<std::size_t>(subIndex_)].second.entryAtTime(engine_->position());
    if (!entry) return;
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(renderer_, &w, &h);
    const int px = std::max(14, h / 20);
    if (!subFont_ || subFontPx_ != px) {
      if (subFont_) TTF_CloseFont(subFont_);
      const fs::path font = deckboy::core::Paths::dataDir() / "fonts" / "LiberationSans-Regular.ttf";
      subFont_ = TTF_OpenFont(font.string().c_str(), static_cast<float>(px));
      subFontPx_ = px;
      subTexText_.clear();
      if (!subFont_) return;
      TTF_SetFontWrapAlignment(subFont_, TTF_HORIZONTAL_ALIGN_CENTER);   // subtitles centre each line
    }
    if (entry->text != subTexText_ || !subTex_) {
      if (subTex_) SDL_DestroyTexture(subTex_);
      subTex_ = nullptr;
      subTexText_ = entry->text;
      const SDL_Color white {255, 255, 255, 255};
      const SDL_Color black {0, 0, 0, 255};
      const int wrap = w * 9 / 10;
      TTF_SetFontOutline(subFont_, std::max(1, px / 12));
      SDL_Surface* edge = TTF_RenderText_Blended_Wrapped(subFont_, entry->text.c_str(), 0, black, wrap);
      TTF_SetFontOutline(subFont_, 0);
      SDL_Surface* face = TTF_RenderText_Blended_Wrapped(subFont_, entry->text.c_str(), 0, white, wrap);
      if (edge && face) {
        const int o = std::max(1, px / 12);
        SDL_Rect at {o, o, face->w, face->h};
        SDL_SetSurfaceBlendMode(face, SDL_BLENDMODE_BLEND);
        SDL_BlitSurface(face, nullptr, edge, &at);
        subTex_ = SDL_CreateTextureFromSurface(renderer_, edge);
      }
      if (edge) SDL_DestroySurface(edge);
      if (face) SDL_DestroySurface(face);
    }
    if (!subTex_) return;
    float tw = 0, th = 0;
    SDL_GetTextureSize(subTex_, &tw, &th);
    SDL_FRect dst {(w - tw) / 2.0f, h - th - h / 14.0f, tw, th};
    SDL_RenderTexture(renderer_, subTex_, nullptr, &dst);
  }

  // ── Displays and sound ──

  std::vector<SDL_DisplayID> displayList() const {
    int count = 0;
    std::vector<SDL_DisplayID> out;
    if (SDL_DisplayID* ids = SDL_GetDisplays(&count)) {
      out.assign(ids, ids + count);
      SDL_free(ids);
    }
    return out;
  }

  std::string displayLine(int n, SDL_DisplayID id) const {
    SDL_Rect b {};
    SDL_GetDisplayBounds(id, &b);
    const char* name = SDL_GetDisplayName(id);
    return std::to_string(n) + " " + (name ? name : "display") + " " + std::to_string(b.w) + "x" + std::to_string(b.h);
  }

  // Move the output to display n (1-based), keeping fullscreen or window.
  bool moveToDisplay(int n) {
    const std::vector<SDL_DisplayID> ids = displayList();
    if (n < 1 || n > static_cast<int>(ids.size())) return false;
    const SDL_DisplayID id = ids[static_cast<std::size_t>(n - 1)];
    const bool wasFullscreen = fullscreen_;
    if (wasFullscreen) {
      SDL_SetWindowFullscreen(window_, false);
      SDL_SyncWindow(window_);
    }
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
    SDL_SyncWindow(window_);
    if (wasFullscreen) setFullscreen(true);
    opt_.display = n;
    hud_.log("output on display " + displayLine(n, id));
    return true;
  }

  void setOutputOn(bool on) {
    outputOn_ = on;
    if (on) SDL_ShowWindow(window_); else SDL_HideWindow(window_);
    hud_.log(on ? "output on" : "output off (sound carries on)");
  }

  std::vector<std::pair<SDL_AudioDeviceID, std::string>> soundDevices() const {
    std::vector<std::pair<SDL_AudioDeviceID, std::string>> out;
    int count = 0;
    if (SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count)) {
      for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetAudioDeviceName(ids[i]);
        out.emplace_back(ids[i], name ? name : "device");
      }
      SDL_free(ids);
    }
    return out;
  }

  // A new sound device means a new engine: the stream is the engine's from
  // birth. The cue on air is reloaded where it was, playing or paused as it was.
  bool useSoundDevice(SDL_AudioDeviceID id, const std::string& name) {
    SDL_AudioSpec spec {};
    spec.freq = kAudioRate;
    spec.format = kAudioFormat;
    spec.channels = kAudioChannels;
    SDL_AudioStream* fresh = SDL_OpenAudioDeviceStream(id, &spec, nullptr, nullptr);
    if (!fresh) {
      hud_.log("cannot open " + name + ": " + SDL_GetError());
      return false;
    }
    const double at = active_ >= 0 ? engine_->position() : 0.0;
    const bool playing = active_ >= 0 && engine_->state() == TransportState::Playing;
    engine_.reset();
    if (audio_) SDL_DestroyAudioStream(audio_);
    audio_ = fresh;
    engine_ = std::make_unique<MediaEngine>(renderer_, audio_);
    engine_->setZeroCopyImport(zeroCopyRequested());
    applyVolume();
    if (active_ >= 0) {
      engine_->loadCue(&cues_[static_cast<std::size_t>(active_)], playing);
      if (at > 0.0) engine_->seek(at);
    }
    soundName_ = name;
    hud_.log("sound to " + name);
    return true;
  }

  void nextSoundDevice() {
    const auto devices = soundDevices();
    if (devices.empty()) { hud_.log("no sound devices"); return; }
    std::size_t at = 0;
    for (std::size_t i = 0; i < devices.size(); ++i) {
      if (devices[i].second == soundName_) at = (i + 1) % devices.size();
    }
    useSoundDevice(devices[at].first, devices[at].second);
  }

  // ── Output ──

  void draw() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    if (!blackout_ && active_ >= 0) {
      int w = 0, h = 0;
      SDL_GetRenderOutputSize(renderer_, &w, &h);
      const auto drawStarted = std::chrono::steady_clock::now();
      engine_->render(SDL_Rect {0, 0, w, h});
      deckboy::libav::StageTimings& timing = deckboy::libav::stageTimings();
      timing.drawNs += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - drawStarted).count());
      timing.draws += 1;
      if (const DecodedFrame* f = engine_->currentFrame(); f && f->index != lastShownIndex_) {
        // A jump of more than a dozen is a seek or a loop, not a stutter.
        if (lastShownIndex_ != static_cast<std::uint64_t>(-1) && f->index > lastShownIndex_ &&
            f->index - lastShownIndex_ <= 12) {
          framesSkipped_ += f->index - lastShownIndex_ - 1;
        }
        lastShownIndex_ = f->index;
        ++framesShown_;
      }
    }
    if (!blackout_) drawSubtitles();
    if (overlay_) drawOverlay();
    // OUTSNAP (a test verb, as on the desk): this frame as it leaves.
    if (!snapPath_.empty()) {
      if (SDL_Surface* shot = SDL_RenderReadPixels(renderer_, nullptr)) {
        SDL_SaveBMP(shot, snapPath_.c_str());
        SDL_DestroySurface(shot);
      }
      snapPath_.clear();
    }
    SDL_RenderPresent(renderer_);
  }

  void pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) gQuit = true;
      if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) addInputs({fs::path(e.drop.data)}, "dropped");
      if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) continue;
      if (auto key = windowKey(e.key.key, e.key.mod)) onKey(*key, false);
    }
  }

  // ── Keys ──
  //
  // ONE SET OF HANDLERS for the terminal and the output window. The terminal
  // is the whole workflow -- typing a cue number, the command line, adding
  // files -- because it is the one place Mini can always be reached from: a
  // second display, or an SSH session on a box with no keyboard of its own.

  static std::optional<mini::Key> windowKey(SDL_Keycode k, SDL_Keymod mod = SDL_KMOD_NONE) {
    using mini::Key;
    switch (k) {
      case SDLK_UP: return Key {Key::Up};
      case SDLK_DOWN: return Key {Key::Down};
      case SDLK_LEFT: return Key {Key::Left};
      case SDLK_RIGHT: return Key {Key::Right};
      case SDLK_RETURN: case SDLK_KP_ENTER: return Key {Key::Enter};
      case SDLK_ESCAPE: return Key {Key::Escape};
      case SDLK_BACKSPACE: return Key {Key::Backspace};
      case SDLK_TAB: return Key {Key::Char, 'h'};  // the output bar, as before
      case SDLK_SLASH: return Key {Key::Char, '?'};
      default: break;
    }
    // SDL reports the unshifted key. Shift+L (loop this cue) has to arrive as
    // L, or the window's Shift+L looped the whole list instead.
    if (k >= 'a' && k <= 'z' && (mod & SDL_KMOD_SHIFT)) return Key {Key::Char, static_cast<char>(k - 'a' + 'A')};
    if (k >= 32 && k < 127) return Key {Key::Char, static_cast<char>(k)};
    return std::nullopt;
  }

  void onKey(const mini::Key& key, bool fromTerminal) {
    using mini::Key;
    if (command_) { commandKey(key); return; }
    const bool quitWasPending = quitPending();
    quitArmedAt_ = {};
    const bool removeWasPending = removePending() && key.kind == Key::Char &&
                                  std::tolower(static_cast<unsigned char>(key.ch)) == 'x';
    removeArmedAt_ = {};
    if (key.kind == Key::Char && key.ch >= '0' && key.ch <= '9') {
      if (number_.size() < 5) number_ += key.ch;
      return;
    }
    switch (key.kind) {
      case Key::Enter:
        if (!number_.empty()) {
          const int n = std::atoi(number_.c_str());
          number_.clear();
          if (validIndex(n - 1)) take(n - 1);
          else hud_.log("no cue " + std::to_string(n) + " (" + std::to_string(cues_.size()) + " cues)");
        } else {
          take(selected_);
        }
        return;
      case Key::Escape: number_.clear(); help_ = false; helpPage_ = 0; return;
      case Key::Backspace: if (!number_.empty()) number_.pop_back(); return;
      case Key::Up: selected_ = std::max(0, selected_ - 1); return;
      case Key::Down: selected_ = std::min(static_cast<int>(cues_.size()) - 1, selected_ + 1); return;
      // From where the show IS: the live cue, or after a stop the cue that was
      // picked. At either end it says so -- it used to re-take the last cue,
      // restarting what was on air.
      case Key::Right: case Key::Left: {
        const int from = active_ >= 0 ? active_ : selected_;
        const int to = from + (key.kind == Key::Right ? 1 : -1);
        if (validIndex(to)) take(to);
        else hud_.log(key.kind == Key::Right ? "already the last cue" : "already the first cue");
        return;
      }
      case Key::Tab: overlay_ = !overlay_; return;
      case Key::Char: break;
    }
    number_.clear();
    // KEYS THAT NEED TYPING say so in the output window instead of doing
    // nothing: rename, still time, save, open and the command line all open
    // a prompt, and the prompt lives in the terminal.
    if (!fromTerminal && key.ch != 0 && std::strchr("rtwoa:", std::tolower(static_cast<unsigned char>(key.ch)))) {
      hud_.log("that key opens a prompt: use the terminal Mini runs in");
      return;
    }
    // CASE MATTERS FOR ONE KEY: Shift+L loops the selected cue, l the list.
    if (key.ch == 'L') {
      if (validIndex(selected_)) setCueLoop(selected_, !cues_[static_cast<std::size_t>(selected_)].loop);
      return;
    }
    switch (std::tolower(static_cast<unsigned char>(key.ch))) {
      // -- editing the list (the terminal: it needs typing) --
      case '<':
        if (moveCue(selected_, selected_ - 1)) hud_.log("moved up to " + std::to_string(selected_ + 1));
        break;
      case '>':
        if (moveCue(selected_, selected_ + 1)) hud_.log("moved down to " + std::to_string(selected_ + 1));
        break;
      // -- what mpv does --
      case ',': frameStep(-1); break;   // a frame back, paused (mpv's ,)
      case '.': frameStep(1); break;    // a frame on, paused (mpv's .)
      case '{': seekBy(-1.0); break;    // a second back
      case '}': seekBy(1.0); break;     // a second on
      case '(': setSpeed(speed_ / 1.1); break;
      case ')': setSpeed(speed_ * 1.1); break;
      case 'm': setMuted(!muted_); break;
      case 'k': abStep(); break;
      case 'j': cycleSubtitles(); break;
      case '#': cycleAudioTrack(); break;
      case 'x':
        // TWICE, like Q: one stray X must not take a cue out of the show.
        if (removeWasPending && removeArmedIndex_ == selected_) removeCue(selected_);
        else if (validIndex(selected_)) {
          removeArmedAt_ = std::chrono::steady_clock::now();
          removeArmedIndex_ = selected_;
        }
        break;
      case 'r':
        if (fromTerminal && validIndex(selected_))
          openCommand("RENAME " + std::to_string(selected_ + 1) + " " + cues_[static_cast<std::size_t>(selected_)].name);
        break;
      case 't':
        if (fromTerminal && validIndex(selected_)) {
          char still[32];
          std::snprintf(still, sizeof(still), "%g", cues_[static_cast<std::size_t>(selected_)].stillDurationSeconds);
          openCommand("STILL " + std::to_string(selected_ + 1) + " " + still);
        }
        break;
      case 'w':
        if (fromTerminal) openCommand("SAVE " + defaultSavePath().string());
        break;
      case 'o':
        if (fromTerminal) openCommand("OPEN ");
        break;
      // -- output and sound --
      case 'd': {
        const int count = static_cast<int>(displayList().size());
        if (count > 0) moveToDisplay(opt_.display % count + 1);
        break;
      }
      case 'v': setOutputOn(!outputOn_); break;
      case 'p': nextSoundDevice(); break;
      case ' ': go(); break;
      case 's': stop(); hud_.log("stop"); break;
      case 'b': blackout_ = !blackout_; hud_.log(blackout_ ? "blackout on" : "blackout off"); break;
      case 'l': opt_.loop = !opt_.loop; hud_.log(opt_.loop ? "loop on" : "loop off"); break;
      case 'f': setFullscreen(!fullscreen_); break;
      case 'h': overlay_ = !overlay_; hud_.log(overlay_ ? "status bar on the output" : "status bar off"); break;
      case '+': case '=': setVolume(opt_.volume + 10); break;
      case '-': case '_': setVolume(opt_.volume - 10); break;
      case ']': seekBy(10.0); break;
      case '[': seekBy(-10.0); break;
      case '?':
        // Pages: show, editing, output -- then closed.
        if (!help_) { help_ = true; helpPage_ = 0; }
        else if (helpPage_ < 3) ++helpPage_;
        else { help_ = false; helpPage_ = 0; }
        break;
      case ':': if (fromTerminal) openCommand(""); break;
      case 'a': if (fromTerminal) openCommand("ADD "); else hud_.log("A works in the terminal; drop files here instead"); break;
      case 'q':
        // TWICE, because the output is live: one stray Q in the middle of a
        // show would take the picture off the wall.
        if (quitWasPending) gQuit = true;
        else { quitArmedAt_ = std::chrono::steady_clock::now(); hud_.log("press Q again to quit"); }
        break;
      default: break;
    }
  }

  bool removePending() const {
    return removeArmedAt_ != std::chrono::steady_clock::time_point {} &&
           std::chrono::steady_clock::now() - removeArmedAt_ < std::chrono::seconds(3);
  }

  bool quitPending() const {
    return quitArmedAt_ != std::chrono::steady_clock::time_point {} &&
           std::chrono::steady_clock::now() - quitArmedAt_ < std::chrono::seconds(3);
  }

  void setVolume(int v) {
    opt_.volume = std::clamp(v, 0, 100);
    applyVolume();
    hud_.log("volume " + std::to_string(opt_.volume));
  }

  void seekBy(double seconds) {
    if (active_ < 0) return;
    engine_->seek(std::max(0.0, engine_->position() + seconds));
    hud_.log(std::string("seek ") + (seconds > 0 ? "+" : "") + std::to_string(static_cast<int>(seconds)) + "s");
  }

  // ── The command line: any remote command, typed ──

  void openCommand(const std::string& start) {
    command_ = true;
    commandText_ = start;
    historyAt_ = static_cast<int>(history_.size());
    if (!hud_.fancy()) hud_.log("command: type it, Enter runs, Esc cancels");
  }

  void commandKey(const mini::Key& key) {
    using mini::Key;
    switch (key.kind) {
      case Key::Escape: command_ = false; commandText_.clear(); return;
      case Key::Backspace: if (!commandText_.empty()) commandText_.pop_back(); return;
      case Key::Tab: completePath(); return;
      case Key::DeleteWord: {
        // Ctrl+W, as in every shell: renaming no longer means backspacing the
        // old name a letter at a time.
        while (!commandText_.empty() && commandText_.back() == ' ') commandText_.pop_back();
        while (!commandText_.empty() && commandText_.back() != ' ') commandText_.pop_back();
        return;
      }
      case Key::Up:
        if (historyAt_ > 0) commandText_ = history_[static_cast<std::size_t>(--historyAt_)];
        return;
      case Key::Down:
        if (historyAt_ + 1 < static_cast<int>(history_.size())) commandText_ = history_[static_cast<std::size_t>(++historyAt_)];
        else { historyAt_ = static_cast<int>(history_.size()); commandText_.clear(); }
        return;
      case Key::Enter: {
        const std::string line = commandText_;
        command_ = false;
        commandText_.clear();
        if (line.find_first_not_of(' ') == std::string::npos) return;
        if (history_.empty() || history_.back() != line) history_.push_back(line);
        std::string reply = handle(line, false);
        // STATUS answers with the whole report; the panel already shows it.
        if (reply.rfind("DECKBOY", 0) == 0) reply = "OK STATUS (see the panel)";
        while (!reply.empty() && (reply.back() == '\n' || reply.back() == '\r')) reply.pop_back();
        hud_.log("> " + line + "   " + reply.substr(0, reply.find('\n')));
        return;
      }
      case Key::Char: commandText_ += key.ch; return;
      default: return;
    }
  }

  // Tab after ADD: complete the file or folder name from what is on disk. One
  // match is filled in; several are filled to what they share, and listed.
  void completePath() {
    const std::string upperText = upper(commandText_);
    std::size_t verbLen = 0;
    for (const char* v : {"ADD ", "OPEN ", "SAVE "}) {
      if (upperText.rfind(v, 0) == 0) verbLen = std::strlen(v);
    }
    if (verbLen == 0) return;
    std::string typed = commandText_.substr(verbLen);
    if (!typed.empty() && typed.front() == '"') typed.erase(0, 1);
    const fs::path partial(typed.empty() ? std::string(".") + static_cast<char>(fs::path::preferred_separator) : typed);
    const bool endsInSeparator = !typed.empty() && (typed.back() == '/' || typed.back() == '\\');
    const fs::path dir = endsInSeparator ? partial : (partial.has_parent_path() ? partial.parent_path() : fs::path("."));
    const std::string prefix = endsInSeparator ? std::string() : partial.filename().string();
    auto lower = [](std::string t) {
      for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return t;
    };
    std::vector<std::string> matches;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      std::string name = entry.path().filename().string();
      if (lower(name).rfind(lower(prefix), 0) != 0) continue;
      if (entry.is_directory(ec)) name += static_cast<char>(fs::path::preferred_separator);
      matches.push_back(name);
    }
    if (matches.empty()) { hud_.log("nothing matches " + (typed.empty() ? std::string("here") : typed)); return; }
    std::sort(matches.begin(), matches.end());
    std::string common = matches.front();
    for (const std::string& m : matches) {
      std::size_t n = 0;
      while (n < common.size() && n < m.size() && std::tolower(static_cast<unsigned char>(common[n])) ==
                                                     std::tolower(static_cast<unsigned char>(m[n]))) ++n;
      common.resize(n);
    }
    const std::string base = (endsInSeparator || partial.has_parent_path()) ? (dir / "").string() : std::string();
    commandText_ = commandText_.substr(0, verbLen) + base + common;
    if (matches.size() > 1) {
      std::string list;
      for (std::size_t i = 0; i < matches.size() && i < 4; ++i) list += (i ? "  " : "") + matches[i];
      if (matches.size() > 4) list += "  +" + std::to_string(matches.size() - 4) + " more";
      hud_.log(list);
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
  // fromNetwork: a command from the remote port is logged as "remote ..."; one
  // typed on Mini's own command line is logged with its reply by the caller.
  std::string handle(const std::string& raw, bool fromNetwork = true) {
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
    if (fromNetwork) hud_.log("remote " + raw);
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
    // The rest of the line after the verb (and an optional cue number): names
    // and paths keep their spaces.
    auto restAfter = [&](int words) {
      std::istringstream rest(raw);
      std::string skip;
      for (int i = 0; i < words && rest >> skip; ++i) {}
      std::string tail;
      std::getline(rest, tail);
      const std::size_t a = tail.find_first_not_of(" \t\"");
      tail = a == std::string::npos ? std::string() : tail.substr(a);
      while (!tail.empty() && (tail.back() == ' ' || tail.back() == '\t' || tail.back() == '"')) tail.pop_back();
      return tail;
    };
    // DECK 1 X arrives with the DECK words already stripped from parts, but
    // raw still carries them.
    const int deckWords = upper(raw).rfind("DECK", 0) == 0 ? 2 : 0;

    if (verb == "OUTSNAP") {
      snapPath_ = restAfter(deckWords + 1);
      return snapPath_.empty() ? err("OUTSNAP <file.bmp>") : ok();
    }
    if (verb == "SPEED") {
      double v = 0.0;
      try { v = std::stod(arg); } catch (...) { return err("SPEED 0.25-4"); }
      if (v < 0.25 || v > 4.0) return err("SPEED 0.25-4");
      setSpeed(v);
      return ok();
    }
    if (verb == "MUTE") {
      auto v = onOff(muted_);
      if (!v) return err("ON, OFF or TOGGLE");
      setMuted(*v);
      return ok();
    }
    if (verb == "FRAME") {
      if (active_ < 0) return err("nothing is playing");
      frameStep(upper(arg) == "BACK" || arg == "-1" ? -1 : 1);
      return ok();
    }
    if (verb == "ABLOOP") {
      if (active_ < 0) return err("nothing is playing");
      const std::string a = upper(arg);
      if (a == "OFF") { abA_ = abB_ = -1.0; return ok(); }
      if (parts.size() >= 3) {
        try { abA_ = std::stod(parts[1]); abB_ = std::stod(parts[2]); } catch (...) { return err("ABLOOP <a> <b> | OFF"); }
        if (abB_ <= abA_) { abA_ = abB_ = -1.0; return err("B must come after A"); }
        engine_->seek(abA_);
        return ok();
      }
      abStep();
      return ok();
    }
    if (verb == "AUDIOTRACK") {
      if (active_ < 0) return err("nothing is playing");
      const Cue& c = cues_[static_cast<std::size_t>(active_)];
      if (arg.empty() || upper(arg) == "NEXT") { cycleAudioTrack(); return ok(); }
      int n = 0;
      try { n = std::stoi(arg); } catch (...) { return err("AUDIOTRACK <n>|NEXT"); }
      if (n < 1 || n > std::max(1, c.audioTrackCount)) {
        return err("this file has " + std::to_string(c.audioTrackCount) + " sound track(s)");
      }
      setAudioTrack(n - 1);
      return ok();
    }
    if (verb == "SUBS" || verb == "SUBTITLES") {
      const std::string a = upper(arg);
      if (a == "OFF") { subIndex_ = -1; subsWanted_ = false; return ok(); }
      if (a == "ON") {
        if (subTracks_.empty()) return err("no subtitles beside this file");
        subIndex_ = std::max(0, subIndex_); subsWanted_ = true; return ok();
      }
      cycleSubtitles();
      return "OK SUBS: " + std::string(subIndex_ < 0 ? "off" : subTracks_[static_cast<std::size_t>(subIndex_)].first) + "\n";
    }
    if (verb == "MOVE") {
      auto from = cueArg();
      if (!from || parts.size() < 3) return err("MOVE <cue> <to>");
      int to = 0;
      try { to = std::stoi(parts[2]) - 1; } catch (...) { return err("MOVE <cue> <to>"); }
      if (!validIndex(to)) return err("no position " + parts[2]);
      moveCue(*from, to);
      return ok();
    }
    if (verb == "REMOVE" || verb == "DELETE") {
      auto n = cueArg();
      if (!n) return noCue();
      removeCue(*n);
      return ok();
    }
    if (verb == "RENAME") {
      auto n = cueArg();
      if (!n) return noCue();
      const std::string name = restAfter(deckWords + 2);
      if (name.empty()) return err("RENAME <cue> <name>");
      cues_[static_cast<std::size_t>(*n)].name = name;
      markDirty();
      return ok();
    }
    if (verb == "STILL") {
      auto n = cueArg();
      if (!n || parts.size() < 3) return err("STILL <cue> <seconds>");
      double secs = 0.0;
      try { secs = std::stod(parts[2]); } catch (...) { return err("STILL <cue> <seconds>"); }
      if (secs <= 0.0) return err("seconds must be more than 0");
      Cue& c = cues_[static_cast<std::size_t>(*n)];
      if (c.kind != CueKind::Image) return err("cue " + arg + " is not a still");
      c.stillDurationSeconds = secs;
      if (*n == active_) engine_->syncActiveCueSnapshot(c);
      markDirty();
      return ok();
    }
    if (verb == "CUELOOP") {
      auto n = cueArg();
      if (!n) return noCue();
      const std::string mode = parts.size() > 2 ? upper(parts[2]) : std::string("TOGGLE");
      const bool now = cues_[static_cast<std::size_t>(*n)].loop;
      if (mode != "ON" && mode != "OFF" && mode != "TOGGLE") return err("ON, OFF or TOGGLE");
      setCueLoop(*n, mode == "ON" ? true : mode == "OFF" ? false : !now);
      return ok();
    }
    if (verb == "SAVE") {
      std::string path = restAfter(deckWords + 1);
      if (path.empty()) {
        if (listFile_.empty()) return err("SAVE <file> (this list has not been saved yet)");
        path = listFile_.string();
      }
      return savePlaylist(fs::path(path)) ? ok() : err("could not write " + path);
    }
    if (verb == "OPEN") {
      const std::string path = restAfter(deckWords + 1);
      if (path.empty()) return err("OPEN <playlist, file or folder>");
      // UNSAVED EDITS ARE NOT THROWN AWAY ON ONE COMMAND: the same OPEN again
      // within ten seconds means it, and W saves first.
      const auto now = std::chrono::steady_clock::now();
      if (listDirty_ && !(openArmedPath_ == path && now - openArmedAt_ < std::chrono::seconds(10))) {
        openArmedPath_ = path;
        openArmedAt_ = now;
        return err("the list has unsaved changes: SAVE first, or OPEN the same file again to drop them");
      }
      openArmedPath_.clear();
      return openPlaylist(fs::path(path)) ? ok() : err("nothing playable at " + path);
    }
    if (verb == "DISPLAYS") {
      std::string out;
      int n = 1;
      for (SDL_DisplayID id : displayList()) {
        out += (n == opt_.display ? "* " : "  ") + displayLine(n, id) + "\n";
        ++n;
      }
      if (!fromNetwork) {
        std::istringstream lines(out);
        for (std::string l; std::getline(lines, l);) hud_.log(l);
      }
      return "OK DISPLAYS\n" + out;
    }
    if (verb == "DISPLAY") {
      const int count = static_cast<int>(displayList().size());
      int n = 0;
      if (upper(arg) == "NEXT") n = count > 0 ? opt_.display % count + 1 : 0;
      else { try { n = std::stoi(arg); } catch (...) { return err("DISPLAY <n> or NEXT"); } }
      if (!moveToDisplay(n)) return err("no display " + arg + " (" + std::to_string(count) + " found)");
      return ok();
    }
    if (verb == "OUTPUT") {
      auto v = onOff(outputOn_);
      if (!v) return err("ON, OFF or TOGGLE");
      setOutputOn(*v);
      return ok();
    }
    if (verb == "AUDIO" || verb == "SOUND") {
      const std::string a = upper(arg);
      const auto devices = soundDevices();
      if (a.empty() || a == "LIST") {
        std::string out;
        for (const auto& d : devices) out += (d.second == soundName_ ? "* " : "  ") + d.second + "\n";
        if (!fromNetwork) {
          std::istringstream lines(out);
          for (std::string l; std::getline(lines, l);) hud_.log(l);
        }
        return "OK AUDIO\n" + out;
      }
      if (a == "NEXT") { nextSoundDevice(); return ok(); }
      if (a == "DEFAULT") {
        return useSoundDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, "default") ? ok() : err("no default device");
      }
      // By name: exact first, then the first that contains what was typed.
      const std::string want = restAfter(deckWords + 1);
      for (int pass = 0; pass < 2; ++pass) {
        for (const auto& d : devices) {
          const bool hit = pass == 0 ? upper(d.second) == upper(want)
                                     : upper(d.second).find(upper(want)) != std::string::npos;
          if (hit) return useSoundDevice(d.first, d.second) ? ok() : err("could not open " + d.second);
        }
      }
      return err("no sound device called " + want + " (AUDIO LIST shows them)");
    }
    if (verb == "QUIT") { gQuit = true; return ok(); }
    return "ERR " + verb + ": unknown command (send HELP)\n";
  }

  // The desk's STATUS shape, trimmed to what one deck has.
  static bool zeroCopyRequested() {
    const char* env = std::getenv("DECKBOY_ZERO_COPY");
    return env && env[0] == '1';
  }

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
      << " vol=" << opt_.volume << " speed=" << speed_ << " mute=" << (muted_ ? "on" : "off")
      << " level=" << static_cast<int>(std::lround(engine_->programAudioLevel01() * 100.0))
      << " track=" << (live ? live->audioTrack + 1 : 0) << "/" << (live ? live->audioTrackCount : 0)
      << " ab=" << (abA_ >= 0.0 && abB_ > abA_ ? clock(abA_) + "-" + clock(abB_) : std::string("off"))
      << " subs=\"" << (subIndex_ < 0 ? std::string("off") : subTracks_[static_cast<std::size_t>(subIndex_)].first) << "\"\n";
    s << "OUTPUT 1 name=\"Output 1\" enabled=" << (blackout_ ? "off" : "on")
      << " health=live display=" << opt_.display << " raster=" << w << "x" << h
      << " fullscreen=" << (fullscreen_ ? "on" : "off") << " window=" << (outputOn_ ? "shown" : "hidden")
      << " frames_shown=" << framesShown_ << " frames_skipped=" << framesSkipped_
      // The file's own rate. frames_skipped only sees frames the engine threw
      // away; a decoder that delivers too few is visible only against this.
      << " media_fps=" << (live && live->fps > 0.0 ? std::lround(live->fps * 1000.0) / 1000.0 : 0.0)
      // Running totals, in microseconds: diff two readings to see which stage
      // a frame's time goes to (decode_us includes convert_us; draw_us is the
      // engine's render, which uploads the frame).
      << " decode_us=" << deckboy::libav::stageTimings().decodeNs.load() / 1000
      << " convert_us=" << deckboy::libav::stageTimings().convertNs.load() / 1000
      << " decoded=" << deckboy::libav::stageTimings().decoded.load()
      << " draw_us=" << deckboy::libav::stageTimings().drawNs.load() / 1000
      << " draws=" << deckboy::libav::stageTimings().draws.load()
      << " decode_fps=" << std::lround(engine_->mediaFpsMeasured() * 10.0) / 10.0
      << " sound=\"" << soundName_ << "\"\n";
    s << "LIST file=\"" << listFile_.string() << "\" dirty=" << (listDirty_ ? "yes" : "no") << " cues=" << cues_.size();
    for (std::size_t i = 0; i < cues_.size(); ++i) {
      s << (i == 0 ? " order=" : "|") << cues_[i].name << (cues_[i].loop ? "(loop)" : "");
    }
    s << "\n";
    return s.str();
  }

  // New cues go on the end of the list; nothing already playing is touched.
  // Shared by a file dropped on the output and the remote ADD.
  int addInputs(const std::vector<fs::path>& inputs, const char* how) {
    std::vector<Cue> more = cuesForWithPlaylists(inputs, opt_.stillSeconds, nextId_,
                                                 [this](const std::string& t) { hud_.log(t); });
    nextId_ += more.size();
    if (!more.empty()) listDirty_ = true;
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
    // The list around the selection, so a cue can be picked by eye.
    const int rows = 6;
    const int count = static_cast<int>(cues_.size());
    int first = std::clamp(selected_ - rows / 2, 0, std::max(0, count - rows));
    for (int i = first; i < std::min(count, first + rows); ++i) {
      s.rows.push_back({i + 1, cues_[static_cast<std::size_t>(i)].name, cues_[static_cast<std::size_t>(i)].duration,
                        i == active_, i == selected_, cues_[static_cast<std::size_t>(i)].loop});
    }
    s.keysLive = keys_.active();
    s.help = help_;
    s.helpPage = helpPage_;
    s.listName = listFile_.empty() ? std::string() : listFile_.filename().string();
    s.listDirty = listDirty_;
    s.outputOn = outputOn_;
    s.soundName = soundName_;
    if (command_) s.prompt = ": " + commandText_;
    else if (!number_.empty()) s.prompt = "go to cue " + number_ + "   Enter takes it, Esc clears";
    else if (quitPending()) s.prompt = listDirty_ ? "press Q again to quit (the list has unsaved changes: W saves)"
                                                  : "press Q again to quit";
    else if (removePending()) s.prompt = "press X again to remove cue " + std::to_string(removeArmedIndex_ + 1);
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
  mini::TerminalKeys keys_;
  std::string number_;                 // a cue number being typed
  bool command_ = false;               // the : command line is open
  std::string commandText_;
  std::vector<std::string> history_;
  int historyAt_ = 0;
  bool help_ = false;
  std::chrono::steady_clock::time_point quitArmedAt_ {};
  std::size_t nextId_ = 1;
  int helpPage_ = 0;
  std::string snapPath_;
  // -- what mpv does that a show player should too --
  bool muted_ = false;
  // Smoothness, as the output saw it: new frames put on screen, and frames the
  // clip had that never reached it (a gap in the display-order index). mpv's
  // frame-drop-count is the number to compare with on weak hardware.
  std::uint64_t framesShown_ = 0;
  std::uint64_t framesSkipped_ = 0;
  std::uint64_t lastShownIndex_ = static_cast<std::uint64_t>(-1);
  double speed_ = 1.0;               // for every cue, the way mpv's carries over
  double abA_ = -1.0;                  // A-B loop points on the live cue, seconds
  double abB_ = -1.0;
  // Subtitles: every sidecar found beside the live cue's file, and which one
  // is shown (-1 none). Found again at each take, the way mpv looks.
  std::vector<std::pair<std::string, deckboy::core::SubtitleTrack>> subTracks_;
  int subIndex_ = -1;
  bool subsWanted_ = true;             // the operator has not switched them off
  TTF_Font* subFont_ = nullptr;
  int subFontPx_ = 0;
  SDL_Texture* subTex_ = nullptr;
  std::string subTexText_;
  // Subtitles INSIDE the file are pulled out in the background: ffmpeg has to
  // read the whole file for them, which on a film takes seconds the picture
  // must not wait for. They join the list when they arrive.
  std::future<std::vector<std::pair<std::string, deckboy::core::SubtitleTrack>>> embeddedSubs_;
  std::string embeddedFor_;
  // A job still running when the next cue is taken is parked here, not waited
  // for: assigning over a std::async future blocks until it finishes.
  std::vector<std::future<std::vector<std::pair<std::string, deckboy::core::SubtitleTrack>>>> parkedSubJobs_;
  std::string openArmedPath_;
  std::chrono::steady_clock::time_point openArmedAt_ {};
  bool atEnd_ = false;                 // a held list has played its last cue
  fs::path listFile_;                  // where the list was opened from / saved to
  bool listDirty_ = false;
  bool outputOn_ = true;
  std::string soundName_;
  std::chrono::steady_clock::time_point removeArmedAt_ {};
  int removeArmedIndex_ = -1;
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
