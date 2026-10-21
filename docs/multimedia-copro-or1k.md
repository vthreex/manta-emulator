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

## MIPS interrupt delivery works (`--exploratory-copro-mips-irq 104`, IRQ-capable engine)
- `tools/patch_unicorn_irq.py` adds `UC_MIPS_REG_CP0_IRQ` (Cause.IP2..IP6 level mask) to the timer-patched Unicorn 2.1.4; the build lives in `work/unicorn-irq*`
  (separate from the working engine) and `work/build-irq` links it. Without the extension `--exploratory-copro-mips-irq` refuses to run.
- FIQ model (EXPLORATORY, `Riu::fiqModel`): pending bits for banks 32..63 (status `1F2033B0/B4`) and 96..127 (`1F2033B8/BC`), write-1-to-clear (the original FIQ
  dispatcher `8060AA88` writes the status back, then 0), masks are the guest's own `1F203390/94/98/9C` words (bit set = masked). Output: Cause.IP3.
- The MIPS unmasked only hardware interrupts **100 and 104** (mask words after boot: bank 3 low half `0xFEEF`), not 106/114 as the role table suggested. Raising **104** when
  the coprocessor fires its reply mailbox makes the original MIPS MBX driver read and acknowledge the reply (2 raises -> 9, 36 status reads/writes).
- Result: the MIPS then sends the still-image decode request (class 0x2B, command 0x10, 4 parameters) and later class 0x2C commands; the original coprocessor decoder
  parses the boot-logo JPEG from the Input buffer: `[Still Image] Decoder, JPEG, size: 4147200`, `Image WxH: 1940x1080`, `Scaled WxH: 970(976)x540`, `Scale factor: 2`,
  `INITED --> BEFORE WORKING --> WORKING`, `Decode done`, then `WORKING --> STOPPED, error: 0x0001`. No JPD hardware is modelled, so "decode done" is NOT a decoded picture:
  the unknown JPD registers read 0 under `--exploratory-copro-zero-unknown-reads` and the Output buffer (`0x827AA000`) has not been checked.
- New MIPS stop: `805E6268` reads RIU `0x1F206504` (a save/restore of the register group `0x6500/6504/6514/65B8/65F4`, unmodelled). The opaque-read-policy mechanism
  (exact code hash, INVALID values only) is the intended way past a pure save/restore; not done yet.
- 1940x1080: the JPEG header really is 1940x1080 (printed by the original decoder).

## What the decoder touches (summary `exploratory_copro.io_log`, run irq6)
Peripheral window accesses other than mailbox/INTC (address, reads, writes, last value, first pc): JPD block `0xA0202E00..2E70` (config/status words; reads of `2E04` x4,
`2E00`, `2E28`, `2E60`; writes of tables/addresses), `0xA0202F00/2F04` (index + data port: **528 half-word writes** - the HAL's `HAL_JPD_LoadCode` / "Send CMD to JPD firmware" path,
i.e. the JPD contains a small processor that runs its own loaded firmware), `0xA02016D4`, `0xA0200D0D`, `0xA020250D` (clock/power-style bytes), `0xA0201250/54`, `0xA0201FD4/FDC`.
With every unknown read returning 0 the decoder's wait loops fall through ("Decode done" then error 0x0001), so the result is not a picture.
Open: the JPD register semantics and the loaded JPD firmware's ISA are not documented; no public MStar `halJPD` source exists (searched). Needs reverse engineering of the CP's HAL_JPD
functions (`0xBD6xx..0xBEAxx`) before a faithful JPD model is possible.

The MIPS side, once the decoder reply arrives, continues into the ADC auto-calibration at `805E6238` (RIU bank `0x6500..65F5`, polls `65F5` bit 0 in a 0x28-iteration tuning loop) - a
separate unmodelled hardware block, unrelated to the logo decode.

## Past the ADC calibration: the logo's GE stretch (run z2, `--diagnostic-riu-zero-unknown-reads`, DIAGNOSTIC ONLY)
- The ADC auto-calibration (`805E6238`, RIU `0x6500..65F5`) is passed with labelled reset-value hypotheses (`--exploratory-mmio-bits 1f2065xx:ff:00+init`; status bit `65F5.0` bracketed 0/1 - identical result);
  the next unknown was the scaler read `1F205E34` (bank 0x10). `--diagnostic-riu-zero-unknown-reads` (opt-in, invalidates strict evidence) lets never-defined RIU registers read 0 and lists them
  (z2: 18 distinct addresses, 178,047 reads, almost all `1F205E78` = the known sync-status accessor).
- With that the MIPS reaches (dispatch 498,596,264) the guarded **GE stretch** command `0x8040` and stops there ("GE BitBlt stretch/filter/mode unsupported"). Decoded request:
  source `0x027AA000` (= the coprocessor's *Output Mem* `0x827AA000`), pitch 1952, srcRect 970x535 (the decoder's scaled 970x540 logo), format 0xE -> format 0xE, destination `0x02D00000`
  pitch 3840 (1920 px x 2 bytes), destRect (0,6)-(1919,1073). I.e. the original DISPLAY_LOGO path is: JPEG decoded by the JPD into the Output buffer -> GE stretch (x~1.98) into a 1920-wide
  16-bit framebuffer -> GOP. The 1940x1080 JPEG becomes 970x540 (scale-down factor 2) and is then stretched back up with a 6-row top/bottom margin.
- Missing for pixels: (1) JPD output (the Output buffer is all zeros without a JPD model), (2) GE stretch for format 0xE (probably packed YUV422; filter/phase unresolved, see ge-8040-register-spec.md),
  (3) GOP scan-out of that format.
