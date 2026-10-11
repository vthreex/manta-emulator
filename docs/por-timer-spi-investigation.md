# POR, timer and SPI investigation

Historical checkpoint for commit 4cf77a4. The CF identification label below was incorrect; current SPI, MIU, scaler and gamma results supersede this checkpoint in [SPI/display continuation](spi-and-display-continuation.md).

Strict candidate boot still stops at **80469898 / BF200A38**. Its reset/producer contents are unresolved. A separate guarded diagnostic continuation reaches **8053C6EC / BF001030**, after 104,577,792 original guest dispatches. Four POR-origin RAM bytes remain invalid. No GOP initializer or authentic frame has been reached. No guest opcode or PC is patched.

## POR observation cache

At 80469898, `94430000: lhu v1,0(v0)` reads BF200A38. The function starts at 804697D4; caller 804887B8 invokes it after I2C initialization. It caches BF206720/6724 at 806B913C/9134, BF200A38 at 806B914A, and BF200A3C at 806B910E. The last SH executes in the JAL delay slot at 804698B8.

This is an unknown-register rejection, not a polling loop or exception. Halfword addresses and little-endian ordering are preserved. No write to BF200A38/3C was observed during flash boot. Later service-display code at 804BCE2C/804BCDF0 counts their set bits and calls decimal Unicode formatter 8049E464, through dispatch 804BC7F4.

The strongest interpretation is **POR_STATUS scratch registers E/F**, not verified Titania4 reset data. [Titania4 RIU research](https://linux-chenxing.org/titania4/riu-map.html) explicitly marks bank 100500 POR_STATUS as an assumption. Logical 10051C/10051E map to BF200A38/3C. The [MST786 datasheet](https://linux-chenxing.org/cedric/mst786_ds_v01maite_new.pdf), pages 114-116, describes RW scratch registers with zero reset on that different processor. This does not prove Titania4 values; zero was not adopted.

Optional `--defer-diagnostic-reads` byte-verifies the two original load/cache paths. It marks the register and cached bytes invalid, allows only those exact original SH stores, and stops before a load, branch, calculation, address or device command consumes them. The engine carries a poison representation, never a valid POR observation. Native tests cover branch/store rejection, first cached read, overwrite and conditional moves. This is diagnostic continuation, not a resolved peripheral.

## RTC and original timer ISR

Original 805482E4 initializes RTC0/1 at BF002400/2600, divider 12,000,000 at +04/+08. 80548380 programs load +0C/+10 and pulses bit 3. 8054841C pulses bit 4, then reads coherent snapshots +20/+24. The encountered blocker was 8054843C / BF002420. `--virtual-rtc` implements these guest commands. The stock offsets/strobes match a corroborating [Linux RTC driver](https://github.com/torvalds/linux/blob/master/drivers/rtc/rtc-msc313.c), which targets a different SoC. RTC alarms/IRQ remain unsupported.

The next barrier was 805CBBD8 waiting for the ticker at 806E561C, read through 805CB6D4. Original 805CB6E4 writes CP0 Count=0 and Compare=156000. Unicorn 2.1.4 returned zero for Count and disabled guest Count/Compare writes. The source extension restores these helpers, exposes deterministic stepping, latches Cause.TI/timer IP, acknowledges Compare writes and requests architectural interrupt entry at a safe translated-block boundary. QEMU handles EPC/BD, EXL and ERET. The stock vector at 803B0180 runs **46 times**, and the stock ISR advances its own ticker to **46**. The host never writes that ticker.

Pacing is explicitly approximate: one dispatch per tick at 312 MHz, Count at half that rate; RTC uses a virtual 12 MHz oscillator divided by guest configuration. These are not measured silicon cycles. Native tests execute a guest ISR/ERET and cover pending, acknowledgement and Count wrap. Windows source builds also needed fixes for the upstream x64 callback shadow space/alignment and MIPS CPU allocation alignment. Matching engine source and a reproducible patch are included.

## UTMI and SPI boundaries

At 804F1094, stock code polls BF207578 bit 1 after clock reset/enable 0A/28 at BF200E00, CA_START bit 0, ten guest ticks and release. Port two repeats at BF200F00/BF207478. A [MStar UTMI header](https://github.com/linux-chenxing/linux/blob/mstar_v5_14_rebase/include/linux/usb/mstar_utmi.h) and [driver](https://github.com/linux-chenxing/linux/blob/mstar_v5_14_rebase/drivers/phy/phy-msc313-usb.c) corroborate offset 78 and start/end bits. Other-chip analogue data is not copied.

`--candidate-utmi` requires that clock/start/hold/release sequence, then a provisional completion deadline. Only the verified CA_END consumer receives the status bit; analogue data/power-good stay unknown. New stock UART output includes `USB_Init_Configure`, `USB port 0 enter power save done`, `done`, and `USB_Init_Configure done...`. USB enumeration/media playback are not implemented.

Original 8053DD84 initializes SPI, with divider 40 at BF001018. Caller chain: 8053C0FC -> 8053DB88 -> 8053C574. BF001000=AAAA enables ISP; +04 holds ordinary commands; +10 transmit data; +14 receive data; +20 ends a transaction; +30 strobes a read; +54/+58/+5C contain read-data/write-data/command-ready bit 0; +A8=3333/2222 enters/leaves trigger mode. The MST786 ISP table, pages 26-33, corroborates this subset against actual stock access sequences.

`--candidate-spi` requires guest clock configuration and enable, models FIFO readiness after virtual deadlines, and provides only known idle WIP/WEL bits for opcode 05. Native tests additionally cover candidate W25Q64BV JEDEC bytes and bounded backing reads; **stock execution has not reached those commands**. Guest program/erase/protection/settings persistence remain unsupported.

The packet at this checkpoint is **CF FF FF 00**, followed by a receive strobe at **8053C6EC / BF001030**. The old error text incorrectly called CF REMS2. Matching MStar code calls it REMS4; Macronix documents CF REMS4D for MX25L6445E and EF REMS2 for MX25L6405D. The supplied service schematic shows U8 MX25L3205DM2I-12G on a different board, with no output pull-up drawn. The Manta repair reference associates a Winbond 25Q64BV, which does not document CF. Image bytes cannot prove chip marking or undocumented output behavior. See the corrected evidence and separately configured electrical experiments in [the continuation report](spi-and-display-continuation.md).

## Graphics and PDF cross-reference

Static references identify MDrv_GOP_Init at 80539CE4 (caller 80533670), window info around 80538948, enable 80538DB8, format 80538F70 and blending 8053912C. Helpers 8053A140/8053A210/8053A43C use a base stored at context+4, bank selector base+3FFC bits 0..3, and twice the encoded low-16-bit offset. Bank bits come from encoded bits 16..19 and are saved/restored. These logical banks must not become separate physical RIU addresses.

Initialization writes encoded 1F00/1F02 and 1F6A/1F6C, clearing descriptor fields at 11F00 + window*40. Guest framebuffer addresses are shifted by three (8-byte units), subtracting MIU1 base 08000000 when applicable; pitch is also shifted by three. Allocation lifetimes, pixel formats, palettes, GE commands, font rendering and panel IRQs remain unresolved. Recorded GOP/GE aperture events are zero.

Requested PDF sections were text-inspected and rendered: 13-20, 29-36, 120-123 and 159-162. Visual checks included GPIO/I2C, HDMI, flash topology, graphics, MAX3543 and factory debugging. Page 31 describes graphics/LVDS capability for MSD308PX, not Manta registers. Page 161 describes BMTEST DDR phase testing and UART register debugging. The MAX3543 appendix does not identify the Manta tuner. Reference board MSD309 V7.0-A/U7 MSD309PX is not proven compatible with 471-0101-D3202G.

To resolve remaining uncertainty, identify the actual SPI marking/revision and obtain passive, electrically verified stock-boot CF/9F captures or an existing read-only debug observation of receive data. Determine input pull configuration where flash outputs are undriven. Capture POR registers and their caches using a verified read-only debug path, correlating them with calibration and boot-ROM code. No physical TV was accessed, modified or flashed.

Evidence: [strict POR summary](../evidence/por-strict-summary.json), [guarded trace](../evidence/guarded-boot.jsonl), [summary](../evidence/guarded-boot-summary.json), [UART](../evidence/guarded-uart-console.log), [regressions](../evidence/regression.json), [static references](../evidence/runtime-investigation.json), [native checks](../evidence/ctest.log).
