// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "ndi_input.hpp"

#include "dynamic_library.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>

#if defined(DECKBOY_HAS_NDI_SDK)
#include <Processing.NDI.Lib.h>
#endif

namespace deckboy::platform::video {
namespace {

#if defined(DECKBOY_HAS_NDI_SDK)

// The NDI runtime, loaded by hand.
//
// The SDK headers give the struct layouts; the runtime gives the code. Linking
// the import library instead would make the NDI runtime a hard requirement for
// starting Deckboy at all, on a machine that may never send or receive a frame
// of it.
struct NdiRuntime {
  deckboy::platform::DynamicLibrary lib;
  bool ok = false;
  std::string error;

  bool (*initialize)() = nullptr;
  void (*destroy)() = nullptr;
  NDIlib_find_instance_t (*findCreate)(const NDIlib_find_create_t*) = nullptr;
  void (*findDestroy)(NDIlib_find_instance_t) = nullptr;
  bool (*findWait)(NDIlib_find_instance_t, std::uint32_t) = nullptr;
  const NDIlib_source_t* (*findCurrent)(NDIlib_find_instance_t, std::uint32_t*) = nullptr;
  NDIlib_recv_instance_t (*recvCreate)(const NDIlib_recv_create_v3_t*) = nullptr;
  void (*recvDestroy)(NDIlib_recv_instance_t) = nullptr;
  NDIlib_frame_type_e (*recvCapture)(NDIlib_recv_instance_t,
                                     NDIlib_video_frame_v2_t*,
                                     NDIlib_audio_frame_v3_t*,
                                     NDIlib_metadata_frame_t*,
                                     std::uint32_t) = nullptr;
  void (*recvFreeVideo)(NDIlib_recv_instance_t, const NDIlib_video_frame_v2_t*) = nullptr;
};

NdiRuntime& runtime() {
  static NdiRuntime rt = [] {
    NdiRuntime r;
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("DECKBOY_NDI_LIB"); env && *env) {
      candidates.emplace_back(env);
    }
#ifdef _WIN32
    candidates.emplace_back("Processing.NDI.Lib.x64.dll");
    if (const char* dir = std::getenv("NDI_SDK_DIR"); dir && *dir) {
      candidates.emplace_back(std::string(dir) + "\\Bin\\x64\\Processing.NDI.Lib.x64.dll");
    }
    candidates.emplace_back("C:\\Program Files\\NDI\\NDI 6 Runtime\\v6\\Processing.NDI.Lib.x64.dll");
    candidates.emplace_back("C:\\Program Files\\NDI\\NDI 5 Runtime\\Processing.NDI.Lib.x64.dll");
#elif defined(__APPLE__)
    candidates.emplace_back("libndi.dylib");
    candidates.emplace_back("/usr/local/lib/libndi.dylib");
#else
    candidates.emplace_back("libndi.so.6");
    candidates.emplace_back("libndi.so");
    candidates.emplace_back("/usr/local/lib/libndi.so.6");
    candidates.emplace_back("/usr/lib/libndi.so.6");
#endif
    r.lib = deckboy::platform::DynamicLibrary(std::move(candidates));
    if (!r.lib.load()) {
      r.error = "the NDI runtime is not installed";
      return r;
    }
    r.initialize = r.lib.loadSymbol<decltype(r.initialize)>("NDIlib_initialize");
    r.destroy = r.lib.loadSymbol<decltype(r.destroy)>("NDIlib_destroy");
    r.findCreate = r.lib.loadSymbol<decltype(r.findCreate)>("NDIlib_find_create_v2");
    r.findDestroy = r.lib.loadSymbol<decltype(r.findDestroy)>("NDIlib_find_destroy");
    r.findWait = r.lib.loadSymbol<decltype(r.findWait)>("NDIlib_find_wait_for_sources");
    r.findCurrent = r.lib.loadSymbol<decltype(r.findCurrent)>("NDIlib_find_get_current_sources");
    r.recvCreate = r.lib.loadSymbol<decltype(r.recvCreate)>("NDIlib_recv_create_v3");
    r.recvDestroy = r.lib.loadSymbol<decltype(r.recvDestroy)>("NDIlib_recv_destroy");
    r.recvCapture = r.lib.loadSymbol<decltype(r.recvCapture)>("NDIlib_recv_capture_v3");
    r.recvFreeVideo = r.lib.loadSymbol<decltype(r.recvFreeVideo)>("NDIlib_recv_free_video_v2");
    if (!r.initialize || !r.findCreate || !r.findDestroy || !r.findWait ||
        !r.findCurrent || !r.recvCreate || !r.recvDestroy || !r.recvCapture ||
        !r.recvFreeVideo) {
      r.error = "the installed NDI runtime is missing entry points this needs";
      return r;
    }
    if (!r.initialize()) {
      // The runtime refuses on a CPU without the instruction set it wants.
      r.error = "the NDI runtime declined to start on this machine";
      return r;
    }
    r.ok = true;
    return r;
  }();
  return rt;
}

#endif  // DECKBOY_HAS_NDI_SDK

}  // namespace

struct NdiInput::Impl {
  NdiInput::FrameCallback onFrame;
  std::thread worker;
  std::atomic<bool> stopping{false};
  mutable std::mutex errorMutex;
  std::string error;

  void setError(std::string message) {
    std::lock_guard<std::mutex> lock(errorMutex);
    error = std::move(message);
  }
};

NdiInput::NdiInput() : impl_(std::make_unique<Impl>()) {}

NdiInput::~NdiInput() { stop(); }

void NdiInput::onFrame(FrameCallback cb) { impl_->onFrame = std::move(cb); }

std::string NdiInput::lastError() const {
  std::lock_guard<std::mutex> lock(impl_->errorMutex);
  return impl_->error;
}

bool NdiInput::available(std::string* whyNot) {
#if defined(DECKBOY_HAS_NDI_SDK)
  NdiRuntime& rt = runtime();
  if (!rt.ok && whyNot) {
    *whyNot = rt.error;
  }
  return rt.ok;
#else
  if (whyNot) {
    *whyNot = "this build has no NDI support";
  }
  return false;
#endif
}

std::vector<std::string> NdiInput::discoverSources(int waitMs) {
  std::vector<std::string> names;
#if defined(DECKBOY_HAS_NDI_SDK)
  NdiRuntime& rt = runtime();
  if (!rt.ok) {
    return names;
  }
  // show_local_sources EXPLICITLY. The default hides sources on this machine,
  // which is most of them when the sender under test is right here -- and a
  // hidden source looks exactly like one that is not advertising.
  NDIlib_find_create_t settings;
  settings.show_local_sources = true;
  settings.p_groups = nullptr;
  settings.p_extra_ips = nullptr;
  NDIlib_find_instance_t finder = rt.findCreate(&settings);
  if (!finder) {
    return names;
  }
  // DISCOVERY IS PROGRESSIVE. Stopping at the first source that answers gives
  // a list that looks complete and is not -- one sender replies in
  // milliseconds while the one being looked for has not yet been asked.
  const int slice = 250;
  for (int waited = 0; waited < std::max(slice, waitMs); waited += slice) {
    rt.findWait(finder, static_cast<std::uint32_t>(slice));
  }
  std::uint32_t count = 0;
  const NDIlib_source_t* sources = rt.findCurrent(finder, &count);
  for (std::uint32_t i = 0; i < count; ++i) {
    if (sources[i].p_ndi_name) {
      names.emplace_back(sources[i].p_ndi_name);
    }
  }
  rt.findDestroy(finder);
#else
  (void) waitMs;
#endif
  return names;
}

bool NdiInput::start(const std::string& sourceName) {
  stop();
#if defined(DECKBOY_HAS_NDI_SDK)
  NdiRuntime& rt = runtime();
  if (!rt.ok) {
    impl_->setError(rt.error);
    return false;
  }
  if (sourceName.empty()) {
    impl_->setError("no NDI source name on this cue");
    return false;
  }
  impl_->setError({});
  impl_->stopping.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);

  impl_->worker = std::thread([this, sourceName]() {
    NdiRuntime& rt2 = runtime();
    NDIlib_find_create_t findSettings;
    findSettings.show_local_sources = true;
    findSettings.p_groups = nullptr;
    findSettings.p_extra_ips = nullptr;
    NDIlib_find_instance_t finder = rt2.findCreate(&findSettings);
    if (!finder) {
      impl_->setError("could not start NDI discovery");
      running_.store(false, std::memory_order_release);
      return;
    }

    // KEEP LOOKING while the cue is live. A source that is not up yet is the
    // normal case on a show day -- the other machine is still booting -- and
    // giving up after one sweep would mean the cue never recovers without
    // being re-taken.
    NDIlib_recv_instance_t receiver = nullptr;
    while (!impl_->stopping.load(std::memory_order_acquire) && !receiver) {
      rt2.findWait(finder, 500);
      std::uint32_t count = 0;
      const NDIlib_source_t* sources = rt2.findCurrent(finder, &count);
      for (std::uint32_t i = 0; i < count; ++i) {
        if (!sources[i].p_ndi_name) {
          continue;
        }
        // Substring, so a show can say "Test Pattern" without knowing which
        // machine will be sending it on the day.
        if (std::string(sources[i].p_ndi_name).find(sourceName) == std::string::npos) {
          continue;
        }
        NDIlib_recv_create_v3_t recvSettings;
        recvSettings.source_to_connect_to = sources[i];
        // BGRA, which is what the rest of Deckboy's egress speaks, so the
        // frame needs no conversion on the way in.
        recvSettings.color_format = NDIlib_recv_color_format_BGRX_BGRA;
        recvSettings.bandwidth = NDIlib_recv_bandwidth_highest;
        recvSettings.allow_video_fields = false;
        recvSettings.p_ndi_recv_name = "Deckboy";
        receiver = rt2.recvCreate(&recvSettings);
        break;
      }
      if (!receiver) {
        impl_->setError("waiting for NDI source \"" + sourceName + "\"");
      }
    }
    rt2.findDestroy(finder);
    if (!receiver) {
      running_.store(false, std::memory_order_release);
      return;
    }
    impl_->setError({});

    while (!impl_->stopping.load(std::memory_order_acquire)) {
      NDIlib_video_frame_v2_t frame;
      std::memset(&frame, 0, sizeof(frame));
      // A timeout rather than a block, so stopping does not wait on a sender
      // that has gone away mid-show.
      const NDIlib_frame_type_e type =
        rt2.recvCapture(receiver, &frame, nullptr, nullptr, 200);
      if (type != NDIlib_frame_type_video) {
        continue;
      }
      if (frame.p_data && frame.xres > 0 && frame.yres > 0 && impl_->onFrame) {
        impl_->onFrame(reinterpret_cast<const std::uint8_t*>(frame.p_data),
                       frame.xres, frame.yres,
                       frame.line_stride_in_bytes > 0
                         ? frame.line_stride_in_bytes
                         : frame.xres * 4);
      }
      rt2.recvFreeVideo(receiver, &frame);
    }
    rt2.recvDestroy(receiver);
    running_.store(false, std::memory_order_release);
  });
  return true;
#else
  impl_->setError("this build has no NDI support");
  return false;
#endif
}

void NdiInput::stop() {
  impl_->stopping.store(true, std::memory_order_release);
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  running_.store(false, std::memory_order_release);
}

}  // namespace deckboy::platform::video
