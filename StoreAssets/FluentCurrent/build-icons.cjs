// Export the approved composition from a clean, transparent code-native master.
const fs=require('node:fs/promises');
const path=require('node:path');
let sharp;try{sharp=require('sharp');}catch{sharp=require('../../analysis-output/store-assets-render/node_modules/sharp');}
const root=path.resolve(__dirname,'../..');
const source=path.join(root,'StoreAssets/Icon/AppIcon-master.svg');
const assets=path.join(root,'BluetoothAudioCodec.WinUI/Assets');
const iconDir=path.join(root,'StoreAssets/Icon');
async function square(size){return sharp(source).resize(size,size).png().toBuffer();}
async function saveSquare(name,size){await fs.writeFile(path.join(assets,name),await square(size));}
async function centered(name,w,h,size){await sharp({create:{width:w,height:h,channels:4,background:{r:0,g:0,b:0,alpha:0}}}).composite([{input:await square(size),gravity:'centre'}]).png().toFile(path.join(assets,name));}
async function main(){
 await fs.mkdir(assets,{recursive:true});
 const master=await square(1024);
 await fs.writeFile(path.join(iconDir,'AppIcon-master.png'),master);
 await fs.writeFile(path.join(iconDir,'AppIcon-transparent.png'),master);
 await saveSquare('AppIcon.png',256);
 for(const [name,size] of [['Square44x44Logo',44],['Square150x150Logo',150],['StoreLogo',50]]){
  await saveSquare(name+'.png',size);
  for(const scale of [100,125,150,200,400])await saveSquare(`${name}.scale-${scale}.png`,Math.round(size*scale/100));
 }
 for(const scale of [100,125,150,200,400]){
  const f=scale/100;
  await centered(`Wide310x150Logo.scale-${scale}.png`,Math.round(310*f),Math.round(150*f),Math.round(132*f));
  await centered(`SplashScreen.scale-${scale}.png`,Math.round(620*f),Math.round(300*f),Math.round(220*f));
 }
 await centered('Wide310x150Logo.png',310,150,132);await centered('SplashScreen.png',620,300,220);
 for(const s of [16,20,24,30,32,36,40,44,48,60,64,72,80,96,256]){
  await saveSquare(`Square44x44Logo.targetsize-${s}.png`,s);
  await saveSquare(`Square44x44Logo.targetsize-${s}_altform-unplated.png`,s);
 }
 const sizes=[16,20,24,32,40,48,64,128,256],images=await Promise.all(sizes.map(square));
 const header=Buffer.alloc(6+sizes.length*16);header.writeUInt16LE(1,2);header.writeUInt16LE(sizes.length,4);
 let offset=header.length;
 images.forEach((b,i)=>{const p=6+i*16,s=sizes[i];header[p]=s===256?0:s;header[p+1]=s===256?0:s;header.writeUInt16LE(1,p+4);header.writeUInt16LE(32,p+6);header.writeUInt32LE(b.length,p+8);header.writeUInt32LE(offset,p+12);offset+=b.length;});
 await fs.writeFile(path.join(assets,'AppIcon.ico'),Buffer.concat([header,...images]));
 console.log('Exported approved logo to all native package sizes and executable ICO.');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
