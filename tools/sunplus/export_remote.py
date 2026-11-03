"""Export an explorer graph (tools/sunplus/explore_menu.py or tools/explore_menu_mstar.py) as the showcase remote's data (a small JS file).

Uses the screen images of the atlas (screen_atlas.py output). Per (screen, key) the most frequent recorded result is kept; from the blank start screen only
the result measured straight from boot (it hides state once the menu has been closed in a walk). Exact key sequences from boot are exported too: the page
looks the whole key history up first. Playback: the start of the exploration's own runs (--playback-mode runs, Sunplus walks from boot) or a sample of
recorded key paths replayed from the start (--playback-mode paths, MStar: its explorer runs one path per screen).

usage: export_remote.py GRAPH_DIR ATLAS_DIR OUT_JS [--atlas-url assets/atlas/sunplus] [--var TOUR] [--playback 160] [--playback-mode runs|paths]
                        [--alias "RAW 15=VOL+,RAW 1C=VOL-"] [--macros "8202=CODE 8202 (factory)"]
"""
import argparse, collections, json

ap = argparse.ArgumentParser()
ap.add_argument('src'); ap.add_argument('atlas'); ap.add_argument('out')
ap.add_argument('--atlas-url', default='assets/atlas/sunplus'); ap.add_argument('--var', default='TOUR')
ap.add_argument('--playback', type=int, default=160); ap.add_argument('--playback-mode', choices=('runs', 'paths'), default='runs')
ap.add_argument('--alias', default='', help='KEY=REMOTE_BUTTON,... (behavioural matches, documented on the page)')
ap.add_argument('--macros', default='', help='DIGITS=KEY,... key names that stand for a typed digit code')
a = ap.parse_args()
alias = dict(x.split('=', 1) for x in a.alias.split(',') if x)
ren = lambda k: alias.get(k, k)
G = json.load(open(a.src + '/graph.json')); A = json.load(open(a.atlas + '/atlas.json'))
ids = {s['hash']: s['id'] for s in A['screens']}
files = {s['id']: a.atlas_url + '/' + s['file'] for s in A['screens']}
cnt = collections.defaultdict(collections.Counter)
for x, k, y in G['edges']:
    if x in ids and y in ids: cnt[(ids[x], ren(k))][ids[y]] += 1
start = ids[G['start']]
# The blank start screen hides state: after the menu closes in a walk, the same blank screen answers MENU / arrows differently. From it only the
# result measured straight from boot counts: the screen whose key path is exactly [key] (or no change when that key never produced a new screen).
from_boot = {ren(G['nodes'][h]['path'][0]): ids[h] for h in G['nodes'] if len(G['nodes'][h]['path']) == 1 and h in ids}
steps = []; step = {}
for (x, k), c in cnt.items():
    y = from_boot.get(k, start) if x == start else c.most_common(1)[0][0]   # elsewhere the majority: a late redraw now and then records a wrong result
    steps.append([x, k, y]); step[(x, k)] = y
# exact key sequences from boot: a screen can hide state (a hotkey that switches the source draws nothing, but MENU afterwards opens a different menu)
paths = {' '.join(map(ren, G['nodes'][h]['path'])): ids[h] for h in G['nodes'] if h in ids and G['nodes'][h]['path']}
if a.playback_mode == 'runs':   # the exploration's first recorded edges in order (each run starts at the start screen)
    play = [[ids[x], ren(k), ids[y]] for x, k, y in G['edges'][:a.playback] if x in ids and y in ids]
else:   # recorded key paths of a spread of screens, each replayed from the start screen
    deep = [s for s in A['screens'] if 3 <= len(s['path']) <= 7]
    pick = deep[::max(1, len(deep) // 30)][:30]; play = []
    for s in pick:
        cur, done = start, []
        for i, k in enumerate(map(ren, s['path'])):
            nxt = paths.get(' '.join(map(ren, s['path'][:i + 1])), step.get((cur, k), cur))
            done.append([cur, k, nxt]); cur = nxt
        play += done
    play = play[:max(a.playback, 1)]
macros = dict(x.split('=', 1) for x in a.macros.split(',') if x)
data = {'start': start, 'paths': paths, 'macros': macros, 'aliases': alias, 'frames': {str(i): i for i in files}, 'files': {str(i): f for i, f in files.items()},
        'steps': steps, 'playback': play, 'screens': len(files), 'transitions_recorded': len(G['edges'])}
open(a.out, 'w').write('/* Remote data from the automatic exploration: steps = [screen, key, next screen]; images are atlas screens of the unmodified firmware. */\nwindow.%s=' % a.var
                       + json.dumps(data, separators=(',', ':')) + ';\n')
print('screens', len(files), 'unique steps', len(steps), 'playback', len(play), 'paths', len(paths))
