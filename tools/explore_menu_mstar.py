"""Breadth-first exploration of the MStar LCD4214 UI by key sequences from a saved machine state - EXPLORATORY.

Every run is a fresh process: load SNAPSHOT (native engine, the control-centre profile of frontend/emulator_session.py), deliver PATH + one probe key as raw IR
bytes through the emulated PM IR FIFO, one key every GAP dispatches, and keep the GOP frame at the end. Screens are deduplicated by the hash of the frame
pixels; every new screen gets every navigation key. Hotkeys (one raw byte per distinct application code of the firmware's raw->app table, plus the
factory / hotel launch codes typed as digits) are probed from the start screen and the open main menu. Labelled options in force are those of the
profile (e.g. --exploratory-menu-no-autoexit, --exploratory-video-plane); they are written to OUT/graph.json.
Caveat: a screen can hide state (changed settings), so an edge means "this key, after THIS path".

usage: explore_menu_mstar.py --snapshot S.snap --out DIR [--parallel 6] [--max-depth 9] [--max-states 900]
"""
import argparse, hashlib, json, os, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'frontend'))
from emulator_session import REPO, Settings, build_command, emulator_candidates  # noqa: E402

NAV = {'MENU': [0xfc], 'UP': [0x0b], 'DOWN': [0x0e], 'LEFT': [0x10], 'RIGHT': [0x11], 'OK': [0x0d], 'EXIT': [0x20], 'SOURCE': [0x41], 'INFO': [0x16]}
DIGITS = {'%d' % d: [r] for d, r in zip(range(10), [0x45, 1, 2, 3, 4, 5, 6, 7, 8, 9])}
MACROS = {'CODE 8202 (factory)': [8, 2, 0x45, 2], 'CODE 4578 (hotel)': [4, 5, 7, 8]}
NAMED = {0x0f: 'MUTE', 0x17: 'SLEEP', 0x0c: 'WIDE', 0x1a: 'SMART S', 0x42: 'SMART P', 0x19: 'LANGUAGE', 0x1d: 'LIST', 0x57: 'FAV', 0x4a: 'EPG', 0x5b: 'SUBTITLE',
         0x4e: 'AV1', 0xf3: 'S-VIDEO', 0xef: 'YPBPR', 0xf5: 'VGA', 0xf7: 'HDMI1', 0x0a: 'POWER'}
GAP, DIGIT_GAP, LEAD, SETTLE = 12000000, 3000000, 4000000, 14000000

def value_steps(path):
    """LEFT/RIGHT presses after the last OK: inside an item page they change values, and every combination of slider values is a new screen.
    Screens with more than two such steps are kept but not expanded (each slider is still seen moving both ways)."""
    tail = path[len(path) - path[::-1].index('OK'):] if 'OK' in path else []
    return sum(1 for k in tail if k in ('LEFT', 'RIGHT'))

def hotkeys():
    rows = [l.split('\t') for l in (HERE.parent / 'docs' / 'ir-raw-to-app-key-table.tsv').read_text().splitlines() if l and not l.startswith('#')]
    used = {r for v in list(NAV.values()) + list(DIGITS.values()) for r in v}; seen = set(); out = {}
    for r in rows:
        raw = int(r[0], 0)
        if len(r) < 3 or r[2] == '0x8046aa64' or raw in used: continue
        app = r[1] or r[2]
        if app in seen: continue
        seen.add(app); out[NAMED.get(raw, 'RAW %02X' % raw)] = [raw]
    out.update(MACROS)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--snapshot', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--parallel', type=int, default=6); ap.add_argument('--max-depth', type=int, default=9); ap.add_argument('--max-states', type=int, default=900)
    a = ap.parse_args()
    out = Path(a.out).resolve(); (out / 'frames').mkdir(parents=True, exist_ok=True); (out / 'runs').mkdir(exist_ok=True)
    st = Settings(); st.emulator = emulator_candidates()[0]; st.snapshot = str(Path(a.snapshot).resolve())
    prog, args = build_command(st, 'sweep')
    for flag in ('--frontend-shm', '--preview-interval-log2', '--preview-max-fps'):
        if flag in args: i = args.index(flag); del args[i:i + 2]
    def cmd(extra, budget):
        a2 = list(args); a2[a2.index('--max-instructions') + 1] = str(budget); return [prog] + a2 + extra
    probe = out / 'probe.json'
    subprocess.run(cmd(['--headless', '--summary', str(probe)], 1), cwd=str(REPO), capture_output=True)
    base = json.load(open(probe))['instruction_dispatches']
    keys = dict(NAV); hot = hotkeys(); keys.update(hot); keys.update(DIGITS)

    def run(path, tag):
        d = out / 'runs' / tag; (d / 'hist').mkdir(parents=True, exist_ok=True)
        ev = []; t = base + LEAD
        for k in path:
            for j, raw in enumerate(keys[k]): ev.append('%d:raw=0x%02x' % (t + j * DIGIT_GAP, raw))
            t += GAP + (len(keys[k]) - 1) * DIGIT_GAP
        extra = ['--headless', '--summary', str(d / 's.json'), '--gop-frame-history', str(d / 'hist'), '--gop-history-interval-log2', '17']
        if ev: extra += ['--ir-script', ','.join(ev)]
        subprocess.run(cmd(extra, (t - GAP if path else t) + SETTLE), cwd=str(REPO), capture_output=True)
        try:
            frames = json.load(open(d / 'hist' / 'index.json'))['frames']
        except Exception:
            return None, None   # the run did not finish (crash / stop before the budget)
        try: active = json.load(open(d / 's.json')).get('gop_active_descriptors')
        except Exception: active = None
        if not frames or active == []:   # no enabled GOP window at the end of the run: the OSD shows nothing (a closed menu leaves a stale last history frame)
            for f in (d / 'hist').iterdir(): f.unlink()
            (d / 's.json').unlink(missing_ok=True)
            return 'no-osd', Image.new('RGB', (1360, 768), (0, 0, 0))
        im = Image.open(d / 'hist' / frames[-1]['file']).convert('RGB')
        for f in (d / 'hist').iterdir(): f.unlink()
        (d / 's.json').unlink(missing_ok=True)   # ~0.5 MB per run; only the frame is kept
        return hashlib.sha1(im.tobytes()).hexdigest()[:12], im

    gpath = out / 'graph.json'
    G = json.loads(gpath.read_text()) if gpath.exists() else {'start': None, 'nodes': {}, 'edges': [], 'tried': [], 'failed': []}
    G['profile_args'] = args; G['snapshot'] = st.snapshot; G['hotkeys'] = sorted(hot); G['gap_dispatches'] = GAP
    tried = {tuple(x) for x in G['tried']}
    if G['start'] is None:
        h, im = run([], 'boot'); im.save(out / 'frames' / (h + '.png')); G['start'] = h; G['nodes'][h] = {'path': [], 'depth': 0}
    n_run = len(list((out / 'runs').iterdir()))
    with ThreadPoolExecutor(a.parallel) as pool:
        while True:
            menu = next((h for h, n in G['nodes'].items() if n['path'] == ['MENU']), None)
            todo = []
            for h, n in sorted(G['nodes'].items(), key=lambda x: x[1]['depth']):
                p = n['path']
                if n['depth'] >= a.max_depth or (p and p[-1] == 'POWER') or value_steps(p) > 2: continue
                ks = list(NAV)
                if h in (G['start'], menu): ks += list(hot)
                if p and p[-1] == 'OK': ks += list(DIGITS)          # dialogs that take digits (Lock password, channel entry)
                elif p and p[-1] in DIGITS: ks += ['0']             # walk a digit field to its end without enumerating every number
                todo += [(h, k) for k in ks if (h, k) not in tried]
            if not todo or len(G['nodes']) >= a.max_states: break
            batch = todo[:a.parallel * 6]; jobs = []
            for h, k in batch:
                tried.add((h, k)); n_run += 1
                jobs.append((h, k, pool.submit(run, G['nodes'][h]['path'] + [k], 'r%05d' % n_run)))
            for h, k, f in jobs:
                h2, im = f.result()
                if h2 is None: G['failed'].append([h, k]); continue
                if h2 not in G['nodes']:
                    im.save(out / 'frames' / (h2 + '.png'))
                    G['nodes'][h2] = {'path': G['nodes'][h]['path'] + [k], 'depth': G['nodes'][h]['depth'] + 1}
                G['edges'].append([h, k, h2])
            G['tried'] = [list(x) for x in tried]
            gpath.write_text(json.dumps(G))
            print('%s states %d edges %d todo %d failed %d' % (time.strftime('%H:%M:%S'), len(G['nodes']), len(G['edges']), len(todo) - len(batch), len(G['failed'])), flush=True)
    print('done: states', len(G['nodes']), 'edges', len(G['edges']))

if __name__ == '__main__':
    main()
