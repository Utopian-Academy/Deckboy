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
// finished with two frames ago and never waits.
//
// THAT LATENCY IS NOT FREE, AND THIS COMMENT USED TO SAY IT WAS. It read
// "costs two frames of latency, which is meaningless for a file recording",
// which is true of the picture considered on its own and false the moment
// anything has to stay in step with it. The picture handed over here was
// composited about kAsyncReadbackDepth frames ago; the SOUND that gets muxed
// beside it is read at hand-over time and is therefore current. MEASURED
// 2026-09-14 on two machines: recorded sound sits 4.51 frames (sd 0.44, n=9)
// ahead of its own picture, constant from 25 to 60fps, and this ring is where
// it comes from.
//
// Not fixed, and the reason is worth knowing before anyone tries: a frame
// carries no measure of its own age. `capturedAtMs` is stamped when the
// readback COMPLETES, not when the frame was composited, so there is nothing
// to ask. The audio-tap fix in v0.99.365 worked precisely because the device
// COULD be asked what it was still holding; until a frame can be asked when it
// was drawn, any correction here is a chosen constant rather than a measured
// one. See private-notes/DEVNOTES.md for the steps.
//
// The recording pacer already allows for this latency. Nothing on the audio
// side does.
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
  int bytesPerPixel = 4;  // 8 for half-float RGBA
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
  int bytesPerPixel = 4;  // 8 for half-float RGBA
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

void* createGpuDownloadReadback(SDL_Renderer* renderer, int width, int height, int bytesPerPixel) {
  SDL_GPUDevice* device = rendererGpuDevice(renderer);
  if (!device || width <= 0 || height <= 0) {
    return nullptr;
  }
  auto* rb = new GpuDownloadReadback();
  rb->renderer = renderer;
  rb->device = device;
  rb->width = width;
  rb->height = height;
  rb->bytesPerPixel = bytesPerPixel;
  const Uint32 bytes = static_cast<Uint32>(width) * static_cast<Uint32>(height) *
                       static_cast<Uint32>(bytesPerPixel);
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
                                 static_cast<std::size_t>(height) * static_cast<std::size_t>(rb->bytesPerPixel);
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
void* createGpuDownloadReadback(SDL_Renderer*, int, int, int) { return nullptr; }
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

void* createD3D11Readback(SDL_Renderer* renderer, int width, int height, bool halfFloat) {
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
  rb->bytesPerPixel = halfFloat ? 8 : 4;
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
  // A half-float compositor stays half float: CopyResource needs the formats
  // to match, and the writer thread does the encoding.
  desc.Format = halfFloat ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
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
  (void) renderer; (void) width; (void) height; (void) halfFloat;
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
  const std::size_t rowBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(rb->bytesPerPixel);
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
void* createStagingReadback(SDL_Renderer* renderer, int width, int height, bool halfFloat) {
  void* d3d11 = nullptr;
#ifdef _WIN32
  d3d11 = createD3D11Readback(renderer, width, height, halfFloat);
#endif
  void* gpu = d3d11 ? nullptr : createGpuDownloadReadback(renderer, width, height, halfFloat ? 8 : 4);
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

// ----------------------------------------------------------------------------
// HDR P010 on the GPU (see gpu_readback.hpp).
#ifdef _WIN32
namespace {
// The same numbers as HdrEncoder::convertHalfToP010, per 2x2 block: the
// compositor's linear BT.709 light (1.0 = SDR white) to nits, to BT.2020
// primaries, through the PQ curve or HLG's inverse OOTF and OETF, to limited
// 10-bit Y'CbCr in the high bits of 16.
const char kHdrP010Shader[] = R"(
Texture2D<float4> src : register(t0);
RWTexture2D<uint> yOut : register(u0);
RWTexture2D<uint2> uvOut : register(u1);
cbuffer Params : register(b0) { float white; uint hlg; uint width; uint height; uint chroma422; uint3 pad; };
float pqOf(float nits) {
  const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
  const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
  float y = pow(saturate(nits / 10000.0), m1);
  return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}
float hlgOf(float e) {
  e = saturate(e);
  return e <= 1.0 / 12.0 ? sqrt(3.0 * e) : 0.17883277 * log(12.0 * e - 0.28466892) + 0.55991073;
}
float3 signalOf(float3 light) {
  float3 n = light * white;
  float3 c = max(float3(dot(n, float3(0.6274, 0.3293, 0.0433)),
                        dot(n, float3(0.0691, 0.9195, 0.0114)),
                        dot(n, float3(0.0164, 0.0880, 0.8956))), 0.0);
  if (hlg == 0) return float3(pqOf(c.r), pqOf(c.g), pqOf(c.b));
  float yd = clamp(dot(c, float3(0.2627, 0.6780, 0.0593)), 1e-7, 1000.0) / 1000.0;
  float gain = pow(yd, -1.0 / 6.0) / 1000.0;
  return float3(hlgOf(c.r * gain), hlgOf(c.g * gain), hlgOf(c.b * gain));
}
uint p010Of(float code) { return ((uint)clamp(floor(code + 0.5), 0.0, 1023.0)) << 6; }
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (chroma422 != 0) {
    // 4:2:2: one horizontal pair a thread, chroma the mean of the two.
    uint2 at0 = uint2(id.x * 2, id.y);
    if (at0.x + 1 >= width || at0.y >= height) return;
    float cb2 = 0.0, cr2 = 0.0;
    [unroll] for (uint dx2 = 0; dx2 < 2; ++dx2) {
      uint2 at = at0 + uint2(dx2, 0);
      float3 e = signalOf(src.Load(int3(at, 0)).rgb);
      float luma = dot(e, float3(0.2627, 0.6780, 0.0593));
      yOut[at] = p010Of(64.0 + 876.0 * luma);
      cb2 += e.b - luma;
      cr2 += e.r - luma;
    }
    uvOut[id.xy] = uint2(p010Of(512.0 + 448.0 / 1.8814 * cb2), p010Of(512.0 + 448.0 / 1.4746 * cr2));
    return;
  }
  uint2 base = id.xy * 2;
  if (base.x + 1 >= width || base.y + 1 >= height) return;
  float cb = 0.0, cr = 0.0;
  [unroll] for (uint dy = 0; dy < 2; ++dy) {
    [unroll] for (uint dx = 0; dx < 2; ++dx) {
      uint2 at = base + uint2(dx, dy);
      float3 e = signalOf(src.Load(int3(at, 0)).rgb);
      float luma = dot(e, float3(0.2627, 0.6780, 0.0593));
      yOut[at] = p010Of(64.0 + 876.0 * luma);
      cb += e.b - luma;
      cr += e.r - luma;
    }
  }
  uvOut[id.xy] = uint2(p010Of(512.0 + 224.0 / 1.8814 * cb), p010Of(512.0 + 224.0 / 1.4746 * cr));
}
)";

// D3DCompile's signature, so no d3dcompiler header or import library is
// needed: the DLL ships with Windows and is loaded when first wanted.
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR,
                                     UINT, UINT, ID3DBlob**, ID3DBlob**);

struct HdrP010Readback {
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  ID3D11ComputeShader* shader = nullptr;
  ID3D11Buffer* params = nullptr;
  ID3D11Texture2D* yTex = nullptr;
  ID3D11Texture2D* uvTex = nullptr;
  ID3D11UnorderedAccessView* yUav = nullptr;
  ID3D11UnorderedAccessView* uvUav = nullptr;
  ID3D11Texture2D* yRing[kStagingRingSize] = {nullptr, nullptr, nullptr};
  ID3D11Texture2D* uvRing[kStagingRingSize] = {nullptr, nullptr, nullptr};
  ID3D11ShaderResourceView* srv = nullptr;   // of the last source seen
  ID3D11Texture2D* srvSource = nullptr;
  int width = 0;
  int height = 0;
  bool chroma422 = false;
  int cursor = 0;
  int primed = 0;
};

template <typename T> void releaseCom(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

void destroyP010(HdrP010Readback* rb) {
  if (!rb) return;
  for (int i = 0; i < kStagingRingSize; ++i) {
    releaseCom(rb->yRing[i]);
    releaseCom(rb->uvRing[i]);
  }
  releaseCom(rb->srv);
  releaseCom(rb->yUav);
  releaseCom(rb->uvUav);
  releaseCom(rb->yTex);
  releaseCom(rb->uvTex);
  releaseCom(rb->params);
  releaseCom(rb->shader);
  releaseCom(rb->context);
  delete rb;
}

ID3D11ComputeShader* compileHdrP010Shader(ID3D11Device* device) {
  static HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
  if (!compiler) return nullptr;
  auto compile = reinterpret_cast<D3DCompileFn>(GetProcAddress(compiler, "D3DCompile"));
  if (!compile) return nullptr;
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  constexpr UINT kOptimise = 1u << 15;   // D3DCOMPILE_OPTIMIZATION_LEVEL3
  const HRESULT hr = compile(kHdrP010Shader, sizeof(kHdrP010Shader) - 1, "hdr_p010", nullptr, nullptr, "main",
                             "cs_5_0", kOptimise, 0, &code, &errors);
  if (FAILED(hr) || !code) {
    if (errors) {
      std::cerr << "hdr-p010: shader did not compile: "
                << static_cast<const char*>(errors->GetBufferPointer()) << std::endl;
    }
    releaseCom(errors);
    releaseCom(code);
    return nullptr;
  }
  releaseCom(errors);
  ID3D11ComputeShader* shader = nullptr;
  device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
  code->Release();
  return shader;
}

ID3D11Texture2D* makeTexture(ID3D11Device* device, int w, int h, DXGI_FORMAT format, bool staging) {
  D3D11_TEXTURE2D_DESC desc {};
  desc.Width = static_cast<UINT>(w);
  desc.Height = static_cast<UINT>(h);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = staging ? D3D11_USAGE_STAGING : D3D11_USAGE_DEFAULT;
  desc.BindFlags = staging ? 0 : D3D11_BIND_UNORDERED_ACCESS;
  desc.CPUAccessFlags = staging ? D3D11_CPU_ACCESS_READ : 0;
  ID3D11Texture2D* tex = nullptr;
  return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &tex)) ? tex : nullptr;
}
}  // namespace
#endif

void* createHdrP010Readback(SDL_Renderer* renderer, int width, int height, bool chroma422) {
#ifdef _WIN32
  ID3D11Device* device = rendererDevice(renderer);
  if (!device || width <= 0 || height <= 0 || (width % 2) || (height % 2)) return nullptr;
  auto* rb = new HdrP010Readback();
  rb->device = device;
  device->GetImmediateContext(&rb->context);
  rb->width = width;
  rb->height = height;
  rb->chroma422 = chroma422;
  const int chromaRows = chroma422 ? height : height / 2;
  rb->shader = compileHdrP010Shader(device);
  D3D11_BUFFER_DESC cb {};
  cb.ByteWidth = 32;
  cb.Usage = D3D11_USAGE_DEFAULT;
  cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  device->CreateBuffer(&cb, nullptr, &rb->params);
  rb->yTex = makeTexture(device, width, height, DXGI_FORMAT_R16_UINT, false);
  rb->uvTex = makeTexture(device, width / 2, chromaRows, DXGI_FORMAT_R16G16_UINT, false);
  bool ok = rb->context && rb->shader && rb->params && rb->yTex && rb->uvTex &&
            SUCCEEDED(device->CreateUnorderedAccessView(rb->yTex, nullptr, &rb->yUav)) &&
            SUCCEEDED(device->CreateUnorderedAccessView(rb->uvTex, nullptr, &rb->uvUav));
  for (int i = 0; ok && i < kStagingRingSize; ++i) {
    rb->yRing[i] = makeTexture(device, width, height, DXGI_FORMAT_R16_UINT, true);
    rb->uvRing[i] = makeTexture(device, width / 2, chromaRows, DXGI_FORMAT_R16G16_UINT, true);
    ok = rb->yRing[i] && rb->uvRing[i];
  }
  if (!ok) {
    destroyP010(rb);
    return nullptr;
  }
  return rb;
#else
  (void) renderer;
  (void) width;
  (void) height;
  (void) chroma422;
  return nullptr;
#endif
}

void destroyHdrP010Readback(void* handle) {
#ifdef _WIN32
  destroyP010(static_cast<HdrP010Readback*>(handle));
#else
  (void) handle;
#endif
}

bool hdrP010ReadbackFrame(void* handle, SDL_Texture* source, float whiteNits, bool hlg,
                          std::uint8_t* out, std::size_t outBytes) {
#ifdef _WIN32
  auto* rb = static_cast<HdrP010Readback*>(handle);
  if (!rb || !source || !out) return false;
  const std::size_t lumaBytes = static_cast<std::size_t>(rb->width) * rb->height * 2;
  const int chromaRows = rb->chroma422 ? rb->height : rb->height / 2;
  if (outBytes < lumaBytes + static_cast<std::size_t>(rb->width) * 2 * chromaRows) return false;
  auto* src = reinterpret_cast<ID3D11Texture2D*>(
    SDL_GetPointerProperty(SDL_GetTextureProperties(source), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
  if (!src) return false;
  if (rb->srvSource != src) {
    releaseCom(rb->srv);
    rb->srvSource = nullptr;
    if (FAILED(rb->device->CreateShaderResourceView(src, nullptr, &rb->srv))) return false;
    rb->srvSource = src;
  }
  // The renderer batches: everything drawn into the compositor must reach the
  // GPU before the shader reads it. And the compositor must not still be the
  // bound render target, or D3D11 nulls the shader's view of it.
  SDL_FlushRenderer(SDL_GetRendererFromTexture(source));
  ID3D11RenderTargetView* savedRtv = nullptr;
  ID3D11DepthStencilView* savedDsv = nullptr;
  rb->context->OMGetRenderTargets(1, &savedRtv, &savedDsv);
  rb->context->OMSetRenderTargets(0, nullptr, nullptr);

  struct {
    float white;
    UINT hlg;
    UINT width;
    UINT height;
    UINT chroma422;
    UINT pad[3];
  } params {whiteNits, hlg ? 1u : 0u, static_cast<UINT>(rb->width), static_cast<UINT>(rb->height),
            rb->chroma422 ? 1u : 0u, {0, 0, 0}};
  rb->context->UpdateSubresource(rb->params, 0, nullptr, &params, 0, 0);
  rb->context->CSSetShader(rb->shader, nullptr, 0);
  rb->context->CSSetConstantBuffers(0, 1, &rb->params);
  rb->context->CSSetShaderResources(0, 1, &rb->srv);
  ID3D11UnorderedAccessView* uavs[2] = {rb->yUav, rb->uvUav};
  rb->context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
  rb->context->Dispatch(static_cast<UINT>((rb->width / 2 + 7) / 8), static_cast<UINT>((chromaRows + 7) / 8), 1);
  // Leave the pipeline as the renderer expects it.
  ID3D11ShaderResourceView* noSrv = nullptr;
  ID3D11UnorderedAccessView* noUav[2] = {nullptr, nullptr};
  ID3D11Buffer* noCb = nullptr;
  rb->context->CSSetShaderResources(0, 1, &noSrv);
  rb->context->CSSetUnorderedAccessViews(0, 2, noUav, nullptr);
  rb->context->CSSetConstantBuffers(0, 1, &noCb);
  rb->context->CSSetShader(nullptr, nullptr, 0);
  rb->context->OMSetRenderTargets(savedRtv ? 1 : 0, savedRtv ? &savedRtv : nullptr, savedDsv);
  releaseCom(savedRtv);
  releaseCom(savedDsv);

  rb->context->CopyResource(rb->yRing[rb->cursor], rb->yTex);
  rb->context->CopyResource(rb->uvRing[rb->cursor], rb->uvTex);
  rb->context->Flush();
  rb->cursor = (rb->cursor + 1) % kStagingRingSize;
  if (rb->primed < kStagingRingSize) {
    rb->primed += 1;
    return false;   // ring still filling
  }
  // The OLDEST, finished frames ago; DO_NOT_WAIT, as in d3d11ReadbackFrame.
  const int oldest = rb->cursor;
  D3D11_MAPPED_SUBRESOURCE yMap {};
  D3D11_MAPPED_SUBRESOURCE uvMap {};
  if (FAILED(rb->context->Map(rb->yRing[oldest], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &yMap)) ||
      !yMap.pData) {
    return false;
  }
  if (FAILED(rb->context->Map(rb->uvRing[oldest], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &uvMap)) ||
      !uvMap.pData) {
    rb->context->Unmap(rb->yRing[oldest], 0);
    return false;
  }
  // Both planes are `width` 2-byte samples a row (chroma: width/2 pairs).
  const std::size_t rowBytes = static_cast<std::size_t>(rb->width) * 2;
  for (int y = 0; y < rb->height; ++y) {
    std::memcpy(out + rowBytes * y,
                static_cast<const std::uint8_t*>(yMap.pData) + static_cast<std::size_t>(yMap.RowPitch) * y, rowBytes);
  }
  std::uint8_t* uvOut = out + lumaBytes;
  for (int y = 0; y < chromaRows; ++y) {
    std::memcpy(uvOut + rowBytes * y,
                static_cast<const std::uint8_t*>(uvMap.pData) + static_cast<std::size_t>(uvMap.RowPitch) * y, rowBytes);
  }
  rb->context->Unmap(rb->yRing[oldest], 0);
  rb->context->Unmap(rb->uvRing[oldest], 0);
  return true;
#else
  (void) handle;
  (void) source;
  (void) whiteNits;
  (void) hlg;
  (void) out;
  (void) outBytes;
  return false;
#endif
}

}  // namespace deckboy::gpu
