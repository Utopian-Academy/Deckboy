/*{
  "DESCRIPTION": "Brion Gysin's Dream Machine: a paper cylinder with cut-outs turns on a record player round a lamp, and the light flickers about ten times a second. EYES CLOSED shows what sitters saw through their eyelids. WARNING: flicker at 3 to 30 flashes a second can bring on seizures in people with photosensitive epilepsy. Warn the audience before you use it, or keep the flicker low.",
  "CREDIT": "Deckboy (GPL-3.0-or-later). After the Dream Machine of Brion Gysin and Ian Sommerville, 1961.",
  "CATEGORIES": ["Generator"],
  "INPUTS": [
    { "NAME": "view", "TYPE": "long", "DEFAULT": 0, "VALUES": [0, 1], "LABELS": ["the machine", "eyes closed"], "LABEL": "view" },
    { "NAME": "rpm", "TYPE": "float", "DEFAULT": 78.0, "MIN": 0.0, "MAX": 120.0, "LABEL": "rpm" },
    { "NAME": "cutouts", "TYPE": "long", "DEFAULT": 8, "VALUES": [4, 6, 8, 10, 12], "LABELS": ["4", "6", "8", "10", "12"], "LABEL": "cut-outs a turn" },
    { "NAME": "shapes", "TYPE": "long", "DEFAULT": 0, "VALUES": [0, 1, 2], "LABELS": ["Gysin's", "slots", "stars"], "LABEL": "cut-out shape" },
    { "NAME": "flicker", "TYPE": "float", "DEFAULT": 0.6, "MIN": 0.0, "MAX": 1.0, "LABEL": "flicker" },
    { "NAME": "spill", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 1.0, "LABEL": "light in the room" },
    { "NAME": "lamp", "TYPE": "color", "DEFAULT": [1.0, 0.78, 0.45, 1.0], "LABEL": "lamp" },
    { "NAME": "paper", "TYPE": "color", "DEFAULT": [0.08, 0.07, 0.06, 1.0], "LABEL": "cylinder" }
  ]
}*/

const float PI = 3.14159265;

// One cut-out in a cell: u across, v up, both -0.5..0.5. 1 inside the hole.
float hole(vec2 c, int kind, float row) {
  if (kind == 1) {
    // tall slots
    return step(abs(c.x), 0.16) * step(abs(c.y), 0.42);
  }
  if (kind == 2) {
    // five-pointed stars
    float a = atan(c.y, c.x) + row * 0.6;
    float r = length(c);
    float lim = 0.22 + 0.16 * cos(5.0 * a);
    return step(r, lim);
  }
  // Gysin's: ovals and triangles, row by row
  if (mod(row, 2.0) < 1.0) {
    return step(length(c * vec2(2.4, 1.3)), 0.5);
  }
  float y = c.y + 0.3;
  return step(0.0, y) * step(y, 0.62) * step(abs(c.x), 0.3 - 0.48 * y);
}

// How lit the holes are: one flash per cut-out passing the eye.
float flashes(float turns) {
  float phase = fract(turns * float(cutouts));
  float pulse = smoothstep(0.0, 0.18, phase) * smoothstep(0.62, 0.36, phase);
  return mix(1.0, pulse, flicker);
}

vec3 eyesClosed(vec2 p, float turns, float aspect) {
  float f = flashes(turns);
  // light through the eyelids: red, warm in the middle
  float r = length(p);
  vec3 lid = lamp.rgb * vec3(1.0, 0.35, 0.18);
  vec3 col = lid * (0.25 + 0.75 * f) * (1.15 - 0.6 * r);
  // the patterns: a mandala that turns, with rings moving out on each flash
  float a = atan(p.y, p.x);
  float k = float(cutouts);
  float petals = cos(a * k + turns * 2.0 * PI * 0.25 + sin(r * 9.0 - turns * 3.0) * 1.4);
  float rings = sin(r * 26.0 - turns * 2.0 * PI * k * 0.5);
  float grid = sin(p.x * 30.0 + turns) * sin(p.y * 30.0 - turns);
  float pattern = smoothstep(0.25, 0.9, petals * rings + 0.35 * grid);
  float hue = turns * 0.13 + r * 0.8;
  vec3 tint = 0.5 + 0.5 * cos(2.0 * PI * (hue + vec3(0.0, 0.33, 0.67)));
  col += tint * pattern * f * 0.7 * smoothstep(1.2, 0.1, r);
  return col;
}

vec3 machine(vec2 p, float turns, float aspect) {
  // the room: dark, with beams of lamp light sweeping the walls
  float f = flashes(turns);
  vec3 col = vec3(0.012, 0.01, 0.014);
  float beamA = atan(p.y + 0.05, p.x) * float(cutouts) * 0.5 - turns * 2.0 * PI * float(cutouts) * 0.5;
  float beams = pow(0.5 + 0.5 * cos(beamA), 8.0);
  col += lamp.rgb * spill * (0.05 + 0.25 * beams) * f * smoothstep(1.6, 0.2, length(p));

  // the record player
  vec2 deck = p - vec2(0.0, -0.66);
  if (abs(deck.x) < 0.46 && deck.y < 0.03 && deck.y > -0.12) {
    col = vec3(0.13, 0.09, 0.06) + 0.04 * step(deck.y, -0.02);
  }
  float platter = step(length(deck * vec2(1.0, 6.0)), 0.36) * step(-0.02, deck.y);
  col = mix(col, vec3(0.05), platter);

  // the cylinder
  float R = 0.3;
  float top = 0.6, bottom = -0.62;
  if (abs(p.x) < R && p.y < top && p.y > bottom) {
    float s = p.x / R;
    float shade = sqrt(1.0 - s * s);
    float theta = asin(s) / (2.0 * PI) + turns;         // in turns, round the drum
    float rows = 4.0;
    float v = (p.y - bottom) / (top - bottom) * rows;
    float row = floor(v);
    float u = fract(theta * float(cutouts) + 0.5 * row) - 0.5;
    vec2 c = vec2(u * 1.0, fract(v) - 0.5);
    // a hole seen at the drum's edge is narrower
    c.x /= max(shade, 0.2);
    float h = hole(c, shapes, row);
    vec3 drum = paper.rgb * (0.35 + 0.65 * shade);
    // the bulb behind the hole: brightest at the middle
    vec3 lit = lamp.rgb * (0.6 + 0.8 * shade) * (0.4 + 0.6 * f);
    col = mix(drum, lit, h);
    // the drum's lip at the top
    col *= 1.0 - 0.5 * smoothstep(top - 0.02, top, p.y);
  } else {
    // light round the drum's edge
    float dx = max(abs(p.x) - R, 0.0);
    float glow = exp(-dx * 14.0) * step(p.y, top) * step(bottom, p.y);
    col += lamp.rgb * glow * 0.12 * f * spill;
  }
  return col;
}

void main() {
  float aspect = RENDERSIZE.x / RENDERSIZE.y;
  vec2 p = (isf_FragNormCoord - 0.5) * 2.0;
  p.x *= aspect;
  float turns = TIME * rpm / 60.0;
  vec3 col = (view == 1) ? eyesClosed(p, turns, aspect) : machine(p, turns, aspect);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
