// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "platform/asio_audio.hpp"

#include "core/sdl_compat.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#if defined(DECKBOY_HAS_ASIO)
#include <windows.h>
// Explicit: the SDK headers trim the Windows includes, so CoInitializeEx and
// friends are not declared by <windows.h> alone here.
#include <objbase.h>
#include "asiosys.h"
#include "asio.h"
#include "asiodrivers.h"
#endif

namespace deckboy {
namespace platform {
namespace audio {

#if defined(DECKBOY_HAS_ASIO)
namespace {

// The SDK's AsioDrivers keeps global state and the ASIO C API is a SINGLETON --
// asioGetChannels() and friends address "the currently loaded driver", not a
// handle. Two threads probing at once would silently read each other's driver,
// so every entry point here serialises on this.
std::mutex& asioMutex() {
  static std::mutex m;
  return m;
}

// COM must be initialised on the calling thread before a driver is loaded.
// Deckboy's main thread already runs COM for other reasons, but this is called
// from wherever the settings UI happens to be, so it is made explicit and
// tolerant: RPC_E_CHANGED_MODE means COM is already up in another apartment,
// which is fine and must NOT be treated as failure.
struct ComScope {
  bool uninit = false;
  ComScope() {
    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    uninit = SUCCEEDED(hr);
  }
  ~ComScope() {
    if (uninit) ::CoUninitialize();
  }
};

}  // namespace
#endif

bool asioSupportCompiled() {
#if defined(DECKBOY_HAS_ASIO)
  return true;
#else
  return false;
#endif
}

std::vector<AsioDeviceInfo> listAsioDevices() {
  std::vector<AsioDeviceInfo> devices;
#if defined(DECKBOY_HAS_ASIO)
  std::lock_guard<std::mutex> lock(asioMutex());
  ComScope com;
  AsioDrivers drivers;
  const long count = drivers.asioGetNumDev();
  for (long i = 0; i < count; ++i) {
    char name[64] = {};
    if (drivers.asioGetDriverName(static_cast<int>(i), name, sizeof(name) - 1) != 0) {
      continue;   // registry entry we cannot read; skip rather than show blank
    }
    AsioDeviceInfo info;
    info.name = name;
    if (!info.name.empty()) {
      devices.push_back(std::move(info));
    }
  }
#endif
  return devices;
}

bool probeAsioDevice(const std::string& name, AsioDeviceInfo& out) {
  out = AsioDeviceInfo{};
  out.name = name;
#if !defined(DECKBOY_HAS_ASIO)
  out.error = "built without ASIO support";
  return false;
#else
  if (name.empty()) {
    out.error = "no driver named";
    return false;
  }
  std::lock_guard<std::mutex> lock(asioMutex());
  ComScope com;
  AsioDrivers drivers;
  // loadDriver takes a non-const char*, which is why this copies.
  std::vector<char> mutableName(name.begin(), name.end());
  mutableName.push_back('\0');
  if (!drivers.loadDriver(mutableName.data())) {
    out.error = "driver would not load (in use by another application?)";
    return false;
  }

  // ASIOInit must succeed before any other call means anything. Its driverInfo
  // carries the driver's own error text, which is far more useful to an
  // operator than anything this layer could invent.
  ASIODriverInfo driverInfo = {};
  driverInfo.asioVersion = 2;
  driverInfo.sysRef = nullptr;
  if (ASIOInit(&driverInfo) != ASE_OK) {
    out.error = driverInfo.errorMessage[0] ? driverInfo.errorMessage
                                           : "ASIOInit failed";
    drivers.removeCurrentDriver();
    return false;
  }

  long inputs = 0, outputs = 0;
  if (ASIOGetChannels(&inputs, &outputs) == ASE_OK) {
    out.inputChannels = static_cast<int>(inputs);
    out.outputChannels = static_cast<int>(outputs);
  }

  long minSize = 0, maxSize = 0, preferred = 0, granularity = 0;
  if (ASIOGetBufferSize(&minSize, &maxSize, &preferred, &granularity) == ASE_OK) {
    out.preferredBufferFrames = static_cast<int>(preferred);
  }

  ASIOSampleRate rate = 0.0;
  if (ASIOGetSampleRate(&rate) == ASE_OK) {
    out.sampleRate = static_cast<double>(rate);
  }

  long inLat = 0, outLat = 0;
  if (ASIOGetLatencies(&inLat, &outLat) == ASE_OK) {
    out.outputLatencyFrames = static_cast<int>(outLat);
  }

  // Release immediately. Holding a driver open blocks every other application
  // on the machine from using the interface, which during a show would be the
  // worst kind of side effect from merely LOOKING at a settings page.
  drivers.removeCurrentDriver();
  out.probed = true;
  return true;
#endif
}


// ---------------------------------------------------------------------------
// AsioOutput implementation
// ---------------------------------------------------------------------------
#if defined(DECKBOY_HAS_ASIO)
namespace {

// ASIO's callbacks are bare C function pointers with NO user-data argument, so
// the running stream has to be reachable from file scope. That is tolerable
// only because ASIO is a process-wide singleton anyway: there can be exactly
// one loaded driver, therefore exactly one stream.
struct AsioRuntime {
  AsioDrivers drivers;
  std::vector<ASIOBufferInfo> bufferInfos;
  std::vector<ASIOSampleType> sampleTypes;
  ASIOCallbacks callbacks {};
  int channels = 0;
  long bufferFrames = 0;
  double sampleRate = 0.0;
  bool started = false;
  bool outputReadySupported = false;   // queried once at open, not per callback
  long outputLatency = 0;              // frames, from ASIOGetLatencies
  // Rate conversion, when the device does not run at the source rate. SDL's
  // audio stream is used purely as a resampler here -- it is already a
  // dependency and its conversion quality is better than anything worth
  // hand-rolling for this.
  SDL_AudioStream* resampler = nullptr;
  double sourceRate = 0.0;
  std::vector<std::int16_t> convertScratch;

  // Single-producer / single-consumer ring of interleaved s16. The writer is
  // the audio thread, the reader is the driver callback; head and tail are
  // atomics and neither side ever locks, because the callback must not block.
  std::vector<std::int16_t> ring;
  std::atomic<std::size_t> head {0};   // write position, in SAMPLES
  std::atomic<std::size_t> tail {0};   // read position, in SAMPLES
  std::atomic<std::uint64_t> underruns {0};
};

std::mutex gRuntimeMutex;              // guards open/close, never the callback
AsioRuntime* gRuntime = nullptr;

// Write one sample into the driver's buffer in whatever type it wants. Only
// the LSB (little-endian) types are handled: every Windows ASIO driver in
// practice uses one of these, and guessing at the big-endian or packed
// variants would be worse than refusing them at open() with a clear message.
inline void storeSample(void* dst, long frame, ASIOSampleType type, std::int16_t v) {
  switch (type) {
    case ASIOSTInt16LSB:
      static_cast<std::int16_t*>(dst)[frame] = v;
      break;
    case ASIOSTInt32LSB:
      // 16-bit source in a 32-bit container: shift up rather than sign-extend
      // into the low bits, or everything plays 48dB quiet.
      static_cast<std::int32_t*>(dst)[frame] = static_cast<std::int32_t>(v) << 16;
      break;
    case ASIOSTFloat32LSB:
      static_cast<float*>(dst)[frame] = static_cast<float>(v) / 32768.0f;
      break;
    case ASIOSTInt24LSB: {
      // Three bytes per sample, little-endian, no padding.
      auto* bytes = static_cast<std::uint8_t*>(dst) + frame * 3;
      const std::int32_t s = static_cast<std::int32_t>(v) << 8;
      bytes[0] = static_cast<std::uint8_t>(s & 0xFF);
      bytes[1] = static_cast<std::uint8_t>((s >> 8) & 0xFF);
      bytes[2] = static_cast<std::uint8_t>((s >> 16) & 0xFF);
      break;
    }
    default:
      break;   // refused at open(); nothing sensible to do here
  }
}

inline bool sampleTypeSupported(ASIOSampleType t) {
  return t == ASIOSTInt16LSB || t == ASIOSTInt24LSB ||
         t == ASIOSTInt32LSB || t == ASIOSTFloat32LSB;
}

// THE REAL-TIME CALLBACK. No allocation, no locking, no logging, no early
// return that leaves buffers untouched -- an ASIO buffer that is not written
// plays whatever was in it last, which is a loud repeated fragment.
void bufferSwitch(long index, ASIOBool /*directProcess*/) {
  AsioRuntime* rt = gRuntime;
  if (!rt) return;

  const long frames = rt->bufferFrames;
  const int chans = rt->channels;
  const std::size_t ringSize = rt->ring.size();
  const std::size_t tail = rt->tail.load(std::memory_order_relaxed);
  const std::size_t head = rt->head.load(std::memory_order_acquire);
  const std::size_t availableSamples = head - tail;
  const std::size_t wantSamples = static_cast<std::size_t>(frames) * chans;
  const std::size_t haveFrames =
    std::min<std::size_t>(availableSamples / chans, static_cast<std::size_t>(frames));

  if (haveFrames < static_cast<std::size_t>(frames)) {
    rt->underruns.fetch_add(1, std::memory_order_relaxed);
  }

  // Walk the ring ONCE per channel with a running index instead of a modulo
  // per sample. In a callback with a hard deadline the difference is real:
  // this is chans * frames divisions removed from the critical path.
  for (int ch = 0; ch < chans; ++ch) {
    void* dst = rt->bufferInfos[ch].buffers[index];
    if (!dst) continue;
    const ASIOSampleType type = rt->sampleTypes[ch];
    std::size_t pos = (tail + static_cast<std::size_t>(ch)) % ringSize;
    for (std::size_t f = 0; f < haveFrames; ++f) {
      storeSample(dst, static_cast<long>(f), type, rt->ring[pos]);
      pos += static_cast<std::size_t>(chans);
      if (pos >= ringSize) pos -= ringSize;
    }
    // Silence the remainder rather than leaving stale audio behind.
    for (std::size_t f = haveFrames; f < static_cast<std::size_t>(frames); ++f) {
      storeSample(dst, static_cast<long>(f), type, 0);
    }
  }
  rt->tail.store(tail + haveFrames * chans, std::memory_order_release);
  (void)wantSamples;
  // Only when the driver actually supports it. Calling it blindly every
  // callback is a wasted driver round-trip on hardware that does not implement
  // it, in the one place where wasted work is measured in dropouts.
  if (rt->outputReadySupported) ASIOOutputReady();
}

void sampleRateDidChange(ASIOSampleRate /*rate*/) {}

long asioMessage(long selector, long value, void* /*msg*/, double* /*opt*/) {
  switch (selector) {
    case kAsioSelectorSupported:
      return (value == kAsioEngineVersion || value == kAsioResetRequest ||
              value == kAsioResyncRequest || value == kAsioLatenciesChanged ||
              value == kAsioSupportsTimeInfo) ? 1L : 0L;
    case kAsioEngineVersion:  return 2L;
    case kAsioResetRequest:   return 1L;
    case kAsioResyncRequest:  return 1L;
    case kAsioLatenciesChanged: return 1L;
    case kAsioSupportsTimeInfo: return 0L;   // plain bufferSwitch is enough
    default: return 0L;
  }
}

ASIOTime* bufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool directProcess) {
  bufferSwitch(index, directProcess);
  return params;
}

}  // namespace
#endif

AsioOutput::~AsioOutput() { close(); }

bool AsioOutput::open(const std::string& driverName, int channels,
                      double sourceRate, std::string& error) {
#if !defined(DECKBOY_HAS_ASIO)
  (void)driverName; (void)channels; (void)sourceRate;
  error = "built without ASIO support";
  return false;
#else
  std::lock_guard<std::mutex> lock(gRuntimeMutex);
  if (gRuntime) {
    error = "an ASIO stream is already open";
    return false;
  }
  if (channels < 1) { error = "channel count must be at least 1"; return false; }

  auto rt = std::make_unique<AsioRuntime>();
  std::vector<char> name(driverName.begin(), driverName.end());
  name.push_back('\0');
  if (!rt->drivers.loadDriver(name.data())) {
    error = "driver would not load (in use by another application?)";
    return false;
  }
  ASIODriverInfo info = {};
  info.asioVersion = 2;
  if (ASIOInit(&info) != ASE_OK) {
    error = info.errorMessage[0] ? info.errorMessage : "ASIOInit failed";
    rt->drivers.removeCurrentDriver();
    return false;
  }

  long maxIn = 0, maxOut = 0;
  if (ASIOGetChannels(&maxIn, &maxOut) != ASE_OK || maxOut <= 0) {
    error = "driver reports no output channels";
    rt->drivers.removeCurrentDriver();
    return false;
  }
  if (channels > static_cast<int>(maxOut)) {
    error = "driver has only " + std::to_string(maxOut) + " output channel(s)";
    rt->drivers.removeCurrentDriver();
    return false;
  }

  // Ask for the rate, then read back what we actually got. Drivers clocked to
  // external word clock will refuse, and pretending otherwise would resample
  // nothing and play everything at the wrong pitch.
  if (sourceRate > 0.0) {
    if (ASIOCanSampleRate(sourceRate) == ASE_OK) {
      ASIOSetSampleRate(sourceRate);
    }
  }
  ASIOSampleRate actual = 0.0;
  ASIOGetSampleRate(&actual);
  rt->sampleRate = static_cast<double>(actual);

  long minSize = 0, maxSize = 0, preferred = 0, granularity = 0;
  if (ASIOGetBufferSize(&minSize, &maxSize, &preferred, &granularity) != ASE_OK ||
      preferred <= 0) {
    error = "driver would not report a buffer size";
    rt->drivers.removeCurrentDriver();
    return false;
  }
  rt->bufferFrames = preferred;
  rt->channels = channels;

  rt->bufferInfos.resize(static_cast<std::size_t>(channels));
  rt->sampleTypes.resize(static_cast<std::size_t>(channels));
  for (int ch = 0; ch < channels; ++ch) {
    rt->bufferInfos[ch].isInput = ASIOFalse;
    rt->bufferInfos[ch].channelNum = ch;
    rt->bufferInfos[ch].buffers[0] = nullptr;
    rt->bufferInfos[ch].buffers[1] = nullptr;
  }

  rt->callbacks.bufferSwitch = &bufferSwitch;
  rt->callbacks.sampleRateDidChange = &sampleRateDidChange;
  rt->callbacks.asioMessage = &asioMessage;
  rt->callbacks.bufferSwitchTimeInfo = &bufferSwitchTimeInfo;

  if (ASIOCreateBuffers(rt->bufferInfos.data(), channels, preferred,
                        &rt->callbacks) != ASE_OK) {
    error = "driver would not create output buffers";
    rt->drivers.removeCurrentDriver();
    return false;
  }

  // Sample type is per channel and only knowable after buffers exist.
  for (int ch = 0; ch < channels; ++ch) {
    ASIOChannelInfo ci = {};
    ci.channel = ch;
    ci.isInput = ASIOFalse;
    if (ASIOGetChannelInfo(&ci) != ASE_OK) {
      error = "driver would not report channel info";
      ASIODisposeBuffers();
      rt->drivers.removeCurrentDriver();
      return false;
    }
    if (!sampleTypeSupported(ci.type)) {
      error = "unsupported ASIO sample type " + std::to_string(ci.type) +
              " on channel " + std::to_string(ch);
      ASIODisposeBuffers();
      rt->drivers.removeCurrentDriver();
      return false;
    }
    rt->sampleTypes[ch] = ci.type;
  }

  // Resample only when the rates actually differ. Creating a stream with
  // matched rates would add a pointless copy to every block.
  rt->sourceRate = sourceRate > 0.0 ? sourceRate : rt->sampleRate;
  if (std::abs(rt->sampleRate - rt->sourceRate) > 0.5) {
    SDL_AudioSpec src {};
    src.format = SDL_AUDIO_S16;
    src.channels = channels;
    src.freq = static_cast<int>(std::lround(rt->sourceRate));
    SDL_AudioSpec dst = src;
    dst.freq = static_cast<int>(std::lround(rt->sampleRate));
    rt->resampler = SDL_CreateAudioStream(&src, &dst);
    if (!rt->resampler) {
      error = "could not create a resampler for " +
              std::to_string(src.freq) + " -> " + std::to_string(dst.freq) + " Hz";
      ASIODisposeBuffers();
      rt->drivers.removeCurrentDriver();
      return false;
    }
  }

  // Ring holds ~8 driver buffers. Big enough to absorb a late writer, small
  // enough that stopping does not leave a second of stale audio to play out.
  rt->ring.assign(static_cast<std::size_t>(preferred) * channels * 8, 0);

  gRuntime = rt.get();
  if (ASIOStart() != ASE_OK) {
    gRuntime = nullptr;
    error = "driver refused to start";
    ASIODisposeBuffers();
    rt->drivers.removeCurrentDriver();
    return false;
  }
  // Latency is only meaningful once the buffers exist, which is why it is read
  // here rather than during the capability probe.
  {
    long inLat = 0, outLat = 0;
    if (ASIOGetLatencies(&inLat, &outLat) == ASE_OK) {
      rt->outputLatency = outLat;
    }
  }
  // Queried once. ASE_OK here means the driver implements the optimisation.
  rt->outputReadySupported = (ASIOOutputReady() == ASE_OK);
  rt->started = true;
  rt.release();   // ownership moves to gRuntime until close()
  return true;
#endif
}

void AsioOutput::close() {
#if defined(DECKBOY_HAS_ASIO)
  std::lock_guard<std::mutex> lock(gRuntimeMutex);
  if (!gRuntime) return;
  AsioRuntime* rt = gRuntime;
  // Clear the pointer BEFORE stopping so a callback already in flight sees
  // null and returns instead of touching buffers we are about to dispose.
  gRuntime = nullptr;
  if (rt->started) ASIOStop();
  ASIODisposeBuffers();
  if (rt->resampler) {
    SDL_DestroyAudioStream(rt->resampler);
    rt->resampler = nullptr;
  }
  rt->drivers.removeCurrentDriver();
  delete rt;
#endif
}

bool AsioOutput::running() const {
#if defined(DECKBOY_HAS_ASIO)
  return gRuntime != nullptr && gRuntime->started;
#else
  return false;
#endif
}

int AsioOutput::channels() const {
#if defined(DECKBOY_HAS_ASIO)
  return gRuntime ? gRuntime->channels : 0;
#else
  return 0;
#endif
}

double AsioOutput::sampleRate() const {
#if defined(DECKBOY_HAS_ASIO)
  return gRuntime ? gRuntime->sampleRate : 0.0;
#else
  return 0.0;
#endif
}

int AsioOutput::bufferFrames() const {
#if defined(DECKBOY_HAS_ASIO)
  return gRuntime ? static_cast<int>(gRuntime->bufferFrames) : 0;
#else
  return 0;
#endif
}

std::size_t AsioOutput::write(const std::int16_t* interleaved, std::size_t frames) {
#if !defined(DECKBOY_HAS_ASIO)
  (void)interleaved; (void)frames;
  return 0;
#else
  AsioRuntime* rt = gRuntime;
  if (!rt || !interleaved || frames == 0) return 0;
  const std::size_t chans = static_cast<std::size_t>(rt->channels);

  // Converting path: hand the block to SDL, take back whatever it has ready at
  // the device rate, and ring-buffer that. The caller is told it consumed the
  // WHOLE block, because it did -- the conversion holds the remainder, not the
  // caller.
  if (rt->resampler) {
    if (!SDL_PutAudioStreamData(rt->resampler, interleaved,
                                static_cast<int>(frames * chans * sizeof(std::int16_t)))) {
      return 0;
    }
    const int availBytes = SDL_GetAudioStreamAvailable(rt->resampler);
    if (availBytes > 0) {
      rt->convertScratch.resize(static_cast<std::size_t>(availBytes) / sizeof(std::int16_t));
      const int got = SDL_GetAudioStreamData(
        rt->resampler, rt->convertScratch.data(), availBytes);
      if (got > 0) {
        const std::size_t gotFrames =
          static_cast<std::size_t>(got) / sizeof(std::int16_t) / chans;
        // Recurse once through the direct path with the converted audio. Any
        // frames the ring cannot take are DROPPED rather than re-queued,
        // because the resampler has already advanced past them -- which is why
        // the ring is sized generously and the caller paces on queuedFrames().
        const std::size_t head = rt->head.load(std::memory_order_relaxed);
        const std::size_t tail = rt->tail.load(std::memory_order_acquire);
        const std::size_t ringSize = rt->ring.size();
        const std::size_t freeSamples = ringSize - (head - tail) - 1;
        const std::size_t take = std::min(gotFrames, freeSamples / chans);
        const std::size_t takeSamples = take * chans;
        const std::size_t start = head % ringSize;
        const std::size_t firstRun = std::min(takeSamples, ringSize - start);
        std::memcpy(rt->ring.data() + start, rt->convertScratch.data(),
                    firstRun * sizeof(std::int16_t));
        if (takeSamples > firstRun) {
          std::memcpy(rt->ring.data(), rt->convertScratch.data() + firstRun,
                      (takeSamples - firstRun) * sizeof(std::int16_t));
        }
        rt->head.store(head + takeSamples, std::memory_order_release);
      }
    }
    return frames;
  }

  const std::size_t ringSize = rt->ring.size();
  const std::size_t head = rt->head.load(std::memory_order_relaxed);
  const std::size_t tail = rt->tail.load(std::memory_order_acquire);
  // One sample of slack keeps head == tail meaning EMPTY rather than full.
  const std::size_t freeSamples = ringSize - (head - tail) - 1;
  const std::size_t writeFrames = std::min(frames, freeSamples / chans);
  // Two contiguous copies rather than a modulo per sample: the ring wraps at
  // most once per write, so find the split and memcpy each side.
  const std::size_t writeSamples = writeFrames * chans;
  const std::size_t start = head % ringSize;
  const std::size_t firstRun = std::min(writeSamples, ringSize - start);
  std::memcpy(rt->ring.data() + start, interleaved,
              firstRun * sizeof(std::int16_t));
  if (writeSamples > firstRun) {
    std::memcpy(rt->ring.data(), interleaved + firstRun,
                (writeSamples - firstRun) * sizeof(std::int16_t));
  }
  rt->head.store(head + writeFrames * chans, std::memory_order_release);
  return writeFrames;
#endif
}

std::size_t AsioOutput::queuedFrames() const {
#if defined(DECKBOY_HAS_ASIO)
  AsioRuntime* rt = gRuntime;
  if (!rt || rt->channels <= 0) return 0;
  const std::size_t head = rt->head.load(std::memory_order_acquire);
  const std::size_t tail = rt->tail.load(std::memory_order_acquire);
  std::size_t deviceFrames = (head - tail) / static_cast<std::size_t>(rt->channels);
  if (rt->resampler) {
    // Audio still inside the converter counts as queued too, or the caller
    // sees a shallower buffer than really exists and over-feeds.
    const int pending = SDL_GetAudioStreamAvailable(rt->resampler);
    if (pending > 0) {
      deviceFrames += static_cast<std::size_t>(pending) / sizeof(std::int16_t) /
                      static_cast<std::size_t>(rt->channels);
    }
    // Scale to SOURCE frames so the caller's pacing maths stays honest.
    if (rt->sampleRate > 0.0 && rt->sourceRate > 0.0) {
      deviceFrames = static_cast<std::size_t>(
        deviceFrames * (rt->sourceRate / rt->sampleRate));
    }
  }
  return deviceFrames;
#else
  return 0;
#endif
}

int AsioOutput::outputLatencyFrames() const {
#if defined(DECKBOY_HAS_ASIO)
  return gRuntime ? gRuntime->outputLatency : 0;
#else
  return 0;
#endif
}

double AsioOutput::outputLatencySeconds() const {
  const double rate = sampleRate();
  if (rate <= 0.0) return 0.0;
  return static_cast<double>(outputLatencyFrames()) / rate;
}

bool AsioOutput::resampling() const {
#if defined(DECKBOY_HAS_ASIO)
  return gRuntime && gRuntime->resampler != nullptr;
#else
  return false;
#endif
}

std::uint64_t AsioOutput::underruns() const {
#if defined(DECKBOY_HAS_ASIO)
  return gRuntime ? gRuntime->underruns.load(std::memory_order_relaxed) : 0;
#else
  return 0;
#endif
}

}  // namespace audio
}  // namespace platform
}  // namespace deckboy
