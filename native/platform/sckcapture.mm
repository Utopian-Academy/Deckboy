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
#import <AppKit/AppKit.h>          // NSApplicationLoad -- see main()
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
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
// Same idea as the browser helper's log: a capture that streams perfectly
// shaped black frames is indistinguishable from a working one at every other
// level of the pipeline, so it has to say what it is actually sending.
static std::string sckTempPath(const char* name) {
  const char* tmp = getenv("TMPDIR");
  std::string path(tmp && *tmp ? tmp : "/tmp/");
  if (path.back() != '/') path.push_back('/');
  return path + name;
}

static void sckLog(const char* fmt, ...) {
  static std::string s_path;
  if (s_path.empty()) {
    const char* tmp = getenv("TMPDIR");
    s_path = std::string(tmp && *tmp ? tmp : "/tmp/");
    if (s_path.back() != '/') s_path.push_back('/');
    s_path += "deckboy-sckcapture.log";
  }
  FILE* f = fopen(s_path.c_str(), "a");
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

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
    // Every 60th frame, say whether what is going down the pipe has any light
    // in it. Nine spread samples, taken from the source buffer before the
    // swizzle so this measures what ScreenCaptureKit handed over.
    static long s_frame = 0;
    if ((s_frame++ % 60) == 0) {
      int lit = 0;
      unsigned first[4] = {0, 0, 0, 0};
      for (int sy = 1; sy <= 3; ++sy) {
        for (int sx = 1; sx <= 3; ++sx) {
          const uint8_t* p = base + ((h * sy) / 4) * stride + ((w * sx) / 4) * 4;
          if (sy == 1 && sx == 1) {
            first[0] = p[2]; first[1] = p[1]; first[2] = p[0]; first[3] = p[3];
          }
          if (p[0] + p[1] + p[2] > 12) ++lit;
        }
      }
      sckLog("frame %ld: %zux%zu stride %zu, %d/9 lit, first rgba %u %u %u %u",
             s_frame - 1, w, h, stride, lit, first[0], first[1], first[2], first[3]);
    }
    // ScreenCaptureKit gives BGRA; Deckboy reads RGBA. Swizzle B<->R per row and
    // write exactly w*4 bytes per row (dropping any stride padding), so the
    // frame the app reads is tightly packed at the size it expects.
      // ONE FRAME TO DISK, ON REQUEST.
      //
      // Deckboy holds the Screen Recording grant; an SSH session does not, and
      // a grant belongs to the code identity that earned it. So the only way to
      // see what this machine's screen really looks like -- to check a layout,
      // or confirm a capture is pointed at the thing it claims -- is to ask the
      // helper that is already allowed to look.
      //
      // Triggered by a file rather than an environment variable: the helper
      // inherits its environment from an app the operator launched from Finder,
      // so an env var would mean relaunching the app to ask a question about
      // it. Drop the request file, take the cue, collect the frame.
      static bool s_dumped = false;
      if (!s_dumped && s_frame > 8) {
        const std::string request = sckTempPath("deckboy-sck-dump-request");
        if (FILE* probe = fopen(request.c_str(), "rb")) {
          fclose(probe);
          s_dumped = true;
          remove(request.c_str());
          const std::string target = sckTempPath("deckboy-sck-frame.ppm");
          if (FILE* out = fopen(target.c_str(), "wb")) {
            fprintf(out, "P6\n%zu %zu\n255\n", w, h);
            for (size_t yy = 0; yy < h; ++yy) {
              const uint8_t* srcRow = base + yy * stride;
              for (size_t xx = 0; xx < w; ++xx) {
                const uint8_t* p = srcRow + xx * 4;
                const uint8_t rgb[3] = {p[2], p[1], p[0]};
                fwrite(rgb, 1, 3, out);
              }
            }
            fclose(out);
            sckLog("dumped one frame to %s", target.c_str());
          } else {
            sckLog("could not write %s", target.c_str());
          }
        }
      }

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
  // CONNECT TO THE WINDOW SERVER BEFORE TOUCHING SCREENCAPTUREKIT.
  //
  // This is a plain command-line tool, so nothing has initialised the Cocoa
  // side of the process -- and SCStream reaches CoreGraphics, which aborts a
  // process that has no window-server connection:
  //
  //   Assertion failed: (did_initialize), function CGS_REQUIRE_INIT,
  //   file CGInitialization.c, line 44          (exit code 134)
  //
  // It hid for a long time because it only fires AFTER the enumeration
  // succeeds: without Screen Recording the helper exits earlier, with a
  // permission message, and never reaches the crash. So the symptom for a user
  // who HAS granted permission was a capture that died instantly, a black
  // output, and -- because the helper's stderr was being discarded -- no
  // explanation anywhere.
  //
  // NSApplicationLoad() is the documented one-liner for exactly this: it
  // initialises Cocoa for a command-line program without turning it into a
  // full app (no dock icon, no menu bar, no run loop taken over).
  NSApplicationLoad();

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
      __block NSUInteger shareableWindowCount = 0;
      __block NSUInteger shareableDisplayCount = 0;
      [SCShareableContent getShareableContentWithCompletionHandler:^(
          SCShareableContent* content, NSError* error) {
        contentError = error;
        if (content) {
          // Kept so the failure paths below can tell a missing PERMISSION
          // (nothing shared at all) from a missing WINDOW (plenty shared, just
          // not that one).
          shareableWindowCount = content.windows.count;
          shareableDisplayCount = content.displays.count;
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

      // TELL THE TWO FAILURES APART.
      //
      // "not capturable (closed, or permission denied)" covered a missing
      // permission AND a window that simply is not there, and those need
      // completely different fixes -- one is a click in System Settings, the
      // other is a bug in whoever asked for that window. Chasing the wrong one
      // of those cost a long afternoon.
      //
      // The distinction is available: with NO permission the enumeration
      // returns nothing at all (or errors); WITH permission it returns a list
      // that simply does not contain the requested id. So count what came back.
      const NSUInteger sawWindows = shareableWindowCount;
      const NSUInteger sawDisplays = shareableDisplayCount;
      const bool permissionLooksDenied = (sawWindows == 0 && sawDisplays == 0);

      if (contentError && permissionLooksDenied) {
        fprintf(stderr, "sckcapture: cannot see any windows or displays: %s\n",
                contentError.localizedDescription.UTF8String);
      }
      if (g_windowId > 0 && !chosenWindow) {
        if (permissionLooksDenied) {
          fprintf(stderr, "sckcapture: SCREEN RECORDING PERMISSION DENIED -- the "
                          "system shared nothing at all. Grant it to Deckboy in "
                          "System Settings > Privacy & Security > Screen Recording.\n");
        } else {
          fprintf(stderr, "sckcapture: window %ld is not in the shareable list "
                          "(permission IS granted -- %lu windows visible -- so that "
                          "window is closed, minimised, on an inactive display, or "
                          "not shareable)\n",
                  g_windowId, (unsigned long)sawWindows);
        }
        return 2;
      }
      if (!chosenWindow && !chosen) {
        fprintf(stderr, "sckcapture: SCREEN RECORDING PERMISSION DENIED -- no "
                        "displays shared. Grant it in System Settings > Privacy & "
                        "Security > Screen Recording.\n");
        return 2;
      }

      if (chosenWindow) {
        const CGRect wr = chosenWindow.frame;
        sckLog("--- capturing window %u '%s' (%s) %.0fx%.0f at %.0f,%.0f "
               "onScreen=%d active=%d -> config %dx%d @%dfps",
               (unsigned)chosenWindow.windowID,
               chosenWindow.title.UTF8String ? chosenWindow.title.UTF8String : "",
               chosenWindow.owningApplication.applicationName.UTF8String
                 ? chosenWindow.owningApplication.applicationName.UTF8String : "?",
               wr.size.width, wr.size.height, wr.origin.x, wr.origin.y,
               (int)chosenWindow.isOnScreen, (int)chosenWindow.isActive,
               g_width, g_height, g_fps);
      } else {
        sckLog("--- capturing a display -> config %dx%d @%dfps",
               g_width, g_height, g_fps);
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
