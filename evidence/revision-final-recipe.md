# Reproduce the latest original-firmware run

Supply your own immutable original Manta dump. Its required SHA-256 is `d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9` and length is 8,388,624 bytes. The first 8 MiB map as flash; the trailing 16 bytes remain outside chip capacity. Never flash this file back to a TV.

In PowerShell from the emulator directory, replace `ORIGINAL.bin` with the local backup path:

```powershell
.\bin\manta.exe --firmware ORIGINAL.bin --headless `
  --symbolic-init --candidate-miu --candidate-bdma --candidate-uart --gpio-i2c `
  --defer-diagnostic-reads --virtual-rtc --cp0-timer --candidate-utmi `
  --candidate-spi --exploratory-spi-input pull-up --candidate-chip-id `
  --exploratory-sc-bank 0 --candidate-gamma --exploratory-gamma-enable 0 `
  --exploratory-display-clock 148500000 --exploratory-subwindow-enable 0 `
  --exploratory-dlc-pending 0 --exploratory-dlc-timeout `
  --exploratory-comb-bank 0 --exploratory-comb-paired-restore `
  --exploratory-gop-bank 0 --candidate-semaphore --exploratory-semaphore-idle `
  --candidate-ge --exploratory-ge-reset --candidate-gop-palette `
  --candidate-tsp --exploratory-tsp-reset-low --exploratory-tsp-cpu `
  --defer-platform-identity --exploratory-pm-ac-on --exploratory-sar-fullscale `
  --exploratory-ir-dma-reset --exploratory-gop-reset-windows `
  --exploratory-scart-grounded --exploratory-scmi-format-byte 0 `
  --exploratory-analog-disconnected --exploratory-pq-sram-idle `
  --exploratory-pq-disabled-ports --graphics-probes --osd-probes `
  --or1k-trace-first-visits --or1k-dispatch-divider 4 `
  --max-or1k-instructions 400000000 --max-instructions 2500000000 `
  --exploratory-fetch-revision-byte 2 --candidate-vd-mcu-program `
  --exploratory-vd-mcu-startup --defer-vd-sfr8e-write `
  --defer-vd-reset-key-writes --exploratory-vd-suspended-boundary `
  --exploratory-ci-detect-high `
  --record-trace manta-private.jsonl --summary manta-summary.json `
  --coverage manta-coverage.txt
```

The exit code is `2` when original guest execution stops at an unresolved hardware input. This command contains **explicit hypotheses and deferred hardware effects**, not validated MSD309 reset defaults. It is intentionally not a finished TV. The raw trace contains original firmware program-port bytes; keep it private. The distributed excerpt omits them and is not a replay input. Use the local raw trace with `--replay-trace` or `tests/openrisc_regression.py` for an exact deterministic replay.
