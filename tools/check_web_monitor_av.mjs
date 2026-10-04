// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real Web Monitor encoder with a simultaneous flash/beep source.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import net from 'node:net';
import http from 'node:http';
import {spawn, spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {pathToFileURL} from 'node:url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const option = (name, fallback) => {
  const i = process.argv.indexOf(name); return i < 0 ? fallback : process.argv[i + 1];
};
const exe = path.resolve(option('--exe', 'build/windows/Release/Deckboy.exe'));
const seconds = Number(option('--seconds', '30'));
const port = Number(option('--port', '5786'));
const webPort = port + 10000;
const root = fs.mkdtempSync(path.join(process.env.RUNNER_TEMP || os.tmpdir(), 'deckboy-web-av-'));
const ffmpeg = option('--ffmpeg', 'ffmpeg');
const ffprobe = option('--ffprobe', 'ffprobe');
const browserModule = option('--playwright', '');
const edgeCases = process.argv.includes('--edge-cases');
// The dummy driver's clock is not a sound card's. A drift seen (or not seen)
// on it says nothing about a real device, so --real-audio uses the machine's.
const realAudio = process.argv.includes('--real-audio');
// With --real-audio, which device the deck plays to: a silent one (an HDMI
// capture input, a virtual cable) keeps a real clock without a room of beeps.
const audioDevice = option('--audio-device', '');
// --share-lan serves on the private LAN address too, and writes the playlist
// URL to lan-url.txt so a second machine can watch over a real network.
const shareLan = process.argv.includes('--share-lan');
// The programme raster, and the fixture's. CI's machines have no GPU and
// render 1080p at about 13 fps, where one frame is 75 ms -- too coarse to
// time lip sync against a 150 ms limit -- so CI tests at 720p. The limits
// stay where they are; the load comes down.
const raster = option('--raster', '1920x1080');
// firefox: the bundled HLS.js path. chrome: the installed Google Chrome as an
// Android phone (Playwright's own Chromium has no H.264/AAC). Phones in the
// field run Chrome, so a Firefox-only pass does not cover them.
const browsers = option('--browsers', 'firefox').split(',').filter(Boolean);
const androidUserAgent = 'Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 ' +
  '(KHTML, like Gecko) Chrome/130.0.0.0 Mobile Safari/537.36';
const passphrase = "Monitor+&%#'test";
const authQuery = '?pin=' + encodeURIComponent(passphrase);
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
function run(args) {
  const result = spawnSync(ffmpeg, args, {encoding: 'utf8', maxBuffer: 32 * 1024 * 1024, windowsHide: true});
  if (result.status !== 0) throw new Error(result.stderr || result.error || 'ffmpeg failed');
  return result;
}
function command(text) {
  return new Promise((resolve, reject) => {
    const socket = net.connect(port, '127.0.0.1'); let reply = ''; let settled = false;
    function done(error) {
      if (settled) return; settled = true; socket.destroy();
      if (error) reject(error); else resolve(reply.trim());
    }
    socket.on('connect', () => socket.write(text + '\n'));
    socket.on('data', bytes => { reply += bytes; socket.setTimeout(100); });
    socket.setTimeout(10000);
    socket.on('timeout', () => done(reply ? null : new Error('command timeout: ' + text)));
    socket.on('error', done); socket.on('end', () => done());
  });
}
async function get(url) {
  const response = await fetch(url, {signal: AbortSignal.timeout(10000)});
  if (!response.ok) throw new Error('HTTP ' + response.status + ' for ' + new URL(url).pathname);
  return response;
}
function capture(url) {
  return new Promise((resolve, reject) => {
    const chunks = []; let timer; let finished = false;
    const began = Date.now();
    function finish(error) {
      if (finished) return;
      finished = true; clearTimeout(timer);
      resolve({bytes:Buffer.concat(chunks),error,elapsed:(Date.now()-began)/1000});
    }
    const request = http.get(url, response => {
      if (response.statusCode !== 200) { response.resume(); reject(new Error('stream HTTP ' + response.statusCode)); return; }
      response.on('data', bytes => chunks.push(bytes));
      response.on('error', finish);
      response.on('end', () => finish(new Error('Stream ended before capture completed')));
      timer = setTimeout(() => { finish(); request.destroy(); }, seconds * 1000);
    });
    request.on('error', finish);
    request.setTimeout(15000, () => request.destroy(new Error('stream stalled')));
  });
}
async function checkBrowser(base, edges = false) {
  if (!browserModule) return;
  for (const name of browsers) await checkOneBrowser(base, edges, name);
}
async function checkOneBrowser(base, edges, browserName) {
  const {firefox, chromium} = await import(pathToFileURL(path.resolve(browserModule)).href);
  const chrome = browserName === 'chrome';
  const browser = chrome ? await chromium.launch({headless:true, channel:'chrome'})
                         : await firefox.launch({headless:true});
  try {
    const page = await browser.newPage({viewport:{width:390,height:844},hasTouch:true,
      ...(chrome ? {isMobile:true, userAgent:androidUserAgent} : {})});
    const errors=[]; page.on('pageerror',error=>errors.push(error.message));
    let segments=0;
    await page.route('**/*.m4s*',async route=>{
      // Reproduce Wi-Fi bursts without slowing the average link below bitrate.
      await sleep(++segments % 6 === 0 ? 650 : 60); await route.continue();
    });
    await page.goto(base+'/' + authQuery);
    await page.waitForFunction(()=>{
      const video=document.querySelector('video');
      return video && video.videoWidth>0 && video.currentTime>1 && !video.paused;
    },null,{timeout:30000});
    await page.click('#sound');
    await page.evaluate(()=>{
      const video=document.querySelector('video');
      window.checkEvents={seeks:0,waits:0,start:video.currentTime};
      video.addEventListener('seeking',()=>window.checkEvents.seeks++);
      video.addEventListener('waiting',()=>window.checkEvents.waits++);
    });
    const watchSeconds = edges ? 3 : Math.max(11, Math.min(60, seconds - 10));
    await sleep(watchSeconds * 1000);
    const playback=await page.evaluate(()=>{
      const video=document.querySelector('video');
      return {...window.checkEvents,advanced:video.currentTime-window.checkEvents.start,
        syncErrorMs:window.deckboyMonitorSyncErrorMs,
        muted:video.muted,width:video.videoWidth,height:video.videoHeight,
        buffered:video.buffered.length ? video.buffered.end(video.buffered.length-1)-video.currentTime:0};
    });
    for(const [name,width,height] of [['phone',390,844],['landscape',844,390],['tv',1920,1080]]) {
      await page.setViewportSize({width,height}); await sleep(250);
      const fits=await page.evaluate(()=>{
        const video=document.querySelector('video').getBoundingClientRect();
        const controls=document.querySelector('.monitor-controls').getBoundingClientRect();
        return video.left>=0 && video.top>=0 && video.right<=innerWidth+1 &&
          video.bottom<=controls.top+1 && controls.bottom<=innerHeight+1 &&
          document.documentElement.scrollWidth<=innerWidth;
      });
      await page.screenshot({path:path.join(root,browserName+'-'+name+'.png')});
      if(!fits) throw new Error(name+' viewport clips programme or controls');
    }
    console.log((chrome ? 'Chrome (Android)' : 'Firefox') + ' mobile playback with jitter:',
                JSON.stringify(playback),'errors:',JSON.stringify(errors));
    fs.writeFileSync(path.join(root,'browser-'+browserName+'.json'),JSON.stringify({playback,errors},null,2));
    if(playback.advanced<watchSeconds-2 || playback.seeks>2 || playback.waits>1 || playback.muted || errors.length)
      throw new Error('Browser playback did not remain smooth and audible');
    // WALL-CLOCK STEP: every screen aims at now minus the same delay, so one
    // that holds its target within this is in step with the others.
    if(!edges && (typeof playback.syncErrorMs!=='number' || Math.abs(playback.syncErrorMs)>150))
      throw new Error('Player not holding the shared wall-clock target: '+playback.syncErrorMs+' ms');
    if (edges) {
      // A paused programme still supplies a continuous muxed stream.
      await command('PAUSE');
      let before = await page.evaluate(()=>document.querySelector('video').currentTime);
      await sleep(7000);
      let after = await page.evaluate(()=>document.querySelector('video').currentTime);
      if (after-before < 5) throw new Error('Monitor stopped when programme paused');
      await command('SEEK 0'); await command('PLAY');
      await sleep(7000);
      before = await page.evaluate(()=>document.querySelector('video').currentTime);
      await sleep(3000);
      after = await page.evaluate(()=>document.querySelector('video').currentTime);
      if (after-before < 2) throw new Error('Monitor did not continue after programme resumed');
      await command('WEBMONITOR OFF'); await sleep(3000);
      await command('WEBMONITOR ON');
      await sleep(16000);
      before = await page.evaluate(()=>document.querySelector('video').currentTime);
      await sleep(3000);
      const recovered = await page.evaluate(()=>({time:document.querySelector('video').currentTime,
        muted:document.querySelector('video').muted,error:document.querySelector('video').error?.message}));
      if (recovered.time-before < 2 || recovered.muted || recovered.error)
        throw new Error('Monitor failed to recover after encoder restart: '+JSON.stringify(recovered));
      await page.click('#reconnect');
      await page.waitForFunction(()=>{const v=document.querySelector('video');return v.readyState>=3 && !v.paused;},null,{timeout:20000});
      console.log('Programme pause/resume, encoder restart and manual reconnect: passed');
    }
  } finally { await browser.close(); }
}
fs.mkdirSync(path.join(root, 'data'), {recursive:true});
for (const name of ['themes', 'ui', 'sprites', 'fonts', 'web']) {
  const source = path.join(repo, 'data', name);
  if (fs.existsSync(source)) fs.cpSync(source, path.join(root, 'data', name), {recursive:true});
}
for (const name of fs.readdirSync(path.join(repo, 'data'))) {
  if (/\.(ttf|otf)$/i.test(name)) fs.copyFileSync(path.join(repo, 'data', name), path.join(root, 'data', name));
}
const suppliedFixture = option('--fixture', '');
const clip = suppliedFixture ? path.resolve(suppliedFixture) : path.join(root, 'sync.mp4');
console.log('Building flash/beep fixture; evidence:', root);
if (!suppliedFixture) run(['-v','error','-y','-f','lavfi','-i',`testsrc2=s=${raster}:r=30:d=${seconds+30}`,
  '-f','lavfi','-i',`sine=frequency=1000:sample_rate=48000:duration=${seconds+30}`,
  '-filter_complex',"[0:v]drawbox=x=0:y=0:w=iw:h=ih:c=white:t=fill:enable='lt(mod(t,1),0.1)'[v];[1:a]volume=0:enable='gte(mod(t,1),0.1)'[a]",
  '-map','[v]','-map','[a]','-c:v','libx264','-preset','ultrafast','-crf','18','-pix_fmt','yuv420p',
  '-c:a','aac','-shortest',clip]);
const log = fs.openSync(path.join(root,'app.log'),'w');
const show = path.join(root,'data','default.deckboy');
const app = spawn(exe, ['--allow-multi-instance',show,'--import',clip], {cwd:path.dirname(exe), windowsHide:true,
  // FFREPORT is inherited by every ffmpeg the app starts, so an encoder that
  // dies leaves its own reason beside the other evidence. (The app discards
  // ffmpeg's stderr, and CI keeps no screen.) Colons in the path need escaping.
  env:{...process.env, FFREPORT:'file='+path.join(root,'ffreport-%p.log').split(path.sep).join('/').split(':').join('\\:')+':level=24',
    ...(realAudio ? {} : {SDL_AUDIODRIVER:'dummy'}), DECKBOY_ROOT:root, DECKBOY_PROJECT:show, DECKBOY_COMPANION_PORT:String(port)},
  stdio:['ignore',log,log]});
let startupError;
app.on('error', error => { startupError = error; });
try {
  let ready = false;
  for (let i=0; i<100; i++) {
    if (startupError) throw startupError;
    try { await command('HELP'); ready=true; break; } catch { await sleep(500); }
  }
  if (!ready) throw new Error('Deckboy did not start');
  const startup = ['OUTPUT ON',`VIDEO ${raster}`,`WEBMONITOR PORT ${webPort}`,`WEBMONITOR PIN ${passphrase}`,
    ...(shareLan ? ['WEBMONITOR SHARE ON'] : []),
    ...(realAudio && audioDevice ? [`AUDIO ${audioDevice}`] : []),
    'SELECT 1','TAKE','MASTERVOL 100','WEBMONITOR ON'];
  for (const cmd of startup) {
    const reply = await command(cmd); if (!reply.startsWith('OK')) throw new Error(cmd + ': ' + reply);
  }
  await sleep(5000);
  // Shared on the network, the monitor listens ONLY on the private LAN
  // address -- loopback is refused by design -- so the test goes there too.
  let host = '127.0.0.1';
  if (shareLan) {
    const lanHost = (await command('WEBMONITOR')).match(/http:\/\/([0-9.]+):/);
    if (!lanHost) throw new Error('No LAN address reported');
    host = lanHost[1];
  }
  const base = `http://${host}:${webPort}`;
  const page = await (await get(base+'/' + authQuery)).text();
  fs.writeFileSync(path.join(root,'player.html'),page);
  const match = page.match(/\/av\/(\d+)/) || page.match(/\/hls\/(\d+)\//);
  if (!match) throw new Error('No programme player in home page');
  const playlistUrl = base + '/hls/' + match[1] + '/index.m3u8' + authQuery;
  if (shareLan) {
    const lanUrl = playlistUrl;
    fs.writeFileSync(path.join(root, 'lan-url.txt'), lanUrl);
    console.log('LAN playlist:', lanUrl.replace(/pin=[^&]*/, 'pin=...'), '->', path.join(root, 'lan-url.txt'));
  }
  // READY WHEN IT SAYS SO. The encoder's first fragment can take well over
  // five seconds on a slow machine; the playlist answers 503 until then.
  let playlist = '';
  for (const until = Date.now() + 30000; Date.now() < until;) {
    const response = await fetch(playlistUrl, {signal: AbortSignal.timeout(5000)}).catch(() => null);
    if (response && response.ok) { playlist = await response.text(); break; }
    if (response) await response.arrayBuffer();
    await sleep(500);
  }
  if (!playlist) {
    // What the app thinks of its own outputs is the only clue CI leaves.
    const status = await command('STATUS').catch(error => String(error));
    const outputs = status.split('\n').filter(line => line.startsWith('OUTPUT '))
      .map(line => line.replace(/(?:url|key|path)="[^"]*"/g, '').replace(/ ndi[^ ]*="[^"]*"/g, ''));
    throw new Error('HLS playlist not ready after 30 s\n' + outputs.join('\n'));
  }
  fs.writeFileSync(path.join(root, 'index.m3u8'), playlist);
  const resources = [base + '/', base + '/web/player.js', base + '/web/hls.light.min.js',
    playlistUrl.split('?')[0], base + '/av/' + match[1]];
  const init = playlist.match(/#EXT-X-MAP:URI="([^"]+)"/);
  const fragment = playlist.split('\n').find(line => line && !line.startsWith('#'));
  if (!init || !fragment) throw new Error('HLS playlist lacks initialization or media');
  for (const reference of [init[1], fragment]) {
    const url = new URL(reference, playlistUrl).href;
    const response = await get(url);
    if ((await response.arrayBuffer()).byteLength < 8) throw new Error('Empty HLS resource');
    resources.push(url.split('?')[0]);
  }
  for (const url of resources) {
    for (const suffix of ['', '?pin=wrong', '?pin=%ZZ']) {
      const response = await fetch(url + suffix, {signal:AbortSignal.timeout(5000)});
      await response.arrayBuffer();
      if (response.status !== 401) throw new Error('Authentication failed for ' + new URL(url).pathname);
    }
  }
  console.log('HLS resources and passphrase authentication: passed');
  // Independently decode the native-player URL, not just the raw MP4 tap.
  run(['-v','error','-rw_timeout','15000000','-i',playlistUrl,'-t','3',
    '-map','0:v:0','-map','0:a:0','-f','null','-']);
  console.log('Native HLS URL: video and audio decoded');
  console.log('Capturing',seconds,'seconds from output',match[1]);
  const telemetry=[]; let polling=true;
  const poll=(async()=>{
    while(polling) {
      try {
        const status=await command('STATUS');
        const manifest=await (await get(playlistUrl)).text();
        telemetry.push({at:Date.now(),outputs:status.split('\n').filter(x=>x.startsWith('OUTPUT ') || x.startsWith('DECK ')),
          sequence:manifest.match(/#EXT-X-MEDIA-SEQUENCE:(\d+)/)?.[1],
          generation:manifest.match(/#EXT-X-DISCONTINUITY-SEQUENCE:(\d+)/)?.[1]});
      } catch(error) {telemetry.push({at:Date.now(),error:String(error)});}
      await sleep(2000);
    }
  })();
  const [captureResult, browserResult] = await Promise.allSettled([capture(base+'/av/'+match[1]+authQuery),checkBrowser(base)]);
  polling=false; await poll;
  fs.writeFileSync(path.join(root,'telemetry.json'),JSON.stringify(telemetry,null,2));
  if (captureResult.status === 'rejected') throw captureResult.reason;
  const {bytes,error:captureError,elapsed} = captureResult.value;
  let offset=0, end=0;
  while (offset+8<=bytes.length) {
    const n=bytes.readUInt32BE(offset);
    if (n<8 || offset+n>bytes.length) break;
    if (bytes.toString('ascii',offset+4,offset+8)==='mdat') end=offset+n;
    offset+=n;
  }
  if (!end) throw new Error('No complete fragments');
  const recording=path.join(root,'web.mp4'); fs.writeFileSync(recording,bytes.subarray(0,end));
  const status=await command('STATUS');
  console.log(status.split('\n').filter(x=>x.startsWith('OUTPUT ')).map(x=>x.replace(/(?:url|key|path)="[^"]*"/g,'')).join('\n'));
  const flashes=[...run(['-v','info','-i',recording,'-vf','negate,blackdetect=d=0.02:pic_th=0.85','-an','-f','null','-']).stderr.matchAll(/black_start:(\d+\.?\d*)/g)].map(x=>Number(x[1]));
  const audioEvents=[...run(['-v','info','-i',recording,'-af','silencedetect=n=-40dB:d=0.05','-vn','-f','null','-']).stderr.matchAll(/silence_(start|end): (\d+\.?\d*)/g)];
  // silencedetect emits a final silence_end at EOF even when no beep follows.
  // Require a complete audible interval before matching its leading edge.
  const beeps=audioEvents.filter((event,i)=>event[1]==='end' && audioEvents[i+1]?.[1]==='start' &&
    Number(audioEvents[i+1][2])-Number(event[2])>=0.03).map(event=>Number(event[2]));
  const offsets=beeps.map(beep=>flashes.reduce((best,flash)=>Math.abs(flash-beep)<Math.abs(best)?flash-beep:best,Infinity)).filter(x=>Math.abs(x)<0.5);
  if (offsets.length<Math.max(5,seconds/2)) {
    // Say WHICH half is missing, and keep what was found: "0 markers" alone
    // could be no picture, no sound or no pairing, and CI keeps no screen.
    fs.writeFileSync(path.join(root,'metrics.json'),JSON.stringify({flashes,beeps},null,2));
    throw new Error('Too few matched flash/beep markers: '+offsets.length+' (flashes '+flashes.length+
      ', beeps '+beeps.length+', first flash '+flashes[0]+', first beep '+beeps[0]+')');
  }
  const median=items=>[...items].sort((a,b)=>a-b)[Math.floor(items.length/2)];
  const early=median(offsets.slice(0,5)), late=median(offsets.slice(-5));
  const probe=spawnSync(ffprobe,['-v','error','-show_streams','-of','json',recording],{encoding:'utf8',windowsHide:true});
  const streams=JSON.parse(probe.stdout).streams;
  // THE TREND, NOT TWO ENDS. First-five against last-five hides a drift that
  // corrects itself in steps (a sawtooth). A least-squares slope over every
  // marker is what a slow rate error looks like: 51 ppm read as "25 ms" over
  // ten minutes and is 174 ms an hour.
  const times=beeps.slice(0,offsets.length);
  const meanT=times.reduce((a,b)=>a+b,0)/times.length, meanO=offsets.reduce((a,b)=>a+b,0)/offsets.length;
  const slope=times.reduce((s,t,i)=>s+(t-meanT)*(offsets[i]-meanO),0)/
              Math.max(1e-9,times.reduce((s,t)=>s+(t-meanT)**2,0));
  const perMinute=[];
  for (let m=0; m*60<times[times.length-1]; ++m) {
    const seg=offsets.filter((_,i)=>times[i]>=m*60&&times[i]<(m+1)*60);
    if (seg.length) perMinute.push(Math.round(median(seg)*1000));
  }
  const metrics={captureSeconds:elapsed,markers:offsets.length,medianMs:median(offsets)*1000,earlyMs:early*1000,lateMs:late*1000,
    driftMs:(late-early)*1000,driftMsPerHour:slope*3600*1000,perMinuteMedianMs:perMinute,
    minMs:Math.min(...offsets)*1000,maxMs:Math.max(...offsets)*1000,audioDriver:realAudio?'device':'dummy',
    streams:streams.map(s=>({type:s.codec_type,width:s.width,height:s.height,rate:s.r_frame_rate,sampleRate:s.sample_rate,channels:s.channels,start:s.start_time,duration:s.duration}))};
  fs.writeFileSync(path.join(root,'metrics.json'),JSON.stringify({metrics,flashes,beeps},null,2));
  console.log(JSON.stringify(metrics,null,2));
  if (Math.abs(late-early)>0.05 || Math.abs(median(offsets))>0.08 ||
      offsets.some(offset=>Math.abs(offset)>0.15)) process.exitCode=1;
  // A slope is only meaningful over minutes; past five, more than 30 ms an
  // hour of drift fails (a show is longer than a test).
  if (elapsed>=300 && Math.abs(slope*3600)>0.03) {
    console.log('A/V drift trend '+(slope*3600*1000).toFixed(0)+' ms/hour exceeds 30 ms/hour');
    process.exitCode=1;
  }
  if (captureError) throw captureError;
  if (browserResult.status === 'rejected') throw browserResult.reason;
  if (edgeCases) {
    await command('SEEK 0'); await command('PLAY');
    await checkBrowser(base, true);
  }
} finally {
  try { await command('WEBMONITOR OFF'); } catch {}
  app.kill(); fs.closeSync(log);
  console.log('Evidence retained:',root);
}
