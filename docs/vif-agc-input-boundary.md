# Original VIF AGC input boundary

The PM-domain IR FIFO continuation reaches854,795,034 original MIPS dispatches,
2,453,934,820 original OpenRISC instructions and38,589 original timer entries.
ZUI initialization entered/returned once. The state dispatcher, enabled GOP
window and raster output have not executed. No original frame is demonstrated.

Original application input polling returns from804F2474 after its own FIFO
EMPTY mask. No external key, PM response or activity marker is injected.
The independent cold-empty FIFO hypothesis remains explicitly unverified.

## Actual VIF control flow

804F2988 calls80504A58, which tail-calls8050962C. This is the original VIF
handler, not an audio-DSP acknowledgment or a readiness wait. The current
handler state reads these hardware observations:

| Original PC | Physical RIU address | Width | Best-supported identity |
|---|---|---|---|
|80509D34|1F224644|1|AGC_MEAN16|
|80509D38|1F22465D|1|AGC_PGA2C, low5 bits|
|80509D3C|1F224650|2|AGC_VGA|

The original PAL predicate is uint8(mean16-20)<120, meaning20..139.
The alternate modulation predicate is uint8(mean16-9)<31, meaning9..39.
If that condition, PGA2==31, or VGA==the configured minimum holds, the guest
programs line count4, clears carrier-recovery K_SEL bit6 and advances its own
software acquisition state. Otherwise it returns through its normal handler.
These are live receiver measurements used for automatic gain control.

The named reference msVifHandler and AGC definitions contain the same three
observations, threshold ranges, line-count update and carrier-recovery update:
[original source](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/device/if_demodulator/MsVIF.c),
[definitions](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/device/if_demodulator/MsVIF.h).
Their absolute register banks differ. Names are correlated by original code
and surrounding operations; the reference bank is not substituted for T4.

## Unknown data remains unknown

The original register history contains AGC configuration and gain-overwrite
pulses, but no software writer for the three live observations. They have not
been produced by an emulated IF/AGC pipeline. INVALID-only observation rules
permit collecting the three bytes/words without defining them. Execution then
correctly stops at80509D54, before ADDIU consumes unknown mean16 in arithmetic.
The original firmware is not stuck in a proven polling loop at this point;
the emulator has rejected required unmodeled signal data.

An unplugged antenna does not prove these values are zero: front-end noise,
offsets, gains and sampling state matter. No convenient signal level, maximum
gain, lock flag or successful acquisition result has been supplied. Resolving
this needs T4 AGC measurement/override/reset semantics or read-only measurements
from the original board, correlated with writes to224600,224618, gain limits,
224644,22465D and224650 across initialization. No physical hardware was accessed.

Independent unsupported boundaries remain PM51 574F/XDATA0E6A, AVD
0802/XDATAC008, DVB-T XDATA2200, exact audio DSP execution, strict POR and
undocumented SPI CF. The current display experiment preserves those limits.
