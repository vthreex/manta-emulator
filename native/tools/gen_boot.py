#!/usr/bin/env python3
"""Recompile the cold-boot code of the user's own LCD4214 firmware into native/gen, next to the runtime image (mips_recomp.py).

    python native/tools/gen_boot.py FIRMWARE.bin [OUTDIR=native/gen]

A cold boot runs, before the runtime image at 0x803B0180 (already in native/gen):
  * the boot ROM, flash 0x0000-0x2C60, executed in place at 0xBFC00000 (KSEG1) and partly through its KSEG0 alias 0x9FC00000;
  * the second stage, flash 0x2C60-0x85C0, which the boot ROM copies to 0x81000000 and which inflates the runtime image.
Each becomes its own region (BOOTK1 / BOOTK0 / STAGE2: <prefix>_runs_00.cpp + <prefix>_tables.cpp) that registers itself with the runtime
dispatcher. Both are entered through computed jumps, so every aligned word of a region is a root (--all-entries); data words become traps.
Like native/gen, the output is derived from the user's image and is not part of the repository.
"""
import os, re, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, '..', '..'))
fw = sys.argv[1]
out = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(repo, 'native', 'gen')
flash = open(fw, 'rb').read()[:0x800000]
recomp = os.path.join(here, 'mips_recomp.py')
with tempfile.TemporaryDirectory() as tmp:
    boot, stage2 = os.path.join(tmp, 'boot.bin'), os.path.join(tmp, 'stage2.bin')
    open(boot, 'wb').write(flash[:0x2c60])
    open(stage2, 'wb').write(flash[0x2c60:0x85c0])

    def run(image, base, size, prefix, entries=()):
        args = [sys.executable, recomp, image, out, '--base', hex(base), '--code-end', hex(base + size), '--prefix', prefix, '--files', '1', '--all-entries']
        for e in entries:
            args += ['--entry', hex(e)]
        subprocess.run(args, check=True)

    run(boot, 0xbfc00000, 0x2c60, 'BOOTK1')
    run(boot, 0x9fc00000, 0x2c60, 'BOOTK0')
    run(stage2, 0x81000000, 0x5960, 'STAGE2')
print('boot regions written to', out)
