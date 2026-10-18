"""Scan the original MIPS image for accesses/constants touching RIU offset range [lo,hi] (offset from 0x1F200000).
Tracks register constants linearly (reset at calls' clobbers only for v0/v1/a*/t*), flags: folded constants in range, and
loads/stores whose base+imm folds into range. Usage: xref_riu.py LO HI"""
import sys, struct
sys.path.insert(0, '.')
from pathlib import Path
from firmware_extract import validate, inflate_at, KNOWN
from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
from capstone.mips import *
BASE = 0x803b0180
flash = validate(Path('../../../work/firmware-original.bin').read_bytes()); _, rt = inflate_at(flash, KNOWN[0][0])
image = flash[0x20080:0x23880] + rt
cs = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN); cs.detail = True
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
const = {}
CALLER = {'v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7','t8','t9'}
def inr(v): return lo <= (v & 0xFFFFFFFF) <= hi or lo <= ((v & 0xFFFFFFFF) - 0x1F200000) <= hi
hits = []
def gen():
    for off in range(0,len(image)-3,4):
        for i in cs.disasm(image[off:off+4], BASE+off): yield i
for i in gen():
    m = i.mnemonic; ops = i.op_str.replace(' ', '').split(',')
    def val(r): return const.get(r)
    d = None
    try:
        if m == 'lui': d = (ops[0], (int(ops[1], 0) << 16) & 0xFFFFFFFF)
        elif m in ('ori', 'addiu', 'addi') and ops[1] != '$zero':
            b = val(ops[1]); imm = int(ops[2], 0)
            if b is not None: d = (ops[0], (b | imm) if m == 'ori' else (b + imm))
        elif m in ('ori', 'addiu') : d = (ops[0], int(ops[2], 0))
        elif m in ('addu', 'or', 'add'):
            a, b = val(ops[1]), val(ops[2])
            if a is not None and b is not None: d = (ops[0], a + b)
        elif m in ('lbu','lb','lhu','lh','lw','sb','sh','sw'):
            off = ops[1]; imm = int(off[:off.index('(')] or '0', 0); r = off[off.index('(')+1:-1]
            b = val(r)
            if b is not None and inr(b + imm): hits.append((i.address, m, i.op_str, 'mem'))
            if m == 'lw' and imm == -0x2684: const[ops[0]] = 0x1F200000
            elif m[0] == 'l': const.pop(ops[0], None)
        elif m == 'sll' and val(ops[1]) is not None: d = (ops[0], val(ops[1]) << int(ops[2], 0))
        if d:
            d = (d[0], d[1] & 0xFFFFFFFF)
            if inr(d[1]) and d[1] > 0x1000: hits.append((i.address, m, i.op_str, 'const 0x%x' % d[1]))
            const[d[0]] = d[1]
        elif m not in ('lbu','lb','lhu','lh','lw','sb','sh','sw','nop') and ops and ops[0].startswith('$') and m not in ('beq','bne','beqz','bnez','j','jr','b','bgez','bltz','blez','bgtz'):
            const.pop(ops[0], None)
    except Exception:
        pass
    if m in ('jal', 'jalr'):
        for r in CALLER: const.pop('$' + r, None)
for h in hits: print('%08x %-5s %-28s %s' % h)
