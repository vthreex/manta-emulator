# MCU stack provenance and original VIF return paths

This continues the existing emulator from `c28b061`. The original firmware and
all CPU interpreters remain intact. No authentic framebuffer or OSD is claimed.

## Stack correction

The later DVB-T reset/interrupt sequence previously stopped at original
`4A8A: PUSH ACC` when an interrupted peripheral read left accumulator bits
invalid. Saving that value does not consume it as a decision or address.

The wrapper now carries bit masks and origins through all 256 indirect IRAM
bytes, stack saves/restores and direct lower-IRAM aliases. Direct addresses
`80..FF` still address SFRs; they never alias the upper indirect RAM. Unknown
data cannot become DPTR, stack-pointer or PSW control bits through POP. A saved
PSW may retain unknown parity while its other seven bits remain defined;
architectural parity continues to follow ACC in the existing interpreter.

Failed opcode callbacks roll back tentative metadata as well as failed boundary
validation. In particular, an unsupported PUSH source read cannot mark an
untouched stack byte valid, including across a later warm reset.

LCALL/ACALL and real interrupt entry mark the actual return-address bytes
written by the existing core. RET/RETI reject invalid return bytes. Reset
preserves invalid RAM provenance when the interpreter preserves the underlying
RAM. The pre-existing conditional standard-8052 reset/RAM experiment is not
promoted into a verified Titania4 reset specification.

Native regressions exercise low, middle and upper IRAM stack bases, partial
masks, unknown control-register restores, retained RAM across reset, and actual
INT0 entry/RETI around the observed ISR save prefix. Saving an unknown value is
allowed; its later decision or address use remains guarded.

## VIF control-flow experiment

`vif_agc_probe` reads a hash-verified original decompressed MIPS image and
executes the complete original handler at `8050962C` using the existing MIPS
engine. This is an isolated diagnostic with explicitly synthetic measurements,
configuration, stack and caller; it supplies nothing to the TV machine.

The probe covers all 256 mean16 bytes, PGA values 0 and 31, VGA equal to or one
above a synthetic configured minimum, and both initial acquisition states 0/1.
The negative-modulation predicate is exactly `20 <= mean16 < 140`, or PGA 31,
or VGA equal to the configured minimum. All 2,048 original calls return through
their own saved RA and restored stack in at most 90 instructions. There are
1,776 acquisition advances and 272 returns retaining the prior state. Only the
advancing cases issue the three original line-count/carrier-control writes.

This proves finite original handler paths under those inputs. It does not
establish a physical AGC measurement, lock state, or an end-to-end boot timeout.
The TV machine must still stop when it needs an unmodeled measurement.

The related [MStar VIF source](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/device/if_demodulator/MsVIF.c)
programs PGA gain overrides with pulses and then releases them. That sequence
does not establish later live `AGC_MEAN16`, `AGC_PGA2C` or `AGC_VGA` observations.
The absolute reference bank differs from this firmware. No absent-signal gain,
measurement or receiver-ready constant is inferred from it.

The probe can be repeated with:

```powershell
python tests/vif_agc_regression.py --firmware ORIGINAL.bin --probe BUILD/vif_agc_probe.exe --report vif-agc.json
```

## Full-machine execution

The corrected wrapper executes 1,454,515 original DVB-T instructions and 161
interrupt entries, compared with 486,469 instructions and 57 interrupts in the
previous same-mode trace. Its final reset instance executes 475,509 instructions.
It passes `4A8A` naturally and stops at `4AE0`, attempting the unsupported
XDATA `2200` write. Shared mailbox writes increase from 14 to 30 through original
MCU execution; the unsupported transaction still supplies no completion.

| Observation | Result in the new exploratory cold execution |
|---|---|
| Last MIPS PC / dispatches | `80509D54` / 854,795,034 |
| Main interrupt entries | 38,589 |
| Original TSP OpenRISC instructions | 2,453,934,820 |
| Original DVB-T instructions / interrupts | 1,454,515 / 161 |
| Original PM51 instructions / interrupts | 3,474 / 0 |
| Original AVD instructions | 6 |
| Audio DSP instructions | 0 |
| PM51 stop | `574F`, XDATA `0E6A` bit-7-clear write; effect still unsupported |
| AVD stop | `0802`, XDATA `C008` write `55` |
| MIPS measurement origin | `80509D34 / BF224644`, invalid mean16; first arithmetic use at `80509D54` |
| Other VIF observations | `80509D38 / BF22465D` low 5 bits; `80509D3C / BF224650` halfword remain invalid |
| ZUI | `80469E38`, `804E0D48` and initializer return `804E0DBC` covered; state dispatcher `804E0DC4` unentered |
| OSD manager / font rendering | No demonstrated event-loop or drawing entry |
| GOP | Initialized twice; no enabled active descriptor |
| Framebuffer | No proven allocation; disabled 8x8 descriptor is still not a displayed framebuffer |
| GE | 626 initialization parameter submissions; no genuine raster drawing demonstrated |
| Guest pixels / exported PNG | None |
| New UART | None; all 1,421 encoded bytes match the preceding same-mode log |

MIPS coverage and PM51 instruction history are byte-identical to the preceding
checkpoint. All reported summary fields agree except the DVB-T execution,
mailbox counts and final boundary, plus the PM51 fingerprint reflecting the
expanded IRAM metadata serialization. This comparison is not a full RAM snapshot.

The physical PM51 power-good detectors, `0EC2` bit 3, `0E6A` side effects and
RTC/cold-start marker producers remain unresolved. Neither `934D` cold-init nor
the RTC marker-eight producer is demonstrated. MIPS passes its activity check
only under the pre-existing named symbolic non-resume scenario; the PM activity
byte remains invalid. No new PM readiness, power-state transition or DSP reply
is supplied by this change.

Strict reset still stops at `BFC00C60 / BF001C74`; guarded POR and Winbond CF
uncertainties persist. The extended run retains all prior clock, PM, audio,
analog, SAR, IR and GOP hypotheses. Its progress is not verified physical cold
boot. The original 8,388,624-byte input retains SHA-256
`d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9`.

## Validation and preserved evidence

1,233 native checks, both CTest entries, 18 original baseline runs, nine display
runs and four forensics tests pass. Original PM51, DVB-T, AVD and isolated
multimedia regressions pass, as do five clock-profile prefix replays,
immutable-input checks and the DSP decoder regression. The separate VIF probe
passes all 2,048 cases. Both build and packaged Windows window startup pass with
only System32 on PATH; that blank window is not an OSD result.

The complete new five-CPU cold run replays every recorded event, the full
summary, MIPS coverage and PM51 history identically. This validates the new MCU
mode through its final application boundary, rather than only an early prefix.
It establishes determinism of the named hypotheses, not physical timing or PM
input correctness. No checkpoint restoration or instruction skipping is used.

[Validation manifest](../evidence/mcu-stack-validation.json) records report hashes,
the pinned binary hashes and the private full-trace location. Compressed reports
include the complete current summary and full replay result. The Git commit
containing this document and those artifacts is the verified continuation.

Audio DSP2 `BF225AB4` remains best identified as a load-code acknowledgment
input. Its `E3` producer and exact DSP architecture are unresolved; this change
executes no DSP instructions. The original finite audio failure paths remain
the route used by the exploratory application boot. The separate AVD image
still executes only its six original 8051 startup instructions.
