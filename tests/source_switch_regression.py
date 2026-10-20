"""Direct source switching through the ORIGINAL firmware (raw 0x4E = source id 7 "AV 1", raw 0xF3 = id 9 "S-Video"), virtual remote input only.

Needs work/checkpoints/no-signal-ready-v2.snap; SKIPS when absent. Runs under the labelled opt-in hypotheses
  --exploratory-provenance-extensions  and  --exploratory-mmio-bits 1F203FE1:01:V+rmw   (V = 1 "ready", V = 0 "never ready": the firmware's own 65,537-try timeout)
and checks, for both V: the original source-switch function 8048710C is called with the right id, the source banner (OSD 3) is shown, the run
reaches its budget WITHOUT a guarded stop, the firmware returns to NO SIGNAL (OSD 2) and the two sources draw different banner text.
No video signal is supplied, so returning to NO SIGNAL is the expected end state.
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
out = {}


def run(tag, key, bit, tmp):
    d = Path(tmp) / tag
    (d / 'hist').mkdir(parents=True)
    cmd = [exe] + base + ['--headless', '--summary', str(d / 's.json'), '--load-state', str(snap), '--load-state-allow-extra-options',
                          '--exploratory-provenance-extensions', '--exploratory-mmio-bits', '1f203fe1:01:%02x+rmw' % bit,
                          '--max-instructions', str(INJECT + 150000000), '--ir-script', '%d:raw=0x%02x' % (INJECT, key),
                          '--gop-frame-history', str(d / 'hist'), '--watch-pc-regs', '8048710c,804c3684']
    subprocess.run(cmd, cwd=repo, capture_output=True)
    s = json.load(open(d / 's.json'))
    frames = json.load(open(d / 'hist/index.json'))['frames']
    banner = [f for f in frames if f['covered_pixels'] == 337280]
    hits = s['watched_pc_hits']
    out[tag] = {'reason': s['reason'], 'dispatches': s['instruction_dispatches'] - INJECT,
                'switch_ids': [h['a0'] for h in hits['0x8048710c']['hits']], 'osd': [h['a0'] for h in hits['0x804c3684']['hits']],
                'banner_frames': len(banner), 'banner_final': banner[-1]['checksum'] if banner else None,
                'plane_unused': True}


with tempfile.TemporaryDirectory(prefix='manta-source-') as tmp:
    ths = [threading.Thread(target=run, args=('s7_b%d' % b, 0x4e, b, tmp)) for b in (0, 1)] + \
          [threading.Thread(target=run, args=('s9_b%d' % b, 0xf3, b, tmp)) for b in (0, 1)]
    [t.start() for t in ths]; [t.join() for t in ths]
for tag, r in out.items():
    want = '0x7' if tag.startswith('s7') else '0x9'
    assert r['reason'] == 'instruction budget exhausted', (tag, r)          # no guarded stop
    assert r['switch_ids'] == [want], (tag, r)                              # original 8048710C called once with the source id
    assert r['osd'][1:2] == ['0x3'] or '0x3' in r['osd'], (tag, r)          # source banner shown
    assert r['osd'].index('0x3') < len(r['osd']) - 1 and '0x2' in r['osd'][r['osd'].index('0x3'):], (tag, r)   # then NO SIGNAL again
    assert r['banner_frames'] >= 5, (tag, r)
for b in (0, 1):
    assert out['s7_b%d' % b]['banner_final'] != out['s9_b%d' % b]['banner_final'], 'AV 1 and S-Video banners must differ'
assert out['s7_b0']['banner_final'] == out['s7_b1']['banner_final'] and out['s9_b0']['banner_final'] == out['s9_b1']['banner_final'], 'bracket must not change the banner'
Path(a.report).write_text(json.dumps({'result': 'PASS', 'runs': out}, indent=1))
print('PASS: sources 7 (AV 1) and 9 (S-Video) switch through the original firmware under both GOP-wait brackets and return to NO SIGNAL')
