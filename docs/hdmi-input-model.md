# Generated HDMI / DVI input signal (EXPLORATORY)

`--exploratory-hdmi-signal MODE` (e.g. `1920x1080@60`, `1920x1080i@60`, `1280x720@50`, `720x576@50`, `640x480@60`; list in `src/video/hdmi_modes.hpp`)
or the raw form `--exploratory-input-timing HPERIOD:VTOTAL:SYNC[:VSTART:HSTART:VEND:HEND]`. Off by default; strict runs are unchanged. The values
are generated from standard CEA-861 / VESA timings; nothing here is a measurement of the real board.

## What the original firmware reads (all from its own code)
* Scaler bank 1 (main window), gatherer `805F2EA0`:
  * reg `0x1E` high byte (`5E79`): bit0 / bit1 -> status bits 0/1 (sync polarity; which is H and the sense are an ASSUMPTION), bit3 -> status `0x10` = Interlace.
  * reg `0x20` (`5E80`) `& 0x3FFF` = H period in crystal clocks; `0x3FFF` or < 10 = H sync lost (status `|= 4`).
  * reg `0x1F` (`5E7C`) `& 0x1FFF` = V total; `0x1FFF` or < 200 = V sync lost (status `|= 8`).
  * H freq x10 = (XTAL / Hperiod) / 100 (`805F1B94`), V freq x10 = H freq x10 * 1000 / V total (`805F1BD8`); XTAL 12 MHz is an ASSUMPTION that
    the firmware's own banner confirms (it reports 60 Hz for the 1080p60 timing).
* Active window, gatherer `805F2FE0`: x = reg `0x13`, y = reg `0x12`, width = reg `0x15` - x + 1, height = reg `0x14` - y + 1 (12 bit each).
* HDMI sources (`0x50`-`0x53`): `805EA9D8` -> `805FB8BC(port)` bit 15 of `0x1F22145C` (port 0) / `0x1F221460` (port 1, source `0x52`): set = clock lost.
* HDMI packet status: `805FB0F8` reads `0x1F221584`; `805EA6DC` splits the low byte into 8 flags at `0x806B9610`; `803E1520` debounces flag 0 and
  then re-reads the AVI InfoFrame (`805EA800`). With the flags at 0 the firmware treats the input as **DVI**.

## Results (native, from the NO SIGNAL snapshot, HDMI1 = raw `0xF7`, then INFO `0x16`)
| mode | the firmware's own INFO banner |
|---|---|
| none (no signal) | NO SIGNAL dialog |
| 1920x1080@60 | **DVI1 1080p@60Hz** (NO SIGNAL hidden, audio DSP re-activated, `MApp_Scaler_SetVideo3DMode` runs) |
| 1280x720@50 | DVI1 720p@50Hz |
| 720x576@50 | DVI1 576p@50Hz |
| 640x480@60 | DVI1 640x480@60Hz |
| 1920x1080i@60 | DVI1 1080i@60Hz (needs V total = whole frame and the window over both fields; per-field values give 1920x540) |

## Input selection
The generated timing answers only while the firmware selects the digital input: SC bank 1 reg `0x02` bits 2:0 = 7 (observed HDMI `0x407`/`0x487`,
AV `0x584`, YPbPr `0x4F0`, VGA `0x80`). On ATV/AV/SCART the TV keeps showing NO SIGNAL.

## Picture (`--exploratory-input-picture FILE.bmp`, default: a built-in generated landscape with a colour-bar strip)
Once the firmware detects the signal it programs the pipeline itself (bank-resolved write log, HDMI1 1080p60 vs no signal): capture window bank 1 regs
`0x04`..`0x07` = V 41, H 192, 1080 x 1920; IPM fetch 1920 / 1080 lines (bank 0x12); scaling 1:1 (bank 0x23; the panel LC420WUE is 1920x1080);
YUV->RGB matrix (banks 0x10 / 0x0F); bank 0x10 reg `0x19` written 0. The presenter (`Msd309Machine::publishPreview`) draws the picture behind the OSD only while
bank 0x10 reg `0x19` = 0 (main-window mute, meaning inferred from that write), inside the main display window (bank 0x10 regs `0x08`..`0x0B`, panel pixels;
observed 112..2031 x 0..1079 = the panel DE window regs `0x04`..`0x07`), cropped like the capture window. Nearest scaling.
Verified behaviour: plug in -> picture, WIDE -> the firmware's aspect modes crop/zoom it, unplug -> the firmware mutes and shows NO SIGNAL again.

## Colour pipeline (picture settings act through the firmware's own registers)
The picture is not pasted over the screen as-is: it goes through the colour registers the original firmware programs, so the PICTURE menu changes it.
* Tested by register diff (write log from the NO SIGNAL snapshot, HDMI1 1080p60, menu setting changed with the virtual remote):
  * Contrast +40 steps rewrites the VOP matrix bank 0x10 regs `0x26`..`0x2E` (writer 805DD950): `0574 03BF 0000 11A1 03BF 10A7 0000 03BF 066F` ->
    `058F 03D1 0000 11A9 03D1 10AA 0000 03D1 068F` (every coefficient ~+2%).
  * Brightness +40 steps writes bank 0x0F regs `0x36`/`0x37`/`0x38` (writer 805DE4FC..805DE50C): `03F0` -> `04C8` each; the matrix is unchanged.
* Model (ASSUMPTIONS, labelled in the code): input RGB -> YCbCr BT.601 full range (Cb/Cr centred); matrix rows R/G/B x columns Cr/Y/Cb,
  13-bit sign-magnitude s2.10 (bit 12 = sign; with the base values this is the standard YCbCr->RGB inverse with Y gain 0.94), enable = bank 0x10
  reg `0x2F` bit 0; brightness = (reg - 0x400) / 4 added to R/G/B after the matrix (11-bit, 10-bit output units). Gamma, DLC, sharpness and
  noise reduction are not applied.
* Result: Contrast up -> brighter, more saturated picture; Brightness up -> raised black level (the black bar turns grey), as on a real set.

## Run time and browser
* Remote functions `hdmi-signal=MODE` / `hdmi-signal=off` (plug / unplug) and `input-picture=FILE.bmp` (empty = built-in), e.g. in `--ir-script`.
* Live page box "HDMI input": mode list, "Picture..." (any image, converted to a 32-bit BMP in the browser), "Switch to HDMI1" (raw `0xF7`), Plug in / Unplug.
  After Restart/Reboot a plugged cable is plugged in again.

## Not modelled yet
* HDMI packets (flag 0 / AVI InfoFrame) -> the banner says DVI1, as a real set does for a source that sends no HDMI packets (e.g. a DVI source).
* Gamma/DLC/sharpness/noise reduction, audio from HDMI, HDCP, and the sub window. Colour/Tint are folded into the same matrix by the firmware
  (expected, not yet tested by a register diff).
