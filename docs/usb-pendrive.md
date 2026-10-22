# Virtual USB pendrive (`--exploratory-usb-pendrive IMAGE`)

EXPLORATORY and opt-in. Strict mode is unchanged (the option is off by default and nothing here runs without it). Everything below that is not
read from the original firmware is a labelled assumption, **not** a Titania4/MSD309 measurement.

## What the original driver tells us (static + dynamic evidence)

* Port 0 register bases come from the table at `0x80658000` (initialised data): UHC `BF204800`, UTMI `BF207500`, USBC `BF200E00`
  (port 1 of the firmware: UHC `BF201A00`, UTMI `BF207400`, USBC `BF200F00`). Registers are 16-bit RIU registers on a 4-byte stride
  (`803BD8F8` & co: byte index `i` lives at `base + (i & ~1) * 2 + (i & 1)`).
* The UHC register layout is the Faraday **FOTG210** EHCI layout (Linux `fotg210.h`): `CAPLENGTH = 0x10`, `USBCMD 0x10`, `USBSTS 0x14`,
  `USBINTR 0x18`, `FRINDEX 0x1C`, `PERIODICLISTBASE 0x24`, `ASYNCLISTADDR 0x28`, `PORTSC 0x30` (bit0 CCS, bit1 CSC, bit2 PE, bit8 PR).
  Evidence in the firmware: `UHC[0x10] & 0x20` (ASE) is cleared in a loop; `UHC[0x31] & 1` (PORTSC.PR) is polled after a port reset; the
  QH `info1` word is edited with `ins ..., 0xc, 2` (endpoint speed) and bit 7 is cleared (inactivate-on-next-transaction), i.e. standard EHCI
  queue heads built in DRAM.
* The driver programs **KSEG1 aliases** (`0xA070CCA0`) into `ASYNCLISTADDR`; the model masks the low 29 bits like the MIU does.
* The driver uses the **dummy-qTD technique**: it copies a new qTD *into* the qTD that `QH.qtd_next` already points at (inactive), so the
  controller must leave `QH.qtd_next` pointing at an inactive qTD instead of advancing past it.
* `UHC[0x41]` bits 2:1 are the detected host speed (`" Host type:%x"` log string); `2` selects the high-speed branch.
* `BF203D98` / `BF203D9D` are read by the init state machine (`8054E3C0` family) as chip id / revision; the project's existing inferred
  Titania4 chip id `0x18` (`BF003D98`, `--candidate-chip-id`) is supplied for the `BF203D98` mirror and revision `0` for `BF203D9D`
  (`--exploratory-mmio-bits 1f203d98:ff:18 --exploratory-mmio-bits 1f203d9d:ff:00`). Mirror assumption, unverified.

## What the model does

`src/peripherals/usb_host.hpp`:

* **Attach**: the device appears once the guest has powered the port-0 PHY (UTMI PWRCTRL bit 15 = 0); USBC+0x10 line state reports D+ (full-speed J),
  PORTSC.CCS/CSC set. The `--exploratory-usb-no-device` hypothesis is superseded for port 0 when a pendrive is attached.
* **Port reset**: `PORTSC.PR` completes after a provisional 20,000 guest ticks (or when the guest clears it); the device is then high-speed.
* **Host controller**: USBCMD/USBSTS/FRINDEX/PORTSC and the extension block `0x34..0x4F` (plain storage, reset 0 assumed). The asynchronous
  schedule is walked while `RUN && ASE`; qTDs (SETUP / IN / OUT, buffers across five pages, data-toggle bit, IOC, short-packet alternate pointer)
  are executed against the device and written back to DRAM. No periodic schedule, no interrupt line to the CPU (the firmware polls).
* **Device**: one bulk-only mass-storage function (standard descriptors, GET_MAX_LUN, BOT CBW/CSW) with SCSI TEST UNIT READY, INQUIRY,
  REQUEST SENSE, MODE SENSE(6/10), READ CAPACITY(10), READ FORMAT CAPACITIES, READ(10), WRITE(10), START STOP, PREVENT/ALLOW. Writes stay in
  memory (the image file is never modified) and are part of snapshots as dirty blocks.
* **Snapshots**: a `usb` section holds controller + device state; restoring checks an FNV-1a hash of the image file.

## Image tool

`python tools/make_pendrive_image.py OUT.img DIR [--size-mib 128]` builds an MBR + FAT32 image (VFAT long names, 2 sectors/cluster) from a folder.

## Results (original firmware, no patches)

From the NO SIGNAL checkpoint: SOURCE, DOWN x5, OK (USB). The firmware enumerates the stick (GET_DESCRIPTOR device/config/strings,
SET_ADDRESS, SET_CONFIGURATION, GET_MAX_LUN, INQUIRY, READ CAPACITY, READ(10) of the MBR, boot sector, FAT and directory) and the original
media player changes from "No Device" to **"USB 2.0"** with PHOTO / MUSIC / MOVIE / TEXT. OK on PHOTO shows the original drive page
("Return", "C", "1 / 1"): the firmware parsed the partition table and FAT32.

## Not verified / open

* Reset values of the MStar UHC extension registers, the port-reset duration, high-speed signalling, the chip-id mirror and every
  value above marked as assumed.
* Photo decode needs the hardware JPEG decoder (JPD) and the video/MVOP path: not modeled.
* Video (MVD/H.264/MPEG) decode and audio output: not modeled.

## With the coprocessor and JPEG block (`--exploratory-copro --exploratory-copro-jpd ...`, IRQ-capable engine in `work/build-irq`)
The USB photo browser now runs end to end from a checkpoint saved at the drive page (`copro-drive.snap`, made with the pendrive attached from 1.3B): the original media player enumerates the stick,
shows C:\ with Return / PHOTO, and the file browser's thumbnail requests go through the coprocessor's STILLIMAGE path: Gradient.jpg (1280x720, scale 1) and Test_Pattern.jpg (640x480) are decoded by the
JPD model, and 250 YUV422 -> ARGB1555 GE stretches (`--exploratory-ge-yuv-stretch`, BT.601 full-range, nearest; all labelled assumptions) were retired in a 2.345B-dispatch run without a stop.
Note: `--diagnostic-riu-zero-unknown-reads` must keep the USB line-state hypothesis (fixed): without it the guest never saw the stick.

## Photo on screen (exploratory presentation, `--exploratory-mvop-plane`)
The original MVOP HAL (writes from `80545xxx`) programs RIU `0x1F202884/88` = frame base / 8 (`0x00646000` -> `0x03230000`, the framebuffer the GE stretch just filled), `0x1F202894` = width (1904), `0x1F202898` = height (1050).
The preview shows that framebuffer (assumed packed YUV 4:2:2, BT.601 full range, nearest scaled to the canvas) behind the GOP layers; with `--exploratory-video-plane black --exploratory-gop-black-colorkey` (a labelled hypothesis: opaque black OSD pixels are see-through)
the opened photo appears behind the browser: `evidence/usb-photo-mvop-plane-diagnostic.png` (Test_Pattern.jpg: JPEG decoded by the JPD model, stretched by the original GE commands). No MVOP enable bit is modelled, so a stale picture can remain after leaving the viewer.

## Videos on the stick (session 12, exploratory probe, nothing plays yet)
Stick with `/MOVIE/*` clips (320x240, 3 s, ffmpeg test sources), media browser MOVIE tab, snapshot flow `copro-ns-vid2` -> `vid2-drivelist` (stick image is part of the snapshot fingerprint: a new stick needs new snapshots).
* All files are listed. Pressing OK on a file: **H.264/AAC `.mp4` is accepted** (red check mark, no popup); **XviD `.avi`, DivX `.avi`, H.264 `.mkv`, MPEG-2 `.mpg` (MP3/MP2 audio) show the firmware's own "Unsupported File" popup** (`804E4EA0`, called from `804E380C`).
* With watches on `MDrv_MVD_Init` (`8051145C`), `HAL_MVD_LoadCode` (`80513650`), `MDrv_MVD_SetCodecInfo` (`80512C10`) and "Enter Mvd Page" (`80461858`): **none runs** in any case, so the verdict is software only, before any decoder hardware. The mp4 mark also starts no decoder; the playback key (PLAY) is not identified, a second OK did nothing visible.
* Unknown: why the other containers/audio are refused (candidates: the MP3/MP2 audio, the probe reading the stick). Next cheap test: the same clips with AAC or no audio; then find the PLAY key.
* The firmware contains MVD (`MDrv_MVD_*`, own firmware via `HAL_MVD_LoadCode`) and HVD (H.264) drivers; neither decoder is modelled.

## Run speed note (session 12)
A profile of a 650M-dispatch headless run showed 97% of the time in the preview composer (`collectGopLayers` / `composeGopPreview` plus memcpy), because `--gop-frame-history` composes the full 1920x1080 preview every 2^14 dispatches. `--gop-history-interval-log2 N` (diagnostic, default 14 = unchanged output) makes it coarser; with N=20 the same run took 2.8 min instead of 32 min. Remaining top cost: `codeHook` 17%, preview 12%, OR1K core 6%, `pushRecent` 5%.
