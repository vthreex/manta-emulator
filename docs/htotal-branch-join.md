# HTotal comparison: guarded diagnostic branch join, AVD no-sync hypothesis, and what they show

Continues `759991f`. Everything here is **opt-in, diagnostic and labelled**; none
of it is enabled in strict or normal connected execution, and none of it is
authentic television boot progress. The strict production path is unchanged:
a fresh replay of the recorded 6,592,741-event trace (see Validation) matches
every summary field, the coverage file and the PM history.

## 1. Branch join at `80501BFC` (`--diagnostic-htotal-branch-join`)

The original comparison branches on the unknown HTotal sample (`a3`) against
known history (`a1`). The unknown value cannot choose the branch, so strict
mode stops there. [`mips_join_proof.py`](../tools/mips_join_proof.py) shows the
three possible paths rejoin at `80501758`, differ only in dead registers
(`v0`, `v1`, `a0`) and two RAM cells, and perform no MMIO/call/trap effects.

`src/board/htotal_join.hpp` does **not** set the PC to the join. It
executes the original instruction words fetched from guest memory along each
outcome separately in a private copy (a verified 15-word table must match
exactly or the experiment refuses), requires all live registers to agree,
and merges conservatively:

- registers that differ (all dead at the join) are reseeded **unknown** in both
  provenance trackers;
- a RAM byte is determinate only when every outcome leaves the same final
  value; otherwise it becomes unknown (stability counter `806D83D4`; accepted
  total `806AD960` unless the matched value equals its old value);
- `a3` keeps its unknown tag, so any later decision on it still stops.

The machine then restarts Unicorn at `80501758`. Skipped instructions are not
added to the dispatch count. Native tests (1,763 checks) cover all counter
boundaries (0, 1, 3, 4, 5, 255), several history values, every word mutation,
and unsupported-instruction refusal.

**Not applied to later branches.** The next unknown-dependent decisions
(co-channel, status words) gate hardware writes, which this join cannot cover.

## 2. What advanced (diagnostic cold runs, ~8 min each; none is replay-verified connected boot)

| Run | Options | MIPS dispatches | New original PCs | Next stop |
|---|---|---:|---:|---|
| baseline | none (strict) | 854,795,171 | 0 | `80501BFC` unknown HTotal |
| join | join | 854,795,227 | 56 | `80503138` reads `BF206A04` (`HAL_AVD_AFEC_GetNoiseMag`), no device model |
| + noise grant | + INVALID read grant for that one load | 854,795,306 | 135 | `80503098` reads `BF206B94` (co-channel; AVD had not published) |
| + AVD hypotheses, `11DB.7` = 0 and 1 | + no-sync events, invalid publication, bracket | 854,795,547 | 319 | `80503810` reads `BF206CBD` (`HAL_AVD_COMB_Set3dCombSpeed`) |
| + generated INVALID grants for the HAL accessors | `opaque-afec-hal-reads.tsv` | 854,795,553 | 325 | `80503828` transient `SH` writes `6CBD`'s unknown nibble into `6CBC` |
| + hash-bound paired restore for that accessor | `--exploratory-comb-paired-restore` generalized | **855,202,172** | **1,243** | `8053C9A8` reads `BF0011C8` (SPI/ISP register `1C8`) |

With the AVD hypotheses the connected AVD runs 2,495 (`11DB.7`=0) or 2,526
(`=1`) original instructions (was 863), publishes its startup values, and
reaches the `11CB.7` request-flag decision at `0AFA`. **The MIPS-side result is
identical for both `11DB.7` values**: same instruction count, same coverage,
same stop. At this boundary the unknown `11DB.7` does not change anything the
host observes.

The last row runs the platform-polling routine for **407,001 further original MIPS
instructions** (`855,202,172` dispatches) through the AFEC/COMB HAL accessors and
into a flash-writing routine (`8053C914`, a page-aligned SPI write with a `divu`
by the page size) that the polling path calls. It uses ISP register `BF0011C8`
(bit 2 cleared, then `AAAA` written to the ISP enable at `BF001000`), outside the
existing SPI model (`1000..10FF`, read transactions only). The next engineering
dependency is therefore **SPI NOR program/erase and the `1C8` register**, not
the AVD. Identical results for both `11DB.7` values again.

Not reached in any run: the UI wrapper `8047602C`, dispatcher `804E0DC4`, OSD 3
selection `804C37F0`, window creation `804AFDFC`, or any GE command. Platform
polling (`8046CA74`) has still not returned.

## 3. The AVD hypotheses (all labelled, off by default)

- `--exploratory-avd-no-sync-events`: with no HSYNC/VSYNC input the sync-derived
  event latch `11FC` bits 0 and 2 stay clear. These are the only bits the
  original AVD program ever uses (IRAM 29 bits 0 and 2); the rest are captured
  and acknowledged but never consumed. Acknowledgement writes are modeled
  **clear-only**: valid under write-0-to-clear or write-1-to-clear, and unable
  to create an event. Requires the absent-HSYNC hypothesis and the shared AVD run.
- `--diagnostic-avd-invalid-publication`: AVD stores of invalid or partly known
  data into the published block `11C0..11DF` keep their exact known mask (a copy
  of an unknown raw byte stays unknown; the HTotal copy rule already did this).
- `--diagnostic-avd-11db-bit7 N`: a **bracketing** diagnostic, not a reset
  value. Run both 0 and 1.
- The AVD abstract model accepts `INC A`/`DEC A`, `CPL A` and `AJMP` with a
  partially known accumulator, computing result bits exactly from all
  completions of the unknown bits.

## 4. The AVD dependency frontier (isolated probe)

[`tools/avd_frontier_explore.py`](../tools/avd_frontier_explore.py) fixes, one at a
time, the register that stops the unmodified AVD (synthetic values, never TV
input). With `11FC`=0 and clear-only acknowledgement the idle loop needs, in
order: `11CB.7` (request flag, set by nothing we can find), `11D8.3` (the
original `McuReset` clears it), then runs ~200,000 instructions to `112A` (an
amplitude measurement filtered by `(7*avg + x)/8` and compared with `0x19`
and `0x12`), then private RAM `806A`. Measurements and uninitialized RAM are
genuine boundaries for any no-signal model.

## 5. Why HTotal itself remains unknown

The AVD publishes `11C7/11C8` only inside the `11FC.0`-gated path. With no sync
events that path never runs, so the host would read the register's power-on
content, which no available source defines. The join does not create a value;
it only lets the original compare finish without choosing a branch.

## 6. Validation

- Native checks: 1,763 pass (was 1,619).
- Strict replay: 6,592,741 recorded events replayed against the new binary:
  all 289 summary fields, coverage and PM history identical.
- All 18 listed suites pass on the final binary (see
  [the session evidence](../evidence/htotal-join-validation.json)): firmware baseline,
  display, immutable input, clock profiles, AVD firmware/HTotal/request/co-channel/event/
  selector/symbolic-table, VD firmware, VIF/AGC, ZUI flow, GE clear / BitBlt / fill options,
  and the new diagnostic-options guard test; plus PM51, coprocessor and forensics tests.
  `tests/pm_power_boot_regression.py` fails an unrelated `pm51_state_sha256` assertion
  **identically on the original packaged `cc225d7` binary**, so it predates this work.
- None of the diagnostic runs is a replay-verified milestone, and none is
  claimed as connected progress.
