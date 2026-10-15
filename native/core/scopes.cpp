// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#include "scopes.hpp"

#include "ycbcr422.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

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
  constexpr double epsilon = 1e-12;  // countRange's
  for (std::size_t y = 0; y < frame.height; ++y) {
    const std::uint8_t* row = frame.bytes.data() + y * stride;
    for (std::size_t x = 0; x < frame.width; ++x) {
      const std::uint8_t bytes[3] = {row[x * 4 + red], row[x * 4 + 1], row[x * 4 + blue]};
      const Rgb rgb{unit[bytes[0]], unit[bytes[1]], unit[bytes[2]]};
      const YCbCr ycc = encode(rgb, k);
      const std::size_t column = columnOf[x];
      ++result.validPixels;
      const std::array<double, 3> yccValues{ycc.y, ycc.cb, ycc.cr};
      for (std::size_t c = 0; c < 3; ++c) {
        addWave(result.ycbcrParade[c], column, yccValues[c]);
        Density& parade = result.rgbParade[c];
        const ByteBin& row_ = paradeRow[bytes[c]];
        ++parade.bins[row_.bin * parade.width + column];
        parade.yExcursions.below += row_.below;
        parade.yExcursions.above += row_.above;
        Histogram& hist = result.rgbHistogram[c];
        const ByteBin& hb = histBin[bytes[c]];
        ++hist.bins[hb.bin];
        hist.excursions.below += hb.below;
        hist.excursions.above += hb.above;
      }
      add(result.lumaHistogram, ycc.y);
      addVector(result.vectorscope, ycc);
      ++result.diamond[0].bins[diamondCell[bytes[2] * 256 + bytes[1]]];
      ++result.diamond[1].bins[diamondCell[bytes[0] * 256 + bytes[1]]];
      if (ycc.y >= -epsilon && ycc.y <= 1.0 + epsilon) ++result.rawGamut.validPixels;
      else countGamut(result.rawGamut, channels(Pixel{rgb, ycc}), options.gamutBitDepth);
    }
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
} // namespace deckboy::core::scopes
