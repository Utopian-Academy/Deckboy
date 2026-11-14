/*{
  "DESCRIPTION": "A slow, juicy plasma: sine waves laid over each other and coloured from two colours you pick.",
  "CREDIT": "Deckboy (GPL-3.0-or-later)",
  "CATEGORIES": ["Generator"],
  "INPUTS": [
    { "NAME": "speed", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 3.0, "LABEL": "speed" },
    { "NAME": "zoom", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.2, "MAX": 4.0, "LABEL": "zoom" },
    { "NAME": "warp", "TYPE": "float", "DEFAULT": 0.6, "MIN": 0.0, "MAX": 1.0, "LABEL": "warp" },
    { "NAME": "colourOne", "TYPE": "color", "DEFAULT": [1.0, 0.2, 0.6, 1.0], "LABEL": "colour one" },
    { "NAME": "colourTwo", "TYPE": "color", "DEFAULT": [0.1, 0.7, 1.0, 1.0], "LABEL": "colour two" },
    { "NAME": "bands", "TYPE": "bool", "DEFAULT": false, "LABEL": "poster bands" }
  ]
}*/

void main() {
  vec2 p = (isf_FragNormCoord - 0.5) * vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0) * 6.0 * zoom;
  float t = TIME * speed;
  float v = sin(p.x + t);
  v += sin((p.y + t) * 0.5);
  v += sin((p.x + p.y + t) * 0.5);
  vec2 c = p + vec2(sin(t * 0.33), cos(t * 0.5)) * 2.0;
  v += sin(sqrt(dot(c, c) + 1.0) + t) * (0.5 + warp);
  v *= 0.5;
  float k = 0.5 + 0.5 * sin(v * 3.14159);
  if (bands) k = floor(k * 5.0) / 4.0;
  vec3 col = mix(colourOne.rgb, colourTwo.rgb, k);
  col += 0.12 * sin(vec3(v * 4.0, v * 5.0 + 2.0, v * 6.0 + 4.0));
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
