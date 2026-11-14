/*{
  "DESCRIPTION": "A lava lamp: warm blobs rise, merge and let go in the glass. Pick how many, and what the wax and the liquid are.",
  "CREDIT": "Deckboy (GPL-3.0-or-later)",
  "CATEGORIES": ["Generator"],
  "INPUTS": [
    { "NAME": "speed", "TYPE": "float", "DEFAULT": 0.6, "MIN": 0.0, "MAX": 3.0, "LABEL": "speed" },
    { "NAME": "blobs", "TYPE": "long", "DEFAULT": 6, "VALUES": [3, 6, 9], "LABELS": ["a few", "some", "plenty"], "LABEL": "how many" },
    { "NAME": "gooey", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.1, "MAX": 1.0, "LABEL": "gooey" },
    { "NAME": "wax", "TYPE": "color", "DEFAULT": [1.0, 0.35, 0.1, 1.0], "LABEL": "wax" },
    { "NAME": "liquidTop", "TYPE": "color", "DEFAULT": [0.25, 0.0, 0.35, 1.0], "LABEL": "liquid, top" },
    { "NAME": "liquidBottom", "TYPE": "color", "DEFAULT": [0.6, 0.05, 0.2, 1.0], "LABEL": "liquid, bottom" }
  ]
}*/

void main() {
  float aspect = RENDERSIZE.x / RENDERSIZE.y;
  vec2 p = vec2(isf_FragNormCoord.x * aspect, isf_FragNormCoord.y);
  float t = TIME * speed * 0.4;
  float field = 0.0;
  for (int i = 0; i < 9; i++) {
    if (i >= blobs) break;
    float fi = float(i);
    vec2 c = vec2(aspect * (0.5 + 0.28 * sin(t * (0.6 + 0.13 * fi) + fi * 2.1)),
                  0.5 + 0.42 * sin(t * (0.5 + 0.09 * fi) + fi * 1.3));
    float r = 0.105 + 0.05 * sin(fi * 3.0 + t * 1.7);
    vec2 d = p - c;
    field += r * r / (dot(d, d) + 0.0004);
  }
  vec3 liquid = mix(liquidBottom.rgb, liquidTop.rgb, isf_FragNormCoord.y);
  float edge = 1.0 - gooey * 0.6;
  float body = smoothstep(edge - 0.08, edge + 0.08, field);
  float rim = smoothstep(edge, edge + 0.7, field);
  vec3 blob = wax.rgb * (0.7 + 0.5 * rim) + vec3(0.25, 0.12, 0.05) * rim * rim;
  vec3 col = mix(liquid, blob, body);
  // the glass: dark at both edges, and a soft bright strip near the left one
  float glass = smoothstep(0.0, 0.12, isf_FragNormCoord.x) * smoothstep(1.0, 0.88, isf_FragNormCoord.x);
  col *= 0.55 + 0.45 * glass;
  col += 0.07 * smoothstep(0.1, 0.16, isf_FragNormCoord.x) * smoothstep(0.2, 0.14, isf_FragNormCoord.x);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
