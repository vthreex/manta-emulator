# GPIO/I2C cross-reference: original firmware and supplied schematic

The stop at `8053BDE4 / BF203D05` was the emulator's deliberate unknown-input boundary. It was not a CPU exception or an infinite firmware polling loop. The original code samples SDA once during software-I2C bus initialization; its caller decides whether the bus is idle or needs recovery clocks.

## Firmware evidence

Reproduce with `python tools/gpio_analyze.py --firmware ORIGINAL.bin --schematic REFERENCE.pdf --report gpio.json`. The tool checks the original SHA, decompressed main-image SHA, instruction encoding, descriptors and five bus tuples. [Generated evidence](../evidence/gpio-disassembly.json) includes the disassembly.

The descriptor table is at `80631F88`, with 24 bytes per logical GPIO. Each row has OEN register/mask, OUT register/mask and INPUT register/mask. The original accessor computes **`BF000000 + 2*logical - (logical & 1)`**. These are byte registers; treating `101E83` as a conventional byte offset or taking the low byte at `BF203D04` would be wrong.

| Firmware GPIO | Role in stock bus table | INPUT | OUT | OEN | Bit mask |
|---|---|---|---|---|---|
| 65 | SCL, physical pair A | `BF203D05` | `BF203D09` | `BF203D0D` | `02` |
| 66 | SDA, physical pair A | `BF203D05` | `BF203D09` | `BF203D0D` | `04` |
| 69 | SCL, physical pair B | `BF203D11` | `BF203D15` | `BF203D19` | `04` |
| 70 | SDA, physical pair B | `BF203D11` | `BF203D15` | `BF203D19` | `08` |

Five initial tuples at `806563F8` are `(65,66,40)`, `(65,66,40)`, `(69,70,40)`, `(65,66,40)`, `(65,66,40)`. Logical bus indices therefore share physical pins. These are boot defaults; later reconfiguration is not ruled out.

`8053BD98..BDF8` validates the GPIO ID, fetches its descriptor and performs:

```text
8053BDE4  lbu  a1,0(v1)      # v1 = BF203D05 at the original boundary
8053BDE8  lbu  v0,14(a2)     # descriptor INPUT mask = 04 for GPIO 66
8053BDEC  and  v0,v0,a1
8053BDF0  sltu v1,zero,v0    # Boolean pin level
8053BDF4  jr   ra
8053BDF8  move v0,v1         # return in the branch delay slot
```

The caller is `803BCF10..803BD15C`. It releases SCL and SDA through wrapper `8053BA6C` / implementation `8053BCD0`, which sets the relevant OEN bit. Thus **OEN=1 means input/released**. At `803BCFB4`, SDA high leads to sampling SCL and returning if both are high. Low SDA or SCL leads to a nine-iteration recovery path, another sample, and the stock diagnostic `IIC init fail SDA:%x SCL:%x` if unsuccessful. No slave address has been transmitted at this checkpoint.

## What the supplied PDF establishes

Source: `1019300715-MSD309-MSD308.pdf`, 162 pages. Its schematic title on page 14 says **MSD309 V7.0-A**, sheet `<02:msd309px>`, dated September 30, 2010. This is a reference board, not verified as Manta's `MSD309-T2B / 471-0101-D3202G`.

| PDF page | Observed circuit | Implication and limit |
|---|---|---|
| 14 | U7D: `PAD_DDCR_CK`, ball AA23, net SCL; `PAD_DDCR_DA`, ball AA24, net SDA | An external master-I2C pair exists. No firmware logical GPIO numbers are printed. |
| 14 | U9 `24C32/NC`: SCL/SDA pull-ups R78/R79, 4.7 kOhm to 3.3VA | Released bus levels are high even when this optional EEPROM is absent. Do not attach an EEPROM to the Manta emulator solely from this reference. |
| 14 | `PAD_TGPIO2(TUNER_SCL)`, AD4, and `PAD_TGPIO3(TUNER_SDA)`, AE4; R55/R54, 4.7 kOhm to `5V_Tuner` | A separate pulled-up tuner bus exists on the reference board. Its power rail differs from SCL/SDA. |
| 16 | TN1 `LG/NC`: SCL pin 4, SDA pin 5; S_SCL/S_SDA through R146/R145, 33 Ohm, to T_SCL/T_SDA | Confirms the reference tuner's electrical path; optional tuner types and Manta's exact tuner remain unverified. |

Visually inspected source crops: [I2CM/GPIO pads](../evidence/schematic/gpio.png), [optional EEPROM pull-ups](../evidence/schematic/eeprom.png), [front-end pull-ups](../evidence/schematic/front-end.png), [tuner routing](../evidence/schematic/tuner-pullup.png), [board title](../evidence/schematic/title.png).

**The remaining bridge is the enum-to-pad mapping.** Pair A being DDCR_CK/DA and pair B being TGPIO2/3 is a plausible architecture match, not a proven register-to-ball assignment. The PDF has no `101E83`, `BF203D05`, or GPIO enum table. A Titania4 HAL GPIO header/table matching this firmware, or a verified board capture correlated with these guest reads, is needed to close that gap. GPIO 66 must not be confused with a BGA pin numbered 66.

## Implemented and tested behavior

`--gpio-i2c` opts into a pulled-up digital bus model for the two firmware pairs. Guest OUT/OEN writes determine master drive/release; external open-drain drivers can pull low. Unknown direction, unrelated pins and push-pull contention stay unresolved. GPIO transitions feed a real START/STOP/address decoder; absent slaves NACK. No EEPROM, tuner, CI or amplifier response is fabricated.

The explicit `--i2c-sda-stuck` electrical fault fixture holds pair A SDA low. The **unchanged original guest** generates nine low/high SCL cycles and its own error message. The normal run reads both pins high and passes initialization. Without `--gpio-i2c`, the old boundary remains reproducible. [Firmware regression](../evidence/regression.json), [fault trace](../evidence/i2c-fault.jsonl), [fault console](../evidence/i2c-fault-console.log) and native direction/contention/NACK tests provide evidence.

## Next verified boundary

The candidate cold path reaches `80469898`, `lhu v1,0(v0)`, with `v0=BF200A38`. It caches this halfword at `806B914A`; the next instruction sequence intends to cache `BF200A3C` at `806B910E`. These registers have not been written by the observed guest. Later disassembly at `804BCDF0` and `804BCE2C` counts set bits in the cached words and converts the count into display text. Their exact hardware meaning and reset values are unresolved. The [independent Titania4 RIU map](https://linux-chenxing.org/titania4/riu-map.html) tentatively labels this bank POR_STATUS and explicitly calls its map an assumption.

Do not assign arbitrary zeros or calibration-success bitmaps to these reads. A documented Titania4 POR_STATUS definition, an existing trusted register dump, or a read-only capture through an already verified debug interface on the exact board would resolve the next input dependency. No physical device was accessed in this work.

The MIU candidate model infers reset bank selection MIU0 from the stock BIST0 routine running before any selector write; BIST1 explicitly selects bit15=1. Only that selector bit is known at reset. Physical capacity is not established by this alone: the observed programmed BIST ranges are 128 MiB per selected bank in the digital model. Calibration tests use the supplied pattern and its complement, not a claimed exact analog eye/pattern generator.
