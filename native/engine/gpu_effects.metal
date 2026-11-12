// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// gpu_effects.metal -- gpu_effects.hlsl for SDL_GPU's Metal, line for line.
// The comments there explain it; change both together, then run
// tools/build_effects_shaders.py, which embeds this source as it is.
//
// SDL_GPU's Metal order: [[texture]] the sampled textures (picture, aux), then
// the read-write output; [[sampler]] one per sampled texture; [[buffer]] the
// uniforms.
#include <metal_stdlib>
using namespace metal;

struct Params {
  uint kind; uint width; uint height; uint frame;
  uint srcW; uint srcH; uint auxW; uint auxH;
  int i0; int i1; int i2; int i3;
  float amt; float pA; float pB; float pC;
  float pD; float f0; float f1; float f2;
  float f3; float f4; float f5; float f6;
  float f7; float f8; float f9; float f10;
  float f11; float f12; float f13; float f14;
  uint4 lut[48];
  float4 tab[512];
};

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

#define SRC texture2d<float, access::sample>
#define DST texture2d<float, access::write>
#define ARGS SRC src [[texture(0)]], sampler srcSampler [[sampler(0)]], \
             SRC aux [[texture(1)]], sampler auxSampler [[sampler(1)]], \
             DST dst [[texture(2)]], constant Params& p [[buffer(0)]], \
             uint2 id [[thread_position_in_grid]]

static uint lutByte(constant Params& p, uint i) {
  const uint word = p.lut[i >> 4][(i >> 2) & 3];
  return (word >> ((i & 3) * 8)) & 255;
}
static float tabAt(constant Params& p, uint i) { return p.tab[i >> 2][i & 3]; }
static uint4 texel(SRC t, int x, int y) { return uint4(t.read(uint2(x, y)) * 255.0f + 0.5f); }
static float c255(float v) { return clamp(v, 0.0f, 255.0f); }
static float3 c255(float3 v) { return clamp(v, float3(0.0f), float3(255.0f)); }
static float4 c255(float4 v) { return clamp(v, float4(0.0f), float4(255.0f)); }
static void put(DST dst, uint2 at, float4 bytes) { dst.write(bytes / 255.0f, at); }
static float smoothstep01(float e0, float e1, float x) {
  const float t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
static uint xorshift(uint h) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return h; }
static int lroundf(float v) { return int(sign(v) * floor(fabs(v) + 0.5f)); }
static int luma8(SRC t, int x, int y) {
  const uint4 q = texel(t, x, y);
  return int((54 * q.r + 183 * q.g + 19 * q.b) >> 8);
}

kernel void pointMain(ARGS) {
  if (id.x >= p.width || id.y >= p.height) return;
  const int x = int(id.x), y = int(id.y);
  const uint4 px = texel(src, x, y);
  float4 o = float4(px);
  const float amt = p.amt, pA = p.pA, pB = p.pB, pC = p.pC;
  const int W = int(p.width), H = int(p.height);
  if (p.kind == K_LUT) {
    if (p.i0 <= 0 || ((y / (p.i0 / 2)) & 1) != 0) {
      o.r = lutByte(p, px.r); o.g = lutByte(p, 256 + px.g); o.b = lutByte(p, 512 + px.b);
    }
  } else if (p.kind == K_THRESHOLD) {
    const float luma = px.r * 0.299f + px.g * 0.587f + px.b * 0.114f;
    const float3 v = float3(px.rgb);
    const float3 lit = floor(c255(v * (1.0f - amt) + 255.0f * amt));
    const float3 dark = floor(c255(v * (1.0f - amt)));
    if (p.f1 <= 0.5f) {
      if (luma >= p.f0) o.rgb = lit; else o.rgb = dark;
    } else {
      const float t = clamp((luma - (p.f0 - p.f1)) / (2.0f * p.f1), 0.0f, 1.0f);
      const float eased = t * t * (3.0f - 2.0f * t);
      o.rgb = floor(c255(dark * (1.0f - eased) + lit * eased));
    }
  } else if (p.kind == K_COLOUR) {
    const float3 c0 = float3(px.rgb) / 255.0f;
    float3 c = float3(p.f0 * c0.r + p.f1 * c0.g + p.f2 * c0.b,
                      p.f3 * c0.r + p.f4 * c0.g + p.f5 * c0.b,
                      p.f6 * c0.r + p.f7 * c0.g + p.f8 * c0.b);
    const float luma = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    c = luma + (c - luma) * p.f12;
    if (p.f13 > 0.0f) {
      const float reach = clamp(min(luma, 1.0f - luma) * 2.0f, 0.0f, 1.0f);
      c += (luma + float3(p.f9, p.f10, p.f11) * reach - c) * p.f13;
    }
    o.rgb = floor(c255((c0 + (c - c0) * amt) * 255.0f + 0.5f));
  } else if (p.kind == K_COLOUR_REPLACE) {
    const int r = int(px.r), g = int(px.g), b = int(px.b);
    const int mx = max(r, max(g, b));
    const int mn = min(r, min(g, b));
    const int c = mx - mn;
    if (c >= 6) {
      const float inv = 1.0f / float(c);
      float h = mx == r ? float(g - b) * inv : mx == g ? 2.0f + float(b - r) * inv : 4.0f + float(r - g) * inv;
      h *= 1.0f / 6.0f;
      if (h < 0.0f) h += 1.0f;
      float d = fabs(h - p.f0);
      if (d > 0.5f) d = 1.0f - d;
      if (d < p.f2 + p.f3) {
        float weight = smoothstep01(6.0f, 40.0f, float(c)) * amt;
        if (d > p.f2) {
          const float t = (d - p.f2) / p.f3;
          weight *= 1.0f - t * t * (3.0f - 2.0f * t);
        }
        float turned = h + p.f1;
        turned -= floor(turned);
        const float sector = turned * 6.0f;
        const int i = min(5, int(sector));
        const float rise = float(mn) + float(c) * (sector - float(i));
        const float fall = float(mx) + float(mn) - rise;
        const float hi = float(mx), lo = float(mn);
        const float nr = i == 0 || i == 5 ? hi : i == 1 ? fall : i == 4 ? rise : lo;
        const float ng = i == 1 || i == 2 ? hi : i == 0 ? rise : i == 3 ? fall : lo;
        const float nb = i == 3 || i == 4 ? hi : i == 2 ? rise : i == 5 ? fall : lo;
        o.r = floor(float(r) + (nr - float(r)) * weight + 0.5f);
        o.g = floor(float(g) + (ng - float(g)) * weight + 0.5f);
        o.b = floor(float(b) + (nb - float(b)) * weight + 0.5f);
      }
    }
  } else if (p.kind == K_VIGNETTE) {
    const float dx = x - p.f0, dy = y - p.f1;
    const float r = sqrt(dx * dx + dy * dy) / p.f2 / p.f3;
    const float shaped = p.f4 == 2.0f ? r * r : pow(clamp(r, 0.0f, 4.0f), p.f4);
    const float gain = 1.0f - amt * clamp(shaped, 0.0f, 1.0f);
    o.rgb = floor(c255(float3(px.rgb) * gain));
  } else if (p.kind == K_GRAIN) {
    const uint cell = p.i1 == 1 ? uint(y) * p.width + uint(x) : uint(y / p.i1) * p.width + uint(x / p.i1);
    uint h = cell ^ uint(p.i0);
    h = xorshift(h);
    const float n = (float(h & 0xFFFF) / 32768.0f - 1.0f) * p.f0;
    if (pB <= 0.0005f) {
      o.rgb = floor(c255(float3(px.rgb) + n));
    } else {
      for (uint c = 0; c < 3; ++c) {
        const uint hc = xorshift(h ^ (c * 0x9E3779B9u));
        const float nc = (float(hc & 0xFFFF) / 32768.0f - 1.0f) * p.f0;
        o[c] = floor(c255(float(px[c]) + n * (1.0f - pB) + nc * pB));
      }
    }
  } else if (p.kind == K_CHANNEL_OFFSET) {
    if (p.i2 != 0) {
      o.b = texel(src, clamp(x + p.i0, 0, W - 1), y).b;
      o.r = texel(src, clamp(x - p.i0, 0, W - 1), y).r;
    } else {
      const int ox = lroundf(p.f0), oy = lroundf(p.f1);
      o.b = texel(src, clamp(x + ox, 0, W - 1), clamp(y + oy, 0, H - 1)).b;
      o.r = texel(src, clamp(x - ox, 0, W - 1), clamp(y - oy, 0, H - 1)).r;
      if (p.i1 != 0) {
        o.g = texel(src, clamp(x + lroundf(p.f1 * pB), 0, W - 1),
                    clamp(y - lroundf(p.f0 * pB), 0, H - 1)).g;
      }
    }
  } else if (p.kind == K_MIRROR) {
    int sx = x, sy = y;
    const float lineEndX = W * p.f0, lineEndY = H * p.f0;
    const float lineStartX = W * (1.0f - p.f0), lineStartY = H * (1.0f - p.f0);
    if (p.i0 == 0 || p.i0 == 2) sx = W - 1 - x;
    if (p.i0 == 1 || p.i0 == 2) sy = H - 1 - y;
    if (p.i0 == 3 && x + 0.5f < lineStartX) sx = clamp(int(floor(2.0f * lineStartX - x - 0.5f)), 0, W - 1);
    if ((p.i0 == 4 || p.i0 >= 7) && x + 0.5f > lineEndX) sx = clamp(int(floor(2.0f * lineEndX - x - 0.5f)), 0, W - 1);
    if (p.i0 == 5 && y + 0.5f < lineStartY) sy = clamp(int(floor(2.0f * lineStartY - y - 0.5f)), 0, H - 1);
    if ((p.i0 == 6 || p.i0 >= 7) && y + 0.5f > lineEndY) sy = clamp(int(floor(2.0f * lineEndY - y - 0.5f)), 0, H - 1);
    const float4 q = float4(texel(src, sx, sy));
    o = floor(c255(float4(px) + (q - float4(px)) * amt + 0.5f));
  } else if (p.kind == K_LUMA_KEY) {
    const uint l = (54 * px.r + 183 * px.g + 19 * px.b) >> 8;
    o.a = float((px.a * lutByte(p, l) + 127) / 255);
  } else if (p.kind == K_EDGES) {
    const int l = max(0, x - 1), r = min(W - 1, x + 1);
    const int u = max(0, y - 1), d = min(H - 1, y + 1);
    const int gx = (luma8(src, r, u) + 2 * luma8(src, r, y) + luma8(src, r, d)) -
                   (luma8(src, l, u) + 2 * luma8(src, l, y) + luma8(src, l, d));
    const int gy = (luma8(src, l, d) + 2 * luma8(src, x, d) + luma8(src, r, d)) -
                   (luma8(src, l, u) + 2 * luma8(src, x, u) + luma8(src, r, u));
    const float e = min(1.0f, sqrt(float(gx * gx + gy * gy)) * p.f0 / 255.0f);
    const float brightest = float(max(max(px.r, px.g), max(px.b, 1u)));
    const float3 line0 = float3(p.f1, p.f2, p.f3);
    for (uint k = 0; k < 3; ++k) {
      const float own = px[k] * 255.0f / brightest;
      const float ln = line0[k] + (own - line0[k]) * pC;
      const float behind = px[k] * pB;
      const float outv = behind + (ln - behind) * e;
      o[k] = floor(c255(px[k] + (outv - px[k]) * amt + 0.5f));
    }
  } else if (p.kind == K_WARP) {
    float sxf = x, syf = y;
    if (p.i0 == 0) {
      const float nx = (x - p.f0) / max(1.0f, p.f0);
      const float ny = (y - p.f1) / max(1.0f, p.f1);
      const float r = sqrt(nx * nx + ny * ny);
      float a = atan2(ny, nx);
      if (a < 0.0f) a += 6.283185307179586f;
      a += (pA - 0.5f) * 2.0f * r * 6.283185307179586f;
      const float u = (a / 6.283185307179586f) * p.width;
      const float v = r * p.height * (1.0f + pB * 2.0f);
      sxf = x * (1.0f - amt) + u * amt;
      syf = y * (1.0f - amt) + v * amt;
    } else if (p.i0 == 1) {
      const float luma = (px.r * 0.299f + px.g * 0.587f + px.b * 0.114f) / 255.0f;
      const float push = (luma - (0.5f + pB * 0.5f)) * amt * p.width * 0.25f;
      sxf = x + push;
      syf = y + push * (0.35f + (pA - 0.5f) * 2.0f * 0.65f);
    } else if (p.i0 == 2) {
      const float ax = x - (p.f0 + p.f5), ay = y - p.f1;
      const float ar = sqrt(ax * ax + ay * ay);
      if (ar >= 0.5f && p.f2 >= 1.0f) {
        const float slot = clamp(ar / p.f2, 0.0f, 1.0f) * 1023.0f;
        const int s0 = int(slot);
        const int s1 = min(1023, s0 + 1);
        const float fr = slot - s0;
        const float srcR = tabAt(p, s0) * (1.0f - fr) + tabAt(p, s1) * fr;
        sxf = p.f0 + ax / ar * srcR;
        syf = p.f1 + ay / ar * srcR;
      }
    } else if (p.i0 == 3) {
      const float dx = x - p.f0, dy = y - p.f1;
      const float r = sqrt(dx * dx + dy * dy);
      const float wave = sin(r / p.f4 * (0.06f * pA * 2.0f) - p.f3 * (3.0f + pB * 9.0f)) * amt * 24.0f * p.f4;
      const float inv = r > 0.001f ? 1.0f / r : 0.0f;
      sxf = x + dx * inv * wave;
      syf = y + dy * inv * wave;
    } else {
      const float nx = x - p.f0, ny = y - p.f1;
      const float r = sqrt(nx * nx + ny * ny);
      const float seg = 6.283185307179586f / p.i1;
      float a = atan2(ny, nx) + pB * 6.283185307179586f;
      a = fabs(fmod(a + seg * 0.5f, seg) - seg * 0.5f);
      const float fx2 = p.f0 + cos(a) * r;
      const float fy2 = p.f1 + sin(a) * r;
      sxf = x * (1.0f - amt) + fx2 * amt;
      syf = y * (1.0f - amt) + fy2 * amt;
    }
    const int sx = clamp(lroundf(sxf), 0, W - 1);
    const int sy = clamp(lroundf(syf), 0, H - 1);
    const uint4 s = texel(src, sx, sy);
    o.rgb = float3(s.rgb);
    if (p.i0 == 2 && pB > 0.0005f) {
      const float bx = x - p.f0, by = y - p.f1;
      const float br = clamp(sqrt(bx * bx + by * by) / max(1.0f, p.f2), 0.0f, 1.0f);
      const float shift = tabAt(p, 1024 + int(br * 1023.0f));
      const float gain = 1.0f + shift * 0.6f;
      o.r = floor(c255(s.r * gain * (1.0f - shift * 0.55f)));
      o.g = floor(c255(s.g * gain));
      o.b = floor(c255(s.b * gain * (1.0f + shift * 0.55f)));
    }
  } else if (p.kind == K_MOSAIC) {
    const int ty = y / p.i0, tx = x / p.i0;
    const uint4 a = texel(aux, tx, ty);
    float c = 1.0f;
    if (p.i1 != 0) {
      const float u = x - tx * p.i0, v = y - ty * p.i0;
      const float qx = fabs(u + 0.5f - p.i0 * 0.5f) - (p.f0 - p.f1);
      const float qy = fabs(v + 0.5f - p.i0 * 0.5f) - (p.f0 - p.f1);
      const float outside = sqrt(max(qx, 0.0f) * max(qx, 0.0f) + max(qy, 0.0f) * max(qy, 0.0f));
      const float sd = outside + min(max(qx, qy), 0.0f) - p.f1;
      c = clamp(0.5f - sd, 0.0f, 1.0f);
    }
    o.rgb = floor(c255(float3(px.rgb) + (float3(a.rgb) * c - float3(px.rgb)) * amt + 0.5f));
    o.a = floor(c255(px.a + (float(a.a) - px.a) * amt + 0.5f));
  } else if (p.kind == K_MIX) {
    float3 rgb;
    if (p.auxW == p.width && p.auxH == p.height) {
      rgb = float3(texel(aux, x, y).rgb);
    } else {
      const float fx = clamp((x + 0.5f) * (float(p.auxW) / p.width) - 0.5f, 0.0f, p.auxW - 1.0f);
      const float fy = clamp((y + 0.5f) * (float(p.auxH) / p.height) - 0.5f, 0.0f, p.auxH - 1.0f);
      const int x0 = int(fx), y0 = int(fy);
      const int x1 = min(int(p.auxW) - 1, x0 + 1), y1 = min(int(p.auxH) - 1, y0 + 1);
      const float tx = fx - floor(fx), ty = fy - y0;
      const float3 a = float3(texel(aux, x0, y0).rgb), b = float3(texel(aux, x1, y0).rgb);
      const float3 c = float3(texel(aux, x0, y1).rgb), d = float3(texel(aux, x1, y1).rgb);
      const float3 top = a + (b - a) * tx;
      const float3 bottom = c + (d - c) * tx;
      rgb = top + (bottom - top) * ty;
    }
    const float3 pv = float3(px.rgb);
    if (p.i0 == 0) {
      o.rgb = floor(c255(pv + (rgb - pv) * amt + 0.5f));
    } else if (p.i0 == 1) {
      const float3 light = min(float3(255.0f), rgb * p.f0);
      o.rgb = floor(c255(255.0f - (255.0f - pv) * (255.0f - light) / 255.0f + 0.5f));
    } else {
      const float3 diff = pv - rgb;
      const float3 over = fabs(diff) - p.f1;
      const float3 lift = float3(over > 0.0f) * sign(diff) * over * p.f0;
      o.rgb = floor(c255(pv + lift + 0.5f));
    }
  }
  put(dst, id, o);
}

kernel void downMain(ARGS) {
  if (id.x >= p.width || id.y >= p.height) return;
  const int x0 = int(id.x) * p.i0, y0 = int(id.y) * p.i0;
  const int x1 = min(int(p.srcW), x0 + p.i0), y1 = min(int(p.srcH), y0 + p.i0);
  uint4 sum = uint4(0, 0, 0, 0);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) sum += texel(src, x, y);
  }
  const uint n = uint((y1 - y0) * (x1 - x0));
  float4 o = float4(sum / n);
  if (p.i1 == 1) {
    const uint luma = (54 * uint(o.r) + 183 * uint(o.g) + 19 * uint(o.b)) >> 8;
    o.rgb = floor(o.rgb * tabAt(p, luma));
  }
  put(dst, id, o);
}

kernel void boxMain(ARGS) {
  if (id.x >= p.width || id.y >= p.height) return;
  const int x = int(id.x), y = int(id.y);
  uint3 sum = uint3(0, 0, 0);
  for (int k = -p.i0; k <= p.i0; ++k) {
    const int sx = p.i1 == 0 ? clamp(x + k, 0, int(p.width) - 1) : x;
    const int sy = p.i1 == 0 ? y : clamp(y + k, 0, int(p.height) - 1);
    sum += texel(src, sx, sy).rgb;
  }
  const uint span = uint(2 * p.i0 + 1);
  float4 o = float4(texel(src, x, y));
  o.rgb = float3((sum * 2 + span) / (span * 2));
  put(dst, id, o);
}

kernel void streakMain(ARGS) {
  if (id.x >= p.width || id.y >= p.height) return;
  const int x = int(id.x), y = int(id.y);
  float3 acc = float3(0, 0, 0);
  for (int t = 0; t < p.i0; ++t) {
    const float u = -1.0f + 2.0f * (t + 0.5f) / p.i0;
    const float atX = clamp(x + p.f0 * u, 0.0f, p.width - 1.0f);
    const float atY = clamp(y + p.f1 * u, 0.0f, p.height - 1.0f);
    const int x0 = int(atX), y0 = int(atY);
    const int x1 = min(int(p.width) - 1, x0 + 1), y1 = min(int(p.height) - 1, y0 + 1);
    const float tx = atX - x0, ty = atY - y0;
    const float3 a = float3(texel(src, x0, y0).rgb), b = float3(texel(src, x1, y0).rgb);
    const float3 c = float3(texel(src, x0, y1).rgb), d = float3(texel(src, x1, y1).rgb);
    const float3 top = a + (b - a) * tx;
    const float3 bottom = c + (d - c) * tx;
    acc += top + (bottom - top) * ty;
  }
  float4 o = float4(texel(src, x, y));
  o.rgb = floor(c255(acc / p.i0 + 0.5f));
  put(dst, id, o);
}

kernel void tileMain(ARGS) {
  if (id.x >= p.width || id.y >= p.height) return;
  const int x0 = int(id.x) * p.i0, y0 = int(id.y) * p.i0;
  const int x1 = min(int(p.srcW), x0 + p.i0), y1 = min(int(p.srcH), y0 + p.i0);
  uint4 sum = uint4(0, 0, 0, 0);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) sum += texel(src, x, y);
  }
  put(dst, id, float4(sum / uint((y1 - y0) * (x1 - x0))));
}

kernel void nv12Main(ARGS) {
  if (id.x >= p.width || id.y >= p.height) return;
  float3 acc = float3(0, 0, 0);
  for (int j = 0; j < p.i0; ++j) {
    for (int i = 0; i < p.i0; ++i) {
      const int x = min(int(id.x) * p.i0 + i, int(p.srcW) - 1);
      const int y = min(int(id.y) * p.i0 + j, int(p.srcH) - 1);
      const float luma = src.read(uint2(x, y)).r;
      const float2 chroma = aux.read(uint2(x / 2, y / 2)).rg + p.f1;
      const float yv = (luma + p.f0) * p.f2;
      acc += saturate(float3(yv + p.f3 * chroma.y, yv - p.f4 * chroma.x - p.f5 * chroma.y, yv + p.f6 * chroma.x));
    }
  }
  put(dst, id, float4(floor(acc / float(p.i0 * p.i0) * 255.0f + 0.5f), 255.0f));
}
