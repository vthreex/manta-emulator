"""Carve PNG / JPEG / GIF / BMP images out of raw files. Usage: python carve_images.py OUTDIR FILE..."""
import sys, os, re, struct, zlib
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
total = 0
def png_end(d, i):
    p = i + 8
    while p + 12 <= len(d):
        ln, = struct.unpack('>I', d[p:p+4]); t = d[p+4:p+8]
        if ln > 0x4000000 or not re.fullmatch(rb'[A-Za-z]{4}', t): return None
        p += 12 + ln
        if t == b'IEND': return p
    return None
def jpg_end(d, i):
    p = i + 2
    while p + 4 <= len(d):
        if d[p] != 0xff: return None
        m = d[p+1]
        if m == 0xd9: return p + 2
        if m == 0xd8 or 0xd0 <= m <= 0xd7 or m == 0x01: p += 2; continue
        ln, = struct.unpack('>H', d[p+2:p+4])
        p += 2 + ln
        if m == 0xda:   # entropy-coded data until next non-RST marker
            while p + 1 < len(d):
                if d[p] == 0xff and d[p+1] not in (0x00,) and not (0xd0 <= d[p+1] <= 0xd7): break
                p += 1
    return None
for f in sys.argv[2:]:
    d = open(f, 'rb').read(); base = os.path.basename(f)
    for m in re.finditer(rb'\x89PNG\r\n\x1a\n', d):
        e = png_end(d, m.start())
        if e:
            w, h = struct.unpack('>II', d[m.start()+16:m.start()+24])
            open(os.path.join(out, '%s_%06x_%dx%d.png' % (base, m.start(), w, h)), 'wb').write(d[m.start():e]); total += 1
    for m in re.finditer(rb'\xff\xd8\xff[\xe0\xe1\xdb\xc0\xc4\xee]', d):
        e = jpg_end(d, m.start())
        if e and e - m.start() > 200:
            open(os.path.join(out, '%s_%06x.jpg' % (base, m.start())), 'wb').write(d[m.start():e]); total += 1
    for m in re.finditer(rb'GIF8[79]a', d):
        p = m.start(); e = d.find(b'\x00;', p + 13)
        if e > 0 and e - p < 2000000:
            w, h = struct.unpack('<HH', d[p+6:p+10])
            if 0 < w < 4096 and 0 < h < 4096:
                open(os.path.join(out, '%s_%06x_%dx%d.gif' % (base, p, w, h)), 'wb').write(d[p:e+2]); total += 1
    for m in re.finditer(rb'BM', d):
        p = m.start()
        if p + 54 > len(d): continue
        sz, = struct.unpack('<I', d[p+2:p+6]); off, = struct.unpack('<I', d[p+10:p+14]); hs, = struct.unpack('<I', d[p+14:p+18])
        if hs in (40, 108, 124) and 54 <= off < 2000 and 0 < sz < 8000000 and p + sz <= len(d):
            w, h = struct.unpack('<ii', d[p+18:p+26]); bpp, = struct.unpack('<H', d[p+28:p+30])
            if 0 < w < 4096 and 0 < abs(h) < 4096 and bpp in (1,4,8,16,24,32):
                open(os.path.join(out, '%s_%06x_%dx%d_%dbpp.bmp' % (base, p, w, abs(h), bpp)), 'wb').write(d[p:p+sz]); total += 1
print('carved', total)
