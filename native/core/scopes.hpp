// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Signal analysis only: no SDL, playback state, colour management or drawing.
// See docs/SCOPES_CORE.md for standards, coordinate and measurement contracts.
namespace deckboy::core::scopes {

enum class Matrix { Bt709, Bt2020Ncl };
enum class Components { Rgb, YCbCr };
enum class Encoding { Normalized, NarrowCode, FullCodeRgb };
enum class GamutFilter { None, Progressive, Interlaced };

struct Rgb { double r = 0, g = 0, b = 0; };
struct YCbCr { double y = 0, cb = 0, cr = 0; };
struct Sample { double a = 0, b = 0, c = 0; };

// R'G'B' and Y' nominally 0..1, signed Cb/Cr nominally -0.5..0.5.
// Values outside those ranges are preserved. No OETF/EOTF is applied.
YCbCr toYCbCr(Rgb rgb, Matrix matrix);
Rgb toRgb(YCbCr ycc, Matrix matrix);

struct FrameView {
  std::span<const Sample> samples;
  std::size_t width = 0, height = 0;
  std::size_t rowStride = 0; // in Samples; zero means width
  Components components = Components::Rgb;
  Encoding encoding = Encoding::Normalized;
  unsigned bitDepth = 10; // digital input only; 8, 10, 12 or 16
};

// Straight/full-range R'G'B'A byte order, alpha ignored. Supply composited
// pixels; this core neither unpremultiplies nor guesses a background.
struct Rgba8View {
  std::span<const std::uint8_t> bytes;
  std::size_t width = 0, height = 0;
  std::size_t rowStrideBytes = 0; // zero means width * 4
  bool bgra = false;               // B, G, R, A byte order (the output capture's)
};

// The BGRA output frame as SDI and ST 2110 SEND it: BT.709 narrow-range
// Y'CbCr 4:2:2 from core/ycbcr422.hpp (the outputs' own encoder), 8- or
// 10-bit, then rebuilt the way a scope downstream sees it -- each pixel's own
// Y' with the Cb Cr its pair shares. That shared chroma is where a saturated
// edge overshoots R'G'B' and EBU R 103 finds something; the 8-bit R'G'B'
// frame itself cannot leave gamut. The matrix is always BT.709 (what is sent);
// Options::matrix does not apply.
struct Bgra422View {
  std::span<const std::uint8_t> bytes;
  std::size_t width = 0, height = 0;
  std::size_t rowStrideBytes = 0; // zero means width * 4
  unsigned bitDepth = 10;         // 8 or 10, as the output sends
};

// An HDR frame as 16-bit R'G'B' (three little-endian uint16 per pixel, no
// alpha): the PQ BT.2020 signal core/hdr_to_sdr.hpp's PqEncoder makes from
// the float compositor, so 0..65535 is the whole PQ range, 0 to 10,000 nits.
// Read as normalised code values; pair it with Matrix::Bt2020Ncl.
struct Rgb16View {
  std::span<const std::uint8_t> bytes;
  std::size_t width = 0, height = 0;
  std::size_t rowStrideBytes = 0; // zero means width * 6
};

// PQ (SMPTE ST 2084): a normalised code value and the luminance it means.
double pqToNits(double code);
double nitsToPq(double nits);

struct Range { double low = 0, high = 1; };
struct Options {
  Matrix matrix = Matrix::Bt709;
  std::size_t columns = 256, levelBins = 256;
  std::size_t vectorBins = 256, histogramBins = 256;
  Range levels{0, 1}, chroma{-0.5, 0.5};
  unsigned gamutBitDepth = 10; // R 103 total-range ceiling depends on depth
  GamutFilter gamutFilter = GamutFilter::Progressive;
};

struct Excursions { std::uint64_t below = 0, above = 0; };
struct Histogram {
  Range range;
  std::vector<std::uint64_t> bins;
  Excursions excursions;
};
struct Density {
  std::size_t width = 0, height = 0;
  Range xRange, yRange;
  // Row-major, low Y first (positive Cr upwards in mathematical coordinates).
  // Equal-width bins, low inclusive, high exclusive except final bin.
  // Outliers accumulate in edge bins AND in the excursion counters.
  std::vector<std::uint64_t> bins;
  Excursions xExcursions, yExcursions;
};

struct GamutRangeCounts {
  // Components are Y', R', G', B'. A pixel counts once in pixels even when
  // several channels violate the range; per-channel counts can overlap.
  std::array<Excursions, 4> channels{};
  std::uint64_t pixels = 0;
};
struct GamutCounts {
  std::uint64_t validPixels = 0, invalidPixels = 0;
  GamutRangeCounts nominal, preferred, total;
  // Strictly MORE than 1% of the active image, not >= 1%. Invalid pixels
  // remain in the denominator and are separately reported, never called legal.
  bool outOfGamut() const;
};

struct Result {
  std::uint64_t validPixels = 0, invalidPixels = 0;
  std::array<Density, 3> ycbcrParade; // Y', Cb, Cr, each retains source X
  std::array<Density, 3> rgbParade;   // R', G', B'
  const Density& lumaWaveform() const { return ycbcrParade[0]; }
  Density vectorscope; // x=Cb, y=Cr; equal scaling, no analogue UV rotation
  // The diamond gamut display: [0] x = B'-G', y = B'+G' (the upper diamond);
  // [1] x = R'-G', y = R'+G' (the lower one, drawn inverted). A legal R'G'B'
  // pixel lands inside the square x in [-1, 1], y in [0, 2] rotated 45 degrees;
  // anything outside that diamond is a component below 0 or above 1.
  std::array<Density, 2> diamond;
  Histogram lumaHistogram;
  std::array<Histogram, 3> rgbHistogram;
  GamutCounts rawGamut, filteredGamut;
  GamutFilter gamutFilter = GamutFilter::None;
};

enum class TargetLevel { Percent75, Percent100 };
struct VectorPoint { double cb = 0, cr = 0; };
struct VectorTarget { const char* label; VectorPoint point; };
// R, Yl, G, Cy, B, Mg; amplitudes 75% or 100%, zero pedestal, no white bar.
std::array<VectorTarget, 6> vectorTargets(Matrix matrix, TargetLevel level);
// Traditional +I signal guide from BT.470-6, projected through the selected
// matrix and normalized to radius 0.5. This is NOT a skin classifier or a
// colourimetric conversion between NTSC, BT.709 and BT.2020 primaries.
VectorPoint skinToneLine(Matrix matrix);

// Input is the ACTIVE picture, already unpacked/resampled to 4:4:4. A subspan
// and row stride can select an ROI. No implicit downsampling or clipping.
// Empty 0x0 frames are allowed. Invalid metadata/options throw invalid_argument
// before reading pixels; allocation failure propagates. Non-finite samples are
// counted and excluded. All results own their memory; no global mutable state.
Result analyze(FrameView frame, const Options& options = {});
Result analyze(Rgba8View frame, const Options& options = {});
Result analyze(Bgra422View frame, const Options& options = {});
Result analyze(Rgb16View frame, const Options& options = {});

// The FILTERED R 103 count alone, for rows [rowBegin, rowEnd) of the whole
// frame (the filter reads the rows either side of the band). Bands summed with
// accumulate() equal the whole frame's filtered count exactly, so the costly
// filtered pass can run across the cores like the rest.
GamutCounts filteredGamutRows(Bgra422View frame, const Options& options,
                              std::size_t rowBegin, std::size_t rowEnd);
void accumulate(GamutCounts& whole, const GamutCounts& part);

// Adds a part analysed with the SAME options (a band of rows of one frame)
// into a whole. Every count and bin is a sum, so splitting a frame into row
// bands, analysing them in parallel and accumulating is exact -- except the
// FILTERED gamut, whose spatial filter needs the neighbouring rows: analyse
// bands with GamutFilter::None and run the filtered count on the whole frame.
// Throws invalid_argument when the shapes differ.
void accumulate(Result& whole, const Result& part);

} // namespace deckboy::core::scopes
