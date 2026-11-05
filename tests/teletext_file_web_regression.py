"""Upload coded packet files to the real WASM firmware and compare guest frames."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(repo / 'tools'))
from teletext_test_stream import page

p = argparse.ArgumentParser(description=__doc__)
for key in ('web', 'args', 'snapshot', 'firmware', 'artifacts', 'report'):
    p.add_argument('--' + key, required=True)
a = p.parse_args()
out = Path(a.artifacts).resolve()
out.mkdir(parents=True, exist_ok=False)
results = []
for tag, body, prefix in [('raw42', 'UPLOADED TELETEXT FILE', b''),
                          ('raw45', 'UPLOADED TELETEXT FILE', bytes.fromhex('55 55 27')),
                          ('changed', 'SECOND BROADCAST BODY', b'')]:
    packet_file = out / (tag + '.ttx')
    packet_file.write_bytes(b''.join(prefix + line for line in page(body)))
    target = out / tag
    subprocess.run(['node', str(repo / 'native/web/node_teletext_test.mjs'),
                    a.web, a.args, a.snapshot, a.firmware, str(target), str(packet_file)],
                   check=True, timeout=180)
    result = json.loads(target.with_suffix('.json').read_text())
    results.append(dict(case=tag, **result))
assert results[0]['sha256'] == results[1]['sha256'], 'framing changed decoded guest pixels'
assert results[0]['sha256'] != results[2]['sha256'], 'uploaded body did not change guest pixels'
Path(a.report).write_text(json.dumps(dict(result='PASS', cases=results,
    scope='original firmware in browser WASM, explicit exploratory VBI/GOP models'), indent=2) + '\n')
print('PASS: raw42 and raw45 render identically; changed uploaded body changes guest pixels')
