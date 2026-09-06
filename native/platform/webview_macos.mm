// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// webview_macos.mm — deckboy-webview, the macOS browser cue backend.
//
// WHY A SEPARATE BINARY, AND WHY THIS SHAPE
// -----------------------------------------
// macOS had no browser backend at all: BrowserRenderer::start() fell through to
// "native browser backend not implemented". This is it, and it is built the way
// the rest of the macOS capture path is already built.
//
// The design follows Mitti, which is the reference for this on macOS: it renders
// web pages with "a transparent, offscreen WebKit based browser", captures them
// with ScreenCaptureKit, and -- the part that matters most -- when the operator
// needs to deal with the page it SHOWS THEM THE REAL WINDOW, so they can click a
// cookie banner, dismiss a consent dialog, or log in. That last one is the
// argument for the whole approach: no amount of synthetic clicking can type a
// password, satisfy a 2FA prompt, or drive a password manager.
//
// So this helper:
//   1. opens a real NSWindow with a WKWebView, parked off the visible screen,
//   2. prints its CGWindowID, which Deckboy hands to deckboy-sckcapture as a
//      "window:<id>" source -- so the frames arrive through the SAME capture
//      path as any other window cue, and nothing downstream changes,
//   3. takes one-line commands on stdin, including `interact 1`, which brings
//      the window to the middle of the screen for the operator to use.
//
// A separate process for the same reasons as sckcapture and ffmpeg: a browser
// that wedges cannot take the show down with it, and WebKit keeps its own
// memory and its own crashes to itself.
//
// Usage:  deckboy-webview --url <url> --width <w> --height <h>
// Stdout: "WINDOWID <n>" once, then nothing. Line buffered.
// Stdin:  one command per line --
//           js <script>        evaluate JavaScript in the page
//           interact <0|1>     hide/show the real window for hands-on use
//           url <address>      navigate
//           reload | back | forward
//           quit
// ============================================================================

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

// ON SCREEN, BUT UNDERNEATH EVERYTHING.
//
// The first version parked this far off to the left (-100000) on the theory
// that an ordered-in window is rendered wherever it sits. It is not: macOS
// marks a fully off-screen window NotVisible -- the log says
// "running-active-NotVisible" -- the window server stops rendering it, and
// ScreenCaptureKit then has no window to share, so the capture helper exits
// and the cue never gets a frame.
//
// This is the same trap the Windows backend documents at its own window
// creation ("off-screen at negative coords -> no DComp surface"), and the same
// answer: keep it at the origin, real and rendered, and push it to the BACK of
// the z-order so it is never in front of the operator. ScreenCaptureKit
// captures a named window's content even when another window covers it, which
// is exactly why it is the right API for this.
constexpr double kParkedX = 0.0;
constexpr double kParkedY = 0.0;

std::string argValue(int argc, const char* argv[], const char* name,
                     const std::string& fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], name) == 0) {
      return argv[i + 1];
    }
  }
  return fallback;
}

int argInt(int argc, const char* argv[], const char* name, int fallback) {
  const std::string value = argValue(argc, argv, name, "");
  if (value.empty()) {
    return fallback;
  }
  try {
    return std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

std::string trimmed(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

}  // namespace

// ---------------------------------------------------------------------------

@interface DeckboyWebHost : NSObject <WKNavigationDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) WKWebView* webView;
@property(nonatomic, assign) BOOL interactive;
@property(nonatomic, assign) NSInteger pageWidth;
@property(nonatomic, assign) NSInteger pageHeight;
- (instancetype)initWithURL:(NSString*)url width:(NSInteger)w height:(NSInteger)h;
- (void)setInteractive:(BOOL)on;
- (NSPoint)parkedOrigin;
- (void)runCommand:(const std::string&)line;
@end

@implementation DeckboyWebHost

- (instancetype)initWithURL:(NSString*)url width:(NSInteger)w height:(NSInteger)h {
  self = [super init];
  if (!self) {
    return nil;
  }
  _pageWidth = w > 0 ? w : 1920;
  _pageHeight = h > 0 ? h : 1080;
  _interactive = NO;

  WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
  // Autoplay without a gesture: a cue is played by the operator taking it, and
  // there is nobody to click the page's own play button.
  config.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;

  const NSRect frame = NSMakeRect(0, 0, _pageWidth, _pageHeight);
  _webView = [[WKWebView alloc] initWithFrame:frame configuration:config];
  _webView.navigationDelegate = self;
  // Transparent, like Mitti's: a lower third is meant to key over the
  // programme, and an opaque white page behind it defeats that entirely.
  [_webView setValue:@NO forKey:@"drawsBackground"];

  // Borderless while parked. The title bar only appears when the operator asks
  // to interact, because a bar in the captured frame would be in the cue.
  _window = [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:NSWindowStyleMaskBorderless
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  _window.contentView = _webView;
  _window.opaque = NO;
  _window.backgroundColor = [NSColor clearColor];
  _window.releasedWhenClosed = NO;
  // Never in the way, never in a screenshot of something else, and not
  // something the operator can tab into by accident while it is parked.
  // Below every ordinary window, so it cannot end up in front of the operator
  // even momentarily, while still being a window the server renders.
  _window.level = NSNormalWindowLevel - 1;
  _window.excludedFromWindowsMenu = YES;
  // Follow the operator between spaces rather than being left behind on one:
  // a window on an inactive space stops being rendered, which is the same
  // failure as being off-screen.
  _window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                             | NSWindowCollectionBehaviorStationary
                             | NSWindowCollectionBehaviorIgnoresCycle;
  [_window setFrameOrigin:[self parkedOrigin]];
  // orderBack rather than orderFront: it has to be ordered IN so the window
  // server renders it (ScreenCaptureKit gets nothing from a window that was
  // never shown), but it must not come forward.
  [_window orderBack:nil];

  if (url.length > 0) {
    [self navigateTo:url];
  }
  return self;
}

// WHERE "PARKED" ACTUALLY IS.
//
// (0,0) in Cocoa's global space is the bottom-left of the PRIMARY screen, which
// is not necessarily a screen that is on: with the lid shut, or the built-in
// display asleep while an external one drives the desk, a window placed there
// is on a display nothing is rendering -- and an unrendered window is one
// ScreenCaptureKit will not share, which is the same dead end as putting it
// off-screen entirely.
//
// So it is parked on whatever screen is actually there, at that screen's own
// origin, and pushed to the back of the z-order rather than out of sight.
- (NSPoint)parkedOrigin {
  NSScreen* screen = [NSScreen mainScreen];
  if (!screen && [NSScreen screens].count > 0) {
    screen = [NSScreen screens][0];
  }
  if (!screen) {
    return NSMakePoint(kParkedX, kParkedY);
  }
  const NSRect frame = screen.frame;
  return NSMakePoint(frame.origin.x + kParkedX, frame.origin.y + kParkedY);
}

- (void)navigateTo:(NSString*)address {
  NSURL* target = [NSURL URLWithString:address];
  if (!target) {
    return;
  }
  if (target.isFileURL) {
    // A file:// page needs explicit read access to its own directory, or
    // WebKit loads a blank frame and says nothing about why.
    NSURL* directory = [target URLByDeletingLastPathComponent];
    [_webView loadFileURL:target allowingReadAccessToURL:directory];
  } else {
    [_webView loadRequest:[NSURLRequest requestWithURL:target]];
  }
}

- (void)setInteractive:(BOOL)on {
  if (on == _interactive) {
    return;
  }
  _interactive = on;
  if (on) {
    // A real, titled, movable window in the middle of the screen. The capture
    // keeps running throughout -- the cue stays on air while the operator
    // works, which is the whole point of doing it this way.
    _window.styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                      | NSWindowStyleMaskResizable;
    _window.title = @"Deckboy browser cue - click, type, log in";
    NSScreen* screen = [NSScreen mainScreen];
    if (screen) {
      const NSRect visible = screen.visibleFrame;
      // Fitted to the screen rather than shown at the cue's raster: a 3840x2160
      // cue would otherwise open a window bigger than the display it is on.
      const CGFloat width = MIN((CGFloat)_pageWidth, visible.size.width * 0.8);
      const CGFloat height = MIN((CGFloat)_pageHeight, visible.size.height * 0.8);
      const NSRect target = NSMakeRect(
        visible.origin.x + (visible.size.width - width) / 2.0,
        visible.origin.y + (visible.size.height - height) / 2.0,
        width, height);
      [_window setFrame:target display:YES];
    }
    [NSApp activateIgnoringOtherApps:YES];
    [_window makeKeyAndOrderFront:nil];
  } else {
    _window.styleMask = NSWindowStyleMaskBorderless;
    // Back to the cue's own raster, or the next captured frame is the wrong
    // shape for the rest of the pipeline.
    const NSPoint parked = [self parkedOrigin];
    [_window setFrame:NSMakeRect(parked.x, parked.y, _pageWidth, _pageHeight)
              display:YES];
    [_window orderBack:nil];
  }
}

- (void)runCommand:(const std::string&)line {
  const std::string text = trimmed(line);
  if (text.empty()) {
    return;
  }
  const auto space = text.find(' ');
  const std::string verb = text.substr(0, space);
  const std::string rest = space == std::string::npos
                         ? std::string() : trimmed(text.substr(space + 1));

  if (verb == "quit") {
    [NSApp terminate:nil];
    return;
  }
  if (verb == "js" && !rest.empty()) {
    NSString* script = [NSString stringWithUTF8String:rest.c_str()];
    if (script) {
      [_webView evaluateJavaScript:script completionHandler:nil];
    }
    return;
  }
  if (verb == "interact") {
    [self setInteractive:(rest == "1" || rest == "on") ? YES : NO];
    return;
  }
  if (verb == "url" && !rest.empty()) {
    NSString* address = [NSString stringWithUTF8String:rest.c_str()];
    if (address) {
      [self navigateTo:address];
    }
    return;
  }
  if (verb == "reload") {
    [_webView reload];
    return;
  }
  if (verb == "back") {
    [_webView goBack];
    return;
  }
  if (verb == "forward") {
    [_webView goForward];
    return;
  }
}

@end

// ---------------------------------------------------------------------------

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    const std::string url = argValue(argc, argv, "--url", "");
    const int width = argInt(argc, argv, "--width", 1920);
    const int height = argInt(argc, argv, "--height", 1080);

    if (url.empty()) {
      std::fprintf(stderr, "deckboy-webview: --url is required\n");
      return 2;
    }

    [NSApplication sharedApplication];
    // Accessory, not Regular: no dock icon and no menu bar for a helper that
    // is usually invisible. It can still be activated when the operator asks
    // to interact.
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    NSString* address = [NSString stringWithUTF8String:url.c_str()];
    DeckboyWebHost* host = [[DeckboyWebHost alloc] initWithURL:address
                                                        width:width
                                                       height:height];
    if (!host || !host.window) {
      std::fprintf(stderr, "deckboy-webview: window creation failed\n");
      return 3;
    }

    // The parent needs this to start capturing, so it goes out immediately and
    // unbuffered -- a helper that has drawn its first frame but not said which
    // window it is looks exactly like one that failed to start.
    std::printf("WINDOWID %ld\n", (long)[host.window windowNumber]);
    std::fflush(stdout);

    // Commands arrive on stdin. Read on a background thread and hand each line
    // to the main thread, because everything AppKit touches has to happen
    // there and a blocking read on the main thread would freeze the page.
    std::thread reader([host]() {
      std::string line;
      int ch = 0;
      while ((ch = std::fgetc(stdin)) != EOF) {
        if (ch == '\n') {
          std::string command = line;
          line.clear();
          dispatch_async(dispatch_get_main_queue(), ^{
            [host runCommand:command];
          });
        } else {
          line.push_back(static_cast<char>(ch));
        }
      }
      // Parent closed the pipe: the cue is gone, so the helper goes with it.
      dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp terminate:nil];
      });
    });
    reader.detach();

    [NSApp run];
  }
  return 0;
}
