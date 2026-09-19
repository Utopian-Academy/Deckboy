// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// audio_plugin_vst3.inc — the VST3 half of audio_plugin.cpp.
//
// Included into audio_plugin.cpp behind DECKBOY_HAS_VST3, in the same style as
// the .ipp files in native/app: one translation unit, no second object, and the
// SDK's headers pulled in only where they are wanted.
//
// WHY THE IIDs ARE DEFINED HERE. The VST3 SDK ships as headers; the interface
// IIDs live in funknown.cpp, which vcpkg does not install. DEF_CLASS_IID is the
// SDK's own macro for exactly this -- a host that does not link the SDK's base
// library defines the IIDs it uses itself.
// ============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Steinberg {
DEF_CLASS_IID(FUnknown)
DEF_CLASS_IID(IPluginBase)
DEF_CLASS_IID(IPluginFactory)
DEF_CLASS_IID(IPluginFactory2)
DEF_CLASS_IID(IPluginFactory3)
namespace Vst {
// Only the interfaces this host actually asks for. Each DEF_CLASS_IID defines a
// symbol, so listing one whose header is not included does not fail to link --
// it fails to compile, which is the better end of that trade.
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IHostApplication)
}  // namespace Vst
}  // namespace Steinberg

namespace deckboy::platform::audioplugin {
namespace {

using namespace Steinberg;

// ── The host, as the plugin sees it ─────────────────────────────────────────
//
// Plugins ask the host for its name and, increasingly, for a message or an
// attribute-list implementation during initialize(). A host that answers
// nothing is refused by a fair number of them, so this is the smallest thing
// that is not a lie: Deckboy identifies itself and says no to the rest.
class DeckboyHostContext : public Vst::IHostApplication {
 public:
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    if (FUnknownPrivate::iidEqual(iid, Vst::IHostApplication::iid) ||
        FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
      *obj = static_cast<Vst::IHostApplication*>(this);
      addRef();
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }

  tresult PLUGIN_API getName(Vst::String128 name) override {
    static const char16_t kName[] = u"Deckboy";
    std::memcpy(name, kName, sizeof(kName));
    return kResultOk;
  }
  tresult PLUGIN_API createInstance(TUID /*cid*/, TUID /*iid*/, void** obj) override {
    // Message and attribute-list objects are what this is usually asked for.
    // Saying no is honest and survivable; inventing a half-working message
    // object is how a plugin ends up in an unexpected state mid-show.
    *obj = nullptr;
    return kNotImplemented;
  }
};

DeckboyHostContext& hostContext() {
  static DeckboyHostContext context;
  return context;
}

// A VST3 on disk is either a bare module or a bundle; inside a bundle the
// binary sits under Contents/<arch>/. Every platform spells the arch folder
// differently, and getting it wrong reads as "the plugin is broken" rather than
// "we looked in the wrong place".
std::filesystem::path resolveModuleBinary(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_regular_file(path, ec)) {
    return path;
  }
#if defined(_WIN32)
  const char* arch = "x86_64-win";
#elif defined(__APPLE__)
  const char* arch = "MacOS";
#elif defined(__aarch64__)
  const char* arch = "aarch64-linux";
#else
  const char* arch = "x86_64-linux";
#endif
  const std::filesystem::path contents = path / "Contents" / arch;
  if (std::filesystem::is_directory(contents, ec)) {
    for (const auto& entry : std::filesystem::directory_iterator(contents, ec)) {
      if (entry.is_regular_file(ec)) {
        return entry.path();
      }
    }
  }
  return path;
}

using GetFactoryProc = IPluginFactory* (*)();
using InitModuleProc = bool (*)();
using ExitModuleProc = bool (*)();

// One loaded module. Kept alive as long as any instance from it is alive: a
// plugin unloaded under its own processing thread is an immediate crash, and
// the SDK says nothing about when a plugin's threads stop.
class Vst3Module {
 public:
  explicit Vst3Module(const std::filesystem::path& path) {
    const std::filesystem::path binary = resolveModuleBinary(path);
#if defined(_WIN32)
    handle_ = static_cast<void*>(LoadLibraryW(binary.wstring().c_str()));
#else
    handle_ = dlopen(binary.string().c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
    if (!handle_) {
      return;
    }
    auto symbol = [&](const char* name) -> void* {
#if defined(_WIN32)
      return reinterpret_cast<void*>(
        GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
      return dlsym(handle_, name);
#endif
    };
#if defined(_WIN32)
    if (auto init = reinterpret_cast<InitModuleProc>(symbol("InitDll"))) {
      init();
    }
#elif defined(__APPLE__)
    if (auto init = reinterpret_cast<InitModuleProc>(symbol("bundleEntry"))) {
      init();
    }
#else
    if (auto init = reinterpret_cast<InitModuleProc>(symbol("ModuleEntry"))) {
      init();
    }
#endif
    auto getFactory = reinterpret_cast<GetFactoryProc>(symbol("GetPluginFactory"));
    if (getFactory) {
      factory_ = getFactory();
    }
  }

  ~Vst3Module() {
    if (factory_) {
      factory_->release();
      factory_ = nullptr;
    }
    // The module itself is deliberately NOT unloaded. A plugin may still have
    // worker threads parked in its own code, and FreeLibrary while one is
    // running takes the whole show down; every serious host leaks the module
    // rather than risk it, and a show loads a handful of plugins, not
    // thousands.
  }

  IPluginFactory* factory() const { return factory_; }
  bool valid() const { return factory_ != nullptr; }

 private:
  void* handle_ = nullptr;
  IPluginFactory* factory_ = nullptr;
};

}  // namespace

// ── The instance ────────────────────────────────────────────────────────────
class Vst3Instance final : public AudioPluginInstance {
 public:
  Vst3Instance(std::shared_ptr<Vst3Module> module, PluginDescriptor descriptor)
    : module_(std::move(module)), descriptor_(std::move(descriptor)) {}

  ~Vst3Instance() override {
    if (processor_) {
      processor_->setProcessing(false);
    }
    if (component_) {
      component_->setActive(false);
      component_->terminate();
      component_->release();
    }
    if (processor_) {
      processor_->release();
    }
    if (controller_) {
      controller_->terminate();
      controller_->release();
    }
  }

  // The class id arrives as a raw TUID rather than an FUID: FUID's
  // constructor lives in the SDK's funknown.cpp, which is not installed
  // with the headers, and sixteen bytes need no wrapper to be copied.
  bool open(const TUID classId, double sampleRate, int maxBlockFrames) {
    IPluginFactory* factory = module_->factory();
    if (!factory) {
      return false;
    }
    TUID cid;
    std::memcpy(cid, classId, sizeof(TUID));
    if (factory->createInstance(cid, Vst::IComponent::iid,
                                reinterpret_cast<void**>(&component_)) != kResultOk ||
        !component_) {
      return false;
    }
    if (component_->initialize(&hostContext()) != kResultOk) {
      return false;
    }
    if (component_->queryInterface(Vst::IAudioProcessor::iid,
                                   reinterpret_cast<void**>(&processor_)) != kResultOk) {
      return false;
    }
    // A single-component plugin is its own controller; a split one names a
    // separate class. Parameters are worth having either way, so both are
    // tried and neither is fatal -- a plugin with no reachable controller
    // still processes audio, it just has no rows in the inspector.
    if (component_->queryInterface(Vst::IEditController::iid,
                                   reinterpret_cast<void**>(&controller_)) != kResultOk) {
      TUID controllerId;
      if (component_->getControllerClassId(controllerId) == kResultOk) {
        factory->createInstance(controllerId, Vst::IEditController::iid,
                                reinterpret_cast<void**>(&controller_));
        if (controller_) {
          controller_->initialize(&hostContext());
        }
      }
    }

    // Stereo in, stereo out. An instrument has no input bus, which is not an
    // error -- it is the difference between a reverb and a synth.
    Vst::SpeakerArrangement stereo = Vst::SpeakerArr::kStereo;
    const int32 inputCount = component_->getBusCount(Vst::kAudio, Vst::kInput);
    const int32 outputCount = component_->getBusCount(Vst::kAudio, Vst::kOutput);
    hasInput_ = inputCount > 0;
    if (outputCount <= 0) {
      return false;   // makes no sound and treats none: not something to host
    }
    Vst::SpeakerArrangement inputArr[1] = {stereo};
    Vst::SpeakerArrangement outputArr[1] = {stereo};
    processor_->setBusArrangements(hasInput_ ? inputArr : nullptr, hasInput_ ? 1 : 0,
                                   outputArr, 1);
    if (hasInput_) {
      component_->activateBus(Vst::kAudio, Vst::kInput, 0, true);
    }
    component_->activateBus(Vst::kAudio, Vst::kOutput, 0, true);
    if (component_->getBusCount(Vst::kEvent, Vst::kInput) > 0) {
      component_->activateBus(Vst::kEvent, Vst::kInput, 0, true);
      hasEventInput_ = true;
    }

    Vst::ProcessSetup setup {};
    setup.processMode = Vst::kRealtime;
    setup.symbolicSampleSize = Vst::kSample32;
    setup.maxSamplesPerBlock = maxBlockFrames;
    setup.sampleRate = sampleRate;
    if (processor_->setupProcessing(setup) != kResultOk) {
      return false;
    }
    if (component_->setActive(true) != kResultOk) {
      return false;
    }
    processor_->setProcessing(true);

    sampleRate_ = sampleRate;
    maxBlock_ = maxBlockFrames;
    left_.assign(static_cast<std::size_t>(maxBlockFrames), 0.0f);
    right_.assign(static_cast<std::size_t>(maxBlockFrames), 0.0f);
    outLeft_.assign(static_cast<std::size_t>(maxBlockFrames), 0.0f);
    outRight_.assign(static_cast<std::size_t>(maxBlockFrames), 0.0f);
    readParameters();
    return true;
  }

  bool process(float* interleavedStereo, int frames) override {
    if (!processor_ || bypassed_.load(std::memory_order_relaxed) || frames <= 0) {
      return false;
    }
    if (frames > maxBlock_) {
      // Never grow a buffer on the audio thread. A block bigger than the one
      // the plugin was set up for is the caller's bug, and silence is a worse
      // answer than passing the audio through untouched.
      return false;
    }
    for (int i = 0; i < frames; ++i) {
      left_[static_cast<std::size_t>(i)] = interleavedStereo[i * 2];
      right_[static_cast<std::size_t>(i)] = interleavedStereo[i * 2 + 1];
    }
    float* inChannels[2] = {left_.data(), right_.data()};
    float* outChannels[2] = {outLeft_.data(), outRight_.data()};
    Vst::AudioBusBuffers inBus {};
    inBus.numChannels = 2;
    inBus.channelBuffers32 = inChannels;
    Vst::AudioBusBuffers outBus {};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChannels;

    Vst::ProcessData data {};
    data.processMode = Vst::kRealtime;
    data.symbolicSampleSize = Vst::kSample32;
    data.numSamples = frames;
    data.numInputs = hasInput_ ? 1 : 0;
    data.numOutputs = 1;
    data.inputs = hasInput_ ? &inBus : nullptr;
    data.outputs = &outBus;

    const auto started = std::chrono::steady_clock::now();
    const tresult result = processor_->process(data);
    const double elapsedMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    // THE BUDGET. A plugin is somebody else's code on our audio thread: it may
    // allocate, take a lock, or simply be too slow for the block it was given.
    // One late block is a hiccup and might be a page fault on first use; a
    // habit of them is a show full of clicks, so a plugin that keeps overrunning
    // is taken out of the chain and the operator is told.
    const double budgetMs = (static_cast<double>(frames) / sampleRate_) * 1000.0 * 0.6;
    if (elapsedMs > budgetMs) {
      if (++overruns_ >= kOverrunsBeforeBypass) {
        bypassed_.store(true, std::memory_order_relaxed);
        return false;
      }
    } else if (overruns_ > 0) {
      --overruns_;
    }
    if (result != kResultOk) {
      return false;
    }
    for (int i = 0; i < frames; ++i) {
      interleavedStereo[i * 2] = outLeft_[static_cast<std::size_t>(i)];
      interleavedStereo[i * 2 + 1] = outRight_[static_cast<std::size_t>(i)];
    }
    return true;
  }

  const std::vector<PluginParameter>& parameters() const override { return parameters_; }

  void setParameter(std::uint32_t id, double normalised) override {
    if (controller_) {
      controller_->setParamNormalized(id, normalised);
    }
  }

  double parameter(std::uint32_t id) const override {
    return controller_ ? controller_->getParamNormalized(id) : 0.0;
  }

  void noteOn(int channel, int key, double velocity) override {
    (void)channel; (void)key; (void)velocity;
    // Note delivery arrives with the instrument work; an effect ignores it,
    // which is what every effect would do with it anyway.
  }
  void noteOff(int channel, int key) override { (void)channel; (void)key; }

  std::string saveState() const override { return {}; }
  bool loadState(const std::string& state) override { (void)state; return false; }

  bool overran() const override { return bypassed_.load(std::memory_order_relaxed); }
  const PluginDescriptor& descriptor() const override { return descriptor_; }

 private:
  void readParameters() {
    if (!controller_) {
      return;
    }
    const int32 count = controller_->getParameterCount();
    parameters_.reserve(static_cast<std::size_t>(count));
    for (int32 i = 0; i < count; ++i) {
      Vst::ParameterInfo info {};
      if (controller_->getParameterInfo(i, info) != kResultOk) {
        continue;
      }
      PluginParameter p;
      p.id = static_cast<std::uint32_t>(info.id);
      // The SDK's strings are UTF-16; the inspector draws UTF-8.
      for (int c = 0; c < 128 && info.title[c] != 0; ++c) {
        const char16_t ch = static_cast<char16_t>(info.title[c]);
        if (ch < 0x80) {
          p.name.push_back(static_cast<char>(ch));
        } else {
          p.name.push_back('?');
        }
      }
      p.defaultValue = info.defaultNormalizedValue;
      p.automatable = (info.flags & Vst::ParameterInfo::kCanAutomate) != 0;
      parameters_.push_back(std::move(p));
    }
  }

  static constexpr int kOverrunsBeforeBypass = 8;

  std::shared_ptr<Vst3Module> module_;
  PluginDescriptor descriptor_;
  Vst::IComponent* component_ = nullptr;
  Vst::IAudioProcessor* processor_ = nullptr;
  Vst::IEditController* controller_ = nullptr;
  std::vector<PluginParameter> parameters_;
  std::vector<float> left_, right_, outLeft_, outRight_;
  double sampleRate_ = 48000.0;
  int maxBlock_ = 0;
  bool hasInput_ = true;
  bool hasEventInput_ = false;
  int overruns_ = 0;
  std::atomic<bool> bypassed_ {false};
};

namespace {

// Modules are shared: two cues using the same reverb load it once.
std::shared_ptr<Vst3Module> moduleFor(const std::string& path) {
  static std::mutex mutex;
  static std::vector<std::pair<std::string, std::weak_ptr<Vst3Module>>> cache;
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& entry : cache) {
    if (entry.first == path) {
      if (auto live = entry.second.lock()) {
        return live;
      }
    }
  }
  auto module = std::make_shared<Vst3Module>(std::filesystem::path(path));
  if (!module->valid()) {
    return nullptr;
  }
  cache.emplace_back(path, module);
  return module;
}

}  // namespace

std::unique_ptr<AudioPluginInstance> openVst3Plugin(const std::string& path,
                                                    double sampleRate,
                                                    int maxBlockFrames) {
  auto module = moduleFor(path);
  if (!module) {
    return nullptr;
  }
  IPluginFactory* factory = module->factory();
  const int32 classCount = factory->countClasses();
  for (int32 i = 0; i < classCount; ++i) {
    PClassInfo info {};
    if (factory->getClassInfo(i, &info) != kResultOk) {
      continue;
    }
    if (std::strcmp(info.category, kVstAudioEffectClass) != 0) {
      continue;
    }
    PluginDescriptor d;
    d.format = "vst3";
    d.path = path;
    d.name = info.name;
    d.id = "vst3:" + path;
    auto instance = std::make_unique<Vst3Instance>(module, d);
    if (instance->open(info.cid, sampleRate, maxBlockFrames)) {
      return instance;
    }
  }
  return nullptr;
}

}  // namespace deckboy::platform::audioplugin
