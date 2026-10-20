"""USB source: the original media player screen ("No Device", PHOTO / MUSIC / MOVIE / TEXT), reached with real remote keys only.

Needs work/checkpoints/no-signal-ready-v2.snap; SKIPS when absent. Keys: SOURCE 0x41, DOWN 0x0E x5 (SCART -> USB in the original Input Source list), OK 0x0D.
Opt-in hypotheses (labelled, docs/source-switching.md): provenance extensions (incl. MOVZ/MOVN with an independent condition), GOP 1F203FE0 bit 8,
audio indirect registers 1F225DD8 and 1F225CBC = 0 (+rmw). No USB device is supplied: "No Device" is the expected content.
Checks: the original source switch is called with id 10, the run reaches its budget without a guarded stop, the media-player OSD (0x1A) is shown,
and a full-screen frame is produced afterwards.
"""
import argparse, json, subprocess, sys, tempfile
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
keys = [(0, 0x41)] + [(14000000 * (i + 1), 0x0e) for i in range(5)] + [(14000000 * 6, 0x0d)]
with tempfile.TemporaryDirectory(prefix='manta-usb-') as tmp:
    d = Path(tmp); (d / 'hist').mkdir()
    cmd = [exe] + base + ['--headless', '--summary', str(d / 's.json'), '--load-state', str(snap), '--load-state-allow-extra-options',
                          '--exploratory-provenance-extensions', '--exploratory-mmio-bits', '1f203fe1:01:01+rmw',
                          '--exploratory-mmio-bits', '1f225dd8:ff:00+rmw', '--exploratory-mmio-bits', '1f225cbc:ff:00+rmw',
                          '--max-instructions', str(INJECT + 230000000), '--ir-script', ','.join('%d:raw=0x%02x' % (INJECT + t, k) for t, k in keys),
                          '--gop-frame-history', str(d / 'hist'), '--watch-pc-regs', '8048710c,804c3684']
    subprocess.run(cmd, cwd=repo, capture_output=True)
    s = json.load(open(d / 's.json'))
    frames = json.load(open(d / 'hist/index.json'))['frames']
hits = s['watched_pc_hits']
switch = [h['a0'] for h in hits['0x8048710c']['hits']]
osd = [h['a0'] for h in hits['0x804c3684']['hits']]
assert s['reason'] == 'instruction budget exhausted', (s['reason'], s.get('stop_pc'), s.get('stop_address'))
assert switch == ['0xa'], switch
assert '0x1a' in osd, osd
late = [f for f in frames if f['dispatches'] - INJECT > 120000000 and f['covered_pixels'] == 1044480]
assert late, [(f['dispatches'] - INJECT, f['covered_pixels']) for f in frames][-5:]
Path(a.report).write_text(json.dumps({'result': 'PASS', 'switch_ids': switch, 'osd_calls': osd, 'frames_after_player': len(late), 'last_checksum': late[-1]['checksum']}, indent=1))
print('PASS: USB source reaches the original media player ("No Device") with real remote keys under the labelled hypotheses')
