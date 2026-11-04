# Remote key semantics recovered from the original firmware (session 8)

Method: the original firmware's own key reader consumes the raw IR byte injected into the emulated PM IR FIFO; the meaning of an app code is read from what the
**original ZUI/menu code draws or does** in the connected diagnostic run (frames in `evidence/menu-diagnostic/`, watched PCs in the run summaries). No guest function is called and
no register is patched. Pixel colours of those frames depend on the labelled exploratory GE models; the *which item is highlighted / which page / which window* facts do not.

## Dispatch chain (verified by watched PCs)
host key (native window `WM_KEYDOWN`) -> `RemoteQueue` (function name) -> `Msd309Machine::serviceVirtualRemote()` (every 0x4000 dispatches, keymap raw byte) ->
`Riu::receivePmIrDecoded` -> emulated PM IR FIFO (0x1F007B00 region) -> original key scan `8046A6C4` (polled) -> jump table `0x8061773C[raw]` -> stub
`lui s2,806C; addiu v0,CODE; j 8046AA70; sb v0,-6EF4(s2)` (app code at `0x806B910C`) -> key chain `8046AA70/8046A754` -> state-specific handlers:
the menu page-state handler `804A4154` (state word `806B91B8`, table `0x8061ADE0`) opens OSD 1 through `804C3684(1)`; ZUI key forwarding `8049585C -> 803FFE40` (class-1 message) is only
used when software flag mask 1 (DTV source id 28) is set: the sources map to masks through `0x8061A250` (ids 0 -> 0x10, 1 -> 2, 2..9 -> 8, 11..14 -> 0x80, 16..18 -> 0x40, 20/21 -> 4,
23..26 -> 0x20, 28 -> 1, 34 -> 0x100). The connected SCART/AV state never calls `803FFE40`.

## Mapping
| function | raw | app | evidence | status |
|---|---|---|---|---|
| digits 1..9 | 0x01..0x09 | 0x11..0x19 | numeric entry `8046DA24`, OSD 17 banner | **verified** |
| digit 0 | 0x45 | 0x10 | static (table + `8046DA24`) | static only |
| MENU | **0x40** | (handler `8046AE60`) | toggles the menu: opens OSD 1 from NO SIGNAL, closes it from the main menu, returns one level from a sub-page (item panel, lock) - the menus' own "MENU: RETURN" hint (2026-11-04, native runs from the NO SIGNAL snapshot) | verified by behaviour |
| MENU_OPEN | 0xFC | 0x29 | opens OSD 1 (full-screen menu, Picture page, first item highlighted); a second press is ignored | verified by behaviour |
| DOWN | **0x0E** | 0x0B | highlight Picture Mode -> Color Temperature -> Aspect Ratio (runs m2, m3) | verified by behaviour |
| LEFT | **0x10** | 0x0C | page tab moves left (Picture -> Lock, antenna tab disabled so it wraps) (m2) | verified by behaviour |
| RIGHT | **0x11** | 0x0F | page tab moves right: Picture -> Sound -> Setup; Lock -> Picture (m2, m3) | verified by behaviour |
| OK | **0x0D** | 0x0D | on the Picture page opens the item panel with slider rows (m2) | verified by behaviour |
| EXIT (candidate) | 0x20 | 0x09 | with the menu open the original code hides OSD 1 and shows OSD 2 (NO SIGNAL) ~2.4M dispatches after the key (run m4) | candidate: no other raw tested in that position, 'EXIT' is a behavioural name |
| POWER (candidate) | 0x0A | 0x08 | `804A4368` re-injects app 8 and hides the OSD; ~37M dispatches later the original reaches the reset/standby routine `8040468C` (GPIO read `0x1F001CD0`), the display windows end disabled (m2, m3) | candidate |
| MUTE | **0x0F** | 0x1A | closes the menu and shows OSD 14 (1143,96,220,90) with the crossed-out speaker icon (run kb4/u0f; needs hypothesis P1 to draw) | behavioural (icon) |
| SLEEP | 0x17 | 0x25 | OSD 14 with a moon-and-star icon (kb3/u17) | behavioural (icon) |
| AUDIO / sound-mode | 0x1A | 0x04 | OSD 14 with a music-note icon (kb3/u1a) | candidate |
| ZOOM/ASPECT/PIP | 0x0C | 0x82 | OSD 14 with an overlapped-windows icon (kb3/u0c) | candidate, name unknown |
| PICTURE mode | 0x42 | 0x21 | OSD 14 with a monitor icon (kb3/u42) | candidate, name unknown |
| INFO (candidate) | 0x16 | 0x1b | closes the menu and shows OSD 3, the bottom source/info bar (runs m5, kb3/u16) | candidate |
| UP | **0x0B** | 0x07 | stub jumps to `8046A984` (arrow-key class path); with the menu open and the highlight on the 2nd item it returns to the first (frame byte-identical to the post-MENU frame) - run kb6/u0b | verified by behaviour |
| (menu closers) | 0x00 (app 0x64), 0x58 (0x6B) | | each closes the open menu and returns to the NO SIGNAL screen (kb5/u00, kb5/u58); user functions unnamed. Raw 0x00 was also what the emulator itself delivered every 108 ms before the IR status fix below | unidentified |
| (panels) | 0x15 (app 5), 0x1C (app 6), 0x41, 0xF1 | | open other windows over the menu (kb6); unnamed | unidentified |
| SOURCE | unidentified | | no raw byte has opened an input-source list (OSD 5..11 side panels) in any run | unidentified |
| INFO, VOL+/-, CH+/-, MUTE | unidentified | | | unidentified |

Notes: `0x0C` (app 0x82), `0x17` (0x25), `0x1A` (4) and `0x42` (0x21) open OSD 14; `0x12`, `0x18`, `0x19`, `0x4A`, `0x54`, `0x57`, `0x5B`/`0x71`, `0xF4` open OSD 15; `0x0D`, `0x16`, `0xEF`, `0xF5`, `0xF6`
open OSD 3 when no menu is open. Their user functions are not named.

In-menu sweep coverage: every raw byte with a decoded stub (74) was injected with the menu open: kb3 (16), kb4, kb5 (32) and kb6 (8) single-key runs plus m4/m5/m6. Besides MENU/UP/DOWN/LEFT/RIGHT/OK, only the closers and banner keys above changed the screen; 0x1E and 0x1F changed nothing.

## Session 10: single-key sweep in the DTV channel-banner state (native tooling)
State: the machine of `tests/dtv_regression.py` after the original DTV channel banner (`tools/make_dtv_state.py`, ~1.6 G dispatches; interpreter), one raw IR byte injected 8 M dispatches after resuming, 40 M dispatches
observed (`tools/key_sweep_native.py --connected --sheet`; ~20 s per key, 6 in parallel). Frames and OSD ids: `evidence/key-sweep-dtv/` (index.json, contact sheets). In this state the early key chain forwards keys to the ZUI, so the
sources of the DTV remote functions show up:

| raw (app) | what the original firmware draws | remote key |
|---|---|---|
| 0x19 (0x1E) | "Audio Languages" popup (OSD 0xA) | LANGUAGE |
| 0x1D (0x37) | "Channel List" popup (OSD 9) | LIST |
| 0x57 (0x2F) | "Favorite List" popup (OSD 9) | FAV |
| 0x4A (0x26), 0x54 (0x30) | "PROGRAMME GUIDE" (OSD 0x10) | EPG (two raws, one is the key) |
| 0x5B/0x71 (0x1D), 0xF4 (0x1F) | "Subtitle Languages" popup (OSD 0xB) | SUBTITLE (three raws) |
| 0x12 (0x31) | "No Teletext" notice (OSD 0xF) | TTX |
| 0xFB (-2) | a four-cell box on OSD 0xF (looks like a PIN/lock entry) | not named |
| 0x43 (0x23), 0x47 (0x34), 0x53/0x72 (0x1C), 0x5A (0x54), 0xC1 (0x52) | "Disk not ready." message (OSD 0x14, PVR/media function without a disk) | five distinct app codes: REC, play, pause, stop and one more of the glyph keys; which raw is which is NOT decidable from this state |
| 0x0C, 0x17, 0x1A, 0x42 | small OSD 0xE banners (aspect / sleep / sound / picture icons, see above) | WIDE, clock, SMART S, SMART P |
| digits, 0x45 | channel-number banner "CH n" (OSD 0x11) | digits |
| 0xFC | the channel menu (OSD 1) | MENU |

No visible response in this state (no audio path, no channel data): arrows 0x0B/0x0E/0x10/0x11, 0x13, 0x14, 0x16, 0x18, 0x1B, 0x20..0x22, 0x44, 0x46, 0x4B, 0x4E..0x51, 0x58, 0x59, 0x5C, 0x5F, 0xBD..0xCE,
0xD2, 0xD4, 0xEF..0xF8 (the source keys act only when they change the source). VOL+/-, CH+/- and the remaining glyph keys (INDEX, ZOOM, REVEAL, HOLD, RADIO), 3D, L/R, REPEAT and the list-icon key therefore remain
without a proven raw byte; the remote model draws them dotted and `configs/input/ir-keymap.tsv` stays free of guesses.


## Phantom raw 00 key (fixed 2026-11-04)

With `--diagnostic-riu-zero-unknown-reads` (part of the NO SIGNAL profile) the blanket zero also replaced the modelled EMPTY bit (bit 1) of the PM IR FIFO status byte
`1F007B59` (logical `3DAD`). The original IR getter `804F2284` polls it every 108 ms (`804F2474`: `andi 2`, `bnez` = empty); a forced 0 read as "not empty", so each poll
popped a key `0x00` (app `0x64`) from the empty FIFO. Any open page treated that unknown key as a close request (menu page handler `804CA08C`: none of its windows
match -> `804F0724(0x17,5)`), which is why menus, the lock page and ATV manual tuning closed after about a second and why every key seemed to close the menu.
`Riu::read` now keeps the FIFO model's known status bits under the blanket zero. With the fix no hack is needed: navigation keys keep the menu open,
EXIT and MENU (0x40) close/return as the firmware decides, and `--exploratory-menu-no-autoexit` is off by default in the control centre.

## Remaining remote keys (2026-11-04, live profile, native sweeps from the NO SIGNAL snapshot)
Sweep: every raw byte with its own handler (not the default 8046AA64) in four states (NO SIGNAL, ATV, main menu, USB media player drive page),
screen 30 M dispatches after the key compared with the same state without it (scratchpad sweep_state.py; ~15 s per state).
The app-key switch at 0x80617D1C (indexed by the app code, 8046DE14) prints debug key names (`91_KEY_*`), which names several cases:

| key | raw | app | evidence |
|---|---|---|---|
| CH+ | 0x1F | 2 | prints 91_KEY_CHANNEL_PLUS (8046E630); ATV channel banner — **proven** |
| CH- | 0x1E | 3 | prints 91_KEY_CHANNEL_MINUS (8046E63C); ATV channel banner — **proven** |
| VOL+ / VOL- | 0x15 / 0x1C | 5 / 6 | 91_KEY_VOLUME_PLUS/MINUS; volume bar 15 / 13 — **proven** |
| RECALL (candidate) | 0x18 | 1 | case printing 91_KEY_RETURN; NO SIGNAL 'Not Available' |
| L/R (candidate) | 0x19 | 0x1E | case printing 91_KEY_MTS (same raw as LANGUAGE) |
| FAV alias | 0xF1 | 0 | ATV 'Favorite List' (shares FAV's case) |
| Key Lock | 0xFB | 0xFE | ATV 'Key Lock' password prompt |
| UART DEBUG | 0xD4 | 0x70 | hidden factory toggle |
| (unnamed) | 0x4B, 0xF6, 0xF9, 0x54 | 0x5D, 0x5E, 0x5C, 0x30 | 0x4B ATV channel banner; 0xF6 source bar / clears the screen; 0x54 'Not Available' (EPG case) |

Not identifiable in these states (no visible reaction): the teletext/media keys INDEX, ⏸, REVEAL, HOLD, RADIO, 3D, REC and ≡. They need a teletext
signal or a playing USB file (photo/music decode is not in the browser profile). Candidate raws with own handlers but no reaction: 0x00 0x13 0x14 0x1B
0x21 0x22 0x43 0x44 0x46 0x47 0x4F 0x51 0x53 0x58 0x59 0x5A 0x5C 0x5F 0x72 0xBD 0xBE 0xC1 0xC3 0xCC-0xCE 0xD2 0xF0 0xF2 0xF8.
