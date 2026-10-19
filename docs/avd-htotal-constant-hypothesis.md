# `--exploratory-avd-htotal-constant N`: the connected ZUI main loop iterates (hypothesis, not a TV measurement)

Continues `e30deb2`. Off by default; requires the absent-HSYNC hypothesis and guarded exploration. Guest writes to the
two bytes are rejected. Unit-tested (`avdHtotalConstantTests`) and label/guard-tested in
`tests/diagnostic_options_regression.py`.

## Why it exists

After the VIF carrier-lock bracket (`vif-cr-lock-status.md`) the application's **second** main-loop iteration stops at
`80501744`: the HTotal stability tracker compares history halfwords that were stored from the unknown sample, and the first
join of `80501BFC` had degraded its counter to unknown, so the join cannot soundly be re-applied (it refuses). Joining
across loop iterations would need a symbolic counter; instead the unknown input is bracketed.

With no HSYNC the real value of the published AFEC HTotal (`11C7/11C8` = `BF206B8D` low, `BF206B90` high; read by the
original getter `805030E0`) is unknown. The hypothesis defines **only those two bytes** as a constant `N`; neighbouring
AFEC bytes (co-channel word, status words) stay invalid, so later decisions on them still stop.

## Result (diagnostic cold runs, ~8 min, deterministic)

| N | MIPS dispatches | ZUI dispatcher entries (`804E0DC4`) | GE fills retired | stop |
|---:|---:|---:|---:|---|
| `0x0000` | 856,560,643 | 104 | 2 | `80526598` write `0x1F205004 = 0x11` (GE mode reg `01` bit4 = stretch enable): "stretch/filter/mode unsupported" |
| `0xFFFF` | 856,560,643 | 104 | 2 | same |
| `0x0360` | 856,560,643 | 104 | 2 | same |

The path is independent of the constant (same instruction count and stop for all three): the original loop only needs
the tracker's comparisons to be decidable. The wrapper `8047602C`, platform polling `8046CA74` and dispatcher `804E0DC4`
run 104 times each; window-state init `8049F040` twice; `8049F824`/`804A0640`/`804A071C` ~117 times each
(per-window paint/update path). GOP shadow state unchanged: one enabled+latched window (bank 4 win 1, x=0 y=520,
1360x248 at `0xE82380`, opaque black from fill 2).

## The first connected stretched request (guarded; still not executed)

The stop is the **command write** `0x8040` to `0x1F205180` (GE write routine `80526598`). Full GE register file at
the stop (index=value; decode-only, from the `ge_stretch_request_guarded` event):

| Field | Value |
|---|---|
| command / control `00` / mode `01` | `8040` / `0007` (enable, dither, blend; **no source key**) / `0011` (stretch enable) |
| formats `34` | `0C09` (source ARGB1555 `09` -> destination ARGB1555_DST `0C`) |
| source `20/21` / pitch `30` | `0x03D46C80` / 8 bytes (4 px) |
| destination `26/27` / pitch `33` | `0x00DDD880` (the **offscreen** root surface, not the GOP window `0xE82380`) / 2720 |
| clip `55..58` | `(0,0xB4)-(0x54F,0xF7)` |
| destination rect `68..6B` | `(0,0xB4)-(0x54F,0xF7)` = 1360 x 68 |
| source rect `6C..6F` | `(0,0)` 4 x 33 |
| scale `64/65`, phase `5E/5F` | `000C` / `08F0`, `1807` / `1C79` |
| alpha `11/12/13`, colour `71` | `FF01` / `0100` / `00FF`, `FF00` |
| `59` | `0040` (GE-init default) |

This is **not** the recorded bitmap-196 request (7 x 42 -> 7 x 44, source key on, `0x03D6E128`): it is a 4 x 33 strip
stretched horizontally by ~340x to the full OSD width at y=180..247, i.e. a background/gradient bar. The decode-only
cross-check refuses it ("ratio disagrees with original scale arithmetic": `65 = 0x8F0` corresponds to a 38-row source
over 68 rows while the source rectangle says 33 rows); the discrepancy is recorded, not resolved. Format-C alpha
conversion, the horizontal filter for a 340x upscale, dither and blend equations are unresolved, so the request stays
guarded and no pixel is written.

## Limits

* Not a measurement and not evidence about the TV's behaviour with no signal; the real AVD/AFEC publication is unknown.
* `0x0360` (864) is the PAL line length at 13.5 MHz, chosen only as a plausible non-degenerate bracket point.
* The GOP window content is still the root clear; no OSD picture exists yet.
