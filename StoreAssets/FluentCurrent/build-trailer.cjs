// Continuous Fluent ribbons, eased entrances, floating real UI; no fabricated results.
const fs=require('node:fs/promises');
const path=require('node:path');
const {spawn}=require('node:child_process');
let sharp;try{sharp=require('sharp');}catch{sharp=require('../../analysis-output/store-assets-render/node_modules/sharp');}
const root=__dirname,out=path.join(root,'import'),work=path.join(root,'motion');
const font="Segoe UI,Microsoft YaHei UI,Yu Gothic UI,sans-serif";
const esc=s=>s.replaceAll('&','&amp;').replaceAll('<','&lt;');
const txt=(s,x,y,size=60,color='#F7FBFF',weight=600)=>`<text x="${x}" y="${y}" fill="${color}" font-family="${font}" font-size="${size}" font-weight="${weight}">${s.split('\n').map((line,i)=>`<tspan x="${x}" dy="${i?size*1.25:0}">${esc(line)}</tspan>`).join('')}</text>`;
const svg=(body,w=1920,h=1080)=>`<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="${w}" height="${h}">${body}</svg>`;
async function png(body,file,w,h){await sharp(Buffer.from(svg(body,w,h))).png().toFile(path.join(work,file));}
async function data(file){return 'data:image/png;base64,'+(await fs.readFile(file)).toString('base64');}
async function main(){
 await fs.mkdir(work,{recursive:true});await fs.mkdir(out,{recursive:true});
 const logo=await data(path.join(root,'../Icon/AppIcon-master.png'));
 const logoAt=(x,y,s)=>`<svg x="${x}" y="${y}" width="${s}" height="${s}" viewBox="0 0 300 300"><defs><clipPath id="logoClip"><rect width="300" height="300" rx="44"/></clipPath></defs><image xlink:href="${logo}" width="300" height="300" clip-path="url(#logoClip)"/></svg>`;
 const brand=logoAt(88,66,76)+txt('Bluetooth Audio Codec Viewer',186,113,28,'#E8F2FA',500);
 const shots=[];
 for(const name of ['ready','details','help']){
  const file=path.join(root,'source/viewer-'+name+'.png');
  const height=name==='ready'?530:614;
  const b=await sharp(file).extract({left:0,top:0,width:847,height}).png().toBuffer();
  shots.push({url:'data:image/png;base64,'+b.toString('base64'),w:847,h:height});
 }
 const ui=(s,x,y,w)=>`<image x="${x}" y="${y}" width="${w}" height="${w*s.h/s.w}" xlink:href="${s.url}"/><rect x="${x}" y="${y}" width="${w}" height="${w*s.h/s.w}" rx="10" fill="none" stroke="#82A0B8" stroke-opacity=".45"/>`;
 await png(`<defs><radialGradient id="g" cx=".7" cy=".3" r=".9"><stop stop-color="#184659"/><stop offset=".55" stop-color="#182A40"/><stop offset="1" stop-color="#101A28"/></radialGradient></defs><rect width="1920" height="1080" fill="url(#g)"/>`,'background.png');
 await png(`<defs><linearGradient id="r" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#00BCDA" stop-opacity=".03"/><stop offset=".5" stop-color="#33B6D4" stop-opacity=".19"/><stop offset="1" stop-color="#508CFF" stop-opacity=".02"/></linearGradient></defs><path d="M-180 110C360-150 720 740 1460 250S2300-70 2580 330L2580 720C2040 210 1760 930 1160 670S270 0-180 520Z" fill="url(#r)"/><path d="M-180 110C360-150 720 740 1460 250S2300-70 2580 330" fill="none" stroke="#65D9E3" stroke-opacity=".16" stroke-width="2"/>`,'ribbon-a.png',2400,1400);
 await png(`<defs><linearGradient id="r"><stop stop-color="#6C80F9" stop-opacity=".12"/><stop offset=".6" stop-color="#278BC2" stop-opacity=".15"/><stop offset="1" stop-color="#159DAD" stop-opacity="0"/></linearGradient></defs><path d="M-100 1000C250 550 630 1230 1260 680S2200 620 2500 830L2500 1400H-100Z" fill="url(#r)"/><path d="M-100 1000C250 550 630 1230 1260 680S2200 620 2500 830" fill="none" stroke="#8EADFA" stroke-opacity=".12" stroke-width="2"/>`,'ribbon-b.png',2400,1400);
 const scenes=[
  brand+txt('See the codec.\nUnderstand\nyour audio.',100,350,86)+txt('A focused viewer for your\nBluetooth headphones.',103,725,32,'#B8CEDD',400)+logoAt(1110,230,600)+txt('Bluetooth Classic · A2DP',103,970,26,'#9CBACD',400),
  brand+txt('Connect.\nThen detect.',90,350,64)+txt('A short local detection.\nA temporary helper asks\nfor administrator approval.',94,590,29,'#B8CEDD',400)+ui(shots[0],720,195,1100)+txt('Real application · Ready state shown',720,1008,22,'#A8C1D2',400),
  brand+txt('Look inside\nthe connection.',90,355,61)+txt('Standard codec ID.\nVendor ID. Vendor codec ID.\nDetails from Windows events.',94,590,28,'#B8CEDD',400)+ui(shots[1],760,180,1000)+txt('A matching codec event is required.',760,1025,22,'#A8C1D2',400),
  brand+txt('Preview first.\nShare by choice.',90,355,60)+txt('Review diagnostics locally.\nExport a problem report.\nChoose what you share.',94,590,29,'#B8CEDD',400)+ui(shots[2],760,180,1000),
  brand+txt('Your audio.\nYour device.',100,380,84)+txt('Local detection. No audio recording.\nWindows 10 / 11 · x64 / ARM64',103,650,31,'#B8CEDD',400)+txt('Bluetooth Audio\nCodec Viewer',103,835,47)+logoAt(1120,230,570)+txt('Classic A2DP only · HFP and LE Audio are outside scope',103,1010,23,'#9CBACD',400)
 ];
 for(let i=0;i<scenes.length;i++)await png(scenes[i],`scene-${i}.png`);
 const ribbonThumb=await sharp(path.join(work,'ribbon-a.png')).extract({left:250,top:180,width:1920,height:1080}).png().toBuffer();
 await sharp(path.join(work,'background.png')).composite([{input:ribbonThumb},{input:path.join(work,'scene-0.png')}]).png().toFile(path.join(out,'trailer-thumbnail.png'));
 const inputs=['background.png','ribbon-a.png','ribbon-b.png',...scenes.map((_,i)=>`scene-${i}.png`)];
 const args=['-y','-hide_banner','-loglevel','warning','-filter_complex_threads','2'];
 for(const file of inputs)args.push('-loop','1','-framerate','30','-i',path.join(work,file));
 args.push('-f','lavfi','-i','anullsrc=channel_layout=stereo:sample_rate=48000');
 const filters=[
  '[0:v]format=rgba[bg]',
  "[bg][1:v]overlay=x='-240+85*sin(t*0.24)':y='-180+45*sin(t*0.3)'[r1]",
  "[r1][2:v]overlay=x='-220+65*cos(t*0.19)':y='-200+35*sin(t*0.23)'[r2]"
 ];
 let last='r2';
 for(let i=0;i<5;i++){
  const start=i*6,end=start+6;
  filters.push(`[${i+3}:v]format=rgba,fade=t=in:st=${start}:d=0.7:alpha=1,fade=t=out:st=${end-.7}:d=0.7:alpha=1[s${i}]`);
  filters.push(`[${last}][s${i}]overlay=x='if(lt(t,${start+.8}),48*pow(max(0,1-(t-${start})/.8),3),0)':y='5*sin(t*0.8)':enable='between(t,${start},${end})'[o${i}]`);last='o'+i;
 }
 filters.push(`[${last}]fade=t=in:st=0:d=0.4,fade=t=out:st=29.4:d=0.6,format=yuv420p[v]`);
 await fs.writeFile(path.join(root,'trailer-filter.txt'),filters.join(';\n'));
 args.push('-filter_complex',filters.join(';'),'-map','[v]','-map','8:a','-t','30','-r','30','-c:v','libx264','-preset','fast','-crf','19','-threads','4','-c:a','aac','-b:a','128k','-movflags','+faststart',path.join(out,'trailer-fluent-en.mp4'));
 await new Promise((resolve,reject)=>{const proc=spawn('ffmpeg',args,{stdio:['ignore','ignore','pipe']});let log='';proc.stderr.on('data',b=>{log+=b;process.stderr.write(b)});proc.on('error',reject);proc.on('exit',c=>c===0?resolve():reject(new Error('ffmpeg '+c+' '+log)));});
 const cues=[['00:00.000','00:06.000','See the codec. Understand your audio. Bluetooth Audio Codec Viewer.'],['00:06.000','00:12.000','Connect your headphones, then start a local detection. A temporary helper requests administrator approval. The ready state is shown.'],['00:12.000','00:18.000','Inspect standard and vendor codec IDs from Windows events. A matching codec event is required.'],['00:18.000','00:24.000','Preview local diagnostics and export a report. You choose whether to share.'],['00:24.000','00:30.000','Local detection, no audio recording. Bluetooth Classic A2DP only. HFP and LE Audio are outside scope.']];
 await fs.writeFile(path.join(out,'trailer-en.vtt'),'WEBVTT\n\n'+cues.map(([a,b,t])=>`${a} --> ${b}\n${t}\n`).join('\n'));
 console.log('Rendered continuous 30-second 1080p/30 H.264/AAC Fluent trailer.');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
