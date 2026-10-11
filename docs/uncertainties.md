# Evidence still needed

| Boundary | Exact observation | Next research/model method |
|---|---|---|
| PM reset bits | Strict read `BFC00C60 / BF001C74` | Trace initialization table masks and identify reset-defined bits from matching chip docs; do not assume all zero |
| MIU readiness | Guest 0/8/C/E/1F sequence modeled, cold path passes | Digital completion is implemented; verify exact PLL/DDR prerequisites, PHY behavior and real timing |
| BIST | Programmed address/length/pattern drive real guest DRAM comparisons; both banks tested | Bank-select reset=MIU0 is inferred from stock BIST0 before any selector write and explicit BIST1 selection; exact five calibration generators and physical memory capacity still need verification |
| CPU/core | LE MIPS32 instructions execute; 24Kc selected provisionally | Recover guest PRId probing, obtain exact chip suffix or authorized serial CPU identification; cross-check 34Kc architecture reference |
| GPIO bus | Original init and nine-clock recovery demonstrated; pair 65/66, bit04 SDA | Line model is functional; identify actual attached slave addresses and register semantics from subsequent guest transactions |
| I2C board wiring | Supplied reference pages14/16 show pulled-up DDCR and tuner buses | Reference is MSD309 V7.0-A; enum65/66-to-pad and exact Manta wiring are not established. Obtain matching HAL mapping or verified bus capture; no pins were probed |
| Current POR/status read | `80469898 / BF200A38`, next BF200A3C; no prior guest writes | Recover hardware producer/reset semantics. Supplied schematic has no register map; a matching datasheet or trusted read-only register dump is needed |
| Runtime scheduler/IRQ | 46 original CP0 timer ISR entries observed, guest ticker=46; general board IRQ delivery unresolved | Validate CPU clock/cycle pacing, board pending bits and PM51 IRQ routing |
| GOP/scaler | Original menu strings and resources extracted; no original pixels | GOP initializer/banked helper/framebuffer units now located statically; trace actual execution, allocation, palette/format and GE commands |
| Current SPI probe | `8053C6EC / BF001030`, CF FF FF 00 packet | Exact chip identity and undriven-input response unresolved; obtain matching chip/pad evidence; no response constants invented |
| Persistent sectors | 7E/7F blocks differ by one byte | Follow real SPI accesses and checksum/generation routines; do not invent a settings format |
| IR/PM51 | IR/MCU51 strings and candidate secondary CPU blobs | Recover mailbox/register transactions and original key tables; real remote protocol captures may validate constants |
| Media/tuner/audio | Manual features and binary data exist; no usable device models | Trace driver interfaces after runtime boots, identify exact external tuner/demod/codec dependencies, supply lawful TS/media fixtures |
| Pixel-exact behavior | No physical LCD4214 golden captures | Compare guest frames with user-supplied physical-TV screenshots and captured key/timing sequences |

Resource container offsets and strings are verified by bounds/hash checks; font/graphics codec, control tokens and all rendering semantics remain unresolved. Electrical panel timings and LC420WUE revision are not claimed. No CI secrets or protected broadcast decryption are supplied.
