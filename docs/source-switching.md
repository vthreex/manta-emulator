# Direct source switching through the original firmware

All results: NO SIGNAL checkpoint (`docs/save-state.md`), ONE genuine virtual-remote raw byte (original PM IR FIFO -> key reader -> app code ->
`8048710C(source_id)`), no guest function called, no firmware byte patched. No video signal exists, so every switch ends back at NO SIGNAL (that is
the correct original behaviour without an input). The SOURCE button is raw `0x41` (list below); the eight direct source keys are separate raw bytes.

## SOURCE key and the Input Source list (found late; earlier notes saying "no source list" were wrong)

**Raw `0x41` is the SOURCE key.** Handler `8046AE40` stores code `0x20` when bit 3 of `0x806C0868` is set (otherwise `8046ABE4`); the original firmware then opens its
**Input Source** list: DTV, ATV, AV 1, AV 2, S-Video, SCART, YPBPR, HDMI1, HDMI2, VGA, USB and "OK: ENTER" (575,232-pixel panel, current source highlighted).
UP `0x0B` / DOWN `0x0E` move the highlight, OK `0x0D` calls the original `8048710C(id)`. It was previously logged as an "unnamed panel" because the earlier
sweeps only looked for the *banner/list names* in the sweep summary, not at the frame. From the NO SIGNAL checkpoint: SOURCE, UP x3 (SCART -> AV 1), OK
completes the AV 1 switch (banner "AV 1", then NO SIGNAL) under the same opt-in hypotheses as the direct key; the list auto-hides after about 100M dispatches.
Raws `0x15` / `0x1C` open the volume bar (level "15"). Evidence: `evidence/ui-states-session10/9_input_source_list.png`, `tests/source_switch_regression.py` (`list_av1`).

## Matrix (banner text is drawn by the original ZUI from its own string resources)

| raw | app | source id | banner | what the original needed (hypotheses are opt-in, labelled) | end state |
| --- | --- | --- | --- | --- | --- |
| (initial) | | | SCART | none | connected SCART/NO SIGNAL |
| `0x4E` | 0x62 | **7** | **AV 1** | provenance ext + GOP wait `1F203FE0` bit 8 | **switch completes**, back to NO SIGNAL (~80M dispatches), no stop |
| `0xF3` | 0x63 | **9** | **S-Video** | same | **switch completes**, back to NO SIGNAL (~82M), no stop |
| `0xEF` | 0x5F | 3 | **YPBPR** | + scaler bank 1 status `1F205E30/31`, bank 6 `1F205E88/89` (bracket 0; guest-writable) | banner, NO SIGNAL at ~106M; stops at `805FAE94` reading `1F204C20` (~111M) |
| `0xF5` | 0x60 | 4 | **VGA** | same as source 3 | same stop |
| `0xF7` | 0x61 | 5 | **HDMI1** | + audio indirect `1F225DD8` (`0x2DEC & 0x3F`), `1F225CBC`, `1F225818`, VIF-side `1F22145C/D`, scaler `1F205E30/31` (all bracket 0, `+rmw`) | banner at ~110M, runs to the 121M budget |
| `0xF9` | 0x5C | 0 | not reached | audio `1F225DD8`, `1F225CBC`, then `1F202844` (stop `80544D80`; the guest *writes* it at `80544D94`) | blocked: `1F202844` needs a writable-register model |
| `0x4B` | 0x5D | 1 | (first banner frame drawn, text not yet) | see below | stops at the VIF carrier-lock registers |
| `0xF6` | 0x5E | none | SCART info bar only | `8048710C` is not called | information bar (OSD 3) only |

Initial DTV (id 28) is not reachable from the direct keys.

## USB, DTV and ATV (selected through the Input Source list)

| source | id | result | blocker / needs |
| --- | --- | --- | --- |
| **USB** | 10 | **reaches the original media player**: "No Device", PHOTO / MUSIC / MOVIE / TEXT (OSD `0x1A`); RIGHT/LEFT move the selection (PHOTO -> MUSIC); UP/DOWN/OK/MENU draw nothing with no device | audio `1F225DD8`, `1F225CBC` = 0 (+rmw), `MOVZ` support in the RMW prover (opt-in). EXIT from the player stops in the scaler restore chain: `1F205E78@bank1`, `1F205E10@bank0x1A`, `1F205F80@bank0x19`, then a rewrite of a bracketed byte at `805F887C` |
| **DTV** | 0 | **works**: the original DTV channel banner (channel 1 "TVP1", clock, "No Information", "DTV") appears at ~250M dispatches | the `805DB6C0..805DB6D8` mailbox handshake (`0x78/0x56` written to `1F206728/29`, polled until a coprocessor changes it) is acknowledged by a new labelled hypothesis kind `+ack` (the next read after a guest write returns the stated value); plus reset values (`+init`) for `1F225DD8/CBC/CB0`, `1F004074/75`, `1F203600/01`, scaler bank 1 `1F205E78/79`. No tuner or signal exists, so "No Information" is expected. `tests/dtv_regression.py` |
| **ATV** | 1 | **works**: the original TV banner (channel 1, "-----", clock, "No Information", "TV") at ~97M dispatches, then NO SIGNAL at ~158M | needs the checkpoint with the AFEC `BF206BA8` bit-4 reset value seeded (`tools/make_ba8_checkpoint.py`, a ~10 minute cold boot) plus reset values (`+init`, 0, lazily seeded at first read for scaler banks) for scaler-bank bytes `1F205E34/35/30/31 @16`, `1F205E10/11 @26`, `1F205F80/81` and `1F205E4C/4D @25`, `1F205E38/39 @34` and the VIF carrier-lock byte `1F224471`. `tests/atv_regression.py` |

Findings behind this: `80544D74` is a TSP helper (`LBU / ANDI / ORI / MOVZ / SB` on TSP+0x2844 bit 0); with `--exploratory-provenance-extensions` the RMW prover now
accepts `MOVZ/MOVN` whose condition register is independent (the chosen operand's bit dependencies carry over). The USB player needs no USB device model to show
"No Device"; real device content (mass storage / FAT) is not modeled. Evidence: `evidence/ui-states-session10/10_usb_player_no_device.png`,
`11_usb_player_music_selected.png`, `tests/usb_player_regression.py`.

Bank-scoped `+init` hypotheses are seeded lazily at the first read of that byte while its bank is selected (seeding at restore time disturbed an unrelated exploratory display-clock check).

Hypothesis kinds now: `+rmw` (writable status byte, carried-back stores allowed), `+init` (reset value; re-applied after a restore to bytes that are still unknown, bank-scoped ones seed their own bank), `+ack` (peer-acknowledged mailbox). The allowed address range is the whole RIU window `1F000000..1F3FFFFF`.

Hypothesis plumbing: same-address hypotheses may now coexist when their masks are disjoint (or their scaler banks differ); a restored snapshot keeps every
process-configured hypothesis keyed by address + bank + mask.

## What each blocker is (all from the original code / provenance, none supplied by default)

* **Identity read-write-back** `80504374` (`LBU/ANDI FF/SB` of AFEC `3C4D`): the unknown byte is written back unchanged. Strict mode stops there. Accepted only
  with `--exploratory-provenance-extensions` (also `INS` bit-field insert on a value with unknown bits, `803FD9E0`). Always-on it diverged the strict cold boot
  (`stopped at 814,470,984`), so it is opt-in.
* **GOP wait** `805392F8`: the firmware sets bits 1:0 of GOP bank register `0x1E0` (`1F203FE0`, via `8053A210`) and polls bit 8 up to 65,537 times
  (`8053A140` reads). The register is a writable control register with a hardware status bit: `--exploratory-mmio-bits 1f203fe1:01:V+rmw`. **V = 1** passes at once;
  **V = 0** runs the firmware's own timeout (about +28M dispatches) and *also* continues, with identical banner pixels. The existing GOP update model covers only
  the bank/ack register `1F203FFC`; `1F203FE0` has no model.
* **AFEC `BF206BA8` cache** (`805014C8`): the boot copies this byte to RAM `0x806D83BE` (`80502EAC`); bit 4 decides `80503558(1)`. Its reset value is unknown and
  the checkpoint already froze it as unknown, so the hypothesis must exist when the checkpoint is created: `--exploratory-mmio-bits 1f206ba8:10:V+init`
  (new reset-value kind: the bit starts known, guest writes behave normally). Checkpoints `ns-ba8i-2.snap` (V = 0) and `ns-ba8i-3.snap` (V = 1) were produced by
  cold boots (~10 min each); both values continue identically.
* **VIF carrier lock** (source 1): `80505E58` reads `1F224471` (CR_LOCK_STATUS), `80509AF0` reads `1F224470` (CR_FOE); `--diagnostic-vif-cr-lock 0/1` moves the stop
  between them. The analogue-TV (VIF/AGC) path needs more of the VIF model; not built.
* **Scaler status** (`805FFF7C`): scaler bank 1 register `0x18` high byte is tested for non-zero in the source-apply code; it is bank-selected (`1F205E00`), so a
  byte hypothesis is broad. Bracket value 0 passes; non-zero stops at `805F887C` (the guest rewrites `1F205E88` with new values).
* **Audio indirect register** `0x2DEC` (`1F225DD8`): HAL audio read through the index port `1F225A30`; reset values and DSP status are not modeled.

## Checkpoint rules

Options whose state is stored inside the snapshot cannot be added to an existing checkpoint: `--load-state-allow-extra-options` now **applies** the supported
additions (`--exploratory-mmio-bits`, `--diagnostic-vif-cr-lock`, `--exploratory-provenance-extensions`, `--trace-instructions`) and **refuses** all others with a
message (previously they were silently ignored; for example `--diagnostic-vif-cr-lock` had no effect on a restored run). Snapshots restored this way keep the
`+rmw` / `+init` flags of the current process's hypotheses.

## Reproduce (completed switch)

```bash
python tests/source_switch_regression.py --firmware ../../work/firmware-original.bin --emulator ../../work/build/manta_final.exe --report out.json
```
or `--load-state work/checkpoints/no-signal-ready-v2.snap --load-state-allow-extra-options --exploratory-provenance-extensions
--exploratory-mmio-bits 1f203fe1:01:01+rmw --ir-script 1300000000:raw=0x4e --gop-frame-history DIR`.

## Not established

Which real remote button is SOURCE; the names of ids 0, 1, 2, 6, 8 and 28; how a TV with an actual signal behaves; the hardware meaning of every bracketed register.
Brackets of 0 are assumptions, not measurements.
