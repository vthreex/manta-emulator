# ATV tuner and station model (EXPLORATORY, work in progress)

Options: `--exploratory-pll-tuner` (PLL tuner slave on the tuner I2C bus), `--exploratory-atv-stations MHZ[=PICTURE.bmp][@LEVEL],...` or the
remote function `atv-stations=...` (runtime, not snapshot state). Diagnostics: `MANTA_I2C_LOG=1` (I2C bytes + tuner frames), `MANTA_READ_HIST=1`
(read counts of AFEC/VIF registers), `MANTA_FOE_SIGN`, `MANTA_B98`, `MANTA_B94`, `MANTA_ATV_MASK` (bracketing knobs).

## Proven from the original firmware
* Tuner bus = GPIO I2C pair 2 (input 1F203D11). The firmware writes address **0xC0** + 5 bytes `DB1 DB2 CB BB AB` (e.g. `38 6A 93 04 F5`,
  `05 B2 93 01 F5`) and retries the address 5x when nothing ACKs. It never reads the tuner status.
* Frequency: RF = divider x 62.5 kHz - 38.9 MHz (ATV Manual Tuning shows 54.25 MHz while divider 1490 = 54.225 MHz is programmed).
  Search steps: 16 (1 MHz); near a signal 8 (0.5 MHz); the AFC/fine loop on a stored channel steps 4 (0.25 MHz) in a fixed sawtooth.
* Standard detection 80502B80 (called from the search state machine via 803E34BC, result needs bit 0x4000): 3x HSYNC detect
  (80503028: writes 0x21 to reg 0x3504, returns **bit 3 of AFEC 0x3501** = 1F206A01), then level loop (0x3504=0, 20 ms, needs
  `(0x3501 & 0xF0) >= 0x60`, up to 39 tries), then 20 consecutive HTotal readings (published 11C7/C8 = 1F206B8D/90) matching the
  table 80627940 (1135 PAL, 1097, 910 NTSC, 917, 1127).
* MApi_AVD_GetStandard 805021BC: status word 1F206B98 (lhu) needs `& 0xC001 == 0xC001`; PAL B/G path = bit1 0, bit12 1, FSC field 0xE0 = 0.
* The original AVD program fed the PAL model (tools/avd_signal_probe) publishes 11C5/C6 = 0x0271 (625 lines), 11C7/C8 = 0x046F, 11CC/CD = 0x7004.
* VIF CR_FOE 1F224470 is read only while a station is in range (fine search).

## Model now (labelled assumptions)
While the programmed RF is within 3 MHz of a station: 3501 = 0x78 (sync bit + level 7), status 0xF005, 11C5/C6/C7/C8 as published by the
original AVD program, VIF lock bit 1, CR_FOE = -(offset)/12.5 kHz, noise byte from offset/level; the AVD MCU shared reads get the PAL model.
The presenter shows the station picture with snow/tearing from offset/level when the firmware un-mutes.

## Open
The search still does not store a station and the stored channel's AFC sawtooth never settles: some further condition of the search state
machine (806BDECC states, 80403940..80403A40) is unmet. Next: follow 803E3488 -> 803E31C4 and 8040C1B0 -> 804F5E00 (must not return 0xF).
