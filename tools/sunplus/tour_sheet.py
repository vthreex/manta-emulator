import sys, os, json
from PIL import Image, ImageDraw
root = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/../../../..')   # directory holding work/ and outputs/manta-emulator/
tags = sys.argv[1:]; tiles = []; seen = set()
for t in tags:
    g = json.load(open('%s/work/tour/%s/graph.json' % (root, t)))
    for h in sorted(g['frames'], key=g['frames'].get):
        if h in seen: continue
        seen.add(h); tiles.append((h, Image.open('%s/work/tour/%s/frames/%s.png' % (root, t, h)).resize((304, 165))))
cols = 5; rows = (len(tiles) + cols - 1) // cols
sheet = Image.new('RGB', (cols * 310, rows * 171), (0, 0, 0)); d = ImageDraw.Draw(sheet)
for i, (h, im) in enumerate(tiles):
    x, y = (i % cols) * 310, (i // cols) * 171; sheet.paste(im, (x, y)); d.rectangle((x, y, x + 90, y + 12), fill=(0, 0, 0)); d.text((x + 2, y), h, fill=(255, 255, 0))
out = '%s/work/tour/sheet_%s.png' % (root, '_'.join(tags)); sheet.save(out); print(out, len(tiles))
