# Deckboy Decision Log

Every decision gets logged with **who made it**. the owner's decisions take priority
over mine and are not to be revisited or "improved" without them saying so — if I
think one is wrong, I raise it, he rules, and the ruling gets logged here.

Legend: **[OWNER]** = their call, binding. **[CLAUDE]** = mine, open to challenge.

---

## Standing rules

| # | Decision | Who | Notes |
|---|---|---|---|
| S1 | Log every decision, marking who made it; prioritise the owner's | **OWNER** | 2026-08-19. This file. |
| S2 | Text placement is a contract — fix alignment systemically, never per-widget | **OWNER** | They judge quality by consistent text placement. |
| S3 | Fix themes as data, never the renderer | **OWNER** | Exception: adding theme *roles* is allowed. |
| S4 | Never raise bot XP rate / never seed population (WoW) | **OWNER** | Out of scope here, kept for cross-project consistency. |
| S5 | Encoding must never outbid playback during a show | **CLAUDE** | Drives the concurrency cap; challenge if you disagree. |
| S6 | Reality outranks notes; a note counts only if checked at the moment of use | **OWNER** | 2026-08-19. Verify paths/flags/capabilities against the live system in the turn that uses them. The system wins over any note, comment, or earlier claim of mine. |
| S7 | Every change must work on Windows, macOS and Linux -- same quality, not necessarily the same solution | **OWNER** | 2026-08-24. See D81. Check the `#ifdef` structure before saying done. |

---

## 2026-08-19 — Encoder / datamosh session

| # | Decision | Who | Rationale / status |
|---|---|---|---|
| D1 | Add the magenta beach splash to the cycle pool | **OWNER** | "You can add the decent one." DONE — `splash_5.png`. |
| D2 | Desaturate + gamma-match it to the existing pool | **CLAUDE** | Pool is grayscale masters tinted per theme; colour art multiplies to mud. Matched mean 22.7 vs pool 19–25. |
| D3 | Datamosh as a per-cue **toggle** | **OWNER** | Not a global or deck-level effect. |
| D4 | Real bitstream datamosh (drop I-frames), not a pixel-domain fake | **OWNER** | They specified "removing all iframe". Fake offered as cheaper alt, not chosen. |
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
| D21 | Whimsy line runs full width below the mascot, not inside the face column | **OWNER** | They reported "info text in the cue is being cut off" - it ellipsized to "this one has opi...". Mascot also silently text-falls-back under 150x120. FIXED. |
| D22 | Queue list distinguishes [converting...] from [queued] | **CLAUDE** | It called all 31 jobs "converting" when only 1 was. FIXED. |
| D23 | Work order: presets -> other encoder fixes -> cancel/pause UI -> cut v0.83.2 | **OWNER** | 2026-08-19, explicit; release last. |
| D24 | Four encode presets: Delivery H.264 / Proxy 720p / Match Source / Datamosh | **CLAUDE** | Scoping the owner's "AME+" ask. DONE + verified. |
| D25 | Datamosh preset is libx264 ONLY (no NVENC) | **CLAUDE** | NVENC ignores/constrains refs + scenecut and injects its own IDRs, silently undoing the preset. |
| D26 | Keyframes every 2s in the mosh file for SEEKING; dropped at decode for the effect | **CLAUDE** | Verified: keyframes at 0/2/4/6/8/10s, scene-cut disabled so they stay regular. |
| D27 | Datamosh output is `<stem>_mosh.mp4` beside the original, not a replacement | **CLAUDE** | Required by D6 (toggle swaps between the two). Awaiting the owner's confirmation. |
| D28 | Leave Delivery preset quality as-is for now | **CLAUDE** | 22min -> 479MB is generous, but changing their existing default silently is their call. OPEN. |
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
| D46 | CPU DXT expansion first; GPU BC upload deferred | **CLAUDE** | SDL3 has no BC pixel format, so the GPU route needs a D3D11 texture + shader outside SDL. The CPU path makes HAP PLAY and gives all-intra seeking now. |
| D47 | Do NOT chase bit-parity with ffmpeg on DXT output | **CLAUDE** | Measured: 87% identical, max diff 1, and both are equally accurate vs the original image (1.22 vs 1.29 of 255). DXT decode is not specified bit-exact - GPU vendors disagree too. |
| D48 | HAP wired into VideoPipeline as a demux-only mode | **CLAUDE** | HAP cues now PLAY. Detected by codec id before any avcodec setup; output is ordinary RGBA32 so nothing downstream changes. All-intra means seek needs no keyframe walk. |
| D49 | Install ffmpeg n8.1 WITH libsnappy (stable release, not master) | **OWNER** asked / **CLAUDE** chose the build | HAP encode needs it. Chose a tagged release over the nightly for a show tool. Old binaries backed up to `C:fmpegin-backup-20260819`. |
| D50 | FIXED: the format picker was a DEAD CONTROL | **CLAUDE** | `attemptsForJob` only consulted the catalog for the datamosh preset; every other job silently encoded H.264 whatever ENCODEFORMAT said, and the extension was hardcoded .mp4. Presets now SELECT a format so there is exactly one thing deciding the encode. |
| D51 | EFFECTS inspector section, datamosh as its first member | **OWNER** asked for a home / **CLAUDE** proposed EFFECTS over renaming KEY | The toggle previously had NO UI - remote verb only. Section shows the three states (ON / off / not prepared) instead of making you trigger it to find out. |
| D52 | Press Start 2P on CHROME only, sans for user text (hybrid) | **OWNER** | 2026-08-19, chose it from a rendered comparison of 5 candidates. Already bundled + OFL. Point sizes ~0.62x the old sans sizes to keep cap height and line rhythm. |
| D53 | Accept wider text as the cost of the pixel font | **CLAUDE** | Press Start 2P is much wider per char, so cue names truncate sooner. Fixed-width inspector LABEL columns now clip ("deck ...") and need widening - that part IS a bug. |
| D54 | REVISED D52 to a hybrid after seeing it in situ | **OWNER** | They flagged the readability himself; the screenshot showed "S06E01" scanning as "S0GE0" - 0/O and 6/G are near-identical in Press Start 2P at UI sizes. Pixel face on labels/buttons/headers, sans on cue names/paths/metadata. |
| D55 | Icon rects must match their button rects | **CLAUDE** | Icons drew into snapRectToGrid(btn) while the button painted btn - up to 7px offset, glyphs outside their boxes. Pre-dated the font change. Same class as the v0.81.0 text-placement contract. |
| D56 | Encoder queue needs reorder + per-job start/pause/stop | **OWNER** | 2026-08-20. Only cancel exists today. |
| D57 | **REVERSES D9**: datamosh toggle AUTO-transcodes on enable | **OWNER** | 2026-08-20: "toggle it on, it immediately transcodes... or gives a warning if it cannot". I had ruled prep must never start from a live toggle; the owner overruled. Implement with clear in-progress state; warn rather than silently start on huge files. |
| D58 | New `Timer` source cue = full stage/speaker timer | **OWNER** | 2026-08-20. Port from a local C#/WPF speaker-timer app. Countdown, presets, amber/red thresholds, overtime count-up, blink, message overlay. |
| D59 | ~~Timer uses the TRANSPORT as its clock~~ REVERSED by D62 | **CLAUDE, overruled** | TAKE starts, PAUSE holds, RERACK resets. The operator gets the controls they already know instead of a second set of timer buttons. |
| D60 | Timer duration = countdown + 1h overtime allowance when count-up is on | **CLAUDE** | Otherwise the transport hits its end at zero and auto-advances to the next cue while a speaker is still talking. |
| D61 | Timer cues default to hold, endAction Stop | **CLAUDE** | A countdown that auto-advanced at zero would be actively dangerous on a show. |
| D62 | Timer has its OWN clock, independent of transport | **OWNER** | 2026-08-20: the cue stays live on the stage screen while the operator starts/pauses/resets/jumps the countdown. Tying it to transport meant pausing the clock took the display off air, which is wrong for a stage timer. |
| D63 | AUDIT found 3 dead controls in same-day code (timer mode/face/serialization) | **CLAUDE** | Same class as the encoder format picker. Auditing for 'written but never read' is now worth doing routinely, not once. |
| D64 | Competitive survey recorded in docs/COMPETITIVE_IDEAS.md | **OWNER** asked | Ranked by value-per-effort FOR DECKBOY, noting where it is already at parity (Mitti) rather than listing impressive features. |
| D65 | Items 1-5 of the survey ACCEPTED as the v1 working list | **OWNER** | 2026-08-20: show log, scheduled start, cue markers, record input, ASIO/Dante. |
| D66 | Timeline + per-layer warp + output map = SUPER DECKBOY (v2), not v1 | **OWNER** | 2026-08-20. Written up in DEVNOTES under 'Super Deckboy (Deckboy 2)'. Each changes the app's shape (time ownership / render pipeline), so starting one inside v1 would destabilise a live tool. |
| D67 | MASTER CUES fire individual SUBCUES (not a ruler with clips) | **OWNER** | 2026-08-20, their original idea, refined. Fan-out from the existing take path. |
| D68 | Master cues are BETTER than a timeline here, and cheaper | **CLAUDE**, agreeing with the owner | They do NOT invert time ownership (each subcue keeps its own position), so this may not need to wait for v2. They also degrade gracefully when a show runs long, where an absolute timeline drifts. Not novel — it is QLab's Group Cue — but uncommon in VIDEO playback (Mitti has none). |
| D69 | Recording: build PROGRAM first, INPUT is a separate harder feature | **CLAUDE** | Program recording reuses the stream egress path with a file target (small). Input recording needs a second capture of an exclusive-open device, or a tee off the decode pipe -- not small, and not to be started without that decision. |
| D70 | Datamosh look is PER-CUE and saved with the show, not a global chip | **CLAUDE** | 2026-08-21. It was a global unserialized bool in the Encoder tab, so it reset to the weakest flavour every launch and the operator was silently always on SUBTLE. A look choice about one clip belongs on that clip. |
| D71 | Added EXTREME, and CLASSIC replaces SUBTLE as the default for new cues | **CLAUDE**, confirmed by the OWNER | 2026-08-21. MEASURED on a 10s/24fps clip: CLASSIC yields 2 keyframes = 1 droppable = ONE smear in ten seconds; EXTREME yields 10 = 9 droppable, at half the file size. The owner checked EXTREME on real footage: "fantastic, and just about exactly what i wanted". |
| D72 | Encoder rate overrides are CODEC-AWARE, and refused where they cannot apply | **CLAUDE** | 2026-08-21. There is no universal quality knob (-crf, -cq, -qscale:v all differ in meaning and range; ProRes/DNxHR/HAP/FFV1 are profile-driven with no continuous axis). A single "CRF" field would be a lie on more than half the catalogue, and silently ignoring an override is the dead-control pattern. |
| D73 | HAP compatibility STAYS; GPU block upload deferred to Super Deckboy | **OWNER** | 2026-08-21. Decode, encode and the conversion offer all ship. Only the DXT-blocks-straight-to-GPU refinement waits, because it needs a D3D11 shader pipeline Deckboy does not otherwise have -- a change in the renderer's shape, not an addition. |
| D74 | HAP is a PREFERRED option, not a default or a recommendation | **OWNER** | 2026-08-21. Measured: 1.7x less CPU per layer but 4.3x the disk. It earns its place on multi-layer shows and heavy scrubbing, not on a single clip, so it is offered on evidence rather than pushed. |
| D75 | Recording is a SEPARATE STANDARD from the programme, defaulting to follow-input | **OWNER** | 2026-08-23: "resolution should be configurable, but it should default to the input resolution". Raster and rate are set independently and scale on the GPU before readback; 0x0 means follow the input. |
| D76 | Recording must meet broadcast rates exactly, not approximately | **OWNER** | 2026-08-23: "not goofy recording framerate nonsense. i want CT and NEP using this". 23.976 is 24000/1001. The file must contain exactly rate x elapsed frames; a shortfall is an alarm, never a shorter file left to look healthy. |
| D77 | Drop-frame is a TIMECODE convention, and auto picks it by rate | **CLAUDE** | DF skips two timecode numbers a minute (except every tenth) at 29.97/59.94 and is meaningless at integer rates; no video frame is ever dropped. Auto is right by default, with an explicit override for a house that insists. |
| D78 | Feature set modelled on the AJA Ki Pro Go2/Ultra and OBS, not invented | **OWNER** asked | 2026-08-23. Segmentation, media-full behaviour, timecode source, codec list and the fragmented-then-remux trade all come from what those actually do. |
| D79 | Fragmented while recording, remuxed on stop | **CLAUDE** | OBS's trade, and the right one: a power cut leaves a playable file, a clean stop leaves a tidy one. It was inside an `#ifdef _WIN32` and shipped broken on the other two platforms -- see D82. |
| D80 | Cadence catch-up fill NOT attempted a third time | **CLAUDE**, after the OWNER pushed back | Bursting repeats to close a deficit wedged the encoder pipe twice (caps of 32, then 8/4). The owner: "something must be wrong or silly here". The pacer repeats at most its normal cadence and the alarm carries the rest. |
| D81 | Every change must work on Windows, macOS and Linux | **OWNER** | 2026-08-24: "going forward all changes must work on all 3", and "not necessarily the same solution, they all just have to work with the same level of quality". Supersedes the older Windows-first posture. |
| D82 | The D3D11 async readback stays Windows-only; the fallback is MEASURED, not assumed | **CLAUDE** | 2026-08-24. `DECKBOY_EGRESS_READBACK=sync` forces the portable path on a Windows desk: frame-exact at 1080p50, 1080p59.94 and 2160p25, behind only at 4K above 30p where the alarm fires. A portable ping-pong readback was tried and measured no better (311 vs 326 frames) -- SDL maps and copies in one call. Closing the 4K gap needs GL PBOs or a Metal blit, which is real work and not to be shipped untested to a field-test Mac. |
| D83 | 4K up to at least 60 must work on ALL THREE platforms | **OWNER** | 2026-08-24: "4k up to at least 60 is important!", in answer to my shipping 0.85.0 with macOS/Linux frame-exact only to 2160p30. Not a nice-to-have; it set the next piece of work. |
| D84 | Async readback via SDL_GPU, and output windows use the "gpu" renderer on macOS/Linux | **CLAUDE** | 2026-08-24. SDL3.4 exposes no route to the MTLTexture behind an SDL texture, so a Metal-specific readback cannot be written; SDL_GPU's `SDL_DownloadFromGPUTexture` is genuinely asynchronous and covers Metal, Vulkan and D3D12 in ONE implementation. Windows stays on D3D11 -- faster there, and the only backend where zero-copy decode works. `DECKBOY_OUTPUT_RENDERER` overrides either way. VERIFIED on Windows by forcing the gpu renderer: readback 3.2ms against 19ms synchronous, 1080p60 1199/1201, 2160p30 599/601, and the recorded 4K picture is correct. Still wants a look on the field-test Mac. |

---

## Open questions awaiting the owner

- Destructive vs non-destructive prep output naming (`<stem>_mosh.mp4`).

_Resolved and removed 2026-08-21: prep-on-toggle (yes), H.264 vs MPEG-4 for the mosh preset (MPEG-4, D71), and the v0.83.2 release question (v0.84.0 has since shipped)._
