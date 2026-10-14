# PM power-good cross-reference and original UART startup

No authentic OSD frame is demonstrated. The default guarded boot still stops
PM51 at `9B27 / XDATA 0EC2 bits0..2` and MIPS at `80404BF4 / BF001CD1`.
The new, separately configured experiment executes 3,474 original PM
instructions and stops at `574F / XDATA 0E6A`, without generating a marker.

## Evidence for the first wait

The original `9B17..9B29` calls the two-byte reader at `6D84`, uses only R7,
masks with 7, XORs with 7, and loops while nonzero. The high byte at `0EC3`
remains invalid and unused. Shared physical addresses are `BF001D84/85`.

Two primary MStar boot sources name the same address/mask/all-set predicate
`CheckPowerGood`: [Amber5 boot.S](https://github.com/neuschaefer/mstar-mboot/blob/962e8b8258378dded694883a9f9acb7058d34631/sboot/src/amber5/boot.S)
and [Kronus boot.S](https://github.com/neuschaefer/mstar-mboot/blob/962e8b8258378dded694883a9f9acb7058d34631/sboot/src/kronus/boot.S).
The accompanying [8051 preloader description](https://github.com/neuschaefer/mstar-mboot/blob/962e8b8258378dded694883a9f9acb7058d34631/doc/sboot_with_8051_preloader.txt)
places this check before releasing MIPS on an **8051 boot-strap path**.
The Manta dump starts with MIPS instructions; that alternate startup ordering
is not imported into this machine.

This supports an aggregate power-good interpretation. It does **not** identify
each MSD309 detector, voltage threshold, settling time, or reset value.
`--exploratory-pm-power-good` explicitly supplies a sustained aggregate powered
input under this related-source wiring hypothesis. It is not enabled by
default, does not change with polling, defines only bits0..2, and never writes
the activity marker. The native model can retain absent or disconnected input;
no speculative power-on delay is claimed.

The MIPS bootloader's `BFC013B4/BFC013C0` writes `BF001C80=00/80` are covered
by the existing trace. Related bootrom/setSPI source identifies clock-source
selection at this address. These writes are not treated as a proven PM reset
release or a reason to manufacture power-good status.

## Original serial configuration, not a readiness response

`--candidate-pm51-uart` adds a bounded R8051-style configuration subset.
The exact original `936C..9389` sequence writes ADCON bit7, S0RELL/S0RELH,
SCON, PCON, SBUF, S1RELL/S1RELH and S1CON. The addresses agree with the
MStar R8051XC header and the [R8051 UART0](https://www.keil.com/dd/vtr/7682/12754.htm)
and [UART1](https://www.keil.com/dd/vtr/4129/10478.htm) descriptions.
This corroborates the serial subset, not every R8051XC option or clock ratio.

Immediate SFR OR/AND operations preserve uninitialized bits. Only original
writes establish configuration; unused ADCON/reload bits remain invalid.
An unmasked unknown value cannot influence a branch. The actual guest SBUF
byte is queued; a consumer of its unimplemented dedicated-generator completion
stops. No transmit-ready or receive event is supplied by the host.

Original PM writes also latch the candidate UART fields at `0E13` and page10
`1E03`. Related pad tables describe receive/select controls; Titania4 UART
source references `BF203C04` bit10, with a conflicting comment about removal.
These mappings therefore remain candidate behavior behind the UART option.
Only their supported configuration bits are made valid, with no pad input or
IRQ invented. Original `0E1E |= 10` updates the PM GPIO direction backing;
the other direction bits remain invalid. Titania4 GPIO definitions identify
`0E1E` as PM GPIO output-enable controls.

With both explicit options, original `RET 9389` executes. The next original
`5749..574F` reads `0E6A`, clears bit7, and attempts a same-origin masked
store. Its side effect is unverified, so the store does **not** retire.
The subsequent static code clears `0E26/27` and waits on **0EC2 bit3** at
`575B`. The named low-three-bit CheckPowerGood reference does not establish
that fourth bit. Neither later operation is claimed as executed.

## Application and display boundary

Neither cold-init writer `934D` nor the RTC-related marker-eight producer has
executed. `BF001CD1` stays invalid. Its low-nibble equality to 8 selects
optional saved source/channel restoration; it is not evidence that cold boot
requires the value 8. Both paths eventually join the original application
flow, with additional initializers before `80469E38 -> 804E0D48`.

ZUI remains unentered. GOP initialization twice and palette upload remain
demonstrated, but no enabled framebuffer, GE raster operation, or original
pixels have appeared. The native window still awaits guest scan-out.

## Reproduction and evidence

Use the existing checkpoint launcher with `--exploratory-pm-power-good
--candidate-pm51-uart --pm51-history FILE`; omission preserves the prior
guarded checkpoint. Both options require original PM startup and keep all
earlier exploratory inputs explicit. No firmware byte or PC is patched.

`tools/pm_reference_analyze.py` validates original byte sequences and local
primary reference hashes. `tests/pm51_firmware_regression.py` runs original
PM instructions, both new configurations twice, and verifies original serial
return, accepted configuration writes, invalid high status, rejected next
write, and zero marker production. See [verification](../evidence/pm-power-uart-verification.json),
[conditional cold-boot summary](../evidence/pm-power-uart-summary.json),
[original PM regressions](../evidence/pm-power-uart-regression.json),
[connected prefix replay](../evidence/pm-power-uart-prefix-replay.json),
and [source cross-reference](../evidence/pm-power-reference-forensics.json).

The rebuilt Windows executable passes **847 native checks**, all **18 baseline**
and **nine display** regressions, **four forensics** tests, the complete existing
**five-CPU event replay**, both CTest targets, and clean-PATH native startup.
The new conditional run executes 1,789,567,273 MIPS dispatches, 280,822,700
OpenRISC instructions, 468,999 DVB-T instructions, six AVD instructions and
3,474 PM instructions, with 5,441 main timer and 52 DVB-T interrupt entries.
PM and AVD interrupt entries remain zero; audio DSP execution remains zero.
The main PC, UART, non-PM summary state and full MIPS coverage match the prior
guarded checkpoint. New original UART messages: none.

Determinism scopes are explicit: the full five-CPU replay uses the existing
guarded trace with power-good/UART options omitted. The new configuration
replays every event of its connected 300,000-dispatch prefix, covering all
new PM execution; summaries, coverage and bounded history match. Isolated
original-PM diagnostics also repeat identically. A full new-mode 10-GB trace
replay is not claimed.

| Capability | Status |
|---|---|
| Explicit aggregate power-good input model | implemented; MSD309 mapping hypothetical |
| R8051 UART configuration and validity subset | implemented |
| Original PM serial initializer return | demonstrated under named hypotheses |
| UART baud-clock transmission/completion | unsupported |
| Individual power-good detectors/reset state | unsupported |
| `0E6A` bit7 side effect / `0EC2` bit3 producer | unsupported |
| Cold PM state, ZUI, enabled framebuffer, authentic frame | unsupported |

The next useful evidence is the **MSD309 PM register definition** for `0E6A`
bit7 and `0EC2` bit3, not another SoC's convenient reset value. A read-only
trace from an identical MSD309-T2B using an already established service UART
or SDK debug interface could sample `BF001D84/85`, `BF001CD4` and `BF001CD1`
through cold power-on and standby/wake, alongside original writes to
`BF001C4C/4D` (`0E26/27`) and `BF001CA5` (`0E53`). No physical television
was accessed, flashed or probed in this work. Later PM CKCON/interrupt wiring
also needs evidence before the original cold-init writer can be claimed.
