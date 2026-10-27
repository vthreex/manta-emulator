"""Writes a machine state with the original firmware sitting on the DTV channel banner (reached with real remote keys, as tests/dtv_regression.py does), so
that key sweeps can run in a TV-source state. Native engine; about a minute.

usage: make_dtv_state.py OUT.snap [extra_dispatches_after_banner]
"""
import json, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
root = HERE.parent
repo = root.parents[1]
sys.path.insert(0, str(root / 'frontend'))
from emulator_session import emulator_candidates  # noqa: E402

out = Path(sys.argv[1]).resolve()
extra = int(sys.argv[2]) if len(sys.argv) > 2 else 0
snap = repo / 'work/checkpoints/no-signal-ready-v2.snap'
base = json.load(open(root / 'configs/checkpoints/connected-menu-base.json'))['argv']
INJECT = 1300000000
keys = [(0, 0x41)] + [(14000000 * (i + 1), 0x0b) for i in range(5)] + [(14000000 * 6, 0x0d)]
hyp = ('1f225dd8:ff:00+init', '1f225cbc:ff:00+init', '1f206728:ff:00+ack', '1f206729:ff:00+ack', '1f004074:ff:00+init', '1f004075:ff:00+init', '1f203600:ff:00+init',
       '1f203601:ff:00+init', '1f225cb0:ff:00+init', '1f205e78:ff:00+init@1', '1f205e79:f4:00+init@1')
at = INJECT + 300000000 + extra
cmd = [emulator_candidates()[0]] + base + ['--headless', '--summary', str(out) + '.summary.json', '--load-state', str(snap), '--load-state-allow-extra-options',
       '--exploratory-provenance-extensions', '--exploratory-mmio-bits', '1f203fe1:01:01+rmw']
for h in hyp:
    cmd += ['--exploratory-mmio-bits', h]
cmd += ['--max-instructions', str(at + 1000), '--ir-script', ','.join('%d:raw=0x%02x' % (INJECT + t, k) for t, k in keys), '--save-state', str(out), '--save-state-at', str(at), '--save-state-stop']
r = subprocess.run(cmd, cwd=str(repo), capture_output=True, text=True)
print(r.returncode, (r.stdout + r.stderr)[-300:])
print(out, out.exists())
