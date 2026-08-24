#include "gpu_readback.hpp"

#include <cstring>

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
#ifdef _WIN32
constexpr int kStagingRingSize = 3;
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
}  // namespace

void* createStagingReadback(SDL_Renderer* renderer, int width, int height) {
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

void destroyStagingReadback(void* handle) {
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

bool stagingReadbackFrame(void* handle, SDL_Texture* source,
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
  // Issue this frame's copy; it completes on the GPU's own schedule.
  rb->context->CopyResource(rb->ring[rb->cursor], src);
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
  return ok;
#else
  (void) handle; (void) source; (void) out; (void) outBytes;
  (void) width; (void) height;
  return false;
#endif
}

}  // namespace deckboy::gpu
