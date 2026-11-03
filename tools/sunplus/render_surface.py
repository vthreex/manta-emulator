"""Render an 8bpp (LUT8) GPU surface from a `sunplus_run --save-ram` dump using the firmware's own CLUT.
usage: python render_surface.py STATE.bin ADDR WIDTH HEIGHT OUT.png [crop x0,y0,x1,y1] [scale] [palette_addr]
Palette: 256 x (A,R,G,B bytes) read from RAM (default 0x85b23750 = the table the OSD region descriptors point to in the Shivaki image;
entry 0 = transparent colour key). Alpha is composited over a dark grey background (stand-in for the video plane)."""
import sys
from PIL import Image
st, addr, w, h, out = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
crop = tuple(int(v) for v in sys.argv[6].split(',')) if len(sys.argv) > 6 and sys.argv[6] != '-' else None
scale = int(sys.argv[7]) if len(sys.argv) > 7 else 1
pal_addr = int(sys.argv[8], 0) if len(sys.argv) > 8 else 0x85b23750
d = open(st, 'rb').read()[152:]
pal = d[pal_addr & 0x1fffffff:(pal_addr & 0x1fffffff) + 1024]
buf = d[addr:addr + w * h]
bg = (48, 48, 56)
lut = []
for i in range(256):
    a, r, g, b = pal[4 * i:4 * i + 4]
    lut.append(bg if (i == 0 and a == 0) else tuple((c * a + k * (255 - a)) // 255 for c, k in zip((r, g, b), bg)))
im = Image.new('RGB', (w, h)); px = im.load()
for y in range(h):
    for x in range(w): px[x, y] = lut[buf[y * w + x]]
if crop: im = im.crop(crop)
if scale != 1: im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
im.save(out); print(out, im.size)
