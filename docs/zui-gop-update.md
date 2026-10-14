# Original ZUI entry and GOP update boundary

## Demonstrated exploratory application execution

The original startup-image timeout/cleanup returns through8049C82C and the
application takes its own8046A3A0 failure branch to80469D14. The original
application call80469E38 reaches ZUI804E0D48 once. No function is invoked by the
host, guest instruction changed, or PC forced.

At747,047,384 original MIPS dispatches,2,207,628,796 original TSP instructions
and33,158 original main timer entries, execution stops at80538468. The preceding
halfword read isBF203FFC; ACK bit13 is unknown. PM remains suspended at574F,
DVB-T at4AE0, AVD at0802. This run uses named power-good, non-resume, CPI16 and
other existing exploratory configurations; it does not establish strict cold boot.

[State/coverage/history equivalence](../evidence/multimedia-map-cache-state-equivalence.json)
compares both completed747M runs before/after the TSP controller mapping cache.
All reported fields, coverage and PM history agree. This is not a full RAM snapshot
or an identical trace claim: logging policies differ. Original CPU instructions and
actual QMEM accesses remain active; the cache captures only controller descriptors
within a deterministic burst and invalidates before a controller write.

## Original GOP transaction

Original805383CC writes register1FFE with0100 using maskFF00, then clears that
upper byte.80538450 reads it;8053A02C returns ACK masks1000,2000,8000 or4000 by
GOP ID.80538468 repeats while the selected mask is nonzero. After100001 checks,
the original fallback pulses0200. Related Marlon GOP library symbols confirm a
register-write/ACK protocol but use a different0400 normal strobe. The T4 strobe
is taken from this firmware, not copied from Marlon.

The new opt-in --exploratory-gop-vsync-latch model accepts complete original
0100 pulses, marks the pending transaction busy, and transfers actual banked
shadow bytes on the next programmed output-sync edge. A complete0200 force pulse
transfers the same actual state without a sync wait. No reset ACK value is given;
software writes alone cannot set hardware-ready fields. Interrupt bits stay invalid.
Active descriptors are separate from later shadow writes; reset disables active
window enables. Native tests verify actual addressed register copies and geometry,
not only a status flag.

Global bank scope, output-VSYNC latch domain and force timing remain explicitly
unverified assumptions. The option requires guarded symbolic mode plus an explicit
display clock. It is not enabled in strict mode. A configured/latching window does
not itself prove valid framebuffer memory, raster output, or native display.

## Verification scope

989 native checks and both CTest entries passed on this build, including native
window creation. Baseline/display regressions and the real GOP continuation are
being rerun. Earlier full2B event replay remains evidence for its recorded binary;
it is not claimed as validation of the new GOP model. The corrected resource61
observer now recognizes actual RAM SHA256 at the original loader return, rather
than incorrectly treating a repurposed S0 end pointer as the resource ID.

No enabled GOP framebuffer, GE raster output or authentic TV pixels are proven.
