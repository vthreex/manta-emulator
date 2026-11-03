"""Merge branch explorations (explore_menu_mstar.py --root ... --extra ...) into a main exploration graph.

The branch's root screen replaces the main graph's screen at the same key path (e.g. the factory menu, which the main profile could not draw: its run
stopped at the first GE line command). Branch screens, transitions and frames are added; every merged screen records the extra options it needed.

usage: merge_explorations.py MAIN_DIR OUT_DIR BRANCH_DIR [BRANCH_DIR ...]
"""
import json, os, shutil, sys

main, out, branches = sys.argv[1], sys.argv[2], sys.argv[3:]
G = json.load(open(os.path.join(main, 'graph.json')))
os.makedirs(os.path.join(out, 'frames'), exist_ok=True)
frames_from = {h: os.path.join(main, 'frames', h + '.png') for h in G['nodes']}
G.setdefault('merged', [])
for b in branches:
    B = json.load(open(os.path.join(b, 'graph.json')))
    root = B['root']; extra = B.get('extra_options', [])
    old = next((h for h, n in G['nodes'].items() if n['path'] == root), None)
    new_root = B['start']
    if old is not None and old != new_root:   # the main graph's screen at this path is replaced by the branch root
        del G['nodes'][old]
        G['edges'] = [[x, k, new_root if y == old else y] for x, k, y in G['edges'] if x != old]
    for h, n in B['nodes'].items():
        if h not in G['nodes']:
            G['nodes'][h] = {'path': n['path'], 'depth': len(n['path']), 'extra_options': extra}
            frames_from[h] = os.path.join(b, 'frames', h + '.png')
    G['edges'] += B['edges']
    G['merged'].append({'root': root, 'extra_options': extra, 'screens': len(B['nodes']), 'transitions': len(B['edges'])})
for h, f in frames_from.items():
    if h in G['nodes']: shutil.copyfile(f, os.path.join(out, 'frames', h + '.png'))
json.dump(G, open(os.path.join(out, 'graph.json'), 'w'))
print('screens', len(G['nodes']), 'transitions', len(G['edges']), 'merged', G['merged'])
