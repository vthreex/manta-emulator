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

## Disabled GWIN and guarded observation debugger

The audio-mode continuation reached813,164,852 original MIPS dispatches. A full
event replay with the corrected descriptor decoder preserved every existing
summary field, coverage and PM history. The additional diagnostics decode the
original bank4/window0 setup as **disabled**,8x8, pitch16, format6, MIU-relative
address00DDD800. Original80538B78/80538D44 program vertical end at offset20h;
the earlier decoder incorrectly used1Ch. MIU selection, allocation ownership,
pixel contents and scan-out remain unproven. This is not an authentic frame.

`--opaque-read-policy` is a guarded debugger facility for INVALID observations.
Each row contains only original PC, physical address, width, original code span
and SHA256. It cannot supply a value, known mask, reply, predicate or PC. The
actual instruction and effective address are checked; original bus access occurs
once. Optional debugger waits freeze all virtual clocks. Bit provenance still
rejects unknown decisions, addresses and transfers to another hardware field.
The final accepted rule sequence has a deterministic digest, independent of host
file-update timing. Strict mode rejects this facility. Unverified read side
effects remain an explicit limitation of the exploratory observations.

An audit found a real safety defect in RMW lookahead: an unrelated saved unknown
field could be treated as a concrete AND/OR operand. At80504548, a saved high
nibble and freshly sampled low nibble ofBF206BB1 incorrectly preserved maskF0
instead ofFE, validating bits1..3. The815,025,640-dispatch policy run is therefore
**bug evidence only** after tick815,025,549. It must not be used as a successful
boot or replay reference. The proof now rejects unrelated invalid operands; both
observations retain their own exact bit identities. Native tests verify that
the mixed restore leaves only the originally known bit0 valid. Corrected cold
boots and replay are required before claiming subsequent progress.

## AFEC status investigation

The next investigated HAL at80503000 has the same complete instruction body as
`HAL_AVD_AFEC_GetStatus` in the related
[MStar AVD archive](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libdrvAVD.a),
apart from relocated global addresses. It readsBF206B98. Original80501698 extracts
bit14, matching `MDrv_AVD_IsSyncLocked`; the
[driver interface](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/include/drvAVD.h)
names it `VD_HSYNC_LOCKED` and separately names bit0 `VD_STATUS_RDY`.
Original803E3018 polls the extracted bit up to ten times with20ms delays.

`--exploratory-avd-no-hsync` is a separate, explicit disconnected-input
hypothesis. Only bit14 is supplied low; READY and all other unimplemented fields
remain invalid. Software writes cannot define the live lock bit. Byte/halfword
addressing and removal of the input hypothesis preserve uncertainty. This does
not validate detector clock gating, forced-sync controls or acquisition latency,
and is unavailable without guarded/disconnected-input options. Native tests
execute both original HAL and caller, not a host replacement. No AVD MCU ready
reply, audio DSP acknowledgment, framebuffer or OSD is generated by this option.

## Corrected runs and VIF continuation

The corrected no-HSYNC-unknown run reaches815,025,648 MIPS dispatches and stops
at the actual803E3020 BEQ: unknown AFEC bit14 cannot choose the branch.
The mixed restore at80504548 now emits invalid maskFE, leaving only bit0 known.
[Full replay](../evidence/gop-validity-full-replay.json.gz) checks every event,
the complete summary, coverage and PM history with the same accepted policies.

With the separate absent-HSYNC option, the original ten20ms polls all execute
and return naturally. The run reaches819,978,158 MIPS dispatches,2,453,934,820
OpenRISC instructions and36,840 original main timer entries, then reads
BF003D9D at8050A5F0. Its [full replay](../evidence/gop-avd-full-replay.json.gz)
passes with every event and all reported state identical. Original ZUI still
has one initialization entry/return, without entry to804E0DC4 or enabled GWIN.

The [Titania4 header](https://github.com/Benjamin-Dobell/kogan-tv-gpl/blob/master/Kernel_updated/mstar/mips-boards/titania4/hwreg_S5.h)
names logical1ECF `REG_CHIP_REVISION`; byte addressing2*index-(index&1) gives
3D9D. Original8050A598 is VIF ADC initialization. Revision0 skips an extra
bit0-clear at the VIF global base+3C71; nonzero performs it. Both paths rejoin
8050A600. The related VIF archive is structurally different and does not supply
the real Manta revision. Native tests execute both original branch paths and
their actual masked control write. `--exploratory-vif-chip-revision-byte` keeps
this input separate from the earlier3C05 fetch-workaround experiment; no default
or readiness response is supplied. An overlapping write to the configured
immutable input rejects.

The explicit revision0 scenario reaches820,110,296 MIPS dispatches and36,846
timer entries, observing a new original `DVB-T Load DSP Code OK` message after
audio recovery. Its next read805FD41C/BF201751 saves the old clock byte; the next
instruction sequence savesBF201754. The complete routine805FD3D4 matches the
structure of named `Hal_SC_ip_software_reset`, temporarily using clock field3C
and restoring the original fields. These are configuration snapshots, not a
ready poll. Further INVALID-only observation rules retain both old snapshots and
fresh restore samples; no reset value or clock-ready transition is invented.
The extended cold run and revision0 replay remain pending at this documentation
checkpoint.

Native1087 checks and both CTest entries pass. The stricter proof intentionally
moves the unqualified COMB regression stop from the eventual SH at805F883C to
its preceding LHU at805F8824; without an observation policy, it now rejects
earlier instead of treating another invalid operand as a constant. The display
suite checks that earlier exact instruction/address and still replays every
event. Earlier records made with the old proof remain historical reproducibility
evidence, not validation of the corrected provenance semantics.

Replay requires the observation policy used by that run. Adding a future grant
at the recorded terminal read changes the experiment and must not be called a
matching replay. Preserve a separate policy snapshot per checkpoint; host waits
are not part of virtual time. No original firmware assets are distributed with
these metadata policies or reports.
