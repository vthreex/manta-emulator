#!/usr/bin/env python3
"""Static recompiler: MIPS32 little-endian machine code -> C++ (x86_64 / ARM native), for the Manta LCD4214 runtime image.

    python native/tools/mips_recomp.py IMAGE.bin OUTDIR [--base 0x803b0180] [--code-end 0x8060b000] [--files 24]

Design ("run functions"): every straight-line sequence of instructions ends after a branch/jump delay slot. Each such run becomes one C++ function
`uint32_t R_<start>(CPU* c, uint32_t pc)` that contains one `case` label per instruction (a switch used as a computed entry), so the dispatcher can
enter a run at ANY instruction - jump tables, function pointers and returns into the middle of a run need no leader analysis. The function executes
until the end of the run and returns the next guest pc; branches into the same run loop inside the function. The runtime dispatcher
(native/runtime/runtime.cpp) looks the next pc up in a table, counts instructions, and delivers timer/external interrupts BETWEEN runs.

Only the code reachable from the roots is emitted (entry, exception vector, every `jal` target, and every aligned pointer into the code range found anywhere in the
image, which covers jump tables and callback tables). Words that do not decode as supported MIPS32r2 stop a path and are emitted as a trap.
Nothing here depends on, or contains, any copy of the firmware: the generated files are derived from the user's own image and are not meant to be redistributed.
"""
import argparse, os, struct, sys
from collections import deque

SUPPORTED_NOTE = "MIPS32r2 subset used by the image; unsupported encodings become traps"


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


class Insn:
    __slots__ = ("pc", "w", "kind", "text", "target", "link", "likely", "cond", "uncond", "rs", "rt")


def decode_kind(w, pc):
    """classify control flow: returns (kind, target) ; kind in plain/branch/jump/jr/jalr/jal/trap/eret/bad"""
    op = w >> 26
    rs = (w >> 21) & 31
    rt = (w >> 16) & 31
    if op == 0:
        f = w & 63
        if f == 8: return "jr", None
        if f == 9: return "jalr", None
        if f in (0x0c, 0x0d): return "trap", None
    elif op == 1:
        r = rt
        if r in (0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13): return "branch", (pc + 4 + (sx16(w & 0xffff) << 2)) & 0xffffffff
    elif op == 2:
        return "j", ((pc + 4) & 0xf0000000) | ((w & 0x3ffffff) << 2)
    elif op == 3:
        return "jal", ((pc + 4) & 0xf0000000) | ((w & 0x3ffffff) << 2)
    elif op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17):
        return "branch", (pc + 4 + (sx16(w & 0xffff) << 2)) & 0xffffffff
    elif op == 0x10 and (w & 0x02000000) and (w & 0x3f) == 0x18:
        return "eret", None
    return "plain", None


# ---- instruction translation (non-control) ----
LOADS = {0x20: ("LB", True, 1), 0x21: ("LH", True, 2), 0x23: ("LW", False, 4), 0x24: ("LBU", False, 1), 0x25: ("LHU", False, 2), 0x30: ("LW", False, 4)}


def tr_plain(w, pc):
    """returns a C++ statement string, or None if unsupported"""
    op = w >> 26
    rs = (w >> 21) & 31
    rt = (w >> 16) & 31
    rd = (w >> 11) & 31
    sa = (w >> 6) & 31
    imm = w & 0xffff
    simm = sx16(imm)
    f = w & 63

    def setr(reg, expr):
        if reg == 0:
            return "(void)(%s);" % expr
        return "r[%d]=%s;" % (reg, expr)

    R = lambda i: "r[%d]" % i if i else "0u"
    if op == 0:
        if w == 0: return ";"
        if f == 0x00: return setr(rd, "%s<<%d" % (R(rt), sa))
        if f == 0x02:
            if rs == 1: return setr(rd, "ROR32(%s,%d)" % (R(rt), sa))   # rotr
            return setr(rd, "%s>>%d" % (R(rt), sa))
        if f == 0x03: return setr(rd, "(uint32_t)((int32_t)%s>>%d)" % (R(rt), sa))
        if f == 0x04: return setr(rd, "%s<<(%s&31)" % (R(rt), R(rs)))
        if f == 0x06:
            if sa == 1: return setr(rd, "ROR32(%s,%s&31)" % (R(rt), R(rs)))
            return setr(rd, "%s>>(%s&31)" % (R(rt), R(rs)))
        if f == 0x07: return setr(rd, "(uint32_t)((int32_t)%s>>(%s&31))" % (R(rt), R(rs)))
        if f == 0x0a: return "if(%s==0)%s" % (R(rt), setr(rd, R(rs))) if rd else ";"
        if f == 0x0b: return "if(%s!=0)%s" % (R(rt), setr(rd, R(rs))) if rd else ";"
        if f == 0x0f: return ";"   # sync
        if f == 0x10: return setr(rd, "c->hi")
        if f == 0x11: return "c->hi=%s;" % R(rs)
        if f == 0x12: return setr(rd, "c->lo")
        if f == 0x13: return "c->lo=%s;" % R(rs)
        if f == 0x18: return "{int64_t p=(int64_t)(int32_t)%s*(int64_t)(int32_t)%s;c->lo=(uint32_t)p;c->hi=(uint32_t)((uint64_t)p>>32);}" % (R(rs), R(rt))
        if f == 0x19: return "{uint64_t p=(uint64_t)%s*(uint64_t)%s;c->lo=(uint32_t)p;c->hi=(uint32_t)(p>>32);}" % (R(rs), R(rt))
        if f == 0x1a: return "{int32_t a=(int32_t)%s,b=(int32_t)%s;if(b){if(b==-1){c->lo=(uint32_t)(0u-(uint32_t)a);c->hi=0;}else{c->lo=(uint32_t)(a/b);c->hi=(uint32_t)(a%%b);}}}" % (R(rs), R(rt))
        if f == 0x1b: return "{uint32_t a=%s,b=%s;if(b){c->lo=a/b;c->hi=a%%b;}}" % (R(rs), R(rt))
        if f in (0x20, 0x21): return setr(rd, "%s+%s" % (R(rs), R(rt)))
        if f in (0x22, 0x23): return setr(rd, "%s-%s" % (R(rs), R(rt)))
        if f == 0x24: return setr(rd, "%s&%s" % (R(rs), R(rt)))
        if f == 0x25: return setr(rd, "%s|%s" % (R(rs), R(rt)))
        if f == 0x26: return setr(rd, "%s^%s" % (R(rs), R(rt)))
        if f == 0x27: return setr(rd, "~(%s|%s)" % (R(rs), R(rt)))
        if f == 0x2a: return setr(rd, "(uint32_t)((int32_t)%s<(int32_t)%s)" % (R(rs), R(rt)))
        if f == 0x2b: return setr(rd, "(uint32_t)(%s<%s)" % (R(rs), R(rt)))
        if f in (0x30, 0x31, 0x32, 0x33, 0x34, 0x36):   # conditional traps (compilers emit teq divisor,zero after divisions)
            cond = {0x30: "(int32_t)%s>=(int32_t)%s", 0x31: "%s>=%s", 0x32: "(int32_t)%s<(int32_t)%s", 0x33: "%s<%s", 0x34: "%s==%s", 0x36: "%s!=%s"}[f] % (R(rs), R(rt))
            return "if(%s) return RAISE_TRAP(c,0x%08xu);" % (cond, pc)
        return None
    if op == 1:
        return None
    if op == 8 or op == 9: return setr(rt, "%s+%du" % (R(rs), simm & 0xffffffff))
    if op == 0x0a: return setr(rt, "(uint32_t)((int32_t)%s<%d)" % (R(rs), simm))
    if op == 0x0b: return setr(rt, "(uint32_t)(%s<%du)" % (R(rs), simm & 0xffffffff))
    if op == 0x0c: return setr(rt, "%s&%du" % (R(rs), imm))
    if op == 0x0d: return setr(rt, "%s|%du" % (R(rs), imm))
    if op == 0x0e: return setr(rt, "%s^%du" % (R(rs), imm))
    if op == 0x0f: return setr(rt, "%du" % (imm << 16))
    if op == 0x10:
        fmt = rs
        if fmt == 0: return setr(rt, "CP0R(c,%d,%d)" % (rd, w & 7))
        if fmt == 4: return "CP0W(c,%d,%d,%s);" % (rd, w & 7, R(rt))
        if fmt == 0x0b:   # di/ei (mfmc0)
            if (w & 0x20): return "%sCP0EI(c);" % ((setr(rt, "c->cp0[12][0]") + " ") if rt else "")
            return "%sCP0DI(c);" % ((setr(rt, "c->cp0[12][0]") + " ") if rt else "")
        if w & 0x02000000:
            f2 = w & 63
            if f2 in (0x01, 0x02, 0x06, 0x08): return ";"   # tlbr/tlbwi/tlbwr/tlbp
            if f2 == 0x20: return "c->wait=1;"
        return None
    if op == 0x1c:
        if f == 0x02: return setr(rd, "(uint32_t)((int64_t)(int32_t)%s*(int64_t)(int32_t)%s)" % (R(rs), R(rt)))
        if f == 0x00: return "{int64_t acc=(int64_t)(((uint64_t)c->hi<<32)|c->lo)+(int64_t)(int32_t)%s*(int64_t)(int32_t)%s;c->lo=(uint32_t)acc;c->hi=(uint32_t)((uint64_t)acc>>32);}" % (R(rs), R(rt))
        if f == 0x01: return "{uint64_t acc=(((uint64_t)c->hi<<32)|c->lo)+(uint64_t)%s*(uint64_t)%s;c->lo=(uint32_t)acc;c->hi=(uint32_t)(acc>>32);}" % (R(rs), R(rt))
        if f == 0x04: return "{int64_t acc=(int64_t)(((uint64_t)c->hi<<32)|c->lo)-(int64_t)(int32_t)%s*(int64_t)(int32_t)%s;c->lo=(uint32_t)acc;c->hi=(uint32_t)((uint64_t)acc>>32);}" % (R(rs), R(rt))
        if f == 0x05: return "{uint64_t acc=(((uint64_t)c->hi<<32)|c->lo)-(uint64_t)%s*(uint64_t)%s;c->lo=(uint32_t)acc;c->hi=(uint32_t)(acc>>32);}" % (R(rs), R(rt))
        if f == 0x20: return setr(rd, "(%s?(uint32_t)__builtin_clz(%s):32u)" % (R(rs), R(rs)))
        if f == 0x21: return setr(rd, "(~%s?(uint32_t)__builtin_clz(~%s):32u)" % (R(rs), R(rs)))
        return None
    if op == 0x1f:
        if f == 0x00:   # ext
            size = rd + 1
            mask = (1 << size) - 1
            return setr(rt, "(%s>>%d)&%du" % (R(rs), sa, mask))
        if f == 0x04:   # ins
            lsb, msb = sa, rd
            size = msb - lsb + 1
            if size <= 0: return None
            mask = ((1 << size) - 1) & 0xffffffff
            return setr(rt, "(%s&~(%du<<%d))|((%s&%du)<<%d)" % (R(rt), mask, lsb, R(rs), mask, lsb))
        if f == 0x20:
            if sa == 0x10: return setr(rd, "(uint32_t)(int32_t)(int8_t)%s" % R(rt))
            if sa == 0x18: return setr(rd, "(uint32_t)(int32_t)(int16_t)%s" % R(rt))
            if sa == 0x02: return setr(rd, "((%s&0x00ff00ffu)<<8)|((%s>>8)&0x00ff00ffu)" % (R(rt), R(rt)))
            return None
        if f == 0x3b: return setr(rt, "c->hwr[%d]" % rd)
        return None
    if op in LOADS:
        name, signed, size = LOADS[op]
        e = "%s(c,%s+%du,0x%08x)" % (name, R(rs), simm & 0xffffffff, w and 0)
        return None  # placeholder, replaced in tr_mem (needs pc)
    return None


def tr_mem(w, pc):
    op = w >> 26
    rs = (w >> 21) & 31
    rt = (w >> 16) & 31
    simm = sx16(w & 0xffff)
    R = lambda i: "r[%d]" % i if i else "0u"
    addr = "(%s+%du)" % (R(rs), simm & 0xffffffff)
    P = "0x%08xu" % pc

    def setr(reg, expr):
        return ("r[%d]=%s;" % (reg, expr)) if reg else ("(void)(%s);" % expr)

    if op == 0x20: return setr(rt, "(uint32_t)(int32_t)(int8_t)LB(c,%s,%s)" % (addr, P))
    if op == 0x21: return setr(rt, "(uint32_t)(int32_t)(int16_t)LH(c,%s,%s)" % (addr, P))
    if op in (0x23, 0x30): return setr(rt, "LW(c,%s,%s)" % (addr, P))
    if op == 0x24: return setr(rt, "LB(c,%s,%s)" % (addr, P))
    if op == 0x25: return setr(rt, "LH(c,%s,%s)" % (addr, P))
    if op == 0x22: return setr(rt, "LWL(c,%s,%s,%s)" % (addr, R(rt), P))
    if op == 0x26: return setr(rt, "LWR(c,%s,%s,%s)" % (addr, R(rt), P))
    if op == 0x28: return "SB(c,%s,%s,%s);" % (addr, R(rt), P)
    if op == 0x29: return "SH(c,%s,%s,%s);" % (addr, R(rt), P)
    if op == 0x2b: return "SW(c,%s,%s,%s);" % (addr, R(rt), P)
    if op == 0x2a: return "SWL(c,%s,%s,%s);" % (addr, R(rt), P)
    if op == 0x2e: return "SWR(c,%s,%s,%s);" % (addr, R(rt), P)
    if op == 0x38: return "SW(c,%s,%s,%s);%s" % (addr, R(rt), P, ("r[%d]=1;" % rt) if rt else "")   # sc: single CPU, always succeeds
    if op in (0x2f, 0x33): return ";"   # cache / pref
    return None


def translate(w, pc):
    op = w >> 26
    if op in (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2a, 0x2b, 0x2e, 0x2f, 0x30, 0x33, 0x38):
        return tr_mem(w, pc)
    return tr_plain(w, pc)


def supported(w, pc):
    kind, _ = decode_kind(w, pc)
    if kind != "plain":
        return True
    return translate(w, pc) is not None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("out")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x803b0180)
    ap.add_argument("--code-end", type=lambda s: int(s, 0), default=0x8060b000)
    ap.add_argument("--entry", type=lambda s: int(s, 0), action="append", default=[])
    ap.add_argument("--files", type=int, default=24)
    # extra code regions (boot ROM, second stage): own table symbols PREFIX_BASE/_WORDS/_INDEX/_RUNS/_RUN_COUNT, own output directory
    ap.add_argument("--prefix", default="GEN")
    # small regions entered through computed jumps (boot ROM): every aligned word in the range is a root
    ap.add_argument("--all-entries", action="store_true")
    a = ap.parse_args()
    img = open(a.image, "rb").read()
    n = len(img) // 4
    words = struct.unpack("<%dI" % n, img[: n * 4])
    base = a.base
    code_end = min(a.code_end, base + n * 4)
    nwords_code = (code_end - base) >> 2
    W = lambda pc: words[(pc - base) >> 2]
    in_code = lambda pc: base <= pc < code_end and not (pc & 3)

    roots = set(a.entry) | ({0x803b0600, 0x803b0180} if a.prefix == "GEN" else {base})
    if a.all_entries:
        roots |= set(range(base, code_end, 4))
    # pointers anywhere in the image that point into the code range (jump tables, callback tables), and every jal target
    for i, w in enumerate(words):
        if in_code(w):
            roots.add(w)
    # address constants built in code: lui rX,hi followed by addiu/ori rX,rX,lo (function pointers taken with `la`)
    for i, w in enumerate(words):
        if (w >> 26) == 0x0f:
            rt = (w >> 16) & 31
            hi = (w & 0xffff) << 16
            for j in range(i + 1, min(i + 8, len(words))):
                v = words[j]
                op2 = v >> 26
                if op2 in (9, 0x0d) and ((v >> 21) & 31) == rt:
                    lo = v & 0xffff
                    val = (hi + (sx16(lo) if op2 == 9 else lo)) & 0xffffffff
                    if in_code(val):
                        roots.add(val)
                    if ((v >> 16) & 31) == rt:
                        break
                if (v >> 16) & 31 == rt and op2 not in (0x2b, 0x28, 0x29):   # rt overwritten
                    if op2 in (0x23, 0x0f, 0x24, 0x25, 0x20, 0x21):
                        break
    reach = set()
    work = deque(r for r in roots if in_code(r))
    unsup = {}

    def push(t):
        if in_code(t) and t not in reach:
            work.append(t)

    while work:
        pc = work.popleft()
        while in_code(pc) and pc not in reach:
            w = W(pc)
            kind, tgt = decode_kind(w, pc)
            if kind == "plain" and translate(w, pc) is None:
                unsup[pc] = w
                reach.add(pc)   # emitted as a trap
                break
            reach.add(pc)
            if kind == "plain":
                pc += 4
                continue
            if kind in ("j", "jal", "branch"):
                push(tgt)
            # delay slot
            ds = pc + 4
            if in_code(ds):
                reach.add(ds)
                if decode_kind(W(ds), ds)[0] == "plain" and translate(W(ds), ds) is None:
                    unsup[ds] = W(ds)
            if kind in ("j", "jr", "eret"):
                break
            if kind == "trap":
                pc += 4   # after break/syscall resume is at pc+4 (an exception handler returns there)
                continue
            if kind == "eret":
                break
            pc += 8   # branch/jal/jalr fall-through after the delay slot (eret has none)
    # jal/jalr targets are call targets; calls return to pc+8 which is already covered above
    order = sorted(reach)
    # ---- build runs ----
    runs = []   # (start, end_exclusive)
    cur = None
    i = 0
    rs_ = set(reach)
    pc_list = order
    run_start = None
    last = None
    ends_after = set()
    for pc in pc_list:
        w = W(pc)
        kind, _ = decode_kind(w, pc)
        if kind in ("j", "jal", "jr", "jalr", "branch"):
            ends_after.add(pc + 4)   # run ends after the delay slot
        elif kind in ("trap", "eret"):
            ends_after.add(pc)
    for pc in pc_list:
        if run_start is None:
            run_start = pc
        elif pc != last + 4:
            runs.append((run_start, last + 4))
            run_start = pc
        last = pc
        if pc in ends_after:
            runs.append((run_start, pc + 4))
            run_start = None
            last = None
    if run_start is not None:
        runs.append((run_start, last + 4))
    print("reachable instructions:", len(reach), "runs:", len(runs), "unsupported words:", len(unsup))

    os.makedirs(a.out, exist_ok=True)
    run_index = [0] * nwords_code
    for ri, (s, e) in enumerate(runs):
        for pc in range(s, e, 4):
            run_index[(pc - base) >> 2] = ri + 1

    # ---- emit ----
    HEAD = '#include "rt.hpp"\n'
    nf = max(1, a.files)
    per = (len(runs) + nf - 1) // nf
    names = []
    for fi in range(nf):
        chunk = runs[fi * per:(fi + 1) * per]
        if not chunk:
            continue
        lines = [HEAD]
        for (s, e) in chunk:
            name = "R_%08x" % s
            names.append(name)
            lines.append("uint32_t %s(CPU* c, uint32_t pc){\n uint32_t* const r=c->r;\n for(;;){\n c->icount+=(0x%08xu-pc)>>2; LOOPCHK(c);\n switch(pc){" % (name, e))
            pcs = list(range(s, e, 4))
            k = 0
            while k < len(pcs):
                pc = pcs[k]
                w = W(pc)
                kind, tgt = decode_kind(w, pc)
                lines.append(" case 0x%08xu:" % pc)
                if pc in unsup and kind == "plain":
                    lines.append("  TRAP(c,0x%08xu,0x%08xu);" % (pc, w))
                    k += 1
                    continue
                if kind == "plain":
                    lines.append("  " + translate(w, pc))
                    k += 1
                    continue
                if kind == "eret":
                    lines.append("  return ERET(c);")
                    k += 1
                    continue
                if kind == "trap":
                    f = w & 63
                    lines.append("  return RAISE_%s(c,0x%08xu);" % ("SYSCALL" if f == 0x0c else "BREAK", pc))
                    k += 1
                    continue
                # control transfer with delay slot
                ds = pc + 4
                dsw = W(ds) if in_code(ds) else 0
                dst = translate(dsw, ds) if decode_kind(dsw, ds)[0] == "plain" else None
                if dst is None:
                    dst = "TRAP(c,0x%08xu,0x%08xu);" % (ds, dsw)
                op = w >> 26
                rs = (w >> 21) & 31
                rt = (w >> 16) & 31
                rd = (w >> 11) & 31
                R = lambda i: "r[%d]" % i if i else "0u"
                body = []
                if kind == "j":
                    body.append("{ %s return 0x%08xu; }" % (dst, tgt))
                elif kind == "jal":
                    body.append("{ r[31]=0x%08xu; %s return 0x%08xu; }" % (pc + 8, dst, tgt))
                elif kind == "jr":
                    body.append("{ uint32_t t=%s; %s return t; }" % (R(rs), dst))
                elif kind == "jalr":
                    link = ("r[%d]=0x%08xu; " % (rd, pc + 8)) if rd else ""
                    body.append("{ uint32_t t=%s; %s%s return t; }" % (R(rs), link, dst))
                else:   # conditional branch
                    likely = op in (0x14, 0x15, 0x16, 0x17) or (op == 1 and rt in (2, 3, 0x12, 0x13))
                    link = op == 1 and rt in (0x10, 0x11, 0x12, 0x13)
                    if op == 4: cond = "%s==%s" % (R(rs), R(rt))
                    elif op == 5: cond = "%s!=%s" % (R(rs), R(rt))
                    elif op in (6, 0x16): cond = "(int32_t)%s<=0" % R(rs)
                    elif op in (7, 0x17): cond = "(int32_t)%s>0" % R(rs)
                    elif op == 0x14: cond = "%s==%s" % (R(rs), R(rt))
                    elif op == 0x15: cond = "%s!=%s" % (R(rs), R(rt))
                    elif op == 1 and rt in (0, 2, 0x10, 0x12): cond = "(int32_t)%s<0" % R(rs)
                    else: cond = "(int32_t)%s>=0" % R(rs)
                    in_run = s <= tgt < e
                    go = "{ pc=0x%08xu; continue; }" % tgt if in_run else "return 0x%08xu;" % tgt
                    lk = ("r[31]=0x%08xu; " % (pc + 8)) if link else ""
                    if likely:
                        body.append("{ bool tk=(%s); %sif(tk){ %s %s } return 0x%08xu; }" % (cond, lk, dst, go, pc + 8))
                    else:
                        body.append("{ bool tk=(%s); %s%s if(tk) %s return 0x%08xu; }" % (cond, lk, dst, go, pc + 8))
                lines.append("  " + body[0])
                # entering exactly at the delay slot (a jump target equal to it) executes it alone and continues sequentially
                lines.append(" case 0x%08xu: %s return 0x%08xu;" % (ds, dst, pc + 8))
                k += 2   # branch + delay slot
            # run ended without control transfer (data boundary / unsupported): continue at the next address
            lines.append("  return 0x%08xu;\n default: return BADPC(c,pc);\n }\n }\n}" % e)
        stem = "runs_%02d.cpp" % fi if a.prefix == "GEN" else "%s_runs_%02d.cpp" % (a.prefix.lower(), fi)
        open(os.path.join(a.out, stem), "w").write("\n".join(lines) + "\n")
    # tables
    t = [HEAD, "extern uint32_t R_dummy;"]
    t = [HEAD]
    P = a.prefix
    t.append("const uint32_t %s_BASE=0x%08xu, %s_WORDS=%du;" % (P, base, P, nwords_code))
    for nme in names:
        t.append("uint32_t %s(CPU*,uint32_t);" % nme)
    t.append("const RunFn %s_RUNS[]={0,%s};" % (P, ",".join(names)))
    t.append("const unsigned %s_RUN_COUNT=%d;" % (P, len(names)))
    # index table as uint32 array
    t.append("const uint32_t %s_INDEX[%d]={" % (P, nwords_code))
    for i in range(0, nwords_code, 16):
        t.append(",".join(str(x) for x in run_index[i:i + 16]) + ",")
    t.append("};")
    if P != "GEN":   # extra region: register with the runtime dispatcher
        t.append("static const int %s_REGISTERED = native_register_region(%s_BASE, %s_WORDS, %s_INDEX, %s_RUNS, \"%s\");" % (P, P, P, P, P, P))
    open(os.path.join(a.out, "tables.cpp" if P == "GEN" else "%s_tables.cpp" % P.lower()), "w").write("\n".join(t) + "\n")
    open(os.path.join(a.out, "unsupported.txt"), "w").write("\n".join("%08x %08x" % (k, v) for k, v in sorted(unsup.items())) + "\n")
    print("files:", nf, "runs emitted:", len(names))


if __name__ == "__main__":
    main()
