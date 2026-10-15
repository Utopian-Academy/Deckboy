// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// Generated signals against production code, no files, SDL or display needed.
// Standalone: c++ -std=c++20 -I native tools/scopes_check.cpp native/core/scopes.cpp -o scopes-check
#include "core/scopes.hpp"
#include "core/ycbcr422.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

namespace {
namespace s = deckboy::core::scopes;
int failures = 0, checks = 0;
void expect(bool condition, const std::string& label) {
  ++checks;
  if (!condition) { ++failures; std::cerr << "[fail] " << label << '\n'; }
}
bool close(double a, double b) { return std::abs(a - b) < 1e-11; }
template<class Function>
void rejects(Function function, const char* label) {
  try { function(); expect(false, label); }
  catch (const std::invalid_argument&) { expect(true, label); }
}
std::uint64_t sum(const std::vector<std::uint64_t>& values) {
  return std::accumulate(values.begin(), values.end(), std::uint64_t{0});
}
s::Options smallOptions() {
  s::Options options;
  options.columns = options.levelBins = options.vectorBins = options.histogramBins = 16;
  return options;
}
void conserved(const s::Result& result, std::uint64_t count, const std::string& label) {
  expect(result.validPixels == count && sum(result.lumaHistogram.bins) == count &&
         sum(result.vectorscope.bins) == count, label + " sample totals");
  for (std::size_t c = 0; c < 3; ++c)
    expect(sum(result.rgbParade[c].bins) == count && sum(result.ycbcrParade[c].bins) == count &&
           sum(result.rgbHistogram[c].bins) == count, label + " channel totals");
}

void bars() {
  const std::array<s::Sample, 6> rgb{{{1,0,0}, {1,1,0}, {0,1,0}, {0,1,1}, {0,0,1}, {1,0,1}}};
  // Independent high-precision evaluations of BT.709-6 section 3 and
  // BT.2020-2 Table 4. These constants never call the conversion under test.
  const std::array<std::array<s::YCbCr, 6>, 2> reference{{
    {{{.2126,-.114572106057340,.5}, {.9278,-.5,.045847091694183},
      {.7152,-.385427893942660,-.454152908305817}, {.7874,.114572106057340,-.5},
      {.0722,.5,-.045847091694183}, {.2848,.385427893942660,.454152908305817}}},
    {{{.2627,-.139630062719252,.5}, {.9407,-.5,.040214295402143},
      {.6780,-.360369937280748,-.459785704597857}, {.7373,.139630062719252,-.5},
      {.0593,.5,-.040214295402143}, {.3220,.360369937280748,.459785704597857}}}
  }};
  for (std::size_t m = 0; m < 2; ++m) {
    const auto matrix = m == 0 ? s::Matrix::Bt709 : s::Matrix::Bt2020Ncl;
    for (const double amplitude : {0.75, 1.0}) {
      const auto targets = s::vectorTargets(matrix, amplitude == .75 ? s::TargetLevel::Percent75 :
                                                                    s::TargetLevel::Percent100);
      std::array<s::Sample, 6> pixels{};
      for (std::size_t c = 0; c < rgb.size(); ++c) {
        const auto input = s::Rgb{rgb[c].a * amplitude, rgb[c].b * amplitude, rgb[c].c * amplitude};
        pixels[c] = {input.r, input.g, input.b};
        const auto ycc = s::toYCbCr(input, matrix);
        const auto expected = reference[m][c];
        expect(close(ycc.y, expected.y * amplitude) && close(ycc.cb, expected.cb * amplitude) &&
               close(ycc.cr, expected.cr * amplitude), "bar matrix m=" + std::to_string(m) + " c=" + std::to_string(c));
        const auto inverse = s::toRgb({expected.y * amplitude, expected.cb * amplitude,
                                      expected.cr * amplitude}, matrix);
        expect(close(inverse.r, input.r) && close(inverse.g, input.g) && close(inverse.b, input.b),
               "inverse matrix uses independent reference");
        expect(close(targets[c].point.cb, expected.cb * amplitude) &&
               close(targets[c].point.cr, expected.cr * amplitude), "75/100 percent vector target");
      }
      auto options = smallOptions();
      options.matrix = matrix;
      options.columns = 6;
      options.levelBins = options.vectorBins = 100;
      const auto result = s::analyze(s::FrameView{pixels, 6, 1}, options);
      conserved(result, 6, "colour bars");
      expect(result.rawGamut.nominal.pixels == 0 && result.filteredGamut.nominal.pixels == 0,
             "legal bars remain legal at exact channel endpoints");
      for (std::size_t c = 0; c < 6; ++c) {
        const auto row = static_cast<std::size_t>(reference[m][c].y * amplitude * 100);
        expect(result.lumaWaveform().bins[row * 6 + c] == 1, "bar waveform retains horizontal position");
        const auto cbBin = (std::min)(std::size_t{99}, static_cast<std::size_t>((reference[m][c].cb * amplitude + .5) * 100));
        const auto crBin = (std::min)(std::size_t{99}, static_cast<std::size_t>((reference[m][c].cr * amplitude + .5) * 100));
        expect(result.vectorscope.bins[crBin * 100 + cbBin] == 1,
               "vectorscope plots independent Cb/Cr coordinates on the correct axes");
        expect(result.ycbcrParade[1].bins[cbBin * 6 + c] == 1 &&
               result.ycbcrParade[2].bins[crBin * 6 + c] == 1, "chroma parade positions and channel order");
        for (std::size_t ch = 0; ch < 3; ++ch) {
          const std::array<double, 3> components{pixels[c].a, pixels[c].b, pixels[c].c};
          const auto rgbRow = components[ch] == 1 ? 99 : static_cast<std::size_t>(components[ch] * 100);
          expect(result.rgbParade[ch].bins[rgbRow * 6 + c] == 1, "RGB parade channel position");
        }
      }
    }
    // Guide reconstructed as a signal with BT.470 +I phase (33 degrees).
    const auto line = s::skinToneLine(matrix);
    expect(line.cb < 0 && line.cr > 0 && close(std::hypot(line.cb, line.cr), .5),
           "skin guide orientation and unit radius");
    const auto referenceRgb = s::toRgb({0, line.cb, line.cr}, matrix);
    const double ntscY = .299 * referenceRgb.r + .587 * referenceRgb.g + .114 * referenceRgb.b;
    const double u = .493 * (referenceRgb.b - ntscY);
    const double v = .877 * (referenceRgb.r - ntscY);
    expect(close(std::atan2(-u, v) * 180 / 3.14159265358979323846, 33), "skin guide +I phase");
  }
}

void rampsAndStorage() {
  auto options = smallOptions();
  options.columns = 256;
  options.levelBins = options.histogramBins = 256;
  std::vector<s::Sample> pixels(256 * 2);
  for (std::size_t x = 0; x < 256; ++x) {
    const double grey = static_cast<double>(x) / 255;
    pixels[x] = pixels[x + 256] = {grey, grey, grey};
  }
  for (const auto matrix : {s::Matrix::Bt709, s::Matrix::Bt2020Ncl}) {
    options.matrix = matrix;
    const auto result = s::analyze(s::FrameView{pixels, 256, 2}, options);
    conserved(result, 512, "grey ramp");
    for (std::size_t i = 0; i < 256; ++i) {
      expect(result.lumaHistogram.bins[i] == 2 && result.lumaWaveform().bins[i * 256 + i] == 2,
             "ramp independent histogram and waveform bins");
      expect(result.rgbHistogram[0].bins[i] == 2 && result.rgbHistogram[1].bins[i] == 2 &&
             result.rgbHistogram[2].bins[i] == 2, "RGB ramp bins");
    }
    expect(result.vectorscope.bins[8 * 16 + 8] == 512, "all grey chroma is at the origin");
    options.columns = 7;
    const auto reduced = s::analyze(s::FrameView{pixels, 256, 2}, options);
    for (std::size_t col = 0; col < 7; ++col) {
      std::uint64_t count = 0, expected = 0;
      for (std::size_t row = 0; row < 256; ++row) count += reduced.lumaWaveform().bins[row * 7 + col];
      for (std::size_t x = 0; x < 256; ++x) if (x * 7 / 256 == col) expected += 2;
      expect(count == expected, "uneven horizontal bin mapping preserves every sample");
    }
    options.columns = 256;
  }
  // Padding and alpha must never contribute. Last row needs no trailing padding.
  const std::array<std::uint8_t, 20> rgba{255,0,0,0, 0,255,0,255, 23,81,19,54, 0,0,255,128, 255,255,255,1};
  options = smallOptions();
  const auto byteResult = s::analyze(s::Rgba8View{rgba, 2, 2, 12}, options);
  const std::array<s::Sample, 5> coded{{{255,0,0}, {0,255,0}, {-500,-500,-500}, {0,0,255}, {255,255,255}}};
  const auto codeResult = s::analyze(s::FrameView{coded, 2, 2, 3, s::Components::Rgb, s::Encoding::FullCodeRgb, 8}, options);
  conserved(byteResult, 4, "padded RGBA");
  expect(byteResult.lumaHistogram.bins == codeResult.lumaHistogram.bins &&
         byteResult.vectorscope.bins == codeResult.vectorscope.bins &&
         byteResult.lumaWaveform().bins == codeResult.lumaWaveform().bins && codeResult.rawGamut.nominal.pixels == 0,
         "RGBA and full-code RGB stride parity");
  const auto empty = s::analyze(s::FrameView{}, options);
  conserved(empty, 0, "empty frame");
  expect(empty.filteredGamut.validPixels == 0 && !empty.filteredGamut.outOfGamut(), "empty filter and threshold");
  // One bin is a supported accumulation mode, including singleton pictures.
  options.columns = options.levelBins = options.vectorBins = options.histogramBins = 1;
  const auto one = s::analyze(s::FrameView{std::span(coded).first(1), 1, 1, 0,
                             s::Components::Rgb, s::Encoding::FullCodeRgb, 8}, options);
  conserved(one, 1, "one pixel and one bin");
  expect(one.filteredGamut.nominal.pixels == 0, "single-pixel border extension");
}

void digitalAndGamut() {
  auto options = smallOptions();
  options.gamutFilter = s::GamutFilter::None;
  for (const unsigned depth : {8u, 10u, 12u, 16u}) {
    options.gamutBitDepth = depth;
    const double scale = static_cast<double>(1u << (depth - 8));
    // Exact limits and one-code neighbours; R 103 v3 Table 1.
    const std::array<double, 12> levels{16*scale, 235*scale, 5*scale, 246*scale,
      5*scale-1, 246*scale+1, scale, 255*scale-1, scale-1, 255*scale, 16*scale-1, 235*scale+1};
    std::vector<s::Sample> rgb, ycc;
    for (const double code : levels) {
      rgb.push_back({code,code,code});
      ycc.push_back({code,128*scale,128*scale});
    }
    for (const auto matrix : {s::Matrix::Bt709, s::Matrix::Bt2020Ncl}) {
      options.matrix = matrix;
      for (const auto components : {s::Components::Rgb, s::Components::YCbCr}) {
        const auto& pixels = components == s::Components::Rgb ? rgb : ycc;
        const auto result = s::analyze(s::FrameView{pixels, pixels.size(), 1, 0, components,
                                                  s::Encoding::NarrowCode, depth}, options);
        conserved(result, 12, "digital range boundaries");
        expect(result.rawGamut.nominal.pixels == 10 && result.rawGamut.preferred.pixels == 6 &&
               result.rawGamut.total.pixels == 2, "nominal/preferred/total counts without clipping");
        for (std::size_t c = 0; c < 4; ++c)
          expect(result.rawGamut.nominal.channels[c].below == 5 && result.rawGamut.nominal.channels[c].above == 5 &&
                 result.rawGamut.preferred.channels[c].below == 3 && result.rawGamut.preferred.channels[c].above == 3 &&
                 result.rawGamut.total.channels[c].below == 1 && result.rawGamut.total.channels[c].above == 1,
                 "per-channel inclusive digital boundaries");
        expect(result.lumaHistogram.excursions.below == 5 && result.lumaHistogram.excursions.above == 5 &&
               result.filteredGamut.preferred.pixels == result.rawGamut.preferred.pixels, "edge bins retain excursions");
      }
    }
    // Narrow-range neutral ramp and PLUGE retain sub-black and super-white.
    std::vector<s::Sample> ramp;
    const auto count = static_cast<std::size_t>(219 * scale) + 1;
    for (std::size_t i = 0; i < count; ++i) ramp.push_back({16*scale + static_cast<double>(i),128*scale,128*scale});
    const auto result = s::analyze(s::FrameView{ramp, count, 1, 0, s::Components::YCbCr,
                                              s::Encoding::NarrowCode, depth}, options);
    expect(result.rawGamut.nominal.pixels == 0 && result.vectorscope.bins[8*16+8] == count,
           "every legal narrow-range ramp code remains neutral and legal");
  }
  // Legal Y/Cb/Cr code ranges can still reconstruct illegal RGB.
  const std::array<s::Sample, 1> impossible{{{.5,.5,.5}}};
  options = smallOptions();
  const auto illegal = s::analyze(s::FrameView{impossible,1,1,0,s::Components::YCbCr}, options);
  expect(illegal.rawGamut.preferred.pixels == 1 && illegal.rawGamut.preferred.channels[0].above == 0 &&
         illegal.rawGamut.preferred.channels[1].above == 1 && illegal.rawGamut.preferred.channels[3].above == 1,
         "gamut checks dematrixed RGB, pixel union is not sum of channels");
  std::vector<s::Sample> threshold(100, {0,0,0});
  threshold[0] = {1.2,1.2,1.2};
  options.gamutFilter = s::GamutFilter::None;
  expect(!s::analyze(s::FrameView{threshold,100,1}, options).rawGamut.outOfGamut(), "exactly one percent is not an alarm");
  threshold[1] = {1.2,1.2,1.2};
  expect(s::analyze(s::FrameView{threshold,100,1}, options).rawGamut.outOfGamut(), "more than one percent is an alarm");
  expect(s::analyze(s::FrameView{std::span(threshold).subspan(1),99,1}, options).rawGamut.outOfGamut(),
         "one of 99 pixels exceeds one percent");
  const std::array<s::Sample, 4> pluge{{{-.04,-.04,-.04},{0,0,0},{.04,.04,.04},{1.04,1.04,1.04}}};
  options.levels = {-.1,1.1};
  const auto extended = s::analyze(s::FrameView{pluge,4,1}, options);
  expect(extended.rawGamut.nominal.pixels == 2 && extended.rawGamut.preferred.pixels == 0 &&
         extended.lumaHistogram.excursions.below == 0 && extended.lumaHistogram.excursions.above == 0,
         "extended axes preserve PLUGE and super-white without altering gamut counts");
  const std::array<s::Sample, 2> chromaExcursions{{{.5,-.75,.8},{.5,.9,-.6}}};
  const auto outside = s::analyze(s::FrameView{chromaExcursions,2,1,0,s::Components::YCbCr}, options);
  expect(outside.vectorscope.xExcursions.below == 1 && outside.vectorscope.xExcursions.above == 1 &&
         outside.vectorscope.yExcursions.below == 1 && outside.vectorscope.yExcursions.above == 1 &&
         outside.vectorscope.bins[15 * 16] == 1 && outside.vectorscope.bins[15] == 1,
         "chroma excursions preserved in corners and independent axis counters");
  // Fractional narrow codes for a BT.709 red bar have exactly known chroma
  // scaling (224, not the 219 used for luma), at each supported depth.
  options.matrix = s::Matrix::Bt709;
  for (const unsigned depth : {8u,10u,12u,16u}) {
    const double scale = static_cast<double>(1u << (depth - 8));
    const std::array<s::Sample, 1> red{{{(16 + 219*.2126)*scale,
      (128 - 224*.114572106057340)*scale, 240*scale}}};
    const auto r = s::analyze(s::FrameView{red,1,1,0,s::Components::YCbCr,s::Encoding::NarrowCode,depth}, options);
    expect(r.rgbHistogram[0].bins[14] == 1 && r.rgbHistogram[1].bins[1] == 1 && r.rgbHistogram[2].bins[1] == 1 &&
           r.rawGamut.nominal.pixels == 0, "narrow YCbCr chroma has distinct offset and scale");
    const double full = static_cast<double>((1u << depth) - 1);
    const std::array<s::Sample, 2> fullCodes{{{0,0,0},{full,full,full}}};
    const auto f = s::analyze(s::FrameView{fullCodes,2,1,0,s::Components::Rgb,s::Encoding::FullCodeRgb,depth}, options);
    expect(f.lumaHistogram.bins[1] == 1 && f.lumaHistogram.bins[14] == 1 && f.rawGamut.nominal.pixels == 0,
           "full-code normalization at every supported depth");
  }
}

void filtering() {
  auto options = smallOptions();
  // At center, horizontal FIR impulse = 4/16; vertical FIR = 1/2.
  // A peak of 4 above black becomes 0.5, hence no preferred-range errors.
  std::vector<s::Sample> impulse(15 * 9);
  impulse[4 * 15 + 7] = {4,4,4};
  const auto smooth = s::analyze(s::FrameView{impulse,15,9}, options);
  expect(smooth.rawGamut.preferred.pixels == 1 && smooth.filteredGamut.preferred.pixels == 0 &&
         smooth.filteredGamut.validPixels == 135, "R 103 suppresses an isolated impulse");
  // A peak of 16 yields 2, 1.5, 1, .5 across the center row, and 1 at
  // the two adjacent row centers: exactly THREE pixels exceed 246/235.
  impulse[4 * 15 + 7] = {16,16,16};
  const auto stronger = s::analyze(s::FrameView{impulse,15,9}, options);
  expect(stronger.filteredGamut.preferred.pixels == 3, "exact separable FIR coefficients");
  // A plateau cannot be hidden by the transient filter.
  std::fill(impulse.begin(), impulse.end(), s::Sample{1.1,1.1,1.1});
  const auto plateau = s::analyze(s::FrameView{impulse,15,9}, options);
  expect(plateau.filteredGamut.preferred.pixels == 135 && plateau.filteredGamut.outOfGamut(),
         "sustained excursion survives filtering and border replication");
  // Alternating fields: filtering between fields would hide every excursion.
  std::vector<s::Sample> fields(7 * 7);
  for (std::size_t y = 0; y < 7; ++y)
    for (std::size_t x = 0; x < 7; ++x) fields[y * 7 + x] = y % 2 == 0 ? s::Sample{1.1,1.1,1.1} : s::Sample{};
  const auto progressive = s::analyze(s::FrameView{fields,7,7}, options);
  options.gamutFilter = s::GamutFilter::Interlaced;
  const auto interlaced = s::analyze(s::FrameView{fields,7,7}, options);
  expect(progressive.filteredGamut.preferred.pixels == 0 && interlaced.filteredGamut.preferred.pixels == 28,
         "interlaced filtering stays within each field including odd image heights");
  for (const std::size_t height : {std::size_t{1}, std::size_t{2}}) {
    const auto tiny = s::analyze(s::FrameView{fields,1,height,7}, options);
    expect(tiny.filteredGamut.preferred.pixels == 1 && tiny.filteredGamut.validPixels == height,
           "short interlaced images repeat the correct field");
  }
  // Independent brute-force integer-code oracle: no shared convolution,
  // range conversion or row-cache code. Exercise every small edge shape,
  // both field parities and enough rows to wrap the five-row cache.
  for (int height = 1; height <= 9; ++height) {
    for (int width = 1; width <= 9; ++width) {
      std::vector<s::Sample> codes;
      std::vector<int> levels;
      std::uint32_t seed = 113;
      for (int i = 0; i < width * height; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const int code = static_cast<int>(seed >> 24) * 2 - 100;
        levels.push_back(code);
        const double value = code;
        codes.push_back({value,value,value});
      }
      for (const int step : {1,2}) {
        options.gamutFilter = step == 1 ? s::GamutFilter::Progressive : s::GamutFilter::Interlaced;
        options.gamutBitDepth = 8;
        std::uint64_t expectedBelow = 0, expectedAbove = 0;
        constexpr std::array<int, 7> horizontal{1,2,3,4,3,2,1};
        for (int y = 0; y < height; ++y) {
          for (int x = 0; x < width; ++x) {
            int numerator = 0;
            for (int dy = -1; dy <= 1; ++dy) {
              const int candidate = y + dy * step;
              const int sy = candidate < 0 || candidate >= height ? y : candidate;
              for (int dx = -3; dx <= 3; ++dx) {
                const int sx = (std::clamp)(x + dx, 0, width - 1);
                numerator += levels[static_cast<std::size_t>(sy * width + sx)] *
                  horizontal[static_cast<std::size_t>(dx + 3)] * (dy == 0 ? 2 : 1);
              }
            }
            if (numerator < 5 * 64) ++expectedBelow;
            if (numerator > 246 * 64) ++expectedAbove;
          }
        }
        const auto actual = s::analyze(s::FrameView{codes,static_cast<std::size_t>(width),
          static_cast<std::size_t>(height),0,s::Components::Rgb,s::Encoding::NarrowCode,8}, options);
        expect(actual.filteredGamut.preferred.pixels == expectedBelow + expectedAbove &&
               actual.filteredGamut.validPixels == codes.size(), "FIR integer oracle union and denominator");
        for (std::size_t c = 0; c < 4; ++c)
          expect(actual.filteredGamut.preferred.channels[c].below == expectedBelow &&
                 actual.filteredGamut.preferred.channels[c].above == expectedAbove, "FIR integer oracle channel counts");
      }
    }
  }
}

void invalidInputs() {
  auto options = smallOptions();
  const double nan = (std::numeric_limits<double>::quiet_NaN)();
  const double inf = (std::numeric_limits<double>::infinity)();
  const std::array<s::Sample, 4> pixels{{{0,0,0},{nan,0,0},{0,inf,0},{0,0,-inf}}};
  const auto result = s::analyze(s::FrameView{pixels,4,1}, options);
  conserved(result, 1, "non-finite samples");
  expect(result.invalidPixels == 3 && result.rawGamut.invalidPixels == 3 &&
         result.filteredGamut.invalidPixels == 4 && result.filteredGamut.validPixels == 0,
         "invalid samples and their filter footprints are excluded and reported");
  s::FrameView frame{pixels,4,1};
  rejects([&] { auto bad = frame; bad.width = 5; s::analyze(bad, options); }, "short sample buffer rejected");
  rejects([&] { auto bad = frame; bad.height = 2; s::analyze(bad, options); }, "short strided buffer rejected");
  rejects([&] { auto bad = frame; bad.rowStride = 3; s::analyze(bad, options); }, "undersized stride rejected");
  rejects([&] { auto bad = frame; bad.height = 0; s::analyze(bad, options); }, "inconsistent empty size rejected");
  rejects([&] { auto bad = frame; bad.height = 3; bad.rowStride = (std::numeric_limits<std::size_t>::max)();
                s::analyze(bad, options); }, "stride overflow rejected before reading");
  rejects([&] { auto bad = frame; bad.encoding = s::Encoding::NarrowCode; bad.bitDepth = 7;
                s::analyze(bad, options); }, "unsupported code depth rejected");
  rejects([&] { auto bad = frame; bad.encoding = s::Encoding::FullCodeRgb; bad.components = s::Components::YCbCr;
                s::analyze(bad, options); }, "full code YCbCr cannot be mistaken for narrow");
  rejects([&] { auto bad = options; bad.columns = 0; s::analyze(frame, bad); }, "zero bins rejected");
  rejects([&] { auto bad = options; bad.vectorBins = 4097; s::analyze(frame, bad); }, "unbounded grid rejected");
  rejects([&] { auto bad = options; bad.columns = bad.levelBins = 4096; s::analyze(frame, bad); }, "aggregate allocation budget");
  rejects([&] { auto bad = options; bad.levels = {1,1}; s::analyze(frame, bad); }, "zero axis rejected");
  rejects([&] { auto bad = options; bad.chroma = {nan,1}; s::analyze(frame, bad); }, "non-finite axis rejected");
  rejects([&] { auto bad = options; bad.chroma = {1,-1}; s::analyze(frame, bad); }, "reversed axis rejected");
  rejects([&] { auto bad = options; const double largest = (std::numeric_limits<double>::max)();
                bad.chroma = {-largest,largest}; s::analyze(frame, bad); }, "overflowing axis extent rejected");
  rejects([&] { auto bad = options; bad.gamutBitDepth = 32; s::analyze(frame, bad); }, "gamut depth rejected");
  rejects([&] { auto bad = options; bad.matrix = static_cast<s::Matrix>(42); s::analyze(frame, bad); }, "unknown matrix rejected");
  rejects([&] { auto bad = options; bad.gamutFilter = static_cast<s::GamutFilter>(42); s::analyze(frame, bad); }, "unknown filter rejected");
  rejects([&] { auto bad = frame; bad.encoding = static_cast<s::Encoding>(42); s::analyze(bad, options); }, "unknown encoding rejected");
  rejects([&] { auto bad = frame; bad.components = static_cast<s::Components>(42); s::analyze(bad, options); }, "unknown components rejected");
  rejects([&] { s::vectorTargets(s::Matrix::Bt709, static_cast<s::TargetLevel>(42)); }, "unknown target rejected");
  const std::array<std::uint8_t, 3> shortRgba{0,0,0};
  rejects([&] { s::analyze(s::Rgba8View{shortRgba,1,1}, options); }, "short RGBA buffer rejected");
  rejects([&] { s::analyze(s::Rgba8View{shortRgba,1,1,3}, options); }, "short RGBA stride rejected");
}
void diamond() {
  // Corners of the diamonds: black at the bottom point, white at the top,
  // pure green on the left of both, pure blue on the right of the upper one
  // and pure red on the right of the lower one. 2 bins across, 2 down.
  s::Options options = smallOptions();
  options.vectorBins = 2;
  const std::array<std::uint8_t, 20> rgba{0,0,0,255, 255,255,255,255, 0,255,0,255, 0,0,255,255, 255,0,0,255};
  const auto result = s::analyze(s::Rgba8View{rgba, 5, 1, 0}, options);
  const auto& up = result.diamond[0];
  const auto& low = result.diamond[1];
  expect(sum(up.bins) == 5 && sum(low.bins) == 5, "diamond: every pixel counted once in each half");
  // black: x 0 -> column 1 (0 is the high edge of bin 0? no: 0 sits at the bin boundary, column 1), y 0 -> row 0
  expect(up.bins[0 * 2 + 1] >= 1 && low.bins[0 * 2 + 1] >= 1, "diamond: black at the bottom point");
  expect(up.bins[1 * 2 + 1] >= 1 && low.bins[1 * 2 + 1] >= 1, "diamond: white at the top point");
  // green: x -1 (column 0), y 1 (row 1, the boundary bin)
  expect(up.bins[1 * 2 + 0] == 1 && low.bins[1 * 2 + 0] == 1, "diamond: green on the left of both");
  std::array<std::uint8_t, 20> bgra = rgba;
  for (std::size_t i = 0; i < 20; i += 4) std::swap(bgra[i], bgra[i + 2]);
  s::Rgba8View swapped{bgra, 5, 1, 0};
  swapped.bgra = true;
  const auto fromBgra = s::analyze(swapped, options);
  expect(fromBgra.diamond[0].bins == up.bins && fromBgra.diamond[1].bins == low.bins &&
         fromBgra.vectorscope.bins == result.vectorscope.bins, "BGRA input reads the same picture as RGBA");
  expect(up.xExcursions.below == 0 && up.xExcursions.above == 0 && up.yExcursions.above == 0,
         "diamond: legal colours never leave the diamond");
}
// BGRA bars, each `barWidth` pixels wide, EBU 75% order, `rows` high.
std::vector<std::uint8_t> bgraBars(std::size_t barWidth, std::size_t rows, std::size_t& width) {
  static const std::uint8_t ebu[8][3] = {{191, 191, 191}, {191, 191, 0}, {0, 191, 191}, {0, 191, 0},
                                         {191, 0, 191}, {191, 0, 0}, {0, 0, 191}, {0, 0, 0}};  // R G B
  width = barWidth * 8;
  std::vector<std::uint8_t> px(width * rows * 4);
  for (std::size_t y = 0; y < rows; ++y)
    for (std::size_t x = 0; x < width; ++x) {
      const auto* c = ebu[x / barWidth];
      std::uint8_t* p = &px[(y * width + x) * 4];
      p[0] = c[2]; p[1] = c[1]; p[2] = c[0]; p[3] = 255;
    }
  return px;
}

bool sameDensity(const s::Density& a, const s::Density& b) {
  return a.bins == b.bins && a.xExcursions.below == b.xExcursions.below &&
         a.xExcursions.above == b.xExcursions.above && a.yExcursions.below == b.yExcursions.below &&
         a.yExcursions.above == b.yExcursions.above;
}
bool sameHistogram(const s::Histogram& a, const s::Histogram& b) {
  return a.bins == b.bins && a.excursions.below == b.excursions.below && a.excursions.above == b.excursions.above;
}
bool sameGamut(const s::GamutCounts& a, const s::GamutCounts& b) {
  const auto range = [](const s::GamutRangeCounts& x, const s::GamutRangeCounts& y) {
    bool same = x.pixels == y.pixels;
    for (std::size_t c = 0; c < 4; ++c)
      same = same && x.channels[c].below == y.channels[c].below && x.channels[c].above == y.channels[c].above;
    return same;
  };
  return a.validPixels == b.validPixels && a.invalidPixels == b.invalidPixels &&
         range(a.nominal, b.nominal) && range(a.preferred, b.preferred) && range(a.total, b.total);
}

// The 8-bit byte path is tabled for speed; it must equal the general path
// fed the same pixels as 8-bit full-code samples, in every bin and count.
void bytePathMatchesGeneral() {
  const std::size_t w = 61, h = 17;
  std::vector<std::uint8_t> px(w * h * 4);
  for (std::size_t i = 0; i < px.size(); ++i) px[i] = static_cast<std::uint8_t>((i * 2654435761u) >> 23);
  for (std::size_t i = 0; i < 64; ++i) px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = static_cast<std::uint8_t>(i * 4);
  std::vector<s::Sample> codes(w * h);
  for (bool bgra : {false, true}) {
    for (std::size_t i = 0; i < w * h; ++i) {
      const std::uint8_t r = px[i * 4 + (bgra ? 2 : 0)], g = px[i * 4 + 1], b = px[i * 4 + (bgra ? 0 : 2)];
      codes[i] = {double(r), double(g), double(b)};
    }
    for (auto filter : {s::GamutFilter::None, s::GamutFilter::Progressive}) {
      for (auto matrix : {s::Matrix::Bt709, s::Matrix::Bt2020Ncl}) {
        s::Options options;
        options.columns = 37;
        options.levelBins = options.vectorBins = options.histogramBins = 64;
        options.gamutFilter = filter;
        options.matrix = matrix;
        s::Rgba8View view{px, w, h, 0, bgra};
        const auto fast = s::analyze(view, options);
        const auto general = s::analyze(
          s::FrameView{codes, w, h, 0, s::Components::Rgb, s::Encoding::FullCodeRgb, 8}, options);
        bool same = fast.validPixels == general.validPixels && sameDensity(fast.vectorscope, general.vectorscope) &&
                    sameHistogram(fast.lumaHistogram, general.lumaHistogram) &&
                    sameGamut(fast.rawGamut, general.rawGamut) && sameGamut(fast.filteredGamut, general.filteredGamut);
        for (std::size_t c = 0; c < 3; ++c)
          same = same && sameDensity(fast.ycbcrParade[c], general.ycbcrParade[c]) &&
                 sameDensity(fast.rgbParade[c], general.rgbParade[c]) &&
                 sameHistogram(fast.rgbHistogram[c], general.rgbHistogram[c]);
        for (std::size_t d = 0; d < 2; ++d) same = same && sameDensity(fast.diamond[d], general.diamond[d]);
        expect(same, "8-bit byte path equals the general path, bin for bin");
      }
    }
  }
  // A levels range narrower than the signal: the tabled bins carry excursions too.
  s::Options narrow;
  narrow.levels = {0.1, 0.9};
  const auto fast = s::analyze(s::Rgba8View{px, w, h, 0, false}, narrow);
  for (std::size_t i = 0; i < w * h; ++i) codes[i] = {double(px[i * 4]), double(px[i * 4 + 1]), double(px[i * 4 + 2])};
  const auto general = s::analyze(s::FrameView{codes, w, h, 0, s::Components::Rgb, s::Encoding::FullCodeRgb, 8}, narrow);
  expect(sameDensity(fast.rgbParade[0], general.rgbParade[0]) && sameHistogram(fast.rgbHistogram[2], general.rgbHistogram[2]) &&
         fast.rgbHistogram[2].excursions.above > 0, "byte path excursions match outside a narrow levels range");
}

void sent422() {
  s::Options options = smallOptions();
  options.gamutFilter = s::GamutFilter::None;
  // Bars whose edges fall between pixel pairs: 4:2:2 changes nothing but the
  // quantisation, so the levels match the R'G'B' frame. Quantising to codes
  // rebuilds a zero channel 0.04-0.1% below black (measured), so NOMINAL
  // gamut sees it, as it does on real SDI; R 103's preferred range must not.
  std::size_t w = 0;
  const auto aligned = bgraBars(8, 4, w);
  for (unsigned depth : {8u, 10u}) {
    const auto sent = s::analyze(s::Bgra422View{aligned, w, 4, 0, depth}, options);
    const auto rgb = s::analyze(s::Rgba8View{aligned, w, 4, 0, true}, options);
    conserved(sent, w * 4, "4:2:2 bars");
    expect(sent.rawGamut.preferred.pixels == 0 && sent.rawGamut.total.pixels == 0,
           "4:2:2 bars on pair boundaries are R 103 legal");
    expect(sent.lumaHistogram.bins == rgb.lumaHistogram.bins,
           "4:2:2 bars on pair boundaries: luma matches the R'G'B' frame");
  }
  // Bars seven pixels wide: the edges at odd x split a pair, the pair's shared
  // chroma lands on a neighbour it does not belong to, and the rebuilt R'G'B'
  // overshoots -- by about 2.7% for 75% bars (measured), which R 103's 5%
  // margin allows: 75% bars are legal, however they are cut.
  const auto split = bgraBars(7, 4, w);
  const auto sentSplit = s::analyze(s::Bgra422View{split, w, 4, 0, 10}, options);
  const auto rgbSplit = s::analyze(s::Rgba8View{split, w, 4, 0, true}, options);
  expect(rgbSplit.rawGamut.preferred.pixels == 0, "the 8-bit R'G'B' frame cannot leave gamut");
  expect(sentSplit.rawGamut.nominal.pixels > 0 && sentSplit.rawGamut.preferred.pixels == 0,
         "4:2:2 75% bars cut inside pairs: edge overshoot, still R 103 legal");
  // The worst case: saturated red against black on every other column.
  std::vector<std::uint8_t> stripes(16 * 2 * 4);
  for (std::size_t i = 0; i < 32; ++i)
    if (i % 2 == 0) stripes[i * 4 + 2] = 255;
  for (std::size_t i = 0; i < 32; ++i) stripes[i * 4 + 3] = 255;
  const auto sentStripes = s::analyze(s::Bgra422View{stripes, 16, 2, 0, 10}, options);
  // Each black pixel carries half the red's chroma: G' and B' near -10.6%,
  // past the -5% preferred floor. The red pixels rebuild inside. This is what
  // R 103 exists to catch, and the R'G'B' frame itself can never show it.
  expect(sentStripes.rawGamut.preferred.pixels == 16,
         "red/black single-pixel stripes: every black pixel outside preferred gamut");
  expect(s::analyze(s::Rgba8View{stripes, 16, 2, 0, true}, options).rawGamut.preferred.pixels == 0,
         "...while the same R'G'B' frame reads legal");
  // Pixel by pixel: the decode is the shared encoder's codes, read back.
  const std::uint8_t pairPixels[8] = {0, 0, 255, 255, 0, 0, 0, 255};  // red, black (BGRA)
  const auto codes = deckboy::core::encodeBgraPair709(pairPixels, pairPixels + 4, true);
  const auto one = s::analyze(s::Bgra422View{std::span<const std::uint8_t>(pairPixels, 8), 2, 1, 0, 10}, options);
  expect(codes.y1 == 64 && codes.cr > 512 && one.rawGamut.nominal.channels[2].below == 1,
         "the black pixel of a red|black pair rebuilds with negative G'");
  // A grey ramp has no chroma to share.
  std::vector<std::uint8_t> ramp(256 * 4);
  for (std::size_t i = 0; i < 256; ++i) ramp[i * 4] = ramp[i * 4 + 1] = ramp[i * 4 + 2] = static_cast<std::uint8_t>(i), ramp[i * 4 + 3] = 255;
  s::Options wide = options;
  wide.vectorBins = 16;
  const auto sentRamp = s::analyze(s::Bgra422View{ramp, 256, 1, 0, 10}, wide);
  expect(sentRamp.vectorscope.bins[8 * 16 + 8] == 256 && sentRamp.rawGamut.nominal.pixels == 0,
         "4:2:2 grey ramp: all chroma at the origin, in gamut");
  // An odd last pixel pairs with itself, as ST 2110 sends it.
  const auto odd = s::analyze(s::Bgra422View{std::span<const std::uint8_t>(ramp.data(), 12), 3, 1, 0, 10}, options);
  expect(odd.validPixels == 3, "odd width: every pixel analysed");
  rejects([&] { s::analyze(s::Bgra422View{ramp, 256, 1, 0, 9}, options); }, "4:2:2 bit depth other than 8/10 rejected");
  // Row bands accumulate exactly here too (pairs are horizontal).
  const auto whole = s::analyze(s::Bgra422View{split, w, 4, 0, 10}, options);
  auto merged = s::analyze(s::Bgra422View{std::span<const std::uint8_t>(split.data(), w * 4 * 2), w, 2, 0, 10}, options);
  s::accumulate(merged, s::analyze(s::Bgra422View{std::span<const std::uint8_t>(split.data() + w * 4 * 2, w * 4 * 2), w, 2, 0, 10}, options));
  expect(merged.rawGamut.nominal.pixels == whole.rawGamut.nominal.pixels &&
         merged.lumaHistogram.bins == whole.lumaHistogram.bins, "4:2:2 row bands equal the whole frame");
}

void bandsMatchWhole() {
  // A frame split into row bands and accumulated equals the whole frame.
  const std::size_t w = 37, h = 23;
  std::vector<std::uint8_t> px(w * h * 4);
  for (std::size_t i = 0; i < px.size(); ++i) px[i] = static_cast<std::uint8_t>((i * 2654435761u) >> 24);
  s::Options options = smallOptions();
  options.gamutFilter = s::GamutFilter::None;
  const auto whole = s::analyze(s::Rgba8View{px, w, h, 0}, options);
  s::Result merged;
  for (std::size_t y0 = 0; y0 < h; y0 += 6) {
    const std::size_t rows = std::min<std::size_t>(6, h - y0);
    const std::span<const std::uint8_t> band(px.data() + y0 * w * 4, rows * w * 4);
    const auto part = s::analyze(s::Rgba8View{band, w, rows, 0}, options);
    if (y0 == 0) merged = part; else s::accumulate(merged, part);
  }
  bool same = merged.validPixels == whole.validPixels && merged.vectorscope.bins == whole.vectorscope.bins &&
              merged.lumaHistogram.bins == whole.lumaHistogram.bins &&
              merged.diamond[0].bins == whole.diamond[0].bins && merged.diamond[1].bins == whole.diamond[1].bins &&
              merged.rawGamut.preferred.pixels == whole.rawGamut.preferred.pixels;
  for (std::size_t c = 0; c < 3; ++c)
    same = same && merged.rgbParade[c].bins == whole.rgbParade[c].bins &&
           merged.ycbcrParade[c].bins == whole.ycbcrParade[c].bins &&
           merged.rgbHistogram[c].bins == whole.rgbHistogram[c].bins;
  expect(same, "row bands accumulated equal the whole frame, bin for bin");
  s::Options other = options;
  other.vectorBins = 8;
  rejects([&] { s::accumulate(merged, s::analyze(s::Rgba8View{px, w, h, 0}, other)); },
          "accumulating different shapes rejected");
}
} // namespace

int main() {
  try {
    bars();
    rampsAndStorage();
    digitalAndGamut();
    filtering();
    invalidInputs();
    diamond();
    bandsMatchWhole();
    sent422();
    bytePathMatchesGeneral();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "[fail] unexpected exception: " << error.what() << '\n';
  }
  std::cout << "scopes checks: " << checks << "; failures: " << failures << '\n';
  return failures ? 1 : 0;
}
