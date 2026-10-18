# AVD register ownership map: who produces `11DB`, `11FC` and the HTotal chain

Continues `f59cf26`. **No production emulator code changes; connected state is
unchanged** (MIPS `80501BFC`, AVD `0702 / 11DB.7`, 863 AVD instructions, no
dispatcher, no pixels). This note adds a reproducible static search
([`tools/avd_register_ownership.py`](../tools/avd_register_ownership.py), report
[`evidence/avd-register-ownership.json`](../evidence/avd-register-ownership.json))
and what it implies for the producer question. Nothing here assigns a register
value.

## Address mapping (confirmed against three known pairs)

AVD MCU XDATA `11xx` reaches the host at
`BF206A00 + (xx>>1)*4 + (xx&1)`. This reproduces the original HTotal pair
`11C7/11C8 -> BF206B8D/BF206B90` and co-channel `11CA -> BF206B94`, and
places the other registers at `11DB -> BF206BB5`, `11FC -> BF206BF8`,
`113C/113D -> BF206A78/79`.

The related MST786 datasheet's
`AFEC Register (Bank = 1035)` is byte-indexed by the same low byte, so these
are bank-`1035` AFEC bytes. That generation documents only a few of them
(`D6/D8/DA` are labeled "Move to SW", meaning firmware-owned); it lists nothing
for `DB`, `FC`, `3C/3D` or `C7/C8`. It is a different chip, so this is a
naming/layout clue, not a Titania4 reset value.

## Ownership results

Search forms: AVD program `MOV DPTR,#11xx` + `MOVX`; original MIPS accesses
through the HAL context base pointer (`LW -0x268c`); the 78-entry MIPS init
table; and immediate offsets in the related Amethyst and Marlon
`libdrvAVD.a` archives.

| XDATA | Host alias | AVD program | Original MIPS | Related host drivers |
|---|---|---|---|---|
| `113C/113D` | `BF206A78/79` | read only (`0AA9`, `2208`; `2203`) | none | none |
| `11C7/11C8` | `BF206B8D/90` | published by `0775/0784` via `076E` (computed DPTR) | read, `805030FC/F8` | `HAL_AVD_AFEC_GetHTotal` |
| `11CA` | `BF206B94` | read (`1130`) | read, `80503098` | `GetCoChannelOn` |
| `11DA/11DB` | `BF206BB4/B5` | read only (`2A11`; **`06FE`**) | none | none |
| `11DC` | `BF206BB8` | read (`12D2`) | read, `805032A0` | `HAL_AVD_AFEC_SetRegFromDSP` |
| `11FA` | `BF206BF4` | none by immediate DPTR | none | none |
| `11FC` | `BF206BF8` | read only (**`098D`**) | none | none |
| `119C` | `BF206B38` | read x4 | writer `80503650`* | `SetHsyncSensitivity` |

\\* The search finds four of the five known host accessors by itself but misses
the `119C` setter, whose base pointer is loaded outside the lookback window.
The table is therefore strong for the driver archives (which also find that
setter) and weaker, not conclusive, for absence in the original MIPS image.

## What this implies

1. **`11DB.7` is not written by anything we can find.** The AVD program reads
   it once (`06FE`), never stores to it, and neither the original MIPS image,
   its 78-entry init table, nor the Marlon/Amethyst drivers touch it. A bit that
   no shipping software writes holds its silicon reset value or is driven by
   hardware. Either way it is a per-chip constant that cannot depend on whether
   a video signal is present.
2. **It is also not the gate.** Both `11DB.7` outcomes were already run from the
   original reset vector (2,278 and 2,309 instructions); both reach the main
   loop and stop at `0993`, which needs `11FC.2`. Resolving `11DB.7` alone would
   move the connected AVD stop from `0702` to `0993`, not publish HTotal.
3. **`11FC` and `113C/113D` are hardware-owned.** The AVD program only reads
   them and no host code touches them. So the event bits (`.0` gates the
   measurement, `.2` clears private counters) and the raw H-period counter come
   from the AFEC/VD silicon. Neither the firmware nor any related driver says
   what they hold with no sync input.
4. **The one host-side indirect path is the `11DC` mailbox.**
   `HAL_AVD_AFEC_SetRegFromDSP` is the host performing register accesses that
   the AVD side requests. That is the only candidate indirect writer for
   registers the AVD does not drive itself, and it is already represented by
   the existing request-channel model; whether it ever targets `11DB` is
   unknown and would have to come from AVD-posted requests, not a guess.

## Soundness note for the MIPS HTotal comparison

`80501BFC` (`bne a3,a1`) compares the unknown sample with history. Both
outcomes rejoin at `80501758` and differ only in the stability counter and
accepted-total stores. A sound way past it would be an explicit
**unknown-effect join** (those cells become unknown, later consumption stops
again), not choosing a branch. That changes strict-mode behavior and later code
conditionally writes hardware based on further unknown status (`BF206B94`,
`BF206B98`), so it is recorded as a design option here and **not implemented**.

## Remaining blockers and the evidence that would remove them

- Silicon behavior of `11FC` events and `113C/113D` with no sync input (a
  register capture from a real MSD309/Titania4 board, or a register-level AFEC
  specification for this generation).
- Reset value of `11DB.7` and `11DA/11DC` (same sources).
- Whether the join abstraction above is acceptable for strict mode.

## Validation

Report regenerated from the unchanged original images; hashes recorded in the
JSON. Because only a tool, an evidence file and this note were added, the
connected counts, 1,619 native checks and previous full-suite results are not
newly re-run and are not claimed fresh for this commit.
