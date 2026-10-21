"""Verify the reverse-engineered JPD table-memory format against a JPEG.

Inputs: the JPEG and the 528 half-words the coprocessor driver wrote to the JPD table data port (summary `exploratory_copro.captured_writes`).
Layout (from the logo run, see docs/jpd-registers.md):
  words   0..127  Huffman length records, 16 per table, 2 words each, tables in order DC0, DC1, AC0, AC1
                  record = (lo16, hi16): hi16 = 0x100 | firstcode>>(L-8) (valid flag + first code of length L left-aligned to 8 bits),
                  lo16 = (extra first-code bits beyond 8, left-aligned) | symbol base index (AC: index; DC: index<<4); empty length = (0, 0)
  words 128..383  AC symbols, word i = AC1[i]<<8 | AC0[i]
  words 384..399  DC symbols, word i = DC1[i]<<8 | DC0[i]
  words 400..527  quantisation tables, raster order, Q0 then Q1
Decodes every block of the scan twice (JPEG's own DHT vs. the table memory) and compares the coefficients.
"""
import json, struct, sys

def parse(jpg):
    d = open(jpg, 'rb').read(); i = 2; q = {}; dht = {}; sof = None; dri = 0; sos = None
    while i < len(d):
        m = d[i + 1]; L = struct.unpack('>H', d[i + 2:i + 4])[0]; seg = d[i + 4:i + 2 + L]
        if m == 0xdb:
            p = 0
            while p < len(seg): q[seg[p] & 15] = list(seg[p + 1:p + 65]); p += 65
        elif m == 0xc4:
            p = 0
            while p < len(seg):
                tc = seg[p] >> 4; th = seg[p] & 15; cnt = list(seg[p + 1:p + 17]); n = sum(cnt)
                dht[(tc, th)] = (cnt, list(seg[p + 17:p + 17 + n])); p += 17 + n
        elif m == 0xc0: sof = seg
        elif m == 0xdd: dri = struct.unpack('>H', seg[:2])[0]
        elif m == 0xda: sos = seg; scan = d[i + 2 + L:]; break
        i += 2 + L
    return q, dht, sof, dri, sos, scan

def split_scan(scan):
    segs = []; cur = bytearray(); i = 0
    while i < len(scan):
        b = scan[i]
        if b == 0xff:
            n = scan[i + 1]
            if n == 0: cur.append(0xff); i += 2; continue
            if 0xd0 <= n <= 0xd7: segs.append(bytes(cur)); cur = bytearray(); i += 2; continue
            if n == 0xd9: break
        cur.append(b); i += 1
    segs.append(bytes(cur)); return segs

class Bits:
    def __init__(s, b): s.b = b; s.p = 0
    def peek(s, n):
        v = 0
        for k in range(n):
            byte = s.b[(s.p + k) >> 3] if (s.p + k) >> 3 < len(s.b) else 0
            v = (v << 1) | ((byte >> (7 - ((s.p + k) & 7))) & 1)
        return v
    def get(s, n): v = s.peek(n); s.p += n; return v

def canon(cnt, syms):
    codes = {}; code = 0; k = 0
    for L in range(1, 17):
        for _ in range(cnt[L - 1]): codes[(L, code)] = syms[k]; k += 1; code += 1
        code <<= 1
    return codes

def table_from_memory(cap, t):  # t: 0 DC0, 1 DC1, 2 AC0, 3 AC1
    recs = [(cap[t * 32 + 2 * k], cap[t * 32 + 2 * k + 1]) for k in range(16)]
    ac = t >= 2; which = t & 1
    def sym(idx):
        if ac: w = cap[128 + idx]
        else: w = cap[384 + idx]
        return (w >> 8) & 255 if which else w & 255
    firsts = []
    for L in range(1, 17):
        lo, hi = recs[L - 1]
        if not hi: continue
        n = L - 8; first16 = ((hi & 0xff) << 8) | ((((lo >> (16 - n)) << (8 - n)) & 0xff) if n > 0 else 0)
        base = (lo & 0x00ff) if ac else ((lo & 0x00ff) >> 4)
        # mask extra bits: for AC lengths > 8 extra bits occupy bits 15..(16-(L-8)); the index uses the low 12 bits only if no overlap
        firsts.append((L, first16, base))
    return firsts

def decode_sym_mem(bits, firsts, ac, which, cap):
    peek = bits.peek(16)
    best = None
    for L, f16, base in firsts:
        if f16 <= peek: best = (L, f16, base)
    L, f16, base = best
    code = peek >> (16 - L); first = f16 >> (16 - L)
    idx = base + (code - first); bits.get(L)
    w = cap[128 + idx] if ac else cap[384 + idx]
    return (w >> 8) & 255 if which else w & 255

def decode_sym_ref(bits, codes):
    code = 0
    for L in range(1, 17):
        code = (code << 1) | bits.get(1)
        if (L, code) in codes: return codes[(L, code)]
    raise ValueError('bad code')

def ext(v, n): return v if n == 0 or v >= (1 << (n - 1)) else v - (1 << n) + 1

def main(jpg, capf):
    cap = json.load(open(capf)); q, dht, sof, dri, sos, scan = parse(jpg)
    mem = [table_from_memory(cap, t) for t in range(4)]
    ref = {(0, 0): canon(*dht[(0, 0)]), (0, 1): canon(*dht[(0, 1)]), (1, 0): canon(*dht[(1, 0)]), (1, 1): canon(*dht[(1, 1)])}
    W = struct.unpack('>H', sof[3:5])[0]; H = struct.unpack('>H', sof[1:3])[0]
    mcus = ((W + 15) // 16) * ((H + 15) // 16); print('MCUs', mcus, 'DRI', dri)
    segs = split_scan(scan); print('restart segments', len(segs))
    comp_tbl = [(0, 0), (1, 1), (1, 1)]  # (dc,ac) table per component from SOS
    nblk = [4, 1, 1]
    bad = 0; blocks = 0
    for si, seg in enumerate(segs):
        bm = Bits(seg); br = Bits(seg); pm = [0, 0, 0]; pr = [0, 0, 0]
        n = dri if dri else mcus
        for m in range(min(n, mcus - si * n)):
            for c in range(3):
                dcsel, acsel = comp_tbl[c]
                for b in range(nblk[c]):
                    for (bits, kind) in ((bm, 'mem'), (br, 'ref')):
                        pass
                    # DC
                    s1 = decode_sym_mem(bm, mem[dcsel], False, dcsel, cap); s2 = decode_sym_ref(br, ref[(0, dcsel)])
                    v1 = ext(bm.get(s1), s1) if s1 else 0; v2 = ext(br.get(s2), s2) if s2 else 0
                    pm[c] += v1; pr[c] += v2
                    k = 1; ok = (s1 == s2 and v1 == v2)
                    while k < 64:
                        a = decode_sym_mem(bm, mem[2 + acsel], True, acsel, cap); b2 = decode_sym_ref(br, ref[(1, acsel)])
                        if a != b2: ok = False
                        if a == 0: break
                        r, s = a >> 4, a & 15
                        if a == 0xf0: k += 16; continue
                        k += r; x1 = ext(bm.get(s), s); x2 = ext(br.get(b2 & 15), b2 & 15)
                        if x1 != x2: ok = False
                        k += 1
                    blocks += 1
                    if not ok or bm.p != br.p:
                        bad += 1
                        if bad < 5: print('mismatch at seg', si, 'mcu', m, 'comp', c, 'blk', b, bm.p, br.p)
                        raise SystemExit(1)
    print('blocks decoded', blocks, 'mismatches', bad)

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
