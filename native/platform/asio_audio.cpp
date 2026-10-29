// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "platform/asio_audio.hpp"

#include "core/sdl_compat.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <memory>
#include <utility>
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

// One producer's audio (see openSource in the header). A single-producer /
// single-consumer ring of interleaved s16 -- the producer is one engine's audio
// thread, the consumer the driver callback; head and tail are atomics and
// neither side ever locks, because the callback must not block -- and that
// producer's own rate converter.
class AsioOutput::Source {
 public:
  std::vector<std::int16_t> ring;
  std::atomic<std::size_t> head {0};   // write position, in SAMPLES
  std::atomic<std::size_t> tail {0};   // read position, in SAMPLES
  // Rate conversion, when the device does not run at the source rate. SDL's
  // audio stream is used purely as a resampler here -- it is already a
  // dependency and its conversion quality is better than anything worth
  // hand-rolling for this. One per source: a converter carries state from one
  // block to the next, and two producers through one would splice together.
  SDL_AudioStream* resampler = nullptr;
  std::vector<std::int16_t> convertScratch;
  int slot = -1;
  std::uint64_t generation = 0;        // which open() it was made for
  // A clear (see AsioOutput::clear): the writer cannot move the read position,
  // which belongs to the callback, so it says where to skip to and the
  // callback does the skipping. The count tells the callback a clear came,
  // even when there was nothing left to skip.
  std::atomic<std::size_t> flushTo {0};
  std::atomic<std::uint32_t> flushes {0};
  std::uint32_t flushesSeen = 0;       // callback only
  std::atomic<bool> paused {false};
  // A tape speed (setRatio): asked for by anyone, applied by the writer, which
  // is the only thread that touches the converter.
  std::atomic<double> ratio {1.0};
  double appliedRatio = 1.0;           // writer only
  // Underrun bookkeeping, touched only by the callback (see bufferSwitch).
  std::uint64_t lastFullBuffer = 0;
  std::uint64_t gapOpenedAt = 0;
  bool gapOpen = false;
  ~Source() {
    // SDL_Quit frees every audio stream itself; destroying this one after
    // that would free it twice.
    if (resampler && SDL_WasInit(SDL_INIT_AUDIO)) SDL_DestroyAudioStream(resampler);
  }
};

namespace {

// Decks, their outgoing tails while a transition finishes, and room to spare.
constexpr int kMaxAsioSources = 64;

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
  double sourceRate = 0.0;             // what the producers write
  bool started = false;
  bool outputReadySupported = false;   // queried once at open, not per callback
  long outputLatency = 0;              // frames, from ASIOGetLatencies
  std::uint64_t generation = 0;
  // Each source's ring: a second of sound (see open() for why it must be
  // deeper than the engine's lead).
  std::size_t ringSamples = 0;
  // The sources in the mix, published and withdrawn with atomic stores so the
  // callback walks them without a lock.
  std::array<std::atomic<AsioOutput::Source*>, kMaxAsioSources> sources {};
  // The callback's sum: one driver buffer of interleaved int32, sized at open
  // and never allocated in the callback.
  std::vector<std::int32_t> mix;
  // Withdrawn from the mix but perhaps still being read by a callback already
  // in flight. Each carries the callback count at the moment it was withdrawn
  // and is freed only once that many callbacks have FINISHED -- proof, not a
  // timeout. Half a second used to stand in for that proof, and a callback
  // descheduled for longer would have read freed memory (Codex audit
  // 2026-10-22, #1). Under gRuntimeMutex.
  std::vector<std::pair<std::unique_ptr<AsioOutput::Source>, std::uint64_t>> retired;
  // Callbacks entered and finished. The driver calls bufferSwitch on one
  // thread, one at a time, so "finished >= N" means callback N and every one
  // before it are done.
  std::atomic<std::uint64_t> callbacksEntered {0};
  std::atomic<std::uint64_t> callbacksFinished {0};
  std::atomic<std::uint64_t> underruns {0};
  std::uint64_t callbackCount = 0;     // callback only
  // DECKBOY_ASIO_CAPTURE=<file.wav>: what was handed to the driver, kept for a
  // test to read back, since a room cannot be measured from a script. The
  // buffer is allocated at open and the callback only copies into it -- no
  // allocation, no lock, no file -- and the WAV is written at close.
  std::string capturePath;
  std::vector<std::int16_t> capture;
  std::size_t captureUsed = 0;         // callback only, until close
};

std::mutex gRuntimeMutex;              // open/close and the source list, never the callback
AsioRuntime* gRuntime = nullptr;
std::uint64_t gNextGeneration = 1;     // under gRuntimeMutex

bool ratesDiffer(const AsioRuntime& rt) {
  return std::abs(rt.sampleRate - rt.sourceRate) > 0.5;
}

SDL_AudioStream* makeResampler(const AsioRuntime& rt) {
  SDL_AudioSpec src {};
  src.format = SDL_AUDIO_S16;
  src.channels = rt.channels;
  src.freq = static_cast<int>(std::lround(rt.sourceRate));
  SDL_AudioSpec dst = src;
  dst.freq = static_cast<int>(std::lround(rt.sampleRate));
  return SDL_CreateAudioStream(&src, &dst);
}

// Free what the callback can no longer be holding: every source withdrawn
// before a callback that has since finished. Under gRuntimeMutex.
void freeRetired(AsioRuntime& rt) {
  const std::uint64_t finished = rt.callbacksFinished.load(std::memory_order_acquire);
  rt.retired.erase(std::remove_if(rt.retired.begin(), rt.retired.end(),
                                  [finished](const auto& r) { return r.second <= finished; }),
                   rt.retired.end());
}

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
  // Count this callback in before reading any source, and out on every way
  // out. The fence pairs with the one after a withdrawal: either this callback
  // sees the empty slot, or the withdrawal sees this callback counted in and
  // waits for it to finish.
  const std::uint64_t epoch = rt->callbacksEntered.fetch_add(1, std::memory_order_seq_cst) + 1;
  std::atomic_thread_fence(std::memory_order_seq_cst);
  struct CountOut {
    AsioRuntime* rt;
    std::uint64_t epoch;
    ~CountOut() { rt->callbacksFinished.store(epoch, std::memory_order_release); }
  } countOut {rt, epoch};

  const long frames = rt->bufferFrames;
  const int chans = rt->channels;
  const std::size_t wantSamples = static_cast<std::size_t>(frames) * chans;
  std::int32_t* mix = rt->mix.data();
  std::fill(mix, mix + wantSamples, 0);

  // SUM every source that has something.
  //
  // AN UNDERRUN IS A GAP IN THE MIDDLE OF PLAYING, and only that. A source that
  // runs short and never comes back is a cue that ended; one that is empty is a
  // deck with nothing on. Counting either would tick the operator's underrun
  // count at every cue end. So a short buffer opens a gap, and the gap counts
  // only if the same source has sound again within about a quarter of a second.
  const std::uint64_t now = ++rt->callbackCount;
  const std::uint64_t gapWindow = std::max<std::uint64_t>(
    2, static_cast<std::uint64_t>(rt->sampleRate * 0.25) / static_cast<std::uint64_t>(frames));
  for (auto& slot : rt->sources) {
    AsioOutput::Source* src = slot.load(std::memory_order_acquire);
    if (!src) continue;
    const std::size_t ringSize = src->ring.size();
    std::size_t tail = src->tail.load(std::memory_order_relaxed);
    // A clear first, so a stop never plays what was queued before it. It
    // closes any gap too: a stop is not a dropout, and a cut to the next cue
    // within the gap window would otherwise count as one.
    const std::uint32_t flushes = src->flushes.load(std::memory_order_acquire);
    if (flushes != src->flushesSeen) {
      src->flushesSeen = flushes;
      const std::size_t to = src->flushTo.load(std::memory_order_relaxed);
      if (to > tail) {
        tail = to;
        src->tail.store(tail, std::memory_order_release);
      }
      src->gapOpen = false;
      src->lastFullBuffer = 0;
    }
    // Paused: out of the mix, ring untouched, and no gap -- the same reasons.
    if (src->paused.load(std::memory_order_acquire)) {
      src->gapOpen = false;
      src->lastFullBuffer = 0;
      continue;
    }
    const std::size_t head = src->head.load(std::memory_order_acquire);
    const std::size_t haveFrames =
      std::min<std::size_t>((head - tail) / chans, static_cast<std::size_t>(frames));
    const bool full = haveFrames == static_cast<std::size_t>(frames);
    if (src->gapOpen && haveFrames > 0) {
      if (now - src->gapOpenedAt <= gapWindow) {
        rt->underruns.fetch_add(1, std::memory_order_relaxed);
      }
      src->gapOpen = false;
    }
    if (full) {
      src->lastFullBuffer = now;
    } else if (!src->gapOpen && src->lastFullBuffer == now - 1) {
      src->gapOpen = true;
      src->gapOpenedAt = now;
    }
    if (haveFrames == 0) continue;
    // One pass with a running index rather than a modulo per sample: in a
    // callback with a hard deadline the difference is real.
    const std::size_t samples = haveFrames * chans;
    std::size_t pos = tail % ringSize;
    for (std::size_t i = 0; i < samples; ++i) {
      mix[i] += src->ring[pos];
      if (++pos == ringSize) pos = 0;
    }
    src->tail.store(tail + samples, std::memory_order_release);
  }

  // Out to the driver, every frame of every channel -- no early return that
  // leaves buffers untouched: an ASIO buffer that is not written plays
  // whatever was in it last, which is a loud repeated fragment. Two sources
  // can add up past full scale, so the sum is clipped, not wrapped.
  for (int ch = 0; ch < chans; ++ch) {
    void* dst = rt->bufferInfos[ch].buffers[index];
    if (!dst) continue;
    const ASIOSampleType type = rt->sampleTypes[ch];
    const std::int32_t* in = mix + ch;
    for (long f = 0; f < frames; ++f, in += chans) {
      storeSample(dst, f, type, static_cast<std::int16_t>(std::clamp(*in, -32768, 32767)));
    }
  }
  if (!rt->capture.empty() && rt->captureUsed + wantSamples <= rt->capture.size()) {
    std::int16_t* out = rt->capture.data() + rt->captureUsed;
    for (std::size_t i = 0; i < wantSamples; ++i) {
      out[i] = static_cast<std::int16_t>(std::clamp(mix[i], -32768, 32767));
    }
    rt->captureUsed += wantSamples;
  }
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
  //
  // DECKBOY_ASIO_RATE=<Hz> asks for that rate instead. For tests: a driver
  // that can run at the source rate never converts, so without it the
  // conversion path -- every word-clocked interface's path -- goes unrun.
  double wantRate = sourceRate;
  if (const char* forced = std::getenv("DECKBOY_ASIO_RATE"); forced && *forced) {
    if (const double hz = std::atof(forced); hz > 0.0) wantRate = hz;
  }
  if (wantRate > 0.0) {
    if (ASIOCanSampleRate(wantRate) == ASE_OK) {
      ASIOSetSampleRate(wantRate);
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

  // Resample only when the rates actually differ. The converters belong to
  // the sources (openSource); one is made here only to find out now, rather
  // than at the first take, that this machine cannot convert these rates.
  rt->sourceRate = sourceRate > 0.0 ? sourceRate : rt->sampleRate;
  if (ratesDiffer(*rt)) {
    SDL_AudioStream* probe = makeResampler(*rt);
    if (!probe) {
      error = "could not create a resampler for " +
              std::to_string(static_cast<int>(std::lround(rt->sourceRate))) + " -> " +
              std::to_string(static_cast<int>(std::lround(rt->sampleRate))) + " Hz";
      ASIODisposeBuffers();
      rt->drivers.removeCurrentDriver();
      return false;
    }
    SDL_DestroyAudioStream(probe);
  }
  // A second of sound per source, or eight driver buffers if that is more.
  // The ring has to be deeper than the lead the engine keeps: its decoders
  // wait only once ~120 ms is queued, and a live feed allows half a second.
  // At eight buffers of a 512-frame driver (85 ms) the queue never reached
  // the mark, so the decoder never waited -- the whole file went into the
  // engine's overflow in a moment, and the cue went silent half a second in.
  // Depth is not latency: the engine's pacing decides how full it runs, and
  // a stop clears it (clear()).
  rt->ringSamples = std::max(static_cast<std::size_t>(preferred) * 8,
                             static_cast<std::size_t>(std::lround(std::max(1.0, rt->sampleRate)))) *
                    static_cast<std::size_t>(channels);
  rt->mix.assign(static_cast<std::size_t>(preferred) * channels, 0);
  rt->generation = gNextGeneration++;
  if (const char* capture = std::getenv("DECKBOY_ASIO_CAPTURE"); capture && *capture) {
    rt->capturePath = capture;
    // Sixty seconds of the device's own rate and channels.
    rt->capture.assign(static_cast<std::size_t>(std::max(1.0, rt->sampleRate) * 60.0) *
                         static_cast<std::size_t>(channels), 0);
  }
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
  rt->drivers.removeCurrentDriver();
  if (!rt->capturePath.empty()) {
    // The test capture (see AsioRuntime::capture), as a plain 16-bit WAV.
    if (std::FILE* f = std::fopen(rt->capturePath.c_str(), "wb")) {
      const std::uint32_t dataBytes = static_cast<std::uint32_t>(rt->captureUsed * 2);
      const std::uint32_t rate = static_cast<std::uint32_t>(std::lround(rt->sampleRate));
      const std::uint16_t chans = static_cast<std::uint16_t>(rt->channels);
      auto u32 = [f](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
      auto u16 = [f](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
      std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVEfmt ", 1, 8, f);
      u32(16); u16(1); u16(chans); u32(rate); u32(rate * chans * 2u);
      u16(static_cast<std::uint16_t>(chans * 2u)); u16(16);
      std::fwrite("data", 1, 4, f); u32(dataBytes);
      std::fwrite(rt->capture.data(), 2, rt->captureUsed, f);
      std::fclose(f);
    }
  }
  // Withdrawn sources go with it. Sources still in the mix belong to the
  // handles that hold them; their deleters find this stream gone and free
  // them directly, since no callback can be reading them now.
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

std::size_t AsioOutput::write(Source& source, const std::int16_t* interleaved,
                              std::size_t frames) {
#if !defined(DECKBOY_HAS_ASIO)
  (void)source; (void)interleaved; (void)frames;
  return 0;
#else
  AsioRuntime* rt = gRuntime;
  if (!rt || !interleaved || frames == 0 || source.generation != rt->generation) return 0;
  const std::size_t chans = static_cast<std::size_t>(rt->channels);
  Source& s = source;

  // A TAPE SPEED plays this source faster or slower with its pitch: the
  // converter does it (its frequency ratio), so a source at the driver's own
  // rate, which had none, is given one -- 48k to 48k -- the first time.
  const double ratio = s.ratio.load(std::memory_order_relaxed);
  if (ratio != s.appliedRatio) {
    if (!s.resampler) s.resampler = makeResampler(*rt);
    if (s.resampler) SDL_SetAudioStreamFrequencyRatio(s.resampler, static_cast<float>(ratio));
    s.appliedRatio = ratio;
  }

  // Converting path: hand the block to this source's converter, take back
  // whatever it has ready at the device rate, and ring-buffer that. The caller
  // is told it consumed the WHOLE block, because it did -- the conversion
  // holds the remainder, not the caller.
  if (s.resampler) {
    if (!SDL_PutAudioStreamData(s.resampler, interleaved,
                                static_cast<int>(frames * chans * sizeof(std::int16_t)))) {
      return 0;
    }
    const int availBytes = SDL_GetAudioStreamAvailable(s.resampler);
    if (availBytes > 0) {
      s.convertScratch.resize(static_cast<std::size_t>(availBytes) / sizeof(std::int16_t));
      const int got = SDL_GetAudioStreamData(s.resampler, s.convertScratch.data(), availBytes);
      if (got > 0) {
        const std::size_t gotFrames =
          static_cast<std::size_t>(got) / sizeof(std::int16_t) / chans;
        // Any frames the ring cannot take are DROPPED rather than re-queued,
        // because the converter has already advanced past them -- which is why
        // the ring is sized generously and the caller paces on queuedFrames().
        const std::size_t head = s.head.load(std::memory_order_relaxed);
        const std::size_t tail = s.tail.load(std::memory_order_acquire);
        const std::size_t ringSize = s.ring.size();
        const std::size_t freeSamples = ringSize - (head - tail) - 1;
        const std::size_t takeSamples = std::min(gotFrames, freeSamples / chans) * chans;
        const std::size_t start = head % ringSize;
        const std::size_t firstRun = std::min(takeSamples, ringSize - start);
        std::memcpy(s.ring.data() + start, s.convertScratch.data(),
                    firstRun * sizeof(std::int16_t));
        if (takeSamples > firstRun) {
          std::memcpy(s.ring.data(), s.convertScratch.data() + firstRun,
                      (takeSamples - firstRun) * sizeof(std::int16_t));
        }
        s.head.store(head + takeSamples, std::memory_order_release);
      }
    }
    return frames;
  }

  const std::size_t ringSize = s.ring.size();
  const std::size_t head = s.head.load(std::memory_order_relaxed);
  const std::size_t tail = s.tail.load(std::memory_order_acquire);
  // One sample of slack keeps head == tail meaning EMPTY rather than full.
  const std::size_t freeSamples = ringSize - (head - tail) - 1;
  const std::size_t writeFrames = std::min(frames, freeSamples / chans);
  // Two contiguous copies rather than a modulo per sample: the ring wraps at
  // most once per write, so find the split and memcpy each side.
  const std::size_t writeSamples = writeFrames * chans;
  const std::size_t start = head % ringSize;
  const std::size_t firstRun = std::min(writeSamples, ringSize - start);
  std::memcpy(s.ring.data() + start, interleaved, firstRun * sizeof(std::int16_t));
  if (writeSamples > firstRun) {
    std::memcpy(s.ring.data(), interleaved + firstRun,
                (writeSamples - firstRun) * sizeof(std::int16_t));
  }
  s.head.store(head + writeSamples, std::memory_order_release);
  return writeFrames;
#endif
}

std::size_t AsioOutput::queuedFrames(const Source& source) const {
#if defined(DECKBOY_HAS_ASIO)
  AsioRuntime* rt = gRuntime;
  if (!rt || rt->channels <= 0 || source.generation != rt->generation) return 0;
  const std::size_t head = source.head.load(std::memory_order_acquire);
  // A clear the callback has not reached yet is already gone, as far as the
  // caller is concerned.
  const std::size_t tail = std::max(source.tail.load(std::memory_order_acquire),
                                    source.flushTo.load(std::memory_order_acquire));
  std::size_t deviceFrames = (head - tail) / static_cast<std::size_t>(rt->channels);
  if (source.resampler) {
    // Audio still inside the converter counts as queued too, or the caller
    // sees a shallower buffer than really exists and over-feeds.
    const int pending = SDL_GetAudioStreamAvailable(source.resampler);
    if (pending > 0) {
      deviceFrames += static_cast<std::size_t>(pending) / sizeof(std::int16_t) /
                      static_cast<std::size_t>(rt->channels);
    }
    // Scale to SOURCE frames so the caller's pacing maths stays honest: by the
    // rates, and by the tape speed, at which one device frame is `ratio`
    // source frames.
    double scale = source.ratio.load(std::memory_order_relaxed);
    if (rt->sampleRate > 0.0 && rt->sourceRate > 0.0) {
      scale *= rt->sourceRate / rt->sampleRate;
    }
    deviceFrames = static_cast<std::size_t>(static_cast<double>(deviceFrames) * scale);
  }
  return deviceFrames;
#else
  (void)source;
  return 0;
#endif
}

void AsioOutput::clear(Source& source) {
#if defined(DECKBOY_HAS_ASIO)
  AsioRuntime* rt = gRuntime;
  if (!rt || source.generation != rt->generation) return;
  // Everything written so far goes. Only the writer moves head, and this is
  // the writer's thread, so it cannot move under us.
  source.flushTo.store(source.head.load(std::memory_order_relaxed), std::memory_order_relaxed);
  source.flushes.fetch_add(1, std::memory_order_release);
  // And what the converter still holds, or the next cue starts with the last
  // few milliseconds of this one.
  if (source.resampler && SDL_WasInit(SDL_INIT_AUDIO)) SDL_ClearAudioStream(source.resampler);
#else
  (void)source;
#endif
}

void AsioOutput::setRatio(Source& source, double ratio) {
#if defined(DECKBOY_HAS_ASIO)
  source.ratio.store(std::clamp(ratio, 0.25, 4.0), std::memory_order_relaxed);
#else
  (void)source; (void)ratio;
#endif
}

void AsioOutput::setPaused(Source& source, bool paused) {
#if defined(DECKBOY_HAS_ASIO)
  source.paused.store(paused, std::memory_order_release);
#else
  (void)source; (void)paused;
#endif
}

std::shared_ptr<AsioOutput::Source> AsioOutput::openSource() {
#if !defined(DECKBOY_HAS_ASIO)
  return {};
#else
  std::lock_guard<std::mutex> lock(gRuntimeMutex);
  AsioRuntime* rt = gRuntime;
  if (!rt) return {};
  freeRetired(*rt);
  int slot = -1;
  for (int i = 0; i < kMaxAsioSources; ++i) {
    if (!rt->sources[static_cast<std::size_t>(i)].load(std::memory_order_relaxed)) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return {};
  auto source = std::make_unique<Source>();
  source->ring.assign(rt->ringSamples, 0);
  source->slot = slot;
  source->generation = rt->generation;
  if (ratesDiffer(*rt)) {
    source->resampler = makeResampler(*rt);
    if (!source->resampler) return {};
  }
  Source* raw = source.release();
  rt->sources[static_cast<std::size_t>(slot)].store(raw, std::memory_order_release);
  // The last handle withdraws it from the mix; the memory waits until no
  // callback can still be reading it.
  return std::shared_ptr<Source>(raw, [](Source* done) {
    std::lock_guard<std::mutex> doneLock(gRuntimeMutex);
    AsioRuntime* live = gRuntime;
    if (live && live->generation == done->generation && done->slot >= 0) {
      live->sources[static_cast<std::size_t>(done->slot)].store(nullptr, std::memory_order_release);
      std::atomic_thread_fence(std::memory_order_seq_cst);
      // Any callback that could have loaded it is counted in by now; free it
      // once callbacks up to this count have finished.
      const std::uint64_t mayHold = live->callbacksEntered.load(std::memory_order_seq_cst);
      freeRetired(*live);
      live->retired.emplace_back(std::unique_ptr<Source>(done), mayHold);
      freeRetired(*live);   // nothing in flight: free it now
    } else {
      delete done;   // its stream is closed: nothing can be reading it
    }
  });
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
  return gRuntime && ratesDiffer(*gRuntime);
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
