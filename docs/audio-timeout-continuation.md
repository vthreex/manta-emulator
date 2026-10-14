# Original audio failure-path investigation

This builds on `cf50f68`; the original firmware and all CPU engines remain
unchanged. DSP2 logical register `2D5A` maps to `BF225AB4`. Original caller
`804F79D8` loads type `60`, reads that byte through `804FB800/804F905C`, and
compares it with `E3`. The related MStar Marlon driver names the corresponding
operation `HAL_MAD2_GetLoadCodeAck`, with a different register address. It
supports the protocol interpretation, not register compatibility or a producer.

The actual producer of `E3` is still unknown. No DSP readiness, boot ROM or
DSP instruction execution is implemented by this change. The previously
verified addressed IDMA upload/readback is preserved.

## Explicit symbolic no-acknowledgment experiment

`--exploratory-audio-no-ack` constrains only the original comparison to
`ack != E3`. It **does not provide a valid reply byte**. The existing invalid
observation retains its exact byte/bit origins. This is a diagnostic path
hypothesis, not a validated physical absent-device or hardware reset model.
Strict mode continues to reject the unknown read.

The entire original reader and caller are SHA-checked. The predicate requires
the exact instruction `1452FFF7` at `804F7A28`, original return address, known
expected `E3`, an exact unknown source origin, and original retry range 1..100.
If the invalid backing placeholder happens to equal `E3`, the experiment stops;
it never changes that value, the guest instruction or the guest PC. Other
unknown decisions and transfers to new devices remain rejected.

The first recorded experiment executes **100 original reads/comparisons**,
then enters `804F7A60` with the original counter `65`, prints the genuine
`DSP2 Re-Active` UART message, sends `F3` through logical `2D38`, and reaches
the original return at `804F7A84` with return value one. The original caller
resumes at `804F7398`. Thus this particular failed initialization has a finite
software path; this does not prove that every later audio handshake can fail
without preventing application startup.

That experiment reaches **1,533,072,413 MIPS dispatches**, **216,698,985
OpenRISC instructions**, **468,999 total DVB-T MCU instructions** and **six
AVD instructions**, with **4,620 main timer** and **52 DVB-T MCU** ISR entries.
It stops at `804F9050 / BF225DD8`, logical sound-mode register `2DEC`.

## Preserving unknown sound-mode bits

The verified `804FC1CC` setter reads `2DEC`, masks to six bits, preserves bit
5 for ordinary mode changes, and writes those bits back to the same byte.
The `10`/`20` argument cases explicitly clear/set bit 5. The guarded reader
checks the complete original helper/wrapper and immediate caller. It permits
only provenance-preserving observation; no reset mode is assigned. The
existing store guard accepts unknown bits only at their identical original
byte and bit position. Any later unknown decision or foreign-register use
still stops.

The normal application contains a single direct ZUI initializer call at
`80469E38 -> 804E0D48`, after platform initialization. Audio wrapper
`8040DAC8` calls `804F7334` synchronously. No manual invocation is added.
GOP driver initialization, disabled windows and palette clearing remain
distinct from application framebuffer allocation and enabled scan-out.

## Decoder initializer and halfword reads

The continued original code uploads another **5,560 addressed words** to
the second IDMA port, with **5,572 word readbacks**. Its addressed-memory
SHA256 is `d045ae28082792eeeecbf335d4073ce5d578d4a628d855f08f4d6202960a87d4`.
Original UART identifies `PAL-SUM(0xB3)` and `T4 DEC system(0x50)` loads.
`804F7A8C` then checks `E3` through `804FC19C/804F9004`. This helper's
`110100` address adjustment maps logical `2D5C` to **BF225CB8**, not
BF225AB8. Its own 100-check loop, failure print `DSP1 Re-Active`, recovery
store to BF225C79 and return one are also observed. Neither acknowledgment
has a host producer. This is the same explicitly constrained experiment.

Both original failure returns are observed at **1,737,007,998 MIPS** and
**267,682,881 OpenRISC** instructions, with **5,273 main timer ISR entries**.
The next selector-preservation read is `804F90C4 / BF225A30`; its full
original helper changes bit0 only, returning the other unknown bits to
the same halfword. Its separately guarded data read is `804F90E8`.

The next original request is command `87`, logical `2D20 / BF225C40`.
MStar's [Marlon command enum](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/include/drvAUDIO_if.h)
names this `GET_STANDARD_CMD`, and command `40` `SET_STD`. The Manta's
own jump table and setters/getters corroborate this command-state use.
This does not import the other chip's register map. The Manta getter reads
a halfword but its original caller consumes only the low byte. Unknown
upper bits must survive the observation until that caller discards them.
Retained low bits may be copied only to the same command byte, and any
unknown decision still stops.

The additional decoder image supports ADSP-like vector decoding: candidate
reset word `18083F` jumps to `0083`. That entry again contains unsupported
`040040`, followed by calls to unuploaded `3FB6/3FC8/3FCB`. Candidate
`IO($20) = AR` at `0091`, after a candidate immediate eight, is a useful
producer/mapping hypothesis, **not an executed instruction or permission
to return eight**. No ordinary candidate E3 immediate or IO5C store was
found in this uploaded image. Its unknown instructions, ROM/external
targets and actual port mapping remain unresolved; zero audio DSP
instructions execute.

## Furthest verified continuation

The SIF trace records original MIPS writes of `4`, then `0`, to BF225C40.
The low command byte is known from those actual stores; only the unconsumed
upper byte is invalid. No candidate DSP IO instruction supplies it.

The final cold boot reaches **1,789,567,273 MIPS dispatches**,
**280,822,700 genuine OpenRISC instructions**, **468,999 total DVB-T MCU
instructions**, and **six AVD instructions**. Main timer ISR entries:
**5,441**; DVB-T MCU IRQ entries: **52**. Audio DSP instructions: **zero**.
Both original audio failure returns complete. Coverage proves the original
audio wrapper returns to `804868E8`, and the platform initializer returns
to application PC `80469B94`.

The new stop is **80404BF4 / BF001CD1**, byte read. Helper `80404BEC`
computes `((byte & 0F) == 8)`, called from `8046A190`. The true branch
checks `8049A050` and optionally restores source/channel state; both paths
join at `80469CB8`. The unknown read stops before its equality predicate.
No PM value or Boolean result is supplied to select a path.

The related Marlon and Milan SDK archives have `HAL_PM_GetBackGroundActive`
with the same instruction structure, but use **1CE4 / marker A**. This
supports a background-activity interpretation, **not** an exact Titania4
register definition. Manta has one observed direct reader of 1CD1, no
observed producer, and separate control-byte operations at 1CD0. Power-on
mode at 1CDD is a separate field; the existing AC-on experiment cannot
establish 1CD1. A candidate original PM51 program now contains matching static
writers, described below; its actual boot mapping, reset nibble and execution
order remain unknown.

Needed evidence is a matching Titania4 HAL/PM51 source or firmware producer,
or read-only captures of the PM register block across cold AC startup,
standby/wake and background activity. Correlate the 1CD1 low nibble with
PM51/mailbox stores and application boot logs; do not assume a different
chip's marker or an arbitrary zero. No physical TV was accessed.

ZUI at `804E0D48` has **not** executed. GOP initializer entries remain two,
palette commits 64, disabled-window programming calls six, and GE parameter
transfers 626. No active descriptor, application framebuffer, GE raster
operation, native guest pixels or PNG is demonstrated. Native window
startup is independent of those unavailable graphics milestones.

Strict POR and Winbond-CF uncertainties remain unchanged. The no-ACK
experiment proves original software failure paths under explicit constraints;
it does not validate absent-DSP electrical behavior, PM51 execution, or a
complete strict boot.

## Byte-addressed IDMA coefficient writes

Later original routine `804FC550` programs the IDMA address using separate
low/high-byte stores before its four data phases. The old model recognized
only halfword address stores, so these writes incorrectly reused a stale
cursor. Both byte phases now assemble the actual RIU latch, and an incomplete
address remains invalid. Replacing an address during a partial word is rejected.

The corrected cold trace contains 64 scalar coefficient writes to existing
decoder addresses, starting at `1300`. Decoder vectors `18083F` and `0A001F`
are preserved. Final decoder storage has 5,560 initialized words, 5,624 writes
and 5,572 reads; SHA256:
`ae80d6f58a52877f6cc0aa4d33d19a57e3ca175e1d2c5484e9fc2df69962f355`.
The earlier `d045...` hash records the image before coefficient updates.
The conditional `4000` address bit is covered by native port tests, but was
not observed in these cold-boot scalar writes. No DSP processor executes.

## Original PM51 candidate and remaining dependency

Flash `10000..20000` contains a coherent 65,536-byte 8051 candidate:
SHA256 `5f9d541120948f5067d162048c17f30fc704f871f9fdfbcc161181567f48fd3c`.
Its reset vector jumps to `60D8`; the first startup write is XDATA `1018=02`.
Later startup calls `9B17`, polling XDATA `0EC2 & 7 == 7`, and also polls
bit 3. The producer of those status bits remains unknown.

Static routine `934D` writes zero to logical `0E69` through helper `724E`
(MOVX at `729A`), then writes `FF` to `0E6F`. The corresponding MIPS RIU
addresses are `BF001CD1` and `BF001CDD`. Routines `8AAF`, `8984` and `90D8`
also manipulate this field through the same helpers. This identifies a
promising original-code producer, not an executed PM response or a valid
cold-reset default. SPI boot selection, the PM XDATA/page mapping and the
startup status producer must be established before relying on this code to
resolve the main CPU boundary. No PM51 instructions are counted as executed.

## Verification

The corrected build passes 803 native checks, 18 baseline runs, nine display
runs, four forensics tests, both CTest targets and native startup with only
Windows System32 on PATH. Full deterministic replay reproduces the original
CPU/MMIO trace, both failure paths and addressed IDMA memory. See the
[replay report](../evidence/audio-timeout-replay.json) and
[build/firmware provenance](../evidence/audio-timeout-provenance.json).
