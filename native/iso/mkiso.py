#!/usr/bin/env python3
"""Build a BIOS-bootable ISO (isolinux + Linux kernel + initramfs) with no external tools.

    python native/iso/mkiso.py OUT.iso --kernel vmlinuz64 --initrd initrd.gz --isolinux-dir DIR --append "vga=0x118 ..." [--volume MANTA]

DIR must contain isolinux.bin and ldlinux.c32 (syslinux 6.03, bios/). Writes ISO9660 level 1 (8.3 upper-case names) with an El Torito no-emulation
boot entry and the isolinux boot-info-table patched into isolinux.bin; `qemu-system-x86_64 -cdrom OUT.iso` boots it with the default SeaBIOS.
"""
import argparse, struct, sys, time, os

SECTOR = 2048


def both32(v): return struct.pack("<I", v) + struct.pack(">I", v)
def both16(v): return struct.pack("<H", v) + struct.pack(">H", v)


def dirrec(extent, length, name, is_dir, when):
    nm = name if isinstance(name, bytes) else name.encode()
    rec = bytearray()
    rec += b"\0"                       # length placeholder
    rec += b"\0"                       # extended attribute length
    rec += both32(extent) + both32(length)
    rec += bytes([when[0] - 1900, when[1], when[2], when[3], when[4], when[5], 0])
    rec += bytes([2 if is_dir else 0])
    rec += b"\0\0"                     # file unit size, interleave gap
    rec += both16(1)                   # volume sequence number
    rec += bytes([len(nm)]) + nm
    if len(rec) % 2: rec += b"\0"
    rec[0] = len(rec)
    return bytes(rec)


def dstr(s, n): return s.encode().ljust(n, b" ")[:n]


def dtime(t):
    return time.strftime("%Y%m%d%H%M%S", t).encode() + b"00" + b"\0"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--initrd", required=True)
    ap.add_argument("--isolinux-dir", required=True)
    ap.add_argument("--append", default="")
    ap.add_argument("--volume", default="MANTA")
    a = ap.parse_args()
    now = time.gmtime()
    when = (now.tm_year, now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec)
    isolinux = bytearray(open(os.path.join(a.isolinux_dir, "isolinux.bin"), "rb").read())
    ldlinux = open(os.path.join(a.isolinux_dir, "ldlinux.c32"), "rb").read()
    kernel = open(a.kernel, "rb").read()
    initrd = open(a.initrd, "rb").read()
    cfg = ("DEFAULT manta\nPROMPT 0\nTIMEOUT 1\nLABEL manta\n  KERNEL /boot/kernel\n  APPEND initrd=/boot/initrd.gz %s\n" % a.append).encode()

    # layout: dirs
    # entries: (path, data) - files placed in /BOOT and /BOOT/ISOLINUX
    files = {  # name in ISO -> (dir, bytes)
        "ISOLINUX.BIN;1": ("ISOLINUX", isolinux),
        "LDLINUX.C32;1": ("ISOLINUX", ldlinux),
        "ISOLINUX.CFG;1": ("ISOLINUX", cfg),
        "KERNEL.;1": ("BOOT", kernel),
        "INITRD.GZ;1": ("BOOT", initrd),
    }
    lba = 19
    boot_catalog_lba = lba; lba += 1
    ptl_lba = lba; lba += 1
    ptm_lba = lba; lba += 1
    root_lba = lba; lba += 1
    boot_lba = lba; lba += 1
    iso_dir_lba = lba; lba += 1
    placement = {}
    for name, (d, data) in files.items():
        placement[name] = (lba, len(data))
        lba += (len(data) + SECTOR - 1) // SECTOR
    total = lba
    # patch isolinux boot info table
    il_lba, il_len = placement["ISOLINUX.BIN;1"]
    csum = 0
    for off in range(64, len(isolinux) - len(isolinux) % 4, 4):
        csum = (csum + struct.unpack_from("<I", isolinux, off)[0]) & 0xFFFFFFFF
    struct.pack_into("<IIII", isolinux, 8, 16, il_lba, len(isolinux), csum)
    isolinux[24:64] = b"\0" * 40
    files["ISOLINUX.BIN;1"] = ("ISOLINUX", bytes(isolinux))

    def dirblock(self_lba, parent_lba, entries):
        blk = dirrec(self_lba, SECTOR, b"\0", True, when) + dirrec(parent_lba, SECTOR, b"\1", True, when)
        for e in entries: blk += e
        assert len(blk) <= SECTOR
        return blk.ljust(SECTOR, b"\0")

    root = dirblock(root_lba, root_lba, [dirrec(boot_lba, SECTOR, "BOOT", True, when)])
    bootdir = dirblock(boot_lba, root_lba, [dirrec(iso_dir_lba, SECTOR, "ISOLINUX", True, when)] +
                       [dirrec(placement[n][0], placement[n][1], n, False, when) for n, (d, _) in files.items() if d == "BOOT"])
    isodir = dirblock(iso_dir_lba, boot_lba, [dirrec(placement[n][0], placement[n][1], n, False, when) for n, (d, _) in files.items() if d == "ISOLINUX"])

    # path table (directories in breadth-first order: root, BOOT, ISOLINUX)
    def ptable(le):
        out = b""
        for ext, par, nm in ((root_lba, 1, b"\0"), (boot_lba, 1, b"BOOT"), (iso_dir_lba, 2, b"ISOLINUX")):
            ent = bytes([len(nm), 0]) + (struct.pack("<I", ext) if le else struct.pack(">I", ext)) + (struct.pack("<H", par) if le else struct.pack(">H", par)) + nm
            if len(nm) % 2: ent += b"\0"
            out += ent
        return out
    pt_l, pt_m = ptable(True), ptable(False)

    # primary volume descriptor
    pvd = bytearray(SECTOR)
    pvd[0] = 1; pvd[1:6] = b"CD001"; pvd[6] = 1
    pvd[8:40] = dstr("MANTA", 32); pvd[40:72] = dstr(a.volume, 32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1); pvd[124:128] = both16(1); pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(len(pt_l))
    struct.pack_into("<I", pvd, 140, ptl_lba); struct.pack_into(">I", pvd, 148, ptm_lba)
    pvd[156:190] = dirrec(root_lba, SECTOR, b"\0", True, when)
    pvd[190:318] = dstr("", 128)
    pvd[318:446] = dstr("", 128); pvd[446:574] = dstr("", 128); pvd[574:702] = dstr("", 128)
    for off in (702, 739, 776, 813):
        pvd[off:off + 37] = b" " * 37
    pvd[813:830] = dtime(now)[:17]; pvd[830:847] = dtime(now)[:17]; pvd[847:864] = b"0" * 16 + b"\0"; pvd[864:881] = b"0" * 16 + b"\0"
    pvd[881] = 1
    # boot record volume descriptor
    brvd = bytearray(SECTOR)
    brvd[0] = 0; brvd[1:6] = b"CD001"; brvd[6] = 1
    brvd[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\0")
    struct.pack_into("<I", brvd, 71, boot_catalog_lba)
    term = bytearray(SECTOR); term[0] = 255; term[1:6] = b"CD001"; term[6] = 1
    # boot catalog
    cat = bytearray(SECTOR)
    cat[0] = 1; cat[1] = 0; cat[4:28] = b"MANTA".ljust(24, b"\0"); cat[30] = 0x55; cat[31] = 0xAA
    s = sum(struct.unpack_from("<16H", cat, 0)) & 0xFFFF
    struct.pack_into("<H", cat, 28, (0x10000 - s) & 0xFFFF)
    cat[32] = 0x88; cat[33] = 0; struct.pack_into("<H", cat, 34, 0); cat[36] = 0
    struct.pack_into("<H", cat, 38, 4); struct.pack_into("<I", cat, 40, il_lba)

    with open(a.out, "wb") as f:
        f.write(b"\0" * (16 * SECTOR))
        f.write(pvd); f.write(brvd); f.write(term)
        f.write(cat)
        f.write(pt_l.ljust(SECTOR, b"\0")); f.write(pt_m.ljust(SECTOR, b"\0"))
        f.write(root); f.write(bootdir); f.write(isodir)
        for n, (d, data) in files.items():
            data = bytes(isolinux) if n == "ISOLINUX.BIN;1" else data
            assert f.tell() == placement[n][0] * SECTOR, n
            f.write(data.ljust((len(data) + SECTOR - 1) // SECTOR * SECTOR, b"\0"))
    print("wrote %s: %d sectors (%.1f MB)" % (a.out, total, total * SECTOR / 1e6))


if __name__ == "__main__":
    main()
