# Status of the research build

Latest [PM status/producer investigation](pm51-status-producer.md) identifies
the original RTC-related activity-eight path statically, adds bounded native
PM histories and isolated status sensitivity tests. The real `0EC2` producer
is still **unsupported**. Main and PM boundaries remain unchanged; no ZUI,
enabled framebuffer or authentic pixels are demonstrated. Synthetic status
input is confined to the probe and is not a television boot option.

Current [shared PM51 continuation](pm51-shared-continuation.md) executes
3,435 original PM instructions on the machine's shared flash/RIU view.
The unused high status byte remains invalid; PM reaches `9B27`, requiring
unknown `0EC2` bits0..2. Main MIPS still stops at `80404BF4 / BF001CD1`.
No PM marker, ZUI, enabled framebuffer or authentic pixels are demonstrated.

| Current PM capability | Status | Evidence and limits |
|---|---|---|
| Original PM CODE and shared RIU connection | partially implemented | 65,536-byte original flash view; page-zero observed mapping; startup/reset/clock/RAM wiring explicit hypotheses |
| Original PM startup and unused high-byte provenance | demonstrated | 3,435 original instructions; high-byte/register aliases remain invalid; [regression](../evidence/pm51-shared-regression.json) |
| MCU register/alias/stack validity guards | implemented | Invalid peripheral values cannot become valid through Rn/IRAM reloads or return addresses; failed checks roll metadata back |
| PM required status and activity response | unsupported | Low-three-bit producer unidentified; no manufactured seven/eight/zero or ready IRQ |
| ZUI and guest pixels | unsupported | Normal call site not reached; native blank-window startup is separate |

The paragraphs below preserve earlier checkpoint evidence.

Latest [original audio timeout continuation](audio-timeout-continuation.md):
**1,789,567,273 MIPS**, **280,822,700 OpenRISC**, **468,999 DVB-T MCU** and
**six AVD** instructions; **5,441 main timer** and **52 DVB-T MCU** IRQ entries.
Original audio and platform initializers return to the application. New stop:
**80404BF4 / BF001CD1**, an unknown PM activity predicate. ZUI and authentic
graphics remain unsupported. The paragraphs below retain historical evidence.

| Current capability | Status | Evidence and limitations |
|---|---|---|
| Original SE and decoder failure returns | demonstrated | Two original 100-check loops, genuine DSP2/DSP1 Re-Active messages and F3 stores; explicit symbolic nonmatch predicates, invalid reply bytes retained |
| Audio IDMA and decoder upload/readback | demonstrated | 2,781 + 5,560 initial words; 64 later byte-addressed coefficient updates; 2,781 + 5,572 readbacks; no DSP execution or ready output |
| Audio selector and command-byte provenance | implemented | Verified same-origin selector restores; actual MIPS command-byte producers; unknown upper fields discarded only by original masks |
| DSP acknowledgment producer / exact ISA | unsupported | No E3 producer identified; candidate entry opcodes and unuploaded targets remain unresolved; zero DSP instructions |
| PM BF001CD1 execution and reset state | unsupported | Original low-nibble==8 predicate; matching static writers in original flash PM51 candidate; boot mapping/status unresolved, no PM value assigned |
| Corrected cold boot and multi-CPU replay | demonstrated | 803 native checks, 18 baseline and nine display runs, four forensics tests; [latest full replay summary](../evidence/audio-pm-replay.json) |
| Original PM51 isolated startup | partially implemented | Existing 8051 core executes 3,406 original instructions; configured SPI/private-RAM experiment stops at 6DC7 / XDATA 0EC3, disconnected from main PM state; [regression](../evidence/pm51-original-regression.json) |
| ZUI, enabled GOP framebuffer, raster and native guest pixels | unsupported | Driver initialization remains separate from application drawing; no authentic frame |

The latest [audio/AVD continuation](audio-avd-continuation.md) implements
actual addressed audio IDMA upload/readback. Original firmware verifies
**2,781 24-bit words**, and all three prior IDMA timeout messages disappear.
It prints successful code-load completion for `SE dec_none`, `SE adv_none`
and `T4 SE system`. The corrected exploratory boot executes **1,439,384,052
MIPS**, **193,276,894 OpenRISC**, **468,999 DVB-T MCU** and **six AVD MCU**
instructions, with **4,320 main timer** and **52 DVB-T MCU** ISR entries.
Audio DSP2 readiness remains unknown at `804F9098 / BF225AB4`; the original
ZUI initializer still has not executed. No authentic framebuffer or pixels
are demonstrated. Lower instruction counts reflect fewer timeout loops.

| Latest capability | Status | Evidence and limits |
|---|---|---|
| Addressed audio IDMA transfer and verification | demonstrated | 2,781 original words written/read through guest routines; atomic functional timing, no DSP ready response |
| Separate original AVD instruction execution | partially implemented | Six original 8051-family instructions retire; next MOVX at 0802 to C008 stops; standard reset/RAM experiment |
| Audio DSP ISA investigation | partially implemented | Native candidate disassembler; ADSP-like vectors, unsupported 040040/0780xx words, possible unuploaded ROM targets; zero DSP instructions executed |
| Audio DSP2 acknowledgment | unsupported | E3 expected by original guest; no observed/modeled producer, no fabricated response |
| ZUI, active GOP framebuffer, GE raster and scan-out | unsupported | GOP init twice, six disabled-window programming calls; no enabled descriptor or authentic frame |

The following paragraphs and tables retain historical checkpoint evidence.

The latest [shared MCU and AVD continuation](shared-mcu-continuation.md) executes **1,788,854,952 original MIPS dispatches**, **280,644,619 OpenRISC instructions**, **468,999 DVB-T 8051 instructions**, and **5,439 original main timer ISR entries** in exploratory mode. The original MCU supplies the previously unknown shared mailbox byte. Its real handler completes supported diagnostic requests, but the normal boot still suspends that MCU at `4AE0 / XDATA 2200` without acknowledgment. Original MIPS code uploads and verifies a separate **13,312-byte AVD MCU program**, prints `DSP code loaded successfully`, returns from AVD initialization, and reaches audio startup. The main stop is now `804F9098 / BF225AB4`: an unknown DSP2 response compared with `E3`. No host response is invented. The AVD auxiliary CPU is not executing. ZUI, an enabled framebuffer, GE raster drawing and authentic pixels remain **unsupported**. Strict POR and SPI-CF uncertainties remain unresolved. The older evidence table records historical milestones rather than the latest stopping state.

| New capability | Status | Evidence and limits |
|---|---|---|
| Original DVB-T MCU shared mailbox and diagnostic replies | demonstrated | Original MOVX at 4EAA initializes status; 4B0C supplies supported completions; [auxiliary regression](../evidence/shared-mcu-probe.json); standard reset/IRQ/clock wiring remains experimental |
| MCU interrupt scheduling, reset hold, byte validity | partially implemented | Deterministic dispatch and real INT0/Timer0 handlers; unrelated unknown bits remain guarded; unmapped 2200 command has no completion |
| Original AVD program upload and readback | demonstrated | 13,312 actual guest writes and reads, original payload hash; [summary](../evidence/shared-final-summary.json) |
| AVD MCU execution and audio DSP2 handshake | unsupported | AVD instructions = 0; audio response at BF225AB4 has no observed MIPS writer and no modeled producer |
| Original ZUI application / scan-out | unsupported | ZUI state initializer 804E0D48 has not executed; no valid enabled GOP descriptor, raster operation or authentic capture |

The requested complete TV emulator is **not achieved**. No authentic OSD has been displayed. The native application executes original guest code and stops at explicit unmodeled hardware boundaries.

| Capability | Status | Evidence and limits |
|---|---|---|
| Original SHA/size, flash/trailer separation | demonstrated | [Gate A log](../evidence/gate-a.log); stock file remains immutable |
| Twelve bounded, end-marked DEFLATE streams | demonstrated | [Manifest](../evidence/forensics/manifest.json), [regression](../evidence/regression.json); includes four supplied reference hashes |
| Boot disassembly, loader ranges, runtime entry | demonstrated | [Boot flow](boot-flow.md), [flash map](firmware-map.md) |
| Locale resource parsing | demonstrated | [Locale table summary](../evidence/forensics/locale-table-summary.json); rendering tokens/fonts unresolved |
| Unicorn MIPS execution and memory aliases | partially implemented | [Native tests](../evidence/ctest.log); exact CPU variant and general IRQ/cache behavior incomplete; CP0 timer ISR demonstrated |
| Observable MMIO, known-bit initialization tracking | implemented | Every bus callback logged; unknown consumed inputs stop; [register map](verified-register-map.md) |
| Strict reset boot without candidate models | partially implemented | Old boundary preserved at `BFC00C60 / BF001C74`: [trace](../evidence/reset.jsonl) |
| Symbolic reset initialization | partially implemented | Without candidate MIU, old readiness boundary preserved; with candidate MIU, proven RMW keeps unknown bits from becoming known status |
| Original guest CRC check, copies, inflate, main entry | demonstrated | [Regression](../evidence/regression.json), [cold summary](../evidence/cold-boot-summary.json); 90,220,219 dispatches, guest output SHA matches |
| Candidate UART transmission | demonstrated | Provisional timing, no RX/IRQ; original `Hello T4` [output](../evidence/prefix-uart-console.log) |
| Firmware from reset to original runtime | demonstrated | [Cold trace](../evidence/cold-boot.jsonl), [summary](../evidence/cold-boot-summary.json); no host stage copy/PC transfer; candidate digital MIU/BIST and known-bit RMW mode remain research approximations |
| Host copy-on-write NOR storage | implemented | Bounded program/erase, protection, image-bound checksum, atomic replacement; native tests |
| Guest SPI writes and saved TV settings | unsupported | Host storage is not wired to a guest SPI controller; redundancy/checksum/generation format unresolved |
| Native Windows window/file selection | demonstrated | [Smoke log](../evidence/native-window-smoke.log); blank guest display, no fabricated menu |
| GPIO/I2C line model | partially implemented | Guest OUT/OEN, pull-ups, external low driver, contention, START/STOP/address decoding; [schematic cross-reference](gpio-cross-reference.md); unknown pins stop |
| Original I2C bus initialization/recovery | demonstrated | GPIO disabled reproduces old boundary; stuck-SDA fixture elicits nine clocks and stock error; [regression](../evidence/regression.json) |
| GPIO/I2C attached devices | unsupported | No invented tuner/EEPROM/amplifier ACK or data; exact enum-to-board-pad mapping unresolved |
| MIU initialization / BIST | partially implemented | Programmed sequence and bounded guest DRAM comparisons cause completion/failure; exact calibration patterns and analog PHY/timing unresolved |
| Interrupt masks / ADC configuration RMW | partially implemented | Original guest sets IRQ/FIQ masks and ADC fields; known-bit tracking stops unknown status; board IRQ pending/delivery unsupported; CP0 timer ISR demonstrated |
| Guarded unknown diagnostic caches | implemented | Optional continuation; four POR-origin RAM bytes stay invalid; first consumption stops |
| RTC counter/snapshot | partially implemented | Guest divider/reset/load/read strobes; coherent virtual snapshots; alarms/IRQ unsupported |
| CP0 Count/Compare and guest ISR | demonstrated | Stock vector and guest ticker reach 2698 in the explicitly exploratory continuation; virtual pacing provisional |
| UTMI calibration handshake | partially implemented | Guest clock/start/hold/release causes CA_END; analogue data and device transfers unsupported |
| SPI NOR and ISP read transactions | partially implemented | CS/command/address/dummy phases, FIFO readiness, W25Q64BV EF4017 and original descriptor selection demonstrated; CF undocumented output remains unknown; no guest program/erase/settings |
| XIU flash aperture and MIU1 bus aliases | demonstrated | Original guest resource reads and original TLB installation execute; bus 40000000..47FFFFFF shares logical MIU1 RAM |
| POR/status dependency | unsupported | Stops `80469898 / BF200A38`; next `BF200A3C`; reset meanings/values unavailable from schematic |
| GOP bank storage and descriptor decoding | partially implemented | Original initializer executes twice; byte-verified bank helpers preserve invalid IRQ/ACK fields; no valid framebuffer descriptor or scan-out |
| GOP palette write pulses | partially implemented | 64 original zero-valued startup entries committed in bank 3; raw transfer modes separate; no invented reset colors or pixel conversion |
| Scaler banking and gamma RAM | partially implemented | Original guest programs 1080p geometry and commits 768 twelve-bit gamma samples under explicit exploratory initial-state assumptions |
| Output VSYNC counter | partially implemented | Named reference and stock reader identify output-sync bit; clock, polarity and bank coupling are explicit experiments; three original waits return, twelve observed transitions |
| DLC capture/curve initialization | partially implemented | Original curve upload and timeout paths execute; async status cannot become known through ordinary command storage; hardware completion remains unresolved |
| COMB bank storage / opaque register provenance | partially implemented | T4-specific reference matches guest PQ table loader; unknown initial selector is explicit; optional paired-restore experiment quarantines temporary foreign-byte bits without validating analog side effects |
| Hardware semaphore ownership | partially implemented | Original four acquisitions / four releases observed; contention modeled; reset ownership unknown unless explicit idle experiment is selected |
| GE parameter queue/context preservation | partially implemented | 626 original parameter submissions/transfers/retirements; 16-entry status fields supported by T4 references; reset, clock, topology and latency explicit experiments; raster and memory queues unsupported |
| TSP queue and indirect configuration | partially implemented | 16-entry queue reset/count with explicit active-low reset experiment; original 336/880 indirect reads/writes preserve unknown filter provenance; no file-in DMA completed |
| TSP original firmware download | demonstrated | Original guest loads raw SPI TSP image and triggers bounded 12-KiB RAM-to-QMEM transfer; [evidence](tsp-continuation.md); latency provisional |
| TSP OpenRISC auxiliary execution | partially implemented | Focused ORBIS32 subset executes 66,669,575 original instructions; reset PC, QMEM fields and relative clock are explicit experiments; full IRQ/MMU/cache unsupported |
| Original TSP startup and alive handshake | demonstrated | Original handler writes response/clears command, original MIPS success branch executes; [full replay](../evidence/or1k-dual-regression.json); exploratory hardware configuration |
| Authentic OSD / panel display | unsupported | Zero authentic frames or configured framebuffer demonstrated; two GOP initializer entries, 8249 successful GOP aperture events; native window remains blank |
| PM/SAR/IR capture boundaries | partially implemented | Explicit external AC-on/fullscale-SAR fixtures and experimental IR empty-DMA reset; original configuration executes; [limits](openrisc-continuation.md); no original PM51 CPU or host keys |
| PM51 execution, interactive remote, standby/wake | unsupported | No original PM51 execution, arbitrary key injection or fabricated ready acknowledgement |
| SCART-related ADC query | partially implemented | Grounded-input experiment advances original predicate; exact nibble transfer/reset/pin mapping remains unverified, strict result unknown; [report](application-display-continuation.md) |
| Original PQ coefficient SRAM uploads | demonstrated | Two Y tables, 256 real guest writes and commits, source/payload/address/state hashes verified in [replay](../evidence/application-dual.json); exploratory idle/disabled-port reset and transfer timing |
| Y/C coefficient SRAM device | partially implemented | Stateful five-byte storage and busy completion; C path native-tested and disassembled, not demonstrated by this guest run; filtering arithmetic unsupported |
| Conditional SCMI configuration proofs | implemented | Known-argument branches, delay slots and SEH preserve sampled bits through same-register writes; unknown-dependent branches still rejected; [report](application-display-continuation.md) |
| Video input, DVB, ATV, USB/media, audio, PVR, 3D | unsupported | No guest device models or authentic menus demonstrated |
| Menu/settings/hotel/factory behavior | unsupported | Manual contract and original strings are evidence, not observed interaction |
| Deterministic trace replay / PC breakpoints | demonstrated | Guest reruns and events compared in regression; complete snapshots/rewind/GDB unsupported |
| Input bounds and DEFLATE random-input tests | demonstrated | [Python tests](../evidence/python-tests.log); sanitizer verification not performed |
| Windows build | demonstrated | GCC 16.2.0, CMake 4.4.4, Unicorn 2.1.4 |
| Linux build/display | unsupported | Headless source branch exists but no Linux environment was available to compile/test; display backend absent |

Gate A has useful reproducible checkpoints but the full rendering/call-graph investigation remains partial. Gate B is partial: the unchanged reset bootloader now performs its own second-stage copy, CRC, inflate and application entry with explicit candidate digital MIU/BIST/BDMA models. These are not a timing-exact hardware validation. Gates C, D and E are not reached. Gate F has build/trace/storage/CPU/forensics tests but no full-TV regression suite.

Strict candidate boot still stops at BF200A38 / 80469898. Guarded unknown-CF continuation reaches 8053DD14 after independently selecting the documented Winbond descriptor, then stops before cached unknown samples are consumed. Additional explicit output-clock, subwindow, DLC timeout and COMB-selector experiments advance to **805F883C**, after **279,469,882** original dispatches, **606** stock timer ISR entries and **768** original gamma commits. Original scaler initialization returns; DLC takes its original timeouts, then returns. A guard rejects unknown COMB high-byte bits being copied into a different byte. The four POR bytes and twenty stale clock-origin stack bytes remain invalid. No GOP initializer or original frame has executed. See [current investigation](display-sync-and-dlc.md); [previous milestone](spi-and-display-continuation.md) remains historical evidence. Native window creation exits 0 independently of guest stop code 2. No physical TV was modified or accessed.

The previous COMB checkpoint reached **8052624C / BF205004** after **656,427,765** dispatches and **1,813** timer ISR entries; its full evidence and 228-check build remain preserved in the [historical report](comb-and-ge-continuation.md).

The latest continuation reaches **805D7B38 / BF202AFC** after **666,140,752** original dispatches and **1,844** ISR entries. Original GE initialization completes, original GOP initialization executes twice, and 64 guest palette clear entries commit. A matched TSP command-count routine is the next real status boundary; its queue producer/reset behavior is unimplemented. No authentic framebuffer or pixels are demonstrated. Native checks now total **300**. See the [current report](ge-and-gop-continuation.md) for code, evidence, replay, experimental assumptions and missing capture requirements. Earlier paragraphs describe historical checkpoints.

The **previous** continuation reached **805D6BAC / BF202BE8** after **666,276,473** original dispatches and **1,844** timer ISR entries. TSP reset/count, 336/880 original indirect reads/writes and the original 12-KiB auxiliary firmware download execute. The exact original 17,416-byte OpenRISC image is located in SPI at 1E7838. Original auxiliary CPU enable now stops explicitly because execution/address mapping is unsupported. GOP entered twice, but no framebuffer or authentic pixels exist. **346 native checks**, baseline 18 runs, display nine runs and both old/new full trace replays pass. See [current report](tsp-continuation.md); all preceding checkpoint paragraphs are historical. Strict POR and undocumented CF remain unresolved.

The **previous** dual-CPU checkpoint is preserved in [its report](openrisc-continuation.md) and [provenance](../evidence/or1k-provenance.json): 932,954,776 MIPS dispatches, 66,669,575 original TSP instructions, 2,698 original timer ISR entries and the unknown SCART query.

The **current** continuation completes two original Y-filter SRAM tables and further original SCMI configuration, then stops at **806006D4 / BF003C05** before a hardware-dependent fetch-workaround decision. **933,062,691 MIPS dispatches**, **66,696,554 original TSP instructions**, **2,698 timer ISR entries**, and **256 coefficient commits** are demonstrated under explicit experiments. GOP enters twice; six reset-window programming calls and 330 generic register-writer calls reveal no enabled framebuffer. **No authentic OSD pixels are demonstrated.** **436 native checks**, 2/2 CTest, 18 baseline and nine display runs, previous dual-CPU replay and new full dual-CPU replay pass. Strict POR, undocumented CF, exact ADC/format reset behavior and the new hardware byte remain unresolved. See [current report](application-display-continuation.md), [replay](../evidence/application-dual.json) and [provenance](../evidence/application-provenance.json). Earlier checkpoint paragraphs remain historical.
