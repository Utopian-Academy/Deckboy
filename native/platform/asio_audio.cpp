// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "platform/asio_audio.hpp"

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

  // Release immediately. Holding a driver open blocks every other application
  // on the machine from using the interface, which during a show would be the
  // worst kind of side effect from merely LOOKING at a settings page.
  drivers.removeCurrentDriver();
  out.probed = true;
  return true;
#endif
}

}  // namespace audio
}  // namespace platform
}  // namespace deckboy
