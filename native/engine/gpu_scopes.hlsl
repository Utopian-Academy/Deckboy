// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// gpu_scopes.hlsl -- the scopes' 8-bit analysis (engine/gpu_scopes.cpp).
//
// One source, three targets: Direct3D 11 compiles it at run time
// (D3DCompile, cs_5_0); SDL_GPU's Vulkan and Direct3D 12 take SPIR-V and DXIL
// that tools/build_scopes_shaders.py makes with dxc and embeds in
// gpu_scopes_shaders.inc. Metal's copy is gpu_scopes.metal, line for line.
// Edit both, then run the script; CI fails if the embedded copies are stale.
//
// The bindings follow SDL_GPU's compute order: sampled textures (the frame),
// then read-write buffers (the waveforms, the cells, the false colour
// samples), then the uniforms. The samples are a buffer, not a texture: a
// texture download has a row pitch to align on Direct3D 12, and a buffer has
// none.
//
// One thread group a waveform column; the group's 256 threads walk the strip
// of pixels that column covers and count into shared memory, then write the
// column out whole. The vectorscope and the diamonds are 65,536 cells each,
// too many to share, so each thread counts a RUN of equal cells and adds the
// run once: a flat field costs one global add a thread, not one a pixel.
// The bins are core/scopes.cpp's exact ones (see exactCoefficients there):
//   255 * 10000 Y' = n = kr R + kg G + kb B,  row = n * 256 / 2550000
//   Cb row = (10000 B - n + 255 (10000 - kb)) * 256 / (510 (10000 - kb))
// all inside 32 bits (at most 1.21e9).
#if defined(__spirv__)
#define BINDING(slot, set) [[vk::binding(slot, set)]]
#define COMBINED [[vk::combinedImageSampler]]
#define SPACE(n)
#elif defined(SDL_GPU_D3D12)
#define BINDING(slot, set)
#define COMBINED
#define SPACE(n) , space##n
#else
#define BINDING(slot, set)
#define COMBINED
#define SPACE(n)
#endif

COMBINED BINDING(0, 0) Texture2D<float4> src : register(t0 SPACE(0));
COMBINED BINDING(0, 0) SamplerState srcSampler : register(s0 SPACE(0));
BINDING(0, 1) RWByteAddressBuffer wave : register(u0 SPACE(1));
BINDING(1, 1) RWByteAddressBuffer cells : register(u1 SPACE(1));
BINDING(2, 1) RWByteAddressBuffer thumb : register(u2 SPACE(1));
BINDING(0, 2) cbuffer Params : register(b0 SPACE(2)) {
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
  for (uint p = tid; p < total; p += 256) {
    const uint y = p / strip;
    const uint x = x0 + (p - y * strip);
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
    if (v != cellV) { if (runV != 0) cells.InterlockedAdd(cellV * 4, runV); cellV = v; runV = 0; }
    if (u != cellU) { if (runU != 0) cells.InterlockedAdd(cellU * 4, runU); cellU = u; runU = 0; }
    if (l != cellL) { if (runL != 0) cells.InterlockedAdd(cellL * 4, runL); cellL = l; runL = 0; }
    ++runV; ++runU; ++runL;
    const uint h = mixHash((c.r | (c.g << 8) | (c.b << 16)) ^ mixHash(y * width + x + 0x9e3779b9u));
    sum += h;
    mixed ^= mixHash(h + 0x632be5abu);
  }
  if (runV != 0) cells.InterlockedAdd(cellV * 4, runV);
  if (runU != 0) cells.InterlockedAdd(cellU * 4, runU);
  if (runL != 0) cells.InterlockedAdd(cellL * 4, runL);
  InterlockedAdd(groupSum, sum);
  InterlockedXor(groupMix, mixed);
  GroupMemoryBarrierWithGroupSync();
  for (uint j = tid; j < 6 * 256; j += 256) wave.Store(((j / 256) * 256 * columns + (j % 256) * columns + col) * 4, counts[j]);
  if (tid == 0) {
    cells.InterlockedAdd(3 * 65536 * 4, groupSum);
    cells.InterlockedXor((3 * 65536 + 1) * 4, groupMix);
  }
}

// The frame nearest-sampled to thumbWidth x thumbHeight, as BGRA words: the
// false colour picture's source, picked exactly as buildFalseColour picks.
[numthreads(8, 8, 1)]
void thumbMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= thumbWidth || id.y >= thumbHeight) return;
  const uint3 c = bytesAt(min(width - 1, id.x * width / thumbWidth), min(height - 1, id.y * height / thumbHeight));
  thumb.Store((id.y * thumbWidth + id.x) * 4, c.b | (c.g << 8) | (c.r << 16) | 0xff000000u);
}
