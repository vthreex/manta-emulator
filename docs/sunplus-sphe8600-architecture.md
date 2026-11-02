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

## First light (tools/sunplus/firstlight.py)
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
