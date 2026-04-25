// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// io_utils.hpp — Low-level pipe I/O for the media decode pipeline.
//
// These inline functions wrap POSIX read() / Windows _read() to provide
// portable byte-level reading from CRT file descriptors. They are the
// bridge between subprocess pipes (subprocess.*) and MediaEngine's decode
// threads (media_engine.cpp).
//
// readExact: used by the video decode thread to read one complete frame
//            (width*height*4 bytes of raw RGBA) from ffmpeg's stdout pipe.
//            Blocks until all bytes arrive, or returns false on EOF/error.
//
// readSome:  used by the audio decode thread to read whatever audio samples
//            are currently available from ffmpeg's audio stdout pipe.
//            Non-blocking in the sense that it returns after one read() call.
//
// Windows note: _read() on Windows has a per-call limit of ~65KB for pipe
// handles, so we cap each individual read to 65536 bytes and loop in
// readExact. readSome returns after a single capped read.
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <io.h>       // _read()
#else
#include <unistd.h>   // read()
#endif

// ---------------------------------------------------------------------------
// readExact — Read exactly `size` bytes from a CRT file descriptor.
//
// Loops until all `size` bytes have been read into `data`, handling partial
// reads (which are normal for pipes). Returns false immediately on EOF
// (bytes == 0) or error (bytes < 0), which signals the ffmpeg process has
// exited or the pipe was closed.
//
// Called by: MediaEngine::startDecoderThreads() video decode loop
// ---------------------------------------------------------------------------
inline bool readExact(int fd, std::uint8_t* data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
#ifdef _WIN32
    // Windows _read() can't handle more than ~65KB per call on pipes
    unsigned int chunk = static_cast<unsigned int>(size - offset < 65536u ? size - offset : 65536u);
    int bytes = _read(fd, data + offset, chunk);
#else
    // POSIX read() handles arbitrary sizes; kernel returns what's available
    ssize_t bytes = read(fd, data + offset, size - offset);
#endif
    if (bytes <= 0) {
      return false;  // EOF (0) or error (negative) — decode is done
    }
    offset += static_cast<size_t>(bytes);
  }
  return true;  // all `size` bytes successfully read
}

// ---------------------------------------------------------------------------
// readSome — Read up to `size` bytes from a CRT file descriptor.
//
// Returns after a single read() call with however many bytes were available.
// Return value: positive = bytes read, 0 = EOF, negative = error.
//
// Called by: MediaEngine audio decode loop (reads PCM samples as they arrive)
// ---------------------------------------------------------------------------
inline int readSome(int fd, void* data, size_t size) {
#ifdef _WIN32
  // Cap to 65KB per call for Windows pipe compatibility
  unsigned int chunk = static_cast<unsigned int>(size < 65536u ? size : 65536u);
  return _read(fd, data, chunk);
#else
  return static_cast<int>(read(fd, data, size));
#endif
}
