# DVB-T demodulator mailbox + multiplex model (EXPLORATORY)

Options: `--exploratory-dtv-demod` (mailbox responder; also keeps the original demodulator 8051 from starting) and
`--exploratory-dtv-muxes MHZ[|spec],...` / remote function `dtv-muxes=...`. DTV also needs the DTV hypothesis set used by
tests/dtv_regression.py (`+ack` on 1F206728/29, `+init` reset values, `--exploratory-provenance-extensions`, 1F203FE1 `+rmw`).

* Mailbox (original MIPS driver): address 1F224C04/C01, data C05, command C00 (1 read, 2 write, 4 write-other), doorbell 1F206905 = 2,
  completion C00 = 0xFF within 255 polls (804F2D78 / 804F2E5C / 804F2F14). Without the demodulator CPU every request timed out.
  The model acknowledges at once and keeps a register file (`DMD` lines with MANTA_DMD_LOG=1).
* Lock query 804F4FEC (stage table 806254E8): stage 0 FSM 0x23E0 == 0x0B (3 s hold logic), 1: 0x232C bit1, 2: 0x2070 bit3, 3: 0x2145 bit0,
  4: 0x212F bit0, 5: 0x24CF bit4. All read "locked" while the tuned RF (dtvTunedMHz, firmware formula trunc((MHz+36.167)*6)) is within
  1 MHz of a user multiplex.
* Result: DTV channel 1 TVP1 at 682.0 MHz (divider 4309, UHF 47) -> the firmware shows its normal DTV banner (TVP1, "No Information",
  DTV, Polski) instead of NO SIGNAL. No transport stream / video yet.
