# UI issues found while testing the live window (language text, Blue Screen, checkerboard, source)

All traces start from the saved NO SIGNAL checkpoint (`docs/save-state.md`) with scripted raw IR bytes through the original
input path; no guest function is called, no register patched. Menu path used: MENU `0xFC`, RIGHT `0x11` x2 (Setup page),
DOWN `0x0E` (OSD Language -> Restore User Default -> Blue Screen), OK `0x0D`.

## 1. Language selector text invisible — emulator bug, fixed

* The popup (OSD Language, 3x3 grid) was drawn with its boxes but **no text**; 93 glyph draws were retired after the OK press.
* Reconstructing the GE register file from the recorded writes showed every glyph request has register 0x11 = `FF02` and
  register 0x13 (constant alpha) = `0000`. The exploratory I2 glyph model multiplied the palette alpha by register 0x13 in
  every mode, so alpha became 0.
* Original code: the ZUI font renderer (`80520A84` -> `805200A4`) calls the library helper with blend mode **2** and constant
  alpha **0**; bitmap draws (`8049D03C`) use mode **1** with constant alpha FF. With Marlon's coefficient numbering
  (1 = constant alpha, 2 = source alpha) the constant is not a factor in mode 2.
* Fix (`src/video/ge_i2_glyph.hpp`): register 0x13 applies only for coefficient 1; coefficient 2 uses the palette alpha; any other
  coefficient is refused. Pinned by 4 native checks. The mode-to-coefficient mapping is an exploratory assumption supported by
  the original code's call pattern, not by hardware documentation.
* Result: all nine languages render; selecting Polski shows "Wybór języka" / "MENU: wróć" with diacritics, all produced by the
  original firmware's own text resources.

## 2. Blue Screen "not working" — the setting works; nothing displays it

* OK on Blue Screen toggles On/Off in guest state: RAM `0x806FBD50` bit 5 (On = 1, Off = 0; byte `E1` <-> `D1`), the menu value
  text follows, and the firmware saves it to SPI flash (page programs ~0.45M dispatches later). It also re-runs its scaler
  source setup (`803E0430` -> `805EF0F0`, `805F1878`, `805F25EC`).
* The emulator has no video plane, so there is nothing that turns the setting into a visible colour. Which scaler/mixer
  register the real TV uses was **not** identified (the code that consumes the byte was not isolated).
* Opt-in presenter aid: `--exploratory-video-plane black|blue|setting` fills pixels no guest graphics provides with a flat colour;
  `setting` follows the guest byte above (this firmware image only). Presentation only; the shade of blue is a choice.
* The NO SIGNAL / menu backgrounds are **opaque black** guest pixels: the full-surface clear is colour `FF000000` (alpha FF, black,
  constant-alpha blend). To show the plane there a further hypothesis is needed: `--exploratory-gop-black-colorkey` (opaque RGB
  black treated as see-through, i.e. a GOP transparent-colour key). Not verified; the firmware does not name such a register.

## 3. Half-screen checkerboard — genuinely uncovered region (no video plane)

* After INFO (`0x16`) the full-screen 1360x768 window is disabled and only a 1360x248 banner window remains (covered pixels
  1,044,480 -> 337,280). The other 520 rows are not provided by any enabled GOP window; the presenter correctly showed them as the
  neutral "not provided" checker. Not a presenter bug; a missing video layer. Alpha-clear pixels inside windows are treated the same way.
* INFO shows the source bar ("SCART"); raw `0xF6` shows the same bar.

## 4. Source selection — what is established

* No raw byte cycles sources; the eight direct source keys (docs/ir-source-power.md) call `8048710C(id)`, then stop at unmodeled hardware:

  | raw | source id | stop | register |
  | --- | --- | --- | --- |
  | `0xF9`, `0xF7` | 0, 5 | `804F9050` via `804FC1BC` | `0x1F225DD8` (audio indirect register `0x2DEC`, `& 0x3F`) |
  | `0x4B` | 1 | `80504374` | `0x1F203C4D` |
  | `0xEF`, `0xF5` | 3, 4 | `805FFF7C` | `0x1F205E30` (scaler bank 1, high byte, tested non-zero) |
  | `0x4E`, `0xF3` | 7, 9 | `805392F8` | unknown preserved bit (one-shot sample) |

* Bracketing the scaler read with a constant moves the stop to scaler bank-6 registers (`1F205E88`) that the firmware also
  *writes*: a read-only status constant is the wrong model there (it needs a reset-value model for a writable register).
* Raw `0xF6` opens only the source information bar; there is no source list in this state.
* None of this is a statement that "no signal explains it": each stop is a specific unmodeled register read inside the source-switch
  path; no hardware value is supplied by default.

## Opt-in provenance extensions (source key 0x4B)

`--exploratory-provenance-extensions` (needs guarded exploration; off by default) adds two generic, bit-identity-preserving
extensions to the unknown-bit tracking: an identity read-write-back of an unknown register byte (`LBU / ANDI FF / SB` to the same
address, as at `80504374`) and bit-field `INS` on a value with unknown bits. With it, source key `0x4B` passes `80504374` and
`803FD9E0` and runs to `805392F8` (a GOP register poll loop waiting for bit 8 of `1F203FE0`; sources 7 and 9 stop at the same place).
An earlier build with these always on changed the strict cold-boot replay (the cold boot executes the same code), so they are gated.

## Still open

* MENU/OK inside the language popup: the key is consumed (key reader runs, app code `0x29`) but no code path differs from the
  no-key run and nothing is drawn, so the popup does not close.
* Source switching beyond the stops above; the real Blue Screen consumer register.
