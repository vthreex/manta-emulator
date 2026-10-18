"""Who reads or writes selected AVD XDATA registers? (static, reproducible)

Maps AVD MCU XDATA 11xx to the candidate MIPS byte address
BF206A00 + (xdata&0xFF)//2*4 + (xdata&1) (verified against the original
HTotal 11C7/11C8 -> BF206B8D/BF206B90 and co-channel 11CA -> BF206B94 pairs),
then searches for producers:

* the AVD MCU program itself (MOV DPTR,#11xx followed by MOVX read/write);
* the original MIPS runtime (straight-line constant MMIO plus the 3-byte
  bank/index/value init tables);
* optional related reference drivers (immediate offset matches).

A missing hit is evidence of absence only for the forms searched here; indirect
or computed addressing is not excluded. Nothing is written to the emulator.
"""
import argparse, hashlib, io, json, re, struct, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from firmware_extract import validate, inflate_at, KNOWN  # noqa: E402

BASE = 0x803b0180
DEFAULT_REGS = ['113C', '113D', '11C7', '11C8', '11CA', '11DA', '11DB', '11DC', '11FA', '11FC', '119C']


def mips_address(xdata):
    idx = xdata & 0xff
    return 0xBF206A00 + (idx >> 1) * 4 + (idx & 1)


def avd_accesses(probe, avd_path, size):
    """AVD program accesses to 11xx by three forms: immediate DPTR + MOVX; the
    8-bit write helper 02AA (R7 = low address, R5 = value); and the 16-bit write
    helper 076E (R7 low address, R5 -> xx, R4 -> xx+1). R7 computed from other
    state (table-driven loops) is not resolved here and is counted separately."""
    out = subprocess.run([str(Path(probe).resolve()), str(Path(avd_path).resolve()), '0', str(size)],
                         capture_output=True, text=True, check=True).stdout
    ins = []
    for line in out.splitlines():
        m = re.match(r'([0-9a-f]+) (\S+)\s*(.*)', line)
        if m:
            ins.append((int(m.group(1), 16), m.group(2), m.group(3)))
    found = {}

    def add(addr, kind, pc):
        found.setdefault(addr, {'read': [], 'write': [], 'other': [], 'helper_write8': [], 'helper_write16': []})[kind].append(hex(pc))

    computed_helper_calls = []
    for i, (pc, op, arg) in enumerate(ins):
        m = re.match(r'DPTR, #0?([0-9A-Fa-f]{4})h', arg) if op == 'MOV' else None
        if m and m.group(1).upper().startswith('11'):
            kind = 'other'
            for j in range(i + 1, min(i + 6, len(ins))):
                o, a = ins[j][1], ins[j][2]
                if o == 'MOVX' and a.startswith('A, @DPTR'):
                    kind = 'read'
                    break
                if o == 'MOVX' and a.startswith('@DPTR, A'):
                    kind = 'write'
                    break
                if o in ('LCALL', 'ACALL', 'LJMP', 'AJMP', 'SJMP', 'RET', 'RETI'):
                    break
            add(int(m.group(1), 16), kind, pc)
        if op in ('LCALL', 'ACALL'):
            target = re.search(r'([0-9A-Fa-f]{4})h', arg)
            t = int(target.group(1), 16) if target else None
            if t in (0x02AA, 0x076E, 0x2C63, 0x2C6B):   # 2C63/2C6B are bare tails calling 076E
                reg = None
                for j in range(i - 1, max(i - 5, -1), -1):
                    mm = re.match(r'R7, #([0-9A-Fa-f]{2})h', ins[j][2]) if ins[j][1] == 'MOV' else None
                    if mm:
                        reg = int(mm.group(1), 16)
                        break
                    if ins[j][1] in ('LCALL', 'ACALL', 'RET', 'SJMP', 'LJMP') or re.match(r'R7', ins[j][2]):
                        break
                if t is not None and t != 0x02AA and reg is not None and t != 0x076E:
                    pass
                if reg is None:
                    computed_helper_calls.append(hex(pc))
                elif t == 0x02AA:
                    add(0x1100 + reg, 'helper_write8', pc)
                else:
                    add(0x1100 + reg, 'helper_write16', pc)
                    add(0x1100 + reg + 1, 'helper_write16', pc)
    found['_computed_r7_helper_calls'] = computed_helper_calls
    return found


def runtime_image(flash_path):
    flash = validate(Path(flash_path).read_bytes())
    _, runtime = inflate_at(flash, KNOWN[0][0])
    if hashlib.sha256(runtime).hexdigest() != KNOWN[0][3]:
        raise SystemExit('runtime hash mismatch')
    return flash[0x20080:0x23880] + runtime


def mips_constant_mmio(image):
    from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN  # noqa: F401
    words = struct.unpack('<%dI' % (len(image) // 4), image)
    known = {0: 0}
    hits, reset_after = [], None
    for i, w in enumerate(words[:(0x80613000 - BASE) // 4]):
        pc = BASE + 4 * i
        op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        imm = w & 0xffff
        signed = imm - 65536 if imm & 0x8000 else imm
        val = None
        if op == 15:
            val = imm << 16
        elif op == 13 and rs in known:
            val = known[rs] | imm
        elif op == 9 and rs in known:
            val = (known[rs] + signed) & 0xffffffff
        elif op in (32, 33, 34, 35, 36, 37, 38, 40, 41, 42, 43, 46) and rs in known:
            addr = (known[rs] + signed) & 0xffffffff
            if 0xbf206a00 <= addr < 0xbf206c00:
                hits.append((addr, pc, 'store' if op >= 40 else 'load'))
        if val is not None:
            known[rt] = val
        elif op in (8, 9, 10, 11, 12, 13, 14, 15, 32, 33, 34, 35, 36, 37, 38):
            known.pop(rt, None)
        elif op == 0 and (w & 63) not in (8, 9, 12, 13, 15, 24, 25, 26, 27, 17, 19):
            known.pop((w >> 11) & 31, None)
        if reset_after == pc:
            known = {0: 0}
            reset_after = None
        if op in (1, 2, 3, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 0 and (w & 63) in (8, 9)):
            reset_after = pc + 4
        known[0] = 0
    return hits


def mips_pointer_based(image):
    """Function-level linear tracker for the original HAL accessors.

    A register is `hal-base` after `LW rt,-0x268c(rs)` (the RIU base loaded from
    the HAL context global) until it is overwritten, copied (MOVE/OR/ADDU with
    zero propagates it), or clobbered by a call. State resets after each
    `JR RA` + delay slot. Branch joins are not modeled (state is carried along
    the linear order), which is exact for the straight-line accessors but can
    over-report after a branch that redefines the register; hits are therefore
    reported with the tracking evidence so a reviewer can confirm them.
    LUI-based RAM bases (for example 806F6BB5) are never `hal-base`."""
    words = struct.unpack('<%dI' % (len(image) // 4), image)
    caller_saved = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25}
    hits, base, delay_reset = [], {}, False
    for i, w in enumerate(words[:(0x80613000 - BASE) // 4]):
        pc = BASE + 4 * i
        op, rs, rt, rd, fn = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, w & 63
        imm = w & 0xffff
        if op in (32, 33, 35, 36, 37, 40, 41, 43) and 0x6A00 <= imm <= 0x6BFF and not imm & 2 and base.get(rs):
            hits.append((0xBF200000 + imm, pc, 'store' if op >= 40 else 'load'))
        dest = None
        if op == 35 and imm == 0xd974:
            base[rt] = True
        elif op in (15, 8, 9, 10, 11, 12, 13, 14, 32, 33, 34, 35, 36, 37, 38, 49, 53):
            dest = rt
        elif op == 0 and fn not in (8, 9, 12, 13, 24, 25, 26, 27, 17, 19):
            if fn in (33, 37, 45) and (rt == 0 or rs == 0):   # MOVE / zero-extend copies
                base[rd] = base.get(rs if rt == 0 else rt, False)
            else:
                dest = rd
        elif op == 31:
            dest = rt
        if dest is not None and not (op == 35 and imm == 0xd974):
            base[dest] = False
        if op == 3:
            for r in caller_saved:
                base[r] = False
        if delay_reset:
            base, delay_reset = {}, False
        if op == 0 and fn == 8 and rs == 31:
            delay_reset = True
    return hits


def init_table(image, address=0x8062AE00, size=237):
    t = image[address - BASE:address - BASE + size]
    return [(t[i], t[i + 1], t[i + 2]) for i in range(0, size - 3, 3) if t[i] != 0xff]


def driver_hits(lib_path, offsets):
    sys.path[:0] = [str(HERE.parent.parent.parent / 'work' / 'deps'), str(HERE.parent.parent.parent / 'work' / 'deps' / 'elf')]
    from elftools.elf.elffile import ELFFile
    from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN  # noqa: F401
    data = Path(lib_path).read_bytes()
    pos, hits = 8, {}
    while pos + 60 <= len(data):
        head = data[pos:pos + 60]
        n = int(head[48:58])
        blob = data[pos + 60:pos + 60 + n]
        pos += 60 + n + (n & 1)
        if not blob.startswith(b'\x7fELF'):
            continue
        elf = ELFFile(io.BytesIO(blob))
        syms = elf.get_section_by_name('.symtab')
        if not syms:
            continue
        for sym in syms.iter_symbols():
            if not isinstance(sym['st_shndx'], int) or sym['st_info']['type'] != 'STT_FUNC':
                continue
            code = elf.get_section(sym['st_shndx']).data()[sym['st_value']:sym['st_value'] + sym['st_size']]
            for i in range(0, len(code) - 3, 4):
                low = struct.unpack_from('<I', code, i)[0] & 0xffff
                if low in offsets:
                    hits.setdefault(hex(offsets[low]), set()).add(sym.name)
    return {k: sorted(v) for k, v in hits.items()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware', required=True)
    p.add_argument('--avd', required=True, help='13,312-byte original AVD program')
    p.add_argument('--probe', required=True, help='avd_mcu_probe executable (disassembler)')
    p.add_argument('--driver', action='append', default=[], help='related driver archive (repeatable)')
    p.add_argument('--regs', nargs='*', default=DEFAULT_REGS)
    p.add_argument('--report', required=True)
    a = p.parse_args()
    regs = [int(r, 16) for r in a.regs]
    avd = avd_accesses(a.probe, a.avd, Path(a.avd).stat().st_size)
    image = runtime_image(a.firmware)
    mips = mips_constant_mmio(image) + mips_pointer_based(image)
    table = init_table(image)
    offsets = {mips_address(r) & 0xffff: mips_address(r) for r in regs}
    drivers = {Path(d).name: driver_hits(d, offsets) for d in a.driver}
    report = {'avd_sha256': hashlib.sha256(Path(a.avd).read_bytes()).hexdigest(),
              'runtime_sha256': KNOWN[0][3],
              'mapping': 'BF206A00 + (xdata&0xFF)//2*4 + (xdata&1)', 'registers': {}}
    for r in regs:
        m = mips_address(r)
        report['registers'][hex(r)] = {
            'mips_address': hex(m),
            'avd_mcu_accesses': {k: v for k, v in avd.get(r, {}).items() if v},
            'original_mips_register_accesses': [dict(pc=hex(pc), kind=k) for addr, pc, k in mips if addr == m],
            'original_mips_init_table_entries': [dict(bank=hex(b), index=hex(i), value=hex(v))
                                                 for b, i, v in table if b == 0x35 and i == (r & 0xff)],
            'reference_driver_function_hits': {d: h.get(hex(m), []) for d, h in drivers.items()},
        }
    report['avd_computed_r7_helper_calls'] = avd.get('_computed_r7_helper_calls', [])
    report['caveat'] = ('Absence is limited to immediate-DPTR and immediate-R7 helper (02AA/076E) AVD accesses (computed-R7 helper calls are listed, not resolved), straight-line constant MIPS MMIO, '
                        'the 8062AE00 init table and immediate offsets in the named archives.')
    Path(a.report).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    for r, v in report['registers'].items():
        print(r, v['mips_address'], 'avd:', {k: len(x) for k, x in v['avd_mcu_accesses'].items()},
              'mips:', len(v['original_mips_register_accesses']), 'table:', len(v['original_mips_init_table_entries']),
              'drivers:', {d: len(x) for d, x in v['reference_driver_function_hits'].items()})


if __name__ == '__main__':
    main()
