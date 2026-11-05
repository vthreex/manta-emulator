// Exercise the actual worker's station-message encoder without starting a CPU.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const commands=[];
const writes=[];
const context=vm.createContext({self:{addEventListener(){},postMessage(){}},console,Uint8Array,ArrayBuffer,
  setTimeout(){},clearTimeout(){},capture:s=>commands.push(s),write:(p,b)=>writes.push([p,Array.from(b)])});
vm.runInContext(fs.readFileSync(new URL('../site/live/worker.js',import.meta.url),'utf8'),context);
vm.runInContext(`let encoded='';M={FS:{mkdirTree(){},writeFile:(p,b)=>write(p,b)},lengthBytesUTF8:s=>s.length,
  _malloc:()=>1,stringToUTF8:s=>{encoded=s},_manta_key:()=>capture(encoded),_free(){}}`,context);
await context.self.onmessage({data:{type:'atv',stations:[{mhz:55.25,level:1,title:'TVP1',text:'FIRST\nSECOND',networkId:0x4801}]}});
assert.equal(commands[0],'atv-stations=55.25@1|TVP1|FIRST~SECOND|18433');
await context.self.onmessage({data:{type:'atv',stations:[{mhz:64,level:0.5,title:'CUSTOM',text:'TEXT'}]}});
assert.equal(commands[1],'atv-stations=64@0.5|CUSTOM|TEXT|0');
const packet=new Uint8Array(42);packet[0]=0x02;packet[41]=0xfe;
await context.self.onmessage({data:{type:'atv',stations:[{mhz:55.25,level:1,ttx:packet.buffer}]}});
assert.equal(commands[2],'atv-stations=55.25@1|||0|/data/atv0.ttx');
assert.deepEqual(writes[0],['/data/atv0.ttx',Array.from(packet)]);
// Run the page's actual validation and send functions, including transfer copies.
const html=fs.readFileSync(new URL('../site/index.html',import.meta.url),'utf8');
vm.runInContext(html.slice(html.indexOf('function atvTtxInfo('),html.indexOf('function atvRender(')),context);
assert.equal(vm.runInContext('atvTtxInfo(new ArrayBuffer(84))',context),'2 packets');
assert.throws(()=>vm.runInContext('atvTtxInfo(new ArrayBuffer(43))',context));
assert.throws(()=>vm.runInContext('atvTtxInfo(new ArrayBuffer(0))',context));
let sent;
context.sent=(m,t)=>{sent=[m,t]};
vm.runInContext("let atvStations=[{mhz:55.25,level:1,ttx:new ArrayBuffer(42),image:new ArrayBuffer(4)}];let live={ready:true,worker:{postMessage:(m,t)=>sent(m,t)}};",context);
vm.runInContext(html.slice(html.indexOf('function atvSend('),html.indexOf('function atvWire(')),context);
vm.runInContext('atvSend()',context);
assert.equal(sent[1].length,2);
assert.ok(vm.runInContext('atvStations[0].ttx',context)!==sent[0].stations[0].ttx);
assert.equal(vm.runInContext('atvStations[0].ttx.byteLength',context),42);
const identities=JSON.parse(fs.readFileSync(new URL('../site/live/station-identities.json',import.meta.url),'utf8'));
assert.equal(identities.stations.find(s=>s.name==='TVP1').id,0x4801);
console.log('PASS: worker identities and byte-exact .ttx upload; page validation and transferable copies; TVP1 catalog matches firmware');
