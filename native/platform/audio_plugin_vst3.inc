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
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
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
DEF_CLASS_IID(IBStream)
namespace Vst {
// Only the interfaces this host actually asks for. Each DEF_CLASS_IID defines a
// symbol, so listing one whose header is not included does not fail to link --
// it fails to compile, which is the better end of that trade.
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IHostApplication)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
DEF_CLASS_IID(IConnectionPoint)
DEF_CLASS_IID(IMessage)
DEF_CLASS_IID(IAttributeList)
}  // namespace Vst
}  // namespace Steinberg

namespace deckboy::platform::audioplugin {
namespace {

using namespace Steinberg;

// ── WHAT A PLUGIN'S TWO HALVES SAY TO EACH OTHER ────────────────────────────
//
// A VST3 plugin is often TWO objects -- the processor and the editor -- and
// they are not allowed to know about each other. They talk through messages
// the HOST creates, which is why a host that answers "not implemented" to
// createInstance leaves them unable to say anything.
//
// The cost of that is not subtle and it is not a missing editor: a JUCE-built
// plugin builds its PARAMETER LIST when its two halves are introduced, so
// without this every plugin on the machine reported zero controls and the
// inspector had nothing to draw. Measured, not assumed -- see the DBG trail in
// the commit that added it.
//
// So here are the two objects the spec asks the host for, at their smallest
// honest size. Refcounted properly, because the plugin owns them once it has
// them and frees them on its own schedule.
class HostAttributeList : public Vst::IAttributeList {
 public:
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    if (FUnknownPrivate::iidEqual(iid, Vst::IAttributeList::iid) ||
        FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
      *obj = static_cast<Vst::IAttributeList*>(this);
      addRef();
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override {
    return static_cast<uint32>(++refs_);
  }
  uint32 PLUGIN_API release() override {
    const int left = --refs_;
    if (left <= 0) {
      delete this;
      return 0;
    }
    return static_cast<uint32>(left);
  }

  tresult PLUGIN_API setInt(AttrID id, int64 value) override {
    values_[id].kind = Value::Int;
    values_[id].i = value;
    return kResultOk;
  }
  tresult PLUGIN_API getInt(AttrID id, int64& value) override {
    const auto it = values_.find(id);
    if (it == values_.end() || it->second.kind != Value::Int) {
      return kResultFalse;
    }
    value = it->second.i;
    return kResultOk;
  }
  tresult PLUGIN_API setFloat(AttrID id, double value) override {
    values_[id].kind = Value::Float;
    values_[id].f = value;
    return kResultOk;
  }
  tresult PLUGIN_API getFloat(AttrID id, double& value) override {
    const auto it = values_.find(id);
    if (it == values_.end() || it->second.kind != Value::Float) {
      return kResultFalse;
    }
    value = it->second.f;
    return kResultOk;
  }
  tresult PLUGIN_API setString(AttrID id, const Vst::TChar* string) override {
    Value& v = values_[id];
    v.kind = Value::String;
    v.s.clear();
    if (string) {
      for (const Vst::TChar* p = string; *p != 0; ++p) {
        v.s.push_back(static_cast<char16_t>(*p));
      }
    }
    return kResultOk;
  }
  tresult PLUGIN_API getString(AttrID id, Vst::TChar* string,
                               uint32 sizeInBytes) override {
    const auto it = values_.find(id);
    if (it == values_.end() || it->second.kind != Value::String || !string) {
      return kResultFalse;
    }
    const std::size_t room = sizeInBytes / sizeof(Vst::TChar);
    if (room == 0) {
      return kResultFalse;
    }
    const std::size_t take = std::min(room - 1, it->second.s.size());
    for (std::size_t i = 0; i < take; ++i) {
      string[i] = static_cast<Vst::TChar>(it->second.s[i]);
    }
    string[take] = 0;
    return kResultOk;
  }
  tresult PLUGIN_API setBinary(AttrID id, const void* data,
                               uint32 sizeInBytes) override {
    Value& v = values_[id];
    v.kind = Value::Binary;
    v.b.assign(static_cast<const char*>(data),
               static_cast<const char*>(data) + sizeInBytes);
    return kResultOk;
  }
  tresult PLUGIN_API getBinary(AttrID id, const void*& data,
                               uint32& sizeInBytes) override {
    const auto it = values_.find(id);
    if (it == values_.end() || it->second.kind != Value::Binary) {
      return kResultFalse;
    }
    data = it->second.b.data();
    sizeInBytes = static_cast<uint32>(it->second.b.size());
    return kResultOk;
  }

 private:
  struct Value {
    enum Kind { Int, Float, String, Binary } kind = Int;
    int64 i = 0;
    double f = 0.0;
    std::u16string s;
    std::vector<char> b;
  };
  std::map<std::string, Value> values_;
  std::atomic<int> refs_ {1};
};

class HostMessage : public Vst::IMessage {
 public:
  HostMessage() : attributes_(new HostAttributeList()) {}
  ~HostMessage() {
    if (attributes_) {
      attributes_->release();
    }
  }

  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    if (FUnknownPrivate::iidEqual(iid, Vst::IMessage::iid) ||
        FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
      *obj = static_cast<Vst::IMessage*>(this);
      addRef();
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override {
    return static_cast<uint32>(++refs_);
  }
  uint32 PLUGIN_API release() override {
    const int left = --refs_;
    if (left <= 0) {
      delete this;
      return 0;
    }
    return static_cast<uint32>(left);
  }

  FIDString PLUGIN_API getMessageID() override {
    return id_.empty() ? nullptr : id_.c_str();
  }
  void PLUGIN_API setMessageID(FIDString id) override {
    id_ = id ? id : "";
  }
  Vst::IAttributeList* PLUGIN_API getAttributes() override { return attributes_; }

 private:
  std::string id_;
  HostAttributeList* attributes_ = nullptr;
  std::atomic<int> refs_ {1};
};

// ── The host, as the plugin sees it ─────────────────────────────────────────
//
// Plugins ask the host for its name and, during initialize(), for the message
// and attribute-list objects above. A host that answers nothing is refused by
// a fair number of them and leaves the rest half-built, so this answers what
// it can and says no to the rest.
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
  tresult PLUGIN_API createInstance(TUID cid, TUID iid, void** obj) override {
    // A message and an attribute list are the two things this is ever asked
    // for, and both are the host's to provide -- see HostMessage above for
    // what refusing them costs.
    if (FUnknownPrivate::iidEqual(cid, Vst::IMessage::iid) &&
        FUnknownPrivate::iidEqual(iid, Vst::IMessage::iid)) {
      *obj = static_cast<Vst::IMessage*>(new HostMessage());
      return kResultOk;
    }
    if (FUnknownPrivate::iidEqual(cid, Vst::IAttributeList::iid) &&
        FUnknownPrivate::iidEqual(iid, Vst::IAttributeList::iid)) {
      *obj = static_cast<Vst::IAttributeList*>(new HostAttributeList());
      return kResultOk;
    }
    *obj = nullptr;
    return kNotImplemented;
  }
};

DeckboyHostContext& hostContext() {
  static DeckboyHostContext context;
  return context;
}

// ── A KNOB REACHES THE PROCESSOR, NOT THE EDITOR ────────────────────────────
//
// setParamNormalized on the controller moves the plugin's OWN UI and nothing
// else. The thing making the sound is the processor, and the only route to it
// is data.inputParameterChanges on the next process call. A host that skips
// this has parameters that visibly move and audibly do nothing -- the exact
// shape of the "control that does nothing" bug, and worth the two small
// classes below to not ship.
//
// Both are fixed-size and preallocated, because they are filled on the audio
// thread. One point per parameter per block: sample-accurate automation inside
// a 40ms block is not something a cue deck's four mapped knobs need.
class HostParamQueue : public Vst::IParamValueQueue {
 public:
  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    if (FUnknownPrivate::iidEqual(iid, Vst::IParamValueQueue::iid) ||
        FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
      *obj = static_cast<Vst::IParamValueQueue*>(this);
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }

  Vst::ParamID PLUGIN_API getParameterId() override { return id_; }
  int32 PLUGIN_API getPointCount() override { return used_ ? 1 : 0; }
  tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset,
                              Vst::ParamValue& value) override {
    if (index != 0 || !used_) {
      return kResultFalse;
    }
    sampleOffset = 0;
    value = value_;
    return kResultOk;
  }
  tresult PLUGIN_API addPoint(int32 /*sampleOffset*/, Vst::ParamValue value,
                              int32& index) override {
    // A plugin is allowed to write back into the queue it was handed. Taking
    // the value and pinning it to the start of the block keeps that honest
    // without needing to grow anything.
    value_ = value;
    used_ = true;
    index = 0;
    return kResultOk;
  }

  void set(Vst::ParamID id, double value) {
    id_ = id;
    value_ = value;
    used_ = true;
  }
  void clear() { used_ = false; }
  Vst::ParamID id() const { return id_; }
  bool used() const { return used_; }

 private:
  Vst::ParamID id_ = 0;
  Vst::ParamValue value_ = 0.0;
  bool used_ = false;
};

class HostParamChanges : public Vst::IParameterChanges {
 public:
  static constexpr int kMaxQueues = 16;

  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    if (FUnknownPrivate::iidEqual(iid, Vst::IParameterChanges::iid) ||
        FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
      *obj = static_cast<Vst::IParameterChanges*>(this);
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }

  int32 PLUGIN_API getParameterCount() override { return count_; }
  Vst::IParamValueQueue* PLUGIN_API getParameterData(int32 index) override {
    if (index < 0 || index >= count_) {
      return nullptr;
    }
    return &queues_[index];
  }
  Vst::IParamValueQueue* PLUGIN_API addParameterData(const Vst::ParamID& id,
                                                     int32& index) override {
    for (int32 i = 0; i < count_; ++i) {
      if (queues_[i].id() == id) {
        index = i;
        return &queues_[i];
      }
    }
    if (count_ >= kMaxQueues) {
      index = 0;
      return nullptr;
    }
    index = count_;
    queues_[count_].set(id, 0.0);
    queues_[count_].clear();
    ++count_;
    return &queues_[index];
  }

  void reset() {
    for (int32 i = 0; i < count_; ++i) {
      queues_[i].clear();
    }
    count_ = 0;
  }
  // Coalesce by id: the last value written in a block is the one that block
  // gets, which is what riding a knob means.
  void push(Vst::ParamID id, double value) {
    for (int32 i = 0; i < count_; ++i) {
      if (queues_[i].id() == id) {
        queues_[i].set(id, value);
        return;
      }
    }
    if (count_ >= kMaxQueues) {
      return;
    }
    queues_[count_].set(id, value);
    ++count_;
  }
  bool empty() const { return count_ == 0; }

 private:
  HostParamQueue queues_[kMaxQueues];
  int32 count_ = 0;
};

// ── THE PLUGIN'S OWN SETTINGS, AS BYTES ─────────────────────────────────────
//
// getState/setState speak IBStream, so a host that wants to put a plugin's
// settings in a show file has to provide one. This is the whole of it: a
// std::string with a cursor. Never touched on the audio thread.
class MemoryStream : public IBStream {
 public:
  MemoryStream() = default;
  explicit MemoryStream(std::string data) : data_(std::move(data)) {}

  tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
    if (FUnknownPrivate::iidEqual(iid, IBStream::iid) ||
        FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
      *obj = static_cast<IBStream*>(this);
      return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }

  tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override {
    if (numBytes < 0) {
      return kInvalidArgument;
    }
    const std::size_t left = data_.size() - std::min(cursor_, data_.size());
    const std::size_t take = std::min(left, static_cast<std::size_t>(numBytes));
    if (take > 0) {
      std::memcpy(buffer, data_.data() + cursor_, take);
      cursor_ += take;
    }
    if (numBytesRead) {
      *numBytesRead = static_cast<int32>(take);
    }
    return kResultOk;
  }
  tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override {
    if (numBytes < 0) {
      return kInvalidArgument;
    }
    if (cursor_ > data_.size()) {
      data_.resize(cursor_, '\0');
    }
    const std::size_t end = cursor_ + static_cast<std::size_t>(numBytes);
    if (end > data_.size()) {
      data_.resize(end, '\0');
    }
    std::memcpy(&data_[cursor_], buffer, static_cast<std::size_t>(numBytes));
    cursor_ = end;
    if (numBytesWritten) {
      *numBytesWritten = numBytes;
    }
    return kResultOk;
  }
  tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override {
    int64 target = pos;
    if (mode == kIBSeekCur) {
      target = static_cast<int64>(cursor_) + pos;
    } else if (mode == kIBSeekEnd) {
      target = static_cast<int64>(data_.size()) + pos;
    }
    if (target < 0) {
      return kInvalidArgument;
    }
    cursor_ = static_cast<std::size_t>(target);
    if (result) {
      *result = target;
    }
    return kResultOk;
  }
  tresult PLUGIN_API tell(int64* pos) override {
    if (pos) {
      *pos = static_cast<int64>(cursor_);
    }
    return kResultOk;
  }

  const std::string& data() const { return data_; }
  void rewind() { cursor_ = 0; }

 private:
  std::string data_;
  std::size_t cursor_ = 0;
};

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
    // Introduced, so they have to be parted -- a plugin whose two halves are
    // still connected while one of them is being terminated is a crash.
    if (componentPoint_ && controllerPoint_) {
      componentPoint_->disconnect(controllerPoint_);
      controllerPoint_->disconnect(componentPoint_);
    }
    if (componentPoint_) {
      componentPoint_->release();
    }
    if (controllerPoint_) {
      controllerPoint_->release();
    }
    if (controller_) {
      // TERMINATE ONLY WHAT WE CREATED. A single-component plugin hands back
      // the SAME object for the component and the controller, so terminating
      // both is terminating one object twice -- which is a crash on a good
      // plugin and undefined behaviour on the rest. Nothing caught this while
      // instances were only ever opened and kept; the first thing that closed
      // one segfaulted on the spot.
      if (ownsController_) {
        controller_->terminate();
      }
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
          ownsController_ = true;
          controller_->initialize(&hostContext());
        }
      }
    }
    // INTRODUCE THE TWO HALVES. A split plugin's processor and editor reach
    // each other only through a connection the host makes, and until it is
    // made a JUCE-built plugin -- which is most of them -- has not yet built
    // its parameter list. Every plugin on this machine reported zero controls
    // until this was here.
    if (controller_ && ownsController_) {
      if (component_->queryInterface(Vst::IConnectionPoint::iid,
                                     reinterpret_cast<void**>(&componentPoint_)) == kResultOk &&
          controller_->queryInterface(Vst::IConnectionPoint::iid,
                                      reinterpret_cast<void**>(&controllerPoint_)) == kResultOk &&
          componentPoint_ && controllerPoint_) {
        // Connected DIRECTLY to each other: the spec allows the host to relay
        // instead, and relaying buys nothing when both ends are in this
        // process and neither is on the audio thread yet.
        componentPoint_->connect(controllerPoint_);
        controllerPoint_->connect(componentPoint_);
      }
      // And hand the editor the processor's state, so the values it shows are
      // the ones that will be heard rather than the factory defaults.
      MemoryStream initial;
      if (component_->getState(&initial) == kResultOk) {
        initial.rewind();
        controller_->setComponentState(&initial);
      }
    }

    // Stereo in, stereo out. An instrument has no input bus, which is not an
    // error -- it is the difference between a reverb and a synth.
    Vst::SpeakerArrangement stereo = Vst::SpeakerArr::kStereo;
    const int32 inputCount = component_->getBusCount(Vst::kAudio, Vst::kInput);
    const int32 outputCount = component_->getBusCount(Vst::kAudio, Vst::kOutput);
    hasInput_ = inputCount > 0;
    if (!hasInput_) {
      descriptor_.isInstrument = true;   // nothing to treat: it makes sound
    }
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

    // Everything the main thread turned since the last block, coalesced by id.
    changes_.reset();
    const unsigned written = ringWrite_.load(std::memory_order_acquire);
    // A consumer more than a ring behind has been overtaken; start from the
    // oldest entry that is still there rather than replaying a lap of stale
    // values. The last value for each id is still the one that lands.
    if (written - ringRead_ > kRingSize) {
      ringRead_ = written - kRingSize;
    }
    for (; ringRead_ != written; ++ringRead_) {
      const PendingParam& p = ring_[ringRead_ % kRingSize];
      changes_.push(static_cast<Vst::ParamID>(p.id), p.value);
    }

    Vst::ProcessData data {};
    data.processMode = Vst::kRealtime;
    data.symbolicSampleSize = Vst::kSample32;
    data.inputParameterChanges = changes_.empty() ? nullptr : &changes_;
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
    // The editor's copy, so a plugin UI opened later shows the right value...
    if (controller_) {
      controller_->setParamNormalized(id, normalised);
    }
    // ...and the processor's, which is the one that makes the sound. Handed
    // over through a single-producer ring rather than written into the change
    // list directly: this is called from the main thread while the audio thread
    // is inside process().
    const unsigned slot = ringWrite_.load(std::memory_order_relaxed);
    ring_[slot % kRingSize] = PendingParam {id, normalised};
    ringWrite_.store(slot + 1, std::memory_order_release);
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

  // THE PROCESSOR'S STATE, not the editor's. A plugin keeps both, and the one
  // that decides what a show sounds like is the component's -- the controller
  // is told about it on load so its UI agrees. Main thread only.
  std::string saveState() const override {
    if (!component_) {
      return {};
    }
    MemoryStream stream;
    if (component_->getState(&stream) != kResultOk) {
      return {};
    }
    return stream.data();
  }

  bool loadState(const std::string& state) override {
    if (!component_ || state.empty()) {
      return false;
    }
    MemoryStream stream(state);
    if (component_->setState(&stream) != kResultOk) {
      return false;
    }
    if (controller_) {
      stream.rewind();
      controller_->setComponentState(&stream);
    }
    return true;
  }

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
  bool ownsController_ = false;    // true only for a separately created one
  Vst::IConnectionPoint* componentPoint_ = nullptr;
  Vst::IConnectionPoint* controllerPoint_ = nullptr;
  int overruns_ = 0;
  std::atomic<bool> bypassed_ {false};

  // Knob turns crossing from the main thread to the audio thread. One writer,
  // one reader, no lock: a parameter that arrives a block late is inaudible,
  // and a main thread that could block the audio thread is not.
  static constexpr unsigned kRingSize = 64;
  struct PendingParam {
    std::uint32_t id = 0;
    double value = 0.0;
  };
  PendingParam ring_[kRingSize] {};
  std::atomic<unsigned> ringWrite_ {0};
  unsigned ringRead_ = 0;          // audio thread only
  HostParamChanges changes_;       // audio thread only, preallocated
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
    // WHETHER IT MAKES SOUND OR TREATS IT, which decides whether an empty
    // chain slot going quiet is a fault or the plugin waiting for a note.
    // The factory says so directly where it implements the richer class info;
    // where it does not, the absence of an input bus says it in open().
    if (IPluginFactory2* factory2 = nullptr;
        factory->queryInterface(IPluginFactory2::iid,
                                reinterpret_cast<void**>(&factory2)) == kResultOk &&
        factory2) {
      PClassInfo2 info2 {};
      if (factory2->getClassInfo2(i, &info2) == kResultOk) {
        d.category = info2.subCategories;
        d.vendor = info2.vendor;
        d.isInstrument = d.category.find("Instrument") != std::string::npos;
      }
      factory2->release();
    }
    auto instance = std::make_unique<Vst3Instance>(module, d);
    if (instance->open(info.cid, sampleRate, maxBlockFrames)) {
      return instance;
    }
  }
  return nullptr;
}

}  // namespace deckboy::platform::audioplugin
