# COMB continuation and the graphics-engine boundary

The original firmware reaches **8052624C / BF205004**, after **656,427,765** MIPS instruction dispatches and **1,813** original timer ISR entries. This is explicitly exploratory execution. **No authentic frame, configured framebuffer, GOP initializer entry or OSD is demonstrated.** The native black window still represents an uninitialized display.

## What stopped the previous run

`805F883C` is a halfword store in `Write2ByteMask`, not a polling instruction. Its caller belongs to the original PQ table loader at `803C6418..803C6B7C`. The odd logical byte `10368F` maps through the original byte-address conversion to `BF206D1D`, in COMB bank 0. The table updates mask `CF`, preserving the other two bits (`30`, bits 4/5). Their analog meaning and physical reset values remain **unknown**. The chroma-activity interpretation is an unconfirmed hypothesis.

The original sequence saves the known low byte, samples the high byte, combines its preserved bits with table data, duplicates the resulting high byte into both halves, writes `BF206D1C`, and restores the saved low byte. Consequently, the original stop correctly rejected transferring unknown high-byte bits to different low-byte hardware bits. It was neither an exception storm nor an input-lock polling loop.

A [public PQ template](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/driver/pq/include/drvPQ_Template.h) identifies a T4/S7LD byte-write workaround covering COMB offsets 47/5F/8F/D7/DF. Its duplicate/restore sequence matches the stock disassembly. It does not define those preserved fields or establish the analog side effects of the temporary write. Searches of the supplied service-manual extracts and related AVD references found no matching MSD309 bit definition. Other SoC register banks were not treated as compatible.

`--exploratory-comb-paired-restore` permits only this byte-verified transaction. It assigns **no COMB reset value**. Unknown high bits retain their origins; temporary low bits become invalid, and all peripheral reads are blocked except the original restore RMW. Wrong bank, address, width, PC or restore value stops execution. The transaction closes only after the original low-byte store, twenty dispatches later. The assumption is that the intermediate unknown write has no separately observable analog effect during this interval. This is a guarded experiment, **not a validated hardware fix**. Without it, the previous COMB store guard remains intact.

## Genuine progress and corrected emulator bugs

After COMB restoration, the stock display initializer executes four calls to `803DF488`, with arguments 50, 51, 52 and 53. Each waits 300 guest timer ticks. These finite delays account for most additional dispatches; they are not evidence of new display features. The timer ISR and software ticker remain responsible for their completion.

The next PQ table saves the GOP selector at `BF203FFC`, compares its low nibble and writes `BF203E65` in banks 0 and 3. The two writes define the high byte's `C0` field; other bits remain unknown. Eleven successful GOP-aperture bus events are observed, including selector accesses. They are **table programming, not graphics-window initialization**. The [Titania4 register definitions](https://github.com/Benjamin-Dobell/kogan-tv-gpl/blob/master/Kernel_updated/mstar/mstar/gflip/drv/titania4/mhal_gflip_reg.h) corroborate the low-nibble selector and separate IRQ/ACK fields.

Two real provenance/addressing bugs were corrected:

- Restoring an unknown register snapshot after a known write now invalidates the restored unknown bits. Previously a known `FF` gain write or defined GOP bank could incorrectly make a later opaque restore usable.
- RMW lookahead and unknown-byte queries now use the documented MIU1 `40000000` alias. They no longer inspect unrelated MIU0 bytes for that alias.

The original GOP byte reader returns through a `JR` delay slot, then its caller masks with `ANDI 000F`. Unknown IRQ flags remain tracked until that mask discards them. Without an explicit initial bank, the original comparison at **803C1F70** stops after **654,548,913** dispatches. `--exploratory-gop-bank 0` defines only the selector nibble, never interrupt or write-ack flags.

The original application then returns from scaler/PQ setup and enters `MApi_GFX_Init` at `80520A40`. A new stop at `8053BA10 / BF203008` was identified as a hardware semaphore resource-ID read. The acquisition, release, reset and query routines match the named HAL bodies in the [Marlon reference archive](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libdrvSEM.a), with independent agreement in the [driver interface](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/include/drvSEM.h). Matching function bodies justify these operations; they do not justify borrowing that platform's reset ownership or device allocation.

`--candidate-semaphore` models resource ownership and contention rather than ordinary writable storage. A competing ID cannot overwrite an occupied slot; zero releases/resets it, and the original guest reads back the result. Initial owners remain unknown. `--exploratory-semaphore-idle` explicitly assumes no initial owners; PM51/AEON ownership has not been captured. The stock run performs four acquisitions and three releases. Without this assumption or a guest reset, an acquisition with unknown ownership stops.

The GE context reader next samples `BF205000` bit 0. Its exact original read/mask/return is tracked through cached RAM, with no enable value invented. The stock `GE_WriteReg` then calls `GE_WaitCmdQAvail` before the hardware write.

## Current blocker and the path to GOP

At **8052624C**, `LHU v0,5004(t0)` reads **BF205004**. The original routine masks bit 1 and branches on whether virtual command queues are enabled. This meaning is corroborated by the matching `GE_WaitCmdQAvail` body in the [GFX reference archive](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libapiGFX.a). No earlier guest write established this field. Its reset value and any producer before this call are missing. The run stops before the original branch consumes it; this is a rejected unknown read, not an observed FIFO polling stall.

If virtual queues are enabled, the function reads configuration at `BF2050A8`, queue capacity at `BF20501C` bits 11..15, and virtual-queue status. The disabled branch still needs real queue-space status at `BF20501C` bits 3..7. Original `GE_WaitIdle` compares that field with 16 and separately tests busy bit 0. Returning 16/idle without a supported reset and command-processing model would fabricate readiness, so no such response is supplied.

The verified control-flow dependency is:

```text
8040440C application display startup
  -> 803E0938 scaler/DLC/PQ setup
     -> COMB table duplicate/restore
     -> four original 300-tick delays
     -> 803C20D8 GOP-associated PQ table
  -> 80404440 application resumes
  -> 80520A40 original GFX initialization
     -> hardware semaphore/context ownership
     -> 8052693C GE context preservation
     -> 80526240 GE_WaitCmdQAvail [CURRENT STOP]
  -> callbacks / display-buffer arguments [NOT REACHED]
  -> 805333AC GOP API wrapper [NOT REACHED]
     -> call at 80533670 to 80539CE4 [NOT REACHED]
```

The remaining evidence needed is a matching T4 GE reset/register specification, or a trusted read-only cold-boot capture of GE control, queue status, clock gates and reset writes. A future capture should record these addresses at reset, before GFX initialization and after the stock initialization sequence, together with COMB bank0 register8F and GOP selector/ACK fields. This session did not access or modify a physical television. Pin connections would require board-specific verification before a capture. No local Shivaki dump was found; its related-board listing supplies no usable register reset capture.

## Reproduce and verify

Use your immutable local original dump. No firmware is included in the build.

```powershell
.\bin\manta.exe --firmware ORIGINAL.bin --headless `
 --symbolic-init --candidate-miu --candidate-bdma --candidate-uart --gpio-i2c `
 --defer-diagnostic-reads --virtual-rtc --cp0-timer --candidate-utmi --candidate-spi `
 --exploratory-spi-input pull-up --candidate-chip-id --exploratory-sc-bank 0 `
 --candidate-gamma --exploratory-gamma-enable 0 --exploratory-display-clock 148500000 `
 --exploratory-subwindow-enable 0 --exploratory-dlc-pending 0 --exploratory-dlc-timeout `
 --exploratory-comb-bank 0 --exploratory-comb-paired-restore --exploratory-gop-bank 0 `
 --candidate-semaphore --exploratory-semaphore-idle --max-instructions 1200000000 `
 --record-trace continuation.jsonl --summary continuation.json --coverage continuation.coverage
```

Exit 2 reports incomplete firmware execution. All hypotheses are visible in the summary. Strict candidate execution still stops at `80469898 / BF200A38`; the default strict path still stops at `BFC00C60 / BF001C74`. POR cache observations remain invalid. The undocumented Winbond `CF` response is not resolved by these changes.

The exact original signatures and complete COMB-containing function can be regenerated with `tools/comb_ge_analyze.py` (Capstone required). It hashes the original dump and verified main stream, using its true load address `803B3980`; it does not confuse the separate `3800`-byte prefix/vector base `803B0180` with that address.

Evidence: [summary](../evidence/comb-ge-summary.json), [selected bus checkpoints](../evidence/comb-ge-checkpoints.json), [full compressed trace](../evidence/comb-ge-full-trace.jsonl.gz), [PC coverage](../evidence/comb-ge-coverage.txt), [disassembly](../evidence/comb-ge-disassembly.json), [unknown bank decision guard](../evidence/comb-gop-bank-guard.json), [UART](../evidence/comb-ge-uart.log), [firmware regression](../evidence/regression.json), [display regression](../evidence/display-regression.json), [new full-path replay regression](../evidence/comb-ge-regression.json), [native checks](../evidence/native-tests.log), [CTest/window startup](../evidence/ctest.log).

Final acceptance remains unmet: GOP window setup, framebuffer allocation/drawing, actual rendering and remote-driven OSD interaction are not demonstrated. There are no new UART messages beyond the previously observed boot text and `sar input test...`. Virtual scan counters and gamma uploads are not rendered frames. The CPU engine and original firmware remain unchanged; no guest instruction or PC was patched.

## Checkpoint inventory

| Requested observation | Verified result |
|---|---|
| COMB bits 4/5 | Preserved fields in logical register `10368F`; exact physical meaning/reset unknown |
| Previous stop | Halfword store at `805F883C` would copy unknown high bits into different low bits |
| Continuation | Explicit quarantined paired-store experiment; no invented bit values |
| Furthest PC / dispatches | `8052624C` / 656,427,765 |
| Original timer ISR entries | 1,813 |
| New UART text | None; preserved original output is linked above |
| Display milestones | Scaler and DLC return; 768 gamma samples; stock COMB restore; GOP-related PQ table; GFX context/semaphore initialization |
| GOP initializer `80539CE4` | Zero entries |
| GOP bus activity | Eleven successful aperture events, including two PQ table writes; no window setup |
| Framebuffer allocation/modification | No authentic framebuffer demonstrated |
| Native guest pixels | None; window startup works independently |
| Current stop | `8052624C`, read `BF205004`; virtual-command-queue control bit/reset unknown |
| Strict uncertainty | POR `80469898 / BF200A38`; default strict path `BFC00C60 / BF001C74`; Winbond `CF` response unresolved |
| Exploratory uncertainty | All reproduction flags above remain explicit; COMB temporary effects, initial GOP bank and semaphore ownership are unverified |

The versioned source and evidence belong to the commit recorded in the [delivery manifest](../../Manta_LCD4214_build_manifest.json). The archive excludes the original firmware and private vendor reference archives.

Final Windows validation against the same delivery executable passed: 228 native checks, CTest 2/2 (including native window startup), four Python forensics tests, eighteen baseline firmware regression runs, nine display regression runs, and full replay of the 656,427,765-dispatch continuation. The [provenance record](../evidence/comb-ge-provenance.json) records source/executable/engine/firmware hashes. These passes demonstrate the stated checkpoints and guards, not completion of TV boot or validation of exploratory assumptions. Linux and sanitizer builds were not verified.
