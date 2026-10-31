// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// gpu_scopes.metal -- gpu_scopes.hlsl for SDL_GPU's Metal, line for line.
// The comments there explain it; change both together, then run
// tools/build_scopes_shaders.py, which embeds this source as it is.
//
// SDL_GPU's Metal order: [[texture]] the sampled frame; [[buffer]] the
// uniforms first, then the read-write buffers (waveforms, cells, samples).
#include <metal_stdlib>
using namespace metal;

struct Params {
  uint width; uint height; uint srcWidth; uint srcHeight;
  uint columns; uint orientation; uint kr; uint kg;
  uint kb; uint thumbWidth; uint thumbHeight; uint pad;
};

static uint3 bytesAt(texture2d<float, access::sample> src, constant Params& p, uint x, uint y) {
  uint sx = x, sy = y;
  if (p.orientation == 90) { sx = y; sy = p.srcHeight - 1 - x; }
  else if (p.orientation == 180) { sx = p.srcWidth - 1 - x; sy = p.srcHeight - 1 - y; }
  else if (p.orientation == 270) { sx = p.srcWidth - 1 - y; sy = x; }
  return uint3(src.read(uint2(sx, sy)).rgb * 255.0f + 0.5f);
}
static uint mixHash(uint v) {
  v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
  return v;
}
static uint diamondCell(uint a, uint g) {
  return min((a + g) * 256 / 510, 255u) * 256 + min((a + 255 - g) * 256 / 510, 255u);
}

kernel void columnsMain(texture2d<float, access::sample> src [[texture(0)]],
                        sampler srcSampler [[sampler(0)]],
                        constant Params& p [[buffer(0)]],
                        device uint* wave [[buffer(1)]],
                        device atomic_uint* cells [[buffer(2)]],
                        device uint* thumb [[buffer(3)]],
                        uint3 group [[threadgroup_position_in_grid]],
                        uint tid [[thread_index_in_threadgroup]]) {
  threadgroup atomic_uint counts[6 * 256];
  threadgroup atomic_uint groupSum;
  threadgroup atomic_uint groupMix;
  for (uint i = tid; i < 6 * 256; i += 256) atomic_store_explicit(&counts[i], 0u, memory_order_relaxed);
  if (tid == 0) {
    atomic_store_explicit(&groupSum, 0u, memory_order_relaxed);
    atomic_store_explicit(&groupMix, 0u, memory_order_relaxed);
  }
  threadgroup_barrier(mem_flags::mem_threadgroup);
  const uint col = group.x;
  const uint x0 = (col * p.width + p.columns - 1) / p.columns;
  const uint x1 = ((col + 1) * p.width + p.columns - 1) / p.columns;
  const uint strip = x1 - x0;
  const uint total = strip * p.height;
  const uint mb = 10000 - p.kb, mr = 10000 - p.kr;
  const uint cbDen = 510 * mb, crDen = 510 * mr;
  uint cellV = 0xffffffffu, cellU = 0xffffffffu, cellL = 0xffffffffu;
  uint runV = 0, runU = 0, runL = 0;
  uint sum = 0, mixed = 0;
  for (uint q = tid; q < total; q += 256) {
    const uint y = q / strip;
    const uint x = x0 + (q - y * strip);
    const uint3 c = bytesAt(src, p, x, y);
    const uint n = p.kr * c.r + p.kg * c.g + p.kb * c.b;
    const uint yRow = min(n * 256 / 2550000, 255u);
    const uint cbRow = min((10000 * c.b + 255 * mb - n) * 256 / cbDen, 255u);
    const uint crRow = min((10000 * c.r + 255 * mr - n) * 256 / crDen, 255u);
    atomic_fetch_add_explicit(&counts[yRow], 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&counts[256 + cbRow], 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&counts[512 + crRow], 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&counts[768 + min(c.r * 256 / 255, 255u)], 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&counts[1024 + min(c.g * 256 / 255, 255u)], 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&counts[1280 + min(c.b * 256 / 255, 255u)], 1u, memory_order_relaxed);
    const uint v = crRow * 256 + cbRow;
    const uint u = 65536 + diamondCell(c.b, c.g);
    const uint l = 131072 + diamondCell(c.r, c.g);
    if (v != cellV) { if (runV != 0) atomic_fetch_add_explicit(&cells[cellV], runV, memory_order_relaxed); cellV = v; runV = 0; }
    if (u != cellU) { if (runU != 0) atomic_fetch_add_explicit(&cells[cellU], runU, memory_order_relaxed); cellU = u; runU = 0; }
    if (l != cellL) { if (runL != 0) atomic_fetch_add_explicit(&cells[cellL], runL, memory_order_relaxed); cellL = l; runL = 0; }
    ++runV; ++runU; ++runL;
    const uint h = mixHash((c.r | (c.g << 8) | (c.b << 16)) ^ mixHash(y * p.width + x + 0x9e3779b9u));
    sum += h;
    mixed ^= mixHash(h + 0x632be5abu);
  }
  if (runV != 0) atomic_fetch_add_explicit(&cells[cellV], runV, memory_order_relaxed);
  if (runU != 0) atomic_fetch_add_explicit(&cells[cellU], runU, memory_order_relaxed);
  if (runL != 0) atomic_fetch_add_explicit(&cells[cellL], runL, memory_order_relaxed);
  atomic_fetch_add_explicit(&groupSum, sum, memory_order_relaxed);
  atomic_fetch_xor_explicit(&groupMix, mixed, memory_order_relaxed);
  threadgroup_barrier(mem_flags::mem_threadgroup);
  for (uint j = tid; j < 6 * 256; j += 256)
    wave[(j / 256) * 256 * p.columns + (j % 256) * p.columns + col] = atomic_load_explicit(&counts[j], memory_order_relaxed);
  if (tid == 0) {
    atomic_fetch_add_explicit(&cells[3 * 65536], atomic_load_explicit(&groupSum, memory_order_relaxed), memory_order_relaxed);
    atomic_fetch_xor_explicit(&cells[3 * 65536 + 1], atomic_load_explicit(&groupMix, memory_order_relaxed), memory_order_relaxed);
  }
}

kernel void thumbMain(texture2d<float, access::sample> src [[texture(0)]],
                      sampler srcSampler [[sampler(0)]],
                      constant Params& p [[buffer(0)]],
                      device uint* wave [[buffer(1)]],
                      device atomic_uint* cells [[buffer(2)]],
                      device uint* thumb [[buffer(3)]],
                      uint3 id [[thread_position_in_grid]]) {
  if (id.x >= p.thumbWidth || id.y >= p.thumbHeight) return;
  const uint3 c = bytesAt(src, p, min(p.width - 1, id.x * p.width / p.thumbWidth),
                          min(p.height - 1, id.y * p.height / p.thumbHeight));
  thumb[id.y * p.thumbWidth + id.x] = c.b | (c.g << 8) | (c.r << 16) | 0xff000000u;
}
