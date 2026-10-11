# Register evidence and model scope

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
