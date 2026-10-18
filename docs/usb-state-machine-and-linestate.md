# USB host state machine and the `BF200E10` / `BF200F10` line-state read

Continues `4646d6a`. The strict boot is unchanged; all new behavior is opt-in and
labelled below. Nothing here is authentic displayed output.

## What the original firmware reads at the stop

`803BD910` is `lhu v1,(base+index*2)` inside the byte accessor **`803BD8F8`**
(`readByte(base, index)`; siblings at `803BD7DC` set-bits, `803BD838` clear-bits,
`803BD89C` write-byte). The caller tests the **low byte** with `andi 0x40`.

Two near-identical USB host state machines call it:

| Function | Port | USB controller base | State byte |
|---|---|---|---|
| `8054E3C0` | 1 | `BF200E00` | `80658018` |
| `80553ED0` | 2 | `BF200F00` | `80658019` |

State 5 means "waiting for a device": it reads USBC byte 8 (`BFxxxE10/F10`
low byte), tests **bit 6**, and

- **bit 6 set**: calls `803BDD40` (rewrites USBC RSTCTRL `0A`/`28`, UTMI CONFIG
  bit 2 and re-runs the UTMI calibration handshake), records the tick
  (`805CBBA8`) and enters state 6 (a 300-tick debounce check at `8054E5B4`);
- **bit 6 clear**: returns `0` immediately and stays in state 5.

So a legitimate no-device path exists and the polling call does **not** block
on it. The state is entered after the wake routine **`803BDC98`**, which writes
UTMI port-0 `+04=84 +05=90 +00=C3 +01=6D` (CONFIG `9084`, PWRCTRL `6DC3`).

## Register ownership (cross-SoC evidence, not Titania4-verified)

The Linux MStar `mstar_usbc.h` / `mstar_utmi.h` headers (linux-chenxing, from
msc313/ssc8336 parts) describe the same IP layout that the original boot
writes, and the original traces agree on offsets and values:

| Register | Original firmware evidence | Cross-SoC meaning |
|---|---|---|
| `BF200E00/F00` USBC `+00` | boot `0x0A`, `0x68`, then app `0x0A`, `0x28`; power-save writes `0x00` | RSTCTRL (`0x228` reset value on msc313; bit 3 suspend, bit 5 UHC XIU) |
| USBC `+04` | RMW with `|=4` after calibration | PRTCTRL (UHC/OTG select) |
| **USBC `+10`** | **read-only in the firmware** | **"utmi signal": bits 7:6 = D-/D+ line state, bit 0 VBUSVALID, bit 1 AVALID, bit 2 BVALID, bit 8 TXREADY** |
| `BF207500/7400` UTMI `+00` | boot `6BC3 -> 69C3 -> 0001`; app wake `6DC3`; power-save `EFC7` | PWRCTRL (bit 15 PDN) |
| UTMI `+04` | `2084`, wake `9084`, power-save `9080` | CONFIG |
| UTMI `+78` | start/end handshake already modeled (`--candidate-utmi`) | calibration start/end/data |

`USBC+0x10` is a read-only PHY status mirror: nothing in the original firmware
writes it, and no register-reset value is documented anywhere available.

## Hypotheses (all opt-in, off by default, traced, tested)

1. **`--exploratory-usb-no-device`** - a PHY the **guest itself powered**
   (UTMI PWRCTRL bit 15 = 0, as written by `803BDC98`) with no device attached
   sees SE0 on the host port, so **USBC+0x10 bits 7:6 read 0**. Grounding: USB
   2.0 host pull-downs give SE0 without a device; the register field is
   documented as D-/D+ in the cross-SoC header; the firmware's own no-device
   branch returns normally. Only bits 7:6 become known; every other USBC bit
   stays invalid, and a port whose PHY the guest has not powered stays invalid.
2. **`--diagnostic-usb-unpowered-dplus N`** - a *bracketing* diagnostic (run
   `0` and `1`), **not** a reset value, for a port whose PHY the guest put in
   power-down (`EFC7`). Line-state outputs of a powered-down transceiver are not
   grounded by anything available, so the strict hypothesis deliberately leaves
   this port unknown.

## Results (diagnostic cold runs, ~8 min each, not authentic boot)

See the table at the end of this file.

## Limits

- No UTMI/PHY, VBUS, interrupt or enumeration model exists; with a device
  attached the firmware would take the debounce/enumeration path, which is
  unmodeled and stops.
- The cross-SoC header is comparative evidence only; Titania4 reset behavior
  of `USBC+0x10` is unverified.

## Results table (diagnostic cold runs; not authentic boot; full suites NOT rerun for this commit)

| Run | Options added to the SPI-write diagnostic | MIPS dispatches | Platform polling `80488864` | UI wrapper `8047602C` | Dispatcher `804E0DC4` | Stop |
|---|---|---:|---|---|---|---|
| o0 | `--exploratory-usb-no-device` (port 0 only passes) | 855,843,282 | no | no | no | `803BD910` reads `BF200F10` (port 2, PHY powered down) |
| p0 | same, per-port gating | 855,843,286 | no | no | no | `80553F84` decision on the unpowered port's line state |
| q0 | + `--diagnostic-usb-unpowered-dplus 0` | 855,872,555 | **yes** | **yes** | no | `80526598`: GE rectangle `0x1F205180` unsupported mode |
| q1 | + `--diagnostic-usb-unpowered-dplus 1` | 856,200,322 | **yes** | **yes** | no | same GE stop (also runs the attach path `805540E0`) |

Platform polling returns for both bracket values; the next dependency is a GE
fill/clear request with a mode the guarded model rejects (not a hardware input).
