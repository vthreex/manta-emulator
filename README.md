# Manta TV firmware emulator

Booting the **real, unmodified firmware** of Manta televisions in software and watching its own OSD come to life: no UI re-created in host code, no patched firmware, no invented hardware answers. Two very different platforms are covered:

| platform | sets | status |
|---|---|---|
| **MStar MSD309** (Titania4, MIPS) | Manta LCD4214 | original firmware boots, the real ZUI draws the OSD, MENU / SOURCE / INFO / factory / hotel / lock menus, USB player, virtual remote |
| **Sunplus SPHE8600** (MIPS + DSP ASE, eCos + DirectFB) | Manta LED4202, Shivaki STV-24LED14 | kernel boot, SPI-flash model, application starts, DirectFB 1.1.0 initialises; real OSD menu renders and is navigable (exploratory GPU model) |

**[Interactive showcase page](https://vthreex.github.io/manta-emulator/)** (a remote you can press: it replays a recorded Sunplus menu session frame by frame, plus a boot walkthrough and a gallery of captured frames) | **[Releases](../../releases)** | [Architecture notes](docs/sunplus-sphe8600-architecture.md) | [Firmware images and hashes](firmware/README.md)

## Gallery (real frames from the emulator)

| | | |
|---|---|---|
| ![Option menu](evidence/ui-states-session10/5_menu_setup_blue_on.png) | ![Input sources](evidence/ui-states-session10/9_input_source_list.png) | ![USB player](evidence/ui-states-session10/10_usb_player_no_device.png) |
| Option menu (MENU key) | Input source list | USB player, no device |
| ![Factory menu](evidence/ui-states-session10/12_factory_setting_root.png) | ![White balance](evidence/ui-states-session10/14_factory_wb_adjust.png) | ![Hotel menu](evidence/ui-states-session10/15_hotel_menu.png) |
| Factory menu | Factory white balance | Hotel menu |
| ![DTV banner](evidence/ui-states-session10/17_dtv_channel_banner.png) | ![Lock dialog](evidence/ui-states-session10/16_lock_password_dialog.png) | ![NO SIGNAL](evidence/no-signal-dialog-moved-s13.png) |
| Digital TV banner | Lock password dialog | NO SIGNAL dialog drawn by the original ZUI |

| ![Sunplus main menu](evidence/sunplus/manta-led4202-main-menu-english.png) | ![Sunplus tabs](evidence/sunplus/manta-menu-tabs-sound-feature-setup.png) | ![Sunplus CI message](evidence/sunplus/dtv-source-ci-module-message.png) |
|---|---|---|
| Sunplus LED4202: main menu | Sound / Feature / Setup tabs | "CI Module is initializing" after selecting DTV |

Sunplus boot logos (recovered from the flash images), the 632-bitmap OSD atlas and the recorded menu session are on the showcase page.

## Highlights

- **Strict by default.** Unknown hardware stays unknown; every hypothesis is opt-in, labelled and listed in the run report. Diagnostic results are kept separate from connected-boot progress and from displayed pixels.
- **Fast.** Exact idle-loop skipping (OpenRISC TSP core, 8051 cores), a recompiled native engine for resumed runs, and full-machine save states (a checkpoint loads in about a second).
- **Qt control centre** (`frontend/manta_control.py`): board switch, remote (keys and mouse), machine settings, live preview, frame sharing over shared memory.
- **Reverse-engineering notes** for both platforms: register maps, boot flow, flash layouts, interrupt controller, GE/GOP/OSD formats (`docs/`).

## Quick start

Download a build from [Releases](../../releases), or build from source (CMake + MinGW, Unicorn 2.1.4; the patched Unicorn source is in `third_party/`).

```powershell
.\bin\manta.exe --firmware firmware\manta-lcd4214-original.bin --headless
```

The firmware images used for testing live in `firmware/` (hashes and a rights notice in `firmware/README.md`). The Sunplus runner is `sunplus_run` (see `docs/sunplus-sphe8600-architecture.md`).

## Layout

`src/` emulator core | `native/` recompiled-engine runtime | `frontend/` Qt control centre | `tools/` analysis and test drivers | `docs/` findings and register maps | `evidence/` validation logs and captured frames | `configs/` policies and key maps | `firmware/` test images | `site/` GitHub Pages showcase

## Authorship and credits

This project was directed, tested and published by **v3x**, and built together with AI coding assistants, credited as they were used:

- **OpenAI Codex** did the first reverse-engineering phase (2026-10-11 to 2026-10-18).
- **Claude** (Anthropic, via Claude Code) did the work after that hand-off: the emulator extensions, the Qt control centre, the Blaupunkt/Sunplus bring-up and this site. Those commits carry a `Co-Authored-By: Claude` trailer, so Claude appears as a contributor in the history.

## Licence

GPL-2.0 (see `LICENSE`), matching the bundled Unicorn engine. Firmware images and extracted artwork belong to their owners and are included for interoperability and preservation research only.

---

## Development log

Newest entries first; each one records what the original instructions executed and what is still unknown.

### Manta LCD4214 firmware execution research build

Latest [HTotal provenance/window-resource work](docs/htotal-provenance-and-zui-resources.md)
allows the original timing publisher to copy INVALID raw samples without
inventing measurement bits. Isolated original-instruction tests verify timing
copies, getter widths, finite stability branches and all 29 original OSD
resource sets (3,616 window records). Connected counts and blockers remain
unchanged: no ZUI dispatcher, active OSD or authentic pixels. The window-state
initializer executes with count zero during ordinary ZUI initialization.

Latest [ZUI-flow and AVD-event investigation](docs/zui-flow-and-avd-events.md)
observes original UI state 1, an empty 40-slot queue and zero windows after
initialization. Platform polling stops on invalid HTotal before the dispatcher.
Read-only probes preserve all prior CPU/peripheral state and coverage. Original
queue/dispatcher function tests and 512 isolated AVD event cases add coverage;
unsupported event acknowledgments cannot manufacture pending events. No authentic
framebuffer or OSD pixels are demonstrated. ZUI's acronym remains unverified;
related window zoom animations do not establish a zoomable-canvas interface.

Latest [AVD continuation](docs/avd-symbolic-code-tables.md) executes 863
original AVD instructions without inventing its startup selector. Conservative
ROM-data uncertainty reaches the firmware's own `119C=0` writer. The next
AVD decision needs unknown `11DB.7`; main MIPS still stops on unknown HTotal
at `80501BFC`. Original ZUI initialization returns, but its dispatcher and
authentic OSD pixels remain unobserved. 1,435 native checks pass.

The following checkpoint descriptions preserve their individual scopes.

Current [VIF input experiment](docs/vif-zero-energy-experiment.md): a separately
configured ideal noiseless IF/gain-equilibrium hypothesis lets the original VIF
handler return naturally. Main execution reaches `805032A0 / BF206BB8`, an AVD
MCU register-service request whose producer has not run. This is not a verified
disconnected-antenna boot. Strict VIF measurements remain invalid.

ZUI initialization enters and returns; its dispatcher, enabled framebuffer and
authentic OSD pixels remain unobserved. All 1,346 native checks, 18 baseline and
nine display runs pass. Complete five-CPU replay verifies 6,591,839 events,
summary, coverage and PM history. Original firmware remains unchanged.

The older checkpoint descriptions below retain their historical results.

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
