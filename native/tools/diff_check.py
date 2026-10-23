#!/usr/bin/env python3
"""Differential test, part 2: replay the native vectors (diff_runs.cpp) in an INDEPENDENT reference interpreter and compare.

The reference interpreter below is written directly from the MIPS32 release 2 instruction descriptions and shares no code with the recompiler
(native/tools/mips_recomp.py). (An attempt to use Unicorn as the reference crashed inside its own JIT setup in every executable that also reads the
vector file; see docs/native-port.md.)

    python native/tools/diff_check.py runtime_image.bin build/vec.bin
"""
import struct
import sys

M32 = 0xFFFFFFFF
RAM = 0x01000000
BASE = 0x803B0180
SCRATCH = 0x00100000
SCRATCH_LEN = 0x40000


def s32(x):
    return x - (1 << 32) if x & 0x80000000 else x


def sx16(x):
    return x - 0x10000 if x & 0x8000 else x


def scratch_byte(i):
    h = (i * 2654435761) & M32
    h ^= h >> 15
    h = (h * 2246822519) & M32
    h ^= h >> 13
    return h & 255


class Mem:
    def __init__(self, image):
        self.m = bytearray(RAM)
        p = BASE & 0x1FFFFFFF
        self.m[p:p + len(image)] = image
        for i in range(SCRATCH_LEN):
            self.m[SCRATCH + i] = scratch_byte(i)
        self.dirty = {}

    def phys(self, a, n):
        p = a & 0x1FFFFFFF
        if p + n > RAM:
            raise IndexError("outside RAM")
        return p

    def rd(self, a, n):
        p = self.phys(a, n)
        return int.from_bytes(self.m[p:p + n], "little")

    def wr(self, a, n, v):
        p = self.phys(a, n)
        for i in range(n):
            if (p + i) not in self.dirty:
                self.dirty[p + i] = self.m[p + i]
        self.m[p:p + n] = (v & ((1 << (8 * n)) - 1)).to_bytes(n, "little")

    def restore(self):
        for k, v in self.dirty.items():
            self.m[k] = v
        self.dirty.clear()


class Cpu:
    def __init__(self, mem):
        self.mem = mem
        self.r = [0] * 32
        self.hi = 0
        self.lo = 0

    def setr(self, i, v):
        if i:
            self.r[i] = v & M32

    # execute one non-control instruction
    def exec_plain(self, w):
        op = w >> 26
        rs, rt, rd, sa, f = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
        imm = w & 0xFFFF
        simm = sx16(imm)
        r = self.r
        if op == 0:
            if f == 0x00: return self.setr(rd, r[rt] << sa)
            if f == 0x02:
                if rs == 1: return self.setr(rd, ((r[rt] >> sa) | (r[rt] << (32 - sa))) if sa else r[rt])
                return self.setr(rd, r[rt] >> sa)
            if f == 0x03: return self.setr(rd, s32(r[rt]) >> sa)
            if f == 0x04: return self.setr(rd, r[rt] << (r[rs] & 31))
            if f == 0x06:
                n = r[rs] & 31
                if sa == 1: return self.setr(rd, ((r[rt] >> n) | (r[rt] << (32 - n))) if n else r[rt])
                return self.setr(rd, r[rt] >> n)
            if f == 0x07: return self.setr(rd, s32(r[rt]) >> (r[rs] & 31))
            if f == 0x0A:
                if r[rt] == 0: self.setr(rd, r[rs])
                return
            if f == 0x0B:
                if r[rt] != 0: self.setr(rd, r[rs])
                return
            if f == 0x0F: return
            if f == 0x10: return self.setr(rd, self.hi)
            if f == 0x11: self.hi = r[rs]; return
            if f == 0x12: return self.setr(rd, self.lo)
            if f == 0x13: self.lo = r[rs]; return
            if f == 0x18:
                p = s32(r[rs]) * s32(r[rt])
                self.lo, self.hi = p & M32, (p >> 32) & M32
                return
            if f == 0x19:
                p = r[rs] * r[rt]
                self.lo, self.hi = p & M32, (p >> 32) & M32
                return
            if f == 0x1A:
                a, b = s32(r[rs]), s32(r[rt])
                if b != 0:
                    q = abs(a) // abs(b)
                    if (a < 0) != (b < 0): q = -q
                    rem = a - q * b
                    self.lo, self.hi = q & M32, rem & M32
                return
            if f == 0x1B:
                if r[rt] != 0:
                    self.lo, self.hi = r[rs] // r[rt], r[rs] % r[rt]
                return
            if f in (0x20, 0x21): return self.setr(rd, r[rs] + r[rt])
            if f in (0x22, 0x23): return self.setr(rd, r[rs] - r[rt])
            if f == 0x24: return self.setr(rd, r[rs] & r[rt])
            if f == 0x25: return self.setr(rd, r[rs] | r[rt])
            if f == 0x26: return self.setr(rd, r[rs] ^ r[rt])
            if f == 0x27: return self.setr(rd, ~(r[rs] | r[rt]))
            if f == 0x2A: return self.setr(rd, 1 if s32(r[rs]) < s32(r[rt]) else 0)
            if f == 0x2B: return self.setr(rd, 1 if r[rs] < r[rt] else 0)
            raise NotImplementedError("special %02x" % f)
        if op in (8, 9): return self.setr(rt, r[rs] + simm)
        if op == 0x0A: return self.setr(rt, 1 if s32(r[rs]) < simm else 0)
        if op == 0x0B: return self.setr(rt, 1 if r[rs] < (simm & M32) else 0)
        if op == 0x0C: return self.setr(rt, r[rs] & imm)
        if op == 0x0D: return self.setr(rt, r[rs] | imm)
        if op == 0x0E: return self.setr(rt, r[rs] ^ imm)
        if op == 0x0F: return self.setr(rt, imm << 16)
        if op == 0x1C:
            if f == 0x02: return self.setr(rd, s32(r[rs]) * s32(r[rt]))
            acc = (self.hi << 32) | self.lo
            if f == 0x00: acc = (acc + s32(r[rs]) * s32(r[rt])) & ((1 << 64) - 1)
            elif f == 0x01: acc = (acc + r[rs] * r[rt]) & ((1 << 64) - 1)
            elif f == 0x04: acc = (acc - s32(r[rs]) * s32(r[rt])) & ((1 << 64) - 1)
            elif f == 0x05: acc = (acc - r[rs] * r[rt]) & ((1 << 64) - 1)
            elif f == 0x20: return self.setr(rd, 32 - r[rs].bit_length())
            elif f == 0x21: return self.setr(rd, 32 - ((~r[rs]) & M32).bit_length())
            else: raise NotImplementedError("special2 %02x" % f)
            self.lo, self.hi = acc & M32, (acc >> 32) & M32
            return
        if op == 0x1F:
            if f == 0x00:
                size = rd + 1
                return self.setr(rt, (r[rs] >> sa) & ((1 << size) - 1))
            if f == 0x04:
                lsb, msb = sa, rd
                size = msb - lsb + 1
                mask = ((1 << size) - 1) << lsb
                return self.setr(rt, (r[rt] & ~mask) | ((r[rs] << lsb) & mask))
            if f == 0x20:
                if sa == 0x10: return self.setr(rd, sx8(r[rt] & 255))
                if sa == 0x18: return self.setr(rd, sx16(r[rt] & 0xFFFF))
                if sa == 0x02:
                    x = r[rt]
                    return self.setr(rd, ((x & 0x00FF00FF) << 8) | ((x >> 8) & 0x00FF00FF))
            raise NotImplementedError("special3 %02x/%02x" % (f, sa))
        a = (r[rs] + simm) & M32
        m = self.mem
        if op == 0x20: return self.setr(rt, sx8(m.rd(a, 1)))
        if op == 0x21: return self.setr(rt, sx16(m.rd(a, 2)))
        if op == 0x23: return self.setr(rt, m.rd(a, 4))
        if op == 0x24: return self.setr(rt, m.rd(a, 1))
        if op == 0x25: return self.setr(rt, m.rd(a, 2))
        if op == 0x28: return m.wr(a, 1, r[rt])
        if op == 0x29: return m.wr(a, 2, r[rt])
        if op == 0x2B: return m.wr(a, 4, r[rt])
        if op == 0x22:   # lwl (little endian): bytes from addr down to the word start fill the high bytes
            base = a & ~3
            k = a & 3
            cur = r[rt]
            word = m.rd(base, 4)
            nb = k + 1
            out = cur
            for i in range(nb):
                src = (word >> (8 * (k - i))) & 255
                pos = 3 - i
                out = (out & ~(255 << (8 * pos))) | (src << (8 * pos))
            return self.setr(rt, out)
        if op == 0x26:   # lwr: bytes from addr up to the word end fill the low bytes
            base = a & ~3
            k = a & 3
            cur = r[rt]
            word = m.rd(base, 4)
            out = cur
            for i in range(4 - k):
                src = (word >> (8 * (k + i))) & 255
                out = (out & ~(255 << (8 * i))) | (src << (8 * i))
            return self.setr(rt, out)
        if op == 0x2A:   # swl: high bytes of rt go to addr, addr-1, ... down to the word start
            base = a & ~3
            k = a & 3
            for i in range(k + 1):
                m.wr(base + k - i, 1, (r[rt] >> (8 * (3 - i))) & 255)
            return
        if op == 0x2E:   # swr: low bytes of rt go to addr, addr+1, ... up to the word end
            base = a & ~3
            k = a & 3
            for i in range(4 - k):
                m.wr(base + k + i, 1, (r[rt] >> (8 * i)) & 255)
            return
        raise NotImplementedError("op %02x" % op)


def sx8(x):
    return (x - 256 if x & 0x80 else x) & M32


def kind(w):
    op = w >> 26
    rt = (w >> 16) & 31
    if op == 0:
        f = w & 63
        if f == 8: return "jr"
        if f == 9: return "jalr"
    if op == 1 and rt in (0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13): return "branch"
    if op == 2: return "j"
    if op == 3: return "jal"
    if op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17): return "branch"
    return "plain"


def run_unit(cpu, pc):
    """execute the instruction (or branch + delay slot) at pc; returns new pc"""
    mem = cpu.mem
    w = mem.rd(pc, 4)
    k = kind(w)
    if k == "plain":
        cpu.exec_plain(w)
        return (pc + 4) & M32
    op = w >> 26
    rs, rt, rd = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    r = cpu.r
    ds = mem.rd(pc + 4, 4)
    if k in ("j", "jal"):
        tgt = ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
        if k == "jal": cpu.setr(31, pc + 8)
        cpu.exec_plain(ds)
        return tgt
    if k in ("jr", "jalr"):
        tgt = r[rs]
        if k == "jalr": cpu.setr(rd, pc + 8)
        cpu.exec_plain(ds)
        return tgt
    tgt = (pc + 4 + (sx16(w & 0xFFFF) << 2)) & M32
    if op == 4 or op == 0x14: cond = r[rs] == r[rt]
    elif op == 5 or op == 0x15: cond = r[rs] != r[rt]
    elif op in (6, 0x16): cond = s32(r[rs]) <= 0
    elif op in (7, 0x17): cond = s32(r[rs]) > 0
    elif rt in (0, 2, 0x10, 0x12): cond = s32(r[rs]) < 0
    else: cond = s32(r[rs]) >= 0
    likely = op in (0x14, 0x15, 0x16, 0x17) or (op == 1 and rt in (2, 3, 0x12, 0x13))
    if op == 1 and rt in (0x10, 0x11, 0x12, 0x13): cpu.setr(31, pc + 8)
    if likely:
        if cond:
            cpu.exec_plain(ds)
            return tgt
        return (pc + 8) & M32
    cpu.exec_plain(ds)
    return tgt if cond else (pc + 8) & M32


def main():
    image = open(sys.argv[1], "rb").read()
    vec = open(sys.argv[2], "rb").read()
    mem = Mem(image)
    cpu = Cpu(mem)
    p = 0
    records = passed = failed = 0
    n = len(vec)
    while p < n:
        s, e, t = struct.unpack_from("<III", vec, p); p += 12
        regs = list(struct.unpack_from("<32I", vec, p)); p += 128
        hi, lo, nextn, executed = struct.unpack_from("<IIII", vec, p); p += 16
        rout = list(struct.unpack_from("<32I", vec, p)); p += 128
        hio, loo, nst = struct.unpack_from("<III", vec, p); p += 12
        st = []
        for k in range(nst):
            a, l = struct.unpack_from("<II", vec, p); p += 8
            st.append((a, l))
        sd = []
        for a, l in st:
            sd.append(vec[p:p + l]); p += l
        records += 1
        cpu.r = regs[:]
        cpu.hi, cpu.lo = hi, lo
        pc = s
        ok = True
        why = ""
        try:
            steps = 0
            while True:
                pc = run_unit(cpu, pc)
                steps += 1
                if not (s <= pc < e) or steps > 6000:
                    break
        except (IndexError, NotImplementedError) as ex:
            ok, why = False, "reference error: %s" % ex
        if ok:
            if pc != nextn: ok, why = False, "next pc ref %08x native %08x" % (pc, nextn)
            else:
                for i in range(1, 32):
                    if cpu.r[i] != rout[i]:
                        ok, why = False, "r%d ref %08x native %08x" % (i, cpu.r[i], rout[i])
                        break
            if ok and (cpu.hi != hio or cpu.lo != loo): ok, why = False, "hi/lo"
            if ok:
                for (a, l), d in zip(st, sd):
                    if bytes(mem.m[a:a + l]) != d:
                        ok, why = False, "store data at %08x" % a
                        break
        if ok:
            passed += 1
        else:
            failed += 1
            if failed <= 30:
                print("MISMATCH run %08x..%08x trial %d: %s" % (s, e, t, why))
                for a in range(s, e, 4):
                    print("   %08x: %08x" % (a, mem.rd(a, 4)))
        mem.restore()
    print("records %d: passed %d, failed %d" % (records, passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
