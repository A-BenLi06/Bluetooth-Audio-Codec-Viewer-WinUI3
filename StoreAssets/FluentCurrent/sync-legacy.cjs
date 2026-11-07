// Keep documented compatibility paths aligned with the current approved assets.
const fs=require('node:fs/promises');const path=require('node:path');
async function main(){
 const base=path.resolve(__dirname,'..'),input=path.join(__dirname,'import');
 const mapping=[['en/box-1080.png','Icon/StoreLogo-1080.png'],['tile-300.png','Icon/StoreLogo-300.png'],['en/poster-720.png','Icon/StorePoster-720x1080.png'],['en/hero-1920.png','Trailer/BluetoothAudioCodecViewer-SuperHero-1920x1080.png'],['trailer-fluent-en.mp4','Trailer/BluetoothAudioCodecViewer-Trailer-en-US.mp4'],['trailer-thumbnail.png','Trailer/BluetoothAudioCodecViewer-TrailerThumbnail-1920x1080.png'],['trailer-en.vtt','Trailer/BluetoothAudioCodecViewer-Trailer-en-US.vtt']];
 for(const [a,b] of mapping)await fs.copyFile(path.join(input,a),path.join(base,b));
 for(const name of ['AppIcon-fluent.png','AppIcon-fluent-clean.png','AppIcon-fluent-processed.png'])await fs.copyFile(path.join(base,'Icon/AppIcon-master.png'),path.join(base,'Icon',name));
 const vtt=await fs.readFile(path.join(input,'trailer-en.vtt'),'utf8');
 const cues=vtt.trim().split(/\r?\n\r?\n/).slice(1);
 await fs.writeFile(path.join(base,'Trailer/BluetoothAudioCodecViewer-Trailer-en-US.srt'),cues.map((c,i)=>(i+1)+'\n'+c.replace(/(\d\d:\d\d)\.(\d{3})/g,'00:$1,$2')).join('\n\n')+'\n');
 console.log('Synchronized compatibility artwork, trailer and caption paths.');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
