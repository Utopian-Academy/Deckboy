// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// deckboy-listen -- the speech engine behind live captions.
//
// A separate program, the way ffmpeg and whisper-cli are: if it crashes, the
// captions stop and the show does not. It loads the model ONCE and then
// answers on its standard input and output, so there is no network socket,
// no web page and nothing another program on the machine can reach.
//
//   deckboy-listen <model.bin> [--language en|auto] [--threads N]
//   -> "READY" once the model is loaded, or "ERR <why>" and exit 1
//
// Each request is one line, then its bytes:
//   LISTEN <samples> <prompt-bytes>\n<prompt><samples x float32 LE, 16 kHz mono>
//   -> "OK <ms> <count>\n" and then <count> lines "<p>\t<t0>\t<t1>\t<text>"
//      (one per token: probability 0..1, start and end in centiseconds from
//       the start of the audio, the token's text with any newline flattened)
//   QUIT\n -> exits. End of input (Deckboy gone) also exits.
//
// Built against whisper.cpp (MIT), the same release CI builds whisper-cli from.

#include "whisper.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

bool readExact(void* into, std::size_t bytes) {
  auto* at = static_cast<char*>(into);
  while (bytes) {
    const std::size_t got = std::fread(at, 1, bytes, stdin);
    if (got == 0) return false;
    at += got;
    bytes -= got;
  }
  return true;
}

bool readLine(std::string& line) {
  line.clear();
  for (int c; (c = std::fgetc(stdin)) != EOF;) {
    if (c == '\n') return true;
    if (line.size() > 256) return false;  // a header line is short; anything else is not ours
    line.push_back(static_cast<char>(c));
  }
  return false;
}

void quietLog(ggml_log_level, const char*, void*) {}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  if (argc < 2) {
    std::printf("ERR usage: deckboy-listen <model.bin> [--language en|auto] [--threads N]\n");
    return 1;
  }
  std::string language = "en";
  int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) / 2, 1, 8);
  for (int i = 2; i + 1 < argc; i += 2) {
    if (std::strcmp(argv[i], "--language") == 0) language = argv[i + 1];
    else if (std::strcmp(argv[i], "--threads") == 0) threads = std::clamp(std::atoi(argv[i + 1]), 1, 64);
  }
  whisper_log_set(quietLog, nullptr);
  whisper_context_params cparams = whisper_context_default_params();
  whisper_context* ctx = whisper_init_from_file_with_params(argv[1], cparams);
  if (!ctx) {
    std::printf("ERR could not load the speech model %s\n", argv[1]);
    std::fflush(stdout);
    return 1;
  }
  std::printf("READY\n");
  std::fflush(stdout);

  std::vector<float> samples;
  std::string line, prompt;
  while (readLine(line)) {
    if (line == "QUIT") break;
    unsigned long count = 0, promptBytes = 0;
    if (std::sscanf(line.c_str(), "LISTEN %lu %lu", &count, &promptBytes) != 2 ||
        count > 16000ul * 60 || promptBytes > 4096) {
      std::printf("ERR bad request\n");
      std::fflush(stdout);
      break;  // out of step with the stream: nothing after this can be trusted
    }
    prompt.assign(promptBytes, '\0');
    samples.resize(count);
    if ((promptBytes && !readExact(prompt.data(), promptBytes)) ||
        (count && !readExact(samples.data(), count * sizeof(float)))) {
      break;
    }
    const auto started = std::chrono::steady_clock::now();
    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads = threads;
    params.language = language.c_str();
    params.detect_language = false;
    params.translate = false;
    params.no_context = true;
    params.single_segment = false;
    params.token_timestamps = true;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_special = false;
    params.print_timestamps = false;
    params.suppress_blank = true;
    params.initial_prompt = prompt.empty() ? nullptr : prompt.c_str();
    // Shorter than a second is padded by whisper itself; nothing to say about silence.
    const int ran = count ? whisper_full(ctx, params, samples.data(), static_cast<int>(count)) : 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
    if (ran != 0) {
      std::printf("ERR the speech engine failed (%d)\n", ran);
      std::fflush(stdout);
      continue;
    }
    std::vector<std::string> out;
    const whisper_token eot = whisper_token_eot(ctx);
    const int segments = whisper_full_n_segments(ctx);
    for (int s = 0; s < segments; ++s) {
      const int tokens = whisper_full_n_tokens(ctx, s);
      for (int t = 0; t < tokens; ++t) {
        const whisper_token_data data = whisper_full_get_token_data(ctx, s, t);
        if (data.id >= eot) continue;  // timestamps and control tokens are not words
        std::string text = whisper_full_get_token_text(ctx, s, t);
        std::replace(text.begin(), text.end(), '\n', ' ');
        std::replace(text.begin(), text.end(), '\r', ' ');
        std::replace(text.begin(), text.end(), '\t', ' ');
        char head[64];
        std::snprintf(head, sizeof(head), "%.3f\t%lld\t%lld\t", data.p,
                      static_cast<long long>(data.t0), static_cast<long long>(data.t1));
        out.push_back(head + text);
      }
    }
    std::printf("OK %lld %zu\n", static_cast<long long>(ms), out.size());
    for (const auto& one : out) std::printf("%s\n", one.c_str());
    std::fflush(stdout);
  }
  whisper_free(ctx);
  return 0;
}
