/*{
  "DESCRIPTION": "Test shader: tells which way is up. The top half is green, the bottom half blue, the left quarter has a red first column.",
  "CREDIT": "Deckboy (GPL-3.0-or-later)",
  "CATEGORIES": ["Test"],
  "INPUTS": []
}*/

void main() {
  // isf_FragNormCoord has y = 1 at the TOP of the picture
  vec3 col = isf_FragNormCoord.y > 0.5 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
  if (isf_FragCoord.x < 1.0) col = vec3(1.0, 0.0, 0.0);
  gl_FragColor = vec4(col, 1.0);
}
