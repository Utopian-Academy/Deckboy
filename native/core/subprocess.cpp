/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Deckboy — Cross-platform subprocess implementation
 * Copyright 2025 the owner
 */

#include "subprocess.hpp"

#include <array>
#include <cstring>

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// ===========================================================================
// ChildProcess lifetime
// ===========================================================================

bool ChildProcess::running() const {
#ifndef _WIN32
  return pid > 0;
#else
  return false;
#endif
}

void ChildProcess::stop() {
#ifndef _WIN32
  if (pid > 0) {
    pid_t target = pid;
    // Close read end first so any blocked read() unblocks
    if (readFd >= 0) {
      close(readFd);
      readFd = -1;
    }
    // SIGKILL always succeeds; avoids hangs on full pipes
    if (processGroup) {
      kill(-target, SIGKILL);
    } else {
      kill(target, SIGKILL);
    }
    int status = 0;
    waitpid(target, &status, 0);
    pid = -1;
    processGroup = false;
  }
  if (readFd >= 0) {
    close(readFd);
    readFd = -1;
  }
#endif
}

ChildProcess::~ChildProcess() {
  stop();
}

// ===========================================================================
// spawnProcess — unified entry point
// ===========================================================================

bool spawnProcess(ChildProcess& process,
                  const std::vector<std::string>& args,
                  const SpawnOptions& options) {
#ifdef _WIN32
  // TODO: Windows CreateProcessW implementation
  (void) process;
  (void) args;
  (void) options;
  return false;
#else
  process.stop();
  if (args.empty()) return false;

  // Create pipe if stdout is piped
  int pipeFd[2] = {-1, -1};
  if (options.stdoutMode == StdioMode::Pipe) {
    if (pipe(pipeFd) != 0) return false;
  }

  pid_t childPid = fork();
  if (childPid < 0) {
    if (pipeFd[0] >= 0) { close(pipeFd[0]); close(pipeFd[1]); }
    return false;
  }

  if (childPid == 0) {
    // --- child ---

    if (options.detached) {
      setsid();
    }

    // stdin
    if (options.stdinMode == StdioMode::Null) {
      int fd = open("/dev/null", O_RDONLY);
      if (fd >= 0) { dup2(fd, STDIN_FILENO); close(fd); }
    }

    // stdout
    if (options.stdoutMode == StdioMode::Pipe) {
      dup2(pipeFd[1], STDOUT_FILENO);
      close(pipeFd[0]);
      close(pipeFd[1]);
    } else if (options.stdoutMode == StdioMode::Null) {
      int fd = open("/dev/null", O_WRONLY);
      if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); }
    }
    // StdioMode::Inherit → leave as-is

    // stderr
    if (options.stderrMode == StdioMode::Null) {
      int fd = open("/dev/null", O_WRONLY);
      if (fd >= 0) { dup2(fd, STDERR_FILENO); close(fd); }
    } else if (options.stderrMode == StdioMode::Merge) {
      dup2(STDOUT_FILENO, STDERR_FILENO);
    }
    // StdioMode::Inherit → leave as-is

    // exec
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }

  // --- parent ---
  if (pipeFd[1] >= 0) close(pipeFd[1]);

  process.pid = childPid;
  process.readFd = pipeFd[0]; // -1 if no pipe
  process.processGroup = options.detached;
  return true;
#endif
}

// ===========================================================================
// readAllText — run, capture, wait
// ===========================================================================

std::optional<std::string> readAllText(const std::vector<std::string>& args) {
#ifdef _WIN32
  (void) args;
  return std::nullopt;
#else
  if (args.empty()) return std::nullopt;

  ChildProcess proc;
  if (!spawnProcess(proc, args, SpawnOptions::captureAll())) {
    return std::nullopt;
  }

  std::string output;
  std::array<char, 4096> buffer{};
  ssize_t bytesRead = 0;
  while ((bytesRead = read(proc.readFd, buffer.data(), buffer.size())) > 0) {
    output.append(buffer.data(), static_cast<size_t>(bytesRead));
  }
  close(proc.readFd);
  proc.readFd = -1;

  int status = 0;
  waitpid(proc.pid, &status, 0);
  pid_t savedPid = proc.pid;
  proc.pid = -1; // prevent double-kill in destructor

  (void) savedPid;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return std::nullopt;
  }
  return output;
#endif
}

// ===========================================================================
// Legacy convenience wrappers
// ===========================================================================

bool spawnPipeProcess(ChildProcess& process,
                      const std::vector<std::string>& args) {
  return spawnProcess(process, args, SpawnOptions::pipedStdout());
}

bool spawnDetachedProcess(ChildProcess& process,
                          const std::vector<std::string>& args) {
  return spawnProcess(process, args, SpawnOptions::detachedSilent());
}
