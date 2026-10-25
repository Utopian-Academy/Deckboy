// SDL-free check of the projected table: missions from the owner's
// files, the five phases, scoring timing, the five-round end, contested
// objectives, the VP cap, secondaries, layouts and deep-strike rings -- and
// that AoS plans are untouched. Writes rendered frames to review for a human.
#include <iostream>
#include <stdexcept>
#include "../native/extras/tabletop/tabletop.cpp"

static std::filesystem::path checkState;
namespace deckboy::core {
std::filesystem::path Paths::stateDir() {
  return checkState;
}
}

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void writeFile(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

void frame(const fs::path& path, int w, int h, double t) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
  render(pixels.data(), w, h, t);
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << w << ' ' << h << "\n255\n";
  for (std::size_t i = 0; i < pixels.size(); i += 4) out.write(reinterpret_cast<const char*>(&pixels[i]), 3);
}

int main(int argc, char** argv) {
  try {
    const fs::path output = argc > 1 ? fs::path(argv[1]) : fs::path("w40k-check");
    fs::create_directories(output);
    checkState = fs::absolute(output) / ("state-" + std::to_string(Clock::now().time_since_epoch().count()));
    const fs::path state = deckboy::core::Paths::stateDir();
    // The owner's files, as he would type them from his cards (test values).
    writeFile(state / "tabletop/40k/deployments/test-dep.txt",
              "table\t60\t44\ndeployment\t1\t0,30 60,30 60,44 0,44\ndeployment\t2\t0,0 60,0 60,14 0,14\n"
              "objective\t30\t22\t3\tA\nobjective\t10\t22\t3\tB\nobjective\t50\t22\t3\tC\n");
    writeFile(state / "tabletop/40k/primaries/test-primary.txt",
              "title\tTEST PRIMARY\nscorefrom\t2\nscoreat\tcommand\nscore\t5\tAT_LEAST\t1\tHold one\nscore\t5\tMORE\tHold more\n");
    writeFile(state / "tabletop/40k/rules/test-rule.txt", "title\tTEST RULE\n");
    writeFile(state / "tabletop/40k/layouts/test-layout.txt",
              "ruin\tA\t5,5 12,5 12,10 5,10\nruin\tB\t40,30 48,30 48,36 40,36\n");

    auto command = [](std::initializer_list<std::string> words) {
      std::string reply;
      return tableCommand(std::vector<std::string>(words), reply);
    };
    ensureBooted();

    // The example plan: a 40k plan, its colours, phases and 40mm markers.
    require(command({"TABLE", "PLAN", "40k-example"}), "40k example missing");
    require(is40k(g_plan) && phaseCount(g_plan) == 5, "40k phases not used");
    require(g_game.c1.r == k40kP1.r && g_game.c2.r == k40kP2.r, "40k colours not adopted");
    require(g_plan.objectives[0].marker > 0.78 && g_plan.objectives[0].marker < 0.79, "40mm marker missing");
    require(g_plan.terrain.size() == 6 && g_plan.terrain[0].label == "A", "example ruins missing");
    require(!command({"TABLE", "BOARD", "44X30"}), "40k plan accepted AoS rescale");
    require(!command({"TABLE", "TACTIC", "CARD", "P1", "1", "NEXT"}), "AoS tactics leaked into 40k");

    // Contested, and the cycle back round.
    for (int i = 0; i < 3; ++i) require(command({"TABLE", "OBJ", "1", "NEXT"}), "objective cycle failed");
    require(g_plan.objectives[0].owner == 3, "contested missing from the cycle");
    require(command({"TABLE", "OBJ", "1", "NEXT"}) && g_plan.objectives[0].owner == 0, "cycle did not return to nobody");
    require(command({"TABLE", "OBJ", "2", "CONTESTED"}) && g_plan.objectives[1].owner == 3, "CONTESTED word failed");

    // The VP cap.
    require(command({"TABLE", "VP", "P1", "150"}) && g_game.vp1 == 100, "VP cap not applied");
    require(command({"TABLE", "VP", "P1", "0"}), "VP reset failed");

    // Secondaries.
    require(command({"TABLE", "SECONDARY", "P1", "ADD", "Behind", "Enemy", "Lines"}), "secondary add failed");
    require(g_game.sec[0].active.size() == 1 && g_game.sec[0].active[0] == "BEHIND ENEMY LINES", "secondary name not kept");
    require(command({"TABLE", "SECONDARY", "P1", "SCORE", "1", "4"}) && g_game.vp1 == 4, "secondary score failed");
    require(g_game.sec[0].active.empty() && g_game.sec[0].scored.size() == 1, "scored card not moved");
    require(command({"TABLE", "SECONDARY", "P2", "ADD", "X"}) && command({"TABLE", "SECONDARY", "P2", "DISCARD", "1"}) &&
            g_game.sec[1].active.empty(), "discard failed");
    require(!command({"TABLE", "SECONDARY", "P2", "SCORE", "1", "5"}), "scored a card not in hand");

    // A mission from the owner's three files, and its scoring timing.
    require(command({"TABLE", "RESET"}), "reset failed");
    require(command({"TABLE", "MISSION", "test-dep", "test-primary", "test-rule"}), "mission did not assemble");
    require(is40k(g_plan) && g_plan.title == "TEST PRIMARY / test-dep" && g_plan.twist == "TEST RULE", "mission title/rule wrong");
    require(g_plan.scoreFrom == 2 && g_plan.scoreAtCommand && g_plan.scores.size() == 2, "primary rules not read");
    require(g_plan.deployments.size() == 2 && g_plan.objectives.size() == 3, "deployment not read");
    require(!command({"TABLE", "MISSION", "nope", "test-primary"}), "missing deployment accepted");
    require(command({"TABLE", "OBJ", "1", "P1"}), "claim failed");
    auto nextTurn = [&] { for (int i = 0; i < 5; ++i) require(command({"TABLE", "PHASE", "NEXT"}), "phase next failed"); };
    nextTurn();   // round 1, P1 -> P2: no scoring before round 2
    require(g_game.turn == 2 && g_game.round == 1 && g_game.vp1 == 0 && g_game.vp2 == 0, "scored before round 2");
    nextTurn();   // round 2, P1's Command phase: P1 holds one, more than P2 -> 10
    require(g_game.round == 2 && g_game.turn == 1 && g_game.phase == 0, "turn did not advance to round 2");
    require(g_game.vp1 == 10, "command-phase primary not scored");
    require(command({"TABLE", "PHASE", "BACK"}) && g_game.vp1 == 0 && g_game.round == 1 && g_game.turn == 2,
            "phase back did not take the command-phase score back");
    require(command({"TABLE", "PHASE", "NEXT"}) && g_game.vp1 == 10, "re-entering the turn did not rescore");
    // On to the end of round five.
    while (!g_game.over) {
      require(g_game.round <= 5, "game ran past round five");
      nextTurn();
    }
    require(g_game.round == 5 && g_game.turn == 2, "game ended at the wrong time");
    require(!command({"TABLE", "PHASE", "NEXT"}), "a finished game kept going");
    require(command({"TABLE", "PHASE", "BACK"}) && !g_game.over, "phase back did not reopen the last turn");
    require(g_game.vp1 <= 100 && g_game.vp2 <= 100, "cap broken by scoring");

    // A terrain layout over the mission, and off again.
    require(command({"TABLE", "LAYOUT", "test-layout"}) && g_plan.terrain.size() == 2, "layout not applied");
    require(command({"TABLE", "RELOAD"}) && g_plan.terrain.size() == 2, "layout lost on reload");
    require(command({"TABLE", "LAYOUT", "OFF"}) && g_plan.terrain.empty(), "layout not removed");
    require(!command({"TABLE", "LAYOUT", "nope"}), "missing layout accepted");

    // Rings.
    require(command({"TABLE", "DEEPSTRIKE", "30", "22"}) && g_game.measures.back().kind == "deepstrike", "deep strike ring missing");
    require(statusJson().find("\"system\":\"40k\"") != std::string::npos, "status does not say 40k");
    require(statusJson().find("\"deployments\":[\"test-dep\"]") != std::string::npos, "status does not list the owner's files");

    // REVIEW FRAMES: the example in battle, and set up with a layout.
    require(command({"TABLE", "RESET"}) && command({"TABLE", "PLAN", "40k-example"}), "example reload failed");
    command({"TABLE", "OBJ", "1", "P1"}); command({"TABLE", "OBJ", "3", "P2"}); command({"TABLE", "OBJ", "4", "P1"});
    command({"TABLE", "OBJ", "5", "CONTESTED"});
    command({"TABLE", "PHASE", "SHOOTING"});
    command({"TABLE", "ROUND", "3"});
    command({"TABLE", "VP", "P1", "38"}); command({"TABLE", "VP", "P2", "31"});
    command({"TABLE", "CP", "P1", "3"}); command({"TABLE", "CP", "P2", "2"});
    command({"TABLE", "SECONDARY", "P1", "ADD", "CARD", "ONE"}); command({"TABLE", "SECONDARY", "P1", "ADD", "CARD", "TWO"});
    command({"TABLE", "SECONDARY", "P2", "ADD", "CARD", "THREE"});
    command({"TABLE", "DEEPSTRIKE", "18", "34"});
    command({"TABLE", "RINGS", "44", "34", "6"});
    // As the table looks once the 1.5 s claim ripples have died away.
    for (auto& o : g_plan.objectives) o.changed = Clock::time_point {};
    frame(output / "40k-battle.ppm", 1600, 1174, 0.4);
    command({"TABLE", "MEASURE", "CLEAR"});
    require(command({"TABLE", "SETUP", "ON"}) && g_game.setup, "setup view failed");
    frame(output / "40k-setup.ppm", 1600, 1174, 0.4);
    require(command({"TABLE", "SETUP", "OFF"}) && !g_game.setup, "battle view failed");

    // AoS untouched: its phases and colours come back.
    require(command({"TABLE", "PLAN", "into-the-fire"}) && !is40k(g_plan), "AoS plan broken");
    require(phaseCount(g_plan) == 7 && g_game.c1.r == kP1.r, "AoS phases or colours not restored");
    std::cout << "40k check passed; frames in " << output.string() << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
