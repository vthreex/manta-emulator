# Audio IDMA and original AVD execution

This continuation preserves `2d5e8c5`, both original main CPU engines, the
DVB-T MCU implementation and the immutable original flash. No OSD is
claimed. AVD execution is a bounded startup experiment, not completed
video-decoder firmware initialization.

## Audio evidence and implemented memory interface

At `804F9098`, helper `804F905C` reads logical sound register `2D5A`
through the exact original RIU byte conversion. Its physical address is
`1F225AB4` (`BF225AB4` alias). Caller `804FB800` supplies `2D5A`;
`804F79D8` invokes code-load type `60`, then compares the result with `E3`.
It retries with the original delay routine and counter limit `65`. Both
success and failure send `F3` through logical `2D38` and return one.
There is no observed MIPS producer write to `BF225AB4`.

The best-supported interpretation is a DSP2 load-code acknowledgment,
not an IDMA program-memory byte or immutable chip identifier. The
related Marlon archive's `HAL_AUDIO_SeSystemLoadCode` has the same
`60/E3/65/F3` sequence and calls `HAL_MAD2_GetLoadCodeAck`. That archive
uses a different acknowledgment address (`2DFC`), so its address map is
not imported. The producer and audio DSP ISA remain unresolved.

The corrected cold boot verifies **2,781 original 24-bit words** (8,343
bytes) through original code comparisons. It prints `DSP_LoadCode
finished(type=SE dec_none(0x30))`, `DSP_LoadCode finished(type=SE
adv_none(0xA0))`, and `DSP_LoadCode finished(type=T4 SE system(0x60))`.
The previous three IDMA readiness failures disappear. It still stops
before consuming the unknown DSP2 acknowledgment.

Original routines `804FF690` (write) and `804FF49C` (verification) prove
the following opaque addressed-word interface. Logical addresses are
relative to the audio driver's `110000` adjustment.

| Operation | Logical offsets | Physical first port |
|---|---|---|
| Write address, 16 bits | 2D04 | BF225A08 |
| Data byte phases: middle, high, low, zero padding | 2D02, 2D03, 2D02, 2D03 | BF225A04/BF225A05 |
| Read address, 16 bits | 2D08 | BF225A10 |
| Read request / write completion wait | 2D00 bits 3 / 4 | BF225A00 |
| Readback middle, high, low | 2D0C, 2D0D, 2D0E | BF225A18/BF225A19/BF225A1C |

The explicit `--candidate-audio-idma` model stores only actual completed
guest word uploads and returns their addressed bytes. Unwritten words
remain unknown. It auto-increments separate read/write cursors after a
word transfer, tracks the four byte phases, and keeps the second port
separate. No PM/DM capacity, address-space selection, DSP opcode or
reset contents are inferred from the 16-bit address port.

Transfer completion is atomic in this functional model; physical
latencies and clock relationships are unverified. It produces no
DSP2 acknowledgment. Status overlays use the existing one-shot
consumed-mask and same-origin RMW validity guards, retaining unrelated
unknown fields. The first cold run exposed an early-return integration
bug at `804F92A8`, which incorrectly rejected a safe masked restore;
the corrected model retains the existing guard path.

Analog Devices' [AN-415](https://www.analog.com/media/en/technical-documentation/application-notes/an-415.pdf)
describes similar two-access packing for 24-bit IDMA transfers. This
similarity is an architecture investigation lead, **not** evidence that
MSD309 executes ADSP-21xx instructions.

`audio_idma_extract.py` reconstructs the original trace's addressed words
and eight upload segments, retaining their addresses and hashes. The
addressed-memory SHA256 is
`f25cba069289823a8165f3d22dbbd90f58c83bbdc49aeff90f6476faad7da2df`.
Images are private original firmware, not bundled with the source/build.

The native `audio_dsp_disasm --candidate-adsp21xx` uses an unmodified,
revision-pinned BSD-3-Clause MAME disassembler. Coherent common decoding
includes word `18064F` at address zero as `JUMP $0064`, original vectors
with `RTI`, and arithmetic/register operations. However word `040040`
at the candidate entry `0064`, and multiple `0780xx` words, are unknown
to that documented decoder. Under the same candidate decoding, `0065`
calls `3FDE` and `006F` jumps to `3FC1`; neither address was initialized
by the observed IDMA upload. This is evidence for further investigation
of MStar extensions and possible ROM/external mappings, **not** proof
of a specific missing mask ROM. No audio DSP instructions execute and
no unknown word is skipped.

## Separate AVD program

Original MIPS loader `805047D4` uploads `80627A00..8062AE00`, 13,312 bytes:
SHA256 `48ab58a5689d3135b2cdf78fa77da92fa912776f741c33c7afa97fdcfc4ee4fd`.
The original port readback and byte comparisons precede its release.
The auxiliary CPU fetches the same storage populated by the guest.

Actual executed startup is `0000 LJMP 0003`, `0003 MOV SP,AC`,
`0006 LJMP 0219`, `0219 ACALL 07FD`, `07FD MOV DPTR,C008`,
`0800 MOV A,55`. The next instruction, `0802 MOVX @DPTR,A`, stops
before its unknown hardware write completes. Six original instructions
retire; no ready/status output is produced. These are 8051-family
instructions; the exact MStar MCU variant is not established. The
standard 8052 reset/upper-IRAM model remains explicitly experimental.

Static continuation of that routine writes `AA` to `C009`, clears SFR
`D8`, writes one to SFR `8E`, programs XDATA `101B/101A`, and configures
Timer0. These later instructions have **not** been executed or modeled.
The paired writes resemble an unlock/key sequence, but watchdog/reset
identity and effects are unverified. No undocumented write is ignored.

`--exploratory-avd-mcu-startup` runs bounded original startup on the
original guest release. `--exploratory-avd-suspended-boundary` lets only
the other CPUs continue after the AVD boundary, without completing its
transaction or acknowledging anything. These options do not establish
a measured auxiliary clock ratio or a running AVD main loop.

## Normal path to ZUI and unresolved dependencies

`80469B8C` calls platform initializer `804868B0`. It synchronously calls
AVD owner `803E364C`, then audio owner `8040DAC8`; the latter calls
`804F7334` at `8040DB58`. ZUI's normal call at `80469E38 -> 804E0D48`
occurs later. No application function is invoked from the host.

The DVB-T MCU remains stopped at `4AE0 / XDATA 2200`. Original supported
mailbox replies and interrupt entry behavior remain intact. Strict POR
and undocumented Winbond `CF` behavior remain unresolved.

To implement the DSP2 producer, obtain a matching DSP ISA/compiler
reference or establish the uploaded instructions against documented
architectures, then trace the code that writes the acknowledgment.
A passive original-hardware host/DSP register trace would establish
the reset values, upload framing and acknowledgment timing. Matching
AVD MCU register documentation or a read-only debug capture is needed
for `C008/C009`, `D8`, `8E` and its XDATA mapping. This session performs
no physical television writes or pin probing.

## Verified boot checkpoint

The corrected exploratory run dispatches **1,439,384,052 original MIPS**
instructions, **193,276,894 original OpenRISC** instructions, **468,999
original DVB-T MCU** instructions and **six original AVD** instructions.
It executes **4,320 original main timer ISR entries** and **52 DVB-T MCU
interrupt entries**. The lower MIPS count reflects removal of the
IDMA timeout loops; it is not a regression in initialization progress.

The main stop is still `804F9098 / BF225AB4`, DVB-T remains at
`4AE0 / XDATA 2200`, and AVD is suspended at `0802 / XDATA C008`.
GOP initialization executes twice, six disabled-window programming
calls occur, and no valid enabled window, framebuffer raster drawing,
ZUI entry or authentic pixels appear. A black unconfigured host window
is not display evidence.

## Validation and reproduction

Build succeeds with the existing Windows CMake/MinGW configuration.
**770 native checks**, **18 baseline firmware runs**, **nine display
regressions**, **four forensics tests**, original DVB-T MCU diagnostics,
original AVD diagnostics and candidate DSP disassembly tests pass.
Native startup also passes with PATH containing Windows System32 only.
The corrected full cold-boot trace replays every event and produces an
identical complete final summary, including all three previously running
CPUs and bounded AVD execution. This does not demonstrate completed
AVD/DSP startup or hardware timing accuracy.

Append `--candidate-audio-idma --exploratory-avd-mcu-startup
--exploratory-avd-suspended-boundary` to the guarded shared-MCU boot
recipe in [the prior continuation](shared-mcu-continuation.md).
`tests/openrisc_regression.py` reconstructs the exact flags from the
new summary and reruns the original firmware with `--replay-trace`.
It independently verifies addressed audio words and readback values,
original AVD fetches and its unacknowledged boundary.

Auxiliary-only original AVD reproduction:

```
python tests/avd_firmware_regression.py --firmware ORIGINAL.bin --probe BUILD/avd_mcu_probe.exe --report avd-result.json
```

Recover private original audio uploads and compare the candidate ISA:

```
python tools/audio_idma_extract.py --firmware ORIGINAL.bin --trace BOOT.jsonl --out PRIVATE-DSP
BUILD/audio_dsp_disasm.exe --candidate-adsp21xx PRIVATE-DSP/port-0-addressed-words.bin 0 0x80
```

No private original program images or full multi-gigabyte traces are
redistributed in the build package. Evidence stores summaries, hashes,
limited diagnostic observations and compressed regression reports.
