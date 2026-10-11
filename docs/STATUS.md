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
| Strict reset boot | partially implemented | Stops `BFC00C60 / BF001C74`: [trace](../evidence/reset.jsonl) |
| Symbolic reset initialization | partially implemented | Reaches MIU readiness poll, does not invent ready: [trace](../evidence/sboot-symbolic.jsonl) |
| Original guest CRC check, copies, inflate, main entry | demonstrated | Explicit stage-two diagnostic bypass; [regression](../evidence/regression.json), [deepest summary](../evidence/prefix-uart-summary.json); about 90 million instruction dispatches |
| Candidate UART transmission | demonstrated | Provisional timing, no RX/IRQ; original `Hello T4` [output](../evidence/prefix-uart-console.log) |
| Full firmware-from-reset boot | unsupported | MIU completion/BIST/stock relocation path not demonstrated end to end |
| Host copy-on-write NOR storage | implemented | Bounded program/erase, protection, image-bound checksum, atomic replacement; native tests |
| Guest SPI writes and saved TV settings | unsupported | Host storage is not wired to a guest SPI controller; redundancy/checksum/generation format unresolved |
| Native Windows window/file selection | demonstrated | [Smoke log](../evidence/native-window-smoke.log); blank guest display, no fabricated menu |
| GPIO/I2C attached devices | unsupported | Stops `8053BDE4 / BF203D05` input mask `04`; paired GPIO 65/66, probable SDA/SCL bus recovery |
| GOP/graphics/scaler/panel, authentic OSD | unsupported | No guest frame, fonts or graphics codec decoded yet |
| PM51, IR remote, front panel, standby/wake | unsupported | No arbitrary key injection or fabricated power state |
| Video input, DVB, ATV, USB/media, audio, PVR, 3D | unsupported | No guest device models or authentic menus demonstrated |
| Menu/settings/hotel/factory behavior | unsupported | Manual contract and original strings are evidence, not observed interaction |
| Deterministic trace replay / PC breakpoints | demonstrated | Guest reruns and events compared in regression; complete snapshots/rewind/GDB unsupported |
| Input bounds and DEFLATE random-input tests | demonstrated | [Python tests](../evidence/python-tests.log); sanitizer verification not performed |
| Windows build | demonstrated | GCC 16.2.0, CMake 4.4.4, Unicorn 2.1.4 |
| Linux build/display | unsupported | Headless source branch exists but no Linux environment was available to compile/test; display backend absent |

Gate A has useful reproducible checkpoints but the full rendering/call-graph investigation remains partial. Gate B is partial: authentic guest inflation and application entry were demonstrated only with an explicit second-stage diagnostic transfer. Gates C, D and E are not reached. Gate F has build/trace/storage/CPU/forensics tests but no full-TV regression suite.

Next useful implementation targets are MIU/BIST for the strict path and an evidence-backed GPIO/I2C bus model for the diagnostic runtime. Then interrupt/timer dispatch and original GOP/graphics paths can be traced. See [uncertainties](uncertainties.md) and [engine limitations](engine-limitations.md). No physical TV was modified or accessed.
