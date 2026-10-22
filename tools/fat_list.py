"""List and extract files from an MBR + FAT32 image (e.g. the virtual pendrive after the guest wrote to it).

usage: fat_list.py IMAGE [--extract DIR]
"""
import argparse, struct, sys
from pathlib import Path

def main():
    p = argparse.ArgumentParser(); p.add_argument('image'); p.add_argument('--extract'); a = p.parse_args()
    d = open(a.image, 'rb').read()
    part = struct.unpack_from('<I', d, 446 + 8)[0]
    b = part * 512
    bps, spc, rsv, nfat = struct.unpack_from('<HBHB', d, b + 11)
    fatsz = struct.unpack_from('<I', d, b + 36)[0]; root = struct.unpack_from('<I', d, b + 44)[0]
    fat0 = b + rsv * bps; data = fat0 + nfat * fatsz * bps
    def fat(c): return struct.unpack_from('<I', d, fat0 + 4 * c)[0] & 0x0fffffff
    def chain(c):
        out = []
        while 2 <= c < 0x0ffffff8 and len(out) < 1 << 20: out.append(c); c = fat(c)
        return out
    def cl(c): o = data + (c - 2) * spc * bps; return d[o:o + spc * bps]
    def walk(c, path):
        raw = b''.join(cl(x) for x in chain(c)); lfn = []
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0: break
            if e[0] == 0xe5: lfn = []; continue
            if e[11] == 0x0f: lfn.insert(0, e[1:11] + e[14:26] + e[28:32]); continue
            name = b''.join(lfn).decode('utf-16le', 'ignore').split('\x00')[0] if lfn else e[:8].decode('latin1').strip() + ('.' + e[8:11].decode('latin1').strip() if e[8:11].strip() else '')
            lfn = []
            if e[11] & 0x08: continue
            first = (struct.unpack_from('<H', e, 20)[0] << 16) | struct.unpack_from('<H', e, 26)[0]; size = struct.unpack_from('<I', e, 28)[0]
            full = path + '/' + name
            if name in ('.', '..'): continue
            if e[11] & 0x10:
                print('DIR ', full); walk(first, full)
            else:
                print('FILE', full, size, 'bytes')
                if a.extract:
                    out = Path(a.extract) / full.lstrip('/'); out.parent.mkdir(parents=True, exist_ok=True)
                    out.write_bytes(b''.join(cl(x) for x in chain(first))[:size])
    walk(root, '')
main()
