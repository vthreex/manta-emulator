# VIF zero-energy input experiment

This builds on `195ac82`. It does not establish measurements from an unplugged
Manta antenna, silicon reset values, or an authentic graphics frame.

## Original producer and consumer

The original `8050962C` handler reads `BF224644` with LBU at `80509D34`.
The first required arithmetic use is ADDIU at `80509D54`. This is an unsigned
8-bit AGC mean16 observation, not an acknowledgement or data-ready register.
The adjacent reads at `80509D38` and `80509D3C` consume PGA2 feedback low five
bits (`BF22465D`) and a VGA feedback halfword (`BF224650`).

Identification follows the matching handler and initializer in the related
[MStar VIF driver](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/device/if_demodulator/MsVIF.c)
and its [register definitions](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/device/if_demodulator/MsVIF.h).
Its absolute bank differs; T4 addresses come from original MIPS instructions.
Original `80508080..8050808C` programs PGA2_MAX low five bits to 31.
`80507F78` programs the AGC reference; `8050818C` enables AGC.
`80508E74` releases all seven observed VIF reset fields. Original register
history contains no software producer of the three feedback observations.

An IF/AGC hardware pipeline is the strongest supported producer. A related
[MStar patent](https://patents.google.com/patent/US8233093B2/en) describes ADC,
envelope detection and feedback gain adjustment. It does not specify T4
register reset values or an exact T4 gain-distribution algorithm.

## Deliberately conditional input

`--exploratory-vif-zero-input-settle-cycles N` enables an idealized, noiseless,
zero-energy post-ADC input. It additionally requires guarded symbolic mode,
deferred diagnostic reads and the separate disconnected-analog option.
It remains disabled by default. This idealization excludes ADC offsets,
thermal noise, tuner noise and interference present on a real disconnected
antenna. The zero envelope is mathematical; PGA2 reaching its programmed maximum,
sampling clocks running and the specified settling interval are hypotheses.

The model requires guest-written VIF reset release, AGC enable, mean16 selection,
LPF0/internal-gain selection, positive reference, released gain overrides,
consistent PGA2 bounds and a nonzero line count. Changing consumed configuration
restarts settling; disabling or invalidating a prerequisite invalidates output.
Preserved unknown high bits never act as a configuration change.

After N virtual CPU cycles, only these observations become defined:

| Address | Conditional observation | Valid mask |
|---|---|---|
| `BF224644` | zero mean envelope | `FF` |
| `BF22465D` | guest-programmed PGA2_MAX | `1F` |
| `BF224650..651` | unmodeled VGA | `0000` |

No gain is fixed to 31 in the device model. All 32 programmed maximum codes are
tested. No carrier lock, HSYNC, RF strength, channel, interrupt or completion is
created. Hardware-owned observations reject guest writes. Summary output and
trace events identify the hypothesis and serialize its configuration/timing
state for deterministic replay. The interval is an explicit experiment parameter,
not a calibrated conversion rate or a firmware timeout.

## Original instruction tests

The hash-verified original MIPS image executes 2,048 negative-modulation and
2,048 positive-modulation calls, covering all 256 mean codes and PGA/VGA branch
boundaries. Respectively 272 and 450 calls return without advancing acquisition.
Six additional calls use the actual RIU model with three settling intervals and
both initial acquisition states. The original code returns through its saved
stack/RA and never compares the still-invalid VGA observation on this path.
This isolated execution is not itself a full television boot.
Another 186 original calls program PGA2 maxima 0..30. The production bit-provenance
guard rejects every resulting invalid-VGA comparison at `80509E0C`, before
any acquisition write. The model cannot silently fill in the next measurement.

Native checks cover each required unknown configuration bit, all PGA2 maxima,
deadline boundaries, disable/re-enable, gain overrides, inconsistent limits,
unrelated poison bits, read-only outputs and controlled stops before settling.
CLI checks reject missing guards, missing disconnected-input scope, zero,
negative and malformed intervals. A guarded prefix reports the hypothesis while
producing no early measurements.

## Why ZUI initialization alone does not draw

Original `804E0D88` sets ZUI state (`806D7E98`) to 1. The dispatcher at
`804E0DC4` handles states 2, 3, 4 and 5; state 1 takes its ordinary idle return.
The normal dispatcher call is `8047606C`, inside `8047602C`, called by the
application loop at `80488864`. The loop first calls `8045CFC4` and `8046CA74`.
The VIF wrapper `804F2988` returns naturally after its handler; it is not an
infinite original wait established by the earlier emulator stop.
Entering the dispatcher, creating a surface and enabling scan-out remain
separate milestones. This experiment never calls a UI function from the host.

## Complete exploratory execution and next dependency

The new cold execution passes `80509D54`, takes the original PGA2-limit branch
`80509D68`, changes acquisition to state 2 and returns at the original wrapper's
`804F2998`. It reaches 854,795,125 original MIPS dispatches, 91 beyond the previous
boundary. It then stops at `805032A0 / BF206BB8`. ZUI initialization and return
remain covered once; dispatcher `804E0DC4` remains unentered. This is conditional
progress under the explicitly configured ideal input, not verified cold boot
with a physically disconnected antenna.

The new stop is the original `8050328C` routine. The related symbol-bearing AVD
library names the matching implementation `HAL_AVD_AFEC_SetRegFromDSP`; its
register offsets and complete branches agree with the original. This is a
host-assisted register request from the AVD MCU, not a VIF measurement or
HSYNC-detection flag. No idle request value is established.

| Field | Meaning established by original MIPS instructions |
|---|---|
| `BF206BB8 & 0C` = `08` | Masked write requested; host writes target and clears command bits |
| `BF206BB8 & 0C` = `04` | Masked read requested; host returns data and sets command bits to `0C` |
| Command `00` or `0C` | Original routine returns without a transaction |
| `BF206BB8 & 03` | Zero selects logical bank `3600`, any nonzero value selects `3500` |
| `BF206BB9` | Register mask |
| `BF206BBC` | Register byte index |
| `BF206BBD` | Request or returned data |

The write computes `(old & ~mask) | (data & mask)`; the read computes
`old & mask`. The original handler does not wait or synthesize requests.
The isolated `avd_request_probe` executes all 1,152 command/bank/mask/data/target
fixtures through this complete original function. Four unknown-command cases
reject at `805032B0`; four partial-command cases verify that inactive operations
ignore unused fields while pending operations reject an unknown bank.
None of these fixtures is connected to television boot.

The actual AVD payload contains a static request producer at `1309..130D`:
R5=09, R7=DC, LCALL 02AA. Its helper `075E..0767` builds XDATA `1100+R7`,
and `02AD` performs MOVX, giving `11DC=09`. This is consistent with the host's
logical `35DC` command/register-bank protocol. The complete auxiliary RIU mapping
and producer execution are not yet demonstrated. The connected AVD remains
stopped at its first unknown `0802 / XDATA C008` write after six instructions.

A [related MStar embedded-MCU register manual](https://datasheet4u.com/pdf/770936/MST703.pdf)
names C008/C009 watchdog disable keys. Its documented disabled pair is AA/55,
whereas this original AVD startup writes 55/AA. That is evidence for the register
family, not permission to assume the same polarity, reset state or watchdog
timing on Titania4. Neither key write is ignored, and no idle mailbox or auxiliary
response is assigned. Resolving that startup hardware and shared mapping is the
next evidence-dependent application barrier.

The run also executes 2,453,934,820 original TSP instructions, 1,454,515 total
DVB-T instructions (475,509 in the final reset instance), 161 DVB-T interrupts,
3,474 PM51 instructions and 38,589 main timer ISR entries. PM51 has zero IRQ
entries; AVD remains at six instructions and audio DSP at zero. PM51 still stops
at `574F/0E6A`, DVB-T at `4AE0/2200`. Original UART is unchanged. The existing
strict POR, SPI CF, PM non-resume, clock, audio no-ack, IR and GOP uncertainties
remain separate from this new input hypothesis.

There is no proven ZUI resource/drawing-loop entry, framebuffer allocation,
enabled GOP window, GE raster modification or native guest frame. GE still has
626 initializer parameter submissions. The disabled 8x8 `00DDD800` descriptor
still contains boot BIST data, not a graphical result; no PNG is exported.

## Verification and artifacts

The Windows build passes 1,346 native checks and both CTest entries. All 18
baseline firmware regressions, nine display regressions, four forensics tests,
five clock-profile replays, original PM51/DVB-T/AVD and multimedia diagnostics,
immutable-input checks and the existing DSP disassembler regression pass.
The packaged executable starts with only System32 on PATH; this tests window
creation, not guest display output. The input hash remains unchanged.

Complete replay matches all 6,591,839 recorded events, the complete summary,
MIPS coverage and PM51 history. No checkpoint, instruction skipping, PC override
or guest patch is used. The raw trace is retained privately; the
[validation manifest](../evidence/vif-zero-validation.json) pins its hash and
location, compiled binary hashes and compressed reports. Those reports include
full cold/replay results, exact VIF entry/return events, UART comparison and
the new AVD request-boundary evidence. The reference host function's complete
256 bytes differ from original code only at three global-variable address relocations.

The commit containing this document and manifest is the tested continuation of
`195ac82`. The shared MCU provenance repair remains intact; independent strict
POR/SPI, PM, AVD and audio DSP uncertainties are not promoted to defaults.
