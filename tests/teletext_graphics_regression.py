"""Connected original-firmware TEXT test from a recovered checkpoint command.

--command is a JSON argv list with the firmware, snapshot and original options.
All new hardware/presentation hypotheses are explicit in each run. Two payloads
with the SAME station name must produce different indexed GOP frames.
This does not verify TV channel-name storage or actual Titania4 pixel rounding.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--command', required=True)
p.add_argument('--emulator', required=True)
p.add_argument('--artifacts', required=True)
p.add_argument('--report', required=True)
a = p.parse_args()
base = json.loads(Path(a.command).read_text())
assert isinstance(base, list) and all(isinstance(x, str) for x in base)
root = Path(__file__).resolve().parents[3]
artifacts = Path(a.artifacts).resolve()
artifacts.mkdir(parents=True, exist_ok=False)
results = []
for tag, text in [('alpha', 'ALPHA TEXT'), ('beta', 'BETA TEXT')]:
    out = artifacts / tag
    out.mkdir()
    cmd = base.copy()
    cmd[0] = str(Path(a.emulator).resolve())
    def value(flag, text):
        if flag in cmd:
            cmd[cmd.index(flag) + 1] = str(text)
        else:
            cmd.extend([flag, str(text)])
    value('--max-instructions', 2000000000)
    value('--summary', out / 'summary.json')
    value('--exploratory-atv-stations', '864|RECOVERY|' + text)
    value('--gop-frame-history', out)
    value('--gop-history-interval-log2', 24)
    for flag in ['--exploratory-gop-palette-dma', '--exploratory-gop-indexed-preview']:
        if flag not in cmd:
            cmd.append(flag)
    (out / 'command.json').write_text(json.dumps(cmd, indent=2))
    run = subprocess.run(cmd, cwd=root, capture_output=True, timeout=180)
    (out / 'stderr.txt').write_bytes(run.stderr)
    s = json.loads((out / 'summary.json').read_text())
    assert s['reason'] == 'instruction budget exhausted', (tag, s['reason'])
    assert s['instruction_dispatches'] >= 2000000000
    assert s['gop_palette_defined_entries'][0] == 256
    assert any(w['bank'] == 1 and w['window'] == 0 and w['format'] == 4
               for w in s['gop_active_descriptors'])
    frames = json.loads((out / 'index.json').read_text())['frames']
    f = frames[-1]
    assert f['covered_pixels'] == 1920 * 1080, (tag, f)
    bitmap = (out / f['file']).read_bytes()
    results.append(dict(case=tag, dispatches=s['instruction_dispatches'],
                        reason=s['reason'], covered_pixels=f['covered_pixels'],
                        frame_sha256=hashlib.sha256(bitmap).hexdigest(),
                        frame=str(out / f['file'])))
assert results[0]['frame_sha256'] != results[1]['frame_sha256'], 'received body text did not change the guest frame'
Path(a.report).write_text(json.dumps(dict(result='PASS',
    scope='connected original-firmware teletext with explicit exploratory models',
    channel_name_storage_verified=False, cases=results), indent=2) + '\n')
print('PASS: two received body texts produce different original-firmware GOP frames; no GE stop to 2B')
