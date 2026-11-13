const fs=require('fs'),p=require('path'),crypto=require('crypto');
const root=p.resolve('docs/images/perf');
const manifest=JSON.parse(fs.readFileSync(p.join(root,'gif-manifest.json')));
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const exe='C:/Users/james/deckboy-audit-1111/build/windows/Release/Deckboy.exe';
for(const m of manifest){m.sha256=hash(p.join(root,m.file));m.executable_sha256=hash(exe);m.source=p.relative(p.resolve('.'),m.source).split(p.sep).join('/');}
fs.writeFileSync(p.join(root,'gif-manifest.json'),JSON.stringify(manifest,null,2));
const rows=manifest.map(m=>`| ${m.file} | ${m.bytes} | ${m.width} x ${m.height} | ${m.duration} | ${m.app_loop_fps.toFixed(1)} |`).join('\n');
const metrics=manifest.map(m=>`| ${m.file} | ${m.profile_samples} | ${m.mean_frame_ms} | ${m.app_loop_fps} |`).join('\n');
const old=fs.readFileSync(p.join(root,'NOTES.md'),'utf8');
const retained=old.slice(old.indexOf('## Retained stills and timing card'));
const evidence=manifest.map(m=>{const name=m.file.replace('.gif',''),dir=p.join(root,'recordings/round3',name),log=fs.readFileSync(p.join(dir,'record-profile.log'),'utf8');const lines=log.split('\n').filter(l=>l.includes('frame dt='));lines.sort((a,b)=>parseFloat(b.match(/frame dt=([\d.]+)/)[1])-parseFloat(a.match(/frame dt=([\d.]+)/)[1]));return `### ${m.file}\n\nComposite reuse confirmed in app.log: \`${m.capture_reuse}\`.\n\n${m.app_loop_fps<55?'Below 55 fps. The following are the five slowest frame profile lines from this same recording; component times include the waits they cover.':'Above 55 fps in this short recording. Slowest frame profile lines are retained here for review.'}\n\n\`\`\`text\n${lines.slice(0,5).join('\n')}\n\`\`\`\n`;}).join('\n');
fs.writeFileSync(p.join(root,'NOTES.md'),`# Performance captures, round three, 13 November 2026

All four GIFs were re-recorded with the supplied NEW Deckboy.exe (build supplied as commit ce7b90fe). Changes are confined to docs/images/perf/ and left uncommitted for Claude. No external network access, push, merge or tag was used. Loopback TCP controlled isolated app instances. No Portal was used.

## Moving pictures

| File | Bytes | Dimensions | Seconds | Badge: app-loop fps |
| --- | ---: | --- | ---: | ---: |
${rows}

Every source was recorded at 1920x1080 with RECFORMAT 1920x1080 60. Retained STATUS replies confirm window_px=1920x1080 and render_px=1920x1080 for output and recorder. Each GIF says Recorded at 1080p. The GIF encoding clock is 10 fps; the badge is the measured app-loop rate, calculated as 1000 divided by the arithmetic mean of frame dt in record-profile.log from that same recording. It covers the recording window, including the parts subsequently trimmed, and is not a claim of sustained throughput or minimum frame rate.

| Capture | Profile samples | Mean frame interval, ms | Unrounded app-loop fps |
| --- | ---: | ---: | ---: |
${metrics}

Shatter and Clouds are trimmed to contiguous 2.5-second excerpts: approximately 0.2 seconds before TAKE, the complete two-second transition, and 0.3 seconds afterward. Command-response latency and recording startup may shift these holds slightly. No speed change, interpolation or fabricated loop dissolve is used. Four Layers retains seconds 1.5 through 5.3; Decoder Effects retains the first three seconds. New contact sheets sample at four frames per second, with unused cells padded. All four new contact sheets were opened and visually reviewed for badges, content and transition coverage. The Four Layers sheet shows the first three seconds of its 3.8-second loop.

Four Layers uses a base picture plus four mixed-codec PIPs: H.264/blur, HEVC/glow, ProRes/kaleidoscope and MJPEG/mosaic. Each effect parameter A has a 0.25 Hz LFO. Decoder Effects uses a 3840x2160 HEVC yuv420p10le source and curves, glow, blur, ripple, vignette, grain, levels and sharpen, with a 0.2 Hz ripple LFO. The output is SDR. Decoder Effects is encoded at 480 pixels wide with 64 palette colours; the other GIFs at 640 pixels with 128 colours.

## Capture evidence

${evidence}
## Reproduction and provenance

Run from the worktree root:

\`node docs/images/perf/record-gifs.cjs\`

\`node docs/images/perf/make-gifs.cjs\`

\`node docs/images/perf/finalize.cjs\`

The recorder checks the composite-reuse log line in every run and fails if absent. Raw MP4s, app.log, record-profile.log, complete commands/replies, elapsed metadata and isolated shows are under recordings/round3/<scene>/. The profile slice starts after RECORD on replies and ends before RECORD off. All four recording processes exit before GIF encoding begins. Earlier recordings remain separately under recordings/final/.

Each app uses MASTERVOL 0, SDL_AUDIODRIVER=dummy, SDL_AUDIO_DRIVER=dummy, update_check=0 and ui_sounds=0, with isolated root, state, project and loopback port. Recording has no overlay, matte, captions or NDI of its own. Exact environment, setup, import, effect, transition and recording commands are in record-gifs.cjs and commands.log. make-gifs.cjs holds the exact trims, drawtext badges, palette settings and contact-sheet commands. gif-manifest.json includes hashes, source paths, dimensions, trim intervals and measured values.

Executable: ${exe}

SHA-256: ${hash(exe)}

The three stills, measurements.csv and earlier timing card are retained from the previous round; they were not remeasured for these GIFs.

${retained}`);
for(const n of fs.readdirSync(root))if(/^label-\d+\.txt$/.test(n))fs.unlinkSync(p.join(root,n));
