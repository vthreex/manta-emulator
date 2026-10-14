# Manta LCD4214 firmware execution research build

Latest [PM/application and original logo-transfer investigation](docs/pm-application-logo-transfer.md) adds an explicit symbolic non-resume path and actual guest-requested flash-to-MIU0 copying. The original logo JPEG reaches guest RAM; decoding, ZUI and authentic scan-out remain unsupported. PM hardware state is still unknown. This diagnostic path is not verified cold boot.


Latest [PM power-good/UART investigation](docs/pm51-power-good-and-uart.md)
cross-references the exact low-bit wait with MStar's `CheckPowerGood` code.
A separately configured aggregate input and candidate UART subset execute
the original serial initializer and reach PM `574F / XDATA 0E6A` after 3,474
instructions. Individual MSD309 detectors and the next write remain unknown.
Default guarded behavior is preserved. **No ZUI/framebuffer/OSD is demonstrated.**

Previous [PM producer/status investigation](docs/pm51-status-producer.md):
original RTC-related activity-eight writer traced statically; bounded
`--pm51-history FILE` diagnostics implemented. Required status remains
unknown and **no authentic OSD/framebuffer is demonstrated**.

Previous checkpoint: [original PM51 on the shared machine](docs/pm51-shared-continuation.md).
`--exploratory-pm51-spi-startup` connects original PM CODE/RIU and executes
3,435 original instructions. PM stops before an unknown-status decision at
`9B27 / XDATA 0EC2 bits0..2`; MIPS still stops at `80404BF4 / BF001CD1`.
No PM activity response, ZUI framebuffer or authentic OSD is demonstrated.
Reset order, clock ratio and unverified wiring remain explicit hypotheses.

Latest continuation: [original audio failure paths and application boot](docs/audio-timeout-continuation.md).
With the explicit `--exploratory-audio-no-ack` predicate experiment, the
guest executes both original finite DSP failure paths, prints DSP2/DSP1
Re-Active, and returns from audio/platform initialization. It reaches
1,789,567,273 MIPS dispatches and stops at unknown PM byte BF001CD1.
The DSP reply bytes remain invalid; no DSP or PM readiness response is
supplied. ZUI, an enabled framebuffer and an authentic frame remain unsupported.

The separate `pm51_probe` now executes the original flash PM51 candidate
through the existing 8051 interpreter. It stops after three instructions
without an XDATA model, or after 3,406 instructions in the isolated candidate
SPI/private-RAM experiment. PM startup status and board boot mapping remain
unknown; this probe supplies no PM output to the television emulation.
See [PM regression](evidence/pm51-original-regression.json).

This is the existing native C++20 research emulator that executes the original Manta MIPS firmware and original uploaded OpenRISC TSP firmware. It also executes the original uploaded DVB-T 8051 program in a bounded diagnostic mode. **It does not yet display an authentic TV frame or OSD.** The native window remains blank until guest graphics hardware produces a valid output.

A historical shared-MCU exploratory run executes **1,788,854,952 MIPS dispatches**, **280,644,619 OpenRISC instructions**, and **468,999 DVB-T MCU instructions**. Original MCU stores establish the shared mailbox; supported diagnostic replies come from its original IRQ handler. Normal boot still stops that MCU on unresolved XDATA 2200. The MIPS CPU additionally uploads and verifies a separate 13,312-byte AVD program, then stops at **804F9098 / BF225AB4** awaiting an unmodeled audio DSP2 response. GOP initializes twice, but no enabled framebuffer, raster output or ZUI frame is demonstrated. See [STATUS](docs/STATUS.md), [latest continuation](docs/shared-mcu-continuation.md), and [evidence](evidence/shared-final-summary.json). Strict POR and SPI-CF uncertainties remain unresolved.

`T4` is best identified as MStar's **Titania4** platform label, inferred from the original strings and independent MSD309PX hardware research. It does not identify an exact silicon revision or mean the TV finished booting. See [T4 evidence](docs/platform-t4.md).

Earlier checkpoints remain in [boot flow](docs/boot-flow.md), [TSP](docs/tsp-continuation.md), [GE/GOP](docs/ge-and-gop-continuation.md), and [application startup](docs/application-display-continuation.md). They describe historical stopping points and their original traces.

## Run the Windows build

Open `bin/manta.exe` and select your original backup, or use PowerShell:

```powershell
.\bin\manta.exe --firmware 'firmware/manta-lcd4214-original.bin' --headless
```

This strict run stops at `0xBFC00C60 / 0xBF001C74`. Exit code **2** means execution is incomplete; **1** means an input/options/build error. There is no successful full-TV exit code in this version.

The candidate reset-from-flash path is:

```powershell
.\bin\manta.exe --firmware 'firmware/manta-lcd4214-original.bin' --headless `
  --symbolic-init --candidate-miu --candidate-bdma --candidate-uart --gpio-i2c `
  --max-instructions 200000000 --record-trace cold.jsonl --summary cold.json
```

The guest performs its own bootloader copy, BIST, CRC, decompression and entry transfer. MIU/BIST is a functional digital-memory model; exact calibration pattern generators and silicon timings are unresolved. The pulled-up I2C model does not attach fake slaves. `--i2c-sda-stuck` is an explicit electrical fault fixture: the original guest emits nine recovery pulses and `IIC init fail SDA:0 SCL:1`. See [schematic cross-reference](docs/gpio-cross-reference.md).

Guarded continuation requires the supplied source-built engine:

```powershell
.\bin\manta.exe --firmware ORIGINAL.bin --headless `
  --symbolic-init --candidate-miu --candidate-bdma --candidate-uart --gpio-i2c `
  --defer-diagnostic-reads --virtual-rtc --cp0-timer --candidate-utmi --candidate-spi `
  --max-instructions 200000000 --record-trace guarded.jsonl --summary guarded.json
```

The two byte-verified cache paths remain invalid and stop at first use. RTC/UTMI/SPI are bounded functional models; silicon timing, analogue results, SPI writes and CF response are unresolved. No authentic frame is available.

`--defer-spi-probe` demonstrates original **9F / EF4017** identification and descriptor selection while preserving unknown CF samples, stopping at their first use. CF is an actual transmitted opcode, but the previous REMS2 label was wrong; see [SPI/display continuation](docs/spi-and-display-continuation.md).

Without the experimental display clock, the earlier exploratory boundary remains `805FBC8C / BF205E78`. The output-VSYNC counter requires original complete scan geometry and a guest-enabled clock gate. Frequency, monitor polarity and subwindow coupling are explicit hypotheses. Later options expose unknown subwindow/DLC/COMB initial states separately; they do not make strict mode boot. The optional paired-restore experiment quarantines unknown bits without defining reset values; without the GE reset experiment, execution still stops at `8052624C / BF205004`. `--candidate-ge --exploratory-ge-reset --candidate-gop-palette` advances into original GOP startup, with bounded parameter queue occupancy and actual guest palette pulses. These options do not produce an OSD frame.

The native window now opens immediately while the guest executes. `--window-smoke-test` needs no firmware and exits 0 after creating a blank native window. Guest stop code 2 means incomplete emulation and is independent of window initialization.

The earlier, explicitly diagnostic path remains available:

```powershell
.\bin\manta.exe --firmware 'firmware/manta-lcd4214-original.bin' --headless `
  --diagnostic-stage2 --diagnostic-init-prefix --symbolic-init `
  --candidate-bdma --candidate-uart --max-instructions 200000000 `
  --record-trace diagnostic.jsonl --summary diagnostic.json --coverage pcs.txt
```

This executes real early initialization writes, stops before MIU readiness, then **the host supplies the second-stage copy/entry transfer**. It bypasses MIU completion, BIST, and the stock first-stage copy. The original second stage then checks the modeled CRC, copies compressed bytes, executes its original zlib code, and enters the application. It is not evidence of a complete cold boot.

`--symbolic-init` carries unknown bits only through disassembled configuration RMW routines. It uses a poison value and per-byte known-bit masks. A later read that consumes unknown input/status bits still stops. Candidate MIU/BIST/BDMA/UART timing is deterministic but provisional. No instruction is patched or skipped to overcome a CPU exception.

Other working options: `--record-trace`, `--replay-trace`, `--trace-instructions`, `--coverage`, `--summary`, `--max-instructions`, `--break-pc`, `--pause-on-mmio` (physical address), `--headless`, and `--nvram` (load a compatible host COW overlay). Full instruction traces are large; normal traces record bus events and checkpoints. Replay runs the guest again and compares events; it does not substitute recorded behavior. `--nvram` does not save guest settings because guest program/erase and protection commands remain unsupported.

`--dump-frame`, media/TS/USB injection, GDB, standby/remote controls, and other unimplemented flags fail explicitly. There is no fabricated demonstration mode.

## Build and test

Windows prerequisites: Python, MinGW-w64 `g++` and `dlltool`. Run:

```powershell
.\tools\bootstrap.ps1
```

It installs pinned dependencies inside `build/.deps`, builds the checksum-verified engine from included matching source, builds the emulator with CMake/Ninja, and runs the native tests. The delivered binary was built with GCC 16.2.0, CMake 4.4.4, and Unicorn 2.1.4. The Win32 UI is a native window and file picker; no browser or web runtime is used. Linux headless source is provided but untested: this host has no WSL/Linux installation. A Linux display backend is unsupported.

```powershell
$env:PYTHONPATH = (Resolve-Path 'build/.deps')
python tools/firmware_analyze.py ORIGINAL.bin -o analysis
python tools/firmware_extract.py ORIGINAL.bin -o PRIVATE_EXTRACTED --scan
python tools/resource_analyze.py PRIVATE_EXTRACTED/stream-31bf58.bin -o PRIVATE_LOCALES.json
python tools/gpio_analyze.py --firmware ORIGINAL.bin --schematic REFERENCE.pdf --report gpio.json
python tests/test_forensics.py
python tests/firmware_regression.py --firmware ORIGINAL.bin --emulator build/manta.exe --report regression.json
python tests/display_regression.py --firmware ORIGINAL.bin --emulator build/manta.exe --report display-regression.json
python tools/comb_ge_analyze.py --firmware ORIGINAL.bin --output comb-ge-disassembly.json
# Full-path replay: supply traces/summaries from the reproduction in docs/comb-and-ge-continuation.md
python tests/comb_ge_regression.py --help
```

Extraction outputs contain your firmware; keep them private. The distribution contains no original or decompressed `.bin` files. The original `0x800010` input is accepted only at the verified SHA-256. Only the first `0x800000` bytes are flash; the last 16 bytes are never mapped. No physical TV was accessed or flashed.

The native storage module supports bounded NOR program/erase, write enable/protection, immutable stock bytes, checksummed overlays bound to the stock SHA, and atomic file replacement. These are host storage tests, **not proof of the TV's settings format or guest settings persistence**.

The Unicorn 2.1.4 generic MIPS memory-hook defect is documented in [engine limitations](docs/engine-limitations.md). The workaround removes those generic hooks; MMIO callbacks and instruction observations remain active. The regression checks the original guest inflate hash and fails to reach milestones when guest CPU execution is stopped at reset.

License: GPL-2.0-or-later for this project; see [LICENSE](LICENSE). Bundled Unicorn contains the documented timer and Windows/alignment fixes; matching source, upstream source, licenses and provenance are in `third_party/`. Reference source was used to research conventions, not assumed to match this board.

228 native checks pass, including real guest MMIO bank switching, shared MIU aliases, gamma commits, deterministic output timing, DLC status invalidation, banked COMB storage, quarantined duplicate/restore, semaphore contention, and unknown-bit provenance. Original-firmware regressions cover reset/replay, twelve streams, CPU/GPIO dependency, POR/CF guards, Winbond descriptor selection, original MMU setup, gamma upload, three original output waits and continued original ISR execution. The delivered Windows build was tested using equivalent CMake commands; the bootstrap recipe has not been run end-to-end. No Linux build or sanitizer verification is claimed.
