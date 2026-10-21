# Multimedia coprocessor (OpenRISC) — standalone execution and interrupt/mailbox findings

Evidence only. Nothing here is wired into the connected MIPS boot yet.

## Tools
- `src/cpu/or1k.hpp`: ORBIS32 interpreter (little-endian words, delay slots, tick timer, PIC SPRs 0x4800/0x4802, exceptions 0x500/0x600/0x700/0x800/0xC00/0xE00).
- `tools/copro_run.cpp` (`copro_run IMAGE.bin ...`): runs resource 61 on a flat 128 MiB DRAM. Unmodelled reads throw unless `--auto-zero` (exploratory, listed at the end)
  or a labelled `--hyp ADDR=VAL` is given. `--poke N ADDR VAL LINE` (exploratory stimulus) sets a 16-bit RIU window value and raises PIC line LINE at instruction N.
- `tools/or1k_dis.py IMAGE.bin dis|xref|str`.

## What the original firmware does (from disassembly + harness)
- With `--auto-zero` it boots to the MM task loop (UART: `MIU1 BaseAddress: 8000000`, `MM TaskEntry`) and idles in `msleep(1)` (0x806BC..0x80728).
- External interrupt vector 0x800 -> dispatcher 0x5EA8: `ff1(PICSR)` indexes a table at 0x131FA0 (8 bytes/entry: handler, arg). Entry 3 (PIC line 2) = 0xC57AC,
  entry 4 (PIC line 3) = 0xC5620. Both read two 32-bit secondary status words (CP RIU half-word index `i` -> address `0xA0200000 + i*2`, low half then `+4`):
  line 2: 0x1958 (0xA02032B0) then 0x195C (0xA02032B8) -> table 0x14572C index `95+ff1`;  line 3: 0x1978 (0xA02032F0) then 0x197C (0xA02032F8) -> index `63+ff1` (second bank).
  Masks are at 0x1948 / 0x1968 (0xA0203290 / 0xA02032D0). Handlers clear the status by writing it back.
- Logical interrupt numbers 35..40 (MBX roles) map through byte table 0x12F001 to hardware indexes 106,102,100,114,113,104. Role 1 (the AEON itself) attaches
  trampoline 0xC79E0 -> `_MDrv_MBX_MsgRecvCb` (0xC63F4) for hw index 102 (int 36) and 113 (int 39).
- Hardware index 102 = status word 0x195C bit 6; 113 = 0x195C bit 17. Poking either with PIC line 2 reaches `MsgRecvCb` and the mail read routine 0xC7540 (verified by PC trace).
  Which of the two is the MIPS->AEON "fire" is NOT yet decided; the MBX debug prints are gated off, so there is no UART text for it.
- Mail read (0xC7540): control byte at mailbox base `0xA0200000 + (0x19C0 + n)*4` (n from the role table at 0x13012E); requires ctrl bit0 (FIRE) set, sets byte+0x1D bit7,
  copies bytes 0,1,4,5 then ten parameter bytes (every second byte from +8) and status bytes +0x1C/+0x1D. Same layout as the MIPS sender at 0x805DB6E8.
