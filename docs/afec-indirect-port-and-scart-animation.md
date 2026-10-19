# AFEC indirect port, source-detect getters, and the complete 32-step SCART slide-in (exploratory)

Evidence: `evidence/scart-animation-and-afec-port.json`, `evidence/scart-gop-history-*.png`. Everything that draws (bar, text, copies) depends on the EXPLORATORY GE
models of `zui-surface-presentation-and-glyphs.md` plus the hypotheses below; none of it is Titania4-verified and none of it is panel output.

## `BF206B40/41` is not a flat register pair: the routine at `80503CE0`

Decoded from the original instructions (`80503D18..80503DB4`), registers by the AFEC XDATA mapping `BF206A00 + (xx>>1)*4 + (xx&1)`:

| MIPS byte | XDATA | Role in the routine |
|---|---|---|
| `BF206B41` / `BF206B40` | `11A1` / `11A0` | **address pair**: saved (`s1`/`s2`), set to `6A`/`3C`, bit 7 of the low byte set (`|0x80`) = open, restored and bit 7 cleared at the end |
| `BF206B39` / `BF206B3C` | `119D` / `119E` | **data pair** (high / low) of the 16-bit register addressed by `6A3C`: read twice, `(D>>5) + ((a0+1)&~1)` is written back, `805CBBD8(0x4B)` delays, then a second value derived only from `a0` is written |

No branch depends on the data: the routine reads, computes, writes, delays and restores. Other users: the AVD MCU writes `11A0/11A1` (pair, `0xA42`) and `119E/119F` (`0x42A`)
with zero; the 8062AE00 init table writes plain `119D = 0x8E`, `119E = 0`. Nothing identifies the addressed register `6A3C` (the same `6A3C` is the only selector in the image).
**Meaning of `6A3C` is unknown**; the protocol is modelled, not the semantics.

* `--exploratory-afec-indirect-6a3c N` (HYPOTHESIS, bracket it): with address `6A3C` and bit 7 set, reads/writes of the data pair address a 16-bit content (initially `N`) instead of the plain
  bytes; closing the window leaves the plain bytes untouched. The two address-pair saves are INVALID observations granted by `configs/diagnostics/opaque-afec-indirect-port.tsv`
  (hash-bound to the routine; the routine restores them unchanged). Results for `N = 0` and `N = 0xFFFF`: identical (the content is overwritten before it matters; final content `0x8E00`).
* The model is unit-tested (`afecIndirectTests`, 9 checks): address programming, bit 7, lane assignment, write-through, close/reopen, other addresses.

## `BF206F5D` bits 1, 4, 6: source-detect status getters

`8052801C` returns the byte; `80527BB4/80527BD4/80527BF4` extract bits 6 / 1 / 4 for the source-status dispatcher `80453860` (13-entry table).
`--exploratory-mmio-bits 1f206f5d:52:VV` (new generic, bit-scoped HYPOTHESIS option: only the mask bits become known, writes rejected, every other bit keeps its provenance,
INVALID grant for the getter's whole-byte load in the same policy file). Bracket `0x00` (nothing detected) versus `0x52` (all detected) **diverges**:

| value | dispatches | outcome |
|---:|---:|---|
| `0x00` | 862,875,831 -> continues | passes; animation completes (below) |
| `0x52` | 862,875,831 | stops at `80528064` reading `BF206F99` (the next detect byte) |

so the two outcomes are not equivalent; the animation run uses `0x00` (selected input SCART, nothing else connected), as a hypothesis.

## The 32 animation steps (original `804EE128` handler, GE copies `803EE398`)

Exploratory blend-copy model, `--exploratory-ge-height5-full-copy` (HYPOTHESIS B3': register `6F` = height-5 is an encoding artefact and every destination row is copied; the strict
rule refused step 24, whose five hidden rows are bar pixels). Machine-readable log: `ge_blend_copy_log` in the run summary. Source is always FB 1 `0x00DDD880` at (0,0); destination
FB 2 `0x00E82380`, x 0..1359, rows `top..247`, `top` = 240, 232, 224, 217, 209, 201, 193, 186, 178, 170, 162, 155, 147, 139, 131, 124, 116, 108, 100, 93, 85, 77, 69, 62, 54, 46, 38, 31, 23, 15, 7, 0
(strip height `step*248/32`). Steps 1-23 copy black onto black (0 changed words). **Step 24 is the first that moves SCART pixels into FB 2** (8,160 changed words = the bar's first 6
rows), steps 25-32 add 10,880 / 10,977 / 11,251 / 10,123 / 11,759 / 11,918 / 11,918 / 10,551 changed words; cumulative non-black words 92,480 = the whole bar (1360 x 68).
After step 32 FB 2 equals FB 1 (SHA-256 `85638ddd...59056` for both).

## What is visible in the GOP preview

Frames recorded by `--gop-frame-history` (bounded timeline, index in the evidence folder): 10 distinct enabled-window states from the black plane to the bar rising from the bottom edge of the
window up to step ~31 (`evidence/scart-gop-history-mid.png`, `-final.png`). The last copy (step 32) and the page flip `8049DD70` (+ full repaint `804A2FF4`) run when the ZUI has already disabled
GWIN 5, so the **complete** bar is never visible through an enabled+latched window in this run; the history never contains a frame after it. Whether the real TV keeps the banner on screen
longer is not decided here (the diagnostic timing/virtual-clock assumptions are unverified).

## Next request: an I8 surface

After the flip the ZUI re-initialises: GE fill `0x30`, control `0001`, formats `0401` (destination **4 = I8**), destination `0x00DDD880`, pitch `0x400`, rectangle `(0,0)-(0x3FF,0x291)`
(1024 x 658), colour `0xFFFF`; the same palette block `35..3C`. This is a different surface format (8-bit indexed) for the next OSD layer; the existing fill model supports only 2/4-byte
formats and one format-C subcase, so it stays guarded (`rect-fill decode refused` plus the register file in the summary field `ge_guarded_stretch_request`).
