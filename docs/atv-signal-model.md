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
