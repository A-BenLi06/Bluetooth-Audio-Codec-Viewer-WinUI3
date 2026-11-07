// Code-native Fluent layout: real screenshots, exact localized text, SVG export.
const fs = require('node:fs/promises');
const path = require('node:path');
let sharp;try{sharp=require('sharp');}catch{sharp=require('../../analysis-output/store-assets-render/node_modules/sharp');}
const root = __dirname;
const out = path.join(root, 'import');
let logoUrl = '';
const copy = {
 en: {titles:['See the\nactive codec.','Look inside\nthe connection.','A clearer\nproblem report.','Your audio.\nYour device.'], bodies:['Connect your headphones.\nStart a local detection.','Standard and vendor IDs.\nDetails from Windows events.','Preview, then export.\nShare only when you choose.','Local detection.\nNo audio recording.'], tag:'See what Windows negotiated.', foot:'Windows 10 / 11 · x64 / ARM64', note:'Ready state shown. A matching codec event is required.'},
 'zh-cn': {titles:['查看当前\n蓝牙编解码器。','连接细节，\n清晰可见。','问题报告，\n先看再分享。','你的音频，\n留在本机。'], bodies:['连接蓝牙耳机，\n开始本机检测。','标准 ID 与厂商 ID，\n来自 Windows 事件。','预览并导出诊断报告，\n由你决定是否分享。','本机检测，\n不录制音频。'], tag:'看清 Windows 实际协商的编解码器。',foot:'Windows 10 / 11 · x64 / ARM64',note:'图示为就绪状态，检测结果需要匹配的编解码器事件。'},
 'zh-tw': {titles:['查看目前的\n藍牙編解碼器。','連線細節，\n清楚呈現。','問題報告，\n先看再分享。','你的音訊，\n留在本機。'],bodies:['連接藍牙耳機，\n開始本機偵測。','標準 ID 與廠商 ID，\n來自 Windows 事件。','預覽並匯出診斷報告，\n由你決定是否分享。','本機偵測，\n不錄製音訊。'],tag:'看清 Windows 實際協商的編解碼器。',foot:'Windows 10 / 11 · x64 / ARM64',note:'圖示為就緒狀態，偵測結果需要相符的編解碼器事件。'},
 ja:{titles:['使用中の\nコーデックを確認。','接続の詳細を\n見やすく。','レポートを確認。\n共有は自分で。','音声は、\nこのデバイスに。'],bodies:['ヘッドホンを接続して、\nローカルで検出。','標準 ID とベンダー ID。\nWindows イベントから取得。','診断内容をプレビューし、\nZIP に保存できます。','ローカルで検出。\n音声は録音しません。'],tag:'Windows が実際に使用するコーデックを確認。',foot:'Windows 10 / 11 · x64 / ARM64',note:'準備完了の画面です。検出には一致するイベントが必要です。'},
 fr:{titles:['Le codec\nréellement utilisé.','Les détails\nde la connexion.','Un rapport\nplus clair.','Votre audio.\nVotre appareil.'],bodies:['Connectez votre casque.\nLancez une détection locale.','Identifiants du codec\net du fournisseur.','Vérifiez, puis exportez.\nVous décidez du partage.','Détection locale.\nAucun enregistrement audio.'],tag:'Découvrez le codec négocié par Windows.',foot:'Windows 10 / 11 · x64 / ARM64',note:'État prêt affiché. Un événement de codec correspondant est requis.'},
 de:{titles:['Den aktiven\nCodec erkennen.','Details zur\nVerbindung.','Ein klarer\nProblembericht.','Ihr Audio.\nIhr Gerät.'],bodies:['Kopfhörer verbinden.\nLokale Erkennung starten.','Codec- und Hersteller-IDs\naus Windows-Ereignissen.','Prüfen und exportieren.\nSie entscheiden, was Sie teilen.','Lokale Erkennung.\nKeine Audioaufnahme.'],tag:'Sehen, was Windows ausgehandelt hat.',foot:'Windows 10 / 11 · x64 / ARM64',note:'Bereitschaftszustand. Ein passendes Codec-Ereignis ist erforderlich.'},
 it:{titles:['Il codec\nin uso.','I dettagli\ndella connessione.','Un rapporto\npiù chiaro.','Il tuo audio.\nIl tuo dispositivo.'],bodies:['Collega le cuffie.\nAvvia un rilevamento locale.','ID del codec e del produttore\ndagli eventi di Windows.','Controlla, poi esporta.\nDecidi tu cosa condividere.','Rilevamento locale.\nNessuna registrazione audio.'],tag:'Scopri il codec negoziato da Windows.',foot:'Windows 10 / 11 · x64 / ARM64',note:'È mostrato lo stato pronto. Serve un evento del codec corrispondente.'},
 'pt-br':{titles:['Veja o codec\nem uso.','Os detalhes\nda conexão.','Um relatório\nmais claro.','Seu áudio.\nSeu dispositivo.'],bodies:['Conecte seus fones.\nInicie uma detecção local.','IDs do codec e do fabricante\na partir de eventos do Windows.','Revise e exporte.\nVocê decide o que compartilhar.','Detecção local.\nSem gravação de áudio.'],tag:'Veja o codec negociado pelo Windows.',foot:'Windows 10 / 11 · x64 / ARM64',note:'Estado pronto exibido. É necessário um evento de codec correspondente.'},
 es:{titles:['El códec\nen uso.','Los detalles\nde la conexión.','Un informe\nmás claro.','Tu audio.\nTu dispositivo.'],bodies:['Conecta tus auriculares.\nInicia una detección local.','ID del códec y del fabricante\na partir de eventos de Windows.','Revisa y exporta.\nTú decides qué compartir.','Detección local.\nSin grabación de audio.'],tag:'Consulta el códec negociado por Windows.',foot:'Windows 10 / 11 · x64 / ARM64',note:'Se muestra el estado listo. Se requiere un evento de códec coincidente.'}
};
const esc=s=>s.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;');
const font="'Segoe UI Variable','Segoe UI','Microsoft YaHei UI','Yu Gothic UI',sans-serif";
function text(s,x,y,size=30,color='#f5f5f7',weight=400,line=1.3){return `<text x="${x}" y="${y}" fill="${color}" font-family="${font}" font-size="${size}" font-weight="${weight}">${s.split('\n').map((l,i)=>`<tspan x="${x}" dy="${i?size*line:0}">${esc(l)}</tspan>`).join('')}</text>`;}
function base(w,h,body){return `<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}"><defs><radialGradient id="ambient"><stop stop-color="#183D50"/><stop offset="1" stop-color="#151D29"/></radialGradient><linearGradient id="surface" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#235D71" stop-opacity=".8"/><stop offset="1" stop-color="#1B2942" stop-opacity=".15"/></linearGradient><linearGradient id="tile"><stop stop-color="#35435d"/><stop offset="1" stop-color="#252c3c"/></linearGradient><filter id="shadow" x="-30%" y="-30%" width="160%" height="170%"><feDropShadow dy="16" stdDeviation="20" flood-color="#000" flood-opacity=".48"/></filter></defs><rect width="${w}" height="${h}" fill="url(#ambient)"/><path d="M ${w*.27} 0 H ${w} V ${h*.5} C ${w*.57} ${h*.4},${w*.05} ${h*.3},${w*.27} 0Z" fill="url(#surface)"/><path d="M 0 ${h*.69} C ${w*.32} ${h*.8},${w*.32} ${h*.95},${w*.22} ${h} H0Z" fill="url(#surface)" stroke="#54BBD0" stroke-opacity=".13"/>${body}</svg>`;}
function mark(x,y,size){return `<svg x="${x}" y="${y}" width="${size}" height="${size}" viewBox="0 0 300 300"><defs><clipPath id="logo-clip"><rect width="300" height="300" rx="42"/></clipPath></defs><image width="300" height="300" xlink:href="${logoUrl}" clip-path="url(#logo-clip)"/></svg>`;}
async function png(svg,file){await fs.mkdir(path.dirname(file),{recursive:true});await sharp(Buffer.from(svg)).png().toFile(file);}
async function shot(file,region){let s=sharp(path.join(root,'source',file));if(region)s=s.extract(region);const b=await s.png().toBuffer();const m=await sharp(b).metadata();return {url:'data:image/png;base64,'+b.toString('base64'),w:m.width,h:m.height};}
function windowImage(s,x,y,w){let h=w*s.h/s.w;return `<g filter="url(#shadow)"><image x="${x}" y="${y}" width="${w}" height="${h}" xlink:href="${s.url}"/><rect x="${x}" y="${y}" width="${w}" height="${h}" rx="12" fill="none" stroke="#979ca7" stroke-opacity=".5"/></g>`;}
async function main(){
 await fs.mkdir(out,{recursive:true});
 logoUrl='data:image/png;base64,'+(await fs.readFile(path.join(root,'../Icon/AppIcon-master.png'))).toString('base64');
 const ready=await shot('viewer-ready.png',{left:0,top:0,width:847,height:530});
 const details=await shot('viewer-details.png',{left:0,top:0,width:847,height:614});
 const help=await shot('viewer-help.png',{left:0,top:0,width:847,height:614});
 for(const size of [71,150,300])await sharp(path.join(root,'../Icon/AppIcon-master.png')).resize(size,size).png().toFile(path.join(out,'tile-'+size+'.png'));
 for(const [lang,d] of Object.entries(copy)){
  let dir=path.join(out,lang); await fs.mkdir(dir,{recursive:true});
  for(let n=0;n<4;n++){
   const image=[ready,details,help,ready][n]; const width=n===1||n===2?1120:1190;const h=width*image.h/image.w;const x=1920-width-65;const y=Math.round((1080-h)/2);
   let svg=base(1920,1080,mark(72,96,110)+text('Bluetooth Audio\nCodec Viewer',208,128,29,'#f5f5f7',600,1.25)+text(d.titles[n],72,390,lang==='ja'?49:57,'#fafafa',600,1.22)+text(d.bodies[n],72,600,lang==='ja'?25:28,'#c7cad3',400,1.45)+text('Bluetooth Classic · A2DP',72,944,23,'#c7cad3')+windowImage(image,x,y,width)+text(d.note,x,1040,20,'#abb0be'));
   await png(svg,path.join(dir,`0${n+1}-feature.png`));await fs.writeFile(path.join(root,`${lang}-0${n+1}.svg`),svg);
  }
  await png(base(1080,1080,mark(384,142,312)+text('Bluetooth Audio',100,610,73,'#fff',600)+text('Codec Viewer',100,708,73,'#fff',600)+text(d.tag,100,842,lang==='ja'?28:30,'#c7cad3')+text('Bluetooth Classic · A2DP',100,988,26,'#abb0be')),path.join(dir,'box-1080.png'));
  let tagLines=d.tag.length>40?d.tag.replace(/ (?!.* )/,'\n'):d.tag;
  await png(base(720,1080,mark(210,125,300)+text('Bluetooth',64,570,68,'#fff',600)+text('Audio Codec',64,655,68,'#fff',600)+text('Viewer',64,740,68,'#fff',600)+text(tagLines,64,852,lang==='ja'?24:26,'#c7cad3',400,1.5)+text('Bluetooth Classic · A2DP',64,1006,23,'#abb0be')),path.join(dir,'poster-720.png'));
  await png(base(1920,1080,mark(1170,270,480)+text(d.titles[0],100,410,lang==='ja'?65:78,'#fff',600,1.25)+text(d.bodies[0],100,680,36,'#c7cad3',400,1.5)+text('Bluetooth Classic · A2DP',100,955,28,'#c7cad3')),path.join(dir,'hero-1920.png'));
 }
 await fs.writeFile(path.join(root,'copy.json'),JSON.stringify(copy,null,2));
 console.log('Rendered all localized current-logo assets for 9 languages.');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
