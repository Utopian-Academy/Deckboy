// SDL-free regression check for actual plan loading, commands and drawing.
#include <stdexcept>
#include "../native/extras/tabletop/tabletop.cpp"

namespace deckboy::core {
std::filesystem::path Paths::stateDir() {
  return std::filesystem::temp_directory_path() / "deckboy-spearhead-check-no-overrides";
}
}

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
  try {
    const fs::path output = argc > 1 ? fs::path(argv[1]) : fs::path("spearhead-check");
    fs::create_directories(output);
    const std::array<const char*, 6> slugs {{
      "spearhead-fire-jade-aqshy", "spearhead-fire-jade-ghyran",
      "spearhead-sand-bone-ossia", "spearhead-sand-bone-dolorum",
      "spearhead-city-ash-crossroads", "spearhead-city-ash-bastion"}};
    const std::array<std::size_t, 6> counts {{5, 5, 4, 5, 5, 5}};
    const std::array<std::size_t, 6> polygons {{0, 0, 0, 0, 4, 1}};
    auto command = [](std::initializer_list<std::string> words) {
      std::string reply;
      return tableCommand(std::vector<std::string>(words), reply);
    };
    ensureBooted();
    auto framePixels = [] {
      std::vector<std::uint8_t> pixels(600 * 440 * 4);
      render(pixels.data(), 600, 440, 0);
      return pixels;
    };
    for (std::size_t n = 0; n < slugs.size(); ++n) {
      g_game = Game {};
      require(command({"TABLE", "PLAN", slugs[n]}), "preset did not load");
      require(g_plan.w == 30 && g_plan.h == 22, "printed board was rescaled");
      require(g_plan.objectives.size() == counts[n], "wrong objective count");
      require(static_cast<std::size_t>(std::count_if(g_plan.objectives.begin(), g_plan.objectives.end(),
        [](const Objective& ob) { return !ob.poly.empty(); })) == polygons[n], "wrong objective shapes");
      for (const auto& ob : g_plan.objectives) {
        require(ob.p.x >= 0 && ob.p.x <= 30 && ob.p.y >= 0 && ob.p.y <= 22, "objective centre outside board");
        if (!ob.poly.empty()) require(inside(ob.poly, ob.p.x, ob.p.y), "polygon label outside objective");
      }
      require(!command({"TABLE", "BOARD", "60X44"}), "printed board accepted pitched resize");
      require(!command({"TABLE", "SIZE", "44", "30"}), "printed layout accepted custom resize");
      require(command({"TABLE", "SIZE", "SPEARHEAD"}), "Spearhead size rejected");
      require(g_plan.w == 30 && g_plan.h == 22, "Spearhead size must be 30 x 22");
      require(command({"TABLE", "OBJ", "1", "P1"}), "objective ownership failed");
      require(command({"TABLE", "RELOAD"}), "reload failed");
      require(g_plan.objectives[0].owner == 1, "reload lost objective ownership");
      require(!command({"TABLE", "TACTIC", "CARD", "P1", "1", "NEXT"}), "GHB tactics leaked into Spearhead");
      require(!command({"TABLE", "SCORE", "AUTO", "ON"}), "board-only preset advertised scoring rules");
      require(command({"TABLE", "VP", "P1", "7"}), "manual VP failed");
      require(command({"TABLE", "TURN", "NEXT"}), "next turn failed");
      require(g_game.vp1 == 7 && g_game.lastScore.empty(), "board-only turn changed points");
      g_game.showStrip = g_game.showEdge = g_game.showLabels = false;
      g_plan.objectives[0].changed = {};
      const auto baseline = framePixels();
      require(command({"TABLE", "HIGHLIGHT", "1"}), "highlight failed");
      require(command({"TABLE", "MUTE", "OBJECTIVE", "1", "ON"}), "mute failed");
      require(g_game.highlight == 0, "muted highlight remained active");
      require(!command({"TABLE", "OBJ", "1", "NEXT"}), "muted ownership changed");
      require(!command({"TABLE", "HIGHLIGHT", "1"}), "muted objective highlighted");
      require(command({"TABLE", "RELOAD"}) && g_plan.objectives[0].muted, "reload revived muted objective");
      const auto muted = framePixels();
      require(muted != baseline, "mute left objective visible");
      const Plan saved = g_plan;
      g_plan.objectives.erase(g_plan.objectives.begin());
      require(framePixels() == muted, "muted objective left a visual trace");
      g_plan = saved;
      require(command({"TABLE", "MUTE", "OBJECTIVE", "1", "OFF"}), "restore failed");
      require(g_plan.objectives[0].owner == 1 && framePixels() == baseline, "restore lost ownership or geometry");
      require(command({"TABLE", "LAYER", "GUIDES", "OFF"}), "board lines toggle failed");
      require(g_game.showTerritories && g_game.showObjectives, "board lines changed other layers");
      const auto noLines = framePixels();
      const auto lines = g_plan.lines;
      g_plan.lines.clear(); g_game.showGuides = true;
      require(framePixels() == noLines && noLines != baseline, "board lines left a trace or did not hide");
      g_plan.lines = lines;
      std::vector<std::uint8_t> pixels(1200 * 880 * 4);
      render(pixels.data(), 1200, 880, 0);
      std::ofstream frame(output / (std::string(slugs[n]) + ".ppm"), std::ios::binary);
      frame << "P6\n1200 880\n255\n";
      for (std::size_t i = 0; i < pixels.size(); i += 4) frame.write(reinterpret_cast<const char*>(&pixels[i]), 3);
      std::ofstream(output / (std::string(slugs[n]) + ".json")) << statusJson();
    }
    Plan plan; std::string why;
    require(loadPlan(slugs[1], plan, why), "Ghyran missing");
    require(plan.objectives[1].p.x == .75 && plan.objectives[3].p.x == 29.25, "Ghyran clipped centres moved");
    require(loadPlan(slugs[2], plan, why), "Ossia missing");
    require(plan.objectives[1].p.x == 17.5 && plan.objectives[1].p.y == 8.75, "Ossia offsets measured from wrong edge");
    require(plan.objectives[2].p.y == 13.25, "Ossia bottom offset inverted");
    require(loadPlan(slugs[3], plan, why), "Dolorum missing");
    require(plan.objectives[2].range == 3.5 && plan.objectives[0].range == 2.5, "diameters confused with radii");
    require(command({"TABLE", "PLAN", "into-the-fire"}), "existing battleplan broken");
    require(command({"TABLE", "BOARD", "44X30"}) && g_plan.w == 44 && g_plan.h == 30, "pitched scaling broken");
    require(command({"TABLE", "BOARD", "60X44"}) && g_plan.w == 60 && g_plan.h == 44, "pitched scale restore broken");
    require(command({"TABLE", "MUTE", "OBJECTIVE", "1", "ON"}), "pitched mute failed");
    require(command({"TABLE", "BOARD", "44X30"}) && g_plan.objectives[0].muted, "resize revived muted objective");
    require(command({"TABLE", "LAYER", "ALL"}) && g_plan.objectives[0].muted, "all layers revived muted objective");
    require(command({"TABLE", "RESET"}) && !g_plan.objectives[0].muted, "new game did not restore elements");
    const auto controls = deskControls();
    int pitched = 0, spearhead = 0;
    for (const auto& control : controls) if (control.command.rfind("TABLE PLAN ", 0) == 0) {
      if (control.command.find("40k-") != std::string::npos) {
        require(control.section.rfind("40K", 0) == 0, "40k plan outside its group");
      } else if (control.command.find("spearhead-") != std::string::npos) {
        require(control.section.rfind("SPEARHEAD", 0) == 0, "Spearhead group missing"); ++spearhead;
      } else { require(control.section == "PITCHED BATTLE", "pitched map mislabeled Spearhead"); ++pitched; }
    }
    require(pitched == 12 && spearhead == 6, "selector lost boards");
    Plan scoring;
    Objective active; active.owner = 1; active.kind = "TEST";
    scoring.objectives.push_back(active);
    scoring.scores = {{1, "AT_LEAST", "1", ""}, {2, "MORE", "", ""}, {3, "KIND", "TEST", ""}};
    require(autoScoreFor(scoring, 1) == 6, "scoring fixture failed");
    scoring.objectives[0].muted = true;
    require(autoScoreFor(scoring, 1) == 0, "muted objective scored");
    scoring.objectives[0].muted = false;
    require(autoScoreFor(scoring, 1) == 6, "restored objective did not score");
    // Every element family must vanish completely, in every drawing style.
    g_plan = Plan{}; g_game = Game{};
    g_game.showStrip = g_game.showEdge = false;
    g_plan.objectives.push_back(active);
    g_plan.objectives[0].p = {20,15};
    g_plan.lines.push_back({{0,0},{30,22}});
    const std::vector<Pt> box {{1,1},{8,1},{8,8},{1,8}};
    g_plan.territories.push_back({1,box}); g_plan.deployments.push_back({2,box});
    g_plan.terrain.push_back({"water",box,false,{}});
    g_plan.markers.push_back({{15,15},"T",0}); g_plan.places.push_back({{30,20},"P",1});
    g_game.measures.push_back({"ruler",{5,5},{25,20},{},0});
    for (const char* type : {"OBJECTIVE","GUIDE","TERRITORY","DEPLOYMENT","TERRAIN","MARKER","PLACE","MEASURE"})
      require(command({"TABLE", "MUTE", type, "1", "ON"}), "element family mute failed");
    for (int style = 0; style < 3; ++style) {
      g_game.style = style;
      const auto black = framePixels();
      for (std::size_t i = 0; i < black.size(); ++i)
        require(black[i] == (i % 4 == 3 ? 255 : 0), "muted elements left pixels behind");
    }
    require(command({"TABLE", "MUTE", "ALL", "OFF"}), "restore all failed");
    visitElements(g_plan, g_game, [](const char*, std::size_t, const std::string&, bool muted) {
      require(!muted, "restore all left a mute");
    });
    require(!command({"TABLE", "MUTE", "OBJECTIVE", "99", "ON"}), "invalid index accepted");
    require(!command({"TABLE", "MUTE", "OBJECTIVE", "1", "invalid"}), "invalid action accepted");
    std::ofstream(output / "table.html") << phonePage();
    std::ofstream(output / "table.js") << deckboy::extras::findWebPage("table.js")->body();
    std::puts("PASS: six boards, separated selectors, mute/restore pixels and ownership, all element families, board lines, inactive scoring, reload/resize/reset, manual VP and render exports");
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
  }
}
