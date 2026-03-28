/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Deckboy — Cross-platform subprocess management
 * Copyright 2025 James
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// ChildProcess — owns a running subprocess handle and its optional pipe fd
// ---------------------------------------------------------------------------
struct ChildProcess {
#ifdef _WIN32
  HANDLE hProcess = INVALID_HANDLE_VALUE;
  int readFd = -1;  // CRT fd from _open_osfhandle; -1 if no pipe
#else
  pid_t pid = -1;
  int readFd = -1;
  bool processGroup = false;
#endif

  bool running() const;
  void stop();
  // Kill the child process only (does NOT close readFd).
  // Use this to unblock a _read() in another thread: killing the process
  // closes the write end of the pipe, causing _read() to return 0 (EOF)
  // rather than -1 (EBADF).  Call stop() after joining the reader thread.
  void killProcessOnly();
  ~ChildProcess();

  ChildProcess() = default;
  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;
  ChildProcess(ChildProcess&& other) noexcept
#ifdef _WIN32
    : hProcess(other.hProcess), readFd(other.readFd)
#else
    : pid(other.pid), readFd(other.readFd), processGroup(other.processGroup)
#endif
  {
#ifdef _WIN32
    other.hProcess = INVALID_HANDLE_VALUE;
    other.readFd = -1;
#else
    other.pid = -1;
    other.readFd = -1;
    other.processGroup = false;
#endif
  }
  ChildProcess& operator=(ChildProcess&& other) noexcept {
    stop();
#ifdef _WIN32
    hProcess = other.hProcess;
    readFd = other.readFd;
    other.hProcess = INVALID_HANDLE_VALUE;
    other.readFd = -1;
#else
    pid = other.pid;
    readFd = other.readFd;
    processGroup = other.processGroup;
    other.pid = -1;
    other.readFd = -1;
    other.processGroup = false;
#endif
    return *this;
  }
};

// ---------------------------------------------------------------------------
// StdioMode — what to do with each standard file descriptor in the child
// ---------------------------------------------------------------------------
enum class StdioMode {
  Inherit,    // child inherits parent fd (default for stdin)
  Null,       // redirect to /dev/null (NUL on Windows)
  Pipe,       // create a pipe; parent gets the read end (stdout only)
  Merge,      // merge stderr into stdout (stderr only)
};

// ---------------------------------------------------------------------------
// SpawnOptions — portable configuration for subprocess launch
// ---------------------------------------------------------------------------
struct SpawnOptions {
  StdioMode stdinMode  = StdioMode::Null;
  StdioMode stdoutMode = StdioMode::Pipe;
  StdioMode stderrMode = StdioMode::Null;

  // If true, child is placed in its own process group / session so it
  // survives if the parent dies (and can be killed as a group).
  bool detached = false;

  // ---------------------------------------------------------------------------
  // Convenience factory helpers — named presets matching old call patterns
  // ---------------------------------------------------------------------------

  // stdout piped to parent, stderr silenced (old spawnPipeProcess)
  static SpawnOptions pipedStdout() {
    SpawnOptions o;
    o.stdinMode  = StdioMode::Null;
    o.stdoutMode = StdioMode::Pipe;
    o.stderrMode = StdioMode::Null;
    return o;
  }

  // Fully detached, all stdio silenced (old spawnDetachedProcess)
  static SpawnOptions detachedSilent() {
    SpawnOptions o;
    o.stdinMode  = StdioMode::Null;
    o.stdoutMode = StdioMode::Null;
    o.stderrMode = StdioMode::Null;
    o.detached   = true;
    return o;
  }

  // Capture both stdout+stderr merged (old readAllText behavior)
  static SpawnOptions captureAll() {
    SpawnOptions o;
    o.stdinMode  = StdioMode::Null;
    o.stdoutMode = StdioMode::Pipe;
    o.stderrMode = StdioMode::Merge;
    return o;
  }
};

// ---------------------------------------------------------------------------
// Core subprocess API
// ---------------------------------------------------------------------------

// Spawn a subprocess with the given options.
// On success the ChildProcess is populated (pid, readFd if piped).
// Returns false on failure or on platforms where not yet implemented.
bool spawnProcess(ChildProcess& process,
                  const std::vector<std::string>& args,
                  const SpawnOptions& options = SpawnOptions::pipedStdout());

// Execute command, capture all stdout+stderr, wait for exit.
// Returns std::nullopt on failure or non-zero exit code.
std::optional<std::string> readAllText(const std::vector<std::string>& args);

// ---------------------------------------------------------------------------
// Legacy convenience wrappers — thin forwards to spawnProcess()
// ---------------------------------------------------------------------------

// Spawn with stdout piped (equivalent to SpawnOptions::pipedStdout()).
bool spawnPipeProcess(ChildProcess& process,
                      const std::vector<std::string>& args);

// Spawn fully detached with all stdio silenced
// (equivalent to SpawnOptions::detachedSilent()).
bool spawnDetachedProcess(ChildProcess& process,
                          const std::vector<std::string>& args);
