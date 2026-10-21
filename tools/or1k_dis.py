#!/usr/bin/env python3
"""ORBIS32 disassembler / cross-reference helper for the multimedia coprocessor image (resource 61, little-endian words, base address 0).

    python tools/or1k_dis.py IMAGE.bin dis START END          # disassemble [START,END)
    python tools/or1k_dis.py IMAGE.bin xref TARGET            # l.j/l.jal/l.bf/l.bnf/movhi+ori references to TARGET
    python tools/or1k_dis.py IMAGE.bin str PATTERN            # strings matching a regex (with addresses)
"""
import re, struct, sys

NAMES_SF = {0: 'eq', 1: 'ne', 2: 'gtu', 3: 'geu', 4: 'ltu', 5: 'leu', 10: 'gts', 11: 'ges', 12: 'lts', 13: 'les'}
ALU = {0x0: 'add', 0x1: 'addc', 0x2: 'sub', 0x3: 'and', 0x4: 'or', 0x5: 'xor', 0x6: 'mul', 0x9: 'div', 0xA: 'divu', 0xB: 'mulu', 0xE: 'cmov'}


def sx(v, b):
    return v - (1 << b) if v & (1 << (b - 1)) else v


def dis(w, a):
    op = w >> 26; D = (w >> 21) & 31; A = (w >> 16) & 31; B = (w >> 11) & 31; I = w & 0xffff; sI = sx(I, 16)
    if op == 0: return 'l.j %x' % ((a + 4 * sx(w & 0x3ffffff, 26)) & 0xffffffff)
    if op == 1: return 'l.jal %x' % ((a + 4 * sx(w & 0x3ffffff, 26)) & 0xffffffff)
    if op == 3: return 'l.bnf %x' % ((a + 4 * sx(w & 0x3ffffff, 26)) & 0xffffffff)
    if op == 4: return 'l.bf %x' % ((a + 4 * sx(w & 0x3ffffff, 26)) & 0xffffffff)
    if op == 5: return 'l.nop %x' % I
    if op == 6: return 'l.macrc r%d' % D if w & (1 << 16) else 'l.movhi r%d,0x%x' % (D, I)
    if op == 8: return {0x2000: 'l.sys', 0x2100: 'l.trap', 0x2200: 'l.msync', 0x2280: 'l.psync', 0x2300: 'l.csync'}.get(w >> 16, '?sys%x' % (w >> 16))
    if op == 9: return 'l.rfe'
    if op == 0x11: return 'l.jr r%d' % B
    if op == 0x12: return 'l.jalr r%d' % B
    m = {0x21: 'lwz', 0x22: 'lws', 0x23: 'lbz', 0x24: 'lbs', 0x25: 'lhz', 0x26: 'lhs'}
    if op in m: return 'l.%s r%d,%d(r%d)' % (m[op], D, sI, A)
    if op == 0x27: return 'l.addi r%d,r%d,%d' % (D, A, sI)
    if op == 0x28: return 'l.addic r%d,r%d,%d' % (D, A, sI)
    if op == 0x29: return 'l.andi r%d,r%d,0x%x' % (D, A, I)
    if op == 0x2a: return 'l.ori r%d,r%d,0x%x' % (D, A, I)
    if op == 0x2b: return 'l.xori r%d,r%d,%d' % (D, A, sI)
    if op == 0x2c: return 'l.muli r%d,r%d,%d' % (D, A, sI)
    if op == 0x2d: return 'l.mfspr r%d,r%d,0x%x' % (D, A, I)
    if op == 0x30: return 'l.mtspr r%d,r%d,0x%x' % (A, B, (D << 11) | (w & 0x7ff))
    if op in (0x35, 0x36, 0x37):
        return 'l.%s %d(r%d),r%d' % ({0x35: 'sw', 0x36: 'sb', 0x37: 'sh'}[op], sx((D << 11) | (w & 0x7ff), 16), A, B)
    if op == 0x2e: return 'l.%si r%d,r%d,%d' % (['sll', 'srl', 'sra', 'ror'][(w >> 6) & 3], D, A, w & 63)
    if op == 0x2f: return 'l.sf%si r%d,%d' % (NAMES_SF.get(D, '?'), A, sI)
    if op == 0x39: return 'l.sf%s r%d,r%d' % (NAMES_SF.get(D, '?'), A, B)
    if op == 0x38:
        fn = w & 0xf; sub = (w >> 6) & 3
        if fn in ALU: return 'l.%s r%d,r%d,r%d' % (ALU[fn], D, A, B)
        if fn == 8: return 'l.%s r%d,r%d,r%d' % (['sll', 'srl', 'sra', 'ror'][sub], D, A, B)
        if fn == 0xC: return 'l.%s r%d,r%d' % (['exths', 'extbs', 'exthz', 'extbz'][sub], D, A)
        if fn == 0xD: return 'l.extw r%d,r%d' % (D, A)
        if fn == 0xF: return 'l.%s r%d,r%d' % ('ff1' if ((w >> 8) & 3) == 0 else 'fl1', D, A)
    if op == 0x31: return 'l.%s r%d,r%d' % ('mac' if (w & 0xf) == 1 else 'msb', A, B)
    return '.word 0x%08x' % w


def main():
    d = open(sys.argv[1], 'rb').read()
    cmd = sys.argv[2]
    w = lambda a: struct.unpack_from('<I', d, a)[0]
    if cmd == 'dis':
        s, e = int(sys.argv[3], 0), int(sys.argv[4], 0)
        for a in range(s & ~3, e, 4):
            print('%06x: %08x  %s' % (a, w(a), dis(w(a), a)))
    elif cmd == 'xref':
        t = int(sys.argv[3], 0)
        for a in range(0, len(d) - 3, 4):
            x = w(a); op = x >> 26
            if op in (0, 1, 3, 4) and ((a + 4 * sx(x & 0x3ffffff, 26)) & 0xffffffff) == t:
                print('%06x: %s' % (a, dis(x, a)))
        hi, lo = (t >> 16) & 0xffff, t & 0xffff
        for a in range(0, len(d) - 8, 4):
            x = w(a)
            if x >> 26 == 6 and (x & 0xffff) == hi:
                for j in range(1, 4):
                    y = w(a + 4 * j)
                    if y >> 26 == 0x2a and (y & 0xffff) == lo and ((y >> 16) & 31) == ((x >> 21) & 31):
                        print('%06x: movhi/ori pair -> %x' % (a, t))
    elif cmd == 'str':
        pat = re.compile(sys.argv[3])
        for m in re.finditer(rb'[ -~]{5,}', d):
            s = m.group().decode()
            if pat.search(s): print('%06x %s' % (m.start(), s[:120]))


if __name__ == '__main__':
    main()
