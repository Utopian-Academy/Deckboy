// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#include "scopes.hpp"

#include "ycbcr422.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace deckboy::core::scopes {
namespace {
struct Coefficients { double kr, kg, kb; };
Coefficients coefficients(Matrix matrix) {
  switch (matrix) {
    case Matrix::Bt709: return {0.2126, 0.7152, 0.0722};
    case Matrix::Bt2020Ncl: return {0.2627, 0.6780, 0.0593};
  }
  throw std::invalid_argument("unsupported scopes matrix");
}
bool supportedDepth(unsigned depth) {
  return depth == 8 || depth == 10 || depth == 12 || depth == 16;
}
YCbCr encode(Rgb rgb, Coefficients k) {
  // Difference form gives neutral greys exact zero chroma, including ramps.
  const double y = rgb.g + k.kr * (rgb.r - rgb.g) + k.kb * (rgb.b - rgb.g);
  return {y, (rgb.b - y) / (2 * (1 - k.kb)),
             (rgb.r - y) / (2 * (1 - k.kr))};
}
Rgb decode(YCbCr ycc, Coefficients k) {
  const double rd = 2 * (1 - k.kr) * ycc.cr;
  const double bd = 2 * (1 - k.kb) * ycc.cb;
  return {ycc.y + rd, ycc.y - (k.kr * rd + k.kb * bd) / k.kg, ycc.y + bd};
}
struct Pixel {
  Rgb rgb;
  YCbCr ycc;
};
using Channels = std::array<double, 4>;
Channels channels(Pixel pixel) {
  return {pixel.ycc.y, pixel.rgb.r, pixel.rgb.g, pixel.rgb.b};
}
bool finite(const Channels& values) {
  return std::all_of(values.begin(), values.end(), [](double v) { return std::isfinite(v); });
}
bool finite(Pixel pixel) {
  return finite(channels(pixel)) && std::isfinite(pixel.ycc.cb) && std::isfinite(pixel.ycc.cr);
}
void validateRange(Range range) {
  if (!std::isfinite(range.low) || !std::isfinite(range.high) ||
      !(range.high > range.low) || !std::isfinite(range.high - range.low))
    throw std::invalid_argument("invalid scopes axis range");
}
void validateOptions(const Options& options) {
  (void)coefficients(options.matrix);
  validateRange(options.levels);
  validateRange(options.chroma);
  if (!supportedDepth(options.gamutBitDepth))
    throw std::invalid_argument("unsupported gamut bit depth");
  if (options.gamutFilter != GamutFilter::None && options.gamutFilter != GamutFilter::Progressive &&
      options.gamutFilter != GamutFilter::Interlaced)
    throw std::invalid_argument("unsupported gamut filter");
  for (const auto size : {options.columns, options.levelBins, options.vectorBins, options.histogramBins})
    if (size == 0 || size > 4096) throw std::invalid_argument("scopes dimensions must be 1..4096");
  // Bound aggregate storage before allocating (128 MiB of 64-bit counts).
  const std::uint64_t cells = 6ULL * options.columns * options.levelBins +
    static_cast<std::uint64_t>(options.vectorBins) * options.vectorBins + 4ULL * options.histogramBins;
  if (cells > 16ULL * 1024 * 1024) throw std::invalid_argument("scopes density storage too large");
}
std::size_t validateView(std::size_t width, std::size_t height, std::size_t stride,
                         std::size_t size, std::size_t unitsPerPixel) {
  if ((width == 0) != (height == 0) || width > UINT32_MAX || height > UINT32_MAX)
    throw std::invalid_argument("invalid scopes frame dimensions");
  const auto limit = (std::numeric_limits<std::size_t>::max)();
  if (width > limit / unitsPerPixel) throw std::invalid_argument("scopes row overflow");
  const auto rowSize = width * unitsPerPixel;
  if (stride == 0) stride = rowSize;
  if (stride < rowSize) throw std::invalid_argument("scopes row stride too short");
  if (height == 0) return stride;
  if (height - 1 > (limit - rowSize) / stride || (height - 1) * stride + rowSize > size)
    throw std::invalid_argument("scopes frame buffer too short");
  return stride;
}
std::size_t bin(double value, Range range, std::size_t count, Excursions& excursions) {
  if (value < range.low) { ++excursions.below; return 0; }
  if (value > range.high) { ++excursions.above; return count - 1; }
  if (value == range.high) return count - 1;
  const auto index = static_cast<std::size_t>(((value - range.low) / (range.high - range.low)) *
                                             static_cast<double>(count));
  return (std::min)(index, count - 1);
}
void add(Histogram& histogram, double value) {
  ++histogram.bins[bin(value, histogram.range, histogram.bins.size(), histogram.excursions)];
}
void addWave(Density& density, std::size_t column, double value) {
  const auto row = bin(value, density.yRange, density.height, density.yExcursions);
  ++density.bins[row * density.width + column];
}
void addVector(Density& density, YCbCr ycc) {
  const auto column = bin(ycc.cb, density.xRange, density.width, density.xExcursions);
  const auto row = bin(ycc.cr, density.yRange, density.height, density.yExcursions);
  ++density.bins[row * density.width + column];
}
void addDiamond(Density& density, double a, double g) {
  const auto column = bin(a - g, density.xRange, density.width, density.xExcursions);
  const auto row = bin(a + g, density.yRange, density.height, density.yExcursions);
  ++density.bins[row * density.width + column];
}
Density density(std::size_t width, std::size_t height, Range xRange, Range yRange) {
  return {width, height, xRange, yRange, std::vector<std::uint64_t>(width * height), {}, {}};
}
Result makeResult(const Options& options) {
  Result result;
  for (std::size_t i = 0; i < 3; ++i) {
    result.ycbcrParade[i] = density(options.columns, options.levelBins, {0, 1},
                                   i == 0 ? options.levels : options.chroma);
    result.rgbParade[i] = density(options.columns, options.levelBins, {0, 1}, options.levels);
    result.rgbHistogram[i] = {options.levels, std::vector<std::uint64_t>(options.histogramBins), {}};
  }
  result.vectorscope = density(options.vectorBins, options.vectorBins, options.chroma, options.chroma);
  for (auto& d : result.diamond) d = density(options.vectorBins, options.vectorBins, {-1, 1}, {0, 2});
  result.lumaHistogram = {options.levels, std::vector<std::uint64_t>(options.histogramBins), {}};
  result.gamutFilter = options.gamutFilter;
  return result;
}
void countRange(GamutRangeCounts& counts, const Channels& values, Range range) {
  // Arithmetic roundoff at an inclusive threshold must not become a violation.
  // This is 1e-9 of an 8-bit code, far smaller than any input code step.
  constexpr double epsilon = 1e-12;
  bool outside = false;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (values[i] < range.low - epsilon) { ++counts.channels[i].below; outside = true; }
    if (values[i] > range.high + epsilon) { ++counts.channels[i].above; outside = true; }
  }
  if (outside) ++counts.pixels;
}
void countGamut(GamutCounts& counts, const Channels& values, unsigned depth) {
  if (!finite(values)) { ++counts.invalidPixels; return; }
  ++counts.validPixels;
  const double scale = static_cast<double>(1u << (depth - 8));
  countRange(counts.nominal, values, {0, 1});
  countRange(counts.preferred, values, {-11.0 / 219.0, 230.0 / 219.0});
  // EBU R 103 v3 Table 1: high is 254/1019/4079/65279, NOT 254 << (n-8).
  countRange(counts.total, values, {-15.0 / 219.0, (239.0 * scale - 1) / (219.0 * scale)});
}

// Rows [rowBegin, rowEnd) of a frame `height` tall: the filter still reads
// the rows either side, so bands of one frame can run apart and be summed.
template<class Reader>
void filteredGamut(Result& result, std::size_t width, std::size_t height,
                   const Options& options, Reader read,
                   std::size_t rowBegin = 0, std::size_t rowEnd = static_cast<std::size_t>(-1)) {
  // Separable R 103 filter, bounded to five cached rows plus one source row.
  // Border samples repeat within the active picture (and within each field).
  std::array<std::vector<Channels>, 5> rows;
  std::array<std::size_t, 5> rowIds;
  rowIds.fill((std::numeric_limits<std::size_t>::max)());
  std::vector<Channels> source(width);
  const auto prepare = [&](std::size_t y) -> const std::vector<Channels>& {
    const auto slot = y % rows.size();
    if (rowIds[slot] == y) return rows[slot];
    auto& row = rows[slot];
    row.resize(width);
    for (std::size_t x = 0; x < width; ++x) {
      const Pixel pixel = read(x, y);
      source[x] = channels(pixel);
      if (!finite(pixel)) source[x].fill((std::numeric_limits<double>::quiet_NaN)());
    }
    constexpr std::array<double, 7> weights{1.0/16, 2.0/16, 3.0/16, 4.0/16, 3.0/16, 2.0/16, 1.0/16};
    for (std::size_t x = 0; x < width; ++x) {
      row[x] = {};
      for (int tap = -3; tap <= 3; ++tap) {
        const auto sx = static_cast<std::size_t>((std::clamp)(static_cast<std::int64_t>(x) + tap,
                                      std::int64_t{0}, static_cast<std::int64_t>(width - 1)));
        for (std::size_t c = 0; c < 4; ++c)
          row[x][c] += source[sx][c] * weights[static_cast<std::size_t>(tap + 3)];
      }
    }
    rowIds[slot] = y;
    return row;
  };
  const std::size_t step = options.gamutFilter == GamutFilter::Interlaced ? 2 : 1;
  for (std::size_t y = rowBegin; y < (std::min)(rowEnd, height); ++y) {
    const auto& upper = prepare(y >= step ? y - step : y);
    const auto& center = prepare(y);
    const auto& lower = prepare(height - y > step ? y + step : y);
    for (std::size_t x = 0; x < width; ++x) {
      Channels values{};
      for (std::size_t c = 0; c < 4; ++c)
        values[c] = upper[x][c] * 0.25 + center[x][c] * 0.5 + lower[x][c] * 0.25;
      countGamut(result.filteredGamut, values, options.gamutBitDepth);
    }
  }
}

template<class Reader>
Result analyzePixels(std::size_t width, std::size_t height, const Options& options, Reader read) {
  Result result = makeResult(options);
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width; ++x) {
      const Pixel pixel = read(x, y);
      if (!finite(pixel)) { ++result.invalidPixels; ++result.rawGamut.invalidPixels; continue; }
      ++result.validPixels;
      // uint32-bounded source dimensions and <=4096 columns cannot overflow.
      const auto column = static_cast<std::size_t>(static_cast<std::uint64_t>(x) * options.columns / width);
      const std::array<double, 3> ycc{pixel.ycc.y, pixel.ycc.cb, pixel.ycc.cr};
      const std::array<double, 3> rgb{pixel.rgb.r, pixel.rgb.g, pixel.rgb.b};
      for (std::size_t c = 0; c < 3; ++c) {
        addWave(result.ycbcrParade[c], column, ycc[c]);
        addWave(result.rgbParade[c], column, rgb[c]);
        add(result.rgbHistogram[c], rgb[c]);
      }
      add(result.lumaHistogram, pixel.ycc.y);
      addVector(result.vectorscope, pixel.ycc);
      addDiamond(result.diamond[0], pixel.rgb.b, pixel.rgb.g);
      addDiamond(result.diamond[1], pixel.rgb.r, pixel.rgb.g);
      countGamut(result.rawGamut, channels(pixel), options.gamutBitDepth);
    }
  }
  if (options.gamutFilter == GamutFilter::None) result.filteredGamut = result.rawGamut;
  else if (width != 0) filteredGamut(result, width, height, options, read);
  return result;
}
} // namespace

YCbCr toYCbCr(Rgb rgb, Matrix matrix) { return encode(rgb, coefficients(matrix)); }
Rgb toRgb(YCbCr ycc, Matrix matrix) { return decode(ycc, coefficients(matrix)); }

bool GamutCounts::outOfGamut() const {
  // Division preserves strict >1% without multiplying a count by 100.
  return preferred.pixels > (validPixels + invalidPixels) / 100;
}

std::array<VectorTarget, 6> vectorTargets(Matrix matrix, TargetLevel level) {
  const auto k = coefficients(matrix);
  if (level != TargetLevel::Percent75 && level != TargetLevel::Percent100)
    throw std::invalid_argument("unsupported vectorscope target level");
  const double amplitude = level == TargetLevel::Percent75 ? 0.75 : 1.0;
  const std::array<Rgb, 6> bars{{{1,0,0}, {1,1,0}, {0,1,0}, {0,1,1}, {0,0,1}, {1,0,1}}};
  const std::array<const char*, 6> labels{"R", "Yl", "G", "Cy", "B", "Mg"};
  std::array<VectorTarget, 6> targets{};
  for (std::size_t i = 0; i < bars.size(); ++i) {
    const auto point = encode({bars[i].r * amplitude, bars[i].g * amplitude, bars[i].b * amplitude}, k);
    targets[i] = {labels[i], {point.cb, point.cr}};
  }
  return targets;
}

VectorPoint skinToneLine(Matrix matrix) {
  // BT.470-6 Table 2 item 2.5: +I with Q=0 has V=cos(33), U=-sin(33).
  // U=0.493(B'-Y'), V=0.877(R'-Y'), Y'=0.299R'+0.587G'+0.114B'.
  constexpr double radians = 33.0 * 3.14159265358979323846 / 180.0;
  const double r = std::cos(radians) / 0.877;
  const double b = -std::sin(radians) / 0.493;
  const double g = -(0.299 * r + 0.114 * b) / 0.587;
  const auto ycc = toYCbCr({r, g, b}, matrix);
  const double radius = std::hypot(ycc.cb, ycc.cr);
  return {0.5 * ycc.cb / radius, 0.5 * ycc.cr / radius};
}

Result analyze(FrameView frame, const Options& options) {
  validateOptions(options);
  const auto stride = validateView(frame.width, frame.height, frame.rowStride, frame.samples.size(), 1);
  if (frame.components != Components::Rgb && frame.components != Components::YCbCr)
    throw std::invalid_argument("unsupported scopes components");
  if (frame.encoding != Encoding::Normalized && frame.encoding != Encoding::NarrowCode &&
      frame.encoding != Encoding::FullCodeRgb)
    throw std::invalid_argument("unsupported scopes input encoding");
  if (frame.encoding == Encoding::FullCodeRgb && frame.components != Components::Rgb)
    throw std::invalid_argument("full-range code input requires RGB");
  if (frame.encoding != Encoding::Normalized && !supportedDepth(frame.bitDepth))
    throw std::invalid_argument("unsupported scopes input bit depth");
  const auto k = coefficients(options.matrix);
  // A normalized input does not use bitDepth, including in shift expressions.
  const unsigned depth = frame.encoding == Encoding::Normalized ? 10 : frame.bitDepth;
  const double scale = static_cast<double>(1u << (depth - 8));
  const double maximum = static_cast<double>((1u << depth) - 1);
  return analyzePixels(frame.width, frame.height, options, [&](std::size_t x, std::size_t y) {
    Sample s = frame.samples[y * stride + x];
    if (frame.encoding == Encoding::FullCodeRgb) {
      s = {s.a / maximum, s.b / maximum, s.c / maximum};
    } else if (frame.encoding == Encoding::NarrowCode) {
      s.a = (s.a - 16 * scale) / (219 * scale);
      if (frame.components == Components::Rgb) {
        s.b = (s.b - 16 * scale) / (219 * scale);
        s.c = (s.c - 16 * scale) / (219 * scale);
      } else {
        s.b = (s.b - 128 * scale) / (224 * scale);
        s.c = (s.c - 128 * scale) / (224 * scale);
      }
    }
    if (frame.components == Components::Rgb) {
      const Rgb rgb{s.a, s.b, s.c};
      return Pixel{rgb, encode(rgb, k)};
    }
    const YCbCr ycc{s.a, s.b, s.c};
    return Pixel{decode(ycc, k), ycc};
  });
}

namespace {
void sumExcursions(Excursions& into, const Excursions& part) {
  into.below += part.below;
  into.above += part.above;
}
void sumDensity(Density& into, const Density& part) {
  if (into.width != part.width || into.height != part.height || into.bins.size() != part.bins.size())
    throw std::invalid_argument("scopes accumulate: density shapes differ");
  for (std::size_t i = 0; i < into.bins.size(); ++i) into.bins[i] += part.bins[i];
  sumExcursions(into.xExcursions, part.xExcursions);
  sumExcursions(into.yExcursions, part.yExcursions);
}
void sumHistogram(Histogram& into, const Histogram& part) {
  if (into.bins.size() != part.bins.size())
    throw std::invalid_argument("scopes accumulate: histogram shapes differ");
  for (std::size_t i = 0; i < into.bins.size(); ++i) into.bins[i] += part.bins[i];
  sumExcursions(into.excursions, part.excursions);
}
void sumRange(GamutRangeCounts& into, const GamutRangeCounts& part) {
  for (std::size_t c = 0; c < into.channels.size(); ++c) sumExcursions(into.channels[c], part.channels[c]);
  into.pixels += part.pixels;
}
void sumGamut(GamutCounts& into, const GamutCounts& part) {
  into.validPixels += part.validPixels;
  into.invalidPixels += part.invalidPixels;
  sumRange(into.nominal, part.nominal);
  sumRange(into.preferred, part.preferred);
  sumRange(into.total, part.total);
}
} // namespace

void accumulate(Result& whole, const Result& part) {
  whole.validPixels += part.validPixels;
  whole.invalidPixels += part.invalidPixels;
  for (std::size_t c = 0; c < 3; ++c) {
    sumDensity(whole.ycbcrParade[c], part.ycbcrParade[c]);
    sumDensity(whole.rgbParade[c], part.rgbParade[c]);
    sumHistogram(whole.rgbHistogram[c], part.rgbHistogram[c]);
  }
  sumDensity(whole.vectorscope, part.vectorscope);
  for (std::size_t d = 0; d < whole.diamond.size(); ++d) sumDensity(whole.diamond[d], part.diamond[d]);
  sumHistogram(whole.lumaHistogram, part.lumaHistogram);
  sumGamut(whole.rawGamut, part.rawGamut);
  sumGamut(whole.filteredGamut, part.filteredGamut);
}

// 8-bit R'G'B' bytes. Anything that depends on one byte (a parade row, a
// histogram bin) or two (a Diamond cell) is binned once per value with the
// same expressions analyzePixels uses, so the result is identical bin for bin
// (scopes_check compares the two). Every R'G'B' value is in 0..1, so the raw
// gamut can only ever see Y' -- and the filtered pass, an average of in-range
// values, finds nothing whenever the raw found nothing: it is skipped then.
// It was 620 ms of a 4K frame spent computing a zero.
Result analyze(Rgba8View frame, const Options& options) {
  validateOptions(options);
  const auto stride = validateView(frame.width, frame.height, frame.rowStrideBytes, frame.bytes.size(), 4);
  const auto k = coefficients(options.matrix);
  const std::size_t red = frame.bgra ? 2 : 0, blue = frame.bgra ? 0 : 2;
  const auto read = [&](std::size_t x, std::size_t y) {
    const auto offset = y * stride + x * 4;
    const Rgb rgb{frame.bytes[offset + red] / 255.0, frame.bytes[offset + 1] / 255.0,
                  frame.bytes[offset + blue] / 255.0};
    return Pixel{rgb, encode(rgb, k)};
  };
  Result result = makeResult(options);
  struct ByteBin { std::size_t bin = 0; std::uint8_t below = 0, above = 0; };
  std::array<double, 256> unit{};
  for (std::size_t v = 0; v < 256; ++v) unit[v] = static_cast<double>(v) / 255.0;
  const auto table = [&](Range range, std::size_t count) {
    std::array<ByteBin, 256> lut{};
    for (std::size_t v = 0; v < 256; ++v) {
      Excursions e;
      lut[v].bin = bin(unit[v], range, count, e);
      lut[v].below = static_cast<std::uint8_t>(e.below);
      lut[v].above = static_cast<std::uint8_t>(e.above);
    }
    return lut;
  };
  const auto paradeRow = table(options.levels, options.levelBins);
  const auto histBin = table(options.levels, options.histogramBins);
  // Diamond cell for (a, g): x = a - g in [-1, 1], y = a + g in [0, 2], so
  // never an excursion; stored as the bin index.
  std::vector<std::uint32_t> diamondCell(256 * 256);
  {
    Excursions unused;
    const Density& d = result.diamond[0];
    for (std::size_t a = 0; a < 256; ++a)
      for (std::size_t g = 0; g < 256; ++g)
        diamondCell[a * 256 + g] = static_cast<std::uint32_t>(
          bin(unit[a] + unit[g], d.yRange, d.height, unused) * d.width +
          bin(unit[a] - unit[g], d.xRange, d.width, unused));
  }
  std::vector<std::size_t> columnOf(frame.width);
  for (std::size_t x = 0; x < frame.width; ++x)
    columnOf[x] = static_cast<std::size_t>(static_cast<std::uint64_t>(x) * options.columns / frame.width);
  // Shared axes use the same bin exactly, including edge excursions.
  const bool sharedLumaBins = options.histogramBins == options.levelBins;
  const bool sharedChromaBins = options.vectorBins == options.levelBins;
  constexpr double epsilon = 1e-12;  // countRange's
  // EVERYTHING THE LOOP TOUCHES, IN LOCALS. MSVC assumes a store through any
  // counter may change any other memory, so with the bins reached through the
  // Result it reloaded every vector's address, width and range, and read and
  // wrote back every excursion and pixel total, after each of a dozen
  // increments per pixel. Copied here, they stay in registers; the totals
  // are written back once. The bins are computed exactly as before.
  const std::size_t width = options.columns;   // every parade's width
  const std::size_t levels = result.ycbcrParade[0].height;
  std::uint64_t* waveBins[3];
  std::uint64_t* paradeBins[3];
  std::uint64_t* histBins[3];
  Range waveRange[3];
  Excursions waveEx[3], paradeEx[3], histEx[3];
  for (std::size_t c = 0; c < 3; ++c) {
    waveBins[c] = result.ycbcrParade[c].bins.data();
    paradeBins[c] = result.rgbParade[c].bins.data();
    histBins[c] = result.rgbHistogram[c].bins.data();
    waveRange[c] = result.ycbcrParade[c].yRange;
  }
  std::uint64_t* const lumaBins = result.lumaHistogram.bins.data();
  std::uint64_t* const vectorBins = result.vectorscope.bins.data();
  const std::size_t vectorWidth = result.vectorscope.width;
  std::uint64_t* const diamondBins[2] = {result.diamond[0].bins.data(), result.diamond[1].bins.data()};
  std::uint64_t valid = 0, rawValid = 0;
  for (std::size_t y = 0; y < frame.height; ++y) {
    const std::uint8_t* row = frame.bytes.data() + y * stride;
    for (std::size_t x = 0; x < frame.width; ++x) {
      const std::uint8_t bytes[3] = {row[x * 4 + red], row[x * 4 + 1], row[x * 4 + blue]};
      const Rgb rgb{unit[bytes[0]], unit[bytes[1]], unit[bytes[2]]};
      const YCbCr ycc = encode(rgb, k);
      const std::size_t column = columnOf[x];
      ++valid;
      const double yccValues[3] = {ycc.y, ycc.cb, ycc.cr};
      std::size_t yccRows[3];
      for (std::size_t c = 0; c < 3; ++c) {
        yccRows[c] = bin(yccValues[c], waveRange[c], levels, waveEx[c]);
        ++waveBins[c][yccRows[c] * width + column];
        const ByteBin& row_ = paradeRow[bytes[c]];
        ++paradeBins[c][row_.bin * width + column];
        paradeEx[c].below += row_.below;
        paradeEx[c].above += row_.above;
        const ByteBin& hb = histBin[bytes[c]];
        ++histBins[c][hb.bin];
        histEx[c].below += hb.below;
        histEx[c].above += hb.above;
      }
      if (sharedLumaBins) ++lumaBins[yccRows[0]];
      else add(result.lumaHistogram, ycc.y);
      if (sharedChromaBins) ++vectorBins[yccRows[2] * vectorWidth + yccRows[1]];
      else addVector(result.vectorscope, ycc);
      ++diamondBins[0][diamondCell[bytes[2] * 256 + bytes[1]]];
      ++diamondBins[1][diamondCell[bytes[0] * 256 + bytes[1]]];
      if (ycc.y >= -epsilon && ycc.y <= 1.0 + epsilon) ++rawValid;
      else countGamut(result.rawGamut, channels(Pixel{rgb, ycc}), options.gamutBitDepth);
    }
  }
  result.validPixels += valid;
  result.rawGamut.validPixels += rawValid;
  for (std::size_t c = 0; c < 3; ++c) {
    sumExcursions(result.ycbcrParade[c].yExcursions, waveEx[c]);
    sumExcursions(result.rgbParade[c].yExcursions, paradeEx[c]);
    sumExcursions(result.rgbHistogram[c].excursions, histEx[c]);
  }
  if (sharedLumaBins) result.lumaHistogram.excursions = result.ycbcrParade[0].yExcursions;
  if (sharedChromaBins) {
    result.vectorscope.xExcursions = result.ycbcrParade[1].yExcursions;
    result.vectorscope.yExcursions = result.ycbcrParade[2].yExcursions;
  }
  const bool rawClean = result.rawGamut.nominal.pixels == 0 && result.rawGamut.total.pixels == 0 &&
                        result.rawGamut.preferred.pixels == 0;
  if (options.gamutFilter == GamutFilter::None || (rawClean && frame.width != 0)) {
    result.filteredGamut = result.rawGamut;
  } else if (frame.width != 0) {
    filteredGamut(result, frame.width, frame.height, options, read);
  }
  return result;
}

namespace {
// Reads a Bgra422View pixel by pixel as the wire carries it. Both pixels of a
// pair read the same encode, so the last one is kept.
struct Sent422Reader {
  // A constructor, not brace-init: GCC and Clang (-Werror) refuse an aggregate
  // initialiser that leaves the cache fields to their defaults.
  Sent422Reader(const Bgra422View& view, std::size_t rowStride, bool ten)
    : frame(view), stride(rowStride), k(coefficients(Matrix::Bt709)), tenBit(ten), scale(ten ? 4.0 : 1.0) {}
  const Bgra422View& frame;
  std::size_t stride;
  Coefficients k;
  bool tenBit;
  double scale;
  std::size_t pairX = (std::numeric_limits<std::size_t>::max)(), pairY = pairX;
  deckboy::core::Ycbcr422Pair pair;
  Pixel operator()(std::size_t x, std::size_t y) {
    const std::size_t x0 = x & ~std::size_t{1};
    if (x0 != pairX || y != pairY) {
      const std::size_t x1 = (std::min)(x0 + 1, frame.width - 1);  // an odd last pixel pairs with itself
      const std::uint8_t* row = frame.bytes.data() + y * stride;
      pair = deckboy::core::encodeBgraPair709(row + x0 * 4, row + x1 * 4, tenBit);
      pairX = x0;
      pairY = y;
    }
    const double code = (x & 1) ? pair.y1 : pair.y0;
    const YCbCr ycc{(code - 16 * scale) / (219 * scale), (pair.cb - 128 * scale) / (224 * scale),
                    (pair.cr - 128 * scale) / (224 * scale)};
    return Pixel{decode(ycc, k), ycc};
  }
};

Sent422Reader sent422Reader(const Bgra422View& frame, const Options& options) {
  validateOptions(options);
  if (frame.bitDepth != 8 && frame.bitDepth != 10)
    throw std::invalid_argument("4:2:2 scopes input is 8- or 10-bit");
  const auto stride = validateView(frame.width, frame.height, frame.rowStrideBytes, frame.bytes.size(), 4);
  return Sent422Reader(frame, stride, frame.bitDepth == 10);
}
} // namespace

Result analyze(Bgra422View frame, const Options& options) {
  Sent422Reader read = sent422Reader(frame, options);
  return analyzePixels(frame.width, frame.height, options, [&](std::size_t x, std::size_t y) { return read(x, y); });
}

Result analyze(Rgb16View frame, const Options& options) {
  validateOptions(options);
  const auto stride = validateView(frame.width, frame.height, frame.rowStrideBytes, frame.bytes.size(), 6);
  const auto k = coefficients(options.matrix);
  return analyzePixels(frame.width, frame.height, options, [&](std::size_t x, std::size_t y) {
    const std::uint8_t* p = frame.bytes.data() + y * stride + x * 6;
    const auto word = [](const std::uint8_t* b) { return (b[0] | (b[1] << 8)) / 65535.0; };
    const Rgb rgb{word(p), word(p + 2), word(p + 4)};
    return Pixel{rgb, encode(rgb, k)};
  });
}

namespace {
constexpr double kPqM1 = 2610.0 / 16384.0, kPqM2 = 2523.0 / 4096.0 * 128.0;
constexpr double kPqC1 = 3424.0 / 4096.0, kPqC2 = 2413.0 / 4096.0 * 32.0, kPqC3 = 2392.0 / 4096.0 * 32.0;
} // namespace

double pqToNits(double code) {
  const double e = std::pow(std::clamp(code, 0.0, 1.0), 1.0 / kPqM2);
  return 10000.0 * std::pow(std::max(0.0, e - kPqC1) / (kPqC2 - kPqC3 * e), 1.0 / kPqM1);
}

double nitsToPq(double nits) {
  const double y = std::pow(std::clamp(nits, 0.0, 10000.0) / 10000.0, kPqM1);
  return std::pow((kPqC1 + kPqC2 * y) / (1.0 + kPqC3 * y), kPqM2);
}

GamutCounts filteredGamutRows(Bgra422View frame, const Options& options,
                              std::size_t rowBegin, std::size_t rowEnd) {
  Sent422Reader read = sent422Reader(frame, options);
  Result result = makeResult(options);
  if (frame.width != 0)
    filteredGamut(result, frame.width, frame.height, options,
                  [&](std::size_t x, std::size_t y) { return read(x, y); }, rowBegin, rowEnd);
  return result.filteredGamut;
}

void accumulate(GamutCounts& whole, const GamutCounts& part) { sumGamut(whole, part); }

struct ParallelAnalyzer::Impl {
  std::mutex mutex;
  std::condition_variable ready, done;
  std::vector<std::thread> threads;
  std::function<void(std::size_t)> job;
  std::size_t generation = 0, remaining = 0;
  bool stopping = false;

  explicit Impl(unsigned count) {
    if (!count) count = std::max(1u, std::thread::hardware_concurrency());
    count = std::clamp(count, 1u, 16u);
    try {
      for (unsigned i = 1; i < count; ++i) threads.emplace_back([this, i] {
        std::size_t seen = 0;
        std::unique_lock lock(mutex);
        for (;;) {
          ready.wait(lock, [&] { return stopping || generation != seen; });
          if (stopping) return;
          seen = generation;
          lock.unlock();
          job(i); // the supplied job catches its own exception in its slot
          lock.lock();
          if (--remaining == 0) done.notify_one();
        }
      });
    } catch (...) { stop(); throw; }
  }
  void stop() {
    { std::lock_guard lock(mutex); stopping = true; }
    ready.notify_all();
    for (auto& thread : threads) if (thread.joinable()) thread.join();
  }
  ~Impl() { stop(); }

  template<class View>
  Result analyzeFrame(View frame, const Options& options, std::size_t bytesPerPixel, bool pairs) {
    validateOptions(options);
    const auto stride = validateView(frame.width, frame.height, frame.rowStrideBytes,
                                     frame.bytes.size(), bytesPerPixel);
    // A filtered pass needs neighbours outside a strip. Keep its existing
    // whole-frame contract; the live monitor schedules it separately.
    if (!frame.width || options.gamutFilter != GamutFilter::None)
      return scopes::analyze(frame, options);
    std::size_t groups = std::gcd(frame.width, options.columns);
    // Each boundary must be both a waveform-bin edge and a chroma-pair edge.
    if (pairs && (frame.width / groups) % 2) {
      if (groups % 2) return scopes::analyze(frame, options);
      groups /= 2;
    }
    if (!groups) return scopes::analyze(frame, options);
    const std::size_t count = std::min(groups, threads.size() + 1);
    if (count == 1) return scopes::analyze(frame, options);
    std::vector<Result> parts(count);
    std::vector<std::exception_ptr> errors(count);
    std::vector<std::size_t> offsets(count);
    const auto run = [&](std::size_t i) {
      if (i >= count) return;
      try {
        const auto first = groups * i / count, last = groups * (i + 1) / count;
        const auto x0 = first * (frame.width / groups), x1 = last * (frame.width / groups);
        Options local = options;
        local.columns = (last - first) * (options.columns / groups);
        offsets[i] = first * (options.columns / groups);
        auto tile = frame;
        tile.bytes = frame.bytes.subspan(x0 * bytesPerPixel,
          (frame.height - 1) * stride + (x1 - x0) * bytesPerPixel);
        tile.width = x1 - x0;
        tile.rowStrideBytes = stride;
        parts[i] = scopes::analyze(tile, local);
      } catch (...) { errors[i] = std::current_exception(); }
    };
    {
      std::lock_guard lock(mutex);
      job = run;
      remaining = threads.size();
      ++generation;
    }
    ready.notify_all();
    run(0);
    { std::unique_lock lock(mutex); done.wait(lock, [&] { return remaining == 0; }); job = {}; }
    for (const auto& error : errors) if (error) std::rethrow_exception(error);
    Result whole = makeResult(options);
    const auto join = [](Density& into, const Density& part, std::size_t offset) {
      for (std::size_t row = 0; row < part.height; ++row)
        std::copy_n(part.bins.begin() + row * part.width, part.width,
                    into.bins.begin() + row * into.width + offset);
      sumExcursions(into.xExcursions, part.xExcursions);
      sumExcursions(into.yExcursions, part.yExcursions);
    };
    for (std::size_t i = 0; i < count; ++i) {
      const auto& part = parts[i];
      whole.validPixels += part.validPixels;
      whole.invalidPixels += part.invalidPixels;
      for (std::size_t c = 0; c < 3; ++c) {
        join(whole.ycbcrParade[c], part.ycbcrParade[c], offsets[i]);
        join(whole.rgbParade[c], part.rgbParade[c], offsets[i]);
        sumHistogram(whole.rgbHistogram[c], part.rgbHistogram[c]);
      }
      sumDensity(whole.vectorscope, part.vectorscope);
      for (std::size_t d = 0; d < 2; ++d) sumDensity(whole.diamond[d], part.diamond[d]);
      sumHistogram(whole.lumaHistogram, part.lumaHistogram);
      sumGamut(whole.rawGamut, part.rawGamut);
      sumGamut(whole.filteredGamut, part.filteredGamut);
    }
    return whole;
  }
};

ParallelAnalyzer::ParallelAnalyzer(unsigned workers) : impl_(std::make_unique<Impl>(workers)) {}
ParallelAnalyzer::~ParallelAnalyzer() = default;
Result ParallelAnalyzer::analyze(Rgba8View frame, const Options& options) {
  return impl_->analyzeFrame(frame, options, 4, false);
}
Result ParallelAnalyzer::analyze(Bgra422View frame, const Options& options) {
  return impl_->analyzeFrame(frame, options, 4, true);
}
Result ParallelAnalyzer::analyze(Rgb16View frame, const Options& options) {
  return impl_->analyzeFrame(frame, options, 6, false);
}
} // namespace deckboy::core::scopes
