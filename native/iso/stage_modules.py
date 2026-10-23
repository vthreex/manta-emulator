#!/usr/bin/env python3
"""Stage kernel modules for the ISO's initramfs from Tiny Core's corepure64.gz (the modules that match the kernel on the ISO).

    python native/iso/stage_modules.py corepure64.gz OUTDIR [name ...]

Writes OUTDIR/<name>.ko (decompressed) for every requested module and all of its dependencies, and OUTDIR/modules.list (load order, one name per line).
"""
import gzip, os, sys

DEFAULT = ["virtio_net", "e1000", "e1000e", "igb", "r8169", "8139too", "8139cp", "tg3", "via-rhine", "pcnet32", "vmxnet3", "forcedeth", "sky2", "atl1c", "alx",
           "r8152", "ax88179_178a", "asix", "cdc_ether", "rndis_host", "smsc95xx", "cdc_ncm"]


def read_cpio(path):
    d = gzip.open(path, "rb").read()
    files, pos = {}, 0
    while pos + 110 <= len(d) and d[pos:pos + 6] == b"070701":
        f = [int(d[pos + 6 + 8 * i:pos + 14 + 8 * i], 16) for i in range(13)]
        size, nlen = f[6], f[11]
        name = d[pos + 110:pos + 110 + nlen - 1].decode()
        pos = (pos + 110 + nlen + 3) & ~3
        files[name] = d[pos:pos + size]
        pos = (pos + size + 3) & ~3
        if name == "TRAILER!!!": break
    return files


def main():
    src, out = sys.argv[1], sys.argv[2]
    want = sys.argv[3:] or DEFAULT
    files = read_cpio(src)
    base = next(n for n in files if n.endswith("/modules.dep"))
    root = base[:-len("modules.dep")]
    dep = {}
    for line in files[base].decode().splitlines():
        if ":" not in line: continue
        mod, rest = line.split(":", 1)
        dep[mod.strip()] = rest.split()
    byname = {os.path.basename(m).split(".ko")[0].replace("_", "-"): m for m in dep}
    order, seen = [], set()

    def visit(path):
        if path in seen: return
        seen.add(path)
        for d in dep.get(path, []): visit(d)
        order.append(path)
    for w in want:
        key = w.replace("_", "-")
        if key not in byname:
            print("skip (not in this kernel's module set):", w); continue
        visit(byname[key])
    os.makedirs(out, exist_ok=True)
    names = []
    for path in order:
        blob = files.get(root + path)
        if blob is None: print("missing", path); continue
        if path.endswith(".gz"): blob = gzip.decompress(blob)
        name = os.path.basename(path).split(".ko")[0]
        open(os.path.join(out, name + ".ko"), "wb").write(blob)
        names.append(name)
    open(os.path.join(out, "modules.list"), "w", newline="\n").write("\n".join(names) + "\n")
    print("staged %d modules (%.1f MB) in %s" % (len(names), sum(os.path.getsize(os.path.join(out, n + ".ko")) for n in names) / 1e6, out))


if __name__ == "__main__":
    main()
