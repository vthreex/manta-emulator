// Runs the browser build of the emulator (manta.js / manta.wasm: the original MStar firmware, statically recompiled, plus the machine model) in a worker
// so the page stays responsive. Messages in: {type:'start', base}, {type:'key', fn}, {type:'pause', on}, {type:'usb', image} (a FAT32 disk image built
// by the page from the visitor's files: written to the in-memory file system and hot-plugged as the EXPLORATORY virtual pendrive).
// Out: progress, log, ready, frame, stats (heartbeat), crashed {message} (machine stop, wasm trap or uncaught error), error (start failed).
let M = null, usbUnlink = 0, paused = false, lastSeq = 0, stopped = false, lastReason = '', lastNative = '';
const post = (m, t) => self.postMessage(m, t || []);
const crashed = msg => { if (stopped) return; stopped = true; post({ type: 'crashed', message: msg }); };
self.addEventListener('error', e => crashed('uncaught error: ' + (e.message || e)));
self.addEventListener('unhandledrejection', e => crashed('uncaught error: ' + (e.reason && e.reason.message || e.reason)));

let fetchMode = 'default';   // 'reload' = bypass the browser cache (the page's Re-download button)
async function fetchWithProgress(url, label, total) {
  const r = await fetch(url, { cache: fetchMode });
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
  if (!M || stopped) return;
  const n = M.lengthBytesUTF8(fn) + 1, p = M._malloc(n);
  M.stringToUTF8(fn, p, n); M._manta_key(p); M._free(p);
}

function loop() {
  if (!M || stopped) return;
  if (paused) { post({ type: 'stats', dispatches: M._manta_dispatches(), slice: 0, ms: 0 }); setTimeout(loop, 50); return; }
  let r = 0;
  try {
    const t0 = performance.now(), d0 = M._manta_dispatches();
    while (r === 0 && performance.now() - t0 < 25) r = M._manta_step(500000);
    const seq = M._manta_frame_update();
    if (seq && seq !== lastSeq) {
      lastSeq = seq;
      const w = M._manta_frame_width(), h = M._manta_frame_height(), p = M._manta_frame_rgba();
      const buf = M.HEAPU8.slice(p, p + w * h * 4).buffer;
      post({ type: 'frame', w, h, buf, seq }, [buf]);
    }
    post({ type: 'stats', dispatches: M._manta_dispatches(), slice: M._manta_dispatches() - d0, ms: performance.now() - t0 });
    if (usbUnlink && --usbUnlink === 0) { try { M.FS.unlink('/data/usb.img'); } catch (_) {} post({ type: 'usb', state: 'inserted' }); }   // the device model keeps its own copy
  } catch (err) {
    crashed('the emulator crashed: ' + String(err && err.message || err) + (lastNative ? ' (' + lastNative + ')' : ''));
    return;
  }
  if (r !== 0) { crashed('the emulated machine stopped' + (lastReason ? ': ' + lastReason : '') + (lastNative ? ' (' + lastNative + ')' : '')); return; }
  setTimeout(loop, 0);
}

self.onmessage = async e => {
  const m = e.data;
  if (m.type === 'key') { key(m.fn); return; }
  if (m.type === 'pause') { paused = !!m.on; return; }
  if (m.type === 'usb') {
    if (!M || stopped) return;
    try { M.FS.mkdirTree('/data'); M.FS.writeFile('/data/usb.img', new Uint8Array(m.image)); key('usb-insert=/data/usb.img'); usbUnlink = 4; }
    catch (err) { post({ type: 'usb', state: 'failed', message: String(err && err.message || err) }); }
    return;
  }
  if (m.type !== 'start' || M) return;
  try {
    const base = m.base; if (m.reload) fetchMode = 'reload';
    const cfg = await (await fetch(base + 'config.json', { cache: 'no-store' })).json();
    const v = () => '';   // plain URLs: the browser cache is used until the page's Re-download button asks for fresh copies (fetchMode 'reload')
    const wasm = await fetchWithProgress(base + 'manta.wasm' + v('manta.wasm'), 'emulator', cfg.sizes && cfg.sizes['manta.wasm']);
    const files = {};
    for (const [dst, src] of Object.entries(cfg.files)) files[dst] = await fetchWithProgress(base + src + v(src), src.split('/').pop(), cfg.sizes && cfg.sizes[src]);
    post({ type: 'progress', label: 'starting', got: 1, total: 1 });
    importScripts(base + 'manta.js' + v('manta.js'));
    M = await self.MantaModule({
      wasmBinary: wasm,
      print: s => { const r = /"reason":\s*"([^"]*)"/.exec(s); if (r) lastReason = r[1]; },   // the machine summary printed when the run ends
      printErr: s => { s = String(s); if (/NATIVE STOP|ERROR|unsupported|stopped/i.test(s)) lastNative = s.slice(0, 200); post({ type: 'log', s: s.slice(0, 300) }); },
      onAbort: what => crashed('the emulator aborted: ' + what),
    });
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
