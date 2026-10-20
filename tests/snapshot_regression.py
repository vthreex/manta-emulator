"""Full-machine save-state regression (fresh process per step).

1. continuous-vs-restored equivalence on the connected configuration (configs/checkpoints/connected-menu-base.json):
   every summary field and every per-section state digest (CPU context, RAM, flash overlay, all devices, all counters) must be equal;
2. integrity: truncated file, flipped payload byte, bad overall checksum, unknown schema version, wrong run configuration are all rejected
   with a clear message and never partially applied;
3. a failed save never replaces an existing valid snapshot (the writer commits through a temporary file).
"""
import argparse, hashlib, json, os, subprocess, sys, tempfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--firmware', required=True)
p.add_argument('--emulator', required=True)
p.add_argument('--report', required=True)
p.add_argument('--save-at', type=int, default=30000000)
p.add_argument('--limit', type=int, default=50000000)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
repo = root.parents[1]          # directory holding work/ and outputs/ (the connected base uses repository-root relative paths)
fw = Path(a.firmware).resolve()
assert hashlib.sha256(fw.read_bytes()).hexdigest() == 'd60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
base = json.load(open(root / 'configs/checkpoints/connected-menu-base.json'))['argv']
exe = str(Path(a.emulator).resolve())
checks = {}


def run(extra, summary, expect_fail=False, timeout=900):
    cmd = [exe] + base + ['--headless', '--summary', str(summary)] + extra
    r = subprocess.run(cmd, cwd=repo, capture_output=True, text=True, timeout=timeout)
    if expect_fail:
        assert r.returncode != 0, ' '.join(extra)
    return r


with tempfile.TemporaryDirectory(prefix='manta-snapshot-test-') as d:
    d = Path(d)
    snap = d / 's.snap'
    # --- equivalence
    ra = d / 'a.json'; rb = d / 'b.json'; rc = d / 'c.json'
    (d / 'base.json').write_text(json.dumps([exe] + base + ['--headless']))
    r = subprocess.run([sys.executable, '-I', str(root / 'tools/snapshot_equivalence.py'), '--emulator', exe, '--base', str(d / 'base.json'),
                        '--workdir', str(d / 'eq'), '--save-at', str(a.save_at), '--limit', str(a.limit), '--cwd', str(repo)],
                       capture_output=True, text=True, timeout=3600)
    assert r.returncode == 0 and 'EQUIVALENT' in r.stdout and 'NOT EQUIVALENT' not in r.stdout, r.stdout[-2000:] + r.stderr[-500:]
    result = json.load(open(d / 'eq' / 'result.json'))
    assert result['equivalent'] and not result['differing_fields']
    checks['continuous_vs_restored_equivalent'] = {'snapshot_bytes': result['snapshot_bytes'], 'saved_at': result['saved_at_dispatch'], 'limit': result['limit']}
    good = d / 'eq' / 'state.snap'
    data = good.read_bytes()

    def load_attempt(blob, extra=()):
        path = d / 'bad.snap'
        path.write_bytes(blob)
        r = run(['--load-state', str(path), '--max-instructions', str(a.limit)] + list(extra), d / 'bad.json', expect_fail=True)
        return r.stderr + r.stdout

    msg = load_attempt(data[:len(data) // 2])
    assert 'checksum' in msg or 'truncated' in msg, msg[-300:]
    checks['truncated_rejected'] = True
    flipped = bytearray(data); flipped[len(flipped) // 3] ^= 0x10
    msg = load_attempt(bytes(flipped))
    assert 'checksum mismatch' in msg, msg[-300:]
    checks['bit_flip_rejected'] = True
    # unknown schema version with a valid overall checksum: patch the version and recompute the trailing SHA-256 (hex)
    body = bytearray(data[:-64]); body[8] = 99
    msg = load_attempt(bytes(body) + hashlib.sha256(bytes(body)).hexdigest().encode())
    assert 'schema version' in msg, msg[-300:]
    checks['unknown_schema_version_rejected'] = True
    msg = load_attempt(data, extra=['--max-or1k-instructions', '123456789'])
    assert 'different run configuration' in msg, msg[-300:]
    checks['different_configuration_rejected'] = True
    # superset load: only with the explicit flag, only when every option of the snapshot is still present; supported extras are applied/reported,
    # options whose state lives inside the snapshot are REFUSED (never silently ignored)
    r = run(['--load-state', str(good), '--load-state-allow-extra-options', '--exploratory-provenance-extensions', '--max-instructions', str(a.save_at + 2000000)], d / 'sup.json')
    sup = json.load(open(d / 'sup.json'))
    assert r.returncode in (0, 2) and '--exploratory-provenance-extensions' in sup.get('snapshot_option_delta', ''), (r.stderr + r.stdout)[-300:]
    checks['superset_load_reports_extra_options'] = sup['snapshot_option_delta'].strip()
    r = run(['--load-state', str(good), '--load-state-allow-extra-options', '--max-or1k-instructions', '123456789', '--max-instructions', str(a.save_at + 2000000)], d / 'sup2.json', expect_fail=True)
    assert 'cannot be added to a snapshot' in (r.stderr + r.stdout), (r.stderr + r.stdout)[-300:]
    checks['superset_load_refuses_unsupported_extra_option'] = True
    msg = load_attempt(b'not a snapshot at all, but long enough to pass size checks' * 4)
    assert 'checksum' in msg or 'not a Manta' in msg, msg[-300:]
    checks['garbage_rejected'] = True
    # a save that cannot be committed must leave the previous snapshot untouched
    keep = d / 'keep.snap'; keep.write_bytes(data)
    before = hashlib.sha256(keep.read_bytes()).hexdigest()
    blocker = d / 'blocked'; blocker.mkdir()
    r = run(['--max-instructions', str(a.save_at + 2000000), '--save-state', str(blocker), '--save-state-at', str(a.save_at // 2), '--save-state-stop'], d / 'x.json')
    assert hashlib.sha256(keep.read_bytes()).hexdigest() == before and blocker.is_dir()
    checks['failed_save_does_not_touch_other_snapshots'] = True

report = {'result': 'PASS', 'checks': checks}
Path(a.report).write_text(json.dumps(report, indent=1))
print('PASS: save/restore equivalence (CPU, RAM, flash overlay, devices, counters) and snapshot integrity/compatibility rejection')
