# nBox 5800S "BSKA" v35 NAND dump — static analysis (2026-10-30)

Input: `Dump bska v35.zip` (user-supplied, Elektroda-style community drop). Static analysis only: nothing from the image was executed.
Extracted files are NOT committed (vendor firmware); `tools/cramfs_extract.py` reproduces the root filesystem.

## Verdict
- **Not MIPS, not MStar.** The box is an ST **STx7100** set-top SoC: SH-4 (little-endian, `SH4LE`) CPU, two ST231 coprocessors, STAPI-style drivers. The `adb_*`
  modules and an author address at `adblobal.com` identify the hardware/software as **ADB (Advanced Digital Broadcast)**.
- The Unicorn engines used by this project have no SH-4. A Windows `qemu-system-sh4.exe` exists in `work/dl/qemu`, but QEMU has no STx7100 machine.
- The dump is a **real, working Linux root filesystem** (cramfs) plus an unidentified boot/kernel area. The kernel area is not recoverable from this dump (see below).

## Layout of `dump_nand_bska_v35.bin` (20 MiB)
| Offset | Size | What |
|---|---|---|
| 0x0000 | 12 B | header, magic bytes `00 DB BA AD` (read LE: 0xADBADB00), then `22 0D 00 00`, `04 17 E3 FD`; rest of the 16 KiB block is 0xFF |
| 0x4000 | 0x2C | header, magic 0xADBADB11, count 4, total 0x013D0000, then 0x200, 0x200, 0x400, 0x000D9DC0 (kernel length), 0x000E0000 (rootfs offset from here), 0x012F0000 (rootfs length) |
| 0x4200 | 0x3E | short high-entropy blob (looks like a signature/hash; unverified) |
| 0x4400 | 0xD9DC0 | **kernel area, ~871 KB, uniformly random** (entropy 7.9998, no gzip/lzma/zlib header, no autocorrelation): encrypted or custom-packed. Not decoded. |
| 0xE4000 | ~19.8 MB | **cramfs**, little-endian (`45 3D CD 28`), flags 0x3, 8,974 blocks, 718 inodes |
| ~0x13D1000 | rest | 0xFF |

The accompanying 64 KB file `BSKA v35 A01E0000.bin` is the **NOR** sector at flash address 0xA01E0000 (SH-4 P2 uncached window). It is a table of ~13-byte records
(`11 02 00 00 00 00 00 02 00 00 <4-byte value>` repeated), probably the bootloader's persistent settings/log. Not decoded. `update.txt` is a U-Boot script
(`nand erase 0 1800000; nand write.jffs2 84010000 0 1400000`); the author's note says a backup U-Boot at 0xA0100000 and his own setup are required.

## Root filesystem (cramfs)
- 497 files, 52 dirs, 67 symlinks, 36 MB unpacked, **0 bad blocks** after fixing one vendor quirk: cramfs file sizes are 24-bit; `/root/main.elf` is
  25,323,232 bytes (8,546,016 + 16 MiB). The extractor picks the wrap count whose block table puts block 0 within 0..4400 bytes of the table end.
- Userland: busybox, glibc 2.3.3, gcc 3.4.3 (STMicroelectronics/Linux Base 3.4.3-19), libncurses, iptables.
- Init: `/etc/init.d/rcS` mounts `/dev/mtdblock2` as JFFS2 on `/mnt/flash`, loads modules, brings up DHCP, then runs `/root/eu` and `/root/root.elf`
  (the application, `main.elf`, is started from there). Config flags in `/etc/.profile` (`BOOT_SPLASH_SHOW=1` runs `/root/splash`).
- Kernel modules (`vermagic=2.6.11.12_stm20-33 preempt SH4LE gcc-3.4`): `stpti4_core` (transport demux, STI), `vidout`/`vdisp`/`vidmixer`/`compositor`/`fb`/`avll`
  (STi7100 video outputs and framebuffer), `fdma`, `h264vdec`/`mp2vdec`, `st231_*`/`embx*`/`mme_host` (ST231 coprocessor control, EMBX/MME), `scart`, `hdcp`, `ohci-hcd`,
  `smc91x`, `sc-st7100` (smartcard), `i2c-stb7100`, `ir_stb7100`, `pt6958` (front panel), `abilis_aton2_dvbt` (DVB-T tuner).
- Firmware blobs: `lib/firmware/adb_comp_audio_MME311.bin`, `adb_comp_video_MME311.bin`, `st_comp_video_MME311.bin` (ST231 code), `as102data*.hex` (Abilis tuner).
- `/root/splash` is a 97 KB SH-4 ELF (EM_SH, entry 0x4000E0).
- UI resources: 380 PNG (HD 1080 menu/guide/banner/pvod/xion), 9 MPEG-2 still pictures (`*.m2v`, menu/radio backgrounds in PAL and HD), 2 Java keystores.

## The application
`/root/main.elf` is one 25 MB SH-4 ELF (entry 0x403780): a **Sun CVM (CDC) Java VM** with AOT-compiled Java classes (symbols like `com_adb_xion_ui_dialogs_LoginBox_1_mt`),
the ADB "Xion" UI, a **DVB-MHP** stack (`MHP_SystemInit`, `[OK] PIL_INIT OK, starting MHP client`) and an embedded **Conax** CA client (`Embedded CA - Conax`, `ConaxDrv%d`).

## What emulating it would take
1. A kernel. The kernel area at 0x4400 is not readable. Without it there is no boot path; only the userland can be studied.
2. An STx7100 machine: SH-4 (QEMU has the CPU), but STBus/LMI/FDMA/PTI/VTG/compositor/ST231 coprocessor/EMBX, none of which has a public model here.
   The application sits on those drivers (`/dev/tsmerge`, `/dev/teletext`, OSDHID, STILL, framebuffer), so running `main.elf` in user mode would need a driver
   shim. That would be reimplementation of device behaviour, not an emulation of the box.
3. Conax CA and the secure-boot signature path stay opaque and out of scope.

Status: analysis only. Nothing in the emulator changed. Reproduce the filesystem with
`python -I tools/cramfs_extract.py dump_nand_bska_v35.bin 0xe4000 OUTDIR`.

## UI structure (static)
The UI is not drawn by native code in `main.elf`; it is **Java, ahead-of-time compiled** into the ELF (4,343 classes, found via `<package>_<Class>_mt` symbols;
`tools/aot_class_survey.py`). The layers, top to bottom:
- **Xion**: ADB's own XHTML/CSS toolkit (`com.adb.xion.{dom,css,renderer,net,ui,event,platform,util}`, ~480 classes; `dvbhtml` adds DVB-HTML DOM/renderer, ~140).
  `xion.ui` holds the widget layer: dialogs (`BaseDialogBox`, `LoginBox`, `PromptBox`, `InputEntry`), `ScrollPane`/`ScrollBar`, progress bars, border looks
  (solid/dashed/dotted/carved/embedded lines). Look-and-feel is skinned (`*Look` classes), so the menu/guide appearance is data plus PNG assets, not hard-coded layout.
- **HAVi UI** (`org.havi.ui`, `com.adb.havi.ui`) widgets and `HScreenDevice` (the MHP graphics model), with `org.dvb.ui.FontFactory`.
- **MHP/OCAP middleware**: `org.dvb.*` (application, si, dsmcc, event, net, media, pvr, tvanytime), `org.davic.*` (mpeg sections, net tuning, CA), `javax.tv.*`
  (service selection/navigation/guide), `org.ocap.*` (DVR, shared media), `com.adb.tv.*` (services/protocols), `com.adb.dvb.si` (SI database, NIT).
  Entry points seen as strings: `MHP_SystemInit`, `MHP_DriversInit`, `PIL_INIT OK, starting MHP client`.
- **Security/CA**: `com.adb.security.*` (certificate chains, digest checks, root-certificate manager), `org.davic.net.ca` (43 classes), Conax drivers (`ConaxDrv%d`).
- **Platform**: `com.sun.cdc.io`, `com.sun.net.ssl`, `sun.net.www.protocol` (CVM/CDC class library), JavaScript-free XHTML via `com.adb.xion.net`.
The nBox-specific application (channel list, EPG, menus, settings) is **not** among these classes: no `pl.*`/nBox package appears. It is most likely an MHP Xlet delivered
over the air via DSM-CC or stored in `/mnt/flash` (JFFS2, not in this dump), which fits the `PersistentManager` and application-storage classes. Hence the dump alone cannot show the real menus.

### Assets (`/opt/uapp01/app/res`)
- `images_new/` 380 PNG, each in three tiers: `pal_*`, `hd_720_*`, `hd_1080_*` (so the UI is built for 720x576, 1280x720 and 1920x1080 outputs).
  Folders: `banner` (139: channel banner, screensaver frames 98x98, up to 1629x166), `common` (96: RCU key glyphs, colour keys, time/clock dials, ticks),
  `guide` (27: 1623x61 tab bookmarks), `menu` (51: aspect-ratio icons tv_169/tv_43 normal/stretch/zoom, parental ratings 7/12/16/18, tuner on/off),
  `pvod` (27: BACK/OK/INFO button caps, parental icons), `messages`, `xion` (browser alert/prompt icons).
- `stills/` 9 MPEG-2 intra frames (`*.m2v`): PAL 720x576 and HD 1080 backgrounds `menu`, `radio`, `pvod`, `box`. The HD menu still is a blue gradient with the "n HD" logo
  bottom-left and two light bars the menu text sits on; the radio still adds a microphone.
- `certificates/` two Java keystores (`box_b-cert.jks`, `cacert_keystore.jks`), not opened.
The stills and PNGs are displayed by hardware planes (STILL driver for the MPEG-2 background, OSDHID/framebuffer for the PNG overlays), consistent with `/dev/...` strings in `main.elf`.
