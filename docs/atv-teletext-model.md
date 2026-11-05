# ATV teletext (VBI slicer) model - EXPLORATORY, work in progress

Station spec: `atv-stations=MHZ[=PIC][@LEVEL][|NAME][|TEXT~LINE2~...]`.
* Original driver: buffer setup 80527F48 (1F206E70/71/74 = DRAM address/8, 6E39 bit0 MIU1, 6E75/78 = slots, 6E3D = 0x82 enable),
  data present = 1F206E41 bit7 (80527F0C), hardware write index = 6E79 | 6E7C<<8 (80527F20), reader 805277B8: slot = base + (idx-1)*48,
  consumer 80455070: byte 5 must be 0x27, copies 44 bytes from slot+4 into the teletext core ring (806C0564).
  NOTE: the older "IR DMA" model (exploratoryIrDmaReset_, irDma_) answers exactly these VBI registers; it was mislabelled.
* Model (Msd309Machine::vbiFeed): while tuned to a station and 6E3D bit7 set, 12 packets per 2 M dispatches (ASSUMED rate): page 100
  (header with name, double-height name row, text rows) and 8/30 format 1 (status display = name). Hamming 8/4 + odd parity per EN 300 706.
* Verified: the driver's read pointer follows the index (806D87D8 = 0x105 vs index 0x107), so the core receives the packets.
* Open: TEXT (raw 0x12) does not open a page yet. Check the core's line format (whether the 42 bytes start at copy offset 2),
  bit order (MANTA_TTX_REV=1 tried, no page), and whether the TEXT handler needs a "teletext available" flag set by the core.
* Findings 2026-11-05: standard Hamming 8/4 decode table at 0x806466C0 (so bytes are NOT bit-reversed); header page-number decode 805C1B80.
  Viewing a stored channel (TV source, channel 1 ~864 MHz) now AFC-locks (divider settles at 14466) but the firmware shows its blue
  no-signal screen: normal viewing uses the status tracker 80475A18 / 806B92C0 path (docs/atv-signal-model.md), not the search checks.
  MENU toggles make scripted menu exits unreliable; test teletext from a viewing state once that path shows the picture.
  Knobs: MANTA_TTX_OFF (line offset in the 48-byte slot, default 6), MANTA_TTX_REV, MANTA_VBI_LOG.

## Recovery checks (2026-11-05)

`tools/teletext_layout_proof.py` now checks the hash-validated firmware's copy,
marker and MRAG/page-digit loads. The default offset 6 and all 16 Hamming codes
pass. Experimental offsets above 6 now fail before writing a slot; all 42 bytes
must fit. Ring-capacity changes bound the producer before its first write.

Reproduced TEXT from `atv-1p3B.snap` using the original snapshot option list and
the existing native binary: stops on GE fill 0030 at dispatch 1,404,995,712,
destination format 9 (register 34=0901), address 8368F000, 496x280, pitch 496.
`evidence/teletext-recovery-stop.json` records the request. This is a rendering
frontier, not evidence of a decoded page or firmware channel naming. See
`handoff.md` for the recovered state and pending validation.

## TEXT graphics fix

Native entry tracing shows 80455DF4 calling the destination binder with format
4, then 80455E88 explicitly rebinding that same buffer as format 9 for a zero
clear. The firmware address helpers at 80525DD0/DD8/DE8 extract logical bit 27,
mask the lower 27 bits and encode the selector as GE bit 31. Thus GE 8368F000
addresses MIU1 offset 0368F000 (backing offset 0B68F000), rather than a MIPS
virtual address. The static proof script pins these original instructions too.

The fill model now supports format-9 zero only (zero is invariant between
RGB555 and ARGB1555). The original 496x280 request has 496-byte pitch and
992-byte rows; writes overlap and cover 139,376 bytes. This is accepted for
this zero-clear format. Other format-9 colours stay guarded. Mapped-memory
callbacks now accept the configured MIU1 bank as well as MIU0 and reject the
hole left by a reduced MIU0 configuration. The full extent is validated before
the first write; absent or undersized MIU1 still refuses completion.

The following I8 copies now support both MIU banks and the original GE-init
register 59 value. Height-minus-five requests require the already existing
`--exploratory-ge-height5-full-copy` hypothesis. Command 1030 in I8 is accepted
only with the existing gradient/I8 hypotheses and zero index-channel deltas;
other indexed gradients still stop. The resumed TEXT sequence subsequently
reaches its 1.46B budget with enabled, latched GOP bank 1/window 0, format 4,
496x280 at MIU1 offset 0368F000. This is distinct from displayed pixels.

### Palette DMA and indexed preview (opt-in)

`--exploratory-gop-palette-dma`: main palette upload 805395B0 uses the
8053A540 record writer, not the previously modeled CPU write pulse. Records
are eight bytes: B,G,R,A,index,0,0,0. Original 8053779C..77DC splits the DMA
base between logical 1F0C bits 15:9 (address bits 18:12) and 1F0E (bits 34:19).
80539258..92B0 writes count-minus-one in 1F0C bits 8:0 and starts logical
1FF0 bits 1:0. Only mode 1000 is decoded. MIU1 selection and immediate transfer
timing are explicitly assumed. No IRQ, ACK or completion-status bits are set.
All records and source-memory provenance are checked before any palette change.
The option can be added on a superset snapshot load; palette content remains
part of the existing serialized palette state.

`--exploratory-gop-indexed-preview`: host-only diagnostic decoding of format-4
pixels through guest-uploaded mode-1000/3000 entries as ARGB8888 (assumed
palette interpretation). No palette defaults are supplied. A used undefined
entry prevents composition of that window. Mode 2000 remains undecoded.
`MANTA_GOP_PALETTE_DUMP=1` includes raw entries/modes in the diagnostic summary.

### Verified connected output

`tests/teletext_graphics_regression.py` runs two received text bodies with the
same station name. Both reach 2B dispatches without a GE stop and produce
different enabled-GOP frames (496x280 / 138,880 covered pixels). The captured
page visibly contains RECOVERY and ALPHA TEXT, drawn by the original firmware.
See `evidence/teletext-graphics-validation.json` and
`evidence/teletext-graphics-preview.png`. This uses the explicit hypotheses
above and is not a measurement of Titania4 pixel conversion or panel scaling.

The generator's initial-page field previously encoded 110, preventing the
page100 body from being selected. It now encodes units/tens00 and magazine1
in byte10 bit4, with subcode0000, per table18 of
[EN 300 706](https://www.etsi.org/deliver/etsi_en/300700_300799/300706/01.02.01_60/en_300706v010201p.pdf).
Body rows start at3 to leave room for the double-height name. GOP blend-disabled
coefficient3F is honored for indexed preview, so zero-alpha uploaded palette
entries can still be displayed with constant opacity. TV channel-name storage
remains unverified.
