/*{
  "DESCRIPTION": "Test shader: every pixel is opaque red (255, 0, 0, 255). tools/check_isf.py checks it exactly.",
  "CREDIT": "Deckboy (GPL-3.0-or-later)",
  "CATEGORIES": ["Test"],
  "INPUTS": []
}*/

void main() {
  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
