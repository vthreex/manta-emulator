// DVB-T transport stream builder for the EXPLORATORY DTV input (port of tools/make_dtv_ts.py). Writes only standard tables
// (EN 300 468 / ISO 13818-1): PAT, PMT, NIT (network name + logical channel numbers), SDT (channel names), EIT present/following
// (now/next titles + descriptions) and TDT (clock). The original firmware decodes them itself. No audio/video payload.
// cfg = {network, tsid, onid, utc: Date, services: [{id, name, provider, lcn, events: [{title, text, start: Date, minutes}]}]}
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
    for (let k = 0; k < (cfg.cycles || 8); k++) {
      add(0, pat); for (const [pid, sec] of pmts) add(pid, sec); add(0x11, sdt); add(0x10, nit); for (const e of eits) add(0x12, e); add(0x14, tdt);
      for (let n = 0; n < (cfg.nulls || 40); n++) { const p = new Uint8Array(188).fill(0xff); p.set([0x47, 0x1f, 0xff, 0x10], 0); packets.push(p); }
    }
    const out = new Uint8Array(packets.length * 188); packets.forEach((p, i) => out.set(p, i * 188)); return out;
  }
  root.buildDtvTs = build;
})(typeof self !== 'undefined' ? self : this);
