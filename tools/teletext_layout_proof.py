"""Check the VBI packet layout against the hash-validated original firmware.

This is static evidence, not proof of teletext display or channel naming.
"""
import argparse
import json
import struct
from pathlib import Path
from mips_join_proof import BASE, load_image


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware', required=True)
    p.add_argument('--report', required=True)
    a = p.parse_args()
    image = load_image(a.firmware)
    # Exact instructions: consumer marker, copy start/length, MRAG and page digits.
    expected = {
        0x80455078: 0x24840004,  # addiu a0,a0,4
        0x8045507c: 0x90430001,  # lbu v1,1(v0)
        0x80455084: 0x24020027,
        0x804550dc: 0x2406002c,
        0x805c10e0: 0x90830003,
        0x805c10ec: 0x90840002,
        0x805c3338: 0x92230004,
        0x805c3348: 0x92220005,
        # Original GE MIU selector/address helpers and explicit TEXT format 9.
        0x80525dd4: 0x7c8206c0,
        0x80525dd8: 0x3c0207ff,
        0x80525ddc: 0x3442ffff,
        0x80525de4: 0x00821024,
        0x80525df4: 0x10a00003,
        0x80525dfc: 0x3c028000,
        0x80525e00: 0x00c23025,
        0x80455e88: 0x24020009,
    }
    for pc, word in expected.items():
        actual = struct.unpack_from('<I', image, pc - BASE)[0]
        assert actual == word, (hex(pc), hex(actual), hex(word))
    ham = bytes.fromhex('15 02 49 5e 64 73 38 2f d0 c7 8c 9b a1 b6 fd ea')
    table = image[0x806466c0 - BASE:0x806467c0 - BASE]
    assert [table[b] for b in ham] == list(range(16))
    result = dict(result='PASS', scope='static original-firmware layout only',
                  exact_instructions=len(expected), hamming_codes=16,
                  slot_copy_offset=4, copied_bytes=44, marker_slot_offset=5,
                  mrag_slot_offsets=[6, 7], page_digits_slot_offsets=[8, 9],
                  displayed_page_verified=False, channel_name_verified=False)
    Path(a.report).write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: original VBI consumer layout and Hamming codes; runtime display unverified')


if __name__ == '__main__':
    main()
