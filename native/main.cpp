#include <SDL.h>
#include <SDL_ttf.h>

#include "core/constants.hpp"
#include "core/types.hpp"
#include "core/paths.hpp"
#include "core/subprocess.hpp"
#include "platform/capture_backend.hpp"
#include "platform/output_backend.hpp"
#include "render/primitives.hpp"

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
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(PLAYBOY_HAS_NDI_SDK)
#include <Processing.NDI.Lib.h>
#endif

#if defined(PLAYBOY_HAS_ALSA)
#include <alsa/asoundlib.h>
#endif

namespace fs = std::filesystem;

using playboy::core::Paths;

namespace {
  using playboy::render::Primitives;


constexpr Uint16 kAudioFormat = AUDIO_S16SYS;

std::atomic<bool> gShouldQuit = false;

#ifndef _WIN32
class SingleInstanceGuard {
 public:
  bool acquire(const fs::path& path) {
    if (locked_) {
      return true;
    }

    fd_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd_ < 0) {
      lastError_ = std::string("open failed: ") + std::strerror(errno);
      return false;
    }

    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        lastError_ = "another Deckboy instance is already running";
      } else {
        lastError_ = std::string("lock failed: ") + std::strerror(errno);
      }
      ::close(fd_);
      fd_ = -1;
      return false;
    }

    std::string pidText = std::to_string(static_cast<long long>(::getpid())) + "\n";
    (void) ::ftruncate(fd_, 0);
    (void) ::write(fd_, pidText.c_str(), pidText.size());
    locked_ = true;
    return true;
  }

  const std::string& lastError() const {
    return lastError_;
  }

  ~SingleInstanceGuard() {
    if (fd_ >= 0) {
      if (locked_) {
        (void) ::flock(fd_, LOCK_UN);
      }
      ::close(fd_);
      fd_ = -1;
    }
  }

 private:
  std::string lastError_;
  int fd_ = -1;
  bool locked_ = false;
};
#endif

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

std::string ellipsizeToPixelWidth(TTF_Font* font, const std::string& text, int maxWidth) {
  if (!font || maxWidth <= 0 || text.empty()) {
    return "";
  }

  int textW = 0;
  int textH = 0;
  if (TTF_SizeUTF8(font, text.c_str(), &textW, &textH) == 0 && textW <= maxWidth) {
    return text;
  }

  const std::string kEllipsis = "...";
  int ellipsisW = 0;
  if (TTF_SizeUTF8(font, kEllipsis.c_str(), &ellipsisW, &textH) != 0) {
    return text;
  }
  if (ellipsisW > maxWidth) {
    return "";
  }

  std::string clipped = text;
  while (!clipped.empty()) {
    clipped.pop_back();
    std::string candidate = clipped + kEllipsis;
    if (TTF_SizeUTF8(font, candidate.c_str(), &textW, &textH) == 0 && textW <= maxWidth) {
      return candidate;
    }
  }

  return kEllipsis;
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

std::string formatTimecode(double seconds, double fps) {
  double safeFps = std::isfinite(fps) && fps > 1.0 ? fps : 30.0;
  double clamped = std::max(0.0, std::isfinite(seconds) ? seconds : 0.0);
  int totalFrames = static_cast<int>(std::floor(clamped * safeFps + 0.0001));
  int fpsInt = std::max(1, static_cast<int>(std::round(safeFps)));
  int frame = totalFrames % fpsInt;
  int totalSeconds = totalFrames / fpsInt;
  int secs = totalSeconds % 60;
  int mins = (totalSeconds / 60) % 60;
  int hours = totalSeconds / 3600;
  std::ostringstream output;
  output << std::setfill('0')
         << std::setw(2) << hours << ':'
         << std::setw(2) << mins << ':'
         << std::setw(2) << secs << ':'
         << std::setw(2) << frame;
  return output.str();
}

std::optional<double> parseTimecodeSeconds(std::string value, double fps) {
  value = trim(value);
  if (value.empty()) {
    return std::nullopt;
  }

  if (value.find(':') == std::string::npos) {
    try {
      return std::max(0.0, std::stod(value));
    } catch (...) {
      return std::nullopt;
    }
  }

  auto parts = splitByChar(value, ':');
  if (parts.size() < 2 || parts.size() > 4) {
    return std::nullopt;
  }

  auto parseIntPart = [&](const std::string& part) -> std::optional<int> {
    try {
      return std::max(0, std::stoi(part));
    } catch (...) {
      return std::nullopt;
    }
  };

  double safeFps = std::isfinite(fps) && fps > 1.0 ? fps : 30.0;
  int hours = 0;
  int mins = 0;
  int secs = 0;
  int frames = 0;

  if (parts.size() == 4) {
    auto h = parseIntPart(parts[0]);
    auto m = parseIntPart(parts[1]);
    auto s = parseIntPart(parts[2]);
    auto f = parseIntPart(parts[3]);
    if (!h || !m || !s || !f) {
      return std::nullopt;
    }
    hours = *h;
    mins = *m;
    secs = *s;
    frames = *f;
  } else if (parts.size() == 3) {
    auto h = parseIntPart(parts[0]);
    auto m = parseIntPart(parts[1]);
    auto s = parseIntPart(parts[2]);
    if (!h || !m || !s) {
      return std::nullopt;
    }
    hours = *h;
    mins = *m;
    secs = *s;
  } else {
    auto m = parseIntPart(parts[0]);
    auto s = parseIntPart(parts[1]);
    if (!m || !s) {
      return std::nullopt;
    }
    mins = *m;
    secs = *s;
  }

  double result = static_cast<double>(hours * 3600 + mins * 60 + secs);
  result += static_cast<double>(frames) / safeFps;
  return std::max(0.0, result);
}

std::optional<double> parseNumericExpression(std::string expression) {
  expression = trim(expression);
  if (expression.empty()) {
    return std::nullopt;
  }

  struct Parser {
    const std::string& text;
    size_t pos = 0;

    void skipWs() {
      while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
      }
    }

    std::optional<double> parseExpr() {
      auto lhs = parseTerm();
      if (!lhs) {
        return std::nullopt;
      }
      while (true) {
        skipWs();
        if (pos >= text.size() || (text[pos] != '+' && text[pos] != '-')) {
          break;
        }
        char op = text[pos++];
        auto rhs = parseTerm();
        if (!rhs) {
          return std::nullopt;
        }
        if (op == '+') {
          *lhs += *rhs;
        } else {
          *lhs -= *rhs;
        }
      }
      return lhs;
    }

    std::optional<double> parseTerm() {
      auto lhs = parseFactor();
      if (!lhs) {
        return std::nullopt;
      }
      while (true) {
        skipWs();
        if (pos >= text.size() || (text[pos] != '*' && text[pos] != '/')) {
          break;
        }
        char op = text[pos++];
        auto rhs = parseFactor();
        if (!rhs) {
          return std::nullopt;
        }
        if (op == '*') {
          *lhs *= *rhs;
        } else {
          if (std::fabs(*rhs) < 1e-12) {
            return std::nullopt;
          }
          *lhs /= *rhs;
        }
      }
      return lhs;
    }

    std::optional<double> parseFactor() {
      skipWs();
      if (pos >= text.size()) {
        return std::nullopt;
      }

      char ch = text[pos];
      if (ch == '+' || ch == '-') {
        ++pos;
        auto value = parseFactor();
        if (!value) {
          return std::nullopt;
        }
        if (ch == '-') {
          *value = -*value;
        }
        return value;
      }

      if (ch == '(') {
        ++pos;
        auto value = parseExpr();
        if (!value) {
          return std::nullopt;
        }
        skipWs();
        if (pos >= text.size() || text[pos] != ')') {
          return std::nullopt;
        }
        ++pos;
        return value;
      }

      const char* start = text.c_str() + pos;
      char* end = nullptr;
      double value = std::strtod(start, &end);
      if (end == start) {
        return std::nullopt;
      }
      pos = static_cast<size_t>(end - text.c_str());
      return value;
    }
  };

  Parser parser {expression, 0};
  auto value = parser.parseExpr();
  if (!value) {
    return std::nullopt;
  }
  parser.skipWs();
  if (parser.pos != expression.size() || !std::isfinite(*value)) {
    return std::nullopt;
  }
  return value;
}

std::string cueKindLabel(CueKind kind) {
  switch (kind) {
    case CueKind::Image:      return "Still";
    case CueKind::Pattern:    return "Pattern";
    case CueKind::Browser:    return "Browser";
    case CueKind::WindowSource: return "Window Source";
    case CueKind::Camera:     return "Camera";
    case CueKind::Syphon:     return "Syphon/Spout";
    case CueKind::LowerThird: return "Lower Third";
    case CueKind::Audio:      return "Audio";
    case CueKind::Video:
    default:                  return "Video";
  }
}

std::string cueKindToken(CueKind kind) {
  switch (kind) {
    case CueKind::Image:      return "image";
    case CueKind::Pattern:    return "pattern";
    case CueKind::Browser:    return "browser";
    case CueKind::WindowSource: return "window_source";
    case CueKind::Camera:     return "camera";
    case CueKind::Syphon:     return "syphon";
    case CueKind::LowerThird: return "lower_third";
    case CueKind::Audio:      return "audio";
    case CueKind::Video:
    default:                  return "video";
  }
}

bool isSourceCueKind(CueKind kind) {
  return kind == CueKind::WindowSource
    || kind == CueKind::Camera
    || kind == CueKind::Syphon;
}

std::string sourceCueTokenForKind(CueKind kind) {
  switch (kind) {
    case CueKind::WindowSource: return "window";
    case CueKind::Camera:       return "camera";
    case CueKind::Syphon:       return "syphon";
    default:                    return "source";
  }
}

std::string defaultSourceRefForKind(CueKind kind) {
  switch (kind) {
    case CueKind::WindowSource: return "active-window";
    case CueKind::Camera:       return "default-camera";
    case CueKind::Syphon:       return "default-bus";
    default:                    return "source";
  }
}

std::string sourceCueRefFromCue(const Cue& cue) {
  if (!isSourceCueKind(cue.kind)) {
    return "";
  }
  std::string path = trim(cue.path);
  if (path.rfind("source://", 0) == 0) {
    size_t kindStart = std::string("source://").size();
    size_t slash = path.find('/', kindStart);
    if (slash != std::string::npos && slash + 1 < path.size()) {
      return trim(path.substr(slash + 1));
    }
  }
  return "";
}

std::string cueEndActionToken(CueEndAction a) {
  switch (a) {
    case CueEndAction::Stop:       return "stop";
    case CueEndAction::Loop:       return "loop";
    case CueEndAction::PauseOnLast: return "hold";
    case CueEndAction::AutoNext:   return "next";
    case CueEndAction::Inherit:
    default:                       return "inherit";
  }
}

std::string cueEndActionLabel(CueEndAction a) {
  switch (a) {
    case CueEndAction::Stop:       return "stop";
    case CueEndAction::Loop:       return "loop";
    case CueEndAction::PauseOnLast: return "hold last";
    case CueEndAction::AutoNext:   return "auto next";
    case CueEndAction::Inherit:
    default:                       return "inherit";
  }
}

CueEndAction parseCueEndAction(const std::string& s) {
  if (s == "stop")    return CueEndAction::Stop;
  if (s == "loop")    return CueEndAction::Loop;
  if (s == "hold")    return CueEndAction::PauseOnLast;
  if (s == "next")    return CueEndAction::AutoNext;
  return CueEndAction::Inherit;
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

std::string transitionStyleToken(TransitionStyle style) {
  switch (style) {
    case TransitionStyle::DipBlack:
      return "dip";
    case TransitionStyle::Cut:
      return "cut";
    case TransitionStyle::Crossfade:
    default:
      return "crossfade";
  }
}

TransitionStyle parseTransitionStyleToken(std::string token) {
  token = trim(token);
  std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (token == "CUT") {
    return TransitionStyle::Cut;
  }
  if (token == "DIP" || token == "DIPBLACK" || token == "DIP_BLACK") {
    return TransitionStyle::DipBlack;
  }
  return TransitionStyle::Crossfade;
}

std::string normalizeJumpModeToken(std::string token) {
  token = trim(token);
  std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (token == "LOAD" || token == "ARM" || token == "READY") {
    return "load";
  }
  return "trigger";
}

std::string jumpModeLabelFromToken(const std::string& token) {
  return normalizeJumpModeToken(token) == "load" ? "Load" : "Trigger";
}

std::string normalizePanicProfileToken(std::string token) {
  token = trim(token);
  std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (token == "FADE_PAUSE" || token == "FADEPAUSE" || token == "PAUSE") {
    return "fade_pause";
  }
  if (token == "FADE_REWIND" || token == "FADEREWIND" || token == "REWIND" || token == "STOP") {
    return "fade_rewind";
  }
  if (token == "FADE_LOAD_NEXT" || token == "FADELOADNEXT" || token == "LOAD_NEXT" || token == "LOADNEXT" || token == "NEXT") {
    return "fade_load_next";
  }
  return "outputs_off";
}

std::string panicProfileLabelFromToken(const std::string& token) {
  std::string normalized = normalizePanicProfileToken(token);
  if (normalized == "fade_pause") {
    return "Fade+Pause";
  }
  if (normalized == "fade_rewind") {
    return "Fade+Rewind";
  }
  if (normalized == "fade_load_next") {
    return "Fade+LoadNext";
  }
  return "Outputs Off";
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

SDL_Color colorTagToSdl(const std::string& tag, Uint8 alpha = 255) {
  if (tag == "red")    return {180,  40,  40, alpha};
  if (tag == "orange") return {190, 100,  20, alpha};
  if (tag == "yellow") return {160, 145,  10, alpha};
  if (tag == "cyan")   return { 15, 140, 140, alpha};
  if (tag == "blue")   return { 20,  60, 175, alpha};
  if (tag == "purple") return {110,  30, 150, alpha};
  if (tag == "pink")   return {175,  45, 115, alpha};
  return {48, 98, 48, alpha};
}

std::string nextColorTag(const std::string& current) {
  static const std::vector<std::string> kTags =
    {"", "red", "orange", "yellow", "cyan", "blue", "purple", "pink"};
  auto it = std::find(kTags.begin(), kTags.end(), current);
  if (it == kTags.end() || std::next(it) == kTags.end()) return "";
  return *std::next(it);
}

std::string normalizeCueIdShort(std::string value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') {
      out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
  }
  if (out.size() > 6) {
    out.resize(6);
  }
  return out;
}

std::string cueDisplayToken(const Cue& cue, int index) {
  if (!trim(cue.cueId).empty()) {
    return cue.cueId;
  }
  if (!trim(cue.cueNumber).empty()) {
    return cue.cueNumber;
  }
  return std::to_string(index + 1);
}

double normalizePlaylistTimebaseFps(double fps) {
  static constexpr std::array<double, 4> kChoices {24.0, 25.0, 29.97, 30.0};
  double candidate = std::isfinite(fps) ? fps : 30.0;
  double best = kChoices.front();
  double bestDiff = std::fabs(candidate - best);
  for (double choice : kChoices) {
    double diff = std::fabs(candidate - choice);
    if (diff < bestDiff) {
      best = choice;
      bestDiff = diff;
    }
  }
  return best;
}

std::string playlistTimebaseLabel(double fps) {
  double normalized = normalizePlaylistTimebaseFps(fps);
  if (std::fabs(normalized - 29.97) < 0.01) {
    return "29.97";
  }
  int whole = static_cast<int>(std::lround(normalized));
  return std::to_string(whole);
}

bool isDefaultStillDurationCueKind(CueKind kind) {
  return kind == CueKind::Image
    || kind == CueKind::Pattern
    || kind == CueKind::Browser
    || kind == CueKind::LowerThird;
}

bool colorControlsActive(float brightness, float contrast, float saturation, float hueShiftDegrees) {
  constexpr float kEpsilon = 0.001f;
  return std::fabs(brightness - 1.0f) > kEpsilon
    || std::fabs(contrast - 1.0f) > kEpsilon
    || std::fabs(saturation - 1.0f) > kEpsilon
    || std::fabs(hueShiftDegrees) > kEpsilon;
}

bool cueHasColorControls(const Cue& cue) {
  return colorControlsActive(cue.brightness, cue.contrast, cue.saturation, cue.hueShift);
}

bool cueHasPixelEffects(const Cue& cue) {
  return cue.chromaKeyEnabled || cueHasColorControls(cue);
}

void applyChromaKeyToPixels(std::vector<std::uint8_t>& pixels,
                            SDL_Color keyColor,
                            float tolerance,
                            float softness) {
  if (pixels.empty()) {
    return;
  }
  tolerance = std::clamp(tolerance, 0.0f, 441.0f);
  softness = std::clamp(softness, 0.0f, 200.0f);
  float inner = std::max(0.0f, tolerance - softness);
  float outer = tolerance + softness;
  float span = std::max(0.0001f, outer - inner);
  for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
    float dr = static_cast<float>(pixels[i + 0]) - static_cast<float>(keyColor.r);
    float dg = static_cast<float>(pixels[i + 1]) - static_cast<float>(keyColor.g);
    float db = static_cast<float>(pixels[i + 2]) - static_cast<float>(keyColor.b);
    float distance = std::sqrt(dr * dr + dg * dg + db * db);
    float keep = 1.0f;
    if (distance <= inner) {
      keep = 0.0f;
    } else if (distance < outer) {
      keep = (distance - inner) / span;
    }
    pixels[i + 3] = static_cast<std::uint8_t>(std::clamp(
      static_cast<int>(std::lround(static_cast<float>(pixels[i + 3]) * keep)),
      0,
      255));
  }
}

void applyColorControlsToPixels(std::vector<std::uint8_t>& pixels,
                                float brightness,
                                float contrast,
                                float saturation,
                                float hueShiftDegrees) {
  if (pixels.empty()) {
    return;
  }
  brightness = std::clamp(brightness, 0.0f, 2.0f);
  contrast = std::clamp(contrast, 0.0f, 2.0f);
  saturation = std::clamp(saturation, 0.0f, 2.0f);
  hueShiftDegrees = std::clamp(hueShiftDegrees, -180.0f, 180.0f);
  if (!colorControlsActive(brightness, contrast, saturation, hueShiftDegrees)) {
    return;
  }

  constexpr float kPi = 3.14159265358979323846f;
  float hueRadians = hueShiftDegrees * (kPi / 180.0f);
  float cosHue = std::cos(hueRadians);
  float sinHue = std::sin(hueRadians);
  bool applyHue = std::fabs(hueShiftDegrees) > 0.001f;

  auto toByte = [](float value) -> std::uint8_t {
    float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
  };

  for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
    float r = static_cast<float>(pixels[i + 0]) / 255.0f;
    float g = static_cast<float>(pixels[i + 1]) / 255.0f;
    float b = static_cast<float>(pixels[i + 2]) / 255.0f;

    r *= brightness;
    g *= brightness;
    b *= brightness;

    r = (r - 0.5f) * contrast + 0.5f;
    g = (g - 0.5f) * contrast + 0.5f;
    b = (b - 0.5f) * contrast + 0.5f;

    float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    r = luma + (r - luma) * saturation;
    g = luma + (g - luma) * saturation;
    b = luma + (b - luma) * saturation;

    if (applyHue) {
      float y = 0.299f * r + 0.587f * g + 0.114f * b;
      float iCh = 0.596f * r - 0.274f * g - 0.322f * b;
      float qCh = 0.211f * r - 0.523f * g + 0.312f * b;
      float iRot = iCh * cosHue - qCh * sinHue;
      float qRot = iCh * sinHue + qCh * cosHue;
      r = y + 0.956f * iRot + 0.621f * qRot;
      g = y - 0.272f * iRot - 0.647f * qRot;
      b = y - 1.106f * iRot + 1.703f * qRot;
    }

    pixels[i + 0] = toByte(r);
    pixels[i + 1] = toByte(g);
    pixels[i + 2] = toByte(b);
  }
}

void applyCueVisualEffectsToPixels(std::vector<std::uint8_t>& pixels,
                                   bool chromaKeyEnabled,
                                   SDL_Color chromaKeyColor,
                                   float chromaKeyTolerance,
                                   float chromaKeySoftness,
                                   float brightness,
                                   float contrast,
                                   float saturation,
                                   float hueShiftDegrees) {
  if (pixels.empty()) {
    return;
  }
  if (chromaKeyEnabled) {
    applyChromaKeyToPixels(pixels, chromaKeyColor, chromaKeyTolerance, chromaKeySoftness);
  }
  applyColorControlsToPixels(pixels, brightness, contrast, saturation, hueShiftDegrees);
}

void applyCueVisualEffectsToPixels(std::vector<std::uint8_t>& pixels, const Cue& cue) {
  applyCueVisualEffectsToPixels(
    pixels,
    cue.chromaKeyEnabled,
    cue.chromaKeyColor,
    cue.chromaKeyTolerance,
    cue.chromaKeySoftness,
    cue.brightness,
    cue.contrast,
    cue.saturation,
    cue.hueShift
  );
}

SDL_Rect insetRect(const SDL_Rect& rect, int amount) {
  return {
    rect.x + amount,
    rect.y + amount,
    std::max(0, rect.w - amount * 2),
    std::max(0, rect.h - amount * 2)
  };
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

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool endsWith(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string normalizePatternTypeId(std::string value) {
  value = toLower(trim(std::move(value)));
  if (value.rfind("pattern://", 0) == 0) {
    value = value.substr(10);
  }
  std::replace(value.begin(), value.end(), '_', '-');
  if (value == "colourbars" || value == "colorbars" || value == "smpte75") {
    value = "smpte-bars";
  } else if (value == "kawaii" || value == "kawaii-pocket") {
    value = "pocket-test";
  } else if (value == "pocket-daytime" || value == "pocket-sunny") {
    value = "pocket-day";
  } else if (value == "pocket-dusk" || value == "pocket-golden") {
    value = "pocket-sunset";
  } else if (value == "pocket-moon" || value == "pocket-evening") {
    value = "pocket-night";
  } else if (value == "pocket-rain" || value == "pocket-tempest") {
    value = "pocket-storm";
  } else if (value == "smpte-bars-animated") {
    value = "smpte-bars-motion";
  } else if (value == "crosshatch-animated") {
    value = "crosshatch-motion";
  } else if (value == "checkerboard-animated") {
    value = "checkerboard-motion";
  } else if (value == "full-white-animated") {
    value = "full-white-motion";
  } else if (value == "full-black-animated") {
    value = "full-black-motion";
  } else if (value == "full-red-animated") {
    value = "full-red-motion";
  } else if (value == "full-green-animated") {
    value = "full-green-motion";
  } else if (value == "full-blue-animated") {
    value = "full-blue-motion";
  }
  return value;
}

std::string stripPatternMotionSuffix(std::string typeId) {
  typeId = normalizePatternTypeId(std::move(typeId));
  if (endsWith(typeId, "-motion")) {
    typeId = typeId.substr(0, typeId.size() - 7);
  }
  return typeId;
}

bool patternTypeSupportsMotion(const std::string& typeId) {
  std::string base = stripPatternMotionSuffix(typeId);
  return base == "smpte-bars" || base == "crosshatch" || base == "checkerboard" ||
         base == "full-white" || base == "full-black" || base == "full-red" ||
         base == "full-green" || base == "full-blue";
}

bool patternTypeIsAnimated(const std::string& typeId) {
  std::string normalized = normalizePatternTypeId(typeId);
  return normalized.rfind("pocket-", 0) == 0 ||
         normalized.find("kawaii") != std::string::npos ||
         endsWith(normalized, "-motion");
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

using OscArg = std::variant<std::int32_t, float, std::string, bool>;

struct OscMessage {
  std::string address;
  std::vector<OscArg> args;
};

struct OscQueryEndpointDoc {
  const char* path;
  const char* command;
  const char* args;
  const char* notes;
};

constexpr std::array<OscQueryEndpointDoc, 33> kOscQueryEndpoints {{
  {"/play", "PLAY", "", "Start focused deck"},
  {"/pause", "PAUSE", "", "Pause focused deck"},
  {"/stop", "STOP", "", "Stop focused deck"},
  {"/go", "GO", "", "Toggle play/pause"},
  {"/toggle", "GO", "", "Alias for GO"},
  {"/take", "TAKE [cue]", "string", "Take selected or cue token"},
  {"/takeid", "TAKEID <cue-id>", "string", "Take by operator cue ID"},
  {"/goto", "GOTO <cue>", "string", "Load/play cue index/token"},
  {"/next", "NEXT", "", "Select next cue"},
  {"/prev", "PREV", "", "Select previous cue"},
  {"/select", "SELECT <cue>", "string", "Select cue without taking"},
  {"/selectid", "SELECTID <cue-id>", "string", "Select by cue ID"},
  {"/deck", "DECK <index>", "int/string", "Focus deck"},
  {"/deck/next", "DECKNEXT", "", "Cycle focused deck forward"},
  {"/deck/prev", "DECKPREV", "", "Cycle focused deck backward"},
  {"/deck/opacity", "DECKOPACITY [0..100]", "number", "Set/query deck opacity"},
  {"/deck/autofade", "DECKAUTOFADE [on|off]", "toggle", "Toggle deck auto fade"},
  {"/deck/fade", "DECKFADE [seconds]", "number", "Set/query deck auto-fade time"},
  {"/route", "ROUTE <output>", "string", "Route focused deck"},
  {"/layer", "LAYER <index>", "int", "Set deck layer index"},
  {"/cue/audio", "CUEAUDIO [on|off]", "toggle", "Per-cue audio enable"},
  {"/cue/pausebegin", "CUEPAUSEBEGIN [on|off]", "toggle", "Per-cue pause at beginning"},
  {"/cue/pauseend", "CUEPAUSEEND [on|off]", "toggle", "Per-cue pause at end"},
  {"/cue/transition", "CUENEXTTRANS [on|off]", "toggle", "Per-cue transition-to-next"},
  {"/cue/goto", "CUEGOTO [target]", "string", "Per-cue goto target token"},
  {"/jumpmode", "JUMPMODE [TRIGGER|LOAD]", "string", "Jump behavior"},
  {"/panic", "PANIC [profile]", "string", "Run panic profile"},
  {"/output", "OUTPUT <index> ...", "string", "Output command namespace"},
  {"/status", "STATUS", "", "Request status snapshot"},
  {"/state", "STATE", "", "Request status snapshot"},
  {"/oscquery", "OSCQUERY [on|off]", "toggle", "Enable OSC Query HTTP server"},
  {"/oscquery/port", "OSCQUERYPORT [port]", "number", "Set/query OSC Query HTTP port"},
  {"/osc/feedback", "OSCFEEDBACK [on|off]", "toggle", "Enable canonical OSC feedback mirror"}
}};

size_t alignOscOffset(size_t offset) {
  return (offset + 3u) & ~size_t(3u);
}

bool readOscString(const std::uint8_t* bytes, size_t size, size_t& offset, std::string& out) {
  if (offset >= size) {
    return false;
  }
  size_t end = offset;
  while (end < size && bytes[end] != 0) {
    ++end;
  }
  if (end >= size) {
    return false;
  }
  out.assign(reinterpret_cast<const char*>(bytes + offset), end - offset);
  offset = alignOscOffset(end + 1u);
  return offset <= size;
}

std::uint32_t readOscU32(const std::uint8_t* bytes, size_t offset) {
  return
    (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
    (static_cast<std::uint32_t>(bytes[offset + 1]) << 16u) |
    (static_cast<std::uint32_t>(bytes[offset + 2]) << 8u) |
    (static_cast<std::uint32_t>(bytes[offset + 3]));
}

void appendOscU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void appendOscString(std::vector<std::uint8_t>& out, const std::string& value) {
  out.insert(out.end(), value.begin(), value.end());
  out.push_back(0);
  while (out.size() % 4u != 0u) {
    out.push_back(0);
  }
}

std::vector<std::uint8_t> buildOscStringMessage(const std::string& address, const std::string& value) {
  std::vector<std::uint8_t> bytes;
  appendOscString(bytes, address);
  appendOscString(bytes, ",s");
  appendOscString(bytes, value);
  return bytes;
}

std::optional<OscMessage> parseOscMessage(const std::string& payload) {
  if (payload.empty() || payload[0] != '/') {
    return std::nullopt;
  }
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
  size_t size = payload.size();
  size_t offset = 0;

  OscMessage message;
  if (!readOscString(bytes, size, offset, message.address) || message.address.empty()) {
    return std::nullopt;
  }

  std::string typeTags;
  if (!readOscString(bytes, size, offset, typeTags) || typeTags.empty() || typeTags[0] != ',') {
    return std::nullopt;
  }

  for (size_t index = 1; index < typeTags.size(); ++index) {
    char tag = typeTags[index];
    switch (tag) {
      case 'i': {
        if (offset + 4u > size) {
          return std::nullopt;
        }
        message.args.emplace_back(static_cast<std::int32_t>(readOscU32(bytes, offset)));
        offset += 4u;
        break;
      }
      case 'f': {
        if (offset + 4u > size) {
          return std::nullopt;
        }
        std::uint32_t raw = readOscU32(bytes, offset);
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(float));
        message.args.emplace_back(value);
        offset += 4u;
        break;
      }
      case 's': {
        std::string value;
        if (!readOscString(bytes, size, offset, value)) {
          return std::nullopt;
        }
        message.args.emplace_back(value);
        break;
      }
      case 'T':
        message.args.emplace_back(true);
        break;
      case 'F':
        message.args.emplace_back(false);
        break;
      default:
        return std::nullopt;
    }
  }

  return message;
}

std::vector<OscMessage> parseOscPacket(const std::string& payload) {
  std::vector<OscMessage> messages;
  if (payload.empty()) {
    return messages;
  }

  const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
  size_t size = payload.size();
  if (size >= 16 && std::memcmp(bytes, "#bundle\0", 8) == 0) {
    size_t offset = 16;  // "#bundle\0" + timetag (8 bytes)
    while (offset + 4u <= size) {
      std::uint32_t elementSize = readOscU32(bytes, offset);
      offset += 4u;
      if (elementSize == 0 || offset + elementSize > size) {
        break;
      }
      std::string element(payload.data() + offset, payload.data() + offset + elementSize);
      auto nested = parseOscPacket(element);
      messages.insert(messages.end(), nested.begin(), nested.end());
      offset += elementSize;
    }
    return messages;
  }

  auto single = parseOscMessage(payload);
  if (single) {
    messages.push_back(*single);
  }
  return messages;
}

std::optional<double> oscArgAsNumber(const OscArg& arg) {
  if (std::holds_alternative<std::int32_t>(arg)) {
    return static_cast<double>(std::get<std::int32_t>(arg));
  }
  if (std::holds_alternative<float>(arg)) {
    return static_cast<double>(std::get<float>(arg));
  }
  if (std::holds_alternative<bool>(arg)) {
    return std::get<bool>(arg) ? 1.0 : 0.0;
  }
  if (std::holds_alternative<std::string>(arg)) {
    try {
      return std::stod(std::get<std::string>(arg));
    } catch (...) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

std::optional<std::string> oscArgAsString(const OscArg& arg) {
  if (std::holds_alternative<std::string>(arg)) {
    return std::get<std::string>(arg);
  }
  if (std::holds_alternative<std::int32_t>(arg)) {
    return std::to_string(std::get<std::int32_t>(arg));
  }
  if (std::holds_alternative<float>(arg)) {
    return std::to_string(std::get<float>(arg));
  }
  if (std::holds_alternative<bool>(arg)) {
    return std::get<bool>(arg) ? "1" : "0";
  }
  return std::nullopt;
}

std::optional<std::string> mapOscToRemoteCommand(const OscMessage& message) {
  std::string path = toUpper(trim(message.address));
  auto argNumber = [&](size_t index) -> std::optional<double> {
    if (index >= message.args.size()) {
      return std::nullopt;
    }
    return oscArgAsNumber(message.args[index]);
  };
  auto argString = [&](size_t index) -> std::optional<std::string> {
    if (index >= message.args.size()) {
      return std::nullopt;
    }
    return oscArgAsString(message.args[index]);
  };
  auto argToggleWord = [&](size_t index) -> std::optional<std::string> {
    auto number = argNumber(index);
    if (!number) {
      return std::nullopt;
    }
    return *number >= 0.5 ? "ON" : "OFF";
  };

  if (path == "/GO" || path == "/TOGGLE") {
    return "GO";
  }
  if (path == "/PLAY") {
    return "PLAY";
  }
  if (path == "/PAUSE") {
    return "PAUSE";
  }
  if (path == "/STOP") {
    return "STOP";
  }
  if (path == "/CLEAR") {
    return "CLEAR";
  }
  if (path == "/NEXT") {
    return "NEXT";
  }
  if (path == "/PREV" || path == "/PREVIOUS") {
    return "PREV";
  }
  if (path == "/FULLSCREEN") {
    return "FULLSCREEN";
  }
  if (path == "/VIDEO" || path == "/OUTPUTMODE") {
    std::string output = "VIDEO";
    for (size_t i = 0; i < message.args.size(); ++i) {
      auto value = argString(i);
      if (!value) {
        continue;
      }
      output += " ";
      output += *value;
    }
    return output;
  }
  if (path == "/SELECT") {
    if (auto value = argString(0)) {
      return "SELECT " + *value;
    }
    return std::nullopt;
  }
  if (path == "/SELECTID" || path == "/CUEID") {
    if (auto value = argString(0)) {
      return "SELECTID " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TAKE") {
    if (auto value = argString(0)) {
      return "TAKE " + *value;
    }
    return "TAKE";
  }
  if (path == "/TAKEID") {
    if (auto value = argString(0)) {
      return "TAKEID " + *value;
    }
    return std::nullopt;
  }
  if (path == "/GOTO") {
    if (auto value = argString(0)) {
      return "GOTO " + *value;
    }
    return std::nullopt;
  }
  if (path == "/FIND" || path == "/CUE/FIND") {
    if (auto value = argString(0)) {
      return "FIND " + *value;
    }
    return "FIND";
  }
  if (path == "/FIND/NEXT" || path == "/CUE/FIND/NEXT") {
    return "FINDNEXT";
  }
  if (path == "/FIND/PREV" || path == "/CUE/FIND/PREV") {
    return "FINDPREV";
  }
  if (path == "/FIND/TAKE" || path == "/CUE/FIND/TAKE") {
    if (auto value = argString(0)) {
      return "FINDTAKE " + *value;
    }
    return "FINDTAKE";
  }
  if (path == "/FIND/CLEAR" || path == "/CUE/FIND/CLEAR") {
    return "FINDCLEAR";
  }
  if (path == "/RENUMBER" || path == "/CUE/RENUMBER" || path == "/CUE/AUTOID") {
    if (message.args.empty()) {
      return "RENUMBER";
    }
    std::string output = "RENUMBER";
    for (size_t i = 0; i < message.args.size(); ++i) {
      if (auto value = argString(i)) {
        output += " " + *value;
      }
    }
    return output;
  }
  if (path == "/DECK") {
    if (auto value = argString(0)) {
      return "DECK " + *value;
    }
    return std::nullopt;
  }
  if (path == "/DECK/OPACITY" || path == "/PLAYLIST/OPACITY") {
    if (auto value = argString(0)) {
      return "DECKOPACITY " + *value;
    }
    return "DECKOPACITY";
  }
  if (path == "/DECK/AUTOFADE" || path == "/PLAYLIST/AUTOFADE") {
    if (auto value = argToggleWord(0)) {
      return "DECKAUTOFADE " + *value;
    }
    return "DECKAUTOFADE";
  }
  if (path == "/DECK/FADE" || path == "/PLAYLIST/FADE") {
    if (auto value = argString(0)) {
      return "DECKFADE " + *value;
    }
    return "DECKFADE";
  }
  if (path == "/OSCQUERY" || path == "/PLAYBOY/OSCQUERY") {
    if (auto value = argToggleWord(0)) {
      return "OSCQUERY " + *value;
    }
    return "OSCQUERY";
  }
  if (path == "/OSCQUERY/PORT" || path == "/PLAYBOY/OSCQUERY/PORT") {
    if (auto value = argString(0)) {
      return "OSCQUERYPORT " + *value;
    }
    return "OSCQUERYPORT";
  }
  if (path == "/OSC/FEEDBACK" || path == "/OSCFEEDBACK" || path == "/PLAYBOY/OSCFEEDBACK") {
    if (auto value = argToggleWord(0)) {
      return "OSCFEEDBACK " + *value;
    }
    return "OSCFEEDBACK";
  }
  if (path == "/OSC/FEEDBACK/RATE" || path == "/OSCFEEDBACK/RATE" || path == "/PLAYBOY/OSCFEEDBACK/RATE") {
    if (auto value = argString(0)) {
      return "OSCFEEDBACKRATE " + *value;
    }
    return "OSCFEEDBACKRATE";
  }
  if (path == "/ROUTE") {
    if (auto value = argString(0)) {
      return "ROUTE " + *value;
    }
    return "ROUTE";
  }
  if (path == "/LAYER") {
    if (auto value = argString(0)) {
      return "LAYER " + *value;
    }
    return "LAYER";
  }
  if (path == "/DECK/NEXT") {
    return "DECKNEXT";
  }
  if (path == "/DECK/PREV") {
    return "DECKPREV";
  }
  if (path == "/VOLUME") {
    if (auto value = argString(0)) {
      return "VOLUME " + *value;
    }
    return std::nullopt;
  }
  if (path == "/CUE/ID" || path == "/CUE/SHORTID") {
    if (auto value = argString(0)) {
      return "CUEIDSHORT " + *value;
    }
    return "CUEIDSHORT";
  }
  if (path == "/CUE/AUDIO") {
    if (auto value = argToggleWord(0)) {
      return "CUEAUDIO " + *value;
    }
    return "CUEAUDIO";
  }
  if (path == "/CUE/PAUSEBEGIN") {
    if (auto value = argToggleWord(0)) {
      return "PAUSEBEGIN " + *value;
    }
    return "PAUSEBEGIN";
  }
  if (path == "/CUE/PAUSEEND") {
    if (auto value = argToggleWord(0)) {
      return "PAUSEEND " + *value;
    }
    return "PAUSEEND";
  }
  if (path == "/CUE/NEXTTRANS") {
    if (auto value = argToggleWord(0)) {
      return "NEXTTRANS " + *value;
    }
    return "NEXTTRANS";
  }
  if (path == "/CUE/GOTO") {
    if (auto value = argString(0)) {
      return "CUEGOTO " + *value;
    }
    return "CUEGOTO";
  }
  if (path == "/SEEK") {
    if (auto value = argString(0)) {
      return "SEEK " + *value;
    }
    return std::nullopt;
  }
  if (path == "/AUTONEXT") {
    if (auto value = argToggleWord(0)) {
      return "AUTONEXT " + *value;
    }
    return std::nullopt;
  }
  if (path == "/PLAYLISTLOOP") {
    if (auto value = argToggleWord(0)) {
      return "PLAYLISTLOOP " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TRANSITION") {
    if (auto value = argString(0)) {
      return "TRANSITION " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TRANSITION/STYLE") {
    if (auto value = argString(0)) {
      return "TRANSITION STYLE " + *value;
    }
    return std::nullopt;
  }
  if (path == "/OVERLAY" || path == "/TIMEOVERLAY") {
    if (auto value = argToggleWord(0)) {
      return "OVERLAY " + *value;
    }
    return "OVERLAY";
  }
  if (path == "/NDI") {
    if (auto value = argToggleWord(0)) {
      return "NDI " + *value;
    }
    return "NDI";
  }
  if (path == "/NDI/NAME") {
    if (auto value = argString(0)) {
      return "NDI NAME " + *value;
    }
    return std::nullopt;
  }
  if (path == "/SOURCE") {
    if (auto value = argString(0)) {
      return "SOURCE " + *value;
    }
    return "SOURCE";
  }
  if (path == "/SOURCE/WINDOW") {
    if (auto value = argString(0)) {
      return "SOURCE WINDOW " + *value;
    }
    return "SOURCE WINDOW";
  }
  if (path == "/SOURCE/CAMERA") {
    if (auto value = argString(0)) {
      return "SOURCE CAMERA " + *value;
    }
    return "SOURCE CAMERA";
  }
  if (path == "/SOURCE/SYPHON" || path == "/SOURCE/SPOUT") {
    if (auto value = argString(0)) {
      return "SOURCE SYPHON " + *value;
    }
    return "SOURCE SYPHON";
  }
  if (path == "/IN") {
    if (auto value = argString(0)) {
      return "IN " + *value;
    }
    return std::nullopt;
  }
  if (path == "/OUT") {
    if (auto value = argString(0)) {
      return "OUT " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TRIM/CLEAR") {
    return "TRIM CLEAR";
  }
  if (path == "/TIMECODE") {
    if (auto value = argString(0)) {
      return "TIMECODE " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/CHASE") {
    if (auto value = argToggleWord(0)) {
      return "TIMECODE CHASE " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/RUN") {
    if (auto value = argToggleWord(0)) {
      return "TIMECODE RUN " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/FPS") {
    if (auto value = argString(0)) {
      return "TIMECODE FPS " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/JAM") {
    if (auto value = argToggleWord(0)) {
      return "TIMECODE JAM " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/FREEWHEEL") {
    if (auto value = argString(0)) {
      return "TIMECODE FREEWHEEL " + *value;
    }
    return std::nullopt;
  }
  if (path == "/TIMECODE/MARK") {
    if (auto value = argString(0)) {
      return "TCMARK " + *value;
    }
    return std::nullopt;
  }

  return std::nullopt;
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
  void (*sendAudioInterleaved16sFn)(NDIlib_send_instance_t, const NDIlib_audio_frame_interleaved_16s_t*) = nullptr;
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
        !loadSymbol(sendAudioInterleaved16sFn, "NDIlib_util_send_send_audio_interleaved_16s") ||
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
    sendAudioInterleaved16sFn = nullptr;
    sendConnectionsFn = nullptr;
  }
};
#endif

class MediaEngine;

// Phased startup for browser cues via virtual framebuffer.
enum class BrowserStartPhase { None, WaitXvfb, WaitChrome, WaitCapture, Live };

struct OutputRuntime {
  struct CapturedFrame {
    int width = 0;
    int height = 0;
    Uint64 capturedAtMs = 0;
    std::vector<std::uint8_t> pixels;
  };

  SDL_Window* outputWindow = nullptr;
  SDL_Renderer* outputRenderer = nullptr;
  SDL_Texture* compositorTexture = nullptr;
  int compositorWidth = 0;
  int compositorHeight = 0;
  Uint32 compositorFormat = SDL_PIXELFORMAT_UNKNOWN;
  int compositorBitDepth = 8;
  std::map<int, SDL_Texture*> layerBridgeTextures;
  std::map<int, int> layerBridgeTextureWidths;
  std::map<int, int> layerBridgeTextureHeights;
  std::vector<std::uint8_t> layerBridgeScratchPixels;
#ifndef _WIN32
  FILE* streamPipe = nullptr;
  int streamAudioPipeFd = -1;
#endif
  std::string streamAudioPipePath;
  std::map<int, std::uint64_t> streamAudioReadSamplesByDeck;
  double streamAudioSampleRemainder = 0.0;
  std::map<int, std::uint64_t> ndiAudioReadSamplesByDeck;
  double ndiAudioSampleRemainder = 0.0;
  std::string streamSpec;
  std::string streamCommand;
  std::vector<std::uint8_t> streamFrameBuffer;
  int streamFrameWidth = 0;
  int streamFrameHeight = 0;
  bool streamStartFailed = false;
  CapturedFrame latestCapturedFrame;
  std::deque<CapturedFrame> delayFrames;
#if defined(PLAYBOY_HAS_NDI_SDK)
  NDIlib_send_instance_t ndiSender = nullptr;
  std::string ndiSenderName;
  std::vector<std::uint8_t> ndiFrameBuffer;
  NDIlib_send_instance_t ndiKeySender = nullptr;
  std::string ndiKeySenderName;
  std::vector<std::uint8_t> ndiKeyFrameBuffer;
#endif
  bool recoveryPausedByEscape = false;
};

struct DeckStreamAudioBuffer {
  std::vector<std::int16_t> samples;
  std::uint64_t droppedSamples = 0;
};

struct DeckRuntime {
  SDL_Window* outputWindow = nullptr;
  SDL_Renderer* outputRenderer = nullptr;
  SDL_AudioDeviceID audioDevice = 0;
  std::unique_ptr<MediaEngine> mediaEngine;
  // Legacy direct-window path (unused with Xvfb, kept for cleanup only)
  ChildProcess browserProcess;
  ChildProcess xvfbProcess;
  bool browserCueLive = false;
  fs::path browserProfileDir;
  std::string virtualDisplayId;        // e.g. ":22"
  BrowserStartPhase browserStartPhase = BrowserStartPhase::None;
  Uint64 browserPhaseStartedAt = 0;
  int pendingBrowserW = 1280;
  int pendingBrowserH = 720;
};

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

std::string escapeHtml(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 16);
  for (char ch : value) {
    switch (ch) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out.push_back(ch); break;
    }
  }
  return out;
}

std::string deckDefaultName(int index) {
  return "Deck " + std::to_string(index + 1);
}

std::string makeCueId(const Cue& cue, int deckIndex, int cueIndex) {
  std::string seed =
    cue.path + "|" +
    cue.name + "|" +
    std::to_string(deckIndex) + "|" +
    std::to_string(cueIndex) + "|" +
    std::to_string(cue.width) + "x" + std::to_string(cue.height);
  size_t hashValue = std::hash<std::string> {}(seed);
  std::ostringstream output;
  output << "cue-" << std::hex << std::nouppercase << hashValue;
  return output.str();
}

void normalizeCueTiming(Cue& cue) {
  if (!std::isfinite(cue.triggerTimecodeSeconds) || cue.triggerTimecodeSeconds < 0.0) {
    cue.triggerTimecodeSeconds = -1.0;
  }

  if (!std::isfinite(cue.inPointSeconds)) {
    cue.inPointSeconds = 0.0;
  }
  if (!std::isfinite(cue.outPointSeconds)) {
    cue.outPointSeconds = 0.0;
  }

  cue.inPointSeconds = std::max(0.0, cue.inPointSeconds);
  cue.outPointSeconds = std::max(0.0, cue.outPointSeconds);

  if (cue.kind != CueKind::Video || cue.duration <= 0.0) {
    cue.inPointSeconds = 0.0;
    cue.outPointSeconds = 0.0;
    return;
  }

  if (cue.outPointSeconds <= 0.0) {
    cue.outPointSeconds = cue.duration;
  }

  cue.inPointSeconds = std::clamp(cue.inPointSeconds, 0.0, cue.duration);
  cue.outPointSeconds = std::clamp(cue.outPointSeconds, cue.inPointSeconds, cue.duration);
  if (cue.outPointSeconds - cue.inPointSeconds < 0.01) {
    cue.outPointSeconds = std::min(cue.duration, cue.inPointSeconds + 0.01);
  }
}

std::string defaultNdiSourceName(const Deck& deck, int index) {
  std::string base = deck.name.empty() ? deckDefaultName(index) : deck.name;
  return "Deckboy - " + base;
}

std::string defaultNdiKeySourceName(const Deck& deck, int index) {
  return defaultNdiSourceName(deck, index) + " Key";
}

std::string defaultOutputNdiSourceName(const OutputTarget& output, int index) {
  std::string base = trim(output.name).empty()
    ? ("Output " + std::to_string(index + 1))
    : output.name;
  return "Deckboy Out - " + base;
}

std::string defaultOutputNdiKeySourceName(const OutputTarget& output, int index) {
  return defaultOutputNdiSourceName(output, index) + " Key";
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

    std::unordered_set<std::string> usedIds;
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      Cue& cue = deck.cues[cueIndex];
      normalizeCueTiming(cue);
      cue.cueId = normalizeCueIdShort(cue.cueId);
      if (cue.cueId.empty()) {
        cue.cueId = normalizeCueIdShort(cue.cueNumber);
      }
      if (!cue.hasAudio) {
        cue.audioEnabled = false;
      }
      cue.outputScaleX = std::clamp(cue.outputScaleX, 0.25f, 4.0f);
      cue.outputScaleY = std::clamp(cue.outputScaleY, 0.25f, 4.0f);
      cue.outputRotationDegrees = std::clamp(cue.outputRotationDegrees, -180.0f, 180.0f);
      cue.cropLeft = std::clamp(cue.cropLeft, 0.0f, 0.90f);
      cue.cropRight = std::clamp(cue.cropRight, 0.0f, 0.90f);
      cue.cropTop = std::clamp(cue.cropTop, 0.0f, 0.90f);
      cue.cropBottom = std::clamp(cue.cropBottom, 0.0f, 0.90f);
      float maxHorizontal = std::max(0.0f, 0.95f - cue.cropLeft);
      cue.cropRight = std::min(cue.cropRight, maxHorizontal);
      float maxVertical = std::max(0.0f, 0.95f - cue.cropTop);
      cue.cropBottom = std::min(cue.cropBottom, maxVertical);
      cue.chromaKeyTolerance = std::clamp(cue.chromaKeyTolerance, 0.0f, 441.0f);
      cue.chromaKeySoftness = std::clamp(cue.chromaKeySoftness, 0.0f, 200.0f);
      cue.brightness = std::clamp(cue.brightness, 0.0f, 2.0f);
      cue.contrast = std::clamp(cue.contrast, 0.0f, 2.0f);
      cue.saturation = std::clamp(cue.saturation, 0.0f, 2.0f);
      cue.hueShift = std::clamp(cue.hueShift, -180.0f, 180.0f);
      cue.gotoTarget = trim(cue.gotoTarget);
      if (cue.id.empty()) {
        cue.id = makeCueId(cue, index, cueIndex);
      }
      std::string baseId = cue.id;
      int dedupe = 2;
      while (usedIds.find(cue.id) != usedIds.end()) {
        cue.id = baseId + "-" + std::to_string(dedupe++);
      }
      usedIds.insert(cue.id);
    }
    std::vector<int> normalizedSelection;
    normalizedSelection.reserve(deck.selectedIndices.size());
    std::unordered_set<int> seenIndices;
    for (int selected : deck.selectedIndices) {
      if (selected < 0 || selected >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      if (seenIndices.insert(selected).second) {
        normalizedSelection.push_back(selected);
      }
    }
    if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
      if (seenIndices.insert(deck.selectedIndex).second) {
        normalizedSelection.push_back(deck.selectedIndex);
      }
    }
    if (normalizedSelection.empty() && !deck.cues.empty()) {
      normalizedSelection.push_back(deck.selectedIndex);
    }
    std::sort(normalizedSelection.begin(), normalizedSelection.end());
    deck.selectedIndices = std::move(normalizedSelection);
  }
  deck.playlistOpacity = std::clamp(deck.playlistOpacity, 0.0f, 1.0f);
  deck.playlistFadeSeconds = std::clamp(
    std::isfinite(deck.playlistFadeSeconds) ? deck.playlistFadeSeconds : 0.8,
    0.05, 10.0);
  deck.playlistTimebaseFps = normalizePlaylistTimebaseFps(deck.playlistTimebaseFps);
  deck.playlistStartOffsetSeconds = std::clamp(
    std::isfinite(deck.playlistStartOffsetSeconds) ? deck.playlistStartOffsetSeconds : 0.0,
    0.0, 24.0 * 60.0 * 60.0);
  deck.playlistDefaultCueFadeSeconds = std::clamp(
    std::isfinite(deck.playlistDefaultCueFadeSeconds) ? deck.playlistDefaultCueFadeSeconds : 0.5,
    0.0, 10.0);
  deck.playlistDefaultStillDurationSeconds = std::clamp(
    std::isfinite(deck.playlistDefaultStillDurationSeconds) ? deck.playlistDefaultStillDurationSeconds : 8.0,
    0.0, 3600.0);
  deck.outputDisplayIndex = std::max(0, deck.outputDisplayIndex);
  if (deck.ndiSourceName.empty()) {
    deck.ndiSourceName = defaultNdiSourceName(deck, index);
  }
  if (deck.ndiKeySourceName.empty()) {
    deck.ndiKeySourceName = defaultNdiKeySourceName(deck, index);
  }
  deck.canvasViewX = std::clamp(deck.canvasViewX, 0, 32768);
  deck.canvasViewY = std::clamp(deck.canvasViewY, 0, 32768);
  deck.warpTopLeftX = std::clamp(deck.warpTopLeftX, -4096.0f, 4096.0f);
  deck.warpTopLeftY = std::clamp(deck.warpTopLeftY, -4096.0f, 4096.0f);
  deck.warpTopRightX = std::clamp(deck.warpTopRightX, -4096.0f, 4096.0f);
  deck.warpTopRightY = std::clamp(deck.warpTopRightY, -4096.0f, 4096.0f);
  deck.warpBottomRightX = std::clamp(deck.warpBottomRightX, -4096.0f, 4096.0f);
  deck.warpBottomRightY = std::clamp(deck.warpBottomRightY, -4096.0f, 4096.0f);
  deck.warpBottomLeftX = std::clamp(deck.warpBottomLeftX, -4096.0f, 4096.0f);
  deck.warpBottomLeftY = std::clamp(deck.warpBottomLeftY, -4096.0f, 4096.0f);
  deck.edgeBlendLeft = std::clamp(deck.edgeBlendLeft, 0.0f, 0.49f);
  deck.edgeBlendRight = std::clamp(deck.edgeBlendRight, 0.0f, 0.49f);
  deck.edgeBlendTop = std::clamp(deck.edgeBlendTop, 0.0f, 0.49f);
  deck.edgeBlendBottom = std::clamp(deck.edgeBlendBottom, 0.0f, 0.49f);
  deck.edgeBlendRight = std::min(deck.edgeBlendRight, std::max(0.0f, 0.95f - deck.edgeBlendLeft));
  deck.edgeBlendBottom = std::min(deck.edgeBlendBottom, std::max(0.0f, 0.95f - deck.edgeBlendTop));
  deck.transitionSeconds = std::clamp(deck.transitionSeconds, 0.0, 10.0);
  deck.transitionStyle = transitionStyleToken(parseTransitionStyleToken(deck.transitionStyle));
  if (!std::isfinite(deck.timecodeFps) || deck.timecodeFps < 1.0) {
    deck.timecodeFps = 30.0;
  }
  deck.timecodeFreewheelSeconds = std::clamp(
    std::isfinite(deck.timecodeFreewheelSeconds) ? deck.timecodeFreewheelSeconds : 1.0,
    0.0, 10.0);
  if (!std::isfinite(deck.timecodeCurrentSeconds) || deck.timecodeCurrentSeconds < 0.0) {
    deck.timecodeCurrentSeconds = 0.0;
  }
  if (!std::isfinite(deck.timecodeLastSeconds) || deck.timecodeLastSeconds < 0.0) {
    deck.timecodeLastSeconds = deck.timecodeCurrentSeconds;
  }
  deck.timecodeDirty = false;
}

std::string outputDefaultName(int index) {
  return "Output " + std::to_string(index + 1);
}

std::string makeOutputId(const OutputTarget& output, int outputIndex) {
  std::string seed =
    output.name + "|" +
    std::to_string(output.hostDeckIndex) + "|" +
    std::to_string(output.displayIndex) + "|" +
    std::to_string(outputIndex);
  size_t hashValue = std::hash<std::string> {}(seed);
  std::ostringstream outputId;
  outputId << "out-" << std::hex << std::nouppercase << hashValue;
  return outputId.str();
}

std::string makeLayerAssignmentId(const LayerAssignment& assignment, int assignmentIndex) {
  std::string seed =
    std::to_string(assignment.deckIndex) + "|" +
    std::to_string(assignment.outputIndex) + "|" +
    std::to_string(assignment.layerIndex) + "|" +
    std::to_string(assignmentIndex);
  size_t hashValue = std::hash<std::string> {}(seed);
  std::ostringstream layerId;
  layerId << "lay-" << std::hex << std::nouppercase << hashValue;
  return layerId.str();
}

std::string groupPresetDefaultName(int index) {
  return "Master Cue " + std::to_string(index + 1);
}

std::string normalizeOutputStreamProtocol(std::string protocol) {
  std::transform(protocol.begin(), protocol.end(), protocol.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (protocol == "rtmp") {
    return "rtmp";
  }
  return "srt";
}

std::string normalizeOutputType(std::string outputType) {
  std::transform(outputType.begin(), outputType.end(), outputType.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (outputType == "stream") {
    return "stream";
  }
  return "window";
}

std::string normalizeOutputColorSpace(std::string colorSpace) {
  colorSpace = toLower(trim(colorSpace));
  if (colorSpace == "709" || colorSpace == "bt709" || colorSpace == "rec709" || colorSpace == "rec-709") {
    return "bt709";
  }
  if (colorSpace == "srgb" || colorSpace == "rgb") {
    return "srgb";
  }
  return "auto";
}

std::string normalizeOutputLayoutMode(std::string mode) {
  mode = toLower(trim(mode));
  if (mode == "duplicate" || mode == "dup" || mode == "clone") {
    return "duplicate";
  }
  return "span";
}

int normalizeOutputOrientationDegrees(int degrees) {
  int normalized = degrees % 360;
  if (normalized < 0) {
    normalized += 360;
  }
  if (normalized < 45) {
    return 0;
  }
  if (normalized < 135) {
    return 90;
  }
  if (normalized < 225) {
    return 180;
  }
  if (normalized < 315) {
    return 270;
  }
  return 0;
}

std::string outputOrientationLabel(int degrees) {
  return std::to_string(normalizeOutputOrientationDegrees(degrees)) + "°";
}

int normalizeOscQueryPort(int port) {
  return std::clamp(port, 1, 65535);
}

int normalizeOscFeedbackRateMs(int value) {
  return std::clamp(value, 40, 2000);
}

std::string defaultOutputStreamUrl(const std::string& protocol, int outputIndex) {
  int normalizedIndex = std::max(0, outputIndex) + 1;
  if (normalizeOutputStreamProtocol(protocol) == "rtmp") {
    return "rtmp://127.0.0.1/live/output" + std::to_string(normalizedIndex);
  }
  return "srt://127.0.0.1:9000?mode=caller&transtype=live&streamid=output" + std::to_string(normalizedIndex);
}

int resolveLegacyOutputHostIndexForProject(const Project& project, int deckIndex) {
  if (deckIndex < 0 || deckIndex >= static_cast<int>(project.decks.size())) {
    return deckIndex;
  }
  int current = deckIndex;
  std::vector<bool> visited(project.decks.size(), false);
  while (current >= 0 && current < static_cast<int>(project.decks.size())) {
    if (visited[current]) {
      return deckIndex;
    }
    visited[current] = true;
    int next = project.decks[current].outputRouteDeckIndex;
    if (next < 0 || next >= static_cast<int>(project.decks.size()) || next == current) {
      return current;
    }
    current = next;
  }
  return deckIndex;
}

void normalizeProjectOutputsAndLayers(Project& project) {
  int deckCount = static_cast<int>(project.decks.size());
  if (deckCount <= 0) {
    project.outputs.clear();
    project.layerAssignments.clear();
    project.focusedOutputIndex = 0;
    return;
  }

  if (project.outputs.empty()) {
    std::vector<int> hostDeckOrder;
    hostDeckOrder.reserve(project.decks.size());
    auto addHost = [&](int hostDeckIndex) {
      if (std::find(hostDeckOrder.begin(), hostDeckOrder.end(), hostDeckIndex) == hostDeckOrder.end()) {
        hostDeckOrder.push_back(hostDeckIndex);
      }
    };
    for (int deckIndex = 0; deckIndex < deckCount; ++deckIndex) {
      addHost(resolveLegacyOutputHostIndexForProject(project, deckIndex));
    }
    if (hostDeckOrder.empty()) {
      hostDeckOrder.push_back(0);
    }
    project.outputs.clear();
    for (size_t i = 0; i < hostDeckOrder.size(); ++i) {
      OutputTarget output;
      output.name = outputDefaultName(static_cast<int>(i));
      output.hostDeckIndex = std::clamp(hostDeckOrder[i], 0, deckCount - 1);
      output.displayIndex = std::max(0, project.decks[output.hostDeckIndex].outputDisplayIndex);
      output.enabled = false;
      project.outputs.push_back(output);
    }
  }

  auto dedupeOutputId = [](std::unordered_set<std::string>& usedIds, const std::string& preferred) {
    std::string normalized = trim(preferred);
    if (normalized.empty()) {
      normalized = "out";
    }
    std::string base = normalized;
    int dedupe = 2;
    while (usedIds.find(normalized) != usedIds.end()) {
      normalized = base + "-" + std::to_string(dedupe++);
    }
    usedIds.insert(normalized);
    return normalized;
  };

  for (size_t i = 0; i < project.outputs.size(); ++i) {
    OutputTarget& output = project.outputs[i];
    if (output.name.empty()) {
      output.name = outputDefaultName(static_cast<int>(i));
    }
    output.hostDeckIndex = std::clamp(output.hostDeckIndex, 0, deckCount - 1);
    output.displayIndex = std::max(0, output.displayIndex);
    output.outputType = normalizeOutputType(output.outputType);
    output.mirrorSourceOutputIndex = std::clamp(output.mirrorSourceOutputIndex, -1, static_cast<int>(project.outputs.size()) - 1);
    if (output.outputType != "stream") {
      output.mirrorSourceOutputIndex = -1;
    }
    if (output.mirrorSourceOutputIndex == static_cast<int>(i)) {
      output.mirrorSourceOutputIndex = -1;
    }
    output.streamProtocol = normalizeOutputStreamProtocol(output.streamProtocol);
    output.streamBitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    output.outputAlpha = std::clamp(output.outputAlpha, 0.0f, 1.0f);
    output.outputDelayMs = std::clamp(output.outputDelayMs, 0, 5000);
    output.outputColorSpace = normalizeOutputColorSpace(output.outputColorSpace);
    output.outputLayoutMode = normalizeOutputLayoutMode(output.outputLayoutMode);
    output.outputOrientationDegrees = normalizeOutputOrientationDegrees(output.outputOrientationDegrees);
    if (trim(output.streamUrl).empty()) {
      output.streamUrl = defaultOutputStreamUrl(output.streamProtocol, static_cast<int>(i));
    }
    if (output.ndiSourceName.empty()) {
      output.ndiSourceName = defaultOutputNdiSourceName(output, static_cast<int>(i));
    }
    if (output.ndiKeySourceName.empty()) {
      output.ndiKeySourceName = defaultOutputNdiKeySourceName(output, static_cast<int>(i));
    }
    if (!output.ndiEnabled) {
      output.ndiKeyEnabled = false;
    }
  }

  {
    std::unordered_set<std::string> usedOutputIds;
    for (size_t i = 0; i < project.outputs.size(); ++i) {
      OutputTarget& output = project.outputs[i];
      if (trim(output.outputId).empty()) {
        output.outputId = makeOutputId(output, static_cast<int>(i));
      }
      output.outputId = dedupeOutputId(usedOutputIds, output.outputId);
    }
  }
  project.focusedOutputIndex = std::clamp(project.focusedOutputIndex, 0, static_cast<int>(project.outputs.size()) - 1);

  if (project.layerAssignments.empty()) {
    for (int deckIndex = 0; deckIndex < deckCount; ++deckIndex) {
      int hostDeckIndex = resolveLegacyOutputHostIndexForProject(project, deckIndex);
      int outputIndex = 0;
      auto it = std::find_if(project.outputs.begin(), project.outputs.end(), [&](const OutputTarget& output) {
        return output.hostDeckIndex == hostDeckIndex;
      });
      if (it != project.outputs.end()) {
        outputIndex = static_cast<int>(it - project.outputs.begin());
      }
      LayerAssignment assignment;
      assignment.deckIndex = deckIndex;
      assignment.outputIndex = outputIndex;
      assignment.outputId = project.outputs[outputIndex].outputId;
      assignment.layerIndex = std::clamp(project.decks[deckIndex].outputLayerIndex, 0, 255);
      assignment.enabled = true;
      project.layerAssignments.push_back(assignment);
    }
  }

  std::map<std::string, int> outputIndexById;
  for (int outputIndex = 0; outputIndex < static_cast<int>(project.outputs.size()); ++outputIndex) {
    const std::string id = trim(project.outputs[outputIndex].outputId);
    if (!id.empty()) {
      outputIndexById[id] = outputIndex;
    }
  }

  std::vector<LayerAssignment> normalizedAssignments;
  normalizedAssignments.reserve(project.layerAssignments.size());
  for (const auto& assignment : project.layerAssignments) {
    if (assignment.deckIndex < 0 || assignment.deckIndex >= deckCount) {
      continue;
    }
    int resolvedOutputIndex = -1;
    std::string outputId = trim(assignment.outputId);
    if (!outputId.empty()) {
      auto it = outputIndexById.find(outputId);
      if (it != outputIndexById.end()) {
        resolvedOutputIndex = it->second;
      }
    }
    if (resolvedOutputIndex < 0 &&
        assignment.outputIndex >= 0 &&
        assignment.outputIndex < static_cast<int>(project.outputs.size())) {
      resolvedOutputIndex = assignment.outputIndex;
    }
    if (resolvedOutputIndex < 0 || resolvedOutputIndex >= static_cast<int>(project.outputs.size())) {
      continue;
    }
    LayerAssignment normalized = assignment;
    normalized.outputIndex = resolvedOutputIndex;
    normalized.outputId = project.outputs[resolvedOutputIndex].outputId;
    normalized.layerIndex = std::clamp(normalized.layerIndex, 0, 255);
    normalizedAssignments.push_back(normalized);
  }
  if (normalizedAssignments.empty()) {
    LayerAssignment fallback;
    fallback.deckIndex = 0;
    fallback.outputIndex = 0;
    fallback.outputId = project.outputs[0].outputId;
    fallback.layerIndex = 0;
    fallback.enabled = true;
    normalizedAssignments.push_back(fallback);
  }
  project.layerAssignments = std::move(normalizedAssignments);

  {
    std::unordered_set<std::string> usedLayerIds;
    for (int assignmentIndex = 0; assignmentIndex < static_cast<int>(project.layerAssignments.size()); ++assignmentIndex) {
      LayerAssignment& assignment = project.layerAssignments[assignmentIndex];
      if (trim(assignment.layerId).empty()) {
        assignment.layerId = makeLayerAssignmentId(assignment, assignmentIndex);
      }
      assignment.layerId = dedupeOutputId(usedLayerIds, assignment.layerId);
    }
  }

  auto ensureOutputIndexForHost = [&](int hostDeckIndex) -> int {
    int clampedHost = std::clamp(hostDeckIndex, 0, deckCount - 1);
    auto it = std::find_if(project.outputs.begin(), project.outputs.end(), [&](const OutputTarget& output) {
      return output.hostDeckIndex == clampedHost;
    });
    if (it != project.outputs.end()) {
      return static_cast<int>(it - project.outputs.begin());
    }
    OutputTarget created;
    created.name = outputDefaultName(static_cast<int>(project.outputs.size()));
    created.hostDeckIndex = clampedHost;
    created.displayIndex = std::max(0, project.decks[clampedHost].outputDisplayIndex);
    created.enabled = true;
    created.outputId = makeOutputId(created, static_cast<int>(project.outputs.size()));
    project.outputs.push_back(created);
    return static_cast<int>(project.outputs.size()) - 1;
  };

  std::vector<int> primaryAssignmentIndex(deckCount, -1);
  for (int i = 0; i < static_cast<int>(project.layerAssignments.size()); ++i) {
    const auto& assignment = project.layerAssignments[i];
    if (!assignment.enabled || assignment.deckIndex < 0 || assignment.deckIndex >= deckCount) {
      continue;
    }
    int existing = primaryAssignmentIndex[assignment.deckIndex];
    if (existing < 0 ||
        assignment.layerIndex < project.layerAssignments[existing].layerIndex) {
      primaryAssignmentIndex[assignment.deckIndex] = i;
    }
  }
  for (int deckIndex = 0; deckIndex < deckCount; ++deckIndex) {
    if (primaryAssignmentIndex[deckIndex] >= 0) {
      continue;
    }
    int hostDeckIndex = resolveLegacyOutputHostIndexForProject(project, deckIndex);
    int outputIndex = ensureOutputIndexForHost(hostDeckIndex);
    LayerAssignment synthesized;
    synthesized.deckIndex = deckIndex;
    synthesized.outputIndex = outputIndex;
    synthesized.outputId = project.outputs[outputIndex].outputId;
    synthesized.layerIndex = std::clamp(project.decks[deckIndex].outputLayerIndex, 0, 255);
    synthesized.enabled = true;
    synthesized.layerId = makeLayerAssignmentId(synthesized, static_cast<int>(project.layerAssignments.size()));
    project.layerAssignments.push_back(synthesized);
    primaryAssignmentIndex[deckIndex] = static_cast<int>(project.layerAssignments.size()) - 1;
  }

  for (size_t i = 0; i < project.outputs.size(); ++i) {
    OutputTarget& output = project.outputs[i];
    if (output.name.empty()) {
      output.name = outputDefaultName(static_cast<int>(i));
    }
    output.hostDeckIndex = std::clamp(output.hostDeckIndex, 0, deckCount - 1);
    output.displayIndex = std::max(0, output.displayIndex);
    output.outputType = normalizeOutputType(output.outputType);
    output.mirrorSourceOutputIndex = std::clamp(output.mirrorSourceOutputIndex, -1, static_cast<int>(project.outputs.size()) - 1);
    if (output.outputType != "stream") {
      output.mirrorSourceOutputIndex = -1;
    }
    if (output.mirrorSourceOutputIndex == static_cast<int>(i)) {
      output.mirrorSourceOutputIndex = -1;
    }
    output.streamProtocol = normalizeOutputStreamProtocol(output.streamProtocol);
    output.streamBitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    output.outputAlpha = std::clamp(output.outputAlpha, 0.0f, 1.0f);
    output.outputDelayMs = std::clamp(output.outputDelayMs, 0, 5000);
    output.outputColorSpace = normalizeOutputColorSpace(output.outputColorSpace);
    output.outputLayoutMode = normalizeOutputLayoutMode(output.outputLayoutMode);
    output.outputOrientationDegrees = normalizeOutputOrientationDegrees(output.outputOrientationDegrees);
    if (trim(output.streamUrl).empty()) {
      output.streamUrl = defaultOutputStreamUrl(output.streamProtocol, static_cast<int>(i));
    }
    if (trim(output.outputId).empty()) {
      output.outputId = makeOutputId(output, static_cast<int>(i));
    }
  }
  project.focusedOutputIndex = std::clamp(project.focusedOutputIndex, 0, static_cast<int>(project.outputs.size()) - 1);

  {
    std::unordered_set<std::string> usedOutputIds;
    for (size_t i = 0; i < project.outputs.size(); ++i) {
      OutputTarget& output = project.outputs[i];
      output.outputId = dedupeOutputId(usedOutputIds, output.outputId);
    }
    for (auto& assignment : project.layerAssignments) {
      int outputIndex = std::clamp(assignment.outputIndex, 0, static_cast<int>(project.outputs.size()) - 1);
      assignment.outputIndex = outputIndex;
      assignment.outputId = project.outputs[outputIndex].outputId;
    }
  }

  std::vector<bool> hostDisplayAssigned(deckCount, false);
  for (const auto& output : project.outputs) {
    if (!output.enabled) {
      continue;
    }
    int hostDeckIndex = std::clamp(output.hostDeckIndex, 0, deckCount - 1);
    if (!hostDisplayAssigned[hostDeckIndex]) {
      project.decks[hostDeckIndex].outputDisplayIndex = std::max(0, output.displayIndex);
      hostDisplayAssigned[hostDeckIndex] = true;
    }
  }

  for (int deckIndex = 0; deckIndex < deckCount; ++deckIndex) {
    int primaryIndex = primaryAssignmentIndex[deckIndex];
    if (primaryIndex < 0 || primaryIndex >= static_cast<int>(project.layerAssignments.size())) {
      project.decks[deckIndex].outputRouteDeckIndex = deckIndex;
      project.decks[deckIndex].outputLayerIndex = 0;
      continue;
    }
    const LayerAssignment& primary = project.layerAssignments[primaryIndex];
    const OutputTarget& output = project.outputs[primary.outputIndex];
    project.decks[deckIndex].outputRouteDeckIndex = std::clamp(output.hostDeckIndex, 0, deckCount - 1);
    project.decks[deckIndex].outputLayerIndex = std::clamp(primary.layerIndex, 0, 255);
  }
}

bool outputHasExplicitNdiSettings(const OutputTarget& output, int outputIndex) {
  std::string fillName = trim(output.ndiSourceName);
  std::string keyName = trim(output.ndiKeySourceName);
  std::string defaultFill = defaultOutputNdiSourceName(output, outputIndex);
  std::string defaultKey = defaultOutputNdiKeySourceName(output, outputIndex);
  bool fillCustom = !fillName.empty() && fillName != defaultFill;
  bool keyCustom = !keyName.empty() && keyName != defaultKey;
  return output.ndiEnabled || output.ndiKeyEnabled || fillCustom || keyCustom;
}

void migrateLegacyDeckNdiToOutputs(Project& project) {
  int deckCount = static_cast<int>(project.decks.size());
  int outputCount = static_cast<int>(project.outputs.size());
  if (deckCount <= 0 || outputCount <= 0) {
    return;
  }

  std::vector<int> primaryDeckForOutput(outputCount, -1);
  std::vector<int> bestLayerForOutput(outputCount, std::numeric_limits<int>::max());
  for (const auto& assignment : project.layerAssignments) {
    if (!assignment.enabled) {
      continue;
    }
    if (assignment.outputIndex < 0 || assignment.outputIndex >= outputCount) {
      continue;
    }
    if (assignment.deckIndex < 0 || assignment.deckIndex >= deckCount) {
      continue;
    }
    int& currentDeck = primaryDeckForOutput[assignment.outputIndex];
    int& currentLayer = bestLayerForOutput[assignment.outputIndex];
    bool shouldReplace = assignment.layerIndex < currentLayer
      || (assignment.layerIndex == currentLayer && (currentDeck < 0 || assignment.deckIndex < currentDeck));
    if (shouldReplace) {
      currentDeck = assignment.deckIndex;
      currentLayer = assignment.layerIndex;
    }
  }

  for (int outputIndex = 0; outputIndex < outputCount; ++outputIndex) {
    OutputTarget& output = project.outputs[outputIndex];
    if (outputHasExplicitNdiSettings(output, outputIndex)) {
      continue;
    }

    int sourceDeckIndex = primaryDeckForOutput[outputIndex];
    if (sourceDeckIndex < 0 || sourceDeckIndex >= deckCount) {
      sourceDeckIndex = std::clamp(output.hostDeckIndex, 0, deckCount - 1);
    }
    const Deck& deck = project.decks[sourceDeckIndex];
    std::string legacyFillName = trim(deck.ndiSourceName);
    std::string legacyKeyName = trim(deck.ndiKeySourceName);
    bool hasLegacy = deck.ndiEnabled || deck.ndiKeyEnabled || !legacyFillName.empty() || !legacyKeyName.empty();
    if (!hasLegacy) {
      continue;
    }

    output.ndiEnabled = deck.ndiEnabled || deck.ndiKeyEnabled;
    output.ndiKeyEnabled = deck.ndiKeyEnabled;
    if (!legacyFillName.empty()) {
      output.ndiSourceName = legacyFillName;
    }
    if (!legacyKeyName.empty()) {
      output.ndiKeySourceName = legacyKeyName;
    }
    if (trim(output.ndiSourceName).empty()) {
      output.ndiSourceName = defaultOutputNdiSourceName(output, outputIndex);
    }
    if (trim(output.ndiKeySourceName).empty()) {
      output.ndiKeySourceName = defaultOutputNdiKeySourceName(output, outputIndex);
    }
    if (!output.ndiEnabled) {
      output.ndiKeyEnabled = false;
    }
  }
}

void normalizeProjectGroupPresets(Project& project) {
  int deckCount = static_cast<int>(project.decks.size());
  if (deckCount <= 0) {
    project.groupPresets.clear();
    project.focusedGroupPresetIndex = 0;
    return;
  }
  for (size_t presetIndex = 0; presetIndex < project.groupPresets.size(); ++presetIndex) {
    GroupPreset& preset = project.groupPresets[presetIndex];
    if (preset.name.empty()) {
      preset.name = groupPresetDefaultName(static_cast<int>(presetIndex));
    }
    if (static_cast<int>(preset.slots.size()) < deckCount) {
      preset.slots.resize(deckCount);
    } else if (static_cast<int>(preset.slots.size()) > deckCount) {
      preset.slots.resize(deckCount);
    }
  }
  if (project.groupPresets.empty()) {
    project.focusedGroupPresetIndex = 0;
  } else {
    project.focusedGroupPresetIndex = std::clamp(
      project.focusedGroupPresetIndex,
      0,
      static_cast<int>(project.groupPresets.size()) - 1);
  }
}

void normalizeProject(Project& project) {
  if (project.decks.empty()) {
    project.decks.push_back(Deck {});
  }
  for (size_t index = 0; index < project.decks.size(); ++index) {
    normalizeDeck(project.decks[index], static_cast<int>(index));
  }
  for (size_t index = 0; index < project.decks.size(); ++index) {
    Deck& deck = project.decks[index];
    if (deck.outputRouteDeckIndex < 0 ||
        deck.outputRouteDeckIndex >= static_cast<int>(project.decks.size())) {
      deck.outputRouteDeckIndex = static_cast<int>(index);
    }
    deck.outputLayerIndex = std::clamp(deck.outputLayerIndex, 0, 255);
  }
  normalizeProjectOutputsAndLayers(project);
  migrateLegacyDeckNdiToOutputs(project);
  normalizeProjectGroupPresets(project);
  project.focusedDeckIndex = std::clamp(project.focusedDeckIndex, 0, static_cast<int>(project.decks.size()) - 1);
  project.outputRenderWidth = std::clamp(project.outputRenderWidth, 320, 7680);
  project.outputRenderHeight = std::clamp(project.outputRenderHeight, 180, 4320);
  if (!std::isfinite(project.outputRefreshRateHz) || project.outputRefreshRateHz < 0.0) {
    project.outputRefreshRateHz = 0.0;
  }
  project.outputRefreshRateHz = std::min(project.outputRefreshRateHz, 240.0);
  if (project.outputBitDepth != 0 && project.outputBitDepth != 8 && project.outputBitDepth != 10) {
    project.outputBitDepth = 0;
  }
  project.outputCanvasWidth = std::clamp(project.outputCanvasWidth, 320, 16384);
  project.outputCanvasHeight = std::clamp(project.outputCanvasHeight, 180, 16384);
  project.advancedOutputMode = project.advancedOutputMode || project.decks.size() > 1;
  project.jumpMode = normalizeJumpModeToken(project.jumpMode);
  project.panicProfile = normalizePanicProfileToken(project.panicProfile);
  project.oscQueryPort = normalizeOscQueryPort(project.oscQueryPort);
  project.oscFeedbackRateMs = normalizeOscFeedbackRateMs(project.oscFeedbackRateMs);
  project.panicFadeSeconds = std::clamp(
    std::isfinite(project.panicFadeSeconds) ? project.panicFadeSeconds : 0.9,
    0.1, 5.0);
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
    } else if (key == "duration" && (cue.kind == CueKind::Video || cue.duration == 0.0)) {
      double d = std::atof(value.c_str());
      if (d > 0.0) cue.duration = d;
    } else if (key == "format_name") {
      cue.formatName = value;
    } else if (key == "size") {
      cue.sizeBytes = static_cast<std::uintmax_t>(std::strtoull(value.c_str(), nullptr, 10));
    }
  }

  // Auto-detect audio-only: no video stream but has audio
  if (cue.videoCodec.empty() && cue.hasAudio) {
    cue.kind = CueKind::Audio;
    // Audio cues don't have video dimensions — give them nominal size
    if (cue.width <= 0) cue.width = 1;
    if (cue.height <= 0) cue.height = 1;
  }
  if (cue.width <= 0 || cue.height <= 0) {
    return std::nullopt;
  }
  if ((cue.kind == CueKind::Video || cue.kind == CueKind::Audio) && cue.duration <= 0.0) {
    cue.duration = 0.0;
  }
  return cue;
}

bool saveProject(const fs::path& projectFile, const Project& project) {
  fs::path resolved = projectFile.empty() ? Paths::defaultProjectFile() : projectFile;
  if (resolved.has_parent_path()) {
    fs::create_directories(resolved.parent_path());
  }
  std::ofstream output(resolved, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }

  output << "title\t" << escapeField(project.title) << '\n';
  output << "focused_deck\t" << project.focusedDeckIndex << '\n';
  output << "focused_output\t" << project.focusedOutputIndex << '\n';
  output << "focused_group\t" << project.focusedGroupPresetIndex << '\n';
  output << "layer_names";
  for (const auto& name : project.layerNames) {
    output << '\t' << escapeField(name);
  }
  output << '\n';
  output << "advanced_mode\t" << (project.advancedOutputMode ? 1 : 0) << '\n';
  output << "ui_sounds\t" << (project.uiSoundsEnabled ? 1 : 0) << '\n';
  output << "ui_transitions\t" << (project.uiTransitionsEnabled ? 1 : 0) << '\n';
  output << "osc_query_enabled\t" << (project.oscQueryEnabled ? 1 : 0) << '\n';
  output << "osc_query_port\t" << project.oscQueryPort << '\n';
  output << "osc_feedback_mirror\t" << (project.oscFeedbackMirrorEnabled ? 1 : 0) << '\n';
  output << "osc_feedback_rate_ms\t" << project.oscFeedbackRateMs << '\n';
  output << "jump_mode\t" << escapeField(project.jumpMode) << '\n';
  output << "jump_transition\t" << (project.jumpTransitionEnabled ? 1 : 0) << '\n';
  output << "panic_profile\t" << escapeField(project.panicProfile) << '\n';
  output << "panic_fade_seconds\t" << project.panicFadeSeconds << '\n';
  output << "panic_auto_restore\t" << (project.panicAutoRestore ? 1 : 0) << '\n';
  output << "master_volume\t" << project.masterVolume << '\n';
  output << "master_dimmer\t" << project.masterDimmer << '\n';
  output << "output_follow_display\t" << (project.outputFollowDisplay ? 1 : 0) << '\n';
  output << "output_render_width\t" << project.outputRenderWidth << '\n';
  output << "output_render_height\t" << project.outputRenderHeight << '\n';
  output << "output_refresh_hz\t" << project.outputRefreshRateHz << '\n';
  output << "output_bit_depth\t" << project.outputBitDepth << '\n';
  output << "output_canvas_enabled\t" << (project.outputCanvasEnabled ? 1 : 0) << '\n';
  output << "output_canvas_width\t" << project.outputCanvasWidth << '\n';
  output << "output_canvas_height\t" << project.outputCanvasHeight << '\n';
  for (size_t outputIndex = 0; outputIndex < project.outputs.size(); ++outputIndex) {
    const auto& outputTarget = project.outputs[outputIndex];
    output
      << "output_target\t"
      << outputIndex << '\t'
      << escapeField(outputTarget.name) << '\t'
      << outputTarget.hostDeckIndex << '\t'
      << outputTarget.displayIndex << '\t'
      << (outputTarget.enabled ? 1 : 0) << '\t'
      << (outputTarget.streamEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.streamProtocol) << '\t'
      << escapeField(outputTarget.streamUrl) << '\t'
      << outputTarget.streamBitrateKbps << '\t'
      << (outputTarget.ndiEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.ndiSourceName) << '\t'
      << (outputTarget.ndiKeyEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.ndiKeySourceName) << '\t'
      << escapeField(outputTarget.outputType) << '\t'
      << outputTarget.mirrorSourceOutputIndex << '\t'
      << escapeField(outputTarget.outputId) << '\t'
      << outputTarget.outputAlpha << '\t'
      << outputTarget.outputDelayMs << '\t'
      << (outputTarget.outputTimeOverlayEnabled ? 1 : 0) << '\t'
      << escapeField(outputTarget.outputColorSpace) << '\t'
      << escapeField(outputTarget.outputLayoutMode) << '\t'
      << outputTarget.outputOrientationDegrees << '\t'
      << (outputTarget.outputTestCardEnabled ? 1 : 0)
      << '\n';
  }
  for (const auto& assignment : project.layerAssignments) {
    output
      << "layer_assignment\t"
      << assignment.deckIndex << '\t'
      << assignment.outputIndex << '\t'
      << assignment.layerIndex << '\t'
      << (assignment.enabled ? 1 : 0) << '\t'
      << escapeField(assignment.outputId) << '\t'
      << escapeField(assignment.layerId)
      << '\n';
  }
  for (size_t presetIndex = 0; presetIndex < project.groupPresets.size(); ++presetIndex) {
    const auto& preset = project.groupPresets[presetIndex];
    output
      << "group_preset\t"
      << presetIndex << '\t'
      << escapeField(preset.name)
      << '\n';
    for (size_t deckIndex = 0; deckIndex < preset.slots.size(); ++deckIndex) {
      const auto& slot = preset.slots[deckIndex];
      output
        << "group_slot\t"
        << presetIndex << '\t'
        << deckIndex << '\t'
        << (slot.bypass ? 1 : 0) << '\t'
        << escapeField(slot.cueId)
        << '\n';
    }
  }

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
      << escapeField(deck.ndiSourceName) << '\t'
      << (deck.timeOverlayEnabled ? 1 : 0) << '\t'
      << deck.transitionSeconds << '\t'
      << escapeField(deck.transitionStyle) << '\t'
      << (deck.timecodeChaseEnabled ? 1 : 0) << '\t'
      << (deck.timecodeRunEnabled ? 1 : 0) << '\t'
      << (deck.timecodeTriggerEnabled ? 1 : 0) << '\t'
      << deck.timecodeFps << '\t'
      << deck.timecodeCurrentSeconds << '\t'
      << (deck.shuffle ? 1 : 0) << '\t'
      << (deck.ndiKeyEnabled ? 1 : 0) << '\t'
      << escapeField(deck.ndiKeySourceName) << '\t'
      << deck.canvasViewX << '\t'
      << deck.canvasViewY << '\t'
      << (deck.warpEnabled ? 1 : 0) << '\t'
      << deck.warpTopLeftX << '\t'
      << deck.warpTopLeftY << '\t'
      << deck.warpTopRightX << '\t'
      << deck.warpTopRightY << '\t'
      << deck.warpBottomRightX << '\t'
      << deck.warpBottomRightY << '\t'
      << deck.warpBottomLeftX << '\t'
      << deck.warpBottomLeftY << '\t'
      << deck.edgeBlendLeft << '\t'
      << deck.edgeBlendRight << '\t'
      << deck.edgeBlendTop << '\t'
      << deck.edgeBlendBottom << '\t'
      << deck.outputRouteDeckIndex << '\t'
      << deck.outputLayerIndex << '\t'
      << deck.timecodeFreewheelSeconds << '\t'
      << (deck.timecodeJamSyncEnabled ? 1 : 0) << '\t'
      << deck.playlistOpacity << '\t'
      << (deck.playlistAutoFade ? 1 : 0) << '\t'
      << deck.playlistFadeSeconds << '\t'
      << deck.playlistTimebaseFps << '\t'
      << deck.playlistStartOffsetSeconds << '\t'
      << deck.playlistDefaultCueFadeSeconds << '\t'
      << deck.playlistDefaultStillDurationSeconds << '\t'
      << (deck.playlistDefaultLoop ? 1 : 0) << '\t'
      << (deck.playlistDefaultFadeInEnabled ? 1 : 0) << '\t'
      << (deck.playlistDefaultFadeOutEnabled ? 1 : 0) << '\t'
      << (deck.playlistDefaultAudioEnabled ? 1 : 0) << '\t'
      << (deck.playlistDefaultPauseAtBeginning ? 1 : 0) << '\t'
      << (deck.playlistDefaultPauseAtEnd ? 1 : 0) << '\t'
      << (deck.playlistDefaultTransitionToNext ? 1 : 0)
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
        << (cue.pauseOnLastFrame ? "1" : "0") << '\t'
        << escapeField(cue.id) << '\t'
        << cue.inPointSeconds << '\t'
        << cue.outPointSeconds << '\t'
        << cue.triggerTimecodeSeconds << '\t'
        << cueEndActionToken(cue.endAction) << '\t'
        << cue.cueTransitionSeconds << '\t'
        << escapeField(cue.cueTransitionStyle) << '\t'
        << escapeField(cue.lowerThirdText) << '\t'
        << escapeField(cue.lowerThirdSubtext) << '\t'
        << cue.lowerThirdBgAlpha << '\t'
        << cue.stillDurationSeconds << '\t'
        << cue.loopCount << '\t'
        << cue.playbackSpeed << '\t'
        << escapeField(cue.colorTag)
        << '\t' << escapeField(cue.notes)
        << '\t' << cue.outputScaleX
        << '\t' << cue.outputScaleY
        << '\t' << static_cast<int>(cue.scaleMode)
        << '\t' << cue.outputOffsetX
        << '\t' << cue.outputOffsetY
        << '\t' << escapeField(cue.cueNumber)
        << '\t' << [&]() {
             std::ostringstream pp;
             for (size_t i = 0; i < cue.pausePoints.size(); ++i) {
               if (i) pp << ',';
               pp << cue.pausePoints[i];
             }
             return pp.str();
           }()
        << '\t' << cue.outputRotationDegrees
        << '\t' << cue.cropLeft
        << '\t' << cue.cropRight
        << '\t' << cue.cropTop
        << '\t' << cue.cropBottom
        << '\t' << (cue.chromaKeyEnabled ? "1" : "0")
        << '\t' << colorToHex(cue.chromaKeyColor)
        << '\t' << cue.chromaKeyTolerance
        << '\t' << cue.chromaKeySoftness
        << '\t' << cue.brightness
        << '\t' << cue.contrast
        << '\t' << cue.saturation
        << '\t' << cue.hueShift
        << '\t' << escapeField(cue.cueId)
        << '\t' << (cue.audioEnabled ? "1" : "0")
        << '\t' << (cue.pauseAtBeginning ? "1" : "0")
        << '\t' << (cue.transitionToNext ? "1" : "0")
        << '\t' << escapeField(cue.gotoTarget)
        << '\n';
    }
  }

  return true;
}

Project loadProject(const fs::path& projectFile) {
  Project project;
  fs::path resolved = projectFile.empty() ? Paths::defaultProjectFile() : projectFile;
  std::ifstream input(resolved, std::ios::binary);
  if (!input) {
    return project;
  }

  project.decks.clear();
  project.decks.push_back(Deck {});
  project.outputs.clear();
  project.layerAssignments.clear();
  project.groupPresets.clear();

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
    } else if (fields[0] == "focused_output") {
      project.focusedOutputIndex = safeInt(fields, 1, 0);
    } else if (fields[0] == "focused_group") {
      project.focusedGroupPresetIndex = safeInt(fields, 1, 0);
    } else if (fields[0] == "layer_names") {
      project.layerNames.clear();
      for (size_t i = 1; i < fields.size(); ++i) {
        project.layerNames.push_back(fields[i]);
      }
      if (project.layerNames.empty()) {
        project.layerNames = {"BG", "LayerA", "LayerB", "LayerC", "LayerD"};
      }
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
    } else if (fields[0] == "osc_query_enabled") {
      project.oscQueryEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "osc_query_port") {
      project.oscQueryPort = safeInt(fields, 1, 5511);
    } else if (fields[0] == "osc_feedback_mirror") {
      project.oscFeedbackMirrorEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "osc_feedback_rate_ms") {
      project.oscFeedbackRateMs = safeInt(fields, 1, 120);
    } else if (fields[0] == "jump_mode") {
      project.jumpMode = normalizeJumpModeToken(safeString(fields, 1));
    } else if (fields[0] == "jump_transition") {
      project.jumpTransitionEnabled = safeBool(fields, 1, true);
    } else if (fields[0] == "panic_profile") {
      project.panicProfile = normalizePanicProfileToken(safeString(fields, 1));
    } else if (fields[0] == "panic_fade_seconds") {
      project.panicFadeSeconds = safeDouble(fields, 1, 0.9);
    } else if (fields[0] == "panic_auto_restore") {
      project.panicAutoRestore = safeBool(fields, 1, false);
    } else if (fields[0] == "master_volume") {
      project.masterVolume = std::clamp(safeDouble(fields, 1, 1.0), 0.0, 1.0);
    } else if (fields[0] == "master_dimmer") {
      project.masterDimmer = std::clamp(safeDouble(fields, 1, 1.0), 0.0, 1.0);
    } else if (fields[0] == "output_follow_display") {
      project.outputFollowDisplay = safeBool(fields, 1, true);
    } else if (fields[0] == "output_render_width") {
      project.outputRenderWidth = safeInt(fields, 1, 1920);
    } else if (fields[0] == "output_render_height") {
      project.outputRenderHeight = safeInt(fields, 1, 1080);
    } else if (fields[0] == "output_refresh_hz") {
      project.outputRefreshRateHz = std::max(0.0, safeDouble(fields, 1, 0.0));
    } else if (fields[0] == "output_bit_depth") {
      project.outputBitDepth = safeInt(fields, 1, 0);
    } else if (fields[0] == "output_canvas_enabled") {
      project.outputCanvasEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "output_canvas_width") {
      project.outputCanvasWidth = safeInt(fields, 1, 3840);
    } else if (fields[0] == "output_canvas_height") {
      project.outputCanvasHeight = safeInt(fields, 1, 2160);
    } else if (fields[0] == "output_target") {
      int outputIndex = safeInt(fields, 1, static_cast<int>(project.outputs.size()));
      int normalizedIndex = std::max(0, outputIndex);
      while (normalizedIndex >= static_cast<int>(project.outputs.size())) {
        project.outputs.push_back(OutputTarget {});
      }
      OutputTarget& outputTarget = project.outputs[normalizedIndex];
      outputTarget.name = safeString(fields, 2);
      outputTarget.hostDeckIndex = safeInt(fields, 3, 0);
      outputTarget.displayIndex = safeInt(fields, 4, 0);
      outputTarget.enabled = safeBool(fields, 5, false);
      outputTarget.streamEnabled = safeBool(fields, 6, false);
      outputTarget.streamProtocol = safeString(fields, 7);
      outputTarget.streamUrl = safeString(fields, 8);
      outputTarget.streamBitrateKbps = safeInt(fields, 9, 6000);
      if (fields.size() >= 17) {
        outputTarget.ndiEnabled = safeBool(fields, 10, false);
        outputTarget.ndiSourceName = safeString(fields, 11);
        outputTarget.ndiKeyEnabled = safeBool(fields, 12, false);
        outputTarget.ndiKeySourceName = safeString(fields, 13);
        outputTarget.outputType = safeString(fields, 14);
        outputTarget.mirrorSourceOutputIndex = safeInt(fields, 15, -1);
        outputTarget.outputId = safeString(fields, 16);
        if (fields.size() >= 21) {
          outputTarget.outputAlpha = static_cast<float>(safeDouble(fields, 17, 1.0));
          outputTarget.outputDelayMs = safeInt(fields, 18, 0);
          outputTarget.outputTimeOverlayEnabled = safeBool(fields, 19, false);
          outputTarget.outputColorSpace = safeString(fields, 20);
          if (fields.size() >= 24) {
            outputTarget.outputLayoutMode = safeString(fields, 21);
            outputTarget.outputOrientationDegrees = safeInt(fields, 22, 0);
            outputTarget.outputTestCardEnabled = safeBool(fields, 23, false);
          }
        }
      } else {
        // Backward compatibility with older 13-column output_target lines.
        outputTarget.outputType = safeString(fields, 10);
        outputTarget.mirrorSourceOutputIndex = safeInt(fields, 11, -1);
        outputTarget.outputId = safeString(fields, 12);
      }
    } else if (fields[0] == "layer_assignment") {
      LayerAssignment assignment;
      assignment.deckIndex = safeInt(fields, 1, 0);
      assignment.outputIndex = safeInt(fields, 2, 0);
      assignment.layerIndex = safeInt(fields, 3, 0);
      assignment.enabled = safeBool(fields, 4, true);
      assignment.outputId = safeString(fields, 5);
      assignment.layerId = safeString(fields, 6);
      project.layerAssignments.push_back(assignment);
    } else if (fields[0] == "group_preset") {
      int presetIndex = safeInt(fields, 1, static_cast<int>(project.groupPresets.size()));
      int normalizedPresetIndex = std::max(0, presetIndex);
      while (normalizedPresetIndex >= static_cast<int>(project.groupPresets.size())) {
        project.groupPresets.push_back(GroupPreset {});
      }
      GroupPreset& preset = project.groupPresets[normalizedPresetIndex];
      preset.name = safeString(fields, 2);
    } else if (fields[0] == "group_slot") {
      int presetIndex = safeInt(fields, 1, 0);
      int deckIndex = safeInt(fields, 2, 0);
      if (presetIndex < 0 || deckIndex < 0) {
        continue;
      }
      while (presetIndex >= static_cast<int>(project.groupPresets.size())) {
        project.groupPresets.push_back(GroupPreset {});
      }
      GroupPreset& preset = project.groupPresets[presetIndex];
      if (deckIndex >= static_cast<int>(preset.slots.size())) {
        preset.slots.resize(deckIndex + 1);
      }
      GroupSlot& slot = preset.slots[deckIndex];
      slot.bypass = safeBool(fields, 3, false);
      slot.cueId = safeString(fields, 4);
    } else if (fields[0] == "audio_output") {
      ensureDeck(0).audioOutputDeviceName = safeString(fields, 1);
    } else if (fields[0] == "display_index") {
      ensureDeck(0).outputDisplayIndex = safeInt(fields, 1, 0);
    } else if (fields[0] == "ndi_enabled") {
      ensureDeck(0).ndiEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "ndi_name") {
      ensureDeck(0).ndiSourceName = safeString(fields, 1);
    } else if (fields[0] == "ndi_key_enabled") {
      ensureDeck(0).ndiKeyEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "ndi_key_name") {
      ensureDeck(0).ndiKeySourceName = safeString(fields, 1);
    } else if (fields[0] == "time_overlay") {
      ensureDeck(0).timeOverlayEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "transition_seconds") {
      ensureDeck(0).transitionSeconds = std::max(0.0, safeDouble(fields, 1, 0.0));
    } else if (fields[0] == "transition_style") {
      ensureDeck(0).transitionStyle = safeString(fields, 1);
    } else if (fields[0] == "timecode_chase") {
      ensureDeck(0).timecodeChaseEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "timecode_run") {
      ensureDeck(0).timecodeRunEnabled = safeBool(fields, 1, false);
    } else if (fields[0] == "timecode_trigger") {
      ensureDeck(0).timecodeTriggerEnabled = safeBool(fields, 1, true);
    } else if (fields[0] == "timecode_jam") {
      ensureDeck(0).timecodeJamSyncEnabled = safeBool(fields, 1, true);
    } else if (fields[0] == "timecode_freewheel") {
      ensureDeck(0).timecodeFreewheelSeconds = safeDouble(fields, 1, 1.0);
    } else if (fields[0] == "timecode_fps") {
      ensureDeck(0).timecodeFps = safeDouble(fields, 1, 30.0);
    } else if (fields[0] == "timecode_current") {
      ensureDeck(0).timecodeCurrentSeconds = std::max(0.0, safeDouble(fields, 1, 0.0));
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
      deck.timeOverlayEnabled = safeBool(fields, 11, false);
      deck.transitionSeconds = std::max(0.0, safeDouble(fields, 12, 0.0));
      deck.transitionStyle = safeString(fields, 13);
      deck.timecodeChaseEnabled = safeBool(fields, 14, false);
      deck.timecodeRunEnabled = safeBool(fields, 15, false);
      deck.timecodeTriggerEnabled = safeBool(fields, 16, true);
      deck.timecodeFps = safeDouble(fields, 17, 30.0);
      deck.timecodeCurrentSeconds = std::max(0.0, safeDouble(fields, 18, 0.0));
      deck.timecodeLastSeconds = deck.timecodeCurrentSeconds;
      deck.shuffle = safeBool(fields, 19, false);
      deck.ndiKeyEnabled = safeBool(fields, 20, false);
      deck.ndiKeySourceName = safeString(fields, 21);
      deck.canvasViewX = safeInt(fields, 22, 0);
      deck.canvasViewY = safeInt(fields, 23, 0);
      deck.warpEnabled = safeBool(fields, 24, false);
      deck.warpTopLeftX = static_cast<float>(safeDouble(fields, 25, 0.0));
      deck.warpTopLeftY = static_cast<float>(safeDouble(fields, 26, 0.0));
      deck.warpTopRightX = static_cast<float>(safeDouble(fields, 27, 0.0));
      deck.warpTopRightY = static_cast<float>(safeDouble(fields, 28, 0.0));
      deck.warpBottomRightX = static_cast<float>(safeDouble(fields, 29, 0.0));
      deck.warpBottomRightY = static_cast<float>(safeDouble(fields, 30, 0.0));
      deck.warpBottomLeftX = static_cast<float>(safeDouble(fields, 31, 0.0));
      deck.warpBottomLeftY = static_cast<float>(safeDouble(fields, 32, 0.0));
      deck.edgeBlendLeft = static_cast<float>(safeDouble(fields, 33, 0.0));
      deck.edgeBlendRight = static_cast<float>(safeDouble(fields, 34, 0.0));
      deck.edgeBlendTop = static_cast<float>(safeDouble(fields, 35, 0.0));
      deck.edgeBlendBottom = static_cast<float>(safeDouble(fields, 36, 0.0));
      deck.outputRouteDeckIndex = safeInt(fields, 37, deckIndex);
      deck.outputLayerIndex = safeInt(fields, 38, 0);
      deck.timecodeFreewheelSeconds = safeDouble(fields, 39, 1.0);
      deck.timecodeJamSyncEnabled = safeBool(fields, 40, true);
      deck.playlistOpacity = static_cast<float>(safeDouble(fields, 41, 1.0));
      deck.playlistAutoFade = safeBool(fields, 42, false);
      deck.playlistFadeSeconds = safeDouble(fields, 43, 0.8);
      deck.playlistTimebaseFps = safeDouble(fields, 44, deck.timecodeFps);
      deck.playlistStartOffsetSeconds = safeDouble(fields, 45, 0.0);
      deck.playlistDefaultCueFadeSeconds = safeDouble(fields, 46, 0.5);
      deck.playlistDefaultStillDurationSeconds = safeDouble(fields, 47, 8.0);
      deck.playlistDefaultLoop = safeBool(fields, 48, false);
      deck.playlistDefaultFadeInEnabled = safeBool(fields, 49, true);
      deck.playlistDefaultFadeOutEnabled = safeBool(fields, 50, true);
      deck.playlistDefaultAudioEnabled = safeBool(fields, 51, true);
      deck.playlistDefaultPauseAtBeginning = safeBool(fields, 52, false);
      deck.playlistDefaultPauseAtEnd = safeBool(fields, 53, false);
      deck.playlistDefaultTransitionToNext = safeBool(fields, 54, true);
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
        (kind == "window_source" || kind == "window") ? CueKind::WindowSource :
        kind == "camera" ? CueKind::Camera :
        (kind == "syphon" || kind == "spout") ? CueKind::Syphon :
        kind == "lower_third" ? CueKind::LowerThird :
        kind == "audio" ? CueKind::Audio :
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
      cue.id = safeString(fields, offset + 17);
      cue.inPointSeconds = std::max(0.0, safeDouble(fields, offset + 18, 0.0));
      cue.outPointSeconds = std::max(0.0, safeDouble(fields, offset + 19, 0.0));
      cue.triggerTimecodeSeconds = safeDouble(fields, offset + 20, -1.0);
      cue.endAction = parseCueEndAction(safeString(fields, offset + 21));
      cue.cueTransitionSeconds = safeDouble(fields, offset + 22, -1.0);
      cue.cueTransitionStyle = safeString(fields, offset + 23);
      cue.lowerThirdText = safeString(fields, offset + 24);
      cue.lowerThirdSubtext = safeString(fields, offset + 25);
      cue.lowerThirdBgAlpha = safeInt(fields, offset + 26, 180);
      cue.stillDurationSeconds = std::max(0.0, safeDouble(fields, offset + 27, 0.0));
      cue.loopCount = safeInt(fields, offset + 28, 0);
      cue.playbackSpeed = std::clamp(safeDouble(fields, offset + 29, 1.0), 0.25, 4.0);
      cue.colorTag = safeString(fields, offset + 30);
      cue.notes = safeString(fields, offset + 31);
      cue.outputScaleX = static_cast<float>(std::clamp(safeDouble(fields, offset + 32, 1.0), 0.25, 4.0));
      cue.outputScaleY = static_cast<float>(std::clamp(safeDouble(fields, offset + 33, 1.0), 0.25, 4.0));
      cue.scaleMode = static_cast<ScaleMode>(safeInt(fields, offset + 34, 0));
      cue.outputOffsetX = static_cast<float>(safeDouble(fields, offset + 35, 0.0));
      cue.outputOffsetY = static_cast<float>(safeDouble(fields, offset + 36, 0.0));
      cue.cueNumber = safeString(fields, offset + 37);
      {
        std::string ppStr = safeString(fields, offset + 38);
        if (!ppStr.empty()) {
          std::istringstream ss(ppStr);
          std::string tok;
          while (std::getline(ss, tok, ',')) {
            try { cue.pausePoints.push_back(std::stod(tok)); } catch (...) {}
          }
          std::sort(cue.pausePoints.begin(), cue.pausePoints.end());
        }
      }
      cue.outputRotationDegrees = static_cast<float>(safeDouble(fields, offset + 39, 0.0));
      cue.cropLeft = static_cast<float>(safeDouble(fields, offset + 40, 0.0));
      cue.cropRight = static_cast<float>(safeDouble(fields, offset + 41, 0.0));
      cue.cropTop = static_cast<float>(safeDouble(fields, offset + 42, 0.0));
      cue.cropBottom = static_cast<float>(safeDouble(fields, offset + 43, 0.0));
      cue.chromaKeyEnabled = safeBool(fields, offset + 44, false);
      cue.chromaKeyColor = parseColor(safeString(fields, offset + 45));
      cue.chromaKeyTolerance = static_cast<float>(safeDouble(fields, offset + 46, 60.0));
      cue.chromaKeySoftness = static_cast<float>(safeDouble(fields, offset + 47, 20.0));
      cue.brightness = std::clamp(static_cast<float>(safeDouble(fields, offset + 48, 1.0)), 0.0f, 2.0f);
      cue.contrast = std::clamp(static_cast<float>(safeDouble(fields, offset + 49, 1.0)), 0.0f, 2.0f);
      cue.saturation = std::clamp(static_cast<float>(safeDouble(fields, offset + 50, 1.0)), 0.0f, 2.0f);
      cue.hueShift = std::clamp(static_cast<float>(safeDouble(fields, offset + 51, 0.0)), -180.0f, 180.0f);
      cue.cueId = normalizeCueIdShort(safeString(fields, offset + 52));
      cue.audioEnabled = safeBool(fields, offset + 53, true);
      cue.pauseAtBeginning = safeBool(fields, offset + 54, false);
      cue.transitionToNext = safeBool(fields, offset + 55, true);
      cue.gotoTarget = safeString(fields, offset + 56);
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
  using AudioTapCallback = std::function<void(const std::vector<std::int16_t>&)>;

  explicit MediaEngine(SDL_Renderer* outputRenderer, SDL_AudioDeviceID audioDevice, AudioTapCallback audioTap = {})
    : outputRenderer_(outputRenderer), audioDevice_(audioDevice), audioTap_(std::move(audioTap)) {}

  ~MediaEngine() {
    stopAll();
  }

  void stopAll() {
    stopDecoderThreads();
    isBrowserCapturing_ = false;
    isSourceCapturing_ = false;
    clearTexture();
    clearTransitionTexture();
    clearAudio();
    state_ = TransportState::Stopped;
    currentPosition_ = 0.0;
    duration_ = 0.0;
    cueInPointSeconds_ = 0.0;
    cueOutPointSeconds_ = 0.0;
    activeCue_ = nullptr;
    frameRate_ = 0.0;
    lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
    displayFrame_.reset();
  }

  void loadCue(const Cue* cue, bool autoplay, double transitionSeconds = 0.0, TransitionStyle transitionStyle = TransitionStyle::Cut) {
    // Capture outgoing fade gain BEFORE stopping decoder (position is still valid now).
    float outgoingGain = static_cast<float>(fadeGainAt(position()));
    stopDecoderThreads();
    beginTransition(transitionSeconds, transitionStyle, outgoingGain);
    clearTexture();
    clearAudio();
    activeCue_ = cue;
    outputScaleX_ = cue ? cue->outputScaleX : 1.0f;
    outputScaleY_ = cue ? cue->outputScaleY : 1.0f;
    scaleMode_ = cue ? cue->scaleMode : ScaleMode::Fit;
    outputOffsetX_ = cue ? cue->outputOffsetX : 0.0f;
    outputOffsetY_ = cue ? cue->outputOffsetY : 0.0f;
    outputRotationDegrees_ = cue ? cue->outputRotationDegrees : 0.0f;
    cropLeft_ = cue ? cue->cropLeft : 0.0f;
    cropRight_ = cue ? cue->cropRight : 0.0f;
    cropTop_ = cue ? cue->cropTop : 0.0f;
    cropBottom_ = cue ? cue->cropBottom : 0.0f;
    chromaKeyEnabled_ = cue ? cue->chromaKeyEnabled : false;
    chromaKeyColor_ = cue ? cue->chromaKeyColor : SDL_Color {0, 255, 0, 255};
    chromaKeyTolerance_ = cue ? cue->chromaKeyTolerance : 60.0f;
    chromaKeySoftness_ = cue ? cue->chromaKeySoftness : 20.0f;
    brightness_ = cue ? std::clamp(cue->brightness, 0.0f, 2.0f) : 1.0f;
    contrast_ = cue ? std::clamp(cue->contrast, 0.0f, 2.0f) : 1.0f;
    saturation_ = cue ? std::clamp(cue->saturation, 0.0f, 2.0f) : 1.0f;
    hueShift_ = cue ? std::clamp(cue->hueShift, -180.0f, 180.0f) : 0.0f;
    pausePoints_   = cue ? cue->pausePoints   : std::vector<double>{};
    nextPausePointIdx_ = 0;
    displayFrame_.reset();
    currentPosition_ = 0.0;
    pausedPosition_ = 0.0;
    playbackStartPosition_ = 0.0;
    duration_ = cue ? cue->duration : 0.0;
    cueInPointSeconds_ = 0.0;
    cueOutPointSeconds_ = cue ? cue->duration : 0.0;
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
      initStillTimer(*cue, autoplay);
      return;
    }

    if (cue->kind == CueKind::Pattern) {
      loadPatternFrame(*cue);
      initStillTimer(*cue, autoplay);
      return;
    }

    if (isSourceCueKind(cue->kind)) {
      loadSourceFrame(*cue);
      duration_ = 0.0;
      pausedPosition_ = 0.0;
      currentPosition_ = 0.0;
      state_ = TransportState::Paused;
      if (autoplay) {
        startSourceCapture(*cue);
      }
      return;
    }

    if (cue->kind == CueKind::Browser || cue->kind == CueKind::LowerThird) {
      initStillTimer(*cue, autoplay);
      return;
    }

    cueInPointSeconds_ = std::clamp(cue->inPointSeconds, 0.0, std::max(0.0, cue->duration));
    cueOutPointSeconds_ = cue->outPointSeconds > 0.0 ? cue->outPointSeconds : cue->duration;
    cueOutPointSeconds_ = std::clamp(cueOutPointSeconds_, cueInPointSeconds_, std::max(cueInPointSeconds_, cue->duration));
    duration_ = std::max(0.01, cueOutPointSeconds_ - cueInPointSeconds_);

    startDecoderThreads(*cue, cueInPointSeconds_, 0.0);
    state_ = autoplay ? TransportState::Playing : TransportState::Paused;
    playbackClockStart_ = std::chrono::steady_clock::now();
    playbackStartPosition_ = 0.0;
    pausedPosition_ = 0.0;
    SDL_PauseAudioDevice(audioDevice_, autoplay ? 0 : 1);
  }

  void play() {
    if (!activeCue_) return;
    if (isSourceCueKind(activeCue_->kind)) {
      if (isSourceCapturing_) {
        state_ = TransportState::Playing;
        return;
      }
      if (startSourceCapture(*activeCue_)) {
        state_ = TransportState::Playing;
      }
      return;
    }
    bool isTimedStill = activeCue_->kind != CueKind::Video && duration_ > 0.0;
    if (activeCue_->kind != CueKind::Video && !isTimedStill) return;
    if (state_ == TransportState::Playing) return;
    playbackClockStart_ = std::chrono::steady_clock::now();
    playbackStartPosition_ = pausedPosition_;
    state_ = TransportState::Playing;
    if (activeCue_->kind == CueKind::Video || activeCue_->kind == CueKind::Audio) SDL_PauseAudioDevice(audioDevice_, 0);
  }

  void pause() {
    if (!activeCue_) return;
    if (isSourceCueKind(activeCue_->kind)) {
      if (isSourceCapturing_) {
        stopDecoderThreads();
      }
      isSourceCapturing_ = false;
      pausedPosition_ = 0.0;
      currentPosition_ = 0.0;
      state_ = TransportState::Paused;
      return;
    }
    bool isAV = activeCue_->kind == CueKind::Video || activeCue_->kind == CueKind::Audio;
    bool isTimedStill = !isAV && duration_ > 0.0;
    if (!isAV && !isTimedStill) return;
    if (state_ != TransportState::Playing) {
      state_ = TransportState::Paused;
      return;
    }
    pausedPosition_ = position();
    currentPosition_ = pausedPosition_;
    state_ = TransportState::Paused;
    if (activeCue_->kind == CueKind::Video || activeCue_->kind == CueKind::Audio) SDL_PauseAudioDevice(audioDevice_, 1);
  }

  void toggle() {
    if (!activeCue_) return;
    if (isSourceCueKind(activeCue_->kind)) {
      if (isSourceCapturing_ || state_ == TransportState::Playing) {
        pause();
      } else {
        play();
      }
      return;
    }
    bool isTimedStill = activeCue_->kind != CueKind::Video && duration_ > 0.0;
    if (activeCue_->kind != CueKind::Video && !isTimedStill) return;
    if (state_ == TransportState::Playing) { pause(); } else { play(); }
  }

  void stop() {
    if (!activeCue_) {
      return;
    }
    if (isSourceCueKind(activeCue_->kind)) {
      if (isSourceCapturing_) {
        stopDecoderThreads();
      }
      isSourceCapturing_ = false;
      loadSourceFrame(*activeCue_);
      state_ = TransportState::Paused;
      pausedPosition_ = 0.0;
      currentPosition_ = 0.0;
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
    // Stills have no seek position — keep the loaded frame as-is.
    if (activeCue_->kind == CueKind::Image || isSourceCueKind(activeCue_->kind)) {
      return;
    }
    double clamped = std::clamp(seconds, 0.0, duration_);
    pausedPosition_ = clamped;
    currentPosition_ = clamped;
    playbackStartPosition_ = clamped;
    playbackClockStart_ = std::chrono::steady_clock::now();
    lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
    displayFrame_.reset();
    // Reset to first pause point strictly after the new position
    nextPausePointIdx_ = 0;
    while (nextPausePointIdx_ < pausePoints_.size() && pausePoints_[nextPausePointIdx_] <= clamped) {
      ++nextPausePointIdx_;
    }
    clearTransitionTexture();
    clearTexture();
    clearAudio();
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
    startDecoderThreads(*activeCue_, cueInPointSeconds_ + clamped, clamped);
    SDL_PauseAudioDevice(audioDevice_, state_ == TransportState::Playing ? 0 : 1);
  }

  void setVolume(float value) {
    volume_.store(std::clamp(value, 0.0f, 1.0f));
  }

  void setPausePoints(std::vector<double> points) {
    std::sort(points.begin(), points.end());
    pausePoints_ = std::move(points);
    // Reset index to first point strictly after current position
    double pos = position();
    nextPausePointIdx_ = 0;
    while (nextPausePointIdx_ < pausePoints_.size() && pausePoints_[nextPausePointIdx_] <= pos) {
      ++nextPausePointIdx_;
    }
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
    if (state_ == TransportState::Playing) {
      auto now = std::chrono::steady_clock::now();
      double elapsed = std::chrono::duration<double>(now - playbackClockStart_).count();
      return std::clamp(playbackStartPosition_ + elapsed, 0.0,
                        duration_ > 0.0 ? duration_ : elapsed);
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
    // Upload any image frame that finished decoding on the background thread.
    if (imageFramePending_.exchange(false)) {
      std::lock_guard<std::mutex> lk(imageMutex_);
      if (pendingImageFrame_) {
        displayFrame_ = std::move(pendingImageFrame_);
        uploadFrame(*displayFrame_);
      }
    }

    if (!activeCue_) {
      return;
    }

    if (activeCue_->kind != CueKind::Video && !isBrowserCapturing_ && !isSourceCapturing_) {
      // Timed stills and audio-only: advance position via wall clock, handle end.
      if (duration_ > 0.0 && state_ == TransportState::Playing) {
        currentPosition_ = position();
        // Pause points (applies to audio cues as well)
        if (nextPausePointIdx_ < pausePoints_.size() &&
            currentPosition_ >= pausePoints_[nextPausePointIdx_]) {
          ++nextPausePointIdx_;
          pause();
          return;
        }
        if (currentPosition_ >= duration_ - 0.01) {
          handlePlaybackEnd();
        }
      } else {
        currentPosition_ = 0.0;
      }
      return;
    }

    currentPosition_ = position();

    // Auto-pause at pause points
    if (state_ == TransportState::Playing && nextPausePointIdx_ < pausePoints_.size()) {
      if (currentPosition_ >= pausePoints_[nextPausePointIdx_]) {
        ++nextPausePointIdx_;
        pause();
        return;
      }
    }

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

  const DecodedFrame* currentFrame() const {
    return displayFrame_.has_value() ? &(*displayFrame_) : nullptr;
  }

  // Start capturing a virtual X11 display via ffmpeg x11grab.
  // Called after Xvfb + browser are running; frames feed the normal pipeline.
  void startBrowserCapture(const std::string& displayId, int w, int h,
                           double /*fadeInSeconds*/, double /*fadeOutSeconds*/,
                           double transSecs, TransitionStyle transStyle) {
    stopDecoderThreads();
    isBrowserCapturing_ = true;
    frameRate_ = 30.0;
    duration_ = 0.0;  // infinite
    browserCaptureW_ = w;
    browserCaptureH_ = h;
    // Trigger transition from whatever was on screen before browser boots.
    if (transSecs > 0.0 && texture_) {
      beginTransition(transSecs, transStyle);
    }

    if (!spawnPipeProcess(videoProcess_, {
      "ffmpeg",
      "-hide_banner",
      "-loglevel", "error",
      "-f", "x11grab",
      "-display", displayId,
      "-r", "30",
      "-i", "0+0,0",
      "-vf", "scale=" + std::to_string(w) + ":" + std::to_string(h),
      "-f", "rawvideo",
      "-pix_fmt", "rgba",
      "pipe:1"
    })) {
      isBrowserCapturing_ = false;
      return;
    }

    const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    int videoFd = videoProcess_.readFd;
    playbackClockStart_ = std::chrono::steady_clock::now();
    playbackStartPosition_ = 0.0;
    state_ = TransportState::Playing;

    videoThread_ = std::thread([this, w, h, frameBytes, videoFd]() {
      std::uint64_t frameIdx = 0;
      while (!decoderStop_.load()) {
        while (!decoderStop_.load()) {
          bool hasRoom = false;
          { std::lock_guard<std::mutex> lk(frameMutex_); hasRoom = frameQueue_.size() < kMaxVideoFrames; }
          if (hasRoom) break;
          SDL_Delay(4);
        }
        if (decoderStop_.load()) break;
        DecodedFrame frame;
        frame.width = w;
        frame.height = h;
        frame.index = frameIdx++;
        frame.pixels.resize(frameBytes);
        if (!readExact(videoFd, frame.pixels.data(), frameBytes)) {
          decoderEof_ = true;
          break;
        }
        std::lock_guard<std::mutex> lk(frameMutex_);
        frameQueue_.push_back(std::move(frame));
      }
      decoderEof_ = true;
    });
  }

  void stopBrowserCapture() {
    isBrowserCapturing_ = false;
    stopDecoderThreads();
  }

  bool isBrowserCapturing() const { return isBrowserCapturing_; }
  bool isSourceCapturing() const { return isSourceCapturing_; }

  bool startSourceCapture(const Cue& cue) {
#ifdef _WIN32
    (void) cue;
    return false;
#else
    if (!isSourceCueKind(cue.kind)) {
      return false;
    }

    auto [fallbackW, fallbackH] = currentOutputSizeHint();
    int w = cue.width > 0 ? cue.width : fallbackW;
    int h = cue.height > 0 ? cue.height : fallbackH;
    w = std::clamp(w, 160, 3840);
    h = std::clamp(h, 90, 2160);

    std::vector<std::string> args;
    if (!buildSourceCaptureArgs(cue, w, h, args)) {
      return false;
    }

    stopDecoderThreads();
    isSourceCapturing_ = false;

    if (!spawnPipeProcess(videoProcess_, args)) {
      return false;
    }

    const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    int videoFd = videoProcess_.readFd;
    frameRate_ = 30.0;
    duration_ = 0.0;
    playbackClockStart_ = std::chrono::steady_clock::now();
    playbackStartPosition_ = 0.0;
    pausedPosition_ = 0.0;
    currentPosition_ = 0.0;
    state_ = TransportState::Playing;
    isSourceCapturing_ = true;

    videoThread_ = std::thread([this, w, h, frameBytes, videoFd]() {
      std::uint64_t frameIdx = 0;
      while (!decoderStop_.load()) {
        while (!decoderStop_.load()) {
          bool hasRoom = false;
          { std::lock_guard<std::mutex> lk(frameMutex_); hasRoom = frameQueue_.size() < kMaxVideoFrames; }
          if (hasRoom) break;
          SDL_Delay(4);
        }
        if (decoderStop_.load()) break;

        DecodedFrame frame;
        frame.width = w;
        frame.height = h;
        frame.index = frameIdx++;
        frame.pixels.resize(frameBytes);
        if (!readExact(videoFd, frame.pixels.data(), frameBytes)) {
          decoderEof_ = true;
          break;
        }

        std::lock_guard<std::mutex> lk(frameMutex_);
        frameQueue_.push_back(std::move(frame));
      }
      decoderEof_ = true;
    });

    return true;
#endif
  }

  // Rebuild the current pattern frame with a wall-clock timestamp (for animation).
  void rebuildPatternFrame(const Cue& cue, double wallSeconds) {
    auto [fallbackW, fallbackH] = currentOutputSizeHint();
    auto frame = buildPatternFrame(cue, wallSeconds, fallbackW, fallbackH);
    if (frame) {
      displayFrame_ = std::move(frame);
      uploadFrame(*displayFrame_);
    }
  }

  void render(SDL_Rect target) {
    bool pixelEffectsChanged = false;
    if (activeCue_) {
      bool prevKeyEnabled = chromaKeyEnabled_;
      SDL_Color prevKeyColor = chromaKeyColor_;
      float prevKeyTolerance = chromaKeyTolerance_;
      float prevKeySoftness = chromaKeySoftness_;
      float prevBrightness = brightness_;
      float prevContrast = contrast_;
      float prevSaturation = saturation_;
      float prevHueShift = hueShift_;

      outputScaleX_ = activeCue_->outputScaleX;
      outputScaleY_ = activeCue_->outputScaleY;
      scaleMode_ = activeCue_->scaleMode;
      outputOffsetX_ = activeCue_->outputOffsetX;
      outputOffsetY_ = activeCue_->outputOffsetY;
      outputRotationDegrees_ = activeCue_->outputRotationDegrees;
      cropLeft_ = activeCue_->cropLeft;
      cropRight_ = activeCue_->cropRight;
      cropTop_ = activeCue_->cropTop;
      cropBottom_ = activeCue_->cropBottom;
      chromaKeyEnabled_ = activeCue_->chromaKeyEnabled;
      chromaKeyColor_ = activeCue_->chromaKeyColor;
      chromaKeyTolerance_ = activeCue_->chromaKeyTolerance;
      chromaKeySoftness_ = activeCue_->chromaKeySoftness;
      brightness_ = std::clamp(activeCue_->brightness, 0.0f, 2.0f);
      contrast_ = std::clamp(activeCue_->contrast, 0.0f, 2.0f);
      saturation_ = std::clamp(activeCue_->saturation, 0.0f, 2.0f);
      hueShift_ = std::clamp(activeCue_->hueShift, -180.0f, 180.0f);

      pixelEffectsChanged =
        prevKeyEnabled != chromaKeyEnabled_
        || prevKeyColor.r != chromaKeyColor_.r
        || prevKeyColor.g != chromaKeyColor_.g
        || prevKeyColor.b != chromaKeyColor_.b
        || std::fabs(prevKeyTolerance - chromaKeyTolerance_) > 0.001f
        || std::fabs(prevKeySoftness - chromaKeySoftness_) > 0.001f
        || std::fabs(prevBrightness - brightness_) > 0.001f
        || std::fabs(prevContrast - contrast_) > 0.001f
        || std::fabs(prevSaturation - saturation_) > 0.001f
        || std::fabs(prevHueShift - hueShift_) > 0.001f;
    }
    if (pixelEffectsChanged && displayFrame_) {
      uploadFrame(*displayFrame_);
    }

    SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, 255);
    SDL_RenderFillRect(outputRenderer_, nullptr);

    bool drewCurrent = drawTextureFitted(texture_, textureWidth_, textureHeight_, target, 255);
    drawTransitionOverlay(target, drewCurrent);

    double gain = fadeGainAt(position());
    if (drewCurrent && gain < 0.999) {
      SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, static_cast<Uint8>((1.0 - gain) * 255.0));
      SDL_RenderFillRect(outputRenderer_, nullptr);
    }
  }

 private:
  // Called after loading any still cue (Image, Pattern, Browser, LowerThird).
  // If stillDurationSeconds > 0 it arms a countdown timer so the cue
  // auto-advances after that long — making stills usable in playlists.
  void initStillTimer(const Cue& cue, bool autoplay) {
    if (cue.stillDurationSeconds > 0.0) {
      duration_ = cue.stillDurationSeconds;
      if (autoplay) {
        playbackClockStart_ = std::chrono::steady_clock::now();
        playbackStartPosition_ = 0.0;
        pausedPosition_ = 0.0;
        state_ = TransportState::Playing;
      } else {
        state_ = TransportState::Paused;
      }
    } else {
      duration_ = 0.0;
      state_ = TransportState::Paused;
    }
  }

  void beginTransition(double seconds, TransitionStyle style, float sourceGain = 1.0f) {
    clearTransitionTexture();
    if (!texture_) {
      return;
    }
    // Steal the last frame so we never flash black between cues.
    transitionTexture_ = texture_;
    transitionTextureWidth_ = textureWidth_;
    transitionTextureHeight_ = textureHeight_;
    texture_ = nullptr;
    textureWidth_ = 0;
    textureHeight_ = 0;
    transitionDurationSeconds_ = std::clamp(seconds, 0.0, 10.0);
    transitionStyle_ = style;
    transitionSourceGain_ = std::clamp(sourceGain, 0.0f, 1.0f);
    // Timer doesn't start until the first new frame arrives (see drawTransitionOverlay).
    transitionActive_ = true;
    transitionWaitingForFirstFrame_ = true;
  }

  void clearTransitionTexture() {
    if (transitionTexture_) {
      SDL_DestroyTexture(transitionTexture_);
      transitionTexture_ = nullptr;
    }
    transitionTextureWidth_ = 0;
    transitionTextureHeight_ = 0;
    transitionActive_ = false;
    transitionWaitingForFirstFrame_ = false;
    transitionDurationSeconds_ = 0.0;
    transitionStyle_ = TransitionStyle::Cut;
    transitionSourceGain_ = 1.0f;
  }

  bool drawTextureFitted(SDL_Texture* texture, int width, int height, const SDL_Rect& target, Uint8 alphaValue) {
    if (!texture || width <= 0 || height <= 0) {
      return false;
    }
    int cropL = std::clamp(static_cast<int>(std::lround(static_cast<double>(width) * cropLeft_)), 0, width - 1);
    int cropR = std::clamp(static_cast<int>(std::lround(static_cast<double>(width) * cropRight_)), 0, width - 1);
    int cropT = std::clamp(static_cast<int>(std::lround(static_cast<double>(height) * cropTop_)), 0, height - 1);
    int cropB = std::clamp(static_cast<int>(std::lround(static_cast<double>(height) * cropBottom_)), 0, height - 1);
    int srcW = std::max(1, width - cropL - cropR);
    int srcH = std::max(1, height - cropT - cropB);
    SDL_Rect source {cropL, cropT, srcW, srcH};

    // Calculate scale based on scaleMode_
    double scale;
    if (scaleMode_ == ScaleMode::Fit) {
      // Letterbox: fit entire image, maintain aspect ratio
      scale = std::min(
        static_cast<double>(target.w) / static_cast<double>(srcW),
        static_cast<double>(target.h) / static_cast<double>(srcH)
      );
    } else if (scaleMode_ == ScaleMode::Fill) {
      // Fill screen and crop, maintain aspect ratio
      scale = std::max(
        static_cast<double>(target.w) / static_cast<double>(srcW),
        static_cast<double>(target.h) / static_cast<double>(srcH)
      );
    } else if (scaleMode_ == ScaleMode::Stretch) {
      // Fill screen, ignore aspect ratio (distort)
      scale = 1.0;  // Will be handled separately per dimension
    } else {  // Unscaled
      scale = 1.0;
    }
    
    int drawW, drawH;
    if (scaleMode_ == ScaleMode::Stretch) {
      drawW = target.w;
      drawH = target.h;
    } else if (scaleMode_ == ScaleMode::Unscaled) {
      drawW = srcW;
      drawH = srcH;
    } else {
      drawW = std::max(1, static_cast<int>(std::round(srcW * scale)));
      drawH = std::max(1, static_cast<int>(std::round(srcH * scale)));
    }
    
    // Apply per-cue output geometry
    int scaledW = std::max(1, static_cast<int>(drawW * outputScaleX_));
    int scaledH = std::max(1, static_cast<int>(drawH * outputScaleY_));
    SDL_Rect destination {
      target.x + (target.w - scaledW) / 2 + static_cast<int>(outputOffsetX_),
      target.y + (target.h - scaledH) / 2 + static_cast<int>(outputOffsetY_),
      scaledW,
      scaledH
    };

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, alphaValue);
    SDL_Point center {destination.w / 2, destination.h / 2};
    SDL_RenderCopyEx(outputRenderer_, texture, &source, &destination, outputRotationDegrees_, &center, SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(texture, 255);
    return true;
  }

  void drawTransitionOverlay(const SDL_Rect& target, bool drewCurrent) {
    if (!transitionActive_) {
      return;
    }

    if (transitionWaitingForFirstFrame_) {
      if (drewCurrent) {
        // First new frame arrived — start timed transition now.
        transitionWaitingForFirstFrame_ = false;
        transitionStartedAt_ = std::chrono::steady_clock::now();
        if (transitionDurationSeconds_ <= 0.001) {
          clearTransitionTexture();  // cut: instantly done
          return;
        }
        // fall through to begin the timed transition immediately
      } else {
        // New frame not ready yet — keep showing old frame, respecting its fade gain.
        Uint8 waitAlpha = static_cast<Uint8>(transitionSourceGain_ * 255.0f);
        drawTextureFitted(transitionTexture_, transitionTextureWidth_, transitionTextureHeight_, target, waitAlpha);
        // If outgoing was partially/fully faded, fill black on top.
        if (waitAlpha < 255) {
          SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, 255 - waitAlpha);
          SDL_RenderFillRect(outputRenderer_, nullptr);
          SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_NONE);
        }
        return;
      }
    }

    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - transitionStartedAt_).count();
    double progress = transitionDurationSeconds_ <= 0.0001 ? 1.0 : std::clamp(elapsed / transitionDurationSeconds_, 0.0, 1.0);
    if (progress >= 1.0) {
      clearTransitionTexture();
      return;
    }

    if (transitionStyle_ == TransitionStyle::DipBlack) {
      if (progress < 0.5) {
        Uint8 srcA = static_cast<Uint8>(transitionSourceGain_ * 255.0f);
        drawTextureFitted(transitionTexture_, transitionTextureWidth_, transitionTextureHeight_, target, srcA);
        SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_BLEND);
        double blackAlpha = std::clamp(progress * 2.0, 0.0, 1.0);
        // Also account for already-faded source
        if (transitionSourceGain_ < 1.0f) blackAlpha = std::max(blackAlpha, 1.0 - transitionSourceGain_);
        SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, static_cast<Uint8>(blackAlpha * 255.0));
        SDL_RenderFillRect(outputRenderer_, nullptr);
      } else {
        if (!drewCurrent) {
          SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, 255);
          SDL_RenderFillRect(outputRenderer_, nullptr);
        }
        double fadeOutBlack = std::clamp(1.0 - (progress - 0.5) * 2.0, 0.0, 1.0);
        SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, static_cast<Uint8>(fadeOutBlack * 255.0));
        SDL_RenderFillRect(outputRenderer_, nullptr);
      }
      return;
    }

    // Crossfade: old frame alpha starts at sourceGain and fades to 0.
    Uint8 alphaValue = static_cast<Uint8>(transitionSourceGain_ * std::clamp(1.0 - progress, 0.0, 1.0) * 255.0);
    drawTextureFitted(transitionTexture_, transitionTextureWidth_, transitionTextureHeight_, target, alphaValue);
  }

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
    // Resolve effective end behaviour from endAction, falling back to per-cue flags.
    CueEndAction act = activeCue_->endAction;
    if (act == CueEndAction::Inherit) {
      if (activeCue_->loop)             act = CueEndAction::Loop;
      else if (activeCue_->pauseOnLastFrame) act = CueEndAction::PauseOnLast;
    }

    if (act == CueEndAction::Loop) {
      seek(0.0);
      state_ = TransportState::Playing;
      playbackClockStart_ = std::chrono::steady_clock::now();
      playbackStartPosition_ = 0.0;
      pausedPosition_ = 0.0;
      SDL_PauseAudioDevice(audioDevice_, 0);
      return;
    }
    if (act == CueEndAction::PauseOnLast) {
      state_ = TransportState::Paused;
      pausedPosition_ = duration_;
      currentPosition_ = duration_;
      SDL_PauseAudioDevice(audioDevice_, 1);
      return;
    }
    // Stop, AutoNext, or Inherit-with-no-flags: stop and let the App decide advance.
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
    if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
      return;
    }
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
    const std::uint8_t* uploadPixels = frame.pixels.data();
    if (chromaKeyEnabled_ || colorControlsActive(brightness_, contrast_, saturation_, hueShift_)) {
      keyedPixelsScratch_.assign(frame.pixels.begin(), frame.pixels.end());
      applyCueVisualEffectsToPixels(
        keyedPixelsScratch_,
        chromaKeyEnabled_,
        chromaKeyColor_,
        chromaKeyTolerance_,
        chromaKeySoftness_,
        brightness_,
        contrast_,
        saturation_,
        hueShift_);
      uploadPixels = keyedPixelsScratch_.data();
    }
    SDL_UpdateTexture(texture_, nullptr, uploadPixels, frame.width * 4);
  }

  void stopImageThread() {
    imageProcess_.stop();   // closes pipe → readExact in thread returns → thread exits
    if (imageThread_.joinable()) {
      imageThread_.join();
    }
    std::lock_guard<std::mutex> lk(imageMutex_);
    pendingImageFrame_.reset();
    imageFramePending_.store(false);
  }

  std::pair<int, int> currentOutputSizeHint() const {
    int w = kOutputWidth;
    int h = kOutputHeight;
    if (outputRenderer_) {
      int rw = 0;
      int rh = 0;
      if (SDL_GetRendererOutputSize(outputRenderer_, &rw, &rh) == 0 && rw > 0 && rh > 0) {
        w = rw;
        h = rh;
      }
    }
    return {w, h};
  }

  void loadStillFrame(const Cue& cue) {
    stopImageThread();

    auto [capW, capH] = currentOutputSizeHint();
    int w = cue.width > 0 ? cue.width : capW;
    int h = cue.height > 0 ? cue.height : capH;
    if (w > capW || h > capH) {
      double scale = std::min(
        static_cast<double>(capW) / w,
        static_cast<double>(capH) / h
      );
      w = std::max(1, static_cast<int>(w * scale));
      h = std::max(1, static_cast<int>(h * scale));
    }

    // Spawn ffmpeg on the main thread so imageProcess_ is fully set up before the
    // reader thread starts — avoids any race on imageProcess_.pid / readFd.
    if (!spawnPipeProcess(imageProcess_, {
      "ffmpeg", "-hide_banner", "-loglevel", "error",
      "-i", cue.path,
      "-frames:v", "1",
      "-vf", "scale=" + std::to_string(w) + ":" + std::to_string(h) + ":flags=neighbor",
      "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"
    })) {
      return;
    }

    const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    // Capture fd by value to avoid a data race with the main thread calling stop().
    int imageFd = imageProcess_.readFd;
    imageThread_ = std::thread([this, w, h, frameBytes, imageFd]() {
      DecodedFrame frame;
      frame.width = w;
      frame.height = h;
      frame.index = 0;
      frame.pixels.resize(frameBytes);
      if (readExact(imageFd, frame.pixels.data(), frameBytes)) {
        std::lock_guard<std::mutex> lk(imageMutex_);
        pendingImageFrame_ = std::move(frame);
        imageFramePending_.store(true);
      }
    });
  }

  void loadPatternFrame(const Cue& cue) {
    auto [fallbackW, fallbackH] = currentOutputSizeHint();
    auto frame = buildPatternFrame(cue, 0.0, fallbackW, fallbackH);
    if (frame) {
      displayFrame_ = std::move(frame);
      uploadFrame(*displayFrame_);
    }
  }

  void loadSourceFrame(const Cue& cue) {
    auto [fallbackW, fallbackH] = currentOutputSizeHint();
    int w = cue.width > 0 ? cue.width : fallbackW;
    int h = cue.height > 0 ? cue.height : fallbackH;
    w = std::clamp(w, 64, 3840);
    h = std::clamp(h, 64, 2160);

    DecodedFrame frame;
    frame.width = w;
    frame.height = h;
    frame.index = 0;
    frame.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);

    SDL_Color bg {48, 98, 48, 255};
    SDL_Color stripe {15, 56, 15, 255};
    SDL_Color accent {139, 172, 15, 255};
    if (cue.kind == CueKind::Camera) {
      bg = SDL_Color {36, 82, 54, 255};
      stripe = SDL_Color {16, 38, 28, 255};
      accent = SDL_Color {155, 208, 125, 255};
    } else if (cue.kind == CueKind::Syphon) {
      bg = SDL_Color {56, 62, 30, 255};
      stripe = SDL_Color {28, 34, 16, 255};
      accent = SDL_Color {177, 188, 94, 255};
    }

    for (int y = 0; y < h; ++y) {
      bool stripeRow = ((y / 14) % 2) == 0;
      SDL_Color rowColor = stripeRow ? bg : stripe;
      for (int x = 0; x < w; ++x) {
        size_t offset = static_cast<size_t>(y * w + x) * 4u;
        frame.pixels[offset + 0] = rowColor.r;
        frame.pixels[offset + 1] = rowColor.g;
        frame.pixels[offset + 2] = rowColor.b;
        frame.pixels[offset + 3] = 255;
      }
    }

    int margin = std::max(6, std::min(w, h) / 18);
    for (int y = margin; y < h - margin; ++y) {
      for (int x = margin; x < w - margin; ++x) {
        bool border = (x - margin < 2) || (h - margin - 1 - y < 2)
          || (y - margin < 2) || (w - margin - 1 - x < 2);
        if (!border) {
          continue;
        }
        size_t offset = static_cast<size_t>(y * w + x) * 4u;
        frame.pixels[offset + 0] = accent.r;
        frame.pixels[offset + 1] = accent.g;
        frame.pixels[offset + 2] = accent.b;
        frame.pixels[offset + 3] = 255;
      }
    }

    displayFrame_ = std::move(frame);
    uploadFrame(*displayFrame_);
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

  // ── Pattern frame builder ────────────────────────────────────────────────

  // Full-color SMPTE 75% HD color bars (SMPTE RP 219 arrangement).
  static void buildSmpte75Bars(DecodedFrame& frame) {
    int W = frame.width, H = frame.height;
    // 7 bar colours at 75% saturation, in standard SMPTE order.
    struct Bar { Uint8 r, g, b; };
    constexpr std::array<Bar, 7> bars {{
      {191,191,191}, // 75% White
      {191,191,  0}, // Yellow
      {  0,191,191}, // Cyan
      {  0,191,  0}, // Green
      {191,  0,191}, // Magenta
      {191,  0,  0}, // Red
      {  0,  0,191}, // Blue
    }};
    int topH   = H * 2 / 3;
    int midH   = H / 12;
    int botH   = H - topH - midH;
    int barW   = W / 7;
    // Top section — 7 bars
    for (int i = 0; i < 7; ++i) {
      SDL_Color c {bars[i].r, bars[i].g, bars[i].b, 255};
      fillPixelRect(frame, i * barW, 0, barW + (i == 6 ? W - 6 * barW : 0), topH, c);
    }
    // Middle band — reverse blue order (cyan, black, magenta, black, white, black, blue)
    constexpr std::array<Bar, 7> midBars {{
      {  0,191,191}, // Cyan
      {  0,  0,  0}, // Black
      {191,  0,191}, // Magenta
      {  0,  0,  0},
      {191,191,191}, // White
      {  0,  0,  0},
      {  0,  0,191}, // Blue
    }};
    for (int i = 0; i < 7; ++i) {
      SDL_Color c {midBars[i].r, midBars[i].g, midBars[i].b, 255};
      fillPixelRect(frame, i * barW, topH, barW + (i == 6 ? W - 6 * barW : 0), midH, c);
    }
    // Bottom band — PLUGE: near-black, black, slightly-above-black strip + white reference
    int botBarW = W / 4;
    fillPixelRect(frame, 0,          topH + midH, botBarW, botH, {  0,  0,  0, 255}); // Black
    fillPixelRect(frame, botBarW,    topH + midH, botBarW, botH, {255,255,255, 255}); // White ref
    fillPixelRect(frame, botBarW*2,  topH + midH, botBarW, botH, { 10, 10, 10, 255}); // Near-black
    fillPixelRect(frame, botBarW*3,  topH + midH, W - botBarW*3, botH, { 4,  4,  4, 255}); // PLUGE
    // Thin label text baked in — just draw a coloured ID marker per bar
    for (int i = 0; i < 7; ++i) {
      SDL_Color label {bars[i].r > 100 ? Uint8(0) : Uint8(230),
                       bars[i].g > 100 ? Uint8(0) : Uint8(230),
                       bars[i].b > 100 ? Uint8(0) : Uint8(230), 255};
      fillPixelRect(frame, i * barW + 2, topH - 14, barW - 4, 10, label);
    }
  }

  static void buildCrosshatch(DecodedFrame& frame, int phaseX = 0, int phaseY = 0) {
    int W = frame.width;
    int H = frame.height;
    constexpr int kStep = 64;
    int shiftX = ((phaseX % kStep) + kStep) % kStep;
    int shiftY = ((phaseY % kStep) + kStep) % kStep;
    // Black background
    fillPixelRect(frame, 0, 0, W, H, {0, 0, 0, 255});
    // White lines every 64px, 2px thick
    for (int x = -shiftX; x < W; x += kStep) {
      fillPixelRect(frame, x, 0, 2, H, {255, 255, 255, 255});
    }
    for (int y = -shiftY; y < H; y += kStep) {
      fillPixelRect(frame, 0, y, W, 2, {255, 255, 255, 255});
    }
    // Centre cross in red
    fillPixelRect(frame, W / 2 - 1, 0,     2, H, {220,  40,  40, 255});
    fillPixelRect(frame, 0,     H / 2 - 1, W, 2, {220,  40,  40, 255});
    // Corner safe-area marks (80% safe)
    int sx = W / 10;
    int sy = H / 10;
    fillPixelRect(frame, sx, sy, W - sx * 2, 2, {60, 180, 60, 200});
    fillPixelRect(frame, sx, sy, 2, H - sy * 2, {60, 180, 60, 200});
    fillPixelRect(frame, W - sx - 2, sy, 2, H - sy * 2, {60, 180, 60, 200});
    fillPixelRect(frame, sx, H - sy - 2, W - sx * 2, 2, {60, 180, 60, 200});
  }

  static void buildCheckerboard(DecodedFrame& frame, int phaseX = 0, int phaseY = 0) {
    int W = frame.width;
    int H = frame.height;
    constexpr int cell = 64;
    int shiftX = ((phaseX % cell) + cell) % cell;
    int shiftY = ((phaseY % cell) + cell) % cell;
    for (int row = 0;; ++row) {
      int y = row * cell - shiftY;
      if (y >= H) {
        break;
      }
      int y0 = std::max(0, y);
      int y1 = std::min(H, y + cell);
      if (y1 <= y0) {
        continue;
      }
      for (int col = 0;; ++col) {
        int x = col * cell - shiftX;
        if (x >= W) {
          break;
        }
        int x0 = std::max(0, x);
        int x1 = std::min(W, x + cell);
        if (x1 <= x0) {
          continue;
        }
        bool white = ((row + col) % 2) == 0;
        SDL_Color c = white ? SDL_Color{255, 255, 255, 255}
                            : SDL_Color{0, 0, 0, 255};
        fillPixelRect(frame, x0, y0, x1 - x0, y1 - y0, c);
      }
    }
  }

  // Pocket Test - tropical retro platform-adventure inspired scene.
  // Full RGB, deterministic animation, no random text/noise bars.
  // `forcedScene`:
  //   -1 = automatic scene cycle
  //    0 = day
  //    1 = sunset
  //    2 = night
  //    3 = storm
  static void buildPocketTest(DecodedFrame& frame, double t, int forcedScene = -1) {
    const int W = frame.width;
    const int H = frame.height;

    auto put = [&](int x, int y, const SDL_Color& color) {
      if (x < 0 || y < 0 || x >= W || y >= H) {
        return;
      }
      size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 4u;
      frame.pixels[idx + 0] = color.r;
      frame.pixels[idx + 1] = color.g;
      frame.pixels[idx + 2] = color.b;
      frame.pixels[idx + 3] = color.a;
    };

    auto rect = [&](int x, int y, int w, int h, const SDL_Color& color) {
      if (w <= 0 || h <= 0) {
        return;
      }
      int x0 = std::max(0, x);
      int y0 = std::max(0, y);
      int x1 = std::min(W, x + w);
      int y1 = std::min(H, y + h);
      for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
          put(xx, yy, color);
        }
      }
    };

    auto lerpColor = [&](const SDL_Color& a, const SDL_Color& b, double v) -> SDL_Color {
      double tClamped = std::clamp(v, 0.0, 1.0);
      auto mix = [&](Uint8 aa, Uint8 bb) -> Uint8 {
        return static_cast<Uint8>(std::round(static_cast<double>(aa) * (1.0 - tClamped) + static_cast<double>(bb) * tClamped));
      };
      return SDL_Color {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), 255};
    };

    auto disc = [&](int cx, int cy, int radius, const SDL_Color& color) {
      int r2 = radius * radius;
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          if (dx * dx + dy * dy <= r2) {
            put(cx + dx, cy + dy, color);
          }
        }
      }
    };

    const int oceanTop = H * 52 / 100;
    const int beachTop = H * 83 / 100;
    int scene = forcedScene;
    if (scene < 0 || scene > 3) {
      scene = static_cast<int>(std::floor(t / 14.0)) % 4;
      if (scene < 0) {
        scene += 4;
      }
    }

    SDL_Color skyTop {60, 170, 225, 255};
    SDL_Color skyBottom {190, 245, 255, 255};
    SDL_Color oceanNear {15, 95, 170, 255};
    SDL_Color oceanFar {25, 130, 195, 255};
    SDL_Color sandTop {248, 226, 154, 255};
    SDL_Color sandBottom {226, 186, 108, 255};
    SDL_Color sunCore {255, 241, 150, 255};
    SDL_Color sunGlow {255, 226, 120, 120};
    SDL_Color cloudMain {245, 255, 255, 255};
    SDL_Color cloudShadow {205, 235, 245, 255};
    bool drawMoon = false;

    if (scene == 1) {
      skyTop = SDL_Color {246, 134, 98, 255};
      skyBottom = SDL_Color {255, 205, 142, 255};
      oceanNear = SDL_Color {40, 82, 156, 255};
      oceanFar = SDL_Color {76, 122, 198, 255};
      sandTop = SDL_Color {255, 214, 144, 255};
      sandBottom = SDL_Color {234, 172, 108, 255};
      sunCore = SDL_Color {255, 216, 114, 255};
      sunGlow = SDL_Color {255, 145, 92, 140};
      cloudMain = SDL_Color {255, 235, 224, 255};
      cloudShadow = SDL_Color {236, 193, 183, 255};
    } else if (scene == 2) {
      skyTop = SDL_Color {24, 38, 102, 255};
      skyBottom = SDL_Color {92, 136, 206, 255};
      oceanNear = SDL_Color {10, 50, 110, 255};
      oceanFar = SDL_Color {25, 76, 150, 255};
      sandTop = SDL_Color {164, 148, 115, 255};
      sandBottom = SDL_Color {132, 114, 90, 255};
      sunCore = SDL_Color {235, 242, 255, 255};
      sunGlow = SDL_Color {170, 198, 255, 120};
      cloudMain = SDL_Color {170, 188, 240, 255};
      cloudShadow = SDL_Color {120, 136, 188, 255};
      drawMoon = true;
    } else if (scene == 3) {
      skyTop = SDL_Color {34, 78, 108, 255};
      skyBottom = SDL_Color {102, 166, 180, 255};
      oceanNear = SDL_Color {22, 86, 124, 255};
      oceanFar = SDL_Color {38, 122, 156, 255};
      sandTop = SDL_Color {198, 184, 142, 255};
      sandBottom = SDL_Color {164, 142, 108, 255};
      sunCore = SDL_Color {230, 236, 245, 255};
      sunGlow = SDL_Color {184, 206, 238, 124};
      cloudMain = SDL_Color {186, 216, 224, 255};
      cloudShadow = SDL_Color {140, 168, 176, 255};
      drawMoon = true;
    }

    for (int y = 0; y < oceanTop; ++y) {
      double v = oceanTop > 1 ? static_cast<double>(y) / static_cast<double>(oceanTop - 1) : 0.0;
      rect(0, y, W, 1, lerpColor(skyTop, skyBottom, v));
    }

    if (scene >= 2) {
      int starCount = std::max(18, W / 54);
      for (int i = 0; i < starCount; ++i) {
        int sx = (i * 73 + 19) % std::max(1, W);
        int sy = 4 + ((i * 47 + 13) % std::max(8, oceanTop - 10));
        double twinkle = 0.5 + 0.5 * std::sin(t * 2.8 + static_cast<double>(i) * 0.71);
        Uint8 a = static_cast<Uint8>(120 + twinkle * 120.0);
        rect(sx, sy, 1, 1, SDL_Color {236, 242, 255, a});
        if ((i % 3) == 0) {
          rect(sx - 1, sy, 3, 1, SDL_Color {210, 226, 255, static_cast<Uint8>(a / 2)});
        }
      }
    }

    int sunX = static_cast<int>(W * 0.78 + std::sin(t * 0.14) * (W * 0.04));
    int sunY = static_cast<int>(H * 0.17 + std::cos(t * 0.11) * (H * 0.03));
    int sunR = std::max(12, H / 16);
    disc(sunX, sunY, sunR + 6, sunGlow);
    disc(sunX, sunY, sunR, sunCore);
    if (drawMoon) {
      disc(sunX + sunR / 3, sunY - sunR / 4, std::max(5, sunR / 2), skyTop);
      disc(sunX - sunR / 3, sunY + sunR / 5, std::max(2, sunR / 8), SDL_Color {210, 216, 226, 200});
      disc(sunX, sunY - sunR / 6, std::max(2, sunR / 10), SDL_Color {190, 202, 214, 200});
    }

    auto drawCloud = [&](int x, int y, int scale) {
      rect(x + scale, y + scale * 2, scale * 9, scale * 2, cloudShadow);
      disc(x + scale * 2, y + scale * 2, scale * 2, cloudMain);
      disc(x + scale * 5, y + scale * 2, scale * 3, cloudMain);
      disc(x + scale * 8, y + scale * 2, scale * 2, cloudMain);
      rect(x + scale * 2, y + scale * 2, scale * 6, scale * 2, cloudMain);
    };

    int cloudCount = scene == 3 ? 6 : (scene >= 2 ? 3 : 4);
    for (int i = 0; i < cloudCount; ++i) {
      int cx = ((i * 280) - static_cast<int>(t * (16.0 + i * 3.0))) % (W + 260) - 180;
      int cy = H / 10 + i * (H / 18);
      drawCloud(cx, cy, std::max(2, H / 120));
    }

    for (int y = oceanTop; y < beachTop; ++y) {
      double v = beachTop > oceanTop + 1
        ? static_cast<double>(y - oceanTop) / static_cast<double>(beachTop - oceanTop - 1)
        : 0.0;
      rect(0, y, W, 1, lerpColor(oceanFar, oceanNear, v));
    }

    for (int x = -8; x < W + 8; x += 6) {
      double waveA = std::sin(static_cast<double>(x) * 0.035 + t * 1.8);
      double waveB = std::sin(static_cast<double>(x) * 0.018 + t * 1.1 + 1.8);
      int y = oceanTop + 14 + static_cast<int>((waveA + waveB) * 4.0);
      rect(x, y, 4, 2, SDL_Color {184, 244, 255, 190});
    }

    int islandCenter = W / 2 + static_cast<int>(std::sin(t * 0.09) * (W * 0.03));
    int islandHalfW = std::max(40, W / 5);
    int islandBaseY = beachTop - 4;
    SDL_Color islandDark {36, 92, 64, 255};
    SDL_Color islandLight {62, 135, 88, 255};
    for (int dx = -islandHalfW; dx <= islandHalfW; ++dx) {
      double u = std::abs(static_cast<double>(dx)) / static_cast<double>(islandHalfW);
      int height = std::max(2, static_cast<int>((1.0 - u * u) * (H * 0.11)));
      rect(islandCenter + dx, islandBaseY - height, 1, height, islandDark);
      if (height > 5 && dx % 3 == 0) {
        rect(islandCenter + dx, islandBaseY - height, 1, 2, islandLight);
      }
    }

    for (int y = beachTop; y < H; ++y) {
      double v = H > beachTop + 1
        ? static_cast<double>(y - beachTop) / static_cast<double>(H - beachTop - 1)
        : 0.0;
      SDL_Color sand = lerpColor(sandTop, sandBottom, v);
      rect(0, y, W, 1, sand);
    }
    for (int x = 0; x < W; x += 5) {
      int y = beachTop + 2 + static_cast<int>(std::sin(static_cast<double>(x) * 0.09 + t * 0.8) * 2.0);
      rect(x, y, 3, 1, SDL_Color {255, 240, 186, 170});
    }

    auto drawPalm = [&](int baseX, int baseY, int trunkH, double sway, bool backLayer) {
      SDL_Color trunkA = backLayer ? SDL_Color{96, 72, 44, 255} : SDL_Color{116, 84, 52, 255};
      SDL_Color trunkB = backLayer ? SDL_Color{130, 95, 58, 255} : SDL_Color{148, 108, 65, 255};
      SDL_Color leafA = backLayer ? SDL_Color{38, 118, 70, 255} : SDL_Color{46, 146, 82, 255};
      SDL_Color leafB = backLayer ? SDL_Color{64, 154, 90, 255} : SDL_Color{82, 176, 108, 255};

      int x = baseX;
      for (int i = 0; i < trunkH; ++i) {
        double bend = std::sin(static_cast<double>(i) * 0.18 + sway) * 0.7;
        x = baseX + static_cast<int>(bend * (1.0 + static_cast<double>(i) / static_cast<double>(trunkH)));
        rect(x - 1, baseY - i, 3, 1, (i % 3 == 0) ? trunkB : trunkA);
      }

      int crownX = x;
      int crownY = baseY - trunkH;
      for (int frond = 0; frond < 6; ++frond) {
        double angle = (-1.25 + frond * 0.48) + std::sin(t * 0.9 + frond) * 0.08;
        int len = std::max(12, H / 14) + (frond % 2 == 0 ? 2 : -1);
        for (int step = 0; step < len; ++step) {
          double s = static_cast<double>(step) / static_cast<double>(len);
          int fx = crownX + static_cast<int>(std::cos(angle) * step);
          int fy = crownY + static_cast<int>(std::sin(angle) * step + s * s * 3.0);
          rect(fx, fy, 2, 1, (step % 2 == 0) ? leafA : leafB);
        }
      }
    };

    drawPalm(W / 2 - W / 7, beachTop + 3, std::max(26, H / 7), t * 0.8 + 0.6, true);
    drawPalm(W / 2 + W / 8, beachTop + 3, std::max(24, H / 8), t * 0.85 + 1.8, true);
    drawPalm(W / 4, H - std::max(16, H / 8), std::max(28, H / 6), t * 0.9 + 0.2, false);
    drawPalm(W * 3 / 4, H - std::max(18, H / 8), std::max(30, H / 6), t * 0.95 + 2.1, false);

    int blockSize = std::max(5, H / 34);
    auto drawBrickPlatform = [&](int x, int y, int blocks, bool glowing) {
      SDL_Color a = glowing ? SDL_Color {232, 188, 92, 255} : SDL_Color {176, 108, 66, 255};
      SDL_Color b = glowing ? SDL_Color {255, 228, 136, 255} : SDL_Color {214, 140, 84, 255};
      SDL_Color stroke = glowing ? SDL_Color {132, 82, 42, 255} : SDL_Color {104, 62, 36, 255};
      for (int i = 0; i < blocks; ++i) {
        int bx = x + i * blockSize;
        rect(bx, y, blockSize - 1, blockSize - 1, ((i + static_cast<int>(t * 2.0)) & 1) ? a : b);
        rect(bx, y + blockSize / 2, blockSize - 1, 1, stroke);
        rect(bx + blockSize / 2, y, 1, blockSize - 1, stroke);
      }
    };

    auto drawPipe = [&](int x, int baseY, int height, bool enemyPipe) {
      int pipeW = std::max(16, blockSize * 3);
      SDL_Color body = enemyPipe ? SDL_Color {80, 188, 98, 255} : SDL_Color {70, 168, 208, 255};
      SDL_Color lip = enemyPipe ? SDL_Color {122, 236, 128, 255} : SDL_Color {118, 218, 252, 255};
      SDL_Color dark = enemyPipe ? SDL_Color {38, 110, 56, 255} : SDL_Color {30, 108, 142, 255};
      rect(x, baseY - height, pipeW, height, body);
      rect(x + pipeW / 2 - 1, baseY - height, 2, height, lip);
      rect(x - 3, baseY - height - 4, pipeW + 6, 5, lip);
      rect(x + 1, baseY - height - 2, pipeW - 2, 1, dark);
    };

    int platformY = beachTop - std::max(20, H / 9);
    drawBrickPlatform(W / 8, platformY, 6, scene == 1);
    drawBrickPlatform(W / 2 + W / 16, platformY - blockSize * 2, 5, scene == 1);
    drawPipe(W / 3, beachTop + std::max(8, H / 40), std::max(18, H / 11), true);
    drawPipe(W * 3 / 5, beachTop + std::max(9, H / 38), std::max(15, H / 12), false);

    auto drawCrab = [&](int x, int y, bool clawsUp) {
      SDL_Color shellA {214, 78, 68, 255};
      SDL_Color shellB {242, 118, 98, 255};
      disc(x, y, std::max(4, H / 55), shellA);
      rect(x - 4, y - 1, 8, 3, shellB);
      rect(x - 6, y + 2, 2, 2, shellA);
      rect(x + 4, y + 2, 2, 2, shellA);
      rect(x - 5, y + 4, 2, 1, SDL_Color {84, 42, 30, 255});
      rect(x + 3, y + 4, 2, 1, SDL_Color {84, 42, 30, 255});
      if (clawsUp) {
        rect(x - 8, y - 5, 2, 4, shellA);
        rect(x + 6, y - 5, 2, 4, shellA);
      } else {
        rect(x - 9, y - 2, 3, 2, shellA);
        rect(x + 6, y - 2, 3, 2, shellA);
      }
      rect(x - 2, y - 5, 1, 2, SDL_Color {255, 255, 255, 255});
      rect(x + 1, y - 5, 1, 2, SDL_Color {255, 255, 255, 255});
      rect(x - 2, y - 4, 1, 1, SDL_Color {0, 0, 0, 255});
      rect(x + 1, y - 4, 1, 1, SDL_Color {0, 0, 0, 255});
    };

    auto drawFish = [&](int x, int y, bool facingRight, SDL_Color body) {
      auto toneDown = [](Uint8 v) -> Uint8 {
        return static_cast<Uint8>(std::max(0, static_cast<int>(v) - 30));
      };
      SDL_Color fin {toneDown(body.r), toneDown(body.g), toneDown(body.b), 255};
      rect(x - 4, y - 2, 8, 4, body);
      rect(x - 2, y - 3, 4, 1, body);
      rect(x - 1, y + 2, 2, 1, body);
      if (facingRight) {
        rect(x - 6, y - 1, 2, 2, fin);
        rect(x + 3, y - 1, 2, 2, fin);
        rect(x + 2, y - 1, 1, 1, SDL_Color {255, 255, 255, 255});
      } else {
        rect(x + 4, y - 1, 2, 2, fin);
        rect(x - 5, y - 1, 2, 2, fin);
        rect(x - 3, y - 1, 1, 1, SDL_Color {255, 255, 255, 255});
      }
    };

    auto drawParrot = [&](int x, int y, bool wingUp) {
      SDL_Color body {70, 214, 120, 255};
      SDL_Color beak {246, 182, 78, 255};
      rect(x - 4, y - 3, 8, 6, body);
      rect(x + 3, y - 1, 3, 2, beak);
      rect(x - 2, y - 1, 1, 1, SDL_Color {0, 0, 0, 255});
      if (wingUp) {
        rect(x - 7, y - 6, 3, 4, SDL_Color {52, 166, 98, 255});
        rect(x + 1, y - 6, 3, 4, SDL_Color {52, 166, 98, 255});
      } else {
        rect(x - 7, y + 0, 3, 4, SDL_Color {52, 166, 98, 255});
        rect(x + 1, y + 0, 3, 4, SDL_Color {52, 166, 98, 255});
      }
      rect(x - 1, y + 3, 1, 2, SDL_Color {170, 102, 54, 255});
      rect(x + 1, y + 3, 1, 2, SDL_Color {170, 102, 54, 255});
    };

    auto drawTurtle = [&](int x, int y, bool stepA) {
      SDL_Color shell {66, 172, 80, 255};
      SDL_Color shellDark {36, 116, 58, 255};
      SDL_Color skin {176, 214, 122, 255};
      rect(x - 7, y - 4, 14, 8, shell);
      rect(x - 5, y - 2, 10, 4, shellDark);
      rect(x + 7, y - 2, 3, 3, skin);
      rect(x + 8, y - 1, 1, 1, SDL_Color {0, 0, 0, 255});
      rect(x - 6, y + 4, 3, 2, skin);
      rect(x + 2, y + 4, 3, 2, skin);
      rect(x - 6 + (stepA ? 0 : 1), y + 6, 3, 1, SDL_Color {88, 72, 46, 255});
      rect(x + 2 + (stepA ? 1 : 0), y + 6, 3, 1, SDL_Color {88, 72, 46, 255});
    };

    auto drawDino = [&](int x, int y, bool blink) {
      SDL_Color body {102, 198, 98, 255};
      SDL_Color belly {186, 236, 154, 255};
      rect(x - 8, y - 8, 16, 10, body);
      rect(x - 4, y - 3, 8, 5, belly);
      rect(x + 6, y - 11, 8, 7, body);
      rect(x + 10, y - 9, 1, 1, blink ? SDL_Color {80, 110, 80, 255} : SDL_Color {0, 0, 0, 255});
      rect(x - 9, y + 2, 4, 4, body);
      rect(x + 1, y + 2, 4, 4, body);
      rect(x - 8, y + 6, 3, 1, SDL_Color {88, 72, 46, 255});
      rect(x + 1, y + 6, 3, 1, SDL_Color {88, 72, 46, 255});
      rect(x - 12, y - 5, 4, 2, body);
    };

    auto drawPuffFriend = [&](int x, int y) {
      disc(x, y, std::max(5, H / 62), SDL_Color {255, 152, 198, 255});
      rect(x - 3, y + 4, 2, 2, SDL_Color {220, 76, 126, 255});
      rect(x + 1, y + 4, 2, 2, SDL_Color {220, 76, 126, 255});
      rect(x - 2, y - 1, 1, 1, SDL_Color {20, 20, 20, 255});
      rect(x + 1, y - 1, 1, 1, SDL_Color {20, 20, 20, 255});
      rect(x - 1, y + 1, 2, 1, SDL_Color {224, 82, 122, 255});
    };

    auto drawCoin = [&](int cx, int cy, int radius) {
      disc(cx, cy, radius, SDL_Color {255, 206, 62, 255});
      disc(cx, cy, std::max(1, radius - 2), SDL_Color {255, 236, 132, 255});
      rect(cx - 1, cy - radius + 2, 2, radius * 2 - 3, SDL_Color {244, 180, 46, 255});
      rect(cx - radius + 2, cy - 1, radius * 2 - 3, 2, SDL_Color {255, 248, 188, 255});
    };

    for (int i = 0; i < 5; ++i) {
      int cx = ((i * (W / 4) + static_cast<int>(t * 34.0)) % (W + 60)) - 30;
      int cy = beachTop - 18 + static_cast<int>(std::sin(t * 2.4 + i * 1.3) * 6.0);
      drawCoin(cx, cy, std::max(4, H / 48));
    }

    int heroX = static_cast<int>(std::fmod(t * 26.0, static_cast<double>(W + 24))) - 12;
    int heroY = beachTop - std::max(16, H / 12);
    int step = (static_cast<int>(t * 8.0) & 1);
    SDL_Color skin {255, 224, 189, 255};
    SDL_Color hat {212, 62, 68, 255};
    SDL_Color shirt {46, 124, 222, 255};
    SDL_Color shorts {34, 78, 138, 255};
    SDL_Color boots {88, 60, 34, 255};
    rect(heroX + 3, heroY + 0, 6, 2, hat);
    rect(heroX + 2, heroY + 2, 8, 2, hat);
    rect(heroX + 3, heroY + 4, 6, 3, skin);
    rect(heroX + 2, heroY + 7, 8, 4, shirt);
    rect(heroX + 3, heroY + 11, 6, 3, shorts);
    rect(heroX + 1, heroY + 8, 2, 4, skin);
    rect(heroX + 9, heroY + 8, 2, 4, skin);
    rect(heroX + 3, heroY + 14, 2, 3, shorts);
    rect(heroX + 7, heroY + 14, 2, 3, shorts);
    rect(heroX + 2 + step, heroY + 17, 3, 2, boots);
    rect(heroX + 6 - step, heroY + 17, 3, 2, boots);

    int crabX = (static_cast<int>(t * 24.0) % (W + 80)) - 40;
    drawCrab(crabX, beachTop + std::max(8, H / 40), (static_cast<int>(t * 4.0) & 1) != 0);

    int turtleX = W - ((static_cast<int>(t * 18.0) + 20) % (W + 90)) + 24;
    drawTurtle(turtleX, beachTop + std::max(5, H / 52), (static_cast<int>(t * 6.0) & 1) != 0);

    int dinoX = ((static_cast<int>(t * 11.0) + W / 3) % (W + 120)) - 60;
    drawDino(dinoX, beachTop - std::max(18, H / 11), (static_cast<int>(t * 2.4) % 5) == 0);

    int puffX = heroX + 28 + static_cast<int>(std::sin(t * 1.7) * 10.0);
    int puffY = heroY + 6 + static_cast<int>(std::fabs(std::sin(t * 3.4)) * 4.0);
    drawPuffFriend(puffX, puffY);

    int parrotX = W - ((static_cast<int>(t * 34.0) + 60) % (W + 120));
    int parrotY = std::max(12, H / 9) + static_cast<int>(std::sin(t * 2.1) * (H / 28.0));
    drawParrot(parrotX, parrotY, (static_cast<int>(t * 8.0) & 1) == 0);

    for (int i = 0; i < 4; ++i) {
      int fishX = ((i * (W / 4) + static_cast<int>(t * 28.0)) % (W + 80)) - 40;
      double jump = std::sin(t * 2.5 + static_cast<double>(i) * 1.2);
      if (jump > -0.2) {
        int fishY = oceanTop + 20 - static_cast<int>(std::max(0.0, jump) * 16.0);
        SDL_Color fishColor = (i % 2 == 0) ? SDL_Color {255, 178, 88, 255} : SDL_Color {96, 230, 220, 255};
        drawFish(fishX, fishY, (i % 2) == 0, fishColor);
      }
    }

    if (scene == 3) {
      for (int i = 0; i < W; i += 14) {
        int rx = (i + static_cast<int>(t * 220.0)) % std::max(1, W);
        int ry = oceanTop / 2 + (i % 24);
        rect(rx, ry, 1, std::max(8, H / 28), SDL_Color {178, 222, 244, 140});
      }
      if (std::sin(t * 3.2) > 0.93) {
        int boltX = W / 3 + static_cast<int>(std::sin(t * 4.4) * (W / 10.0));
        rect(boltX, 0, 3, oceanTop + 24, SDL_Color {242, 248, 255, 170});
        rect(boltX + 3, oceanTop / 3, 2, oceanTop / 3, SDL_Color {242, 248, 255, 140});
      }
    }

    int stripH = std::max(16, H / 14);
    int stripY = H - stripH;
    rect(0, stripY, W, stripH, SDL_Color {12, 30, 56, 230});

    const std::array<SDL_Color, 8> bars {{
      SDL_Color{232, 78, 72, 255},
      SDL_Color{246, 160, 70, 255},
      SDL_Color{252, 226, 96, 255},
      SDL_Color{104, 202, 108, 255},
      SDL_Color{78, 198, 212, 255},
      SDL_Color{70, 144, 244, 255},
      SDL_Color{152, 116, 232, 255},
      SDL_Color{244, 244, 244, 255},
    }};
    int barW = std::max(10, (W - 24) / static_cast<int>(bars.size()));
    for (int i = 0; i < static_cast<int>(bars.size()); ++i) {
      rect(8 + i * barW, stripY + 4, barW - 2, stripH - 8, bars[i]);
      int pulseY = stripY + stripH / 2 + static_cast<int>(std::sin(t * 3.0 + i * 0.7) * (stripH / 4));
      rect(8 + i * barW + 2, pulseY, barW - 6, 1, SDL_Color {255, 255, 255, 255});
    }
  }

  static std::optional<DecodedFrame> buildPatternFrame(const Cue& cue, double animTime = 0.0,
                                                       int fallbackWidth = kOutputWidth,
                                                       int fallbackHeight = kOutputHeight) {
    int sourceW = cue.width > 0 ? cue.width : fallbackWidth;
    int sourceH = cue.height > 0 ? cue.height : fallbackHeight;
    bool legacyRaster = cue.width == kOutputWidth && cue.height == kOutputHeight;
    if (legacyRaster && (fallbackWidth != kOutputWidth || fallbackHeight != kOutputHeight)) {
      sourceW = fallbackWidth;
      sourceH = fallbackHeight;
    }

    DecodedFrame frame;
    frame.width  = std::max(320, sourceW);
    frame.height = std::max(180, sourceH);
    frame.index  = 0;
    frame.pixels.assign(static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4u, 255);

    std::string patternType = normalizePatternTypeId(cue.path);
    std::string basePatternType = stripPatternMotionSuffix(patternType);
    bool motion = endsWith(patternType, "-motion");

    auto pulseByte = [&](Uint8 fullScale, double speed, double minScale = 0.40, double maxScale = 1.0) -> Uint8 {
      double wave = 0.5 + 0.5 * std::sin(animTime * speed);
      double scaled = minScale + (maxScale - minScale) * wave;
      return static_cast<Uint8>(std::clamp(std::lround(static_cast<double>(fullScale) * scaled), 0l, 255l));
    };

    if (basePatternType == "smpte-bars") {
      buildSmpte75Bars(frame);
      if (motion) {
        int scanX = static_cast<int>(std::fmod(animTime * 230.0, static_cast<double>(frame.width + 120))) - 60;
        fillPixelRect(frame, scanX, 0, 4, frame.height, {255, 255, 255, 255});
        int scanY = static_cast<int>(std::fmod(animTime * 140.0, static_cast<double>(frame.height + 80))) - 40;
        fillPixelRect(frame, 0, scanY, frame.width, 2, {8, 8, 8, 255});
      }
    } else if (basePatternType == "crosshatch") {
      int phase = motion ? static_cast<int>(std::fmod(animTime * 92.0, 64.0)) : 0;
      buildCrosshatch(frame, phase, phase / 2);
      if (motion) {
        int markerX = static_cast<int>(std::fmod(animTime * 170.0, static_cast<double>(frame.width + 40))) - 20;
        fillPixelRect(frame, markerX, frame.height / 2 - 4, 10, 8, {245, 220, 80, 255});
      }
    } else if (basePatternType == "checkerboard" || basePatternType == "checker") {
      int phase = motion ? static_cast<int>(std::fmod(animTime * 110.0, 64.0)) : 0;
      buildCheckerboard(frame, phase, phase / 2);
      if (motion) {
        int y = static_cast<int>(frame.height * (0.5 + 0.35 * std::sin(animTime * 1.9)));
        fillPixelRect(frame, 0, y, frame.width, 2, {255, 96, 32, 255});
      }
    } else if (basePatternType == "full-white") {
      Uint8 v = motion ? pulseByte(255, 2.5, 0.55, 1.0) : 255;
      fillPixelRect(frame, 0, 0, frame.width, frame.height, {v, v, v, 255});
    } else if (basePatternType == "full-black") {
      Uint8 v = motion ? pulseByte(80, 2.3, 0.05, 1.0) : 0;
      fillPixelRect(frame, 0, 0, frame.width, frame.height, {v, v, v, 255});
    } else if (basePatternType == "full-red") {
      Uint8 r = motion ? pulseByte(255, 2.6, 0.30, 1.0) : 255;
      fillPixelRect(frame, 0, 0, frame.width, frame.height, {r, 0, 0, 255});
    } else if (basePatternType == "full-green") {
      Uint8 g = motion ? pulseByte(255, 2.7, 0.30, 1.0) : 255;
      fillPixelRect(frame, 0, 0, frame.width, frame.height, {0, g, 0, 255});
    } else if (basePatternType == "full-blue") {
      Uint8 b = motion ? pulseByte(255, 2.8, 0.30, 1.0) : 255;
      fillPixelRect(frame, 0, 0, frame.width, frame.height, {0, 0, b, 255});
    } else if (basePatternType == "pocket-day") {
      buildPocketTest(frame, animTime, 0);
    } else if (basePatternType == "pocket-sunset") {
      buildPocketTest(frame, animTime, 1);
    } else if (basePatternType == "pocket-night") {
      buildPocketTest(frame, animTime, 2);
    } else if (basePatternType == "pocket-storm") {
      buildPocketTest(frame, animTime, 3);
    } else {
      // Default / "pocket-test" / "kawaii-pocket": animated full-colour scene cycle.
      buildPocketTest(frame, animTime, -1);
    }

    return frame;
  }

  std::optional<DecodedFrame> decodeSingleFrame(ChildProcess& process, const std::string& path, int width, int height, double seconds) {
    auto [capW, capH] = currentOutputSizeHint();
    int w = width > 0 ? width : capW;
    int h = height > 0 ? height : capH;
    // Cap to output dimensions — decoding huge media at native resolution can block UI and GPU
    // for no visible benefit.
    if (w > capW || h > capH) {
      double scale = std::min(
        static_cast<double>(capW) / w,
        static_cast<double>(capH) / h
      );
      w = std::max(1, static_cast<int>(w * scale));
      h = std::max(1, static_cast<int>(h * scale));
    }
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
      "-vf",
      "scale=" + std::to_string(w) + ":" + std::to_string(h) + ":flags=neighbor",
      "-f",
      "rawvideo",
      "-pix_fmt",
      "rgba",
      "pipe:1"
    });

    if (!spawnPipeProcess(process, args)) {
      return std::nullopt;
    }

    const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    DecodedFrame frame;
    frame.width = w;
    frame.height = h;
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
    stopImageThread();
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
    isSourceCapturing_ = false;
    isBrowserCapturing_ = false;
  }

  bool buildSourceCaptureArgs(const Cue& cue, int w, int h, std::vector<std::string>& args) const {
#ifdef _WIN32
    (void) cue;
    (void) w;
    (void) h;
    (void) args;
    return false;
#else
    std::string sourceRef = sourceCueRefFromCue(cue);
    if (sourceRef.empty()) {
      sourceRef = defaultSourceRefForKind(cue.kind);
    }
    sourceRef = trim(sourceRef);
    std::string sourceRefLower = toLower(sourceRef);

    auto cameraDeviceForRef = [&](const std::string& refLower, const std::string& rawRef) -> std::string {
      if (refLower.empty() || refLower == "default-camera" || refLower == "default") {
        return "/dev/video0";
      }
      if (refLower.rfind("v4l2:", 0) == 0 && rawRef.size() > 5) {
        return trim(rawRef.substr(5));
      }
      if (refLower.rfind("/dev/video", 0) == 0) {
        return rawRef;
      }
      bool numeric = !rawRef.empty() &&
        std::all_of(rawRef.begin(), rawRef.end(), [](unsigned char ch) { return std::isdigit(ch); });
      if (numeric) {
        return "/dev/video" + rawRef;
      }
      return rawRef;
    };

    if (cue.kind == CueKind::Camera) {
      std::string device = cameraDeviceForRef(sourceRefLower, sourceRef);
      if (device.empty()) {
        device = "/dev/video0";
      }
      args = {
        "ffmpeg",
        "-hide_banner",
        "-loglevel", "error",
        "-f", "v4l2",
        "-thread_queue_size", "64",
        "-framerate", "30",
        "-i", device,
        "-vf", "scale=" + std::to_string(w) + ":" + std::to_string(h) + ":flags=neighbor",
        "-f", "rawvideo",
        "-pix_fmt", "rgba",
        "pipe:1"
      };
      return true;
    }

    std::string displayEnv = ":0.0";
    if (const char* envDisplay = std::getenv("DISPLAY"); envDisplay && *envDisplay) {
      std::string trimmed = trim(envDisplay);
      if (!trimmed.empty()) {
        displayEnv = trimmed;
      }
    }

    std::string inputSpec = displayEnv + "+0,0";
    bool useWindowId = false;
    std::string windowId;
    if (sourceRefLower.rfind("x11:", 0) == 0 && sourceRef.size() > 4) {
      inputSpec = trim(sourceRef.substr(4));
      if (inputSpec.empty()) {
        inputSpec = displayEnv + "+0,0";
      }
    } else if (sourceRefLower.rfind("id:", 0) == 0 && sourceRef.size() > 3) {
      useWindowId = true;
      windowId = trim(sourceRef.substr(3));
      inputSpec = displayEnv;
    } else if (sourceRefLower.rfind("window_id:", 0) == 0 && sourceRef.size() > 10) {
      useWindowId = true;
      windowId = trim(sourceRef.substr(10));
      inputSpec = displayEnv;
    } else if (!sourceRef.empty() && sourceRef[0] == ':') {
      inputSpec = sourceRef;
      if (inputSpec.find('+') == std::string::npos) {
        inputSpec += "+0,0";
      }
    } else if (!sourceRef.empty() && sourceRef[0] == '+') {
      inputSpec = displayEnv + sourceRef;
    } else if (sourceRefLower == "active-window" || sourceRefLower == "default-window"
               || sourceRefLower == "screen" || sourceRefLower == "desktop"
               || sourceRefLower == "default-bus" || sourceRefLower == "default-source"
               || sourceRefLower.empty()) {
      inputSpec = displayEnv + "+0,0";
    }

    args = {
      "ffmpeg",
      "-hide_banner",
      "-loglevel", "error",
      "-f", "x11grab",
      "-framerate", "30",
      "-draw_mouse", "1"
    };
    if (useWindowId && !windowId.empty()) {
      args.push_back("-window_id");
      args.push_back(windowId);
      args.push_back("-i");
      args.push_back(inputSpec);
    } else {
      args.push_back("-video_size");
      args.push_back(std::to_string(w) + "x" + std::to_string(h));
      args.push_back("-i");
      args.push_back(inputSpec);
    }
    args.push_back("-vf");
    args.push_back("scale=" + std::to_string(w) + ":" + std::to_string(h) + ":flags=neighbor");
    args.push_back("-f");
    args.push_back("rawvideo");
    args.push_back("-pix_fmt");
    args.push_back("rgba");
    args.push_back("pipe:1");
    return true;
#endif
  }

  void startDecoderThreads(const Cue& cue, double mediaStartSeconds, double cueStartSeconds) {
    if (!spawnPipeProcess(videoProcess_, {
      "ffmpeg",
      "-hide_banner",
      "-loglevel",
      "error",
      "-ss",
      std::to_string(mediaStartSeconds),
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
    if (frameBytes == 0) {
      videoProcess_.stop();
      decoderEof_ = true;  // prevent engine stalling waiting for frames that can never arrive
      return;
    }
    // Capture fd by value to avoid a data race: the main thread sets
    // videoProcess_.readFd = -1 in stop() while this thread reads it.
    int videoFd = videoProcess_.readFd;
    videoThread_ = std::thread([this, cue, frameBytes, cueStartSeconds, videoFd]() {
      std::uint64_t frameIndex = static_cast<std::uint64_t>(std::floor(cueStartSeconds * frameRate_));
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

        if (!readExact(videoFd, frame.pixels.data(), frameBytes)) {
          decoderEof_ = true;
          break;
        }

        std::lock_guard<std::mutex> lock(frameMutex_);
        frameQueue_.push_back(std::move(frame));
      }
      decoderEof_ = true;
    });

    if (cue.hasAudio && cue.audioEnabled) {
      if (spawnPipeProcess(audioProcess_, {
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-ss",
        std::to_string(mediaStartSeconds),
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
        // Capture fd by value to avoid a data race: the main thread sets
        // audioProcess_.readFd = -1 in stop() while this thread reads it.
        int audioFd = audioProcess_.readFd;
        audioThread_ = std::thread([this, cueStartSeconds, audioFd]() {
          std::vector<std::uint8_t> buffer(8192);
          double audioTime = cueStartSeconds;
          while (!decoderStop_.load()) {
            if (SDL_GetQueuedAudioSize(audioDevice_) > 23040) {  // ~120ms at 48kHz stereo s16
              SDL_Delay(4);
              continue;
            }

            ssize_t bytesRead = read(audioFd, buffer.data(), buffer.size());
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
            if (audioTap_) {
              audioTap_(scaled);
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
  SDL_Texture* transitionTexture_ = nullptr;
  int transitionTextureWidth_ = 0;
  int transitionTextureHeight_ = 0;
  bool transitionActive_ = false;
  bool transitionWaitingForFirstFrame_ = false;
  double transitionDurationSeconds_ = 0.0;
  TransitionStyle transitionStyle_ = TransitionStyle::Cut;
  float transitionSourceGain_ = 1.0f;  // fade gain of outgoing frame at transition start
  float outputScaleX_ = 1.0f;          // per-cue output X scale (applied in drawTextureFitted)
  float outputScaleY_ = 1.0f;          // per-cue output Y scale (applied in drawTextureFitted)
  ScaleMode scaleMode_ = ScaleMode::Fit;  // Fit/Fill/Stretch/Unscaled mode
  float outputOffsetX_ = 0.0f;         // per-cue output X offset (pixels)
  float outputOffsetY_ = 0.0f;         // per-cue output Y offset (pixels)
  float outputRotationDegrees_ = 0.0f; // per-cue rotation angle
  float cropLeft_ = 0.0f;              // fractional crop 0..1 from left
  float cropRight_ = 0.0f;             // fractional crop 0..1 from right
  float cropTop_ = 0.0f;               // fractional crop 0..1 from top
  float cropBottom_ = 0.0f;            // fractional crop 0..1 from bottom
  bool chromaKeyEnabled_ = false;
  SDL_Color chromaKeyColor_ {0, 255, 0, 255};
  float chromaKeyTolerance_ = 60.0f;   // RGB distance threshold (0..441)
  float chromaKeySoftness_ = 20.0f;    // feather width around threshold
  float brightness_ = 1.0f;             // per-cue brightness (0..2)
  float contrast_ = 1.0f;               // per-cue contrast (0..2)
  float saturation_ = 1.0f;             // per-cue saturation (0..2)
  float hueShift_ = 0.0f;               // per-cue hue shift (-180..180)
  std::vector<std::uint8_t> keyedPixelsScratch_;
  std::vector<double> pausePoints_;    // sorted pause point positions for active cue
  size_t nextPausePointIdx_ = 0;       // index of next unpassed pause point
  std::chrono::steady_clock::time_point transitionStartedAt_ = std::chrono::steady_clock::now();
  std::atomic<float> volume_ {1.0f};
  TransportState state_ = TransportState::Stopped;
  double currentPosition_ = 0.0;
  double pausedPosition_ = 0.0;
  double playbackStartPosition_ = 0.0;
  double duration_ = 0.0;
  double cueInPointSeconds_ = 0.0;
  double cueOutPointSeconds_ = 0.0;
  double frameRate_ = 30.0;
  std::chrono::steady_clock::time_point playbackClockStart_ = std::chrono::steady_clock::now();
  std::optional<DecodedFrame> displayFrame_;
  std::uint64_t lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
  std::mutex frameMutex_;
  std::deque<DecodedFrame> frameQueue_;
  ChildProcess videoProcess_;
  ChildProcess audioProcess_;
  ChildProcess imageProcess_;
  std::thread videoThread_;
  std::thread audioThread_;
  std::thread imageThread_;
  std::mutex imageMutex_;
  std::optional<DecodedFrame> pendingImageFrame_;
  std::atomic<bool> imageFramePending_ {false};
  AudioTapCallback audioTap_;
  std::atomic<bool> decoderStop_ {false};
  std::atomic<bool> decoderEof_ {false};
  bool reachedEnd_ = false;
  bool isBrowserCapturing_ = false;
  int browserCaptureW_ = 1280;
  int browserCaptureH_ = 720;
  bool isSourceCapturing_ = false;
};

// Offline waveform analysis: runs ffmpeg to extract mono PCM and compute per-bucket peaks.
// Returns empty vector on failure or if the file has no audio.
static std::vector<float> computeWaveformPeaks(const std::string& path, int numBuckets = 512) {
  std::string cmd = "ffmpeg -i \"" + path + "\" -ac 1 -ar 4000 -f s16le -vn -loglevel quiet pipe:1";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return {};
  std::vector<int16_t> samples;
  constexpr size_t kChunk = 4096;
  int16_t buf[kChunk];
  size_t n;
  while ((n = fread(buf, sizeof(int16_t), kChunk, pipe)) > 0) {
    samples.insert(samples.end(), buf, buf + n);
    if (samples.size() > 4000u * 600u) break; // cap at 10 min
  }
  pclose(pipe);
  if (samples.empty()) return {};
  std::vector<float> peaks(numBuckets, 0.0f);
  size_t perBucket = std::max<size_t>(1, samples.size() / numBuckets);
  for (int b = 0; b < numBuckets; ++b) {
    size_t start = b * perBucket;
    size_t end   = std::min(start + perBucket, samples.size());
    float mx = 0.0f;
    for (size_t i = start; i < end; ++i)
      mx = std::max(mx, std::abs(samples[i]) / 32768.0f);
    peaks[b] = mx;
  }
  return peaks;
}

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
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    controlWindow_ = SDL_CreateWindow(
      kAppTitle.data(),
      SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED,
      kControlWidth,
      kControlHeight,
      0
    );
    if (!controlWindow_) {
      std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    SDL_SetWindowMinimumSize(controlWindow_, 1320, 780);

    controlRenderer_ = SDL_CreateRenderer(controlWindow_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!controlRenderer_) {
      std::cerr << "Renderer creation failed: " << SDL_GetError() << '\n';
      return false;
    }

    // Create optional Decks Panel window
    decksPanelWindow_ = SDL_CreateWindow(
      "Deckboy Decks",
      SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED,
      1760,
      1020,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (decksPanelWindow_) {
      SDL_SetWindowMinimumSize(decksPanelWindow_, 1400, 780);
      decksPanelRenderer_ = SDL_CreateRenderer(decksPanelWindow_, -1, SDL_RENDERER_ACCELERATED);
      if (!decksPanelRenderer_) {
        SDL_DestroyWindow(decksPanelWindow_);
        decksPanelWindow_ = nullptr;
      }
    }

    fontLarge_ = TTF_OpenFont(Paths::fontPath(Paths::FontName::Sans).string().c_str(), 32);
    fontBase_ = TTF_OpenFont(Paths::fontPath(Paths::FontName::Sans).string().c_str(), 21);
    fontSmall_ = TTF_OpenFont(Paths::fontPath(Paths::FontName::Sans).string().c_str(), 17);
    fontMono_ = TTF_OpenFont(Paths::fontPath(Paths::FontName::Mono).string().c_str(), 18);
    fontPixel_ = TTF_OpenFont(Paths::fontPath(Paths::FontName::Pixel).string().c_str(), 24);
    if (!fontLarge_ || !fontBase_ || !fontSmall_ || !fontMono_) {
      std::cerr << "Font load failed: " << TTF_GetError() << '\n';
      return false;
    }

    Paths::ensureDataDir();
    currentProjectFile_ = defaultProjectFile();
    project_ = loadProject(currentProjectFile_);
    normalizeProject(project_);
    disarmAllOutputsForStartup();
    updateDecksPanelVisibility();
    // Output starts black — no cue is active until the operator takes one
    for (auto& deck : project_.decks) { deck.activeIndex = -1; }
    // Show startup dialog so operator can choose to load or start fresh
    showStartupDialog_ = true;
    showSplashOverlay_ = true;
    splashStartedAt_ = SDL_GetTicks64();
    ensureUiAudioDevice();
    if (!rebuildDeckRuntimes()) {
      std::cerr << "Deck runtime creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    if (!rebuildOutputRuntimes()) {
      std::cerr << "Output runtime creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    observedDisplayCount_ = SDL_GetNumVideoDisplays();
    refreshDisplayTopology(false);
    selectionChangedAt_ = SDL_GetTicks64();
    lastUpdateTickMs_ = selectionChangedAt_;
    startCompanionControl();
#ifndef _WIN32
    if (project_.oscQueryEnabled) {
      startOscQueryServer();
    }
#endif
    startHyperDeckServer();
    layoutButtons(kControlHeight);
    return true;
  }

  void shutdown() {
    stopHyperDeckServer();
    stopMidiInput();
#ifndef _WIN32
    stopOscQueryServer();
#endif
    stopCompanionControl();
    for (auto& runtime : deckRuntimes_) {
      destroyDeckRuntime(runtime);
    }
    deckRuntimes_.clear();
    for (auto& runtime : outputRuntimes_) {
      destroyOutputRuntime(runtime);
    }
    outputRuntimes_.clear();
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
    if (fontPixel_) {
      TTF_CloseFont(fontPixel_);
      fontPixel_ = nullptr;
    }
    if (thumbnailThread_.joinable()) {
      thumbnailProcess_.stop();
      thumbnailThread_.join();
    }
    if (selectedThumbnailTex_) {
      SDL_DestroyTexture(selectedThumbnailTex_);
      selectedThumbnailTex_ = nullptr;
    }
    if (controlPreviewTex_) {
      SDL_DestroyTexture(controlPreviewTex_);
      controlPreviewTex_ = nullptr;
    }
    if (decksPanelRenderer_) {
      SDL_DestroyRenderer(decksPanelRenderer_);
      decksPanelRenderer_ = nullptr;
    }
    if (decksPanelWindow_) {
      SDL_DestroyWindow(decksPanelWindow_);
      decksPanelWindow_ = nullptr;
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
      auto frameStart = std::chrono::steady_clock::now();
      processEvents();
      drainPickers();
      update();
      render();
      // Prevent CPU spin when vsync isn't gating (hidden window, browser cue, etc.)
      auto frameElapsed = std::chrono::steady_clock::now() - frameStart;
      if (frameElapsed < std::chrono::milliseconds(1)) {
        SDL_Delay(1);
      }
    }
  }

  void drainPickers() {
    if (pendingImport_.valid() &&
        pendingImport_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      auto paths = pendingImport_.get();
      if (!paths.empty()) {
        importPaths(paths);
      }
    }
    if (pendingProjectOpen_.valid() &&
        pendingProjectOpen_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      auto path = pendingProjectOpen_.get();
      if (path) {
        openProjectFromPath(*path);
      }
    }
    if (pendingProjectSaveAs_.valid() &&
        pendingProjectSaveAs_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      auto path = pendingProjectSaveAs_.get();
      if (path) {
        currentProjectFile_ = *path;
        saveProjectNow(true);
      }
    }
  }

  static int runSelfCheck() {
    std::cout << "deckboy-native self-check\n";
    std::cout << "project-root: " << Paths::projectRoot() << '\n';
    std::cout << "font-sans: " << (fs::exists(Paths::fontPath(Paths::FontName::Sans)) ? "ok" : "missing") << '\n';
    std::cout << "font-mono: " << (fs::exists(Paths::fontPath(Paths::FontName::Mono)) ? "ok" : "missing") << '\n';
    std::cout << "font-pixel: " << (fs::exists(Paths::fontPath(Paths::FontName::Pixel)) ? "ok" : "missing") << '\n';
    std::cout << "ffmpeg: " << (readAllText({"ffmpeg", "-version"}).has_value() ? "ok" : "missing") << '\n';
    std::cout << "ffprobe: " << (readAllText({"ffprobe", "-version"}).has_value() ? "ok" : "missing") << '\n';
#if defined(PLAYBOY_HAS_NDI_SDK)
    std::cout << "ndi-sdk: headers detected (runtime loads when NDI is enabled)\n";
#else
    std::cout << "ndi-sdk: not built (set PLAYBOY_NDI_SDK or install SDK headers)\n";
#endif
    std::cout << "ui-sfx: enabled by separate SDL audio device when available\n";
    std::cout << "companion-control: tcp/udp port 5510 by default (override with PLAYBOY_COMPANION_PORT)\n";

    {
      auto captureCatalog = deckboy::platform::createCaptureBackendCatalog();
      std::ostringstream line;
      line << "capture-backends:";
      for (const auto& info : captureCatalog->list()) {
        line << ' ' << info.id << '[' << (info.supported ? "ok" : "stub") << ']';
      }
      std::cout << line.str() << '\n';
    }

    {
      auto outputCatalog = deckboy::platform::createOutputBackendCatalog();
      std::ostringstream line;
      line << "output-backends:";
      for (const auto& info : outputCatalog->list()) {
        line << ' ' << info.id << '[' << (info.supported ? "ok" : "stub") << ']';
      }
      std::cout << line.str() << '\n';
    }

    return 0;
  }

  static int runSmoke() {
    int failures = 0;
    auto expect = [&](bool condition, const std::string& label) {
      if (condition) {
        std::cout << "[ok] " << label << '\n';
      } else {
        std::cout << "[fail] " << label << '\n';
        failures += 1;
      }
    };

    {
      auto osc = buildOscStringMessage("/take", "3");
      std::string packet(reinterpret_cast<const char*>(osc.data()), osc.size());
      auto parsed = parseOscPacket(packet);
      expect(parsed.size() == 1 && toUpper(parsed[0].address) == "/TAKE", "osc message parse");
      auto mapped = parsed.empty() ? std::optional<std::string> {} : mapOscToRemoteCommand(parsed[0]);
      expect(mapped && *mapped == "TAKE 3", "osc command mapping");
    }

    {
      auto osc = buildOscStringMessage("/cue/audio", "on");
      std::string packet(reinterpret_cast<const char*>(osc.data()), osc.size());
      auto parsed = parseOscPacket(packet);
      auto mapped = parsed.empty() ? std::optional<std::string> {} : mapOscToRemoteCommand(parsed[0]);
      expect(mapped && (*mapped == "CUEAUDIO ON" || *mapped == "CUEAUDIO"), "osc cue audio mapping");
    }

    {
      auto osc = buildOscStringMessage("/deck/opacity", "75");
      std::string packet(reinterpret_cast<const char*>(osc.data()), osc.size());
      auto parsed = parseOscPacket(packet);
      auto mapped = parsed.empty() ? std::optional<std::string> {} : mapOscToRemoteCommand(parsed[0]);
      expect(mapped && *mapped == "DECKOPACITY 75", "osc deck opacity mapping");
    }

    {
      auto one = buildOscStringMessage("/go", "1");
      auto two = buildOscStringMessage("/overlay", "1");
      std::vector<std::uint8_t> bundle;
      bundle.insert(bundle.end(), {'#', 'b', 'u', 'n', 'd', 'l', 'e', '\0'});
      bundle.insert(bundle.end(), 8, 0);
      appendOscU32(bundle, static_cast<std::uint32_t>(one.size()));
      bundle.insert(bundle.end(), one.begin(), one.end());
      appendOscU32(bundle, static_cast<std::uint32_t>(two.size()));
      bundle.insert(bundle.end(), two.begin(), two.end());
      std::string packet(reinterpret_cast<const char*>(bundle.data()), bundle.size());
      auto parsed = parseOscPacket(packet);
      expect(parsed.size() == 2, "osc bundle parse");
    }

    {
      Project project;
      project.outputCanvasEnabled = true;
      project.outputCanvasWidth = 5760;
      project.outputCanvasHeight = 2160;
      project.focusedOutputIndex = 1;
      project.oscQueryEnabled = true;
      project.oscQueryPort = 6410;
      project.oscFeedbackMirrorEnabled = true;
      project.oscFeedbackRateMs = 90;
      project.jumpMode = "load";
      project.jumpTransitionEnabled = false;
      project.panicProfile = "fade_rewind";
      project.panicFadeSeconds = 1.4;
      project.panicAutoRestore = true;
      Deck deck;
      deck.name = "Deck Smoke";
      deck.outputRouteDeckIndex = 0;
      deck.outputLayerIndex = 3;
      deck.transitionSeconds = 1.5;
      deck.transitionStyle = "dip";
      deck.playlistOpacity = 0.62f;
      deck.playlistAutoFade = true;
      deck.playlistFadeSeconds = 1.7;
      deck.playlistTimebaseFps = 29.97;
      deck.playlistStartOffsetSeconds = 3600.0;
      deck.playlistDefaultCueFadeSeconds = 0.75;
      deck.playlistDefaultStillDurationSeconds = 6.5;
      deck.playlistDefaultLoop = true;
      deck.playlistDefaultFadeInEnabled = true;
      deck.playlistDefaultFadeOutEnabled = false;
      deck.playlistDefaultAudioEnabled = false;
      deck.playlistDefaultPauseAtBeginning = true;
      deck.playlistDefaultPauseAtEnd = true;
      deck.playlistDefaultTransitionToNext = false;
      deck.ndiEnabled = true;
      deck.ndiSourceName = "Smoke Fill";
      deck.ndiKeyEnabled = true;
      deck.ndiKeySourceName = "Smoke Key";
      deck.canvasViewX = 320;
      deck.canvasViewY = 40;
      deck.warpEnabled = true;
      deck.warpTopLeftX = -12.0f;
      deck.warpTopLeftY = 8.0f;
      deck.warpTopRightX = 10.0f;
      deck.warpTopRightY = 5.0f;
      deck.warpBottomRightX = 14.0f;
      deck.warpBottomRightY = -6.0f;
      deck.warpBottomLeftX = -9.0f;
      deck.warpBottomLeftY = -7.0f;
      deck.edgeBlendLeft = 0.08f;
      deck.edgeBlendRight = 0.12f;
      deck.edgeBlendTop = 0.03f;
      deck.edgeBlendBottom = 0.05f;
      deck.timecodeChaseEnabled = true;
      deck.timecodeRunEnabled = false;
      deck.timecodeTriggerEnabled = true;
      deck.timecodeJamSyncEnabled = false;
      deck.timecodeFreewheelSeconds = 2.5;
      deck.timecodeFps = 25.0;
      deck.timecodeCurrentSeconds = 12.0;
      Cue cue;
      cue.path = "/tmp/test.mp4";
      cue.name = "Smoke Cue";
      cue.id = "smoke-cue-1";
      cue.cueId = "A1";
      cue.kind = CueKind::Video;
      cue.duration = 20.0;
      cue.width = 1920;
      cue.height = 1080;
      cue.hasAudio = true;
      cue.audioEnabled = false;
      cue.inPointSeconds = 2.0;
      cue.outPointSeconds = 8.0;
      cue.pauseAtBeginning = true;
      cue.pauseOnLastFrame = true;
      cue.transitionToNext = false;
      cue.gotoTarget = "Q12";
      cue.triggerTimecodeSeconds = 13.0;
      cue.cueTransitionSeconds = 1.25;
      cue.cueTransitionStyle = "crossfade";
      cue.outputRotationDegrees = 17.5f;
      cue.cropLeft = 0.10f;
      cue.cropRight = 0.05f;
      cue.cropTop = 0.02f;
      cue.cropBottom = 0.03f;
      cue.chromaKeyEnabled = true;
      cue.chromaKeyColor = SDL_Color {20, 220, 45, 255};
      cue.chromaKeyTolerance = 88.5f;
      cue.chromaKeySoftness = 14.0f;
      cue.brightness = 1.25f;
      cue.contrast = 0.85f;
      cue.saturation = 1.40f;
      cue.hueShift = -22.0f;
      Cue imgCue;
      imgCue.path = "/tmp/test.jpg";
      imgCue.name = "Smoke Still";
      imgCue.id = "smoke-cue-2";
      imgCue.kind = CueKind::Image;
      imgCue.stillDurationSeconds = 5.0;
      Cue ltCue;
      ltCue.path = "graphic://lower-third";
      ltCue.name = "Smoke Lower Third";
      ltCue.kind = CueKind::LowerThird;
      ltCue.lowerThirdText = "Hello World";
      ltCue.lowerThirdSubtext = "subtitle here";
      ltCue.lowerThirdBgAlpha = 200;
      deck.cues.push_back(cue);      // [0]: video — trim/tc/transition tests
      deck.cues.push_back(imgCue);   // [1]: image still — stillDuration test
      deck.cues.push_back(ltCue);    // [2]: lower_third — lowerThird tests
      project.decks = {deck};
      project.outputs = {
        OutputTarget {"Program Out", 0, 1, true, "window", -1, true, "srt", "srt://127.0.0.1:9100?mode=caller", 7200},
        OutputTarget {"Stage Left Stream", 0, 2, true, "stream", 0, true, "rtmp", "rtmp://127.0.0.1/live/stage-left", 4200}
      };
      project.outputs[0].ndiEnabled = true;
      project.outputs[0].ndiSourceName = "Program Fill";
      project.outputs[0].ndiKeyEnabled = true;
      project.outputs[0].ndiKeySourceName = "Program Key";
      project.outputs[0].outputId = "out-smoke-program";
      project.outputs[0].outputAlpha = 0.82f;
      project.outputs[0].outputDelayMs = 240;
      project.outputs[0].outputTimeOverlayEnabled = true;
      project.outputs[0].outputColorSpace = "bt709";
      project.outputs[0].outputLayoutMode = "span";
      project.outputs[0].outputOrientationDegrees = 90;
      project.outputs[0].outputTestCardEnabled = true;
      project.outputs[1].outputId = "out-smoke-stream";
      project.outputs[1].outputAlpha = 0.67f;
      project.outputs[1].outputDelayMs = 120;
      project.outputs[1].outputTimeOverlayEnabled = false;
      project.outputs[1].outputColorSpace = "srgb";
      project.outputs[1].outputLayoutMode = "duplicate";
      project.outputs[1].outputOrientationDegrees = 270;
      project.outputs[1].outputTestCardEnabled = false;
      project.layerAssignments = {
        LayerAssignment {0, 0, 3, true},
        LayerAssignment {0, 1, 9, true}
      };
      project.layerAssignments[0].outputId = project.outputs[0].outputId;
      project.layerAssignments[0].layerId = "lay-smoke-main";
      project.layerAssignments[1].outputId = project.outputs[1].outputId;
      project.layerAssignments[1].layerId = "lay-smoke-stream";
      project.groupPresets = {
        GroupPreset {"Preset Smoke", std::vector<GroupSlot> {
          GroupSlot {false, "smoke-cue-2"}
        }}
      };
      project.focusedGroupPresetIndex = 0;
      project.outputBitDepth = 10;
      normalizeProject(project);

      fs::path smokePath = fs::path("/tmp") / "playboy-smoke.playboy";
      expect(saveProject(smokePath, project), "project save");
      Project loaded = loadProject(smokePath);
      expect(!loaded.decks.empty(), "project load");
      if (!loaded.decks.empty() && !loaded.decks[0].cues.empty()) {
        const Deck& loadedDeck = loaded.decks[0];
        const Cue& loadedCue = loadedDeck.cues[0];
        expect(loaded.outputBitDepth == 10, "output bit depth persisted");
        expect(loaded.outputCanvasEnabled && loaded.outputCanvasWidth == 5760 && loaded.outputCanvasHeight == 2160,
               "output canvas persisted");
        expect(loaded.oscQueryEnabled &&
               loaded.oscQueryPort == 6410 &&
               loaded.oscFeedbackMirrorEnabled &&
               loaded.oscFeedbackRateMs == 90,
               "osc query settings persisted");
        expect(loaded.jumpMode == "load" && !loaded.jumpTransitionEnabled, "jump mode persisted");
        expect(loaded.panicProfile == "fade_rewind", "panic profile persisted");
        expect(std::abs(loaded.panicFadeSeconds - 1.4) < 0.01 && loaded.panicAutoRestore, "panic options persisted");
        expect(loaded.focusedOutputIndex == 1, "focused output persisted");
        expect(loaded.focusedGroupPresetIndex == 0, "focused group preset persisted");
        expect(loaded.outputs.size() == 2 &&
               loaded.outputs[0].name == "Program Out" &&
               loaded.outputs[1].name == "Stage Left Stream" &&
               loaded.outputs[0].hostDeckIndex == 0 &&
               loaded.outputs[1].displayIndex == 2 &&
               loaded.outputs[0].streamEnabled &&
               loaded.outputs[0].streamProtocol == "srt" &&
               loaded.outputs[0].streamUrl == "srt://127.0.0.1:9100?mode=caller" &&
               loaded.outputs[0].streamBitrateKbps == 7200 &&
               loaded.outputs[0].outputType == "window" &&
               loaded.outputs[0].mirrorSourceOutputIndex == -1 &&
               loaded.outputs[1].streamEnabled &&
               loaded.outputs[1].streamProtocol == "rtmp" &&
               loaded.outputs[1].streamBitrateKbps == 4200 &&
               loaded.outputs[1].outputType == "stream" &&
               loaded.outputs[1].mirrorSourceOutputIndex == 0 &&
               loaded.outputs[0].outputId == "out-smoke-program" &&
               loaded.outputs[1].outputId == "out-smoke-stream" &&
               std::abs(loaded.outputs[0].outputAlpha - 0.82f) < 0.01f &&
               loaded.outputs[0].outputDelayMs == 240 &&
               loaded.outputs[0].outputTimeOverlayEnabled &&
               loaded.outputs[0].outputColorSpace == "bt709" &&
               loaded.outputs[0].outputLayoutMode == "span" &&
               loaded.outputs[0].outputOrientationDegrees == 90 &&
               loaded.outputs[0].outputTestCardEnabled &&
               std::abs(loaded.outputs[1].outputAlpha - 0.67f) < 0.01f &&
               loaded.outputs[1].outputDelayMs == 120 &&
               !loaded.outputs[1].outputTimeOverlayEnabled &&
               loaded.outputs[1].outputColorSpace == "srgb" &&
               loaded.outputs[1].outputLayoutMode == "duplicate" &&
               loaded.outputs[1].outputOrientationDegrees == 270 &&
               !loaded.outputs[1].outputTestCardEnabled,
               "output targets persisted");
        expect(loaded.outputs[0].ndiEnabled &&
               loaded.outputs[0].ndiSourceName == "Program Fill" &&
               loaded.outputs[0].ndiKeyEnabled &&
               loaded.outputs[0].ndiKeySourceName == "Program Key",
               "output ndi persisted");
        expect(loaded.groupPresets.size() == 1 &&
               loaded.groupPresets[0].name == "Preset Smoke" &&
               loaded.groupPresets[0].slots.size() == loaded.decks.size() &&
               !loaded.groupPresets[0].slots[0].bypass &&
               loaded.groupPresets[0].slots[0].cueId == "smoke-cue-2",
               "group presets persisted");
        expect(loaded.layerAssignments.size() == 2 &&
               loaded.layerAssignments[0].deckIndex == 0 &&
               loaded.layerAssignments[0].outputIndex == 0 &&
               loaded.layerAssignments[0].outputId == "out-smoke-program" &&
               loaded.layerAssignments[0].layerIndex == 3 &&
               loaded.layerAssignments[0].layerId == "lay-smoke-main" &&
               loaded.layerAssignments[1].outputIndex == 1 &&
               loaded.layerAssignments[1].outputId == "out-smoke-stream" &&
               loaded.layerAssignments[1].layerIndex == 9 &&
               loaded.layerAssignments[1].layerId == "lay-smoke-stream", "layer assignments persisted");
        expect(loadedDeck.ndiEnabled && loadedDeck.ndiSourceName == "Smoke Fill", "ndi fill persisted");
        expect(loadedDeck.ndiKeyEnabled && loadedDeck.ndiKeySourceName == "Smoke Key", "ndi key persisted");
        expect(loadedDeck.canvasViewX == 320 && loadedDeck.canvasViewY == 40, "canvas view persisted");
        expect(loadedDeck.outputRouteDeckIndex == 0 && loadedDeck.outputLayerIndex == 3, "deck route/layer persisted");
        expect(loadedDeck.warpEnabled &&
               std::abs(loadedDeck.warpTopLeftX + 12.0f) < 0.01f &&
               std::abs(loadedDeck.warpBottomRightY + 6.0f) < 0.01f, "warp persisted");
        expect(std::abs(loadedDeck.edgeBlendLeft - 0.08f) < 0.001f &&
               std::abs(loadedDeck.edgeBlendRight - 0.12f) < 0.001f &&
               std::abs(loadedDeck.edgeBlendTop - 0.03f) < 0.001f &&
               std::abs(loadedDeck.edgeBlendBottom - 0.05f) < 0.001f, "edge blend persisted");
        expect(std::abs(loadedDeck.transitionSeconds - 1.5) < 0.01, "transition persisted");
        expect(parseTransitionStyleToken(loadedDeck.transitionStyle) == TransitionStyle::DipBlack, "transition style persisted");
        expect(std::abs(loadedDeck.playlistOpacity - 0.62f) < 0.01f &&
               loadedDeck.playlistAutoFade &&
               std::abs(loadedDeck.playlistFadeSeconds - 1.7) < 0.01,
               "playlist opacity settings persisted");
        expect(std::abs(loadedDeck.playlistTimebaseFps - 29.97) < 0.01 &&
               std::abs(loadedDeck.playlistStartOffsetSeconds - 3600.0) < 0.01 &&
               std::abs(loadedDeck.playlistDefaultCueFadeSeconds - 0.75) < 0.01 &&
               std::abs(loadedDeck.playlistDefaultStillDurationSeconds - 6.5) < 0.01 &&
               loadedDeck.playlistDefaultLoop &&
               loadedDeck.playlistDefaultFadeInEnabled &&
               !loadedDeck.playlistDefaultFadeOutEnabled &&
               !loadedDeck.playlistDefaultAudioEnabled &&
               loadedDeck.playlistDefaultPauseAtBeginning &&
               loadedDeck.playlistDefaultPauseAtEnd &&
               !loadedDeck.playlistDefaultTransitionToNext,
               "playlist preference defaults persisted");
        expect(loadedDeck.timecodeChaseEnabled, "timecode chase persisted");
        expect(!loadedDeck.timecodeJamSyncEnabled && std::abs(loadedDeck.timecodeFreewheelSeconds - 2.5) < 0.01,
               "timecode follower options persisted");
        expect(std::abs(loadedCue.inPointSeconds - 2.0) < 0.01 && std::abs(loadedCue.outPointSeconds - 8.0) < 0.01, "trim persisted");
        expect(std::abs(loadedCue.triggerTimecodeSeconds - 13.0) < 0.01, "cue tc mark persisted");
        expect(std::abs(loadedCue.cueTransitionSeconds - 1.25) < 0.01, "cue transition persisted");
        expect(loadedCue.cueTransitionStyle == "crossfade", "cue transition style persisted");
        expect(loadedCue.cueId == "A1" &&
               loadedCue.hasAudio &&
               !loadedCue.audioEnabled &&
               loadedCue.pauseAtBeginning &&
               loadedCue.pauseOnLastFrame &&
               !loadedCue.transitionToNext &&
               loadedCue.gotoTarget == "Q12",
               "cue parity fields persisted");
        expect(std::abs(loadedCue.outputRotationDegrees - 17.5f) < 0.01f, "cue rotation persisted");
        expect(std::abs(loadedCue.cropLeft - 0.10f) < 0.001f &&
               std::abs(loadedCue.cropRight - 0.05f) < 0.001f &&
               std::abs(loadedCue.cropTop - 0.02f) < 0.001f &&
               std::abs(loadedCue.cropBottom - 0.03f) < 0.001f, "cue crop persisted");
        expect(loadedCue.chromaKeyEnabled &&
               loadedCue.chromaKeyColor.r == 20 &&
               loadedCue.chromaKeyColor.g == 220 &&
               loadedCue.chromaKeyColor.b == 45 &&
               std::abs(loadedCue.chromaKeyTolerance - 88.5f) < 0.01f &&
               std::abs(loadedCue.chromaKeySoftness - 14.0f) < 0.01f, "cue chroma key persisted");
        expect(std::abs(loadedCue.brightness - 1.25f) < 0.01f &&
               std::abs(loadedCue.contrast - 0.85f) < 0.01f &&
               std::abs(loadedCue.saturation - 1.40f) < 0.01f &&
               std::abs(loadedCue.hueShift + 22.0f) < 0.01f, "cue color controls persisted");
        if (loadedDeck.cues.size() > 1) {
          const Cue& img = loadedDeck.cues[1];
          expect(img.kind == CueKind::Image, "still image kind persisted");
          expect(std::abs(img.stillDurationSeconds - 5.0) < 0.01, "still duration persisted");
        }
        if (loadedDeck.cues.size() > 2) {
          const Cue& lt = loadedDeck.cues[2];
          expect(lt.kind == CueKind::LowerThird, "lower third kind persisted");
          expect(lt.lowerThirdText == "Hello World", "lower third text persisted");
          expect(lt.lowerThirdSubtext == "subtitle here", "lower third subtext persisted");
          expect(lt.lowerThirdBgAlpha == 200, "lower third alpha persisted");
        }
      }
      std::error_code ignored;
      fs::remove(smokePath, ignored);
    }

    {
      Project legacy;
      Deck legacyDeck;
      legacyDeck.name = "Legacy Deck";
      legacyDeck.ndiEnabled = true;
      legacyDeck.ndiSourceName = "Legacy Fill";
      legacyDeck.ndiKeyEnabled = true;
      legacyDeck.ndiKeySourceName = "Legacy Key";
      legacy.decks = {legacyDeck};
      legacy.outputs = {OutputTarget {"Legacy Output", 0, 0, false, "window", -1, false, "srt", "", 6000}};
      legacy.layerAssignments = {LayerAssignment {0, 0, 0, true}};
      normalizeProject(legacy);
      expect(!legacy.outputs.empty()
               && legacy.outputs[0].ndiEnabled
               && legacy.outputs[0].ndiSourceName == "Legacy Fill"
               && legacy.outputs[0].ndiKeyEnabled
               && legacy.outputs[0].ndiKeySourceName == "Legacy Key",
             "legacy deck ndi migrated to output");
    }

    std::cout << "smoke failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
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

  OutputRuntime* runtimeForOutput(int outputIndex) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(outputRuntimes_.size())) {
      return nullptr;
    }
    return &outputRuntimes_[outputIndex];
  }

  const OutputRuntime* runtimeForOutput(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(outputRuntimes_.size())) {
      return nullptr;
    }
    return &outputRuntimes_[outputIndex];
  }

  void setOutputRecoveryPausedByEscape(int outputIndex, bool paused) {
    if (OutputRuntime* runtime = runtimeForOutput(outputIndex); runtime) {
      runtime->recoveryPausedByEscape = paused;
    }
  }

  const OutputTarget& focusedOutput() const {
    static OutputTarget fallback;
    if (project_.outputs.empty()) {
      return fallback;
    }
    int index = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    return project_.outputs[index];
  }

  OutputTarget& focusedOutputMutable() {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      project_.outputs.push_back(OutputTarget {});
      project_.focusedOutputIndex = 0;
    }
    int index = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    return project_.outputs[index];
  }

  std::string outputLabel(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return outputDefaultName(0);
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    return output.name.empty() ? outputDefaultName(outputIndex) : output.name;
  }

  std::string deckLabel(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return deckDefaultName(0);
    }
    const Deck& deck = project_.decks[deckIndex];
    return deck.name.empty() ? deckDefaultName(deckIndex) : deck.name;
  }

  int outputIndexById(const std::string& outputId) const {
    std::string needle = trim(outputId);
    if (needle.empty()) {
      return -1;
    }
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (project_.outputs[outputIndex].outputId == needle) {
        return outputIndex;
      }
    }
    return -1;
  }

  int resolveAssignmentOutputIndex(const LayerAssignment& assignment) const {
    int fromId = outputIndexById(assignment.outputId);
    if (fromId >= 0) {
      return fromId;
    }
    if (assignment.outputIndex >= 0 && assignment.outputIndex < static_cast<int>(project_.outputs.size())) {
      return assignment.outputIndex;
    }
    return -1;
  }

  void syncAssignmentOutputReference(LayerAssignment& assignment) {
    int resolved = resolveAssignmentOutputIndex(assignment);
    if (resolved < 0) {
      return;
    }
    assignment.outputIndex = resolved;
    assignment.outputId = project_.outputs[resolved].outputId;
  }

  std::optional<int> primaryAssignmentIndexForDeck(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return std::nullopt;
    }
    std::optional<int> best;
    for (int i = 0; i < static_cast<int>(project_.layerAssignments.size()); ++i) {
      const LayerAssignment& assignment = project_.layerAssignments[i];
      if (!assignment.enabled || assignment.deckIndex != deckIndex) {
        continue;
      }
      if (resolveAssignmentOutputIndex(assignment) < 0) {
        continue;
      }
      if (!best || assignment.layerIndex < project_.layerAssignments[*best].layerIndex) {
        best = i;
      }
    }
    return best;
  }

  std::optional<int> primaryOutputIndexForDeck(int deckIndex) const {
    if (auto assignmentIndex = primaryAssignmentIndexForDeck(deckIndex); assignmentIndex) {
      int resolved = resolveAssignmentOutputIndex(project_.layerAssignments[*assignmentIndex]);
      if (resolved >= 0) {
        return resolved;
      }
    }
    return std::nullopt;
  }

  int primaryLayerIndexForDeck(int deckIndex) const {
    if (auto assignmentIndex = primaryAssignmentIndexForDeck(deckIndex); assignmentIndex) {
      return std::clamp(project_.layerAssignments[*assignmentIndex].layerIndex, 0, 255);
    }
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return 0;
    }
    return std::clamp(project_.decks[deckIndex].outputLayerIndex, 0, 255);
  }

  int resolveDeckOutputHostIndex(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return deckIndex;
    }
    auto outputIndex = primaryOutputIndexForDeck(deckIndex);
    if (!outputIndex || *outputIndex < 0 || *outputIndex >= static_cast<int>(project_.outputs.size())) {
      return std::clamp(project_.decks[deckIndex].outputRouteDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    }
    return std::clamp(project_.outputs[*outputIndex].hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
  }

  int outputIndexForHostDeck(int hostDeckIndex) const {
    for (int i = 0; i < static_cast<int>(project_.outputs.size()); ++i) {
      if (project_.outputs[i].hostDeckIndex == hostDeckIndex) {
        return i;
      }
    }
    return -1;
  }

  int ensureOutputIndexForHostDeck(int hostDeckIndex, bool* created = nullptr) {
    normalizeProject(project_);
    if (created) {
      *created = false;
    }
    int clampedHost = std::clamp(hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    int existing = outputIndexForHostDeck(clampedHost);
    if (existing >= 0) {
      return existing;
    }
    OutputTarget output;
    output.name = outputDefaultName(static_cast<int>(project_.outputs.size()));
    output.hostDeckIndex = clampedHost;
    output.displayIndex = std::max(0, project_.decks[clampedHost].outputDisplayIndex);
    output.enabled = false;
    output.outputId = makeOutputId(output, static_cast<int>(project_.outputs.size()));
    project_.outputs.push_back(output);
    if (created) {
      *created = true;
    }
    return static_cast<int>(project_.outputs.size()) - 1;
  }

  int ensurePrimaryAssignmentIndexForDeck(int deckIndex) {
    normalizeProject(project_);
    if (auto existing = primaryAssignmentIndexForDeck(deckIndex); existing) {
      syncAssignmentOutputReference(project_.layerAssignments[*existing]);
      return *existing;
    }
    LayerAssignment assignment;
    assignment.deckIndex = std::clamp(deckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    assignment.outputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
    assignment.outputId = project_.outputs[assignment.outputIndex].outputId;
    assignment.layerIndex = std::clamp(project_.decks[assignment.deckIndex].outputLayerIndex, 0, 255);
    assignment.enabled = true;
    project_.layerAssignments.push_back(assignment);
    return static_cast<int>(project_.layerAssignments.size()) - 1;
  }

  std::vector<std::pair<int, int>> layeredDeckEntriesForOutput(int outputIndex) const {
    std::vector<std::pair<int, int>> entries;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return entries;
    }
    for (const auto& assignment : project_.layerAssignments) {
      if (!assignment.enabled) {
        continue;
      }
      if (resolveAssignmentOutputIndex(assignment) != outputIndex) {
        continue;
      }
      if (assignment.deckIndex < 0 || assignment.deckIndex >= static_cast<int>(project_.decks.size())) {
        continue;
      }
      entries.emplace_back(std::clamp(assignment.layerIndex, 0, 255), assignment.deckIndex);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
      if (a.first != b.first) {
        return a.first < b.first;
      }
      return a.second < b.second;
    });
    return entries;
  }

  int nextLayerIndexForOutput(int outputIndex, int ignoreDeckIndex = -1) const {
    int nextLayer = 0;
    for (const auto& assignment : project_.layerAssignments) {
      if (!assignment.enabled || resolveAssignmentOutputIndex(assignment) != outputIndex) {
        continue;
      }
      if (assignment.deckIndex == ignoreDeckIndex) {
        continue;
      }
      nextLayer = std::max(nextLayer, assignment.layerIndex + 1);
    }
    return std::clamp(nextLayer, 0, 255);
  }

  std::string deckOutputRoutingLabel(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "out:-- layer:--";
    }
    auto outputIndex = primaryOutputIndexForDeck(deckIndex);
    if (!outputIndex) {
      return "out:-- layer:" + std::to_string(primaryLayerIndexForDeck(deckIndex));
    }
    return "out:" + std::to_string(*outputIndex + 1) + " layer:" + std::to_string(primaryLayerIndexForDeck(deckIndex));
  }

  std::optional<int> parseDeckReferenceToken(const std::string& token) const {
    std::string trimmed = trim(token);
    if (trimmed.empty()) {
      return std::nullopt;
    }
    std::string upper = toUpper(trimmed);
    if (upper == "SELF" || upper == "THIS" || upper == "FOCUSED" || upper == "FOCUS") {
      return project_.focusedDeckIndex;
    }
    if (upper == "HOST" || upper == "OUTPUT") {
      return resolveDeckOutputHostIndex(project_.focusedDeckIndex);
    }
    try {
      int index = std::stoi(trimmed);
      if (index >= 1 && index <= static_cast<int>(project_.decks.size())) {
        return index - 1;
      }
    } catch (...) {
    }
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      std::string deckName = project_.decks[deckIndex].name.empty()
        ? deckDefaultName(deckIndex)
        : project_.decks[deckIndex].name;
      if (toUpper(deckName) == upper) {
        return deckIndex;
      }
    }
    return std::nullopt;
  }

  bool setFocusedDeckIndex(int deckIndex) {
    normalizeProject(project_);
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    project_.focusedDeckIndex = deckIndex;
    if (auto outputIndex = primaryOutputIndexForDeck(deckIndex); outputIndex) {
      project_.focusedOutputIndex = *outputIndex;
    }
    selectionChangedAt_ = SDL_GetTicks64();
    cueSettingsScroll_ = 0;
    cueSettingsScrollMax_ = 0;
    clearCueFindState();
    triggerToast("deck: " + focusedDeckLabel());
    markProjectDirty();
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

  bool setFocusedOutputIndex(int outputIndex) {
    normalizeProject(project_);
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    if (project_.focusedOutputIndex == outputIndex) {
      triggerToast("output: " + outputLabel(outputIndex));
      return false;
    }
    project_.focusedOutputIndex = outputIndex;
    triggerToast("output: " + outputLabel(outputIndex));
    markProjectDirty();
    return true;
  }

  void cycleFocusedOutput(int direction) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return;
    }
    int outputCount = static_cast<int>(project_.outputs.size());
    int nextIndex = (project_.focusedOutputIndex + direction + outputCount) % outputCount;
    if (setFocusedOutputIndex(nextIndex)) {
      playUiSound(UiSoundEffect::Navigate);
    }
  }

  int addOutput(int hostDeckIndex = -1, std::string outputType = "window") {
    normalizeProject(project_);
    if (project_.decks.empty()) {
      return 0;
    }
    int normalizedHost = hostDeckIndex;
    if (normalizedHost < 0 || normalizedHost >= static_cast<int>(project_.decks.size())) {
      normalizedHost = project_.focusedDeckIndex;
    }
    normalizedHost = std::clamp(normalizedHost, 0, static_cast<int>(project_.decks.size()) - 1);

    OutputTarget output;
    output.name = outputDefaultName(static_cast<int>(project_.outputs.size()));
    output.hostDeckIndex = normalizedHost;
    output.displayIndex = std::max(0, project_.decks[normalizedHost].outputDisplayIndex);
    output.enabled = false;
    output.outputType = normalizeOutputType(outputType);
    output.mirrorSourceOutputIndex = -1;
    output.streamEnabled = (output.outputType == "stream");
    output.streamProtocol = "srt";
    output.streamUrl = defaultOutputStreamUrl(output.streamProtocol, static_cast<int>(project_.outputs.size()));
    output.streamBitrateKbps = 6000;
    output.outputId = makeOutputId(output, static_cast<int>(project_.outputs.size()));
    project_.outputs.push_back(output);
    int newIndex = static_cast<int>(project_.outputs.size()) - 1;
    project_.focusedOutputIndex = newIndex;

    if (!rebuildOutputRuntimes()) {
      triggerToast("output create failed");
      return project_.focusedOutputIndex;
    }
    triggerToast("output added (off): " + outputLabel(newIndex));
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
    return newIndex;
  }

  bool removeOutput(int outputIndex) {
    normalizeProject(project_);
    int outputCount = static_cast<int>(project_.outputs.size());
    if (outputCount <= 1) {
      triggerToast("keep at least one output");
      return false;
    }
    if (outputIndex < 0 || outputIndex >= outputCount) {
      return false;
    }

    int fallbackIndex = (outputIndex + 1 < outputCount) ? outputIndex + 1 : outputIndex - 1;
    fallbackIndex = std::clamp(fallbackIndex, 0, outputCount - 1);
    int fallbackHostDeck = std::clamp(project_.outputs[fallbackIndex].hostDeckIndex, 0,
                                      static_cast<int>(project_.decks.size()) - 1);
    std::string removedLabel = outputLabel(outputIndex);
    std::string removedOutputId = trim(project_.outputs[outputIndex].outputId);

    std::vector<bool> deckTouched(project_.decks.size(), false);
    for (const LayerAssignment& assignment : project_.layerAssignments) {
      bool removeAssignment = false;
      if (!removedOutputId.empty() && trim(assignment.outputId) == removedOutputId) {
        removeAssignment = true;
      } else if (removedOutputId.empty() && assignment.outputIndex == outputIndex) {
        removeAssignment = true;
      }
      if (removeAssignment &&
          assignment.deckIndex >= 0 &&
          assignment.deckIndex < static_cast<int>(deckTouched.size())) {
        deckTouched[assignment.deckIndex] = true;
      }
    }

    for (auto it = project_.layerAssignments.begin(); it != project_.layerAssignments.end();) {
      bool removeAssignment = false;
      if (!removedOutputId.empty() && trim(it->outputId) == removedOutputId) {
        removeAssignment = true;
      } else if (removedOutputId.empty() && it->outputIndex == outputIndex) {
        removeAssignment = true;
      }
      if (removeAssignment) {
        it = project_.layerAssignments.erase(it);
      } else {
        if (it->outputIndex > outputIndex) {
          it->outputIndex -= 1;
        }
        ++it;
      }
    }

    project_.outputs.erase(project_.outputs.begin() + outputIndex);

    for (OutputTarget& output : project_.outputs) {
      if (output.mirrorSourceOutputIndex == outputIndex) {
        output.mirrorSourceOutputIndex = -1;
      } else if (output.mirrorSourceOutputIndex > outputIndex) {
        output.mirrorSourceOutputIndex -= 1;
      }
    }

    if (project_.focusedOutputIndex > outputIndex) {
      project_.focusedOutputIndex -= 1;
    } else if (project_.focusedOutputIndex >= static_cast<int>(project_.outputs.size())) {
      project_.focusedOutputIndex = static_cast<int>(project_.outputs.size()) - 1;
    }

    int fallbackNewIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (!deckTouched[deckIndex]) {
        continue;
      }
      project_.decks[deckIndex].outputRouteDeckIndex = fallbackHostDeck;
      bool hasAssignment = false;
      for (const LayerAssignment& assignment : project_.layerAssignments) {
        if (!assignment.enabled || assignment.deckIndex != deckIndex) {
          continue;
        }
        hasAssignment = true;
        break;
      }
      if (!hasAssignment) {
        LayerAssignment assignment;
        assignment.deckIndex = deckIndex;
        assignment.outputIndex = fallbackNewIndex;
        assignment.outputId = project_.outputs[fallbackNewIndex].outputId;
        assignment.layerIndex = std::clamp(project_.decks[deckIndex].outputLayerIndex, 0, 255);
        assignment.enabled = true;
        project_.layerAssignments.push_back(assignment);
      }
    }

    normalizeProject(project_);
    if (!rebuildOutputRuntimes()) {
      triggerToast("output remove failed");
      return false;
    }
    triggerToast("output removed: " + removedLabel);
    playUiSound(UiSoundEffect::Delete);
    markProjectDirty();
    return true;
  }

  bool assignDeckToOutput(int deckIndex, int outputIndex, std::optional<int> requestedLayer = std::nullopt) {
    normalizeProject(project_);
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }

    int assignmentIndex = -1;
    for (int i = 0; i < static_cast<int>(project_.layerAssignments.size()); ++i) {
      LayerAssignment& candidate = project_.layerAssignments[i];
      if (candidate.deckIndex == deckIndex && resolveAssignmentOutputIndex(candidate) == outputIndex) {
        assignmentIndex = i;
        break;
      }
    }

    bool created = false;
    if (assignmentIndex < 0) {
      LayerAssignment assignment;
      assignment.deckIndex = deckIndex;
      assignment.outputIndex = outputIndex;
      assignment.outputId = project_.outputs[outputIndex].outputId;
      assignment.layerIndex = nextLayerIndexForOutput(outputIndex);
      assignment.enabled = true;
      project_.layerAssignments.push_back(assignment);
      assignmentIndex = static_cast<int>(project_.layerAssignments.size()) - 1;
      created = true;
    }

    LayerAssignment& assignment = project_.layerAssignments[assignmentIndex];
    int nextLayer = assignment.layerIndex;
    if (requestedLayer) {
      nextLayer = std::clamp(*requestedLayer, 0, 255);
    } else if (created || !assignment.enabled) {
      nextLayer = nextLayerIndexForOutput(outputIndex, deckIndex);
    }

    bool changed = created
      || !assignment.enabled
      || assignment.layerIndex != nextLayer;
    assignment.deckIndex = deckIndex;
    assignment.outputIndex = outputIndex;
    assignment.outputId = project_.outputs[outputIndex].outputId;
    assignment.layerIndex = nextLayer;
    assignment.enabled = true;
    project_.focusedOutputIndex = outputIndex;
    project_.decks[deckIndex].outputRouteDeckIndex = std::clamp(project_.outputs[outputIndex].hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    project_.decks[deckIndex].outputLayerIndex = std::clamp(nextLayer, 0, 255);

    triggerToast("assign: " + deckLabel(deckIndex) + " -> " + outputLabel(outputIndex) + " L" + std::to_string(nextLayer));
    if (changed) {
      playUiSound(UiSoundEffect::Toggle);
      markProjectDirty();
    }
    return changed;
  }

  bool assignFocusedDeckToFocusedOutput(std::optional<int> requestedLayer = std::nullopt) {
    return assignDeckToOutput(project_.focusedDeckIndex, project_.focusedOutputIndex, requestedLayer);
  }

  std::optional<int> assignmentIndexForDeckOutput(int deckIndex, int outputIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return std::nullopt;
    }
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return std::nullopt;
    }
    for (int i = 0; i < static_cast<int>(project_.layerAssignments.size()); ++i) {
      const LayerAssignment& assignment = project_.layerAssignments[i];
      if (!assignment.enabled || assignment.deckIndex != deckIndex) {
        continue;
      }
      if (resolveAssignmentOutputIndex(assignment) != outputIndex) {
        continue;
      }
      return i;
    }
    return std::nullopt;
  }

  int enabledAssignmentCountForDeck(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return 0;
    }
    int count = 0;
    for (const LayerAssignment& assignment : project_.layerAssignments) {
      if (!assignment.enabled || assignment.deckIndex != deckIndex) {
        continue;
      }
      if (resolveAssignmentOutputIndex(assignment) < 0) {
        continue;
      }
      ++count;
    }
    return count;
  }

  bool setDeckOutputAssignmentLayer(int deckIndex, int outputIndex, int layerIndex) {
    normalizeProject(project_);
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    int clamped = std::clamp(layerIndex, 0, 255);
    bool changed = false;
    for (LayerAssignment& assignment : project_.layerAssignments) {
      if (assignment.deckIndex != deckIndex || !assignment.enabled) {
        continue;
      }
      if (resolveAssignmentOutputIndex(assignment) != outputIndex) {
        continue;
      }
      if (assignment.layerIndex != clamped) {
        assignment.layerIndex = clamped;
        changed = true;
      }
    }
    if (!changed) {
      triggerToast("layer " + std::to_string(clamped));
      return false;
    }
    if (auto primaryOutput = primaryOutputIndexForDeck(deckIndex); primaryOutput && *primaryOutput == outputIndex) {
      project_.decks[deckIndex].outputLayerIndex = clamped;
    }
    triggerToast("layer set d" + std::to_string(deckIndex + 1)
      + " -> " + outputLabel(outputIndex) + " L" + std::to_string(clamped));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool unassignDeckFromOutput(int deckIndex, int outputIndex) {
    normalizeProject(project_);
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    int remainingElsewhere = 0;
    for (const LayerAssignment& assignment : project_.layerAssignments) {
      if (!assignment.enabled || assignment.deckIndex != deckIndex) {
        continue;
      }
      if (resolveAssignmentOutputIndex(assignment) != outputIndex) {
        ++remainingElsewhere;
      }
    }
    if (remainingElsewhere <= 0) {
      triggerToast("routing: keep at least one output");
      return false;
    }

    bool removed = false;
    for (auto it = project_.layerAssignments.begin(); it != project_.layerAssignments.end();) {
      if (it->deckIndex == deckIndex && resolveAssignmentOutputIndex(*it) == outputIndex) {
        it = project_.layerAssignments.erase(it);
        removed = true;
      } else {
        ++it;
      }
    }
    if (!removed) {
      return false;
    }
    normalizeProject(project_);
    triggerToast("unassign: " + deckLabel(deckIndex) + " x " + outputLabel(outputIndex));
    playUiSound(UiSoundEffect::Delete);
    markProjectDirty();
    return true;
  }

  bool moveDeckToOutput(int deckIndex, int outputIndex, std::optional<int> requestedLayer = std::nullopt) {
    normalizeProject(project_);
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    bool changed = assignDeckToOutput(deckIndex, outputIndex, requestedLayer);

    bool removedOthers = false;
    bool keptOneForTarget = false;
    for (auto it = project_.layerAssignments.begin(); it != project_.layerAssignments.end();) {
      if (it->deckIndex != deckIndex) {
        ++it;
        continue;
      }
      int resolved = resolveAssignmentOutputIndex(*it);
      if (resolved != outputIndex) {
        it = project_.layerAssignments.erase(it);
        removedOthers = true;
        continue;
      }
      if (!keptOneForTarget) {
        keptOneForTarget = true;
        ++it;
        continue;
      }
      it = project_.layerAssignments.erase(it);
      removedOthers = true;
    }

    if (removedOthers) {
      normalizeProject(project_);
      triggerToast("move: " + deckLabel(deckIndex) + " -> " + outputLabel(outputIndex));
      playUiSound(UiSoundEffect::Toggle);
      markProjectDirty();
    }
    return changed || removedOthers;
  }

  bool setFocusedOutputHostDeck(int hostDeckIndex) {
    normalizeProject(project_);
    if (hostDeckIndex < 0 || hostDeckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    OutputTarget& output = focusedOutputMutable();
    int clampedHost = std::clamp(hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    if (output.hostDeckIndex == clampedHost) {
      triggerToast("host: " + deckLabel(clampedHost));
      return false;
    }
    output.hostDeckIndex = clampedHost;
    project_.decks[clampedHost].outputDisplayIndex = std::max(0, output.displayIndex);
    applyOutputDisplaySelection(project_.focusedOutputIndex);
    triggerToast("host: " + deckLabel(clampedHost));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool setFocusedOutputEnabled(bool enabled, bool autoFullscreenWhenEnabling = true) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return false;
    }
    int outputIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    OutputTarget& output = project_.outputs[outputIndex];
    bool windowOutput = normalizeOutputType(output.outputType) == "window";
    bool autoSwitchedToNative = false;
    if (enabled && windowOutput && !project_.outputFollowDisplay) {
      project_.outputFollowDisplay = true;
      autoSwitchedToNative = true;
    }

    if (output.enabled == enabled) {
      // Treat repeated ON as a recovery operation: re-apply display placement,
      // re-fullscreen window outputs, and raise the window.
      if (enabled) {
        if (autoSwitchedToNative) {
          applyOutputDisplaySelectionAllOutputs(true);
        }
        setOutputRecoveryPausedByEscape(outputIndex, false);
        if (windowOutput) {
          recoverWindowOutputIfNeeded(outputIndex, true);
          if (autoSwitchedToNative) {
            triggerToast("output recovered: auto native");
          }
        } else {
          triggerToast("output: on (stream)");
        }
        playUiSound(UiSoundEffect::Toggle);
        if (autoSwitchedToNative) {
          markProjectDirty();
        }
        return true;
      }
      triggerToast("output: off");
      return false;
    }

    output.enabled = enabled;
    setOutputRecoveryPausedByEscape(outputIndex, false);
    if (!enabled) {
      stopOutputStream(outputIndex);
      if (auto* runtime = runtimeForOutput(outputIndex)) {
        runtime->delayFrames.clear();
        runtime->latestCapturedFrame = {};
      }
    }
    if (autoSwitchedToNative) {
      applyOutputDisplaySelectionAllOutputs(true);
    } else {
      applyOutputDisplaySelection(outputIndex);
    }

    if (enabled && autoFullscreenWhenEnabling && windowOutput) {
      enableOutputFullscreen(outputIndex, false);
      triggerToast("output on: " + currentDisplayLabel() + (autoSwitchedToNative ? "  auto native" : ""));
    } else {
      triggerToast(std::string("output: ") + (enabled ? "on" : "off"));
    }

    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  void toggleFocusedOutputEnabled() {
    setFocusedOutputEnabled(!focusedOutput().enabled);
  }

  bool setFocusedOutputType(const std::string& outputType) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return false;
    }
    int outputIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    OutputTarget& output = project_.outputs[outputIndex];
    std::string current = normalizeOutputType(output.outputType);
    std::string nextType = normalizeOutputType(outputType);
    if (current == nextType) {
      triggerToast("output type: " + current);
      return false;
    }
    output.outputType = nextType;
    if (nextType == "stream") {
      output.streamEnabled = true;
    } else {
      output.mirrorSourceOutputIndex = -1;
    }
    stopOutputStream(outputIndex);
    if (rebuildOutputRuntimes()) {
      if (project_.outputs[outputIndex].enabled && nextType == "window") {
        enableOutputFullscreen(outputIndex, false);
      }
      triggerToast("output type: " + nextType);
      playUiSound(UiSoundEffect::Toggle);
      markProjectDirty();
      return true;
    }
    triggerToast("output type change failed");
    return false;
  }

  bool setFocusedOutputMirrorSource(int sourceOutputIndex) {
    normalizeProject(project_);
    int normalized = sourceOutputIndex;
    if (normalized < 0 || normalized >= static_cast<int>(project_.outputs.size()) ||
        normalized == project_.focusedOutputIndex) {
      normalized = -1;
    }
    OutputTarget& output = focusedOutputMutable();
    if (output.mirrorSourceOutputIndex == normalized) {
      std::string label = normalized >= 0 ? ("out " + std::to_string(normalized + 1)) : "off";
      triggerToast("mirror: " + label);
      return false;
    }
    output.mirrorSourceOutputIndex = normalized;
    stopOutputStream(project_.focusedOutputIndex);
    std::string label = normalized >= 0 ? ("out " + std::to_string(normalized + 1)) : "off";
    triggerToast("mirror: " + label);
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  void cycleFocusedOutputMirrorSource(int direction) {
    normalizeProject(project_);
    std::vector<int> candidates;
    candidates.push_back(-1);
    for (int i = 0; i < static_cast<int>(project_.outputs.size()); ++i) {
      if (i == project_.focusedOutputIndex) {
        continue;
      }
      candidates.push_back(i);
    }
    if (candidates.empty()) {
      return;
    }
    int current = focusedOutput().mirrorSourceOutputIndex;
    auto it = std::find(candidates.begin(), candidates.end(), current);
    int index = (it == candidates.end()) ? 0 : static_cast<int>(std::distance(candidates.begin(), it));
    int next = (index + direction + static_cast<int>(candidates.size())) % static_cast<int>(candidates.size());
    setFocusedOutputMirrorSource(candidates[next]);
  }

  bool promptFocusedOutputMirrorSourcePicker() {
    normalizeProject(project_);
    std::vector<std::pair<std::string, std::string>> choices;
    choices.emplace_back("Off (render own assignments)", "-1");
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (outputIndex == project_.focusedOutputIndex) {
        continue;
      }
      std::string label = "Out " + std::to_string(outputIndex + 1) + " - " + outputLabel(outputIndex);
      choices.emplace_back(label, std::to_string(outputIndex));
    }
    std::string initial = std::to_string(focusedOutput().mirrorSourceOutputIndex);
    auto choice = pickChoiceFromList("Mirror Source", "Choose output feed to mirror", choices, initial);
    if (!choice) {
      return false;
    }
    try {
      int mirrorIndex = std::stoi(trim(*choice));
      return setFocusedOutputMirrorSource(mirrorIndex);
    } catch (...) {
      triggerToast("mirror: invalid");
      return false;
    }
  }

  bool setFocusedOutputStreamEnabled(bool enabled) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    if (output.streamEnabled == enabled) {
      triggerToast("stream: " + std::string(enabled ? "on" : "off"));
      return false;
    }
    output.streamEnabled = enabled;
    output.streamProtocol = normalizeOutputStreamProtocol(output.streamProtocol);
    if (trim(output.streamUrl).empty()) {
      output.streamUrl = defaultOutputStreamUrl(output.streamProtocol, project_.focusedOutputIndex);
    }
    if (!enabled) {
      stopOutputStream(project_.focusedOutputIndex);
    } else {
      stopOutputStream(project_.focusedOutputIndex);
    }
    triggerToast("stream: " + std::string(enabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  void toggleFocusedOutputStreamEnabled() {
    setFocusedOutputStreamEnabled(!focusedOutput().streamEnabled);
  }

  bool setFocusedOutputStreamProtocol(const std::string& protocol) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    std::string current = normalizeOutputStreamProtocol(output.streamProtocol);
    std::string nextProtocol = normalizeOutputStreamProtocol(protocol);
    if (nextProtocol == current) {
      triggerToast("stream proto: " + toUpper(current));
      return false;
    }
    output.streamProtocol = nextProtocol;
    if (trim(output.streamUrl).empty() || output.streamUrl == defaultOutputStreamUrl(current, project_.focusedOutputIndex)) {
      output.streamUrl = defaultOutputStreamUrl(nextProtocol, project_.focusedOutputIndex);
    }
    stopOutputStream(project_.focusedOutputIndex);
    triggerToast("stream proto: " + toUpper(nextProtocol));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool cycleFocusedOutputStreamProtocol(int direction) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    static const std::array<std::string, 2> protocols {"srt", "rtmp"};
    std::string current = normalizeOutputStreamProtocol(output.streamProtocol);
    int index = 0;
    for (int i = 0; i < static_cast<int>(protocols.size()); ++i) {
      if (protocols[i] == current) {
        index = i;
        break;
      }
    }
    int next = (index + direction + static_cast<int>(protocols.size())) % static_cast<int>(protocols.size());
    return setFocusedOutputStreamProtocol(protocols[next]);
  }

  bool setFocusedOutputStreamUrl(const std::string& streamUrl) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    std::string normalized = trim(streamUrl);
    if (normalized.empty()) {
      normalized = defaultOutputStreamUrl(output.streamProtocol, project_.focusedOutputIndex);
    }
    if (output.streamUrl == normalized) {
      triggerToast("stream url unchanged");
      return false;
    }
    output.streamUrl = normalized;
    stopOutputStream(project_.focusedOutputIndex);
    triggerToast("stream url set");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool setFocusedOutputStreamBitrateKbps(int bitrateKbps) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    int clamped = std::clamp(bitrateKbps, 500, 50000);
    if (output.streamBitrateKbps == clamped) {
      triggerToast("stream bitrate: " + std::to_string(clamped) + " kbps");
      return false;
    }
    output.streamBitrateKbps = clamped;
    stopOutputStream(project_.focusedOutputIndex);
    triggerToast("stream bitrate: " + std::to_string(clamped) + " kbps");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool setFocusedOutputAlpha(float alpha) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    float clamped = std::clamp(alpha, 0.0f, 1.0f);
    if (std::fabs(output.outputAlpha - clamped) < 0.001f) {
      int pct = static_cast<int>(std::lround(clamped * 100.0f));
      triggerToast("output alpha: " + std::to_string(pct) + "%");
      return false;
    }
    output.outputAlpha = clamped;
    int pct = static_cast<int>(std::lround(clamped * 100.0f));
    triggerToast("output alpha: " + std::to_string(pct) + "%");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool setFocusedOutputDelayMs(int delayMs) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    int clamped = std::clamp(delayMs, 0, 5000);
    if (output.outputDelayMs == clamped) {
      triggerToast("output delay: " + std::to_string(clamped) + " ms");
      return false;
    }
    output.outputDelayMs = clamped;
    if (auto* runtime = runtimeForOutput(project_.focusedOutputIndex)) {
      runtime->delayFrames.clear();
    }
    triggerToast("output delay: " + std::to_string(clamped) + " ms");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool setFocusedOutputTimeOverlayEnabled(bool enabled) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    if (output.outputTimeOverlayEnabled == enabled) {
      triggerToast(enabled ? "output overlay on" : "output overlay off");
      return false;
    }
    output.outputTimeOverlayEnabled = enabled;
    triggerToast(enabled ? "output overlay on" : "output overlay off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool toggleFocusedOutputTimeOverlayEnabled() {
    return setFocusedOutputTimeOverlayEnabled(!focusedOutput().outputTimeOverlayEnabled);
  }

  bool setFocusedOutputColorSpace(const std::string& colorSpaceToken) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    std::string next = normalizeOutputColorSpace(colorSpaceToken);
    if (output.outputColorSpace == next) {
      triggerToast("color space: " + toUpper(next));
      return false;
    }
    output.outputColorSpace = next;
    stopOutputStream(project_.focusedOutputIndex);
    triggerToast("color space: " + toUpper(next));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool cycleFocusedOutputColorSpace(int direction) {
    normalizeProject(project_);
    static const std::array<std::string, 3> kColorSpaces {"auto", "bt709", "srgb"};
    std::string current = normalizeOutputColorSpace(focusedOutput().outputColorSpace);
    int index = 0;
    for (int i = 0; i < static_cast<int>(kColorSpaces.size()); ++i) {
      if (kColorSpaces[i] == current) {
        index = i;
        break;
      }
    }
    int next = (index + direction + static_cast<int>(kColorSpaces.size())) % static_cast<int>(kColorSpaces.size());
    return setFocusedOutputColorSpace(kColorSpaces[next]);
  }

  bool setFocusedOutputLayoutMode(const std::string& modeToken) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    std::string next = normalizeOutputLayoutMode(modeToken);
    if (normalizeOutputLayoutMode(output.outputLayoutMode) == next) {
      triggerToast("layout: " + next);
      return false;
    }
    output.outputLayoutMode = next;
    triggerToast("layout: " + next);
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool cycleFocusedOutputLayoutMode(int direction) {
    normalizeProject(project_);
    static const std::array<std::string, 2> kModes {"span", "duplicate"};
    std::string current = normalizeOutputLayoutMode(focusedOutput().outputLayoutMode);
    int index = 0;
    for (int i = 0; i < static_cast<int>(kModes.size()); ++i) {
      if (kModes[i] == current) {
        index = i;
        break;
      }
    }
    int next = (index + direction + static_cast<int>(kModes.size())) % static_cast<int>(kModes.size());
    return setFocusedOutputLayoutMode(kModes[next]);
  }

  bool setFocusedOutputOrientationDegrees(int degrees) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    int normalized = normalizeOutputOrientationDegrees(degrees);
    if (normalizeOutputOrientationDegrees(output.outputOrientationDegrees) == normalized) {
      triggerToast("orientation: " + outputOrientationLabel(normalized));
      return false;
    }
    output.outputOrientationDegrees = normalized;
    if (auto* runtime = runtimeForOutput(project_.focusedOutputIndex)) {
      runtime->delayFrames.clear();
      runtime->latestCapturedFrame = {};
    }
    triggerToast("orientation: " + outputOrientationLabel(normalized));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool cycleFocusedOutputOrientation(int direction) {
    normalizeProject(project_);
    static const std::array<int, 4> kOrientations {0, 90, 180, 270};
    int current = normalizeOutputOrientationDegrees(focusedOutput().outputOrientationDegrees);
    int index = 0;
    for (int i = 0; i < static_cast<int>(kOrientations.size()); ++i) {
      if (kOrientations[i] == current) {
        index = i;
        break;
      }
    }
    int next = (index + direction + static_cast<int>(kOrientations.size())) % static_cast<int>(kOrientations.size());
    return setFocusedOutputOrientationDegrees(kOrientations[next]);
  }

  bool setFocusedOutputTestCardEnabled(bool enabled) {
    normalizeProject(project_);
    OutputTarget& output = focusedOutputMutable();
    if (output.outputTestCardEnabled == enabled) {
      triggerToast(std::string("test card: ") + (enabled ? "on" : "off"));
      return false;
    }
    output.outputTestCardEnabled = enabled;
    if (auto* runtime = runtimeForOutput(project_.focusedOutputIndex)) {
      runtime->delayFrames.clear();
      runtime->latestCapturedFrame = {};
    }
    triggerToast(std::string("test card: ") + (enabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool toggleFocusedOutputTestCardEnabled() {
    return setFocusedOutputTestCardEnabled(!focusedOutput().outputTestCardEnabled);
  }

  bool setAllOutputsTestCardEnabled(bool enabled) {
    normalizeProject(project_);
    bool changed = false;
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      OutputTarget& output = project_.outputs[outputIndex];
      if (output.outputTestCardEnabled == enabled) {
        continue;
      }
      output.outputTestCardEnabled = enabled;
      if (auto* runtime = runtimeForOutput(outputIndex)) {
        runtime->delayFrames.clear();
        runtime->latestCapturedFrame = {};
      }
      changed = true;
    }
    triggerToast(std::string("test cards: ") + (enabled ? "on" : "off"));
    if (changed) {
      playUiSound(UiSoundEffect::Toggle);
      markProjectDirty();
    }
    return changed;
  }

  const GroupPreset* focusedGroupPreset() const {
    if (project_.groupPresets.empty()) {
      return nullptr;
    }
    int index = std::clamp(project_.focusedGroupPresetIndex, 0, static_cast<int>(project_.groupPresets.size()) - 1);
    return &project_.groupPresets[index];
  }

  GroupPreset* focusedGroupPresetMutable() {
    normalizeProject(project_);
    if (project_.groupPresets.empty()) {
      return nullptr;
    }
    int index = std::clamp(project_.focusedGroupPresetIndex, 0, static_cast<int>(project_.groupPresets.size()) - 1);
    return &project_.groupPresets[index];
  }

  std::string groupPresetLabel(int presetIndex) const {
    if (presetIndex < 0 || presetIndex >= static_cast<int>(project_.groupPresets.size())) {
      return groupPresetDefaultName(0);
    }
    const GroupPreset& preset = project_.groupPresets[presetIndex];
    return preset.name.empty() ? groupPresetDefaultName(presetIndex) : preset.name;
  }

  int ensureGroupPreset(bool captureSelected = true, const std::string& preferredName = "") {
    normalizeProject(project_);
    if (!project_.groupPresets.empty()) {
      project_.focusedGroupPresetIndex = std::clamp(
        project_.focusedGroupPresetIndex, 0, static_cast<int>(project_.groupPresets.size()) - 1);
      return project_.focusedGroupPresetIndex;
    }
    GroupPreset preset;
    preset.name = trim(preferredName).empty() ? groupPresetDefaultName(0) : trim(preferredName);
    preset.slots.resize(project_.decks.size());
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      GroupSlot& slot = preset.slots[deckIndex];
      if (!captureSelected) {
        slot.bypass = true;
        continue;
      }
      const Deck& deck = project_.decks[deckIndex];
      if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
        slot.bypass = false;
        slot.cueId = deck.cues[deck.selectedIndex].id;
      } else {
        slot.bypass = true;
      }
    }
    project_.groupPresets.push_back(preset);
    project_.focusedGroupPresetIndex = 0;
    markProjectDirty();
    return 0;
  }

  bool setFocusedGroupPresetIndex(int presetIndex) {
    normalizeProject(project_);
    if (project_.groupPresets.empty()) {
      triggerToast("master cue: none");
      return false;
    }
    if (presetIndex < 0 || presetIndex >= static_cast<int>(project_.groupPresets.size())) {
      return false;
    }
    if (project_.focusedGroupPresetIndex == presetIndex) {
      triggerToast("master cue: " + groupPresetLabel(presetIndex));
      return false;
    }
    project_.focusedGroupPresetIndex = presetIndex;
    triggerToast("master cue: " + groupPresetLabel(presetIndex));
    markProjectDirty();
    return true;
  }

  void cycleFocusedGroupPreset(int direction) {
    normalizeProject(project_);
    if (project_.groupPresets.empty()) {
      return;
    }
    int count = static_cast<int>(project_.groupPresets.size());
    int next = (project_.focusedGroupPresetIndex + direction + count) % count;
    if (setFocusedGroupPresetIndex(next)) {
      playUiSound(UiSoundEffect::Navigate);
    }
  }

  int addGroupPreset(const std::string& name = "", bool captureSelected = true) {
    normalizeProject(project_);
    GroupPreset preset;
    std::string trimmedName = trim(name);
    preset.name = trimmedName.empty() ? groupPresetDefaultName(static_cast<int>(project_.groupPresets.size())) : trimmedName;
    preset.slots.resize(project_.decks.size());
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      GroupSlot& slot = preset.slots[deckIndex];
      if (!captureSelected) {
        slot.bypass = true;
        continue;
      }
      const Deck& deck = project_.decks[deckIndex];
      if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
        slot.bypass = false;
        slot.cueId = deck.cues[deck.selectedIndex].id;
      } else {
        slot.bypass = true;
      }
    }
    project_.groupPresets.push_back(preset);
    project_.focusedGroupPresetIndex = static_cast<int>(project_.groupPresets.size()) - 1;
    triggerToast("master cue add: " + groupPresetLabel(project_.focusedGroupPresetIndex));
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
    return project_.focusedGroupPresetIndex;
  }

  bool deleteFocusedGroupPreset() {
    normalizeProject(project_);
    if (project_.groupPresets.empty()) {
      triggerToast("master cue: none");
      return false;
    }
    int index = std::clamp(project_.focusedGroupPresetIndex, 0, static_cast<int>(project_.groupPresets.size()) - 1);
    std::string removedName = groupPresetLabel(index);
    project_.groupPresets.erase(project_.groupPresets.begin() + index);
    if (project_.groupPresets.empty()) {
      project_.focusedGroupPresetIndex = 0;
    } else {
      project_.focusedGroupPresetIndex = std::clamp(index, 0, static_cast<int>(project_.groupPresets.size()) - 1);
    }
    triggerToast("master cue del: " + removedName);
    playUiSound(UiSoundEffect::Delete);
    markProjectDirty();
    return true;
  }

  bool renameFocusedGroupPreset(const std::string& name) {
    GroupPreset* preset = focusedGroupPresetMutable();
    if (!preset) {
      triggerToast("master cue: none");
      return false;
    }
    std::string trimmedName = trim(name);
    if (trimmedName.empty()) {
      trimmedName = groupPresetDefaultName(project_.focusedGroupPresetIndex);
    }
    if (preset->name == trimmedName) {
      triggerToast("master cue: " + trimmedName);
      return false;
    }
    preset->name = trimmedName;
    triggerToast("master cue name: " + trimmedName);
    markProjectDirty();
    return true;
  }

  bool setFocusedGroupSlotBypass(int deckIndex, bool bypass) {
    GroupPreset* preset = focusedGroupPresetMutable();
    if (!preset) {
      triggerToast("master cue: none");
      return false;
    }
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    if (deckIndex >= static_cast<int>(preset->slots.size())) {
      preset->slots.resize(project_.decks.size());
    }
    GroupSlot& slot = preset->slots[deckIndex];
    if (slot.bypass == bypass) {
      triggerToast("master slot d" + std::to_string(deckIndex + 1) + ": " + (bypass ? "bypass" : "armed"));
      return false;
    }
    slot.bypass = bypass;
    if (bypass) {
      slot.cueId.clear();
    }
    triggerToast("master slot d" + std::to_string(deckIndex + 1) + ": " + (bypass ? "bypass" : "armed"));
    markProjectDirty();
    return true;
  }

  bool setFocusedGroupSlotFromCueIndex(int deckIndex, int cueIndex) {
    GroupPreset* preset = focusedGroupPresetMutable();
    if (!preset) {
      triggerToast("master cue: none");
      return false;
    }
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return false;
    }
    if (deckIndex >= static_cast<int>(preset->slots.size())) {
      preset->slots.resize(project_.decks.size());
    }
    GroupSlot& slot = preset->slots[deckIndex];
    std::string nextCueId = deck.cues[cueIndex].id;
    if (!slot.bypass && slot.cueId == nextCueId) {
      triggerToast("master slot d" + std::to_string(deckIndex + 1) + ": cue " + std::to_string(cueIndex + 1));
      return false;
    }
    slot.bypass = false;
    slot.cueId = nextCueId;
    triggerToast("master slot d" + std::to_string(deckIndex + 1) + ": cue " + std::to_string(cueIndex + 1));
    markProjectDirty();
    return true;
  }

  bool setFocusedGroupSlotByToken(int deckIndex, const std::string& token) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    std::string value = trim(token);
    std::string upper = toUpper(value);
    if (upper == "BYPASS" || upper == "OFF" || upper == "SKIP" || upper == "NONE" || upper == "--") {
      return setFocusedGroupSlotBypass(deckIndex, true);
    }
    if (upper == "SEL" || upper == "SELECTED") {
      return setFocusedGroupSlotFromCueIndex(deckIndex, project_.decks[deckIndex].selectedIndex);
    }
    if (upper == "ACT" || upper == "ACTIVE" || upper == "LIVE") {
      return setFocusedGroupSlotFromCueIndex(deckIndex, project_.decks[deckIndex].activeIndex);
    }
    auto cueIndex = cueIndexByToken(project_.decks[deckIndex], value);
    if (!cueIndex) {
      return false;
    }
    return setFocusedGroupSlotFromCueIndex(deckIndex, *cueIndex);
  }

  bool cycleFocusedGroupSlotCue(int deckIndex, int direction) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    ensureGroupPreset(true);
    const Deck& deck = project_.decks[deckIndex];
    if (deck.cues.empty()) {
      return setFocusedGroupSlotBypass(deckIndex, true);
    }

    int optionCount = static_cast<int>(deck.cues.size()) + 1; // 0=bypass, 1..N=cues
    int currentOption = 0;
    if (const GroupPreset* preset = focusedGroupPreset()) {
      if (deckIndex >= 0 && deckIndex < static_cast<int>(preset->slots.size())) {
        const GroupSlot& slot = preset->slots[deckIndex];
        if (!slot.bypass && !slot.cueId.empty()) {
          for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
            if (deck.cues[cueIndex].id == slot.cueId) {
              currentOption = cueIndex + 1;
              break;
            }
          }
        }
      }
    }

    int step = direction >= 0 ? 1 : -1;
    int nextOption = (currentOption + step + optionCount) % optionCount;
    if (nextOption == 0) {
      return setFocusedGroupSlotBypass(deckIndex, true);
    }
    return setFocusedGroupSlotFromCueIndex(deckIndex, nextOption - 1);
  }

  bool captureFocusedGroupPreset(bool useActiveCues) {
    GroupPreset* preset = focusedGroupPresetMutable();
    if (!preset) {
      triggerToast("master cue: none");
      return false;
    }
    if (static_cast<int>(preset->slots.size()) < static_cast<int>(project_.decks.size())) {
      preset->slots.resize(project_.decks.size());
    }
    bool any = false;
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      const Deck& deck = project_.decks[deckIndex];
      int cueIndex = useActiveCues ? deck.activeIndex : deck.selectedIndex;
      GroupSlot& slot = preset->slots[deckIndex];
      if (cueIndex >= 0 && cueIndex < static_cast<int>(deck.cues.size())) {
        slot.bypass = false;
        slot.cueId = deck.cues[cueIndex].id;
        any = true;
      } else {
        slot.bypass = true;
        slot.cueId.clear();
      }
    }
    triggerToast(any
      ? std::string("master cue capture ") + (useActiveCues ? "active" : "selected")
      : "master cue capture: empty");
    markProjectDirty();
    return any;
  }

  bool fireGroupPreset(int presetIndex, bool autoplay = true) {
    normalizeProject(project_);
    if (presetIndex < 0 || presetIndex >= static_cast<int>(project_.groupPresets.size())) {
      return false;
    }
    GroupPreset& preset = project_.groupPresets[presetIndex];
    if (static_cast<int>(preset.slots.size()) < static_cast<int>(project_.decks.size())) {
      preset.slots.resize(project_.decks.size());
    }
    int savedFocus = project_.focusedDeckIndex;
    int fired = 0;
    int missing = 0;
    int bypassed = 0;
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      const GroupSlot& slot = preset.slots[deckIndex];
      if (slot.bypass) {
        ++bypassed;
        continue;
      }
      Deck& deck = project_.decks[deckIndex];
      auto cueIndex = cueIndexById(deck, slot.cueId);
      if (!cueIndex || *cueIndex < 0 || *cueIndex >= static_cast<int>(deck.cues.size())) {
        ++missing;
        continue;
      }
      deck.selectedIndex = *cueIndex;
      project_.focusedDeckIndex = deckIndex;
      takeSelected(autoplay);
      ++fired;
    }
    project_.focusedDeckIndex = std::clamp(savedFocus, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
    onSelectionChanged();
    triggerToast("master cue fire " + std::to_string(fired)
      + " (miss " + std::to_string(missing)
      + ", bypass " + std::to_string(bypassed) + ")");
    markProjectDirty();
    return fired > 0;
  }

  bool fireFocusedGroupPreset(bool autoplay = true) {
    if (project_.groupPresets.empty()) {
      triggerToast("master cue: none");
      return false;
    }
    int index = std::clamp(project_.focusedGroupPresetIndex, 0, static_cast<int>(project_.groupPresets.size()) - 1);
    return fireGroupPreset(index, autoplay);
  }

  bool setFocusedDeckOutputRoute(int targetDeckIndex, std::optional<int> requestedLayerIndex = std::nullopt) {
    normalizeProject(project_);
    if (targetDeckIndex < 0 || targetDeckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    int focusedIndex = project_.focusedDeckIndex;
    Deck& deck = project_.decks[focusedIndex];
    int previousRoute = resolveDeckOutputHostIndex(focusedIndex);
    int previousLayer = primaryLayerIndexForDeck(focusedIndex);
    bool createdOutput = false;
    int targetOutputIndex = ensureOutputIndexForHostDeck(targetDeckIndex, &createdOutput);
    int assignmentIndex = ensurePrimaryAssignmentIndexForDeck(focusedIndex);
    LayerAssignment& primary = project_.layerAssignments[assignmentIndex];
    int previousOutputIndex = primary.outputIndex;
    primary.outputIndex = targetOutputIndex;
    primary.outputId = project_.outputs[targetOutputIndex].outputId;
    primary.enabled = true;
    if (requestedLayerIndex) {
      primary.layerIndex = std::clamp(*requestedLayerIndex, 0, 255);
    } else if (previousOutputIndex != targetOutputIndex) {
      primary.layerIndex = nextLayerIndexForOutput(targetOutputIndex, focusedIndex);
    }
    deck.outputRouteDeckIndex = targetDeckIndex;
    deck.outputLayerIndex = std::clamp(primary.layerIndex, 0, 255);
    project_.focusedOutputIndex = targetOutputIndex;
    normalizeProject(project_);
    int hostIndex = resolveDeckOutputHostIndex(focusedIndex);
    const Deck& hostDeck = project_.decks[hostIndex];
    std::string hostLabel = hostDeck.name.empty() ? deckDefaultName(hostIndex) : hostDeck.name;
    triggerToast("route: " + hostLabel + " L" + std::to_string(primaryLayerIndexForDeck(focusedIndex)));
    playUiSound(UiSoundEffect::Toggle);
    if (createdOutput) {
      rebuildOutputRuntimes();
    }
    if (previousRoute != resolveDeckOutputHostIndex(focusedIndex) ||
        previousLayer != primaryLayerIndexForDeck(focusedIndex)) {
      markProjectDirty();
    }
    return true;
  }

  bool setFocusedDeckLayerIndex(int layerIndex) {
    normalizeProject(project_);
    int clamped = std::clamp(layerIndex, 0, 255);
    int assignmentIndex = ensurePrimaryAssignmentIndexForDeck(project_.focusedDeckIndex);
    LayerAssignment& primary = project_.layerAssignments[assignmentIndex];
    if (primary.layerIndex == clamped) {
      triggerToast("layer " + std::to_string(clamped));
      return false;
    }
    primary.layerIndex = clamped;
    focusedDeckMutable().outputLayerIndex = clamped;
    triggerToast("layer " + std::to_string(clamped));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  void nudgeFocusedDeckLayerIndex(int delta) {
    setFocusedDeckLayerIndex(primaryLayerIndexForDeck(project_.focusedDeckIndex) + delta);
  }

  void addDeck() {
    normalizeProject(project_);
    int focusedOutputIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    int routeHostIndex = std::clamp(project_.outputs[focusedOutputIndex].hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    Deck deck;
    deck.name = deckDefaultName(static_cast<int>(project_.decks.size()));
    if (routeHostIndex >= 0 && routeHostIndex < static_cast<int>(project_.decks.size()) && focusedOutputIndex >= 0) {
      deck.outputDisplayIndex = project_.decks[routeHostIndex].outputDisplayIndex;
      deck.outputRouteDeckIndex = routeHostIndex;
      deck.outputLayerIndex = nextLayerIndexForOutput(focusedOutputIndex);
    }
    int newDeckIndex = static_cast<int>(project_.decks.size());
    project_.decks.push_back(deck);
    LayerAssignment assignment;
    assignment.deckIndex = newDeckIndex;
    assignment.outputIndex = focusedOutputIndex;
    assignment.outputId = project_.outputs[focusedOutputIndex].outputId;
    assignment.layerIndex = deck.outputLayerIndex;
    assignment.enabled = true;
    project_.layerAssignments.push_back(assignment);
    project_.advancedOutputMode = true;
    updateDecksPanelVisibility();
    rebuildDeckRuntimes();
    normalizeProject(project_);
    setFocusedDeckIndex(newDeckIndex);
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
    runtime.xvfbProcess.stop();
    runtime.virtualDisplayId.clear();
    runtime.browserStartPhase = BrowserStartPhase::None;
    runtime.browserCueLive = false;
    if (!runtime.browserProfileDir.empty()) {
      std::error_code error;
      fs::remove_all(runtime.browserProfileDir, error);
      runtime.browserProfileDir.clear();
    }
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

  static std::string shellQuoteSingle(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    escaped.push_back('\'');
    for (char ch : value) {
      if (ch == '\'') {
        escaped += "'\"'\"'";
      } else {
        escaped.push_back(ch);
      }
    }
    escaped.push_back('\'');
    return escaped;
  }

  double outputStreamFps(double fpsHint) const {
    if (std::isfinite(project_.outputRefreshRateHz) && project_.outputRefreshRateHz > 1.0) {
      return std::clamp(project_.outputRefreshRateHz, 1.0, 120.0);
    }
    if (std::isfinite(fpsHint) && fpsHint > 1.0) {
      return std::clamp(fpsHint, 1.0, 120.0);
    }
    return 30.0;
  }

  void pushDeckStreamAudioSamples(int deckIndex, const std::vector<std::int16_t>& samples) {
    if (deckIndex < 0 || samples.empty()) {
      return;
    }
    static constexpr size_t kMaxBufferedSamplesPerDeck = static_cast<size_t>(48000 * 2 * 10); // ~10s stereo
    std::lock_guard<std::mutex> lock(streamAudioMutex_);
    if (deckIndex >= static_cast<int>(deckStreamAudioBuffers_.size())) {
      deckStreamAudioBuffers_.resize(deckIndex + 1);
    }
    DeckStreamAudioBuffer& buffer = deckStreamAudioBuffers_[deckIndex];
    buffer.samples.insert(buffer.samples.end(), samples.begin(), samples.end());
    if (buffer.samples.size() > kMaxBufferedSamplesPerDeck) {
      size_t dropCount = buffer.samples.size() - kMaxBufferedSamplesPerDeck;
      buffer.samples.erase(buffer.samples.begin(), buffer.samples.begin() + static_cast<std::ptrdiff_t>(dropCount));
      buffer.droppedSamples += static_cast<std::uint64_t>(dropCount);
    }
  }

  void clearDeckStreamAudioBuffers() {
    std::lock_guard<std::mutex> lock(streamAudioMutex_);
    deckStreamAudioBuffers_.clear();
    deckStreamAudioBuffers_.resize(project_.decks.size());
  }

  std::vector<int> streamAudioDecksForOutput(int outputIndex) const {
    std::vector<int> deckIndices;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size()) || project_.decks.empty()) {
      return deckIndices;
    }
    int routedOutputIndex = outputIndex;
    const OutputTarget& output = project_.outputs[outputIndex];
    if (output.mirrorSourceOutputIndex >= 0 &&
        output.mirrorSourceOutputIndex < static_cast<int>(project_.outputs.size())) {
      routedOutputIndex = output.mirrorSourceOutputIndex;
    }
    for (const auto& entry : layeredDeckEntriesForOutput(routedOutputIndex)) {
      if (entry.second >= 0 && entry.second < static_cast<int>(project_.decks.size())) {
        deckIndices.push_back(entry.second);
      }
    }
    if (deckIndices.empty()) {
      int hostDeck = std::clamp(project_.outputs[routedOutputIndex].hostDeckIndex, 0,
                                static_cast<int>(project_.decks.size()) - 1);
      deckIndices.push_back(hostDeck);
    }
    std::sort(deckIndices.begin(), deckIndices.end());
    deckIndices.erase(std::unique(deckIndices.begin(), deckIndices.end()), deckIndices.end());
    return deckIndices;
  }

  std::string buildOutputStreamSpec(int outputIndex, int width, int height, double fpsHint) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return {};
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    std::string protocol = normalizeOutputStreamProtocol(output.streamProtocol);
    std::string url = trim(output.streamUrl);
    if (url.empty()) {
      url = defaultOutputStreamUrl(protocol, outputIndex);
    }
    int bitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    double fps = outputStreamFps(fpsHint);
    std::string colorSpace = normalizeOutputColorSpace(output.outputColorSpace);
    std::ostringstream spec;
    spec << protocol << '|'
         << url << '|'
         << width << 'x' << height << '|'
         << std::fixed << std::setprecision(2) << fps << '|'
         << bitrateKbps << '|'
         << colorSpace;
    return spec.str();
  }

#ifndef _WIN32
  std::string outputStreamAudioPipePath(int outputIndex) const {
    std::ostringstream path;
    path << "/tmp/deckboy_stream_audio_" << static_cast<long long>(getpid()) << "_" << outputIndex << ".fifo";
    return path.str();
  }
#endif

  std::string buildOutputStreamCommand(int outputIndex, int width, int height, double fpsHint,
                                       const std::string& audioPipePath) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return {};
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    std::string protocol = normalizeOutputStreamProtocol(output.streamProtocol);
    std::string url = trim(output.streamUrl);
    if (url.empty()) {
      url = defaultOutputStreamUrl(protocol, outputIndex);
    }
    int bitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    int bufferKbps = std::clamp(bitrateKbps * 2, 1000, 100000);
    double fps = outputStreamFps(fpsHint);
    int gop = std::max(1, static_cast<int>(std::lround(fps)));
    std::string mux = (protocol == "rtmp") ? "flv" : "mpegts";
    std::string colorSpace = normalizeOutputColorSpace(output.outputColorSpace);

    std::ostringstream command;
    command << "ffmpeg -hide_banner -loglevel error "
            << "-thread_queue_size 2048 "
            << "-f rawvideo -pix_fmt bgra "
            << "-video_size " << width << "x" << height << ' '
            << "-framerate " << std::fixed << std::setprecision(2) << fps << ' '
            << "-i - "
            << "-thread_queue_size 2048 "
            << "-f s16le -ar 48000 -ac 2 "
            << "-i " << shellQuoteSingle(audioPipePath) << ' '
            << "-map 0:v:0 -map 1:a:0 "
            << "-c:v libx264 -preset veryfast -tune zerolatency "
            << "-pix_fmt yuv420p "
            << (colorSpace == "bt709"
                  ? "-colorspace bt709 -color_primaries bt709 -color_trc bt709 "
                  : (colorSpace == "srgb"
                      ? "-colorspace bt709 -color_primaries bt709 -color_trc iec61966-2-1 "
                      : ""))
            << "-g " << gop << ' '
            << "-b:v " << bitrateKbps << "k "
            << "-maxrate " << bitrateKbps << "k "
            << "-bufsize " << bufferKbps << "k "
            << "-c:a aac -b:a 160k -ar 48000 -ac 2 "
            << "-f " << mux << ' '
            << shellQuoteSingle(url)
            << " 2>/dev/null";
    return command.str();
  }

  void stopOutputStreamRuntime(OutputRuntime& runtime) {
#ifndef _WIN32
    if (runtime.streamPipe) {
      pclose(runtime.streamPipe);
      runtime.streamPipe = nullptr;
    }
    if (runtime.streamAudioPipeFd >= 0) {
      close(runtime.streamAudioPipeFd);
      runtime.streamAudioPipeFd = -1;
    }
    if (!runtime.streamAudioPipePath.empty()) {
      unlink(runtime.streamAudioPipePath.c_str());
      runtime.streamAudioPipePath.clear();
    }
#endif
    runtime.streamSpec.clear();
    runtime.streamCommand.clear();
    runtime.streamFrameBuffer.clear();
    runtime.streamAudioReadSamplesByDeck.clear();
    runtime.streamAudioSampleRemainder = 0.0;
    runtime.streamFrameWidth = 0;
    runtime.streamFrameHeight = 0;
  }

  void stopOutputStream(int outputIndex) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      return;
    }
    stopOutputStreamRuntime(*runtime);
  }

#ifndef _WIN32
  bool prepareOutputStreamAudioPipe(OutputRuntime& runtime, int outputIndex) {
    std::string pipePath = outputStreamAudioPipePath(outputIndex);
    if (!runtime.streamAudioPipePath.empty() && runtime.streamAudioPipePath != pipePath) {
      unlink(runtime.streamAudioPipePath.c_str());
    }
    runtime.streamAudioPipePath = pipePath;
    runtime.streamAudioPipeFd = -1;
    unlink(pipePath.c_str());
    if (mkfifo(pipePath.c_str(), 0600) != 0) {
      runtime.streamAudioPipePath.clear();
      return false;
    }
    return true;
  }

  bool ensureOutputStreamAudioPipeOpen(OutputRuntime& runtime) {
    if (runtime.streamAudioPipeFd >= 0) {
      return true;
    }
    if (runtime.streamAudioPipePath.empty()) {
      return false;
    }
    int fd = open(runtime.streamAudioPipePath.c_str(), O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
      return false;
    }
    runtime.streamAudioPipeFd = fd;
    return true;
  }

  void primeOutputStreamAudioReadPositions(int outputIndex, OutputRuntime& runtime) {
    runtime.streamAudioReadSamplesByDeck.clear();
    auto deckIndices = streamAudioDecksForOutput(outputIndex);
    std::lock_guard<std::mutex> lock(streamAudioMutex_);
    for (int deckIndex : deckIndices) {
      if (deckIndex < 0 || deckIndex >= static_cast<int>(deckStreamAudioBuffers_.size())) {
        runtime.streamAudioReadSamplesByDeck[deckIndex] = 0;
        continue;
      }
      const DeckStreamAudioBuffer& buffer = deckStreamAudioBuffers_[deckIndex];
      runtime.streamAudioReadSamplesByDeck[deckIndex] = buffer.droppedSamples + static_cast<std::uint64_t>(buffer.samples.size());
    }
  }

  void primeOutputNdiAudioReadPositions(int outputIndex, OutputRuntime& runtime) {
    runtime.ndiAudioReadSamplesByDeck.clear();
    auto deckIndices = streamAudioDecksForOutput(outputIndex);
    std::lock_guard<std::mutex> lock(streamAudioMutex_);
    for (int deckIndex : deckIndices) {
      if (deckIndex < 0 || deckIndex >= static_cast<int>(deckStreamAudioBuffers_.size())) {
        runtime.ndiAudioReadSamplesByDeck[deckIndex] = 0;
        continue;
      }
      const DeckStreamAudioBuffer& buffer = deckStreamAudioBuffers_[deckIndex];
      runtime.ndiAudioReadSamplesByDeck[deckIndex] = buffer.droppedSamples + static_cast<std::uint64_t>(buffer.samples.size());
    }
  }

  std::vector<std::int16_t> collectOutputAudioFrameSamples(
      int outputIndex,
      std::map<int, std::uint64_t>& readSamplesByDeck,
      double& sampleRemainder,
      double fpsHint) {
    constexpr int kSampleRate = 48000;
    constexpr int kChannels = 2;

    double fps = outputStreamFps(fpsHint);
    double exactFrames = (static_cast<double>(kSampleRate) / std::max(1.0, fps)) + sampleRemainder;
    int sampleFrames = std::max(1, static_cast<int>(std::floor(exactFrames)));
    sampleRemainder = exactFrames - static_cast<double>(sampleFrames);
    int interleavedSamples = sampleFrames * kChannels;
    std::vector<std::int32_t> mixed(interleavedSamples, 0);

    auto deckIndices = streamAudioDecksForOutput(outputIndex);
    {
      std::lock_guard<std::mutex> lock(streamAudioMutex_);
      if (deckStreamAudioBuffers_.size() < project_.decks.size()) {
        deckStreamAudioBuffers_.resize(project_.decks.size());
      }
      for (int deckIndex : deckIndices) {
        if (deckIndex < 0 || deckIndex >= static_cast<int>(deckStreamAudioBuffers_.size())) {
          continue;
        }
        const DeckStreamAudioBuffer& buffer = deckStreamAudioBuffers_[deckIndex];
        std::uint64_t availableBegin = buffer.droppedSamples;
        std::uint64_t availableEnd = availableBegin + static_cast<std::uint64_t>(buffer.samples.size());
        auto [it, inserted] = readSamplesByDeck.try_emplace(deckIndex, availableEnd);
        std::uint64_t& readPos = it->second;
        if (inserted) {
          continue;
        }
        if (readPos < availableBegin) {
          readPos = availableBegin;
        } else if (readPos > availableEnd) {
          readPos = availableEnd;
        }
        std::uint64_t available = availableEnd - readPos;
        size_t toMix = static_cast<size_t>(std::min<std::uint64_t>(available, static_cast<std::uint64_t>(interleavedSamples)));
        size_t sourceOffset = static_cast<size_t>(readPos - availableBegin);
        for (size_t i = 0; i < toMix; ++i) {
          mixed[i] += static_cast<std::int32_t>(buffer.samples[sourceOffset + i]);
        }
        readPos += static_cast<std::uint64_t>(toMix);
      }
    }

    std::vector<std::int16_t> out(interleavedSamples, 0);
    for (int i = 0; i < interleavedSamples; ++i) {
      out[i] = static_cast<std::int16_t>(std::clamp(mixed[i], -32768, 32767));
    }
    return out;
  }

  void sendOutputStreamAudioFrame(int outputIndex, double fpsHint) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->streamPipe) {
      return;
    }
    if (!ensureOutputStreamAudioPipeOpen(*runtime)) {
      return;
    }
    std::vector<std::int16_t> out = collectOutputAudioFrameSamples(
      outputIndex,
      runtime->streamAudioReadSamplesByDeck,
      runtime->streamAudioSampleRemainder,
      fpsHint);
    if (out.empty()) {
      return;
    }
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(out.data());
    size_t bytesRemaining = out.size() * sizeof(std::int16_t);
    while (bytesRemaining > 0) {
      ssize_t written = write(runtime->streamAudioPipeFd, bytes, bytesRemaining);
      if (written > 0) {
        bytes += static_cast<size_t>(written);
        bytesRemaining -= static_cast<size_t>(written);
        continue;
      }
      if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break;
      }
      if (outputIndex == project_.focusedOutputIndex) {
        triggerToast("stream audio stopped");
      }
      stopOutputStreamRuntime(*runtime);
      break;
    }
  }
#endif

  bool ensureOutputStreamRunning(int outputIndex, int width, int height, double fpsHint) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer) {
      return false;
    }
    if (!output.streamEnabled) {
      stopOutputStreamRuntime(*runtime);
      return false;
    }
    width = std::max(1, width);
    height = std::max(1, height);
    output.streamProtocol = normalizeOutputStreamProtocol(output.streamProtocol);
    output.streamBitrateKbps = std::clamp(output.streamBitrateKbps, 500, 50000);
    if (trim(output.streamUrl).empty()) {
      output.streamUrl = defaultOutputStreamUrl(output.streamProtocol, outputIndex);
    }
    std::string desiredSpec = buildOutputStreamSpec(outputIndex, width, height, fpsHint);
    if (runtime->streamPipe && runtime->streamSpec == desiredSpec) {
#ifndef _WIN32
      ensureOutputStreamAudioPipeOpen(*runtime);
#endif
      return true;
    }

    stopOutputStreamRuntime(*runtime);
#ifdef _WIN32
    (void) desiredSpec;
    runtime->streamStartFailed = true;
    return false;
#else
    if (!prepareOutputStreamAudioPipe(*runtime, outputIndex)) {
      runtime->streamStartFailed = true;
      return false;
    }
    std::string command = buildOutputStreamCommand(outputIndex, width, height, fpsHint, runtime->streamAudioPipePath);
    if (command.empty()) {
      runtime->streamStartFailed = true;
      stopOutputStreamRuntime(*runtime);
      return false;
    }
    runtime->streamPipe = popen(command.c_str(), "w");
    if (!runtime->streamPipe) {
      stopOutputStreamRuntime(*runtime);
      if (!runtime->streamStartFailed && outputIndex == project_.focusedOutputIndex) {
        triggerToast("stream failed");
      }
      runtime->streamStartFailed = true;
      return false;
    }
    setvbuf(runtime->streamPipe, nullptr, _IONBF, 0);
    runtime->streamSpec = desiredSpec;
    runtime->streamCommand = command;
    runtime->streamFrameWidth = width;
    runtime->streamFrameHeight = height;
    runtime->streamAudioSampleRemainder = 0.0;
    primeOutputStreamAudioReadPositions(outputIndex, *runtime);
    ensureOutputStreamAudioPipeOpen(*runtime);
    runtime->streamFrameBuffer.clear();
    runtime->streamStartFailed = false;
    return true;
#endif
  }

  bool rotateCapturedFramePixels(const std::vector<std::uint8_t>& sourcePixels,
                                 int sourceW,
                                 int sourceH,
                                 int orientationDegrees,
                                 std::vector<std::uint8_t>& destPixels,
                                 int& destW,
                                 int& destH) const {
    int normalized = normalizeOutputOrientationDegrees(orientationDegrees);
    if (sourceW <= 0 || sourceH <= 0) {
      return false;
    }
    if (normalized == 0) {
      destW = sourceW;
      destH = sourceH;
      destPixels = sourcePixels;
      return true;
    }

    if (normalized == 90 || normalized == 270) {
      destW = sourceH;
      destH = sourceW;
    } else {
      destW = sourceW;
      destH = sourceH;
    }
    destPixels.assign(static_cast<size_t>(destW) * static_cast<size_t>(destH) * 4u, 0);
    if (destPixels.empty()) {
      return false;
    }

    for (int y = 0; y < sourceH; ++y) {
      for (int x = 0; x < sourceW; ++x) {
        int dx = x;
        int dy = y;
        if (normalized == 90) {
          dx = sourceH - 1 - y;
          dy = x;
        } else if (normalized == 180) {
          dx = sourceW - 1 - x;
          dy = sourceH - 1 - y;
        } else if (normalized == 270) {
          dx = y;
          dy = sourceW - 1 - x;
        }
        size_t srcOffset = (static_cast<size_t>(y) * static_cast<size_t>(sourceW) + static_cast<size_t>(x)) * 4u;
        size_t dstOffset = (static_cast<size_t>(dy) * static_cast<size_t>(destW) + static_cast<size_t>(dx)) * 4u;
        std::memcpy(destPixels.data() + dstOffset, sourcePixels.data() + srcOffset, 4u);
      }
    }
    return true;
  }

  bool captureOutputFrameForEgress(int outputIndex, OutputRuntime& runtime, const SDL_Rect& requestedRect) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size()) || !runtime.outputRenderer) {
      return false;
    }
    SDL_Rect captureRect = requestedRect;
    captureRect.w = std::max(1, captureRect.w);
    captureRect.h = std::max(1, captureRect.h);
    captureRect.x = std::max(0, captureRect.x);
    captureRect.y = std::max(0, captureRect.y);
    if (runtime.compositorTexture) {
      int texW = std::max(1, runtime.compositorWidth);
      int texH = std::max(1, runtime.compositorHeight);
      if (captureRect.x >= texW || captureRect.y >= texH) {
        return false;
      }
      captureRect.w = std::min(captureRect.w, texW - captureRect.x);
      captureRect.h = std::min(captureRect.h, texH - captureRect.y);
    } else {
      captureRect.x = 0;
      captureRect.y = 0;
    }

    int captureW = std::max(1, captureRect.w);
    int captureH = std::max(1, captureRect.h);
    size_t stride = static_cast<size_t>(captureW) * 4u;
    size_t frameBytes = stride * static_cast<size_t>(captureH);
    if (runtime.latestCapturedFrame.pixels.size() != frameBytes) {
      runtime.latestCapturedFrame.pixels.resize(frameBytes);
    }
    if (runtime.latestCapturedFrame.pixels.empty()) {
      return false;
    }

    SDL_Texture* previousTarget = SDL_GetRenderTarget(runtime.outputRenderer);
    if (runtime.compositorTexture) {
      SDL_SetRenderTarget(runtime.outputRenderer, runtime.compositorTexture);
    }
    bool ok = SDL_RenderReadPixels(
      runtime.outputRenderer,
      &captureRect,
      SDL_PIXELFORMAT_BGRA32,
      runtime.latestCapturedFrame.pixels.data(),
      static_cast<int>(stride)) == 0;
    if (runtime.compositorTexture) {
      SDL_SetRenderTarget(runtime.outputRenderer, previousTarget);
    }
    if (!ok) {
      return false;
    }

    int orientationDegrees = normalizeOutputOrientationDegrees(project_.outputs[outputIndex].outputOrientationDegrees);
    if (orientationDegrees != 0) {
      std::vector<std::uint8_t> rotatedPixels;
      int rotatedW = captureW;
      int rotatedH = captureH;
      if (!rotateCapturedFramePixels(
            runtime.latestCapturedFrame.pixels,
            captureW,
            captureH,
            orientationDegrees,
            rotatedPixels,
            rotatedW,
            rotatedH)) {
        return false;
      }
      runtime.latestCapturedFrame.pixels.swap(rotatedPixels);
      captureW = rotatedW;
      captureH = rotatedH;
    }

    Uint64 nowMs = SDL_GetTicks64();
    runtime.latestCapturedFrame.width = captureW;
    runtime.latestCapturedFrame.height = captureH;
    runtime.latestCapturedFrame.capturedAtMs = nowMs;

    int delayMs = std::clamp(project_.outputs[outputIndex].outputDelayMs, 0, 5000);
    if (delayMs <= 0) {
      runtime.delayFrames.clear();
      return true;
    }

    OutputRuntime::CapturedFrame delayedFrame;
    delayedFrame.width = captureW;
    delayedFrame.height = captureH;
    delayedFrame.capturedAtMs = nowMs;
    delayedFrame.pixels = runtime.latestCapturedFrame.pixels;
    runtime.delayFrames.push_back(std::move(delayedFrame));

    size_t maxFrames = static_cast<size_t>(std::clamp(delayMs / 8 + 12, 16, 900));
    while (runtime.delayFrames.size() > maxFrames) {
      runtime.delayFrames.pop_front();
    }
    while (runtime.delayFrames.size() > 2 &&
           nowMs > runtime.delayFrames.front().capturedAtMs + static_cast<Uint64>(delayMs + 2500)) {
      runtime.delayFrames.pop_front();
    }
    return true;
  }

  const OutputRuntime::CapturedFrame* outputFrameForEgress(int outputIndex, OutputRuntime& runtime) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return nullptr;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    int delayMs = std::clamp(output.outputDelayMs, 0, 5000);
    if (delayMs > 0) {
      Uint64 nowMs = SDL_GetTicks64();
      const OutputRuntime::CapturedFrame* selected = nullptr;
      for (const auto& frame : runtime.delayFrames) {
        if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
          continue;
        }
        if (nowMs >= frame.capturedAtMs + static_cast<Uint64>(delayMs)) {
          selected = &frame;
        }
      }
      if (selected) {
        return selected;
      }
    }
    if (runtime.latestCapturedFrame.width > 0 &&
        runtime.latestCapturedFrame.height > 0 &&
        !runtime.latestCapturedFrame.pixels.empty()) {
      return &runtime.latestCapturedFrame;
    }
    return nullptr;
  }

  void sendOutputStreamFrame(int outputIndex, int width, int height, double fpsHint) {
#ifdef _WIN32
    (void) outputIndex;
    (void) width;
    (void) height;
    (void) fpsHint;
#else
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      return;
    }
    const OutputRuntime::CapturedFrame* frame = outputFrameForEgress(outputIndex, *runtime);
    if (!frame || frame->width <= 0 || frame->height <= 0 || frame->pixels.empty()) {
      return;
    }
    if (!ensureOutputStreamRunning(outputIndex, frame->width, frame->height, fpsHint)) {
      return;
    }
    runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->streamPipe) {
      return;
    }
    width = frame->width;
    height = frame->height;
    size_t stride = static_cast<size_t>(width) * 4u;
    size_t frameBytes = stride * static_cast<size_t>(height);
    if (runtime->streamFrameBuffer.size() != frameBytes) {
      runtime->streamFrameBuffer.resize(frameBytes);
    }
    if (runtime->streamFrameBuffer.empty()) {
      return;
    }
    std::memcpy(runtime->streamFrameBuffer.data(), frame->pixels.data(), frameBytes);
    size_t written = fwrite(runtime->streamFrameBuffer.data(), 1, frameBytes, runtime->streamPipe);
    if (written != frameBytes) {
      stopOutputStreamRuntime(*runtime);
      if (outputIndex == project_.focusedOutputIndex) {
        triggerToast("stream stopped");
      }
      return;
    }
    sendOutputStreamAudioFrame(outputIndex, fpsHint);
#endif
  }

  void destroyOutputRuntime(OutputRuntime& runtime) {
    stopOutputStreamRuntime(runtime);
#if defined(PLAYBOY_HAS_NDI_SDK)
    if (runtime.ndiKeySender && ndiApi_.sendDestroyFn) {
      ndiApi_.sendDestroyFn(runtime.ndiKeySender);
      runtime.ndiKeySender = nullptr;
    }
    runtime.ndiKeySenderName.clear();
    runtime.ndiKeyFrameBuffer.clear();
    if (runtime.ndiSender && ndiApi_.sendDestroyFn) {
      ndiApi_.sendDestroyFn(runtime.ndiSender);
      runtime.ndiSender = nullptr;
    }
    runtime.ndiSenderName.clear();
    runtime.ndiFrameBuffer.clear();
#endif
    runtime.ndiAudioReadSamplesByDeck.clear();
    runtime.ndiAudioSampleRemainder = 0.0;
    runtime.latestCapturedFrame = {};
    runtime.delayFrames.clear();
    for (auto& [sourceDeckIndex, texture] : runtime.layerBridgeTextures) {
      (void) sourceDeckIndex;
      if (texture) {
        SDL_DestroyTexture(texture);
      }
    }
    runtime.layerBridgeTextures.clear();
    runtime.layerBridgeTextureWidths.clear();
    runtime.layerBridgeTextureHeights.clear();
    runtime.layerBridgeScratchPixels.clear();
    if (runtime.compositorTexture) {
      SDL_DestroyTexture(runtime.compositorTexture);
      runtime.compositorTexture = nullptr;
    }
    runtime.compositorWidth = 0;
    runtime.compositorHeight = 0;
    runtime.compositorFormat = SDL_PIXELFORMAT_UNKNOWN;
    runtime.compositorBitDepth = 8;
    if (runtime.outputRenderer) {
      SDL_DestroyRenderer(runtime.outputRenderer);
      runtime.outputRenderer = nullptr;
    }
    if (runtime.outputWindow) {
      SDL_DestroyWindow(runtime.outputWindow);
      runtime.outputWindow = nullptr;
    }
    runtime.recoveryPausedByEscape = false;
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
    runtime->mediaEngine = std::make_unique<MediaEngine>(
      runtime->outputRenderer,
      runtime->audioDevice,
      [this, deckIndex](const std::vector<std::int16_t>& samples) {
        pushDeckStreamAudioSamples(deckIndex, samples);
        // Capture samples for VU meter (only from focused deck)
        if (deckIndex == project_.focusedDeckIndex) {
          std::lock_guard<std::mutex> lock(vuSamplesMutex_);
          vuSamples_ = samples;
        }
      }
    );
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

  void applyOutputNdiSettings(int outputIndex, bool withToast) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime) {
      return;
    }

#if defined(PLAYBOY_HAS_NDI_SDK)
    auto clearFillSender = [&]() {
      if (runtime->ndiSender && ndiApi_.sendDestroyFn) {
        ndiApi_.sendDestroyFn(runtime->ndiSender);
      }
      runtime->ndiSender = nullptr;
      runtime->ndiSenderName.clear();
      runtime->ndiFrameBuffer.clear();
      runtime->ndiAudioReadSamplesByDeck.clear();
      runtime->ndiAudioSampleRemainder = 0.0;
    };
    auto clearKeySender = [&]() {
      if (runtime->ndiKeySender && ndiApi_.sendDestroyFn) {
        ndiApi_.sendDestroyFn(runtime->ndiKeySender);
      }
      runtime->ndiKeySender = nullptr;
      runtime->ndiKeySenderName.clear();
      runtime->ndiKeyFrameBuffer.clear();
    };
    auto clearSenders = [&]() {
      clearFillSender();
      clearKeySender();
    };

    if (!output.ndiEnabled) {
      clearSenders();
      output.ndiKeyEnabled = false;
      if (withToast) {
        triggerToast("ndi off");
      }
      return;
    }

    std::string loadError;
    if (!ensureNdiRuntimeReady(&loadError)) {
      output.ndiEnabled = false;
      output.ndiKeyEnabled = false;
      clearSenders();
      if (withToast) {
        triggerToast("ndi unavailable");
      }
      return;
    }

    if (trim(output.ndiSourceName).empty()) {
      output.ndiSourceName = defaultOutputNdiSourceName(output, outputIndex);
    }
    if (trim(output.ndiKeySourceName).empty()) {
      output.ndiKeySourceName = defaultOutputNdiKeySourceName(output, outputIndex);
    }

    clearSenders();

    NDIlib_send_create_t fillCreate {};
    fillCreate.p_ndi_name = output.ndiSourceName.c_str();
    fillCreate.p_groups = nullptr;
    fillCreate.clock_video = false;
    fillCreate.clock_audio = false;
    runtime->ndiSender = ndiApi_.sendCreateFn ? ndiApi_.sendCreateFn(&fillCreate) : nullptr;
    if (!runtime->ndiSender) {
      output.ndiEnabled = false;
      output.ndiKeyEnabled = false;
      if (withToast) {
        triggerToast("ndi sender failed");
      }
      return;
    }
    runtime->ndiSenderName = output.ndiSourceName;
    primeOutputNdiAudioReadPositions(outputIndex, *runtime);
    runtime->ndiAudioSampleRemainder = 0.0;

    if (output.ndiKeyEnabled) {
      NDIlib_send_create_t keyCreate {};
      keyCreate.p_ndi_name = output.ndiKeySourceName.c_str();
      keyCreate.p_groups = nullptr;
      keyCreate.clock_video = false;
      keyCreate.clock_audio = false;
      runtime->ndiKeySender = ndiApi_.sendCreateFn ? ndiApi_.sendCreateFn(&keyCreate) : nullptr;
      if (!runtime->ndiKeySender) {
        output.ndiKeyEnabled = false;
      } else {
        runtime->ndiKeySenderName = output.ndiKeySourceName;
      }
    }

    if (withToast) {
      triggerToast("ndi: " + currentNdiOutputLabel());
    }
#else
    (void) output;
    (void) runtime;
    if (withToast) {
      triggerToast("ndi unsupported build");
    }
#endif
  }

  void setFocusedOutputNdiEnabled(bool enabled) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return;
    }
    OutputTarget& output = focusedOutputMutable();
    if (output.ndiEnabled == enabled) {
      return;
    }
    output.ndiEnabled = enabled;
    if (!output.ndiEnabled) {
      output.ndiKeyEnabled = false;
    }
    applyOutputNdiSettings(project_.focusedOutputIndex, true);
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleFocusedOutputNdi() {
    setFocusedOutputNdiEnabled(!focusedOutput().ndiEnabled);
  }

  void setFocusedOutputNdiName(const std::string& requestedName) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return;
    }
    OutputTarget& output = focusedOutputMutable();
    std::string normalized = trim(requestedName);
    if (normalized.empty()) {
      normalized = defaultOutputNdiSourceName(output, project_.focusedOutputIndex);
    }
    if (output.ndiSourceName == normalized) {
      return;
    }
    output.ndiSourceName = normalized;
    if (output.ndiEnabled) {
      applyOutputNdiSettings(project_.focusedOutputIndex, true);
    } else {
      triggerToast("ndi name: " + output.ndiSourceName);
    }
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setFocusedOutputNdiKeyEnabled(bool enabled) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return;
    }
    OutputTarget& output = focusedOutputMutable();
    if (output.ndiKeyEnabled == enabled && (!enabled || output.ndiEnabled)) {
      return;
    }
    if (enabled) {
      output.ndiEnabled = true;
      if (trim(output.ndiKeySourceName).empty()) {
        output.ndiKeySourceName = defaultOutputNdiKeySourceName(output, project_.focusedOutputIndex);
      }
    }
    output.ndiKeyEnabled = enabled;
    applyOutputNdiSettings(project_.focusedOutputIndex, true);
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleFocusedOutputNdiKey() {
    setFocusedOutputNdiKeyEnabled(!focusedOutput().ndiKeyEnabled);
  }

  void setFocusedOutputNdiKeyName(const std::string& requestedName) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return;
    }
    OutputTarget& output = focusedOutputMutable();
    std::string normalized = trim(requestedName);
    if (normalized.empty()) {
      normalized = defaultOutputNdiKeySourceName(output, project_.focusedOutputIndex);
    }
    if (output.ndiKeySourceName == normalized) {
      return;
    }
    output.ndiKeySourceName = normalized;
    if (output.ndiEnabled && output.ndiKeyEnabled) {
      applyOutputNdiSettings(project_.focusedOutputIndex, true);
    } else {
      triggerToast("ndi key name: " + output.ndiKeySourceName);
    }
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void sendOutputNdiAudioFrame(int outputIndex, double fpsHint) {
#if defined(PLAYBOY_HAS_NDI_SDK)
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->ndiSender || !ndiApi_.sendAudioInterleaved16sFn) {
      return;
    }
    std::vector<std::int16_t> samples = collectOutputAudioFrameSamples(
      outputIndex,
      runtime->ndiAudioReadSamplesByDeck,
      runtime->ndiAudioSampleRemainder,
      fpsHint);
    if (samples.empty()) {
      return;
    }
    constexpr int kChannels = 2;
    int sampleCount = static_cast<int>(samples.size() / static_cast<size_t>(kChannels));
    if (sampleCount <= 0) {
      return;
    }
    NDIlib_audio_frame_interleaved_16s_t frame {};
    frame.sample_rate = kAudioRate;
    frame.no_channels = kChannels;
    frame.no_samples = sampleCount;
    frame.timecode = NDIlib_send_timecode_synthesize;
    frame.reference_level = 0;
    frame.p_data = samples.data();
    ndiApi_.sendAudioInterleaved16sFn(runtime->ndiSender, &frame);
#else
    (void) outputIndex;
    (void) fpsHint;
#endif
  }

  void sendOutputNdiFrame(int outputIndex, OutputRuntime& outputRuntime, int width, int height, double fpsHint) {
#if defined(PLAYBOY_HAS_NDI_SDK)
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size()) || !outputRuntime.outputRenderer) {
      return;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    if (!output.ndiEnabled) {
      return;
    }
    if (!outputRuntime.ndiSender || (output.ndiKeyEnabled && !outputRuntime.ndiKeySender)) {
      applyOutputNdiSettings(outputIndex, false);
      if (!outputRuntime.ndiSender || (output.ndiKeyEnabled && !outputRuntime.ndiKeySender)) {
        return;
      }
    }
    const OutputRuntime::CapturedFrame* frameCapture = outputFrameForEgress(outputIndex, outputRuntime);
    if (!frameCapture || frameCapture->width <= 0 || frameCapture->height <= 0 || frameCapture->pixels.empty()) {
      return;
    }
    width = frameCapture->width;
    height = frameCapture->height;

    size_t stride = static_cast<size_t>(width) * 4u;
    size_t frameBytes = stride * static_cast<size_t>(height);
    if (outputRuntime.ndiFrameBuffer.size() != frameBytes) {
      outputRuntime.ndiFrameBuffer.resize(frameBytes);
    }
    if (outputRuntime.ndiFrameBuffer.empty()) {
      return;
    }

    std::memcpy(outputRuntime.ndiFrameBuffer.data(), frameCapture->pixels.data(), frameBytes);

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
    frame.p_data = outputRuntime.ndiFrameBuffer.data();
    frame.line_stride_in_bytes = static_cast<int>(stride);
    frame.p_metadata = nullptr;
    frame.timestamp = 0;
    ndiApi_.sendVideoFn(outputRuntime.ndiSender, &frame);

    if (output.ndiKeyEnabled && outputRuntime.ndiKeySender) {
      if (outputRuntime.ndiKeyFrameBuffer.size() != frameBytes) {
        outputRuntime.ndiKeyFrameBuffer.resize(frameBytes);
      }
      if (!outputRuntime.ndiKeyFrameBuffer.empty()) {
        for (size_t i = 0; i + 3 < outputRuntime.ndiFrameBuffer.size(); i += 4) {
          Uint8 a = outputRuntime.ndiFrameBuffer[i + 3];
          outputRuntime.ndiKeyFrameBuffer[i + 0] = a;
          outputRuntime.ndiKeyFrameBuffer[i + 1] = a;
          outputRuntime.ndiKeyFrameBuffer[i + 2] = a;
          outputRuntime.ndiKeyFrameBuffer[i + 3] = 255;
        }
        NDIlib_video_frame_v2_t keyFrame {};
        keyFrame.xres = width;
        keyFrame.yres = height;
        keyFrame.FourCC = NDIlib_FourCC_video_type_BGRA;
        keyFrame.frame_rate_N = frameRateN;
        keyFrame.frame_rate_D = frameRateD;
        keyFrame.picture_aspect_ratio = frame.picture_aspect_ratio;
        keyFrame.frame_format_type = NDIlib_frame_format_type_progressive;
        keyFrame.timecode = NDIlib_send_timecode_synthesize;
        keyFrame.p_data = outputRuntime.ndiKeyFrameBuffer.data();
        keyFrame.line_stride_in_bytes = static_cast<int>(stride);
        keyFrame.p_metadata = nullptr;
        keyFrame.timestamp = 0;
        ndiApi_.sendVideoFn(outputRuntime.ndiKeySender, &keyFrame);
      }
    }
    sendOutputNdiAudioFrame(outputIndex, fpsHint);
#else
    (void) outputIndex;
    (void) outputRuntime;
    (void) width;
    (void) height;
    (void) fpsHint;
#endif
  }

  int ndiConnectionCountForOutput(int outputIndex) const {
#if defined(PLAYBOY_HAS_NDI_SDK)
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->ndiSender || !ndiApi_.sendConnectionsFn) {
      return 0;
    }
    return std::max(0, ndiApi_.sendConnectionsFn(runtime->ndiSender, 0));
#else
    (void) outputIndex;
    return 0;
#endif
  }

  int ndiKeyConnectionCountForOutput(int outputIndex) const {
#if defined(PLAYBOY_HAS_NDI_SDK)
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->ndiKeySender || !ndiApi_.sendConnectionsFn) {
      return 0;
    }
    return std::max(0, ndiApi_.sendConnectionsFn(runtime->ndiKeySender, 0));
#else
    (void) outputIndex;
    return 0;
#endif
  }

  std::pair<int, int> fixedOutputRenderSize() const {
    int w = std::clamp(project_.outputRenderWidth, 320, 7680);
    int h = std::clamp(project_.outputRenderHeight, 180, 4320);
    return {w, h};
  }

  std::pair<int, int> displayNativeRenderSize(int displayIndex) const {
    int displayCount = SDL_GetNumVideoDisplays();
    if (displayCount <= 0) {
      return fixedOutputRenderSize();
    }
    int normalizedIndex = std::clamp(displayIndex, 0, displayCount - 1);

    SDL_DisplayMode desktopMode {};
    if (SDL_GetDesktopDisplayMode(normalizedIndex, &desktopMode) == 0 &&
        desktopMode.w > 0 && desktopMode.h > 0) {
      return {desktopMode.w, desktopMode.h};
    }

    SDL_Rect bounds {};
    if (SDL_GetDisplayBounds(normalizedIndex, &bounds) == 0 &&
        bounds.w > 0 && bounds.h > 0) {
      return {bounds.w, bounds.h};
    }
    return fixedOutputRenderSize();
  }

  int outputDisplayIndex(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return 0;
    }
    return std::max(0, project_.outputs[outputIndex].displayIndex);
  }

  std::pair<int, int> outputRenderSizeForOutput(int outputIndex) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return fixedOutputRenderSize();
    }
    if (project_.outputFollowDisplay) {
      return displayNativeRenderSize(outputDisplayIndex(outputIndex));
    }
    return fixedOutputRenderSize();
  }

  std::pair<int, int> outputRenderSizeForDeck(int deckIndex) const {
    auto outputIndex = primaryOutputIndexForDeck(deckIndex);
    if (!outputIndex) {
      return fixedOutputRenderSize();
    }
    return outputRenderSizeForOutput(*outputIndex);
  }

  std::string outputResolutionLabelForOutput(int outputIndex) const {
    auto [w, h] = outputRenderSizeForOutput(outputIndex);
    return std::to_string(w) + "x" + std::to_string(h);
  }

  std::string outputResolutionLabel(int deckIndex) const {
    auto outputIndex = primaryOutputIndexForDeck(deckIndex);
    if (!outputIndex) {
      return outputResolutionLabelForOutput(project_.focusedOutputIndex);
    }
    return outputResolutionLabelForOutput(*outputIndex);
  }

  std::string outputSizingModeLabel() const {
    return project_.outputFollowDisplay ? "display native" : "fixed";
  }

  std::string formatRefreshRateLabel(double hz) const {
    if (!std::isfinite(hz) || hz <= 0.0) {
      return "auto";
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(hz < 100.0 ? 2 : 1) << hz;
    std::string text = ss.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text + " Hz";
  }

  std::string outputRefreshRateLabel() const {
    return formatRefreshRateLabel(project_.outputRefreshRateHz);
  }

  static bool isTenBitFormat(Uint32 format) {
    return format == SDL_PIXELFORMAT_ARGB2101010;
  }

  static int normalizeOutputBitDepthMode(int mode) {
    if (mode == 8 || mode == 10) {
      return mode;
    }
    return 0;
  }

  std::string outputBitDepthModeLabel() const {
    int mode = normalizeOutputBitDepthMode(project_.outputBitDepth);
    if (mode == 8) return "8-bit";
    if (mode == 10) return "10-bit";
    return "auto";
  }

  std::string outputBitDepthActiveLabelForOutput(int outputIndex) const {
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer) {
      return "n/a";
    }
    return std::to_string(runtime->compositorBitDepth) + "-bit";
  }

  std::string outputBitDepthActiveLabel(int deckIndex) const {
    auto outputIndex = primaryOutputIndexForDeck(deckIndex);
    return outputBitDepthActiveLabelForOutput(outputIndex ? *outputIndex : project_.focusedOutputIndex);
  }

  static bool rendererSupportsTextureFormat(SDL_Renderer* renderer, Uint32 format) {
    if (!renderer || format == SDL_PIXELFORMAT_UNKNOWN) {
      return false;
    }
    SDL_RendererInfo info {};
    if (SDL_GetRendererInfo(renderer, &info) != 0) {
      return false;
    }
    for (Uint32 i = 0; i < info.num_texture_formats; ++i) {
      if (info.texture_formats[i] == format) {
        return true;
      }
    }
    return false;
  }

  Uint32 preferredCompositorFormat(SDL_Renderer* renderer) const {
    if (!renderer) {
      return SDL_PIXELFORMAT_RGBA32;
    }
    int mode = normalizeOutputBitDepthMode(project_.outputBitDepth);
    bool supportsArgb2101010 = rendererSupportsTextureFormat(renderer, SDL_PIXELFORMAT_ARGB2101010);
    if (mode == 10) {
      if (supportsArgb2101010) return SDL_PIXELFORMAT_ARGB2101010;
      return SDL_PIXELFORMAT_RGBA32;
    }
    if (mode == 0) {
      if (supportsArgb2101010) return SDL_PIXELFORMAT_ARGB2101010;
    }
    return SDL_PIXELFORMAT_RGBA32;
  }

  bool configureOutputCompositor(int outputIndex, int width = -1, int height = -1) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer) {
      return false;
    }
    int targetW = width;
    int targetH = height;
    if (targetW <= 0 || targetH <= 0) {
      int windowW = 0;
      int windowH = 0;
      if (runtime->outputWindow) {
        SDL_GetWindowSize(runtime->outputWindow, &windowW, &windowH);
      }
      if (project_.outputCanvasEnabled) {
        auto [canvasW, canvasH] = outputCanvasRenderSize();
        targetW = canvasW;
        targetH = canvasH;
      } else {
        if (windowW <= 0 || windowH <= 0) {
          auto [rasterW, rasterH] = outputRenderSizeForOutput(outputIndex);
          windowW = rasterW;
          windowH = rasterH;
        }
        targetW = windowW;
        targetH = windowH;
      }
    }
    targetW = std::max(1, targetW);
    targetH = std::max(1, targetH);

    Uint32 format = preferredCompositorFormat(runtime->outputRenderer);
    SDL_Texture* compositor = SDL_CreateTexture(
      runtime->outputRenderer,
      format,
      SDL_TEXTUREACCESS_TARGET,
      targetW,
      targetH
    );
    if (!compositor && format != SDL_PIXELFORMAT_RGBA32) {
      format = SDL_PIXELFORMAT_RGBA32;
      compositor = SDL_CreateTexture(
        runtime->outputRenderer,
        format,
        SDL_TEXTUREACCESS_TARGET,
        targetW,
        targetH
      );
    }
    if (!compositor) {
      return false;
    }
    SDL_SetTextureBlendMode(compositor, SDL_BLENDMODE_BLEND);

    if (runtime->compositorTexture) {
      SDL_DestroyTexture(runtime->compositorTexture);
    }
    runtime->compositorTexture = compositor;
    runtime->compositorWidth = targetW;
    runtime->compositorHeight = targetH;
    runtime->compositorFormat = format;
    runtime->compositorBitDepth = isTenBitFormat(format) ? 10 : 8;
    runtime->delayFrames.clear();
    runtime->latestCapturedFrame = {};
    return true;
  }

  void applyOutputBitDepthAllOutputs() {
    for (int outputIndex = 0; outputIndex < static_cast<int>(outputRuntimes_.size()); ++outputIndex) {
      configureOutputCompositor(outputIndex);
    }
  }

  void setOutputBitDepthMode(int mode) {
    int normalized = normalizeOutputBitDepthMode(mode);
    bool changed = normalized != normalizeOutputBitDepthMode(project_.outputBitDepth);
    project_.outputBitDepth = normalized;
    applyOutputBitDepthAllOutputs();
    triggerToast("video depth: " + outputBitDepthModeLabel() + " (" + outputBitDepthActiveLabelForOutput(project_.focusedOutputIndex) + ")");
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  std::pair<int, int> outputCanvasRenderSize() const {
    int w = std::clamp(project_.outputCanvasWidth, 320, 16384);
    int h = std::clamp(project_.outputCanvasHeight, 180, 16384);
    return {w, h};
  }

  void clampDeckCanvasViewToWindow(int deckIndex, int windowW, int windowH) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    if (!project_.outputCanvasEnabled) {
      deck.canvasViewX = 0;
      deck.canvasViewY = 0;
      return;
    }
    auto [canvasW, canvasH] = outputCanvasRenderSize();
    int maxX = std::max(0, canvasW - std::max(1, windowW));
    int maxY = std::max(0, canvasH - std::max(1, windowH));
    deck.canvasViewX = std::clamp(deck.canvasViewX, 0, maxX);
    deck.canvasViewY = std::clamp(deck.canvasViewY, 0, maxY);
  }

  void setOutputCanvasMode(bool enabled, int width = 0, int height = 0) {
    bool changed = false;
    if (enabled) {
      int targetW = width;
      int targetH = height;
      if (targetW <= 0 || targetH <= 0) {
        auto [nativeW, nativeH] = displayNativeRenderSize(outputDisplayIndex(project_.focusedOutputIndex));
        targetW = std::max(nativeW * 2, nativeW);
        targetH = nativeH;
      }
      targetW = std::clamp(targetW, 320, 16384);
      targetH = std::clamp(targetH, 180, 16384);
      changed = !project_.outputCanvasEnabled
        || project_.outputCanvasWidth != targetW
        || project_.outputCanvasHeight != targetH;
      project_.outputCanvasEnabled = true;
      project_.outputCanvasWidth = targetW;
      project_.outputCanvasHeight = targetH;
    } else {
      changed = project_.outputCanvasEnabled;
      project_.outputCanvasEnabled = false;
      for (auto& deck : project_.decks) {
        deck.canvasViewX = 0;
        deck.canvasViewY = 0;
      }
    }

    applyOutputBitDepthAllOutputs();
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      OutputRuntime* runtime = runtimeForOutput(outputIndex);
      if (!runtime || !runtime->outputWindow) {
        continue;
      }
      int hostDeckIndex = std::clamp(project_.outputs[outputIndex].hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
      int ww = 0;
      int wh = 0;
      SDL_GetWindowSize(runtime->outputWindow, &ww, &wh);
      clampDeckCanvasViewToWindow(hostDeckIndex, ww, wh);
    }

    std::string label = project_.outputCanvasEnabled
      ? ("canvas " + std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
      : "canvas off";
    triggerToast("video: " + label);
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  void setFocusedDeckCanvasView(int x, int y) {
    if (!project_.outputCanvasEnabled) {
      triggerToast("canvas mode off");
      return;
    }
    Deck& deck = focusedDeckMutable();
    deck.canvasViewX = std::max(0, x);
    deck.canvasViewY = std::max(0, y);
    int outputIndex = primaryOutputIndexForDeck(project_.focusedDeckIndex).value_or(project_.focusedOutputIndex);
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (runtime && runtime->outputWindow) {
      int ww = 0;
      int wh = 0;
      SDL_GetWindowSize(runtime->outputWindow, &ww, &wh);
      clampDeckCanvasViewToWindow(project_.focusedDeckIndex, ww, wh);
    }
    triggerToast("view: " + std::to_string(deck.canvasViewX) + "," + std::to_string(deck.canvasViewY));
    markProjectDirty();
  }

  void nudgeFocusedDeckCanvasView(int dx, int dy) {
    const Deck& deck = focusedDeck();
    setFocusedDeckCanvasView(deck.canvasViewX + dx, deck.canvasViewY + dy);
  }

  void setFocusedDeckWarpEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.warpEnabled == enabled) {
      return;
    }
    deck.warpEnabled = enabled;
    triggerToast(deck.warpEnabled ? "warp on" : "warp off");
    markProjectDirty();
  }

  void toggleFocusedDeckWarpEnabled() {
    setFocusedDeckWarpEnabled(!focusedDeck().warpEnabled);
  }

  void resetFocusedDeckWarp() {
    Deck& deck = focusedDeckMutable();
    deck.warpTopLeftX = 0.0f;
    deck.warpTopLeftY = 0.0f;
    deck.warpTopRightX = 0.0f;
    deck.warpTopRightY = 0.0f;
    deck.warpBottomRightX = 0.0f;
    deck.warpBottomRightY = 0.0f;
    deck.warpBottomLeftX = 0.0f;
    deck.warpBottomLeftY = 0.0f;
    deck.edgeBlendLeft = 0.0f;
    deck.edgeBlendRight = 0.0f;
    deck.edgeBlendTop = 0.0f;
    deck.edgeBlendBottom = 0.0f;
    triggerToast("warp/blend reset");
    markProjectDirty();
  }

  void adjustFocusedDeckWarpCorner(const std::string& cornerToken, float dx, float dy) {
    Deck& deck = focusedDeckMutable();
    std::string corner = toUpper(cornerToken);
    if (corner == "TL" || corner == "TOPLEFT") {
      deck.warpTopLeftX += dx;
      deck.warpTopLeftY += dy;
    } else if (corner == "TR" || corner == "TOPRIGHT") {
      deck.warpTopRightX += dx;
      deck.warpTopRightY += dy;
    } else if (corner == "BR" || corner == "BOTTOMRIGHT") {
      deck.warpBottomRightX += dx;
      deck.warpBottomRightY += dy;
    } else if (corner == "BL" || corner == "BOTTOMLEFT") {
      deck.warpBottomLeftX += dx;
      deck.warpBottomLeftY += dy;
    } else {
      return;
    }
    normalizeDeck(deck, project_.focusedDeckIndex);
    triggerToast("warp " + corner + " " + std::to_string(static_cast<int>(std::lround(dx))) + "," + std::to_string(static_cast<int>(std::lround(dy))));
    markProjectDirty();
  }

  void setFocusedDeckEdgeBlend(const std::string& edgeToken, float value) {
    Deck& deck = focusedDeckMutable();
    float v = std::clamp(value, 0.0f, 0.49f);
    std::string edge = toUpper(edgeToken);
    if (edge == "L" || edge == "LEFT") {
      deck.edgeBlendLeft = v;
    } else if (edge == "R" || edge == "RIGHT") {
      deck.edgeBlendRight = v;
    } else if (edge == "T" || edge == "TOP") {
      deck.edgeBlendTop = v;
    } else if (edge == "B" || edge == "BOTTOM") {
      deck.edgeBlendBottom = v;
    } else {
      return;
    }
    normalizeDeck(deck, project_.focusedDeckIndex);
    triggerToast("blend " + edge + " " + std::to_string(static_cast<int>(std::lround(v * 100.0f))) + "%");
    markProjectDirty();
  }

  std::vector<int> refreshChoicesForOutput(int outputIndex) const {
    std::vector<int> refreshes;
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return refreshes;
    }
    int displayCount = SDL_GetNumVideoDisplays();
    if (displayCount <= 0) {
      return refreshes;
    }
    int displayIndex = std::clamp(outputDisplayIndex(outputIndex), 0, displayCount - 1);
    auto [targetW, targetH] = outputRenderSizeForOutput(outputIndex);

    int modeCount = SDL_GetNumDisplayModes(displayIndex);
    for (int modeIndex = 0; modeIndex < modeCount; ++modeIndex) {
      SDL_DisplayMode mode {};
      if (SDL_GetDisplayMode(displayIndex, modeIndex, &mode) != 0) {
        continue;
      }
      if (mode.w != targetW || mode.h != targetH) {
        continue;
      }
      if (mode.refresh_rate > 0) {
        refreshes.push_back(mode.refresh_rate);
      }
    }
    std::sort(refreshes.begin(), refreshes.end());
    refreshes.erase(std::unique(refreshes.begin(), refreshes.end()), refreshes.end());
    return refreshes;
  }

  bool selectDisplayModeForOutput(int outputIndex, SDL_DisplayMode& selectedMode) const {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    int displayCount = SDL_GetNumVideoDisplays();
    if (displayCount <= 0) {
      return false;
    }
    int displayIndex = std::clamp(outputDisplayIndex(outputIndex), 0, displayCount - 1);
    auto [targetW, targetH] = outputRenderSizeForOutput(outputIndex);
    double targetHz = project_.outputRefreshRateHz;

    SDL_DisplayMode desktopMode {};
    bool hasDesktop = SDL_GetDesktopDisplayMode(displayIndex, &desktopMode) == 0;
    if (targetHz <= 0.0 && hasDesktop && desktopMode.w == targetW && desktopMode.h == targetH) {
      selectedMode = desktopMode;
      return true;
    }

    int modeCount = SDL_GetNumDisplayModes(displayIndex);
    bool found = false;
    SDL_DisplayMode best {};
    double bestScore = 1e9;

    for (int modeIndex = 0; modeIndex < modeCount; ++modeIndex) {
      SDL_DisplayMode mode {};
      if (SDL_GetDisplayMode(displayIndex, modeIndex, &mode) != 0) {
        continue;
      }
      if (mode.w != targetW || mode.h != targetH) {
        continue;
      }

      double hz = mode.refresh_rate > 0 ? static_cast<double>(mode.refresh_rate) : 60.0;
      double score = 0.0;
      if (targetHz > 0.0) {
        score = std::abs(hz - targetHz);
      } else {
        // Auto: prefer desktop refresh if available, then highest refresh.
        if (hasDesktop && desktopMode.w == targetW && desktopMode.h == targetH && desktopMode.refresh_rate > 0) {
          score = std::abs(hz - static_cast<double>(desktopMode.refresh_rate));
        } else {
          score = -hz;
        }
      }

      if (!found || score < bestScore) {
        found = true;
        best = mode;
        bestScore = score;
      }
    }

    if (!found) {
      return false;
    }
    selectedMode = best;
    return true;
  }

  bool enableOutputFullscreen(int outputIndex, bool withToast) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputWindow) {
      return false;
    }
    runtime->recoveryPausedByEscape = false;

    SDL_DisplayMode selectedMode {};
    if (selectDisplayModeForOutput(outputIndex, selectedMode)) {
      SDL_SetWindowDisplayMode(runtime->outputWindow, &selectedMode);
      if (SDL_SetWindowFullscreen(runtime->outputWindow, SDL_WINDOW_FULLSCREEN) == 0) {
        if (withToast) {
          triggerToast("big screen @" + formatRefreshRateLabel(selectedMode.refresh_rate));
        }
        return true;
      }
    }

    if (SDL_SetWindowFullscreen(runtime->outputWindow, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0) {
      if (withToast) {
        triggerToast("big screen");
      }
      return true;
    }
    return false;
  }

  bool createDeckRuntime(int deckIndex) {
    Deck& deck = project_.decks[deckIndex];
    DeckRuntime& runtime = deckRuntimes_[deckIndex];
    destroyDeckRuntime(runtime);
    auto [targetW, targetH] = outputRenderSizeForDeck(deckIndex);
    targetW = std::max(1, targetW);
    targetH = std::max(1, targetH);

    // Hidden per-deck renderer used by the media engine decode/upload pipeline.
    std::string title = std::string("Deckboy Deck Runtime - ") + (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name);
    runtime.outputWindow = SDL_CreateWindow(
      title.c_str(),
      SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED,
      targetW,
      targetH,
      SDL_WINDOW_HIDDEN
    );
    if (!runtime.outputWindow) {
      return false;
    }

    runtime.outputRenderer = SDL_CreateRenderer(runtime.outputWindow, -1, SDL_RENDERER_ACCELERATED);
    if (!runtime.outputRenderer) {
      runtime.outputRenderer = SDL_CreateRenderer(runtime.outputWindow, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!runtime.outputRenderer) {
      destroyDeckRuntime(runtime);
      return false;
    }

    if (!reopenDeckAudioOutput(deckIndex, deck.audioOutputDeviceName)) {
      destroyDeckRuntime(runtime);
      return false;
    }

    return true;
  }

  bool createOutputRuntime(int outputIndex) {
    OutputTarget& output = project_.outputs[outputIndex];
    OutputRuntime& runtime = outputRuntimes_[outputIndex];
    destroyOutputRuntime(runtime);
    output.outputType = normalizeOutputType(output.outputType);
    auto [targetW, targetH] = outputRenderSizeForOutput(outputIndex);
    targetW = std::max(1, targetW);
    targetH = std::max(1, targetH);

    std::string label = output.name.empty() ? outputDefaultName(outputIndex) : output.name;
    bool streamType = output.outputType == "stream";
    bool windowOutputEnabled = output.enabled && !streamType;
    std::string title = std::string(kOutputTitle) + " - " + label + (streamType ? " [stream]" : "");
    Uint32 windowFlags = streamType
      ? SDL_WINDOW_HIDDEN
      : (SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    runtime.outputWindow = SDL_CreateWindow(
      title.c_str(),
      streamType ? SDL_WINDOWPOS_UNDEFINED : SDL_WINDOWPOS_CENTERED,
      streamType ? SDL_WINDOWPOS_UNDEFINED : SDL_WINDOWPOS_CENTERED,
      targetW,
      targetH,
      windowFlags
    );
    if (!runtime.outputWindow) {
      return false;
    }

    Uint32 rendererFlags = SDL_RENDERER_ACCELERATED | (streamType ? 0u : SDL_RENDERER_PRESENTVSYNC);
    runtime.outputRenderer = SDL_CreateRenderer(runtime.outputWindow, -1, rendererFlags);
    if (!runtime.outputRenderer) {
      runtime.outputRenderer = SDL_CreateRenderer(runtime.outputWindow, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!runtime.outputRenderer) {
      destroyOutputRuntime(runtime);
      return false;
    }

    if (!configureOutputCompositor(outputIndex)) {
      destroyOutputRuntime(runtime);
      return false;
    }

    applyOutputDisplaySelection(outputIndex);
    applyOutputNdiSettings(outputIndex, false);
    if (output.enabled && !streamType) {
      enableOutputFullscreen(outputIndex, false);
    }
    return true;
  }

  bool rebuildDeckRuntimes() {
    normalizeProject(project_);
    for (auto& runtime : deckRuntimes_) {
      destroyDeckRuntime(runtime);
    }
    clearDeckStreamAudioBuffers();
    deckRuntimes_.clear();
    deckRuntimes_.resize(project_.decks.size());
    ensureTimecodeFollowerStateSize();

    for (size_t index = 0; index < project_.decks.size(); ++index) {
      if (!createDeckRuntime(static_cast<int>(index))) {
        return false;
      }
    }
    return true;
  }

  bool rebuildOutputRuntimes() {
    normalizeProject(project_);
    for (auto& runtime : outputRuntimes_) {
      destroyOutputRuntime(runtime);
    }
    outputRuntimes_.clear();
    outputRuntimes_.resize(project_.outputs.size());

    for (size_t index = 0; index < project_.outputs.size(); ++index) {
      if (!createOutputRuntime(static_cast<int>(index))) {
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
    markProjectDirty();
  }

  void restartLiveBrowserCueIfNeeded(int deckIndex) {
    const Cue* active = activeCuePtr(deckIndex);
    if (!active || active->kind != CueKind::Browser) {
      return;
    }
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->browserCueLive) {
      return;
    }
    startBrowserCue(deckIndex, *active);
  }

  void applyOutputDisplaySelection(int outputIndex) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    output.outputType = normalizeOutputType(output.outputType);
    bool streamType = output.outputType == "stream";
    bool windowOutputEnabled = output.enabled && !streamType;
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputWindow) {
      return;
    }

    int displayCount = SDL_GetNumVideoDisplays();
    bool haveDisplayBounds = false;
    SDL_Rect bounds {};
    if (displayCount > 0) {
      output.displayIndex = std::clamp(output.displayIndex, 0, displayCount - 1);
      haveDisplayBounds = (SDL_GetDisplayBounds(output.displayIndex, &bounds) == 0);
    } else {
      output.displayIndex = 0;
    }

    auto [targetW, targetH] = outputRenderSizeForOutput(outputIndex);
    targetW = std::max(1, targetW);
    targetH = std::max(1, targetH);

    Uint32 flags = SDL_GetWindowFlags(runtime->outputWindow);
    bool fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
    if (!streamType && fullscreen) {
      SDL_SetWindowFullscreen(runtime->outputWindow, 0);
    }

    SDL_SetWindowSize(runtime->outputWindow, targetW, targetH);
    if (!streamType && haveDisplayBounds) {
      int x = bounds.x + std::max(0, (bounds.w - targetW) / 2) + outputIndex * 20;
      int y = bounds.y + std::max(0, (bounds.h - targetH) / 2) + outputIndex * 20;
      if (targetW > bounds.w) x = bounds.x + 20 + outputIndex * 20;
      if (targetH > bounds.h) y = bounds.y + 20 + outputIndex * 20;
      SDL_SetWindowPosition(runtime->outputWindow, x, y);
    }

    if (windowOutputEnabled && fullscreen) {
      enableOutputFullscreen(outputIndex, false);
    }
    if (windowOutputEnabled) {
      SDL_ShowWindow(runtime->outputWindow);
      SDL_RaiseWindow(runtime->outputWindow);
    } else {
      SDL_HideWindow(runtime->outputWindow);
    }
    configureOutputCompositor(outputIndex);

    int hostDeckIndex = std::clamp(output.hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    project_.decks[hostDeckIndex].outputDisplayIndex = output.displayIndex;
    std::string titleLabel = output.name.empty() ? outputDefaultName(outputIndex) : output.name;
    std::string title = std::string(kOutputTitle) + " - " + titleLabel;
    SDL_SetWindowTitle(runtime->outputWindow, title.c_str());
  }

  void applyOutputDisplaySelectionAllOutputs(bool restartLiveBrowsers) {
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      applyOutputDisplaySelection(outputIndex);
    }
    if (restartLiveBrowsers) {
      for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
        restartLiveBrowserCueIfNeeded(deckIndex);
      }
    }
  }

  void setOutputSizingModeDisplayNative() {
    bool changed = !project_.outputFollowDisplay;
    project_.outputFollowDisplay = true;
    applyOutputDisplaySelectionAllOutputs(true);
    triggerToast("video mode: native (" + currentDisplayLabel() + ")");
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  void setOutputSizingModeFixed(int width, int height) {
    int w = std::clamp(width, 320, 7680);
    int h = std::clamp(height, 180, 4320);
    bool changed = project_.outputFollowDisplay
      || project_.outputRenderWidth != w
      || project_.outputRenderHeight != h;
    project_.outputFollowDisplay = false;
    project_.outputRenderWidth = w;
    project_.outputRenderHeight = h;
    applyOutputDisplaySelectionAllOutputs(true);
    triggerToast("video mode: fixed " + std::to_string(w) + "x" + std::to_string(h));
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  void sizeFocusedOutputToSelectedDisplay() {
    applyOutputDisplaySelection(project_.focusedOutputIndex);
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (primaryOutputIndexForDeck(deckIndex) && *primaryOutputIndexForDeck(deckIndex) == project_.focusedOutputIndex) {
        restartLiveBrowserCueIfNeeded(deckIndex);
      }
    }
    triggerToast("output sized: " + outputResolutionLabelForOutput(project_.focusedOutputIndex));
    playUiSound(UiSoundEffect::Toggle);
  }

  void setOutputRefreshRate(double hz) {
    double normalized = (!std::isfinite(hz) || hz <= 0.0) ? 0.0 : std::clamp(hz, 1.0, 240.0);
    bool changed = std::abs(project_.outputRefreshRateHz - normalized) > 0.0001;
    project_.outputRefreshRateHz = normalized;
    applyOutputDisplaySelectionAllOutputs(false);
    triggerToast("video refresh: " + outputRefreshRateLabel());
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  void cycleOutputRefreshRate(int direction) {
    auto choices = refreshChoicesForOutput(project_.focusedOutputIndex);
    if (choices.empty()) {
      triggerToast("no refresh choices for raster");
      return;
    }

    int current = project_.outputRefreshRateHz > 0.0
      ? static_cast<int>(std::lround(project_.outputRefreshRateHz))
      : 0;

    int currentIndex = -1;
    for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
      if (choices[i] == current) {
        currentIndex = i;
        break;
      }
    }

    int nextIndex = 0;
    if (currentIndex >= 0) {
      nextIndex = (currentIndex + direction + static_cast<int>(choices.size())) % static_cast<int>(choices.size());
    } else if (current > 0) {
      int nearest = 0;
      int bestDelta = std::abs(choices[0] - current);
      for (int i = 1; i < static_cast<int>(choices.size()); ++i) {
        int delta = std::abs(choices[i] - current);
        if (delta < bestDelta) {
          bestDelta = delta;
          nearest = i;
        }
      }
      nextIndex = (nearest + direction + static_cast<int>(choices.size())) % static_cast<int>(choices.size());
    } else {
      nextIndex = direction >= 0 ? 0 : static_cast<int>(choices.size()) - 1;
    }

    setOutputRefreshRate(static_cast<double>(choices[nextIndex]));
  }

  void cycleOutputDisplay(int direction) {
    int displayCount = SDL_GetNumVideoDisplays();
    if (displayCount <= 0) {
      return;
    }
    OutputTarget& output = focusedOutputMutable();
    output.displayIndex = (output.displayIndex + direction + displayCount) % displayCount;
    bool autoSwitchedToNative = false;
    if (!project_.outputFollowDisplay) {
      project_.outputFollowDisplay = true;
      autoSwitchedToNative = true;
    }
    applyOutputDisplaySelection(project_.focusedOutputIndex);
    if (output.enabled && normalizeOutputType(output.outputType) == "window") {
      enableOutputFullscreen(project_.focusedOutputIndex, false);
    }
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (primaryOutputIndexForDeck(deckIndex) && *primaryOutputIndexForDeck(deckIndex) == project_.focusedOutputIndex) {
        restartLiveBrowserCueIfNeeded(deckIndex);
      }
    }
    std::string label = SDL_GetDisplayName(output.displayIndex);
    triggerToast("display: "
      + (label.empty() ? std::to_string(output.displayIndex + 1) : label)
      + "  " + outputResolutionLabelForOutput(project_.focusedOutputIndex)
      + (autoSwitchedToNative ? "  auto native" : ""));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool setOutputDisplayIndex(int index) {
    int displayCount = SDL_GetNumVideoDisplays();
    if (displayCount <= 0 || index < 0 || index >= displayCount) {
      return false;
    }
    OutputTarget& output = focusedOutputMutable();
    output.displayIndex = index;
    bool autoSwitchedToNative = false;
    if (!project_.outputFollowDisplay) {
      project_.outputFollowDisplay = true;
      autoSwitchedToNative = true;
    }
    applyOutputDisplaySelection(project_.focusedOutputIndex);
    if (output.enabled && normalizeOutputType(output.outputType) == "window") {
      enableOutputFullscreen(project_.focusedOutputIndex, false);
    }
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (primaryOutputIndexForDeck(deckIndex) && *primaryOutputIndexForDeck(deckIndex) == project_.focusedOutputIndex) {
        restartLiveBrowserCueIfNeeded(deckIndex);
      }
    }
    triggerToast("display: " + currentDisplayLabel() + "  "
      + outputResolutionLabelForOutput(project_.focusedOutputIndex)
      + (autoSwitchedToNative ? "  auto native" : ""));
    markProjectDirty();
    return true;
  }

  void refreshDisplayTopology(bool withToast = false) {
    int displayCount = SDL_GetNumVideoDisplays();
    bool changed = false;
    if (displayCount <= 0) {
      for (auto& output : project_.outputs) {
        if (output.displayIndex != 0) {
          output.displayIndex = 0;
          changed = true;
        }
      }
      applyOutputDisplaySelectionAllOutputs(true);
      if (changed) {
        markProjectDirty();
      }
      if (withToast) {
        triggerToast("display scan: no displays reported");
      }
      return;
    }

    for (auto& output : project_.outputs) {
      int clamped = std::clamp(output.displayIndex, 0, displayCount - 1);
      if (output.displayIndex != clamped) {
        output.displayIndex = clamped;
        changed = true;
      }
    }

    applyOutputDisplaySelectionAllOutputs(true);
    if (changed) {
      markProjectDirty();
    }
    if (withToast) {
      triggerToast("display scan: " + std::to_string(displayCount) + " detected");
      playUiSound(UiSoundEffect::Toggle);
    }
  }

  bool setAudioOutputDevice(const std::string& deviceName) {
    if (!reopenDeckAudioOutput(project_.focusedDeckIndex, deviceName)) {
      triggerToast("audio switch failed", {79, 98, 48, 230}, {223, 248, 185, 255});
      return false;
    }
    triggerToast("audio: " + currentAudioOutputLabel());
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool recoverWindowOutputIfNeeded(int outputIndex, bool withToast = false) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return false;
    }
    OutputTarget& output = project_.outputs[outputIndex];
    if (!output.enabled || normalizeOutputType(output.outputType) != "window") {
      return false;
    }
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputWindow) {
      return false;
    }
    if (runtime->recoveryPausedByEscape) {
      return false;
    }

    Uint32 flags = SDL_GetWindowFlags(runtime->outputWindow);
    bool fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
    bool hidden = (flags & SDL_WINDOW_HIDDEN) != 0;
    bool minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;

    int displayCount = SDL_GetNumVideoDisplays();
    int targetDisplay = displayCount > 0
      ? std::clamp(output.displayIndex, 0, displayCount - 1)
      : 0;
    int windowDisplay = SDL_GetWindowDisplayIndex(runtime->outputWindow);
    bool wrongDisplay = displayCount > 0 && (windowDisplay < 0 || windowDisplay != targetDisplay);

    bool needsRecovery = hidden || minimized || !fullscreen || wrongDisplay;
    if (!needsRecovery) {
      return false;
    }

    applyOutputDisplaySelection(outputIndex);
    SDL_ShowWindow(runtime->outputWindow);
    SDL_RaiseWindow(runtime->outputWindow);
    enableOutputFullscreen(outputIndex, false);
    if (withToast) {
      std::string displayLabel = "display " + std::to_string(targetDisplay + 1);
      if (displayCount > 0) {
        const char* displayName = SDL_GetDisplayName(targetDisplay);
        if (displayName && *displayName) {
          displayLabel = displayName;
        }
      }
      triggerToast("output recovered: " + outputLabel(outputIndex) + " -> " + displayLabel);
    }
    return true;
  }

  std::string currentDisplayLabel() const {
    const char* name = SDL_GetDisplayName(outputDisplayIndex(project_.focusedOutputIndex));
    if (name && *name) {
      return name;
    }
    return "display " + std::to_string(outputDisplayIndex(project_.focusedOutputIndex) + 1);
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

  // Find a virtual display number not currently in use.
  static int findFreeVirtualDisplay() {
    for (int n = 20; n < 100; ++n) {
      std::string lock = "/tmp/.X" + std::to_string(n) + "-lock";
      if (!fs::exists(lock)) {
        return n;
      }
    }
    return -1;
  }

  void stopBrowserCue(int deckIndex) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime) {
      return;
    }
    // Stop x11grab capture in the media engine
    if (runtime->mediaEngine && runtime->mediaEngine->isBrowserCapturing()) {
      runtime->mediaEngine->stopBrowserCapture();
    }
    runtime->browserProcess.stop();
    runtime->xvfbProcess.stop();
    runtime->virtualDisplayId.clear();
    runtime->browserStartPhase = BrowserStartPhase::None;
    runtime->browserCueLive = false;
    if (!runtime->browserProfileDir.empty()) {
      std::error_code error;
      fs::remove_all(runtime->browserProfileDir, error);
      runtime->browserProfileDir.clear();
    }
    // Output window is always visible now — never hidden for browser cues
  }

  void stopBrowserCue() {
    stopBrowserCue(project_.focusedDeckIndex);
  }

  // Phase 1: start Xvfb on a free virtual display + begin phased chromium launch.
  // Frame capture (x11grab) kicks in automatically via App::update() after delays.
  bool startBrowserCue(int deckIndex, const Cue& cue) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime) {
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

    int dispNum = findFreeVirtualDisplay();
    if (dispNum < 0) {
      triggerToast("no free virtual display", {79, 98, 48, 230}, {223, 248, 185, 255});
      return false;
    }

    runtime->virtualDisplayId = ":" + std::to_string(dispNum);

    auto [targetW, targetH] = outputRenderSizeForDeck(deckIndex);
    int w = cue.width > 0 ? cue.width : targetW;
    int h = cue.height > 0 ? cue.height : targetH;
    bool legacyRaster = cue.width == kOutputWidth && cue.height == kOutputHeight;
    if (legacyRaster && (targetW != kOutputWidth || targetH != kOutputHeight)) {
      w = targetW;
      h = targetH;
    }
    runtime->pendingBrowserW = w;
    runtime->pendingBrowserH = h;

    // Start Xvfb synchronously (it backgrounds itself).
    if (!spawnDetachedProcess(runtime->xvfbProcess, {
      "Xvfb", runtime->virtualDisplayId,
      "-screen", "0",
      std::to_string(w) + "x" + std::to_string(h) + "x24",
      "-nolisten", "tcp"
    })) {
      triggerToast("Xvfb launch failed", {79, 98, 48, 230}, {223, 248, 185, 255});
      runtime->virtualDisplayId.clear();
      return false;
    }

    // Store URL + profile dir for deferred Chromium launch in update().
    runtime->browserProfileDir = nextBrowserProfilePath();
    std::error_code error;
    fs::create_directories(runtime->browserProfileDir, error);

    // Store URL in a slot accessible to the update loop.
    // Reuse browserProfileDir parent as a signal, but we need the URL.
    // Write it to a temp file so the update loop can read it.
    {
      std::ofstream uf(runtime->browserProfileDir / ".pending_url");
      uf << browserUrl;
    }

    runtime->browserStartPhase = BrowserStartPhase::WaitXvfb;
    runtime->browserPhaseStartedAt = SDL_GetTicks64();
    triggerToast("browser loading…");
    return true;
  }

  bool startBrowserCue(const Cue& cue) {
    return startBrowserCue(project_.focusedDeckIndex, cue);
  }

  // Called from App::update() to advance the phased browser startup.
  void tickBrowserStartup(int deckIndex) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || runtime->browserStartPhase == BrowserStartPhase::None ||
        runtime->browserStartPhase == BrowserStartPhase::Live) {
      return;
    }

    Uint64 now = SDL_GetTicks64();
    Uint64 elapsed = now - runtime->browserPhaseStartedAt;

    if (runtime->browserStartPhase == BrowserStartPhase::WaitXvfb) {
      if (elapsed < 400) return;  // let Xvfb start
      // Read back the pending URL
      std::string browserUrl;
      {
        std::ifstream uf(runtime->browserProfileDir / ".pending_url");
        std::getline(uf, browserUrl);
      }
      if (browserUrl.empty()) {
        stopBrowserCue(deckIndex);
        return;
      }
      std::string executable = browserExecutablePath();
      int w = runtime->pendingBrowserW;
      int h = runtime->pendingBrowserH;
      std::vector<std::string> args {
        executable,
        "--no-first-run",
        "--disable-session-crashed-bubble",
        "--disable-infobars",
        "--disable-gpu",
        "--app=" + browserUrl,
        "--window-size=" + std::to_string(w) + "," + std::to_string(h),
        "--window-position=0,0",
        "--user-data-dir=" + runtime->browserProfileDir.string(),
        "--start-maximized"
      };
      // Set DISPLAY to virtual display via environment variable prefix trick.
      // spawnDetachedProcess takes a plain argv; prepend env via a shell wrapper.
      std::vector<std::string> envArgs {
        "env",
        "DISPLAY=" + runtime->virtualDisplayId,
        "LIBGL_ALWAYS_SOFTWARE=1"
      };
      envArgs.insert(envArgs.end(), args.begin(), args.end());
      spawnDetachedProcess(runtime->browserProcess, envArgs);
      runtime->browserStartPhase = BrowserStartPhase::WaitChrome;
      runtime->browserPhaseStartedAt = now;
      return;
    }

    if (runtime->browserStartPhase == BrowserStartPhase::WaitChrome) {
      if (elapsed < 1200) return;  // let Chrome render first frame
      // Begin x11grab capture via the media engine.
      MediaEngine* eng = runtime->mediaEngine.get();
      if (!eng) { stopBrowserCue(deckIndex); return; }
      // Get transition params from the active cue if available.
      const Deck& deck = project_.decks[deckIndex];
      double transSecs = deck.transitionSeconds;
      TransitionStyle transStyle = parseTransitionStyleToken(deck.transitionStyle);
      if (deck.activeIndex >= 0 && deck.activeIndex < static_cast<int>(deck.cues.size())) {
        const Cue& ac = deck.cues[deck.activeIndex];
        if (ac.cueTransitionSeconds >= 0.0) transSecs = ac.cueTransitionSeconds;
        if (!ac.cueTransitionStyle.empty()) transStyle = parseTransitionStyleToken(ac.cueTransitionStyle);
      }
      eng->startBrowserCapture(
        runtime->virtualDisplayId,
        runtime->pendingBrowserW,
        runtime->pendingBrowserH,
        deck.activeIndex >= 0 ? deck.cues[deck.activeIndex].fadeInSeconds : 0.0,
        deck.activeIndex >= 0 ? deck.cues[deck.activeIndex].fadeOutSeconds : 0.0,
        transSecs,
        transStyle
      );
      runtime->browserStartPhase = BrowserStartPhase::Live;
      runtime->browserCueLive = true;
      triggerToast("browser live");
      return;
    }
  }

  fs::path defaultProjectFile() const {
    const char* envPath = std::getenv("PLAYBOY_PROJECT");
    if (envPath && *envPath) {
      std::error_code ec;
      return Paths::normalizeProjectPath(fs::absolute(envPath, ec));
    }
    return Paths::defaultProjectFile();
  }

  fs::path normalizeProjectPath(fs::path path) const {
    return Paths::normalizeProjectPath(path);
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
    if (project_.outputs.empty()) {
      return "off";
    }
    int outputIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    const OutputTarget& output = project_.outputs[outputIndex];
    std::string source = output.ndiSourceName.empty()
      ? defaultOutputNdiSourceName(output, outputIndex)
      : output.ndiSourceName;
    std::string keySource = output.ndiKeySourceName.empty()
      ? defaultOutputNdiKeySourceName(output, outputIndex)
      : output.ndiKeySourceName;
    if (!output.ndiEnabled) {
      return "off";
    }
#if defined(PLAYBOY_HAS_NDI_SDK)
    const OutputRuntime* runtime = runtimeForOutput(outputIndex);
    bool live = runtime && runtime->ndiSender;
    bool keyLive = runtime && runtime->ndiKeySender;
    int listeners = ndiConnectionCountForOutput(outputIndex);
    int keyListeners = ndiKeyConnectionCountForOutput(outputIndex);
    std::string suffix = live ? "on" : "pending";
    if (listeners > 0) {
      suffix += " (" + std::to_string(listeners) + " rx)";
    }
    std::string label = suffix + " / fill:" + source;
    if (output.ndiKeyEnabled) {
      std::string keySuffix = keyLive ? "on" : "pending";
      if (keyListeners > 0) {
        keySuffix += " (" + std::to_string(keyListeners) + " rx)";
      }
      label += "  key:" + keySource + " [" + keySuffix + "]";
    }
    return label;
#else
    return "unavailable";
#endif
  }

  std::string currentTransitionLabel() const {
    const Deck& deck = focusedDeck();
    return transitionStyleToken(parseTransitionStyleToken(deck.transitionStyle)) + " " + formatSeconds(deck.transitionSeconds);
  }

  std::string currentTimecodeLabel() const {
    const Deck& deck = focusedDeck();
    return formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps) +
           " @" + std::to_string(static_cast<int>(std::round(deck.timecodeFps))) +
           (deck.timecodeChaseEnabled ? " chase" : " free");
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
    output << " | " << deckOutputRoutingLabel(deckIndex);
    if (deck.timecodeChaseEnabled) {
      output << " | tc:" << formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps);
    }
    return output.str();
  }

  std::string cueNumberLabelForStatus(const Deck& deck, int cueIndex) const {
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return "";
    }
    return cueDisplayToken(deck.cues[cueIndex], cueIndex);
  }

  std::string cueIdForStatus(const Deck& deck, int cueIndex) const {
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return "";
    }
    return deck.cues[cueIndex].id;
  }

  std::string buildCueProgrammingSnapshot() const {
    std::ostringstream output;
    output << "DECKBOY_0.01 cues"
           << " focus=" << (project_.focusedDeckIndex + 1)
           << " decks=" << project_.decks.size();
    if (!lastCueFindToken_.empty() && !lastCueFindMatches_.empty()) {
      int cursor = std::clamp(lastCueFindCursor_, 0, static_cast<int>(lastCueFindMatches_.size()) - 1);
      output << " find=\"" << lastCueFindToken_ << "\""
             << " match=" << (cursor + 1) << "/" << lastCueFindMatches_.size()
             << " find_deck=" << (lastCueFindDeckIndex_ >= 0 ? lastCueFindDeckIndex_ + 1 : 0);
    } else {
      output << " find=none";
    }
    output << '\n';

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      const Deck& deck = project_.decks[deckIndex];
      std::string selectedNum = cueNumberLabelForStatus(deck, deck.selectedIndex);
      std::string selectedId = cueIdForStatus(deck, deck.selectedIndex);
      std::string activeNum = cueNumberLabelForStatus(deck, deck.activeIndex);
      std::string activeId = cueIdForStatus(deck, deck.activeIndex);
      output << "CUEDECK " << (deckIndex + 1)
             << " name=\"" << (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << "\""
             << " selected_num=\"" << selectedNum << "\""
             << " selected_id=\"" << selectedId << "\""
             << " active_num=\"" << activeNum << "\""
             << " active_id=\"" << activeId << "\"";
      if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
        output << " selected_name=\"" << deck.cues[deck.selectedIndex].name << "\"";
      }
      if (deck.activeIndex >= 0 && deck.activeIndex < static_cast<int>(deck.cues.size())) {
        output << " active_name=\"" << deck.cues[deck.activeIndex].name << "\"";
      }
      output << '\n';
    }
    return output.str();
  }

  std::string buildStatusSnapshot() const {
    std::ostringstream output;
    output << "DECKBOY_0.01"
           << " focus=" << (project_.focusedDeckIndex + 1)
           << " decks=" << project_.decks.size()
           << " focused_output=" << (project_.focusedOutputIndex + 1)
           << " outputs=" << project_.outputs.size()
           << " focused_group=" << (project_.groupPresets.empty() ? 0 : project_.focusedGroupPresetIndex + 1)
           << " groups=" << project_.groupPresets.size()
           << " panic_profile=" << normalizePanicProfileToken(project_.panicProfile)
           << " panic_fade_s=" << project_.panicFadeSeconds
           << " panic_restore=" << (project_.panicAutoRestore ? "on" : "off")
           << " find=\"" << lastCueFindToken_ << "\""
           << " find_matches=" << lastCueFindMatches_.size()
           << " find_cursor=" << (lastCueFindCursor_ >= 0 ? lastCueFindCursor_ + 1 : 0)
           << " find_deck=" << (lastCueFindDeckIndex_ >= 0 ? lastCueFindDeckIndex_ + 1 : 0)
           << " video_mode=" << (project_.outputFollowDisplay ? "native" : "fixed")
           << " video_hz=" << formatRefreshRateLabel(project_.outputRefreshRateHz)
           << " video_depth=" << outputBitDepthModeLabel()
           << " canvas=" << (project_.outputCanvasEnabled
                ? (std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
                : "off")
           << '\n';
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      const Deck& deck = project_.decks[deckIndex];
      auto outputIndexOpt = primaryOutputIndexForDeck(deckIndex);
      int outputIndex = outputIndexOpt ? *outputIndexOpt : -1;
      int displayIndex = outputIndex >= 0 ? outputDisplayIndex(outputIndex) : deck.outputDisplayIndex;
      int layerIndex = primaryLayerIndexForDeck(deckIndex);
      std::string routeLabel = outputIndex >= 0 ? std::to_string(outputIndex + 1) : "--";
      std::string rasterLabel = outputIndex >= 0 ? outputResolutionLabelForOutput(outputIndex) : outputResolutionLabel(deckIndex);
      std::string depthLabel = outputIndex >= 0 ? outputBitDepthActiveLabelForOutput(outputIndex) : outputBitDepthActiveLabel(deckIndex);
      const Cue* activeCue = activeCuePtr(deckIndex);
      const Cue* selectedCue = selectedCuePtr(deckIndex);
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      std::string selectedNum = cueNumberLabelForStatus(deck, deck.selectedIndex);
      std::string selectedId = cueIdForStatus(deck, deck.selectedIndex);
      std::string activeNum = cueNumberLabelForStatus(deck, deck.activeIndex);
      std::string activeId = cueIdForStatus(deck, deck.activeIndex);
      output << "DECK " << (deckIndex + 1)
             << " name=\"" << (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << "\""
             << " status=" << transportStatusLabel(deckIndex)
             << " selected=" << (deck.selectedIndex >= 0 ? deck.selectedIndex + 1 : 0)
             << " active=" << (deck.activeIndex >= 0 ? deck.activeIndex + 1 : 0)
             << " selected_num=\"" << selectedNum << "\""
             << " selected_id=\"" << selectedId << "\""
             << " active_num=\"" << activeNum << "\""
             << " active_id=\"" << activeId << "\""
             << " display=" << (displayIndex + 1)
             << " route=" << routeLabel
             << " layer=" << layerIndex
             << " raster=" << rasterLabel
             << " depth=" << depthLabel
             << " audio=\"" << (deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\""
             << " overlay=" << (deck.timeOverlayEnabled ? "on" : "off")
             << " view=" << deck.canvasViewX << "," << deck.canvasViewY
             << " warp=" << (deck.warpEnabled ? "on" : "off")
             << " blend=" << static_cast<int>(std::lround(deck.edgeBlendLeft * 100.0f))
             << "," << static_cast<int>(std::lround(deck.edgeBlendRight * 100.0f))
             << "," << static_cast<int>(std::lround(deck.edgeBlendTop * 100.0f))
             << "," << static_cast<int>(std::lround(deck.edgeBlendBottom * 100.0f))
             << " transition=" << transitionStyleToken(parseTransitionStyleToken(deck.transitionStyle))
             << " transition_s=" << deck.transitionSeconds
             << " tc=" << formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps)
             << " tc_chase=" << (deck.timecodeChaseEnabled ? "on" : "off")
             << " tc_run=" << (deck.timecodeRunEnabled ? "on" : "off")
             << " tc_trigger=" << (deck.timecodeTriggerEnabled ? "on" : "off")
             << " tc_jam=" << (deck.timecodeJamSyncEnabled ? "on" : "off")
             << " tc_freewheel_s=" << deck.timecodeFreewheelSeconds
             << " cue=\"" << (activeCue ? activeCue->name : (selectedCue ? selectedCue->name : "")) << "\"";
      if (activeCue) {
        output << " cue_id=\"" << activeCue->id << "\""
               << " in=" << formatSeconds(activeCue->inPointSeconds)
               << " out=" << formatSeconds(activeCue->outPointSeconds > 0.0 ? activeCue->outPointSeconds : activeCue->duration)
               << " tc_mark=" << (activeCue->triggerTimecodeSeconds >= 0.0 ? formatTimecode(activeCue->triggerTimecodeSeconds, deck.timecodeFps) : "--:--:--:--");
      }
      if (engine) {
        output << " pos=" << formatSeconds(engine->position())
               << " dur=" << formatSeconds(engine->duration())
               << " vol=" << static_cast<int>(std::round(engine->volume() * 100.0f));
      }
      output << '\n';
    }
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      const OutputTarget& out = project_.outputs[outputIndex];
      int hostDeckIndex = std::clamp(out.hostDeckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
      int layerCount = 0;
      for (const auto& assignment : project_.layerAssignments) {
        if (assignment.enabled && resolveAssignmentOutputIndex(assignment) == outputIndex) {
          ++layerCount;
        }
      }
      std::string type = normalizeOutputType(out.outputType);
      std::string protocol = normalizeOutputStreamProtocol(out.streamProtocol);
      std::string mirror = out.mirrorSourceOutputIndex >= 0 ? std::to_string(out.mirrorSourceOutputIndex + 1) : "off";
      std::string url = trim(out.streamUrl);
      int alphaPct = static_cast<int>(std::lround(std::clamp(out.outputAlpha, 0.0f, 1.0f) * 100.0f));
      std::string ndiSource = trim(out.ndiSourceName).empty() ? defaultOutputNdiSourceName(out, outputIndex) : out.ndiSourceName;
      std::string ndiKeySource = trim(out.ndiKeySourceName).empty() ? defaultOutputNdiKeySourceName(out, outputIndex) : out.ndiKeySourceName;
      if (url.empty()) {
        url = defaultOutputStreamUrl(protocol, outputIndex);
      }
      output << "OUTPUT " << (outputIndex + 1)
             << " name=\"" << (out.name.empty() ? outputDefaultName(outputIndex) : out.name) << "\""
             << " id=\"" << out.outputId << "\""
             << " enabled=" << (out.enabled ? "on" : "off")
             << " type=" << type
             << " host=" << (hostDeckIndex + 1)
             << " display=" << (outputDisplayIndex(outputIndex) + 1)
             << " layers=" << layerCount
             << " mirror=" << mirror
             << " stream=" << (out.streamEnabled ? "on" : "off")
             << " proto=" << protocol
             << " bitrate=" << out.streamBitrateKbps
             << " url=\"" << url << "\""
             << " ndi=" << (out.ndiEnabled ? "on" : "off")
             << " ndi_name=\"" << ndiSource << "\""
             << " ndi_rx=" << ndiConnectionCountForOutput(outputIndex)
             << " ndi_key=" << (out.ndiKeyEnabled ? "on" : "off")
             << " ndi_key_name=\"" << ndiKeySource << "\""
             << " ndi_key_rx=" << ndiKeyConnectionCountForOutput(outputIndex)
             << " alpha=" << alphaPct
             << " delay_ms=" << std::clamp(out.outputDelayMs, 0, 5000)
             << " overlay=" << (out.outputTimeOverlayEnabled ? "on" : "off")
             << " color_space=" << normalizeOutputColorSpace(out.outputColorSpace)
             << " layout=" << normalizeOutputLayoutMode(out.outputLayoutMode)
             << " orientation=" << normalizeOutputOrientationDegrees(out.outputOrientationDegrees)
             << " test_card=" << (out.outputTestCardEnabled ? "on" : "off")
             << '\n';
    }
    for (int presetIndex = 0; presetIndex < static_cast<int>(project_.groupPresets.size()); ++presetIndex) {
      const GroupPreset& preset = project_.groupPresets[presetIndex];
      int slotCount = std::min(static_cast<int>(preset.slots.size()), static_cast<int>(project_.decks.size()));
      int armed = 0;
      for (int deckIndex = 0; deckIndex < slotCount; ++deckIndex) {
        const GroupSlot& slot = preset.slots[deckIndex];
        if (!slot.bypass && !slot.cueId.empty()) {
          ++armed;
        }
      }
      output << "GROUP " << (presetIndex + 1)
             << " name=\"" << (preset.name.empty() ? groupPresetDefaultName(presetIndex) : preset.name) << "\""
             << " slots=" << slotCount
             << " armed=" << armed
             << '\n';
    }
    return output.str();
  }

  std::string buildDeckStatusSnapshot(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "DECK offline\n";
    }
    std::ostringstream output;
    const Deck& deck = project_.decks[deckIndex];
    auto outputIndexOpt = primaryOutputIndexForDeck(deckIndex);
    int outputIndex = outputIndexOpt ? *outputIndexOpt : -1;
    int displayIndex = outputIndex >= 0 ? outputDisplayIndex(outputIndex) : deck.outputDisplayIndex;
    int layerIndex = primaryLayerIndexForDeck(deckIndex);
    std::string routeLabel = outputIndex >= 0 ? std::to_string(outputIndex + 1) : "--";
    std::string rasterLabel = outputIndex >= 0 ? outputResolutionLabelForOutput(outputIndex) : outputResolutionLabel(deckIndex);
    std::string depthLabel = outputIndex >= 0 ? outputBitDepthActiveLabelForOutput(outputIndex) : outputBitDepthActiveLabel(deckIndex);
    const Cue* activeCue = activeCuePtr(deckIndex);
    const Cue* selectedCue = selectedCuePtr(deckIndex);
    const MediaEngine* engine = mediaEngineForDeck(deckIndex);
    std::string selectedNum = cueNumberLabelForStatus(deck, deck.selectedIndex);
    std::string selectedId = cueIdForStatus(deck, deck.selectedIndex);
    std::string activeNum = cueNumberLabelForStatus(deck, deck.activeIndex);
    std::string activeId = cueIdForStatus(deck, deck.activeIndex);
    output << "DECKBOY_0.01"
           << " focus=" << (project_.focusedDeckIndex + 1)
           << " decks=" << project_.decks.size()
           << " panic_profile=" << normalizePanicProfileToken(project_.panicProfile)
           << " panic_fade_s=" << project_.panicFadeSeconds
           << " panic_restore=" << (project_.panicAutoRestore ? "on" : "off")
           << " find=\"" << lastCueFindToken_ << "\""
           << " find_matches=" << lastCueFindMatches_.size()
           << " find_cursor=" << (lastCueFindCursor_ >= 0 ? lastCueFindCursor_ + 1 : 0)
           << " find_deck=" << (lastCueFindDeckIndex_ >= 0 ? lastCueFindDeckIndex_ + 1 : 0)
           << " video_mode=" << (project_.outputFollowDisplay ? "native" : "fixed")
           << " video_hz=" << formatRefreshRateLabel(project_.outputRefreshRateHz)
           << " video_depth=" << outputBitDepthModeLabel()
           << " canvas=" << (project_.outputCanvasEnabled
                ? (std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
                : "off")
           << '\n';
    output << "DECK " << (deckIndex + 1)
           << " name=\"" << (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << "\""
           << " status=" << transportStatusLabel(deckIndex)
           << " selected=" << (deck.selectedIndex >= 0 ? deck.selectedIndex + 1 : 0)
           << " active=" << (deck.activeIndex >= 0 ? deck.activeIndex + 1 : 0)
           << " selected_num=\"" << selectedNum << "\""
           << " selected_id=\"" << selectedId << "\""
           << " active_num=\"" << activeNum << "\""
           << " active_id=\"" << activeId << "\""
           << " display=" << (displayIndex + 1)
           << " route=" << routeLabel
           << " layer=" << layerIndex
           << " raster=" << rasterLabel
           << " depth=" << depthLabel
           << " audio=\"" << (deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\""
           << " overlay=" << (deck.timeOverlayEnabled ? "on" : "off")
           << " view=" << deck.canvasViewX << "," << deck.canvasViewY
           << " warp=" << (deck.warpEnabled ? "on" : "off")
           << " blend=" << static_cast<int>(std::lround(deck.edgeBlendLeft * 100.0f))
           << "," << static_cast<int>(std::lround(deck.edgeBlendRight * 100.0f))
           << "," << static_cast<int>(std::lround(deck.edgeBlendTop * 100.0f))
           << "," << static_cast<int>(std::lround(deck.edgeBlendBottom * 100.0f))
           << " transition=" << transitionStyleToken(parseTransitionStyleToken(deck.transitionStyle))
           << " transition_s=" << deck.transitionSeconds
           << " tc=" << formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps)
           << " tc_chase=" << (deck.timecodeChaseEnabled ? "on" : "off")
           << " tc_run=" << (deck.timecodeRunEnabled ? "on" : "off")
           << " tc_trigger=" << (deck.timecodeTriggerEnabled ? "on" : "off")
           << " tc_jam=" << (deck.timecodeJamSyncEnabled ? "on" : "off")
           << " tc_freewheel_s=" << deck.timecodeFreewheelSeconds
           << " cue=\"" << (activeCue ? activeCue->name : (selectedCue ? selectedCue->name : "")) << "\"";
    if (activeCue) {
      output << " cue_id=\"" << activeCue->id << "\""
             << " in=" << formatSeconds(activeCue->inPointSeconds)
             << " out=" << formatSeconds(activeCue->outPointSeconds > 0.0 ? activeCue->outPointSeconds : activeCue->duration)
             << " tc_mark=" << (activeCue->triggerTimecodeSeconds >= 0.0 ? formatTimecode(activeCue->triggerTimecodeSeconds, deck.timecodeFps) : "--:--:--:--");
    }
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
           << "\"app\":\"DECKBOY_0.01\","
           << "\"focusedDeck\":" << (project_.focusedDeckIndex + 1) << ","
           << "\"deckCount\":" << project_.decks.size() << ","
           << "\"focusedOutput\":" << (project_.focusedOutputIndex + 1) << ","
           << "\"outputCount\":" << project_.outputs.size() << ","
           << "\"focusedGroupPreset\":" << (project_.groupPresets.empty() ? 0 : project_.focusedGroupPresetIndex + 1) << ","
           << "\"groupPresetCount\":" << project_.groupPresets.size() << ","
           << "\"panicProfile\":\"" << escapeJson(normalizePanicProfileToken(project_.panicProfile)) << "\","
           << "\"panicFadeSeconds\":" << project_.panicFadeSeconds << ","
           << "\"panicAutoRestore\":" << (project_.panicAutoRestore ? "true" : "false") << ","
           << "\"findToken\":\"" << escapeJson(lastCueFindToken_) << "\","
           << "\"findMatchCount\":" << lastCueFindMatches_.size() << ","
           << "\"findCursor\":" << (lastCueFindCursor_ >= 0 ? lastCueFindCursor_ + 1 : 0) << ","
           << "\"findDeck\":" << (lastCueFindDeckIndex_ >= 0 ? lastCueFindDeckIndex_ + 1 : 0) << ","
           << "\"outputMode\":\"" << (project_.outputFollowDisplay ? "native" : "fixed") << "\","
           << "\"outputRefreshHz\":" << project_.outputRefreshRateHz << ","
           << "\"outputBitDepthMode\":\"" << escapeJson(outputBitDepthModeLabel()) << "\","
           << "\"outputCanvasEnabled\":" << (project_.outputCanvasEnabled ? "true" : "false") << ","
           << "\"outputCanvasWidth\":" << project_.outputCanvasWidth << ","
           << "\"outputCanvasHeight\":" << project_.outputCanvasHeight << ","
           << "\"outputs\":[";
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (outputIndex > 0) {
        output << ",";
      }
      const OutputTarget& out = project_.outputs[outputIndex];
      int hostDeckIndex = std::clamp(out.hostDeckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
      int layerCount = 0;
      for (const auto& assignment : project_.layerAssignments) {
        if (assignment.enabled && resolveAssignmentOutputIndex(assignment) == outputIndex) {
          ++layerCount;
        }
      }
      std::string type = normalizeOutputType(out.outputType);
      std::string protocol = normalizeOutputStreamProtocol(out.streamProtocol);
      std::string url = trim(out.streamUrl);
      int alphaPct = static_cast<int>(std::lround(std::clamp(out.outputAlpha, 0.0f, 1.0f) * 100.0f));
      std::string ndiSource = trim(out.ndiSourceName).empty() ? defaultOutputNdiSourceName(out, outputIndex) : out.ndiSourceName;
      std::string ndiKeySource = trim(out.ndiKeySourceName).empty() ? defaultOutputNdiKeySourceName(out, outputIndex) : out.ndiKeySourceName;
      if (url.empty()) {
        url = defaultOutputStreamUrl(protocol, outputIndex);
      }
      output << "{"
             << "\"index\":" << (outputIndex + 1) << ","
             << "\"name\":\"" << escapeJson(out.name.empty() ? outputDefaultName(outputIndex) : out.name) << "\","
             << "\"outputId\":\"" << escapeJson(out.outputId) << "\","
             << "\"type\":\"" << escapeJson(type) << "\","
             << "\"hostDeck\":" << (hostDeckIndex + 1) << ","
             << "\"display\":" << (outputDisplayIndex(outputIndex) + 1) << ","
             << "\"enabled\":" << (out.enabled ? "true" : "false") << ","
             << "\"mirrorSourceOutput\":" << (out.mirrorSourceOutputIndex >= 0 ? out.mirrorSourceOutputIndex + 1 : 0) << ","
             << "\"layerCount\":" << layerCount << ","
             << "\"streamEnabled\":" << (out.streamEnabled ? "true" : "false") << ","
             << "\"streamProtocol\":\"" << escapeJson(protocol) << "\","
             << "\"streamUrl\":\"" << escapeJson(url) << "\","
             << "\"streamBitrateKbps\":" << out.streamBitrateKbps << ","
             << "\"ndiEnabled\":" << (out.ndiEnabled ? "true" : "false") << ","
             << "\"ndiName\":\"" << escapeJson(ndiSource) << "\","
             << "\"ndiReceivers\":" << ndiConnectionCountForOutput(outputIndex) << ","
             << "\"ndiKeyEnabled\":" << (out.ndiKeyEnabled ? "true" : "false") << ","
             << "\"ndiKeyName\":\"" << escapeJson(ndiKeySource) << "\","
             << "\"ndiKeyReceivers\":" << ndiKeyConnectionCountForOutput(outputIndex) << ","
             << "\"outputAlphaPercent\":" << alphaPct << ","
             << "\"outputDelayMs\":" << std::clamp(out.outputDelayMs, 0, 5000) << ","
             << "\"outputTimeOverlay\":" << (out.outputTimeOverlayEnabled ? "true" : "false") << ","
             << "\"outputColorSpace\":\"" << escapeJson(normalizeOutputColorSpace(out.outputColorSpace)) << "\","
             << "\"outputLayoutMode\":\"" << escapeJson(normalizeOutputLayoutMode(out.outputLayoutMode)) << "\","
             << "\"outputOrientation\":" << normalizeOutputOrientationDegrees(out.outputOrientationDegrees) << ","
             << "\"outputTestCard\":" << (out.outputTestCardEnabled ? "true" : "false")
             << "}";
    }
    output << "],"
           << "\"groupPresets\":[";
    for (int presetIndex = 0; presetIndex < static_cast<int>(project_.groupPresets.size()); ++presetIndex) {
      if (presetIndex > 0) {
        output << ",";
      }
      const GroupPreset& preset = project_.groupPresets[presetIndex];
      output << "{"
             << "\"index\":" << (presetIndex + 1) << ","
             << "\"name\":\"" << escapeJson(preset.name.empty() ? groupPresetDefaultName(presetIndex) : preset.name) << "\","
             << "\"slots\":[";
      int slotCount = std::min(static_cast<int>(preset.slots.size()), static_cast<int>(project_.decks.size()));
      for (int deckIndex = 0; deckIndex < slotCount; ++deckIndex) {
        if (deckIndex > 0) {
          output << ",";
        }
        const GroupSlot& slot = preset.slots[deckIndex];
        output << "{"
               << "\"deck\":" << (deckIndex + 1) << ","
               << "\"bypass\":" << (slot.bypass ? "true" : "false") << ","
               << "\"cueId\":\"" << escapeJson(slot.cueId) << "\""
               << "}";
      }
      output << "]}";
    }
    output << "],"
           << "\"decks\":[";
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (deckIndex > 0) {
        output << ",";
      }
      const Deck& deck = project_.decks[deckIndex];
      auto outputIndexOpt = primaryOutputIndexForDeck(deckIndex);
      int outputIndex = outputIndexOpt ? *outputIndexOpt : -1;
      int displayIndex = outputIndex >= 0 ? outputDisplayIndex(outputIndex) : deck.outputDisplayIndex;
      int layerIndex = primaryLayerIndexForDeck(deckIndex);
      std::string rasterLabel = outputIndex >= 0 ? outputResolutionLabelForOutput(outputIndex) : outputResolutionLabel(deckIndex);
      std::string depthLabel = outputIndex >= 0 ? outputBitDepthActiveLabelForOutput(outputIndex) : outputBitDepthActiveLabel(deckIndex);
      const Cue* activeCue = activeCuePtr(deckIndex);
      const Cue* selectedCue = selectedCuePtr(deckIndex);
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      std::string selectedNum = cueNumberLabelForStatus(deck, deck.selectedIndex);
      std::string selectedId = cueIdForStatus(deck, deck.selectedIndex);
      std::string activeNum = cueNumberLabelForStatus(deck, deck.activeIndex);
      std::string activeId = cueIdForStatus(deck, deck.activeIndex);
      output << "{"
             << "\"index\":" << (deckIndex + 1) << ","
             << "\"name\":\"" << escapeJson(deck.name.empty() ? deckDefaultName(deckIndex) : deck.name) << "\","
             << "\"status\":\"" << escapeJson(transportStatusLabel(deckIndex)) << "\","
             << "\"selected\":" << (deck.selectedIndex >= 0 ? deck.selectedIndex + 1 : 0) << ","
             << "\"active\":" << (deck.activeIndex >= 0 ? deck.activeIndex + 1 : 0) << ","
             << "\"selectedCueNumber\":\"" << escapeJson(selectedNum) << "\","
             << "\"selectedCueId\":\"" << escapeJson(selectedId) << "\","
             << "\"activeCueNumber\":\"" << escapeJson(activeNum) << "\","
             << "\"activeCueId\":\"" << escapeJson(activeId) << "\","
             << "\"display\":" << (displayIndex + 1) << ","
             << "\"routeOutput\":" << (outputIndex + 1) << ","
             << "\"layer\":" << layerIndex << ","
             << "\"raster\":\"" << rasterLabel << "\","
             << "\"outputDepth\":\"" << depthLabel << "\","
             << "\"audio\":\"" << escapeJson(deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\","
             << "\"timeOverlay\":" << (deck.timeOverlayEnabled ? "true" : "false") << ","
             << "\"canvasViewX\":" << deck.canvasViewX << ","
             << "\"canvasViewY\":" << deck.canvasViewY << ","
             << "\"warpEnabled\":" << (deck.warpEnabled ? "true" : "false") << ","
             << "\"warpTopLeftX\":" << deck.warpTopLeftX << ","
             << "\"warpTopLeftY\":" << deck.warpTopLeftY << ","
             << "\"warpTopRightX\":" << deck.warpTopRightX << ","
             << "\"warpTopRightY\":" << deck.warpTopRightY << ","
             << "\"warpBottomRightX\":" << deck.warpBottomRightX << ","
             << "\"warpBottomRightY\":" << deck.warpBottomRightY << ","
             << "\"warpBottomLeftX\":" << deck.warpBottomLeftX << ","
             << "\"warpBottomLeftY\":" << deck.warpBottomLeftY << ","
             << "\"edgeBlendLeft\":" << deck.edgeBlendLeft << ","
             << "\"edgeBlendRight\":" << deck.edgeBlendRight << ","
             << "\"edgeBlendTop\":" << deck.edgeBlendTop << ","
             << "\"edgeBlendBottom\":" << deck.edgeBlendBottom << ","
             << "\"transitionStyle\":\"" << escapeJson(transitionStyleToken(parseTransitionStyleToken(deck.transitionStyle))) << "\","
             << "\"transitionSeconds\":" << deck.transitionSeconds << ","
             << "\"timecode\":\"" << escapeJson(formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps)) << "\","
             << "\"timecodeFps\":" << deck.timecodeFps << ","
             << "\"timecodeChase\":" << (deck.timecodeChaseEnabled ? "true" : "false") << ","
             << "\"timecodeRun\":" << (deck.timecodeRunEnabled ? "true" : "false") << ","
             << "\"timecodeTrigger\":" << (deck.timecodeTriggerEnabled ? "true" : "false") << ","
             << "\"timecodeJam\":" << (deck.timecodeJamSyncEnabled ? "true" : "false") << ","
             << "\"timecodeFreewheelSeconds\":" << deck.timecodeFreewheelSeconds << ","
             << "\"cue\":\"" << escapeJson(activeCue ? activeCue->name : (selectedCue ? selectedCue->name : "")) << "\"";
      if (activeCue) {
        output << ",\"cueId\":\"" << escapeJson(activeCue->id) << "\""
               << ",\"cueIn\":\"" << escapeJson(formatSeconds(activeCue->inPointSeconds)) << "\""
               << ",\"cueOut\":\"" << escapeJson(formatSeconds(activeCue->outPointSeconds > 0.0 ? activeCue->outPointSeconds : activeCue->duration)) << "\""
               << ",\"cueTriggerTc\":\"" << (activeCue->triggerTimecodeSeconds >= 0.0 ? escapeJson(formatTimecode(activeCue->triggerTimecodeSeconds, deck.timecodeFps)) : std::string("")) << "\"";
      }
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
    statusCueSnapshot_ = buildCueProgrammingSnapshot();
    statusDeckSnapshots_.clear();
    statusDeckSnapshots_.reserve(project_.decks.size());
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      statusDeckSnapshots_.push_back(buildDeckStatusSnapshot(deckIndex));
    }
  }

  void disarmAllOutputsForStartup() {
    normalizeProject(project_);
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      OutputTarget& output = project_.outputs[outputIndex];
      output.enabled = false;
      stopOutputStream(outputIndex);
    }
  }

  bool jumpTriggersPlayback() const {
    return normalizeJumpModeToken(project_.jumpMode) == "trigger";
  }

  void setJumpModeToken(const std::string& token) {
    std::string normalized = normalizeJumpModeToken(token);
    if (project_.jumpMode == normalized) {
      triggerToast("jump mode: " + jumpModeLabelFromToken(project_.jumpMode));
      return;
    }
    project_.jumpMode = normalized;
    triggerToast("jump mode: " + jumpModeLabelFromToken(project_.jumpMode));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleJumpMode() {
    setJumpModeToken(jumpTriggersPlayback() ? "load" : "trigger");
  }

  void setJumpTransitionEnabled(bool enabled) {
    if (project_.jumpTransitionEnabled == enabled) {
      triggerToast(std::string("jump transition: ") + (project_.jumpTransitionEnabled ? "on" : "off"));
      return;
    }
    project_.jumpTransitionEnabled = enabled;
    triggerToast(std::string("jump transition: ") + (project_.jumpTransitionEnabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void cyclePanicProfile(int direction) {
    static const std::array<std::string, 4> kProfiles {
      "outputs_off", "fade_pause", "fade_rewind", "fade_load_next"
    };
    std::string current = normalizePanicProfileToken(project_.panicProfile);
    int currentIndex = 0;
    for (int i = 0; i < static_cast<int>(kProfiles.size()); ++i) {
      if (kProfiles[i] == current) {
        currentIndex = i;
        break;
      }
    }
    int nextIndex = (currentIndex + direction + static_cast<int>(kProfiles.size())) % static_cast<int>(kProfiles.size());
    project_.panicProfile = kProfiles[nextIndex];
    triggerToast("panic profile: " + panicProfileLabelFromToken(project_.panicProfile));
    playUiSound(UiSoundEffect::Navigate);
    markProjectDirty();
  }

  void setPanicFadeSeconds(double seconds) {
    double next = std::clamp(std::isfinite(seconds) ? seconds : 0.9, 0.1, 5.0);
    if (std::abs(project_.panicFadeSeconds - next) < 0.001) {
      std::ostringstream msg;
      msg << std::fixed << std::setprecision(1) << next;
      triggerToast("panic fade " + msg.str() + "s");
      return;
    }
    project_.panicFadeSeconds = next;
    std::ostringstream msg;
    msg << std::fixed << std::setprecision(1) << project_.panicFadeSeconds;
    triggerToast("panic fade " + msg.str() + "s");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void adjustPanicFadeSeconds(double deltaSeconds) {
    setPanicFadeSeconds(project_.panicFadeSeconds + deltaSeconds);
  }

  void setPanicAutoRestoreEnabled(bool enabled) {
    if (project_.panicAutoRestore == enabled) {
      triggerToast(std::string("panic auto restore: ") + (enabled ? "on" : "off"));
      return;
    }
    project_.panicAutoRestore = enabled;
    triggerToast(std::string("panic auto restore: ") + (enabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setOscQueryEnabled(bool enabled) {
    normalizeProject(project_);
#if defined(_WIN32)
    project_.oscQueryEnabled = false;
    if (enabled) {
      triggerToast("osc query: unavailable");
      playUiSound(UiSoundEffect::Toggle);
    } else {
      triggerToast("osc query: off");
    }
    return;
#else
    if (project_.oscQueryEnabled == enabled) {
      triggerToast(std::string("osc query: ") + (enabled ? "on" : "off"));
      return;
    }
    project_.oscQueryEnabled = enabled;
    if (project_.oscQueryEnabled) {
      if (!startOscQueryServer()) {
        project_.oscQueryEnabled = false;
        triggerToast("osc query: unavailable");
        playUiSound(UiSoundEffect::Toggle);
        return;
      }
    } else {
      stopOscQueryServer();
    }
    triggerToast(std::string("osc query: ") + (project_.oscQueryEnabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
#endif
  }

  void setOscQueryPort(int port) {
    normalizeProject(project_);
    int normalized = normalizeOscQueryPort(port);
    if (project_.oscQueryPort == normalized) {
      triggerToast("osc query port: " + std::to_string(project_.oscQueryPort));
      return;
    }
    project_.oscQueryPort = normalized;
#if !defined(_WIN32)
    if (project_.oscQueryEnabled) {
      stopOscQueryServer();
      startOscQueryServer();
    }
#endif
    triggerToast("osc query port: " + std::to_string(project_.oscQueryPort));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setOscFeedbackMirrorEnabled(bool enabled) {
    normalizeProject(project_);
    if (project_.oscFeedbackMirrorEnabled == enabled) {
      triggerToast(std::string("osc feedback mirror: ") + (enabled ? "on" : "off"));
      return;
    }
    project_.oscFeedbackMirrorEnabled = enabled;
    lastOscMirrorFeedbackPayload_.clear();
    lastOscMirrorFeedbackBroadcastMs_ = 0;
    triggerToast(std::string("osc feedback mirror: ") + (enabled ? "on" : "off"));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setOscFeedbackRateMs(int rateMs) {
    normalizeProject(project_);
    int normalized = normalizeOscFeedbackRateMs(rateMs);
    if (project_.oscFeedbackRateMs == normalized) {
      triggerToast("osc feedback rate: " + std::to_string(project_.oscFeedbackRateMs) + " ms");
      return;
    }
    project_.oscFeedbackRateMs = normalized;
    triggerToast("osc feedback rate: " + std::to_string(project_.oscFeedbackRateMs) + " ms");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool runPanicOutputsOff(bool requireSafetyContext, Uint32 sourceWindowId) {
    normalizeProject(project_);

    bool fromOutputWindow = outputIndexForWindowId(sourceWindowId).has_value();
    bool anyWindowOutputEnabled = false;
    for (const auto& output : project_.outputs) {
      if (output.enabled && normalizeOutputType(output.outputType) == "window") {
        anyWindowOutputEnabled = true;
        break;
      }
    }
    if (requireSafetyContext && !fromOutputWindow && !anyWindowOutputEnabled) {
      return false;
    }

    bool anyEnabled = false;
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      OutputTarget& output = project_.outputs[outputIndex];
      if (output.enabled) {
        anyEnabled = true;
      }
      output.enabled = false;
      setOutputRecoveryPausedByEscape(outputIndex, false);
      stopOutputStream(outputIndex);
      applyOutputDisplaySelection(outputIndex);
    }

    panicProfilePending_ = false;
    pendingPanicProfileToken_.clear();
    panicProfileRequestedAt_ = 0;
    SDL_RaiseWindow(controlWindow_);
    triggerToast(anyEnabled ? "panic: outputs off" : "panic: outputs already off");
    playUiSound(UiSoundEffect::Toggle);
    if (anyEnabled) {
      markProjectDirty();
    }
    return true;
  }

  bool emergencyDisarmOutputsFromEsc(Uint32 sourceWindowId) {
    return runPanicOutputsOff(true, sourceWindowId);
  }

  void executePanicDeckAction(const std::string& token) {
    std::string profile = normalizePanicProfileToken(token);
    int savedFocus = project_.focusedDeckIndex;
    bool changed = false;
    if (profile == "fade_pause") {
      for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
        if (const Cue* activeCue = activeCuePtr(deckIndex); activeCue && activeCue->kind == CueKind::Browser) {
          stopBrowserCue(deckIndex);
          changed = true;
        }
        if (auto* engine = mediaEngineForDeck(deckIndex)) {
          engine->pause();
          changed = true;
        }
      }
    } else if (profile == "fade_rewind") {
      for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
        stopBrowserCue(deckIndex);
        if (auto* engine = mediaEngineForDeck(deckIndex)) {
          engine->stop();
          changed = true;
        }
      }
    } else if (profile == "fade_load_next") {
      for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
        Deck& deck = project_.decks[deckIndex];
        if (deck.cues.empty()) {
          continue;
        }
        int nextIndex = deck.selectedIndex < 0
          ? 0
          : std::clamp(deck.selectedIndex + 1, 0, static_cast<int>(deck.cues.size()) - 1);
        if (deck.selectedIndex != nextIndex) {
          deck.selectedIndex = nextIndex;
          changed = true;
        }
        project_.focusedDeckIndex = deckIndex;
        takeSelected(false, false);
        changed = true;
      }
    }
    project_.focusedDeckIndex = savedFocus;
    onSelectionChanged();
    if (project_.panicAutoRestore) {
      masterDimmerTarget_ = std::clamp(panicRestoreDimmerTarget_, 0.0, 1.0);
    }
    triggerToast("panic: " + panicProfileLabelFromToken(profile));
    playUiSound(UiSoundEffect::Stop);
    if (changed) {
      markProjectDirty();
    }
  }

  void triggerPanicProfile(std::optional<std::string> overrideProfile = std::nullopt) {
    normalizeProject(project_);
    std::string profile = overrideProfile
      ? normalizePanicProfileToken(*overrideProfile)
      : normalizePanicProfileToken(project_.panicProfile);
    if (profile == "outputs_off") {
      runPanicOutputsOff(false, 0);
      return;
    }
    pendingPanicProfileToken_ = profile;
    panicProfilePending_ = true;
    panicProfileRequestedAt_ = SDL_GetTicks64();
    panicRestoreDimmerTarget_ = std::clamp(masterDimmerTarget_, 0.0, 1.0);
    masterDimmerTarget_ = 0.0;
    triggerToast("panic arm: " + panicProfileLabelFromToken(profile));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool saveProjectNow(bool withToast = true) {
    normalizeProject(project_);
    if (currentProjectFile_.empty()) {
      currentProjectFile_ = defaultProjectFile();
    }
    bool ok = saveProject(currentProjectFile_, project_);
    if (ok) {
      projectDirty_ = false;
      if (withToast) {
        triggerToast("saved " + currentProjectLabel());
      }
    } else if (withToast) {
      triggerToast("save failed");
    }
    return ok;
  }

  bool startNewShow(bool withToast = true) {
    project_ = Project {};
    normalizeProject(project_);
    disarmAllOutputsForStartup();
    for (auto& deck : project_.decks) {
      deck.activeIndex = -1;
    }
    currentProjectFile_ = defaultProjectFile();
    updateDecksPanelVisibility();
    timecodeTriggeredCueIds_.clear();
    resetTimecodeFollowerState();
    selectionChangedAt_ = SDL_GetTicks64();
    if (!rebuildDeckRuntimes()) {
      std::cerr << "Deck runtime creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    if (!rebuildOutputRuntimes()) {
      std::cerr << "Output runtime creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    projectDirty_ = false;
    if (withToast) {
      triggerToast("new show");
    }
    return true;
  }

  void persistProject() {
    normalizeProject(project_);
    saveProject(currentProjectFile_, project_);
    projectDirty_ = false;
  }

  void markProjectDirty() {
    if (!projectDirty_) {
      projectDirty_ = true;
      projectDirtyAt_ = std::chrono::steady_clock::now();
    }
  }

  void flushDirtyProject() {
    if (!projectDirty_) {
      return;
    }
    auto age = std::chrono::steady_clock::now() - projectDirtyAt_;
    if (age >= std::chrono::milliseconds(300)) {
      persistProject();
    }
  }

  float computeVuLevel() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(vuSamplesMutex_));
    if (vuSamples_.empty()) return 0.0f;
    double sum = 0.0;
    for (auto s : vuSamples_) sum += static_cast<double>(s) * s;
    return static_cast<float>(std::sqrt(sum / vuSamples_.size()) / 32768.0);
  }

  void triggerWaveformAnalysis(const std::string& path) {
    if (path.empty()) return;
    std::lock_guard<std::mutex> lk(waveformMutex_);
    if (waveformCache_.count(path) || waveformFutures_.count(path)) return;
    waveformFutures_[path] = std::async(std::launch::async,
      [path]() { return computeWaveformPeaks(path); });
  }

  // Draw a waveform bar graph into dest. playFrac/inFrac/outFrac in [0,1].
  void drawWaveform(SDL_Renderer* ren, SDL_Rect dest, const std::vector<float>& peaks,
                    float playFrac, float inFrac, float outFrac,
                    const std::vector<double>& pausePoints = {}, double duration = 0.0) {
    Primitives::fillRect(ren, dest, colorFromRgba(kScreenDeepColor));
    Primitives::strokeRect(ren, dest, colorFromRgba(kScreenMidColor));
    if (peaks.empty()) {
      drawCenteredText(ren, fontSmall_, "analyzing...", colorFromRgba(kScreenInkSoftColor), dest);
      return;
    }
    int n  = static_cast<int>(peaks.size());
    int x0 = dest.x + 2, y0 = dest.y + 2;
    int w  = dest.w - 4, h = dest.h - 4;
    int cy = y0 + h / 2; // center line for symmetric waveform

    // Draw centre line
    SDL_SetRenderDrawColor(ren, 30, 50, 30, 255);
    SDL_RenderDrawLine(ren, x0, cy, x0 + w, cy);

    for (int i = 0; i < w; ++i) {
      int   pi   = std::min(i * n / std::max(1, w), n - 1);
      float peak = peaks[pi];
      int   halfH = std::max(1, static_cast<int>(peak * h / 2));
      float frac = static_cast<float>(i) / std::max(1, w);
      bool  inRange = (frac >= inFrac && frac <= outFrac);
      SDL_Color c = inRange ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor);
      SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
      // Symmetric: draw bar from centre up and down
      SDL_RenderDrawLine(ren, x0 + i, cy - halfH, x0 + i, cy + halfH);
    }
    // Pause point ticks (orange verticals)
    if (!pausePoints.empty() && duration > 0.0) {
      SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
      for (double pp : pausePoints) {
        float ppFrac = static_cast<float>(std::clamp(pp / duration, 0.0, 1.0));
        int px = x0 + static_cast<int>(ppFrac * w);
        SDL_SetRenderDrawColor(ren, 220, 120, 30, 200);
        SDL_RenderDrawLine(ren, px, y0, px, y0 + h);
      }
      SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    }
    // Playhead
    if (playFrac >= 0.0f && playFrac <= 1.0f) {
      int px = x0 + static_cast<int>(playFrac * w);
      SDL_SetRenderDrawColor(ren, 200, 220, 80, 255);
      SDL_RenderDrawLine(ren, px, y0, px, y0 + h);
    }
    // In/out markers
    auto drawMarker = [&](float frac, Uint8 r, Uint8 g, Uint8 b) {
      int mx = x0 + static_cast<int>(frac * std::max(1, w));
      SDL_SetRenderDrawColor(ren, r, g, b, 255);
      SDL_RenderDrawLine(ren, mx, y0, mx, y0 + h);
    };
    if (inFrac > 0.0f)  drawMarker(inFrac,  80, 220, 80);
    if (outFrac < 1.0f) drawMarker(outFrac, 220, 80, 80);
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
    markProjectDirty();
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
    markProjectDirty();
  }

  void setAutoAdvance(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.autoAdvance == enabled) {
      return;
    }
    deck.autoAdvance = enabled;
    triggerToast(deck.autoAdvance ? "auto next on" : "auto next off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
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
    markProjectDirty();
  }

  void togglePlaylistLoop() {
    setPlaylistLoop(!focusedDeck().playlistLoop);
  }

  void ensureDeckOpacityTargetsSize() {
    if (deckPlaylistOpacityTargets_.size() < project_.decks.size()) {
      size_t previous = deckPlaylistOpacityTargets_.size();
      deckPlaylistOpacityTargets_.resize(project_.decks.size(), 1.0f);
      for (size_t i = previous; i < project_.decks.size(); ++i) {
        deckPlaylistOpacityTargets_[i] = std::clamp(project_.decks[i].playlistOpacity, 0.0f, 1.0f);
      }
    } else if (deckPlaylistOpacityTargets_.size() > project_.decks.size()) {
      deckPlaylistOpacityTargets_.resize(project_.decks.size(), 1.0f);
    }
  }

  float deckPlaylistOpacityTarget(int deckIndex) {
    ensureDeckOpacityTargetsSize();
    if (deckIndex < 0 || deckIndex >= static_cast<int>(deckPlaylistOpacityTargets_.size())) {
      return 1.0f;
    }
    return std::clamp(deckPlaylistOpacityTargets_[deckIndex], 0.0f, 1.0f);
  }

  void setDeckPlaylistOpacityTarget(int deckIndex, float value) {
    ensureDeckOpacityTargetsSize();
    if (deckIndex < 0 || deckIndex >= static_cast<int>(deckPlaylistOpacityTargets_.size())) {
      return;
    }
    deckPlaylistOpacityTargets_[deckIndex] = std::clamp(value, 0.0f, 1.0f);
  }

  void setDeckPlaylistOpacity(int deckIndex, float value, bool immediate = true) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    float clamped = std::clamp(value, 0.0f, 1.0f);
    if (std::fabs(deck.playlistOpacity - clamped) < 0.001f && std::fabs(deckPlaylistOpacityTarget(deckIndex) - clamped) < 0.001f) {
      int pct = static_cast<int>(std::lround(clamped * 100.0f));
      triggerToast("deck opacity: " + std::to_string(pct) + "%");
      return;
    }
    if (immediate) {
      deck.playlistOpacity = clamped;
    }
    setDeckPlaylistOpacityTarget(deckIndex, clamped);
    int pct = static_cast<int>(std::lround(clamped * 100.0f));
    triggerToast("deck opacity: " + std::to_string(pct) + "%");
    markProjectDirty();
  }

  void setFocusedDeckPlaylistOpacity(float value, bool immediate = true) {
    setDeckPlaylistOpacity(project_.focusedDeckIndex, value, immediate);
  }

  void setFocusedDeckPlaylistAutoFade(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.playlistAutoFade == enabled) {
      return;
    }
    deck.playlistAutoFade = enabled;
    triggerToast(deck.playlistAutoFade ? "auto fade: on" : "auto fade: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleFocusedDeckPlaylistAutoFade() {
    setFocusedDeckPlaylistAutoFade(!focusedDeck().playlistAutoFade);
  }

  void setFocusedDeckPlaylistFadeSeconds(double seconds) {
    Deck& deck = focusedDeckMutable();
    double clamped = std::clamp(std::isfinite(seconds) ? seconds : deck.playlistFadeSeconds, 0.05, 10.0);
    if (std::abs(deck.playlistFadeSeconds - clamped) < 0.001) {
      std::ostringstream label;
      label << std::fixed << std::setprecision(2) << deck.playlistFadeSeconds;
      triggerToast("deck fade: " + label.str() + "s");
      return;
    }
    deck.playlistFadeSeconds = clamped;
    std::ostringstream label;
    label << std::fixed << std::setprecision(2) << deck.playlistFadeSeconds;
    triggerToast("deck fade: " + label.str() + "s");
    markProjectDirty();
  }

  std::vector<int> selectedCueIndices(const Deck& deck) const {
    std::vector<int> result;
    result.reserve(deck.selectedIndices.size() + 1);
    std::unordered_set<int> seen;
    for (int index : deck.selectedIndices) {
      if (index < 0 || index >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      if (seen.insert(index).second) {
        result.push_back(index);
      }
    }
    if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
      if (seen.insert(deck.selectedIndex).second) {
        result.push_back(deck.selectedIndex);
      }
    }
    std::sort(result.begin(), result.end());
    return result;
  }

  bool cueIndexSelected(const Deck& deck, int cueIndex) const {
    if (cueIndex < 0) {
      return false;
    }
    if (deck.selectedIndex == cueIndex) {
      return true;
    }
    return std::find(deck.selectedIndices.begin(), deck.selectedIndices.end(), cueIndex) != deck.selectedIndices.end();
  }

  template <typename Fn>
  bool forEachFocusedSelectedCueMutable(Fn&& fn) {
    Deck& deck = focusedDeckMutable();
    auto indices = selectedCueIndices(deck);
    if (indices.empty()) {
      return false;
    }
    bool changed = false;
    for (int index : indices) {
      if (index < 0 || index >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      fn(deck.cues[index], index);
      changed = true;
    }
    return changed;
  }

  template <typename Pred>
  Cue* firstFocusedSelectedCueMutable(Pred&& pred) {
    Deck& deck = focusedDeckMutable();
    auto indices = selectedCueIndices(deck);
    for (int index : indices) {
      if (index < 0 || index >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      Cue& cue = deck.cues[index];
      if (pred(cue)) {
        return &cue;
      }
    }
    return nullptr;
  }

  void selectCueInDeck(int deckIndex, int cueIndex, bool extendRange, bool toggleSingle) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    if (extendRange) {
      int anchor = deck.selectedIndex >= 0 ? deck.selectedIndex : cueIndex;
      int start = std::min(anchor, cueIndex);
      int end = std::max(anchor, cueIndex);
      deck.selectedIndices.clear();
      for (int i = start; i <= end; ++i) {
        deck.selectedIndices.push_back(i);
      }
      deck.selectedIndex = cueIndex;
    } else if (toggleSingle) {
      auto it = std::find(deck.selectedIndices.begin(), deck.selectedIndices.end(), cueIndex);
      if (it == deck.selectedIndices.end()) {
        deck.selectedIndices.push_back(cueIndex);
      } else if (deck.selectedIndices.size() > 1 || deck.selectedIndex != cueIndex) {
        deck.selectedIndices.erase(it);
      }
      deck.selectedIndex = cueIndex;
    } else {
      deck.selectedIndex = cueIndex;
      deck.selectedIndices.clear();
      deck.selectedIndices.push_back(cueIndex);
    }
    onSelectionChanged();
    markProjectDirty();
  }

  void onSelectionChanged() {
    Deck& deck = focusedDeckMutable();
    if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
      if (std::find(deck.selectedIndices.begin(), deck.selectedIndices.end(), deck.selectedIndex) == deck.selectedIndices.end()) {
        deck.selectedIndices.push_back(deck.selectedIndex);
      }
    }
    deck.selectedIndices.erase(
      std::remove_if(deck.selectedIndices.begin(), deck.selectedIndices.end(), [&](int index) {
        return index < 0 || index >= static_cast<int>(deck.cues.size());
      }),
      deck.selectedIndices.end());
    std::sort(deck.selectedIndices.begin(), deck.selectedIndices.end());
    deck.selectedIndices.erase(std::unique(deck.selectedIndices.begin(), deck.selectedIndices.end()), deck.selectedIndices.end());

    selectionChangedAt_ = SDL_GetTicks64();
    cueSettingsScroll_ = 0;
    cueSettingsScrollMax_ = 0;
    playUiSound(UiSoundEffect::Navigate);
    if (const Cue* cue = selectedCuePtr()) {
      requestThumbnail(*cue);
    } else {
      clearSelectedThumbnail();
    }
  }

  void clearSelectedThumbnail() {
    selectedThumbnailCueId_.clear();
    if (thumbnailThread_.joinable()) {
      thumbnailProcess_.stop();
      thumbnailThread_.join();
    }
    {
      std::lock_guard<std::mutex> lk(thumbnailMutex_);
      pendingThumbnail_.reset();
    }
    thumbnailPending_.store(false);
    if (selectedThumbnailTex_) {
      SDL_DestroyTexture(selectedThumbnailTex_);
      selectedThumbnailTex_ = nullptr;
      selectedThumbnailTexW_ = 0;
      selectedThumbnailTexH_ = 0;
    }
  }

  void requestThumbnail(const Cue& cue) {
    if (cue.kind != CueKind::Video && cue.kind != CueKind::Image) {
      clearSelectedThumbnail();
      return;
    }
    if (selectedThumbnailCueId_ == cue.id) {
      return;  // already loaded or loading
    }
    selectedThumbnailCueId_ = cue.id;
    if (thumbnailThread_.joinable()) {
      thumbnailProcess_.stop();
      thumbnailThread_.join();
    }
    {
      std::lock_guard<std::mutex> lk(thumbnailMutex_);
      pendingThumbnail_.reset();
    }
    thumbnailPending_.store(false);

    constexpr int kThumbW = 320;
    constexpr int kThumbH = 180;
    std::string scaleFilter =
      "scale=" + std::to_string(kThumbW) + ":" + std::to_string(kThumbH) +
      ":force_original_aspect_ratio=decrease:flags=lanczos,"
      "pad=" + std::to_string(kThumbW) + ":" + std::to_string(kThumbH) + ":(ow-iw)/2:(oh-ih)/2";

    std::vector<std::string> args = {"ffmpeg", "-hide_banner", "-loglevel", "error"};
    if (cue.kind == CueKind::Video && cue.duration > 0.5) {
      double seekPos = std::min(cue.duration * 0.1, cue.duration - 0.1);
      args.push_back("-ss");
      args.push_back(std::to_string(std::max(0.0, seekPos)));
    }
    args.insert(args.end(), {"-i", cue.path, "-frames:v", "1",
                             "-vf", scaleFilter,
                             "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"});

    if (!spawnPipeProcess(thumbnailProcess_, args)) {
      return;
    }
    int fd = thumbnailProcess_.readFd;
    thumbnailThread_ = std::thread([this, fd]() {
      constexpr int kThumbW = 320;
      constexpr int kThumbH = 180;
      DecodedFrame frame;
      frame.width = kThumbW;
      frame.height = kThumbH;
      frame.index = 0;
      frame.pixels.resize(static_cast<size_t>(kThumbW) * kThumbH * 4u);
      if (readExact(fd, frame.pixels.data(), frame.pixels.size())) {
        std::lock_guard<std::mutex> lk(thumbnailMutex_);
        pendingThumbnail_ = std::move(frame);
        thumbnailPending_.store(true);
      }
    });
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
  std::string oscSenderKey(const sockaddr_in& sender) const {
    char host[INET_ADDRSTRLEN] {};
    const char* text = inet_ntop(AF_INET, &sender.sin_addr, host, sizeof(host));
    if (!text) {
      return "unknown:" + std::to_string(ntohs(sender.sin_port));
    }
    return std::string(text) + ":" + std::to_string(ntohs(sender.sin_port));
  }

  void rememberOscSubscriber(const sockaddr_in& sender) {
    oscSubscribers_[oscSenderKey(sender)] = {sender, SDL_GetTicks64()};
  }

  void sendOscStringTo(const sockaddr_in& target, const std::string& address, const std::string& payload) {
    if (companionUdpSocket_ == kInvalidSocket) {
      return;
    }
    std::vector<std::uint8_t> message = buildOscStringMessage(address, payload);
    sendto(
      companionUdpSocket_,
      message.data(),
      message.size(),
      0,
      reinterpret_cast<const sockaddr*>(&target),
      sizeof(target)
    );
  }

  std::string snapshotJsonForFeedback() {
    std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
    if (!statusSnapshotJson_.empty()) {
      return statusSnapshotJson_;
    }
    return "{\"app\":\"DECKBOY_0.01\",\"deckCount\":0,\"decks\":[]}\n";
  }

  std::vector<std::pair<std::string, std::string>> buildOscMirrorFeedbackValues() const {
    std::vector<std::pair<std::string, std::string>> values;
    values.reserve(8 + project_.decks.size() * 5 + project_.outputs.size() * 8);
    values.emplace_back("/playboy/focus/deck", std::to_string(project_.focusedDeckIndex + 1));
    values.emplace_back("/playboy/focus/output", std::to_string(project_.focusedOutputIndex + 1));
    values.emplace_back("/playboy/decks/count", std::to_string(project_.decks.size()));
    values.emplace_back("/playboy/outputs/count", std::to_string(project_.outputs.size()));
    values.emplace_back("/playboy/jump_mode", normalizeJumpModeToken(project_.jumpMode));
    values.emplace_back("/playboy/panic_profile", normalizePanicProfileToken(project_.panicProfile));

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      const Deck& deck = project_.decks[deckIndex];
      std::string prefix = "/playboy/deck/" + std::to_string(deckIndex + 1);
      values.emplace_back(prefix + "/status", transportStatusLabel(deckIndex));
      values.emplace_back(prefix + "/selected", std::to_string(deck.selectedIndex >= 0 ? deck.selectedIndex + 1 : 0));
      values.emplace_back(prefix + "/active", std::to_string(deck.activeIndex >= 0 ? deck.activeIndex + 1 : 0));
      values.emplace_back(prefix + "/opacity", std::to_string(static_cast<int>(std::lround(std::clamp(deck.playlistOpacity, 0.0f, 1.0f) * 100.0f))));
      auto outputIndex = primaryOutputIndexForDeck(deckIndex);
      values.emplace_back(prefix + "/route_output", std::to_string(outputIndex ? *outputIndex + 1 : 0));
      values.emplace_back(prefix + "/layer", std::to_string(primaryLayerIndexForDeck(deckIndex)));
    }

    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      const OutputTarget& output = project_.outputs[outputIndex];
      std::string prefix = "/playboy/output/" + std::to_string(outputIndex + 1);
      values.emplace_back(prefix + "/enabled", output.enabled ? "1" : "0");
      values.emplace_back(prefix + "/type", normalizeOutputType(output.outputType));
      values.emplace_back(prefix + "/ndi", output.ndiEnabled ? "1" : "0");
      values.emplace_back(prefix + "/stream", output.streamEnabled ? "1" : "0");
      values.emplace_back(prefix + "/alpha", std::to_string(static_cast<int>(std::lround(std::clamp(output.outputAlpha, 0.0f, 1.0f) * 100.0f))));
      values.emplace_back(prefix + "/layout", normalizeOutputLayoutMode(output.outputLayoutMode));
      values.emplace_back(prefix + "/orientation", std::to_string(normalizeOutputOrientationDegrees(output.outputOrientationDegrees)));
      values.emplace_back(prefix + "/testcard", output.outputTestCardEnabled ? "1" : "0");
    }
    return values;
  }

  std::string buildOscQueryDocumentJson() {
    std::string stateJson = trim(snapshotJsonForFeedback());
    if (stateJson.empty()) {
      stateJson = "{\"app\":\"DECKBOY_0.01\",\"deckCount\":0,\"decks\":[]}";
    }

    std::ostringstream output;
    output << "{"
           << "\"name\":\"Deckboy OSC Query\","
           << "\"app\":\"DECKBOY_0.01\","
           << "\"oscUdpPort\":" << companionPort_ << ","
           << "\"httpPort\":" << project_.oscQueryPort << ","
           << "\"feedbackMirror\":" << (project_.oscFeedbackMirrorEnabled ? "true" : "false") << ","
           << "\"feedbackRateMs\":" << project_.oscFeedbackRateMs << ","
           << "\"endpoints\":[";
    for (size_t i = 0; i < kOscQueryEndpoints.size(); ++i) {
      if (i > 0) {
        output << ",";
      }
      const auto& endpoint = kOscQueryEndpoints[i];
      output << "{"
             << "\"path\":\"" << escapeJson(endpoint.path) << "\","
             << "\"command\":\"" << escapeJson(endpoint.command) << "\","
             << "\"args\":\"" << escapeJson(endpoint.args) << "\","
             << "\"notes\":\"" << escapeJson(endpoint.notes) << "\""
             << "}";
    }
    output << "],"
           << "\"state\":" << stateJson
           << "}\n";
    return output.str();
  }

  std::string buildOscQueryHtmlPage() {
    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset='utf-8'>"
         << "<title>Deckboy OSC Query</title>"
         << "<style>"
         << "body{font-family:monospace;background:#0f380f;color:#9bbc0f;margin:0;padding:18px;}"
         << "h1{margin:0 0 6px 0;font-size:22px;}"
         << "p{margin:4px 0;}"
         << "a{color:#c9d7a3;text-decoration:none;}"
         << "table{border-collapse:collapse;width:100%;margin-top:10px;}"
         << "th,td{border:1px solid #306230;padding:6px;text-align:left;font-size:12px;}"
         << "th{background:#306230;color:#9bbc0f;}"
         << "pre{background:#101410;border:1px solid #306230;padding:8px;overflow:auto;}"
         << "</style></head><body>";
    html << "<h1>DECKBOY OSC QUERY</h1>";
    html << "<p>OSC UDP Port: " << companionPort_ << " | HTTP Port: " << project_.oscQueryPort << "</p>";
    html << "<p>Feedback Mirror: " << (project_.oscFeedbackMirrorEnabled ? "ON" : "OFF")
         << " @ " << project_.oscFeedbackRateMs << " ms</p>";
    html << "<p><a href='/oscquery.json'>/oscquery.json</a> | <a href='/state.json'>/state.json</a></p>";
    html << "<table><tr><th>Path</th><th>Command</th><th>Args</th><th>Notes</th></tr>";
    for (const auto& endpoint : kOscQueryEndpoints) {
      html << "<tr><td>" << escapeHtml(endpoint.path) << "</td>"
           << "<td>" << escapeHtml(endpoint.command) << "</td>"
           << "<td>" << escapeHtml(endpoint.args) << "</td>"
           << "<td>" << escapeHtml(endpoint.notes) << "</td></tr>";
    }
    html << "</table>";
    html << "<h2>State</h2><pre>" << escapeHtml(trim(snapshotJsonForFeedback())) << "</pre>";
    html << "</body></html>";
    return html.str();
  }

  void sendHttpResponse(SocketHandle client,
                        const std::string& statusLine,
                        const std::string& contentType,
                        const std::string& body) {
    std::ostringstream header;
    header << "HTTP/1.1 " << statusLine << "\r\n"
           << "Content-Type: " << contentType << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n"
           << "Cache-Control: no-cache\r\n"
           << "\r\n";
    std::string response = header.str();
    response += body;
    send(client, response.c_str(), response.size(), MSG_NOSIGNAL);
  }

  void handleOscQueryHttpClient(SocketHandle client) {
    std::string request;
    std::array<char, 2048> buffer {};
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
      ssize_t bytes = recv(client, buffer.data(), buffer.size(), 0);
      if (bytes <= 0) {
        break;
      }
      request.append(buffer.data(), static_cast<size_t>(bytes));
    }
    if (request.empty()) {
      return;
    }

    size_t lineEnd = request.find("\r\n");
    if (lineEnd == std::string::npos) {
      lineEnd = request.find('\n');
    }
    std::string requestLine = lineEnd == std::string::npos ? request : request.substr(0, lineEnd);
    auto parts = splitWhitespace(requestLine);
    if (parts.size() < 2 || toUpper(parts[0]) != "GET") {
      sendHttpResponse(client, "405 Method Not Allowed", "text/plain; charset=utf-8",
                       "Only GET is supported.\n");
      return;
    }

    std::string path = parts[1];
    size_t queryPos = path.find('?');
    if (queryPos != std::string::npos) {
      path = path.substr(0, queryPos);
    }
    if (path.empty()) {
      path = "/";
    }

    if (path == "/") {
      sendHttpResponse(client, "200 OK", "text/html; charset=utf-8", buildOscQueryHtmlPage());
      return;
    }
    if (path == "/oscquery" || path == "/oscquery.json") {
      sendHttpResponse(client, "200 OK", "application/json; charset=utf-8", buildOscQueryDocumentJson());
      return;
    }
    if (path == "/state" || path == "/state.json") {
      sendHttpResponse(client, "200 OK", "application/json; charset=utf-8", snapshotJsonForFeedback());
      return;
    }

    sendHttpResponse(client, "404 Not Found", "text/plain; charset=utf-8",
                     "Deckboy OSC Query endpoint not found.\n");
  }

  bool startOscQueryServer() {
    if (!project_.oscQueryEnabled) {
      oscQueryReady_ = false;
      return false;
    }
    if (oscQueryTcpListen_ != kInvalidSocket) {
      return true;
    }

    oscQueryTcpListen_ = createBoundSocket(SOCK_STREAM, project_.oscQueryPort, true);
    if (oscQueryTcpListen_ == kInvalidSocket) {
      oscQueryReady_ = false;
      return false;
    }

    oscQueryStop_.store(false);
    oscQueryThread_ = std::thread([this]() {
      oscQueryLoop();
    });
    oscQueryReady_ = true;
    return true;
  }

  void stopOscQueryServer() {
    oscQueryStop_.store(true);
    if (oscQueryTcpListen_ != kInvalidSocket) {
      closeSocket(oscQueryTcpListen_);
      oscQueryTcpListen_ = kInvalidSocket;
    }
    if (oscQueryThread_.joinable()) {
      oscQueryThread_.join();
    }
    oscQueryReady_ = false;
  }

  void oscQueryLoop() {
    while (!oscQueryStop_.load()) {
      if (oscQueryTcpListen_ == kInvalidSocket) {
        break;
      }
      fd_set readFds;
      FD_ZERO(&readFds);
      FD_SET(oscQueryTcpListen_, &readFds);
      timeval timeout {};
      timeout.tv_sec = 0;
      timeout.tv_usec = 200000;
      int ready = select(oscQueryTcpListen_ + 1, &readFds, nullptr, nullptr, &timeout);
      if (ready < 0) {
        if (errno == EINTR) {
          continue;
        }
        break;
      }
      if (ready == 0) {
        continue;
      }
      if (!FD_ISSET(oscQueryTcpListen_, &readFds)) {
        continue;
      }

      sockaddr_in clientAddress {};
      socklen_t clientLength = sizeof(clientAddress);
      SocketHandle client = accept(oscQueryTcpListen_, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
      if (client < 0) {
        continue;
      }
      handleOscQueryHttpClient(client);
      closeSocket(client);
    }
  }

  void maybeBroadcastOscState() {
    Uint64 now = SDL_GetTicks64();
    if (oscSubscribers_.empty()) {
      return;
    }

    std::string snapshot = snapshotJsonForFeedback();
    bool stateChanged = snapshot != lastOscFeedbackPayload_;
    bool shouldBroadcastState = stateChanged || now - lastOscFeedbackBroadcastMs_ >= 2000;

    std::vector<std::pair<std::string, std::string>> mirrorValues;
    std::string mirrorDigest;
    bool mirrorChanged = false;
    Uint64 mirrorInterval = static_cast<Uint64>(normalizeOscFeedbackRateMs(project_.oscFeedbackRateMs));
    bool shouldBroadcastMirror = false;
    if (project_.oscFeedbackMirrorEnabled) {
      mirrorValues = buildOscMirrorFeedbackValues();
      std::ostringstream mirrorDigestStream;
      for (const auto& value : mirrorValues) {
        mirrorDigestStream << value.first << '=' << value.second << '\n';
      }
      mirrorDigest = mirrorDigestStream.str();
      mirrorChanged = mirrorDigest != lastOscMirrorFeedbackPayload_;
      shouldBroadcastMirror =
        (mirrorChanged && now - lastOscMirrorFeedbackBroadcastMs_ >= mirrorInterval) ||
        (!mirrorChanged && now - lastOscMirrorFeedbackBroadcastMs_ >= 2000);
    }

    if (!shouldBroadcastState && !shouldBroadcastMirror) {
      return;
    }

    std::vector<std::string> stale;
    for (const auto& [key, entry] : oscSubscribers_) {
      if (now > entry.second + 30000) {
        stale.push_back(key);
        continue;
      }
      if (shouldBroadcastState) {
        sendOscStringTo(entry.first, "/playboy/state", snapshot);
      }
      if (shouldBroadcastMirror) {
        for (const auto& value : mirrorValues) {
          sendOscStringTo(entry.first, value.first, value.second);
        }
      }
    }
    for (const auto& key : stale) {
      oscSubscribers_.erase(key);
    }

    if (shouldBroadcastState) {
      lastOscFeedbackPayload_ = snapshot;
      lastOscFeedbackBroadcastMs_ = now;
    }
    if (shouldBroadcastMirror) {
      lastOscMirrorFeedbackPayload_ = mirrorDigest;
      lastOscMirrorFeedbackBroadcastMs_ = now;
    }
  }

  bool maybeRespondToCompanionQuery(SocketHandle client, const std::string& line) {
    std::string query = trim(line);
    std::string upper = toUpper(query);

    auto sendSnapshot = [&](const std::string& payload) {
      if (!payload.empty()) {
        send(client, payload.c_str(), payload.size(), MSG_NOSIGNAL);
      }
    };

    if (upper == "STATUS JSON" || upper == "STATE JSON") {
      std::string snapshotJson;
      {
        std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
        snapshotJson = statusSnapshotJson_;
      }
      if (snapshotJson.empty()) {
        snapshotJson = "{\"app\":\"DECKBOY_0.01\",\"deckCount\":0,\"decks\":[]}\n";
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
        snapshot = "DECKBOY_0.01 decks=0\n";
      }
      sendSnapshot(snapshot);
      return true;
    }
    if (upper == "STATUS CUES" || upper == "STATE CUES") {
      std::string cueSnapshot;
      {
        std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
        cueSnapshot = statusCueSnapshot_;
      }
      if (cueSnapshot.empty()) {
        cueSnapshot = "DECKBOY_0.01 cues decks=0\n";
      }
      sendSnapshot(cueSnapshot);
      return true;
    }
    if (upper == "FINDSTATUS" || upper == "STATUS FIND" || upper == "STATE FIND") {
      std::string cueSnapshot;
      {
        std::lock_guard<std::mutex> lock(statusSnapshotMutex_);
        cueSnapshot = statusCueSnapshot_;
      }
      if (cueSnapshot.empty()) {
        cueSnapshot = "DECKBOY_0.01 cues find=none\n";
      } else {
        size_t newline = cueSnapshot.find('\n');
        cueSnapshot = newline == std::string::npos
          ? cueSnapshot + "\n"
          : cueSnapshot.substr(0, newline + 1);
      }
      sendSnapshot(cueSnapshot);
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
    oscSubscribers_.clear();
    lastOscFeedbackPayload_.clear();
    lastOscFeedbackBroadcastMs_ = 0;
    lastOscMirrorFeedbackPayload_.clear();
    lastOscMirrorFeedbackBroadcastMs_ = 0;
    closeSocket(companionTcpListen_);
    closeSocket(companionUdpSocket_);
    companionTcpListen_ = kInvalidSocket;
    companionUdpSocket_ = kInvalidSocket;
    companionReady_ = false;
#endif
  }

  void startHyperDeckServer() {
#ifndef _WIN32
    const char* portEnv = std::getenv("PLAYBOY_HYPERDECK_PORT");
    if (portEnv && *portEnv) {
      try { hyperDeckPort_ = std::clamp(std::stoi(portEnv), 1, 65535); } catch (...) {}
    }
    hyperDeckListenFd_ = createBoundSocket(SOCK_STREAM, hyperDeckPort_, true);
    if (hyperDeckListenFd_ == kInvalidSocket) return;
    hyperDeckRunning_.store(true);
    hyperDeckThread_ = std::thread([this]() { hyperDeckLoop(); });
#endif
  }

  void stopHyperDeckServer() {
#ifndef _WIN32
    hyperDeckRunning_.store(false);
    if (hyperDeckListenFd_ != kInvalidSocket) {
      closeSocket(hyperDeckListenFd_);
      hyperDeckListenFd_ = kInvalidSocket;
    }
    if (hyperDeckThread_.joinable()) hyperDeckThread_.join();
#endif
  }

  bool startMidiInput() {
#if defined(PLAYBOY_HAS_ALSA)
    stopMidiInput();
    midiStop_ = false;
    int err = snd_seq_open(&midiSeq_, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK);
    if (err < 0) { midiSeq_ = nullptr; return false; }
    snd_seq_set_client_name(midiSeq_, "Deckboy");
    midiSeqPort_ = snd_seq_create_simple_port(midiSeq_, "input",
      SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
      SND_SEQ_PORT_TYPE_APPLICATION);
    if (midiSeqPort_ < 0) { snd_seq_close(midiSeq_); midiSeq_ = nullptr; return false; }

    // If a specific port was configured, connect it
    if (!midiDeviceName_.empty()) {
      snd_seq_addr_t sender;
      if (snd_seq_parse_address(midiSeq_, &sender, midiDeviceName_.c_str()) == 0) {
        snd_seq_port_subscribe_t* sub;
        snd_seq_port_subscribe_alloca(&sub);
        snd_seq_port_subscribe_set_sender(sub, &sender);
        snd_seq_addr_t dest {static_cast<unsigned char>(snd_seq_client_id(midiSeq_)), static_cast<unsigned char>(midiSeqPort_)};
        snd_seq_port_subscribe_set_dest(sub, &dest);
        snd_seq_subscribe_port(midiSeq_, sub);
      }
    }

    midiThread_ = std::thread([this]() { midiLoop(); });
    return true;
#else
    return false;
#endif
  }

  void stopMidiInput() {
#if defined(PLAYBOY_HAS_ALSA)
    midiStop_ = true;
    if (midiThread_.joinable()) midiThread_.join();
    if (midiSeq_) {
      if (midiSeqPort_ >= 0) snd_seq_delete_port(midiSeq_, midiSeqPort_);
      snd_seq_close(midiSeq_);
      midiSeq_ = nullptr;
      midiSeqPort_ = -1;
    }
#endif
  }

  void midiLoop() {
#if defined(PLAYBOY_HAS_ALSA)
    while (!midiStop_) {
      snd_seq_event_t* ev = nullptr;
      int r = snd_seq_event_input(midiSeq_, &ev);
      if (r < 0) {
        if (r == -EAGAIN) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        break;
      }
      if (!ev) continue;

      std::string cmd;
      switch (ev->type) {
        case SND_SEQ_EVENT_NOTEON:
          if (ev->data.note.velocity > 0) {
            // Note On: trigger cue at note index (1-based for GOTO)
            cmd = "GOTO " + std::to_string(ev->data.note.note + 1);
          }
          break;
        case SND_SEQ_EVENT_CONTROLLER:
          if (ev->data.control.param == 7) {
            // CC7 = volume: 0-127 -> 0-200%
            int pct = static_cast<int>(ev->data.control.value * 200.0 / 127.0);
            cmd = "MASTERVOL " + std::to_string(pct);
          } else if (ev->data.control.param == 20) {
            // CC20 = speed: 0-127 -> 0.5x-2.0x
            double spd = 0.5 + ev->data.control.value * 1.5 / 127.0;
            std::ostringstream ss; ss << std::fixed << std::setprecision(2) << spd;
            cmd = "SPEED " + ss.str();
          }
          break;
        case SND_SEQ_EVENT_SYSEX: {
          // Check for MMC (F0 7F <dev> 06 <cmd> F7)
          auto* data = static_cast<unsigned char*>(ev->data.ext.ptr);
          size_t len = ev->data.ext.len;
          if (len >= 6 && data[0] == 0xF0 && data[1] == 0x7F && data[3] == 0x06) {
            switch (data[4]) {
              case 0x01: cmd = "STOP"; break;   // MMC Stop
              case 0x02: cmd = "PLAY"; break;   // MMC Play
              case 0x03: cmd = "PLAY"; break;   // MMC Deferred Play
              case 0x05: cmd = "STOP"; break;   // MMC Record Exit (treat as stop)
              case 0x44: {                        // MMC Goto
                if (len >= 12 && data[5] == 0x06) {
                  // Timecode target: data[7]=hr, data[8]=min, data[9]=sec, data[10]=fr
                  double secs = data[7] * 3600.0 + data[8] * 60.0 + data[9] + data[10] / 30.0;
                  cmd = "SEEKPOS " + std::to_string(secs);
                }
                break;
              }
              default: break;
            }
          }
          // Check for MSC (F0 7F <dev> 02 <cmdFmt> 01 <cueNum> F7)
          if (len >= 7 && data[0] == 0xF0 && data[1] == 0x7F && data[3] == 0x02 && data[5] == 0x01) {
            int cueNum = data[6]; // cue number
            cmd = "GOTO " + std::to_string(cueNum);
          }
          break;
        }
        default: break;
      }

      if (!cmd.empty()) {
        std::lock_guard<std::mutex> lk(remoteCommandMutex_);
        remoteCommands_.push_back(std::move(cmd));
      }
      snd_seq_free_event(ev);
    }
#endif
  }

#ifndef _WIN32
  void hyperDeckLoop() {
    // Each connected client gets a simple blocking handler in this loop.
    // We only handle one client at a time (sufficient for HyperDeck use).
    while (hyperDeckRunning_.load()) {
      fd_set readFds;
      FD_ZERO(&readFds);
      FD_SET(hyperDeckListenFd_, &readFds);
      timeval tv {0, 100000};  // 100ms timeout
      if (select(hyperDeckListenFd_ + 1, &readFds, nullptr, nullptr, &tv) <= 0) continue;
      sockaddr_in clientAddr {};
      socklen_t addrLen = sizeof(clientAddr);
      int clientFd = accept(hyperDeckListenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
      if (clientFd < 0) continue;
      // Greet
      const char* greeting =
        "500 connection info:\r\n"
        "protocol version: 1.11\r\n"
        "model: HyperDeck Studio Mini\r\n"
        "\r\n";
      send(clientFd, greeting, strlen(greeting), MSG_NOSIGNAL);
      // Handle client
      std::string buf;
      while (hyperDeckRunning_.load()) {
        char tmp[256];
        int n = recv(clientFd, tmp, sizeof(tmp) - 1, 0);
        if (n <= 0) break;
        tmp[n] = '\0';
        buf += tmp;
        // Process complete lines (\r\n terminated)
        size_t pos;
        while ((pos = buf.find("\r\n")) != std::string::npos) {
          std::string line = buf.substr(0, pos);
          buf.erase(0, pos + 2);
          // Trim trailing \r if any
          while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
          if (line.empty()) continue;

          std::string resp = hyperDeckHandleCommand(line);
          if (!resp.empty()) {
            send(clientFd, resp.c_str(), resp.size(), MSG_NOSIGNAL);
          }
        }
      }
      close(clientFd);
    }
  }

  std::string hyperDeckHandleCommand(const std::string& line) {
    // Parse "verb: key: val" style
    std::string cmd = line;
    // Lowercase for matching
    std::string cmdL;
    for (char ch : cmd) cmdL += std::tolower(static_cast<unsigned char>(ch));

    auto enqueueAndWait = [this](const std::string& rc) {
      enqueueRemoteCommand(rc);
    };

    if (cmdL == "play") {
      enqueueAndWait("PLAY");
      return "200 ok\r\n\r\n";
    }
    if (cmdL == "stop") {
      enqueueAndWait("STOP");
      return "200 ok\r\n\r\n";
    }
    if (cmdL.rfind("goto:", 0) == 0) {
      // "goto: clip id: N" or "goto: timeline: HH:MM:SS:FF"
      size_t ci = cmdL.find("clip id:");
      if (ci != std::string::npos) {
        std::string numStr = cmd.substr(ci + 8);
        while (!numStr.empty() && (numStr[0] == ' ' || numStr[0] == '\t')) numStr.erase(0, 1);
        enqueueAndWait("GOTO " + numStr);
        return "200 ok\r\n\r\n";
      }
      size_t ti = cmdL.find("timeline:");
      if (ti != std::string::npos) {
        std::string tcStr = cmd.substr(ti + 9);
        while (!tcStr.empty() && (tcStr[0] == ' ' || tcStr[0] == '\t')) tcStr.erase(0, 1);
        enqueueAndWait("TC SET " + tcStr);
        return "200 ok\r\n\r\n";
      }
      return "200 ok\r\n\r\n";
    }
    if (cmdL.rfind("play range:", 0) == 0) {
      size_t ci = cmdL.find("clip id:");
      if (ci != std::string::npos) {
        std::string numStr = cmd.substr(ci + 8);
        while (!numStr.empty() && (numStr[0] == ' ' || numStr[0] == '\t')) numStr.erase(0, 1);
        enqueueAndWait("GOTO " + numStr);
        enqueueAndWait("PLAY");
        return "200 ok\r\n\r\n";
      }
      return "200 ok\r\n\r\n";
    }
    if (cmdL == "transport info") {
      // Build response with current state
      std::string status = "stopped";
      {
        std::lock_guard<std::mutex> lk(statusSnapshotMutex_);
        if (statusSnapshot_.find("playing") != std::string::npos) status = "play";
        else if (statusSnapshot_.find("paused") != std::string::npos) status = "paused";
      }
      const Deck& deck = focusedDeck();
      int clipId = deck.activeIndex + 1;
      std::ostringstream resp;
      resp << "208 transport info:\r\n"
           << "status: " << status << "\r\n"
           << "speed: 100\r\n"
           << "slot id: 1\r\n"
           << "clip id: " << clipId << "\r\n"
           << "single clip: false\r\n"
           << "display timecode: 00:00:00:00\r\n"
           << "timecode: 00:00:00:00\r\n"
           << "video format: 1080p25\r\n"
           << "loop: " << (deck.playlistLoop ? "true" : "false") << "\r\n"
           << "\r\n";
      return resp.str();
    }
    if (cmdL == "clips count") {
      const Deck& deck = focusedDeck();
      std::ostringstream resp;
      resp << "214 clips count:\r\n"
           << "clip count: " << deck.cues.size() << "\r\n"
           << "\r\n";
      return resp.str();
    }
    if (cmdL == "clips get") {
      const Deck& deck = focusedDeck();
      std::ostringstream resp;
      resp << "205 clips info:\r\n"
           << "clip count: " << deck.cues.size() << "\r\n";
      for (int i = 0; i < static_cast<int>(deck.cues.size()); ++i) {
        const Cue& cue = deck.cues[i];
        resp << (i + 1) << ": " << cue.name << " 00:00:00:00 "
             << formatSeconds(cue.duration) << "\r\n";
      }
      resp << "\r\n";
      return resp.str();
    }
    if (cmdL == "device info") {
      return "500 device info:\r\n"
             "protocol version: 1.11\r\n"
             "model: HyperDeck Studio Mini\r\n"
             "unique id: PLAYBOY00001\r\n"
             "\r\n";
    }
    // Unknown command
    return "109 unsupported parameter\r\n\r\n";
  }
#endif

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
        maybeBroadcastOscState();
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
        sockaddr_in sender {};
        socklen_t senderLen = sizeof(sender);
        ssize_t bytes = recvfrom(
          companionUdpSocket_,
          buffer.data(),
          buffer.size(),
          0,
          reinterpret_cast<sockaddr*>(&sender),
          &senderLen
        );
        if (bytes > 0) {
          std::string payload(buffer.data(), static_cast<size_t>(bytes));
          auto oscMessages = parseOscPacket(payload);
          if (!oscMessages.empty()) {
            rememberOscSubscriber(sender);
            for (const auto& osc : oscMessages) {
              std::string path = toUpper(trim(osc.address));
              if (path == "/STATUS" || path == "/STATE" || path == "/PLAYBOY/STATUS" || path == "/PLAYBOY/STATE") {
                sendOscStringTo(sender, "/playboy/state", snapshotJsonForFeedback());
                continue;
              }
              if (path == "/PING" || path == "/PLAYBOY/PING") {
                sendOscStringTo(sender, "/playboy/pong", "DECKBOY_0.01");
                continue;
              }
              auto mapped = mapOscToRemoteCommand(osc);
              if (mapped && !mapped->empty()) {
                enqueueRemoteCommand(*mapped);
                sendOscStringTo(sender, "/playboy/ack", *mapped);
              }
            }
          } else if (payload.find('\0') != std::string::npos) {
            continue;
          } else {
            enqueueRemoteCommandBatch(payload);
          }
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
      maybeBroadcastOscState();
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
    if (command == "JUMPMODE" || command == "JUMP_MODE") {
      if (parts.size() < 2) {
        triggerToast("jump mode: " + jumpModeLabelFromToken(project_.jumpMode));
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "TOGGLE") {
        toggleJumpMode();
      } else {
        setJumpModeToken(value);
      }
      return;
    }
    if (command == "JUMPTRANS" || command == "JUMPTRANSITION" || command == "JUMP_XFADE") {
      auto state = parseToggleWord(1);
      if (!state) {
        setJumpTransitionEnabled(!project_.jumpTransitionEnabled);
      } else {
        setJumpTransitionEnabled(*state);
      }
      return;
    }
    if (command == "PANICPROFILE" || command == "PANIC_PROFILE") {
      if (parts.size() < 2) {
        triggerToast("panic profile: " + panicProfileLabelFromToken(project_.panicProfile));
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "NEXT") {
        cyclePanicProfile(1);
      } else if (value == "PREV" || value == "PREVIOUS") {
        cyclePanicProfile(-1);
      } else {
        project_.panicProfile = normalizePanicProfileToken(value);
        triggerToast("panic profile: " + panicProfileLabelFromToken(project_.panicProfile));
        playUiSound(UiSoundEffect::Toggle);
        markProjectDirty();
      }
      return;
    }
    if (command == "PANICFADE" || command == "PANIC_FADE") {
      if (parts.size() < 2) {
        std::ostringstream label;
        label << std::fixed << std::setprecision(1) << project_.panicFadeSeconds;
        triggerToast("panic fade " + label.str() + "s");
      } else if (auto value = parseNumber(1); value) {
        setPanicFadeSeconds(*value);
      }
      return;
    }
    if (command == "PANICAUTORESTORE" || command == "PANIC_RESTORE") {
      auto state = parseToggleWord(1);
      if (!state) {
        setPanicAutoRestoreEnabled(!project_.panicAutoRestore);
      } else {
        setPanicAutoRestoreEnabled(*state);
      }
      return;
    }
    if (command == "PANIC") {
      if (parts.size() > 1) {
        triggerPanicProfile(parts[1]);
      } else {
        triggerPanicProfile();
      }
      return;
    }
    if (command == "OSCQUERY" || command == "OSC_QUERY") {
      auto state = parseToggleWord(1);
      if (!state) {
        setOscQueryEnabled(!project_.oscQueryEnabled);
      } else {
        setOscQueryEnabled(*state);
      }
      return;
    }
    if (command == "OSCQUERYPORT" || command == "OSC_QUERY_PORT") {
      if (parts.size() < 2) {
        triggerToast("osc query port: " + std::to_string(project_.oscQueryPort));
      } else if (auto value = parseNumber(1); value) {
        setOscQueryPort(static_cast<int>(std::lround(*value)));
      }
      return;
    }
    if (command == "OSCFEEDBACK" || command == "OSC_FEEDBACK") {
      auto state = parseToggleWord(1);
      if (!state) {
        setOscFeedbackMirrorEnabled(!project_.oscFeedbackMirrorEnabled);
      } else {
        setOscFeedbackMirrorEnabled(*state);
      }
      return;
    }
    if (command == "OSCFEEDBACKRATE" || command == "OSC_FEEDBACK_RATE") {
      if (parts.size() < 2) {
        triggerToast("osc feedback rate: " + std::to_string(project_.oscFeedbackRateMs) + " ms");
      } else if (auto value = parseNumber(1); value) {
        setOscFeedbackRateMs(static_cast<int>(std::lround(*value)));
      }
      return;
    }
    // ── All-deck simultaneous commands ─────────────────────────────
    if (command == "ALLTAKE" || command == "SYNCTAKE") {
      takeAllDecks(true);
      return;
    }
    if (command == "ALLGO" || command == "SYNCGO") {
      goAllDecks();
      return;
    }
    if (command == "ALLPLAY") {
      for (int di = 0; di < static_cast<int>(project_.decks.size()); ++di) {
        if (auto* e = mediaEngineForDeck(di)) e->play();
      }
      triggerToast("all decks play");
      return;
    }
    if (command == "ALLPAUSE") {
      for (int di = 0; di < static_cast<int>(project_.decks.size()); ++di) {
        if (auto* e = mediaEngineForDeck(di)) e->pause();
      }
      triggerToast("all decks paused");
      return;
    }
    if (command == "ALLSTOP") {
      for (int di = 0; di < static_cast<int>(project_.decks.size()); ++di) {
        if (auto* e = mediaEngineForDeck(di)) e->stop();
      }
      triggerToast("all decks stopped");
      return;
    }
    if (command == "GROUP" || command == "MASTER" || command == "MASTERCUE" ||
        command == "PRESET" || command == "GROUPPRESET") {
      if (parts.size() == 1) {
        if (project_.groupPresets.empty()) {
          triggerToast("master cue: none");
        } else {
          int idx = std::clamp(project_.focusedGroupPresetIndex, 0, static_cast<int>(project_.groupPresets.size()) - 1);
          triggerToast("master cue " + std::to_string(idx + 1) + "/" + std::to_string(project_.groupPresets.size())
            + ": " + groupPresetLabel(idx));
        }
        return;
      }

      std::string sub = toUpper(parts[1]);
      if (sub == "ADD" || sub == "NEW") {
        std::string name = parts.size() > 2 ? joinParts(parts, 2) : "";
        addGroupPreset(name, true);
        return;
      }
      if (sub == "ADDEMPTY") {
        std::string name = parts.size() > 2 ? joinParts(parts, 2) : "";
        addGroupPreset(name, false);
        return;
      }
      if (sub == "NEXT") {
        ensureGroupPreset(true);
        cycleFocusedGroupPreset(1);
        return;
      }
      if (sub == "PREV" || sub == "PREVIOUS") {
        ensureGroupPreset(true);
        cycleFocusedGroupPreset(-1);
        return;
      }
      if (sub == "DELETE" || sub == "DEL" || sub == "REMOVE") {
        deleteFocusedGroupPreset();
        return;
      }
      if (sub == "FIRE" || sub == "TAKE" || sub == "GO") {
        if (parts.size() > 2) {
          try {
            int idx = std::stoi(parts[2]) - 1;
            fireGroupPreset(idx, true);
          } catch (...) {
          }
        } else {
          fireFocusedGroupPreset(true);
        }
        return;
      }
      if (sub == "CAPTURE") {
        ensureGroupPreset(true);
        std::string mode = parts.size() > 2 ? toUpper(parts[2]) : "SEL";
        captureFocusedGroupPreset(mode == "ACTIVE" || mode == "ACT" || mode == "LIVE");
        return;
      }
      if (sub == "NAME") {
        ensureGroupPreset(true);
        if (parts.size() > 2) {
          renameFocusedGroupPreset(joinParts(parts, 2));
        }
        return;
      }
      if (sub == "SET" && parts.size() > 3) {
        ensureGroupPreset(true);
        auto deckRef = parseDeckReferenceToken(parts[2]);
        if (deckRef) {
          if (!setFocusedGroupSlotByToken(*deckRef, joinParts(parts, 3))) {
            triggerToast("master cue set failed");
          }
        }
        return;
      }
      if (sub == "BYPASS" && parts.size() > 3) {
        ensureGroupPreset(true);
        auto deckRef = parseDeckReferenceToken(parts[2]);
        if (!deckRef) {
          return;
        }
        std::string mode = toUpper(parts[3]);
        bool nextBypass = true;
        if (mode == "ON" || mode == "TRUE" || mode == "1") {
          nextBypass = true;
        } else if (mode == "OFF" || mode == "FALSE" || mode == "0") {
          nextBypass = false;
        } else {
          bool current = false;
          if (const GroupPreset* preset = focusedGroupPreset()) {
            if (*deckRef >= 0 && *deckRef < static_cast<int>(preset->slots.size())) {
              current = preset->slots[*deckRef].bypass;
            }
          }
          nextBypass = !current;
        }
        setFocusedGroupSlotBypass(*deckRef, nextBypass);
        return;
      }
      if (sub == "SELECT" || sub == "FOCUS") {
        ensureGroupPreset(true);
        if (parts.size() > 2) {
          std::string indexToken = toUpper(parts[2]);
          if (indexToken == "NEXT") {
            cycleFocusedGroupPreset(1);
          } else if (indexToken == "PREV" || indexToken == "PREVIOUS") {
            cycleFocusedGroupPreset(-1);
          } else {
            try {
              setFocusedGroupPresetIndex(std::stoi(parts[2]) - 1);
            } catch (...) {
            }
          }
        }
        return;
      }
      if (sub == "LIST") {
        if (project_.groupPresets.empty()) {
          triggerToast("master cues: 0");
          return;
        }
        int focus = std::clamp(project_.focusedGroupPresetIndex, 0, static_cast<int>(project_.groupPresets.size()) - 1);
        triggerToast("master cues: " + std::to_string(project_.groupPresets.size()) + " focus " + std::to_string(focus + 1));
        return;
      }

      // GROUP 2 [nested-command...]
      try {
        int idx = std::stoi(parts[1]) - 1;
        if (!setFocusedGroupPresetIndex(idx)) {
          return;
        }
        if (parts.size() > 2) {
          handleRemoteCommand(command + " " + joinParts(parts, 2));
        }
      } catch (...) {
      }
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
      if (!index && parts.size() > 1) {
        index = cueIndexByToken(focusedDeck(), joinParts(parts, 1));
      }
      if (index) {
        Deck& deck = focusedDeckMutable();
        if (deck.selectedIndex != *index || !cueIndexSelected(deck, *index)) {
          selectCueInDeck(project_.focusedDeckIndex, *index, false, false);
          triggerToast("cue " + std::to_string(*index + 1) + " armed");
        }
      }
      return;
    }
    if (command == "FIND" || command == "CUEFIND") {
      if (parts.size() < 2) {
        if (!lastCueFindToken_.empty()) {
          findCueToken(lastCueFindToken_, 1, false);
        } else {
          triggerToast("find: token required");
        }
      } else {
        findCueToken(joinParts(parts, 1), 1, false);
      }
      return;
    }
    if (command == "FINDNEXT" || command == "CUEFINDNEXT") {
      if (lastCueFindToken_.empty()) {
        triggerToast("find: run FIND first");
      } else {
        findCueToken(lastCueFindToken_, 1, false);
      }
      return;
    }
    if (command == "FINDPREV" || command == "FINDPREVIOUS" || command == "CUEFINDPREV") {
      if (lastCueFindToken_.empty()) {
        triggerToast("find: run FIND first");
      } else {
        findCueToken(lastCueFindToken_, -1, false);
      }
      return;
    }
    if (command == "FINDTAKE" || command == "CUEFINDTAKE") {
      if (parts.size() < 2) {
        if (!lastCueFindToken_.empty()) {
          findCueToken(lastCueFindToken_, 1, true);
        } else {
          triggerToast("find: token required");
        }
      } else {
        findCueToken(joinParts(parts, 1), 1, true);
      }
      return;
    }
    if (command == "FINDCLEAR" || command == "FINDRESET" || command == "CUEFINDCLEAR") {
      clearCueFindState();
      triggerToast("find cleared");
      return;
    }
    if (command == "FINDSTATUS" || command == "CUEFINDSTATUS") {
      if (lastCueFindToken_.empty() || lastCueFindMatches_.empty()) {
        triggerToast("find: none");
      } else {
        int cursor = std::clamp(lastCueFindCursor_, 0, static_cast<int>(lastCueFindMatches_.size()) - 1);
        triggerToast("find \"" + lastCueFindToken_ + "\" "
          + std::to_string(cursor + 1) + "/" + std::to_string(lastCueFindMatches_.size()));
      }
      return;
    }
    if (command == "RENUMBER" || command == "CUEAUTOID" || command == "AUTOID") {
      if (parts.size() > 1 && toUpper(parts[1]) == "CLEAR") {
        clearFocusedDeckCueNumbers();
        return;
      }
      std::string prefix;
      int startAt = 1;
      if (parts.size() > 1) {
        prefix = parts[1];
      }
      if (parts.size() > 2) {
        try {
          startAt = std::stoi(parts[2]);
        } catch (...) {
        }
      }
      renumberFocusedDeckCueNumbers(prefix, startAt);
      return;
    }
    if (command == "SELECTID" || command == "CUEID") {
      selectCueById(joinParts(parts, 1));
      return;
    }
    if (command == "TAKE") {
      auto index = parseCueIndex(1);
      if (!index && parts.size() > 1) {
        index = cueIndexByToken(focusedDeck(), joinParts(parts, 1));
      }
      if (index) {
        selectCueInDeck(project_.focusedDeckIndex, *index, false, false);
      }
      if (parts.size() > 2 && toUpper(parts[2]) == "AUTO") {
        takeSelected(true);
      } else {
        jumpSelectedCue();
      }
      return;
    }
    if (command == "TAKEID") {
      if (selectCueById(joinParts(parts, 1))) {
        jumpSelectedCue();
      }
      return;
    }
    if (command == "GOTO") {
      std::string token = joinParts(parts, 1);
      if (token.empty()) {
        return;
      }
      Deck& deck = focusedDeckMutable();
      auto index = cueIndexByToken(deck, token);
      if (!index) {
        return;
      }
      selectCueInDeck(project_.focusedDeckIndex, *index, false, false);
      jumpSelectedCue();
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
    if (command == "SEEK" || command == "SEEKPOS") {
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
    if (command == "IN" || command == "TRIMIN") {
      auto value = parseNumber(1);
      if (value) {
        setSelectedTrimIn(*value);
      }
      return;
    }
    if (command == "OUT" || command == "TRIMOUT") {
      auto value = parseNumber(1);
      if (value) {
        setSelectedTrimOut(*value);
      }
      return;
    }
    if (command == "TRIM") {
      if (parts.size() < 2) {
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "CLEAR" || value == "RESET") {
        clearSelectedTrim();
      } else if (value == "IN") {
        auto number = parseNumber(2);
        if (number) {
          setSelectedTrimIn(*number);
        }
      } else if (value == "OUT") {
        auto number = parseNumber(2);
        if (number) {
          setSelectedTrimOut(*number);
        }
      }
      return;
    }
    if (command == "OVERLAY" || command == "TIMEOVERLAY") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleTimeOverlayEnabled();
      } else {
        setTimeOverlayEnabled(*state);
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
        markProjectDirty();
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
        markProjectDirty();
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
    if (command == "HOLD" || command == "HOLDLAST" || command == "PAUSEEND" || command == "PAUSEATEND") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleSelectedPauseOnLastFrame();
      } else {
        setSelectedPauseOnLastFrame(*state);
      }
      return;
    }
    if (command == "PAUSEBEGIN" || command == "PAUSEATBEGIN" || command == "PAUSESTART") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleSelectedPauseAtBeginning();
      } else {
        setSelectedPauseAtBeginning(*state);
      }
      return;
    }
    if (command == "CUEAUDIO" || command == "AUDIOCUE" || command == "AUDIOENABLED") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleSelectedAudioEnabled();
      } else {
        setSelectedAudioEnabled(*state);
      }
      return;
    }
    if (command == "NEXTTRANS" || command == "TRANSITIONTONEXT" || command == "CUEXNEXT") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleSelectedTransitionToNext();
      } else {
        setSelectedTransitionToNext(*state);
      }
      return;
    }
    if (command == "CUEGOTO" || command == "GOTOTARGET") {
      if (parts.size() <= 1) {
        Cue* cue = selectedCueMutable();
        if (!cue) {
          return;
        }
        triggerToast(cue->gotoTarget.empty() ? "goto target: none" : ("goto target: " + cue->gotoTarget));
      } else {
        setSelectedGotoTarget(joinParts(parts, 1));
      }
      return;
    }
    if (command == "CUEIDSHORT" || command == "SHORTID" || command == "CUESHORTID") {
      Cue* cue = selectedCueMutable();
      if (!cue) {
        return;
      }
      if (parts.size() <= 1) {
        triggerToast("cue id: " + (cue->cueId.empty() ? std::string("(none)") : cue->cueId));
      } else {
        std::string cueIdShort = normalizeCueIdShort(parts[1]);
        forEachFocusedSelectedCueMutable([&](Cue& each, int) {
          each.cueId = cueIdShort;
        });
        triggerToast("cue id: " + (cueIdShort.empty() ? std::string("(none)") : cueIdShort));
        markProjectDirty();
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
    if (command == "PLAYLISTOPACITY" || command == "DECKOPACITY" || command == "DECKDIM") {
      if (parts.size() <= 1) {
        int pct = static_cast<int>(std::lround(std::clamp(focusedDeck().playlistOpacity, 0.0f, 1.0f) * 100.0f));
        triggerToast("deck opacity: " + std::to_string(pct) + "%");
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "UP" || value == "+" || value == "INC") {
        setFocusedDeckPlaylistOpacity(focusedDeck().playlistOpacity + 0.05f, true);
        return;
      }
      if (value == "DOWN" || value == "-" || value == "DEC") {
        setFocusedDeckPlaylistOpacity(focusedDeck().playlistOpacity - 0.05f, true);
        return;
      }
      if (value == "ON" || value == "100") {
        setFocusedDeckPlaylistOpacity(1.0f, true);
        return;
      }
      if (value == "OFF" || value == "0") {
        setFocusedDeckPlaylistOpacity(0.0f, true);
        return;
      }
      if (auto parsed = parseNumber(1); parsed) {
        double normalized = *parsed > 1.0 ? *parsed / 100.0 : *parsed;
        setFocusedDeckPlaylistOpacity(static_cast<float>(normalized), true);
      }
      return;
    }
    if (command == "PLAYLISTAUTOFADE" || command == "DECKAUTOFADE") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleFocusedDeckPlaylistAutoFade();
      } else {
        setFocusedDeckPlaylistAutoFade(*state);
      }
      return;
    }
    if (command == "PLAYLISTFADE" || command == "DECKFADE") {
      if (parts.size() <= 1) {
        setFocusedDeckPlaylistFadeSeconds(focusedDeck().playlistFadeSeconds);
        return;
      }
      if (auto parsed = parseNumber(1); parsed) {
        setFocusedDeckPlaylistFadeSeconds(*parsed);
      }
      return;
    }
    if (command == "SHUFFLE") {
      auto state = parseToggleWord(1);
      if (!state) {
        toggleShuffle();
      } else {
        Deck& deck = focusedDeckMutable();
        if (deck.shuffle != *state) {
          deck.shuffle = *state;
          triggerToast(deck.shuffle ? "shuffle on" : "shuffle off");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "ENDACTION") {
      if (parts.size() >= 2) {
        setSelectedEndAction(parseCueEndAction(toUpper(parts[1]) == "INHERIT" ? "inherit" :
                                               toUpper(parts[1]) == "STOP"    ? "stop"    :
                                               toUpper(parts[1]) == "LOOP"    ? "loop"    :
                                               toUpper(parts[1]) == "HOLD"    ? "hold"    :
                                               toUpper(parts[1]) == "NEXT"    ? "next"    : "inherit"));
      } else {
        cycleSelectedEndAction();
      }
      return;
    }
    if (command == "TRANSITION" || command == "XFADE") {
      if (parts.size() < 2) {
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "OFF" || value == "0" || value == "CUT") {
        setTransitionSeconds(0.0);
        if (value == "CUT") {
          setTransitionStyle(TransitionStyle::Cut);
        }
      } else if (value == "STYLE") {
        if (parts.size() > 2) {
          setTransitionStyle(parseTransitionStyleToken(parts[2]));
        }
      } else {
        auto seconds = parseNumber(1);
        if (seconds) {
          setTransitionSeconds(*seconds);
          if (focusedDeck().transitionStyle == "cut" && *seconds > 0.0) {
            setTransitionStyle(TransitionStyle::Crossfade);
          }
        } else {
          setTransitionStyle(parseTransitionStyleToken(parts[1]));
        }
      }
      return;
    }
    if (command == "TRANSITIONSTYLE") {
      if (parts.size() > 1) {
        setTransitionStyle(parseTransitionStyleToken(parts[1]));
      }
      return;
    }
    if (command == "TCMARK" || command == "TIMECODEMARK") {
      if (parts.size() < 2) {
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "NOW" || value == "HERE") {
        setSelectedCueTimecodeTrigger(focusedDeck().timecodeCurrentSeconds);
        return;
      }
      if (value == "CLEAR" || value == "NONE" || value == "OFF") {
        clearSelectedCueTimecodeTrigger();
        return;
      }
      auto parsed = parseTimecodeSeconds(joinParts(parts, 1), focusedDeck().timecodeFps);
      if (parsed) {
        setSelectedCueTimecodeTrigger(*parsed);
      }
      return;
    }
    if (command == "TIMECODE" || command == "TC") {
      if (parts.size() < 2) {
        triggerToast("tc " + formatTimecode(focusedDeck().timecodeCurrentSeconds, focusedDeck().timecodeFps));
        return;
      }
      std::string sub = toUpper(parts[1]);
      if (sub == "CHASE") {
        auto state = parseToggleWord(2);
        if (state) {
          setTimecodeChaseEnabled(*state);
        }
        return;
      }
      if (sub == "RUN") {
        auto state = parseToggleWord(2);
        if (state) {
          setTimecodeRunEnabled(*state);
        }
        return;
      }
      if (sub == "TRIGGER") {
        auto state = parseToggleWord(2);
        if (state) {
          focusedDeckMutable().timecodeTriggerEnabled = *state;
          triggerToast(*state ? "tc trigger on" : "tc trigger off");
          markProjectDirty();
        }
        return;
      }
      if (sub == "JAM") {
        auto state = parseToggleWord(2);
        if (state) {
          setTimecodeJamSyncEnabled(*state);
        } else {
          triggerToast(focusedDeck().timecodeJamSyncEnabled ? "tc jam on" : "tc jam off");
        }
        return;
      }
      if (sub == "FREEWHEEL" || sub == "FREE") {
        if (parts.size() < 3) {
          std::ostringstream label;
          label << std::fixed << std::setprecision(1) << focusedDeck().timecodeFreewheelSeconds;
          triggerToast("tc freewheel " + label.str() + "s");
        } else if (auto value = parseNumber(2); value) {
          setTimecodeFreewheelSeconds(*value);
        }
        return;
      }
      if (sub == "FPS") {
        auto value = parseNumber(2);
        if (value) {
          setTimecodeFps(*value);
        }
        return;
      }
      if (sub == "SET") {
        auto parsed = parseTimecodeSeconds(joinParts(parts, 2), focusedDeck().timecodeFps);
        if (parsed) {
          setFocusedDeckTimecode(*parsed, true);
        }
        return;
      }
      auto parsed = parseTimecodeSeconds(joinParts(parts, 1), focusedDeck().timecodeFps);
      if (parsed) {
        setFocusedDeckTimecode(*parsed, false);
      }
      return;
    }
    if (command == "PATTERN") {
      if (parts.size() > 1) {
        std::string sub = toUpper(parts[1]);
        if (sub == "LIST") {
          triggerToast("patterns: " + std::to_string(patternTypes().size()) + " types");
          return;
        }
        if (sub == "SET") {
          std::string typeId = parts.size() > 2 ? normalizePatternTypeId(joinParts(parts, 2)) : "";
          if (typeId == "checker") {
            typeId = "checkerboard";
          }
          if (typeId.empty() || !isKnownPatternType(typeId)) {
            triggerToast("pattern default: invalid");
            return;
          }
          patternDefaultTypeId_ = typeId;
          triggerToast("pattern default: " + patternLabelForType(typeId));
          return;
        }
      }

      std::string typeId;
      if (parts.size() > 2) {
        std::string tail = toUpper(parts.back());
        if (tail == "MOTION" || tail == "ANIM" || tail == "ANIMATED") {
          typeId = normalizePatternTypeId(parts[1] + "-motion");
        }
      }
      if (typeId.empty()) {
        typeId = parts.size() > 1 ? normalizePatternTypeId(joinParts(parts, 1)) : patternDefaultTypeId_;
      }
      addPatternCue(typeId);
      return;
    }
    if (command == "SOURCE" || command == "SRC" || command == "WINDOWSOURCE" ||
        command == "CAMERACUE" || command == "SYPHONCUE" || command == "SPOUTCUE") {
      CueKind kind = CueKind::WindowSource;
      size_t refStartIndex = 1;
      if (command == "CAMERACUE") {
        kind = CueKind::Camera;
      } else if (command == "SYPHONCUE" || command == "SPOUTCUE") {
        kind = CueKind::Syphon;
      } else if (parts.size() > 1) {
        std::string typeArg = toUpper(parts[1]);
        if (typeArg == "WINDOW" || typeArg == "WINDOWSOURCE" || typeArg == "WINDOWS") {
          kind = CueKind::WindowSource;
          refStartIndex = 2;
        } else if (typeArg == "CAMERA" || typeArg == "CAM" || typeArg == "WEBCAM") {
          kind = CueKind::Camera;
          refStartIndex = 2;
        } else if (typeArg == "SYPHON" || typeArg == "SIPHON" || typeArg == "SPOUT") {
          kind = CueKind::Syphon;
          refStartIndex = 2;
        }
      }
      std::string sourceRef = parts.size() > refStartIndex ? joinParts(parts, refStartIndex) : "";
      addSourceCue(kind, sourceRef);
      return;
    }
    if (command == "STILLDUR" || command == "DURATION") {
      if (parts.size() > 1) {
        try {
          double dur = std::stod(parts[1]);
          if (Cue* cue = selectedCueMutable()) {
            if (cue->kind != CueKind::Video) {
              cue->stillDurationSeconds = std::max(0.0, dur);
              triggerToast(cue->stillDurationSeconds > 0.0
                ? "still dur " + formatSeconds(cue->stillDurationSeconds)
                : "still dur: hold");
              markProjectDirty();
            }
          }
        } catch (...) {}
      }
      return;
    }
    if (command == "GRAPHIC" || command == "LOWERTHIRD") {
      addLowerThirdCue();
      return;
    }
    if (command == "LOWERTEXT") {
      std::string txt = joinParts(parts, 1);
      if (Cue* cue = selectedCueMutable()) {
        if (cue->kind == CueKind::LowerThird) {
          cue->lowerThirdText = txt;
          triggerToast("lower text set");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "LOWERSUB") {
      std::string txt = joinParts(parts, 1);
      if (Cue* cue = selectedCueMutable()) {
        if (cue->kind == CueKind::LowerThird) {
          cue->lowerThirdSubtext = txt;
          triggerToast("lower sub set");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "LOWERALPHA") {
      if (parts.size() > 1) {
        try {
          int alpha = std::stoi(parts[1]);
          if (Cue* cue = selectedCueMutable()) {
            if (cue->kind == CueKind::LowerThird) {
              cue->lowerThirdBgAlpha = std::clamp(alpha, 0, 255);
              triggerToast("overlay alpha " + std::to_string(cue->lowerThirdBgAlpha));
              markProjectDirty();
            }
          }
        } catch (...) {}
      }
      return;
    }
    if (command == "CLEAROVERLAY") {
      clearOverlay();
      return;
    }
    if (command == "OVERLAY" && parts.size() > 1) {
      std::string sub = toUpper(parts[1]);
      if (sub == "CLEAR") { clearOverlay(); return; }
      if (sub == "POP")   { popOverlay();   return; }
      if (sub == "PUSH" && parts.size() > 2) {
        try {
          int idx = std::stoi(parts[2]) - 1;  // 1-based
          Deck& deck = focusedDeckMutable();
          if (idx >= 0 && idx < static_cast<int>(deck.cues.size())) {
            auto& ov = deck.overlayActiveIndices;
            if (std::find(ov.begin(), ov.end(), idx) == ov.end()) {
              if (ov.size() >= 4) ov.erase(ov.begin());
              ov.push_back(idx);
              triggerToast("overlay pushed: " + deck.cues[idx].name);
              markProjectDirty();
            }
          }
        } catch (...) {}
        return;
      }
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
    if (command == "ROUTE") {
      if (parts.size() <= 1) {
        triggerToast(deckOutputRoutingLabel(project_.focusedDeckIndex));
        return;
      }
      std::optional<int> targetDeck = parseDeckReferenceToken(parts[1]);
      std::optional<int> requestedLayer;
      if (targetDeck && parts.size() > 2) {
        try {
          requestedLayer = std::stoi(parts[2]);
        } catch (...) {
          requestedLayer = std::nullopt;
        }
      } else if (!targetDeck) {
        targetDeck = parseDeckReferenceToken(joinParts(parts, 1));
      }
      if (targetDeck) {
        setFocusedDeckOutputRoute(*targetDeck, requestedLayer);
      }
      return;
    }
    if (command == "LAYER") {
      if (parts.size() <= 1) {
        triggerToast(deckOutputRoutingLabel(project_.focusedDeckIndex));
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "UP" || value == "NEXT" || value == "+") {
        nudgeFocusedDeckLayerIndex(1);
        return;
      }
      if (value == "DOWN" || value == "PREV" || value == "PREVIOUS" || value == "-") {
        nudgeFocusedDeckLayerIndex(-1);
        return;
      }
      if (value == "TOP" || value == "FRONT") {
        int outputIndex = primaryOutputIndexForDeck(project_.focusedDeckIndex).value_or(project_.focusedOutputIndex);
        int nextTop = nextLayerIndexForOutput(outputIndex, project_.focusedDeckIndex);
        setFocusedDeckLayerIndex(nextTop);
        return;
      }
      if (value == "BOTTOM" || value == "BACK") {
        setFocusedDeckLayerIndex(0);
        return;
      }
      try {
        setFocusedDeckLayerIndex(std::stoi(parts[1]));
      } catch (...) {
      }
      return;
    }
    if (command == "LAYERNAME") {
      if (parts.size() < 3) {
        triggerToast("LAYERNAME: need layer_index/name and new_name");
        return;
      }
      std::string layerRef = toUpper(parts[1]);
      std::string newName = joinParts(parts, 2);
      int layerIdx = -1;
      // Try parse as number first
      try {
        layerIdx = std::stoi(layerRef);
      } catch (...) {
        // Try find by name
        for (int i = 0; i < static_cast<int>(project_.layerNames.size()); ++i) {
          if (toUpper(project_.layerNames[i]) == layerRef) {
            layerIdx = i;
            break;
          }
        }
      }
      if (layerIdx >= 0 && layerIdx < static_cast<int>(project_.layerNames.size())) {
        project_.layerNames[layerIdx] = newName;
        triggerToast("layer " + std::to_string(layerIdx) + " renamed to: " + newName);
        markProjectDirty();
      }
      return;
    }
    if (command == "CANVAS" || command == "VIEW" || command == "WARP" || command == "BLEND") {
      std::string forwarded = "VIDEO " + command;
      if (parts.size() > 1) {
        forwarded += " " + joinParts(parts, 1);
      }
      handleRemoteCommand(forwarded);
      return;
    }
    if (command == "VIDEO" || command == "OUTPUTMODE") {
      if (parts.size() <= 1) {
        std::string canvasLabel = project_.outputCanvasEnabled
          ? (" canvas " + std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
          : " canvas off";
        triggerToast("video: " + outputSizingModeLabel() + " " + outputResolutionLabelForOutput(project_.focusedOutputIndex)
          + " @" + outputRefreshRateLabel() + " " + outputBitDepthModeLabel() + canvasLabel);
        return;
      }

      auto applyRasterToken = [&](std::string token) -> bool {
        token = toUpper(trim(token));
        double hzOverride = -1.0;
        auto atPos = token.find('@');
        if (atPos != std::string::npos && atPos + 1 < token.size()) {
          try {
            hzOverride = std::stod(token.substr(atPos + 1));
          } catch (...) {
            hzOverride = -1.0;
          }
          token = token.substr(0, atPos);
        }
        auto xPos = token.find('X');
        if (xPos == std::string::npos || xPos == 0 || xPos + 1 >= token.size()) {
          return false;
        }
        try {
          int w = std::stoi(token.substr(0, xPos));
          int h = std::stoi(token.substr(xPos + 1));
          if (w > 0 && h > 0) {
            setOutputSizingModeFixed(w, h);
            if (hzOverride >= 0.0) {
              setOutputRefreshRate(hzOverride);
            }
            return true;
          }
        } catch (...) {
        }
        return false;
      };

      auto parsePointToken = [&](std::string token) -> std::optional<std::pair<int, int>> {
        token = trim(token);
        if (token.empty()) {
          return std::nullopt;
        }
        size_t split = token.find(',');
        if (split == std::string::npos) {
          split = token.find('x');
        }
        if (split == std::string::npos) {
          split = token.find('X');
        }
        if (split == std::string::npos || split == 0 || split + 1 >= token.size()) {
          return std::nullopt;
        }
        try {
          int x = std::stoi(token.substr(0, split));
          int y = std::stoi(token.substr(split + 1));
          return std::make_pair(x, y);
        } catch (...) {
          return std::nullopt;
        }
      };

      auto parseBlendValue = [&](size_t tokenIndex) -> std::optional<float> {
        auto value = parseNumber(tokenIndex);
        if (!value) {
          return std::nullopt;
        }
        float normalized = static_cast<float>(*value);
        if (std::abs(normalized) > 1.0f) {
          normalized /= 100.0f;
        }
        return normalized;
      };

      std::string value = toUpper(parts[1]);
      if (value == "OUTPUT" || value == "OUT") {
        if (parts.size() <= 2) {
          triggerToast("output: " + outputLabel(project_.focusedOutputIndex)
            + " host:" + deckLabel(focusedOutput().hostDeckIndex));
          return;
        }
        std::string outputArg = toUpper(parts[2]);
        if (outputArg == "NEXT") {
          cycleFocusedOutput(1);
          return;
        }
        if (outputArg == "PREV" || outputArg == "PREVIOUS") {
          cycleFocusedOutput(-1);
          return;
        }
        if (outputArg == "ADD" || outputArg == "NEW" || outputArg == "CREATE") {
          std::string newType = "window";
          if (parts.size() > 3) {
            std::string typeArg = toUpper(parts[3]);
            if (typeArg == "STREAM") {
              newType = "stream";
            }
          }
          addOutput(project_.focusedDeckIndex, newType);
          return;
        }
        if (outputArg == "ON" || outputArg == "ENABLE") {
          setFocusedOutputEnabled(true);
          return;
        }
        if (outputArg == "OFF" || outputArg == "DISABLE") {
          setFocusedOutputEnabled(false);
          return;
        }
        if (outputArg == "TOGGLE") {
          toggleFocusedOutputEnabled();
          return;
        }
        if (outputArg == "ASSIGN") {
          std::optional<int> layer;
          if (parts.size() > 3) {
            try {
              layer = std::stoi(parts[3]);
            } catch (...) {
              layer = std::nullopt;
            }
          }
          assignFocusedDeckToFocusedOutput(layer);
          return;
        }
        if (outputArg == "HOST") {
          if (parts.size() <= 3) {
            setFocusedOutputHostDeck(project_.focusedDeckIndex);
            return;
          }
          std::optional<int> targetDeck = parseDeckReferenceToken(parts[3]);
          if (!targetDeck && parts.size() > 4) {
            targetDeck = parseDeckReferenceToken(joinParts(parts, 3));
          }
          if (targetDeck) {
            setFocusedOutputHostDeck(*targetDeck);
          }
          return;
        }
        if (outputArg == "TYPE") {
          if (parts.size() <= 3) {
            const OutputTarget& focused = focusedOutput();
            triggerToast("type: " + normalizeOutputType(focused.outputType));
            return;
          }
          std::string typeArg = toUpper(parts[3]);
          if (typeArg == "WINDOW" || typeArg == "DISPLAY") {
            setFocusedOutputType("window");
            return;
          }
          if (typeArg == "STREAM") {
            setFocusedOutputType("stream");
            return;
          }
          return;
        }
        if (outputArg == "MIRROR") {
          if (parts.size() <= 3) {
            cycleFocusedOutputMirrorSource(1);
            return;
          }
          std::string mirrorArg = toUpper(parts[3]);
          if (mirrorArg == "OFF" || mirrorArg == "NONE") {
            setFocusedOutputMirrorSource(-1);
            return;
          }
          if (mirrorArg == "NEXT") {
            cycleFocusedOutputMirrorSource(1);
            return;
          }
          if (mirrorArg == "PREV" || mirrorArg == "PREVIOUS") {
            cycleFocusedOutputMirrorSource(-1);
            return;
          }
          try {
            int sourceOutput = std::stoi(parts[3]);
            setFocusedOutputMirrorSource(std::max(0, sourceOutput - 1));
          } catch (...) {
          }
          return;
        }
        if (outputArg == "ALPHA" || outputArg == "DIM" || outputArg == "OPACITY") {
          if (parts.size() <= 3) {
            int pct = static_cast<int>(std::lround(std::clamp(focusedOutput().outputAlpha, 0.0f, 1.0f) * 100.0f));
            triggerToast("output alpha: " + std::to_string(pct) + "%");
            return;
          }
          std::string alphaArg = toUpper(parts[3]);
          if (alphaArg == "UP" || alphaArg == "+" || alphaArg == "INC") {
            setFocusedOutputAlpha(focusedOutput().outputAlpha + 0.05f);
            return;
          }
          if (alphaArg == "DOWN" || alphaArg == "-" || alphaArg == "DEC") {
            setFocusedOutputAlpha(focusedOutput().outputAlpha - 0.05f);
            return;
          }
          if (alphaArg == "ON") {
            setFocusedOutputAlpha(1.0f);
            return;
          }
          if (alphaArg == "OFF") {
            setFocusedOutputAlpha(0.0f);
            return;
          }
          try {
            double value = std::stod(parts[3]);
            if (value > 1.0) {
              value /= 100.0;
            }
            setFocusedOutputAlpha(static_cast<float>(value));
          } catch (...) {
          }
          return;
        }
        if (outputArg == "DELAY" || outputArg == "LATENCY") {
          if (parts.size() <= 3) {
            triggerToast("output delay: " + std::to_string(focusedOutput().outputDelayMs) + " ms");
            return;
          }
          std::string delayArg = toUpper(parts[3]);
          if (delayArg == "OFF" || delayArg == "NONE") {
            setFocusedOutputDelayMs(0);
            return;
          }
          if (delayArg == "UP" || delayArg == "+" || delayArg == "INC") {
            setFocusedOutputDelayMs(focusedOutput().outputDelayMs + 100);
            return;
          }
          if (delayArg == "DOWN" || delayArg == "-" || delayArg == "DEC") {
            setFocusedOutputDelayMs(focusedOutput().outputDelayMs - 100);
            return;
          }
          try {
            setFocusedOutputDelayMs(std::stoi(parts[3]));
          } catch (...) {
          }
          return;
        }
        if (outputArg == "OVERLAY" || outputArg == "TIMEOVERLAY") {
          if (parts.size() <= 3) {
            toggleFocusedOutputTimeOverlayEnabled();
            return;
          }
          if (auto state = parseToggleWord(3); state) {
            setFocusedOutputTimeOverlayEnabled(*state);
          }
          return;
        }
        if (outputArg == "COLORSPACE" || outputArg == "COLOR" || outputArg == "SPACE") {
          size_t colorTokenIndex = 3;
          if (outputArg == "COLOR" && parts.size() > 4 && toUpper(parts[3]) == "SPACE") {
            colorTokenIndex = 4;
          }
          if (parts.size() <= colorTokenIndex) {
            triggerToast("color space: " + toUpper(normalizeOutputColorSpace(focusedOutput().outputColorSpace)));
            return;
          }
          std::string colorArg = toUpper(parts[colorTokenIndex]);
          if (colorArg == "NEXT") {
            cycleFocusedOutputColorSpace(1);
            return;
          }
          if (colorArg == "PREV" || colorArg == "PREVIOUS") {
            cycleFocusedOutputColorSpace(-1);
            return;
          }
          setFocusedOutputColorSpace(parts[colorTokenIndex]);
          return;
        }
        if (outputArg == "LAYOUT" || outputArg == "MODE") {
          if (parts.size() <= 3) {
            triggerToast("layout: " + normalizeOutputLayoutMode(focusedOutput().outputLayoutMode));
            return;
          }
          std::string modeArg = toUpper(parts[3]);
          if (modeArg == "NEXT") {
            cycleFocusedOutputLayoutMode(1);
            return;
          }
          if (modeArg == "PREV" || modeArg == "PREVIOUS") {
            cycleFocusedOutputLayoutMode(-1);
            return;
          }
          if (modeArg == "SPAN") {
            setFocusedOutputLayoutMode("span");
            return;
          }
          if (modeArg == "DUP" || modeArg == "DUPLICATE" || modeArg == "CLONE") {
            setFocusedOutputLayoutMode("duplicate");
            return;
          }
          return;
        }
        if (outputArg == "ORIENTATION" || outputArg == "ORIENT" || outputArg == "ROTATE" || outputArg == "ROT") {
          if (parts.size() <= 3) {
            triggerToast("orientation: " + outputOrientationLabel(focusedOutput().outputOrientationDegrees));
            return;
          }
          std::string rotateArg = toUpper(parts[3]);
          if (rotateArg == "NEXT" || rotateArg == "CW" || rotateArg == "RIGHT") {
            cycleFocusedOutputOrientation(1);
            return;
          }
          if (rotateArg == "PREV" || rotateArg == "PREVIOUS" || rotateArg == "CCW" || rotateArg == "LEFT") {
            cycleFocusedOutputOrientation(-1);
            return;
          }
          if (rotateArg == "RESET" || rotateArg == "NORMAL") {
            setFocusedOutputOrientationDegrees(0);
            return;
          }
          try {
            setFocusedOutputOrientationDegrees(std::stoi(parts[3]));
          } catch (...) {
          }
          return;
        }
        if (outputArg == "TESTCARD" || outputArg == "TEST") {
          if (parts.size() <= 3) {
            toggleFocusedOutputTestCardEnabled();
            return;
          }
          std::string testArg = toUpper(parts[3]);
          if (testArg == "ALL") {
            if (parts.size() > 4) {
              if (auto state = parseToggleWord(4); state) {
                setAllOutputsTestCardEnabled(*state);
              }
            } else {
              bool anyOff = false;
              for (const auto& out : project_.outputs) {
                if (!out.outputTestCardEnabled) {
                  anyOff = true;
                  break;
                }
              }
              setAllOutputsTestCardEnabled(anyOff);
            }
            return;
          }
          if (testArg == "TOGGLE") {
            toggleFocusedOutputTestCardEnabled();
            return;
          }
          if (auto state = parseToggleWord(3); state) {
            setFocusedOutputTestCardEnabled(*state);
          }
          return;
        }
        try {
          int outputIndex = std::stoi(parts[2]);
          setFocusedOutputIndex(std::max(0, outputIndex - 1));
        } catch (...) {
        }
        return;
      }
      if (value == "STREAM") {
        if (parts.size() <= 2) {
          const OutputTarget& output = focusedOutput();
          std::string protocol = normalizeOutputStreamProtocol(output.streamProtocol);
          triggerToast("stream: "
            + std::string(output.streamEnabled ? "on " : "off ")
            + toUpper(protocol)
            + " " + std::to_string(output.streamBitrateKbps) + "k");
          return;
        }
        std::string streamArg = toUpper(parts[2]);
        if (streamArg == "ON") {
          setFocusedOutputStreamEnabled(true);
          return;
        }
        if (streamArg == "OFF") {
          setFocusedOutputStreamEnabled(false);
          return;
        }
        if (streamArg == "TOGGLE") {
          toggleFocusedOutputStreamEnabled();
          return;
        }
        if (streamArg == "SRT" || streamArg == "RTMP") {
          setFocusedOutputStreamProtocol(toLower(streamArg));
          return;
        }
        if ((streamArg == "PROTO" || streamArg == "PROTOCOL") && parts.size() > 3) {
          setFocusedOutputStreamProtocol(toLower(parts[3]));
          return;
        }
        if ((streamArg == "URL" || streamArg == "TARGET") && parts.size() > 3) {
          setFocusedOutputStreamUrl(joinParts(parts, 3));
          return;
        }
        if ((streamArg == "BITRATE" || streamArg == "RATE") && parts.size() > 3) {
          try {
            setFocusedOutputStreamBitrateKbps(std::stoi(parts[3]));
          } catch (...) {
          }
          return;
        }
        return;
      }
      if (value == "CANVAS") {
        if (parts.size() <= 2) {
          std::string label = project_.outputCanvasEnabled
            ? (std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
            : "off";
          triggerToast("canvas: " + label);
          return;
        }
        std::string canvasArg = toUpper(parts[2]);
        if (canvasArg == "OFF" || canvasArg == "0") {
          setOutputCanvasMode(false);
          return;
        }
        if (canvasArg == "ON" || canvasArg == "1") {
          if (parts.size() > 3) {
            if (auto size = parsePointToken(parts[3]); size) {
              setOutputCanvasMode(true, size->first, size->second);
              return;
            }
          }
          setOutputCanvasMode(true, project_.outputCanvasWidth, project_.outputCanvasHeight);
          return;
        }
        if (canvasArg == "DISPLAY" || canvasArg == "NATIVE") {
          auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
          setOutputCanvasMode(true, rasterW, rasterH);
          return;
        }
        if (canvasArg == "DOUBLE" || canvasArg == "2X") {
          auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
          setOutputCanvasMode(true, std::max(320, rasterW * 2), std::max(180, rasterH));
          return;
        }
        if ((canvasArg == "SIZE" || canvasArg == "SET") && parts.size() > 3) {
          if (auto size = parsePointToken(parts[3]); size) {
            setOutputCanvasMode(true, size->first, size->second);
          }
          return;
        }
        if (auto size = parsePointToken(parts[2]); size) {
          setOutputCanvasMode(true, size->first, size->second);
        }
        return;
      }
      if (value == "VIEW" || value == "PAN") {
        if (parts.size() <= 2) {
          const Deck& focused = focusedDeck();
          triggerToast("view: " + std::to_string(focused.canvasViewX) + "," + std::to_string(focused.canvasViewY));
          return;
        }
        std::string viewArg = toUpper(parts[2]);
        if (viewArg == "LEFT" || viewArg == "RIGHT" || viewArg == "UP" || viewArg == "DOWN") {
          int amount = 100;
          if (parts.size() > 3) {
            try {
              amount = std::stoi(parts[3]);
            } catch (...) {
              amount = 100;
            }
          }
          amount = std::clamp(std::abs(amount), 1, 8192);
          if (viewArg == "LEFT") nudgeFocusedDeckCanvasView(-amount, 0);
          if (viewArg == "RIGHT") nudgeFocusedDeckCanvasView(amount, 0);
          if (viewArg == "UP") nudgeFocusedDeckCanvasView(0, -amount);
          if (viewArg == "DOWN") nudgeFocusedDeckCanvasView(0, amount);
          return;
        }
        if (viewArg == "NUDGE" && parts.size() > 4) {
          try {
            int dx = std::stoi(parts[3]);
            int dy = std::stoi(parts[4]);
            nudgeFocusedDeckCanvasView(dx, dy);
          } catch (...) {
          }
          return;
        }
        if (auto point = parsePointToken(parts[2]); point) {
          setFocusedDeckCanvasView(point->first, point->second);
          return;
        }
        if (parts.size() > 3) {
          try {
            int x = std::stoi(parts[2]);
            int y = std::stoi(parts[3]);
            setFocusedDeckCanvasView(x, y);
          } catch (...) {
          }
        }
        return;
      }
      if (value == "WARP") {
        if (parts.size() <= 2) {
          triggerToast(std::string("warp: ") + (focusedDeck().warpEnabled ? "on" : "off"));
          return;
        }
        std::string warpArg = toUpper(parts[2]);
        if (warpArg == "ON") {
          setFocusedDeckWarpEnabled(true);
          return;
        }
        if (warpArg == "OFF") {
          setFocusedDeckWarpEnabled(false);
          return;
        }
        if (warpArg == "TOGGLE") {
          toggleFocusedDeckWarpEnabled();
          return;
        }
        if (warpArg == "RESET") {
          resetFocusedDeckWarp();
          return;
        }
        std::string corner = warpArg;
        size_t deltaIndex = 3;
        if ((warpArg == "MOVE" || warpArg == "ADJUST" || warpArg == "SET") && parts.size() > 3) {
          corner = parts[3];
          deltaIndex = 4;
        }
        if (parts.size() <= deltaIndex + 1) {
          return;
        }
        try {
          float dx = static_cast<float>(std::stod(parts[deltaIndex]));
          float dy = static_cast<float>(std::stod(parts[deltaIndex + 1]));
          adjustFocusedDeckWarpCorner(corner, dx, dy);
        } catch (...) {
        }
        return;
      }
      if (value == "BLEND") {
        if (parts.size() <= 2) {
          const Deck& focused = focusedDeck();
          triggerToast(
            "blend: L" + std::to_string(static_cast<int>(std::lround(focused.edgeBlendLeft * 100.0f))) +
            " R" + std::to_string(static_cast<int>(std::lround(focused.edgeBlendRight * 100.0f))) +
            " T" + std::to_string(static_cast<int>(std::lround(focused.edgeBlendTop * 100.0f))) +
            " B" + std::to_string(static_cast<int>(std::lround(focused.edgeBlendBottom * 100.0f)))
          );
          return;
        }
        std::string edge = toUpper(parts[2]);
        if (edge == "RESET") {
          setFocusedDeckEdgeBlend("L", 0.0f);
          setFocusedDeckEdgeBlend("R", 0.0f);
          setFocusedDeckEdgeBlend("T", 0.0f);
          setFocusedDeckEdgeBlend("B", 0.0f);
          triggerToast("blend reset");
          return;
        }
        if (edge == "ALL" && parts.size() > 3) {
          if (auto amount = parseBlendValue(3); amount) {
            setFocusedDeckEdgeBlend("L", *amount);
            setFocusedDeckEdgeBlend("R", *amount);
            setFocusedDeckEdgeBlend("T", *amount);
            setFocusedDeckEdgeBlend("B", *amount);
          }
          return;
        }
        if (parts.size() > 3) {
          if (auto amount = parseBlendValue(3); amount) {
            setFocusedDeckEdgeBlend(edge, *amount);
          }
        }
        return;
      }
      if (value == "REFRESH" || value == "RATE" || value == "HZ") {
        if (parts.size() <= 2) {
          triggerToast("video refresh: " + outputRefreshRateLabel());
          return;
        }
        std::string rateArg = toUpper(parts[2]);
        if (rateArg == "AUTO") {
          setOutputRefreshRate(0.0);
          return;
        }
        if (rateArg == "NEXT") {
          cycleOutputRefreshRate(1);
          return;
        }
        if (rateArg == "PREV" || rateArg == "PREVIOUS") {
          cycleOutputRefreshRate(-1);
          return;
        }
        try {
          setOutputRefreshRate(std::stod(parts[2]));
        } catch (...) {
        }
        return;
      }
      if (value == "BITDEPTH" || value == "DEPTH" || value == "FORMAT") {
        if (parts.size() <= 2) {
          triggerToast("video depth: " + outputBitDepthModeLabel() + " (" + outputBitDepthActiveLabelForOutput(project_.focusedOutputIndex) + ")");
          return;
        }
        std::string depthArg = toUpper(parts[2]);
        if (depthArg == "AUTO") {
          setOutputBitDepthMode(0);
          return;
        }
        if (depthArg == "8" || depthArg == "8BIT" || depthArg == "8BPC") {
          setOutputBitDepthMode(8);
          return;
        }
        if (depthArg == "10" || depthArg == "10BIT" || depthArg == "10BPC") {
          setOutputBitDepthMode(10);
          return;
        }
        return;
      }
      if (value == "8BIT" || value == "8BPC") {
        setOutputBitDepthMode(8);
        return;
      }
      if (value == "10BIT" || value == "10BPC") {
        setOutputBitDepthMode(10);
        return;
      }
      if (value == "NATIVE" || value == "AUTO" || value == "DISPLAY") {
        setOutputSizingModeDisplayNative();
        return;
      }
      if (value == "SIZE") {
        if (parts.size() > 2) {
          std::string sub = toUpper(parts[2]);
          if (sub == "DISPLAY" || sub == "NATIVE") {
            setOutputSizingModeDisplayNative();
            return;
          }
        }
        sizeFocusedOutputToSelectedDisplay();
        return;
      }
      if (value == "4K" || value == "UHD" || value == "2160P" || value == "2160") {
        setOutputSizingModeFixed(3840, 2160);
        return;
      }
      if (value == "1440P" || value == "1440") {
        setOutputSizingModeFixed(2560, 1440);
        return;
      }
      if (value == "1080P" || value == "1080") {
        setOutputSizingModeFixed(1920, 1080);
        return;
      }
      if (value == "720P" || value == "720") {
        setOutputSizingModeFixed(1280, 720);
        return;
      }
      if ((value == "CUSTOM" || value == "SET") && parts.size() > 2) {
        applyRasterToken(parts[2]);
        return;
      }
      if (applyRasterToken(value)) {
        return;
      }
      return;
    }
    if (command == "NDI") {
      if (parts.size() == 1) {
        toggleFocusedOutputNdi();
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "ON") {
        setFocusedOutputNdiEnabled(true);
      } else if (value == "OFF") {
        setFocusedOutputNdiEnabled(false);
      } else if (value == "TOGGLE") {
        toggleFocusedOutputNdi();
      } else if (value == "KEY") {
        if (parts.size() == 2) {
          toggleFocusedOutputNdiKey();
        } else {
          std::string keyValue = toUpper(parts[2]);
          if (keyValue == "ON") {
            setFocusedOutputNdiKeyEnabled(true);
          } else if (keyValue == "OFF") {
            setFocusedOutputNdiKeyEnabled(false);
          } else if (keyValue == "TOGGLE") {
            toggleFocusedOutputNdiKey();
          } else if (keyValue == "NAME") {
            setFocusedOutputNdiKeyName(joinParts(parts, 3));
          } else if (keyValue == "DEFAULT" || keyValue == "CLEAR") {
            setFocusedOutputNdiKeyName("");
          }
        }
      } else if (value == "NAME") {
        setFocusedOutputNdiName(joinParts(parts, 2));
      } else if (value == "KEYNAME") {
        setFocusedOutputNdiKeyName(joinParts(parts, 2));
      } else if (value == "DEFAULT" || value == "CLEAR") {
        setFocusedOutputNdiName("");
      } else if (value == "STATUS") {
        triggerToast("ndi: " + currentNdiOutputLabel());
      }
      return;
    }
    if (command == "NDINAME") {
      setFocusedOutputNdiName(joinParts(parts, 1));
      return;
    }
    if (command == "NDIKEY" || command == "NDIKEYER") {
      if (parts.size() <= 1) {
        toggleFocusedOutputNdiKey();
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "ON") {
        setFocusedOutputNdiKeyEnabled(true);
      } else if (value == "OFF") {
        setFocusedOutputNdiKeyEnabled(false);
      } else if (value == "TOGGLE") {
        toggleFocusedOutputNdiKey();
      } else if (value == "NAME") {
        setFocusedOutputNdiKeyName(joinParts(parts, 2));
      } else if (value == "DEFAULT" || value == "CLEAR") {
        setFocusedOutputNdiKeyName("");
      }
      return;
    }
    if (command == "NDIKEYNAME") {
      setFocusedOutputNdiKeyName(joinParts(parts, 1));
      return;
    }
    if (command == "BLACKOUT") {
      std::string val = parts.size() > 1 ? toUpper(parts[1]) : "TOGGLE";
      if (val == "ON")           masterDimmerTarget_ = 0.0;
      else if (val == "OFF")     masterDimmerTarget_ = 1.0;
      else if (val == "TOGGLE")  masterDimmerTarget_ = (masterDimmerTarget_ < 0.5) ? 1.0 : 0.0;
      else if (auto v = parseNumber(1); v) masterDimmerTarget_ = std::clamp(*v, 0.0, 1.0);
      triggerToast(masterDimmerTarget_ < 0.5 ? "blackout ON" : "blackout off");
      markProjectDirty();
      return;
    }
    if (command == "DIMMER") {
      auto value = parseNumber(1);
      if (value) {
        // 0-100 range
        masterDimmerTarget_ = std::clamp(*value / 100.0, 0.0, 1.0);
        triggerToast("dimmer " + std::to_string(static_cast<int>(std::round(masterDimmerTarget_ * 100.0))) + "%");
        markProjectDirty();
      }
      return;
    }
    if (command == "MASTERVOL" || command == "MASTERVOLUME") {
      auto value = parseNumber(1);
      if (value) {
        project_.masterVolume = std::clamp(*value, 0.0, 2.0);
        int pct = static_cast<int>(std::round(project_.masterVolume * 100.0));
        triggerToast("master vol " + std::to_string(pct) + "%");
        markProjectDirty();
      }
      return;
    }
    if (command == "SPEED") {
      auto value = parseNumber(1);
      if (value && *value > 0.0) {
        if (Cue* cue = selectedCueMutable()) {
          if (cue->kind == CueKind::Video) {
            cue->playbackSpeed = std::clamp(*value, 0.25, 4.0);
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << cue->playbackSpeed;
            triggerToast("speed " + ss.str() + "x");
            markProjectDirty();
          }
        }
      }
      return;
    }
    if (command == "SCALE") {
      // Backward compatibility: SCALE sets both X and Y
      auto value = parseNumber(1);
      if (value && *value > 0.0) {
        if (Cue* cue = selectedCueMutable()) {
          cue->outputScaleX = std::clamp(*value, 0.25, 4.0);
          cue->outputScaleY = std::clamp(*value, 0.25, 4.0);
          std::ostringstream ss;
          ss << std::fixed << std::setprecision(2) << cue->outputScaleX;
          triggerToast("scale " + ss.str() + "x");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "SCALEX") {
      auto value = parseNumber(1);
      if (value && *value > 0.0) {
        if (Cue* cue = selectedCueMutable()) {
          cue->outputScaleX = std::clamp(*value, 0.25, 4.0);
          std::ostringstream ss;
          ss << std::fixed << std::setprecision(2) << cue->outputScaleX;
          triggerToast("scale X " + ss.str() + "x");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "SCALEY") {
      auto value = parseNumber(1);
      if (value && *value > 0.0) {
        if (Cue* cue = selectedCueMutable()) {
          cue->outputScaleY = std::clamp(*value, 0.25, 4.0);
          std::ostringstream ss;
          ss << std::fixed << std::setprecision(2) << cue->outputScaleY;
          triggerToast("scale Y " + ss.str() + "x");
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "COLOR" || command == "COLORTAG") {
      std::string tag = parts.size() > 1 ? toLower(parts[1]) : "";
      if (tag == "none" || tag == "clear") tag = "";
      static const std::vector<std::string> kValid =
        {"", "red", "orange", "yellow", "cyan", "blue", "purple", "pink"};
      if (std::find(kValid.begin(), kValid.end(), tag) != kValid.end()) {
        if (Cue* cue = selectedCueMutable()) {
          cue->colorTag = tag;
          triggerToast("color: " + (tag.empty() ? "none" : tag));
          markProjectDirty();
        }
      }
      return;
    }
    if (command == "LOOPCOUNT") {
      auto value = parseNumber(1);
      if (value) {
        if (Cue* cue = selectedCueMutable()) {
          if (cue->kind == CueKind::Video) {
            cue->loopCount = std::max(0, static_cast<int>(*value));
            triggerToast(cue->loopCount == 0 ? "repeats: inf" : "repeats: " + std::to_string(cue->loopCount));
            markProjectDirty();
          }
        }
      }
      return;
    }
    if (command == "CUENOTES") {
      if (parts.size() < 2) return;
      std::string token = parts[1];
      std::string text = parts.size() > 2 ? joinParts(parts, 2) : "";
      Deck& deck = focusedDeckMutable();
      auto index = cueIndexByToken(deck, token);
      if (index) {
        deck.cues[*index].notes = text;
        triggerToast("notes set");
        markProjectDirty();
      }
      return;
    }
  }

  void processEvents() {
    SDL_Event event {};
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT:
          // OS/app close request should quit immediately.
          gShouldQuit.store(true);
          break;
        case SDL_WINDOWEVENT:
          if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
            Uint32 closingWindowId = event.window.windowID;
            if (closingWindowId == SDL_GetWindowID(controlWindow_)) {
              // Closing the main window should exit immediately (no hidden confirm trap).
              gShouldQuit.store(true);
              break;
            }
            if (decksPanelWindow_ && closingWindowId == SDL_GetWindowID(decksPanelWindow_)) {
              // Keep Decks workspace available; treat window close as a show/raise request.
              setDecksPanelVisible(true, true);
              break;
            }
            if (auto outputIndex = outputIndexForWindowId(closingWindowId); outputIndex) {
              setFocusedOutputIndex(*outputIndex);
              setFocusedOutputEnabled(false, false);
              break;
            }
          }
          break;
        case SDL_DROPFILE:
          handleDropFile(event.drop.file);
          SDL_free(event.drop.file);
          break;
        case SDL_MOUSEWHEEL:
          if (event.wheel.windowID == SDL_GetWindowID(controlWindow_)) {
            if (cueSettingsViewportRect_.w > 0 && cueSettingsViewportRect_.h > 0 &&
                pointInRect(mouseX_, mouseY_, cueSettingsViewportRect_) &&
                cueSettingsScrollMax_ > 0) {
              cueSettingsScroll_ = std::clamp(
                cueSettingsScroll_ - event.wheel.y * 36,
                0,
                cueSettingsScrollMax_);
              break;
            }
            if (!settingsOpen_ && masterCueProgrammerExpanded_) {
              bool handledMasterCueWheel = false;
              for (const auto& hit : masterCueSidebarProgramHits_) {
                if (hit.deckIndex < 0 || hit.deckIndex >= static_cast<int>(project_.decks.size())) {
                  continue;
                }
                if (!pointInRect(mouseX_, mouseY_, hit.rowRect)) {
                  continue;
                }
                setFocusedDeckIndex(hit.deckIndex);
                ensureGroupPreset(true);
                int direction = event.wheel.y < 0 ? 1 : -1;
                if (direction != 0) {
                  cycleFocusedGroupSlotCue(hit.deckIndex, direction);
                }
                handledMasterCueWheel = true;
                break;
              }
              if (handledMasterCueWheel) {
                break;
              }
            }
            for (int di = 0; di < static_cast<int>(deckColumnRects_.size()); ++di) {
              if (pointInRect(mouseX_, mouseY_, deckColumnRects_[di])) {
                if (di < static_cast<int>(deckScrolls_.size())) {
                  deckScrolls_[di] = std::max(0, deckScrolls_[di] - event.wheel.y * 36);
                }
                break;
              }
            }
          }
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (event.button.windowID == SDL_GetWindowID(controlWindow_)) {
            if (event.button.button == SDL_BUTTON_RIGHT) {
              handleRightClick(event.button.x, event.button.y);
            } else {
              if (contextMenuOpen_) {
                handleContextMenuClick(event.button.x, event.button.y);
              } else {
                handleMouseDown(event.button.x, event.button.y);
              }
            }
          } else if (decksPanelWindow_ &&
                     event.button.windowID == SDL_GetWindowID(decksPanelWindow_)) {
            handleDecksPanelMouseDown(event.button.x, event.button.y, event.button.button);
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
            mouseX_ = event.motion.x;
            mouseY_ = event.motion.y;
            handleMouseMotion(event.motion.x, event.motion.y);
          }
          break;
        case SDL_KEYDOWN:
          handleKeyDown(event.key.keysym.sym, event.key.keysym.mod, event.key.windowID, event.key.repeat != 0);
          break;
        case SDL_DISPLAYEVENT:
#if defined(SDL_DISPLAYEVENT_CONNECTED) && defined(SDL_DISPLAYEVENT_DISCONNECTED)
          if (event.display.event == SDL_DISPLAYEVENT_CONNECTED ||
              event.display.event == SDL_DISPLAYEVENT_DISCONNECTED) {
            observedDisplayCount_ = SDL_GetNumVideoDisplays();
            refreshDisplayTopology(true);
          }
#else
          observedDisplayCount_ = SDL_GetNumVideoDisplays();
          refreshDisplayTopology(true);
#endif
          break;
        default:
          break;
      }
    }
  }

  void update() {
    flushDirtyProject();
    processRemoteCommands();
    // Poll waveform analysis futures
    {
      std::lock_guard<std::mutex> lk(waveformMutex_);
      for (auto it = waveformFutures_.begin(); it != waveformFutures_.end(); ) {
        if (it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
          waveformCache_[it->first] = it->second.get();
          it = waveformFutures_.erase(it);
        } else ++it;
      }
    }
    // Trigger waveform analysis for selected/active cue
    {
      const Cue* sel = selectedCuePtr();
      if (sel && sel->hasAudio) triggerWaveformAnalysis(sel->path);
      const Cue* act = activeCuePtr();
      if (act && act->hasAudio && act != sel) triggerWaveformAnalysis(act->path);
    }
    Uint64 now = SDL_GetTicks64();
    if (showSplashOverlay_ && splashStartedAt_ > 0 && now - splashStartedAt_ > 2600) {
      showSplashOverlay_ = false;
    }
    double deltaSeconds = lastUpdateTickMs_ == 0 ? 0.0 : static_cast<double>(now - lastUpdateTickMs_) / 1000.0;
    lastUpdateTickMs_ = now;

    if (now - lastDisplayPollMs_ >= 1200) {
      lastDisplayPollMs_ = now;
      int displayCount = SDL_GetNumVideoDisplays();
      if (observedDisplayCount_ < 0) {
        observedDisplayCount_ = displayCount;
      } else if (displayCount != observedDisplayCount_) {
        observedDisplayCount_ = displayCount;
        refreshDisplayTopology(true);
      }
    }

    if (now - lastOutputRecoveryPollMs_ >= 1000) {
      lastOutputRecoveryPollMs_ = now;
      for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
        recoverWindowOutputIfNeeded(outputIndex, false);
      }
    }

    ensureTimecodeFollowerStateSize();
    ensureDeckOpacityTargetsSize();

    // Animate master video dimmer toward target.
    if (std::abs(project_.masterDimmer - masterDimmerTarget_) > 0.001) {
      double dimDuration = panicProfilePending_
        ? std::clamp(project_.panicFadeSeconds, 0.1, 5.0)
        : 0.5;
      double dimSpeed = 1.0 / std::max(0.05, dimDuration);
      double step = dimSpeed * std::max(deltaSeconds, 1.0 / 120.0);
      project_.masterDimmer = std::clamp(
        project_.masterDimmer + std::copysign(std::min(step, std::abs(masterDimmerTarget_ - project_.masterDimmer)), masterDimmerTarget_ - project_.masterDimmer),
        0.0, 1.0);
    }
    if (panicProfilePending_) {
      bool faded = project_.masterDimmer <= 0.05;
      Uint64 timeoutMs = static_cast<Uint64>(
        std::llround(std::clamp(project_.panicFadeSeconds, 0.1, 5.0) * 1000.0 + 150.0));
      bool timeout = panicProfileRequestedAt_ > 0 && (now - panicProfileRequestedAt_) >= timeoutMs;
      if (faded || timeout) {
        panicProfilePending_ = false;
        panicProfileRequestedAt_ = 0;
        executePanicDeckAction(pendingPanicProfileToken_);
      }
    }

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      Deck& deck = project_.decks[deckIndex];
      float target = deckPlaylistOpacityTarget(deckIndex);
      if (std::fabs(deck.playlistOpacity - target) <= 0.001f) {
        deck.playlistOpacity = target;
        continue;
      }
      double fadeTime = std::max(0.05, deck.playlistFadeSeconds);
      double speed = 1.0 / fadeTime;
      double step = speed * std::max(deltaSeconds, 1.0 / 120.0);
      float delta = static_cast<float>(std::copysign(
        std::min(step, static_cast<double>(std::fabs(deck.playlistOpacity - target))),
        static_cast<double>(target - deck.playlistOpacity)));
      deck.playlistOpacity = std::clamp(deck.playlistOpacity + delta, 0.0f, 1.0f);
    }

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      Deck& deck = project_.decks[deckIndex];
      double fromTc = deck.timecodeCurrentSeconds;
      if (deck.timecodeDirty) {
        fromTc = deck.timecodeLastSeconds;
      }
      bool shouldRunTimecode = deck.timecodeRunEnabled && deltaSeconds > 0.0;
      if (shouldRunTimecode && deck.timecodeChaseEnabled &&
          deckIndex >= 0 && deckIndex < static_cast<int>(deckTimecodeHasExternal_.size())) {
        if (!deckTimecodeHasExternal_[deckIndex]) {
          shouldRunTimecode = false;
        } else {
          Uint64 lastExternalMs = deckTimecodeLastExternalMs_[deckIndex];
          Uint64 ageMs = now >= lastExternalMs ? (now - lastExternalMs) : 0;
          Uint64 freewheelMs = static_cast<Uint64>(
            std::llround(std::max(0.0, deck.timecodeFreewheelSeconds) * 1000.0));
          if (ageMs > freewheelMs) {
            shouldRunTimecode = false;
          }
        }
      }
      if (shouldRunTimecode) {
        if (deck.timecodeDirty) {
          fromTc = deck.timecodeLastSeconds;
        } else {
          fromTc = deck.timecodeCurrentSeconds;
        }
        deck.timecodeCurrentSeconds = std::max(0.0, deck.timecodeCurrentSeconds + deltaSeconds);
      }

      if (deck.timecodeChaseEnabled && deck.timecodeTriggerEnabled) {
        processTimecodeTriggersForDeck(deckIndex, fromTc, deck.timecodeCurrentSeconds);
      }
      deck.timecodeLastSeconds = deck.timecodeCurrentSeconds;
      deck.timecodeDirty = false;
    }

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      // Advance browser cue Xvfb startup state machine.
      tickBrowserStartup(deckIndex);

      MediaEngine* engine = mediaEngineForDeck(deckIndex);
      if (!engine) {
        continue;
      }
      engine->update();

      // Animate pattern cues: rebuild frame every tick using wall-clock time.
      const Cue* activeCue = activeCuePtr(deckIndex);
      if (activeCue && activeCue->kind == CueKind::Pattern) {
        // Only animated patterns need continuous rebuilds.
        if (patternTypeIsAnimated(activeCue->path)) {
          engine->rebuildPatternFrame(*activeCue, static_cast<double>(now) / 1000.0);
        }
      }
      if (engine->reachedEnd()) {
        Deck& deck = project_.decks[deckIndex];
        if (deck.activeIndex >= 0 && !deck.cues.empty()) {
          const Cue& activeCue = deck.cues[deck.activeIndex];

          // Per-cue endAction can force or block auto-advance regardless of deck setting.
          bool shouldAdvance = deck.autoAdvance;
          if (activeCue.endAction == CueEndAction::AutoNext) shouldAdvance = true;
          if (activeCue.endAction == CueEndAction::Stop)     shouldAdvance = false;

          int nextIndex = -1;
          int n = static_cast<int>(deck.cues.size());
          if (!trim(activeCue.gotoTarget).empty()) {
            if (auto resolved = cueIndexByToken(deck, activeCue.gotoTarget); resolved) {
              nextIndex = *resolved;
            }
          }
          if (nextIndex < 0) {
            if (deck.shuffle && shouldAdvance && n > 1) {
              // Pick a random cue that isn't the current one.
              nextIndex = deck.activeIndex;
              while (nextIndex == deck.activeIndex) {
                nextIndex = std::rand() % n;
              }
            } else if (deck.activeIndex + 1 < n) {
              nextIndex = deck.activeIndex + 1;
            } else if (deck.playlistLoop) {
              nextIndex = 0;
            }
          }

          if (nextIndex >= 0) {
            if (deck.selectedIndex != nextIndex) {
              deck.selectedIndex = nextIndex;
              if (deckIndex == project_.focusedDeckIndex) {
                onSelectionChanged();
              }
            }
            markProjectDirty();
            if (shouldAdvance) {
              int previousFocus = project_.focusedDeckIndex;
              project_.focusedDeckIndex = deckIndex;
              takeSelected(true, activeCue.transitionToNext);
              project_.focusedDeckIndex = previousFocus;
            }
          }
        }
      }
    }
    updateStatusSnapshot();
    // Update control window preview texture from focused engine's current frame.
    {
      const MediaEngine* eng = focusedMediaEngine();
      const DecodedFrame* frame = eng ? eng->currentFrame() : nullptr;
      if (frame && frame->width > 0 && frame->height > 0 &&
          frame->index != controlPreviewFrameIdx_) {
        controlPreviewFrameIdx_ = frame->index;
        if (!controlPreviewTex_ || controlPreviewTexW_ != frame->width ||
            controlPreviewTexH_ != frame->height) {
          if (controlPreviewTex_) SDL_DestroyTexture(controlPreviewTex_);
          controlPreviewTex_ = SDL_CreateTexture(
            controlRenderer_, SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height);
          controlPreviewTexW_ = frame->width;
          controlPreviewTexH_ = frame->height;
        }
        if (controlPreviewTex_) {
          SDL_UpdateTexture(controlPreviewTex_, nullptr,
                            frame->pixels.data(), frame->width * 4);
        }
      } else if (!frame) {
        // Clear preview when nothing is loaded
        if (controlPreviewTex_) {
          SDL_DestroyTexture(controlPreviewTex_);
          controlPreviewTex_ = nullptr;
          controlPreviewTexW_ = 0;
          controlPreviewTexH_ = 0;
        }
        controlPreviewFrameIdx_ = static_cast<std::uint64_t>(-1);
      }
    }
    // Upload thumbnail if one finished decoding
    if (thumbnailPending_.exchange(false)) {
      std::lock_guard<std::mutex> lk(thumbnailMutex_);
      if (pendingThumbnail_) {
        if (selectedThumbnailTex_) {
          SDL_DestroyTexture(selectedThumbnailTex_);
          selectedThumbnailTex_ = nullptr;
        }
        const auto& f = *pendingThumbnail_;
        selectedThumbnailTex_ = SDL_CreateTexture(controlRenderer_, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STREAMING, f.width, f.height);
        if (selectedThumbnailTex_) {
          SDL_UpdateTexture(selectedThumbnailTex_, nullptr, f.pixels.data(), f.width * 4);
          selectedThumbnailTexW_ = f.width;
          selectedThumbnailTexH_ = f.height;
        }
        pendingThumbnail_.reset();
      }
    }
  }

  void render() {
    animationNow_ = SDL_GetTicks64();
    renderControlWindow();
    renderDecksPanel();
    if (outputRuntimes_.size() != project_.outputs.size()) {
      rebuildOutputRuntimes();
    }
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (!project_.outputs[outputIndex].enabled) {
        continue;
      }
      renderOutputWindow(outputIndex);
    }
  }

  bool decksPanelVisible() const {
    if (!decksPanelWindow_) {
      return false;
    }
    Uint32 flags = SDL_GetWindowFlags(decksPanelWindow_);
    return (flags & SDL_WINDOW_HIDDEN) == 0;
  }

  void setDecksPanelVisible(bool visible, bool raiseWindow = false) {
    if (!decksPanelWindow_) {
      return;
    }
    if (visible) {
      SDL_ShowWindow(decksPanelWindow_);
      if ((SDL_GetWindowFlags(decksPanelWindow_) & SDL_WINDOW_MINIMIZED) != 0) {
        SDL_RestoreWindow(decksPanelWindow_);
      }
      if (raiseWindow) {
        SDL_RaiseWindow(decksPanelWindow_);
      }
    } else {
      SDL_HideWindow(decksPanelWindow_);
    }
  }

  void updateDecksPanelVisibility() {
    if (!decksPanelWindow_) {
      return;
    }
    // Deck window is now a primary always-on workspace.
    setDecksPanelVisible(true, false);
  }

  void renderQuitConfirm() {
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);

    // Semi-transparent dark overlay
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0x0F, 0x38, 0x0F, 160);
    SDL_Rect overlay {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &overlay);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    // Dialog panel: larger default sizing for legibility.
    SDL_Rect dialog {(width - 420) / 2, (height - 232) / 2, 420, 232};
    Primitives::drawFramedPanel(controlRenderer_, dialog, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));

    drawText(controlRenderer_, fontLarge_, "QUIT DECKBOY?", colorFromRgba(kScreenDeepColor), dialog.x + 28, dialog.y + 36);

    // YES / NO buttons
    quitYesBtn_ = {dialog.x + 32,  dialog.y + 130, 156, 54};
    quitNoBtn_  = {dialog.x + 232, dialog.y + 130, 156, 54};
    Primitives::drawFramedPanel(controlRenderer_, quitYesBtn_, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    Primitives::drawFramedPanel(controlRenderer_, quitNoBtn_,  colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    drawCenteredText(controlRenderer_, fontBase_, "YES", colorFromRgba(kScreenLightColor), quitYesBtn_);
    drawCenteredText(controlRenderer_, fontBase_, "NO",  colorFromRgba(kScreenLightColor), quitNoBtn_);

    drawText(controlRenderer_, fontSmall_, "esc or N to cancel", colorFromRgba(kScreenInkSoftColor), dialog.x + 32, dialog.y + 200);
  }

  void renderStartupDialog() {
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);

    // Full-screen dim
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0x0F, 0x38, 0x0F, 200);
    SDL_Rect overlay {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &overlay);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    // Dialog panel
    const int kDW = 660;
    const int kDH = 440;
    SDL_Rect dialog {(width - kDW) / 2, (height - kDH) / 2, kDW, kDH};
    Primitives::drawFramedPanel(controlRenderer_, dialog, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));

    // Title + file name
    int tx = dialog.x + 36;
    TTF_Font* titleFont = fontPixel_ ? fontPixel_ : fontLarge_;
    drawText(controlRenderer_, titleFont, "DECKBOY_0.01", colorFromRgba(kScreenDeepColor), tx, dialog.y + 44);
    drawText(controlRenderer_, fontSmall_, "dot-matrix cue deck", colorFromRgba(kScreenInkSoftColor), tx, dialog.y + 82);
    drawText(controlRenderer_, fontBase_, "Choose startup mode:", colorFromRgba(kScreenDeepColor), tx, dialog.y + 122);

    std::string fname = currentProjectFile_.empty() ? "default.playboy" : currentProjectFile_.filename().string();
    bool hasSavedFile = !currentProjectFile_.empty() && fs::exists(currentProjectFile_);
    if (hasSavedFile) {
      drawText(controlRenderer_, fontSmall_, "Previous show file:", colorFromRgba(kScreenDeepColor), tx, dialog.y + 156);
      drawText(controlRenderer_, fontSmall_, ellipsizeToPixelWidth(fontSmall_, fname, dialog.w - 72),
               colorFromRgba(kScreenDarkColor), tx, dialog.y + 180);
    } else {
      drawText(controlRenderer_, fontSmall_, "No previous show file found at startup path.", colorFromRgba(kScreenDarkColor),
               tx, dialog.y + 164);
    }

    // Buttons
    int buttonY = dialog.y + 270;
    int buttonW = 184;
    int buttonH = 58;
    startupNewBtn_ = {dialog.x + 36, buttonY, buttonW, buttonH};
    startupLoadBtn_ = {startupNewBtn_.x + buttonW + 10, buttonY, buttonW, buttonH};
    startupOpenSavedBtn_ = {startupLoadBtn_.x + buttonW + 10, buttonY, buttonW, buttonH};

    Primitives::drawFramedPanel(controlRenderer_, startupNewBtn_, colorFromRgba(kScreenMidColor),
                                colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
    drawCenteredText(controlRenderer_, fontBase_, "NEW SHOW FILE", colorFromRgba(kScreenDeepColor), startupNewBtn_);

    SDL_Color loadFill = hasSavedFile ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kShellOuterColor);
    SDL_Color loadText = hasSavedFile ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenMidColor);
    Primitives::drawFramedPanel(controlRenderer_, startupLoadBtn_, loadFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    drawCenteredText(controlRenderer_, fontBase_, hasSavedFile ? "OPEN PREVIOUS" : "NO PREVIOUS", loadText, startupLoadBtn_);

    Primitives::drawFramedPanel(controlRenderer_, startupOpenSavedBtn_, colorFromRgba(kScreenMidColor),
                                colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
    drawCenteredText(controlRenderer_, fontBase_, "OPEN SAVED...", colorFromRgba(kScreenDeepColor), startupOpenSavedBtn_);

    drawText(controlRenderer_, fontSmall_, "N=new  Enter/P=previous  O=open saved picker",
             colorFromRgba(kScreenInkSoftColor), tx, dialog.y + 356);
    drawText(controlRenderer_, fontSmall_, "Esc=continue with current session",
             colorFromRgba(kScreenInkSoftColor), tx, dialog.y + 378);
  }

  void renderSplashOverlay() {
    if (!showSplashOverlay_) {
      return;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);

    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0x0F, 0x38, 0x0F, 245);
    SDL_Rect full {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &full);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    SDL_Rect card {(width - 760) / 2, (height - 430) / 2, 760, 430};
    Primitives::drawFramedPanel(controlRenderer_, card, colorFromRgba(kShellInnerColor),
                                colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));

    TTF_Font* titleFont = fontPixel_ ? fontPixel_ : fontLarge_;
    drawText(controlRenderer_, titleFont, "DECKBOY", colorFromRgba(kScreenDeepColor), card.x + 36, card.y + 34);
    drawText(controlRenderer_, fontBase_, "dot-matrix cue deck", colorFromRgba(kScreenDarkColor), card.x + 38, card.y + 82);

    SDL_Rect bootRect {card.x + 36, card.y + 126, card.w - 72, 184};
    Primitives::drawFramedPanel(controlRenderer_, bootRect, colorFromRgba(kScreenLightColor),
                                colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));

    static const std::array<const char*, 5> kBootLines {
      "initializing deck runtime...",
      "loading outputs...",
      "starting compositor...",
      "opening companion port 5510...",
      "arming safety guards..."
    };
    Uint64 now = SDL_GetTicks64();
    Uint64 elapsed = splashStartedAt_ > 0 ? (now - splashStartedAt_) : 0;
    int visibleLines = std::clamp(static_cast<int>(elapsed / 380), 1, static_cast<int>(kBootLines.size()));
    for (int i = 0; i < visibleLines; ++i) {
      drawText(controlRenderer_, fontMono_, kBootLines[i], colorFromRgba(kScreenDeepColor),
               bootRect.x + 12, bootRect.y + 14 + i * 30);
    }
    if (((now / 300) % 2) == 0) {
      drawText(controlRenderer_, fontMono_, "_", colorFromRgba(kScreenDeepColor),
               bootRect.x + 12 + 9 * 14, bootRect.y + 14 + (visibleLines - 1) * 30);
    }

    drawText(controlRenderer_, fontBase_, "press ENTER to start",
             colorFromRgba(kScreenDeepColor), card.x + 36, card.y + card.h - 74);
    drawText(controlRenderer_, fontSmall_, "Esc or click to skip",
             colorFromRgba(kScreenDarkColor), card.x + 36, card.y + card.h - 44);
  }

  void renderDecksPanel() {
    if (!decksPanelWindow_ || !decksPanelRenderer_) {
      return;
    }
    decksPanelButtons_.clear();
    decksPanelRowHits_.clear();
    decksPanelCueHits_.clear();
    decksPanelDeckButtonHits_.clear();
    masterCueRowHits_.clear();

    int panelW = 0, panelH = 0;
    SDL_GetWindowSize(decksPanelWindow_, &panelW, &panelH);

    // Keep this workspace in the same Game Boy green family as the control UI.
    SDL_SetRenderDrawColor(decksPanelRenderer_, 0x0F, 0x38, 0x0F, 0xFF);
    SDL_RenderClear(decksPanelRenderer_);
    SDL_Rect skyBand {0, 0, panelW, std::max(64, panelH / 4)};
    SDL_SetRenderDrawColor(decksPanelRenderer_, 0x1F, 0x48, 0x1F, 0xFF);
    SDL_RenderFillRect(decksPanelRenderer_, &skyBand);

    int headerH = 54;
    SDL_Rect headerRect {0, 0, panelW, headerH};
    SDL_SetRenderDrawColor(decksPanelRenderer_, 0x1F, 0x48, 0x1F, 0xFF);
    SDL_RenderFillRect(decksPanelRenderer_, &headerRect);
    drawText(decksPanelRenderer_, fontBase_, "DECKS", colorFromRgba(kScreenLightColor), 10, 10);
    drawText(decksPanelRenderer_, fontSmall_, "deck list + playlist view", colorFromRgba(kScreenLightColor), 12, 31);

    SDL_Rect colHeader {0, headerH, panelW, 30};
    SDL_SetRenderDrawColor(decksPanelRenderer_, 0x17, 0x42, 0x17, 0xFF);
    SDL_RenderFillRect(decksPanelRenderer_, &colHeader);
    const int colXIndex = 12;
    const int colXDeck = 52;
    const int colXSel = 292;
    const int colXAct = 386;
    const int colXLayer = 484;
    const int colXState = 560;
    const int colXTc = 648;
    drawText(decksPanelRenderer_, fontSmall_, "#", colorFromRgba(kScreenMidColor), colXIndex, headerH + 7);
    drawText(decksPanelRenderer_, fontSmall_, "deck", colorFromRgba(kScreenMidColor), colXDeck, headerH + 7);
    drawText(decksPanelRenderer_, fontSmall_, "selected", colorFromRgba(kScreenMidColor), colXSel, headerH + 7);
    drawText(decksPanelRenderer_, fontSmall_, "active", colorFromRgba(kScreenMidColor), colXAct, headerH + 7);
    drawText(decksPanelRenderer_, fontSmall_, "layer", colorFromRgba(kScreenMidColor), colXLayer, headerH + 7);
    drawText(decksPanelRenderer_, fontSmall_, "state", colorFromRgba(kScreenMidColor), colXState, headerH + 7);
    drawText(decksPanelRenderer_, fontSmall_, "timecode", colorFromRgba(kScreenMidColor), colXTc, headerH + 7);

    const int footerTop = panelH - 46;
    int y = headerH + 28;
    int rowH = 34;

    auto cueLabel = [&](const Deck& deck, int cueIndex) -> std::string {
      if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
        return "--";
      }
      return cueDisplayToken(deck.cues[cueIndex], cueIndex);
    };

    int deckCount = static_cast<int>(project_.decks.size());
    constexpr int kDeckGridMinH = 132;
    int trackerTopY = y;
    int trackerBottomLimit = std::max(trackerTopY + rowH, footerTop - kDeckGridMinH - 6);
    int trackerRowsVisible = std::max(1, (trackerBottomLimit - trackerTopY + 1) / (rowH + 2));
    int trackerStart = 0;
    if (deckCount > trackerRowsVisible) {
      int focusedDeck = std::clamp(project_.focusedDeckIndex, 0, std::max(0, deckCount - 1));
      trackerStart = std::clamp(focusedDeck - trackerRowsVisible / 2, 0, deckCount - trackerRowsVisible);
    }

    int trackerRowsRendered = 0;
    for (int slot = 0; slot < trackerRowsVisible; ++slot) {
      int i = trackerStart + slot;
      if (i >= deckCount) {
        break;
      }
      const auto& deck = project_.decks[i];
      y = trackerTopY + slot * (rowH + 2);

      bool focused = i == project_.focusedDeckIndex;
      SDL_Rect rowRect {6, y, panelW - 12, rowH};
      SDL_Color rowFill = focused
        ? SDL_Color {48, 98, 48, 255}
        : ((i % 2) == 0 ? SDL_Color {20, 56, 20, 255} : SDL_Color {16, 48, 16, 255});
      Primitives::fillRect(decksPanelRenderer_, rowRect, rowFill);
      Primitives::strokeRect(decksPanelRenderer_, rowRect, colorFromRgba(kScreenDeepColor));

      // Compact tracker-style status fields.
      std::ostringstream idx;
      idx << std::setw(2) << std::setfill('0') << (i + 1);
      std::string deckDisplay = deck.name.empty() ? deckDefaultName(i) : deck.name;
      if (deckDisplay.size() > 24) {
        deckDisplay = deckDisplay.substr(0, 23) + "~";
      }
      std::string selCue = cueLabel(deck, deck.selectedIndex);
      std::string actCue = cueLabel(deck, deck.activeIndex);
      int layerIndex = primaryLayerIndexForDeck(i);

      std::string statusStr = "STOP";
      if (i >= 0 && i < static_cast<int>(deckRuntimes_.size()) && deckRuntimes_[i].mediaEngine) {
        if (deckRuntimes_[i].mediaEngine->state() == TransportState::Playing) {
          statusStr = "PLAY";
        } else if (deckRuntimes_[i].mediaEngine->state() == TransportState::Paused) {
          statusStr = "PAUS";
        }
      }
      std::string tcStr = formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps);
      tcStr = ellipsizeToPixelWidth(fontSmall_, tcStr, std::max(48, panelW - colXTc - 12));

      SDL_Color rowInk = focused ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenMidColor);
      drawText(decksPanelRenderer_, fontSmall_, idx.str(), rowInk, colXIndex, y + 8);
      drawText(decksPanelRenderer_, fontSmall_, deckDisplay, rowInk, colXDeck, y + 8);
      drawText(decksPanelRenderer_, fontSmall_, selCue, rowInk, colXSel, y + 8);
      drawText(decksPanelRenderer_, fontSmall_, actCue, rowInk, colXAct, y + 8);
      drawText(decksPanelRenderer_, fontSmall_, std::to_string(layerIndex), rowInk, colXLayer, y + 8);
      drawText(decksPanelRenderer_, fontSmall_, statusStr, rowInk, colXState, y + 8);
      drawText(decksPanelRenderer_, fontSmall_, tcStr, rowInk, colXTc, y + 8);

      DecksPanelRowHit rowHit;
      rowHit.deckIndex = i;
      rowHit.rowRect = rowRect;
      rowHit.groupRect = {0, 0, 0, 0};
      decksPanelRowHits_.push_back(rowHit);
      trackerRowsRendered += 1;
    }

    if (deckCount > trackerRowsRendered) {
      std::string trackerPage = "Deck rows " + std::to_string(trackerStart + 1) + "-" +
        std::to_string(trackerStart + trackerRowsRendered) + "/" + std::to_string(deckCount);
      drawText(decksPanelRenderer_, fontSmall_, trackerPage, colorFromRgba(kScreenInkSoftColor),
               std::max(8, panelW - 170), headerH + 4);
    }

    // Deck playlists now live in this separate window.
    int deckGridTop = trackerTopY + trackerRowsRendered * (rowH + 2) + 10;
    int deckGridBottom = footerTop - 8;
    if (deckGridBottom > deckGridTop + 72) {
      SDL_Rect deckGridRect {8, deckGridTop, panelW - 16, deckGridBottom - deckGridTop};
      Primitives::drawFramedPanel(decksPanelRenderer_, deckGridRect, colorFromRgba(kShellInnerColor),
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(decksPanelRenderer_, fontBase_, "DECK PLAYLISTS", colorFromRgba(kScreenDeepColor),
               deckGridRect.x + 8, deckGridRect.y + 3);
      drawText(decksPanelRenderer_, fontSmall_, "click cue: select   right-click cue: take",
               colorFromRgba(kScreenInkSoftColor), deckGridRect.x + 154, deckGridRect.y + 7);

      int colGap = 10;
      int availableW = deckGridRect.w - 10;
      int maxVisibleCols = std::max(1, availableW / 360);
      int visibleCols = std::max(1, std::min(deckCount, maxVisibleCols));
      int focusedDeck = std::clamp(project_.focusedDeckIndex, 0, std::max(0, deckCount - 1));
      int deckStart = 0;
      if (deckCount > visibleCols) {
        deckStart = std::clamp(focusedDeck - visibleCols / 2, 0, deckCount - visibleCols);
      }
      int colW = (availableW - (visibleCols - 1) * colGap) / std::max(1, visibleCols);
      int colH = deckGridRect.h - 44;
      int colY = deckGridRect.y + 40;

      for (int slot = 0; slot < visibleCols; ++slot) {
        int deckIndex = deckStart + slot;
        if (deckIndex >= deckCount) {
          break;
        }
        const Deck& deck = project_.decks[deckIndex];
        int colX = deckGridRect.x + 4 + slot * (colW + colGap);
        SDL_Rect colRect {colX, colY, colW, colH};
        bool focused = deckIndex == project_.focusedDeckIndex;

        SDL_Color colFill = focused ? colorFromRgba(kScreenLightColor) : colorFromRgba(kShellInnerColor);
        Primitives::drawFramedPanel(decksPanelRenderer_, colRect, colFill,
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));

        SDL_Rect hdr {colRect.x + 2, colRect.y + 2, colRect.w - 4, 88};
        SDL_Color hdrFill = focused ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor);
        SDL_Color hdrInk = focused ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
        Primitives::drawFramedPanel(decksPanelRenderer_, hdr, hdrFill,
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));

        std::string deckName = deck.name.empty() ? deckDefaultName(deckIndex) : deck.name;
        std::string sel = cueLabel(deck, deck.selectedIndex);
        std::string act = cueLabel(deck, deck.activeIndex);
        std::string hdrText = deckName + "  S:" + sel + " A:" + act;
        std::string hdrInfo = transportStatusLabel(deckIndex) + "  tc "
          + formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps);

        constexpr int btnGap = 6;
        constexpr int takeW = 74;
        constexpr int stopW = 74;
        constexpr int btnH = 22;
        int btnY = hdr.y + hdr.h - btnH - 3;
        int stopX = hdr.x + hdr.w - 4 - stopW;
        int takeX = stopX - btnGap - takeW;
        int textMaxW = std::max(36, takeX - (hdr.x + 4) - 4);

        drawText(decksPanelRenderer_, fontBase_,
                 ellipsizeToPixelWidth(fontBase_, hdrText, textMaxW),
                 hdrInk, hdr.x + 4, hdr.y + 4);
        drawText(decksPanelRenderer_, fontSmall_,
                 ellipsizeToPixelWidth(fontSmall_, hdrInfo, textMaxW),
                 hdrInk, hdr.x + 4, hdr.y + 26);
        drawText(decksPanelRenderer_, fontSmall_,
                 ellipsizeToPixelWidth(fontSmall_, "layer " + std::to_string(primaryLayerIndexForDeck(deckIndex)), textMaxW),
                 hdrInk, hdr.x + 4, hdr.y + 42);

        SDL_Rect takeBtn {takeX, btnY, takeW, btnH};
        Primitives::drawFramedPanel(decksPanelRenderer_, takeBtn,
                                    focused ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(decksPanelRenderer_, fontSmall_, "Take",
                         focused ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor), takeBtn);
        decksPanelDeckButtonHits_.push_back({deckIndex, kDecksPanelDeckActionTake, takeBtn});

        SDL_Rect stopBtn {stopX, btnY, stopW, btnH};
        Primitives::drawFramedPanel(decksPanelRenderer_, stopBtn,
                                    colorFromRgba(kScreenMidColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
        drawCenteredText(decksPanelRenderer_, fontSmall_, "Stop",
                         colorFromRgba(kScreenDeepColor), stopBtn);
        decksPanelDeckButtonHits_.push_back({deckIndex, kDecksPanelDeckActionStop, stopBtn});

        int listTop = hdr.y + hdr.h + 4;
        int listH = std::max(0, (colRect.y + colRect.h) - listTop - 4);
        SDL_Rect listRect {colRect.x + 4, listTop, colRect.w - 8, listH};
        Primitives::drawFramedPanel(decksPanelRenderer_, listRect, colorFromRgba(kScreenLightColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        if (listRect.h <= 10) {
          drawText(decksPanelRenderer_, fontSmall_, "(resize for cues)",
                   colorFromRgba(kScreenInkSoftColor), listRect.x + 6, listRect.y + 6);
        } else if (deck.cues.empty()) {
          drawText(decksPanelRenderer_, fontSmall_, "(empty deck)",
                   colorFromRgba(kScreenInkSoftColor), listRect.x + 6, listRect.y + 6);
        } else {
          int rowGap = 3;
          int cueRowH = 26;
          int rowsVisible = std::max(1, (listRect.h - 6) / (cueRowH + rowGap));
          int cueStart = 0;
          if (static_cast<int>(deck.cues.size()) > rowsVisible) {
            int anchor = deck.selectedIndex >= 0 ? deck.selectedIndex : 0;
            cueStart = std::clamp(anchor - rowsVisible / 2, 0, static_cast<int>(deck.cues.size()) - rowsVisible);
          }
          for (int rowSlot = 0; rowSlot < rowsVisible; ++rowSlot) {
            int cueIndex = cueStart + rowSlot;
            if (cueIndex >= static_cast<int>(deck.cues.size())) {
              break;
            }
            SDL_Rect cueRect {listRect.x + 3, listRect.y + 3 + rowSlot * (cueRowH + rowGap), listRect.w - 6, cueRowH};
            bool selectedCue = cueIndex == deck.selectedIndex;
            bool activeCue = cueIndex == deck.activeIndex;
            SDL_Color cueFill = selectedCue
              ? colorFromRgba(kScreenMidColor)
              : (activeCue ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kShellInnerColor));
            SDL_Color cueInk = activeCue ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
            Primitives::drawFramedPanel(decksPanelRenderer_, cueRect, cueFill,
                                        colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
            const Cue& cue = deck.cues[cueIndex];
            std::string cueNum = cueLabel(deck, cueIndex);
            std::string cueText = cueNum + "  " + cue.name;
            if (activeCue) {
              cueText = "> " + cueText;
            }
            drawText(decksPanelRenderer_, fontSmall_,
                     ellipsizeToPixelWidth(fontSmall_, cueText, cueRect.w - 6),
                     cueInk, cueRect.x + 4, cueRect.y + 5);
            decksPanelCueHits_.push_back({deckIndex, cueIndex, cueRect});
          }
        }
      }

      if (deckCount > visibleCols) {
        std::string pageLabel = "Decks " + std::to_string(deckStart + 1) + "-" +
          std::to_string(deckStart + visibleCols) + "/" + std::to_string(deckCount);
        drawText(decksPanelRenderer_, fontSmall_, pageLabel, colorFromRgba(kScreenInkSoftColor),
                 deckGridRect.x + deckGridRect.w - 120, deckGridRect.y + 3);
      }
    }

    drawText(decksPanelRenderer_, fontSmall_,
             "master scenes live in the main sidebar",
             colorFromRgba(kScreenInkSoftColor), 10, panelH - 22);

    SDL_RenderPresent(decksPanelRenderer_);
  }

  void renderDeckSidebar(const SDL_Rect& panel) {
    masterCueSidebarButtons_.clear();
    masterCueSidebarRows_.clear();
    masterCueSidebarProgramHits_.clear();
    if (panel.w <= 0 || panel.h <= 0) {
      return;
    }
    Primitives::drawFramedPanel(controlRenderer_, panel, colorFromRgba(kShellInnerColor),
                                colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));

    SDL_Rect head {panel.x + 2, panel.y + 2, panel.w - 4, 24};
    Primitives::drawFramedPanel(controlRenderer_, head, colorFromRgba(kScreenMidColor),
                                colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
    drawText(controlRenderer_, fontSmall_, "MASTER SCENES", colorFromRgba(kScreenDeepColor), head.x + 6, head.y + 4);
    if (!project_.groupPresets.empty()) {
      int focus = std::clamp(project_.focusedGroupPresetIndex, 0, static_cast<int>(project_.groupPresets.size()) - 1);
      std::string label = "SCN" + std::to_string(focus + 1);
      drawText(controlRenderer_, fontSmall_, label, colorFromRgba(kScreenDeepColor),
               head.x + head.w - 42, head.y + 4);
    }

    auto cueLabel = [&](const Deck& deck, int cueIndex) -> std::string {
      if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
        return "--";
      }
      return cueDisplayToken(deck.cues[cueIndex], cueIndex);
    };

    auto drawMasterSidebarButton = [&](int x, int y, int w, int h, const std::string& label, int action,
                                       bool emphasized = false) {
      SDL_Rect rect {x, y, w, h};
      SDL_Color fill = emphasized ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor);
      SDL_Color ink = emphasized ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
      Primitives::drawFramedPanel(controlRenderer_, rect, fill,
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, label, ink, rect);
      masterCueSidebarButtons_.push_back({rect, action, -1});
    };

    int controlsY = panel.y + 30;
    constexpr int btnGap = 4;
    constexpr int btnH = 22;
    int innerX = panel.x + 6;
    int innerW = std::max(120, panel.w - 12);

    int row1BtnW = std::max(34, (innerW - btnGap * 4) / 5);
    int bx = innerX;
    drawMasterSidebarButton(bx, controlsY, row1BtnW, btnH, "<SC", kDecksPanelActionGroupPrev);
    bx += row1BtnW + btnGap;
    drawMasterSidebarButton(bx, controlsY, row1BtnW, btnH, "SC>", kDecksPanelActionGroupNext);
    bx += row1BtnW + btnGap;
    drawMasterSidebarButton(bx, controlsY, row1BtnW, btnH, "New", kDecksPanelActionGroupNew);
    bx += row1BtnW + btnGap;
    drawMasterSidebarButton(bx, controlsY, row1BtnW, btnH, "Del", kDecksPanelActionGroupDelete);
    bx += row1BtnW + btnGap;
    drawMasterSidebarButton(bx, controlsY, row1BtnW, btnH, "SCENE", kDecksPanelActionGroupFire, true);

    controlsY += btnH + btnGap;
    int row2BtnW = std::max(44, (innerW - btnGap * 3) / 4);
    bx = innerX;
    drawMasterSidebarButton(bx, controlsY, row2BtnW, btnH, "Name", kDecksPanelActionGroupRename);
    bx += row2BtnW + btnGap;
    drawMasterSidebarButton(bx, controlsY, row2BtnW, btnH, "CapSel", kDecksPanelActionGroupCaptureSelected);
    bx += row2BtnW + btnGap;
    drawMasterSidebarButton(bx, controlsY, row2BtnW, btnH, "CapAct", kDecksPanelActionGroupCaptureActive);
    bx += row2BtnW + btnGap;
    drawMasterSidebarButton(bx, controlsY, row2BtnW, btnH,
                            masterCueProgrammerExpanded_ ? "Prog-" : "Prog+",
                            kDecksPanelActionGroupProgramToggle);

    int listTopY = controlsY + btnH + 6;

    if (masterCueProgrammerExpanded_) {
      int availableBelow = panel.h - (listTopY - panel.y) - 112;
      int progH = std::clamp(availableBelow, 112, 208);
      SDL_Rect progRect {panel.x + 4, listTopY, panel.w - 8, progH};
      Primitives::drawFramedPanel(controlRenderer_, progRect, colorFromRgba(kScreenLightColor),
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(controlRenderer_, fontSmall_, "PROGRAM FOCUSED SCENE", colorFromRgba(kScreenDeepColor),
               progRect.x + 6, progRect.y + 3);
      drawText(controlRenderer_, fontSmall_, "Sel/Act assign, Byp skips, -/+ cycles slot cue",
               colorFromRgba(kScreenInkSoftColor), progRect.x + 6, progRect.y + 17);

      const GroupPreset* preset = focusedGroupPreset();
      if (!preset) {
        drawText(controlRenderer_, fontSmall_, "No scene selected", colorFromRgba(kScreenInkSoftColor),
                 progRect.x + 8, progRect.y + 38);
        drawText(controlRenderer_, fontSmall_, "Press New, then assign deck slots", colorFromRgba(kScreenInkSoftColor),
                 progRect.x + 8, progRect.y + 54);
      } else {
        std::string presetName = groupPresetLabel(std::clamp(project_.focusedGroupPresetIndex, 0,
                                      static_cast<int>(project_.groupPresets.size()) - 1));
        drawText(controlRenderer_, fontSmall_, ellipsizeToPixelWidth(fontSmall_, "Editing: " + presetName, progRect.w - 18),
                 colorFromRgba(kScreenDeepColor), progRect.x + 6, progRect.y + 31);

        int deckCount = static_cast<int>(project_.decks.size());
        int rowH = 24;
        int rowGap = 3;
        int rowsTop = progRect.y + 48;
        int rowsBottom = progRect.y + progRect.h - 4;
        int rowsVisible = std::max(1, (rowsBottom - rowsTop) / (rowH + rowGap));
        int focusedDeck = std::clamp(project_.focusedDeckIndex, 0, std::max(0, deckCount - 1));
        int rowStart = 0;
        if (deckCount > rowsVisible) {
          rowStart = std::clamp(focusedDeck - rowsVisible / 2, 0, deckCount - rowsVisible);
        }

        for (int slot = 0; slot < rowsVisible; ++slot) {
          int deckIndex = rowStart + slot;
          if (deckIndex >= deckCount) {
            break;
          }
          const Deck& deck = project_.decks[deckIndex];
          SDL_Rect rowRect {progRect.x + 3, rowsTop + slot * (rowH + rowGap), progRect.w - 6, rowH};
          bool focused = deckIndex == project_.focusedDeckIndex;
          SDL_Color rowFill = focused
            ? SDL_Color {48, 98, 48, 255}
            : ((deckIndex % 2) == 0 ? SDL_Color {20, 56, 20, 255} : SDL_Color {16, 48, 16, 255});
          Primitives::fillRect(controlRenderer_, rowRect, rowFill);
          Primitives::strokeRect(controlRenderer_, rowRect, colorFromRgba(kScreenDeepColor));

          constexpr int kBtnGap = 2;
          constexpr int kSelW = 34;
          constexpr int kActW = 34;
          constexpr int kBypW = 38;
          constexpr int kStepW = 24;
          constexpr int kBtnH = 18;
          int by = rowRect.y + (rowRect.h - kBtnH) / 2;
          int plusX = rowRect.x + rowRect.w - 4 - kStepW;
          int minusX = plusX - kBtnGap - kStepW;
          int bypX = minusX - kBtnGap - kBypW;
          int actX = bypX - kBtnGap - kActW;
          int selX = actX - kBtnGap - kSelW;

          SDL_Rect selectedRect {selX, by, kSelW, kBtnH};
          SDL_Rect activeRect {actX, by, kActW, kBtnH};
          SDL_Rect bypassRect {bypX, by, kBypW, kBtnH};
          SDL_Rect minusRect {minusX, by, kStepW, kBtnH};
          SDL_Rect plusRect {plusX, by, kStepW, kBtnH};

          auto drawProgBtn = [&](const SDL_Rect& r, const std::string& text) {
            Primitives::drawFramedPanel(controlRenderer_, r, colorFromRgba(kScreenMidColor),
                                        colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
            drawCenteredText(controlRenderer_, fontSmall_, text, colorFromRgba(kScreenDeepColor), r);
          };
          drawProgBtn(selectedRect, "Sel");
          drawProgBtn(activeRect, "Act");
          drawProgBtn(bypassRect, "Byp");
          drawProgBtn(minusRect, "-");
          drawProgBtn(plusRect, "+");

          std::string deckName = deck.name.empty() ? deckDefaultName(deckIndex) : deck.name;
          std::string slotText = "\xc3\x97";
          if (deckIndex < static_cast<int>(preset->slots.size())) {
            const GroupSlot& slotDef = preset->slots[deckIndex];
            if (!slotDef.bypass) {
              auto cueIndex = cueIndexById(deck, slotDef.cueId);
              if (cueIndex) {
                slotText = cueLabel(deck, *cueIndex) + " " + deck.cues[*cueIndex].name;
              } else {
                slotText = "?? missing";
              }
            }
          }
          std::string rowText = "D" + std::to_string(deckIndex + 1) + " " + deckName + " -> " + slotText;
          int textMaxW = std::max(42, selectedRect.x - (rowRect.x + 4) - 4);
          drawText(controlRenderer_, fontSmall_,
                   ellipsizeToPixelWidth(fontSmall_, rowText, textMaxW),
                   focused ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenMidColor),
                   rowRect.x + 4, rowRect.y + 4);

          masterCueSidebarProgramHits_.push_back(
            {deckIndex, rowRect, selectedRect, activeRect, bypassRect, minusRect, plusRect});
        }
      }

      listTopY = progRect.y + progRect.h + 6;
    }

    SDL_Rect listRect {panel.x + 4, listTopY, panel.w - 8, panel.h - (listTopY - panel.y) - 8};
    if (listRect.w <= 0 || listRect.h <= 24) {
      return;
    }
    Primitives::drawFramedPanel(controlRenderer_, listRect, colorFromRgba(kScreenLightColor),
                                colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));

    int totalMasters = static_cast<int>(project_.groupPresets.size());
    int lineRowH = 40;
    int lineRowsPerPage = std::max(1, (listRect.h - 6) / (lineRowH + 2));
    int focusMaster = std::clamp(project_.focusedGroupPresetIndex, 0, std::max(0, totalMasters - 1));
    int pageStart = (lineRowsPerPage > 0) ? (focusMaster / lineRowsPerPage) * lineRowsPerPage : 0;
    int visibleRows = std::max(0, std::min(totalMasters - pageStart, lineRowsPerPage));

    if (visibleRows <= 0) {
      drawText(controlRenderer_, fontSmall_, "no scenes yet",
               colorFromRgba(kScreenInkSoftColor), listRect.x + 8, listRect.y + 10);
      drawText(controlRenderer_, fontSmall_, "Use New to add one",
               colorFromRgba(kScreenInkSoftColor), listRect.x + 8, listRect.y + 26);
      return;
    }

    int deckCount = static_cast<int>(project_.decks.size());
    int rowY = listRect.y + 4;
    for (int i = 0; i < visibleRows; ++i) {
      int presetIndex = pageStart + i;
      const GroupPreset& preset = project_.groupPresets[presetIndex];
      bool focusedMaster = presetIndex == project_.focusedGroupPresetIndex;

      SDL_Rect rowRect {listRect.x + 2, rowY, listRect.w - 4, lineRowH};
      SDL_Color rowFill = focusedMaster
        ? SDL_Color {48, 98, 48, 255}
        : ((i % 2) == 0 ? SDL_Color {20, 56, 20, 255} : SDL_Color {16, 48, 16, 255});
      Primitives::fillRect(controlRenderer_, rowRect, rowFill);
      Primitives::strokeRect(controlRenderer_, rowRect, colorFromRgba(kScreenDeepColor));

      SDL_Rect fireRect {rowRect.x + rowRect.w - 58, rowRect.y + 4, 54, rowRect.h - 8};
      SDL_Color fireFill = focusedMaster ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor);
      SDL_Color fireInk = focusedMaster ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
      Primitives::drawFramedPanel(controlRenderer_, fireRect, fireFill,
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "TAKE", fireInk, fireRect);

      std::string masterName = preset.name.empty() ? groupPresetDefaultName(presetIndex) : preset.name;
      std::string prefix = "[" + std::to_string(presetIndex + 1) + "] " + masterName;

      std::string slotSummary;
      int deckPreviewCount = std::min(deckCount, 3);
      for (int deckIndex = 0; deckIndex < deckPreviewCount; ++deckIndex) {
        std::string chunk = " D" + std::to_string(deckIndex + 1) + ":";
        if (deckIndex < static_cast<int>(preset.slots.size())) {
          const GroupSlot& slot = preset.slots[deckIndex];
          if (slot.bypass) {
            chunk += "\xc3\x97";
          } else {
            const Deck& deck = project_.decks[deckIndex];
            auto cueIndex = cueIndexById(deck, slot.cueId);
            if (cueIndex) {
              chunk += cueLabel(deck, *cueIndex);
            } else {
              chunk += "??";
            }
          }
        } else {
          chunk += "--";
        }
        slotSummary += chunk;
      }
      if (deckCount > deckPreviewCount) {
        slotSummary += " ...";
      }

      int textMaxW = std::max(40, fireRect.x - (rowRect.x + 4) - 4);
      std::string line = prefix;
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, line, textMaxW),
               colorFromRgba(kScreenLightColor), rowRect.x + 3, rowRect.y + 4);
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, slotSummary, textMaxW),
               colorFromRgba(kScreenMidColor), rowRect.x + 3, rowRect.y + 20);

      masterCueSidebarRows_.push_back({presetIndex, rowRect, fireRect});
      rowY += lineRowH + 3;
    }

    if (totalMasters > lineRowsPerPage) {
      std::string page = std::to_string(pageStart + 1) + "-" +
        std::to_string(pageStart + visibleRows) + "/" + std::to_string(totalMasters);
      drawText(controlRenderer_, fontSmall_, page, colorFromRgba(kScreenInkSoftColor),
               listRect.x + listRect.w - 84, listRect.y + listRect.h - 16);
    }
  }

  void renderControlWindow() {
    int numDecks = static_cast<int>(project_.decks.size());
    deckScrolls_.resize(numDecks, 0);
    deckColumnRects_.resize(numDecks);
    deckListClipRects_.resize(numDecks);
    outputMenuButtons_.clear();
    masterCueSidebarButtons_.clear();
    masterCueSidebarRows_.clear();
    masterCueSidebarProgramHits_.clear();
    deckSidebarToggleRect_ = SDL_Rect {};
    std::fill(deckColumnRects_.begin(), deckColumnRects_.end(), SDL_Rect {0, 0, 0, 0});
    std::fill(deckListClipRects_.begin(), deckListClipRects_.end(), SDL_Rect {0, 0, 0, 0});

    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);
    layoutButtons(height);

    SDL_SetRenderDrawColor(controlRenderer_, red(kShellShadowColor), green(kShellShadowColor), blue(kShellShadowColor), 255);
    SDL_RenderClear(controlRenderer_);

    SDL_Rect shell {10, 10, width - 20, height - 20};
    Primitives::drawFramedPanel(controlRenderer_, shell, colorFromRgba(kShellOuterColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellInnerColor));

    // Global header strip
    SDL_Rect header {shell.x + 4, shell.y + 4, shell.w - 8, kGlobalHeaderH};
    Primitives::drawFramedPanel(controlRenderer_, header, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));
    // Pixel-font title with animated sparkle stars
    {
      TTF_Font* titleFont = fontPixel_ ? fontPixel_ : fontLarge_;
      const std::string appTitle = std::string(kAppTitle);
      drawText(controlRenderer_, titleFont, appTitle, colorFromRgba(kScreenDeepColor), header.x + 14, header.y + 8);
      if (project_.uiTransitionsEnabled) {
        SDL_Color starC = colorFromRgba(kScreenDeepColor);
        // Three orbiting sparkles with phase offsets
        int titleW = 0;
        if (fontPixel_) { TTF_SizeUTF8(fontPixel_, appTitle.c_str(), &titleW, nullptr); }
        else { titleW = 120; }
        int starBaseX = header.x + 14 + titleW + 8;
        int starBaseY = header.y + 22;
        for (int s = 0; s < 3; ++s) {
          double phase = static_cast<double>(animationNow_) * 0.0018 + s * 2.094;
          int sx = starBaseX + 14 * s + static_cast<int>(std::sin(phase) * 5.0);
          int sy = starBaseY + static_cast<int>(std::cos(phase * 1.3) * 4.0);
          int arm = 3 + (s % 2);
          starC.a = static_cast<Uint8>(140 + 115 * std::abs(std::sin(phase * 0.7)));
          drawStar(controlRenderer_, sx, sy, arm, starC);
        }
      }
    }
    drawText(controlRenderer_, fontSmall_, std::string(kAppModelLabel) + "  ·  dot-matrix cue deck", colorFromRgba(kScreenDeepColor), header.x + 14, header.y + 36);
    const Deck& focDeck = focusedDeck();
    std::string outputStatus = "outputs:";
    int outputPreview = std::min(static_cast<int>(project_.outputs.size()), 4);
    for (int i = 0; i < outputPreview; ++i) {
      outputStatus += " O" + std::to_string(i + 1);
      outputStatus += project_.outputs[i].enabled ? "*" : "-";
    }
    if (static_cast<int>(project_.outputs.size()) > outputPreview) {
      outputStatus += " +" + std::to_string(static_cast<int>(project_.outputs.size()) - outputPreview);
    }
    std::string tcStatus = "tc " + formatTimecode(focDeck.timecodeCurrentSeconds, focDeck.timecodeFps);
    std::string companionStatus = companionReady_
      ? "net " + std::to_string(companionPort_)
      : "net off";
    int rightMetaX = header.x + header.w - 560;
    drawText(controlRenderer_, fontSmall_, outputStatus, colorFromRgba(kScreenDeepColor), rightMetaX, header.y + 6);
    drawText(controlRenderer_, fontSmall_, companionStatus + "  " + tcStatus,
             colorFromRgba(kScreenDeepColor), rightMetaX, header.y + 22);
    drawText(controlRenderer_, fontSmall_,
             ellipsizeToPixelWidth(fontSmall_, "show: " + currentProjectLabel(), 540),
             colorFromRgba(kScreenDeepColor), rightMetaX, header.y + 38);

    std::string deckSummary = "decks: ";
    int deckPreview = std::min(3, static_cast<int>(project_.decks.size()));
    for (int i = 0; i < deckPreview; ++i) {
      if (i > 0) {
        deckSummary += " | ";
      }
      const Deck& deck = project_.decks[i];
      const Cue* liveCue = activeCuePtr(i);
      std::string liveName = liveCue ? liveCue->name : "--";
      deckSummary += "D" + std::to_string(i + 1) + " LIVE " + liveName;
    }
    if (static_cast<int>(project_.decks.size()) > deckPreview) {
      deckSummary += " | +" + std::to_string(static_cast<int>(project_.decks.size()) - deckPreview);
    }
    drawText(controlRenderer_, fontSmall_,
             ellipsizeToPixelWidth(fontSmall_, deckSummary, std::max(120, rightMetaX - (header.x + 316))),
             colorFromRgba(kScreenDarkColor), header.x + 316, header.y + 38);
    // Master volume fader (horizontal slider at right of header)
    {
      constexpr int kFaderW = 132;
      constexpr int kFaderH = 16;
      int fx = header.x + header.w - 150;
      int fy = header.y + header.h - kFaderH - 8;
      masterFaderRect_ = {fx, fy, kFaderW, kFaderH};
      SDL_Rect track = masterFaderRect_;
      Primitives::fillRect(controlRenderer_, track, colorFromRgba(kScreenDeepColor));
      int fillW = static_cast<int>(std::clamp(project_.masterVolume, 0.0, 2.0) / 2.0 * kFaderW);
      SDL_Rect fill {track.x, track.y, fillW, track.h};
      SDL_Color faderCol = project_.masterVolume > 1.0 ? SDL_Color{180, 80, 20, 255} : colorFromRgba(kScreenDarkColor);
      Primitives::fillRect(controlRenderer_, fill, faderCol);
      Primitives::strokeRect(controlRenderer_, track, colorFromRgba(kScreenMidColor));
      int volPct = static_cast<int>(std::round(project_.masterVolume * 100.0));
      drawText(controlRenderer_, fontSmall_, "vol " + std::to_string(volPct) + "%",
               colorFromRgba(kScreenDeepColor), track.x, track.y - 18);
    }
    // Settings gear button
    {
      auto drawHeaderActionButton = [&](SDL_Rect& rect, const std::string& label) {
        Primitives::drawFramedPanel(controlRenderer_, rect, colorFromRgba(kShellInnerColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, label, colorFromRgba(kScreenDeepColor), rect);
      };

      int fileBtnY = header.y + 7;
      int fileBtnH = 28;
      int fileStartX = std::max(header.x + 220, header.x + header.w - 960);
      fileNewBtnRect_ = {fileStartX, fileBtnY, 62, fileBtnH};
      fileOpenBtnRect_ = {fileStartX + 70, fileBtnY, 66, fileBtnH};
      fileSaveBtnRect_ = {fileStartX + 144, fileBtnY, 66, fileBtnH};
      fileSaveAsBtnRect_ = {fileStartX + 218, fileBtnY, 82, fileBtnH};
      drawHeaderActionButton(fileNewBtnRect_, "New");
      drawHeaderActionButton(fileOpenBtnRect_, "Open");
      drawHeaderActionButton(fileSaveBtnRect_, "Save");
      drawHeaderActionButton(fileSaveAsBtnRect_, "SaveAs");

      decksPanelToggleRect_ = {header.x + header.w - 698, header.y + 6, 62, 46};
      bool decksShown = decksPanelVisible();
      SDL_Color decksFill = decksShown ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kShellInnerColor);
      SDL_Color decksInk = decksShown ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
      Primitives::drawFramedPanel(controlRenderer_, decksPanelToggleRect_, decksFill,
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawCenteredText(controlRenderer_, fontSmall_, "decks", decksInk, decksPanelToggleRect_);

      settingsGearRect_ = {header.x + header.w - 622, header.y + 6, 62, 46};
      SDL_Color gearFill = settingsOpen_ ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kShellInnerColor);
      SDL_Color gearInk  = settingsOpen_ ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
      Primitives::drawFramedPanel(controlRenderer_, settingsGearRect_, gearFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawCenteredText(controlRenderer_, fontSmall_, "prefs", gearInk, settingsGearRect_);
      // BLK (blackout) button
      {
        blackoutBtnRect_ = {header.x + header.w - 684, header.y + 6, 58, 46};
        bool isBlacked = masterDimmerTarget_ < 0.5;
        SDL_Color blkFill = isBlacked ? SDL_Color{160, 18, 18, 255} : colorFromRgba(kShellInnerColor);
        SDL_Color blkInk  = isBlacked ? SDL_Color{255, 180, 180, 255} : colorFromRgba(kScreenDeepColor);
        Primitives::drawFramedPanel(controlRenderer_, blackoutBtnRect_, blkFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "BLK", blkInk, blackoutBtnRect_);
      }
    }

    // Operational strip: workflow + signal flow + output routing rows.
    int routeDeckCount = std::max(1, static_cast<int>(project_.decks.size()));
    int routeRowsVisible = std::max(1, std::min(routeDeckCount, 4));
    int outputStripH = 84 + routeRowsVisible * 24;
    SDL_Rect outputStrip {shell.x + 4, header.y + kGlobalHeaderH + 4, shell.w - 8, outputStripH};
    Primitives::drawFramedPanel(controlRenderer_, outputStrip, colorFromRgba(kShellInnerColor),
                                colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));

    deckSidebarToggleRect_ = SDL_Rect {};
    drawText(controlRenderer_, fontSmall_,
             "WORKFLOW: IMPORT -> CUES -> ROUTE -> TAKE -> LAYERS -> MASTER SCENES",
             colorFromRgba(kScreenDeepColor), outputStrip.x + 8, outputStrip.y + 6);

    std::string signalFlow = "Signal Flow: ";
    int flowDeckCount = std::min(static_cast<int>(project_.decks.size()), 3);
    for (int deckIndex = 0; deckIndex < flowDeckCount; ++deckIndex) {
      auto out = primaryOutputIndexForDeck(deckIndex);
      int layer = primaryLayerIndexForDeck(deckIndex);
      if (deckIndex > 0) {
        signalFlow += "   ";
      }
      signalFlow += "Deck " + std::to_string(deckIndex + 1) + " -> ";
      signalFlow += out ? outputLabel(*out) : "Unrouted";
      signalFlow += " -> ";
      signalFlow += (layer <= 0 ? "BG" : ("L" + std::to_string(layer)));
    }
    if (flowDeckCount == 0) {
      signalFlow += "No decks";
    }
    drawText(controlRenderer_, fontSmall_,
             ellipsizeToPixelWidth(fontSmall_, signalFlow, outputStrip.w - 16),
             colorFromRgba(kScreenDarkColor), outputStrip.x + 8, outputStrip.y + 24);

    int topRowY = outputStrip.y + 42;
    drawText(controlRenderer_, fontSmall_, "OUTPUT ARM",
             colorFromRgba(kScreenDeepColor), outputStrip.x + 8, topRowY + 4);

    SDL_Rect addOutputBtn {outputStrip.x + outputStrip.w - 84, topRowY, 78, 22};
    Primitives::drawFramedPanel(controlRenderer_, addOutputBtn, colorFromRgba(kScreenMidColor),
                                colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
    drawCenteredText(controlRenderer_, fontSmall_, "+OUTPUT", colorFromRgba(kScreenDeepColor), addOutputBtn);
    outputMenuButtons_.push_back({addOutputBtn, -1, -1, kOutputMenuActionAddOutput});

    int toggleX = outputStrip.x + 96;
    constexpr int kOutputToggleCellW = 132;
    constexpr int kOutputToggleCellH = 24;
    constexpr int kOutputToggleGap = 4;
    int toggleLimitX = addOutputBtn.x - 6;
    int hiddenOutputs = 0;
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (toggleX + kOutputToggleCellW > toggleLimitX) {
        hiddenOutputs = static_cast<int>(project_.outputs.size()) - outputIndex;
        break;
      }
      const OutputTarget& output = project_.outputs[outputIndex];
      bool focusedOutput = outputIndex == project_.focusedOutputIndex;
      SDL_Rect cellRect {toggleX, topRowY, kOutputToggleCellW, kOutputToggleCellH};
      SDL_Color cellFill = focusedOutput ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor);
      SDL_Color cellInk = focusedOutput ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
      Primitives::drawFramedPanel(controlRenderer_, cellRect, cellFill,
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      std::string typeToken = normalizeOutputType(output.outputType) == "stream"
        ? toUpper(normalizeOutputStreamProtocol(output.streamProtocol))
        : "HDMI";
      std::string stateToken = output.enabled ? "ARMED" : "OFF";
      if (normalizeOutputType(output.outputType) == "stream" && output.streamEnabled) {
        stateToken = "LIVE";
      }
      if (output.ndiEnabled) {
        stateToken += " NDI";
      }
      if (output.outputTestCardEnabled) {
        stateToken += " TEST";
      }
      int orientation = normalizeOutputOrientationDegrees(output.outputOrientationDegrees);
      if (orientation != 0) {
        stateToken += " R" + std::to_string(orientation);
      }
      drawText(controlRenderer_, fontSmall_,
               "O" + std::to_string(outputIndex + 1) + " " + typeToken,
               cellInk, cellRect.x + 5, cellRect.y + 3);
      drawText(controlRenderer_, fontSmall_, stateToken, cellInk, cellRect.x + 5, cellRect.y + 12);

      SDL_Rect armBtn {cellRect.x + cellRect.w - 44, cellRect.y + 3, 40, cellRect.h - 6};
      SDL_Color armFill = output.enabled ? colorFromRgba(kScreenLightColor) : colorFromRgba(kShellOuterColor);
      SDL_Color armInk = output.enabled ? colorFromRgba(kScreenDeepColor) : colorFromRgba(kScreenDarkColor);
      Primitives::drawFramedPanel(controlRenderer_, armBtn, armFill,
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, output.enabled ? "ON" : "OFF", armInk, armBtn);
      if (focusedOutput) {
        Primitives::strokeRect(controlRenderer_, insetRect(cellRect, 1), colorFromRgba(kScreenLightColor));
      }

      outputMenuButtons_.push_back({armBtn, -1, outputIndex, kOutputMenuActionToggle});
      outputMenuButtons_.push_back({cellRect, -1, outputIndex, kOutputMenuActionFocus});
      toggleX += kOutputToggleCellW + kOutputToggleGap;
    }
    if (hiddenOutputs > 0) {
      drawText(controlRenderer_, fontSmall_, "+" + std::to_string(hiddenOutputs),
               colorFromRgba(kScreenInkSoftColor), addOutputBtn.x - 26, topRowY + 4);
    }

    // Single routing strip: Deck -> Output -> Layer (editable inline).
    int rowBaseY = topRowY + 26;
    int deckCount = static_cast<int>(project_.decks.size());
    int deckStart = 0;
    if (deckCount > routeRowsVisible) {
      int focusDeck = std::clamp(project_.focusedDeckIndex, 0, std::max(0, deckCount - 1));
      deckStart = std::clamp(focusDeck - routeRowsVisible / 2, 0, deckCount - routeRowsVisible);
    }
    for (int slot = 0; slot < routeRowsVisible; ++slot) {
      int deckIndex = deckStart + slot;
      if (deckIndex >= deckCount) {
        break;
      }
      bool focusedDeck = deckIndex == project_.focusedDeckIndex;
      SDL_Rect rowRect {outputStrip.x + 8, rowBaseY + slot * 24, outputStrip.w - 16, 22};
      SDL_Color rowFill = focusedDeck ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor);
      SDL_Color rowInk = focusedDeck ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
      Primitives::drawFramedPanel(controlRenderer_, rowRect, rowFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));

      auto primaryOut = primaryOutputIndexForDeck(deckIndex);
      int currentOutput = primaryOut ? *primaryOut : -1;
      int outputCount = static_cast<int>(project_.outputs.size());
      int routeOutput = currentOutput >= 0
        ? currentOutput
        : (outputCount > 0
            ? std::clamp(project_.focusedOutputIndex, 0, std::max(0, outputCount - 1))
            : -1);
      auto assignment = (routeOutput >= 0 && outputCount > 0)
        ? assignmentIndexForDeckOutput(deckIndex, routeOutput)
        : std::nullopt;
      bool assigned = assignment.has_value();
      int layerIndex = assigned ? std::clamp(project_.layerAssignments[*assignment].layerIndex, 0, 255) : 0;
      std::string layerLabel = assigned ? (layerIndex <= 0 ? "BG" : ("L" + std::to_string(layerIndex))) : "--";
      std::string outputLabelText = routeOutput >= 0 ? ("O" + std::to_string(routeOutput + 1)) : "--";

      std::string lead = (focusedDeck ? "\xe2\x96\xb8 " : "  ")
        + deckLabel(deckIndex) + " -> ";
      drawText(controlRenderer_, fontSmall_, lead, rowInk, rowRect.x + 6, rowRect.y + 4);

      int bx = rowRect.x + 118;
      SDL_Rect outPrevBtn {bx, rowRect.y + 2, 18, 18};
      SDL_Rect outValBtn {bx + 20, rowRect.y + 2, 56, 18};
      SDL_Rect outNextBtn {bx + 78, rowRect.y + 2, 18, 18};
      SDL_Rect layerDecBtn {bx + 104, rowRect.y + 2, 18, 18};
      SDL_Rect layerValBtn {bx + 124, rowRect.y + 2, 42, 18};
      SDL_Rect layerIncBtn {bx + 168, rowRect.y + 2, 18, 18};
      SDL_Rect assignBtn {bx + 194, rowRect.y + 2, 74, 18};

      auto drawMiniBtn = [&](const SDL_Rect& rect, const std::string& text, bool lit = false) {
        SDL_Color fill = lit ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kShellInnerColor);
        SDL_Color ink = lit ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
        Primitives::drawFramedPanel(controlRenderer_, rect, fill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
        drawCenteredText(controlRenderer_, fontSmall_, text, ink, rect);
      };
      drawMiniBtn(outPrevBtn, "<");
      drawMiniBtn(outValBtn, outputLabelText, routeOutput == project_.focusedOutputIndex);
      drawMiniBtn(outNextBtn, ">");
      drawMiniBtn(layerDecBtn, "-");
      drawMiniBtn(layerValBtn, layerLabel, assigned);
      drawMiniBtn(layerIncBtn, "+");
      drawMiniBtn(assignBtn, assigned ? "UNLINK" : "LINK", assigned);

      outputMenuButtons_.push_back({outPrevBtn, deckIndex, routeOutput, kOutputMenuActionRouteOutputPrev});
      outputMenuButtons_.push_back({outValBtn, deckIndex, routeOutput, kOutputMenuActionRouteFocusDeck});
      outputMenuButtons_.push_back({outNextBtn, deckIndex, routeOutput, kOutputMenuActionRouteOutputNext});
      outputMenuButtons_.push_back({layerDecBtn, deckIndex, routeOutput, kOutputMenuActionRouteLayerDec});
      outputMenuButtons_.push_back({layerIncBtn, deckIndex, routeOutput, kOutputMenuActionRouteLayerInc});
      outputMenuButtons_.push_back({assignBtn, deckIndex, routeOutput, kOutputMenuActionRouteAssignToggle});
      outputMenuButtons_.push_back({rowRect, deckIndex, routeOutput, kOutputMenuActionRouteFocusDeck});
    }

    // Content area: below header + output strip, above buttons
    int contentY = outputStrip.y + outputStrip.h + 4;
    int contentH = (height - 74) - contentY - 4;
    int contentLeft = shell.x + 4;
    int contentRight = shell.x + shell.w - 4;
    int colGap = 6;

    int shelfW = std::clamp(shell.w / 5, 260, 320);
    int sidebarW = std::clamp(shell.w / 4, 280, 380);
    int mainW = contentRight - contentLeft - shelfW - sidebarW - colGap * 2;
    if (mainW < 560) {
      int deficit = 560 - mainW;
      int shaveShelf = std::min(deficit / 2, std::max(0, shelfW - 220));
      shelfW -= shaveShelf;
      deficit -= shaveShelf;
      int shaveSidebar = std::min(deficit, std::max(0, sidebarW - 240));
      sidebarW -= shaveSidebar;
      mainW = contentRight - contentLeft - shelfW - sidebarW - colGap * 2;
    }

    int playlistDeckIndex = project_.decks.empty()
      ? -1
      : std::clamp(project_.focusedDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    if (playlistDeckIndex >= 0) {
      SDL_Rect shelfRect {contentLeft, contentY, shelfW, contentH};
      deckColumnRects_[playlistDeckIndex] = shelfRect;
      renderPlaylistColumn(shelfRect, playlistDeckIndex);
    }

    SDL_Rect mainPanel {contentLeft + shelfW + colGap, contentY, mainW, contentH};
    if (mainPanel.w > 0) {
      Primitives::drawFramedPanel(controlRenderer_, mainPanel, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));
      renderMainPanel(mainPanel);
    }

    SDL_Rect sidebarRect {mainPanel.x + mainPanel.w + colGap, contentY, sidebarW, contentH};
    renderDeckSidebar(sidebarRect);

    renderButtons();
    renderToast(width);
    if (confirmQuit_) {
      renderQuitConfirm();
    }
    if (showStartupDialog_ && !showSplashOverlay_) {
      renderStartupDialog();
    }
    // Popups rendered last (on top)
    renderContextMenu();
    renderSettingsModal();
    renderSplashOverlay();
    SDL_RenderPresent(controlRenderer_);
  }

  void renderPlaylistColumn(const SDL_Rect& col, int deckIndex) {
    const Deck& deck = project_.decks[deckIndex];
    bool focused = (deckIndex == project_.focusedDeckIndex);
    if (deckOpacityFaderRects_.size() < project_.decks.size()) {
      deckOpacityFaderRects_.resize(project_.decks.size(), SDL_Rect {});
    }

    // Cartridge shelf header.
    SDL_Rect colHeader {col.x, col.y, col.w, kColHeaderH};
    SDL_Color headerFill = focused ? colorFromRgba(kScreenMidColor) : colorFromRgba(kShellInnerColor);
    Primitives::drawFramedPanel(controlRenderer_, colHeader, headerFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));
    std::string deckName = deck.name.empty() ? deckDefaultName(deckIndex) : deck.name;
    auto outputIndex = primaryOutputIndexForDeck(deckIndex);
    int layerIndex = primaryLayerIndexForDeck(deckIndex);
    std::string layerToken = layerIndex <= 0 ? "BG" : ("L" + std::to_string(layerIndex));
    std::string headerLine = "Deck " + std::to_string(deckIndex + 1) + "  " + deckName
      + "  |  " + (outputIndex ? outputLabel(*outputIndex) : "Unrouted")
      + "  |  " + layerToken;
    drawText(controlRenderer_, fontSmall_,
             ellipsizeToPixelWidth(fontSmall_, headerLine, col.w - 20),
             colorFromRgba(kScreenDeepColor), col.x + 10, col.y + 7);
    std::string audioLabel = deck.audioOutputDeviceName.empty() ? "(default audio)" : deck.audioOutputDeviceName;
    drawText(controlRenderer_, fontSmall_,
             ellipsizeToPixelWidth(fontSmall_, "audio: " + audioLabel, col.w - 20),
             colorFromRgba(kScreenDarkColor), col.x + 10, col.y + 24);
    const Cue* activeCue = activeCuePtr(deckIndex);
    const MediaEngine* engine = mediaEngineForDeck(deckIndex);
    std::string stateStr = "■ ";
    if (engine) {
      switch (engine->state()) {
        case TransportState::Playing: stateStr = "▶ "; break;
        case TransportState::Paused:  stateStr = "‖ "; break;
        default: break;
      }
    }
    std::string activeName = activeCue ? activeCue->name : "no cue loaded";
    drawText(controlRenderer_, fontSmall_, ellipsizeToPixelWidth(fontSmall_, stateStr + activeName, col.w - 20),
             colorFromRgba(kScreenDeepColor), col.x + 10, col.y + 42);

    // Cue list area
    int listAreaY = col.y + kColHeaderH + 4;
    int listAreaH = col.h - kColHeaderH - 4 - kColFooterH - 4;
    SDL_Rect clipFrame {col.x + 4, listAreaY, col.w - 8, std::max(0, listAreaH)};
    deckListClipRects_[deckIndex] = clipFrame;

    Primitives::drawFramedPanel(controlRenderer_, clipFrame, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    SDL_Rect clipRect {clipFrame.x + 8, clipFrame.y + 8, clipFrame.w - 16, clipFrame.h - 16};
    SDL_RenderSetClipRect(controlRenderer_, &clipRect);
    int y = clipRect.y - deckScrolls_[deckIndex];
    for (int ci = 0; ci < static_cast<int>(deck.cues.size()); ++ci) {
      SDL_Rect row {clipRect.x, y, clipRect.w, kRowHeight};
      renderCueRow(row, deckIndex, ci);
      y += kRowHeight + 8;
    }
    // Empty deck — show import hints.
    if (deck.cues.empty()) {
      int hx = clipRect.x + clipRect.w / 2 - 30;
      int hy = clipRect.y + clipRect.h / 2 - 20;
      drawText(controlRenderer_, fontSmall_, "I  import", colorFromRgba(kScreenDeepColor), hx, hy);
      drawText(controlRenderer_, fontSmall_, "B  browser", colorFromRgba(kScreenDeepColor), hx, hy + 20);
      drawText(controlRenderer_, fontSmall_, "P  pattern", colorFromRgba(kScreenDeepColor), hx, hy + 40);
    }
    SDL_RenderSetClipRect(controlRenderer_, nullptr);

    // Column footer (routing info)
    int footerY = col.y + col.h - kColFooterH;
    SDL_Rect footer {col.x, footerY, col.w, kColFooterH};
    Primitives::drawFramedPanel(controlRenderer_, footer, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));
    int displayIndex = outputIndex ? outputDisplayIndex(*outputIndex) : deck.outputDisplayIndex;
    std::string layerName = (layerIndex >= 0 && layerIndex < static_cast<int>(project_.layerNames.size()))
      ? project_.layerNames[layerIndex]
      : "?";
    std::string rasterLabel = outputIndex ? outputResolutionLabelForOutput(*outputIndex) : outputResolutionLabel(deckIndex);
    std::string routing = deckOutputRoutingLabel(deckIndex)
      + "  layer:" + layerName
      + "  disp:" + std::to_string(displayIndex + 1)
      + "  res:" + rasterLabel
      + "  " + (deck.autoAdvance ? "auto" : "man")
      + "  " + (deck.playlistLoop ? "loop" : "once")
      + "  " + (deck.shuffle ? "shuf" : "seq");
    drawText(controlRenderer_, fontSmall_, routing, colorFromRgba(kScreenDeepColor), col.x + 8, footerY + 8);
    std::string routing2 = std::string("tc:") + (deck.timecodeChaseEnabled ? "chase" : "free")
      + "  op:" + std::to_string(static_cast<int>(std::lround(std::clamp(deck.playlistOpacity, 0.0f, 1.0f) * 100.0f))) + "%"
      + (deck.playlistAutoFade ? " af:on" : " af:off");
    drawText(controlRenderer_, fontSmall_, routing2, colorFromRgba(kScreenDeepColor), col.x + 8, footerY + 22);
    SDL_Rect opacityRail {col.x + 8, footerY + kColFooterH - 12, col.w - 16, 8};
    Primitives::drawFramedPanel(controlRenderer_, opacityRail, colorFromRgba(kScreenLightColor),
                    colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));
    int fillW = static_cast<int>(std::lround(std::clamp(deck.playlistOpacity, 0.0f, 1.0f) * (opacityRail.w - 4)));
    fillW = std::clamp(fillW, 0, opacityRail.w - 4);
    SDL_Rect opacityFill {opacityRail.x + 2, opacityRail.y + 2, fillW, opacityRail.h - 4};
    Primitives::fillRect(controlRenderer_, opacityFill, colorFromRgba(kScreenDarkColor));
    deckOpacityFaderRects_[deckIndex] = opacityRail;
    // Animated shuffle sparkle when enabled
    if (deck.shuffle && project_.uiTransitionsEnabled) {
      double phase = static_cast<double>(animationNow_) * 0.002;
      int sy = footerY + kColFooterH / 2 + static_cast<int>(std::sin(phase) * 2.0);
      SDL_Color sc = colorFromRgba(kScreenDeepColor);
      sc.a = static_cast<Uint8>(160 + 95 * std::abs(std::sin(phase * 0.8)));
      drawStar(controlRenderer_, col.x + col.w - 14, sy, 3, sc);
    }
  }

  void renderCueRow(const SDL_Rect& row, int deckIndex, int index) {
    if (row.y + row.h < kPadding || row.y > 2000) {
      return;
    }

    const Deck& deck = project_.decks[deckIndex];
    const auto& cue = deck.cues[index];
    bool isOverlay = std::any_of(deck.overlayActiveIndices.begin(), deck.overlayActiveIndices.end(),
                                  [&](int i) { return i == index; });
    bool isSelected = cueIndexSelected(deck, index);
    bool isLive = index == deck.activeIndex;
    bool isQueued = !isLive && !isSelected && deck.selectedIndex >= 0 && index == deck.selectedIndex + 1;
    SDL_Color fill = colorFromRgba(kScreenLightColor);
    if (isSelected) {
      fill = colorFromRgba(kScreenMidColor);
    } else if (isLive) {
      fill = colorFromRgba(kScreenDarkColor);
    } else if (isOverlay) {
      fill = {48, 80, 48, 255};  // distinct teal-ish tint for active overlay
    }

    Primitives::drawFramedPanel(controlRenderer_, row, fill, colorFromRgba(kScreenDeepColor), colorFromRgba(kShellInnerColor));
    if (project_.uiTransitionsEnabled && isSelected) {
      double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_ - selectionChangedAt_) / 95.0);
      SDL_Color glow {155, 188, 15, static_cast<Uint8>(60 + pulse * 80.0)};
      Primitives::strokeRect(controlRenderer_, insetRect(row, 1), glow);
    }

    SDL_Rect chip {row.x + 12, row.y + 10, 10, row.h - 20};
    SDL_Color chipColor = !cue.colorTag.empty() ? colorTagToSdl(cue.colorTag) : cue.color;
    Primitives::fillRect(controlRenderer_, chip, chipColor);

    SDL_Color ink = isLive ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
    SDL_Color subInk = isLive ? colorFromRgba(kShellOuterColor) : colorFromRgba(kScreenDarkColor);
    std::string indicator = " ";
    if (isLive) {
      bool blinkOn = ((animationNow_ / 280) % 2) == 0;
      indicator = blinkOn ? "\xe2\x96\xa3" : "\xe2\x96\xa1"; // ▣ / □
    } else if (isSelected) {
      indicator = "\xe2\x96\xb8"; // ▸
    } else if (isQueued) {
      indicator = "\xe2\x97\x8f"; // ●
    }
    drawText(controlRenderer_, fontBase_, indicator, ink, row.x + 28, row.y + 13);

    std::string typeIcon = "VID";
    switch (cue.kind) {
      case CueKind::Video:        typeIcon = "VID"; break;
      case CueKind::Image:        typeIcon = "IMG"; break;
      case CueKind::Browser:      typeIcon = "WEB"; break;
      case CueKind::WindowSource: typeIcon = "WIN"; break;
      case CueKind::Camera:       typeIcon = "CAM"; break;
      case CueKind::Syphon:       typeIcon = "SYP"; break;
      case CueKind::Pattern:      typeIcon = "PAT"; break;
      case CueKind::LowerThird:   typeIcon = "L3"; break;
      case CueKind::Audio:        typeIcon = "AUD"; break;
      default: break;
    }
    std::string cueToken = cueDisplayToken(cue, index);
    constexpr int kNumColW = 58;
    constexpr int kTypeColW = 46;
    constexpr int kStateColW = 102;
    int numX = row.x + 48;
    int typeX = numX + kNumColW;
    int nameX = typeX + kTypeColW;
    int stateX = row.x + row.w - kStateColW;
    int nameW = std::max(44, stateX - nameX - 6);

    drawText(controlRenderer_, fontMono_,
             ellipsizeToPixelWidth(fontMono_, cueToken, kNumColW - 8),
             subInk, numX, row.y + 14);
    drawText(controlRenderer_, fontSmall_, typeIcon, subInk, typeX, row.y + 17);

    std::string nameTrimmed = ellipsizeToPixelWidth(fontBase_, cue.name, nameW);
    drawText(controlRenderer_, fontBase_, nameTrimmed, ink, nameX, row.y + 13);

    std::string stateMeta = "--";
    if (cue.kind == CueKind::Video || cue.kind == CueKind::Audio) {
      stateMeta = cue.duration > 0.0 ? formatSeconds(cue.duration) : "hold";
    } else if (cue.kind == CueKind::Image || cue.kind == CueKind::Pattern ||
               cue.kind == CueKind::Browser || isSourceCueKind(cue.kind)) {
      stateMeta = cue.stillDurationSeconds > 0.0 ? formatSeconds(cue.stillDurationSeconds) : "hold";
    }
    if (isLive) {
      stateMeta = "LIVE";
    } else if (isSelected) {
      stateMeta = "SEL";
    } else if (isQueued) {
      stateMeta = "NEXT";
    }
    drawText(controlRenderer_, fontMono_, ellipsizeToPixelWidth(fontMono_, stateMeta, kStateColW - 8),
             subInk, stateX, row.y + 14);

    // End-action glyph (top-right corner of row, small pixel indicator)
    if (cue.endAction != CueEndAction::Inherit) {
      const char* glyph = nullptr;
      switch (cue.endAction) {
        case CueEndAction::Loop:        glyph = "↻"; break;
        case CueEndAction::PauseOnLast: glyph = "‖"; break;
        case CueEndAction::AutoNext:    glyph = "▶▶"; break;
        case CueEndAction::Stop:        glyph = "■"; break;
        default: break;
      }
      if (glyph) {
        drawText(controlRenderer_, fontSmall_, glyph, subInk, row.x + row.w - 28, row.y + 10);
      }
    }

    // Remaining time badge on the active cue row
    if (isLive) {
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      if (engine && engine->duration() > 0.0 && engine->state() == TransportState::Playing) {
        double remaining = std::max(0.0, engine->duration() - engine->position());
        std::string remStr = "-" + formatSeconds(remaining);
        bool urgent = remaining < 10.0;
        SDL_Color remInk = urgent ? colorFromRgba(kScreenLightColor) : colorFromRgba(kShellOuterColor);
        if (urgent) {
          double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_) / 120.0);
          Uint8 alpha = static_cast<Uint8>(180 + pulse * 75.0);
          SDL_Rect badge {row.x + row.w - 96, row.y + 8, 88, row.h - 16};
          SDL_Color badgeFill {15, 56, 15, alpha};
          Primitives::fillRect(controlRenderer_, badge, badgeFill);
        }
        drawText(controlRenderer_, fontMono_, remStr, remInk, row.x + row.w - 90, row.y + 28);
      }
    }

    // Cue row hover tip
    if (pointInRect(mouseX_, mouseY_, row)) {
      std::string rowTip;
      switch (cue.kind) {
        case CueKind::Video:      rowTip = "Enter=take  Space=play/pause  L=loop  E=hold  K=color tag  Right-click=menu"; break;
        case CueKind::Image:      rowTip = "Enter=take  Duration 0 = hold until next Take"; break;
        case CueKind::Pattern:    rowTip = "Enter=take  Test pattern — hold or auto-advance"; break;
        case CueKind::Browser:    rowTip = "Enter=take  Browser cue — renders into output via Xvfb"; break;
        case CueKind::WindowSource: rowTip = "Enter=take  Window source cue (x11 capture)"; break;
        case CueKind::Camera:     rowTip = "Enter=take  Camera source cue (v4l2 capture)"; break;
        case CueKind::Syphon:     rowTip = "Enter=take  Syphon/Spout cue (desktop fallback on Linux)"; break;
        case CueKind::LowerThird: rowTip = "Enter=push overlay  Backspace=pop  LOWERTEXT via Companion"; break;
        default: rowTip = "Enter=take  Delete=remove"; break;
      }
      if (nameTrimmed != cue.name) {
        rowTip = cue.name + "  |  " + rowTip;
      }
      drawHoverTip(rowTip, row.x + row.w / 2, row.y);
    }
  }

  // Draw a small floating tooltip panel anchored below/above (ax, ay).
  void drawHoverTip(const std::string& tip, int ax, int ay) {
    if (tip.empty()) return;
    int w = 0;
    TTF_SizeUTF8(fontSmall_, tip.c_str(), &w, nullptr);
    w += 20;
    int h = 26;
    int x = ax - w / 2;
    int y = ay - h - 6;
    // Keep on screen
    int winW = 0, winH = 0;
    SDL_GetWindowSize(controlWindow_, &winW, &winH);
    x = std::clamp(x, 6, winW - w - 6);
    y = std::max(y, 6);
    SDL_Rect panel {x, y, w, h};
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_Color bg {15, 56, 15, 230};
    Primitives::fillRect(controlRenderer_, panel, bg);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    Primitives::strokeRect(controlRenderer_, panel, colorFromRgba(kScreenDarkColor));
    drawText(controlRenderer_, fontSmall_, tip, colorFromRgba(kScreenLightColor), panel.x + 10, panel.y + 6);
  }

  void renderButtons() {
    for (const auto& button : buttons_) {
      Primitives::fillRect(controlRenderer_, button.rect, button.fill);
      Primitives::strokeRect(controlRenderer_, button.rect, button.outline);
      SDL_Rect topRect {button.rect.x, button.rect.y + 2, button.rect.w, 28};
      drawCenteredText(controlRenderer_, fontBase_, button.label, button.text, topRect);
      std::string keyHint;
      if (button.label == "IMPORT") keyHint = "Shift+I";
      else if (button.label == "SOURCE") keyHint = "Menu";
      else if (button.label == "PATTERN") keyHint = "P";
      else if (button.label == "(A) TAKE") keyHint = "Enter";
      else if (button.label == "(B) STOP") keyHint = "S";
      else if (button.label == "START PLAY") keyHint = "Space";
      else if (button.label == "SELECT CLR") keyHint = "C";
      else if (button.label == "OUTPUT") keyHint = "Prefs";
      if (!keyHint.empty()) {
        SDL_Rect hintRect {button.rect.x, button.rect.y + 30, button.rect.w, 20};
        drawCenteredText(controlRenderer_, fontSmall_, keyHint, colorFromRgba(kScreenDeepColor), hintRect);
      }
    }
    // Hover tip for bottom-bar buttons
    for (const auto& button : buttons_) {
      if (!button.tip.empty() && pointInRect(mouseX_, mouseY_, button.rect)) {
        drawHoverTip(button.tip, button.rect.x + button.rect.w / 2, button.rect.y);
        break;
      }
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
    Primitives::drawFramedPanel(controlRenderer_, panel, toast_.fill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    drawText(controlRenderer_, fontBase_, toast_.message, toast_.ink, panel.x + 14, panel.y + 20);
  }

  // Draws a tiny 4-pointed pixel star centered at (cx, cy), arm half-length S.
  void drawStar(SDL_Renderer* r, int cx, int cy, int S, SDL_Color c) {
    // Center pixel
    SDL_Rect center {cx - 1, cy - 1, 2, 2};
    Primitives::fillRect(r, center, c);
    // Four arms
    for (int i = 1; i <= S; ++i) {
      Uint8 fade = static_cast<Uint8>(c.a * (S - i + 1) / (S + 1));
      SDL_Color arm {c.r, c.g, c.b, fade};
      SDL_Rect h {cx + i, cy - 1, 2, 2}; Primitives::fillRect(r, h, arm);
      SDL_Rect hl{cx - i - 1, cy - 1, 2, 2}; Primitives::fillRect(r, hl, arm);
      SDL_Rect v {cx - 1, cy + i, 2, 2}; Primitives::fillRect(r, v, arm);
      SDL_Rect vt{cx - 1, cy - i - 1, 2, 2}; Primitives::fillRect(r, vt, arm);
    }
  }

  // Character-art rendering has been intentionally removed from operational UI paths.



  void renderMainPanel(const SDL_Rect& panel) {
    const Deck& deck = focusedDeck();
    const MediaEngine* engine = focusedMediaEngine();
    const Cue* selectedCue = selectedCuePtr();
    const Cue* activeCue = activeCuePtr();
    int x = panel.x + 18;
    int y = panel.y + 18;

    quickButtons_.clear();
    cueSettingsQuickButtonStartIndex_ = 0;
    cueSettingsViewportRect_ = SDL_Rect {};

    drawText(controlRenderer_, fontSmall_, "SCREEN / PROGRAM OUTPUT", colorFromRgba(kScreenDeepColor), x, y);
    drawText(controlRenderer_, fontLarge_, activeCue ? activeCue->name : "Insert cartridge", colorFromRgba(kScreenDeepColor), x, y + 22);

    std::string focusedDeckName = deck.name.empty() ? deckDefaultName(project_.focusedDeckIndex) : deck.name;
    std::string deckTcLine = "deck " + std::to_string(project_.focusedDeckIndex + 1) + ": " + focusedDeckName;
    deckTcLine += "   |   tc " + formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps);
    deckTcLine += " @" + std::to_string(static_cast<int>(std::round(deck.timecodeFps)));
    deckTcLine += (deck.timecodeChaseEnabled ? " chase" : " free");
    deckTcLine += (deck.timecodeRunEnabled ? " run" : " hold");
    drawText(controlRenderer_, fontSmall_,
             ellipsizeToPixelWidth(fontSmall_, deckTcLine, std::max(80, panel.w - 52)),
             colorFromRgba(kScreenDeepColor), x, y + 50);

    std::string status = transportStatusLabel();
    std::string clock = formatSeconds(engine ? engine->position() : 0.0) + " / " + formatSeconds(engine ? engine->duration() : 0.0);
    drawText(controlRenderer_, fontBase_, status, colorFromRgba(kScreenDeepColor), x, y + 70);
    drawText(controlRenderer_, fontMono_, clock, colorFromRgba(kScreenDeepColor), x + 150, y + 72);

    // Remaining time countdown
    double engDuration = engine ? engine->duration() : 0.0;
    double engPosition = engine ? engine->position() : 0.0;
    double remaining = engDuration > 0.0 ? std::max(0.0, engDuration - engPosition) : 0.0;
    bool isPlaying = engine && engine->state() == TransportState::Playing;
    if (activeCue && engDuration > 0.0) {
      bool urgent = remaining < 10.0 && isPlaying;
      std::string remStr = "-" + formatSeconds(remaining);
      SDL_Color remColor = colorFromRgba(kScreenDeepColor);
      if (urgent) {
        double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_) / 100.0);
        Uint8 brightness = static_cast<Uint8>(80 + pulse * 175.0);
        remColor = {brightness, static_cast<Uint8>(std::min(255, static_cast<int>(brightness) + 20)), 15, 255};
        SDL_Rect glowPanel {x + panel.w - 180, y + 58, 140, 28};
        SDL_Color glowFill {15, 56, 15, static_cast<Uint8>(40 + pulse * 100.0)};
        Primitives::fillRect(controlRenderer_, glowPanel, glowFill);
        bool tick = (static_cast<int>(remaining) % 2) == 0;
        std::string tickChar = tick ? ">" : "<";
        drawText(controlRenderer_, fontMono_, tickChar, remColor, x + panel.w - 195, y + 62);
        drawText(controlRenderer_, fontMono_, tickChar, remColor, x + panel.w - 50, y + 62);
      }
      drawText(controlRenderer_, fontMono_, remStr, remColor, x + panel.w - 174, y + 62);
    }

    progressBarRect_ = {x, y + 106, panel.w - 52, 26};
    Primitives::drawFramedPanel(controlRenderer_, progressBarRect_, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    double duration = engine ? engine->duration() : 0.0;
    double fraction = duration > 0.0 ? (engine ? engine->position() / duration : 0.0) : 0.0;
    fraction = std::clamp(fraction, 0.0, 1.0);
    SDL_Rect fillBar = insetRect(progressBarRect_, 3);
    fillBar.w = static_cast<int>(std::round(fillBar.w * fraction));
    Primitives::fillRect(controlRenderer_, fillBar, colorFromRgba(kScreenDarkColor));
    if (duration > 0.0) {
      std::string progressClock = formatSeconds(engine ? engine->position() : 0.0)
        + " / " + formatSeconds(duration);
      drawCenteredText(controlRenderer_, fontMono_, progressClock, colorFromRgba(kScreenDeepColor), progressBarRect_);
    }

    // Pause point tick marks on progress bar
    if (activeCue && !activeCue->pausePoints.empty() && duration > 0.0) {
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
      for (double pp : activeCue->pausePoints) {
        double ppFrac = std::clamp(pp / duration, 0.0, 1.0);
        int tickX = progressBarRect_.x + static_cast<int>(progressBarRect_.w * ppFrac);
        SDL_SetRenderDrawColor(controlRenderer_, 220, 120, 30, 200);
        SDL_RenderDrawLine(controlRenderer_, tickX, progressBarRect_.y + 2, tickX, progressBarRect_.y + progressBarRect_.h - 2);
      }
      SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
    }

    // Goto-last buttons: -30 / -20 / -10 (placed below progress bar)
    {
      int btnY = y + 138;
      int btnW = 46;
      int btnX = x;
      for (auto& [label, gotoAction] : std::vector<std::pair<std::string, QuickAction>>{
        {"-30s", QuickAction::GotoMinus30}, {"-20s", QuickAction::GotoMinus20}, {"-10s", QuickAction::GotoMinus10}
      }) {
        SDL_Rect btn {btnX, btnY, btnW, 18};
        Primitives::drawFramedPanel(controlRenderer_, btn, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, label, colorFromRgba(kScreenDeepColor), btn);
        quickButtons_.push_back({btn, gotoAction, label + " from end"});
        btnX += btnW + 4;
      }
    }

    // Middle section: video preview (left) + per-cue controls with thumbnail (right)
    int midY = y + 150;
    constexpr int kDetailAreaH = 120; // always reserve this many px at bottom for cart details
    int midH = panel.h - (midY - panel.y) - kDetailAreaH;
    midH = std::max(320, midH); // floor so settings panel stays usable
    constexpr int kCtrlW = 330;
    int previewW = panel.w - 52 - 12 - kCtrlW;
    constexpr int kStackViewH = 102;
    int previewH = std::max(190, midH - kStackViewH - 6);

    // --- Program monitor / live video preview ---
    bool hasLiveVideo = controlPreviewTex_ && controlPreviewTexW_ > 0 && controlPreviewTexH_ > 0;
    SDL_Color previewBg = hasLiveVideo ? colorFromRgba(kScreenDeepColor) : colorFromRgba(kScreenLightColor);
    SDL_Color previewBorder = hasLiveVideo ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor);
    SDL_Rect preview {x, midY, previewW, previewH};
    Primitives::drawFramedPanel(controlRenderer_, preview, previewBg, colorFromRgba(kScreenDeepColor), previewBorder);
    drawText(controlRenderer_, fontSmall_, "program monitor",
             hasLiveVideo ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenDeepColor),
             preview.x + 10, preview.y + 6);
    {
      int focusedOutputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
      auto [outW, outH] = outputRenderSizeForOutput(focusedOutputIndex);
      std::string outInfo = outputLabel(focusedOutputIndex)
        + "  " + std::to_string(outW) + "x" + std::to_string(outH)
        + "  " + outputRefreshRateLabel();
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, outInfo, std::max(60, preview.w - 180)),
               hasLiveVideo ? colorFromRgba(kScreenMidColor) : colorFromRgba(kScreenDarkColor),
               preview.x + 146, preview.y + 6);
    }

    if (hasLiveVideo) {
      SDL_Rect inner {preview.x + 4, preview.y + 24, preview.w - 8, preview.h - 28};
      // Mirror output geometry (scale/crop/offset/rotation) in the control preview.
      renderTextureWithCueGeometry(
        controlRenderer_,
        controlPreviewTex_,
        controlPreviewTexW_,
        controlPreviewTexH_,
        activeCue,
        inner);
    } else if (!activeCue) {
      drawText(controlRenderer_, fontSmall_, "drop media or press I to import",
               colorFromRgba(kScreenDeepColor), preview.x + 16, preview.y + preview.h - 28);
    } else if (activeCue->kind == CueKind::Audio) {
      // Audio-only cue: show full waveform as main preview
      SDL_Rect inner {preview.x + 4, preview.y + 24, preview.w - 8, preview.h - 48};
      std::vector<float> peaks;
      { std::lock_guard<std::mutex> lk(waveformMutex_);
        auto it = waveformCache_.find(activeCue->path);
        if (it != waveformCache_.end()) peaks = it->second; }
      double dur = activeCue->duration > 0.0 ? activeCue->duration : 1.0;
      float inFrac  = static_cast<float>(activeCue->inPointSeconds / dur);
      float outFrac = activeCue->outPointSeconds > 0.0
                    ? static_cast<float>(activeCue->outPointSeconds / dur) : 1.0f;
      float playFrac = engine ? static_cast<float>(std::clamp(engine->position() / dur, 0.0, 1.0)) : -1.0f;
      drawWaveform(controlRenderer_, inner, peaks, playFrac, inFrac, outFrac,
                   activeCue->pausePoints, dur);
      drawText(controlRenderer_, fontSmall_, activeCue->name,
               colorFromRgba(kScreenLightColor), preview.x + 10, preview.y + preview.h - 36);
      drawText(controlRenderer_, fontSmall_, transportStatusLabel(),
               colorFromRgba(kScreenMidColor), preview.x + 10, preview.y + preview.h - 20);
    } else {
      drawText(controlRenderer_, fontBase_, activeCue->name, colorFromRgba(kScreenDeepColor), preview.x + 16, preview.y + 80);
      drawText(controlRenderer_, fontSmall_, transportStatusLabel(), colorFromRgba(kScreenDeepColor), preview.x + 16, preview.y + 110);
    }

    // VU meter at bottom of program monitor
    {
      float rms = computeVuLevel();
      int vuY = preview.y + preview.h - 14;
      int vuW = preview.w - 8;
      SDL_Rect vuBg {preview.x + 4, vuY, vuW, 10};
      Primitives::drawFramedPanel(controlRenderer_, vuBg, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      int fillW = static_cast<int>(vuW * std::clamp(rms * 4.0f, 0.0f, 1.0f));
      SDL_Color vuColor = rms > 0.7f ? SDL_Color{220, 60, 60, 255}
                        : rms > 0.5f ? SDL_Color{220, 180, 0, 255}
                        : colorFromRgba(kScreenDarkColor);
      if (fillW > 0) {
        SDL_Rect vuFill {vuBg.x + 2, vuBg.y + 2, std::min(fillW, vuW - 4), vuBg.h - 4};
        Primitives::fillRect(controlRenderer_, vuFill, vuColor);
      }
    }

    SDL_Rect stackRect {preview.x, preview.y + preview.h + 6, preview.w, kStackViewH};
    Primitives::drawFramedPanel(controlRenderer_, stackRect, colorFromRgba(kScreenLightColor),
                                colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    int focusedOutputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
    drawText(controlRenderer_, fontSmall_,
             "STACK VIEW (" + outputLabel(focusedOutputIndex) + ")",
             colorFromRgba(kScreenDeepColor), stackRect.x + 8, stackRect.y + 6);
    std::vector<std::pair<int, int>> stackEntries = layeredDeckEntriesForOutput(focusedOutputIndex);
    int stackY = stackRect.y + 26;
    if (stackEntries.empty()) {
      drawText(controlRenderer_, fontSmall_, "no active deck-layer links for this output",
               colorFromRgba(kScreenInkSoftColor), stackRect.x + 10, stackY);
    } else {
      for (auto it = stackEntries.rbegin(); it != stackEntries.rend(); ++it) {
        if (stackY + 14 > stackRect.y + stackRect.h - 6) {
          break;
        }
        int layerIndex = it->first;
        int deckIndex = it->second;
        const Deck& stackDeck = project_.decks[deckIndex];
        const Cue* liveCue = activeCuePtr(deckIndex);
        std::string cueName = liveCue ? liveCue->name : "--";
        std::string layerToken = layerIndex <= 0 ? "BG" : ("L" + std::to_string(layerIndex));
        std::string rowText = layerToken + "  " + deckLabel(deckIndex) + "  ->  " + cueName;
        drawText(controlRenderer_, fontSmall_,
                 ellipsizeToPixelWidth(fontSmall_, rowText, stackRect.w - 16),
                 colorFromRgba(kScreenDeepColor), stackRect.x + 10, stackY);
        stackY += 16;
      }
    }

    // --- Per-cue settings panel (with thumbnail at top) ---
    int ctrlX = x + previewW + 12;
    SDL_Rect ctrl {ctrlX, midY, kCtrlW, midH};
    Primitives::drawFramedPanel(controlRenderer_, ctrl, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));

    // Thumbnail of selected cue (top portion)
    constexpr int kThumbAreaH = 110;
    SDL_Rect thumbArea {ctrl.x + 4, ctrl.y + 4, kCtrlW - 8, kThumbAreaH};
    Primitives::drawFramedPanel(controlRenderer_, thumbArea, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenDarkColor));
    if (selectedCue && selectedCue->kind == CueKind::Audio) {
      // Audio cue: fill entire thumb area with waveform
      std::vector<float> peaks;
      bool pending = false;
      { std::lock_guard<std::mutex> lk(waveformMutex_);
        auto it = waveformCache_.find(selectedCue->path);
        if (it != waveformCache_.end()) peaks = it->second;
        else pending = waveformFutures_.count(selectedCue->path) > 0; }
      double dur = selectedCue->duration > 0.0 ? selectedCue->duration : 1.0;
      float inFrac  = static_cast<float>(selectedCue->inPointSeconds / dur);
      float outFrac = selectedCue->outPointSeconds > 0.0
                    ? static_cast<float>(selectedCue->outPointSeconds / dur) : 1.0f;
      float playFrac = -1.0f;
      if (const MediaEngine* eng = focusedMediaEngine())
        playFrac = static_cast<float>(std::clamp(eng->position() / dur, 0.0, 1.0));
      drawWaveform(controlRenderer_, thumbArea, peaks, playFrac, inFrac, outFrac,
                   selectedCue->pausePoints, dur);
      drawText(controlRenderer_, fontSmall_, selectedCue->name,
               colorFromRgba(kScreenMidColor), thumbArea.x + 6, thumbArea.y + 4);
    } else if (selectedThumbnailTex_) {
      float aspect = static_cast<float>(selectedThumbnailTexW_) / static_cast<float>(selectedThumbnailTexH_);
      int drawW = thumbArea.w - 4;
      int drawH = static_cast<int>(drawW / aspect);
      if (drawH > thumbArea.h - 4) {
        drawH = thumbArea.h - 4;
        drawW = static_cast<int>(drawH * aspect);
      }
      SDL_Rect dst {thumbArea.x + (thumbArea.w - drawW) / 2, thumbArea.y + (thumbArea.h - drawH) / 2, drawW, drawH};
      SDL_SetTextureBlendMode(selectedThumbnailTex_, SDL_BLENDMODE_NONE);
      SDL_RenderCopy(controlRenderer_, selectedThumbnailTex_, nullptr, &dst);
    } else if (selectedCue) {
      drawText(controlRenderer_, fontSmall_, selectedCue->name,
               colorFromRgba(kScreenDarkColor), thumbArea.x + 6, thumbArea.y + 8);
      drawText(controlRenderer_, fontSmall_, "loading preview...",
               colorFromRgba(kScreenDarkColor), thumbArea.x + 6, thumbArea.y + 28);
    } else {
      drawText(controlRenderer_, fontSmall_, "Insert cartridge",
               colorFromRgba(kScreenDarkColor), thumbArea.x + 8, thumbArea.y + thumbArea.h - 44);
      drawText(controlRenderer_, fontSmall_, "Drop media here",
               colorFromRgba(kScreenDarkColor), thumbArea.x + 8, thumbArea.y + thumbArea.h - 30);
      drawText(controlRenderer_, fontSmall_, "Press A to take cue",
               colorFromRgba(kScreenDarkColor), thumbArea.x + 8, thumbArea.y + thumbArea.h - 16);
    }

    // Waveform strip at bottom of thumb area (for video cues with audio — Audio cues get full thumb above)
    if (selectedCue && selectedCue->hasAudio && selectedCue->kind != CueKind::Audio) {
      std::vector<float> peaks;
      bool pending = false;
      {
        std::lock_guard<std::mutex> lk(waveformMutex_);
        auto it = waveformCache_.find(selectedCue->path);
        if (it != waveformCache_.end()) peaks = it->second;
        else pending = waveformFutures_.count(selectedCue->path) > 0;
      }
      SDL_Rect waveRect {thumbArea.x + 2, thumbArea.y + thumbArea.h - 34, thumbArea.w - 4, 32};
      double dur = selectedCue->duration > 0.0 ? selectedCue->duration : 1.0;
      float inFrac  = static_cast<float>(selectedCue->inPointSeconds / dur);
      float outFrac = selectedCue->outPointSeconds > 0.0
                    ? static_cast<float>(selectedCue->outPointSeconds / dur) : 1.0f;
      float playFrac = -1.0f;
      if (const MediaEngine* eng = focusedMediaEngine())
        playFrac = static_cast<float>(eng->position() / dur);
      if (!peaks.empty() || pending)
        drawWaveform(controlRenderer_, waveRect, peaks, playFrac, inFrac, outFrac,
                     selectedCue->pausePoints, dur);
    }

    // Label "cue panel" below thumb
    int ctrlSettingsY = ctrl.y + kThumbAreaH + 10;
    drawText(controlRenderer_, fontSmall_, "CUE PANEL", colorFromRgba(kScreenDeepColor), ctrl.x + 10, ctrlSettingsY);

    auto drawQuickRow = [&](int rowY, const std::string& label, QuickAction decAction, const std::string& value,
                            QuickAction incAction, QuickAction toggleAction = QuickAction::ToggleLoop,
                            bool isToggle = false, bool toggleOn = false, std::string tip = "",
                            bool valueEditable = false, QuickAction valueAction = QuickAction::ToggleLoop) {
      constexpr int kLabelW = 64;
      constexpr int kBtnW = 32;
      constexpr int kValW = 98;
      constexpr int kRowH = 30;
      int rx = ctrl.x + 10;

      if (isToggle) {
        SDL_Rect btn {rx, rowY, kCtrlW - 20, kRowH};
        SDL_Color fill = toggleOn ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
        SDL_Color ink  = toggleOn ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
        Primitives::drawFramedPanel(controlRenderer_, btn, fill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, label + ": " + value, ink, btn.x + 10, btn.y + 8);
        quickButtons_.push_back({btn, toggleAction, tip});
      } else {
        drawText(controlRenderer_, fontSmall_, label, colorFromRgba(kScreenDeepColor), rx, rowY + 8);
        SDL_Rect decBtn {rx + kLabelW, rowY, kBtnW, kRowH};
        Primitives::drawFramedPanel(controlRenderer_, decBtn, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "-", colorFromRgba(kScreenDeepColor), decBtn);
        quickButtons_.push_back({decBtn, decAction, tip});
        SDL_Rect valRect {rx + kLabelW + kBtnW + 4, rowY, kValW, kRowH};
        Primitives::drawFramedPanel(controlRenderer_, valRect, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
        drawCenteredText(controlRenderer_, fontSmall_, value, colorFromRgba(kScreenDeepColor), valRect);
        if (valueEditable) {
          std::string valueTip = tip.empty() ? "Click value to type an exact number" : tip + " | click value to type exact value";
          quickButtons_.push_back({valRect, valueAction, valueTip});
        }
        SDL_Rect incBtn {rx + kLabelW + kBtnW + 4 + kValW + 4, rowY, kBtnW, kRowH};
        Primitives::drawFramedPanel(controlRenderer_, incBtn, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "+", colorFromRgba(kScreenDeepColor), incBtn);
        quickButtons_.push_back({incBtn, incAction, tip});
      }
    };

    int settingsContentTopY = ctrlSettingsY + 18;
    int settingsContentBottomY = ctrl.y + ctrl.h - 10;
    cueSettingsViewportRect_ = {
      ctrl.x + 6,
      settingsContentTopY - 2,
      kCtrlW - 12,
      std::max(0, settingsContentBottomY - settingsContentTopY + 2)
    };
    cueSettingsScroll_ = std::clamp(cueSettingsScroll_, 0, cueSettingsScrollMax_);
    cueSettingsQuickButtonStartIndex_ = quickButtons_.size();
    SDL_RenderSetClipRect(controlRenderer_,
      cueSettingsViewportRect_.h > 0 ? &cueSettingsViewportRect_ : nullptr);

    auto formatFloat = [](float value, int decimals = 2) {
      std::ostringstream ss;
      ss << std::fixed << std::setprecision(decimals) << value;
      return ss.str();
    };
    auto formatPercent = [&](float value) {
      return formatFloat(value * 100.0f, 1) + "%";
    };
    auto formatScaleMode = [](ScaleMode mode) {
      switch (mode) {
        case ScaleMode::Fit: return "Fit";
        case ScaleMode::Fill: return "Fill";
        case ScaleMode::Stretch: return "Stretch";
        case ScaleMode::Unscaled: return "Unscaled";
      }
      return "?";
    };

    auto drawKeyColorRow = [&](int rowY, const Cue& cue) {
      SDL_Rect colorBtn {ctrl.x + 10, rowY, kCtrlW - 20, 30};
      SDL_Color fill = cue.chromaKeyEnabled ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
      SDL_Color ink = cue.chromaKeyEnabled ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
      Primitives::drawFramedPanel(controlRenderer_, colorBtn, fill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(controlRenderer_, fontSmall_, "key color: " + colorToHex(cue.chromaKeyColor), ink, colorBtn.x + 10, colorBtn.y + 8);
      quickButtons_.push_back({colorBtn, QuickAction::EditKeyColor, "Click to set chroma-key color"});
    };

    auto drawGeometryRows = [&](int startY, const Cue& cue, bool includeScaleOffset) {
      constexpr int kRowStep = 28;
      int rowY = startY;
      if (includeScaleOffset) {
        drawQuickRow(rowY, "mode", QuickAction::CycleScaleMode, formatScaleMode(cue.scaleMode), QuickAction::CycleScaleMode,
                     QuickAction::ToggleLoop, false, false, "Fit/Fill/Stretch/Unscaled");
        rowY += kRowStep;
        drawQuickRow(rowY, "scale X", QuickAction::ScaleXDec, formatFloat(cue.outputScaleX, 2) + "x", QuickAction::ScaleXInc,
                     QuickAction::ToggleLoop, false, false, "Output X scale (0.25–4.0×)", true, QuickAction::EditScaleX);
        rowY += kRowStep;
        drawQuickRow(rowY, "scale Y", QuickAction::ScaleYDec, formatFloat(cue.outputScaleY, 2) + "x", QuickAction::ScaleYInc,
                     QuickAction::ToggleLoop, false, false, "Output Y scale (0.25–4.0×)", true, QuickAction::EditScaleY);
        rowY += kRowStep;
        drawQuickRow(rowY, "off X", QuickAction::OffsetXDec, std::to_string(static_cast<int>(cue.outputOffsetX)) + "px", QuickAction::OffsetXInc,
                     QuickAction::ToggleLoop, false, false, "Horizontal output offset in pixels", true, QuickAction::EditOffsetX);
        rowY += kRowStep;
        drawQuickRow(rowY, "off Y", QuickAction::OffsetYDec, std::to_string(static_cast<int>(cue.outputOffsetY)) + "px", QuickAction::OffsetYInc,
                     QuickAction::ToggleLoop, false, false, "Vertical output offset in pixels", true, QuickAction::EditOffsetY);
        rowY += kRowStep;
      }
      drawQuickRow(rowY, "rot", QuickAction::RotDec, formatFloat(cue.outputRotationDegrees, 1) + " deg", QuickAction::RotInc,
                   QuickAction::ToggleLoop, false, false, "Output rotation angle (-180..180)", true, QuickAction::EditRotation);
      rowY += kRowStep;
      drawQuickRow(rowY, "crop L", QuickAction::CropLDec, formatPercent(cue.cropLeft), QuickAction::CropLInc,
                   QuickAction::ToggleLoop, false, false, "Crop from left");
      rowY += kRowStep;
      drawQuickRow(rowY, "crop R", QuickAction::CropRDec, formatPercent(cue.cropRight), QuickAction::CropRInc,
                   QuickAction::ToggleLoop, false, false, "Crop from right");
      rowY += kRowStep;
      drawQuickRow(rowY, "crop T", QuickAction::CropTDec, formatPercent(cue.cropTop), QuickAction::CropTInc,
                   QuickAction::ToggleLoop, false, false, "Crop from top");
      rowY += kRowStep;
      drawQuickRow(rowY, "crop B", QuickAction::CropBDec, formatPercent(cue.cropBottom), QuickAction::CropBInc,
                   QuickAction::ToggleLoop, false, false, "Crop from bottom");
      rowY += kRowStep;
      return rowY;
    };

    auto drawColorRows = [&](int startY, const Cue& cue) {
      constexpr int kRowStep = 28;
      int rowY = startY;
      if (!cueSupportsColorControls(&cue)) {
        return rowY;
      }
      drawQuickRow(rowY, "bright", QuickAction::BrightnessDec, formatFloat(cue.brightness, 2) + "x", QuickAction::BrightnessInc,
                   QuickAction::ToggleLoop, false, false, "Brightness multiplier (0.0–2.0×)");
      rowY += kRowStep;
      drawQuickRow(rowY, "contrast", QuickAction::ContrastDec, formatFloat(cue.contrast, 2) + "x", QuickAction::ContrastInc,
                   QuickAction::ToggleLoop, false, false, "Contrast multiplier (0.0–2.0×)");
      rowY += kRowStep;
      drawQuickRow(rowY, "sat", QuickAction::SaturationDec, formatFloat(cue.saturation, 2) + "x", QuickAction::SaturationInc,
                   QuickAction::ToggleLoop, false, false, "Saturation multiplier (0.0–2.0×)");
      rowY += kRowStep;
      drawQuickRow(rowY, "hue", QuickAction::HueShiftDec, formatFloat(cue.hueShift, 0) + " deg", QuickAction::HueShiftInc,
                   QuickAction::ToggleLoop, false, false, "Hue rotation (-180 to +180 degrees)");
      rowY += kRowStep;
      return rowY;
    };

    auto drawKeyRows = [&](int startY, const Cue& cue) {
      constexpr int kRowStep = 28;
      int rowY = startY;
      if (!cueSupportsKeying(&cue)) {
        return rowY;
      }
      drawQuickRow(rowY, "key", QuickAction::KeyToggle,
                   cue.chromaKeyEnabled ? "on" : "off",
                   QuickAction::KeyToggle, QuickAction::KeyToggle, true, cue.chromaKeyEnabled,
                   "Toggle chroma key");
      rowY += kRowStep;
      drawKeyColorRow(rowY, cue);
      rowY += kRowStep;
      drawQuickRow(rowY, "tol", QuickAction::KeyTolDec, formatFloat(cue.chromaKeyTolerance, 1), QuickAction::KeyTolInc,
                   QuickAction::ToggleLoop, false, false, "Key tolerance (RGB distance)");
      rowY += kRowStep;
      drawQuickRow(rowY, "soft", QuickAction::KeySoftDec, formatFloat(cue.chromaKeySoftness, 1), QuickAction::KeySoftInc,
                   QuickAction::ToggleLoop, false, false, "Key edge softness");
      rowY += kRowStep;
      return rowY;
    };

    auto drawSectionHeader = [&](int rowY, const std::string& title, bool open,
                                 QuickAction toggleAction, const std::string& tip) {
      SDL_Rect hdr {ctrl.x + 10, rowY, kCtrlW - 20, 24};
      SDL_Color fill = open ? colorFromRgba(kScreenMidColor) : colorFromRgba(kScreenLightColor);
      SDL_Color ink = open ? colorFromRgba(kScreenDeepColor) : colorFromRgba(kScreenDarkColor);
      Primitives::drawFramedPanel(controlRenderer_, hdr, fill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenDarkColor));
      drawText(controlRenderer_, fontSmall_, std::string(open ? "[-] " : "[+] ") + title, ink, hdr.x + 8, hdr.y + 5);
      quickButtons_.push_back({hdr, toggleAction, tip});
      return rowY + 28;
    };

    auto drawCueRoutingRows = [&](int startY) {
      constexpr int kRowStep = 28;
      if (project_.decks.empty() || project_.outputs.empty()) {
        return startY;
      }
      int deckIndex = std::clamp(project_.focusedDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
      auto primaryOut = primaryOutputIndexForDeck(deckIndex);
      int outputCount = static_cast<int>(project_.outputs.size());
      int routeOutput = primaryOut
        ? *primaryOut
        : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
      auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
      bool assigned = assignmentIndex.has_value();
      int layerIndex = assigned ? std::clamp(project_.layerAssignments[*assignmentIndex].layerIndex, 0, 255) : 0;

      drawQuickRow(startY, "output", QuickAction::CueRouteOutputPrev,
                   outputLabel(routeOutput), QuickAction::CueRouteOutputNext,
                   QuickAction::CueSectionRoutingToggle, false, false,
                   "Route this deck to previous/next output");
      drawQuickRow(startY + kRowStep, "layer", QuickAction::CueRouteLayerDec,
                   assigned ? (layerIndex <= 0 ? "BG" : ("L" + std::to_string(layerIndex))) : "--",
                   QuickAction::CueRouteLayerInc,
                   QuickAction::CueSectionRoutingToggle, false, false,
                   "Adjust assignment layer");
      SDL_Rect assignBtn {ctrl.x + 10, startY + kRowStep * 2, kCtrlW - 20, 28};
      SDL_Color assignFill = assigned ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
      SDL_Color assignInk = assigned ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
      Primitives::drawFramedPanel(controlRenderer_, assignBtn, assignFill,
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawCenteredText(controlRenderer_, fontSmall_, assigned ? "UNLINK" : "LINK", assignInk, assignBtn);
      quickButtons_.push_back({assignBtn, QuickAction::CueRouteAssignToggle, "Assign/Unassign deck routing"});
      return startY + kRowStep * 3;
    };

    std::vector<int> panelSelectedIndices = selectedCue ? selectedCueIndices(deck) : std::vector<int> {};
    std::vector<const Cue*> panelSelectedCues;
    panelSelectedCues.reserve(panelSelectedIndices.size());
    for (int index : panelSelectedIndices) {
      if (index >= 0 && index < static_cast<int>(deck.cues.size())) {
        panelSelectedCues.push_back(&deck.cues[index]);
      }
    }
    bool panelMultiSelection = panelSelectedCues.size() > 1;

    auto allSelectedCues = [&](auto pred) {
      if (panelSelectedCues.empty()) {
        return false;
      }
      for (const Cue* cue : panelSelectedCues) {
        if (!pred(*cue)) {
          return false;
        }
      }
      return true;
    };

    auto boolMixedState = [&](auto getter) {
      bool first = getter(*panelSelectedCues.front());
      bool mixed = false;
      for (const Cue* cue : panelSelectedCues) {
        if (getter(*cue) != first) {
          mixed = true;
          break;
        }
      }
      return std::pair<bool, bool> {mixed, first};
    };

    auto doubleMixedLabel = [&](auto getter, int decimals, const std::string& suffix = std::string()) {
      double first = getter(*panelSelectedCues.front());
      bool mixed = false;
      for (const Cue* cue : panelSelectedCues) {
        if (std::abs(getter(*cue) - first) > 0.0001) {
          mixed = true;
          break;
        }
      }
      if (mixed) {
        return std::string("mixed");
      }
      std::ostringstream ss;
      ss << std::fixed << std::setprecision(decimals) << first;
      return ss.str() + suffix;
    };

    auto intMixedLabel = [&](auto getter, const std::string& suffix = std::string()) {
      int first = getter(*panelSelectedCues.front());
      bool mixed = false;
      for (const Cue* cue : panelSelectedCues) {
        if (getter(*cue) != first) {
          mixed = true;
          break;
        }
      }
      if (mixed) {
        return std::string("mixed");
      }
      return std::to_string(first) + suffix;
    };

    auto stringMixedLabel = [&](auto getter, const std::string& emptyToken = std::string("none")) {
      std::string first = getter(*panelSelectedCues.front());
      bool mixed = false;
      for (const Cue* cue : panelSelectedCues) {
        if (getter(*cue) != first) {
          mixed = true;
          break;
        }
      }
      if (mixed) {
        return std::string("mixed");
      }
      return first.empty() ? emptyToken : first;
    };

    if (panelMultiSelection) {
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      constexpr int kRowStep = 28;

      std::vector<std::string> kindLabels;
      for (const Cue* cue : panelSelectedCues) {
        std::string label = cueKindLabel(cue->kind);
        if (std::find(kindLabels.begin(), kindLabels.end(), label) == kindLabels.end()) {
          kindLabels.push_back(label);
        }
      }
      std::string kindSummary;
      for (size_t i = 0; i < kindLabels.size(); ++i) {
        if (i > 0) {
          kindSummary += ", ";
        }
        kindSummary += kindLabels[i];
      }

      bool allVideoAudio = allSelectedCues([&](const Cue& cue) {
        return cue.kind == CueKind::Video || cue.kind == CueKind::Audio;
      });
      bool allStillLike = allSelectedCues([&](const Cue& cue) {
        return cue.kind != CueKind::Video && cue.kind != CueKind::Audio;
      });
      bool allSupportsGeometry = allSelectedCues([&](const Cue& cue) {
        return cueSupportsGeometry(&cue);
      });
      bool allSupportsKey = allSelectedCues([&](const Cue& cue) {
        return cueSupportsKeying(&cue);
      });
      bool allHasAudio = allSelectedCues([&](const Cue& cue) {
        return cue.hasAudio;
      });
      bool allLowerThird = allSelectedCues([&](const Cue& cue) {
        return cue.kind == CueKind::LowerThird;
      });

      int playbackBodyY = drawSectionHeader(ry - 14, "PLAYBACK", cueSectionPlaybackOpen_,
                                            QuickAction::CueSectionPlaybackToggle,
                                            "Common controls for selected cues");
      if (cueSectionPlaybackOpen_) {
        ry = playbackBodyY;
        drawText(controlRenderer_, fontSmall_,
                 std::to_string(panelSelectedCues.size()) + " cues: " + kindSummary,
                 colorFromRgba(kScreenInkSoftColor), ctrl.x + 10, ry + 4);
        ry += kRowStep;

        if (allVideoAudio) {
          drawQuickRow(ry, "fade in", QuickAction::FadeInDec,
                       stringMixedLabel([&](const Cue& cue) { return formatSeconds(cue.fadeInSeconds); }),
                       QuickAction::FadeInInc, QuickAction::ToggleLoop, false, false,
                       "Apply fade-in to selected video/audio cues");
          ry += kRowStep;
          drawQuickRow(ry, "fade out", QuickAction::FadeOutDec,
                       stringMixedLabel([&](const Cue& cue) { return formatSeconds(cue.fadeOutSeconds); }),
                       QuickAction::FadeOutInc, QuickAction::ToggleLoop, false, false,
                       "Apply fade-out to selected video/audio cues");
          ry += kRowStep;

          auto loopState = boolMixedState([&](const Cue& cue) { return cue.loop; });
          drawQuickRow(ry, "loop", QuickAction::ToggleLoop,
                       loopState.first ? "mixed" : (loopState.second ? "on" : "off"),
                       QuickAction::ToggleLoop, QuickAction::ToggleLoop, true, !loopState.first && loopState.second,
                       "Toggle loop for selected cues");
          ry += kRowStep;

          auto holdState = boolMixedState([&](const Cue& cue) { return cue.pauseOnLastFrame; });
          drawQuickRow(ry, "hold", QuickAction::ToggleHold,
                       holdState.first ? "mixed" : (holdState.second ? "on" : "off"),
                       QuickAction::ToggleHold, QuickAction::ToggleHold, true, !holdState.first && holdState.second,
                       "Toggle hold-at-end for selected cues");
          ry += kRowStep;

          bool mixedEnd = false;
          CueEndAction endAction = panelSelectedCues.front()->endAction;
          for (const Cue* cue : panelSelectedCues) {
            if (cue->endAction != endAction) {
              mixedEnd = true;
              break;
            }
          }
          SDL_Rect endBtn {ctrl.x + 10, ry, kCtrlW - 20, 30};
          Primitives::drawFramedPanel(controlRenderer_, endBtn, colorFromRgba(kScreenLightColor),
                                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
          drawText(controlRenderer_, fontSmall_, "end: " + std::string(mixedEnd ? "mixed" : cueEndActionLabel(endAction)) + "  [X cycle]",
                   colorFromRgba(kScreenDeepColor), endBtn.x + 10, endBtn.y + 8);
          quickButtons_.push_back({endBtn, QuickAction::CycleEndAction, "Cycle end action for selected cues"});
          ry += kRowStep;

          drawQuickRow(ry, "repeats", QuickAction::LoopCountDec,
                       intMixedLabel([&](const Cue& cue) { return cue.loopCount; }, "x"),
                       QuickAction::LoopCountInc, QuickAction::ToggleLoop, false, false,
                       "Set repeat count for selected cues");
          ry += kRowStep;

          drawQuickRow(ry, "speed", QuickAction::SpeedDec,
                       doubleMixedLabel([&](const Cue& cue) { return cue.playbackSpeed; }, 2, "x"),
                       QuickAction::SpeedInc, QuickAction::ToggleLoop, false, false,
                       "Set playback speed for selected cues");
          ry += kRowStep;
        }

        if (allStillLike) {
          drawQuickRow(ry, "duration", QuickAction::DurDec,
                       stringMixedLabel([&](const Cue& cue) {
                         return cue.stillDurationSeconds > 0.0 ? formatSeconds(cue.stillDurationSeconds) : std::string("hold");
                       }),
                       QuickAction::DurInc, QuickAction::ToggleLoop, false, false,
                       "Set still/pattern/browser duration for selected cues");
          ry += kRowStep;
        }

        drawQuickRow(ry, "trans", QuickAction::TransDec,
                     stringMixedLabel([&](const Cue& cue) {
                       return cue.cueTransitionSeconds >= 0.0 ? formatSeconds(cue.cueTransitionSeconds) : std::string("deck");
                     }),
                     QuickAction::TransInc, QuickAction::ToggleLoop, false, false,
                     "Set per-cue transition duration");
        {
          constexpr int kLabelW = 64, kBtnW = 32, kValW = 98;
          int rx = ctrl.x + 10;
          int styleX = rx + kLabelW + kBtnW + 4 + kValW + 4 + kBtnW + 4;
          int styleW = (ctrl.x + kCtrlW - 10) - styleX;
          SDL_Rect styleBtn {styleX, ry, styleW, 30};
          std::string styleLabel = stringMixedLabel([&](const Cue& cue) {
            return cue.cueTransitionStyle.empty() ? focusedDeck().transitionStyle : cue.cueTransitionStyle;
          }, "deck");
          if (styleLabel == "crossfade") styleLabel = "xfade";
          SDL_Color styleFill = colorFromRgba(kScreenLightColor);
          SDL_Color styleInk = colorFromRgba(kScreenDeepColor);
          Primitives::drawFramedPanel(controlRenderer_, styleBtn, styleFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
          drawCenteredText(controlRenderer_, fontSmall_, styleLabel, styleInk, styleBtn);
          quickButtons_.push_back({styleBtn, QuickAction::CycleTransStyle, "Cycle transition style for selected cues"});
        }
        ry += kRowStep;

        auto pauseBeginState = boolMixedState([&](const Cue& cue) { return cue.pauseAtBeginning; });
        drawQuickRow(ry, "pause in", QuickAction::TogglePauseBegin,
                     pauseBeginState.first ? "mixed" : (pauseBeginState.second ? "on" : "off"),
                     QuickAction::TogglePauseBegin, QuickAction::TogglePauseBegin, true,
                     !pauseBeginState.first && pauseBeginState.second,
                     "Toggle pause-at-beginning for selected cues");
        ry += kRowStep;

        if (allHasAudio) {
          auto audioState = boolMixedState([&](const Cue& cue) { return cue.audioEnabled; });
          drawQuickRow(ry, "audio", QuickAction::ToggleCueAudio,
                       audioState.first ? "mixed" : (audioState.second ? "on" : "off"),
                       QuickAction::ToggleCueAudio, QuickAction::ToggleCueAudio, true,
                       !audioState.first && audioState.second,
                       "Toggle audio enable for selected cues");
          ry += kRowStep;
        }

        auto nextTransState = boolMixedState([&](const Cue& cue) { return cue.transitionToNext; });
        drawQuickRow(ry, "next xfade", QuickAction::ToggleNextTransition,
                     nextTransState.first ? "mixed" : (nextTransState.second ? "on" : "off"),
                     QuickAction::ToggleNextTransition, QuickAction::ToggleNextTransition, true,
                     !nextTransState.first && nextTransState.second,
                     "Toggle transition-to-next for selected cues");
        ry += kRowStep;

        SDL_Rect gotoBox {ctrl.x + 10, ry, kCtrlW - 80, 26};
        SDL_Rect gotoEdit {ctrl.x + kCtrlW - 64, ry, 54, 26};
        std::string gotoDisplay = stringMixedLabel([&](const Cue& cue) { return cue.gotoTarget; }, "(next)");
        if (gotoDisplay.size() > 28) gotoDisplay = gotoDisplay.substr(0, 25) + "...";
        Primitives::drawFramedPanel(controlRenderer_, gotoBox, colorFromRgba(kScreenLightColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, gotoDisplay, colorFromRgba(kScreenDeepColor), gotoBox.x + 6, gotoBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, gotoEdit, colorFromRgba(kScreenDarkColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "goto", colorFromRgba(kScreenLightColor), gotoEdit);
        quickButtons_.push_back({gotoEdit, QuickAction::EditGotoTarget, "Set goto target for selected cues"});
        ry += kRowStep;

        if (allLowerThird) {
          drawQuickRow(ry, "bg alpha", QuickAction::LowerBgDec,
                       intMixedLabel([&](const Cue& cue) { return cue.lowerThirdBgAlpha; }),
                       QuickAction::LowerBgInc, QuickAction::ToggleLoop, false, false,
                       "Set lower-third background alpha");
          ry += kRowStep;
        }

        std::string tagStr = stringMixedLabel([&](const Cue& cue) { return cue.colorTag; }, "none");
        SDL_Rect tagBtn {ctrl.x + 10, ry, kCtrlW - 20, 28};
        SDL_Color tagFill = colorTagToSdl(tagStr == "mixed" ? std::string() : tagStr, 200);
        Primitives::drawFramedPanel(controlRenderer_, tagBtn, tagFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "tag: " + tagStr + "  [K cycle]",
                         colorFromRgba(kScreenLightColor), tagBtn);
        quickButtons_.push_back({tagBtn, QuickAction::CycleColorTag, "Cycle color tag for selected cues"});
        ry += kRowStep;

        SDL_Rect notesBox {ctrl.x + 10, ry, kCtrlW - 80, 26};
        SDL_Rect notesEdit {ctrl.x + kCtrlW - 64, ry, 54, 26};
        std::string notesDisplay = stringMixedLabel([&](const Cue& cue) { return cue.notes; }, "(no notes)");
        if (notesDisplay.size() > 28) notesDisplay = notesDisplay.substr(0, 25) + "...";
        Primitives::drawFramedPanel(controlRenderer_, notesBox, colorFromRgba(kScreenLightColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, notesDisplay,
                 colorFromRgba(notesDisplay == "(no notes)" ? kScreenInkSoftColor : kScreenDeepColor),
                 notesBox.x + 6, notesBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, notesEdit, colorFromRgba(kScreenDarkColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), notesEdit);
        quickButtons_.push_back({notesEdit, QuickAction::EditNotes, "Edit notes for selected cues"});
        ry += kRowStep;

        SDL_Rect cueIdBox {ctrl.x + 10, ry, kCtrlW - 80, 26};
        SDL_Rect cueIdEdit {ctrl.x + kCtrlW - 64, ry, 54, 26};
        std::string cueIdDisplay = stringMixedLabel([&](const Cue& cue) { return cue.cueId; }, "(none)");
        Primitives::drawFramedPanel(controlRenderer_, cueIdBox, colorFromRgba(kScreenLightColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, cueIdDisplay, colorFromRgba(kScreenDeepColor), cueIdBox.x + 6, cueIdBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, cueIdEdit, colorFromRgba(kScreenDarkColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), cueIdEdit);
        quickButtons_.push_back({cueIdEdit, QuickAction::EditCueNumber, "Set cue ID for selected cues"});
        ry += kRowStep;
      }

      ry = drawSectionHeader(ry, "GEOMETRY", cueSectionGeometryOpen_,
                             QuickAction::CueSectionGeometryToggle,
                             "Common geometry controls");
      if (cueSectionGeometryOpen_) {
        if (allSupportsGeometry) {
          ry = drawGeometryRows(ry, *selectedCue, true);
          ry = drawColorRows(ry, *selectedCue);
        } else {
          drawText(controlRenderer_, fontSmall_, "mixed selection: geometry unavailable",
                   colorFromRgba(kScreenInkSoftColor), ctrl.x + 10, ry + 4);
          ry += kRowStep;
        }
      }

      ry = drawSectionHeader(ry, "KEY", cueSectionKeyOpen_,
                             QuickAction::CueSectionKeyToggle,
                             "Common key controls");
      if (cueSectionKeyOpen_) {
        if (allSupportsKey) {
          ry = drawKeyRows(ry, *selectedCue);
        } else {
          drawText(controlRenderer_, fontSmall_, "mixed selection: key unavailable",
                   colorFromRgba(kScreenInkSoftColor), ctrl.x + 10, ry + 4);
          ry += kRowStep;
        }
      }

      ry = drawSectionHeader(ry, "ROUTING", cueSectionRoutingOpen_,
                             QuickAction::CueSectionRoutingToggle,
                             "Deck -> Output -> Layer route controls");
      if (cueSectionRoutingOpen_) {
        drawCueRoutingRows(ry);
      }
    } else if (selectedCue && selectedCue->kind == CueKind::Video) {
      int volPct = static_cast<int>(std::round((engine ? engine->volume() : 1.0f) * 100.0f));
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      constexpr int kRowStep = 28;
      int playbackBodyY = drawSectionHeader(ry - 14, "PLAYBACK", cueSectionPlaybackOpen_,
                                            QuickAction::CueSectionPlaybackToggle,
                                            "Collapse/expand playback settings");
      int playbackRowsUsed = 8;
      if (cueSectionPlaybackOpen_) {
        ry = playbackBodyY;
      drawQuickRow(ry,                "vol",      QuickAction::VolDec,     std::to_string(volPct) + "%",               QuickAction::VolInc,    QuickAction::ToggleLoop, false, false, "Volume: +/- keys or click to adjust");
      drawQuickRow(ry + kRowStep,     "fade in",  QuickAction::FadeInDec,  formatSeconds(selectedCue->fadeInSeconds),  QuickAction::FadeInInc, QuickAction::ToggleLoop, false, false, "[ / ] keys — fade-in duration");
      drawQuickRow(ry + kRowStep * 2, "fade out", QuickAction::FadeOutDec, formatSeconds(selectedCue->fadeOutSeconds), QuickAction::FadeOutInc,QuickAction::ToggleLoop, false, false, "Shift+[ / ] — fade-out duration");
      drawQuickRow(ry + kRowStep * 3, "in",       QuickAction::InDec,      formatSeconds(selectedCue->inPointSeconds), QuickAction::InInc,     QuickAction::ToggleLoop, false, false, "In-point: cue starts playback here");
      {
        double outVal = selectedCue->outPointSeconds > 0.0 ? selectedCue->outPointSeconds : selectedCue->duration;
        drawQuickRow(ry + kRowStep * 4, "out",    QuickAction::OutDec,     formatSeconds(outVal),                      QuickAction::OutInc,    QuickAction::ToggleLoop, false, false, "Out-point: cue stops playback here");
      }
      {
        // Per-cue transition: duration [-][+] on the left, style cycle button on the right
        bool hasCueTrans = selectedCue->cueTransitionSeconds >= 0.0;
        std::string tranVal = hasCueTrans
          ? formatSeconds(selectedCue->cueTransitionSeconds)
          : "deck";
        drawQuickRow(ry + kRowStep * 5, "trans", QuickAction::TransDec, tranVal, QuickAction::TransInc,
                     QuickAction::ToggleLoop, false, false, "Per-cue transition duration override");
        // Style button: shows cut / crossfade / dip, cycles on click
        {
          constexpr int kLabelW = 64, kBtnW = 32, kValW = 98;
          int rx = ctrl.x + 10;
          int styleX = rx + kLabelW + kBtnW + 4 + kValW + 4 + kBtnW + 4;
          int styleW = (ctrl.x + kCtrlW - 10) - styleX;
          SDL_Rect styleBtn {styleX, ry + kRowStep * 5, styleW, 30};
          std::string curStyle = selectedCue->cueTransitionStyle.empty()
            ? focusedDeck().transitionStyle : selectedCue->cueTransitionStyle;
          // Abbreviate for space
          std::string styleLabel = (curStyle == "crossfade") ? "xfade"
                                 : (curStyle == "dip")       ? "dip"
                                 : (curStyle == "cut")       ? "cut"
                                 : "deck";
          SDL_Color styleFill = hasCueTrans
            ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
          SDL_Color styleInk = hasCueTrans
            ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
          Primitives::drawFramedPanel(controlRenderer_, styleBtn, styleFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
          drawCenteredText(controlRenderer_, fontSmall_, styleLabel, styleInk, styleBtn);
          quickButtons_.push_back({styleBtn, QuickAction::CycleTransStyle,
            "Click to cycle: cut / crossfade / dip  (sets per-cue style)"});
        }
      }
      // loop / hold toggles side by side
      {
        int rx = ctrl.x + 10;
        int ty = ry + kRowStep * 6;
        int halfW = (kCtrlW - 24) / 2;
        SDL_Rect loopBtn {rx, ty, halfW, 30};
        SDL_Rect holdBtn {rx + halfW + 4, ty, halfW, 30};
        SDL_Color loopFill = selectedCue->loop ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
        SDL_Color holdFill = selectedCue->pauseOnLastFrame ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
        SDL_Color loopInk  = selectedCue->loop ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
        SDL_Color holdInk  = selectedCue->pauseOnLastFrame ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
        Primitives::drawFramedPanel(controlRenderer_, loopBtn, loopFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, std::string("loop: ") + (selectedCue->loop ? "on" : "off"), loopInk, loopBtn);
        quickButtons_.push_back({loopBtn, QuickAction::ToggleLoop, "L — loop this cue continuously"});
        Primitives::drawFramedPanel(controlRenderer_, holdBtn, holdFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, std::string("hold: ") + (selectedCue->pauseOnLastFrame ? "on" : "off"), holdInk, holdBtn);
        quickButtons_.push_back({holdBtn, QuickAction::ToggleHold, "E — freeze on last frame instead of stopping"});
      }
      SDL_Rect endBtn {ctrl.x + 10, ry + kRowStep * 7, kCtrlW - 20, 30};
      Primitives::drawFramedPanel(controlRenderer_, endBtn, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(controlRenderer_, fontSmall_, "end: " + cueEndActionLabel(selectedCue->endAction) + "  [X cycle]",
               colorFromRgba(kScreenDeepColor), endBtn.x + 10, endBtn.y + 8);
      quickButtons_.push_back({endBtn, QuickAction::CycleEndAction, "X — cycle end action: stop / next / loop"});
      {
        int rowCursor = 8;
        std::string loopStr = selectedCue->loopCount == 0 ? "inf" : std::to_string(selectedCue->loopCount) + "x";
        drawQuickRow(ry + kRowStep * rowCursor, "repeats", QuickAction::LoopCountDec, loopStr, QuickAction::LoopCountInc,
                     QuickAction::ToggleLoop, false, false, "Fixed repeat count — 0 = loop forever");
        rowCursor += 1;

        std::ostringstream spdSS;
        spdSS << std::fixed << std::setprecision(2) << selectedCue->playbackSpeed;
        drawQuickRow(ry + kRowStep * rowCursor, "speed", QuickAction::SpeedDec, spdSS.str() + "x", QuickAction::SpeedInc,
                     QuickAction::ToggleLoop, false, false, "Playback speed: 0.25–4.0×");
        rowCursor += 1;

        drawQuickRow(ry + kRowStep * rowCursor, "pause in", QuickAction::TogglePauseBegin,
                     selectedCue->pauseAtBeginning ? "on" : "off",
                     QuickAction::TogglePauseBegin, QuickAction::TogglePauseBegin, true, selectedCue->pauseAtBeginning,
                     "Load the cue and hold first frame when taken");
        rowCursor += 1;

        std::string cueAudioLabel = selectedCue->hasAudio
          ? (selectedCue->audioEnabled ? "on" : "off")
          : "n/a";
        drawQuickRow(ry + kRowStep * rowCursor, "audio", QuickAction::ToggleCueAudio,
                     cueAudioLabel,
                     QuickAction::ToggleCueAudio, QuickAction::ToggleCueAudio, true,
                     selectedCue->hasAudio && selectedCue->audioEnabled,
                     "Toggle cue audio track for this cue");
        rowCursor += 1;

        drawQuickRow(ry + kRowStep * rowCursor, "next xfade", QuickAction::ToggleNextTransition,
                     selectedCue->transitionToNext ? "on" : "off",
                     QuickAction::ToggleNextTransition, QuickAction::ToggleNextTransition, true,
                     selectedCue->transitionToNext,
                     "Use transition when auto-advancing or goto-taking next cue");
        rowCursor += 1;

        int gotoY = ry + kRowStep * rowCursor;
        SDL_Rect gotoBox {ctrl.x + 10, gotoY, kCtrlW - 80, 26};
        SDL_Rect gotoEdit {ctrl.x + kCtrlW - 64, gotoY, 54, 26};
        std::string gotoDisplay = selectedCue->gotoTarget.empty() ? "(next cue)" : selectedCue->gotoTarget;
        if (gotoDisplay.size() > 28) {
          gotoDisplay = gotoDisplay.substr(0, 25) + "...";
        }
        Primitives::drawFramedPanel(controlRenderer_, gotoBox, colorFromRgba(kScreenLightColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, gotoDisplay, colorFromRgba(kScreenDeepColor), gotoBox.x + 6, gotoBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, gotoEdit, colorFromRgba(kScreenDarkColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "goto", colorFromRgba(kScreenLightColor), gotoEdit);
        quickButtons_.push_back({gotoEdit, QuickAction::EditGotoTarget, "Set cue token to jump to when cue ends"});
        rowCursor += 1;

        std::string tagStr = selectedCue->colorTag.empty() ? "none" : selectedCue->colorTag;
        SDL_Rect tagBtn {ctrl.x + 10, ry + kRowStep * rowCursor, kCtrlW - 20, 28};
        SDL_Color tagFill = colorTagToSdl(selectedCue->colorTag, 200);
        Primitives::drawFramedPanel(controlRenderer_, tagBtn, tagFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "tag: " + tagStr + "  [K cycle]", colorFromRgba(kScreenLightColor), tagBtn);
        quickButtons_.push_back({tagBtn, QuickAction::CycleColorTag, "C — cycle cue color tag"});
        rowCursor += 1;

        int notesY = ry + kRowStep * rowCursor;
        SDL_Rect notesBox {ctrl.x + 10, notesY, kCtrlW - 80, 26};
        SDL_Rect notesEdit {ctrl.x + kCtrlW - 64, notesY, 54, 26};
        std::string notesDisplay = selectedCue->notes.empty() ? "(no notes)" : selectedCue->notes;
        if (notesDisplay.size() > 28) notesDisplay = notesDisplay.substr(0, 25) + "...";
        Primitives::drawFramedPanel(controlRenderer_, notesBox, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, notesDisplay, colorFromRgba(selectedCue->notes.empty() ? kScreenInkSoftColor : kScreenDeepColor), notesBox.x + 6, notesBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, notesEdit, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), notesEdit);
        quickButtons_.push_back({notesEdit, QuickAction::EditNotes, "Click to edit cue notes"});
        rowCursor += 1;

        int cnY = ry + kRowStep * rowCursor;
        SDL_Rect idLabel {ctrl.x + 10, cnY, 36, 26};
        SDL_Rect val {ctrl.x + 52, cnY, kCtrlW - 122, 26};
        SDL_Rect editBtn {ctrl.x + kCtrlW - 64, cnY, 54, 26};
        drawText(controlRenderer_, fontSmall_, "id", colorFromRgba(kScreenInkSoftColor), idLabel.x + 4, idLabel.y + 6);
        std::string cnDisplay = cueDisplayToken(*selectedCue, focusedDeck().selectedIndex);
        Primitives::drawFramedPanel(controlRenderer_, val, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, cnDisplay, colorFromRgba(kScreenDeepColor), val.x + 6, val.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, editBtn, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), editBtn);
        quickButtons_.push_back({editBtn, QuickAction::EditCueNumber, "Set short cue id for search/goto"});
        rowCursor += 1;

        int ppY = ry + kRowStep * rowCursor;
        int ppCount = static_cast<int>(selectedCue->pausePoints.size());
        SDL_Rect ppLabel {ctrl.x + 10, ppY, 72, 26};
        SDL_Rect addBtn {ctrl.x + 88, ppY, 46, 26};
        SDL_Rect clrBtn {ctrl.x + 140, ppY, 46, 26};
        drawText(controlRenderer_, fontSmall_, "pause pts: " + std::to_string(ppCount),
                 colorFromRgba(kScreenInkSoftColor), ppLabel.x + 4, ppLabel.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, addBtn, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "+now", colorFromRgba(kScreenLightColor), addBtn);
        Primitives::drawFramedPanel(controlRenderer_, clrBtn, colorFromRgba(kDeleteBezelColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "clr", colorFromRgba(kScreenLightColor), clrBtn);
        quickButtons_.push_back({addBtn, QuickAction::AddPausePoint, "Add pause point at current position"});
        quickButtons_.push_back({clrBtn, QuickAction::ClearPausePoints, "Clear all pause points"});
        rowCursor += 1;

        playbackRowsUsed = rowCursor;
      }
      }
      int geoY = cueSectionPlaybackOpen_ ? (ry + kRowStep * playbackRowsUsed) : (playbackBodyY + 4);
      geoY = drawSectionHeader(geoY, "GEOMETRY", cueSectionGeometryOpen_,
                               QuickAction::CueSectionGeometryToggle,
                               "Collapse/expand geometry controls");
      if (cueSectionGeometryOpen_) {
        geoY = drawGeometryRows(geoY, *selectedCue, true);
        geoY = drawColorRows(geoY, *selectedCue);
      }
      geoY = drawSectionHeader(geoY, "KEY", cueSectionKeyOpen_,
                               QuickAction::CueSectionKeyToggle,
                               "Collapse/expand key controls");
      if (cueSectionKeyOpen_) {
        geoY = drawKeyRows(geoY, *selectedCue);
      }
      geoY = drawSectionHeader(geoY, "ROUTING", cueSectionRoutingOpen_,
                               QuickAction::CueSectionRoutingToggle,
                               "Deck -> Output -> Layer route controls");
      if (cueSectionRoutingOpen_) {
        drawCueRoutingRows(geoY);
      }
    } else if (selectedCue && selectedCue->kind == CueKind::LowerThird) {
      // Lower-third / graphic cue settings
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      SDL_Color inkC = colorFromRgba(kScreenDeepColor);
      SDL_Color softC = colorFromRgba(kScreenInkSoftColor);
      drawText(controlRenderer_, fontSmall_, "graphic overlay cue", inkC, ctrl.x + 10, ry);
      // Preview main text line
      SDL_Rect txtPreview {ctrl.x + 10, ry + 24, kCtrlW - 20, 28};
      Primitives::drawFramedPanel(controlRenderer_, txtPreview, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      std::string mainTxt = selectedCue->lowerThirdText.empty() ? selectedCue->name : selectedCue->lowerThirdText;
      drawCenteredText(controlRenderer_, fontSmall_, mainTxt, inkC, txtPreview);
      // Sub text
      SDL_Rect subPreview {ctrl.x + 10, ry + 56, kCtrlW - 20, 24};
      Primitives::drawFramedPanel(controlRenderer_, subPreview, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawCenteredText(controlRenderer_, fontSmall_, selectedCue->lowerThirdSubtext.empty() ? "(no subtext)" : selectedCue->lowerThirdSubtext, softC, subPreview);
      // Background alpha + duration controls
      drawQuickRow(ry + 90, "bg alpha", QuickAction::LowerBgDec,
                   std::to_string(selectedCue->lowerThirdBgAlpha), QuickAction::LowerBgInc,
                   QuickAction::ToggleLoop, false, false, "Lower-third background opacity (0-255)");
      {
        std::string durVal = selectedCue->stillDurationSeconds > 0.0 ? formatSeconds(selectedCue->stillDurationSeconds) : "hold";
        drawQuickRow(ry + 128, "dur", QuickAction::DurDec, durVal, QuickAction::DurInc,
                     QuickAction::ToggleLoop, false, false, "Auto-advance duration — 0 = hold until taken");
      }
      drawText(controlRenderer_, fontSmall_, "LOWERTEXT / LOWERSUB via Companion",
               softC, ctrl.x + 10, ry + 170);
      // Notes row
      {
        int notesY = ry + 192;
        SDL_Rect notesBox {ctrl.x + 10, notesY, kCtrlW - 80, 26};
        SDL_Rect notesEdit {ctrl.x + kCtrlW - 64, notesY, 54, 26};
        std::string notesDisplay = selectedCue->notes.empty() ? "(no notes)" : selectedCue->notes;
        if (notesDisplay.size() > 28) notesDisplay = notesDisplay.substr(0, 25) + "...";
        Primitives::drawFramedPanel(controlRenderer_, notesBox, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, notesDisplay, colorFromRgba(selectedCue->notes.empty() ? kScreenInkSoftColor : kScreenDeepColor), notesBox.x + 6, notesBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, notesEdit, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), notesEdit);
        quickButtons_.push_back({notesEdit, QuickAction::EditNotes, "Click to edit cue notes"});
      }
      // Clear overlay button
      SDL_Rect clearBtn {ctrl.x + 10, ry + 226, kCtrlW - 20, 28};
      Primitives::drawFramedPanel(controlRenderer_, clearBtn, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawCenteredText(controlRenderer_, fontSmall_, "CLEAR OVERLAY  [Backspace]", inkC, clearBtn);
    } else if (selectedCue && (selectedCue->kind == CueKind::Image
                               || selectedCue->kind == CueKind::Pattern
                               || isSourceCueKind(selectedCue->kind))) {
      // Still image / pattern settings — full parity with video panel + duration
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      constexpr int kRowStep = 28;
      drawText(controlRenderer_, fontSmall_, "PLAYBACK", colorFromRgba(kScreenDarkColor), ctrl.x + 10, ry - 14);
      int rowOffset = 0;
      if (selectedCue->kind == CueKind::Pattern) {
        std::string typeId = normalizePatternTypeId(selectedCue->path);
        bool motionEnabled = endsWith(typeId, "-motion");
        std::string label = patternLabelForType(typeId);
        if (label.size() > 26) {
          label = label.substr(0, 23) + "...";
        }
        drawQuickRow(ry, "pattern", QuickAction::PatternTypePrev, label, QuickAction::PatternTypeNext,
                     QuickAction::TogglePatternMotion, true, motionEnabled,
                     "Cycle pattern with +/-; center toggles motion");
        rowOffset = 1;
      }
      // Row 0: duration
      {
        std::string durVal = selectedCue->stillDurationSeconds > 0.0
          ? formatSeconds(selectedCue->stillDurationSeconds) : "hold";
        drawQuickRow(ry + kRowStep * rowOffset, "duration", QuickAction::DurDec, durVal, QuickAction::DurInc,
                     QuickAction::ToggleLoop, false, false, "Auto-advance duration — 0 = hold until taken");
      }
      // Row 1: trans + style button
      {
        bool hasCueTrans = selectedCue->cueTransitionSeconds >= 0.0;
        std::string tranVal = hasCueTrans ? formatSeconds(selectedCue->cueTransitionSeconds) : "deck";
        drawQuickRow(ry + kRowStep * (rowOffset + 1), "trans", QuickAction::TransDec, tranVal, QuickAction::TransInc,
                     QuickAction::ToggleLoop, false, false, "Per-cue transition duration override");
        constexpr int kLabelW = 64, kBtnW = 32, kValW = 98;
        int rx = ctrl.x + 10;
        int styleX = rx + kLabelW + kBtnW + 4 + kValW + 4 + kBtnW + 4;
        int styleW = (ctrl.x + kCtrlW - 10) - styleX;
        SDL_Rect styleBtn {styleX, ry + kRowStep * (rowOffset + 1), styleW, 30};
        std::string curStyle = selectedCue->cueTransitionStyle.empty()
          ? focusedDeck().transitionStyle : selectedCue->cueTransitionStyle;
        std::string styleLabel = (curStyle == "crossfade") ? "xfade"
                               : (curStyle == "dip")       ? "dip"
                               : (curStyle == "cut")       ? "cut" : "deck";
        SDL_Color styleFill = hasCueTrans ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
        SDL_Color styleInk  = hasCueTrans ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
        Primitives::drawFramedPanel(controlRenderer_, styleBtn, styleFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, styleLabel, styleInk, styleBtn);
        quickButtons_.push_back({styleBtn, QuickAction::CycleTransStyle,
          "Click to cycle: cut / crossfade / dip"});
      }
      // Row 2: fade in
      drawQuickRow(ry + kRowStep * (rowOffset + 2), "fade in",  QuickAction::FadeInDec,  formatSeconds(selectedCue->fadeInSeconds),
                   QuickAction::FadeInInc, QuickAction::ToggleLoop, false, false, "Fade-in duration for this still");
      // Row 3: fade out
      drawQuickRow(ry + kRowStep * (rowOffset + 3), "fade out", QuickAction::FadeOutDec, formatSeconds(selectedCue->fadeOutSeconds),
                   QuickAction::FadeOutInc, QuickAction::ToggleLoop, false, false, "Fade-out duration before next cue");
      // Row 4: loop / hold buttons side by side
      {
        int rx = ctrl.x + 10;
        int ty = ry + kRowStep * (rowOffset + 4);
        int halfW = (kCtrlW - 24) / 2;
        SDL_Rect loopBtn {rx, ty, halfW, 30};
        SDL_Rect holdBtn {rx + halfW + 4, ty, halfW, 30};
        SDL_Color loopFill = selectedCue->loop ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
        SDL_Color holdFill = selectedCue->pauseOnLastFrame ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
        SDL_Color loopInk  = selectedCue->loop ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
        SDL_Color holdInk  = selectedCue->pauseOnLastFrame ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
        Primitives::drawFramedPanel(controlRenderer_, loopBtn, loopFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, std::string("loop: ") + (selectedCue->loop ? "on" : "off"), loopInk, loopBtn);
        quickButtons_.push_back({loopBtn, QuickAction::ToggleLoop, "L — loop this still"});
        Primitives::drawFramedPanel(controlRenderer_, holdBtn, holdFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, std::string("hold: ") + (selectedCue->pauseOnLastFrame ? "on" : "off"), holdInk, holdBtn);
        quickButtons_.push_back({holdBtn, QuickAction::ToggleHold, "E — hold on this still indefinitely"});
      }
      // Row 5: end action
      {
        SDL_Rect endBtn {ctrl.x + 10, ry + kRowStep * (rowOffset + 5), kCtrlW - 20, 30};
        Primitives::drawFramedPanel(controlRenderer_, endBtn, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, "end: " + cueEndActionLabel(selectedCue->endAction) + "  [X cycle]",
                 colorFromRgba(kScreenDeepColor), endBtn.x + 10, endBtn.y + 8);
        quickButtons_.push_back({endBtn, QuickAction::CycleEndAction, "X — cycle end action"});
      }
      // Row 6: repeats
      {
        std::string loopStr = selectedCue->loopCount == 0 ? "inf" : std::to_string(selectedCue->loopCount) + "x";
        drawQuickRow(ry + kRowStep * (rowOffset + 6), "repeats", QuickAction::LoopCountDec, loopStr, QuickAction::LoopCountInc,
                     QuickAction::ToggleLoop, false, false, "Fixed repeat count — 0 = loop forever");
      }
      // Row 7: color tag
      {
        std::string tagStr = selectedCue->colorTag.empty() ? "none" : selectedCue->colorTag;
        SDL_Rect tagBtn {ctrl.x + 10, ry + kRowStep * (rowOffset + 7), kCtrlW - 20, 28};
        SDL_Color tagFill = colorTagToSdl(selectedCue->colorTag, 200);
        Primitives::drawFramedPanel(controlRenderer_, tagBtn, tagFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "tag: " + tagStr + "  [K cycle]", colorFromRgba(kScreenLightColor), tagBtn);
        quickButtons_.push_back({tagBtn, QuickAction::CycleColorTag, "K — cycle cue color tag"});
      }
      // Notes row
      {
        int notesY = ry + kRowStep * (rowOffset + 8);
        SDL_Rect notesBox {ctrl.x + 10, notesY, kCtrlW - 80, 26};
        SDL_Rect notesEdit {ctrl.x + kCtrlW - 64, notesY, 54, 26};
        std::string notesDisplay = selectedCue->notes.empty() ? "(no notes)" : selectedCue->notes;
        if (notesDisplay.size() > 28) notesDisplay = notesDisplay.substr(0, 25) + "...";
        Primitives::drawFramedPanel(controlRenderer_, notesBox, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, notesDisplay, colorFromRgba(selectedCue->notes.empty() ? kScreenInkSoftColor : kScreenDeepColor), notesBox.x + 6, notesBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, notesEdit, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), notesEdit);
        quickButtons_.push_back({notesEdit, QuickAction::EditNotes, "Click to edit cue notes"});
      }
      {
        int rowY = ry + kRowStep * (rowOffset + 9);
        rowY = drawSectionHeader(rowY, "GEOMETRY", cueSectionGeometryOpen_,
                                 QuickAction::CueSectionGeometryToggle,
                                 "Collapse/expand geometry controls");
        if (cueSectionGeometryOpen_) {
          rowY = drawGeometryRows(rowY, *selectedCue, true);
          rowY = drawColorRows(rowY, *selectedCue);
        }
        rowY = drawSectionHeader(rowY, "KEY", cueSectionKeyOpen_,
                                 QuickAction::CueSectionKeyToggle,
                                 "Collapse/expand key controls");
        if (cueSectionKeyOpen_) {
          rowY = drawKeyRows(rowY, *selectedCue);
        }
        rowY = drawSectionHeader(rowY, "ROUTING", cueSectionRoutingOpen_,
                                 QuickAction::CueSectionRoutingToggle,
                                 "Deck -> Output -> Layer route controls");
        if (cueSectionRoutingOpen_) {
          rowY = drawCueRoutingRows(rowY);
        }
        int cnY = rowY + 2;
        SDL_Rect label {ctrl.x + 10, cnY, 36, 26};
        SDL_Rect val {ctrl.x + 52, cnY, kCtrlW - 122, 26};
        SDL_Rect editBtn {ctrl.x + kCtrlW - 64, cnY, 54, 26};
        drawText(controlRenderer_, fontSmall_, "#", colorFromRgba(kScreenInkSoftColor), label.x + 4, label.y + 6);
        std::string cnDisplay = cueDisplayToken(*selectedCue, focusedDeck().selectedIndex);
        Primitives::drawFramedPanel(controlRenderer_, val, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, cnDisplay, colorFromRgba(kScreenDeepColor), val.x + 6, val.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, editBtn, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), editBtn);
        quickButtons_.push_back({editBtn, QuickAction::EditCueNumber, "Set short cue label for search/goto"});
      }
    } else if (selectedCue && selectedCue->kind == CueKind::Browser) {
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      constexpr int kRowStep = 28;
      drawText(controlRenderer_, fontSmall_, "PLAYBACK", colorFromRgba(kScreenDarkColor), ctrl.x + 10, ry - 14);
      {
        std::string durVal = selectedCue->stillDurationSeconds > 0.0
          ? formatSeconds(selectedCue->stillDurationSeconds) : "hold";
        drawQuickRow(ry, "duration", QuickAction::DurDec, durVal, QuickAction::DurInc,
                     QuickAction::ToggleLoop, false, false, "Auto-advance duration — 0 = hold until taken");
      }
      {
        bool hasCueTrans = selectedCue->cueTransitionSeconds >= 0.0;
        std::string tranVal = hasCueTrans ? formatSeconds(selectedCue->cueTransitionSeconds) : "deck";
        drawQuickRow(ry + kRowStep, "trans", QuickAction::TransDec, tranVal, QuickAction::TransInc,
                     QuickAction::ToggleLoop, false, false, "Per-cue transition duration override");
        constexpr int kLabelW = 64, kBtnW = 32, kValW = 98;
        int rx = ctrl.x + 10;
        int styleX = rx + kLabelW + kBtnW + 4 + kValW + 4 + kBtnW + 4;
        int styleW = (ctrl.x + kCtrlW - 10) - styleX;
        SDL_Rect styleBtn {styleX, ry + kRowStep, styleW, 30};
        std::string curStyle = selectedCue->cueTransitionStyle.empty()
          ? focusedDeck().transitionStyle : selectedCue->cueTransitionStyle;
        std::string styleLabel = (curStyle == "crossfade") ? "xfade"
                               : (curStyle == "dip")       ? "dip"
                               : (curStyle == "cut")       ? "cut" : "deck";
        SDL_Color styleFill = hasCueTrans ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
        SDL_Color styleInk  = hasCueTrans ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
        Primitives::drawFramedPanel(controlRenderer_, styleBtn, styleFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, styleLabel, styleInk, styleBtn);
        quickButtons_.push_back({styleBtn, QuickAction::CycleTransStyle,
          "Click to cycle: cut / crossfade / dip"});
      }
      // Notes row
      {
        int notesY = ry + kRowStep * 2;
        SDL_Rect notesBox {ctrl.x + 10, notesY, kCtrlW - 80, 26};
        SDL_Rect notesEdit {ctrl.x + kCtrlW - 64, notesY, 54, 26};
        std::string notesDisplay = selectedCue->notes.empty() ? "(no notes)" : selectedCue->notes;
        if (notesDisplay.size() > 28) notesDisplay = notesDisplay.substr(0, 25) + "...";
        Primitives::drawFramedPanel(controlRenderer_, notesBox, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, notesDisplay, colorFromRgba(selectedCue->notes.empty() ? kScreenInkSoftColor : kScreenDeepColor), notesBox.x + 6, notesBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, notesEdit, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), notesEdit);
        quickButtons_.push_back({notesEdit, QuickAction::EditNotes, "Click to edit cue notes"});
      }
      {
        int rowY = ry + kRowStep * 3;
        rowY = drawSectionHeader(rowY, "GEOMETRY", cueSectionGeometryOpen_,
                                 QuickAction::CueSectionGeometryToggle,
                                 "Collapse/expand geometry controls");
        if (cueSectionGeometryOpen_) {
          rowY = drawGeometryRows(rowY, *selectedCue, true);
          rowY = drawColorRows(rowY, *selectedCue);
        }
        rowY = drawSectionHeader(rowY, "KEY", cueSectionKeyOpen_,
                                 QuickAction::CueSectionKeyToggle,
                                 "Collapse/expand key controls");
        if (cueSectionKeyOpen_) {
          rowY = drawKeyRows(rowY, *selectedCue);
        }
        rowY = drawSectionHeader(rowY, "ROUTING", cueSectionRoutingOpen_,
                                 QuickAction::CueSectionRoutingToggle,
                                 "Deck -> Output -> Layer route controls");
        if (cueSectionRoutingOpen_) {
          rowY = drawCueRoutingRows(rowY);
        }
        int cnY = rowY + 2;
        SDL_Rect label {ctrl.x + 10, cnY, 36, 26};
        SDL_Rect val {ctrl.x + 52, cnY, kCtrlW - 122, 26};
        SDL_Rect editBtn {ctrl.x + kCtrlW - 64, cnY, 54, 26};
        drawText(controlRenderer_, fontSmall_, "#", colorFromRgba(kScreenInkSoftColor), label.x + 4, label.y + 6);
        std::string cnDisplay = cueDisplayToken(*selectedCue, focusedDeck().selectedIndex);
        Primitives::drawFramedPanel(controlRenderer_, val, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, cnDisplay, colorFromRgba(kScreenDeepColor), val.x + 6, val.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, editBtn, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), editBtn);
        quickButtons_.push_back({editBtn, QuickAction::EditCueNumber, "Set short cue label for search/goto"});
      }
    } else if (selectedCue && selectedCue->kind == CueKind::Audio) {
      // Audio-only cue settings
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      constexpr int kRowStep = 28;
      drawText(controlRenderer_, fontSmall_, "PLAYBACK", colorFromRgba(kScreenDarkColor), ctrl.x + 10, ry - 14);
      int volPct = static_cast<int>(std::round((engine ? engine->volume() : 1.0f) * 100.0f));
      drawQuickRow(ry, "vol", QuickAction::VolDec, std::to_string(volPct) + "%", QuickAction::VolInc,
                   QuickAction::ToggleLoop, false, false, "Volume: +/- keys or click to adjust");
      drawQuickRow(ry + kRowStep, "fade in", QuickAction::FadeInDec, formatSeconds(selectedCue->fadeInSeconds),
                   QuickAction::FadeInInc, QuickAction::ToggleLoop, false, false, "Fade-in duration");
      drawQuickRow(ry + kRowStep * 2, "fade out", QuickAction::FadeOutDec, formatSeconds(selectedCue->fadeOutSeconds),
                   QuickAction::FadeOutInc, QuickAction::ToggleLoop, false, false, "Fade-out duration");
      // loop / hold toggles
      {
        int rx = ctrl.x + 10;
        int ty = ry + kRowStep * 3;
        int halfW = (kCtrlW - 24) / 2;
        SDL_Rect loopBtn {rx, ty, halfW, 30};
        SDL_Rect holdBtn {rx + halfW + 4, ty, halfW, 30};
        SDL_Color loopFill = selectedCue->loop ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
        SDL_Color holdFill = selectedCue->pauseOnLastFrame ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor);
        SDL_Color loopInk  = selectedCue->loop ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
        SDL_Color holdInk  = selectedCue->pauseOnLastFrame ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
        Primitives::drawFramedPanel(controlRenderer_, loopBtn, loopFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, std::string("loop: ") + (selectedCue->loop ? "on" : "off"), loopInk, loopBtn);
        quickButtons_.push_back({loopBtn, QuickAction::ToggleLoop, "L — loop this audio"});
        Primitives::drawFramedPanel(controlRenderer_, holdBtn, holdFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, std::string("hold: ") + (selectedCue->pauseOnLastFrame ? "on" : "off"), holdInk, holdBtn);
        quickButtons_.push_back({holdBtn, QuickAction::ToggleHold, "E — hold at end"});
      }
      // End action
      {
        SDL_Rect endBtn {ctrl.x + 10, ry + kRowStep * 4, kCtrlW - 20, 30};
        Primitives::drawFramedPanel(controlRenderer_, endBtn, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, "end: " + cueEndActionLabel(selectedCue->endAction) + "  [X cycle]",
                 colorFromRgba(kScreenDeepColor), endBtn.x + 10, endBtn.y + 8);
        quickButtons_.push_back({endBtn, QuickAction::CycleEndAction, "X — cycle end action"});
      }
      // Loop count
      {
        std::string loopStr = selectedCue->loopCount == 0 ? "inf" : std::to_string(selectedCue->loopCount) + "x";
        drawQuickRow(ry + kRowStep * 5, "repeats", QuickAction::LoopCountDec, loopStr, QuickAction::LoopCountInc,
                     QuickAction::ToggleLoop, false, false, "Fixed repeat count — 0 = infinite");
      }
      // Color tag
      {
        std::string tagStr = selectedCue->colorTag.empty() ? "none" : selectedCue->colorTag;
        SDL_Rect tagBtn {ctrl.x + 10, ry + kRowStep * 6, kCtrlW - 20, 28};
        SDL_Color tagFill = colorTagToSdl(selectedCue->colorTag, 200);
        Primitives::drawFramedPanel(controlRenderer_, tagBtn, tagFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "tag: " + tagStr + "  [K cycle]", colorFromRgba(kScreenLightColor), tagBtn);
        quickButtons_.push_back({tagBtn, QuickAction::CycleColorTag, "K — cycle cue color tag"});
      }
      // Notes row
      {
        int notesY = ry + kRowStep * 7;
        SDL_Rect notesBox {ctrl.x + 10, notesY, kCtrlW - 80, 26};
        SDL_Rect notesEdit {ctrl.x + kCtrlW - 64, notesY, 54, 26};
        std::string notesDisplay = selectedCue->notes.empty() ? "(no notes)" : selectedCue->notes;
        if (notesDisplay.size() > 28) notesDisplay = notesDisplay.substr(0, 25) + "...";
        Primitives::drawFramedPanel(controlRenderer_, notesBox, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, notesDisplay, colorFromRgba(selectedCue->notes.empty() ? kScreenInkSoftColor : kScreenDeepColor), notesBox.x + 6, notesBox.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, notesEdit, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), notesEdit);
        quickButtons_.push_back({notesEdit, QuickAction::EditNotes, "Click to edit cue notes"});
      }
      // Cue number row
      {
        int cnY = ry + kRowStep * 8;
        SDL_Rect label {ctrl.x + 10, cnY, 36, 26};
        SDL_Rect val {ctrl.x + 52, cnY, kCtrlW - 122, 26};
        SDL_Rect editBtn {ctrl.x + kCtrlW - 64, cnY, 54, 26};
        drawText(controlRenderer_, fontSmall_, "#", colorFromRgba(kScreenInkSoftColor), label.x + 4, label.y + 6);
        std::string cnDisplay = cueDisplayToken(*selectedCue, focusedDeck().selectedIndex);
        Primitives::drawFramedPanel(controlRenderer_, val, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, cnDisplay, colorFromRgba(kScreenDeepColor), val.x + 6, val.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, editBtn, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), editBtn);
        quickButtons_.push_back({editBtn, QuickAction::EditCueNumber, "Set short cue label for search/goto"});
      }
      // Pause points row
      {
        int ppY = ry + kRowStep * 9;
        int ppCount = static_cast<int>(selectedCue->pausePoints.size());
        SDL_Rect addBtn {ctrl.x + 10, ppY, 80, 26};
        SDL_Rect clrBtn {ctrl.x + 96, ppY, 46, 26};
        SDL_Rect infoLbl {ctrl.x + 148, ppY, kCtrlW - 158, 26};
        Primitives::drawFramedPanel(controlRenderer_, addBtn, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "+pause pt", colorFromRgba(kScreenLightColor), addBtn);
        Primitives::drawFramedPanel(controlRenderer_, clrBtn, colorFromRgba(kDeleteBezelColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "clr", colorFromRgba(kScreenLightColor), clrBtn);
        drawText(controlRenderer_, fontSmall_, std::to_string(ppCount) + " pts",
                 colorFromRgba(kScreenInkSoftColor), infoLbl.x + 4, infoLbl.y + 6);
        quickButtons_.push_back({addBtn, QuickAction::AddPausePoint, "Add auto-pause at current playback position"});
        quickButtons_.push_back({clrBtn, QuickAction::ClearPausePoints, "Clear all pause points"});
      }
      int routeY = ry + kRowStep * 10 + 4;
      routeY = drawSectionHeader(routeY, "ROUTING", cueSectionRoutingOpen_,
                                 QuickAction::CueSectionRoutingToggle,
                                 "Deck -> Output -> Layer route controls");
      if (cueSectionRoutingOpen_) {
        drawCueRoutingRows(routeY);
      }
    } else if (!selectedCue) {
      drawText(controlRenderer_, fontSmall_, "select a cue to edit settings",
               colorFromRgba(kScreenInkSoftColor), ctrl.x + 10, ctrlSettingsY + 18);
      drawText(controlRenderer_, fontSmall_, "G = add lower-third / graphic cue",
               colorFromRgba(kScreenInkSoftColor), ctrl.x + 10, ctrlSettingsY + 38);
    } else {
      drawText(controlRenderer_, fontSmall_, "no per-cue settings for this type",
               colorFromRgba(kScreenInkSoftColor), ctrl.x + 10, ctrlSettingsY + 24);
    }

    SDL_RenderSetClipRect(controlRenderer_, nullptr);
    int settingsContentLogicalBottom = settingsContentTopY;
    for (size_t i = cueSettingsQuickButtonStartIndex_; i < quickButtons_.size(); ++i) {
      settingsContentLogicalBottom = std::max(
        settingsContentLogicalBottom,
        quickButtons_[i].rect.y + quickButtons_[i].rect.h + cueSettingsScroll_);
    }
    int viewportBottom = cueSettingsViewportRect_.y + cueSettingsViewportRect_.h;
    cueSettingsScrollMax_ = std::max(0, settingsContentLogicalBottom - viewportBottom + 6);
    cueSettingsScroll_ = std::clamp(cueSettingsScroll_, 0, cueSettingsScrollMax_);
    if (cueSettingsScrollMax_ > 0 && cueSettingsViewportRect_.h > 10) {
      SDL_Rect rail {
        ctrl.x + kCtrlW - 8,
        cueSettingsViewportRect_.y + 2,
        4,
        cueSettingsViewportRect_.h - 4
      };
      Primitives::fillRect(controlRenderer_, rail, colorFromRgba(kScreenMidColor));
      int thumbH = std::max(24, (cueSettingsViewportRect_.h * cueSettingsViewportRect_.h) /
                                 std::max(1, cueSettingsViewportRect_.h + cueSettingsScrollMax_));
      thumbH = std::min(thumbH, rail.h);
      int travel = std::max(1, rail.h - thumbH);
      int thumbOffset = static_cast<int>(std::lround(
        static_cast<double>(cueSettingsScroll_) / static_cast<double>(cueSettingsScrollMax_) * travel));
      SDL_Rect thumb {rail.x - 1, rail.y + thumbOffset, rail.w + 2, thumbH};
      Primitives::drawFramedPanel(controlRenderer_, thumb, colorFromRgba(kScreenDarkColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
    }

    // --- Cart details --- (anchored to bottom of panel, always kDetailAreaH px tall)
    int detailX = x;
    int detailY = panel.y + panel.h - kDetailAreaH + 4;
    int detailBottom = panel.y + panel.h - 4;
    drawText(controlRenderer_, fontSmall_, "cart details", colorFromRgba(kScreenDeepColor), detailX, detailY);
    drawText(controlRenderer_, fontBase_, selectedCue ? selectedCue->name : "Drop or import some media", colorFromRgba(kScreenDeepColor), detailX, detailY + 16);

    // Progress bar tip
    if (pointInRect(mouseX_, mouseY_, progressBarRect_)) {
      drawHoverTip("Click to seek — drag to scrub", progressBarRect_.x + progressBarRect_.w / 2, progressBarRect_.y);
    }
    // Quick button tips
    for (size_t i = 0; i < quickButtons_.size(); ++i) {
      const auto& qb = quickButtons_[i];
      bool isCueSettingsButton = i >= cueSettingsQuickButtonStartIndex_;
      if (isCueSettingsButton && !pointInRect(mouseX_, mouseY_, cueSettingsViewportRect_)) {
        continue;
      }
      if (!qb.tip.empty() && pointInRect(mouseX_, mouseY_, qb.rect)) {
        drawHoverTip(qb.tip, qb.rect.x + qb.rect.w / 2, qb.rect.y);
        break;
      }
    }

    if (!selectedCue) {
      if (detailY + 40 < detailBottom)
        drawText(controlRenderer_, fontSmall_, "Drop files into the shell or tap Import to add some carts.", colorFromRgba(kScreenInkSoftColor), detailX, detailY + 40);
      if (detailY + 56 < detailBottom)
        drawText(controlRenderer_, fontSmall_, "Shift + arrows shuffles the selected cue up or down.", colorFromRgba(kScreenInkSoftColor), detailX, detailY + 56);
      return;
    }

    std::vector<std::string> lines {
      std::string(selectedCue->kind == CueKind::Browser ? "URL: " : "Path: ") + selectedCue->path,
      "Kind: " + cueKindLabel(selectedCue->kind) + "   " + std::to_string(selectedCue->width) + "x" + std::to_string(selectedCue->height) + "   Duration: " + formatSeconds(selectedCue->duration),
      "Format: " + selectedCue->formatName + "   Video: " + selectedCue->videoCodec + "   Audio: " + (selectedCue->audioCodec.empty() ? "none" : selectedCue->audioCodec),
      "In: " + formatSeconds(selectedCue->inPointSeconds) + "   Out: " + formatSeconds(selectedCue->outPointSeconds > 0.0 ? selectedCue->outPointSeconds : selectedCue->duration) + "   Size: " + std::to_string(static_cast<unsigned long long>(selectedCue->sizeBytes / 1024)) + " KB",
    };

    for (size_t i = 0; i < lines.size(); ++i) {
      int lineY = detailY + 38 + static_cast<int>(i) * 18;
      if (lineY + 14 > detailBottom) break;
      drawText(controlRenderer_, fontSmall_, lines[i], colorFromRgba(kScreenInkSoftColor), detailX, lineY);
    }
    if (selectedCue && !selectedCue->notes.empty()) {
      int notesLineY = detailY + 38 + static_cast<int>(lines.size()) * 18;
      if (notesLineY + 14 <= detailBottom) {
        std::string notesStr = "\xe2\x80\x9c" + selectedCue->notes + "\xe2\x80\x9d";
        drawText(controlRenderer_, fontSmall_, notesStr, colorFromRgba(kScreenDeepColor), detailX, notesLineY);
      }
    }
  }

  static Uint8 edgeBlendAlphaForUv(const Deck& deck, float u, float v) {
    float ax = 1.0f;
    if (deck.edgeBlendLeft > 0.0001f && u < deck.edgeBlendLeft) {
      ax = std::min(ax, u / deck.edgeBlendLeft);
    }
    if (deck.edgeBlendRight > 0.0001f && u > 1.0f - deck.edgeBlendRight) {
      ax = std::min(ax, (1.0f - u) / deck.edgeBlendRight);
    }
    float ay = 1.0f;
    if (deck.edgeBlendTop > 0.0001f && v < deck.edgeBlendTop) {
      ay = std::min(ay, v / deck.edgeBlendTop);
    }
    if (deck.edgeBlendBottom > 0.0001f && v > 1.0f - deck.edgeBlendBottom) {
      ay = std::min(ay, (1.0f - v) / deck.edgeBlendBottom);
    }
    float alphaValue = std::clamp(ax * ay, 0.0f, 1.0f);
    return static_cast<Uint8>(std::lround(alphaValue * 255.0f));
  }

  void presentOutputCompositorToWindow(int outputIndex, int windowW, int windowH) {
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer || !runtime->compositorTexture) {
      return;
    }
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    int hostDeckIndex = std::clamp(output.hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    const Deck& deck = project_.decks[hostDeckIndex];
    int texW = runtime->compositorWidth;
    int texH = runtime->compositorHeight;
    if (texW <= 0 || texH <= 0 || windowW <= 0 || windowH <= 0) {
      return;
    }

    SDL_Rect src {0, 0, std::min(windowW, texW), std::min(windowH, texH)};
    std::string layoutMode = normalizeOutputLayoutMode(output.outputLayoutMode);
    if (project_.outputCanvasEnabled && layoutMode == "span") {
      src.x = std::clamp(deck.canvasViewX, 0, std::max(0, texW - src.w));
      src.y = std::clamp(deck.canvasViewY, 0, std::max(0, texH - src.h));
    }

    bool hasBlend = deck.edgeBlendLeft > 0.0001f || deck.edgeBlendRight > 0.0001f
      || deck.edgeBlendTop > 0.0001f || deck.edgeBlendBottom > 0.0001f;
    bool hasWarp = deck.warpEnabled;
    int orientationDegrees = normalizeOutputOrientationDegrees(output.outputOrientationDegrees);
    bool hasOrientation = orientationDegrees != 0;

#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (hasWarp || hasBlend || hasOrientation) {
      float u0 = static_cast<float>(src.x) / static_cast<float>(texW);
      float v0 = static_cast<float>(src.y) / static_cast<float>(texH);
      float u1 = static_cast<float>(src.x + src.w) / static_cast<float>(texW);
      float v1 = static_cast<float>(src.y + src.h) / static_cast<float>(texH);

      SDL_FPoint uvTL {u0, v0};
      SDL_FPoint uvTR {u1, v0};
      SDL_FPoint uvBR {u1, v1};
      SDL_FPoint uvBL {u0, v1};
      if (orientationDegrees == 90) {
        uvTL = SDL_FPoint {u0, v1};
        uvTR = SDL_FPoint {u0, v0};
        uvBR = SDL_FPoint {u1, v0};
        uvBL = SDL_FPoint {u1, v1};
      } else if (orientationDegrees == 180) {
        uvTL = SDL_FPoint {u1, v1};
        uvTR = SDL_FPoint {u0, v1};
        uvBR = SDL_FPoint {u0, v0};
        uvBL = SDL_FPoint {u1, v0};
      } else if (orientationDegrees == 270) {
        uvTL = SDL_FPoint {u1, v0};
        uvTR = SDL_FPoint {u1, v1};
        uvBR = SDL_FPoint {u0, v1};
        uvBL = SDL_FPoint {u0, v0};
      }

      SDL_FPoint p0 {0.0f, 0.0f};
      SDL_FPoint p1 {static_cast<float>(windowW), 0.0f};
      SDL_FPoint p2 {static_cast<float>(windowW), static_cast<float>(windowH)};
      SDL_FPoint p3 {0.0f, static_cast<float>(windowH)};
      if (hasWarp) {
        p0.x += deck.warpTopLeftX;      p0.y += deck.warpTopLeftY;
        p1.x += deck.warpTopRightX;     p1.y += deck.warpTopRightY;
        p2.x += deck.warpBottomRightX;  p2.y += deck.warpBottomRightY;
        p3.x += deck.warpBottomLeftX;   p3.y += deck.warpBottomLeftY;
      }

      Uint8 aTL = hasBlend ? edgeBlendAlphaForUv(deck, 0.0f, 0.0f) : 255;
      Uint8 aTR = hasBlend ? edgeBlendAlphaForUv(deck, 1.0f, 0.0f) : 255;
      Uint8 aBR = hasBlend ? edgeBlendAlphaForUv(deck, 1.0f, 1.0f) : 255;
      Uint8 aBL = hasBlend ? edgeBlendAlphaForUv(deck, 0.0f, 1.0f) : 255;
      SDL_Vertex verts[4] {
        {p0, SDL_Color {255, 255, 255, aTL}, uvTL},
        {p1, SDL_Color {255, 255, 255, aTR}, uvTR},
        {p2, SDL_Color {255, 255, 255, aBR}, uvBR},
        {p3, SDL_Color {255, 255, 255, aBL}, uvBL},
      };
      const int indices[6] {0, 1, 2, 0, 2, 3};
      SDL_SetTextureBlendMode(runtime->compositorTexture, SDL_BLENDMODE_BLEND);
      if (SDL_RenderGeometry(runtime->outputRenderer, runtime->compositorTexture, verts, 4, indices, 6) == 0) {
        return;
      }
    }
#endif

    if (hasOrientation) {
      SDL_RenderCopyEx(runtime->outputRenderer, runtime->compositorTexture, &src, nullptr,
                       static_cast<double>(orientationDegrees), nullptr, SDL_FLIP_NONE);
    } else {
      SDL_RenderCopy(runtime->outputRenderer, runtime->compositorTexture, &src, nullptr);
    }
  }

  SDL_Texture* ensureLayerBridgeTexture(OutputRuntime& outputRuntime, int sourceDeckIndex, int width, int height) {
    if (width <= 0 || height <= 0) {
      return nullptr;
    }
    auto texIt = outputRuntime.layerBridgeTextures.find(sourceDeckIndex);
    bool needsRecreate = texIt == outputRuntime.layerBridgeTextures.end();
    if (!needsRecreate) {
      int prevW = outputRuntime.layerBridgeTextureWidths[sourceDeckIndex];
      int prevH = outputRuntime.layerBridgeTextureHeights[sourceDeckIndex];
      needsRecreate = prevW != width || prevH != height;
    }
    if (needsRecreate) {
      if (texIt != outputRuntime.layerBridgeTextures.end() && texIt->second) {
        SDL_DestroyTexture(texIt->second);
      }
      SDL_Texture* texture = SDL_CreateTexture(
        outputRuntime.outputRenderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
      );
      if (!texture) {
        outputRuntime.layerBridgeTextures.erase(sourceDeckIndex);
        outputRuntime.layerBridgeTextureWidths.erase(sourceDeckIndex);
        outputRuntime.layerBridgeTextureHeights.erase(sourceDeckIndex);
        return nullptr;
      }
      SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
      outputRuntime.layerBridgeTextures[sourceDeckIndex] = texture;
      outputRuntime.layerBridgeTextureWidths[sourceDeckIndex] = width;
      outputRuntime.layerBridgeTextureHeights[sourceDeckIndex] = height;
      return texture;
    }
    return texIt->second;
  }

  void renderTextureWithCueGeometry(SDL_Renderer* renderer,
                                    SDL_Texture* texture,
                                    int textureWidth,
                                    int textureHeight,
                                    const Cue* cue,
                                    const SDL_Rect& target) {
    if (!renderer || !texture || textureWidth <= 0 || textureHeight <= 0) {
      return;
    }
    float cropLeft = cue ? cue->cropLeft : 0.0f;
    float cropRight = cue ? cue->cropRight : 0.0f;
    float cropTop = cue ? cue->cropTop : 0.0f;
    float cropBottom = cue ? cue->cropBottom : 0.0f;
    int cropL = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureWidth) * cropLeft)), 0, textureWidth - 1);
    int cropR = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureWidth) * cropRight)), 0, textureWidth - 1);
    int cropT = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureHeight) * cropTop)), 0, textureHeight - 1);
    int cropB = std::clamp(static_cast<int>(std::lround(static_cast<double>(textureHeight) * cropBottom)), 0, textureHeight - 1);
    int srcW = std::max(1, textureWidth - cropL - cropR);
    int srcH = std::max(1, textureHeight - cropT - cropB);
    SDL_Rect source {cropL, cropT, srcW, srcH};
    ScaleMode scaleMode = cue ? cue->scaleMode : ScaleMode::Fit;
    double baseScaleX = 1.0;
    double baseScaleY = 1.0;
    if (scaleMode == ScaleMode::Fit) {
      double fit = std::min(
        static_cast<double>(target.w) / static_cast<double>(srcW),
        static_cast<double>(target.h) / static_cast<double>(srcH)
      );
      baseScaleX = fit;
      baseScaleY = fit;
    } else if (scaleMode == ScaleMode::Fill) {
      double fill = std::max(
        static_cast<double>(target.w) / static_cast<double>(srcW),
        static_cast<double>(target.h) / static_cast<double>(srcH)
      );
      baseScaleX = fill;
      baseScaleY = fill;
    } else if (scaleMode == ScaleMode::Stretch) {
      baseScaleX = static_cast<double>(target.w) / static_cast<double>(srcW);
      baseScaleY = static_cast<double>(target.h) / static_cast<double>(srcH);
    }
    float outputScaleX = cue ? cue->outputScaleX : 1.0f;
    float outputScaleY = cue ? cue->outputScaleY : 1.0f;
    float offsetX = cue ? cue->outputOffsetX : 0.0f;
    float offsetY = cue ? cue->outputOffsetY : 0.0f;
    float rotationDegrees = cue ? cue->outputRotationDegrees : 0.0f;
    int drawW = std::max(1, static_cast<int>(std::round(srcW * baseScaleX * static_cast<double>(outputScaleX))));
    int drawH = std::max(1, static_cast<int>(std::round(srcH * baseScaleY * static_cast<double>(outputScaleY))));
    SDL_Rect destination {
      target.x + (target.w - drawW) / 2 + static_cast<int>(offsetX),
      target.y + (target.h - drawH) / 2 + static_cast<int>(offsetY),
      drawW,
      drawH
    };
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_Point center {destination.w / 2, destination.h / 2};
    SDL_RenderCopyEx(renderer, texture, &source, &destination, rotationDegrees, &center, SDL_FLIP_NONE);
  }

  void renderDeckLayerIntoOutput(int outputIndex, int sourceDeckIndex, const SDL_Rect& target) {
    OutputRuntime* outputRuntime = runtimeForOutput(outputIndex);
    if (!outputRuntime || !outputRuntime->outputRenderer) {
      return;
    }
    if (sourceDeckIndex < 0 || sourceDeckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    const Cue* sourceCue = activeCuePtr(sourceDeckIndex);
    if (!sourceCue) {
      return;
    }
    DeckRuntime* sourceRuntime = runtimeForDeck(sourceDeckIndex);
    if (!sourceRuntime || !sourceRuntime->mediaEngine) {
      return;
    }
    const DecodedFrame* sourceFrame = sourceRuntime->mediaEngine->currentFrame();
    if (!sourceFrame || sourceFrame->width <= 0 || sourceFrame->height <= 0 || sourceFrame->pixels.empty()) {
      return;
    }
    SDL_Texture* bridgeTexture = ensureLayerBridgeTexture(*outputRuntime, sourceDeckIndex, sourceFrame->width, sourceFrame->height);
    if (!bridgeTexture) {
      return;
    }
    const std::uint8_t* uploadPixels = sourceFrame->pixels.data();
    if (cueHasPixelEffects(*sourceCue)) {
      outputRuntime->layerBridgeScratchPixels = sourceFrame->pixels;
      applyCueVisualEffectsToPixels(outputRuntime->layerBridgeScratchPixels, *sourceCue);
      uploadPixels = outputRuntime->layerBridgeScratchPixels.data();
    }
    SDL_UpdateTexture(bridgeTexture, nullptr, uploadPixels, sourceFrame->width * 4);
    float deckOpacity = std::clamp(project_.decks[sourceDeckIndex].playlistOpacity, 0.0f, 1.0f);
    Uint8 alpha = static_cast<Uint8>(std::lround(deckOpacity * 255.0f));
    SDL_SetTextureAlphaMod(bridgeTexture, alpha);
    renderTextureWithCueGeometry(outputRuntime->outputRenderer, bridgeTexture, sourceFrame->width, sourceFrame->height, sourceCue, target);
    SDL_SetTextureAlphaMod(bridgeTexture, 255);
  }

  void renderOutputTestCard(int outputIndex, SDL_Renderer* renderer, int width, int height) {
    if (!renderer || width <= 0 || height <= 0) {
      return;
    }
    const OutputTarget& output = project_.outputs[std::clamp(outputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1))];
    auto fill = [&](int x, int y, int w, int h, SDL_Color color) {
      SDL_Rect rect {x, y, w, h};
      SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
      SDL_RenderFillRect(renderer, &rect);
    };

    const std::array<SDL_Color, 7> bars {{
      SDL_Color {191, 191, 191, 255},
      SDL_Color {191, 191,   0, 255},
      SDL_Color {  0, 191, 191, 255},
      SDL_Color {  0, 191,   0, 255},
      SDL_Color {191,   0, 191, 255},
      SDL_Color {191,   0,   0, 255},
      SDL_Color {  0,   0, 191, 255},
    }};
    int topH = height * 2 / 3;
    int barW = std::max(1, width / static_cast<int>(bars.size()));
    for (int i = 0; i < static_cast<int>(bars.size()); ++i) {
      int x = i * barW;
      int w = (i == static_cast<int>(bars.size()) - 1) ? (width - x) : barW;
      fill(x, 0, w, topH, bars[static_cast<size_t>(i)]);
    }

    int midY = topH;
    int midH = std::max(10, height / 10);
    fill(0, midY, width, midH, SDL_Color {18, 18, 18, 255});
    for (int i = 0; i < 8; ++i) {
      int x = i * width / 8;
      int w = (i == 7) ? (width - x) : (width / 8);
      Uint8 gray = static_cast<Uint8>(i * 255 / 7);
      fill(x, midY + 2, w, midH - 4, SDL_Color {gray, gray, gray, 255});
    }

    int botY = midY + midH;
    int botH = std::max(1, height - botY);
    fill(0, botY, width / 4, botH, SDL_Color {0, 0, 0, 255});
    fill(width / 4, botY, width / 4, botH, SDL_Color {255, 255, 255, 255});
    fill(width / 2, botY, width / 4, botH, SDL_Color {18, 18, 18, 255});
    fill((width * 3) / 4, botY, width - (width * 3) / 4, botH, SDL_Color {36, 36, 36, 255});

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 190);
    SDL_RenderDrawLine(renderer, width / 2, 0, width / 2, height);
    SDL_RenderDrawLine(renderer, 0, height / 2, width, height / 2);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 130);
    SDL_Rect safe80 {width / 10, height / 10, width - (width / 10) * 2, height - (height / 10) * 2};
    SDL_RenderDrawRect(renderer, &safe80);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    std::string outName = output.name.empty() ? outputDefaultName(outputIndex) : output.name;
    std::string line1 = "OUTPUT TEST CARD";
    std::string line2 = "Output " + std::to_string(outputIndex + 1) + " - " + outName;
    std::string line3 = "layout: " + normalizeOutputLayoutMode(output.outputLayoutMode)
      + "  rot: " + outputOrientationLabel(output.outputOrientationDegrees);
    drawText(renderer, fontLarge_, line1, SDL_Color {245, 245, 245, 255}, 22, 20);
    drawText(renderer, fontSmall_, line2, SDL_Color {245, 245, 245, 240}, 24, 52);
    drawText(renderer, fontSmall_, line3, SDL_Color {220, 220, 220, 220}, 24, 70);
  }

  void renderOutputWindow(int outputIndex) {
    if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
      return;
    }
    OutputRuntime* runtime = runtimeForOutput(outputIndex);
    if (!runtime || !runtime->outputRenderer) {
      return;
    }
    const OutputTarget& output = project_.outputs[outputIndex];
    if (!output.enabled) {
      return;
    }
    std::string outputType = normalizeOutputType(output.outputType);
    bool streamType = outputType == "stream";
    int compositionOutputIndex = outputIndex;
    if (streamType &&
        output.mirrorSourceOutputIndex >= 0 &&
        output.mirrorSourceOutputIndex < static_cast<int>(project_.outputs.size()) &&
        output.mirrorSourceOutputIndex != outputIndex) {
      compositionOutputIndex = output.mirrorSourceOutputIndex;
    }
    const OutputTarget& compositionOutput = project_.outputs[compositionOutputIndex];
    int hostDeckIndex = std::clamp(compositionOutput.hostDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    const Deck& hostDeck = project_.decks[hostDeckIndex];

    int width = 0;
    int height = 0;
    if (!streamType && runtime->outputWindow) {
      SDL_GetWindowSize(runtime->outputWindow, &width, &height);
    } else {
      auto [rasterW, rasterH] = outputRenderSizeForOutput(compositionOutputIndex);
      width = rasterW;
      height = rasterH;
    }
    width = std::max(1, width);
    height = std::max(1, height);
    if (project_.outputCanvasEnabled) {
      clampDeckCanvasViewToWindow(hostDeckIndex, width, height);
    }
    int targetCompositorW = width;
    int targetCompositorH = height;
    if (project_.outputCanvasEnabled) {
      auto [canvasW, canvasH] = outputCanvasRenderSize();
      targetCompositorW = canvasW;
      targetCompositorH = canvasH;
    }
    if (!runtime->compositorTexture
        || runtime->compositorWidth != targetCompositorW
        || runtime->compositorHeight != targetCompositorH) {
      configureOutputCompositor(outputIndex, targetCompositorW, targetCompositorH);
    }
    bool usingCompositor = runtime->compositorTexture != nullptr;
    if (usingCompositor) {
      SDL_SetRenderTarget(runtime->outputRenderer, runtime->compositorTexture);
    }
    int renderW = usingCompositor ? runtime->compositorWidth : width;
    int renderH = usingCompositor ? runtime->compositorHeight : height;
    SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0, 255);
    SDL_RenderClear(runtime->outputRenderer);

    SDL_Rect bounds {0, 0, renderW, renderH};
    auto outputLayers = layeredDeckEntriesForOutput(compositionOutputIndex);
    if (outputLayers.empty()) {
      outputLayers.emplace_back(0, hostDeckIndex);
    }
    if (output.outputTestCardEnabled) {
      renderOutputTestCard(outputIndex, runtime->outputRenderer, renderW, renderH);
    } else {
      for (const auto& entry : outputLayers) {
        renderDeckLayerIntoOutput(outputIndex, entry.second, bounds);
      }

      // Output window is always clean black — no status overlays or decorations.
      // The only things drawn here are the media content itself, cue overlays, and
      // the optional time/ID overlay that the operator explicitly enables.
      const Cue* activeCue = activeCuePtr(hostDeckIndex);

      // Audio-only cue: draw a centred waveform + info on the output window
      if (activeCue && activeCue->kind == CueKind::Audio) {
        int margin = renderW / 10;
        SDL_Rect wfRect {margin, renderH / 4, renderW - margin * 2, renderH / 3};
        std::vector<float> peaks;
        { std::lock_guard<std::mutex> lk(waveformMutex_);
          auto it = waveformCache_.find(activeCue->path);
          if (it != waveformCache_.end()) peaks = it->second; }
        double dur = activeCue->duration > 0.0 ? activeCue->duration : 1.0;
        const MediaEngine* eng = mediaEngineForDeck(hostDeckIndex);
        float playFrac = eng ? static_cast<float>(std::clamp(eng->position() / dur, 0.0, 1.0)) : -1.0f;
        float inFrac  = static_cast<float>(activeCue->inPointSeconds / dur);
        float outFrac = activeCue->outPointSeconds > 0.0
                      ? static_cast<float>(activeCue->outPointSeconds / dur) : 1.0f;
        drawWaveform(runtime->outputRenderer, wfRect, peaks, playFrac, inFrac, outFrac,
                     activeCue->pausePoints, dur);
        // Cue name
        drawText(runtime->outputRenderer, fontBase_, activeCue->name,
                 colorFromRgba(kScreenLightColor), wfRect.x, wfRect.y - 36);
        // Transport position + duration
        std::string posStr = (eng ? formatSeconds(eng->position()) : "0:00")
                           + "  /  " + formatSeconds(activeCue->duration);
        drawText(runtime->outputRenderer, fontSmall_, posStr,
                 colorFromRgba(kScreenMidColor), wfRect.x, wfRect.y + wfRect.h + 10);
        // State badge
        std::string stateLbl = !eng ? "stopped"
                             : eng->state() == TransportState::Playing ? "playing"
                             : eng->state() == TransportState::Paused  ? "paused" : "stopped";
        drawText(runtime->outputRenderer, fontSmall_, stateLbl,
                 colorFromRgba(kScreenDarkColor), wfRect.x + wfRect.w - 60, wfRect.y - 36);
      }

      // Overlay layer stack — rendered bottom to top in push order.
      int overlaySlot = 0;
      for (int ovIdx : hostDeck.overlayActiveIndices) {
        if (ovIdx < 0 || ovIdx >= static_cast<int>(hostDeck.cues.size())) continue;
        const Cue& lc = hostDeck.cues[ovIdx];
        if (lc.kind == CueKind::LowerThird) {
          // Stack lower-thirds bottom-up: first slot at bottom, each extra one steps up.
          int barH = renderH / 6;
          int barY = renderH - barH - renderH / 20 - overlaySlot * (barH + 8);
          SDL_Rect bar {0, barY, renderW, barH};

          SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(runtime->outputRenderer, 8, 16, 24, static_cast<Uint8>(lc.lowerThirdBgAlpha));
          SDL_RenderFillRect(runtime->outputRenderer, &bar);

          // Coloured accent strip (hue shifts per slot for differentiation)
          static constexpr std::array<SDL_Color, 4> accentColors {{
            {155, 188,  15, 220},
            { 15, 155, 188, 220},
            {188,  15, 155, 220},
            {188, 155,  15, 220},
          }};
          SDL_Color acc = accentColors[static_cast<size_t>(overlaySlot) % accentColors.size()];
          SDL_SetRenderDrawColor(runtime->outputRenderer, acc.r, acc.g, acc.b, acc.a);
          SDL_Rect strip {bar.x, bar.y, 8, bar.h};
          SDL_RenderFillRect(runtime->outputRenderer, &strip);
          SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_NONE);

          std::string mainTxt = lc.lowerThirdText.empty() ? lc.name : lc.lowerThirdText;
          drawText(runtime->outputRenderer, fontLarge_, mainTxt,
                   {255, 255, 255, 255}, bar.x + 24, bar.y + 14);
          if (!lc.lowerThirdSubtext.empty()) {
            drawText(runtime->outputRenderer, fontBase_, lc.lowerThirdSubtext,
                     {200, 220, 200, 255}, bar.x + 26, bar.y + barH - 36);
          }
          ++overlaySlot;
        }
      }

      if (output.outputTimeOverlayEnabled || hostDeck.timeOverlayEnabled) {
        const MediaEngine* engine = mediaEngineForDeck(hostDeckIndex);
        std::string position = formatSeconds(engine ? engine->position() : 0.0);
        std::string total = formatSeconds(engine ? engine->duration() : 0.0);
        std::string timeLine = position + " / " + total;
        std::string cueIdLine = activeCue ? ("id: " + activeCue->id) : "id: --";
        std::string tcLine = "tc: " + formatTimecode(hostDeck.timecodeCurrentSeconds, hostDeck.timecodeFps);
        SDL_Rect overlay {26, 26, std::max(300, renderW / 3), 72};
        Primitives::drawFramedPanel(runtime->outputRenderer, overlay, {15, 56, 15, 204}, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenMidColor));
        drawText(runtime->outputRenderer, fontMono_, timeLine, colorFromRgba(kScreenLightColor), overlay.x + 14, overlay.y + 9);
        drawText(runtime->outputRenderer, fontSmall_, cueIdLine, colorFromRgba(kScreenMidColor), overlay.x + 14, overlay.y + 34);
        drawText(runtime->outputRenderer, fontSmall_, tcLine, colorFromRgba(kScreenMidColor), overlay.x + 14, overlay.y + 50);
      }
    }

    // Master video dimmer overlay
    if (project_.masterDimmer < 0.999) {
      SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0,
        static_cast<Uint8>((1.0 - project_.masterDimmer) * 255.0));
      SDL_RenderFillRect(runtime->outputRenderer, nullptr);
      SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_NONE);
    }
    float outputAlpha = std::clamp(output.outputAlpha, 0.0f, 1.0f);
    if (outputAlpha < 0.999f) {
      SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0,
        static_cast<Uint8>((1.0f - outputAlpha) * 255.0f));
      SDL_RenderFillRect(runtime->outputRenderer, nullptr);
      SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_NONE);
    }
    if (usingCompositor) {
      SDL_SetRenderTarget(runtime->outputRenderer, nullptr);
      if (!streamType) {
        SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0, 255);
        SDL_RenderClear(runtime->outputRenderer);
        presentOutputCompositorToWindow(outputIndex, width, height);
      }
    }
    bool needsEgressCapture =
      output.streamEnabled || output.ndiEnabled || output.ndiKeyEnabled
      || std::clamp(output.outputDelayMs, 0, 5000) > 0;
    SDL_Rect egressRect {0, 0, renderW, renderH};
    if (usingCompositor) {
      egressRect.w = std::max(1, std::min(width, renderW));
      egressRect.h = std::max(1, std::min(height, renderH));
      if (project_.outputCanvasEnabled &&
          normalizeOutputLayoutMode(output.outputLayoutMode) == "span") {
        egressRect.x = std::clamp(hostDeck.canvasViewX, 0, std::max(0, renderW - egressRect.w));
        egressRect.y = std::clamp(hostDeck.canvasViewY, 0, std::max(0, renderH - egressRect.h));
      }
    }
    if (needsEgressCapture) {
      captureOutputFrameForEgress(outputIndex, *runtime, egressRect);
    } else {
      runtime->latestCapturedFrame = {};
      runtime->delayFrames.clear();
    }
    double fpsHint = 30.0;
    for (auto it = outputLayers.rbegin(); it != outputLayers.rend(); ++it) {
      const Cue* layerCue = activeCuePtr(it->second);
      if (layerCue && layerCue->kind == CueKind::Video) {
        fpsHint = std::max(1.0, layerCue->fps);
        break;
      }
    }
    sendOutputNdiFrame(outputIndex, *runtime, width, height, fpsHint);
    sendOutputStreamFrame(outputIndex, width, height, fpsHint);
    if (!streamType) {
      SDL_RenderPresent(runtime->outputRenderer);
    }
  }

  void handleRightClick(int x, int y) {
    // Determine which cue was right-clicked
    for (int di = 0; di < static_cast<int>(deckListClipRects_.size()); ++di) {
      const SDL_Rect& clipFrame = deckListClipRects_[di];
      SDL_Rect clipRect {clipFrame.x + 8, clipFrame.y + 8, clipFrame.w - 16, clipFrame.h - 16};
      if (!pointInRect(x, y, clipRect)) continue;
      int listY = clipRect.y - deckScrolls_[di];
      for (int ci = 0; ci < static_cast<int>(project_.decks[di].cues.size()); ++ci) {
        SDL_Rect row {clipRect.x, listY, clipRect.w, kRowHeight};
        if (pointInRect(x, y, row)) {
          openContextMenu(di, ci, x, y);
          return;
        }
        listY += kRowHeight + 8;
      }
    }
    contextMenuOpen_ = false;
  }

  void openContextMenu(int deckIdx, int cueIdx, int mx, int my) {
    contextMenuOpen_ = true;
    contextMenuDeckIdx_ = deckIdx;
    contextMenuCueIdx_ = cueIdx;
    contextItems_.clear();

    Deck& deck = project_.decks[deckIdx];
    Cue& cue = deck.cues[cueIdx];

    // Color tag items
    static const std::vector<std::pair<std::string, SDL_Color>> kTagOpts = {
      {"no color",  {48,  98,  48,  255}},
      {"red",       {180, 40,  40,  255}},
      {"orange",    {190, 100, 20,  255}},
      {"yellow",    {160, 145, 10,  255}},
      {"cyan",      {15,  140, 140, 255}},
      {"blue",      {20,  60,  175, 255}},
      {"purple",    {110, 30,  150, 255}},
      {"pink",      {175, 45,  115, 255}},
    };
    for (const auto& [label, col] : kTagOpts) {
      std::string tag = label == "no color" ? "" : label;
      bool isCurrent = cue.colorTag == tag;
      contextItems_.push_back({
        (isCurrent ? "* " : "  ") + label,
        col,
        [this, deckIdx, cueIdx, tag]() {
          project_.decks[deckIdx].cues[cueIdx].colorTag = tag;
          triggerToast("tag: " + (tag.empty() ? "none" : tag));
          markProjectDirty();
        }
      });
    }
    contextItems_.push_back({"— delete cue", {80, 30, 30, 255}, [this, deckIdx, cueIdx]() {
      Deck& d = project_.decks[deckIdx];
      if (cueIdx >= 0 && cueIdx < static_cast<int>(d.cues.size())) {
        d.cues.erase(d.cues.begin() + cueIdx);
        d.selectedIndex = std::clamp(d.selectedIndex, 0, static_cast<int>(d.cues.size()) - 1);
        d.activeIndex = std::clamp(d.activeIndex, -1, static_cast<int>(d.cues.size()) - 1);
        onSelectionChanged();
        markProjectDirty();
      }
    }});

    // Position menu so it fits on screen
    int winW = 0, winH = 0;
    SDL_GetWindowSize(controlWindow_, &winW, &winH);
    constexpr int kItemH = 32;
    constexpr int kMenuW = 212;
    int menuH = static_cast<int>(contextItems_.size()) * kItemH + 8;
    int mx2 = std::min(mx, winW - kMenuW - 4);
    int my2 = std::min(my, winH - menuH - 4);
    contextMenuRect_ = {mx2, my2, kMenuW, menuH};
    int iy = my2 + 4;
    for (auto& item : contextItems_) {
      item.rect = {mx2 + 4, iy, kMenuW - 8, kItemH - 2};
      iy += kItemH;
    }
  }

  void handleContextMenuClick(int x, int y) {
    if (!contextMenuOpen_) return;
    if (!pointInRect(x, y, contextMenuRect_)) {
      contextMenuOpen_ = false;
      return;
    }
    for (auto& item : contextItems_) {
      if (pointInRect(x, y, item.rect)) {
        if (item.action) item.action();
        contextMenuOpen_ = false;
        return;
      }
    }
    contextMenuOpen_ = false;
  }

  void renderContextMenu() {
    if (!contextMenuOpen_) return;
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_Color bg {20, 50, 20, 245};
    Primitives::fillRect(controlRenderer_, contextMenuRect_, bg);
    Primitives::strokeRect(controlRenderer_, contextMenuRect_, colorFromRgba(kScreenDarkColor));
    for (const auto& item : contextItems_) {
      bool hover = pointInRect(mouseX_, mouseY_, item.rect);
      if (hover) {
        SDL_Color hov {48, 90, 48, 200};
        Primitives::fillRect(controlRenderer_, item.rect, hov);
      }
      // Color swatch (small square on left)
      if (item.swatch.a > 0) {
        SDL_Rect sw {item.rect.x, item.rect.y + 5, 12, item.rect.h - 10};
        Primitives::fillRect(controlRenderer_, sw, item.swatch);
      }
      drawText(controlRenderer_, fontSmall_, item.label,
               hover ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor),
               item.rect.x + 18, item.rect.y + 7);
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
  }

  SDL_Rect settingsModalRect() const {
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);
    bool videoTab = settingsTab_ == 3;
    int margin = videoTab ? 10 : 20;
    int minW = videoTab ? 1160 : 760;
    int minH = videoTab ? 670 : 520;
    int maxW = videoTab ? 1560 : 1140;
    int maxH = videoTab ? 940 : 760;
    int modalW = std::clamp(width - margin * 2, minW, maxW);
    int modalH = std::clamp(height - margin * 2, minH, maxH);
    modalW = std::min(modalW, std::max(320, width - 12));
    modalH = std::min(modalH, std::max(260, height - 12));
    return SDL_Rect {(width - modalW) / 2, (height - modalH) / 2, modalW, modalH};
  }

  void renderSettingsModal() {
    if (!settingsOpen_) return;
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);

    // Dim backdrop
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(controlRenderer_, 0, 0, 0, 160);
    SDL_Rect full {0, 0, width, height};
    SDL_RenderFillRect(controlRenderer_, &full);
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);

    // Modal panel
    SDL_Rect modal = settingsModalRect();
    Primitives::drawFramedPanel(controlRenderer_, modal, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));

    // Title
    drawText(controlRenderer_, fontBase_, "SYSTEM SETTINGS", colorFromRgba(kScreenDeepColor), modal.x + 16, modal.y + 10);

    // Close button [X]
    settingsCloseBtn_ = {modal.x + modal.w - 42, modal.y + 6, 34, 30};
    Primitives::drawFramedPanel(controlRenderer_, settingsCloseBtn_, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
    drawCenteredText(controlRenderer_, fontSmall_, "X", colorFromRgba(kScreenDeepColor), settingsCloseBtn_);

    // Tab bar
    constexpr int kTabW = 118;
    constexpr int kTabH = 36;
    int tabY = modal.y + 44;
    settingsBtns_.clear();
    const std::vector<std::string> tabs {"System", "Audio", "Network", "Video Outputs", "About"};
    for (int t = 0; t < (int)tabs.size(); ++t) {
      SDL_Rect tab {modal.x + 16 + t * (kTabW + 4), tabY, kTabW, kTabH};
      bool active = (t == settingsTab_);
      Primitives::drawFramedPanel(controlRenderer_, tab, active ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenLightColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawCenteredText(controlRenderer_, fontSmall_, tabs[t],
                       active ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor), tab);
      settingsBtns_.push_back({tab, 100 + t, tabs[t]});
    }

    // Content area
    SDL_Rect content {modal.x + 16, tabY + kTabH + 10, modal.w - 32, modal.h - kTabH - 82};
    Primitives::drawFramedPanel(controlRenderer_, content, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));

    int cx = content.x + 12, cy = content.y + 10;
    SDL_Color ink = colorFromRgba(kScreenDeepColor);
    SDL_Color soft = colorFromRgba(kScreenInkSoftColor);

    if (settingsTab_ == 0) {
      // System tab (clean two-column layout)
      const Deck& tcDeck = focusedDeck();
      auto drawPillToggle = [&](const SDL_Rect& rect, bool on, const std::string& onLabel = "ON", const std::string& offLabel = "OFF") {
        Primitives::drawFramedPanel(controlRenderer_, rect, on ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                        colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
        drawCenteredText(controlRenderer_, fontSmall_, on ? onLabel : offLabel,
                         on ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor), rect);
      };

      int colGap = 12;
      int leftW = std::max(320, (content.w - 12 - colGap) / 2);
      int rightW = std::max(320, content.w - 12 - colGap - leftW);
      SDL_Rect leftCol {cx, cy, leftW, content.h - 20};
      SDL_Rect rightCol {cx + leftW + colGap, cy, rightW, content.h - 20};
      Primitives::drawFramedPanel(controlRenderer_, leftCol, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      Primitives::drawFramedPanel(controlRenderer_, rightCol, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));

      SDL_Rect sysRect {leftCol.x + 8, leftCol.y + 8, leftCol.w - 16, 126};
      Primitives::drawFramedPanel(controlRenderer_, sysRect, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(controlRenderer_, fontBase_, "SYSTEM", ink, sysRect.x + 8, sysRect.y + 6);
      drawText(controlRenderer_, fontSmall_, "Audio device and UI feedback", soft, sysRect.x + 8, sysRect.y + 28);
      SDL_Rect devBtn {sysRect.x + 8, sysRect.y + 48, sysRect.w - 16, 26};
      Primitives::drawFramedPanel(controlRenderer_, devBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      std::string devName = focusedDeck().audioOutputDeviceName.empty() ? "(default)" : focusedDeck().audioOutputDeviceName;
      drawCenteredText(controlRenderer_, fontSmall_, ellipsizeToPixelWidth(fontSmall_, devName, devBtn.w - 14), ink, devBtn);
      settingsBtns_.push_back({devBtn, 200, "audio_device"});
      int toggleW = std::max(84, (sysRect.w - 24) / 2);
      SDL_Rect sfxBtn {sysRect.x + 8, sysRect.y + 84, toggleW, 24};
      SDL_Rect animBtn {sfxBtn.x + toggleW + 8, sysRect.y + 84, sysRect.w - 16 - toggleW - 8, 24};
      drawPillToggle(sfxBtn, project_.uiSoundsEnabled, "SFX ON", "SFX OFF");
      drawPillToggle(animBtn, project_.uiTransitionsEnabled, "ANIM ON", "ANIM OFF");
      settingsBtns_.push_back({sfxBtn, 201, "sfx_toggle"});
      settingsBtns_.push_back({animBtn, 202, "anim_toggle"});

      SDL_Rect playRect {leftCol.x + 8, sysRect.y + sysRect.h + 8, leftCol.w - 16, leftCol.h - (sysRect.h + 24)};
      Primitives::drawFramedPanel(controlRenderer_, playRect, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(controlRenderer_, fontBase_, "PLAYBACK", ink, playRect.x + 8, playRect.y + 6);
      SDL_Rect jumpModeBtn {playRect.x + 8, playRect.y + 32, 132, 24};
      SDL_Rect jumpTransBtn {jumpModeBtn.x + jumpModeBtn.w + 8, playRect.y + 32, playRect.w - 16 - jumpModeBtn.w - 8, 24};
      Primitives::drawFramedPanel(controlRenderer_, jumpModeBtn, colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, jumpModeLabelFromToken(project_.jumpMode), ink, jumpModeBtn);
      settingsBtns_.push_back({jumpModeBtn, 203, "jump_mode"});
      drawPillToggle(jumpTransBtn, project_.jumpTransitionEnabled, "XFADE ON", "XFADE OFF");
      settingsBtns_.push_back({jumpTransBtn, 204, "jump_transition"});

      drawText(controlRenderer_, fontSmall_, "panic profile", soft, playRect.x + 8, playRect.y + 64);
      SDL_Rect panicPrevBtn {playRect.x + 8, playRect.y + 82, 24, 24};
      SDL_Rect panicNextBtn {playRect.x + playRect.w - 32, playRect.y + 82, 24, 24};
      SDL_Rect panicLabelRect {panicPrevBtn.x + panicPrevBtn.w + 6, playRect.y + 82, panicNextBtn.x - (panicPrevBtn.x + panicPrevBtn.w + 12), 24};
      Primitives::drawFramedPanel(controlRenderer_, panicPrevBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "<", ink, panicPrevBtn);
      settingsBtns_.push_back({panicPrevBtn, 205, "panic_profile_prev"});
      Primitives::drawFramedPanel(controlRenderer_, panicLabelRect, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, panicProfileLabelFromToken(project_.panicProfile), ink, panicLabelRect);
      Primitives::drawFramedPanel(controlRenderer_, panicNextBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, ">", ink, panicNextBtn);
      settingsBtns_.push_back({panicNextBtn, 206, "panic_profile_next"});

      SDL_Rect panicRunBtn {playRect.x + 8, playRect.y + 114, playRect.w - 16, 26};
      Primitives::drawFramedPanel(controlRenderer_, panicRunBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Run Panic", ink, panicRunBtn);
      settingsBtns_.push_back({panicRunBtn, 207, "panic_run"});

      SDL_Rect safetyRect {rightCol.x + 8, rightCol.y + 8, rightCol.w - 16, 142};
      Primitives::drawFramedPanel(controlRenderer_, safetyRect, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(controlRenderer_, fontBase_, "TIMECODE / SAFETY", ink, safetyRect.x + 8, safetyRect.y + 6);
      drawText(controlRenderer_, fontSmall_, "panic fade", soft, safetyRect.x + 8, safetyRect.y + 32);
      SDL_Rect panicFadeDecBtn {safetyRect.x + 8, safetyRect.y + 50, 24, 22};
      SDL_Rect panicFadeValRect {panicFadeDecBtn.x + 28, panicFadeDecBtn.y, 66, 22};
      SDL_Rect panicFadeIncBtn {panicFadeValRect.x + panicFadeValRect.w + 4, panicFadeDecBtn.y, 24, 22};
      Primitives::drawFramedPanel(controlRenderer_, panicFadeDecBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "-", ink, panicFadeDecBtn);
      settingsBtns_.push_back({panicFadeDecBtn, 208, "panic_fade_dec"});
      std::ostringstream panicFadeLabel;
      panicFadeLabel << std::fixed << std::setprecision(1) << project_.panicFadeSeconds << "s";
      Primitives::drawFramedPanel(controlRenderer_, panicFadeValRect, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, panicFadeLabel.str(), ink, panicFadeValRect);
      Primitives::drawFramedPanel(controlRenderer_, panicFadeIncBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "+", ink, panicFadeIncBtn);
      settingsBtns_.push_back({panicFadeIncBtn, 209, "panic_fade_inc"});

      SDL_Rect panicRestoreBtn {safetyRect.x + 110, safetyRect.y + 50, safetyRect.w - 118, 22};
      drawPillToggle(panicRestoreBtn, project_.panicAutoRestore, "AUTO RESTORE ON", "AUTO RESTORE OFF");
      settingsBtns_.push_back({panicRestoreBtn, 212, "panic_restore_toggle"});

      SDL_Rect tcJamBtn {safetyRect.x + 8, safetyRect.y + 84, 130, 22};
      drawPillToggle(tcJamBtn, tcDeck.timecodeJamSyncEnabled, "TC JAM ON", "TC JAM OFF");
      settingsBtns_.push_back({tcJamBtn, 213, "tc_jam_toggle"});
      SDL_Rect tcFwDecBtn {safetyRect.x + 146, safetyRect.y + 84, 24, 22};
      SDL_Rect tcFwValRect {tcFwDecBtn.x + 28, tcFwDecBtn.y, 66, 22};
      SDL_Rect tcFwIncBtn {tcFwValRect.x + tcFwValRect.w + 4, tcFwDecBtn.y, 24, 22};
      Primitives::drawFramedPanel(controlRenderer_, tcFwDecBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "-", ink, tcFwDecBtn);
      settingsBtns_.push_back({tcFwDecBtn, 214, "tc_freewheel_dec"});
      std::ostringstream tcFreewheelLabel;
      tcFreewheelLabel << std::fixed << std::setprecision(1) << tcDeck.timecodeFreewheelSeconds << "s";
      Primitives::drawFramedPanel(controlRenderer_, tcFwValRect, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, tcFreewheelLabel.str(), ink, tcFwValRect);
      Primitives::drawFramedPanel(controlRenderer_, tcFwIncBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "+", ink, tcFwIncBtn);
      settingsBtns_.push_back({tcFwIncBtn, 215, "tc_freewheel_inc"});

      SDL_Rect cuesRect {rightCol.x + 8, safetyRect.y + safetyRect.h + 8, rightCol.w - 16, rightCol.h - (safetyRect.h + 24)};
      Primitives::drawFramedPanel(controlRenderer_, cuesRect, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(controlRenderer_, fontBase_, "CUE TOOLS", ink, cuesRect.x + 8, cuesRect.y + 6);
      SDL_Rect findPromptBtn {cuesRect.x + 8, cuesRect.y + 32, cuesRect.w - 16, 24};
      Primitives::drawFramedPanel(controlRenderer_, findPromptBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Find Cue...", ink, findPromptBtn);
      settingsBtns_.push_back({findPromptBtn, 216, "cue_find_prompt"});

      int navBtnW = std::max(60, (cuesRect.w - 16 - 12) / 3);
      SDL_Rect findNextBtn {cuesRect.x + 8, cuesRect.y + 62, navBtnW, 24};
      SDL_Rect findPrevBtn {findNextBtn.x + navBtnW + 6, findNextBtn.y, navBtnW, 24};
      SDL_Rect findTakeBtn {findPrevBtn.x + navBtnW + 6, findNextBtn.y, cuesRect.x + cuesRect.w - 8 - (findPrevBtn.x + navBtnW + 6), 24};
      Primitives::drawFramedPanel(controlRenderer_, findNextBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Next", ink, findNextBtn);
      settingsBtns_.push_back({findNextBtn, 217, "cue_find_next"});
      Primitives::drawFramedPanel(controlRenderer_, findPrevBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Prev", ink, findPrevBtn);
      settingsBtns_.push_back({findPrevBtn, 218, "cue_find_prev"});
      Primitives::drawFramedPanel(controlRenderer_, findTakeBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Find+Take", ink, findTakeBtn);
      settingsBtns_.push_back({findTakeBtn, 219, "cue_find_take"});

      SDL_Rect renumberBtn {cuesRect.x + 8, cuesRect.y + 92, cuesRect.w - 16, 24};
      Primitives::drawFramedPanel(controlRenderer_, renumberBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Renumber...", ink, renumberBtn);
      settingsBtns_.push_back({renumberBtn, 221, "cue_renumber"});
      SDL_Rect clearNumBtn {cuesRect.x + 8, cuesRect.y + 122, (cuesRect.w - 22) / 2, 24};
      SDL_Rect clearFindBtn {clearNumBtn.x + clearNumBtn.w + 6, clearNumBtn.y, cuesRect.x + cuesRect.w - 8 - (clearNumBtn.x + clearNumBtn.w + 6), 24};
      Primitives::drawFramedPanel(controlRenderer_, clearNumBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Clear Numbers", ink, clearNumBtn);
      settingsBtns_.push_back({clearNumBtn, 222, "cue_numbers_clear"});
      Primitives::drawFramedPanel(controlRenderer_, clearFindBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Clear Find", ink, clearFindBtn);
      settingsBtns_.push_back({clearFindBtn, 224, "cue_find_clear"});

      SDL_Rect sourceCueBtn {cuesRect.x + 8, cuesRect.y + 152, cuesRect.w - 16, 24};
      Primitives::drawFramedPanel(controlRenderer_, sourceCueBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Add Source Cue...", ink, sourceCueBtn);
      settingsBtns_.push_back({sourceCueBtn, 223, "cue_source_add"});

      std::string findStatus = "find: none";
      if (!lastCueFindToken_.empty() && !lastCueFindMatches_.empty()) {
        int cursor = std::clamp(lastCueFindCursor_, 0, static_cast<int>(lastCueFindMatches_.size()) - 1);
        findStatus = "find \"" + lastCueFindToken_ + "\" " + std::to_string(cursor + 1) + "/" + std::to_string(lastCueFindMatches_.size());
      }
      drawText(controlRenderer_, fontSmall_, ellipsizeToPixelWidth(fontSmall_, findStatus, cuesRect.w - 16), soft, cuesRect.x + 8, cuesRect.y + 182);
      const Cue* selectedCue = selectedCuePtr();
      std::string selectedStatus = "selected: none";
      if (selectedCue) {
        std::string cueNum = cueDisplayToken(*selectedCue, focusedDeck().selectedIndex);
        selectedStatus = "selected " + cueNum + "  " + selectedCue->name;
      }
      drawText(controlRenderer_, fontSmall_, ellipsizeToPixelWidth(fontSmall_, selectedStatus, cuesRect.w - 16), soft, cuesRect.x + 8, cuesRect.y + 200);

      int prefsTop = cuesRect.y + 224;
      int prefsHeight = (cuesRect.y + cuesRect.h - 8) - prefsTop;
      if (prefsHeight >= 96) {
        SDL_Rect prefsRect {cuesRect.x + 8, prefsTop, cuesRect.w - 16, prefsHeight};
        Primitives::drawFramedPanel(controlRenderer_, prefsRect, colorFromRgba(kShellInnerColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
        const Deck& prefDeck = focusedDeck();
        drawText(controlRenderer_, fontBase_, "PLAYLIST PREFS", ink, prefsRect.x + 8, prefsRect.y + 6);

        SDL_Rect prefsEditBtn {prefsRect.x + 8, prefsRect.y + 28, prefsRect.w - 16, 24};
        Primitives::drawFramedPanel(controlRenderer_, prefsEditBtn, colorFromRgba(kScreenMidColor),
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
        drawCenteredText(controlRenderer_, fontSmall_, "Edit Timebase/Start/Fade/Duration...", ink, prefsEditBtn);
        settingsBtns_.push_back({prefsEditBtn, kSettingsActionPlaylistPrefsEdit, "playlist_prefs_edit"});

        std::string prefSummary = "tc " + playlistTimebaseLabel(prefDeck.playlistTimebaseFps)
          + "  start " + formatTimecode(prefDeck.playlistStartOffsetSeconds, prefDeck.playlistTimebaseFps)
          + "  fade " + formatSeconds(prefDeck.playlistDefaultCueFadeSeconds)
          + "  still " + formatSeconds(prefDeck.playlistDefaultStillDurationSeconds);
        drawText(controlRenderer_, fontSmall_, ellipsizeToPixelWidth(fontSmall_, prefSummary, prefsRect.w - 16),
                 soft, prefsRect.x + 8, prefsRect.y + 56);

        int toggleY = prefsRect.y + 74;
        int toggleGap = 4;
        int toggleW = std::max(72, (prefsRect.w - 16 - toggleGap * 3) / 4);
        SDL_Rect loopT {prefsRect.x + 8, toggleY, toggleW, 22};
        SDL_Rect fadeInT {loopT.x + toggleW + toggleGap, toggleY, toggleW, 22};
        SDL_Rect fadeOutT {fadeInT.x + toggleW + toggleGap, toggleY, toggleW, 22};
        SDL_Rect audioT {fadeOutT.x + toggleW + toggleGap, toggleY, prefsRect.x + prefsRect.w - 8 - (fadeOutT.x + toggleW + toggleGap), 22};
        drawPillToggle(loopT, prefDeck.playlistDefaultLoop, "LOOP ON", "LOOP OFF");
        drawPillToggle(fadeInT, prefDeck.playlistDefaultFadeInEnabled, "FI ON", "FI OFF");
        drawPillToggle(fadeOutT, prefDeck.playlistDefaultFadeOutEnabled, "FO ON", "FO OFF");
        drawPillToggle(audioT, prefDeck.playlistDefaultAudioEnabled, "AUD ON", "AUD OFF");
        settingsBtns_.push_back({loopT, kSettingsActionPlaylistDefaultLoopToggle, "playlist_default_loop"});
        settingsBtns_.push_back({fadeInT, kSettingsActionPlaylistDefaultFadeInToggle, "playlist_default_fadein"});
        settingsBtns_.push_back({fadeOutT, kSettingsActionPlaylistDefaultFadeOutToggle, "playlist_default_fadeout"});
        settingsBtns_.push_back({audioT, kSettingsActionPlaylistDefaultAudioToggle, "playlist_default_audio"});

        int toggleY2 = toggleY + 26;
        int toggleW2 = std::max(100, (prefsRect.w - 16 - toggleGap * 2) / 3);
        SDL_Rect pauseBeginT {prefsRect.x + 8, toggleY2, toggleW2, 22};
        SDL_Rect pauseEndT {pauseBeginT.x + toggleW2 + toggleGap, toggleY2, toggleW2, 22};
        SDL_Rect nextTransT {pauseEndT.x + toggleW2 + toggleGap, toggleY2, prefsRect.x + prefsRect.w - 8 - (pauseEndT.x + toggleW2 + toggleGap), 22};
        drawPillToggle(pauseBeginT, prefDeck.playlistDefaultPauseAtBeginning, "P-BEGIN ON", "P-BEGIN OFF");
        drawPillToggle(pauseEndT, prefDeck.playlistDefaultPauseAtEnd, "P-END ON", "P-END OFF");
        drawPillToggle(nextTransT, prefDeck.playlistDefaultTransitionToNext, "NEXT X ON", "NEXT X OFF");
        settingsBtns_.push_back({pauseBeginT, kSettingsActionPlaylistDefaultPauseBeginToggle, "playlist_default_pausebegin"});
        settingsBtns_.push_back({pauseEndT, kSettingsActionPlaylistDefaultPauseEndToggle, "playlist_default_pauseend"});
        settingsBtns_.push_back({nextTransT, kSettingsActionPlaylistDefaultNextTransitionToggle, "playlist_default_nexttrans"});
      }

    } else if (settingsTab_ == 1) {
      // MIDI tab
      drawText(controlRenderer_, fontSmall_, "ALSA MIDI Input", ink, cx, cy);
      SDL_Rect midiEnBtn {cx, cy + 22, 100, 26};
      Primitives::drawFramedPanel(controlRenderer_, midiEnBtn, midiEnabled_ ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, midiEnabled_ ? "Enabled" : "Disabled",
                       midiEnabled_ ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor), midiEnBtn);
      settingsBtns_.push_back({midiEnBtn, 210, "midi_toggle"});

      drawText(controlRenderer_, fontSmall_, "MIDI port: " + (midiDeviceName_.empty() ? "(auto)" : midiDeviceName_), soft, cx, cy + 60);
      SDL_Rect midiPortBtn {cx, cy + 78, 200, 26};
      Primitives::drawFramedPanel(controlRenderer_, midiPortBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Set port (e.g. 20:0)...", ink, midiPortBtn);
      settingsBtns_.push_back({midiPortBtn, 211, "midi_port"});

      drawText(controlRenderer_, fontSmall_, "MIDI mappings:", ink, cx, cy + 120);
      drawText(controlRenderer_, fontSmall_, "Note 0-127 -> Trigger cue at that index in focused deck", soft, cx, cy + 138);
      drawText(controlRenderer_, fontSmall_, "CC 7 (Volume) -> Master volume", soft, cx, cy + 154);
      drawText(controlRenderer_, fontSmall_, "CC 20 -> Playback speed (0-127 = 0.5x-2x)", soft, cx, cy + 170);
      drawText(controlRenderer_, fontSmall_, "MMC Play/Stop/Goto -> Transport control", soft, cx, cy + 186);
      drawText(controlRenderer_, fontSmall_, "MSC Trigger -> cue by cue number", soft, cx, cy + 202);

    } else if (settingsTab_ == 2) {
      // OSC/Net tab
      drawText(controlRenderer_, fontSmall_, "Companion / OSC port:", ink, cx, cy);
      drawText(controlRenderer_, fontSmall_, std::to_string(companionPort_), soft, cx, cy + 18);
      SDL_Rect portBtn {cx, cy + 40, 160, 26};
      Primitives::drawFramedPanel(controlRenderer_, portBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Change port...", ink, portBtn);
      settingsBtns_.push_back({portBtn, 220, "osc_port"});

      auto drawPill = [&](const SDL_Rect& rect, bool active, const std::string& onLabel, const std::string& offLabel, int action) {
        Primitives::drawFramedPanel(controlRenderer_, rect,
                        active ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                        colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
        drawCenteredText(controlRenderer_, fontSmall_, active ? onLabel : offLabel,
                         active ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor), rect);
        settingsBtns_.push_back({rect, action, onLabel});
      };

      int rowY = cy + 78;
      SDL_Rect queryRect {cx, rowY, content.w - 24, 92};
      Primitives::drawFramedPanel(controlRenderer_, queryRect, colorFromRgba(kShellInnerColor),
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawText(controlRenderer_, fontBase_, "OSC QUERY", ink, queryRect.x + 8, queryRect.y + 6);
#if defined(_WIN32)
      std::string queryStatus = project_.oscQueryEnabled ? "unsupported" : "off";
#else
      std::string queryStatus = project_.oscQueryEnabled ? (oscQueryReady_ ? "running" : "error") : "off";
#endif
      drawText(controlRenderer_, fontSmall_,
               "HTTP " + std::to_string(project_.oscQueryPort) + "  status: " + queryStatus,
               soft, queryRect.x + 8, queryRect.y + 30);
#ifndef _WIN32
      drawText(controlRenderer_, fontSmall_,
               "URL: http://127.0.0.1:" + std::to_string(project_.oscQueryPort) + "/oscquery.json",
               soft, queryRect.x + 8, queryRect.y + 48);
#else
      drawText(controlRenderer_, fontSmall_, "OSC Query server is currently disabled on this build.", soft,
               queryRect.x + 8, queryRect.y + 48);
#endif
      SDL_Rect queryToggle {queryRect.x + 8, queryRect.y + 64, 140, 22};
      SDL_Rect queryPortBtn {queryToggle.x + queryToggle.w + 8, queryToggle.y, 170, 22};
      drawPill(queryToggle, project_.oscQueryEnabled, "QUERY ON", "QUERY OFF", kSettingsActionOscQueryToggle);
      Primitives::drawFramedPanel(controlRenderer_, queryPortBtn, colorFromRgba(kScreenMidColor),
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Set HTTP Port...", ink, queryPortBtn);
      settingsBtns_.push_back({queryPortBtn, kSettingsActionOscQueryPortPrompt, "osc_query_port"});

      rowY += queryRect.h + 8;
      SDL_Rect feedbackRect {cx, rowY, content.w - 24, 74};
      Primitives::drawFramedPanel(controlRenderer_, feedbackRect, colorFromRgba(kShellInnerColor),
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawText(controlRenderer_, fontBase_, "OSC FEEDBACK", ink, feedbackRect.x + 8, feedbackRect.y + 6);
      drawText(controlRenderer_, fontSmall_,
               std::string("mirror canonical values: ") + (project_.oscFeedbackMirrorEnabled ? "on" : "off")
               + "  rate: " + std::to_string(project_.oscFeedbackRateMs) + " ms",
               soft, feedbackRect.x + 8, feedbackRect.y + 30);
      SDL_Rect fbToggle {feedbackRect.x + 8, feedbackRect.y + 48, 170, 22};
      SDL_Rect fbRateBtn {fbToggle.x + fbToggle.w + 8, fbToggle.y, 170, 22};
      drawPill(fbToggle, project_.oscFeedbackMirrorEnabled, "MIRROR ON", "MIRROR OFF", kSettingsActionOscFeedbackMirrorToggle);
      Primitives::drawFramedPanel(controlRenderer_, fbRateBtn, colorFromRgba(kScreenMidColor),
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Set Mirror Rate...", ink, fbRateBtn);
      settingsBtns_.push_back({fbRateBtn, kSettingsActionOscFeedbackRatePrompt, "osc_feedback_rate"});

      rowY += feedbackRect.h + 8;
      drawText(controlRenderer_, fontSmall_, "HyperDeck emulation: port 9992 (always on)", soft, cx, rowY);
      drawText(controlRenderer_, fontSmall_, "OSC subscribe: send /playboy/subscribe from your OSC app", soft, cx, rowY + 20);
      drawText(controlRenderer_, fontSmall_, "NDI: configured per output (N key)", soft, cx, rowY + 40);

    } else if (settingsTab_ == 3) {
      // Video Outputs tab (simplified)
      const OutputTarget& outputTarget = focusedOutput();
      int focusedOutputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
      auto [nativeW, nativeH] = displayNativeRenderSize(outputDisplayIndex(project_.focusedOutputIndex));
      auto [targetW, targetH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
      std::string outputTypeLabel = normalizeOutputType(outputTarget.outputType);
      std::string mirrorLabel = outputTarget.mirrorSourceOutputIndex >= 0
        ? outputLabel(outputTarget.mirrorSourceOutputIndex)
        : "off";
      std::string streamProtocol = normalizeOutputStreamProtocol(outputTarget.streamProtocol);
      std::string streamUrl = trim(outputTarget.streamUrl);
      if (streamUrl.empty()) {
        streamUrl = defaultOutputStreamUrl(streamProtocol, focusedOutputIndex);
      }
      int outputAlphaPct = static_cast<int>(std::lround(std::clamp(outputTarget.outputAlpha, 0.0f, 1.0f) * 100.0f));
      int outputDelayMs = std::clamp(outputTarget.outputDelayMs, 0, 5000);
      std::string outputColorSpace = normalizeOutputColorSpace(outputTarget.outputColorSpace);
      std::string outputLayoutMode = normalizeOutputLayoutMode(outputTarget.outputLayoutMode);
      int outputOrientation = normalizeOutputOrientationDegrees(outputTarget.outputOrientationDegrees);
      std::string ndiSource = trim(outputTarget.ndiSourceName).empty()
        ? defaultOutputNdiSourceName(outputTarget, focusedOutputIndex)
        : outputTarget.ndiSourceName;
      bool anyOutputTestCardsOn = false;
      bool anyOutputTestCardsOff = false;
      for (const auto& out : project_.outputs) {
        if (out.outputTestCardEnabled) {
          anyOutputTestCardsOn = true;
        } else {
          anyOutputTestCardsOff = true;
        }
      }
      auto drawActionBtn = [&](const SDL_Rect& rect, const std::string& label, int action, bool active = false) {
        SDL_Color fill = active ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor);
        SDL_Color txt = active ? colorFromRgba(kScreenLightColor) : ink;
        Primitives::drawFramedPanel(controlRenderer_, rect, fill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
        drawCenteredText(controlRenderer_, fontSmall_, label, txt, rect);
        settingsBtns_.push_back({rect, action, label});
      };

      std::vector<std::pair<int, int>> focusedFlowDecks;
      for (const auto& assignment : project_.layerAssignments) {
        if (!assignment.enabled) {
          continue;
        }
        if (resolveAssignmentOutputIndex(assignment) != focusedOutputIndex) {
          continue;
        }
        focusedFlowDecks.emplace_back(
          std::clamp(assignment.layerIndex, 0, 255),
          std::clamp(assignment.deckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1)));
      }
      std::sort(focusedFlowDecks.begin(), focusedFlowDecks.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) {
          return a.first < b.first;
        }
        return a.second < b.second;
      });
      std::string flowLine = "Signal flow: ";
      if (focusedFlowDecks.empty()) {
        flowLine += "no deck links";
      } else {
        for (size_t i = 0; i < focusedFlowDecks.size(); ++i) {
          if (i > 0) {
            flowLine += " + ";
          }
          flowLine += deckLabel(focusedFlowDecks[i].second);
          flowLine += " (L";
          flowLine += std::to_string(focusedFlowDecks[i].first);
          flowLine += ")";
        }
      }
      flowLine += " -> ";
      flowLine += outputLabel(focusedOutputIndex);
      flowLine += " -> Display ";
      flowLine += std::to_string(outputDisplayIndex(project_.focusedOutputIndex) + 1);

      SDL_Rect summaryRect {cx, cy, content.w - 24, 156};
      Primitives::drawFramedPanel(controlRenderer_, summaryRect,
                      colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawText(controlRenderer_, fontBase_, "OUTPUT STATUS", ink, summaryRect.x + 8, summaryRect.y + 8);
      drawText(controlRenderer_, fontSmall_,
               "Focused: " + outputLabel(focusedOutputIndex) + "  (" + std::to_string(focusedOutputIndex + 1) + "/" + std::to_string(project_.outputs.size()) + ")",
               ink, summaryRect.x + 8, summaryRect.y + 32);
      drawText(controlRenderer_, fontSmall_,
               "Type: " + outputTypeLabel + "  enabled: " + std::string(outputTarget.enabled ? "on" : "off")
               + "  host: " + deckLabel(outputTarget.hostDeckIndex) + "  display: " + currentDisplayLabel(),
               soft, summaryRect.x + 8, summaryRect.y + 50);
      drawText(controlRenderer_, fontSmall_,
               "Raster: " + std::to_string(targetW) + "x" + std::to_string(targetH)
               + "  native: " + std::to_string(nativeW) + "x" + std::to_string(nativeH)
               + "  refresh: " + outputRefreshRateLabel() + "  depth: " + outputBitDepthModeLabel(),
               soft, summaryRect.x + 8, summaryRect.y + 68);
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, flowLine, summaryRect.w - 16),
               soft, summaryRect.x + 8, summaryRect.y + 86);
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_,
                 "NDI: " + std::string(outputTarget.ndiEnabled ? "on" : "off")
                 + "  fill: " + ndiSource
                 + "  key: " + std::string(outputTarget.ndiKeyEnabled ? "on" : "off"),
                 summaryRect.w - 16),
               soft, summaryRect.x + 8, summaryRect.y + 104);
      drawText(controlRenderer_, fontSmall_,
               ellipsizeToPixelWidth(fontSmall_, "Stream: " + streamProtocol + "  " + streamUrl + "  mirror: " + mirrorLabel, summaryRect.w - 16),
               soft, summaryRect.x + 8, summaryRect.y + 122);
      drawText(controlRenderer_, fontSmall_,
               "FX: alpha " + std::to_string(outputAlphaPct) + "%  delay " + std::to_string(outputDelayMs)
               + " ms  overlay: " + std::string(outputTarget.outputTimeOverlayEnabled ? "on" : "off")
               + "  color: " + toUpper(outputColorSpace)
               + "  layout: " + outputLayoutMode
               + "  rot: " + outputOrientationLabel(outputOrientation)
               + "  test: " + std::string(outputTarget.outputTestCardEnabled ? "on" : "off"),
               soft, summaryRect.x + 8, summaryRect.y + 140);

      int rowY = summaryRect.y + summaryRect.h + 8;
      int leftW = content.w - 24;
      int gap = 6;

      int row1W = (leftW - gap * 4) / 5;
      SDL_Rect outputPrevBtn {cx, rowY, row1W, 26};
      SDL_Rect outputNextBtn {outputPrevBtn.x + row1W + gap, rowY, row1W, 26};
      SDL_Rect outputAddBtn {outputNextBtn.x + row1W + gap, rowY, row1W, 26};
      SDL_Rect outputRemoveBtn {outputAddBtn.x + row1W + gap, rowY, row1W, 26};
      SDL_Rect outputEnableBtn {outputRemoveBtn.x + row1W + gap, rowY, leftW - (outputRemoveBtn.x + row1W + gap - cx), 26};
      drawActionBtn(outputPrevBtn, "Prev Output", 250);
      drawActionBtn(outputNextBtn, "Next Output", 251);
      drawActionBtn(outputAddBtn, "Add Output", 252);
      drawActionBtn(outputRemoveBtn, "Remove", kSettingsActionOutputRemove, project_.outputs.size() <= 1);
      drawActionBtn(outputEnableBtn, outputTarget.enabled ? "Enabled" : "Disabled", kSettingsActionOutputToggle, outputTarget.enabled);

      rowY += 32;
      int createW = (leftW - gap) / 2;
      drawActionBtn({cx, rowY, createW, 26}, "Create Standard", 275);
      drawActionBtn({cx + createW + gap, rowY, leftW - createW - gap, 26}, "Create Stream", 276);

      rowY += 32;
      int row2W = (leftW - gap * 5) / 6;
      drawActionBtn({cx, rowY, row2W, 26}, "Host Deck", 254);
      drawActionBtn({cx + (row2W + gap), rowY, row2W, 26}, "Set Window", 281, outputTypeLabel != "stream");
      drawActionBtn({cx + (row2W + gap) * 2, rowY, row2W, 26}, "Set Stream", 282, outputTypeLabel == "stream");
      drawActionBtn({cx + (row2W + gap) * 3, rowY, row2W, 26}, "Fullscreen", 236);
      drawActionBtn({cx + (row2W + gap) * 4, rowY, row2W, 26}, "Size To Display", 235);
      drawActionBtn({cx + (row2W + gap) * 5, rowY, leftW - (row2W + gap) * 5, 26},
                    "Display Native", 230, project_.outputFollowDisplay);

      rowY += 32;
      int modeW = (leftW - gap * 4) / 5;
      drawActionBtn({cx, rowY, modeW, 26}, "Span", kSettingsActionOutputLayoutSpan, outputLayoutMode == "span");
      drawActionBtn({cx + (modeW + gap), rowY, modeW, 26}, "Duplicate", kSettingsActionOutputLayoutDuplicate, outputLayoutMode == "duplicate");
      drawActionBtn({cx + (modeW + gap) * 2, rowY, modeW, 26},
                    "Rotate " + outputOrientationLabel(outputOrientation),
                    kSettingsActionOutputOrientationCycle, outputOrientation != 0);
      drawActionBtn({cx + (modeW + gap) * 3, rowY, modeW, 26},
                    outputTarget.outputTestCardEnabled ? "Test Card ON" : "Test Card OFF",
                    kSettingsActionOutputTestCardToggle, outputTarget.outputTestCardEnabled);
      drawActionBtn({cx + (modeW + gap) * 4, rowY, leftW - (modeW + gap) * 4, 26},
                    anyOutputTestCardsOff ? "All Cards ON" : "All Cards OFF",
                    kSettingsActionOutputTestCardAllToggle,
                    anyOutputTestCardsOn && !anyOutputTestCardsOff);

      int displayCount = SDL_GetNumVideoDisplays();
      int focusedDisplayIndex = displayCount > 0
        ? std::clamp(outputDisplayIndex(project_.focusedOutputIndex), 0, displayCount - 1)
        : 0;
      std::string displayLabel = displayCount <= 0
        ? "Display none"
        : ("Display " + std::to_string(focusedDisplayIndex + 1));
      if (displayCount > 0) {
        const char* displayName = SDL_GetDisplayName(focusedDisplayIndex);
        if (displayName && *displayName) {
          displayLabel += ": ";
          displayLabel += displayName;
        }
      }

      rowY += 32;
      SDL_Rect displayPrevBtn {cx, rowY, 72, 26};
      SDL_Rect displayLabelRect {displayPrevBtn.x + displayPrevBtn.w + gap, rowY, leftW - 72 - 72 - 90 - gap * 3, 26};
      SDL_Rect displayNextBtn {displayLabelRect.x + displayLabelRect.w + gap, rowY, 72, 26};
      SDL_Rect displayRescanBtn {displayNextBtn.x + displayNextBtn.w + gap, rowY, leftW - (displayNextBtn.x + displayNextBtn.w + gap - cx), 26};
      drawActionBtn(displayPrevBtn, "Disp -", kSettingsActionOutputDisplayPrev);
      Primitives::drawFramedPanel(controlRenderer_, displayLabelRect, colorFromRgba(kShellInnerColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, ellipsizeToPixelWidth(fontSmall_, displayLabel, displayLabelRect.w - 8), ink, displayLabelRect);
      drawActionBtn(displayNextBtn, "Disp +", kSettingsActionOutputDisplayNext);
      drawActionBtn(displayRescanBtn, "Rescan Displays", kSettingsActionOutputDisplayRescan);

      rowY += 32;
      auto drawPreset = [&](const SDL_Rect& rect, const std::string& label, int action, int w, int h) {
        bool active = !project_.outputFollowDisplay
          && project_.outputRenderWidth == w
          && project_.outputRenderHeight == h;
        drawActionBtn(rect, label, action, active);
      };
      int presetW = (leftW - gap * 5) / 6;
      drawPreset({cx, rowY, presetW, 26}, "720p", 231, 1280, 720);
      drawPreset({cx + (presetW + gap), rowY, presetW, 26}, "1080p", 232, 1920, 1080);
      drawPreset({cx + (presetW + gap) * 2, rowY, presetW, 26}, "1440p", 233, 2560, 1440);
      drawPreset({cx + (presetW + gap) * 3, rowY, presetW, 26}, "4K", 234, 3840, 2160);
      drawActionBtn({cx + (presetW + gap) * 4, rowY, presetW, 26}, "Custom", 237);
      drawActionBtn({cx + (presetW + gap) * 5, rowY, leftW - (presetW + gap) * 5, 26}, "Set Hz...", 241);

      rowY += 32;
      int streamW = (leftW - gap * 4) / 5;
      drawActionBtn({cx, rowY, streamW, 26}, outputTarget.streamEnabled ? "Stream ON" : "Stream OFF", 255, outputTarget.streamEnabled);
      drawActionBtn({cx + (streamW + gap), rowY, streamW, 26}, toUpper(streamProtocol), 256);
      drawActionBtn({cx + (streamW + gap) * 2, rowY, streamW, 26}, "Stream URL...", 257);
      drawActionBtn({cx + (streamW + gap) * 3, rowY, streamW, 26}, "Bitrate", 258);
      drawActionBtn({cx + (streamW + gap) * 4, rowY, leftW - (streamW + gap) * 4, 26}, "Mirror", 260, outputTarget.mirrorSourceOutputIndex >= 0);

      rowY += 32;
      int ndiW = (leftW - gap * 3) / 4;
      drawActionBtn({cx, rowY, ndiW, 26}, outputTarget.ndiEnabled ? "NDI ON" : "NDI OFF", 271, outputTarget.ndiEnabled);
      drawActionBtn({cx + (ndiW + gap), rowY, ndiW, 26}, "NDI Name...", 272);
      drawActionBtn({cx + (ndiW + gap) * 2, rowY, ndiW, 26}, outputTarget.ndiKeyEnabled ? "NDI Key ON" : "NDI Key OFF", 273, outputTarget.ndiKeyEnabled);
      drawActionBtn({cx + (ndiW + gap) * 3, rowY, leftW - (ndiW + gap) * 3, 26}, "NDI Key Name...", 274);

      rowY += 32;
      int fxW = (leftW - gap * 4) / 5;
      drawActionBtn({cx, rowY, fxW, 26},
                    "Overlay " + std::string(outputTarget.outputTimeOverlayEnabled ? "ON" : "OFF"),
                    kSettingsActionOutputOverlayToggle, outputTarget.outputTimeOverlayEnabled);
      drawActionBtn({cx + (fxW + gap), rowY, fxW, 26},
                    "Alpha " + std::to_string(outputAlphaPct) + "%",
                    kSettingsActionOutputAlphaPrompt);
      drawActionBtn({cx + (fxW + gap) * 2, rowY, fxW, 26},
                    "Delay " + std::to_string(outputDelayMs) + "ms",
                    kSettingsActionOutputDelayPrompt, outputDelayMs > 0);
      drawActionBtn({cx + (fxW + gap) * 3, rowY, fxW, 26},
                    "Color " + toUpper(outputColorSpace),
                    kSettingsActionOutputColorSpaceCycle, outputColorSpace != "auto");
      drawActionBtn({cx + (fxW + gap) * 4, rowY, leftW - (fxW + gap) * 4, 26},
                    "Delay +100",
                    kSettingsActionOutputDelayInc);

      rowY += 34;
      SDL_Rect advancedBtn {cx, rowY, 196, 26};
      drawActionBtn(advancedBtn,
                    videoOutputsAdvanced_ ? "Hide Advanced" : "Show Advanced",
                    kSettingsActionOutputAdvancedToggle,
                    videoOutputsAdvanced_);
      SDL_Rect advancedHint {advancedBtn.x + advancedBtn.w + gap, rowY,
                             leftW - advancedBtn.w - gap, 26};
      Primitives::drawFramedPanel(controlRenderer_, advancedHint, colorFromRgba(kShellInnerColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawText(controlRenderer_, fontSmall_, "advanced: refresh, depth, canvas, warp, output fx",
               soft, advancedHint.x + 8, advancedHint.y + 7);

      rowY += 32;
      if (videoOutputsAdvanced_) {
        int advW = (leftW - gap * 6) / 7;
        drawActionBtn({cx, rowY, advW, 26}, "Hz Auto", 238, project_.outputRefreshRateHz <= 0.0);
        drawActionBtn({cx + (advW + gap), rowY, advW, 26}, "Hz -", 239);
        drawActionBtn({cx + (advW + gap) * 2, rowY, advW, 26}, "Hz +", 240);
        drawActionBtn({cx + (advW + gap) * 3, rowY, advW, 26}, "Depth Auto", 242, project_.outputBitDepth == 0);
        drawActionBtn({cx + (advW + gap) * 4, rowY, advW, 26}, "Depth 8", 243, project_.outputBitDepth == 8);
        drawActionBtn({cx + (advW + gap) * 5, rowY, advW, 26}, "Depth 10", 244, project_.outputBitDepth == 10);
        drawActionBtn({cx + (advW + gap) * 6, rowY, leftW - (advW + gap) * 6, 26}, "Canvas WxH...", 247);

        rowY += 32;
        const Deck& focused = focusedDeck();
        int canvasW = (leftW - gap * 4) / 5;
        drawActionBtn({cx, rowY, canvasW, 26}, "Canvas Off", 245, !project_.outputCanvasEnabled);
        drawActionBtn({cx + (canvasW + gap), rowY, canvasW, 26}, "Canvas On", 246, project_.outputCanvasEnabled);
        drawActionBtn({cx + (canvasW + gap) * 2, rowY, canvasW, 26}, "View XY...", 248);
        drawActionBtn({cx + (canvasW + gap) * 3, rowY, canvasW, 26},
                      focused.warpEnabled ? "Warp On" : "Warp Off", 249, focused.warpEnabled);
        SDL_Rect routingStripTag {cx + (canvasW + gap) * 4, rowY, leftW - (canvasW + gap) * 4, 26};
        Primitives::drawFramedPanel(controlRenderer_, routingStripTag, colorFromRgba(kShellInnerColor),
                        colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
        drawCenteredText(controlRenderer_, fontSmall_, "Routing In Main Strip", ink, routingStripTag);
        rowY += 36;
      } else {
        rowY += 4;
      }

      SDL_Rect routingTable {cx, rowY, leftW, std::max(72, (content.y + content.h - 10) - rowY)};
      Primitives::drawFramedPanel(controlRenderer_, routingTable,
                      colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(controlRenderer_, fontBase_, "ROUTING", ink, routingTable.x + 8, routingTable.y + 6);
      drawText(controlRenderer_, fontSmall_, "Deck -> Output -> Layer (inline edit)", soft, routingTable.x + 112, routingTable.y + 8);

      SDL_Rect tableHeader {routingTable.x + 6, routingTable.y + 28, routingTable.w - 12, 22};
      Primitives::drawFramedPanel(controlRenderer_, tableHeader, colorFromRgba(kShellInnerColor),
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      int colDeckX = tableHeader.x + 8;
      int colOutputX = tableHeader.x + tableHeader.w / 3;
      int colLayerX = tableHeader.x + tableHeader.w * 2 / 3 - 42;
      int colAssignX = tableHeader.x + tableHeader.w - 86;
      drawText(controlRenderer_, fontSmall_, "Deck", ink, colDeckX, tableHeader.y + 4);
      drawText(controlRenderer_, fontSmall_, "Output", ink, colOutputX, tableHeader.y + 4);
      drawText(controlRenderer_, fontSmall_, "Layer", ink, colLayerX, tableHeader.y + 4);
      drawText(controlRenderer_, fontSmall_, "Assigned", ink, colAssignX, tableHeader.y + 4);

      int rowYTable = tableHeader.y + tableHeader.h + 4;
      int rowH = 24;
      int rowGap = 3;
      int visibleRows = std::max(1, (routingTable.y + routingTable.h - 6 - rowYTable) / (rowH + rowGap));
      int deckCount = static_cast<int>(project_.decks.size());
      int focusedDeck = std::clamp(project_.focusedDeckIndex, 0, std::max(0, deckCount - 1));
      int deckStart = 0;
      if (deckCount > visibleRows) {
        deckStart = std::clamp(focusedDeck - visibleRows / 2, 0, deckCount - visibleRows);
      }
      for (int slot = 0; slot < visibleRows; ++slot) {
        int deckIndex = deckStart + slot;
        if (deckIndex >= deckCount) {
          break;
        }
        SDL_Rect rowRect {routingTable.x + 6, rowYTable + slot * (rowH + rowGap), routingTable.w - 12, rowH};
        bool focusedRow = deckIndex == project_.focusedDeckIndex;
        SDL_Color rowFill = focusedRow ? colorFromRgba(kScreenMidColor) : colorFromRgba(kShellInnerColor);
        Primitives::drawFramedPanel(controlRenderer_, rowRect, rowFill,
                                    colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        SDL_Color rowInk = focusedRow ? colorFromRgba(kScreenDeepColor) : soft;
        drawText(controlRenderer_, fontSmall_, deckLabel(deckIndex), rowInk, rowRect.x + 6, rowRect.y + 5);
        settingsBtns_.push_back({SDL_Rect {rowRect.x + 2, rowRect.y + 2, std::max(40, colOutputX - rowRect.x - 4), rowRect.h - 4},
                                 kSettingsActionRoutingDeckFocusBase + deckIndex, "route_focus_deck"});

        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        int routeOutput = primaryOut
          ? *primaryOut
          : (outputCount > 0 ? std::clamp(project_.focusedOutputIndex, 0, outputCount - 1) : -1);
        auto assignmentIndex = (routeOutput >= 0 && outputCount > 0)
          ? assignmentIndexForDeckOutput(deckIndex, routeOutput)
          : std::nullopt;
        bool assigned = assignmentIndex.has_value();
        int layerIndex = assigned ? std::clamp(project_.layerAssignments[*assignmentIndex].layerIndex, 0, 255) : 0;

        SDL_Rect outPrev {colOutputX, rowRect.y + 3, 18, rowRect.h - 6};
        SDL_Rect outVal {outPrev.x + outPrev.w + 2, rowRect.y + 3, 52, rowRect.h - 6};
        SDL_Rect outNext {outVal.x + outVal.w + 2, rowRect.y + 3, 18, rowRect.h - 6};
        drawActionBtn(outPrev, "<", kSettingsActionRoutingTableOutputPrevBase + deckIndex);
        drawActionBtn(outVal, routeOutput >= 0 ? outputLabel(routeOutput) : "--",
                      kSettingsActionRoutingOutputFocusBase + std::max(0, routeOutput),
                      routeOutput == project_.focusedOutputIndex);
        drawActionBtn(outNext, ">", kSettingsActionRoutingTableOutputNextBase + deckIndex);

        SDL_Rect layerDec {colLayerX, rowRect.y + 3, 18, rowRect.h - 6};
        SDL_Rect layerVal {layerDec.x + layerDec.w + 2, rowRect.y + 3, 44, rowRect.h - 6};
        SDL_Rect layerInc {layerVal.x + layerVal.w + 2, rowRect.y + 3, 18, rowRect.h - 6};
        drawActionBtn(layerDec, "-", kSettingsActionRoutingTableLayerDecBase + deckIndex);
        drawActionBtn(layerVal, assigned ? (layerIndex <= 0 ? "BG" : ("L" + std::to_string(layerIndex))) : "--",
                      kSettingsActionRoutingTableAssignToggleBase + deckIndex, assigned);
        drawActionBtn(layerInc, "+", kSettingsActionRoutingTableLayerIncBase + deckIndex);

        SDL_Rect assignBtn {colAssignX, rowRect.y + 3, rowRect.x + rowRect.w - colAssignX - 4, rowRect.h - 6};
        drawActionBtn(assignBtn, assigned ? "UNLINK" : "LINK",
                      kSettingsActionRoutingTableAssignToggleBase + deckIndex, assigned);
      }

    } else if (settingsTab_ == 4) {
      // About tab
      SDL_Rect logoRect {cx, cy, content.w - 24, 116};
      Primitives::drawFramedPanel(controlRenderer_, logoRect, colorFromRgba(kShellInnerColor),
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      TTF_Font* titleFont = fontPixel_ ? fontPixel_ : fontLarge_;
      drawText(controlRenderer_, titleFont, "DECKBOY", ink, logoRect.x + 14, logoRect.y + 14);
      drawText(controlRenderer_, fontBase_, "dot-matrix cue deck", soft, logoRect.x + 16, logoRect.y + 58);
      drawText(controlRenderer_, fontSmall_, "build: " + std::string(kAppModelLabel), soft, logoRect.x + 16, logoRect.y + 82);

      SDL_Rect infoRect {cx, logoRect.y + logoRect.h + 8, content.w - 24, content.h - logoRect.h - 18};
      Primitives::drawFramedPanel(controlRenderer_, infoRect, colorFromRgba(kScreenLightColor),
                                  colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawText(controlRenderer_, fontBase_, "RUNTIME", ink, infoRect.x + 8, infoRect.y + 8);
      drawText(controlRenderer_, fontSmall_, "Companion port: " + std::to_string(companionPort_), soft, infoRect.x + 8, infoRect.y + 32);
      drawText(controlRenderer_, fontSmall_, "HyperDeck port: 9992", soft, infoRect.x + 8, infoRect.y + 48);
      drawText(controlRenderer_, fontSmall_, "UI mascot/sprite art is disabled in live control panels.", soft, infoRect.x + 8, infoRect.y + 64);
      drawText(controlRenderer_, fontSmall_, "Core transport keys: Enter Take | Space Play/Pause | S Stop | C Clear", soft, infoRect.x + 8, infoRect.y + 88);
    }
  }

  void handleSettingsClick(int mx, int my) {
    // Check close button
    if (pointInRect(mx, my, settingsCloseBtn_)) {
      settingsOpen_ = false;
      return;
    }
    for (const auto& sb : settingsBtns_) {
      if (!pointInRect(mx, my, sb.rect)) continue;
      if (sb.action >= 100 && sb.action <= 104) {
        // Tab switch
        settingsTab_ = sb.action - 100;
      } else if (sb.action == 200) {
        // Change audio device
        auto dev = pickTextInput("Audio output device", "device name or empty for default", focusedDeck().audioOutputDeviceName);
        if (dev) {
          focusedDeckMutable().audioOutputDeviceName = *dev;
          markProjectDirty();
        }
      } else if (sb.action == 201) {
        project_.uiSoundsEnabled = !project_.uiSoundsEnabled;
        markProjectDirty();
      } else if (sb.action == 202) {
        project_.uiTransitionsEnabled = !project_.uiTransitionsEnabled;
        markProjectDirty();
      } else if (sb.action == 203) {
        toggleJumpMode();
      } else if (sb.action == 204) {
        setJumpTransitionEnabled(!project_.jumpTransitionEnabled);
      } else if (sb.action == 205) {
        cyclePanicProfile(-1);
      } else if (sb.action == 206) {
        cyclePanicProfile(1);
      } else if (sb.action == 207) {
        triggerPanicProfile();
      } else if (sb.action == 208) {
        adjustPanicFadeSeconds(-0.1);
      } else if (sb.action == 209) {
        adjustPanicFadeSeconds(0.1);
      } else if (sb.action == 212) {
        setPanicAutoRestoreEnabled(!project_.panicAutoRestore);
      } else if (sb.action == 213) {
        setTimecodeJamSyncEnabled(!focusedDeck().timecodeJamSyncEnabled);
      } else if (sb.action == 214) {
        setTimecodeFreewheelSeconds(focusedDeck().timecodeFreewheelSeconds - 0.1);
      } else if (sb.action == 215) {
        setTimecodeFreewheelSeconds(focusedDeck().timecodeFreewheelSeconds + 0.1);
      } else if (sb.action == 216) {
        auto token = pickTextInput("Find Cue", "Cue number, cue id, or name contains:", lastCueFindToken_);
        if (token) {
          findCueToken(*token, 1, false);
        }
      } else if (sb.action == 217) {
        if (!lastCueFindToken_.empty()) {
          findCueToken(lastCueFindToken_, 1, false);
        } else {
          triggerToast("find: run Find Cue first");
        }
      } else if (sb.action == 218) {
        if (!lastCueFindToken_.empty()) {
          findCueToken(lastCueFindToken_, -1, false);
        } else {
          triggerToast("find: run Find Cue first");
        }
      } else if (sb.action == 219) {
        if (!lastCueFindToken_.empty()) {
          findCueToken(lastCueFindToken_, 1, true);
        } else {
          auto token = pickTextInput("Find+Take Cue", "Cue number, cue id, or name contains:", "");
          if (token) {
            findCueToken(*token, 1, true);
          }
        }
      } else if (sb.action == 221) {
        auto input = pickTextInput("Renumber Cues", "Prefix and optional start (example: Q 100)", "");
        if (input) {
          std::string trimmedInput = trim(*input);
          if (trimmedInput.empty()) {
            renumberFocusedDeckCueNumbers("", 1);
          } else {
            auto parts = splitWhitespace(trimmedInput);
            std::string prefix = parts.empty() ? "" : parts[0];
            int start = 1;
            bool numericOnly = false;
            if (parts.size() == 1) {
              try {
                start = std::stoi(parts[0]);
                prefix.clear();
                numericOnly = true;
              } catch (...) {
              }
            }
            if (!numericOnly && parts.size() > 1) {
              try {
                start = std::stoi(parts[1]);
              } catch (...) {
              }
            }
            renumberFocusedDeckCueNumbers(prefix, start);
          }
        }
      } else if (sb.action == 222) {
        clearFocusedDeckCueNumbers();
      } else if (sb.action == 223) {
        addSourceCueFromMenu();
      } else if (sb.action == 224) {
        clearCueFindState();
        triggerToast("find cleared");
      } else if (sb.action == kSettingsActionPlaylistPrefsEdit) {
        editFocusedDeckPlaylistPreferences();
      } else if (sb.action == kSettingsActionPlaylistDefaultLoopToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultLoop = !deck.playlistDefaultLoop;
        triggerToast(std::string("new cues loop: ") + (deck.playlistDefaultLoop ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultFadeInToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultFadeInEnabled = !deck.playlistDefaultFadeInEnabled;
        triggerToast(std::string("new cues fade in: ") + (deck.playlistDefaultFadeInEnabled ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultFadeOutToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultFadeOutEnabled = !deck.playlistDefaultFadeOutEnabled;
        triggerToast(std::string("new cues fade out: ") + (deck.playlistDefaultFadeOutEnabled ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultAudioToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultAudioEnabled = !deck.playlistDefaultAudioEnabled;
        triggerToast(std::string("new cues audio: ") + (deck.playlistDefaultAudioEnabled ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultPauseBeginToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultPauseAtBeginning = !deck.playlistDefaultPauseAtBeginning;
        triggerToast(std::string("new cues pause begin: ") + (deck.playlistDefaultPauseAtBeginning ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultPauseEndToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultPauseAtEnd = !deck.playlistDefaultPauseAtEnd;
        triggerToast(std::string("new cues pause end: ") + (deck.playlistDefaultPauseAtEnd ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == kSettingsActionPlaylistDefaultNextTransitionToggle) {
        Deck& deck = focusedDeckMutable();
        deck.playlistDefaultTransitionToNext = !deck.playlistDefaultTransitionToNext;
        triggerToast(std::string("new cues next transition: ") + (deck.playlistDefaultTransitionToNext ? "on" : "off"));
        markProjectDirty();
      } else if (sb.action == 210) {
        // Toggle MIDI
        midiEnabled_ = !midiEnabled_;
        if (midiEnabled_) startMidiInput(); else stopMidiInput();
      } else if (sb.action == 211) {
        // Set MIDI port
        auto port = pickTextInput("ALSA MIDI port", "e.g. 20:0 or client name", midiDeviceName_);
        if (port) {
          midiDeviceName_ = *port;
          if (midiEnabled_) { stopMidiInput(); startMidiInput(); }
        }
      } else if (sb.action == 220) {
        // Change OSC/Companion port
        auto portStr = pickTextInput("Companion/OSC port", "port number (default 5510)", std::to_string(companionPort_));
        if (portStr) {
          try {
            int p = std::stoi(*portStr);
            if (p > 0 && p < 65536 && p != companionPort_) {
              companionPort_ = p;
              stopCompanionControl();
              startCompanionControl();
              triggerToast("companion port: " + std::to_string(companionPort_));
            }
          } catch (...) {}
        }
      } else if (sb.action == kSettingsActionOscQueryToggle) {
        setOscQueryEnabled(!project_.oscQueryEnabled);
      } else if (sb.action == kSettingsActionOscQueryPortPrompt) {
        auto portStr = pickTextInput("OSC Query HTTP port", "port number (default 5511)", std::to_string(project_.oscQueryPort));
        if (portStr) {
          try {
            int p = std::stoi(*portStr);
            if (p > 0 && p < 65536) {
              setOscQueryPort(p);
            }
          } catch (...) {}
        }
      } else if (sb.action == kSettingsActionOscFeedbackMirrorToggle) {
        setOscFeedbackMirrorEnabled(!project_.oscFeedbackMirrorEnabled);
      } else if (sb.action == kSettingsActionOscFeedbackRatePrompt) {
        auto rate = pickTextInput("OSC feedback mirror rate", "milliseconds (40..2000)", std::to_string(project_.oscFeedbackRateMs));
        if (rate) {
          try {
            setOscFeedbackRateMs(std::stoi(*rate));
          } catch (...) {}
        }
      } else if (sb.action == 230) {
        setOutputSizingModeDisplayNative();
      } else if (sb.action == 231) {
        setOutputSizingModeFixed(1280, 720);
      } else if (sb.action == 232) {
        setOutputSizingModeFixed(1920, 1080);
      } else if (sb.action == 233) {
        setOutputSizingModeFixed(2560, 1440);
      } else if (sb.action == 234) {
        setOutputSizingModeFixed(3840, 2160);
      } else if (sb.action == 235) {
        sizeFocusedOutputToSelectedDisplay();
      } else if (sb.action == 236) {
        toggleOutputFullscreen();
      } else if (sb.action == 237) {
        std::string initial = std::to_string(project_.outputRenderWidth) + "x" + std::to_string(project_.outputRenderHeight);
        auto value = pickTextInput("Custom output raster", "WIDTHxHEIGHT (e.g. 2560x1080)", initial);
        if (value) {
          std::string token = toUpper(trim(*value));
          auto xPos = token.find('X');
          if (xPos != std::string::npos && xPos > 0 && xPos + 1 < token.size()) {
            try {
              int w = std::stoi(token.substr(0, xPos));
              int h = std::stoi(token.substr(xPos + 1));
              if (w > 0 && h > 0) {
                setOutputSizingModeFixed(w, h);
              }
            } catch (...) {
            }
          }
        }
      } else if (sb.action == 238) {
        setOutputRefreshRate(0.0);
      } else if (sb.action == 239) {
        cycleOutputRefreshRate(-1);
      } else if (sb.action == 240) {
        cycleOutputRefreshRate(1);
      } else if (sb.action == 241) {
        auto value = pickTextInput("Output refresh", "Hz or AUTO (e.g. 60 or 59.94)", outputRefreshRateLabel());
        if (value) {
          std::string token = toUpper(trim(*value));
          if (token == "AUTO") {
            setOutputRefreshRate(0.0);
          } else {
            try {
              setOutputRefreshRate(std::stod(*value));
            } catch (...) {
            }
          }
        }
      } else if (sb.action == 242) {
        setOutputBitDepthMode(0);
      } else if (sb.action == 243) {
        setOutputBitDepthMode(8);
      } else if (sb.action == 244) {
        setOutputBitDepthMode(10);
      } else if (sb.action == 245) {
        setOutputCanvasMode(false);
      } else if (sb.action == 246) {
        setOutputCanvasMode(true, project_.outputCanvasWidth, project_.outputCanvasHeight);
      } else if (sb.action == 247) {
        std::string initial = std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight);
        auto value = pickTextInput("Output canvas", "WIDTHxHEIGHT (e.g. 5760x2160)", initial);
        if (value) {
          std::string token = toUpper(trim(*value));
          auto xPos = token.find('X');
          if (xPos != std::string::npos && xPos > 0 && xPos + 1 < token.size()) {
            try {
              int w = std::stoi(token.substr(0, xPos));
              int h = std::stoi(token.substr(xPos + 1));
              if (w > 0 && h > 0) {
                setOutputCanvasMode(true, w, h);
              }
            } catch (...) {
            }
          }
        }
      } else if (sb.action == 248) {
        const Deck& deck = focusedDeck();
        std::string initial = std::to_string(deck.canvasViewX) + "," + std::to_string(deck.canvasViewY);
        auto value = pickTextInput("Canvas view", "X,Y offset in pixels", initial);
        if (value) {
          std::string token = trim(*value);
          size_t split = token.find(',');
          if (split == std::string::npos) {
            split = token.find(' ');
          }
          if (split != std::string::npos && split > 0 && split + 1 < token.size()) {
            try {
              int x = std::stoi(token.substr(0, split));
              int y = std::stoi(token.substr(split + 1));
              setFocusedDeckCanvasView(x, y);
            } catch (...) {
            }
          }
        }
      } else if (sb.action == 249) {
        toggleFocusedDeckWarpEnabled();
      } else if (sb.action == 250) {
        cycleFocusedOutput(-1);
      } else if (sb.action == 251) {
        cycleFocusedOutput(1);
      } else if (sb.action == 252) {
        addOutput(project_.focusedDeckIndex);
      } else if (sb.action == 275) {
        addOutput(project_.focusedDeckIndex, "window");
      } else if (sb.action == 276) {
        addOutput(project_.focusedDeckIndex, "stream");
      } else if (sb.action == kSettingsActionOutputRemove) {
        removeOutput(project_.focusedOutputIndex);
      } else if (sb.action == 254) {
        setFocusedOutputHostDeck(project_.focusedDeckIndex);
      } else if (sb.action == 255) {
        toggleFocusedOutputStreamEnabled();
      } else if (sb.action == 256) {
        cycleFocusedOutputStreamProtocol(1);
      } else if (sb.action == 257) {
        const OutputTarget& output = focusedOutput();
        std::string initial = trim(output.streamUrl).empty()
          ? defaultOutputStreamUrl(output.streamProtocol, project_.focusedOutputIndex)
          : output.streamUrl;
        auto value = pickTextInput("Stream URL", "srt://... or rtmp://...", initial);
        if (value) {
          setFocusedOutputStreamUrl(*value);
        }
      } else if (sb.action == 258) {
        const OutputTarget& output = focusedOutput();
        auto value = pickTextInput("Stream bitrate", "kbps (500-50000)", std::to_string(output.streamBitrateKbps));
        if (value) {
          try {
            setFocusedOutputStreamBitrateKbps(std::stoi(trim(*value)));
          } catch (...) {
          }
        }
      } else if (sb.action == 259) {
        const OutputTarget& output = focusedOutput();
        std::string nextType = normalizeOutputType(output.outputType) == "stream" ? "window" : "stream";
        setFocusedOutputType(nextType);
      } else if (sb.action == 281) {
        setFocusedOutputType("window");
      } else if (sb.action == 282) {
        setFocusedOutputType("stream");
      } else if (sb.action == kSettingsActionOutputOverlayToggle) {
        toggleFocusedOutputTimeOverlayEnabled();
      } else if (sb.action == kSettingsActionOutputAlphaPrompt) {
        int initialPct = static_cast<int>(std::lround(std::clamp(focusedOutput().outputAlpha, 0.0f, 1.0f) * 100.0f));
        auto value = pickTextInput("Output alpha", "Percent 0-100", std::to_string(initialPct));
        if (value) {
          try {
            setFocusedOutputAlpha(static_cast<float>(std::stod(trim(*value)) / 100.0));
          } catch (...) {
          }
        }
      } else if (sb.action == kSettingsActionOutputDelayPrompt) {
        auto value = pickTextInput("Output delay", "Milliseconds 0-5000", std::to_string(focusedOutput().outputDelayMs));
        if (value) {
          try {
            setFocusedOutputDelayMs(std::stoi(trim(*value)));
          } catch (...) {
          }
        }
      } else if (sb.action == kSettingsActionOutputColorSpaceCycle) {
        cycleFocusedOutputColorSpace(1);
      } else if (sb.action == kSettingsActionOutputDelayInc) {
        setFocusedOutputDelayMs(focusedOutput().outputDelayMs + 100);
      } else if (sb.action == kSettingsActionOutputLayoutSpan) {
        setFocusedOutputLayoutMode("span");
      } else if (sb.action == kSettingsActionOutputLayoutDuplicate) {
        setFocusedOutputLayoutMode("duplicate");
      } else if (sb.action == kSettingsActionOutputOrientationCycle) {
        cycleFocusedOutputOrientation(1);
      } else if (sb.action == kSettingsActionOutputTestCardToggle) {
        toggleFocusedOutputTestCardEnabled();
      } else if (sb.action == kSettingsActionOutputTestCardAllToggle) {
        bool anyOff = false;
        for (const auto& out : project_.outputs) {
          if (!out.outputTestCardEnabled) {
            anyOff = true;
            break;
          }
        }
        setAllOutputsTestCardEnabled(anyOff);
      } else if (sb.action == 260) {
        promptFocusedOutputMirrorSourcePicker();
      } else if (sb.action == 271) {
        setFocusedOutputNdiEnabled(!focusedOutput().ndiEnabled);
      } else if (sb.action == 272) {
        const OutputTarget& output = focusedOutput();
        std::string initial = trim(output.ndiSourceName).empty()
          ? defaultOutputNdiSourceName(output, project_.focusedOutputIndex)
          : output.ndiSourceName;
        auto value = pickTextInput("NDI Name", "Sender source name", initial);
        if (value) {
          setFocusedOutputNdiName(*value);
        }
      } else if (sb.action == 273) {
        setFocusedOutputNdiKeyEnabled(!focusedOutput().ndiKeyEnabled);
      } else if (sb.action == 274) {
        const OutputTarget& output = focusedOutput();
        std::string initial = trim(output.ndiKeySourceName).empty()
          ? defaultOutputNdiKeySourceName(output, project_.focusedOutputIndex)
          : output.ndiKeySourceName;
        auto value = pickTextInput("NDI Key Name", "Key sender source name", initial);
        if (value) {
          setFocusedOutputNdiKeyName(*value);
        }
      } else if (sb.action == kSettingsActionOutputAdvancedToggle) {
        videoOutputsAdvanced_ = !videoOutputsAdvanced_;
      } else if (sb.action == kSettingsActionOutputToggle) {
        toggleFocusedOutputEnabled();
      } else if (sb.action == kSettingsActionOutputDisplayPrev) {
        cycleOutputDisplay(-1);
      } else if (sb.action == kSettingsActionOutputDisplayNext) {
        cycleOutputDisplay(1);
      } else if (sb.action == kSettingsActionOutputDisplayRescan) {
        observedDisplayCount_ = SDL_GetNumVideoDisplays();
        refreshDisplayTopology(true);
      } else if (sb.action >= kSettingsActionOutputDisplayFocusBase &&
                 sb.action < kSettingsActionOutputDisplayFocusBase + 64) {
        int displayIndex = sb.action - kSettingsActionOutputDisplayFocusBase;
        setOutputDisplayIndex(displayIndex);
      } else if (sb.action >= kSettingsActionRoutingTableOutputPrevBase &&
                 sb.action < kSettingsActionRoutingTableOutputPrevBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableOutputPrevBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        int nextOutput = (routeOutput - 1 + outputCount) % outputCount;
        moveDeckToOutput(deckIndex, nextOutput);
        setFocusedOutputIndex(nextOutput);
      } else if (sb.action >= kSettingsActionRoutingTableOutputNextBase &&
                 sb.action < kSettingsActionRoutingTableOutputNextBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableOutputNextBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        int nextOutput = (routeOutput + 1) % outputCount;
        moveDeckToOutput(deckIndex, nextOutput);
        setFocusedOutputIndex(nextOutput);
      } else if (sb.action >= kSettingsActionRoutingTableLayerDecBase &&
                 sb.action < kSettingsActionRoutingTableLayerDecBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableLayerDecBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (!assignmentIndex) {
          assignDeckToOutput(deckIndex, routeOutput);
          assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        }
        if (!assignmentIndex) {
          return;
        }
        int currentLayer = std::clamp(project_.layerAssignments[*assignmentIndex].layerIndex, 0, 255);
        setDeckOutputAssignmentLayer(deckIndex, routeOutput, currentLayer - 1);
      } else if (sb.action >= kSettingsActionRoutingTableLayerIncBase &&
                 sb.action < kSettingsActionRoutingTableLayerIncBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableLayerIncBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (!assignmentIndex) {
          assignDeckToOutput(deckIndex, routeOutput);
          assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        }
        if (!assignmentIndex) {
          return;
        }
        int currentLayer = std::clamp(project_.layerAssignments[*assignmentIndex].layerIndex, 0, 255);
        setDeckOutputAssignmentLayer(deckIndex, routeOutput, currentLayer + 1);
      } else if (sb.action >= kSettingsActionRoutingTableAssignToggleBase &&
                 sb.action < kSettingsActionRoutingTableAssignToggleBase + static_cast<int>(project_.decks.size())) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = sb.action - kSettingsActionRoutingTableAssignToggleBase;
        setFocusedDeckIndex(deckIndex);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (assignmentIndex) {
          unassignDeckFromOutput(deckIndex, routeOutput);
        } else {
          assignDeckToOutput(deckIndex, routeOutput);
        }
      } else if (sb.action == kSettingsActionRoutingLayerDec ||
                 sb.action == kSettingsActionRoutingLayerInc) {
        int deckIndex = std::clamp(project_.focusedDeckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
        int outputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, outputIndex);
        if (!assignmentIndex) {
          triggerToast("assign route first");
        } else {
          int currentLayer = std::clamp(project_.layerAssignments[*assignmentIndex].layerIndex, 0, 255);
          bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
          bool ctrlHeld = (SDL_GetModState() & KMOD_CTRL) != 0;
          int step = ctrlHeld ? 10 : 1;
          int delta = (sb.action == kSettingsActionRoutingLayerDec) ? -step : step;
          if (shiftHeld) {
            delta = -delta;
          }
          setDeckOutputAssignmentLayer(deckIndex, outputIndex, currentLayer + delta);
        }
      } else if (sb.action == kSettingsActionRoutingAssignToggle) {
        int deckIndex = std::clamp(project_.focusedDeckIndex, 0, std::max(0, static_cast<int>(project_.decks.size()) - 1));
        int outputIndex = std::clamp(project_.focusedOutputIndex, 0, std::max(0, static_cast<int>(project_.outputs.size()) - 1));
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, outputIndex);
        if (assignmentIndex) {
          unassignDeckFromOutput(deckIndex, outputIndex);
        } else {
          assignDeckToOutput(deckIndex, outputIndex);
        }
      } else if (sb.action == kSettingsActionRoutingModeToggle) {
        routingMoveMode_ = !routingMoveMode_;
        triggerToast(std::string("routing mode: ") + (routingMoveMode_ ? "Move" : "Add"));
        playUiSound(UiSoundEffect::Toggle);
      } else if (sb.action >= kSettingsActionRoutingDeckFocusBase &&
                 sb.action < kSettingsActionRoutingDeckFocusBase + static_cast<int>(project_.decks.size())) {
        int deckIndex = sb.action - kSettingsActionRoutingDeckFocusBase;
        setFocusedDeckIndex(deckIndex);
      } else if (sb.action >= kSettingsActionRoutingOutputFocusBase &&
                 sb.action < kSettingsActionRoutingOutputFocusBase + static_cast<int>(project_.outputs.size())) {
        int outputIndex = sb.action - kSettingsActionRoutingOutputFocusBase;
        setFocusedOutputIndex(outputIndex);
      } else if (sb.action >= kSettingsActionRoutingCellBase) {
        int packed = sb.action - kSettingsActionRoutingCellBase;
        int deckIndex = packed / kSettingsActionRoutingCellStride;
        int outputIndex = packed % kSettingsActionRoutingCellStride;
        if (deckIndex >= 0 && deckIndex < static_cast<int>(project_.decks.size()) &&
            outputIndex >= 0 && outputIndex < static_cast<int>(project_.outputs.size())) {
          setFocusedDeckIndex(deckIndex);
          setFocusedOutputIndex(outputIndex);
          auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, outputIndex);
          bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
          bool ctrlHeld = (SDL_GetModState() & KMOD_CTRL) != 0;
          if (routingMoveMode_) {
            if (assignmentIndex) {
              int currentLayer = std::clamp(project_.layerAssignments[*assignmentIndex].layerIndex, 0, 255);
              int step = ctrlHeld ? 10 : 1;
              int delta = shiftHeld ? -step : step;
              setDeckOutputAssignmentLayer(deckIndex, outputIndex, currentLayer + delta);
            } else {
              moveDeckToOutput(deckIndex, outputIndex);
            }
          } else {
            if (assignmentIndex) {
              if (shiftHeld || ctrlHeld) {
                int currentLayer = std::clamp(project_.layerAssignments[*assignmentIndex].layerIndex, 0, 255);
                int step = ctrlHeld ? 10 : 1;
                int delta = shiftHeld ? -step : step;
                setDeckOutputAssignmentLayer(deckIndex, outputIndex, currentLayer + delta);
              } else {
                unassignDeckFromOutput(deckIndex, outputIndex);
              }
            } else {
              assignDeckToOutput(deckIndex, outputIndex);
            }
          }
        }
      }
      return;
    }
    // Click outside modal = close
    SDL_Rect modal = settingsModalRect();
    if (!pointInRect(mx, my, modal)) settingsOpen_ = false;
  }

  void handleDecksPanelMouseDown(int x, int y, Uint8 mouseButton) {
    auto focusMasterCueSilently = [&](int presetIndex) -> bool {
      normalizeProject(project_);
      if (presetIndex < 0 || presetIndex >= static_cast<int>(project_.groupPresets.size())) {
        return false;
      }
      if (project_.focusedGroupPresetIndex != presetIndex) {
        project_.focusedGroupPresetIndex = presetIndex;
        markProjectDirty();
      }
      return true;
    };

    for (const auto& hit : decksPanelDeckButtonHits_) {
      if (hit.deckIndex < 0 || hit.deckIndex >= static_cast<int>(project_.decks.size())) {
        continue;
      }
      if (!pointInRect(x, y, hit.rect)) {
        continue;
      }
      setFocusedDeckIndex(hit.deckIndex);
      switch (hit.action) {
        case kDecksPanelDeckActionTake:
          jumpSelectedCue();
          break;
        case kDecksPanelDeckActionStop:
          stopTransport();
          break;
        default:
          break;
      }
      return;
    }

    for (const auto& hit : decksPanelCueHits_) {
      if (hit.deckIndex < 0 || hit.deckIndex >= static_cast<int>(project_.decks.size())) {
        continue;
      }
      if (!pointInRect(x, y, hit.rowRect)) {
        continue;
      }
      setFocusedDeckIndex(hit.deckIndex);
      Deck& deck = project_.decks[hit.deckIndex];
      if (hit.cueIndex >= 0 && hit.cueIndex < static_cast<int>(deck.cues.size())) {
        bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
        bool ctrlHeld = (SDL_GetModState() & KMOD_CTRL) != 0;
        selectCueInDeck(hit.deckIndex, hit.cueIndex, shiftHeld, ctrlHeld);
      }
      if (mouseButton == SDL_BUTTON_RIGHT) {
        jumpSelectedCue();
      }
      return;
    }

    for (const auto& btn : decksPanelButtons_) {
      if (!pointInRect(x, y, btn.rect)) {
        continue;
      }
      if (btn.action >= kDecksPanelActionMasterFireBase &&
          btn.action < kDecksPanelActionMasterFireBase + static_cast<int>(project_.groupPresets.size())) {
        int presetIndex = btn.action - kDecksPanelActionMasterFireBase;
        setFocusedGroupPresetIndex(presetIndex);
        fireGroupPreset(presetIndex, true);
        return;
      }
      switch (btn.action) {
        case kDecksPanelActionGroupPrev:
          ensureGroupPreset(true);
          cycleFocusedGroupPreset(-1);
          break;
        case kDecksPanelActionGroupNext:
          ensureGroupPreset(true);
          cycleFocusedGroupPreset(1);
          break;
        case kDecksPanelActionGroupNew:
          addGroupPreset("", false);
          break;
        case kDecksPanelActionGroupFire:
          ensureGroupPreset(true);
          fireFocusedGroupPreset(true);
          break;
        case kDecksPanelActionGroupDelete:
          deleteFocusedGroupPreset();
          break;
        default:
          break;
      }
      return;
    }

    for (const auto& hit : masterCueRowHits_) {
      if (hit.presetIndex < 0 || hit.presetIndex >= static_cast<int>(project_.groupPresets.size())) {
        continue;
      }
      if (!pointInRect(x, y, hit.rowRect)) {
        continue;
      }
      focusMasterCueSilently(hit.presetIndex);

      if (pointInRect(x, y, hit.fireRect)) {
        fireGroupPreset(hit.presetIndex, true);
        return;
      }

      bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
      bool ctrlHeld = (SDL_GetModState() & KMOD_CTRL) != 0;
      for (int deckIndex = 0; deckIndex < static_cast<int>(hit.slotRects.size()); ++deckIndex) {
        if (!pointInRect(x, y, hit.slotRects[deckIndex])) {
          continue;
        }
        setFocusedDeckIndex(deckIndex);

        if (mouseButton == SDL_BUTTON_RIGHT) {
          bool bypass = false;
          if (const GroupPreset* preset = focusedGroupPreset()) {
            if (deckIndex >= 0 && deckIndex < static_cast<int>(preset->slots.size())) {
              bypass = preset->slots[deckIndex].bypass;
            }
          }
          setFocusedGroupSlotBypass(deckIndex, !bypass);
          return;
        }

        if (mouseButton == SDL_BUTTON_MIDDLE || ctrlHeld) {
          int direction = shiftHeld ? -1 : 1;
          cycleFocusedGroupSlotCue(deckIndex, direction);
          return;
        }

        if (shiftHeld) {
          setFocusedGroupSlotFromCueIndex(deckIndex, project_.decks[deckIndex].activeIndex);
        } else {
          setFocusedGroupSlotFromCueIndex(deckIndex, project_.decks[deckIndex].selectedIndex);
        }
        return;
      }
      return;
    }

    for (const auto& hit : decksPanelRowHits_) {
      if (hit.deckIndex < 0 || hit.deckIndex >= static_cast<int>(project_.decks.size())) {
        continue;
      }
      if (pointInRect(x, y, hit.groupRect)) {
        setFocusedDeckIndex(hit.deckIndex);
        ensureGroupPreset(true);
        bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
        if (mouseButton == SDL_BUTTON_RIGHT) {
          bool bypass = false;
          if (const GroupPreset* preset = focusedGroupPreset()) {
            if (hit.deckIndex >= 0 && hit.deckIndex < static_cast<int>(preset->slots.size())) {
              bypass = preset->slots[hit.deckIndex].bypass;
            }
          }
          setFocusedGroupSlotBypass(hit.deckIndex, !bypass);
          return;
        }
        int direction = (mouseButton == SDL_BUTTON_MIDDLE || shiftHeld) ? -1 : 1;
        cycleFocusedGroupSlotCue(hit.deckIndex, direction);
        return;
      }
      if (pointInRect(x, y, hit.rowRect)) {
        setFocusedDeckIndex(hit.deckIndex);
        return;
      }
    }
  }

  void handleMouseDown(int x, int y) {
    if (showSplashOverlay_) {
      showSplashOverlay_ = false;
      return;
    }
    if (showStartupDialog_) {
      bool hasSavedFile = !currentProjectFile_.empty() && fs::exists(currentProjectFile_);
      if (pointInRect(x, y, startupNewBtn_)) {
        startNewShow(false);
        showStartupDialog_ = false;
      } else if (pointInRect(x, y, startupLoadBtn_) && hasSavedFile) {
        // Open previous show file already loaded from startup path.
        showStartupDialog_ = false;
      } else if (pointInRect(x, y, startupOpenSavedBtn_)) {
        openProjectFromPicker();
        showStartupDialog_ = false;
      }
      return;
    }
    if (confirmQuit_) {
      if (pointInRect(x, y, quitYesBtn_)) {
        gShouldQuit.store(true);
      } else {
        confirmQuit_ = false;
      }
      return;
    }

    // Settings modal intercepts all clicks when open
    if (settingsOpen_) {
      handleSettingsClick(x, y);
      return;
    }
    for (const auto& outputBtn : outputMenuButtons_) {
      if (!pointInRect(x, y, outputBtn.rect)) {
        continue;
      }
      if (outputBtn.action == kOutputMenuActionAddOutput) {
        addOutput(project_.focusedDeckIndex);
        return;
      }
      if (outputBtn.action == kOutputMenuActionFocus) {
        if (outputBtn.outputIndex >= 0 && outputBtn.outputIndex < static_cast<int>(project_.outputs.size())) {
          setFocusedOutputIndex(outputBtn.outputIndex);
        }
        return;
      }
      if (outputBtn.action == kOutputMenuActionToggle) {
        if (outputBtn.outputIndex >= 0 && outputBtn.outputIndex < static_cast<int>(project_.outputs.size())) {
          setFocusedOutputIndex(outputBtn.outputIndex);
          setFocusedOutputEnabled(!project_.outputs[outputBtn.outputIndex].enabled);
        }
        return;
      }
      if (outputBtn.action == kOutputMenuActionRouteFocusDeck ||
          outputBtn.action == kOutputMenuActionRouteAssignToggle ||
          outputBtn.action == kOutputMenuActionRouteLayerDec ||
          outputBtn.action == kOutputMenuActionRouteLayerInc ||
          outputBtn.action == kOutputMenuActionRouteOutputPrev ||
          outputBtn.action == kOutputMenuActionRouteOutputNext) {
        if (project_.decks.empty() || project_.outputs.empty()) {
          return;
        }
        int deckIndex = std::clamp(outputBtn.deckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
        setFocusedDeckIndex(deckIndex);

        int outputCount = static_cast<int>(project_.outputs.size());
        int routeOutput = outputBtn.outputIndex;
        if (routeOutput < 0 || routeOutput >= outputCount) {
          routeOutput = std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        }

        if (outputBtn.action == kOutputMenuActionRouteOutputPrev ||
            outputBtn.action == kOutputMenuActionRouteOutputNext) {
          int delta = outputBtn.action == kOutputMenuActionRouteOutputPrev ? -1 : 1;
          int nextOutput = (routeOutput + delta + outputCount) % outputCount;
          moveDeckToOutput(deckIndex, nextOutput);
          setFocusedOutputIndex(nextOutput);
          return;
        }

        setFocusedOutputIndex(routeOutput);
        auto assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (outputBtn.action == kOutputMenuActionRouteFocusDeck) {
          return;
        }
        if (outputBtn.action == kOutputMenuActionRouteAssignToggle) {
          if (assignmentIndex) {
            unassignDeckFromOutput(deckIndex, routeOutput);
          } else {
            assignDeckToOutput(deckIndex, routeOutput);
          }
          return;
        }
        if (!assignmentIndex) {
          assignDeckToOutput(deckIndex, routeOutput);
          assignmentIndex = assignmentIndexForDeckOutput(deckIndex, routeOutput);
          if (!assignmentIndex) {
            return;
          }
        }
        int currentLayer = std::clamp(project_.layerAssignments[*assignmentIndex].layerIndex, 0, 255);
        int delta = outputBtn.action == kOutputMenuActionRouteLayerDec ? -1 : 1;
        setDeckOutputAssignmentLayer(deckIndex, routeOutput, currentLayer + delta);
        return;
      }
      return;
    }
    for (int deckIndex = 0; deckIndex < static_cast<int>(deckColumnRects_.size()); ++deckIndex) {
      if (!pointInRect(x, y, deckColumnRects_[deckIndex])) {
        continue;
      }
      setFocusedDeckIndex(deckIndex);
      if (deckIndex < static_cast<int>(deckOpacityFaderRects_.size()) &&
          pointInRect(x, y, deckOpacityFaderRects_[deckIndex]) &&
          deckOpacityFaderRects_[deckIndex].w > 0) {
        bool altHeld = (SDL_GetModState() & KMOD_ALT) != 0;
        const SDL_Rect& rail = deckOpacityFaderRects_[deckIndex];
        float value = static_cast<float>(std::clamp(
          static_cast<double>(x - rail.x) / static_cast<double>(rail.w),
          0.0,
          1.0));
        if (altHeld) {
          value = value >= 0.5f ? 1.0f : 0.0f;
        }
        setDeckPlaylistOpacity(deckIndex, value, true);
        return;
      }
      const SDL_Rect& clipFrame = deckListClipRects_[deckIndex];
      SDL_Rect clipRect {clipFrame.x + 8, clipFrame.y + 8, clipFrame.w - 16, clipFrame.h - 16};
      if (!pointInRect(x, y, clipRect)) {
        return;
      }
      int listY = clipRect.y - deckScrolls_[deckIndex];
      Deck& deck = project_.decks[deckIndex];
      for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
        SDL_Rect row {clipRect.x, listY, clipRect.w, kRowHeight};
        if (pointInRect(x, y, row)) {
          bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
          bool ctrlHeld = (SDL_GetModState() & KMOD_CTRL) != 0;
          selectCueInDeck(deckIndex, cueIndex, shiftHeld, ctrlHeld);
          drag_.active = true;
          drag_.deckIndex = deckIndex;
          drag_.cueIndex = cueIndex;
          return;
        }
        listY += kRowHeight + 8;
      }
      return;
    }
    if (pointInRect(x, y, fileNewBtnRect_)) {
      startNewShow(true);
      return;
    }
    if (pointInRect(x, y, fileOpenBtnRect_)) {
      openProjectFromPicker();
      return;
    }
    if (pointInRect(x, y, fileSaveBtnRect_)) {
      saveProjectNow(true);
      return;
    }
    if (pointInRect(x, y, fileSaveAsBtnRect_)) {
      saveProjectAsFromPicker();
      return;
    }
    if (pointInRect(x, y, decksPanelToggleRect_)) {
      setDecksPanelVisible(true, true);
      return;
    }
    if (pointInRect(x, y, deckSidebarToggleRect_)) {
      // Sidebar is fixed-visible; keep click handler as a no-op safety.
      return;
    }
    for (const auto& btn : masterCueSidebarButtons_) {
      if (!pointInRect(x, y, btn.rect)) {
        continue;
      }
      switch (btn.action) {
        case kDecksPanelActionGroupPrev:
          ensureGroupPreset(true);
          cycleFocusedGroupPreset(-1);
          return;
        case kDecksPanelActionGroupNext:
          ensureGroupPreset(true);
          cycleFocusedGroupPreset(1);
          return;
        case kDecksPanelActionGroupNew:
          addGroupPreset("", false);
          return;
        case kDecksPanelActionGroupFire:
          ensureGroupPreset(true);
          fireFocusedGroupPreset(true);
          return;
        case kDecksPanelActionGroupDelete:
          deleteFocusedGroupPreset();
          return;
        case kDecksPanelActionGroupCaptureSelected:
          ensureGroupPreset(false);
          captureFocusedGroupPreset(false);
          return;
        case kDecksPanelActionGroupCaptureActive:
          ensureGroupPreset(false);
          captureFocusedGroupPreset(true);
          return;
        case kDecksPanelActionGroupRename: {
          ensureGroupPreset(false);
          int presetIndex = std::clamp(project_.focusedGroupPresetIndex, 0,
                                       static_cast<int>(project_.groupPresets.size()) - 1);
          std::string currentName = groupPresetLabel(presetIndex);
          auto value = pickTextInput("Scene Name", "Enter name for focused scene", currentName);
          if (value) {
            renameFocusedGroupPreset(*value);
          }
          return;
        }
        case kDecksPanelActionGroupProgramToggle:
          masterCueProgrammerExpanded_ = !masterCueProgrammerExpanded_;
          return;
        default:
          break;
      }
      if (btn.action >= kDecksPanelActionMasterFireBase &&
          btn.action < kDecksPanelActionMasterFireBase + static_cast<int>(project_.groupPresets.size())) {
        int presetIndex = btn.action - kDecksPanelActionMasterFireBase;
        setFocusedGroupPresetIndex(presetIndex);
        fireGroupPreset(presetIndex, true);
        return;
      }
    }
    for (const auto& hit : masterCueSidebarProgramHits_) {
      if (hit.deckIndex < 0 || hit.deckIndex >= static_cast<int>(project_.decks.size())) {
        continue;
      }
      if (!pointInRect(x, y, hit.rowRect)) {
        continue;
      }
      setFocusedDeckIndex(hit.deckIndex);
      ensureGroupPreset(true);
      if (pointInRect(x, y, hit.selectedRect)) {
        setFocusedGroupSlotFromCueIndex(hit.deckIndex, project_.decks[hit.deckIndex].selectedIndex);
        return;
      }
      if (pointInRect(x, y, hit.activeRect)) {
        setFocusedGroupSlotFromCueIndex(hit.deckIndex, project_.decks[hit.deckIndex].activeIndex);
        return;
      }
      if (pointInRect(x, y, hit.bypassRect)) {
        bool bypass = false;
        if (const GroupPreset* preset = focusedGroupPreset()) {
          if (hit.deckIndex >= 0 && hit.deckIndex < static_cast<int>(preset->slots.size())) {
            bypass = preset->slots[hit.deckIndex].bypass;
          }
        }
        setFocusedGroupSlotBypass(hit.deckIndex, !bypass);
        return;
      }
      if (pointInRect(x, y, hit.prevRect)) {
        cycleFocusedGroupSlotCue(hit.deckIndex, -1);
        return;
      }
      if (pointInRect(x, y, hit.nextRect)) {
        cycleFocusedGroupSlotCue(hit.deckIndex, 1);
        return;
      }
      // Row click (outside buttons): assign this deck's currently selected cue.
      setFocusedGroupSlotFromCueIndex(hit.deckIndex, project_.decks[hit.deckIndex].selectedIndex);
      return;
    }
    for (const auto& row : masterCueSidebarRows_) {
      if (row.presetIndex < 0 || row.presetIndex >= static_cast<int>(project_.groupPresets.size())) {
        continue;
      }
      if (!pointInRect(x, y, row.rowRect)) {
        continue;
      }
      setFocusedGroupPresetIndex(row.presetIndex);
      if (pointInRect(x, y, row.fireRect)) {
        fireGroupPreset(row.presetIndex, true);
      }
      return;
    }
    // Settings gear button
    if (pointInRect(x, y, settingsGearRect_)) {
      settingsOpen_ = !settingsOpen_;
      return;
    }
    // BLK (blackout) button
    if (pointInRect(x, y, blackoutBtnRect_)) {
      masterDimmerTarget_ = (masterDimmerTarget_ < 0.5) ? 1.0 : 0.0;
      triggerToast(masterDimmerTarget_ < 0.5 ? "blackout ON" : "blackout off");
      return;
    }

    for (size_t i = 0; i < quickButtons_.size(); ++i) {
      const auto& qb = quickButtons_[i];
      bool isCueSettingsButton = i >= cueSettingsQuickButtonStartIndex_;
      if (isCueSettingsButton && !pointInRect(x, y, cueSettingsViewportRect_)) {
        continue;
      }
      if (pointInRect(x, y, qb.rect)) {
        dispatchQuickAction(qb.action);
        return;
      }
    }

    for (const auto& button : buttons_) {
      if (pointInRect(x, y, button.rect)) {
        triggerButton(button.label);
        return;
      }
    }

    if (pointInRect(x, y, masterFaderRect_) && masterFaderRect_.w > 0) {
      double frac = static_cast<double>(x - masterFaderRect_.x) / static_cast<double>(masterFaderRect_.w);
      project_.masterVolume = std::clamp(frac * 2.0, 0.0, 2.0);
      markProjectDirty();
      return;
    }
    if (pointInRect(x, y, progressBarRect_)) {
      MediaEngine* engine = focusedMediaEngine();
      if (!engine) {
        return;
      }
      double fraction = static_cast<double>(x - progressBarRect_.x) / static_cast<double>(progressBarRect_.w);
      engine->seek(engine->duration() * std::clamp(fraction, 0.0, 1.0));
    }
  }

  void handleMouseMotion(int x, int y) {
    if (!drag_.active || drag_.cueIndex < 0) {
      return;
    }
    int di = drag_.deckIndex;
    if (di < 0 || di >= static_cast<int>(deckListClipRects_.size())) {
      return;
    }
    const SDL_Rect& clipFrame = deckListClipRects_[di];
    SDL_Rect clipRect {clipFrame.x + 8, clipFrame.y + 8, clipFrame.w - 16, clipFrame.h - 16};
    Deck& deck = project_.decks[di];
    int listY = clipRect.y - deckScrolls_[di];
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      SDL_Rect row {clipRect.x, listY, clipRect.w, kRowHeight};
      if (pointInRect(x, y, row) && index != drag_.cueIndex) {
        auto cue = deck.cues[drag_.cueIndex];
        deck.cues.erase(deck.cues.begin() + drag_.cueIndex);
        deck.cues.insert(deck.cues.begin() + index, cue);
        deck.selectedIndex = index;
        deck.selectedIndices.clear();
        deck.selectedIndices.push_back(index);
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
        markProjectDirty();
        return;
      }
      listY += kRowHeight + 8;
    }
  }

  void handleKeyDown(SDL_Keycode key, Uint16 mod, Uint32 sourceWindowId = 0, bool keyRepeat = false) {
    bool ctrl = (mod & KMOD_CTRL) != 0;
    bool shift = (mod & KMOD_SHIFT) != 0;

    if (showSplashOverlay_) {
      if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
        showSplashOverlay_ = false;
      }
      return;
    }

    if (showStartupDialog_) {
      bool hasSavedFile = !currentProjectFile_.empty() && fs::exists(currentProjectFile_);
      if (key == SDLK_n) {
        startNewShow(false);
        showStartupDialog_ = false;
      } else if (key == SDLK_o) {
        openProjectFromPicker();
        showStartupDialog_ = false;
      } else if (key == SDLK_p && hasSavedFile) {
        showStartupDialog_ = false;
      } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (hasSavedFile) {
          showStartupDialog_ = false;
        } else {
          openProjectFromPicker();
          showStartupDialog_ = false;
        }
      } else if (key == SDLK_ESCAPE) {
        showStartupDialog_ = false;
      }
      return;
    }

    if (settingsOpen_) {
      if (key == SDLK_ESCAPE) settingsOpen_ = false;
      return;
    }

    if (confirmQuit_) {
      if (key == SDLK_y || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        gShouldQuit.store(true);
      } else {
        confirmQuit_ = false;
      }
      return;
    }

    if (ctrl && key == SDLK_q) {
      confirmQuit_ = true;
      return;
    }

    // Ctrl+Enter / Ctrl+Return — simultaneous all-deck take
    if (ctrl && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
      takeAllDecks(true);
      return;
    }
    // Ctrl+Shift+Space — stop all decks (must be checked before Ctrl+Space)
    if (ctrl && shift && key == SDLK_SPACE) {
      allStop();
      return;
    }
    // Ctrl+Shift+G — fire focused group preset
    if (ctrl && shift && key == SDLK_g) {
      ensureGroupPreset(true);
      fireFocusedGroupPreset(true);
      return;
    }
    // Ctrl+Shift+N — new group preset from selected cues
    if (ctrl && shift && key == SDLK_n) {
      addGroupPreset("", true);
      return;
    }
    // Ctrl+Shift+[ / ] — cycle focused group preset
    if (ctrl && shift && key == SDLK_LEFTBRACKET) {
      ensureGroupPreset(true);
      cycleFocusedGroupPreset(-1);
      return;
    }
    if (ctrl && shift && key == SDLK_RIGHTBRACKET) {
      ensureGroupPreset(true);
      cycleFocusedGroupPreset(1);
      return;
    }
    // Ctrl+Space — all-deck go (play/pause toggle)
    if (ctrl && key == SDLK_SPACE) {
      goAllDecks();
      return;
    }
    // Ctrl+G — GOTO (jump to cue number via dialog)
    if (ctrl && key == SDLK_g) {
      auto r = pickTextInput("GOTO Cue", "Jump to cue number:");
      if (r) handleRemoteCommand("GOTO " + *r);
      return;
    }
    if (ctrl && !shift && key == SDLK_f) {
      auto r = pickTextInput("Find Cue", "Cue number, cue id, or name contains:", lastCueFindToken_);
      if (r) {
        findCueToken(*r, 1, false);
      }
      return;
    }
    if (ctrl && shift && key == SDLK_f) {
      if (!lastCueFindToken_.empty()) {
        findCueToken(lastCueFindToken_, 1, false);
      } else {
        triggerToast("find: press Ctrl+F first");
      }
      return;
    }
    if (ctrl && shift && key == SDLK_r) {
      auto prefix = pickTextInput("Renumber Cues", "Prefix (optional). Example: Q", "");
      if (prefix) {
        renumberFocusedDeckCueNumbers(*prefix, 1);
      }
      return;
    }

    if (ctrl && key == SDLK_o) {
      openProjectFromPicker();
      return;
    }
    if (ctrl && key == SDLK_i) {
      importWithPicker();
      return;
    }
    if (ctrl && key == SDLK_n) {
      addDeck();
      return;
    }
    if (ctrl && !shift && key == SDLK_s) {
      saveProjectNow(true);
      return;
    }
    if (ctrl && shift && key == SDLK_s) {
      saveProjectAsFromPicker();
      return;
    }

    switch (key) {
      case SDLK_ESCAPE:
        if (keyRepeat) {
          break;
        }
        {
          constexpr Uint64 kEscRepeatMs = 900;
          Uint64 now = SDL_GetTicks64();
          bool quickEsc = lastEscapeKeyMs_ > 0 && (now - lastEscapeKeyMs_) <= kEscRepeatMs;
          escapePressStreak_ = quickEsc ? (escapePressStreak_ + 1) : 1;
          lastEscapeKeyMs_ = now;
          if (escapePressStreak_ >= 3) {
            if (emergencyDisarmOutputsFromEsc(sourceWindowId)) {
              escapePressStreak_ = 0;
              confirmQuit_ = false;
              break;
            }
            // No active output-safety context: treat this press as a new sequence start.
            escapePressStreak_ = 1;
          }
        }
        if (!escapeOutputFullscreen(sourceWindowId)) {
          confirmQuit_ = true;
        } else {
          confirmQuit_ = false;
        }
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
      case SDLK_LEFT:
        nudgeFocusedPausedPlayback(-1, mod);
        break;
      case SDLK_RIGHT:
        nudgeFocusedPausedPlayback(1, mod);
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
        jumpSelectedCue();
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
        if (shift) {
          importWithPicker();
        } else if (!setActiveTrimFromPlayhead(true)) {
          importWithPicker();
        }
        break;
      case SDLK_b:
        addBrowserCueFromPrompt();
        break;
      case SDLK_g:
        addLowerThirdCue();
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
      case SDLK_x:
        cycleSelectedEndAction();
        break;
      case SDLK_6:
        toggleShuffle();
        break;
      case SDLK_k:
        cycleSelectedColorTag();
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
        toggleFocusedOutputNdi();
        break;
      case SDLK_o:
        if (shift) {
          toggleTimeOverlayEnabled();
        } else if (!setActiveTrimFromPlayhead(false)) {
          toggleTimeOverlayEnabled();
        }
        break;
      case SDLK_t:
        setTimecodeRunEnabled(!focusedDeck().timecodeRunEnabled);
        break;
      case SDLK_5:
        setTimecodeChaseEnabled(!focusedDeck().timecodeChaseEnabled);
        break;
      case SDLK_DELETE:
        deleteSelected();
        break;
      case SDLK_BACKSPACE:
        if (!focusedDeck().overlayActiveIndices.empty()) {
          clearOverlay();
        } else {
          deleteSelected();
        }
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
        if (handleCueTypeAheadKey(key, mod)) {
          return;
        }
        break;
    }
  }

  void triggerButton(const std::string& label) {
    if (label == "IMPORT") {
      importWithPicker();
    } else if (label == "SOURCE") {
      addSourceCueFromMenu();
    } else if (label == "PATTERN") {
      addPatternCueFromMenu();
    } else if (label == "(A) TAKE") {
      jumpSelectedCue();
    } else if (label == "START PLAY") {
      toggleTransport();
    } else if (label == "(B) STOP") {
      stopTransport();
    } else if (label == "SELECT CLR") {
      clearOutput();
    } else if (label == "OUTPUT") {
      settingsOpen_ = true;
      settingsTab_ = 3;
    }
  }

  std::string transportStatusLabel(int deckIndex) const {
    const Cue* activeCue = activeCuePtr(deckIndex);
    const DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (activeCue && activeCue->kind == CueKind::Browser) {
      return runtime && runtime->browserCueLive ? "Live Browser" : "Browser Ready";
    }
    if (activeCue && isSourceCueKind(activeCue->kind)) {
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      return (engine && engine->isSourceCapturing()) ? "Live Source" : "Source Ready";
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
    markProjectDirty();
  }

  void takeSelected(bool autoplay, bool useTransition = true) {
    Deck& deck = focusedDeckMutable();
    int deckIndex = std::clamp(project_.focusedDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
    MediaEngine* engine = focusedMediaEngine();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return;
    }
    if (!engine) {
      return;
    }
    const Cue& cue = deck.cues[deck.selectedIndex];

    // Lower-third cues go to the overlay slot, not the main slot
    if (cue.kind == CueKind::LowerThird) {
      // Push to overlay stack (max 4; avoid duplicates).
      auto& ov = deck.overlayActiveIndices;
      if (std::find(ov.begin(), ov.end(), deck.selectedIndex) == ov.end()) {
        if (ov.size() >= 4) ov.erase(ov.begin());
        ov.push_back(deck.selectedIndex);
      }
      triggerToast("overlay live: " + cue.name);
      playUiSound(UiSoundEffect::Take);
      markProjectDirty();
      return;
    }

    deck.activeIndex = deck.selectedIndex;
    stopBrowserCue();
    bool effectiveAutoplay = autoplay && !cue.pauseAtBeginning;
    if (deck.playlistAutoFade && autoplay) {
      deck.playlistOpacity = 0.0f;
      setDeckPlaylistOpacityTarget(deckIndex, 1.0f);
    }
    // Use per-cue transition override if set, else deck default
    double transSecs = 0.0;
    std::string transStyleStr = "cut";
    if (useTransition) {
      transSecs = (cue.cueTransitionSeconds >= 0.0)
        ? cue.cueTransitionSeconds : deck.transitionSeconds;
      transStyleStr = !cue.cueTransitionStyle.empty()
        ? cue.cueTransitionStyle : deck.transitionStyle;
    }
    engine->loadCue(
      &cue,
      effectiveAutoplay,
      transSecs,
      parseTransitionStyleToken(transStyleStr)
    );
    if (cue.kind == CueKind::Browser) {
      startBrowserCue(project_.focusedDeckIndex, cue);
      triggerToast("browser jumped live");
    } else if (isSourceCueKind(cue.kind)) {
      bool live = engine->isSourceCapturing();
      if (effectiveAutoplay && live) {
        triggerToast("source live");
      } else if (effectiveAutoplay && !live) {
        triggerToast("source unavailable");
      } else {
        triggerToast("source loaded");
      }
    } else {
      triggerToast(effectiveAutoplay ? "cue jumped live" : "cue loaded");
    }
    playUiSound(UiSoundEffect::Take);
    markProjectDirty();
  }

  void jumpSelectedCue() {
    takeSelected(jumpTriggersPlayback(), project_.jumpTransitionEnabled);
  }

  // Fire the selected cue on every deck simultaneously.
  // Useful for synced multi-layer playback (e.g. video + audio on separate decks).
  void takeAllDecks(bool autoplay) {
    int savedFocus = project_.focusedDeckIndex;
    for (int di = 0; di < static_cast<int>(project_.decks.size()); ++di) {
      Deck& deck = project_.decks[di];
      if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
        continue;
      }
      project_.focusedDeckIndex = di;
      takeSelected(autoplay);
    }
    project_.focusedDeckIndex = savedFocus;
    triggerToast("all decks fired");
  }

  // Toggle play/pause on every deck simultaneously.
  void goAllDecks() {
    int savedFocus = project_.focusedDeckIndex;
    for (int di = 0; di < static_cast<int>(project_.decks.size()); ++di) {
      project_.focusedDeckIndex = di;
      MediaEngine* engine = focusedMediaEngine();
      const Cue* activeCue = activeCuePtr();
      if (engine && activeCue) {
        if (engine->state() == TransportState::Playing) {
          engine->pause();
        } else {
          engine->play();
        }
      } else {
        takeSelected(true);
      }
    }
    project_.focusedDeckIndex = savedFocus;
    triggerToast("all decks go");
  }

  void allStop() {
    int saved = project_.focusedDeckIndex;
    for (int di = 0; di < (int)project_.decks.size(); ++di) {
      project_.focusedDeckIndex = di;
      if (auto* e = focusedMediaEngine()) e->stop();
    }
    project_.focusedDeckIndex = saved;
    triggerToast("all decks stopped");
  }

  void selectRelative(int direction, bool reorder) {
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) {
      return;
    }

    if (deck.selectedIndex < 0) {
      deck.selectedIndex = 0;
      onSelectionChanged();
      markProjectDirty();
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
      deck.selectedIndices.clear();
      deck.selectedIndices.push_back(nextIndex);
      onSelectionChanged();
    }
    markProjectDirty();
  }

  Cue* selectedCueMutable() {
    Deck& deck = focusedDeckMutable();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return nullptr;
    }
    return &deck.cues[deck.selectedIndex];
  }

  std::optional<int> cueIndexById(const Deck& deck, const std::string& cueId) const {
    std::string needle = toUpper(trim(cueId));
    if (needle.empty()) {
      return std::nullopt;
    }
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      if (toUpper(deck.cues[index].id) == needle) {
        return index;
      }
    }
    return std::nullopt;
  }

  std::optional<int> cueIndexByToken(const Deck& deck, const std::string& token) const {
    std::string trimmed = trim(token);
    if (trimmed.empty()) {
      return std::nullopt;
    }

    std::string upperToken = toUpper(trimmed);
    if (upperToken == "FIRST") {
      return deck.cues.empty() ? std::nullopt : std::optional<int> {0};
    }
    if (upperToken == "LAST") {
      return deck.cues.empty() ? std::nullopt : std::optional<int> {static_cast<int>(deck.cues.size()) - 1};
    }
    if (upperToken == "NEXT") {
      if (deck.cues.empty()) {
        return std::nullopt;
      }
      if (deck.selectedIndex < 0) {
        return 0;
      }
      return std::clamp(deck.selectedIndex + 1, 0, static_cast<int>(deck.cues.size()) - 1);
    }
    if (upperToken == "PREV" || upperToken == "PREVIOUS") {
      if (deck.cues.empty()) {
        return std::nullopt;
      }
      if (deck.selectedIndex < 0) {
        return 0;
      }
      return std::clamp(deck.selectedIndex - 1, 0, static_cast<int>(deck.cues.size()) - 1);
    }
    if (upperToken == "SEL" || upperToken == "SELECTED") {
      if (deck.selectedIndex >= 0 && deck.selectedIndex < static_cast<int>(deck.cues.size())) {
        return deck.selectedIndex;
      }
      return std::nullopt;
    }
    if (upperToken == "ACT" || upperToken == "ACTIVE") {
      if (deck.activeIndex >= 0 && deck.activeIndex < static_cast<int>(deck.cues.size())) {
        return deck.activeIndex;
      }
      return std::nullopt;
    }
    if ((trimmed[0] == '+' || trimmed[0] == '-') && deck.selectedIndex >= 0) {
      try {
        int delta = std::stoi(trimmed);
        return std::clamp(deck.selectedIndex + delta, 0, static_cast<int>(deck.cues.size()) - 1);
      } catch (...) {
      }
    }

    try {
      int index = std::stoi(trimmed);
      if (index >= 1 && index <= static_cast<int>(deck.cues.size())) {
        return index - 1;
      }
    } catch (...) {
    }

    if (auto byId = cueIndexById(deck, trimmed); byId) {
      return byId;
    }

    // Exact match on operator-facing short cue id / cue number
    std::string needle = toUpper(trimmed);
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      if (!deck.cues[index].cueId.empty() && toUpper(deck.cues[index].cueId) == needle) {
        return index;
      }
      if (!deck.cues[index].cueNumber.empty() && toUpper(deck.cues[index].cueNumber) == needle) {
        return index;
      }
    }

    // Prefix match on short cue id / cue number (operator-friendly shorthand)
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      if (!deck.cues[index].cueId.empty()) {
        std::string cueIdShort = toUpper(deck.cues[index].cueId);
        if (cueIdShort.rfind(needle, 0) == 0) {
          return index;
        }
      }
      if (!deck.cues[index].cueNumber.empty()) {
        std::string cueNum = toUpper(deck.cues[index].cueNumber);
        if (cueNum.rfind(needle, 0) == 0) {
          return index;
        }
      }
    }

    // Partial match on name
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      if (toUpper(deck.cues[index].name).find(needle) != std::string::npos) {
        return index;
      }
    }
    return std::nullopt;
  }

  std::vector<int> cueFindMatches(const Deck& deck, const std::string& token) const {
    std::string needle = toUpper(trim(token));
    if (needle.empty()) {
      return {};
    }

    std::vector<int> matches;
    std::unordered_set<int> seen;
    auto pushMatch = [&](int cueIndex) {
      if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) {
        return;
      }
      if (seen.insert(cueIndex).second) {
        matches.push_back(cueIndex);
      }
    };

    auto isExact = [&](const Cue& cue) {
      return toUpper(cue.id) == needle
        || toUpper(cue.cueId) == needle
        || toUpper(cue.cueNumber) == needle
        || toUpper(cue.name) == needle;
    };
    auto isPrefix = [&](const Cue& cue) {
      std::string cueId = toUpper(cue.id);
      std::string cueIdShort = toUpper(cue.cueId);
      std::string cueNum = toUpper(cue.cueNumber);
      std::string cueName = toUpper(cue.name);
      return cueId.rfind(needle, 0) == 0
        || cueIdShort.rfind(needle, 0) == 0
        || cueNum.rfind(needle, 0) == 0
        || cueName.rfind(needle, 0) == 0;
    };
    auto isContains = [&](const Cue& cue) {
      std::string cueId = toUpper(cue.id);
      std::string cueIdShort = toUpper(cue.cueId);
      std::string cueNum = toUpper(cue.cueNumber);
      std::string cueName = toUpper(cue.name);
      return cueId.find(needle) != std::string::npos ||
        cueIdShort.find(needle) != std::string::npos ||
        cueNum.find(needle) != std::string::npos ||
        cueName.find(needle) != std::string::npos;
    };

    if (auto direct = cueIndexByToken(deck, token); direct) {
      pushMatch(*direct);
    }
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      if (isExact(deck.cues[cueIndex])) {
        pushMatch(cueIndex);
      }
    }
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      if (isPrefix(deck.cues[cueIndex])) {
        pushMatch(cueIndex);
      }
    }
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      if (isContains(deck.cues[cueIndex])) {
        pushMatch(cueIndex);
      }
    }

    return matches;
  }

  bool findCueToken(const std::string& token, int direction = 1, bool triggerJump = false) {
    normalizeProject(project_);
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) {
      triggerToast("find: empty deck");
      return false;
    }
    std::string trimmed = trim(token);
    if (trimmed.empty()) {
      triggerToast("find: enter cue id/number/name");
      return false;
    }

    bool tokenChanged = toUpper(trimmed) != toUpper(lastCueFindToken_) ||
      lastCueFindDeckIndex_ != project_.focusedDeckIndex || lastCueFindMatches_.empty();
    if (tokenChanged) {
      lastCueFindToken_ = trimmed;
      lastCueFindDeckIndex_ = project_.focusedDeckIndex;
      lastCueFindMatches_ = cueFindMatches(deck, trimmed);
      lastCueFindCursor_ = -1;
    }
    if (lastCueFindMatches_.empty()) {
      triggerToast("find: none");
      return false;
    }

    if (lastCueFindCursor_ < 0 || tokenChanged) {
      lastCueFindCursor_ = direction < 0
        ? static_cast<int>(lastCueFindMatches_.size()) - 1
        : 0;
    } else if (direction != 0) {
      int count = static_cast<int>(lastCueFindMatches_.size());
      lastCueFindCursor_ = (lastCueFindCursor_ + (direction > 0 ? 1 : -1) + count) % count;
    }

    int cueIndex = lastCueFindMatches_[lastCueFindCursor_];
    if (deck.selectedIndex != cueIndex) {
      deck.selectedIndex = cueIndex;
      onSelectionChanged();
      markProjectDirty();
    }

    std::string cueNum = cueDisplayToken(deck.cues[cueIndex], cueIndex);
    triggerToast("find " + std::to_string(lastCueFindCursor_ + 1) + "/" +
      std::to_string(lastCueFindMatches_.size()) + " -> " + cueNum + " " + deck.cues[cueIndex].name);
    if (triggerJump) {
      jumpSelectedCue();
    }
    return true;
  }

  void clearCueFindState() {
    lastCueFindToken_.clear();
    lastCueFindMatches_.clear();
    lastCueFindCursor_ = -1;
    lastCueFindDeckIndex_ = -1;
  }

  bool handleCueTypeAheadKey(SDL_Keycode key, Uint16 mod) {
    if ((mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) != 0) {
      return false;
    }
    auto toSearchChar = [&](SDL_Keycode code) -> char {
      if (code >= SDLK_0 && code <= SDLK_9) {
        return static_cast<char>('0' + (code - SDLK_0));
      }
      if (code >= SDLK_a && code <= SDLK_z) {
        return static_cast<char>('A' + (code - SDLK_a));
      }
      if (code == SDLK_MINUS) return '-';
      if (code == SDLK_UNDERSCORE) return '_';
      return '\0';
    };

    Uint64 now = SDL_GetTicks64();
    if (now > typedCueSearchLastKeyAtMs_ + 1200) {
      typedCueSearchBuffer_.clear();
    }
    typedCueSearchLastKeyAtMs_ = now;

    if (key == SDLK_BACKSPACE) {
      if (!typedCueSearchBuffer_.empty()) {
        typedCueSearchBuffer_.pop_back();
        if (!typedCueSearchBuffer_.empty()) {
          findCueToken(typedCueSearchBuffer_, 1, false);
          triggerToast("id: " + typedCueSearchBuffer_);
        } else {
          triggerToast("id: cleared");
        }
      }
      return true;
    }

    char ch = toSearchChar(key);
    if (ch == '\0') {
      return false;
    }
    typedCueSearchBuffer_.push_back(ch);
    if (typedCueSearchBuffer_.size() > 6) {
      typedCueSearchBuffer_.erase(typedCueSearchBuffer_.begin());
    }
    findCueToken(typedCueSearchBuffer_, 1, false);
    triggerToast("id: " + typedCueSearchBuffer_);
    return true;
  }

  void renumberFocusedDeckCueNumbers(const std::string& prefix = "", int startAt = 1) {
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) {
      triggerToast("renumber: empty deck");
      return;
    }
    int start = std::max(1, startAt);
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      std::string token = prefix + std::to_string(start + cueIndex);
      deck.cues[cueIndex].cueNumber = token;
      deck.cues[cueIndex].cueId = normalizeCueIdShort(token);
    }
    clearCueFindState();
    triggerToast("renumbered " + std::to_string(deck.cues.size()) + " cues");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void clearFocusedDeckCueNumbers() {
    Deck& deck = focusedDeckMutable();
    if (deck.cues.empty()) {
      return;
    }
    for (auto& cue : deck.cues) {
      cue.cueNumber.clear();
      cue.cueId.clear();
    }
    clearCueFindState();
    triggerToast("cue numbers cleared");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool selectCueById(const std::string& cueId) {
    Deck& deck = focusedDeckMutable();
    auto index = cueIndexById(deck, cueId);
    if (!index) {
      return false;
    }
    selectCueInDeck(project_.focusedDeckIndex, *index, false, false);
    triggerToast("cue " + std::to_string(*index + 1) + " armed");
    return true;
  }

  bool takeCueById(const std::string& cueId, bool autoplay) {
    Deck& deck = focusedDeckMutable();
    auto index = cueIndexById(deck, cueId);
    if (!index) {
      return false;
    }
    selectCueInDeck(project_.focusedDeckIndex, *index, false, false);
    takeSelected(autoplay);
    return true;
  }

  double snapToCueFrame(const Cue& cue, double seconds) const {
    double duration = std::max(0.0, cue.duration);
    double clamped = std::clamp(seconds, 0.0, duration);
    double fps = cue.fps;
    if (!std::isfinite(fps) || fps <= 0.0) {
      return clamped;
    }
    double frameIndex = std::round(clamped * fps);
    double snapped = frameIndex / fps;
    return std::clamp(snapped, 0.0, duration);
  }

  void setSelectedTrimIn(double seconds) {
    if (!firstFocusedSelectedCueMutable([&](const Cue& cue) {
      return cue.kind == CueKind::Video;
    })) {
      return;
    }
    double sampleNext = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video) {
        return;
      }
      double duration = std::max(0.0, cue.duration);
      double next = snapToCueFrame(cue, seconds);
      double out = cue.outPointSeconds > 0.0 ? cue.outPointSeconds : duration;
      out = std::clamp(snapToCueFrame(cue, out), next, duration);
      cue.inPointSeconds = next;
      cue.outPointSeconds = out;
      if (!changed) {
        sampleNext = next;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("in " + formatSeconds(sampleNext));
    markProjectDirty();
  }

  void setSelectedTrimOut(double seconds) {
    if (!firstFocusedSelectedCueMutable([&](const Cue& cue) {
      return cue.kind == CueKind::Video;
    })) {
      return;
    }
    double sampleNext = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video) {
        return;
      }
      double duration = std::max(0.0, cue.duration);
      double next = std::clamp(snapToCueFrame(cue, seconds), cue.inPointSeconds, duration);
      cue.outPointSeconds = next;
      if (!changed) {
        sampleNext = next;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("out " + formatSeconds(sampleNext));
    markProjectDirty();
  }

  void clearSelectedTrim() {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video) {
        return;
      }
      cue.inPointSeconds = 0.0;
      cue.outPointSeconds = cue.duration;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("trim reset");
    markProjectDirty();
  }

  bool setActiveTrimFromPlayhead(bool setInPoint) {
    Deck& deck = focusedDeckMutable();
    if (deck.activeIndex < 0 || deck.activeIndex >= static_cast<int>(deck.cues.size())) {
      return false;
    }
    Cue& cue = deck.cues[deck.activeIndex];
    if (cue.kind != CueKind::Video) {
      return false;
    }
    MediaEngine* engine = focusedMediaEngine();
    if (!engine) {
      return false;
    }
    double duration = std::max(0.0, cue.duration);
    double playhead = snapToCueFrame(cue, engine->position());
    if (setInPoint) {
      double out = cue.outPointSeconds > 0.0 ? cue.outPointSeconds : duration;
      cue.inPointSeconds = playhead;
      cue.outPointSeconds = std::clamp(snapToCueFrame(cue, out), cue.inPointSeconds, duration);
      triggerToast("in " + formatSeconds(cue.inPointSeconds));
    } else {
      cue.outPointSeconds = std::clamp(playhead, cue.inPointSeconds, duration);
      triggerToast("out " + formatSeconds(cue.outPointSeconds));
    }
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  bool nudgeFocusedPausedPlayback(int direction, Uint16 mod) {
    if (direction == 0) {
      return false;
    }
    MediaEngine* engine = focusedMediaEngine();
    const Cue* cue = activeCuePtr();
    if (!engine || !cue || cue->kind != CueKind::Video) {
      return false;
    }
    if (engine->state() != TransportState::Paused) {
      return false;
    }
    double fps = (std::isfinite(cue->fps) && cue->fps > 1.0) ? cue->fps : 30.0;
    double stepSeconds = 1.0 / fps;
    std::string stepLabel = "1f";
    if ((mod & KMOD_ALT) != 0) {
      stepSeconds = 1.0;
      stepLabel = "1s";
    } else if ((mod & KMOD_CTRL) != 0) {
      stepSeconds = 10.0 / fps;
      stepLabel = "10f";
    } else if ((mod & KMOD_SHIFT) != 0) {
      stepSeconds = 5.0 / fps;
      stepLabel = "5f";
    }
    double target = snapToCueFrame(*cue, engine->position() + static_cast<double>(direction) * stepSeconds);
    engine->seek(target);
    triggerToast((direction > 0 ? ">> " : "<< ") + stepLabel + "  " + formatTimecode(target, fps));
    playUiSound(UiSoundEffect::Toggle);
    return true;
  }

  void setTimeOverlayEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.timeOverlayEnabled == enabled) {
      return;
    }
    deck.timeOverlayEnabled = enabled;
    triggerToast(deck.timeOverlayEnabled ? "time overlay on" : "time overlay off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleTimeOverlayEnabled() {
    setTimeOverlayEnabled(!focusedDeck().timeOverlayEnabled);
  }

  void setTransitionSeconds(double seconds) {
    Deck& deck = focusedDeckMutable();
    double next = std::clamp(seconds, 0.0, 10.0);
    if (std::abs(deck.transitionSeconds - next) < 0.001) {
      return;
    }
    deck.transitionSeconds = next;
    triggerToast("transition " + formatSeconds(deck.transitionSeconds));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setTransitionStyle(TransitionStyle style) {
    Deck& deck = focusedDeckMutable();
    std::string token = transitionStyleToken(style);
    if (deck.transitionStyle == token) {
      return;
    }
    deck.transitionStyle = token;
    triggerToast("style " + deck.transitionStyle);
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool setSelectedCueTimecodeTrigger(double seconds) {
    double clamped = std::max(0.0, seconds);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      cue.triggerTimecodeSeconds = clamped;
      changed = true;
    });
    if (!changed) {
      return false;
    }
    triggerToast("tc mark " + formatTimecode(clamped, focusedDeck().timecodeFps));
    markProjectDirty();
    return true;
  }

  void clearSelectedCueTimecodeTrigger() {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      cue.triggerTimecodeSeconds = -1.0;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("tc mark cleared");
    markProjectDirty();
  }

  void ensureTimecodeFollowerStateSize() {
    size_t deckCount = project_.decks.size();
    if (deckTimecodeLastExternalMs_.size() != deckCount) {
      deckTimecodeLastExternalMs_.resize(deckCount, 0);
    }
    if (deckTimecodeLastExternalSeconds_.size() != deckCount) {
      deckTimecodeLastExternalSeconds_.resize(deckCount, 0.0);
    }
    if (deckTimecodeHasExternal_.size() != deckCount) {
      deckTimecodeHasExternal_.resize(deckCount, false);
    }
  }

  void resetTimecodeFollowerState() {
    ensureTimecodeFollowerStateSize();
    std::fill(deckTimecodeLastExternalMs_.begin(), deckTimecodeLastExternalMs_.end(), 0);
    std::fill(deckTimecodeLastExternalSeconds_.begin(), deckTimecodeLastExternalSeconds_.end(), 0.0);
    std::fill(deckTimecodeHasExternal_.begin(), deckTimecodeHasExternal_.end(), false);
  }

  void setTimecodeFps(double fps) {
    Deck& deck = focusedDeckMutable();
    double next = std::clamp(fps, 1.0, 120.0);
    if (std::abs(deck.timecodeFps - next) < 0.001) {
      return;
    }
    deck.timecodeFps = next;
    triggerToast("tc fps " + std::to_string(static_cast<int>(std::round(deck.timecodeFps))));
    markProjectDirty();
  }

  void setTimecodeChaseEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.timecodeChaseEnabled == enabled) {
      return;
    }
    deck.timecodeChaseEnabled = enabled;
    triggerToast(deck.timecodeChaseEnabled ? "tc chase on" : "tc chase off");
    markProjectDirty();
  }

  void setTimecodeRunEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.timecodeRunEnabled == enabled) {
      return;
    }
    deck.timecodeRunEnabled = enabled;
    triggerToast(deck.timecodeRunEnabled ? "tc run on" : "tc run off");
    markProjectDirty();
  }

  void setTimecodeJamSyncEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.timecodeJamSyncEnabled == enabled) {
      triggerToast(enabled ? "tc jam on" : "tc jam off");
      return;
    }
    deck.timecodeJamSyncEnabled = enabled;
    triggerToast(deck.timecodeJamSyncEnabled ? "tc jam on" : "tc jam off");
    markProjectDirty();
  }

  void setTimecodeFreewheelSeconds(double seconds) {
    Deck& deck = focusedDeckMutable();
    double next = std::clamp(std::isfinite(seconds) ? seconds : 1.0, 0.0, 10.0);
    if (std::abs(deck.timecodeFreewheelSeconds - next) < 0.001) {
      std::ostringstream label;
      label << std::fixed << std::setprecision(1) << next;
      triggerToast("tc freewheel " + label.str() + "s");
      return;
    }
    deck.timecodeFreewheelSeconds = next;
    std::ostringstream label;
    label << std::fixed << std::setprecision(1) << deck.timecodeFreewheelSeconds;
    triggerToast("tc freewheel " + label.str() + "s");
    markProjectDirty();
  }

  void setDeckTimecode(int deckIndex, double seconds, bool forceApply = false) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    ensureTimecodeFollowerStateSize();
    Deck& deck = project_.decks[deckIndex];
    double normalized = std::max(0.0, std::isfinite(seconds) ? seconds : 0.0);
    Uint64 now = SDL_GetTicks64();
    bool hadExternal = deckTimecodeHasExternal_[deckIndex];
    Uint64 previousExternalMs = deckTimecodeLastExternalMs_[deckIndex];
    Uint64 freewheelMs = static_cast<Uint64>(
      std::llround(std::max(0.0, deck.timecodeFreewheelSeconds) * 1000.0));
    bool withinFreewheel = hadExternal && now >= previousExternalMs && (now - previousExternalMs) <= freewheelMs;
    bool suppressJam = !forceApply
      && !deck.timecodeJamSyncEnabled
      && deck.timecodeChaseEnabled
      && deck.timecodeRunEnabled
      && withinFreewheel;

    deckTimecodeHasExternal_[deckIndex] = true;
    deckTimecodeLastExternalMs_[deckIndex] = now;
    deckTimecodeLastExternalSeconds_[deckIndex] = normalized;
    if (suppressJam) {
      return;
    }

    if (normalized + 0.0001 < deck.timecodeCurrentSeconds) {
      timecodeTriggeredCueIds_.erase(deckIndex);
    }
    deck.timecodeLastSeconds = deck.timecodeCurrentSeconds;
    deck.timecodeCurrentSeconds = normalized;
    deck.timecodeDirty = true;
  }

  void processTimecodeTriggersForDeck(int deckIndex, double fromSeconds, double toSeconds) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    if (deck.cues.empty()) {
      return;
    }

    auto& fired = timecodeTriggeredCueIds_[deckIndex];
    if (toSeconds + 0.0001 < fromSeconds) {
      fired.clear();
      return;
    }

    std::optional<int> bestIndex;
    double bestTc = 0.0;
    for (int cueIndex = 0; cueIndex < static_cast<int>(deck.cues.size()); ++cueIndex) {
      const Cue& cue = deck.cues[cueIndex];
      if (cue.triggerTimecodeSeconds < 0.0) {
        continue;
      }
      if (cue.triggerTimecodeSeconds <= toSeconds + 0.0001 && cue.triggerTimecodeSeconds > fromSeconds + 0.0001) {
        if (fired.find(cue.id) != fired.end()) {
          continue;
        }
        if (!bestIndex || cue.triggerTimecodeSeconds < bestTc) {
          bestIndex = cueIndex;
          bestTc = cue.triggerTimecodeSeconds;
        }
      }
    }

    if (!bestIndex) {
      return;
    }

    fired.insert(deck.cues[*bestIndex].id);
    deck.selectedIndex = *bestIndex;
    int previousFocus = project_.focusedDeckIndex;
    project_.focusedDeckIndex = deckIndex;
    takeSelected(true);
    project_.focusedDeckIndex = previousFocus;
  }

  void setFocusedDeckTimecode(double seconds, bool forceApply = false) {
    setDeckTimecode(project_.focusedDeckIndex, seconds, forceApply);
    triggerToast("tc " + formatTimecode(focusedDeck().timecodeCurrentSeconds, focusedDeck().timecodeFps));
  }

  void cycleSelectedEndAction() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.kind == CueKind::Video || each.kind == CueKind::Audio;
    });
    if (!cue) {
      return;
    }
    CueEndAction next = cue->endAction;
    // Cycle: Inherit → Stop → Loop → PauseOnLast → AutoNext → Inherit
    switch (next) {
      case CueEndAction::Inherit:     next = CueEndAction::Stop;       break;
      case CueEndAction::Stop:        next = CueEndAction::Loop;       break;
      case CueEndAction::Loop:        next = CueEndAction::PauseOnLast; break;
      case CueEndAction::PauseOnLast: next = CueEndAction::AutoNext;   break;
      case CueEndAction::AutoNext:    next = CueEndAction::Inherit;    break;
    }
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.kind != CueKind::Video && each.kind != CueKind::Audio) {
        return;
      }
      each.endAction = next;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("end: " + cueEndActionLabel(next));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedEndAction(CueEndAction action) {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.kind == CueKind::Video || each.kind == CueKind::Audio;
    });
    if (!cue || cue->endAction == action) {
      return;
    }
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.kind != CueKind::Video && each.kind != CueKind::Audio) {
        return;
      }
      each.endAction = action;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("end: " + cueEndActionLabel(action));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool cueSupportsGeometry(const Cue* cue) const {
    return cue && (cue->kind == CueKind::Video
      || cue->kind == CueKind::Image
      || cue->kind == CueKind::Pattern
      || cue->kind == CueKind::Browser
      || isSourceCueKind(cue->kind));
  }

  bool cueSupportsKeying(const Cue* cue) const {
    return cue && (cue->kind == CueKind::Video
      || cue->kind == CueKind::Image
      || cue->kind == CueKind::Pattern
      || cue->kind == CueKind::Browser
      || isSourceCueKind(cue->kind));
  }

  bool cueSupportsColorControls(const Cue* cue) const {
    return cueSupportsKeying(cue);
  }

  std::optional<double> promptNumericExpression(const std::string& title,
                                                const std::string& prompt,
                                                const std::string& currentValue) {
    auto text = pickTextInput(title, prompt, currentValue);
    if (!text) {
      return std::nullopt;
    }
    auto parsed = parseNumericExpression(*text);
    if (!parsed) {
      triggerToast("invalid number");
      return std::nullopt;
    }
    return parsed;
  }

  void editSelectedScaleX() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    std::ostringstream current;
    current << std::fixed << std::setprecision(2) << cue->outputScaleX;
    auto parsed = promptNumericExpression("Scale X", "Factor 0.25-4.0 (supports + - * / and ())", current.str());
    if (!parsed) {
      return;
    }
    float next = std::clamp(static_cast<float>(*parsed), 0.25f, 4.0f);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsGeometry(&each)) {
        return;
      }
      each.outputScaleX = next;
      changed = true;
    });
    if (!changed) {
      return;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << next;
    triggerToast("scale X " + ss.str() + "x");
    markProjectDirty();
  }

  void editSelectedScaleY() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    std::ostringstream current;
    current << std::fixed << std::setprecision(2) << cue->outputScaleY;
    auto parsed = promptNumericExpression("Scale Y", "Factor 0.25-4.0 (supports + - * / and ())", current.str());
    if (!parsed) {
      return;
    }
    float next = std::clamp(static_cast<float>(*parsed), 0.25f, 4.0f);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsGeometry(&each)) {
        return;
      }
      each.outputScaleY = next;
      changed = true;
    });
    if (!changed) {
      return;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << next;
    triggerToast("scale Y " + ss.str() + "x");
    markProjectDirty();
  }

  void editSelectedOffsetX() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    int current = static_cast<int>(std::lround(cue->outputOffsetX));
    auto parsed = promptNumericExpression("Offset X", "Pixels (supports + - * / and ())", std::to_string(current));
    if (!parsed) {
      return;
    }
    float next = static_cast<float>(std::lround(*parsed));
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsGeometry(&each)) {
        return;
      }
      each.outputOffsetX = next;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("off X " + std::to_string(static_cast<int>(std::lround(next))) + "px");
    markProjectDirty();
  }

  void editSelectedOffsetY() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    int current = static_cast<int>(std::lround(cue->outputOffsetY));
    auto parsed = promptNumericExpression("Offset Y", "Pixels (supports + - * / and ())", std::to_string(current));
    if (!parsed) {
      return;
    }
    float next = static_cast<float>(std::lround(*parsed));
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsGeometry(&each)) {
        return;
      }
      each.outputOffsetY = next;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("off Y " + std::to_string(static_cast<int>(std::lround(next))) + "px");
    markProjectDirty();
  }

  void editSelectedRotation() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    std::ostringstream current;
    current << std::fixed << std::setprecision(1) << cue->outputRotationDegrees;
    auto parsed = promptNumericExpression("Rotation", "Degrees -180..180 (supports + - * / and ())", current.str());
    if (!parsed) {
      return;
    }
    float next = std::clamp(static_cast<float>(*parsed), -180.0f, 180.0f);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsGeometry(&each)) {
        return;
      }
      each.outputRotationDegrees = next;
      changed = true;
    });
    if (!changed) {
      return;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << next;
    triggerToast("rot " + ss.str() + " deg");
    markProjectDirty();
  }

  void normalizeCueCrop(Cue& cue) {
    cue.cropLeft = std::clamp(cue.cropLeft, 0.0f, 0.90f);
    cue.cropRight = std::clamp(cue.cropRight, 0.0f, 0.90f);
    cue.cropTop = std::clamp(cue.cropTop, 0.0f, 0.90f);
    cue.cropBottom = std::clamp(cue.cropBottom, 0.0f, 0.90f);
    cue.cropRight = std::min(cue.cropRight, std::max(0.0f, 0.95f - cue.cropLeft));
    cue.cropBottom = std::min(cue.cropBottom, std::max(0.0f, 0.95f - cue.cropTop));
  }

  void adjustSelectedRotation(float deltaDegrees) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      cue.outputRotationDegrees = std::clamp(cue.outputRotationDegrees + deltaDegrees, -180.0f, 180.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedCrop(char edge, float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      switch (edge) {
        case 'L': cue.cropLeft += delta; break;
        case 'R': cue.cropRight += delta; break;
        case 'T': cue.cropTop += delta; break;
        case 'B': cue.cropBottom += delta; break;
        default: return;
      }
      normalizeCueCrop(cue);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void setSelectedChromaKeyEnabled(bool enabled) {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return cueSupportsKeying(&each);
    });
    if (!cue) {
      return;
    }
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsKeying(&each)) {
        return;
      }
      if (each.chromaKeyEnabled == enabled) {
        return;
      }
      each.chromaKeyEnabled = enabled;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast(enabled ? "key on" : "key off");
    markProjectDirty();
  }

  void toggleSelectedChromaKey() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return cueSupportsKeying(&each);
    });
    if (!cue) {
      return;
    }
    setSelectedChromaKeyEnabled(!cue->chromaKeyEnabled);
  }

  void adjustSelectedKeyTolerance(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsKeying(&cue)) {
        return;
      }
      cue.chromaKeyTolerance = std::clamp(cue.chromaKeyTolerance + delta, 0.0f, 441.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedKeySoftness(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsKeying(&cue)) {
        return;
      }
      cue.chromaKeySoftness = std::clamp(cue.chromaKeySoftness + delta, 0.0f, 200.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void editSelectedKeyColor() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return cueSupportsKeying(&each);
    });
    if (!cue) {
      return;
    }
    auto value = pickTextInput("Key Color", "Hex color (#RRGGBB)", colorToHex(cue->chromaKeyColor));
    if (!value) {
      return;
    }
    SDL_Color color = parseColor(*value);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsKeying(&each)) {
        return;
      }
      each.chromaKeyColor = color;
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedBrightness(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsColorControls(&cue)) {
        return;
      }
      cue.brightness = std::clamp(cue.brightness + delta, 0.0f, 2.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedContrast(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsColorControls(&cue)) {
        return;
      }
      cue.contrast = std::clamp(cue.contrast + delta, 0.0f, 2.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedSaturation(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsColorControls(&cue)) {
        return;
      }
      cue.saturation = std::clamp(cue.saturation + delta, 0.0f, 2.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedHueShift(float deltaDegrees) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsColorControls(&cue)) {
        return;
      }
      cue.hueShift = std::clamp(cue.hueShift + deltaDegrees, -180.0f, 180.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedScaleX(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      cue.outputScaleX = std::clamp(cue.outputScaleX + delta, 0.25f, 4.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedScaleY(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      cue.outputScaleY = std::clamp(cue.outputScaleY + delta, 0.25f, 4.0f);
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedOffsetX(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      cue.outputOffsetX += delta;
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void adjustSelectedOffsetY(float delta) {
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (!cueSupportsGeometry(&cue)) {
        return;
      }
      cue.outputOffsetY += delta;
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void cycleSelectedScaleMode() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return cueSupportsGeometry(&each);
    });
    if (!cue) {
      return;
    }
    ScaleMode next = static_cast<ScaleMode>((static_cast<int>(cue->scaleMode) + 1) % 4);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (!cueSupportsGeometry(&each)) {
        return;
      }
      each.scaleMode = next;
      changed = true;
    });
    if (!changed) {
      return;
    }
    markProjectDirty();
  }

  void dispatchQuickAction(QuickAction action) {
    switch (action) {
      case QuickAction::ToggleLoop:      toggleSelectedLoop(); break;
      case QuickAction::ToggleHold:      toggleSelectedPauseOnLastFrame(); break;
      case QuickAction::TogglePauseBegin: toggleSelectedPauseAtBeginning(); break;
      case QuickAction::ToggleCueAudio:   toggleSelectedAudioEnabled(); break;
      case QuickAction::ToggleNextTransition: toggleSelectedTransitionToNext(); break;
      case QuickAction::EditGotoTarget: {
        Cue* sel = selectedCueMutable();
        if (!sel) {
          break;
        }
        auto result = pickTextInput("Cue Goto", "Target cue token on end (blank = next):", sel->gotoTarget);
        if (result) {
          setSelectedGotoTarget(*result);
        }
        break;
      }
      case QuickAction::CycleEndAction:  cycleSelectedEndAction(); break;
      case QuickAction::FadeInDec:       adjustSelectedFade(true,  -0.25); break;
      case QuickAction::FadeInInc:       adjustSelectedFade(true,   0.25); break;
      case QuickAction::FadeOutDec:      adjustSelectedFade(false, -0.25); break;
      case QuickAction::FadeOutInc:      adjustSelectedFade(false,  0.25); break;
      case QuickAction::InDec:           adjustSelectedIn(-0.5); break;
      case QuickAction::InInc:           adjustSelectedIn( 0.5); break;
      case QuickAction::OutDec:          adjustSelectedOut(-0.5); break;
      case QuickAction::OutInc:          adjustSelectedOut( 0.5); break;
      case QuickAction::TransDec:        adjustSelectedCueTransition(-0.25); break;
      case QuickAction::TransInc:        adjustSelectedCueTransition( 0.25); break;
      case QuickAction::CycleTransStyle: cycleSelectedCueTransStyle(); break;
      case QuickAction::LowerBgDec:      adjustSelectedLowerAlpha(-16); break;
      case QuickAction::LowerBgInc:      adjustSelectedLowerAlpha( 16); break;
      case QuickAction::DurDec:          adjustSelectedStillDuration(-1.0); break;
      case QuickAction::DurInc:          adjustSelectedStillDuration( 1.0); break;
      case QuickAction::LoopCountDec:    adjustSelectedLoopCount(-1); break;
      case QuickAction::LoopCountInc:    adjustSelectedLoopCount( 1); break;
      case QuickAction::SpeedDec:        adjustSelectedSpeed(-0.25); break;
      case QuickAction::SpeedInc:        adjustSelectedSpeed( 0.25); break;
      case QuickAction::CycleColorTag:   cycleSelectedColorTag(); break;
      case QuickAction::CycleScaleMode:  cycleSelectedScaleMode(); break;
      case QuickAction::EditNotes: {
        Cue* sel = selectedCueMutable();
        if (sel) {
          auto result = pickTextInput("Cue Notes", "Enter notes for selected cue(s):", sel->notes);
          if (result) {
            if (forEachFocusedSelectedCueMutable([&](Cue& cue, int) { cue.notes = *result; })) {
              markProjectDirty();
            }
          }
        }
        break;
      }
      case QuickAction::GotoMinus10:
      case QuickAction::GotoMinus20:
      case QuickAction::GotoMinus30: {
        MediaEngine* eng = focusedMediaEngine();
        if (eng) {
          double dur = eng->duration();
          double offset = (action == QuickAction::GotoMinus10) ? 10.0
                        : (action == QuickAction::GotoMinus20) ? 20.0 : 30.0;
          double target = std::max(0.0, dur - offset);
          eng->seek(target);
          triggerToast("jumped to -" + std::to_string(static_cast<int>(offset)) + "s");
        }
        break;
      }
      case QuickAction::ScaleXDec:
        adjustSelectedScaleX(-0.05f);
        break;
      case QuickAction::ScaleXInc:
        adjustSelectedScaleX(0.05f);
        break;
      case QuickAction::ScaleYDec:
        adjustSelectedScaleY(-0.05f);
        break;
      case QuickAction::ScaleYInc:
        adjustSelectedScaleY(0.05f);
        break;
      case QuickAction::EditScaleX:
        editSelectedScaleX();
        break;
      case QuickAction::EditScaleY:
        editSelectedScaleY();
        break;
      case QuickAction::OffsetXDec:
        adjustSelectedOffsetX(-1.0f);
        break;
      case QuickAction::OffsetXInc:
        adjustSelectedOffsetX(1.0f);
        break;
      case QuickAction::OffsetYDec:
        adjustSelectedOffsetY(-1.0f);
        break;
      case QuickAction::OffsetYInc:
        adjustSelectedOffsetY(1.0f);
        break;
      case QuickAction::EditOffsetX:
        editSelectedOffsetX();
        break;
      case QuickAction::EditOffsetY:
        editSelectedOffsetY();
        break;
      case QuickAction::RotDec:
        adjustSelectedRotation(-1.0f);
        break;
      case QuickAction::RotInc:
        adjustSelectedRotation(1.0f);
        break;
      case QuickAction::EditRotation:
        editSelectedRotation();
        break;
      case QuickAction::CropLDec:
        adjustSelectedCrop('L', -0.01f);
        break;
      case QuickAction::CropLInc:
        adjustSelectedCrop('L', 0.01f);
        break;
      case QuickAction::CropRDec:
        adjustSelectedCrop('R', -0.01f);
        break;
      case QuickAction::CropRInc:
        adjustSelectedCrop('R', 0.01f);
        break;
      case QuickAction::CropTDec:
        adjustSelectedCrop('T', -0.01f);
        break;
      case QuickAction::CropTInc:
        adjustSelectedCrop('T', 0.01f);
        break;
      case QuickAction::CropBDec:
        adjustSelectedCrop('B', -0.01f);
        break;
      case QuickAction::CropBInc:
        adjustSelectedCrop('B', 0.01f);
        break;
      case QuickAction::KeyToggle:
        toggleSelectedChromaKey();
        break;
      case QuickAction::KeyTolDec:
        adjustSelectedKeyTolerance(-5.0f);
        break;
      case QuickAction::KeyTolInc:
        adjustSelectedKeyTolerance(5.0f);
        break;
      case QuickAction::KeySoftDec:
        adjustSelectedKeySoftness(-2.0f);
        break;
      case QuickAction::KeySoftInc:
        adjustSelectedKeySoftness(2.0f);
        break;
      case QuickAction::EditKeyColor:
        editSelectedKeyColor();
        break;
      case QuickAction::BrightnessDec:
        adjustSelectedBrightness(-0.05f);
        break;
      case QuickAction::BrightnessInc:
        adjustSelectedBrightness(0.05f);
        break;
      case QuickAction::ContrastDec:
        adjustSelectedContrast(-0.05f);
        break;
      case QuickAction::ContrastInc:
        adjustSelectedContrast(0.05f);
        break;
      case QuickAction::SaturationDec:
        adjustSelectedSaturation(-0.05f);
        break;
      case QuickAction::SaturationInc:
        adjustSelectedSaturation(0.05f);
        break;
      case QuickAction::HueShiftDec:
        adjustSelectedHueShift(-5.0f);
        break;
      case QuickAction::HueShiftInc:
        adjustSelectedHueShift(5.0f);
        break;
      case QuickAction::PatternTypePrev:
        cycleSelectedPatternType(-1);
        break;
      case QuickAction::PatternTypeNext:
        cycleSelectedPatternType(1);
        break;
      case QuickAction::TogglePatternMotion:
        toggleSelectedPatternMotion();
        break;
      case QuickAction::CueSectionPlaybackToggle:
        cueSectionPlaybackOpen_ = !cueSectionPlaybackOpen_;
        break;
      case QuickAction::CueSectionGeometryToggle:
        cueSectionGeometryOpen_ = !cueSectionGeometryOpen_;
        break;
      case QuickAction::CueSectionKeyToggle:
        cueSectionKeyOpen_ = !cueSectionKeyOpen_;
        break;
      case QuickAction::CueSectionRoutingToggle:
        cueSectionRoutingOpen_ = !cueSectionRoutingOpen_;
        break;
      case QuickAction::CueRouteOutputPrev:
      case QuickAction::CueRouteOutputNext: {
        if (project_.decks.empty() || project_.outputs.empty()) {
          break;
        }
        int deckIndex = std::clamp(project_.focusedDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        int delta = (action == QuickAction::CueRouteOutputPrev) ? -1 : 1;
        int nextOutput = (routeOutput + delta + outputCount) % outputCount;
        moveDeckToOutput(deckIndex, nextOutput);
        setFocusedOutputIndex(nextOutput);
        break;
      }
      case QuickAction::CueRouteLayerDec:
      case QuickAction::CueRouteLayerInc: {
        if (project_.decks.empty() || project_.outputs.empty()) {
          break;
        }
        int deckIndex = std::clamp(project_.focusedDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        auto assignment = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (!assignment) {
          assignDeckToOutput(deckIndex, routeOutput);
          assignment = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        }
        if (!assignment) {
          break;
        }
        int currentLayer = std::clamp(project_.layerAssignments[*assignment].layerIndex, 0, 255);
        int delta = (action == QuickAction::CueRouteLayerDec) ? -1 : 1;
        setDeckOutputAssignmentLayer(deckIndex, routeOutput, currentLayer + delta);
        break;
      }
      case QuickAction::CueRouteAssignToggle: {
        if (project_.decks.empty() || project_.outputs.empty()) {
          break;
        }
        int deckIndex = std::clamp(project_.focusedDeckIndex, 0, static_cast<int>(project_.decks.size()) - 1);
        int outputCount = static_cast<int>(project_.outputs.size());
        auto primaryOut = primaryOutputIndexForDeck(deckIndex);
        int routeOutput = primaryOut
          ? *primaryOut
          : std::clamp(project_.focusedOutputIndex, 0, outputCount - 1);
        auto assignment = assignmentIndexForDeckOutput(deckIndex, routeOutput);
        if (assignment) {
          unassignDeckFromOutput(deckIndex, routeOutput);
        } else {
          assignDeckToOutput(deckIndex, routeOutput);
        }
        break;
      }
      case QuickAction::EditCueNumber: {
        Cue* sel = selectedCueMutable();
        if (sel) {
          std::string initial = sel->cueId.empty() ? cueDisplayToken(*sel, focusedDeck().selectedIndex) : sel->cueId;
          auto result = pickTextInput("Cue ID", "Short cue id (max 6 chars, letters/numbers):", initial);
          if (result) {
            std::string normalized = normalizeCueIdShort(*result);
            if (forEachFocusedSelectedCueMutable([&](Cue& cue, int) { cue.cueId = normalized; })) {
              markProjectDirty();
            }
          }
        }
        break;
      }
      case QuickAction::AddPausePoint: {
        Cue* sel = selectedCueMutable();
        if (sel) {
          MediaEngine* eng = focusedMediaEngine();
          double pos = eng ? eng->position() : 0.0;
          sel->pausePoints.push_back(pos);
          std::sort(sel->pausePoints.begin(), sel->pausePoints.end());
          // Update engine's live pause points
          if (eng) eng->setPausePoints(sel->pausePoints);
          triggerToast("pause point added at " + formatSeconds(pos));
          markProjectDirty();
        }
        break;
      }
      case QuickAction::ClearPausePoints: {
        Cue* sel = selectedCueMutable();
        if (sel && !sel->pausePoints.empty()) {
          sel->pausePoints.clear();
          if (MediaEngine* eng = focusedMediaEngine()) eng->setPausePoints({});
          triggerToast("pause points cleared");
          markProjectDirty();
        }
        break;
      }
      case QuickAction::VolDec:
        if (MediaEngine* eng = focusedMediaEngine()) {
          eng->setVolume(eng->volume() - 0.05f);
          triggerToast("vol " + std::to_string(static_cast<int>(std::round(eng->volume() * 100.0f))) + "%");
        }
        break;
      case QuickAction::VolInc:
        if (MediaEngine* eng = focusedMediaEngine()) {
          eng->setVolume(eng->volume() + 0.05f);
          triggerToast("vol " + std::to_string(static_cast<int>(std::round(eng->volume() * 100.0f))) + "%");
        }
        break;
    }
  }

  void toggleShuffle() {
    Deck& deck = focusedDeckMutable();
    deck.shuffle = !deck.shuffle;
    triggerToast(deck.shuffle ? "shuffle on" : "shuffle off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleSelectedLoop() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.kind == CueKind::Video || each.kind == CueKind::Audio;
    });
    if (!cue) {
      return;
    }
    bool next = !cue->loop;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.kind == CueKind::Video || each.kind == CueKind::Audio) {
        each.loop = next;
      }
    });
    triggerToast(next ? "loop on" : "loop off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedLoop(bool enabled) {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.kind == CueKind::Video || each.kind == CueKind::Audio;
    });
    if (!cue || cue->loop == enabled) {
      return;
    }
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.kind == CueKind::Video || each.kind == CueKind::Audio) {
        each.loop = enabled;
      }
    });
    triggerToast(enabled ? "loop on" : "loop off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleSelectedPauseOnLastFrame() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.kind == CueKind::Video || each.kind == CueKind::Audio;
    });
    if (!cue) {
      return;
    }
    bool next = !cue->pauseOnLastFrame;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.kind == CueKind::Video || each.kind == CueKind::Audio) {
        each.pauseOnLastFrame = next;
      }
    });
    triggerToast(next ? "hold on" : "hold off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedPauseOnLastFrame(bool enabled) {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.kind == CueKind::Video || each.kind == CueKind::Audio;
    });
    if (!cue || cue->pauseOnLastFrame == enabled) {
      return;
    }
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.kind == CueKind::Video || each.kind == CueKind::Audio) {
        each.pauseOnLastFrame = enabled;
      }
    });
    triggerToast(enabled ? "hold on" : "hold off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleSelectedPauseAtBeginning() {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    bool next = !cue->pauseAtBeginning;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.pauseAtBeginning = next;
    });
    triggerToast(next ? "pause at begin: on" : "pause at begin: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedPauseAtBeginning(bool enabled) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->pauseAtBeginning == enabled) {
      return;
    }
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.pauseAtBeginning = enabled;
    });
    triggerToast(enabled ? "pause at begin: on" : "pause at begin: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleSelectedAudioEnabled() {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.hasAudio;
    });
    if (!cue) {
      return;
    }
    bool next = !cue->audioEnabled;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.hasAudio) {
        each.audioEnabled = next;
      }
    });
    triggerToast(next ? "cue audio: on" : "cue audio: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedAudioEnabled(bool enabled) {
    Cue* cue = firstFocusedSelectedCueMutable([&](const Cue& each) {
      return each.hasAudio;
    });
    if (!cue || cue->audioEnabled == enabled) {
      return;
    }
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.hasAudio) {
        each.audioEnabled = enabled;
      }
    });
    triggerToast(enabled ? "cue audio: on" : "cue audio: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleSelectedTransitionToNext() {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    bool next = !cue->transitionToNext;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.transitionToNext = next;
    });
    triggerToast(next ? "next transition: on" : "next transition: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedTransitionToNext(bool enabled) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->transitionToNext == enabled) {
      return;
    }
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.transitionToNext = enabled;
    });
    triggerToast(enabled ? "next transition: on" : "next transition: off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedGotoTarget(const std::string& token) {
    std::string trimmed = trim(token);
    if (!forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.gotoTarget = trimmed;
    })) {
      return;
    }
    triggerToast(trimmed.empty() ? "goto target: cleared" : ("goto target: " + trimmed));
    markProjectDirty();
  }

  void adjustSelectedFade(bool fadeIn, double deltaSeconds) {
    Cue* cue = selectedCueMutable();
    if (!cue || (cue->kind != CueKind::Video && cue->kind != CueKind::Audio)) {
      return;
    }
    double sampleTarget = std::clamp((fadeIn ? cue->fadeInSeconds : cue->fadeOutSeconds) + deltaSeconds, 0.0, 10.0);
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.kind != CueKind::Video && each.kind != CueKind::Audio) {
        return;
      }
      double& target = fadeIn ? each.fadeInSeconds : each.fadeOutSeconds;
      target = std::clamp(target + deltaSeconds, 0.0, 10.0);
    });
    triggerToast(std::string(fadeIn ? "fade in " : "fade out ") + formatSeconds(sampleTarget));
    markProjectDirty();
  }

  void adjustSelectedIn(double delta) {
    double sample = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video) {
        return;
      }
      cue.inPointSeconds = std::clamp(cue.inPointSeconds + delta, 0.0, cue.duration > 0.0 ? cue.duration : 3600.0);
      if (!changed) {
        sample = cue.inPointSeconds;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("in " + formatSeconds(sample));
    markProjectDirty();
  }

  void adjustSelectedOut(double delta) {
    double sample = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video) {
        return;
      }
      double cur = cue.outPointSeconds > 0.0 ? cue.outPointSeconds : cue.duration;
      cur = std::clamp(cur + delta, 0.0, cue.duration > 0.0 ? cue.duration : 3600.0);
      cue.outPointSeconds = cur;
      if (!changed) {
        sample = cue.outPointSeconds;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("out " + formatSeconds(sample));
    markProjectDirty();
  }

  void adjustSelectedCueTransition(double delta) {
    double sample = 0.0;
    bool changed = false;
    double deckDefault = focusedDeck().transitionSeconds;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.cueTransitionSeconds < 0.0) {
        cue.cueTransitionSeconds = deckDefault;
      }
      cue.cueTransitionSeconds = std::clamp(cue.cueTransitionSeconds + delta, 0.0, 10.0);
      if (!changed) {
        sample = cue.cueTransitionSeconds;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("cue trans " + formatSeconds(sample));
    markProjectDirty();
  }

  void cycleSelectedCueTransStyle() {
    Cue* cue = selectedCueMutable();
    if (!cue) return;
    static const std::vector<std::string> kStyles = {"cut", "crossfade", "dip"};
    std::string cur = cue->cueTransitionStyle.empty() ? focusedDeck().transitionStyle : cue->cueTransitionStyle;
    auto it = std::find(kStyles.begin(), kStyles.end(), cur);
    std::string nextStyle = (it == kStyles.end() || std::next(it) == kStyles.end())
      ? kStyles.front()
      : *std::next(it);
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.cueTransitionStyle = nextStyle;
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("cue style: " + nextStyle);
    markProjectDirty();
  }

  void adjustSelectedLowerAlpha(int delta) {
    int sample = 0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::LowerThird) {
        return;
      }
      cue.lowerThirdBgAlpha = std::clamp(cue.lowerThirdBgAlpha + delta, 0, 255);
      if (!changed) {
        sample = cue.lowerThirdBgAlpha;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast("overlay alpha " + std::to_string(sample));
    markProjectDirty();
  }

  void adjustSelectedStillDuration(double delta) {
    double sample = 0.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind == CueKind::Video || cue.kind == CueKind::Audio) {
        return;
      }
      cue.stillDurationSeconds = std::max(0.0, cue.stillDurationSeconds + delta);
      if (cue.stillDurationSeconds < 0.5 && delta < 0) cue.stillDurationSeconds = 0.0;
      if (!changed) {
        sample = cue.stillDurationSeconds;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast(sample > 0.0
      ? "still dur " + formatSeconds(sample)
      : "still dur: hold");
    markProjectDirty();
  }

  void adjustSelectedLoopCount(int delta) {
    int sample = 0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video && cue.kind != CueKind::Audio) {
        return;
      }
      cue.loopCount = std::max(0, cue.loopCount + delta);
      if (!changed) {
        sample = cue.loopCount;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    triggerToast(sample == 0 ? "repeats: inf" : "repeats: " + std::to_string(sample) + "x");
    markProjectDirty();
  }

  void adjustSelectedSpeed(double delta) {
    double sample = 1.0;
    bool changed = false;
    forEachFocusedSelectedCueMutable([&](Cue& cue, int) {
      if (cue.kind != CueKind::Video && cue.kind != CueKind::Audio) {
        return;
      }
      cue.playbackSpeed = std::clamp(std::round((cue.playbackSpeed + delta) * 4.0) / 4.0, 0.25, 4.0);
      if (!changed) {
        sample = cue.playbackSpeed;
      }
      changed = true;
    });
    if (!changed) {
      return;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << sample;
    triggerToast("speed: " + ss.str() + "x");
    markProjectDirty();
  }

  void cycleSelectedColorTag() {
    Cue* cue = selectedCueMutable();
    if (!cue) return;
    std::string next = nextColorTag(cue->colorTag);
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      each.colorTag = next;
    });
    triggerToast("tag: " + (next.empty() ? "none" : next));
    markProjectDirty();
  }

  void clearOverlay() {
    Deck& deck = focusedDeckMutable();
    deck.overlayActiveIndices.clear();
    triggerToast("overlay cleared");
    markProjectDirty();
  }

  void popOverlay() {
    Deck& deck = focusedDeckMutable();
    if (!deck.overlayActiveIndices.empty()) {
      deck.overlayActiveIndices.pop_back();
      triggerToast("overlay popped");
      markProjectDirty();
    }
  }

  void setSelectedFade(bool fadeIn, double seconds) {
    Cue* cue = selectedCueMutable();
    if (!cue || (cue->kind != CueKind::Video && cue->kind != CueKind::Audio)) {
      return;
    }
    double next = std::clamp(seconds, 0.0, 10.0);
    forEachFocusedSelectedCueMutable([&](Cue& each, int) {
      if (each.kind == CueKind::Video || each.kind == CueKind::Audio) {
        double& target = fadeIn ? each.fadeInSeconds : each.fadeOutSeconds;
        target = next;
      }
    });
    triggerToast(std::string(fadeIn ? "fade in " : "fade out ") + formatSeconds(next));
    markProjectDirty();
  }

  void deleteSelected() {
    Deck& deck = focusedDeckMutable();
    auto indices = selectedCueIndices(deck);
    if (indices.empty()) {
      return;
    }
    std::sort(indices.begin(), indices.end());
    int firstDeleted = indices.front();
    bool removedActive = std::find(indices.begin(), indices.end(), deck.activeIndex) != indices.end();
    int activeShift = 0;
    for (int idx : indices) {
      if (idx < deck.activeIndex) {
        ++activeShift;
      }
    }
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
      deck.cues.erase(deck.cues.begin() + *it);
    }
    if (deck.cues.empty()) {
      deck.selectedIndex = -1;
      deck.activeIndex = -1;
      deck.selectedIndices.clear();
    } else {
      deck.selectedIndex = std::min(firstDeleted, static_cast<int>(deck.cues.size()) - 1);
      if (removedActive) {
        deck.activeIndex = -1;
        if (MediaEngine* engine = focusedMediaEngine()) {
          engine->clear();
        }
      } else if (deck.activeIndex >= 0) {
        deck.activeIndex = std::max(0, deck.activeIndex - activeShift);
      }
      deck.selectedIndices.clear();
      deck.selectedIndices.push_back(deck.selectedIndex);
    }
    if (indices.size() == 1) {
      triggerToast("cart popped");
    } else {
      triggerToast(std::to_string(indices.size()) + " carts popped");
    }
    playUiSound(UiSoundEffect::Delete);
    markProjectDirty();
  }

  void handleDropFile(const char* rawPath) {
    if (!rawPath) {
      return;
    }
    importPaths({rawPath});
  }

  void importWithPicker() {
    if (pendingImport_.valid() &&
        pendingImport_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      return;  // already picking
    }
    pendingImport_ = std::async(std::launch::async, [this] {
      return pickFiles();
    });
  }

  std::optional<fs::path> pickProjectPath(bool saveMode, std::string initialPath = {}) {
#ifdef _WIN32
    std::string script = saveMode
      ? "Add-Type -AssemblyName System.Windows.Forms;$dialog = New-Object System.Windows.Forms.SaveFileDialog;$dialog.Filter = 'Deckboy Playlist (*.playboy)|*.playboy';if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 };$dialog.FileName"
      : "Add-Type -AssemblyName System.Windows.Forms;$dialog = New-Object System.Windows.Forms.OpenFileDialog;$dialog.Filter = 'Deckboy Playlist (*.playboy)|*.playboy';if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 };$dialog.FileName";
    auto text = readAllText({"powershell.exe", "-NoProfile", "-Command", script});
#elif __APPLE__
    auto text = saveMode
      ? readAllText({
          "osascript",
          "-e",
          "set targetFile to choose file name with prompt \"Save Deckboy playlist\"",
          "-e",
          "return POSIX path of targetFile"
        })
      : readAllText({
          "osascript",
          "-e",
          "set pickedFile to choose file with prompt \"Open Deckboy playlist\"",
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
          "--title=Save Deckboy playlist",
          "--filename",
          initialPath
        })
      : readAllText({
          "zenity",
          "--file-selection",
          "--title=Open Deckboy playlist",
          "--filename",
          initialPath
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
    disarmAllOutputsForStartup();
    updateDecksPanelVisibility();
    timecodeTriggeredCueIds_.clear();
    resetTimecodeFollowerState();
    selectionChangedAt_ = SDL_GetTicks64();
    if (!rebuildDeckRuntimes()) {
      std::cerr << "Deck runtime creation failed: " << SDL_GetError() << '\n';
    }
    if (!rebuildOutputRuntimes()) {
      std::cerr << "Output runtime creation failed: " << SDL_GetError() << '\n';
    }
    triggerToast("playlist: " + currentProjectLabel());
  }

  void openProjectFromPicker() {
    if (pendingProjectOpen_.valid() &&
        pendingProjectOpen_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      return;
    }
    std::string ip = currentProjectFile_.string();  // capture on main thread — no race
    pendingProjectOpen_ = std::async(std::launch::async, [this, ip = std::move(ip)] {
      return pickProjectPath(false, ip);
    });
  }

  void saveProjectAsFromPicker() {
    if (pendingProjectSaveAs_.valid() &&
        pendingProjectSaveAs_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      return;
    }
    std::string ip = currentProjectFile_.string();  // capture on main thread — no race
    pendingProjectSaveAs_ = std::async(std::launch::async, [this, ip = std::move(ip)] {
      return pickProjectPath(true, ip);
    });
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

  std::optional<std::string> pickTextInput(const std::string& title,
                                            const std::string& prompt,
                                            const std::string& defaultVal = "") {
    auto shellEscape = [](const std::string& s) {
      std::string out;
      for (char ch : s) {
        if (ch == '\'') out += "'\"'\"'";
        else out += ch;
      }
      return out;
    };
    std::string safeTitle   = shellEscape(title);
    std::string safePrompt  = shellEscape(prompt);
    std::string safeDefault = shellEscape(defaultVal);
    std::string cmd = "zenity --entry --title='" + safeTitle +
                      "' --text='" + safePrompt +
                      "' --entry-text='" + safeDefault + "' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return std::nullopt;
    char buf[2048] = {};
    std::string result;
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    if (!result.empty() && result.back() == '\n') result.pop_back();
    if (result.empty()) return std::nullopt;
    return std::optional<std::string>(result);
  }

  std::optional<std::string> pickChoiceFromList(
      const std::string& title,
      const std::string& prompt,
      const std::vector<std::pair<std::string, std::string>>& options,
      const std::string& defaultValue = "") {
    if (options.empty()) {
      return std::nullopt;
    }
#ifdef __linux__
    std::vector<std::string> args {
      "zenity",
      "--list",
      "--title=" + title,
      "--text=" + prompt,
      "--column=Choice",
      "--column=Value",
      "--print-column=2",
      "--hide-column=2"
    };
    for (const auto& option : options) {
      args.push_back(option.first);
      args.push_back(option.second);
    }
    auto text = readAllText(args);
    if (text) {
      std::string value = trim(*text);
      if (!value.empty()) {
        return value;
      }
    }
#endif
    std::string fallbackPrompt = prompt + "\nEnter value token:";
    for (size_t i = 0; i < options.size() && i < 24; ++i) {
      fallbackPrompt += "\n" + options[i].second + "  -  " + options[i].first;
    }
    if (!defaultValue.empty()) {
      fallbackPrompt += "\n(default: " + defaultValue + ")";
    }
    return pickTextInput(title, fallbackPrompt, defaultValue);
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
      "--title=Import media into Deckboy Native"
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

  void applyDeckDefaultsToCue(Cue& cue, const Deck& deck) {
    cue.loop = deck.playlistDefaultLoop;
    cue.pauseAtBeginning = deck.playlistDefaultPauseAtBeginning;
    cue.pauseOnLastFrame = deck.playlistDefaultPauseAtEnd;
    cue.transitionToNext = deck.playlistDefaultTransitionToNext;
    cue.audioEnabled = cue.hasAudio ? deck.playlistDefaultAudioEnabled : false;

    double fadeDefault = std::clamp(deck.playlistDefaultCueFadeSeconds, 0.0, 10.0);
    cue.fadeInSeconds = deck.playlistDefaultFadeInEnabled ? fadeDefault : 0.0;
    cue.fadeOutSeconds = deck.playlistDefaultFadeOutEnabled ? fadeDefault : 0.0;

    if (isDefaultStillDurationCueKind(cue.kind)) {
      cue.stillDurationSeconds = std::clamp(deck.playlistDefaultStillDurationSeconds, 0.0, 3600.0);
    }
  }

  void editFocusedDeckPlaylistPreferences() {
    Deck& deck = focusedDeckMutable();
    bool changed = false;

    std::vector<std::pair<std::string, std::string>> fpsChoices {
      {"24", "24"},
      {"25", "25"},
      {"29.97", "29.97"},
      {"30", "30"},
    };
    auto fpsChoice = pickChoiceFromList(
      "Playlist Timebase",
      "Choose playlist SMPTE FPS",
      fpsChoices,
      playlistTimebaseLabel(deck.playlistTimebaseFps));
    if (fpsChoice) {
      try {
        double next = normalizePlaylistTimebaseFps(std::stod(trim(*fpsChoice)));
        if (std::fabs(deck.playlistTimebaseFps - next) > 0.001) {
          deck.playlistTimebaseFps = next;
          deck.timecodeFps = next;  // keep follower display base aligned with playlist base
          changed = true;
        }
      } catch (...) {
      }
    }

    std::string startDefault = formatTimecode(deck.playlistStartOffsetSeconds, deck.playlistTimebaseFps);
    auto startTc = pickTextInput(
      "Playlist Start TC",
      "SMPTE hh:mm:ss:ff or seconds (blank = 00:00:00:00)",
      startDefault);
    if (startTc) {
      std::string token = trim(*startTc);
      double parsed = 0.0;
      bool valid = false;
      if (token.empty()) {
        valid = true;
      } else if (auto tc = parseTimecodeSeconds(token, deck.playlistTimebaseFps)) {
        parsed = *tc;
        valid = true;
      } else {
        try {
          parsed = std::max(0.0, std::stod(token));
          valid = true;
        } catch (...) {
        }
      }
      if (valid) {
        parsed = std::clamp(parsed, 0.0, 24.0 * 60.0 * 60.0);
        if (std::fabs(deck.playlistStartOffsetSeconds - parsed) > 0.001) {
          deck.playlistStartOffsetSeconds = parsed;
          changed = true;
        }
      }
    }

    auto fadeValue = pickTextInput(
      "Default Cue Fade",
      "Seconds for new-cue fade in/out defaults",
      [&]() {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << deck.playlistDefaultCueFadeSeconds;
        return ss.str();
      }());
    if (fadeValue) {
      try {
        double next = std::clamp(std::stod(trim(*fadeValue)), 0.0, 10.0);
        if (std::fabs(deck.playlistDefaultCueFadeSeconds - next) > 0.001) {
          deck.playlistDefaultCueFadeSeconds = next;
          changed = true;
        }
      } catch (...) {
      }
    }

    auto stillValue = pickTextInput(
      "Default Non-Movie Duration",
      "Seconds for new image/pattern/browser/lower-third cues (0 = hold)",
      [&]() {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << deck.playlistDefaultStillDurationSeconds;
        return ss.str();
      }());
    if (stillValue) {
      try {
        double next = std::clamp(std::stod(trim(*stillValue)), 0.0, 3600.0);
        if (std::fabs(deck.playlistDefaultStillDurationSeconds - next) > 0.001) {
          deck.playlistDefaultStillDurationSeconds = next;
          changed = true;
        }
      } catch (...) {
      }
    }

    if (changed) {
      markProjectDirty();
      triggerToast(
        "playlist prefs: "
        + playlistTimebaseLabel(deck.playlistTimebaseFps)
        + " start "
        + formatTimecode(deck.playlistStartOffsetSeconds, deck.playlistTimebaseFps));
    }
  }

  void addBrowserCue(const std::string& rawUrl) {
    std::string url = normalizeBrowserUrl(rawUrl);
    if (url.empty()) {
      return;
    }
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);

    Cue cue;
    cue.kind = CueKind::Browser;
    cue.path = url;
    cue.name = browserCueNameForUrl(url);
    cue.width = rasterW;
    cue.height = rasterH;
    cue.color = SDL_Color {139, 172, 15, 255};
    cue.formatName = "browser";
    cue.videoCodec = "chromium";
    cue.audioCodec = "system";
    cue.hasAudio = true;
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("browser cue added");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  void addBrowserCueFromPrompt() {
    auto url = pickBrowserUrl();
    if (!url) {
      return;
    }
    addBrowserCue(*url);
  }

  void addSourceCue(CueKind kind, const std::string& rawRef) {
    if (!isSourceCueKind(kind)) {
      return;
    }
    std::string sourceRef = trim(rawRef);
    if (sourceRef.empty()) {
      if (kind == CueKind::WindowSource) {
        sourceRef = "active-window";
      } else if (kind == CueKind::Camera) {
        sourceRef = "default-camera";
      } else {
        sourceRef = "default-bus";
      }
    }
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
    Cue cue;
    cue.kind = kind;
    cue.path = "source://" + sourceCueTokenForKind(kind) + "/" + sourceRef;
    cue.name = cueKindLabel(kind) + " · " + sourceRef;
    cue.width = rasterW;
    cue.height = rasterH;
    cue.duration = 0.0;
    cue.stillDurationSeconds = 0.0;
    cue.hasAudio = (kind == CueKind::Camera);
    cue.formatName = "source";
    cue.videoCodec = sourceCueTokenForKind(kind);
    cue.audioCodec = cue.hasAudio ? "source" : "";
    cue.color = SDL_Color {139, 172, 15, 255};
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("source cue added");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  void addSourceCueFromMenu() {
    std::vector<std::pair<std::string, std::string>> choices {
      {"Window Source", "window"},
      {"Camera Source", "camera"},
#ifdef _WIN32
      {"Spout Source", "spout"}
#else
      {"Syphon Source", "syphon"}
#endif
    };
    auto picked = pickChoiceFromList("Add Source Cue", "Choose source type", choices, "window");
    if (!picked) {
      return;
    }
    std::string token = toLower(trim(*picked));
    CueKind kind = CueKind::WindowSource;
    std::string prompt = "Window title / id (blank = active-window)";
    std::string initial = "active-window";
    if (token == "camera" || token == "cam") {
      kind = CueKind::Camera;
      prompt = "Camera device path/name (blank = default-camera)";
      initial = "default-camera";
    } else if (token == "syphon" || token == "spout" || token == "siphon") {
      kind = CueKind::Syphon;
      prompt = "Syphon/Spout source id (blank = default-bus)";
      initial = "default-bus";
    }
    auto source = pickTextInput("Source Cue", prompt, initial);
    addSourceCue(kind, source ? *source : initial);
  }

  void addLowerThirdCue() {
    Cue cue;
    cue.kind = CueKind::LowerThird;
    cue.path = "graphic://lower-third";
    cue.name = "Lower Third";
    cue.lowerThirdText = "Lower Third Title";
    cue.lowerThirdSubtext = "";
    cue.lowerThirdBgAlpha = 180;
    cue.color = colorFromRgba(kScreenDeepColor);
    cue.formatName = "graphic";
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("lower third cue added");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  // Named pattern types and their pretty labels.
  static const std::vector<std::pair<std::string, std::string>>& patternBaseTypes() {
    static const std::vector<std::pair<std::string, std::string>> types {
      {"pocket-test",   "Pocket Test (scene cycle + creatures)"},
      {"pocket-day",    "Pocket Day Quest (animated)"},
      {"pocket-sunset", "Pocket Sunset Quest (animated)"},
      {"pocket-night",  "Pocket Night Quest (animated)"},
      {"pocket-storm",  "Pocket Storm Quest (animated)"},
      {"smpte-bars",   "SMPTE 75% Colour Bars"},
      {"crosshatch",   "Crosshatch"},
      {"checkerboard", "Checkerboard"},
      {"full-white",   "Full White"},
      {"full-black",   "Full Black"},
      {"full-red",     "Full Red"},
      {"full-green",   "Full Green"},
      {"full-blue",    "Full Blue"},
    };
    return types;
  }

  static const std::vector<std::pair<std::string, std::string>>& patternTypes() {
    static const std::vector<std::pair<std::string, std::string>> types = [] {
      std::vector<std::pair<std::string, std::string>> list = patternBaseTypes();
      list.emplace_back("smpte-bars-motion", "SMPTE 75% Colour Bars (motion)");
      list.emplace_back("crosshatch-motion", "Crosshatch (motion)");
      list.emplace_back("checkerboard-motion", "Checkerboard (motion)");
      list.emplace_back("full-white-motion", "Full White (motion)");
      list.emplace_back("full-black-motion", "Full Black (motion)");
      list.emplace_back("full-red-motion", "Full Red (motion)");
      list.emplace_back("full-green-motion", "Full Green (motion)");
      list.emplace_back("full-blue-motion", "Full Blue (motion)");
      return list;
    }();
    return types;
  }

  bool isKnownPatternType(const std::string& rawTypeId) const {
    std::string typeId = normalizePatternTypeId(rawTypeId);
    if (typeId == "checker") {
      typeId = "checkerboard";
    }
    const auto& types = patternTypes();
    return std::any_of(types.begin(), types.end(), [&](const auto& item) {
      return item.first == typeId;
    });
  }

  std::string patternLabelForType(const std::string& rawTypeId) const {
    std::string typeId = normalizePatternTypeId(rawTypeId);
    if (typeId == "checker") {
      typeId = "checkerboard";
    }
    const auto& types = patternTypes();
    for (const auto& [id, lbl] : types) {
      if (id == typeId) {
        return lbl;
      }
    }
    return typeId;
  }

  bool applyPatternTypeToSelectedCue(const std::string& rawTypeId, bool announce) {
    Deck& deck = focusedDeckMutable();
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return false;
    }
    Cue& cue = deck.cues[deck.selectedIndex];
    if (cue.kind != CueKind::Pattern) {
      return false;
    }

    std::string typeId = normalizePatternTypeId(rawTypeId);
    if (typeId == "checker") {
      typeId = "checkerboard";
    }
    if (typeId.empty() || !isKnownPatternType(typeId)) {
      triggerToast("pattern: invalid");
      return false;
    }

    std::string previousType = normalizePatternTypeId(cue.path);
    std::string previousLabel = patternLabelForType(previousType);
    std::string nextLabel = patternLabelForType(typeId);
    cue.path = "pattern://" + typeId;
    if (cue.name.empty() || cue.name == previousLabel || cue.name == previousType) {
      cue.name = nextLabel;
    }
    patternDefaultTypeId_ = typeId;
    markProjectDirty();

    if (deck.activeIndex == deck.selectedIndex) {
      if (MediaEngine* engine = focusedMediaEngine()) {
        bool autoplay = engine->state() == TransportState::Playing;
        engine->loadCue(&cue, autoplay);
      }
    }
    if (announce) {
      triggerToast("pattern: " + nextLabel);
    }
    return true;
  }

  void cycleSelectedPatternType(int direction) {
    const Cue* cue = selectedCuePtr();
    if (!cue || cue->kind != CueKind::Pattern) {
      return;
    }
    std::string currentType = normalizePatternTypeId(cue->path);
    bool motion = endsWith(currentType, "-motion");
    std::string baseType = stripPatternMotionSuffix(currentType);
    const auto& bases = patternBaseTypes();
    if (bases.empty()) {
      return;
    }
    int currentIndex = 0;
    for (int i = 0; i < static_cast<int>(bases.size()); ++i) {
      if (bases[i].first == baseType) {
        currentIndex = i;
        break;
      }
    }
    int step = direction < 0 ? -1 : 1;
    int nextIndex = (currentIndex + step + static_cast<int>(bases.size())) % static_cast<int>(bases.size());
    std::string nextType = bases[nextIndex].first;
    if (motion && patternTypeSupportsMotion(nextType)) {
      nextType += "-motion";
    }
    applyPatternTypeToSelectedCue(nextType, true);
  }

  void toggleSelectedPatternMotion() {
    const Cue* cue = selectedCuePtr();
    if (!cue || cue->kind != CueKind::Pattern) {
      return;
    }
    std::string currentType = normalizePatternTypeId(cue->path);
    std::string baseType = stripPatternMotionSuffix(currentType);
    if (!patternTypeSupportsMotion(baseType)) {
      triggerToast("pattern motion: n/a");
      return;
    }
    bool motion = endsWith(currentType, "-motion");
    applyPatternTypeToSelectedCue(motion ? baseType : (baseType + "-motion"), true);
  }

  void addPatternCue(const std::string& rawTypeId) {
    std::string typeId = normalizePatternTypeId(rawTypeId);
    if (typeId.empty()) {
      typeId = patternDefaultTypeId_;
    }
    if (typeId == "checker") {
      typeId = "checkerboard";
    }
    if (!isKnownPatternType(typeId)) {
      triggerToast("pattern: invalid");
      return;
    }
    patternDefaultTypeId_ = typeId;
    std::string label = patternLabelForType(typeId);
    auto [rasterW, rasterH] = outputRenderSizeForOutput(project_.focusedOutputIndex);
    Cue cue;
    cue.kind = CueKind::Pattern;
    cue.path = "pattern://" + typeId;
    cue.name = label;
    cue.width = rasterW;
    cue.height = rasterH;
    cue.color = {50, 50, 120, 255};
    cue.formatName = "generated";
    Deck& deck = focusedDeckMutable();
    applyDeckDefaultsToCue(cue, deck);
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("pattern: " + label);
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  void addPatternCueFromMenu() {
    std::vector<std::pair<std::string, std::string>> choices;
    choices.reserve(patternTypes().size());
    for (const auto& [id, label] : patternTypes()) {
      choices.push_back({label, id});
    }
    auto picked = pickChoiceFromList("Add Pattern", "Choose test pattern type", choices, patternDefaultTypeId_);
    if (!picked) {
      return;
    }
    addPatternCue(*picked);
  }

  void addKawaiiPatternCue() {
    addPatternCue(patternDefaultTypeId_);
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

      applyDeckDefaultsToCue(*cue, deck);
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
    markProjectDirty();
  }

  void toggleOutputFullscreen() {
    if (project_.focusedOutputIndex >= 0 &&
        project_.focusedOutputIndex < static_cast<int>(project_.outputs.size())) {
      const OutputTarget& output = project_.outputs[project_.focusedOutputIndex];
      if (normalizeOutputType(output.outputType) == "stream") {
        triggerToast("fullscreen: stream output");
        return;
      }
      if (!output.enabled) {
        setFocusedOutputEnabled(true);
        return;
      }
    }
    OutputRuntime* runtime = runtimeForOutput(project_.focusedOutputIndex);
    if (!runtime || !runtime->outputWindow) {
      return;
    }
    Uint32 flags = SDL_GetWindowFlags(runtime->outputWindow);
    bool fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
    if (fullscreen) {
      // Fullscreen button now acts as "reassert fullscreen on target display"
      // to avoid losing output windows off-screen.
      recoverWindowOutputIfNeeded(project_.focusedOutputIndex, true);
    } else {
      enableOutputFullscreen(project_.focusedOutputIndex, true);
      SDL_ShowWindow(runtime->outputWindow);
      SDL_RaiseWindow(runtime->outputWindow);
    }
    playUiSound(UiSoundEffect::Toggle);
  }

  std::optional<int> outputIndexForWindowId(Uint32 windowId) const {
    if (windowId == 0) {
      return std::nullopt;
    }
    for (int outputIndex = 0; outputIndex < static_cast<int>(outputRuntimes_.size()); ++outputIndex) {
      const OutputRuntime* runtime = runtimeForOutput(outputIndex);
      if (!runtime || !runtime->outputWindow) {
        continue;
      }
      if (SDL_GetWindowID(runtime->outputWindow) == windowId) {
        return outputIndex;
      }
    }
    return std::nullopt;
  }

  bool escapeOutputFullscreen(Uint32 sourceWindowId) {
    normalizeProject(project_);
    if (project_.outputs.empty()) {
      return false;
    }

    int targetOutputIndex = std::clamp(project_.focusedOutputIndex, 0, static_cast<int>(project_.outputs.size()) - 1);
    std::optional<int> sourceOutputIndex = outputIndexForWindowId(sourceWindowId);
    if (sourceOutputIndex) {
      targetOutputIndex = *sourceOutputIndex;
    }

    auto tryExitFullscreen = [&](int outputIndex) -> bool {
      if (outputIndex < 0 || outputIndex >= static_cast<int>(project_.outputs.size())) {
        return false;
      }
      OutputTarget& output = project_.outputs[outputIndex];
      if (!output.enabled || normalizeOutputType(output.outputType) != "window") {
        return false;
      }
      OutputRuntime* runtime = runtimeForOutput(outputIndex);
      if (!runtime || !runtime->outputWindow) {
        return false;
      }
      Uint32 flags = SDL_GetWindowFlags(runtime->outputWindow);
      bool fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
      if (!fullscreen) {
        return false;
      }
      SDL_SetWindowFullscreen(runtime->outputWindow, 0);
      runtime->recoveryPausedByEscape = true;
      SDL_ShowWindow(runtime->outputWindow);
      SDL_RaiseWindow(controlWindow_);
      triggerToast("escape: " + outputLabel(outputIndex) + " windowed");
      playUiSound(UiSoundEffect::Toggle);
      project_.focusedOutputIndex = outputIndex;
      return true;
    };

    if (tryExitFullscreen(targetOutputIndex)) {
      return true;
    }

    if (sourceOutputIndex &&
        *sourceOutputIndex >= 0 &&
        *sourceOutputIndex < static_cast<int>(project_.outputs.size())) {
      OutputTarget& sourceOutput = project_.outputs[*sourceOutputIndex];
      if (sourceOutput.enabled && normalizeOutputType(sourceOutput.outputType) == "window") {
        if (OutputRuntime* runtime = runtimeForOutput(*sourceOutputIndex); runtime && runtime->outputWindow) {
          // Esc from an output window should remain an output-safety action,
          // not fall through to app quit confirmation.
          runtime->recoveryPausedByEscape = true;
          SDL_ShowWindow(runtime->outputWindow);
          SDL_RaiseWindow(controlWindow_);
          project_.focusedOutputIndex = *sourceOutputIndex;
          return true;
        }
      }
    }

    int controlDisplay = SDL_GetWindowDisplayIndex(controlWindow_);
    for (int outputIndex = 0; outputIndex < static_cast<int>(project_.outputs.size()); ++outputIndex) {
      if (!project_.outputs[outputIndex].enabled ||
          normalizeOutputType(project_.outputs[outputIndex].outputType) != "window") {
        continue;
      }
      OutputRuntime* runtime = runtimeForOutput(outputIndex);
      if (!runtime || !runtime->outputWindow) {
        continue;
      }
      Uint32 flags = SDL_GetWindowFlags(runtime->outputWindow);
      bool fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
      if (!fullscreen) {
        continue;
      }
      int outputDisplay = SDL_GetWindowDisplayIndex(runtime->outputWindow);
      if (controlDisplay >= 0 && outputDisplay >= 0 && outputDisplay != controlDisplay) {
        continue;
      }
      if (tryExitFullscreen(outputIndex)) {
        return true;
      }
    }

    return false;
  }

  void layoutButtons(int windowHeight) {
    buttons_.clear();
    int x = kPadding + 16;
    int y = windowHeight - 94;
    auto push = [&](std::string label, SDL_Color fill, std::string tip = "") {
      Button button;
      button.label = std::move(label);
      button.tip   = std::move(tip);
      button.rect = {x, y, 138, 56};
      button.fill = fill;
      button.outline = colorFromRgba(kScreenDeepColor);
      button.text = colorFromRgba(kScreenDeepColor);
      buttons_.push_back(button);
      x += button.rect.w + 12;
    };
    push("IMPORT",     colorFromRgba(kScreenMidColor), "Shift+I — import media files");
    push("SOURCE",     colorFromRgba(kScreenMidColor), "Add live source cue (window/camera/syphon-spout)");
    push("PATTERN",    colorFromRgba(kScreenMidColor), "Open test pattern menu");
    push("(A) TAKE",   colorFromRgba(kScreenLightColor), "Enter — take selected cue live");
    push("(B) STOP",   colorFromRgba(kScreenMidColor), "S — stop and rewind active cue");
    push("START PLAY", colorFromRgba(kScreenMidColor), "Space — play/pause active cue");
    push("SELECT CLR", colorFromRgba(kScreenMidColor), "C — clear output to black");
    push("OUTPUT",     colorFromRgba(kScreenMidColor), "Open video output settings");
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

  struct DecksPanelButton {
    SDL_Rect rect {};
    int action = 0;
  };

  struct DecksPanelRowHit {
    int deckIndex = -1;
    SDL_Rect rowRect {};
    SDL_Rect groupRect {};
  };

  struct MasterCueSidebarButtonHit {
    SDL_Rect rect {};
    int action = 0;
    int presetIndex = -1;
  };

  struct MasterCueSidebarRowHit {
    int presetIndex = -1;
    SDL_Rect rowRect {};
    SDL_Rect fireRect {};
  };

  struct MasterCueSidebarProgramHit {
    int deckIndex = -1;
    SDL_Rect rowRect {};
    SDL_Rect selectedRect {};
    SDL_Rect activeRect {};
    SDL_Rect bypassRect {};
    SDL_Rect prevRect {};
    SDL_Rect nextRect {};
  };

  struct DecksPanelCueHit {
    int deckIndex = -1;
    int cueIndex = -1;
    SDL_Rect rowRect {};
  };

  struct DecksPanelDeckButtonHit {
    int deckIndex = -1;
    int action = 0;
    SDL_Rect rect {};
  };

  struct MasterCueRowHit {
    int presetIndex = -1;
    SDL_Rect rowRect {};
    SDL_Rect fireRect {};
    std::vector<SDL_Rect> slotRects;
  };

  struct OutputMenuButton {
    SDL_Rect rect {};
    int deckIndex = -1;
    int outputIndex = -1;
    int action = 0;
  };

  static constexpr int kDecksPanelActionGroupPrev = 1;
  static constexpr int kDecksPanelActionGroupNext = 2;
  static constexpr int kDecksPanelActionGroupNew = 3;
  static constexpr int kDecksPanelActionGroupFire = 4;
  static constexpr int kDecksPanelActionGroupDelete = 5;
  static constexpr int kDecksPanelActionGroupProgramToggle = 6;
  static constexpr int kDecksPanelActionGroupCaptureSelected = 7;
  static constexpr int kDecksPanelActionGroupCaptureActive = 8;
  static constexpr int kDecksPanelActionGroupRename = 9;
  static constexpr int kDecksPanelActionMasterFireBase = 1000;
  static constexpr int kDecksPanelDeckActionTake = 200;
  static constexpr int kDecksPanelDeckActionStop = 201;
  static constexpr int kOutputMenuActionFocus = 1;
  static constexpr int kOutputMenuActionToggle = 2;
  static constexpr int kOutputMenuActionAddOutput = 3;
  static constexpr int kOutputMenuActionRouteFocusDeck = 4;
  static constexpr int kOutputMenuActionRouteAssignToggle = 5;
  static constexpr int kOutputMenuActionRouteLayerDec = 6;
  static constexpr int kOutputMenuActionRouteLayerInc = 7;
  static constexpr int kOutputMenuActionRouteOutputPrev = 8;
  static constexpr int kOutputMenuActionRouteOutputNext = 9;
  static constexpr int kSettingsActionOutputRemove = 269;
  static constexpr int kSettingsActionOutputToggle = 262;
  static constexpr int kSettingsActionOutputDisplayPrev = 263;
  static constexpr int kSettingsActionOutputDisplayNext = 264;
  static constexpr int kSettingsActionOutputDisplayRescan = 265;
  static constexpr int kSettingsActionRoutingLayerDec = 266;
  static constexpr int kSettingsActionRoutingLayerInc = 267;
  static constexpr int kSettingsActionRoutingAssignToggle = 268;
  static constexpr int kSettingsActionOutputOverlayToggle = 283;
  static constexpr int kSettingsActionOutputAlphaPrompt = 284;
  static constexpr int kSettingsActionOutputDelayPrompt = 285;
  static constexpr int kSettingsActionOutputColorSpaceCycle = 286;
  static constexpr int kSettingsActionOutputDelayInc = 287;
  static constexpr int kSettingsActionOutputLayoutSpan = 288;
  static constexpr int kSettingsActionOutputLayoutDuplicate = 289;
  static constexpr int kSettingsActionOutputOrientationCycle = 290;
  static constexpr int kSettingsActionOutputTestCardToggle = 291;
  static constexpr int kSettingsActionOutputTestCardAllToggle = 292;
  static constexpr int kSettingsActionPlaylistPrefsEdit = 500;
  static constexpr int kSettingsActionPlaylistDefaultLoopToggle = 501;
  static constexpr int kSettingsActionPlaylistDefaultFadeInToggle = 502;
  static constexpr int kSettingsActionPlaylistDefaultFadeOutToggle = 503;
  static constexpr int kSettingsActionPlaylistDefaultAudioToggle = 504;
  static constexpr int kSettingsActionPlaylistDefaultPauseBeginToggle = 505;
  static constexpr int kSettingsActionPlaylistDefaultPauseEndToggle = 506;
  static constexpr int kSettingsActionPlaylistDefaultNextTransitionToggle = 507;
  static constexpr int kSettingsActionOscQueryToggle = 508;
  static constexpr int kSettingsActionOscQueryPortPrompt = 509;
  static constexpr int kSettingsActionOscFeedbackMirrorToggle = 510;
  static constexpr int kSettingsActionOscFeedbackRatePrompt = 511;
  static constexpr int kSettingsActionOutputDisplayFocusBase = 32000;
  static constexpr int kSettingsActionOutputAdvancedToggle = 270;
  static constexpr int kSettingsActionRoutingModeToggle = 261;
  static constexpr int kSettingsActionRoutingCellBase = 20000;
  static constexpr int kSettingsActionRoutingCellStride = 256;
  static constexpr int kSettingsActionRoutingDeckFocusBase = 26000;
  static constexpr int kSettingsActionRoutingOutputFocusBase = 28000;
  static constexpr int kSettingsActionRoutingTableOutputPrevBase = 34000;
  static constexpr int kSettingsActionRoutingTableOutputNextBase = 35000;
  static constexpr int kSettingsActionRoutingTableLayerDecBase = 36000;
  static constexpr int kSettingsActionRoutingTableLayerIncBase = 37000;
  static constexpr int kSettingsActionRoutingTableAssignToggleBase = 38000;

  SDL_Window* controlWindow_ = nullptr;
  SDL_Renderer* controlRenderer_ = nullptr;
  SDL_Window* decksPanelWindow_ = nullptr;
  SDL_Renderer* decksPanelRenderer_ = nullptr;
  TTF_Font* fontLarge_ = nullptr;
  TTF_Font* fontBase_ = nullptr;
  TTF_Font* fontSmall_ = nullptr;
  TTF_Font* fontMono_ = nullptr;
  TTF_Font* fontPixel_ = nullptr;
  SDL_AudioDeviceID uiAudioDevice_ = 0;
  fs::path currentProjectFile_;
  Project project_;
  std::vector<DeckRuntime> deckRuntimes_;
  std::vector<OutputRuntime> outputRuntimes_;
  std::mutex streamAudioMutex_;
  std::vector<DeckStreamAudioBuffer> deckStreamAudioBuffers_;
#if defined(PLAYBOY_HAS_NDI_SDK)
  NdiApi ndiApi_;
#endif
  std::vector<Button> buttons_;
  std::vector<SDL_Rect> deckColumnRects_;
  std::vector<SDL_Rect> deckListClipRects_;
  SDL_Rect progressBarRect_ {};

  // Context menu
  bool contextMenuOpen_ = false;
  int contextMenuDeckIdx_ = 0;
  int contextMenuCueIdx_ = 0;
  SDL_Rect contextMenuRect_ {};
  struct ContextItem {
    std::string label;
    SDL_Color swatch {0,0,0,0};
    std::function<void()> action;
    SDL_Rect rect {};
  };
  std::vector<ContextItem> contextItems_;

  // Pattern picker popup
  bool patternPickerOpen_ = false;
  SDL_Rect patternPickerRect_ {};
  struct PatternItem { std::string label; std::string typeId; SDL_Rect rect {}; };
  std::vector<PatternItem> patternItems_;
  std::string patternDefaultTypeId_ = "pocket-test";

  // Deck settings modal
  bool deckSettingsOpen_ = false;
  int deckSettingsDeckIdx_ = 0;
  SDL_Rect deckSettingsCloseBtn_ {};
  std::vector<SDL_Rect> deckGearBtns_;
  SDL_Rect dsTransDurMinus_ {}, dsTransDurPlus_ {};
  std::vector<SDL_Rect> dsTransStyleBtns_;
  SDL_Rect dsTcFpsCycle_ {}, dsTcChaseBtn_ {}, dsTcRunBtn_ {}, dsTcTrigBtn_ {};
  SDL_Rect dsTcSetBtn_ {};
  SDL_Rect dsNdiToggle_ {}, dsNdiRename_ {};

  // Master fader
  SDL_Rect masterFaderRect_ {};

  // HyperDeck server
  int hyperDeckPort_ = 9992;
  std::thread hyperDeckThread_;
  std::atomic<bool> hyperDeckRunning_ {false};
  int hyperDeckListenFd_ = -1;

  SDL_Texture* controlPreviewTex_ = nullptr;
  int controlPreviewTexW_ = 0;
  int controlPreviewTexH_ = 0;
  std::uint64_t controlPreviewFrameIdx_ = static_cast<std::uint64_t>(-1);
  std::vector<QuickButton> quickButtons_;
  std::vector<MasterCueSidebarButtonHit> masterCueSidebarButtons_;
  std::vector<MasterCueSidebarRowHit> masterCueSidebarRows_;
  std::vector<MasterCueSidebarProgramHit> masterCueSidebarProgramHits_;
  std::vector<DecksPanelButton> decksPanelButtons_;
  std::vector<DecksPanelRowHit> decksPanelRowHits_;
  std::vector<DecksPanelCueHit> decksPanelCueHits_;
  std::vector<DecksPanelDeckButtonHit> decksPanelDeckButtonHits_;
  std::vector<MasterCueRowHit> masterCueRowHits_;
  std::vector<OutputMenuButton> outputMenuButtons_;
  size_t cueSettingsQuickButtonStartIndex_ = 0;
  SDL_Rect cueSettingsViewportRect_ {};
  int cueSettingsScroll_ = 0;
  int cueSettingsScrollMax_ = 0;
  bool cueSectionPlaybackOpen_ = true;
  bool cueSectionGeometryOpen_ = true;
  bool cueSectionKeyOpen_ = false;
  bool cueSectionRoutingOpen_ = true;
  // Per-selection thumbnail (decoded from the selected cue via ffmpeg)
  ChildProcess thumbnailProcess_;
  std::thread thumbnailThread_;
  std::mutex thumbnailMutex_;
  std::optional<DecodedFrame> pendingThumbnail_;
  std::atomic<bool> thumbnailPending_ {false};
  SDL_Texture* selectedThumbnailTex_ = nullptr;
  int selectedThumbnailTexW_ = 0;
  int selectedThumbnailTexH_ = 0;
  std::string selectedThumbnailCueId_;
  std::vector<int> deckScrolls_;
  int mouseX_ = 0;
  int mouseY_ = 0;
  bool confirmQuit_ = false;
  SDL_Rect quitYesBtn_ {};
  SDL_Rect quitNoBtn_ {};
  bool showStartupDialog_ = false;
  bool showSplashOverlay_ = true;
  Uint64 splashStartedAt_ = 0;
  SDL_Rect startupLoadBtn_ {};
  SDL_Rect startupNewBtn_ {};
  SDL_Rect startupOpenSavedBtn_ {};
  std::future<std::vector<std::string>> pendingImport_;
  std::future<std::optional<fs::path>> pendingProjectOpen_;
  std::future<std::optional<fs::path>> pendingProjectSaveAs_;
  DragState drag_;
  ToastState toast_;
  Uint64 animationNow_ = 0;
  Uint64 selectionChangedAt_ = 0;
  Uint64 lastUpdateTickMs_ = 0;
  Uint64 lastDisplayPollMs_ = 0;
  Uint64 lastOutputRecoveryPollMs_ = 0;
  Uint64 lastEscapeKeyMs_ = 0;
  int escapePressStreak_ = 0;
  int observedDisplayCount_ = -1;
  bool projectDirty_ = false;
  std::chrono::steady_clock::time_point projectDirtyAt_;
  int companionPort_ = 5510;
  bool companionReady_ = false;
  std::atomic<bool> companionStop_ {false};
  std::thread companionThread_;
  std::mutex remoteCommandMutex_;
  std::deque<std::string> remoteCommands_;
  std::mutex statusSnapshotMutex_;
  std::string statusSnapshot_;
  std::string statusSnapshotJson_;
  std::string statusCueSnapshot_;
  std::vector<std::string> statusDeckSnapshots_;
  std::map<int, std::unordered_set<std::string>> timecodeTriggeredCueIds_;
  std::vector<Uint64> deckTimecodeLastExternalMs_;
  std::vector<double> deckTimecodeLastExternalSeconds_;
  std::vector<bool> deckTimecodeHasExternal_;
  std::string lastCueFindToken_;
  std::vector<int> lastCueFindMatches_;
  int lastCueFindCursor_ = -1;
  int lastCueFindDeckIndex_ = -1;
  std::string typedCueSearchBuffer_;
  Uint64 typedCueSearchLastKeyAtMs_ = 0;
  std::vector<float> deckPlaylistOpacityTargets_;
  std::vector<SDL_Rect> deckOpacityFaderRects_;
  std::vector<std::int16_t> vuSamples_;
  std::mutex vuSamplesMutex_;
  // Waveform analysis cache (path → peaks vector)
  std::map<std::string, std::vector<float>> waveformCache_;
  std::map<std::string, std::future<std::vector<float>>> waveformFutures_;
  std::mutex waveformMutex_;
  // Settings modal
  bool settingsOpen_ = false;
  int settingsTab_ = 0; // 0=System 1=Audio 2=Network 3=Video Outputs 4=About
  bool videoOutputsAdvanced_ = false;
  bool routingMoveMode_ = true; // true=single-output move, false=add/remove fan-out
  bool deckSidebarOpen_ = true;
  bool masterCueProgrammerExpanded_ = true;
  SDL_Rect settingsCloseBtn_ {};
  SDL_Rect settingsGearRect_ {};
  SDL_Rect decksPanelToggleRect_ {};
  SDL_Rect deckSidebarToggleRect_ {};
  SDL_Rect blackoutBtnRect_ {};
  SDL_Rect fileNewBtnRect_ {};
  SDL_Rect fileOpenBtnRect_ {};
  SDL_Rect fileSaveBtnRect_ {};
  SDL_Rect fileSaveAsBtnRect_ {};
  double masterDimmerTarget_ = 1.0;  // target for animated masterDimmer (0=black, 1=full)
  bool panicProfilePending_ = false;
  std::string pendingPanicProfileToken_;
  Uint64 panicProfileRequestedAt_ = 0;
  double panicRestoreDimmerTarget_ = 1.0;
  struct SettingsButton { SDL_Rect rect; int action; std::string label; };
  std::vector<SettingsButton> settingsBtns_;
  bool midiEnabled_ = false;
  std::string midiDeviceName_;
#if defined(PLAYBOY_HAS_ALSA)
  snd_seq_t* midiSeq_ = nullptr;
  int midiSeqPort_ = -1;
  std::thread midiThread_;
  std::atomic<bool> midiStop_ {false};
#endif
#ifndef _WIN32
  SocketHandle companionTcpListen_ = kInvalidSocket;
  SocketHandle companionUdpSocket_ = kInvalidSocket;
  SocketHandle oscQueryTcpListen_ = kInvalidSocket;
  bool oscQueryReady_ = false;
  std::atomic<bool> oscQueryStop_ {false};
  std::thread oscQueryThread_;
  std::vector<SocketHandle> companionClients_;
  std::map<SocketHandle, std::string> companionClientBuffers_;
  std::map<std::string, std::pair<sockaddr_in, Uint64>> oscSubscribers_;
  Uint64 lastOscFeedbackBroadcastMs_ = 0;
  std::string lastOscFeedbackPayload_;
  Uint64 lastOscMirrorFeedbackBroadcastMs_ = 0;
  std::string lastOscMirrorFeedbackPayload_;
#endif
};

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--self-check") {
    return App::runSelfCheck();
  }
  if (argc > 1 && std::string_view(argv[1]) == "--smoke") {
    return App::runSmoke();
  }

  bool allowMultiInstance = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--allow-multi-instance") {
      allowMultiInstance = true;
      break;
    }
  }
#ifndef _WIN32
  SingleInstanceGuard instanceGuard;
  if (!allowMultiInstance) {
    fs::path lockPath = "/tmp/playboy-native.instance.lock";
    try {
      lockPath = fs::temp_directory_path() / "playboy-native.instance.lock";
    } catch (...) {
    }
    if (!instanceGuard.acquire(lockPath)) {
      std::cerr << "Refusing to launch another instance (" << lockPath.string() << "): "
                << instanceGuard.lastError() << '\n';
      std::cerr << "Use --allow-multi-instance to bypass this safety lock.\n";
      return 2;
    }
  }
#endif

  App app;
  if (!app.init()) {
    app.shutdown();
    return 1;
  }

  app.run();
  app.shutdown();
  return 0;
}
