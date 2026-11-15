/*{
  "DESCRIPTION": "A circuit-bent video enhancer with nothing plugged in: it makes its own picture and bends it. Brightness becomes a few loud colours that drift, the colour floods past edges in blocks, edges ring in fine stripes and the sync tears. The CIRCUIT BEND effect does the same to any cue.",
  "CREDIT": "Deckboy (GPL-3.0-or-later). The same arithmetic as the Circuit Bend effect.",
  "CATEGORIES": ["Generator"],
  "INPUTS": [
    { "NAME": "picture", "TYPE": "long", "DEFAULT": 2, "VALUES": [0, 1, 2, 3], "LABELS": ["colour bars", "ramp", "blobs", "noise"], "LABEL": "picture" },
    { "NAME": "speed", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 2.0, "LABEL": "speed" },
    { "NAME": "bands", "TYPE": "float", "DEFAULT": 0.4, "MIN": 0.0, "MAX": 1.0, "LABEL": "bands" },
    { "NAME": "drift", "TYPE": "float", "DEFAULT": 0.35, "MIN": 0.0, "MAX": 1.0, "LABEL": "drift" },
    { "NAME": "bleed", "TYPE": "float", "DEFAULT": 0.4, "MIN": 0.0, "MAX": 1.0, "LABEL": "bleed" },
    { "NAME": "ringing", "TYPE": "float", "DEFAULT": 0.45, "MIN": 0.0, "MAX": 1.0, "LABEL": "ringing" },
    { "NAME": "tears", "TYPE": "float", "DEFAULT": 0.25, "MIN": 0.0, "MAX": 1.0, "LABEL": "tears" }
  ]
}*/

uint bendHash(uint n) {
  n = (n ^ 61u) ^ (n >> 16); n = n + (n << 3); n = n ^ (n >> 4); n *= 0x27d4eb2du; n = n ^ (n >> 15);
  return n;
}
float hash01(uint n) { return float(bendHash(n)) / 4294967296.0; }

float valueNoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  float a = hash01(uint(int(i.x) * 73 + int(i.y) * 9973 + 100000));
  float b = hash01(uint(int(i.x + 1.0) * 73 + int(i.y) * 9973 + 100000));
  float c = hash01(uint(int(i.x) * 73 + int(i.y + 1.0) * 9973 + 100000));
  float d = hash01(uint(int(i.x + 1.0) * 73 + int(i.y + 1.0) * 9973 + 100000));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// The picture the enhancer is fed, as brightness, at a pixel.
float pictureLuma(float x, float y, float t) {
  vec2 uv = vec2(x, y) / RENDERSIZE;
  if (picture == 0) {
    // colour bars, as luma, drifting sideways
    float bar = floor(fract(uv.x + t * 0.03) * 7.0);
    float lum[7] = float[7](0.92, 0.78, 0.65, 0.55, 0.42, 0.3, 0.12);
    float l = lum[int(bar)];
    if (uv.y > 0.75) l = fract(uv.x * 4.0 - t * 0.1);
    return l;
  }
  if (picture == 1) {
    return fract(uv.x * 1.5 + uv.y * 0.6 + t * 0.07 + 0.08 * sin(uv.y * 9.0 + t));
  }
  if (picture == 2) {
    float aspect = RENDERSIZE.x / RENDERSIZE.y;
    vec2 p = vec2(uv.x * aspect, uv.y);
    float field = 0.0;
    for (int i = 0; i < 6; i++) {
      float fi = float(i);
      vec2 c = vec2(aspect * (0.5 + 0.38 * sin(t * (0.31 + 0.07 * fi) + fi * 2.3)),
                    0.5 + 0.38 * sin(t * (0.27 + 0.05 * fi) + fi * 1.7));
      vec2 d = p - c;
      field += 0.018 / (dot(d, d) + 0.004);
    }
    return clamp(field * 0.35, 0.0, 1.0);
  }
  return valueNoise(uv * vec2(9.0, 6.0) + vec2(t * 0.4, t * 0.15)) * 0.7 +
         valueNoise(uv * vec2(31.0, 22.0) - vec2(t * 0.9, 0.0)) * 0.3;
}

void main() {
  float W = RENDERSIZE.x, H = RENDERSIZE.y;
  float t = TIME * speed;
  vec2 px = floor(gl_FragCoord.xy);
  float x = px.x, y = H - 1.0 - px.y;     // from the top, as the effect counts lines
  float s = W / 480.0;
  // The effect's numbers (cue_effects.hpp, circuitBendSetup).
  int steps = 2 + int(round(bands * 10.0));
  float ph = TIME * drift * drift * 0.6 + 0.2 * sin(TIME * 0.23) * drift;
  float phase = ph - floor(ph);
  float block = max(1.0, round(min(W, H) * bleed * bleed * 0.06));
  float stride = max(1.0, round(s * 0.6));
  float bandH = max(2.0, round(5.0 * s));
  uint tf = uint(TIME * 60.0) / 4u;
  // Tears: a band of lines slips sideways for a moment.
  uint band = uint(floor(y / bandH));
  float sh = 0.0;
  if (hash01(tf * 7919u + band * 31u) < tears * 0.3) sh = round((hash01(tf * 104729u + band * 17u + 1u) - 0.5) * W * 0.3 * tears);
  // Ringing after every edge.
  float ring = 0.0;
  for (int k = 1; k <= 16; k++) {
    float xa = clamp(x - float(k) * stride, 0.0, W - 1.0);
    float xb = clamp(x - float(k + 1) * stride, 0.0, W - 1.0);
    float w = pow(0.8, float(k)) * cos(6.2831853 * float(k) / 6.0);
    ring += (pictureLuma(clamp(xa - sh, 0.0, W - 1.0), y, t) - pictureLuma(clamp(xb - sh, 0.0, W - 1.0), y, t)) * w;
  }
  float by = min(H - 1.0, floor(y / block) * block + floor(block / 2.0));
  float bx = min(W - 1.0, floor(x / block) * block + floor(block / 2.0));
  float yb = pictureLuma(bx, by, t);
  float yf = clamp(pictureLuma(clamp(x - sh, 0.0, W - 1.0), y, t) + ringing * 1.6 * ring, 0.0, 1.0);
  int b = min(steps - 1, int(floor(yb * float(steps))));
  float hue = float(b) * 0.618034 + phase + ring * ringing * 0.8;
  hue -= floor(hue);
  float h6 = hue * 6.0;
  int sector = min(5, int(floor(h6)));
  float fr = h6 - float(sector);
  float lo = 0.1, dn = 1.0 - 0.9 * fr, up = 1.0 - 0.9 * (1.0 - fr);
  vec3 c = vec3(1.0, lo, dn);
  if (sector == 0) c = vec3(1.0, up, lo);
  else if (sector == 1) c = vec3(dn, 1.0, lo);
  else if (sector == 2) c = vec3(lo, 1.0, up);
  else if (sector == 3) c = vec3(lo, dn, 1.0);
  else if (sector == 4) c = vec3(up, lo, 1.0);
  float bright = 0.6 + 0.5 * yf;
  float white = 0.35 * yf * yf;
  gl_FragColor = vec4(clamp(c * bright * (1.0 - white) + white, 0.0, 1.0), 1.0);
}
