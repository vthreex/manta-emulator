"""Continuous-versus-restored equivalence test for the full-machine save state.

Path A  : fresh process runs N2 dispatches continuously.
Path B  : fresh process runs to the first safe boundary at/after N1, writes the snapshot and stops; a SECOND fresh process loads it and
          continues to N2.
The two final summaries must be identical (every field), including the per-section SHA-256 digests of the complete serialized machine
state (CPU context, guest RAM, flash overlay, every device, every counter) and the guest-visible output fields.

usage: snapshot_equivalence.py --emulator EXE --base COMMAND.json --workdir DIR --save-at N1 --limit N2 [--extra ARG ...] [--keep]
BASE is a JSON list holding a complete emulator command line (argv[0] included); output/trace/coverage/budget options in it are replaced.
"""
import argparse, json, os, subprocess, sys, time
from pathlib import Path

DROP_WITH_VALUE = {'--summary', '--coverage', '--pm51-history', '--record-trace', '--replay-trace', '--max-instructions', '--save-state',
                   '--save-state-at', '--load-state', '--export-gop-frame', '--dump-prefix', '--dump-guest-range', '--tsp-firmware-dump'}
p = argparse.ArgumentParser()
p.add_argument('--emulator', required=True)
p.add_argument('--base', required=True)
p.add_argument('--workdir', required=True)
p.add_argument('--save-at', type=int, required=True)
p.add_argument('--limit', type=int, required=True)
p.add_argument('--cwd', default=None)
p.add_argument('--extra', nargs='*', default=[])
p.add_argument('--second-extra', nargs='*', default=[], help='extra options only for the resumed run (output/remote options; must not change hardware behaviour)')
a = p.parse_args()
work = Path(a.workdir)
work.mkdir(parents=True, exist_ok=True)
base = json.load(open(a.base))
cmd = [a.emulator]
i = 1
while i < len(base):
    if base[i] in DROP_WITH_VALUE:
        i += 2
        continue
    cmd.append(base[i])
    i += 1
cmd += a.extra + ['--state-digest']


def launch(extra, tag):
    out = work / (tag + '.json')
    if out.exists():
        out.unlink()
    log = open(work / (tag + '.log'), 'w')
    proc = subprocess.Popen(cmd + extra + ['--summary', str(out)], stdout=log, stderr=subprocess.STDOUT, cwd=a.cwd)
    return proc, out


def finish(proc, out, tag, timeout=7200):
    t0 = time.time()
    while proc.poll() is None:
        time.sleep(2)
        if out.exists() and time.time() - out.stat().st_mtime > 8:
            proc.kill()          # the emulator can linger after writing its summary (windowed shutdown issue, tracked separately)
            break
        if time.time() - t0 > timeout:
            proc.kill(); raise SystemExit('TIMEOUT ' + tag)
    return json.load(open(out))


state = work / 'state.snap'
if state.exists():
    state.unlink()
pa, oa = launch(['--max-instructions', str(a.limit)], 'A')
pb, ob = launch(['--max-instructions', str(a.limit), '--save-state', str(state), '--save-state-at', str(a.save_at), '--save-state-stop'], 'B1')
b1 = finish(pb, ob, 'B1')
assert state.exists(), 'B1 produced no snapshot: ' + str(b1.get('stop_reason') or b1.get('reason'))
t = time.time()
pc, oc = launch(['--max-instructions', str(a.limit), '--load-state', str(state)] + a.second_extra, 'B2')
sa = finish(pa, oa, 'A')
sc = finish(pc, oc, 'B2')
print('snapshot bytes', state.stat().st_size, 'saved at dispatch', b1['instruction_dispatches'])
diff = sorted(k for k in set(sa) | set(sc) if sa.get(k) != sc.get(k))
print('summary fields A:', len(sa), 'B2:', len(sc), 'differing:', len(diff))
for k in diff[:40]:
    print('  DIFF', k, str(sa.get(k))[:100], '|', str(sc.get(k))[:100])
print('state digest A :', sa.get('state_digest'))
print('state digest B2:', sc.get('state_digest'))
ok = not diff and sa.get('state_digest') == sc.get('state_digest')
json.dump({'equivalent': ok, 'differing_fields': diff, 'snapshot_bytes': state.stat().st_size, 'saved_at_dispatch': b1['instruction_dispatches'],
           'limit': a.limit, 'digest_A': sa.get('state_digest'), 'digest_B2': sc.get('state_digest')}, open(work / 'result.json', 'w'), indent=1)
print('EQUIVALENT' if ok else 'NOT EQUIVALENT')
sys.exit(0 if ok else 1)
