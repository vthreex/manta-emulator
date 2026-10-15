"""Original VIF handler control flow under isolated, explicitly synthetic measurements."""
import argparse
import hashlib
import json
import subprocess
import tempfile
import zlib
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
for name in ('firmware', 'probe', 'report'):
    parser.add_argument('--' + name, required=True)
parser.add_argument('--emulator', help='Also check strict rejection and explicit reporting of the ideal-input option')
args = parser.parse_args()
stock = Path(args.firmware).read_bytes()
digest = hashlib.sha256(stock).hexdigest()
assert digest == 'd60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
image = zlib.decompress(stock[0x23880:0x16124e], -15)
with tempfile.TemporaryDirectory(prefix='manta-vif-isolated-') as directory:
    path = Path(directory) / 'original-mips.bin'
    path.write_bytes(image)
    result = subprocess.run([str(Path(args.probe).resolve()), str(path)], capture_output=True, text=True)
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    report = json.loads(result.stdout)
    assert report['result'] == 'PASS' and report['measurement_cases'] == 2048
    assert report['guest_nonacquisition_returns'] == 272
    assert report['guest_acquisition_advances'] == 1776
    assert report['maximum_original_instructions_per_call'] < 128
    assert report['tv_measurements_supplied'] is False
    assert report['positive_modulation_cases'] == 2048 and report['positive_nonacquisition_returns'] == 450
    assert report['ideal_zero_input_original_calls'] == 6 and report['ideal_zero_input_VGA_remains_invalid']
    assert report['original_unknown_VGA_decisions_rejected'] == 186
    if args.emulator:
        executable = str(Path(args.emulator).resolve())
        base = [executable, '--firmware', str(Path(args.firmware).resolve()), '--headless']
        option = '--exploratory-vif-zero-input-settle-cycles'
        rejected = 0
        for flags in ([option, '1'], ['--symbolic-init', '--defer-diagnostic-reads', option, '1'],
                      [option, '0'], [option, '-1'], [option, '1junk']):
            result = subprocess.run(base + flags, capture_output=True, text=True)
            assert result.returncode == 1 and ('VIF' in result.stderr), result.stderr
            rejected += 1
        summary = Path(directory) / 'prefix.json'
        result = subprocess.run(base + ['--symbolic-init', '--defer-diagnostic-reads',
            '--exploratory-analog-disconnected', option, '4320000', '--max-instructions', '150',
            '--summary', str(summary)], capture_output=True, text=True)
        assert result.returncode == 2, result.stderr
        state = json.loads(summary.read_text())
        assert state['exploratory_vif_ideal_zero_energy'] and state['vif_zero_input_settle_cycles'] == 4320000
        assert state['vif_zero_input_samples'] == state['vif_zero_input_settlements'] == 0
        assert state['vif_disconnected_antenna_hardware_verified'] is False
        report['CLI_rejected_configurations'] = rejected
        report['CLI_reports_hypothesis_without_manufacturing_early_measurements'] = True
assert hashlib.sha256(Path(args.firmware).read_bytes()).hexdigest() == digest
report['probe_sha256'] = hashlib.sha256(Path(args.probe).read_bytes()).hexdigest()
Path(args.report).write_text(json.dumps(report, indent=2) + '\n')
print('PASS: 4096 original modulation/measurement cases and six ideal-zero-input calls; VGA remains invalid')
