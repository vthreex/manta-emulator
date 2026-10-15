# Evidence still needed

Current application investigation: original ZUI initialization has entered and
returned in explicitly exploratory runs. Its state dispatcher and authentic
OSD pixels remain unobserved. Main execution reaches `80509D54`, consuming
invalid VIF mean16 from `BF224644`; neighboring PGA/VGA observations remain
invalid. The [MCU/VIF continuation](mcu-stack-vif-continuation.md) verifies
isolated finite original VIF return paths without supplying TV measurements.
PM51 `574F/0E6A`, AVD `0802/C008`, DVB-T `4AE0/2200`, exact DSP execution,
strict POR and undocumented SPI CF remain independent uncertainties.

The older checkpoint paragraphs below retain their historical boundaries.

Latest [PM power-good/UART cross-reference](pm51-power-good-and-uart.md):
related boot source names the exact `0EC2 & 7 == 7` predicate CheckPowerGood.
Individual MSD309 detectors and reset state remain unverified. The named
powered/UART experiment reaches original `574F / 0E6A` bit7-clear write;
its side effect remains unsupported. Later `0EC2` bit3 is also unresolved.
Default PM still stops at `9B27`; no activity marker or ZUI is supplied.

Current main boundary is `80404BF4 / BF001CD1`, the PM background-activity
predicate. Original audio failure paths return under an explicit symbolic
non-acknowledgment experiment; both reply bytes stay invalid. Strict execution
still rejects unresolved inputs. No ZUI/framebuffer is demonstrated.

Original flash `10000..20000` contains the PM51 candidate. Bounded standard
8051 reset executes three instructions before XDATA `1018=02`. The isolated
private-RAM experiment executes 3,406 instructions and stops at
`6DC7 / XDATA 0EC3`, the high byte of the subsequent `0EC2` status check.
Related Amber3/Kaiser historical definitions name `1018` ROM_CODE_CTRL and
`02` SPI_ENABLE. That corroborates a separate candidate SPI-selection model,
not the exact Titania4 reset/fetch mapping. The PM status producer and bit
meanings, private RAM/page wiring, reset/clock routing and PM/MIPS scheduling
remain unknown. Static `934D` clears `0E69`, but it has not executed; no zero
or marker response is assigned to the main machine. See
[original PM execution evidence](../evidence/pm51-original-regression.json).

The older paragraphs below describe earlier stops, not the current main PC.

Latest [audio/AVD checkpoint](audio-avd-continuation.md): addressed IDMA
readback works, but DSP2 load-code acknowledgment `BF225AB4` is still
unknown. Candidate ADSP-21xx decoding does not explain reset-entry word
`040040`, the `0780xx` family or calls to unuploaded `3FDE/3FC1` targets.
Exact MStar DSP extensions and ROM/external mappings must be established
before original audio execution can produce the expected acknowledgment.
Original AVD stops at `0802 / XDATA C008` write55; the subsequent C009
key, SFR D8/8E and XDATA mappings remain unmodeled. The separate DVB-T
MCU is stopped at `4AE0 / XDATA 2200`. No host acknowledgments are
fabricated. Strict POR/CF and exploratory clock/reset assumptions persist.
Earlier checkpoint paragraphs below are historical.

Latest boot dependencies: `BF003C05` likely reports chip revision, but the Manta byte is unmeasured; `BF206891` is a PCMCIA detect input whose board polarity is unverified; `BF224C38` bit 0 blocks original MIPS decision making at `804F496C`; the original DVB-T 8051 blocks on unmapped XDATA `2F00` at `55B9` after an explicitly deferred four-byte key sequence. SFR `8E`, those key-write effects, reset timing, and MIPS-to-MCU interrupt routing also remain unverified. See [current execution evidence](revision-and-vd-mcu-continuation.md). A matching MSD309/Titania4 register definition or safe read-only capture is needed before these values can become strict hardware behavior. Strict POR `BF200A38` and the undocumented Winbond `CF` response remain separate unresolved prerequisites.

Historical stop: `806006D4 / BF003C05`. The original full-byte `<2` predicate changes a capped fetch-size parameter, consistent with a related named OPM-fetch patch. Exact byte identity and value are unresolved; neither Marlon's constant-false implementation nor other CHIPTOP revision fields establish it. See [current evidence and capture method](application-display-continuation.md). SCART zero-code, SCMI initial low byte, no-input detection/reset, SRAM idle/port reset and transfer clock remain explicitly experimental. Strict POR and undocumented CF are unchanged. Historical boundaries below remain evidence, not current-stop claims.

| Boundary | Exact observation | Next research/model method |
|---|---|---|
| PM reset bits | Strict read `BFC00C60 / BF001C74` | Trace initialization table masks and identify reset-defined bits from matching chip docs; do not assume all zero |
| MIU readiness | Guest 0/8/C/E/1F sequence modeled, cold path passes | Digital completion is implemented; verify exact PLL/DDR prerequisites, PHY behavior and real timing |
| BIST | Programmed address/length/pattern drive real guest DRAM comparisons; both banks tested | Bank-select reset=MIU0 is inferred from stock BIST0 before any selector write and explicit BIST1 selection; exact five calibration generators and physical memory capacity still need verification |
| CPU/core | LE MIPS32 instructions execute; 24Kc selected provisionally | Recover guest PRId probing, obtain exact chip suffix or authorized serial CPU identification; cross-check 34Kc architecture reference |
| GPIO bus | Original init and nine-clock recovery demonstrated; pair 65/66, bit04 SDA | Line model is functional; identify actual attached slave addresses and register semantics from subsequent guest transactions |
| I2C board wiring | Supplied reference pages14/16 show pulled-up DDCR and tuner buses | Reference is MSD309 V7.0-A; enum65/66-to-pad and exact Manta wiring are not established. Obtain matching HAL mapping or verified bus capture; no pins were probed |
| Current POR/status read | `80469898 / BF200A38`, next BF200A3C; no prior guest writes | Recover hardware producer/reset semantics. Supplied schematic has no register map; a matching datasheet or trusted read-only register dump is needed |
| Runtime scheduler/IRQ | 606 original CP0 timer ISR entries observed in explicitly exploratory mode, guest ticker=606; general board IRQ delivery unresolved | Validate CPU clock/cycle pacing, board pending bits and PM51 IRQ routing |
| Scaler edge wait | Named reference identifies output VSYNC; stock bit0 reader has different inversion convention | Validate exact T4 polarity, PLL/clock routing, bank3 coupling and clock-off level; explicit clock experiment does not resolve these |
| DLC completion | Bank1A reg4 bit2 request / bit3 completion; stock timeout path observed | Determine actual completion producer, timing and reset state; command writes cannot define async status |
| COMB temporary write | At `805F883C`, original SH targets BF206D1C and would copy unknown reg8F high bits4/5 into low byte before restoring it | Establish initial bits and T4 erratum/write side effects using matching source/register evidence or documented passive trace; no arbitrary reset assignment |
| Exploratory initial conditions | CF FF sampling, chip identity 18h, SC bank 0, gamma enable 0/1, output clock 148.5MHz, PIP0, DLC pending0/timeout, COMB bank0 | Configured hypotheses remain separate from strict behavior; obtain physical marking and trusted captures; see [continuation](display-sync-and-dlc.md) |
| GOP/scaler | Original menu strings and resources extracted; no original pixels | GOP initializer/banked helper/framebuffer units now located statically; trace actual execution, allocation, palette/format and GE commands |
| Current SPI probe | `8053C6EC / BF001030`, CF FF FF 00 packet | Exact chip identity and undriven-input response unresolved; obtain matching chip/pad evidence; no response constants invented |
| Persistent sectors | 7E/7F blocks differ by one byte | Follow real SPI accesses and checksum/generation routines; do not invent a settings format |
| IR/PM51 | IR/MCU51 strings and candidate secondary CPU blobs | Recover mailbox/register transactions and original key tables; real remote protocol captures may validate constants |
| Media/tuner/audio | Manual features and binary data exist; no usable device models | Trace driver interfaces after runtime boots, identify exact external tuner/demod/codec dependencies, supply lawful TS/media fixtures |
| Pixel-exact behavior | No physical LCD4214 golden captures | Compare guest frames with user-supplied physical-TV screenshots and captured key/timing sequences |

Resource container offsets and strings are verified by bounds/hash checks; font/graphics codec, control tokens and all rendering semantics remain unresolved. Electrical panel timings and LC420WUE revision are not claimed. No CI secrets or protected broadcast decryption are supplied.

Latest continuation: the original DVB-T MCU establishes the previously
unknown shared mailbox through genuine instructions, but XDATA 2200 still
has no supported transaction effect or completion. The AVD 13312-byte
programming port is demonstrated; that auxiliary CPU has not executed.
SFR8E and reset-key effects, private XRAM extent, IRQ routing and relative
clock remain explicit experiments. Audio DSP2 logical 2D5A / BF225AB4 is
the current main blocking input: expected E3 is a firmware comparison,
not a proven reset or ready value. Recover IDMA completion and the actual
DSP-side reply producer before implementing it. See the
[latest evidence](shared-mcu-continuation.md). Older boundaries below are
historical; strict POR/SPI-CF and all other stated uncertainties persist.

Current additional boundary: **8052624C / BF205004 bit1**, GE virtual command-queue enable, after original GFX context preservation. Exact reset state, any external producer, clock/reset prerequisites and command-queue processing remain unknown. Genuine FIFO space/busy/tag status must follow queued operations; no constant success response is acceptable. SEM initial ownership and GOP initial bank are explicit experiments. The COMB transaction still assumes absent intermediate analog side effects, with high bits never made valid. See [current continuation and capture requirements](comb-and-ge-continuation.md).

Current original TSP execution resolves the auxiliary-enable software dependency under explicit experiments; it does not validate silicon reset PC/QMEM reset fields or clock ratio. Full auxiliary interrupts/MMU/cache and PM51 execution remain unsupported. The current hardware decision is 804F0C48 / BF204B15 low nibble >=4. Related named HAL code identifies a SCART RGB detection use of the same byte with different bit masks; exact Titania4 input/ADC semantics and absent-input reset value remain unknown. No arbitrary nibble is supplied. Original GOP enters twice but no framebuffer is demonstrated. [Current report](openrisc-continuation.md).

Latest connected PM instructions: 3435. The 0EC3 high byte is unused by the
startup caller, and remains unknown. Required low 0EC2 bits0..2 now block
the original JNZ at 9B27. Main still blocks at 80404BF4 / BF001CD1, since
934D has not executed. Startup ordering, private RAM/page mapping, clocks,
interrupt routing and required status producer remain unverified. The
software test requiring seven is not a reset/completion specification.
The configured external power reason is consistent across CPU views;
firmware writes take precedence and invalid bits cannot fall back to a
known external value. [Current report](pm51-shared-continuation.md).

The [status/producer investigation](pm51-status-producer.md) identifies a
static original writer of activity eight through RTC-related event dispatch:
0E1C bit4 sampled, 0E1E bit4 clear, 9B96 -> 956B -> 8AAF -> 724E.
This unexecuted path is not a cold-boot readiness response. Neither the
0EC2 hardware producer nor cold reset value is established. Synthetic
low-three-bit inputs are isolated probe experiments, absent from TV boot.
