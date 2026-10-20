# Blue Screen: what the original firmware does, and what is still missing

Method: NO SIGNAL checkpoint, scripted virtual-remote keys, `--watch-guest-access` (per-instruction record of loads/stores to a guest range), `--record-trace`
differential of MMIO writes, and disassembly of the original code. Nothing is patched or forced.

## Guest state (verified)

* Setting = **bit 12 of the word at `0x806FBD84`**, i.e. **bit 4 of byte `0x806FBD85`** (`0x10` = On, `0x00` = Off); settings base `0x806FBD4C`, field offset `+0x38`.
  (Correction of an earlier note: `0x806FBD50` is the settings **checksum** halfword, `80472F60..80472F84` verifies it as byte sum + `0x55` compared with `lhu 4(base)`,
  `8047424C` rewrites it after every settings change, so it changes with the language selection too.)
* Toggle handler `804D2AB4..804D2AD8`: `lw 0x38(base)`, flip bit 12, `sw`, `jal 8047A00C(0)` (source-class test), re-read. The menu text follows. The settings
  are then written to SPI flash (page programs about 0.45M and 0.86M dispatches later; the changed checksum byte is part of the data: `0xD1` Off vs `0xE1` On).
* Consumer at startup/no-signal detection: `804863E8` (inside the UI wrapper loop) tests bit 12 after the no-signal counter (`806D4A20/22`) exceeds 40 and calls the
  mute/source-apply routine `803E0358(on, mode, 200 ms, 0)` with different arguments: Off takes `8048674C` (argument 0 unless `8040B5D8(2)`), On takes
  `80486734` (argument 1). `803E0358` then runs `805EF0D4`, `805F1878(lut[mode])` and `805F25EC(1)` (scaler register programming).
* The periodic reader `8047D19C` tests a *different* bit (9) of the same word; it is not the Blue Screen consumer.

## Hardware differential (toggle On->Off vs Off->On, same checkpoint and keys)

Both directions run the same scaler re-initialisation (`805EF0F0`, `805FF734`, `805FF604`, `805F887C` zeroing scaler bank 6). Switching **On** additionally
programs about 34 scaler registers in banks `0x02`, `0x12` and `0x23` (`805FE310..805FE4A0`, e.g. `1F205E14=C010`, `1F205E1C=3778`, `1F205E48=4FB4`) that the Off direction
does not touch, plus bank `0x23` ranges via `805F887C`. Off only repeats the flash save with the new checksum.

Not identified: which scaler/mixer register selects the *colour* of the background or the mute/blue output. The values above look like window/PQ-style
programming, not a colour constant. No claim is made that they are the blue generator.

## Presentation model in the emulator (opt-in, labelled)

* `--exploratory-video-plane setting`: pixels no guest graphics provides are drawn in a flat colour: blue (RGB 0,0,255, a choice, not verified) when `0x806FBD85` bit 4 is
  set, black when clear. Regression: `tests/blue_screen_setting_regression.py`.
* `--exploratory-gop-black-colorkey`: opaque pure-black guest pixels (RGB 0) are see-through to that plane. **Scope check:** the firmware's menu backgrounds are `FF101010`
  (not black), so only the full-screen NO SIGNAL / INFO / banner clears (`FF000000`) are affected: Setup page covered pixels 1,044,479 of 1,044,480.
  Whether the real GOP has such a transparent-colour key (or uses window alpha / another mixer mode) is **not established**; no GOP register was identified as the key.
* Eight guest-controlled states with framebuffer SHA-256, GOP windows and the exact options: `evidence/ui-states-session10/ui-states.json` (screenshots next to it).

## Open

The scaler/mixer register that produces the blue frame; GOP transparency/alpha/key programming; panel output routing. See also `docs/gop-preview-presenter.md`.
