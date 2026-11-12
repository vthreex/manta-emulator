# Status (2026-11-12)

What the emulator does today with the **original, unmodified firmware**. Every row says how the result is obtained:

- **real**: the original firmware does the work on emulated hardware.
- **exploratory**: a labelled, opt-in model answers for hardware that is not emulated.
- **missing**: not done.

The older status page (2026-10-18) and the chronological logs are in [history/](history/).

## MStar MSD309 (Manta LCD4214)

| Area | State | How | Notes |
|---|---|---|---|
| Boot from the reset vector to the NO SIGNAL screen | works | real | Recompiled native engine: desktop ~50 s, web ~90 s. [native-cold-boot](native-cold-boot.md) |
| Save states | works | real | Load in ~1 s; full-machine, checksummed. [save-state](save-state.md) |
| Menus, source list, INFO, factory/hotel/lock menus | works | real | Drawn by the firmware's own ZUI through the emulated GE/GOP |
| Remote | works | real IR path | Proven keys: [ir-key-semantics](ir-key-semantics.md). Unproven: VOL, INDEX, ZOOM, REVEAL, HOLD, RADIO, L/R, REPEAT |
| USB pendrive: media player browsing, software update to "Success." | works | real driver, exploratory USB device | [usb-pendrive](usb-pendrive.md). Photo (JPEG) and video playback: missing |
| HDMI input (still picture) | works | exploratory signal | The firmware detects and names the mode and unmutes. The picture goes through the firmware's colour matrix. [hdmi-input-model](hdmi-input-model.md) |
| 3D (side by side / top-bottom from HDMI) | works | exploratory | Key raw 0x00. The output is a 50/50 eye mix (assumed passive panel) |
| ATV: tuner, search, fine tune, picture, interference | works | exploratory tuner + station model | [atv-tuner-model](atv-tuner-model.md) |
| ATV teletext | partly | exploratory VBI feed, real decoder | Written by another model and not reviewed. [atv-teletext-model](atv-teletext-model.md) |
| DTV: lock, scan, channel names, now/next EPG, clock | works | real firmware + exploratory demodulator/TS input | [dtv-demod-model](dtv-demod-model.md) |
| DTV video | works | real routing + exploratory MPEG-2 decode (libmpeg2) | The firmware's own PID filter chooses the stream. Shown at the stream's frame rate; black until the first picture |
| Sound (any source) | missing | the audio DSP is not emulated | For free DTV channels the model answers "audio decoding" so the firmware does not show NO SIGNAL |
| AV / SCART / S-Video / YPbPr / VGA inputs | missing | no signal models | They show NO SIGNAL |
| POWER / standby | unknown | not re-tested since 2026-10-20 | Last run stopped at an unmodelled PM register |
| REC / timeshift | missing | not tried | |
| Graphics-engine stretch filter, alpha, dither | partly | exploratory | Pixels are not verified against a real set |

### Live firmware in the browser
`site/` → **Live firmware** runs the same native engine compiled to WebAssembly. It starts from a save state and takes inputs from the
tabbed panel under the TV (USB stick, HDMI, Antenna, DTV with your own MPEG-2 `.ts` clip). In the DTV channel view the native desktop
engine runs at about 23 M instructions/s; the browser is roughly half of that.

## Sunplus SPHE8600 (Manta LED4202, Shivaki STV-24LED14)
Kernel boot, SPI-flash model, application start and DirectFB 1.1.0 initialisation work. The original menus render and can be navigated
through an exploratory GPU FIFO executor. [sunplus-sphe8600-architecture](sunplus-sphe8600-architecture.md),
[sunplus-boot-and-gpu-notes](sunplus-boot-and-gpu-notes.md).

## Tests (measured 2026-11-12)
- `core_tests`: **4,478 checks pass** (build target `core_tests`; needs no firmware).
- Python regression drivers in `tests/` need the original firmware image and Windows tooling. They were not all re-run for this page.
- No CI job builds or tests on push yet; the only workflow deploys the site.

## Known structural debts (from an outside review, 2026-11-12)
- `Msd309Machine` (src/board/machine.cpp, ~3.9k lines) and the MMIO read dispatcher should be split by peripheral.
- `Options` (~190 fields) and the hand-kept option sets in `src/main.cpp` should become a declarative registry.
- No CI, no sanitizer or fuzz jobs. Most regressions need the private firmware image.
- `src/input` and `src/inputs` both exist.
- `firmware/` contains the original vendor images: the redistribution question is open (owner's decision).
