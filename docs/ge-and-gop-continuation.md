# Original GE and GOP initialization reached; no OSD frame yet

This continuation preserves the existing emulator, original MIPS CPU engine and immutable SPI dump. It reaches **two normal calls to original GOP initializer `80539CE4`**, after completing original GE initialization. It does **not** demonstrate a framebuffer, scan-out, menu or authentic pixels. All prior exploratory assumptions remain necessary; strict boot is unchanged.

## Observed execution

| Observation | Cold-run evidence |
|---|---|
| Furthest instruction | `805D7B38`, `LHU v0,00FC(v1)` |
| Dispatches / original timer ISR entries | **666,140,752 / 1,844** |
| Guest inflate | Original main stream SHA `149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236` |
| GE parameter submissions / transfers / retirements | **626 / 626 / 626**; no raster command submitted |
| GOP initializer calls | **2**, ticks 656,564,909 and 656,569,475; original caller return address `80533678` |
| Successful GOP aperture reads/writes | **8,145**, including selector, configuration and palette accesses |
| Palette entries actually committed | **64**, bank 3, indices 0..63, original packed values all zero, transfer mode `2000` |
| New original UART text | `First GOP driver instance, flush GWIN HW` |
| Scaler/DLC | Both initializers returned; original 1920×1080 timing and 768 gamma samples preserved |
| Actual graphics window/framebuffer/pixels | **None demonstrated**; window enable bits remain incompletely known; no valid descriptor or GE raster command observed |
| Native display | Window startup works; blank host window is not a guest frame |
| Remaining access | Physical `1F202AFC`, guest `BF202AFC`, two-byte read, queue-count bits 0..4 unknown |

Evidence: [cold summary](../evidence/ge-gop-summary.json), [graphics and bus checkpoints](../evidence/ge-gop-checkpoints.json), [UART](../evidence/ge-gop-uart.log), [full compressed bus trace](../evidence/ge-gop-full-trace.jsonl.gz), [PC coverage](../evidence/ge-gop-coverage.txt), [original disassembly](../evidence/ge-gop-disassembly.json), [full replay regression](../evidence/ge-gop-regression.json), [provenance](../evidence/ge-gop-provenance.json).

The readable UART log normalizes line endings; [the original UART bytes](../evidence/ge-gop-uart.raw) are retained separately without changing guest output.

## GE queue implementation and limits

The [Titania4 GFLIP register definitions](https://github.com/Benjamin-Dobell/kogan-tv-gpl/blob/master/Kernel_updated/mstar/mstar/gflip/drv/titania4/mhal_gflip_reg.h) identify GE control register 1 bit 1 as virtual-command-queue enable, status register 7 bits 11..15 and 3..7 as queue-space fields, busy bit 0, and tag register 32h. The [matching Titania4 tag-completion reader](https://github.com/Benjamin-Dobell/kogan-tv-gpl/blob/master/Kernel_updated/mstar/mstar/gflip/drv/titania4/mhal_gflip.c) compares an empty count with 16. Original guest `GE_WaitCmdQAvail` extracts bits 11..15 even in the non-virtual-queue branch; `GE_WaitIdle` checks bits 3..7 and busy separately. The named [MStar GFX archive](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libapiGFX.a) corroborates these function structures, without proving another chip's reset values.

`--candidate-ge --exploratory-ge-reset` selects an explicit experiment: drawing enable initially clear, memory-backed virtual queue disabled, both 16-entry stages empty, and a running GE clock. A bounded queue now derives space/busy state from original parameter submissions, moves them through deterministic stages, and retires tags in order. The provisional latencies of 8/16 dispatch ticks, two-stage topology, initial state and clock are hypotheses, not measured silicon timing. Status bits outside mask `F8F9` stay unknown. Parameter registers retain the existing storage behavior; a complete raster-state latch is not claimed. Memory-backed virtual queues remain unsupported. Register 60h drawing commands fail before acceptance or completion, so an unimplemented blit cannot produce fake success or pixels.

Without the explicit GE reset experiment, the old queue-enable boundary remains. No CPU exception is skipped and no original instruction or PC is patched.

## Original GOP control and palette

The original HAL selector/data routines retain unspecified IRQ/ACK fields while changing the bank nibble. Their complete byte-verified instruction sequences now carry invalid bit provenance through registers and RAM. Only the known bank nibble can affect comparisons; an unknown decision, address or cross-register bit copy still stops. The original variable-shift MIU selector and original masked scaler writes are handled by the same provenance guard.

`--candidate-gop-palette` consumes original data register 3/4 writes and full register-5 write pulses, using the guest index and packed data. Mode `2000` observed here remains separate from mode `3000`; no ARGB format conversion is assumed. Entries start undefined, not black. The 64 zero entries are genuine original startup clears, not an image. Palette pulses neither enable a GWIN nor invent a framebuffer.

Original application startup supplies GOP API initialization arguments including values `00DDD800`, `0072F400` and `0BE08000` at `804044B4..804044DC`. Their presence is not proof of a live framebuffer: a corresponding enabled window, validated MIU address/pitch/format and guest drawing have not been observed. GOP IRQ/ACK latching, scan-out, palette pixel interpretation and GE rasterization remain incomplete.

## Subsequent TSP boundaries

The guest next enters TSP startup through normal control flow. Two rejected configuration reads were investigated and transparently preserved:

* `805D7A14 / BF202BFC`: original strided interrupt-mask RMW; high pre-reads and unchanged halfword fields remain invalid.
* `805D6BD8 / BF202BFC`: original filter-route configuration in `805D6B68..805D6D1C`; complete 109-word function checked before permitting opaque observations. Only same-origin bit restoration is allowed.

These changes do not implement TSP interrupts or assign reset/status values. Native tests explicitly reject copying unknown IRQ fields into command status and reject branching on unknown command count.

The current routine is:

```text
805D7B30  LUI v0,806E
805D7B34  LW v1,6E84(v0)       ; guest-installed TSP base BF202A00
805D7B38  LHU v0,00FC(v1)     ; BF202AFC
805D7B3C  LHU v1,00F8(v1)     ; paired BF202AF8 read, not reached
805D7B40  JR ra
805D7B44  ANDI v0,v0,001F     ; low five count bits returned

805D5E48  JAL 805D7B30
805D5E4C  NOP
805D5E50  BNE v0,zero,805D5E48
805D5E54  NOP
```

The [named MStar DMX/TSP archive](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libapiDMX.a) contains matching `HAL_TSP_CmdQ_CmdCount` operations, with only the relocatable global address differing. Its `CmdQ_EmptyCount` subtracts this field from 16. This is command-queue occupancy, not an input-signal or interrupt-pending poll. The preceding original `805D76BC` matches `HAL_TSP_Ind_Enable`, setting bit 1 of the engine control; it is not evidence that queue count resets to zero. The firmware currently stops at the first unknown read, before actually looping on a supplied value.

Resolving this requires the T4 command-queue producer/dequeue/reset/clock semantics, or a read-only cold-boot trace recording `BF202AF8`, `BF202AFC`, `BF202BE8`, `BF202BEC`, relevant clock/reset writes and indirect accesses. A later TSP alive/auxiliary-firmware handshake also exists statically, but has not been passed. A reference T4 `fwTSP.dat` and the dump's fourth DEFLATE stream are not interchangeable and have not been substituted. No physical television was accessed.

## Reproduction and strict behavior

Append these options to the full [previous COMB continuation command](comb-and-ge-continuation.md#reproduce-and-verify):

```powershell
--candidate-ge --exploratory-ge-reset --candidate-gop-palette
```

The output summary records every selected experiment. Exit 2 remains incomplete emulation. Strict candidate mode still stops at `80469898 / BF200A38`; the unconfigured strict path still stops at `BFC00C60 / BF001C74`. POR samples remain invalid and the undocumented Winbond `CF` response is unresolved. The exploratory GOP milestone does not validate these assumptions.

Regressions require the original local dump; they fail if original reset/inflate, timer handlers, GOP entries or recorded guest palette pulses disappear. `tests/ge_gop_regression.py` reruns the entire original boot and compares the full trace and complete summary, rather than playing back output.

Final Windows validation against the delivery executable passed: [300 native checks](../evidence/ge-native-tests.log), [CTest 2/2](../evidence/ge-ctest.log), [native window startup](../evidence/ge-native-window-smoke.log), [four forensics tests / 400 bounded random inputs](../evidence/ge-python-tests.log), [18 baseline firmware regressions](../evidence/ge-baseline-regression.json), [nine display regressions](../evidence/ge-display-regression.json), and [complete 666,140,752-dispatch replay](../evidence/ge-gop-regression.json). Original dump and CPU-engine hashes are unchanged. These passes demonstrate original execution and the stated guards, not validation of experimental hardware behavior. Linux and sanitizer runs are not verified.

The remaining path to OSD includes TSP startup, any further application prerequisites, real framebuffer/window configuration, original drawing operations and native scan-out. There is no defensible percentage or ETA until those operations execute.
