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
`804D95A0..804D9640` is a ZUI text callback (event type `a1 == 7` = text request). It reads the word at `806C92C0` (`lui 806C; lw -0x6D40`),
and for values `< 16` jumps through the table at `0x8062294C`:

| `806C92C0` | string | | `806C92C0` | string |
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
