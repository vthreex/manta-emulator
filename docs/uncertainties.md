# Evidence still needed

| Boundary | Exact observation | Next research/model method |
|---|---|---|
| PM reset bits | Strict read `BFC00C60 / BF001C74` | Trace initialization table masks and identify reset-defined bits from matching chip docs; do not assume all zero |
| MIU readiness | `BFC01A78`, `BF202400` bit `8000` | Decode full initialization sequence and derive when readiness follows clock/config state; verify MIU variant |
| BIST | `BF2025C0..25D0` | Recover programmed range, command, fail flags and completion semantics before modeling tested DRAM |
| CPU/core | LE MIPS32 instructions execute; 24Kc selected provisionally | Recover guest PRId probing, obtain exact chip suffix or authorized serial CPU identification; cross-check 34Kc architecture reference |
| GPIO bus | `8053BDE4 / BF203D05` bit `04`, GPIO 66; pair 65/66 at `806563F8` | Decode output-enable polarity, START/STOP/ACK loops, enumerate slave addresses and read requirements; implement open-drain lines and actual attached-device state |
| I2C board wiring | Paired pins, probable bus recovery; `IIC init fail SDA:%x SCL:%x` | Match exact board schematic if available. Otherwise an authorized noninvasive logic-analyzer capture of the verified I2C bus during boot can establish transactions; no pins have been probed here |
| Runtime scheduler/IRQ | Main entry reached, device initialization stops before dispatch established | Follow vector setup/dispatch code and timer programming; use exact guest mask/status conventions |
| GOP/scaler | Original menu strings and resources extracted; no original pixels | Find resource loads and renderer callers, trace descriptor/palette/plane writes, decode buffer formats; use MSD309PX references only as hypotheses |
| Persistent sectors | 7E/7F blocks differ by one byte | Follow real SPI accesses and checksum/generation routines; do not invent a settings format |
| IR/PM51 | IR/MCU51 strings and candidate secondary CPU blobs | Recover mailbox/register transactions and original key tables; real remote protocol captures may validate constants |
| Media/tuner/audio | Manual features and binary data exist; no usable device models | Trace driver interfaces after runtime boots, identify exact external tuner/demod/codec dependencies, supply lawful TS/media fixtures |
| Pixel-exact behavior | No physical LCD4214 golden captures | Compare guest frames with user-supplied physical-TV screenshots and captured key/timing sequences |

Resource container offsets and strings are verified by bounds/hash checks; font/graphics codec, control tokens and all rendering semantics remain unresolved. Electrical panel timings and LC420WUE revision are not claimed. No CI secrets or protected broadcast decryption are supplied.
