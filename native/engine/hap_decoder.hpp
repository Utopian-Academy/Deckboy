// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// hap_decoder.hpp — HAP frame decoding to GPU-ready block-compressed texels.
//
// HAP stores frames as DXT/BC blocks — the format a GPU samples natively — so
// "decoding" is a container parse plus a Snappy decompression, and the result
// uploads straight to a compressed texture. No YUV->RGB, no per-frame scale.
//
// This deliberately does NOT use ffmpeg's `hap` decoder: that decompresses DXT
// to RGB on the CPU, which is slower than H.264 for 5-10x the file size and
// throws away the entire point of the format. See docs/HAP_PLAYBACK_PLAN.md.
//
// Snappy is implemented here rather than linked: decompression is a small,
// frozen format, and this repo's CI has a long history of breaking on
// per-platform dependencies. Nothing to install on Windows, Linux or macOS.
//
// Format reference: https://github.com/Vidvox/hap/blob/master/documentation/
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deckboy {
namespace hap {

// Texture layout of a decoded frame. These map 1:1 onto GPU formats:
// DXT1 -> DXGI_FORMAT_BC1_UNORM / GL_COMPRESSED_RGB_S3TC_DXT1_EXT
// DXT5 -> DXGI_FORMAT_BC3_UNORM / GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
enum class TextureFormat {
  Unknown,
  RgbDxt1,        // Hap
  RgbaDxt5,       // Hap Alpha
  YCoCgDxt5,      // Hap Q — needs a shader to convert YCoCg->RGB after sampling
  AlphaRgtc1,     // Hap Alpha Only (single channel)
};

// Bytes per 4x4 block. DXT1/RGTC1 are 8, DXT5 is 16.
int blockBytes(TextureFormat format);

const char* textureFormatName(TextureFormat format);

struct Frame {
  TextureFormat format = TextureFormat::Unknown;
  std::vector<std::uint8_t> data;   // raw block data, ready for GPU upload
};

// Decode one HAP frame (the contents of a single video packet).
//
// Handles all three second-stage compressors the format defines: none, Snappy,
// and "complex" (the chunked form, where a decode-instructions container gives
// a per-chunk compressor and size table). Chunked frames exist so a decoder can
// spread work across threads; correctness does not depend on doing so, and this
// decodes them in order.
//
// Returns false and leaves `error` set when the packet is not valid HAP.
bool decodeFrame(const std::uint8_t* packet, std::size_t size, Frame& out,
                 std::string& error);

// Expand block-compressed texels to 8-bit RGBA, row-major, `width`*`height`*4.
//
// This is the CPU path. It exists because SDL3 has no pixel format for BC data
// -- SDL_CreateTexture cannot express BC1/BC3 and SDL_UpdateTexture has no
// notion of block pitch -- so blocks cannot simply be handed to the renderer.
// DXT decompression itself is cheap (a few ms at 1080p, far below H.264
// decode), and it already buys HAP's all-intra instant seeking. What it does
// NOT buy is the many-layers-at-no-CPU win, which needs the blocks uploaded as
// a compressed GPU texture; see docs/HAP_PLAYBACK_PLAN.md.
//
// `out` is resized to width*height*4. Returns false if the data is too small
// for the stated raster.
bool decompressToRgba(const Frame& frame, int width, int height,
                      std::vector<std::uint8_t>& out, std::string& error);

// Raw Snappy block decompression (not the framing format). Exposed for tests.
// `expectedSize` of 0 means "trust the stream's own preamble".
bool snappyUncompress(const std::uint8_t* src, std::size_t srcSize,
                      std::vector<std::uint8_t>& out, std::string& error);

}  // namespace hap
}  // namespace deckboy
