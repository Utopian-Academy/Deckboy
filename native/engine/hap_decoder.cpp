// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "engine/hap_decoder.hpp"

#include <cstring>

namespace deckboy {
namespace hap {
namespace {

// ---------------------------------------------------------------------------
// Snappy raw-block decompression.
//
// The format is a varint of the uncompressed length, then a run of elements.
// Each element starts with a tag byte whose low two bits select a literal or
// one of three copy encodings. Copies may overlap their own output (that is how
// repeated runs are encoded), so they must be copied byte by byte.
//
// Everything here parses file data, so every read is bounds-checked: a corrupt
// or hostile frame must fail cleanly, never walk off the buffer.
// ---------------------------------------------------------------------------

bool readVarint(const std::uint8_t* src, std::size_t size, std::size_t& pos,
                std::uint64_t& value) {
  value = 0;
  int shift = 0;
  while (pos < size) {
    const std::uint8_t byte = src[pos++];
    if (shift > 63) {
      return false;                       // overlong: not a valid length
    }
    value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) {
      return true;
    }
    shift += 7;
  }
  return false;                           // ran out of input mid-varint
}

}  // namespace

bool snappyUncompress(const std::uint8_t* src, std::size_t srcSize,
                      std::vector<std::uint8_t>& out, std::string& error) {
  std::size_t pos = 0;
  std::uint64_t uncompressedLength = 0;
  if (!readVarint(src, srcSize, pos, uncompressedLength)) {
    error = "snappy: truncated length preamble";
    return false;
  }
  // Sanity bound: a HAP frame is block data, so under a byte per pixel. 512 MB
  // is far past any real raster and stops a corrupt varint asking for 16 EB.
  if (uncompressedLength > (512u << 20)) {
    error = "snappy: implausible uncompressed length";
    return false;
  }

  out.clear();
  out.reserve(static_cast<std::size_t>(uncompressedLength));

  while (pos < srcSize) {
    const std::uint8_t tag = src[pos++];
    if ((tag & 0x03) == 0x00) {           // literal
      std::size_t length = static_cast<std::size_t>(tag >> 2);
      if (length >= 60) {
        const std::size_t extraBytes = length - 59;   // 1..4
        if (pos + extraBytes > srcSize) {
          error = "snappy: truncated literal length";
          return false;
        }
        std::size_t parsed = 0;
        for (std::size_t i = 0; i < extraBytes; ++i) {
          parsed |= static_cast<std::size_t>(src[pos + i]) << (8 * i);
        }
        pos += extraBytes;
        length = parsed;
      }
      ++length;                           // stored value is length - 1
      if (pos + length > srcSize) {
        error = "snappy: literal runs past end of input";
        return false;
      }
      out.insert(out.end(), src + pos, src + pos + length);
      pos += length;
    } else {                              // one of the three copy forms
      std::size_t length = 0;
      std::size_t offset = 0;
      if ((tag & 0x03) == 0x01) {
        if (pos >= srcSize) {
          error = "snappy: truncated copy";
          return false;
        }
        length = 4 + ((tag >> 2) & 0x07);
        offset = (static_cast<std::size_t>(tag >> 5) << 8) | src[pos++];
      } else {
        const std::size_t offsetBytes = (tag & 0x03) == 0x02 ? 2 : 4;
        if (pos + offsetBytes > srcSize) {
          error = "snappy: truncated copy offset";
          return false;
        }
        for (std::size_t i = 0; i < offsetBytes; ++i) {
          offset |= static_cast<std::size_t>(src[pos + i]) << (8 * i);
        }
        pos += offsetBytes;
        length = static_cast<std::size_t>(tag >> 2) + 1;
      }
      if (offset == 0 || offset > out.size()) {
        error = "snappy: copy offset outside output";
        return false;
      }
      // Byte at a time on purpose: an offset smaller than the length is legal
      // and is how runs are encoded, so the copy reads bytes it is writing.
      const std::size_t start = out.size() - offset;
      for (std::size_t i = 0; i < length; ++i) {
        out.push_back(out[start + i]);
      }
    }
    if (out.size() > uncompressedLength) {
      error = "snappy: output overran declared length";
      return false;
    }
  }

  if (out.size() != uncompressedLength) {
    error = "snappy: output short of declared length";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// HAP container.
// ---------------------------------------------------------------------------

int blockBytes(TextureFormat format) {
  switch (format) {
    case TextureFormat::RgbDxt1:
    case TextureFormat::AlphaRgtc1:
      return 8;
    case TextureFormat::RgbaDxt5:
    case TextureFormat::YCoCgDxt5:
      return 16;
    default:
      return 0;
  }
}

const char* textureFormatName(TextureFormat format) {
  switch (format) {
    case TextureFormat::RgbDxt1:    return "RGB_DXT1";
    case TextureFormat::RgbaDxt5:   return "RGBA_DXT5";
    case TextureFormat::YCoCgDxt5:  return "YCoCg_DXT5";
    case TextureFormat::AlphaRgtc1: return "A_RGTC1";
    default:                        return "unknown";
  }
}

namespace {

// Section type byte: low nibble is the texture format, high nibble is the
// second-stage compressor.
constexpr std::uint8_t kFormatRgbDxt1     = 0x0B;
constexpr std::uint8_t kFormatRgbaDxt5    = 0x0E;
constexpr std::uint8_t kFormatYCoCgDxt5   = 0x0F;
constexpr std::uint8_t kFormatAlphaRgtc1  = 0x0C;

constexpr std::uint8_t kCompressorNone    = 0xA0;
constexpr std::uint8_t kCompressorSnappy  = 0xB0;
constexpr std::uint8_t kCompressorComplex = 0xC0;

// Sections inside a complex frame decode-instructions container.
constexpr std::uint8_t kSectionDecodeInstructions = 0x01;
constexpr std::uint8_t kSectionCompressorTable    = 0x02;
constexpr std::uint8_t kSectionSizeTable          = 0x03;

TextureFormat formatFromNibble(std::uint8_t low) {
  switch (low) {
    case kFormatRgbDxt1:    return TextureFormat::RgbDxt1;
    case kFormatRgbaDxt5:   return TextureFormat::RgbaDxt5;
    case kFormatYCoCgDxt5:  return TextureFormat::YCoCgDxt5;
    case kFormatAlphaRgtc1: return TextureFormat::AlphaRgtc1;
    default:                return TextureFormat::Unknown;
  }
}

// A section header is 3 bytes of little-endian length plus a type byte. A zero
// length means the extended form: the real 32-bit length follows the type.
struct SectionHeader {
  std::uint8_t type = 0;
  std::size_t bodySize = 0;
  std::size_t headerSize = 0;
};

bool readSectionHeader(const std::uint8_t* src, std::size_t size,
                       std::size_t pos, SectionHeader& out) {
  if (pos + 4 > size) {
    return false;
  }
  const std::size_t shortSize = static_cast<std::size_t>(src[pos]) |
                                (static_cast<std::size_t>(src[pos + 1]) << 8) |
                                (static_cast<std::size_t>(src[pos + 2]) << 16);
  out.type = src[pos + 3];
  if (shortSize != 0) {
    out.bodySize = shortSize;
    out.headerSize = 4;
    return true;
  }
  if (pos + 8 > size) {
    return false;
  }
  out.bodySize = static_cast<std::size_t>(src[pos + 4]) |
                 (static_cast<std::size_t>(src[pos + 5]) << 8) |
                 (static_cast<std::size_t>(src[pos + 6]) << 16) |
                 (static_cast<std::size_t>(src[pos + 7]) << 24);
  out.headerSize = 8;
  return true;
}

// Complex frames split the texture into chunks so a decoder can decompress them
// in parallel. The decode-instructions container carries a compressor byte and
// a size per chunk; the chunk payloads follow it in order. Correctness does not
// depend on threading them, so they are decoded in sequence here.
bool decodeComplexFrame(const std::uint8_t* body, std::size_t bodySize,
                        std::vector<std::uint8_t>& out, std::string& error) {
  SectionHeader instructions {};
  if (!readSectionHeader(body, bodySize, 0, instructions) ||
      instructions.type != kSectionDecodeInstructions ||
      instructions.headerSize + instructions.bodySize > bodySize) {
    error = "hap: missing decode instructions";
    return false;
  }

  std::vector<std::uint8_t> compressors;
  std::vector<std::size_t> sizes;
  std::size_t pos = instructions.headerSize;
  const std::size_t instructionsEnd = pos + instructions.bodySize;

  while (pos < instructionsEnd) {
    SectionHeader sub {};
    if (!readSectionHeader(body, instructionsEnd, pos, sub) ||
        pos + sub.headerSize + sub.bodySize > instructionsEnd) {
      error = "hap: malformed decode instructions";
      return false;
    }
    const std::uint8_t* data = body + pos + sub.headerSize;
    if (sub.type == kSectionCompressorTable) {
      compressors.assign(data, data + sub.bodySize);
    } else if (sub.type == kSectionSizeTable) {
      const std::size_t count = sub.bodySize / 4;
      sizes.resize(count);
      for (std::size_t i = 0; i < count; ++i) {
        sizes[i] = static_cast<std::size_t>(data[i * 4]) |
                   (static_cast<std::size_t>(data[i * 4 + 1]) << 8) |
                   (static_cast<std::size_t>(data[i * 4 + 2]) << 16) |
                   (static_cast<std::size_t>(data[i * 4 + 3]) << 24);
      }
    }
    // An offset table (0x04) may also be present; decoding in order makes it
    // redundant, so it is skipped.
    pos += sub.headerSize + sub.bodySize;
  }

  if (sizes.empty() || compressors.size() != sizes.size()) {
    error = "hap: chunk tables missing or mismatched";
    return false;
  }

  out.clear();
  std::size_t chunkPos = instructionsEnd;
  std::vector<std::uint8_t> scratch;
  for (std::size_t i = 0; i < sizes.size(); ++i) {
    if (chunkPos + sizes[i] > bodySize) {
      error = "hap: chunk runs past end of frame";
      return false;
    }
    const std::uint8_t* chunk = body + chunkPos;
    // Writers differ on whether the table stores the bare nibble or the
    // high-nibble constant; accept both rather than silently mis-decoding.
    const bool snappy = compressors[i] == kCompressorSnappy ||
                        compressors[i] == (kCompressorSnappy >> 4);
    if (snappy) {
      if (!snappyUncompress(chunk, sizes[i], scratch, error)) {
        return false;
      }
      out.insert(out.end(), scratch.begin(), scratch.end());
    } else {
      out.insert(out.end(), chunk, chunk + sizes[i]);
    }
    chunkPos += sizes[i];
  }
  return true;
}

}  // namespace

bool decodeFrame(const std::uint8_t* packet, std::size_t size, Frame& out,
                 std::string& error) {
  SectionHeader header {};
  if (!readSectionHeader(packet, size, 0, header)) {
    error = "hap: truncated frame header";
    return false;
  }
  if (header.headerSize + header.bodySize > size) {
    error = "hap: frame body runs past packet end";
    return false;
  }

  out.format = formatFromNibble(header.type & 0x0F);
  if (out.format == TextureFormat::Unknown) {
    error = "hap: unsupported texture format";
    return false;
  }

  const std::uint8_t* body = packet + header.headerSize;
  const std::size_t bodySize = header.bodySize;
  switch (header.type & 0xF0) {
    case kCompressorNone:
      out.data.assign(body, body + bodySize);
      return true;
    case kCompressorSnappy:
      return snappyUncompress(body, bodySize, out.data, error);
    case kCompressorComplex:
      return decodeComplexFrame(body, bodySize, out.data, error);
    default:
      error = "hap: unsupported second-stage compressor";
      return false;
  }
}

}  // namespace hap
}  // namespace deckboy
