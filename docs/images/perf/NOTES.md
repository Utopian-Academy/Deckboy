# Performance images, 12 November 2026

James asked for "a new set of gifs and images for the update, showcasing the new performance gains". These are review assets for Claude and James. Nothing was published. No source, version, release text, or existing documentation was changed.

## Files

| File | What it shows | Bytes |
| --- | --- | ---: |
| frame-timings.png | Measured mean app frame intervals, OLD and NEW. | 105110 |
| four-layers.png | 4K base with four mixed-codec picture layers and effect LFOs. | 276242 |
| decoder-effects.png | 4K 10-bit HEVC source with eight GPU effects. | 2005034 |
| portal.png | Portal transition, NEW build, a still from the output. | 389431 |
| shatter.png | Shatter transition, NEW build, a still from the output. | 303892 |
| measurements.csv | Every sampled frame interval used for the table; stack and decoder samples too. | 162186 |

## Conditions and metric

The app reported a 3840 x 2160 render and window on display 1 at 60 Hz. The attached primary display reported VG279QM, scale 1.5; the brief identifies the GPU as an RTX 4070 Laptop. This is one PC, one short run per condition. It is not a long-duration stability test or a measurement across platforms.

The timing card uses the arithmetic mean of every logged `frame dt=...ms` in each measurement window. This is the app loop's frame interval, including pacing and the rest of the app. It is not the isolated outgoing-transition draw cost or CPU grade cost quoted in the brief. OLD does not emit the detailed outgoing draw log, so those unlike metrics were not compared. No published brief number was copied onto an image.

Transition windows contain four alternating takes, each with a 2-second transition and a 2.2-second wait after TAKE, plus protocol overhead and a 1-second lead-in. Short gaps between transitions are included. Grade/key windows begin after a warm-up and span six waits of 2 seconds plus six STATUS queries. Log timestamps, sample counts, and unrounded measurements are in measurements.csv. The mean is rounded to two decimal places for the image. A frame interval can vary inside a run; the card does not claim every frame met the 60 Hz budget.

Windows were shown, not hidden or minimized. Primary-display STATUS sometimes reported windows above the output (an input-indicator overlay); gdigrab also exposed unrelated desktop windows. These are app timing runs, not proof of unobstructed screen playback. The two builds ran sequentially for the table. Clip generation and image encoding were outside those measurement windows. No RECORD command was used. OUTSNAP stills were requested after the timed windows. The transition illustration snapshots were taken in a separate run.

## Builds

Both binaries ran from their own Release directory, with all state and shows redirected into this worktree's build/media-perf. Neither binary was rebuilt or edited.

- OLD: `C:/Users/james/deckboy-baseline-1112/build/windows/Release/Deckboy.exe`
  SHA-256: `063938e4f099637090c993569ccc9fd700a1e6f453786e06e9573731929a872f`
- NEW: `C:/Users/james/deckboy-audit-1111/build/windows/Release/Deckboy.exe`
  SHA-256: `96eda6955a3aebbf7f886e272bbc314a3d845d10283e8e4b3568bdd1124771b3`

## Exact runs and numbers on the timing card

Commands ran in this worktree. Node is used because neither python nor py had an installed Python available in this session. Local run scripts and complete logs remain under build/media-perf (ignored by git).

`node build/media-perf/run.cjs generate` generated the H.264 base. A second ffmpeg pass applied `hue=h=130` to make the incoming picture distinguishable.

`node build/media-perf/run.cjs measure old` and `node build/media-perf/run.cjs measure new` measured both transition styles.

`node build/media-perf/suite.cjs` generated the other silent clips and ran grade/key on OLD then NEW, then stack/decoder on NEW. Decoder was repeated alone with `node build/media-perf/run.cjs decoder new`; only this final decoder run is retained in the evidence CSV. `node build/media-perf/analyze.cjs` extracted the frame intervals and calculated arithmetic means. `node build/media-perf/art.cjs` rendered the five PNGs with ffmpeg drawtext and Segoe UI.

Every app process received `SDL_AUDIODRIVER=dummy` and `SDL_AUDIO_DRIVER=dummy`. The first state-changing TCP command was `MASTERVOL 0`. Seed shows set `ui_sounds 0` and `update_check 0`. The app received `DECKBOY_ROOT`, `DECKBOY_STATE_DIR`, `DECKBOY_PROJECT`, and `DECKBOY_COMPANION_PORT` for isolated local runs, plus `DECKBOY_UI_PROFILE=2` and `DECKBOY_HIDE_WINDOWS=0`. Some later runs enabled `DECKBOY_OUTPUT_ON_TOP=1`; on display 1 with the control window also there, the app reported `on_top=0` and this option did not take effect.

Common TCP setup after importing: `DECK 1; SELECT 1; LOOP ON; SELECT 2; LOOP ON; OUTPUT SELECT 1; OUTPUT ON; VIDEO 3840X2160; SELECT 1; TAKE`. Semicolons here separate commands; each was sent on its own TCP connection. Imports used `DROPTEST <local text file listing the clip>` and polled `DROPTEST` until pending/probes were zero.

Transition setup: `TRANSITION 2`, `TRANSITIONSTYLE portal` or `shatter`, alternating `SELECT 2 / SELECT 1` and `TAKE` four times. Grade: brightness 1.25, contrast 1.2, saturation 1.3 on cue 1. Key: chromaKeyEnabled=1, all other key values left at their saved defaults. Grade and key were authored into cue fields 50/51/52 and 46 respectively (record has deck index in column 1, offset=2; verified against project_file.ipp), before launching the app. Base source: generated H.264 3840x2160, 60/1 fps, yuv420p, 8 seconds, looped.

| Condition | OLD samples | OLD mean ms | NEW samples | NEW mean ms |
| --- | ---: | ---: | ---: | ---: |
| portal | 475 | 25.79 | 725 | 16.89 |
| shatter | 505 | 24.26 | 728 | 16.82 |
| grade | 97 | 129.32 | 695 | 17.92 |
| key | 281 | 44.44 | 724 | 17.21 |

Raw first and last app lines for each table cell (all intervening frame samples are in measurements.csv):

OLD portal, command as above, measure-old/portal-profile.log:

```text
[DECKBOY_UI_PROFILE 8136ms] frame dt=17.02ms events=0.01ms update=0.78ms layout=0.01ms render=0.67ms (present=0.12ms) render+outputs=16.23ms
[DECKBOY_UI_PROFILE 20385ms] frame dt=16.71ms events=0.00ms update=0.81ms layout=0.01ms render=0.59ms (present=0.10ms) render+outputs=15.90ms
```

OLD shatter, command as above, measure-old/shatter-profile.log:

```text
[DECKBOY_UI_PROFILE 20769ms] frame dt=16.66ms events=0.00ms update=0.85ms layout=0.01ms render=0.60ms (present=0.10ms) render+outputs=15.80ms
[DECKBOY_UI_PROFILE 33019ms] frame dt=16.91ms events=0.01ms update=0.88ms layout=0.01ms render=0.70ms (present=0.12ms) render+outputs=16.02ms
```

OLD grade, command as above, grade-old/measured-profile.log:

```text
[DECKBOY_UI_PROFILE 9805ms] frame dt=122.28ms events=0.01ms update=4.02ms layout=0.01ms render=1.06ms (present=0.13ms) render+outputs=118.24ms
[DECKBOY_UI_PROFILE 22230ms] frame dt=121.90ms events=0.01ms update=3.77ms layout=0.01ms render=0.65ms (present=0.13ms) render+outputs=118.12ms
```

OLD key, command as above, key-old/measured-profile.log:

```text
[DECKBOY_UI_PROFILE 9874ms] frame dt=46.88ms events=0.01ms update=2.42ms layout=0.01ms render=1.08ms (present=0.15ms) render+outputs=44.45ms
[DECKBOY_UI_PROFILE 22324ms] frame dt=48.46ms events=0.01ms update=1.75ms layout=0.01ms render=0.60ms (present=0.12ms) render+outputs=46.70ms
```

NEW portal, command as above, measure-new/portal-profile.log:

```text
[DECKBOY_UI_PROFILE 7763ms] frame dt=16.75ms events=0.01ms update=0.88ms layout=0.02ms render=0.77ms (present=0.14ms) render+outputs=15.86ms
[DECKBOY_UI_PROFILE 20012ms] frame dt=16.69ms events=0.00ms update=0.88ms layout=0.02ms render=1.50ms (present=0.12ms) render+outputs=15.80ms
```

NEW shatter, command as above, measure-new/shatter-profile.log:

```text
[DECKBOY_UI_PROFILE 20396ms] frame dt=16.98ms events=0.01ms update=0.94ms layout=0.01ms render=0.89ms (present=0.14ms) render+outputs=16.03ms
[DECKBOY_UI_PROFILE 32646ms] frame dt=16.61ms events=0.01ms update=0.81ms layout=0.01ms render=0.61ms (present=0.10ms) render+outputs=15.79ms
```

NEW grade, command as above, grade-new/measured-profile.log:

```text
[DECKBOY_UI_PROFILE 9685ms] frame dt=16.76ms events=0.01ms update=0.83ms layout=0.01ms render=1.17ms (present=0.18ms) render+outputs=15.92ms
[DECKBOY_UI_PROFILE 22152ms] frame dt=11.16ms events=0.01ms update=1.28ms layout=0.01ms render=1.48ms (present=0.30ms) render+outputs=9.87ms
```

NEW key, command as above, key-new/measured-profile.log:

```text
[DECKBOY_UI_PROFILE 9682ms] frame dt=16.60ms events=0.01ms update=0.80ms layout=0.01ms render=0.69ms (present=0.14ms) render+outputs=15.79ms
[DECKBOY_UI_PROFILE 22148ms] frame dt=16.94ms events=0.00ms update=1.97ms layout=0.01ms render=0.69ms (present=0.12ms) render+outputs=14.96ms
```

## Numbers and content on the scene stills

four-layers.png: the 4K H.264 base plus four 1280x720 sources. Top left H.264/blur, top right HEVC/glow, bottom left ProRes/kaleidoscope, bottom right MJPEG/mosaic. Each has `FX LFO 1 A on`, `FX LFO 1 A rate 0.25`, `SCALE 0.36`, offsets X +/-1000 and Y +/-560, and `VIDEO OUTPUT ASSIGN`. Each was taken, looped, and visible in OUTSNAP. The screenshot shows those four layers. The 60.0 fps badge is a sampled output presentation rate, not a source frame rate or a minimum over the run. Command: `node build/media-perf/run.cjs stack new` (also invoked by suite.cjs).

decoder-effects.png: generated HEVC 3840x2160, yuv420p10le, 30/1 fps, confirmed by `ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,pix_fmt,width,height,r_frame_rate -of json build/media-perf/tenbit.mkv`. Eight `FX ADD` commands added curves, glow, blur, ripple, vignette, grain, levels and sharpen; ripple parameter A has a 0.2 Hz LFO. The 60.0 fps badge is the output presentation rate (the source is 30 fps). Command: `node build/media-perf/run.cjs decoder new`. The app logged:

```text
gpu-effects: on direct3d11
gpu-effects: playlist 1 effects from the decoded picture on
```

The source is 10-bit SDR; the output is not claimed as HDR or 10-bit. The project STATUS depth label reports the output setting, not the source pixel format.

Raw final STATUS output lines supplying the 60.0 fps labels:

STACK (NEW), stack-new-results.txt:

```text
OUTPUT 1 name="Output 1" id="out-87735c2a3f680a83" enabled=on health=live type=window host=1 display=1 layers=1 mirror=off stream=off proto=srt bitrate=6000 url="srt://127.0.0.1:9000" ndi=off ndi_name="Deckboy - Deck 1" ndi_rx=0 ndi_key=off ndi_key_name="Deckboy - Deck 1 Key" ndi_key_rx=0 alpha=100 delay_ms=0 overlay=off color_space=auto layout=span orientation=0 test_card=off output_fps=60.0 presents=1911 hitches=19 missed=45 target_hz=60 worst_gap_ms=259 window_px=3840x2160 render_px=3840x2160 fullscreen=1 paced_hz=60.001 compositor_hz=60.001 dxgi_mode=composed same_gpu=yes covers_screen=1 windows_above=-1 on_top=0 raised=0 stream_fps=--.- stream_q=0 stream_sent=0 stream_vbytes=0 stream_abytes=0 backend=window[ok]
```

DECODER (NEW), decoder-new-results.txt:

```text
undefined
```

portal.png and shatter.png contain no performance number. Their 4K labels refer to the verified 3840x2160 output snapshots. Command: `node build/media-perf/run.cjs stills new`; TAKE the incoming colour variant, wait 650 ms, then `OUTSNAP <style>.bmp`. These illustrate the transition, not its motion cadence.

## Media generation

Only ffmpeg-generated testsrc2 clips were used; no footage was downloaded and no audio stream was generated. Base command:

```text
ffmpeg -v error -y -f lavfi -i testsrc2=s=3840x2160:r=60:d=8 -an -c:v libx264 -preset ultrafast -crf 20 -pix_fmt yuv420p build/media-perf/base.mp4
ffmpeg -v error -y -i build/media-perf/base.mp4 -an -vf hue=h=130 -c:v libx264 -preset ultrafast -crf 20 build/media-perf/second.mp4
```

PIPs: testsrc2 at 1280x720, six seconds. H.264 uses 60 fps/libx264 ultrafast; HEVC uses 30 fps/libx265 ultrafast and hue=90; ProRes uses 30 fps/prores_ks profile 1/yuv422p10le and hue=180; MJPEG uses 30 fps/mjpeg q=5 and hue=260. The 10-bit source uses testsrc2 at 3840x2160, 30 fps, six seconds, libx265 ultrafast with yuv420p10le. x265 generation used log-level=error:pools=4. All ffmpeg commands use -an.

## Capture limits and unfinished GIFs

No GIF or MP4 is delivered. gdigrab of `title=Deckboy Output - Output 1` on the primary display captured unrelated desktop windows rather than a clean output. Its logs included `Couldn't get cursor info (error 5)`. Those captures are rejected and remain only in ignored run folders; none are staged.

The installed computer-use helper could not connect its native pipe (os error 2), so it could not select or activate the output. ffmpeg gfxcapture failed with `Failed to setup graphics capture for window (0x80070424)`. A separate attempt used display 2 and the app's existing DECKBOY_OUTPUT_ON_TOP option; STATUS then confirmed windows_above=0/on_top=1, but gdigrab returned black frames. Switching display also reset that attempted run to a 1280x720 render. That run is excluded from all delivered numbers. ffmpeg ddagrab on output_idx=1 failed with `Desktop duplication access denied` / `Operation not permitted`. No access settings were changed.

Some early capture attempts briefly overlapped a second app instance. Those runs are excluded from the comparison table. The final decoder timing run was repeated alone after capture testing ended. The primary table's measure/grade/key runs and the stack measurement ran sequentially, without capture or clip encoding alongside them.

For the requested motion assets, a session with working window capture is still needed. Do not make the OLD side stutter by retiming a NEW recording, or turn OUTSNAP sequences into a claim about live performance. The delivered stills do not assert visible smoothness. No 25-effect count, zero-copy throughput measurement, clouds/ghastly result, or all-platform result was measured here.

## Review

All five final PNGs were opened and checked: readable labels, full output picture, correct effects, no desktop content. No GIF exists, so there is no GIF contact sheet. Assets are 1200 pixels wide; the table is 800 pixels high and scene cards are 900 pixels high. The PNGs have no audio. All test app processes launched by these scripts were terminated. The supplied builds and existing documentation were left untouched. No external network access, publication, push, merge, or tag was used. Local TCP controlled only the test app instances.

## Commit status

The requested named-file staging attempt failed: Git could not create C:/Users/james/deckboy/.git/worktrees/deckboy-media-perf/index.lock (Permission denied). The linked worktree's Git metadata is outside this session's writable paths. No commit was created. The seven delivered files remain untracked for Claude to review and commit on media-perf. No permissions were changed and no lock file was removed.

