/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Playboy_0.01 - Subprocess Management
 * Copyright 2025 the owner
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

// Lightweight subprocess with pipe I/O (Unix-only)
struct ChildProcess {
#ifndef _WIN32
  pid_t pid = -1;
  int readFd = -1;
  bool processGroup = false;
#endif

  bool running() const;
  void stop();
  ~ChildProcess();

  ChildProcess() = default;
  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;
  ChildProcess(ChildProcess&& other) noexcept : pid(other.pid), readFd(other.readFd), processGroup(other.processGroup) {
    other.pid = -1;
    other.readFd = -1;
    other.processGroup = false;
  }
  ChildProcess& operator=(ChildProcess&& other) noexcept {
    stop();
    pid = other.pid;
    readFd = other.readFd;
    processGroup = other.processGroup;
    other.pid = -1;
    other.readFd = -1;
    other.processGroup = false;
    return *this;
  }
};

// Execute command and capture all output (returns empty if fails)
std::optional<std::string> readAllText(const std::vector<std::string>& args);

// Spawn subprocess with stdout piped to readFd (Unix-only, returns false on Windows)
bool spawnPipeProcess(ChildProcess& process, const std::vector<std::string>& args);
