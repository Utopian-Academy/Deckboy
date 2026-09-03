// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "spout_input.hpp"

#include <mutex>
#include <thread>

#if defined(DECKBOY_HAS_SPOUT)
#include <windows.h>
#include <GL/gl.h>
#include <SpoutLibrary/SpoutLibrary.h>
#endif

namespace deckboy::platform::video {

#if defined(DECKBOY_HAS_SPOUT)
namespace {

constexpr GLenum kGlBgraExt = 0x80E1;

// A window that exists only to own a device context.
//
// wglCreateContext needs an HDC and an HDC needs a window, so there has to be
// one; it is never shown, never moved and never pumped. WS_EX_TOOLWINDOW keeps
// it out of the taskbar and the alt-tab list even in the moment before it is
// hidden, and Deckboy's own window enumeration already skips tool windows, so
// a Spout receiver cannot appear in the window-capture picker.
//
// Made on the RECEIVE THREAD and owned by it. Handing a GL context between
// threads means one of them must give it up before the other can use it, and
// getting that wrong shows up as a receiver that works until something else
// touches the GPU.
class GlScratchContext {
 public:
  bool create(std::string& error) {
    static const wchar_t* kClassName = L"DeckboySpoutReceiveGL";
    static std::once_flag registered;
    std::call_once(registered, [] {
      WNDCLASSEXW wc {};
      wc.cbSize = sizeof(wc);
      wc.lpfnWndProc = DefWindowProcW;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.lpszClassName = kClassName;
      RegisterClassExW(&wc);
    });
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"", WS_POPUP,
                              0, 0, 1, 1, nullptr, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (!window_) {
      error = "could not create the offscreen window Spout receive needs";
      return false;
    }
    dc_ = GetDC(window_);
    if (!dc_) {
      error = "could not get a device context for Spout receive";
      destroy();
      return false;
    }
    PIXELFORMATDESCRIPTOR pfd {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ChoosePixelFormat(dc_, &pfd);
    if (!format || !SetPixelFormat(dc_, format, &pfd)) {
      error = "no OpenGL pixel format available for Spout receive";
      destroy();
      return false;
    }
    gl_ = wglCreateContext(dc_);
    if (!gl_ || !wglMakeCurrent(dc_, gl_)) {
      error = "could not create an OpenGL context for Spout receive";
      destroy();
      return false;
    }
    return true;
  }

  void destroy() {
    if (gl_) {
      wglMakeCurrent(nullptr, nullptr);
      wglDeleteContext(gl_);
      gl_ = nullptr;
    }
    if (dc_ && window_) {
      ReleaseDC(window_, dc_);
    }
    dc_ = nullptr;
    if (window_) {
      DestroyWindow(window_);
      window_ = nullptr;
    }
  }

  ~GlScratchContext() { destroy(); }

 private:
  HWND window_ = nullptr;
  HDC dc_ = nullptr;
  HGLRC gl_ = nullptr;
};

}  // namespace
#endif  // DECKBOY_HAS_SPOUT

struct SpoutInput::Impl {
  SpoutInput::FrameCallback onFrame;
  std::thread worker;
  std::atomic<bool> stopping{false};
  mutable std::mutex errorMutex;
  std::string error;

  void setError(std::string message) {
    std::lock_guard<std::mutex> lock(errorMutex);
    error = std::move(message);
  }
};

SpoutInput::SpoutInput() : impl_(std::make_unique<Impl>()) {}

SpoutInput::~SpoutInput() { stop(); }

void SpoutInput::onFrame(FrameCallback cb) { impl_->onFrame = std::move(cb); }

std::string SpoutInput::lastError() const {
  std::lock_guard<std::mutex> lock(impl_->errorMutex);
  return impl_->error;
}

bool SpoutInput::available(std::string* whyNot) {
#if defined(DECKBOY_HAS_SPOUT)
  return true;
#else
  if (whyNot) {
    *whyNot = "this build has no Spout support";
  }
  return false;
#endif
}

std::vector<std::string> SpoutInput::discoverSenders() {
  std::vector<std::string> names;
#if defined(DECKBOY_HAS_SPOUT)
  SPOUTLIBRARY* spout = GetSpout();
  if (!spout) {
    return names;
  }
  const int count = spout->GetSenderCount();
  for (int i = 0; i < count; ++i) {
    char name[256] = {};
    if (spout->GetSender(i, name, sizeof(name))) {
      names.emplace_back(name);
    }
  }
  spout->Release();
#endif
  return names;
}

bool SpoutInput::start(const std::string& senderName) {
  stop();
#if defined(DECKBOY_HAS_SPOUT)
  impl_->setError({});
  impl_->stopping.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);

  impl_->worker = std::thread([this, senderName]() {
    GlScratchContext gl;
    std::string glError;
    if (!gl.create(glError)) {
      impl_->setError(glError);
      running_.store(false, std::memory_order_release);
      return;
    }
    SPOUTLIBRARY* spout = GetSpout();
    if (!spout) {
      impl_->setError("the Spout library could not be started");
      running_.store(false, std::memory_order_release);
      return;
    }
    if (!senderName.empty()) {
      spout->SetReceiverName(senderName.c_str());
    }

    // Sized from the sender once it is known. Big enough for anything Deckboy
    // would send at first, then trimmed: ReceiveImage writes whatever the
    // sender's size is, so a buffer smaller than the sender is a memory
    // overrun and one much bigger is only wasted pages.
    std::vector<std::uint8_t> buffer;
    unsigned int width = 0, height = 0;
    bool everConnected = false;

    while (!impl_->stopping.load(std::memory_order_acquire)) {
      // SPOUT'S OWN RECEIVER SHAPE, which is not optional.
      //
      // The documented loop is: receive, and if IsUpdated() then the sender's
      // size has just become known (or changed) -- resize to exactly
      // width*height*4 and let the NEXT call fill it. Handing it one large
      // buffer up front and reading the size afterwards looks equivalent and
      // is not: the receiver reports the sender's dimensions and returns true
      // while never connecting, so the picture stays black with every other
      // sign saying it should work.
      if (buffer.size() < 64) {
        buffer.assign(64, 0);   // somewhere to point until the size is known
      }
      if (spout->IsUpdated()) {
        const unsigned int uw = spout->GetSenderWidth();
        const unsigned int uh = spout->GetSenderHeight();
        if (uw > 0 && uh > 0) {
          width = uw;
          height = uh;
          buffer.assign(static_cast<std::size_t>(uw) * uh * 4, 0);
        }
        continue;
      }
      const bool okRecv = spout->ReceiveImage(buffer.data(), kGlBgraExt, false, 0);
      if (okRecv) {
        // Only once the buffer is the sender's size -- before that the receive
        // is the connecting call, not a frame.
        const std::size_t need = static_cast<std::size_t>(width) * height * 4;
        if (width > 0 && height > 0 && buffer.size() == need) {
          if (!everConnected) {
            everConnected = true;
            impl_->setError({});
          }
          if (impl_->onFrame) {
            impl_->onFrame(buffer.data(), static_cast<int>(width),
                           static_cast<int>(height), static_cast<int>(width) * 4);
          }
        }
      } else if (!everConnected) {
        impl_->setError(senderName.empty()
                          ? std::string("waiting for any Spout sender")
                          : ("waiting for Spout sender \"" + senderName + "\""));
      }
      // Spout has no blocking receive, so this is a poll. 120Hz is comfortably
      // above any sender's rate without spinning a core on an idle one.
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    spout->ReleaseReceiver();
    spout->Release();
    gl.destroy();
    running_.store(false, std::memory_order_release);
  });
  return true;
#else
  (void) senderName;
  impl_->setError("this build has no Spout support");
  return false;
#endif
}

void SpoutInput::stop() {
  impl_->stopping.store(true, std::memory_order_release);
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  running_.store(false, std::memory_order_release);
}

}  // namespace deckboy::platform::video
