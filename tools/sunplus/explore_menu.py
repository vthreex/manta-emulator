"""Exhaustive-ish exploration of the Sunplus LED4202 UI by key sequences - EXPLORATORY.

The headless runner is deterministic: the same key events posted at the same idle wakes give the same machine state. So a state is reached again by
replaying its key path from boot (~15 s) - that is the "snapshot". Every run posts PATH + probe keys,
one key every GAP idle wakes, and dumps the OSD plane (+ CLUT) just before each next key. Consecutive dumps give observed transitions
(screen, key, next screen); screens are deduplicated by the hash of plane + palette. Breadth-first: every new screen gets every navigation key;
hotkeys (all other events of the firmware's IR table IRMANTA_BIG) are probed from the start screen and the open main menu.
Caveat: a screen can hide state (a changed setting, or the blank screen after closing the menu), so an edge says "this key, after THIS path".

Each run is guided: after the probe key it walks the known transition graph to the nearest screen with untried keys (up to --tail keys per boot).
A screen reached by three equal LEFT/RIGHT/VOL steps in a row is not expanded (slider and volume values).

usage: explore_menu.py --runner EXE --kernel K --flash F --hyp H --out DIR [--parallel 12] [--max-depth 80] [--max-states 1500] [--tail 30]
"""
import argparse, hashlib, json, os, random, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from menu_viewer import render

KEY_FN = 0x80cee834
NAMES = {0x1021: 'MENU', 0x1018: 'UP', 0x1019: 'DOWN', 0x101a: 'LEFT', 0x101b: 'RIGHT', 0x101c: 'OK', 0x1043: 'EXIT', 0x1047: 'SOURCE',
         0x101f: 'VOL+', 0x1020: 'VOL-', 0x1017: 'POWER'}
NAV = [0x1021, 0x1018, 0x1019, 0x101a, 0x101b, 0x101c, 0x1043, 0x1047, 0x101f, 0x1020]
IR_TABLE, NO_KEY = 0x80014d54, 0x109c
W, H = 1216, 660
FIRST, GAP = 26000, 700

def name(e): return NAMES.get(e, 'EV_%04X' % e)

def ir_events(kernel):
    d = open(kernel, 'rb').read(); o = IR_TABLE - 0x80000400; ev = []
    for i in range(256):
        v = int.from_bytes(d[o + 4 * i:o + 4 * i + 4], 'little')
        if v == NO_KEY or not (0x1000 <= v < 0x1100): continue
        if v not in ev: ev.append(v)
    return ev

def run(a, keys, tag):
    d = os.path.join(a.out, 'runs', tag); os.makedirs(d, exist_ok=True)
    pre = os.path.join(d, 'd_')
    dumps = [FIRST - 50] + [FIRST + i * GAP + GAP - 50 for i in range(len(keys))]
    calls = []
    for i, k in enumerate(keys): calls += ['--call', '0x%x:0x%x:0@%d' % (KEY_FN, k, FIRST + i * GAP)]
    env = dict(os.environ, SUNPLUS_BEST_SCREEN='4a17a00,%d,%d' % (W, H), SUNPLUS_DUMP_PREFIX=pre, SUNPLUS_DUMP_IDLES=','.join(map(str, dumps)), SUNPLUS_PALETTE_ADDR='85cbf4b8')
    cmd = [a.runner, a.kernel, '--cpu', '24kec', '--flash', a.flash, '--flash-xip', '0x18000000', '--hyp', a.hyp, '--readback', '--unknown-zero', '--map-iop',
           '--iop-set', '0x1eec=0x98000000', '--iop-set', '0x0=0', '--iop-set', '0x8=1', '--irq-vec', '0x48', '--poll-limit', '50000000',
           '--max-idle', str(dumps[-1] + 1), '--max-blocks', '4000000000', '--gpu'] + calls
    with open(os.path.join(d, 'log.txt'), 'wb') as lg: subprocess.run(cmd, stdout=lg, stderr=subprocess.STDOUT, env=env)
    frames = []
    for w in dumps:
        f = pre + str(w)
        if not (os.path.exists(f + '.bin') and os.path.exists(f + '.pal')): break
        raw = open(f + '.bin', 'rb').read(); pal = open(f + '.pal', 'rb').read()
        frames.append((hashlib.sha1(raw + pal).hexdigest()[:12], raw, pal))
        os.remove(f + '.bin'); os.remove(f + '.pal')
    return frames

def main():
    ap = argparse.ArgumentParser()
    for k in ('--runner', '--kernel', '--flash', '--hyp', '--out'): ap.add_argument(k, required=True)
    ap.add_argument('--parallel', type=int, default=12); ap.add_argument('--max-depth', type=int, default=80); ap.add_argument('--max-states', type=int, default=1500)
    ap.add_argument('--tail', type=int, default=30); ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args(); rnd = random.Random(a.seed)
    for k in ('runner', 'kernel', 'flash', 'hyp', 'out'): setattr(a, k, os.path.abspath(getattr(a, k)))
    os.makedirs(os.path.join(a.out, 'frames'), exist_ok=True)
    gpath = os.path.join(a.out, 'graph.json')
    G = json.load(open(gpath)) if os.path.exists(gpath) else {'start': None, 'nodes': {}, 'edges': [], 'tried': [], 'crashes': []}
    hot = [e for e in ir_events(a.kernel) if e not in NAV]
    G['hotkeys'] = [name(e) for e in hot]
    tried = {(h, k) for h, k in G['tried']}

    def absorb(keys, frames):
        nonlocal tried
        if not frames: return
        h0 = frames[0][0]
        if G['start'] is None: G['start'] = h0
        prev = h0; path = []
        for i, (h, raw, pal) in enumerate(frames):
            if h not in G['nodes']:
                render(raw, pal).save(os.path.join(a.out, 'frames', h + '.png'))
                G['nodes'][h] = {'path': [name(k) for k in path], 'keys': path[:], 'depth': len(path)}
            if i:
                k = keys[i - 1]
                G['edges'].append([prev, name(k), h]); tried.add((prev, k))
            if i < len(keys): path = path + [keys[i]]
            prev = h
        if len(frames) < len(keys) + 1:   # the run stopped early (crash / stop): remember the key that killed it
            k = keys[len(frames) - 1]; G['crashes'].append([frames[-1][0], name(k), [name(x) for x in keys[:len(frames)]]]); tried.add((frames[-1][0], k))

    if G['start'] is None:
        absorb([], run(a, [], 'boot'))
    n_run = len(os.listdir(os.path.join(a.out, 'runs'))) if os.path.isdir(os.path.join(a.out, 'runs')) else 0
    BY = {v: k for k, v in NAMES.items()}
    REPEAT = {0x101a, 0x101b, 0x101f, 0x1020}

    def terminal(n):
        ks = n['keys']
        if ks and ks[-1] == 0x1017: return True                          # power-off path
        run_len = 0
        for k in reversed(ks):
            if k == (ks[-1] if ks else None) and k in REPEAT: run_len += 1
            else: break
        return run_len >= 3 or n['depth'] >= a.max_depth                  # slider / volume values: three steps in a row are enough

    with ThreadPoolExecutor(a.parallel) as pool:
        while True:
            menu = next((h for h, n in G['nodes'].items() if n['keys'] == [0x1021]), None)
            E = {(x, BY.get(k, int(k[3:], 16) if k.startswith('EV_') else None)): y for x, k, y in G['edges']}
            def keyset(h): return NAV + (hot + [0x1017] if h in (G['start'], menu) else [])
            reserved = set()
            def untried(h):
                n = G['nodes'].get(h)
                if n is None or terminal(n): return []
                return [k for k in keyset(h) if (h, k) not in tried and (h, k) not in reserved]
            def route(p):   # shortest known key route from p to a screen with untried keys
                seen = {p}; q = [(p, [])]
                while q:
                    x, r = q.pop(0)
                    if r and untried(x): return r
                    if len(r) > 12: continue
                    for k in NAV[:7]:
                        y = E.get((x, k))
                        if y is not None and y not in seen: seen.add(y); q.append((y, r + [k]))
                return None
            todo = [(h, k) for h, n in sorted(G['nodes'].items(), key=lambda x: x[1]['depth']) for k in untried(h)]
            if not todo or len(G['nodes']) >= a.max_states: break
            jobs = []; starts = []
            for h, k in todo:
                if len(jobs) >= a.parallel * 2: break
                if (h, k) in reserved or h in starts: continue
                starts.append(h)
                keys = G['nodes'][h]['keys'][:]; p = h; added = 0
                while added < a.tail + 1:   # guided walk: an untried key here, else follow the known graph to the nearest screen that has one
                    if p is None:   # the last key led somewhere not predicted: keep probing with navigation keys
                        keys.append(rnd.choice(NAV[:7])); added += 1; continue
                    u = untried(p)
                    if u:
                        k2 = u[0]; reserved.add((p, k2)); keys.append(k2); added += 1
                        if k2 == 0x1017: break
                        p = None; continue
                    r = route(p)
                    if not r: break
                    for k2 in r: keys.append(k2); p = E[(p, k2)]
                    added += len(r)
                n_run += 1; jobs.append((keys, pool.submit(run, a, keys, 'r%05d' % n_run)))
            for keys, f in jobs: absorb(keys, f.result())
            G['tried'] = [[h, k] for h, k in tried]
            json.dump(G, open(gpath, 'w'))
            print('%s states %d edges %d todo %d crashes %d' % (time.strftime('%H:%M:%S'), len(G['nodes']), len(G['edges']), len(todo), len(G['crashes'])), flush=True)
    print('done: states', len(G['nodes']), 'edges', len(G['edges']))

if __name__ == '__main__':
    main()
