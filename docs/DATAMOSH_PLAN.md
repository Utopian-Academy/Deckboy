# Datamosh — Plan

**Status:** the encode half is DONE and verified (the `Datamosh` preset). The
decode half — dropping I-frames at playback — is not started.

Design decisions and who made them are in `DECISIONS.md` (D3–D9, D24–D28).

## What it is

Drop I-frames from the compressed stream so P-frames apply their motion vectors
onto a stale reference. The decoder does the smearing; there is no pixel maths.
the owner specified real bitstream moshing, not a pixel-domain imitation (D4).

## Half that is done

The **`Datamosh` encode preset**, libx264 only:

```
-bf 0 -sc_threshold 0 -refs 1 -g 120 -vsync cfr
```

| flag | why |
|---|---|
| `-bf 0` | B-frames reference both directions and break worst when the reference is gone |
| `-sc_threshold 0` | no keyframe at every scene change — those cuts are exactly the moments the effect should carry through |
| `-refs 1` | motion vectors read only the previous frame: the classic look |
| `-g 120` | regular keyframes for **seeking only**; the decoder drops them at playback, so this is seek granularity, not an effect parameter (D26) |
| `-vsync cfr` | the moshed copy must stay frame-for-frame interchangeable with the original |

NVENC is excluded deliberately: it ignores the reference and scene-cut controls
and injects its own IDR frames, silently undoing the preset (D25).

**Verified** against a real source: B-frames 1→0, refs→1, GOP `IBBPBBP`→`IPPPPPP`,
keyframes regular at 2.0 s, and frame/duration parity with a normal encode
(1200 frames / 20.000000 s both) so the swap cannot shift sync.

Output is `<stem>_mosh.mp4` beside the original, never replacing it (D27) —
required because the toggle swaps between the two.

## Half that is not

### 1. Drop keyframes in the decoder

`native/engine/libav_decoder.cpp`, in the packet loop right before
`avcodec_send_packet` (~line 385):

```cpp
if (datamosh_.load() && (packet->flags & AV_PKT_FLAG_KEY) && seenFirstKey_) {
  av_packet_unref(packet);
  continue;   // P-frames now apply motion onto a stale reference
}
```

`seenFirstKey_` matters: the first keyframe after open or seek must pass, or the
decoder never produces a picture at all. The pipeline already has a cross-thread
atomic pattern (`requestStop`), so a `setDatamosh(bool)` setter is the same
shape — note the header currently documents `requestStop` as "the only
cross-thread entry point", which stops being true.

### 2. Force software decode for moshed cues (D8)

Hardware decoders fed P-frames with no valid reference are driver-dependent: may
smear, may error, may reset. Errors accumulate against `kMaxConsecutiveErrors`
(40) and then kill the decode. Software decode is predictable and identical
across GPUs. Cost: loses the zero-copy path, noticeable at 4K — acceptable
because it only applies while the effect is on.

### 3. The toggle swaps clips (D6, the owner's design)

Toggle on → play `<stem>_mosh.mp4` with keyframe dropping. Toggle off → play the
original. This is better than moshing in place: toggle-off is instant and clean
rather than waiting up to a GOP for a keyframe, and the software-decode cost is
scoped to when the effect is active.

The swap mechanism already exists: `MediaEngine::refreshActiveCueRuntime`
(media_engine.cpp:369) computes the current absolute position, stops the
decoders, clears audio and restarts from that position. It is what already runs
when SPEED or in/out points change on a playing cue.

**Trap:** `MediaEngine::mediaPathForCue` calls an app-supplied resolver, and that
looks like the ideal one-line insertion point. It is not.
`resolvedCueFilesystemPathString` is shared by waveform analysis, thumbnails,
timeline filmstrips, the missing-media scan and loudness normalize — swapping
there would retarget all of them at the mosh file. Scope the swap to the decode
path only.

### 4. Guards

- **All-intra sources** (ProRes, DNxHD, MJPEG, image sequences): every frame is
  a keyframe, so dropping them all yields nothing, then the 4-second decode
  watchdog reracks the deck dark. Needs an explicit codec guard, not a graceful
  accident. `cue.videoCodec` is already probed and available.
- **Stills, patterns, browser, live sources** have no bitstream — hide or
  disable the toggle, as `cueSupportsKeying` already does for chroma key.
- **Not prepped, already friendly** (H.264, no B-frames): mosh in place, no
  transcode needed. So the toggle has three states — mosh in place, mosh via
  prepped copy, needs prep — and only the third costs an encode.

### 5. Data model

`Cue` gains `bool datamoshEnabled` and `std::string moshPath` (appended fields,
backward-compatible via `safeBool`/`safeString` as usual). The media-presence
scan should check `moshPath`, RELINK should repoint it, and disk doubles per
prepped cue.

## Show-day rule (D9)

Prep is a **pre-production** action. A transcode must never start because
someone hit the toggle during a show. If a cue is not prepped, the honest
behaviour is "not prepared for datamosh" — not an encode starting behind the
fader.

## Open questions for the owner

- H.264 vs MPEG-4 Part 2 for the mosh preset. The catalog now has `mpeg4`
  (`MPEG-4 Part 2 / AVI`) — that is the *classic* datamosh look: chunkier, no
  deblocking filter tidying up the smear. Worth prototyping both and picking by
  eye.
- Whether prep runs automatically on toggle, or on demand only.
