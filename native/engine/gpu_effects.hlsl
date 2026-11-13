// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// gpu_effects.hlsl -- picture effects on the graphics card (engine/gpu_effects.cpp).
//
// THE CPU VERSION IS THE REFERENCE. Every kernel here is core/cue_effects.hpp's
// effect of the same name, step for step, in float where that is in double;
// tools/check_gpu_effects.py runs both on the same pictures and fails on a
// difference. Anything an effect works out once per frame (a table, a matrix,
// a hash seed) is worked out on the CPU by the same code and passed in, so the
// GPU only does the per-pixel part.
//
// One source, three targets, as gpu_scopes.hlsl: Direct3D 11 compiles it at
// run time (cs_5_0); SDL_GPU's Direct3D 12 and Vulkan take DXIL and SPIR-V
// from tools/build_effects_shaders.py; Metal's copy is gpu_effects.metal, line
// for line. Edit both, then run the script.
//
// Bindings, in SDL_GPU's compute order: two sampled textures (the picture, and
// an auxiliary one: a reduced copy, tile averages), the read-write output, the
// uniforms. Every kernel declares the whole layout so one set fits them all.
#if defined(__spirv__)
#define BINDING(slot, set) [[vk::binding(slot, set)]]
#define COMBINED [[vk::combinedImageSampler]]
#define SPACE(n)
#define RGBA8 [[vk::image_format("rgba8")]]
#elif defined(SDL_GPU_D3D12)
#define BINDING(slot, set)
#define COMBINED
#define SPACE(n) , space##n
#define RGBA8
#else
#define BINDING(slot, set)
#define COMBINED
#define SPACE(n)
#define RGBA8
#endif

COMBINED BINDING(0, 0) Texture2D<float4> src : register(t0 SPACE(0));
COMBINED BINDING(0, 0) SamplerState srcSampler : register(s0 SPACE(0));
COMBINED BINDING(1, 0) Texture2D<float4> aux : register(t1 SPACE(0));
COMBINED BINDING(1, 0) SamplerState auxSampler : register(s1 SPACE(0));
RGBA8 BINDING(0, 1) RWTexture2D<unorm float4> dst : register(u0 SPACE(1));
BINDING(0, 2) cbuffer Params : register(b0 SPACE(2)) {
  uint kind; uint width; uint height; uint frame;   // width/height: the OUTPUT
  uint srcW; uint srcH; uint auxW; uint auxH;
  int i0; int i1; int i2; int i3;
  float amt; float pA; float pB; float pC;
  float pD; float f0; float f1; float f2;
  float f3; float f4; float f5; float f6;
  float f7; float f8; float f9; float f10;
  float f11; float f12; float f13; float f14;
  uint4 lut[48];      // 768 bytes: three 256-entry channel tables
  float4 tab[64];     // 256 floats: glow's knee (kept small: 9 KB of uniforms
                      // a pass came through Vulkan cut short)
};

// Kinds (gpu_effects.cpp, GpuKind).
#define K_LUT 1
#define K_THRESHOLD 2
#define K_COLOUR 3
#define K_COLOUR_REPLACE 4
#define K_VIGNETTE 5
#define K_GRAIN 6
#define K_CHANNEL_OFFSET 7
#define K_MIRROR 8
#define K_LUMA_KEY 9
#define K_EDGES 10
#define K_WARP 11
#define K_MOSAIC 12
#define K_MIX 13

uint lutByte(uint i) {
  const uint word = lut[i >> 4][(i >> 2) & 3];
  return (word >> ((i & 3) * 8)) & 255;
}
float tabAt(uint i) { return tab[i >> 2][i & 3]; }
uint4 texel(int x, int y) { return uint4(src.Load(int3(x, y, 0)) * 255.0 + 0.5); }
uint4 auxTexel(int x, int y) { return uint4(aux.Load(int3(x, y, 0)) * 255.0 + 0.5); }
// detail::clamp8: clamp, then TRUNCATE (static_cast), as the CPU does.
float clamp8(float v) { return floor(clamp(v, 0.0, 255.0)); }
void put(uint2 at, float4 bytes) { dst[at] = bytes / 255.0; }
float smoothstep01(float e0, float e1, float x) {
  const float t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}
uint xorshift(uint h) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return h; }
// Lightspeed's two radial tables, one entry each, as the CPU builds them
// (1024 entries, i / 1023 of the way out): where the light came from, and its
// Doppler gain. f6 beta, f7 gamma, f8 half the field of view, f2 maxR.
float boostRadius(int i) {
  const float cosSeen = cos(float(i) / 1023.0 * f8);
  const float denom = 1.0 - f6 * cosSeen;
  const float cosRest = clamp((cosSeen - f6) / (abs(denom) < 1e-6 ? 1e-6 : denom), -1.0, 1.0);
  return acos(cosRest) / f8 * f2;
}
float boostGain(int i) {
  const float cosSeen = cos(float(i) / 1023.0 * f8);
  const float doppler = 1.0 / max(1e-6, f7 * (1.0 - f6 * cosSeen));
  return tanh((doppler - 1.0) * 1.2) * pB;
}
// std::lround: halves away from zero (HLSL's round() goes to even).
int lroundf(float v) { return int(sign(v) * floor(abs(v) + 0.5)); }

// ---- one output pixel from the picture ------------------------------------
[numthreads(8, 8, 1)]
void pointMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const int x = int(id.x), y = int(id.y);
  const uint4 p = texel(x, y);
  float4 o = float4(p);
  if (kind == K_LUT) {
    // i0 > 0: scanlines -- only every other band of i0 / 2 rows.
    if (i0 <= 0 || ((y / (i0 / 2)) & 1) != 0) {
      o.r = lutByte(p.r); o.g = lutByte(256 + p.g); o.b = lutByte(512 + p.b);
    }
  } else if (kind == K_THRESHOLD) {
    const float luma = p.r * 0.299 + p.g * 0.587 + p.b * 0.114;
    const float3 v = float3(p.rgb);
    const float3 lit = floor(clamp(v * (1.0 - amt) + 255.0 * amt, 0.0, 255.0));
    const float3 dark = floor(clamp(v * (1.0 - amt), 0.0, 255.0));
    if (f1 <= 0.5) {
      if (luma >= f0) o.rgb = lit; else o.rgb = dark;
    } else {
      const float t = clamp((luma - (f0 - f1)) / (2.0 * f1), 0.0, 1.0);
      const float eased = t * t * (3.0 - 2.0 * t);
      o.rgb = floor(clamp(dark * (1.0 - eased) + lit * eased, 0.0, 255.0));
    }
  } else if (kind == K_COLOUR) {
    const float3 c0 = float3(p.rgb) / 255.0;
    float3 c = float3(f0 * c0.r + f1 * c0.g + f2 * c0.b,
                      f3 * c0.r + f4 * c0.g + f5 * c0.b,
                      f6 * c0.r + f7 * c0.g + f8 * c0.b);
    const float luma = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
    c = luma + (c - luma) * f12;
    if (f13 > 0.0) {
      const float reach = clamp(min(luma, 1.0 - luma) * 2.0, 0.0, 1.0);
      c += (luma + float3(f9, f10, f11) * reach - c) * f13;
    }
    o.rgb = floor(clamp((c0 + (c - c0) * amt) * 255.0 + 0.5, 0.0, 255.0));
  } else if (kind == K_COLOUR_REPLACE) {
    const int r = int(p.r), g = int(p.g), b = int(p.b);
    const int mx = max(r, max(g, b));
    const int mn = min(r, min(g, b));
    const int c = mx - mn;
    if (c >= 6) {
      const float inv = 1.0 / float(c);
      float h = mx == r ? float(g - b) * inv : mx == g ? 2.0 + float(b - r) * inv : 4.0 + float(r - g) * inv;
      h *= 1.0 / 6.0;
      if (h < 0.0) h += 1.0;
      float d = abs(h - f0);
      if (d > 0.5) d = 1.0 - d;
      if (d < f2 + f3) {
        float weight = smoothstep01(6.0, 40.0, float(c)) * amt;
        if (d > f2) {
          const float t = (d - f2) / f3;
          weight *= 1.0 - t * t * (3.0 - 2.0 * t);
        }
        float turned = h + f1;
        turned -= floor(turned);
        const float sector = turned * 6.0;
        const int i = min(5, int(sector));
        const float rise = float(mn) + float(c) * (sector - float(i));
        const float fall = float(mx) + float(mn) - rise;
        const float hi = float(mx), lo = float(mn);
        const float nr = i == 0 || i == 5 ? hi : i == 1 ? fall : i == 4 ? rise : lo;
        const float ng = i == 1 || i == 2 ? hi : i == 0 ? rise : i == 3 ? fall : lo;
        const float nb = i == 3 || i == 4 ? hi : i == 2 ? rise : i == 5 ? fall : lo;
        o.r = floor(float(r) + (nr - float(r)) * weight + 0.5);
        o.g = floor(float(g) + (ng - float(g)) * weight + 0.5);
        o.b = floor(float(b) + (nb - float(b)) * weight + 0.5);
      }
    }
  } else if (kind == K_VIGNETTE) {
    const float dx = x - f0, dy = y - f1;
    const float r = sqrt(dx * dx + dy * dy) / f2 / f3;
    const float shaped = f4 == 2.0 ? r * r : pow(clamp(r, 0.0, 4.0), f4);
    const float gain = 1.0 - amt * clamp(shaped, 0.0, 1.0);
    o.rgb = floor(clamp(float3(p.rgb) * gain, 0.0, 255.0));
  } else if (kind == K_GRAIN) {
    const uint cell = i1 == 1 ? uint(y) * width + uint(x) : uint(y / i1) * width + uint(x / i1);
    uint h = cell ^ uint(i0);
    h = xorshift(h);
    const float n = (float(h & 0xFFFF) / 32768.0 - 1.0) * f0;
    if (pB <= 0.0005) {
      o.rgb = floor(clamp(float3(p.rgb) + n, 0.0, 255.0));
    } else {
      [unroll] for (uint c = 0; c < 3; ++c) {
        const uint hc = xorshift(h ^ (c * 0x9E3779B9u));
        const float nc = (float(hc & 0xFFFF) / 32768.0 - 1.0) * f0;
        o[c] = floor(clamp(float(p[c]) + n * (1.0 - pB) + nc * pB, 0.0, 255.0));
      }
    }
  } else if (kind == K_CHANNEL_OFFSET) {
    // Byte 2 from one side, byte 0 from the other, as the CPU does.
    if (i2 != 0) {
      o.b = texel(clamp(x + i0, 0, int(width) - 1), y).b;
      o.r = texel(clamp(x - i0, 0, int(width) - 1), y).r;
    } else {
      const int ox = lroundf(f0), oy = lroundf(f1);
      o.b = texel(clamp(x + ox, 0, int(width) - 1), clamp(y + oy, 0, int(height) - 1)).b;
      o.r = texel(clamp(x - ox, 0, int(width) - 1), clamp(y - oy, 0, int(height) - 1)).r;
      if (i1 != 0) {
        o.g = texel(clamp(x + lroundf(f1 * pB), 0, int(width) - 1),
                    clamp(y - lroundf(f0 * pB), 0, int(height) - 1)).g;
      }
    }
  } else if (kind == K_MIRROR) {
    const int w = int(width), hh = int(height);
    int sx = x, sy = y;
    const float lineEndX = w * f0, lineEndY = hh * f0;
    const float lineStartX = w * (1.0 - f0), lineStartY = hh * (1.0 - f0);
    if (i0 == 0 || i0 == 2) sx = w - 1 - x;
    if (i0 == 1 || i0 == 2) sy = hh - 1 - y;
    if (i0 == 3 && x + 0.5 < lineStartX) sx = clamp(int(floor(2.0 * lineStartX - x - 0.5)), 0, w - 1);
    if ((i0 == 4 || i0 >= 7) && x + 0.5 > lineEndX) sx = clamp(int(floor(2.0 * lineEndX - x - 0.5)), 0, w - 1);
    if (i0 == 5 && y + 0.5 < lineStartY) sy = clamp(int(floor(2.0 * lineStartY - y - 0.5)), 0, hh - 1);
    if ((i0 == 6 || i0 >= 7) && y + 0.5 > lineEndY) sy = clamp(int(floor(2.0 * lineEndY - y - 0.5)), 0, hh - 1);
    const float4 q = float4(texel(sx, sy));
    o = floor(clamp(float4(p) + (q - float4(p)) * amt + 0.5, 0.0, 255.0));
  } else if (kind == K_LUMA_KEY) {
    const uint l = (54 * p.r + 183 * p.g + 19 * p.b) >> 8;
    o.a = float((p.a * lutByte(l) + 127) / 255);
  } else if (kind == K_EDGES) {
    const int w = int(width), hh = int(height);
    const int l = max(0, x - 1), r = min(w - 1, x + 1);
    const int u = max(0, y - 1), d = min(hh - 1, y + 1);
    #define LUMA(X, Y) int((54 * texel(X, Y).r + 183 * texel(X, Y).g + 19 * texel(X, Y).b) >> 8)
    const int gx = (LUMA(r, u) + 2 * LUMA(r, y) + LUMA(r, d)) - (LUMA(l, u) + 2 * LUMA(l, y) + LUMA(l, d));
    const int gy = (LUMA(l, d) + 2 * LUMA(x, d) + LUMA(r, d)) - (LUMA(l, u) + 2 * LUMA(x, u) + LUMA(r, u));
    #undef LUMA
    const float e = min(1.0, sqrt(float(gx * gx + gy * gy)) * f0 / 255.0);
    const float brightest = float(max(max(p.r, p.g), max(p.b, 1u)));
    const float3 line0 = float3(f1, f2, f3);
    [unroll] for (uint k = 0; k < 3; ++k) {
      const float own = p[k] * 255.0 / brightest;
      const float ln = line0[k] + (own - line0[k]) * pC;
      const float behind = p[k] * pB;
      const float outv = behind + (ln - behind) * e;
      o[k] = floor(clamp(p[k] + (outv - p[k]) * amt + 0.5, 0.0, 255.0));
    }
  } else if (kind == K_WARP) {
    // i0: 0 polar, 1 luma displace, 2 relativistic, 3 ripple, 4 kaleidoscope.
    // f0 cx, f1 cy, f2 maxR, f3 t (frame * 0.08), f4 pixelScale.
    float sxf = x, syf = y;
    if (i0 == 0) {
      const float nx = (x - f0) / max(1.0, f0);
      const float ny = (y - f1) / max(1.0, f1);
      const float r = sqrt(nx * nx + ny * ny);
      float a = atan2(ny, nx);
      if (a < 0.0) a += 6.283185307179586;
      a += (pA - 0.5) * 2.0 * r * 6.283185307179586;
      const float u = (a / 6.283185307179586) * width;
      const float v = r * height * (1.0 + pB * 2.0);
      sxf = x * (1.0 - amt) + u * amt;
      syf = y * (1.0 - amt) + v * amt;
    } else if (i0 == 1) {
      const float luma = (p.r * 0.299 + p.g * 0.587 + p.b * 0.114) / 255.0;
      const float push = (luma - (0.5 + pB * 0.5)) * amt * width * 0.25;
      sxf = x + push;
      syf = y + push * (0.35 + (pA - 0.5) * 2.0 * 0.65);
    } else if (i0 == 2) {
      const float ax = x - (f0 + f5), ay = y - f1;
      const float ar = sqrt(ax * ax + ay * ay);
      if (ar >= 0.5 && f2 >= 1.0) {
        const float slot = clamp(ar / f2, 0.0, 1.0) * 1023.0;
        const int s0 = int(slot);
        const int s1 = min(1023, s0 + 1);
        const float fr = slot - s0;
        const float srcR = boostRadius(s0) * (1.0 - fr) + boostRadius(s1) * fr;
        sxf = f0 + ax / ar * srcR;
        syf = f1 + ay / ar * srcR;
      }
    } else if (i0 == 3) {
      const float dx = x - f0, dy = y - f1;
      const float r = sqrt(dx * dx + dy * dy);
      const float wave = sin(r / f4 * (0.06 * pA * 2.0) - f3 * (3.0 + pB * 9.0)) * amt * 24.0 * f4;
      const float inv = r > 0.001 ? 1.0 / r : 0.0;
      sxf = x + dx * inv * wave;
      syf = y + dy * inv * wave;
    } else {
      const float nx = x - f0, ny = y - f1;
      const float r = sqrt(nx * nx + ny * ny);
      const float seg = 6.283185307179586 / i1;
      float a = atan2(ny, nx) + pB * 6.283185307179586;
      a = abs(fmod(a + seg * 0.5, seg) - seg * 0.5);   // a + seg/2 >= -pi + seg/2: C fmod's sign
      const float fx2 = f0 + cos(a) * r;
      const float fy2 = f1 + sin(a) * r;
      sxf = x * (1.0 - amt) + fx2 * amt;
      syf = y * (1.0 - amt) + fy2 * amt;
    }
    // std::lround: halves away from zero.
    const int sx = clamp(lroundf(sxf), 0, int(width) - 1);
    const int sy = clamp(lroundf(syf), 0, int(height) - 1);
    const uint4 s = texel(sx, sy);
    o.rgb = float3(s.rgb);
    if (i0 == 2 && pB > 0.0005) {
      const float bx = x - f0, by = y - f1;
      const float br = clamp(sqrt(bx * bx + by * by) / max(1.0, f2), 0.0, 1.0);
      const float shift = boostGain(int(br * 1023.0));
      const float gain = 1.0 + shift * 0.6;
      o.r = floor(clamp(s.r * gain * (1.0 - shift * 0.55), 0.0, 255.0));
      o.g = floor(clamp(s.g * gain, 0.0, 255.0));
      o.b = floor(clamp(s.b * gain * (1.0 + shift * 0.55), 0.0, 255.0));
    }
  } else if (kind == K_MOSAIC) {
    // i0 tile; aux holds each tile's average. f0 half, f1 corner, i1 rounded.
    const int ty = y / i0, tx = x / i0;
    const uint4 a = auxTexel(tx, ty);
    float c = 1.0;
    if (i1 != 0) {
      const float u = x - tx * i0, v = y - ty * i0;
      const float qx = abs(u + 0.5 - i0 * 0.5) - (f0 - f1);
      const float qy = abs(v + 0.5 - i0 * 0.5) - (f0 - f1);
      const float outside = sqrt(max(qx, 0.0) * max(qx, 0.0) + max(qy, 0.0) * max(qy, 0.0));
      const float sd = outside + min(max(qx, qy), 0.0) - f1;
      c = clamp(0.5 - sd, 0.0, 1.0);
    }
    o.rgb = floor(clamp(float3(p.rgb) + (float3(a.rgb) * c - float3(p.rgb)) * amt + 0.5, 0.0, 255.0));
    o.a = floor(clamp(p.a + (float(a.a) - p.a) * amt + 0.5, 0.0, 255.0));
  } else if (kind == K_MIX) {
    // aux, read back up bilinearly (detail::upsampleRgb), laid over the picture.
    // i0: 0 blur (mix by amt), 1 glow (screen, strength f0), 2 sharpen (lift:
    // gain f0, threshold f1; aux is the softened copy at full size).
    float3 rgb;
    if (auxW == width && auxH == height) {
      rgb = float3(auxTexel(x, y).rgb);
    } else {
      const float fx = clamp((x + 0.5) * (float(auxW) / width) - 0.5, 0.0, auxW - 1.0);
      const float fy = clamp((y + 0.5) * (float(auxH) / height) - 0.5, 0.0, auxH - 1.0);
      const int x0 = int(fx), y0 = int(fy);
      const int x1 = min(int(auxW) - 1, x0 + 1), y1 = min(int(auxH) - 1, y0 + 1);
      const float tx = fx - floor(fx), ty = fy - y0;
      const float3 a = float3(auxTexel(x0, y0).rgb), b = float3(auxTexel(x1, y0).rgb);
      const float3 c = float3(auxTexel(x0, y1).rgb), d = float3(auxTexel(x1, y1).rgb);
      const float3 top = a + (b - a) * tx;
      const float3 bottom = c + (d - c) * tx;
      rgb = top + (bottom - top) * ty;
    }
    const float3 pv = float3(p.rgb);
    if (i0 == 0) {
      o.rgb = floor(clamp(pv + (rgb - pv) * amt + 0.5, 0.0, 255.0));
    } else if (i0 == 1) {
      const float3 light = min(255.0, rgb * f0);
      o.rgb = floor(clamp(255.0 - (255.0 - pv) * (255.0 - light) / 255.0 + 0.5, 0.0, 255.0));
    } else {
      const float3 diff = pv - rgb;
      const float3 over = abs(diff) - f1;
      const float3 lift = float3(over > 0.0) * sign(diff) * over * f0;
      o.rgb = floor(clamp(pv + lift + 0.5, 0.0, 255.0));
    }
  }
  put(id.xy, o);
}

// ---- a box-averaged copy 1/i0 the size (detail::downsampleRgba) -------------
// i1 = 1: then each pixel kept by its brightness through tab[0..255] (glow's knee).
[numthreads(8, 8, 1)]
void downMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const int x0 = int(id.x) * i0, y0 = int(id.y) * i0;
  const int x1 = min(int(srcW), x0 + i0), y1 = min(int(srcH), y0 + i0);
  uint4 sum = uint4(0, 0, 0, 0);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) sum += texel(x, y);
  }
  const uint n = uint((y1 - y0) * (x1 - x0));
  float4 o = float4(sum / n);
  if (i1 == 1) {
    const uint luma = (54 * uint(o.r) + 183 * uint(o.g) + 19 * uint(o.b)) >> 8;
    o.rgb = floor(o.rgb * tabAt(luma));
  }
  put(id.xy, o);
}

// ---- one box pass along one axis (detail::boxBlurRgb): i0 radius, i1 axis --
[numthreads(8, 8, 1)]
void boxMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const int x = int(id.x), y = int(id.y);
  uint3 sum = uint3(0, 0, 0);
  for (int k = -i0; k <= i0; ++k) {
    const int sx = i1 == 0 ? clamp(x + k, 0, int(width) - 1) : x;
    const int sy = i1 == 0 ? y : clamp(y + k, 0, int(height) - 1);
    sum += texel(sx, sy).rgb;
  }
  const uint span = uint(2 * i0 + 1);
  float4 o = float4(texel(x, y));
  o.rgb = float3((sum * 2 + span) / (span * 2));
  put(id.xy, o);
}

// ---- the blur's streak along an angle (f0 stepX, f1 stepY, i0 taps) -------
[numthreads(8, 8, 1)]
void streakMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const int x = int(id.x), y = int(id.y);
  float3 acc = float3(0, 0, 0);
  for (int t = 0; t < i0; ++t) {
    const float u = -1.0 + 2.0 * (t + 0.5) / i0;
    const float atX = clamp(x + f0 * u, 0.0, width - 1.0);
    const float atY = clamp(y + f1 * u, 0.0, height - 1.0);
    const int x0 = int(atX), y0 = int(atY);
    const int x1 = min(int(width) - 1, x0 + 1), y1 = min(int(height) - 1, y0 + 1);
    const float tx = atX - x0, ty = atY - y0;
    const float3 a = float3(texel(x0, y0).rgb), b = float3(texel(x1, y0).rgb);
    const float3 c = float3(texel(x0, y1).rgb), d = float3(texel(x1, y1).rgb);
    const float3 top = a + (b - a) * tx;
    const float3 bottom = c + (d - c) * tx;
    acc += top + (bottom - top) * ty;
  }
  float4 o = float4(texel(x, y));
  o.rgb = floor(clamp(acc / i0 + 0.5, 0.0, 255.0));
  put(id.xy, o);
}

// ---- mosaic: each tile's average (i0 tile) --------------------------------
[numthreads(8, 8, 1)]
void tileMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const int x0 = int(id.x) * i0, y0 = int(id.y) * i0;
  const int x1 = min(int(srcW), x0 + i0), y1 = min(int(srcH), y0 + i0);
  uint4 sum = uint4(0, 0, 0, 0);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) sum += texel(x, y);
  }
  put(id.xy, float4(sum / uint((y1 - y0) * (x1 - x0))));
}

// ---- the decoder's NV12 picture, as RGB, reduced by i0 ----------------------
// src: the luma plane (R8), aux: the chroma plane (R8G8), both straight off
// the decode. f0/f1: luma and chroma offsets; f2: luma scale; f3 Cr->R,
// f4 Cb->G, f5 Cr->G, f6 Cb->B (the frame's own matrix and range, from
// gpu_effects.cpp). Each output pixel averages its i0 x i0 block, each sample
// with its own chroma, as the CPU path's conversion then reduction does.
[numthreads(8, 8, 1)]
void nv12Main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  float3 acc = float3(0, 0, 0);
  for (int j = 0; j < i0; ++j) {
    for (int i = 0; i < i0; ++i) {
      const int x = min(int(id.x) * i0 + i, int(srcW) - 1);
      const int y = min(int(id.y) * i0 + j, int(srcH) - 1);
      const float luma = src.Load(int3(x, y, 0)).r;
      const float2 chroma = aux.Load(int3(x / 2, y / 2, 0)).rg + f1;
      const float yv = (luma + f0) * f2;
      acc += saturate(float3(yv + f3 * chroma.y, yv - f4 * chroma.x - f5 * chroma.y, yv + f6 * chroma.x));
    }
  }
  put(id.xy, float4(floor(acc / float(i0 * i0) * 255.0 + 0.5), 255.0));
}

// ---- portal, shatter, clouds and ghastly: the outgoing layer ----------------
// engine/media_engine.cpp's transition builders, per pixel. kind: 1 portal,
// 2 shatter, 3 clouds, 4 ghastly. aux: each frame's small inputs
// (MediaEngine::planGpuTransition) as one flat array of floats, four to a
// texel and auxW texels to a row. The output is the layer with its alpha,
// drawn over the incoming cue.
float auxAt(int i) {
  const int t = i >> 2;
  const float4 v = aux.Load(int3(t % int(auxW), t / int(auxW), 0));
  const int c = i & 3;
  return c == 0 ? v.x : (c == 1 ? v.y : (c == 2 ? v.z : v.w));
}
// Shatter's roundHalfAway: std::lround, half away from zero.
int roundHalfAway(float v) {
  const float whole = trunc(v);
  const float frac = v - whole;
  return int(whole) + (frac >= 0.5 ? 1 : (frac <= -0.5 ? -1 : 0));
}

[numthreads(8, 8, 1)]
void transMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const int x = int(id.x), y = int(id.y);
  const int W = int(width), H = int(height);
  float4 o = float4(0, 0, 0, 0);
  if (kind == 1) {
    // PORTAL. i0 the grid step, i1/i2 its size; aux: the field, its hue, the
    // edge's profile (2048 x core, hot, light, space), the wheel (256 x RGB).
    // f0/f1: where the light has gone outside and inside; f2 the profile's scale.
    const int step = i0, gw = i1, gh = i2;
    const float invStep = 1.0 / float(step);
    const float gyf = float(y) * invStep;
    const int gy = min(gh - 2, int(gyf));
    const float fy = gyf - float(gy);
    const int cgx = min(gw - 2, x / step);
    const float fx = float(x) * invStep - float(cgx);
    const int a = gy * gw + cgx;
    const float d0 = auxAt(a) + (auxAt(a + 1) - auxAt(a)) * fx;
    const float d1 = auxAt(a + gw) + (auxAt(a + gw + 1) - auxAt(a + gw)) * fx;
    const float d = d0 + (d1 - d0) * fy;
    if (d > f0) {
      o = float4(texel(x, y));   // untouched old picture
    } else if (d >= -f1) {
      const int hb = gw * gh;
      const float h0 = auxAt(hb + a) + (auxAt(hb + a + 1) - auxAt(hb + a)) * fx;
      const float h1 = auxAt(hb + a + gw) + (auxAt(hb + a + gw + 1) - auxAt(hb + a + gw)) * fx;
      float hu = h0 + (h1 - h0) * fy;
      hu -= floor(hu);
      const int pb = hb * 2;
      const int wb = pb + 2048 * 4 + min(255, int(hu * 256.0)) * 3;
      const float3 wc = float3(auxAt(wb), auxAt(wb + 1), auxAt(wb + 2));
      const int pi = pb + clamp(int((d + f1) * f2 + 0.5), 0, 2047) * 4;
      const float core = auxAt(pi), hot = auxAt(pi + 1), light = auxAt(pi + 2), space = auxAt(pi + 3);
      const float3 rim = wc + (255.0 - wc) * hot;
      if (d >= 0.0) {
        const float3 s = float3(texel(x, y).rgb);
        o = float4(floor(min(255.0, s + (rim - s) * core + rim * light * 0.6)), 255.0);
      } else {
        const float alpha = light + space * (1.0 - light);
        if (alpha > 0.002) {
          const float under = space * (1.0 - light);
          const float inv = 1.0 / alpha;
          o = float4(floor(min(255.0, (rim * light + float3(12.0, 10.0, 58.0) * under) * inv)),
                     floor(clamp(alpha * 255.0, 0.0, 255.0)));
        }
      }
    }
  } else if (kind == 2) {
    // SHATTER. i0 the shard size, i1 shards across; aux: eight floats a shard
    // (gone, offset x/y, cos, sin, centre x/y). f0: the glint's falloff.
    const int shard = i0;
    const int gx = x / shard, gy = y / shard;
    const int b = (gy * i1 + gx) * 8;
    const float gone = auxAt(b);
    if (gone <= 0.0) {
      o = float4(float3(texel(x, y).rgb), 255.0);
    } else if (gone < 1.0) {
      const float ox = auxAt(b + 1), oy = auxAt(b + 2), ca = auxAt(b + 3), sa = auxAt(b + 4);
      const float ccx = auxAt(b + 5), ccy = auxAt(b + 6);
      const float rx = float(x) - ccx, ry = float(y) - ccy;
      const int sx = roundHalfAway(ccx + rx * ca - ry * sa - ox);
      const int sy = roundHalfAway(ccy + rx * sa + ry * ca - oy);
      if (sx >= 0 && sx < W && sy >= 0 && sy < H) {
        const float3 s = float3(texel(sx, sy).rgb);
        const int edgeY = min(y - gy * shard, (gy + 1) * shard - 1 - y);
        const int edge = min(edgeY, min(x - gx * shard, (gx + 1) * shard - 1 - x));
        const float glint = clamp(1.0 - float(edge) / f0, 0.0, 1.0) * (0.55 * gone);
        o = float4(floor(min(255.0, s + (255.0 - s) * glint)), floor(clamp((1.0 - gone) * 255.0, 0.0, 255.0)));
      }
    }
  } else if (kind == 3) {
    // CLOUDS. i0 the lattice step, i1 its width; aux: the field. f0 the cut,
    // f1 the band.
    const int step = i0, gw = i1;
    const float invStep = 1.0 / float(step);
    const float fy = float(y) * invStep;
    const int gy = int(fy);
    const float wy = fy - float(gy);
    const float fx = float(x) * invStep;
    const int gx = int(fx);
    const float wx = fx - float(gx);
    const int at = gy * gw + gx;
    const float c0 = auxAt(at), c1 = auxAt(at + 1), c2 = auxAt(at + gw), c3 = auxAt(at + gw + 1);
    const float top = c0 + (c1 - c0) * wx;
    const float bot = c2 + (c3 - c2) * wx;
    const float density = top + (bot - top) * wy;
    const float open = clamp((f0 - density) / f1 + 0.5, 0.0, 1.0);
    const float keep = 1.0 - open;
    if (keep > 0.0) {
      const float3 s = float3(texel(x, y).rgb);
      const float fringe = 4.0 * open * (1.0 - open) * 0.55;
      o = float4(floor(min(255.0, s + (255.0 - s) * fringe)), floor(clamp(keep * 255.0, 0.0, 255.0)));
    }
  } else if (kind == 4) {
    // GHASTLY. aux: the wisp line per column. f0 the lift, f1 the drain,
    // f2 the thinning, f3 the wisp's soft edge.
    const float wisp = auxAt(x);
    const float keep = clamp((wisp - float(y)) / f3, 0.0, 1.0);
    const int sy = lroundf(float(y) + f0);
    if (keep > 0.0 && sy < H) {
      const float3 s = float3(texel(x, clamp(sy, 0, H - 1)).rgb);
      const float luma = 0.299 * s.r + 0.587 * s.g + 0.114 * s.b;
      const float3 cold = float3(luma * 0.82, luma * 0.90, min(255.0, luma * 1.12 + 10.0));
      float3 c = s + (cold - s) * f1;
      const float bite = 4.0 * keep * (1.0 - keep) * 0.5;
      c += (float3(235.0, 240.0, 255.0) - c) * bite;
      o = float4(floor(clamp(c, 0.0, 255.0)), floor(clamp(keep * f2 * 255.0, 0.0, 255.0)));
    }
  }
  put(id.xy, o);
}
