# SPI identification and display continuation from 4cf77a4

Historical milestone retained from `1f79adc`. The newer [display-sync/DLC/COMB investigation](display-sync-and-dlc.md) supersedes the execution boundary and test counts below; strict POR and CF uncertainties remain.

The first authentic TV frame is **not achieved**. The existing Unicorn engine and guest instruction memory are retained. The newest exploratory reset-from-flash run dispatches **175,471,842** original instructions, delivers **273** stock Count/Compare ISR entries and commits **768** original gamma samples. It stops at **805FBC8C / BF205E78**, with SC bank **1** selected. GOP initializer **80539CE4 has not executed**, GOP/GE aperture events are zero, and no original framebuffer or OSD pixels have been demonstrated.

## What CF actually does

The complete original probe is 8053C574..8053C784. Caller 8053DB88 supplies a two-byte stack buffer. 8053C61C loads CF into v1; 8053C624 stores it to BF001010, the trigger-mode transmit FIFO. Actual bus traces show CS assertion followed by **CF FF FF 00**, two receive strobes, and CS deassertion. CF is an opcode, not a length, status flag, reversed byte, or identification result.

The previous REMS2 label was wrong. Matching [MStar ISP definitions](https://github.com/Benjamin-Dobell/kogan-tv-gpl/blob/master/Kernel_updated/mstar/mstar/flash/emerald/regSERFLASH.h) name CF REMS4; [HAL_SERFLASH_ReadREMS4](https://github.com/Benjamin-Dobell/kogan-tv-gpl/blob/master/Kernel_updated/mstar/mstar/flash/emerald/halSERFLASH.c) transmits the same packet. These Emerald references corroborate the observed instruction sequence; they are not proof that the complete Emerald hardware map matches Titania4. No proprietary reference implementation is copied into this project.

Macronix's [AN069](https://www.macronix.com/Lists/ApplicationNote/Attachments/1864/AN069-Migrating%20from%20MX25L6405D_6445E%20to%20MX25L6406E-0719.pdf) distinguishes 90 REMS, EF REMS2 and CF REMS4D. CF belongs to MX25L6445E's command set, not MX25L6405D REMS2. The [W25Q64BV datasheet, revision E](https://wiki.stepfpga.com/_media/w25q64.pdf) documents 90, 9F and AB identification; it does not specify CF. Neither an invented Macronix ID nor a Winbond ID is returned for CF.

After CF, the guest calls its 8053C784 **9F** routine with a three-byte buffer. The default explicitly configured W25Q64BV responds **EF 40 17**, from its documented JEDEC identity. This is a configured virtual part, not a measurement of the original physical chip. The original guest compares all three bytes against its table at 806ADEE0, stride 28h, and copies the matching 40-byte descriptor into 806FFC9C. At 8053DD14 it subsequently checks the cached CF bytes for C2 and 16/17 to choose specific Macronix variants. Deferred mode stops before this check consumes unknown data, after independently demonstrating the Winbond descriptor selection.

The strongest board evidence remains the Manta repair entry associating 25Q64BV. The supplied 162-page service manual describes a different MSD309 V7.0-A board whose U8 is MX25L3205DM2I-12G. Its schematic does not show a MISO output pull-up. It cannot establish this Manta board's undocumented CF output or electrical input level.

## Implemented hardware and its limits

* SPI NOR: CS-framed command/address/dummy phases; W25Q64BV 9F/90/AB, 03/0B array reads and wrap; WREN/WRDI on CS rising; deep-power state; separate MX25L6405D identity. Unsupported framing/opcodes stop. Persistent protection/SRP/QE bits are not inferred from the array.
* ISP: guest divider/enable/trigger, transmit FIFO, receive request/data consumption, delayed CMD/WR/RD readiness, CS cleanup and FIFO overrun rejection. Eight MSB-first clocks per byte are represented by provisional deterministic deadlines. This is not waveform-accurate oscillator validation. Guest program/erase and settings saving remain unsupported; no physical flash is written.
* XIU: original guest B4000000 flash reads address an immutable eight-MiB, overlay-aware snapshot. The programmer trailer is excluded. No guest array mutation is wired yet.
* MIU1: original guest MMU writes at 805CB7CC and TLBWI at 805CB7E0 install PageMask 1FFFE000, EntryHi C0000000, EntryLo0 01000007 and EntryLo1 01000017. CPU bus 40000000..47FFFFFF shares the existing logical MIU1 08000000..0FFFFFFF backing. C0/D0 aliases are therefore coherent. This fixes the original store at 805490A8 through D3FFF0C0; it does not allocate disconnected alias RAM.
* GOP: BF203FFC low four bits select a bank; BF203E00..3FFB stores bank-specific state. No initial bank, interrupt flags or write acknowledgment is invented. Original framebuffer programming decodes eight-byte base/pitch units and 64-bit horizontal units. MIU selection, shadow-latch activation, palettes, scaling, alpha and pixel output remain unsupported. Native guest-MIPS fixtures exercise bank switching; that is not a stock GOP boot demonstration.
* Scaler: BF205E00 selects banked BF205E04..5FFF registers. The original timing setup selects bank 10h and programs 1920x1080 active dimensions, 2200x1125 totals. The earlier description of this selector as a temporary hold was incorrect. Bank separation prevents one bank's writes from legitimizing another bank's unknown state.
* Gamma RAM: original 805E55D4 writes a ten-bit address at bank 10h BF205FB0, twelve-bit channel samples at BF205FB8/BC/C0, and starts writes with BF205FB4 bits 5/6/7. A pending transfer commits its actual sample and clears the matching busy bit after a provisional virtual deadline. Guest execution uploads 256 values for each of three channels. Each channel's observed little-endian sample sequence hashes to `41dcc47001b16039c7afe19678010e2257c3a6b80e6d32d5b315108564f1d97c`. Gamma RAM does not generate TV menu pixels.

## Strict, guarded and exploratory execution

Strict candidate boot still stops at **80469898 / BF200A38**. BF200A3C is the subsequent observation. The POR scratch interpretation and physical reset values remain unresolved. No arbitrary zero reset values were added. The four original POR-origin bytes remain invalid in every continued run.

Guarded SPI continuation uses `--defer-spi-probe`. CF receives invalid samples, not a chip ID or a claimed high-impedance response. Only the byte-verified original cache path is allowed; the first later load of these bytes stops at 8053DD14. The original JEDEC read and descriptor copy precede that stop.

Additional progress requires explicitly configured exploratory assumptions, visible in CLI options and summaries:

| Configuration | Provenance / confidence | What is not established |
|---|---|---|
| `--exploratory-spi-input pull-up` | Hypothetical CF receive samples FF / low | Undocumented chip output and Manta board pull configuration |
| `--candidate-chip-id` | Original HAL at 8054CDC4 expects identity 18h / inferred | Physical chip identity capture and reset behavior |
| `--exploratory-sc-bank 0` | Explicit initial selector / low | Hardware reset selector |
| `--exploratory-gamma-enable 0` or `1` | Explicit initial bank-10 control bit / low | Physical power-on enable value |
| `--candidate-gamma` | Original command/data/poll paths / strong digital-operation evidence | Busy latency, analog transfer and output calibration |

Both gamma-enable experiments complete 768 transfers and reach the same clock boundary. They are not claims that both values occur on a real TV.

Without an initial SC selector assumption, exact saved/restored selector bytes remain opaque. Only original byte masks/restores are permitted. Discarded timing reads are guarded against other use. Immediate destructive masks may consume known bits without legitimizing unknown reserved bits. These are diagnostic provenance rules, not invented register reset values.

Clock reads at 805FD41C / BF201751 and 805FD424 / BF201754 are snapshots in function 805FD3D4. The observed argument is **1**, whereas selector changes using these saved values occur for **2, 4 or 10h**. Byte-verified guarded mode tracks them as invalid through identity copies and interrupt save/reload frames. Any branch, calculation or device write using them stops. Twenty clock-origin stale stack bytes remain invalid at the newest stop, in addition to the four POR bytes. No clock frequency or reset selector was assigned. The subsequent original register-table RMW uses independently known RAM masks, SEH and NOR; unknown preserved bits stay unknown.

## Current exact barrier

805FBC54 selects SC bank 1 (or 3 for the alternate path), reads byte BF205E78 at 805FBC8C, restores the previous selector and returns bit 0 at 805FBCAC. Caller 805F24A4 repeatedly checks for alternating boolean states and uses the real guest software clock for a timeout. Its call at 805F24D8 returns to 805F24E0. This is an edge-wait routine, plausibly vertical synchronization; the exact Titania4 signal represented by bank-1 register 1Eh bit 0 is unverified. The stop is an emulator-rejected unknown read, not a CPU exception or proven guest hang. No arbitrary VSYNC waveform or inactive-zero response is supplied.

The next required evidence is a matching Titania4 scaler definition or a trusted read-only trace correlating bank-1/bank-3 1Eh bit 0 with input/output sync, no-source behavior and programmed timing. Also recover pixel-clock/PLL routing before using 2200x1125 totals to claim a refresh rate. A passive stock-boot SPI capture resolves CF; an established read-only debug capture resolves POR and initial selectors. No physical TV was accessed, modified or flashed.

## Reproduction and evidence

The investigative mode used for the newest trace is:

```powershell
.\bin\manta.exe --firmware ORIGINAL.bin --headless `
  --symbolic-init --candidate-miu --candidate-bdma --candidate-uart --gpio-i2c `
  --defer-diagnostic-reads --virtual-rtc --cp0-timer --candidate-utmi --candidate-spi `
  --exploratory-spi-input pull-up --candidate-chip-id --exploratory-sc-bank 0 `
  --candidate-gamma --exploratory-gamma-enable 0 --max-instructions 200000000 `
  --record-trace continuation.jsonl --summary continuation.json
```

Remove the exploratory options when investigating strict unknown boundaries; use `--defer-spi-probe` to preserve the unknown CF samples. `tools/spi_display_analyze.py` regenerates focused disassembly and direct-call references from a hash-verified local original. Firmware and extracted binaries are not distributed.

The Windows frontend now opens immediately while the guest runs on its execution thread. `--window-smoke-test` creates the native blank window without firmware and exits **0**. The old exit **2** was propagated from an intentionally incomplete guest run, not evidence of a failed Win32 window. Win32 creation/timer/message errors now report their actual error codes. No display content or menus are fabricated.

Validation: the Windows build succeeds, **145 native checks** and **2/2 CTest cases** pass, and four Python forensic tests pass (including 400 bounded random inputs). The original-firmware regression report contains **18 passing executions**, twelve verified extracted streams, both exploratory gamma-enable paths, and deterministic replay. After the final GOP change preserving unknown hardware status flags, the delivery binary separately replayed the full 175,471,842-dispatch trace with an identical summary and 273 ISR entries. Native exit **2** in this run denotes the recorded unresolved hardware boundary; it is not a successful TV boot or a window failure. Linux and sanitizer builds were not verified in this session.

Evidence: [continued trace](../evidence/display-continuation.jsonl), [summary](../evidence/display-continuation-summary.json), [UART](../evidence/display-uart-console.log), [guarded SPI](../evidence/spi-guarded-summary.json), [focused disassembly](../evidence/spi-display-disassembly.json), [firmware regression report](../evidence/regression.json), [native tests](../evidence/ctest.log).
