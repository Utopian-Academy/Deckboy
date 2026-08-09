// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
//
// syphon_metal.mm — Objective-C++ implementation of the Syphon publisher.
// See syphon_metal.h and docs/SYPHON_PLAN.md.
//
// Built only when DECKBOY_HAS_SIPHON is defined (ENABLE_SIPHON on macOS), which
// also guarantees Syphon.framework is present. Everything Objective-C stays in
// this translation unit; the rest of Deckboy sees only the plain-C++ facade.
// Our facade is deckboy::...::SyphonPublisher; Syphon's own class is
// SyphonMetalServer, so the two never collide.

#include "syphon_metal.h"

#if defined(DECKBOY_HAS_SIPHON) && defined(__APPLE__)

#import <Metal/Metal.h>
#import <Syphon/Syphon.h>

namespace deckboy {
namespace platform {
namespace video {

namespace {
struct Backing {
  id<MTLDevice> device = nil;
  SyphonMetalServer* server = nil;   // Syphon's class
  id<MTLTexture> texture = nil;
  int width = 0;
  int height = 0;
};
}  // namespace

SyphonPublisher::SyphonPublisher() = default;

SyphonPublisher::~SyphonPublisher() {
  close();
}

bool SyphonPublisher::open(const std::string& serverName) {
  close();
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
      return false;
    }
    NSString* name = [NSString stringWithUTF8String:serverName.c_str()];
    SyphonMetalServer* server =
        [[SyphonMetalServer alloc] initWithName:name device:device options:nil];
    if (server == nil) {
      return false;
    }
    Backing* backing = new Backing();
    backing->device = device;
    backing->server = server;
    impl_ = backing;
  }
  return impl_ != nullptr;
}

bool SyphonPublisher::publishBGRA(const std::uint8_t* bgra, int width, int height) {
  if (impl_ == nullptr || bgra == nullptr || width <= 0 || height <= 0) {
    return false;
  }
  Backing* backing = static_cast<Backing*>(impl_);
  @autoreleasepool {
    // (Re)allocate the backing texture only when the raster changes.
    if (backing->texture == nil || backing->width != width || backing->height != height) {
      MTLTextureDescriptor* desc =
          [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                             width:(NSUInteger)width
                                                            height:(NSUInteger)height
                                                         mipmapped:NO];
      desc.usage = MTLTextureUsageShaderRead;
      id<MTLTexture> tex = [backing->device newTextureWithDescriptor:desc];
      if (tex == nil) {
        return false;
      }
      backing->texture = tex;
      backing->width = width;
      backing->height = height;
    }

    // The buffer is tightly packed BGRA8 — the pixel format matches directly,
    // no swizzle.
    [backing->texture replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height)
                        mipmapLevel:0
                          withBytes:bgra
                        bytesPerRow:(NSUInteger)(width * 4)];

    // flipped:NO — the CPU buffer is already top-left origin like the on-screen
    // output; toggle this first if a Syphon client shows the image upside down.
    [backing->server publishFrameTexture:backing->texture
                             imageRegion:NSMakeRect(0, 0, width, height)
                                 flipped:NO];
  }
  return true;
}

void SyphonPublisher::close() {
  if (impl_ == nullptr) {
    return;
  }
  Backing* backing = static_cast<Backing*>(impl_);
  @autoreleasepool {
    [backing->server stop];
    backing->server = nil;
    backing->texture = nil;
    backing->device = nil;
  }
  delete backing;
  impl_ = nullptr;
}

}  // namespace video
}  // namespace platform
}  // namespace deckboy

#else  // no Syphon: out-of-line stubs so the facade still links everywhere.

namespace deckboy {
namespace platform {
namespace video {

SyphonPublisher::SyphonPublisher() = default;
SyphonPublisher::~SyphonPublisher() { close(); }
bool SyphonPublisher::open(const std::string&) { return false; }
bool SyphonPublisher::publishBGRA(const std::uint8_t*, int, int) { return false; }
void SyphonPublisher::close() { impl_ = nullptr; }

}  // namespace video
}  // namespace platform
}  // namespace deckboy

#endif
