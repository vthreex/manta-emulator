# PM startup and original logo-transfer investigation

This continuation preserves the existing machine and immutable firmware. The
power-management hardware dependency is **not resolved**. A separately selected
symbolic non-resume predicate explores the original application path; it is not
verified cold boot. No firmware word, guest PC, architectural register, PM reply
or activity marker is supplied by this experiment.

## PM hardware and provenance

At PM PC574F the original MOVX writes logical0E6A after AND7F. Bit7 is cleared;
lower bits are copied unchanged and remain invalid. The shared MIPS alias is
BF001CD4. Related MStar PM headers place this byte in the SPI/UART/GPIO pad-control
group, but no source establishes the MSD309 bit7 side effect. A numerically
similar non-PM all-pad-input control is a different bank and is not imported.

Probe-only `--probe-pending-startup-configurations` retires this store and the
following zero stores to 0E26/0E27 as three **pending, unimplemented effects**.
It does not change live RIU values or known masks. Original code reaches PM575B,
JNB ACC3, after3482 instructions. ACC low three bits have the explicit aggregate
power-good input hypothesis; bit3 remains invalid. Integrated powered/UART mode
still stops at574F after3474 instructions. Default guarded PM mode remains3435
instructions and stops at9B27. No PM interrupt or marker writer executes.

The bit3 check follows the pending writes and precedes normal C/main setup and
interrupt enable. A local PM software producer is not observed. Its electrical
identity, reset behavior and relationship to sequencing remain unsupported.
The original cold writer934D and RTC activity-eight producer remain unentered.

## Explicit original non-resume path

`--explore-pm-nonresume-branch` is independently opt-in and requires the existing
guarded original PM cold-start experiment. The exact original seven-word getter
80404BEC is SHA-verified. Only one invalid activity observation at80404BF4 may
enter the constraint; known status, inconsistent representatives, changed code,
wrong callers and repeated observations reject.

The original ANDI/XORI/SLTIU and caller BEQ execute in the existing MIPS engine.
The one constrained boolean is activity-low-nibble !=8. The register byte remains
invalid (known mask0). Bit provenance retains inversion, so an inverted unknown
bit cannot be laundered into a same-origin write. No PC/register value is patched.
The original BEQ8046A198 selects80469CB8, skipping optional source/channel
restoration. This demonstrates the branch's meaning, not an actual cold marker.
Normal code still has many initializers before80469E38 calls ZUI804E0D48.

## Actual flash-to-MIU0 request

Original guest PC80547350 submits BDMA command3035. Source device5 and destination
device0 match FLASH/MIU0 in related drivers; the observed T4 width fields are
retained rather than importing another variant's width constants.

| Field | Guest-programmed value |
|---|---|
| Source byte address | 002D89D8 |
| Destination MIU0 byte address | 03230000 |
| Length | B280 /45696 |
| Command0 | 03230000 |
| Command1 | 0 |
| Configuration | 0400 |

The model validates framing and bounds, stages actual flash bytes, commits them
to shared guest RAM, clears overwritten invalid-memory metadata, and only then
sets DONE. Its64-dispatch latency remains provisional. Unsupported modes and
bounds stop; existing flash CRC behavior and default traces are preserved. This
is a narrow partial BDMA model, not a claim about every status/control field.

The source is the original Manta orange startup JPEG. The transferred block SHA256
is92b1ccd98f3a02f7dd1a7e5d9a6b0cd73e18e07bdeee9531bf3fbaaf66d2b700.
Its baseline JPEG header reports1940x1080,8-bit,4:2:0. The panel is1920x1080;
firmware-directed decode/crop/scale is still required. The private image inspected
for forensics is not an emulated display capture.03230000 holds compressed input,
not a decoded framebuffer. Original assets are not redistributed.

## Next original mailbox dependency

The first recorded run reaches2,000,000,000 MIPS dispatches and333,430,881 original
OpenRISC instructions. It exhausts its instruction budget at805DB954, reading
BF206700 bit0; this is a real request wait, not a new rejected register read.

Original MBX code names the containing path `_MDrv_MBX_SendMsg`. Guest writes:
control header2B at6701, class12 at6704, command index0 at6705, then FIRE bit0 at6700.
The original role/group table selects the multimedia control-processor mailbox.
Related drvMBX.h names class18 JPEG. It is not the PM51 activity predicate or the
TSP program-download handshake. COPRO enable writes match related HAL_COPRO_Enable
(including the1FE0 sequence and67BC write); actual multimedia processor execution,
code mapping and acknowledgment remain unsupported. No reply is synthesized.

805D9E24..805D9E78 poll in batches of65536 iterations and compare elapsed original
MsOS time with the configured timeout. If elapsed time expires, original code
calls805DB578 to reset mailbox bytes and returns error9. Whether this happens in
the extended run is a separate execution result, not inferred from static code.

At the two-billion checkpoint:6115 guest timer vector entries,468999 total DVB-T
instructions/52 interrupts,6 AVD instructions,3474 PM instructions,0 audio DSP
instructions. UART adds no new JPEG-complete message. ZUI804E0D48 and call80469E38
remain unentered. GOP initialization is still2 entries, palette64 entries,
6 disabled-window programming calls and no decoded enabled window. No GE raster
operation, decoded framebuffer or native guest pixels is demonstrated.

## Validation

941 native checks;18 baseline firmware runs;9 display runs;4 forensics tests;
PM isolated/deferred probe replay; connected PM powered/UART prefix replay;
DVB/AVD and audio-disassembly checks;18 immutable path/alias checks;clean-PATH
native-window startup and both CTest targets pass. The previous complete five-CPU
replay passes unchanged. The new complete diagnostic application replay and
extended timeout-run results are recorded in the accompanying evidence report.

Primary comparison references (variants are not imported blindly):
- [Related CheckPowerGood boot code](https://github.com/neuschaefer/mstar-mboot/blob/962e8b8258378dded694883a9f9acb7058d34631/sboot/src/amber5/boot.S)
- [Related BDMA definitions](https://github.com/neuschaefer/mstar-mboot/blob/962e8b8258378dded694883a9f9acb7058d34631/MstarCore/bsp/milan/include/drvBDMA.h)
- [Related mailbox classes and roles](https://github.com/neuschaefer/mstar-mboot/blob/962e8b8258378dded694883a9f9acb7058d34631/MstarCore/bsp/k1/include/drvMBX.h)
- [Related CPU driver archive](https://github.com/neuschaefer/mstar-mboot/blob/962e8b8258378dded694883a9f9acb7058d34631/MstarCore/bsp/k1/lib/libdrvCPU.a)
