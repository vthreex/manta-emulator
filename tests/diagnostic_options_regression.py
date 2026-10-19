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
    ('vif-lock-bracket-needs-guarded-exploration', ['--diagnostic-vif-cr-lock', '1'], 'requires guarded exploration'),
    ('htotal-constant-needs-absent-hsync', ['--exploratory-avd-htotal-constant', '0'], 'requires the absent-HSYNC hypothesis'),
    ('uniform-stretch-needs-ge-bitblt', ['--exploratory-ge-uniform-stretch', 'opaque-source'], 'requires the candidate GE BitBlt model'),
    ('i2-glyph-needs-ge-bitblt', ['--exploratory-ge-i2-glyph'], 'requires the candidate GE BitBlt model'),
    ('blend-copy-needs-ge-bitblt', ['--exploratory-ge-blend-copy'], 'requires the candidate GE BitBlt model'),
    ('noise-constant-needs-absent-hsync', ['--exploratory-avd-noise-constant', '0'], 'requires the absent-HSYNC hypothesis'),
    ('afec-indirect-needs-guarded-exploration', ['--exploratory-afec-indirect-6a3c', '0'], 'requires guarded exploration'),
    ('mmio-bits-need-guarded-exploration', ['--exploratory-mmio-bits', '1f206f5d:52:10'], 'requires guarded exploration'),
    ('height5-needs-a-model', ['--exploratory-ge-height5-full-copy'], 'requires an exploratory blend-copy or I2 glyph model'),
    ('i8-fill-needs-rect-model', ['--exploratory-ge-i8-fill'], 'requires the candidate GE rectangle model'),
    ('nearest-stretch-needs-uniform-opaque', ['--exploratory-ge-nearest-stretch'], 'requires --exploratory-ge-uniform-stretch opaque-source'),
    ('gradient-fill-needs-rect-model', ['--exploratory-ge-gradient-fill'], 'requires the candidate GE rectangle model'),
    ('fill-ignore-reg00-needs-rect-model', ['--exploratory-ge-fill-ignore-reg00', '6'], 'requires the candidate GE rectangle model'),
    ('rpt-latch-needs-pm-ir-fifo', ['--exploratory-pm-ir-rpt-latch'], 'requires the PM IR FIFO'),
    ('ir-script-rejects-garbage', ['--ir-script', 'abc'], ''),
    ('nor-write-needs-candidate-spi', ['--candidate-spi-nor-write'], 'requires the candidate SPI model'),
    ('restore-branch-needs-nor-write', ['--diagnostic-restore-by-identity-branch'], 'requires guarded exploration and the SPI NOR write model'),
):
    r = run(extra)
    assert r.returncode == 1 and text in r.stderr, (name, r.returncode, r.stderr)
    checks[name] = True
r = run(['--diagnostic-avd-11db-bit7', '2'])
assert r.returncode == 1 and 'must be 0 or 1' in r.stderr, r.stderr
checks['11db-bracket-rejects-non-bit'] = True
r = run(['--exploratory-avd-htotal-constant', '65536'])
assert r.returncode == 1 and 'must be 0..0xFFFF' in r.stderr, r.stderr
checks['htotal-constant-rejects-out-of-range'] = True
r = run(['--diagnostic-vif-cr-lock', '2'])
assert r.returncode == 1 and 'must be 0 or 1' in r.stderr, r.stderr
checks['vif-lock-bracket-rejects-non-bit'] = True
env = os.environ.copy()
env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32')
help_text = subprocess.check_output([str(exe), '--help'], text=True, env=env, timeout=15)
for flag, label in (('--diagnostic-htotal-branch-join', 'DIAGNOSTIC ONLY, not hardware behavior'),
                    ('--exploratory-avd-no-sync-events', 'HYPOTHESIS'),
                    ('--diagnostic-avd-invalid-publication', 'DIAGNOSTIC'),
                    ('--diagnostic-avd-11db-bit7', 'BRACKETING DIAGNOSTIC'),
                    ('--exploratory-avd-htotal-constant', 'not a TV measurement'),
                    ('--exploratory-avd-noise-constant', 'not a measurement'),
                    ('--exploratory-afec-indirect-6a3c', 'not a measurement'),
                    ('--exploratory-mmio-bits', 'not a measurement'),
                    ('--exploratory-ge-height5-full-copy', 'NOT TITANIA4-VERIFIED'),
                    ('--exploratory-ge-i8-fill', 'NOT TITANIA4-VERIFIED'),
                    ('--exploratory-ge-uniform-stretch', 'NOT TITANIA4-VERIFIED'),
                    ('--exploratory-ge-i2-glyph', 'NOT TITANIA4-VERIFIED'),
                    ('--exploratory-ge-blend-copy', 'NOT TITANIA4-VERIFIED'),
                    ('--diagnostic-vif-cr-lock', 'BRACKETING DIAGNOSTIC'),
                    ('--export-gop-frame', 'never an authentic-frame claim'),
                    ('--exploratory-ge-nearest-stretch', 'NOT TITANIA4-VERIFIED'),
                    ('--exploratory-ge-gradient-fill', 'NOT TITANIA4-VERIFIED'),
                    ('--exploratory-ge-fill-ignore-reg00', 'NOT TITANIA4-VERIFIED'),
                    ('--exploratory-pm-ir-rpt-latch', 'HYPOTHESIS'),
                    ('--ir-script', 'raw=0xNN'),
                    ('--candidate-spi-nor-write', 'copy-on-write overlay'),
                    ('--diagnostic-restore-by-identity-branch', 'DIAGNOSTIC ONLY')):
    assert flag in help_text and label in help_text, flag
checks['help-labels'] = True
assert hashlib.sha256(firmware.read_bytes()).hexdigest() == sha
Path(a.report).write_text(json.dumps({'result': 'PASS', 'checks': checks, 'firmware_sha256': sha,
                                      'emulator_sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}, indent=2) + '\n')
print('PASS: diagnostic options are guarded, validated and labeled')
