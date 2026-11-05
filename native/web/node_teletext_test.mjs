// Same WASM runtime and virtual remote API used by the live page.
// node node_teletext_test.mjs WEB_DIR ARGS_JSON SNAPSHOT FIRMWARE OUT_PREFIX
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const [webDir, argsFile, snapshot, firmware, output] = process.argv.slice(2);
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const require=createRequire(import.meta.url);
const M=await require(path.resolve(webDir,'manta.js'))({print:()=>{},printErr:s=>console.log(s)});
const put=(dst,src)=>{M.FS.mkdirTree(path.posix.dirname(dst));M.FS.writeFile(dst,fs.readFileSync(src));};
put('/data/firmware.bin',firmware);put('/data/state.snap',snapshot);
const args=JSON.parse(fs.readFileSync(argsFile,'utf8')).filter(a=>a!=='--headless');
for(let i=0;i<args.length;++i){
  if(args[i]==='--firmware')args[++i]='/data/firmware.bin';
  else if(args[i]==='--load-state')args[++i]='/data/state.snap';
  else if(args[i].endsWith('.tsv'))put('/'+args[i].replaceAll('\\','/'),path.resolve(repo,'../..',args[i]));
}
M.FS.chdir('/');M.callMain(args);
function run(n){for(let i=0;i<n;i+=2e6){const status=M._manta_step(2e6);if(status!==0)throw Error('firmware stopped: '+status);}}
function key(s){const p=M._malloc(M.lengthBytesUTF8(s)+1);M.stringToUTF8(s,p,M.lengthBytesUTF8(s)+1);M._manta_key(p);M._free(p);}
run(10e6);key('atv-stations=864|WEB TEST|BROWSER TELETEXT');key('raw=0x4b');
run(90e6);key('raw=0x12');run(120e6);
M._manta_frame_update();const w=M._manta_frame_width(),h=M._manta_frame_height();
const pixels=M.HEAPU8.slice(M._manta_frame_rgba(),M._manta_frame_rgba()+w*h*4);
let nonblack=0;for(let i=0;i<pixels.length;i+=4)if(pixels[i]||pixels[i+1]||pixels[i+2])++nonblack;
let bodyWhite=0;for(let y=40;y<50;++y)for(let x=0;x<300;++x){const i=(y*w+x)*4;if(pixels[i]>240&&pixels[i+1]>240&&pixels[i+2]>240)++bodyWhite;}
if(w!==1360||h!==768||nonblack<1000||bodyWhite<50)throw Error('teletext frame absent: '+w+'x'+h+' nonblack='+nonblack+' bodyWhite='+bodyWhite);
fs.writeFileSync(output+'.rgba',pixels);
const report={result:'PASS',width:w,height:h,nonblack,bodyWhite,dispatches:M._manta_dispatches(),sha256:crypto.createHash('sha256').update(pixels).digest('hex'),scope:'browser WASM, original firmware, exploratory teletext models'};
fs.writeFileSync(output+'.json',JSON.stringify(report,null,2));console.log(JSON.stringify(report));
