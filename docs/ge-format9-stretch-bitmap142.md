# GE format 9 stretched BitBlt after the first digit key (bitmap 142)

## Request (run s14/s15, decoded by `GeStretchRequest`, never executed in strict mode)
`cmd 8040`, control `0007` (enable | dither | blend, no key), mode `0010`, formats `0C09` (source 9, destination C), source `0x03D44628` pitch 130, destination `0x00DDD880`
pitch 600, destination rectangle (0,0)-(119,99), source rectangle 65 x 53, ratio `8AA / 947`, register 11..13 = `FF01 / 0100 / 00FF`.

## Source resource: bitmap 142 (65 x 58, native format 9)
- The original pool allocator (`803EE5C0`, 8-byte aligned cursor from `0x03C3D000`, verified by `zui_flow_probe`) places **bitmap 142** at `0x03D44628` exactly.
  Layout of all 350 bitmaps: `docs/zui-bitmap-pool-layout.json` (id, width, height, address). The register `6F = 53` is the original height-5 programming of a 58-row bitmap (ratio `947 = 58*4096/100`).
- The original metadata callback reports native format **9** for every bitmap. Decoded as ARGB1555 (A bit 15, R 14..10, G 9..5, B 4..0) the pixels form a coherent image: a white
  disc with an antenna / broadcast icon on the dialog's teal background (rendered in `evidence/bitmap142-argb1555.png`); RGB565 and ARGB4444 decodings are visibly wrong.
  The alpha bit is **clear for every pixel** (including the opaque ones), so how the GE uses source alpha for this request (constant alpha FF in register 13, blend enabled) matters:
  the exploratory model assumes "opaque source, constant alpha FF" like the established bitmap-146 case. Not verified for Titania4.
- Format numbering agrees with MStar's later public GFX enumeration (RGB565 = 8, ARGB1555 = 9, ARGB4444 = 0xA, ARGB1555_DST = 0xC, YUV422 = 0xE, ARGB8888 = 0xF) and with the original
  format-size table (`80525E28`); this is comparative evidence, not a Titania4 datasheet.

## Window that requests it
The digit key raw `0x01` (app `0x11`) opens **OSD 17**, descriptor `0x80686030 + 48*17`: 6 windows, surface (x,y,w,h) = (1020, 20, 300, 100) - the numeric-entry banner (handler
`8046D9DC`). The first GOP window of that surface is 1320 x 120 in the native preview because the window state was composed into the display-sized preview region.
`evidence/virtual-remote-original-key-path.json` previously called it a "new 1320x120 window"; this is the same event.

## Exploratory model: `--exploratory-ge-nearest-stretch`
Needs `--exploratory-ge-uniform-stretch opaque-source`. `dest(x,y)` samples source `round((x*ratioX + phaseX) / 4096)`, `round((y*ratioY + phaseY) / 4096)` (the original's phase is the half-pixel centring term
`(src-dst)/(2*dst)` in 4.12), clamped to the source rectangle, alpha bit forced on, dither/blend assumed to leave the 5-bit word unchanged. **Assumptions:** nearest rather than bilinear taps,
rounding position, opaque alpha, no dithering. Eight stretches in run s15 executed under it. It does not support source keying (`control & 0x40`), other formats, or non-MIU0 addresses.

## Still unresolved (all guarded in strict mode)
Hardware filter taps, sampling position rounding, dither pattern, blend equation, format-C alpha conversion, and the source-key comparison (requests with `control 0047`, e.g. 6 x 226 at `0x03E41810`,
and the 4 x 35 -> 125 x 58 stretch at `0x03D62750` stop the run).
