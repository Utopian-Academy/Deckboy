// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#ifndef _WIN32

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

class SingleInstanceGuard {
 public:
  bool acquire(const std::filesystem::path& path) {
    if (locked_) {
      return true;
    }

    fd_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd_ < 0) {
      lastError_ = std::string("open failed: ") + std::strerror(errno);
      return false;
    }
    int fdFlags = fcntl(fd_, F_GETFD, 0);
    if (fdFlags >= 0) {
      fcntl(fd_, F_SETFD, fdFlags | FD_CLOEXEC);
    }

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

    std::string pidText = std::to_string(static_cast<long long>(::getpid())) + "\n";
    if (::ftruncate(fd_, 0) != 0) {
      // best-effort; stale content just means a later reader sees an old pid
    }
    ssize_t written = ::write(fd_, pidText.c_str(), pidText.size());
    (void) written;
    locked_ = true;
    return true;
  }

  const std::string& lastError() const {
    return lastError_;
  }

  ~SingleInstanceGuard() {
    if (fd_ >= 0) {
      if (locked_) {
        (void) ::flock(fd_, LOCK_UN);
      }
      ::close(fd_);
      fd_ = -1;
    }
  }

 private:
  std::string lastError_;
  int fd_ = -1;
  bool locked_ = false;
};

#else // _WIN32

#include <filesystem>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

class SingleInstanceGuard {
 public:
  // On Windows the path argument is converted to a Global\\ named mutex so
  // that only one Deckboy instance can run per desktop session regardless of
  // the working directory.  The path's filename (minus extension) is used as
  // the mutex name to keep it short and deterministic.
  bool acquire(const std::filesystem::path& path) {
    if (locked_) {
      return true;
    }

    // Build a mutex name from the lock file stem, e.g. "Global\\deckboy"
    std::wstring mutexName = L"Global\\deckboy_";
    mutexName += path.stem().wstring();

    mutex_ = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!mutex_) {
      lastError_ = "CreateMutexW failed (error " +
                   std::to_string(GetLastError()) + ")";
      return false;
    }

    DWORD waitResult = WaitForSingleObject(mutex_, 0);
    if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED) {
      locked_ = true;
      return true;
    }

    // WAIT_TIMEOUT means another instance already holds the mutex.
    lastError_ = "another Deckboy instance is already running";
    CloseHandle(mutex_);
    mutex_ = nullptr;
    return false;
  }

  const std::string& lastError() const {
    return lastError_;
  }

  ~SingleInstanceGuard() {
    if (mutex_) {
      if (locked_) {
        ReleaseMutex(mutex_);
      }
      CloseHandle(mutex_);
      mutex_ = nullptr;
    }
  }

 private:
  std::string lastError_;
  HANDLE mutex_ = nullptr;
  bool locked_ = false;
};

#endif
