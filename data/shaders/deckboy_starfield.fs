/*{
  "DESCRIPTION": "Flying through stars. Steer with the point, tint the light, make the sky busy or bare.",
  "CREDIT": "Deckboy (GPL-3.0-or-later)",
  "CATEGORIES": ["Generator"],
  "INPUTS": [
    { "NAME": "speed", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "speed" },
    { "NAME": "density", "TYPE": "float", "DEFAULT": 0.35, "MIN": 0.02, "MAX": 0.9, "LABEL": "how many" },
    { "NAME": "size", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.3, "MAX": 3.0, "LABEL": "star size" },
    { "NAME": "steer", "TYPE": "point2D", "DEFAULT": [0.0, 0.0], "MIN": [-1.0, -1.0], "MAX": [1.0, 1.0], "LABEL": "steer" },
    { "NAME": "tint", "TYPE": "color", "DEFAULT": [0.75, 0.85, 1.0, 1.0], "LABEL": "starlight" },
    { "NAME": "dust", "TYPE": "bool", "DEFAULT": true, "LABEL": "dust behind" }
  ]
}*/

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
  float aspect = RENDERSIZE.x / RENDERSIZE.y;
  vec2 p = (isf_FragNormCoord - 0.5) * vec2(aspect, 1.0) - steer * 0.3;
  vec3 col = vec3(0.0, 0.0, 0.02);
  if (dust) {
    float cloud = 0.5 + 0.5 * sin(p.x * 3.0 + TIME * 0.05) * sin(p.y * 2.3 - TIME * 0.04);
    col += vec3(0.05, 0.02, 0.09) * cloud * cloud;
  }
  for (int i = 0; i < 8; i++) {
    float fi = float(i);
    float depth = fract(fi / 8.0 - TIME * speed * 0.12);
    float cells = mix(26.0, 3.0, 1.0 - depth);
    vec2 q = p * cells + fi * 17.31;
    vec2 cell = floor(q);
    vec2 local = fract(q) - 0.5;
    float h = hash21(cell + fi);
    if (h > density) continue;
    vec2 off = (vec2(hash21(cell * 1.3 + 4.0), hash21(cell * 2.7 + 9.0)) - 0.5) * 0.7;
    float d = length(local - off);
    float fade = smoothstep(1.0, 0.75, depth) * smoothstep(0.0, 0.12, depth);
    float radius = 0.05 * size * (1.0 + 2.5 * (1.0 - depth));
    float star = smoothstep(radius, 0.0, d) * fade;
    float twinkle = 0.75 + 0.25 * sin(TIME * 6.0 + h * 90.0);
    col += tint.rgb * star * twinkle * (0.6 + 0.8 * hash21(cell + 3.0));
  }
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
