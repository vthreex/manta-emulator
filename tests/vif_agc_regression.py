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
assert hashlib.sha256(Path(args.firmware).read_bytes()).hexdigest() == digest
report['probe_sha256'] = hashlib.sha256(Path(args.probe).read_bytes()).hexdigest()
Path(args.report).write_text(json.dumps(report, indent=2) + '\n')
print('PASS: 2048 original VIF handler calls return naturally; isolated measurements never supplied to TV boot')
