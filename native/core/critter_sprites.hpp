// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.
#pragma once
// THE CRITTER BRUSH'S CRITTERS (board BBfda3Tf, James 2026-11-25: "this method
// of drawing with animated sprites and critters"; "quite silly and slightly
// unexpected"). Drawn for Deckboy: 10x10 pixels, two frames each, a letter per
// pixel from one shared palette. Never copied from anyone's sprite pack.
#include <cstdint>

namespace deckboy::draw {

struct CritterSprite {
  const char* name;
  const char* frames[2][10];
};

// '.' is clear; every other letter is a colour in critterInk().
inline std::uint32_t critterInk(char c) {
  switch (c) {
    case 'k': return 0x1B1B22FFu;   // outline
    case 'w': return 0xFFFFFFFFu;
    case 'r': return 0xE8394DFFu;
    case 'p': return 0xFF9AA8FFu;
    case 'y': return 0xFFD23FFFu;
    case 'o': return 0xFF8A2AFFu;
    case 'b': return 0x3AA6FFFFu;
    case 'c': return 0x9FE0FFFFu;
    case 'g': return 0x5FCF4AFFu;
    case 'd': return 0x2C7A2AFFu;
    case 'v': return 0xB46CFFFFu;
    case 'l': return 0xE3C3FFFFu;
    case 'n': return 0x8A5A3AFFu;
    case 's': return 0xC9C9D6FFu;
    case 'a': return 0x6E6E80FFu;
    case 'e': return 0xF2D29BFFu;
    default:  return 0u;
  }
}

inline constexpr CritterSprite kCritters[] = {
  {"heart", {{".rr...rr..", "rpprr.rrr.", "rpprrrrrr.", "rrrrrrrrr.", ".rrrrrrr..", "..rrrrr...", "...rrr....", "....r.....", "..........", ".........."},
             {"..........", ".rr..rr...", "rprrrrrr..", "rrrrrrrr..", ".rrrrrr...", "..rrrr....", "...rr.....", "..........", "..........", ".........."}}},
  {"star", {{"....y.....", "....y.....", "...yyy....", "yyyyyyyyy.", ".yyyyyyy..", "..yyyyy...", "..yy.yy...", ".yy...yy..", ".y.....y..", ".........."},
            {"....w.....", "....y.....", "...ywy....", "yyyyyyyyy.", ".yyyyyyy..", "..yyyyy...", "..yy.yy...", ".yy...yy..", ".y.....y..", ".........."}}},
  {"slime", {{"..........", "...bbbb...", "..bccbbb..", ".bcbbbbbb.", ".bwkbbwkb.", ".bbbbbbbb.", "bbbbbbbbbb", "bbbbbbbbbb", ".bbbbbbbb.", ".........."},
             {"..........", "..........", "....bbb...", "..bccbbbb.", ".bcwkbwkbb", "bbbbbbbbbb", "bbbbbbbbbb", "bbbbbbbbbb", "bbbbbbbbbb", ".........."}}},
  {"fish", {{"..........", "...ooo....", "..oyyoo..o", ".okooooooo", "ooooooooo.", ".ooooooooo", "..oyyoo..o", "...ooo....", "..........", ".........."},
            {"..........", "...ooo....", "..oyyoo...", ".okoooooo.", "ooooooooo.", ".ooooooooo", "..oyyoo.oo", "...ooo...o", "..........", ".........."}}},
  {"mushroom", {{"...rrrr...", ".rrwrrwrr.", "rrrrrrrrrr", "rwrrrrrrwr", "rrrrrrrrrr", "...wwww...", "...wkwk...", "...wwww...", "...wwww...", ".........."},
                {"..........", "...rrrr...", ".rrwrrwrr.", "rrrrrrrrrr", "rwrrrrrrwr", "rrrrrrrrrr", "...wwww...", "...wkwk...", "..wwwwww..", ".........."}}},
  {"potion", {{"....nn....", "....ss....", "...s..s...", "..s....s..", ".svvvvvvs.", "svlvvvvvvs", "svvvvvlvvs", "svvvvvvvvs", ".svvvvvvs.", "..ssssss.."},
              {"....nn..l.", "....ss.l..", "...s..s...", "..s....s..", ".svvlvvvs.", "svvvvvvvvs", "svvvvvvvvs", "svlvvvvvvs", ".svvvvvvs.", "..ssssss.."}}},
  {"octopus", {{"...gggg...", "..gggggg..", ".gggggggg.", ".gwkggwkg.", ".gggggggg.", "..gggggg..", ".g.g..g.g.", "g..g..g..g", "g...gg...g", ".........."},
               {"...gggg...", "..gggggg..", ".gggggggg.", ".gwkggwkg.", ".gggggggg.", "..gggggg..", "..g.gg.g..", ".g..gg..g.", ".g.g..g.g.", ".........."}}},
  {"flame", {{"....y.....", "...yo.....", "...yoo....", "..yooo.y..", "..yoroo...", ".yorrroo..", ".orrrrro..", ".orryrro..", "..orrro...", "...ooo...."},
             {".....y....", ".....oy...", "....ooy...", "..y.oooy..", "...ooroy..", "..oorrroy.", "..orrrrro.", "..orryrro.", "...orrro..", "....ooo..."}}},
  {"ghost", {{"...wwww...", "..wwwwww..", ".wwwwwwww.", ".wkwwkwww.", ".wwwwwwww.", ".wwwwwwww.", ".wwwwwwww.", ".wwwwwwww.", ".w.ww.ww.w", ".........."},
             {"...wwww...", "..wwwwww..", ".wwwwwwww.", ".wwkwwkww.", ".wwwwwwww.", ".wwwwwwww.", ".wwwwwwww.", ".wwwwwwww.", "w.ww.ww.w.", ".........."}}},
  {"frog", {{"..........", ".gg....gg.", "gwkg..gwkg", "gggggggggg", "gggggggggg", "g.gddddg.g", "gggggggggg", ".g.gggg.g.", "gg......gg", ".........."},
            {".gg....gg.", "gwkg..gwkg", "gggggggggg", "gggrrrrggg", "gggggggggg", ".gggddggg.", "..gggggg..", ".g......g.", "g........g", ".........."}}},
  {"cat", {{"..........", "a...a.....", "aa.aa.....", "aaaaa.....", "akaka....a", "aaaaa...a.", ".aaaaaaaa.", ".aaaaaaaa.", ".a.a..a.a.", ".........."},
           {"..........", "a...a.....", "aa.aa.....", "aaaaa...a.", "akaka...a.", "aaaaa..a..", ".aaaaaaaa.", ".aaaaaaaa.", ".a.a..a.a.", ".........."}}},
  {"snail", {{"..........", "......nnn.", "c....nnenn", "c...nnennn", "cc..nneenn", "ckc.nnnnnn", "cccccccccc", ".cccccccc.", "..........", ".........."},
             {"..........", "......nnn.", ".c...nnenn", ".c..nnennn", ".cc.nneenn", ".ckcnnnnnn", "cccccccccc", ".cccccccc.", "..........", ".........."}}},
  {"bat", {{"..........", "v..v..v..v", "vv.vvvv.vv", "vvvvvvvvvv", ".vvvwkvvv.", "..vvvvvv..", "...v..v...", "..........", "..........", ".........."},
           {"..........", "..........", "...vvvv...", "v.vvvvvv.v", "vvvvwkvvvv", "vv.vvvv.vv", "v..v..v..v", "..........", "..........", ".........."}}},
  {"coin", {{"..yyyyy...", ".yyyyyyy..", "yyywwyyyy.", "yyywyyyyy.", "yyyyyyyyy.", "yyyyyyyyy.", "yyyyyyyyy.", ".yyyyyyy..", "..yyyyy...", ".........."},
            {"...yyy....", "...yyy....", "...ywy....", "...ywy....", "...yyy....", "...yyy....", "...yyy....", "...yyy....", "...yyy....", ".........."}}},
  {"duck", {{"...yyy....", "..yyyky...", "..yyyyyoo.", "...yyy....", ".yyyyyyy..", "yyyyyyyyy.", "yyyyyyyyy.", ".yyyyyyy..", "..........", ".........."},
            {"..........", "...yyy....", "..yyyky...", "..yyyyyoo.", "...yyy....", ".yyyyyyy..", "yyyyyyyyyy", ".yyyyyyyy.", "..........", ".........."}}},
  {"toast", {{".nnnnnnnn.", "neeeeeeeen", "neeeeeeeen", "nekeeekeen", "neeeeeeeen", "neeyyyyeen", "neeerreeen", "neeeeeeeen", "nnnnnnnnnn", ".........."},
             {".nnnnnnnn.", "neeeeeeeen", "neeyyyyeen", "nekeeekeen", "neeeeeeeen", "neeerreeen", "neeeeeeeen", "neeeeeeeen", "nnnnnnnnnn", ".........."}}},
  {"moth", {{"..........", "s...k...s.", "ss..k..ss.", "sss.k.sss.", "ssssksssss", "sss.k.sss.", "ss..k..ss.", "s.......s.", "..........", ".........."},
            {"..........", "....k.....", "....k.....", ".ss.k.ss..", "sssskssss.", ".ss.k.ss..", "....k.....", "..........", "..........", ".........."}}},
  {"jellyfish", {{"...pppp...", "..pppppp..", ".pplpppp..", ".pppppppp.", "..p.p.p.p.", "..p.p.p.p.", ".p.p.p.p..", ".p.p.p.p..", "..........", ".........."},
                 {"..........", "...pppp...", "..pppppp..", ".pplppppp.", ".pppppppp.", "..p.p.p.p.", "..p.p.p.p.", "..p.p.p.p.", ".p.p.p.p..", ".........."}}},
};
inline constexpr int kCritterCount = static_cast<int>(sizeof(kCritters) / sizeof(kCritters[0]));

}  // namespace deckboy::draw
