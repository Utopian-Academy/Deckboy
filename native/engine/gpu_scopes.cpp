// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#include "gpu_scopes.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>

#ifdef _WIN32
#include <d3d11.h>
#endif

namespace deckboy::gpu {
namespace s = core::scopes;
namespace {
constexpr int kLevels = 256;
constexpr std::size_t kCellPlane = 256 * 256;
constexpr std::size_t kFingerprint = 3 * kCellPlane;   // two words after the three planes
constexpr std::size_t kCellWords = kFingerprint + 2;
constexpr int kSlots = 3;   // in flight at once: the render thread never waits for one
// The shader in every form: HLSL text (Direct3D 11), MSL text (Metal),
// SPIR-V (Vulkan) and DXIL (Direct3D 12). See gpu_scopes.hlsl.
#include "gpu_scopes_shaders.inc"
}  // namespace

bool gpuScopesSupport(const s::Options& options, int width) {
  return options.levels.low == 0 && options.levels.high == 1 && options.chroma.low == -0.5 &&
         options.chroma.high == 0.5 && options.levelBins == kLevels && options.vectorBins == kLevels &&
         options.histogramBins == kLevels && options.columns >= 1 &&
         options.columns <= static_cast<std::size_t>(std::max(width, 1));
}

s::Result unpackGpuScopes(const GpuScopeCounts& counts, const s::Options& options) {
  s::Result result = s::analyze(s::Rgba8View{{}, 0, 0}, options);   // the empty shape
  const auto columns = static_cast<std::size_t>(counts.columns);
  const std::size_t plane = kLevels * columns;
  if (counts.wave.size() < 6 * plane || counts.cells.size() < kCellWords ||
      columns != options.columns) {
    return result;
  }
  for (std::size_t c = 0; c < 3; ++c) {
    std::copy_n(counts.wave.begin() + c * plane, plane, result.ycbcrParade[c].bins.begin());
    std::copy_n(counts.wave.begin() + (3 + c) * plane, plane, result.rgbParade[c].bins.begin());
  }
  // The histograms are the waveforms summed across: on these axes a value's
  // histogram bin IS its waveform row.
  const auto sumRows = [&](const s::Density& wave, std::vector<std::uint64_t>& bins) {
    for (std::size_t row = 0; row < kLevels; ++row) {
      std::uint64_t total = 0;
      for (std::size_t col = 0; col < columns; ++col) total += wave.bins[row * columns + col];
      bins[row] = total;
    }
  };
  sumRows(result.ycbcrParade[0], result.lumaHistogram.bins);
  for (std::size_t c = 0; c < 3; ++c) sumRows(result.rgbParade[c], result.rgbHistogram[c].bins);
  std::copy_n(counts.cells.begin(), kCellPlane, result.vectorscope.bins.begin());
  std::copy_n(counts.cells.begin() + kCellPlane, kCellPlane, result.diamond[0].bins.begin());
  std::copy_n(counts.cells.begin() + 2 * kCellPlane, kCellPlane, result.diamond[1].bins.begin());
  const auto pixels = static_cast<std::uint64_t>(counts.width) * static_cast<std::uint64_t>(counts.height);
  result.validPixels = result.rawGamut.validPixels = pixels;
  result.filteredGamut = result.rawGamut;
  return result;
}

// ----------------------------------------------------------------------------
// Direct3D 11, Windows' output renderer: the HLSL compiled at run time, as
// the HDR P010 encoder's is (gpu_readback.cpp).
#ifdef _WIN32
namespace {
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR,
                                     UINT, UINT, ID3DBlob**, ID3DBlob**);

template <typename T> void releaseCom(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

ID3D11ComputeShader* compileScopesShader(ID3D11Device* device, const char* entry) {
  static HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
  if (!compiler) return nullptr;
  auto compile = reinterpret_cast<D3DCompileFn>(GetProcAddress(compiler, "D3DCompile"));
  if (!compile) return nullptr;
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  constexpr UINT kOptimise = 1u << 15;   // D3DCOMPILE_OPTIMIZATION_LEVEL3
  const HRESULT hr = compile(kScopesHlsl, sizeof(kScopesHlsl) - 1, "gpu_scopes", nullptr, nullptr, entry,
                             "cs_5_0", kOptimise, 0, &code, &errors);
  if (FAILED(hr) || !code) {
    if (errors) {
      std::cerr << "gpu-scopes: shader did not compile: "
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

ID3D11Buffer* makeBuffer(ID3D11Device* device, std::size_t words, bool staging) {
  D3D11_BUFFER_DESC desc {};
  desc.ByteWidth = static_cast<UINT>(words * 4);
  desc.Usage = staging ? D3D11_USAGE_STAGING : D3D11_USAGE_DEFAULT;
  desc.BindFlags = staging ? 0 : D3D11_BIND_UNORDERED_ACCESS;
  desc.CPUAccessFlags = staging ? D3D11_CPU_ACCESS_READ : 0;
  desc.MiscFlags = staging ? 0 : D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  ID3D11Buffer* buffer = nullptr;
  return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &buffer)) ? buffer : nullptr;
}

ID3D11UnorderedAccessView* rawView(ID3D11Device* device, ID3D11Buffer* buffer, std::size_t words) {
  D3D11_UNORDERED_ACCESS_VIEW_DESC desc {};
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  desc.Buffer.NumElements = static_cast<UINT>(words);
  desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
  ID3D11UnorderedAccessView* view = nullptr;
  return SUCCEEDED(device->CreateUnorderedAccessView(buffer, &desc, &view)) ? view : nullptr;
}

struct Slot {
  ID3D11Buffer* wave = nullptr;
  ID3D11Buffer* cells = nullptr;
  ID3D11Buffer* thumb = nullptr;
  std::size_t thumbWords = 0;
  GpuScopeCounts meta;   // everything but the counts
};

struct D3D11Scopes {
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  ID3D11ComputeShader* columnsShader = nullptr;
  ID3D11ComputeShader* thumbShader = nullptr;
  ID3D11Buffer* params = nullptr;
  ID3D11Buffer* wave = nullptr;
  ID3D11UnorderedAccessView* waveUav = nullptr;
  ID3D11Buffer* cells = nullptr;
  ID3D11UnorderedAccessView* cellsUav = nullptr;
  ID3D11Buffer* thumb = nullptr;
  ID3D11UnorderedAccessView* thumbUav = nullptr;
  std::size_t thumbWords = 0;
  ID3D11ShaderResourceView* srv = nullptr;   // of the last source seen
  ID3D11Texture2D* srvSource = nullptr;
  int width = 0, height = 0, columns = 0;
  Slot slots[kSlots];
  std::deque<int> pending;   // submitted, oldest first
  int next = 0;
};

void releaseSlotThumb(Slot& slot) {
  releaseCom(slot.thumb);
  slot.thumbWords = 0;
}

void destroyD3D11Scopes(D3D11Scopes* g) {
  if (!g) return;
  for (auto& slot : g->slots) {
    releaseCom(slot.wave);
    releaseCom(slot.cells);
    releaseSlotThumb(slot);
  }
  releaseCom(g->srv);
  releaseCom(g->thumbUav);
  releaseCom(g->thumb);
  releaseCom(g->cellsUav);
  releaseCom(g->cells);
  releaseCom(g->waveUav);
  releaseCom(g->wave);
  releaseCom(g->params);
  releaseCom(g->thumbShader);
  releaseCom(g->columnsShader);
  releaseCom(g->context);
  delete g;
}

ID3D11Device* rendererDevice(SDL_Renderer* renderer) {
  return renderer ? reinterpret_cast<ID3D11Device*>(SDL_GetPointerProperty(
                      SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr))
                  : nullptr;
}

// The waveform buffers depend on the column count; made when it changes.
// Only with nothing in flight, so no slot is read against the wrong shape.
bool ensureColumns(D3D11Scopes* g, int columns) {
  if (g->columns == columns) return true;
  if (!g->pending.empty()) return false;
  releaseCom(g->waveUav);
  releaseCom(g->wave);
  for (auto& slot : g->slots) releaseCom(slot.wave);
  g->columns = 0;
  const std::size_t words = static_cast<std::size_t>(6) * kLevels * columns;
  g->wave = makeBuffer(g->device, words, false);
  g->waveUav = g->wave ? rawView(g->device, g->wave, words) : nullptr;
  bool ok = g->waveUav != nullptr;
  for (auto& slot : g->slots) ok = ok && (slot.wave = makeBuffer(g->device, words, true)) != nullptr;
  if (ok) g->columns = columns;
  return ok;
}

// The false colour samples are a buffer, one BGRA word a sample, sized to
// the picture asked for (it changes only with the raster or orientation).
bool ensureThumb(D3D11Scopes* g, Slot& slot, std::size_t words) {
  if (g->thumbWords != words) {
    releaseCom(g->thumbUav);
    releaseCom(g->thumb);
    g->thumbWords = 0;
    g->thumb = makeBuffer(g->device, words, false);
    g->thumbUav = g->thumb ? rawView(g->device, g->thumb, words) : nullptr;
    if (!g->thumbUav) return false;
    g->thumbWords = words;
  }
  if (slot.thumbWords != words) {
    releaseSlotThumb(slot);
    slot.thumb = makeBuffer(g->device, words, true);
    if (!slot.thumb) return false;
    slot.thumbWords = words;
  }
  return true;
}
}  // namespace
#endif

namespace {
void* d3d11Create(SDL_Renderer* renderer, int width, int height) {
#ifdef _WIN32
  ID3D11Device* device = rendererDevice(renderer);
  if (!device || width <= 0 || height <= 0) return nullptr;
  auto* g = new D3D11Scopes();
  g->device = device;
  device->GetImmediateContext(&g->context);
  g->width = width;
  g->height = height;
  g->columnsShader = compileScopesShader(device, "columnsMain");
  g->thumbShader = compileScopesShader(device, "thumbMain");
  D3D11_BUFFER_DESC cb {};
  cb.ByteWidth = 48;
  cb.Usage = D3D11_USAGE_DEFAULT;
  cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  device->CreateBuffer(&cb, nullptr, &g->params);
  g->cells = makeBuffer(device, kCellWords, false);
  g->cellsUav = g->cells ? rawView(device, g->cells, kCellWords) : nullptr;
  bool ok = g->context && g->columnsShader && g->thumbShader && g->params && g->cellsUav;
  for (auto& slot : g->slots) ok = ok && (slot.cells = makeBuffer(device, kCellWords, true)) != nullptr;
  if (!ok) {
    destroyD3D11Scopes(g);
    return nullptr;
  }
  return g;
#else
  (void) renderer;
  (void) width;
  (void) height;
  return nullptr;
#endif
}

void d3d11Destroy(void* handle) {
#ifdef _WIN32
  destroyD3D11Scopes(static_cast<D3D11Scopes*>(handle));
#else
  (void) handle;
#endif
}

std::string d3d11Adapter(void* handle) {
#ifdef _WIN32
  auto* g = static_cast<D3D11Scopes*>(handle);
  if (!g) return {};
  std::string name;
  IDXGIDevice* dxgi = nullptr;
  IDXGIAdapter* adapter = nullptr;
  DXGI_ADAPTER_DESC desc {};
  if (SUCCEEDED(g->device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi))) &&
      SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&desc))) {
    for (const wchar_t* c = desc.Description; *c; ++c) name += *c < 128 ? static_cast<char>(*c) : '?';
  }
  releaseCom(adapter);
  releaseCom(dxgi);
  return name;
#else
  (void) handle;
  return {};
#endif
}

int d3d11InFlight(void* handle) {
#ifdef _WIN32
  auto* g = static_cast<D3D11Scopes*>(handle);
  return g ? static_cast<int>(g->pending.size()) : 0;
#else
  (void) handle;
  return 0;
#endif
}

bool d3d11Submit(void* handle, SDL_Texture* source, int orientation, int columns, s::Matrix matrix,
                 int thumbWidth, int thumbHeight, std::uint64_t submittedAtNs) {
#ifdef _WIN32
  auto* g = static_cast<D3D11Scopes*>(handle);
  if (!g || !source || g->pending.size() >= kSlots) return false;
  const bool turned = orientation == 90 || orientation == 270;
  const int width = turned ? g->height : g->width;
  const int height = turned ? g->width : g->height;
  if (columns < 1 || columns > width || !ensureColumns(g, columns)) return false;
  auto* src = reinterpret_cast<ID3D11Texture2D*>(
    SDL_GetPointerProperty(SDL_GetTextureProperties(source), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
  if (!src) return false;
  D3D11_TEXTURE2D_DESC srcDesc {};
  src->GetDesc(&srcDesc);
  if (static_cast<int>(srcDesc.Width) != g->width || static_cast<int>(srcDesc.Height) != g->height) return false;
  if (g->srvSource != src) {
    releaseCom(g->srv);
    g->srvSource = nullptr;
    if (FAILED(g->device->CreateShaderResourceView(src, nullptr, &g->srv))) return false;
    g->srvSource = src;
  }
  Slot& slot = g->slots[g->next];
  const bool wantThumb = thumbWidth > 0 && thumbHeight > 0;
  if (wantThumb && !ensureThumb(g, slot, static_cast<std::size_t>(thumbWidth) * thumbHeight)) return false;

  // Everything drawn into the source must reach the GPU first, and the source
  // must not still be the bound render target, or D3D11 nulls the shader's
  // view of it (as in hdrP010ReadbackFrame).
  SDL_FlushRenderer(SDL_GetRendererFromTexture(source));
  ID3D11RenderTargetView* savedRtv = nullptr;
  ID3D11DepthStencilView* savedDsv = nullptr;
  g->context->OMGetRenderTargets(1, &savedRtv, &savedDsv);
  g->context->OMSetRenderTargets(0, nullptr, nullptr);

  const auto k = s::exactCoefficients(matrix);
  const UINT params[12] = {static_cast<UINT>(width), static_cast<UINT>(height), static_cast<UINT>(g->width),
                           static_cast<UINT>(g->height), static_cast<UINT>(columns),
                           static_cast<UINT>(orientation), k.kr, k.kg, k.kb,
                           wantThumb ? static_cast<UINT>(thumbWidth) : 0u,
                           wantThumb ? static_cast<UINT>(thumbHeight) : 0u, 0u};
  g->context->UpdateSubresource(g->params, 0, nullptr, params, 0, 0);
  const UINT zero[4] = {0, 0, 0, 0};
  g->context->ClearUnorderedAccessViewUint(g->cellsUav, zero);
  g->context->CSSetConstantBuffers(0, 1, &g->params);
  g->context->CSSetShaderResources(0, 1, &g->srv);
  ID3D11UnorderedAccessView* uavs[3] = {g->waveUav, g->cellsUav, wantThumb ? g->thumbUav : nullptr};
  g->context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
  g->context->CSSetShader(g->columnsShader, nullptr, 0);
  g->context->Dispatch(static_cast<UINT>(columns), 1, 1);
  if (wantThumb) {
    g->context->CSSetShader(g->thumbShader, nullptr, 0);
    g->context->Dispatch(static_cast<UINT>((thumbWidth + 7) / 8), static_cast<UINT>((thumbHeight + 7) / 8), 1);
  }
  // Leave the pipeline as the renderer expects it.
  ID3D11ShaderResourceView* noSrv = nullptr;
  ID3D11UnorderedAccessView* noUav[3] = {nullptr, nullptr, nullptr};
  ID3D11Buffer* noCb = nullptr;
  g->context->CSSetShaderResources(0, 1, &noSrv);
  g->context->CSSetUnorderedAccessViews(0, 3, noUav, nullptr);
  g->context->CSSetConstantBuffers(0, 1, &noCb);
  g->context->CSSetShader(nullptr, nullptr, 0);
  g->context->OMSetRenderTargets(savedRtv ? 1 : 0, savedRtv ? &savedRtv : nullptr, savedDsv);
  releaseCom(savedRtv);
  releaseCom(savedDsv);

  g->context->CopyResource(slot.wave, g->wave);
  g->context->CopyResource(slot.cells, g->cells);
  if (wantThumb) g->context->CopyResource(slot.thumb, g->thumb);
  // Submit now: a copy left in the context's command buffer is never
  // finished when the map comes round (see d3d11ReadbackFrame).
  g->context->Flush();
  slot.meta = {};
  slot.meta.width = width;
  slot.meta.height = height;
  slot.meta.columns = columns;
  slot.meta.matrix = matrix;
  slot.meta.submittedAtNs = submittedAtNs;
  slot.meta.thumbWidth = wantThumb ? thumbWidth : 0;
  slot.meta.thumbHeight = wantThumb ? thumbHeight : 0;
  g->pending.push_back(g->next);
  g->next = (g->next + 1) % kSlots;
  return true;
#else
  (void) handle; (void) source; (void) orientation; (void) columns; (void) matrix;
  (void) thumbWidth; (void) thumbHeight; (void) submittedAtNs;
  return false;
#endif
}

bool d3d11Collect(void* handle, GpuScopeCounts& out) {
#ifdef _WIN32
  auto* g = static_cast<D3D11Scopes*>(handle);
  if (!g || g->pending.empty()) return false;
  Slot& slot = g->slots[g->pending.front()];
  const std::size_t waveWords = static_cast<std::size_t>(6) * kLevels * slot.meta.columns;
  D3D11_MAPPED_SUBRESOURCE wave {}, cells {}, thumb {};
  if (FAILED(g->context->Map(slot.wave, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &wave)) || !wave.pData)
    return false;   // still on the GPU
  if (FAILED(g->context->Map(slot.cells, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &cells)) || !cells.pData) {
    g->context->Unmap(slot.wave, 0);
    return false;
  }
  const bool hasThumb = slot.meta.thumbWidth > 0;
  if (hasThumb && (FAILED(g->context->Map(slot.thumb, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &thumb)) ||
                   !thumb.pData)) {
    g->context->Unmap(slot.cells, 0);
    g->context->Unmap(slot.wave, 0);
    return false;
  }
  out = slot.meta;
  out.wave.resize(waveWords);
  std::memcpy(out.wave.data(), wave.pData, waveWords * 4);
  const auto* words = static_cast<const std::uint32_t*>(cells.pData);
  out.cells.assign(words, words + kCellWords);
  out.fingerprint = (static_cast<std::uint64_t>(words[kFingerprint + 1]) << 32) | words[kFingerprint];
  if (hasThumb) {
    out.thumb.resize(static_cast<std::size_t>(slot.meta.thumbWidth) * slot.meta.thumbHeight * 4);
    std::memcpy(out.thumb.data(), thumb.pData, out.thumb.size());
    g->context->Unmap(slot.thumb, 0);
  }
  g->context->Unmap(slot.cells, 0);
  g->context->Unmap(slot.wave, 0);
  g->pending.pop_front();
  return true;
#else
  (void) handle;
  (void) out;
  return false;
#endif
}

}  // namespace

// ----------------------------------------------------------------------------
// SDL_GPU: the same shader for Metal (macOS, MSL source), Vulkan (Linux,
// SPIR-V) and Direct3D 12 (DXIL), through the "gpu" renderer the outputs use
// off Windows. The shape is d3d11's: count into device buffers, copy them to
// transfer buffers, and read the oldest when its fence has signalled -- never
// waited on. Clearing the cells is an upload from a zeroed transfer buffer;
// SDL_GPU has no clear for a buffer.
//
// ONE PRESENT LATE, ON PURPOSE. The GPU renderer records into its own command
// buffer and submits it only at SDL_RenderPresent (SDL_FlushRenderer does not
// submit), so work submitted straight after drawing the capture ran BEFORE
// that draw and read the previous contents -- a black frame the first time.
// A submission is therefore staged, and issued at the next call (collect runs
// first every present): after the present that carried the capture draw, and
// before the next capture draw is recorded.
#if SDL_VERSION_ATLEAST(3, 4, 0)
namespace {
struct SdlSlot {
  SDL_GPUTransferBuffer* wave = nullptr;
  SDL_GPUTransferBuffer* cells = nullptr;
  SDL_GPUTransferBuffer* thumb = nullptr;
  std::size_t thumbBytes = 0;
  SDL_GPUFence* fence = nullptr;
  GpuScopeCounts meta;   // everything but the counts
};

struct SdlGpuScopes {
  SDL_Renderer* renderer = nullptr;
  SDL_GPUDevice* device = nullptr;
  SDL_GPUComputePipeline* columnsPipeline = nullptr;
  SDL_GPUComputePipeline* thumbPipeline = nullptr;
  SDL_GPUSampler* sampler = nullptr;
  SDL_GPUBuffer* wave = nullptr;
  SDL_GPUBuffer* cells = nullptr;
  SDL_GPUTransferBuffer* zeros = nullptr;
  SDL_GPUBuffer* thumb = nullptr;   // always one (bound in every pass), a word until wanted
  std::size_t thumbBytes = 0;
  int width = 0, height = 0, columns = 0;
  std::string driver;
  SdlSlot slots[kSlots];
  std::deque<int> pending;
  int next = 0;
  struct Staged {
    bool valid = false;
    SDL_Texture* source = nullptr;
    int orientation = 0, columns = 0, thumbWidth = 0, thumbHeight = 0;
    s::Matrix matrix = s::Matrix::Bt709;
    std::uint64_t submittedAtNs = 0;
  } staged;
};

SDL_GPUComputePipeline* sdlPipeline(SDL_GPUDevice* device, bool columns) {
  const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
  SDL_GPUComputePipelineCreateInfo info {};
  info.entrypoint = columns ? "columnsMain" : "thumbMain";
  if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0) {
    info.format = SDL_GPU_SHADERFORMAT_MSL;
    info.code = reinterpret_cast<const Uint8*>(kScopesMsl);
    info.code_size = sizeof(kScopesMsl) - 1;
  } else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0 && !kScopesColumnsSpirvMissing) {
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.code = columns ? kScopesColumnsSpirv : kScopesThumbSpirv;
    info.code_size = columns ? sizeof(kScopesColumnsSpirv) : sizeof(kScopesThumbSpirv);
  } else if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0) {
    info.format = SDL_GPU_SHADERFORMAT_DXIL;
    info.code = columns ? kScopesColumnsDxil : kScopesThumbDxil;
    info.code_size = columns ? sizeof(kScopesColumnsDxil) : sizeof(kScopesThumbDxil);
  } else {
    return nullptr;   // e.g. Vulkan from a build whose dxc made no SPIR-V
  }
  // Both entry points declare the whole layout, so one compute pass binds
  // for either.
  info.num_samplers = 1;
  info.num_readwrite_storage_buffers = 3;
  info.num_uniform_buffers = 1;
  info.threadcount_x = columns ? 256 : 8;
  info.threadcount_y = columns ? 1 : 8;
  info.threadcount_z = 1;
  SDL_GPUComputePipeline* pipeline = SDL_CreateGPUComputePipeline(device, &info);
  if (!pipeline) {
    std::cerr << "gpu-scopes: the " << info.entrypoint << " pipeline did not build: " << SDL_GetError() << std::endl;
  }
  return pipeline;
}

SDL_GPUTransferBuffer* sdlTransfer(SDL_GPUDevice* device, std::size_t bytes, bool download) {
  SDL_GPUTransferBufferCreateInfo info {};
  info.usage = download ? SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD : SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  info.size = static_cast<Uint32>(bytes);
  return SDL_CreateGPUTransferBuffer(device, &info);
}

SDL_GPUBuffer* sdlStorage(SDL_GPUDevice* device, std::size_t bytes) {
  SDL_GPUBufferCreateInfo info {};
  info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
  info.size = static_cast<Uint32>(bytes);
  return SDL_CreateGPUBuffer(device, &info);
}

template <typename T, typename Release> void releaseGpu(SDL_GPUDevice* device, T*& p, Release release) {
  if (p) {
    release(device, p);
    p = nullptr;
  }
}

void sdlReleaseSlot(SdlGpuScopes* g, SdlSlot& slot) {
  if (slot.fence) {
    SDL_WaitForGPUFences(g->device, true, &slot.fence, 1);   // the one place this waits: teardown
    SDL_ReleaseGPUFence(g->device, slot.fence);
    slot.fence = nullptr;
  }
  releaseGpu(g->device, slot.wave, SDL_ReleaseGPUTransferBuffer);
  releaseGpu(g->device, slot.cells, SDL_ReleaseGPUTransferBuffer);
  releaseGpu(g->device, slot.thumb, SDL_ReleaseGPUTransferBuffer);
  slot.thumbBytes = 0;
}

void sdlDestroy(SdlGpuScopes* g) {
  if (!g) return;
  for (auto& slot : g->slots) sdlReleaseSlot(g, slot);
  releaseGpu(g->device, g->thumb, SDL_ReleaseGPUBuffer);
  releaseGpu(g->device, g->zeros, SDL_ReleaseGPUTransferBuffer);
  releaseGpu(g->device, g->cells, SDL_ReleaseGPUBuffer);
  releaseGpu(g->device, g->wave, SDL_ReleaseGPUBuffer);
  releaseGpu(g->device, g->sampler, SDL_ReleaseGPUSampler);
  releaseGpu(g->device, g->thumbPipeline, SDL_ReleaseGPUComputePipeline);
  releaseGpu(g->device, g->columnsPipeline, SDL_ReleaseGPUComputePipeline);
  delete g;
}

SdlGpuScopes* sdlCreate(SDL_Renderer* renderer, int width, int height) {
  auto* device = renderer ? static_cast<SDL_GPUDevice*>(SDL_GetPointerProperty(
                              SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr))
                          : nullptr;
  if (!device || width <= 0 || height <= 0) return nullptr;
  auto* g = new SdlGpuScopes();
  g->renderer = renderer;
  g->device = device;
  g->width = width;
  g->height = height;
  if (const char* driver = SDL_GetGPUDeviceDriver(device)) g->driver = driver;
  g->columnsPipeline = sdlPipeline(device, true);
  g->thumbPipeline = sdlPipeline(device, false);
  SDL_GPUSamplerCreateInfo sampler {};
  sampler.min_filter = SDL_GPU_FILTER_NEAREST;
  sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
  sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  g->sampler = SDL_CreateGPUSampler(device, &sampler);
  g->cells = sdlStorage(device, kCellWords * 4);
  g->zeros = sdlTransfer(device, kCellWords * 4, false);
  bool ok = g->columnsPipeline && g->thumbPipeline && g->sampler && g->cells && g->zeros;
  if (ok) {
    if (void* mapped = SDL_MapGPUTransferBuffer(device, g->zeros, false)) {
      std::memset(mapped, 0, kCellWords * 4);
      SDL_UnmapGPUTransferBuffer(device, g->zeros);
    } else {
      ok = false;
    }
  }
  for (auto& slot : g->slots) ok = ok && (slot.cells = sdlTransfer(device, kCellWords * 4, true)) != nullptr;
  if (!ok) {
    sdlDestroy(g);
    return nullptr;
  }
  return g;
}

bool sdlEnsureColumns(SdlGpuScopes* g, int columns) {
  if (g->columns == columns) return true;
  if (!g->pending.empty()) return false;
  releaseGpu(g->device, g->wave, SDL_ReleaseGPUBuffer);
  for (auto& slot : g->slots) releaseGpu(g->device, slot.wave, SDL_ReleaseGPUTransferBuffer);
  g->columns = 0;
  const std::size_t bytes = static_cast<std::size_t>(6) * kLevels * columns * 4;
  g->wave = sdlStorage(g->device, bytes);
  bool ok = g->wave != nullptr;
  for (auto& slot : g->slots) ok = ok && (slot.wave = sdlTransfer(g->device, bytes, true)) != nullptr;
  if (ok) g->columns = columns;
  return ok;
}

bool sdlEnsureThumb(SdlGpuScopes* g, SdlSlot& slot, std::size_t bytes) {
  if (g->thumbBytes != bytes) {
    if (!g->pending.empty() && g->thumb) return false;   // in use by a submission in flight
    releaseGpu(g->device, g->thumb, SDL_ReleaseGPUBuffer);
    g->thumbBytes = 0;
    g->thumb = sdlStorage(g->device, bytes);
    if (!g->thumb) return false;
    g->thumbBytes = bytes;
  }
  if (slot.thumbBytes != bytes) {
    releaseGpu(g->device, slot.thumb, SDL_ReleaseGPUTransferBuffer);
    slot.thumbBytes = 0;
    slot.thumb = sdlTransfer(g->device, bytes, true);
    if (!slot.thumb) return false;
    slot.thumbBytes = bytes;
  }
  return true;
}

bool sdlIssue(SdlGpuScopes* g, SDL_Texture* source, int orientation, int columns, s::Matrix matrix,
              int thumbWidth, int thumbHeight, std::uint64_t submittedAtNs) {
  if (!g || !source || g->pending.size() >= kSlots) return false;
  const bool turned = orientation == 90 || orientation == 270;
  const int width = turned ? g->height : g->width;
  const int height = turned ? g->width : g->height;
  if (columns < 1 || columns > width || !sdlEnsureColumns(g, columns)) return false;
  auto* texture = static_cast<SDL_GPUTexture*>(
    SDL_GetPointerProperty(SDL_GetTextureProperties(source), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
  if (!texture || source->w != g->width || source->h != g->height) return false;
  SdlSlot& slot = g->slots[g->next];
  if (slot.fence) return false;   // collected slots release their fence; this one never was
  const bool wantThumb = thumbWidth > 0 && thumbHeight > 0;
  const std::size_t thumbBytes = wantThumb ? static_cast<std::size_t>(thumbWidth) * thumbHeight * 4 : 4;
  if (!sdlEnsureThumb(g, slot, thumbBytes)) return false;

  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g->device);
  if (!cmd) return false;
  const Uint32 cellBytes = static_cast<Uint32>(kCellWords * 4);
  const Uint32 waveBytes = static_cast<Uint32>(6 * kLevels * columns * 4);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation zeros {g->zeros, 0};
  SDL_GPUBufferRegion cellRegion {g->cells, 0, cellBytes};
  SDL_UploadToGPUBuffer(copy, &zeros, &cellRegion, false);
  SDL_EndGPUCopyPass(copy);

  const auto k = s::exactCoefficients(matrix);
  const Uint32 params[12] = {static_cast<Uint32>(width), static_cast<Uint32>(height), static_cast<Uint32>(g->width),
                             static_cast<Uint32>(g->height), static_cast<Uint32>(columns),
                             static_cast<Uint32>(orientation), k.kr, k.kg, k.kb,
                             wantThumb ? static_cast<Uint32>(thumbWidth) : 0u,
                             wantThumb ? static_cast<Uint32>(thumbHeight) : 0u, 0u};
  SDL_GPUStorageBufferReadWriteBinding buffers[3] {};
  buffers[0].buffer = g->wave;
  buffers[1].buffer = g->cells;
  buffers[2].buffer = g->thumb;
  SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, nullptr, 0, buffers, 3);
  const SDL_GPUTextureSamplerBinding frame {texture, g->sampler};
  SDL_BindGPUComputePipeline(pass, g->columnsPipeline);
  SDL_BindGPUComputeSamplers(pass, 0, &frame, 1);
  SDL_PushGPUComputeUniformData(cmd, 0, params, sizeof(params));
  SDL_DispatchGPUCompute(pass, static_cast<Uint32>(columns), 1, 1);
  if (wantThumb) {
    SDL_BindGPUComputePipeline(pass, g->thumbPipeline);
    SDL_BindGPUComputeSamplers(pass, 0, &frame, 1);
    SDL_PushGPUComputeUniformData(cmd, 0, params, sizeof(params));
    SDL_DispatchGPUCompute(pass, static_cast<Uint32>((thumbWidth + 7) / 8), static_cast<Uint32>((thumbHeight + 7) / 8), 1);
  }
  SDL_EndGPUComputePass(pass);

  copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUBufferRegion waveRegion {g->wave, 0, waveBytes};
  SDL_GPUTransferBufferLocation waveOut {slot.wave, 0};
  SDL_DownloadFromGPUBuffer(copy, &waveRegion, &waveOut);
  SDL_GPUTransferBufferLocation cellsOut {slot.cells, 0};
  SDL_DownloadFromGPUBuffer(copy, &cellRegion, &cellsOut);
  if (wantThumb) {
    SDL_GPUBufferRegion thumbRegion {g->thumb, 0, static_cast<Uint32>(thumbBytes)};
    SDL_GPUTransferBufferLocation thumbOut {slot.thumb, 0};
    SDL_DownloadFromGPUBuffer(copy, &thumbRegion, &thumbOut);
  }
  SDL_EndGPUCopyPass(copy);
  slot.fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (!slot.fence) return false;
  slot.meta = {};
  slot.meta.width = width;
  slot.meta.height = height;
  slot.meta.columns = columns;
  slot.meta.matrix = matrix;
  slot.meta.submittedAtNs = submittedAtNs;
  slot.meta.thumbWidth = wantThumb ? thumbWidth : 0;
  slot.meta.thumbHeight = wantThumb ? thumbHeight : 0;
  g->pending.push_back(g->next);
  g->next = (g->next + 1) % kSlots;
  return true;
}

// Stage now; issued by the next call, after the present (see above).
bool sdlSubmit(SdlGpuScopes* g, SDL_Texture* source, int orientation, int columns, s::Matrix matrix,
               int thumbWidth, int thumbHeight, std::uint64_t submittedAtNs) {
  if (!g || !source || g->staged.valid || g->pending.size() >= kSlots) return false;
  g->staged = {true, source, orientation, columns, thumbWidth, thumbHeight, matrix, submittedAtNs};
  return true;
}

void sdlIssueStaged(SdlGpuScopes* g) {
  if (!g || !g->staged.valid) return;
  const auto st = g->staged;
  g->staged.valid = false;
  sdlIssue(g, st.source, st.orientation, st.columns, st.matrix, st.thumbWidth, st.thumbHeight, st.submittedAtNs);
}

int sdlInFlight(SdlGpuScopes* g) {
  return g ? static_cast<int>(g->pending.size()) + (g->staged.valid ? 1 : 0) : 0;
}

bool sdlCollect(SdlGpuScopes* g, GpuScopeCounts& out) {
  sdlIssueStaged(g);
  if (!g || g->pending.empty()) return false;
  SdlSlot& slot = g->slots[g->pending.front()];
  if (!slot.fence || !SDL_QueryGPUFence(g->device, slot.fence)) return false;   // still on the GPU
  const std::size_t waveWords = static_cast<std::size_t>(6) * kLevels * slot.meta.columns;
  out = slot.meta;
  bool ok = true;
  if (const auto* wave = static_cast<const std::uint32_t*>(SDL_MapGPUTransferBuffer(g->device, slot.wave, false))) {
    out.wave.assign(wave, wave + waveWords);
    SDL_UnmapGPUTransferBuffer(g->device, slot.wave);
  } else {
    ok = false;
  }
  if (const auto* words = static_cast<const std::uint32_t*>(SDL_MapGPUTransferBuffer(g->device, slot.cells, false))) {
    out.cells.assign(words, words + kCellWords);
    out.fingerprint = (static_cast<std::uint64_t>(words[kFingerprint + 1]) << 32) | words[kFingerprint];
    SDL_UnmapGPUTransferBuffer(g->device, slot.cells);
  } else {
    ok = false;
  }
  if (slot.meta.thumbWidth > 0) {
    if (const auto* thumb = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(g->device, slot.thumb, false))) {
      out.thumb.assign(thumb, thumb + static_cast<std::size_t>(slot.meta.thumbWidth) * slot.meta.thumbHeight * 4);
      SDL_UnmapGPUTransferBuffer(g->device, slot.thumb);
    } else {
      ok = false;
    }
  }
  SDL_ReleaseGPUFence(g->device, slot.fence);
  slot.fence = nullptr;
  g->pending.pop_front();
  return ok;
}
}  // namespace
#else   // SDL < 3.4: no SDL_GPU texture property to read the frame through.
namespace {
struct SdlGpuScopes {
  std::string driver;
};
SdlGpuScopes* sdlCreate(SDL_Renderer*, int, int) { return nullptr; }
int sdlInFlight(SdlGpuScopes*) { return 0; }
void sdlDestroy(SdlGpuScopes*) {}
bool sdlSubmit(SdlGpuScopes*, SDL_Texture*, int, int, s::Matrix, int, int, std::uint64_t) { return false; }
bool sdlCollect(SdlGpuScopes*, GpuScopeCounts&) { return false; }
}  // namespace
#endif

// ----------------------------------------------------------------------------
// Public entry points: Direct3D 11 where the renderer is that (Windows'
// output renderer), SDL_GPU where it is the "gpu" renderer (macOS, Linux).
namespace {
struct ScopesHandle {
  void* d3d11 = nullptr;
  SdlGpuScopes* sdl = nullptr;
};
}  // namespace

void* createGpuScopes(SDL_Renderer* renderer, int width, int height) {
  if (std::getenv("DECKBOY_CPU_SCOPES")) return nullptr;   // the A/B: the CPU analyser instead
  void* d3d11 = d3d11Create(renderer, width, height);
  SdlGpuScopes* sdl = d3d11 ? nullptr : sdlCreate(renderer, width, height);
  if (!d3d11 && !sdl) return nullptr;
  return new ScopesHandle {d3d11, sdl};
}

void destroyGpuScopes(void* handle) {
  auto* h = static_cast<ScopesHandle*>(handle);
  if (!h) return;
  d3d11Destroy(h->d3d11);
  sdlDestroy(h->sdl);
  delete h;
}

std::string gpuScopesAdapter(void* handle) {
  auto* h = static_cast<ScopesHandle*>(handle);
  if (!h) return {};
  if (h->d3d11) return d3d11Adapter(h->d3d11);
  return h->sdl ? "SDL_GPU " + h->sdl->driver : std::string();
}

int gpuScopesInFlight(void* handle) {
  auto* h = static_cast<ScopesHandle*>(handle);
  if (!h) return 0;
  return h->d3d11 ? d3d11InFlight(h->d3d11) : sdlInFlight(h->sdl);
}

bool submitGpuScopes(void* handle, SDL_Texture* source, int orientation, int columns, s::Matrix matrix,
                     int thumbWidth, int thumbHeight, std::uint64_t submittedAtNs) {
  auto* h = static_cast<ScopesHandle*>(handle);
  if (!h) return false;
  if (h->d3d11) return d3d11Submit(h->d3d11, source, orientation, columns, matrix, thumbWidth, thumbHeight, submittedAtNs);
  return sdlSubmit(h->sdl, source, orientation, columns, matrix, thumbWidth, thumbHeight, submittedAtNs);
}

bool collectGpuScopes(void* handle, GpuScopeCounts& out) {
  auto* h = static_cast<ScopesHandle*>(handle);
  if (!h) return false;
  return h->d3d11 ? d3d11Collect(h->d3d11, out) : sdlCollect(h->sdl, out);
}

}  // namespace deckboy::gpu
