# PM activity producer and unresolved startup status

Continuation of `971a331`. **No authentic OSD frame is demonstrated.**
The hardware producer of `0EC2` bits 0–2 remains unknown. No status seven,
activity zero/eight, interrupt or PM reply has been added to television boot.

## Original startup status function

`9B17` calls the 16-bit getter `6D84`. MOVX at `6DC7` reads high byte
`0EC3`; MOVX at `6DD7` reads low byte `0EC2`. The caller takes only R7,
masks seven, XORs seven and executes JNZ at `9B27` back to `9B17`.
Values 0–6 repeat; seven returns at `9B29`. The high byte stays invalid.
The actual emulator boundary is a rejected unknown branch operand, not an
observed hardware timeout or exception storm.

Only the original SPI CODE-source write `1018=02` and RAM initialization
precede this test. The unexecuted continuation writes `0E13=10`, changes
P2 to `10`, writes `1E03=04`, calls a delay, modifies `0E1E` bit 4,
rechecks the low-three-bit status, clears `0E6A` bit 7 and word `0E26`,
then waits on `0EC2` bit 3. Another caller uses the same helper during a
power-control sequence at `9F21`. This supports a quiescence/reset-handshake
hypothesis, but establishes neither bit identities nor their hardware producer.

The [Titania4 reference header](https://github.com/Benjamin-Dobell/kogan-tv-gpl/blob/master/Kernel_updated/mstar/mips-boards/titania4/hwreg_S5.h)
places `0EC0..0EFF` in a RIU debug region. Its legacy filename and other
register differences limit that evidence: it supplies no definitions for
these bits. The [MST786 datasheet](https://linux-chenxing.org/cedric/mst786_ds_v01maite_new.pdf),
pages 42–43, assigns the same address to a clock-control password on an
automotive chip. That incompatible meaning/reset value was not imported.

## Original producer of activity marker eight

The main helper at `80404BEC` returns `(BF001CD1 & 0F) == 8`.
Caller `8046A190` optionally restores stored source/channel information.
Both branches join at `80469CB8`; neither is a PM polling loop.
There are additional application calls before `80469E38 -> 804E0D48`.
Eight is not a required successful cold-boot reply.

The original PM code supplies a specific producer path:

```text
5E2F event dispatch
  8A7E -> 9D48: sample logical 0E14/0E15 into 40A1/40A2
  8A7E -> 9D54: sample logical 0E1C into 40A3
5EB0..5EB8: extract 40A3 bit 4, call 9B96
9B96: if event is set, call 9E7A -> 99E4
99E4: inspect logical 0E1E bit 4; return one when clear
9B9F: R7=8; call 956B when that gate allows it
956B -> 8AAF: replace logical 0E69 low nibble, preserving upper bits
8ADD -> 724E: original MOVX at 729A performs the write
9BA4 -> 9438: access RTC bank 1200, OR bit 7, write through 724E
```

RTC bank access and the original command shape support an **RTC0-related
wake-event reason** interpretation. Exact interrupt routing remains unverified.
This is static original-code evidence: the path has **not executed** in the
connected machine and no RTC wake event was injected.

Cold initialization has a different writer: `934D` prepares `0E69=0` and
`0E6F=FF`. Its normal chain is reset pre-C initializer `5717` returning,
C initialization at `6136`, jump `60FB -> 7038`, then
`7061 -> 9E49 -> 934D`. Earlier driver/interrupt initialization must return.
The original instruction at `7050` also writes unmodeled SFR `8E`.
None of this establishes a hardware reset value or licenses presetting zero.
See [original-code forensics](../evidence/pm-activity-forensics.json).

## Implemented diagnostics and isolated experiments

`--pm51-history FILE` exports the last 256 instruction attempts and 64 bus
observations. It records PC, opcode, DPTR, accumulator, validity/origin,
stack, PSW, RIU page, virtual tick and whether an instruction actually
dispatched. Rejected transactions and the rejected branch remain visible.
The observations do not affect CPU/peripheral state or deterministic replay.
This is bounded diagnostics, not a restorable checkpoint.

`pm51_probe --status-counterfactual 0..7` supplies only a synthetic
low-three-bit input. It is deliberately absent from `manta.exe`.
Each input/replay is labeled synthetic; `0EC3`, the activity field and
other hardware status remain invalid.

| Synthetic field | Original-code outcome | What it does not prove |
|---|---|---|
| 0–6 | 15,625 original instructions within the bounded run; loop repeats | Physical reset/nonready values or producer |
| 7 | 3,441 original instructions; next rejection `5727 / XDATA 0E13`, write `10` | Real readiness, completed PM initialization, MIPS or TV boot |

No writer at `934D`, activity marker or MIPS instruction executes in these
isolated experiments. The television path retains unknown status. The
experiments identify the next required control interface without introducing
a successful peripheral model. [Regression](../evidence/pm-status-regression.json).

Diagnostic/NVRAM output paths reject aliases of the selected immutable
firmware, including hard links. Existing output options and the new history
export cannot truncate that input. File-policy fixtures test this separately
from original guest execution.

## Connected execution and remaining blockers

The connected cold boot still reaches `80404BF4 / BF001CD1` with:

| Processor | Original instructions |
|---|---:|
| MIPS | 1,789,567,273 |
| OpenRISC TSP | 280,822,700 |
| DVB-T MCU, total across resets | 468,999 |
| AVD MCU | 6 |
| PM51 | 3,435 |
| Audio DSP | 0 |

Main timer ISR entries: 5,441; DVB-T MCU: 52; PM/AVD: zero.
PM still stops at `9B27`, unknown origin `0EC2`, known mask `F8`.
No new UART messages or genuine PM replies were observed. Existing TSP
communication and DVB-T diagnostic responses remain intact.

The cold summary matches `971a331` exactly. Its history matches the final
executable's 300,000-dispatch cold prefix byte for byte. Prefix execution
is not claimed as a second full boot. History SHA256:
`b4064adbb5c8b41a54398bed072d8043bf09b4b9db328db61c4695def4481cac`.

ZUI call site `80469E38` and initializer `804E0D48` remain unentered.
GOP initializes twice, commits 64 palette entries and has 8,249 aperture
events. GE has 2,924 aperture events and 626 parameter transfers. These
are configuration, not raster drawing. No active framebuffer, enabled
GOP window, guest pixels, PNG or authentic native scan-out exists.

Independent boundaries remain AVD `0802 / C008`, DVB-T `4AE0 / 2200`,
audio acknowledgment/ISA, strict POR and undocumented Winbond `CF` behavior.
Strict mode retains its earlier POR stop. Exploratory reset/clock/input
hypotheses remain explicit; the counterfactual is not a boot option.

Required evidence is a matching Titania4 PM/RIU register definition or an
already-established safe read-only capture of `0EC2/0EC3`, `0E13`, `0E1E`,
`0E26`, reset/clock sequencing and `0E69` across AC boot/standby/wake.
The capture must establish the producer and ordering, rather than one value
that satisfies a comparison. No physical TV or unknown pins were accessed.

## Verification

Windows build and **825 native checks** pass; CTest is 2/2. All **18 baseline
firmware runs**, **nine display runs**, **four forensics tests** and auxiliary
regressions pass. The final executable completes the full original five-CPU
deterministic replay, checking every recorded event and the PM state/validity
fingerprint. Clean-PATH native window startup passes; 18 file-policy cases
reject input/output aliases without touching their input.

See [verification and source/build hashes](../evidence/pm-status-verification.json),
[full replay report](../evidence/pm-status-full-replay.json.gz),
[baseline regressions](../evidence/pm-status-baseline.json.gz) and
[display regressions](../evidence/pm-status-display.json.gz).
Original firmware SHA256 remains
`d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9`.
