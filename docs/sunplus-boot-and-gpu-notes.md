# Sunplus SPHE8600 bring-up notes: boot kind, audio loop, GPU command FIFO

Status: the Shivaki STV-24LED14 image boots through DirectFB init and the main app's event loop under the exploratory
configuration below. No pixel has been drawn yet; the GPU command FIFO is the next piece to model.

## Runner configuration that reaches the app idle loop (all hypotheses are exploratory)
`sunplus_run` with `configs/sunplus/hyp-003-boot-kind-b.tsv`, `--iop-set 0x0=0 --iop-set 0x8=1 --state-follow 0x80dd80e4:0xBFFE1DB0 --state-default 1 --irq-vec 0x48`
and a DSP-capable core (`--cpu 24kec`). Requires the patched Unicorn (`tools/patch_unicorn_temps.py`, see below).

## Findings
* Host crash: Unicorn 2.1.4 `tcg_temp_alloc` has no release-build bound check; with a block hook installed the firmware's unrolled
  SHA-256 (guest 0x80d18000..) overflows `temps[]` and zeroes the TCG op list. Fix: end the block when `nb_temps >= TCG_MAX_TEMPS-96`.
* Boot kind: kernel 0x8004c214 compares register `0xBFFE0000` with IOP data words at offsets 0 and 8 (`0xBFFC0000`, `0xBFFC0008`), then
  checks the standby-wake magic `0x2012` in `0xBFFE042C` (written by 0x8005a808 before standby). With an all-zero window the first compare is
  equal, the firmware takes the "A" path, the app decides standby and runs the power-off wait loop (prints "Wait turn off power", polls
  `0xBFFE0434` bit 15). Boot kind B (`0xBFFE0000`=1, IOP[0]=0, IOP[8]=1) avoids it. Which kind a real power-on produces is unknown.
* Audio thread (0x80dd65xx): reads `0xBFFE1DB0 & 0xf`; value 4 triggers "AUDIF_Reset_DSP: force to reload dsp codec again" every pass
  (module loader 0x80dd8670 + checksum 0x80ddad34, ~1.5M blocks per pass). `--state-default 1` (any value != 4) stops the reload loop.
* Main app: `DRV_OSD_CreateRegion` and the palette setter (0x80dc606c, writes the CLUT address/size at window+0x28/+0x2c) run; the app event pump
  delivers events 0x1118/0x1017/0x1003 repeatedly. Host call `0x80d42c34(0x17,0)` (monitor "mmenu") runs without error.
* Drawing goes through the **SUNPLUS GPU command FIFO** (DirectFB gfx driver `spgraphic`, kernel blob 0x8016c400..0x80171800):
  - regs 0xBFFE3580 ctrl (init 0x181f), 0x358C FIFO base>>10, 0x3590 write pointer (kick, 16-bit byte offset), 0x3594 read ptr/flags,
    0x3598 status (bit0 busy, bit1/2 idle), 0x35A4 enable. FIFO ring is 0x8000 bytes (struct at 0x809c66dc).
  - command word = `(reg << 24) | data`; `0xA0000000 | n | flags` executes command n (0 = rect fill/draw, 3 = blit, 5 = blit variant, flags 0x800...).
  - regs seen: 50/51 dst base lo/hi16, 56 dst size (w-1,h-1)<<12.., 60/61 src base, 66 src size, 70 formats, 73/74 colors, 75/76 rect, 77/78 third
    address, 80/81 fill dst, 90/91/92 rectangle geometry. Exact semantics still to be derived (driver functions 0x8016e9xx..0x80171700).
* `SUNPLUS_GPU_TRACE=1` prints every kick's command words; at the first 88 kicks all referenced buffers (around 0x04a7aa00..0x04eb0000) are still empty.

## Debug switches added to the runner (environment variables)
`SUNPLUS_UART_PC`, `SUNPLUS_WATCH_RING`, `SUNPLUS_WATCH_FROM`, `SUNPLUS_STOP_AT_PC`, `SUNPLUS_READ_TRACE=lo:hi`, `SUNPLUS_SPI_TRACE=lo:hi`,
`SUNPLUS_GPU_TRACE`, `SUNPLUS_STOP_AT_KICK`, `SUNPLUS_HEAPCHECK`, `SUNPLUS_FLUSH_TB_IDLES`; plus `--save-ram FILE` (registers + RAM).

## GPU model status (tools/sunplus_gpu.hpp, `--gpu`, exploratory)
Surface convention derived from the driver: base = ((reg[base+1]&0xffff)<<16 | reg[base]&0xffff) & 0x0fffffff, tightly packed ARGB8888,
dims reg = (width-1)<<12 | (height-1); pairs are (x:hi12, y:lo12) and (w:hi12, h:lo12). Dst of blits = regs 50/51/56, src = 60/61/66 (fill uses 60/61/66 as its dst),
rect/position regs 90 (src x,y) 91 (w,h) 92 (dst x,y) 94 (stretch dst w,h), fill rect 80/81, colour 73/74, colour key 71.
With the menu/source host calls the firmware issues only 88 GPU commands (15 fill, 62 blit, 11 stretch) in the whole 2.5G-block run; all source
surfaces are still empty at that point, so the rendered output is flat fill colour. The GPU is therefore not the first missing piece: the app is
idle before it draws the real UI. Public searches (Sunplus site, mikrocontroller.net repair thread, GPL/driver searches) found no register documentation;
the real device's boot log matches ours up to the DirectFB "Default 0.3" line.

## MILESTONE: the original firmware's own UI is drawn (GPU model corrected)
Two corrections made the recorded 88-command FIFO produce real UI: (1) surfaces are 8 bits per pixel palette indices (LUT8), tightly packed rows of
`width` bytes; (2) for blits regs 0x50/0x51/0x56 are the SOURCE and 0x60/0x61/0x66 the DESTINATION (positions only fit this way; clipped pixels dropped
from 851,190 to 0). Result: the surface at 0x04e815d0 (390x165) is the firmware's "no channels" dialog: a black header with the red title "ЦТВ"
(DTV) and a blue body "Нет каналов" (no channels, Russian localisation). See evidence/sunplus/no-channels-dialog-false-colour.png (placeholder palette, superseded:
the real CLUT is not located yet; this is layout only). The dialog is composited onto the 1216x660 OSD buffer at 0x04a7aa00 at (453,200).

### Palette found
The OSD region descriptors (table at 0x8085df78, 24 x 12 bytes {handle, descriptor, ..}) hold the CLUT pointer at descriptor+0xc (set by
`DRV_OSD_SetClutPtr` = 0x80dc5544, flag +0x10): 256 x (A,R,G,B) bytes at 0x85b217b0 / 0x85b22780 / 0x85b23750 (identical in the three windows; entry 0 = transparent
magenta key, 1..~200 grey gradient, 228 orange (255,193,30), 255 light grey A=205). `tools/sunplus/render_surface.py` uses it:
evidence/sunplus/no-channels-dialog.png is the dialog in the firmware's own colours. (The 256-entry table written through 0xBFFEA1C8/1CC/1D0/1D4 is a
smooth colour ramp loaded once by a video module, not the OSD palette.) `DRV_OSD_SetWindow`-like 0x80dc606c is called as (window 2, 1320, 1366, 750, 768).

## UI event sweep (host call `0x80d42c34(event,0)` at idle wake 60000, V2 config, 330M blocks, GPU model on)
Of 164 event ids 0x109d..0x1140 most change nothing (GPU stays at the base 88 commands); these draw more: 0x109f 0x10a0 (52 fill / 154 blit / 32 stretch, the largest) 0x10a1..0x10a4 0x10a7 0x10a9
0x10ab 0x10ac 0x10b5 0x1120 0x1121 0x1129..0x112b. Event 0x10a0 draws a menu-style window (806x517 at 0x04e4c480: ruled list rows and the hint bar "Enter Ввод / Exit Выход / Menu Назад")
and a red "ПО не найдено" ("software not found") line: the USB software-update screen. See evidence/sunplus/menu-window-event-10a0.png.
Strings: "Нет каналов"/"ЦТВ" (ISO-8859-5 at 0x80fcb57c/0x80fc69bc) is the DTV no-channels popup; "Нет сигнала" (no signal, 0x80f383d4 in the Russian table) is used for
non-DTV sources, which the emulated TV does not select (boots to DTV).

## MILESTONE 2: the Manta LED4202 firmware draws its own main menu
The Manta LED4202 image (kernel blob `lzma_059070.bin`, flash `Manta_LED4202_CV9202H-A39_25Q32FV.bin`, same hypotheses file `hyp-v2.tsv` and options, WITHOUT the Shivaki-specific `--state-follow`)
boots to the same DirectFB point and issues 297 GPU kicks (58 fill / 212 blit / 27 stretch) with no host calls. `SUNPLUS_BEST_SCREEN=4a17a00,1216,660 SUNPLUS_BEST_FILE=f` keeps the OSD buffer state with the most
drawn pixels; rendered with the firmware's CLUT (0x85cbf4b8) it is the main menu: title "Картина" (Picture), sidebar Картина / Звук / Канал / Особенности / Уст, rows
Режим изоб. Динамично, Яркость 55, Контраст 55, Резкость 5, Цветность 55, Оттенок 50, Цветовая температура Стандар, Формат экрана 16:9, hint bar "Перем. / Ok Ввод / Exit Выход / Menu Возвр.".
evidence/sunplus/manta-led4202-main-menu.png (1216x660 OSD buffer) and ...-1366x768-approx-position.png (placement is a guess; scan-out registers still unlocated). Some sidebar labels use
other CLUT entries than the table used here (dark text on dark), so colours there are not final. The default OSD language is Russian in both images (language/region selection and persisted settings not modelled).

### GPU model corrections that fixed the menu (Manta)
* All rectangle sizes are stored as (width-1, height-1): regs 0x81 (fill) and 0x91 (blit/stretch source). Dropping the "+1" lost every 1-pixel slice.
* Command 5 (stretch): regs 0x75/0x76 are the DESTINATION rectangle as inclusive (x0,y0)-(x1,y1); 0x90/0x91 select the source slice (typically a 1-px column/row of a 3-slice button or a 4-px bar)
  that is stretched (nearest) over it. For ordinary blits 0x75/0x76 are the destination clip rectangle and 0x92 the destination position.
* Result: evidence/sunplus/manta-led4202-main-menu.png is the complete "Картина" menu (silver sidebar buttons with the selected one in blue, rounded black title capsule, blue value pills,
  greyed disabled entry, scroll arrows, hint bar). The earlier "broken" look (dark unreadable labels, missing separators) was the missing 1-px stretch slices.

## Language: it is the saved COUNTRY setting in the user's own flash dump
The Manta dump stores the system settings (0x1d bytes) at flash 0x3e6da2: `00 00 0b 0d 0d 0c 05 00 00 31 01 ...`. Byte 2 is the country index (0x0b = 11). Firmware 0x8126cdd4 reads it,
maps it through a jump table (0x80f8df54) to a language code, and 0x81244030 maps the code to a language id (11 -> code 0x78 -> language 7 = Russian; unmatched codes such as index 1 -> English).
So the Russian UI is what this TV was actually set to (country Russia), not an emulator default. `tools/sunplus/set_country.py IN OUT 1` writes a copy with country 1 (user data only);
booted with it the menu is English (evidence/sunplus/manta-led4202-main-menu-english.png). No firmware code is patched.

## MILESTONE 3: the Manta menu is navigable
Key events are posted with the firmware's own event entry (Manta: `0x80cee834(event, 0)`, the equivalent of Shivaki's `0x80d42c34`; reached from a host call on the idle thread).
Found by sweeping event ids 0x1001..0x10a0 and diffing the OSD buffer (`SUNPLUS_DUMP_IDLES`, `navprobe`): 
| key | event | evidence |
| --- | --- | --- |
| MENU | 0x1021 | opens the main menu (+291 GPU draws) |
| UP / DOWN | 0x1018 / 0x1019 | move the focus ring (from "Picture Mode" both reach "Colour Temperature": the only other enabled row) / move the list highlight |
| LEFT / RIGHT | 0x101a / 0x101b | focus back to the sidebar / into the settings pane |
| OK | 0x101c | opens the "Dynamic / Standard / Mild / User" list; second OK selects |
| EXIT | 0x1043 | clears the OSD buffer (closes the menu after the OSD timeout/animation) |
| POWER | 0x1017 | enters the firmware's power-off path ("Wait turn off power") |
Other ids draw dialogs: 0x103f "DTV / Database Empty", 0x1069 "CH 001" banner, 0x10a0 "Flip Mode has been changed, Confirm to flip?" (OK / Cancel).
Session evidence: evidence/sunplus/manta-menu-navigation-session.png (boot, MENU, RIGHT, OK, DOWN, OK, EXIT). Selecting "Standard" makes the firmware apply its preset (Brightness 55 -> 50).
Tools: `sunplus_run --cmd-file F --realtime-after N --key-fn ADDR` polls F for `key 0xHEX` / `shot PATH` lines (dumps the OSD buffer and CLUT); `tools/sunplus/menu_viewer.py` drives it
(headless `--script "wait 75,key MENU,shot x.png,..."` verified; the Tk window mode is untested). The OSD buffer 0x04a17a00 (1216x660) and CLUT 0x85cbf4b8 are specific to the Manta image/config.
Not modelled yet: display scan-out registers (placement is a guess), video plane behind the OSD, IR hardware (keys are injected as events), persistence of changed settings.
