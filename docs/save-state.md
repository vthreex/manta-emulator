# Full-machine save state (schema 1)

A save state captures the **entire emulated machine** so that a run can be resumed in a fresh process and continue
*exactly* as an uninterrupted run would. It is not a fast boot: the firmware is never skipped or patched, the
snapshot is produced by the real boot and only restores what that boot produced.

```
manta … --save-state FILE [--save-state-at DISPATCH] [--save-state-stop] [--save-state-max-defer N]
manta … --load-state FILE [--load-state-allow-extra-options] [--state-digest]
```

## File format

| Field | Content |
| --- | --- |
| magic | `MANTASNP` (8 bytes) |
| version | u32 little endian, currently **1**; any other value is rejected |
| count | u32 section count (≤ 64) |
| per section | 16-byte zero-padded name, u64 length, 64 hex chars SHA-256 of the payload, payload |
| trailer | 64 hex chars SHA-256 of everything before it |

Sections: `meta`, `mips`, `dram`, `flash`, `riu`, `machine`. The file is written to `FILE.tmp` and renamed, so a failed save
(disk full, target is a directory, …) never replaces an existing valid snapshot. A loader verifies, **before applying
anything**: overall checksum (truncation / corruption), magic, schema version, every section checksum, no trailing data,
firmware SHA-256, run-option fingerprint, CPI and RAM size. Any failure aborts the process with a message and leaves the
machine untouched.

Measured sizes of the interactive checkpoints (bytes): NO SIGNAL `meta 3,534 · mips 11,208 · dram 17,201,220 · flash 8,214 ·
riu 707,878 · machine 9,498,032` = 27,430,694 total; MENU checkpoint 28,608,770 total.

## What is captured

* **MIPS**: Unicorn context blob (`uc_context_save`, public API only), PC to resume, dispatch and cycle counters, CP0 MMU
  register shadow, and the ordered log of TLB installs. Unicorn cannot read the TLB back, so the loader **replays** the
  logged `TLBWI` installs by executing synthetic MIPS code (`replayingTlb_` turns every board side effect off while it
  runs) and then restores the context.
* **DRAM**: non-zero 4 KB pages, de-duplicated by content, written once. RAM has no per-byte provenance in the machine;
  unknown-bit provenance lives in the register file / device state below and is serialised there.
* **Flash**: the copy-on-write overlay of the SPI NOR write model (the stock image is never written) plus WEL / protection
  state; the CPU-visible flash windows are refreshed after restore.
* **Riu and devices** (`riu`): every register byte with its known-mask and bit identity (`PreservedBits`,
  `UnknownObservations`, per-byte controls), GE queue state and registers, GOP windows / latches / palette, scaler banks,
  VIF/AFEC/AVD state, PM IR FIFO (queued keys, pop counters), SAR ADC, UART, I²C, SPI, BDMA, IDMA, TSP and OpenRISC state, the
  hypothesis tables (`--exploratory-mmio-bits` counters, …).
* **MCUs** (`machine`): VD, AVD and PM51 8051 cores (registers, IRAM/XRAM, timers, `VdByte` provenance). On restore the
  program storage and callbacks are **re-bound** (`rebind*`) and only then is the dynamic state overlaid.
* **Machine** (`machine`): virtual time, CP0 timer bookkeeping, event/poll counters, recent-MMIO window, history, remote-control
  queue and log, pending IR deliveries, GE/GOP counters.

Not serialised: host handles, `std::function` members, the preview window, trace/replay streams. A run that is recording
or replaying a trace never saves.

## Safe boundary

A snapshot is written at the first instruction at/after `--save-state-at` for which **all** of these hold, otherwise the
save is deferred (up to `--save-state-max-defer`, then the run stops with a clear reason):

1. the GE queue is idle (queued raster closures cannot be serialised; the queue is *never* forced to drain);
2. the previous instruction is not a branch/jump (the resume PC could not express a delay slot);
3. no CP0 timer interrupt is pending (Cause.TI/IP7 lives partly in the CPU object);
4. no TLB-replay/`TLBWR` hazard, not stopped, not in prefix preparation.

The snapshot is taken at the start of the code hook, before the instruction is dispatched or counted, and the resumed
run re-enters at that PC with the remaining dispatch budget.

## Compatibility rules

A snapshot loads only under the **same hardware-affecting options** (fingerprint of the canonical option lines; output,
observation and run-length options are excluded). `--load-state-allow-extra-options` additionally accepts a load when the
snapshot's option lines are a sub-multiset of the current ones; the extras are listed in the summary as
`snapshot_option_delta`. This exists so a labelled hypothesis can be added *after* a checkpoint (for example a bracket of
a PM status bit) without a 15-minute cold boot; process-configured hypotheses are kept across the device restore. Snapshots
written before the option lines were stored cannot be loaded this way and must be re-saved (load without the flag,
`--save-state` again).

## Verification

* `tests/snapshot_regression.py`: uninterrupted vs. restored run on the connected configuration — every summary field and
  every per-section state digest (`--state-digest`: CPU context, RAM, flash overlay, each device, counters) equal; truncated,
  bit-flipped, wrong-schema, different-configuration and garbage files rejected; failed save leaves other snapshots intact;
  superset load reports its delta.
* `tools/snapshot_equivalence.py` (used by the test, also standalone): exact equivalence demonstrated at 100M→160M and at
  700M→780M dispatches; a resumed MENU press produces snapshot sections byte-identical to the cold boot's.
* `core_tests` `snapshotArchiveTests` for the archive primitives.

## Measured

| | cold boot | from snapshot |
| --- | --- | --- |
| connected NO SIGNAL state (≈ 1.30 B dispatches) | ≈ 15 min CPU | **≈ 1.1 s** process start + load |
| write (load + save + stop) | | ≈ 0.6 s on top of the load |

Checkpoints: `work/checkpoints/no-signal-ready-v2.snap`, `menu-ready.snap` (local artefacts, not committed — regenerate with
`configs/checkpoints/connected-menu-base.json` and `--save-state`). Experiments from a checkpoint:
`tools/checkpoint_sweep.py` (one raw IR byte per short run).

## Limits

* Resumed runs reproduce the state of the *emulation*; they make no claim about real hardware beyond what the cold boot
  already established. Exploratory hypotheses remain labelled in the summary of every resumed run.
* GE closures in flight and trace/replay streams are not part of a snapshot (see *Safe boundary*).
* The TSP/OpenRISC reference fixtures are still stale and were not regenerated by this work.
