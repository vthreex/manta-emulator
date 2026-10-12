# Original TSP startup continuation

The first authentic TV frame has **not** been reached. This continuation preserves the existing MIPS engine and executes the original bootloader, runtime and normal application initialization. It advances past the previous TSP count boundary to the original auxiliary-CPU enable write. It does not emulate an OpenRISC CPU or manufacture its alive response.

## Verified execution

| Observation | Result |
|---|---|
| Original MIPS instruction dispatches | 666,276,473 |
| Original timer ISR entries / guest ticker | 1,844 / 1,844 |
| Stop PC / guest MMIO | `805D6BAC / BF202BE8` |
| Stop operation | Original write enabling the TSP auxiliary CPU |
| Original GOP initializer `80539CE4` | Entered twice through normal guest flow |
| GE parameter submissions / transfers / retirements | 626 / 626 / 626 |
| Original GOP palette commits | 64 zero-valued startup entries |
| TSP indirect reads / writes | 336 / 880 |
| TSP file-in descriptors submitted | **0**; no transport stream was drained |
| Original TSP firmware buffer | 17,416 bytes at physical `0072DB00` |
| Original firmware download | 12,288 bytes transferred to internal QMEM |
| Configured GOP framebuffer / raster commands / authentic pixels | Not demonstrated |
| New UART message after previous checkpoint | None |

The previous `@@@ Hello T4`, `USB_Init_Configure done...` and `First GOP driver instance, flush GWIN HW` remain in the original UART capture. GOP initialization alone does not demonstrate the OSD renderer or a valid enabled graphics window.

Evidence: [cold summary](../evidence/tsp-summary.json), [full trace](../evidence/tsp-full-trace.jsonl.gz), [startup disassembly and writes](../evidence/tsp-boot-disassembly.json), [original UART](../evidence/tsp-uart.log), [full deterministic replay](../evidence/tsp-regression.json).

## What caused the previous count stop

At `805D7B38`, the original `HAL_TSP_CmdCount` loads the high half of the file-in CSR at `BF202AFC` and returns its low five bits. The low half read at `805D7B3C` is not the queue count; the `ANDI` is in the `JR` delay slot. The matched empty-count routine subtracts this count from 16.

The preceding guest engine-reset/release and later command-queue reset provide the queue's producer state. A bounded 16-entry queue now derives count/full/empty from actual pending descriptors. Initial occupancy stays unknown. Engine-control bit 1 active-low reset coupling is explicitly selected with `--exploratory-tsp-reset-low`; corroborating nearby MStar references are not proof of the exact Titania4 reset circuitry. The original reset discards pending descriptors, never claims to execute them. An unsupported submitted TS transfer stops with its descriptor still pending.

There are **no file-in commands before this startup count poll**. Clearing and releasing the real reset path establishes an empty queue under the explicit reset hypothesis; it is not a fake DMA completion. Tests cover nonempty/full/reset states so a universal-zero count fails.

## Indirect register transactions

The original Titania4 HAL writes an address shifted right one to `BF202B24/BF202B28`, data to `BF202B2C/BF202B30`, and control `21h` for read or `23h` for write at `BF202B20`. Read results appear at `BF202B34/BF202B38`. The internal target is reconstructed by shifting left one, and aligned four-byte payloads at an eight-byte register stride are kept in the corroborated `00420000..0042FFFF` filter aperture.

Transactions retain unknown-bit provenance for the actual internal target. Changing the address does not change the producer of an existing read latch. A preserved unknown bit cannot be restored into another filter or peripheral. The SHA-verified original HAL region permits these opaque observations in guarded exploratory mode; unknown decisions, addresses and foreign writes still stop. This validates storage/transport effects, not unknown filter reset values or all read side effects. The related Titania3 read command `25h` is deliberately rejected because it differs from this binary.

## Original OpenRISC image and download

The guest's actual CPU_SetBase arguments identify a 17,416-byte buffer. Its complete bytes match raw SPI range **`001E7838..001EBC40`**, end exclusive, with SHA-256:

`69785f9a58c2927ceb9e92cd0fb5d8b39c9978a3c2971a7e9e6fbd9a09df189a`

At buffer offset `100`, little-endian words decode as `l.movhi r0,0`, `l.slli r0,r0,16`, then exactly 31 `l.addi r31..r1,r0,0` instructions, followed by OpenRISC stack setup and indirect calls. This is strong ISA evidence for **little-endian OpenRISC**, not PM51/8051. The decoder is bounded static analysis, not auxiliary CPU execution. The exact silicon reset PC is still unvalidated.

The original `805D819C` function programs download source units of 64 bytes, high address bits via `BF202C1C`, and length units of four bytes. Actual register writes are `CB6C` and `0C00`, recovering physical source `0072DB00` and length `3000`. A start edge captures the actual guest RAM bytes, checks bounds and invalid provenance, and transfers them into 16-KiB QMEM. The completion bit is generated by the transfer, not guest register storage. Provisional deterministic latency is 16 bytes per dispatch tick; this is not a measured hardware bus rate.

Transferred prefix SHA-256:

`cb673a6f285c933d063e9ed921bb436bf3a524ac417d8d98bcd42df84221fe1c`

Untouched QMEM bytes remain unknown. The final eight buffer bytes are `000000000000715f`; their purpose remains unknown and they are not used as an assumed checksum. The full buffer and reference libraries stay private and are excluded from the delivery archive. [Reproducible analysis](../tools/tsp_firmware_analyze.py), [analysis output](../evidence/tsp-firmware-analysis.json).

## Does TSP have to run before OSD?

TSP handles transport streams; GOP is a different display subsystem. Complete DVB demux, tuning and playback are not prerequisites we need to implement now. **This observed application startup path does require the TSP boot/command handshake.** After the enable call, original code at `805D5F30` calls the alive routine `805D7C30`; only its nonzero return branches to `805D5F88`. Failure takes an original disable/download/enable retry path. No optional exit was found in this region. This post-enable path is static disassembly evidence; the emulator has not yet executed it after enabling the processor.

The current stop is an emulator-rejected enable write, not proof that physical hardware is stuck polling. Jumping over it or returning a host-generated alive word would bypass original initialization.

## Exact next dependencies

An auxiliary little-endian OpenRISC execution core, instruction/data address translation, exception/delay-slot behavior and original firmware mailbox response are missing. The main MIPS engine remains unchanged.

The guest preserves unknown QMEM base fields at `BF202B88/BF202B8C` and `BF202B98/BF202B9C`. It ORs `E000` into the instruction/data masks at `BF202B90/BF202BA0` and sets their high halves to `FFFF`. Some low mask bits, base fields and endianness control fields retain invalid reset provenance. Trace values such as `A5A5` are **poison bytes, not hardware reset values**. A correct next implementation must establish these mappings or expose specific exploratory fixtures before executing the auxiliary code; it must not mistake the poison for a configured address.

Useful next evidence is an exact Titania4 TSP register reset specification, or read-only register captures from a working matching board during CPU_SetBase and CPU enable. Nearby source and register measurements guide hypotheses but do not establish those reset values. No physical device has been accessed or modified.

Strict candidate POR still stops at `80469898 / BF200A38`; `BF200A3C` and undocumented Winbond `CFh` responses remain unknown. The farthest run depends on all previously named exploratory clock, reset, bank, DLC and COMB assumptions. Four POR-origin RAM bytes remain invalid. It is not a strict-mode full boot.

## Reproduction and tests

Use the documented GE/GOP continuation command from [the previous report](ge-and-gop-continuation.md), adding:

```text
--candidate-tsp --exploratory-tsp-reset-low --max-instructions 1200000000
```

Optionally add `--tsp-firmware-dump PRIVATE_FILE` to retain the original guest buffer locally. Supply your original firmware; it is not distributed. `--record-trace`, `--summary` and `--coverage` capture this milestone. The default native display remains blank because guest scan-out is not configured. Window startup itself works independently of guest completion.

Validation: **346 native checks**, **2/2 CTest tests**, four forensics tests including 400 bounded random inputs, **18 baseline firmware regressions**, **nine display regressions**, previous full GE/GOP replay and new full TSP cold replay pass. The TSP regression compares the entire replay summary and event stream and hashes the actual guest-supplied payload. It fails if the original execution, reset, indirect transactions or DMA are removed. Auxiliary execution, TS demux, framebuffer rendering, remote input and settings remain unsupported. Linux and sanitizer builds have not been verified.

Sources used as comparison evidence:

- [MStar Marlon MIPS DMX library](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libapiDMX.a): matched HAL structure; platform differences remain explicit.
- [Titania3 TSP definitions](https://raw.githubusercontent.com/Benjamin-Dobell/kogan-tv-gpl/master/Kernel_updated/mstar/mstar/tsp/drv/titania3/mhal_tsp_reg.h): count/reset candidates, not copied opcode compatibility.
- [Measured MStar TSP register architecture](https://linux-chenxing.org/ip/tsp.html): QMEM mapping, download encoding and auxiliary CPU control, cross-checked against original shifts/writes.
- [MStar OpenRISC research](https://linux-chenxing.org/cpu/or1k.html): little-endian implementation context, not exact Titania4 SPR reset proof.
- [GNU OpenRISC opcode definitions](https://gnu.googlesource.com/binutils-gdb/+/150bfae1d719a3b5c12edc780f0b360a1056b802/opcodes/or1k-opc.c): independent instruction decode.
