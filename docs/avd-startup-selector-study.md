# Original AVD startup-field dependency study

This follows `a801d31`. It does not advance connected TV boot or identify
the real startup selector. It fixes an additional validity-tracking gap and
tests actual original AVD instructions with explicitly synthetic, isolated
inputs. The TV executable rejects the fixture option.

## Direct IRAM masked updates

Original `1CB1: ANL 07,#1F` masks a register alias while ACC still holds an
invalid value. This operation does not need concrete values for the bits it
preserves. The wrapper now tracks direct-IRAM immediate AND/OR/XOR, retaining
unknown bits and origin. The existing 8051 interpreter executes the instruction;
its engine is unchanged. For the original AVD update, subsequent instructions
set bit 5 and write the byte back to the same native `114C` origin. High bits
become defined; the low five bits stay invalid. No reset value, clock state
or measured input is supplied by this repair.

Native tests cover every source byte and boundary masks for all three
operations, including an unrelated invalid ACC. Both previous full-machine
paths replay unchanged after this repair.

## Isolated original-program results

`avd_mcu_probe ORIGINAL.bin --deferred-shared-prefix
--synthetic-startup-selector VALUE MASK` runs the complete original program
from its reset vector. The program is hash checked and unchanged. One
synthetic shared byte, candidate native `119C`, is supplied only to this
standalone diagnostic. The existing deferred startup-effect and bus/clock
hypotheses still apply. All other external inputs remain invalid.

| Known selector fields | Original instructions | First required unknown use |
|---|---:|---|
| none | 379 | `1C26`, first table address |
| bits 1, 2, 4 only | 404 | `1C3C`, MUL for second table address |
| bits 1, 2, 4, 5, 6, 7 | 863 | `0702`, unrelated native `11DB` bit 7 |
| all eight bits | 863 | same `0702` decision |

All 256 fully specified input patterns reach the same instruction count and
first-visit instruction sequence, with no host-request command written.
This does not establish that all patterns are valid hardware configurations.
Ten partial-field cases independently check which unknown bits really matter.
Bits 0 and 3 are not needed before this later boundary: original instructions
discard them or replace the affected carry before it is consumed.

The isolated 863-instruction path performs 21 actual shared-register stores
and 25 private-RAM stores, with zero interrupts. Its original `114C` masked
store has known mask `E0`, preserving unknown low bits. It then reads `11DB`
(candidate MIPS `BF206BB5`) and stops before `0702: JNB ACC.7`. Neither that
bit's hardware meaning nor its producer is established here. **These 863
instructions are not connected-TV progress.** Connected AVD remains at 379.

## Original MIPS initialization was checked

Original `8050479C..805047C0` pulses bit 7 at `BF206A28` and invokes the
table writer at `80502F1C` with pointer `8062AE00`. The original table is
237 bytes including its terminator: 78 register/value triplets. Its SHA256 is
`c581361ebf3c20c80b8fab703306996151d808102cc18490c7d925fd1ddbd798`.
Every one of its 78 writes matches the actual original `80502F98` trace in
order. The table contains no entries for logical `359C`, `35DB`, `35DC`,
`35C7` or `35C8`. No other original MIPS write to the selector, later flag or
host command appears before the current stop.

Thus the missing fields are not explained by an overlooked write in this
executed initializer. The precise domain/defaults of the `BF206A28` pulse
remain unverified; it is not used to invent register-reset values. Related
Marlon initialization data differs from the original table and is not imported.

## Connected TV status remains unchanged

With the separate unverified request-latch reset hypothesis, original MIPS
reaches `80501BFC` after 854,795,171 dispatches and rejects unknown HTotal.
Without that hypothesis, the shared-run mode stops at `805032A0` after
854,795,125 dispatches. The two whole-machine replays match 6,592,234 and
6,592,227 recorded events respectively, full summaries, coverage and PM
history. Replay compares newly executed events; it does not feed recorded
device responses into the guest.

TSP remains at 2,453,934,820 original instructions; DVB-T at 1,454,515 with
161 interrupt entries; PM51 at 3,474; connected AVD at 379; audio DSP at zero.
Main timer entries remain 38,589; PM51/AVD IRQ entries remain zero. UART is
unchanged. Original ZUI initialization returns, but its dispatcher, framebuffer
allocation, enabled GWIN, GE raster effects and authentic pixels remain absent
from the demonstrated execution. No screenshot or PNG is exported.

The next evidence needed is still actual AVD startup-field/reset behavior
and the counter/latch behavior required by the original HTotal publisher.
Synthetic selector values are deliberately unavailable in TV boot.

## Validation

1,399 native checks, 18 baseline runs, nine display runs, original auxiliary
regressions, clock profiles, VIF/request/DSP-decoder/immutable-input suites,
four forensics tests and both CTest cases pass. Packaged Windows startup and
native checks pass with System32-only PATH. The original firmware SHA256
remains unchanged. No CPU engine or original firmware was replaced or patched.

Run the new isolated test with:

```
python tests/avd_selector_regression.py --firmware ORIGINAL.bin --probe BUILD/avd_mcu_probe.exe --emulator BUILD/manta.exe --report REPORT.json
```

See [validation manifest](../evidence/avd-selector-validation.json),
[isolated study](../evidence/avd-selector-isolated-study.json.gz), and
[original table evidence](../evidence/avd-selector-original-table.json.gz).
