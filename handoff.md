# Manta ATV / teletext handoff

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
See evidence/teletext-publish-validation.json. Website publication in progress.

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
