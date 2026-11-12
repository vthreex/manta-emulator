# Manta ATV / teletext handoff

## Current counter fix validation (2026-11-06)

7dd85a1 committed the candidate TSP transport/interrupt path after full checks.
Additional uncommitted generic 8051 fix: known direct DJNZ counters may execute
while ACC is opaque. Original MCU0376 was incorrectly stopped solely because
ACC held partially unknown measurement data, although B itself was known.
New isolated regression FAILED before the fix; 4,478 native checks now PASS.
Tests cover IRAM/B/DPL/DPH, both symbolic modes, two poison values, and the
later invalid-ACC branch plus unknown-B counter rejection. No unknown counter
or measurement is made known. Also split the new TSP clock statement to remove
its misleading-indentation warning; no timing semantics changed.

The 213D bit0 brackets both reached0376 with unknown21B8/B9/BA. Original
3471/19C7 selects21B6=5 and freezes2105.7 before those reads; 525A selects2
and reads a shorter value. Treat this as a selected measurement, not a fixed
RF lock bit. Separate all-zero/all-one measurement diagnostics both reached
FEC3 then3D7E reading245C.0; they do NOT establish real detector output or lock.
No such measurement values have been added to the RF model/defaults.

Full validation is running again via work/dtv_validate.py (DO NOT build while
it runs), along with fresh ATV/TTX compatibility at work/dtv-ttx-counter-compat
and copied-native diagnostic ...-213d0-counter-init00. Read live processes and
reports. The previous full 4,445-check build is recorded in the section below.

## Active DTV checkpoint (2026-11-06, supersedes older active-process notes)

Current uncommitted build passes 4,445 native checks and the original TSP
packet/DMA instruction probe. ATV/teletext compatibility also passes to 2B
dispatches for both body-text fixtures, covering all 2,073,600 output pixels.
No DTV website deployment has been made; native channel discovery and decoded
video are still unproven. Preserve unrelated Sunplus edits/untracked files.

The previous connected run assembled 5,304 actual section bytes, checked CRC
with the original TSP code, and posted AEON notification 80010000. Ring0 then
filled because the notification interrupt was missing. The candidate now wires
TSP2AEON source47 through its original local SW_INT enable and CP mask3290;
original getters and the Titania4 IRQ map corroborate the routing. Tiny ISA
fixtures verify actual interrupt entry, pending status and deassertion.

New --exploratory-tsp-packet-cycles explicitly paces transmitter arrivals;
busy receiver slots drop arrivals and advance the source, rather than pausing
the transmitter. Idle-skip on/off tests compare CPU and serialized input state
across 201 arrival slots. Timing remains explicitly uncalibrated. RF24CF now
exposes the fixed transmitter's 8K/1:32 mode fields, corroborated by original
2E6B/2E72 and the public demodulator enum. No software FEC state is supplied.

Completed copied-native 64-packets-irq-paced and diagnostic
64-packets-xramzero-timer-irq-paced-init00: both reach 2,150,000,003 MIPS,
238 real packets/5,278 section bytes, no input overruns or CP validity stop,
original AEON notification acknowledged to zero. Original MCU baseline still
stops0637; startup-hypothesis run now reaches FEC6 then3443 reading213D bit0.
Final original GOP still shows NO SIGNAL; channel discovery is NOT proven.
Full validation finished: all suites pass, 6,592,741-event strict replay has
289 identical summary fields and identical coverage. Latest compatibility
report: work/dtv-ttx-irq-paced-compat.json. Evidence is recorded separately from
private TS/firmware inputs in evidence/dtv-tsp-packets-validation.json.

Currently running: all-one TOP reset bracket
64-packets-xramzero-timer-irq-paced-initff; plus two additional original213D.0
startup brackets ...-213d0-init00 / ...-213d1-init00 via the private run helper.
These use +init at physical1F224279, never an RF-owned forced lock response.
Original3434/2773 both read213D.0 to choose21B0/B4 programming41/54 vs71/64;
hardware meaning/reset value unresolved. Read live process/results rather than
relying on the older session IDs below. Validation is finished, so builds are
allowed; active native probes use copied binaries.

## Latest DTV continuation (2026-11-05/06)

NEWEST WORK IN PROGRESS, NOT COMMITTED: integrated --candidate-tsp-packets
now delivers actual TS through native PID routing. First connected3 runs
work/dtv-rf-clock-64-packets-{rmw,init00,initff} all reached1438025221 MIPS,
one actual packet delivered, TSP stopped714 shifting partial status. Original
descriptor was FULLY KNOWN (filter0,cursor5,MaskFFFFFF00,Cur82F40000,CRCunknown),
so the issue was OpenRisc rejecting partial-source shifts, NOT uninitialized
filters. Generic immediate/register shifts now propagate masks/sign extension
and drop permuted RMW identity; exhaustive low8+sign fixtures added. Original
710/714/71C positive-match path is tested on the existing original core.

DMA now retains descriptor cached aliases (82F4...) for original arithmetic,
but decodes actual MIU writes into physical RAM (02F4...). Original428 tests
linear+wrap under BOTH physical/cached pointers and two-fragment CRC. Added
candidate notification aliases: TSP21032C->1F202AE0/AE4 (HK),2130DC->
1F202DB4/DB8(AEON); static MIPS805D69D0/69A8 and CPA33D0/A33EC agree.
IRQ output to CP is NOT wired yet. Candidate summary now reports actual
notification word and CP3290 mask to establish which IRQ was enabled.

work/dtv-rf-clock-64-packets-xramzero-init00 used NEW explicit
--exploratory-vd-xram-poweron-zero + existing TOP2073..75 +init00 hypothesis.
No defaults changed; this is NOT exact hardware evidence. Original MCU got
to FEC state2, then506E waiting23F9. Reads/writes prove a timeout counter:
16AB sets F2/F3 count,16B3 writes F0=40 then1;0 stops. NEW candidate
--exploratory-dvbt-timer-divider N models elapsed guest cycles, divider is
explicitly uncalibrated. Riu MCU-step clock scope + optional snapshot section
implemented; tests exercise deadline/reset/stop/no read-triggered completion.

CURRENT BUILD session96543 was at29/31 (core compile pending), no suites or
native runs active. After it ends, ensure latest core_tests edit is rebuilt,
run core+tsp_packet_probe (new positive-path fixture previously needed a
write callback; fixed), launch new packet baseline and
TAG-packets-xramzero-timer-init00 via work/dtv_default_clock.py64 TAG. Work
script adds timer divisor4096 when tag contains timer. Existing unknown
TOP reset values remain separate00/FF diagnostics, never RF-owned defaults.
Last complete full validation was4011-check PRE-shift/timer build: all suites
+6592741-event replay289fields/coverage identical. Latest changes need full
validation before commit. DO NOT ninja during work/dtv_validate.py probes.

CURRENT ACTIVE WORK (new turn, uncommitted): MOV Rn,direct now retains masks
and origins across all4 register banks.3999-check build passed ALL required
suites/replay; cold work/dtv-rf-clock-64-copy passed22CE then stopped0637 ADD
A,R3, with unknown2074 source. Do not assign those bytes to the RF model.

TSP candidate now implemented in src/peripherals/tsp_packet_window.hpp,
tsp_section_dma.hpp, tsp_transport.hpp, attached with --candidate-tsp-packets.
Original FF8/3C4/1004 read6 real byte offsets; original428 checks actual DMA,
wrap and two-fragment MPEG CRC (tsp_packet_probe PASS). New core integration
tests4011 PASS, including MIPS indirect/live packet sharing, routing, snapshots
and unknown/unsupported bounds. Input is UNPACED post-FEC pin stimulus from
configured station bytes, not measured RF timing. Never writes MCU FEC state,
never parses service metadata. Optional snapshot section checks input hashes.

First connected packet probe work/dtv-rf-clock-64-packets stopped194857124 at
original TSP1E14: startup RMWs release bit4 while reserved bits stay unknown.
Latest UNBUILT fix admits ONLY same-origin218 update with known mask10, plus
ordinary fully-known10/0 release commands. Original startup fixture covers
both opaque poison values. Full validation driver for PRE-fix build is still
running (session37504); do not ninja until it ends. Then build, run core and
tsp_packet_probe, launch new cold baseline and separate2073..75 startup00/FF
diagnostic brackets via work/dtv_default_clock.py64 TAG-packets-init00/initff.
No reset hypothesis has been added to code defaults. See docs/dtv-tsp-transport.md.

2073 OWNERSHIP CORRECTION: it has an original MCU writer!1511 sets DPTR2073,
1514 MOV A,R7,1515 calls16F5 (MOVX @DPTR,A). This path is gated by FEC state9
at14BA. Early initialization1E3D reads2073 through2375, then22BB copies the
24-bit2073/74/75 value via integer/floating helpers. Its initial/reset contents
are still unknown; it is NOT safe to call it a silicon-only lock observation
or to make the RF model own it. Keep unknown, inspect original init tables or
exact register docs. Static proof now pins this writer and reader. Pure
MOV Rn,direct tracking at22CE is still conservative/unimplemented for invalid
source; admitting the copy alone will not establish2073's missing value.

Verified continuation commits:5aa8a86 (tuner/parameter/save-state plumbing),
25a741d (XRAM reset retention + first-visit trace formatting). All code changes
are committed; only unrelated pre-existing Sunplus edits remain. New static
ownership anchors pass against original unchanged firmware. No active runs.

LATEST COMPLETED RESULT: reset retention cold probe work/dtv-rf-clock-64-retain
completed2150000003 MIPS. Original MCU passes old0368 carry boundary, writes
FEC23E0=0 ITSELF, then stops22CE MOV R2,06 on invalid data from hardware2073
(read at2375). Parameters8013/8014 no longer appear as invalid sources.
No reception claim: FEC0 is unlocked, zero packets delivered. Next investigate
pure IRAM byte-copy tracking and ownership/meaning of2073; never set it to0
just to pass arithmetic. Formatting-only paired cold runs matched ALL364
summary fields and their one exported frame exactly.

Reset-fix final verification COMPLETE:3967 native checks, baseline18/display9,
auxiliary/clock/forensics/diagnostics/snapshot/clean-PATH suites, full6592741-
event replay (289 baseline fields+coverage identical), fresh ATV/TTX PASS.
evidence/dtv-mcu-reset-retention-validation.json records this; no active probes
or suite drivers remain. No website deployment. Unrelated Sunplus files intact.

NEWEST ROOT CAUSE/FIX (uncommitted, after5aa8a86): divider64 old/filtered
formatting cold runs both completed2.15B; ALL summary fields identical. Both
still hit0368 with invalid8013/8014, refuting clock timeout as the whole cause.
Found host bug: Riu reset release cleared vdPrivateRam_ every time. Original
MIPS configuration804F4B5C calls CPU reset helper804F4894 AFTER parameter init,
without rewriting the full table. Native ISR regression now writes parameter13,
resets through actual board control, and reads via command3. It FAILED before
removing host XRAM clear, PASSES after. Preserve bytes AND masks across CPU
reset; fresh board remains invalid. Do not inject parameter values or defaults.
Static proof now pins original config/loader calls and reset writes.

3967 core checks and13-reply original-MCU probe PASS. Full required suites and
strict replay running AGAIN for reset repair (work/dtv_validate.py). Native
cold probe work/dtv-rf-clock-64-retain is running with divider64/actual TS;
script work/dtv_default_clock.py64 64-retain. Inspect connected outcome before
claiming acquisition progress. No packets/service discovery/video yet.

AFTER COMMIT5aa8a86: default-divider4 cold probe completed at556784827 MIPS
with VD instruction budget1B exhausted, BEFORE DTV selection; MCU still runs,
PC4ECC, no validity boundary. This is a BUDGET result, not a reception result
or proof/refutation of the mailbox-timeout explanation. Divider1024 is too
slow to trust short mailbox polling; next paired cold probes use divider64.
work/dtv-rf-clock-64 = pre-optimization, work/dtv-rf-clock-64-fast = new copy.
Both run actual TS fixture and original firmware, max2.15B MIPS/1B VD.

New UNCOMMITTED logging-only optimization: instructionTextAt pure observation
predicate avoids sprintf/disassembly at already-visited VD PCs, whose events
were already discarded. CPU execution, rollback, validity, ticks and interrupts
are unchanged. Original-ISR snapshot test compares filtered formatting versus
full formatting with byte-identical serialized device result.3967 core checks
and12-reply original ISR probe PASS. Full required suites/replay running again
(work/dtv_validate.py); do NOT ninja while it runs. Compare paired connected
summaries/frames before committing performance claim. No DTV deployment.

NEWEST UPDATE (supersedes pending-build notes below):3967 native checks PASS.
Fresh callback-fix verification is COMPLETE: baseline18, display9, auxiliary,
clock, forensics, clean-PATH startup, diagnostics and snapshots PASS. Strict
replay6592741 events matches289 reference fields plus coverage exactly.
ATV/teletext was rerun on the new native binary: both payloads remain distinct,
full2073600-pixel GOP extent, no GE stop through2B. Consolidated evidence:
evidence/dtv-tuner-mcu-validation.json. Only connected default-clock probe
remains running; no build/suite driver is active now.
Original-ISR probe now tests12 replies: invalid register/private-RAM transport,
full Riu save/restore equivalence, and actual DSP parameter commands4/3.
Snapshot restore DID omit VD callbacks; bindVdCallbacks now reinstalls them
without resetting PC/registers. Continuous/restored transactions leave every
serialized device byte identical. Default probe remains unchanged.

The original MIPS initializer804F2FD0 writes46 parameter bytes from806A4950
through804F2F14, command4. MCU4AF5/5469 maps DSP index to8000+index; command3
reads it. Static proof pins these instructions. The native MIPS reply loop
has only255 iterations and no delay. Divider1024 may timeout before servicing
the original ISR and overwrite pending requests; DO NOT interpret resulting
unknown private parameters as hardware reset values. New cold native probe
work/dtv-rf-default-clock uses existing divider4 with the SAME RF fixture and
diagnostic assumptions, script work/dtv_default_clock.py. Inspect its summary
and last invalid reads when finished. Active process uses a copied executable.

RF match682/mismatch683 probes at divider1024 BOTH completed2.15B with MCU0368
unknown carry, no acquisition, no packets. Last invalid sources include8013,
8014 and other configuration bytes. Their evidence does not validate RF lock.
All suites + full6592741-event replay passed for3967-check RF/history build
BEFORE callback repair. Fresh full validation is running again for callback
repair and12-reply probe; do NOT ninja while it executes probes. No DTV push
or deployment has happened. Prior ATV/TTX compatibility remains passing.

LATEST WORK IN PROGRESS (after d380414):3957 core checks + all required suites
and full6,592,741-event replay PASS on the field-packing/PLL decoder build.
ATV/teletext compatibility PASS with two original decoded payloads/full-screen
2073600-pixel GOP extent (work/dtv-atv-compatibility.json). Original native DTV
probe work/dtv-native-raster reaches2.6B, tuner682MHz/bandwidth8 derived from
real GPIO frame10 D5 90 14 C0. MCU now stops0368 on unknown carry used by JC
in a generic arithmetic helper (not safe to choose an outcome). Last source
origin was lost through rotations; new bounded dvbt_invalid_mcu_reads history
will retain actual unknown XDATA reads so the missing measurement/config can
be identified. Do NOT simply zero the carry or skip the helper.

Original MIPS804F58C8..804F5A38 constructs digital PLL: double36.167 at80625748,
double6.0 at80625750, CB90/ABC0; divide4309 maps681.999667MHz (CH47). Original
analog804F5A3C emits CB93/ABF5,62.5kHz/38.9MHz. GpioI2c::pllTune now recognizes
these two complete frames only; partial/unknown modes stay unknown. A digital
frame no longer aliases an ATV station at230MHz. Native function SHA and pool
constants are pinned in tools/dtv_firmware_proof.py. Bandwidth bit10 distinguishes
8MHz from shared6/7 selection; six/seven ambiguity is kept unknown.

NEW UNCOMMITTED RF FRONTEND (not yet validated end-to-end):
src/peripherals/dvbt_rf.hpp + --exploratory-dtv-stations 'MHz|BW|FILE.ts;...' or
off. Requires original VD bus +PLL. Bounded valid188-byte packets, no service
parsing. IDEAL uncalibrated receiver only; derives documented silicon AGC212F.0,
DCR2145.0, mode24CF.4, TPS2222.1 and P-sync232C.1; TPS2224..26 describe explicit
64QAM/no hierarchy/2/3/8K/GI1/32 transmitter defaults. NEVER owns2070/23E0 or
mailbox bytes. Unknown bandwidth/cochannel collision stays unknown. Strict
defaults unchanged. PACKET DELIVERY TO TSP IS STILL UNIMPLEMENTED and summary
explicitly reports0 delivered packets. No DTV website controls/deployment.
Next build is running; run core/probes, full suites, then separate matching
682MHz/off-input cold probes with actual .ts fixture and compare the last
invalid MCU read history. No RF acquisition or channel reception claim yet.

Potential existing bug to fix before using post-start VD snapshots: ioClass(Riu)
rebinds VD code storage but appears not to reinstall VD busRead/busWrite callbacks
in a fresh process. Early snapshot regression never reaches VD startup. Test an
original-ISR save/restore equivalence and factor callback binding as needed.
All active RF development probes use cold native boot, so this has not affected
their evidence. Do not claim restored DTV MCU equivalence without testing it.

Foundation committed as d380414. Continuing changes NOT YET COMMITTED:
cpu/mcu_value_mask.hpp exact correlated ADD A,ACC abstraction, admitted only
if EVERY represented input agrees on C/AC/OV; otherwise existing guard stops.
Logical AND/OR/XOR with partially unknown Rn preserve masks and origins rather
than refusing pure field-packing data. Unknown decisions/addresses still stop.
3936 native checks PASS + original MCU seven-reply register-bus probe PASS.
Current full verification driver work/dtv_validate.py is RUNNING again (do not
ninja during its probes). Work/dtv-native-fields is a fresh native cold run
to2.6B, SAME explicit diagnostic assumptions/divider1024, with MANTA_I2C_LOG=1.
Binary work/dtv_field_native.exe is a COPY. Inspect its summary/stderr/index
when finished. Prior2.1B probe is work/dtv-native-semaphore (completed).

TSP packet-window deductions to VERIFY in an isolated ORIGINAL-program probe:
210204 is the LE raw TS word at selected cursor: bits14/15=PUSI/TEI in byte1,
bits28/29=adaptation control in byte3, bits24..27=continuity. 210208 is likely
next raw word (low byte adaptation length at packet offset4). Native0xFF8
writes payload cursor to210200, calls3C4 waiting210214 bit8, then1004 reads
pointer field at210204. Thus ready8 may mean read-window-ready as well as
initial packet arrival; must derive semantics, NOT blindly fake a ready poll.
DMA helper400 writes count210224;428 writes command210218, polls210214 bit0,
checks bit1 overflow and actual section BufCur movement. Original section
register CP offsets24 start,28 end,2C read,30 write,34 current,38 remaining
count,3C CRC (MIPS indirect offsets twice these). Native TSP handles section
assembly/matching and original mailbox notifications itself. Generic HW DMA
must move actual packet bytes into guest-configured buffers, never host-write
PAT/SDT results or a channel database. Snapshot/idle-skip dependencies need
invalidating on packet arrival/device state changes.

NEWEST verified state:3931 native checks and ALL required suites PASS, including
full6,592,741-event replay (289 fields equal, identical coverage). Original VD
probe now verifies SEVEN ISR replies, invalid data round-trip through private
XRAM, and no reply for unmapped7000. Evidence/dtv-native-bridge-validation.json
and evidence/dtv-native-firmware-proof.json. Earlier five-reply/3929 notes below
are superseded. No DTV deployment or working RF reception claim.

Connected work/dtv-native-semaphore reaches2,100,000,004 MIPS dispatches with
the ORIGINAL CP running (no unknown boundary, no blanket-zero CP reads), prints
start/end TSP init and _appMHEG5_TaskEntry. Original DTV Manual Tuning panel
shown (UHF CH47 / NO SIGNAL). Fixed genuine bridge bug: semaphore.owner() is
real device state, and knownBits now reports it; unknown owners remain invalid.
Original MCU private-RAM data copies retain invalid masks. Original INC/DEC Rn
can adjust known pointer registers while ACC holds an opaque byte, since they
do not consume ACC or alter arithmetic flags (usesRn still guards Rn itself).

Native diagnostic command: work/dtv-native-semaphore/command.json, COPY binary
work/dtv_sem_native.exe. Uses --vd-dispatch-divider1024 and1B MCU budget to avoid
billions of idle MCU instructions; UNCALIBRATED, explicitly experimental. Still
has chip mirror3D98/99=18/00, cache7844 bit0=1, boot-info5700/01=0 hypotheses.
These are NOT defaults or evidence of exact reset hardware. Default divider4
long run work/dtv-native-bootinfo-budget executes1B original MCU instructions,
stops881,336,661 MIPS dispatches at diagnostic instruction bound, no hardware
completion. No processes from these completed probes remain running.

NEXT MCU boundary is5311 (ADD A,ACC), invoked3719/3732 after reading private
8380/8382 and maskingbit0. This is data-field packing, not a lock decision.
Consider conservative exact ADD A,ACC mask propagation ONLY if C/AC/OV all
determinate; do not blanket-allow invalid arithmetic/flags. Original core has
finite abstractions in cpu/mcu_value_mask.hpp, symbolicCodeTables only AVD now.

IMPORTANT native transport discovery: original TSP firmware already processes
TS packets/sections. Do NOT replace section parsing with a host DVB database.
Original0xE20 reads210214 bit8 (packet ready); controller210200, filter-id read
21021C, header/data access210204, input cursor210200, DMA size write210224,
DMA launch210218;0x428 polls210214 bit0 then checks section buffer cursor.
Section bank211000 + id*80 (MIPS indirect422000 + id*100), PID48words at420000.
Static original words pinned in tools/dtv_firmware_proof.py. Related T3 register
struct independently matches this layout: private work/dtv-reference/t3-mhal_
tsp{_reg.h,_hardware.h,.c}, downloaded from Benjamin-Dobell/kogan-tv-gpl.
NEXT transport model should feed the native TSP FIFO/DMA, letting ORIGINAL TSP
code assemble/filter sections and publish its own mailbox messages. Need derive
DMA command semantics/timing, output pointer, packet flags and RF acquisition.
Private fixture dtv-one.ts is real MPEG2/MP2 transport with DTV ONE SDT/NIT,
ffprobe verified, still NOT connected. Future cold probes should explicitly set
MANTA_I2C_LOG=1 in subprocess env to capture digital tuner bytes (last probe did
NOT set it; command JSON cannot contain environment).

Read docs/dtv-native-hardware.md first. DTV remains unfinished; no fabricated
channels/lock/picture or DTV website deployment. All new CPU/device paths are
opt-in and preserve the original instructions. Earlier work below is retained.

Native core checks3929 PASS; original VD register-bus probe now verifies FIVE
actual ISR replies (unwritten payload remains INVALID, unmapped7000 no reply).
Previous four-reply notes below predate invalid DATA transport support.
Baseline18 cases, display9 cases including replay, clock, auxiliary AVD/VD/PM51,
copro, snapshot, diagnostic options, forensics4 and clean-PATH presenter PASS.
Full strict6,592,741-event replay is currently running in work/dtv-validation
(driver work/dtv_validate.py; do NOT rebuild while it executes probes).
The first display validation failed writing a trace on nearly full C:; rerun
using D:/CodexWork/MantaLCD4214/dtv-test-temp passed. No reference was changed.

Resource51 (946752 bytes) is the actual DTV CP program loaded by original MIPS
after rawF9, not resource61. DTV cache geometry1F207844 bit0 remains unknown;
explicit0/1 probes reached identical next boundaries with896 original cache
warm-up instructions difference. Diagnostic chip mirror3D98/99=18/00 remains
unverified. Corrected partial mailbox role read67BC/BD: known low nibble can be
masked without inventing unwritten high byte. Latest CP stop (work/dtv-native-
mail-known)659E0 uses unknown1F205700 as boot-info memory base: ((reg<<16)+10100),
checksFA54534D/61744150. No real meaning/reset/writer yet; NEVER silently set0.
Caller8EA0 falls back65A8C on failed signature. Any explicit5700=0 probe is
counterfactual only and must not become the working DTV configuration.

New --max-vd-instructions (default20M) allows bounded longer original MCU runs.
work/dtv-native-bootinfo-budget uses1B and the explicit5700=0 hypothesis;
copied binary work/dtv_bootinfo_budget_native.exe is running. Full instruction
stepping can be slow now that the native MCU stays in its own polling loop.
No RF/lock defaults are supplied. Do not interpret the old20M stop as hardware.

CP RAM validity now conservatively drops all known bits/origin for a shared
invalid byte: the board observer is byte-granular and another CPU can overwrite
it after a previous CP partial store. A regression prevents stale masks from
promoting the new observation. Existing snapshots/layouts/defaults unchanged;
optional cpknown preserves register masks and standalone RAM metadata.

Ten completed copied dtv*.exe probe binaries were MOVED (preserved) from work/
to D:/CodexWork/MantaLCD4214/dtv-probe-binaries/ to free C: space. Old command
JSON files reference their former locations; update executable path when
replaying them. Active dtv_bootinfo_budget_native.exe remains in work/.

Private transmitter fixture work/dtv-inputs/dtv-one.ts (MPEG2/MP2 with SDT/NIT)
is NOT connected to TSP or evidence of reception. TSP transport section DMA,
RF acquisition model, native compressed video decode and DTV antenna UI remain.

## DTV antenna work in progress (2026-11-05)

User now requests DTV antenna inputs like ATV, explicitly native/original
firmware rather than fabricated channels/UI. Do not claim a working DTV signal
or publish a pretend picture/lock. No digital stations have been discovered yet.

Recovered DTV banner snapshot at work/checkpoints/dtv-banner.snap, 1.6B. Its
canonical options are recoverable read-only from the snapshot meta section;
saved locally in work/dtv-banner-options.txt. Exact options + native-cpu +
load-state-allow-extra-options loads. ATV profile is not a superset of that old
snapshot. Real keys at 1.62B MENU40, 1.66B DOWN0E, 1.70B OK0D reach original
DTV Manual Tuning, UHF CH47. Screen evidence is in work/dtv-antenna-menu-exact.

Opening that panel blocks in MDrv_MBX_SendMsg, 805D9E24/805DB908, before it
can consume the next key or program the tuner. 1F206700 FIRE remains set;
class2/index1/no params. Class2 is TSP; this goes to the ORIGINAL multimedia
AEON/OpenRISC resource61, NOT PM51. Existing copro.hpp already models the
mailbox IRQ and original CPU; most live profiles have it disabled. Do not
invent a mailbox acknowledgment. Stack/write trace: work/dtv-mbx-trace.

Cold native probe with exploratory-copro, NO copro-zero-unknown, starts the
actual resource61 and stops after10 instructions at original CP124 reading
A0202400 (MIU control). work/dtv-copro-cold contains command, summary, logs.
This exposed another missing connection: copro RIU accesses were private
latches, not connected to the existing board device models. New uncommitted
--candidate-copro-riu connects fully known reads and writes to those existing
models; unknown reads stop instead of falling through to blanket zero. This
still needs validation and further continuation. Next cold probe should remove
the old 1f206728/29 +ack hypotheses as well as keeping copro-zero-unknown OFF.

Also added uncommitted --exploratory-dvbt-registers: provisional MCU XDATA
2000..25FF to RIU1120..1125 shared mapping, preserving unknown masks and original
software writes; no reset/lock/status defaults. The original VD MCU previously
stopped at4EF1 MOVX from2070. Crucially, original MCU writes its FEC state
23E0 itself (e.g.3CE7 clears,445F sets1): NEVER supply0B from the host to fake
FEC lock. Pending MOVX can be retried after attaching the new bus, without
resetting/changing PC/registers or resuming unknown branch/SFR boundaries.
Snapshot opt-in resumes at current virtual time, not accumulated suspended time.
Extended vd_mcu_probe --register-bus tests original IRQ write/read completion
and refusal to acknowledge an unknown register; not yet run at this note.

Native copro continuation: work/dtv-copro-known-exact cold run reaches1.1B
MIPS dispatches,376,274,944 original multimedia instructions, MM TaskEntry,
475 real mailbox FIQs, zero copro blanket-zero reads. Both old1f206728/29
+ack hypotheses were removed. Full original firmware still runs; no function
hooks/PC redirects added. This is connected execution, not DTV reception.

--candidate-copro-riu now enables conservative validity monitoring on the
EXISTING Or1k engine (cpu/or1k_observations.hpp + beforeInstruction hook).
Bitwise masks/copies/loads/stores preserve known masks and origins; unknown
comparisons, addresses, arithmetic or foreign peripheral writes stop. INTC
mask reset bits remain INVALID; original mask/unmask RMW operations establish
them. The virtual CP interrupt fabric currently wires only the actual MIPS
mailbox source113 (other CP device interrupt outputs are not wired). CP general
RIU reads use INVALID observations, never blanket zero, with existing board
devices connected. Partial configuration stores need the same observed origin
and preserve unknown masks in Riu. Platform-ID cache routine C8454..C8490 is
checked against all16 exact original words; its198/19C reads remain INVALID,
including cached RAM. tools/dtv_firmware_proof.py pins this and the original
DVB-T FEC writer/getter. Do not supply a fake ID or FEC0B.

The board unknown-memory page filter is NEGATIVE ONLY. New exact memoryKnown
query prevents an invalid identity byte from falsely invalidating known
pointers elsewhere on its page. CP validity lives in optional cpknown snapshot
section; legacy snapshot layout and default paths preserved. Isolated tests
and3919 native checks passed before the last debugger/summary additions;
vd_mcu_probe --register-bus passes with4 actual original ISR replies and no
acknowledgment of an unwritten register.

--diagnostic-copro-boundary-stop halts the main run on a CP hardware boundary
(debugger only, excluded from option fingerprint). Native cold search v1 halted
correctly at421,625,859 MIPS /5,153,592 CP instructions: unknown JPD2E60 status
comparison at CPB2C34. Do NOT bypass this. Existing real JPD model is available;
new run work/dtv-native-search-jpd/command.json enables exploratory-copro-jpd
and ge-yuv-stretch, with no copro-zero-unknown or handshake+ack. Session83786
is active as of this note. Binary work/dtv_search_native.exe is a COPY, so
rebuilding build-irq/manta_native_full.exe cannot relink underneath that probe.
Keys: DTV rawF9 at1.3B, MENU40 at1.68B, DOWN0E1.72B, OK0D1.76B/1.88B;
max2.1B. MANTA_I2C_LOG=1; read stdout/stderr/summary and frames after completion.
VD currently stops on unknown-to-foreign XDATA (get exact PC from summary),
because input/control reset bits still unknown; no RF/DVB TS model yet.

Build dir work/build-irq, no current ninja. Unrelated tools/sunplus_run.cpp
etc remain preserved. No DTV changes committed or deployed yet. Previous
teletext deployment below remains live. Continue implementing the original
tuning/TS path and required validation before commit. No DTV UI controls should
be published as working until a real transport stream is parsed by firmware.

## Teletext file input (2026-11-05)

Latest user request: publish the scaling/name fixes first, then support .ttx
files. Scaling/identity commit d6bb703 deployed successfully in Pages run
42076020370; live config and WASM HEAD verified (a6ff52c36f22, 39,034,984 bytes).

Added per-station Teletext .ttx file picker and return-to-text control. The
browser keeps the original ArrayBuffer and transfers copies on Connect antenna.
The worker writes /data/atvN.ttx; optional fifth station field selects the file:
55.25|||0|capture.ttx. Non-empty files up to 16 MiB, raw42 or 55 55 27-framed
raw45, are loaded once; framing alone is stripped. Original encoded packet
bytes/order are cycled through the same VBI ring, without generated page/8-30
mixing. The file owns page numbers and broadcast identity. Station-list parsing
is transactional. No firmware/name RAM patches or host teletext renderer.

WASM tests render full-screen FILE TEST / UPLOADED TELETEXT FILE through the
original firmware. Raw42 and raw45 frames have identical SHA256 222ac7df388e...;
different body produces 83b629bf914c... . See evidence/teletext-file-preview.png
and evidence/teletext-file-web-validation.json. 3905 native checks pass;
worker transfer/byte tests, page validation, JS parsing, baseline/display,
clock, auxiliary, clean-PATH presenter and forensics checks pass. Final full
strict replay passed all 6,592,741 events with 289 reference summary fields
and coverage identical. Commit 97c61d2 deployed successfully in Pages run
42077377967 (https://github.com/vthreex/manta-emulator/actions/runs/42077377967).
Live upload UI, worker hash, config and WASM HEAD verified: c0bdd2de5d3f,
39,037,408 bytes. Both requested website updates are published.

File selection persists only in the current page memory. Connect antenna again
after restarting the emulator. TTI, CEBRA, MPEG-TS and bit-reversed files are not
supported containers. Existing exploratory VBI timing/palette/scaling models
remain assumptions. Arbitrary TV station names and power-cycle persistence
remain unverified. Unrelated Sunplus changes are preserved and excluded.

## Follow-up: full-screen teletext and automatic station identity (2026-11-05)

User screenshots showed the496x280 teletext buffer in the panel corner and
C-03 in the TV banner. The previous checks verified page text only, not output
scaling or stored TV names.

Scaling: original80539824 /8053979C explicitly request496->1920 and280->1080,
writing12-fractional-bit ratios0422/0425 at active engine-bank offsetsD4/D8.
Indexed preview now samples with those ratios under its existing opt-in nearest
sampling hypothesis. Native two-body tests pass to2B with2,073,600 covered
pixels; actual live-profile WASM renders1920x1080. Screenshot:
evidence/teletext-scaled-preview.png. Existing unscaled/palette tests preserved.

Naming: original80453D50 calls CNI lookup805B248C and name copier805B27FC.
It uses a built-in identifier table at806B2BD0 (549 records x28 bytes), not
the arbitrary teletext status string. Without a known ID,80402254 falls back
to80402118 formatting C-03. New station syntax adds an optional broadcast ID:
55.25|TVP1|TEST|0x4801. Packet8/30 encodes both reversed NI bytes; original
804530F8 reverses them back. A real connected manual search now decodes4801,
selects index512 and writes TVP1 into its own806BDEE0 name buffer. No host RAM
name writes or firmware changes. See docs/atv-channel-identification.md.

The web station controls now separate custom teletext text/name from the TV
channel identity, offering466 usable identities extracted from the original
table. Arbitrary TV-name editing is not verified; do not promise a Rename
action (observed Programme Edit offers Delete/Move/Skip/Favourite). Worker
protocol test passes. tools/station_name_probe.cpp also verifies the original
lookup in isolation (distinct from connected progress).

Programme Edit hit a previously unsupported C1030 gradient. Added horizontal
gradient execution under the existing opt-in quantization hypothesis, and
corrected the axis/register pairing from original80523228..80523420:
1030 uses horizontal72/76/7A;2030 vertical74/78/7C. Stale other-axis registers
are ignored. Updated the old synthetic gradient test to verify vertical rows;
static firmware-word proof now pins the command/register pairings.3897 native
checks pass. Connected RED Save, MENU, DOWN, OK now opens Programme Edit and
visibly shows the selected1 TVP1 / ATV entry. Snapshot digital TVP1 is a separate
pre-existing row; the analog row verifies this run's new stored station name.

Follow-up validated: 3897 native checks; 18 baseline and 9 display cases;
auxiliary/clock/forensics; clean-PATH presenter; original lookup probe; web
worker and actual WASM teletext rendering. Final strict replay matches all
289 reference summary fields and coverage (6,592,741 events). Saved analog
TVP1 row visually verified. Publishing this follow-up now; next user request
is raw .ttx file input. All unrelated Sunplus changes remain preserved.

Recovered from repository and local evidence on 2026-11-05. Teletext page 100
now renders transmitted name/body through the original firmware using the
explicit exploratory models below. TV channel-name storage remains unverified.

## Current graphics work (supersedes earlier GE stop)

GE format-9 zero clear and MIU1 address selector now implemented from original
80525DD0/DD8/DE8 and TEXT 80455E88. Overlapping zero-fill rows are accepted;
other format-9 colours remain guarded. Shared DRAM write bounds now cover the
configured MIU1 bank while preserving reduced-MIU0 holes and complete-range
validation. Plain same-format I8 copies support MIU1; height-minus-five still
requires the existing full-height hypothesis. Constant-index I8 gradient 1030
uses existing opt-in I8/gradient models; nonzero index deltas remain refused.
The old TEXT sequence now runs to 1,460,000,011 dispatches without a GE stop
and enables/latches GOP bank1/window0: 496x280, pitch496, format4, MIU1 0368F000.
Local summaries: ../../work/ttx-ge-fixed{2,3,4}.json.

Next missing component was the main palette DMA. Original 805395B0 builds
eight-byte BGRA/index records via 8053A540 into MIU1. 8053779C..77DC splits its
base across logical 1F0C upper bits and 1F0E; 80539258..92B0 supplies count-minus-
one and starts logical 1FF0 bits0/1. New opt-in --exploratory-gop-palette-dma
loads only complete known mode1000 records, validates reserved bytes and the
entire source first. MIU1 selection and immediate timing remain assumptions;
no completion/IRQ/ACK is fabricated. Existing palette serialization holds the
results. Superset snapshot load accepts this stateless decoder flag.

New --exploratory-gop-indexed-preview is presentation-only (excluded from
snapshot fingerprint). It interprets uploaded mode1000/3000 entries as ARGB8888
under an explicit diagnostic assumption; unknown entries and mode2000 remain
undecoded. Main TTX window calls 8053A610(0,0,3F), reflected in known GWIN high
control bits 3F (blend disabled / full constant opacity); preview honors this
instead of interpreting every zero-alpha palette entry as transparent.
MANTA_GOP_PALETTE_DUMP=1 dumps raw entries and modes in the summary.

Current validation: 3,892 native checks PASS. Two connected original-firmware
runs with the same RECOVERY name and different ALPHA TEXT / BETA TEXT bodies
both reach 2B dispatches, 138,880 covered pixels, 256 uploaded main palette
entries and different frame hashes. Visually inspected ALPHA TEXT frame:
evidence/teletext-graphics-preview.png. Details:
evidence/teletext-graphics-validation.json. Reusable test:
python tests/teletext_graphics_regression.py
  --command ../../work/teletext-recovery-command.json
  --emulator ../../work/build-irq/manta_native_full.exe
  --artifacts NEW_EMPTY_DIRECTORY --report OUTPUT.json
Targeted GE fill strict parity, native presenter/clean-PATH startup and
diagnostic-option regressions PASS (evidence/teletext-*-regression.json).
Publication validation (2026-11-05): baseline firmware suite, nine-case display
suite, clock profiles, AVD/VD/PM51 auxiliary suites and four forensics tests PASS.
Full 6,592,741-event ge-blit-cold replay completes at 854,795,171 dispatches;
all pre-existing summary fields and coverage match exactly. The old standalone
PM history reference is absent; current replay history is retained locally.
See evidence/teletext-publish-validation.json. Website publication complete:
https://vthreex.github.io/manta-emulator/ (source cfe83e2, successful Pages run
42068994653). Live config contains the new options and WASM version
03685edd9317; live WASM returns HTTP200 / 39,034,004 bytes. Evidence:
evidence/teletext-pages-deployment.json. Unrelated Sunplus edits remain local.

Browser build rebuilt with native/web/build.py. Existing site snapshot and
configuration preserved; two new explicit exploratory options enabled in
site/live/config.json. WASM menu smoke test and actual live-site-profile
teletext test PASS (WEB TEST / BROWSER TELETEXT visible, 462 white body glyph
pixels). Screenshot: evidence/teletext-wasm-site-preview.png. New reproducible
WASM test: native/web/node_teletext_test.mjs. Worker now versions JS/WASM fetches
using the existing config hashes so returning browsers cannot reuse old code.

Packet generator correction: 8/30 advertised page 110, while transmitting 100.
EN 300 706 table18 gives units/tens in bytes7/8 and M1 in byte10 bit4. It now
advertises page100/subcode0000/magazine1 (tested constant in vbi_slot.hpp).
Reserved the second row of the double-height name, so body starts at row3.
Constant-index gradients also ignore unused alpha deltas under the existing
I8 hypothesis; any nonzero B/index delta still refuses. Original firmware now
renders the page body and coloured page links; header blinking is firmware
behavior. No host-side text/OSD drawing was added.

TV banner probe (raw4B then raw16, budget1.407B) reaches budget but captures no
active GOP pixels; it does NOT verify channel-name storage. Local evidence:
../../work/ttx-channel-name/. Next: trace actual channel-name update callback
from packet8/30 and/or the original auto-tune/store path. Do not infer TV
channel naming merely from RECOVERY appearing in the teletext header/body.

Further naming trace: core 805C36FC..3744 copies the decoded packet8/30 status
display into core-state+048B (20 chars). Getter 805C1270 returns that pointer
and tests core+04BA. Its only direct JAL caller found in the original image is
805C5F54, which calls the teletext renderer 805C5A1C. This is evidence of a
teletext status-display path, not an ATV channel-storage writer. Do not assume
automatic TV channel naming is a firmware feature; investigate ACI/header-name
and indirect callers before claiming it or inventing a guest-memory update.

## Preserved state

HEAD at recovery: `4470991` (live station name/text fields), preceded by
`10c117a`, `c63929e` (layout experiments), `2ab2339` (VBI feed), `4b59d1b`
(antenna UI), `55ef6b4` (PLL tuner and manual search).
Pre-existing edits: tools/sunplus_run.cpp; untracked .claude/, 5MB, AGENTS.md,
configs/sunplus/hyp-005-no-input.tsv, evidence/blaupunkt/. These were left intact.
No firmware patches, repository resets, or CPU replacement.

## Implemented before this recovery

PLL tuner slave and station frequency/level/picture model; manual search detects
55.25 MHz and unmutes the picture (docs/atv-tuner-model.md and its screenshot).
Station name/text fields are connected through the live page to the existing
exploratory packet generator. VBI writes 48-byte DRAM slots; original driver read
pointer follows the hardware index. This demonstrates packet transport only.

Local earlier experiments: ../../work/atv/{tx1,tx2,tx3,tx4,c1o6,c1o8,o7,o8}/.
tx4/s.json reaches dispatch 2,404,001,692 (budget); tx4/index.json has zero
covered GOP pixels. It is not a successful teletext display result.
Checkpoints: ../../work/checkpoints/atv-boot.snap and atv-1p3B*.snap.
Old run helper: ../../work/atvrun.py; its hardcoded executables may be stale.
Latest pre-recovery native binary: ../../work/build-irq/manta_native_full.exe.

## Changes in this recovery

Added tools/teletext_layout_proof.py: hash-validates the stock firmware via
load_image, checks eight exact instructions and all sixteen Hamming codewords.
evidence/teletext-layout-proof.json records PASS. Consumer at 80455070 copies
slot+4 for 44 bytes, tests slot+5=27. Core 805C10E0 decodes MRAG at record+2/+3;
805C3338/3348 decode header page digits at record+4/+5. Thus default slot offset
6 is correct; byte reversal is not needed for these Hamming codes.

Added src/video/vbi_slot.hpp and native tests: reject offsets above 6 before
writing (the old unsigned 48-offset could underflow and corrupt host memory),
preserve the slot marker during reversal, verify adjacent bytes remain intact.
Producer now bounds its cursor before writing after a ring-capacity reduction.

## Verification

python tools/teletext_layout_proof.py --firmware ../../work/firmware-original.bin
  --report evidence/teletext-layout-proof.json : PASS.
ninja -C ../../work/build core_tests : built; core_tests.exe : 3,865 checks PASS
(includes new slot tests; rerun after final cursor-bound edit also PASS).
Full baseline/display/auxiliary/clock/forensics suites, clean-PATH startup and
6,592,741-event replay have NOT been rerun this session. No commit was made.

## Next work / known bugs

1. Reproduce a stored-channel viewing state with picture unmuted. Previous work
   reports the tracker 80475A18 / 806B92C0 path still yields blue/no signal even
   when the tuner AFC locks (~864 MHz, divider 14466). See docs/atv-signal-model.md.
2. Observe core entry 805C31E4 and return from 805C10E0, page-cache updates and
   TEXT raw 12 dispatch. Default offset/Hamming now has static proof; transport
   consumption alone does not establish cache acceptance or UI availability.
3. Trace packet 8/30 name callback through firmware to channel storage; do not
   write names into guest RAM directly. Validate auto-tune/store/restart too.
4. Packet 8/30 initial-page/subcode/date fields need an EN 300 706 audit. Standard:
   https://www.etsi.org/deliver/etsi_en/300700_300799/300706/01.02.01_60/en_300706v010201p.pdf
   Do not conflate its status-display text with proof of automatic naming.
5. vbiLast_/vbiSeq_/vbiSlot_ and runtime station list are not serialized by the
   snapshot implementation; teletext snapshot continuation equivalence is unproven.
6. Run required full verification before committing. Do not rebuild during suite
   probes; copy executables for long runs. Strict hardware behavior stays unknown;
   all station/slicer models remain exploratory.

## Reproduced TEXT rendering stop

Recovered the exact option list from the checkpoint meta canonical string (length
prefixed), without modifying the checkpoint or bypassing fingerprint validation.
The current frontend profile does not match this checkpoint; naive superset load
fails correctly. Command saved locally: ../../work/teletext-recovery-command.json.
Existing pre-recovery build-irq native binary, atv-1p3B.snap, station 864 MHz,
TV raw 4B at 1.31B then TEXT raw 12 at 1.4B: stop at 1,404,995,712 dispatches,
80526598 / 1F205180. See evidence/teletext-recovery-stop.json.
GE cmd 0030, format reg34=0901 (destination format **9**, source field 1),
destination 8368F000, pitch 01F0, extent 496x280, colour zero. Both destination
format and address interpretation require tracing. Do not infer format 1 from
the low byte: GeRectFill decodes the destination from bits 12:8.
This confirms TEXT triggers another original graphics path, not successful page
decode. The old 'TEXT does not open' observation is too weak to locate the fault.
The packet standard was checked against the primary ETSI source above, clause
9.8.1; no packet-field change was made based only on search snippets.
