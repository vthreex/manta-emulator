#!/usr/bin/env python3
"""Bare-metal BIOS CD image: El Torito no-emulation boot code (native/boot/boot.S) + a payload, no kernel and no bootloader.

    python native/iso/mkbare.py OUT.iso --boot boot.bin --payload payload.bin [--volume MANTA]

The boot code (<= 2048 bytes, loaded at 0x7C00) gets the payload's LBA and byte size patched at offsets 8 and 12 and copies the payload to 1 MB.
"""
import argparse, os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkiso import SECTOR, both32, both16, dirrec, dstr, dtime


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out"); ap.add_argument("--boot", required=True); ap.add_argument("--payload", required=True); ap.add_argument("--volume", default="MANTA")
    a = ap.parse_args()
    now = time.gmtime()
    when = (now.tm_year, now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec)
    boot = bytearray(open(a.boot, "rb").read())
    payload = open(a.payload, "rb").read()
    assert len(boot) <= SECTOR, "boot code must fit one 2048-byte sector (it is loaded as 4 x 512)"
    cat_lba, ptl_lba, ptm_lba, root_lba, boot_lba = 19, 20, 21, 22, 23
    pay_lba = boot_lba + 1
    pay_sectors = ((len(payload) + SECTOR - 1) // SECTOR + 15) // 16 * 16   # the boot code reads in 16-sector chunks
    total = pay_lba + pay_sectors
    struct.pack_into("<II", boot, 8, pay_lba, len(payload))
    boot = bytes(boot).ljust(SECTOR, b"\0")
    root = (dirrec(root_lba, SECTOR, b"\0", True, when) + dirrec(root_lba, SECTOR, b"\1", True, when) +
            dirrec(boot_lba, SECTOR, "BOOT.BIN;1", False, when) + dirrec(pay_lba, len(payload), "PAYLOAD.BIN;1", False, when)).ljust(SECTOR, b"\0")
    pt_l = bytes([1, 0]) + struct.pack("<I", root_lba) + struct.pack("<H", 1) + b"\0\0"
    pt_m = bytes([1, 0]) + struct.pack(">I", root_lba) + struct.pack(">H", 1) + b"\0\0"
    pvd = bytearray(SECTOR); pvd[0] = 1; pvd[1:6] = b"CD001"; pvd[6] = 1
    pvd[8:40] = dstr("MANTA", 32); pvd[40:72] = dstr(a.volume, 32); pvd[80:88] = both32(total)
    pvd[120:124] = both16(1); pvd[124:128] = both16(1); pvd[128:132] = both16(SECTOR); pvd[132:140] = both32(len(pt_l))
    struct.pack_into("<I", pvd, 140, ptl_lba); struct.pack_into(">I", pvd, 148, ptm_lba)
    pvd[156:190] = dirrec(root_lba, SECTOR, b"\0", True, when)
    for off in (190, 318, 446, 574): pvd[off:off + 128] = b" " * 128
    for off in (702, 739, 776, 813 - 37 + 37): pass
    pvd[702:813] = b" " * 111
    pvd[813:830] = dtime(now)[:17]; pvd[830:847] = dtime(now)[:17]; pvd[847:864] = b"0" * 16 + b"\0"; pvd[864:881] = b"0" * 16 + b"\0"; pvd[881] = 1
    brvd = bytearray(SECTOR); brvd[1:6] = b"CD001"; brvd[6] = 1; brvd[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\0")
    struct.pack_into("<I", brvd, 71, cat_lba)
    term = bytearray(SECTOR); term[0] = 255; term[1:6] = b"CD001"; term[6] = 1
    cat = bytearray(SECTOR); cat[0] = 1; cat[4:28] = b"MANTA".ljust(24, b"\0"); cat[30] = 0x55; cat[31] = 0xAA
    struct.pack_into("<H", cat, 28, (0x10000 - (sum(struct.unpack_from("<16H", cat, 0)) & 0xFFFF)) & 0xFFFF)
    cat[32] = 0x88; struct.pack_into("<H", cat, 38, 4); struct.pack_into("<I", cat, 40, boot_lba)   # no emulation, segment 0 (=0x7C0), 4 sectors
    with open(a.out, "wb") as f:
        f.write(b"\0" * (16 * SECTOR)); f.write(pvd); f.write(brvd); f.write(term); f.write(cat)
        f.write(pt_l.ljust(SECTOR, b"\0")); f.write(pt_m.ljust(SECTOR, b"\0")); f.write(root); f.write(boot)
        f.write(payload.ljust(pay_sectors * SECTOR, b"\0"))
    print("wrote %s: %d sectors (%.2f MB), payload %d bytes at LBA %d" % (a.out, total, total * SECTOR / 1e6, len(payload), pay_lba))


if __name__ == "__main__":
    main()
