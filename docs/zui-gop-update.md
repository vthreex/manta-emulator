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

996 native checks pass, including active-window reset preservation. The prior
build passed both CTest entries including native window creation. All18 baseline
and nine display runs pass;650M MIPS/1.8B TSP reported state, coverage and PM
history agree with the pre-filter run. Five clock-prefix replays, four forensics
tests and original PM51/DVB-T/AVD diagnostic regressions pass. Earlier full2B event replay remains evidence for its recorded binary;
it is not claimed as validation of the new GOP model. The corrected resource61
observer now recognizes actual RAM SHA256 at the original loader return, rather
than incorrectly treating a repurposed S0 end pointer as the resource ID.

No enabled GOP framebuffer, GE raster output or authentic TV pixels are proven.

## Real continuation after GOP update

The original guest consumed the modeled ACK and continued to808,027,989 MIPS
and2,451,551,300 original TSP instructions,36,236 main timer entries and53 DVB-T
interrupt entries. There were five normal update requests,42 force pulses and47
actual register-bank latches, covering238 bytes. No decoded enabled window or
raster output appeared. ZUI returned at804E0DBC, but its state/event loop804E0DC4
has not been entered.

The next stop805EE830/BF206230 was an original read-modify-write clearing bit6.
An independent EXT instruction between its LBU and ANDI was not recognized by
the proof detector. The related Marlon XC archive names the matching routine
MDrv_SC_set_fpll and HAL_SC_EnableFPLL uses the same6230 bit3/bit6 controls.
This identifies frame-PLL configuration, not a ready status or reset value.
The proof now handles a valid independent EXT only when its input is known and
not derived from the sampled MMIO byte. Unit execution of the actual EXT word
agrees with the proof; invalid-source, INS and invalid-range cases reject.
Unknown preserved6230 bits remain invalid. A new original cold boot is running
to validate this correction in application execution.

[Completed GOP continuation](../evidence/gop-first-continuation.json) and
compressed full summaries/coverage/history preserve the evidence. Global latch
scope and sync timing are still exploratory; no verified power marker or CP
response was generated.

## FPLL continuation and ADC dummy read

The independent EXT correction advanced the original application to808,041,401
MIPS dispatches at8054CD88/BF204AFC. Active-descriptor diagnostics remain empty.
This helper addresses bytes using RIU_base +2*index -(index&1). Its first LBU
is overwritten in the BEQ delay slot, before the separate second LBU/RMW.
The new guarded observation is restricted to the exact15 original words and
ADC aperture204A00..204BFF. It keeps the first sample INVALID; mutation of the
original delay slot or branch rejects the proof. Native execution with different
first samples demonstrates identical writes based on the independently reread
second byte. Read side effects and precise ADC field semantics remain unverified;
no input-presence, power-ready or completion response is generated.

A third cold boot is testing this continuation. Its private pre-bitmap executable
SHA256 is7bedb39c393ed94935ff656733b15a20c22235ce4e6c9fb6abc85d1ffd22086d.
A logging-only QMEM PC bitmap now avoids repeated set searches; instruction
fetch, decode, execution, data accesses and counters remain unchanged. Native
loop coverage verifies four first-visit records for ten actual instructions.
1002 native checks pass. Complete new-mode replay remains pending; no claim of
an authentic frame or exact physical clock/latch semantics is made.

## User's physical-TV boot reference

The owner reports Manta logo -> temporary lower source/resolution banner ->
moving NO SIGNAL text over blue or black. Their SCART/1920x1080 wording is an
example, not a verified source/timing combination or exact screenshot. Banner
duration, text motion, colors and positions remain unmeasured. Use this as an
observation to compare against genuine firmware output, never as instructions
to construct host-side artwork. The current stock JPEG copy/decoder failure
path and absent active framebuffer do not reproduce this observed boot yet.

## Audio mode preservation after ADC initialization

The ADC continuation reaches809,361,032 MIPS dispatches and2,453,934,820 original
TSP instructions,36,304 main timer entries and53 DVB-T interrupts. The next read
is804F9050/BF225DD8, logical sound-mode2DEC. The original caller804FE23C reads the
mode, ORs its argument, and invokes the existing setter. Its6 exact original
instructions and existing complete reader/setter SHA256 identities now guard the
additional preservation path. No value for the unknown mode bit is supplied.

The setter compares against10h/20h; already programmed low mode bits can prove
inequality independently of unknown bit5. PreservedBits now permits only an
equality/inequality predicate with a known differing bit. It never clears unknown
origins, changes a CPU value or selects a PC. Predicates dependent on unknown
fields still stop. Exhaustive byte-value pairs across five unknown masks and
original uncertain CI/GOP/TSP cases test this distinction. Old metadata-only
fixtures now apply their observed ANDI to simulated register values, matching
production CPU execution rather than leaving stale unmasked bits.

1022 native checks and both CTest entries pass. Full event/summary/coverage/PM
history replay passed through the809M ADC checkpoint, including the logging-only
PC bitmap; the newer audio preservation path is being cold-boot tested. No enabled
GOP descriptor or raster operation is proven. Literal stock NO SIGNAL/SCART/HDMI1
UTF16LE candidates were independently located in resource826; they are data
forensics, never host-rendered OSD or proof that the guest drew them.
