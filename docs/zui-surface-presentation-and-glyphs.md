# ZUI surface presentation, the I2 glyph request, and the first recognizable (exploratory) OSD content

Evidence: `evidence/zui-surface-presentation-and-glyphs.json`. Runs: q0 diagnostic flags + `--diagnostic-vif-cr-lock 0` + `--exploratory-avd-htotal-constant 0`
(+ `--exploratory-avd-noise-constant 0|255` for the last runs) + the three EXPLORATORY GE models below. Everything rendered here depends on
unverified GE semantics; none of it is Titania4-verified output and none of it is panel output.

## Roles of `0x00DDD880` and `0x00E82380` (original code and dynamic traces)

`8049DFC4` (ZUI surface/graphics init, reached from window creation `804AFDFC`) builds two frame buffers with the GOP library:
`8052911C`+`8052CCDC` create **FB 1** (`0x00DDD880`: pool `0x00DDD800` + 0x80 after the disabled 8x8 window 0; address word `0x1BBB10` x 8) and GWIN 5
(bank 4 window 1) is first programmed to show it (`80538948`/`8053A43C`); a GE clear follows. A second `8052911C`/`8052CCDC` creates **FB 2**
(`0x00E82380`, word `0x1D0470`), `80532128(fb=2,gwin=5)` attaches it, GWIN 5 is re-programmed to FB 2, FB 2 is cleared and `8052FBD4(gwin 5, 1)` enables the window.
The ZUI surface record at `806D4F9C` (dump) is: flags `0x00FF020B` (bit 0 initialised, bit 1 painting, **bit 3 double-buffer mode**), `[1]` = 2 (front, shown),
`[4]` = 1 (back, GE target), `[7]` = 5 (GWIN), `+8..+0xE` = x/y/width/height (`y=0x208`, `1360 x 248`). So FB 1 is the back/offscreen buffer all paint callbacks draw
into (GE destination `0xDDD880`), and FB 2 is the buffer the enabled GOP window displays.

## How the original makes painted content visible (two mechanisms, both present)

* **Page flip (double-buffer mode, flag bit 3):** `8049DD70` swaps the record's front/back ids and calls `80532128(old back, gwin)` to attach the back buffer to the GWIN
  (then `8052F3BC`, `80528288`/`8053415C`, `803EA14C`). It is called only from the full-repaint function `804A2FF4` (via `8049F1EC`) after all windows are painted. Not executed yet.
* **Dirty-rectangle GE copy:** `803EE398` blits a rectangle FB 1 -> FB 2 (the call that issued the observed GE `0040`/`0005` copies). In the message handler `804EE128`
  (call site `804EE53C`) it runs once per animation step: a step counter at `-0x6D2C($v0)` counts down from 32 and each call copies a strip whose height is `step*h/32`
  (slide-in animation of the banner), pacing by `803E6BB0`/`803E6BCC` (timer). There is **no** copy and **no** flip in the region paint pass `8049DBC4`.

So presentation is not triggered by paint completion: it follows the ZUI message/animation steps. The first stretch/glyph blockers sat inside the paint pass, which runs
before those copies; once they are supported the copies start (5 steps so far, each preceded by ~400,000 original instructions).

## The 2-bpp glyph request is a text glyph: the "S" of "SCART"

Draw path: text component `804A22E0` (70 hits) -> `803EB45C` -> GFX glyph routine `80520A90` (font callback `803EE508`, glyph info format value 2 -> path `80520F60`) ->
`805254EC` (source format 1) -> `80523BFC`. Source `0x00CB6700`, pitch 8 bytes, **2 bits per pixel, most-significant pair first** (dump: rows 10..31 read as an
anti-aliased capital S with this order). Palette registers `35..3C` as four ARGB8888 words, low half first: `FF010101`, `AAFFFFFF`, `CCFFFFFF`, `FFFFFFFF`; key range
`16/17 = 0` .. `14/15 = 01010101` (index 0 keyed out). Programmed source height is destination height - 5 (the same `805241C4` quirk); rows 32..39 are blank, so both readings agree.
Five glyphs retire (3,080 pixels): the offscreen surface then shows a blue-grey bar with the white text **SCART** (the input-source banner; the board's boot log selected SCART).

Register 11's low byte differs per request class: `01` (bar stretch), `02` (glyph), `00` (surface copy). Marlon's blend-coefficient enum is ONE=0, CONST=1, ASRC=2, which would be
exactly "constant alpha for the bar (reg 13 = FF), per-pixel palette alpha for the glyph, plain replace for the copy". That is a strong circumstantial fit for the exploratory
models below, but it is cross-chip evidence only.

## Exploratory models (all opt-in, labelled in the window, NOT Titania4-verified)

| Flag | Request class | Hypotheses |
|---|---|---|
| `--exploratory-ge-uniform-stretch opaque-source|alpha-bit` | single-colour stretched BitBlt (bitmap 146) | alpha handling; dither no-op |
| `--exploratory-ge-i2-glyph` | `0040`, control `0045`, formats `0C01` | H1..H5 in `src/video/ge_i2_glyph.hpp` |
| `--exploratory-ge-blend-copy` | `0040`, control `0005`, formats `0C09`, reg 11 low byte 0 | ONE = replace; word unchanged; hidden height-5 rows must not change the destination |
| `--exploratory-avd-noise-constant N` | AFEC noise-magnitude byte `BF206A04` | constant (bracket 0 and 255, identical results) |

Unit tests fix exact bytes for each model (self-consistency of the hypotheses, not hardware evidence). Unsupported variants still stop with the exact guarded text and a register dump.

## Visible result

* GOP preview (bank 4 window 1 = FB 2): still the opaque-black plane (checksum unchanged); the five copies so far copied black strips.
* Offscreen inspector (`--offscreen-inspector ddd880:1360x248:2720`, key **O**): the "SCART" bar. This is a read-only diagnostic view of FB 1, **not** the GOP window and not panel output.

## Next blocker

After the 5th copy the monitor `805016C8` (each ZUI loop iteration) reads an AFEC indirect port at `BF206B40/41` (`80503D18`, save/restore of the address port around a `6B39/6B3C` data pair),
an unmodeled device read. The animation needs ~27 more iterations; each iteration passes the same monitor, so the AFEC chain (HTotal, noise, now the indirect port) gates presentation in
this diagnostic scenario, not the GOP model.
