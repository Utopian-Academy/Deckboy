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

#ifdef _WIN32
namespace {
// One thread group a waveform column; the group's 256 threads walk the strip
// of pixels that column covers and count into shared memory, then write the
// column out whole. The vectorscope and the diamonds are 65,536 cells each,
// too many to share, so each thread counts a RUN of equal cells and adds the
// run once: a flat field costs one global add a thread, not one a pixel.
// The bins are core/scopes.cpp's exact ones (see exactCoefficients there):
//   255 * 10000 Y' = n = kr R + kg G + kb B,  row = n * 256 / 2550000
//   Cb row = (10000 B - n + 255 (10000 - kb)) * 256 / (510 (10000 - kb))
// all inside 32 bits (at most 1.21e9).
const char kScopesShader[] = R"(
Texture2D<float4> src : register(t0);
RWByteAddressBuffer wave : register(u0);
RWByteAddressBuffer cells : register(u1);
RWTexture2D<uint> thumb : register(u2);
cbuffer Params : register(b0) {
  uint width; uint height; uint srcWidth; uint srcHeight;
  uint columns; uint orientation; uint kr; uint kg;
  uint kb; uint thumbWidth; uint thumbHeight; uint pad;
};
uint3 bytesAt(uint x, uint y) {
  uint sx = x, sy = y;
  if (orientation == 90) { sx = y; sy = srcHeight - 1 - x; }
  else if (orientation == 180) { sx = srcWidth - 1 - x; sy = srcHeight - 1 - y; }
  else if (orientation == 270) { sx = srcWidth - 1 - y; sy = x; }
  return uint3(src.Load(int3(sx, sy, 0)).rgb * 255.0 + 0.5);
}
uint mixHash(uint v) {
  v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
  return v;
}
uint diamondCell(uint a, uint g) {
  return min((a + g) * 256 / 510, 255) * 256 + min((a + 255 - g) * 256 / 510, 255);
}
groupshared uint counts[6 * 256];
groupshared uint groupSum;
groupshared uint groupMix;
[numthreads(256, 1, 1)]
void columnsMain(uint3 group : SV_GroupID, uint tid : SV_GroupIndex) {
  for (uint i = tid; i < 6 * 256; i += 256) counts[i] = 0;
  if (tid == 0) { groupSum = 0; groupMix = 0; }
  GroupMemoryBarrierWithGroupSync();
  const uint col = group.x;
  const uint x0 = (col * width + columns - 1) / columns;
  const uint x1 = ((col + 1) * width + columns - 1) / columns;
  const uint strip = x1 - x0;
  const uint total = strip * height;
  const uint mb = 10000 - kb, mr = 10000 - kr;
  const uint cbDen = 510 * mb, crDen = 510 * mr;
  uint cellV = 0xffffffffu, cellU = 0xffffffffu, cellL = 0xffffffffu;
  uint runV = 0, runU = 0, runL = 0;
  uint sum = 0, mixed = 0;
  for (uint i = tid; i < total; i += 256) {
    const uint y = i / strip;
    const uint x = x0 + (i - y * strip);
    const uint3 c = bytesAt(x, y);
    const uint n = kr * c.r + kg * c.g + kb * c.b;
    const uint yRow = min(n * 256 / 2550000, 255);
    const uint cbRow = min((10000 * c.b + 255 * mb - n) * 256 / cbDen, 255);
    const uint crRow = min((10000 * c.r + 255 * mr - n) * 256 / crDen, 255);
    InterlockedAdd(counts[yRow], 1);
    InterlockedAdd(counts[256 + cbRow], 1);
    InterlockedAdd(counts[512 + crRow], 1);
    InterlockedAdd(counts[768 + min(c.r * 256 / 255, 255)], 1);
    InterlockedAdd(counts[1024 + min(c.g * 256 / 255, 255)], 1);
    InterlockedAdd(counts[1280 + min(c.b * 256 / 255, 255)], 1);
    const uint v = crRow * 256 + cbRow;
    const uint u = 65536 + diamondCell(c.b, c.g);
    const uint l = 131072 + diamondCell(c.r, c.g);
    if (v != cellV) { if (runV) cells.InterlockedAdd(cellV * 4, runV); cellV = v; runV = 0; }
    if (u != cellU) { if (runU) cells.InterlockedAdd(cellU * 4, runU); cellU = u; runU = 0; }
    if (l != cellL) { if (runL) cells.InterlockedAdd(cellL * 4, runL); cellL = l; runL = 0; }
    ++runV; ++runU; ++runL;
    const uint h = mixHash((c.r | (c.g << 8) | (c.b << 16)) ^ mixHash(y * width + x + 0x9e3779b9u));
    sum += h;
    mixed ^= mixHash(h + 0x632be5abu);
  }
  if (runV) cells.InterlockedAdd(cellV * 4, runV);
  if (runU) cells.InterlockedAdd(cellU * 4, runU);
  if (runL) cells.InterlockedAdd(cellL * 4, runL);
  InterlockedAdd(groupSum, sum);
  InterlockedXor(groupMix, mixed);
  GroupMemoryBarrierWithGroupSync();
  for (uint j = tid; j < 6 * 256; j += 256) wave.Store(((j / 256) * 256 * columns + (j % 256) * columns + col) * 4, counts[j]);
  if (tid == 0) {
    cells.InterlockedAdd(3 * 65536 * 4, groupSum);
    cells.InterlockedXor((3 * 65536 + 1) * 4, groupMix);
  }
}
[numthreads(8, 8, 1)]
void thumbMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= thumbWidth || id.y >= thumbHeight) return;
  const uint3 c = bytesAt(min(width - 1, id.x * width / thumbWidth), min(height - 1, id.y * height / thumbHeight));
  thumb[id.xy] = c.b | (c.g << 8) | (c.r << 16) | 0xff000000u;
}
)";

constexpr int kSlots = 3;   // in flight at once: the render thread never waits for one

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
  const HRESULT hr = compile(kScopesShader, sizeof(kScopesShader) - 1, "gpu_scopes", nullptr, nullptr, entry,
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

ID3D11Texture2D* makeThumbTexture(ID3D11Device* device, int w, int h, bool staging) {
  D3D11_TEXTURE2D_DESC desc {};
  desc.Width = static_cast<UINT>(w);
  desc.Height = static_cast<UINT>(h);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R32_UINT;
  desc.SampleDesc.Count = 1;
  desc.Usage = staging ? D3D11_USAGE_STAGING : D3D11_USAGE_DEFAULT;
  desc.BindFlags = staging ? 0 : D3D11_BIND_UNORDERED_ACCESS;
  desc.CPUAccessFlags = staging ? D3D11_CPU_ACCESS_READ : 0;
  ID3D11Texture2D* tex = nullptr;
  return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &tex)) ? tex : nullptr;
}

struct Slot {
  ID3D11Buffer* wave = nullptr;
  ID3D11Buffer* cells = nullptr;
  ID3D11Texture2D* thumb = nullptr;
  int thumbWidth = 0, thumbHeight = 0;
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
  ID3D11Texture2D* thumb = nullptr;
  ID3D11UnorderedAccessView* thumbUav = nullptr;
  int thumbWidth = 0, thumbHeight = 0;
  ID3D11ShaderResourceView* srv = nullptr;   // of the last source seen
  ID3D11Texture2D* srvSource = nullptr;
  int width = 0, height = 0, columns = 0;
  Slot slots[kSlots];
  std::deque<int> pending;   // submitted, oldest first
  int next = 0;
};

void releaseSlotThumb(Slot& slot) {
  releaseCom(slot.thumb);
  slot.thumbWidth = slot.thumbHeight = 0;
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

bool ensureThumb(D3D11Scopes* g, Slot& slot, int w, int h) {
  if (g->thumbWidth != w || g->thumbHeight != h) {
    releaseCom(g->thumbUav);
    releaseCom(g->thumb);
    g->thumbWidth = g->thumbHeight = 0;
    g->thumb = makeThumbTexture(g->device, w, h, false);
    if (!g->thumb || FAILED(g->device->CreateUnorderedAccessView(g->thumb, nullptr, &g->thumbUav))) return false;
    g->thumbWidth = w;
    g->thumbHeight = h;
  }
  if (slot.thumbWidth != w || slot.thumbHeight != h) {
    releaseSlotThumb(slot);
    slot.thumb = makeThumbTexture(g->device, w, h, true);
    if (!slot.thumb) return false;
    slot.thumbWidth = w;
    slot.thumbHeight = h;
  }
  return true;
}
}  // namespace
#endif

void* createGpuScopes(SDL_Renderer* renderer, int width, int height) {
#ifdef _WIN32
  ID3D11Device* device = rendererDevice(renderer);
  if (!device || width <= 0 || height <= 0) return nullptr;
  if (std::getenv("DECKBOY_CPU_SCOPES")) return nullptr;   // the A/B: the CPU analyser instead
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

void destroyGpuScopes(void* handle) {
#ifdef _WIN32
  destroyD3D11Scopes(static_cast<D3D11Scopes*>(handle));
#else
  (void) handle;
#endif
}

std::string gpuScopesAdapter(void* handle) {
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

int gpuScopesInFlight(void* handle) {
#ifdef _WIN32
  auto* g = static_cast<D3D11Scopes*>(handle);
  return g ? static_cast<int>(g->pending.size()) : 0;
#else
  (void) handle;
  return 0;
#endif
}

bool submitGpuScopes(void* handle, SDL_Texture* source, int orientation, int columns, s::Matrix matrix,
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
  if (wantThumb && !ensureThumb(g, slot, thumbWidth, thumbHeight)) return false;

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

bool collectGpuScopes(void* handle, GpuScopeCounts& out) {
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
    const std::size_t rowBytes = static_cast<std::size_t>(slot.meta.thumbWidth) * 4;
    out.thumb.resize(rowBytes * slot.meta.thumbHeight);
    for (int y = 0; y < slot.meta.thumbHeight; ++y)
      std::memcpy(out.thumb.data() + rowBytes * y,
                  static_cast<const std::uint8_t*>(thumb.pData) + static_cast<std::size_t>(thumb.RowPitch) * y,
                  rowBytes);
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

}  // namespace deckboy::gpu
