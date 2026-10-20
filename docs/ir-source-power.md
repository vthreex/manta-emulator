# SOURCE and POWER keys — what is and is not established

All results come from the original firmware reading raw bytes through the emulated PM IR FIFO (no host call into the UI),
started from the NO SIGNAL checkpoint (`docs/save-state.md`) with `tools/checkpoint_sweep.py` or a single `--ir-script`.
Nothing here is a measurement of a real remote; names are behavioural.

## SOURCE

* **No raw byte of the 74 decoded stubs cycles through the sources.** A sweep of every decoded raw byte from the checkpoint
  (watching `8048710C`, `8048848C`, `804C3684`, `8046A758`) found no key that advances a source cycle.
* **Direct source-select keys exist.** The key table maps app codes `0x5C..0x63` to the source-switch function `8048710C`
  with an explicit source id in `a0`:

  | raw | app | source id |
  | --- | --- | --- |
  | `0xF9` | 0x5C | 0 |
  | `0x4B` | 0x5D | 1 |
  | `0xF6` | 0x5E | (not observed) |
  | `0xEF` | 0x5F | 3 |
  | `0xF5` | 0x60 | 4 |
  | `0xF7` | 0x61 | 5 |
  | `0x4E` | 0x62 | 7 |
  | `0xF3` | 0x63 | 9 |

  Which id is DTV / ATV / AV / SCART / YPbPr / HDMI is **not** established; the source-mask table at `0x8061A250` flags
  id 28 (DTV) as the one that enables ZUI key forwarding. These raws are therefore *not* added to
  `configs/input/ir-keymap.tsv` under source names.
* Raw `0x16` is an **info / source-banner toggle** (three presses return to the start state); it remains the INFO candidate.

## POWER (raw `0x0A`)

* Raw `0x0A` → app `8` → page-state handler `804A4368` → routine `8040468C`. The routine copies a 4 KB block from `0x80610000`
  (RAM-resident code/data), tests **bit 6 of PM byte `0x1F001CD0`** through the helper `804F1304`
  (`lbu / andi 0xFF / and a0 / jr / sltu`, only the bits in `a0` reach the result), calls `80547AA8(2)` and the PM
  register helpers, which is consistent with a standby entry path (unconfirmed: the path has not run to completion).
  It is **not proven** that raw `0x0A` is the physical POWER key; it is the only decoded key observed to reach this routine.
* `0x1F001CD0` is not read-only: the setter at `804F1320` read-modify-writes bits 5:4 and carries bit 6 back unchanged.
  Its meaning (bit 6 = a power/wake state bit) is **unknown**; the hardware value is not supplied by default.
* Bracket (opt-in, labelled hypothesis `--exploratory-mmio-bits 1f001cd0:40:00` and `:40:40`, loaded onto the checkpoint with
  `--load-state-allow-extra-options`): **both outcomes pass the bit test identically** and run about 150 further
  dispatches, then stop at the same place — the PM register read–modify–write helper (`8054730C`, ra `805478D0`) reads
  **`0x1F002030`**, an unmodeled PM-block register (previous accesses of `0x1F001C80` in the same helper pass). So bit 6 of
  `0x1F001CD0` does not decide the first part of the path; the next missing hardware model is the PM register block
  around `0x1F002030`.
* **Not reached**: standby display shut-off, GOP window disable, wake-up. No claim about standby/wake behaviour is made.

## Next steps (evidence-backed)

1. Bracket the discarded first load in the PM RMW helper (`80547324`, result overwritten at `80547334`) as was done for the
   ADC helper, then the second load/RMW at `80547338` for `0x1F002030`.
2. Identify the register bank of `0x1F002030` (PM sleep / GPIO block) from the original code before any value is proposed.
3. Only after the standby path completes: observe whether the firmware disables GOP windows and what, if anything, re-enables
   them (wake needs the PM51 side).
