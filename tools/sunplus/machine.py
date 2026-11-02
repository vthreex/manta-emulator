"""Sunplus SPHE8600 (MIPS32 LE, eCos) bring-up machine for the Cultraview CV9202 firmware (Shivaki STV-24LED14 / Manta LED4202).

Not a stage-1 boot-ROM path: the decompressed kernel blob is placed at physical 0x400 (derived: the application image then starts at
0x400 + kernel size, and every call inside the image lands on a function entry) and entered at 0x80000400.  That hand-off is a
LABELLED EXPLORATORY SHORTCUT.

Strict by default: the SoC register window (0x1FFE0000..0x1FFFFFFF) is unknown hardware.  The only modelled device is what the
firmware's own code polls: UART TX (0xBFFE0900 data, 0xBFFE0904 status = 0x41).  Every other READ stops the run with a report, unless
a *hypothesis* supplies a value:
  --hyp FILE     lines "ADDR VALUE [MASK] # why"  (physical or KSEG1 address); each use is counted and reported as a hypothesis
  --readback     (hypothesis) registers read back the last value the firmware wrote to them
  --unknown-zero (EXPLORATORY) any other register READ returns 0; the report lists every address that was read this way
Nothing here reads real hardware; hypotheses are bracketed, labelled and listed in the run report.

Usage: PYTHONPATH=work/deps python machine.py KERNEL.bin [--hyp FILE] [--readback] [--max-insns N] [--ram-mb N]
"""
import argparse
import collections
import sys
from unicorn import (Uc, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_LITTLE_ENDIAN, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE,
                     UC_HOOK_BLOCK, UC_HOOK_INTR, UcError)
from unicorn.mips_const import *
import capstone

REG_BASE = 0x1FFE0000
REG_SIZE = 0x20000
UART_DATA = REG_BASE + 0x0900
UART_STAT = REG_BASE + 0x0904


class Stop(Exception):
    pass


def parse_hyp(path):
    h = {}
    for line in open(path, encoding='utf-8'):
        note = ''
        if '#' in line:
            line, note = line.split('#', 1)
        f = line.split()
        if not f:
            continue
        addr = int(f[0], 0) & 0x1FFFFFFF
        val = int(f[1], 0)
        mask = int(f[2], 0) if len(f) > 2 else 0xFFFFFFFF
        h[addr] = (val, mask, note.strip())
    return h


class Machine:
    def __init__(self, kernel, ram_mb=128, load=0x400, hyp=None, readback=False, unknown_zero=False, poll_limit=300000, map_iop=False):
        self.uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 + UC_MODE_LITTLE_ENDIAN)
        # The firmware executes MIPS32r2 instructions (di/ei/ehb/ext): use a r2 core model.  The real core is unknown (EXPLORATORY: 24Kc).
        self.uc.ctl_set_cpu_model(UC_CPU_MIPS32_24KC)
        self.load = load
        ram = ram_mb << 20
        self.uc.mem_map(0, ram)
        self.uc.mem_write(load, kernel[:ram - load])
        if map_iop:                                # EXPLORATORY: 0x1FFC0000..0x1FFDFFFF is read by 'Get IOP Data' (memcpy 0x200 bytes); contents unknown -> zeros
            self.uc.mem_map(0x1FFC0000, 0x20000)
        self.uart = bytearray()
        self.written = {}                         # addr -> last value (kept for --readback and for the report)
        self.writes = collections.OrderedDict()   # (addr, value) -> (pc, count)
        self.hyp = hyp or {}
        self.hyp_used = collections.Counter()
        self.readback = readback
        self.reads_ok = collections.Counter()
        self.blocks = 0
        self.stop_reason = ''
        self.unknown_zero = unknown_zero          # EXPLORATORY: unmodelled reads return 0 (each distinct address is listed in the report)
        self.zero_reads = collections.OrderedDict()   # addr -> [first_pc, count]
        self.poll_limit = poll_limit
        self.max_blocks = 0
        self.ring = collections.deque(maxlen=64)
        self.md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 + capstone.CS_MODE_LITTLE_ENDIAN)
        self.uc.mmio_map(REG_BASE, REG_SIZE, self.on_read, None, self.on_write, None)
        self.uc.hook_add(UC_HOOK_BLOCK, self.on_block)
        self.uc.hook_add(UC_HOOK_INTR, self.on_intr)
        # Reset leaves Status.ERL=1/BEV=1; the boot ROM (not in the dump) hands over with them cleared.  EXPLORATORY: Status = 0 at entry.
        self.uc.reg_write(UC_MIPS_REG_CP0_STATUS, 0)
        self.uc.reg_write(UC_MIPS_REG_S6, 0xBFFE8000)   # stage-1 stub: lui s6,0xbffe ; ori s6,s6,0x8000

    # ---- devices -------------------------------------------------------------------------------------------------
    # The register window is an mmio_map (callbacks), NOT memory hooks: in this Unicorn a memory-write hook on a store in a taken branch's
    # delay slot raises a spurious reserved-instruction exception (reproduced in isolation); callbacks do not.
    def _reg_value(self, off, size):
        """Value the firmware reads at register offset `off` (strict: only modelled devices and labelled hypotheses)."""
        base = off & ~3
        pc = self.ring[-1] if self.ring else 0
        addr = REG_BASE + base
        if addr == UART_STAT:
            val = 0x41                                          # bit0 TX ready + bit6 TX idle (modelled from the firmware's own polls)
        elif addr in self.hyp:
            v, mask, _note = self.hyp[addr]
            val = v & mask
            self.hyp_used[addr] += 1
        elif self.readback and base in self.written:
            val = self.written[base]
            self.hyp_used['readback'] += 1
        elif self.unknown_zero:
            ent = self.zero_reads.setdefault(base, [pc, 0])
            ent[1] += 1
            val = 0
            if ent[1] >= self.poll_limit:
                self.stop_reason = 'stuck polling %08x (%d reads of the unknown-zero value), first read near block %08x, now block %08x' % (addr | 0xA0000000, ent[1], ent[0], pc)
                raise Stop(self.stop_reason)
        else:
            self.stop_reason = 'unmodelled register READ %08x (size %d) near block %08x' % (addr | 0xA0000000, size, pc)
            raise Stop(self.stop_reason)
        sh = (off & 3) * 8
        return (val >> sh) & ((1 << (8 * size)) - 1)

    def on_read(self, uc, offset, size, user):
        return self._reg_value(offset, size)

    def on_write(self, uc, offset, size, value, user):
        pc = self.ring[-1] if self.ring else 0
        address = REG_BASE + offset
        if address == UART_DATA:
            self.uart.append(value & 0xff)
            return
        v = value & ((1 << (8 * size)) - 1)
        base = offset & ~3
        if size == 4:
            self.written[base] = v
        else:
            old = self.written.get(base, 0)
            sh = (offset & 3) * 8
            self.written[base] = (old & ~(((1 << (8 * size)) - 1) << sh)) | (v << sh)
        k = (address, v)
        if k not in self.writes:
            self.writes[k] = [pc, 1]
        else:
            self.writes[k][1] += 1

    def on_intr(self, uc, intno, user):
        pc = uc.reg_read(UC_MIPS_REG_PC)
        cp0 = {}
        for n in ('CP0_STATUS', 'CP0_CAUSE', 'CP0_EPC', 'CP0_BADVADDR'):
            try:
                cp0[n] = uc.reg_read(getattr(__import__('unicorn.mips_const', fromlist=['x']), 'UC_MIPS_REG_' + n))
            except Exception:
                pass
        self.stop_reason = 'CPU exception intno=%d at pc %08x cp0=%s' % (intno, pc, {k: '%08x' % v for k, v in cp0.items()})
        self.exc = (intno, pc)
        raise Stop(self.stop_reason)

    def on_block(self, uc, address, size, user):
        self.ring.append(address)
        self.blocks += 1
        if self.max_blocks and self.blocks >= self.max_blocks:
            self.stop_reason = 'block budget'
            raise Stop(self.stop_reason)
        if address < 0x400 and self.blocks > 10:
            self.stop_reason = 'execution at low address %08x' % address
            raise Stop(self.stop_reason)

    # ---- run -----------------------------------------------------------------------------------------------------
    def run(self, max_blocks):
        self.max_blocks = max_blocks
        why = ''
        try:
            self.uc.emu_start(0x80000000 + self.load, 0xFFFFFFFF)
            why = 'ended'
        except Stop as e:
            why = str(e)
        except UcError as e:
            why = self.stop_reason or 'unicorn: %s' % e
        return why

    def disasm_around(self, pc, before=6, after=8):
        out = []
        start = pc - 4 * before
        try:
            code = bytes(self.uc.mem_read(start & 0x1FFFFFFF, 4 * (before + after)))
        except UcError:
            return out
        for i in self.md.disasm(code, start):
            out.append('%s %08x  %-7s %s' % ('=>' if i.address == pc else '  ', i.address, i.mnemonic, i.op_str))
        return out

    def report(self, why, nwrites=60):
        uc = self.uc
        pc = uc.reg_read(UC_MIPS_REG_PC)
        print('stop: %s' % why)
        print('pc=%08x  blocks=%d' % (pc, self.blocks))
        print('UART: %r' % self.uart.decode('latin1')[-600:])
        print('regs: ' + ' '.join('%s=%08x' % (n, uc.reg_read(r)) for n, r in (
            ('ra', UC_MIPS_REG_RA), ('sp', UC_MIPS_REG_SP), ('v0', UC_MIPS_REG_V0), ('v1', UC_MIPS_REG_V1), ('a0', UC_MIPS_REG_A0), ('a1', UC_MIPS_REG_A1),
            ('a2', UC_MIPS_REG_A2), ('s0', UC_MIPS_REG_S0), ('s1', UC_MIPS_REG_S1), ('s6', UC_MIPS_REG_S6), ('gp', UC_MIPS_REG_GP))))
        print('last blocks: ' + ' '.join('%08x' % x for x in list(self.ring)[-16:]))
        print('code:')
        for l in self.disasm_around(pc):
            print(l)
        print('hypotheses used: %s' % (dict((('%08x' % k if isinstance(k, int) else k), v) for k, v in self.hyp_used.items()) or 'none'))
        for k, (val, mask, note) in sorted(self.hyp.items()):
            print('  hyp %08x = %08x  %s' % (k | 0xA0000000, val, note))
        if self.zero_reads:
            print('EXPLORATORY unknown-zero reads (%d distinct registers):' % len(self.zero_reads))
            for ad, (fpc, cnt) in self.zero_reads.items():
                print('  %08x read %d times, first at pc %08x' % (ad | 0xA0000000, cnt, fpc))
        print('distinct register writes: %d (first %d):' % (len(self.writes), nwrites))
        for (ad, v), (wpc, cnt) in list(self.writes.items())[:nwrites]:
            print('  %08x: %08x <= %08x%s' % (wpc, ad | 0xA0000000, v, ('  x%d' % cnt) if cnt > 1 else ''))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('kernel')
    ap.add_argument('--hyp')
    ap.add_argument('--readback', action='store_true')
    ap.add_argument('--unknown-zero', action='store_true', help='EXPLORATORY: unmodelled register reads return 0 (listed in the report)')
    ap.add_argument('--map-iop', action='store_true', help='EXPLORATORY: map 0x1FFC0000..0x1FFDFFFF (IOP data window) as zero RAM')
    ap.add_argument('--poll-limit', type=int, default=300000)
    ap.add_argument('--max-insns', type=int, default=50_000_000, help='budget in translated blocks (about 1/5 of instructions)')
    ap.add_argument('--ram-mb', type=int, default=128)
    ap.add_argument('--load', type=lambda s: int(s, 0), default=0x400)
    ap.add_argument('--writes', type=int, default=60)
    a = ap.parse_args()
    m = Machine(open(a.kernel, 'rb').read(), a.ram_mb, a.load, parse_hyp(a.hyp) if a.hyp else None, a.readback, a.unknown_zero, a.poll_limit, a.map_iop)
    why = m.run(a.max_insns)
    m.report(why, a.writes)
    return 0


if __name__ == '__main__':
    sys.exit(main())
