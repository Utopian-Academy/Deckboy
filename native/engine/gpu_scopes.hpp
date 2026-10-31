// gpu_scopes.hpp -- the picture scopes' analysis, on the GPU.
//
// The CPU analyser reads every pixel of the frame, and that is the cost: a 4K
// frame is 8.3 million pixels, 23 ms on six threads, so a 4K60 output could
// only be measured about forty times a second -- and first the whole frame
// (33 MB) had to come back from the GPU. Here a compute shader bins the
// output's own capture texture where it already is, and only the COUNTS come
// back: about 6.7 MB of them at 960 waveform columns, none of it picture.
//
// It is the 8-bit R'G'B' path only (the 4:2:2 wire and the HDR signal stay
// on the CPU), on the monitor's own axes: levels 0..1, chroma -0.5..0.5, 256
// bins on every axis. On those the bins are integer ratios (core/scopes.hpp,
// exactCoefficients), so the shader's integer division gives exactly what
// core::scopes::analyze(Rgba8View) gives, bin for bin. tools/scopes_gpu_check
// holds the two side by side, including on every 8-bit colour.
//
// Direct3D 11 today; null elsewhere, where the CPU analyser stays in charge.
#ifndef DECKBOY_ENGINE_GPU_SCOPES_HPP
#define DECKBOY_ENGINE_GPU_SCOPES_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../core/scopes.hpp"
#include "../core/sdl_compat.hpp"

namespace deckboy::gpu {

// What one analysis hands back: raw counts, unpacked off the render thread
// by unpackGpuScopes.
struct GpuScopeCounts {
  int width = 0, height = 0;   // the analysed (oriented) frame
  int columns = 0;
  core::scopes::Matrix matrix = core::scopes::Matrix::Bt709;
  std::uint64_t submittedAtNs = 0;
  std::uint64_t fingerprint = 0;   // moves when any pixel does
  // Y', Cb, Cr, R', G', B' waveform planes, each 256 rows of `columns`; then
  // the vectorscope (Cr row, Cb column) and the two diamonds, 256 x 256 each.
  std::vector<std::uint32_t> wave, cells;
  // The frame nearest-sampled to thumbWidth x thumbHeight, BGRA: the false
  // colour picture's source, picked exactly as buildFalseColour picks.
  std::vector<std::uint8_t> thumb;
  int thumbWidth = 0, thumbHeight = 0;
};

// Can the GPU analyser give exactly these results? The monitor's own axes.
bool gpuScopesSupport(const core::scopes::Options& options, int width);

// width x height is the SOURCE texture (before orientation); columns as the
// analyser is asked for. Null when this renderer has no compute path.
void* createGpuScopes(SDL_Renderer* renderer, int width, int height);
void destroyGpuScopes(void* handle);
// The graphics adapter it runs on, for the log and the checks.
std::string gpuScopesAdapter(void* handle);

// Queue one analysis of `source` (BGRA, the size the analyser was made for)
// turned by `orientation` degrees, as rotateCapturedFramePixels turns it.
// thumbWidth 0 skips the false colour picture. False when nothing could be
// queued (every slot still in flight) -- the frame is simply not measured.
bool submitGpuScopes(void* handle, SDL_Texture* source, int orientation, int columns,
                     core::scopes::Matrix matrix, int thumbWidth, int thumbHeight,
                     std::uint64_t submittedAtNs);
// A finished analysis, oldest first, if the GPU has one; never waits.
bool collectGpuScopes(void* handle, GpuScopeCounts& out);
// How many submissions have not been collected yet.
int gpuScopesInFlight(void* handle);

// Counts to the Result the CPU analyser would have produced. Any thread.
core::scopes::Result unpackGpuScopes(const GpuScopeCounts& counts, const core::scopes::Options& options);

}  // namespace deckboy::gpu

#endif  // DECKBOY_ENGINE_GPU_SCOPES_HPP
