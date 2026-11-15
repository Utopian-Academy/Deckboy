// voice_check.cpp -- the lesson characters' vocal-tract voice (core/vocal_tract.hpp).
//
// With no arguments: checks that every cast voice speaks a line that is
// finite, not silent, not clipped, and has the gaps a closure makes.
// With an output folder: also writes WAVs to listen to and measure --
// every vowel held (vowel_<v>.wav, for tools/check_voice.py's formants) and
// one line per character (<name>.wav).

#include "core/vocal_tract.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

void writeWav(const std::string& path, const std::vector<float>& s, int rate) {
  std::ofstream f(path, std::ios::binary);
  auto u32 = [&f](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
  auto u16 = [&f](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
  const std::uint32_t bytes = static_cast<std::uint32_t>(s.size() * 2);
  f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8);
  u32(16); u16(1); u16(1); u32(static_cast<std::uint32_t>(rate)); u32(static_cast<std::uint32_t>(rate * 2)); u16(2); u16(16);
  f.write("data", 4); u32(bytes);
  for (float v : s) {
    const double c = std::clamp(static_cast<double>(v), -1.0, 1.0);
    u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(c * 32767.0))));
  }
}

std::string fileName(std::string name) {
  for (char& c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
  }
  return name;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace deckboy::voice;
  const int rate = 48000;
  // --shape <tongue index> <tongue diameter> <lip rounding or -1> <out.wav>:
  // one held vowel of any shape, for tuning the table (tools/check_voice.py --tune).
  if (argc == 6 && std::strcmp(argv[1], "--shape") == 0) {
    VoiceParams vp;
    vp.pitchHz = 110;
    vp.vibrato = 0.0;
    Target t = detail::vowel(std::atof(argv[2]), std::atof(argv[3]), std::atof(argv[4]));
    writeWav(argv[5], holdShape(t, vp, 0.8, rate), rate);
    return 0;
  }
  const std::string out = argc > 1 ? argv[1] : "";
  int failures = 0;

  const char* cast[] = {"DR. PIXEL", "PROF. VIDEO", "COACH MARV", "JUNE O'NEIL", "TURNIP TOP", "CHEF BORK",
                        "DONNA WHEEL", "KEEPER ROOK", "AGENT BRANDT", "AGENT HOLLIS", "MR. SALTY",
                        "OPTIMUS PRIMETIME", "WALLIS", "CAROLINE POLKADOT", "LT. HAILA", "ENSIGN JINKLEY"};
  const char* lines[] = {
    "Welcome to Deckboy, skapi. Press TAKE and the picture goes live.",
    "A good mosh starts with a well made clip. Hard cuts, loud colour, then one big move!",
    "Is that a Lower Third? Put a name on it.",
  };

  int which = 0;
  for (const char* name : cast) {
    const std::string line = lines[which++ % 3];
    const Spoken sp = speak(line, castVoice(name), rate);
    double peak = 0, energy = 0;
    bool finite = true;
    // A jump of more than half full scale in one sample is a click: the
    // waveguide ringing at the sample rate (what James heard as "clippy").
    double jump = 0.0;
    float last = 0.0f;
    for (float v : sp.samples) {
      if (!std::isfinite(v)) finite = false;
      jump = std::max(jump, static_cast<double>(std::abs(v - last)));
      last = v;
      peak = std::max(peak, static_cast<double>(std::abs(v)));
      energy += static_cast<double>(v) * v;
    }
    const double rms = sp.samples.empty() ? 0 : std::sqrt(energy / sp.samples.size());
    // quiet stretches of 30 ms or more: closures and pauses
    int gaps = 0, quiet = 0;
    for (std::size_t i = 0; i < sp.samples.size(); i += 480) {
      double e = 0;
      for (std::size_t k = i; k < std::min(sp.samples.size(), i + 480); ++k) e += sp.samples[k] * sp.samples[k];
      e = std::sqrt(e / 480.0);
      if (e < rms * 0.08) { if (++quiet == 3) ++gaps; } else quiet = 0;
    }
    const double secs = static_cast<double>(sp.samples.size()) / rate;
    const bool ok = finite && rms > 0.02 && peak <= 1.0 && jump < 0.5 && gaps >= 2 && secs > 1.0 && secs < 12.0 &&
                    !sp.mouth.empty();
    std::printf("%-6s %-18s %.2f s  rms %.3f  peak %.2f  biggest step %.2f  gaps %d  mouth frames %zu\n",
                ok ? "ok" : "FAIL", name, secs, rms, peak, jump, gaps, sp.mouth.size());
    if (!ok) ++failures;
    if (!out.empty()) writeWav(out + "/" + fileName(name) + ".wav", sp.samples, rate);
  }

  if (!out.empty()) {
    VoiceParams vp;
    vp.pitchHz = 110;
    vp.vibrato = 0.0;
    for (const char* v : {"i", "I", "e", "ae", "a", "o", "u", "U", "^", "@"}) {
      // Named, not spelled: Windows file names ignore case (i, I) and both
      // ^ and @ became "_".
      const std::string key = v;
      const std::string file = key == "^" ? "cup" : key == "@" ? "schwa"
                             : std::isupper(static_cast<unsigned char>(v[0])) ? key + "_lax" : key;
      writeWav(out + "/vowel_" + file + ".wav", holdVowel(v, vp, 1.0, rate), rate);
    }
    std::printf("wrote WAVs to %s\n", out.c_str());
  }
  std::printf("%s\n", failures ? "FAIL" : "voices: ok");
  return failures ? 1 : 0;
}
