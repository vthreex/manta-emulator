"""Builds a DVB-T transport stream (188-byte packets) from a small JSON description, for the exploratory DTV input
(--exploratory-dtv-stations "MHZ|8|FILE.ts"). The original firmware parses everything itself; this only writes standard
EN 300 468 / ISO 13818-1 tables: PAT, PMT, NIT (network name), SDT (channel names), EIT present/following (EPG now/next with
descriptions), TDT (clock). No video/audio payload (the PMT lists the elementary streams).

usage: make_dtv_ts.py CONFIG.json OUT.ts
CONFIG: {"network": "Manta", "tsid": 1, "onid": 0x20FA, "utc": "2026-11-09T20:00:00",
         "services": [{"id": 1, "name": "DTV ONE", "provider": "Manta", "lcn": 1,
                       "events": [{"title": "News", "text": "Evening news", "start": "2026-11-09T19:30:00", "minutes": 60}, ...]}]}
"""
import datetime, json, struct, sys


def crc32(data):
    crc = 0xffffffff
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04c11db7) & 0xffffffff if crc & 0x80000000 else (crc << 1) & 0xffffffff
    return crc


def section(table_id, ext, body, version=0, number=0, last=0, long_form=True):
    if not long_form:   # TDT: short section, no CRC
        return bytes([table_id, 0x70 | (len(body) >> 8), len(body) & 0xff]) + body
    payload = struct.pack('>HBBB', ext, 0xc1 | (version << 1), number, last) + body
    length = len(payload) + 4
    head = bytes([table_id, 0xb0 | (length >> 8), length & 0xff])   # section_syntax_indicator=1, reserved bits
    s = head + payload
    return s + struct.pack('>I', crc32(s))


def text(s):
    b = s.encode('latin-1', 'replace')
    return b   # default table (ISO 6937 / Latin) for plain ASCII


def mjd_bcd(dt):
    mjd = (dt.date() - datetime.date(1858, 11, 17)).days
    bcd = lambda v: ((v // 10) << 4) | (v % 10)
    return struct.pack('>H', mjd) + bytes([bcd(dt.hour), bcd(dt.minute), bcd(dt.second)])


def dur_bcd(minutes):
    bcd = lambda v: ((v // 10) << 4) | (v % 10)
    return bytes([bcd(minutes // 60), bcd(minutes % 60), 0])


class Muxer:
    def __init__(self):
        self.cc = {}
        self.packets = []

    def add_section(self, pid, sec):
        data = bytes([0]) + sec   # pointer_field
        first = True
        while data:
            chunk, data = data[:184], data[184:]
            cc = self.cc.get(pid, 0); self.cc[pid] = (cc + 1) & 15
            hdr = bytes([0x47, (0x40 if first else 0) | (pid >> 8), pid & 0xff, 0x10 | cc])
            self.packets.append(hdr + chunk + b'\xff' * (184 - len(chunk)))
            first = False

    def null(self, n):
        for _ in range(n):
            self.packets.append(bytes([0x47, 0x1f, 0xff, 0x10]) + b'\xff' * 184)


def build(cfg):
    tsid, onid = cfg.get('tsid', 1), cfg.get('onid', 0x20fa)
    utc = datetime.datetime.fromisoformat(cfg.get('utc', '2026-11-09T20:00:00'))
    services = cfg['services']
    pat = b''.join(struct.pack('>HH', s['id'], 0xe000 | (0x1000 + i)) for i, s in enumerate(services))
    pat = struct.pack('>HH', 0, 0xe010) + pat   # program 0 -> NIT PID 0x10
    pmts = []
    for i, s in enumerate(services):
        vpid, apid = 0x100 + 2 * i, 0x101 + 2 * i
        streams = bytes([0x02]) + struct.pack('>HH', 0xe000 | vpid, 0xf000) + bytes([0x03]) + struct.pack('>HH', 0xe000 | apid, 0xf006) + b'\x0a\x04pol\x00'
        pmts.append((0x1000 + i, section(0x02, s['id'], struct.pack('>HH', 0xe000 | vpid, 0xf000) + streams)))
    netname = text(cfg.get('network', 'Manta'))
    ndesc = bytes([0x40, len(netname)]) + netname
    slist = b''.join(struct.pack('>HB', s['id'], 0x01) for s in services)
    lcn = b''.join(struct.pack('>HH', s['id'], 0xfc00 | s.get('lcn', i + 1)) for i, s in enumerate(services))
    tsdesc = bytes([0x41, len(slist)]) + slist + bytes([0x83, len(lcn)]) + lcn
    nit_body = struct.pack('>H', 0xf000 | len(ndesc)) + ndesc + struct.pack('>H', 0xf000 | (6 + len(tsdesc))) + struct.pack('>HHH', tsid, onid, 0xf000 | len(tsdesc)) + tsdesc
    nit = section(0x40, cfg.get('nid', 0x3001), nit_body)
    sdt_loop = b''
    for s in services:
        prov, name = text(s.get('provider', 'Manta')), text(s['name'])
        d = bytes([0x48, 3 + len(prov) + len(name), 0x01, len(prov)]) + prov + bytes([len(name)]) + name
        sdt_loop += struct.pack('>HBH', s['id'], 0xfd, 0x8000 | len(d)) + d   # running status 4 (running), not scrambled
    sdt = section(0x42, tsid, struct.pack('>HB', onid, 0xff) + sdt_loop)
    eits = []
    for s in services:
        evs = sorted(s.get('events', []), key=lambda e: e['start'])
        cur = [e for e in evs if datetime.datetime.fromisoformat(e['start']) <= utc] or evs[:1]
        nxt = [e for e in evs if datetime.datetime.fromisoformat(e['start']) > utc]
        pf = [cur[-1] if cur else None, nxt[0] if nxt else None]
        for num, e in enumerate(pf):
            body = struct.pack('>HHBB', tsid, onid, 1, 0x4e)   # segment_last_section_number, last_table_id
            if e:
                title, desc = text(e['title'])[:80], text(e.get('text', ''))[:160]
                sd = b'pol' + bytes([len(title)]) + title + bytes([len(desc)]) + desc
                d = bytes([0x4d, len(sd)]) + sd
                start = datetime.datetime.fromisoformat(e['start'])
                running = 4 if num == 0 else 1
                body += struct.pack('>H', 0x100 + evs.index(e)) + mjd_bcd(start) + dur_bcd(int(e.get('minutes', 60))) + struct.pack('>H', (running << 13) | len(d)) + d
            eits.append(section(0x4e, s['id'], body, number=num, last=1))
    tdt = section(0x70, 0, mjd_bcd(utc), long_form=False)
    mux = Muxer()
    # optional audio/video: packets of PIDs 0x100 (video, carries the PCR) and 0x101 (audio) from an ffmpeg-made TS, copied to every
    # service's PIDs with their own continuity counters
    av = []
    if cfg.get('av'):
        d = open(cfg['av'], 'rb').read()
        av = [d[i:i + 188] for i in range(0, len(d), 188) if (((d[i + 1] & 0x1f) << 8) | d[i + 2]) in (0x100, 0x101)]
    cycles = int(cfg.get('cycles', 8))
    per = (len(av) + cycles - 1) // cycles if av else 0
    for k in range(cycles):
        mux.add_section(0x0000, section(0x00, tsid, pat))
        for pid, pmt in pmts: mux.add_section(pid, pmt)
        mux.add_section(0x0011, sdt)
        mux.add_section(0x0010, nit)
        for e in eits: mux.add_section(0x0012, e)
        mux.add_section(0x0014, tdt)
        if av:
            for p in av[k * per:(k + 1) * per]:
                src = ((p[1] & 0x1f) << 8) | p[2]
                for i in range(len(services)):
                    pid = src + 2 * i
                    cc = mux.cc.get(pid, 0); mux.cc[pid] = (cc + 1) & 15 if p[3] & 0x10 else cc
                    mux.packets.append(bytes([0x47, (p[1] & 0xe0) | (pid >> 8), pid & 0xff, (p[3] & 0xf0) | cc]) + p[4:])
        else:
            mux.null(int(cfg.get('nulls', 40)))
    return b''.join(mux.packets)


if __name__ == '__main__':
    cfg = json.load(open(sys.argv[1], encoding='utf-8'))
    data = build(cfg)
    open(sys.argv[2], 'wb').write(data)
    print(len(data) // 188, 'packets')
