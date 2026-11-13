# Performance captures, round three, 13 November 2026

All four GIFs were re-recorded with the supplied NEW Deckboy.exe (build supplied as commit ce7b90fe). Changes are confined to docs/images/perf/ and left uncommitted for Claude. No external network access, push, merge or tag was used. Loopback TCP controlled isolated app instances. No Portal was used.

## Moving pictures

| File | Bytes | Dimensions | Seconds | Badge: app-loop fps |
| --- | ---: | --- | ---: | ---: |
| four-layers.gif | 869777 | 640 x 470 | 3.800000 | 60.2 |
| shatter.gif | 1104945 | 640 x 470 | 2.500000 | 57.5 |
| clouds.gif | 627013 | 640 x 470 | 2.500000 | 58.1 |
| decoder-effects.gif | 1655802 | 480 x 380 | 3.000000 | 58.4 |

Every source was recorded at 1920x1080 with RECFORMAT 1920x1080 60. Retained STATUS replies confirm window_px=1920x1080 and render_px=1920x1080 for output and recorder. Each GIF says Recorded at 1080p. The GIF encoding clock is 10 fps; the badge is the measured app-loop rate, calculated as 1000 divided by the arithmetic mean of frame dt in record-profile.log from that same recording. It covers the recording window, including the parts subsequently trimmed, and is not a claim of sustained throughput or minimum frame rate.

| Capture | Profile samples | Mean frame interval, ms | Unrounded app-loop fps |
| --- | ---: | ---: | ---: |
| four-layers.gif | 312 | 16.623269230769246 | 60.15663863213053 |
| shatter.gif | 309 | 17.377864077669905 | 57.544471261285416 |
| clouds.gif | 311 | 17.19961414790998 | 58.14083917234362 |
| decoder-effects.gif | 302 | 17.133145695364245 | 58.36639796220097 |

Shatter and Clouds are trimmed to contiguous 2.5-second excerpts: approximately 0.2 seconds before TAKE, the complete two-second transition, and 0.3 seconds afterward. Command-response latency and recording startup may shift these holds slightly. No speed change, interpolation or fabricated loop dissolve is used. Four Layers retains seconds 1.5 through 5.3; Decoder Effects retains the first three seconds. New contact sheets sample at four frames per second, with unused cells padded. All four new contact sheets were opened and visually reviewed for badges, content and transition coverage. The Four Layers sheet shows the first three seconds of its 3.8-second loop.

Four Layers uses a base picture plus four mixed-codec PIPs: H.264/blur, HEVC/glow, ProRes/kaleidoscope and MJPEG/mosaic. Each effect parameter A has a 0.25 Hz LFO. Decoder Effects uses a 3840x2160 HEVC yuv420p10le source and curves, glow, blur, ripple, vignette, grain, levels and sharpen, with a 0.2 Hz ripple LFO. The output is SDR. Decoder Effects is encoded at 480 pixels wide with 64 palette colours; the other GIFs at 640 pixels with 128 colours.

## Capture evidence

### four-layers.gif

Composite reuse confirmed in app.log: `recording-capture: output 2 reads output 1's composite`.

Above 55 fps in this short recording. Slowest frame profile lines are retained here for review.

```text
[DECKBOY_UI_PROFILE 25481ms] frame dt=22.64ms events=0.01ms update=1.05ms layout=0.01ms render=1.55ms (present=0.16ms) render+outputs=21.58ms
[DECKBOY_UI_PROFILE 25279ms] frame dt=20.06ms events=0.01ms update=1.28ms layout=0.02ms render=1.38ms (present=0.17ms) render+outputs=18.77ms
[DECKBOY_UI_PROFILE 24227ms] frame dt=17.64ms events=0.01ms update=1.95ms layout=0.03ms render=2.00ms (present=0.23ms) render+outputs=15.68ms
[DECKBOY_UI_PROFILE 21260ms] frame dt=17.61ms events=0.01ms update=1.39ms layout=0.01ms render=2.08ms (present=0.25ms) render+outputs=16.21ms
[DECKBOY_UI_PROFILE 23976ms] frame dt=17.26ms events=0.01ms update=1.13ms layout=0.01ms render=1.63ms (present=0.33ms) render+outputs=16.13ms
```

### shatter.gif

Composite reuse confirmed in app.log: `recording-capture: output 2 reads output 1's composite`.

Above 55 fps in this short recording. Slowest frame profile lines are retained here for review.

```text
[DECKBOY_UI_PROFILE 9980ms] frame dt=48.68ms events=0.02ms update=19.68ms layout=0.03ms render=2.57ms (present=0.44ms) render+outputs=28.98ms
[DECKBOY_UI_PROFILE 9680ms] frame dt=48.56ms events=0.01ms update=1.77ms layout=0.01ms render=2.23ms (present=0.26ms) render+outputs=46.79ms
[DECKBOY_UI_PROFILE 9742ms] frame dt=43.73ms events=0.01ms update=0.89ms layout=0.01ms render=0.87ms (present=0.21ms) render+outputs=42.83ms
[DECKBOY_UI_PROFILE 9894ms] frame dt=38.78ms events=0.33ms update=1.40ms layout=0.04ms render=2.33ms (present=0.54ms) render+outputs=37.05ms
[DECKBOY_UI_PROFILE 9817ms] frame dt=37.87ms events=0.01ms update=1.19ms layout=0.02ms render=2.22ms (present=0.49ms) render+outputs=36.66ms
```

### clouds.gif

Composite reuse confirmed in app.log: `recording-capture: output 2 reads output 1's composite`.

Above 55 fps in this short recording. Slowest frame profile lines are retained here for review.

```text
[DECKBOY_UI_PROFILE 9773ms] frame dt=53.00ms events=0.01ms update=0.88ms layout=0.01ms render=5.63ms (present=0.39ms) render+outputs=52.11ms
[DECKBOY_UI_PROFILE 9882ms] frame dt=36.76ms events=0.02ms update=1.33ms layout=0.03ms render=1.82ms (present=0.53ms) render+outputs=35.42ms
[DECKBOY_UI_PROFILE 9679ms] frame dt=36.71ms events=0.01ms update=2.58ms layout=0.01ms render=1.56ms (present=0.20ms) render+outputs=34.12ms
[DECKBOY_UI_PROFILE 9809ms] frame dt=36.24ms events=0.01ms update=1.05ms layout=0.02ms render=2.71ms (present=1.69ms) render+outputs=35.18ms
[DECKBOY_UI_PROFILE 9845ms] frame dt=35.58ms events=0.01ms update=1.24ms layout=0.02ms render=1.89ms (present=0.53ms) render+outputs=34.33ms
```

### decoder-effects.gif

Composite reuse confirmed in app.log: `recording-capture: output 2 reads output 1's composite`.

Above 55 fps in this short recording. Slowest frame profile lines are retained here for review.

```text
[DECKBOY_UI_PROFILE 15382ms] frame dt=35.58ms events=0.01ms update=1.30ms layout=0.01ms render=1.15ms (present=0.40ms) render+outputs=34.28ms
[DECKBOY_UI_PROFILE 14713ms] frame dt=33.66ms events=0.21ms update=1.19ms layout=0.01ms render=0.67ms (present=0.13ms) render+outputs=32.26ms
[DECKBOY_UI_PROFILE 15613ms] frame dt=33.61ms events=0.01ms update=1.02ms layout=0.02ms render=1.03ms (present=0.13ms) render+outputs=32.59ms
[DECKBOY_UI_PROFILE 12463ms] frame dt=33.50ms events=0.01ms update=0.93ms layout=0.01ms render=0.72ms (present=0.16ms) render+outputs=32.56ms
[DECKBOY_UI_PROFILE 12363ms] frame dt=33.49ms events=0.01ms update=0.99ms layout=0.01ms render=0.73ms (present=0.15ms) render+outputs=32.49ms
```

## Reproduction and provenance

Run from the worktree root:

`node docs/images/perf/record-gifs.cjs`

`node docs/images/perf/make-gifs.cjs`

`node docs/images/perf/finalize.cjs`

The recorder checks the composite-reuse log line in every run and fails if absent. Raw MP4s, app.log, record-profile.log, complete commands/replies, elapsed metadata and isolated shows are under recordings/round3/<scene>/. The profile slice starts after RECORD on replies and ends before RECORD off. All four recording processes exit before GIF encoding begins. Earlier recordings remain separately under recordings/final/.

Each app uses MASTERVOL 0, SDL_AUDIODRIVER=dummy, SDL_AUDIO_DRIVER=dummy, update_check=0 and ui_sounds=0, with isolated root, state, project and loopback port. Recording has no overlay, matte, captions or NDI of its own. Exact environment, setup, import, effect, transition and recording commands are in record-gifs.cjs and commands.log. make-gifs.cjs holds the exact trims, drawtext badges, palette settings and contact-sheet commands. gif-manifest.json includes hashes, source paths, dimensions, trim intervals and measured values.

Executable: C:/Users/james/deckboy-audit-1111/build/windows/Release/Deckboy.exe

SHA-256: e975d13fb55a993df88e8a53ac663f47e07e1d6e91e7028f19b11fd38c11f20c

The three stills, measurements.csv and earlier timing card are retained from the previous round; they were not remeasured for these GIFs.

## Retained stills and timing card

four-layers.png (276242 bytes), decoder-effects.png (2005034 bytes), and shatter.png (303892 bytes) are unchanged from the earlier still run. Their 4K labels describe that earlier output; they do not describe these 1080p GIFs. The decoder source was verified with ffprobe codec_name,pix_fmt,width,height,r_frame_rate. The four-layer still's 60.0 fps badge came from its STATUS output; the earlier decoder still has no retained valid STATUS line supporting its 60.0 badge, so it must not be reused as performance evidence.

frame-timings.png was rebuilt in the same style with three rows. Bytes: 95371. Its earlier measurements are unchanged:

| Condition | OLD samples | OLD mean ms | NEW samples | NEW mean ms |
| --- | ---: | ---: | ---: | ---: |
| Shatter | 505 | 24.26 | 728 | 16.82 |
| Colour grade | 97 | 129.32 | 695 | 17.92 |
| Chroma key | 281 | 44.44 | 724 | 17.21 |

These are arithmetic mean app-loop intervals from the earlier 3840x2160 run, including pacing and the rest of the app. Transition windows contain four alternating two-second takes with 2.2-second waits, protocol overhead and a one-second lead-in, including short gaps. Grade/key windows follow warm-up and include six two-second waits plus STATUS queries. They are not isolated shader costs. measurements.csv retains the applicable original frame samples. This is one PC and short runs, not a cross-platform or long-duration stability result.

Earlier commands: node build/media-perf/run.cjs generate; measure old; measure new; node build/media-perf/suite.cjs; node build/media-perf/run.cjs decoder new; node build/media-perf/analyze.cjs. Earlier raw logs and scripts remain in ignored build/media-perf. The rebuilt card is generated by make-gifs.cjs without rerunning those measurements. Grade cue fields 50/51/52 were 1.25/1.2/1.3; key cue field 46 was enabled. No recorder ran in those timing windows.

All source clips were generated locally with ffmpeg testsrc2; no downloaded footage or generated audio. Base: testsrc2=s=3840x2160:r=60:d=8, libx264 ultrafast, crf=20, yuv420p; incoming variant: hue=h=130. PIPs: 1280x720, six seconds; H.264 60 fps/libx264; HEVC 30 fps/libx265/hue=90; ProRes 30 fps/prores_ks profile 1/yuv422p10le/hue=180; MJPEG 30 fps/q=5/hue=260. Ten-bit: 3840x2160/30 fps/six seconds/libx265 ultrafast/yuv420p10le. x265 used log-level=error:pools=4. All generation commands used -an.

Assets are left for Claude's review and commit. No commit was attempted in this round.
