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
| MENU | **0xFC** | 0x29 | opens OSD 1 (full-screen menu, Picture page, first item highlighted) | verified by behaviour |
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
| (menu closers) | 0x00 (app 0x64), 0x40, 0x58 (0x6B) | | each closes the open menu and returns to the NO SIGNAL screen (kb5/u00, kb6/u40, kb5/u58); user functions unnamed | unidentified |
| (panels) | 0x15 (app 5), 0x1C (app 6), 0x41, 0xF1 | | open other windows over the menu (kb6); unnamed | unidentified |
| SOURCE | unidentified | | no raw byte has opened an input-source list (OSD 5..11 side panels) in any run | unidentified |
| INFO, VOL+/-, CH+/-, MUTE | unidentified | | | unidentified |

Notes: `0x0C` (app 0x82), `0x17` (0x25), `0x1A` (4) and `0x42` (0x21) open OSD 14; `0x12`, `0x18`, `0x19`, `0x4A`, `0x54`, `0x57`, `0x5B`/`0x71`, `0xF4` open OSD 15; `0x0D`, `0x16`, `0xEF`, `0xF5`, `0xF6`
open OSD 3 when no menu is open. Their user functions are not named.

In-menu sweep coverage: every raw byte with a decoded stub (74) was injected with the menu open: kb3 (16), kb4, kb5 (32) and kb6 (8) single-key runs plus m4/m5/m6. Besides MENU/UP/DOWN/LEFT/RIGHT/OK, only the closers and banner keys above changed the screen; 0x1E and 0x1F changed nothing.
