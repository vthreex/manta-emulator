# OSD 3 GE command `8040`: register-level specification and evidence limits

Continues `cc225d7`. **`8040` is still not executed: no pixels are produced, no
queue entry is retired, and no connected OSD frame is claimed.** This note
turns the recorded original request into a tested, self-consistent description
([`ge_stretch_request.hpp`](../src/video/ge_stretch_request.hpp), decode-only,
no raster path) and states exactly which semantics no available evidence fixes.

Source of the request: the 52 original GE register writes recorded by the
isolated OSD 3 paint probe
(`evidence/ge-osd-copy-guard.json.gz`, `allocated_paint_GE_registers`), ending
with `60 = 8040` at original PC `80526598`. A native test replays those exact
writes and requires the decoder to reproduce every field below.

## Command word

| Bit | Meaning | Evidence |
|---|---|---|
| `0040` | Blit trigger | Original `80523BFC` (plain copy) |
| `8000` | Stretch | Original command selection programs ratio/phase `64/65/5E/5F` and mode bit 4 only for this command. Comparative: Marlon `MDrv_GE_BitBltEX` sets `0x8000` and ORs `0x10` into register `01` in the same branch. |
| `4000` | Nearest sampling | Original `8051F9E8`/`8051F938` flag tests; comparative: Marlon selects `C000` instead of `8000` when its nearest draw flag (bit 16) is set. |

Both the original-code and the Marlon correlation are software-side only.
They establish which bit the driver sets, not what the filter hardware does.
`C040` is the nearest variant; OSD 3 uses the non-nearest `8040`.

## Differences from the supported plain `0040` copy

| Register | Plain `0040` | OSD 3 `8040` | Status |
|---|---|---|---|
| `00` control | `0001` | `0047` = enable + **dither (bit 1)** + **blend (bit 2)** + **source key (bit 6)** | bits located by original helpers `8052272C` (dither), `805224FC` (key); blend bit 2 is inferred from the `0003`→`0007` control sequence |
| `01` mode (known mask `0372`) | `0` | `A495`: bit 4 set | confirmed stretch enable (see above) |
| `34` formats | `0808` or `0F0F` | `0C09`: source `09`, destination `0C` | low byte = source, high byte = destination (Marlon reads `34` the same way) |
| `64/65` | not written | `1000 / 0F45` | verified against original arithmetic, below |
| `5E/5F` | not written | `0000 / 1FA3` | verified against original arithmetic, below |
| `11/12/13` alpha | not written | `FF01 / 0100 / 00FF` | high byte of `11` from the original 1555 helper |
| `14..17`, `1C` key | not written | `14=F0FF 15=00FF 16=0078 17=0078 1C=0` | written by original `805224FC` |
| `59` | `0` | `0` | required zero by the decoder |
| `68..6B` destination | equals source extent | `(531,42)-(537,85)` = 7x44 | |
| `6E/6F` source extent | equals destination | 7x42 | stretch is vertical only |
| `20/21`, `30` | source address/pitch | `03D6E128`, 14 | bitmap 196 |
| `26/27`, `33` | destination | `00DDD800`, 2720 | diagnostic allocation |

## Scale registers: confirmed against original arithmetic

The decoder recomputes, from the programmed rectangles, the original routine's
results (`ratio = floor(src*4096/dst)`, `phase = trunc((src-dst)*4096/(2*dst))`
masked to 12 bits, bit 12 set when `src<dst`, ratio extension in bit 15 of
`5E/5F`) and rejects any disagreement:

- X: 7 -> 7: ratio `1000` (exactly 1.0 in 4.12), phase `0`.
- Y: 42 -> 44: ratio `0F45` (3909/4096), phase `-93` (`0FA3` with bit 12 set).

This is confirmed (800 original cases plus the OSD request). It verifies
**how the registers are programmed**, not how the hardware consumes them.

## Observation, labeled inference: the key range

The key registers hold the 32-bit values `00780078` and `00FFF0FF`. Read as
`AARRGGBB`, they bracket magenta-like colors (R,B in `78..FF`, G in `00..F0`),
which matches the two source colors of bitmap 196 (`014E` and magenta `7C1F`).
A range far wider than the single 5-bit magenta value is consistent with the
comparison seeing **filtered/expanded 8-bit colors after stretching**, so that
blended edge pixels still key out. That is an inference from register values
only. Whether the comparator runs before or after filtering, and its range
inclusivity, remain unestablished.

## What nothing available fixes

Every item below needs hardware evidence (a register-level driver, vendor
documentation, or measured GE output). The firmware cannot supply it: it writes
no dither matrix, no filter-tap table and no rounding register for this
command, so these behaviors are fixed in silicon.

1. Sampling position and rounding for ratio `0F45`/phase `1FA3` (nearest
   selection or a filter, and endpoint handling).
2. Non-nearest filter taps.
3. Dither pattern, coordinate dependence, and effect for a 1555->`0C` pair.
4. Blend equation and alpha rounding, and its order versus stretch.
5. Key comparison point and range inclusivity.
6. Format-`0C` alpha-bit quantization and whether source alpha or register `11`
   supplies it. Bitmap 196 has bit 15 clear in every word, so treating it as
   opaque or as transparent are both unsupported.
7. Source ARGB1555 (`09`) alpha expansion.

Searches this session found only API-level documentation (the obsolete
`apiGFX.h` names `ARGB1555_DST`, dither and nearest mode without behavior) and
Marlon driver disassembly, which agrees on command bits but shows no
hardware-side rounding. `MsTypes.h` and the pinned drivers target later SoCs, so
they remain comparative evidence only.

## Decision

No guessed implementation is added. `8040` stays rejected, unretired and
unpainted. If an exploratory model is wanted, it must be an explicitly labeled,
opt-in hypothesis (for example nearest sampling with the key tested on the
expanded source color), kept out of strict mode and never described as
Titania4-verified. The fastest route to a real answer is any of: a Titania4/
Uranus-family GE register-level source (`halGE.c`), vendor documentation for
format `0C`, or a captured GE output for a known input.

## Validation of this change

`core_tests` passes with **1,619 checks** (previously 1,563): the recorded
request decodes to the fields above, every consumed register's unknown-bit
case is rejected, and mutated ratio, phase, extent, mode-bit and control cases
are rejected. No production source changed, so the connected-boot, regression
and replay results of `cc225d7` are unaffected.
