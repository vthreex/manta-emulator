"""Export an explorer graph (explore_menu.py) as the showcase remote's data: site/assets/sunplus/tour/tour.js.

Uses the screen images of the atlas (screen_atlas.py output). Per (screen, key) the most frequent recorded result is kept; from the blank start screen only
the result measured straight from boot (it hides state once the menu has been closed in a walk). The playback list is the start of the exploration's runs.

usage: export_remote.py GRAPH_DIR ATLAS_DIR OUT_JS [--atlas-url assets/atlas/sunplus] [--playback 160]
"""
import argparse, collections, json

ap = argparse.ArgumentParser()
ap.add_argument('src'); ap.add_argument('atlas'); ap.add_argument('out')
ap.add_argument('--atlas-url', default='assets/atlas/sunplus'); ap.add_argument('--playback', type=int, default=160)
a = ap.parse_args()
G = json.load(open(a.src + '/graph.json')); A = json.load(open(a.atlas + '/atlas.json'))
ids = {s['hash']: s['id'] for s in A['screens']}
files = {s['id']: a.atlas_url + '/' + s['file'] for s in A['screens']}
cnt = collections.defaultdict(collections.Counter)
for x, k, y in G['edges']:
    if x in ids and y in ids: cnt[(ids[x], k)][ids[y]] += 1
start = ids[G['start']]
# The blank start screen hides state: after the menu closes in a walk, the same blank screen answers MENU / arrows differently. From it only the
# result measured straight from boot counts: the screen whose key path is exactly [key] (or no change when that key never produced a new screen).
from_boot = {G['nodes'][h]['path'][0]: ids[h] for h in G['nodes'] if len(G['nodes'][h]['path']) == 1 and h in ids}
steps = []
for (x, k), c in cnt.items():
    if x == start:
        y = from_boot.get(k, start)
    else:   # majority of the observations: a late redraw now and then records a wrong result, the common answer outvotes it
        y = c.most_common(1)[0][0]
    steps.append([x, k, y])
# playback: the exploration's first recorded edges in order (each run starts at the start screen)
play = [[ids[x], k, ids[y]] for x, k, y in G['edges'][:a.playback] if x in ids and y in ids]
# exact key sequences from boot: a screen can hide state (a hotkey that switches the source draws nothing, but MENU afterwards opens a different menu),
# so the page looks up the whole key history first and uses the per-screen steps only when that exact sequence was not recorded
paths = {' '.join(G['nodes'][h]['path']): ids[h] for h in G['nodes'] if h in ids and G['nodes'][h]['path']}
data = {'start': start, 'paths': paths, 'frames': {str(i): i for i in files}, 'files': {str(i): f for i, f in files.items()},
        'steps': steps, 'playback': play, 'source': 'automatic exploration (tools/sunplus/explore_menu.py)', 'screens': len(files), 'transitions_recorded': len(G['edges'])}
open(a.out, 'w').write('/* Sunplus LED4202 remote data from the automatic exploration: steps = [screen, key, next screen]; images are atlas screens of the unmodified firmware. */\nwindow.TOUR=' + json.dumps(data, separators=(',', ':')) + ';\n')
print('screens', len(files), 'unique steps', len(steps), 'playback', len(play))
