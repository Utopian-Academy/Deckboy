// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// live_captions.hpp -- captions of what is being said, as it is said.
//
// James, 2026-10-14, on the design: "love it". Words appear about two seconds
// after they are spoken, the newest faint until the engine is sure of them.
//
// The listening is done by deckboy-listen (tools/listen), a separate program
// that holds the speech model and answers on its stdin/stdout: no network
// socket, nothing else on the machine can reach it, and a crash in it stops
// the captions, never the show. It is restarted a few times before giving up.
//
// HOW WORDS BECOME SURE. Once a second the last few seconds of audio go to the
// engine. A word is committed when two passes in a row agree on it (and on
// everything before it): a word heard on the edge of the window is often
// wrong ("queue" for "cue") until the next second of audio arrives. What is
// not yet agreed is shown faint. A pause commits what is pending, since there
// is nothing more coming to change it. The window is trimmed behind the
// committed words, so a pass never grows past about ten seconds of audio.
//
// SDL-free: the app feeds it the 48 kHz stereo it already mixes for outputs.
// ============================================================================

#pragma once

#include "core/captioning.hpp"
#include "core/io_utils.hpp"
#include "core/subprocess.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace deckboy::captioning {

// Where deckboy-listen is: DECKBOY_LISTEN, beside Deckboy, or in a whisper
// folder beside it or in the state folder -- the places whisper-cli is found.
// Empty on a system too old to run it (captionsNeedNewerSystem).
inline fs::path listenPath() {
  if (captionsNeedNewerSystem()) return {};
  if (const char* env = std::getenv("DECKBOY_LISTEN"); env && *env && fs::exists(env)) return fs::path(env);
  const fs::path exeDir = deckboy::core::Paths::executablePath().parent_path();
  const std::string name = exeName("deckboy-listen");
  for (const fs::path& candidate : {exeDir / name, exeDir / "whisper" / name,
                                    deckboy::core::Paths::stateDir() / "whisper" / name}) {
    std::error_code ec;
    if (fs::exists(candidate, ec)) return candidate;
  }
  return {};
}

struct CaptionWord {
  std::string text;
  bool sure = false;
};

// One word as the engine heard it: tokens joined, probability the least of
// them, times in centiseconds from the start of the audio it was given.
struct HeardWord {
  std::string text;
  float p = 0.0f;
  long long t0 = 0, t1 = 0;
};

// Engine tokens to words. A token starting with a space starts a word;
// "[BLANK_AUDIO]"-style markers and bracketed sound descriptions are dropped.
inline std::vector<HeardWord> wordsFromTokens(const std::vector<HeardWord>& tokens) {
  std::vector<HeardWord> words;
  for (const HeardWord& token : tokens) {
    if (token.text.empty()) continue;
    const bool starts = token.text.front() == ' ' || words.empty();
    const std::string piece = token.text.front() == ' ' ? token.text.substr(1) : token.text;
    if (piece.empty()) continue;
    if (starts) {
      words.push_back({piece, token.p, token.t0, token.t1});
    } else {
      words.back().text += piece;
      words.back().p = std::min(words.back().p, token.p);
      words.back().t1 = token.t1;
    }
  }
  words.erase(std::remove_if(words.begin(), words.end(), [](const HeardWord& w) {
    return w.text.front() == '[' || w.text.front() == '(' || w.text.front() == '*';
  }), words.end());
  return words;
}

// Two hearings of a word agree when their letters and digits do: "lights,"
// then "lights" is one word whose punctuation arrived late.
inline bool sameWord(const std::string& a, const std::string& b) {
  std::string x, y;
  for (char c : a) if (std::isalnum(static_cast<unsigned char>(c))) x += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (char c : b) if (std::isalnum(static_cast<unsigned char>(c))) y += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return x == y;
}

inline std::string srtTime(double ms) {
  const long long t = std::max<long long>(0, std::llround(ms));
  char out[32];
  std::snprintf(out, sizeof(out), "%02lld:%02lld:%02lld,%03lld", t / 3600000, t / 60000 % 60, t / 1000 % 60, t % 1000);
  return out;
}

class LiveCaptioner {
 public:
  struct Status {
    bool running = false;   // a session is on (the engine may be restarting)
    bool ready = false;     // the engine has the model loaded and is answering
    std::string error;      // why it is not, in words for the operator
    double behindSeconds = 0.0;  // from a word being said to it being sure
    double level = 0.0;          // input RMS, 0..1, for "it hears something"
    double passMs = 0.0;         // the last pass, for the operator's peace of mind
  };

  ~LiveCaptioner() { stop(); }

  // `transcript` empty: no file. Returns false (and why) when it cannot start.
  bool start(const fs::path& engine, const fs::path& model, const std::string& language,
             const fs::path& transcript, std::string& why) {
    stop();
    if (engine.empty()) { why = "live captions are not installed here (deckboy-listen was not found)"; return false; }
    if (!fs::exists(model)) { why = "the caption model has not been downloaded yet"; return false; }
    engine_ = engine;
    model_ = model;
    language_ = language;
    transcriptPath_ = transcript;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      history_.clear();
      tentative_.clear();
      pending_.clear();
      status_ = {};
      status_.running = true;
    }
    stop_ = false;
    origin_ = std::chrono::steady_clock::now();
    worker_ = std::thread([this] { run(); });
    return true;
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    wake_.notify_all();
    {
      std::lock_guard<std::mutex> lock(childMutex_);
      child_.killProcessOnly();   // unblocks a read the worker is waiting in
    }
    if (worker_.joinable()) worker_.join();
    child_.stop();
    std::lock_guard<std::mutex> lock(mutex_);
    status_.running = false;
    status_.ready = false;
  }

  bool running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.running;
  }

  // 48 kHz interleaved stereo, as mixed for the outputs. Main thread; cheap.
  void feed(const std::int32_t* mixed, std::size_t interleaved) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!status_.running) return;
    double energy = 0.0;
    std::size_t frames = 0;
    for (std::size_t i = 0; i + 1 < interleaved; i += 2, ++frames) {
      const float mono = static_cast<float>((mixed[i] + mixed[i + 1]) / 65536.0);
      // 48 -> 16 kHz: a 1-2-3-2-1 triangle over the last five, every third.
      tap_[0] = tap_[1]; tap_[1] = tap_[2]; tap_[2] = tap_[3]; tap_[3] = tap_[4]; tap_[4] = mono;
      if (++phase_ == 3) {
        phase_ = 0;
        const float v = (tap_[0] + 2 * tap_[1] + 3 * tap_[2] + 2 * tap_[3] + tap_[4]) / 9.0f;
        pending_.push_back(v);
        energy += static_cast<double>(v) * v;
      }
    }
    // Keep at most a minute: an engine that has stopped answering must not
    // make the show's memory grow.
    if (pending_.size() > 16000u * 60) pending_.erase(pending_.begin(), pending_.end() - 16000 * 60);
    const std::size_t heard = std::max<std::size_t>(1, frames / 3);
    const double rms = std::sqrt(energy / static_cast<double>(heard));
    status_.level = std::max(rms, status_.level * 0.85);
  }

  // The last `max` words, oldest first: sure ones, then the faint tail.
  // Nothing new for kClearAfterMs and nothing pending: the screen clears, as
  // roll-up captions do, rather than holding the last sentence over whatever
  // happens next. The transcript keeps everything.
  std::vector<CaptionWord> recentWords(std::size_t max) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CaptionWord> out;
    if (tentative_.empty() && nowMs() - lastWordMs_ > kClearAfterMs) return out;
    for (const auto& w : history_) out.push_back({w, true});
    for (const auto& w : tentative_) out.push_back({w, false});
    if (out.size() > max) out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(max));
    return out;
  }

  Status status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
  }

 private:
  static constexpr int kRate = 16000;
  static constexpr int kStepMs = 1000;
  static constexpr double kSilence = 0.006;   // RMS under this is nobody talking
  static constexpr std::size_t kTrimSamples = kRate * 8;
  static constexpr std::size_t kMaxSamples = kRate * 15;
  static constexpr double kClearAfterMs = 6000.0;

  double nowMs() const {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - origin_).count();
  }

  static bool writeAll(int fd, const void* data, std::size_t size) {
    const auto* at = static_cast<const char*>(data);
    while (size) {
#ifdef _WIN32
      const int wrote = _write(fd, at, static_cast<unsigned>(std::min<std::size_t>(size, 1u << 20)));
#else
      const auto wrote = ::write(fd, at, size);
#endif
      if (wrote <= 0) return false;
      at += wrote;
      size -= static_cast<std::size_t>(wrote);
    }
    return true;
  }

  bool readLine(std::string& line) {
    line.clear();
    for (;;) {
      const auto nl = inbox_.find('\n');
      if (nl != std::string::npos) {
        line = inbox_.substr(0, nl);
        inbox_.erase(0, nl + 1);
        return true;
      }
      char buffer[4096];
      const int got = ::readSome(child_.readFd, buffer, sizeof(buffer));
      if (got <= 0) return false;
      inbox_.append(buffer, static_cast<std::size_t>(got));
    }
  }

  void fail(const std::string& why) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.ready = false;
    status_.error = why;
  }

  bool launch() {
    std::unique_lock<std::mutex> childLock(childMutex_);
    if (stop_) return false;  // stop() won the race: do not start what it is ending
    child_.stop();
    inbox_.clear();
    ::SpawnOptions options;
    options.stdinMode = ::StdioMode::Pipe;
    options.stdoutMode = ::StdioMode::Pipe;
    options.stderrMode = ::StdioMode::Null;
    const unsigned threads = std::clamp(std::thread::hardware_concurrency() / 2, 1u, 8u);
    if (!::spawnProcess(child_, {engine_.string(), model_.string(), "--language", language_,
                                              "--threads", std::to_string(threads)}, options)) {
      fail("the speech engine could not be started");
      return false;
    }
    childLock.unlock();  // the READY wait can be long; stop() must be able to kill it
    std::string line;
    if (!readLine(line) || line != "READY") {
      fail(line.rfind("ERR ", 0) == 0 ? line.substr(4) : "the speech engine did not start");
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    status_.ready = true;
    status_.error.clear();
    return true;
  }

  bool listen(const std::vector<float>& audio, std::vector<HeardWord>& words, double& passMs) {
    const std::string head = "LISTEN " + std::to_string(audio.size()) + " 0\n";
    if (!writeAll(child_.writeFd, head.data(), head.size()) ||
        !writeAll(child_.writeFd, audio.data(), audio.size() * sizeof(float))) return false;
    std::string line;
    if (!readLine(line)) return false;
    unsigned long long ms = 0, count = 0;
    if (std::sscanf(line.c_str(), "OK %llu %llu", &ms, &count) != 2) return false;
    std::vector<HeardWord> tokens;
    for (unsigned long long i = 0; i < count; ++i) {
      if (!readLine(line)) return false;
      HeardWord token;
      const auto a = line.find('\t'), b = line.find('\t', a + 1), c = line.find('\t', b + 1);
      if (c == std::string::npos) continue;
      token.p = std::strtof(line.c_str(), nullptr);
      token.t0 = std::atoll(line.c_str() + a + 1);
      token.t1 = std::atoll(line.c_str() + b + 1);
      token.text = line.substr(c + 1);
      tokens.push_back(token);
    }
    words = wordsFromTokens(tokens);
    passMs = static_cast<double>(ms);
    return true;
  }

  bool heardOnlySilence(const std::vector<float>& window) const { return rms(window, 0) < kSilence; }

  static double rms(const std::vector<float>& audio, std::size_t from) {
    double e = 0.0;
    for (std::size_t i = from; i < audio.size(); ++i) e += static_cast<double>(audio[i]) * audio[i];
    return audio.size() > from ? std::sqrt(e / static_cast<double>(audio.size() - from)) : 0.0;
  }

  // Committed words go to the screen's history and, in lines, to the transcript.
  void commit(const std::vector<HeardWord>& words, std::size_t from, std::size_t to, double windowStartMs) {
    if (from >= to) return;
    const double now = nowMs();
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = from; i < to; ++i) {
      history_.push_back(words[i].text);
      lastWordMs_ = now;
      const double said = windowStartMs + words[i].t1 * 10.0;
      const double behind = std::max(0.0, (now - said) / 1000.0);
      status_.behindSeconds = status_.behindSeconds == 0.0 ? behind : status_.behindSeconds * 0.8 + behind * 0.2;
      // A line never starts before the last one ended: the engine's word
      // times overlap a little, and overlapping subtitles are invalid.
      if (lineWords_.empty()) lineStartMs_ = std::max(lastLineEndMs_, windowStartMs + words[i].t0 * 10.0);
      lineWords_.push_back(words[i].text);
      lineEndMs_ = said;
      const char last = words[i].text.back();
      if (lineWords_.size() >= 9 || last == '.' || last == '?' || last == '!') flushLineLocked();
    }
    while (history_.size() > 200) history_.pop_front();
  }

  // Where the open words start in a pass: just after the best match for the
  // last (up to four) committed words, allowing one of them to be heard
  // differently this time. Ties go to the match nearest where the committed
  // words ended. No match of at least two words (one, if only one was
  // committed): fall back to the engine's times.
  std::size_t openFrom(const std::vector<HeardWord>& heard) const {
    if (tail_.empty()) return 0;
    const std::size_t k = tail_.size();
    const std::size_t need = std::min<std::size_t>(2, k);
    std::size_t best = 0, bestEnd = 0;
    double bestDistance = 0.0;
    for (std::size_t end = 0; end < heard.size(); ++end) {
      std::size_t score = 0;
      for (std::size_t i = 0; i < k && i <= end; ++i)
        if (sameWord(heard[end - i].text, tail_[k - 1 - i])) ++score;
      const double distance = std::abs(static_cast<double>(heard[end].t1) - committedEndCs_);
      if (score > best || (score == best && score > 0 && distance < bestDistance)) {
        best = score;
        bestEnd = end;
        bestDistance = distance;
      }
    }
    if (best >= need && best + 1 >= k) return bestEnd + 1;
    std::size_t at = 0;
    while (at < heard.size() && heard[at].t0 < committedEndCs_ - 15) ++at;
    return at;
  }

  void flushLineLocked() {
    if (lineWords_.empty()) return;
    if (!transcriptPath_.empty()) {
      std::ofstream out(transcriptPath_, std::ios::app | std::ios::binary);
      std::string text;
      for (const auto& w : lineWords_) text += (text.empty() ? "" : " ") + w;
      out << ++srtIndex_ << "\n" << srtTime(lineStartMs_) << " --> "
          << srtTime(std::max(lineEndMs_, lineStartMs_ + 500.0)) << "\n" << text << "\n\n";
    }
    lastLineEndMs_ = std::max(lineEndMs_, lineStartMs_ + 500.0);
    lineWords_.clear();
  }

  void run() {
    int restarts = 0;
    std::vector<float> window;
    double windowStartMs = 0.0;
    std::vector<HeardWord> previous;
    double quietSinceMs = -1.0;
    while (true) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_) break;
      }
      if (!child_.running() || !status().ready) {
        if (restarts > 3) {
          fail("the speech engine stopped and could not be restarted");
          std::unique_lock<std::mutex> lock(mutex_);
          wake_.wait_for(lock, std::chrono::seconds(5), [this] { return stop_.load(); });
          continue;
        }
        if (restarts++ > 0) {
          std::unique_lock<std::mutex> lock(mutex_);
          if (wake_.wait_for(lock, std::chrono::seconds(3), [this] { return stop_.load(); })) break;
        }
        if (!launch()) continue;
        window.clear();
        previous.clear();
        committedEndCs_ = 0.0;
        tail_.clear();
      }
      {
        std::unique_lock<std::mutex> lock(mutex_);
        if (wake_.wait_for(lock, std::chrono::milliseconds(kStepMs), [this] { return stop_.load(); })) break;
        if (window.empty()) windowStartMs = nowMs() - pending_.size() * 1000.0 / kRate;
        window.insert(window.end(), pending_.begin(), pending_.end());
        pending_.clear();
      }
      if (window.empty() || heardOnlySilence(window)) {
        // Nobody talking means no passes, and without a pass a dead engine
        // goes unnoticed until somebody speaks -- whose first words are then
        // lost to the restart (measured). An empty request every two seconds
        // finds it while the room is quiet.
        if (nowMs() - lastHeartbeatMs_ > 2000.0) {
          lastHeartbeatMs_ = nowMs();
          std::vector<HeardWord> none;
          double ms = 0.0;
          if (!listen({}, none, ms)) {
            fail("the speech engine stopped; restarting");
            std::lock_guard<std::mutex> lock(childMutex_);
            child_.stop();
          }
        }
        if (window.empty()) continue;
      }
      // Nobody talking: say nothing, and let what was pending stand.
      const bool quietNow = rms(window, window.size() > kRate ? window.size() - kRate : 0) < kSilence;
      if (quietNow) {
        if (quietSinceMs < 0) quietSinceMs = nowMs();
        if (rms(window, 0) < kSilence) {
          window.clear();
          previous.clear();
          committedEndCs_ = 0.0;
          tail_.clear();
          std::lock_guard<std::mutex> lock(mutex_);
          tentative_.clear();
          flushLineLocked();
          // Long enough to have cleared from the screen: the next words start
          // a fresh caption instead of rolling on from the old one.
          if (nowMs() - lastWordMs_ > kClearAfterMs) history_.clear();
          continue;
        }
      } else {
        quietSinceMs = -1.0;
      }
      std::vector<HeardWord> heard;
      double passMs = 0.0;
      if (!listen(window, heard, passMs)) {
        fail("the speech engine stopped; restarting");
        {
          std::lock_guard<std::mutex> lock(childMutex_);
          child_.stop();
        }
        continue;
      }
      restarts = std::min(restarts, 1);
      // Only what comes AFTER the committed words is still open.
      //  - Not by counting from the start: a pass re-hears a committed word
      //    differently ("cue", "queue", "cue"), and counting then disagreed at
      //    that word on every pass -- measured, seven seconds of faint words.
      //  - Not by the engine's word times alone: they overlap by a word or so,
      //    and cutting on them dropped "on in the" (measured).
      // So by the words: where the last committed ones reappear in this pass.
      // Time is the fallback when they do not.
      const std::size_t from = openFrom(heard);
      std::vector<HeardWord> open(heard.begin() + static_cast<std::ptrdiff_t>(from), heard.end());
      // Agreement: the longest run that this pass and the last one share. A
      // pause ends the sentence: nothing more is coming to change it.
      // One word heard two ways ("cue" / "queue"), with the next two agreed
      // on both sides, is as settled as it is going to get: it takes this
      // pass's hearing. Holding out for it held every word after it faint for
      // as long as it kept flipping (measured: the whole first sentence).
      std::size_t agreed = 0;
      while (agreed < open.size() && agreed < previous.size()) {
        const bool same = sameWord(open[agreed].text, previous[agreed].text);
        const bool flipped = !same && agreed + 2 < open.size() && agreed + 2 < previous.size() &&
                             sameWord(open[agreed + 1].text, previous[agreed + 1].text) &&
                             sameWord(open[agreed + 2].text, previous[agreed + 2].text);
        if (!same && !flipped) break;
        ++agreed;
      }
      const bool pause = quietSinceMs >= 0 && nowMs() - quietSinceMs > 1200.0;
      if (pause || window.size() >= kMaxSamples) agreed = open.size();
      commit(open, 0, agreed, windowStartMs);
      if (agreed) committedEndCs_ = static_cast<double>(open[agreed - 1].t1);
      for (std::size_t i = 0; i < agreed; ++i) tail_.push_back(open[i].text);
      while (tail_.size() > 4) tail_.erase(tail_.begin());
      previous.assign(open.begin() + static_cast<std::ptrdiff_t>(agreed), open.end());
      {
        std::lock_guard<std::mutex> lock(mutex_);
        tentative_.clear();
        for (const HeardWord& w : previous) tentative_.push_back(w.text);
        if (!tentative_.empty()) lastWordMs_ = nowMs();
        status_.passMs = passMs;
      }
      if (pause || window.size() >= kMaxSamples) {
        window.clear();
        previous.clear();
        committedEndCs_ = 0.0;
        tail_.clear();
        std::lock_guard<std::mutex> lock(mutex_);
        tentative_.clear();
        flushLineLocked();
      } else if (window.size() >= kTrimSamples && committedEndCs_ > 0.0) {
        // Trim behind the committed words so the next pass stays short.
        // 0.6 s short of the committed end: the engine's word times run into
        // the next word, and a cut right on them clipped "music" off "music
        // starts" (measured). The overlap is heard again and openFrom() drops it.
        const double cutCs = std::max(0.0, committedEndCs_ - 60.0);
        const std::size_t drop = std::min(window.size(), static_cast<std::size_t>(cutCs) * (kRate / 100));
        window.erase(window.begin(), window.begin() + static_cast<std::ptrdiff_t>(drop));
        windowStartMs += drop * 1000.0 / kRate;
        committedEndCs_ -= cutCs;  // in the trimmed window's time; the tail stays for openFrom()
      }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    flushLineLocked();
  }

  fs::path engine_, model_, transcriptPath_;
  std::string language_;
  ::ChildProcess child_;          // childMutex_ for start/kill/stop
  std::mutex childMutex_;
  std::string inbox_;
  std::thread worker_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::atomic<bool> stop_ {false};          // set under mutex_ (the waits), read anywhere
  std::vector<float> pending_;              // mutex_: 16 kHz mono not yet heard
  float tap_[5] {};                         // mutex_
  int phase_ = 0;                           // mutex_
  std::deque<std::string> history_;         // mutex_
  std::vector<std::string> tentative_;      // mutex_
  Status status_;                           // mutex_
  std::vector<std::string> lineWords_;      // mutex_
  double lineStartMs_ = 0.0, lineEndMs_ = 0.0, lastLineEndMs_ = 0.0;
  double lastWordMs_ = -1e12;               // mutex_: when a word was last committed
  double lastHeartbeatMs_ = 0.0;            // worker
  int srtIndex_ = 0;
  double committedEndCs_ = 0.0;             // worker: end of the committed words, in window time
  std::vector<std::string> tail_;           // worker: the last committed words in the window
  std::chrono::steady_clock::time_point origin_;
};

}  // namespace deckboy::captioning
