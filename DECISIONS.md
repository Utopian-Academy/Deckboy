# Deckboy Decision Log

Every decision gets logged with **who made it**. the owner's decisions take priority
over mine and are not to be revisited or "improved" without him saying so — if I
think one is wrong, I raise it, he rules, and the ruling gets logged here.

Legend: **[OWNER]** = his call, binding. **[CLAUDE]** = mine, open to challenge.

---

## Standing rules

| # | Decision | Who | Notes |
|---|---|---|---|
| S1 | Log every decision, marking who made it; prioritise the owner's | **OWNER** | 2026-08-19. This file. |
| S2 | Text placement is a contract — fix alignment systemically, never per-widget | **OWNER** | He judges quality by consistent text placement. |
| S3 | Fix themes as data, never the renderer | **OWNER** | Exception: adding theme *roles* is allowed. |
| S4 | Never raise bot XP rate / never seed population (WoW) | **OWNER** | Out of scope here, kept for cross-project consistency. |
| S5 | Encoding must never outbid playback during a show | **CLAUDE** | Drives the concurrency cap; challenge if you disagree. |

---

## 2026-08-19 — Encoder / datamosh session

| # | Decision | Who | Rationale / status |
|---|---|---|---|
| D1 | Add the magenta beach splash to the cycle pool | **OWNER** | "You can add the decent one." DONE — `splash_5.png`. |
| D2 | Desaturate + gamma-match it to the existing pool | **CLAUDE** | Pool is grayscale masters tinted per theme; colour art multiplies to mud. Matched mean 22.7 vs pool 19–25. |
| D3 | Datamosh as a per-cue **toggle** | **OWNER** | Not a global or deck-level effect. |
| D4 | Real bitstream datamosh (drop I-frames), not a pixel-domain fake | **OWNER** | He specified "removing all iframe". Fake offered as cheaper alt, not chosen. |
| D5 | Re-encode source to a datamosh-friendly format when needed | **OWNER** | "if necessary it could include the option to re-encode." |
| D6 | Toggle swaps between original clip and prepped moshable clip | **OWNER** | Non-destructive; makes toggle-off instant. Better than my in-place plan. |
| D7 | Keyframes stay in the prepped file for seeking; dropped at decode | **CLAUDE** | Resolves long-GOP vs seek-cost tension. GOP = seek granularity, not an effect parameter. |
| D8 | Datamosh cues decode in **software**, not d3d11va | **CLAUDE** | HW decoders with missing references are driver-dependent. NOT yet built. |
| D9 | Prep transcode must never run during a show | **CLAUDE** | Pre-production action only. |
| D10 | Cute loading animation while anything encodes | **OWNER** | "obvi". IN PROGRESS. |
| D11 | Reuse the startup mascot via `overrideTip` rather than a 2nd mascot | **CLAUDE** | DONE. |
| D12 | Encoder tab should become "fully functional Adobe Media Encoder+" | **OWNER** | Scoped by me into queue → presets → per-item overrides. |
| D13 | Real encode queue with concurrency cap, default 1 | **CLAUDE** | Also fixes a real bug: every flagged cue used to spawn its own ffmpeg at once. DONE. |
| D14 | Real progress via ffmpeg `-progress pipe:1`, not a spinner | **CLAUDE** | DONE, verified live 6→34→45→56→65%. |
| D15 | Remote `ENCODE` / `ENCODEALL` / `ENCODEPAUSE` + `STATUS ENCODER` | **CLAUDE** | Needed for headless verification; also a real feature. Kept OUT of the integration-safe whitelist. |
| D16 | Push v0.83.2 work to `main`, not a feature branch | **OWNER** | Overrode my branch recommendation mid-turn ("wait main plz"). |
| D17 | Encoder tab layout must be fixed like the other menus were | **OWNER** | 2026-08-19, this session. Encoder tab was never converted to the scaled-metrics sweep. |
| D18 | Add three more splashes (autumn rooftop / winter campfire / brooklyn) | **OWNER** | 2026-08-19. DONE - splash_6/7/8, grayscale + gamma-matched to pool mean 22.7. Pool now 8. |
| D19 | Encoder tab body text must use light/soft, not `pal.deep` | **CLAUDE** | Root cause of "layout looks fucked up": rows rendered dark-on-dark. Last holdout in the modal (ink used 5x vs soft 26x / light 55x). FIXED. |
| D20 | Encoder tab joins the uiScale sweep (shared sPad/sLineH/sRowH + cardBody helpers) | **CLAUDE** | It was fully 1x-authored, incl. a hardcoded `cy + 56` header offset. Same failure as the display list's hardcoded 32. FIXED. |
| D21 | Whimsy line runs full width below the mascot, not inside the face column | **OWNER** | He reported "info text in the cue is being cut off" - it ellipsized to "this one has opi...". Mascot also silently text-falls-back under 150x120. FIXED. |
| D22 | Queue list distinguishes [converting...] from [queued] | **CLAUDE** | It called all 31 jobs "converting" when only 1 was. FIXED. |
| D23 | Work order: presets -> other encoder fixes -> cancel/pause UI -> cut v0.83.2 | **OWNER** | 2026-08-19, explicit; release last. |
| D24 | Four encode presets: Delivery H.264 / Proxy 720p / Match Source / Datamosh | **CLAUDE** | Scoping the owner's "AME+" ask. DONE + verified. |
| D25 | Datamosh preset is libx264 ONLY (no NVENC) | **CLAUDE** | NVENC ignores/constrains refs + scenecut and injects its own IDRs, silently undoing the preset. |
| D26 | Keyframes every 2s in the mosh file for SEEKING; dropped at decode for the effect | **CLAUDE** | Verified: keyframes at 0/2/4/6/8/10s, scene-cut disabled so they stay regular. |
| D27 | Datamosh output is `<stem>_mosh.mp4` beside the original, not a replacement | **CLAUDE** | Required by D6 (toggle swaps between the two). Awaiting the owner's confirmation. |
| D28 | Leave Delivery preset quality as-is for now | **CLAUDE** | 22min -> 479MB is generous, but changing his existing default silently is his call. OPEN. |
| D29 | Re-encode to ANY format, driven by a probed catalog ("like Media Encoder") | **OWNER** | 2026-08-19. 20 formats; availability probed from `ffmpeg -encoders`, never assumed. |
| D30 | Format availability is PROBED, not hardcoded | **CLAUDE** | Different ffmpeg builds ship different encoders; an offered-but-missing format fails into "conversion failed" with no reason. Verified: HAP correctly excluded on this build. |
| D31 | Quality args are per-codec, not one template | **CLAUDE** | x264/x265 `-crf`, NVENC `-cq`, ProRes/DNxHD profiles, VP9/AV1 need `-b:v 0`, GIF/PNG/audio must drop the stream they lack. All 17 available formats test-encoded OK. |
| D32 | Add HAP rows now, but do NOT advertise HAP as fast | **CLAUDE** | Through the ordinary decode path HAP is SLOWER than H.264 (CPU DXT->RGB) for 5-10x the file size. Needs the GPU path first. |
| D33 | HAP accelerated playback is its own project, not part of v0.83.2 | **CLAUDE** | New decode path: demux -> Snappy -> BC texture -> GPU bridge. See docs/HAP_PLAYBACK_PLAN.md. |
| D34 | Datamosh needs a SMOOTH/CHUNKY toggle, not two buried list entries | **OWNER** | 2026-08-19: "I would love an option, between classic chunky and modern smooth" + "maybe a toggle". DONE. |
| D35 | Smooth = H.264 (deblocking softens the smear), chunky = MPEG-4 Part 2 (none) | **CLAUDE** | Both verified moshable: no B-frames, IPPPP, identical frame counts so both stay swappable with the original. |
| D36 | Warn before bulk-encoding to a mastering format | **CLAUDE** | ProRes ~167MB/3s, QTRLE ~424MB/3s. 5+ cues to a mastering codec now warns rather than filling the drive silently. |
| D37 | Datamosh DECODE half built: withhold keyframes before avcodec_send_packet | **CLAUDE** | Verified live: `moshing=1`, first keyframe passes, later ones dropped. Forces software decode + disables zero-copy for moshed cues. |
| D38 | Toggle refuses when a cue is not prepared; NEVER transcodes from a live toggle | **CLAUDE** | Show-day rule (D9). Returns `ERR DATAMOSH: not prepared...`. |
| D39 | RENAMED the flavours after MEASURING them: CLASSIC / SUBTLE, not CHUNKY / SMOOTH | **CLAUDE** | H.264 barely moshes - a P-frame may contain INTRA macroblocks, so x264 self-heals within frames and there is no switch to stop it. MPEG-4 Part 2 gives the full effect. Labels now describe what they do, not what I assumed. |
| D40 | Format picker: all 22 catalog rows as wrapping chips in the Encoder tab | **CLAUDE** | Finishes the matrix; unavailable rows are drawn dim AND labelled. |
| D41 | Mark unavailable formats in TEXT ("n/a"), not by dimming alone | **CLAUDE** | Across 30 colorways a colour-only signal is not reliably readable - same class as the dark-on-dark encoder rows. |
| D42 | Build HAP playback | **OWNER** | 2026-08-19 "Build it?". Decode half done+verified; GPU upload remains. |
| D43 | VENDOR a Snappy decompressor rather than link libsnappy | **CLAUDE** | Decompress-only is a small frozen format; a per-platform dep is what kept this CI red for months. Verified against the reference impl. |
| D44 | Ship `tools/make_hap_sample.py` to generate HAP test media | **CLAUDE** | ffmpeg only ENCODES HAP with --enable-libsnappy, which the owner's build lacks. Output validated by ffmpeg's own HAP decoder. |
| D45 | Demux HAP without ever calling avcodec_send_packet | **CLAUDE** | Letting ffmpeg decode HAP unpacks DXT to RGB on the CPU - the exact cost the feature exists to avoid. |

---

## Open questions awaiting the owner

- Destructive vs non-destructive prep output naming (`<stem>_mosh.mp4`).
- Whether prep runs automatically on toggle, or on demand only.
- H.264 vs MPEG-4 Part 2 for the mosh preset (the latter is the classic look).
- Whether to tag/release v0.83.2 — 18 commits of cross-platform work are unreleased.
