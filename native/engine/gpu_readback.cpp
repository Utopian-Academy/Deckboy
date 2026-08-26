#include "gpu_readback.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <d3d11.h>
#endif

namespace deckboy::gpu {
namespace {
#ifdef _WIN32
// The renderer's device, or null when this is not a D3D11 renderer. Duplicated
// rather than shared with the decoder's copy: this file must build with
// in-process decode switched off, and it is four lines.
ID3D11Device* rendererDevice(SDL_Renderer* renderer) {
  if (!renderer) {
    return nullptr;
  }
  return reinterpret_cast<ID3D11Device*>(
    SDL_GetPointerProperty(SDL_GetRendererProperties(renderer),
                           SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr));
}
#endif
}  // namespace

// SDL_RenderReadPixels is SYNCHRONOUS: it asks the GPU for the surface and
// blocks the calling thread until the copy has drained. MEASURED on the
// recording path, that is 11.8ms for one 1080 frame and 21-24ms for 4K, on the
// render thread, every frame -- roughly 700MB/s, which is nowhere near bus
// bandwidth and is simply the pipeline stall. It put a hard ~45fps ceiling on a
// 4K recording and ~18fps in practice once the encoder write is added, so 50
// and 59.94 were unreachable no matter what the muxer was told.
//
// The fix is the standard one: CopyResource into a ring of STAGING textures and
// Map the OLDEST with DO_NOT_WAIT, so the CPU only ever touches a frame the GPU
// finished with two frames ago and never waits. Costs two frames of latency,
// which is meaningless for a file recording.
// ============================================================================

namespace {
// Three deep in both implementations: one slot being written by the GPU, one in
// flight, one safe to read. Published in the header because the recording
// pacer has to allow for this latency (see kAsyncReadbackDepth).
constexpr int kStagingRingSize = kAsyncReadbackDepth;
#ifdef _WIN32
struct StagingReadback {
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  ID3D11Texture2D* ring[kStagingRingSize] = {nullptr, nullptr, nullptr};
  int width = 0;
  int height = 0;
  int cursor = 0;
  int primed = 0;         // frames issued but not yet read back
};
#endif
// ----------------------------------------------------------------------------
// SDL_GPU path: one asynchronous readback for Metal, Vulkan and D3D12.
//
// The D3D11 ring below only ever helped Windows, and 4K at 50 and 60 was
// unreachable everywhere else -- the synchronous read costs 16.8-18.8ms a frame
// and 60fps has 16.7ms to spend in total. SDL3 has no way to reach the
// MTLTexture behind an SDL texture, so a Metal-specific path is not even
// expressible through the public API; but the SDL_GPU renderer exposes both its
// device and its textures, and SDL_DownloadFromGPUTexture is genuinely
// asynchronous -- it runs on the GPU timeline and hands back a fence.
//
// Same shape as the D3D11 ring: issue this frame's download, then read the
// OLDEST slot only if its fence has already signalled. QueryGPUFence never
// blocks, so a slow frame degrades into a repeat rather than a stall.
// SDL 3.4 is the floor: SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER does not exist in
// 3.2, and building against it there is a compile error rather than a graceful
// absence. CI found exactly that -- it built SDL from release-3.2.x while this
// desk had 3.4 from vcpkg. CI now requires 3.4; this guard is for anyone
// building against an older SDL, who falls back to the synchronous read.
#if SDL_VERSION_ATLEAST(3, 4, 0)
struct GpuDownloadReadback {
  SDL_Renderer* renderer = nullptr;
  SDL_GPUDevice* device = nullptr;
  SDL_GPUTransferBuffer* ring[kStagingRingSize] = {nullptr, nullptr, nullptr};
  SDL_GPUFence* fence[kStagingRingSize] = {nullptr, nullptr, nullptr};
  int width = 0;
  int height = 0;
  int cursor = 0;
  int primed = 0;
};

SDL_GPUDevice* rendererGpuDevice(SDL_Renderer* renderer) {
  if (!renderer) {
    return nullptr;
  }
  return static_cast<SDL_GPUDevice*>(
    SDL_GetPointerProperty(SDL_GetRendererProperties(renderer),
                           SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr));
}

SDL_GPUTexture* textureGpuHandle(SDL_Texture* texture) {
  if (!texture) {
    return nullptr;
  }
  return static_cast<SDL_GPUTexture*>(
    SDL_GetPointerProperty(SDL_GetTextureProperties(texture),
                           SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
}

void* createGpuDownloadReadback(SDL_Renderer* renderer, int width, int height) {
  SDL_GPUDevice* device = rendererGpuDevice(renderer);
  if (!device || width <= 0 || height <= 0) {
    return nullptr;
  }
  auto* rb = new GpuDownloadReadback();
  rb->renderer = renderer;
  rb->device = device;
  rb->width = width;
  rb->height = height;
  const Uint32 bytes = static_cast<Uint32>(width) * static_cast<Uint32>(height) * 4u;
  for (int i = 0; i < kStagingRingSize; ++i) {
    SDL_GPUTransferBufferCreateInfo info {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = bytes;
    rb->ring[i] = SDL_CreateGPUTransferBuffer(device, &info);
    if (!rb->ring[i]) {
      for (int j = 0; j < i; ++j) {
        SDL_ReleaseGPUTransferBuffer(device, rb->ring[j]);
      }
      delete rb;
      return nullptr;
    }
  }
  return rb;
}

void destroyGpuDownloadReadback(void* handle) {
  auto* rb = static_cast<GpuDownloadReadback*>(handle);
  if (!rb) return;
  for (int i = 0; i < kStagingRingSize; ++i) {
    // A fence still in flight must be waited on before its buffer goes away,
    // or the GPU writes into freed memory. This is the one place we DO block.
    if (rb->fence[i]) {
      SDL_WaitForGPUFences(rb->device, true, &rb->fence[i], 1);
      SDL_ReleaseGPUFence(rb->device, rb->fence[i]);
      rb->fence[i] = nullptr;
    }
    if (rb->ring[i]) {
      SDL_ReleaseGPUTransferBuffer(rb->device, rb->ring[i]);
      rb->ring[i] = nullptr;
    }
  }
  delete rb;
}

bool gpuDownloadReadbackFrame(void* handle, SDL_Texture* source,
                              std::uint8_t* out, std::size_t outBytes,
                              int width, int height) {
  auto* rb = static_cast<GpuDownloadReadback*>(handle);
  if (!rb || !source || !out) {
    return false;
  }
  if (rb->width != width || rb->height != height) {
    return false;   // caller recreates on a raster change
  }

  SDL_GPUTexture* tex = textureGpuHandle(source);
  if (!tex) {
    return false;
  }
  // DECKBOY_EGRESS_BENCH=1 prints the three costs every 100 frames. Read once:
  // this runs on the render thread at frame rate.
  static const bool bench = [] {
    const char* e = std::getenv("DECKBOY_EGRESS_BENCH");
    return e && *e;
  }();
  const Uint64 t0 = SDL_GetPerformanceCounter();
  // The renderer batches; without this the download can be submitted BEFORE the
  // draw that filled the texture, and the recording gets last frame's picture.
  SDL_FlushRenderer(rb->renderer);
  const Uint64 t1 = SDL_GetPerformanceCounter();

  // The slot about to be written may still have a download in flight -- three
  // frames is normally ample, but if the GPU falls behind, issuing a second
  // download into the same transfer buffer would race the first. Skip this
  // frame instead; the caller repeats the previous picture, which is what it
  // does for every other "not ready" case.
  if (rb->fence[rb->cursor] && !SDL_QueryGPUFence(rb->device, rb->fence[rb->cursor])) {
    return false;
  }

  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(rb->device);
  if (!cmd) {
    return false;
  }
  SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
  if (!pass) {
    SDL_SubmitGPUCommandBuffer(cmd);
    return false;
  }
  SDL_GPUTextureRegion region {};
  region.texture = tex;
  region.w = static_cast<Uint32>(width);
  region.h = static_cast<Uint32>(height);
  region.d = 1;
  SDL_GPUTextureTransferInfo dst {};
  dst.transfer_buffer = rb->ring[rb->cursor];
  dst.pixels_per_row = static_cast<Uint32>(width);
  dst.rows_per_layer = static_cast<Uint32>(height);
  SDL_DownloadFromGPUTexture(pass, &region, &dst);
  SDL_EndGPUCopyPass(pass);

  if (rb->fence[rb->cursor]) {
    // Signalled (checked above), so this only releases our reference.
    SDL_ReleaseGPUFence(rb->device, rb->fence[rb->cursor]);
    rb->fence[rb->cursor] = nullptr;
  }
  rb->fence[rb->cursor] = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  rb->cursor = (rb->cursor + 1) % kStagingRingSize;
  if (rb->primed < kStagingRingSize) {
    rb->primed += 1;
    return false;   // ring still filling
  }

  const Uint64 t2 = SDL_GetPerformanceCounter();
  const int oldest = rb->cursor;
  if (!rb->fence[oldest] || !SDL_QueryGPUFence(rb->device, rb->fence[oldest])) {
    if (bench) {
      static int miss = 0;
      if (++miss % 100 == 0) {
        std::cerr << "gpu-readback fence-miss x" << miss << std::endl;
      }
    }

    return false;   // still in flight; never wait on the render thread
  }
  const std::size_t frameBytes = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height) * 4u;
  bool ok = outBytes >= frameBytes;
  if (ok) {
    if (void* mapped = SDL_MapGPUTransferBuffer(rb->device, rb->ring[oldest], false)) {
      std::memcpy(out, mapped, frameBytes);
      SDL_UnmapGPUTransferBuffer(rb->device, rb->ring[oldest]);
    } else {
      ok = false;
    }
  }
  SDL_ReleaseGPUFence(rb->device, rb->fence[oldest]);
  rb->fence[oldest] = nullptr;
  if (bench) {
    static int n = 0; static double flush = 0, submit = 0, map = 0;
    const double f = static_cast<double>(SDL_GetPerformanceFrequency());
    flush += (t1 - t0) * 1000.0 / f;
    submit += (t2 - t1) * 1000.0 / f;
    map += (SDL_GetPerformanceCounter() - t2) * 1000.0 / f;
    if (++n == 100) {
      std::cerr << "gpu-readback flush=" << (flush / n) << "ms submit="
                << (submit / n) << "ms map=" << (map / n) << "ms" << std::endl;
      n = 0; flush = 0; submit = 0; map = 0;
    }
  }
  return ok;
}
#else   // SDL < 3.4: there is no SDL_GPU texture property to read from.
void* createGpuDownloadReadback(SDL_Renderer*, int, int) { return nullptr; }
void destroyGpuDownloadReadback(void*) {}
bool gpuDownloadReadbackFrame(void*, SDL_Texture*, std::uint8_t*, std::size_t,
                              int, int) { return false; }
#endif

// Which implementation a handle belongs to. Both rings hand back the same
// "false means reuse the previous picture" contract, so the only thing the
// caller ever sees is that one of them exists.
struct ReadbackHandle {
  void* d3d11 = nullptr;
  void* gpu = nullptr;
};

void* createD3D11Readback(SDL_Renderer* renderer, int width, int height) {
#ifdef _WIN32
  ID3D11Device* device = rendererDevice(renderer);
  if (!device || width <= 0 || height <= 0) {
    return nullptr;
  }
  auto* rb = new StagingReadback();
  rb->device = device;
  device->GetImmediateContext(&rb->context);
  rb->width = width;
  rb->height = height;
  if (!rb->context) {
    delete rb;
    return nullptr;
  }
  D3D11_TEXTURE2D_DESC desc {};
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  // BGRA, matching both the scale target and the byte order ffmpeg is fed, so
  // the mapped rows copy STRAIGHT into the egress buffer: no temporary, no
  // format conversion, no second pass over 8MB per frame.
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  for (int i = 0; i < kStagingRingSize; ++i) {
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &rb->ring[i]))) {
      for (int j = 0; j < i; ++j) rb->ring[j]->Release();
      rb->context->Release();
      delete rb;
      return nullptr;
    }
  }
  return rb;
#else
  (void) renderer; (void) width; (void) height;
  return nullptr;
#endif
}

void destroyD3D11Readback(void* handle) {
#ifdef _WIN32
  auto* rb = reinterpret_cast<StagingReadback*>(handle);
  if (!rb) return;
  for (auto*& t : rb->ring) {
    if (t) { t->Release(); t = nullptr; }
  }
  if (rb->context) rb->context->Release();
  delete rb;
#else
  (void) handle;
#endif
}

bool d3d11ReadbackFrame(void* handle, SDL_Texture* source,
                        std::uint8_t* out, std::size_t outBytes,
                        int width, int height) {
#ifdef _WIN32
  auto* rb = reinterpret_cast<StagingReadback*>(handle);
  if (!rb || !source || !out) {
    return false;
  }
  if (rb->width != width || rb->height != height) {
    return false;   // caller recreates on a raster change
  }
  auto* src = reinterpret_cast<ID3D11Texture2D*>(
    SDL_GetPointerProperty(SDL_GetTextureProperties(source),
                           SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
  if (!src) {
    return false;
  }
  // Issue this frame's copy, then FLUSH.
  //
  // Without the flush this never worked at all. D3D11 defers submission: the
  // CopyResource sits in the immediate context's command buffer until
  // something else forces it out, so the staging texture is still "in use" when
  // the map comes round and D3D11_MAP_FLAG_DO_NOT_WAIT dutifully refuses.
  // MEASURED before this line existed: 240 calls, 0 successes, 237 map
  // failures -- the readback returned "nothing ready" every single frame, the
  // caller served the held picture every single frame, and the recording was
  // one still image with a perfect frame count. Every frame-count check passed
  // while the picture never moved, which is exactly how it went unnoticed.
  static const bool bench = [] {
    const char* e = std::getenv("DECKBOY_EGRESS_BENCH");
    return e && *e;
  }();
  const Uint64 tCopy0 = SDL_GetPerformanceCounter();
  rb->context->CopyResource(rb->ring[rb->cursor], src);
  const Uint64 tCopy1 = SDL_GetPerformanceCounter();
  rb->context->Flush();
  const Uint64 tFlush = SDL_GetPerformanceCounter();
  rb->cursor = (rb->cursor + 1) % kStagingRingSize;
  if (rb->primed < kStagingRingSize) {
    rb->primed += 1;
    return false;   // ring still filling: nothing old enough to read yet
  }
  // Read the OLDEST, which the GPU finished with two frames ago. DO_NOT_WAIT so
  // a stall degrades into a skipped frame rather than blocking the render
  // thread -- the whole point of the exercise.
  const int oldest = rb->cursor;
  D3D11_MAPPED_SUBRESOURCE mapped {};
  const HRESULT hr = rb->context->Map(rb->ring[oldest], 0, D3D11_MAP_READ,
                                      D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
  if (FAILED(hr) || !mapped.pData) {
    return false;
  }
  const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
  bool ok = outBytes >= rowBytes * static_cast<std::size_t>(height);
  if (ok) {
    const auto* srcRow = static_cast<const std::uint8_t*>(mapped.pData);
    for (int y = 0; y < height; ++y) {
      std::memcpy(out + static_cast<std::size_t>(y) * rowBytes,
                  srcRow + static_cast<std::size_t>(y) * mapped.RowPitch,
                  rowBytes);
    }
  }
  rb->context->Unmap(rb->ring[oldest], 0);
  if (bench) {
    static int n = 0; static double sCopy = 0, sFlush = 0, sMap = 0;
    const double f = static_cast<double>(SDL_GetPerformanceFrequency());
    sCopy  += (tCopy1 - tCopy0) * 1000.0 / f;
    sFlush += (tFlush - tCopy1) * 1000.0 / f;
    sMap   += (SDL_GetPerformanceCounter() - tFlush) * 1000.0 / f;
    if (++n == 60) {
      std::cerr << "d3d11-readback copy=" << (sCopy / n) << " flush="
                << (sFlush / n) << " map+copy=" << (sMap / n) << "ms "
                << width << "x" << height << std::endl;
      n = 0; sCopy = sFlush = sMap = 0;
    }
  }
  return ok;
#else
  (void) handle; (void) source; (void) out; (void) outBytes;
  (void) width; (void) height;
  return false;
#endif
}

}  // namespace

// ----------------------------------------------------------------------------
// Public entry points. D3D11 first where it exists, because it is the path with
// years of frames behind it; the SDL_GPU download is what gives macOS, Linux --
// and a Windows machine running the "gpu" renderer -- the same 4K60 headroom.
void* createStagingReadback(SDL_Renderer* renderer, int width, int height) {
  void* d3d11 = nullptr;
#ifdef _WIN32
  d3d11 = createD3D11Readback(renderer, width, height);
#endif
  void* gpu = d3d11 ? nullptr : createGpuDownloadReadback(renderer, width, height);
  if (!d3d11 && !gpu) {
    return nullptr;   // caller falls back to SDL_RenderReadPixels
  }
  auto* handle = new ReadbackHandle();
  handle->d3d11 = d3d11;
  handle->gpu = gpu;
  return handle;
}

void destroyStagingReadback(void* handle) {
  auto* h = static_cast<ReadbackHandle*>(handle);
  if (!h) return;
#ifdef _WIN32
  destroyD3D11Readback(h->d3d11);
#endif
  destroyGpuDownloadReadback(h->gpu);
  delete h;
}

bool stagingReadbackFrame(void* handle, SDL_Texture* source,
                          std::uint8_t* out, std::size_t outBytes,
                          int width, int height) {
  auto* h = static_cast<ReadbackHandle*>(handle);
  if (!h) return false;
#ifdef _WIN32
  if (h->d3d11) {
    return d3d11ReadbackFrame(h->d3d11, source, out, outBytes, width, height);
  }
#endif
  return gpuDownloadReadbackFrame(h->gpu, source, out, outBytes, width, height);
}

}  // namespace deckboy::gpu
