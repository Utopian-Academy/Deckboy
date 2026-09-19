// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// audio_plugin_vst2.inc — the VST2 half of audio_plugin.cpp.
//
// #########################################################################
// ##                                                                     ##
// ##        T H E   S W I T C H   W E   L E F T   U N L O C K E D        ##
// ##                                                                     ##
// ##  THIS FILE IS IN NO DECKBOY RELEASE. Not because the code is bad --  ##
// ##  it works -- but because Steinberg withdrew the VST2 SDK in 2018,   ##
// ##  hands out no new licences, and wrote terms the GPL will not sit    ##
// ##  next to. Twenty years of perfectly good plugins, orphaned by       ##
// ##  paperwork. We are complying through gritted teeth.                 ##
// ##                                                                     ##
// ##  It compiles when somebody turns it on for themselves:              ##
// ##                                                                     ##
// ##    cmake -B build -DENABLE_VST2=ON \                                ##
// ##                   -DVST2_SDK_DIR=/path/to/your/own/VST2_SDK         ##
// ##                                                                     ##
// ##  and it refuses to configure without that path, because the licence ##
// ##  in question has to be YOURS. Hold one -- plenty of people do --    ##
// ##  and the plugins you already own turn up in the rack like they      ##
// ##  never left. You build it, you run it, you are the distributor.     ##
// ##                                                                     ##
// ##  NOTHING OF STEINBERG'S IS IN THIS FILE. Not one header. The dozen  ##
// ##  structs and opcodes below are declared from the API's shape, the   ##
// ##  way you would describe a lock you are not picking.                 ##
// ##                                                                     ##
// ##  Turning it on for a build you then publish as Deckboy is the one   ##
// ##  move that makes this everybody's problem. Don't.                   ##
// ##                                                                     ##
// #########################################################################
//
// Everything above this seam is format-agnostic already -- the scan, the chain
// slot, the four mapped controls, the show format -- which is why VST2 is one
// branch in openAudioPlugin rather than a second implementation of the rack.
//
// ── AND ONE PIECE OF HONESTY ───────────────────────────────────────────────
//
// THIS FILE HAS NEVER BEEN COMPILED HERE. Nobody on this project holds a VST2
// SDK, so it cannot be, and CI cannot either -- which means it is written to
// the API rather than verified against it. If you are the first to turn it on,
// expect to fix a type or a field order, and please send the patch back. The
// SHAPE is right; the compiler has simply never been asked.
//
// ── WHAT VST2 IS ───────────────────────────────────────────────────────────
//
// One struct and one function pointer. The module exports VSTPluginMain (older
// ones spell it `main`), which is called with a host callback and returns an
// AEffect. Everything after that goes through AEffect::dispatcher with an
// integer opcode, and audio through processReplacing. There is no separate
// editor object, so the parameter list and the state are read straight off the
// effect -- which makes this the shorter of the two backends by a long way.
// ============================================================================

#include <atomic>
#include <chrono>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace deckboy::platform::audioplugin {
namespace vst2 {

// The pieces of the ABI this host touches, declared rather than included. The
// layout is fixed by every VST2 plugin ever compiled, so these names describe
// it; they do not reproduce the SDK.
struct AEffect;
using AudioMasterCallback = std::intptr_t (*)(AEffect*, std::int32_t opcode,
                                              std::int32_t index,
                                              std::intptr_t value, void* ptr,
                                              float opt);
using DispatcherProc = std::intptr_t (*)(AEffect*, std::int32_t opcode,
                                         std::int32_t index,
                                         std::intptr_t value, void* ptr,
                                         float opt);
using ProcessProc = void (*)(AEffect*, float** inputs, float** outputs,
                             std::int32_t sampleFrames);
using SetParameterProc = void (*)(AEffect*, std::int32_t index, float value);
using GetParameterProc = float (*)(AEffect*, std::int32_t index);

struct AEffect {
  std::int32_t magic;
  DispatcherProc dispatcher;
  ProcessProc processDeprecated;
  SetParameterProc setParameter;
  GetParameterProc getParameter;
  std::int32_t numPrograms;
  std::int32_t numParams;
  std::int32_t numInputs;
  std::int32_t numOutputs;
  std::int32_t flags;
  std::intptr_t resvd1;
  std::intptr_t resvd2;
  std::int32_t initialDelay;
  std::int32_t realQualitiesDeprecated;
  std::int32_t offQualitiesDeprecated;
  float ioRatioDeprecated;
  void* object;
  void* user;
  std::int32_t uniqueID;
  std::int32_t version;
  ProcessProc processReplacing;
  ProcessProc processDoubleReplacing;
  char future[56];
};

using MainProc = AEffect* (*)(AudioMasterCallback);

// Opcodes, by the names the API gives them. Only the ones this host sends.
enum : std::int32_t {
  effOpen = 0,
  effClose = 1,
  effSetParameterAuto = 6,
  effGetParamName = 8,
  effSetSampleRate = 10,
  effSetBlockSize = 11,
  effMainsChanged = 12,
  effProcessEvents = 25,      // notes, handed over just before their block
  effGetChunk = 23,
  effSetChunk = 24,
  effGetEffectName = 45,
  effGetVendorString = 47,
  effGetPlugCategory = 35,
  effCanDo = 51,
  effStartProcess = 71,
  effStopProcess = 72,
};

enum : std::int32_t {
  audioMasterVersion = 1,
  audioMasterGetSampleRate = 16,
  audioMasterGetBlockSize = 17,
  audioMasterGetCurrentProcessLevel = 23,
  audioMasterGetVendorString = 32,
  audioMasterGetProductString = 33,
  audioMasterGetVendorVersion = 34,
  audioMasterCanDo = 37,
};

constexpr std::int32_t kEffectMagic = 0x56737450;   // 'VstP'
constexpr std::int32_t kEffFlagsProgramChunks = 1 << 5;
constexpr std::int32_t kEffFlagsIsSynth = 1 << 8;
constexpr std::int32_t kCategorySynth = 2;

// What the plugin is allowed to ask the host. Deliberately small and
// deliberately honest: a host that lies about what it can do gets asked to do
// it. "No" is a valid answer to almost everything here.
std::intptr_t hostCallback(AEffect* effect, std::int32_t opcode,
                           std::int32_t /*index*/, std::intptr_t /*value*/,
                           void* ptr, float /*opt*/) {
  switch (opcode) {
    case audioMasterVersion:
      return 2400;
    case audioMasterGetSampleRate:
      return 48000;
    case audioMasterGetBlockSize:
      return 2048;
    case audioMasterGetCurrentProcessLevel:
      return 2;   // realtime
    case audioMasterGetVendorString:
    case audioMasterGetProductString:
      if (ptr) {
        std::strncpy(static_cast<char*>(ptr), "Deckboy", 63);
        static_cast<char*>(ptr)[63] = '\0';
        return 1;
      }
      return 0;
    case audioMasterGetVendorVersion:
      return 1;
    case audioMasterCanDo:
      // Everything this host has not implemented is a no, including the
      // idle/timer callbacks: saying yes to one of those and then never
      // calling it is how a plugin ends up waiting forever.
      return 0;
    default:
      (void)effect;
      return 0;
  }
}

// One loaded module, kept alive exactly as long as any instance from it --
// same rule as the VST3 side, same reason: a plugin unloaded while its own
// threads are running is an immediate crash.
class Vst2Module {
 public:
  explicit Vst2Module(const std::filesystem::path& path) {
#if defined(_WIN32)
    handle_ = LoadLibraryW(path.wstring().c_str());
    if (handle_) {
      entry_ = reinterpret_cast<MainProc>(
        reinterpret_cast<void*>(GetProcAddress(handle_, "VSTPluginMain")));
      if (!entry_) {
        // Pre-2.4 plugins export `main`, and a fair number of the ones still
        // in use are that old -- which is rather the point of hosting VST2.
        entry_ = reinterpret_cast<MainProc>(
          reinterpret_cast<void*>(GetProcAddress(handle_, "main")));
      }
    }
#else
    handle_ = dlopen(path.string().c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (handle_) {
      entry_ = reinterpret_cast<MainProc>(dlsym(handle_, "VSTPluginMain"));
      if (!entry_) {
        entry_ = reinterpret_cast<MainProc>(dlsym(handle_, "main"));
      }
    }
#endif
  }

  // Never unloaded, for the reason in the class comment.
  ~Vst2Module() = default;

  MainProc entry() const { return entry_; }
  bool valid() const { return entry_ != nullptr; }

 private:
#if defined(_WIN32)
  HMODULE handle_ = nullptr;
#else
  void* handle_ = nullptr;
#endif
  MainProc entry_ = nullptr;
};

class Vst2Instance final : public AudioPluginInstance {
 public:
  Vst2Instance(std::shared_ptr<Vst2Module> module, PluginDescriptor descriptor)
    : module_(std::move(module)), descriptor_(std::move(descriptor)) {}

  ~Vst2Instance() override {
    if (effect_) {
      effect_->dispatcher(effect_, effStopProcess, 0, 0, nullptr, 0.0f);
      effect_->dispatcher(effect_, effMainsChanged, 0, 0, nullptr, 0.0f);
      effect_->dispatcher(effect_, effClose, 0, 0, nullptr, 0.0f);
    }
  }

  bool open(double sampleRate, int maxBlockFrames) {
    MainProc entry = module_->entry();
    if (!entry) {
      return false;
    }
    effect_ = entry(&hostCallback);
    if (!effect_ || effect_->magic != kEffectMagic || !effect_->dispatcher) {
      effect_ = nullptr;
      return false;
    }
    effect_->dispatcher(effect_, effOpen, 0, 0, nullptr, 0.0f);
    effect_->dispatcher(effect_, effSetSampleRate, 0, 0, nullptr,
                        static_cast<float>(sampleRate));
    effect_->dispatcher(effect_, effSetBlockSize, 0, maxBlockFrames, nullptr, 0.0f);
    effect_->dispatcher(effect_, effMainsChanged, 0, 1, nullptr, 0.0f);
    effect_->dispatcher(effect_, effStartProcess, 0, 0, nullptr, 0.0f);

    if (!effect_->processReplacing) {
      return false;   // the deprecated accumulating process is not worth having
    }
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlockFrames;
    left_.assign(static_cast<std::size_t>(maxBlockFrames), 0.0f);
    right_.assign(static_cast<std::size_t>(maxBlockFrames), 0.0f);
    outLeft_.assign(static_cast<std::size_t>(maxBlockFrames), 0.0f);
    outRight_.assign(static_cast<std::size_t>(maxBlockFrames), 0.0f);

    char name[128] = {0};
    if (effect_->dispatcher(effect_, effGetEffectName, 0, 0, name, 0.0f) &&
        name[0] != '\0') {
      descriptor_.name = name;
    }
    char vendor[128] = {0};
    if (effect_->dispatcher(effect_, effGetVendorString, 0, 0, vendor, 0.0f) &&
        vendor[0] != '\0') {
      descriptor_.vendor = vendor;
    }
    descriptor_.isInstrument =
      (effect_->flags & kEffFlagsIsSynth) != 0 || effect_->numInputs <= 0 ||
      effect_->dispatcher(effect_, effGetPlugCategory, 0, 0, nullptr, 0.0f) ==
        kCategorySynth;
    readParameters();
    return true;
  }

  bool process(float* interleavedStereo, int frames) override {
    if (!effect_ || bypassed_.load(std::memory_order_relaxed) || frames <= 0 ||
        frames > maxBlock_) {
      return false;
    }
    deliverNotes();   // events belong to the block they arrive with
    for (int i = 0; i < frames; ++i) {
      left_[static_cast<std::size_t>(i)] = interleavedStereo[i * 2];
      right_[static_cast<std::size_t>(i)] = interleavedStereo[i * 2 + 1];
    }
    // A mono plugin gets one input and writes one output; feeding it two
    // pointers it never reads is harmless, reading two it never wrote is not,
    // so the second output is cleared first either way.
    std::fill(outLeft_.begin(), outLeft_.begin() + frames, 0.0f);
    std::fill(outRight_.begin(), outRight_.begin() + frames, 0.0f);
    float* inputs[2] = {left_.data(), right_.data()};
    float* outputs[2] = {outLeft_.data(), outRight_.data()};

    const auto started = std::chrono::steady_clock::now();
    effect_->processReplacing(effect_, inputs, outputs, frames);
    const double elapsedMs =
      std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();

    // THE SAME BUDGET AS VST3, and for the same reason: this is somebody
    // else's code on the audio thread, and a plugin that cannot keep up is a
    // show full of clicks rather than a plugin to argue with.
    const double budgetMs =
      (static_cast<double>(frames) / sampleRate_) * 1000.0 * 0.6;
    if (elapsedMs > budgetMs) {
      if (++overruns_ >= kOverrunsBeforeBypass) {
        bypassed_.store(true, std::memory_order_relaxed);
        return false;
      }
    } else if (overruns_ > 0) {
      --overruns_;
    }

    const bool mono = effect_->numOutputs < 2;
    for (int i = 0; i < frames; ++i) {
      const float l = outLeft_[static_cast<std::size_t>(i)];
      const float r = mono ? l : outRight_[static_cast<std::size_t>(i)];
      interleavedStereo[i * 2] = l;
      interleavedStereo[i * 2 + 1] = r;
    }
    return true;
  }

  const std::vector<PluginParameter>& parameters() const override {
    return parameters_;
  }

  // VST2 parameters ARE normalised 0-1 by the API, so there is no conversion
  // here -- and the id is the index, which is why it round-trips through a
  // show file unchanged.
  void setParameter(std::uint32_t id, double normalised) override {
    if (effect_ && effect_->setParameter &&
        id < static_cast<std::uint32_t>(effect_->numParams)) {
      effect_->setParameter(effect_, static_cast<std::int32_t>(id),
                            static_cast<float>(normalised));
    }
  }

  double parameter(std::uint32_t id) const override {
    if (!effect_ || !effect_->getParameter ||
        id >= static_cast<std::uint32_t>(effect_->numParams)) {
      return 0.0;
    }
    return effect_->getParameter(effect_, static_cast<std::int32_t>(id));
  }

  // Notes ride a VstEvents block handed over with effProcessEvents just before
  // the audio block they belong to. Same single-producer ring as the VST3
  // side; see that file for why a note crosses threads this way.
  void noteOn(int channel, int key, double velocity) override {
    pushNote(0x90, channel, key, static_cast<int>(velocity * 127.0));
  }
  void noteOff(int channel, int key) override {
    pushNote(0x80, channel, key, 0);
  }

  std::string saveState() const override {
    if (!effect_ || (effect_->flags & kEffFlagsProgramChunks) == 0) {
      // No chunk support: the parameters ARE the state, and those are already
      // in the show file. Returning nothing is the honest answer.
      return {};
    }
    void* data = nullptr;
    const std::intptr_t size =
      effect_->dispatcher(effect_, effGetChunk, 0, 0, &data, 0.0f);
    if (size <= 0 || !data) {
      return {};
    }
    return std::string(static_cast<const char*>(data),
                       static_cast<std::size_t>(size));
  }

  bool loadState(const std::string& state) override {
    if (!effect_ || state.empty() ||
        (effect_->flags & kEffFlagsProgramChunks) == 0) {
      return false;
    }
    return effect_->dispatcher(
             effect_, effSetChunk, 0,
             static_cast<std::intptr_t>(state.size()),
             const_cast<char*>(state.data()), 0.0f) != 0;
  }

  bool overran() const override { return bypassed_.load(std::memory_order_relaxed); }
  const PluginDescriptor& descriptor() const override { return descriptor_; }

 private:
  void readParameters() {
    const std::int32_t count = effect_->numParams;
    parameters_.reserve(static_cast<std::size_t>(std::max(0, count)));
    for (std::int32_t i = 0; i < count; ++i) {
      PluginParameter p;
      p.id = static_cast<std::uint32_t>(i);
      char name[128] = {0};
      effect_->dispatcher(effect_, effGetParamName, i, 0, name, 0.0f);
      p.name = name[0] != '\0' ? name : ("param " + std::to_string(i + 1));
      p.defaultValue = effect_->getParameter
                         ? effect_->getParameter(effect_, i) : 0.0;
      p.automatable = true;   // VST2 draws no distinction
      parameters_.push_back(std::move(p));
    }
  }

  // VstEvents is a variable-length struct: a count followed by that many
  // pointers. Declared here at a fixed capacity rather than allocated, because
  // it is filled on the audio thread.
  struct VstMidiEvent {
    std::int32_t type;          // 1 = kVstMidiType
    std::int32_t byteSize;
    std::int32_t deltaFrames;
    std::int32_t flags;
    std::int32_t noteLength;
    std::int32_t noteOffset;
    char midiData[4];
    char detune;
    char noteOffVelocity;
    char reserved1;
    char reserved2;
  };
  static constexpr int kMaxEvents = 64;
  struct VstEventsBlock {
    std::int32_t numEvents = 0;
    std::intptr_t reserved = 0;
    void* events[kMaxEvents] {};
  };

  void pushNote(int status, int channel, int key, int velocity) {
    const unsigned slot = noteWrite_.load(std::memory_order_relaxed);
    noteRing_[slot % kRingSize] = PendingNote {status, channel, key, velocity};
    noteWrite_.store(slot + 1, std::memory_order_release);
  }

  // Drains the ring into the preallocated block and hands it over. Called at
  // the top of process(), so the notes reach the plugin in the same block.
  void deliverNotes() {
    const unsigned written = noteWrite_.load(std::memory_order_acquire);
    if (written - noteRead_ > kRingSize) {
      noteRead_ = written - kRingSize;
    }
    block_.numEvents = 0;
    for (; noteRead_ != written && block_.numEvents < kMaxEvents; ++noteRead_) {
      const PendingNote& n = noteRing_[noteRead_ % kRingSize];
      VstMidiEvent& e = events_[block_.numEvents];
      e = VstMidiEvent {};
      e.type = 1;                       // kVstMidiType
      e.byteSize = sizeof(VstMidiEvent);
      e.deltaFrames = 0;
      e.midiData[0] = static_cast<char>(n.status | (n.channel & 0x0f));
      e.midiData[1] = static_cast<char>(n.key & 0x7f);
      e.midiData[2] = static_cast<char>(n.velocity & 0x7f);
      e.midiData[3] = 0;
      block_.events[block_.numEvents] = &e;
      ++block_.numEvents;
    }
    if (block_.numEvents > 0) {
      effect_->dispatcher(effect_, effProcessEvents, 0, 0, &block_, 0.0f);
    }
  }

  static constexpr unsigned kRingSize = 64;
  struct PendingNote {
    int status = 0;
    int channel = 0;
    int key = 0;
    int velocity = 0;
  };
  PendingNote noteRing_[kRingSize] {};
  std::atomic<unsigned> noteWrite_ {0};
  unsigned noteRead_ = 0;        // audio thread only
  VstMidiEvent events_[kMaxEvents] {};
  VstEventsBlock block_;

  static constexpr int kOverrunsBeforeBypass = 8;

  std::shared_ptr<Vst2Module> module_;
  PluginDescriptor descriptor_;
  AEffect* effect_ = nullptr;
  std::vector<PluginParameter> parameters_;
  std::vector<float> left_, right_, outLeft_, outRight_;
  double sampleRate_ = 48000.0;
  int maxBlock_ = 0;
  int overruns_ = 0;
  std::atomic<bool> bypassed_ {false};
};

std::shared_ptr<Vst2Module> moduleFor(const std::string& path) {
  static std::mutex mutex;
  static std::map<std::string, std::weak_ptr<Vst2Module>> cache;
  std::lock_guard<std::mutex> lock(mutex);
  auto it = cache.find(path);
  if (it != cache.end()) {
    if (auto alive = it->second.lock()) {
      return alive;
    }
  }
  auto module = std::make_shared<Vst2Module>(std::filesystem::path(path));
  if (!module->valid()) {
    return nullptr;
  }
  cache[path] = module;
  return module;
}

}  // namespace vst2

std::unique_ptr<AudioPluginInstance> openVst2Plugin(const std::string& path,
                                                    double sampleRate,
                                                    int maxBlockFrames) {
  auto module = vst2::moduleFor(path);
  if (!module) {
    return nullptr;
  }
  PluginDescriptor d;
  d.format = "vst2";
  d.path = path;
  d.name = std::filesystem::path(path).stem().string();
  d.id = "vst2:" + path;
  auto instance = std::make_unique<vst2::Vst2Instance>(module, d);
  if (!instance->open(sampleRate, maxBlockFrames)) {
    return nullptr;
  }
  return instance;
}

}  // namespace deckboy::platform::audioplugin
