# Original AVD ROM data with unknown startup configuration

This continues `a2345bd`. **There is still no authentic OSD frame.**
The connected original AVD now executes 863 instructions, up from 379,
without supplying a startup selector. Its original writer defines `119C=0`.
It stops at `0702`, requiring native `11DB` bit 7. Main MIPS still rejects
unknown HTotal at `80501BFC`; ZUI dispatch is unentered.

## What changed

`--diagnostic-avd-symbolic-code-tables` adds a conservative data abstraction
to the existing 8051 interpreter wrapper. It requires guarded shared AVD
execution. It supplies no input value, patches no instructions and changes
no program counter. The existing CPU engine executes every instruction.
The standalone probe supports the corresponding `--symbolic-code-tables`.
Previous execution modes retain their earlier boundaries.

Original `1C22` forms CODE-table addresses from captured, unknown `119C`.
ADD/ADDC and MUL now retain only result and flag bits common to every possible
operand. DPL/DPH/B copies retain uncertainty. MOVC checks **every** address
represented by those masks against the actual uploaded CODE image, then marks
only common result bits known. Byte correlations are deliberately forgotten;
the approximation can lose information, but cannot create known bits. A lookup
with any unwritten possible byte or an excessive address set stops.

This capability applies to ROM **data** reads. An uncertain XDATA address,
instruction target, required branch, carry, arithmetic flag or unimplemented
SFR operation still stops. All additional masks enter diagnostic fingerprints
and roll back on a failed instruction. Neither firmware image is modified.

Original `1C55..1C63` writes table-derived bytes to `117C..117F`. In this mode
these explicit configuration destinations may receive INVALID data. Their known
masks are `00 FF 00 00`; this is not a configured signal measurement. Unknown
unrelated destinations, commands and HTotal publications remain rejected.
A separate exact-origin copy from private `8077` to `11D6` can retain an INVALID
timer diagnostic byte; it does not produce a timer tick or initialize RAM.

## Original producer reached

The uncertain table computation no longer stops the CPU merely for reading
immutable ROM data. Execution reaches:

```
1B27 CLR A
1B28 MOV R5,A
1B29 MOV R7,#9C
1B2B LCALL 02AA
02AD MOVX @DPTR,A       ; native 119C = 0, known FF
```

Under the existing candidate shared mapping this is MIPS `BF206B38`.
The previous MIPS writer `80503650`, identified as a related driver's
Hsync-sensitivity setter, still has not executed. The initial selector's
physical reset value and precise wiring are not established. It is not needed
as a concrete value to demonstrate this longer prefix; three earlier table
configuration bytes remain unknown rather than being guessed.

## Next actual dependency: `11DB.7`

Original `06FE..0702` reads `11DB` (candidate MIPS `BF206BB5`) and branches on
bit 7. If clear, it skips `0705..0716`. If set, that block multiplies a
ROM-derived coefficient by 17 with original `003B`, shifts the 16-bit product
right four through original `0F48`, and replaces IRAM `63:64`. Thus the
software role is an optional coefficient adjustment, not a proven ready/idle
acknowledgement. The full hardware register name, producer and reset remain
unknown. Neither the executed 78-entry MIPS initialization table nor the
observed connected writes initializes this bit. No value is assigned to it.

The probe-only `--synthetic-11db-bit7 0|1` explores both possibilities from
the unmodified reset vector, with all other missing inputs invalid. These
fixtures **are unavailable in the TV executable**. They execute 2,278 and
2,309 instructions respectively, enable IE.7 at `0284`, return from startup,
and call the actual main-loop routine at `0292 -> 098D`. Both stop at `0993`,
requiring `11FC.2`. They do not establish connected TV progress, valid hardware
state, successful interrupts, HTotal or graphics. Private `8077` stays invalid
when copied to `11D6`; no artificial zero counter is supplied.

## HTotal publication needs an event as well as a sample

The original `098D` function reads native `11FC` into IRAM `29`.
`0993` tests its bit 2. At `099B..09A1` the firmware complements the captured
byte and writes it back through original `02AA`; this resembles an event
acknowledgement, but its silicon clearing semantics are not proven or modeled
as write-zero-to-clear. `09B6` tests captured bit 0 and skips the measurement
path if clear. Startup writes `11FA=1` at `027E..0282`; its exact mask/source
semantics still require evidence.

After the gated work, `0AA9..0ABC` reads `113C/113D`, masks the high byte with
`07`, and invokes original `076E` to publish through actual MOVX instructions
`0775/0784` to `11C7/11C8`. Candidate aliases are logical `35C7/35C8`, physical
`1F206B8D/1F206B90`. MIPS `805030F8/805030FC` reads high then low and assembles
the word. These are AVD input-domain values, not panel output timing.
The MCU stores low then high; MIPS reads high then low. No undocumented atomic
latch or coherent-sample guarantee is assumed for this two-byte transfer.

The standard Timer0 ISR `082E..084C` only decrements IRAM `78`, reloads TL0/TH0
and increments private `8077`. It does not produce `11FC`, IRAM `29.0`, or the
raw HTotal bytes. Merely enabling that ISR cannot establish HTotal publication.
Event sources, latch/reset behavior, counter clock and absent-HSYNC behavior
remain missing. The publisher has not executed in connected boot or these
isolated fixtures. No valid zero or convenient horizontal period is supplied.

## Main application and graphics

The new cold run has the same complete MIPS coverage as the previous reset-latch
experiment. It reaches 854,795,171 dispatches and stops at `80501BFC` comparing
the current invalid sample with previous history. Both equality outcomes lead
back to the same finite continuation at `80501758`, with different stability
counter effects; this comparison is not itself an infinite wait for HTotal.
Its consumed value is still needed and remains invalid.

The application loop calls platform polling at `8048885C -> 8046CA74` before
the UI wrapper at `80488864 -> 8047602C`. The latter contains the dispatcher
call `8047606C -> 804E0DC4`. The polling path has not returned. Original ZUI
initialization `804E0D48` and its return `804E0DBC` remain covered, while the
wrapper and dispatcher are unentered. No keypress or UI event is synthesized.

No actual framebuffer allocation, enabled GWIN, original raster drawing or
authentic pixels are demonstrated. The disabled 8x8 BIST buffer and compressed
startup JPEG retain their earlier meanings. No PNG is exported.

| Connected run | Result |
|---|---|
| Main MIPS | 854,795,171; `80501BFC`, HTotal comparison |
| TSP OpenRISC | 2,453,934,820 original instructions |
| DVB-T | 1,454,515; 161 interrupts; `4AE0 / XDATA 2200` |
| PM51 | 3,474; no interrupts; `574F / XDATA 0E6A` |
| AVD | 863; no interrupts; `0702 / XDATA 11DB bit7` |
| Audio DSP | zero original instructions |
| Main timer entries | 38,589 |
| UART | byte-identical to previous checkpoint |

The shared bus, deferred startup effects, reset-empty request channel, clock
ratio, earlier PM/input/VIF/GOP hypotheses and all strict uncertainties remain
explicit. Default AVD diagnostic execution still stops at `0802/C008` after six
instructions. Strict mode gains no reset value, measurement or response.

## Validation and evidence

1,435 native checks cover arithmetic, ROM alternatives, unwritten CODE, invalid
address/control uses, SFR/PSW leakage and the narrow data-copy destinations.
The original-program differential suite compares every known symbolic store
bit against all 256 concrete selector fixtures and 256 alternate unknown-byte
representatives. Exact isolated instruction/write/state replay also passes.
Both synthetic `11DB.7` outcomes are preserved separately from connected boot.

All 18 baseline runs, nine display runs, auxiliary/VIF/request/clock/DSP-decoder
and immutable-input regressions, four forensics tests and both CTest entries
pass. The Windows package passes native checks and window creation with only
System32 on PATH. A complete fresh-reset replay of the new 6,592,734-event
path matches every event, full summary, MIPS coverage and PM history. The
previous 6,592,234-event path also replays identically with the final binary.
Replay compares new execution; it never supplies recorded MMIO read values.
All 78 original host initialization-table writes match the new cold trace.

The recording binary predates the final stricter SFR/PSW guards and narrow
invalid timer-byte copy. Neither changes this connected prefix, as the final
binary's complete replay proves. Both executable hashes are preserved.

The validation manifest and complete replay reports are preserved alongside
the original AVD trace and analysis in
[`avd-symbolic-tables-validation.json`](../evidence/avd-symbolic-tables-validation.json).
The original firmware hash remains
`d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9`.
