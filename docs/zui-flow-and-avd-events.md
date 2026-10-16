# Original ZUI state and AVD event-consumer boundary

The subsequent [timing/resource continuation](htotal-provenance-and-zui-resources.md)
observes the original zero-count window-state initializer and tests every
original OSD resource set. This page preserves the earlier four-observation
trace and its hypothesis scope.

This continues `9ac1691`. **No authentic OSD pixels are demonstrated.**
Read-only probes now establish the actual UI globals in a full conditional
cold run. They do not change any previous CPU/peripheral summary field,
MIPS coverage, UART byte or hardware input. Original AVD remains at 863
instructions and MIPS remains at `80501BFC` on invalid HTotal.

## ZUI initialization leaves an inactive UI

`--zui-flow-probes` checks hashes of the original initializer/dispatcher and
queue code before reading bounded RAM locations. Uncertain bytes are reported
as null, never made known. It neither reads hardware nor follows guest resource
pointers. It counts watched entries and retains one snapshot per site; it
cannot invoke a guest function, post a message or modify a CPU register.

The completed cold execution observes:

| Original instruction boundary | State | Active OSD | Head/tail | Occupied handle slots | Windows/list |
|---|---:|---:|---|---:|---|
| `804E0D48`, initializer entry | 0 | 0 | 0/0 | 40 raw slots | 0/0 |
| `8049F0DC`, before queue clear | 1 | 0 | 0/0 | 40 raw slots | 0/0 |
| `804E0DBC`, initializer return | 1 | 0 | 0/0 | 0 | 0/0 |
| `8046CA74`, platform polling | 1 | 0 | 0/0 | 0 | 0/0 |

Before queue initialization, zero handle bytes are merely raw storage: they
are **not 40 pending events**. The original clearer invalidates each handle
with `FFFF`. After initialization, all 40 handle slots are empty.

The original state word is at `806D7E98`, previous state at `806D7EA0`, and
active OSD at `806D7E9C`. The original dispatcher `804E0DC4` does no message
handling for state 1. State 2 drains messages and runs timers/idle processing;
states 3/4 run transition-effect paths, and state 5 shuts the OSD down.
Related MStar source names these standby/running/transition/terminate, matching
the original state machine. These names do not imply that the whole TV is in
electrical standby.

The application's actual order is:

```
8048885C -> 8046CA74   platform/peripheral polling, entered
              ... -> 805016C8 -> 80501BFC, invalid HTotal comparison
80488864 -> 8047602C   UI wrapper, not reached
8047606C -> 804E0DC4   original UI dispatcher, not reached
```

Original `804C3684` opens an OSD state. Its stores at `804C3700/804C3708`
select state 2 and the requested OSD, then original `8049F0DC` clears the queue
before resource-specific work. All 82 direct callers of that opener are absent
from this cold run's coverage. No opener, window allocation or drawing call
is invoked by the host. The evidence narrows the current problem: initialization
has not selected a live OSD, and the platform polling call prevents reaching
the UI wrapper. Resolving HTotal alone is not proof that an OSD will open.

An additional static lead identifies OSD ID 3: its original jump-table case
`804C37F0` calls `804AFDFC`, which obtains 113 window records from the original
descriptor at `806860C0`, including table pointers `806798B4/8067A188`. The
descriptor and original instructions both supply rectangle `(3,520,1360,248)`
to candidate surface setup `804AFEAC -> 8049DFC4`. Several original source-state
paths call the opener with ID 3. This offers a concrete resource/surface path
to follow after polling returns. Its screen name and coordinate relationship
to the panel are not established here. None of these calls has executed;
the descriptor is compressed-program data recovered by guest decompression,
not an allocated framebuffer or a rendered bottom bar.

## Original-instruction queue and dispatcher tests

The new `zui_flow_probe` loads the hash-verified original MIPS program into the
existing CPU engine. This is an isolated function-test fixture, not TV boot.
Synthetic RAM messages are confined to that fixture and never fed to the TV.

Original `8049F160` initializes all 40 twelve-byte queue slots, eight timer
handles and the timing baseline through the original timer getter. Original
`8049F040` is exercised at every queue index: it copies the full message,
invalidates only its handle, advances modulo 40 and returns false without
inventing data when empty. Tests also execute original dispatcher returns for
states 0/1/6/FFFFFFFF and original branches to the first real worker for states
2/3/4/5. Workers are observation boundaries, with no substitute return or
fabricated completion. The original instruction image remains byte-identical.

This verifies original software semantics; it does **not** establish connected
dispatcher execution, a legitimate UI event, framebuffer allocation or pixels.
The separate strict-boot regression proves enabling the observer changes no
CPU/peripheral summary field apart from the empty observer's own diagnostics.

## AVD event writes must not create events

Native `11DB.7` still has no identified producer/reset. Its original branch
at `0702` selects optional ROM-coefficient scaling. No value is supplied in
connected execution. Neither the original 78-entry MIPS initialization table
nor observed auxiliary stores defines it.

Isolated original-reset execution can supply either synthetic coefficient
flag and one explicitly synthetic `11FC` input byte. All 512 combinations are
tested. Original `0993` tests captured event bit 2; if set, `0996..0999` clears
IRAM `62` and `30`. The difference is exactly three original instructions.
Before any later event-bit branch, original `099B..09A1` complements the
captured byte and calls `02AA`; `02AD` attempts the actual MOVX to `11FC`.

That operation resembles an event acknowledgment. A plain register store of
the complement would turn clear event bits into asserted events. No clearing,
write-zero-to-clear, mask, priority or latch semantics have been established.
The AVD bus now rejects the store before changing shared backing. Overlapping
MIPS stores to candidate physical `1F206BF8` are also rejected when the AVD
program interface is selected. This is a conservative unsupported-device
boundary, **not an implementation of event acknowledgment**.

Both input flag outcomes and every event byte therefore stop at original
`02AD`, attempting `~captured_status`: 2,293 instructions for flag 0/bit2 clear,
plus 31 if coefficient scaling is selected and plus three if event bit2 is set.
No pending event, host request, interrupt or HTotal output is generated. These
fixtures are unavailable as options in the TV executable. Connected execution
has not reached `0993` and receives no synthetic flag or event byte.

## HTotal and remaining physical evidence

Captured `11FC.0` later gates the path that reads `113C/113D`, masks the high
byte to three bits, and calls `076E` from `0ABC`. Actual stores `0775/0784`
publish to `11C7/11C8`. Under the candidate shared mapping these correspond
to `BF206B8D/BF206B90`, read high then low by original MIPS. The MCU writes
low then high; an undocumented atomic latch is not assumed. The mapping is
correlated software evidence, not newly verified electrical register wiring.

The raw counter clock, latch, reset, no-HSYNC behavior, native event producers
and acknowledgment semantics remain unknown. Timer0's original ISR decrements
IRAM `78`, reloads its timer and increments private `8077`; it is not an
identified producer of `11FC` or HTotal. Connected AVD has zero interrupts,
and the HTotal publisher has not executed. No panel timing, PAL/NTSC constant,
zero measurement or synthetic sync event is substituted.

## What does ZUI mean here?

No authoritative expansion of MStar's acronym was found. It should not be
expanded to "zoomable user interface" as a firmware fact.

The related source implements a conventional TV window/message toolkit.
Its [zoom-effect control](https://github.com/Toysoft/mstar/blob/5d447aa5b54b60ede27d5fda42d481efa4b10ff8/Chakra3_017a0d6_20170731/tv-ap/common/zui/MApp_ZUI_CTLeffectzoom.c)
uses framebuffer region copies/scaling for window show/hide animations, and
its drawing code includes enlarged text. This supports zoom **effects** in
that toolkit, not a zoomable-canvas navigation model or proof that this Manta
build enables those effects. The related [main state machine](https://github.com/Toysoft/mstar/blob/5d447aa5b54b60ede27d5fda42d481efa4b10ff8/Chakra3_017a0d6_20170731/tv-ap/dvb/ui2/MApp_ZUI_Main.c)
helps interpret names; actual addresses, state observations and tests above
come from the original Manta binary. The later source targets a different
platform and is not imported as replacement firmware or graphics resources.

## Validation and current connected result

See the [validation manifest](../evidence/zui-flow-validation.json) for binary
and input hashes, original-function tests, event fixtures, all regression
reports, cold state/coverage comparison and complete replay results.
All 1,447 native checks, 18 baseline runs, nine display runs, auxiliary/clock/
VIF/request suites, four forensics tests and System32-only-PATH Windows startup
pass. Complete fresh-reset replays match all 6,592,738 new-path and 6,592,734
previous-path events, full summaries, MIPS coverage and PM histories. The new
trace contains exactly four additional read-only UI observations. The final
binary also includes the unsupported event-store guards; their addition leaves
both connected paths identical. Original firmware and UART hashes match.

| Processor/activity | Connected result |
|---|---|
| MIPS | 854,795,171; `80501BFC`, invalid HTotal |
| TSP OpenRISC | 2,453,934,820 |
| DVB-T | 1,454,515; 161 IRQs; `4AE0/2200` |
| PM51 | 3,474; zero IRQs; `574F/0E6A` |
| AVD | 863; zero IRQs; `0702/11DB.7` |
| Audio DSP | zero instructions |
| Main timer entries | 38,589 |
| UART | byte-identical to previous checkpoint |
| OSD | no dispatcher, enabled GWIN, proven framebuffer, raster output or PNG |

Default AVD startup still stops at `0802/C008` after six instructions. The
863-instruction connected prefix requires the existing deferred startup-effect,
shared mapping/clock and symbolic CODE-data diagnostics. It passes `0802` only
under those explicitly recorded assumptions. Connected `0702`/`0993`/`0ABC`
have not executed, native HTotal bytes remain invalid, and original MIPS has
not passed its `80501BFC` comparison. Startup-selector producers are unchanged
from the [previous CODE-data study](avd-symbolic-code-tables.md).

Strict mode gains no reset value, analog input, ready byte or event producer.
Existing PM, SPI, analog, AVD startup/reset and GOP timing assumptions remain
separately opt-in. The observer adds four read-only trace events; all other
summary fields and MIPS coverage match the previous checkpoint exactly.
