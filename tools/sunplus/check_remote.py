"""Consistency check of the showcase remote data: for every screen, replay its own key path (as recorded by the explorer) through the remote's
transitions from the start screen and see whether the remote ends on that screen. usage: check_remote.py TOUR_JS ATLAS_JSON"""
import json, sys
js = open(sys.argv[1]).read(); T = json.loads(js[js.index('window.TOUR=') + 12:].rstrip().rstrip(';'))
A = json.load(open(sys.argv[2]))
step = {(str(a), k): str(b) for a, k, b in T['steps']}
ok = bad = lost = 0; worst = []
for s in A['screens']:
    cur = str(T['start'])
    for k in s['path']:
        cur = step.get((cur, k))
        if cur is None: break
    if cur is None: lost += 1
    elif cur == str(s['id']): ok += 1
    else: bad += 1; worst.append((s['id'], ' '.join(s['path']), cur))
print('screens', len(A['screens']), 'path replays end on the screen:', ok, ' end elsewhere:', bad, ' path leaves the recorded data:', lost)
for w in worst[:12]: print('  #%s via %s -> remote shows #%s' % w)
