# Status of the research build

The requested complete TV emulator is **not achieved**. No authentic OSD has been displayed. The native application executes original guest code and stops at explicit unmodeled hardware boundaries.

| Capability | Status | Evidence and limits |
|---|---|---|
| Original SHA/size, flash/trailer separation | demonstrated | [Gate A log](../evidence/gate-a.log); stock file remains immutable |
| Twelve bounded, end-marked DEFLATE streams | demonstrated | [Manifest](../evidence/forensics/manifest.json), [regression](../evidence/regression.json); includes four supplied reference hashes |
| Boot disassembly, loader ranges, runtime entry | demonstrated | [Boot flow](boot-flow.md), [flash map](firmware-map.md) |
| Locale resource parsing | demonstrated | [Locale table summary](../evidence/forensics/locale-table-summary.json); rendering tokens/fonts unresolved |
| Unicorn MIPS execution and memory aliases | partially implemented | [Native tests](../evidence/ctest.log); exact CPU variant, exception delivery, IRQ/cache behavior incomplete |
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
| Interrupt masks / ADC configuration RMW | partially implemented | Original guest sets IRQ/FIQ masks and ADC fields; known-bit tracking stops unknown status; no IRQ delivery yet |
| POR/status dependency | unsupported | Stops `80469898 / BF200A38`; next `BF200A3C`; reset meanings/values unavailable from schematic |
| GOP/graphics/scaler/panel, authentic OSD | unsupported | No guest frame, fonts or graphics codec decoded yet |
| PM51, IR remote, front panel, standby/wake | unsupported | No arbitrary key injection or fabricated power state |
| Video input, DVB, ATV, USB/media, audio, PVR, 3D | unsupported | No guest device models or authentic menus demonstrated |
| Menu/settings/hotel/factory behavior | unsupported | Manual contract and original strings are evidence, not observed interaction |
| Deterministic trace replay / PC breakpoints | demonstrated | Guest reruns and events compared in regression; complete snapshots/rewind/GDB unsupported |
| Input bounds and DEFLATE random-input tests | demonstrated | [Python tests](../evidence/python-tests.log); sanitizer verification not performed |
| Windows build | demonstrated | GCC 16.2.0, CMake 4.4.4, Unicorn 2.1.4 |
| Linux build/display | unsupported | Headless source branch exists but no Linux environment was available to compile/test; display backend absent |

Gate A has useful reproducible checkpoints but the full rendering/call-graph investigation remains partial. Gate B is partial: the unchanged reset bootloader now performs its own second-stage copy, CRC, inflate and application entry with explicit candidate digital MIU/BIST/BDMA models. These are not a timing-exact hardware validation. Gates C, D and E are not reached. Gate F has build/trace/storage/CPU/forensics tests but no full-TV regression suite.

The next actual boundary is the halfword at BF200A38 read by 80469898. Identify its reset/producer semantics before permitting consumption. Then trace the next guest encounters, including SPI, interrupt/timer dispatch and GOP/graphics. No guest GOP frame or original OSD has been observed. See [uncertainties](uncertainties.md) and [engine limitations](engine-limitations.md). No physical TV was modified or accessed.
