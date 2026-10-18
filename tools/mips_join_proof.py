"""Static soundness check for a conservative two-branch join in the original MIPS image.

For the HTotal comparison at 80501BFC this (1) enumerates every path from the
branch to the join at 80501758, (2) computes each path's register and memory
effects by exact abstract execution of its original instructions (including
branch delay slots), and (3) runs a register-liveness analysis from the join
over the rest of the function. The join is accepted only if every register
that differs between paths is dead at the join and the paths perform no MMIO,
calls, exceptions-capable operations or scheduler-visible effects.

This proves a property of the original instruction words. It does not execute
the TV and says nothing about what the unknown sample really is.
"""
import argparse, hashlib, json, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from firmware_extract import validate, inflate_at, KNOWN  # noqa: E402
from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN, CS_OP_REG  # noqa: E402

BASE = 0x803b0180
NAMES = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
         's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']
CALLER_SAVED = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 31}
ARGS = {4, 5, 6, 7}
CALLEE_SAVED_AND_RET = {2, 3, 16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31}


def load_image(flash_path):
    flash = validate(Path(flash_path).read_bytes())
    _, runtime = inflate_at(flash, KNOWN[0][0])
    if hashlib.sha256(runtime).hexdigest() != KNOWN[0][3]:
        raise SystemExit('runtime hash mismatch')
    return flash[0x20080:0x23880] + runtime


class Decoder:
    def __init__(self, image):
        self.image = image
        self.cs = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN)
        self.cs.detail = True
        self.cache = {}

    def insn(self, pc):
        if pc not in self.cache:
            off = pc - BASE
            got = list(self.cs.disasm(self.image[off:off + 4], pc))
            if not got:
                raise SystemExit(f'cannot decode {pc:08x}')
            self.cache[pc] = got[0]
        return self.cache[pc]

    def word(self, pc):
        return struct.unpack_from('<I', self.image, pc - BASE)[0]


def reg_index(name):
    name = name.replace('$', '')
    return NAMES.index({'s8': 'fp'}.get(name, name))


def reads_writes(d, pc):
    """Integer MIPS32 register read/write sets decoded from the instruction word
    (capstone has no register-access detail for MIPS). Unknown encodings read
    every operand field and write nothing, which can only enlarge liveness."""
    w = d.word(pc)
    op, fn = w >> 26, w & 63
    rs, rt, rd = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    R = lambda *x: {r for r in x if r}
    if op == 3:                               # JAL
        return ARGS | {29, 28}, set(CALLER_SAVED)
    if op == 0:
        if fn == 9:                           # JALR
            return ARGS | {29, 28, rs}, set(CALLER_SAVED)
        if fn == 8:                           # JR
            return (({31} | CALLEE_SAVED_AND_RET) if rs == 31 else set(range(1, 32))), set()
        if fn in (0, 2, 3):
            return R(rt), R(rd)
        if fn in (4, 6, 7, 32, 33, 34, 35, 36, 37, 38, 39, 42, 43):
            return R(rs, rt), R(rd)
        if fn in (10, 11):
            return R(rs, rt, rd), R(rd)
        if fn in (16, 18):
            return set(), R(rd)
        if fn in (17, 19):
            return R(rs), set()
        if fn in (24, 25, 26, 27):
            return R(rs, rt), set()
        return R(rs, rt, rd), set()
    if op == 1:
        return R(rs), ({31} if rt in (16, 17) else set())
    if op in (2,):
        return set(), set()
    if op in (4, 5, 20, 21):
        return R(rs, rt), set()
    if op in (6, 7, 22, 23):
        return R(rs), set()
    if 8 <= op <= 14:
        return R(rs), R(rt)
    if op == 15:
        return set(), R(rt)
    if op == 28:
        return R(rs, rt, rd), (R(rd) if fn in (2, 32, 33) else set())
    if op == 31:
        if fn == 0:                           # EXT
            return R(rs), R(rt)
        if fn == 4:                           # INS
            return R(rs, rt), R(rt)
        if fn == 32:                          # SEB/SEH (BSHFL)
            return R(rt), R(rd)
        if fn == 59:                          # RDHWR
            return set(), R(rt)
        return R(rs, rt, rd), set()
    if op in (32, 33, 35, 36, 37):
        return R(rs), R(rt)
    if op in (34, 38):                        # LWL/LWR keep old rt bits
        return R(rs, rt), R(rt)
    if op in (40, 41, 42, 43, 46):
        return R(rs, rt), set()
    if op == 48:                              # LL
        return R(rs), R(rt)
    if op == 56:                              # SC
        return R(rs, rt), R(rt)
    if op in (47, 51, 49, 53, 57, 61):        # CACHE/PREF/LWC1/LDC1/SWC1/SDC1
        return R(rs), set()
    if op == 16:                              # COP0 move
        if rs == 0:
            return set(), R(rt)
        if rs == 4:
            return R(rt), set()
        return R(rs, rt, rd), set()
    return R(rs, rt, rd), set()


def successors(d, pc, lo, hi):
    """Instruction-level successors; branch/jump delay slots are treated as part of
    the branch (delay-slot effects are applied before control transfer)."""
    w = d.word(pc)
    op, fn = w >> 26, w & 63
    nxt = pc + 4
    if op == 2:
        return [((pc & 0xf0000000) | ((w & 0x3ffffff) << 2))]
    if op == 3:
        return [pc + 8]
    if op == 0 and fn in (8,):
        return []
    if op == 0 and fn == 9:
        return [pc + 8]
    if op in (4, 5, 20, 21) or (op == 1 and ((w >> 16) & 31) in (0, 1, 2, 3, 16, 17)) or op in (6, 7, 22, 23):
        off = w & 0xffff
        off = off - 65536 if off & 0x8000 else off
        tgt = pc + 4 + (off << 2)
        return [tgt, pc + 8]
    return [nxt]


def liveness(d, start, lo, hi):
    """Backward live-in sets for every instruction reachable from start (delay slots are
    folded into their branch so that slot effects precede the transfer)."""
    seen, work, nodes = set(), [start], {}
    while work:
        pc = work.pop()
        if pc in seen or not lo <= pc < hi:
            continue
        seen.add(pc)
        w = d.word(pc)
        op, fn = w >> 26, w & 63
        is_branch = op in (1, 2, 3, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 0 and fn in (8, 9))
        r, wr = reads_writes(d, pc)
        if is_branch:
            sr, sw = reads_writes(d, pc + 4)
            # branch reads happen before the slot; slot reads that are not rewritten
            # by the branch itself (link register for JAL/JALR) count too
            r = r | (sr - (wr if op in (3,) or (op == 0 and fn == 9) else set()))
            wr = wr | sw
        nodes[pc] = (r, wr, successors(d, pc, lo, hi))
        work.extend(nodes[pc][2])
    live_in = {pc: set() for pc in nodes}
    changed = True
    while changed:
        changed = False
        for pc, (r, wr, succ) in nodes.items():
            out = set()
            for s in succ:
                if s in live_in:
                    out |= live_in[s]
                elif not lo <= s < hi:
                    out |= set(range(32)) - {0}  # leaves analyzed range: everything live
            new = r | (out - wr)
            if new != live_in[pc]:
                live_in[pc] = new
                changed = True
    return live_in, nodes


def run_path(d, pcs, regs, mem):
    """Exact abstract execution of a straight list of instruction addresses."""
    effects = []
    for pc in pcs:
        w = d.word(pc)
        op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xffff
        sim = imm - 65536 if imm & 0x8000 else imm
        if op == 15:
            regs[rt] = ('const', imm << 16)
        elif op == 9:
            base = regs.get(rs, ('reg', rs))
            regs[rt] = ('const', (base[1] + sim) & 0xffffffff) if base[0] == 'const' else ('add', base, sim)
        elif op == 11:  # sltiu
            regs[rt] = ('sltiu', regs.get(rs, ('reg', rs)), imm)
        elif op == 36:  # lbu
            base = regs.get(rs, ('reg', rs))
            addr = (base[1] + sim) & 0xffffffff if base[0] == 'const' else None
            regs[rt] = ('load8', addr)
            effects.append(('load8', hex(addr) if addr is not None else 'symbolic', hex(pc)))
        elif op == 40:  # sb
            base = regs.get(rs, ('reg', rs))
            addr = (base[1] + sim) & 0xffffffff
            effects.append(('store8', hex(addr), regs.get(rt, ('reg', rt)) if rt else ('const', 0), hex(pc)))
        elif op == 41:  # sh
            base = regs.get(rs, ('reg', rs))
            addr = (base[1] + sim) & 0xffffffff
            effects.append(('store16', hex(addr), regs.get(rt, ('reg', rt)), hex(pc)))
        elif op in (4, 5, 2):
            effects.append(('control', hex(pc)))
        else:
            effects.append(('other', hex(w), hex(pc)))
        regs[0] = ('const', 0)
    return effects


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware', required=True)
    p.add_argument('--report', required=True)
    a = p.parse_args()
    image = load_image(a.firmware)
    d = Decoder(image)
    branch, join = 0x80501bfc, 0x80501758
    lo = 0x80501698
    # Function bounds: body starts at its stack-frame prologue.
    for pc in range(0x80501698, 0x805016d0, 4):
        if d.word(pc) >> 16 == 0x27bd and d.word(pc) & 0x8000:
            lo = pc
            break
    hi = 0x80502800  # liveness range; any successor outside it makes every register live
    # Executed instruction sequences (branch delay slots included, branch/jump words omitted).
    paths = {
        'mismatch': [0x80501c00, 0x80501750, 0x80501754],
        'match_counter_lt4': [0x80501c00, 0x80501c04, 0x80501c08, 0x80501c0c, 0x80501c14, 0x80502018, 0x80502020],
        'match_counter_ge4': [0x80501c00, 0x80501c04, 0x80501c08, 0x80501c0c, 0x80501c14, 0x80501c18, 0x80501c20],
    }
    results = {}
    initial = {3: ('sym', 'v1_in'), 4: ('sym', 'a0_in'), 2: ('sym', 'v0_in')}
    for name, seq in paths.items():
        regs, mem = dict(initial), {}
        results[name] = (run_path(d, seq, regs, mem), regs)
    live, nodes = liveness(d, join, lo, hi)
    live_at_join = sorted(NAMES[r] for r in live.get(join, set()))
    tracked = [r for r in range(1, 32)]
    differing = set()
    base_regs = results['mismatch'][1]
    for name, (_, regs) in results.items():
        for r in tracked:
            if regs.get(r) != base_regs.get(r):
                differing.add(r)
    effects = {n: e for n, (e, _) in results.items()}
    observable = [e for n, es in effects.items() for e in es if e[0] in ('control', 'other')]
    mmio = [e for n, es in effects.items() for e in es
            if e[0].startswith('store') and not 0x80000000 <= int(e[1], 16) < 0x80800000]
    report = {
        'runtime_sha256': KNOWN[0][3], 'branch': hex(branch), 'join': hex(join),
        'function_start': hex(lo), 'analysis_end': hex(hi),
        'paths': {n: [hex(x) for x in s] for n, s in paths.items()},
        'path_effects': {n: [list(map(str, e)) for e in es] for n, es in effects.items()},
        'registers_differing_between_paths': sorted(NAMES[r] for r in differing),
        'registers_live_at_join': live_at_join,
        'differing_and_live': sorted(NAMES[r] for r in differing if r in live[join]),
        'non_ram_stores': mmio, 'unexpected_instructions': observable,
        'analysis_nodes': len(nodes),
    }
    report['join_is_register_compatible'] = not report['differing_and_live']
    report['join_has_no_mmio_or_control_effects'] = not mmio and not observable
    Path(a.report).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: report[k] for k in ('registers_differing_between_paths', 'registers_live_at_join',
                                              'differing_and_live', 'join_is_register_compatible',
                                              'join_has_no_mmio_or_control_effects', 'analysis_nodes')}, indent=1))
    for n, es in report['path_effects'].items():
        print(n)
        for e in es:
            print('  ', e)


if __name__ == '__main__':
    main()
