# ATV signal model (exploratory): PAL CVBS waveform generator + measurement front end

`src/video/pal_cvbs.hpp`, tests in `tests/pal_cvbs_tests.inc` (15 checks, 2,039 native checks total). **Nothing here is wired to the emulated chip yet.**

## What it is
* **`PalSource`** generates the sample stream of a PAL-B/G composite signal at 27 MHz (1728 samples per line, 625 lines, 2:1 interlace): line sync 4.7 us at -300 mV, swinging burst
  (10 cycles of 4.43361875 MHz at +-135 degrees, 300 mV p-p), vertical interval (5 equalizing, 5 broad, 5 equalizing pulses at half-line spacing, two groups 625 half-lines apart),
  EBU 100/0/75/0 colour bars with PAL encoding (V switched every line) on active lines 23..310 and 336..623. Options: deterministic noise, no burst (monochrome), no sync (open input).
* **`measure()`** is a separate signal-only front end: level estimation (blanking and sync tip from a filtered histogram of ADC codes), slicer at mid sync, line period, sync width,
  sync amplitude, broad-pulse vertical sync, lines per frame/field, interlace (vertical sync alternates by half a line), burst amplitude and PAL phase swing by quadrature correlation.
  It never looks at generator state: removing sync, burst or adding noise changes the result.
* Verified by tests on a clean signal (line 1728 clocks, sync 127 clocks, 300 mV, 625 lines, interlaced, burst 300 mV p-p, PAL switch), with 20 mV noise, monochrome, and open input.

## Assumptions (labelled, not measurements of the Manta TV)
ADC 10 bit, input range -0.4..+0.8 V, 27 MHz sample rate; the real MSD309 analog front end (gain, clamp, ADC rate/resolution) is undocumented. Waveform timing and levels come from ITU-R BT.470 / BT.1700 / EBU.

## Not done yet (next)
Mapping the measurements to the AFEC/AVD/IP registers the firmware reads. The AVD MCU reads silicon-owned bytes whose bit meaning is unknown (`11DB.7`, `11FC.2`, `113C/D`, see `avd-register-ownership.md`); the only
physically meaningful published quantity is HTotal (`11C7/11C8` -> `BF206B8D/90`). Mappings from measurements to those bytes will be hypotheses, bracketed and labelled, and compared on what the firmware then does.

## The input surface the AVD firmware actually reads (static, session 12)
The AVD MCU program (13,312 bytes, `work/avd-original-private.bin`, disassembled with `work/build/avd_mcu_probe.exe`) reads these 43 XDATA bytes/halfwords that it never writes itself
(immediate-DPTR `MOVX A,@DPTR` forms): `110A 1110 1118 111B 1122 112A 1132 1133 1136 113A 113B 113C 113D 1142 1143 114C 114D 1152 1154 1155 1156 1158 1159 1184 1185 119C 11AB 11AD 11B0 11BC
11CA 11CB 11CE 11CF 11D4 11D5 11D7 11D8 11D9 11DA 11DB 11DC 11FC`. These are the places where a real signal has to enter. Examples of how they are used: the measurement block `09BB..0AAA` reads
`110A/110B` (16 bit, compared with the previous sample in `6C/6D`, accepted when the difference lies in `0x60..0xE0` counts), `11BC/11BD` (10 bit, averaged with the previous value), `1110/1111`
(status word, bit 5 set from a comparison, republished through `076E` to `1110`), `113C`; `11FC` is read, tested (`bit 2` clears two state bytes) and written back complemented (events captured, write-0-to-clear).
Which physical quantity each one is (line period counter, burst amplitude, noise estimate, sync lock...) is **not documented**; deriving it from the consumer logic, one register at a time, is the next step.
Each mapping from `Measurement` fields to these registers will be an opt-in, labelled hypothesis; the PAL generator only supplies quantities whose unit is fixed by the standard (line period 1728 clocks at 27 MHz = 864 at 13.5 MHz, sync width, burst, 625 lines).

## Isolated AVD experiment (`tools/avd_signal_probe.cpp`, session 12)
The original AVD MCU program runs in isolation with a configurable register provider (`--set XDATA VALUE`, `--field-events STEPS`, `--mailbox-zero`, `--xram-zero`, `--visit PC`); it prints where the MCU stops on an
unknown byte (with the origin of unknown IRAM bytes) and the published block `11C0..11DF`. Loop used to map the input surface: run, read the stop, look at the consumer code, supply the value, repeat.

Evidence-backed meanings (from the consumer code, not from a datasheet):
* `113C/113D`: raw horizontal total of the incoming line, copied by `0AA9..0ABC` through `076E` (high byte `& 07`) to the published HTotal `11C7/11C8` that the MIPS reads (`HAL_AVD_AFEC_GetHTotal`).
  The MIPS compares it with the table at `0x80627940` = `1135, 1097, 910, 917, 1127` (tolerance in `s3`): 1135 = 283.75 x 4 = PAL line length in **4 x fsc clocks**, 910 = 227.5 x 4 = NTSC. A PAL line therefore reads `0x046F`
  (line period 64 us x 17.734475 MHz). With `113C/D = 0x46F` the isolated MCU publishes `11C7/C8 = 6F 04`.
* `11FC` is an **event register**: the main loop copies it to `29h`; the measurement block (`09BB..0AAA`) only runs when bit 0 is set, bit 2 clears two state bytes; the MCU acknowledges by writing the complement.
  A steady signal produces one bit-0 event per field (modelled: every 20,000 MCU instructions = 20 ms at an ASSUMED 1 MIPS MCU).
* `110A/110B`: 16-bit counter compared between events (`9C5..9D6`: current - previous, or current if it wrapped); the result is later tested against `0x134` (308) and `0x101` (257): **lines per field** latched at the field event
  (PAL 312 / 313 alternating, NTSC 262 / 263). Modelled that way.
* `112A`: noise estimate, averaged `(7*avg + x)/8` and compared with `0x12`/`0x19` into a saturating counter; clean signal -> 0.
* Everything else the loop touches (30 bytes: `1103..1106 1121..1123 1129..112B 1135..113B 1159..115C 1183..1185 11A7..11AA 11DB 11DC 11DD`) is supplied as a **zero reset fixture** (unknown on the TV), and the host mailbox `11C0..11DF` reads 0 until written.
With these the original AVD program runs its idle loop indefinitely (6M instructions, no boundary), measures and **publishes HTotal 1135 for the PAL signal**.

## Wired into the emulator
`--exploratory-atv-signal pal` (needs the AVD shared run and no absent-HSYNC flags; state is part of the snapshot): the PAL generator is measured at start-up by the signal-only front end and the AVD MCU's reads of `113C/D`, `110A/B` and
`11FC` are served from that model; the zero fixtures are installed for the other bytes. Summary key `exploratory_atv_signal`.
