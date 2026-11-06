# Manta ATV / teletext handoff

## Latest DTV continuation (2026-11-05/06)

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
