# Why MENU and OK do nothing in the OSD Language popup (original firmware path)

Everything below was traced from the NO SIGNAL checkpoint with scripted raw IR bytes through the emulated PM IR FIFO; the firmware's own
key reader runs. No guest function is called and nothing is patched. Menu path: MENU `0xFC`, RIGHT `0x11` x2, OK `0x0D` on OSD Language.

## Chain (verified by instruction traces resumed from snapshots taken just before the key is consumed)

1. Raw byte -> jump table `0x8061773C` -> stub stores the **app code** at `0x806B910C` and jumps to `8046AA70` (key class filter).
   Arrow-class app codes (`0xB 0xC 0xF 0x5 0x6 0x2 0x3`) set `0x806721D0 = 3`, others clear it, then `8046A754` (timer/flag filter).
2. While the menu is open the menu state word (`0x806B91B8`) is 1. The state-1 handler `804A4390` forwards the app code to the ZUI with
   `804E0CDC(app_code)` unless `806B9139` is set.
3. `804E0CDC` (only when ZUI state `0x806D7E98` is 2 or 3) calls **`804C3960`**, which translates the app code to a **ZUI key code** through the
   jump table `0x8061F1B8` (131 entries). The return value 0 means "no ZUI key": the call returns without posting anything.
4. A ZUI key is posted (`804D97C8`, `8049F824`) and dispatched by `804E0DC4`; the window key handler `8049C9AC` looks the key up in the key list
   of the focused control (halfword `key | flags`, halfword command, terminated by key 0) and posts the command (`804C29E0`). Without a list
   entry only a few default keys are handled (`0x12 0x13 0x0E 0x15`); anything else returns "unhandled".

## What that means for the keys

| remote key | raw | app | ZUI key | result in the popup |
| --- | --- | --- | --- | --- |
| UP | `0x0B` | 7 | `0x0E` | posted, handled (focus/selection moves) |
| DOWN | `0x0E` | 0x0B | `0x12` | posted, handled |
| LEFT | `0x10` | 0x0C | `0x13` | posted, handled |
| RIGHT | `0x11` | 0x0F | `0x15` | posted, handled (highlight moves; the title language previews immediately) |
| OK | `0x0D` | 0x0D | `0x14` | posted and dispatched; **no entry in the popup cells' key lists**, returns unhandled; no draw, no close |
| MENU | `0xFC` | 0x29 | **none** (table entry 0x29 = 0) | **never posted**: `804C3974` returns 0 (nothing reaches the ZUI) |
| raw `0x12` | `0x12` | 0x31 | `0x29` | posted; no window key list contains `0x29`; no effect |
| EXIT | `0x20` | 9 | `0x10` | handled by the application: the whole menu closes to NO SIGNAL (`804C3684(2)`), not just the popup |

Window key lists were dumped from guest RAM (`0x806A0224..`, 146 windows): Setup-page controls map `0x14 (ENTER) -> command`, `0x0E/0x12 -> 9/0xA`,
`0x13/0x15 -> 0x297/0x298`; **no list contains ZUI key `0x29` (MENU) or `0x10` (EXIT)**.

MENU does nothing on plain menu pages either (a second MENU press on the Picture page leaves the frame unchanged), so this is not specific to the popup.

## Conclusions

* The emulator delivers every key correctly (pops counted, key reader and translation executed). The behaviour is the original firmware's
  in this state, not a lost key, stale repeat latch or timing problem: the traces show the keys reaching the exact code above.
* **MENU**: the application-level MENU code (`0x29`) has no ZUI translation, so it can neither close the popup nor leave a page in the connected
  SCART/NO SIGNAL state. The popup label "MENU: wroc" describes a key the firmware does not forward here.
* **OK**: delivered as ZUI ENTER, but the popup's cells have no ENTER mapping. The language is applied while the highlight moves (the title text
  changes before OK is pressed), so OK has nothing left to confirm; the guest settings checksum (`0x806FBD50`) changes with the selection.
* **EXIT works** but leaves the whole menu, not one level.
* Hypothesis, **not verified**: when the DTV source (id 28) is active the application forwards keys through a different path
  (`8049585C -> 803FFE40`, table at `0x806708FC`), which may map MENU to a ZUI key. That cannot be tested until a source switch to DTV
  completes (see `docs/ui-issues-language-bluescreen-checker-source.md`). No host code closes the popup.

## Reproduction

```bash
python tools/checkpoint_sweep.py --emulator ../../work/build/manta_final.exe --snapshot ../../work/checkpoints/no-signal-ready-v2.snap --out out --raws 0x20 ...
```
or use `--ir-script` after the menu sequence above; watch `804E0CDC`, `804C3960`, `8049C9AC`.
