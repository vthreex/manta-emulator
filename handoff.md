# Manta ATV / teletext handoff

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
and coverage identical. Updated browser build is c0bdd2de5d3f; publishing next.

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
