# CPU engine and diagnostic limits

Unicorn 2.1.4 is a QEMU-derived engine with a reproducible Count/Compare extension and Windows/alignment fixes. Matching source is included. This build selects little-endian MIPS32 with its 24Kc model. Selection is a compatibility choice, not a verified identification of the television CPU. Titania4 [research](https://linux-chenxing.org/titania4/) reports a 34Kc; this project's exact chip suffix and core must be confirmed. A core/CP0 mismatch can affect code that probes PRId, uses multithreading facilities, or relies on cache and exception behavior.

Original guest code runs unchanged. The deep run observes 90,038,632 instruction dispatches and 4,388 distinct PCs. Dispatch counters are observations, not retired-instruction counts or calibrated cycles. Guest decompressor output is independently SHA-256 checked at application entry.

Native tests cover little-endian loads/stores, KSEG aliases, branch delay slots, branch-likely annulment, a JR/store delay slot returning into a branch, and writable CP0 status. They do not establish every MIPS instruction, exception, TLB, cache, or coprocessor behavior. Engine exceptions currently stop with diagnostics. Optional CP0 timer delivery uses QEMU architectural exception entry and the original guest ISR. General board IRQ delivery and cache timing remain unsupported.

A minimized Unicorn 2.1.4 MIPS failure was observed with generic `UC_HOOK_MEM_READ` or `UC_HOOK_MEM_WRITE` installed: a JR with a load/store delay slot returning directly to a branch caused an engine exception 20 and PC zero. Removing the generic hooks makes the same unchanged instruction sequence run correctly. This build therefore uses MMIO callbacks and instruction observations, not generic memory hooks. The native JR/store regression guards the supported configuration. No guest opcode was patched or skipped.

MMIO callbacks still trace every emulated bus read/write with PC, width, value, physical address, dispatch time, RA and SP. Summary files include registers, recent PCs and stack words. Instruction-decoded flash-load/DRAM-store counters count attempted dispatches and cannot replace full memory access instrumentation or a stack unwind.

DRAM capacity is provisionally 128 MiB. The mapped boot flash aperture is four MiB; the complete eight MiB stock image is retained by storage. Remaining flash access through the guest SPI/ISP controller is unsupported. Full real hardware memory map validation is outstanding.

Candidate BDMA CRC and UART TX are explicitly optional models. Their bounded data operation and readiness transitions are causal, but their dispatch-time latency is provisional. Symbolic initialization propagates unknown register bits through specifically disassembled RMW paths; it stops when code consumes unknown status/input bits. It does not manufacture general zero reads or universal success.

Diagnostic stage-two execution host-copies the original second-stage bytes and selects the original entry. The optional reset prefix executes real early initialization before this transfer. MIU completion, BIST and the original first-stage relocation are bypassed in this mode. All reports retain the diagnostic flags. This is not complete firmware-from-reset boot.

See [investigation](por-timer-spi-investigation.md) for source changes, 46 stock ISR entries and exact boundaries. Timers use deterministic virtual pacing, not measured CPU cycles.
