# Original AVD shared-register execution

This continues `b67392d`. The original main firmware still stops at
`805032A0 / BF206BB8`. **No authentic OSD frame exists.** The new result is
379 original AVD instructions, seven actual shared-register stores and sixteen
actual private-RAM stores in an explicitly conditional diagnostic. Default
AVD startup still stops after six instructions at `0802 / C008`.

## Host-assisted register protocol

Original `8050328C..8050338B` matches the complete 256-byte related
[`HAL_AVD_AFEC_SetRegFromDSP`](https://github.com/Toysoft/mstar/blob/5d447aa5b54b60ede27d5fda42d481efa4b10ff8/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libdrvAVD.a).
Only global-address relocations at offsets 0, 4 and 16 differ. The caller at
`805016C8` calls it at `80501704`, with return address `8050170C`.
`805032A0` is LBU of the command byte. This is a nonblocking service poll,
not a DSP-alive acknowledgement or a wait for an RF measurement.

| MIPS byte | Candidate AVD XDATA | Role |
|---|---|---|
| `BF206BB8` | `11DC` | command; bits 2..3 select operation |
| `BF206BB9` | `11DD` | register mask |
| `BF206BBC` | `11DE` | register index |
| `BF206BBD` | `11DF` | data |

Command `& 0C == 08` requests a masked write. Main MIPS performs
`(old & ~mask) | (data & mask)` and clears command bits `0C`. Command
`& 0C == 04` requests a read: MIPS puts `register & mask` in the data byte
and sets bits `0C`. Values `00` and `0C` return without a transaction.
Command low two bits select bank: zero chooses logical `3600`; any nonzero
value chooses `3500`. The original code converts logical address `a` to
`BF200000 + 2*a - (a&1)`.

The original AVD producer at `12D2` checks `11DC & 0C`, programs mask `FF`,
index `26` and conditionally data `10` or `30` (retaining old data in its
middle hysteresis interval), then executes `1309: MOV R5,#09`,
`130B: MOV R7,#DC`, `130D: LCALL 02AA`. The actual store is `02AD: MOVX`.
MIPS owns this request's software service/completion. That producer has
**not executed in the connected run**. No idle command, pending command,
payload, response or readiness value is initialized by the new bus.

## Conditional AVD connection

`--exploratory-avd-shared-run` connects the existing 8051 engine to the actual
guest-uploaded program, candidate `1100..12FF` shared RIU aliases and a
bounded `8000..80FF` private XRAM view. The candidate mapping is
`logical = native + 2400`, followed by the original MIPS byte-address rule.
Mailbox and soft-stop code corroborate the candidate wiring; this does not
independently validate every register, COMB bank or field in that aperture.
Unwritten RAM and register fields remain invalid. Partial unknown AFEC writes
require the same byte origin. Partial COMB writes reject because MCU origin
metadata does not identify a possibly changed COMB bank.

Scheduling uses an explicit virtual-cycle divider (default 32) and original
instruction budget (default 1,000,000). It respects program hold and the
separate freeze bit `BF2068C0.0`, identified in original `80504434`
and related `HAL_AVD_VDMCU_SetFreeze`. It waits for an actual guest unfreeze
write; no unknown initial freeze value is chosen. Standard Timer0 behavior
and this clock ratio remain hypotheses. There are zero AVD interrupt entries
in the observed prefix; EA has not yet been enabled.

The independently enabled `--diagnostic-avd-deferred-startup-writes` records
only the original, fully known startup payloads:

- XDATA `C008=55`, `C009=AA`, `101B=07`, `101A=06`.
- Nonstandard SFR `D8=00`, `8E=01`, `E2=00`.

It assumes these unresolved effects do not reset or disturb the bounded CPU
prefix. It supplies no readback or status and does not claim to implement
watchdog disable, mapping controls, UART or clock selection. The related
[MST703 manual](https://datasheet4u.com/pdf/770936/MST703.pdf) associates
`C008/C009` with watchdog keys, but lists the opposite disable pair. It is
not evidence for Titania4 key polarity or timeout behavior. Related Marlon,
Maya, Melody, Milan, Nasa and Whisky AVD images contain the same startup
sequence; they are reference binaries, never substituted for the Manta image.

Both options require guarded symbolic execution and explicit diagnostic
deferral. Default hardware behavior and the prior full-machine mode are unchanged.

## New genuine boundary

Original AVD executes from reset through its own initialization, preserving
invalid bits in masked AFEC updates. The next source is `1ED6: MOVX` from
XDATA `119C`, candidate MIPS alias `BF206B38`. Original code clears bit 3,
rotates, swaps and masks the captured byte before an ADD at `1C26`.
The first code-table address would be:

`0139 + ((captured_119C & 16) << 3)`

Here constants are hexadecimal. Original bits 1, 2 and 4 are needed; they
remain unknown. The emulator stops before using them in an address calculation.
It does not choose a table entry or an analog measurement.

On the MIPS side, original `805035D0..8050365B` exactly matches the related
140-byte `HAL_AVD_AFEC_SetHsyncSensitivity`, except global relocations at
offsets 0, 4 and 44. `80503650` programs `BF206B38` low six bits from the
`u8CNTRSyncAfterLock` parameter in the related driver interface. Its callers
are `805015D4` and `80501618`. None of those instructions executed before the
current boundary. This identifies a software configuration writer for the
candidate alias; the full native-to-MIPS correspondence and its reset value
still require independent evidence. It is not a proven RF measurement or
an independently verified native AVD register definition.

The 8051 validity wrapper now preserves unknown bits through register-operand
AND/OR/XOR, register-index copies and rotate/swap operations. Carry validity
is tracked separately, including PSW saves/restores. Unknown carry cannot
control a branch, ADC, bit store or PSW read. Permuted unknown bytes lose
their former same-register masked-write identity. Existing CPU engines and
original instructions are unchanged.

## Application and graphics results

| Item | Result |
|---|---|
| Main MIPS | 854,795,125 original dispatches; `805032A0 / BF206BB8` |
| TSP OpenRISC | 2,453,934,820 original instructions |
| DVB-T | 1,454,515 original instructions; 161 interrupts; `4AE0 / 2200` |
| PM51 | 3,474 original instructions; zero interrupts; `574F / 0E6A` |
| AVD | 379 original instructions; zero interrupts; `1C26`, captured `119C` |
| Audio DSP | zero instructions; architecture and acknowledgement unresolved |
| Main timer | 38,589 original vector entries |
| UART | byte-identical to `b67392d`; no new original messages |
| ZUI initialization | original `80469E38 -> 804E0D48`, return covered |
| ZUI dispatch/drawing | `804E0DC4` unentered; no drawing demonstrated |
| Framebuffer/GWIN | no actual allocation proved; no enabled window |
| GE/visible pixels | initializer parameters only; no raster effects or real pixels |

The first application poll reaches `80488844`, `8045CFC4`, `8046CA74` and
`805016C8`. It has not returned to `8050170C` or reached `80488864`,
`8047602C` or dispatcher call `8047606C`. Main coverage and every pre-existing
summary field except AVD instruction count/PC are identical to `b67392d`.
Thus this change advances the auxiliary investigation, not the application.
The disabled 8x8 `00DDD800` BIST-pattern descriptor remains unrelated to OSD.
No PNG was exported.

## Validation and evidence

1,387 native checks pass, including all-byte bit-validity permutations,
carry guards, shared register causality, unknown-field rejection, freeze and
deterministic scheduling. Default original AVD still stops at six instructions;
the 379-instruction conditional prefix replays byte-identically twice.
The 18 baseline runs, nine display runs, auxiliary, clock-profile, VIF,
AVD-request, DSP-decoder, immutable-input and four forensics tests pass.
Both CTest cases and the packaged Windows startup pass with System32-only PATH.
Six invalid/strict AVD configurations reject.

The full connected experiment recorded 6,592,227 events. Full replay validation
and exact artifact hashes are recorded in the validation manifest below.
Determinism does not establish hardware reset/clock accuracy. Strict POR,
Winbond CF, PM power/reset inputs, analog hypotheses and DSP acknowledgement
uncertainty retain their earlier scope. The original firmware hash remains
`d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9`.

See [validation manifest](../evidence/avd-shared-validation.json),
[analysis](../evidence/avd-shared-analysis.json.gz), and
[full replay](../evidence/avd-shared-full-replay.json.gz).
