# GWIN 5 handoff: page flip, intentional close, and the I8 / OSD 2 phase (exploratory run)

Evidence: `evidence/gwin5-handoff-and-i8.json`. Timeline from the new read-only `gop_timeline_tail` summary field (`--gop-frame-history DIR --gop-timeline-from CYCLES`: every guest write into
GOP bank 4 and every active-latch event, anchored to the original PC; ticks are virtual CPU cycles, about 16 per dispatch). Run flags are those of `scart-animation-and-afec-port.md`
plus `--exploratory-ge-i8-fill`; everything drawn depends on the exploratory GE models.

## 1. The complete bar IS shown; the window is closed deliberately ~45M dispatches later

* Step 32 (full 248-row copy) finishes at ~870.04M dispatches; the history frame at 870.05M is the complete bar in the enabled, latched GWIN 5 (0,520).
* **Page flip** (`804A2FF4` full repaint at 870,053,777 -> `8049DD70` at 870,088,823, called from `804A32B8`): `80532128(fb 1, gwin 5)` attaches the old back buffer; the GOP timeline shows
  ctrl `0xF865` (enable kept, format 6) written at tick 13,926,806,496 (`8053A31C`), an active latch at 13,932,002,944, then the base words `0x84 = 0xBB10`, `0x88 = 0x001B`
  (= `0x1BBB10 * 8 = 0x00DDD880`, FB 1) and `0xA4/0x90/0x94/0x98/0xA0 = 0x154/0/0x154/0x208/0x300` (pitch 2720, x 0..1360, y 520..768) at 13,932,005,456..9,568 (`8053A4F8/FC`, `8053A31C`), and the next latch at
  13,937,202,960. The sequence is repeated once more (`8053415C`, 13,947.6M..13,958.0M). FB 1 and FB 2 are pixel-identical at that time, so the flip changes the attached address, not the image.
  The enable bit is never cleared by the flip.
* **GWIN 5 disable:** `8052FBD4(5, 0)` at dispatch 915,737,848 (`8049DF04`, call `8049DF38`, ra `8049DF40`), inside the OSD-close routine `804C3574` (called from `804AFD3C` at 915,724,445). Register
  effect: bank 4 offset `0x80` low byte `0x65 -> 0x64` (written as `0xF864` by `8053A31C` at tick 14,651,833,328), latch 14,654,802,976, then base/geometry words cleared (14,660.06M..).
  It is the original ZUI's **state change**: `804C3684` ("OSD select") is entered with `a0 = 2` from `80486780` (`80486778: jal 804C3684, a0=2`) at 916,978,404, hides every window (message `0x17` via `804A071C`), tears the surfaces down
  and re-initialises them. Nothing indicates an emulation latch error: the shadow/active enable bit tracks the guest writes exactly (timeline). The banner was enabled for ~0.72e9 virtual cycles
  (a 312 MHz domain would be ~2.3 s; that clock is an unverified assumption).
* GWIN 5 is re-enabled immediately by the next surface set-up (`8052FBD4(5,1)` at 917,017,257) at a **new full-screen window**: bank 4 window 1, `(0,0)` 1360 x 768 at `0x00FDB880` (word `0x1FB710`), format 6, pixels opaque black.

## 2. The OSD 2 phase

`804C3684(2, N)` repeats every ~3.25M dispatches with `N = 0x11E, 0x129, 0x134, ...` (+11 each), 17 times up to 969M; each repetition closes and re-opens the surfaces (`804C3574`/`8049DF04`
17x, `8052FBD4` 35x) and issues GE fills into a 1024-pitch format-4 surface. The caller is the application function around `80486778` (the same dispatcher that opened OSD 3); what the counter
represents is not determined. The run then stops at `805FBBA4` reading `BF205E79` (scaler bank 1, byte `0x79`, via the bank-select routine `805FBB70` called from `805F2ECC`): a new unmodelled scaler read.

## 3. Format 4 and the I8 fill

The original format table `80525E28` classifies destination formats: 6..14 (bits `0x57C0`) = 2 bytes/pixel, 15 = 4 bytes, **4 = 1 byte/pixel**; the request (control `0001`, formats `0401`, destination
`0x00DDD880`, pitch `0x400`, rectangle `(0,0)-(0x3FF,0x291)` = 1024 x 658, colour `0x0000FFFF`) is a plain fill. `--exploratory-ge-i8-fill` (HYPOTHESIS, NOT TITANIA4-VERIFIED) writes the low byte of
registers `70/71` as the index (`0xFF`); the palette is not applied. The surface reuses FB 1's memory (`0xDDD880`..+658 KB) after the banner's teardown: the old 1360 x 248 x 2 surface was released
(`8052AB60`) and the allocator hands out the same pool address. 68 I8 fills retire (68,195,840 bytes). Native tests (`geI8FillTests`, 12 checks) fix exact bytes, clipping, pitch, empty clips,
strict rejection and bounds; they establish internal consistency only.
