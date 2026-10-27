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
4. (superseded by 5-8) Result: UART shows BIST0/1-OK, `Hello A7`, `u32CpuSpeedMHz= 576 MHz`, the Utopia banner and USB init; then **"SPI receive FIFO overrun"** at `805F7DEC` reading `1F001030`: the runtime talks to an SPI master
   (halfword registers +0x10 data, +0x14 read data, +0x20, +0x30 trigger, +0xA8 control; command bytes CF FF FF 00) that is not the ISP flash path the stock image uses. Not modelled; next evidence needed: what device
   that is (the same stop was seen with the MSD308 image).
All hypotheses above are labelled exploratory; nothing is a measurement. The GUI applies 1-2 through its "Bring-up hypotheses" checkbox for any non-stock image.

5. The ISP SPI driver of this firmware reads the flash status register (RDSR) with all 8 bits consumed (`805F85FC`); the model only knows BUSY/WEL. `--exploratory-spi-status V` (HYPOTHESIS: SRP/BP/QE bits = V)
   lets it pass; with it the firmware prints `Flash is detected (0x0705, 0xEF, 0x40, 0x17)` (Winbond W25Q64 JEDEC id: the existing flash model matches). The CF/FF/FF/00 packet is the same REMS4 probe the stock image sends.
   (A first attempt failed because the receive-register mask analysis only knows the stock program counters; `lhu rt,RDATA; andi rd,rt,0xFF` is now accepted as proof for unverified images.)
6. Runtime MIU1 routine `805EB1F0` writes `1F203104 = 4`, then `0x14`, and polls **bit 10** (the boot stub polled bit 8 after writing 0x10). `--exploratory-miu1-done N` models a calibration/BIST engine: bit4 starts a run,
   bits 8..11 rise after N guest cycles (assumed), clearing bit4 clears them. It replaces the earlier fixed-bit hypothesis (the runtime's own write of the register conflicted with it).
7. Next stop: `unmapped/protected guest access` at `805D7418`, address `60225A3D`: an audio-bank byte write (`HAL` helper `805D73F4`, RIU byte formula `(reg<<1)-(reg&1)`) through the IO-map base word `[8081294C]`
   which holds **0x60000000** (set by `805D734C` from `[s7+0x55D0]`; a driver descriptor is initialised with `lui 0x6000` at `805FC29C`). The stock board uses 0xBF200000-style bases; the firmware obtains module bases through
   `805BF2A8` (MMIO table lookup via `805EAF4C`) and no guest TLB entry maps 0x60000000 (only the 16 start-up TLBWI entries were written). Open question: which hardware/chip-id value makes the firmware pick 0x60000000 instead
   of the RIU base (the zero-unknown fallback returns 0 for the chip-id registers it read, e.g. 1F203Cxx). Not modelled.
Diagnostics: `MANTA_DBG_TLB=1` prints TLBWI/TLBWR writes, `MANTA_DBG_SPI=1` prints SPI receive-register reads (stderr).

## Update (Blaupunkt = OKbin.bin)
8. The stop at `805D7418` was NOT an IO-base problem. The firmware's very first TLB entry (TLBWI idx 0 at `805A8760`, pagemask 0x1FFFE000, EntryLo0 0x01800007, EntryLo1 0x01800017) maps
   virtual 0x00000000.. (128 MB pages) to **physical 0x60000000**; the audio HAL write goes through the not-yet-initialised IO base word `[8081294C]` = 0, so the store hits virtual 0x00225A3D = physical
   0x60225A3D. On the real chip that is plain DRAM (second bank), so the store is harmless; the board model had nothing there. `--exploratory-miu1-alias-at 60000000` (HYPOTHESIS: MIU1 bank is also visible at
   physical 0x60000000) maps the existing MIU1 backing there; the GUI bring-up checkbox passes it. (The HAL setter 805D734C is only called from 805A9808 inside the audio-init path 805A96C4, which runs later.)
9. Next stop (~461 M dispatches, both fast and strict): stuck poll at `805DCD90` reading halfword `1F001C48` until it equals 2 (after writing bytes 1,0,0x10 to `1F001CA5` and other PM registers). Same register stops the
   BP85 MSD309 image at `805EFE08`. It is a PM51 (power-management 8051) handshake: PM XDATA 0x0E24 = RIU 0x1F001C48. The PM51 model stops earlier on an unsupported XDATA write (pm51 boundary at PC 0x4868, XDATA 0xE56 for OKbin).

10. PM51 of this image (flash 0x10000, 8051 code) needs, in order: MOVX to the PM register banks with P2 = 0xFF at reset (XDATA 0x0E56 first); reads of undefined PM bits; PM reg 0x0EC2 bit 3 (RIU 1F001D84) set; plain
   8051 interrupt sources; SFR 0x9A/0xA9 and other nonstandard SFRs; UART transmit completion. All are EXPLORATORY, labelled hypotheses, enabled by `--exploratory-pm51-register-storage`
   (XDATA 0x0E00..0x7FFF = shared RIU byte store, undefined reads 0, INT0/T0/INT1/T1/serial admitted, nonstandard SFRs = plain storage, TX completes at once), `--pm51-instruction-budget N`, and
   `--exploratory-mmio-bits 1f001d84:08:08` (also applied to PM51 reads). With them OKbin passes the 805DCD90 handshake, runs the whole init and prints
   "AC on, should go to standby!!!!! Wait for PM51 standby... PM51 run ok..." then idles at 806BF1xx: the AC-boot path chose standby and waits for a power key. Next: send the board's POWER IR code / find the
   power-on-mode predicate. The PM51 is emulated at 1/32 of the dispatch clock (1.5 G PM51 instructions per 3 G dispatches), so these runs are slow (900 M dispatches = 4 min).
