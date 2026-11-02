"""Disassemble MIPS32 little-endian: python mdis.py FILE BASEADDR START COUNT"""
import sys, capstone
f, base, start, n = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0), int(sys.argv[4], 0)
d = open(f, 'rb').read()
md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 + capstone.CS_MODE_LITTLE_ENDIAN)
off = start - base
for i in md.disasm(d[off:off + n * 4], start):
    print('%08x  %s  %-8s %s' % (i.address, d[i.address-base:i.address-base+4].hex(), i.mnemonic, i.op_str))
