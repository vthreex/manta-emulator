"""ATV source: the original TV banner (channel 1, "-----", clock, "No Information", "TV"), reached with real remote keys only.

Needs work/checkpoints/ns-ba8i-2.snap (tools/make_ba8_checkpoint.py: a cold boot with the AFEC BF206BA8 bit-4 reset value seeded); SKIPS when absent.
Keys: SOURCE 0x41, UP 0x0B x4 (SCART -> ATV in the original Input Source list), OK 0x0D.
Opt-in hypotheses (labelled, docs/source-switching.md): provenance extensions, GOP 1F203FE0 bit 8 (+rmw), AFEC 1F206BA8 bit 4 reset value (+init, already part of
the checkpoint) and reset values (+init, 0) for scaler-bank bytes 1F205E34/35/30/31 @16, 1F205E10/11 @26, 1F205F80/81 and 1F205E4C/4D @25, 1F205E38/39 @34 and the VIF
carrier-lock byte 1F224471. No antenna signal is supplied: "No Information" and "-----" are the expected content.
Checks: the original source switch is called with id 1, the run reaches its budget without a guarded stop, the TV banner (OSD 3, 337,280-pixel window) is shown at
~97M dispatches and NO SIGNAL (OSD 2) returns afterwards.
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
snap = repo / 'work/checkpoints/ns-ba8i-2.snap'
if not snap.exists():
    Path(a.report).write_text(json.dumps({'result': 'SKIP', 'reason': 'checkpoint absent'}))
    print('SKIP: checkpoint absent')
    sys.exit(0)
base = json.load(open(root / 'configs/checkpoints/connected-menu-base.json'))['argv']
exe = str(Path(a.emulator).resolve())
INJECT = 1300000000
keys = [(0, 0x41)] + [(14000000 * (i + 1), 0x0b) for i in range(4)] + [(14000000 * 5, 0x0d)]
HYP = ('1f206ba8:10:00+init', '1f205e34:ff:00+init@16', '1f205e35:ff:00+init@16', '1f205e30:ff:00+init@16', '1f205e31:ff:00+init@16',
       '1f205e10:ff:00+init@26', '1f205e11:ff:00+init@26', '1f205f80:ff:00+init@25', '1f205f81:ff:00+init@25', '1f205e4c:ff:00+init@25',
       '1f205e4d:ff:00+init@25', '1f205e38:ff:00+init@34', '1f205e39:ff:00+init@34', '1f224471:ff:00+init')
with tempfile.TemporaryDirectory(prefix='manta-atv-') as tmp:
    d = Path(tmp); (d / 'hist').mkdir()
    cmd = [exe] + base + ['--headless', '--summary', str(d / 's.json'), '--load-state', str(snap), '--load-state-allow-extra-options',
                          '--exploratory-provenance-extensions', '--exploratory-mmio-bits', '1f203fe1:01:01+rmw']
    for h in HYP:
        cmd += ['--exploratory-mmio-bits', h]
    cmd += ['--max-instructions', str(INJECT + 230000000), '--ir-script', ','.join('%d:raw=0x%02x' % (INJECT + t, k) for t, k in keys),
            '--gop-frame-history', str(d / 'hist'), '--watch-pc-regs', '8048710c,804c3684']
    subprocess.run(cmd, cwd=repo, capture_output=True)
    s = json.load(open(d / 's.json'))
    frames = json.load(open(d / 'hist/index.json'))['frames']
hits = s['watched_pc_hits']
switch = [h['a0'] for h in hits['0x8048710c']['hits']]
osd = [h['a0'] for h in hits['0x804c3684']['hits']]
assert s['reason'] == 'instruction budget exhausted', (s['reason'], s.get('stop_pc'), s.get('stop_address'))
assert switch == ['0x1'], switch
assert '0x3' in osd and '0x2' in osd[osd.index('0x3'):], osd
banner = [f for f in frames if 90000000 < f['dispatches'] - INJECT < 130000000 and f['covered_pixels'] == 337280]
assert banner, [(f['dispatches'] - INJECT, f['covered_pixels']) for f in frames][-5:]
Path(a.report).write_text(json.dumps({'result': 'PASS', 'switch_ids': switch, 'osd_calls': osd, 'banner_frames': len(banner), 'last_checksum': banner[-1]['checksum']}, indent=1))
print('PASS: ATV source reaches the original TV banner and returns to NO SIGNAL with real remote keys under the labelled hypotheses')
