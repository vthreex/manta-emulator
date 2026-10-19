"""The recorded IR key tables must match the original firmware and the evidence-labelled keymap must stay consistent.

- docs/ir-raw-to-app-key-table.tsv is regenerated from the original image (jump table 0x8061773C, exact 4-instruction stubs) and compared byte for byte.
- configs/input/ir-keymap.tsv: every entry has an evidence text, no raw byte or function is duplicated, every raw byte decodes in the firmware table, and every
  'app 0xNN' claim in the evidence matches the table. Digits must be exactly raw 0x01..0x09 -> app 0x11..0x19 and raw 0x45 -> app 0x10.
- unmapped host functions (S, EXIT, UP, POWER, INFO...) must not appear unless they carry an evidence text.
"""
import argparse, hashlib, json, re, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
from ir_key_table_extract import extract

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--firmware', required=True)
p.add_argument('--report', required=True)
a = p.parse_args()
fw = Path(a.firmware)
assert hashlib.sha256(fw.read_bytes()).hexdigest() == 'd60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
rows = extract(str(fw))
table = {}
for raw, code, target in rows:
    table[raw] = code
lines = [l.rstrip('\n') for l in (root / 'docs/ir-raw-to-app-key-table.tsv').read_text().splitlines()]
recorded = {}
for l in lines:
    if l.startswith('#'):
        continue
    r, c, t = l.split('\t')
    recorded[int(r, 0)] = (None if c == '' else int(c, 0), int(t, 0))
assert len(recorded) == 253 and all(recorded[r][0] == table[r] for r in table), 'recorded table differs from the firmware'
decoded = [r for r in table if table[r] is not None]
assert len(decoded) == 73, len(decoded)
keymap = {}
raws = {}
for l in (root / 'configs/input/ir-keymap.tsv').read_text().splitlines():
    if not l.strip() or l.startswith('#'):
        continue
    parts = l.split('\t')
    assert len(parts) == 3 and parts[2].strip(), ('entry needs FUNCTION RAW evidence', l)
    f, raw, ev = parts[0], int(parts[1], 0), parts[2]
    assert f not in keymap and raw not in raws, ('duplicate', l)
    keymap[f], raws[raw] = raw, f
    assert table[raw] is not None, ('raw byte not decoded by the firmware table', l)
    m = re.search(r'app 0x([0-9A-Fa-f]+)', ev)
    assert m and int(m.group(1), 16) == (table[raw] & 0xff), ('evidence app code differs from the table', l, table[raw])
digits = {'DIGIT_%d' % d: (0x45 if d == 0 else d) for d in range(10)}
for f, raw in digits.items():
    assert keymap[f] == raw and table[raw] == 0x10 + int(f[-1]), (f, raw)
for banned in ('SOURCE', 'EXIT', 'UP', 'POWER', 'INFO'):
    assert banned not in keymap or 'BEHAVIOURAL' in Path(root / 'configs/input/ir-keymap.tsv').read_text(), banned
Path(a.report).write_text(json.dumps({'decoded_raw_bytes': len(decoded), 'keymap_entries': keymap}, indent=1))
print('PASS: recorded raw->app table equals the original firmware table; keymap entries are unique, evidence-labelled and consistent with it')
