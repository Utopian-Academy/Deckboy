#include <SDL.h>
#include <SDL_ttf.h>

#include "core/constants.hpp"
#include "core/types.hpp"
#include "core/paths.hpp"
#include "core/subprocess.hpp"
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
#include <sys/socket.h>
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

std::string cueKindLabel(CueKind kind) {
  switch (kind) {
    case CueKind::Image:      return "Still";
    case CueKind::Pattern:    return "Pattern";
    case CueKind::Browser:    return "Browser";
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
    case CueKind::LowerThird: return "lower_third";
    case CueKind::Audio:      return "audio";
    case CueKind::Video:
    default:                  return "video";
  }
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
  if (path == "/DECK") {
    if (auto value = argString(0)) {
      return "DECK " + *value;
    }
    return std::nullopt;
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

struct DeckRuntime {
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
#if defined(PLAYBOY_HAS_NDI_SDK)
  NDIlib_send_instance_t ndiSender = nullptr;
  std::string ndiSenderName;
  std::vector<std::uint8_t> ndiFrameBuffer;
  NDIlib_send_instance_t ndiKeySender = nullptr;
  std::string ndiKeySenderName;
  std::vector<std::uint8_t> ndiKeyFrameBuffer;
#endif
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
  return "Playboy - " + base;
}

std::string defaultNdiKeySourceName(const Deck& deck, int index) {
  return defaultNdiSourceName(deck, index) + " Key";
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
  }
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
  if (!std::isfinite(deck.timecodeCurrentSeconds) || deck.timecodeCurrentSeconds < 0.0) {
    deck.timecodeCurrentSeconds = 0.0;
  }
  if (!std::isfinite(deck.timecodeLastSeconds) || deck.timecodeLastSeconds < 0.0) {
    deck.timecodeLastSeconds = deck.timecodeCurrentSeconds;
  }
  deck.timecodeDirty = false;
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
  output << "advanced_mode\t" << (project.advancedOutputMode ? 1 : 0) << '\n';
  output << "ui_sounds\t" << (project.uiSoundsEnabled ? 1 : 0) << '\n';
  output << "ui_transitions\t" << (project.uiTransitionsEnabled ? 1 : 0) << '\n';
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
      << deck.outputLayerIndex
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
      cue.outputOffsetX = static_cast<float>(safeDouble(fields, offset + 34, 0.0));
      cue.outputOffsetY = static_cast<float>(safeDouble(fields, offset + 35, 0.0));
      cue.cueNumber = safeString(fields, offset + 36);
      {
        std::string ppStr = safeString(fields, offset + 37);
        if (!ppStr.empty()) {
          std::istringstream ss(ppStr);
          std::string tok;
          while (std::getline(ss, tok, ',')) {
            try { cue.pausePoints.push_back(std::stod(tok)); } catch (...) {}
          }
          std::sort(cue.pausePoints.begin(), cue.pausePoints.end());
        }
      }
      cue.outputRotationDegrees = static_cast<float>(safeDouble(fields, offset + 38, 0.0));
      cue.cropLeft = static_cast<float>(safeDouble(fields, offset + 39, 0.0));
      cue.cropRight = static_cast<float>(safeDouble(fields, offset + 40, 0.0));
      cue.cropTop = static_cast<float>(safeDouble(fields, offset + 41, 0.0));
      cue.cropBottom = static_cast<float>(safeDouble(fields, offset + 42, 0.0));
      cue.chromaKeyEnabled = safeBool(fields, offset + 43, false);
      cue.chromaKeyColor = parseColor(safeString(fields, offset + 44));
      cue.chromaKeyTolerance = static_cast<float>(safeDouble(fields, offset + 45, 60.0));
      cue.chromaKeySoftness = static_cast<float>(safeDouble(fields, offset + 46, 20.0));
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
    bool isTimedStill = activeCue_->kind != CueKind::Video && duration_ > 0.0;
    if (activeCue_->kind != CueKind::Video && !isTimedStill) return;
    if (state_ == TransportState::Playing) { pause(); } else { play(); }
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
    // Stills have no seek position — keep the loaded frame as-is.
    if (activeCue_->kind == CueKind::Image) {
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

    if (activeCue_->kind != CueKind::Video && !isBrowserCapturing_) {
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
    if (activeCue_) {
      outputScaleX_ = activeCue_->outputScaleX;
      outputScaleY_ = activeCue_->outputScaleY;
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

    double scale = std::min(
      static_cast<double>(target.w) / static_cast<double>(srcW),
      static_cast<double>(target.h) / static_cast<double>(srcH)
    );
    int drawW = std::max(1, static_cast<int>(std::round(srcW * scale)));
    int drawH = std::max(1, static_cast<int>(std::round(srcH * scale)));
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
    if (chromaKeyEnabled_) {
      keyedPixelsScratch_.assign(frame.pixels.begin(), frame.pixels.end());
      float tolerance = std::clamp(chromaKeyTolerance_, 0.0f, 441.0f);
      float softness = std::clamp(chromaKeySoftness_, 0.0f, 200.0f);
      float inner = std::max(0.0f, tolerance - softness);
      float outer = tolerance + softness;
      float span = std::max(0.0001f, outer - inner);

      for (size_t i = 0; i + 3 < keyedPixelsScratch_.size(); i += 4) {
        float dr = static_cast<float>(keyedPixelsScratch_[i + 0]) - static_cast<float>(chromaKeyColor_.r);
        float dg = static_cast<float>(keyedPixelsScratch_[i + 1]) - static_cast<float>(chromaKeyColor_.g);
        float db = static_cast<float>(keyedPixelsScratch_[i + 2]) - static_cast<float>(chromaKeyColor_.b);
        float distance = std::sqrt(dr * dr + dg * dg + db * db);
        float keep = 1.0f;
        if (distance <= inner) {
          keep = 0.0f;
        } else if (distance < outer) {
          keep = (distance - inner) / span;
        }
        keyedPixelsScratch_[i + 3] = static_cast<std::uint8_t>(
          std::clamp(static_cast<int>(std::lround(static_cast<float>(keyedPixelsScratch_[i + 3]) * keep)), 0, 255)
        );
      }
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

  static void buildCrosshatch(DecodedFrame& frame) {
    int W = frame.width, H = frame.height;
    // Black background
    fillPixelRect(frame, 0, 0, W, H, {0, 0, 0, 255});
    // White lines every 64px, 2px thick
    for (int x = 0; x < W; x += 64) {
      fillPixelRect(frame, x, 0, 2, H, {255, 255, 255, 255});
    }
    for (int y = 0; y < H; y += 64) {
      fillPixelRect(frame, 0, y, W, 2, {255, 255, 255, 255});
    }
    // Centre cross in red
    fillPixelRect(frame, W / 2 - 1, 0,     2, H, {220,  40,  40, 255});
    fillPixelRect(frame, 0,     H / 2 - 1, W, 2, {220,  40,  40, 255});
    // Corner safe-area marks (80% safe)
    int sx = W / 10, sy = H / 10;
    fillPixelRect(frame, sx, sy, W - sx * 2, 2, {60, 180, 60, 200});
    fillPixelRect(frame, sx, sy, 2, H - sy * 2, {60, 180, 60, 200});
    fillPixelRect(frame, W - sx - 2, sy, 2, H - sy * 2, {60, 180, 60, 200});
    fillPixelRect(frame, sx, H - sy - 2, W - sx * 2, 2, {60, 180, 60, 200});
  }

  static void buildCheckerboard(DecodedFrame& frame) {
    int W = frame.width, H = frame.height;
    int cell = 64;
    for (int y = 0; y < H; y += cell) {
      for (int x = 0; x < W; x += cell) {
        bool white = (((x / cell) + (y / cell)) % 2) == 0;
        SDL_Color c = white ? SDL_Color{255,255,255,255} : SDL_Color{0,0,0,255};
        fillPixelRect(frame, x, y, std::min(cell, W - x), std::min(cell, H - y), c);
      }
    }
  }

  // Pocket Test — full-colour animated kawaii procedural scene.
  // Pocket Test — authentic 4-colour Game Boy aesthetic.
  // Uses only the classic GB green palette, 8×8 tile grid, chunky sprites.
  static void buildPocketTest(DecodedFrame& frame, double t) {
    const int W = frame.width, H = frame.height;
    // 4-colour GB palette (indices 0–3, lightest to darkest)
    const SDL_Color P[4] = {
      {155, 188,  15, 255},  // 0  lightest (screen-on green)
      {139, 172,  15, 255},  // 1  light
      { 48,  98,  48, 255},  // 2  dark
      { 15,  56,  15, 255},  // 3  darkest
    };

    // Scale factor: each "GB pixel" = S×S screen pixels.
    // Pick S so the virtual resolution is ~160×144 (authentic GB) scaled up to fill frame.
    const int S = std::max(1, std::min(W / 160, H / 144));
    // Virtual dims in GB pixels
    const int VW = W / S, VH = H / S;

    // Helper: fill one GB pixel
    auto gbp = [&](int gx, int gy, int p) {
      if (gx < 0 || gy < 0 || gx >= VW || gy >= VH) return;
      fillPixelRect(frame, gx * S, gy * S, S, S, P[p & 3]);
    };
    // Fill a rect in GB pixels
    auto gbr = [&](int gx, int gy, int gw, int gh, int p) {
      fillPixelRect(frame, gx * S, gy * S, gw * S, gh * S, P[p & 3]);
    };

    // ── Background: sky / ground tiles ─────────────────────────────────────
    // Sky (colour 0 — lightest)
    gbr(0, 0, VW, VH * 2 / 3, 0);
    // Ground (colour 1)
    gbr(0, VH * 2 / 3, VW, VH / 3 + 1, 1);
    // Ground top strip (colour 0 — highlight)
    gbr(0, VH * 2 / 3, VW, 1, 0);

    // ── Scrolling tile map (8×8 tiles) ─────────────────────────────────────
    int scrollX = static_cast<int>(t * 16.0) % 8;  // 2 GB px/s scroll
    int groundY = VH * 2 / 3;

    // Draw grass ground tiles
    for (int tx = -1; tx <= VW / 8 + 1; ++tx) {
      int tileX = tx * 8 - scrollX;
      // Grass tuft pattern on ground row
      gbp(tileX + 1, groundY + 1, 2);
      gbp(tileX + 3, groundY + 1, 2);
      gbp(tileX + 5, groundY + 1, 2);
      gbp(tileX + 2, groundY + 2, 2);
      gbp(tileX + 4, groundY + 2, 2);
    }

    // Draw trees along the horizon (every 24 px, deterministic)
    for (int tx = -1; tx <= VW / 24 + 2; ++tx) {
      int baseX = tx * 24 - (static_cast<int>(t * 16.0) % 24);
      int baseY = groundY - 12;
      // Tree trunk (colour 2)
      gbr(baseX + 3, baseY + 8, 2, 4, 2);
      // Leaves (colour 2 with colour 3 outline)
      //  top
      gbr(baseX + 1, baseY + 4, 6, 5, 2);
      gbr(baseX + 2, baseY + 2, 4, 3, 2);
      gbp(baseX + 3, baseY + 1, 2);  gbp(baseX + 4, baseY + 1, 2);
      // shadow pixels (colour 3)
      gbp(baseX + 6, baseY + 4, 3); gbp(baseX + 6, baseY + 5, 3);
      gbp(baseX + 6, baseY + 6, 3); gbp(baseX + 6, baseY + 7, 3);
      gbp(baseX + 5, baseY + 8, 3); gbp(baseX + 6, baseY + 8, 3);
    }

    // ── Stars / small moon in sky ─────────────────────────────────────────
    // Static star pattern — twinkle via frame parity
    bool starFrame = (static_cast<int>(t * 3.0) % 2) == 0;
    int stars[][2] = {{12,4},{30,2},{48,6},{65,3},{82,5},{100,2},{115,6},{130,4}};
    for (auto& s : stars) {
      gbp(s[0], s[1], starFrame ? 2 : 3);
      if (starFrame) { gbp(s[0]+1, s[1], 2); }
    }

    // Moon (top-right area)
    int moonX = VW - 28 + static_cast<int>(std::sin(t * 0.1) * 2);
    int moonY = 5;
    for (int my = 0; my < 7; ++my) {
      for (int mx = 0; mx < 7; ++mx) {
        // Circular moon mask
        int dx = mx - 3, dy = my - 3;
        if (dx*dx + dy*dy <= 9) gbp(moonX + mx, moonY + my, 1);
      }
    }
    // Moon crater details
    gbp(moonX + 1, moonY + 2, 2);
    gbp(moonX + 4, moonY + 4, 2);

    // ── Clouds (scrolling, colour 0/1 dithered) ──────────────────────────
    auto drawGBCloud = [&](int cx, int cy) {
      // Cloud body in colour 0 (lightest) on colour-0 sky — outline in colour 1
      gbr(cx+1, cy+1, 4, 2, 1);  gbr(cx, cy+2, 6, 2, 1);  // darker outline
      gbr(cx+1, cy+1, 3, 2, 0);  gbr(cx+1, cy+2, 4, 1, 0);  // light fill
    };
    for (int c = 0; c < 3; ++c) {
      int cx = (c * 50 - static_cast<int>(t * 8.0 + c * 37)) % (VW + 16) - 8;
      drawGBCloud(cx, 8 + c * 3);
    }

    // ── Girl character sprite (16×16 GB pixels, 2-frame walk) ───────────
    // GB sprites use 4 shades. Design inspired by LOZLA/Pokemon heroine.
    int walkSpeed = 24;  // GB pixels per second
    int cycleW = VW + 20;
    double walkPhase = std::fmod(t * walkSpeed, static_cast<double>(cycleW * 2));
    int sprX, flip = 0;
    if (walkPhase < cycleW) {
      sprX = static_cast<int>(walkPhase) - 10;
      flip = 0;
    } else {
      sprX = cycleW - static_cast<int>(walkPhase - cycleW) - 10;
      flip = 1;
    }
    int sprY = groundY - 16;
    int wf = (static_cast<int>(t * 8.0)) % 2;  // walk frame 0/1

    // Sprite: 10 wide × 16 tall, defined as 2-bit colour indices
    // 0=light, 1=mid, 2=dark, 3=darkest; X=skip (transparent / background)
    // Row 0–3: hair + head
    static const std::array<std::array<int,10>,16> spr = {{
      {{99,99, 1, 1, 1, 1, 1,99,99,99}},  // hair top
      {{99, 1, 2, 2, 2, 2, 2, 1,99,99}},
      {{ 1, 2, 0, 0, 0, 0, 0, 2, 1,99}},  // face (colour 0 = skin highlight)
      {{ 1, 2, 0, 2, 0, 2, 0, 2, 1,99}},  // eyes (colour 2 = eye)
      {{ 1, 2, 0, 0, 0, 0, 0, 2, 1,99}},
      {{ 1, 1, 0, 0, 3, 0, 0, 1, 1,99}},  // mouth
      {{ 1, 1, 1, 1, 1, 1, 1, 1,99,99}},  // hair bottom
      {{99, 2, 2, 2, 2, 2, 2, 2,99,99}},  // collar
      {{99, 2, 1, 1, 1, 1, 1, 2,99,99}},  // body
      {{99, 2, 1, 1, 1, 1, 1, 2,99,99}},
      {{99, 1, 2, 1, 1, 1, 2, 1,99,99}},  // belt
      {{99, 1, 2, 1, 1, 1, 2, 1,99,99}},  // skirt
      {{99, 1, 1, 2, 1, 2, 1, 1,99,99}},
      {{99,99, 2, 0,99, 0, 2,99,99,99}},  // legs
      {{99,99, 2, 0,99, 0, 2,99,99,99}},
      {{99,99, 3, 3,99, 3, 3,99,99,99}},  // shoes
    }};
    // Walk frame shifts one leg forward
    for (int row = 0; row < 16; ++row) {
      for (int col = 0; col < 10; ++col) {
        int px_col = col;
        int val = spr[row][px_col];
        if (val > 90) continue;  // transparent
        // Animate legs on rows 13-15 in frame 1
        if (wf == 1 && row >= 13) {
          // Swap leg positions
          if (col == 3) px_col = 5;
          else if (col == 5) px_col = 3;
          val = spr[row][px_col];
          if (val > 90) continue;
        }
        int drawX = flip ? sprX + (9 - col) : sprX + col;
        gbp(drawX, sprY + row, val & 3);
      }
    }

    // ── Top HUD bar (status panel, colour 3 bg) ─────────────────────────
    gbr(0, 0, VW, 9, 3);
    // HP label (crude pixel text via rectangles)
    // "HP" — H
    gbp(2,1,0); gbp(2,2,0); gbp(2,3,0); gbp(3,2,0); gbp(4,1,0); gbp(4,2,0); gbp(4,3,0);
    // "P"
    gbp(6,1,0); gbp(6,2,0); gbp(6,3,0); gbp(7,1,0); gbp(8,1,0); gbp(7,2,0); gbp(8,2,0);
    // HP hearts (alternating fill/outline, 3 hearts)
    for (int h = 0; h < 3; ++h) {
      int hx = 12 + h * 8;
      // full heart (both sides up + bottom triangle) in colour 1
      gbp(hx+1,2,1); gbp(hx+2,1,1); gbp(hx+3,2,1);
      gbp(hx,3,1); gbp(hx+1,3,1); gbp(hx+2,3,1); gbp(hx+3,3,1); gbp(hx+4,3,1);
      gbp(hx+1,4,1); gbp(hx+2,5,1); gbp(hx+3,4,1);
      gbp(hx+2,4,1);
    }
    // Score ticker top-right
    int score = static_cast<int>(t * 10.0) % 10000;
    std::string scoreStr = std::to_string(score);
    while (scoreStr.size() < 5) scoreStr = "0" + scoreStr;
    // Draw score digits (tiny 3×5 pixel digits)
    static const std::array<std::array<int,15>, 10> digits {{
      {{1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1}}, // 0
      {{0,1,0, 1,1,0, 0,1,0, 0,1,0, 1,1,1}}, // 1
      {{1,1,1, 0,0,1, 1,1,1, 1,0,0, 1,1,1}}, // 2
      {{1,1,1, 0,0,1, 0,1,1, 0,0,1, 1,1,1}}, // 3
      {{1,0,1, 1,0,1, 1,1,1, 0,0,1, 0,0,1}}, // 4
      {{1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1}}, // 5
      {{1,1,1, 1,0,0, 1,1,1, 1,0,1, 1,1,1}}, // 6
      {{1,1,1, 0,0,1, 0,1,0, 0,1,0, 0,1,0}}, // 7
      {{1,1,1, 1,0,1, 1,1,1, 1,0,1, 1,1,1}}, // 8
      {{1,1,1, 1,0,1, 1,1,1, 0,0,1, 1,1,1}}, // 9
    }};
    int digitX = VW - 28;
    for (char ch : scoreStr) {
      int d = ch - '0';
      for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
          if (digits[d][row * 3 + col]) gbp(digitX + col, 2 + row, 0);
        }
      }
      digitX += 4;
    }

    // ── Bottom text box (message window, GB style) ───────────────────────
    int boxY = VH - 20;
    gbr(0, boxY, VW, 20, 3);
    // Inner border (colour 2)
    gbr(2, boxY + 2, VW - 4, 1, 2);
    gbr(2, boxY + 2, 1, 16, 2);
    gbr(VW - 3, boxY + 2, 1, 16, 2);
    gbr(2, boxY + 17, VW - 4, 1, 2);
    // Blinking cursor ▶ (colour 0)
    bool cursorOn = std::fmod(t, 1.0) < 0.6;
    if (cursorOn) {
      gbp(VW - 6, boxY + 13, 0); gbp(VW - 5, boxY + 14, 0); gbp(VW - 6, boxY + 15, 0);
    }
    // Static message text (pixel dots approximation)
    // "POCKET TEST v1" — just draw a decorative text-like row of pixels
    // Use horizontal bar pattern to suggest text without needing a font
    for (int ch = 0; ch < 14; ++ch) {
      // Each char = 3 wide, 5 tall, separated by 1px
      static const std::array<int, 14> textCols {{ 0,1,0, 0,1,1, 0,1,0, 0,1,0, 1,0 }};
      (void)textCols;
      int cx = 5 + ch * 4;
      // Draw simple high-contrast pixel bar for each "letter"
      gbp(cx, boxY + 5, 0); gbp(cx+1, boxY + 5, 0); gbp(cx+2, boxY + 5, 0);
      gbp(cx, boxY + 6, 0);
      gbp(cx, boxY + 7, 0); gbp(cx+1, boxY + 7, 0);
      gbp(cx, boxY + 8, 0);
      gbp(cx, boxY + 9, 0); gbp(cx+1, boxY + 9, 0); gbp(cx+2, boxY + 9, 0);
    }

    // ── Tile grid ghost (subtle 8×8 grid lines in colour 3) ─────────────
    // Very faint to give "tile map" feel without dominating
    for (int tx = 0; tx < VW; tx += 8) {
      gbp(tx, groundY - 1, 2);  // just the horizon column dots
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

    const std::string& path = cue.path;

    if (path.find("smpte-bars") != std::string::npos ||
        path.find("smpte_bars") != std::string::npos ||
        path.find("colorbars")  != std::string::npos ||
        path.find("colourbars") != std::string::npos) {
      buildSmpte75Bars(frame);
    } else if (path.find("crosshatch") != std::string::npos) {
      buildCrosshatch(frame);
    } else if (path.find("checkerboard") != std::string::npos ||
               path.find("checker")      != std::string::npos) {
      buildCheckerboard(frame);
    } else if (path.find("full-white") != std::string::npos) {
      fillPixelRect(frame, 0, 0, frame.width, frame.height, {255, 255, 255, 255});
    } else if (path.find("full-black") != std::string::npos) {
      fillPixelRect(frame, 0, 0, frame.width, frame.height, {0, 0, 0, 255});
    } else if (path.find("full-red")   != std::string::npos) {
      fillPixelRect(frame, 0, 0, frame.width, frame.height, {255, 0, 0, 255});
    } else if (path.find("full-green") != std::string::npos) {
      fillPixelRect(frame, 0, 0, frame.width, frame.height, {0, 255, 0, 255});
    } else if (path.find("full-blue")  != std::string::npos) {
      fillPixelRect(frame, 0, 0, frame.width, frame.height, {0, 0, 255, 255});
    } else {
      // Default / "pocket-test" / "kawaii-pocket": animated full-colour scene.
      buildPocketTest(frame, animTime);
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

    if (cue.hasAudio) {
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

    controlRenderer_ = SDL_CreateRenderer(controlWindow_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!controlRenderer_) {
      std::cerr << "Renderer creation failed: " << SDL_GetError() << '\n';
      return false;
    }

    fontLarge_ = TTF_OpenFont(Paths::fontPath(Paths::FontName::Sans).string().c_str(), 28);
    fontBase_ = TTF_OpenFont(Paths::fontPath(Paths::FontName::Sans).string().c_str(), 18);
    fontSmall_ = TTF_OpenFont(Paths::fontPath(Paths::FontName::Sans).string().c_str(), 15);
    fontMono_ = TTF_OpenFont(Paths::fontPath(Paths::FontName::Mono).string().c_str(), 16);
    fontPixel_ = TTF_OpenFont(Paths::fontPath(Paths::FontName::Pixel).string().c_str(), 20);
    if (!fontLarge_ || !fontBase_ || !fontSmall_ || !fontMono_) {
      std::cerr << "Font load failed: " << TTF_GetError() << '\n';
      return false;
    }

    Paths::ensureDataDir();
    currentProjectFile_ = defaultProjectFile();
    project_ = loadProject(currentProjectFile_);
    normalizeProject(project_);
    // Output starts black — no cue is active until the operator takes one
    for (auto& deck : project_.decks) { deck.activeIndex = -1; }
    // Show startup dialog so operator can choose to load or start fresh
    showStartupDialog_ = true;
    ensureUiAudioDevice();
    if (!rebuildDeckRuntimes()) {
      std::cerr << "Deck runtime creation failed: " << SDL_GetError() << '\n';
      return false;
    }
    selectionChangedAt_ = SDL_GetTicks64();
    lastUpdateTickMs_ = selectionChangedAt_;
    startCompanionControl();
    startHyperDeckServer();
    layoutButtons(kControlHeight);
    return true;
  }

  void shutdown() {
    stopHyperDeckServer();
    stopMidiInput();
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
        markProjectDirty();
        triggerToast("saved as " + currentProjectLabel());
      }
    }
  }

  static int runSelfCheck() {
    std::cout << "playboy-native self-check\n";
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
      Deck deck;
      deck.name = "Deck Smoke";
      deck.outputRouteDeckIndex = 0;
      deck.outputLayerIndex = 3;
      deck.transitionSeconds = 1.5;
      deck.transitionStyle = "dip";
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
      deck.timecodeFps = 25.0;
      deck.timecodeCurrentSeconds = 12.0;
      Cue cue;
      cue.path = "/tmp/test.mp4";
      cue.name = "Smoke Cue";
      cue.kind = CueKind::Video;
      cue.duration = 20.0;
      cue.width = 1920;
      cue.height = 1080;
      cue.inPointSeconds = 2.0;
      cue.outPointSeconds = 8.0;
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
      Cue imgCue;
      imgCue.path = "/tmp/test.jpg";
      imgCue.name = "Smoke Still";
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
        expect(loadedDeck.timecodeChaseEnabled, "timecode chase persisted");
        expect(std::abs(loadedCue.inPointSeconds - 2.0) < 0.01 && std::abs(loadedCue.outPointSeconds - 8.0) < 0.01, "trim persisted");
        expect(std::abs(loadedCue.triggerTimecodeSeconds - 13.0) < 0.01, "cue tc mark persisted");
        expect(std::abs(loadedCue.cueTransitionSeconds - 1.25) < 0.01, "cue transition persisted");
        expect(loadedCue.cueTransitionStyle == "crossfade", "cue transition style persisted");
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

  int resolveDeckOutputHostIndex(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return deckIndex;
    }
    int current = deckIndex;
    std::vector<bool> visited(project_.decks.size(), false);
    while (current >= 0 && current < static_cast<int>(project_.decks.size())) {
      if (visited[current]) {
        return deckIndex;
      }
      visited[current] = true;
      int next = project_.decks[current].outputRouteDeckIndex;
      if (next < 0 || next >= static_cast<int>(project_.decks.size()) || next == current) {
        return current;
      }
      current = next;
    }
    return deckIndex;
  }

  std::vector<int> layeredDeckIndicesForOutputHost(int outputDeckIndex) const {
    std::vector<int> indices;
    if (outputDeckIndex < 0 || outputDeckIndex >= static_cast<int>(project_.decks.size())) {
      return indices;
    }
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (resolveDeckOutputHostIndex(deckIndex) == outputDeckIndex) {
        indices.push_back(deckIndex);
      }
    }
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
      const Deck& deckA = project_.decks[a];
      const Deck& deckB = project_.decks[b];
      if (deckA.outputLayerIndex != deckB.outputLayerIndex) {
        return deckA.outputLayerIndex < deckB.outputLayerIndex;
      }
      return a < b;
    });
    return indices;
  }

  int nextLayerIndexForOutputHost(int outputDeckIndex, int ignoreDeckIndex = -1) const {
    int nextLayer = 0;
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (deckIndex == ignoreDeckIndex) {
        continue;
      }
      if (resolveDeckOutputHostIndex(deckIndex) == outputDeckIndex) {
        nextLayer = std::max(nextLayer, project_.decks[deckIndex].outputLayerIndex + 1);
      }
    }
    return std::clamp(nextLayer, 0, 255);
  }

  std::string deckOutputRoutingLabel(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "out:-- layer:--";
    }
    int hostIndex = resolveDeckOutputHostIndex(deckIndex);
    const Deck& deck = project_.decks[deckIndex];
    return "out:" + std::to_string(hostIndex + 1) + " layer:" + std::to_string(deck.outputLayerIndex);
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
    selectionChangedAt_ = SDL_GetTicks64();
    cueSettingsScroll_ = 0;
    cueSettingsScrollMax_ = 0;
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

  bool setFocusedDeckOutputRoute(int targetDeckIndex, std::optional<int> requestedLayerIndex = std::nullopt) {
    normalizeProject(project_);
    if (targetDeckIndex < 0 || targetDeckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    int focusedIndex = project_.focusedDeckIndex;
    Deck& deck = project_.decks[focusedIndex];
    int previousRoute = deck.outputRouteDeckIndex;
    int previousLayer = deck.outputLayerIndex;
    deck.outputRouteDeckIndex = targetDeckIndex;
    if (requestedLayerIndex) {
      deck.outputLayerIndex = std::clamp(*requestedLayerIndex, 0, 255);
    } else if (previousRoute != targetDeckIndex) {
      int hostIndex = resolveDeckOutputHostIndex(targetDeckIndex);
      deck.outputLayerIndex = nextLayerIndexForOutputHost(hostIndex, focusedIndex);
    }
    normalizeProject(project_);
    int hostIndex = resolveDeckOutputHostIndex(focusedIndex);
    const Deck& hostDeck = project_.decks[hostIndex];
    std::string hostLabel = hostDeck.name.empty() ? deckDefaultName(hostIndex) : hostDeck.name;
    triggerToast("route: " + hostLabel + " L" + std::to_string(project_.decks[focusedIndex].outputLayerIndex));
    playUiSound(UiSoundEffect::Toggle);
    if (previousRoute != project_.decks[focusedIndex].outputRouteDeckIndex ||
        previousLayer != project_.decks[focusedIndex].outputLayerIndex) {
      markProjectDirty();
    }
    return true;
  }

  bool setFocusedDeckLayerIndex(int layerIndex) {
    normalizeProject(project_);
    Deck& deck = focusedDeckMutable();
    int clamped = std::clamp(layerIndex, 0, 255);
    if (deck.outputLayerIndex == clamped) {
      triggerToast("layer " + std::to_string(clamped));
      return false;
    }
    deck.outputLayerIndex = clamped;
    triggerToast("layer " + std::to_string(deck.outputLayerIndex));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
    return true;
  }

  void nudgeFocusedDeckLayerIndex(int delta) {
    setFocusedDeckLayerIndex(focusedDeck().outputLayerIndex + delta);
  }

  void addDeck() {
    normalizeProject(project_);
    int routeHostIndex = resolveDeckOutputHostIndex(project_.focusedDeckIndex);
    Deck deck;
    deck.name = deckDefaultName(static_cast<int>(project_.decks.size()));
    if (routeHostIndex >= 0 && routeHostIndex < static_cast<int>(project_.decks.size())) {
      deck.outputDisplayIndex = project_.decks[routeHostIndex].outputDisplayIndex;
      deck.outputRouteDeckIndex = routeHostIndex;
      deck.outputLayerIndex = nextLayerIndexForOutputHost(routeHostIndex);
    }
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
    if (runtime.audioDevice != 0) {
      SDL_CloseAudioDevice(runtime.audioDevice);
      runtime.audioDevice = 0;
    }
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
        sendDeckNdiAudioSamples(deckIndex, samples);
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

  void applyDeckNdiSettings(int deckIndex, bool withToast) {
    Deck& deck = project_.decks[deckIndex];
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
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

    if (!deck.ndiEnabled) {
      clearSenders();
      if (withToast) {
        triggerToast("ndi off");
      }
      return;
    }

    std::string loadError;
    if (!ensureNdiRuntimeReady(&loadError)) {
      deck.ndiEnabled = false;
      clearSenders();
      if (withToast) {
        triggerToast("ndi unavailable");
      }
      return;
    }

    if (deck.ndiSourceName.empty()) {
      deck.ndiSourceName = defaultNdiSourceName(deck, deckIndex);
    }
    if (deck.ndiKeySourceName.empty()) {
      deck.ndiKeySourceName = defaultNdiKeySourceName(deck, deckIndex);
    }

    clearSenders();

    NDIlib_send_create_t fillCreate {};
    fillCreate.p_ndi_name = deck.ndiSourceName.c_str();
    fillCreate.p_groups = nullptr;
    fillCreate.clock_video = false;
    fillCreate.clock_audio = false;
    runtime->ndiSender = ndiApi_.sendCreateFn ? ndiApi_.sendCreateFn(&fillCreate) : nullptr;
    if (!runtime->ndiSender) {
      deck.ndiEnabled = false;
      if (withToast) {
        triggerToast("ndi sender failed");
      }
      return;
    }
    runtime->ndiSenderName = deck.ndiSourceName;

    if (deck.ndiKeyEnabled) {
      NDIlib_send_create_t keyCreate {};
      keyCreate.p_ndi_name = deck.ndiKeySourceName.c_str();
      keyCreate.p_groups = nullptr;
      keyCreate.clock_video = false;
      keyCreate.clock_audio = false;
      runtime->ndiKeySender = ndiApi_.sendCreateFn ? ndiApi_.sendCreateFn(&keyCreate) : nullptr;
      if (!runtime->ndiKeySender) {
        deck.ndiKeyEnabled = false;
      } else {
        runtime->ndiKeySenderName = deck.ndiKeySourceName;
      }
    }

    if (withToast) {
      triggerToast("ndi: " + currentNdiOutputLabel());
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
    markProjectDirty();
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
    markProjectDirty();
  }

  void setFocusedDeckNdiKeyEnabled(bool enabled) {
    Deck& deck = focusedDeckMutable();
    if (deck.ndiKeyEnabled == enabled && (!enabled || deck.ndiEnabled)) {
      return;
    }
    if (enabled) {
      deck.ndiEnabled = true;
      if (deck.ndiKeySourceName.empty()) {
        deck.ndiKeySourceName = defaultNdiKeySourceName(deck, project_.focusedDeckIndex);
      }
    }
    deck.ndiKeyEnabled = enabled;
    applyDeckNdiSettings(project_.focusedDeckIndex, true);
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleFocusedDeckNdiKey() {
    setFocusedDeckNdiKeyEnabled(!focusedDeck().ndiKeyEnabled);
  }

  void setFocusedDeckNdiKeyName(const std::string& requestedName) {
    Deck& deck = focusedDeckMutable();
    std::string normalized = trim(requestedName);
    if (normalized.empty()) {
      normalized = defaultNdiKeySourceName(deck, project_.focusedDeckIndex);
    }
    if (deck.ndiKeySourceName == normalized) {
      return;
    }
    deck.ndiKeySourceName = normalized;
    if (deck.ndiEnabled && deck.ndiKeyEnabled) {
      applyDeckNdiSettings(project_.focusedDeckIndex, true);
    } else {
      triggerToast("ndi key name: " + deck.ndiKeySourceName);
    }
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
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
    if (!runtime->ndiSender || (deck.ndiKeyEnabled && !runtime->ndiKeySender)) {
      applyDeckNdiSettings(deckIndex, false);
      if (!runtime->ndiSender || (deck.ndiKeyEnabled && !runtime->ndiKeySender)) {
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

    SDL_Texture* previousTarget = SDL_GetRenderTarget(runtime->outputRenderer);
    if (runtime->compositorTexture) {
      SDL_SetRenderTarget(runtime->outputRenderer, runtime->compositorTexture);
    }
    if (SDL_RenderReadPixels(runtime->outputRenderer, nullptr, SDL_PIXELFORMAT_BGRA32,
                             runtime->ndiFrameBuffer.data(), static_cast<int>(stride)) != 0) {
      if (runtime->compositorTexture) {
        SDL_SetRenderTarget(runtime->outputRenderer, previousTarget);
      }
      return;
    }
    if (runtime->compositorTexture) {
      SDL_SetRenderTarget(runtime->outputRenderer, previousTarget);
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

    if (deck.ndiKeyEnabled && runtime->ndiKeySender) {
      if (runtime->ndiKeyFrameBuffer.size() != frameBytes) {
        runtime->ndiKeyFrameBuffer.resize(frameBytes);
      }
      if (!runtime->ndiKeyFrameBuffer.empty()) {
        for (size_t i = 0; i + 3 < runtime->ndiFrameBuffer.size(); i += 4) {
          Uint8 a = runtime->ndiFrameBuffer[i + 3];
          runtime->ndiKeyFrameBuffer[i + 0] = a;
          runtime->ndiKeyFrameBuffer[i + 1] = a;
          runtime->ndiKeyFrameBuffer[i + 2] = a;
          runtime->ndiKeyFrameBuffer[i + 3] = 255;
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
        keyFrame.p_data = runtime->ndiKeyFrameBuffer.data();
        keyFrame.line_stride_in_bytes = static_cast<int>(stride);
        keyFrame.p_metadata = nullptr;
        keyFrame.timestamp = 0;
        ndiApi_.sendVideoFn(runtime->ndiKeySender, &keyFrame);
      }
    }
#else
    (void) deckIndex;
    (void) width;
    (void) height;
    (void) fpsHint;
#endif
  }

  void sendDeckNdiAudioSamples(int deckIndex, const std::vector<std::int16_t>& samples) {
#if defined(PLAYBOY_HAS_NDI_SDK)
    if (samples.empty()) {
      return;
    }
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime) {
      return;
    }
    const Deck& deck = project_.decks[deckIndex];
    if (!deck.ndiEnabled || !ndiApi_.sendAudioInterleaved16sFn) {
      return;
    }
    if (!runtime->ndiSender) {
      applyDeckNdiSettings(deckIndex, false);
      if (!runtime->ndiSender) {
        return;
      }
    }

    int channels = 2;
    int sampleCount = static_cast<int>(samples.size() / static_cast<size_t>(channels));
    if (sampleCount <= 0) {
      return;
    }

    NDIlib_audio_frame_interleaved_16s_t frame {};
    frame.sample_rate = kAudioRate;
    frame.no_channels = channels;
    frame.no_samples = sampleCount;
    frame.timecode = NDIlib_send_timecode_synthesize;
    frame.reference_level = 0;
    frame.p_data = const_cast<std::int16_t*>(samples.data());
    ndiApi_.sendAudioInterleaved16sFn(runtime->ndiSender, &frame);
#else
    (void) deckIndex;
    (void) samples;
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

  int ndiKeyConnectionCount(int deckIndex) const {
#if defined(PLAYBOY_HAS_NDI_SDK)
    const DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->ndiKeySender || !ndiApi_.sendConnectionsFn) {
      return 0;
    }
    return std::max(0, ndiApi_.sendConnectionsFn(runtime->ndiKeySender, 0));
#else
    (void) deckIndex;
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

  std::pair<int, int> outputRenderSizeForDeck(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return fixedOutputRenderSize();
    }
    const Deck& deck = project_.decks[deckIndex];
    if (project_.outputFollowDisplay) {
      return displayNativeRenderSize(deck.outputDisplayIndex);
    }
    return fixedOutputRenderSize();
  }

  std::string outputResolutionLabel(int deckIndex) const {
    auto [w, h] = outputRenderSizeForDeck(deckIndex);
    return std::to_string(w) + "x" + std::to_string(h);
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

  std::string outputBitDepthActiveLabel(int deckIndex) const {
    const DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->outputRenderer) {
      return "n/a";
    }
    return std::to_string(runtime->compositorBitDepth) + "-bit";
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

  bool configureDeckCompositor(int deckIndex, int width = -1, int height = -1) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
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
          auto [rasterW, rasterH] = outputRenderSizeForDeck(deckIndex);
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
    return true;
  }

  void applyOutputBitDepthAllDecks() {
    for (int deckIndex = 0; deckIndex < static_cast<int>(deckRuntimes_.size()); ++deckIndex) {
      configureDeckCompositor(deckIndex);
    }
  }

  void setOutputBitDepthMode(int mode) {
    int normalized = normalizeOutputBitDepthMode(mode);
    bool changed = normalized != normalizeOutputBitDepthMode(project_.outputBitDepth);
    project_.outputBitDepth = normalized;
    applyOutputBitDepthAllDecks();
    triggerToast("video depth: " + outputBitDepthModeLabel() + " (" + outputBitDepthActiveLabel(project_.focusedDeckIndex) + ")");
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
        auto [nativeW, nativeH] = displayNativeRenderSize(focusedDeck().outputDisplayIndex);
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

    applyOutputBitDepthAllDecks();
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      DeckRuntime* runtime = runtimeForDeck(deckIndex);
      if (!runtime || !runtime->outputWindow) {
        continue;
      }
      int ww = 0;
      int wh = 0;
      SDL_GetWindowSize(runtime->outputWindow, &ww, &wh);
      clampDeckCanvasViewToWindow(deckIndex, ww, wh);
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
    DeckRuntime* runtime = focusedRuntime();
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

  std::vector<int> refreshChoicesForDeck(int deckIndex) const {
    std::vector<int> refreshes;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return refreshes;
    }

    const Deck& deck = project_.decks[deckIndex];
    int displayCount = SDL_GetNumVideoDisplays();
    if (displayCount <= 0) {
      return refreshes;
    }
    int displayIndex = std::clamp(deck.outputDisplayIndex, 0, displayCount - 1);
    auto [targetW, targetH] = outputRenderSizeForDeck(deckIndex);

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

  bool selectDisplayModeForDeck(int deckIndex, SDL_DisplayMode& selectedMode) const {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return false;
    }
    const Deck& deck = project_.decks[deckIndex];
    int displayCount = SDL_GetNumVideoDisplays();
    if (displayCount <= 0) {
      return false;
    }
    int displayIndex = std::clamp(deck.outputDisplayIndex, 0, displayCount - 1);
    auto [targetW, targetH] = outputRenderSizeForDeck(deckIndex);
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

  bool enableDeckFullscreen(int deckIndex, bool withToast) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->outputWindow) {
      return false;
    }

    SDL_DisplayMode selectedMode {};
    if (selectDisplayModeForDeck(deckIndex, selectedMode)) {
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

    std::string title = std::string(kOutputTitle) + " - " + (deck.name.empty() ? deckDefaultName(deckIndex) : deck.name);
    runtime.outputWindow = SDL_CreateWindow(
      title.c_str(),
      SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED,
      targetW,
      targetH,
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
    if (!configureDeckCompositor(deckIndex)) {
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

  void applyOutputDisplaySelection(int deckIndex) {
    Deck& deck = project_.decks[deckIndex];
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->outputWindow) {
      return;
    }

    int displayCount = SDL_GetNumVideoDisplays();
    bool haveDisplayBounds = false;
    SDL_Rect bounds {};
    if (displayCount > 0) {
      deck.outputDisplayIndex = std::clamp(deck.outputDisplayIndex, 0, displayCount - 1);
      haveDisplayBounds = (SDL_GetDisplayBounds(deck.outputDisplayIndex, &bounds) == 0);
    } else {
      deck.outputDisplayIndex = 0;
    }

    auto [targetW, targetH] = outputRenderSizeForDeck(deckIndex);
    targetW = std::max(1, targetW);
    targetH = std::max(1, targetH);

    Uint32 flags = SDL_GetWindowFlags(runtime->outputWindow);
    bool fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
    if (fullscreen) {
      SDL_SetWindowFullscreen(runtime->outputWindow, 0);
    }

    SDL_SetWindowSize(runtime->outputWindow, targetW, targetH);
    if (haveDisplayBounds) {
      int x = bounds.x + std::max(0, (bounds.w - targetW) / 2) + deckIndex * 20;
      int y = bounds.y + std::max(0, (bounds.h - targetH) / 2) + deckIndex * 20;
      if (targetW > bounds.w) x = bounds.x + 20 + deckIndex * 20;
      if (targetH > bounds.h) y = bounds.y + 20 + deckIndex * 20;
      SDL_SetWindowPosition(runtime->outputWindow, x, y);
    }

    if (fullscreen) {
      enableDeckFullscreen(deckIndex, false);
    }
    configureDeckCompositor(deckIndex);
  }

  void applyOutputDisplaySelectionAllDecks(bool restartLiveBrowsers) {
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      applyOutputDisplaySelection(deckIndex);
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
    applyOutputDisplaySelectionAllDecks(true);
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
    applyOutputDisplaySelectionAllDecks(true);
    triggerToast("video mode: fixed " + std::to_string(w) + "x" + std::to_string(h));
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  void sizeFocusedOutputToSelectedDisplay() {
    applyOutputDisplaySelection(project_.focusedDeckIndex);
    restartLiveBrowserCueIfNeeded(project_.focusedDeckIndex);
    triggerToast("output sized: " + outputResolutionLabel(project_.focusedDeckIndex));
    playUiSound(UiSoundEffect::Toggle);
  }

  void setOutputRefreshRate(double hz) {
    double normalized = (!std::isfinite(hz) || hz <= 0.0) ? 0.0 : std::clamp(hz, 1.0, 240.0);
    bool changed = std::abs(project_.outputRefreshRateHz - normalized) > 0.0001;
    project_.outputRefreshRateHz = normalized;
    applyOutputDisplaySelectionAllDecks(false);
    triggerToast("video refresh: " + outputRefreshRateLabel());
    playUiSound(UiSoundEffect::Toggle);
    if (changed) {
      markProjectDirty();
    }
  }

  void cycleOutputRefreshRate(int direction) {
    auto choices = refreshChoicesForDeck(project_.focusedDeckIndex);
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
    Deck& deck = focusedDeckMutable();
    deck.outputDisplayIndex = (deck.outputDisplayIndex + direction + displayCount) % displayCount;
    applyOutputDisplaySelection(project_.focusedDeckIndex);
    restartLiveBrowserCueIfNeeded(project_.focusedDeckIndex);
    std::string label = SDL_GetDisplayName(deck.outputDisplayIndex);
    triggerToast("display: "
      + (label.empty() ? std::to_string(deck.outputDisplayIndex + 1) : label)
      + "  " + outputResolutionLabel(project_.focusedDeckIndex));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool setOutputDisplayIndex(int index) {
    int displayCount = SDL_GetNumVideoDisplays();
    if (displayCount <= 0 || index < 0 || index >= displayCount) {
      return false;
    }
    focusedDeckMutable().outputDisplayIndex = index;
    applyOutputDisplaySelection(project_.focusedDeckIndex);
    restartLiveBrowserCueIfNeeded(project_.focusedDeckIndex);
    triggerToast("display: " + currentDisplayLabel() + "  " + outputResolutionLabel(project_.focusedDeckIndex));
    markProjectDirty();
    return true;
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
    const Deck& deck = focusedDeck();
    std::string source = deck.ndiSourceName.empty()
      ? defaultNdiSourceName(deck, project_.focusedDeckIndex)
      : deck.ndiSourceName;
    std::string keySource = deck.ndiKeySourceName.empty()
      ? defaultNdiKeySourceName(deck, project_.focusedDeckIndex)
      : deck.ndiKeySourceName;
    if (!deck.ndiEnabled) {
      return "off";
    }
#if defined(PLAYBOY_HAS_NDI_SDK)
    const DeckRuntime* runtime = focusedRuntime();
    bool live = runtime && runtime->ndiSender;
    bool keyLive = runtime && runtime->ndiKeySender;
    int listeners = ndiConnectionCount(project_.focusedDeckIndex);
    int keyListeners = ndiKeyConnectionCount(project_.focusedDeckIndex);
    std::string suffix = live ? "on" : "pending";
    if (listeners > 0) {
      suffix += " (" + std::to_string(listeners) + " rx)";
    }
    std::string label = suffix + " / fill:" + source;
    if (deck.ndiKeyEnabled) {
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
    if (deck.ndiEnabled) {
      output << " | ndi:" << (deck.ndiSourceName.empty() ? defaultNdiSourceName(deck, deckIndex) : deck.ndiSourceName);
    }
    if (deck.timecodeChaseEnabled) {
      output << " | tc:" << formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps);
    }
    return output.str();
  }

  std::string buildStatusSnapshot() const {
    std::ostringstream output;
    output << "PLAYBOY_0.01"
           << " focus=" << (project_.focusedDeckIndex + 1)
           << " decks=" << project_.decks.size()
           << " video_mode=" << (project_.outputFollowDisplay ? "native" : "fixed")
           << " video_hz=" << formatRefreshRateLabel(project_.outputRefreshRateHz)
           << " video_depth=" << outputBitDepthModeLabel()
           << " canvas=" << (project_.outputCanvasEnabled
                ? (std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
                : "off")
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
             << " route=" << (resolveDeckOutputHostIndex(deckIndex) + 1)
             << " layer=" << deck.outputLayerIndex
             << " raster=" << outputResolutionLabel(deckIndex)
             << " depth=" << outputBitDepthActiveLabel(deckIndex)
             << " audio=\"" << (deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\""
             << " ndi=" << (deck.ndiEnabled ? "on" : "off")
             << " ndi_name=\"" << (deck.ndiSourceName.empty() ? defaultNdiSourceName(deck, deckIndex) : deck.ndiSourceName) << "\""
             << " ndi_rx=" << ndiConnectionCount(deckIndex)
             << " ndi_key=" << (deck.ndiKeyEnabled ? "on" : "off")
             << " ndi_key_name=\"" << (deck.ndiKeySourceName.empty() ? defaultNdiKeySourceName(deck, deckIndex) : deck.ndiKeySourceName) << "\""
             << " ndi_key_rx=" << ndiKeyConnectionCount(deckIndex)
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
           << " display=" << (deck.outputDisplayIndex + 1)
           << " route=" << (resolveDeckOutputHostIndex(deckIndex) + 1)
           << " layer=" << deck.outputLayerIndex
           << " raster=" << outputResolutionLabel(deckIndex)
           << " depth=" << outputBitDepthActiveLabel(deckIndex)
           << " audio=\"" << (deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\""
           << " ndi=" << (deck.ndiEnabled ? "on" : "off")
           << " ndi_name=\"" << (deck.ndiSourceName.empty() ? defaultNdiSourceName(deck, deckIndex) : deck.ndiSourceName) << "\""
           << " ndi_rx=" << ndiConnectionCount(deckIndex)
           << " ndi_key=" << (deck.ndiKeyEnabled ? "on" : "off")
           << " ndi_key_name=\"" << (deck.ndiKeySourceName.empty() ? defaultNdiKeySourceName(deck, deckIndex) : deck.ndiKeySourceName) << "\""
           << " ndi_key_rx=" << ndiKeyConnectionCount(deckIndex)
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
           << "\"app\":\"PLAYBOY_0.01\","
           << "\"focusedDeck\":" << (project_.focusedDeckIndex + 1) << ","
           << "\"deckCount\":" << project_.decks.size() << ","
           << "\"outputMode\":\"" << (project_.outputFollowDisplay ? "native" : "fixed") << "\","
           << "\"outputRefreshHz\":" << project_.outputRefreshRateHz << ","
           << "\"outputBitDepthMode\":\"" << escapeJson(outputBitDepthModeLabel()) << "\","
           << "\"outputCanvasEnabled\":" << (project_.outputCanvasEnabled ? "true" : "false") << ","
           << "\"outputCanvasWidth\":" << project_.outputCanvasWidth << ","
           << "\"outputCanvasHeight\":" << project_.outputCanvasHeight << ","
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
             << "\"routeOutput\":" << (resolveDeckOutputHostIndex(deckIndex) + 1) << ","
             << "\"layer\":" << deck.outputLayerIndex << ","
             << "\"raster\":\"" << outputResolutionLabel(deckIndex) << "\","
             << "\"outputDepth\":\"" << outputBitDepthActiveLabel(deckIndex) << "\","
             << "\"audio\":\"" << escapeJson(deck.audioOutputDeviceName.empty() ? "system default" : deck.audioOutputDeviceName) << "\","
             << "\"ndiEnabled\":" << (deck.ndiEnabled ? "true" : "false") << ","
             << "\"ndiName\":\"" << escapeJson(deck.ndiSourceName.empty() ? defaultNdiSourceName(deck, deckIndex) : deck.ndiSourceName) << "\","
             << "\"ndiReceivers\":" << ndiConnectionCount(deckIndex) << ","
             << "\"ndiKeyEnabled\":" << (deck.ndiKeyEnabled ? "true" : "false") << ","
             << "\"ndiKeyName\":\"" << escapeJson(deck.ndiKeySourceName.empty() ? defaultNdiKeySourceName(deck, deckIndex) : deck.ndiKeySourceName) << "\","
             << "\"ndiKeyReceivers\":" << ndiKeyConnectionCount(deckIndex) << ","
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
    statusDeckSnapshots_.clear();
    statusDeckSnapshots_.reserve(project_.decks.size());
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      statusDeckSnapshots_.push_back(buildDeckStatusSnapshot(deckIndex));
    }
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

  void onSelectionChanged() {
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
    return "{\"app\":\"PLAYBOY_0.01\",\"deckCount\":0,\"decks\":[]}\n";
  }

  void maybeBroadcastOscState() {
    Uint64 now = SDL_GetTicks64();
    if (oscSubscribers_.empty()) {
      return;
    }

    std::string snapshot = snapshotJsonForFeedback();
    bool changed = snapshot != lastOscFeedbackPayload_;
    if (!changed && now - lastOscFeedbackBroadcastMs_ < 2000) {
      return;
    }

    std::vector<std::string> stale;
    for (const auto& [key, entry] : oscSubscribers_) {
      if (now > entry.second + 30000) {
        stale.push_back(key);
        continue;
      }
      sendOscStringTo(entry.first, "/playboy/state", snapshot);
    }
    for (const auto& key : stale) {
      oscSubscribers_.erase(key);
    }

    lastOscFeedbackPayload_ = snapshot;
    lastOscFeedbackBroadcastMs_ = now;
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
    oscSubscribers_.clear();
    lastOscFeedbackPayload_.clear();
    lastOscFeedbackBroadcastMs_ = 0;
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
    snd_seq_set_client_name(midiSeq_, "Playboy");
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
                sendOscStringTo(sender, "/playboy/pong", "PLAYBOY_0.01");
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
        if (deck.selectedIndex != *index) {
          deck.selectedIndex = *index;
          onSelectionChanged();
          triggerToast("cue " + std::to_string(*index + 1) + " armed");
          markProjectDirty();
        }
      }
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
        focusedDeckMutable().selectedIndex = *index;
        onSelectionChanged();
      }
      takeSelected(parts.size() > 2 && toUpper(parts[2]) == "AUTO");
      return;
    }
    if (command == "TAKEID") {
      takeCueById(joinParts(parts, 1), true);
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
      deck.selectedIndex = *index;
      onSelectionChanged();
      takeSelected(true);
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
          setFocusedDeckTimecode(*parsed);
        }
        return;
      }
      auto parsed = parseTimecodeSeconds(joinParts(parts, 1), focusedDeck().timecodeFps);
      if (parsed) {
        setFocusedDeckTimecode(*parsed);
      }
      return;
    }
    if (command == "PATTERN") {
      std::string typeId = parts.size() > 1 ? toLower(joinParts(parts, 1)) : "pocket-test";
      addPatternCue(typeId);
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
        int hostIndex = resolveDeckOutputHostIndex(project_.focusedDeckIndex);
        int nextTop = nextLayerIndexForOutputHost(hostIndex, project_.focusedDeckIndex);
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
        triggerToast("video: " + outputSizingModeLabel() + " " + outputResolutionLabel(project_.focusedDeckIndex)
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
          auto [rasterW, rasterH] = outputRenderSizeForDeck(project_.focusedDeckIndex);
          setOutputCanvasMode(true, rasterW, rasterH);
          return;
        }
        if (canvasArg == "DOUBLE" || canvasArg == "2X") {
          auto [rasterW, rasterH] = outputRenderSizeForDeck(project_.focusedDeckIndex);
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
          triggerToast("video depth: " + outputBitDepthModeLabel() + " (" + outputBitDepthActiveLabel(project_.focusedDeckIndex) + ")");
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
      } else if (value == "KEY") {
        if (parts.size() == 2) {
          toggleFocusedDeckNdiKey();
        } else {
          std::string keyValue = toUpper(parts[2]);
          if (keyValue == "ON") {
            setFocusedDeckNdiKeyEnabled(true);
          } else if (keyValue == "OFF") {
            setFocusedDeckNdiKeyEnabled(false);
          } else if (keyValue == "TOGGLE") {
            toggleFocusedDeckNdiKey();
          } else if (keyValue == "NAME") {
            setFocusedDeckNdiKeyName(joinParts(parts, 3));
          } else if (keyValue == "DEFAULT" || keyValue == "CLEAR") {
            setFocusedDeckNdiKeyName("");
          }
        }
      } else if (value == "NAME") {
        setFocusedDeckNdiName(joinParts(parts, 2));
      } else if (value == "KEYNAME") {
        setFocusedDeckNdiKeyName(joinParts(parts, 2));
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
    if (command == "NDIKEY" || command == "NDIKEYER") {
      if (parts.size() <= 1) {
        toggleFocusedDeckNdiKey();
        return;
      }
      std::string value = toUpper(parts[1]);
      if (value == "ON") {
        setFocusedDeckNdiKeyEnabled(true);
      } else if (value == "OFF") {
        setFocusedDeckNdiKeyEnabled(false);
      } else if (value == "TOGGLE") {
        toggleFocusedDeckNdiKey();
      } else if (value == "NAME") {
        setFocusedDeckNdiKeyName(joinParts(parts, 2));
      } else if (value == "DEFAULT" || value == "CLEAR") {
        setFocusedDeckNdiKeyName("");
      }
      return;
    }
    if (command == "NDIKEYNAME") {
      setFocusedDeckNdiKeyName(joinParts(parts, 1));
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
          confirmQuit_ = true;
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
          handleKeyDown(event.key.keysym.sym, event.key.keysym.mod);
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
    double deltaSeconds = lastUpdateTickMs_ == 0 ? 0.0 : static_cast<double>(now - lastUpdateTickMs_) / 1000.0;
    lastUpdateTickMs_ = now;

    // Animate master video dimmer toward target (0.5 seconds to full black/restore)
    if (std::abs(project_.masterDimmer - masterDimmerTarget_) > 0.001) {
      constexpr double kDimSpeed = 2.0; // units per second (0→1 in 0.5s)
      double step = kDimSpeed * std::max(deltaSeconds, 1.0 / 120.0);
      project_.masterDimmer = std::clamp(
        project_.masterDimmer + std::copysign(std::min(step, std::abs(masterDimmerTarget_ - project_.masterDimmer)), masterDimmerTarget_ - project_.masterDimmer),
        0.0, 1.0);
    }

    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      Deck& deck = project_.decks[deckIndex];
      double fromTc = deck.timecodeCurrentSeconds;
      if (deck.timecodeDirty) {
        fromTc = deck.timecodeLastSeconds;
      }
      if (deck.timecodeRunEnabled && deltaSeconds > 0.0) {
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
        const std::string& pt = activeCue->path;
        bool animated = pt.find("pocket-test") != std::string::npos ||
                        pt.find("kawaii")      != std::string::npos;
        if (animated) {
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
              takeSelected(true);
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
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      renderOutputWindow(deckIndex);
    }
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

    // Dialog panel: 340x180, centred
    SDL_Rect dialog {(width - 340) / 2, (height - 180) / 2, 340, 180};
    Primitives::drawFramedPanel(controlRenderer_, dialog, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));

    drawText(controlRenderer_, fontLarge_, "QUIT PLAYBOY?", colorFromRgba(kScreenDeepColor), dialog.x + 24, dialog.y + 28);

    // YES / NO buttons
    quitYesBtn_ = {dialog.x + 26,  dialog.y + 90, 118, 44};
    quitNoBtn_  = {dialog.x + 196, dialog.y + 90, 118, 44};
    Primitives::drawFramedPanel(controlRenderer_, quitYesBtn_, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    Primitives::drawFramedPanel(controlRenderer_, quitNoBtn_,  colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    drawCenteredText(controlRenderer_, fontBase_, "YES", colorFromRgba(kScreenLightColor), quitYesBtn_);
    drawCenteredText(controlRenderer_, fontBase_, "NO",  colorFromRgba(kScreenLightColor), quitNoBtn_);

    drawText(controlRenderer_, fontSmall_, "esc or N to cancel", colorFromRgba(kScreenInkSoftColor), dialog.x + 26, dialog.y + 152);
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
    const int kDW = 520, kDH = 340;
    SDL_Rect dialog {(width - kDW) / 2, (height - kDH) / 2, kDW, kDH};
    Primitives::drawFramedPanel(controlRenderer_, dialog, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));

    // Title + file name
    int tx = dialog.x + 160;
    TTF_Font* titleFont = fontPixel_ ? fontPixel_ : fontLarge_;
    drawText(controlRenderer_, titleFont, "PLAYBOY_0.01", colorFromRgba(kScreenDeepColor), tx, dialog.y + 36);
    drawText(controlRenderer_, fontSmall_, "dot-matrix cue deck", colorFromRgba(kScreenInkSoftColor), tx, dialog.y + 70);

    std::string fname = currentProjectFile_.empty() ? "default.playboy" : currentProjectFile_.filename().string();
    bool hasSavedFile = !currentProjectFile_.empty() && fs::exists(currentProjectFile_);
    if (hasSavedFile) {
      drawText(controlRenderer_, fontBase_, "saved show found:", colorFromRgba(kScreenDeepColor), tx, dialog.y + 106);
      drawText(controlRenderer_, fontSmall_, fname, colorFromRgba(kScreenDarkColor), tx, dialog.y + 128);
    } else {
      drawText(controlRenderer_, fontBase_, "no saved show found", colorFromRgba(kScreenDeepColor), tx, dialog.y + 106);
    }

    // Buttons
    startupLoadBtn_ = {tx, dialog.y + 210, 156, 48};
    startupNewBtn_  = {tx + 172, dialog.y + 210, 156, 48};

    SDL_Color loadFill = hasSavedFile ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kShellOuterColor);
    SDL_Color loadText = hasSavedFile ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenMidColor);
    Primitives::drawFramedPanel(controlRenderer_, startupLoadBtn_, loadFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    drawCenteredText(controlRenderer_, fontBase_, hasSavedFile ? "LOAD SHOW" : "NO FILE", loadText, startupLoadBtn_);

    Primitives::drawFramedPanel(controlRenderer_, startupNewBtn_, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
    drawCenteredText(controlRenderer_, fontBase_, "NEW SHOW", colorFromRgba(kScreenDeepColor), startupNewBtn_);

    drawText(controlRenderer_, fontSmall_, "Enter = load   N = new show", colorFromRgba(kScreenInkSoftColor), tx, dialog.y + 282);
  }

  void renderControlWindow() {
    int numDecks = static_cast<int>(project_.decks.size());
    deckScrolls_.resize(numDecks, 0);
    deckColumnRects_.resize(numDecks);
    deckListClipRects_.resize(numDecks);

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
      drawText(controlRenderer_, titleFont, project_.title, colorFromRgba(kScreenDeepColor), header.x + 14, header.y + 8);
      if (project_.uiTransitionsEnabled) {
        SDL_Color starC = colorFromRgba(kScreenDeepColor);
        // Three orbiting sparkles with phase offsets
        int titleW = 0;
        if (fontPixel_) { TTF_SizeUTF8(fontPixel_, project_.title.c_str(), &titleW, nullptr); }
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
    std::string fxStatus =
      std::string("1·sfx:") + (project_.uiSoundsEnabled ? "on" : "off") +
      "  2·anim:" + (project_.uiTransitionsEnabled ? "on" : "off") +
      "  3·auto:" + (focDeck.autoAdvance ? "on" : "off") +
      "  4·loop:" + (focDeck.playlistLoop ? "on" : "off") +
      "  5·tc:" + (focDeck.timecodeChaseEnabled ? "chase" : "free");
    std::string companionStatus = companionReady_
      ? "companion " + std::to_string(companionPort_)
      : "companion off";
    drawText(controlRenderer_, fontSmall_, fxStatus, colorFromRgba(kScreenDeepColor), header.x + header.w - 480, header.y + 6);
    drawText(controlRenderer_, fontSmall_, companionStatus, colorFromRgba(kScreenDeepColor), header.x + header.w - 480, header.y + 22);
    drawText(controlRenderer_, fontSmall_, "file: " + currentProjectLabel(), colorFromRgba(kScreenDeepColor), header.x + header.w - 480, header.y + 38);
    // Master volume fader (horizontal slider at right of header)
    {
      constexpr int kFaderW = 110;
      constexpr int kFaderH = 14;
      int fx = header.x + header.w - 130;
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
               colorFromRgba(kScreenDeepColor), track.x, track.y - 14);
    }
    // Settings gear button
    {
      settingsGearRect_ = {header.x + header.w - 560, header.y + 6, 52, 40};
      SDL_Color gearFill = settingsOpen_ ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kShellInnerColor);
      SDL_Color gearInk  = settingsOpen_ ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
      Primitives::drawFramedPanel(controlRenderer_, settingsGearRect_, gearFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
      drawCenteredText(controlRenderer_, fontSmall_, "prefs", gearInk, settingsGearRect_);
      // BLK (blackout) button
      {
        blackoutBtnRect_ = {header.x + header.w - 614, header.y + 6, 48, 40};
        bool isBlacked = masterDimmerTarget_ < 0.5;
        SDL_Color blkFill = isBlacked ? SDL_Color{160, 18, 18, 255} : colorFromRgba(kShellInnerColor);
        SDL_Color blkInk  = isBlacked ? SDL_Color{255, 180, 180, 255} : colorFromRgba(kScreenDeepColor);
        Primitives::drawFramedPanel(controlRenderer_, blackoutBtnRect_, blkFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "BLK", blkInk, blackoutBtnRect_);
      }
    }

    // Content area: below header, above buttons
    int contentY = header.y + kGlobalHeaderH + 4;
    int contentH = (height - 74) - contentY - 4;

    // Deck columns
    int colStartX = shell.x + 4;
    for (int di = 0; di < numDecks; ++di) {
      SDL_Rect col {colStartX + di * (kColWidth + 4), contentY, kColWidth, contentH};
      deckColumnRects_[di] = col;
      renderPlaylistColumn(col, di);
    }

    // Main panel (program monitor + transport)
    int mainX = colStartX + numDecks * (kColWidth + 4);
    SDL_Rect mainPanel {mainX, contentY, shell.x + shell.w - 4 - mainX, contentH};
    if (mainPanel.w > 0) {
      Primitives::drawFramedPanel(controlRenderer_, mainPanel, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));
      renderMainPanel(mainPanel);
    }

    renderButtons();
    renderToast(width);
    if (confirmQuit_) {
      renderQuitConfirm();
    }
    if (showStartupDialog_) {
      renderStartupDialog();
    }
    // Popups rendered last (on top)
    renderContextMenu();
    renderSettingsModal();
    SDL_RenderPresent(controlRenderer_);
  }

  void renderPlaylistColumn(const SDL_Rect& col, int deckIndex) {
    const Deck& deck = project_.decks[deckIndex];
    bool focused = (deckIndex == project_.focusedDeckIndex);

    // Column header (deck name + active cue status)
    SDL_Rect colHeader {col.x, col.y, col.w, kColHeaderH};
    SDL_Color headerFill = focused ? colorFromRgba(kScreenMidColor) : colorFromRgba(kShellInnerColor);
    Primitives::drawFramedPanel(controlRenderer_, colHeader, headerFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));
    std::string deckName = deck.name.empty() ? deckDefaultName(deckIndex) : deck.name;
    drawText(controlRenderer_, fontBase_, deckName, colorFromRgba(kScreenDeepColor), col.x + 10, col.y + 8);
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
    drawText(controlRenderer_, fontSmall_, stateStr + activeName, colorFromRgba(kScreenDeepColor), col.x + 10, col.y + 34);

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
    std::string routing = deckOutputRoutingLabel(deckIndex)
      + "  disp:" + std::to_string(deck.outputDisplayIndex + 1)
      + "  res:" + outputResolutionLabel(deckIndex)
      + "  " + (deck.autoAdvance ? "auto" : "man")
      + "  " + (deck.playlistLoop ? "loop" : "once")
      + "  " + (deck.shuffle ? "shuf" : "seq");
    drawText(controlRenderer_, fontSmall_, routing, colorFromRgba(kScreenDeepColor), col.x + 8, footerY + 8);
    std::string routing2 = std::string("tc:") + (deck.timecodeChaseEnabled ? "chase" : "free")
      + "  ndi:" + currentNdiOutputLabel();
    drawText(controlRenderer_, fontSmall_, routing2, colorFromRgba(kScreenDeepColor), col.x + 8, footerY + 24);
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
    SDL_Color fill = colorFromRgba(kScreenLightColor);
    if (index == deck.selectedIndex) {
      fill = colorFromRgba(kScreenMidColor);
    } else if (index == deck.activeIndex) {
      fill = colorFromRgba(kScreenDarkColor);
    } else if (isOverlay) {
      fill = {48, 80, 48, 255};  // distinct teal-ish tint for active overlay
    }

    Primitives::drawFramedPanel(controlRenderer_, row, fill, colorFromRgba(kScreenDeepColor), colorFromRgba(kShellInnerColor));
    if (project_.uiTransitionsEnabled && index == deck.selectedIndex) {
      double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(animationNow_ - selectionChangedAt_) / 95.0);
      SDL_Color glow {155, 188, 15, static_cast<Uint8>(60 + pulse * 80.0)};
      Primitives::strokeRect(controlRenderer_, insetRect(row, 1), glow);
    }

    SDL_Rect chip {row.x + 12, row.y + 10, 10, row.h - 20};
    SDL_Color chipColor = !cue.colorTag.empty() ? colorTagToSdl(cue.colorTag) : cue.color;
    Primitives::fillRect(controlRenderer_, chip, chipColor);

    SDL_Color ink = index == deck.activeIndex ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor);
    SDL_Color subInk = index == deck.activeIndex ? colorFromRgba(kShellOuterColor) : colorFromRgba(kScreenDarkColor);
    // Cue number badge (user-assigned label)
    int nameX = row.x + 34;
    if (!cue.cueNumber.empty()) {
      std::string numBadge = cue.cueNumber;
      drawText(controlRenderer_, fontSmall_, numBadge, colorFromRgba(kScreenInkSoftColor), nameX, row.y + 4);
      nameX += static_cast<int>(numBadge.size()) * 7 + 4;
    }
    drawText(controlRenderer_, fontBase_, cue.name, ink, nameX, row.y + 13);
    std::string meta = cueKindLabel(cue.kind);
    if (cue.kind == CueKind::Video) {
      meta += "  ";
      meta += formatSeconds(cue.duration);
      if (cue.hasAudio) meta += "  + audio";
    } else if (cue.kind == CueKind::Browser) {
      meta += "  web";
      if (cue.stillDurationSeconds > 0.0) meta += "  " + formatSeconds(cue.stillDurationSeconds);
    } else if (cue.kind == CueKind::Image || cue.kind == CueKind::Pattern) {
      if (cue.stillDurationSeconds > 0.0) meta += "  " + formatSeconds(cue.stillDurationSeconds);
      else meta += "  hold";
    } else if (cue.kind == CueKind::LowerThird) {
      if (!cue.lowerThirdText.empty()) meta += "  \"" + cue.lowerThirdText + "\"";
      if (isOverlay) meta += "  [LIVE]";
    }
    drawText(controlRenderer_, fontSmall_, meta, subInk, row.x + 34, row.y + 39);

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
    if (index == deck.activeIndex) {
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
        case CueKind::LowerThird: rowTip = "Enter=push overlay  Backspace=pop  LOWERTEXT via Companion"; break;
        default: rowTip = "Enter=take  Delete=remove"; break;
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
      drawCenteredText(controlRenderer_, fontBase_, button.label, button.text, button.rect);
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
    drawText(controlRenderer_, fontSmall_, "cute mode", toast_.ink, panel.x + 14, panel.y + 10);
    drawText(controlRenderer_, fontBase_, toast_.message, toast_.ink, panel.x + 14, panel.y + 28);
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

  // ── Pixel-art character system ───────────────────────────────────────────────
  // Playboy-bunny style pixel art women. Full RGB, no palette restrictions.
  // Three variants (0,1,2) used across different panels.
  //   cy = hip centre. Crown (excl ears) at cy-20*S. Heel tips at cy+24*S.

  void drawPixelLady(SDL_Renderer* r, int cx, int cy, int S, Uint64 now,
                     Uint8 alpha, int variant = 0) {
    // Portrait bust view — chunky retro pixel art, limited palette.
    // cy = face-centre (approximately mid-eye level).
    // Art bounds: x -10..+10, y -16..+14  →  21×31 art units.
    if (alpha == 0 || S <= 0) return;
    bool blink = (now % 4500) < 120;

    auto a = [&](Uint8 base) -> Uint8 {
      return static_cast<Uint8>(static_cast<int>(base) * alpha / 255);
    };
    auto fb = [&](int ax, int ay, int aw, int ah, SDL_Color c) {
      Primitives::fillRect(r, {cx + ax * S, cy + ay * S, aw * S, ah * S}, c);
    };

    // ── Palette (4-5 colours + outline, high contrast) ───────────────────
    SDL_Color hair  = variant == 0 ? SDL_Color{  0, 178, 200, a(255)}  // teal
                    : variant == 1 ? SDL_Color{155,  28, 155, a(255)}  // purple
                    :                SDL_Color{205, 158,  18, a(255)}; // blonde
    SDL_Color hairD = {static_cast<Uint8>(hair.r >> 1),
                       static_cast<Uint8>(hair.g >> 1),
                       static_cast<Uint8>(hair.b >> 1), a(255)};
    SDL_Color outl  = {12,   8,  20, a(255)};   // near-black outline
    SDL_Color earPk = {255, 138, 168, a(255)};  // bunny ear pink
    SDL_Color earLt = {255, 200, 215, a(255)};  // ear inner light
    SDL_Color skin  = {240, 198, 158, a(255)};  // face skin
    SDL_Color skinD = {192, 148, 108, a(255)};  // skin shadow
    SDL_Color eyeW  = {255, 255, 255, a(255)};
    SDL_Color iris  = { 48, 105, 218, a(255)};
    SDL_Color pupil = {10,   8,  20, a(255)};
    SDL_Color lash  = outl;
    SDL_Color lip   = {218,  45,  72, a(255)};
    SDL_Color blush = {255, 138, 148, a(100)};
    SDL_Color leot  = {244, 244, 252, a(255)};  // white leotard
    SDL_Color leotS = {188, 188, 210, a(255)};  // leotard side shadow
    SDL_Color cuff  = leot;

    // ── Dark poster card ─────────────────────────────────────────────────
    SDL_Rect card {cx - 10*S, cy - 16*S, 21*S, 31*S};
    Primitives::fillRect(r, card, {0x0C, 0x05, 0x18, a(225)});
    SDL_Color bord = {188, 28, 82, a(90)};
    Primitives::fillRect(r, {card.x - S,      card.y - S,     card.w + 2*S, S      }, bord);
    Primitives::fillRect(r, {card.x - S,      card.y+card.h,  card.w + 2*S, S      }, bord);
    Primitives::fillRect(r, {card.x - S,      card.y,         S,  card.h           }, bord);
    Primitives::fillRect(r, {card.x+card.w,   card.y,         S,  card.h           }, bord);

    // ── BUNNY EARS (tall, slender, pink) ─────────────────────────────────
    fb(-4, -16,  2,  7, earPk);   // left ear outer
    fb(-3, -15,  1,  5, earLt);   // left ear inner
    fb( 3, -16,  2,  7, earPk);   // right ear outer
    fb( 4, -15,  1,  5, earLt);   // right ear inner

    // ── HAIR (wide mass either side, back layer) ──────────────────────────
    fb(-9,  -9,  5, 19, hairD);   // left hair dark mass
    fb(-8,  -8,  3, 17, hair);    // left hair fill
    fb( 5,  -9,  5, 19, hairD);   // right dark mass
    fb( 6,  -8,  3, 17, hair);    // right hair fill
    fb(-5, -12, 11,  4, hair);    // crown cap
    // Crown highlight streak
    fb(-3, -12,  5,  1, {static_cast<Uint8>(std::min(255,(int)hair.r+50)),
                          static_cast<Uint8>(std::min(255,(int)hair.g+50)),
                          static_cast<Uint8>(std::min(255,(int)hair.b+50)), a(190)});

    // ── FACE (main skin area) ─────────────────────────────────────────────
    fb(-5, -11, 11, 10, skin);   // face fill
    fb(-4,  -1,  9,  1, skin);   // chin row 1
    fb(-3,   0,  7,  1, skin);   // chin row 2
    fb(-2,   1,  5,  1, skin);   // chin tip
    // Ear lobes (sides of face)
    fb(-6,  -8,  1,  3, skinD);
    fb( 6,  -8,  1,  3, skinD);
    // Blush
    fb(-5,  -6,  2,  1, blush);
    fb( 4,  -6,  2,  1, blush);

    // ── EYEBROWS ─────────────────────────────────────────────────────────
    fb(-5, -10,  3,  1, outl);
    fb( 3, -10,  3,  1, outl);

    // ── EYES (5×5 each — BIG portrait anime eyes) ─────────────────────────
    if (!blink) {
      // Left eye
      fb(-5,  -9,  5,  5, eyeW);
      fb(-5,  -9,  5,  1, lash);    // top lash line
      fb(-5,  -8,  5,  3, iris);    // iris
      fb(-5,  -7,  5,  2, {static_cast<Uint8>(iris.r>>1),
                            static_cast<Uint8>(iris.g>>1),
                            static_cast<Uint8>(iris.b>>1), a(255)});
      fb(-4,  -8,  3,  2, pupil);   // pupil
      fb(-4,  -8,  1,  1, eyeW);    // top-left shine
      fb(-2,  -6,  1,  1, {200, 218, 255, a(155)});  // lower shine
      fb(-5,  -4,  5,  1, lash);    // bottom lash
      fb(-6,  -9,  1,  2, lash);    // outer corner flick
      // Right eye
      fb( 1,  -9,  5,  5, eyeW);
      fb( 1,  -9,  5,  1, lash);
      fb( 1,  -8,  5,  3, iris);
      fb( 1,  -7,  5,  2, {static_cast<Uint8>(iris.r>>1),
                            static_cast<Uint8>(iris.g>>1),
                            static_cast<Uint8>(iris.b>>1), a(255)});
      fb( 2,  -8,  3,  2, pupil);
      fb( 2,  -8,  1,  1, eyeW);
      fb( 4,  -6,  1,  1, {200, 218, 255, a(155)});
      fb( 1,  -4,  5,  1, lash);
      fb( 6,  -9,  1,  2, lash);
    } else {
      fb(-5,  -7,  5,  1, lash);
      fb( 1,  -7,  5,  1, lash);
    }

    // ── NOSE ─────────────────────────────────────────────────────────────
    fb( 0,  -3,  1,  1, skinD);

    // ── LIPS ─────────────────────────────────────────────────────────────
    fb(-3,  -2,  7,  1, lip);
    fb(-2,  -1,  5,  1, lip);
    fb(-3,  -2,  2,  1, {static_cast<Uint8>(std::min(255,(int)lip.r+30)),
                          lip.g, lip.b, a(175)});  // cupid's bow

    // ── NECK ─────────────────────────────────────────────────────────────
    fb(-2,   2,  5,  3, skin);
    fb(-1,   2,  1,  2, skinD);
    fb( 3,   2,  1,  2, skinD);

    // ── BOW TIE ──────────────────────────────────────────────────────────
    fb(-4,   5,  3,  2, outl);
    fb( 2,   5,  3,  2, outl);
    fb(-1,   4,  3,  4, outl);
    fb( 0,   5,  1,  2, {178, 178, 198, a(198)});

    // ── DÉCOLLETAGE (skin above leotard) ──────────────────────────────────
    fb(-7,   7, 15,  5, skin);    // wide chest
    fb(-6,   8,  6,  3, skinD);   // left shadow curve
    fb( 1,   8,  6,  3, skinD);   // right shadow curve
    fb(-5,   8,  5,  3, skin);    // left highlight
    fb( 1,   8,  5,  3, skin);    // right highlight
    fb(-1,   8,  3,  4, skinD);   // cleavage centre

    // ── STRAPLESS LEOTARD (just the top band, bust-view) ──────────────────
    fb(-8,  12, 17,  1, {255, 255, 255, a(252)});  // bright neckline
    fb(-8,  13, 17,  2, leot);
    fb(-8,  12,  1,  3, leotS);   // left edge shadow
    fb( 8,  12,  1,  3, leotS);   // right edge shadow

    // ── WRIST CUFFS (arms partially in frame) ────────────────────────────
    fb(-10, 13,  3,  2, cuff);
    fb(  8, 13,  3,  2, cuff);
  }

  // ── Wrapper helpers ────────────────────────────────────────────────────────

  void drawMascot(SDL_Renderer* r, int cx, int cy, int S, Uint64 now, bool /*lightBg*/) {
    int floatY = static_cast<int>(std::sin(static_cast<double>(now) * 0.0013) * 2.5);
    drawPixelLady(r, cx, cy + floatY, S, now, 255, 0);
  }

  void drawStandbyFigure(SDL_Renderer* r, int cx, int baseY, Uint64 now) {
    // Figure: heel-tip = cy+24*S → cy = baseY - 24*S
    constexpr int S = 3;
    int floatY = static_cast<int>(std::sin(static_cast<double>(now) * 0.0011) * 4.0);
    int cy = baseY - 24 * S + floatY;
    drawPixelLady(r, cx, cy, S, now, 255, 0);
  }

  void drawHeaderIcon(SDL_Renderer* r, int cx, int cy, Uint64 now) {
    // Tiny S=1 figure in the header bar; shift so face is visible
    drawPixelLady(r, cx, cy - 2, 1, now, 200, 0);
  }

  // Draw as semi-transparent background watermark.
  // variant 0=black bunny, 1=magenta bunny, 2=blue bunny.
  void drawGirlBg(SDL_Renderer* r, const SDL_Rect& panelRect, Uint64 now,
                  Uint8 alpha = 45, int variant = 0) {
    if (alpha == 0) return;
    // Scale so figure is ~55% of panel height (total span = 46 art-units)
    int S = std::max(1, panelRect.h / 84);
    int cx = panelRect.x + panelRect.w * 2 / 3;
    int cy = panelRect.y + panelRect.h / 2;
    drawPixelLady(r, cx, cy, S, now, alpha, variant);
  }

  // Stub so any remaining drawAnimeGirl calls compile
  void drawAnimeGirl(SDL_Renderer* r, int cx, int cy, int S, Uint64 now, Uint8 alpha) {
    drawPixelLady(r, cx, cy, S, now, alpha, 0);
  }



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

    drawText(controlRenderer_, fontSmall_, "little screen", colorFromRgba(kScreenDeepColor), x, y);
    drawText(controlRenderer_, fontLarge_, activeCue ? activeCue->name : "No cue loaded", colorFromRgba(kScreenDeepColor), x, y + 22);

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

    progressBarRect_ = {x, y + 108, panel.w - 52, 20};
    Primitives::drawFramedPanel(controlRenderer_, progressBarRect_, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
    double duration = engine ? engine->duration() : 0.0;
    double fraction = duration > 0.0 ? (engine ? engine->position() / duration : 0.0) : 0.0;
    fraction = std::clamp(fraction, 0.0, 1.0);
    SDL_Rect fillBar = insetRect(progressBarRect_, 3);
    fillBar.w = static_cast<int>(std::round(progressBarRect_.w * fraction));
    Primitives::fillRect(controlRenderer_, fillBar, colorFromRgba(kScreenDarkColor));

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
      int btnY = y + 130;
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

    // --- Program monitor / live video preview ---
    bool hasLiveVideo = controlPreviewTex_ && controlPreviewTexW_ > 0 && controlPreviewTexH_ > 0;
    SDL_Color previewBg = hasLiveVideo ? colorFromRgba(kScreenDeepColor) : colorFromRgba(kScreenLightColor);
    SDL_Color previewBorder = hasLiveVideo ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor);
    SDL_Rect preview {x, midY, previewW, midH};
    Primitives::drawFramedPanel(controlRenderer_, preview, previewBg, colorFromRgba(kScreenDeepColor), previewBorder);
    drawText(controlRenderer_, fontSmall_, "program monitor",
             hasLiveVideo ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenDeepColor),
             preview.x + 10, preview.y + 6);

    if (hasLiveVideo) {
      SDL_Rect inner {preview.x + 4, preview.y + 24, preview.w - 8, preview.h - 28};
      float aspect = static_cast<float>(controlPreviewTexW_) / static_cast<float>(controlPreviewTexH_);
      int drawW = inner.w;
      int drawH = static_cast<int>(drawW / aspect);
      if (drawH > inner.h) {
        drawH = inner.h;
        drawW = static_cast<int>(drawH * aspect);
      }
      SDL_Rect dst {inner.x + (inner.w - drawW) / 2, inner.y + (inner.h - drawH) / 2, drawW, drawH};
      SDL_SetTextureBlendMode(controlPreviewTex_, SDL_BLENDMODE_NONE);
      SDL_RenderCopy(controlRenderer_, controlPreviewTex_, nullptr, &dst);
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

    // NO characters inside the program monitor — output-adjacent areas stay clean.

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
      drawText(controlRenderer_, fontSmall_, "no cue selected",
               colorFromRgba(kScreenDarkColor), thumbArea.x + 6, thumbArea.y + 8);
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

    // Label "cue settings" below thumb
    int ctrlSettingsY = ctrl.y + kThumbAreaH + 10;
    drawText(controlRenderer_, fontSmall_, "cue settings", colorFromRgba(kScreenDeepColor), ctrl.x + 10, ctrlSettingsY);

    auto drawQuickRow = [&](int rowY, const std::string& label, QuickAction decAction, const std::string& value,
                            QuickAction incAction, QuickAction toggleAction = QuickAction::ToggleLoop,
                            bool isToggle = false, bool toggleOn = false, std::string tip = "") {
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
        SDL_Rect incBtn {rx + kLabelW + kBtnW + 4 + kValW + 4, rowY, kBtnW, kRowH};
        Primitives::drawFramedPanel(controlRenderer_, incBtn, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
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
        drawQuickRow(rowY, "scale X", QuickAction::ScaleXDec, formatFloat(cue.outputScaleX, 2) + "x", QuickAction::ScaleXInc,
                     QuickAction::ToggleLoop, false, false, "Output X scale (0.25–4.0×)");
        rowY += kRowStep;
        drawQuickRow(rowY, "scale Y", QuickAction::ScaleYDec, formatFloat(cue.outputScaleY, 2) + "x", QuickAction::ScaleYInc,
                     QuickAction::ToggleLoop, false, false, "Output Y scale (0.25–4.0×)");
        rowY += kRowStep;
        drawQuickRow(rowY, "off X", QuickAction::OffsetXDec, std::to_string(static_cast<int>(cue.outputOffsetX)) + "px", QuickAction::OffsetXInc,
                     QuickAction::ToggleLoop, false, false, "Horizontal output offset in pixels");
        rowY += kRowStep;
        drawQuickRow(rowY, "off Y", QuickAction::OffsetYDec, std::to_string(static_cast<int>(cue.outputOffsetY)) + "px", QuickAction::OffsetYInc,
                     QuickAction::ToggleLoop, false, false, "Vertical output offset in pixels");
        rowY += kRowStep;
      }
      drawQuickRow(rowY, "rot", QuickAction::RotDec, formatFloat(cue.outputRotationDegrees, 1) + " deg", QuickAction::RotInc,
                   QuickAction::ToggleLoop, false, false, "Output rotation angle (-180..180)");
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

    if (selectedCue && selectedCue->kind == CueKind::Video) {
      int volPct = static_cast<int>(std::round((engine ? engine->volume() : 1.0f) * 100.0f));
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      constexpr int kRowStep = 28;
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
      // Loop count, speed, color tag
      {
        std::string loopStr = selectedCue->loopCount == 0 ? "inf" : std::to_string(selectedCue->loopCount) + "x";
        drawQuickRow(ry + kRowStep * 8, "repeats", QuickAction::LoopCountDec, loopStr, QuickAction::LoopCountInc,
                     QuickAction::ToggleLoop, false, false, "Fixed repeat count — 0 = loop forever");
        std::ostringstream spdSS;
        spdSS << std::fixed << std::setprecision(2) << selectedCue->playbackSpeed;
        drawQuickRow(ry + kRowStep * 9, "speed", QuickAction::SpeedDec, spdSS.str() + "x", QuickAction::SpeedInc,
                     QuickAction::ToggleLoop, false, false, "Playback speed: 0.25–4.0×");
        std::string tagStr = selectedCue->colorTag.empty() ? "none" : selectedCue->colorTag;
        SDL_Rect tagBtn {ctrl.x + 10, ry + kRowStep * 10, kCtrlW - 20, 28};
        SDL_Color tagFill = colorTagToSdl(selectedCue->colorTag, 200);
        Primitives::drawFramedPanel(controlRenderer_, tagBtn, tagFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "tag: " + tagStr + "  [K cycle]", colorFromRgba(kScreenLightColor), tagBtn);
        quickButtons_.push_back({tagBtn, QuickAction::CycleColorTag, "C — cycle cue color tag"});
      }
      // Notes row
      {
        int notesY = ry + kRowStep * 11;
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
        int cnY = ry + kRowStep * 12;
        SDL_Rect label {ctrl.x + 10, cnY, 36, 26};
        SDL_Rect val {ctrl.x + 52, cnY, kCtrlW - 122, 26};
        SDL_Rect editBtn {ctrl.x + kCtrlW - 64, cnY, 54, 26};
        drawText(controlRenderer_, fontSmall_, "#", colorFromRgba(kScreenInkSoftColor), label.x + 4, label.y + 6);
        std::string cnDisplay = selectedCue->cueNumber.empty() ? "--" : selectedCue->cueNumber;
        Primitives::drawFramedPanel(controlRenderer_, val, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, cnDisplay, colorFromRgba(kScreenDeepColor), val.x + 6, val.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, editBtn, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), editBtn);
        quickButtons_.push_back({editBtn, QuickAction::EditCueNumber, "Set short cue label for search/goto"});
      }
      // Pause points row
      {
        int ppY = ry + kRowStep * 13;
        int ppCount = static_cast<int>(selectedCue->pausePoints.size());
        SDL_Rect label {ctrl.x + 10, ppY, 72, 26};
        SDL_Rect addBtn {ctrl.x + 88, ppY, 46, 26};
        SDL_Rect clrBtn {ctrl.x + 140, ppY, 46, 26};
        drawText(controlRenderer_, fontSmall_, "pause pts: " + std::to_string(ppCount),
                 colorFromRgba(kScreenInkSoftColor), label.x + 4, label.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, addBtn, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "+now", colorFromRgba(kScreenLightColor), addBtn);
        Primitives::drawFramedPanel(controlRenderer_, clrBtn, colorFromRgba(kDeleteBezelColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "clr", colorFromRgba(kScreenLightColor), clrBtn);
        quickButtons_.push_back({addBtn, QuickAction::AddPausePoint, "Add pause point at current position"});
        quickButtons_.push_back({clrBtn, QuickAction::ClearPausePoints, "Clear all pause points"});
      }
      {
        int geoY = ry + kRowStep * 14;
        int nextY = drawGeometryRows(geoY, *selectedCue, true);
        drawKeyRows(nextY, *selectedCue);
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
    } else if (selectedCue && (selectedCue->kind == CueKind::Image || selectedCue->kind == CueKind::Pattern)) {
      // Still image / pattern settings — full parity with video panel + duration
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      constexpr int kRowStep = 28;
      // Row 0: duration
      {
        std::string durVal = selectedCue->stillDurationSeconds > 0.0
          ? formatSeconds(selectedCue->stillDurationSeconds) : "hold";
        drawQuickRow(ry, "duration", QuickAction::DurDec, durVal, QuickAction::DurInc,
                     QuickAction::ToggleLoop, false, false, "Auto-advance duration — 0 = hold until taken");
      }
      // Row 1: trans + style button
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
      // Row 2: fade in
      drawQuickRow(ry + kRowStep * 2, "fade in",  QuickAction::FadeInDec,  formatSeconds(selectedCue->fadeInSeconds),
                   QuickAction::FadeInInc, QuickAction::ToggleLoop, false, false, "Fade-in duration for this still");
      // Row 3: fade out
      drawQuickRow(ry + kRowStep * 3, "fade out", QuickAction::FadeOutDec, formatSeconds(selectedCue->fadeOutSeconds),
                   QuickAction::FadeOutInc, QuickAction::ToggleLoop, false, false, "Fade-out duration before next cue");
      // Row 4: loop / hold buttons side by side
      {
        int rx = ctrl.x + 10;
        int ty = ry + kRowStep * 4;
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
        SDL_Rect endBtn {ctrl.x + 10, ry + kRowStep * 5, kCtrlW - 20, 30};
        Primitives::drawFramedPanel(controlRenderer_, endBtn, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, "end: " + cueEndActionLabel(selectedCue->endAction) + "  [X cycle]",
                 colorFromRgba(kScreenDeepColor), endBtn.x + 10, endBtn.y + 8);
        quickButtons_.push_back({endBtn, QuickAction::CycleEndAction, "X — cycle end action"});
      }
      // Row 6: repeats
      {
        std::string loopStr = selectedCue->loopCount == 0 ? "inf" : std::to_string(selectedCue->loopCount) + "x";
        drawQuickRow(ry + kRowStep * 6, "repeats", QuickAction::LoopCountDec, loopStr, QuickAction::LoopCountInc,
                     QuickAction::ToggleLoop, false, false, "Fixed repeat count — 0 = loop forever");
      }
      // Row 7: color tag
      {
        std::string tagStr = selectedCue->colorTag.empty() ? "none" : selectedCue->colorTag;
        SDL_Rect tagBtn {ctrl.x + 10, ry + kRowStep * 7, kCtrlW - 20, 28};
        SDL_Color tagFill = colorTagToSdl(selectedCue->colorTag, 200);
        Primitives::drawFramedPanel(controlRenderer_, tagBtn, tagFill, colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "tag: " + tagStr + "  [K cycle]", colorFromRgba(kScreenLightColor), tagBtn);
        quickButtons_.push_back({tagBtn, QuickAction::CycleColorTag, "K — cycle cue color tag"});
      }
      // Notes row
      {
        int notesY = ry + kRowStep * 8;
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
        int rowY = ry + kRowStep * 9;
        rowY = drawGeometryRows(rowY, *selectedCue, true);
        rowY = drawKeyRows(rowY, *selectedCue);
        int cnY = rowY + 2;
        SDL_Rect label {ctrl.x + 10, cnY, 36, 26};
        SDL_Rect val {ctrl.x + 52, cnY, kCtrlW - 122, 26};
        SDL_Rect editBtn {ctrl.x + kCtrlW - 64, cnY, 54, 26};
        drawText(controlRenderer_, fontSmall_, "#", colorFromRgba(kScreenInkSoftColor), label.x + 4, label.y + 6);
        std::string cnDisplay = selectedCue->cueNumber.empty() ? "--" : selectedCue->cueNumber;
        Primitives::drawFramedPanel(controlRenderer_, val, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawText(controlRenderer_, fontSmall_, cnDisplay, colorFromRgba(kScreenDeepColor), val.x + 6, val.y + 6);
        Primitives::drawFramedPanel(controlRenderer_, editBtn, colorFromRgba(kScreenDarkColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));
        drawCenteredText(controlRenderer_, fontSmall_, "edit", colorFromRgba(kScreenLightColor), editBtn);
        quickButtons_.push_back({editBtn, QuickAction::EditCueNumber, "Set short cue label for search/goto"});
      }
    } else if (selectedCue && selectedCue->kind == CueKind::Browser) {
      int ry = ctrlSettingsY + 18 - cueSettingsScroll_;
      constexpr int kRowStep = 28;
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
        rowY = drawGeometryRows(rowY, *selectedCue, true);
        rowY = drawKeyRows(rowY, *selectedCue);
        int cnY = rowY + 2;
        SDL_Rect label {ctrl.x + 10, cnY, 36, 26};
        SDL_Rect val {ctrl.x + 52, cnY, kCtrlW - 122, 26};
        SDL_Rect editBtn {ctrl.x + kCtrlW - 64, cnY, 54, 26};
        drawText(controlRenderer_, fontSmall_, "#", colorFromRgba(kScreenInkSoftColor), label.x + 4, label.y + 6);
        std::string cnDisplay = selectedCue->cueNumber.empty() ? "--" : selectedCue->cueNumber;
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
        std::string cnDisplay = selectedCue->cueNumber.empty() ? "--" : selectedCue->cueNumber;
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

  void presentDeckCompositorToWindow(int deckIndex, int windowW, int windowH) {
    DeckRuntime* runtime = runtimeForDeck(deckIndex);
    if (!runtime || !runtime->outputRenderer || !runtime->compositorTexture) {
      return;
    }
    const Deck& deck = project_.decks[deckIndex];
    int texW = runtime->compositorWidth;
    int texH = runtime->compositorHeight;
    if (texW <= 0 || texH <= 0 || windowW <= 0 || windowH <= 0) {
      return;
    }

    SDL_Rect src {0, 0, std::min(windowW, texW), std::min(windowH, texH)};
    if (project_.outputCanvasEnabled) {
      src.x = std::clamp(deck.canvasViewX, 0, std::max(0, texW - src.w));
      src.y = std::clamp(deck.canvasViewY, 0, std::max(0, texH - src.h));
    }

    bool hasBlend = deck.edgeBlendLeft > 0.0001f || deck.edgeBlendRight > 0.0001f
      || deck.edgeBlendTop > 0.0001f || deck.edgeBlendBottom > 0.0001f;
    bool hasWarp = deck.warpEnabled;

#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (hasWarp || hasBlend) {
      float u0 = static_cast<float>(src.x) / static_cast<float>(texW);
      float v0 = static_cast<float>(src.y) / static_cast<float>(texH);
      float u1 = static_cast<float>(src.x + src.w) / static_cast<float>(texW);
      float v1 = static_cast<float>(src.y + src.h) / static_cast<float>(texH);

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

      SDL_Vertex verts[4] {
        {p0, SDL_Color {255, 255, 255, edgeBlendAlphaForUv(deck, 0.0f, 0.0f)}, SDL_FPoint {u0, v0}},
        {p1, SDL_Color {255, 255, 255, edgeBlendAlphaForUv(deck, 1.0f, 0.0f)}, SDL_FPoint {u1, v0}},
        {p2, SDL_Color {255, 255, 255, edgeBlendAlphaForUv(deck, 1.0f, 1.0f)}, SDL_FPoint {u1, v1}},
        {p3, SDL_Color {255, 255, 255, edgeBlendAlphaForUv(deck, 0.0f, 1.0f)}, SDL_FPoint {u0, v1}},
      };
      const int indices[6] {0, 1, 2, 0, 2, 3};
      SDL_SetTextureBlendMode(runtime->compositorTexture, SDL_BLENDMODE_BLEND);
      if (SDL_RenderGeometry(runtime->outputRenderer, runtime->compositorTexture, verts, 4, indices, 6) == 0) {
        return;
      }
    }
#endif

    SDL_RenderCopy(runtime->outputRenderer, runtime->compositorTexture, &src, nullptr);
  }

  SDL_Texture* ensureLayerBridgeTexture(DeckRuntime& outputRuntime, int sourceDeckIndex, int width, int height) {
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

  static void applyCueChromaKeyToPixels(std::vector<std::uint8_t>& pixels, const Cue& cue) {
    if (!cue.chromaKeyEnabled || pixels.empty()) {
      return;
    }
    float tolerance = std::clamp(cue.chromaKeyTolerance, 0.0f, 441.0f);
    float softness = std::clamp(cue.chromaKeySoftness, 0.0f, 200.0f);
    float inner = std::max(0.0f, tolerance - softness);
    float outer = tolerance + softness;
    float span = std::max(0.0001f, outer - inner);
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
      float dr = static_cast<float>(pixels[i + 0]) - static_cast<float>(cue.chromaKeyColor.r);
      float dg = static_cast<float>(pixels[i + 1]) - static_cast<float>(cue.chromaKeyColor.g);
      float db = static_cast<float>(pixels[i + 2]) - static_cast<float>(cue.chromaKeyColor.b);
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
    double scale = std::min(
      static_cast<double>(target.w) / static_cast<double>(srcW),
      static_cast<double>(target.h) / static_cast<double>(srcH)
    );
    float outputScaleX = cue ? cue->outputScaleX : 1.0f;
    float outputScaleY = cue ? cue->outputScaleY : 1.0f;
    float outputScale = std::max(outputScaleX, outputScaleY);  // Use max for thumbnail to show full size
    float offsetX = cue ? cue->outputOffsetX : 0.0f;
    float offsetY = cue ? cue->outputOffsetY : 0.0f;
    float rotationDegrees = cue ? cue->outputRotationDegrees : 0.0f;
    int drawW = std::max(1, static_cast<int>(std::round(srcW * scale * outputScale)));
    int drawH = std::max(1, static_cast<int>(std::round(srcH * scale * outputScale)));
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

  void renderDeckLayerIntoOutput(int outputDeckIndex, int sourceDeckIndex, const SDL_Rect& target) {
    DeckRuntime* outputRuntime = runtimeForDeck(outputDeckIndex);
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
    if (sourceDeckIndex == outputDeckIndex) {
      if (outputRuntime->mediaEngine) {
        outputRuntime->mediaEngine->render(target);
      }
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
    if (sourceCue->chromaKeyEnabled) {
      outputRuntime->layerBridgeScratchPixels = sourceFrame->pixels;
      applyCueChromaKeyToPixels(outputRuntime->layerBridgeScratchPixels, *sourceCue);
      uploadPixels = outputRuntime->layerBridgeScratchPixels.data();
    }
    SDL_UpdateTexture(bridgeTexture, nullptr, uploadPixels, sourceFrame->width * 4);
    renderTextureWithCueGeometry(outputRuntime->outputRenderer, bridgeTexture, sourceFrame->width, sourceFrame->height, sourceCue, target);
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
    width = std::max(1, width);
    height = std::max(1, height);
    if (project_.outputCanvasEnabled) {
      clampDeckCanvasViewToWindow(deckIndex, width, height);
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
      configureDeckCompositor(deckIndex, targetCompositorW, targetCompositorH);
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
    auto outputLayers = layeredDeckIndicesForOutputHost(deckIndex);
    if (outputLayers.empty()) {
      outputLayers.push_back(deckIndex);
    }
    for (int sourceDeckIndex : outputLayers) {
      renderDeckLayerIntoOutput(deckIndex, sourceDeckIndex, bounds);
    }

    // Output window is always clean black — no status overlays or decorations.
    // The only things drawn here are the media content itself, cue overlays, and
    // the optional time/ID overlay that the operator explicitly enables.
    const Cue* activeCue = activeCuePtr(deckIndex);

    // Audio-only cue: draw a centred waveform + info on the output window
    if (activeCue && activeCue->kind == CueKind::Audio) {
      int margin = renderW / 10;
      SDL_Rect wfRect {margin, renderH / 4, renderW - margin * 2, renderH / 3};
      std::vector<float> peaks;
      { std::lock_guard<std::mutex> lk(waveformMutex_);
        auto it = waveformCache_.find(activeCue->path);
        if (it != waveformCache_.end()) peaks = it->second; }
      double dur = activeCue->duration > 0.0 ? activeCue->duration : 1.0;
      const MediaEngine* eng = runtime->mediaEngine.get();
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
    for (int ovIdx : deck.overlayActiveIndices) {
      if (ovIdx < 0 || ovIdx >= static_cast<int>(deck.cues.size())) continue;
      const Cue& lc = deck.cues[ovIdx];
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

    if (deck.timeOverlayEnabled) {
      const MediaEngine* engine = mediaEngineForDeck(deckIndex);
      std::string position = formatSeconds(engine ? engine->position() : 0.0);
      std::string total = formatSeconds(engine ? engine->duration() : 0.0);
      std::string timeLine = position + " / " + total;
      std::string cueIdLine = activeCue ? ("id: " + activeCue->id) : "id: --";
      std::string tcLine = "tc: " + formatTimecode(deck.timecodeCurrentSeconds, deck.timecodeFps);
      SDL_Rect overlay {26, 26, std::max(300, renderW / 3), 72};
      Primitives::drawFramedPanel(runtime->outputRenderer, overlay, {15, 56, 15, 204}, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenMidColor));
      drawText(runtime->outputRenderer, fontMono_, timeLine, colorFromRgba(kScreenLightColor), overlay.x + 14, overlay.y + 9);
      drawText(runtime->outputRenderer, fontSmall_, cueIdLine, colorFromRgba(kScreenMidColor), overlay.x + 14, overlay.y + 34);
      drawText(runtime->outputRenderer, fontSmall_, tcLine, colorFromRgba(kScreenMidColor), overlay.x + 14, overlay.y + 50);
    }

    // Master video dimmer overlay
    if (project_.masterDimmer < 0.999) {
      SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0,
        static_cast<Uint8>((1.0 - project_.masterDimmer) * 255.0));
      SDL_RenderFillRect(runtime->outputRenderer, nullptr);
      SDL_SetRenderDrawBlendMode(runtime->outputRenderer, SDL_BLENDMODE_NONE);
    }
    if (usingCompositor) {
      SDL_SetRenderTarget(runtime->outputRenderer, nullptr);
      SDL_SetRenderDrawColor(runtime->outputRenderer, 0, 0, 0, 255);
      SDL_RenderClear(runtime->outputRenderer);
      presentDeckCompositorToWindow(deckIndex, width, height);
    }
    double fpsHint = 30.0;
    for (auto it = outputLayers.rbegin(); it != outputLayers.rend(); ++it) {
      const Cue* layerCue = activeCuePtr(*it);
      if (layerCue && layerCue->kind == CueKind::Video) {
        fpsHint = std::max(1.0, layerCue->fps);
        break;
      }
    }
    sendDeckNdiFrame(deckIndex, width, height, fpsHint);
    SDL_RenderPresent(runtime->outputRenderer);
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
    constexpr int kItemH = 26;
    constexpr int kMenuW = 160;
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
        SDL_Rect sw {item.rect.x, item.rect.y + 4, 10, item.rect.h - 8};
        Primitives::fillRect(controlRenderer_, sw, item.swatch);
      }
      drawText(controlRenderer_, fontSmall_, item.label,
               hover ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor),
               item.rect.x + 14, item.rect.y + 6);
    }
    SDL_SetRenderDrawBlendMode(controlRenderer_, SDL_BLENDMODE_NONE);
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
    constexpr int kModalW = 640, kModalH = 440;
    SDL_Rect modal {(width - kModalW) / 2, (height - kModalH) / 2, kModalW, kModalH};
    Primitives::drawFramedPanel(controlRenderer_, modal, colorFromRgba(kShellInnerColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kShellOuterColor));

    // Title
    drawText(controlRenderer_, fontBase_, "Preferences", colorFromRgba(kScreenDeepColor), modal.x + 16, modal.y + 10);

    // Close button [X]
    settingsCloseBtn_ = {modal.x + modal.w - 36, modal.y + 6, 28, 26};
    Primitives::drawFramedPanel(controlRenderer_, settingsCloseBtn_, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
    drawCenteredText(controlRenderer_, fontSmall_, "X", colorFromRgba(kScreenDeepColor), settingsCloseBtn_);

    // Tab bar
    constexpr int kTabW = 100, kTabH = 30;
    int tabY = modal.y + 40;
    settingsBtns_.clear();
    const std::vector<std::string> tabs {"Audio", "MIDI", "OSC/Net", "Video", "About"};
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
    SDL_Rect content {modal.x + 16, tabY + kTabH + 10, modal.w - 32, modal.h - kTabH - 70};
    Primitives::drawFramedPanel(controlRenderer_, content, colorFromRgba(kScreenLightColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenMidColor));

    int cx = content.x + 12, cy = content.y + 10;
    SDL_Color ink = colorFromRgba(kScreenDeepColor);
    SDL_Color soft = colorFromRgba(kScreenInkSoftColor);

    if (settingsTab_ == 0) {
      // Audio tab
      drawText(controlRenderer_, fontSmall_, "Master volume: set with the header fader (vol XX%)", soft, cx, cy);
      drawText(controlRenderer_, fontSmall_, "Audio output device:", ink, cx, cy + 22);
      std::string devName = focusedDeck().audioOutputDeviceName.empty() ? "(default)" : focusedDeck().audioOutputDeviceName;
      drawText(controlRenderer_, fontSmall_, devName, soft, cx, cy + 38);
      SDL_Rect devBtn {cx, cy + 60, 180, 26};
      Primitives::drawFramedPanel(controlRenderer_, devBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Change device...", ink, devBtn);
      settingsBtns_.push_back({devBtn, 200, "audio_device"});

      drawText(controlRenderer_, fontSmall_, "UI sounds (key 1):", ink, cx, cy + 100);
      SDL_Rect sfxBtn {cx, cy + 118, 80, 24};
      Primitives::drawFramedPanel(controlRenderer_, sfxBtn, project_.uiSoundsEnabled ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, project_.uiSoundsEnabled ? "ON" : "OFF",
                       project_.uiSoundsEnabled ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor), sfxBtn);
      settingsBtns_.push_back({sfxBtn, 201, "sfx_toggle"});

      drawText(controlRenderer_, fontSmall_, "UI animations (key 2):", ink, cx, cy + 155);
      SDL_Rect animBtn {cx, cy + 173, 80, 24};
      Primitives::drawFramedPanel(controlRenderer_, animBtn, project_.uiTransitionsEnabled ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, project_.uiTransitionsEnabled ? "ON" : "OFF",
                       project_.uiTransitionsEnabled ? colorFromRgba(kScreenLightColor) : colorFromRgba(kScreenDeepColor), animBtn);
      settingsBtns_.push_back({animBtn, 202, "anim_toggle"});

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
      drawText(controlRenderer_, fontSmall_, "MSC Go -> Trigger cue by cue number", soft, cx, cy + 202);

    } else if (settingsTab_ == 2) {
      // OSC/Net tab
      drawText(controlRenderer_, fontSmall_, "Companion / OSC port:", ink, cx, cy);
      drawText(controlRenderer_, fontSmall_, std::to_string(companionPort_), soft, cx, cy + 18);
      SDL_Rect portBtn {cx, cy + 40, 160, 26};
      Primitives::drawFramedPanel(controlRenderer_, portBtn, colorFromRgba(kScreenMidColor), colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Change port...", ink, portBtn);
      settingsBtns_.push_back({portBtn, 220, "osc_port"});

      drawText(controlRenderer_, fontSmall_, "HyperDeck emulation: port 9992 (always on)", soft, cx, cy + 80);
      drawText(controlRenderer_, fontSmall_, "OSC subscribe: send /playboy/subscribe from your OSC app", soft, cx, cy + 100);
      drawText(controlRenderer_, fontSmall_, "NDI: configured per deck (N key)", soft, cx, cy + 120);

    } else if (settingsTab_ == 3) {
      // Video tab
      auto [nativeW, nativeH] = displayNativeRenderSize(focusedDeck().outputDisplayIndex);
      auto [targetW, targetH] = outputRenderSizeForDeck(project_.focusedDeckIndex);

      drawText(controlRenderer_, fontSmall_, "Focused deck display: " + currentDisplayLabel(), ink, cx, cy);
      drawText(controlRenderer_, fontSmall_,
               "Desktop mode (EDID/OS): " + std::to_string(nativeW) + "x" + std::to_string(nativeH),
               soft, cx, cy + 18);
      drawText(controlRenderer_, fontSmall_,
               "Output raster: " + std::to_string(targetW) + "x" + std::to_string(targetH) + "  (" + outputSizingModeLabel() + ")",
               soft, cx, cy + 36);
      drawText(controlRenderer_, fontSmall_,
               "Refresh target: " + outputRefreshRateLabel(),
               soft, cx, cy + 54);
      drawText(controlRenderer_, fontSmall_,
               "Output depth: " + outputBitDepthModeLabel() + " (active " + outputBitDepthActiveLabel(project_.focusedDeckIndex) + ")",
               soft, cx, cy + 72);

      SDL_Rect nativeBtn {cx, cy + 82, 170, 28};
      bool nativeActive = project_.outputFollowDisplay;
      Primitives::drawFramedPanel(controlRenderer_, nativeBtn, nativeActive ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Display Native", nativeActive ? colorFromRgba(kScreenLightColor) : ink, nativeBtn);
      settingsBtns_.push_back({nativeBtn, 230, "video_native"});

      SDL_Rect sizeBtn {cx + 186, cy + 82, 160, 28};
      Primitives::drawFramedPanel(controlRenderer_, sizeBtn, colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Size To Display", ink, sizeBtn);
      settingsBtns_.push_back({sizeBtn, 235, "video_size_display"});

      SDL_Rect fsBtn {cx + 362, cy + 82, 150, 28};
      Primitives::drawFramedPanel(controlRenderer_, fsBtn, colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Toggle Fullscreen", ink, fsBtn);
      settingsBtns_.push_back({fsBtn, 236, "video_fullscreen"});

      SDL_Rect rateAutoBtn {cx, cy + 118, 90, 26};
      Primitives::drawFramedPanel(controlRenderer_, rateAutoBtn,
                      project_.outputRefreshRateHz <= 0.0 ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Hz Auto",
                       project_.outputRefreshRateHz <= 0.0 ? colorFromRgba(kScreenLightColor) : ink, rateAutoBtn);
      settingsBtns_.push_back({rateAutoBtn, 238, "video_rate_auto"});

      SDL_Rect ratePrevBtn {cx + 104, cy + 118, 64, 26};
      Primitives::drawFramedPanel(controlRenderer_, ratePrevBtn, colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Hz -", ink, ratePrevBtn);
      settingsBtns_.push_back({ratePrevBtn, 239, "video_rate_prev"});

      SDL_Rect rateNextBtn {cx + 178, cy + 118, 64, 26};
      Primitives::drawFramedPanel(controlRenderer_, rateNextBtn, colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Hz +", ink, rateNextBtn);
      settingsBtns_.push_back({rateNextBtn, 240, "video_rate_next"});

      SDL_Rect rateCustomBtn {cx + 254, cy + 118, 180, 26};
      Primitives::drawFramedPanel(controlRenderer_, rateCustomBtn, colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Set Hz...", ink, rateCustomBtn);
      settingsBtns_.push_back({rateCustomBtn, 241, "video_rate_custom"});

      drawText(controlRenderer_, fontSmall_, "Fixed raster presets:", ink, cx, cy + 154);
      auto drawPreset = [&](int x, int y, int w, int h, const std::string& label, int action,
                            int presetW, int presetH) {
        bool active = !project_.outputFollowDisplay
          && project_.outputRenderWidth == presetW
          && project_.outputRenderHeight == presetH;
        SDL_Rect btn {x, y, w, h};
        Primitives::drawFramedPanel(controlRenderer_, btn, active ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                        colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
        drawCenteredText(controlRenderer_, fontSmall_, label, active ? colorFromRgba(kScreenLightColor) : ink, btn);
        settingsBtns_.push_back({btn, action, label});
      };
      int py = cy + 172;
      drawPreset(cx,      py, 120, 28, "720p",   231, 1280, 720);
      drawPreset(cx + 132,py, 120, 28, "1080p",  232, 1920, 1080);
      drawPreset(cx + 264,py, 120, 28, "1440p",  233, 2560, 1440);
      drawPreset(cx + 396,py, 120, 28, "4K UHD", 234, 3840, 2160);

      SDL_Rect customBtn {cx, cy + 204, 180, 28};
      Primitives::drawFramedPanel(controlRenderer_, customBtn, colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Custom WxH...", ink, customBtn);
      settingsBtns_.push_back({customBtn, 237, "video_custom"});

      SDL_Rect depthAutoBtn {cx, cy + 242, 90, 26};
      Primitives::drawFramedPanel(controlRenderer_, depthAutoBtn,
                      project_.outputBitDepth == 0 ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Depth Auto",
                       project_.outputBitDepth == 0 ? colorFromRgba(kScreenLightColor) : ink, depthAutoBtn);
      settingsBtns_.push_back({depthAutoBtn, 242, "video_depth_auto"});

      SDL_Rect depth8Btn {cx + 104, cy + 242, 90, 26};
      Primitives::drawFramedPanel(controlRenderer_, depth8Btn,
                      project_.outputBitDepth == 8 ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Depth 8-bit",
                       project_.outputBitDepth == 8 ? colorFromRgba(kScreenLightColor) : ink, depth8Btn);
      settingsBtns_.push_back({depth8Btn, 243, "video_depth_8"});

      SDL_Rect depth10Btn {cx + 208, cy + 242, 100, 26};
      Primitives::drawFramedPanel(controlRenderer_, depth10Btn,
                      project_.outputBitDepth == 10 ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Depth 10-bit",
                       project_.outputBitDepth == 10 ? colorFromRgba(kScreenLightColor) : ink, depth10Btn);
      settingsBtns_.push_back({depth10Btn, 244, "video_depth_10"});

      const Deck& focused = focusedDeck();
      std::string canvasLabel = project_.outputCanvasEnabled
        ? (std::to_string(project_.outputCanvasWidth) + "x" + std::to_string(project_.outputCanvasHeight))
        : "off";
      drawText(controlRenderer_, fontSmall_,
               "Canvas span: " + canvasLabel + "  view " + std::to_string(focused.canvasViewX) + "," + std::to_string(focused.canvasViewY),
               soft, cx, cy + 274);

      SDL_Rect canvasOffBtn {cx, cy + 292, 88, 26};
      Primitives::drawFramedPanel(controlRenderer_, canvasOffBtn,
                      !project_.outputCanvasEnabled ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Canvas Off",
                       !project_.outputCanvasEnabled ? colorFromRgba(kScreenLightColor) : ink, canvasOffBtn);
      settingsBtns_.push_back({canvasOffBtn, 245, "video_canvas_off"});

      SDL_Rect canvasOnBtn {cx + 98, cy + 292, 88, 26};
      Primitives::drawFramedPanel(controlRenderer_, canvasOnBtn,
                      project_.outputCanvasEnabled ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Canvas On",
                       project_.outputCanvasEnabled ? colorFromRgba(kScreenLightColor) : ink, canvasOnBtn);
      settingsBtns_.push_back({canvasOnBtn, 246, "video_canvas_on"});

      SDL_Rect canvasSizeBtn {cx + 196, cy + 292, 150, 26};
      Primitives::drawFramedPanel(controlRenderer_, canvasSizeBtn, colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "Canvas WxH...", ink, canvasSizeBtn);
      settingsBtns_.push_back({canvasSizeBtn, 247, "video_canvas_custom"});

      SDL_Rect canvasViewBtn {cx + 356, cy + 292, 120, 26};
      Primitives::drawFramedPanel(controlRenderer_, canvasViewBtn, colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, "View XY...", ink, canvasViewBtn);
      settingsBtns_.push_back({canvasViewBtn, 248, "video_canvas_view"});

      SDL_Rect warpBtn {cx + 486, cy + 292, 90, 26};
      Primitives::drawFramedPanel(controlRenderer_, warpBtn,
                      focused.warpEnabled ? colorFromRgba(kScreenDarkColor) : colorFromRgba(kScreenMidColor),
                      colorFromRgba(kScreenDeepColor), colorFromRgba(kScreenLightColor));
      drawCenteredText(controlRenderer_, fontSmall_, focused.warpEnabled ? "Warp On" : "Warp Off",
                       focused.warpEnabled ? colorFromRgba(kScreenLightColor) : ink, warpBtn);
      settingsBtns_.push_back({warpBtn, 249, "video_warp_toggle"});

    } else if (settingsTab_ == 4) {
      // About tab
      drawText(controlRenderer_, fontSmall_, "Playboy  v0.01", ink, cx, cy);
      drawText(controlRenderer_, fontSmall_, "dot-matrix show control", soft, cx, cy + 18);
      drawText(controlRenderer_, fontSmall_, "Companion port: " + std::to_string(companionPort_), soft, cx, cy + 40);
      drawText(controlRenderer_, fontSmall_, "HyperDeck port: 9992", soft, cx, cy + 56);
      drawText(controlRenderer_, fontSmall_, "Keyboard shortcuts:", ink, cx, cy + 80);
      const char* shortcuts[] = {
        "Enter = Take/Load   Space = Go/Pause   S = Stop",
        "L = Loop   E = Hold   X = Cycle end action   K = Color tag",
        "F = Fullscreen   D = Next display   G = Add graphic cue",
        "Ctrl+G = Goto cue   Ctrl+Shift+Space = All stop",
        "[ / ] = fade in   Shift+[ / ] = fade out",
        "-30s/-20s/-10s buttons = jump to end   N = edit notes",
        "1/2/3/4/5/6 = SFX/Anim/AutoNext/Loop/TC/Shuffle toggles",
      };
      for (int i = 0; i < 7; ++i)
        drawText(controlRenderer_, fontSmall_, shortcuts[i], soft, cx, cy + 100 + i * 16);
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
          try { int p = std::stoi(*portStr); if (p > 0 && p < 65536) companionPort_ = p; } catch (...) {}
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
      }
      return;
    }
    // Click outside modal = close
    constexpr int kModalW = 640, kModalH = 440;
    int width = 0, height = 0;
    SDL_GetWindowSize(controlWindow_, &width, &height);
    SDL_Rect modal {(width - kModalW) / 2, (height - kModalH) / 2, kModalW, kModalH};
    if (!pointInRect(mx, my, modal)) settingsOpen_ = false;
  }

  void handleMouseDown(int x, int y) {
    if (showStartupDialog_) {
      if (pointInRect(x, y, startupLoadBtn_)) {
        // Keep loaded project as-is
        showStartupDialog_ = false;
      } else if (pointInRect(x, y, startupNewBtn_)) {
        // Start fresh
        project_ = Project{};
        normalizeProject(project_);
        rebuildDeckRuntimes();
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

    // Deck column headers: click to focus deck
    for (int di = 0; di < static_cast<int>(deckColumnRects_.size()); ++di) {
      const SDL_Rect& col = deckColumnRects_[di];
      SDL_Rect colHeader {col.x, col.y, col.w, kColHeaderH};
      if (pointInRect(x, y, colHeader)) {
        setFocusedDeckIndex(di);
        return;
      }
    }

    // Deck column cue lists: click to focus deck + select cue
    for (int di = 0; di < static_cast<int>(deckListClipRects_.size()); ++di) {
      const SDL_Rect& clipFrame = deckListClipRects_[di];
      SDL_Rect clipRect {clipFrame.x + 8, clipFrame.y + 8, clipFrame.w - 16, clipFrame.h - 16};
      if (!pointInRect(x, y, clipRect)) {
        continue;
      }
      int listY = clipRect.y - deckScrolls_[di];
      for (int ci = 0; ci < static_cast<int>(project_.decks[di].cues.size()); ++ci) {
        SDL_Rect row {clipRect.x, listY, clipRect.w, kRowHeight};
        if (pointInRect(x, y, row)) {
          setFocusedDeckIndex(di);
          Deck& deck = project_.decks[di];
          if (deck.selectedIndex != ci) {
            deck.selectedIndex = ci;
            onSelectionChanged();
            markProjectDirty();
          }
          drag_.active = true;
          drag_.cueIndex = ci;
          drag_.deckIndex = di;
          return;
        }
        listY += kRowHeight + 8;
      }
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

  void handleKeyDown(SDL_Keycode key, Uint16 mod) {
    bool ctrl = (mod & KMOD_CTRL) != 0;
    bool shift = (mod & KMOD_SHIFT) != 0;

    if (showStartupDialog_) {
      bool hasSavedFile = !currentProjectFile_.empty() && fs::exists(currentProjectFile_);
      if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && hasSavedFile) {
        showStartupDialog_ = false;
      } else if (key == SDLK_n) {
        project_ = Project{};
        normalizeProject(project_);
        rebuildDeckRuntimes();
        showStartupDialog_ = false;
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

    if (ctrl && key == SDLK_o) {
      openProjectFromPicker();
      return;
    }
    if (ctrl && key == SDLK_n) {
      addDeck();
      return;
    }
    if (ctrl && !shift && key == SDLK_s) {
      markProjectDirty();
      triggerToast("playlist saved");
      return;
    }
    if (ctrl && shift && key == SDLK_s) {
      saveProjectAsFromPicker();
      return;
    }

    switch (key) {
      case SDLK_ESCAPE:
        confirmQuit_ = true;
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
        takeSelected(true);
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
        toggleFocusedDeckNdi();
        break;
      case SDLK_o:
        toggleTimeOverlayEnabled();
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
    markProjectDirty();
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
    // Use per-cue transition override if set, else deck default
    double transSecs = (cue.cueTransitionSeconds >= 0.0)
      ? cue.cueTransitionSeconds : deck.transitionSeconds;
    std::string transStyleStr = !cue.cueTransitionStyle.empty()
      ? cue.cueTransitionStyle : deck.transitionStyle;
    engine->loadCue(
      &cue,
      autoplay,
      transSecs,
      parseTransitionStyleToken(transStyleStr)
    );
    if (cue.kind == CueKind::Browser) {
      startBrowserCue(project_.focusedDeckIndex, cue);
      triggerToast("browser jumped live");
    } else {
      triggerToast(autoplay ? "cue jumped live" : "cue loaded");
    }
    playUiSound(UiSoundEffect::Take);
    markProjectDirty();
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

    // Exact match on user-assigned cue number
    std::string needle = toUpper(trimmed);
    for (int index = 0; index < static_cast<int>(deck.cues.size()); ++index) {
      if (!deck.cues[index].cueNumber.empty() && toUpper(deck.cues[index].cueNumber) == needle) {
        return index;
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

  bool selectCueById(const std::string& cueId) {
    Deck& deck = focusedDeckMutable();
    auto index = cueIndexById(deck, cueId);
    if (!index) {
      return false;
    }
    if (deck.selectedIndex != *index) {
      deck.selectedIndex = *index;
      onSelectionChanged();
    }
    triggerToast("cue " + std::to_string(*index + 1) + " armed");
    markProjectDirty();
    return true;
  }

  bool takeCueById(const std::string& cueId, bool autoplay) {
    Deck& deck = focusedDeckMutable();
    auto index = cueIndexById(deck, cueId);
    if (!index) {
      return false;
    }
    deck.selectedIndex = *index;
    onSelectionChanged();
    takeSelected(autoplay);
    return true;
  }

  void setSelectedTrimIn(double seconds) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video) {
      return;
    }
    double duration = std::max(0.0, cue->duration);
    double next = std::clamp(seconds, 0.0, duration);
    double out = cue->outPointSeconds > 0.0 ? cue->outPointSeconds : duration;
    out = std::clamp(out, next, duration);
    cue->inPointSeconds = next;
    cue->outPointSeconds = out;
    triggerToast("in " + formatSeconds(cue->inPointSeconds));
    markProjectDirty();
  }

  void setSelectedTrimOut(double seconds) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video) {
      return;
    }
    double duration = std::max(0.0, cue->duration);
    double next = std::clamp(seconds, cue->inPointSeconds, duration);
    cue->outPointSeconds = next;
    triggerToast("out " + formatSeconds(cue->outPointSeconds));
    markProjectDirty();
  }

  void clearSelectedTrim() {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video) {
      return;
    }
    cue->inPointSeconds = 0.0;
    cue->outPointSeconds = cue->duration;
    triggerToast("trim reset");
    markProjectDirty();
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
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return false;
    }
    cue->triggerTimecodeSeconds = std::max(0.0, seconds);
    triggerToast("tc mark " + formatTimecode(cue->triggerTimecodeSeconds, focusedDeck().timecodeFps));
    markProjectDirty();
    return true;
  }

  void clearSelectedCueTimecodeTrigger() {
    Cue* cue = selectedCueMutable();
    if (!cue) {
      return;
    }
    cue->triggerTimecodeSeconds = -1.0;
    triggerToast("tc mark cleared");
    markProjectDirty();
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

  void setDeckTimecode(int deckIndex, double seconds) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return;
    }
    Deck& deck = project_.decks[deckIndex];
    double normalized = std::max(0.0, std::isfinite(seconds) ? seconds : 0.0);
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

  void setFocusedDeckTimecode(double seconds) {
    setDeckTimecode(project_.focusedDeckIndex, seconds);
    triggerToast("tc " + formatTimecode(focusedDeck().timecodeCurrentSeconds, focusedDeck().timecodeFps));
  }

  void cycleSelectedEndAction() {
    Cue* cue = selectedCueMutable();
    if (!cue || (cue->kind != CueKind::Video && cue->kind != CueKind::Audio)) {
      return;
    }
    // Cycle: Inherit → Stop → Loop → PauseOnLast → AutoNext → Inherit
    switch (cue->endAction) {
      case CueEndAction::Inherit:     cue->endAction = CueEndAction::Stop;       break;
      case CueEndAction::Stop:        cue->endAction = CueEndAction::Loop;       break;
      case CueEndAction::Loop:        cue->endAction = CueEndAction::PauseOnLast; break;
      case CueEndAction::PauseOnLast: cue->endAction = CueEndAction::AutoNext;   break;
      case CueEndAction::AutoNext:    cue->endAction = CueEndAction::Inherit;    break;
    }
    triggerToast("end: " + cueEndActionLabel(cue->endAction));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedEndAction(CueEndAction action) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video || cue->endAction == action) {
      return;
    }
    cue->endAction = action;
    triggerToast("end: " + cueEndActionLabel(cue->endAction));
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  bool cueSupportsGeometry(const Cue* cue) const {
    return cue && (cue->kind == CueKind::Video
      || cue->kind == CueKind::Image
      || cue->kind == CueKind::Pattern
      || cue->kind == CueKind::Browser);
  }

  bool cueSupportsKeying(const Cue* cue) const {
    return cue && (cue->kind == CueKind::Video
      || cue->kind == CueKind::Image
      || cue->kind == CueKind::Pattern
      || cue->kind == CueKind::Browser);
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
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    cue->outputRotationDegrees = std::clamp(cue->outputRotationDegrees + deltaDegrees, -180.0f, 180.0f);
    markProjectDirty();
  }

  void adjustSelectedCrop(char edge, float delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsGeometry(cue)) {
      return;
    }
    switch (edge) {
      case 'L': cue->cropLeft += delta; break;
      case 'R': cue->cropRight += delta; break;
      case 'T': cue->cropTop += delta; break;
      case 'B': cue->cropBottom += delta; break;
      default: return;
    }
    normalizeCueCrop(*cue);
    markProjectDirty();
  }

  void setSelectedChromaKeyEnabled(bool enabled) {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsKeying(cue) || cue->chromaKeyEnabled == enabled) {
      return;
    }
    cue->chromaKeyEnabled = enabled;
    triggerToast(enabled ? "key on" : "key off");
    markProjectDirty();
  }

  void toggleSelectedChromaKey() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsKeying(cue)) {
      return;
    }
    setSelectedChromaKeyEnabled(!cue->chromaKeyEnabled);
  }

  void adjustSelectedKeyTolerance(float delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsKeying(cue)) {
      return;
    }
    cue->chromaKeyTolerance = std::clamp(cue->chromaKeyTolerance + delta, 0.0f, 441.0f);
    markProjectDirty();
  }

  void adjustSelectedKeySoftness(float delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsKeying(cue)) {
      return;
    }
    cue->chromaKeySoftness = std::clamp(cue->chromaKeySoftness + delta, 0.0f, 200.0f);
    markProjectDirty();
  }

  void editSelectedKeyColor() {
    Cue* cue = selectedCueMutable();
    if (!cue || !cueSupportsKeying(cue)) {
      return;
    }
    auto value = pickTextInput("Key Color", "Hex color (#RRGGBB)", colorToHex(cue->chromaKeyColor));
    if (!value) {
      return;
    }
    cue->chromaKeyColor = parseColor(*value);
    markProjectDirty();
  }

  void dispatchQuickAction(QuickAction action) {
    switch (action) {
      case QuickAction::ToggleLoop:      toggleSelectedLoop(); break;
      case QuickAction::ToggleHold:      toggleSelectedPauseOnLastFrame(); break;
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
      case QuickAction::EditNotes: {
        Cue* sel = selectedCueMutable();
        if (sel) {
          auto result = pickTextInput("Cue Notes", "Enter notes for cue:", sel->notes);
          if (result) {
            sel->notes = *result;
            markProjectDirty();
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
        if (Cue* sel = selectedCueMutable()) { sel->outputScaleX = std::clamp(sel->outputScaleX - 0.05f, 0.25f, 4.0f); markProjectDirty(); }
        break;
      case QuickAction::ScaleXInc:
        if (Cue* sel = selectedCueMutable()) { sel->outputScaleX = std::clamp(sel->outputScaleX + 0.05f, 0.25f, 4.0f); markProjectDirty(); }
        break;
      case QuickAction::ScaleYDec:
        if (Cue* sel = selectedCueMutable()) { sel->outputScaleY = std::clamp(sel->outputScaleY - 0.05f, 0.25f, 4.0f); markProjectDirty(); }
        break;
      case QuickAction::ScaleYInc:
        if (Cue* sel = selectedCueMutable()) { sel->outputScaleY = std::clamp(sel->outputScaleY + 0.05f, 0.25f, 4.0f); markProjectDirty(); }
        break;
      case QuickAction::OffsetXDec:
        if (Cue* sel = selectedCueMutable()) { sel->outputOffsetX -= 10.0f; markProjectDirty(); }
        break;
      case QuickAction::OffsetXInc:
        if (Cue* sel = selectedCueMutable()) { sel->outputOffsetX += 10.0f; markProjectDirty(); }
        break;
      case QuickAction::OffsetYDec:
        if (Cue* sel = selectedCueMutable()) { sel->outputOffsetY -= 10.0f; markProjectDirty(); }
        break;
      case QuickAction::OffsetYInc:
        if (Cue* sel = selectedCueMutable()) { sel->outputOffsetY += 10.0f; markProjectDirty(); }
        break;
      case QuickAction::RotDec:
        adjustSelectedRotation(-1.0f);
        break;
      case QuickAction::RotInc:
        adjustSelectedRotation(1.0f);
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
      case QuickAction::EditCueNumber: {
        Cue* sel = selectedCueMutable();
        if (sel) {
          auto result = pickTextInput("Cue Number", "Short cue label (e.g. Q1, 2A, INTRO):", sel->cueNumber);
          if (result) {
            sel->cueNumber = *result;
            markProjectDirty();
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
    Cue* cue = selectedCueMutable();
    if (!cue || (cue->kind != CueKind::Video && cue->kind != CueKind::Audio)) {
      return;
    }
    cue->loop = !cue->loop;
    triggerToast(cue->loop ? "loop on" : "loop off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedLoop(bool enabled) {
    Cue* cue = selectedCueMutable();
    if (!cue || (cue->kind != CueKind::Video && cue->kind != CueKind::Audio) || cue->loop == enabled) {
      return;
    }
    cue->loop = enabled;
    triggerToast(cue->loop ? "loop on" : "loop off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void toggleSelectedPauseOnLastFrame() {
    Cue* cue = selectedCueMutable();
    if (!cue || (cue->kind != CueKind::Video && cue->kind != CueKind::Audio)) {
      return;
    }
    cue->pauseOnLastFrame = !cue->pauseOnLastFrame;
    triggerToast(cue->pauseOnLastFrame ? "hold on" : "hold off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void setSelectedPauseOnLastFrame(bool enabled) {
    Cue* cue = selectedCueMutable();
    if (!cue || (cue->kind != CueKind::Video && cue->kind != CueKind::Audio) || cue->pauseOnLastFrame == enabled) {
      return;
    }
    cue->pauseOnLastFrame = enabled;
    triggerToast(cue->pauseOnLastFrame ? "hold on" : "hold off");
    playUiSound(UiSoundEffect::Toggle);
    markProjectDirty();
  }

  void adjustSelectedFade(bool fadeIn, double deltaSeconds) {
    Cue* cue = selectedCueMutable();
    if (!cue || (cue->kind != CueKind::Video && cue->kind != CueKind::Audio)) {
      return;
    }
    double& target = fadeIn ? cue->fadeInSeconds : cue->fadeOutSeconds;
    target = std::clamp(target + deltaSeconds, 0.0, 10.0);
    triggerToast(std::string(fadeIn ? "fade in " : "fade out ") + formatSeconds(target));
    markProjectDirty();
  }

  void adjustSelectedIn(double delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video) return;
    cue->inPointSeconds = std::clamp(cue->inPointSeconds + delta, 0.0, cue->duration > 0.0 ? cue->duration : 3600.0);
    triggerToast("in " + formatSeconds(cue->inPointSeconds));
    markProjectDirty();
  }

  void adjustSelectedOut(double delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video) return;
    double cur = cue->outPointSeconds > 0.0 ? cue->outPointSeconds : cue->duration;
    cur = std::clamp(cur + delta, 0.0, cue->duration > 0.0 ? cue->duration : 3600.0);
    cue->outPointSeconds = cur;
    triggerToast("out " + formatSeconds(cue->outPointSeconds));
    markProjectDirty();
  }

  void adjustSelectedCueTransition(double delta) {
    Cue* cue = selectedCueMutable();
    if (!cue) return;
    if (cue->cueTransitionSeconds < 0.0) {
      // First nudge: initialize from deck default
      cue->cueTransitionSeconds = focusedDeck().transitionSeconds;
    }
    cue->cueTransitionSeconds = std::clamp(cue->cueTransitionSeconds + delta, 0.0, 10.0);
    triggerToast("cue trans " + formatSeconds(cue->cueTransitionSeconds));
    markProjectDirty();
  }

  void cycleSelectedCueTransStyle() {
    Cue* cue = selectedCueMutable();
    if (!cue) return;
    static const std::vector<std::string> kStyles = {"cut", "crossfade", "dip"};
    std::string cur = cue->cueTransitionStyle.empty() ? focusedDeck().transitionStyle : cue->cueTransitionStyle;
    auto it = std::find(kStyles.begin(), kStyles.end(), cur);
    if (it == kStyles.end() || std::next(it) == kStyles.end()) {
      cue->cueTransitionStyle = kStyles.front();
    } else {
      cue->cueTransitionStyle = *std::next(it);
    }
    triggerToast("cue style: " + cue->cueTransitionStyle);
    markProjectDirty();
  }

  void adjustSelectedLowerAlpha(int delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::LowerThird) return;
    cue->lowerThirdBgAlpha = std::clamp(cue->lowerThirdBgAlpha + delta, 0, 255);
    triggerToast("overlay alpha " + std::to_string(cue->lowerThirdBgAlpha));
    markProjectDirty();
  }

  void adjustSelectedStillDuration(double delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind == CueKind::Video) return;
    cue->stillDurationSeconds = std::max(0.0, cue->stillDurationSeconds + delta);
    if (cue->stillDurationSeconds < 0.5 && delta < 0) cue->stillDurationSeconds = 0.0;
    triggerToast(cue->stillDurationSeconds > 0.0
      ? "still dur " + formatSeconds(cue->stillDurationSeconds)
      : "still dur: hold");
    markProjectDirty();
  }

  void adjustSelectedLoopCount(int delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video) return;
    cue->loopCount = std::max(0, cue->loopCount + delta);
    triggerToast(cue->loopCount == 0 ? "repeats: inf" : "repeats: " + std::to_string(cue->loopCount) + "x");
    markProjectDirty();
  }

  void adjustSelectedSpeed(double delta) {
    Cue* cue = selectedCueMutable();
    if (!cue || cue->kind != CueKind::Video) return;
    cue->playbackSpeed = std::clamp(std::round((cue->playbackSpeed + delta) * 4.0) / 4.0, 0.25, 4.0);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << cue->playbackSpeed;
    triggerToast("speed: " + ss.str() + "x");
    markProjectDirty();
  }

  void cycleSelectedColorTag() {
    Cue* cue = selectedCueMutable();
    if (!cue) return;
    cue->colorTag = nextColorTag(cue->colorTag);
    triggerToast("tag: " + (cue->colorTag.empty() ? "none" : cue->colorTag));
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
    markProjectDirty();
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
          initialPath
        })
      : readAllText({
          "zenity",
          "--file-selection",
          "--title=Open Playboy playlist",
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
    timecodeTriggeredCueIds_.clear();
    selectionChangedAt_ = SDL_GetTicks64();
    if (!rebuildDeckRuntimes()) {
      std::cerr << "Deck runtime creation failed: " << SDL_GetError() << '\n';
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
    auto [rasterW, rasterH] = outputRenderSizeForDeck(project_.focusedDeckIndex);

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
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("lower third cue added");
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  // Named pattern types and their pretty labels.
  static const std::vector<std::pair<std::string, std::string>>& patternTypes() {
    static const std::vector<std::pair<std::string, std::string>> types {
      {"pocket-test",   "Pocket Test (animated)"},
      {"smpte-bars",    "SMPTE 75% Colour Bars"},
      {"crosshatch",    "Crosshatch"},
      {"checkerboard",  "Checkerboard"},
      {"full-white",    "Full White"},
      {"full-black",    "Full Black"},
      {"full-red",      "Full Red"},
      {"full-green",    "Full Green"},
      {"full-blue",     "Full Blue"},
    };
    return types;
  }

  void addPatternCue(const std::string& typeId) {
    const auto& types = patternTypes();
    std::string label = typeId;
    for (const auto& [id, lbl] : types) {
      if (id == typeId) { label = lbl; break; }
    }
    auto [rasterW, rasterH] = outputRenderSizeForDeck(project_.focusedDeckIndex);
    Cue cue;
    cue.kind = CueKind::Pattern;
    cue.path = "pattern://" + typeId;
    cue.name = label;
    cue.width = rasterW;
    cue.height = rasterH;
    cue.color = {50, 50, 120, 255};
    cue.formatName = "generated";
    Deck& deck = focusedDeckMutable();
    deck.cues.push_back(cue);
    deck.selectedIndex = static_cast<int>(deck.cues.size()) - 1;
    onSelectionChanged();
    triggerToast("pattern: " + label);
    playUiSound(UiSoundEffect::Import);
    markProjectDirty();
  }

  void addKawaiiPatternCue() {
    addPatternCue("pocket-test");
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
    markProjectDirty();
  }

  void toggleOutputFullscreen() {
    DeckRuntime* runtime = focusedRuntime();
    if (!runtime || !runtime->outputWindow) {
      return;
    }
    Uint32 flags = SDL_GetWindowFlags(runtime->outputWindow);
    bool fullscreen = (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
    if (fullscreen) {
      SDL_SetWindowFullscreen(runtime->outputWindow, 0);
      triggerToast("tiny screen");
    } else {
      enableDeckFullscreen(project_.focusedDeckIndex, true);
    }
    playUiSound(UiSoundEffect::Toggle);
  }

  void layoutButtons(int windowHeight) {
    buttons_.clear();
    int x = kPadding + 16;
    int y = windowHeight - 74;
    auto push = [&](std::string label, SDL_Color fill, std::string tip = "") {
      Button button;
      button.label = std::move(label);
      button.tip   = std::move(tip);
      button.rect = {x, y, 118, 44};
      button.fill = fill;
      button.outline = colorFromRgba(kScreenDeepColor);
      button.text = colorFromRgba(kScreenDeepColor);
      buttons_.push_back(button);
      x += button.rect.w + 10;
    };
    push("Import",     colorFromRgba(kScreenMidColor),   "I — import media files (drag-and-drop also works)");
    push("Take",       colorFromRgba(kScreenLightColor),  "Enter — load & play the selected cue");
    push("Go/Pause",   colorFromRgba(kScreenMidColor),   "Space — play, pause, or resume active cue");
    push("Stop",       colorFromRgba(kScreenMidColor),   "S — stop and rewind active cue");
    push("Clear",      colorFromRgba(kScreenMidColor),   "C — cut to black, clear the output");
    push("Fullscreen", colorFromRgba(kScreenMidColor),   "F — toggle output window fullscreen");
    push("Delete",     colorFromRgba(kDeleteBezelColor), "Delete — remove selected cue from playlist");
    push("SFX",    project_.uiSoundsEnabled   ? colorFromRgba(kScreenLightColor) : colorFromRgba(kButtonBezelColor),
         "1 — toggle UI click sounds  |  2 — toggle UI animations");
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
  TTF_Font* fontPixel_ = nullptr;
  SDL_AudioDeviceID uiAudioDevice_ = 0;
  fs::path currentProjectFile_;
  Project project_;
  std::vector<DeckRuntime> deckRuntimes_;
#if defined(PLAYBOY_HAS_NDI_SDK)
  NdiApi ndiApi_;
#endif
  std::vector<Button> buttons_;
  std::vector<SDL_Rect> deckColumnRects_;
  std::vector<SDL_Rect> deckListClipRects_;
  SDL_Rect progressBarRect_ {};
  SDL_Rect mascotRect_ {};

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
  size_t cueSettingsQuickButtonStartIndex_ = 0;
  SDL_Rect cueSettingsViewportRect_ {};
  int cueSettingsScroll_ = 0;
  int cueSettingsScrollMax_ = 0;
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
  SDL_Rect startupLoadBtn_ {};
  SDL_Rect startupNewBtn_ {};
  std::future<std::vector<std::string>> pendingImport_;
  std::future<std::optional<fs::path>> pendingProjectOpen_;
  std::future<std::optional<fs::path>> pendingProjectSaveAs_;
  DragState drag_;
  ToastState toast_;
  Uint64 animationNow_ = 0;
  Uint64 selectionChangedAt_ = 0;
  Uint64 lastUpdateTickMs_ = 0;
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
  std::vector<std::string> statusDeckSnapshots_;
  std::map<int, std::unordered_set<std::string>> timecodeTriggeredCueIds_;
  std::vector<std::int16_t> vuSamples_;
  std::mutex vuSamplesMutex_;
  // Waveform analysis cache (path → peaks vector)
  std::map<std::string, std::vector<float>> waveformCache_;
  std::map<std::string, std::future<std::vector<float>>> waveformFutures_;
  std::mutex waveformMutex_;
  // Settings modal
  bool settingsOpen_ = false;
  int settingsTab_ = 0; // 0=Audio 1=MIDI 2=OSC 3=Video 4=About
  SDL_Rect settingsCloseBtn_ {};
  SDL_Rect settingsGearRect_ {};
  SDL_Rect blackoutBtnRect_ {};
  double masterDimmerTarget_ = 1.0;  // target for animated masterDimmer (0=black, 1=full)
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
  std::vector<SocketHandle> companionClients_;
  std::map<SocketHandle, std::string> companionClientBuffers_;
  std::map<std::string, std::pair<sockaddr_in, Uint64>> oscSubscribers_;
  Uint64 lastOscFeedbackBroadcastMs_ = 0;
  std::string lastOscFeedbackPayload_;
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

  App app;
  if (!app.init()) {
    app.shutdown();
    return 1;
  }

  app.run();
  app.shutdown();
  return 0;
}
