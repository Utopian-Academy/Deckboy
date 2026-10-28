// SDL-free check that the edge strips (round, score, twist, secondaries) keep
// clear of every label on the table, and fit the board they run along.
// Each built-in plan is drawn with and without its labels: no pixel of label
// text may fall on a strip's plate, and no plate may run off the board.
#include <stdexcept>
#include "../native/extras/tabletop/tabletop.cpp"

namespace deckboy::core {
std::filesystem::path Paths::stateDir() {
  return std::filesystem::temp_directory_path() / "deckboy-edges-check-no-overrides";
}
}

namespace {

constexpr double kPixelsPerInch = 30;

void writeFrame(const fs::path& path, const std::vector<std::uint8_t>& pixels, int w, int h) {
  std::ofstream frame(path, std::ios::binary);
  frame << "P6\n" << w << ' ' << h << "\n255\n";
  for (std::size_t i = 0; i < pixels.size(); i += 4) frame.write(reinterpret_cast<const char*>(&pixels[i]), 3);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const fs::path output = argc > 1 ? fs::path(argv[1]) : fs::path("edges-check");
    fs::create_directories(output);
    ensureBooted();
    auto command = [](std::initializer_list<std::string> words) {
      std::string reply;
      return tableCommand(std::vector<std::string>(words), reply);
    };
    std::vector<std::string> slugs;
    for (const auto& control : deskControls())
      if (control.command.rfind("TABLE PLAN ", 0) == 0) slugs.push_back(control.command.substr(11));
    static const char* kStates[] = {"level", "underdog", "clock", "small board"};
    int failures = 0, cases = 0;
    for (const auto& slug : slugs) {
      for (int state = 0; state < 4; ++state) {
        g_game = Game {};
        if (!command({"TABLE", "PLAN", slug})) throw std::runtime_error("plan did not load: " + slug);
        if (state >= 1) command({"TABLE", "VP", "P1", "3"});   // the UNDERDOG line appears
        if (state >= 2) command({"TABLE", "CLOCK", "ON"});     // the longest strip
        if (state == 3 && !command({"TABLE", "BOARD", "44X30"})) continue;   // printed boards keep their size
        ++cases;
        const std::string name = slug + " (" + kStates[state] + ")";
        const int w = static_cast<int>(std::lround(g_plan.w * kPixelsPerInch));
        const int h = static_cast<int>(std::lround(g_plan.h * kPixelsPerInch));
        std::vector<std::uint8_t> labelled(static_cast<std::size_t>(w) * h * 4), bare(labelled.size());
        renderTable(labelled.data(), w, h, 0);
        const Plan plan = g_plan;
        for (auto& o : g_plan.objectives) { o.label.clear(); o.kind.clear(); }
        for (auto& p : g_plan.places) p.label.clear();
        for (auto& m : g_plan.markers) m.label.clear();
        for (auto& t : g_plan.terrain) t.label.clear();
        renderTable(bare.data(), w, h, 0);
        g_plan = plan;
        const Canvas canvas = tableCanvas(nullptr, w, h, g_plan);
        std::vector<PlateBox> plates;
        for (const auto& line : edgeLines(g_plan, g_game, canvas)) plates.push_back(plateBox(line, canvas, g_plan));
        if (plates.size() < 2) throw std::runtime_error("no edge strips on " + name);
        bool offBoard = false;
        for (const auto& box : plates) offBoard = offBoard || box.x0 < 0 || box.x1 > w;
        long hidden = 0;
        for (int y = 0; y < h; ++y)
          for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            if (labelled[i] == bare[i] && labelled[i + 1] == bare[i + 1] && labelled[i + 2] == bare[i + 2]) continue;
            for (const auto& box : plates)
              if (x >= box.x0 && x < box.x1 && y >= box.y0 && y < box.y1) { ++hidden; break; }
          }
        if (offBoard || hidden) {
          std::fprintf(stderr, "FAIL: %s:%s%s\n", name.c_str(), offBoard ? " a strip runs off the board" : "",
                       hidden ? (" " + std::to_string(hidden) + " pixels of label text on a strip").c_str() : "");
          writeFrame(output / (slug + "-" + std::to_string(state) + ".ppm"), labelled, w, h);
          ++failures;
        }
      }
    }
    if (cases < static_cast<int>(slugs.size()) * 3) throw std::runtime_error("too few cases ran");
    if (failures) return 1;
    std::printf("PASS: %zu plans, %d cases: every label clear of the edge strips, every strip on its board\n",
                slugs.size(), cases);
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
  }
}
