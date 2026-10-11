# What does `Hello T4` mean?

The best-supported identification is **T4 = Titania4**, MStar's internal TV SoC/platform name. It is a platform/build label in this firmware, rather than a user-facing Manta model name.

Direct evidence from the original recovered runtime includes ` @@@ Hello T4`, `T4 DEC system`, `T4 SE system`, `MSVC00T4R00.05.00MSTAR---1003090`, and `KTC_MSD309_V01`. These are original guest strings; the host emulator does not supply the greeting. See [runtime strings](../evidence/forensics/runtime-strings.json) and [observed serial output](../evidence/prefix-uart-console.log).

Independent reverse-engineering documentation groups MSD309PX under [Titania4](https://linux-chenxing.org/titania4/). This supports the platform identification, but does not establish the exact Manta chip suffix, board wiring, or silicon revision. The expansion of the abbreviation is inferred from these combined sources; no defining `T4 = Titania4` constant has been recovered from this dump.

The message is a developer startup greeting. Reaching it demonstrates application initialization code running, not a completed cold boot, scheduler, display, or usable TV menu. In the diagnostic run the next blocking read is `8053BDE4 / BF203D05`, GPIO 66 input bit `04` during probable bitbanged I2C initialization. The nearby string `IIC init fail SDA:%x SCL:%x` corroborates that interpretation.
