// Runs the browser build of the emulator (manta.js / manta.wasm: the original MStar firmware, statically recompiled, plus the machine model) in a worker
// so the page stays responsive. Messages in: {type:'start', base}, {type:'key', fn}, {type:'pause', on}. Out: progress, log, ready, frame, stats, stopped, error.
let M = null, paused = false, lastSeq = 0, stopped = false;
const post = (m, t) => self.postMessage(m, t || []);

async function fetchWithProgress(url, label, total) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(label + ': HTTP ' + r.status);
  const len = +r.headers.get('content-length') || total || 0;
  if (!r.body || !r.body.getReader) { const b = new Uint8Array(await r.arrayBuffer()); post({ type: 'progress', label, got: b.length, total: b.length }); return b; }
  const reader = r.body.getReader(), parts = []; let got = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    parts.push(value); got += value.length;
    post({ type: 'progress', label, got, total: len });
  }
  const out = new Uint8Array(got); let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}

function key(fn) {
  if (!M) return;
  const n = M.lengthBytesUTF8(fn) + 1, p = M._malloc(n);
  M.stringToUTF8(fn, p, n); M._manta_key(p); M._free(p);
}

function loop() {
  if (!M || stopped) return;
  if (paused) { setTimeout(loop, 50); return; }
  const t0 = performance.now(), d0 = M._manta_dispatches();
  let r = 0;
  while (r === 0 && performance.now() - t0 < 25) r = M._manta_step(500000);
  const seq = M._manta_frame_update();
  if (seq && seq !== lastSeq) {
    lastSeq = seq;
    const w = M._manta_frame_width(), h = M._manta_frame_height(), p = M._manta_frame_rgba();
    const buf = M.HEAPU8.slice(p, p + w * h * 4).buffer;
    post({ type: 'frame', w, h, buf, seq }, [buf]);
  }
  post({ type: 'stats', dispatches: M._manta_dispatches(), slice: M._manta_dispatches() - d0, ms: performance.now() - t0 });
  if (r !== 0) { stopped = true; post({ type: 'stopped' }); return; }
  setTimeout(loop, 0);
}

self.onmessage = async e => {
  const m = e.data;
  if (m.type === 'key') { key(m.fn); return; }
  if (m.type === 'pause') { paused = !!m.on; return; }
  if (m.type !== 'start' || M) return;
  try {
    const base = m.base;
    const cfg = await (await fetch(base + 'config.json')).json();
    const wasm = await fetchWithProgress(base + 'manta.wasm', 'emulator', cfg.sizes && cfg.sizes['manta.wasm']);
    const files = {};
    for (const [dst, src] of Object.entries(cfg.files)) files[dst] = await fetchWithProgress(base + src, src.split('/').pop(), cfg.sizes && cfg.sizes[src]);
    post({ type: 'progress', label: 'starting', got: 1, total: 1 });
    importScripts(base + 'manta.js');
    M = await self.MantaModule({ wasmBinary: wasm, print: () => {}, printErr: s => post({ type: 'log', s: String(s).slice(0, 300) }) });
    for (const [dst, data] of Object.entries(files)) {
      const dir = dst.substring(0, dst.lastIndexOf('/'));
      if (dir) M.FS.mkdirTree(dir);
      M.FS.writeFile(dst, data);
    }
    M.FS.chdir('/');
    const rc = M.callMain(cfg.args);
    if (rc !== 0) throw new Error('the emulator refused to start (exit code ' + rc + ')');
    post({ type: 'ready', dispatches: M._manta_dispatches() });
    loop();
  } catch (err) {
    post({ type: 'error', message: String(err && err.message || err) });
  }
};
