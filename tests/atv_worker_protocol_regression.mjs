// Exercise the actual worker's station-message encoder without starting a CPU.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const commands=[];
const context=vm.createContext({self:{addEventListener(){},postMessage(){}},console,Uint8Array,ArrayBuffer,
  setTimeout(){},clearTimeout(){},capture:s=>commands.push(s)});
vm.runInContext(fs.readFileSync(new URL('../site/live/worker.js',import.meta.url),'utf8'),context);
vm.runInContext(`let encoded='';M={FS:{mkdirTree(){},writeFile(){}},lengthBytesUTF8:s=>s.length,
  _malloc:()=>1,stringToUTF8:s=>{encoded=s},_manta_key:()=>capture(encoded),_free(){}}`,context);
await context.self.onmessage({data:{type:'atv',stations:[{mhz:55.25,level:1,title:'TVP1',text:'FIRST\nSECOND',networkId:0x4801}]}});
assert.equal(commands[0],'atv-stations=55.25@1|TVP1|FIRST~SECOND|18433');
await context.self.onmessage({data:{type:'atv',stations:[{mhz:64,level:0.5,title:'CUSTOM',text:'TEXT'}]}});
assert.equal(commands[1],'atv-stations=64@0.5|CUSTOM|TEXT|0');
const identities=JSON.parse(fs.readFileSync(new URL('../site/live/station-identities.json',import.meta.url),'utf8'));
assert.equal(identities.stations.find(s=>s.name==='TVP1').id,0x4801);
console.log('PASS: worker sends broadcast identities separately from custom teletext names; TVP1 catalog matches firmware');
