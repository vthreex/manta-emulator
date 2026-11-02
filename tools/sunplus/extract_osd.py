"""Extract the GEL/OSD bitmaps of a Sunplus CV9202 application image.
Descriptor (24 bytes, little endian): data_ptr, fmt(0x01000000), palette_ptr, (height<<16)|width, bits_per_pixel, kind.
Pixel rows are padded to 4 bytes; 8bpp images use a 256 x 4-byte A,R,G,B palette (entry 0 = colour key, alpha 0).
Usage: python extract_osd.py APP.bin BASE OUTDIR"""
import sys, os, struct, collections
from PIL import Image
d = open(sys.argv[1], 'rb').read(); base = int(sys.argv[2], 0); out = sys.argv[3]; os.makedirs(out, exist_ok=True)
n = len(d) // 4; w = struct.unpack('<%dI' % n, d[:n * 4])
lo, hi = base, base + len(d)
def inrange(x): return lo <= x < hi
ents = []
for i in range(n - 6):
    ptr, fmt, pal, dim, bpp, kind = w[i:i+6]
    h, wd = dim >> 16, dim & 0xffff
    if inrange(ptr) and inrange(pal) and fmt == 0x01000000 and bpp in (1, 2, 4, 8) and 1 <= wd <= 1920 and 1 <= h <= 1080 and kind < 16:
        ents.append((i * 4, ptr, pal, wd, h, bpp, kind))
print('descriptors', len(ents))
seen = {}; k = 0
for off, ptr, pal, wd, h, bpp, kind in ents:
    o = ptr - base
    def rough(st):
        s = c = 0
        for y in range(min(h, 48)):
            r = d[o + y * st:o + y * st + wd]
            s += sum(1 for x in range(1, len(r)) if r[x] != r[x - 1]); c += max(len(r) - 1, 1)
        return s / max(c, 1)
    cands = [st for st in (((wd * bpp + 31) // 32) * 4, (wd * bpp + 7) // 8, ((wd * bpp + 63) // 64) * 8, ((wd * bpp + 127) // 128) * 16) if o + st * h <= len(d)]
    if not cands: continue
    stride = cands[0]
    for st in cands[1:]:                 # prefer the 4-byte padded stride unless another is clearly smoother
        if rough(st) < rough(stride) - 0.05: stride = st
    raw = d[o:o + stride * h]
    po = pal - base; ncol = 1 << bpp
    colors = [d[po + 4 * j:po + 4 * j + 4] for j in range(ncol)]
    # A,R,G,B byte order
    pl = [(c[1], c[2], c[3], c[0]) for c in colors]
    im = Image.new('RGBA', (wd, h))
    px = im.load()
    for y in range(h):
        row = raw[y * stride:(y + 1) * stride]
        for x in range(wd):
            if bpp == 8: v = row[x]
            else:
                bit = x * bpp; v = (row[bit >> 3] >> (8 - bpp - (bit & 7))) & (ncol - 1)
            px[x, y] = pl[v]
    key = (ptr, wd, h, bpp)
    if key in seen: continue
    seen[key] = 1
    im.save(os.path.join(out, 'osd_%06x_%dx%d_%dbpp_pal%06x_s%d.png' % (o, wd, h, bpp, po, stride)))
    k += 1
print('saved', k, 'unique images,', len(set(e[2] for e in ents)), 'distinct palettes')
