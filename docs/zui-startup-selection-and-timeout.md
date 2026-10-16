# Original startup screen selection and the numeric-entry timeout

This continues `e91ec31`. No original firmware, processor engine, physical
input, HTotal sample, hardware completion or connected guest state is changed.
The production change is additional opt-in, read-only ZUI observations. The
original-function tests below execute separately with explicitly synthetic RAM.

## Connected startup is already selecting wrapper state 11

A complete fresh-reset run observes the following original RAM at the HTotal
boundary `80501BFC`, dispatch 854,795,171:

| Original field | Value |
|---|---:|
| ZUI state `806D7E98` | 1 |
| ActiveOsd `806D7E9C` | 0 |
| Wrapper state `806C0918` | **11** |
| Source phase `806D4A08` | **0** |
| Raw source ID `806F8F8C` | 20 |
| Software flags `806BEDF0` | 4 |
| Raw input byte `806B910C` | FF |
| Numeric-entry timestamp `806B9050` | **0, unarmed** |
| Software ticker `806E561C` | 38,589 |
| Queue, window count, window table | empty / 0 / null |

The same wrapper/source state is present at platform-poll entry and at the
source callback `8046B690`. At ZUI initialization the wrapper/source fields
were 0/0, source ID 37 and software flags 0. The original firmware therefore
performs a real startup selection after ZUI initialization; the host does not
need to invent a UI message to make that selection.

The original call/table chain for the observed selection is:

```
80488844 application loop
  8048885C -> 8046CA74 platform polling             [entered]
    ... source callback -> AVD -> 80501BFC          [unknown HTotal stop]
  80488864 -> 8047602C UI wrapper                  [not yet entered]
    original table 80618E38[11] -> 804762DC
      -> 80480CA8, source-phase worker
        phase 0, ActiveOsd != 3:
          80480CE0 -> 804C3684, argument 3
        phase 0, ActiveOsd == 3:
          80480DD4 -> 804C29E0, action 0105
    8047606C -> 804E0DC4 ZUI dispatcher
```

The remaining platform polling must return without changing this state for
that path to execute in the TV. The observer does not bypass it. This is a
specific original first-screen candidate supported by actual startup state
and isolated execution, **not proof that connected activation has occurred**.
Neither the screen's name nor its rectangle's panel-pixel interpretation is
inferred from the user's description of the real TV.

Original wrapper tests cover nine table/default cases. Fifteen complete
`80480CA8` tests cover phases 0..4 and ActiveOsd 0/3/17; they stop before
opening a screen or dispatching its action. The separate state-0 source worker
`80482DA8` is also tested: phase 0 enters configuration phase 3 without an OSD,
phase 3 calls `80481704`, and phase 1 can select OSD 3. That state-0 worker is
not the wrapper-11 path observed at the current connected stop.

## Correction: the previously mapped timeout is not a boot activation timer

The earlier [timing/resource report](htotal-provenance-and-zui-resources.md)
placed `80476B88` inside the UI wrapper. The actual containing function starts
at **80476A10**, after the wrapper has returned. Its direct caller is
`8046FBEC`, within `8046F960`, selected by wrapper state **3**.

The complete timeout gate is:

1. Per-source value at `806F8F8C + 12*source` is 20, 21 or 22.
2. ActiveOsd is already **17**.
3. Timestamp `806B9050` is nonzero.
4. Unsigned elapsed guest ticks are **at least 3001** (`SLTIU 0BB9`).
5. The original software flags include mask **4**.
6. `80476EA0` calls `8048848C`; otherwise the routine takes its other original
   workers or returns to ordinary source polling at `80476A84`.

`8048848C` performs source setup, resets the source-phase worker and wrapper
state, and queries ActiveOsd. With an OSD already active it sends original
action 2. Its alternative no-active-OSD branch can open OSD 3 when its
suppression byte is clear. Since the timeout gate requires OSD 17, the mere
existence of that alternative is **not an established first-boot trigger**.
Intervening callees' effects also cannot be assumed away.

The timestamp's original writer is `8046DA34` in input handler `8046D9DC`.
It stores the result of `803E6BCC`. If OSD 17 is inactive, that handler opens
OSD 17 first at `8046DAC0`, then arms the timestamp. Original key table
`80617D1C` routes key codes **10h..19h** to `8046E4DC`; that path calls the
handler when software mask 2 or 1 is set. The handler subtracts 10h from the
input and builds the numeric-entry state. This is evidence for a **numeric
input timeout**, rather than an automatically armed boot-logo deadline.
Neither the handler, its caller nor the timeout service executes in connected
boot at this checkpoint. No key is supplied by the host.

`8040B5D8` tests RAM flags, not PM/AVD hardware. `8040B5AC` ORs a supplied mask
into `806BEDF0`; `8040B5C0` clears it. Original source-selection jump table
`8061A250` routes source IDs 20/21 through `80488074`, which supplies mask 4
to the OR helper via `804873F0`. Those PCs and the OR helper are covered in
connected boot, consistent with the observed flags=4. Source ID 22 has a
different table entry; its name or supported connector is not guessed.

The real time source is the original timer handler **80607874**. It increments
`806E561C` at `80607894`. Startup `805CBCF0` registers that handler for vector
5; it and the handler execute in the connected trace. The getter chain is
`803E6BB0 -> 805CBBB0 -> 805CB6D4`; subtraction is modulo 2^32. These are guest
ticks under the existing clock/CPI experiment, not host wall-clock events.
Nothing in this timer path identifies an AVD event or HTotal producer.

The probe executes **1,620** complete original source-service prefixes across
source/OSD/timestamp/flags combinations and exact 3000/3001 boundaries, plus
**30** original elapsed-getter wraparound cases. It stops before selected
workers and executes no TV activation. These fixtures are not hardware-state
assumptions and are never options for connected execution.

## Resource selection and window-state creation

The original opener `804C3684` gets its ordinary RAM graphics-context state,
rejects ZUI state 0, and returns success without recreating an already-active
ID in state 2. Otherwise it clears focus, sets ZUI state 2, writes ActiveOsd,
clears the real queue, and selects a worker through `8061F140`.

There are 29 resource descriptors with 3,616 records, but **IDs 22, 23 and 28
select the original assertion loop at 804C3718**. They cannot be described as
29 successfully openable screens. The isolated opener test covers all 29 IDs
at worker/assertion entry, their already-active case and state-0 rejection:
87 cases, including three assertion selections. Synthetic context RAM supplies
an ordinary getter input; no graphics callback or hardware status is supplied.

For ID 3 the original call is `804C37F0 -> 804AFDFC`. Running that original
worker to its own surface entry **8049DFC4** establishes, without fixture
writes to the resource selection globals:

- Count 113 and original window table `806798B4`.
- Original secondary/rectangle/resource pointers selected by original getters.
- Original visibility/disable bitmaps and rectangle-state initialization.
- A stack rectangle passed to the surface: `(3, 520, 1360, 248)`.

This takes 14,227 original instructions in the **isolated worker test**, not
14,227 additional connected-TV instructions. The test stops before allocation,
graphics callbacks, GE commands, GOP enable or scan-out. The prior 29-resource
window-state tests remain intact. Connected initialization still deliberately
uses count zero; the opener has not executed there.

The next original instructions also explain why an empty event queue need not
prevent painting once a screen exists. All 113 original ID 3 window records
pass the genuine direct-message constructor and dispatcher, routing message
`14h` to their own control procedure IDs. These isolated routing tests stop at
`804C2DAC`, before executing the individual procedures.

For the root, the separately executed original creation path is:

```
804A071C -> 804A0640 -> 804C2DAC
  original procedure table, class 0 -> 804C31A0
  -> 804A2FD8 -> 8049C9AC, message 14h
  -> 8049F78C -> 8049F214 rectangle getter
  -> 8049F48C invalidation accumulator
  -> writes dirty flag 806D4FB4 = 1

804E0DC4, ZUI state 2 and an empty message queue
  -> 8049F040 returns empty
  -> 8049F180 consumes the dirty region and clears its flag
  -> 804A2FF4 paint worker [test stops BEFORE this worker]
```

Root creation takes 198 original instructions; dispatcher-to-paint-boundary
takes 82 in these isolated fixtures. No queue message, key or backend callback
is injected. The original screen-creation worker supplies message `14h` itself
after successful surface creation. Its surface call is still the earlier
unexecuted step in connected boot. No connected drawing or completed surface
is claimed from these tests. `8049F180` is the invalid-region paint service;
`804A0770` services the eight software timers and sends message `64h` when due.
Calling both loosely "idle/timer workers" obscures the actual first-paint path.

Window 1's original procedure, `804AE6C8`, handles creation by calling
`8049EBEC` four times. Six isolated tests execute that actual creation and
timer service: the original code installs timers 0..3 for HWND 1 with period
100, leaves the other four slots unused, does not fire at 99 ticks, and selects
HWND 1 / message `64h` / timer ID 0 at 100 or 101 ticks, including ticker wrap.
The test stops at the original direct-message entry before its timed handler.
No timer, message or clock event is inserted into connected boot. These tests
identify an actual original software event producer after window creation;
they do not supply AVD line/field events or a physical HTotal counter.

## AVD and strict hardware boundaries

No producer of `11DB.7`, `11FC.0`, raw `113C/113D` or HTotal validity was
established. `11FC.2` clears private counters; it is distinct from bit 0's
measurement gate. The native event acknowledgment remains unsupported.
The original HTotal publisher at `0ABC` has not executed connected.

Additional static comparison of the two embedded AVD programs in the related
[Marlon AVD library](https://github.com/Toysoft/mstar/blob/5d447aa5b54b60ede27d5fda42d481efa4b10ff8/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libdrvAVD.a)
finds the same `11DB.7` coefficient-scaling consumer in both ADC and VIF
variants. This does not identify its producer or reset. The original Manta
`R7=DA` writer at `2B1A` uses the **single-byte** helper `02AA`; it does not
establish bit 7 in the next byte `11DB`. The additional original raw-counter
reader at `2203/2208` masks the high byte to three bits and adjusts private
accumulators; it does not generate the counter. No reference firmware or
reset value is substituted into the TV.

The original timer, source flags and screen-selection globals now have much
clearer provenance. Physical AVD measurements do not. Strict mode is unchanged;
all prior exploratory PM/SPI/analog/clock/AVD/GOP options remain explicit.

## Connected result and validation

| Processor/activity | Connected result |
|---|---|
| MIPS | 854,795,171; `80501BFC`, unknown HTotal |
| TSP OpenRISC | 2,453,934,820 |
| DVB-T | 1,454,515; 161 IRQs; `4AE0/2200` |
| PM51 | 3,474; zero IRQs; `574F/0E6A` |
| AVD | 863; zero IRQs; `0702/11DB.7` |
| Audio DSP | zero instructions |
| Main timer entries | 38,589 |
| UART | unchanged |
| ZUI dispatch / connected windows | not executed / zero |
| Enabled GWIN / genuine raster / pixels | none demonstrated |

There is no authentic image or PNG. Default AVD remains at `0802/C008`.
Strict POR, undocumented SPI CF, auxiliary hardware events and audio-DSP ISA/
acknowledgment remain unresolved independently. Audio timeout/recovery already
returned and is not the present UI blocker.

The [validation manifest](../evidence/zui-timeout-validation.json) preserves
20 reports, input/binary hashes, original-code region hashes and the original
OSD switch map. All **1,457 native checks**, 18 baseline runs, nine display
runs, auxiliary, clock, VIF, AVD, immutable-input and decoder suites pass.
Four forensics tests and both CTest entries pass. The packaged native window
starts with only System32 on PATH; it still reports no guest display.

Complete fresh-reset replay matches **6,592,741 events**, full summaries,
coverage and PM history. The cold trace adds two read-only observations versus
`e91ec31`. Every nonobserver trace byte, nonobserver summary field, original
MIPS coverage and raw UART byte matches that prior checkpoint. The binary
used for recording and replay is identical. Replay does not inject recorded
read values. No pixel/framebuffer milestone is inferred from these results.

The first parallel display-suite attempt failed while flushing a temporary
summary. Its cause was not established. The same suite and assertions passed
when rerun with a process-local temporary directory on D:. The manifest
records both log hashes and that retry; no expected result was weakened.
