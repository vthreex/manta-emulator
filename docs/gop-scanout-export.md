# Connected OSD 3 surface clear and the enabled GOP window (diagnostic, not a displayed frame)

Evidence: `evidence/connected-osd3-clear-and-gop-window.json`. Category: **connected diagnostic raster under
opt-in hypotheses** (the q0 USB/AVD/HTotal diagnostics plus `--diagnostic-vif-cr-lock 0`). Not authentic
television output, not a verified physical startup.

## What the original firmware does

Two original calls to the rect-fill helper `803EAC58` (call sites `8049E184` and `8049E244`) in the ZUI
surface/graphics initializer `8049DFC4` (reached from window creation `804AFDFC` <- OSD select `804C3684`
<- `80480CA8` <- UI wrapper `8047602C`). The helper builds a rectangle record (colour `0xFF000000`,
opaque black ARGB) and the original GFX library (`805202C8` -> `80522F20`) programs the GE:

| Reg (byte addr) | Value | Meaning (see `ge-8040-register-spec.md`, `ge-destination-binding.md`) |
|---|---|---|
| `0x1F2051A0..AC` | `0,0,0x54F,0xF7` | rectangle 1360 x 248 |
| `0x1F2051C0/C4` | `0x0000/0xFF00` | colour `0xFF000000` |
| `0x1F2050D0` | `0x0C09` | destination format C, source 9 |
| `0x1F205098/9C` | `0x2380/0x00E8` | destination `0x00E82380` (fill 2); fill 1 targets `0x00DDD880` |
| `0x1F2050CC` | `0x0AA0` | pitch 2720 |
| `0x1F205180` | `0x0030` | command: fill |

Fill 1 clears `0x00DDD880..0x00E8237F` before the GOP window is created (`8052911C`, `8052CCDC` with format
`0xC`); fill 2 clears `0x00E82380..` after the window setup. Together: 2 retired fills, 674,560 pixel-bytes.
Both are the rule-independent subset supported by `ceb9475` (opaque black -> pixel `0x8000`).

## The enabled GOP window (from the original GOP register writes, active latch)

bank 4, window 1: x=0, y=520, 1360 x 248, pitch 2720, hardware format 6, MIU-relative `0xE82380` = exactly the
destination of fill 2. Its 674,560 bytes: 337,280 nonzero (`0x80` high byte of every pixel), 0 invalid bytes,
SHA-256 `bfa2ec18...a25393` (identical in the summary and the export). The window is enabled and active-latched
in the emulator's GOP shadow model; the shadow latch timing is not hardware verified, and no panel/mixer
composition, blend or scaling is modeled.

## `--export-gop-frame FILE.json`

After the run, decodes the memory of every **enabled and active-latched** GOP window (never forces anything on,
refuses windows with unknown bytes or other formats) to `FILE_bankB_winW.bmp` (32-bit BGRA, top-down) plus
metadata with `authentic_frame:false`. Format 6 is decoded as ARGB1555: **assumed**, because the original GE
clears this surface with destination format C (ARGB1555_DST); the repository does not contain an independent
Titania4 GOP format table (the Marlon `libapiGOP` enum uses different API numbers: ARGB1555=9, RGB565=8).
Exact-byte unit test in `tests/spi_nor_program_tests.inc` (`gopExportTests`). The export of this run is a
uniform opaque-black 1360 x 248 image: it proves the surface-clear path, not an OSD picture.

## What is still missing for an authentic OSD frame

1. The ZUI has painted only the root clear. The window paints (bitmap 196 `0x8040` format-C stretch, text,
   borders) have not started: the application's second main-loop iteration stops at `80501744` (HTotal history
   compare, see `vif-cr-lock-status.md`).
2. The panel path (GOP mixer, output enable, scaler) and a live window presenter are not implemented; the native
   window still shows the "guest display not initialized" placeholder.
