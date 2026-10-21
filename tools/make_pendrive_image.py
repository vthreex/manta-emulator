#!/usr/bin/env python3
"""Build a small FAT32 USB-stick image (MBR + one FAT32 partition) for the emulator's virtual pendrive.

    python tools/make_pendrive_image.py OUT.img DIR [--size-mib 128] [--label MANTA]

Every regular file under DIR is copied (sub-directories are kept, names are stored with VFAT long-file-name entries plus a unique 8.3
alias). Standard library only. The image is a plain disk image: the emulator exposes it read/write in memory and never modifies the file.
"""
import argparse, os, struct, sys, time

SECTOR = 512


def short_alias(name, used):
    base, dot, ext = name.rpartition('.') if '.' in name else (name, '', '')
    clean = lambda s: ''.join(c for c in s.upper() if c.isalnum() or c in '_-$~!#%&-{}()@\'`^')
    b, e = clean(base)[:8] or 'FILE', clean(ext)[:3]
    for n in range(0, 10000):
        cand_b = b if n == 0 else (b[:8 - len(str(n)) - 1] + '~' + str(n))
        cand = (cand_b.ljust(8) + e.ljust(3))
        if cand not in used:
            used.add(cand)
            return cand
    raise SystemExit('cannot alias ' + name)


def lfn_checksum(alias):
    s = 0
    for c in alias.encode('ascii'):
        s = (((s & 1) << 7) + (s >> 1) + c) & 0xFF
    return s


def lfn_entries(name, alias):
    chk = lfn_checksum(alias)
    u = name.encode('utf-16-le')
    chars = [u[i] | u[i + 1] << 8 for i in range(0, len(u), 2)]
    chars.append(0)
    while len(chars) % 13:
        chars.append(0xFFFF)
    out = []
    parts = [chars[i:i + 13] for i in range(0, len(chars), 13)]
    for i, p in enumerate(parts):
        seq = i + 1 | (0x40 if i == len(parts) - 1 else 0)
        e = struct.pack('<B5HBBB6HH2H', seq, *p[0:5], 0x0F, 0, chk, *p[5:11], 0, *p[11:13])
        out.append(e)
    return out[::-1]


class Fat32:
    def __init__(self, total_sectors, spc=2, label='MANTA'):
        self.spc = spc
        self.reserved = 32
        self.total = total_sectors
        self.fat_sectors = 0
        # solve FAT size
        clusters = (total_sectors - self.reserved) // spc
        for _ in range(10):
            self.fat_sectors = ((clusters + 2) * 4 + SECTOR - 1) // SECTOR
            clusters = (total_sectors - self.reserved - 2 * self.fat_sectors) // spc
        self.clusters = clusters
        assert clusters >= 65525, 'FAT32 needs at least 65525 clusters; use a bigger image or fewer sectors per cluster'
        self.data = bytearray(total_sectors * SECTOR)
        self.fat = [0] * (clusters + 2)
        self.fat[0], self.fat[1], self.fat[2] = 0x0FFFFFF8, 0x0FFFFFFF, 0x0FFFFFFF  # root cluster 2
        self.next_free = 3
        self.label = label.upper().ljust(11)[:11]
        self.dirs = {}

    def alloc(self, nbytes):
        n = max(1, (nbytes + self.spc * SECTOR - 1) // (self.spc * SECTOR))
        first = self.next_free
        for i in range(n):
            self.fat[first + i] = first + i + 1 if i < n - 1 else 0x0FFFFFFF
        self.next_free += n
        assert self.next_free < self.clusters + 2, 'image full'
        return first, n

    def cluster_off(self, c):
        return (self.reserved + 2 * self.fat_sectors + (c - 2) * self.spc) * SECTOR

    def write_chain(self, first, payload):
        csz = self.spc * SECTOR
        c = first
        for off in range(0, max(len(payload), 1), csz):
            o = self.cluster_off(c)
            chunk = payload[off:off + csz]
            self.data[o:o + len(chunk)] = chunk
            c = self.fat[c]

    @staticmethod
    def dos_time(t):
        tm = time.localtime(t)
        return (tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec // 2), ((max(tm.tm_year, 1980) - 1980) << 9) | (tm.tm_mon << 5) | tm.tm_mday

    def dir_entry(self, alias, attr, cluster, size, mtime):
        tm, dt = self.dos_time(mtime)
        return struct.pack('<11sBBBHHHHHHHI', alias.encode('ascii'), attr, 0, 0, tm, dt, dt, cluster >> 16, tm, dt, cluster & 0xFFFF, size)

    def build_dir(self, path, parent_cluster, self_cluster):
        entries, used = [], set()
        if parent_cluster is not None:
            entries.append(self.dir_entry('.          ', 0x10, self_cluster, 0, time.time()))
            entries.append(self.dir_entry('..         ', 0x10, 0 if parent_cluster == 2 else parent_cluster, 0, time.time()))
        for name in sorted(os.listdir(path), key=str.lower):
            full = os.path.join(path, name)
            alias = short_alias(name, used)
            st = os.stat(full)
            if os.path.isdir(full):
                first, n = self.alloc(self.spc * SECTOR * 4)
                entries += lfn_entries(name, alias) + [self.dir_entry(alias, 0x10, first, 0, st.st_mtime)]
                self.build_dir(full, self_cluster, first)
                continue
            payload = open(full, 'rb').read()
            first, n = self.alloc(len(payload)) if payload else (0, 0)
            if payload:
                self.write_chain(first, payload)
            entries += lfn_entries(name, alias) + [self.dir_entry(alias, 0x20, first, len(payload), st.st_mtime)]
        raw = b''.join(entries)
        csz = self.spc * SECTOR
        if self_cluster == 2:
            assert len(raw) <= csz, 'too many entries in the root directory'
        else:
            assert len(raw) <= 4 * csz, 'directory too large'
        self.write_chain(self_cluster, raw)

    def finish(self, root):
        self.build_dir(root, None, 2)
        boot = bytearray(SECTOR)
        boot[0:3] = b'\xEB\x58\x90'
        boot[3:11] = b'MSDOS5.0'
        struct.pack_into('<HBHBHHBHHHII', boot, 11, SECTOR, self.spc, self.reserved, 2, 0, 0, 0xF8, 0, 63, 255, 2048, self.total)
        struct.pack_into('<IHHIHH', boot, 36, self.fat_sectors, 0, 0, 2, 1, 6)
        boot[64] = 0x80; boot[66] = 0x29
        struct.pack_into('<I', boot, 67, 0x4D414E54)
        boot[71:82] = self.label.encode('ascii')
        boot[82:90] = b'FAT32   '
        boot[510:512] = b'\x55\xAA'
        fsinfo = bytearray(SECTOR)
        struct.pack_into('<I', fsinfo, 0, 0x41615252)
        struct.pack_into('<III', fsinfo, 484, 0x61417272, self.clusters - (self.next_free - 2), self.next_free)
        fsinfo[508:512] = b'\x00\x00\x55\xAA'
        for base in (0, 6 * SECTOR):
            self.data[base:base + SECTOR] = boot
            self.data[base + SECTOR:base + 2 * SECTOR] = fsinfo
        fat = b''.join(struct.pack('<I', v) for v in self.fat)
        fat = fat.ljust(self.fat_sectors * SECTOR, b'\0')
        for i in range(2):
            o = (self.reserved + i * self.fat_sectors) * SECTOR
            self.data[o:o + len(fat)] = fat
        return bytes(self.data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out'); ap.add_argument('root')
    ap.add_argument('--size-mib', type=int, default=128)
    ap.add_argument('--label', default='MANTA')
    a = ap.parse_args()
    total_sectors = a.size_mib * 1024 * 1024 // SECTOR - 2048
    fs = Fat32(total_sectors, spc=2, label=a.label)
    part = fs.finish(a.root)
    mbr = bytearray(2048 * SECTOR)
    struct.pack_into('<I', mbr, 440, 0x4D414E54)
    ent = struct.pack('<B3sB3sII', 0x00, b'\x00\x21\x00', 0x0C, b'\xFE\xFF\xFF', 2048, total_sectors)
    mbr[446:446 + 16] = ent
    mbr[510:512] = b'\x55\xAA'
    with open(a.out, 'wb') as f:
        f.write(mbr)
        f.write(part)
    print('wrote', a.out, len(mbr) + len(part), 'bytes;', fs.clusters, 'clusters;', fs.next_free - 2, 'used')


if __name__ == '__main__':
    main()
