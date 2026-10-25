// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
//
// TABLETOP: a projected wargames table. The generator draws the table in inches,
// fitted to the output raster; Deckboy's warp lines that rectangle up with the
// real table. Battleplans are text files in <state>/tabletop/<name>.txt that the
// owner enters from his own books, alongside the private built-in plans. The game on the
// night -- round, turn, points, who holds which objective -- lives in memory
// and is driven by the TABLE verb, from the phone page /remote/x/table, the
// desk, DASH or Companion.
//
// Plan file, one item per line, tab separated, inches from the top-left corner:
//   table      <w> <h>
//   objective  <x> <y> <range> <label>
//   objective-poly <label> <kind> <x,y> <x,y> ... (whole printed footprint)
//   pack       <name> (Spearhead layout; fixed physical dimensions)
//   note       <measurement/provenance note>
//   line       <x1> <y1> <x2> <y2>
//   territory  <1|2> <x,y> <x,y> ...
//   deployment <1|2> <x,y> <x,y> ...
//   place      <x> <y> <label>
//   terrain    <none|mist|embers|water|arcane|forest> <x,y> <x,y> ...
//   system     <aos|40k>  (40k: its five phases, 40mm markers, contested
//              objectives, five rounds, VP capped at 100, secondaries)
//   ruin       <label> <x,y> <x,y> ...  (a terrain footprint, lettered)
//   scorefrom  <round>   (no automatic scoring before this round)
//   scoreat    <end|command>  (score at the end of a turn, or at the start of
//              the next player's Command phase)
//
// 40k MISSIONS are put together on the night from three files the owner
// enters from his own cards, in <state>/tabletop/40k/:
//   deployments/<name>.txt  table, deployment, objective, line
//   primaries/<name>.txt    title, score, scorefrom, scoreat
//   rules/<name>.txt        title (shown as the mission rule)
// and terrain layouts in layouts/<name>.txt (ruin / terrain lines).

#include "core/private_extras.hpp"
#include "core/paths.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using deckboy::extras::Command;
using deckboy::extras::Generator;
using deckboy::extras::Registration;
using deckboy::extras::WebPage;

// Rows [0, n) split across the cores; each band's rows are independent.
template <typename Fn>
void parallelRows(int n, Fn fn) {
  const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 8);
  if (threads == 1 || n < 64) { fn(0, n); return; }
  std::vector<std::thread> pool;
  const int band = (n + threads - 1) / threads;
  for (int i = 0; i < threads; ++i) {
    const int a = i * band, b = std::min(n, a + band);
    if (a < b) pool.emplace_back([=] { fn(a, b); });
  }
  for (auto& t : pool) t.join();
}


using Clock = std::chrono::steady_clock;
struct Pt { double x = 0, y = 0; };
struct Rgb { int r, g, b; };
struct Phase { const char* name; Rgb colour; };
// The phases of a turn, and the colour the table's edge runs in for each.
const Phase kPhases[] = {
  {"START OF TURN", {235, 235, 235}},
  {"HERO PHASE", {240, 200, 60}},
  {"MOVEMENT PHASE", {70, 210, 100}},
  {"SHOOTING PHASE", {255, 150, 40}},
  {"CHARGE PHASE", {240, 60, 50}},
  {"COMBAT PHASE", {225, 50, 200}},
  {"END OF TURN", {160, 100, 240}},
};
constexpr int kPhaseCount = static_cast<int>(sizeof(kPhases) / sizeof(kPhases[0]));
// 40k's five, in the colours the design board gave them.
const Phase kPhases40k[] = {
  {"COMMAND PHASE", {242, 209, 74}},
  {"MOVEMENT PHASE", {74, 208, 138}},
  {"SHOOTING PHASE", {224, 72, 72}},
  {"CHARGE PHASE", {255, 122, 217}},
  {"FIGHT PHASE", {255, 255, 255}},
};
constexpr int kPhaseCount40k = static_cast<int>(sizeof(kPhases40k) / sizeof(kPhases40k[0]));
// 40k's default sides: blue and orange, apart in lightness as well as hue.
const Rgb k40kP1 {74, 163, 255}, k40kP2 {255, 154, 60};
// A 40k objective marker is 40mm across; control is measured from its edge.
constexpr double k40kMarkerRadius = 0.787;
// Attacker red, defender blue, as the battleplan cards colour them.
const Rgb kP1 {232, 74, 58}, kP2 {79, 141, 255}, kWhite {255, 255, 255}, kGold {231, 211, 106};
struct Objective {
  // owner: 0 nobody, 1 or 2 a player, 3 contested (40k)
  Pt p; double range = 3.0, marker = 0.0; std::string label, kind; int owner = 0;
  std::vector<Pt> poly;          // empty: circle; otherwise the objective's boundary
  Clock::time_point changed {};   // when it last changed hands: the ripple
  bool muted = false;            // inactive: no drawing, ownership edits or scoring
};
struct Line { Pt a, b; bool muted = false; };
struct Zone { int player = 1; std::vector<Pt> poly; bool muted = false; };
struct Place { Pt p; std::string label; int owner = 0; bool muted = false; };   // owner: places of power can be held
// A measurement on the table: rings round a point, a ruler between two, or
// a unit's move (its move, and its move plus a run).
struct Measure { std::string kind; Pt a, b; std::vector<double> radii; int player = 0; bool muted = false; };
struct Terrain { std::string look = "none"; std::vector<Pt> poly; bool muted = false; std::string label; };

struct Plan {
  std::string name = "blank", title = "Blank";
  std::string p1 = "PLAYER 1", p2 = "PLAYER 2";
  std::string twist;   // the battleplan's twist, by name
  std::string pack, note;       // board-only Spearhead presets use their own cards
  std::string system = "aos";   // "aos" or "40k"
  int scoreFrom = 1;            // no automatic scoring before this round
  bool scoreAtCommand = false;  // 40k primaries: score at the start of a Command phase
  // END-OF-TURN SCORING: AT_LEAST n, MORE, KIND <type>, or MANUAL (a button).
  struct Score { int vp = 0; std::string rule, arg, text; };
  std::vector<Score> scores;
  double w = 44.0, h = 30.0;
  std::vector<Objective> objectives;
  std::vector<Line> lines;
  std::vector<Zone> territories, deployments;
  std::vector<Place> places;    // places of power and of interest
  std::vector<Place> markers;   // where a piece of terrain goes
  std::vector<Terrain> terrain;
};

struct Game {
  int round = 1, turn = 1, vp1 = 0, vp2 = 0, highlight = 0;
  // The phase of the turn (index into kPhases); the table's edge runs in its
  // colour. NEXT TURN starts the next turn at its first phase.
  int phase = 0;
  bool showEdge = true;
  int cp1 = 0, cp2 = 0;   // command points
  // THE CLOCK: each player's time, running for whoever's turn it is.
  bool clockOn = false, clockPaused = true;
  double clock1 = 0, clock2 = 0;
  std::chrono::steady_clock::time_point clockSince {};
  std::vector<Measure> measures;
  // SCORING. Each turn's automatic award, by (round, turn), so stepping back
  // over the end of a turn can take it back off.
  bool autoScore = true;
  std::map<std::pair<int, int>, int> awarded;
  std::string lastScore;
  Clock::time_point scoredAt {};
  // BATTLE TACTICS, per player: the two cards they brought (indices into
  // kTacticCards, -1 none), the tactic they are going for (card slot 0/1 and
  // tactic 0-2, -1 none), and what they have completed.
  struct Tactics { int card[2] = {-1, -1}; int slot = -1, pick = -1; std::vector<std::string> done; };
  Tactics tac[2];
  // 40k SECONDARIES, per player: the cards in hand (typed in by name) and
  // the points each scored card earned.
  struct Secondaries { std::vector<std::string> active; std::vector<std::pair<std::string, int>> scored; };
  Secondaries sec[2];
  bool over = false;            // 40k: the fifth round has ended
  bool setup = false;           // SETTING UP: the mission card, no game strips
  std::string layout;           // a terrain layout laid over the plan
  bool calibrate = false, animate = true;
  bool showTerritories = true, showDeployment = true, showObjectives = true;
  bool showGuides = true; // printed board divider lines, independent of zones
  bool showPlaces = true, showTerrain = true, showStrip = true, showLabels = true;
  // THE LOOK. Style 0 clean, 1 arcane (rune halos), 2 ember (fire);
  // speed scales every animation (0 holds it still); brightness all of it.
  int style = 0;
  double speed = 1.0, bright = 1.0;
  // The season's maps are drawn 44" x 30"; its rules recommend 60" x 44" for
  // the same plans. On (the default), a 44 x 30 plan is scaled up to 60 x 44.
  bool board60 = true;
  // THE GROUND between everything: none, grass, swamp, ash, lava, snow, sand.
  std::string ground = "none";
  double groundLevel = 0.6;
  Rgb c1 = kP1, c2 = kP2, hl = kWhite, place = kGold;
};

std::mutex g_mutex;
Plan g_plan;
Game g_game;

bool is40k(const Plan& p) { return p.system == "40k"; }
int phaseCount(const Plan& p) { return is40k(p) ? kPhaseCount40k : kPhaseCount; }
const Phase& phaseAt(const Plan& p, int i) {
  return is40k(p) ? kPhases40k[std::clamp(i, 0, kPhaseCount40k - 1)] : kPhases[std::clamp(i, 0, kPhaseCount - 1)];
}
int vpCap(const Plan& p) { return is40k(p) ? 100 : 999; }
std::string upper(std::string s);   // defined with the TABLE verb
void capVp(const Plan& p, Game& g) { g.vp1 = std::clamp(g.vp1, 0, vpCap(p)); g.vp2 = std::clamp(g.vp2, 0, vpCap(p)); }

// One element list drives the command, desk and phone. Indices stay stable
// while an element is muted, so restoring it also restores its identity.
template<class P, class G, class F>
void visitElements(P& plan, G& game, F visit) {
  auto each = [&](const char* kind, const char* label, auto& list) {
    for (std::size_t i = 0; i < list.size(); ++i)
      visit(kind, i + 1, std::string(label) + " " + std::to_string(i + 1), list[i].muted);
  };
  for (std::size_t i = 0; i < plan.objectives.size(); ++i) {
    auto& ob = plan.objectives[i];
    visit("OBJECTIVE", i + 1, "Objective " + ob.label + (ob.kind.empty() ? "" : " - " + ob.kind), ob.muted);
  }
  each("GUIDE", "Board line", plan.lines);
  each("TERRITORY", "Territory", plan.territories);
  each("DEPLOYMENT", "Deployment", plan.deployments);
  each("TERRAIN", "Terrain", plan.terrain);
  each("MARKER", "Terrain marker", plan.markers);
  each("PLACE", "Place of power", plan.places);
  each("MEASURE", "Measurement", game.measures);
}

void retainPlanState(Plan& next, const Plan& previous) {
  auto copy = [](auto& to, const auto& from) {
    for (std::size_t i = 0; i < std::min(to.size(), from.size()); ++i) to[i].muted = from[i].muted;
  };
  copy(next.objectives, previous.objectives); copy(next.lines, previous.lines);
  copy(next.territories, previous.territories); copy(next.deployments, previous.deployments);
  copy(next.terrain, previous.terrain); copy(next.markers, previous.markers); copy(next.places, previous.places);
  for (std::size_t i = 0; i < std::min(next.objectives.size(), previous.objectives.size()); ++i)
    next.objectives[i].owner = previous.objectives[i].owner;
  for (std::size_t i = 0; i < std::min(next.places.size(), previous.places.size()); ++i)
    next.places[i].owner = previous.places[i].owner;
}

fs::path planDir() { return deckboy::core::Paths::stateDir() / "tabletop"; }

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : line) {
    if (c == '\t') { out.push_back(cur); cur.clear(); } else if (c != '\r') cur += c;
  }
  out.push_back(cur);
  return out;
}

std::vector<Pt> parsePoints(const std::vector<std::string>& f, std::size_t from) {
  std::vector<Pt> pts;
  for (std::size_t i = from; i < f.size(); ++i) {
    std::istringstream in(f[i]);
    std::string tok;
    while (in >> tok) {
      const auto comma = tok.find(',');
      if (comma == std::string::npos) continue;
      pts.push_back({std::atof(tok.c_str()), std::atof(tok.c_str() + comma + 1)});
    }
  }
  return pts;
}

struct BuiltinPlan { const char* slug; const char* title; const char* text; };
// The current season's battleplans, generated by tools/import_aosmissions.py.
const BuiltinPlan kBuiltinPlans[] = {
#include "plans_builtin.inc"
#include "spearhead_builtin.inc"
#include "plans_40k_builtin.inc"
};

struct Tactic { const char* name; int vp; const char* type; const char* text; };
struct TacticCard { const char* slug; const char* name; const char* rules; Tactic tactics[3]; };
// The season's battle tactic cards, generated by tools/import_tactics.py.
const TacticCard kTacticCards[] = {
#include "tactics_builtin.inc"
};
constexpr int kTacticCardCount = static_cast<int>(sizeof(kTacticCards) / sizeof(kTacticCards[0]));

Plan parsePlan(const std::string& name, const std::string& text) {
  Plan plan;
  plan.name = name;
  plan.title = name;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto f = splitTabs(line);
    const std::string& k = f[0];
    auto num = [&](std::size_t i) { return i < f.size() ? std::atof(f[i].c_str()) : 0.0; };
    if (k == "table" && f.size() >= 3) { plan.w = std::max(6.0, num(1)); plan.h = std::max(6.0, num(2)); }
    else if (k == "players" && f.size() >= 3) { plan.p1 = f[1]; plan.p2 = f[2]; }
    else if (k == "title" && f.size() >= 2) plan.title = f[1];
    else if (k == "twist" && f.size() >= 2) plan.twist = f[1];
    else if (k == "pack" && f.size() >= 2) plan.pack = f[1];
    else if (k == "note" && f.size() >= 2) plan.note = f[1];
    else if (k == "objective" && f.size() >= 3) {
      Objective o; o.p = {num(1), num(2)}; o.range = f.size() > 3 ? num(3) : 3.0;
      o.label = f.size() > 4 ? f[4] : std::to_string(plan.objectives.size() + 1);
      o.kind = f.size() > 5 ? f[5] : "";
      plan.objectives.push_back(o);
    }
    else if (k == "objective-poly" && f.size() >= 4) {
      Objective o; o.label = f[1]; o.kind = f[2]; o.range = 0;
      o.poly = parsePoints(f, 3);
      if (o.poly.size() >= 3) {
        for (const auto& p : o.poly) { o.p.x += p.x / o.poly.size(); o.p.y += p.y / o.poly.size(); }
        plan.objectives.push_back(o);
      }
    }
    else if (k == "line" && f.size() >= 5) plan.lines.push_back({{num(1), num(2)}, {num(3), num(4)}});
    else if ((k == "territory" || k == "deployment") && f.size() >= 3) {
      Zone z; z.player = static_cast<int>(num(1)) == 2 ? 2 : 1; z.poly = parsePoints(f, 2);
      (k == "territory" ? plan.territories : plan.deployments).push_back(z);
    }
    else if (k == "place" && f.size() >= 3) plan.places.push_back({{num(1), num(2)}, f.size() > 3 ? f[3] : ""});
    else if (k == "marker" && f.size() >= 3) plan.markers.push_back({{num(1), num(2)}, f.size() > 3 ? f[3] : ""});
    else if (k == "score" && f.size() >= 4) {
      Plan::Score sc;
      sc.vp = static_cast<int>(num(1));
      sc.rule = f[2];
      const bool hasArg = sc.rule == "AT_LEAST" || sc.rule == "KIND";
      sc.arg = hasArg && f.size() > 3 ? f[3] : "";
      sc.text = f.size() > (hasArg ? 4u : 3u) ? f[hasArg ? 4 : 3] : "";
      plan.scores.push_back(sc);
    }
    else if (k == "terrain" && f.size() >= 3) {
      Terrain t; t.look = f[1]; t.poly = parsePoints(f, 2); plan.terrain.push_back(t);
    }
    else if (k == "ruin" && f.size() >= 3) {
      Terrain t; t.look = "ruin"; t.label = f[1]; t.poly = parsePoints(f, 2);
      if (t.poly.size() >= 3) plan.terrain.push_back(t);
    }
    else if (k == "system" && f.size() >= 2) plan.system = f[1] == "40k" || f[1] == "40K" ? "40k" : "aos";
    else if (k == "scorefrom" && f.size() >= 2) plan.scoreFrom = std::max(1, static_cast<int>(num(1)));
    else if (k == "scoreat" && f.size() >= 2) plan.scoreAtCommand = f[1] == "command" || f[1] == "COMMAND";
  }
  // 40k: every round objective is a 40mm marker, with control measured from its edge.
  if (is40k(plan)) for (auto& o : plan.objectives) if (o.poly.empty()) o.marker = k40kMarkerRadius;
  return plan;
}

struct CatalogPlan { const char* slug; const char* title; std::string pack, system; };
const std::vector<CatalogPlan>& planCatalog() {
  static const auto catalog = [] {
    std::vector<CatalogPlan> result;
    for (const auto& b : kBuiltinPlans) {
      const Plan parsed = parsePlan(b.slug, b.text);
      result.push_back({b.slug, b.title, parsed.pack, parsed.system});
    }
    return result;
  }();
  return catalog;
}

// Scale a 44" x 30" map to the 60" x 44" battlefield its rules recommend:
// every position in proportion (60/44 across, 44/30 down). Ranges stay 3".
// Both ways: plans are kept at 60 x 44 (the book's grid), and the smaller
// board option scales them down the same way.
void fitBoard(Plan& plan, bool board60) {
  if (!plan.pack.empty()) return; // printed Spearhead geometry stays 30 x 22
  if (is40k(plan)) return;        // a 40k plan is drawn at the size its card gives
  double kx = 0, ky = 0, nw = 0, nh = 0;
  if (board60 && plan.w == 44.0 && plan.h == 30.0) { kx = 60.0 / 44.0; ky = 44.0 / 30.0; nw = 60; nh = 44; }
  else if (!board60 && plan.w == 60.0 && plan.h == 44.0) { kx = 44.0 / 60.0; ky = 30.0 / 44.0; nw = 44; nh = 30; }
  else return;
  auto fit = [&](Pt& p) { p.x *= kx; p.y *= ky; };
  for (auto& o : plan.objectives) { fit(o.p); for (auto& p : o.poly) fit(p); }
  for (auto& l : plan.lines) { fit(l.a); fit(l.b); }
  for (auto* zones : {&plan.territories, &plan.deployments}) for (auto& z : *zones) for (auto& p : z.poly) fit(p);
  for (auto& pl : plan.places) fit(pl.p);
  for (auto& m : plan.markers) fit(m.p);
  for (auto& t : plan.terrain) for (auto& p : t.poly) fit(p);
  plan.w = nw; plan.h = nh;
}

// A file of the same name in <state>/tabletop overrides a built-in plan (so
// traced terrain and corrections survive); anything else there is a plan of
// its own.
fs::path dir40k(const char* kind) { return planDir() / "40k" / kind; }

// The .txt files in a folder, by name, sorted.
std::vector<std::string> listPlans(const fs::path& dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir, ec))
    if (e.path().extension() == ".txt") out.push_back(e.path().stem().string());
  std::sort(out.begin(), out.end());
  return out;
}

bool readFile(const fs::path& path, std::string& out) {
  std::ifstream in(path);
  if (!in) return false;
  std::stringstream text;
  text << in.rdbuf();
  out = text.str();
  return true;
}

// A 40k mission, put together from the owner's three files:
// "mission:<deployment>|<primary>|<rule>" (the rule may be "-").
bool composeMission(const std::string& name, Plan& out, std::string& why) {
  std::string spec = name.substr(8), parts[3];
  for (int i = 0; i < 3; ++i) {
    const auto bar = spec.find('|');
    parts[i] = spec.substr(0, bar);
    spec = bar == std::string::npos ? "" : spec.substr(bar + 1);
  }
  std::string deployment, primary, rule;
  if (!readFile(dir40k("deployments") / (parts[0] + ".txt"), deployment)) { why = "no deployment named " + parts[0]; return false; }
  if (!readFile(dir40k("primaries") / (parts[1] + ".txt"), primary)) { why = "no primary named " + parts[1]; return false; }
  if (!parts[2].empty() && parts[2] != "-" && !readFile(dir40k("rules") / (parts[2] + ".txt"), rule)) {
    why = "no mission rule named " + parts[2]; return false;
  }
  out = parsePlan(name, std::string("system") + '\t' + "40k" + '\n' + deployment + '\n' + primary);
  out.system = "40k";
  for (auto& o : out.objectives) if (o.poly.empty()) o.marker = k40kMarkerRadius;
  const Plan ruleText = parsePlan(parts[2], rule);
  out.twist = rule.empty() ? "" : (ruleText.title == parts[2] ? parts[2] : ruleText.title);
  const Plan primaryText = parsePlan(parts[1], primary);
  out.title = (primaryText.title == parts[1] ? parts[1] : primaryText.title) + " / " + parts[0];
  return true;
}

bool loadPlan(const std::string& name, Plan& out, std::string& why) {
  if (name.rfind("mission:", 0) == 0) return composeMission(name, out, why);
  std::ifstream in(planDir() / (name + ".txt"));
  const BuiltinPlan* builtin = nullptr;
  for (const auto& b : kBuiltinPlans) if (name == b.slug) builtin = &b;
  if (in) {
    std::stringstream text;
    text << in.rdbuf();
    out = parsePlan(name, text.str());
    if (builtin && out.title == name) out.title = builtin->title;
    return true;
  }
  if (builtin) {
    out = parsePlan(name, builtin->text);
    out.title = builtin->title;
    return true;
  }
  why = "no plan named " + name;
  return false;
}

// A terrain layout laid over the plan: its ruins replace the plan's terrain.
bool applyLayout(Plan& plan, const std::string& layout, std::string& why) {
  if (layout.empty()) return true;
  std::string text;
  if (!readFile(dir40k("layouts") / (layout + ".txt"), text)) { why = "no layout named " + layout; return false; }
  plan.terrain = parsePlan(layout, text).terrain;
  return true;
}

// Load, fit to the board and lay the chosen terrain layout over it.
bool loadAndFit(const std::string& name, Plan& out, std::string& why) {
  if (!loadPlan(name, out, why)) return false;
  fitBoard(out, g_game.board60);
  if (is40k(out)) applyLayout(out, g_game.layout, why);
  return true;
}

// Each system's own default sides; colours the owner picked are left alone.
void adoptSystemColours(const Plan& plan, Game& g) {
  auto same = [](Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; };
  if (is40k(plan) && same(g.c1, kP1) && same(g.c2, kP2)) { g.c1 = k40kP1; g.c2 = k40kP2; }
  if (!is40k(plan) && same(g.c1, k40kP1) && same(g.c2, k40kP2)) { g.c1 = kP1; g.c2 = kP2; }
}

// ── drawing ────────────────────────────────────────────────────────────────

struct Canvas {
  std::uint8_t* px; int w, h;
  double s, ox, oy;   // inches -> pixels
  int X(double inches) const { return static_cast<int>(std::lround(ox + inches * s)); }
  int Y(double inches) const { return static_cast<int>(std::lround(oy + inches * s)); }
  void blend(int x, int y, int r, int g, int b, double a) {
    if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.0) return;
    a = std::min(1.0, a);
    std::uint8_t* p = px + (static_cast<std::size_t>(y) * w + x) * 4;
    p[0] = static_cast<std::uint8_t>(p[0] + (r - p[0]) * a);
    p[1] = static_cast<std::uint8_t>(p[1] + (g - p[1]) * a);
    p[2] = static_cast<std::uint8_t>(p[2] + (b - p[2]) * a);
    p[3] = 255;
  }
};



bool inside(const std::vector<Pt>& poly, double x, double y) {
  bool in = false;
  for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    if ((poly[i].y > y) != (poly[j].y > y) &&
        x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
  }
  return in;
}

template <typename Shade>
void fillPoly(Canvas& c, const std::vector<Pt>& poly, Shade shade) {
  if (poly.size() < 3) return;
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (const auto& p : poly) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
  const int px0 = std::max(0, c.X(x0)), px1 = std::min(c.w - 1, c.X(x1));
  const int py0 = std::max(0, c.Y(y0)), py1 = std::min(c.h - 1, c.Y(y1));
  // Rows across the cores: a territory covers half the table, and with a
  // shimmer on it that was most of a frame on one core.
  parallelRows(py1 - py0 + 1, [&](int firstRow, int lastRow) {
    for (int y = py0 + firstRow; y < py0 + lastRow; ++y) {
      const double iy = (y + 0.5 - c.oy) / c.s;
      for (int x = px0; x <= px1; ++x) {
        const double ix = (x + 0.5 - c.ox) / c.s;
        if (inside(poly, ix, iy)) shade(x, y, ix, iy);
      }
    }
  });
}

// fillPoly for something soft (a shimmer, a texture): the colour is worked out
// on a raster a quarter the size, then smoothed up and drawn only inside the
// shape. Per-pixel noise over half a table was most of a frame; this is 1/16.
template <typename Look>
void fillPolySoft(Canvas& c, const std::vector<Pt>& poly, Look look) {
  if (poly.size() < 3) return;
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (const auto& p : poly) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
  const int px0 = std::max(0, c.X(x0)), px1 = std::min(c.w - 1, c.X(x1));
  const int py0 = std::max(0, c.Y(y0)), py1 = std::min(c.h - 1, c.Y(y1));
  if (px1 < px0 || py1 < py0) return;
  constexpr int k = 4;
  const int sw = (px1 - px0) / k + 2, sh = (py1 - py0) / k + 2;
  std::vector<float> small(static_cast<std::size_t>(sw) * sh * 4);
  parallelRows(sh, [&](int firstRow, int lastRow) {
    for (int sy = firstRow; sy < lastRow; ++sy) {
      for (int sx = 0; sx < sw; ++sx) {
        Rgb col; double a;
        look((px0 + sx * k - c.ox) / c.s, (py0 + sy * k - c.oy) / c.s, col, a);
        float* q = &small[(static_cast<std::size_t>(sy) * sw + sx) * 4];
        q[0] = static_cast<float>(col.r); q[1] = static_cast<float>(col.g); q[2] = static_cast<float>(col.b);
        q[3] = static_cast<float>(a);
      }
    }
  });
  parallelRows(py1 - py0 + 1, [&](int firstRow, int lastRow) {
    for (int y = py0 + firstRow; y < py0 + lastRow; ++y) {
      const double iy = (y + 0.5 - c.oy) / c.s;
      const double fy = static_cast<double>(y - py0) / k;
      const int sy = std::min(sh - 2, static_cast<int>(fy));
      const double ty = fy - sy;
      for (int x = px0; x <= px1; ++x) {
        if (!inside(poly, (x + 0.5 - c.ox) / c.s, iy)) continue;
        const double fx = static_cast<double>(x - px0) / k;
        const int sx = std::min(sw - 2, static_cast<int>(fx));
        const double tx = fx - sx;
        float v[4];
        for (int ch = 0; ch < 4; ++ch) {
          const auto at = [&](int ax, int ay) { return small[(static_cast<std::size_t>(ay) * sw + ax) * 4 + ch]; };
          v[ch] = static_cast<float>((at(sx, sy) * (1 - tx) + at(sx + 1, sy) * tx) * (1 - ty) +
                                     (at(sx, sy + 1) * (1 - tx) + at(sx + 1, sy + 1) * tx) * ty);
        }
        c.blend(x, y, static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]), v[3]);
      }
    }
  });
}

// A segment of thickness `t` pixels; `dash` > 0 draws it dashed (in pixels).
void segment(Canvas& c, double ax, double ay, double bx, double by, double t, Rgb col, double a, double dash = 0, double phase = 0) {
  const double x0 = std::min(ax, bx) - t, x1 = std::max(ax, bx) + t;
  const double y0 = std::min(ay, by) - t, y1 = std::max(ay, by) + t;
  const double dx = bx - ax, dy = by - ay, len2 = std::max(1e-9, dx * dx + dy * dy), len = std::sqrt(len2);
  for (int y = std::max(0, static_cast<int>(y0)); y <= std::min(c.h - 1, static_cast<int>(y1)); ++y) {
    for (int x = std::max(0, static_cast<int>(x0)); x <= std::min(c.w - 1, static_cast<int>(x1)); ++x) {
      const double u = std::clamp(((x - ax) * dx + (y - ay) * dy) / len2, 0.0, 1.0);
      const double ex = ax + u * dx - x, ey = ay + u * dy - y;
      const double d = std::sqrt(ex * ex + ey * ey);
      if (d > t / 2 + 0.7) continue;
      if (dash > 0 && std::fmod(u * len + phase + dash * 1000, dash * 2) > dash) continue;
      c.blend(x, y, col.r, col.g, col.b, a * std::clamp(t / 2 + 0.7 - d, 0.0, 1.0));
    }
  }
}

void outline(Canvas& c, const std::vector<Pt>& poly, double t, Rgb col, double a, double dash = 0, double phase = 0) {
  for (std::size_t i = 0; i < poly.size(); ++i) {
    const Pt& p = poly[i]; const Pt& q = poly[(i + 1) % poly.size()];
    segment(c, c.X(p.x), c.Y(p.y), c.X(q.x), c.Y(q.y), t, col, a, dash, phase);
  }
}

// A ring of radius r (pixels) and thickness t, with an optional soft glow.
// segments > 0 draws that many arcs (duty: how much of each is lit), turned by rot.
void ring(Canvas& c, double cx, double cy, double r, double t, Rgb col, double a, double glow,
          int segments = 0, double rot = 0, double duty = 0.5) {
  // Only the pixels in the band: a ring is thin, and visiting its whole
  // bounding square (the empty middle included, with an exp() per pixel for
  // the glow) was most of a frame with five objectives on the table.
  const double reach = t / 2 + 0.7 + (glow > 0 ? glow * 1.5 : 0.0);
  const double outer = r + reach, inner = std::max(0.0, r - reach);
  const int y0 = std::max(0, static_cast<int>(std::floor(cy - outer)));
  const int y1 = std::min(c.h - 1, static_cast<int>(std::ceil(cy + outer)));
  for (int y = y0; y <= y1; ++y) {
    const double dy = y - cy;
    const double xo = std::sqrt(std::max(0.0, outer * outer - dy * dy));
    const double xi = std::abs(dy) < inner ? std::sqrt(inner * inner - dy * dy) : 0.0;
    // Two spans when the row crosses the hole, one when it does not.
    const double spans[2][2] = {{cx - xo, cx - xi}, {cx + xi, cx + xo}};
    for (int k = 0; k < (xi > 0 ? 2 : 1); ++k) {
      const double from = xi > 0 ? spans[k][0] : cx - xo, to = xi > 0 ? spans[k][1] : cx + xo;
      for (int x = std::max(0, static_cast<int>(std::floor(from))); x <= std::min(c.w - 1, static_cast<int>(std::ceil(to))); ++x) {
        const double d = std::abs(std::hypot(x - cx, dy) - r);
        if (d > reach) continue;
        if (segments > 0) {
          const double span = 6.283185307179586 / segments;
          const double ang = std::atan2(dy, x - cx) - rot + 6.283185307179586 * 100;
          if (std::fmod(ang, span) > span * duty) continue;
        }
        double v = std::clamp(t / 2 + 0.7 - d, 0.0, 1.0);
        if (glow > 0 && d > t / 2) v = std::max(v, 0.45 * std::exp(-(d - t / 2) / (glow / 3)));
        c.blend(x, y, col.r, col.g, col.b, a * v);
      }
    }
  }
}

// 5x7 capitals, digits and a little punctuation.
const std::array<std::uint8_t, 7>* glyph(char ch) {
  struct G { char c; std::array<std::uint8_t, 7> rows; };
  static const G font[] = {
    {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},{'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},{'3',{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},{'5',{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6',{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},{'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},{'9',{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'A',{0x0E,0x11,0x11,0x11,0x1F,0x11,0x11}},{'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},{'D',{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}},
    {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},{'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G',{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}},{'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I',{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},{'J',{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}},{'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},{'N',{0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
    {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},{'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},{'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},{'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},{'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W',{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},{'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y',{0x11,0x11,0x11,0x0A,0x04,0x04,0x04}},{'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'-',{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},{':',{0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}},
    {'.',{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},{'"',{0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}},
    {'/',{0x01,0x01,0x02,0x04,0x08,0x10,0x10}},{',',{0x00,0x00,0x00,0x00,0x0C,0x04,0x08}},
    {'+',{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}},
    {'!',{0x04,0x04,0x04,0x04,0x04,0x00,0x04}},{'\x27',{0x04,0x04,0x08,0x00,0x00,0x00,0x00}},
    {'|',{0x04,0x04,0x04,0x04,0x04,0x04,0x04}},{'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}},
    {')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
  };
  const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  for (const auto& g : font) if (g.c == up) return &g.rows;
  return nullptr;
}

double textWidth(const std::string& s, double cell) { return s.size() * 6 * cell - cell; }

// A dark plate under a line of text, so it reads over whatever it crosses.
void plate(Canvas& c, const std::string& s, double cx, double cy, double cell, double a) {
  const double w = textWidth(s, cell) + cell * 4, h = 7 * cell + cell * 3;
  for (int y = static_cast<int>(cy - h / 2); y < static_cast<int>(cy + h / 2); ++y)
    for (int x = static_cast<int>(cx - w / 2); x < static_cast<int>(cx + w / 2); ++x) c.blend(x, y, 0, 0, 0, a);
}

// Text centred on (cx, cy), `cell` pixels per font pixel; flipped turns it
// 180 degrees, for the player across the table.
void text(Canvas& c, const std::string& s, double cx, double cy, double cell, Rgb col, double a, bool flipped = false) {
  const double w = textWidth(s, cell), h = 7 * cell;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const auto* g = glyph(s[i]);
    if (!g) continue;
    for (int row = 0; row < 7; ++row) {
      for (int col5 = 0; col5 < 5; ++col5) {
        if (!((*g)[row] & (0x10 >> col5))) continue;
        double gx = -w / 2 + (i * 6 + col5) * cell, gy = -h / 2 + row * cell;
        if (flipped) { gx = -gx - cell; gy = -gy - cell; }
        for (int yy = 0; yy < static_cast<int>(std::ceil(cell)); ++yy)
          for (int xx = 0; xx < static_cast<int>(std::ceil(cell)); ++xx)
            c.blend(static_cast<int>(cx + gx) + xx, static_cast<int>(cy + gy) + yy, col.r, col.g, col.b, a);
      }
    }
  }
}

// Smooth value noise, for the terrain looks.
double hash2(int x, int y) {
  std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0;
}
double noise(double x, double y) {
  const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
  const double fx = x - xi, fy = y - yi;
  const double u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
  const double a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
double fbm(double x, double y) { return 0.55 * noise(x, y) + 0.3 * noise(x * 2.1, y * 2.1) + 0.15 * noise(x * 4.3, y * 4.3); }

// What a piece of terrain looks like, at (x, y) inches, time t.
void terrainLook(const std::string& look, double x, double y, Pt centre, double t, bool animate, Rgb& col, double& a) {
  if (!animate) t = 0;
  if (look == "mist") {
    const double n = fbm(x * 0.5 + t * 0.12, y * 0.5 + t * 0.05);
    col = {210, 220, 230}; a = 0.10 + 0.35 * n * n;
  } else if (look == "embers") {
    const double n = fbm(x * 0.9, y * 0.9 - t * 0.7);
    const double flick = 0.75 + 0.25 * noise(x * 3 + t * 6, y * 3);
    col = {255, static_cast<int>(70 + 110 * n), 20}; a = std::pow(n, 2.2) * flick * 0.9;
  } else if (look == "water") {
    const double v = std::sin(x * 2.0 + t * 1.6) + std::sin(y * 2.3 - t * 1.3) + std::sin((x + y) * 1.4 + t);
    col = {60, 150, 230}; a = 0.18 + 0.12 * (v / 3.0);
  } else if (look == "arcane") {
    // Rings pulsing out from the middle of the piece.
    const double v = 0.5 + 0.5 * std::sin(std::hypot(x - centre.x, y - centre.y) * 3 - t * 2.5);
    col = {170, 90, 255}; a = 0.08 + 0.3 * v * v;
  } else if (look == "forest") {
    const double n = fbm(x * 1.3 + 0.25 * std::sin(t * 0.8 + y), y * 1.3);
    col = {60, 170, 70}; a = 0.08 + 0.3 * n;
  } else {
    col = {200, 200, 200}; a = 0.06;
  }
}

// What the open ground looks like at (x, y) inches, time t. Kept dim: it is the
// floor everything else stands on.
void groundLook(const std::string& kind, double x, double y, double t, Rgb& col, double& a) {
  if (kind == "grass") {
    // Gusts rolling across, over a patchy sward.
    const double base = fbm(x * 0.35, y * 0.35);
    const double gust = 0.5 + 0.5 * std::sin(x * 0.45 + y * 0.18 - t * 1.4 + 2.0 * noise(x * 0.1, y * 0.1));
    const double blades = noise(x * 3.0 + std::sin(t * 1.4 + y * 0.4) * 0.6, y * 6.0);
    col = {static_cast<int>(40 + 50 * gust), static_cast<int>(110 + 70 * base + 40 * gust), 40};
    a = 0.22 + 0.18 * base + 0.12 * gust * blades;
  } else if (kind == "swamp") {
    // Murk that drifts, with bubbles that rise and pop.
    const double murk = fbm(x * 0.3 + t * 0.03, y * 0.3 - t * 0.02);
    double bubble = 0.0;
    const int cx = static_cast<int>(std::floor(x / 2.5)), cy = static_cast<int>(std::floor(y / 2.5));
    const double phase = std::fmod(t * 0.35 + hash2(cx, cy) * 10.0, 1.0);
    const double bx = (cx + 0.2 + 0.6 * hash2(cx + 7, cy)) * 2.5, by = (cy + 0.2 + 0.6 * hash2(cx, cy + 7)) * 2.5;
    const double d = std::hypot(x - bx, y - by), rad = 0.15 + 0.35 * phase;
    if (hash2(cx + 3, cy + 5) > 0.55 && std::abs(d - rad) < 0.06) bubble = (1.0 - phase) * 0.8;
    col = {static_cast<int>(50 + 40 * murk), static_cast<int>(80 + 50 * murk), 40};
    a = 0.22 + 0.25 * murk + bubble * 0.5;
  } else if (kind == "ash") {
    // Grey ash drifting on the wind, the odd ember glinting in it.
    const double drift = fbm(x * 0.4 - t * 0.25, y * 0.4 + t * 0.05);
    const double glint = std::pow(std::max(0.0, noise(x * 2.5 - t * 0.8, y * 2.5 + t * 0.3) - 0.78) / 0.22, 2.0);
    col = {static_cast<int>(110 + 90 * glint + 30 * drift), static_cast<int>(100 + 20 * glint + 25 * drift), static_cast<int>(95 - 40 * glint)};
    a = 0.14 + 0.2 * drift + 0.5 * glint;
  } else if (kind == "lava") {
    // Dark crust split by cracks that glow and breathe.
    const double n = fbm(x * 0.28, y * 0.28);
    const double crack = std::pow(std::max(0.0, 1.0 - std::abs(n - 0.5) * 24.0), 1.5);
    const double breathe = 0.6 + 0.4 * std::sin(t * 1.1 + n * 9.0);
    col = {255, static_cast<int>(70 + 110 * crack * breathe), 20};
    a = 0.05 + 0.45 * crack * breathe;
  } else if (kind == "snow") {
    // Drifts, and snow blowing across them.
    const double drift = fbm(x * 0.25, y * 0.25);
    // Streaks: stretched along the wind, so it reads as snow blowing, not static.
    const double blow = std::pow(fbm(x * 0.6 - t * 1.0, y * 2.4 + t * 0.15), 3.0);
    col = {225, 232, 245};
    a = 0.12 + 0.2 * drift + 0.35 * blow;
  } else if (kind == "sand") {
    // Dune ripples that creep, over warmer and cooler patches.
    const double ripple = 0.5 + 0.5 * std::sin(y * 2.2 + 1.5 * noise(x * 0.3, y * 0.3) * 6.0 - t * 0.4);
    const double tone = fbm(x * 0.2, y * 0.2);
    col = {static_cast<int>(190 + 40 * tone), static_cast<int>(150 + 30 * tone), 90};
    a = 0.14 + 0.16 * ripple;
  } else {
    a = 0.0; col = {0, 0, 0};
  }
}

// The ground is all detail and no edges, so it is drawn at a quarter of the
// size and smoothed back up (the "a smear can be computed small" rule).
void drawGround(Canvas& c, const Plan& plan, const std::string& kind, double t, double level) {
  const int x0 = std::max(0, c.X(0)), x1 = std::min(c.w, c.X(plan.w));
  const int y0 = std::max(0, c.Y(0)), y1 = std::min(c.h, c.Y(plan.h));
  constexpr int k = 4;
  const int sw = (x1 - x0) / k + 2, sh = (y1 - y0) / k + 2;
  std::vector<float> small(static_cast<std::size_t>(sw) * sh * 4);
  parallelRows(sh, [&](int firstRow, int lastRow) {
  for (int sy = firstRow; sy < lastRow; ++sy) {
    for (int sx = 0; sx < sw; ++sx) {
      const double ix = (x0 + sx * k - c.ox) / c.s, iy = (y0 + sy * k - c.oy) / c.s;
      Rgb col; double a;
      groundLook(kind, ix, iy, t, col, a);
      float* q = &small[(static_cast<std::size_t>(sy) * sw + sx) * 4];
      q[0] = static_cast<float>(col.r); q[1] = static_cast<float>(col.g); q[2] = static_cast<float>(col.b);
      q[3] = static_cast<float>(std::clamp(a * level, 0.0, 1.0));
    }
  }
  });
  parallelRows(y1 - y0, [&](int firstRow, int lastRow) {
  for (int y = y0 + firstRow; y < y0 + lastRow; ++y) {
    const double fy = static_cast<double>(y - y0) / k;
    const int sy = std::min(sh - 2, static_cast<int>(fy));
    const double ty = fy - sy;
    for (int x = x0; x < x1; ++x) {
      const double fx = static_cast<double>(x - x0) / k;
      const int sx = std::min(sw - 2, static_cast<int>(fx));
      const double tx = fx - sx;
      float v[4];
      for (int ch = 0; ch < 4; ++ch) {
        const auto at = [&](int ax, int ay) { return small[(static_cast<std::size_t>(ay) * sw + ax) * 4 + ch]; };
        v[ch] = static_cast<float>((at(sx, sy) * (1 - tx) + at(sx + 1, sy) * tx) * (1 - ty) +
                                   (at(sx, sy + 1) * (1 - tx) + at(sx + 1, sy + 1) * tx) * ty);
      }
      c.blend(x, y, static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]), v[3]);
    }
  }
  });
}

void drawCalibration(Canvas& c, const Plan& plan) {
  const double tick = std::max(1.0, c.s * 0.05);
  // 6" grid, faint, and every inch along the edges.
  for (double x = 0; x <= plan.w + 1e-6; x += 6) segment(c, c.X(x), c.Y(0), c.X(x), c.Y(plan.h), tick, kWhite, 0.25);
  for (double y = 0; y <= plan.h + 1e-6; y += 6) segment(c, c.X(0), c.Y(y), c.X(plan.w), c.Y(y), tick, kWhite, 0.25);
  for (int i = 0; i <= static_cast<int>(plan.w); ++i) {
    const double len = (i % 6 == 0 ? 0.9 : 0.4) * c.s;
    segment(c, c.X(i), c.Y(0), c.X(i), c.Y(0) + len, tick, kWhite, 0.9);
    segment(c, c.X(i), c.Y(plan.h), c.X(i), c.Y(plan.h) - len, tick, kWhite, 0.9);
  }
  for (int i = 0; i <= static_cast<int>(plan.h); ++i) {
    const double len = (i % 6 == 0 ? 0.9 : 0.4) * c.s;
    segment(c, c.X(0), c.Y(i), c.X(0) + len, c.Y(i), tick, kWhite, 0.9);
    segment(c, c.X(plan.w), c.Y(i), c.X(plan.w) - len, c.Y(i), tick, kWhite, 0.9);
  }
  // Crosshairs at every 12" crossing inside the table, each labelled with
  // where it should be: lay a tape on them.
  const double cell = std::max(1.0, c.s * 0.07);
  for (int x = 12; x < static_cast<int>(plan.w); x += 12) {
    for (int y = 12; y < static_cast<int>(plan.h); y += 12) {
      const double cx = c.X(x), cy = c.Y(y), arm = c.s * 0.75;
      segment(c, cx - arm, cy, cx + arm, cy, tick * 1.5, kGold, 1.0);
      segment(c, cx, cy - arm, cx, cy + arm, tick * 1.5, kGold, 1.0);
      text(c, std::to_string(x) + "\" " + std::to_string(y) + "\"", cx, cy + c.s * 1.2, cell, kGold, 1.0);
    }
  }
  outline(c, {{0, 0}, {plan.w, 0}, {plan.w, plan.h}, {0, plan.h}}, tick * 2, kWhite, 1.0);
  text(c, "CALIBRATE " + std::to_string(static_cast<int>(plan.w)) + "\" X " + std::to_string(static_cast<int>(plan.h)) + "\"",
       c.X(plan.w / 2), c.Y(plan.h / 2), cell * 1.6, kWhite, 0.9);
}

// Seconds on a player's clock now, counting the stretch that is running.
double clockSeconds(const Game& g, int player) {
  double t = player == 2 ? g.clock2 : g.clock1;
  if (g.clockOn && !g.clockPaused && g.turn == player)
    t += std::chrono::duration<double>(Clock::now() - g.clockSince).count();
  return t;
}
std::string clockText(double seconds) {
  const int s = static_cast<int>(seconds);
  char out[16];
  std::snprintf(out, sizeof(out), "%d:%02d", s / 60, s % 60);
  return out;
}
// Bank the running stretch (before the turn changes, or a pause).
void bankClock(Game& g) {
  if (g.clockOn && !g.clockPaused) {
    const auto now = Clock::now();
    (g.turn == 2 ? g.clock2 : g.clock1) += std::chrono::duration<double>(now - g.clockSince).count();
    g.clockSince = now;
  }
}

// The automatic part of a turn's scoring, for the player whose turn it was.
int autoScoreFor(const Plan& plan, int player) {
  int mine = 0, theirs = 0;
  for (const auto& o : plan.objectives) if (!o.muted) { if (o.owner == player) ++mine; else if (o.owner) ++theirs; }
  int vp = 0;
  for (const auto& sc : plan.scores) {
    if (sc.rule == "AT_LEAST" && mine >= std::atoi(sc.arg.c_str())) vp += sc.vp;
    else if (sc.rule == "MORE" && mine > theirs) vp += sc.vp;
    else if (sc.rule == "KIND") {
      for (const auto& o : plan.objectives) if (!o.muted && o.owner == player && o.kind == sc.arg) { vp += sc.vp; break; }
    }
  }
  return vp;
}

// The end of the current player's turn: bank its points (when auto-scoring is
// on) before the turn moves on. Recorded, so PHASE BACK can undo it.
void scoreNow(const Plan& plan, Game& g) {
  if (!g.autoScore || plan.scores.empty() || g.round < plan.scoreFrom) return;
  int& total = g.turn == 2 ? g.vp2 : g.vp1;
  const int before = total;
  total = std::min(vpCap(plan), total + autoScoreFor(plan, g.turn));
  // Recorded as what was actually added, so the cap is undone exactly too.
  g.awarded[{g.round, g.turn}] = total - before;
  g.lastScore = (g.turn == 2 ? plan.p2 : plan.p1) + " SCORES " + std::to_string(total - before) + " VP";
  g.scoredAt = Clock::now();
}
void endTurn(const Plan& plan, Game& g) {
  if (!plan.scoreAtCommand) scoreNow(plan, g);
}

// On to the next player's turn (or the next round). A 40k game ends after
// the fifth round. A primary scored at the start of a Command phase is
// scored here, once the new turn has begun.
void advanceTurn(const Plan& plan, Game& g) {
  bankClock(g);
  endTurn(plan, g);
  if (is40k(plan) && g.round >= 5 && g.turn == 2) {
    g.over = true;
    g.phase = phaseCount(plan) - 1;
    g.lastScore = "GAME OVER";
    g.scoredAt = Clock::now();
    return;
  }
  g.phase = 0;
  if (g.turn == 1) g.turn = 2; else { g.turn = 1; ++g.round; }
  if (plan.scoreAtCommand) scoreNow(plan, g);
}

// Going back into a turn whose end was scored: take the award back off.
void unscoreTurn(Game& g, int round, int turn) {
  auto it = g.awarded.find({round, turn});
  if (it == g.awarded.end()) return;
  int& vp = turn == 2 ? g.vp2 : g.vp1;
  vp = std::max(0, vp - it->second);
  g.awarded.erase(it);
}

Rgb scaled(Rgb c, double k) { return {static_cast<int>(c.r * k), static_cast<int>(c.g * k), static_cast<int>(c.b * k)}; }

void renderTable(std::uint8_t* rgba, int w, int h, double t) {
  Plan plan;
  Game game;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    plan = g_plan;
    game = g_game;
  }
  std::fill(rgba, rgba + static_cast<std::size_t>(w) * h * 4, std::uint8_t {0});
  for (std::size_t i = 3; i < static_cast<std::size_t>(w) * h * 4; i += 4) rgba[i] = 255;
  Canvas c {rgba, w, h, 1, 0, 0};
  c.s = std::min(w / plan.w, h / plan.h);
  c.ox = (w - plan.w * c.s) / 2;
  c.oy = (h - plan.h * c.s) / 2;
  if (game.calibrate) { drawCalibration(c, plan); return; }

  // One clock for every animation, scaled by SPEED (0 holds it all still).
  const double ts = t * game.speed;
  const double B = std::clamp(game.bright, 0.1, 1.0);
  const Rgb c1 = scaled(game.c1, B), c2 = scaled(game.c2, B), hlc = scaled(game.hl, B), pc = scaled(game.place, B);
  const Rgb white = scaled(kWhite, B), grey = scaled({200, 200, 200}, B);
  auto side = [&](int p) { return p == 2 ? c2 : c1; };
  const bool arcane = game.style == 1, ember = game.style == 2;
  const double line = std::max(1.5, c.s * 0.08);
  const double cell = std::max(1.0, c.s * 0.11);

  if (game.ground != "none") drawGround(c, plan, game.ground, ts, game.groundLevel * B);
  if (game.showEdge && !game.setup) {
    // THE EDGE: a band of light running round the table in the phase's
    // colour, dashes chasing clockwise, brighter where they meet the corners'
    // glow. A new phase reads across the room at a glance.
    const Phase& ph = phaseAt(plan, game.phase);
    const Rgb col = scaled(ph.colour, B);
    const double band = c.s * 0.35, dash = c.s * 1.5;
    const double run = ts * c.s * 2.5;
    const std::vector<Pt> rim {{0.15, 0.15}, {plan.w - 0.15, 0.15}, {plan.w - 0.15, plan.h - 0.15}, {0.15, plan.h - 0.15}};
    outline(c, rim, band * 0.35, col, 0.35);
    outline(c, rim, band, col, 0.9, dash, -run);
  }
  if (game.showTerritories) {
    for (const auto& z : plan.territories) {
      if (z.muted) continue;
      const Rgb col = side(z.player);
      if (arcane || ember) {
        // Arcane: a slow shimmer through the ground. Ember: heat rising.
        fillPolySoft(c, z.poly, [&](double ix, double iy, Rgb& out, double& a) {
          out = col;
          a = 0.10 + (arcane ? 0.05 * (fbm(ix * 0.35 + ts * 0.08, iy * 0.35 - ts * 0.05) - 0.5)
                             : 0.06 * (fbm(ix * 0.6, iy * 0.6 + ts * 0.3) - 0.5));
        });
      } else {
        fillPoly(c, z.poly, [&](int x, int y, double, double) { c.blend(x, y, col.r, col.g, col.b, 0.10); });
      }
      // The edge of a territory flows, in its owner's colour.
      outline(c, z.poly, line, col, 0.55, c.s * 0.8, ts * c.s * 1.2 * (z.player == 2 ? -1 : 1));
    }
  }
  if (game.showGuides) for (const auto& l : plan.lines) if (!l.muted)
    segment(c, c.X(l.a.x), c.Y(l.a.y), c.X(l.b.x), c.Y(l.b.y), line, white, 0.45, c.s * 0.6);
  if (game.showDeployment) {
    for (const auto& z : plan.deployments) {
      if (z.muted) continue;
      const Rgb col = side(z.player);
      fillPoly(c, z.poly, [&](int x, int y, double, double) { c.blend(x, y, col.r, col.g, col.b, 0.16); });
      outline(c, z.poly, line, col, 0.7);
    }
  }
  if (game.showTerrain) {
    for (const auto& tr : plan.terrain) {
      if (tr.muted) continue;
      Pt centre;
      for (const auto& p : tr.poly) { centre.x += p.x / tr.poly.size(); centre.y += p.y / tr.poly.size(); }
      fillPolySoft(c, tr.poly, [&](double ix, double iy, Rgb& col, double& a) {
        terrainLook(tr.look, ix, iy, centre, ts, game.animate, col, a);
        col = scaled(col, B);
      });
      outline(c, tr.poly, line * (tr.look == "ruin" ? 1.4 : 1.0), grey, tr.look == "ruin" ? 0.8 : 0.55);
      if (!tr.label.empty()) text(c, tr.label, c.X(centre.x), c.Y(centre.y), cell * 1.4, grey, 0.85);
    }
    // Where each piece of terrain goes: a breathing dashed circle.
    for (const auto& m : plan.markers) {
      if (m.muted) continue;
      const double breathe = 0.45 + 0.2 * std::sin(ts * 1.3 + m.p.x);
      ring(c, c.X(m.p.x), c.Y(m.p.y), c.s * 1.0, line * 0.8, grey, breathe, 0, 12, ts * 0.2, 0.55);
      if (game.showLabels && !m.label.empty()) text(c, m.label, c.X(m.p.x), c.Y(m.p.y) + c.s * 1.5, cell * 0.6, grey, 0.75);
    }
  }
  if (game.showPlaces) {
    for (const auto& p : plan.places) {
      if (p.muted) continue;
      // A diamond that turns, with a glow that breathes.
      const double spin = ts * 0.6, r = 0.7;
      std::vector<Pt> diamond;
      for (int k = 0; k < 4; ++k) {
        const double ang = spin + k * 1.5707963267948966;
        diamond.push_back({p.p.x + r * std::cos(ang), p.p.y + r * std::sin(ang)});
      }
      const double glow = 0.5 + 0.5 * std::sin(ts * 2.0);
      // Held: drawn in its holder's colour, glowing harder.
      const Rgb held = p.owner ? side(p.owner) : pc;
      ring(c, c.X(p.p.x), c.Y(p.p.y), c.s * (1.1 + 0.15 * glow), line, held, (p.owner ? 0.5 : 0.25) + 0.3 * glow,
           c.s * (p.owner ? 0.8 : 0.5) * glow);
      outline(c, diamond, line * 1.2, held, 1.0);
      if (game.showLabels && !p.label.empty()) text(c, p.label, c.X(p.p.x), c.Y(p.p.y) + c.s * 1.7, cell * 0.65, held, 0.95);
    }
  }
  if (game.showObjectives) {
    const auto now = Clock::now();
    for (std::size_t i = 0; i < plan.objectives.size(); ++i) {
      const auto& o = plan.objectives[i];
      if (o.muted) continue;
      const double cx = c.X(o.p.x), cy = c.Y(o.p.y), r = (o.range + o.marker) * c.s;
      const bool contested = o.owner == 3;
      const Rgb col = contested ? white : o.owner ? side(o.owner) : white;
      const bool lit = game.highlight == static_cast<int>(i) + 1;
      const double pulse = lit ? 0.6 + 0.4 * std::sin(ts * 4.0) : 1.0;
      if (!o.poly.empty()) {
        if (o.owner || lit) fillPoly(c, o.poly, [&](int x, int y, double, double) {
          c.blend(x, y, col.r, col.g, col.b, lit ? 0.12 * pulse : 0.06);
        });
        outline(c, o.poly, line * (lit ? 1.8 : 1.2), col, o.owner || lit ? 1.0 : 0.7);
        if (lit) outline(c, o.poly, line * 2.5, hlc, 0.35 * pulse);
        if (arcane || ember) outline(c, o.poly, line * 0.7, col, 0.9, c.s * 0.35, ts * c.s * 0.3);
        text(c, o.label, cx, cy, cell * 1.3, col, 1.0);
        if (game.showLabels && !o.kind.empty()) text(c, o.kind, cx, cy + c.s * 0.9, cell * 0.6, col, 0.8);
        continue;
      }
      if (contested) {
        // Contested: the ring in both sides' colours, alternating.
        // Eight long arcs each, so the two colours read apart across a table
        // instead of averaging to white.
        ring(c, cx, cy, r, line * 2.2, c1, 1.0, 0, 8, ts * 0.15, 0.5);
        ring(c, cx, cy, r, line * 2.2, c2, 1.0, 0, 8, ts * 0.15 + 3.141592653589793 / 8, 0.5);
        // Clear of the round strips along both edges.
        const bool above = o.p.y + o.range + o.marker + 3.5 > plan.h;
        plate(c, "CONTESTED", cx, above ? cy - r - c.s * 0.9 : cy + r + c.s * 0.9, cell * 0.6, 0.8);
        text(c, "CONTESTED", cx, above ? cy - r - c.s * 0.9 : cy + r + c.s * 0.9, cell * 0.6, white, 0.95);
      } else {
        ring(c, cx, cy, r, line * (lit ? 1.6 : 1.2), col, o.owner || lit ? 1.0 : 0.7, o.owner || lit ? c.s * 0.6 * pulse : 0);
      }
      // The 40mm marker itself, so the range reads from its edge.
      if (o.marker > 0) ring(c, cx, cy, o.marker * c.s, line, white, 0.9, 0);
      // The halo: a slowly turning ring of arcs outside the control range.
      // Arcane runs it as rune ticks; ember flickers it.
      const double dir = (i % 2) ? -1 : 1;
      if (arcane) {
        ring(c, cx, cy, r + c.s * 0.55, line * 1.4, col, 0.7, 0, 24, dir * ts * 0.35, 0.25);
        ring(c, cx, cy, r + c.s * 0.95, line * 0.6, col, 0.4, 0, 6, -dir * ts * 0.2, 0.6);
      } else if (ember) {
        const double flick = 0.55 + 0.45 * noise(i * 7.0, ts * 3.0);
        ring(c, cx, cy, r + c.s * 0.5, line, scaled(Rgb {255, 140, 40}, B), 0.35 * flick, c.s * 0.7 * flick, 10, dir * ts * 0.5, 0.7);
      } else if (o.owner && !contested) {
        ring(c, cx, cy, r + c.s * 0.5, line, col, 0.5, 0, 8, dir * ts * 0.4, 0.5);
      }
      if (lit) ring(c, cx, cy, r + c.s * 0.6, line, hlc, 0.45 * pulse, 0);
      // Changing hands: a ripple out from the ring, over a second and a half.
      const double since = std::chrono::duration<double>(now - o.changed).count();
      if (o.changed != Clock::time_point {} && since < 1.5) {
        const double k = since / 1.5;
        ring(c, cx, cy, r + c.s * 4.0 * k, line * 2.0, contested ? c2 : col, 0.8 * (1 - k), c.s * 0.4);
      }
      text(c, o.label, cx, cy, cell * 1.3, col, 1.0);
      // Its type under the ring, or over it when the ring is near the bottom edge.
      if (game.showLabels && !o.kind.empty()) {
        const bool above = o.p.y + o.range + 1.5 > plan.h;
        text(c, o.kind, cx, above ? cy - r - c.s * 0.9 : cy + r + c.s * 0.9, cell * 0.6, col, 0.8);
      }
    }
  }
  // MEASUREMENTS, over everything else on the table.
  for (const auto& m : game.measures) {
    if (m.muted) continue;
    const Rgb mc = m.player ? side(m.player) : scaled(Rgb {255, 240, 160}, B);
    if (m.kind == "deepstrike") {
      // Nothing arrives within 9": a red no-go disc, dashed at its edge.
      const double cx = c.X(m.a.x), cy = c.Y(m.a.y), r = 9.0 * c.s;
      const Rgb red = scaled(Rgb {224, 72, 72}, B);
      const std::vector<Pt> disc = [&] {
        std::vector<Pt> pts;
        for (int k = 0; k < 64; ++k) pts.push_back({m.a.x + 9.0 * std::cos(k * 6.283185307179586 / 64), m.a.y + 9.0 * std::sin(k * 6.283185307179586 / 64)});
        return pts;
      }();
      fillPoly(c, disc, [&](int x, int y, double, double) { c.blend(x, y, red.r, red.g, red.b, 0.10); });
      ring(c, cx, cy, r, line * 1.4, red, 0.95, 0, 48, ts * 0.1, 0.55);
      ring(c, cx, cy, c.s * 0.3, line * 1.4, red, 1.0, 0);
      const std::string d = "9\" NO DEEP STRIKE";
      plate(c, d, cx, cy - r, cell * 0.8, 0.8);
      text(c, d, cx, cy - r, cell * 0.8, red, 1.0);
      continue;
    }
    if (m.kind == "ruler") {
      const double ax = c.X(m.a.x), ay = c.Y(m.a.y), bx = c.X(m.b.x), by = c.Y(m.b.y);
      segment(c, ax, ay, bx, by, line * 1.4, mc, 0.95);
      const double len = std::hypot(m.b.x - m.a.x, m.b.y - m.a.y);
      // A tick every inch, longer every six.
      for (int i = 1; i < static_cast<int>(len); ++i) {
        const double f = i / len, tx = ax + (bx - ax) * f, ty = ay + (by - ay) * f;
        const double nx = -(by - ay) / std::max(1.0, std::hypot(bx - ax, by - ay)), ny = (bx - ax) / std::max(1.0, std::hypot(bx - ax, by - ay));
        const double tl = c.s * (i % 6 == 0 ? 0.45 : 0.22);
        segment(c, tx - nx * tl, ty - ny * tl, tx + nx * tl, ty + ny * tl, line, mc, 0.9);
      }
      ring(c, ax, ay, c.s * 0.25, line * 1.4, mc, 1.0, 0);
      ring(c, bx, by, c.s * 0.25, line * 1.4, mc, 1.0, 0);
      char d[16];
      std::snprintf(d, sizeof(d), "%.1f\"", len);
      plate(c, d, (ax + bx) / 2, (ay + by) / 2 - c.s * 0.9, cell * 1.1, 0.85);
      text(c, d, (ax + bx) / 2, (ay + by) / 2 - c.s * 0.9, cell * 1.1, mc, 1.0);
    } else {
      // Rings: solid, with the distance written at the top of each. A move's
      // second ring (the run) is dashed.
      const double cx = c.X(m.a.x), cy = c.Y(m.a.y);
      ring(c, cx, cy, c.s * 0.25, line * 1.4, mc, 1.0, 0);
      for (std::size_t i = 0; i < m.radii.size(); ++i) {
        const bool run = m.kind == "move" && i == 1;
        const double r = m.radii[i] * c.s;
        if (run) ring(c, cx, cy, r, line * 1.2, mc, 0.8, 0, 48, ts * 0.1, 0.5);
        else ring(c, cx, cy, r, line * 1.4, mc, 0.9, c.s * 0.25);
        char d[24];
        std::snprintf(d, sizeof(d), run ? "RUN %g\"" : "%g\"", m.radii[i]);
        plate(c, d, cx, cy - r, cell * 0.8, 0.8);
        text(c, d, cx, cy - r, cell * 0.8, mc, 1.0);
      }
    }
  }

  if (game.setup) {
    // SETTING UP: what the mission is and what goes where, in the middle of
    // the table where both players can read it; turned for the far side too.
    std::vector<std::string> lines {
      "SETUP  -  " + plan.title,
      std::to_string(static_cast<int>(plan.w)) + "\" X " + std::to_string(static_cast<int>(plan.h)) + "\"" +
        (plan.twist.empty() ? std::string() : "   RULE: " + plan.twist),
    };
    if (!plan.terrain.empty())
      lines.push_back(std::string("TERRAIN: ") + (game.layout.empty() ? "AS DRAWN" : game.layout) +
                      ". PUT EACH RUIN ON ITS OUTLINE.");
    lines.push_back("TAP BATTLE ON THE PHONE TO START ROUND 1");
    // One card per player, in their own half and facing them, on a solid
    // panel: the middle of the table is where the objectives are.
    const double sc = cell * 0.9, gap = sc * 11;
    double widest = 0;
    for (const auto& l : lines) widest = std::max(widest, textWidth(l, sc));
    const double pw = widest + sc * 8, ph = gap * lines.size() + sc * 6;
    for (int side2 = 0; side2 < 2; ++side2) {
      const bool flip = side2 == 1;
      const double cy = c.Y(plan.h * (flip ? 0.32 : 0.68)), cx = c.X(plan.w / 2);
      for (int y = static_cast<int>(cy - ph / 2); y < static_cast<int>(cy + ph / 2); ++y)
        for (int x = static_cast<int>(cx - pw / 2); x < static_cast<int>(cx + pw / 2); ++x) c.blend(x, y, 0, 0, 0, 0.9);
      outline(c, {{(cx - pw / 2 - c.ox) / c.s, (cy - ph / 2 - c.oy) / c.s}, {(cx + pw / 2 - c.ox) / c.s, (cy - ph / 2 - c.oy) / c.s},
                  {(cx + pw / 2 - c.ox) / c.s, (cy + ph / 2 - c.oy) / c.s}, {(cx - pw / 2 - c.ox) / c.s, (cy + ph / 2 - c.oy) / c.s}},
              line, flip ? c2 : c1, 0.9);
      for (std::size_t i = 0; i < lines.size(); ++i) {
        const double off = (static_cast<double>(i) - (lines.size() - 1) / 2.0) * gap;
        text(c, upper(lines[i]), cx, cy + (flip ? -off : off), sc, i == 0 ? white : grey, 0.95, flip);
      }
    }
  }
  if (game.showStrip && !game.setup) {
    const std::string who = game.over ? std::string("GAME OVER")
                          : (game.turn == 2 ? plan.p2 : plan.p1) + " - " + phaseAt(plan, game.phase).name;
    const std::string r = "ROUND " + std::to_string(game.round) + (is40k(plan) ? " / 5" : "");
    const std::string cp = "   CP " + std::to_string(game.cp1) + " - " + std::to_string(game.cp2);
    const std::string cpTheirs = "   CP " + std::to_string(game.cp2) + " - " + std::to_string(game.cp1);
    const std::string clk = game.clockOn ? "   " + clockText(clockSeconds(game, 1)) + " | " + clockText(clockSeconds(game, 2)) + (game.clockPaused ? " (PAUSED)" : "") : "";
    const std::string clkTheirs = game.clockOn ? "   " + clockText(clockSeconds(game, 2)) + " | " + clockText(clockSeconds(game, 1)) + (game.clockPaused ? " (PAUSED)" : "") : "";
    const std::string mine = r + "   " + who + "   " + plan.p1 + " " + std::to_string(game.vp1) + " - " + std::to_string(game.vp2) + " " + plan.p2 + cp + clk;
    const std::string theirs = r + "   " + who + "   " + plan.p2 + " " + std::to_string(game.vp2) + " - " + std::to_string(game.vp1) + " " + plan.p1 + cpTheirs + clkTheirs;
    // Second line: the twist, and the underdog (the side behind on points).
    std::string second;
    if (std::chrono::duration<double>(Clock::now() - game.scoredAt).count() < 8.0 && !game.lastScore.empty())
      second = game.lastScore + "   ";
    if (!plan.twist.empty()) second += "TWIST: " + plan.twist;
    {
      const auto& activeTactic = game.tac[game.turn == 2 ? 1 : 0];
      if (plan.pack.empty() && activeTactic.slot >= 0 && activeTactic.pick >= 0 && activeTactic.card[activeTactic.slot] >= 0)
        second += std::string("   TACTIC: ") + kTacticCards[activeTactic.card[activeTactic.slot]].tactics[activeTactic.pick].name;
    }
    if (game.vp1 != game.vp2) second += (second.empty() ? "" : "   ") + std::string("UNDERDOG: ") + (game.vp1 < game.vp2 ? plan.p1 : plan.p2);
    // Small, hard against each edge, on a plate: objectives often sit near an
    // edge, and the strip must neither hide one nor be lost over its ring.
    const double sc = cell * 0.75, edge = c.s * 0.55;
    plate(c, mine, c.X(plan.w / 2), c.Y(plan.h) - edge, sc, 0.8);
    text(c, mine, c.X(plan.w / 2), c.Y(plan.h) - edge, sc, white, 0.95);
    plate(c, theirs, c.X(plan.w / 2), c.Y(0) + edge, sc, 0.8);
    text(c, theirs, c.X(plan.w / 2), c.Y(0) + edge, sc, white, 0.95, true);
    if (is40k(plan)) {
      // Each player's own secondaries, on their own edge, in their colour;
      // the mission rule and the last score go with them.
      const double lift = sc * 11;
      for (int pl = 0; pl < 2; ++pl) {
        std::string line2;
        for (const auto& name : game.sec[pl].active) line2 += (line2.empty() ? "" : "  |  ") + name;
        if (line2.empty()) line2 = "NO SECONDARIES";
        line2 = "SECONDARIES: " + line2;
        if (!plan.twist.empty()) line2 += "   RULE: " + plan.twist;
        if (std::chrono::duration<double>(Clock::now() - game.scoredAt).count() < 8.0 && !game.lastScore.empty())
          line2 = game.lastScore + "   " + line2;
        const bool top = pl == 1;   // P2 sits at the top edge, so its strip is turned to face them
        const double y = top ? c.Y(0) + edge + lift : c.Y(plan.h) - edge - lift;
        plate(c, line2, c.X(plan.w / 2), y, sc * 0.85, 0.75);
        text(c, line2, c.X(plan.w / 2), y, sc * 0.85, side(pl + 1), 0.95, top);
      }
      second.clear();
    }
    if (!second.empty()) {
      const double lift = sc * 11;
      plate(c, second, c.X(plan.w / 2), c.Y(plan.h) - edge - lift, sc * 0.85, 0.75);
      text(c, second, c.X(plan.w / 2), c.Y(plan.h) - edge - lift, sc * 0.85, scaled(kGold, B), 0.95);
      plate(c, second, c.X(plan.w / 2), c.Y(0) + edge + lift, sc * 0.85, 0.75);
      text(c, second, c.X(plan.w / 2), c.Y(0) + edge + lift, sc * 0.85, scaled(kGold, B), 0.95, true);
    }
  }
}

// THE TABLE FILLS THE WHOLE PICTURE. The warp pulls the output's four corners
// onto the table's four corners, so any border here would be a dead strip
// inside the table and every inch would land short. The table is drawn at its
// true proportions -- never smaller than the output either way, so nothing is
// magnified -- then scaled to fill the frame; the warp undoes the stretch on
// the real table, and CALIBRATE and a tape measure confirm it.
void render(std::uint8_t* rgba, int w, int h, double t) {
  double pw, ph;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    pw = g_plan.w;
    ph = g_plan.h;
  }
  const int tw = std::max(w, static_cast<int>(std::lround(h * pw / ph)));
  const int th = std::max(h, static_cast<int>(std::lround(w * ph / pw)));
  if (tw == w && th == h) { renderTable(rgba, w, h, t); return; }
  thread_local std::vector<std::uint8_t> table;
  table.resize(static_cast<std::size_t>(tw) * th * 4);
  renderTable(table.data(), tw, th, t);
  // A plain pointer: inside the worker threads `table` would name each
  // thread's own (empty) thread_local copy, not this one.
  const std::uint8_t* src = table.data();
  const double kx = static_cast<double>(tw) / w, ky = static_cast<double>(th) / h;
  parallelRows(h, [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      // Box-average the source rows and columns that fall in this pixel: the
      // scale only ever shrinks, and averaging keeps thin lines from flickering.
      const int y0 = static_cast<int>(y * ky), y1 = std::max(y0 + 1, std::min(th, static_cast<int>((y + 1) * ky)));
      for (int x = 0; x < w; ++x) {
        const int x0 = static_cast<int>(x * kx), x1 = std::max(x0 + 1, std::min(tw, static_cast<int>((x + 1) * kx)));
        unsigned sum[3] = {0, 0, 0};
        for (int sy = y0; sy < y1; ++sy) {
          const std::uint8_t* row = src + (static_cast<std::size_t>(sy) * tw + x0) * 4;
          for (int sx = x0; sx < x1; ++sx, row += 4) { sum[0] += row[0]; sum[1] += row[1]; sum[2] += row[2]; }
        }
        const unsigned n = static_cast<unsigned>((y1 - y0) * (x1 - x0));
        std::uint8_t* out = &rgba[(static_cast<std::size_t>(y) * w + x) * 4];
        out[0] = static_cast<std::uint8_t>(sum[0] / n);
        out[1] = static_cast<std::uint8_t>(sum[1] / n);
        out[2] = static_cast<std::uint8_t>(sum[2] / n);
        out[3] = 255;
      }
    }
  });
}

// ── the TABLE verb ─────────────────────────────────────────────────────────

std::string upper(std::string s) { for (auto& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); return s; }

std::string statusJson() {
  std::ostringstream o;
  auto js = [](const std::string& v) {
    std::string out;
    for (char ch : v) { if (ch == '"' || ch == '\\') out += '\\'; out += ch; }
    return out;
  };
  auto hex = [](Rgb c) { char b[8]; std::snprintf(b, sizeof(b), "#%02x%02x%02x", c.r, c.g, c.b); return std::string(b); };
  o << "{\"plan\":\"" << g_plan.name << "\",\"title\":\"" << g_plan.title << "\",\"p1\":\"" << g_plan.p1
    << "\",\"p2\":\"" << g_plan.p2 << "\",\"style\":" << g_game.style << ",\"speed\":" << g_game.speed
    << ",\"bright\":" << g_game.bright << ",\"board60\":" << g_game.board60 << ",\"ground\":\"" << g_game.ground << "\",\"groundLevel\":" << g_game.groundLevel << ",\"labels\":" << g_game.showLabels
    << ",\"colors\":[\"" << hex(g_game.c1) << "\",\"" << hex(g_game.c2) << "\",\"" << hex(g_game.hl) << "\",\"" << hex(g_game.place) << "\"]"
    << ",\"w\":" << g_plan.w << ",\"h\":" << g_plan.h
    << ",\"pack\":\"" << js(g_plan.pack) << "\",\"note\":\"" << js(g_plan.note) << "\""
    << ",\"round\":" << g_game.round << ",\"turn\":" << g_game.turn << ",\"phase\":\"" << (g_game.over ? "GAME OVER" : phaseAt(g_plan, g_game.phase).name) << "\",\"edge\":" << g_game.showEdge
    << ",\"system\":\"" << g_plan.system << "\",\"over\":" << g_game.over << ",\"setup\":" << g_game.setup << ",\"vpCap\":" << vpCap(g_plan)
    << ",\"layout\":\"" << js(g_game.layout) << "\",\"phases\":["
    ;
  for (int i = 0; i < phaseCount(g_plan); ++i) o << (i ? "," : "") << '"' << phaseAt(g_plan, i).name << '"';
  o << "],\"sec\":[";
  for (int pl = 0; pl < 2; ++pl) {
    o << (pl ? "," : "") << "{\"active\":[";
    for (std::size_t i = 0; i < g_game.sec[pl].active.size(); ++i) o << (i ? "," : "") << '"' << js(g_game.sec[pl].active[i]) << '"';
    o << "],\"scored\":[";
    for (std::size_t i = 0; i < g_game.sec[pl].scored.size(); ++i)
      o << (i ? "," : "") << "[\"" << js(g_game.sec[pl].scored[i].first) << "\"," << g_game.sec[pl].scored[i].second << ']';
    o << "]}";
  }
  o << "],\"missions\":{";
  {
    const char* kinds[] = {"deployments", "primaries", "rules", "layouts"};
    for (int k = 0; k < 4; ++k) {
      o << (k ? "," : "") << '"' << kinds[k] << "\":[";
      const auto names = listPlans(dir40k(kinds[k]));
      for (std::size_t i = 0; i < names.size(); ++i) o << (i ? "," : "") << '"' << js(names[i]) << '"';
      o << ']';
    }
  }
  o << "},\"folder40k\":\"" << js((planDir() / "40k").generic_string()) << "\""
    << ",\"cp\":[" << g_game.cp1 << ',' << g_game.cp2 << "],\"clock\":" << g_game.clockOn << ",\"clockPaused\":" << g_game.clockPaused
    << ",\"clocks\":[\"" << clockText(clockSeconds(g_game, 1)) << "\",\"" << clockText(clockSeconds(g_game, 2)) << "\"]"
    << ",\"twist\":\"" << g_plan.twist << "\",\"measures\":" << g_game.measures.size() << ",\"vp\":[" << g_game.vp1 << ',' << g_game.vp2
    << "],\"hl\":" << g_game.highlight << ",\"cal\":" << g_game.calibrate << ",\"anim\":" << g_game.animate
    << ",\"layers\":{\"territories\":" << g_game.showTerritories << ",\"deployment\":" << g_game.showDeployment
    << ",\"guides\":" << g_game.showGuides << ",\"labels\":" << g_game.showLabels
    << ",\"terrain\":" << g_game.showTerrain << ",\"places\":" << g_game.showPlaces
    << ",\"objectives\":" << g_game.showObjectives << ",\"strip\":" << g_game.showStrip << ",\"edge\":" << g_game.showEdge << "},\"obj\":[";
  for (std::size_t i = 0; i < g_plan.objectives.size(); ++i) {
    const auto& ob = g_plan.objectives[i];
    o << (i ? "," : "") << '[' << ob.p.x << ',' << ob.p.y << ',' << ob.owner << ",\"" << js(ob.label) << "\"," << (ob.range + ob.marker) << ",[";
    for (std::size_t j = 0; j < ob.poly.size(); ++j)
      o << (j ? "," : "") << '[' << ob.poly[j].x << ',' << ob.poly[j].y << ']';
    o << "]," << ob.muted << ']';
  }
  o << "],\"lines\":[";
  for (std::size_t i = 0; i < g_plan.lines.size(); ++i) {
    const auto& l = g_plan.lines[i];
    o << (i ? "," : "") << '[' << l.a.x << ',' << l.a.y << ',' << l.b.x << ',' << l.b.y << ',' << l.muted << ']';
  }
  o << "],\"places\":[";
  for (std::size_t i = 0; i < g_plan.places.size(); ++i) {
    const auto& pl = g_plan.places[i];
    o << (i ? "," : "") << '[' << pl.p.x << ',' << pl.p.y << ',' << pl.owner << ",\"" << js(pl.label) << "\"," << pl.muted << ']';
  }
  o << "],\"elements\":[";
  bool firstElement = true;
  visitElements(g_plan, g_game, [&](const char* kind, std::size_t index, const std::string& label, bool muted) {
    o << (firstElement ? "" : ",") << "[\"" << kind << "\"," << index << ",\"" << js(label) << "\"," << muted << ']';
    firstElement = false;
  });
  o << "],\"auto\":" << g_game.autoScore << ",\"lastScore\":\"" << js(g_game.lastScore) << "\",\"scores\":[";
  for (std::size_t i = 0; i < g_plan.scores.size(); ++i) {
    const auto& sc = g_plan.scores[i];
    o << (i ? "," : "") << '[' << sc.vp << ",\"" << sc.rule << "\",\"" << js(sc.text) << "\"]";
  }
  o << "],\"tac\":[";
  for (int pl = 0; pl < 2; ++pl) {
    const auto& t = g_game.tac[pl];
    o << (pl ? "," : "") << "{\"card\":[" << t.card[0] << ',' << t.card[1] << "],\"slot\":" << t.slot << ",\"pick\":" << t.pick
      << ",\"done\":" << t.done.size() << '}';
  }
  o << "],\"cards\":[";
  for (int i = 0; i < kTacticCardCount; ++i) {
    const auto& cd = kTacticCards[i];
    o << (i ? "," : "") << "{\"name\":\"" << js(cd.name) << "\",\"t\":[";
    for (int k = 0; k < 3; ++k)
      o << (k ? "," : "") << "[\"" << js(cd.tactics[k].name) << "\"," << cd.tactics[k].vp << ",\"" << cd.tactics[k].type << "\",\"" << js(cd.tactics[k].text) << "\"]";
    o << "]}";
  }
  o << "],\"plans\":[";
  bool first = true;
  std::vector<std::string> listed;
  for (const auto& b : planCatalog()) {
    o << (first ? "" : ",") << "[\"" << b.slug << "\",\"" << b.title << "\",\"" << js(b.pack)
      << "\",\"" << (b.system == "40k" ? "40k" : b.pack.empty() ? "pitched" : "spearhead") << "\"]";
    listed.push_back(b.slug);
    first = false;
  }
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(planDir(), ec)) {
    const std::string stem = e.path().stem().string();
    if (e.path().extension() != ".txt" || std::find(listed.begin(), listed.end(), stem) != listed.end()) continue;
    o << (first ? "" : ",") << "[\"" << js(stem) << "\",\"" << js(stem) << "\",\"\",\"custom\"]";
    first = false;
  }
  o << "]}";
  return o.str();
}

bool toggleWord(const std::string& word, bool current, bool& out) {
  const std::string w = upper(word);
  if (w == "ON") out = true; else if (w == "OFF") out = false; else if (w == "TOGGLE" || w.empty()) out = !current;
  else return false;
  return true;
}

bool step(const std::string& word, int& value, int lo, int hi) {
  if (word == "+") value = std::min(hi, value + 1);
  else if (word == "-") value = std::max(lo, value - 1);
  else if (!word.empty() && std::isdigit(static_cast<unsigned char>(word[0]))) value = std::clamp(std::atoi(word.c_str()), lo, hi);
  else return false;
  return true;
}

bool tableCommand(const std::vector<std::string>& parts, std::string& reply) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const std::string sub = parts.size() > 1 ? upper(parts[1]) : "STATUS";
  const std::string a = parts.size() > 2 ? parts[2] : "";
  const std::string b = parts.size() > 3 ? parts[3] : "";
  if (sub == "STATUS") { reply = statusJson(); return true; }
  if (sub == "PLAN") {
    if (a.empty()) { reply = "PLAN <name> (files in " + planDir().string() + ")"; return false; }
    Plan plan;
    if (!loadAndFit(a, plan, reply)) return false;
    adoptSystemColours(plan, g_game);
    g_plan = std::move(plan);
    g_game.highlight = 0;
    g_game.measures.clear();
    g_game.awarded.clear(); g_game.lastScore.clear();
    reply = "plan " + g_plan.name;
    return true;
  }
  if (sub == "BOARD") {
    if (!g_plan.pack.empty()) { reply = "Spearhead boards are fixed at 30 x 22 inches"; return false; }
    if (is40k(g_plan)) { reply = "a 40k plan is drawn at the size its card gives"; return false; }
    const std::string k = upper(a);
    if (k == "60X44" || k == "LARGE") g_game.board60 = true;
    else if (k == "44X30" || k == "SMALL") g_game.board60 = false;
    else { reply = "BOARD 60X44 | 44X30"; return false; }
    Plan plan;
    if (!loadAndFit(g_plan.name, plan, reply)) return false;
    retainPlanState(plan, g_plan);
    g_plan = std::move(plan);
    reply = "board " + std::to_string(static_cast<int>(g_plan.w)) + " x " + std::to_string(static_cast<int>(g_plan.h));
    return true;
  }
  if (sub == "RELOAD") {
    Plan plan;
    if (!loadAndFit(g_plan.name, plan, reply)) return false;
    retainPlanState(plan, g_plan);
    g_plan = std::move(plan);
    reply = "reloaded " + g_plan.name;
    return true;
  }
  if (sub == "SIZE") {
    if (!g_plan.pack.empty() && upper(a) != "SPEARHEAD" && !(a == "30" && b == "22")) {
      reply = "This Spearhead layout uses a fixed 30 x 22 inch board"; return false;
    }
    const std::string k = upper(a);
    if (k == "SPEARHEAD") { g_plan.w = 30; g_plan.h = 22; }
    else if (k == "PITCHED") { g_plan.w = 60; g_plan.h = 44; }
    else if (std::atof(a.c_str()) >= 6 && std::atof(b.c_str()) >= 6) { g_plan.w = std::atof(a.c_str()); g_plan.h = std::atof(b.c_str()); }
    else { reply = "SIZE SPEARHEAD | PITCHED | <w> <h> (inches)"; return false; }
    reply = "table " + std::to_string(g_plan.w) + " x " + std::to_string(g_plan.h);
    return true;
  }
  if (sub == "OBJ") {
    const int n = std::atoi(a.c_str());
    if (n < 1 || n > static_cast<int>(g_plan.objectives.size())) { reply = "no objective " + a; return false; }
    if (g_plan.objectives[n - 1].muted) { reply = "Objective is muted; restore it before changing ownership"; return false; }
    int& owner = g_plan.objectives[n - 1].owner;
    const std::string k = upper(b);
    if (k == "P1") owner = 1; else if (k == "P2") owner = 2; else if (k == "NONE") owner = 0;
    else if (k == "CONTESTED" && is40k(g_plan)) owner = 3;
    else if (k == "NEXT" || k.empty()) owner = (owner + 1) % (is40k(g_plan) ? 4 : 3);
    else { reply = is40k(g_plan) ? "OBJ <n> P1|P2|CONTESTED|NONE|NEXT" : "OBJ <n> P1|P2|NONE|NEXT"; return false; }
    g_plan.objectives[n - 1].changed = Clock::now();
    reply = "objective " + a + (owner == 3 ? " contested" : owner ? " player " + std::to_string(owner) : " nobody");
    return true;
  }
  if (sub == "HIGHLIGHT") {
    const int n = upper(a) == "OFF" ? 0 : std::atoi(a.c_str());
    if (n < 0 || n > static_cast<int>(g_plan.objectives.size()) || (n && g_plan.objectives[n - 1].muted)) {
      reply = "Choose an active objective to highlight"; return false;
    }
    g_game.highlight = (n == g_game.highlight) ? 0 : n;
    reply = g_game.highlight ? "highlight " + std::to_string(g_game.highlight) : "highlight off";
    return true;
  }
  if (sub == "MUTE") {
    const std::string kind = upper(a);
    const bool all = kind == "ALL";
    const int index = std::atoi(b.c_str());
    const std::string action = all ? b : parts.size() > 4 ? parts[4] : "TOGGLE";
    bool checked = false;
    if (!toggleWord(action, false, checked)) { reply = "MUTE <type> <n> ON|OFF|TOGGLE | MUTE ALL OFF"; return false; }
    bool found = false;
    visitElements(g_plan, g_game, [&](const char* type, std::size_t n, const std::string&, bool& muted) {
      if (all || (kind == type && index > 0 && n == static_cast<std::size_t>(index))) {
        toggleWord(action, muted, muted); found = true;
      }
    });
    if (!found && !all) { reply = "No such element: " + kind + " " + b; return false; }
    if (g_game.highlight > 0 && g_game.highlight <= static_cast<int>(g_plan.objectives.size()) &&
        g_plan.objectives[g_game.highlight - 1].muted) g_game.highlight = 0;
    reply = all ? "element mutes updated" : kind + " " + b + " mute updated";
    return true;
  }
  if (sub == "ROUND") { if (!step(a, g_game.round, 1, 99)) { reply = "ROUND + | - | <n>"; return false; } reply = "round " + std::to_string(g_game.round); return true; }
  if (sub == "SCORE") {
    if (g_plan.scores.empty()) { reply = "Use VP controls and this battlepack's cards for scoring"; return false; }
    // SCORE AUTO ON|OFF|TOGGLE | SCORE <n> P1|P2 (a manual line's points).
    if (upper(a) == "AUTO") {
      if (!toggleWord(b, g_game.autoScore, g_game.autoScore)) { reply = "SCORE AUTO ON|OFF|TOGGLE"; return false; }
      reply = g_game.autoScore ? "auto-scoring on" : "auto-scoring off";
      return true;
    }
    const int n = std::atoi(a.c_str());
    if (n < 1 || n > static_cast<int>(g_plan.scores.size()) || (upper(b) != "P1" && upper(b) != "P2")) {
      reply = "SCORE AUTO ON|OFF | SCORE <line> P1|P2"; return false;
    }
    const int pl = upper(b) == "P2" ? 2 : 1;
    (pl == 2 ? g_game.vp2 : g_game.vp1) += g_plan.scores[n - 1].vp;
    capVp(g_plan, g_game);
    g_game.lastScore = (pl == 2 ? g_plan.p2 : g_plan.p1) + " SCORES " + std::to_string(g_plan.scores[n - 1].vp) + " VP";
    g_game.scoredAt = Clock::now();
    reply = g_game.lastScore;
    return true;
  }
  if (sub == "TACTIC") {
    if (!g_plan.pack.empty()) { reply = "Use this Spearhead battlepack's tactic deck and the VP controls"; return false; }
    if (is40k(g_plan)) { reply = "40k uses secondaries: TABLE SECONDARY"; return false; }
    // TACTIC CARD P1|P2 <slot 1|2> NEXT|<n>  -- the cards a player brought
    // TACTIC PICK P1|P2 <slot 1|2> <1-3>|NONE -- the tactic they go for
    // TACTIC DONE P1|P2                        -- completed: its VP are banked
    const std::string what = upper(a), who = upper(b);
    if (who != "P1" && who != "P2") { reply = "TACTIC CARD|PICK|DONE P1|P2 ..."; return false; }
    auto& t = g_game.tac[who == "P2" ? 1 : 0];
    const std::string c3 = parts.size() > 4 ? upper(parts[4]) : "";
    const std::string c4 = parts.size() > 5 ? upper(parts[5]) : "";
    if (what == "CARD") {
      const int slot = std::atoi(c3.c_str()) - 1;
      if (slot < 0 || slot > 1) { reply = "TACTIC CARD P1|P2 1|2 NEXT|<n>"; return false; }
      int& card = t.card[slot];
      if (c4 == "NEXT" || c4.empty()) card = card + 1 >= kTacticCardCount ? -1 : card + 1;
      else card = std::clamp(std::atoi(c4.c_str()) - 1, -1, kTacticCardCount - 1);
      if (t.slot == slot) t.pick = -1;
      reply = card >= 0 ? std::string(kTacticCards[card].name) : std::string("no card");
      return true;
    }
    if (what == "PICK") {
      if (c3 == "NONE") { t.slot = t.pick = -1; reply = "no tactic"; return true; }
      const int slot = std::atoi(c3.c_str()) - 1, k = std::atoi(c4.c_str()) - 1;
      if (slot < 0 || slot > 1 || k < 0 || k > 2 || t.card[slot] < 0) { reply = "TACTIC PICK P1|P2 <slot 1|2> <1-3> (that slot needs a card)"; return false; }
      t.slot = slot; t.pick = k;
      reply = kTacticCards[t.card[slot]].tactics[k].name;
      return true;
    }
    if (what == "DONE") {
      if (t.slot < 0 || t.pick < 0 || t.card[t.slot] < 0) { reply = "no tactic picked"; return false; }
      const auto& tc = kTacticCards[t.card[t.slot]].tactics[t.pick];
      (who == "P2" ? g_game.vp2 : g_game.vp1) += tc.vp;
      t.done.push_back(tc.name);
      g_game.lastScore = (who == "P2" ? g_plan.p2 : g_plan.p1) + " COMPLETES " + tc.name + " +" + std::to_string(tc.vp) + " VP";
      g_game.scoredAt = Clock::now();
      t.slot = t.pick = -1;
      reply = g_game.lastScore;
      return true;
    }
    reply = "TACTIC CARD|PICK|DONE P1|P2 ...";
    return false;
  }
  if (sub == "CP") {
    int& v = upper(a) == "P2" ? g_game.cp2 : g_game.cp1;
    if ((upper(a) != "P1" && upper(a) != "P2") || !step(b, v, 0, 99)) { reply = "CP P1|P2 + | - | <n>"; return false; }
    reply = "cp " + std::to_string(g_game.cp1) + " - " + std::to_string(g_game.cp2);
    return true;
  }
  if (sub == "CLOCK") {
    const std::string k = upper(a);
    if (k == "ON" || ((k == "TOGGLE" || k.empty()) && !g_game.clockOn)) { g_game.clockOn = true; g_game.clockPaused = false; g_game.clockSince = Clock::now(); }
    else if (k == "OFF" || k == "TOGGLE" || k.empty()) { bankClock(g_game); g_game.clockOn = false; }
    else if (k == "PAUSE") { bankClock(g_game); g_game.clockPaused = !g_game.clockPaused; g_game.clockSince = Clock::now(); }
    else if (k == "RESET") { g_game.clock1 = g_game.clock2 = 0; g_game.clockSince = Clock::now(); }
    else { reply = "CLOCK ON | OFF | TOGGLE | PAUSE | RESET"; return false; }
    reply = std::string(g_game.clockOn ? "clock on" : "clock off") + (g_game.clockOn && g_game.clockPaused ? ", paused" : "");
    return true;
  }
  if (sub == "PLACE") {
    // PLACE <n> P1|P2|NONE|NEXT: who holds a place of power.
    const int n = std::atoi(a.c_str());
    if (n < 1 || n > static_cast<int>(g_plan.places.size())) { reply = "no place " + a; return false; }
    if (g_plan.places[n - 1].muted) { reply = "Place is muted; restore it before changing ownership"; return false; }
    int& owner = g_plan.places[n - 1].owner;
    const std::string k = upper(b);
    if (k == "P1") owner = 1; else if (k == "P2") owner = 2; else if (k == "NONE") owner = 0;
    else if (k == "NEXT" || k.empty()) owner = (owner + 1) % 3;
    else { reply = "PLACE <n> P1|P2|NONE|NEXT"; return false; }
    reply = "place " + a + (owner ? " player " + std::to_string(owner) : " nobody");
    return true;
  }
  if (sub == "RINGS" || sub == "RULER" || sub == "MOVE" || sub == "MEASURE") {
    // RINGS x y [r,r,...] | RULER x1 y1 x2 y2 | MOVE x y <move> [run]
    // | MEASURE CLEAR. Inches from the top-left corner. Up to eight stay up.
    auto num = [&](std::size_t i) { return i < parts.size() ? std::atof(parts[i].c_str()) : 0.0; };
    if (sub == "MEASURE") {
      if (upper(a) != "CLEAR") { reply = "MEASURE CLEAR"; return false; }
      g_game.measures.clear(); reply = "measurements cleared"; return true;
    }
    Measure m;
    m.kind = sub == "RINGS" ? "rings" : sub == "RULER" ? "ruler" : "move";
    m.a = {num(2), num(3)};
    if (parts.size() < 4) { reply = sub + ": needs a point (x y inches)"; return false; }
    if (sub == "RULER") {
      if (parts.size() < 6) { reply = "RULER x1 y1 x2 y2"; return false; }
      m.b = {num(4), num(5)};
    } else if (sub == "MOVE") {
      const double mv = parts.size() > 4 ? num(4) : 5;
      const double run = parts.size() > 5 ? num(5) : 6;
      m.radii = {mv, mv + run};
      m.player = g_game.turn;
    } else {
      if (parts.size() > 4) {
        std::istringstream in(parts[4]);
        std::string tok;
        while (std::getline(in, tok, ',')) if (std::atof(tok.c_str()) > 0) m.radii.push_back(std::atof(tok.c_str()));
      }
      if (m.radii.empty()) m.radii = {3, 6, 9, 12};
    }
    g_game.measures.push_back(m);
    if (g_game.measures.size() > 8) g_game.measures.erase(g_game.measures.begin());
    reply = m.kind + " placed";
    return true;
  }
  if (sub == "SECONDARY") {
    // SECONDARY P1|P2 ADD <name> | SCORE <n> <vp> | DISCARD <n> | CLEAR
    // The cards are the owner's; Warboy only keeps their names and points.
    const std::string who = upper(a);
    if (who != "P1" && who != "P2") { reply = "SECONDARY P1|P2 ADD <name> | SCORE <n> <vp> | DISCARD <n> | CLEAR"; return false; }
    auto& sc = g_game.sec[who == "P2" ? 1 : 0];
    const std::string what = upper(b);
    const std::string arg = parts.size() > 4 ? parts[4] : "";
    if (what == "ADD") {
      std::string name;
      for (std::size_t i = 4; i < parts.size(); ++i) name += (name.empty() ? "" : " ") + upper(parts[i]);
      if (name.empty()) { reply = "SECONDARY " + who + " ADD <card name>"; return false; }
      if (sc.active.size() >= 4) { reply = "four cards already: score or discard one first"; return false; }
      sc.active.push_back(name);
      reply = who + " draws " + name;
      return true;
    }
    if (what == "SCORE" || what == "DISCARD") {
      const int n = std::atoi(arg.c_str());
      if (n < 1 || n > static_cast<int>(sc.active.size())) { reply = "no card " + arg; return false; }
      const std::string name = sc.active[static_cast<std::size_t>(n - 1)];
      sc.active.erase(sc.active.begin() + (n - 1));
      if (what == "DISCARD") { reply = who + " discards " + name; return true; }
      const int vp = std::clamp(parts.size() > 5 ? std::atoi(parts[5].c_str()) : 0, 0, 100);
      int& total = who == "P2" ? g_game.vp2 : g_game.vp1;
      const int before = total;
      total = std::min(vpCap(g_plan), total + vp);
      sc.scored.push_back({name, total - before});
      g_game.lastScore = (who == "P2" ? g_plan.p2 : g_plan.p1) + " SCORES " + name + " +" + std::to_string(total - before);
      g_game.scoredAt = Clock::now();
      reply = g_game.lastScore;
      return true;
    }
    if (what == "CLEAR") { sc.active.clear(); reply = who + " secondaries cleared"; return true; }
    reply = "SECONDARY P1|P2 ADD <name> | SCORE <n> <vp> | DISCARD <n> | CLEAR";
    return false;
  }
  if (sub == "SETUP") {
    // SETUP ON|OFF|TOGGLE: the mission card on the table while the armies
    // and terrain go down. BATTLE (or SETUP OFF) starts the game.
    if (!toggleWord(a, g_game.setup, g_game.setup)) { reply = "SETUP ON|OFF|TOGGLE"; return false; }
    reply = g_game.setup ? "setting up" : "battle";
    return true;
  }
  if (sub == "DEEPSTRIKE") {
    // DEEPSTRIKE x y: nothing can arrive within 9" of here.
    if (parts.size() < 4) { reply = "DEEPSTRIKE x y (inches)"; return false; }
    Measure m; m.kind = "deepstrike"; m.a = {std::atof(a.c_str()), std::atof(b.c_str())}; m.radii = {9};
    g_game.measures.push_back(m);
    if (g_game.measures.size() > 8) g_game.measures.erase(g_game.measures.begin());
    reply = "deep strike ring placed";
    return true;
  }
  if (sub == "MISSION") {
    // MISSION <deployment> <primary> [<rule>] -- from the owner's 40k files.
    if (parts.size() < 4) {
      reply = "MISSION <deployment> <primary> [<rule>] (files in " + (planDir() / "40k").string() + ")";
      return false;
    }
    const std::string name = "mission:" + a + "|" + b + "|" + (parts.size() > 4 ? parts[4] : "-");
    Plan plan;
    if (!loadAndFit(name, plan, reply)) return false;
    adoptSystemColours(plan, g_game);
    g_plan = std::move(plan);
    g_game.highlight = 0;
    g_game.measures.clear();
    g_game.awarded.clear(); g_game.lastScore.clear();
    reply = "mission " + g_plan.title;
    return true;
  }
  if (sub == "LAYOUT") {
    // LAYOUT <name> | OFF -- a terrain layout's ruins over a 40k plan.
    if (a.empty()) { reply = "LAYOUT <name> | OFF (files in " + dir40k("layouts").string() + ")"; return false; }
    if (!is40k(g_plan)) { reply = "terrain layouts go on a 40k plan"; return false; }
    const std::string next = upper(a) == "OFF" ? "" : a;
    Plan plan;
    const std::string keep = g_game.layout;
    g_game.layout = next;
    if (!loadAndFit(g_plan.name, plan, reply)) { g_game.layout = keep; return false; }
    if (!next.empty() && plan.terrain.empty()) { g_game.layout = keep; reply = "no ruins in layout " + next; return false; }
    retainPlanState(plan, g_plan);
    g_plan = std::move(plan);
    reply = next.empty() ? "layout off" : "layout " + next;
    return true;
  }
  if (sub == "PHASE") {
    // PHASE NEXT runs on into the next turn after the last phase.
    const std::string k = upper(a);
    const int count = phaseCount(g_plan);
    if (k == "NEXT" || k.empty()) {
      if (g_game.over) { reply = "the game is over: NEW GAME, or PHASE BACK"; return false; }
      if (g_game.phase + 1 >= count) advanceTurn(g_plan, g_game);
      else ++g_game.phase;
    } else if (k == "BACK" || k == "PREV") {
      // Back past the start of a turn undoes the turn too: an accidental
      // NEXT PHASE off the end is always one press from where it was.
      if (g_game.over) { g_game.over = false; if (!g_plan.scoreAtCommand) unscoreTurn(g_game, g_game.round, g_game.turn); }
      else if (g_game.phase > 0) --g_game.phase;
      else {
        // Leaving a turn whose Command phase scored takes that score back.
        if (g_plan.scoreAtCommand) unscoreTurn(g_game, g_game.round, g_game.turn);
        if (g_game.turn == 2) { g_game.turn = 1; g_game.phase = count - 1; if (!g_plan.scoreAtCommand) unscoreTurn(g_game, g_game.round, 1); }
        else if (g_game.round > 1) { --g_game.round; g_game.turn = 2; g_game.phase = count - 1; if (!g_plan.scoreAtCommand) unscoreTurn(g_game, g_game.round, 2); }
      }
    } else if (!k.empty() && std::isdigit(static_cast<unsigned char>(k[0]))) {
      g_game.phase = std::clamp(std::atoi(k.c_str()) - 1, 0, count - 1);
    } else {
      int found = -1;
      for (int i = 0; i < count; ++i) if (std::string(phaseAt(g_plan, i).name).rfind(k, 0) == 0) found = i;
      if (found < 0) {
        reply = is40k(g_plan) ? "PHASE NEXT | BACK | <n> | COMMAND|MOVEMENT|SHOOTING|CHARGE|FIGHT"
                              : "PHASE NEXT | BACK | <n> | START|HERO|MOVEMENT|SHOOTING|CHARGE|COMBAT|END";
        return false;
      }
      g_game.phase = found;
    }
    reply = g_game.over ? std::string("GAME OVER") : std::string(phaseAt(g_plan, g_game.phase).name);
    return true;
  }
  if (sub == "TURN") {
    const std::string k = upper(a);
    if (k == "P1" || k == "P2") { bankClock(g_game); g_game.turn = k == "P2" ? 2 : 1; g_game.phase = 0; }
    else if (k == "NEXT" || k.empty()) {
      if (g_game.over) { reply = "the game is over: NEW GAME, or PHASE BACK"; return false; }
      advanceTurn(g_plan, g_game);
    }
    else { reply = "TURN P1 | P2 | NEXT"; return false; }
    reply = g_game.over ? std::string("game over") : "round " + std::to_string(g_game.round) + " player " + std::to_string(g_game.turn);
    return true;
  }
  if (sub == "VP") {
    int& v = upper(a) == "P2" ? g_game.vp2 : g_game.vp1;
    if ((upper(a) != "P1" && upper(a) != "P2") || !step(b, v, 0, vpCap(g_plan))) { reply = "VP P1|P2 + | - | <n>"; return false; }
    reply = "vp " + std::to_string(g_game.vp1) + " - " + std::to_string(g_game.vp2);
    return true;
  }
  if (sub == "LAYER") {
    const std::string k = upper(a);
    bool* layer = k == "TERRITORIES" ? &g_game.showTerritories : k == "DEPLOYMENT" ? &g_game.showDeployment
                : k == "TERRAIN" ? &g_game.showTerrain : k == "PLACES" ? &g_game.showPlaces
                : k == "OBJECTIVES" ? &g_game.showObjectives : k == "STRIP" ? &g_game.showStrip
                : k == "GUIDES" || k == "LINES" ? &g_game.showGuides
                : k == "LABELS" ? &g_game.showLabels : k == "EDGE" ? &g_game.showEdge : nullptr;
    if (k == "ALL" || k == "NONE") {
      const bool on = k == "ALL";
      g_game.showGuides = g_game.showTerritories = g_game.showDeployment = g_game.showTerrain = on;
      g_game.showPlaces = g_game.showObjectives = g_game.showStrip = on;
      reply = on ? "all layers on" : "all layers off";
      return true;
    }
    if (!layer || !toggleWord(b, *layer, *layer)) { reply = "LAYER GUIDES|TERRITORIES|DEPLOYMENT|TERRAIN|PLACES|OBJECTIVES|STRIP|LABELS|EDGE|ALL|NONE [ON|OFF|TOGGLE]"; return false; }
    reply = k + (*layer ? " on" : " off");
    return true;
  }
  if (sub == "CALIBRATE") { if (!toggleWord(a, g_game.calibrate, g_game.calibrate)) { reply = "CALIBRATE ON|OFF|TOGGLE"; return false; } reply = g_game.calibrate ? "calibrate on" : "calibrate off"; return true; }
  if (sub == "ANIMATE") { if (!toggleWord(a, g_game.animate, g_game.animate)) { reply = "ANIMATE ON|OFF|TOGGLE"; return false; } reply = g_game.animate ? "terrain animated" : "terrain still"; return true; }
  if (sub == "BENCH") {
    // Milliseconds per frame at 1920 x 1080, rendered here, for finding
    // what is slow. Unlocks while it renders (render takes the lock itself).
    const int frames = std::clamp(a.empty() ? 30 : std::atoi(a.c_str()), 1, 600);
    g_mutex.unlock();
    std::vector<std::uint8_t> buf(1920u * 1080u * 4u);
    const auto start = Clock::now();
    for (int i = 0; i < frames; ++i) render(buf.data(), 1920, 1080, i / 60.0);
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / frames;
    g_mutex.lock();
    char text[64];
    std::snprintf(text, sizeof(text), "%.1f ms a frame (%d frames, 1920x1080)", ms, frames);
    reply = text;
    return true;
  }
  if (sub == "GROUND") {
    const std::string k = upper(a);
    static const char* kinds[] = {"NONE", "GRASS", "SWAMP", "ASH", "LAVA", "SNOW", "SAND"};
    if (k == "+" || k == "-") {
      g_game.groundLevel = std::clamp(g_game.groundLevel + (k == "+" ? 0.15 : -0.15), 0.1, 1.5);
      reply = "ground level " + std::to_string(static_cast<int>(std::lround(g_game.groundLevel * 100))) + "%";
      return true;
    }
    if (k == "OFF") { g_game.ground = "none"; reply = "ground off"; return true; }
    for (const char* kind : kinds) {
      if (k == kind) {
        std::string lower(kind);
        for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        g_game.ground = lower;
        reply = "ground " + lower;
        return true;
      }
    }
    reply = "GROUND NONE|GRASS|SWAMP|ASH|LAVA|SNOW|SAND|OFF | GROUND + | GROUND -";
    return false;
  }
  if (sub == "STYLE") {
    const std::string k = upper(a);
    const int st = k == "CLEAN" ? 0 : k == "ARCANE" ? 1 : k == "EMBER" ? 2 : k == "NEXT" ? (g_game.style + 1) % 3 : -1;
    if (st < 0) { reply = "STYLE CLEAN | ARCANE | EMBER | NEXT"; return false; }
    g_game.style = st;
    reply = st == 0 ? "style clean" : st == 1 ? "style arcane" : "style ember";
    return true;
  }
  if (sub == "SPEED" || sub == "BRIGHT") {
    double& v = sub == "SPEED" ? g_game.speed : g_game.bright;
    const double lo = sub == "SPEED" ? 0.0 : 0.1, hi = sub == "SPEED" ? 3.0 : 1.0;
    if (a == "+") v = std::min(hi, v + (sub == "SPEED" ? 0.25 : 0.1));
    else if (a == "-") v = std::max(lo, v - (sub == "SPEED" ? 0.25 : 0.1));
    else if (!a.empty() && (std::isdigit(static_cast<unsigned char>(a[0])) || a[0] == '.')) v = std::clamp(std::atof(a.c_str()) / 100.0, lo, hi);
    else { reply = sub + " + | - | <percent>"; return false; }
    reply = (sub == "SPEED" ? "speed " : "brightness ") + std::to_string(static_cast<int>(std::lround(v * 100))) + "%";
    return true;
  }
  if (sub == "COLOR" || sub == "COLOUR") {
    const std::string k = upper(a);
    Rgb* target = k == "P1" ? &g_game.c1 : k == "P2" ? &g_game.c2 : k == "HIGHLIGHT" ? &g_game.hl : k == "PLACE" ? &g_game.place : nullptr;
    unsigned int r = 0, g = 0, bl = 0;
    if (!target || b.size() != 7 || b[0] != '#' || std::sscanf(b.c_str() + 1, "%02x%02x%02x", &r, &g, &bl) != 3) {
      reply = "COLOR P1|P2|HIGHLIGHT|PLACE #rrggbb"; return false;
    }
    *target = {static_cast<int>(r), static_cast<int>(g), static_cast<int>(bl)};
    reply = "colour " + k + " " + b;
    return true;
  }
  if (sub == "RESET") {
    const Game fresh;
    g_game.round = fresh.round; g_game.turn = fresh.turn; g_game.vp1 = g_game.vp2 = 0; g_game.highlight = 0;
    g_game.phase = 0;
    g_game.cp1 = g_game.cp2 = 0;
    g_game.clock1 = g_game.clock2 = 0; g_game.clockSince = Clock::now();
    g_game.measures.clear();
    g_game.awarded.clear(); g_game.lastScore.clear();
    for (auto& t : g_game.tac) { t.slot = t.pick = -1; t.done.clear(); }
    for (auto& sc : g_game.sec) { sc.active.clear(); sc.scored.clear(); }
    g_game.over = false;
    for (auto& pl : g_plan.places) pl.owner = 0;
    for (auto& o : g_plan.objectives) o.owner = 0;
    visitElements(g_plan, g_game, [](const char*, std::size_t, const std::string&, bool& muted) { muted = false; });
    reply = "game reset";
    return true;
  }
  reply = "STATUS | PLAN <name> | MISSION <dep> <primary> [rule] | LAYOUT <name>|OFF | SECONDARY | DEEPSTRIKE x y | RELOAD | BOARD 60X44|44X30 | SIZE | OBJ | HIGHLIGHT | MUTE | ROUND | TURN | VP | LAYER | CALIBRATE | ANIMATE | SCORE | TACTIC | CP | CLOCK | PLACE | RINGS | RULER | MOVE | MEASURE CLEAR | GROUND | STYLE | SPEED | BRIGHT | COLOR | RESET";
  return false;
}

void ensureBooted();

// THE DESK: the phone remote's controls, as the Cue Inspector draws them while
// a Tabletop cue is selected. Rebuilt every frame from the live game.
std::vector<deckboy::extras::DeskControl> deskControls() {
  ensureBooted();
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<deckboy::extras::DeskControl> out;
  auto add = [&](const std::string& section, int row, const std::string& label, const std::string& cmd, bool lit = false) {
    out.push_back({section, row, label, cmd, lit});
  };
  const Plan& pl = g_plan;
  const Game& g = g_game;
  const std::string a = pl.p1.substr(0, 3), d = pl.p2.substr(0, 3);
  std::string sec = "GAME - " + pl.title;
  add(sec, 0, "ROUND -", "TABLE ROUND -");
  add(sec, 0, "ROUND " + std::to_string(g.round), "");
  add(sec, 0, "ROUND +", "TABLE ROUND +");
  add(sec, 1, (g.turn == 2 ? pl.p2 : pl.p1) + " TURN", "");
  add(sec, 1, "NEXT TURN", "TABLE TURN NEXT", true);
  add(sec, 4, "< PHASE", "TABLE PHASE BACK");
  add(sec, 4, g.over ? std::string("GAME OVER") : std::string(phaseAt(pl, g.phase).name), "");
  add(sec, 4, "NEXT PHASE >", "TABLE PHASE NEXT", true);
  add(sec, 2, a + " -", "TABLE VP P1 -");
  add(sec, 2, a + " " + std::to_string(g.vp1), "");
  add(sec, 2, a + " +", "TABLE VP P1 +");
  add(sec, 3, d + " -", "TABLE VP P2 -");
  add(sec, 3, d + " " + std::to_string(g.vp2), "");
  add(sec, 3, d + " +", "TABLE VP P2 +");
  sec = "OBJECTIVES - click: who holds it";
  for (std::size_t i = 0; i < pl.objectives.size(); ++i) {
    const auto& o = pl.objectives[i];
    const std::string n = std::to_string(i + 1);
    const std::string who = o.owner == 1 ? pl.p1 : o.owner == 2 ? pl.p2 : "-";
    add(sec, static_cast<int>(i), o.label + "  " + (o.kind.empty() ? std::string() : o.kind + "  ") + who,
        o.muted ? "" : "TABLE OBJ " + n + " NEXT", !o.muted && o.owner != 0);
    add(sec, static_cast<int>(i), o.muted ? "MUTED" : g.highlight == static_cast<int>(i) + 1 ? "LIT" : "LIGHT", o.muted ? "" : "TABLE HIGHLIGHT " + n,
        g.highlight == static_cast<int>(i) + 1);
  }
  sec = "ELEMENTS - mute / restore";
  int elementRow = 0;
  visitElements(pl, g, [&](const char* kind, std::size_t index, const std::string& label, bool muted) {
    add(sec, elementRow++, (muted ? "RESTORE " : "MUTE ") + label,
        "TABLE MUTE " + std::string(kind) + " " + std::to_string(index) + " TOGGLE", muted);
  });
  add(sec, elementRow, "RESTORE ALL ELEMENTS", "TABLE MUTE ALL OFF");
  sec = "COMMAND POINTS AND CLOCK";
  add(sec, 0, a + " CP -", "TABLE CP P1 -");
  add(sec, 0, a + " " + std::to_string(g.cp1), "");
  add(sec, 0, a + " CP +", "TABLE CP P1 +");
  add(sec, 1, d + " CP -", "TABLE CP P2 -");
  add(sec, 1, d + " " + std::to_string(g.cp2), "");
  add(sec, 1, d + " CP +", "TABLE CP P2 +");
  add(sec, 2, g.clockOn ? "CLOCK ON" : "CLOCK OFF", "TABLE CLOCK TOGGLE", g.clockOn);
  add(sec, 2, g.clockPaused ? "RESUME" : "PAUSE", "TABLE CLOCK PAUSE", g.clockOn && !g.clockPaused);
  add(sec, 2, "RESET CLOCKS", "TABLE CLOCK RESET");
  if (g.clockOn) add(sec, 3, a + " " + clockText(clockSeconds(g, 1)) + "   " + d + " " + clockText(clockSeconds(g, 2)), "");
  if (!pl.places.empty()) {
    sec = "PLACES OF POWER - click: who holds it";
    for (std::size_t i = 0; i < pl.places.size(); ++i) {
      const auto& p = pl.places[i];
      const std::string who = p.owner == 1 ? pl.p1 : p.owner == 2 ? pl.p2 : "-";
      add(sec, static_cast<int>(i), std::to_string(i + 1) + "  " + p.label + "  " + who,
          p.muted ? "" : "TABLE PLACE " + std::to_string(i + 1) + " NEXT", !p.muted && p.owner != 0);
    }
  }
  if (!g.measures.empty()) {
    sec = "MEASURING (place them from the phone's map)";
    add(sec, 0, std::to_string(g.measures.size()) + " ON THE TABLE", "");
    add(sec, 0, "CLEAR", "TABLE MEASURE CLEAR");
  }
  sec = "SCORING";
  if (pl.scores.empty()) add(sec, 0, "MANUAL VP - use this battlepack's cards", "");
  else add(sec, 0, g.autoScore ? "AUTO-SCORE ON (end of each turn)" : "AUTO-SCORE OFF", "TABLE SCORE AUTO TOGGLE", g.autoScore);
  for (std::size_t i = 0; i < pl.scores.size(); ++i) {
    const auto& sc = pl.scores[i];
    if (sc.rule != "MANUAL") continue;
    add(sec, 1 + static_cast<int>(i), sc.text, "");
    add(sec, 1 + static_cast<int>(i), a + " +" + std::to_string(sc.vp), "TABLE SCORE " + std::to_string(i + 1) + " P1");
    add(sec, 1 + static_cast<int>(i), d + " +" + std::to_string(sc.vp), "TABLE SCORE " + std::to_string(i + 1) + " P2");
  }
  for (int who = 0; pl.pack.empty() && !is40k(pl) && who < 2; ++who) {
    const auto& t = g.tac[who];
    const std::string P = who ? "P2" : "P1", name = who ? pl.p2 : pl.p1;
    sec = "BATTLE TACTICS - " + name + " (" + std::to_string(t.done.size()) + " done)";
    for (int slot = 0; slot < 2; ++slot) {
      const int cd = t.card[slot];
      add(sec, slot * 2, "CARD " + std::to_string(slot + 1) + ": " + (cd >= 0 ? kTacticCards[cd].name : "NONE"),
          "TABLE TACTIC CARD " + P + " " + std::to_string(slot + 1) + " NEXT");
      if (cd >= 0)
        for (int k = 0; k < 3; ++k)
          add(sec, slot * 2 + 1, kTacticCards[cd].tactics[k].name, "TABLE TACTIC PICK " + P + " " + std::to_string(slot + 1) + " " + std::to_string(k + 1),
              t.slot == slot && t.pick == k);
    }
    if (t.slot >= 0 && t.pick >= 0 && t.card[t.slot] >= 0) {
      const auto& tc = kTacticCards[t.card[t.slot]].tactics[t.pick];
      add(sec, 5, std::string(tc.type) + " +" + std::to_string(tc.vp), "");
      add(sec, 5, "COMPLETED", "TABLE TACTIC DONE " + P, true);
      add(sec, 5, "CLEAR", "TABLE TACTIC PICK " + P + " NONE");
    }
  }
  for (int who = 0; is40k(pl) && who < 2; ++who) {
    const auto& sc = g.sec[who];
    const std::string P = who ? "P2" : "P1", name = who ? pl.p2 : pl.p1;
    sec = "SECONDARIES - " + name + " (add cards from the phone)";
    if (sc.active.empty()) add(sec, 0, "NO CARDS IN HAND", "");
    for (std::size_t i = 0; i < sc.active.size(); ++i) {
      const std::string n = std::to_string(i + 1);
      add(sec, static_cast<int>(i), sc.active[i], "");
      for (int vp : {2, 3, 5})
        add(sec, static_cast<int>(i), "+" + std::to_string(vp), "TABLE SECONDARY " + P + " SCORE " + n + " " + std::to_string(vp));
      add(sec, static_cast<int>(i), "DISCARD", "TABLE SECONDARY " + P + " DISCARD " + n);
    }
  }
  sec = "LIGHT UP";
  add(sec, 0, "TERRITORIES", "TABLE LAYER TERRITORIES TOGGLE", g.showTerritories);
  add(sec, 0, "TERRAIN", "TABLE LAYER TERRAIN TOGGLE", g.showTerrain);
  add(sec, 0, "PLACES", "TABLE LAYER PLACES TOGGLE", g.showPlaces);
  add(sec, 1, "OBJECTIVES", "TABLE LAYER OBJECTIVES TOGGLE", g.showObjectives);
  add(sec, 1, "ROUND STRIP", "TABLE LAYER STRIP TOGGLE", g.showStrip);
  add(sec, 1, "LABELS", "TABLE LAYER LABELS TOGGLE", g.showLabels);
  add(sec, 2, "ANIMATE", "TABLE ANIMATE TOGGLE", g.animate);
  add(sec, 3, "PHASE EDGE", "TABLE LAYER EDGE TOGGLE", g.showEdge);
  add(sec, 3, "BOARD LINES", "TABLE LAYER GUIDES TOGGLE", g.showGuides);
  add(sec, 3, "DEPLOYMENT", "TABLE LAYER DEPLOYMENT TOGGLE", g.showDeployment);
  add(sec, 2, "CALIBRATE", "TABLE CALIBRATE TOGGLE", g.calibrate);
  add(sec, 2, "NEW GAME", "TABLE RESET");
  sec = "GROUND";
  const char* grounds[] = {"none", "grass", "swamp", "ash", "lava", "snow", "sand"};
  for (int i = 0; i < 7; ++i) {
    std::string label = i == 0 ? "BARE" : upper(grounds[i]);
    add(sec, i / 4, label, std::string("TABLE GROUND ") + (i == 0 ? "OFF" : upper(grounds[i])), g.ground == grounds[i]);
  }
  add(sec, 1, "FAINTER", "TABLE GROUND -");
  add(sec, 2, "STRONGER", "TABLE GROUND +");
  sec = "LOOK";
  add(sec, 0, "CLEAN", "TABLE STYLE CLEAN", g.style == 0);
  add(sec, 0, "ARCANE", "TABLE STYLE ARCANE", g.style == 1);
  add(sec, 0, "EMBER", "TABLE STYLE EMBER", g.style == 2);
  add(sec, 1, "SLOWER", "TABLE SPEED -");
  add(sec, 1, "SPEED " + std::to_string(static_cast<int>(std::lround(g.speed * 100))) + "%", "");
  add(sec, 1, "FASTER", "TABLE SPEED +");
  add(sec, 2, "DIMMER", "TABLE BRIGHT -");
  add(sec, 2, "BRIGHT " + std::to_string(static_cast<int>(std::lround(g.bright * 100))) + "%", "");
  add(sec, 2, "BRIGHTER", "TABLE BRIGHT +");
  add(sec, 3, "FREEZE", "TABLE SPEED 0", g.speed == 0.0);
  add(sec, 3, "NORMAL", "TABLE SPEED 100", g.speed == 1.0);
  sec = "PITCHED BATTLE";
  add(sec, 0, "60\" X 44\"", pl.pack.empty() ? "TABLE BOARD 60X44" : "", pl.pack.empty() && g.board60);
  add(sec, 0, "44\" X 30\"", pl.pack.empty() ? "TABLE BOARD 44X30" : "", pl.pack.empty() && !g.board60);
  int row = 1;
  for (const auto& b : planCatalog()) if (b.pack.empty() && b.system != "40k")
    add(sec, row++, b.title, std::string("TABLE PLAN ") + b.slug, pl.name == b.slug);
  sec = "40K (missions are set up from the phone)";
  row = 0;
  for (const auto& b : planCatalog()) if (b.system == "40k")
    add(sec, row++, b.title, std::string("TABLE PLAN ") + b.slug, pl.name == b.slug);
  for (const auto& b : planCatalog()) if (!b.pack.empty()) {
    const std::string group = "SPEARHEAD 30\" X 22\" - " + b.pack;
    if (sec != group) { sec = group; row = 0; }
    add(sec, row++, b.title, std::string("TABLE PLAN ") + b.slug, pl.name == b.slug);
  }
  return out;
}

std::string phonePage() {
  return
#include "table_page.inc"
  ;
}

bool tableCommand(const std::vector<std::string>& parts, std::string& reply);

// The first use loads the example layout, once, whichever comes first.
void ensureBooted() {
  static const bool booted = [] {
    std::string why;
    Plan plan;
    if (loadPlan(kBuiltinPlans[0].slug, plan, why)) {
      fitBoard(plan, true);
      std::lock_guard<std::mutex> lock(g_mutex);
      g_plan = std::move(plan);
    }
    // For testing renders headlessly: TABLE commands to apply at start,
    // separated by ';' (e.g. "OBJ 1 P1;HIGHLIGHT 3").
    if (const char* script = std::getenv("DECKBOY_TABLE_SCRIPT")) {
      std::istringstream in(script);
      std::string one;
      while (std::getline(in, one, ';')) {
        std::istringstream words(one);
        std::vector<std::string> parts {"TABLE"};
        std::string w;
        while (words >> w) parts.push_back(w);
        std::string reply;
        tableCommand(parts, reply);
      }
    }
    return true;
  }();
  (void)booted;
}

const Registration kGenerator(Generator {"tabletop", "Tabletop (projected table)",
  [](std::uint8_t* rgba, int w, int h, double t) {
    ensureBooted();
    render(rgba, w, h, t);
  }, "table", [] { return deskControls(); }, true});
const Registration kCommand(Command {"TABLE", true, [](const std::vector<std::string>& parts, std::string& reply) {
  ensureBooted();
  return tableCommand(parts, reply);
}});
const Registration kPage(WebPage {"table", "text/html; charset=utf-8", [] { return phonePage(); }});
// Its script is a file of its own: the phone server's policy refuses inline script.
const Registration kScript(WebPage {"table.js", "application/javascript; charset=utf-8", [] {
  return std::string(
#include "table_js.inc"
  );
}});

}  // namespace
