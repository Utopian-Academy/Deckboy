/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Playboy_0.01 - Subprocess Implementation
 * Copyright 2025 the owner
 */

#include <array>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#endif

#include "subprocess.hpp"

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
