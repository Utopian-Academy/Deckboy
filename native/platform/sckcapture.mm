// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
//
// sckcapture.mm — a tiny standalone macOS helper that captures a display with
// ScreenCaptureKit and writes raw RGBA frames to stdout, one after another.
//
// WHY A SEPARATE BINARY
// ---------------------
// Deckboy's capture backends produce a command line, and MediaEngine spawns it
// and reads width*height*4 RGBA bytes per frame from its stdout — exactly how it
// consumes ffmpeg. avfoundation screen capture is gone on current macOS, so the
// macOS window/screen backend runs THIS helper instead of ffmpeg. Everything
// downstream (frame reader, compositor, display) is unchanged. It is a separate
// process for the same reason ffmpeg is: capture failure never takes the app
// down, and the helper can hold the Screen Recording permission cleanly.
//
// Usage:  deckboy-sckcapture --width <w> --height <h> --fps <n>
//                            [--display <index> | --window <CGWindowID>]
//         --display captures a whole screen (default, index 0); --window
//         captures one window by its CGWindowID (from the app's window picker).
// Output: continuous tightly-packed RGBA8 frames on stdout (w*h*4 bytes each).
//
// PERMISSION: screen capture requires the Screen Recording TCC grant, attributed
// to the responsible app (Deckboy). The first run triggers the system prompt; if
// it is denied, ScreenCaptureKit reports no shareable content and the helper
// exits non-zero, which Deckboy surfaces through the usual capture-failure path.

#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {
int g_width = 1280;
int g_height = 720;
int g_fps = 30;
int g_displayIndex = 0;
long g_windowId = 0;   // >0 -> capture this CGWindowID instead of a whole display
std::atomic<bool> g_running{true};

int parseIntArg(const char* v, int fallback) {
  if (!v) return fallback;
  char* end = nullptr;
  long parsed = std::strtol(v, &end, 10);
  if (end == v) return fallback;
  return static_cast<int>(parsed);
}
}  // namespace

// Receives frames and writes them, swizzled to RGBA, to stdout.
API_AVAILABLE(macos(12.3))
@interface DeckboySCKOutput : NSObject <SCStreamOutput, SCStreamDelegate>
@end

@implementation DeckboySCKOutput {
  std::string _rowbuf;  // reused per-row RGBA scratch
}

- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type API_AVAILABLE(macos(12.3)) {
  if (type != SCStreamOutputTypeScreen) return;
  if (!CMSampleBufferIsValid(sampleBuffer)) return;

  CVImageBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (!pixelBuffer) return;

  CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
  const size_t w = CVPixelBufferGetWidth(pixelBuffer);
  const size_t h = CVPixelBufferGetHeight(pixelBuffer);
  const size_t stride = CVPixelBufferGetBytesPerRow(pixelBuffer);
  const uint8_t* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pixelBuffer));

  if (base && w > 0 && h > 0) {
    // ScreenCaptureKit gives BGRA; Deckboy reads RGBA. Swizzle B<->R per row and
    // write exactly w*4 bytes per row (dropping any stride padding), so the
    // frame the app reads is tightly packed at the size it expects.
    if (_rowbuf.size() < w * 4) _rowbuf.resize(w * 4);
    uint8_t* row = reinterpret_cast<uint8_t*>(&_rowbuf[0]);
    for (size_t y = 0; y < h; ++y) {
      const uint8_t* src = base + y * stride;
      for (size_t x = 0; x < w; ++x) {
        const uint8_t* p = src + x * 4;
        row[x * 4 + 0] = p[2];  // R <- B
        row[x * 4 + 1] = p[1];  // G
        row[x * 4 + 2] = p[0];  // B <- R
        row[x * 4 + 3] = p[3];  // A
      }
      if (fwrite(row, 1, w * 4, stdout) != w * 4) {
        g_running.store(false);  // stdout closed (Deckboy stopped the cue)
        break;
      }
    }
    fflush(stdout);
  }

  CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error API_AVAILABLE(macos(12.3)) {
  g_running.store(false);
}
@end

int main(int argc, const char* argv[]) {
  for (int i = 1; i + 1 < argc; i += 2) {
    if (std::strcmp(argv[i], "--display") == 0) g_displayIndex = parseIntArg(argv[i + 1], 0);
    else if (std::strcmp(argv[i], "--width") == 0) g_width = parseIntArg(argv[i + 1], 1280);
    else if (std::strcmp(argv[i], "--height") == 0) g_height = parseIntArg(argv[i + 1], 720);
    else if (std::strcmp(argv[i], "--fps") == 0) g_fps = parseIntArg(argv[i + 1], 30);
    else if (std::strcmp(argv[i], "--window") == 0) g_windowId = std::strtol(argv[i + 1], nullptr, 10);
  }

  if (@available(macOS 12.3, *)) {
    @autoreleasepool {
      __block SCDisplay* chosen = nil;
      __block SCWindow* chosenWindow = nil;
      __block NSError* contentError = nil;
      dispatch_semaphore_t sem = dispatch_semaphore_create(0);

      // Enumerate shareable content. Fails (or returns nothing) without the
      // Screen Recording permission.
      [SCShareableContent getShareableContentWithCompletionHandler:^(
          SCShareableContent* content, NSError* error) {
        contentError = error;
        if (content) {
          if (g_windowId > 0) {
            // Find the specific window the picker chose, by its CGWindowID.
            for (SCWindow* w in content.windows) {
              if ((long)w.windowID == g_windowId) { chosenWindow = w; break; }
            }
          }
          if (!chosenWindow && content.displays.count > 0) {
            NSInteger idx = g_displayIndex;
            if (idx < 0 || idx >= (NSInteger)content.displays.count) idx = 0;
            chosen = content.displays[idx];
          }
        }
        dispatch_semaphore_signal(sem);
      }];
      dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10LL * NSEC_PER_SEC));

      if (g_windowId > 0 && !chosenWindow) {
        // The requested window is gone (closed/minimised) or permission denied.
        fprintf(stderr, "sckcapture: requested window %ld not capturable "
                        "(closed, or Screen Recording permission denied)\n", g_windowId);
        return 2;
      }
      if (!chosenWindow && !chosen) {
        fprintf(stderr, "sckcapture: no capturable display "
                        "(Screen Recording permission may be denied)\n");
        return 2;
      }

      SCContentFilter* filter =
          chosenWindow
              ? [[SCContentFilter alloc] initWithDesktopIndependentWindow:chosenWindow]
              : [[SCContentFilter alloc] initWithDisplay:chosen excludingWindows:@[]];

      SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
      config.width = g_width;
      config.height = g_height;
      config.minimumFrameInterval = CMTimeMake(1, g_fps > 0 ? g_fps : 30);
      config.pixelFormat = kCVPixelFormatType_32BGRA;
      config.showsCursor = YES;
      config.queueDepth = 5;

      DeckboySCKOutput* output = [[DeckboySCKOutput alloc] init];
      SCStream* stream = [[SCStream alloc] initWithFilter:filter
                                            configuration:config
                                                 delegate:output];

      dispatch_queue_t q = dispatch_queue_create("deckboy.sckcapture", DISPATCH_QUEUE_SERIAL);
      NSError* addErr = nil;
      if (![stream addStreamOutput:output
                              type:SCStreamOutputTypeScreen
                sampleHandlerQueue:q
                             error:&addErr]) {
        fprintf(stderr, "sckcapture: addStreamOutput failed: %s\n",
                addErr ? addErr.localizedDescription.UTF8String : "unknown");
        return 3;
      }

      __block NSError* startErr = nil;
      dispatch_semaphore_t startSem = dispatch_semaphore_create(0);
      [stream startCaptureWithCompletionHandler:^(NSError* error) {
        startErr = error;
        dispatch_semaphore_signal(startSem);
      }];
      dispatch_semaphore_wait(startSem, dispatch_time(DISPATCH_TIME_NOW, 10LL * NSEC_PER_SEC));
      if (startErr) {
        fprintf(stderr, "sckcapture: startCapture failed: %s\n",
                startErr.localizedDescription.UTF8String);
        return 4;
      }

      // Frames arrive on the sample queue and are written to stdout there. Idle
      // here until stdout closes (Deckboy stopped the cue) or the stream stops.
      while (g_running.load()) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
      }
      [stream stopCaptureWithCompletionHandler:^(NSError*){}];
    }
    return 0;
  }

  fprintf(stderr, "sckcapture: ScreenCaptureKit requires macOS 12.3 or later\n");
  return 5;
}
