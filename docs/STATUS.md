# Status of the research build

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
| CP0 Count/Compare and guest ISR | demonstrated | Stock vector and guest ticker reach 1844 in the explicitly exploratory continuation; virtual pacing provisional |
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
| TSP configuration provenance | partially implemented | Original IRQ-mask and filter-route RMW fields remain invalid; no ready/pending/status values invented; next queue-count read stops at 805D7B38 / BF202AFC |
| Authentic OSD / panel display | unsupported | Zero authentic frames or configured framebuffer demonstrated; two GOP initializer entries, 8145 successful GOP aperture events; native window remains blank |
| PM51, IR remote, front panel, standby/wake | unsupported | No arbitrary key injection or fabricated power state |
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
