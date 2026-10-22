# Factory Setting menu and Hotel Menu (original firmware), launch codes and status

Everything here is the original firmware reached with real virtual-remote digits; nothing is injected or called. Opt-in exploratory models are named.

## Launch codes (verified by running them)

The ZUI shifts every pressed digit (ZUI key `0x16..0x1F` = digit 0..9) into a 16-bit buffer, `_u16LaunchKeys` at **`0x806D7DD8`** (handler `804CB824`; the firmware prints
`factory key 0x%02bx , _u16LaunchKeys = 0x%02bx`) and compares it after every digit:

| digits (after MENU) | buffer | action | result |
| --- | --- | --- | --- |
| **8 2 0 2** | `0x8202` | prints " factory menu", posts command `0x18` | **Factory Setting** (OSD `0x12`): ADC ADJUST, PICTURE MODE, W/B ADJUST, SSC, SPECIAL SET, VIF1, QMAP ADJUST, PEQ, SW INFORMATION, CHANNEL TABLE1/2, USB, NONLINEAR, BMTEST, build string `KTC_MSD309_V01` |
| **4 5 7 8** | `0x4578` | posts command `0x345` (`EN_EXE_GOTO_HOTEL_MENU`) | **Hotel Menu** (OSD `0x1D`): Hotel Lock, Picture Lock, Key Lock, Set Source, Channel Num, Set Volume, Max Volume (100), Usb Clone (Export) |

Works with the menu open on the Picture/Setup/Lock pages and even inside the Lock password dialog (the launch buffer is global to the menu windows). It does not
work in the plain NO SIGNAL screen (digits go to the channel-number entry there) or in the USB player (the media player has its own key code; the buffer is never written).
EXIT leaves the factory menu to NO SIGNAL.

## Other digit handlers found in the code (not yet reached)

* `804C1158..` (buffer `0x806D7DB0`): `0x9999` posts `804A0460(0x24, 1)`; ZUI keys 0xF/0x10 post `804C09E0(3/2)`: a password/PIN-style window.
* `804C5040..` (buffer `0x806D7DC4`): `0x2580` -> `804C5370`, `0x2588` -> `804C4AE8(0x19)`.
  Typed into the Lock password dialog neither buffer is touched (the dialog's digits go to the launch buffer); the windows that own them were not located.
  The USB player's own `9999` (if any) is in media-player code, not in these buffers.

## What had to be modeled

The factory and hotel windows draw their frames with GE **line** commands (`0x0010`: endpoints `68..6B`, colour `70/71`, clip `55..58`, format `34 = 0C09`).
`--exploratory-ge-line` adds an axis-aligned, one-pixel-wide model (colour quantized like the exploratory format-C fill); other slopes stay refused. About 32 lines
are drawn for the factory root. Hypothesis, not hardware knowledge; see `src/video/ge_line.hpp`.

## Pages checked (from a checkpoint inside the factory menu, DOWN x k then OK)

All eleven selectable items open and draw without a hardware stop except SW INFORMATION: ADC ADJUST (MODE RGB, R/G/B-GAIN 5469/5419/5433, offsets 33/37/33, AUTO ADC FAIL),
W/B ADJUST (MODE SCART, Standard, gains 150/152/152, offsets 1024), PICTURE MODE, VIF1, SPECIAL SET, CHANNEL TABLE1/2, USB, NONLINEAR, BMTEST.
**SW INFORMATION** stops at `804BCE2C`: the page counts the set bits of the RAM halfword `0x806B914A`, a cache of MMIO `1F200A38` taken early in the boot
(`804698A8`), whose value is unknown; needs a reset-value hypothesis in a freshly created checkpoint (`--exploratory-mmio-bits 1f200a38:ff:V+init`).

## Evidence

`evidence/ui-states-session10/12_factory_setting_root.png` .. `16_lock_password_dialog.png`; `tests/factory_menu_regression.py`.

## More launch codes found statically in the firmware (session 11), and what was tried
Method: scan the ZUI key-handler region `804A0000..80500000` for digit-only 16-bit constants, then follow the owning window through the per-OSD key dispatcher (`804C31B0`: OSD id in `806D7E9C` selects a handler via the table at `8061F050`).

| code | where it is compared | owner | effect in the code | tried |
|---|---|---|---|---|
| `8202` | `804CB858` (buffer `806D7DD8`) | all menu windows incl. the Lock password dialog | posts command `0x18` -> Factory Setting (OSD `0x12`) | works in the menu and inside the Lock System password dialog (OSDs `1, 0x12`) |
| `4578` | `804CB850` | same | posts `0x345` -> Hotel Menu (OSD `0x1D`) | works (existing regression) |
| `9999` | `804C11A8` (buffer `806D7DB0`) | handler of **OSD `0x12` = the Factory Setting window itself** (`804C1150`) | posts `804A0460(0x24, 1)` | typed inside Factory Setting: the buffer reaches `0x9999`, no visible change and no new OSD; the command `0x24` target is not identified |
| `2580` | `804C50CC` (buffer `806D7DC4`) | handler `804C5060` of **OSD `8`** (reached only from state 0 of the UI-wrapper state machine `804A3E50`, called from `80476240`) | `804C5370` -> `804C4AE8(0x18)` = the same command that opens Factory Setting | OSD 8 never appears in the NO SIGNAL / menu states, so it could not be exercised; typed elsewhere it only fills the global launch buffer |
| `2588` | `804C50D8` | OSD 8 | `804C4AE8(0x19)` (a second service command, not identified) | same |
Other digit-looking constants in that region are timeouts (`0x1388`, `0x1770`, `0x7530`, `0x2710`, `0x5460`), not codes.
Lock-password dialog: `0000`, `1234`, `2580`, `2588`, `8888`, `9999` were typed after OK on Lock System; none produced a different OSD. Whether one is accepted depends on the stored password (not located).
`tools/try_launch_codes.py` drives these experiments (parallel headless runs from `no-signal-ready-v2.snap` / `factory-menu.snap`).

## The Factory "USB" item (traced statically and with watches, session 12)
* Row 11 of the factory root table (`0x8061DEB4`, 20-byte records) has target page 0 = the root itself, so **OK only rebuilds the root** (`804C0C00` -> `804BE020(0)`): the "goes back to the top" behaviour is by design.
* The action is the value control: RIGHT opens the `< USB  Search >` popup, a second LEFT/RIGHT calls `804BE3E8(item 0xB)` -> `804C0298`: probes USB port 0/1 (`8054E3C0`/`80553ED0`), mounts (`8047E870`), searches for the update file (`8045A560`), reads it (`8045A278`).
* With a stick the original prints `File size: 0x00080000; Flash size: 0x007E0000; Download Buffer Addr: 0x01890000 ...` and `Copying image from USB to DRAM`. It then programs BDMA `0x330` (MIU0 source -> CRC32, poly `04C11DB7`), which strict mode stops on.
* `--exploratory-bdma-crc-miu` (opt-in, mode `0x330` inferred from the field layout of `0x335`/`0x3035`, not verified) models it; the run then continues 2.3B dispatches without a stop but never reaches the CRC verdict (still inside the image copy). Needs stick-side transfers checked next.
* Reproduce: `work/usbtrace.py` (snapshot `factory-menu.snap`, 8 x DOWN, RIGHT, RIGHT, stick image via `--exploratory-usb-pendrive`, plus the `1f203d98/9d` and `1f203fe1/225dd8/225cbc` mmio-bit flags of the factory launcher).
