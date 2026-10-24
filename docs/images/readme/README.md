# Deckboy application captures

## Published release 0.99.419 / 23 October 2026

The homepage, README and current demo gallery use `release-419/` captures from
the published Windows portable package. The package SHA-256 is
`cb44ef60c9b985b6692fee5e6527c1c8b8582eded0576fe09e46e45256443f77`, matching GitHub's
release digest. The startup screen visibly reports v0.99.419.

| Files in `release-419/` | What they show |
| --- | --- |
| `example-02-gameboy.png`, `example-02-dark.png`, `example-02-virtual-boy.png` | Three layers from the bundled layers and titles show, at 150% UI scale |
| `deckboy-video-playback.gif`, `.png`, `deckboy-video-playback-ymck.mp4`, `.png` | Video playback with the next cue selected; Tears of Steel footage, with YMCK music in the MP4 |
| `live-effects.gif`, `.mp4`, `.png` | Kaleidoscope, RGB split and ripple from the effects playground |
| `picture-scopes.gif`, `.mp4`, `.png` | Scopes on Deckboy's built-in moving test card; output and analysis counters are visible |
| `warp-monitor.gif`, `.mp4`, `.png` | Perspective corner adjustments shown in the programme monitor |
| `startup-loop.gif`, `.mp4`, `.png`, `startup-example-shows.png` | The opening screen and EXAMPLE SHOWS |
| `language-greetings.gif`, `.mp4`, `.png` | Danish, 1337 and Japanese greetings |

All pictures come from the released application's UI snapshot or programme
recorder. Capture runs use isolated show/state directories and dummy audio.
The video-playback GIF is silent; its MP4 has a YMCK music excerpt. The other
MP4s and GIFs are silent and use generated or bundled example-show material.
No reconstructed interface, altered counter, reversed footage or
motion interpolation is used. UI samples retain their measured elapsed time.
GIFs use a 10 fps encoding clock and a half-second dissolve at the loop boundary;
their playback rate is separate from the application's visible live counters.
The 1280 x 720 source raster and measured analysis/output rates are visible in the scopes capture. The moving test-card generator and the 60 Hz output have separate cadences; the GIF is a feature demonstration, not a throughput benchmark.
See `release-419/media-manifest.json` for exact dimensions, durations and hashes.

The video-playback capture shows the launch scene from
[Tears of Steel](https://mango.blender.org/), © Blender Foundation,
[CC BY 3.0](https://creativecommons.org/licenses/by/3.0/). The footage is trimmed,
shown inside the application, and its original audio removed. The 24 fps film
plays on a 60 Hz output. The GIF is 6.5 seconds at 10 fps; the MP4 is 6 seconds.
The MP4 music is YMCK — *Family Music* (2004), used with permission, with a
visible credit below the application picture. Music permission is separate
from the film and software licences. See `release-419/playback-credits.txt`.

The site serves H.264 MP4s with playback controls and no autoplay. GitHub uses
GIF derivatives. The v0.99.397 and v0.99.374 trailers remain labelled as earlier
films with their original credits and reuse terms. The tracker, phone, fixtures,
Mic, Mini and output-only feature demonstrations below retain their capture dates.

## Earlier captures

| Files | Capture | What they show |
| --- | --- | --- |
| `desk-gameboy.png`, `desk-dark.png`, `desk-virtual-boy.png` | v0.99.417, 18 October | The public Windows app at 150% UI scale, 1600 × 900 exports |
| `master-tracker.png` | v0.99.417, 18 October | Pattern tracker with cue, volume, opacity, fade and stop cells and named sections |
| `phone-controller.png` | v0.99.417, 18 October | Complete phone controller connected to an isolated desk |
| `phone-controller.mp4`, `phone-controller-poster.png` | v0.99.417, 18 October | Phone selecting standby cues and pressing TAKE; actual browser timing retained |
| `taking-cues.mp4`, `.gif`, `.png` | v0.99.414, 17 October | A generated Swirl clip, a still and a local web page taken from the cue list |
| `live-effects.mp4`, `.gif`, `.png` | 16 October | A kaleidoscope on the Swirl clip, with an LFO moving a parameter |
| `picture-scopes.mp4`, `.gif`, `.png` | 16 October | A moving test pattern with waveform and vectorscope |
| `fixture-patch.png` | 16 October | Named dimmer, RGBW and dimmer/RGB fixtures |
| `dmx-by-fixture.mp4`, `.gif`, `.png` | 16 October | A DMX cue changing fixture level and colour |
| `mic-inspector.mp4`, `.gif`, `.png` | 16 October | A Mic cue's gain and meter, explicitly using its built-in test signal |

MP4 clips are silent H.264 with browser-compatible pixel format and fast-start metadata.
These earlier GIFs are 12 fps derivatives for GitHub. The site uses the compact MP4s with controls,
without autoplay. Screenshots and video frames have been reviewed before publication.

The separately hosted one-minute trailer remains the finished v0.99.397 film and
retains its original credits. These captures are not a replacement trailer.
