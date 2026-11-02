"""First-light harness for the Sunplus SPHE8600 (MIPS32 LE, eCos) kernel of the Shivaki STV-24LED14 image.

Not the stage-1 boot ROM path: the decompressed kernel (flash LZMA stream at 0x54070) is placed at physical 0 and entered
at 0x80000000 with $s6 = 0xBFFE8000, exactly as the stage-1 stub at flash 0x3030 sets it.  That hand-off is a LABELLED
EXPLORATORY SHORTCUT (stage 1 itself is not run yet).

Hardware is modelled only where the firmware's own code demands it:
  * UART data  0xBFFE0900 (TX, logged), status 0xBFFE0904 = 0x41: bit0 'TX ready' polled before every character, bit6 'TX idle' polled after the newline.
Every other register access in 0x1FFE0000..0x1FFFFFFF is recorded; the first READ that is not modelled stops the run
(strict: unknown hardware stays unknown, nothing is invented).

Usage: PYTHONPATH=work/deps python firstlight.py KERNEL.bin [--max-insns N] [--ram-mb N]
"""
import argparse
import collections
import sys
from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_LITTLE_ENDIAN, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_UNMAPPED, UC_HOOK_CODE, UcError
from unicorn.mips_const import *

REG_BASE = 0x1FFE0000
REG_SIZE = 0x20000
UART_DATA = REG_BASE + 0x0900
UART_STAT = REG_BASE + 0x0904


class Stop(Exception):
    pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('kernel')
    ap.add_argument('--max-insns', type=int, default=20_000_000)
    ap.add_argument('--ram-mb', type=int, default=128)
    ap.add_argument('--log-writes', type=int, default=40, help='how many distinct register writes to print')
    a = ap.parse_args()

    kernel = open(a.kernel, 'rb').read()
    uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 + UC_MODE_LITTLE_ENDIAN)
    ram = a.ram_mb << 20
    uc.mem_map(0, ram)
    uc.mem_write(0, kernel[:ram])
    regs = bytearray(REG_SIZE)           # backing store only for registers whose written value is read back is NOT assumed
    uc.mem_map(REG_BASE, REG_SIZE)       # physical alias; accesses are intercepted by the hooks below

    uart = bytearray()
    writes = []                           # (pc, addr, size, value)
    seen_w = set()
    state = {'insns': 0}
    ring = collections.deque(maxlen=48)

    def on_read(uc, access, address, size, value, user):
        pc = uc.reg_read(UC_MIPS_REG_PC)
        if address == UART_STAT:
            uc.mem_write(address, (0x41).to_bytes(4, 'little'))   # bit0 TX ready + bit6 TX idle (modelled from the firmware's own polls)
            return
        if not (REG_BASE <= address < REG_BASE + REG_SIZE):
            return
        print('STOP unmodelled register READ  %08x size %d  at pc %08x' % (address, size, pc))
        raise Stop('read %08x' % address)

    def on_write(uc, access, address, size, value, user):
        pc = uc.reg_read(UC_MIPS_REG_PC)
        if address == UART_DATA:
            uart.append(value & 0xff)
            return
        k = (address, value & 0xffffffff)
        if k not in seen_w:
            seen_w.add(k)
            writes.append((pc, address, size, value & 0xffffffff))

    def on_code(uc, address, size, user):
        state['insns'] += 1
        ring.append(address)
        if address < 0x1000 and state['insns'] > 100:     # KUSEG page 0: real hardware would fault here, the RAM alias at 0 would hide it
            raise Stop('execution at low address %08x' % address)
        if state['insns'] >= a.max_insns:
            raise Stop('instruction budget')

    uc.hook_add(UC_HOOK_MEM_READ, on_read, begin=REG_BASE, end=REG_BASE + REG_SIZE - 1)
    uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=REG_BASE, end=REG_BASE + REG_SIZE - 1)
    uc.hook_add(UC_HOOK_CODE, on_code)

    uc.reg_write(UC_MIPS_REG_S6, 0xBFFE8000)        # stage-1 stub: lui s6,0xbffe ; ori s6,s6,0x8000
    why = ''
    try:
        uc.emu_start(0x80000000, 0xFFFFFFFF)
        why = 'ended'
    except Stop as e:
        why = str(e)
    except UcError as e:
        why = 'unicorn: %s' % e
    pc = uc.reg_read(UC_MIPS_REG_PC)
    print('\nstop: %s   pc=%08x   insns=%d' % (why, pc, state['insns']))
    print('UART: %r' % bytes(uart).decode('latin1')[:400])
    print('last pcs: ' + ' '.join('%08x' % x for x in list(ring)[-32:]))
    print('regs: ' + ' '.join('%s=%08x' % (n, uc.reg_read(r)) for n, r in (('ra', UC_MIPS_REG_RA), ('sp', UC_MIPS_REG_SP), ('v0', UC_MIPS_REG_V0), ('t9', UC_MIPS_REG_T9), ('a0', UC_MIPS_REG_A0), ('gp', UC_MIPS_REG_GP))))
    print('\nfirst %d distinct register writes (pc: addr <= value):' % a.log_writes)
    for pc_, ad, sz, v in writes[:a.log_writes]:
        print('  %08x: %08x <= %08x (%d)' % (pc_, ad | 0xA0000000, v, sz))
    print('  (%d distinct writes total)' % len(writes))
    return 0


if __name__ == '__main__':
    sys.exit(main())
