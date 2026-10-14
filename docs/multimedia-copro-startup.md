# Original multimedia program and startup-image investigation

The original compressed startup JPEG and a separate multimedia executable have
reached guest RAM. There is still no demonstrated decoded framebuffer, enabled
GOP window, ZUI entry or native guest frame. Loading compressed image bytes is
distinct from decoding and displaying them.

## Recovered original directory

MIPS804F0AF4 reads a directory at803B2000, originally flash021F00. The count is a
big-endian halfword at+0C. Thirteen entries begin at+0E, each11 bytes: big-endian
ID16, source32, stored extent32, storage type8. The original code rounds extents
up to8 bytes. Type4 resources contain raw DEFLATE plus an uncompressed-length
trailer; type0 is copied without decompression. All13 entries fit actual8MiB
flash capacity. The extra programmer16 bytes are excluded.

`tools/firmware_resources.py` validates the stock SHA, directory, DEFLATE EOF,
trailer and output bounds. Extraction is explicitly requested into a private
directory. Input/output aliases reject. No original payload is redistributed.

| ID | Original flash | Stored extent | Decoded bytes | Observed use |
|---|---|---|---|---|
| 0011 | 1E7838 | 4408 | 17416 | Existing original TSP upload source |
| 0051 | 1EBC40 | 50488 | 946752 | Earlier multimedia program resource |
| 0061 | 242910 | 769B8 | 1253416 | COPRO loader invoked by original startup-image path |
| 0300 | 2D89D8 | B280 | 45696 | Original MANTA baseline JPEG input |

The new native `copro_probe` executes the complete original MIPS resource lookup
function against the original runtime/prefix. It actually returns resource61,
source242910, extent769B8, type4. An independently parsed manifest agrees.

Resource61 SHA256 is
`bc00e532ab47b35e2f3a8be0b59a40b7ae80111dd366b6ceb741cdb74ca95ae1`.
A read-only asynchronous inspection of the running diagnostic machine found
those exact1253416 bytes at physical0. It also found the original45696-byte
JPEG at03230000 with SHA
`92b1ccd98f3a02f7dd1a7e5d9a6b0cd73e18e07bdeee9531bf3fbaaf66d2b700`.
These were reads of the workspace emulator's validated guest-RAM allocation;
they did not supply state, alter execution or establish unknown-bit provenance.

## Original application path

8049C788 loads JPEG resource0300 through original BDMA. At8049C7C0 the original
call passes resource61, destination0 and size3B0000 to803EDE00. The original
loader obtains the resource descriptor through803BD42C/804F0AF4, selects the
type4 path803EDA60, executes the decompressor via803EDB04, flushes/returns,
and calls COPRO initialization end80545A40 through803EDF0C.

`--copro-probes` observes the original resource61 caller and hashes its actual
guest RAM output before that initializer. It supplies no code, entry point,
hardware value or reply. Other resource IDs are not confused with resource61.

The subsequent image-start function80439138 calls the original MBX driver. Its
still-image request uses class2B(hex) atBF206701, command index12(hex) atBF206704,
parameter count0 atBF206705, and FIRE atBF206700. Original role/group
selection and related driver headers identify the multimedia-control-processor
mailbox. The original sender805DB6E8 and native regression verify every field,
including original rejection9 when FIRE is still occupied. The previous class18
label was incorrect:18 is this command index; related headers identify class43
as STILLIMAGE_CMD_IO. This is separate from PM51 and from the working TSP CPU.

The CP image itself contains mailbox-array access with a6700 offset nearC71C0.
Exact CP RIU wiring and peripheral behavior still require verification.

## Original timeout actually executed

An unchanged default-timing diagnostic continuation reached4000000000 MIPS
dispatches,833430808 original TSP instructions and12519 original main timer
vector entries/software-clock ticks. Coverage proves805D9E7C,805D9E84 and
805D9E8C executed: the original mailbox timeout resets its own bytes and returns
error9. No host-generated completion was required.

The budget ended at80601BC4 inside the critical-section restore used by the
still-image receive loop80439454..84. This PC is not a rejected MMIO access or
proof of an infinite hardware poll. Recent BF006000 read/OR1/write is consistent
with a watchdog service; it is not established as a missing timer interrupt.

The original software timer80438274 compares elapsed MsOS time with a guest
deadline/active flag. At the4B checkpoint, the later timeout branch804394B8 and
image-start return8043917C were not covered. The main activity byte remains
invalid; this path depends on the explicitly selected non-resume predicate.
ZUI804E0D48 and its application call80469E38 remain unentered at that checkpoint.

## Isolated original OpenRISC prefix

Resource61 begins with an8051-like configuration prefix. From100 its actual
little-endian32-bit words decode coherently as ORBIS32-like instructions, unlike
the newer big-endian variable-length AEON reference ISA. This establishes a
startup subset, not complete processor-revision compatibility.

The existing OpenRISC interpreter is reused; no processor is replaced. The
isolated probe independently labels entry100 and any configured reset-r0 state.
The [OpenRISC architecture](https://openrisc.io/revisions/r1.0) makes r0 hardware
implementation-dependent; zero cannot silently become a universal reset value.

| Isolated configuration | Original instructions | Boundary |
|---|---:|---|
| Entry100, unknown reset GPRs | 1 | PC104 consumes unknown r0/r3 in MTSPR |
| Additional explicit reset-r0-zero hypothesis | 5 | PC114 writes A0201FD4, valueF000 |
| Also defer exactly two unimplemented writes | 9 | PC124 reads halfword A0202400 |

The two pending writes are A0201FD4=F000 and A0201FDC=B000. Their effects are not
applied. A0202400 is not supplied a convenient ready value. No CP mailbox reply,
interrupt or image decode is generated. These instructions are isolated
experiments, not an integrated sixth processor or successful TV startup.

## Clock experiments and correctness

`--exploratory-cpi N` supports1(default),2,4,8,16 average virtual CPU cycles per
original MIPS dispatch. All original instructions still execute. One shared
cycle domain drives RIU/peripheral scheduling and CP0 Count at half-rate.
Unsupported factors, counter overflow and use outside guarded reset execution
reject. This is not a calibrated cache/pipeline/bus or physical-clock model.

Default1 preserves previous event/summary formats. Other profiles report actual
dispatches separately from virtual cycles and explicitly mark timing unverified.
Five300000-instruction reset/PM prefixes replay every event, full summary,
coverage and original PM history. No PM readiness or activity marker is added.

Coverage insertion now occurs only on first PC visits. Instruction observation
reads the same shared RAM bytes directly where available, with the old engine
read fallback for other mappings. Neither optimization skips CPU execution,
caches instruction words, changes PCs or substitutes instruction counts.

## Independent unresolved hardware

PM0E6A bit7 has an unimplemented pad-control side effect; connected PM remains
at574F after3474 original instructions under the powered/UART hypotheses.
Probe-only pending effects reach575B and an invalid0EC2 bit3. The cold-init
writer934D and RTC-related activity8 writer have not executed. Strict POR,
undocumented WinbondCF, AVD0802/C008, DVB4AE0/2200 and audio DSP execution remain
unresolved. Diagnostic application progress is not verified cold-power-on state.
