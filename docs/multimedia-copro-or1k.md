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

## Connected execution (`--exploratory-copro`, opt-in; src/peripherals/copro.hpp)
The original resource-61 image already sits in guest DRAM at physical 0 (the MIPS loader put it there). The model starts the OpenRISC core at the architectural reset
vector 0x100 when the guest writes 3 to the reset-control byte `1F201FE0`; any other value written there holds the core (the guest does this several times and reloads
the image in between - without modelling the hold the reloaded image corrupted the running core). Snapshots do not include the core yet (`--save-state` is refused).

| Piece | Status |
|---|---|
| DRAM, CP phys == MIPS phys | modelled; `0x80000000..0x87FFFFFF` is treated as a cached alias of the same DRAM (**labelled assumption**, consistent with the Input/Output buffer addresses the firmware prints) |
| UART 0x90000000 | text captured into the summary (`exploratory_copro.uart`) |
| RIU window `0xA0200000+off` == MIPS `0x1F200000+off` | mailbox bytes `0x6700..0x67FF` are the SAME storage as the MIPS side |
| CP interrupt controller 0x3290..0x32FF | mask words are plain storage (bit clear = enabled); only source modelled: hw interrupt 113 (status `0x32BC` bit 1) = MIPS->AEON mailbox 0 FIRE, level, PIC line 2 |
| Everything else | stops the core (strict) unless `--exploratory-copro-zero-unknown-reads` (**hypothesis**: unknown reads return 0; counted in the summary). Writes with unknown effect are latched (read back only). `0xA0200A88` (written 4 then 0 after a reply) is only counted - **hypothesis** that it strobes an AEON->host interrupt. |

### What the original coprocessor firmware does in the connected boot
- Boots, prints `MIU1 BaseAddress: 8000000` / `MM TaskEntry`, runs the original ImageDecoder init: it checks the `0x5AA55AA5`-style version block published by the MIPS side
  at `0x008AC0F0` (with the DRAM alias the check passes) and prints the shared-memory layout (`Input Mem 0x83230000`, `Output Mem 0x827AA000`, ...) and `STANDBY --> INITED`.
- The MIPS side fires mailbox 0 (class 0x2B, command 0x12, no parameters) about every 590k instructions. The coprocessor consumes it (clears FIRE) and replied exactly once
  (class 0x2B, command 0x82, no parameters) - **the reply is never acknowledged by the MIPS**, so mailbox 1 stays full and no further reply is possible.
- Diagnosis: the MIPS-side MBX driver (`_MDrv_MBX_MsgRecvCb` at `805DA4C4`, same logical interrupt numbers 35..40 -> hardware 106,102,100,114,113,104) is interrupt driven. The emulator
  has no MIPS external interrupt source (only the CP0 Count/Compare timer; the patched Unicorn cannot raise IP2/IP3), and no model of the MIPS interrupt controller
  (RIU `0x33D0/33D8/3390/3398` masks, `0x33F0/33F8` IRQ status, `0x33B0..` FIQ status; dispatcher `8060A844`/`8060AA88`; guest exception entry `803B0180` -> `80601AF4`,
  IP2 handler slot `806F70A8`, IP3 `806F70B0`).
- Next step: (1) add a deterministic external-interrupt register to the Unicorn extension (set Cause.IP2/IP3); (2) model the MIPS INTC status/mask words with the
  mailbox-reply source (hardware index for CP->MIPS mail is 106 or 114 - decide by bracketing); (3) then watch whether the MIPS acknowledges and the decoder receives
  its next command (the logo JPEG at `0x03230000` is already in the Input buffer region).
