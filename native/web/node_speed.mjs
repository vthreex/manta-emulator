// Speed check of the browser build in Node, using site/live/config.json as the page does.
// node native/web/node_speed.mjs SITE_LIVE_DIR [MILLIONS] [--set FLAG=VALUE ...] [--frames]   (--frames: also compose/copy every new frame like the worker)
import fs from 'fs';
import path from 'path';
import { createRequire } from 'module';
const argv = process.argv.slice(2);
const live = path.resolve(argv[0]); const millions = +(argv[1] || 100);
const require = createRequire(import.meta.url);
const MantaModule = require(path.join(live, 'manta.js'));
const cfg = JSON.parse(fs.readFileSync(path.join(live, 'config.json'), 'utf8'));
const args = cfg.args.slice();
for (let i = 0; i < argv.length; i++) if (argv[i] === '--set') { const [f, v] = argv[++i].split('='); const j = args.indexOf(f); if (j >= 0) args[j + 1] = v; else args.push(f, v); }
const M = await MantaModule({ print: () => {}, printErr: () => {} });
for (const [dst, src] of Object.entries(cfg.files)) { M.FS.mkdirTree(path.posix.dirname(dst)); M.FS.writeFile(dst, fs.readFileSync(path.join(live, src))); }
M.FS.chdir('/');
if (M.callMain(args) !== 0) { console.log('start failed'); process.exit(1); }
const frames = argv.includes('--frames');
let seq = 0, nf = 0; const t0 = Date.now(), d0 = M._manta_dispatches();
while (M._manta_dispatches() - d0 < millions * 1e6) {
  if (M._manta_step(500000) !== 0) { console.log('stopped'); break; }
  if (frames) { const s = M._manta_frame_update(); if (s && s !== seq) { seq = s; nf++; const p = M._manta_frame_rgba(); M.HEAPU8.slice(p, p + M._manta_frame_width() * M._manta_frame_height() * 4); } }
}
const dt = (Date.now() - t0) / 1000, dd = (M._manta_dispatches() - d0) / 1e6;
console.log(`${argv.slice(2).join(' ') || 'default'}: ${dd.toFixed(0)} M in ${dt.toFixed(1)} s = ${(dd / dt).toFixed(1)} M/s, ${nf} frames composed`);
