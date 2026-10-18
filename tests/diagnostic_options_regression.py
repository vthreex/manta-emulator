"""New diagnostic/exploratory options must be opt-in, guarded and labeled."""
import argparse, hashlib, json, os, subprocess, tempfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
for n in ('firmware', 'emulator', 'report'):
    p.add_argument('--' + n, required=True)
a = p.parse_args()
firmware, exe = Path(a.firmware).resolve(), Path(a.emulator).resolve()
sha = hashlib.sha256(firmware.read_bytes()).hexdigest()
assert sha == 'd60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'


def run(extra):
    return subprocess.run([str(exe), '--firmware', str(firmware), '--headless'] + extra,
                          capture_output=True, text=True, timeout=60)


checks = {}
# Each new option is rejected without its guarded prerequisites.
for name, extra, text in (
    ('join-needs-guarded-exploration', ['--diagnostic-htotal-branch-join'], 'requires guarded symbolic exploration'),
    ('no-sync-needs-hsync-and-avd-run', ['--exploratory-avd-no-sync-events'], 'requires the absent-HSYNC hypothesis'),
    ('invalid-publication-needs-avd-run', ['--diagnostic-avd-invalid-publication'], 'requires the guarded shared AVD run'),
    ('11db-bracket-needs-avd-run', ['--diagnostic-avd-11db-bit7', '1'], 'requires the guarded shared AVD run'),
    ('nor-write-needs-candidate-spi', ['--candidate-spi-nor-write'], 'requires the candidate SPI model'),
    ('restore-branch-needs-nor-write', ['--diagnostic-restore-by-identity-branch'], 'requires guarded exploration and the SPI NOR write model'),
):
    r = run(extra)
    assert r.returncode == 1 and text in r.stderr, (name, r.returncode, r.stderr)
    checks[name] = True
r = run(['--diagnostic-avd-11db-bit7', '2'])
assert r.returncode == 1 and 'must be 0 or 1' in r.stderr, r.stderr
checks['11db-bracket-rejects-non-bit'] = True
env = os.environ.copy()
env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32')
help_text = subprocess.check_output([str(exe), '--help'], text=True, env=env, timeout=15)
for flag, label in (('--diagnostic-htotal-branch-join', 'DIAGNOSTIC ONLY, not hardware behavior'),
                    ('--exploratory-avd-no-sync-events', 'HYPOTHESIS'),
                    ('--diagnostic-avd-invalid-publication', 'DIAGNOSTIC'),
                    ('--diagnostic-avd-11db-bit7', 'BRACKETING DIAGNOSTIC'),
                    ('--candidate-spi-nor-write', 'copy-on-write overlay'),
                    ('--diagnostic-restore-by-identity-branch', 'DIAGNOSTIC ONLY')):
    assert flag in help_text and label in help_text, flag
checks['help-labels'] = True
assert hashlib.sha256(firmware.read_bytes()).hexdigest() == sha
Path(a.report).write_text(json.dumps({'result': 'PASS', 'checks': checks, 'firmware_sha256': sha,
                                      'emulator_sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}, indent=2) + '\n')
print('PASS: diagnostic options are guarded, validated and labeled')
