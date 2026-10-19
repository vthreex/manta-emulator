# The blocked 4x33 stretch is bitmap 146, a single-colour strip; exploratory uniform-stretch model

Evidence: `evidence/zui-bitmap146-resource-trace.json`. Diagnostic connected run (q0 flags + `--diagnostic-vif-cr-lock 0` +
`--exploratory-avd-htotal-constant 0`), read-only probes `--watch-pc-regs` and `--dump-guest-range`.

## Resource-resolution trace (dynamic, every function entered exactly once for this draw)

| Dispatch | PC | Registers | Meaning |
|---:|---|---|---|
| 856,552,664 | `8049DBC4` (ra `804F067C`) | | ZUI paint pass |
| 856,554,467 | `8049E7BC` (a2 `806A3314`) | after `8049C9AC` for windows `0x01`, `0x24`, `0x25` | window-record walk; the draw belongs to **window id `0x25`** |
| 856,554,484 | `8049D8D8` | a0 = 4 (component type), a1 = `0x43` (67) | component paint callback |
| 856,554,502 | `8049CD90` | a1 = `80686AF0` | component descriptor (`80686AD0 + 0x20`) |
| 856,556,811 | `8049D3E0` | | bitmap component painter |
| 856,557,929 | `803EA1A4` | **a0 = `0x92` = bitmap 146**, a2 = `0xB4` (destination y 180) | bitmap frontend |
| 856,557,969 | `8051FF80` | | GFX BitBlt |
| 856,557,981 | `803EE5C0` | a0 = `0x92` | registered bitmap callback |
| 856,558,023 | `805254EC` | a1 = 9 (ARGB1555), **a2 = 4, a3 = 38** | source-buffer API: 4 x **38** |
| 856,558,669 | `80523BFC` | | stretch/blit setup |
| 856,560,135 | `805241C4` | s0 = ctx `806FEB60` | the `height-5` reduction (reads ctx+0x518; the only access of that offset in the image) |

Source `0x03D46C80` (pixel pool `0x03C3D000` + `0x109C80`), 8-byte rows, **304 bytes, all 152 words are `0x46D8`**: alpha bit 0, R=17 G=22 B=24
(5-bit). So the programmed 33 is the register value and 38 is the bitmap height; both rows ranges hold identical data.
The ctx+0x518 flag has no store with that immediate anywhere in the image (set by a bulk initializer or computed offset): not located.
Marlon's analogous scale routine tests a context byte at `0x614` for the same `height < 6 -> phase 0x1000` special case; its meaning is not named in the
available debug info.

## What the uniform source implies

A source whose whole sampled region is one colour makes sampling position, phase rounding and any normalized filter irrelevant (every tap reads
the same word). What is left unestablished: the alpha handling of a source with alpha bit 0 under GE registers `11=FF01`, `12=0100`, `13=00FF`
and control `0007` (dither + blend, no key), the dither, and the format-C output. Comparative Marlon disassembly (later SoC; offsets, not semantics,
carried over): `MDrv_GE_SetAlphaConst` writes reg `13`, `SetAlpha_ARGB1555` ORs the coefficient into the high byte of reg `11`, `SetDither`/`SetAlphaBlend`
set control bits 1 and 2. The original sets constant alpha `0xFF`, which suggests an opaque result, but nothing proves that a source alpha bit of 0
is ignored (see `ge-8040-register-spec.md` item 6).

## `--exploratory-ge-uniform-stretch opaque-source|alpha-bit` (opt-in, NOT Titania4-verified)

Executes a stretched BitBlt only when all inspected source words (the larger of the programmed and scale heights) are equal, the pair is
ARGB1555 -> format C and no source key is set. `opaque-source` writes `0x8000|rgb555`; `alpha-bit` writes nothing for alpha bit 0. Dither is assumed to
leave 5-bit -> 5-bit unchanged. Everything else keeps the exact guard. The window label states "EXPLORATORY GE uniform-stretch model in use".
Native tests (`geUniformStretchTests`) fix exact bytes for the connected request (1360 x 68 x `D8 C6`, 92,480 pixels, rows outside untouched), clipping, the
alpha-bit hypothesis, a single differing word in row 38 and a format mismatch.

Result (`opaque-source`): the stretch retires (92,480 pixels into the **offscreen** surface `0x00DDD880`, rows 180..247 = `0xC6D8`), ZUI execution continues for
12,719 more MIPS instructions (856,573,362) and reaches the next GE request. The GOP-visible surface `0x00E82380` is unchanged, so the preview is
still the black plane: the bar is not visible yet.

## The next request: an indexed glyph blit (guarded)

Plain command `0x0040`, control `0045` (enable + blend + **source key**), format `0C01` (**source format 1**, destination C), source `0x00CB6700`/pitch 8, destination
`0x00DDD880`/pitch 2720, destination rectangle `(0x5A,0xBE)-(0x66,0xE5)` = 13 x 40, source rectangle 13 x 35 (unequal extents), clip `(0x57,0xB4)-(0x113,0xEF)`,
`11=FF02 13=00FF 14=15=0101`, and a register block `35..3C = 0101 FF01 FFFF AAFF FFFF CCFF FFFF FFFF` that looks like an indexed-colour table, colour `71=FF00`.
This is consistent with an I2 text glyph; it needs the indexed-format conversion, the key and blend semantics, and the unequal-extent rule: all unestablished. The summary
field `ge_guarded_stretch_request` carries the whole register file.

## Sources inspected for GE semantics (none is Titania4)

`work/reference-marlon-libapiGFX.a` (later Marlon SoC; DWARF enums for formats/blend/alpha, disassembly of `MDrv_GE_*`), `reference-apiGFX.h` (API names only), SigmaStar GFX
documentation (earlier session). No Titania4/Uranus `halGE.c`, vendor register document or captured GE output is available locally; panel-routing registers were not progressed.
