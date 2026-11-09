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

## With a transport stream (2026-11-09)
`--exploratory-dtv-stations "MHZ|8|FILE.ts" --candidate-tsp-packets --exploratory-tsp-packet-cycles 60000` (the other session's real TS input)
now works together with `--exploratory-dtv-demod`: every TS station is also a lockable multiplex; the options may be added to an older save
state (the input starts at its first packet). In this exploratory mode only:
* MBX (1F206700 bit0 "message pending") needs `--exploratory-mmio-bits 1f206700:01:00+ack`; without it the UI loop (805DB94C) hangs on DTV.
* TSP section DMA drops a fragment that does not fit (the section fails its CRC) instead of stopping (TspSectionDma::dropOnOverflow).
* TSP-CPU reads of unknown bits return 0 (same zero-unknown hypothesis as the live MIPS reads).
* MPEG video decoder command register 0x1101 (1F202201) reports bit1 "command done" (driver 80513C5C polls up to 2000x per command).
Result: DTV with dtv-one.ts locks, the firmware reads the stream (the banner name changes from TVP1 to the stream's service), MENU works,
"Signal Information" is enabled. No decoded video; the firmware keeps the video window muted.

## Channel names and EPG from a generated stream
`tools/make_dtv_ts.py CONFIG.json OUT.ts` writes PAT, PMT, NIT (network name + logical channel numbers), SDT (channel names),
EIT present/following (programme titles + descriptions) and TDT (clock); example `configs/dtv/example-mux.json`.
With it the original DTV Manual Tuning on UHF 47 finds "DTV 2" (evidence/dtv-scan-generated-2-services.png) and the original
PROGRAMME GUIDE lists "1 Emu News / Evening News" and "2 Emu Movies / The Long Boot" with the TDT clock (evidence/dtv-epg-generated.png).

## Live page and control centre
Both profiles now start with the DTV options, the DTV hypothesis set (incl. MBX `1f206700:01:00+ack`) and the example multiplex on UHF 47.
Live page box "DTV broadcast": channels (name, now/next title + description) are built into DVB tables in the page (site/live/dtvts.js,
byte-identical to tools/make_dtv_ts.py) and swapped in at run time (`dtv-stations=MHZ|8|FILE`). Verified in the wasm build (node): DTV ->
DTV Manual Tuning -> PROGRAMME GUIDE lists Emu News / Emu Movies; HDMI input unchanged. The TS-processor CPU has no diagnostic budget in this mode.
