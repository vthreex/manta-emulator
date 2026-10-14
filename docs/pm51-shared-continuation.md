# Original PM51 on the shared machine bus

This continues `2258306`, preserving the original firmware, CPU engines and
previous peripheral models. No authentic OSD is demonstrated.

## What the activity check does

Main routine `80404BEC` reads `BF001CD1`, masks `0F`, compares with eight,
and returns a Boolean. Caller `8046A190` optionally restores stored source
and channel information through `8049A050`, `803EF670`, `803F8F9C` and
`803F8FE8`. Both paths join at `80469CB8`. This is not a main-CPU polling
loop or an IRQ wait. The emulator rejects an unknown input before the
predicate executes. Background/resume activity is the strongest supported
interpretation; exact silicon/reset semantics are unverified.

Original PM routine `934D` prepares zero for logical `0E69`, calls the byte
writer `724E` (MOVX at `729A`), then prepares `FF` for `0E6F`. Caller chain
`7061 -> 9E49 -> 934D` follows additional driver/interrupt initialization.
This writer has not executed. Routines `8AAF`, `8984` and `90D8` also modify
the activity field. No host marker is assigned.

## CODE and XDATA connection

The exact original 65,536-byte block `10000..20000` has SHA256
`5f9d541120948f5067d162048c17f30fc704f871f9fdfbcc161181567f48fd3c`.
Reset vector `0000` jumps to `60D8`. Standard 8051-family instructions
execute through the existing interpreter. The CODE view references the
main machine's immutable flash storage, and fetch/MOVC validation checks
the current SPI backing. There is no independent replacement program.

`--exploratory-pm51-spi-startup` selects an explicit housekeeping/concurrent
reset experiment. The original `1018=02` instruction selects the configured
SPI view. Related historical ROM-source definitions corroborate this candidate
interpretation; exact Titania4 reset/fetch wiring remains unverified.
Private XRAM `4000..43FF` is inferred from the original 1,024-byte clear.
Reads before original initialization stop. No private-RAM reset data is supplied.

For the observed page-zero PM bank, logical byte address `a` maps to
`1F000000 + 2*(a & ~1) + (a & 1)`. Thus `0EC2/0EC3` use the MIPS RIU
bytes `1F001D84/1F001D85`, and `0E69/0E6F` use `1F001CD1/1F001CDD`.
They use the same register backing and validity masks, not independent copies.
Other pages and unimplemented controls still stop. The optional external
AC-on observation is consistent in both views; a subsequent PM write takes
precedence, including preservation of invalid bits.

Scheduling uses deterministic MIPS-dispatch time. The default divider of 32
dispatches per PM machine tick is an explicit hypothesis, configurable with
`--pm51-dispatch-divider`; it is not a measured physical clock relationship.
Unknown required input suspends PM without generating a response while the
other processors continue their explicitly guarded diagnostic execution.

## The actual dependency behind 0EC3

Getter `6D84` reads a high byte at `6DC7` and a low byte at `6DD7`, caches
them at XRAM `4283/4284`, and returns them in `R6/R7`. Caller `9B17` consumes
only `R7 & 7`. The high `0EC3` observation remains invalid and unused.
The 8051 provenance layer now preserves invalid data through banked Rn,
direct/indirect IRAM aliases and reloads. Unknown address, stack/return and
decision consumers stop. Failed validation cannot commit speculative metadata.

PM therefore executes **3,435 original instructions**, 1,034 private RAM
writes and 11 reads, then stops before **JNZ at 9B27**. Its accumulator has
known mask `F8` and origin `0EC2`: the required low three bits remain unknown.
The program requires `(0EC2 & 7) == 7`; later it also tests bit three.
No matching Titania4 definition identifies those status producers. Related
RIU-debug region names do not establish their meanings. Before this first
check, the original PM code has written only its ROM-source control and RAM.
Hardware, earlier ROM activity or external initialization must be researched;
no completion value is manufactured.

A test-only partial status fixture with low bits four makes the original
program keep polling. It never reaches the marker writer or TV startup.
The power-alias fixture tests bridge semantics with zero original instructions;
its injected values are not firmware responses or stock reset measurements.
These fixtures are separate from the full cold boot, whose status bits remain
unknown. See [PM regression](../evidence/pm51-shared-regression.json).

## Full television boot result

The connected cold boot still reaches main **80404BF4 / BF001CD1**, with
**1,789,567,273 MIPS**, **280,822,700 OpenRISC**, **468,999 DVB-T**, **six AVD**
and **3,435 PM** instructions. Audio DSP instructions remain zero. Main timer
ISR entries: **5,441**; DVB-T ISR entries: **52**; PM/AVD ISR entries: zero.
There are no new UART messages beyond the previous original audio recovery
messages. Original TSP communication, DVB-T diagnostics and all existing
display initialization remain intact.

The PM caller and activity helper execute; ZUI call site `80469E38` and state
initializer `804E0D48` do not. GOP initializes twice, with 64 palette commits
and six disabled-window programming entries. No enabled descriptor, proven
OSD framebuffer, GE raster drawing, guest pixels or PNG is demonstrated.

Independent boundaries remain AVD `0802 / C008` and DVB-T `4AE0 / 2200`.
The audio E3 producer/ISA and strict POR/Winbond-CF behavior remain unresolved.
Required next evidence is a matching Titania4 PM/XDATA register definition,
or safe read-only captures of `0EC2/0EC3`, reset/SPI/clock/MIU controls and
the activity field across AC startup. Capture the producing sequence, not
just a successful value. No physical TV or unconfirmed pins were accessed.

Verification: Windows build, **822 native checks**, 18 baseline runs, nine
display runs, four forensics tests, auxiliary regressions and clean-PATH native
startup. The full five-CPU replay checks original opcodes, shared accesses,
CPU counters, every recorded event and the PM state/validity fingerprint.
See [replay](../evidence/pm51-shared-replay.json) and
[source/build provenance](../evidence/pm51-shared-provenance.json).
The fingerprint is diagnostic; no restorable checkpoint is claimed.

## Reproduce the checkpoint

```powershell
python tools/run_pm_boot_checkpoint.py --firmware ORIGINAL.bin --summary pm51-checkpoint.json
```

The launcher spells out every earlier exploratory configuration and uses the
local native executable. `--print-command` shows the complete argument list.
`--window` uses the existing native frontend. Tracing is optional (`--trace`)
because a full trace is about 10 GB. The shipped launcher was also exercised
against a bounded 300,000-instruction reset prefix; this is separate from the
verified full cold boot/replay and does not claim application completion.
