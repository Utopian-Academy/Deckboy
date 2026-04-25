/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Deckboy — Cross-platform subprocess management
 * Copyright 2025 James
 */

// ============================================================================
// subprocess.hpp — Cross-platform subprocess spawning and pipe management.
//
// This is the foundation for all external process interaction in Deckboy:
//   - ffmpeg video decode (piped raw RGBA frames to stdout)
//   - ffmpeg audio decode (piped raw PCM samples to stdout)
//   - ffprobe metadata extraction (capture stdout text output)
//   - ffmpeg stream egress (detached, no pipe needed)
//
// Architecture:
//   ChildProcess  — RAII handle to a running subprocess + its pipe fd.
//                   Move-only (no copy). Destructor calls stop() to kill
//                   the child and close the pipe.
//   SpawnOptions  — Configuration for stdio redirection (pipe/null/inherit/merge).
//   spawnProcess  — The core spawn function (CreateProcessW on Windows, fork+exec on POSIX).
//   readAllText   — Convenience: spawn, capture all output, wait for exit.
//
// Threading model:
//   MediaEngine spawns ffmpeg as a ChildProcess with piped stdout.
//   A dedicated decode thread reads from readFd using readExact()/readSome()
//   (from io_utils.hpp). When playback stops, killProcessOnly() is called
//   first to unblock the reader thread (EOF on pipe), then stop() is called
//   after the thread joins to clean up the handle.
//
// Implementation: subprocess.cpp
// ============================================================================

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
// ChildProcess — RAII wrapper for a running subprocess and its pipe.
//
// Owns the process handle (HANDLE on Windows, pid_t on POSIX) and an
// optional CRT file descriptor for reading the child's stdout.
// Move-only: moving transfers ownership; the source is left in a
// "not running" state. Destructor calls stop() to kill the child.
// ---------------------------------------------------------------------------
struct ChildProcess {
#ifdef _WIN32
  HANDLE hProcess = INVALID_HANDLE_VALUE;  // Win32 process handle
  int readFd = -1;   // CRT fd from _open_osfhandle (for _read); -1 if no pipe
  int writeFd = -1;  // CRT fd for writing to child's stdin; -1 if no pipe
#else
  pid_t pid = -1;          // POSIX child process ID (-1 = not running)
  int readFd = -1;         // pipe read end fd (-1 = no pipe)
  int writeFd = -1;        // pipe write end fd for stdin (-1 = no pipe)
  bool processGroup = false; // true if child is in its own process group (for group kill)
#endif

  /// Returns true if the process handle is valid (child was spawned and not yet stopped).
  bool running() const;

  /// Kill the child process, close the pipe, and reset all handles.
  /// Safe to call multiple times (idempotent). Called by destructor.
  void stop();

  /// Kill the child process only (does NOT close readFd).
  /// This is specifically designed for the decode thread shutdown sequence:
  ///   1. Call killProcessOnly() — kills ffmpeg, closing its pipe write end
  ///   2. The reader thread's _read()/read() returns 0 (EOF) and exits
  ///   3. Join the reader thread
  ///   4. Call stop() to close readFd and clean up handles
  /// Without this two-phase approach, closing readFd while a thread is
  /// blocked on _read() causes EBADF on Windows instead of a clean EOF.
  void killProcessOnly();

  /// Destructor — calls stop() to ensure the child is killed and handles are closed.
  ~ChildProcess();

  ChildProcess() = default;
  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;
  ChildProcess(ChildProcess&& other) noexcept
#ifdef _WIN32
    : hProcess(other.hProcess), readFd(other.readFd), writeFd(other.writeFd)
#else
    : pid(other.pid), readFd(other.readFd), writeFd(other.writeFd), processGroup(other.processGroup)
#endif
  {
#ifdef _WIN32
    other.hProcess = INVALID_HANDLE_VALUE;
    other.readFd = -1;
    other.writeFd = -1;
#else
    other.pid = -1;
    other.readFd = -1;
    other.writeFd = -1;
    other.processGroup = false;
#endif
  }
  ChildProcess& operator=(ChildProcess&& other) noexcept {
    stop();
#ifdef _WIN32
    hProcess = other.hProcess;
    readFd = other.readFd;
    writeFd = other.writeFd;
    other.hProcess = INVALID_HANDLE_VALUE;
    other.readFd = -1;
    other.writeFd = -1;
#else
    pid = other.pid;
    readFd = other.readFd;
    writeFd = other.writeFd;
    processGroup = other.processGroup;
    other.pid = -1;
    other.readFd = -1;
    other.writeFd = -1;
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

  // stdin piped from parent, stdout+stderr silenced (for stream egress)
  static SpawnOptions pipedStdin() {
    SpawnOptions o;
    o.stdinMode  = StdioMode::Pipe;
    o.stdoutMode = StdioMode::Null;
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
