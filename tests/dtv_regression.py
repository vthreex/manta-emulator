"""DTV source: the original DTV channel banner (channel 1 "TVP1", clock, "No Information", "DTV"), reached with real remote keys only.

Needs work/checkpoints/no-signal-ready-v2.snap; SKIPS when absent. Keys: SOURCE 0x41, UP 0x0B x5 (SCART -> DTV in the original Input Source list), OK 0x0D.
Opt-in hypotheses (labelled, docs/source-switching.md): provenance extensions (incl. MOVZ/MOVN with an independent condition), GOP 1F203FE0 bit 8,
reset values (+init) of audio 1F225DD8/1F225CBC/1F225CB0, TSP-side 1F004074/75 and 1F203600/01, scaler bank 1 1F205E78/79, and a peer-acknowledged mailbox
1F206728/29 (+ack: the DVB-T handshake at 805DB6C0 is acknowledged by clearing). No tuner/signal is supplied: "No Information" is the expected content.
Checks: the original source switch is called with id 0, the run reaches its budget without a guarded stop, and the DTV banner (OSD 3) is shown after the
firmware's own initialisation (~250M dispatches).
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
keys = [(0, 0x41)] + [(14000000 * (i + 1), 0x0b) for i in range(5)] + [(14000000 * 6, 0x0d)]
with tempfile.TemporaryDirectory(prefix='manta-usb-') as tmp:
    d = Path(tmp); (d / 'hist').mkdir()
    cmd = [exe] + base + ['--headless', '--summary', str(d / 's.json'), '--load-state', str(snap), '--load-state-allow-extra-options',
                          '--exploratory-provenance-extensions', '--exploratory-mmio-bits', '1f203fe1:01:01+rmw',
                          *sum([['--exploratory-mmio-bits', h] for h in ('1f225dd8:ff:00+init', '1f225cbc:ff:00+init', '1f206728:ff:00+ack', '1f206729:ff:00+ack', '1f004074:ff:00+init', '1f004075:ff:00+init', '1f203600:ff:00+init', '1f203601:ff:00+init', '1f225cb0:ff:00+init', '1f205e78:ff:00+init@1', '1f205e79:f4:00+init@1')], []),
                          '--max-instructions', str(INJECT + 330000000), '--ir-script', ','.join('%d:raw=0x%02x' % (INJECT + t, k) for t, k in keys),
                          '--gop-frame-history', str(d / 'hist'), '--watch-pc-regs', '8048710c,804c3684']
    subprocess.run(cmd, cwd=repo, capture_output=True)
    s = json.load(open(d / 's.json'))
    frames = json.load(open(d / 'hist/index.json'))['frames']
hits = s['watched_pc_hits']
switch = [h['a0'] for h in hits['0x8048710c']['hits']]
osd = [h['a0'] for h in hits['0x804c3684']['hits']]
assert s['reason'] == 'instruction budget exhausted', (s['reason'], s.get('stop_pc'), s.get('stop_address'))
assert switch == ['0x0'], switch
late = [h for h in hits['0x804c3684']['hits'] if h['a0'] == '0x3' and h['dispatch'] - INJECT > 200000000]
assert late, osd
banner = [f for f in frames if f['dispatches'] - INJECT > 250000000 and f['covered_pixels'] == 337280]
assert banner, [(f['dispatches'] - INJECT, f['covered_pixels']) for f in frames][-5:]
late = banner
Path(a.report).write_text(json.dumps({'result': 'PASS', 'switch_ids': switch, 'osd_calls': osd, 'frames_after_player': len(late), 'last_checksum': late[-1]['checksum']}, indent=1))
print('PASS: DTV source reaches the original channel banner (TVP1, No Information) with real remote keys under the labelled hypotheses')
