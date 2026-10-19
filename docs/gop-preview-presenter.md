# Diagnostic GOP preview presenter (native window) and the display-chain inventory

Evidence: `evidence/gop-preview-window-validation.json`. Everything displayed is **guest framebuffer bytes** of
ENABLED + ACTIVE-LATCHED GOP windows; nothing is drawn by the host except the window's status bar and an
explicitly neutral checker for canvas pixels the guest did not provide. It is a **diagnostic GOP preview, not
verified panel scan-out**.

## Components

| Piece | File | Role |
|---|---|---|
| Layer capture | `Msd309Machine::collectGopLayers` (`src/board/machine.cpp`) | emulator thread copies `pitch*height` guest bytes behind each window found by `readGopWindow(active=true)` (enable bit AND active latch), address via `gopPhysicalAddress` (MIU1 bit from the guest), count of unknown bytes via the provenance trackers |
| Composer | `composeGopPreview` (`src/video/gop_preview.hpp`) | format 6 decoded as ARGB1555 (inference, see below); windows composed in (bank,window) order (priority NOT established); alpha bit clear = leave the pixel uncovered; clipping to a 1920x1080 canvas; unknown formats/bytes/geometry skipped with a stated reason; deterministic FNV checksum |
| Hand-off | `GopPreviewStore` | immutable snapshots + sequence number behind a mutex; the window never touches guest RAM or the machine |
| Publishing | `codeHook` every 2^21 dispatches and once at the end of `run()` | on the emulator thread, the only thread that writes guest RAM (CPU, GE retire); no race possible |
| Window | `src/ui/native_window.cpp` | polls the store at 10 Hz, draws the status bar (mode label, window geometry, checksum, run state) and the frame with integer scaling; uncovered pixels as a grey checker; closing/minimizing only affects the UI thread |
| Evidence | `--preview-capture FILE.bmp --preview-close-after-ms N` | renders the same window content off-screen to a BMP once the guest run stopped (works minimized/occluded) |
| Test | `--window-preview-selftest` + `tests/window_preview_regression.py` | labelled SYNTHETIC buffer proves exact pixel delivery, checker for uncovered pixels, determinism, clean-PATH startup |

Windowed mode (no `--headless`) now runs the emulator on a worker thread and the preview window on the UI thread; the
windowed run's summary is identical (all fields except the ring buffers) to the headless run of the same options.

## Display chain: what exists

| Stage | State |
|---|---|
| GE raster writes into guest RAM (fills, plain BitBlt) | implemented; stretched `8040` guarded |
| GOP window register shadow + active latch (banks 1/4) | implemented (guest writes, update/ack timing hypothesis) |
| Window geometry/format/address decode, MIU1 bit | implemented, byte-verified against the original window programming |
| Palette (8-bit formats) | commit tracking only; no I8 presentation |
| Preview of enabled windows | **new** (this document) |
| GOP top-bank output routing, mux, destination (IP/OP), blend/alpha registers | guest values recorded (below) but **not interpreted or modeled** |
| Window priority, constant/per-pixel alpha, transparent-color key | not modeled |
| Scaler/mixer, panel output enable, timing/refresh into a panel frame | not modeled (an output VSYNC counter exists for polling only) |

## Guest-programmed state in the diagnostic run (`gop_register_banks` in the export metadata)

Bank 4 window 1 (`0x80..`): control `0x61` (enable + format nibble 6), low/high address words `0x0470`/`0x001D`
(x8 = `0xE82380`), `hs 0`, `he 0x154` (x4 px = 1360), `vs 0x208`, `ve 0x300`, pitch word `0x154` (x8 = 2720): all consistent
with the decoded 1360x248 window at y=520. Bank 4 window 0 (`0x00..`): control `0x60` (format 6, **disabled**), 8x8 at `0xDDD800`.
Banks 1 and 0/3 (top banks) hold the remaining guest writes (e.g. bank 0 `0C0=C1 0C1=03 0C4=38 0C5=04`), whose meanings (output
select, mux, blend, gamma) are **unresolved**; see the dump in the metadata. An enabled GWIN therefore does not prove panel output.

## Hardware format 6

The firmware's own helper `8053A5C4` classifies hardware nibbles: {0,1,2,3,6,8,9} are 16 bpp, 4 is 8 bpp, 5 is 32 bpp; the window
programming (`80538FF0..`) writes `format<<4` straight into the control byte, so firmware API format == hardware nibble. The library
enum with names (Marlon `libapiGOP`, DWARF) uses different numbers (ARGB1555 = 9) and cannot be mapped to this firmware. Evidence
that 6 is ARGB1555: the original GE clears this very surface with destination format `C` (ARGB1555_DST). That is consistent, **not
proven**; the decoder is labelled "inferred" everywhere.

## The 4x33 -> 1360x68 stretch: the 33/38 discrepancy is explained

The original library routine at `805241C4..805241DC` programs source height with `if (ctx+0x518 != 0 && h >= 6) h -= 5`, while
the ratio/phase were produced by the scale packer from the full height. For the connected request: ratio Y `0x8F0` =
floor(38*4096/68) and phase `0x1C79` = expectedPhase(38,68); X `0x0C`/`0x1807` match 4 -> 1360; register `6F` = `0x21` = 33 = 38 - 5.
`GeStretchRequest` now accepts exactly that signature (`heightReducedBy5`, `scaleSourceHeight`), still rejects any other
disagreement, and lists the hardware meaning of the reduction as unresolved. Source/destination, format C alpha, dither pattern and the
filter remain unestablished, so `8040` stays guarded. (The 38-row hypothesis was not invented: it is what the original arithmetic encodes.)
