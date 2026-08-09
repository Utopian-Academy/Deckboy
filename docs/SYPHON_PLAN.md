# Syphon on macOS — implementation plan

Status: **planned**. The catalog reports Syphon as unsupported on macOS; this is
the honest state until the code below exists and has been verified on a Mac.
Nothing ships claiming Syphon works until it does.

## Why this is a plan and not a patch

Syphon is a native macOS framework for **GPU texture sharing** between apps
(the mac counterpart to Spout on Windows — send Deckboy's programme to Resolume,
VDMX, MadMapper, etc., or receive theirs as a cue). Unlike camera/screen capture,
there is **no ffmpeg path**: Syphon shares an `IOSurface` via Metal or OpenGL.
That means Objective-C++ against `Syphon.framework`, which is:

- not installed on the GitHub CI runners, so it cannot even **compile-check**
  until CI builds the framework, and
- not runtime-testable without a Mac running a Syphon client to see the output.

Writing it blind — unable to compile or run — would be guesswork dressed as a
feature. So this documents the concrete approach and the foundation to build,
and the code lands when it can be verified.

## The lucky break: the CPU buffer already exists

`SiphonSpoutSender` (native/platform/siphon_spout.cpp) does not share a GPU
texture today — it `SDL_LockTexture`s the finished output, copies it into a CPU
**BGRA** buffer, and hands that to Spout's `SendImage()`. Syphon output can reuse
that identical buffer: upload it to a `MTLTexture` once per frame and publish via
`SyphonMetalServer`. No SDL-Metal-internals surgery, no zero-copy plumbing —
just a standalone `MTLDevice` and a per-frame texture upload. This makes the
OUTPUT direction genuinely tractable.

## Scope, in priority order

1. **Syphon OUTPUT (publish)** — the valuable half for a cue deck, and the
   tractable one. Publishes the programme output as a Syphon server.
2. **Syphon INPUT (receive as a cue)** — a `SyphonMetalClient` whose frames feed
   the decode/render pipeline like any other source. More plumbing (a new
   frame source into MediaEngine), lower priority.

## OUTPUT design

```
finished output (SDL_Texture)
  -> SDL_LockTexture -> CPU BGRA buffer         [ALREADY DONE for Spout]
  -> upload buffer into a reused MTLTexture      [new, Objective-C++]
  -> SyphonMetalServer publishFrameTexture:...   [new, Objective-C++]
```

- New file `native/platform/syphon_metal.mm` (Objective-C++), exposing a tiny
  C++ interface (`class SyphonServer { bool open(name); void publish(bgra, w, h);
  void close(); }`) so `siphon_spout.cpp` stays plain C++ and only the `.mm`
  touches Metal/Syphon.
- One `MTLCreateSystemDefaultDevice()`, one `id<MTLTexture>` reallocated only
  when the raster changes, `replaceRegion:` to upload the BGRA buffer each frame,
  then `[server publishFrameTexture:... imageRegion:... flipped:...]`.
- Wire into the same place the Spout sender is driven from
  (`app_output_mgmt.ipp` / `app_render_output.ipp`), behind the existing
  `OutputTarget` Spout/Syphon enable flag — one "texture share" toggle, Spout on
  Windows, Syphon on macOS.
- `SiphonSpoutSender::isSupported()` returns true on macOS once this compiles
  with the framework present.

## Build / CI foundation (the real next step)

`ENABLE_SIPHON` + the `find_library(Siphon)` already exist in CMakeLists.txt. To
make the code compile-checkable — the same bar every other macOS feature meets —
CI must provide the framework:

- Add a CI step (macos jobs) that builds **Syphon-Framework** from source
  (github.com/Syphon/Syphon-Framework, a small Xcode project) and installs it
  where `find_library` sees it, then configures with `-DENABLE_SIPHON=ON`.
- Alternatively vendor a prebuilt `Syphon.framework`, but building from source in
  CI avoids committing a binary blob and keeps provenance clean.

Once CI builds with `ENABLE_SIPHON=ON`, `syphon_metal.mm` gets a real
compile-check on every push — at which point writing it is engineering, not
guesswork.

## Traps (write them down before they cost a day)

- **Colour order.** The CPU buffer is BGRA (SDL ARGB8888 in memory);
  `MTLPixelFormatBGRA8Unorm` matches it directly — do not swizzle.
- **Flip.** Syphon's origin convention vs. the texture's may differ; the
  `flipped:` argument on `publishFrameTexture:` exists for exactly this and is
  the first thing to toggle if the client shows the image upside down.
- **Texture lifetime.** Reallocate the `MTLTexture` only on a raster change;
  allocating per frame will thrash.
- **Server naming.** A Syphon server is identified by (app name, server name).
  Keep the server name stable across a show so clients keep their binding, the
  same lesson as the NMOS UUID seeds.
- **Teardown.** Release the server and device on output disable; a leaked
  `SyphonServer` keeps advertising a dead source.

## Verifying it (when a Mac is available)

Run Deckboy with Syphon output enabled, open a Syphon client (the free **Simple
Client** from the Syphon site, or Resolume) and confirm the programme appears,
right-side-up, at the right size, updating live. That is the acceptance test;
until it passes, the catalog stays honest.
