# DTV antenna implementation: original processors and unresolved hardware

Status: **no DTV reception, channel scan, or decoded broadcast picture yet**.
The existing DTV banner and its stored TVP1 name do not demonstrate reception.
This work connects existing processor/device models so the original software
can eventually discover broadcast services itself. No host channel database,
forced FEC lock, patched firmware routine, or replacement DTV UI is implemented.

## Recovered connected path

Real remote keys reach DTV Manual Tuning (MENU raw40, DOWN0E, OK0D).
Previously MIPS stopped waiting for mailbox FIRE1F206700 to clear, inside
MDrv_MBX_SendMsg805D9E24/805DB908. Class2/index1 is an original request to the
multimedia AEON/OpenRISC processor, not the PM51. The normal live profile did
not run this processor. Synthesizing an acknowledgment would hide the missing
execution and leave its DTV tasks absent.

`--candidate-copro-riu` requires `--exploratory-copro` and refuses
`--exploratory-copro-zero-unknown-reads`. It connects the existing Or1k CPU
to the board RIU models with the machine virtual clock. Original mailbox
instructions clear FIRE and publish replies; there is no function/PC redirect.
The interrupt fabric wires the actual MIPS mailbox to CP source113/PIC2;
other CP device interrupt outputs are currently unwired.

The existing CPU now has an optional conservative validity shadow. Loads,
copies, masks and shifts retain invalid bits. Comparisons, addresses, unsupported
arithmetic and control writes using invalid operands stop. Partial configuration
stores require the same observed register origin and preserve their masks.
Unknown data cached in DRAM stays invalid, including across snapshots. Board
RAM validity has byte granularity: a shared invalid byte conservatively loses
all known bits and origin, so another CPU's store cannot inherit stale CP masks.
The exact memory query distinguishes an invalid byte from known pointers
elsewhere on the same page; the pre-existing page filter is negative only.

The original resource61 straight-line chip-identity cache instructions are
checked against all16 exact words before admitting INVALID identity data. No
identity value is supplied by this mechanism. A later use still stops.
Optional `cpknown` snapshot state retains this shadow without changing legacy
snapshot sections when the option is off.

Connected cold run `work/dtv-copro-known-exact` reaches1.1B MIPS dispatches,
376,274,944 genuine CP instructions and MM TaskEntry, with475 real mailbox FIQs
and zero CP blanket-zero reads. Old1F206728/29 acknowledgment hypotheses were
removed. This is connected execution under existing exploratory platform
options, **not strict-mode full boot or DTV reception**.

## DVB-T MCU register bus

`--exploratory-dvbt-registers` requires original shared VD MCU execution.
It provisionally connects XDATA2000..25FF to shared RIU1F224000..1F224BFF,
preserving known masks. It supplies no register reset, ready or lock defaults.
The existing special2F00 mailbox and2071/72 original version publication remain.
An explicitly attached bus can resume a pending MOVX without changing guest
PC/registers or bypassing a branch/SFR boundary. Its scheduling origin is the
attachment time rather than accumulated suspended time.

The original MCU RPC handler may return an INVALID register byte through DATA;
this does not make it known. Isolated `vd_mcu_probe --register-bus` executes the
original ISR and verifies seven guest-generated replies, invalid unwritten DATA,
and no reply for an unmapped target. This test does not demonstrate RF input.
`--max-vd-instructions N` changes only the diagnostic execution bound (default
20M, allowed1..100B with shared execution), never the instruction stream.
`--vd-dispatch-divider` makes the uncalibrated diagnostic clock ratio explicit;
the default remains4. These controls do not supply demodulator status.

The original MCU **writes FEC state23E0 itself**:3CE7 clears it,445F writes1.
Original MIPS804F506C requests23E0,804F5088 compares0B,804F50D8 requests TPS2070.
The MCU also publishes2070 bit3 after testing2222 bit1. Setting23E0=0B or2070
bit3 from the host would bypass original acquisition software. Do not do so.
Static anchors are checked by `tools/dtv_firmware_proof.py`.

Related T4 register mapping/ownership reference (not exact reset evidence):
[INTERN_DVBT.c](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/device/cofdm/INTERN_DVBT.c).

## Current DTV processor boundary

With the existing JPD decoder enabled, original boot-logo JPEG work completes.
Selecting DTV (rawF9) then makes original MIPS load **resource51**,946,752 bytes,
instead of resource61. Resource61 instruction addresses do not apply to it.

Resource51 first encounters unknown cache geometry at1F207844 bit0 (PC85C4).
Separate explicit0/1 diagnostic branches perform different cache warm-up loop
lengths and meet the same next boundary. Unverified chip mirror1F203D98/99
values18/00 are also present only in those diagnostic commands. None of these
values has become a production reset default.

After correcting byte-granular mailbox validity, the next recorded CP stop is
PC659E0, using unknown1F205700 to form a memory address. The original function
659C8 reads this halfword, shifts16, adds10100, checksFA54534D/61744150, then
reads boot-information fields. The caller8EA0 falls back to65A8C if the native
checks fail. No original MIPS writer or exact hardware definition of5700 has
yet been found. It stays unknown. A separate explicit5700=0 diagnostic is
only a counterfactual probe, not a boot configuration or valid completion.

`--diagnostic-copro-boundary-stop` is a debugger halt when the original CP
reaches such a boundary; it is excluded from snapshot option fingerprints.
It does not retire or skip the failing guest instruction.

## Work still required

### Current tuner, RF experiment and MCU restoration

The original digital tuner function804F58C8..804F5A38 uses36.167MHz IF and
six divider steps per MHz. Native CH47 writes10 D5 90 14 C0, divider4309,
or681.999667MHz. The analog path uses a different control/auxiliary pair
(93/F5),62.5kHz steps and38.9MHz IF. The tuner model recognizes these complete
five-byte frames separately; partial and unknown modes remain unknown. The
bandwidth mask0x10 identifies8MHz; the shared6/7MHz setting stays ambiguous.

`--exploratory-dtv-stations 'MHz|BW|FILE.ts[;...]'` supplies bounded188-byte
transport input, or `off` disconnects the experimental input. This is an
explicitly ideal, uncalibrated post-FEC input model, not an OFDM decoder.
Its half-raster capture window and fixed64QAM/2/3/8K/GI1/32 transmitter
parameters are assumptions. It owns only documented silicon AGC/DCR/mode/TPS/
P-sync observations; it never writes software status2070/23E0 or mailbox
replies. No packets are delivered to TSP yet and no channels are received.

Restoring an active original VD MCU now reinstalls its board callbacks without
resetting guest state. The original-ISR probe verifies byte-identical device
archives after continuous versus restored transactions, plus invalid-data
round trips and unmapped-access rejection.

The native parameter initializer804F2FD0 sends46 bytes through804F2F14 using
mailbox command4. Original MCU4AF5/5469 writes each into XDATA8000+index;
command3 reads it back. These original instructions are tested directly.
The MIPS reply loop is bounded to255 iterations without a delay. Divider1024
diagnostic runs left parameter bytes uninitialized and stopped on unknown
carry at MCU0368. A default-divider4 cold run is checking whether those
timeouts explain the missing parameters. Neither carry nor parameter RAM is
initialized by the host to bypass that boundary.

Subsequent divider64 probes still stopped at0368 with invalid parameter bytes;
therefore the slow-clock timeout hypothesis alone does not explain the failure.
They exposed a host reset bug: every MCU hold/release erased private XRAM.
Original MIPS804F4B5C calls reset helper804F4894 during configuration, after the
loader has written the parameter table. This helper asserts/release CPU reset
and does not upload the table again. The model now retains guest-written XRAM
and its masks across CPU resets. Fresh-board RAM remains unknown. An original-
ISR regression writes a parameter, resets the MCU, then reads it through the
original handler: it failed before this change and passes afterward.

This follows ordinary 8051 RAM retention and the original driver's dependence
on it, rather than assuming any power-on contents. A related-family primary
[8051 datasheet](https://www.keil.com/dd/docs/datashts/silabs/c8051f39x_37x_ds.pdf)
also documents RAM retention across reset; it is not an exact MStar reset map.

With retention fixed, the connected divider64 probe passes0368, publishes
software FEC state0 itself, and stops22CE at an unsupported invalid-byte
`MOV R2,06` transfer. The source is2073, read through2375 from1E3D. Importantly,
2073 also has an original software writer:1511/1514/1515 invokes16F5 to write
it after the FEC-state9 gate at14BA. It must not be supplied as an RF lock
observation. Its power-on contents and the initial2073..75 value remain
unresolved; extending data-copy tracking alone would not establish them.

The next connected probe passes native TSP initialization and starts the
original MHEG5 task through2.1B MIPS dispatches. It exposed and fixed missing
validity reporting for the existing hardware semaphore owner. This run uses
the explicit boot-info/cache/chip-mirror hypotheses above and a diagnostic VD
divider1024; it does not establish their hardware reset values. The original
Manual Tuning panel shows CH47 and NO SIGNAL. See
[validation](../evidence/dtv-native-bridge-validation.json).

Static evidence now pins packet-processing instructions in the original TSP
image: packet-ready210214 bit8, controller210200, filter-id21021C, DMA count
210224 and launch210218. Original MIPS save/restore loops independently anchor
48 PID registers and the section-bank layout. The next transport implementation
should supply the hardware packet window/DMA to that native program, letting
the original TSP perform section assembly and publish its own messages.

Establish the remaining CP boot hardware semantics, then observe real digital
tuner programming and its IF/bandwidth configuration. Build an RF/multiplex
input and TSP packet/PID/section-DMA path driven by guest registers. Feed actual
transport packets so original firmware parses PAT/PMT/SDT/EIT and stores its
own channels/names. Compressed picture playback also needs the native video
decoder hardware path; an ATV/HDMI picture overlay is not DTV decoding.

A private FFmpeg MPEG-TS fixture at `work/dtv-inputs/dtv-one.ts` is transmitter
input only. Its service metadata is not evidence that the emulator receives it.
No DTV antenna controls or DTV deployment have been added yet.
