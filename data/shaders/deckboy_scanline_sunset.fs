/*{
  "DESCRIPTION": "A striped sun sinking over a neon grid, seen on a tired television. Raise the scanlines for more tired.",
  "CREDIT": "Deckboy (GPL-3.0-or-later)",
  "CATEGORIES": ["Generator"],
  "INPUTS": [
    { "NAME": "speed", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "speed" },
    { "NAME": "sunHeight", "TYPE": "float", "DEFAULT": 0.55, "MIN": 0.0, "MAX": 1.0, "LABEL": "sun height" },
    { "NAME": "scanlines", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 1.0, "LABEL": "scanlines" },
    { "NAME": "sunTop", "TYPE": "color", "DEFAULT": [1.0, 0.9, 0.2, 1.0], "LABEL": "sun, top" },
    { "NAME": "sunBottom", "TYPE": "color", "DEFAULT": [1.0, 0.15, 0.5, 1.0], "LABEL": "sun, bottom" },
    { "NAME": "grid", "TYPE": "bool", "DEFAULT": true, "LABEL": "neon grid" }
  ]
}*/

void main() {
  float aspect = RENDERSIZE.x / RENDERSIZE.y;
  vec2 uv = isf_FragNormCoord;
  vec2 p = vec2(uv.x * aspect, uv.y);
  float horizon = 0.38;
  vec3 col;
  if (uv.y > horizon) {
    float k = (uv.y - horizon) / (1.0 - horizon);
    col = mix(vec3(0.95, 0.25, 0.45), vec3(0.08, 0.02, 0.22), pow(k, 0.6));
    vec2 c = vec2(aspect * 0.5, horizon + 0.08 + 0.3 * sunHeight);
    float r = 0.24;
    float d = length(p - c);
    float s = (c.y - p.y) / r;               // 0 at the middle of the sun, 1 at its foot
    float cut = 1.0;
    if (s > 0.0) {
      float stripe = fract(p.y * 34.0 + TIME * speed * 0.35);
      cut = step(s * 0.55, stripe);
    }
    float body = smoothstep(r, r - 0.006, d) * cut;
    vec3 sun = mix(sunBottom.rgb, sunTop.rgb, clamp(1.0 - s * 0.5, 0.0, 1.0));
    col += sunBottom.rgb * 0.35 * smoothstep(r * 2.2, r, d);   // the glow round it
    col = mix(col, sun, body);
  } else {
    float below = horizon - uv.y;
    float z = 0.12 / below;
    float x = (p.x - aspect * 0.5) * z * 6.0;
    col = mix(vec3(0.25, 0.02, 0.35), vec3(0.03, 0.0, 0.1), clamp(below * 3.0, 0.0, 1.0));
    if (grid) {
      float lx = smoothstep(0.47, 0.5, abs(fract(x) - 0.5));
      float lz = smoothstep(0.46, 0.5, abs(fract(z * 3.0 - TIME * speed * 0.45) - 0.5));
      float line = max(lx, lz) * clamp(below * 6.0, 0.0, 1.0);
      col += vec3(0.1, 0.9, 1.0) * line * 0.9;
    }
  }
  // the television: dark scanlines, and a pinch of vignette
  float scan = 0.5 + 0.5 * sin(gl_FragCoord.y * 3.14159 * 0.75);
  col *= 1.0 - scanlines * 0.35 * (1.0 - scan);
  vec2 v = uv - 0.5;
  col *= 1.0 - scanlines * 0.6 * dot(v, v);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
