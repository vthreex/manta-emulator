# Native x86_64 port (side project): the firmware statically recompiled

Goal: run the original MIPS firmware as native code on x86_64 (or ARM64) instead of interpreting it, keeping the peripheral/OSD models, with the tuner paths kept but harmless.
Everything lives in `native/`. **No firmware bytes are committed**: the recompiler reads the user's own decompressed runtime image (`native/runtime_image.bin`, extracted as in `tools/firmware_extract.py`:
stub `flash[0x20080:0x23880]` + the inflated main image, base `0x803B0180`, entry `0x803B0600`) and writes C++ into `native/gen/` (git-ignored, derived from the user's image, not for redistribution).

## How it works
* `native/tools/mips_recomp.py` - static recompiler. Roots: entry, exception vector, every `jal` target, every aligned pointer into the code range found anywhere in the image (jump tables,
  callback tables) and every `lui/addiu|ori` address constant. 612,408 reachable instructions in 125,283 straight-line "runs" (a run ends after a branch/jump delay slot).
  Each run is one C++ function with a `case` label per instruction, so any address can be entered mid-run (returns, computed jumps) without leader analysis; branches inside the run loop inside the function.
  MIPS32r2 subset used by the image (incl. `ext/ins/seb/seh/wsbh/movn/movz/clz/mul/madd`, branch-likely, unaligned `lwl/lwr/swl/swr`, conditional traps). 17 reachable words are text/data and become traps.
* `native/runtime/` - dispatcher (`run_guest`), CP0 (Count/Compare timer interrupt between runs, exceptions, `eret`, `wait` fast-forward), memory (128 MB RAM, everything else via a `Bus`), `main.cpp` (a minimal lenient bus).
  Count advances 8 ticks per instruction (the same ratio as the interpreter-based emulator's `--exploratory-cpi 16`).
* Build: `cmake -S native -B native/build -G Ninja` (needs `python native/tools/mips_recomp.py native/runtime_image.bin native/gen` first; ~2.5 min to compile on 16 cores).
* Speed: 300 M guest instructions in 0.51 s (about 585 MIPS) on the development machine, versus ~2.4 M/s for the interpreter-based emulator.

## Verification
`native/tools/diff_runs.cpp` runs every recompiled run natively from random register and memory state (pointers into a scratch window, or fully random values) and records inputs/results; `native/tools/diff_check.py`
replays them in an **independent reference interpreter** written from the MIPS32 manual (shares no code with the recompiler): registers, HI/LO, next pc and every stored byte must agree.
Result: 124,768 runs (all but 515 that touch CP0/system state), 567,901 trials, **0 mismatches**. (An attempt to use Unicorn as the reference crashed inside Unicorn's own JIT setup for reasons not understood;
it is not a translation problem.) CP0/exception paths and MMIO are not covered by this test.

## State
Milestone 1 (CPU) works: the recompiled image runs from the runtime entry. With the stub bus it stops in a hardware wait loop (`pc 80547C90`, UART status). Next: attach the real peripheral models
(`manta::Riu`: GE, GOP, UART, timers, ...) as the `Bus`, with the tuner/decoder paths (AVD/VIF/AFEC) answering benignly, then the GOP preview window.

## Milestone 2: the native CPU inside the real machine (`manta_native_full`)
`native/runtime/native_runner.cpp` + `--native-cpu` (native build only; root target `manta_native_full`, built when `native/gen` exists): the machine object (peripherals, GE/GOP/OSD models, ATV signal, preview window,
virtual remote, snapshots) is unchanged, but its CPU is the recompiled firmware. It takes over from a saved state (`--load-state`): registers/CP0 are read from the interpreter engine, guest RAM is the machine's own
backing store, MMIO and the flash windows go through the machine's `mmioRead/mmioWrite`, MIU1 through the kseg2 alias, periodic services (coprocessor, IR remote, preview) run at run boundaries.
Tuner paths are kept but harmless: `--diagnostic-riu-zero-unknown-reads` makes never-defined registers read 0 instead of stopping the run (the AVD/VIF/AFEC models and the PAL signal still apply).
* Example: `manta_native_full.exe <the usual options> --load-state work/checkpoints/atv-840M.snap --native-cpu --diagnostic-riu-zero-unknown-reads --max-instructions 3000000000` runs the SCART state to 3.0B dispatches (2.16B of them native) in
  3.7 min (about 10 M guest instructions/s including all peripheral models; about 4x the interpreter; the CPU alone does ~585 M/s, the rest is the MMIO models); the real OSD ("SCART" source bar) is drawn by the original code through the GE/GOP models.
  `work/launch-WINDOW-native.cmd` opens the same state in the window.
* **Not carried over**: the interpreter's per-instruction hook (unknown-bit provenance, `preserved_control_write` events, loop skipping, coverage/traces). With provenance-dependent hypotheses the event streams diverge
  (first difference at event 350: the interpreter logs an extra `preserved_control_write` before a GOP selector write) and the native run later stops on registers the interpreter tolerates; use the lenient diagnostics.
  Also not equal yet: the SCART banner is not dismissed in the native run (guest stays in its idle loop), cause not found.
* Boot: the native CPU needs a snapshot (or the interpreter run up to the runtime entry); a standalone native boot would need the boot ROM/stub run natively or high-level-emulated.
