# Original AVD polling after the HTotal comparison

This continues `86b2215` using isolated original instructions. Connected MIPS
still stops at `80501BFC`; no measurement or later status is supplied to it.
No production emulator binary changes in this test extension.

The connected register snapshot has `a2=0` at the comparison. The original
table at `806278EC` selects the normal `80501728` history path for mode 0.
Other entries select nominal programming totals before the same history logic.
These nominal constants are software configuration, not proof of the unknown
input measurement. Selecting a video mode does not eliminate the sample read.

For nine explicit RAM/sample fixtures, the extended probe starts at the actual
original getter call `8050170C` and executes through history/stability into the
subsequent polling code. These fixtures use mode 0, patch flags 0, accepted
total 1135, equal old history, counters 0/3/4, and raw samples 0/1135/2047.
**They are not asserted to be the connected TV's full configuration.**

In those cases original execution performs:

```
8050170C -> 805030E0, read published HTotal
  original history/comparison -> 80501758
  -> 80503A5C, SH BF206CA4 = 046F
  -> 805035AC, SB BF206BAD = configured byte 00
  80501824 -> 80503080, next status getter [test stops at entry]
```

The two writes are observed from original MIPS instructions and checked in
order, width and value. All unrelated MMIO is rejected by the fixture.
`BF206CA4` is a COMB **programming** total; it must not be confused with the
unresolved AVD input measurement or used to fill `BF206B8D/BF206B90`.
The prior interleaving and partial/unknown tests remain unchanged and passing.

The next getter reads `BF206B94` at **80503098** and extracts **bit 3**. A named
related Marlon `HAL_AVD_AFEC_GetCoChannelOn` is an exact 40-byte instruction
match after masking only three linked data-address immediate fields. The
[symbol comparison evidence](../evidence/htotal-next-boundary-symbol-match.json)
records the original/normalized hashes and reference archive provenance.
This supports the **AFEC co-channel detector status** identification, not a
reset value or an automatic false value with disconnected video. The subsequent
[publisher investigation](avd-cochannel-publisher.md) identifies original AVD
startup and recurring software writers for this shared byte, without supplying
their output to connected boot before those instructions execute.

Other patch-flag configurations can read AFEC status `BF206B98` earlier.
The original routine later consumes its `C000` mask and additional detector
fields. Their required values are not guessed. The isolated nine-case result
does not claim the next connected stop is definitively co-channel: connected
patch/configuration fields and intervening activity must first be established.

The original HTotal branch rejoins after finite work. The emulator's rejection
of an unknown measurement is still a real conservative boundary; finite
control flow is not permission to choose the branch or write a nominal sample.
No path is skipped, guest PC forced in the TV, or hardware completion fabricated.

The [validation extension](../evidence/htotal-next-boundary-validation.json)
preserves the new twice-repeated probe results and symbol-match evidence.
Production and other probe hashes match the preceding fully validated build;
1,457 native checks and the existing full suites/6,592,741-event replay remain
applicable to that identical production binary. Only the timing diagnostic and
its regression change. Original input hashes and invalid-data cases are intact.

The first-screen path remains wrapper 11/source phase 0 -> OSD 3 if the rest of
platform polling returns without changing state. Connected AVD remains at
`0702/11DB.7`; `11FC` event/ack and raw timing producers remain unresolved.
No connected ZUI dispatch, framebuffer enable, genuine raster drawing or OSD
pixels are demonstrated by these tests.
