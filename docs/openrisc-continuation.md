# Original TSP execution and application continuation

The original uploaded TSP firmware now **executes and supplies the MIPS alive response**. The original MIPS application continues through normal control flow. The first authentic OSD frame is **not achieved**. Results depend on explicitly selected hardware/reset/clock experiments; strict boot uncertainties remain unchanged.

## Farthest preserved cold run

| Measurement | Result |
|---|---|
| Original MIPS dispatches | 932,954,776 |
| Original TSP instructions successfully executed | 66,669,575 |
| Original guest timer ISR entries | 2,698 |
| Original auxiliary instruction words matching immutable upload | 66,669,575; zero changed words |
| Unique original auxiliary PCs | 207 |
| TSP startup / main loop | Both entered through original code |
| MIPS alive-success branch | `805D5F88` executed |
| GOP initializer `80539CE4` | Entered twice |
| Successful GOP aperture events | 8,249 |
| GE aperture events | 2,924; no demonstrated raster drawing |
| Authentic framebuffer / native guest pixels | Not demonstrated |
| Final stop | MIPS `804F0C48`, byte read `BF204B15` |
| New original UART messages | Original TSP upload diagnostics; no TV-ready banner |

Original upload diagnostics include `[HAL_TSP_CPU_SetBase][1708] load firmware (address, size) = (0x0072DB00, 0x00004408)` and `firmware 111 0x0072DB00 0x00010000 0x0000CB6C`. Earlier `@@@ Hello T4`, USB initialization and GOP initialization messages remain intact.

Evidence: [summary](../evidence/or1k-summary.json), [full event trace](../evidence/or1k-full-trace.jsonl.gz), [UART](../evidence/or1k-uart.log), [dual-CPU replay](../evidence/or1k-dual-regression.json), [provenance](../evidence/or1k-provenance.json).

## Actual code, architecture and memory

The complete original 17,416-byte TSP buffer is raw SPI `1E7838..1EBC40`, with SHA-256 `69785f9a58c2927ceb9e92cd0fb5d8b39c9978a3c2971a7e9e6fbd9a09df189a`. The guest loads it at physical `0072DB00` and downloads the first 12,288 bytes. Download-prefix SHA-256 is `cb673a6f285c933d063e9ed921bb436bf3a524ac417d8d98bcd42df84221fe1c`. No reference firmware, transformed host image or patched guest instructions are substituted.

The executed code matches **little-endian ORBIS32/OpenRISC with delay slots**. Exact silicon revision and SPR feature values remain unconfirmed. The earlier bounded analysis decoder had JR/JALR names reversed; it is corrected against the primary opcode definitions. The execution core uses JR opcode `11h`, JALR opcode `12h`, writable r0 and PC+8 call links. Registers begin unknown and become known through original initialization instructions.

The selected exploratory reset PC is `100`, consistent with the original register-clear prefix and conventional OpenRISC reset vector, not a measured silicon reset address. Startup sets stack top `2700`, enters trampoline `1F04`, initialization `1DBC`, then main `D94`. Mailbox dispatch is `1C4`.

QMEM is one shared 16-KiB backing with per-byte validity. The guest's original DMA populates `0000..2FFF`; original auxiliary stores change that same backing. The selected reset fixture lets original configuration establish instruction window `0000..1FFF` and data window `2000..3FFF`, using masks `FFFFE000` and bases zero/`2000`. Untouched bytes stay unknown. Unprogrammed reset fields and little-endian bus controls are explicit assumptions. General auxiliary MIU execution/access outside the implemented windows remains unsupported; no separate fake shared DRAM is supplied.

Internal auxiliary register addresses are connected to the same storage used by MIPS indirect transactions. Command/response aliases use the same backing as direct MIPS registers, including split 16-bit RIU layout. Native tests verify these aliases instead of maintaining independent processor mailboxes.

## Core and scheduling

An auxiliary C++ interpreter was added alongside the unchanged MIPS engine. It supports the original observed immediate/register arithmetic and logic, comparisons, byte/halfword/word loads and stores, direct/indirect jumps, branches and delay slots, plus a limited SR/SPR and exception subset. Alignment exception entry and RFE have native tests. Unknown data affecting an address/comparison, unsupported opcodes/SPR accesses and unsupported bus ranges stop explicitly. External auxiliary IRQ/tick/MMU/cache fidelity and the complete ISA are not claimed.

QEMU 9.2's OpenRISC implementation and decoder were evaluated as primary architectural references. They depend on QEMU CPU/TCG/system infrastructure rather than a small standalone embedding API, and the MStar little-endian bus requires adaptation. Integrating a second complete QEMU target was unsuitable for this focused Windows continuation. No QEMU implementation source was copied into the interpreter, and the existing MIPS DLL is unchanged.

Both CPUs run on a deterministic dispatch timeline, with no host-thread races. `--or1k-dispatch-divider` specifies MIPS dispatches per auxiliary instruction. The farthest run uses **four**, an unmeasured clock relationship. Earlier one-to-one runs also completed the genuine handshake and executed up to 100 million original auxiliary instructions before their diagnostic budgets. Budgets are emulator limits, not hardware failures; no instructions are skipped to increase progress.

`--or1k-trace-first-visits` records each auxiliary PC once while every instruction fetch is still compared with the immutable uploaded words and counted. All modeled bus operations remain in the event trace. Detailed repeated instruction tracing is the default when this option is absent. Replay checks the complete event sequence, both execution counts and a digest of auxiliary registers/validity, SR/SPR, branch state, QMEM contents/validity and scheduling position. This digest is not a complete machine snapshot or rewind implementation.

## Genuine alive sequence

1. Original MIPS programs source/length and triggers the firmware DMA.
2. DMA installs actual guest RAM bytes and produces download completion.
3. Original MIPS writes CPU enable/reset release at `805D6BAC / BF202BE8`.
4. Original auxiliary startup clears registers, initializes stack/data and enters its main loop.
5. Main writes controller pointer `210200` to its own global at `2330`.
6. MIPS writes command `01000000` at `BF202B00/BF202B04`.
7. Original auxiliary handler reads `210330`, writes response `01000000` at `21033C`, then clears `210330`.
8. MIPS observes the cleared command and expected response at `BF202C00/BF202C04`, takes its original success branch and continues.

The response comes from original auxiliary instructions, not a host ready word. The version path contains original value `14050200`; no vendor/version reply table is fabricated. With auxiliary execution disabled, the old enable boundary remains the tested stop.

## Subsequent boundaries and explicit experiments

The original STC control routine at `805D8A90` programs `BF201614/1618/161C` and internal registers `420498/4204B8`. Matching MStar HAL disassembly identifies the control sequence. Guarded configuration reads preserve invalid fields through the SHA-verified original routine; a counter/PLL implementation is not inferred from ordinary register storage.

The original function at `80500270` caches observations from `BF200198/BF20019C`. `--defer-platform-identity` verifies the exact routine SHA and preserves four additional invalid bytes through its stores. Their hardware meanings are unresolved; no identity, license or authorization value is supplied. Unknown consumers still stop. This is separate from the four existing invalid POR bytes.

`BF001CDD` is identified as PM PowerOnMode by a matching HAL reader. The related header names `FF` as AC-on, with separate standby/sleep/deep-sleep values. `--exploratory-pm-ac-on` supplies an explicit external AC-on boundary. It is not PM51 execution, wake-key injection or a ready acknowledgement. Without it, the guest stops at `80404BD8`.

The AC-on path scans SAR bytes `BF002834/BF002835`, comparing ranges in original code. `--exploratory-sar-fullscale` supplies 3300-mV inputs/reference on two channels. Guest digital/analog power and free-running controls and sample-period programming cause timed conversion; reads do not toggle or manufacture samples. Native tests vary voltages and power/timing states. Actual board wiring/reference voltage and conversion timing remain unverified. Without this fixture, the original data read at `804F1518` stops.

Original IR code programs physical ring address `03847000`, capacity 360, and uses 48-byte software records. `--exploratory-ir-dma-reset` models an empty producer index after original descriptor command `82h`; its reset/index coupling is unvalidated. A native circular-record model tests actual byte movement and cursor changes. No external captures or decoded keys are supplied in the cold run, and host remote input/IRQ delivery remain unsupported. Without this experiment, `80527F28 / BF206E79` stops.

The application then queries GWIN enable at `8053787C`. Original GOP initialization pulses top-bank bit 0 and deasserts low control bits. `--exploratory-gop-reset-windows` interprets that pulse as disabling the corresponding window-enable bits, leaving format, addresses and all unrelated fields unknown. Exact reset scope remains unverified. Native tests ensure the pulse affects only its plane and does not create a framebuffer. Without this experiment the original enable decision is correctly rejected.

All experiments are optional and explicit. Strict candidate boot still stops at `80469898 / BF200A38`; undocumented Winbond `CFh` response and the previous clock/reset/bank/COMB assumptions remain unresolved.

## Exact remaining blocker

At `804F0C48`, original code reads **`BF204B15`**, masks the low nibble, and returns whether it is **at least four**. Its direct callers are `803E29CC` and `803E2B1C`; the current wrapper returns at `803E29D4`. This is a required hardware-dependent branch, not an idle loop or coprocessor timeout. A related Marlon `libapiXC.a` symbol, `Hal_ADC_is_scart_rgb`, accesses this same byte, but checks mask `50h` against `10h` and sets bit 5. Its calibration function also uses those fields. This supports a **SCART-related ADC detection** interpretation; it does not prove the Manta low-nibble transfer or status semantics. The Manta threshold is four, and no corresponding original producer or board input mapping is established. No reference bit mask, zero/no-input value or ADC voltage transfer is transplanted. There is one literal `204B15` reference in recovered runtime; computed/table-based accesses are not ruled out.

Do not assign zero/four merely to select a path. Next evidence is an exact Titania4 definition/ADC transfer and board pin for this byte and its producer, or a read-only register capture from a working matching board at this query. Any capture requires verified board-specific access/test points and preserves the flash backup; no physical TV was accessed during this work. The [original predicate and comparison provenance](../evidence/or1k-next-blocker.json) record the difference from the [related MStar archive](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libapiXC.a).

## Reproduction and validation

Use the previous TSP continuation command, adding:

```text
--exploratory-tsp-cpu --defer-platform-identity --exploratory-pm-ac-on
--exploratory-sar-fullscale --exploratory-ir-dma-reset
--exploratory-gop-reset-windows --or1k-trace-first-visits
--or1k-dispatch-divider 4 --max-or1k-instructions 200000000
--max-instructions 2000000000
```

Supply the immutable original firmware locally. Record trace/summary/coverage paths with enough free space. A disk-full experiment produced truncated evidence and is excluded from the verified milestone. Logging now stops on a trace write failure, and incomplete coverage/summary writes report errors. Generated large raw traces were moved to a dedicated D-drive cache; verified compressed evidence is retained with the repository.

398 native checks and 2/2 CTest tests pass, including native window startup. Baseline 18-run, display nine-run and prior TSP full replay regressions retain their original checkpoints. The new [dual-CPU regression](../tests/openrisc_regression.py) verifies original words, startup entries, actual response writes, the MIPS success branch and deterministic replay. Final detailed results are recorded in the linked evidence/provenance. The [executed disassembly](../evidence/or1k-executed-disassembly.json) lists only the 207 observed original auxiliary PCs, rather than treating unexecuted data as code. To rerun full replay, decompress the preserved JSONL gzip to a scratch trace path; the emulator accepts a plain JSONL trace. No original firmware bytes are bundled.

No authentic framebuffer allocation, GE raster operation, native OSD pixels, remote navigation, settings persistence, TS playback or HEVC enhancement is demonstrated. The next graphics work must follow original application execution past the hardware-dependent branch. [HEVC feasibility investigation](hevc-investigation.md) remains a separate future capability.

Primary comparison references: [QEMU OpenRISC decoder](https://raw.githubusercontent.com/qemu/qemu/v9.2.0/target/openrisc/insns.decode), [QEMU translator](https://raw.githubusercontent.com/qemu/qemu/v9.2.0/target/openrisc/translate.c), [MStar TSP register research](https://linux-chenxing.org/ip/tsp.html), [MStar little-endian OpenRISC context](https://linux-chenxing.org/cpu/or1k.html), [MStar PM enum/header](https://raw.githubusercontent.com/Toysoft/mstar/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/include/drvPM.h). These do not establish exact silicon reset values or full board compatibility.
