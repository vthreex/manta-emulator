"""Merge recorded tours into site/assets/sunplus/tour/: palette PNGs (exact: frames come from an 8-bit CLUT plane) + tour.js."""
import sys, os, json
from PIL import Image
root = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/../../../..')   # directory holding work/ and outputs/manta-emulator/
dst = root + '/outputs/manta-emulator/site/assets/sunplus/tour'; os.makedirs(dst, exist_ok=True)
steps = []; frames = {}; start = None
for tag in sys.argv[1:]:
    g = json.load(open('%s/work/tour/%s/graph.json' % (root, tag)))
    if start is None: start = g['start']
    elif g['start'] != start: print('warning: tour', tag, 'starts at a different frame', g['start'])
    if steps and steps[-1][2] != g['start']: steps.append([steps[-1][2], 'RESET', g['start']])
    # the blank start screen hides state (after the menu closes, reopening lags by a key): keep only the tour's first step out of it
    dropped = [s for j, s in enumerate(g['steps']) if j > 0 and s[0] == g['start']]
    steps += [s for j, s in enumerate(g['steps']) if j == 0 or s[0] != g['start']]
    print(tag, 'dropped', len(dropped), 'steps out of the blank start frame')
    for h in g['frames']:
        if h in frames: continue
        im = Image.open('%s/work/tour/%s/frames/%s.png' % (root, tag, h)).convert('RGB')
        if im.getcolors(256) is None: print('frame', h, 'has >256 colours, saved as RGB'); out = im
        else: out = im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        if out.mode == 'P': assert list(out.convert('RGB').getdata()) == list(im.getdata()), h
        out.save('%s/%s.png' % (dst, h), optimize=True); frames[h] = len(frames)
steps = [s for s in steps if s[1] != 'RESET']
open(dst + '/tour.js', 'w').write('/* Recorded Sunplus LED4202 menu session: [frame before, key, frame after]; frames are OSD-plane captures of the unmodified firmware. */\nwindow.TOUR=' + json.dumps({'start': start, 'frames': frames, 'steps': steps}, separators=(',', ':')) + ';\n')
tot = sum(os.path.getsize('%s/%s.png' % (dst, h)) for h in frames)
print('frames', len(frames), 'steps', len(steps), 'bytes', tot)
