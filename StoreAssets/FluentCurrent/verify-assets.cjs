const fs=require('node:fs/promises');const path=require('node:path');const assert=require('node:assert/strict');
let sharp;try{sharp=require('sharp');}catch{sharp=require('../../analysis-output/store-assets-render/node_modules/sharp');}
async function main(){
 const root=path.resolve(__dirname,'../..'),input=path.join(__dirname,'import');
 const locales=['en','zh-cn','zh-tw','ja','fr','de','it','pt-br','es'];let images=0;
 async function dimensions(file,w,h){const m=await sharp(file).metadata();assert.equal(m.width,w,file);assert.equal(m.height,h,file);images++;}
 for(const lang of locales){for(let i=1;i<=4;i++)await dimensions(path.join(input,lang,`0${i}-feature.png`),1920,1080);for(const [name,w,h] of [['box-1080.png',1080,1080],['poster-720.png',720,1080],['hero-1920.png',1920,1080]])await dimensions(path.join(input,lang,name),w,h);}
 for(const s of [71,150,300]){const file=path.join(input,`tile-${s}.png`);await dimensions(file,s,s);const stats=await sharp(file).stats();assert.equal(stats.isOpaque,false,file);assert.equal(stats.channels[3].min,0,file);assert.equal(stats.channels[3].max,255,file);}
 await dimensions(path.join(input,'trailer-thumbnail.png'),1920,1080);
 const icons=path.join(root,'BluetoothAudioCodec.WinUI/Assets');let native=0;
 for(const file of await fs.readdir(icons)){if(!file.endsWith('.png'))continue;const m=await sharp(path.join(icons,file)).metadata();const s=await sharp(path.join(icons,file)).stats();assert.equal(m.hasAlpha,true,file);assert.equal(s.isOpaque,false,file);native++;}
 const xaml=await fs.readFile(path.join(root,'BluetoothAudioCodec.WinUI/MainWindow.xaml'),'utf8');assert.equal((xaml.match(/Glyph="&#xE702;"/g)||[]).length,1,'Keep original in-app Bluetooth function glyph');assert.match(xaml,/Source="ms-appx:\/\/\/Assets\/AppIcon.png"/);
 const result={localized_images_checked:images,native_transparent_pngs_checked:native,locales,original_bluetooth_function_glyph_preserved:true,icon_background:'genuine alpha transparency'};
 await fs.writeFile(path.join(__dirname,'ASSET_VERIFICATION.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
}
main().catch(e=>{console.error(e);process.exitCode=1;});
