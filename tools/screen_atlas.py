"""Build a screen atlas from an explorer graph (tools/sunplus/explore_menu.py or tools/explore_menu_mstar.py).

OUT/screens/NNNN.webp  every distinct screen (lossless WebP), numbered by the length of its key path (then discovery order); optional downscale
OUT/atlas.json         [{id, file, path: [keys from the start], next: {key: id}}], plus run metadata
SHEETS/sheet_NN.png    contact sheets (--sheets) (labelled with id and key path)

usage: screen_atlas.py GRAPH_DIR OUT [--width 680] [--title NAME] [--panel 1360 768]
"""
import argparse, json, ntpath, os
from PIL import Image, ImageDraw

ap = argparse.ArgumentParser()
ap.add_argument('src'); ap.add_argument('out'); ap.add_argument('--width', type=int, default=680); ap.add_argument('--title', default='')
ap.add_argument('--panel', type=int, nargs=2, help='pad every frame top-left onto a W H canvas (MStar: 1360 768)')
ap.add_argument('--frames', help='frame folder (default GRAPH_DIR/frames)')
ap.add_argument('--sheets', help='also write contact sheets into this folder')
ap.add_argument('--quality', type=int, default=0, help='lossy WebP quality (default: lossless)')
a = ap.parse_args()
G = json.load(open(os.path.join(a.src, 'graph.json')))
order = sorted(G['nodes'], key=lambda h: (len(G['nodes'][h]['path']), list(G['nodes']).index(h)))
ids = {h: i for i, h in enumerate(order)}
os.makedirs(os.path.join(a.out, 'screens'), exist_ok=True)
if a.sheets: os.makedirs(a.sheets, exist_ok=True)
nxt = {h: {} for h in order}
for x, k, y in G['edges']:
    if x in nxt and y in ids: nxt[x].setdefault(k, ids[y])
entries = []
for h in order:
    im = Image.open(os.path.join(a.frames or os.path.join(a.src, 'frames'), h + '.png')).convert('RGB')
    if a.panel and im.size != tuple(a.panel):   # the MStar GOP preview is cropped to the windows' extent from the panel origin: place it on the panel
        c = Image.new('RGB', tuple(a.panel), (0, 0, 0)); c.paste(im, (0, 0)); im = c
    if a.width and im.size[0] > a.width: im = im.resize((a.width, round(im.size[1] * a.width / im.size[0])), Image.LANCZOS)
    f = 'screens/%04d.webp' % ids[h]
    if a.quality: im.save(os.path.join(a.out, f), 'WEBP', quality=a.quality, method=4)   # lossy (for noisy video-plane backgrounds)
    else: im.save(os.path.join(a.out, f), 'WEBP', lossless=True, method=4)          # lossless: the pixels are the captured frame (after the resize)
    entries.append({'id': ids[h], 'file': f, 'hash': h, 'path': G['nodes'][h]['path'], 'next': nxt[h]})
meta = {k: G[k] for k in ('hotkeys', 'crashes', 'failed', 'snapshot', 'gap_dispatches') if k in G}
if 'snapshot' in meta: meta['snapshot'] = ntpath.basename(meta['snapshot'])   # no local paths in published data
json.dump({'title': a.title, 'encoding': ('webp lossy q%d' % a.quality) if a.quality else 'webp lossless', 'screens': entries, 'transitions': len(G['edges']), 'meta': meta}, open(os.path.join(a.out, 'atlas.json'), 'w'), indent=0)
# contact sheets: 5 x 6 per sheet
TW, TH, cols, rows = 272, 0, 5, 6
for s in (range(0, len(entries), cols * rows) if a.sheets else []):
    chunk = entries[s:s + cols * rows]; tiles = []
    for e in chunk:
        im = Image.open(os.path.join(a.out, e['file'])).convert('RGB'); th = round(im.size[1] * TW / im.size[0]); tiles.append((e, im.resize((TW, th))))
    TH = max(t[1].size[1] for t in tiles) + 26
    sheet = Image.new('RGB', (cols * (TW + 4), ((len(tiles) + cols - 1) // cols) * TH), (16, 18, 22)); d = ImageDraw.Draw(sheet)
    for i, (e, im) in enumerate(tiles):
        x, y = (i % cols) * (TW + 4), (i // cols) * TH; sheet.paste(im, (x, y + 24))
        p = ' '.join(e['path']) or '(start)'
        d.text((x + 2, y + 1), '#%d' % e['id'], fill=(255, 220, 90)); d.text((x + 2, y + 12), p if len(p) < 44 else '...' + p[-41:], fill=(210, 215, 222))
    sheet.save(os.path.join(a.sheets, 'sheet_%02d.png' % (s // (cols * rows))))
print(len(entries), 'screens,', len(G['edges']), 'transitions ->', a.out)
