// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <cstddef>
#include <cstdint>

#ifndef _WIN32
#include <unistd.h>
#endif

inline bool readExact(int fd, std::uint8_t* data, size_t size) {
#ifdef _WIN32
  (void) fd;
  (void) data;
  (void) size;
  return false;
#else
  size_t offset = 0;
  while (offset < size) {
    ssize_t bytes = read(fd, data + offset, size - offset);
    if (bytes <= 0) {
      return false;
    }
    offset += static_cast<size_t>(bytes);
  }
  return true;
#endif
}
