#!/usr/bin/env python3
"""Write a gzip-compressed newc cpio archive (Linux initramfs).

    python native/iso/mkinitrd.py OUT.gz SRC@DEST[@MODE] ...      (MODE octal, default 644; /init should be 755)
Directories on the DEST paths are created automatically.
"""
import gzip, os, struct, sys, time


def main():
    out = sys.argv[1]
    specs = sys.argv[2:]
    entries = []   # (path, mode, data)
    dirs = set()
    for spec in specs:
        parts = spec.split("@")
        src, dest = parts[0], parts[1].lstrip("/")
        mode = int(parts[2], 8) if len(parts) > 2 else 0o644
        d = os.path.dirname(dest)
        while d and d not in dirs:
            dirs.add(d)
            d = os.path.dirname(d)
        entries.append((dest, 0o100000 | mode, open(src, "rb").read()))
    for d in ("dev", "proc", "sys", "tmp"):
        dirs.add(d)
    allent = [(d, 0o040755, b"") for d in sorted(dirs)] + entries
    buf = bytearray()
    ino = 1
    now = int(time.time())

    def pad(n): return (4 - n % 4) % 4

    for name, mode, data in allent + [("TRAILER!!!", 0, b"")]:
        nm = name.encode() + b"\0"
        hdr = b"070701" + b"".join(b"%08X" % v for v in (ino, mode, 0, 0, 1, now, len(data), 0, 0, 0, 0, len(nm), 0))
        buf += hdr + nm + b"\0" * pad(len(hdr) + len(nm))
        buf += data + b"\0" * pad(len(data))
        ino += 1
    with gzip.GzipFile(out, "wb", compresslevel=9, mtime=0) as g:
        g.write(bytes(buf))
    print("wrote %s: %d entries, %.1f MB uncompressed" % (out, len(allent), len(buf) / 1e6))


if __name__ == "__main__":
    main()
