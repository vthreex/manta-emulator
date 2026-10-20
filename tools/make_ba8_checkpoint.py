"""Create the NO SIGNAL checkpoint with the AFEC BF206BA8 bit-4 reset value seeded (needed for the ATV source; docs/source-switching.md).

usage: make_ba8_checkpoint.py --emulator EXE --out work/checkpoints/ns-ba8i-2.snap [--value 0|16]
A cold boot (about 10 minutes) with the labelled reset-value hypothesis --exploratory-mmio-bits 1f206ba8:10:V+init; the boot reads that byte once and caches it in RAM
(0x806D83BE), so the hypothesis must exist before the checkpoint is taken. Both values (0 and 0x10) continue identically through the ATV path.
"""
import argparse, json, subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--emulator', required=True)
p.add_argument('--out', required=True)
p.add_argument('--value', type=int, default=0, choices=(0, 16))
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
repo = root.parents[1]
base = json.load(open(root / 'configs/checkpoints/connected-menu-base.json'))['argv']
out = Path(a.out).resolve()
cmd = [str(Path(a.emulator).resolve())] + base + ['--headless', '--summary', str(out.with_suffix('.summary.json')), '--exploratory-mmio-bits', '1f206ba8:10:%02x+init' % a.value,
                                                  '--save-state', str(out), '--save-state-at', '1300000000', '--save-state-stop', '--max-instructions', '1400000000']
subprocess.run(cmd, cwd=repo, check=False)
print(out, out.exists() and out.stat().st_size)
