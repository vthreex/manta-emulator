// DVB-T transport stream builder for the EXPLORATORY DTV input (port of tools/make_dtv_ts.py). Writes only standard tables
// (EN 300 468 / ISO 13818-1): PAT, PMT, NIT (network name + logical channel numbers), SDT (channel names), EIT present/following
// (now/next titles + descriptions) and TDT (clock). The original firmware decodes them itself. Optional cfg.av: an MPEG-TS clip
// (MPEG-2 video + MP2 audio, e.g. from ffmpeg); its video/audio packets are copied to every service's PIDs 0x100+2i / 0x101+2i
// with their own continuity counters (as tools/make_dtv_ts.py does with "av").
// cfg = {network, tsid, onid, utc: Date, services: [{id, name, provider, lcn, events: [{title, text, start: Date, minutes}]}], av: Uint8Array}
(function (root) {
  function crc32(bytes) {
    let crc = 0xffffffff;
    for (const b of bytes) { crc ^= b << 24; for (let i = 0; i < 8; i++) crc = (crc & 0x80000000) ? ((crc << 1) ^ 0x04c11db7) >>> 0 : (crc << 1) >>> 0; }
    return crc >>> 0;
  }
  const u16 = v => [(v >> 8) & 255, v & 255];
  const txt = s => Array.from(String(s || '')).map(c => { const k = c.charCodeAt(0); return k < 32 || k > 126 ? 63 : k; });
  function section(tableId, ext, body, number = 0, last = 0) {
    const payload = [...u16(ext), 0xc1, number, last, ...body];
    const len = payload.length + 4;
    const s = [tableId, 0xb0 | (len >> 8), len & 255, ...payload];
    const c = crc32(s);
    return [...s, (c >>> 24) & 255, (c >>> 16) & 255, (c >>> 8) & 255, c & 255];
  }
  const bcd = v => ((Math.floor(v / 10)) << 4) | (v % 10);
  function mjdBcd(d) {
    const mjd = Math.floor(Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate()) / 86400000) + 40587;
    return [...u16(mjd), bcd(d.getUTCHours()), bcd(d.getUTCMinutes()), bcd(d.getUTCSeconds())];
  }
  const durBcd = m => [bcd(Math.floor(m / 60) % 100), bcd(m % 60), 0];
  // video/audio PIDs of the first programme of an MPEG-TS clip (PAT -> PMT): {v, a} (a may be -1); null when there is no MPEG video
  function clipPids(d) {
    const pid = i => ((d[i + 1] & 31) << 8) | d[i + 2];
    const payload = i => { const afc = (d[i + 3] >> 4) & 3; let at = i + 4; if (afc === 3) at += 1 + d[i + 4]; return at; };
    let pmt = -1;
    for (let i = 0; i + 188 <= d.length && pmt < 0; i += 188) if (d[i] === 0x47 && pid(i) === 0 && (d[i + 1] & 0x40)) {
      const at = payload(i), s = at + 1 + d[at], len = ((d[s + 1] & 15) << 8) | d[s + 2];
      for (let k = s + 8; k + 4 <= s + 3 + len - 4; k += 4) { const prog = (d[k] << 8) | d[k + 1]; if (prog) { pmt = ((d[k + 2] & 31) << 8) | d[k + 3]; break; } }
    }
    if (pmt < 0) return null;
    for (let i = 0; i + 188 <= d.length; i += 188) if (d[i] === 0x47 && pid(i) === pmt && (d[i + 1] & 0x40)) {
      const at = payload(i), s = at + 1 + d[at], len = ((d[s + 1] & 15) << 8) | d[s + 2], end = s + 3 + len - 4;
      let k = s + 12 + (((d[s + 10] & 15) << 8) | d[s + 11]), v = -1, a = -1;
      for (; k + 5 <= end; k += 5 + (((d[k + 3] & 15) << 8) | d[k + 4])) {
        const t = d[k], ep = ((d[k + 1] & 31) << 8) | d[k + 2];
        if ((t === 1 || t === 2) && v < 0) v = ep; else if ((t === 3 || t === 4) && a < 0) a = ep;
      }
      return v < 0 ? null : { v, a };
    }
    return null;
  }
  function build(cfg) {
    const tsid = cfg.tsid || 1, onid = cfg.onid || 0x20fa, utc = cfg.utc || new Date(), svcs = cfg.services || [];
    const packets = [], cc = {};
    function add(pid, sec) {
      let data = [0, ...sec], first = true;
      while (data.length) {
        const chunk = data.slice(0, 184); data = data.slice(184);
        const c = cc[pid] || 0; cc[pid] = (c + 1) & 15;
        const p = new Uint8Array(188).fill(0xff);
        p.set([0x47, (first ? 0x40 : 0) | (pid >> 8), pid & 255, 0x10 | c], 0); p.set(chunk, 4);
        packets.push(p); first = false;
      }
    }
    const pat = section(0x00, tsid, [...u16(0), ...u16(0xe010), ...svcs.flatMap((s, i) => [...u16(s.id), ...u16(0xe000 | (0x1000 + i))])]);
    const pmts = svcs.map((s, i) => { const v = 0x100 + 2 * i, a = v + 1;
      return [0x1000 + i, section(0x02, s.id, [...u16(0xe000 | v), ...u16(0xf000), 0x02, ...u16(0xe000 | v), ...u16(0xf000), 0x03, ...u16(0xe000 | a), ...u16(0xf006), 0x0a, 4, 112, 111, 108, 0])]; });
    const net = txt(cfg.network || 'Manta').slice(0, 60), nd = [0x40, net.length, ...net];
    const slist = svcs.flatMap(s => [...u16(s.id), 1]), lcn = svcs.flatMap((s, i) => [...u16(s.id), ...u16(0xfc00 | (s.lcn || i + 1))]);
    const tsd = [0x41, slist.length, ...slist, 0x83, lcn.length, ...lcn];
    const nit = section(0x40, cfg.nid || 0x3001, [...u16(0xf000 | nd.length), ...nd, ...u16(0xf000 | (6 + tsd.length)), ...u16(tsid), ...u16(onid), ...u16(0xf000 | tsd.length), ...tsd]);
    const sdt = section(0x42, tsid, [...u16(onid), 0xff, ...svcs.flatMap(s => { const pr = txt(s.provider || 'Manta').slice(0, 40), nm = txt(s.name).slice(0, 40);
      const d = [0x48, 3 + pr.length + nm.length, 1, pr.length, ...pr, nm.length, ...nm]; return [...u16(s.id), 0xfd, ...u16(0x8000 | d.length), ...d]; })]);
    const eits = [];
    for (const s of svcs) {
      const evs = (s.events || []).filter(e => e.title).slice().sort((a, b) => a.start - b.start);
      const cur = evs.filter(e => e.start <= utc), nxt = evs.filter(e => e.start > utc);
      const pf = [cur.length ? cur[cur.length - 1] : (evs[0] || null), nxt[0] || null];
      pf.forEach((e, num) => {
        let body = [...u16(tsid), ...u16(onid), 1, 0x4e];
        if (e) { const t = txt(e.title).slice(0, 80), x = txt(e.text).slice(0, 160), sd = [112, 111, 108, t.length, ...t, x.length, ...x], d = [0x4d, sd.length, ...sd];
          body = body.concat([...u16(0x100 + evs.indexOf(e)), ...mjdBcd(e.start), ...durBcd(e.minutes || 60), ...u16(((num === 0 ? 4 : 1) << 13) | d.length), ...d]); }
        eits.push(section(0x4e, s.id, body, num, 1));
      });
    }
    const tdtBody = mjdBcd(utc), tdt = [0x70, 0x70 | (tdtBody.length >> 8), tdtBody.length & 255, ...tdtBody];
    let av = [];
    const ids = cfg.av ? clipPids(cfg.av) : null;
    if (ids) for (let i = 0; i + 188 <= cfg.av.length; i += 188) { const p = ((cfg.av[i + 1] & 31) << 8) | cfg.av[i + 2];
      if (cfg.av[i] === 0x47 && (p === ids.v || p === ids.a)) av.push([p === ids.v ? 0x100 : 0x101, cfg.av.subarray(i, i + 188)]); }
    const cycles = cfg.cycles || (av.length ? 16 : 8), per = av.length ? Math.ceil(av.length / cycles) : 0;
    for (let k = 0; k < cycles; k++) {
      add(0, pat); for (const [pid, sec] of pmts) add(pid, sec); add(0x11, sdt); add(0x10, nit); for (const e of eits) add(0x12, e); add(0x14, tdt);
      if (av.length) {
        for (const [src, p] of av.slice(k * per, (k + 1) * per)) for (let i = 0; i < svcs.length; i++) {
          const pid = src + 2 * i, c = cc[pid] || 0; cc[pid] = (p[3] & 0x10) ? (c + 1) & 15 : c;
          const q = new Uint8Array(p); q[1] = (p[1] & 0xe0) | (pid >> 8); q[2] = pid & 255; q[3] = (p[3] & 0xf0) | c; packets.push(q);
        }
      } else for (let n = 0; n < (cfg.nulls || 40); n++) { const p = new Uint8Array(188).fill(0xff); p.set([0x47, 0x1f, 0xff, 0x10], 0); packets.push(p); }
    }
    const out = new Uint8Array(packets.length * 188); packets.forEach((p, i) => out.set(p, i * 188)); return out;
  }
  root.buildDtvTs = build;
})(typeof self !== 'undefined' ? self : this);
