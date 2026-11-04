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

## Not modelled yet
* HDMI packets (flag 0 / AVI InfoFrame) -> the banner says DVI1, not HDMI1; colour space and video code are not provided.
* The picture: the firmware programs the scaler for the input, but no video pixels are generated, so the screen behind the OSD stays empty.
* Hot plug / changing the mode at run time, and the browser settings.
