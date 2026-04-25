// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// single_instance_guard.hpp — Prevent multiple Deckboy instances from running.
//
// In a live event environment, running two instances simultaneously would
// cause conflicts over output windows, audio devices, and network ports
// (OSC, Companion, tally). This guard ensures only one instance runs.
//
// Platform implementations:
//   Linux/macOS: flock() on a lock file in the data directory. The file
//                contains the PID of the owning process for debugging.
//   Windows:     Named mutex (Global\deckboy_<name>) — works across all
//                desktop sessions regardless of working directory.
//
// Usage in main.cpp:
//   SingleInstanceGuard guard;
//   if (!guard.acquire(lockFilePath)) {
//     // show error: guard.lastError()
//     return 1;
//   }
//   // guard automatically releases in destructor
// ============================================================================

#pragma once

#ifndef _WIN32
// ============================================================================
// POSIX implementation (Linux, macOS, FreeBSD, OpenBSD)
// Uses flock() for advisory file locking — simple and reliable.
// ============================================================================

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

class SingleInstanceGuard {
 public:
  /// Try to acquire the instance lock. Returns true if this is the only
  /// running instance, false if another instance holds the lock.
  bool acquire(const std::filesystem::path& path) {
    if (locked_) {
      return true;  // already acquired (idempotent)
    }

    // Open (or create) the lock file with read/write access.
    // Mode 0666 lets any user on the system acquire it (umask applies).
    fd_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd_ < 0) {
      lastError_ = std::string("open failed: ") + std::strerror(errno);
      return false;
    }
    // Set close-on-exec so child processes (ffmpeg, etc.) don't inherit the lock fd
    int fdFlags = fcntl(fd_, F_GETFD, 0);
    if (fdFlags >= 0) {
      fcntl(fd_, F_SETFD, fdFlags | FD_CLOEXEC);
    }

    // Try to acquire an exclusive lock without blocking (LOCK_NB).
    // If another instance holds it, we get EWOULDBLOCK immediately.
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        lastError_ = "another Deckboy instance is already running";
      } else {
        lastError_ = std::string("lock failed: ") + std::strerror(errno);
      }
      ::close(fd_);
      fd_ = -1;
      return false;
    }

    // Write our PID to the lock file for debugging (e.g. "who's holding the lock?")
    std::string pidText = std::to_string(static_cast<long long>(::getpid())) + "\n";
    if (::ftruncate(fd_, 0) != 0) {
      // best-effort; stale content just means a later reader sees an old pid
    }
    ssize_t written = ::write(fd_, pidText.c_str(), pidText.size());
    (void) written;  // ignore write errors — the lock itself is what matters
    locked_ = true;
    return true;
  }

  /// Human-readable error message from the last failed acquire() call.
  const std::string& lastError() const {
    return lastError_;
  }

  /// Destructor releases the lock and closes the file descriptor.
  /// The lock is automatically released if the process crashes (OS cleans up).
  ~SingleInstanceGuard() {
    if (fd_ >= 0) {
      if (locked_) {
        (void) ::flock(fd_, LOCK_UN);  // explicitly unlock before close
      }
      ::close(fd_);
      fd_ = -1;
    }
  }

 private:
  std::string lastError_;   // error message from last failed acquire()
  int fd_ = -1;             // lock file descriptor (-1 = not open)
  bool locked_ = false;     // true if we hold the exclusive lock
};

#else // _WIN32
// ============================================================================
// Windows implementation
// Uses a named mutex (Global\ namespace) for cross-session exclusivity.
// The path argument is only used for the mutex name — no file is created.
// ============================================================================

#include <filesystem>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

class SingleInstanceGuard {
 public:
  /// Try to acquire the instance lock via a named mutex.
  /// The path's filename stem becomes part of the mutex name (e.g. "Global\deckboy_deckboy").
  /// This ensures only one Deckboy runs per Windows session regardless of cwd.
  bool acquire(const std::filesystem::path& path) {
    if (locked_) {
      return true;  // already acquired (idempotent)
    }

    // Build a Global\ mutex name from the lock file stem.
    // "Global\" prefix makes it visible across all desktop sessions.
    std::wstring mutexName = L"Global\\deckboy_";
    mutexName += path.stem().wstring();

    // CreateMutexW: creates the mutex if it doesn't exist, or opens it if it does.
    // FALSE = don't request initial ownership (we'll do that with WaitForSingleObject).
    mutex_ = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!mutex_) {
      lastError_ = "CreateMutexW failed (error " +
                   std::to_string(GetLastError()) + ")";
      return false;
    }

    // Try to acquire ownership with a zero timeout (don't block).
    // WAIT_OBJECT_0 = we got it. WAIT_ABANDONED = previous owner crashed (we still get it).
    DWORD waitResult = WaitForSingleObject(mutex_, 0);
    if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED) {
      locked_ = true;
      return true;
    }

    // WAIT_TIMEOUT = another instance already holds the mutex.
    lastError_ = "another Deckboy instance is already running";
    CloseHandle(mutex_);
    mutex_ = nullptr;
    return false;
  }

  /// Human-readable error message from the last failed acquire() call.
  const std::string& lastError() const {
    return lastError_;
  }

  /// Destructor releases the mutex. If the process crashes, Windows
  /// automatically releases the mutex (next acquirer gets WAIT_ABANDONED).
  ~SingleInstanceGuard() {
    if (mutex_) {
      if (locked_) {
        ReleaseMutex(mutex_);  // release ownership before closing handle
      }
      CloseHandle(mutex_);
      mutex_ = nullptr;
    }
  }

 private:
  std::string lastError_;    // error message from last failed acquire()
  HANDLE mutex_ = nullptr;  // Win32 mutex handle (nullptr = not created)
  bool locked_ = false;     // true if we hold the mutex
};

#endif
