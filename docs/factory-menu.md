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
