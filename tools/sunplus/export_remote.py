"""Export an explorer graph (explore_menu.py) as the showcase remote's data: site/assets/sunplus/tour/tour.js.

Uses the screen images of the atlas (screen_atlas.py output). Per (screen, key) the most frequent recorded result is kept, a visible change winning over
"nothing happened" (a late redraw can look like a no-op). The playback list is the start of the exploration's own runs (continuous walks from boot).

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
steps = []
for (x, k), c in cnt.items():
    y = sorted(c.items(), key=lambda t: (t[0] == x, -t[1]))[0][0]
    steps.append([x, k, y])
# playback: the exploration's first recorded edges in order (each run starts at the start screen)
play = [[ids[x], k, ids[y]] for x, k, y in G['edges'][:a.playback] if x in ids and y in ids]
start = ids[G['start']]
data = {'start': start, 'frames': {str(i): i for i in files}, 'files': {str(i): f for i, f in files.items()},
        'steps': steps, 'playback': play, 'source': 'automatic exploration (tools/sunplus/explore_menu.py)', 'screens': len(files), 'transitions_recorded': len(G['edges'])}
open(a.out, 'w').write('/* Sunplus LED4202 remote data from the automatic exploration: steps = [screen, key, next screen]; images are atlas screens of the unmodified firmware. */\nwindow.TOUR=' + json.dumps(data, separators=(',', ':')) + ';\n')
print('screens', len(files), 'unique steps', len(steps), 'playback', len(play))
