# DEVNOTES

## Text Mode Is A Look, Not A Cue Kind (v0.88.0)

The character grid began inside rebuildVideoSynthFrame, which made "draw this
as characters" a property of one cue kind. Nothing in the renderer ever
required that: cells sample their source with `cx * srcW / cols`, so any
source size and any destination size have always worked. It is now a member
taking those as parameters, and the synth calls it with what it used to reach
for directly.

The effect stack is handed the renderer as a `std::function` on
CueEffectContext rather than calling the engine itself. That keeps the stack a
pure function of its inputs, which is what lets every effect be dumped
headlessly, benched, and run by the output and the preview independently
without either knowing about the other. Sprite-sheet state stays on the engine,
which is why the callback exists at all -- and headless, where nobody supplies
one, the effect passes the picture through rather than pretending.

The grid's settings live on the cue (VideoSynthSettings) whatever the cue's
kind, so any cue carrying the effect gets the full set of controls. The four
effect parameters are folded ON TOP of a copy of those settings: what is worth
reaching for mid-set sits on a fader and can take an LFO, and everything else
stays in the inspector where it can be read.

That inspector section sits OUTSIDE the per-cue-kind chain in
renderMainPanel. That chain has one branch per CueKind and each draws its own
sections, so anything belonging to every kind has to be drawn after it.

## Statements In The Code Source (v0.88.0)

A source is now a sequence of statements: everything before the last semicolon
names a value, and what follows is the one or three channel expressions. A
source with no semicolon is exactly the previous grammar, so every expression
written before this compiles unchanged and means the same thing.

The compiled form gained a `prelude` program and a list of names. The prelude
runs once per pixel and writes into a caller-owned scratch vector; the three
channel programs then read those slots. That is also the performance argument
for the feature: a distance field wanted by three channels used to be written
into all three expressions and evaluated three times per pixel.

Names are resolved AFTER the built-ins, so no assignment can shadow x, t or
sin -- a source that redefined its own coordinate system would be a puzzle
rather than a feature. Each statement compiles against the names declared
above it, so a forward reference is reported rather than silently reading
zero.

The editor's field lays out through one shared cell function used by the
drawing, the caret and click-to-place, because a newline now ends a row and
three separate divisions would only agree while the answer stayed trivial.
The highlighter takes a first pass for `name =` declarations so a source's own
names colour as names, from the same tables the compiler reads.

## What Six New Effects Cost, And Where The Cost Was (v0.87.0)

Every one of them was over budget when it first rendered correctly, and in each
case the cost was somewhere other than where it looked.

**Grain flow: 119ms -> 13ms at 1080p.** Three separate lessons in one effect.
The structure tensor was written as a lambda converting RGB to luma called from
inside a 3x3 window loop, so every pixel converted thirty-six neighbours that
its neighbours had already converted -- three cheap full-frame passes first and
the per-pixel work becomes nine reads. Then `cos`/`sin` of a frame constant and
a `std::pow` sat inside the pixel loop: four million trig calls and two million
pows a frame, for values that never changed or only ever ranged 0-1. And after
all that it was still 20ms with the stroke length turned to zero, because the
remaining cost is a scattered gather per pixel -- so it now runs on a
third-resolution raster and scales back up. The result is visually
indistinguishable, which it would be: the effect's whole job is to destroy
detail along one axis.

**Chladni: 24ms -> 7ms.** The plate equation is
`sin(m.pi.u).sin(n.pi.v) - sin(n.pi.u).sin(m.pi.v)`, evaluated five times per
pixel to get the value and its slope in both directions -- twenty sines per
pixel, which was the entire effect. It is separable: u depends only on x and v
only on y, so two tables of frame width plus three rows of constants replace all
of it and the inner loop does no trigonometry at all.

**Crystallise: 18ms -> 16ms.** Nine `sqrt` per pixel to rank nine seeds by
arrival time. Ranking by the SQUARE of the time gives the identical ordering,
and only the grain-boundary test needs a real distance -- two roots per pixel
instead of nine.

**Wavefront: two mistakes that looked like each other.** First it rendered as
speckle, which reads as an unstable solver; it was not, it was a displacement
scale of half the frame width per unit of grid slope, and grid slopes are order
0.1. Then, once that was fixed, it showed visible square blocks -- the coarse
field being read one cell at a time. Bilinear sampling of the field costs four
reads instead of one and the blocks vanish; a wave is smooth by nature, so
interpolating is reconstructing it rather than smoothing over it. It also seeds
from a BLOCK AVERAGE of the picture rather than one pixel per cell: point
sampling seeds the plate with the image's highest frequencies, and a wave
equation handed noise correctly gives noise back.

**And `near` is a macro in the Windows headers.** So are `min`, `max`, `far` and
`small`. A local variable called `near` silently stops being C++, and the error
lands two lines further down.

## A Stateless Stack With Two Frames Of Memory (v0.87.0)

The effect stack is deliberately stateless: it is a free function over a pixel
buffer, so it can be run twice, dumped headlessly by `--effect-dump`, benched,
and applied by the output and the preview independently. Video feedback is the
first effect that needs to remember the last frame, and the temptation is to
put a static buffer inside the effect. That would break every one of those
properties at once, and it would not know which deck it was working for.

So the memory lives with the caller. `CueEffectContext::feedback` points at a
buffer the app owns, one per deck, and the effect does nothing at all when it
is null -- which is what a caller with no buffer deserves, rather than a
pretend result.

**Two planes and a cursor byte, in that one buffer.** The obvious shape
allocates a frame, renders into it, copies it back over the picture and keeps
it for next time. At 1080p that is eight megabytes of allocation and eight of
memcpy every frame. Instead the buffer holds two whole frames plus a trailing
byte naming which one is newest: the effect reads the newest, and writes the
result to the other plane AND to the picture in the same pass. No allocation,
no copy back, and the read and the write can never alias.

**A hold, for the second consumer.** Two window outputs showing the same deck
both run the stack. Whoever asks first steps the loop; everyone after gets
`feedbackHold`, reads the same previous frame the step read, and writes
nothing -- so both screens show the identical picture instead of drifting one
frame of echo apart. This is the same fault the motion driver had (D93) and the
same shape of answer. The control preview keeps a SEPARATE buffer rather than
holding on the output's, because it runs at its own rate and sometimes not at
all.

**Cleared at every take.** The loop belongs to what was on the deck, not to the
deck. Left alone, the first frame of a new cue echoes the last frame of the old
one -- a ghost of the previous clip, on the output, at the take.

**Lighten, not add.** Written additively first, which is what feedback
physically is, and a colour bar went to clipped white in twenty frames -- a
third of a second, on stage. Adding has a fixed point at L/(1-echo), several
times the input; taking the brighter of the live pixel and the decayed echo has
its fixed point at L, so the picture can never come out brighter than it went
in. Measured over 120 passes the result is stable to within three levels and
the mean stops moving. That bound is the whole reason this is shippable and not
a party trick.

**32.32 fixed point, not 16.16.** The source coordinate is affine in x, so it
is a start and a step per row rather than two rotations and two `lround`s per
pixel; with the echo as three 256-entry tables the effect went from 5.5ms to
1.0ms at 1080p. At 16 fractional bits the step's own rounding error accumulates
to about four thousandths of a pixel across a 1080p row, which was enough to
move a few dozen pixels of a colour-bar edge and break byte-identity with the
plain double version. At 32 bits the drift is under a millionth of a pixel and
the two agree exactly, which is the standard this codebase holds optimisations
to (D95).

## The Readback That Never Ran (v0.86.0)

**One missing `Flush()` meant every recording was a still frame.** D3D11 defers
submission: the `CopyResource` into the staging ring sat in the immediate
context's command buffer until something forced it out. Nothing did -- the
recording output has no window, so it never presents -- so the staging texture
was still in use when the map came round and `D3D11_MAP_FLAG_DO_NOT_WAIT`
correctly refused. Measured over ten seconds: 240 calls, 0 successes, 237 map
failures. With the flush: 237 successes, 0 failures.

**Why it survived three tools and a week of measurement.** The readback
returning "nothing ready" is a legitimate, expected condition: the caller keeps
the previous picture and the CFR pacer repeats it, which is exactly right for a
momentary miss. When it happens on *every* frame, the file is one still image
and every counter still reads perfect -- frames delivered equals frames owed,
duration exact, alarm silent, file size plausible.

Two lessons worth more than the fix:

- **A fallback that is correct for a rare case will hide a permanent failure of
  the fast path.** The graceful degradation was the camouflage.
- **Any check that counts frames must also look at one.** `record_rate_check.py`
  now samples the finished file at 1fps, quantises, and counts distinct
  pictures. The quantise matters: comparing raw frames finds differences between
  identical pictures encoded at different GOP positions and reads lossy noise as
  motion.

## The Writer Had A Mailbox (v0.86.0)

`OutputStreamWriterState` held a single `pendingPacket`. A second frame pushed
before the writer drained the first *silently replaced it*, while the pacer
counted both as written. That is the whole explanation for two separate
mysteries: recordings that ran short whenever capture outpaced the writer, and
two earlier attempts at filling the cadence with repeats that achieved nothing
because every repeat landed in the same slot and one was written.

It is a `std::deque` bounded at eight frames now. The pacer catches up to four
frames a tick, which it can afford because packets carry a
`shared_ptr<const vector>` rather than a copy -- a repeat costs a pointer
instead of 33MB at 4K.

**Bound it in FRAMES, deliberately small.** A deep queue means a recording that
lags seconds behind the programme before anyone notices.

## Alarms That Cry Wolf Are Worse Than No Alarm (v0.86.0)

The dropped-frame alarm was rewritten twice and both mistakes are instructive.

**First it fired on every 4K take.** The asynchronous readback is a pipeline:
the frame handed to the writer is `kAsyncReadbackDepth` behind the one just
rendered, so a healthy recording sits a constant few frames behind what the
pacer says is owed. The tolerance was two frames. Every 4K take raised
"RECORDING DROPPING FRAMES - 3 behind" once a second for its whole length while
delivering 449 frames of 450.

**Then the fix over-corrected.** Counting FRESH frames against the recording
rate looks obviously right and is not: a 30fps source recorded at 60 is half
repeats *by definition*, so it fired on every standard above the source rate --
including 2160p25 and 1080p50, both perfectly healthy.

The right question turned out to be neither. It is whether the picture has gone
**stale** -- the capture stopped producing anything new -- because the pacer
will paper over a dead capture with repeats and hand back a duration-correct
file of one still image, reporting no fault at all.

## Effects Run Per-Effect, Not Per-Stack (v0.86.0)

The CRT stage cost 23ms at 4K until it moved to the small buffer, and the rule
that came out of it applies to the whole effect stack:

- A **single-pass per-pixel** operation (invert, posterise, threshold, grain,
  vignette, scanlines, dither) reads one pixel and writes one. Downscaling first
  would soften a colour grade for no reason. Full raster.
- Anything with a **window or an iteration** (blur, bloom, sort, seam carve,
  flow) computes its field small and applies it at full resolution.

**The trap that made the first effect invisible:** the engine picks NV12 -- and
on Windows, zero-copy GPU decode -- unless a cue needs RGBA. That predicate knew
about chroma key and colour controls but not about the new stack, so the pixels
never reached a CPU buffer for the effects to run on and the whole stack was
silently skipped.

## Motion Vectors Are Free (v0.86.0)

H.264 and MPEG-4 already contain a per-macroblock description of what moved
where; decoding normally throws it away. `flags2 +export_mvs` keeps it, and the
decoder computed it regardless.

Two constraints, both found rather than assumed:

- **Software decode only.** A d3d11va frame is a GPU surface and the hardware
  decoder does not surface what it used. Affordable here because the driver
  clip's *pixels are discarded* -- only its motion is wanted -- so it can be
  decoded small. Measured 909fps for 720p, 230fps for 4K.
- **An I-frame carries no vectors**, because it predicted nothing. Reported as
  an empty field, not a failure, and the caller holds the previous picture.

Where a macroblock splits into several vectors the cell takes the **largest**,
not the average: averaging a split block's opposing halves cancels them to
nothing, which would render the busiest part of the picture as the stillest.

## Recording Egress: Getting The Frame Off The GPU (v0.85.0)

**The readback was the whole bottleneck.** `SDL_RenderReadPixels` is
synchronous: it issues a copy and maps it in one call, so the render thread
waits for the GPU. At 4K that is 16.8-18.8ms per frame, and with the render
thread parked the capture could only run at about 23fps -- a 50p recording came
out at 326 frames where 700 were owed. Only 2.9ms of it is the CPU-side format
conversion, so there is nothing to win by tightening this side of the call.

**The Windows path is a three-deep D3D11 staging ring.** `CopyResource` into
the ring each frame, `Map` the *oldest* with `D3D11_MAP_FLAG_DO_NOT_WAIT`, and
copy its rows straight into the egress buffer -- no temporary, no conversion.
Cost falls from ~21ms to about 0.01ms. Two consequences worth knowing: the
recording runs a few frames behind the programme, and while the ring primes
there is nothing to read, so the caller keeps the previous picture and lets the
CFR pacer repeat it. A held frame is right; a gap in the timeline is not.

**The staging texture is BGRA on purpose.** It matches both the scale target and
the byte order ffmpeg is fed (`-pix_fmt bgra`), so the mapped rows memcpy
straight out. An earlier version allocated and zero-filled an 8MB temporary
every frame and then walked it twice more -- about 24MB of pointless traffic per
frame, on the render thread.

**Scale before readback, not after.** The composite is blitted into a target at
the *recording* raster first, so a 1080 recording off a 4K programme moves a
quarter of the bytes. The blit is 1:1 and nearly free when no resize is asked
for; its real job is putting the frame into a BGRA target we own, which is what
the staging path needs. This runs for *every* file recording, including the
default that follows the input -- gating it on an explicit recording size meant
the most common configuration fell through to the slow path.

**On a raster change, drop the held frame.** The async path serves the previous
picture whenever the ring has nothing ready, and the encoder locks to the size
of the first frame it sees. Without dropping it, a take started right after a
raster change encodes the whole thing at the old size. Measured: a recording
asked for 1280x720 came back 1920x1080.

### What the other two platforms get

The staging ring is D3D11-only, so macOS and Linux take the synchronous read.
`DECKBOY_EGRESS_READBACK=sync` forces that path on a machine that has the fast
one, which is the only way to measure their behaviour from a Windows desk.
Measured over 14-second takes off a 4K programme:

| standard | frames delivered / owed |
|---|---|
| 1080p50 | 700 / 700 |
| 1080p59.94 | 838 / 839 |
| 2160p25 | 349 / 350 |
| 2160p50 | 326 / 700 |

So the portable path is frame-exact for every raster and rate a show is likely
to deliver, and only 4K above 30p outruns it -- where the dropped-frame alarm
fires once a second on the output health state and in the show log.

### Closing the 4K60 gap: the SDL_GPU download

4K at 50 and 60 was the one thing the portable read could not do, and 60fps has
16.7ms a frame in total against a read that costs 19. It matters -- a 4K60
deliverable is not an exotic ask -- so it needed a real asynchronous path, not a
smaller stall.

**There is no Metal path through SDL's public API.** SDL3.4 exposes
`SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER`, the OpenGL texture id, the Vulkan
image and the SDL_GPU texture -- but nothing that reaches the `MTLTexture`
behind an SDL texture. A Metal-specific readback is not expressible, however
much one would like to write it.

**SDL_GPU is the way through, and it covers all three at once.** The SDL_GPU
renderer publishes both its device (`SDL_PROP_RENDERER_GPU_DEVICE_POINTER`) and
its textures (`SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER`), and
`SDL_DownloadFromGPUTexture` runs on the GPU timeline and hands back a fence.
That is one implementation over Metal, Vulkan and D3D12. It has the same shape
as the D3D11 ring: issue this frame's download, read the oldest slot only if
`SDL_QueryGPUFence` says it has already signalled, and never wait.

Two things it must do that are easy to miss:

- **Flush the renderer first.** SDL batches draws. Without `SDL_FlushRenderer`
  the download can be submitted ahead of the draw that filled the texture, and
  the recording quietly contains the previous picture.
- **Wait on an in-flight fence before releasing its transfer buffer**, in
  teardown only. It is the single place this path blocks, and skipping it lets
  the GPU write into freed memory.

So `createOutputRenderer` asks for the `"gpu"` driver on macOS and Linux, and
leaves Windows on D3D11 -- which is both faster there and the only backend where
the in-process zero-copy decode works. `DECKBOY_OUTPUT_RENDERER=<driver>`
overrides either way; forcing `gpu` on a Windows desk is how this path gets
tested at all.

MEASURED on Windows with the GPU renderer forced, 20s takes off a 4K programme:

| standard | frames | readback cost |
|---|---|---|
| 1080p60 | 1199 / 1201 | flush 0.0006ms, submit 0.13ms, map+copy 3.0ms |
| 2160p30 | 599 / 601 | as above |
| 2160p60 | 850 / 1201 | as above |

The readback is no longer the limiter anywhere: 3.2ms against a 16.7ms budget,
where the synchronous read cost 19. What holds 2160p60 back on *this* desk is
that the SDL_GPU renderer cannot use the D3D11 zero-copy decode path, so the 4K
source is decoded and uploaded on the CPU alongside the recording. That penalty
is specific to Windows, which does not use this path anyway. `DECKBOY_EGRESS_BENCH=1`
prints the three costs every 100 frames.

**A ping-pong between two scale targets does not help.** Blit into one, read
back the other -- last frame's, which the GPU finished with long ago -- was
tried and measured *no better* (311 frames against 326). SDL copies and maps in
a single call and the map waits on the copy whatever the texture's age. Closing
the 4K gap on those platforms needs a real per-backend async path: GL pixel
buffer objects, or a Metal blit into a shared buffer with a completion handler.
Do not spend another afternoon rearranging SDL calls.

## The Control Window Was Stealing Half The Frame Rate (v0.85.0)

Two windows both waiting on vsync means the render loop runs at half the refresh
-- with a 60Hz control window and a 60Hz output, the ceiling for capture was 30
fps no matter how fast the readback was. While any output is running egress the
control window drops vsync and its redraw is capped to 16ms. The show window
keeps its own pacing; the operator's window is the one that can afford to be
late.

## cueKindToken Was Defined Twice (v0.85.0)

`main.cpp` and `core/utils.cpp` each had a non-static definition. The linker
picks one without complaining, and the one it picked was the incomplete copy --
seven of the fifteen cue kinds serialised as plain `video`, so tone, timer,
video synth, PIP, composite, camera, window and Syphon cues all came back as
broken video cues on the next load. The definition now lives in `core/utils.cpp`
only, and the loader repairs the affected saves by looking at the path scheme.
If a token function is worth having, it is worth having once.

## NMOS Node: One Node, Blocking Patches, One SDP (v0.83.0)

**One node per machine, like PTP.** `NmosNode` binds a TCP port, so there can
only be one — it is a member of the app, not of `OutputRuntime`, and it
advertises *every* output that has ST 2110 armed. `syncNmosNode()` runs each
update tick; `setSenders()` early-outs on an unchanged list, so this does not
bump resource versions (and re-POST to the registry) at frame rate. Only a
port or registry-URL change forces a real restart, because the listen socket is
already bound and the registry client caches the parsed URL.

**IDs are derived, not stored.** `nmosDeterministicUuid()` is UUIDv5 (SHA-1)
over a fixed namespace constant plus a per-output seed like
`"sender:output-0-video"`. That gives stable ids across restarts with nothing
written to disk, so a controller's saved route survives. Two consequences:
never change `kDeckboyNamespace` (it renames every sender in every controller
that has us saved), and keep the seed dependent only on *which output* this is
— folding in the address or label would make every retune look like a brand new
sender.

**The patch handler blocks on purpose.** IS-05 arrives on the node's HTTP
thread but the state it changes (`OutputTarget`) belongs to the main thread. The
handler queues the patch and *waits* (2 s cap) for the main thread to really
apply it, then reports the true result. Returning success at queue time would
tell a controller a route moved before it had.

That creates a deadlock: `NmosNode::stop()` joins the HTTP thread, but that
thread may be parked waiting for the main thread — which is the thread calling
`stop()`. **Always tear down via `shutdownNmosNode()`, never `nmosNode_.stop()`
directly**: it fails every waiter and wakes them *before* joining. The 2 s
timeout would eventually break it, but a 2 s freeze on every settings change is
not acceptable either.

**`staged` and `active` are NOT the same object.** `staged` is a scratch area a
controller writes and reads back at will; `active` changes only on activation.
Collapsing them (the original implementation did) makes stage-then-activate
silently impossible. Every staged field carries a `*Set` flag so a PATCH
touching one field cannot blank another. `applyStagedPatch()` is shared by
`single/` and `bulk/` so the two can never drift in what they accept, and it
works on a *copy* — a rejected PATCH must leave staged untouched.

**Conformance is testable, so test it.** The AMWA NMOS Testing Tool
(`IS-05-01`) runs against a live Deckboy in minutes and found nineteen real
defects the hand-rolled mock could not — a missing `/transporttype` (which
suppressed 26 further tests), the staged/active collapse, unknown fields
returning 200, and an invalid FEC/RTCP constraint combination. **Re-run it after
any change to the connection API.** Point it at the node port:
`nmos-test.py suite IS-05-01 --host <ip> --port <nodePort> --version v1.1`.
Note that the tool persists nothing but Deckboy does: a failed run can leave a
bad value (e.g. a literal `"auto"`) saved in the project, so reset the fixture
before re-testing or you will chase a ghost.

**NMOS requires remote reachability, so it refuses to lie about it.** With the
Network tab set to LOCAL ONLY the listener binds 127.0.0.1, but the registration
still advertises the machine's LAN address — a controller finds the sender and
cannot open it. `syncNmosNode()` detects that combination
(`nmosLocalOnlyBlocked_`), serves the node API locally, and **does not register**.
Do not "fix" this by forcing the bind open: LOCAL ONLY is a deliberate operator
choice, and silently overriding it is worse than declining to publish.

**One SDP.** `st2110ConfigForOutput(i)` is the single source of truth; both the
settings modal's "SHOW SDP" and the IS-05 transportfile derive from it. Do not
add a second SDP builder — a receiver handed two different descriptions of one
flow is a fault that only shows up at a venue.

**Honesty constraints baked in.** The advertised clock reports `ref_type:ptp`
only when genuinely locked, otherwise `internal`; `hostname` is folded to the
RFC 1123 character set (an operator label with a space fails schema validation
and gets the whole node rejected); scheduled activation returns 501 rather than
pretending. There is no mDNS, so an empty registry URL is labelled "NOT SET" in
the UI rather than presented as a working default.

## AOI / Edge Blend: Pixel View Over Fraction Storage (v0.80.0)

The settings UI edits the Area of Interest as a pixel rect (`X/Y/W/H` of the
output raster from `outputRenderSizeForOutput`) and edge blending as pixel
widths, but `OutputTarget::aoi*` / `Deck::edgeBlend*` stay fractions — the
serializer, remote commands, and render path are untouched. Conversion lives
in `focusedOutputAoiRectPx` / `applyFocusedOutputAoiRectPx`
(app_render_settings.ipp). The apply helper clamps the region to ≥5% of the
raster per axis, which keeps every stored edge fraction within the
serializer's long-standing 0–0.95 clamp — do not relax one without the other.
Same doctrine as Pixel-Based Geometry Editing (v0.76.21): operators get
pixels, storage keeps normalized values.

## Stereo Waveform: Content-Authoritative Split (v0.80.0)

`WaveformPeaks` carries a `distinct` flag measured during analysis: the
ffmpeg decode is forced `-ac 2`, and the side-signal ratio
`Σ|L-R| / Σ(|L|+|R|) > 1%` marks real stereo. `drawWaveform` uses **only**
this flag to pick split-vs-mono — cue metadata (`audioChannels`) is a hint
for badges, not the view, because (a) cues saved before the field existed
load with 0 and (b) stereo containers frequently carry mono content, where
twin identical lanes waste half the lane. Old saves additionally get
`audioChannels` backfilled via `queueAudioMetadataRepairProbes()` on project
open (the probe poll in app_update.ipp has a repair branch that fills audio
fields on already-probed cues; capped at 64 probes per open).

## Panic Must Kill Audio (v0.80.0)

`runPanicOutputsOff` (triple-Esc and the outputs_off panic profile) now stops
every deck engine (`stop(true)`) and browser cue after disarming outputs.
Audio does not route through video outputs, so an outputs-only panic left
sound running against a dark program. Related fix: `MediaEngine::stop`
treated only `CueKind::Video` as stoppable A/V — Audio cues fell into the
"not video" early-return that neither killed decode pipes nor paused the
audio stream. Both cue kinds now share the stop path. If a new audible cue
kind is ever added, it must join that `isAV` check.

## Test Bars Pattern (v0.80.0)

`pattern://test-bars` (aliases testsrc/testsrc2) is a homage to ffmpeg's
testsrc2, born from the v0.79.10 HEVC test sessions: saturated bars, a
bouncing rainbow diagonal, a dissolving checker patch, a sliding grey block,
and a clock + nominal-30fps frame counter. Built by
`MediaEngine::buildTestBars`; always animated (`patternTypeIsAnimated`), no
`-motion` variant — motion is the point. Verify with
`--pattern-dump test-bars out.ppm 1280x720 6.5`.

## Native Terrarium Pattern (v0.78.4)

The Terrarium ecosystem sim runs in-process as `pattern://terrarium`. The
whole SDL-free core (world state, 9 TPS step, procedural species, the 8x8
glyph font, per-cell glyph/color logic, and `renderWorldRgba` — a pixel
renderer mirroring the exe's SDL render loop cell-for-cell) lives in
`native/extras/terrarium_core.hpp`, namespace `terra`, included by BOTH the
companion exe (which keeps only windowing/input/SDL textures) and
media_engine.cpp. Traps: (1) the engine TU pulls windows.h — legacy macro
names like `near` are BANNED in the core (we hit this); (2) glyph bitmap
arrays are extern-declared early and defined at the bottom of the header —
keep declarations and definitions inside the namespace together; (3) the
pattern is STATEFUL — one process-wide world behind a mutex in
`buildTerrariumFrame` (media_engine.cpp), seeded once with a 120-tick warmup
and stepped off the wall clock; `rebuildPatternFrame` throttles terrarium to
the sim's 9 TPS because each frame is a 5.7 MB copy + upload. The Konami egg
now adds the native pattern cue (no exe launch, no window capture).

## Pocket Test Card (v0.78.1, diegetic rework v0.78.2)

`pocket-test` (the auto-cycling default pattern) is Deckboy's working test
card: `drawPocketTestCardOverlay` in media_engine.cpp draws the instruments
over the island scene. the owner's direction: instruments must be DIEGETIC —
scene objects (billboard = color bars, staircase = grayscale, sky banner =
banding ramp, cave eyes = 2%/4% black-crush, cloud lumps = 98%/96% white
clip, flashing ? block = cadence, beach TV static = fine detail, runner past
10%-spaced fence posts = judder, buoy lamp + engine-synthesized 1 kHz pop =
A/V sync, v0.78.3 — `queuePocketSyncAudio` keys the pop to the wall time the
samples will PLAY, i.e. now + queued audio, so it aligns with the flash the
overlay draws off the same clock), never chart furniture. The Pokémon-style
dialog box (chunky `gbBox` chrome, cream/ink/red palette, encounter text,
blinking continue-cursor) is the game-UI layer. The forced-scene variants
(`pocket-day` etc.) deliberately do NOT get the overlay — operators use them
as backgrounds. Measurement values must stay EXACT (75% = 191, 2% = 5, etc.)
and the pixel-precision elements (border checkerboard, TV static) must never
be scaled by the proportional unit `u` — single pixels are the point. Text
uses a built-in 3x5 pixel font (`glyphRows` lambda) since patterns are raw
CPU pixels with no TTF access. `--pattern-dump <id> <out.ppm> [WxH] [t]`
renders any pattern for visual inspection; smoke scans for the exact
diegetic values (75% red, 2% eyes, 96% lump) + the border checker, and
asserts scene variants stay clean.

## In-Process GPU Decode (v0.78.0)

Session 2 of `docs/GPU_DECODE_PLAN.md`: file-backed Video/Audio cues decode
in-process via libav\* (`native/engine/libav_decoder.hpp/.cpp`,
`DECKBOY_INPROC_DECODE`), replacing the two ffmpeg subprocess pipes per deck.
Architecture and traps:

- **Zero-copy path (Windows/D3D11).** Deck engines get a
  `DecodeDeviceProvider` returning the program output renderer's
  `ID3D11Device` (`primaryOutputDecodeDevice()`); the decoder adopts that
  device into an ffmpeg hw ctx and decodes d3d11va straight onto it. Decoded
  frames ride `DecodedFrame::gpu*` (texture-array slice + AVFrame ref;
  `pixels` empty). `renderDeckLayerIntoOutput` GPU-copies the slice into a
  per-deck SDL-owned NV12 texture (`ensureLayerGpuTexture` →
  `createWrappedNV12Texture`, which pulls SDL's backing texture out via
  `SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER`) — video never touches the CPU.
- **`SDL_HINT_RENDER_DIRECT3D_THREADSAFE = "1"` at init is LOAD-BEARING.**
  SDL otherwise creates its D3D11 devices `D3D11_CREATE_DEVICE_SINGLETHREADED`;
  the ID3D10/11Multithread QI then fails and a shared device crashes or
  deadlocks at random (we hit both). `adoptD3D11Device` refuses devices where
  multithread protection can't be enabled — zero-copy silently degrades to
  in-process CPU output, which is the symptom to check first if
  `--decode-bench` reports `inproc-cpu` unexpectedly.
- **Who falls back where:** live streams (SRT/NDI), stills, capture,
  waveform, ffprobe, encode-out → CLI (unchanged). Rotation-metadata files →
  CLI (libav doesn't autorotate). RGBA effects cues + non-hw codecs +
  engines without a device provider (preview/PiP) → in-process CPU frames.
  In-process open failure → CLI pipe path automatically. Runtime
  break-glass: `--no-inproc-decode`; build-time: `-DDECKBOY_INPROC_DECODE=OFF`.
- **Semantics preserved:** the decode threads keep the same
  `frameQueue_`/`kMaxVideoFrames` backpressure and frame indexing; audio
  decodes to the same s16/stereo/48k stream through the shared
  `applyGainAndQueueAudio` tail, so the audio-master A/V clock (v0.76.19) is
  untouched. Audio speed uses an avfilter atempo chain identical to the old
  CLI args; in-point seeks trim to the sample in the output domain.
- **Crash resilience (no more subprocess isolation):** `VideoPipeline::open`
  primes the first frame (validation gate + hw→sw retry), corrupt packets
  degrade to EOF after `kMaxConsecutiveErrors`, and `consumeDecodeStall()`
  (4 s watchdog in `update()`) makes the app rerack the deck dark + toast.
  `stopDecoderThreads` trips the pipelines' AVIO interrupt callback before
  joining — the in-process replacement for killing the child to unblock a
  pipe read (dead network shares can't hang a TAKE).
- **Device lifecycle:** output-runtime create/destroy calls
  `scheduleDecodeDeviceReconcile()`; the app tick restarts decode on decks
  whose `activeDecodeDevice()` no longer matches (`reconcileDecodeDevices`).
  Frames referencing a destroyed output's device stay valid (COM refs);
  consumers on a different device CPU-download per frame advance
  (`downloadGpuFrameNV12`), control preview throttles that to ~10 fps.
- **Bench:** `--decode-bench <file> [seconds] [cli]` — prints
  `mode=inproc-zerocopy | inproc-cpu | cli-pipe` and sustained decode fps
  (consumer drains the queue; pacing removed). Desktop reference (RTX-class,
  1080x1920 h264): cli-pipe 228, inproc-cpu 236, inproc-zerocopy 240 —
  desktop is decode-bound; the Pocket is transport-bound, which is where the
  win lives. Capture Pocket numbers before the next show.

## SDL2 → SDL3 Migration (v0.77.0)

Whole-app port, executed per `docs/SDL3_MIGRATION_PLAN.md` (Session 1 of the
GPU-decode Option B sequencing — decode rewrite follows in a separate session
on this base). Design decisions and the traps future edits must respect:

- **Compat layer: `native/core/sdl_compat.hpp`.** Every `#include <SDL.h>` was
  rewritten to include this header. It provides:
  - C++ overloads of `SDL_RenderFillRect` / `SDL_RenderRect` /
    `SDL_RenderTexture` / `SDL_RenderTextureRotated` that accept the int
    `SDL_Rect` the entire layout system uses and convert to `SDL_FRect` at the
    draw boundary. Layout stays integer; only the boundary converts. A literal
    `nullptr` rect needs no cast (dedicated `std::nullptr_t` overloads).
  - `deckboyCreateTexture` / `deckboyCreateTextureFromSurface` — ALWAYS use
    these instead of raw `SDL_CreateTexture*`: they apply
    `SDL_SCALEMODE_NEAREST` per texture, preserving the SDL2-era global
    `SDL_HINT_RENDER_SCALE_QUALITY="0"` look (the hint is gone in SDL3, default
    is linear — a raw create call would silently blur pixel-art UI).
  - SDL2-style display indices (`deckboyGetNumVideoDisplays`,
    `deckboyDisplayIdFromIndex`, `deckboyGetWindowDisplayIndex`,
    `deckboyGetDisplayBounds`, `deckboyGetDesktopDisplayMode`, …) mapping index
    ↔ `SDL_DisplayID` through `SDL_GetDisplays()` order. Projects keep
    persisting display *indices*; hot-plug revalidation works as before.
  - `deckboySetAudioPaused(stream, paused)` — SDL2 `SDL_PauseAudioDevice(dev,
    0/1)` semantics over a device-bound stream.
- **Audio = SDL3 streams, queue model preserved.** Every SDL2
  `SDL_AudioDeviceID` became an `SDL_AudioStream*` from
  `SDL_OpenAudioDeviceStream` (one logical device per consumer: per-deck main
  out, UI sounds, LTC recording). `SDL_QueueAudio` → `SDL_PutAudioStreamData`,
  `SDL_GetQueuedAudioSize` → `SDL_GetAudioStreamQueued`, `SDL_ClearQueuedAudio`
  → `SDL_ClearAudioStream`, close → `SDL_DestroyAudioStream` (closes the
  logical device). Streams open PAUSED; the engine resumes per transport state.
  - **A/V audio-master clock re-anchored** on `SDL_GetAudioStreamQueued`:
    "queued frames minus stream-buffered bytes" as before. The physical device
    buffer beyond the stream adds a small constant offset (~one buffer,
    5–20 ms) that sits inside the 60 ms drift threshold — do not tighten that
    threshold below ~2 device buffers.
  - The operator buffer-size setting (`audioBufferSamples`) now applies via
    `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES` set immediately before each device
    open (`applyAudioBufferSizeHint()`); SDL3 specs have no `samples` field.
  - Device pickers enumerate `SDL_GetAudioPlaybackDevices` and resolve
    persisted device *names* to ids at open time; a missing name falls back to
    the system default (same policy as SDL2).
- **Fullscreen model.** `SDL_WINDOW_FULLSCREEN_DESKTOP` is gone. Policy
  mapping: borderless desktop = `SDL_SetWindowFullscreenMode(win, nullptr)` +
  `SDL_SetWindowFullscreen(win, true)`; exclusive (only when the operator picked
  a fixed raster/refresh) = `SDL_SetWindowFullscreenMode(win, &mode)` first.
  Flag checks are just `flags & SDL_WINDOW_FULLSCREEN` now.
  `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS="0"` still exists in SDL3 and REMAINS
  SET — never remove (see "output frozen" saga below). The recovery/backoff
  policy in app_output_mgmt.ipp is unchanged; only the SDL calls under it are
  new. Re-verify taskbar/focus/hot-plug in the field.
- **Windows are shown by default** in SDL3 (`SDL_WINDOW_SHOWN` gone); hidden
  decode/monitor windows keep `SDL_WINDOW_HIDDEN`. Visibility checks use
  `!(flags & SDL_WINDOW_HIDDEN)`. `SDL_CreateWindow` lost the x/y args —
  position is set after create via `SDL_SetWindowPosition`.
- **Renderers**: no creation flags. vsync is per-renderer at runtime
  (`SDL_SetRenderVSync`): control window ON, visible program outputs ON,
  stream-only outputs and hidden per-deck decode renderers OFF (never set).
  Software fallback = `SDL_CreateRenderer(win, SDL_SOFTWARE_RENDERER)`.
- **Events**: window events are top-level (`SDL_EVENT_WINDOW_CLOSE_REQUESTED`
  etc.), display hot-plug is `SDL_EVENT_DISPLAY_ADDED/REMOVED`. Mouse/wheel
  coords are float — the UI truncates via `static_cast<int>` at the dispatch
  boundary in app_update.ipp. `event.drop.data` is OWNED BY SDL now (freeing it
  is a heap corruption); `event.key.key`/`.mod` replace `.keysym.*`; letter
  keycodes are uppercase (`SDLK_A`); `SDL_StartTextInput` takes the window.
- **Bool returns**: most SDL3 calls return `bool` (true = success). All the old
  `== 0` / `!= 0` int-return checks were flipped; when adding code, never write
  `SDL_X(...) == 0` for success.
- **`SDL_RenderReadPixels` returns a new `SDL_Surface*`** (no in-place buffer
  fill). Egress capture converts via `SDL_ConvertPixels` into the persistent
  BGRA buffer; the key-color picker reads via `SDL_ReadSurfacePixel`.
- **`SDL_Vertex` carries `SDL_FColor`** (floats) — warp/edge-blend vertex
  alphas convert `Uint8 → a/255.0f`.
- **Renderer format probe** moved from `SDL_RendererInfo` to the
  `SDL_PROP_RENDERER_TEXTURE_FORMATS_POINTER` property (UNKNOWN-terminated
  array) — see `rendererSupportsTextureFormat()`.
- **HWND** for WM_SETICON comes from
  `SDL_PROP_WINDOW_WIN32_HWND_POINTER` (SDL_syswm.h is gone).
- **Not migrated / unchanged**: `SDL_Color` (554 refs), `SDL_SetRenderClipRect`
  still takes int `SDL_Rect`, blend modes, `SDL_UpdateNVTexture` (NV12 upload
  path intact for the decode rewrite), render targets.
- **Next session**: in-process libav decode straight to zero-copy via
  `SDL_CreateTextureWithProperties` + `SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER`
  (GPU_DECODE_PLAN §7–§11, Session 2). Keep it separate from any other
  playback-path work.

## Media converter + ENCODER tab (v0.76.31)
- `cueConvertReason()` flags file-backed Video cues that ffprobe can't read
  (tracked in the transient `unreadablePaths_` set) or that carry a heavy codec
  (HEVC/H265/AV1/ProRes/DNxHD) or a >1080p frame. Surfaced three ways: a toast on
  import, a contextual CONVERT button in the SELECTED CUE panel (only when
  flagged/converting), and the Settings → Encoder tab (`settingsTab_ == 5`).
- `convertCueMedia()` runs an async ffmpeg job in `conversionJobs_`: CPU decode +
  `h264_nvenc` (libx264 fallback) → `<show>/_converted/<stem>.mp4`. The per-frame
  poll in `update()` swaps the cue's path to the copy and pushes a re-probe on
  success. Original untouched; `_converted/` is git-ignored.

## Audio device hot-swap (v0.76.31)
- `MediaEngine::setAudioDevice()` redirects the SDL output device in place (the
  engine never owns it — it only queues PCM via SDL_QueueAudio). `reopenDeckAudioOutput()`
  now hot-swaps on the existing engine instead of `stopAll()` + recreate, so
  changing a deck's audio output mid-cue no longer stops playback. First-time
  setup (null engine) still constructs it.

## Deck-list scroll + splash system (v0.76.31)
- The MAIN deck cue list (renderCueRow path, not the compact panel) clamps
  `deckScrolls_` to `[0, deckScrollMax_]` before drawing, with a bottom-only
  rubber-band: the wheel may push to `max + kDeckScrollOverscroll`, then a
  time-based exponential settle (τ≈71ms, gated on `lastDeckScrollMs_` idle)
  springs it back. Top is hard-clamped at 0.
- Splash: grayscale masters in `data/ui/.../splash/cycle/` are picked at random
  per boot (`SDL_GetPerformanceCounter`) and tinted to `pal.light` via
  `splashTintable_`; the default (gameboy) theme instead boots the branded
  character splash untinted. The splash background follows `pal.deep` (was a
  hardcoded green).

## Save / Theme / Output / UX batch (v0.76.30)
- **SAVE always prompts.** The toolbar SAVE button and Ctrl+S both call
  `saveProjectAsFromPicker` (file picker every time, project only — no media);
  there is deliberately no separate Save As button (it would be identical).
  BUNDLE (`exportProjectBundleTo`) is the export-with-media path. The old SAVE
  silently overwrote `data/default.deckboy` with no filename feedback, which
  read as "saving a default state."
- **Theme persists per project.** `Project::theme` (types.hpp) holds the saved
  colorway (a `data/themes/<name>` dir), serialized as a key-value line
  `theme\t<name>` like `splash_character`. Applied on open in
  `openProjectFromPath` and at boot after project load — only when non-empty,
  so an older theme-less show never stomps the operator's current pick.
  `DECKBOY_THEME` still hard-overrides at boot. The Appearance dropdown scans
  `data/themes/` for any dir containing `theme.txt` (24 shipped: dark sci-fi +
  Nintendo colorways).
- **Output flushes black instead of freezing.** A disabled-but-visible output
  window is painted black exactly once, latched by
  `OutputRuntime::blackedWhileDisabled` (reset when it renders again) via
  `clearDisabledOutputWindow` in the render loop — this covers New Show, where
  `ensureOutputRuntimesSynced` reuses the window and just stops rendering it.
  `destroyOutputRuntime` also presents two black frames before tearing a
  visible window down (exit / display switch / capture dongles that latch the
  last received frame). Never let a disabled output silently hold the last
  frame.
- **Inline editor type-to-replace.** `InlineTextEditorState::freshEntry` is set
  on open; the first character (or backspace) clears the pre-filled value
  first, mimicking select-all-on-focus. Submitting with no edit keeps the
  original value.
- **Recursive folder import.** `importPaths` expands directories via
  `recursive_directory_iterator` (skip_permission_denied, name-sorted),
  filtered by `isAcceptableMediaPath` (video/image/audio ext lists beside
  `isImagePath`). Cue kind is Image/Audio/Video; `probeCue` now also returns
  Audio for audio extensions so the async probe doesn't relabel audio as Video.
- **Cue-list over-scroll** is clamped BEFORE the draw loop (deck panel list and
  overlay bin) so the wheel can't push past the last row into empty space; the
  old after-draw clamp flickered.
- **Audio timeline seek.** The audio lane rect is stored in
  `audioProgressBarRect_` and is a click-to-seek target alongside
  `progressBarRect_` (same x-mapping, shared scrub/resume path).

## Pixel-Based Geometry Editing (v0.76.21)
- Operator-facing size unit is output pixels; `Cue::outputScaleX/Y`
  multipliers are an implementation detail. `cueBaseRenderSize(cue)`
  (app_geometry.ipp) returns the rendered px size at multiplier 1.0 — the
  same scale-mode + crop math as the compositor path; keep them in step.
  `finalPx = base × outputScale`; typed px → `scale = px / base`, clamped to
  the historical 0.25–4.0 range (so extreme px requests saturate).
- `Project::geometryAspectLinked` (default true, header line
  `geometry_aspect_link`) drives the link: any scale change on one axis
  multiplies the other axis by the same relative factor
  (`newOther = other × newX/oldX`). Applied in `adjustSelectedScaleX/Y`
  (nudge buttons) and `editSelectedScaleX/Y` (typed px). Note the clamp can
  bend aspect at range extremes — same tradeoff as other media software.
- UI: `link aspect` toggle row (QuickAction::ToggleAspectLink) in the
  GEOMETRY section above width/height. If a new surface edits cue scale,
  route it through the adjust/edit helpers — do not write outputScaleX/Y
  directly, or the link silently won't apply.

## Fullscreen Minimize-On-Focus-Loss Root Cause (v0.76.20)
- The deepest layer of the fullscreen fight: SDL2 minimizes EXCLUSIVE
  fullscreen windows on focus loss by default. Operator clicks the control
  window → program output minimizes ("output frozen while preview plays")
  → recovery re-raises + re-fullscreens it (stealing focus) → operator
  clicks again → loop. `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS = "0"` is now
  set in init, right after the DPI hint. Never remove it — a playout output
  must survive the operator working in the control surface.
- `enableOutputFullscreen` now uses exclusive `SDL_WINDOW_FULLSCREEN` (real
  mode switch) ONLY when `!outputFollowDisplay || outputRefreshRateHz > 0`
  (operator explicitly chose a fixed raster or refresh). Display-native
  outputs use `SDL_WINDOW_FULLSCREEN_DESKTOP` — no mode switch, no
  blanking, reliable placement on mixed-DPI multi-monitor. The custom-EDID /
  high-refresh feature is unchanged when explicitly requested.

## Fullscreen Recovery Fight Postmortem (v0.76.20)
- v0.76.19 widened `recoverWindowOutputIfNeeded`'s `wrongDisplay` check to
  apply while the window was fullscreen (intended to migrate windows left on
  the wrong display after hot-plug). Field result: on a mixed-DPI
  multi-monitor setup, `SDL_GetWindowDisplayIndex` persistently disagreed
  with the target for a fullscreen window → recovery exited fullscreen,
  moved, re-entered, and raised the output every 1.2 s. Each
  `SDL_RaiseWindow` moved keyboard focus to the output window (which
  ignores all keys except Esc by design) and ate in-flight clicks. Operator
  experience: "typing becomes difficult", "controls seem like they weren't
  happening", "trying to take over the wrong screen".
- **Rules now enforced:**
  1. `wrongDisplay` is only evaluated for NON-fullscreen windows (original
     behavior). A stable fullscreen window is never repositioned
     automatically; wrong placement is the operator's explicit call.
  2. Strike backoff: >3 recovery attempts within 15 s → recovery pauses 30 s,
     health `Error("output unstable - recovery paused 30s")` + toast. A
     healthy output needs recovery rarely; repetition means recovery itself
     is the problem.
  3. Any path that raises an output window (recovery, deferred display-move
     tick, display identify) captures whether the control window had
     keyboard focus first and re-raises it after.
- The audio-master clock gained a stall guard in the same release: the
  correction only runs while the audio clock advanced within the last
  400 ms. A frozen clock (dead endpoint, audio pipe death mid-file) would
  otherwise pin video at the freeze position forever.

## Display Identify Overlay (v0.76.20)
- `showDisplayIdentify()` / `renderDisplayIdentify()` / `closeDisplayIdentify()`
  in `app_output_mgmt.ipp`; state is `identifyWindows_` + `identifyUntilMs_`.
  One borderless always-on-top window per display, auto-closed after 2.5 s
  from the render loop. Text is blitted straight from TTF surfaces per
  identify renderer — the `drawText*` helpers are bound to
  `controlRenderer_` textures and must not be used for other renderers.
- Triggered by `kSettingsActionDisplayIdentify` (637) — the IDENTIFY button
  in Video Outputs → Display → CONNECTED DISPLAYS. The list itself now sizes
  for every connected display (clamped to modal space) instead of collapsing
  to two rows.

## Engine Cue Snapshot Ownership (v0.76.19)
- `MediaEngine` no longer holds a raw pointer into `Deck::cues`.
  `loadCue()` copies the cue into `activeCueSnapshot_` (a
  `std::optional<Cue>`) and `activeCue_` points at that owned storage.
  Rationale: `deck.cues.push_back()` (import) reallocates and
  `deck.cues.erase()` (delete) shifts elements while the render path reads
  `activeCue_->fadeInSeconds` every frame and the audio thread reads it per
  sample pair — the old pointer dangled (reallocation) or silently retargeted
  the *next* cue's parameters (erase-shift above the live cue).
- **Live-edit contract**: the output compositor reads geometry/effects from
  the app's cue (`activeCuePtr(deckIndex)`), NOT the engine snapshot — so
  geometry/color edits were never affected. Fields the ENGINE reads mid-play
  (fade in/out, still duration) stay fresh because `markProjectDirty()` sets
  `engineCueSyncPending_`, and `App::update()` drains it by calling
  `syncEngineCueSnapshots()` → `engine->syncActiveCueSnapshot(cue)` for every
  deck with a valid activeIndex. One frame of lag, no per-frame copying.
- `refreshActiveCueRuntime(const Cue* updatedCue)` now takes the app's
  current cue and refreshes the snapshot before re-reading runtime params.
- When adding engine reads of new cue fields, nothing extra is needed — the
  snapshot sync covers all fields. When adding new app-side mutation paths,
  make sure they call `markProjectDirty()` (they all should anyway).

## Audio-Thread Fade Atomics (v0.76.19)
- The audio decode thread previously called `fadeGainAt()`, which reads
  `activeCue_`, `duration_`, and the two suppress flags — plain members
  mutated by the main thread (data race, torn-double risk on non-x86).
- Now: `syncAudioFadeParams()` (main thread) publishes fade-in/out seconds,
  duration, and suppress flags to relaxed atomics; the audio thread uses
  `audioFadeGainAt()` which reads ONLY those mirrors. Publish points: before
  the audio thread spawns in `startDecoderThreads`, in `stopAll`, in
  `syncActiveCueSnapshot`, and once per `update()` tick as a catch-all
  (covers `handlePlaybackEnd` loop re-assert, browser duration restore).
- Rule: never add a read of a non-atomic engine member inside the audio
  thread lambda. Mirror it through an atomic and publish in
  `syncAudioFadeParams()` (or a sibling).

## Audio-Master A/V Clock (v0.76.19)
- `position()` runs on the wall clock (steady_clock); the audio pipeline
  free-runs into the SDL queue throttled only by queue depth. The two clocks
  drift (audio device crystal ≠ steady_clock; VFR sources drift immediately
  because frame index × probed fps is wrong for them).
- The audio thread counts stereo frames it queues (`audioFramesQueued_`,
  atomic). `update()` derives the audio playback clock:
  `audioClockStartSeconds_ + (queued − SDL_GetQueuedAudioSize buffered) ×
  playbackSpeed_` (atempo re-times the pipe to wall rate, position space is
  speed × wall). When |video − audio| > 60 ms it re-anchors
  `playbackClockStart_`/`playbackStartPosition_` to the audio clock.
- Guards: needs ≥100 ms of queued audio before trusting the clock; skipped
  when `decoderEof_` (audio drains before video ends), within 250 ms of the
  cue end, and for live streams (`audioClockValid_` false — no deterministic
  sample→position mapping). `seek()`/`refreshActiveCueRuntime()` re-base the
  clock automatically because they restart the pipes with a new
  `cueStartSeconds`.
- Cues without audio keep the wall clock — nothing to slave to.

## Display Identity By Name + Hot-Plug Safety (v0.76.19)
- `OutputTarget::displayName` (serialized field 35, guard ≥ 36) records the
  SDL display name whenever the operator explicitly picks a display
  (`setOutputDisplayIndex`, `cycleOutputDisplay` → `recordOutputDisplayName`).
  SDL display indices are enumeration-order-dependent and shuffle across
  hot-plug/reboot/driver updates — a bare persisted index could silently
  retarget program output to the operator's monitor.
- `resolveOutputDisplayIndex()` is the single resolution choke point (called
  from `applyOutputDisplaySelection` and `refreshDisplayTopology`): keep the
  current index if its name still matches, else find the display carrying the
  recorded name, else clamp — WITHOUT erasing `displayName`, so the intended
  display re-matches when it comes back.
- `refreshDisplayTopology` no longer calls
  `applyOutputDisplaySelectionAllOutputs(true, true)`. That forced every
  enabled output through a fullscreen exit/re-enter on ANY display connect/
  disconnect and — critically — re-fullscreened outputs the operator had
  escaped to windowed (bypassing `recoveryPausedByEscape`). It now resolves
  indices and heals via `recoverWindowOutputIfNeeded`, which honors the
  escape flag and `fullscreenIntended`. `wrongDisplay` detection now also
  applies to windows still fullscreen on the wrong display (post-hot-plug),
  and recovery passes `allowFullscreenTransition=true` only for display
  moves. Zero-display scans (RDP handoff, driver reset) no longer mutate
  persisted display targets.
- `tickPendingOutputDisplayTransitions` cancels a pending re-fullscreen when
  `recoveryPausedByEscape` is set. Explicit operator display changes re-arm
  (the runtime-rebuild path clears the escape flag).

## HyperDeck Snapshot (v0.76.19)
- `hyperDeckHandleCommand` runs on the HyperDeck TCP thread. It previously
  read `focusedDeck()` (a race against main-thread cue mutations) and
  inferred transport by substring-matching the human-readable status text —
  a cue named "playing" would corrupt replies.
- Now: `App::HyperDeckSnapshot` (transport token, 1-based clip id, loop,
  {name, duration} clip list) is rebuilt on the main thread in
  `updateStatusSnapshot()` and read under `statusSnapshotMutex_`. Rule for
  new HyperDeck (or any network-thread) handlers: reply from a snapshot
  field; if one doesn't exist, add it to the snapshot — never touch
  `project_` from the network thread.

## Still Cue Hold / Fade Interaction (v0.76.17)
- **Symptom:** an Image (or other still-type) cue with hold / pause-on-last set
  vanished the instant its `stillDurationSeconds` elapsed, even though the frame
  was still resident. Animated Pattern cues masked the bug because they re-render
  every tick.
- **Root cause chain:**
  1. The output composites `MediaEngine::currentFrame()` (the `DecodedFrame`),
     and its alpha = `currentVisualFadeGain()` = `visualFadeGainAt(position())`.
  2. `position()` returns `currentPosition_` whenever the engine isn't `Playing`.
  3. `handlePlaybackEnd()` → `PauseOnLast` correctly froze at
     `currentPosition_ = pausedPosition_ = duration_` and set state `Paused`.
  4. BUT the very next `update()` tick took the non-video `else` branch and
     reset `currentPosition_ = 0.0`. So `position()` returned 0, and
     `visualFadeGainAt(0)` applied the **fade-IN** ramp at t=0 → gain 0 → the
     held frame drew fully transparent.
- **Fix:** the still `else` branch now holds `currentPosition_ = pausedPosition_`
  instead of snapping to 0. `pausedPosition_` is the authoritative freeze point
  (0 at start, mid-cue for a manual pause, `duration_` after pause-on-last).
- **Fade-out policy:** `visualFadeGainAt` does NOT special-case hold cues — a
  configured fade-out always ramps. Instead, still-type cues default to
  `fadeOutSeconds = 0` in `applyDeckDefaultsToCue` (a static graphic that holds
  shouldn't dip to black), but the operator can re-enable fade-out per cue and it
  will then run normally. An earlier attempt that suppressed fade-out for
  PauseOnLast in `visualFadeGainAt` was reverted because it broke deliberately
  configured fade-outs.
- **Render loop refresh:** `run()`'s anti-spin floor is `1/240s`, not `1/120s`,
  so vsync (not the floor) governs on 144/165/240 Hz displays.

## Output Backend Route Architecture (v0.76.11)
- Output destinations use a catalog → route-plan → runtime-route
  pipeline. `OutputBackendCatalog` (output_backend.cpp) reports all
  backends with platform availability at compile time via feature
  gates (`DECKBOY_HAS_NDI_SDK`, `DECKBOY_HAS_DECKLINK`,
  `DECKBOY_HAS_SPOUT`). `planOutputBackendRoute()` converts an
  `OutputBackendRouteRequest` (built from per-output `OutputTarget`
  enable flags) into an ordered list of steps, each annotated with
  whether the backend is supported on this build.
- At runtime, `resolveOutputBackendRuntimeRoute()` in
  `app_output_mgmt.ipp` maps the plan into `*Supported` bools on an
  `OutputBackendRuntimeRoute` struct. The render loop in
  `app_render_output.ipp` computes `*RouteActive` (enabled AND
  supported) and gates send/shutdown calls accordingly.
- Adding a new output backend: add enum to `OutputRouteKind`, add
  catalog entry (with platform `#if` guards), add enable flag to
  `OutputBackendRouteRequest` and `OutputTarget`, add case to
  `planOutputBackendRoute()` and `resolveOutputBackendRuntimeRoute()`,
  implement send/shutdown functions in `app_output_mgmt.ipp`, add
  `*RouteActive` check in render loop, add settings UI, add
  serialization fields.

## Spout Integration Notes (v0.76.11)
- Spout2 is Windows-only interprocess texture sharing (analogous to
  Syphon on macOS). The `siphon_spout.hpp` interface is shared; the
  `.cpp` has `#if defined(DECKBOY_HAS_SPOUT)` for the Windows
  implementation and a stub for other platforms.
- SpoutLibrary provides a COM-like interface via `GetSpout()` factory.
  Lifecycle: `GetSpout()` → `SetSenderName()` → `SendImage(pixels,
  w, h, GL_BGRA, true)` per frame → `ReleaseSender()` → `Release()`.
  `SendImage()` accepts raw CPU pixel buffers and handles
  DirectX/OpenGL internally — no GL context from the caller.
- The sender name is user-configurable via settings. Changing the
  name requires `ReleaseSender()` then `SetSenderName()` to re-create
  the shared texture with the new identity.
- Installed via vcpkg (`spout2:x64-windows`). CMake auto-detects via
  `find_package(Spout2 CONFIG QUIET)` and sets `DECKBOY_HAS_SPOUT`.
  Falls back to manual `find_path` if `ENABLE_SPOUT` is explicitly
  set.

## MSVC Block-Nesting Limit and handleSettingsClick (v0.76.11)
- MSVC has a hard limit on block nesting depth (C1061, ~128 levels).
  The settings click handler's if-else-if chain can exceed this when
  enough handlers accumulate. The function is split across three
  methods: `handleSettingsClick` → `handleSettingsClickPart2` →
  `handleSettingsClickPart3`. Each ends with
  `else { handleSettingsClickPartN+1(sb); }` to chain to the next.
- When adding new settings handlers, add them to the last Part
  function. If the build starts failing with C2628 / C2065 cascades
  (members reported as undeclared), split again by ending the current
  chain with an `else { handleSettingsClickPartN(sb); }` and starting
  a new function.

## Timeline Loading Animations (v0.76.10)
- The timeline has two lanes — video (`progressBarRect_`) and audio
  (`audioLaneRect`) — each of which can be waiting on background work
  before it has anything meaningful to draw. Video waits on
  `timelineStripTex_` to be rendered from the cue's thumbnail grid;
  audio waits on `computeWaveformPeaks` to finish on a background
  `std::async`. Both are gated by kind-specific state: the video
  loading branch fires when `timelineStripLoading_` is true for the
  current cue's cache key, the audio branch fires when
  `getWaveformPeaks` returns `pending == true` and an empty peaks
  struct.
- Both animations use the same widget: a `drawUIPanel`-framed box
  centered in its lane, sized proportionally to the lane
  (`std::min(188, std::max(124, laneRect.w - 28))` x
  `std::min(46, std::max(34, laneRect.h - 16))`), over a translucent
  `{7, 12, 7, 148}` dimming overlay, with a pulsing "LOADING..."
  label at the bottom whose dot count advances on a 180ms clock.
  This is deliberate: they should read as sibling animations so the
  operator recognizes "timeline is fetching" instantly regardless of
  which lane is loading.
- Iconography differs to distinguish lanes. Video uses 5 filmstrip
  cells with sprocket holes (`drawTimelineLoadingAnimation`); audio
  uses a 9-bar EQ meter where each bar's height is a squared sine
  envelope with a per-bar phase offset
  (`drawAudioTimelineLoadingAnimation`). Both animations run on
  `animationNow_` as their time base, so the idle/fps clamp
  behavior for UI animations applies uniformly.
- If you add a third lane that can load asynchronously, follow the
  same pattern: build a lambda in `app_render_main.ipp` next to the
  existing two, reuse the widget frame + dimming + LOADING label,
  and pick iconography that's unambiguously different from the
  other two. Do not factor the shared chrome into a helper until
  there is a third site — the duplication is cheap and the lambdas
  have to capture animation state from the enclosing render
  function.

## Settings Card Row Spacing Note (v0.76.9)
- The System Settings modal used hand-picked absolute y-offsets
  (`safetyRect.y + 56`, `+ 74`, `+ 108`, ...) sized for the stock font
  load (`fontSmall_` @ 15pt, `TTF_FontHeight ≈ 18`). On retina / HiDPI
  the loader switches to a 17pt face (`TTF_FontHeight ≈ 21`), and label
  rows started overlapping the button rows directly below them by
  2–4px. Because the buttons call `drawFramedPanel` *after* the label
  was drawn, the panel fill overpainted the descenders — reported as
  "panic text is so low it's cutoff".
- The fix is ergonomic rather than structural: a trio of helpers in
  `render/layout.hpp` — `textLineHeight(font)`, `rowYBelowLabel(labelY,
  font, gap)`, and `rowYBelowLines(startY, font, lines, gap)` — derive
  row Y and multi-line spacing from `TTF_FontHeight` at runtime. They
  are pure functions with a null-safe fallback (returns 18 when font
  is null), so callsites can use them interchangeably with literal
  offsets during incremental migration.
- All settings cards that mixed a single-line label and a control row
  were migrated (SAFETY / TIMECODE, SHOW FLOW, CUE TOOLS, AUDIO, MIDI,
  REMOTE, OSC, NOTES, INTEGRATION, About/RUNTIME, Edge Blending, AOI).
  The inspector (`app_render_inspector.ipp`) and its four primitive
  drawers in `app_cue_mgmt.ipp` were left untouched because the user's
  acceptance criteria explicitly called out that the inspector already
  rendered correctly in scrolling menus and must not regress. The new
  helpers are additive; they don't change any existing behavior at the
  stock font size.
- The four text-drawing primitives (`drawText`, `drawTextSafe`,
  `drawCenteredText`, `drawCenteredTextSafe`) remain the canonical
  way to render text in the app. If a future pass wants to unify
  optical centering across button surfaces, that lives inside those
  primitives — don't introduce a parallel drawer.

## FFprobe Parser Stream Boundary Note (v0.76.8)
- `probeCue()` in `main.cpp` parses ffprobe's `default=noprint_wrappers=1`
  output line-by-line. It requests `stream=codec_type,codec_name,...` and
  expects these fields in either order within a stream (the order varies
  by ffprobe version and container format).
- The original buffering logic stored the most recent `codec_name` as
  `pendingCodecName` and the most recent `codec_type` as `lastCodecType`,
  applying the pair via `tryApplyCodec()` whenever both were known. A
  `pendingApplied` flag prevented double-application.
- **The bug:** it treated codec_name and codec_type as independent fields
  without recognizing that a NEW stream resets both. For an mp4 with the
  audio stream emitted first:
  ```
  codec_name=aac
  codec_type=audio       ← pair applied, pendingApplied=true
  sample_rate=48000
  channels=2
  r_frame_rate=0/0
  codec_name=h264        ← new stream, but lastCodecType still "audio"
  codec_type=video
  ...
  ```
  On `codec_name=h264`, tryApplyCodec ran with `lastCodecType="audio"` and
  saw `cue.audioCodec` already filled, so the "h264" was silently
  discarded. The subsequent `codec_type=video` set `lastCodecType="video"`
  but `pendingApplied` was already true from the audio pair, so
  tryApplyCodec early-returned. Result: `cue.videoCodec` stayed empty,
  triggering the end-of-function audio-only detection.
- **Fix:** when `codec_type` arrives AND `pendingApplied` is already true,
  that codec_type marks a new stream — clear `pendingCodecName` and reset
  `pendingApplied = false` before assigning the new type. Symmetric handling
  on `codec_name` (clear `lastCodecType`). Both within-stream orderings
  continue to work; new-stream boundaries are now detected.
- **Testing:** verified against `H:\Missa X\Missa X - Bachelorette Pt 2
  (1080p).mp4` which emits audio-first. Traced step-by-step through both
  the audio-first case and the conventional video-first case to confirm
  no regression in the normal path.

## Duplicate Cue Allowance Note (v0.76.8)
- `importPaths()` in `app_cue_mgmt.ipp` used to silently skip any path
  that already existed in the deck's cue list. This blocked a legitimate
  operator workflow: using the same asset twice (e.g., as two cues with
  different in/out trim, or a playlist that loops back through an asset).
- Removed the dedup check. Library-level dedup (avoiding re-ingesting the
  same file into the media library) is a separate concern that belongs in
  the media library layer, not the cue list.

## Video Cue Fade Suppression Removal Note (v0.76.7)
- **Context:** this is a follow-on to the v0.76.4 output fade gain fix.
  That release moved per-cue fade ramps from the (dead) `MediaEngine::render()`
  path into the live `renderDeckLayerIntoOutput` path via
  `currentVisualFadeGain()` → bridge texture alpha. It made fades work on
  output for the first time. But it left a stale cue-kind gate in
  `loadCue()` intact, which silently killed fades for video/source cues in
  playlists.
- **The stale gate (now removed):**
  ```cpp
  bool isStillTypeCue = cue && (cue->kind == Image || Pattern || Browser || Composite);
  suppressFadeInForCurrentCue_ = suppressFadeIn && !isStillTypeCue;
  suppressVisualFadeOutForCurrentCue_ =
    cue && !isStillTypeCue && (cueAdvancesWhenFinished(*cue) || Loop);
  ```
  The comment said: "Video/source cues suppress these during auto-advance
  because the crossfade handles the outgoing/incoming visual, avoiding a
  double-fade effect." This was true pre-v0.76.4 — but the "crossfade" it
  was protecting lives inside `MediaEngine::render()`'s transition overlay
  path, which writes to a hidden per-deck `SDL_WINDOW_HIDDEN` that the
  output compositor never reads. The crossfade is invisible on output, so
  there's no double-fade hazard for the suppression to guard against —
  removing it just means the per-cue fade ramp actually runs on video cues.
- **Symptom before fix:** playlist of video+browser cues, same
  `fadeInSeconds`/`fadeOutSeconds` set on both. Browser cue fades correctly
  (isStillTypeCue=true → suppression skipped). Video cue pops in and out
  (suppression active → `visualFadeGainAt` returns 1.0 throughout).
- **Fix surface:**
  - `engine/media_engine.cpp:loadCue` — `suppressFadeInForCurrentCue_`
    honors the caller hint only, no cue-kind override;
    `suppressVisualFadeOutForCurrentCue_` always starts false. Loop
    suppression still re-asserts itself in `handlePlaybackEnd` (correct).
  - `app/app_update.ipp:416` — auto-advance no longer passes
    `suppressIncomingFadeIn=true`; the per-cue fade-in IS the visible
    transition.
- **Audio side effect:** `fadeGainAt()` used by the audio thread respects
  the same suppression flags, so audio also now fades in/out for
  auto-advancing video cues. This is the expected behavior — a cue with a
  visible fade should also have an audible fade. The fade ramp happens
  before `stopDecoderThreads` kills the outgoing audio pipe, so there's no
  click or abrupt termination.
- **If a visible crossfade mechanism is re-introduced in a future release**:
  wire it into the output path (bridge texture or a second bridge layer),
  NOT back into `MediaEngine::render()`. At that point, reconsider whether
  the crossfade should multiply OR replace the per-cue fade ramp. Do not
  resurrect the `isStillTypeCue` gate — use a more targeted mechanism
  (e.g., flag the outgoing frame explicitly during the crossfade window).

## Audio Thread Byte Alignment Note (v0.76.6)
- `MediaEngine::startDecoderThreads` spawns an audio reader thread that pulls
  s16le stereo @ 48 kHz from an FFmpeg pipe via `readSome()`. The previous
  code sized its `scaled` vector at `bytesRead / 2` (elements) and then
  `memcpy`'d `bytesRead` raw bytes in. If `readSome` ever returned an odd
  byte count (possible on EINTR / short read / EOF boundary), this wrote one
  byte past the end of the vector's backing storage — classic off-by-one,
  invisible in release but UB under sanitizers.
- Fix: mask `bytesRead` to an even count (`bytesRead & ~size_t{1}`) before
  sizing and copying. Any stray trailing byte is dropped; the pipe will
  return it on the next read, so sample alignment is preserved across the
  boundary. Sub-2-byte reads are skipped via `continue` rather than breaking
  the loop.
- This is not a rewrite of the audio path — the sample interleave, volume
  ramp, tap callback, and SDL queue are unchanged. Purely a boundary fix.

## Capture Backend Factory Platform Guard Note (v0.76.6)
- `platform/capture_backend.cpp` exposes three factories:
  `createWindowCaptureBackend()`, `createCameraCaptureBackend()`, and
  `createAppTextureCaptureBackend()`. The window factory already had a
  `_WIN32` guard that returns `WindowsGdigrabCaptureBackend` on Windows.
- Camera factory did not — it unconditionally returned
  `LinuxCameraCaptureBackend`, which on Windows produced a plan with
  `supported=false` and `backendId="v4l2"`. This contradicts the catalog,
  which advertises Windows camera capture as `mediafoundation`
  ("backend scaffold only").
- Added a small `UnsupportedCameraCaptureBackend` scaffold class (guarded
  `#if !defined(__linux__)`) that reports the correct platform backend id
  (`mediafoundation` on Windows, `avfoundation` on macOS) and
  `reasonUnavailable = "camera capture backend scaffold only"`. Factory is
  now `__linux__`-guarded to pick the right implementation.
- When a real Windows or macOS camera backend lands (Media Foundation,
  AVFoundation), replace the scaffold with the real class; the factory guard
  is the single switch point.

## Output Fade Gain Fix Note (v0.76.4)
- `MediaEngine::render()` applies fade-in/out via `visualFadeGainAt` to a hidden
  per-deck SDL window (DeckRuntime::outputWindow, SDL_WINDOW_HIDDEN). This window
  is never read by the output compositor — `MediaEngine::render()` is effectively
  dead code for output purposes.
- The actual output path is `renderDeckLayerIntoOutput` (app_render_output.ipp):
  reads `currentFrame()` pixels, uploads to a bridge texture, applies alpha via
  `SDL_SetTextureAlphaMod`. Before this fix, only `playlistOpacity` (deck-level
  opacity) was applied — the per-cue fade ramp was completely ignored.
- Fix: added public `currentVisualFadeGain() const` to `MediaEngine` (wraps the
  private `visualFadeGainAt(position())`). `renderDeckLayerIntoOutput` now sets
  `alpha = deckOpacity × fadeGain × 255`.
- `suppressVisualFadeOutForCurrentCue_` is respected by `visualFadeGainAt` —
  auto-advancing cues (which use crossfade for the outgoing visual) correctly
  return 1.0 from `currentVisualFadeGain()` so the outgoing frame is not double-faded.

## Browser Cue Duration Fix Note
- `startBrowserFrameMode` was unconditionally setting `duration_ = 0.0` on every first
  frame arrival from the browser capture pipeline.
- `loadCue` calls `initStillTimer` (which sets `duration_` from `stillDurationSeconds`)
  before starting browser capture, but `startBrowserFrameMode` overwrote it.
- Fix: `startBrowserFrameMode` now reads `activeCue_->stillDurationSeconds` and restores
  the duration if it is > 0 — otherwise leaves `duration_` at 0 (infinite still).
- This is why fade-out and auto-advance were silently broken for all browser cues.

## Area of Interest (AOI) Output Crop Note
- Per-output fractional edge crop, stored as `aoiLeft/Right/Top/Bottom` float fields
  on `OutputTarget` (0 = no crop, 1 = full crop from that edge; max 0.95 per edge).
- Applied in `presentOutputCompositorToWindow` (app_render_output.ipp) when computing
  the SDL source rect for the compositor→window blit.
- When AOI is active, canvas view pan (`canvasViewX/Y`) is intentionally skipped —
  the two modes are mutually exclusive to avoid confusing double-offset behavior.
- NDI and DeckLink outputs read the compositor texture via `SDL_RenderReadPixels` using
  a separate `captureRect` — AOI does NOT apply to those paths currently; a future
  improvement would composite to an intermediate scaled texture first.
- Settings panel: 4 dec/inc controls at 5% step + RESET button. Panel header highlights
  when any AOI edge is active.
- Serialized as fields 28–31 of the OutputTarget record (guard: `fields.size() >= 32`).

## Dependency Prompt (v0.76.14)
- Three optional Windows backends — NDI runtime, Blackmagic Desktop Video,
  Microsoft WebView2 — are not redistributable but Deckboy can detect them
  at runtime and route the operator to the official vendor download page.
- Detection lives next to the modal in `app/app_overlays.ipp`:
  - `ndiRuntimeAvailable()` calls `NdiApi::ensureLoaded()` (clears
    `attempted` on failure so a fresh attempt after installing works).
  - `deckLinkRuntimeAvailable()` returns true iff `DeckLinkOutput::listDevices()`
    finds at least one device. Empty means either Desktop Video missing or
    no card connected; the prompt copy addresses both.
  - `webView2RuntimeAvailable()` dynamically loads `WebView2Loader.dll` and
    calls `GetAvailableCoreWebView2BrowserVersionString` — the official
    runtime-availability check. Frees the COM-allocated version string
    via `CoTaskMemFree` (objbase.h, included from `main.cpp`).
- Prompt state is `App::depPrompt_` (a `DependencyPromptState` struct).
  `showDependencyPrompt()` populates it; `renderDependencyPrompt()` draws
  the modal in `renderControlWindow()`'s overlay stack; click handling
  lives in `processMouseDown` next to the existing confirmQuit_ block.
  CTA opens the vendor URL via `deckboy::platform::openExternalUrl()`
  (`core/system_browser.hpp` — ShellExecuteW on Windows, xdg-open / open
  elsewhere). Either button dismisses the prompt.
- Hook points must check at the UI-action site, NOT at the underlying
  setter, so a project file opened on a machine without the dep can still
  load with the flag respected (the setter then silently degrades).
  Current hooks: NDI toggle in settings + the `N` hotkey; DeckLink toggle
  in settings; `addBrowserCue` (Windows-only branch).

## UI Scale (v0.76.14, layout-chrome pass v0.76.15)
- `Project::uiScale` (double, default 1.0, clamped to [0.75, 3.0]) is the
  operator-facing scale multiplier. Persisted in the show file as
  `ui_scale` so a project authored on a 4K monitor keeps its scale.
- Fonts: `loadFonts(scale)` reopens all six TTF faces at `base × scale ×
  platformNudge` point sizes (Windows keeps the historical 0.9 nudge for
  its DPI baseline). Six base sizes: large 32, base 21, small 17, mono 18,
  pixel 24, pixel-small 12.
- Layout chrome: the `kLayout*` identifiers in `core/constants.hpp` are
  now mutable `inline int` globals (C++17), seeded from immutable
  `*Base` constexprs and rewritten by `App::rebuildLayoutMetrics(scale)`.
  `applyUiScale()` calls both `loadFonts` and `rebuildLayoutMetrics` so
  the next frame draws against a consistent set of metrics. Every prior
  callsite that read `kLayoutHeaderHeight` etc. picks up the scaled
  value without edits — the rename strategy is in-place rather than
  per-callsite.
- Default arguments in `render/layout.hpp` (VerticalLayout/HorizontalLayout/
  GridLayout/UITable) read `kLayoutPanelGap` / `kLayoutButtonGap` at call
  time, so they auto-scale too. Callers that pass explicit gaps still
  need to feed scaled values themselves.
- Pocket 3 / Touch preset is a single pill that bundles `uiScale = 2.0`
  AND `interactionMode = "touch"` (see Touch Mode note). The "active"
  state on the pill requires both fields to match the preset, so a
  half-applied state (e.g. scale 2.0 but mouse mode) renders as inactive.

## Touch Interaction Mode (v0.76.15)
- `Project::interactionMode` (`"mouse"` default, `"touch"` alternative)
  persisted as `interaction_mode` in the show file.
- `App::inTouchMode()` is the only accessor render code should consult.
  Three suppression sites today:
  - playlist splitter hover (`app_render_control.ipp`)
  - inspector splitter hover (`app_render_main.ipp`)
  - context menu item hover (`app_ui_widgets.ipp`)
- The Pocket 3 preset sets `interactionMode = "touch"` and `uiScale = 2.0`
  in lockstep. Add new touch-only ergonomic behavior here — never split
  the touch surface across a second flag.
- Right-click menus, drag-resize splitters, and other mouse-only gestures
  are intentionally not yet rewired for touch. The preset is an
  ergonomic improvement, not a full touch-input redesign — that's
  product work.

## Splash Mascot Swap (v0.76.14)
- `Project::splashCharacter` ("deckbot" default | "deckgirl") names the
  splash art. `pickSplashCandidates(character)` in `main.cpp` returns a
  fallback chain: `deckboy_splash_<name>.{mp4,gif,png}` then the legacy v2
  filename, then the v074 plain art. `refreshSplashAsset()` re-resolves
  and reloads the texture.
- Both assets live in `data/ui/deckboy_ui_pack_v3/splash/`. To add a third
  character: drop `deckboy_splash_<name>.png` into that folder and either
  extend the toggle in settings or set the value via a saved project.
- For an animated mascot: drop `deckboy_splash_<name>.mp4` (or `.gif`)
  next to the PNG and teach `UiImageAsset` to render video frames. The
  pickSplashCandidates chain already tries those extensions first, so the
  upgrade lands without touching the splash overlay code path.

## GPU Hardware Decode + NV12 Upload Path (v0.76.13)
- `startDecoderThreads` passes `-hwaccel auto` before `-i`. FFmpeg picks the
  best hardware decoder (DXVA2/D3D11VA on Windows, NVDEC/VAAPI/VDPAU on
  Linux/macOS) and inserts a `hwdownload + format=...` filter automatically
  when the downstream filter chain needs CPU frames. Falls back to software
  decode silently when no hardware backend is available.
- **Pipe pixel format is per-cue.** A cue with `chromaKeyEnabled` or active
  color controls (`brightness`/`contrast`/`saturation`/`hueShift` ≠ unity)
  decodes as `rawvideo rgba`, because the CPU effects path
  (`applyCueVisualEffectsToPixels`) mutates interleaved RGBA bytes. Every
  other cue decodes as `rawvideo nv12` — planar Y plane followed by
  interleaved UV, 12 bpp instead of 32, cutting pipe + system-RAM traffic
  by ~62%. Decision is frozen at TAKE time; the engine reads the cue once
  in `startDecoderThreads`.
- **Frame shape is tagged.** `DecodedFrame::format` carries the layout
  (`FramePixelFormat::RGBA32` or `NV12`). All six SDL_Texture upload sites
  branch on it:
  - `MediaEngine::uploadFrame` — main deck texture
  - `renderDeckLayerIntoOutput` — per-output layer bridge
  - `renderOverlayFrameIntoOutput` — per-overlay bridge
  - `uploadPreviewCueTexture` — preview-cue texture
  - `update()` focused-engine block — control-window preview texture
  Each tracks `Uint32` cached SDL pixel format alongside cached
  width/height and recreates the texture when format OR dimensions
  change. `syncFrameTexture()` in `render/texture_helpers.hpp` is the
  shared helper for the create/upload pair; callers that need
  CPU-effects pre-processing (the bridge and main-deck paths) inline the
  branch instead so they can apply effects only on the RGBA arm.
- **NV12 requires even dimensions** because the chroma plane is at half
  resolution. `startDecoderThreads` rounds `decodeW`/`decodeH` down to
  even before building the scale filter; `frameBufferSize()` does the
  same trim when computing the byte count. Sources virtually always have
  even dimensions, but the trim avoids a partial-UV-row hazard.
- **CPU scaler is fast_bilinear**, not bicubic. Bicubic costs ~3–4× more
  per frame and is indistinguishable on moving video at deck-output
  sizes. Still-image and thumbnail paths (`loadStillFrame`,
  `decodeSingleFrame`) keep `flags=neighbor` — that decision is
  deliberate for pixel-art and diagram stills.
- **Live effect-toggle limitation.** If the operator enables chroma key
  or color controls on a cue already decoded as NV12, the effect will
  not appear visually until the next TAKE — the upload path takes the
  NV12 branch and skips the RGBA effects scratch. If you need
  live-toggleable effects, enable at least one effect parameter on the
  cue before TAKE so the decoder picks RGBA up front. A real fix would
  push effects to a fragment shader so they run on the GPU regardless
  of pipe format; that's left as future work.

## TSL/Tally Protocol Note
- UDP listener on port 5800 (configurable). Supports TSL 3.1 (20-byte packets) and
  TSL 5.0 (variable-length). Sends tally state on every deck active-status change.
- `tslTallyEnabled` / `tslTallyPort` added to `Project`. Tally thread started/stopped
  alongside other integration adapters in `applyIntegrationRoute`.
- Each active deck maps to a TSL address (deck 0 → address 1, etc.). PGM bit set when
  deck is active (playing/paused with active cue); PVW bit set when deck is the
  currently focused/selected deck in standby.

## SRT Input Source Note
- `CueKind::SrtStream` is a dedicated cue kind for live stream input (srt://, rtmp://,
  rtsp://, udp://). The cue path stores the full stream URL.
- Added via SOURCE menu → "Stream Cue (SRT / RTMP / RTSP)" → URL prompt.
- `startDecoderThreads` detects `cue.kind == CueKind::SrtStream` and skips ffprobe and
  the `-ss` seek flag. FFmpeg receives the URL directly as `-i URL`.
- The ffmpeg build shipped with Deckboy must be compiled with `--enable-libsrt`.
- Inspector shows a URL editor row (edit path via `QuickAction::EditBrowserUrl`).

## NDI Receive Input Note
- `CueKind::NdiSource` is a dedicated cue kind for NDI receive input.
- `cue.path` stores `ndi://SOURCE_NAME`. The engine strips the prefix and passes the
  name to ffmpeg as `-f libndi_newtek -i SOURCE_NAME`.
- Added via SOURCE menu → "NDI Source Cue" → source name prompt.
- `startDecoderThreads` detects `cue.kind == CueKind::NdiSource`: sets `isNdiSource=true`,
  `isLiveStream=true`; skips ffprobe and `-ss`.
- Inspector shows a source name editor row (edit via `QuickAction::EditBrowserUrl`).
- Windows DLL candidates for NDI SDK in `ndi_api.hpp` / `ndi_trigger_api.hpp`.

## Audio Buffer Size Tuning Note
- `Project::audioBufferSamples` (256/512/1024/2048, default 1024) controls the SDL
  audio buffer size passed to `SDL_OpenAudioDevice` in `openMainAudioDevice` and the
  UI audio device open call.
- Smaller buffers reduce audio-to-video sync latency. Larger buffers improve stability
  on slower/loaded systems.
- On Windows, SDL2 uses WASAPI in shared mode. There is no mechanism to switch to
  WASAPI exclusive mode or ASIO via SDL2. True ASIO support would require PortAudio
  with the Steinberg ASIO SDK — deferred pending SDK licensing review.
- Buffer size changes take effect on the next app restart (audio devices are opened
  during init, not on-the-fly).

## Startup Project Restore Note
- Startup no longer assumes `data/default.deckboy` is synonymous with “previous
  show.”
- Deckboy now remembers the actual last opened/saved project path in
  `data/last_project.txt` and uses that to seed the startup dialog/load path on
  the next launch.

## Saved Show Path Repair Note
- The current `data/default.deckboy` file had two collapsed Windows media paths
  (`G:...`) that prevented the previous-show flow from finding its clips.
- Those saved cue paths were repaired directly in the project file to valid
  `G:\\...` paths.
- The more aggressive auto-repair-at-startup experiment was removed after it
  proved too risky for startup stability.

## Cue Inspector Text Clip Note
- The inspector scroll viewport clip alone was not enough; text could still
  render outside its own label/value rect when a row was only partially visible.
- `drawTextSafe()` and `drawCenteredTextSafe()` now intersect the active
  renderer clip with the real control bounds before drawing, which keeps
  scrolled parameter text visually locked inside its box without clipping text
  to an overly shrunken inner rect.
- The shared inspector row renderer now also uses slightly taller rows, wider
  horizontal gaps, and slightly roomier internal text spacing so the cue
  inspector feels cleaner without squeezing labels and values into unreadable
  widths.

## Timeline Scrub Note
- The old timeline input path only sought once on mouse-down.
- There is now a dedicated `timelineScrubActive_` state so left-button hold +
  drag keeps sending clamped timeline seeks on mouse motion until button-up.
- The click path and drag path now share the same timeline-fraction helper, so
  trim-relative timeline views and normal full-duration views seek consistently.

## Cue Row Readability Note
- Playlist / overlay cue rows use the shared `kRowHeight`, which is now a bit
  taller to give the three-line row layout more breathing room.
- The cue name line in `renderCueRow()` now uses the smaller sans face instead
  of the larger base face, which gives long cue names more usable width before
  ellipsizing.

## Windows Live Icon Note
- The Deckboy executable and the live SDL windows are not the same icon path on
  Windows.
- Embedding an `.ico` in the executable helps Explorer/shortcuts, but the
  actual running control/output windows still need explicit `WM_SETICON`
  handling if we want the taskbar/titlebar identity to stay reliable.
- `applyDeckboyWindowIcon()` now loads `IDI_DECKBOY_APP_ICON` from the current
  module and applies both big and small icons to the control window, monitors
  window, and output windows.

## Program Monitor Layout Note
- The old right-side `NEXT` preview panel has been removed from the main
  control-window monitor area.
- `app_update.ipp` now clears/stops the corresponding preview runtime instead
  of continuing to decode a hidden next-cue monitor surface.
- Program-monitor telemetry badges now compute against the remaining header
  width after reserving space for the `WARP` button and title label, so they
  shrink/drop cleanly instead of overlapping the header controls.

## Async Media Task Note
- The main update loop now treats media-probe and waveform futures as fallible
  background work instead of assuming `future.get()` can never throw.
- This is important for operator robustness: a bad probe/decode should degrade
  to a failed asset analysis state, not terminate the whole app.
- Windows waveform analysis now mirrors the Unix code path by draining ffmpeg
  output with `_read()`, which was previously skipped under `_WIN32`.

## Seek Frame Hold Note
- `MediaEngine::seek()` now defaults `clearVisualFrame` to `false`.
- The main reason is operator-facing transport behavior: jumps, scrubs, and
  quick seek actions should keep the last good frame visible until the decoder
  publishes replacement pixels.
- This prevents preview/output flashes to black during routine navigation while
  still leaving `seek(..., true)` available for any path that truly wants a
  hard visual clear.

## Output Display Switch Note
- `applyOutputDisplaySelection()` now treats fullscreen exit, geometry update,
  and fullscreen re-entry as separate steps.
- Manual output-display changes (`setOutputDisplayIndex`,
  `cycleOutputDisplay`, and `sizeFocusedOutputToSelectedDisplay`) now route
  through one fullscreen restore path instead of two.
- Geometry updates only occur after SDL reports the output window is no longer
  fullscreen, which avoids the worst multi-monitor thrash during display moves.
- For enabled window outputs, display reassignment now prefers recreating that
  one output runtime on the target display, which is more robust on Windows
  than asking the same fullscreen window to migrate in place.
- That recreation is now queued onto the next update tick instead of happening
  directly inside the display-picker action, so SDL gets one deliberate teardown
  and rebuild instead of overlapping the user's click with recovery/fullscreen
  churn.
- `destroyOutputRuntime()` now clears the pending display-transition flags and
  timers so a rebuilt output starts from a clean state.

## Windows Launch Note
- The Windows target now sets `WIN32_EXECUTABLE`, so `Deckboy.exe` launches as
  a GUI app rather than spawning a blank console window.
- `native/main.cpp` now shares startup through `runDeckboyMain()` and adds a
  Windows `WinMain` wrapper that reconstructs UTF-8 argv values with
  `CommandLineToArgvW`, keeping the CLI code paths aligned with the normal app
  launch path.

## Keyboard Focus Note
- `processEvents()` now forwards `SDL_KEYDOWN` into `handleKeyDown()` only when
  the event came from the main control window, plus `Esc` from output windows
  for fullscreen safety handling.
- This avoids transport/editor shortcuts firing from secondary Deckboy windows,
  which was especially confusing when the control window was not the active
  place receiving text input.
- `openInlineTextEditor()` now raises the control window before
  `SDL_StartTextInput()` so token-entry tools such as `Ctrl+G` behave more
  predictably on multi-window setups.

## Final Naming Notes
- The internal CMake target remains `deckboy-native` for now, but the release-
  facing output name is now `Deckboy`.
- This keeps existing target references stable in CMake while making the built
  app, workflow artifacts, and user-facing docs match the real product name.
- Because `v0.75.0` was already tagged before this rename landed, the repo
  version advances to `0.75.1` for the first release that ships the corrected
  final output name.
- Startup dialog, splash overlay, and About/settings branding now derive their
  visible version line from generated version metadata instead of stale
  hardcoded `0.74` constants.

## Version Flow Notes
- `VERSION` is now the single source of truth for Deckboy's SemVer version.
- CMake reads `VERSION`, parses the numeric core into `project(... VERSION ...)`,
  and generates `deckboy_version.hpp` so native code can print the same version.
- `Deckboy --version` is now the quickest sanity check when a local build
  or GitHub artifact feels ambiguous.
- GitHub Actions now guard `v*` tags against `VERSION` before running
  Linux/macOS/Windows build jobs, so a mistyped tag cannot silently create a
  mismatched release candidate build.

## Deckboy 0.60 Cleanup + Portability Audit
- Shared runtime fix note:
  - timeline strip EOF sampling and strip publish behavior are now safer in the
    shared native path, which fixes the black-final-tile issue seen on long
    clips
  - animated engineering patterns now republish with a fresh frame serial, so
    output compositors no longer hold stale still frames
  - crosshatch and checkerboard pattern loops now wrap on seam-safe phase math
- Browser cue backend note:
  - `native/platform/browser.*` now owns browser session lifecycle and phased
    startup state instead of keeping the Linux external-browser runtime smeared
    through `native/app/app_output_mgmt.ipp`
  - current behavior is still an external-browser Linux backend; native webview
    or more owned rendering remains future work
- `deckboy-0.60` is now in an audit / cleanup phase rather than a keep-adding-
  features phase.
- **Audit roadmap:** see `docs/AUDIT_ROADMAP.md` for the full task map covering
  remaining optimization and cleanup work.
- **Companion thread safety (fixed):** `companionClientsMutex_` now protects
  `companionClients_` + `companionClientBuffers_` in both the network thread
  (`companionLoop`) and main-thread shutdown (`stopCompanionControl`).
- **Pre-converted palette (migrated + fixed):** `Palette pal` struct holds
  `SDL_Color` versions of all 10 theme colors + `scanlineAlpha`. Rebuilt on
  theme load via `rebuildPalette()`. All ~1247 `colorFromRgba(kConstant)` calls
  migrated to `pal.*` members. (Bug fix: `rebuildPalette()` was a no-op — now
  converts from kConstants.)
- **Beveled panel rendering:** `drawUIPanel()` and `Primitives::drawFramedPanel()`
  draw beveled edges. Accent-vs-fill luma comparison determines raised/inset.
  No signature or call-site changes.
- **Scanline overlay:** Procedural 1×4 texture rendered before each present.
  `scanline_alpha` theme key (0=off, default 18).
- **Theme system:** 7 themes in `data/themes/`: gameboy (default), dark, pocket,
  color, advance, sp. Set via `DECKBOY_THEME=name` env var. Users create custom
  themes by adding `data/themes/mytheme/theme.txt`.
- **Inspector helpers (shared):** `InspectorCtx` struct + 15 `insp*()` member
  functions (~line 34529) provide shared implementations for both docked and
  floating cue inspector paths. Both render paths use thin wrapper lambdas.
  `fmtFloat()`/`fmtPercent()` use `snprintf` (zero heap alloc).
- Immediate operational priority:
  - remove the last active modal text-entry flows from the live UI
  - park half-finished overlay/scene authoring surfaces until the core app is
    steadier
- Active UI rule for this phase:
  - prefer `openInlineTextEditor(...)` everywhere the operator is already inside
    Deckboy
  - do not reintroduce ad-hoc `zenity` / modal prompt text entry for normal
    show-control editing
- The old deck-level auto-advance flag is now treated as legacy:
  - cue endings are per-cue only
  - save/load still tolerates old `auto_advance` fields for compatibility
  - do not build new UI/state on top of `Deck::autoAdvance`
- Lower Third / PIP / Composite current stance:
  - existing cues still load, inspect, save, and render
  - new cue creation from the bottom bar / hotkeys / remote add commands is
    intentionally parked for now
  - this reduces UI clutter while keeping forward-compatibility work on the
    branch
- Portability audit conclusion:
  - no major product or runtime-ownership rewrite is required to make
    portability realistic
  - the real blockers are backend/runtime seams:
    - Unix-first child-process execution (`fork/execvp`, FIFO-based stream feed)
    - Linux-only browser/source capture (`Xvfb`, `x11grab`, `v4l2`)
    - Windows/macOS backend completion for capture, stream egress, and runtime
      loading

## Phase 4 Inline Editing + Floating Panels
- Panel presentation/visibility is now a real persisted part of
  `UiWorkspaceState`, not just a computed summary.
- The main helpers added/extended in `native/main.cpp`:
  - `panelIsVisible(...)`
  - `panelPresentation(...)`
  - `panelIsDockedVisible(...)`
  - `setPanelVisible(...)`
  - `setPanelPresentation(...)`
  - `cyclePanelWorkspaceMode(...)`
  - `panelHasLocalFocus(...)`
  - `panelFocusBadge(...)`
- `Deckboy Panels` is a secondary floating workspace window used for popped-out
  singleton operational panels. Current behavior:
  - renders floating `Program / Transport` as a live summary panel
  - renders floating `Cue Inspector` as a live summary panel
  - renders floating `Routing`, `Master Scene`, and `Output Panels`
  - `DOCK` returns a floating panel to the main control workspace
- Important limitation:
  - floating `Program / Transport` and floating `Cue Inspector` are mirrored
    summaries in this pass, not full independent interactive clones of the main
    control workspace render path
  - this avoids renderer-specific texture duplication bugs while keeping the
    pop-out workflow real and safe
- Inline operational editing should prefer `openInlineTextEditor(...)` over
  `pickTextInput(...)` whenever the operator is already in the live control UI.

## Phase 3 Workflow Polish
- Shared dropdown scaffolding is now the standard selector path for operational
  UI selection surfaces. Active dropdown-based settings selectors include:
  - audio output device
  - output display
  - stream protocol
  - mirror source
- The old blocking list-picker path was removed from active UI flows.
- `nextCueIndexForDeck(...)` is now the canonical UI helper for `what is next`.
  It is used by:
  - `renderMainPanel()` summaries
  - `renderCueRow()` deck playlist rows
- Text-safe cleanup in this pass focused on the highest-density views:
  - `renderDecksPanel()`
  - `renderDeckSidebar()`
  - `renderMainPanel()`
  - `renderSettingsModal()`
  - `renderDropdownPopover()`
- The default control workspace was rebalanced to favor the center
  `Program / Transport` region over the right-side operational column.

## Phase 2 Operational Panel Split
- The control workspace is no longer treated as one render block conceptually.
- Current operational panel functions in `native/main.cpp`:
  - `renderPlaylistColumn()` -> docked `Deck Playlist`
  - `renderDecksPanel()` -> floating/repeating `Deck Playlist` views
  - `renderMainPanel()` -> `Program / Transport` + `Cue Inspector`
  - `renderOutputPanelsPanel()` -> repeating `Output` panels
  - `renderRoutingMatrixPanel()` -> singleton `Routing` panel
  - `renderDeckSidebar()` -> singleton `Master Scene` panel
- Shared panel chrome helper:
  - `drawOperationalPanel(...)`
- Shared rendered-frame sync helper:
  - `recordRenderedPanelFrame(...)`
- Current Phase 2 limitation:
  - only `Deck Playlist` has a true floating window surface today (`Decks window`)
  - `Program / Transport`, `Cue Inspector`, `Routing`, `Master Scene`, and `Output`
    are modular/persisted panels but still render docked inside the control window
  - panel persistence is ready for future pop-out/docking work, but that behavior
    is not fully implemented yet
- Scrollable operational regions added in Phase 2:
  - `outputPanelsViewportRect_`
  - `routingMatrixViewportRect_`
  - wheel scrolling is state-driven in `processEvents()`

## Layout System (March 2026 cleanup pass)
- Grid/layout primitives live in `native/main.cpp` near the shared rect helpers:
  - `VerticalLayout`
  - `HorizontalLayout`
  - `GridLayout`
  - `UITable`
- Shared layout constants also live there:
  - `kLayoutSpacingUnit`
  - `kLayoutPanelPadding`
  - `kLayoutPanelGap`
  - `kLayoutPanelBorder`
  - `kLayoutHeaderHeight`
  - `kLayoutBottomBarHeight`
  - `kLayoutButtonHeight`
- Shared drawing helpers used by the live control window:
  - `drawTextSafe(...)`
  - `drawCenteredTextSafe(...)`
  - `drawUIPanel(...)`
  - `drawUIButton(...)`
  - `drawUIDropdown(...)`

### Inspector Section Scopes
- Cue inspector implementations live in shared `insp*()` member functions
  (~line 34529) parameterized by `InspectorCtx` struct. Both docked
  (`renderMainPanel()`) and floating inspector paths use thin wrapper lambdas.
- Key shared functions: `inspDrawQuickRow`, `inspDrawMessageRow`,
  `inspDrawActionRow`, `inspDrawEditableRow`, `inspDrawStatusRow`,
  `inspDrawKeyColorRow`, `inspDrawGeometryRows`, `inspDrawColorRows`,
  `inspDrawKeyRows`, `inspBeginSection`, `inspFinishSection`.
- Format helpers: `fmtFloat()`, `fmtPercent()`, `fmtScaleMode()` (static,
  snprintf-based — no heap alloc).
- `InspectorCtx` fields: `ctrl`, `ctrlW`, `inset`, `rowH`, `rowStep`,
  `sectionHeaderH`, `sectionGap`, `headerFont`, `valueFont`, `labelFont`,
  `ellipsize`.
- If you add a new inspector group, follow the same pattern:
  1. begin section (`inspBeginSection`)
  2. render rows (use `inspDraw*` helpers or thin wrappers)
  3. finish section with final body Y (`inspFinishSection`).
- Current section set used in the live inspector:
  - `PLAYBACK`
  - `METADATA`
  - `GEOMETRY`
  - `KEY`
  - `ROUTING`

When adjusting control-window layout, change these helpers/constants first
instead of reintroducing local pixel offsets inside render functions.

## Layout Component Map (native)
- Main control layout entry: `native/main.cpp` -> `renderControlWindow()`.
- Global header + workspace/focus strip: `renderControlWindow()`.
- Deck Playlist panel + cue rows: `renderPlaylistColumn()` and `renderCueRow()`.
- Program / Transport panel: `renderMainPanel()` (`Program monitor`, `Preview monitor`, `STACK VIEW`, timeline, summaries).
- Cue Inspector panel: `renderMainPanel()` (section helpers + row helpers).
- Master Scene panel: `renderDeckSidebar()`.
- Output panels: `renderOutputPanelsPanel()`.
- Routing Matrix panel: `renderRoutingMatrixPanel()`.
- Preferences modal remains in `renderSettingsModal()`.
- Splash overlay and startup dialog: `renderSplashOverlay()` and `renderStartupDialog()`.

## Terminology Policy
- Operator-facing UI/docs should use:
  - `Master Scene`
  - `Decks window`
  - `Window Output` / `Stream Output` / `NDI Output`
  - `Window Source` / `Camera Source` / `Syphon/Spout Source`
  - `Lower Third`
- Compatibility aliases stay in place unless there is an explicit migration plan:
  - `GROUP` / `SCENE` command aliases for Master Scene control
  - `.deckboy`, `DECKBOY_*`, `/deckboy/*`, and `deckboy-native`

## Workspace Foundation
- Runtime panel/workspace scaffolding lives in `native/main.cpp` inside `App`:
  - `UiPanelCategory`
  - `UiPanelPresentation`
  - `UiPanelKind`
  - `UiPanelKey`
  - `UiPanelDefinition`
  - `UiPanelState`
  - `UiPanelManager`
  - `UiWorkspaceState`
  - `UiFocusState`
- Sync/persistence helpers:
  - `syncUiWorkspaceState()`
  - `uiWorkspaceSummaryLine()`
  - `uiFocusSummaryLine()`
  - `saveUiWorkspaceNow()`
  - `loadUiWorkspaceFromDisk()`
  - `applyUiWorkspaceState()`
  - `flushDirtyUiWorkspace()`
- Current mapping is intentionally conservative:
  - singleton modules: Program / Transport, Preview, Cue Inspector, Routing, Master Scene, Preferences
  - repeating modules: Deck Playlist, Output
- Workspace persistence file:
  - `data/deckboy.workspace`
  - tab-delimited, same escape rules as `.deckboy`
  - separate from show files on purpose so Phase 1 does not mutate project serialization
- Persisted in Phase 1:
  - panel visibility
  - panel presentation
  - panel frames
  - control window frame
  - Decks window frame
  - focused panel
  - focused Deck / Output / Cue
  - Decks window manual-open state
  - Master Scene sidebar visible/expanded state
  - last settings tab
- Not implemented yet:
  - docking
  - multiple named workspaces
  - real pop-out singleton panels
  - restoring modal panels open on launch
- Selector pattern for future migrations:
  - `DropdownState`
  - `openDropdown(...)`
  - `renderDropdownPopover()`
  - current proof path: bottom-bar Pattern selector / cue Pattern selector

## DMG Palette Tuning
Palette constants live in `native/core/constants.hpp`:
- `kScreenDeepColor` (`#0f380f`)
- `kScreenDarkColor` (`#306230`)
- `kScreenMidColor` (`#8bac0f`)
- `kScreenLightColor` (`#9bbc0f`)

For readability tuning, prefer changing only these constants first so all framed panels/text inherit consistently.

## Adding Cue-Type Icons
Cue list type tokens are defined in `renderCueRow()` (`typeIcon` switch on `CueKind`).
- Update that switch to add or adjust tokens.
- Keep tokens short (3-4 chars) so fixed columns remain stable.
- If adding a new `CueKind`, update both:
  - `native/core/types.hpp` (`enum class CueKind`)
  - `renderCueRow()` type switch.

## Routing Table Wiring
Video Outputs routing rows use per-deck action ranges in `native/main.cpp`:
- `kSettingsActionRoutingTableOutputPrevBase`
- `kSettingsActionRoutingTableOutputNextBase`
- `kSettingsActionRoutingTableLayerDecBase`
- `kSettingsActionRoutingTableLayerIncBase`
- `kSettingsActionRoutingTableAssignToggleBase`

Click handling lives in `handleSettingsClick()`.

## Warp Mode Implementation
- Deck warp state now includes `Deck.warpMode` (`linear` | `perspective`) in `native/core/types.hpp`.
- Normalize/save/load wiring lives in:
  - `normalizeWarpMode(...)`
  - `saveProject(...)` / `loadProject(...)` deck row handling in `native/main.cpp`.
- UI control lives in Video Outputs -> Advanced row:
  - action id `kSettingsActionOutputWarpModeCycle`
  - handled in `handleSettingsClick()`.
- Command control lives in `handleRemoteCommand(...)`:
  - `VIDEO WARP MODE LINEAR|PERSPECTIVE|NEXT|PREV`
  - direct aliases: `VIDEO WARP LINEAR|PERSPECTIVE`.
- Render behavior:
  - `linear`: existing quad geometry path
  - `perspective`: tessellated projective UV mapping via `renderPerspectiveWarp(...)`.
  - Mesh density is controlled by `kCols` / `kRows` inside `renderPerspectiveWarp(...)`.

## Portability Backends
- Capture backend interfaces now live in:
  - `native/platform/capture_backend.hpp/.cpp`
  - Catalog API: `createCaptureBackendCatalog()`
  - Runtime planning API: `planSourceCapture(const SourceCaptureRequest&)`
- `MediaEngine::buildSourceCaptureArgs(...)` now delegates source cue FFmpeg arg
  planning to `planSourceCapture(...)` (Linux backends active, other OSes stubbed).
- Output backend interfaces now live in:
  - `native/platform/output_backend.hpp/.cpp`
  - Catalog API: `createOutputBackendCatalog()`
  - Route planning API: `planOutputBackendRoute(const OutputBackendRouteRequest&)`
- Runtime egress dispatch now uses backend route planning in `renderOutputWindow()`:
  - stream send is gated by `route.streamSupported`
  - NDI send is gated by `route.ndiSupported`
  - stream runtime is stopped automatically when stream route is not available.
- `--self-check` prints backend introspection lines:
  - `capture-plan-defaults: ...`
  - `output-route-defaults: ...`
  - `integration-route-defaults: ...`
- Top-level CMake now prefers exported `SDL2` / `SDL2_ttf` config packages,
  then falls back to pkg-config/manual lookup. macOS framework feature gates
  use `deckboy_target_link_frameworks(...)`.
- `native/core/paths.cpp` now resolves executable paths on Linux/macOS/Windows
  and expands sans/mono font lookup to macOS + Windows system font locations.
- `native/core/subprocess.*` now provides a unified `spawnProcess()` entry point
  with `SpawnOptions` (StdioMode for stdin/stdout/stderr, detached flag). Legacy
  wrappers `spawnPipeProcess()` / `spawnDetachedProcess()` / `readAllText()` are
  thin forwards so existing call sites need no changes. The old inline
  `spawnDetachedProcess()` definition was removed from `native/main.cpp`.
  Windows builds stub all paths safely; macOS builds do not hard-require
  `MSG_NOSIGNAL` on socket sends.

## Integration Adapter Foundation
- Integration backend planning APIs now live in:
  - `native/platform/integration_backend.hpp/.cpp`
  - Catalog API: `createIntegrationBackendCatalog()`
  - Route planning API: `planIntegrationBackendRoute(const IntegrationBackendRouteRequest&)`
- Network tab integration controls are rendered in `renderSettingsModal()`
  (`settingsTab_ == 2`, `INTEGRATION ADAPTERS` block).
- Actions are handled in `handleSettingsClick()`:
  - `kSettingsActionIntegrationAtemToggle`
  - `kSettingsActionIntegrationNdiTriggerToggle`
  - `kSettingsActionIntegrationNmcToggle`
  - `kSettingsActionIntegrationMtcToggle`
  - `kSettingsActionIntegrationLtcToggle`
  - `kSettingsActionIntegrationArtNetToggle`
  - `kSettingsActionIntegrationArtNetPortPrompt`
  - `kSettingsActionIntegrationAllToggle`
- Companion/OSC command wiring is in `handleRemoteCommand(...)` and
  `mapOscToRemoteCommand(...)` for:
  - `ATEM`, `NDITRIGGER`, `NMC`, `MTC`, `LTC`, `ARTNET`, `ARTNETPORT`, `INTEGRATIONS`.
- Runtime listeners (Linux/macOS path) live in `native/main.cpp`:
  - `startAtemBridgeListener()` / `atemBridgeLoop()`
  - `startNdiTriggerBridge()` / `ndiTriggerLoop()`
  - `startArtNetBridgeListener()` / `artNetBridgeLoop()`
  - bridge lifecycle wrappers: `startIntegrationBridges()` / `stopIntegrationBridges()`.
- NDI metadata trigger runtime details:
  - dynamically loads `libndi` with `NdiTriggerApi`
  - metadata frames are enqueued as `NDIEVENT ...`
  - payload parsing is centralized in `handleNdiTriggerPayload(...)`
  - optional source selection currently uses `DECKBOY_NDI_TRIGGER_SOURCE`
    until a proper UI picker exists.
- NMC sync runtime details:
  - lifecycle/state lives in `refreshNmcSyncState()` / `startNmcSyncBridge()` /
    `stopNmcSyncBridge()`
  - input mode is a UDP listener thread (`nmcSyncLoop()`) that enqueues
    `NMCEVENT ...`
  - payload application is centralized in `handleNmcSyncPayload(...)`
  - output mode is polled from `tickNmcSyncOutput()` inside `update()`
  - current config is env-driven: `DECKBOY_NMC_MODE`, `DECKBOY_NMC_PORT`,
    `DECKBOY_NMC_HOST`, `DECKBOY_NMC_SOURCE`, `DECKBOY_NMC_LOCATE_MS`

## UI cleanup notes (March 2026)

- Composite cue first implementation cut (`deckboy-0.60`):
  - cue kind + serialization are live in `native/core/types.hpp` and
    `saveProject(...)` / `loadProject(...)`
  - add flows:
    - `addCompositeCue()`
    - bottom `SCENE` media button
    - `M` keyboard shortcut
    - remote aliases: `COMPOSITE`, `SCENE`, `MULTIVIEW`
  - this first pass intentionally uses a scene placeholder renderer:
    - `renderCompositeCuePlaceholder(...)`
    - used in Program monitor, Preview monitor, and output render path
    - avoids black / invalid runtime behavior while the slot-runtime phase is
      still pending
  - inspector path currently supports:
    - layout presets `2-UP`, `70/30`, `QUAD`
    - up to 4 saved slot sources
    - cycling a designated audio slot
    - attached overlays from the overlay bin
  - next phase should replace placeholder slot cards with real per-slot source
    runtimes, most likely by adapting the existing `PIP` source-resolution
    pattern to a per-slot runtime map
- Composite cue planning notes:
  - see `docs/COMPOSITE_CUE_SPEC.md`
  - recommendation is to add a first-class `Composite` cue rather than a
    generic live layer system
  - reasoning is architectural, not aesthetic:
    - Deckboy currently has one primary live cue (`Deck::activeIndex`)
    - overlays are sidecar items (`Deck::overlayActiveIndices`)
    - output rendering is main scene first, then overlays
  - the spec proposes reusing the existing source-resolution pattern pioneered
    by `PIP`, but moving multi-source authored layouts into a main-cue runtime
    instead of the overlay system
- Cue / warp clipboard notes:
  - cue settings copy/paste intentionally preserves cue identity and source
    media (`name`, `id`, `path`, probed metadata), and only copies the
    inspector-facing playback / geometry / key / color / overlay-attachment
    settings
  - warp copy/paste is deck-scoped and currently copies the 4-corner warp plus
    edge blends
  - warp preset naming now uses `openInlineTextEditor("warp.preset", ...)`
    instead of the older blocking picker path
- Fade defaults:
  - `Deck::playlistDefaultCueFadeSeconds` now defaults to `1.5`
  - normalization / load fallbacks in `native/main.cpp` were updated to the
    same default so new/empty projects inherit the longer fade
- Overlay subdeck/bin operator model:
  - for compatibility, overlays still live in `Deck::cues` internally, but the
    active control-surface layout now splits them into a dedicated `OVERLAY BIN`
    instead of mixing them into the main playback rundown
  - the overlay bin is conditional: if there are no overlay-only cues, the main
    rundown expands to fill the space
  - `cueIsOverlayOnly(...)` is the gate for `Lower Third` / `PIP`
  - `nextCueIndexForDeck(...)`, `selectRelative(...)`, and cue-end
    auto-advance now skip overlay-only cues so looping/next logic stays about
    the primary playback sequence
  - playlist mouse hit-testing now has separate primary-list vs overlay-bin
    regions; drag reorder is intentionally limited to main cues for now
  - main rundown and overlay bin maintain separate wheel-scroll offsets
- PIP operator controls:
  - `PIP` is now source-driven rather than cue-target-driven:
    - supported inspector source types are `media`, `browser`, `window`,
      `camera`, and `syphon/spout`
    - legacy cue-linked PIP cues are still loadable and editable as
      `Legacy Cue Link`
  - live PIP overlay runtimes now build a resolved runtime cue from the chosen
    source type before loading the overlay media engine
  - corner presets and size presets are rendered inline in the `PLAYBACK`
    section, with exact geometry still handled below in `GEOMETRY`
  - `anchorPipCueToCorner(...)` uses current output size plus the cue's scale
    to keep the preset inset visible with a consistent margin
- Primary cue overlay attachments:
  - non-overlay cues now expose an `OVERLAYS` section in the cue inspector
  - each cue can attach one `Lower Third` and one `PIP` by overlay-bin
    cue token/id/number/name
  - attachments fire on `TAKE` only and intentionally do not retrigger on loop
- Cue-row playback state controls now live in `renderCueRow(...)`:
  - icon-only buttons are rendered directly on each cue row for fade in,
    fade out, loop, hold, and cue audio
  - click handling is routed through `cueRowActionHits_` in
    `handleMouseDown(...)`, then dispatched with existing `QuickAction` wiring
- The footer `MEDIA / TRANSPORT / OUTPUT` strip is now cleaner:
  - section labels are drawn inside their panel groups in `renderButtons()`
  - old footer `Source` / `Pattern` selectors were removed from
    `renderButtons()` / `handleMouseDown()`
  - the media group now includes a dedicated `LOWER 3RD` button
- Footer tiles were later resized back up after the first cleanup pass:
  - `kLayoutBottomBarHeight` / `kLayoutButtonHeight` were increased again so
    bottom-bar labels fit without clipping
  - telemetry pills beside the program monitor now render `label + value`
    separately instead of squeezing everything into one clipped string
- Source cue type selection moved into the cue inspector:
  - `cueSourceTypeDropdownRect_` anchors the dropdown
  - `setSelectedSourceCueKind(...)` swaps source cue kind in-place while
    preserving/re-normalizing the source reference where possible
- Lower Third cues now have direct inspector-side editing:
  - `QuickAction::EditLowerThirdText`
  - `QuickAction::EditLowerThirdSubtext`
  - both actions use `openInlineTextEditor(...)` and update focused selected
    lower-third cues in place
- `clearOutput()` now clears `overlayActiveIndices` immediately so output clear
  removes live lower-third overlays during the fade rather than waiting for the
  deferred cleanup callback
- Settings modal layout was reorganized in `renderSettingsModal()`:
  - `System` now emphasizes `Appearance`, `Safety / Timecode`, `Show Flow`,
    `Cue Tools`, and `Playlist Prefs`
  - audio device selection moved to the `Audio` tab
  - `Network` now uses larger cards for Companion/OSC, OSC Query/Feedback, and
    integration adapters
- `PIP` cue implementation notes:
  - `CueKind::Pip` uses the existing overlay stack (`overlayActiveIndices`)
    instead of inventing a second deck/sub-deck
  - each live `PIP` cue owns a separate silent `MediaEngine` keyed by
    `deckIndex:cueIndex`, so the inset can play independently of the main deck
  - output rendering reuses normal cue geometry/color/key controls, so PIP
    placement is just the standard cue geometry path applied to the overlay
  - `setSelectedPipCueTarget(...)` updates the selected cue, reloads its
    target thumbnail, and refreshes any live overlay runtime
- UI motion policy changed:
  - `project.uiTransitionsEnabled` is normalized back to `true`
  - `System -> Appearance` now treats motion as always-on feedback rather than
    exposing an operator-facing `ANIM OFF` state
- Playback flag helpers were broadened so inspector/cue-row toggles behave
  consistently for still/source/browser/pattern/lower-third cues:
  - `toggleSelectedLoop()`
  - `toggleSelectedPauseOnLastFrame()`
  - `adjustSelectedFade(...)`
  - `setSelectedFade(...)`
- MTC ingest runtime is decoded in `midiLoop()`:
  - `SND_SEQ_EVENT_QFRAME` -> `decodeMidiMtcQuarterFrame(...)`
  - internal command ingress `MTCEXT <seconds> <fps>`
  - applied via `ingestIntegrationTimecode(...)`.
- Art-Net runtime command mapping is centralized in `handleArtNetEvent(...)`
  (ch1-10 mapping to transport/master cue commands).

## Super Deckboy (Deckboy 2) — deferred by design

Ideas that are **not** v1 work. They are here so they are not lost and not
half-started: each one changes the shape of the app rather than adding to it,
and starting any of them inside v1 would destabilise a tool people run shows
with. Revisit after 1.0 stability.

Survey they came from: `docs/COMPETITIVE_IDEAS.md` (2026-08-20).

### Timeline sequencing — from Resolume Arena, Millumin, disguise, Pixera

Every high-end server in the field puts cues on a **timeline** as well as in a
list, so a show can be scheduled and edited as a whole rather than fired cue by
cue. disguise calls it the thing that lets you "schedule and edit all the
visuals needed to run the show"; Pixera supports several timelines at once.

**Why it is a v2 item, not a v1 feature:** Deckboy's model is a *playlist of
cues with a focused deck*, and transport position belongs to the live cue. A
timeline means a show-level clock that cues are placed against, which is a
different ownership model for time — the same thing that made the stage timer
need its own clock instead of borrowing the transport. Bolting it onto v1 would
mean two competing notions of "now".

What already helps when it comes:
- The timeline PANEL and its filmstrip/waveform lanes already exist.
- `LayoutDragMode::Timeline` already handles the resizable lane area.
- Cue in/out points, pause points and trigger timecodes are already per-cue.

**SETTLED (the owner, 2026-08-20): MASTER CUES that fire individual SUBCUES.** Not a
ruler with clips on it. A master cue fans out to children, each of which keeps
owning its own time.

**This is the better design, and cheaper than a timeline.** Worth writing down
why, because the obvious reading is that it is the smaller idea:

- **It does not invert time ownership.** A timeline needs a show-level clock
  that decks follow, which is the expensive, invasive part of this whole
  section. Master cues need none of it: firing is the existing take path with a
  fan-out, and every subcue still owns its own position. That is why this may
  not even need to wait for v2, where a real timeline does.
- **It degrades gracefully live.** Absolute positions on a ruler drift the
  moment a speaker overruns, and the operator spends the show nudging. Relative
  firing (pre-wait / post-wait per child) does not care how long the previous
  item took.
- **It matches the mental model already in the app.** Deckboy is a list you
  fire. A master cue is still a cue you fire; it just does more.

It is NOT novel -- this is QLab's Group Cue, and QLab dominates theatre and live
events. That is a point in its favour (proven model), and it is genuinely
uncommon in the VIDEO playback space: Mitti has no equivalent. A cue deck with
video-server outputs and group cues is a real differentiator.

Where a timeline still wins, and why it stays on the v2 list anyway: tightly
choreographed content, where absolute positions ARE the point (music-synced LED
sequences). The two can coexist -- QLab effectively has both -- but they answer
different questions and should not be conflated.

What already exists for master cues:

- **Firing another deck's cue** is half-built: `handleRemoteCommand` routes
  `DECK <n> <command>` by re-entering itself, so "deck 2, take cue 5" is an
  existing code path. A master-cue fan-out is that call from a different source.
- **A cue that references another cue** is already modelled twice: Pip cues
  (`buildResolvedPipSourceCue`) and Composite cues (`compositeSlots`).
- **Per-cue waits** map onto existing fade/still-duration timing fields.

Decide early: does firing a master cue TAKE each subcue on its own deck (fan-out
to decks), or does one deck play them in sequence (a nested playlist)? Both are
defensible; they are different features.

### Per-LAYER warp and mask — from Millumin

Millumin does perspective transform, mesh warping and masks **at the layer level
as well as the output level**. Deckboy warps per OUTPUT only (`OutputTarget`
carries warp corners, warp mode, edge blend).

**Why v2:** per-layer warp means every composited source needs its own geometry
pass before the compositor combines them, which is a change to the render
pipeline rather than a new field. It also only becomes genuinely useful once
multi-deck compositing is the normal way to work — i.e. once Super Deckboy's
multi-deck mode exists — because with a single deck the output warp is already
the layer warp.

What already helps when it comes:
- Per-cue geometry (scale, offset, crop, rotation) is already modelled and
  applied per cue in the compositor.
- Warp maths and the four-corner UI already exist for outputs; the algorithm is
  reusable, it is the plumbing that changes.
- The per-deck GPU bridge from the v0.78.0 zero-copy work already composites
  multiple sources onto one output device.

### Multi-output screen layout map — from ProVideoPlayer, disguise (scaled down)

A flat 2D map of how outputs land on physical screens, so a multi-output show
can be checked before doors. Explicitly NOT 3D previz — that is a different
product (see COMPETITIVE_IDEAS.md).

**Why v2:** it is only worth building once there are routinely several outputs
with warp and blend between them, which is the Super Deckboy use case. Deckboy
already knows every output's raster, display, AOI and edge blend, so this is
drawing what it already knows — the work is deciding what the drawing is FOR.

### HAP GPU block upload — deferred, compatibility stays

HAP PLAYBACK AND ENCODING SHIP AND STAY. Deckboy decodes HAP, HAP Alpha, HAP Q
and HAP Alpha Only (own container parser + vendored Snappy), encodes all three
variants from the Encoder tab, and offers a conversion when it would actually
pay. None of that is affected by this deferral. What is deferred is uploading
the DXT/BC blocks straight to the GPU instead of expanding them to RGBA on the
CPU first.

**Why it is a v2 item.** SDL3 has no BC pixel format: `SDL_CreateTexture`
cannot express BC1/BC3 and `SDL_UpdateTexture` has no notion of block pitch, so
blocks cannot be handed to the renderer at all. And a BC texture cannot be
`CopySubresourceRegion`'d into the RGBA `SDL_Texture` the compositor samples --
the formats do not match. Doing it properly means a D3D11 pass of our own:
create the BC texture, bind it as an SRV, draw a fullscreen quad through a
pixel shader into an RGBA render target, and hand THAT to the compositor. Hap Q
needs a YCoCg->RGB conversion in the same shader. That is a rendering pipeline
Deckboy does not otherwise have, per platform, which is a change in the shape
of the renderer rather than an addition to it.

**What it would buy, measured** (5s of 1080p on the dev machine):

| | H.264 | HAP |
|---|---|---|
| per stream, one thread | 454ms | 265ms |
| six concurrent | 540ms | 361ms |
| file size | 4.5MB | 19.3MB |

The CPU path already delivers most of this: HAP is 1.7x cheaper per layer than
H.264 today, without any GPU work, because DXT decompression is far cheaper
than H.264 decode. GPU upload would remove the remaining decompress and cut the
upload to a quarter of the bytes. Real, but a refinement of a working feature
rather than the feature itself -- which is exactly why it waits.

**Cheaper win to try first, inside v1.** `hap_decoder.cpp` decodes chunked
frames in order. The format's chunking exists specifically so a decoder can
spread the work across threads; correctness does not depend on doing so. Both
the chunk decode and the DXT expansion are embarrassingly parallel. That is
measurable CPU savings with no shader pipeline and no platform-specific code.

What already helps when GPU upload comes:
- `hap::Frame::data` is already raw block data, ready for upload as-is.
- `TextureFormat` already maps 1:1 onto DXGI BC formats.
- `DecodedFrame` already carries a GPU payload (`gpuTexture`/`gpuDevice`) and
  the per-deck D3D11 bridge from the v0.78.0 zero-copy work already composites
  GPU-resident frames.

### Not pursuing at any version

3D projection mapping, FBX import, marker calibration, Notch/Unreal/TouchDesigner
blocks, cloud collaboration. Different products, not features.

## Deck mixing for VJ work (proposed and then BUILT in v0.87.0)

Raised by James 2026-08-28 as research toward "superdeckboy", written down as a
proposal, and built the same day as VJ mode. Kept as written because the design
reasoning is still what the code does, and because the shape of the answer is
not what it looks like from outside.

### The second deck already exists

`Project::decks` is a vector and always has been. Each deck carries its own
playlist, transport, `MediaEngine`, cues, geometry and effects, and they run
simultaneously today — the five-deck demo shows in `tests/` are exactly that.
Every `OutputTarget` names one source deck through `hostDeckIndex`, and
`app_render_output.ipp` renders that deck's active cue into the output's
compositor texture.

So the work is not "add a second deck". It is that **an output can only be fed
by one deck**, and there is no way to blend two. That single constraint is what
separates a cue deck from a VJ mixer.

### What has to change, smallest first

**1. An output can name a B source.** `OutputTarget` gains `mixDeckIndexB`
(-1 = no mix), `mixPosition` (0.0 = all A, 1.0 = all B) and `mixBlendMode`.
Nothing else about the output changes, and an output with `mixDeckIndexB == -1`
behaves exactly as today — which matters, because every existing show is that
case and must stay pixel-identical.

**2. The compositor renders twice.** Where it renders deck A's cue into
`compositorTexture` it renders A, then B over it with the fader's alpha. Both
decks already produce compositor-ready output, so this is a texture operation
at the point where the picture is assembled, not a change to decode, effects or
geometry.

**3. Blend modes.** `SDL_BLENDMODE_BLEND` gives dissolve, `ADD` gives additive,
`MOD` gives multiply — all free. Screen, difference and luma-key need
`SDL_ComposeCustomBlendMode` or a pass over pixels. Start with the three that
are free; they are also the three a VJ reaches for most.

**4. Audio follows the fader, or does not.** A video crossfade with a hard
audio cut is worse than no crossfade. Deck faders already exist, so the mix
position should drive them unless the operator unlinks it — that unlink is a
real control, because "video mixes, audio stays on A" is a normal request.

### What it looks like

The current layout is one deck: playlist left, program centre, inspector right.
A VJ layout is a **mode**, not a replacement — the existing one stays for
cue-deck work.

    ┌──────────┬───────────────────────┬──────────┐
    │ DECK A   │        PROGRAM        │ DECK B   │
    │ playlist │                       │ playlist │
    │          ├───────────────────────┤          │
    │          │  A ▓▓▓▓▓▓░░░░░░░░ B    │          │  ← crossfader
    │          │  [dissolve][add][mul] │          │
    └──────────┴───────────────────────┴──────────┘

The crossfader is the `masterFaderRect_` pattern already in `app_input.ipp`:
a rect, a drag flag, position from mouse x. That idiom is proven and takes
about twenty lines.

Two monitors would be better than one — A and B previews either side of the
program — but that is a layout question, and the program-monitor tap
(`captureOutputPreviewTap`) already knows how to sample a deck's picture
without a second decode, so it is not a new mechanism.

### What is genuinely hard, and worth knowing up front

- **Two decks at 4K.** One 4K decode plus effects is already most of a frame.
  Two, plus a blend, will not hold 60 at 4K on modest hardware. The honest
  answer is that VJ mixing is a 1080p feature unless the machine is large, and
  the effect benchmarks (`--effect-bench`) already make that measurable rather
  than a guess.
- **Effects are per cue.** A VJ wants them per DECK — grab the deck, wreck it,
  bring it back. The stack is on `Cue`, so a per-deck stack would either be a
  second stack applied after the cue's, or a "deck cue" that the whole deck
  inherits. The former is simpler and composes better.
- **Tempo.** Beat-matching is the thing that makes a VJ set feel played rather
  than triggered. `SPEED` (0.25–4) exists per deck already; what does not is a
  tempo source, a per-cue beat length, and a loop that respects it. That is its
  own project and probably the real content of "superdeckboy".
- **The mix must survive a panic.** `runPanicOutputsOff` assumes one source per
  output. A mix has two, and both have to go.

