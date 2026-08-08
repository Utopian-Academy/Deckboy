# Browser Cues on CEF (off-screen rendering)

Status: **planned / in progress**. The Xvfb and WebView2 backends remain the
shipping path until CEF is proven; nothing reports CEF support unless it is
genuinely built and working.

## Why

Three backends, three behaviours, one of them absent:

| Platform | Today | How it works |
|---|---|---|
| Windows | WebView2 (`DECKBOY_HAS_WEBVIEW`) | Embedded, offscreen via PrintWindow |
| Windows (no WebView2) | Edge/Chrome `--app=` + `gdigrab` | Spawns a real browser, screen-grabs it |
| Linux | Chromium + Xvfb + `x11grab` | Spawns Xvfb, spawns Chromium, screen-grabs it |
| macOS | — | Scaffold. Browser cues do not run. |

The Linux path is not a rendering pipeline, it is a *screenshot* pipeline. It
needs a virtual X server, it depends on a browser being installed and on its
command-line flags not changing, its startup is a timing-dependent phase machine
(`WaitXvfb` → `WaitChrome` → `WaitCapture`), it cannot deliver input, it cannot
capture page audio, and every frame makes a round trip through an X11 grab and
an ffmpeg pipe. It works, but it is not something to trust on a show.

CEF (Chromium Embedded Framework) with off-screen rendering is what OBS uses for
browser sources. It gives real `OnPaint` callbacks with a BGRA buffer, no
external browser, no virtual display, no screen capture — and it is the only
option that also revives macOS.

## Target

One backend behind the existing `BrowserRenderer` seam
(`native/platform/browser.hpp`), used on all three platforms. WebView2 and Xvfb
stay in-tree as fallbacks until CEF has been proven on each platform.

## The traps

Written down because each of these is a day lost if you meet it cold.

**1. `CefExecuteProcess` must be the first thing `main()` does.**
CEF spawns its renderer/GPU/utility subprocesses by re-executing *the same
binary* with `--type=renderer` and friends. If the app does not short-circuit at
the very top of `main()`, every subprocess tries to boot a full Deckboy — SDL
window, audio device, output windows. Symptom is a storm of windows and a hang.

**2. macOS needs separate helper `.app` bundles.**
Not optional and not a detail: macOS requires each subprocess to be its own
bundle with its own identifier — `Deckboy Helper.app`, `Deckboy Helper
(GPU).app`, `Deckboy Helper (Renderer).app`, `Deckboy Helper (Plugin).app` — all
inside `Contents/Frameworks`. `tools/package_macos.sh` must build and sign each
of them, and the ad-hoc re-signing order gets deeper. This is the single biggest
packaging change.

**3. Missing resource files render a silent white page.**
CEF needs `icudtl.dat`, the `.pak` files and `locales/` at runtime. If they are
absent the browser loads and paints *nothing*, with no error. Point
`CefSettings.resources_dir_path` and `locales_dir_path` at the bundled copies
explicitly rather than relying on them sitting beside the executable.

**4. The message loop is ours, not CEF's.**
Do not call `CefRunMessageLoop` — it takes the thread. Use
`external_message_pump` and drive `CefDoMessageLoopWork()` from the app tick.
Too rarely and the page stutters; in a tight loop and it burns a core.

**5. Shutdown is asynchronous.**
`CefShutdown()` may only be called once every browser has actually closed, and
closing completes on `OnBeforeClose`, not when you ask. Tearing down naively
either deadlocks or crashes on exit — and Deckboy already learned this lesson
once with the NMOS patch handler joining a thread that was waiting on it.

**6. All `CefBrowserHost` calls belong on the CEF UI thread**, which is whichever
thread called `CefInitialize`. Deckboy's `tick()` runs on the main thread, so
initialise there and keep it there.

**7. Version pinning.** CEF binary distributions are welded to an exact Chromium
build. Pin the version; do not float.

## What it buys beyond parity

- **Page audio.** `CefAudioHandler::OnAudioStreamPacket` delivers the page's
  audio, which the screen-grab path never could. Browser cues become genuinely
  audio-visual.
- **Input.** Clicks and keystrokes can be sent to a cue, which the Xvfb path
  cannot do.
- **No installed browser required.** Today a Linux box without Chromium simply
  fails; the runtime would travel with the app.

## Cost, stated plainly

- ~200 MB redistributable per platform. The macOS bundle grows from ~45 MB to
  roughly a quarter of a gigabyte, and Windows and Linux similarly.
- A build-time SDK download of ~1 GB, and CI cache pressure to match.
- Licensing: CEF is BSD and Chromium is BSD-plus-others; Deckboy is GPL-3.0.
  Dynamically linking is fine, but the bundled Chromium's own notices must ship
  alongside the existing `LICENSE-ffmpeg.txt`.

## Phasing

Each phase ends somewhere shippable, and no phase advertises CEF before it works.

1. **Seam + build plumbing.** `ENABLE_CEF` CMake option, `DECKBOY_HAS_CEF`
   define, CEF located or fetched. No behaviour change; existing backends still
   used. *(in progress)*
2. **`CefExecuteProcess` short-circuit** at the top of `main()`, guarded, plus
   `CefInitialize`/`CefShutdown` lifecycle with correct async close.
3. **Off-screen render path.** `CefRenderHandler::OnPaint` into a locked frame
   buffer that `BrowserRenderer::grabFrame` returns. This is the point at which
   a browser cue renders without a screen grab.
4. **Prove on Linux**, where the current path is worst. Retire Xvfb there.
5. **macOS**, including the helper bundles in `package_macos.sh`. This is where
   browser cues start existing at all.
6. **Windows**, retiring WebView2 once CEF matches it.
7. **Audio and input**, which the old architecture could not do.

## Rule

Until a platform reaches its phase, it keeps its current backend and reports
honestly. A CEF backend that is compiled but not working must report
unavailable — the same rule that just removed the macOS Syphon claim.
