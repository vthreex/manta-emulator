# Single-key connected sweep (session 8)

Method: for each of the 62 non-digit raw IR bytes whose jump-table target decodes (`docs/ir-raw-to-app-key-table.tsv`), one full connected run (same flags as the NO SIGNAL run s13, plus
`--exploratory-ge-nearest-stretch`) injected that raw byte through the emulated PM IR FIFO at guest dispatch 1,300,000,000 and ran to 1,335,000,000 dispatches (or the first guarded stop).
Watched PCs: key chain read `8046A758`, OSD show `804C3684`, OSD hide `804C3574`, ZUI key post `803FFE40`. Raw data: `evidence/key-sweep-no-signal-state.{json,tsv}`; reproduction driver
`tools/key_sweep_report.py` (the runs themselves need ~15 CPU minutes each; eight ran in parallel).

State of the machine at injection: connected SCART/AV source with the original NO SIGNAL screen (top state `806F8F8C = 0x14`, ActiveOsd `806D7E9C = 2`, status word `806B92C0 = 0xB`,
software flags `806BEDF0 = 4` as recorded by the earlier session's observation, not re-measured here).

## Results
- Every observed application code equals the corrected table (62/62; signed bytes only differ in representation). The table is therefore exact for these raws.
- **No key was forwarded to the ZUI** (`803FFE40` never ran, 0 posts in 62 runs). The forwarding function `8049585C` (called from `8046B8BC`) needs the software flag mask 1
  (`8040B5D8(1)` at `8046B7A4`), but the connected source only supplies mask 4 (source IDs 20/21 through `804873F0`); mask 1 is only ever cleared (`804880B4`).
  So in the present SCART state the original firmware handles keys in its early key chain only, without ZUI key dispatch. This is a statement about the state reached by the
  labelled hypotheses (SCART ID grounded, scaler/AFEC bit hypotheses), not about a real TV.
- Observed immediate OSD responses (`804C3684` within ~15,000 dispatches of the key read):

| OSD shown | geometry (x,y,w,h) | raw bytes (app codes) |
|---|---|---|
| 14 | 1143,96,220,90 | 0x0C (0x82), 0x17 (0x25), 0x1A (0x04), 0x42 (0x21) |
| 15 | 989,102,336,280 | 0x12 (0x31), 0x18 (0x01), 0x19 (0x1E), 0x4A (0x26), 0x54 (0x30), 0x57 (0x2F), 0x5B and 0x71 (0x1D), 0xF4 (0x1F) |
| 3 | 3,520,1360,248 (bottom bar) | 0x0D (0x0D), 0x16 (0x1B), 0xEF (0x5F), 0xF5 (0x60), 0xF6 (0x5E) |
| **1** | full screen, 1078 windows | **0xFC (0x29)** |
| 17 | 1020,20,300,100 | digits (raw 0x01..0x09 / 0x45), s15 |
| none | | the other 43 raws (their effect is not an OSD in this state) |

- **Raw 0xFC / app 0x29 opens OSD 1** through the menu page-state handler `804A4154` (state word `806B91B8`, jump table `0x8061ADE0`; state 0 calls `804C3684(1)`, sets state 1 and calls `80451E7C`).
  The first paint of OSD 1 is a keyed, stretched BitBlt of **bitmap 0** (300 x 300 four-arrow navigation icon on a magenta key colour) to a 90 x 90 corner area at (1117,679)-(1206,768).
  A four-arrow hint at the corner of a 1078-window full-screen page is what a menu page looks like, so this is the best **candidate MENU** key; it is *not* verified, because the
  menu's text/items have not been drawn and no other raw key was tested while OSD 1 was open.
- OSD 14/15/3 responses belong to keys the early chain acknowledges with a banner; their user functions are unidentified (volume/mute style names are NOT claimed).

## Classification (conservative)
Verified: digits 0..9 (raw 0x45, 0x01..0x09). Candidate: raw 0xFC = MENU (OSD 1 + arrow hint). Everything else: unidentified.
