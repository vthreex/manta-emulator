// Builds a USB-stick disk image (MBR + one FAT32 partition, VFAT long names) from files the visitor picked, entirely in the browser.
// Same layout as tools/make_pendrive_image.py (2 sectors per cluster, 32 reserved sectors, two FATs, partition at sector 2048), which the
// original firmware's media player reads. The image is handed to the emulator's EXPLORATORY virtual pendrive; nothing leaves the browser.
// buildPendriveImage(files: [{path: 'DIR/name.ext', data: Uint8Array, mtime: ms}], label) -> Uint8Array
(function (root) {
  'use strict';
  const SECTOR = 512, SPC = 2, CSZ = SECTOR * SPC, RESERVED = 32, PART_START = 2048;
  const MAX_FILES_BYTES = 128 * 1024 * 1024;

  function shortAlias(name, used) {
    const dot = name.lastIndexOf('.');
    const base = dot > 0 ? name.slice(0, dot) : name, ext = dot > 0 ? name.slice(dot + 1) : '';
    const clean = s => [...s.toUpperCase()].filter(c => /[A-Z0-9_\-$~!#%&{}()@'`^]/.test(c)).join('');
    const b = clean(base).slice(0, 8) || 'FILE', e = clean(ext).slice(0, 3);
    for (let n = 0; n < 10000; n++) {
      const cb = n === 0 ? b : b.slice(0, 8 - String(n).length - 1) + '~' + n;
      const cand = cb.padEnd(8) + e.padEnd(3);
      if (!used.has(cand)) { used.add(cand); return cand; }
    }
    throw new Error('cannot make a short name for ' + name);
  }
  function lfnChecksum(alias) { let s = 0; for (let i = 0; i < 11; i++) s = (((s & 1) << 7) + (s >> 1) + alias.charCodeAt(i)) & 0xff; return s; }
  function lfnEntries(name, alias) {
    const chk = lfnChecksum(alias), chars = [];
    for (let i = 0; i < name.length; i++) chars.push(name.charCodeAt(i));
    chars.push(0); while (chars.length % 13) chars.push(0xffff);
    const parts = []; for (let i = 0; i < chars.length; i += 13) parts.push(chars.slice(i, i + 13));
    const out = parts.map((p, i) => {
      const e = new Uint8Array(32), v = new DataView(e.buffer);
      e[0] = (i + 1) | (i === parts.length - 1 ? 0x40 : 0);
      for (let k = 0; k < 5; k++) v.setUint16(1 + 2 * k, p[k], true);
      e[11] = 0x0f; e[12] = 0; e[13] = chk;
      for (let k = 0; k < 6; k++) v.setUint16(14 + 2 * k, p[5 + k], true);
      v.setUint16(26, 0, true);
      for (let k = 0; k < 2; k++) v.setUint16(28 + 2 * k, p[11 + k], true);
      return e;
    });
    return out.reverse();
  }
  function dosTime(ms) {
    const d = new Date(ms || Date.now());
    const t = (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1);
    const dt = ((Math.max(d.getFullYear(), 1980) - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate();
    return [t, dt];
  }
  function dirEntry(alias, attr, cluster, size, mtime) {
    const e = new Uint8Array(32), v = new DataView(e.buffer), [t, dt] = dosTime(mtime);
    for (let i = 0; i < 11; i++) e[i] = alias.charCodeAt(i);
    e[11] = attr; v.setUint16(14, t, true); v.setUint16(16, dt, true); v.setUint16(18, dt, true);
    v.setUint16(20, cluster >>> 16, true); v.setUint16(22, t, true); v.setUint16(24, dt, true); v.setUint16(26, cluster & 0xffff, true);
    v.setUint32(28, size >>> 0, true);
    return e;
  }

  function buildPendriveImage(files, label) {
    let total = 0; for (const f of files) total += f.data.length;
    if (total > MAX_FILES_BYTES) throw new Error('the files add up to ' + (total / 1048576).toFixed(1) + ' MB; the virtual pendrive holds at most 128 MB');
    // tree
    const tree = { dirs: new Map(), files: [] };
    for (const f of files) {
      const parts = f.path.split('/').filter(Boolean); let node = tree;
      for (const d of parts.slice(0, -1)) { if (!node.dirs.has(d)) node.dirs.set(d, { dirs: new Map(), files: [] }); node = node.dirs.get(d); }
      node.files.push({ name: parts[parts.length - 1], data: f.data, mtime: f.mtime });
    }
    // size: data + per-file slack + FAT metadata; at least 66 MiB so FAT32 has its minimum 65525 clusters with 1 KiB clusters
    const sizeMiB = Math.max(80, Math.ceil((total + files.length * CSZ) * 1.04 / 1048576) + 24);
    const totalSectors = sizeMiB * 2048 - PART_START;
    let clusters = Math.floor((totalSectors - RESERVED) / SPC), fatSectors = 0;
    for (let i = 0; i < 10; i++) { fatSectors = Math.ceil((clusters + 2) * 4 / SECTOR); clusters = Math.floor((totalSectors - RESERVED - 2 * fatSectors) / SPC); }
    const img = new Uint8Array(sizeMiB * 1048576), part = PART_START * SECTOR;
    const fat = new Uint32Array(clusters + 2); fat[0] = 0x0ffffff8; fat[1] = 0x0fffffff;
    let nextFree = 2;
    const alloc = nbytes => {
      const n = Math.max(1, Math.ceil(nbytes / CSZ)), first = nextFree;
      for (let i = 0; i < n; i++) fat[first + i] = i < n - 1 ? first + i + 1 : 0x0fffffff;
      nextFree += n; if (nextFree >= clusters + 2) throw new Error('image full'); return first;
    };
    const clusterOff = c => part + (RESERVED + 2 * fatSectors + (c - 2) * SPC) * SECTOR;
    const writeChain = (first, payload) => { let c = first; for (let off = 0; off < Math.max(payload.length, 1); off += CSZ) { img.set(payload.subarray(off, off + CSZ), clusterOff(c)); c = fat[c]; } };
    const entryCount = node => { let n = 2; for (const name of [...node.dirs.keys(), ...node.files.map(f => f.name)]) n += Math.ceil((name.length + 1) / 13) + 1; return n; };
    const buildDir = (node, parent, self) => {
      const entries = [], used = new Set();
      if (parent !== null) { entries.push(dirEntry('.          ', 0x10, self, 0)); entries.push(dirEntry('..         ', 0x10, parent === 2 ? 0 : parent, 0)); }
      const items = [...[...node.dirs.keys()].map(n => ({ n, dir: true })), ...node.files.map(f => ({ n: f.name, f }))].sort((a, b) => a.n.toLowerCase() < b.n.toLowerCase() ? -1 : 1);
      for (const it of items) {
        const alias = shortAlias(it.n, used);
        if (it.dir) {
          const child = node.dirs.get(it.n), first = alloc(entryCount(child) * 32);
          entries.push(...lfnEntries(it.n, alias), dirEntry(alias, 0x10, first, 0));
          buildDir(child, self, first);
        } else {
          const first = it.f.data.length ? alloc(it.f.data.length) : 0;
          if (first) writeChain(first, it.f.data);
          entries.push(...lfnEntries(it.n, alias), dirEntry(alias, 0x20, first, it.f.data.length, it.f.mtime));
        }
      }
      const raw = new Uint8Array(Math.max(entries.length * 32, 32)); entries.forEach((e, i) => raw.set(e, i * 32));
      writeChain(self, raw);
    };
    const rootFirst = alloc(Math.max(entryCount(tree) * 32, CSZ));   // cluster 2, chained as long as the root listing needs
    buildDir(tree, null, rootFirst);
    // boot sector + FSInfo (and their backups at sector 6)
    const boot = new Uint8Array(SECTOR), bv = new DataView(boot.buffer);
    boot.set([0xeb, 0x58, 0x90], 0); for (let i = 0; i < 8; i++) boot[3 + i] = 'MSDOS5.0'.charCodeAt(i);
    bv.setUint16(11, SECTOR, true); boot[13] = SPC; bv.setUint16(14, RESERVED, true); boot[16] = 2; bv.setUint16(17, 0, true); bv.setUint16(19, 0, true);
    boot[21] = 0xf8; bv.setUint16(22, 0, true); bv.setUint16(24, 63, true); bv.setUint16(26, 255, true); bv.setUint32(28, PART_START, true); bv.setUint32(32, totalSectors, true);
    bv.setUint32(36, fatSectors, true); bv.setUint16(40, 0, true); bv.setUint16(42, 0, true); bv.setUint32(44, 2, true); bv.setUint16(48, 1, true); bv.setUint16(50, 6, true);
    boot[64] = 0x80; boot[66] = 0x29; bv.setUint32(67, 0x4d414e54, true);
    const lab = (label || 'MANTA').toUpperCase().replace(/[^A-Z0-9 _-]/g, '').padEnd(11).slice(0, 11);
    for (let i = 0; i < 11; i++) boot[71 + i] = lab.charCodeAt(i);
    for (let i = 0; i < 8; i++) boot[82 + i] = 'FAT32   '.charCodeAt(i);
    boot[510] = 0x55; boot[511] = 0xaa;
    const fsinfo = new Uint8Array(SECTOR), fv = new DataView(fsinfo.buffer);
    fv.setUint32(0, 0x41615252, true); fv.setUint32(484, 0x61417272, true); fv.setUint32(488, clusters - (nextFree - 2), true); fv.setUint32(492, nextFree, true);
    fsinfo[510] = 0x55; fsinfo[511] = 0xaa;
    for (const b of [0, 6 * SECTOR]) { img.set(boot, part + b); img.set(fsinfo, part + b + SECTOR); }
    const fatBytes = new Uint8Array(fat.buffer);
    for (let i = 0; i < 2; i++) img.set(fatBytes, part + (RESERVED + i * fatSectors) * SECTOR);
    // MBR: one FAT32 (LBA) partition
    const mv = new DataView(img.buffer);
    mv.setUint32(440, 0x4d414e54, true);
    img.set([0x00, 0x00, 0x21, 0x00, 0x0c, 0xfe, 0xff, 0xff], 446); mv.setUint32(454, PART_START, true); mv.setUint32(458, totalSectors, true);
    img[510] = 0x55; img[511] = 0xaa;
    return img;
  }
  root.buildPendriveImage = buildPendriveImage;
  root.PENDRIVE_MAX_BYTES = MAX_FILES_BYTES;
  if (typeof module !== 'undefined') module.exports = { buildPendriveImage };
})(typeof self !== 'undefined' ? self : this);
