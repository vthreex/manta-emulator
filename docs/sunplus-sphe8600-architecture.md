# Sunplus SPHE8600 TV platform (Cultraview CV9202) — architecture notes

Status: **exploratory**. Derived from two third-party flash dumps; nothing here is verified against silicon.
The firmware images themselves are not part of this repository (supply your own dump).

Images studied
- Shivaki `STV-24LED14` (Cultraview CV9202L-T, 8 MB SPI flash, 2014-11).
- Manta `LED4202` (Cultraview CV9202H-A39, 25Q32FV = 4 MB SPI flash, 2016-03).

Both are the same platform and software family (not MStar; the Manta LCD4214 emulated elsewhere in this repo is an MStar board).

## Identification (evidence)
- Kernel strings: `SPHE8600_ehci%d`, `SPHE8600_ohci%d`, `Sunplus TV`, `Sunplus SMI/MDIO`, `SunplusDemod`.
- OS: eCos (`cyg_*`, `ecos-he-mj2` source paths, `cyg_ldr_malloc` ELF loader), UI toolkit DirectFB 1.1.0 (`surfacemanager.c`, `gfxcard.c`).
- CPU: MIPS32 little-endian with Config1 (the entry code derives I/D cache geometry from CP0 Config1).
- No `MStar`/`MSD`/`Titania`/MediaTek/Realtek/Novatek markers anywhere.
- Panel board vendor: Cultraview (`APP_MenuMgr_*_Cultraview_Factory_Menu`); Manta image has an `IRMANTA_BIG` key table.

## Flash layout (8 MB Shivaki image; Manta differs only in offsets)
| offset | content |
|---|---|
| `0x0000..0x0fff` | 0xFF |
| `0x1000..0x13ff` | opaque header block (high entropy; not yet understood) |
| `0x1400..0x1a17` | **register init table**, 130 entries of `{addr, value, flags}` (12 bytes each, flags `0x0fff` normal / `0x2000` seen with delay-like writes). Writes into `0xBFFE0xxx..0xBFFE1xxx`; includes UART `0xBFFE0900` 'W','D','G' and the DRAM controller block `0xBFFE10xx/11xx` |
| `0x3030` | stage-1 stub: `lui s6,0xbffe; ori s6,s6,0x8000; b ...; ... j 0x020000a4 (region-relative)` |
| `0x300dc` / `0x30154` | LZMA (alone) stream: OTA/update helper (`APP_OTA_*`), ~340-395 KB |
| `0x54070` / `0x59070` | LZMA stream -> **kernel image**, ~10.3 MB (eCos + DirectFB + drivers), entry `0x80000000` |
| `0xf1f5c` / `0xf6124` | LZMA stream -> **application** (middleware, `PE`/`AVD` decode libraries, menus), ~10.4 MB |
| `0x3bc019` | LZMA stream -> MIPS relocatable ELF (`7f 45 4c 5a`, magic's last byte scrambled) |
| `0x3f0000` | 1920x1080 JPEG boot logo (SHIVAKI on red; a 160x90 EXIF thumbnail follows) |
| tail (`0x7d0000..`) | plain settings DB (`Favorite 1..4`, 2222222 per-source tables) + zlib-wrapped data tables |

Tools (this repo): `tools/sunplus/firstlight.py`; unpack/carve/disassemble helpers live next to the data under `work/sunplus-stv/tools/`.

## Memory map (observed)
- RAM at physical 0 (kernel linked at `0x80000000`; KSEG0/KSEG1 aliases).
- SoC registers at physical `0x1FFE0000` (KSEG1 `0xBFFE0000`); the kernel keeps `$s6 = 0xBFFE8000` as a register-window pointer.
- UART: TX data `0xBFFE0900`, status `0xBFFE0904` (bit0 = TX ready, bit6 = TX idle — both polled by the firmware's own early print routine).
- The early console prints `Timer`, `Cache region`, `Mon`, `Zero BSS` (verified in `firstlight.py`, identical on both images).

## CORRECTION (supersedes the "First light" / open-question text below)
- The kernel blob is loaded at **physical 0x400**, not 0 (entry `0x80000400`, whose first instruction jumps to `0x80000910`). Evidence: with a -0x400 shift every call target in the early code lands on a function entry, on both images; the application image then sits at `0x400 + kernel size` (`0x809e2400` Shivaki, `0x809cc000` Manta LED4202). The earlier "call lands mid-epilogue" problem was this off-by-0x400 load address.
- Flash has named section headers (`bootloader` `0x8010`, `mainSys` `0x54010`, ...) and a boot header at `0x1200` followed by the register init table; the firmware looks the sections up through a flash driver (`pl_flash.c`, device id 3, ops table `0x800054b0`).

## Bring-up runner (tools/sunplus_run.cpp, built as `sunplus_run`)
Runs on the repo's patched Unicorn (CP0 Count/Compare ticking and timer IRQ). Strict by default (unmodelled register read = stop); labelled opt-ins: `--hyp FILE` (value / `or` / `clear` rules, each use counted and reported), `--readback`, `--unknown-zero`, `--map-iop`. The register window is an `mmio_map` (memory hooks mis-handle stores in branch delay slots in this Unicorn; reproduced in isolation). `configs/sunplus/hyp-001.tsv` lists every hypothesis used so far (none is observed hardware).

Console output reached (eCos boot, real firmware code): `MMU, FPU, IntC, Cache, Timer, Cache region, Mon, Zero BSS, Var Init, Plf Init, Get IOP Data, Ctor Init, cyg_start`, GPIO/LED setup text, a DDR PUB (PHY) delay-line dump, `Thermal report`, an I2C bus table, `system_standby in RISC cache`, `Wait turn off power`.
Hypotheses needed so far: clock multiplier `0xBFFE0018` (low 6 bits+1, x6.75 MHz), `0xBFFE1448` bit13 (done after start bit12), `0xBFFE0B08` bit7 (thermal busy clears), `0xBFFE0434` bit11 (ready/lock), IOP window `0xBFFC0000..` as zero RAM.

**Where it stops (not solved):** the event/IOP handler at `0x8004c4c8` asks the standby processor ("IOP") for wake-up/key state through a register mailbox around `0xBFFE0428..0xBFFE0440` (and the `0x172b7c` call interface); with zero replies it takes the `PowerCtl(0)` power-off branch (`0x8004c788`). Needs an IOP/mailbox model (the IOP firmware is probably the 4 KB high-entropy block at flash `0x1c00..0x2c00`, not decoded yet) and a registered SPI-flash device (`PL_FlashInit(3)`). Nothing is reaching the OSD, GE or display yet; the OSD bitmap extraction is independent of this.

## Bring-up status (sunplus_run, Shivaki STV-24LED14)
Reached, in order, with the firmware's own code (all numbers are from runs of `sunplus_run`; every hypothesis is in `configs/sunplus/hyp-001.tsv` / `hyp-002-auto.tsv`):
1. eCos kernel boot -> platform init -> **flash driver registered** (`PL_FlashInit(3)`) -> system header (`mainSys`) lookups succeed once the SPI controller is modelled.
2. **SPI flash controller** (modelled device, `--flash`): registers `0xBFFE0B00..0B3F`; command `0B08 = [opcode:8][flags:8]` (`9F` JEDEC, `90` ID, `03` read 4 bytes, `05` status, `06` write enable, `02` page program), busy = bit 7, address `0B0C` (low 16) / `0B10` (bits 16..23), data `0B14` (low 16) / `0B18` (high 16). The flash is also mapped for execute-in-place at physical `0x18000000` (KSEG0 `0x98000000`); the firmware tells flash addresses from RAM addresses with the word at IOP offset `0x1EEC` (= `0x98000000`, set with `--iop-set`). JEDEC `EF 40 17` matches the driver's chip table. Flash writes (1,493 page programs) are still ignored.
3. **Module loader** reads ~3 MB of flash through the SPI controller (the application image) and the **application starts**: console shows `StartUp_Assistant_task Start!` and `HPD Pull Low/High`.
4. The "power off" decision at `0x8004c4c8` depends on `standby_status_dtv` (`0xBFFE006C`, 0 = wake from standby) and on settings stored in flash (NVM, e.g. key `0x16f4` -> byte at flash `0x7d40ac`); the settings area is `0x7d29b8..` in this dump.
5. eCos idle: the idle thread executes `wait`; the runner advances CP0 Count to Compare (`--max-idle`) so timer interrupts run the scheduler.
Interrupt controller layout (from the kernel's unmask table and a test interrupt): CPU lines IP2..IP6 map to mask registers `0x49C` (vec 0x40-0x47), `0x4A0` (0x48-0x5B), `0x4A4` (0x5C-0x5F), `0x4A8` (0x21-0x30), `0x4AC` (0x31-0x3F), vec `0x20` = `0x4B0` (timer, IM7); the dispatcher reads pending registers `0x4CC/4D0/4D4/4D8` for IP2..IP5.
**Where it is stuck now:** all threads block (idle). The GUI side waits in `DRV_OSD_WaitForVisibleBufferChange`-style calls for a display vsync interrupt that is not delivered; no OSD region has been created yet, nothing is drawn. Next: find the OSD/vsync interrupt vector (candidates unmasked by drivers: `0x28, 0x40, 0x41, 0x44, 0x48, 0x4b, 0x4e, 0x5a, 0x5e, 0x5f`), model the pending/ack registers, then the OSD region registers (`DRV_OSD_CreateRegion` at ~`0x80dc4034` in the application image) and a frame scan-out from DRAM; then IR input.
Tools: `tools/sunplus/auto_poll.py` derives *labelled, exploratory* hypotheses for stuck polling loops (wait-for-set => `or`, wait-for-clear => `clear`); `fnstrings.py` lists the strings a function references.

## First light (tools/sunplus/firstlight.py) - historical, load address was wrong
Kernel placed at phys 0, entered at `0x80000000` with `$s6=0xBFFE8000` (what the stage-1 stub sets). Result on both images:
`Timer / Cache region / Mon / Zero BSS`, then a call through `lui k0,0x8020; addiu k0,k0,-0x2144` (= `0x801fdebc` Shivaki,
equivalent address in the Manta image) lands in the middle of a function epilogue and returns to `$ra = 0`.
**Open question:** that call target does not match a function entry in the image as decompressed, so something between
the stub and the kernel (relocation / second copy / patched pointer / different load base) is not yet understood. Not resolved; do not
assume the load address.

## Next steps
1. Understand stage 1 (`0x1000` block, `0x1c00..0x2c00` high-entropy region, `0x3030` stub, `j 0x020000a4`) — the on-chip boot ROM is not in the dump, so its copy/decompress contract has to be inferred from the data.
2. Resolve the kernel call target above (load address / relocation).
3. Then model only the registers the firmware reads (strict mode: stop at the first unmodelled read).

## Graphics: where the OSD bitmaps are (found)
- **Boot logos** are standard JPEGs: Shivaki image `0x3f0000` (1920x1080), Manta LED4202 image `0x3b5000` (1920x1080), each with a 160x90 EXIF thumbnail. (The "repeating pattern" in that region is the JPEG's flat-colour data, not encryption.)
- **OSD/menu bitmaps** live in the *application* image (LZMA stream `0xf1f5c` Shivaki / `0xf6124` LED4202), not as PNG/BMP files. They are listed in a table of 24-byte descriptors (little endian):
  `data_ptr, 0x01000000, palette_ptr, (height<<16)|width, bits_per_pixel (8), 5`.
  Pixels are 8 bpp palette indices, rows padded to 4 bytes in most images (a few use another stride - not all decode cleanly yet); the palette is 256 x 4 bytes `A,R,G,B` with entry 0 = colour key (alpha 0, magenta). 632 descriptors / 10 distinct palettes in each image; artwork is shared between the Shivaki and Manta builds (media-browser icons, player controls, `Ok`/`Exit`/`Return` buttons, satellite-dish/gamepad/settings/tools menu icons, 3D mode icons, ...).
- **Load address of the application image** (needed to turn pointers into offsets): `0x809e2400` (Shivaki) and `0x809cc000` (LED4202) - found by voting `lui+addiu` targets against string offsets (`tools/sunplus/findbase3.py`); the kernel blob is linked at `0x80000000` and the application sits right after it.
- Extraction: `tools/sunplus/unpack.py` (LZMA/zlib streams) -> `tools/sunplus/extract_osd.py APP.bin BASE OUTDIR`. Outputs are not committed (copyrighted artwork).
- Not graphics: flash `0x3e2000..0x3ef000` (panel timing names, per-source picture-mode tables); the scrambled-ELF module at `0x3bc019` (626 tiny zlib streams = per-function compressed MIPS code); the 0x6c0000-0x780000 "low entropy" block is mostly tables, not ARGB.
- Open: some descriptors still decode as noise (different stride/format); text is drawn from fonts (TrueType-looking `00 01 00 00` zlib blobs in the flash tail) via the GEL/DirectFB layer, not from these bitmaps.
