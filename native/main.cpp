#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(PLAYBOY_HAS_NDI_SDK)
#include <Processing.NDI.Lib.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr int kControlWidth = 1440;
constexpr int kControlHeight = 900;
constexpr int kOutputWidth = 1280;
constexpr int kOutputHeight = 720;
constexpr int kSidebarWidth = 420;
constexpr int kRowHeight = 68;
constexpr int kPadding = 20;
constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;
constexpr Uint16 kAudioFormat = AUDIO_S16SYS;
constexpr size_t kMaxVideoFrames = 6;
constexpr Uint32 kShellOuterColor = 0xC9CFB3FFu;
constexpr Uint32 kShellInnerColor = 0xB0B98DFFu;
constexpr Uint32 kShellShadowColor = 0x7B8167FFu;
constexpr Uint32 kScreenLightColor = 0x9BBC0FFFu;
constexpr Uint32 kScreenMidColor = 0x8BAC0FFFu;
constexpr Uint32 kScreenDarkColor = 0x306230FFu;
constexpr Uint32 kScreenDeepColor = 0x0F380FFFu;
constexpr Uint32 kScreenInkSoftColor = 0x234A23FFu;
constexpr Uint32 kButtonBezelColor = 0x5E6954FFu;
constexpr Uint32 kDeleteBezelColor = 0x3B4B38FFu;
constexpr std::string_view kAppTitle = "Playboy_0.01";
constexpr std::string_view kOutputTitle = "Playboy_0.01 Output";
constexpr std::string_view kAppModelLabel = "model pb-001 / v0.01";

const fs::path kProjectRoot = "/home/user/playboy";
const fs::path kDataDir = kProjectRoot / "data";
const fs::path kStoreFile = kDataDir / "project.playboy";
const fs::path kFontSans = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
const fs::path kFontMono = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";

enum class CueKind {
  Video,
  Image,
  Pattern,
  Browser
};

enum class TransportState {
  Stopped,
  Paused,
  Playing
};

struct Cue {
  std::string path;
  std::string name;
  CueKind kind = CueKind::Video;
  double duration = 0.0;
  int width = 0;
  int height = 0;
  double fps = 30.0;
  std::string formatName;
  std::string videoCodec;
  std::string audioCodec;
  bool hasAudio = false;
  std::uintmax_t sizeBytes = 0;
  SDL_Color color {48, 98, 48, 255};
  double fadeInSeconds = 0.0;
  double fadeOutSeconds = 0.0;
  bool loop = false;
  bool pauseOnLastFrame = false;
};

struct Deck {
  std::string name = "Deck 1";
  std::vector<Cue> cues;
  int selectedIndex = -1;
  int activeIndex = -1;
  bool autoAdvance = false;
  bool playlistLoop = false;
  std::string audioOutputDeviceName;
  int outputDisplayIndex = 0;
  bool ndiEnabled = false;
  std::string ndiSourceName;
};

struct Project {
  std::string title = std::string(kAppTitle);
  std::vector<Deck> decks {Deck {}};
  int focusedDeckIndex = 0;
  bool advancedOutputMode = false;
  bool uiSoundsEnabled = true;
  bool uiTransitionsEnabled = true;
};

struct DecodedFrame {
  int width = 0;
  int height = 0;
  std::uint64_t index = 0;
  std::vector<std::uint8_t> pixels;
};

struct Button {
  std::string label;
  SDL_Rect rect {};
  SDL_Color fill {48, 40, 31, 255};
  SDL_Color outline {255, 255, 255, 20};
  SDL_Color text {245, 234, 215, 255};
};

struct DragState {
  bool active = false;
  int cueIndex = -1;
};

struct ToastState {
  bool active = false;
  Uint64 startedAt = 0;
  Uint32 durationMs = 1200;
  std::string message;
  SDL_Color fill {155, 188, 15, 220};
  SDL_Color ink {15, 56, 15, 255};
};

enum class UiSoundEffect {
  Navigate,
  Import,
  Take,
  Toggle,
  Stop,
  Clear,
  Delete
};

std::atomic<bool> gShouldQuit = false;

std::string trim(const std::string& value) {
  auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::stringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

std::vector<std::string> splitByChar(const std::string& text, char separator) {
  std::vector<std::string> parts;
  std::string current;
  for (char ch : text) {
    if (ch == separator) {
      parts.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  parts.push_back(current);
  return parts;
}

std::string formatSeconds(double seconds) {
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    return "00:00.0";
  }
  int wholeMinutes = static_cast<int>(seconds / 60.0);
  double remaining = seconds - wholeMinutes * 60.0;
  std::ostringstream output;
  output << std::setfill('0') << std::setw(2) << wholeMinutes << ':';
  output << std::fixed << std::setprecision(1) << std::setw(4) << remaining;
  return output.str();
}

std::string cueKindLabel(CueKind kind) {
  switch (kind) {
    case CueKind::Image:
      return "Still";
    case CueKind::Pattern:
      return "Pattern";
    case CueKind::Browser:
      return "Browser";
    case CueKind::Video:
    default:
      return "Video";
  }
}

std::string cueKindToken(CueKind kind) {
  switch (kind) {
    case CueKind::Image:
      return "image";
    case CueKind::Pattern:
      return "pattern";
    case CueKind::Browser:
      return "browser";
    case CueKind::Video:
    default:
      return "video";
  }
}

std::string transportLabel(TransportState state) {
  switch (state) {
    case TransportState::Playing:
      return "Playing";
    case TransportState::Paused:
      return "Paused";
    case TransportState::Stopped:
    default:
      return "Stopped";
  }
}

double easeOutCubic(double value) {
  double t = std::clamp(value, 0.0, 1.0);
  double inv = 1.0 - t;
  return 1.0 - inv * inv * inv;
}

SDL_Color parseColor(std::string_view input) {
  std::string value(input);
  if (value.size() != 7 || value[0] != '#') {
    return {48, 98, 48, 255};
  }
  auto fromHex = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') {
      return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
      return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
      return ch - 'A' + 10;
    }
    return -1;
  };

  auto readByte = [&](int offset) -> int {
    int hi = fromHex(value[offset]);
    int lo = fromHex(value[offset + 1]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    return hi * 16 + lo;
  };

  int r = readByte(1);
  int g = readByte(3);
  int b = readByte(5);
  if (r < 0 || g < 0 || b < 0) {
    return {48, 98, 48, 255};
  }
  return {static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), 255};
}

std::string colorToHex(SDL_Color color) {
  std::ostringstream output;
  output << '#'
         << std::hex << std::nouppercase << std::setfill('0')
         << std::setw(2) << static_cast<int>(color.r)
         << std::setw(2) << static_cast<int>(color.g)
         << std::setw(2) << static_cast<int>(color.b);
  return output.str();
}

Uint8 alpha(Uint32 rgba) {
  return static_cast<Uint8>(rgba & 0xFFu);
}

Uint8 blue(Uint32 rgba) {
  return static_cast<Uint8>((rgba >> 8) & 0xFFu);
}

Uint8 green(Uint32 rgba) {
  return static_cast<Uint8>((rgba >> 16) & 0xFFu);
}

Uint8 red(Uint32 rgba) {
  return static_cast<Uint8>((rgba >> 24) & 0xFFu);
}

SDL_Color colorFromRgba(Uint32 rgba) {
  return {red(rgba), green(rgba), blue(rgba), alpha(rgba)};
}

SDL_Rect insetRect(const SDL_Rect& rect, int amount) {
  return {
    rect.x + amount,
    rect.y + amount,
    std::max(0, rect.w - amount * 2),
    std::max(0, rect.h - amount * 2)
  };
}

void fillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(renderer, &rect);
}

void strokeRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color color) {
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawRect(renderer, &rect);
}

void drawFramedPanel(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color body, SDL_Color border, SDL_Color innerBorder) {
  fillRect(renderer, rect, body);
  strokeRect(renderer, rect, border);
  SDL_Rect inner = insetRect(rect, 2);
  if (inner.w > 0 && inner.h > 0) {
    strokeRect(renderer, inner, innerBorder);
  }
}

void drawSpeakerGrille(SDL_Renderer* renderer, int x, int y, int width, int bars, SDL_Color color) {
  for (int index = 0; index < bars; ++index) {
    SDL_Rect slot {x, y + index * 7, width, 3};
    fillRect(renderer, slot, color);
  }
}

bool pointInRect(int x, int y, const SDL_Rect& rect) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

#ifndef _WIN32
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

void closeSocket(SocketHandle socketHandle) {
  if (socketHandle >= 0) {
    close(socketHandle);
  }
}

SocketHandle createBoundSocket(int type, int port, bool shouldListen) {
  SocketHandle socketHandle = socket(AF_INET, type, 0);
  if (socketHandle < 0) {
    return kInvalidSocket;
  }

  int reuse = 1;
  setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<uint16_t>(port));

  if (bind(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    closeSocket(socketHandle);
    return kInvalidSocket;
  }

  if (shouldListen && listen(socketHandle, 8) != 0) {
    closeSocket(socketHandle);
    return kInvalidSocket;
  }

  return socketHandle;
}
#endif

std::vector<std::string> splitWhitespace(const std::string& text) {
  std::vector<std::string> parts;
  std::stringstream stream(text);
  std::string item;
  while (stream >> item) {
    parts.push_back(item);
  }
  return parts;
}

std::string toUpper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

std::string joinParts(const std::vector<std::string>& parts, size_t startIndex) {
  if (startIndex >= parts.size()) {
    return "";
  }
  std::string joined;
  for (size_t index = startIndex; index < parts.size(); ++index) {
    if (!joined.empty()) {
      joined += ' ';
    }
    joined += parts[index];
  }
  return joined;
}

std::string normalizeBrowserUrl(std::string value) {
  value = trim(value);
  if (value.empty()) {
    return value;
  }
  std::error_code error;
  fs::path localPath(value);
  if (fs::exists(localPath, error)) {
    return "file://" + fs::absolute(localPath, error).string();
  }
  if (value.find("://") != std::string::npos) {
    return value;
  }
  if (value.rfind("about:", 0) == 0 || value.rfind("file:", 0) == 0 || value.rfind("data:", 0) == 0) {
    return value;
  }
  return "https://" + value;
}

std::string browserCueNameForUrl(const std::string& url) {
  std::string value = normalizeBrowserUrl(url);
  size_t scheme = value.find("://");
  size_t begin = scheme == std::string::npos ? 0 : scheme + 3;
  size_t end = value.find('/', begin);
  std::string host = value.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
  if (host.empty()) {
    return "Browser Cue";
  }
  return "Browser: " + host;
}

#ifndef _WIN32
bool executableOnPath(const std::string& name) {
  if (name.empty()) {
    return false;
  }
  if (name.find('/') != std::string::npos) {
    return access(name.c_str(), X_OK) == 0;
  }

  const char* pathEnv = std::getenv("PATH");
  if (!pathEnv) {
    return false;
  }

  for (const auto& part : splitByChar(pathEnv, ':')) {
    if (part.empty()) {
      continue;
    }
    fs::path candidate = fs::path(part) / name;
    if (access(candidate.c_str(), X_OK) == 0) {
      return true;
    }
  }
  return false;
}
#endif

struct ChildProcess {
#ifndef _WIN32
  pid_t pid = -1;
  int readFd = -1;
  bool processGroup = false;
#endif

  bool running() const {
#ifndef _WIN32
    return pid > 0;
#else
    return false;
#endif
  }

  void stop() {
#ifndef _WIN32
    if (pid > 0) {
      pid_t target = pid;
      if (processGroup) {
        kill(-target, SIGTERM);
      } else {
        kill(target, SIGTERM);
      }
      int status = 0;
      waitpid(target, &status, 0);
      if (processGroup) {
        kill(-target, SIGKILL);
      }
      pid = -1;
      processGroup = false;
    }
    if (readFd >= 0) {
      close(readFd);
      readFd = -1;
    }
#endif
  }

  ~ChildProcess() {
    stop();
  }
};

#if defined(PLAYBOY_HAS_NDI_SDK)
struct NdiApi {
  void* libraryHandle = nullptr;
  bool loaded = false;
  bool attempted = false;
  std::string loadError = "not initialized";
  bool (*initializeFn)(void) = nullptr;
  void (*destroyFn)(void) = nullptr;
  NDIlib_send_instance_t (*sendCreateFn)(const NDIlib_send_create_t*) = nullptr;
  void (*sendDestroyFn)(NDIlib_send_instance_t) = nullptr;
  void (*sendVideoFn)(NDIlib_send_instance_t, const NDIlib_video_frame_v2_t*) = nullptr;
  int (*sendConnectionsFn)(NDIlib_send_instance_t, uint32_t) = nullptr;

  bool ensureLoaded() {
    if (attempted) {
      return loaded;
    }
    attempted = true;

    std::vector<std::string> candidates;
    if (const char* env = std::getenv("PLAYBOY_NDI_LIB"); env && *env) {
      candidates.emplace_back(env);
    }
#ifdef __APPLE__
    candidates.emplace_back("libndi.dylib");
#else
    candidates.emplace_back("libndi.so.6");
    candidates.emplace_back("libndi.so");
    candidates.emplace_back("/home/user/NDI SDK for Linux/lib/x86_64-linux-gnu/libndi.so.6.3.1");
    candidates.emplace_back("/usr/local/lib/libndi.so.6");
    candidates.emplace_back("/usr/lib/libndi.so.6");
#endif

    for (const auto& candidate : candidates) {
      if (candidate.empty()) {
        continue;
      }
      libraryHandle = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (libraryHandle) {
        break;
      }
    }

    if (!libraryHandle) {
      const char* error = dlerror();
      loadError = error ? error : "unable to load libndi";
      return false;
    }

    auto loadSymbol = [&](auto& target, const char* symbol) -> bool {
      target = reinterpret_cast<std::decay_t<decltype(target)>>(dlsym(libraryHandle, symbol));
      return target != nullptr;
    };

    if (!loadSymbol(initializeFn, "NDIlib_initialize") ||
        !loadSymbol(destroyFn, "NDIlib_destroy") ||
        !loadSymbol(sendCreateFn, "NDIlib_send_create") ||
        !loadSymbol(sendDestroyFn, "NDIlib_send_destroy") ||
        !loadSymbol(sendVideoFn, "NDIlib_send_send_video_v2") ||
        !loadSymbol(sendConnectionsFn, "NDIlib_send_get_no_connections")) {
      loadError = "missing NDI symbols in runtime";
      dlclose(libraryHandle);
      libraryHandle = nullptr;
      return false;
    }

    if (!initializeFn()) {
      loadError = "NDIlib_initialize failed";
      dlclose(libraryHandle);
      libraryHandle = nullptr;
      return false;
    }

    loaded = true;
    loadError.clear();
    return true;
  }

  void shutdown() {
    if (loaded && destroyFn) {
      destroyFn();
    }
    if (libraryHandle) {
      dlclose(libraryHandle);
    }
    libraryHandle = nullptr;
    loaded = false;
    attempted = false;
    loadError = "not initialized";
    initializeFn = nullptr;
    destroyFn = nullptr;
    sendCreateFn = nullptr;
    sendDestroyFn = nullptr;
    sendVideoFn = nullptr;
    sendConnectionsFn = nullptr;
  }
};
#endif

class MediaEngine;

struct DeckRuntime {
  SDL_Window* outputWindow = nullptr;
  SDL_Renderer* outputRenderer = nullptr;
  SDL_AudioDeviceID audioDevice = 0;
  std::unique_ptr<MediaEngine> mediaEngine;
  ChildProcess browserProcess;
  bool browserCueLive = false;
  fs::path browserProfileDir;
#if defined(PLAYBOY_HAS_NDI_SDK)
  NDIlib_send_instance_t ndiSender = nullptr;
  std::string ndiSenderName;
  std::vector<std::uint8_t> ndiFrameBuffer;
#endif
};

std::optional<std::string> readAllText(const std::vector<std::string>& args) {
#ifdef _WIN32
  (void) args;
  return std::nullopt;
#else
  if (args.empty()) {
    return std::nullopt;
  }

  int pipeFd[2];
  if (pipe(pipeFd) != 0) {
    return std::nullopt;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipeFd[0]);
    close(pipeFd[1]);
    return std::nullopt;
  }

  if (pid == 0) {
    dup2(pipeFd[1], STDOUT_FILENO);
    dup2(pipeFd[1], STDERR_FILENO);
    close(pipeFd[0]);
    close(pipeFd[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execvp(argv[0], argv.data());
    _exit(127);
  }

  close(pipeFd[1]);

  std::string output;
  std::array<char, 4096> buffer {};
  ssize_t bytesRead = 0;
  while ((bytesRead = read(pipeFd[0], buffer.data(), buffer.size())) > 0) {
    output.append(buffer.data(), static_cast<size_t>(bytesRead));
  }
  close(pipeFd[0]);

  int status = 0;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return std::nullopt;
  }
  return output;
#endif
}

bool spawnPipeProcess(ChildProcess& process, const std::vector<std::string>& args) {
#ifdef _WIN32
  (void) process;
  (void) args;
  return false;
#else
  process.stop();

  int pipeFd[2];
  if (pipe(pipeFd) != 0) {
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipeFd[0]);
    close(pipeFd[1]);
    return false;
  }

  if (pid == 0) {
    dup2(pipeFd[1], STDOUT_FILENO);
    int devNullFd = open("/dev/null", O_WRONLY);
    if (devNullFd >= 0) {
      dup2(devNullFd, STDERR_FILENO);
      close(devNullFd);
    }
    close(pipeFd[0]);
    close(pipeFd[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }

  close(pipeFd[1]);
  process.pid = pid;
  process.readFd = pipeFd[0];
  process.processGroup = false;
  return true;
#endif
}

bool spawnDetachedProcess(ChildProcess& process, const std::vector<std::string>& args) {
#ifdef _WIN32
  (void) process;
  (void) args;
  return false;
#else
  process.stop();
  if (args.empty()) {
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    return false;
  }

  if (pid == 0) {
    setsid();

    int devNullRead = open("/dev/null", O_RDONLY);
    if (devNullRead >= 0) {
      dup2(devNullRead, STDIN_FILENO);
      close(devNullRead);
    }

    int devNullWrite = open("/dev/null", O_WRONLY);
    if (devNullWrite >= 0) {
      dup2(devNullWrite, STDOUT_FILENO);
      dup2(devNullWrite, STDERR_FILENO);
      close(devNullWrite);
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execvp(argv[0], argv.data());
    _exit(127);
  }

  process.pid = pid;
  process.readFd = -1;
  process.processGroup = true;
  return true;
#endif
}

bool readExact(int fd, std::uint8_t* data, size_t size) {
#ifdef _WIN32
  (void) fd;
  (void) data;
  (void) size;
  return false;
#else
  size_t offset = 0;
  while (offset < size) {
    ssize_t bytes = read(fd, data + offset, size - offset);
    if (bytes <= 0) {
      return false;
    }
    offset += static_cast<size_t>(bytes);
  }
  return true;
#endif
}

std::string escapeField(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (ch == '\\' || ch == '\t' || ch == '\n') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

std::string unescapeField(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  bool escaping = false;
  for (char ch : value) {
    if (escaping) {
      out.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    out.push_back(ch);
  }
  return out;
}

std::vector<std::string> splitEscapedTabs(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  bool escaping = false;
  for (char ch : line) {
    if (escaping) {
      current.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    if (ch == '\t') {
      fields.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  fields.push_back(current);
  return fields;
}

std::string safeString(const std::vector<std::string>& fields, size_t index) {
  if (index >= fields.size()) {
    return "";
  }
  return unescapeField(fields[index]);
}

double safeDouble(const std::vector<std::string>& fields, size_t index, double fallback = 0.0) {
  if (index >= fields.size()) {
    return fallback;
  }
  try {
    return std::stod(fields[index]);
  } catch (...) {
    return fallback;
  }
}

int safeInt(const std::vector<std::string>& fields, size_t index, int fallback = 0) {
  if (index >= fields.size()) {
    return fallback;
  }
  try {
    return std::stoi(fields[index]);
  } catch (...) {
    return fallback;
  }
}

std::uintmax_t safeSize(const std::vector<std::string>& fields, size_t index, std::uintmax_t fallback = 0) {
  if (index >= fields.size()) {
    return fallback;
  }
  try {
    return static_cast<std::uintmax_t>(std::stoull(fields[index]));
  } catch (...) {
    return fallback;
  }
}

bool safeBool(const std::vector<std::string>& fields, size_t index, bool fallback = false) {
  if (index >= fields.size()) {
    return fallback;
  }
  return fields[index] == "1" || fields[index] == "true";
}

std::string escapeJson(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          std::ostringstream escaped;
          escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                  << static_cast<int>(static_cast<unsigned char>(ch));
          out += escaped.str();
        } else {
          out.push_back(ch);
        }
        break;
    }
  }
  return out;
}

std::string deckDefaultName(int index) {
  return "Deck " + std::to_string(index + 1);
}

std::string defaultNdiSourceName(const Deck& deck, int index) {
  std::string base = deck.name.empty() ? deckDefaultName(index) : deck.name;
  return "Playboy - " + base;
}

void normalizeDeck(Deck& deck, int index) {
  if (deck.name.empty()) {
    deck.name = deckDefaultName(index);
  }
  if (deck.cues.empty()) {
    deck.selectedIndex = -1;
    deck.activeIndex = -1;
  } else {
    deck.selectedIndex = std::clamp(deck.selectedIndex, 0, static_cast<int>(deck.cues.size()) - 1);
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      deck.activeIndex = -1;
    }
  }
  deck.outputDisplayIndex = std::max(0, deck.outputDisplayIndex);
  if (deck.ndiSourceName.empty()) {
    deck.ndiSourceName = defaultNdiSourceName(deck, index);
  }
}

void normalizeProject(Project& project) {
  if (project.decks.empty()) {
    project.decks.push_back(Deck {});
  }
  for (size_t index = 0; index < project.decks.size(); ++index) {
    normalizeDeck(project.decks[index], static_cast<int>(index));
  }
  project.focusedDeckIndex = std::clamp(project.focusedDeckIndex, 0, static_cast<int>(project.decks.size()) - 1);
  project.advancedOutputMode = project.advancedOutputMode || project.decks.size() > 1;
}

bool isImagePath(const fs::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  static const std::array<std::string, 9> kImageExts {
    ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", ".tif", ".tiff", ".avif"
  };
  return std::find(kImageExts.begin(), kImageExts.end(), ext) != kImageExts.end();
}

std::optional<double> parseFps(const std::string& rate) {
  auto slash = rate.find('/');
  if (slash == std::string::npos) {
    return std::nullopt;
  }
  try {
    double numerator = std::stod(rate.substr(0, slash));
    double denominator = std::stod(rate.substr(slash + 1));
    if (denominator == 0.0) {
      return std::nullopt;
    }
    return numerator / denominator;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<Cue> probeCue(const fs::path& mediaPath) {
  auto output = readAllText({
    "ffprobe",
    "-v",
    "error",
    "-show_entries",
    "format=duration,format_name,size:stream=codec_type,codec_name,width,height,r_frame_rate",
    "-of",
    "default=noprint_wrappers=1",
    mediaPath.string()
  });

  if (!output) {
    return std::nullopt;
  }

  Cue cue;
  cue.path = mediaPath.string();
  cue.name = mediaPath.stem().string();
  cue.kind = isImagePath(mediaPath) ? CueKind::Image : CueKind::Video;
  cue.fps = cue.kind == CueKind::Image ? 0.0 : 30.0;

  std::string lastCodecType;
  for (const auto& line : splitLines(*output)) {
    auto sep = line.find('=');
    if (sep == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, sep);
    std::string value = line.substr(sep + 1);

    if (key == "codec_type") {
      lastCodecType = value;
    } else if (key == "codec_name") {
      if (lastCodecType == "video" && cue.videoCodec.empty()) {
        cue.videoCodec = value;
      } else if (lastCodecType == "audio" && cue.audioCodec.empty()) {
        cue.audioCodec = value;
        cue.hasAudio = true;
      }
    } else if (key == "width" && cue.width == 0) {
      cue.width = std::max(0, std::atoi(value.c_str()));
    } else if (key == "height" && cue.height == 0) {
      cue.height = std::max(0, std::atoi(value.c_str()));
    } else if (key == "r_frame_rate" && cue.kind == CueKind::Video) {
      auto fps = parseFps(value);
      if (fps && *fps > 1.0) {
        cue.fps = *fps;
      }
    } else if (key == "duration" && cue.kind == CueKind::Video) {
      cue.duration = std::max(0.0, std::atof(value.c_str()));
    } else if (key == "format_name") {
      cue.formatName = value;
    } else if (key == "size") {
      cue.sizeBytes = static_cast<std::uintmax_t>(std::strtoull(value.c_str(), nullptr, 10));
    }
  }

  if (cue.width <= 0 || cue.height <= 0) {
    return std::nullopt;
  }
  if (cue.kind == CueKind::Video && cue.duration <= 0.0) {
    cue.duration = 0.0;
  }
  return cue;
}

bool saveProject(const fs::path& projectFile, const Project& project) {
  fs::path resolved = projectFile.empty() ? (kDataDir / "default.playboy") : projectFile;
  if (resolved.has_parent_path()) {
    fs::create_directories(resolved.parent_path());
  }
  std::ofstream output(resolved, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }

  output << "title\t" << escapeField(project.title) << '\n';
  output << "focused_deck\t" << project.focusedDeckIndex << '\n';
  output << "advanced_mode\t" << (project.advancedOutputMode ? 1 : 0) << '\n';
  output << "ui_sounds\t" << (project.uiSoundsEnabled ? 1 : 0) << '\n';
  output << "ui_transitions\t" << (project.uiTransitionsEnabled ? 1 : 0) << '\n';

  for (size_t deckIndex = 0; deckIndex < project.decks.size(); ++deckIndex) {
    const auto& deck = project.decks[deckIndex];
    output
      << "deck\t"
      << deckIndex << '\t'
      << escapeField(deck.name) << '\t'
      << deck.selectedIndex << '\t'
      << deck.activeIndex << '\t'
      << (deck.autoAdvance ? 1 : 0) << '\t'
      << (deck.playlistLoop ? 1 : 0) << '\t'
      << escapeField(deck.audioOutputDeviceName) << '\t'
      << deck.outputDisplayIndex << '\t'
      << (deck.ndiEnabled ? 1 : 0) << '\t'
      << escapeField(deck.ndiSourceName)
      << '\n';

    for (const auto& cue : deck.cues) {
      output
        << "cue\t"
        << deckIndex << '\t'
        << escapeField(cue.path) << '\t'
        << escapeField(cue.name) << '\t'
        << cueKindToken(cue.kind) << '\t'
        << cue.duration << '\t'
        << cue.width << '\t'
        << cue.height << '\t'
        << cue.fps << '\t'
        << escapeField(cue.formatName) << '\t'
        << escapeField(cue.videoCodec) << '\t'
        << escapeField(cue.audioCodec) << '\t'
        << (cue.hasAudio ? "1" : "0") << '\t'
        << cue.sizeBytes << '\t'
        << colorToHex(cue.color) << '\t'
        << cue.fadeInSeconds << '\t'
        << cue.fadeOutSeconds << '\t'
        << (cue.loop ? "1" : "0") << '\t'
        << (cue.pauseOnLastFrame ? "1" : "0")
        << '\n';
    }
  }

  return true;
}

Project loadProject(const fs::path& projectFile) {
  Project project;
  fs::path resolved = projectFile.empty() ? (kDataDir / "default.playboy") : projectFile;
  std::ifstream input(resolved, std::ios::binary);
  if (!input) {
    return project;
  }

  project.decks.clear();
  project.decks.push_back(Deck {});

  auto ensureDeck = [&](int deckIndex) -> Deck& {
    int normalizedIndex = std::max(0, deckIndex);
    while (normalizedIndex >= static_cast<int>(project.decks.size())) {
      Deck deck;
      deck.name = deckDefaultName(static_cast<int>(project.decks.size()));
      project.decks.push_back(deck);
    }
    return project.decks[normalizedIndex];
  };

  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    auto fields = splitEscapedTabs(line);
    if (fields.empty()) {
      continue;
    }

    if (fields[0] == "title") {
      project.title = safeString(fields, 1);
    } else if (fields[0] == "focused_deck") {
      project.focusedDeckIndex = safeInt(fields, 1, 0);
    } else if (fields[0] == "advanced_mode") {
      project.advancedOutputMode = safeBool(fields, 1, false);
    } else if (fields[0] == "selected") {
      ensureDeck(0).selectedIndex = safeInt(fields, 1, -1);
    } else if (fields[0] == "active") {
      ensureDeck(0).activeIndex = safeInt(fields, 1, -1);
    } else if (fields[0] == "auto_advance") {
      ensureDeck(0).autoAdvance = safeBool(fields, 1, false);
    } else if (fields[0] == "playlist_loop") {
      ensureDeck(0).playlistLoop = safeBool(fields, 1, false);
    } else if (fields[0] == "ui_sounds") {
      project.uiSoundsEnabled = safeBool(fields, 1, true);
    } else if (fields[0] == "ui_transitions") {
      project.uiTransitionsEnabled = safeBool(fields, 1, true);
    } else if (fields[0] == "audio_output") {
      ensureDeck(0).audioOutputDeviceName = safeString(fields, 1);
    } else if (fields[0] == "display_index") {
      ensureDeck(0).outputDisplayIndex = safeInt(fields, 1, 0);
    } else if (fields[0] == "ndi_enabled") {
      ensureDeck(0).ndiEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "ndi_name") {
      ensureDeck(0).ndiSourceName = safeString(fields, 1);
    } else if (fields[0] == "deck") {
      int deckIndex = safeInt(fields, 1, static_cast<int>(project.decks.size()) - 1);
      Deck& deck = ensureDeck(deckIndex);
      deck.name = safeString(fields, 2);
      deck.selectedIndex = safeInt(fields, 3, -1);
      deck.activeIndex = safeInt(fields, 4, -1);
      deck.autoAdvance = safeBool(fields, 5, false);
      deck.playlistLoop = safeBool(fields, 6, false);
      deck.audioOutputDeviceName = safeString(fields, 7);
      deck.outputDisplayIndex = safeInt(fields, 8, 0);
      deck.ndiEnabled = safeBool(fields, 9, false);
      deck.ndiSourceName = safeString(fields, 10);
    } else if (fields[0] == "cue") {
      int deckIndex = 0;
      size_t offset = 1;
      if (fields.size() >= 19) {
        try {
          deckIndex = std::stoi(fields[1]);
          offset = 2;
        } catch (...) {
          deckIndex = 0;
          offset = 1;
        }
      }

      Cue cue;
      cue.path = safeString(fields, offset + 0);
      cue.name = safeString(fields, offset + 1);
      std::string kind = safeString(fields, offset + 2);
      cue.kind =
        kind == "image" ? CueKind::Image :
        kind == "pattern" ? CueKind::Pattern :
        kind == "browser" ? CueKind::Browser :
        CueKind::Video;
      cue.duration = safeDouble(fields, offset + 3, 0.0);
      cue.width = safeInt(fields, offset + 4, 0);
      cue.height = safeInt(fields, offset + 5, 0);
      cue.fps = safeDouble(fields, offset + 6, cue.kind == CueKind::Video ? 30.0 : 0.0);
      cue.formatName = safeString(fields, offset + 7);
      cue.videoCodec = safeString(fields, offset + 8);
      cue.audioCodec = safeString(fields, offset + 9);
      cue.hasAudio = safeBool(fields, offset + 10, false);
      cue.sizeBytes = safeSize(fields, offset + 11, 0);
      cue.color = parseColor(safeString(fields, offset + 12));
      cue.fadeInSeconds = std::max(0.0, safeDouble(fields, offset + 13, 0.0));
      cue.fadeOutSeconds = std::max(0.0, safeDouble(fields, offset + 14, 0.0));
      cue.loop = safeBool(fields, offset + 15, false);
      cue.pauseOnLastFrame = safeBool(fields, offset + 16, false);
      if (!cue.path.empty()) {
        if (cue.name.empty()) {
          cue.name = fs::path(cue.path).stem().string();
        }
        ensureDeck(deckIndex).cues.push_back(cue);
      }
    }
  }

  normalizeProject(project);
  return project;
}

class MediaEngine {
 public:
  explicit MediaEngine(SDL_Renderer* outputRenderer, SDL_AudioDeviceID audioDevice)
    : outputRenderer_(outputRenderer), audioDevice_(audioDevice) {}

  ~MediaEngine() {
    stopAll();
  }

  void stopAll() {
    stopDecoderThreads();
    clearTexture();
    clearAudio();
    state_ = TransportState::Stopped;
    currentPosition_ = 0.0;
    duration_ = 0.0;
    activeCue_ = nullptr;
    frameRate_ = 0.0;
    lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
    displayFrame_.reset();
  }

  void loadCue(const Cue* cue, bool autoplay) {
    stopDecoderThreads();
    clearTexture();
    clearAudio();
    activeCue_ = cue;
    displayFrame_.reset();
    currentPosition_ = 0.0;
    pausedPosition_ = 0.0;
    playbackStartPosition_ = 0.0;
    duration_ = cue ? cue->duration : 0.0;
    frameRate_ = cue && cue->kind == CueKind::Video && cue->fps > 1.0 ? cue->fps : 30.0;
    lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
    decoderEof_ = false;
    reachedEnd_ = false;

    if (!cue) {
      state_ = TransportState::Stopped;
      return;
    }

    if (cue->kind == CueKind::Image) {
      loadStillFrame(*cue);
      state_ = TransportState::Paused;
      return;
    }

    if (cue->kind == CueKind::Pattern) {
      loadPatternFrame(*cue);
      state_ = TransportState::Paused;
      return;
    }

    if (cue->kind == CueKind::Browser) {
      state_ = TransportState::Paused;
      return;
    }

    startDecoderThreads(*cue, 0.0);
    state_ = autoplay ? TransportState::Playing : TransportState::Paused;
    playbackClockStart_ = std::chrono::steady_clock::now();
    playbackStartPosition_ = 0.0;
    pausedPosition_ = 0.0;
    SDL_PauseAudioDevice(audioDevice_, autoplay ? 0 : 1);
  }

  void play() {
    if (!activeCue_ || activeCue_->kind != CueKind::Video) {
      return;
    }
    if (state_ == TransportState::Playing) {
      return;
    }
    playbackClockStart_ = std::chrono::steady_clock::now();
    playbackStartPosition_ = pausedPosition_;
    state_ = TransportState::Playing;
    SDL_PauseAudioDevice(audioDevice_, 0);
  }

  void pause() {
    if (!activeCue_ || activeCue_->kind != CueKind::Video) {
      return;
    }
    if (state_ != TransportState::Playing) {
      state_ = TransportState::Paused;
      return;
    }
    pausedPosition_ = position();
    currentPosition_ = pausedPosition_;
    state_ = TransportState::Paused;
    SDL_PauseAudioDevice(audioDevice_, 1);
  }

  void toggle() {
    if (!activeCue_) {
      return;
    }
    if (activeCue_->kind != CueKind::Video) {
      return;
    }
    if (state_ == TransportState::Playing) {
      pause();
    } else {
      play();
    }
  }

  void stop() {
    if (!activeCue_) {
      return;
    }
    if (activeCue_->kind != CueKind::Video) {
      state_ = TransportState::Paused;
      pausedPosition_ = 0.0;
      currentPosition_ = 0.0;
      return;
    }
    seek(0.0);
    state_ = TransportState::Stopped;
    pausedPosition_ = 0.0;
    currentPosition_ = 0.0;
    SDL_PauseAudioDevice(audioDevice_, 1);
  }

  void clear() {
    stopAll();
  }

  void seek(double seconds) {
    if (!activeCue_) {
      return;
    }
    double clamped = std::clamp(seconds, 0.0, duration_);
    pausedPosition_ = clamped;
    currentPosition_ = clamped;
    playbackStartPosition_ = clamped;
    playbackClockStart_ = std::chrono::steady_clock::now();
    lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
    displayFrame_.reset();
    clearTexture();
    clearAudio();

    if (activeCue_->kind == CueKind::Image) {
      loadStillFrame(*activeCue_);
      state_ = TransportState::Paused;
      return;
    }
    if (activeCue_->kind == CueKind::Pattern) {
      loadPatternFrame(*activeCue_);
      state_ = TransportState::Paused;
      return;
    }
    if (activeCue_->kind == CueKind::Browser) {
      state_ = TransportState::Paused;
      return;
    }

    stopDecoderThreads();
    startDecoderThreads(*activeCue_, clamped);
    SDL_PauseAudioDevice(audioDevice_, state_ == TransportState::Playing ? 0 : 1);
  }

  void setVolume(float value) {
    volume_.store(std::clamp(value, 0.0f, 1.0f));
  }

  float volume() const {
    return volume_.load();
  }

  const Cue* activeCue() const {
    return activeCue_;
  }

  TransportState state() const {
    return state_;
  }

  double duration() const {
    return duration_;
  }

  double position() const {
    if (!activeCue_) {
      return 0.0;
    }
    if (state_ == TransportState::Playing && activeCue_->kind == CueKind::Video) {
      auto now = std::chrono::steady_clock::now();
      double elapsed = std::chrono::duration<double>(now - playbackClockStart_).count();
      return std::clamp(playbackStartPosition_ + elapsed, 0.0, duration_);
    }
    return currentPosition_;
  }

  bool reachedEnd() {
    if (reachedEnd_) {
      reachedEnd_ = false;
      return true;
    }
    return false;
  }

  void update() {
    if (!activeCue_) {
      return;
    }

    if (activeCue_->kind != CueKind::Video) {
      currentPosition_ = 0.0;
      return;
    }

    currentPosition_ = position();
    std::uint64_t targetFrame = static_cast<std::uint64_t>(std::floor(currentPosition_ * frameRate_));

    {
      std::lock_guard<std::mutex> lock(frameMutex_);
      while (!frameQueue_.empty() && frameQueue_.front().index <= targetFrame) {
        displayFrame_ = std::move(frameQueue_.front());
        frameQueue_.pop_front();
        lastRenderedFrameIndex_ = displayFrame_->index;
      }
    }

    if (displayFrame_) {
      uploadFrame(*displayFrame_);
    }

    if (state_ == TransportState::Playing && duration_ > 0.0 && currentPosition_ >= duration_ - 0.01) {
      handlePlaybackEnd();
    }

    if (state_ == TransportState::Playing && decoderEof_ && queuedFrames() == 0 && currentPosition_ >= duration_ - 0.02) {
      handlePlaybackEnd();
    }
  }

  void render(SDL_Rect target) {
    if (!texture_) {
      SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, 255);
      SDL_RenderFillRect(outputRenderer_, nullptr);
      return;
    }

    SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, 255);
    SDL_RenderFillRect(outputRenderer_, nullptr);

    int texW = 0;
    int texH = 0;
    SDL_QueryTexture(texture_, nullptr, nullptr, &texW, &texH);
    if (texW <= 0 || texH <= 0) {
      return;
    }

    double scale = std::min(
      static_cast<double>(target.w) / static_cast<double>(texW),
      static_cast<double>(target.h) / static_cast<double>(texH)
    );
    int drawW = std::max(1, static_cast<int>(std::round(texW * scale)));
    int drawH = std::max(1, static_cast<int>(std::round(texH * scale)));
    SDL_Rect destination {
      target.x + (target.w - drawW) / 2,
      target.y + (target.h - drawH) / 2,
      drawW,
      drawH
    };
    SDL_RenderCopy(outputRenderer_, texture_, nullptr, &destination);

    double gain = fadeGainAt(position());
    if (gain < 0.999) {
      SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, static_cast<Uint8>((1.0 - gain) * 255.0));
      SDL_RenderFillRect(outputRenderer_, nullptr);
    }
  }

 private:
  double fadeGainAt(double positionSeconds) const {
    if (!activeCue_) {
      return 1.0;
    }
    double gain = 1.0;
    if (activeCue_->fadeInSeconds > 0.001) {
      gain = std::min(gain, std::clamp(positionSeconds / activeCue_->fadeInSeconds, 0.0, 1.0));
    }
    if (activeCue_->fadeOutSeconds > 0.001 && duration_ > 0.0) {
      double remaining = std::max(0.0, duration_ - positionSeconds);
      gain = std::min(gain, std::clamp(remaining / activeCue_->fadeOutSeconds, 0.0, 1.0));
    }
    return std::clamp(gain, 0.0, 1.0);
  }

  void handlePlaybackEnd() {
    if (!activeCue_) {
      return;
    }
    if (activeCue_->loop) {
      bool wasPlaying = true;
      seek(0.0);
      if (wasPlaying) {
        state_ = TransportState::Playing;
        playbackClockStart_ = std::chrono::steady_clock::now();
        playbackStartPosition_ = 0.0;
        pausedPosition_ = 0.0;
        SDL_PauseAudioDevice(audioDevice_, 0);
      }
      return;
    }
    if (activeCue_->pauseOnLastFrame) {
      state_ = TransportState::Paused;
      pausedPosition_ = duration_;
      currentPosition_ = duration_;
      SDL_PauseAudioDevice(audioDevice_, 1);
      return;
    }
    state_ = TransportState::Stopped;
    pausedPosition_ = duration_;
    currentPosition_ = duration_;
    SDL_PauseAudioDevice(audioDevice_, 1);
    reachedEnd_ = true;
  }

  void clearTexture() {
    if (texture_) {
      SDL_DestroyTexture(texture_);
      texture_ = nullptr;
    }
  }

  void uploadFrame(const DecodedFrame& frame) {
    if (!texture_ || textureWidth_ != frame.width || textureHeight_ != frame.height) {
      clearTexture();
      texture_ = SDL_CreateTexture(
        outputRenderer_,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        frame.width,
        frame.height
      );
      textureWidth_ = frame.width;
      textureHeight_ = frame.height;
    }
    if (!texture_) {
      return;
    }
    SDL_UpdateTexture(texture_, nullptr, frame.pixels.data(), frame.width * 4);
  }

  void loadStillFrame(const Cue& cue) {
    auto frame = decodeSingleFrame(cue.path, cue.width, cue.height, 0.0);
    if (frame) {
      displayFrame_ = std::move(frame);
      uploadFrame(*displayFrame_);
    }
  }

  void loadPatternFrame(const Cue& cue) {
    auto frame = buildPatternFrame(cue);
    if (frame) {
      displayFrame_ = std::move(frame);
      uploadFrame(*displayFrame_);
    }
  }

  static void writePixel(DecodedFrame& frame, int x, int y, SDL_Color color) {
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
      return;
    }
    size_t offset = static_cast<size_t>(y * frame.width + x) * 4u;
    frame.pixels[offset + 0] = color.r;
    frame.pixels[offset + 1] = color.g;
    frame.pixels[offset + 2] = color.b;
    frame.pixels[offset + 3] = color.a;
  }

  static void fillPixelRect(DecodedFrame& frame, int x, int y, int w, int h, SDL_Color color) {
    for (int py = std::max(0, y); py < std::min(frame.height, y + h); ++py) {
      for (int px = std::max(0, x); px < std::min(frame.width, x + w); ++px) {
        writePixel(frame, px, py, color);
      }
    }
  }

  static void drawHeart(DecodedFrame& frame, int centerX, int centerY, int radius, SDL_Color color) {
    for (int y = -radius * 2; y <= radius * 2; ++y) {
      for (int x = -radius * 2; x <= radius * 2; ++x) {
        double fx = static_cast<double>(x) / static_cast<double>(radius);
        double fy = static_cast<double>(y) / static_cast<double>(radius);
        double equation = std::pow(fx * fx + fy * fy - 1.0, 3.0) - fx * fx * std::pow(fy, 3.0);
        if (equation <= 0.0) {
          writePixel(frame, centerX + x, centerY + y, color);
        }
      }
    }
  }

  static std::optional<DecodedFrame> buildPatternFrame(const Cue& cue) {
    DecodedFrame frame;
    frame.width = std::max(320, cue.width > 0 ? cue.width : 1280);
    frame.height = std::max(180, cue.height > 0 ? cue.height : 720);
    frame.index = 0;
    frame.pixels.resize(static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4u, 255);

    SDL_Color light {155, 188, 15, 255};
    SDL_Color mid {139, 172, 15, 255};
    SDL_Color dark {48, 98, 48, 255};
    SDL_Color deep {15, 56, 15, 255};

    fillPixelRect(frame, 0, 0, frame.width, frame.height, light);
    int stripeWidth = frame.width / 6;
    std::array<SDL_Color, 6> stripes {light, mid, dark, deep, mid, light};
    for (int index = 0; index < 6; ++index) {
      fillPixelRect(frame, index * stripeWidth, 0, stripeWidth + 1, frame.height / 3, stripes[index]);
    }

    for (int y = frame.height / 3; y < frame.height; y += 24) {
      for (int x = 0; x < frame.width; x += 24) {
        SDL_Color cell = ((x / 24) + (y / 24)) % 2 == 0 ? mid : light;
        fillPixelRect(frame, x, y, 24, 24, cell);
      }
    }

    fillPixelRect(frame, frame.width / 6, frame.height / 2 - 90, frame.width * 2 / 3, 180, light);
    fillPixelRect(frame, frame.width / 6 + 10, frame.height / 2 - 80, frame.width * 2 / 3 - 20, 160, mid);
    fillPixelRect(frame, frame.width / 6 + 24, frame.height / 2 - 64, frame.width * 2 / 3 - 48, 128, light);
    drawHeart(frame, frame.width / 2, frame.height / 2 + 16, 24, dark);
    drawHeart(frame, frame.width / 2 - 170, frame.height / 2 - 28, 14, deep);
    drawHeart(frame, frame.width / 2 + 170, frame.height / 2 - 28, 14, deep);

    fillPixelRect(frame, frame.width / 2 - 74, frame.height / 2 - 38, 18, 18, deep);
    fillPixelRect(frame, frame.width / 2 + 56, frame.height / 2 - 38, 18, 18, deep);
    fillPixelRect(frame, frame.width / 2 - 42, frame.height / 2 + 24, 84, 10, dark);
    fillPixelRect(frame, frame.width / 2 - 58, frame.height / 2 + 34, 116, 10, deep);

    return frame;
  }

  std::optional<DecodedFrame> decodeSingleFrame(const std::string& path, int width, int height, double seconds) {
    ChildProcess process;
    std::vector<std::string> args {
      "ffmpeg",
      "-hide_banner",
      "-loglevel",
      "error"
    };
    if (seconds > 0.0) {
      args.push_back("-ss");
      args.push_back(std::to_string(seconds));
    }
    args.insert(args.end(), {
      "-i",
      path,
      "-frames:v",
      "1",
      "-f",
      "rawvideo",
      "-pix_fmt",
      "rgba",
      "pipe:1"
    });

    if (!spawnPipeProcess(process, args)) {
      return std::nullopt;
    }

    const size_t frameBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    DecodedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.index = 0;
    frame.pixels.resize(frameBytes);
    bool ok = readExact(process.readFd, frame.pixels.data(), frameBytes);
    process.stop();
    if (!ok) {
      return std::nullopt;
    }
    return frame;
  }

  void clearAudio() {
    if (audioDevice_ != 0) {
      SDL_ClearQueuedAudio(audioDevice_);
      SDL_PauseAudioDevice(audioDevice_, 1);
    }
  }

  size_t queuedFrames() {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return frameQueue_.size();
  }

  void stopDecoderThreads() {
    decoderStop_.store(true);
    videoProcess_.stop();
    audioProcess_.stop();

    if (videoThread_.joinable()) {
      videoThread_.join();
    }
    if (audioThread_.joinable()) {
      audioThread_.join();
    }

    {
      std::lock_guard<std::mutex> lock(frameMutex_);
      frameQueue_.clear();
    }
    decoderStop_.store(false);
    decoderEof_ = false;
  }

  void startDecoderThreads(const Cue& cue, double startSeconds) {
    if (!spawnPipeProcess(videoProcess_, {
      "ffmpeg",
      "-hide_banner",
      "-loglevel",
      "error",
      "-ss",
      std::to_string(startSeconds),
      "-i",
      cue.path,
      "-an",
      "-f",
      "rawvideo",
      "-pix_fmt",
      "rgba",
      "pipe:1"
    })) {
      return;
    }

    const size_t frameBytes = static_cast<size_t>(cue.width) * static_cast<size_t>(cue.height) * 4u;
    videoThread_ = std::thread([this, cue, frameBytes]() {
      std::uint64_t frameIndex = static_cast<std::uint64_t>(std::floor(playbackStartPosition_ * frameRate_));
      while (!decoderStop_.load()) {
        while (!decoderStop_.load()) {
          bool hasRoom = false;
          {
            std::lock_guard<std::mutex> lock(frameMutex_);
            hasRoom = frameQueue_.size() < kMaxVideoFrames;
          }
          if (hasRoom) {
            break;
          }
          SDL_Delay(4);
        }
        if (decoderStop_.load()) {
          break;
        }

        DecodedFrame frame;
        frame.width = cue.width;
        frame.height = cue.height;
        frame.index = frameIndex++;
        frame.pixels.resize(frameBytes);

        if (!readExact(videoProcess_.readFd, frame.pixels.data(), frameBytes)) {
          decoderEof_ = true;
          break;
        }

        std::lock_guard<std::mutex> lock(frameMutex_);
        frameQueue_.push_back(std::move(frame));
      }
      decoderEof_ = true;
    });

    if (cue.hasAudio) {
      if (spawnPipeProcess(audioProcess_, {
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-ss",
        std::to_string(startSeconds),
        "-i",
        cue.path,
        "-vn",
        "-f",
        "s16le",
        "-acodec",
        "pcm_s16le",
        "-ac",
        "2",
        "-ar",
        "48000",
        "pipe:1"
      })) {
        audioThread_ = std::thread([this, startSeconds]() {
          std::vector<std::uint8_t> buffer(8192);
          double audioTime = startSeconds;
          while (!decoderStop_.load()) {
            if (SDL_GetQueuedAudioSize(audioDevice_) > 384000) {
              SDL_Delay(4);
              continue;
            }

            ssize_t bytesRead = read(audioProcess_.readFd, buffer.data(), buffer.size());
            if (bytesRead <= 0) {
              break;
            }

            std::vector<std::int16_t> scaled(static_cast<size_t>(bytesRead) / sizeof(std::int16_t));
            std::memcpy(scaled.data(), buffer.data(), static_cast<size_t>(bytesRead));
            for (size_t index = 0; index < scaled.size(); index += 2) {
              double gain = static_cast<double>(volume_.load()) * fadeGainAt(audioTime);
              for (size_t channel = 0; channel < 2 && index + channel < scaled.size(); ++channel) {
                auto& sample = scaled[index + channel];
                sample = static_cast<std::int16_t>(std::clamp(
                  static_cast<int>(std::lround(static_cast<double>(sample) * gain)),
                  -32768,
                  32767
                ));
              }
              audioTime += 1.0 / 48000.0;
            }
            SDL_QueueAudio(audioDevice_, scaled.data(), static_cast<Uint32>(scaled.size() * sizeof(std::int16_t)));
          }
        });
      }
    }
  }

  SDL_Renderer* outputRenderer_ = nullptr;
  SDL_AudioDeviceID audioDevice_ = 0;
  const Cue* activeCue_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  int textureWidth_ = 0;
  int textureHeight_ = 0;
  std::atomic<float> volume_ {1.0f};
  TransportState state_ = TransportState::Stopped;
  double currentPosition_ = 0.0;
  double pausedPosition_ = 0.0;
  double playbackStartPosition_ = 0.0;
  double duration_ = 0.0;
  double frameRate_ = 30.0;
  std::chrono::steady_clock::time_point playbackClockStart_ = std::chrono::steady_clock::now();
  std::optional<DecodedFrame> displayFrame_;
  std::uint64_t lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
  std::mutex frameMutex_;
  std::deque<DecodedFrame> frameQueue_;
  ChildProcess videoProcess_;
  ChildProcess audioProcess_;
  std::thread videoThread_;
  std::thread audioThread_;
  std::atomic<bool> decoderStop_ {false};
  std::atomic<bool> decoderEof_ {false};
  bool reachedEnd_ = false;
};

class App {
 public:
  bool init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
      std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
      return false;
    }
    if (TTF_Init() != 0) {
      std::cerr << "TTF_Init failed: " << TTF_GetError() << '\n';
      return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    controlWindow_ = SDL_CreateWindow(
      kAppTitle.data(),
      SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED,
      kControlWidth,
      kControlHeight,
      SDL_WINDOW_RESIZABLE
    );
    if (!controlWindow_) {
      std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
      return false;
    }

    controlRenderer_ = SDL_CreateRenderer(controlWindow_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!controlRenderer_) {
      std::cerr << "Renderer creation failed: " << SDL_GetError() << '\n';
      return false;
    }

    fontLarge_ = TTF_OpenFont(kFontSans.string().c_str(), 28);
    fontBase_ = TTF_OpenFont(kFontSans.string().c_str(), 18);
    fontSmall_ = TTF_OpenFont(kFontSans.string().c_str(), 15);
    fontMono_ = TTF_OpenFont(kFontMono.string().c_str(), 16);
    if (!fontLarge_ || !fontBase_ || !fontSmall_ || !fontMono_) {
      std::cerr << "Font load failed: " << TTF_GetError() << '\n';
      return false;
    }

    currentProjectFile_ = defaultProjectFile();
    project_ = loadProject(currentProjectFile_);
    normalizeProject(project_);
    ensureUiAudioDevice();
    if (!rebuildDeckRuntimes()) {
      std::cerr << "Deck runtime creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    selectionChangedAt_ = SDL_GetTicks64();
    startCompanionControl();
    layoutButtons(kControlHeight);
    return true;
  }

  void shutdown() {
    stopCompanionControl();
    for (auto& runtime : deckRuntimes_) {
      destroyDeckRuntime(runtime);
    }
    deckRuntimes_.clear();
#if defined(PLAYBOY_HAS_NDI_SDK)
    ndiApi_.shutdown();
#endif
    if (uiAudioDevice_ != 0) {
      SDL_CloseAudioDevice(uiAudioDevice_);
      uiAudioDevice_ = 0;
    }
    if (fontLarge_) {
      TTF_CloseFont(fontLarge_);
      fontLarge_ = nullptr;
    }
    if (fontBase_) {
      TTF_CloseFont(fontBase_);
      fontBase_ = nullptr;
    }
    if (fontSmall_) {
      TTF_CloseFont(fontSmall_);
      fontSmall_ = nullptr;
    }
    if (fontMono_) {
      TTF_CloseFont(fontMono_);
      fontMono_ = nullptr;
    }
    if (controlRenderer_) {
      SDL_DestroyRenderer(controlRenderer_);
      controlRenderer_ = nullptr;
    }
    if (controlWindow_) {
      SDL_DestroyWindow(controlWindow_);
      controlWindow_ = nullptr;
    }
    TTF_Quit();
    SDL_Quit();
  }

  void run() {
    while (!gShouldQuit.load()) {
      processEvents();
      update();
      render();
    }
  }

  static int runSelfCheck() {
    std::cout << "playboy-native self-check\n";
    std::cout << "project-root: " << kProjectRoot << '\n';
    std::cout << "font-sans: " << (fs::exists(kFontSans) ? "ok" : "missing") << '\n';
    std::cout << "font-mono: " << (fs::exists(kFontMono) ? "ok" : "missing") << '\n';
    std::cout << "ffmpeg: " << (readAllText({"ffmpeg", "-version"}).has_value() ? "ok" : "missing") << '\n';
    std::cout << "ffprobe: " << (readAllText({"ffprobe", "-version"}).has_value() ? "ok" : "missing") << '\n';
#if defined(PLAYBOY_HAS_NDI_SDK)
    std::cout << "ndi-sdk: headers detected (runtime loads when NDI is enabled)\n";
#else
    std::cout << "ndi-sdk: not built (set PLAYBOY_NDI_SDK or install SDK headers)\n";
#endif
    std::cout << "ui-sfx: enabled by separate SDL audio device when available\n";
    std::cout << "companion-control: tcp/udp port 5510 by default (override with PLAYBOY_COMPANION_PORT)\n";
    return 0;
  }

 private:
  Deck& focusedDeckMutable() {
    normalizeProject(project_);
    return project_.decks[project_.focusedDeckIndex];
  }

  const Deck& focusedDeck() const {
    if (project_.decks.empty()) {
      static Deck fallback;
      return fallback;
    }
    int index = std::clamp(project_.focusedDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    return project_.decks[index];
  }

  std::string focusedDeckLabel() const {
    const Deck& deck = focusedDeck();
    return deck.name.empty() ? deckDefaultName(project_.focusedDeckIndex) : deck.name;
  }

  DeckRuntime* runtimeForDeck(int deckIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(deckRuntimes_.size())) {
      return nullptr;
    }
    return &deckRuntimes_[deckIndex];
  }

  const DeckRuntime* runtimeForDeck(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(deckRuntimes_.size())) {
      return nullptr;
    }
    return &deckRuntimes_[deckIndex];
  }

  DeckRuntime* focusedRuntime() {
    return runtimeForDeck(project_.focusedDeckIndex);
  }

  const DeckRuntime* focusedRuntime() const {
    return runtimeForDeck(project_.focusedDeckIndex);
  }

  MediaEngine* focusedMediaEngine() {
    auto* runtime = focusedRuntime();
    return runtime ? runtime->mediaEngine.get() : nullptr;
  }

  const MediaEngine* focusedMediaEngine() const {
    auto* runtime = focusedRuntime();
    return runtime ? runtime->mediaEngine.get() : nullptr;
  }

  MediaEngine* mediaEngineForDeck(int deckIndex) {
    auto* runtime = runtimeForDeck(deckIndex);
    return runtime ? runtime->mediaEngine.get() : nullptr;
  }

  const MediaEngine* mediaEngineForDeck(int deckIndex) const {
    auto* runtime = runtimeForDeck(deckIndex);
    return runtime ? runtime->mediaEngine.get() : nullptr;
  }

  bool setFocusedDeckIndex(int deckIndex) {
    normalizeProject(project_);
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    project_.focusedDeckIndex = deckIndex;
    selectionChangedAt_ = SDL_GetTicks64();
    triggerToast("deck: " + focusedDeckLabel());
    persistProject();
    return true;
  }

  void cycleFocusedDeck(int direction) {
    normalizeProject(project_);
    if (project_.decks.empty()) {
      return;
    }
    int deckCount = static_cast<int>(project_.decks.size());
    int nextIndex = (project_.focusedDeckIndex + direction + deckCount) % deckCount;
    setFocusedDeckIndex(nextIndex);
    playUiSound(UiSoundEffect::Navigate);
  }

  void addDeck() {
    normalizeProject(project_);
    Deck deck;
    deck.name = deckDefaultName(static_cast<int>(project_.decks.size()));
    project_.decks.push_back(deck);
    project_.advancedOutputMode = true;
    rebuildDeckRuntimes();
    setFocusedDeckIndex(static_cast<int>(project_.decks.size()) - 1);
    playUiSound(UiSoundEffect::Import);
  }

  bool ensureUiAudioDevice() {
    if (uiAudioDevice_ != 0) {
      return true;
    }

    SDL_AudioSpec desired {};
    desired.freq = kAudioRate;
    desired.format = kAudioFormat;
    desired.channels = kAudioChannels;
    desired.samples = 2048;
    uiAudioDevice_ = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
    if (uiAudioDevice_ != 0) {
      SDL_PauseAudioDevice(uiAudioDevice_, 1);
      return true;
    }
    return false;
  }

  SDL_AudioDeviceID openMainAudioDevice(const std::string& preferredDeviceName, std::string& effectiveName) {
    SDL_AudioSpec desired {};
    desired.freq = kAudioRate;
    desired.format = kAudioFormat;
    desired.channels = kAudioChannels;
    desired.samples = 2048;

    auto openMain = [&](const char* deviceName) -> SDL_AudioDeviceID {
      SDL_AudioSpec obtained {};
      return SDL_OpenAudioDevice(deviceName, 0, &desired, &obtained, 0);
    };

    effectiveName = preferredDeviceName;
    SDL_AudioDeviceID mainOut = 0;

    if (!preferredDeviceName.empty()) {
      mainOut = openMain(preferredDeviceName.c_str());
    } else {
      mainOut = openMain(nullptr);
    }

    if (mainOut == 0 && !preferredDeviceName.empty()) {
      effectiveName.clear();
      mainOut = openMain(nullptr);
    }
    SDL_PauseAudioDevice(mainOut, 1);
    return mainOut;
  }

  void destroyDeckRuntime(DeckRuntime& runtime) {
    if (runtime.mediaEngine) {
      runtime.mediaEngine->stopAll();
      runtime.mediaEngine.reset();
    }
    runtime.browserProcess.stop();
    runtime.browserCueLive = false;
    if (!runtime.browserProfileDir.empty()) {
      std::error_code error;
      fs::remove_all(runtime.browserProfileDir, error);
      runtime.browserProfileDir.clear();
    }
#if defined(PLAYBOY_HAS_NDI_SDK)
    if (runtime.ndiSender && ndiApi_.sendDestroyFn) {
      ndiApi_.sendDestroyFn(runtime.ndiSender);
      runtime.ndiSender = nullptr;
    }
    runtime.ndiSenderName.clear();
    runtime.ndiFrameBuffer.clear();
#endif
    if (runtime.audioDevice != 0) {
      SDL_CloseAudioDevice(runtime.audioDevice);
      runtime.audioDevice = 0;
    }
    if (runtime.outputRenderer) {
      SDL_DestroyRenderer(runtime.outputRenderer);
      runtime.outputRenderer = nullptr;
    }
    if (runtime.outputWindow) {
      SDL_DestroyWindow(runtime.outputWindow);
      runtime.outputWindow = nullptr;
    }
  }

  bool reopenDeckAudioOutput(int deckIndex, const std::string& preferredDeviceName) {
    Deck& deck = project_.decks[deckIndex];
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime) {
      return false;
    }

    std::string effectiveName;
    SDL_AudioDeviceID newMain = openMainAudioDevice(preferredDeviceName, effectiveName);
    if (newMain == 0) {
      return false;
    }

    if (runtime->mediaEngine) {
      runtime->mediaEngine->stopAll();
      runtime->mediaEngine.reset();
    }
    if (runtime->audioDevice != 0) {
      SDL_CloseAudioDevice(runtime->audioDevice);
      runtime->audioDevice = 0;
    }
    runtime->audioDevice = newMain;
    deck.audioOutputDeviceName = effectiveName;
    runtime->mediaEngine = std::make_unique<MediaEngine>(runtime->outputRenderer, runtime->audioDevice);
    return true;
  }

  bool ensureNdiRuntimeReady(std::string* errorMessage = nullptr) {
#if defined(PLAYBOY_HAS_NDI_SDK)
    if (ndiApi_.ensureLoaded()) {
      return true;
    }
    if (errorMessage) {
      *errorMessage = ndiApi_.loadError;
    }
    return false;
#else
    if (errorMessage) {
      *errorMessage = "built without NDI SDK headers";
    }
    return false;
#endif
  }

  void applyDeckNdiSettings(int deckIndex, bool withToast) {
    Deck& deck = project_.decks[deckIndex];
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime) {
      return;
    }

#if defined(PLAYBOY_HAS_NDI_SDK)
    auto clearSender = [&]() {
      if (runtime->ndiSender && ndiApi_.sendDestroyFn) {
        ndiApi_.sendDestroyFn(runtime->ndiSender);
      }
      runtime->ndiSender = nullptr;
      runtime->ndiSenderName.clear();
      runtime->ndiFrameBuffer.clear();
    };

    if (!deck.ndiEnabled) {
      clearSender();
      if (withToast) {
        triggerToast("ndi off");
      }
      return;
    }

    std::string loadError;
    if (!ensureNdiRuntimeReady(&loadError)) {
      deck.ndiEnabled = false;
      clearSender();
      if (withToast) {
        triggerToast("ndi unavailable");
      }
      return;
    }

    if (deck.ndiSourceName.empty()) {
      deck.ndiSourceName = defaultNdiSourceName(deck, deckIndex);
    }
    if (runtime->ndiSender && runtime->ndiSenderName == deck.ndiSourceName) {
      if (withToast) {
        triggerToast("ndi live: " + deck.ndiSourceName);
      }
      return;
    }

    clearSender();
    NDIlib_send_create_t create {};
    create.p_ndi_name = deck.ndiSourceName.c_str();
    create.p_groups = nullptr;
    create.clock_video = false;
    create.clock_audio = false;
    runtime->ndiSender = ndiApi_.sendCreateFn ? ndiApi_.sendCreateFn(&create) : nullptr;
    if (!runtime->ndiSender) {
      deck.ndiEnabled = false;
      if (withToast) {
        triggerToast("ndi sender failed");
      }
      return;
    }
    runtime->ndiSenderName = deck.ndiSourceName;
    if (withToast) {
      triggerToast("ndi live: " + runtime->ndiSenderName);
    }
#else
    (void) deck;
    (void) runtime;
    if (withToast) {
      triggerToast("ndi unsupported build");
    }
#endif
  }

  void setFocusedDeckNdiEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.ndiEnabled == enabled) {
      return;
    }
    deck.ndiEnabled = enabled;
    applyDeckNdiSettings(project_.focusedDeckIndex, true);
    playUiSound(UiSoundEffect::Toggle);
    persistProject();
  }

  void toggleFocusedDeckNdi() {
    setFocusedDeckNdiEnabled(!focusedDeck().ndiEnabled);
  }

  void setFocusedDeckNdiName(const std::string& requestedName) {
    Deck& deck = focusedDeckMutable();
    std::string normalized = trim(requestedName);
    if (normalized.empty()) {
      normalized = defaultNdiSourceName(deck, project_.focusedDeckIndex);
    }
    if (deck.ndiSourceName == normalized) {
      return;
    }
    deck.ndiSourceName = normalized;
    if (deck.ndiEnabled) {
      applyDeckNdiSettings(project_.focusedDeckIndex, true);
    } else {
      triggerToast("ndi name: " + deck.ndiSourceName);
    }
    playUiSound(UiSoundEffect::Toggle);
    persistProject();
  }

  void sendDeckNdiFrame(int deckIndex, int width, int height, double fpsHint) {
#if defined(PLAYBOY_HAS_NDI_SDK)
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->outputRenderer) {
      return;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (!deck.ndiEnabled) {
      return;
    }
    if (!runtime->ndiSender) {
      applyDeckNdiSettings(deckIndex, false);
      if (!runtime->ndiSender) {
        return;
      }
    }
    if (width <= 0 || height <= 0) {
      return;
    }

    size_t stride = static_cast<size_t>(width) * 4u;
    size_t frameBytes = stride * static_cast<size_t>(height);
    if (runtime->ndiFrameBuffer.size() != frameBytes) {
      runtime->ndiFrameBuffer.resize(frameBytes);
    }
    if (runtime->ndiFrameBuffer.empty()) {
      return;
    }
    if (SDL_RenderReadPixels(runtime->outputRenderer, nullptr, SDL_PIXELFORMAT_BGRA32, runtime->ndiFrameBuffer.data(), static_cast<int>(stride)) != 0) {
      return;
    }

    int frameRateN = 30000;
    int frameRateD = 1000;
    if (std::isfinite(fpsHint) && fpsHint > 1.0) {
      frameRateN = std::max(1, static_cast<int>(std::round(fpsHint * 1000.0)));
    }

    NDIlib_video_frame_v2_t frame {};
    frame.xres = width;
    frame.yres = height;
    frame.FourCC = NDIlib_FourCC_video_type_BGRA;
    frame.frame_rate_N = frameRateN;
    frame.frame_rate_D = frameRateD;
    frame.picture_aspect_ratio = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : (16.0f / 9.0f);
    frame.frame_format_type = NDIlib_frame_format_type_progressive;
    frame.timecode = NDIlib_send_timecode_synthesize;
    frame.p_data = runtime->ndiFrameBuffer.data();
    frame.line_stride_in_bytes = static_cast<int>(stride);
    frame.p_metadata = nullptr;
    frame.timestamp = 0;
    ndiApi_.sendVideoFn(runtime->ndiSender, &frame);
#else
    (void) deckIndex;
    (void) width;
    (void) height;
    (void) fpsHint;
#endif
  }

  int ndiConnectionCount(int deckIndex) const {
#if defined(PLAYBOY_HAS_NDI_SDK)
    const DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->ndiSender || !ndiApi_.sendConnectionsFn) {
      return 0;
    }
    return std::max(0, ndiApi_.sendConnectionsFn(runtime->ndiSender, 0));
#else
    (void) deckIndex;
    return 0;
#endif
  }

  bool createDeckRuntime(int deckIndex) {
    Deck& deck = project_.decks[deckIndex];
    DeckRuntime& runtime = deckRuntimes_[deckIndex];
    destroyDeckRuntime(runtime);

    std::string title = std::string(kOutputTitle) + " - " + (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name);
    runtime.outputWindow = SDL_CreateWindow(
      title.c_str(),
      SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED,
      kOutputWidth,
      kOutputHeight,
      SDL_WINDOW_RESIZABLE
    );
    if (!runtime.outputWindow) {
      return false;
    }

    runtime.outputRenderer = SDL_CreateRenderer(runtime.outputWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!runtime.outputRenderer) {
      destroyDeckRuntime(runtime);
      return false;
    }

    if (!reopenDeckAudioOutput(deckIndex, deck.audioOutputDeviceName)) {
      destroyDeckRuntime(runtime);
      return false;
    }

    applyOutputDisplaySelection(deckIndex);
    applyDeckNdiSettings(deckIndex, false);
    return true;
  }

  bool rebuildDeckRuntimes() {
    normalizeProject(project_);
    for (auto& runtime : deckRuntimes_) {
      destroyDeckRuntime(runtime);
    }
    deckRuntimes_.clear();
    deckRuntimes_.resize(project_.decks.size());

    for (size_t index = 0; index < project_.decks.size(); ++index) {
      if (!createDeckRuntime(static_cast<int>(index))) {
        return false;
      }
    }
    return true;
  }

  std::vector<std::string> outputAudioDeviceChoices() const {
    std::vector<std::string> names;
    names.push_back("");
    int deviceCount = SDL_GetNumAudioDevices(0);
    for (int index = 0; index < deviceCount; ++index) {
      const char* name = SDL_GetAudioDeviceName(index, 0);
      if (name && *name) {
        names.emplace_back(name);
      }
    }
    return names;
  }

  void cycleAudioOutputDevice(int direction) {
    auto choices = outputAudioDeviceChoices();
    if (choices.empty()) {
      return;
    }

    auto current = std::find(choices.begin(), choices.end(), focusedDeck().audioOutputDeviceName);
    int currentIndex = current == choices.end() ? 0 : static_cast<int>(std::distance(choices.begin(), current));
    int nextIndex = (currentIndex + direction + static_cast<int>(choices.size())) % static_cast<int>(choices.size());
    if (!reopenDeckAudioOutput(project_.focusedDeckIndex, choices[nextIndex])) {
      triggerToast("audio switch failed", {79, 98, 48, 230}, {223, 248, 185, 255});
      return;
    }
    triggerToast("audio: " + currentAudioOutputLabel());
    playUiSound(UiSoundEffect::Toggle);
    persistProject();
  }

  void applyOutputDisplaySelection(int deckIndex) {
    int displayCount = SDL_GetNumVideoDisplays();
    Deck& deck = project_.decks[deckIndex];
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->outputWindow) {
      return;
    }
    if (displayCount <= 0) {
      deck.outputDisplayIndex = 0;
      return;
    }
    deck.outputDisplayIndex = std::clamp(deck.outputDisplayIndex, 0, displayCount - 1);
    SDL_Rect bounds {};
    if (SDL_GetDisplayBounds(deck.outputDisplayIndex, &bounds) != 0) {
      return;
    }
    Uint32 flags = SDL_GetWindowFlags(runtime->outputWindow);
    bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    if (fullscreen) {
      SDL_SetWindowFullscreen(runtime->outputWindow, 0);
    }
    SDL_SetWindowPosition(runtime->outputWindow, bounds.x + 40 + deckIndex * 20, bounds.y + 40 + deckIndex * 20);
    if (fullscreen) {
      SDL_SetWindowFullscreen(runtime->outputWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
  }

  void cycleOutputDisplay(int direction) {
    int displayCount = SDL_GetNumVideoDisplays();
    if (displayCount <= 0) {
      return;
    }
    Deck& deck = focusedDeckMutable();
    deck.outputDisplayIndex = (deck.outputDisplayIndex + direction + displayCount) % displayCount;
    applyOutputDisplaySelection(project_.focusedDeckIndex);
    std::string label = SDL_GetDisplayName(deck.outputDisplayIndex);
    triggerToast("display: " + (label.empty() ? std::to_string(deck.outputDisplayIndex + 1) : label));
    playUiSound(UiSoundEffect::Toggle);
    persistProject();

    const Cue* active = activeCuePtr();
    if (active && active->kind == CueKind::Browser) {
      auto* runtime = focusedRuntime();
      if (runtime && runtime->browserCueLive) {
        startBrowserCue(project_.focusedDeckIndex, *active);
      }
    }
  }

  bool setOutputDisplayIndex(int index) {
    int displayCount = SDL_GetNumVideoDisplays();
    if (displayCount <= 0 || index < 0 || index >= displayCount) {
      return false;
    }
    focusedDeckMutable().outputDisplayIndex = index;
    applyOutputDisplaySelection(project_.focusedDeckIndex);
    triggerToast("display: " + currentDisplayLabel());
    persistProject();
    const Cue* active = activeCuePtr();
    if (active && active->kind == CueKind::Browser) {
      auto* runtime = focusedRuntime();
      if (runtime && runtime->browserCueLive) {
        startBrowserCue(project_.focusedDeckIndex, *active);
      }
    }
    return true;
  }

  bool setAudioOutputDevice(const std::string& deviceName) {
    if (!reopenDeckAudioOutput(project_.focusedDeckIndex, deviceName)) {
      triggerToast("audio switch failed", {79, 98, 48, 230}, {223, 248, 185, 255});
      return false;
    }
    triggerToast("audio: " + currentAudioOutputLabel());
    playUiSound(UiSoundEffect::Toggle);
    persistProject();
    return true;
  }

  std::string currentDisplayLabel() const {
    const char* name = SDL_GetDisplayName(focusedDeck().outputDisplayIndex);
    if (name && *name) {
      return name;
    }
    return "display " + std::to_string(focusedDeck().outputDisplayIndex + 1);
  }

  std::string browserExecutablePath() const {
#ifdef _WIN32
    static const std::array<std::string, 3> candidates {
      "msedge.exe",
      "chrome.exe",
      "chrome"
    };
#elif __APPLE__
    static const std::array<std::string, 3> candidates {
      "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
      "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
      "/Applications/Chromium.app/Contents/MacOS/Chromium"
    };
#else
    static const std::array<std::string, 7> candidates {
      "chromium",
      "chromium-browser",
      "google-chrome",
      "google-chrome-stable",
      "microsoft-edge",
      "microsoft-edge-stable",
      "chrome"
    };
#endif

    for (const auto& candidate : candidates) {
#ifdef _WIN32
      if (!candidate.empty()) {
        return candidate;
      }
#else
      if (executableOnPath(candidate)) {
        return candidate;
      }
#endif
    }
    return "";
  }

  fs::path nextBrowserProfilePath() const {
    return fs::path("/tmp") / ("playboy-browser-" + std::to_string(static_cast<unsigned long long>(SDL_GetTicks64())));
  }

  void stopBrowserCue(int deckIndex) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime) {
      return;
    }
    runtime->browserProcess.stop();
    runtime->browserCueLive = false;
    if (!runtime->browserProfileDir.empty()) {
      std::error_code error;
      fs::remove_all(runtime->browserProfileDir, error);
      runtime->browserProfileDir.clear();
    }
    if (runtime->outputWindow) {
      SDL_ShowWindow(runtime->outputWindow);
    }
  }

  void stopBrowserCue() {
    stopBrowserCue(project_.focusedDeckIndex);
  }

  bool startBrowserCue(int deckIndex, const Cue& cue) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->outputWindow) {
      return false;
    }
    std::string browserUrl = normalizeBrowserUrl(cue.path);
    if (browserUrl.empty()) {
      return false;
    }

    std::string executable = browserExecutablePath();
    if (executable.empty()) {
      triggerToast("no browser found", {79, 98, 48, 230}, {223, 248, 185, 255});
      return false;
    }

    stopBrowserCue(deckIndex);

    SDL_Rect bounds {0, 0, kOutputWidth, kOutputHeight};
    SDL_GetDisplayBounds(project_.decks[deckIndex].outputDisplayIndex, &bounds);
    runtime->browserProfileDir = nextBrowserProfilePath();
    std::error_code error;
    fs::create_directories(runtime->browserProfileDir, error);

    std::vector<std::string> args {
      executable,
      "--no-first-run",
      "--disable-session-crashed-bubble",
      "--disable-infobars",
      "--new-window",
      "--app=" + browserUrl,
      "--window-position=" + std::to_string(bounds.x) + "," + std::to_string(bounds.y),
      "--window-size=" + std::to_string(std::max(320, bounds.w)) + "," + std::to_string(std::max(180, bounds.h)),
      "--user-data-dir=" + runtime->browserProfileDir.string(),
      "--start-fullscreen"
    };
    if (!spawnDetachedProcess(runtime->browserProcess, args)) {
      std::error_code cleanupError;
      fs::remove_all(runtime->browserProfileDir, cleanupError);
      runtime->browserProfileDir.clear();
      triggerToast("browser launch failed", {79, 98, 48, 230}, {223, 248, 185, 255});
      return false;
    }

    runtime->browserCueLive = true;
    SDL_HideWindow(runtime->outputWindow);
    triggerToast("browser live");
    return true;
  }

  bool startBrowserCue(const Cue& cue) {
    return startBrowserCue(project_.focusedDeckIndex, cue);
  }

  fs::path defaultProjectFile() const {
    const char* envPath = std::getenv("PLAYBOY_PROJECT");
    if (envPath && *envPath) {
      return normalizeProjectPath(fs::absolute(envPath));
    }
    return kDataDir / "default.playboy";
  }

  fs::path normalizeProjectPath(fs::path path) const {
    if (path.empty()) {
      return kDataDir / "default.playboy";
    }
    if (path.extension().empty()) {
      path += ".playboy";
    }
    return path;
  }

  std::string currentProjectLabel() const {
    if (currentProjectFile_.empty()) {
      return "default.playboy";
    }
    return currentProjectFile_.filename().string();
  }

  std::string currentAudioOutputLabel() const {
    return focusedDeck().audioOutputDeviceName.empty() ? "system default" : focusedDeck().audioOutputDeviceName;
  }

  std::string currentNdiOutputLabel() const {
    const Deck& deck = focusedDeck();
    std::string source = deck.ndiSourceName.empty() ? defaultNdiSourceName(deck, project_.focusedDeckIndex) : deck.ndiSourceName;
    if (!deck.ndiEnabled) {
      return "off";
    }
#if defined(PLAYBOY_HAS_NDI_SDK)
    const DeckRuntime* runtime = focusedRuntime();
    bool live = runtime && runtime->ndiSender;
    int listeners = ndiConnectionCount(project_.focusedDeckIndex);
    std::string suffix = live ? "on" : "pending";
    if (listeners > 0) {
      suffix += " (" + std::to_string(listeners) + " rx)";
    }
    return suffix + " / " + source;
#else
    return "unavailable";
#endif
  }

  std::string deckSummaryLabel() const {
    return focusedDeckLabel() + "  (" + std::to_string(project_.focusedDeckIndex + 1) + "/" + std::to_string(project_.decks.size()) + ")";
  }

  std::string deckStatusSummary(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "offline";
    }
    const Deck& deck = project_.decks[deckIndex];
    const Cue* activeCue = activeCuePtr(deckIndex);
    const MediaEngine* engine = mediaEngineForDeck(deckIndex);
    std::ostringstream output;
    output << (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << " | " << transportStatusLabel(deckIndex);
    if (activeCue) {
      output << " | " << activeCue->name;
    } else {
      output << " | idle";
    }
    if (engine) {
      output << " | " << formatSeconds(engine->position()) << "/" << formatSeconds(engine->duration());
    }
    if (deck.ndiEnabled) {
      output << " | ndi:" << (deck.ndiSourceName.empty() ? defaultNdiSourceName(deck, deckIndex) : deck.ndiSourceName);
    }
    return output.str();
  }

  std::string buildStatusSnapshot() const {
    std::ostringstream output;
    output << "PLAYBOY_0.01"
           << " focus=" << (project_.focusedDeckIndex + 1)
           << " decks=" << project_.decks.size()
           << '\n';
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      const Deck& deck = project_.decks[deckIndex];
      const Cue* activeCue = activeCuePtr(deckIndex);
      const Cue* selectedCue = selectedCuePtr(deckIndex);
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      output << "DECK " << (deckIndex + 1)
             << " name=\"" << (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << "\""
             << " status=" << transportStatusLabel(deckIndex)
             << " selected=" << (deck.selectedIndex >= 0 ? deck.selectedIndex + 1 : 0)
             << " active=" << (deck.activeIndex >= 0 ? deck.activeIndex + 1 : 0)
             << " display=" << (deck.outputDisplayIndex + 1)
             << " audio=\"" << (deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\""
             << " ndi=" << (deck.ndiEnabled ? "on" : "off")
             << " ndi_name=\"" << (deck.ndiSourceName.empty() ? defaultNdiSourceName(deck, deckIndex) : deck.ndiSourceName) << "\""
             << " ndi_rx=" << ndiConnectionCount(deckIndex)
             << " cue=\"" << (activeCue ? activeCue->name : (selectedCue ? selectedCue->name : "")) << "\"";
      if (engine) {
        output << " pos=" << formatSeconds(engine->position())
               << " dur=" << formatSeconds(engine->duration())
               << " vol=" << static_cast<int>(std::round(engine->volume() * 100.0f));
      }
      output << '\n';
    }
    return output.str();
  }

  std::string buildDeckStatusSnapshot(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "DECK offline\n";
    }
    std::ostringstream output;
    const Deck& deck = project_.decks[deckIndex];
    const Cue* activeCue = activeCuePtr(deckIndex);
    const Cue* selectedCue = selectedCuePtr(deckIndex);
    const MediaEngine* engine = mediaEngineForDeck(deckIndex);
    output << "PLAYBOY_0.01"
           << " focus=" << (project_.focusedDeckIndex + 1)
           << " decks=" << project_.decks.size()
           << '\n';
    output << "DECK " << (deckIndex + 1)
           << " name=\"" << (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << "\""
           << " status=" << transportStatusLabel(deckIndex)
           << " selected=" << (deck.selectedIndex >= 0 ? deck.selectedIndex + 1 : 0)
           << " active=" << (deck.activeIndex >= 0 ? deck.activeIndex + 1 : 0)
           << " display=" << (deck.outputDisplayIndex + 1)
           << " audio=\"" << (deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\""
           << " ndi=" << (deck.ndiEnabled ? "on" : "off")
           << " ndi_name=\"" << (deck.ndiSourceName.empty() ? defaultNdiSourceName(deck, deckIndex) : deck.ndiSourceName) << "\""
           << " ndi_rx=" << ndiConnectionCount(deckIndex)
           << " cue=\"" << (activeCue ? activeCue->name : (selectedCue ? selectedCue->name : "")) << "\"";
    if (engine) {
      output << " pos=" << formatSeconds(engine->position())
             << " dur=" << formatSeconds(engine->duration())
             << " vol=" << static_cast<int>(std::round(engine->volume() * 100.0f));
    }
    output << '\n';
    return output.str();
  }

  std::string buildStatusSnapshotJson() const {
    std::ostringstream output;
    output << "{"
           << "\"app\":\"PLAYBOY_0.01\","
           << "\"focusedDeck\":" << (project_.focusedDeckIndex + 1) << ","
           << "\"deckCount\":" << project_.decks.size() << ","
           << "\"decks\":[";
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (deckIndex > 0) {
        output << ",";
      }
      const Deck& deck = project_.decks[deckIndex];
      const Cue* activeCue = activeCuePtr(deckIndex);
      const Cue* selectedCue = selectedCuePtr(deckIndex);
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      output << "{"
             << "\"index\":" << (deckIndex + 1) << ","
             << "\"name\":\"" << escapeJson(deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << "\","
             << "\"status\":\"" << escapeJson(transportStatusLabel(deckIndex)) << "\","
             << "\"selected\":" << (deck.selectedIndex >= 0 ? deck.selectedIndex + 1 : 0) << ","
             << "\"active\":" << (deck.activeIndex >= 0 ? deck.activeIndex + 1 : 0) << ","
             << "\"display\":" << (deck.outputDisplayIndex + 1) << ","
             << "\"audio\":\"" << escapeJson(deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\","
             << "\"ndiEnabled\":" << (deck.ndiEnabled ? "true" : "false") << ","
             << "\"ndiName\":\"" << escapeJson(deck.ndiSourceName.empty() ? defaultNdiSourceName(deck, deckIndex) : deck.ndiSourceName) << "\","
             << "\"ndiReceivers\":" << ndiConnectionCount(deckIndex) << ","
             << "\"cue\":\"" << escapeJson(activeCue ? activeCue->name : (selectedCue ? selectedCue->name : "")) << "\"";
      if (engine) {
        output << ",\"position\":\"" << escapeJson(formatSeconds(engine->position())) << "\""
               << ",\"duration\":\"" << escapeJson(formatSeconds(engine->duration())) << "\""
               << ",\"volume\":" << static_cast<int>(std::round(engine->volume() * 100.0f));
      }
      output << "}";
    }
    output << "]}\n";
    return output.str();
  }

  void updateStatusSnapshot() {
    std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
    statusSnapshot_ = buildStatusSnapshot();
    statusSnapshotJson_ = buildStatusSnapshotJson();
    statusDeckSnapshots_.clear();
    statusDeckSnapshots_.reserve(project_.decks.size());
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      statusDeckSnapshots_.push_back(buildDeckStatusSnapshot(deckIndex));
    }
  }

  void persistProject() {
    normalizeProject(project_);
    saveProject(currentProjectFile_, project_);
  }

  void triggerToast(std::string message, SDL_Color fill = {155, 188, 15, 220}, SDL_Color ink = {15, 56, 15, 255}, Uint32 durationMs = 1200) {
    if (!project_.uiTransitionsEnabled) {
      return;
    }
    toast_.active = true;
    toast_.startedAt = SDL_GetTicks64();
    toast_.durationMs = durationMs;
    toast_.message = std::move(message);
    toast_.fill = fill;
    toast_.ink = ink;
  }

  void queueUiPattern(const std::vector<std::pair<double, int>>& notes, float level = 0.13f) {
    if (!project_.uiSoundsEnabled || uiAudioDevice_ == 0) {
      return;
    }

    SDL_ClearQueuedAudio(uiAudioDevice_);

    std::vector<std::int16_t> pcm;
    for (const auto& [frequency, milliseconds] : notes) {
      int samples = std::max(1, milliseconds * kAudioRate / 1000);
      int ramp = std::max(4, samples / 10);
      for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
        double time = static_cast<double>(sampleIndex) / static_cast<double>(kAudioRate);
        double wave = std::sin(time * frequency * 6.28318530718) >= 0.0 ? 1.0 : -1.0;
        double envelope = 1.0;
        if (sampleIndex < ramp) {
          envelope = static_cast<double>(sampleIndex) / static_cast<double>(ramp);
        } else if (sampleIndex > samples - ramp) {
          envelope = static_cast<double>(samples - sampleIndex) / static_cast<double>(ramp);
        }
        std::int16_t value = static_cast<std::int16_t>(std::lround(32767.0 * level * envelope * wave));
        pcm.push_back(value);
        pcm.push_back(value);
      }

      int gapSamples = kAudioRate / 80;
      for (int gap = 0; gap < gapSamples; ++gap) {
        pcm.push_back(0);
        pcm.push_back(0);
      }
    }

    if (!pcm.empty()) {
      SDL_QueueAudio(uiAudioDevice_, pcm.data(), static_cast<Uint32>(pcm.size() * sizeof(std::int16_t)));
      SDL_PauseAudioDevice(uiAudioDevice_, 0);
    }
  }

  void playUiSound(UiSoundEffect effect) {
    switch (effect) {
      case UiSoundEffect::Navigate:
        queueUiPattern({{880.0, 26}, {1046.5, 34}}, 0.08f);
        break;
      case UiSoundEffect::Import:
        queueUiPattern({{523.3, 34}, {659.3, 34}, {784.0, 48}}, 0.10f);
        break;
      case UiSoundEffect::Take:
        queueUiPattern({{659.3, 34}, {987.8, 34}, {1318.5, 62}}, 0.11f);
        break;
      case UiSoundEffect::Toggle:
        queueUiPattern({{784.0, 30}, {1174.7, 42}}, 0.09f);
        break;
      case UiSoundEffect::Stop:
        queueUiPattern({{659.3, 32}, {440.0, 46}}, 0.09f);
        break;
      case UiSoundEffect::Clear:
        queueUiPattern({{392.0, 34}, {293.7, 50}}, 0.09f);
        break;
      case UiSoundEffect::Delete:
        queueUiPattern({{523.3, 24}, {349.2, 58}}, 0.09f);
        break;
    }
  }

  void toggleUiSounds() {
    project_.uiSoundsEnabled = !project_.uiSoundsEnabled && uiAudioDevice_ != 0;
    if (uiAudioDevice_ == 0) {
      project_.uiSoundsEnabled = false;
    }
    if (project_.uiSoundsEnabled) {
      playUiSound(UiSoundEffect::Toggle);
    }
    triggerToast(project_.uiSoundsEnabled ? "little bloops on" : "little bloops off");
    persistProject();
  }

  void toggleUiTransitions() {
    bool next = !project_.uiTransitionsEnabled;
    project_.uiTransitionsEnabled = next;
    if (project_.uiTransitionsEnabled) {
      triggerToast("screen wiggles on");
      playUiSound(UiSoundEffect::Toggle);
    } else {
      toast_.active = false;
    }
    persistProject();
  }

  void setAutoAdvance(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.autoAdvance == enabled) {
      return;
    }
    deck.autoAdvance = enabled;
    triggerToast(deck.autoAdvance ? "auto next on" : "auto next off");
    playUiSound(UiSoundEffect::Toggle);
    persistProject();
  }

  void toggleAutoAdvance() {
    setAutoAdvance(!focusedDeck().autoAdvance);
  }

  void setPlaylistLoop(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.playlistLoop == enabled) {
      return;
    }
    deck.playlistLoop = enabled;
    triggerToast(deck.playlistLoop ? "playlist loop on" : "playlist loop off");
    playUiSound(UiSoundEffect::Toggle);
    persistProject();
  }

  void togglePlaylistLoop() {
    setPlaylistLoop(!focusedDeck().playlistLoop);
  }

  void onSelectionChanged() {
    selectionChangedAt_ = SDL_GetTicks64();
    playUiSound(UiSoundEffect::Navigate);
  }

  void enqueueRemoteCommand(std::string command) {
    std::lock_guard<std::mutex> lock(remoteCommandMutex_);
    remoteCommands_.push_back(std::move(command));
  }

  void enqueueRemoteCommandBatch(const std::string& payload, char separatorHint = '\n') {
    if (separatorHint == '\n') {
      for (auto& line : splitLines(payload)) {
        std::string trimmed = trim(line);
        if (!trimmed.empty()) {
          enqueueRemoteCommand(trimmed);
        }
      }
      return;
    }

    for (auto& item : splitByChar(payload, separatorHint)) {
      std::string trimmed = trim(item);
      if (!trimmed.empty()) {
        enqueueRemoteCommand(trimmed);
      }
    }
  }

#ifndef _WIN32
  bool maybeRespondToCompanionQuery(SocketHandle client, const std::string& line) {
    std::string query = trim(line);
    std::string upper = toUpper(query);

    auto sendSnapshot = [&](const std::string& payload) {
      if (!payload.empty()) {
        send(client, payload.c_str(), payload.size(), 0);
      }
    };

    if (upper == "STATUS JSON" || upper == "STATE JSON") {
      std::string snapshotJson;
      {
        std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
        snapshotJson = statusSnapshotJson_;
      }
      if (snapshotJson.empty()) {
        snapshotJson = "{\"app\":\"PLAYBOY_0.01\",\"deckCount\":0,\"decks\":[]}\n";
      }
      sendSnapshot(snapshotJson);
      return true;
    }
    if (upper == "STATUS" || upper == "STATE" || upper == "STATUS ALL" || upper == "STATE ALL") {
      std::string snapshot;
      {
        std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
        snapshot = statusSnapshot_;
      }
      if (snapshot.empty()) {
        snapshot = "PLAYBOY_0.01 decks=0\n";
      }
      sendSnapshot(snapshot);
      return true;
    }

    if (upper.rfind("STATUS ", 0) == 0 || upper.rfind("STATE ", 0) == 0) {
      auto parts = splitWhitespace(query);
      if (parts.size() >= 2) {
        try {
          int deckIndex = std::stoi(parts[1]) - 1;
          std::string deckSnapshot;
          {
            std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
            if (deckIndex >= 0 && deckIndex < static_cast<int>(statusDeckSnapshots_.size())) {
              deckSnapshot = statusDeckSnapshots_[deckIndex];
            }
          }
          if (!deckSnapshot.empty()) {
            sendSnapshot(deckSnapshot);
            return true;
          }
        } catch (...) {
        }
      }
    }

    return false;
  }
#endif

  bool startCompanionControl() {
#ifdef _WIN32
    companionReady_ = false;
    return false;
#else
    const char* portEnv = std::getenv("PLAYBOY_COMPANION_PORT");
    if (portEnv && *portEnv) {
      try {
        companionPort_ = std::clamp(std::stoi(portEnv), 1, 65535);
      } catch (...) {
        companionPort_ = 5510;
      }
    }

    companionTcpListen_ = createBoundSocket(SOCK_STREAM, companionPort_, true);
    companionUdpSocket_ = createBoundSocket(SOCK_DGRAM, companionPort_, false);
    if (companionTcpListen_ == kInvalidSocket || companionUdpSocket_ == kInvalidSocket) {
      closeSocket(companionTcpListen_);
      closeSocket(companionUdpSocket_);
      companionTcpListen_ = kInvalidSocket;
      companionUdpSocket_ = kInvalidSocket;
      companionReady_ = false;
      return false;
    }

    companionStop_.store(false);
    companionThread_ = std::thread([this]() {
      companionLoop();
    });
    companionReady_ = true;
    return true;
#endif
  }

  void stopCompanionControl() {
#ifdef _WIN32
    companionReady_ = false;
#else
    companionStop_.store(true);
    if (companionThread_.joinable()) {
      companionThread_.join();
    }
    for (auto client : companionClients_) {
      closeSocket(client);
    }
    companionClients_.clear();
    companionClientBuffers_.clear();
    closeSocket(companionTcpListen_);
    closeSocket(companionUdpSocket_);
    companionTcpListen_ = kInvalidSocket;
    companionUdpSocket_ = kInvalidSocket;
    companionReady_ = false;
#endif
  }

#ifndef _WIN32
  void companionLoop() {
    while (!companionStop_.load()) {
      fd_set readFds;
      FD_ZERO(&readFds);
      int maxFd = -1;

      FD_SET(companionTcpListen_, &readFds);
      maxFd = std::max(maxFd, companionTcpListen_);
      FD_SET(companionUdpSocket_, &readFds);
      maxFd = std::max(maxFd, companionUdpSocket_);

      for (auto client : companionClients_) {
        FD_SET(client, &readFds);
        maxFd = std::max(maxFd, client);
      }

      timeval timeout {};
      timeout.tv_sec = 0;
      timeout.tv_usec = 200000;

      int ready = select(maxFd + 1, &readFds, nullptr, nullptr, &timeout);
      if (ready < 0) {
        if (errno == EINTR) {
          continue;
        }
        break;
      }
      if (ready == 0) {
        continue;
      }

      if (FD_ISSET(companionTcpListen_, &readFds)) {
        sockaddr_in clientAddress {};
        socklen_t clientLength = sizeof(clientAddress);
        SocketHandle client = accept(companionTcpListen_, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
        if (client >= 0) {
          companionClients_.push_back(client);
          companionClientBuffers_[client] = "";
        }
      }

      if (FD_ISSET(companionUdpSocket_, &readFds)) {
        std::array<char, 2048> buffer {};
        ssize_t bytes = recvfrom(companionUdpSocket_, buffer.data(), buffer.size(), 0, nullptr, nullptr);
        if (bytes > 0) {
          enqueueRemoteCommandBatch(std::string(buffer.data(), static_cast<size_t>(bytes)));
        }
      }

      std::vector<SocketHandle> closedClients;
      for (auto client : companionClients_) {
        if (!FD_ISSET(client, &readFds)) {
          continue;
        }

        std::array<char, 2048> buffer {};
        ssize_t bytes = recv(client, buffer.data(), buffer.size(), 0);
        if (bytes <= 0) {
          closedClients.push_back(client);
          continue;
        }

        std::string& pending = companionClientBuffers_[client];
        pending.append(buffer.data(), static_cast<size_t>(bytes));

        size_t newlinePos = std::string::npos;
        while ((newlinePos = pending.find('\n')) != std::string::npos) {
          std::string line = trim(pending.substr(0, newlinePos));
          pending.erase(0, newlinePos + 1);
          if (!line.empty()) {
            if (!maybeRespondToCompanionQuery(client, line)) {
              enqueueRemoteCommand(line);
            }
          }
        }
      }

      if (!closedClients.empty()) {
        for (auto client : closedClients) {
          auto pendingIt = companionClientBuffers_.find(client);
          if (pendingIt != companionClientBuffers_.end()) {
            std::string leftover = trim(pendingIt->second);
            if (!leftover.empty()) {
              if (!maybeRespondToCompanionQuery(client, leftover)) {
                enqueueRemoteCommand(leftover);
              }
            }
            companionClientBuffers_.erase(pendingIt);
          }
          closeSocket(client);
          companionClients_.erase(
            std::remove(companionClients_.begin(), companionClients_.end(), client),
            companionClients_.end()
          );
        }
      }
    }
  }
#endif

  void processRemoteCommands() {
    std::deque<std::string> pending;
    {
      std::lock_guard<std::mutex> lock(remoteCommandMutex_);
      pending.swap(remoteCommands_);
    }

    for (const auto& command : pending) {
      handleRemoteCommand(command);
    }
  }

  void handleRemoteCommand(const std::string& rawCommand) {
    auto parts = splitWhitespace(rawCommand);
    if (parts.empty()) {
      return;
    }

    std::string command = toUpper(parts[0]);
    auto parseCueIndex = [&](size_t tokenIndex) -> std::optional<int> {
      if (tokenIndex >= parts.size()) {
        return std::nullopt;
      }
      try {
        int index = std::stoi(parts[tokenIndex]);
        if (index < 1 || index > static_cast<int>(focusedDeck().cues.size())) {
          return std::nullopt;
        }
        return index - 1;
      } catch (...) {
        return std::nullopt;
      }
    };
    auto parseNumber = [&](size_t tokenIndex) -> std::optional<double> {
      if (tokenIndex >= parts.size()) {
        return std::nullopt;
      }
      try {
        return std::stod(parts[tokenIndex]);
      } catch (...) {
        return std::nullopt;
      }
    };
    auto parseToggleWord = [&](size_t tokenIndex) -> std::optional<bool> {
      if (tokenIndex >= parts.size()) {
        return std::nullopt;
      }
      std::string value = toUpper(parts[tokenIndex]);
      if (value == "ON" || value == "1" || value == "TRUE") {
        return true;
      }
      if (value == "OFF" || value == "0" || value == "FALSE") {
        return false;
      }
      return std::nullopt;
    };

    if (command == "PING") {
      triggerToast("companion ping");
      return;
    }
    if (command == "DECK") {
      if (parts.size() < 2) {
        return;
      }
      try {
        int deckIndex = std::stoi(parts[1]) - 1;
        if (!setFocusedDeckIndex(deckIndex)) {
          return;
        }
        if (parts.size() > 2) {
          handleRemoteCommand(joinParts(parts, 2));
        }
      } catch (...) {
      }
      return;
    }
    if (command == "DECKNEXT") {
      cycleFocusedDeck(1);
      return;
    }
    if (command == "DECKPREV" || command == "DECKPREVIOUS") {
      cycleFocusedDeck(-1);
      return;
    }
    if (command == "DECKADD" || command == "NEWDECK") {
      addDeck();
      return;
    }
    if (command == "GO" || command == "TOGGLE") {
      toggleTransport();
      return;
    }
    if (command == "PLAY") {
      playTransport();
      return;
    }
    if (command == "PAUSE") {
      pauseTransport();
      return;
    }
    if (command == "STOP") {
      stopTransport();
      return;
    }
    if (command == "CLEAR") {
      clearOutput();
      return;
    }
    if (command == "FULLSCREEN") {
      toggleOutputFullscreen();
      return;
    }
    if (command == "NEXT") {
      selectRelative(1, false);
      return;
    }
    if (command == "PREV" || command == "PREVIOUS") {
      selectRelative(-1, false);
      return;
    }
    if (command == "SELECT") {
      auto index = parseCueIndex(1);
      if (index) {
        Deck& deck = focusedDeckMutable();
        if (deck.selectedIndex != *index) {
          deck.selectedIndex = *index;
          onSelectionChanged();
          triggerToast("cue " + std::to_string(*index + 1) + " armed");
          persistProject();
        }
      }
      return;
    }
    if (command == "TAKE") {
      auto index = parseCueIndex(1);
      if (index) {
        focusedDeckMutable().selectedIndex = *index;
        onSelectionChanged();
      }
      takeSelected(parts.size() > 2 && toUpper(parts[2]) == "AUTO");
      return;
    }
    if (command == "VOLUME") {
      auto value = parseNumber(1);
      if (value) {
        MediaEngine* engine = focusedMediaEngine();
        if (!engine) {
          return;
        }
        double normalized = *value > 1.0 ? *value / 100.0 : *value;
        engine->setVolume(static_cast<float>(std::clamp(normalized, 0.0, 1.0)));
        triggerToast("speaker " + std::to_string(static_cast<int>(std::round(engine->volume() * 100.0f))) + "%");
      }
      return;
    }
    if (command == "SEEK") {
      auto value = parseNumber(1);
      if (value) {
        MediaEngine* engine = focusedMediaEngine();
        if (!engine) {
          return;
        }
        engine->seek(*value);
        triggerToast("jump to " + formatSeconds(*value));
      }
      return;
    }
    if (command == "SFX") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleUiSounds();
      } else if (*state != project_.uiSoundsEnabled) {
        project_.uiSoundsEnabled = *state && uiAudioDevice_ != 0;
        if (project_.uiSoundsEnabled) {
          playUiSound(UiSoundEffect::Toggle);
        }
        triggerToast(project_.uiSoundsEnabled ? "little bloops on" : "little bloops off");
        persistProject();
      }
      return;
    }
    if (command == "ANIM" || command == "ANIMATION") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleUiTransitions();
      } else if (*state != project_.uiTransitionsEnabled) {
        project_.uiTransitionsEnabled = *state;
        if (project_.uiTransitionsEnabled) {
          triggerToast("screen wiggles on");
          playUiSound(UiSoundEffect::Toggle);
        } else {
          toast_.active = false;
        }
        persistProject();
      }
      return;
    }
    if (command == "DELETE") {
      deleteSelected();
      return;
    }
    if (command == "LOOP") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleSelectedLoop();
      } else {
        setSelectedLoop(*state);
      }
      return;
    }
    if (command == "HOLD" || command == "HOLDLAST") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleSelectedPauseOnLastFrame();
      } else {
        setSelectedPauseOnLastFrame(*state);
      }
      return;
    }
    if (command == "FADEIN") {
      auto value = parseNumber(1);
      if (value) {
        setSelectedFade(true, *value);
      }
      return;
    }
    if (command == "FADEOUT") {
      auto value = parseNumber(1);
      if (value) {
        setSelectedFade(false, *value);
      }
      return;
    }
    if (command == "AUTONEXT" || command == "AUTOADVANCE") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleAutoAdvance();
      } else {
        setAutoAdvance(*state);
      }
      return;
    }
    if (command == "PLAYLISTLOOP") {
      auto state = parseToggleWord(1);
      if (!state) {
        togglePlaylistLoop();
      } else {
        setPlaylistLoop(*state);
      }
      return;
    }
    if (command == "PATTERN") {
      addKawaiiPatternCue();
      return;
    }
    if (command == "BROWSER") {
      std::string url = joinParts(parts, 1);
      if (!url.empty()) {
        addBrowserCue(url);
      }
      return;
    }
    if (command == "AUDIO") {
      if (parts.size() > 1) {
        std::string value = toUpper(parts[1]);
        if (value == "NEXT") {
          cycleAudioOutputDevice(1);
        } else if (value == "PREV" || value == "PREVIOUS") {
          cycleAudioOutputDevice(-1);
        } else if (value == "DEFAULT") {
          setAudioOutputDevice("");
        } else {
          setAudioOutputDevice(joinParts(parts, 1));
        }
      }
      return;
    }
    if (command == "DISPLAY") {
      if (parts.size() > 1) {
        std::string value = toUpper(parts[1]);
        if (value == "NEXT") {
          cycleOutputDisplay(1);
        } else if (value == "PREV" || value == "PREVIOUS") {
          cycleOutputDisplay(-1);
        } else {
          try {
            int displayIndex = std::stoi(parts[1]);
            setOutputDisplayIndex(std::max(0, displayIndex - 1));
          } catch (...) {
          }
        }
      }
      return;
    }
    if (command == "NDI") {
      if (parts.size() == 1) {
        toggleFocusedDeckNdi();
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "ON") {
        setFocusedDeckNdiEnabled(true);
      } else if (value == "OFF") {
        setFocusedDeckNdiEnabled(false);
      } else if (value == "TOGGLE") {
        toggleFocusedDeckNdi();
      } else if (value == "NAME") {
        setFocusedDeckNdiName(joinParts(parts, 2));
      } else if (value == "DEFAULT" || value == "CLEAR") {
        setFocusedDeckNdiName("");
      } else if (value == "STATUS") {
        triggerToast("ndi: " + currentNdiOutputLabel());
      }
      return;
    }
    if (command == "NDINAME") {
      setFocusedDeckNdiName(joinParts(parts, 1));
      return;
    }
  }

  void processEvents() {
    SDL_Event event {};
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT:
          gShouldQuit.store(true);
          break;
        case SDL_DROPFILE:
          handleDropFile(event.drop.file);
          SDL_free(event.drop.file);
          break;
        case SDL_MOUSEWHEEL:
          if (event.wheel.windowID == SDL_GetWindowID(controlWindow_)) {
            listScroll_ = std::max(0, listScroll_ - event.wheel.y * 36);
          }
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (event.button.windowID == SDL_GetWindowID(controlWindow_)) {
            handleMouseDown(event.button.x, event.button.y);
          }
          break;
        case SDL_MOUSEBUTTONUP:
          if (event.button.windowID == SDL_GetWindowID(controlWindow_)) {
            drag_.active = false;
            drag_.cueIndex = -1;
          }
          break;
        case SDL_MOUSEMOTION:
          if (event.motion.windowID == SDL_GetWindowID(controlWindow_)) {
            handleMouseMotion(event.motion.x, event.motion.y);
          }
          break;
        case SDL_KEYDOWN:
          handleKeyDown(event.key.keysym.sym, event.key.keysym.mod);
          break;
        default:
          break;
      }
    }
  }

  void update() {
    processRemoteCommands();
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      MediaEngine* engine = mediaEngineForDeck(deckIndex);
      if (!engine) {
        continue;
      }
      engine->update();
      if (engine->reachedEnd()) {
        Deck& deck = project_.decks[deckIndex];
        if (deck.activeIndex >= 0 && !deck.cues.empty()) {
          int nextIndex = -1;
          if (deck.activeIndex + 1 < static_cast<int>(deck.cues.size())) {
            nextIndex = deck.activeIndex + 1;
          } else if (deck.playlistLoop) {
            nextIndex = 0;
          }

          if (nextIndex >= 0) {
            if (deck.selectedIndex != nextIndex) {
              deck.selectedIndex = nextIndex;
              if (deckIndex == project_.focusedDeckIndex) {
                onSelectionChanged();
              }
            }
            persistProject();
            if (deck.autoAdvance) {
              int previousFocus = project_.focusedDeckIndex;
              project_.focusedDeckIndex = deckIndex;
              takeSelected(true);
              project_.focusedDeckIndex = previousFocus;
            }
          }
        }
      }
    }
    updateStatusSnapshot();
  }

  void render() {
    animationNow_ = SDL_GetTicks64();
    renderControlWindow();
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      renderOutputWindow(deckIndex);
    }
  }

  void renderControlWindow() {
    const Deck& deck = focusedDeck();
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);
    layoutButtons(height);

    SDL_SetRenderDrawColor(controlRenderer_, red(kShellShadowColor), green(kShellShadowColor), blue(kShellShadowColor), 255);
    SDL_RenderClear(controlRenderer_);

    SDL_Rect shell {10, 10, width - 20, height - 20};
    drawFramedPanel(controlRenderer_, shell, colorFromRgba(kShellOuterColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellInnerColor));

    SDL_Rect sidebar {kPadding + 12, kPadding + 18, kSidebarWidth, height - kPadding * 2 - 26};
    SDL_Rect mainPanel {sidebar.x + sidebar.w + kPadding, sidebar.y, width - sidebar.w - kPadding * 3 - 24, sidebar.h};
    drawFramedPanel(controlRenderer_, sidebar, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));
    drawFramedPanel(controlRenderer_, mainPanel, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));

    drawText(controlRenderer_, fontSmall_, "dot-matrix cue deck", colorFromRgba(kScreenDeepColor), sidebar.x + 20, sidebar.y + 18);
    drawText(controlRenderer_, fontLarge_, project_.title, colorFromRgba(kScreenDeepColor), sidebar.x + 20, sidebar.y + 42);
    drawText(controlRenderer_, fontSmall_, std::string(kAppModelLabel), colorFromRgba(kScreenInkSoftColor), sidebar.x + 20, sidebar.y + 76);
    std::string fxStatus =
      std::string("1 sfx ") + (project_.uiSoundsEnabled ? "on" : "off") +
      "   2 anim " + (project_.uiTransitionsEnabled ? "on" : "off") +
      "   3 auto " + (deck.autoAdvance ? "on" : "off") +
      "   4 plist " + (deck.playlistLoop ? "on" : "off");
    drawText(controlRenderer_, fontSmall_, fxStatus, colorFromRgba(kScreenInkSoftColor), sidebar.x + 190, sidebar.y + 76);
    drawText(controlRenderer_, fontSmall_, "playlist: " + currentProjectLabel(), colorFromRgba(kScreenInkSoftColor), sidebar.x + 20, sidebar.y + 94);
    drawText(controlRenderer_, fontSmall_, "deck: " + deckSummaryLabel(), colorFromRgba(kScreenInkSoftColor), sidebar.x + 20, sidebar.y + 112);
    std::string companionStatus = companionReady_
      ? "companion tcp/udp " + std::to_string(companionPort_)
      : "companion control unavailable";
    drawText(controlRenderer_, fontSmall_, "audio: " + currentAudioOutputLabel(), colorFromRgba(kScreenInkSoftColor), sidebar.x + 20, sidebar.y + sidebar.h - 64);
    drawText(controlRenderer_, fontSmall_, "display: " + std::to_string(deck.outputDisplayIndex + 1), colorFromRgba(kScreenInkSoftColor), sidebar.x + 20, sidebar.y + sidebar.h - 52);
    drawText(controlRenderer_, fontSmall_, "ndi: " + currentNdiOutputLabel(), colorFromRgba(kScreenInkSoftColor), sidebar.x + 20, sidebar.y + sidebar.h - 40);
    drawText(controlRenderer_, fontSmall_, companionStatus, colorFromRgba(kScreenInkSoftColor), sidebar.x + 20, sidebar.y + sidebar.h - 28);

    int listTop = sidebar.y + 128;
    SDL_Rect clipFrame {sidebar.x + 14, listTop - 8, sidebar.w - 28, sidebar.h - 182};
    drawFramedPanel(controlRenderer_, clipFrame, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    SDL_Rect clipRect {clipFrame.x + 10, clipFrame.y + 10, clipFrame.w - 20, clipFrame.h - 20};
    SDL_RenderSetClipRect(controlRenderer_, &clipRect);
    int y = clipRect.y - listScroll_;
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      SDL_Rect row {clipRect.x, y, clipRect.w, kRowHeight};
      renderCueRow(row, index);
      y += kRowHeight + 10;
    }
    SDL_RenderSetClipRect(controlRenderer_, nullptr);
    drawText(controlRenderer_, fontSmall_, "cart shelf", colorFromRgba(kScreenDeepColor), clipFrame.x + 10, clipFrame.y - 22);
    drawSpeakerGrille(controlRenderer_, sidebar.x + sidebar.w - 56, sidebar.y + sidebar.h - 84, 26, 5, colorFromRgba(kScreenDeepColor));

    renderButtons();
    renderMainPanel(mainPanel);
    renderToast(width);
    SDL_RenderPresent(controlRenderer_);
  }

  void renderCueRow(const SDL_Rect& row, int index) {
    if (row.y + row.h < kPadding || row.y > 2000) {
      return;
    }

    const Deck& deck = focusedDeck();
    const auto& cue = deck.cues[index];
    SDL_Color fill = colorFromRgba(kScreenLightColor);
    if (index == deck.selectedIndex) {
      fill = colorFromRgba(kScreenMidColor);
    } else if (index == deck.activeIndex) {
      fill = colorFromRgba(kScreenDarkColor);
    }

    drawFramedPanel(controlRenderer_, row, fill, colorFromRgba(kScreenDeepColor), colorFromRgba(kShellInnerColor));
    if (project_.uiTransitionsEnabled && index == deck.selectedIndex) {
      double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_ - selectionChangedAt_) / 95.0);
      SDL_Color glow {155, 188, 15, static_cast<Uint8>(60 + pulse * 80.0)};
      strokeRect(controlRenderer_, insetRect(row, 1), glow);
    }

    SDL_Rect chip {row.x + 12, row.y + 10, 10, row.h - 20};
    fillRect(controlRenderer_, chip, cue.color);

    SDL_Color ink = index == deck.activeIndex ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
    SDL_Color subInk = index == deck.activeIndex ? colorFromRgba(kShellOuterColor) : colorFromRgba(kScreenInkSoftColor);
    drawText(controlRenderer_, fontBase_, cue.name, ink, row.x + 34, row.y + 13);
    std::string meta = cueKindLabel(cue.kind);
    if (cue.kind == CueKind::Video) {
      meta += "  ";
      meta += formatSeconds(cue.duration);
      if (cue.hasAudio) {
        meta += "  + beep";
      }
    } else if (cue.kind == CueKind::Browser) {
      meta += "  web";
    }
    drawText(controlRenderer_, fontSmall_, meta, subInk, row.x + 34, row.y + 39);
  }

  void renderButtons() {
    for (const auto& button : buttons_) {
      fillRect(controlRenderer_, button.rect, button.fill);
      strokeRect(controlRenderer_, button.rect, button.outline);
      drawCenteredText(controlRenderer_, fontBase_, button.label, button.text, button.rect);
    }
  }

  void renderToast(int windowWidth) {
    if (!project_.uiTransitionsEnabled || !toast_.active) {
      return;
    }

    Uint64 elapsed = animationNow_ - toast_.startedAt;
    if (elapsed >= toast_.durationMs) {
      toast_.active = false;
      return;
    }

    double progress = static_cast<double>(elapsed) / static_cast<double>(toast_.durationMs);
    double intro = easeOutCubic(std::min(progress / 0.2, 1.0));
    double outro = progress > 0.78 ? 1.0 - easeOutCubic((progress - 0.78) / 0.22) : 1.0;
    double visibility = std::min(intro, outro);

    SDL_Rect panel {windowWidth - 344, 36 + static_cast<int>((1.0 - visibility) * -24.0), 300, 58};
    panel.x = windowWidth - 44 - static_cast<int>(300.0 * visibility);
    drawFramedPanel(controlRenderer_, panel, toast_.fill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    drawText(controlRenderer_, fontSmall_, "cute mode", toast_.ink, panel.x + 14, panel.y + 10);
    drawText(controlRenderer_, fontBase_, toast_.message, toast_.ink, panel.x + 14, panel.y + 28);
  }

  void renderDeckTabs(const SDL_Rect& panel) {
    deckTabRects_.clear();
    int cardsPerRow = std::max(1, (panel.w - 52) / 250);
    int cardWidth = std::max(160, (panel.w - 52 - (cardsPerRow - 1) * 12) / cardsPerRow);
    int x = panel.x + 26;
    int y = panel.y + 18;

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      int column = deckIndex % cardsPerRow;
      int row = deckIndex / cardsPerRow;
      SDL_Rect rect {
        x + column * (cardWidth + 12),
        y + row * 62,
        cardWidth,
        50
      };
      deckTabRects_.push_back(rect);

      SDL_Color fill = deckIndex == project_.focusedDeckIndex ? colorFromRgba(kScreenMidColor) : colorFromRgba(kScreenLightColor);
      drawFramedPanel(controlRenderer_, rect, fill, colorFromRgba(kScreenDeepColor), colorFromRgba(kShellInnerColor));
      drawText(controlRenderer_, fontSmall_, project_.decks[deckIndex].name, colorFromRgba(kScreenDeepColor), rect.x + 12, rect.y + 8);
      drawText(controlRenderer_, fontSmall_, deckStatusSummary(deckIndex), colorFromRgba(kScreenInkSoftColor), rect.x + 12, rect.y + 26);
    }
  }

  void renderMainPanel(const SDL_Rect& panel) {
    const Deck& deck = focusedDeck();
    const MediaEngine* engine = focusedMediaEngine();
    const Cue* selectedCue = selectedCuePtr();
    const Cue* activeCue = activeCuePtr();
    renderDeckTabs(panel);
    int x = panel.x + 26;
    int rows = std::max(1, static_cast<int>((project_.decks.size() + std::max(1, (panel.w - 52) / 250) - 1) / std::max(1, (panel.w - 52) / 250)));
    int y = panel.y + 22 + rows * 62;

    drawText(controlRenderer_, fontSmall_, "little screen", colorFromRgba(kScreenDeepColor), x, y);
    drawText(controlRenderer_, fontLarge_, activeCue ? activeCue->name : "No cue loaded", colorFromRgba(kScreenDeepColor), x, y + 22);

    std::string status = transportStatusLabel();
    std::string clock = formatSeconds(engine ? engine->position() : 0.0) + " / " + formatSeconds(engine ? engine->duration() : 0.0);
    drawText(controlRenderer_, fontBase_, status, colorFromRgba(kScreenInkSoftColor), x, y + 70);
    drawText(controlRenderer_, fontMono_, clock, colorFromRgba(kScreenInkSoftColor), x + 150, y + 72);

    progressBarRect_ = {x, y + 108, panel.w - 52, 20};
    drawFramedPanel(controlRenderer_, progressBarRect_, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    double duration = engine ? engine->duration() : 0.0;
    double fraction = duration > 0.0 ? (engine ? engine->position() / duration : 0.0) : 0.0;
    fraction = std::clamp(fraction, 0.0, 1.0);
    SDL_Rect fillBar = insetRect(progressBarRect_, 3);
    fillBar.w = static_cast<int>(std::round(progressBarRect_.w * fraction));
    fillRect(controlRenderer_, fillBar, colorFromRgba(kScreenDarkColor));

    SDL_Rect preview {x, y + 150, panel.w - 52, 300};
    drawFramedPanel(controlRenderer_, preview, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    drawText(controlRenderer_, fontSmall_, "program monitor", colorFromRgba(kScreenDeepColor), preview.x + 16, preview.y + 16);
    drawText(controlRenderer_, fontLarge_, activeCue ? activeCue->name : "waiting...", colorFromRgba(kScreenDeepColor), preview.x + 16, preview.y + 42);
    drawText(controlRenderer_, fontBase_, activeCue ? transportStatusLabel() : "take a cue to wake it up", colorFromRgba(kScreenInkSoftColor), preview.x + 16, preview.y + 88);
    drawText(controlRenderer_, fontSmall_, "output stays in the second window so the live path stays clean.", colorFromRgba(kScreenInkSoftColor), preview.x + 16, preview.y + 126);
    drawText(controlRenderer_, fontSmall_, "ctrl+o open  |  ctrl+s save  |  ctrl+shift+s save as  |  ctrl+n new deck", colorFromRgba(kScreenInkSoftColor), preview.x + 16, preview.y + 150);
    drawText(controlRenderer_, fontSmall_, "tab deck +/-  |  b browser  |  p pattern  |  a audio  |  d display  |  n ndi", colorFromRgba(kScreenInkSoftColor), preview.x + 16, preview.y + 172);
    SDL_Rect fauxScreen {preview.x + 18, preview.y + 190, preview.w - 36, 86};
    drawFramedPanel(controlRenderer_, fauxScreen, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
    drawText(controlRenderer_, fontMono_, activeCue ? ("cue:" + std::to_string(deck.activeIndex + 1)) : "cue:--", colorFromRgba(kScreenDeepColor), fauxScreen.x + 16, fauxScreen.y + 18);
    drawText(controlRenderer_, fontMono_, "vol:" + std::to_string(static_cast<int>(std::round((engine ? engine->volume() : 1.0f) * 100.0f))) + "%  auto:" + (deck.autoAdvance ? std::string("on") : std::string("off")), colorFromRgba(kScreenDeepColor), fauxScreen.x + 16, fauxScreen.y + 44);

    int detailX = x;
    int detailY = preview.y + preview.h + 26;
    drawText(controlRenderer_, fontSmall_, "cart details", colorFromRgba(kScreenDeepColor), detailX, detailY);
    drawText(controlRenderer_, fontBase_, selectedCue ? selectedCue->name : "Drop or import some media", colorFromRgba(kScreenDeepColor), detailX, detailY + 22);

    if (!selectedCue) {
      drawText(controlRenderer_, fontSmall_, "Drop files into the shell or tap LOAD to add some carts.", colorFromRgba(kScreenInkSoftColor), detailX, detailY + 60);
      drawText(controlRenderer_, fontSmall_, "Shift + arrows shuffles the selected cue up or down.", colorFromRgba(kScreenInkSoftColor), detailX, detailY + 84);
      return;
    }

    std::vector<std::string> lines {
      std::string(selectedCue->kind == CueKind::Browser ? "URL: " : "Path: ") + selectedCue->path,
      "Kind: " + cueKindLabel(selectedCue->kind),
      "Dimensions: " + std::to_string(selectedCue->width) + "x" + std::to_string(selectedCue->height),
      "Format: " + selectedCue->formatName,
      "Video codec: " + selectedCue->videoCodec,
      "Audio codec: " + (selectedCue->audioCodec.empty() ? "none" : selectedCue->audioCodec),
      "Duration: " + formatSeconds(selectedCue->duration),
      "Size: " + std::to_string(static_cast<unsigned long long>(selectedCue->sizeBytes / 1024)) + " KB",
      "Fade in: " + formatSeconds(selectedCue->fadeInSeconds) + "   Fade out: " + formatSeconds(selectedCue->fadeOutSeconds),
      std::string("Loop: ") + (selectedCue->loop ? "on" : "off") + "   Hold last: " + (selectedCue->pauseOnLastFrame ? "on" : "off")
    };

    for (size_t i = 0; i < lines.size(); ++i) {
      drawText(controlRenderer_, fontSmall_, lines[i], colorFromRgba(kScreenInkSoftColor), detailX, detailY + 58 + static_cast<int>(i) * 24);
    }
  }

  void renderOutputWindow(int deckIndex) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    const Deck& deck = project_.decks[deckIndex];
    if (!runtime || !runtime->outputWindow || !runtime->outputRenderer || !runtime->mediaEngine) {
      return;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(runtime->outputWindow, &width, &height);
    SDL_SetRenderDrawColor(runtime->outputRenderer, red(kScreenDeepColor), green(kScreenDeepColor), blue(kScreenDeepColor), 255);
    SDL_RenderClear(runtime->outputRenderer);

    SDL_Rect bounds {0, 0, width, height};
    runtime->mediaEngine->render(bounds);

    const Cue* activeCue = activeCuePtr(deckIndex);
    if (!activeCue) {
      SDL_Rect screen {24, 24, width - 48, height - 48};
      drawFramedPanel(runtime->outputRenderer, screen, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(runtime->outputRenderer, fontSmall_, std::string(kOutputTitle), colorFromRgba(kScreenDeepColor), 46, 44);
      drawText(runtime->outputRenderer, fontLarge_, "waiting for a cue", colorFromRgba(kScreenDeepColor), 46, 72);
      drawText(runtime->outputRenderer, fontBase_, "use the control window to take media live.", colorFromRgba(kScreenInkSoftColor), 46, 112);
    } else if (activeCue->kind == CueKind::Browser && !runtime->browserCueLive) {
      SDL_Rect screen {24, 24, width - 48, height - 48};
      drawFramedPanel(runtime->outputRenderer, screen, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(runtime->outputRenderer, fontSmall_, "browser cue standby", colorFromRgba(kScreenDeepColor), 46, 44);
      drawText(runtime->outputRenderer, fontLarge_, activeCue->name, colorFromRgba(kScreenDeepColor), 46, 72);
      drawText(runtime->outputRenderer, fontBase_, "press space or GO to launch the page on this output.", colorFromRgba(kScreenInkSoftColor), 46, 112);
      drawText(runtime->outputRenderer, fontSmall_, activeCue->path, colorFromRgba(kScreenInkSoftColor), 46, 148);
    } else {
      SDL_Rect overlay {24, height - 68, width - 48, 44};
      drawFramedPanel(runtime->outputRenderer, overlay, {155, 188, 15, 185}, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(runtime->outputRenderer, fontBase_, deck.name, colorFromRgba(kScreenDeepColor), overlay.x + 16, overlay.y + 10);
      drawText(runtime->outputRenderer, fontSmall_, transportStatusLabel(deckIndex), colorFromRgba(kScreenInkSoftColor), overlay.x + overlay.w - 140, overlay.y + 13);
    }

    double fpsHint = activeCue && activeCue->kind == CueKind::Video ? std::max(1.0, activeCue->fps) : 30.0;
    sendDeckNdiFrame(deckIndex, width, height, fpsHint);
    SDL_RenderPresent(runtime->outputRenderer);
  }

  void handleMouseDown(int x, int y) {
    const Deck& deck = focusedDeck();
    for (int deckIndex = 0; deckIndex < static_cast<int>(deckTabRects_.size()); ++deckIndex) {
      if (pointInRect(x, y, deckTabRects_[deckIndex])) {
        setFocusedDeckIndex(deckIndex);
        return;
      }
    }
    for (const auto& button : buttons_) {
      if (pointInRect(x, y, button.rect)) {
        triggerButton(button.label);
        return;
      }
    }

    if (pointInRect(x, y, progressBarRect_)) {
      MediaEngine* engine = focusedMediaEngine();
      if (!engine) {
        return;
      }
      double fraction = static_cast<double>(x - progressBarRect_.x) / static_cast<double>(progressBarRect_.w);
      engine->seek(engine->duration() * std::clamp(fraction, 0.0, 1.0));
      return;
    }

    int sidebarY = kPadding + 18;
    int listTop = sidebarY + 128;
    int clipRectX = kPadding + 12 + 14 + 10;
    int clipRectY = listTop - 8 + 10;
    int clipRectW = kSidebarWidth - 28 - 20;
    int listY = clipRectY - listScroll_;
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      SDL_Rect row {clipRectX, listY, clipRectW, kRowHeight};
      if (pointInRect(x, y, row)) {
        if (focusedDeckMutable().selectedIndex != index) {
          focusedDeckMutable().selectedIndex = index;
          onSelectionChanged();
          persistProject();
        }
        drag_.active = true;
        drag_.cueIndex = index;
        return;
      }
      listY += kRowHeight + 10;
    }
  }

  void handleMouseMotion(int x, int y) {
    if (!drag_.active || drag_.cueIndex < 0) {
      return;
    }
    Deck& deck = focusedDeckMutable();
    int sidebarY = kPadding + 18;
    int listTop = sidebarY + 128;
    int clipRectX = kPadding + 12 + 14 + 10;
    int clipRectY = listTop - 8 + 10;
    int clipRectW = kSidebarWidth - 28 - 20;
    int listY = clipRectY - listScroll_;
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      SDL_Rect row {clipRectX, listY, clipRectW, kRowHeight};
      if (pointInRect(x, y, row) && index != drag_.cueIndex) {
        auto cue = deck.cues[drag_.cueIndex];
        deck.cues.erase(deck.cues.begin() + drag_.cueIndex);
        deck.cues.insert(deck.cues.begin() + index, cue);
        deck.selectedIndex = index;
        if (deck.activeIndex == drag_.cueIndex) {
          deck.activeIndex = index;
        } else if (deck.activeIndex >= 0) {
          if (drag_.cueIndex < deck.activeIndex && index >= deck.activeIndex) {
            deck.activeIndex -= 1;
          } else if (drag_.cueIndex > deck.activeIndex && index <= deck.activeIndex) {
            deck.activeIndex += 1;
          }
        }
        drag_.cueIndex = index;
        triggerToast("cart shuffle");
        persistProject();
        return;
      }
      listY += kRowHeight + 10;
    }
  }

  void handleKeyDown(SDL_Keycode key, Uint16 mod) {
    bool ctrl = (mod & KMOD_CTRL) != 0;
    bool shift = (mod & KMOD_SHIFT) != 0;

    if (ctrl && key == SDLK_o) {
      openProjectFromPicker();
      return;
    }
    if (ctrl && key == SDLK_n) {
      addDeck();
      return;
    }
    if (ctrl && !shift && key == SDLK_s) {
      persistProject();
      triggerToast("playlist saved");
      return;
    }
    if (ctrl && shift && key == SDLK_s) {
      saveProjectAsFromPicker();
      return;
    }

    switch (key) {
      case SDLK_ESCAPE:
        gShouldQuit.store(true);
        break;
      case SDLK_TAB:
        cycleFocusedDeck(shift ? -1 : 1);
        break;
      case SDLK_UP:
        selectRelative(-1, shift);
        break;
      case SDLK_DOWN:
        selectRelative(1, shift);
        break;
      case SDLK_1:
        toggleUiSounds();
        break;
      case SDLK_2:
        toggleUiTransitions();
        break;
      case SDLK_3:
        toggleAutoAdvance();
        break;
      case SDLK_4:
        togglePlaylistLoop();
        break;
      case SDLK_RETURN:
      case SDLK_KP_ENTER:
        takeSelected(false);
        break;
      case SDLK_SPACE:
        toggleTransport();
        break;
      case SDLK_s:
        stopTransport();
        break;
      case SDLK_c:
        clearOutput();
        break;
      case SDLK_f:
        toggleOutputFullscreen();
        break;
      case SDLK_i:
        importWithPicker();
        break;
      case SDLK_b:
        addBrowserCueFromPrompt();
        break;
      case SDLK_p:
        addKawaiiPatternCue();
        break;
      case SDLK_l:
        toggleSelectedLoop();
        break;
      case SDLK_e:
        toggleSelectedPauseOnLastFrame();
        break;
      case SDLK_LEFTBRACKET:
        adjustSelectedFade(!shift, -0.25);
        break;
      case SDLK_RIGHTBRACKET:
        adjustSelectedFade(!shift, 0.25);
        break;
      case SDLK_a:
        cycleAudioOutputDevice(1);
        break;
      case SDLK_d:
        cycleOutputDisplay(1);
        break;
      case SDLK_n:
        toggleFocusedDeckNdi();
        break;
      case SDLK_DELETE:
      case SDLK_BACKSPACE:
        deleteSelected();
        break;
      case SDLK_EQUALS:
      case SDLK_PLUS:
        if (MediaEngine* engine = focusedMediaEngine()) {
          engine->setVolume(engine->volume() + 0.05f);
          triggerToast("speaker up");
        }
        break;
      case SDLK_MINUS:
        if (MediaEngine* engine = focusedMediaEngine()) {
          engine->setVolume(engine->volume() - 0.05f);
          triggerToast("speaker down");
        }
        break;
      default:
        break;
    }
  }

  void triggerButton(const std::string& label) {
    if (label == "Import") {
      importWithPicker();
    } else if (label == "Take") {
      takeSelected(false);
    } else if (label == "Go/Pause") {
      toggleTransport();
    } else if (label == "Stop") {
      stopTransport();
    } else if (label == "Clear") {
      clearOutput();
    } else if (label == "Fullscreen") {
      toggleOutputFullscreen();
    } else if (label == "Delete") {
      deleteSelected();
    } else if (label == "SFX") {
      toggleUiSounds();
    } else if (label == "ANIM") {
      toggleUiTransitions();
    }
  }

  std::string transportStatusLabel(int deckIndex) const {
    const Cue* activeCue = activeCuePtr(deckIndex);
    const DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (activeCue && activeCue->kind == CueKind::Browser) {
      return runtime && runtime->browserCueLive ? "Live Browser" : "Browser Ready";
    }
    const MediaEngine* engine = mediaEngineForDeck(deckIndex);
    return engine ? transportLabel(engine->state()) : transportLabel(TransportState::Stopped);
  }

  std::string transportStatusLabel() const {
    return transportStatusLabel(project_.focusedDeckIndex);
  }

  void playTransport() {
    MediaEngine* engine = focusedMediaEngine();
    DeckRuntime* runtime = focusedRuntime();
    const Cue* activeCue = activeCuePtr();
    if (!activeCue && selectedCuePtr()) {
      takeSelected(true);
      return;
    }
    if (!activeCue || !engine || !runtime) {
      return;
    }
    if (activeCue->kind == CueKind::Browser) {
      if (startBrowserCue(project_.focusedDeckIndex, *activeCue)) {
        playUiSound(UiSoundEffect::Toggle);
      }
      return;
    }
    engine->play();
    triggerToast("rolling");
    playUiSound(UiSoundEffect::Toggle);
  }

  void pauseTransport() {
    MediaEngine* engine = focusedMediaEngine();
    DeckRuntime* runtime = focusedRuntime();
    const Cue* activeCue = activeCuePtr();
    if (activeCue && runtime && activeCue->kind == CueKind::Browser) {
      stopBrowserCue();
      triggerToast("browser parked");
      playUiSound(UiSoundEffect::Toggle);
      return;
    }
    if (!engine) {
      return;
    }
    engine->pause();
    triggerToast("tiny pause");
    playUiSound(UiSoundEffect::Toggle);
  }

  void stopTransport() {
    MediaEngine* engine = focusedMediaEngine();
    DeckRuntime* runtime = focusedRuntime();
    const Cue* activeCue = activeCuePtr();
    if (!engine) {
      return;
    }
    if (activeCue && runtime && activeCue->kind == CueKind::Browser) {
      stopBrowserCue();
      engine->stop();
      triggerToast("browser parked");
      playUiSound(UiSoundEffect::Stop);
      return;
    }
    engine->stop();
    triggerToast("rewound");
    playUiSound(UiSoundEffect::Stop);
  }

  void toggleTransport() {
    MediaEngine* engine = focusedMediaEngine();
    DeckRuntime* runtime = focusedRuntime();
    const Cue* activeCue = activeCuePtr();
    if (!activeCue && selectedCuePtr()) {
      takeSelected(true);
      return;
    }
    if (!activeCue || !engine || !runtime) {
      return;
    }
    if (activeCue->kind == CueKind::Browser) {
      if (runtime->browserCueLive) {
        pauseTransport();
      } else {
        playTransport();
      }
      return;
    }
    engine->toggle();
    triggerToast(engine->state() == TransportState::Playing ? "go go go" : "tiny pause");
    playUiSound(UiSoundEffect::Toggle);
  }

  void clearOutput() {
    MediaEngine* engine = focusedMediaEngine();
    stopBrowserCue();
    focusedDeckMutable().activeIndex = -1;
    if (engine) {
      engine->clear();
    }
    triggerToast("screen cleared");
    playUiSound(UiSoundEffect::Clear);
    persistProject();
  }

  void takeSelected(bool autoplay) {
    Deck& deck = focusedDeckMutable();
    MediaEngine* engine = focusedMediaEngine();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    if (!engine) {
      return;
    }
    deck.activeIndex = deck.selectedIndex;
    const Cue& cue = deck.cues[deck.activeIndex];
    stopBrowserCue();
    engine->loadCue(&cue, autoplay);
    if (cue.kind == CueKind::Browser) {
      startBrowserCue(project_.focusedDeckIndex, cue);
      triggerToast("browser jumped live");
    } else {
      triggerToast(autoplay ? "cue jumped live" : "cue loaded");
    }
    playUiSound(UiSoundEffect::Take);
    persistProject();
  }

  void selectRelative(int direction, bool reorder) {
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) {
      return;
    }

    if (deck.selectedIndex < 0) {
      deck.selectedIndex = 0;
      onSelectionChanged();
      persistProject();
      return;
    }

    int nextIndex = std::clamp(deck.selectedIndex + direction, 0, static_cast<int>(deck.cues.size()) - 1);
    if (reorder && nextIndex != deck.selectedIndex) {
      std::swap(deck.cues[deck.selectedIndex], deck.cues[nextIndex]);
      if (deck.activeIndex == deck.selectedIndex) {
        deck.activeIndex = nextIndex;
      } else if (deck.activeIndex == nextIndex) {
        deck.activeIndex = deck.selectedIndex;
      }
      triggerToast("cart shuffled");
      playUiSound(UiSoundEffect::Toggle);
    }
    if (deck.selectedIndex != nextIndex) {
      deck.selectedIndex = nextIndex;
      onSelectionChanged();
    }
    persistProject();
  }

  Cue* selectedCueMutable() {
    Deck& deck = focusedDeckMutable();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.selectedIndex];
  }

  void toggleSelectedLoop() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video) {
      return;
    }
    cue->loop = !cue->loop;
    triggerToast(cue->loop ? "loop on" : "loop off");
    playUiSound(UiSoundEffect::Toggle);
    persistProject();
  }

  void setSelectedLoop(bool enabled) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video || cue->loop == enabled) {
      return;
    }
    cue->loop = enabled;
    triggerToast(cue->loop ? "loop on" : "loop off");
    playUiSound(UiSoundEffect::Toggle);
    persistProject();
  }

  void toggleSelectedPauseOnLastFrame() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video) {
      return;
    }
    cue->pauseOnLastFrame = !cue->pauseOnLastFrame;
    triggerToast(cue->pauseOnLastFrame ? "hold frame on" : "hold frame off");
    playUiSound(UiSoundEffect::Toggle);
    persistProject();
  }

  void setSelectedPauseOnLastFrame(bool enabled) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video || cue->pauseOnLastFrame == enabled) {
      return;
    }
    cue->pauseOnLastFrame = enabled;
    triggerToast(cue->pauseOnLastFrame ? "hold frame on" : "hold frame off");
    playUiSound(UiSoundEffect::Toggle);
    persistProject();
  }

  void adjustSelectedFade(bool fadeIn, double deltaSeconds) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video) {
      return;
    }
    double& target = fadeIn ? cue->fadeInSeconds : cue->fadeOutSeconds;
    target = std::clamp(target + deltaSeconds, 0.0, 10.0);
    triggerToast(std::string(fadeIn ? "fade in " : "fade out ") + formatSeconds(target));
    persistProject();
  }

  void setSelectedFade(bool fadeIn, double seconds) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video) {
      return;
    }
    double& target = fadeIn ? cue->fadeInSeconds : cue->fadeOutSeconds;
    double next = std::clamp(seconds, 0.0, 10.0);
    if (std::abs(target - next) < 0.001) {
      return;
    }
    target = next;
    triggerToast(std::string(fadeIn ? "fade in " : "fade out ") + formatSeconds(target));
    persistProject();
  }

  void deleteSelected() {
    Deck& deck = focusedDeckMutable();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    int index = deck.selectedIndex;
    bool removedActive = index == deck.activeIndex;
    deck.cues.erase(deck.cues.begin() + index);
    if (deck.cues.empty()) {
      deck.selectedIndex = -1;
      deck.activeIndex = -1;
    } else {
      deck.selectedIndex = std::min(index, static_cast<int>(deck.cues.size()) - 1);
      if (deck.activeIndex > index) {
        deck.activeIndex -= 1;
      } else if (removedActive) {
        deck.activeIndex = -1;
        if (MediaEngine* engine = focusedMediaEngine()) {
          engine->clear();
        }
      }
    }
    triggerToast("cart popped");
    playUiSound(UiSoundEffect::Delete);
    persistProject();
  }

  void handleDropFile(const char* rawPath) {
    if (!rawPath) {
      return;
    }
    importPaths({rawPath});
  }

  void importWithPicker() {
    auto result = pickFiles();
    if (result.empty()) {
      return;
    }
    importPaths(result);
  }

  std::optional<fs::path> pickProjectPath(bool saveMode) {
#ifdef _WIN32
    std::string script = saveMode
      ? "Add-Type -AssemblyName System.Windows.Forms;$dialog = New-Object System.Windows.Forms.SaveFileDialog;$dialog.Filter = 'Playboy Playlist (*.playboy)|*.playboy';if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 };$dialog.FileName"
      : "Add-Type -AssemblyName System.Windows.Forms;$dialog = New-Object System.Windows.Forms.OpenFileDialog;$dialog.Filter = 'Playboy Playlist (*.playboy)|*.playboy';if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 };$dialog.FileName";
    auto text = readAllText({"powershell.exe", "-NoProfile", "-Command", script});
#elif __APPLE__
    auto text = saveMode
      ? readAllText({
          "osascript",
          "-e",
          "set targetFile to choose file name with prompt \"Save Playboy playlist\"",
          "-e",
          "return POSIX path of targetFile"
        })
      : readAllText({
          "osascript",
          "-e",
          "set pickedFile to choose file with prompt \"Open Playboy playlist\"",
          "-e",
          "return POSIX path of pickedFile"
        });
#else
    auto text = saveMode
      ? readAllText({
          "zenity",
          "--file-selection",
          "--save",
          "--confirm-overwrite",
          "--title=Save Playboy playlist",
          "--filename",
          currentProjectFile_.string()
        })
      : readAllText({
          "zenity",
          "--file-selection",
          "--title=Open Playboy playlist",
          "--filename",
          currentProjectFile_.string()
        });
#endif
    if (!text) {
      return std::nullopt;
    }
    std::string value = trim(*text);
    if (value.empty()) {
      return std::nullopt;
    }
    return normalizeProjectPath(fs::absolute(value));
  }

  void openProjectFromPath(const fs::path& projectPath) {
    fs::path normalized = normalizeProjectPath(projectPath);
    currentProjectFile_ = normalized;
    project_ = loadProject(normalized);
    normalizeProject(project_);
    selectionChangedAt_ = SDL_GetTicks64();
    if (!rebuildDeckRuntimes()) {
      std::cerr << "Deck runtime creation failed: " << SDL_GetError() << '\n';
    }
    triggerToast("playlist: " + currentProjectLabel());
  }

  void openProjectFromPicker() {
    auto picked = pickProjectPath(false);
    if (!picked) {
      return;
    }
    openProjectFromPath(*picked);
  }

  void saveProjectAsFromPicker() {
    auto picked = pickProjectPath(true);
    if (!picked) {
      return;
    }
    currentProjectFile_ = *picked;
    persistProject();
    triggerToast("saved as " + currentProjectLabel());
  }

  std::optional<std::string> pickBrowserUrl() {
#ifdef _WIN32
    auto text = readAllText({
      "powershell.exe",
      "-NoProfile",
      "-Command",
      "Add-Type -AssemblyName Microsoft.VisualBasic;"
      "[Microsoft.VisualBasic.Interaction]::InputBox('URL or local file path','Add browser cue','https://example.com')"
    });
#elif __APPLE__
    auto text = readAllText({
      "osascript",
      "-e",
      "text returned of (display dialog \"URL or local file path\" default answer \"https://example.com\" with title \"Add browser cue\")"
    });
#else
    auto text = readAllText({
      "zenity",
      "--entry",
      "--title=Add browser cue",
      "--text=URL or local file path",
      "--entry-text=https://example.com"
    });
#endif
    if (!text) {
      return std::nullopt;
    }
    std::string value = normalizeBrowserUrl(trim(*text));
    if (value.empty()) {
      return std::nullopt;
    }
    return value;
  }

  std::vector<std::string> pickFiles() {
#ifdef _WIN32
    auto text = readAllText({
      "powershell.exe",
      "-NoProfile",
      "-Command",
      "Add-Type -AssemblyName System.Windows.Forms;"
      "$dialog = New-Object System.Windows.Forms.OpenFileDialog;"
      "$dialog.Multiselect = $true;"
      "if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 };"
      "$dialog.FileNames -join \"`n\""
    });
#elif __APPLE__
    auto text = readAllText({
      "osascript",
      "-e",
      "set filesPicked to choose file with multiple selections allowed true",
      "-e",
      "set outputLines to {}",
      "-e",
      "repeat with currentFile in filesPicked",
      "-e",
      "set end of outputLines to POSIX path of currentFile",
      "-e",
      "end repeat",
      "-e",
      "set AppleScript's text item delimiters to linefeed",
      "-e",
      "return outputLines as text"
    });
#else
    auto text = readAllText({
      "zenity",
      "--file-selection",
      "--multiple",
      "--separator=|",
      "--title=Import media into Playboy Native"
    });
#endif

    if (!text) {
      return {};
    }

    std::vector<std::string> paths;
#ifdef __linux__
    for (const auto& value : splitByChar(*text, '|')) {
      if (!trim(value).empty()) {
        paths.push_back(trim(value));
      }
    }
#else
    for (const auto& line : splitLines(*text)) {
      if (!trim(line).empty()) {
        paths.push_back(trim(line));
      }
    }
#endif
    return paths;
  }

  void addBrowserCue(const std::string& rawUrl) {
    std::string url = normalizeBrowserUrl(rawUrl);
    if (url.empty()) {
      return;
    }

    Cue cue;
    cue.kind = CueKind::Browser;
    cue.path = url;
    cue.name = browserCueNameForUrl(url);
    cue.width = 1280;
    cue.height = 720;
    cue.color = SDL_Color {139, 172, 15, 255};
    cue.formatName = "browser";
    cue.videoCodec = "chromium";
    cue.audioCodec = "system";
    cue.hasAudio = true;
    Deck& deck = focusedDeckMutable();
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("browser cue added");
    playUiSound(UiSoundEffect::Import);
    persistProject();
  }

  void addBrowserCueFromPrompt() {
    auto url = pickBrowserUrl();
    if (!url) {
      return;
    }
    addBrowserCue(*url);
  }

  void addKawaiiPatternCue() {
    Cue cue;
    cue.kind = CueKind::Pattern;
    cue.path = "pattern://kawaii-pocket";
    cue.name = "Kawaii Pocket Pattern";
    cue.width = 1280;
    cue.height = 720;
    cue.color = colorFromRgba(kScreenDarkColor);
    cue.formatName = "generated";
    Deck& deck = focusedDeckMutable();
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("kawaii test pattern added");
    playUiSound(UiSoundEffect::Import);
    persistProject();
  }

  void importPaths(const std::vector<std::string>& rawPaths) {
    Deck& deck = focusedDeckMutable();
    bool changed = false;
    int addedCount = 0;
    for (const auto& raw : rawPaths) {
      fs::path path = fs::absolute(trim(raw));
      if (!fs::exists(path)) {
        continue;
      }

      auto cue = probeCue(path);
      if (!cue) {
        continue;
      }

      auto duplicate = std::find_if(deck.cues.begin(), deck.cues.end(), [&](const Cue& existing) {
        return existing.path == cue->path;
      });
      if (duplicate != deck.cues.end()) {
        continue;
      }

      deck.cues.push_back(*cue);
      changed = true;
      addedCount += 1;
    }

    if (!changed) {
      return;
    }

    if (deck.selectedIndex < 0 && !deck.cues.empty()) {
      deck.selectedIndex = 0;
      onSelectionChanged();
    }
    triggerToast(addedCount == 1 ? "1 new cart loaded" : std::to_string(addedCount) + " new carts loaded");
    playUiSound(UiSoundEffect::Import);
    persistProject();
  }

  void toggleOutputFullscreen() {
    DeckRuntime* runtime = focusedRuntime();
    if (!runtime || !runtime->outputWindow) {
      return;
    }
    Uint32 flags = SDL_GetWindowFlags(runtime->outputWindow);
    bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    SDL_SetWindowFullscreen(runtime->outputWindow, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    triggerToast(fullscreen ? "tiny screen" : "big screen");
    playUiSound(UiSoundEffect::Toggle);
  }

  void layoutButtons(int windowHeight) {
    buttons_.clear();
    int x = kPadding + 16;
    int y = windowHeight - 74;
    auto push = [&](std::string label, SDL_Color fill) {
      Button button;
      button.label = std::move(label);
      button.rect = {x, y, 118, 44};
      button.fill = fill;
      button.outline = colorFromRgba(kScreenDeepColor);
      button.text = colorFromRgba(kScreenDeepColor);
      buttons_.push_back(button);
      x += button.rect.w + 10;
    };
    push("Import", colorFromRgba(kScreenMidColor));
    push("Take", colorFromRgba(kScreenLightColor));
    push("Go/Pause", colorFromRgba(kScreenMidColor));
    push("Stop", colorFromRgba(kScreenMidColor));
    push("Clear", colorFromRgba(kScreenMidColor));
    push("Fullscreen", colorFromRgba(kScreenMidColor));
    push("Delete", colorFromRgba(kDeleteBezelColor));
    push("SFX", project_.uiSoundsEnabled ? colorFromRgba(kScreenLightColor) : colorFromRgba(kButtonBezelColor));
    push("ANIM", project_.uiTransitionsEnabled ? colorFromRgba(kScreenLightColor) : colorFromRgba(kButtonBezelColor));
  }

  const Cue* selectedCuePtr(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return nullptr;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.selectedIndex];
  }

  const Cue* selectedCuePtr() const {
    const Deck& deck = focusedDeck();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.selectedIndex];
  }

  const Cue* activeCuePtr(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return nullptr;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.activeIndex];
  }

  const Cue* activeCuePtr() const {
    const Deck& deck = focusedDeck();
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.activeIndex];
  }

  void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_Color color, int x, int y) {
    if (!font || text.empty()) {
      return;
    }
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
      return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
      SDL_FreeSurface(surface);
      return;
    }
    SDL_Rect dst {x, y, surface->w, surface->h};
    SDL_FreeSurface(surface);
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
  }

  void drawCenteredText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_Color color, const SDL_Rect& rect) {
    if (!font || text.empty()) {
      return;
    }
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
      return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
      SDL_FreeSurface(surface);
      return;
    }
    SDL_Rect dst {
      rect.x + (rect.w - surface->w) / 2,
      rect.y + (rect.h - surface->h) / 2,
      surface->w,
      surface->h
    };
    SDL_FreeSurface(surface);
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
  }

  SDL_Window* controlWindow_ = nullptr;
  SDL_Renderer* controlRenderer_ = nullptr;
  TTF_Font* fontLarge_ = nullptr;
  TTF_Font* fontBase_ = nullptr;
  TTF_Font* fontSmall_ = nullptr;
  TTF_Font* fontMono_ = nullptr;
  SDL_AudioDeviceID uiAudioDevice_ = 0;
  fs::path currentProjectFile_;
  Project project_;
  std::vector<DeckRuntime> deckRuntimes_;
#if defined(PLAYBOY_HAS_NDI_SDK)
  NdiApi ndiApi_;
#endif
  std::vector<Button> buttons_;
  std::vector<SDL_Rect> deckTabRects_;
  SDL_Rect progressBarRect_ {};
  int listScroll_ = 0;
  DragState drag_;
  ToastState toast_;
  Uint64 animationNow_ = 0;
  Uint64 selectionChangedAt_ = 0;
  int companionPort_ = 5510;
  bool companionReady_ = false;
  std::atomic<bool> companionStop_ {false};
  std::thread companionThread_;
  std::mutex remoteCommandMutex_;
  std::deque<std::string> remoteCommands_;
  std::mutex statusSnapshotMutex_;
  std::string statusSnapshot_;
  std::string statusSnapshotJson_;
  std::vector<std::string> statusDeckSnapshots_;
#ifndef _WIN32
  SocketHandle companionTcpListen_ = kInvalidSocket;
  SocketHandle companionUdpSocket_ = kInvalidSocket;
  std::vector<SocketHandle> companionClients_;
  std::map<SocketHandle, std::string> companionClientBuffers_;
#endif
};

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--self-check") {
    return App::runSelfCheck();
  }

  App app;
  if (!app.init()) {
    app.shutdown();
    return 1;
  }

  app.run();
  app.shutdown();
  return 0;
}
