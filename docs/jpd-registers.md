# JPD (JPEG decoder) hardware — evidence from the multimedia coprocessor's own driver

Status: reverse engineering in progress. Nothing here is a vendor document; each item says how it was established.

## Table memory (index port `RIU 0x2F00`, data port `RIU 0x2F04`) — VERIFIED
The driver writes 528 half-words (captured with `--exploratory-copro-capture 0xa0202f04`, indices 0x0000 / 0x0200 / 0x0400):

| words | content |
|---|---|
| 0..127 | Huffman length records, 16 per table, tables in order DC0, DC1, AC0, AC1; 2 half-words per length L=1..16 |
| 128..383 | AC symbols: word i = `AC1[i] << 8 | AC0[i]` |
| 384..399 | DC symbols: word i = `DC1[i] << 8 | DC0[i]` |
| 400..527 | quantisation tables in raster (not zig-zag) order, Q0 then Q1 |

Length record `(lo16, hi16)`; an empty length is `(0, 0)`:
- `hi16 = 0x100 | (firstcode_L >> (L-8))` (bit 8 = valid, low 8 bits = first canonical code of length L left-aligned to 8 bits; for L < 8 the code is shifted left)
- `lo16 = (extra first-code bits beyond the first 8, left-aligned at bit 15) | symbol base index` (AC: index in bits 7..0; DC: index << 4)
- decode: peek 16 bits, pick the LARGEST L whose 16-bit-aligned first code <= peek, `index = base + (code - firstcode)`, symbol = table word.

`tools/jpd_table_check.py LOGO.jpg captured_writes.json` decodes the logo's whole entropy-coded scan twice (the JPEG's own DHT vs. this table memory): **49,776 blocks, 0 mismatches**
(symbols, magnitudes and bit consumption identical), so the format above is confirmed on the logo JPEG (1940x1080, 4:2:0, DRI 122, Q0/Q1, DC0/DC1/AC0/AC1).

## Registers seen during the logo decode (CP window `0xA0202E00 + off`; values are the logo's)
| off | value | meaning (evidence) |
|---|---|---|
| 2E00 | `0x2000`, then `0x215E`, then `0x21DE` | SCONFIG; bit 7 (`0x80`) written last = START (driver prints "After Setting SCONFIG, JPD START!!"); other bits TBD |
| 2E04 | `0x8` then `0x0`; later `0x3`, `0xB`, `0x3` | MCONFIG-like: bit 3 pulsed = software reset; low bits enable |
| 2E08 | `0x79` (=121) | restart interval - 1 (DRI = 0x7A = 122) |
| 2E0C / 2E10 | `0xF4` / `0x88` | picture width / 8 and height / 8 aligned up (1952/8, 1088/8) |
| 2E24 / 2E28 | `0x7F` / `0x7F` | event mask / clear (all 7 event bits) and event flags (read) |
| 2E2C:2E30 | `0x45C3`, `0x0323` | 32-bit absolute address of the first entropy-coded byte = `0x032345C3` = input buffer `0x03230000` + SOS-end offset `0x45C3` |
| 2E34:2E38, 2E3C:2E40 | `0x00646000`, `0x006C48FF` | read buffer start / 8 and end / 8 - 1 (`0x3230000`, `0x36247F8`) |
| 2E44:2E48 | `0x004F5400` | output buffer start / 8 (= `0x027AA000`, the decoder's "Output Mem") |
| 2E4C | 0 | TBD |
| 2E50..2E5C | read only | TBD (32-bit pairs: current positions?) |
| 2E60 | read | status/error (read once after START) |
| 2E70 | `0x3B` | TBD |

## Executable model (`--exploratory-copro-jpd`, src/peripherals/jpd.hpp) — first result
Driven only by the original coprocessor driver's register programming and table writes, the model decodes the boot-logo JPEG (1940x1080, 4:2:0, DRI 122, scale 1/2) from guest DRAM:
**8,296 MCUs / 49,776 blocks -> 976x544 packed 4:2:2 at `0x027AA000`** (exactly the buffer the MIPS later stretches with GE command 0x8040, pitch 1952).
The decoded picture is the Manta logo (`evidence/jpd-logo-decode-diagnostic.png`, converted for viewing with BT.601 full-range - the conversion is the viewer's, not the hardware's).
Fields from the driver code: SCONFIG [1:0]=Y H-sampling, bit2=Y V-sampling-1, bit3=colour, [5:4]=scale code (0 1/1, 1 1/2, 2 1/4, 3 1/8; the driver writes
the code to its global `0x14100A` and the shift to `0x141008` together), bit6=restart enable, bit8 always set, bit9=separate U/V Q tables, bit13 set before START, bit7=START.
Still assumptions (not evidence): reduced-size IDCT method (orthonormal N-point IDCT of the top-left NxN coefficients scaled N/8), vertical chroma replication, byte order Y0 U Y1 V,
instant completion, event bit 0 = done, component->table selection (Y Q0/DC0/AC0, Cb/Cr Q1/DC1/AC1).
Open: after START the driver still ends the job with `error: 0x0001` (event/status semantics of `2E28`/`2E60` are not right yet), progressive / other sampling / SUVQ / scale 1/4,1/8 are refused by the model.

## End-to-end result: the logo reaches a 1920x1080 guest framebuffer (diagnostic, exploratory models)
Chain (all original firmware code + the opt-in models): original JPEG resource -> coprocessor decoder driving the JPD model (976x544 packed 4:2:2 at `0x027AA000`) -> original GE stretch `0x8040`
(`--exploratory-ge-yuv-stretch`, nearest, packed Y0 U Y1 V assumed) into `0x02D00000` (1920x1068 rows written, 6-row margins) -> original GE plain copy `0x0040` (programmed height 5 rows short:
hypothesis H5) into `0x03230000`. Both framebuffers are byte-identical and show the Manta logo (`evidence/logo-framebuffer-0x3230000-diagnostic.png`, half size, BT.601 conversion by the viewer).
Measured with a snapshot saved at 560M dispatches (coprocessor, JPD and FIQ state are part of the save state).
Not shown on any display layer yet: through 1.5B dispatches the only enabled GOP window is the OSD bar (bank 4); GOP register writes since 416M are all bank 4, and the MVOP (`0x1F202400` block) only
receives timing writes. Whatever scans `0x03230000` out (or whether the original shows it at a later UI state) is still unknown.
