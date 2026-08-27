# Per-Cue Effects — design notes

Status: **proposal, nothing built.** Written 2026-08-26 after a survey of what
the glitch/pixel-art field is currently doing. Revised the same day after
looking at GitHub and the TouchDesigner forums, which corrected two novelty
claims in the first draft -- see 1b.

---

## 1. What the field is actually doing

The honest finding from the survey is a negative one, and it shapes everything
below: **the published glitch-art discourse is still recycling four techniques
from 2010–2012.** Databending, datamoshing, pixel sorting, VHS emulation. A 2026
festival roundup covering fifteen artists names a tool for five of them and a
reproducible method for none. Wikipedia, the aesthetics wikis and the tutorial
sites all describe the same four.

Pixel sorting is the clearest case. Asendorf published it in 2011, open-sourced
it in 2012, and the fifteen years since have produced variations on *which*
interval to sort and *what* to sort by — not a successor technique.

**Conclusion: copying the ART discourse produces derivative work by
construction.** Anything genuinely new has to come from somewhere else.

### 1b. Where the practitioners actually are

The art-world sources are the wrong place to look, and the first draft of this
document was written before checking the right ones. **GitHub and the
TouchDesigner forums are where the method lives**, and they are considerably
further ahead than the festival write-ups suggest.

**FFglitch** (ramiropolla) is the serious tool here and has been since about
2020. It exposes a video's *bitstream* to a JavaScript engine: motion vectors --
split into their prediction and delta parts -- DC coefficients and the
quantizer, all scriptable per frame for MPEG-2 and MPEG-4. There is a tutorial
series and a scripts repository. Motion-vector averaging across frames is a
worked example there, not a frontier.

**tiberiuiancu/datamoshing** combines *two videos' motion vectors by adding
them together*, via ffgac/ffedit. **LukasBommes/mv-extractor** pulls frames and
motion vectors out of H.264 and MPEG-4 with a documented vector format.

**TouchDesigner** ships an **Optical Flow TOP** (X motion in red, Y in green)
and the forum has years of optical-flow warp feedback patches -- iterative loops
that displace the previous frame by the current motion field. Worth noting: that
operator requires an Nvidia 3000-series or newer.

**What this corrects in the first draft:**

- *Motion-vector puppetry* is **not** unprecedented. Combining and rewriting
  motion vectors is established offline practice with a mature toolchain. What
  does not exist is doing it **live, at frame rate, on a running cue, with the
  vectors driving a different source** -- a camera, a synth, another deck. Still
  distinctive, but the claim is about *liveness and cross-source*, not the idea.
- *Flow feedback* is **not** novel at all. It is a standard TouchDesigner patch.
  Struck from the novel list below and moved to table stakes -- though taking
  the flow from decoder motion vectors would let Deckboy do it on any GPU
  rather than Nvidia-only.

## 2. Where new material actually is

Two seams worth mining.

**(a) Algorithms from adjacent fields that have never been live video effects.**
Computer vision, computational photography and compression research are full of
methods whose *failure modes* are beautiful and whose authors spend their papers
trying to suppress them. Seam carving's literature is largely about eliminating
the temporal jitter that appears when you run it per-frame — which is to say,
there is a decade of research characterising an effect nobody has ever used on
purpose.

**(b) Deckboy's own position.** Every glitch tool in the survey operates on a
file. Deckboy is a *live cue player*: it has a transport, a running order, an
audio bus, multiple decks, and — since v0.78.0 — an in-process libav decoder.
Effects that know about any of those are effects an image tool structurally
cannot have. This is the richest seam and the least explored.

## 3. Design principles

- **Per-cue, saved with the show.** Same ruling as the datamosh look (D70): a
  choice about one clip belongs on that clip.
- **Run on the small buffer where possible.** The video synth's CRT stage cost
  23ms at 4K until it was moved to the internal raster; an effect stack that
  ignores that lesson will make the app unusable at 4K.
- **Zero cost at zero.** Every stage skipped entirely when its amount is 0, as
  the synth's glitch stack already does.
- **Nothing that only works on synthetic content.** These have to survive real
  programme material, which is mostly faces and text on flat backgrounds.

## 4. Tier 1 — the expected set

Table stakes. Cheap, well understood, and the operator will look for them.
Nothing here is interesting; it just needs to exist.

Blur/sharpen, bloom, chromatic aberration, posterise, solarise, invert, hue
rotate, saturation, contrast curves, vignette, film grain, halftone, scanlines,
edge detect, threshold, mirror/kaleidoscope, RGB channel offset.

## 5. Tier 2 — novel because the substrate is video

These need motion or frame history. They are not available to an image tool,
which is why they are largely unexplored.

**Flow-sort.** Pixel sorting has always run along rows or columns because a
still image has no other axis. Sort along **optical flow lines** instead and the
smear follows the motion *in the picture* — a face turning drags its own
highlights around the turn. As far as the survey found, nobody has done this.

**Seam tremor.** Run seam carving per frame and deliberately do not stabilise
it. The literature's "annoying artifact, magnified in video" is the effect: the
picture writhes as the energy map shifts, and the writhing is driven by the
content rather than by noise. Carve and re-insert to keep the raster.

**Self-referential time map.** Slit-scan where the per-pixel time offset comes
from *the picture itself* — bright regions live in the present, dark regions lag
by up to a second. The image chooses its own temporal geometry, and a cut makes
the shadows arrive late.

**Energy wells.** Pixels fall toward local luminance minima and pool there, so
the picture slowly drains into its own shadows. Sorting as gravity rather than
as ordering.

**Temporal dither.** A pixel-art idea on a video substrate: quantise hard to a
tiny palette, then advance the dither pattern every frame. At 60Hz the eye
integrates shades that are not in the palette at all — a 4-colour image that
reads as continuous tone, and freezes into visible checkerboard the moment you
pause. Ties directly to the existing hardware palettes.

*(Flow feedback was listed here in the first draft and has been struck: it is a
standard TouchDesigner patch, not a new idea. It still belongs in the product --
see 1b -- just not as a claim to originality.)*

## 6. Tier 3 — novel because of what Deckboy is

The strongest ideas. None of these are expressible in a file-based tool.

**Live motion-vector puppetry.** *Still the standout, with a narrower claim.*
Deckboy decodes H.264 in-process, so it can read real per-macroblock motion
vectors out of a playing clip. Apply one clip's vectors to a *different
source's* pixels and that source is puppeteered by the first one's movement —
a camera feed animated by a crowd scene, a synth driven by a dancer.

Rewriting motion vectors offline is mature practice (FFglitch, 1b). Doing it
**live, at frame rate, across two sources, while the show is running** is not,
and it is what a file-based toolchain structurally cannot do: FFglitch edits a
bitstream and writes a new file, which is not an operation you can put on a cue.
Datamosh already exploits the codec but only by *removing* information; this
uses what the codec actually knows.

**Cue bleed.** The effect knows the running order: the outgoing cue's frames
persist inside the incoming cue's dark regions, so a cut leaves a residue that
decays over seconds. Datamosh between *cues* rather than within a clip.

**Transport-indexed feedback.** Index the feedback buffer by transport position
rather than frame count. Scrub the deck and you scrub the smear; jog backwards
and the trails run backwards. The operator's hands are in the effect.

**Programme-bus reaction.** The synth already reacts to audio. Extend it to
every effect, driven by the *programme* mix — the effect responds to what the
audience is hearing, not to a file.

**Live quantizer bend.** FFglitch's other lever, which the first draft
missed entirely: the DC coefficients and the quantizer, not just the motion
vectors. Driving the quantizer live — from audio, from the transport, from a
fader — makes the *compression itself* an instrument, and the picture blocks
up and recovers under the operator's hand. Offline tooling for this exists; a
live control surface for it does not.

## 7. Architecture sketch

- `CueEffectStack`: an ordered list of `{kind, amount, params}` on the `Cue`,
  serialised at the end of the record like everything else.
- Runs where the synth's glitch stack runs — on the internal buffer, before the
  upscale — for generated cues. For video cues it needs a decided home:
  `applyCueVisualEffectsToPixels` already exists in the bridge and is the
  obvious candidate, but it runs at full raster, which is the CRT mistake again.
  **Open question: a small working raster for video effects too, or accept full
  raster and budget accordingly.**
- Flow-based effects need an optical flow field. Cheap options: reuse the
  decoder's motion vectors (free, coarse, H.264 only) or a small Lucas-Kanade
  on a downscaled luma (costly, universal). The former is more interesting
  *and* cheaper, which is unusual.

## 8. What to build first

1. **Live motion-vector puppetry**, as a spike. Most distinctive thing on the
   list, exercises the decoder plumbing, and if the vectors turn out to be
   unavailable or too coarse at frame rate then the whole flow family needs
   rethinking. Fail fast on the interesting one. `mv-extractor` documents the
   vector format; FFglitch's tutorials explain the prediction/delta split.
2. **Temporal dither**, as the cheap win — small, self-contained, immediately
   striking, and it makes the existing hardware palettes far more interesting.
3. **Tier 1**, in bulk, once the stack exists.

Flow-sort, seam tremor and the self-referential time map are the ones worth
real effort after that.

## 9. Drivers that are not files (proposed 2026-08-27)

The motion driver currently reads vectors out of an encoded file, because that
is where a codec leaves them. The owner asked whether the VIDEO SYNTH could be a
driver too, and it should be -- but not by that route. A synth has no bitstream,
so there is nothing to read.

It has something better: it knows its own motion **analytically**. The synth
already tracks `vsynthRotation_`, `feedbackZoom` and `speed`, and a rotate-plus-
zoom about the centre has a closed-form displacement per cell:

    dx = (cos t - 1) * x' - sin t * y'      (x', y' relative to centre, scaled)
    dy =  sin t      * x' + (cos t - 1) * y'

So a synth driver would be exact rather than estimated, cost nothing to compute,
and carry no I-frame gaps -- strictly better than what a file can offer. The
same is true of any generated source whose transform is known: patterns, the
timer, a still with cue geometry animating.

That suggests the driver should not be "a file path" at all but a SOURCE:

  - a file            -> motion vectors from its bitstream (built)
  - a generated cue   -> its transform, evaluated (proposed)
  - another deck      -> whichever of the two that deck is running

Worth doing after the vector path has been used in anger, so the shape of the
abstraction comes from two working implementations rather than one and a guess.
