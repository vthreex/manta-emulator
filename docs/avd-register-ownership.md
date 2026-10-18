# AVD register ownership map (corrected) and the host-assist mailbox

Continues `e5427a4`, **correcting its conclusions**. The first version searched
only immediate-DPTR AVD accesses and missed one known host setter. Both are
fixed: [`tools/avd_register_ownership.py`](../tools/avd_register_ownership.py)
now also resolves the AVD's helper-call writes (`02AA`, `076E`, and the bare
`2C63/2C6B` tails) and tracks the HAL context base pointer per function in the
original MIPS image. It finds **all five** previously known host accessors
(`11C7`, `11C8`, `11CA`, `11DC`, `119C`; the last two via
`805032A0` and `80503644/50`). Report:
[`evidence/avd-register-ownership.json`](../evidence/avd-register-ownership.json).

## Address mapping

AVD XDATA `11xx` reaches the host at `BF206A00 + (xx>>1)*4 + (xx&1)`
(reproduces `11C7/11C8 -> BF206B8D/90` and `11CA -> BF206B94`). The MST786
datasheet's `AFEC Register (Bank = 1035)` is byte-indexed the same way. This
generation documents only a few AFEC bytes (`D6/D8/DA` are "Move to SW", i.e.
firmware-owned) and nothing for `DB`, `FC`, `3C/3D`; it is another chip, so it
is a layout clue, not a Titania4 reset value.

## Results (who touches each register)

| XDATA | Host alias | AVD program | Original MIPS | Related host drivers |
|---|---|---|---|---|
| `113C/D` | `BF206A78/79` | read only | none | none |
| `11C7/C8` | `BF206B8D/90` | **written** by the publisher (`076E`, from `0ABC`) | read `805030FC/F8` | `GetHTotal` |
| `11CA` | `BF206B94` | read + written twice (`02AA`) | read `80503098` | `GetCoChannelOn` |
| `11DA` | `BF206BB4` | **written** `2B1A` (averaged result, `0xFF` or computed) + read | none | none |
| `11DB` | `BF206BB5` | **read only** (`06FE`) | none | none |
| `11DC..DF` | `BF206BB8..BD` | mailbox, below | `805032A0..` | `SetRegFromDSP` |
| `11FA` | `BF206BF4` | written (`=1` at `027E..0282`) | none | none |
| `11FC` | `BF206BF8` | read `098D` and **written back** `099F` (`~captured`) | none | none |
| `119C` | `BF206B38` | read x4, written x4 | read `80503644`, write `80503650` | `SetHsyncSensitivity` |

So the previous "silicon-owned, nobody writes it" statement was wrong for
`11DA`, `11FA` and `11FC`: the AVD MCU writes them. For `11DB` the narrower
claim still holds in the forms searched: no immediate-address AVD write, no
original MIPS access, no init-table entry (bank `35`, index `DB`), no
Marlon/Amethyst host access. Six AVD helper calls use a computed `R7`
(`4DF`, `6AD`, `17CF`, `2C63`, `2C6B`, `2DE1`); the first, second, fifth and
sixth resolve to fixed registers (`11A2`, `11A2`, `115D`, `1100`), and
`2C63/2C6B` are shared tails whose callers were resolved individually. A
table-driven loop with a computed register number is not excluded. **What can
be said:** `11DB` has no identified software writer and could be reset-latched,
driven by hardware, or written through a path not enumerated; this study
cannot tell which.

## The host-assist mailbox (decoded)

`HAL_AVD_AFEC_SetRegFromDSP` (`8050328C`) is a generic register
read-modify-write that the MIPS performs for the AVD:

| Byte (`XDATA`/host) | Role |
|---|---|
| `11DC` / `BF206BB8` | command: bits `3:2` = `2` write, `1` read; bits `1:0` select the bank (`0` = `0x3600`, non-zero = `0x3500`); MIPS clears bits `3:2` when a write completes and sets them (`|0xC`) when a read completes |
| `11DD` / `BB9` | write mask |
| `11DE` / `BBC` | register index within the bank |
| `11DF` / `BBD` | write data; receives the read value |

Address formed is `bank + index`, converted to bytes as `2*addr - (addr&1)`
(so `0x35DB -> 0x6BB5`). Write: `new = (old & ~mask) | (mask & data)`.

The AVD program issues **one** such request in its static code (`12DA..130D`):
mask `FF`, register `0x3526`, value `0x30` or `0x10` chosen by a measured
range, command `0x09` (write, bank `0x35`). It is not a request for
`0x35DB`. The mailbox is the only mechanism by which the host writes AFEC
registers on the AVD's behalf, and nothing here makes it write `11DB`.

## Join soundness (supports the diagnostic experiment)

[`tools/mips_join_proof.py`](../tools/mips_join_proof.py) analyzes the original
instruction words for the HTotal comparison at `80501BFC`:

- Three paths (mismatch; match with counter `<4`; match with counter `>=4`)
  all rejoin at `80501758`.
- Only `v0`, `v1`, `a0` differ between paths, and all three are **dead at the
  join** (liveness over 431 reachable instructions of the same function).
- Memory differences are exactly two RAM cells: counter `806D83D4`
  (`0` / `counter+1` / unchanged) and accepted total `806AD960` (unchanged,
  or the matched value, equal to history `a1`).
- No MMIO store, call, trap or scheduler-visible effect on any path.

The branch outcome depends on the unknown sample only through mismatch vs
match; the counter sub-branch uses a known value. See
[htotal-branch-join.md](htotal-branch-join.md) for the guarded experiment.

## Remaining producer questions (unchanged)

Silicon behavior of `11FC` events and `113C/113D` with no sync input, and the
reset/producer of `11DB.7`, remain unestablished by any available source.

## Validation

Tool and evidence only; the scanner's five-accessor check is a regression
anchor. See the join note for the machine-level results.
