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
