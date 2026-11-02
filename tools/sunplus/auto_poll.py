"""EXPLORATORY driver for sunplus_run: when the firmware is stuck polling a hardware register, read the polling loop from the blobs and append a labelled
'auto-poll' hypothesis that lets the loop exit, then run again.

Rule (a hypothesis about hardware, NOT an observation): a register polled with  `lw; andi MASK; beqz -> loop`  is waiting for MASK to become 1 (hypothesis
`or MASK`); one polled with  `bnez -> loop`  is waiting for MASK to become 0 (hypothesis `clear MASK`).  Anything that does not match that shape stops the driver.
Every line it appends is tagged AUTO-POLL in the hypothesis file, so the list can be reviewed (and tested with the opposite polarity: bracketing).

Usage: python auto_poll.py HYPFILE ITERATIONS -- <sunplus_run args...>     (the --hyp FILE argument is added by this script)
"""
import os
import re
import struct
import subprocess
import sys
import capstone

BLOBS = [  # (path, base)
    ('work/sunplus-stv/blobs/lzma_054070.bin', 0x80000400),
    ('work/sunplus-stv/blobs/lzma_0f1f5c.bin', 0x809e2400),
]
md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 + capstone.CS_MODE_LITTLE_ENDIAN)


def code_at(addr, n=24):
    for path, base in BLOBS:
        d = open(path, 'rb').read()
        if base <= addr < base + len(d):
            off = addr - base
            return list(md.disasm(d[off:off + n * 4], addr))
    return []


def classify(block, reg_addr):
    """Find lw -> (andi|ext) -> beqz/bnez(back) in the block; return (kind, mask) or None."""
    ins = code_at(block, 20)
    for k, i in enumerate(ins):
        if i.mnemonic == 'lw':
            reg = i.op_str.split(',')[0].strip()
            for j in ins[k + 1:k + 4]:
                m = re.match(r'(\$\w+), (\$\w+), (0x[0-9a-f]+|\d+)$', j.op_str)
                if j.mnemonic == 'andi' and m and m.group(2) == reg:
                    mask = int(m.group(3), 0)
                    tgt = m.group(1)
                elif j.mnemonic == 'ext':
                    parts = [p.strip() for p in j.op_str.split(',')]
                    if parts[1] != reg:
                        continue
                    pos, size = int(parts[2], 0), int(parts[3], 0)
                    mask = ((1 << size) - 1) << pos
                    tgt = parts[0]
                else:
                    continue
                for b in ins:
                    if b.mnemonic in ('beqz', 'bnez') and b.op_str.split(',')[0].strip() == tgt and int(b.op_str.split(',')[1], 0) <= b.address:
                        return ('or' if b.mnemonic == 'beqz' else 'clear'), mask
    return None


def main():
    hyp, iters = sys.argv[1], int(sys.argv[2])
    args = sys.argv[sys.argv.index('--') + 1:]
    args[0] = os.path.abspath(args[0])
    for n in range(iters):
        r = subprocess.run(args + ['--hyp', hyp], capture_output=True, text=True)
        out = r.stdout
        stop = re.search(r'^stop: (.*)$', out, re.M).group(1)
        print('[%d] %s' % (n, stop[:200]))
        m = re.match(r'stuck polling ([0-9a-f]{8}) .* now block ([0-9a-f]{8})', stop)
        if not m:
            print(out[:3000])
            return
        reg, block = int(m.group(1), 16), int(m.group(2), 16)
        c = classify(block, reg)
        if not c:
            print('no recognisable poll shape at block %08x; code:' % block)
            for i in code_at(block, 16):
                print('  %08x %s %s' % (i.address, i.mnemonic, i.op_str))
            return
        kind, mask = c
        line = '0x%08X %s 0x%08X   # AUTO-POLL (exploratory): loop at block %08x waits for mask %s; assumed to exit\n' % (reg, kind, mask, block, 'set' if kind == 'or' else 'clear')
        print('   ->', line.strip())
        open(hyp, 'a', encoding='utf-8').write(line)
    print(out[:2500])


if __name__ == '__main__':
    main()
