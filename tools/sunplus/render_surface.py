"""Render an 8bpp (LUT8) GPU surface from a `sunplus_run --save-ram` dump.
usage: python render_surface.py STATE.bin ADDR WIDTH HEIGHT OUT.png [crop x0,y0,x1,y1]
The palette is a PLACEHOLDER (the real CLUT is not located yet): each distinct index gets a fixed false colour, so use it for layout only."""
import sys
from PIL import Image
st, addr, w, h, out = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
d = open(st, 'rb').read()[152:]
buf = d[addr:addr + w * h]
cols = [(0, 0, 0), (255, 255, 255), (200, 30, 30), (30, 30, 200), (30, 160, 30), (200, 200, 30), (30, 200, 200), (200, 30, 200)]
cm = {v: cols[i % len(cols)] for i, v in enumerate(sorted(set(buf)))}
im = Image.new('RGB', (w, h)); px = im.load()
for y in range(h):
    for x in range(w): px[x, y] = cm[buf[y * w + x]]
if len(sys.argv) > 6: im = im.crop(tuple(int(v) for v in sys.argv[6].split(',')))
im.save(out); print(out, im.size, 'indices', sorted(cm))
