# HTotal data provenance and original ZUI resource setup

This continues `1745c6c`. **No original OSD frame is demonstrated.**
Connected instruction counts and hardware boundaries are unchanged. The work
repairs a data-copy limitation, tests original timing/window instructions, and
extends read-only observation. It does not identify a silicon event producer.

## Timing data is allowed to remain invalid

Original AVD `0AA9..0ABC` reads `113C/113D`, masks the high byte with `07`,
and calls `076E`. Its stores `0775/0784` publish low then high to `11C7/11C8`.
Previously, the conservative bus rejected a partial-known value copied to a
different register origin, even for this identified software data publication.

In the existing symbolic-data diagnostic mode, the bus now permits only:

- raw-origin `113C` to published low byte `11C7`;
- raw-origin `113D` to published high byte `11C8`, with the upper five bits
  already defined as zero by the original mask.

The actual value, known mask and source origin are copied. A previously known
output is demoted to invalid when the source is invalid. There is no merge
that retains stale known output bits. Swapped lanes, unrelated origins and
partial high bytes without the required mask are rejected. This permission
cannot initialize a raw counter, event or host command. Default bus behavior
is unchanged. No source bit becomes known because a copy is permitted.

This fixes the software transfer mechanism, **not the measurement producer**.
The connected MCU has not reached these stores, and MIPS still cannot consume
an unknown timing comparison.

The new `avd_htotal_probe` checks 2,348 sample-copy cases, including 276 with
invalid measured fields. An isolated test program contains unchanged original
sampling instructions and their unchanged absolute helper; it begins at its
own reset and follows actual CALL/RET instructions. This is a synthetic
fragment harness, not the original TV reset-to-publisher path. The original
firmware file, original reset vector and connected PC are never changed.
The 117,400 fragment instructions are **not added to connected AVD counts**.

## What the MIPS comparison actually does

Original getter `805030E0` reads high at `805030F8`, then low at `805030FC`,
from candidate `BF206B90/BF206B8D`. It combines **all sixteen bits**, without
applying its own eleven-bit mask. The AVD publisher's AND is therefore relevant;
reserved high bits cannot simply be presumed zero before it runs. 512 original
getter fixtures test this width and ordering, including upper-byte bits 3..7.
The tests establish software behavior under the candidate mapping, not physical
MSD309 wiring or an atomic latch across the two accesses.

The stability logic uses RAM:

| Field | Original address |
|---|---|
| Current/newest sample | `806D83CC` |
| Previous sample | `806D83CE` |
| Older sample | `806D83D0` |
| Equality counter | `806D83D4`, byte |
| Accepted stable HTotal | `806AD960`, halfword |

`80501728..80501748` shifts the history and stores the new observation.
If the two previous samples differ, the counter becomes zero. Otherwise,
`80501BFC` compares the current sample with that history. A mismatch also clears
the counter. Three matching samples increment a counter below four; if its
old value is already at least four, the current sample is accepted at
`80501C20`. Values above four are retained, not clamped by this block.

All paths rejoin at `80501758`; there is **no wait loop in this comparison**.
320 original-instruction fixtures cover equal/unequal histories and counter
boundaries 0, 3, 4, 5 and 255. Each reaches the join within forty instructions.
These isolated samples never enter TV execution. A finite software path does
not make its unknown input safe to consume. Later driver code also has other
input-dependent operations; returning from this block would not prove a return
from the entire platform polling call.

## Original application/control-flow map

The main loop calls `8046CA74` at `8048885C`, before its UI wrapper call at
`80488864 -> 8047602C`. That wrapper calls the dispatcher at
`8047606C -> 804E0DC4`. It has not executed in the connected run.

The original source-service path includes these concrete calls:

```
8046B690 source-service callback
  8046B6B8 -> 8048863C, periodic source/SCART-state service
  8046B6C0 -> 804861B4, time-gated source processing
    80486260 -> 803E3964
      803E39CC -> 803E34D8
        803E34E4 -> 805016C8, AVD maintenance
          80501704 -> 8050328C, host-assisted register requests
          8050170C -> 805030E0, HTotal getter
          80501BFC, unknown current/history comparison
```

These call sites are present in the original image and covered in the run.
The source callback is installed by original code at `804878D8..80487908`;
its pointer `8046B690` is supplied to `805F6F68`. This is an indirect callback
relationship, not an additional direct JAL. The exact callback scheduler's
complete dynamic stack is not asserted from coverage alone. Platform polling
is observed before the stop; the subsequent UI-wrapper call is unentered.
Source branches other than this analog path exist, so AVD maintenance is not
proved mandatory for every possible input source.

The callback's source processing has an elapsed-time gate, rather than an
unconditional acquisition wait. The original TV normally receives some digital
counter result even where the software can tolerate unstable input. The current
stop is the emulator's conservative unknown-value boundary. No invalid sample
is replaced with a convenient value or an assumed equality outcome.

## A genuine software path that can open an OSD

ZUI's initializer explicitly clears the window count before calling
`8049EE1C`, then sets state 1, clears ActiveOsd and initializes its queue.
The new cold-run observer confirms this call executes with **count 0 and a
null window table**. It is empty window-state setup, not creation of a screen.
The initializer's later return and platform polling still show state 1,
ActiveOsd 0, an empty 40-slot queue and no windows.

Inside the unentered UI wrapper, one mapped timeout path reads a timestamp at
`80476B88`, calls the original elapsed-time getter, and tests against `0BB9`
at `80476B9C`. With the additional original flag-4 test true, it branches
through `80476EA0 -> 8048848C`. The latter can call `804C3684` with OSD ID 3
and then `804C29E0` with action `0102`, when no OSD is active and its suppression
byte is clear. This provides an original software activation path without a
fabricated remote key. Its enclosing application-state conditions have not
been demonstrated dynamically, so it is **not yet the proven first boot event**.

Another source-state service at `8048863C` is easy to misidentify as video-lock
handling. Its `80486B38 -> 80486B00 -> 804F0D48` chain uses a logical SAR query
2, scaled by twenty, with thresholds 1001 and 2301. It tracks classifications
in `806D4A30` and transitions in `806D4A2C`, then calls source/OSD actions.
It must not be labeled an AVD sync/event producer merely because it also opens
OSDs. No new SAR value or software event is supplied.

## Original resources and the first surface path

The original resource getters and `8049EE1C` now execute in isolation against
all 29 actual OSD resource sets: **3,616 window records**. The tests verify:

- each original count/table getter, including rejection of invalid IDs;
- visible/disabled bitmaps from the actual window flags, bits 0 and 2;
- original bitmap pointer layout and per-window indexing;
- the original fixed-size rectangle copy using LWL/LWR and SWL/SWR;
- byte-identical original instruction/resource storage after execution.

These tests execute 414,612 window-state instructions, confined to a separate
CPU fixture. They allocate no TV surface, render no pixels and do not activate
an OSD in the connected machine.

For OSD ID 3 the original path is:

```
804C3684 -> case 804C37F0 -> 804AFDFC
  count/table/rectangle resource getters
  804AFE78 -> 8049EE1C, window-state setup
  804AFEAC -> 8049DFC4, candidate surface setup
    8049E0C4 -> 805366D4, width/height and raw format argument 0C
    8049E118 -> 803EA14C
    8049E184 -> 803EAC58, candidate clear/draw submission
    8049E1C4 -> 8052CCDC
    8049E244 -> 803EAC58
    8049E260 -> 8052FBD4 with argument 1
  804AFECC -> 804A071C, message 14 for each actual window
    804A0760 -> 804A0640, dispatch
      804A070C -> 804C2DAC, original control-procedure routing
```

Raw call arguments are not decoded as a validated pixel format or proven GWIN
enable. The surface routine also uses indirect graphics callbacks. These are
specific future trace targets, not completed hardware transactions. ID 3's
113-window descriptor and rectangle `(3,520,1360,248)` remain original resource
data. Its screen name and relation to panel pixels are not asserted.
The observer now watches resource, window-state, surface and direct-message
entries independently. Only the zero-count initialization entry is newly seen.

## AVD events and physical evidence still missing

Connected AVD remains at `0702`, consuming `11DB.7` to select coefficient
scaling. Its exact register definition, producer and reset remain unresolved.
Both isolated branches can reach main-loop `0292 -> 098D`; this is not connected
progress. `11FC.2` clears private counters `62/30`, while captured bit 0 gates
the path containing HTotal publication. They are distinct software roles.
The complement acknowledgment still stops at unsupported `11FC` stores;
no W0C semantics, event source or reset is invented.

Original EA enable at `0284` has not executed in connected startup. Thus zero
connected interrupts is consistent with its current location; it is not proof
that a missing synthetic interrupt should be added. The HTotal path is polled
from the main loop. The known Timer0 ISR does not produce the native event byte
or timing sample.

Related [MStar bootloader VDMCU bank definitions](https://github.com/neuschaefer/mstar-mboot/blob/962e8b8258378dded694883a9f9acb7058d34631/sboot/src/amethyst/include/c_vdmcu_riubase.h)
identify the 1100/1200 native VD regions on that platform, but not these bit
fields. The [MST786 datasheet](https://linux-chenxing.org/cedric/mst786_ds_v01maite_new.pdf)
has only a partial AFEC table on pages 1092-1094 and COMB free-run controls
afterward. It does not establish the required MSD309 event/reset or raw-counter
semantics. Register numbering and the chip differ. None of its reset constants
or COMB output timing values are transferred into this model. No external
HSYNC, frame event, raw HTotal, mailbox response or initial OSD event is created.

## Connected result and validation

| Processor/activity | Result |
|---|---|
| MIPS | 854,795,171; `80501BFC`, invalid HTotal |
| TSP OpenRISC | 2,453,934,820 |
| DVB-T | 1,454,515; 161 IRQs; `4AE0/2200` |
| PM51 | 3,474; zero IRQs; `574F/0E6A` |
| AVD | 863; zero IRQs; `0702/11DB.7` |
| Audio DSP | zero instructions |
| Main timer entries | 38,589 |
| UART | unchanged |
| ZUI | init returns; dispatcher `804E0DC4` unentered |
| Graphics | no active OSD, created windows, enabled GWIN, proven framebuffer or raster output |

Default AVD still stops at `0802/C008`. The longer prefix and MIPS boot retain
all existing explicit PM/SPI/analog/reset/clock hypotheses. Strict unknowns
are unchanged. Published timing fields remain invalid because the original
publisher has not executed; raw sources and hardware event producers remain
unknown. There is no authentic image or PNG.

The [validation manifest](../evidence/htotal-window-validation.json) preserves
all native/regression results, original-function/fragment tests, input and
binary hashes, new cold-run analysis, and complete fresh-reset replay results.
The new trace adds only one read-only empty-window-initializer observation;
all other summary fields, MIPS coverage and UART match the previous checkpoint.

All **1,454 native checks**, 18 baseline runs, nine display runs, auxiliary MCU,
clock, VIF, AVD request, immutable-input and decoder regressions pass. Four
forensics tests and both CTest entries pass. The Windows package starts with
only System32 on PATH. Complete fresh-reset replay matches all **6,592,739**
new-path events, summaries, MIPS coverage and PM history. The earlier
6,592,734-event path also replays exactly with the final binary. The same final
binary produced and replayed the new cold trace. Original firmware and UART
hashes are unchanged; the manifest retains 21 validation reports.

Independent unresolved boundaries remain default AVD `0802/C008`, PM51
`574F/0E6A`, DVB-T `4AE0/2200`, strict POR `BF200A38/BF200A3C`, undocumented
Winbond command `CF`, and the audio DSP's architecture/`BF225AB4` acknowledgment
producer. Original audio timeout/recovery has already returned; it is not
reintroduced as the immediate UI blocker.
