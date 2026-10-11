# Original display initialization continuation

Continues `1f79adc` using the existing CPU engine, immutable original SPI dump and original reset path. **No authentic framebuffer or TV frame has been demonstrated.** The furthest run is explicitly exploratory; the additional clock and reset-state assumptions below are not validated hardware behavior.

## Observed result

| Requested observation | Result |
|---|---|
| `BF205E78`, SC bank 1, bit 0 | Strongest supported identification: output VSYNC monitor; bank 3 is the stock subwindow selection. Input-sync reader uses bit 2. Exact T4 monitor polarity and clock routing remain unresolved. |
| State transitions | Implemented only with explicit experimental output clock; derived from guest-programmed scan geometry and virtual time, never toggled per read. |
| After the old `805FBC8C` boundary | Three original waits return with zero pulses remaining; original scaler initializer returns 1; original DLC routine uploads curves, takes its own timeout paths and returns; picture-quality COMB table loading begins. |
| Furthest original dispatch | `805F883C`, 279,469,882 dispatches; a guard stops before the original halfword store. 20,334 unique PCs and 3,051,644 MMIO events. |
| Original timer handlers | 606 exception-vector entries; original software ticker also 606. |
| Scaler/panel state | Guest writes 1920x1080 active geometry, totals 2200x1125, VSYNC lines 1101..1109 and 768 gamma samples. Twelve observed sync transitions, three successful stock waits. Twenty virtual scan periods are **not twenty rendered frames**. |
| Original GOP initializer `80539CE4` | Zero entries, including wrapper `805333AC`; zero GOP aperture and candidate GE aperture events. |
| Guest framebuffer or OSD pixels | No demonstrated allocation or rendering. GOP descriptors, palettes, MIU selection and compositor remain incomplete. |
| Native window | Starts independently, remains blank. No genuine pixels rendered. |
| New original UART messages | None beyond the previous `sar input test...`; `@@@ Hello T4` and `USB_Init_Configure done...` remain present. |
| Strict POR / undocumented CF | Unresolved. Strict candidate boundary remains `80469898 / BF200A38`; guarded CF boundary remains `8053DD14`. Four cached POR bytes and twenty stale clock-origin bytes stay invalid. |

Evidence: [summary](../evidence/display-sync-summary.json), [PC coverage](../evidence/display-sync-coverage.txt), [UART](../evidence/display-sync-uart.log), [selected checkpoints](../evidence/display-sync-checkpoints.json), [full compressed trace](../evidence/display-sync-full-trace.jsonl.gz), [trace/reference hashes](../evidence/display-sync-provenance.json), [original disassembly and callers](../evidence/spi-display-disassembly.json). Decompress the trace before passing it to `--replay-trace`.

## Why the original edge wait was blocked

Stock `805FBC54..805FBCAC` saves the SC selector, selects bank 1 or 3, reads `BF205E78` at `805FBC8C`, restores the selector and returns bit 0. `805F24A4` waits for complete high-to-low pulses and uses the original software ticker for its timeout. Call `805F25C8` requests two pulses with a 200 ms limit. Later main/subwindow waits request another two each. This was an emulator-rejected unknown status read, rather than an observed exception storm.

The named MIPS reference `Hal_SC_ip_get_output_vsync_value` uses bank 1/3 register `1E`, bit 0, while `Hal_SC_ip_get_input_vsync_value` uses bit 2. The [Marlon XC archive](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libapiXC.a) inverts its output bit; the original T4 routine does not. This supports the signal identity, not identical polarity, panel-clock frequency or subwindow clock coupling.

The experimental counter arms only after the guest supplies complete geometry and clears the observed display-clock gate bit. It counts pixels against the existing 312 MHz virtual dispatch timebase. `148500000` gives 60 Hz for the programmed totals; this frequency is a configured hypothesis, not a recovered PLL value. Halving it must take more guest time to satisfy the same waits. Unknown neighboring status bits still stop. Missing geometry or a disabled clock cannot produce a fabricated edge.

## Subsequent original initialization and guards

The original subwindow predicate `805FE024` reads SC bank 20 register 10 bit 1. The named reference `Hal_SC_subwindow_disable` corroborates this field. Initial enable is unknown. Setting it to 1 experimentally stops before unknown cached subwindow dimensions affect a comparison; 0 allows the stock unused-subwindow path. Original scaler initialization `805F8300` then returns at `805F833C` through caller `803E0BA0`.

Stock `805E3874` matches the structure of named `msDlcInitWithCurve` in the [Marlon DLC archive](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/drv_bsp/marlon_nos_mips/lib/libapiDLC.a). SC bank 1A register 4 bit 2 requests capture-range update; bit 3 is polled for completion. A guest command write previously risked making status bit 3 known as ordinary register storage. It now invalidates that asynchronous status. Without an explicit experiment, execution stops at `805E3D14` before consuming unknown completion. `--exploratory-dlc-timeout` holds only the proven consumed completion bit low, causing the **original timeout path**, not successful hardware completion. The completion producer, clock, latency and reset state remain unknown.

Original luminance-statistic and chroma-gain getters cache unknown samples. New bit provenance tracks them through original shifts, masks, byte concatenation, RAM and MIU1 aliases. They remain unknown; branches, addresses and new-device use stop. Restoration is allowed only to the same banked byte/bit identity. This observer never executes, patches or skips guest instructions.

COMB banking at `BF206C00..BF206DFF` is corroborated particularly strongly by [_MDrv_PQ_DumpCombRegTable](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/driver/pq/include/drvPQ_Template.h) and [COMB_REG_BASE](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/driver/pq/hal/marlon/include/hwreg_pq.h). The source's T4-specific workaround addresses `47/5F/8F/D7/DF`, exactly matching the stock loader. Banked storage is implemented; initial selector is still unknown.

The current blocker is bank 0 register `8F` high byte `BF206D1D`, bits 4/5. Stock `803C69B4..803C6A08` snapshots both bytes, preserves those high-byte bits with mask `30`, combines table value `C3`, duplicates the resulting byte into both halves, writes a halfword, then restores the saved low byte. At `805F883C` the original store targets `BF206D1C`; it would temporarily move unknown high-byte bits into the low byte. The guard rejects this before the write. The summary address is zero because the instruction observer stops before the bus callback; the target is reconstructed from original instruction and GPR4. Merely treating this as a harmless latch would assume unknown COMB side effects. No arbitrary reset value was supplied for those bits.

GOP follows further application initialization: `803E0B94` invokes scaler/color/DLC/PQ work; a later call at `804044E4` enters `805333AC`, which calls `80539CE4` at `80533670`. These targets have execution instrumentation but have not run. Static buffer arguments are not evidence of allocated or drawn guest framebuffers.

## Reproduction

The furthest **experimental** path:

```powershell
.\bin\manta.exe --firmware ORIGINAL.bin --headless `
  --symbolic-init --candidate-miu --candidate-bdma --candidate-uart --gpio-i2c `
  --defer-diagnostic-reads --virtual-rtc --cp0-timer --candidate-utmi --candidate-spi `
  --exploratory-spi-input pull-up --candidate-chip-id --exploratory-sc-bank 0 `
  --candidate-gamma --exploratory-gamma-enable 0 `
  --exploratory-display-clock 148500000 --exploratory-subwindow-enable 0 `
  --exploratory-dlc-pending 0 --exploratory-dlc-timeout --exploratory-comb-bank 0 `
  --max-instructions 400000000 --summary display.json --record-trace display.jsonl
```

Every new experimental option requires `--symbolic-init --defer-diagnostic-reads`. Omit display clock to preserve the old edge-read boundary. Omit subwindow initial state, DLC initial request, DLC timeout or COMB initial bank to stop at the corresponding unresolved input. Strict mode receives none of these values. A physical T4 reset/read trace is needed to establish initial SC/COMB state, monitor polarity, actual PLL clock and DLC completion; capture through existing authorized debugger/UART access or a documented passive setup, without modifying the backup or flashing the TV.

## Verification and useful donor

194 native checks and CTest 2/2 pass, including independent native window startup. Four forensics tests include 400 bounded random inputs. [Display regression](../evidence/display-regression.json) exercises original pulse waits, half-clock timing, unknown feature/completion guards, COMB loading and full deterministic replay; [baseline regression](../evidence/regression.json) retains existing reset, decompression, GPIO, timer, SPI, gamma and POR/CF boundaries. Test success does not validate the exploratory electrical/timing assumptions.

The supplied [Shivaki listing](https://kenotrontv.ru/files/file/5858-shivaki-stv-32led18s/) reports an 8 MiB dump, board `471-01A3-D3202G` and MSD306/308/309 platform, with unknown panel. Its board number differs from this Manta. Download requires sign-in; no archive has been retrieved. If supplied locally, its scaler/DLC/COMB/GOP code and initialization tables can be compared by function and offsets, never substituted as compatible firmware.
