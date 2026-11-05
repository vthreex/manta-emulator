"""Generate deterministic EN 300 706 test packets, not a host renderer.

Usage: python tools/teletext_test_stream.py OUTPUT [BODY] [--raw45]
"""
import argparse
from pathlib import Path

HAM = bytes.fromhex('15 02 49 5e 64 73 38 2f d0 c7 8c 9b a1 b6 fd ea')

def parity(text, width):
    data = text.encode('ascii').ljust(width, b' ')[:width]
    return bytes(c if c.bit_count() % 2 else c | 128 for c in data)

def page(body):
    packets = []
    for row in range(4):
        mrag = bytes([HAM[1 | ((row & 1) << 3)], HAM[row >> 1]])
        if row == 0:
            payload = bytes([HAM[0]]) * 8 + parity('FILE TEST               100 TTX', 32)
        else:
            payload = parity({1: '\x0dFILE TEST', 2: '', 3: body}[row], 40)
        packets.append(mrag + payload)
    # Broadcast service data: initial page 100, unknown network identity.
    packets.append(bytes([HAM[0], HAM[15]]) + bytes.fromhex('15 15 15 15 d0 15 15')
                   + bytes(3) + bytes([0x15])*10 + parity('FILE TEST', 20))
    assert all(len(p) == 42 for p in packets)
    return packets

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('body', nargs='?', default='UPLOADED TELETEXT FILE')
    parser.add_argument('--raw45', action='store_true')
    args = parser.parse_args()
    prefix = bytes.fromhex('55 55 27') if args.raw45 else b''
    args.output.write_bytes(b''.join(prefix + p for p in page(args.body)))
