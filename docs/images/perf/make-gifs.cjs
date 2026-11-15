const fs=require('fs'),p=require('path'),cp=require('child_process');
const out=p.join(process.cwd(),'docs/images/perf'),runs=p.join(out,'recordings/round3');
const ff=a=>cp.execFileSync('ffmpeg',['-v','error','-y',...a],{stdio:'inherit'});
let serial=0;
function text(t,x,y,size=22,color='white') {const f=`label-${serial++}.txt`;fs.writeFileSync(p.join(out,f),t);return `drawtext=fontfile='C\\:/Windows/Fonts/segoeui.ttf':textfile='docs/images/perf/${f}':x=${x}:y=${y}:fontsize=${size}:fontcolor=${color}`;}
const results=[];
for(const [name,title,desc] of [
 ['four-layers','Four picture layers','H.264 / HEVC / ProRes / MJPEG | effect LFOs'],
 ['shatter','Shatter','A complete two-second transition'],
 ['clouds','Clouds','A complete two-second transition'],
 ['decoder-effects','Effects from a 10-bit source','HEVC | curves, glow, blur, ripple, vignette, grain, levels, sharpen']]) {
 const dir=p.join(runs,name),rec=p.join(dir,'recordings');
 const src=p.join(rec,fs.readdirSync(rec).filter(n=>n.endsWith('.mp4')).sort().at(-1));
 const dts=[...fs.readFileSync(p.join(dir,'record-profile.log'),'utf8').matchAll(/frame dt=([\d.]+)ms/g)].map(m=>+m[1]);
 const mean=dts.reduce((a,b)=>a+b,0)/dts.length,hz=1000/mean;
 const small=name==='decoder-effects',w=small?480:640,h=small?270:360;
 const trans=['shatter','clouds'].includes(name),elapsed=JSON.parse(fs.readFileSync(p.join(dir,'elapsed.json')));
 const trimStart=trans?Math.max(0,elapsed.takeSeconds-0.2):(name==='four-layers'?1.5:0),trimDuration=trans?2.5:(small?3:3.8);
 const filter=['fps=10',`scale=${w}:${h}:flags=lanczos`,`pad=${w}:${h+110}:0:80:color=0x101923`,text('DECKBOY / '+title,18,12,small?20:24),text(`App loop: ${hz.toFixed(1)} fps`,18,44,21,'0x8fdeb5'),text('Recorded at 1080p',w-216,48,17,'0xbdcbd5'),text(small?'10-bit HEVC | eight effects | ripple LFO':desc,14,h+87,small?15:17,'0xbdcbd5')].join(',');
 ff(['-ss',String(trimStart),'-i',src,'-t',String(trimDuration),'-filter_complex',`${filter},split[a][b];[a]palettegen=max_colors=${small?64:128}:stats_mode=diff[pal];[b][pal]paletteuse=dither=bayer:bayer_scale=3`,'-loop','0',p.join(out,name+'.gif')]);
 const contact=p.join(out,name+'-contact.png');if(fs.existsSync(contact))fs.unlinkSync(contact);
 ff(['-i',p.join(out,name+'.gif'),'-vf',`fps=4,scale=320:${small?253:235},tile=4x3`,'-frames:v','1',p.join(out,name+'-contact.png')]);
 const probe=JSON.parse(cp.execFileSync('ffprobe',['-v','error','-show_streams','-show_format','-of','json',p.join(out,name+'.gif')]));
 const reuse=fs.readFileSync(p.join(dir,'app.log'),'utf8').match(/recording-capture: output \d+ reads output \d+'s composite/);if(!reuse)throw Error('No composite reuse '+name);
 results.push({capture_reuse:reuse[0],trim_start_seconds:trimStart,trim_duration_seconds:trimDuration,recording_resolution:'1920x1080',file:name+'.gif',bytes:fs.statSync(p.join(out,name+'.gif')).size,app_loop_fps:hz,mean_frame_ms:mean,profile_samples:dts.length,source:src,width:probe.streams[0].width,height:probe.streams[0].height,duration:probe.format.duration});
}
fs.writeFileSync(p.join(out,'gif-manifest.json'),JSON.stringify(results,null,2));
console.log(JSON.stringify(results,null,2));
