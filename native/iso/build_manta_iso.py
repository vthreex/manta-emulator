#!/usr/bin/env python3
"""Assemble the Manta boot ISO: Tiny Core kernel + initramfs (/init, the native firmware runner, firmware dump, boot snapshot, policy files).

    python native/iso/build_manta_iso.py OUT.iso --launcher work/launch-WINDOW-native.cmd [--snapshot FILE] [--binary FILE]

The runner's argument list is taken from an existing Windows launcher .cmd (so the ISO runs the same configuration); paths are rewritten to the
initramfs layout, which mirrors the repository-relative paths the options use.
"""
import argparse, os, re, shlex, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, "..", ".."))
work = os.path.abspath(os.path.join(repo, "..", "..", "work"))
ap = argparse.ArgumentParser()
ap.add_argument("out"); ap.add_argument("--launcher", required=True); ap.add_argument("--snapshot"); ap.add_argument("--binary", default=os.path.join(work, "linux", "manta_native_linux"))
ap.add_argument("--vga", default="0x318")
ap.add_argument("--cpi-after-load", type=int, default=2, help="EXPLORATORY time dilation after the snapshot loads (2/4/8/16 cycles per instruction)")
ap.add_argument("--keep-colorkey", action="store_true", help="keep --exploratory-gop-black-colorkey (opaque black OSD pixels become transparent: the video plane shows through menus)")
a = ap.parse_args()
text = open(a.launcher).read()
cmd = [l for l in text.splitlines() if "--firmware" in l][0]
toks = shlex.split(cmd.replace("\\", "/"), posix=True)
args = toks[1:]
files = []      # (src, dest, mode)
out_args = []
i = 0
root = os.path.abspath(os.path.join(repo, "..", ".."))
def local(p):
    p = p.replace("\\", "/")
    for cand in (p, os.path.join(root, p), os.path.join(repo, p)):
        if os.path.exists(cand): return os.path.abspath(cand)
    return None
while i < len(args):
    t = args[i]
    if t == "--exploratory-gop-black-colorkey" and not a.keep_colorkey: out_args.append("--exploratory-gop-black-colorkey-edge"); i += 1; continue
    if t in ("--summary", "--preview-capture", "--gop-frame-history", "--export-gop-frame"): i += 2; continue
    if t == "--load-state":
        snap = a.snapshot or args[i + 1]
        files.append((os.path.abspath(snap), "/data/state.snap", 0o644)); out_args += [t, "/data/state.snap"]; i += 2; continue
    out_args.append(t)
    if t in ("--firmware",) or t == "--opaque-read-policy":
        src = local(args[i + 1]); dest = "/" + os.path.relpath(src, root).replace("\\", "/") if src and src.startswith(root) else None
        if not src: sys.exit("missing file " + args[i + 1])
        rel = args[i + 1].replace("\\", "/")
        files.append((src, "/" + rel.lstrip("/") if not os.path.isabs(rel) and ":" not in rel else dest, 0o644))
        out_args.append(rel if not os.path.isabs(rel) and ":" not in rel else dest); i += 2; continue
    i += 1
if a.cpi_after_load: out_args += ["--exploratory-cpi-after-load", str(a.cpi_after_load)]
ir = os.path.join(repo, "configs", "input", "ir-keymap.tsv")
files.append((ir, "/outputs/manta-emulator/configs/input/ir-keymap.tsv", 0o644))
files.append((ir, "/configs/input/ir-keymap.tsv", 0o644))
argfile = os.path.join(work, "iso", "manta.args"); open(argfile, "w", newline="\n").write("\n".join(out_args) + "\n")
init = os.path.join(work, "iso", "init")
zig = os.path.join(work, "dl", "zig", "zig-windows-x86_64-0.13.0", "zig.exe")
subprocess.check_call([zig, "cc", "-target", "x86_64-linux-musl", "-O2", "-static", os.path.join(here, "init.c"), "-o", init])
spec = ["%s@/init@755" % init, "%s@/manta@755" % a.binary, "%s@/manta.args" % argfile] + ["%s@%s@%o" % (s, d, m) for s, d, m in files]
initrd = os.path.join(work, "iso", "initrd-manta.gz")
subprocess.check_call([sys.executable, os.path.join(here, "mkinitrd.py"), initrd] + spec)
subprocess.check_call([sys.executable, os.path.join(here, "mkiso.py"), a.out, "--kernel", os.path.join(work, "dl", "kernel", "vmlinuz64"), "--initrd", initrd,
                       "--isolinux-dir", os.path.join(work, "iso", "stage"), "--append", "vga=%s console=tty0 console=ttyS0 quiet loglevel=3 nokaslr" % a.vga])
print("args:", " ".join(out_args)[:300], "...")
