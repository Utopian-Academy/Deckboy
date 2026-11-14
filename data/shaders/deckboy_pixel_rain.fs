/*{
  "DESCRIPTION": "Chunky pixel rain: bright heads fall down columns and leave a tail that fades. Make the pixels big for a Pocket feel.",
  "CREDIT": "Deckboy (GPL-3.0-or-later)",
  "CATEGORIES": ["Generator"],
  "INPUTS": [
    { "NAME": "speed", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "speed" },
    { "NAME": "pixelSize", "TYPE": "float", "DEFAULT": 16.0, "MIN": 4.0, "MAX": 64.0, "LABEL": "pixel size" },
    { "NAME": "tail", "TYPE": "float", "DEFAULT": 10.0, "MIN": 2.0, "MAX": 40.0, "LABEL": "tail length" },
    { "NAME": "rain", "TYPE": "color", "DEFAULT": [0.2, 0.9, 0.55, 1.0], "LABEL": "rain" },
    { "NAME": "ground", "TYPE": "color", "DEFAULT": [0.0, 0.04, 0.05, 1.0], "LABEL": "background" },
    { "NAME": "flicker", "TYPE": "bool", "DEFAULT": true, "LABEL": "flicker" }
  ]
}*/

float hash11(float n) {
  return fract(sin(n * 12.9898) * 43758.5453);
}

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
  vec2 cell = floor(isf_FragCoord.xy / pixelSize);
  vec2 local = fract(isf_FragCoord.xy / pixelSize);
  float rows = RENDERSIZE.y / pixelSize;
  float column = hash11(cell.x + 1.0);
  float fall = mix(0.5, 1.4, column);
  float head = fract(TIME * speed * 0.1 * fall + column * 7.0) * (rows + tail * 1.5) - tail * 0.25;
  float fromTop = rows - cell.y;            // 0 on the top row, counting down
  float behind = head - fromTop;            // above 0: the tail trails the head
  float glow = 0.0;
  if (behind >= 0.0) glow = exp(-behind / (tail * 0.45));
  float spark = 1.0;
  if (flicker) spark = 0.55 + 0.45 * hash21(cell + floor(TIME * 9.0));
  vec2 inner = step(vec2(0.12), local) * step(local, vec2(0.88));
  float square = inner.x * inner.y;
  vec3 col = ground.rgb;
  vec3 lit = rain.rgb * glow * spark;
  if (behind >= 0.0 && behind < 1.0) lit = mix(rain.rgb, vec3(1.0), 0.8);
  col += lit * square;
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
