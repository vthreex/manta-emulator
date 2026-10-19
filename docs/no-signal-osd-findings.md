# NO SIGNAL — what the original firmware contains (static findings, session 7)

Nothing here is drawn by the host. No NO SIGNAL pixel has been produced by the emulator yet.

## Strings (evidence: `work/extracted-verified/stream-31bf58.bin`, MST type-2 text resource, 26 language tables of 1,214 strings,
## table `L` at `0x240 + L*0x1300` = u32 BE count + u32 BE byte offsets into UTF-16LE data at `0x1F040`)
| ID | English (table 0) | Notes |
|---|---|---|
| `0x1A7` (423) | `NO SIGNAL` | used by exactly one code site, `804D95E8` |
| `0x3A2` (930) | `Warning! No Signal.` | 58 `addiu a0,zero,0x3A2` sites, clustered at `804CA38C..804CA71C` and `804D1128..804D1A0C` (pop-up/warning request paths) |
| `0x1A2..0x1AA` | AUDIO ONLY, CH BLOCKED, INVALID FORMAT, INVALID SERVICE, MHEG5 ONLY, NO SIGNAL, NO CI MODULE, RATING BLOCKED, SCRAMBLED PROGRAM | the screen-status messages |
| `0x1A5`/`0x185`... | SCART is `0x185` (389), `0x28E` "Scart", `0x2A0` "Scart RGB" | |
| `0x48C` | `Loss of signal! Check new frequency?` | |

## Where `0x1A7` is chosen
`804D95A0..804D9640` is a ZUI text callback (event type `a1 == 7` = text request). It reads the word at `806B92C0` (`lui 806C; lw -0x6D40`),
and for values `< 16` jumps through the table at `0x8062294C`:

| `806B92C0` | string | | `806B92C0` | string |
|---|---|---|---|---|
| 0 | (none) | | 8, 9, 10, 11 | **NO SIGNAL** |
| 1, 2 | **NO SIGNAL** | | 12 | INVALID FORMAT |
| 3 | AUDIO ONLY | | 13, 14 | INVALID SERVICE |
| 4 | RATING BLOCKED | | 15 | MHEG5 ONLY |
| 5 | CH BLOCKED | | >= 16 | **NO SIGNAL** |
| 6 | NO CI MODULE | | 7 | SCRAMBLED PROGRAM |

The word is a screen-status/mute reason. Immediate writers found so far store only 0, 3 and 4 (`8047078C`, `80470874`, `80470A20/2C`, `80470C30`, `804D4474`...);
the values that select NO SIGNAL come from a writer that was not found by the base-register scan (computed base or helper). The producing state machine is the
large application dispatcher around `8046FA4C..80470C50` (current top state in `806F8F8C`, `0x1C` is tested repeatedly), which also calls `804C3574` (hide) when the status
is cleared. **Open:** the writer of the NO SIGNAL values, the window/resource that hosts the text, the timeout.

## Not yet established
- Guest timer/timeout that dismisses it, the colour/background, whether analog AV (SCART) uses the same status word.
- The 0x52 vs 0x00 source-detect brackets still give distinct outcomes; no source-detect result has been chosen.

## Dynamic result (s12, 1,305,936,592 dispatches)
With the exploratory GE models (I8 fill, reg00 bits 1..2 ignored for fills, gradient/format-C quantization; see `evidence/ge-reg00-flags-and-gradient-stop.json`) the
connected boot reaches the original ZUI drawing a **NO SIGNAL** dialog (rounded light frame, blue-grey gradient body, white text of string `0x1A7`) into GOP bank 4 /
window 1 at `0xFDB880`. `evidence/no-signal-dialog-diagnostic-window.png`, `evidence/no-signal-dialog-diagnostic.json`.
- Guest-drawn: gradient and solid rectangles through GE (196 gradient fills), glyphs through the I2 model; no host text.
- Colours are NOT verified: format-C channel quantization (truncate to 5 bits) and gradient accumulation are assumptions (`--exploratory-ge-gradient-fill`).
- Gradient evidence: the original routine `8052158C` computes `(|c1-c0|<<12)/(length-1)` per channel (7.12 fixed point, negative as `0x100000-v`); regs 74/75 R, 78/79 G, 7C/7D B (x steps), 72/73, 76/77, 7A/7B (y steps).
- The run then stops on a scaler-side status poll at `BF204A3C` bit 12 (`805FAFD8`), with the retry loop `0x20` x sleep (`805CBBD8`); the timeout that dismisses the dialog is not reached.

## Status-word producers and the repeated `804C3684(2, N)` calls (session 8, static)
`806B92C0` is element 0 of an array of status words (`806B92C0 + 4*index`; helper `8046CE90(index)` switches on its value). Element 0 is written by the large
application state function `8046F960` (its current top state is `806F8F8C`, value `0x1C` is tested at every write):
| writer | stored value | condition (static reading) |
|---|---|---|
| `804705D0..804705DC` | **1** (NO SIGNAL) | `806B905C == 2`; byte counter `806C08A8` is incremented per pass; when it reaches **0x65 (101)** it is cleared and the status becomes 1 (top state must be 0x1C) |
| `80470E60..80470E64` | **1** (NO SIGNAL) | top state 0x1C and `80475788()` returns 0 |
| `80470738`, `80470874` | 4 (RATING BLOCKED) | top state 0x1C; `806C08A5` set |
| `80470900` | 8 (NO SIGNAL) | `805615A4()` returns 8 |
| `80470A20/2C` | 3 (AUDIO ONLY) | |
| `80470DAC..80470F84` | 14, 13, 6, 1, 4, 6, 7 | other service-state results |
| clears (store 0) | `8047078C`, `804707CC`, `80470C30`, `80469A98`, `80451478`, `804D4474`, `804D9368` | `8046CE24()` (signal OK helper using `8058C4F0` and `806B9004 == 1`) returns 0 while status != 0 and top state is 0x1C: the status is cleared and `804C3574` hides the window |
`806B905C` (signal tracker) is written by `80475CA8..80475E84` (values 0, 1, 2): a timer-driven service using the guest tick getter `803E6BB0` with a **0xBB8 = 3000 tick**
elapsed test (`80475CF0`) and a **0x1F4 = 500 tick** re-arm value (`80475D80`); it consults `80561E64` and `80475944`. Ticks are the guest timer at `806E561C`, not host time.
So the NO SIGNAL decision is software-only inside the original firmware: tracker 2 for 101 passes of the state function. What drives tracker 0/1/2 (hardware inputs behind
`80561E64`/`80475944`) is not decoded yet; the unconstrained branch outcomes in the connected run are produced by the labelled scaler/AFEC hypotheses.

`804C3684(a0)` takes ONE argument, the OSD id (stored in `s2` at `804C36A0`). For `a0 == 2` it first calls `803FF8B0` (reads the current UI mode word at `806BDE98`) and returns 0 without
opening anything when the mode already equals 2. The second value (`N`, +11 per call in earlier logs) is a stale `a1` register left by the caller at `80486778`, **not a parameter**.

## Original OSD geometry (descriptors at `0x80686030 + 48*id`, 29 sets, ids 22/23/28 hit the assertion loop)
Fields `(x, y, w, h)` and window counts: 1 full 1360x768 (1078 windows); 2 full (11); 3 (3,520,1360,248) bottom bar (113); 4 full (170); 5 (782,96,544,416) (31); 6 (783,50,544,504) (45);
7 (882,44,440,424) (121); 8 (10,0,856,672) (43); 9/10 (783,47,536,616) (79/88); 11 (854,44,472,624) (68); 12 (195,173,976,424) (36); 13 (300,620,760,80) (11); 14 (1143,96,220,90) (17);
15 (989,102,336,280) (31); 16 full (442); 17 **(1020,20,300,100) (6 windows: the digit-entry banner)**; 18 (100,4,880,760) (69); 19 (760,0,600,600) (40); 20 full (273); 21 (3,0,1200,760) (75);
22 (784,0,570,750); 24 full (44); 25 (882,44,440,368) (22); 26 full (497); 27 full (52). Geometry alone does not name a menu.
