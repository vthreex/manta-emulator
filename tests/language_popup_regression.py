"""OSD Language popup input path (original firmware behaviour in the connected SCART/NO SIGNAL state), virtual remote input only.

Needs work/checkpoints/no-signal-ready-v2.snap; SKIPS when absent. Menu path: MENU 0xFC, RIGHT 0x11 x2 (Setup), OK 0x0D opens the popup.
Then one key is pressed inside the popup and the GOP frame timeline is compared (docs/language-popup-input-path.md):
  RIGHT 0x11 -> the popup redraws (new frame checksum after the key)
  MENU  0xFC -> consumed by the key reader, never translated to a ZUI key: no new frame, popup stays
  OK    0x0D -> posted to the ZUI, no handler in the popup cells: no new frame, popup stays
  EXIT  0x20 -> the application closes the menu: OSD 2 (NO SIGNAL) is shown (804C3684(2)) and the frame changes
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
nav = [(0, 0xfc), (8000000, 0x11), (16000000, 0x11), (28000000, 0x0d)]
KEY_AT = 42000000
out = {}


def run(tag, key, tmp):
    d = Path(tmp) / tag
    (d / 'hist').mkdir(parents=True)
    keys = nav + [(KEY_AT, key)]
    cmd = [exe] + base + ['--headless', '--summary', str(d / 's.json'), '--load-state', str(snap), '--max-instructions', str(INJECT + 75000000),
                          '--ir-script', ','.join('%d:raw=0x%02x' % (INJECT + t, k) for t, k in keys), '--gop-frame-history', str(d / 'hist'),
                          '--watch-pc-regs', '804c3684,804c3974']
    subprocess.run(cmd, cwd=repo, capture_output=True)
    summary = json.load(open(d / 's.json'))
    frames = json.load(open(d / 'hist/index.json'))['frames']
    after = [f for f in frames if f['dispatches'] - INJECT > KEY_AT]   # frames produced after the key was delivered
    osd = summary['watched_pc_hits'].get('0x804c3684', {})
    out[tag] = {'frames_after_key': len(after), 'osd_calls': osd.get('count', 0),
                'osd_a0': [h['a0'] for h in osd.get('hits', [])], 'pops': summary.get('pm_ir_fifo_pops'),
                'frames': [f['dispatches'] - INJECT for f in frames][-4:], 'untranslated_hits': summary['watched_pc_hits'].get('0x804c3974', {}).get('count', 0)}


with tempfile.TemporaryDirectory(prefix='manta-popup-') as tmp:
    ths = [threading.Thread(target=run, args=(n, k, tmp)) for n, k in (('right', 0x11), ('menu', 0xfc), ('ok', 0x0d), ('exit', 0x20))]
    [t.start() for t in ths]; [t.join() for t in ths]
assert out["right"]["frames_after_key"] >= 1, out["right"]
assert out['menu']['frames_after_key'] == 0 and out['ok']['frames_after_key'] == 0, out
assert out['menu']['untranslated_hits'] >= 1, 'MENU (app 0x29) must reach the ZUI translation and map to no key: %r' % out['menu']
assert out['exit']['osd_calls'] >= 2 and '0x2' in out['exit']['osd_a0'], 'EXIT must show OSD 2 (NO SIGNAL): %r' % out['exit']
assert out['exit']['frames_after_key'] >= 1
for k in out.values(): assert k['pops'] == 5, k
Path(a.report).write_text(json.dumps({'result': 'PASS', 'runs': out}, indent=1))
print('PASS: popup input path - RIGHT redraws, MENU/OK leave it unchanged (MENU untranslated), EXIT closes the menu')
