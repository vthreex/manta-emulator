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
| CP0 Count/Compare and guest ISR | demonstrated | Stock vector and guest ticker reach 273 in the explicitly exploratory continuation; virtual pacing provisional |
| UTMI calibration handshake | partially implemented | Guest clock/start/hold/release causes CA_END; analogue data and device transfers unsupported |
| SPI NOR and ISP read transactions | partially implemented | CS/command/address/dummy phases, FIFO readiness, W25Q64BV EF4017 and original descriptor selection demonstrated; CF undocumented output remains unknown; no guest program/erase/settings |
| XIU flash aperture and MIU1 bus aliases | demonstrated | Original guest resource reads and original TLB installation execute; bus 40000000..47FFFFFF shares logical MIU1 RAM |
| POR/status dependency | unsupported | Stops `80469898 / BF200A38`; next `BF200A3C`; reset meanings/values unavailable from schematic |
| GOP bank storage and descriptor decoding | partially implemented | Original helper semantics and real MIPS fixtures; stock initializer has not executed; no palette/compositor/output path |
| Scaler banking and gamma RAM | partially implemented | Original guest programs 1080p geometry and commits 768 twelve-bit gamma samples under explicit exploratory initial-state assumptions |
| Authentic OSD / panel display | unsupported | Zero authentic frames or original framebuffer demonstrated; SC bank-1 edge status remains unresolved |
| PM51, IR remote, front panel, standby/wake | unsupported | No arbitrary key injection or fabricated power state |
| Video input, DVB, ATV, USB/media, audio, PVR, 3D | unsupported | No guest device models or authentic menus demonstrated |
| Menu/settings/hotel/factory behavior | unsupported | Manual contract and original strings are evidence, not observed interaction |
| Deterministic trace replay / PC breakpoints | demonstrated | Guest reruns and events compared in regression; complete snapshots/rewind/GDB unsupported |
| Input bounds and DEFLATE random-input tests | demonstrated | [Python tests](../evidence/python-tests.log); sanitizer verification not performed |
| Windows build | demonstrated | GCC 16.2.0, CMake 4.4.4, Unicorn 2.1.4 |
| Linux build/display | unsupported | Headless source branch exists but no Linux environment was available to compile/test; display backend absent |

Gate A has useful reproducible checkpoints but the full rendering/call-graph investigation remains partial. Gate B is partial: the unchanged reset bootloader now performs its own second-stage copy, CRC, inflate and application entry with explicit candidate digital MIU/BIST/BDMA models. These are not a timing-exact hardware validation. Gates C, D and E are not reached. Gate F has build/trace/storage/CPU/forensics tests but no full-TV regression suite.

Strict candidate boot still stops at BF200A38 / 80469898. Guarded unknown-CF continuation reaches 8053DD14 after independently selecting the documented Winbond descriptor, then stops before cached unknown samples are consumed. Explicitly exploratory SPI input, chip identity, SC selector and gamma-enable configurations advance to 805FBC8C / BF205E78, SC bank 1, after **175,471,842** dispatches, **273** stock timer ISR entries and **768** original gamma commits. The four POR bytes and twenty stale clock-origin stack bytes remain invalid. The exact edge-status signal is unresolved; no arbitrary sync waveform was added. No GOP initializer or original frame has executed. See [current investigation](spi-and-display-continuation.md). Native window creation now exits 0 independently of guest stop code 2. No physical TV was modified or accessed.
