// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

// Read exactly `size` bytes from CRT fd `fd` into `data`.
// Returns false on EOF or error.
inline bool readExact(int fd, std::uint8_t* data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
#ifdef _WIN32
    unsigned int chunk = static_cast<unsigned int>(size - offset < 65536u ? size - offset : 65536u);
    int bytes = _read(fd, data + offset, chunk);
#else
    ssize_t bytes = read(fd, data + offset, size - offset);
#endif
    if (bytes <= 0) {
      return false;
    }
    offset += static_cast<size_t>(bytes);
  }
  return true;
}

// Read up to `size` bytes from CRT fd `fd` into `data`.
// Returns number of bytes read, 0 on EOF, or negative on error.
inline int readSome(int fd, void* data, size_t size) {
#ifdef _WIN32
  unsigned int chunk = static_cast<unsigned int>(size < 65536u ? size : 65536u);
  return _read(fd, data, chunk);
#else
  return static_cast<int>(read(fd, data, size));
#endif
}
