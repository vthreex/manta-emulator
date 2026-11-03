// Node check of the browser build: node native/web/node_test.mjs WEB_DIR ARGS_JSON SNAPSHOT FIRMWARE OUT_PREFIX
// ARGS_JSON = the control-centre live profile (frontend/emulator_session.py build_command); host paths are remapped into the wasm file system.
import fs from 'fs';
import path from 'path';
import { createRequire } from 'module';
const [webDir, argsJson, snapshot, firmware, outPrefix] = process.argv.slice(2);
const require = createRequire(import.meta.url);
const MantaModule = require(path.resolve(webDir, 'manta.js'));
const repo = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')), '..', '..');
const M = await MantaModule({ print: s => console.log('[out]', s.slice(0, 200)), printErr: s => console.log('[err]', s.slice(0, 300)) });
const put = (dst, src) => { M.FS.mkdirTree(path.posix.dirname(dst)); M.FS.writeFile(dst, fs.readFileSync(src)); };
put('/data/firmware.bin', firmware);
put('/data/state.snap', snapshot);
put('/configs/input/ir-keymap.tsv', path.join(repo, 'configs/input/ir-keymap.tsv'));
if (fs.existsSync(path.join(repo, 'configs/input/sar-keymap.tsv'))) put('/configs/input/sar-keymap.tsv', path.join(repo, 'configs/input/sar-keymap.tsv'));
let args = JSON.parse(fs.readFileSync(argsJson, 'utf8'));
const out = [];
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--frontend-shm' || a === '--summary') { i++; continue; }
  if (a === '--firmware') { out.push(a, '/data/firmware.bin'); i++; continue; }
  if (a === '--load-state') { out.push(a, '/data/state.snap'); i++; continue; }
  // policy files keep their exact (relative) path: it is part of the snapshot's configuration fingerprint
  if (a.endsWith('.tsv') && fs.existsSync(path.resolve(repo, '..', '..', a))) { put('/' + a, path.resolve(repo, '..', '..', a)); out.push(a); continue; }
  out.push(a);
}
M.FS.chdir('/');
console.log('callMain', out.length, 'args');
let t = Date.now();
const rc = M.callMain(out);
console.log('main returned', rc, 'in', Date.now() - t, 'ms; dispatches', M._manta_dispatches());
const step = n => M._manta_step(n);
const run = (total, slice = 2e6) => { const t0 = Date.now(), d0 = M._manta_dispatches(); let r = 0; for (let k = 0; k < total / slice && r === 0; k++) r = step(slice);
  const dt = (Date.now() - t0) / 1000, dd = M._manta_dispatches() - d0; console.log(`ran ${(dd / 1e6).toFixed(1)} M in ${dt.toFixed(2)} s = ${(dd / 1e6 / dt).toFixed(1)} M/s, status ${r}`); return r; };
const key = fn => { const n = M.lengthBytesUTF8(fn) + 1, p = M._malloc(n); M.stringToUTF8(fn, p, n); M._manta_key(p); M._free(p); };
const dump = name => { const seq = M._manta_frame_update(), w = M._manta_frame_width(), h = M._manta_frame_height();
  if (!w) { console.log('no frame yet'); return; }
  const px = M.HEAPU8.slice(M._manta_frame_rgba(), M._manta_frame_rgba() + w * h * 4);
  fs.writeFileSync(`${outPrefix}_${name}.rgba`, px); fs.writeFileSync(`${outPrefix}_${name}.json`, JSON.stringify({ w, h, seq })); console.log('frame', name, w, h, 'seq', seq); };
run(40e6); dump('start');
key('raw=0xfc'); run(30e6); dump('menu');
key('raw=0x0e'); run(15e6); key('raw=0x0e'); run(15e6); dump('down2');
