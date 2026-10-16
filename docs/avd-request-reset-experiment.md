# AVD request-latch reset experiment and HTotal producer

This follows `5cea1ba`. It tests a **new, unverified reset hypothesis**. It
does not identify Titania4 reset values or demonstrate a functioning AVD MCU.
No authentic OSD pixels exist.

## Independently enabled hypothesis

`--exploratory-avd-host-request-reset-empty` hypothesizes a nonretained
host-request latch whose operation bits clear when the original MIPS firmware
holds the AVD MCU for programming. This option requires guarded shared AVD
execution and explicit unresolved-MCU suspension. It is disabled by default.

The firmware-level rationale is narrow: both original processors examine
command bits `0C` before doing a transaction; the recovered AVD producer
submits requests later, and neither observed startup initializes those bits.
An initially empty, reset-cleared channel is a plausible digital reset model.
That does **not** prove its reset value, retention, or coupling to the observed
hold control on MSD309. Other reset states remain possible. The option must
not be presented as an identified silicon producer or validated cold boot.

The model acts only on an actual, known guest hold assertion at `BF2068C1`.
It clears/defines only command bits `0C` in shared `BF206BB8`. Bank bits,
upper fields, mask, index and payload are untouched. Enabling the option
alone changes no register. Releasing the MCU generates no completion.
Real submitted requests remain pending across reads; MIPS must execute its
original service routine to complete them. Under this hypothesis, a subsequent
hold discards a pending latch rather than marking it DONE or modifying data.
Every such reset is logged and counted as hypothetical.

The original full 256-byte MIPS helper is hash checked before a partial
command observation is allowed. Its LBU copies the byte before testing `0C`.
Normal bit provenance retains all invalid fields; an actual pending operation
still cannot select an unknown bank or use an unknown payload. No PC, guest
branch or firmware instruction is changed.

## Actual execution result

Under this option, plus the previous named hypotheses, the original MIPS
service returns to `8050170C`. Main executes 46 further original dispatches
and reaches **`80501BFC` after 854,795,171 dispatches**. It stops on BNE of the
new AVD horizontal-total sample against the prior value, which is zero.
The sample stays invalid. No horizontal timing value or equality result is
supplied.

Original `805030E0..80503117` is `HAL_AVD_AFEC_GetHTotal`, independently
matching the named related driver. It assembles:

| Original load | Register | Result lane |
|---|---|---|
| `805030F8` | `BF206B90` | high byte |
| `805030FC` | `BF206B8D` | low byte |

Two byte/hash-checked debugger rules permit these as **INVALID observations**.
The policy format has no value, known-mask, branch or acknowledgement fields.
Original code copies them into its history at `806D83CC`; two more RAM bytes
retain unknown provenance. At the first required comparison, normal provenance
stops execution. The old private policy was backed up and restored; the new
experiment's final policy is preserved separately as `avd-host-opaque-policy.tsv`.

## Recovered original HTotal writer

AVD `0AA9..0ABC` reads native `113C/113D`, masks the high byte with `07`,
loads `R7=C7`, then calls original `076E`. The actual MOVX stores at `0775`
and `0784` write `11C7/11C8`. These correspond exactly to the two MIPS
HTotal lanes above under the candidate shared mapping. Thus an original AVD
routine, rather than a fabricated host reply, can publish the 11-bit sample.

That writer has **not executed** in the connected run. Its raw counter input's
clock, reset, latch and absent-HSYNC behavior still need hardware evidence.
No pulses does not by itself establish a zero or any other valid sample.

The connected AVD remains at `1C26`, requiring captured `119C` for startup
table addressing. The previous [shared-prefix analysis](avd-shared-prefix.md)
identifies the candidate MIPS configuration writer, which remains unentered.
Default AVD startup still stops at `0802/C008` after six instructions.

## Milestones and limits

| Item | Current reset-latch experiment |
|---|---|
| Main MIPS | 854,795,171 dispatches; stop `80501BFC` |
| Main service at `805032A0` | passed; original return `8050170C` covered |
| TSP | 2,453,934,820 original instructions |
| DVB-T | 1,454,515 instructions, 161 IRQ entries; `4AE0/2200` |
| PM51 | 3,474 instructions, zero IRQs; `574F/0E6A` |
| AVD | 379 instructions, zero IRQs; `1C26`, captured `119C` |
| Audio DSP | zero instructions; architecture/ACK producer unresolved |
| Main timer entries | 38,589 |
| UART | unchanged |
| ZUI initializer | original entry and return still covered |
| ZUI dispatcher | `804E0DC4` still unentered |
| Framebuffer / enabled GWIN | none demonstrated |
| GE / visible pixels | initialization only; no genuine raster effects or pixels |

The only newly defined command bits in this run come from the explicit
reset hypothesis. No original AVD request, readiness reply or MIPS request
completion was observed. Consequently **the AVD dependency is not resolved
as verified hardware behavior**, even though this conditional path advances
the main CPU. The disabled BIST-filled 8x8 buffer is still not a frame.

1,396 native checks pass, including reset causality, untouched unknown fields,
pending polls, explicit service and reset-abort behavior. All 18 baseline and
nine display runs, auxiliary, clock, VIF, request, DSP-decoder, immutable-input
and four forensics tests pass. Both CTest cases and packaged Windows startup
pass with a System32-only PATH. Strict/missing-suspension configurations reject.
The new experiment recorded 6,592,234 events. Its full replay and a separate
full replay with this hypothesis disabled are pinned in the
[validation manifest](../evidence/avd-reset-validation.json).
The disabled mode preserves every prior event, summary field, coverage entry
and PM-history byte. Deterministic replay does not verify the reset hypothesis.

See [analysis](../evidence/avd-reset-analysis.json.gz) and
[conditional full replay](../evidence/avd-reset-full-replay.json.gz).
