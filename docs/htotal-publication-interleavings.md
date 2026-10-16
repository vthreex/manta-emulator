# HTotal publication and sample interleavings

This extends `41622e6` with isolated original-instruction tests. No production
emulator code, connected processor, scheduler, peripheral, register value or
firmware is changed. It does not resolve the HTotal measurement producer.

The original AVD sampling/helper fragment stores low byte `11C7`, executes its
original intervening instructions, then stores high byte `11C8`. The MIPS
getter `805030E0` reads the candidate shared high lane `BF206B90` first and
low lane `BF206B8D` second. Those software sequences do not establish an atomic
hardware latch. The current candidate shared backing therefore exposes writes
in their actual order.

The expanded `avd_htotal_probe` executes two original processor engines:

- For 64 old/new synthetic sample pairs, the unchanged 8051 publication fragment
  runs and the unchanged MIPS getter executes after **each actual store**.
  Its first observation combines old high/new low; the second sees the new
  complete value. There are 128 getter invocations and 34 intermediate values
  unequal to either complete sample.
- For another 64 pairs, MIPS executes the original call at `8050170C`. After
  its high-byte read samples the old value, the actual 8051 fragment completes
  before the original low-byte read. Both engines execute unchanged original
  instructions; no return value or latch is substituted. The current register
  backing then contains the new complete sample, while the getter has seen old
  high/new low. There are again 34 mixed values.
- That second group continues through the **original MIPS history and stability
  code** to its own `80501758` rejoin. Tests verify all three history entries,
  acceptance counter and accepted-value field. A mixed value can reset the
  software stability counter; it is not silently filtered by host code.

The fixtures cover byte-boundary crossings and values 0, 1, 255, 256, 511,
1023, 1135 and 2047. These values are test inputs, **not TV measurements**.
Mode 8, old history and counter 4 are explicit isolated RAM fixtures for the
second group. They are never connected-machine settings. A getter observation
is accepted only when the fixture's actual published bytes are fully known.
All 276 prior partial/unknown publication cases remain invalid and tested.

The original 2,348 publication cases, 512 getter cases and 320 stability cases
remain. The additional fragments execute 6,400 instructions in these isolated
tests, not in connected AVD. They do not run the original reset-to-publisher
path, create an AVD event or increase the 863 connected-instruction milestone.

This demonstrates behavior under the **candidate shared wiring with no
undocumented latch**. It does not prove that physical MSD309 hardware lacks a
latch. Any future latch model needs independent evidence for its trigger,
ownership, lifetime and byte ordering; the getter's two loads alone cannot
justify inventing one. An atomic latch would still not produce a raw sample.

Additional static research inspected the related
[Amethyst AVD driver](https://github.com/neuschaefer/mstar-mboot/blob/962e8b8258378dded694883a9f9acb7058d34631/MstarCore/bsp/amethyst/lib/libdrvAVD.a).
Its named `HAL_AVD_AFEC_GetHTotal` also reads offsets `6B90` then `6B8D`, with
16-bit assembly and no hidden validity mask. Its embedded 13,312-byte AVD
program has a similar `11DB.7` scaling consumer. The chip and firmware differ;
no physical reset value, event source or counter behavior is transferred.
Neither that driver nor the earlier Marlon ADC/VIF images identifies the
required producer in this investigation.

The [validation report](../evidence/htotal-interleaving-validation.json) links
the repeatable new probe report to the unchanged emulator/firmware and the
preceding full validation manifest. The new probe runs twice identically,
rejects a damaged original image, and preserves the immutable input hash.
Production binary hashes are identical to `41622e6`, whose 1,457 native checks,
full suites and complete 6,592,741-event replay remain applicable. No duplicate
cold boot is needed for this probe-only change.

Connected state is unchanged: MIPS `80501BFC`, AVD `0702/11DB.7`, wrapper 11,
source phase 0, no active OSD, windows or genuine pixels. The original dirty
region/timer/paint path established in the preceding study remains an isolated
test result until the actual source-poll dependency is resolved.
