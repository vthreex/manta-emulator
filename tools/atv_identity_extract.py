"""Export the original receiver's 8/30 identity/name table for broadcast selection.

This describes transmitted identities; it does not write channel names into RAM.
"""
import argparse
import json
import struct
from pathlib import Path
from mips_join_proof import BASE, load_image

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--firmware', required=True)
p.add_argument('--output', required=True)
a = p.parse_args()
b = load_image(a.firmware)
stations = []
seen_ids, seen_names = set(), set()
for index in range(0x225):  # original lookup loop bound, 805B278C
    at = 0x806b2bd0 - BASE + 28 * index
    name = b[at:at + 5].decode('ascii').strip()
    count = b[at + 12]
    ptr, = struct.unpack_from('<I', b, at + 16)
    if not count:
        continue
    assert BASE <= ptr and ptr - BASE + count * 2 <= len(b)
    ids = struct.unpack_from('<' + 'H' * count, b, ptr - BASE)
    valid = [n for n in ids if n not in (0, 0xfefe, 0xffff) and n not in seen_ids]
    if valid and name not in seen_names:
        stations.append(dict(name=name, id=valid[0]))
        seen_names.add(name)
    seen_ids.update(ids)
assert next(s['id'] for s in stations if s['name'] == 'TVP1') == 0x4801
Path(a.output).write_bytes((json.dumps(dict(stations=stations), indent=1) + '\n').encode())
print('Exported', len(stations), 'original receiver broadcast identities')
