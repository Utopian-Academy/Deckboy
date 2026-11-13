const fs=require('fs'),path=require('path'),cp=require('child_process'),net=require('net');
const root=process.cwd(), work=path.join(root,'docs/images/perf/recordings/round3');
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const exes={new:'C:/Users/james/deckboy-audit-1111/build/windows/Release/Deckboy.exe',old:'C:/Users/james/deckboy-baseline-1112/build/windows/Release/Deckboy.exe'};
function ff(args){cp.execFileSync('ffmpeg',['-v','error','-y',...args],{stdio:'inherit'});}
async function start(build,name){
 const dir=path.join(work,name);fs.mkdirSync(dir,{recursive:true});
 const show=path.join(dir,'show.deckboy');let seed='update_check\t0\nui_sounds\t0\nui_scale\t1.0\n';
 if(name.startsWith('grade-')||name.startsWith('key-')){seed=fs.readFileSync(path.join(work,'probe-new/show.deckboy'),'utf8').split('\n').map(l=>{if(!l.startsWith('cue\t'))return l;let f=l.split('\t');if(name.startsWith('grade-')){f[50]='1.25';f[51]='1.2';f[52]='1.3'}else f[46]='1';return f.join('\t')}).join('\n');}
 fs.writeFileSync(show,seed);
 const port=19000+Math.floor(Math.random()*8000),log=fs.openSync(path.join(dir,'app.log'),'w');
 const proc=cp.spawn(exes[build],['--allow-multi-instance',show,'--ui-size','1366','768'],{cwd:path.dirname(exes[build]),env:{...process.env,DECKBOY_ROOT:dir,DECKBOY_STATE_DIR:path.join(dir,'state'),DECKBOY_PROJECT:show,DECKBOY_COMPANION_PORT:''+port,SDL_AUDIODRIVER:'dummy',SDL_AUDIO_DRIVER:'dummy',DECKBOY_UI_PROFILE:'2',DECKBOY_HIDE_WINDOWS:'0',DECKBOY_OUTPUT_ON_TOP:'1'},stdio:['ignore',log,log]});
 const send=cmd=>new Promise((resolve,reject)=>{let data='',timer;const s=net.createConnection({host:'127.0.0.1',port},()=>s.write(cmd+'\n'));s.on('data',b=>{data+=b;clearTimeout(timer);timer=setTimeout(()=>{s.destroy();fs.appendFileSync(path.join(dir,'commands.log'),cmd+' => '+data+'\n');data.startsWith('ERR')?reject(Error(data)):resolve(data.trim());},80)});s.on('error',reject);s.setTimeout(20000,()=>{s.destroy();reject(Error('timeout '+cmd))});});
 for(let i=0;i<150;i++){try{await send('PING');break;}catch(e){if(i===149)throw e;await sleep(200);}}
 await send('MASTERVOL 0');return {dir,show,send,proc,stop(){proc.kill();fs.closeSync(log)}};
}
async function commands(db,arr){for(const c of arr)await db.send(c)}
async function imp(db,file){const drop=path.join(db.dir,'drop.txt');fs.writeFileSync(drop,file+'\n');await db.send('DROPTEST '+drop);for(let i=0;i<200;i++){const r=await db.send('DROPTEST');if(r.includes('pending=0')&&r.includes('probes=0'))break;await sleep(200)}await sleep(500)}

const media=path.join(root,'build/media-perf');
async function main(){for(const mode of (process.env.PERF_MODES||'four-layers,shatter,clouds,decoder-effects').split(',')){
 const db=await start('new',mode);try{
 await imp(db,path.join(media,mode==='decoder-effects'?'tenbit.mkv':'base.mp4'));await imp(db,path.join(media,'second.mp4'));
 await commands(db,['DECK 1','SELECT 1','LOOP ON','SELECT 2','LOOP ON','OUTPUT SELECT 1','VIDEO 1920X1080','OUTPUT ON','FULLSCREEN','VIDEO 1920X1080','SELECT 1','TAKE']);
 if(mode==='four-layers')for(let i=0;i<4;i++){await commands(db,['DECKADD','DECK '+(i+2)]);await imp(db,path.join(media,['pip-h264.mp4','pip-hevc.mkv','pip-prores.mov','pip-mjpeg.avi'][i]));await commands(db,['SELECT 1','LOOP ON','SCALE 0.36','OFFSETX '+(i%2?500:-500),'OFFSETY '+(i<2?-280:280),'FX ADD '+['blur','glow','kaleidoscope','mosaic'][i],'FX LFO 1 A on','FX LFO 1 A rate 0.25','VIDEO OUTPUT ASSIGN','TAKE']);}
 if(mode==='decoder-effects')await commands(db,['DECK 1','SELECT 1',...['curves','glow','blur','ripple','vignette','grain','levels','sharpen'].map(f=>'FX ADD '+f),'FX LFO 4 A on','FX LFO 4 A rate 0.2']);
 const trans=['shatter','clouds'].includes(mode);if(trans)await commands(db,['TRANSITION 2','TRANSITIONSTYLE '+mode,'SELECT 2']);
 await commands(db,['DECK 1','OUTPUT SELECT 1','VIDEO 1920X1080']);await sleep(3000);await db.send('RECFORMAT 1920x1080 60');await db.send('RECORD on');
 const begun=Date.now(),mark=fs.readFileSync(path.join(db.dir,'app.log'),'utf8').length;await sleep(500);let takeSeconds=null;if(trans){takeSeconds=(Date.now()-begun)/1000;await db.send('TAKE');}
 for(let i=0;i<6;i++){await sleep(700);await db.send('STATUS');}
 fs.writeFileSync(path.join(db.dir,'record-profile.log'),fs.readFileSync(path.join(db.dir,'app.log'),'utf8').slice(mark));await db.send('RECORD off');fs.writeFileSync(path.join(db.dir,'elapsed.json'),JSON.stringify({recordCommandSeconds:(Date.now()-begun)/1000,takeSeconds}));await sleep(3000);const reuse=fs.readFileSync(path.join(db.dir,'app.log'),'utf8').match(/recording-capture: output \d+ reads output \d+'s composite/);if(!reuse)throw Error('Composite reuse not confirmed: '+mode);console.log(mode,reuse[0],db.dir);
 }finally{db.stop();await sleep(1000)}
}}
main().catch(e=>{console.error(e);process.exitCode=1});
