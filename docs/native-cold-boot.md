# Native cold boot and the live page's Reboot

The browser build (and `manta_native_full --native-cpu`) runs only statically recompiled firmware code; it has no interpreter. Until now it could
only take over a save state. With the boot code recompiled too, it boots from the **reset vector** like the real SoC, from whatever the flash holds.

## What a cold boot runs

| stage | flash bytes | executed at | recompiled region |
|---|---|---|---|
| boot ROM | `0x0000-0x2C60` | `0xBFC00000` (KSEG1), partly through its KSEG0 alias `0x9FC00000` | `BOOTK1`, `BOOTK0` |
| second stage | `0x2C60-0x85C0` | copied by the boot ROM to `0x81000000` | `STAGE2` |
| runtime | `0x20080-0x23880` + LZMA stream at `0x23880` | `0x803B0180` (inflated by the second stage) | `GEN` (existing `native/gen`) |

`python native/tools/gen_boot.py FIRMWARE.bin` writes the three extra regions into `native/gen` (derived from the user's own image, not committed).
`mips_recomp.py --prefix NAME --all-entries` gives each region its own tables, which register themselves with the runtime dispatcher
(`native_register_region`); every aligned word is an entry because the boot code uses computed jumps. Re-run CMake after generating.

## Fixes the cold path needed (all engine fixes, no firmware patch)

* **Device clock inside a run**: device time only advanced between translated runs, so the boot ROM's MIU BIST wait (`BFC023F0`, a poll that stays
  inside one run) never saw its done bit. The native bus now brings the clock up to date before every MMIO access.
* **CP0 Cause IV/DC**: `CP0W` OR-ed the written IV/DC bits into the old value and never cleared them. The boot ROM sets IV (`BFC00014`), the runtime
  clears it (`803B0600`, `805CB660`, ...), but the stale IV sent the first timer interrupt to `EBase+0x200` (the middle of the handler) and corrupted
  the stack. Snapshot takeovers never noticed (they start with IV = 0).
* **GOP palette, native cold boot only**: the interpreter recognises the original read-modify-write of palette register 5 (`8053A210`) on a never-written
  register and keeps the unwritten bits unknown, so its palette model ignores it. The native engine has no CPU bit provenance; a palette commit whose
  colour registers were never written now leaves the entry undefined (`native_gop_palette_unknown_color_ignored` event) instead of stopping.
* **Code guard**: the translated code matches only flash whose code areas (`0x0-0x161258`: boot ROM, second stage, runtime header and stream) equal the
  stock image. A cold boot of a flash with different code stops with an explicit reason (it would need the interpreter).

## Results

* Desktop: `--native-cpu --native-cold-boot` with the live profile (no `--load-state`): 1.4 B instructions in 50 s, UART `BIST0-OK ... Hello T4 ...`
  board banner, NO SIGNAL dialog drawn by the original ZUI.
* Browser build (Node): snapshot session -> `manta_save_flash` overlay -> fresh module cold-boots with `--nvram` overlay: NO SIGNAL after 1.4 B
  instructions, 88 s at 16 M/s.
* Snapshot runs are unchanged (same frames for MENU, MENU+DOWN).

## Live page

* The firmware's own whole-chip reset (after a USB update, see docs/usb-pendrive.md) or the **Reboot** button: the worker saves the flash overlay
  (every sector the firmware programmed) and the page starts a fresh machine that cold-boots with it. RAM, CPU and devices start from scratch; the
  save state is not used. **Restart** still goes back to the save state.
* The cold boot under the browser profile still uses its labelled hypotheses (absent input, zero-unknown reads, etc.).
