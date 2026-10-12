# Original PQ uploads and guarded application continuation

The original Manta firmware now completes two Y-filter coefficient uploads, totaling **256 five-byte entries**, and continues through further SCMI configuration. **The first authentic OSD frame is still not achieved.** These results use explicitly selected input, reset and clock experiments. Strict boot does not acquire their assumed values.

Development continued from `59bf60152dcbeffc363645d689b825d426cada89`. The existing OpenRISC interpreter, TSP mailbox implementation, MIPS engine and previously demonstrated peripherals were preserved. The delivery commit is recorded in the [build manifest](../../Manta_LCD4214_build_manifest.json).

| Requested observation | Verified result |
|---|---|
| Furthest original MIPS PC | `806006D4`, reading byte `BF003C05` |
| Original MIPS instruction dispatches | **933,062,691** |
| Genuine original OpenRISC instructions | **66,696,554**; all fetched words match the original upload, zero modified words |
| Original timer ISR entries | **2,698** |
| Auxiliary CPU at the stop | Running, PC `143C` |
| Interprocessor communication | Original alive reply and MIPS success branch remain demonstrated; original command polling continues. Another successful command/reply cycle is not demonstrated |
| New UART messages | None after the previous TSP upload diagnostics. `@@@ Hello T4`, USB completion, GOP flush and original TSP upload messages remain present |
| Original TV application progress | Additional original PQ and SCMI initialization executes; interactive application readiness is not demonstrated |
| GOP initialization | `80539CE4` entered twice; **8,249** successful aperture events |
| Graphics probes | Six original window-programming entries during reset, 330 generic 32-bit GOP register-helper entries; no enabled valid framebuffer descriptor |
| GE | 626 parameter submissions/transfers/retirements; no demonstrated raster drawing commands |
| Framebuffer allocation/writes | No authentic framebuffer has been identified or demonstrated. General RAM writes and coefficient storage do not establish one |
| Native pixels | No authentic guest pixels rendered; the native window starts successfully and remains blank |
| Scaler/panel | Original 1920×1080 configuration, totals 2200×1125, 768 gamma samples and 12 experimental output-sync transitions remain demonstrated |

The [summary](../evidence/application-summary.json), [UART](../evidence/application-uart.log), [full trace](../evidence/application-full-trace.jsonl.gz), [disassembly](../evidence/application-disassembly.json), [regression](../evidence/application-dual.json) and [provenance](../evidence/application-provenance.json) preserve the observations.

**What advanced execution.** An explicitly grounded SCART FB fixture supplies only a hypothetical low-nibble ADC code zero after the original configuration sequence. The actual Titania4 ADC transfer, pin mapping and upper status fields remain unverified. A separately configured SCMI initial format byte supplies the low byte only; its upper fields retain invalid provenance. An explicitly disconnected analog input produces NORMAL/no Euro or Australia HDTV detection in the original two-bit predicate. Positive-pattern detection and its timing are unsupported.

The alternate format-byte `F0` experiment remains blocked reading timing-word **bit 8** at `805F8A28 / BF205E78`, bank 1. The related named PQ implementation identifies this field as input VSYNC polarity. It is distinct from output VSYNC bit 0. No input polarity or free-running input-sync behavior was fabricated to advance that path. The primary continuation uses initial format byte `00`, an unvalidated reset experiment.

The RMW proof checker now accepts an independent `SLTU`, known-argument BEQ/BNE paths with their delay slots, and SEH that preserves the sampled low sixteen bit identities. Sample-dependent branches, branch instructions in a branch delay slot, escaping stores and calls remain rejected. Original instructions still execute in the existing CPU engine. At `806001D0` the guest clears only bit 0 and preserves the other fifteen sampled bits. At `805FDC3C` and `805FDD24`, it clears bits 4 and 5 of bank-12 control register 03 through its original conditional paths. These corrections supply no reset/status constants and change no guest PC.

**Coefficient SRAM is a real data path.** The original Y uploader is `803C8250`; the C uploader is `803C8424`. The bank-23 port uses:

| Register | Observed role |
|---|---|
| `BF205F04`, reg41 | Y/C write-enable selection in bits 0/1; write/busy bit 8 |
| `BF205F08`, reg42 | Destination index; original Y writer masks `01FF`, C writer masks `00FF` |
| `BF205F0C`, reg43 | First two coefficient bytes |
| `BF205F10`, reg44 | Next two coefficient bytes |
| `BF205F14`, reg45 | Fifth coefficient byte |
| Bank00 `BF205E0C`, reg03, bit12 | Original uploader brackets access by clearing/restoring this field; exact electrical gating is unverified |

The model captures the actual payload/address on each selected-port write, stays busy until a deterministic deadline, then stores all five bytes with validity in separate Y/C arrays. It does not toggle status on each read. Disabling both ports cannot submit another transaction through a preserved busy field; the preceding captured transaction completes normally. Conflicting/unknown modes, addresses or payloads stop explicitly.

Both observed Y tables are 640 bytes. Source `8065F368` supplies type 1, indices `80..FF`; source `8065E464` supplies type 0, indices `00..7F`. Both original payloads have SHA-256 `754eaf4b027fd439926c919f883ff2d03db2e13376f4bfe1be9c2525e87ebeb4`. All **256 submissions and 256 commits** occur through original guest writes. The actual SRAM state, including validity and plane/index layout, hashes to `4c5c6b042a14eee6b6f77709b05f1cd8b706c2e28dd8f50b7a5907d963e434ed`; the firmware regression independently reconstructs and verifies it from the original uploads.

Initial idle status and initially disabled Y/C ports require **two separate experimental flags**. The 64-MIPS-dispatch transfer latency is provisional. The C writer's eight-bit mask does not prove the ninth address bit irrelevant: the model requires it known and rejects unverified upper C banks. C storage has native tests and an original disassembly basis; no original C-table execution is claimed. Filtering arithmetic and analog pixel processing are unsupported. Coefficients are not OSD pixels.

The [related Marlon PQ implementation](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/driver/pq/hal/marlon/mhal_pq.c) corroborates the port sequence and VSYNC-polarity field. Its Y loop length and upper C-bank indices differ from this original binary: Manta uses 128 Y entries per table and C offsets `00/40/80/C0`. Marlon's constants were not substituted for them. Source hashes and compatibility limitations are preserved in provenance; proprietary reference archives are not bundled.

**The SAR-aperture observation stays invalid.** Original caller `803BE898` reads `BF002880` through `805F87DC`, discards the result, then writes masked configuration at `BF0028C0`. A SHA-verified guarded observation tracks the discarded value until the original code overwrites it. Its hardware meaning and read side effects are unresolved. This is exploratory deferral, not a validated SAR hardware response. Any unknown value used in a decision, address or another device operation remains prohibited.

**Exact remaining blocker.** `806006CC..806006DF` loads the RIU base, reads `BF003C05`, and returns `(byte < 2)`. Its only direct caller is `805F4B10`. A true result adds two to a 16-bit fetch-size parameter, capped by another original field, before later memory configuration. This is a first emulator-rejected hardware read, not evidence of a polling loop or exception storm. The value cannot be discarded or treated as an unused observation.

The [related XC archive](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libapiXC.a) has `Hal_SC_IsOPMFetchPatch_Enable`, consistent with the caller's fetch adjustment, but returns constant false on Marlon. That does not prove a response on Titania4. The [complete trace's overlapping-access history](../evidence/application-fetch-field-history.json) contains no earlier guest write to this byte, so no software-programmed bits establish the predicate. **Its exact identity, producer and valid value remain unresolved.** It is not silently equated with the separately modeled device-ID register `BF003D98`, the CHIPTOP version definitions, CP0 PRId or another board's revision. No value was assigned to escape this decision.

The next evidence needed is an exact Titania4 definition for this byte, or a read-only observation on the matching board at this boot stage. With a documented and electrically verified debug interface, capture the instruction's actual read, the base pointer at `806B963C`, and preceding writes to the surrounding RIU word. Record the board/SoC markings and whether the byte is constant across cold starts. A related-board dump can help locate an equivalent function, but cannot establish this Manta's register value. No physical TV was flashed, edited or probed during this work.

**Strict and exploratory results.** Default strict execution still stops at `BFC00C60 / BF001C74`. The existing candidate reset boot stops at unresolved POR `80469898 / BF200A38`. Undocumented Winbond `CF` behavior remains unresolved; the explicitly selected electrical pull-up experiment is retained. Earlier clock, subwindow, DLC, COMB, semaphore, GE/GOP, TSP, PM/SAR and IR experiments remain unvalidated where previously stated. New experiments do not silently become strict behavior.

**Validation.** The final Windows build passes **436 native checks**, **2/2 CTest** including native window startup, four forensics tests including 400 bounded random inputs, **18 baseline firmware runs**, **nine display runs**, the previous full dual-CPU replay and the new full dual-CPU replay. The MIPS DLL is unchanged. The regression verifies actual original auxiliary words, original MIPS alive-success execution, SCART/detection predicates, both coefficient payloads and address loops, actual SRAM contents, masked/conditional stores and the complete final summary/event replay. The conservative C-address ninth-bit guard is included in this final validation. This is boot/peripheral coverage, not a working-menu/media test suite. Linux and sanitizer builds are unverified.

The original firmware remains SHA-256 `d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9`, length 8,388,624 bytes. Its trailing 16 bytes remain outside mapped SPI capacity. No instruction memory or guest PCs were patched. Native remote interaction, PM51 execution, stock settings saves, media/DVB/audio/PVR/3D and HEVC enhancements remain unsupported. The earlier [OpenRISC report](openrisc-continuation.md) retains the original TSP image location, hashes, architecture, reset/memory assumptions and instruction support.

To reproduce the latest exploratory stop with your own immutable backup:

```powershell
.\bin\manta.exe --firmware ORIGINAL.bin --headless `
  --symbolic-init --candidate-miu --candidate-bdma --candidate-uart --gpio-i2c `
  --defer-diagnostic-reads --virtual-rtc --cp0-timer --candidate-utmi `
  --candidate-spi --exploratory-spi-input pull-up --candidate-chip-id `
  --exploratory-sc-bank 0 --candidate-gamma --exploratory-gamma-enable 0 `
  --exploratory-display-clock 148500000 --exploratory-subwindow-enable 0 `
  --exploratory-dlc-pending 0 --exploratory-dlc-timeout `
  --exploratory-comb-bank 0 --exploratory-comb-paired-restore `
  --exploratory-gop-bank 0 --candidate-semaphore --exploratory-semaphore-idle `
  --candidate-ge --exploratory-ge-reset --candidate-gop-palette `
  --candidate-tsp --exploratory-tsp-reset-low --exploratory-tsp-cpu `
  --defer-platform-identity --exploratory-pm-ac-on --exploratory-sar-fullscale `
  --exploratory-ir-dma-reset --exploratory-gop-reset-windows `
  --exploratory-scart-grounded --exploratory-scmi-format-byte 0 `
  --exploratory-analog-disconnected --exploratory-pq-sram-idle `
  --exploratory-pq-disabled-ports --graphics-probes --or1k-trace-first-visits `
  --or1k-dispatch-divider 4 --max-or1k-instructions 400000000 `
  --max-instructions 2500000000 --record-trace current.jsonl `
  --summary current.json --coverage current.coverage
```

Expected guest-incomplete exit code is **2**. Omitting `--headless` opens the native display, which has no authentic frame yet. Firmware is supplied locally; it is not included in the research build.
