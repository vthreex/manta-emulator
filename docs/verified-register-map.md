# Register evidence and model scope

The latest [revision, DVB-T MCU and CI continuation](revision-and-vd-mcu-continuation.md) supersedes historical stopping points below. Addresses and bit tests in the next table are directly observed in original firmware; exact Titania4 reset values, external wiring, and timing are stated separately.

| Address / path | Observed original firmware use | Current supported interpretation and limit |
|---|---|---|
| `BF003C05` | `806006CC..DC` returns `byte < 2`; caller adjusts capped fetch parameter | Related SYS library labels this byte `ChipRev`; exact Manta stepping unknown. A chosen byte is opt-in only. |
| `BF206900/01/05/08/09/18/20` | `804F565C` holds/releases auxiliary MCU, writes 22,026 original bytes via `18`, rewinds address via `08/09`, compares every `20` read; later pulses `05=2→0` | Shared addressed program store and pulse count are modeled. The UART identifies this selected program as DVB-T. Execution depends on unresolved SFR/XDATA effects. |
| `BF2068A0` | `8051EF94` PCMCIA byte read; original `8051EEC8` caller overwrites its copy with `51` before use | Guarded unknown pre-read only; no reset value or side effect assumed. |
| `BF206891` | PCMCIA index 9; original `8051E980` masks bit 2 | Related named PCMCIA driver uses the same address formula and bit-2 card test. Optional external high-line fixture defines bit 2 only; connector polarity unknown. |
| `BF224C00` | Original MIPS command byte and finite polling; `BF206905` pulses request | Original 8051 is suspended, so the command remains pending and timeout returns failure; no `FF` acknowledgment generated. |
| `BF224C38` | `804F496C` tests bit 0 before a second mailbox transaction | Initial busy/status state and producer unknown. Strict and exploratory modes stop rather than choose the branch. |
| 8051 XDATA `1080..1083`, `2F00` | Original `AA/55/AA/55` writes, then write `00` to `2F00` at MCU `55B9` | Only the first four writes can be explicitly deferred for diagnosis; their effects, `2F00` mapping and MCU interrupt routing remain unsupported. |

"Verified" here means an address/access/formula or use is directly present in this firmware. It does not establish every register's electrical or timing semantics.

| Address / block | Direct evidence | Current model / unresolved dependency |
|---|---|---|
| `BF001C7C` | Reset `BFC0000C`: word write `4F` | Logged latch, function unclassified |
| `BF006010/6014` | Reset word writes zero | Logged storage; device semantics unclassified |
| `BF206700` | Sequential stage markers from reset | Logged progress writes; not a substitute for hardware |
| RIU `BF000000`, `BF200000` | Table routines `BFC00C00`, `BFC00C90`, `803BD654` | Byte formula `base + 2*index - (index&1)`; word/halfword accesses retain width; no collapsed generic stride |
| `BF001C74` | Masked initialization read at `BFC00C60` | Unknown reset bits stop strict mode; diagnostic known-bit masks preserve uncertainty |
| `BF202400`, `BF200C00` | Program sequence `0,8,C,E,1F`; polls bit `8000` at `BFC01A78/1A98` | Candidate digital MIU accepts full observed sequence and schedules ready after 64 dispatch ticks; invalid sequence cancels; no analog PHY model |
| `BF2025C0..25D0` | BIST label emitted by stock code; command and result masks at `BFC023EC..2408` | Candidate bounded guest DRAM write/read comparisons derive done/failure; start in 4 KiB units, length in 16-byte units, bank select 25E0 bit 15; five calibration patterns approximated |
| `BF201200..1234` | Second-stage programs command `0335`, source, length, polynomial, seed; polls `1204` bits 0/3 | Candidate CRC engine computes actual bounded flash data; done follows command after provisional 64 dispatch ticks; other BDMA operations stop |
| `BF201300` | Boot and stage-two literal writes; runtime transmitter | Candidate UART TX; DLAB changes divisor-vs-TX meaning; ordinary mode captures raw candidate writes only |
| `BF201318` | Runtime `8054AB84..ABC8` accesses LCR/DLAB; direct early writes | UART layout corroborates eight-byte register stride; IRQ/RX/error modes unsupported |
| `BF201328` | Runtime `8054B430` reads LSR, tests TX readiness | Candidate UART models THRE/TEMT after actual TX writes with provisional 16 dispatch-tick latency |
| `BF20164D` | Runtime `8054AB34` extracts clock selector bits 2..4 | Real early guest write `BF20164C=C01` supplies selector in prefix diagnostic; no guessed zero clock |
| GPIO table `80631F88` | 24-byte descriptor stride, 207 valid IDs, RMW/input functions `8053BB7C..BDF8` | Firmware-consumed output/OEN bit writes tracked symbolically; pin mapping to connectors unverified |
| GPIO 65/66 input `BF203D05` | Descriptor `101E83`, masks 02/04; read at `8053BDE4` | Modeled input bits from guest direction/output, pulled-up release and external low driver; unrelated bits remain unresolved |
| GPIO 65/66 output `BF203D09` | Descriptor `101E85`, masks 02/04 | Logged guest-written bits |
| GPIO 65/66 direction `BF203D0D` | Descriptor `101E87`, masks 02/04 | OEN=1 releases the line, confirmed by init/recovery disassembly and fault trace |
| `BF2033D0/D4/D8/DC`, `BF203390/94/98/9C` | `8060A744`, `8060AD4C`: 128 IRQ/FIQ mask bits, 16-bit halves at four-byte stride | Symbolic RMW only; guest progressively defines every mask bit; IRQ delivery/pending status unsupported |
| `BF204B10` | `80469844`: AND 80C0 / OR 7F20 / SH | Bounded straight-line immediate RMW proof preserves unknown unused bits |
| `BF221ADC`, `BF221BDC` | Boot writes low-byte phase seed 88, then sweeps low/high nibbles and chooses 77 | Only known low byte consumed; upper unknown bits discarded by original ANDI; PHY phase effects unresolved |
| `BF200A38`, `BF200A3C` | `80469898/804698AC`: LHU and cache in BSS; later popcount/text conversion | Strict read stops; POR scratch interpretation provisional; diagnostic caches remain invalid |

Continuation additions: BF001010 transmits the real CF opcode in trigger mode; BF001030 requests receive clocks; BF001054/58/5C are RD/WR/CMD FIFO readiness. BF203FFC selects banked GOP registers. BF205E00 selects the banked scaler aperture. Bank 10h BF205FB0/B8/BC/C0/B4 carries gamma address/samples/channel strobes and busy. See [SPI evidence and limits](spi-and-display-continuation.md).

| New encountered field | Evidence | Scope |
|---|---|---|
| SC bank 1/3 BF205E78 bit 0 | Stock output-edge reader and named reference output-VSYNC reader; input reader instead consumes bit 2 | Signal identity strongly supported; T4 polarity, clock routing, bank coupling and reset state unverified; explicit virtual clock experiment only |
| SC bank 20 BF205E40 bit 1 | Stock 805FE024 predicate and named reference subwindow enable/disable | Initial enable unknown; exploratory bit selects stock path, unknown geometry comparison still stops |
| SC bank 1A BF205E10 bits 2/3 | Stock DLC capture-range request and completion polls; matching named reference | Guest request tracked, async completion invalidated on command; optional held-low experiment exercises original timeout only |
| SC bank 1A BF205E1C/1D, BF205E51 | Stock luminance statistic / chroma gain getters | Unknown samples retained through instruction/RAM bit provenance; no fabricated measured values |
| BF206C00, BF206C04..6DFF | Original COMB PQ table loader; public T4-specific workaround and base 103600 | Separate banked storage; selector reset unknown; no comb-filter pixel-processing engine |
| COMB bank 0 BF206D1D bits 4/5 | Stock reg8F high-byte preservation and temporary duplicated-halfword store | Unknown reset bits remain unknown; guard stops 805F883C before foreign low-byte use |

See [current display evidence and limitations](display-sync-and-dlc.md). These fields do not establish complete register semantics or substitute for a matching silicon register manual.

Prioritized remaining work: COMB high-byte reset/side effects, actual scaler clock/polarity and DLC completion; POR producer/reset and undocumented CF electrical behavior; GOP output/shadow/MIU/palette semantics; guest flash program/erase; GPIO/I2C attached devices; PM51/IR; board IRQs; audio, tuner/demod/TS, USB/media, PVR and 3D. No register map from MSD309PX or a later MStar SoC was copied wholesale.

The observable bus records PC, width, value, physical address, instruction-dispatch tick, RA and SP. Stop summaries include all general registers, the last 64 PCs, and 16 stack words. A reliable full stack unwind and calibrated cycle timing are unsupported.

New encountered subsets: RTC BF002400/2600; UTMI CA_START/CA_END BF207578/7478; SPI ISP BF001000..10FC. Actual sequences, source corroboration and limitations are in [investigation](por-timer-spi-investigation.md). Summaries now include 4096 runtime dispatches, 256 bus accesses, Count/Compare/Cause/EPC and guest software tick. No full stack unwind is claimed.

## COMB/GOP-table/GE continuation

COMB bank0 logical10368F / BF206D1D bits4/5 remain unclassified preserved fields. The original table's mask CF does not establish their reset values. The paired duplicate/restore is an explicit quarantined experiment, not a validated analog model. BF203FFC low nibble is corroborated as GOP bank selection; IRQ/ACK reset states remain unknown. Original table writes BF203E65 in banks0/3 before GOP initialization.

BF203000 + slot*4 is a 16-bit semaphore resource-ID aperture, independently identified from the original acquire/release/reset/query functions and matching named reference bodies. Candidate ownership retains contention; reset ownership is unknown by default. No PM51/AEON owner is fabricated.

BF205000 bit0 is preserved by original GE_ReadReg context logic without assigning it. BF205004 bit1 selects virtual command queues in original GE_WaitCmdQAvail; first encountered before any establishing guest write. BF20501C bits3..7 and11..15 are queue-space fields in the matching functions; bit0 is busy. Queue depth16 is a stock wait predicate, **not an implemented ready constant**. No GE command processing/status producer is claimed. See [current evidence](comb-and-ge-continuation.md).

## Original auxiliary execution continuation

Original ORBIS32 code accesses command 210330 and response 21033C. They alias MIPS BF202B00/B04 and BF202C00/C04 with shared backing, also accessible through original indirect targets 420660/420678. Original instructions produce the alive reply; no host response constant is inserted. QMEM one-backing memory and per-byte validity are maintained. Exact reset fields, relative clock and general auxiliary MIU accesses remain unvalidated/unsupported.

BF001CDD is corroborated as PM PowerOnMode by a matching reader and related enum header; an explicit external AC-on experiment selects FF. BF002834/35 are original SAR readings with programmed power/free-running conversion. Fullscale voltages and conversion pacing remain experiments. BF206E79/7C is the split IR DMA producer query; original 03847000/360/48-byte ring layout is recovered, while descriptor command 82h reset-index coupling is an experiment. Original GOP top-bank reset pulse to window-enable reset coupling likewise remains experimental.

BF204B15 at 804F0C48 is an unresolved SCART-related ADC query. Related Marlon archive symbol Hal_ADC_is_scart_rgb accesses this same byte but tests bits 4/6 and sets bit 5, whereas this Manta code reads low nibble and compares it with four. Thus neither the reference bit masks nor a zero/no-input value is copied into the model. The exact Titania4 ADC transfer, producer, board pin and status semantics require further evidence. [Report](openrisc-continuation.md).

## Original MCU / AVD and audio continuation

The actual MIPS and 8051 handlers establish shared aliases between XDATA
2F00..2F1F and BF224C00..4C3D with byte stride `2*i-(i&1)`. Original MCU
MOVX at 4EAA defines BF224C38; its IRQ handler writes FF at 4B0C only after
successful target access. MCU 1B00 mask, 1B10 pending and 1B08 clear bit2
are corroborated by original code. Other bits remain unknown. IRQ routing,
reset/RAM and relative timing are explicitly experimental. XDATA 2200
remains an unsupported target, not a writable ready shortcut.

Original AVD loader 805047D4 establishes the BF2068C1/C5/C8/C9/D8/E0
programming sequence. All 13312 uploaded bytes read back through the actual
port before the original comparison succeeds. Execution/ready effects of
that auxiliary CPU remain unsupported. Original 35D4 / BF206BA8 cache and
unused BF206A85 observation keep invalid provenance.

The latest actual stopping read is **804F9098 / BF225AB4**. Its logical
audio register is 2D5A; original caller 804F79D8 compares it with E3 during
DSP2 system-code startup. The trace has no MIPS producer write to this
byte. Audio IDMA/processor completion and its reset value remain unknown.
No E3 or arbitrary initial failure value is returned. See the
[current continuation](shared-mcu-continuation.md).

The [audio/AVD continuation](audio-avd-continuation.md) now models the
original 2D04/2D02/2D03 write-address/data phases and 2D08/2D00/2D0C..0E
readback sequence as addressed 24-bit transfers. 2D00 bits 3/4 completion
is causal to actual transfers, with atomic functional timing; unrelated
fields remain unknown. BF225AB4 is still an unresolved DSP2 load-code
acknowledgment. Original AVD executes to PC 0802, XDATA C008 write55;
the register identity/effect is unknown and its write does not complete.

The [audio timeout continuation](audio-timeout-continuation.md) observes two
original finite failure returns without validating either E3 acknowledgment.
Actual low-byte SIF producers and same-origin preservation retain unknown
upper fields. IDMA also handles original byte-addressed coefficient transfers;
an incomplete address latch cannot reuse a stale cursor.

Current main boundary: `80404BF4 / BF001CD1`, low-nibble-equals-eight PM
activity test. Candidate original PM51 code has static writers to logical
`0E69`, including zero at `934D` through `724E/729A`. They have not executed.
The isolated original PM startup stops at `6DC7 / XDATA 0EC3`, before the
`0EC2` readiness test. Related historical ROM-source definitions corroborate
the separate candidate `1018=02` SPI-selection interpretation; no Titania4
reset/status value or PM reply is imported from another platform.

Current shared PM experiment refines the earlier 0EC3 boundary. Original
getter 6D84 reads high 0EC3 then low 0EC2 and returns R6/R7; caller 9B17
uses only R7&7. The high byte remains invalid through register/IRAM copies.
PM now stops before JNZ at 9B27, unknown source 0EC2 bits0..2. Page-zero
observations share candidate MIPS aliases BF001D85/BF001D84. No hardware
producer, reset value, completion or activity marker is inferred.
See [shared PM evidence](pm51-shared-continuation.md).
