# Bring-up of other firmware images (`--unverified-firmware`)

Everything in the board model comes from the stock Manta image. Another image is loaded with `--unverified-firmware` (bytes never modified; shorter images padded with 0xFF; the stock-only
hash guards for the PM51 program and the inflated image are skipped; the blanket zero-unknown-read hypothesis keeps bytes the guest itself wrote). Each stop is a piece of hardware the model does not know.

## <downloads>/OKbin.bin (3.3 MB, MStar boot stub with "Hello A7", `<Utopia> [CVT_Only] [7.3.11] [V29] [CL676357]`)
Static + dynamic findings (frontend/try_firmware.py / try_ok.py, interpreter, cold boot):
1. `BFC01470..7C`: boot stub polls `1F006044` bit0 after `1F006048 = 0x2EE0`, `1F006040 |= 2` (and later `0x5DC0`): the MStar one-shot delay idiom (12 MHz ticks: 1 ms, 2 ms). Opt-in model
   `--exploratory-mstar-timer N` (assumed layout, one tick = N guest cycles; N=50 is a guess); the stock image never touches this timer.
2. `BFC01CC4`: MIU1 block `1F203100..0C`: sets `3104` low nibble to 0x10 and polls bit 8 (init/BIST done). Hypothesis `--exploratory-mmio-bits 1f203105:01:01+rmw` (bit reads 1; guest RMW may carry it back).
3. The firmware's own boot stub writes the CPU clock register (`1F22184C = 0x60`, 6 MHz steps -> 576 MHz, formula at `805DCEBC`); the old zero-unknown path discarded that write and read 0 -> divide by zero
   trap at `805A1B58`. Fixed for unverified images by keeping known bytes (`zeroUnknownKeepsKnown`); the stock image is byte-identical (1.54 G-dispatch summaries compared).
4. Result: UART shows BIST0/1-OK, `Hello A7`, `u32CpuSpeedMHz= 576 MHz`, the Utopia banner and USB init; then **"SPI receive FIFO overrun"** at `805F7DEC` reading `1F001030`: the runtime talks to an SPI master
   (halfword registers +0x10 data, +0x14 read data, +0x20, +0x30 trigger, +0xA8 control; command bytes CF FF FF 00) that is not the ISP flash path the stock image uses. Not modelled; next evidence needed: what device
   that is (the same stop was seen with the MSD308 image).
All hypotheses above are labelled exploratory; nothing is a measurement. The GUI applies 1-2 through its "Bring-up hypotheses" checkbox for any non-stock image.
