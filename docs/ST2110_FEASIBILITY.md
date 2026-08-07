# SMPTE ST 2110 Output — Feasibility Assessment

Status: **research only, no code written.** Written 2026-08-05 at the owner's request
("explore the possibility of SMPTE 2110 output").

---

## The short answer

ST 2110 is not "another output backend" like NDI or Spout. Those are one library
and a send call. ST 2110 is a **facility discipline** that a device participates
in: a shared PTP clock, a negotiated stream description, and a sender that must
hit its packet timing to the microsecond. Adding it means Deckboy stops being a
self-contained app and starts being a piece of broadcast infrastructure.

That is achievable, but the honest cost is **months, not a weekend**, and roughly
half of it is not video code at all — it's clock sync, network configuration, and
interoperability testing against gear we don't own.

My recommendation: **do not build ST 2110 before 1.0.** The nearest useful thing —
and the thing that actually gets Deckboy into 2110 facilities — is NDI, which
Deckboy already has, plus a documented path through a hardware or software gateway.
Revisit native 2110 only if a specific venue demands it.

---

## What the standard actually requires

ST 2110 is a suite. A minimum-credible video sender needs all of these:

| Part | What it is | Can we skip it? |
|---|---|---|
| **ST 2110-10** | System, timing, RTP transport | No |
| **ST 2110-20** | Uncompressed active video RTP payload | No |
| **ST 2110-21** | Sender traffic shaping (N / NL / W models) | **No — this is the hard part** |
| **ST 2110-30** | PCM audio (AES67) | Only if video-only is acceptable |
| **ST 2110-40** | Ancillary data (timecode, captions) | Yes, initially |
| **ST 2059-1/2** | PTP profile for broadcast (media clock alignment) | No |
| **IEEE 1588 PTP** | The actual clock | No |
| **AMWA NMOS IS-04/05** | Discovery and connection management | In practice, no |

### The three things that make this hard

**1. PTP is not optional and not simple.**
Every ST 2110 sender derives its media clock from a grandmaster over IEEE 1588.
Windows is a poor PTP host: there is no general-purpose OS-level PTP service, and
software timestamping typically lands in the tens-of-microseconds range against a
budget that wants sub-microsecond. Doing this properly means a NIC with **hardware
PTP timestamping** and a driver stack that exposes it. That is a hardware
requirement we would be imposing on the user, not a software feature.

**2. ST 2110-21 traffic shaping is the real engineering.**
1080p59.94 uncompressed is ~2.6 Gb/s; 2160p59.94 is ~10.6 Gb/s. You cannot simply
`sendto()` that. The packets must be paced to a defined model (narrow, narrow
linear, or wide) or downstream receivers under-run and the stream is
non-conformant. Meeting the narrow model from user-space on Windows is not
realistic — this is where implementations reach for kernel bypass:

- **DPDK** or **Rivermax** (NVIDIA/Mellanox), which do the pacing in hardware
- Rivermax requires a **ConnectX-5/6/7 class NIC** and a **paid licence**
- DPDK on Windows is immature relative to Linux

**3. NMOS discovery is a second product.**
Facilities do not hand-configure SDP files. They expect IS-04 registration and
IS-05 connection management, which means an HTTP/JSON API, mDNS registration, and
conformance to AMWA specs. That is a meaningful subsystem on its own — comparable
in size to Deckboy's entire existing networking layer.

---

## Options, with honest costs

### Option A — Native ST 2110 via Rivermax
Real, conformant, professional.
- **Needs:** ConnectX-class NIC, Rivermax licence, PTP grandmaster, managed switch
  with PTP boundary clock, 25GbE for UHD
- **Effort:** large. Rivermax integration, PTP alignment, SDP generation, NMOS
  IS-04/05, then interop testing
- **Risk:** we cannot validate it without a real 2110 plant to test against.
  Building this blind is how you ship something that "works" on the bench and
  fails at a venue
- **Verdict:** only justified by a concrete customer requirement

### Option B — Native ST 2110 via a software stack (FFmpeg / GStreamer / libst2110)
FFmpeg has partial ST 2110-20 support and there are open stacks (e.g. Intel's
Media Transport Library). Cheaper to start.
- **Reality check:** without hardware pacing these produce streams that are
  *structurally* 2110 but frequently **not 2110-21 conformant**, and PTP accuracy
  on Windows remains the ceiling
- **Verdict:** fine for a lab demo, misleading to ship as "ST 2110 output"

### Option C — Gateway out (RECOMMENDED)
Deckboy emits what it already emits well; a gateway converts.
- **NDI → ST 2110** via a hardware bridge or software gateway; several vendors sell
  exactly this box, and 2110 facilities routinely already own one
- **SDI → ST 2110** via DeckLink out (Deckboy already has DeckLink) into a standard
  SDI-to-IP gateway. This is the most common real-world path
- **Effort:** effectively zero new code
- **Verdict:** this is how a product Deckboy's size gets into a 2110 plant

### Option D — Do nothing yet, document the path
Note the gateway route in the manual, revisit on demand.

---

## If we ever do build it

A phased order that keeps every phase independently useful:

1. **PTP client + clock telemetry.** Read the grandmaster, show offset/lock state
   in Settings. Useful on its own for A/V sync diagnostics even without 2110.
2. **SDP generation + ST 2110-20 sender, wide model, 1080p only.** Validate against
   a software receiver. Explicitly labelled experimental.
3. **NMOS IS-04/05.** Registration and connection management.
4. **Rivermax path for narrow-model conformance.** The point where it becomes real.
5. **ST 2110-30 audio, then -40 ancillary.**

Do not present anything before step 4 as "ST 2110 output" in the UI. Half-conformant
2110 that fails at a venue is worse for Deckboy's reputation than no 2110 at all —
the same reasoning behind parking the multi-output spanning features until Super
Deckboy rather than shipping them half-wired.

---

## What I'd do instead, near-term

The gap between Deckboy and professional IP video is not ST 2110. It's the things
directly under it that are already half-built:

- **NDI is thin.** No group, bandwidth mode, multicast, or audio-enable — all of
  which NDI 6 supports and 2110-adjacent facilities actually use
- **SRT has no SRT-specific settings.** No latency, passphrase, or streamid. Those
  are the fields that make SRT usable over a real WAN
- **No PTP/timecode discipline** anywhere, which is the genuinely transferable
  groundwork toward 2110 later

Closing those three would do more for professional deployment than a partial ST
2110 implementation, and step 1 of the 2110 plan falls out of the third for free.
