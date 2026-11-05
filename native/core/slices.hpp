#pragma once
// SLICES: pieces of the programme picture placed on an output.
//
// James, 2026-11-04 (Screens board 5Dsz7KuL, "lets do 2, then 3"): what
// Resolume calls Advanced Output. A slice takes a region of the programme
// (its crop) and puts it somewhere on the output (its place), pinned at its
// corners if the surface is not square on, soft at its edges where two
// projectors overlap, and cut by a mask if it should be a shape.
//
// An output with no slices shows what it always has: its area of interest,
// warp and edge blend. Slices are what an output has once it needs more than
// one piece.
//
// Fractions throughout: a crop is a fraction of the programme, a place and
// a corner's nudge are fractions of the output, an edge is a fraction of the
// slice. SDL-free.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "cue_effects.hpp"

namespace deckboy::slices {

struct Slice {
  std::string name;
  bool enabled = true;
  // Where in the programme: x, y, w, h.
  float srcX = 0.0f, srcY = 0.0f, srcW = 1.0f, srcH = 1.0f;
  // Where on the output: x, y, w, h.
  float dstX = 0.0f, dstY = 0.0f, dstW = 1.0f, dstH = 1.0f;
  // Each corner's nudge from the place, TL TR BR BL as x, y pairs.
  std::array<float, 8> corners {};
  // Straight lines stay straight when pinned (perspective), or two flat
  // triangles (linear), as for an output's own warp.
  bool perspective = true;
  // Soft edges, as a fraction of the slice: left, right, top, bottom.
  float edgeL = 0.0f, edgeR = 0.0f, edgeT = 0.0f, edgeB = 0.0f;
  std::string maskId;   // Project::masks by id; empty: none
  // What the slice takes its picture from: "" the programme, "deck:<n>"
  // playlist n on its own (zero based), as a Resolume slice can take a layer.
  std::string source;
  // Degrees: the crop turned about its middle (input), and the placed
  // picture turned about its middle (output).
  float inRotation = 0.0f;
  float outRotation = 0.0f;
  bool flipH = false;
  bool flipV = false;
  float opacity = 1.0f;
  // Oscillators: across, down, rotation, opacity (kSliceLfo*). Saved apart
  // from the rest, in the show's own oscillator spelling.
  std::array<deckboy::effects::ParamLfo, 4> lfo {};
};

constexpr int kSliceLfoX = 0;
constexpr int kSliceLfoY = 1;
constexpr int kSliceLfoRotation = 2;
constexpr int kSliceLfoOpacity = 3;

constexpr int kMaxSlices = 16;

inline bool pinned(const Slice& s) {
  for (float c : s.corners) {
    if (c != 0.0f) return true;
  }
  return false;
}

inline bool soft(const Slice& s) {
  return s.edgeL > 0.0001f || s.edgeR > 0.0001f || s.edgeT > 0.0001f || s.edgeB > 0.0001f;
}

inline Slice clampSlice(Slice s) {
  auto c01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
  s.srcX = c01(s.srcX);
  s.srcY = c01(s.srcY);
  s.srcW = std::clamp(s.srcW, 0.001f, 1.0f - s.srcX);
  s.srcH = std::clamp(s.srcH, 0.001f, 1.0f - s.srcY);
  // A place may hang off the output (a slice can be pushed part way off a
  // screen), but not without limit.
  s.dstX = std::clamp(s.dstX, -1.0f, 2.0f);
  s.dstY = std::clamp(s.dstY, -1.0f, 2.0f);
  s.dstW = std::clamp(s.dstW, 0.001f, 3.0f);
  s.dstH = std::clamp(s.dstH, 0.001f, 3.0f);
  for (float& c : s.corners) c = std::clamp(c, -1.0f, 1.0f);
  s.edgeL = std::clamp(s.edgeL, 0.0f, 0.5f);
  s.edgeR = std::clamp(s.edgeR, 0.0f, 0.5f);
  s.edgeT = std::clamp(s.edgeT, 0.0f, 0.5f);
  s.edgeB = std::clamp(s.edgeB, 0.0f, 0.5f);
  s.inRotation = std::clamp(s.inRotation, -360.0f, 360.0f);
  s.outRotation = std::clamp(s.outRotation, -360.0f, 360.0f);
  s.opacity = std::clamp(s.opacity, 0.0f, 1.0f);
  if (!s.source.empty() && s.source.rfind("deck:", 0) != 0) s.source.clear();
  for (char& ch : s.name) {
    if (ch == ',' || ch == ';' || ch == '|' || ch == '\t' || ch == '\n' || ch == '\r') ch = '_';
  }
  return s;
}

// The show file: slices split by ';', each one's fields by ','.
//   name,on,srcX,srcY,srcW,srcH,dstX,dstY,dstW,dstH,c0..c7,perspective,L,R,T,B,mask
inline std::string serialize(const std::vector<Slice>& slices) {
  std::string out;
  char buf[64];
  for (std::size_t i = 0; i < slices.size(); ++i) {
    const Slice s = clampSlice(slices[i]);
    if (i) out += ';';
    out += s.name;
    out += s.enabled ? ",1" : ",0";
    const float numbers[] = {s.srcX, s.srcY, s.srcW, s.srcH, s.dstX, s.dstY, s.dstW, s.dstH,
                             s.corners[0], s.corners[1], s.corners[2], s.corners[3],
                             s.corners[4], s.corners[5], s.corners[6], s.corners[7]};
    for (float v : numbers) {
      std::snprintf(buf, sizeof buf, ",%.6g", static_cast<double>(v));
      out += buf;
    }
    out += s.perspective ? ",1" : ",0";
    for (float v : {s.edgeL, s.edgeR, s.edgeT, s.edgeB}) {
      std::snprintf(buf, sizeof buf, ",%.6g", static_cast<double>(v));
      out += buf;
    }
    out += ',';
    out += s.maskId;
    // Appended after the mask, so a show from before them reads as it did.
    out += ',';
    out += s.source;
    std::snprintf(buf, sizeof buf, ",%.6g,%.6g,%d,%d,%.6g", static_cast<double>(s.inRotation),
                  static_cast<double>(s.outRotation), s.flipH ? 1 : 0, s.flipV ? 1 : 0,
                  static_cast<double>(s.opacity));
    out += buf;
  }
  return out;
}

inline std::vector<Slice> parse(const std::string& text) {
  std::vector<Slice> out;
  std::size_t at = 0;
  while (at < text.size() && static_cast<int>(out.size()) < kMaxSlices) {
    const std::size_t end = std::min(text.find(';', at), text.size());
    const std::string one = text.substr(at, end - at);
    at = end + 1;
    std::vector<std::string> f;
    std::size_t c = 0;
    while (true) {
      const std::size_t comma = one.find(',', c);
      f.push_back(one.substr(c, comma == std::string::npos ? std::string::npos : comma - c));
      if (comma == std::string::npos) break;
      c = comma + 1;
    }
    if (f.size() < 23) continue;
    auto num = [&](std::size_t i) { return static_cast<float>(std::atof(f[i].c_str())); };
    Slice s;
    s.name = f[0];
    s.enabled = f[1] != "0";
    s.srcX = num(2); s.srcY = num(3); s.srcW = num(4); s.srcH = num(5);
    s.dstX = num(6); s.dstY = num(7); s.dstW = num(8); s.dstH = num(9);
    for (int k = 0; k < 8; ++k) s.corners[static_cast<std::size_t>(k)] = num(10 + static_cast<std::size_t>(k));
    s.perspective = f[18] != "0";
    s.edgeL = num(19); s.edgeR = num(20); s.edgeT = num(21); s.edgeB = num(22);
    if (f.size() > 23) s.maskId = f[23];
    if (f.size() > 29) {
      s.source = f[24];
      s.inRotation = num(25);
      s.outRotation = num(26);
      s.flipH = f[27] == "1";
      s.flipV = f[28] == "1";
      s.opacity = num(29);
    }
    out.push_back(clampSlice(s));
  }
  return out;
}

}  // namespace deckboy::slices
