// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// The GPU scopes analyser (engine/gpu_scopes) against the CPU one
// (core::scopes::analyze on the same BGRA bytes), bin for bin, on the
// renderer an output would get. Every 8-bit colour, odd rasters, all four
// orientations, a flat field (the worst case for the shader's global adds),
// and the false colour picture's nearest samples.
//
//   deckboy-scopes-gpu-check [--require] [--frame <file.bgra> <w> <h>] [--bench <file.bgra> <w> <h>]
//
// Without --require, a renderer with no GPU analyser is reported and passes.
#include "core/scopes.hpp"
#include "engine/gpu_scopes.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
namespace s = deckboy::core::scopes;
namespace g = deckboy::gpu;
int failures = 0, checks = 0;
void expect(bool condition, const std::string& label) {
  ++checks;
  if (!condition) {
    ++failures;
    std::cerr << "[fail] " << label << '\n';
  }
}

bool sameDensity(const s::Density& a, const s::Density& b) {
  return a.bins == b.bins && a.xExcursions.below == b.xExcursions.below &&
         a.xExcursions.above == b.xExcursions.above && a.yExcursions.below == b.yExcursions.below &&
         a.yExcursions.above == b.yExcursions.above;
}
bool sameHistogram(const s::Histogram& a, const s::Histogram& b) {
  return a.bins == b.bins && a.excursions.below == b.excursions.below && a.excursions.above == b.excursions.above;
}
// Names the first thing that differs, or returns empty.
std::string difference(const s::Result& gpu, const s::Result& cpu) {
  if (gpu.validPixels != cpu.validPixels || gpu.invalidPixels != cpu.invalidPixels) return "pixel counts";
  static const char* const ycc[3] = {"Y' waveform", "Cb waveform", "Cr waveform"};
  static const char* const rgb[3] = {"R' parade", "G' parade", "B' parade"};
  for (std::size_t c = 0; c < 3; ++c) {
    if (!sameDensity(gpu.ycbcrParade[c], cpu.ycbcrParade[c])) return ycc[c];
    if (!sameDensity(gpu.rgbParade[c], cpu.rgbParade[c])) return rgb[c];
    if (!sameHistogram(gpu.rgbHistogram[c], cpu.rgbHistogram[c])) return std::string(rgb[c]) + " histogram";
  }
  if (!sameHistogram(gpu.lumaHistogram, cpu.lumaHistogram)) return "luma histogram";
  if (!sameDensity(gpu.vectorscope, cpu.vectorscope)) return "vectorscope";
  for (std::size_t d = 0; d < 2; ++d)
    if (!sameDensity(gpu.diamond[d], cpu.diamond[d])) return d ? "lower diamond" : "upper diamond";
  if (gpu.rawGamut.validPixels != cpu.rawGamut.validPixels || gpu.rawGamut.preferred.pixels != cpu.rawGamut.preferred.pixels ||
      gpu.filteredGamut.validPixels != cpu.filteredGamut.validPixels)
    return "gamut counts";
  return {};
}

// The CPU's turn of a BGRA frame (rotateCapturedFramePixels' mapping).
std::vector<std::uint8_t> turned(const std::vector<std::uint8_t>& px, int w, int h, int degrees, int& ow, int& oh) {
  ow = (degrees == 90 || degrees == 270) ? h : w;
  oh = (degrees == 90 || degrees == 270) ? w : h;
  std::vector<std::uint8_t> out(px.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int dx = x, dy = y;
      if (degrees == 90) { dx = h - 1 - y; dy = x; }
      else if (degrees == 180) { dx = w - 1 - x; dy = h - 1 - y; }
      else if (degrees == 270) { dx = y; dy = w - 1 - x; }
      std::memcpy(&out[(static_cast<std::size_t>(dy) * ow + dx) * 4], &px[(static_cast<std::size_t>(y) * w + x) * 4], 4);
    }
  return out;
}

struct Gpu {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
};

// The picture in a render-target texture, as the output's scopes capture is.
SDL_Texture* targetWith(SDL_Renderer* renderer, const std::vector<std::uint8_t>& bgra, int w, int h) {
  SDL_Texture* upload = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, w, h);
  SDL_Texture* target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_TARGET, w, h);
  if (!upload || !target) {
    if (upload) SDL_DestroyTexture(upload);
    if (target) SDL_DestroyTexture(target);
    return nullptr;
  }
  SDL_UpdateTexture(upload, nullptr, bgra.data(), w * 4);
  SDL_SetTextureScaleMode(upload, SDL_SCALEMODE_NEAREST);
  SDL_SetTextureBlendMode(upload, SDL_BLENDMODE_NONE);
  SDL_SetRenderTarget(renderer, target);
  SDL_RenderTexture(renderer, upload, static_cast<const SDL_FRect*>(nullptr), static_cast<const SDL_FRect*>(nullptr));
  SDL_SetRenderTarget(renderer, nullptr);
  SDL_FlushRenderer(renderer);
  SDL_DestroyTexture(upload);
  return target;
}

// The render thread's own time inside submit and collect, which is what an
// output pays for its scopes; the rest of a round trip is the GPU's.
double renderThreadMs = 0;

// Submit once and wait for the counts (a test may wait; the app never does).
bool analyseOnce(void* scopes, SDL_Texture* target, int orientation, int columns, s::Matrix matrix,
                 int thumbW, int thumbH, g::GpuScopeCounts& counts) {
  const auto timed = [](auto&& call) {
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = call();
    renderThreadMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return ok;
  };
  if (!timed([&] { return g::submitGpuScopes(scopes, target, orientation, columns, matrix, thumbW, thumbH, 1); }))
    return false;
  for (int i = 0; i < 5000; ++i) {
    if (timed([&] { return g::collectGpuScopes(scopes, counts); })) return true;
    SDL_Delay(1);
  }
  return false;
}

void compare(SDL_Renderer* renderer, const std::vector<std::uint8_t>& bgra, int w, int h, int columns,
             const std::string& label, bool allOrientations = true) {
  SDL_Texture* target = targetWith(renderer, bgra, w, h);
  void* scopes = target ? g::createGpuScopes(renderer, w, h) : nullptr;
  expect(scopes != nullptr, label + ": the analyser opens");
  if (!scopes) {
    if (target) SDL_DestroyTexture(target);
    return;
  }
  for (int orientation : {0, 90, 180, 270}) {
    if (!allOrientations && orientation) break;
    int ow = w, oh = h;
    const auto oriented = orientation ? turned(bgra, w, h, orientation, ow, oh) : bgra;
    for (auto matrix : {s::Matrix::Bt709, s::Matrix::Bt2020Ncl}) {
      s::Options options;
      options.columns = static_cast<std::size_t>(std::clamp(columns, 1, ow));
      options.gamutFilter = s::GamutFilter::None;
      options.matrix = matrix;
      const std::string name = label + " at " + std::to_string(orientation) + " degrees, " +
                               (matrix == s::Matrix::Bt709 ? "BT.709" : "BT.2020");
      expect(g::gpuScopesSupport(options, ow), name + ": the monitor's axes are supported");
      const int thumbW = std::min(ow, 480);
      const int thumbH = std::max(1, static_cast<int>(static_cast<long long>(oh) * thumbW / ow));
      g::GpuScopeCounts counts;
      if (!analyseOnce(scopes, target, orientation, static_cast<int>(options.columns), matrix, thumbW, thumbH, counts)) {
        expect(false, name + ": the GPU answers");
        continue;
      }
      const auto gpu = g::unpackGpuScopes(counts, options);
      const auto cpu = s::analyze(s::Rgba8View{oriented, static_cast<std::size_t>(ow), static_cast<std::size_t>(oh), 0, true},
                                  options);
      const auto diff = difference(gpu, cpu);
      expect(diff.empty(), name + ": GPU equals CPU" + (diff.empty() ? "" : " (differs in the " + diff + ")"));
      bool sampled = counts.thumbWidth == thumbW && counts.thumbHeight == thumbH &&
                     counts.thumb.size() == static_cast<std::size_t>(thumbW) * thumbH * 4;
      for (int y = 0; sampled && y < thumbH; ++y)
        for (int x = 0; sampled && x < thumbW; ++x) {
          const int sy = std::min(oh - 1, y * oh / thumbH), sx = std::min(ow - 1, x * ow / thumbW);
          sampled = std::memcmp(&counts.thumb[(static_cast<std::size_t>(y) * thumbW + x) * 4],
                                &oriented[(static_cast<std::size_t>(sy) * ow + sx) * 4], 3) == 0;
        }
      expect(sampled, name + ": the false colour samples are the frame's own pixels");
    }
  }
  g::destroyGpuScopes(scopes);
  SDL_DestroyTexture(target);
}

std::vector<std::uint8_t> noise(int w, int h, unsigned seed) {
  std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
  std::uint32_t x = seed;
  for (std::size_t i = 0; i < px.size(); ++i) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    px[i] = (i % 4 == 3) ? 255 : static_cast<std::uint8_t>(x >> 24);
  }
  return px;
}

bool readFrame(const char* path, int w, int h, std::vector<std::uint8_t>& px) {
  std::ifstream in(path, std::ios::binary);
  px.resize(static_cast<std::size_t>(w) * h * 4);
  return in.read(reinterpret_cast<char*>(px.data()), static_cast<std::streamsize>(px.size())).good();
}

// Analyses a second, submitted as fast as the ring takes them.
int bench(SDL_Renderer* renderer, const std::vector<std::uint8_t>& px, int w, int h) {
  SDL_Texture* target = targetWith(renderer, px, w, h);
  void* scopes = target ? g::createGpuScopes(renderer, w, h) : nullptr;
  if (!scopes) {
    std::cerr << "bench: no GPU analyser on this renderer\n";
    return 1;
  }
  const int columns = std::min(w, 960);
  g::GpuScopeCounts counts;
  analyseOnce(scopes, target, 0, columns, s::Matrix::Bt709, 480, 270, counts);   // warm
  renderThreadMs = 0;
  const auto started = std::chrono::steady_clock::now();
  int done = 0;
  double roundTrip = 0;
  while (std::chrono::steady_clock::now() - started < std::chrono::seconds(3)) {
    const auto t0 = std::chrono::steady_clock::now();
    if (analyseOnce(scopes, target, 0, columns, s::Matrix::Bt709, 480, 270, counts)) {
      roundTrip += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      ++done;
    }
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  const auto t0 = std::chrono::steady_clock::now();
  s::Options options;
  options.columns = static_cast<std::size_t>(columns);
  options.gamutFilter = s::GamutFilter::None;
  for (int i = 0; i < 20; ++i) (void)g::unpackGpuScopes(counts, options);
  const double unpack = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20;
  std::cout << w << "x" << h << " on " << SDL_GetRendererName(renderer) << ": " << done / seconds
            << " analyses a second one at a time, " << roundTrip / std::max(done, 1)
            << " ms submit to counts, " << renderThreadMs / std::max(done, 1)
            << " ms of it on the render thread; unpacking " << unpack << " ms\n";
  g::destroyGpuScopes(scopes);
  SDL_DestroyTexture(target);
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  bool require = false;
  const char* framePath = nullptr;
  int frameW = 0, frameH = 0;
  bool benchOnly = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--require") require = true;
    else if ((arg == "--frame" || arg == "--bench") && i + 3 < argc) {
      benchOnly = arg == "--bench";
      framePath = argv[i + 1];
      frameW = std::atoi(argv[i + 2]);
      frameH = std::atoi(argv[i + 3]);
      i += 3;
    }
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "scopes-gpu: SDL_Init failed: " << SDL_GetError() << '\n';
    return require ? 1 : 0;
  }
  Gpu gpu;
  gpu.window = SDL_CreateWindow("scopes gpu check", 64, 64, SDL_WINDOW_HIDDEN);
#ifdef _WIN32
  gpu.renderer = gpu.window ? SDL_CreateRenderer(gpu.window, "direct3d11") : nullptr;
#else
  gpu.renderer = gpu.window ? SDL_CreateRenderer(gpu.window, "gpu") : nullptr;
#endif
  if (!gpu.renderer && gpu.window) gpu.renderer = SDL_CreateRenderer(gpu.window, nullptr);
  if (!gpu.renderer) {
    std::cerr << "scopes-gpu: no renderer: " << SDL_GetError() << '\n';
    return require ? 1 : 0;
  }
  int code = 0;
  void* probe = g::createGpuScopes(gpu.renderer, 16, 16);
  if (!probe) {
    std::cout << "scopes-gpu: no GPU analyser on the " << SDL_GetRendererName(gpu.renderer)
              << " renderer; the CPU one measures here\n";
    code = require ? 1 : 0;
  } else {
    std::cout << "scopes-gpu: " << SDL_GetRendererName(gpu.renderer) << " on " << g::gpuScopesAdapter(probe) << '\n';
    g::destroyGpuScopes(probe);
    std::vector<std::uint8_t> px;
    if (benchOnly) {
      if (!readFrame(framePath, frameW, frameH, px)) {
        std::cerr << "bench: cannot read " << framePath << '\n';
        code = 1;
      } else {
        code = bench(gpu.renderer, px, frameW, frameH);
      }
    } else {
      // Every 8-bit colour once: R in the high byte of the pixel index.
      std::vector<std::uint8_t> every(4096u * 4096u * 4u);
      for (std::size_t i = 0; i < 4096u * 4096u; ++i) {
        every[i * 4] = static_cast<std::uint8_t>(i);          // B
        every[i * 4 + 1] = static_cast<std::uint8_t>(i >> 8);  // G
        every[i * 4 + 2] = static_cast<std::uint8_t>(i >> 16); // R
        every[i * 4 + 3] = 255;
      }
      compare(gpu.renderer, every, 4096, 4096, 960, "every 8-bit colour", false);
      compare(gpu.renderer, noise(1280, 720, 7), 1280, 720, 960, "720p noise, 960 columns");
      compare(gpu.renderer, noise(61, 17, 11), 61, 17, 37, "a 61x17 raster, 37 columns");
      compare(gpu.renderer, noise(3, 2, 5), 3, 2, 3, "a 3x2 raster");
      std::vector<std::uint8_t> flat(3840u * 2160u * 4u);
      for (std::size_t i = 0; i < flat.size(); i += 4) {
        flat[i] = 40; flat[i + 1] = 200; flat[i + 2] = 120; flat[i + 3] = 255;
      }
      compare(gpu.renderer, flat, 3840, 2160, 960, "a flat 4K field", false);
      // The fingerprint moves with one pixel and not without one.
      {
        auto a = noise(320, 180, 3);
        SDL_Texture* ta = targetWith(gpu.renderer, a, 320, 180);
        a[(90 * 320 + 160) * 4 + 1] ^= 1;
        SDL_Texture* tb = targetWith(gpu.renderer, a, 320, 180);
        void* scopes = g::createGpuScopes(gpu.renderer, 320, 180);
        g::GpuScopeCounts first, again, changed;
        const bool ran = scopes && analyseOnce(scopes, ta, 0, 320, s::Matrix::Bt709, 0, 0, first) &&
                         analyseOnce(scopes, ta, 0, 320, s::Matrix::Bt709, 0, 0, again) &&
                         analyseOnce(scopes, tb, 0, 320, s::Matrix::Bt709, 0, 0, changed);
        expect(ran && first.fingerprint == again.fingerprint, "the same picture keeps its fingerprint");
        expect(ran && first.fingerprint != changed.fingerprint, "one changed pixel moves the fingerprint");
        g::destroyGpuScopes(scopes);
        SDL_DestroyTexture(ta);
        SDL_DestroyTexture(tb);
      }
      if (framePath) {
        if (readFrame(framePath, frameW, frameH, px)) compare(gpu.renderer, px, frameW, frameH, 960, framePath);
        else expect(false, std::string("read ") + framePath);
      }
      std::cout << "scopes-gpu on " << SDL_GetRendererName(gpu.renderer) << ": " << checks
                << " checks; failures: " << failures << '\n';
      code = failures ? 1 : 0;
    }
  }
  SDL_DestroyRenderer(gpu.renderer);
  SDL_DestroyWindow(gpu.window);
  SDL_Quit();
  return code;
}
