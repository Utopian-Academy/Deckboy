// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
//
// A child must not inherit another child's pipe.
//
// One thread keeps starting long-lived children (this program, told to sleep)
// while another runs short captured commands (this program, told to print a
// line) and reads each to the end. If a sleeper is started while a short
// command's pipe is open, and inherits it, the short command exits but its
// reader never sees end-of-file until the sleeper does: the read stalls for
// the sleeper's whole life. That is how live captions' helper, which lives all
// session, once left Deckboy waiting forever on an ffmpeg query.
//
// Exit 0 when every short command was read within 600 ms; 1 otherwise.

#include "core/paths.hpp"
#include "core/subprocess.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--sleep") == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "--echo") == 0) {
    std::puts("echo");
    return 0;
  }
  const std::string self = deckboy::core::Paths::executablePath().string();

  std::atomic<bool> done {false};
  std::thread sleepers([&] {
    // Enough slots that a sleeper is never killed early to make room: a
    // stolen pipe is then held for the sleeper's whole 1.5 s.
    std::vector<ChildProcess> live(400);
    std::size_t next = 0;
    SpawnOptions quiet;
    quiet.stdinMode = StdioMode::Null;
    quiet.stdoutMode = StdioMode::Null;
    quiet.stderrMode = StdioMode::Null;
    while (!done.load()) {
      spawnProcess(live[next], {self, "--sleep"}, quiet);   // stops the one it replaces
      next = (next + 1) % live.size();
      std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    for (auto& child : live) child.stop();
  });

  int slow = 0;
  const int runs = 60;
  for (int i = 0; i < runs; ++i) {
    const auto start = std::chrono::steady_clock::now();
    const ProcessCapture got = runCaptured({self, "--echo"});
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    // A clean run takes tens of milliseconds; a stolen pipe holds it until
    // the sleeper that took it exits.
    if (ms > 600.0 || got.output.find("echo") == std::string::npos) {
      ++slow;
      std::printf("run %d: %.0f ms -- a sleeper held its pipe\n", i, ms);
    }
  }
  done.store(true);
  sleepers.join();
  std::printf("spawn-race: %d of %d captured reads stalled\n", slow, runs);
  return slow ? 1 : 0;
}
