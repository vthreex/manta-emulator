# VIF `0x1F224471` (carrier-recovery lock status) and the `80509A94` dependency

Continues `5581ad1`. Everything marked *diagnostic* is opt-in, off by default and
not Titania4-verified hardware behaviour.

## Identity (evidence, with the revision mismatch stated)

| Original MIPS | Byte address | VIF bank offset | Reference `MsVIF.h` (later chip, bank `0x1010`) |
|---|---|---|---|
| getter `80505E08` and `80509AE8` | `0x1F224470` | `0x38` | `CR_FOE` (`0x101038`) |
| getter `80505E34`, tests at `80509898`, `80509968`, `80509A94`, `80509DC0`, `8050A140`, `8050A170` | `0x1F224471` | `0x39` | `CR_LOCK_STATUS` (`0x101039`, bit 0 tested) |
| branch target `80509E58` (clears bit 5, then sets it) | `0x1F224440` | `0x20` | `CR_LF_FF_RSTZ` (`0x101020`, "reset AFC integral part") |
| lock-set path `80509AA4` (sets bit 0) | `0x1F224740` | `0xA0` of the next bank | `AAGC_ENABLE` (`0x102BA0`, bit 0) |

Byte address = `0x1F224400 + (offset>>1)*4 + (offset&1)` (same mapping as the AFEC bank
in `avd-register-ownership.md`). Three independent offsets (`0x38`, `0x39`, `0x20`) agree
between the original code and the reference `msVifHandler` `VIF_AFC_STATUS` case
(`if (CR_LOCK_STATUS bit0) {AGC_VGA_THR..., AAGC_ENABLE=1, step=AFC_STATUS2} else {CR_LF_FF_RSTZ bit5 0->1}`).
The bank number differs (T4 `0x1122`, reference `0x1010`); only offsets and control flow are compared.

**Reading:** `0x1F224471` bit 0 is the VIF carrier-recovery lock indication and
`0x1F224470` is the carrier frequency-offset estimate that the next state
(`VIF_AFC_STATUS2`) reads. They are named by correlation only.

## Writers (searched, none)

`tools/riu_const_xref.py` folds `lui/ori/addiu/addu` constants and the RIU base pointer
(`lw rX,-0x2684(..)`) over the whole original MIPS image (stepping through data words):
the offsets `0x24470/0x24471` appear only in **loads** (nine sites above). No store, no
halfword/word store spanning them, no init-table entry in any form searched
(`u32 1F/BF224471`, `u32 0x24471`, `u16 7122/2271` are coincidences inside MIPS/8051 code or
MD5 constants). Indirect or computed addressing is not excluded by a static scan.
Runtime corroboration: in 856 M dispatches the emulator never recorded a guest write, and
the register stayed an unknown MMIO read. The register is therefore a hardware-produced status
(needs an IF carrier from the tuner), not software state. Whether it is defined with no
analog input connected is **unknown**.

## Branch effects (both executed from original instructions)

* lock = 0: `80509E58` clears and sets `CR_LF_FF_RSTZ` bit 5 (resets the AFC integrator), stays in
  state `VIF_AFC_STATUS` and returns to the caller.
* lock = 1: sets `AAGC_ENABLE` bit 0 and programs the AGC thresholds, advances to `VIF_AFC_STATUS2`
  (which will read `CR_FOE` on its next call).

## Diagnostic: `--diagnostic-vif-cr-lock N` (bracket 0 and 1)

Defines only bit 0 of `0x1F224471` (requires guarded exploration), rejects guest writes to it,
records `diagnostic_vif_cr_lock` events and the assumed value in the summary. Unit-tested
(11 native checks) and guarded in `tests/diagnostic_options_regression.py`.

Cold diagnostic runs (q0 flags + `--diagnostic-vif-cr-lock N`, ~7.5 min each):

| lock | dispatches | covered branch | next stop |
|---|---:|---|---|
| 0 | 856,177,968 | `80509E58..60` | `80501744` |
| 1 | 856,177,969 | `80509AA4..AD8` | `80501744` |

Both pass `80509A94`, return to the application, and differ by one dispatch and by which VIF
side effects were executed (kept distinct; not joined). Next stop: see below.

## Next dependency: `80501744`

The application's second visit to the AVD monitor `805016C8` compares two history halfwords
(`806E83CC`..) that were stored from the unknown HTotal sample. The first join degraded the
stability counter to unknown, so the join of `80501BFC` cannot be re-applied soundly (it
refuses when its counter cells are already unknown). The remaining honest options are a
bracketed HTotal input hypothesis or a symbolic counter; neither is a TV measurement.
