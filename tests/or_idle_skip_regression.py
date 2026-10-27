"""Exactness of the auxiliary-CPU idle-loop fast-forward (host speed only).

The TSP OpenRISC core spends most of a connected boot in one idle dispatcher loop. Riu::advanceTsp fast-forwards whole
iterations of such a loop when it can prove them side-effect free (identical architectural state, net memory effect none,
every external read re-verified). This test runs the same cold boot twice, once with the fast-forward disabled
(MANTA_NO_OR_SKIP=1) and once enabled, and requires every summary field and every per-section state digest to be equal,
for both the interpreter and, when the executable supports it, nothing else: the native CPU shares the same Riu code.
The enabled run must also be clearly faster (the skip must actually engage), unless --no-speed-check is given.
"""
import argparse, hashlib, json, os, subprocess, tempfile, time
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--firmware', required=True)
p.add_argument('--emulator', required=True)
p.add_argument('--report', required=True)
p.add_argument('--limit', type=int, default=330000000, help='dispatch budget; the auxiliary CPU starts after about 230 M')
p.add_argument('--min-speedup', type=float, default=1.5)
p.add_argument('--no-speed-check', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
repo = root.parents[1]
fw = Path(a.firmware).resolve()
assert hashlib.sha256(fw.read_bytes()).hexdigest() == 'd60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
base = json.load(open(root / 'configs/checkpoints/connected-menu-base.json'))['argv']
base = list(base)
for flag in ('--max-instructions', '--summary', '--save-state', '--save-state-at', '--profile'):
    while flag in base:
        j = base.index(flag)
        del base[j:j + 2]
exe = str(Path(a.emulator).resolve())


def run(summary, skip):
    env = dict(os.environ)
    env.pop('MANTA_NO_OR_SKIP', None)
    if not skip:
        env['MANTA_NO_OR_SKIP'] = '1'
    cmd = [exe] + base + ['--max-instructions', str(a.limit), '--headless', '--state-digest', '--summary', str(summary)]
    t0 = time.time()
    r = subprocess.run(cmd, cwd=repo, capture_output=True, text=True, env=env, timeout=3600)
    dt = time.time() - t0
    assert Path(summary).exists(), r.stderr[-500:]
    return json.load(open(summary)), dt


with tempfile.TemporaryDirectory(prefix='manta-or-skip-test-') as d:
    d = Path(d)
    off, t_off = run(d / 'off.json', False)
    on, t_on = run(d / 'on.json', True)

differing = sorted(k for k in set(off) | set(on) if off.get(k) != on.get(k))
assert not differing, 'fast-forward changed observable state: %s' % differing[:20]
assert off['reason'] == 'instruction budget exhausted', off['reason']
assert off['or1k_instructions'] > 100000000, 'the auxiliary CPU did not run long enough to exercise the idle loop'
assert 'state_digest' in off and off['state_digest'] == on['state_digest']
speedup = t_off / t_on
if not a.no_speed_check:
    assert speedup >= a.min_speedup, 'idle-loop fast-forward did not engage: %.1fs -> %.1fs' % (t_off, t_on)
report = {'fields_compared': len(off), 'differing': differing, 'or1k_instructions': off['or1k_instructions'],
          'dispatches': off['instruction_dispatches'], 'seconds_without_skip': round(t_off, 1), 'seconds_with_skip': round(t_on, 1),
          'speedup': round(speedup, 2), 'state_digest': off['state_digest']}
Path(a.report).write_text(json.dumps(report, indent=2))
print('PASS', json.dumps(report))
