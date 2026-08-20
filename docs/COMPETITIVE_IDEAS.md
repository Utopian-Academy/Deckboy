# Ideas Worth Stealing

Survey of the field, 2026-08-20, done at the owner's request. Ordered by value per
unit of effort **for Deckboy specifically** — not by how impressive the feature
is in the product it came from.

**Status (the owner, 2026-08-20):** items 1–5 below are ACCEPTED for v1 and are the
working list. The timeline and per-layer warp ideas were also accepted, but as
**Super Deckboy (Deckboy 2)** work — they are written up in `DEVNOTES.md` under
"Super Deckboy (Deckboy 2) — deferred by design", with the reason each one
changes the shape of the app rather than adding to it. Do not start them inside
v1.

## Where Deckboy already stands

Worth stating first, because it changes what is worth copying. Against **Mitti**
— the closest peer — Deckboy is at rough parity and already has: NDI, DeckLink,
Syphon/Spout, SRT/RTMP, corner pin + edge blend, subtitles, NMC multi-instance
sync, ATEM input triggering, HyperDeck emulation, OSC, Art-Net, TSL tally.

Against **stagetimer.io premium** it already has NDI output, Companion/Stream
Deck, embedded web pages, transparent background, on-air indicator and blackout
— because Deckboy is a video tool and stagetimer is a web app.

So the gaps below are genuinely gaps, not things we forgot we had.

---

## 1. Show log — QLab

**What:** records cue order and execution timing for the whole show.

**Why it is first:** Deckboy has none. After a show goes wrong the operator has
no way to answer "what fired, and when?" Everything else on this list makes
Deckboy do more; this makes it *trustworthy*, which matters more on a tool
people run live events with. The `--soak` harness already proves we can write a
timestamped log; this is that, wired to cue events.

**Effort:** low. Append-only file, one line per take/stop/panic/output change.

## 2. Scheduled start / time-of-day triggers — PlayDeck, stagetimer

**What:** a cue fires at a wall-clock time. PlayDeck builds 24/7 automated
rundowns on it; stagetimer charges for "Scheduled Start".

**Why:** unlocks unattended playback, which Deckboy cannot do at all today. It
also lands the last obvious stagetimer premium feature.

**Effort:** low-medium. The timecode infrastructure (LTC/MTC ingest, trigger
timecodes per cue) already exists — this is a wall-clock variant of a mechanism
that is already there.

## 3. Multiple cue markers — PlayDeck

**What:** named jump marks inside a clip, with countdowns to the next mark.

**Why:** Deckboy has in/out points and pause points, so the data model is most
of the way there. For a long-form clip an operator wants to jump to "verse 2"
rather than scrub.

**Effort:** medium. Mostly UI on the timeline.

## 4. Record incoming signal — PlayDeck

**What:** record a live input, and edit the clip while it is still recording.

**Why:** Deckboy has capture inputs (camera, window, NDI, SRT) and can encode —
it simply never writes an input to disk. For an event that wants an instant
replay or a same-day edit this is significant, and most of the plumbing exists.

**Effort:** medium.

## 5. ASIO / Dante audio — PlayDeck

**What:** PlayDeck does up to 32 independent channels over Dante/ASIO.

**Why:** Deckboy is SDL audio only. It has multichannel output pair routing per
cue, so the routing concept is already modelled, but on Windows a pro audio
install means ASIO. This is the biggest *professional* gap on the list.

**Effort:** high. New audio backend.

## Moved to Super Deckboy (Deckboy 2)

Accepted by the owner, but deferred: see `DEVNOTES.md`. Both change the render or
time-ownership model rather than adding a feature, which is exactly why they do
not belong in a v1 that people run shows with.

- **Timeline sequencing** — Resolume, Millumin, disguise, Pixera. Needs a
  show-level clock; today transport position belongs to the live cue.
- **Per-layer warp and mask** — Millumin. Needs a geometry pass per composited
  source, and only pays off once multi-deck compositing is normal.
- **Output layout map** — ProVideoPlayer, disguise scaled down. Worth building
  once several warped/blended outputs are routine.

## 6. Output layout preview — disguise, Pixera (scaled down)

**What:** disguise and Pixera do full real-time 3D stage previz with FBX import
and marker calibration. That is a different product, not a feature.

**The stealable part:** a flat 2D map of how outputs land on screens, so a
multi-output show can be checked before doors. Deckboy already knows every
output's raster, display, AOI and edge blend — this is drawing what it already
knows.

**Effort:** medium. Explicitly NOT chasing 3D previz.

---

## Deliberately not pursuing

- **3D projection mapping / previz** (disguise, Pixera) — a different product.
- **Notch / Unreal / TouchDesigner blocks** — enormous integration surface.
- **Hardware switcher lines** (PixelHue F-series, X400) — hardware, not software
  features.
- **Cloud collaboration** — Deckboy is a local tool by design.

## Sources

- [Mitti](https://imimot.com/mitti)
- [PLAYDECK](https://playdeck.tv/)
- [ProVideoPlayer](https://www.renewedvision.com/provideoplayer)
- [disguise Designer](https://www.disguise.one/en/products/designer)
- [Pixera](https://pixera.one/)
- [Stagetimer features](https://stagetimer.io/features/)
