// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// captioning.hpp -- captions made on this machine, for a cue that has none.
//
// James, 2026-10-07: "is there any way to have deckboy generate captions if
// there are none, optionally? (some sort of local trained captioning bit that
// can be activated?)"
//
// whisper.cpp (MIT) does the listening, as its own small program beside
// Deckboy -- the way ffmpeg is called, so a crash in it is never a crash in a
// show. Nothing leaves the machine. The model is NOT shipped: it is fetched
// once, on the operator's say-so, into the state folder.
//
// The result is an ordinary SRT beside the media (<name>.auto.srt, never over
// a caption file somebody made by hand), which the desk's caption display and
// Mini's subtitle search already read.
// ============================================================================

#pragma once

#include "core/paths.hpp"
#include "core/subprocess.hpp"

#include "core/sdl_compat.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace deckboy::captioning {

namespace fs = std::filesystem;

// "base.en": English, 142 MB, about seven times faster than real time on an
// ordinary CPU and right about almost every word. "base" is the same size for
// 99 languages; "small" / "small.en" (466 MB) are slower and better.
inline const char* kDefaultModel = "base.en";

inline std::string exeName(const std::string& stem) {
#ifdef _WIN32
  return stem + ".exe";
#else
  return stem;
#endif
}

// Where whisper-cli is: DECKBOY_WHISPER, beside Deckboy, a whisper folder
// beside it, the state folder, then PATH. Empty when there is none anywhere.
inline fs::path whisperCliPath() {
  std::error_code ec;
  if (const char* env = std::getenv("DECKBOY_WHISPER"); env && *env && fs::exists(env, ec)) {
    return fs::path(env);
  }
  const fs::path exeDir = deckboy::core::Paths::executablePath().parent_path();
  const std::string name = exeName("whisper-cli");
  for (const fs::path& candidate : {exeDir / name, exeDir / "whisper" / name,
                                    deckboy::core::Paths::stateDir() / "whisper" / name}) {
    if (fs::exists(candidate, ec)) return candidate;
  }
  // PATH: runCaptured resolves a bare name the way a shell would.
  const auto probe = ::runCaptured({"whisper-cli", "--help"});
  if (probe.ran) return fs::path("whisper-cli");
  return {};
}

inline fs::path modelPath(const std::string& model) {
  return deckboy::core::Paths::stateDir() / "models" / ("ggml-" + model + ".bin");
}

inline std::string modelUrl(const std::string& model) {
  return "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-" + model + ".bin";
}

inline bool modelPresent(const std::string& model) {
  std::error_code ec;
  return fs::exists(modelPath(model), ec) && fs::file_size(modelPath(model), ec) > 1000000;
}

struct Result {
  bool ok = false;
  std::string message;   // what to tell the operator, either way
  fs::path srt;          // the caption file, when ok
};

// The one-time download, with the curl every supported OS now ships. Written to
// .part and renamed, so an interrupted download is never taken for a model.
inline Result downloadModel(const std::string& model) {
  std::error_code ec;
  const fs::path target = modelPath(model);
  fs::create_directories(target.parent_path(), ec);
  const fs::path part = target.string() + ".part";
  const auto got = ::runCaptured(
    {"curl", "-sL", "--fail", "-o", part.string(), modelUrl(model)});
  if (!got.ok() || !fs::exists(part, ec) || fs::file_size(part, ec) < 1000000) {
    fs::remove(part, ec);
    return {false, "caption model download failed" + (got.output.empty() ? std::string() : ": " + got.output.substr(0, 120)), {}};
  }
  fs::rename(part, target, ec);
  if (ec) return {false, "could not keep the caption model: " + ec.message(), {}};
  return {true, "caption model ready", {}};
}

// Where the captions go: beside the media, as <name>.auto.srt, unless that
// folder cannot be written -- then the state folder's captions folder.
inline fs::path captionFileFor(const fs::path& media) {
  const fs::path beside = media.parent_path() / (media.stem().string() + ".auto.srt");
  std::error_code ec;
  const fs::path probe = media.parent_path() / ".deckboy-write-test";
  {
    std::FILE* f = std::fopen(probe.string().c_str(), "wb");
    if (f) {
      std::fclose(f);
      fs::remove(probe, ec);
      return beside;
    }
  }
  const fs::path dir = deckboy::core::Paths::stateDir() / "captions";
  fs::create_directories(dir, ec);
  return dir / beside.filename();
}

// Listen to a media file and write captions. Blocking: call it off the main
// thread. `language` is a code ("en", "de") or "auto".
inline Result transcribe(const fs::path& media, const std::string& model = kDefaultModel,
                         const std::string& language = "auto") {
  const fs::path cli = whisperCliPath();
  if (cli.empty()) {
    return {false, "captioning is not installed here (whisper-cli was not found)", {}};
  }
  if (!modelPresent(model)) {
    return {false, "the caption model has not been downloaded yet", {}};
  }
  std::error_code ec;
  const fs::path work = deckboy::core::Paths::stateDir() / "captions-work";
  fs::create_directories(work, ec);
  const fs::path wav = work / (media.stem().string() + "-16k.wav");
  // 16 kHz mono PCM is what whisper listens to.
  const auto extract = ::runCaptured(
    {"ffmpeg", "-v", "error", "-y", "-i", media.string(), "-vn", "-ac", "1", "-ar", "16000",
     "-c:a", "pcm_s16le", wav.string()});
  if (!extract.ok()) {
    return {false, "could not read the sound: " + extract.output.substr(0, 120), {}};
  }
  const fs::path srt = captionFileFor(media);
  fs::path base = srt;
  base.replace_extension();   // whisper adds .srt itself
  const int threads = std::clamp(SDL_GetNumLogicalCPUCores() - 1, 1, 8);
  // -ml 42 -sow: lines broken at words around 42 characters, the length a
  // caption can be read at; -np: no progress chatter in the log.
  std::vector<std::string> args = {cli.string(), "-m", modelPath(model).string(), "-f", wav.string(),
                                   "-osrt", "-of", base.string(), "-ml", "42", "-sow", "-np",
                                   "-t", std::to_string(threads)};
  if (model.size() < 3 || model.substr(model.size() - 3) != ".en") {
    args.insert(args.end(), {"-l", language});
  }
  const auto run = ::runCaptured(args);
  fs::remove(wav, ec);
  if (!run.ok() || !fs::exists(srt, ec)) {
    return {false, "captioning failed: " + run.output.substr(0, 160), {}};
  }
  return {true, "captions made: " + srt.filename().string(), srt};
}

}  // namespace deckboy::captioning
