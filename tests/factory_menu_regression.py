"""Factory/Hotel menu launch codes in the original firmware, typed as digits on the remote after MENU.

The firmware's ZUI shifts every pressed digit into a 16-bit buffer (_u16LaunchKeys at 0x806D7DD8, handler 804CB824) and compares it with
0x8202 (prints " factory menu", posts command 0x18 -> OSD 0x12 "Factory Setting") and 0x4578 (posts command 0x345 -> OSD 0x1D "Hotel Menu").
Needs work/checkpoints/no-signal-ready-v2.snap; SKIPS when absent. Opt-in hypotheses: --exploratory-ge-line (GE command 0x10 axis-aligned lines that the
factory/hotel windows draw; docs/factory-menu.md) and the provenance extensions. Checks: the launch buffer reaches the code, the right OSD is shown,
the run reaches its budget without a guarded stop, the window frame is produced and lines were drawn.
"""
import argparse, json, subprocess, sys, tempfile, threading
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--firmware', required=True)
p.add_argument('--emulator', required=True)
p.add_argument('--report', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
repo = root.parents[1]
snap = repo / 'work/checkpoints/no-signal-ready-v2.snap'
if not snap.exists():
    Path(a.report).write_text(json.dumps({'result': 'SKIP', 'reason': 'checkpoint absent'}))
    print('SKIP: checkpoint absent')
    sys.exit(0)
base = json.load(open(root / 'configs/checkpoints/connected-menu-base.json'))['argv']
exe = str(Path(a.emulator).resolve())
INJECT = 1300000000
DIG = {'0': 0x45, '1': 1, '2': 2, '3': 3, '4': 4, '5': 5, '6': 6, '7': 7, '8': 8, '9': 9}
out = {}


def run(tag, code, tmp):
    d = Path(tmp) / tag; (d / 'hist').mkdir(parents=True)
    keys = [(0, 0xfc)] + [(10000000 + i * 4000000, DIG[c]) for i, c in enumerate(code)]
    cmd = [exe] + base + ['--headless', '--summary', str(d / 's.json'), '--load-state', str(snap), '--load-state-allow-extra-options',
                          '--exploratory-ge-line', '--exploratory-provenance-extensions', '--max-instructions', str(INJECT + 90000000),
                          '--ir-script', ','.join('%d:raw=0x%02x' % (INJECT + t, k) for t, k in keys), '--gop-frame-history', str(d / 'hist'),
                          '--watch-pc-regs', '804c3684', '--watch-guest-access', '6d7dd8+2']
    subprocess.run(cmd, cwd=repo, capture_output=True)
    s = json.load(open(d / 's.json')); frames = json.load(open(d / 'hist/index.json'))['frames']
    out[tag] = {'reason': s['reason'], 'osd': [h['a0'] for h in s['watched_pc_hits']['0x804c3684']['hits']], 'lines': s.get('ge_lines_retired', 0),
                'buffer_last': [e['last_value'] for e in s.get('watched_data_accesses', []) if e['kind'] == 'store' and e['pc'] == '0x804cb848'],
                'frames': sorted({f['covered_pixels'] for f in frames})}


with tempfile.TemporaryDirectory(prefix='manta-factory-') as tmp:
    ths = [threading.Thread(target=run, args=('factory', '8202', tmp)), threading.Thread(target=run, args=('hotel', '4578', tmp))]
    [t.start() for t in ths]; [t.join() for t in ths]
f, h = out['factory'], out['hotel']
assert f['reason'] == h['reason'] == 'instruction budget exhausted', out
assert f['buffer_last'] == ['0x8202'] and h['buffer_last'] == ['0x4578'], out
assert '0x12' in f['osd'] and '0x1d' not in f['osd'] and f['lines'] >= 20 and 668800 in f['frames'], f
assert '0x1d' in h['osd'] and '0x12' not in h['osd'] and 675840 in h['frames'], h
Path(a.report).write_text(json.dumps({'result': 'PASS', 'runs': out}, indent=1))
print('PASS: 8202 opens the Factory Setting menu, 4578 the Hotel Menu (original launch-key buffer, exploratory GE lines)')
