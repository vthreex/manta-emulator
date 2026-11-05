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
