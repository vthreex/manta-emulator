# MStar ISP controller, SPI NOR write path and the flash routine at `8053C914`

Continues `39e8530`. Everything that changes behavior is behind the opt-in
`--candidate-spi-nor-write`; with it off the strict boot is byte-identical (see
Validation). Nothing here is authentic displayed output.

## What the original firmware does at the stop

`BF0011C8` is read by `lhu` at `8053C9A8/9AC` inside **`8053C914`**, a SPI NOR
**page-write** routine (`write(flashAddress, length, buffer)`):

1. Takes an RTOS lock (`805CB844`, timeout `0xBB8`), reads ISP register `+1C8`,
   saves it on the stack, clears bit 2 and writes it back, writes the ISP
   enable key `AAAA` to `+00`.
2. Splits the request at page boundaries (`divu` by the page size at `[806FFCB8]`).
3. For each chunk: `06` (WREN) to `+04`, waits `+5C` bit 0, writes the three
   address bytes to `+08`/`+0C`, writes `02` (page program) to `+04`, waits
   `+5C`, pushes data bytes through `+10` (waiting on `+58` bit 0, except for
   a full 256-byte page which writes the first byte immediately), releases CS
   with `+20 = 1`, and calls **`8053C19C`**, which polls `RDSR` (`05`) until
   BUSY (bit 0) clears (up to `timeout * 100,000,000` polls).
4. Restores `+1C8` bit 2 from the saved copy (`8053CB80` branch) and unlocks.

Its caller chain is `80468E70` (a 0x430-byte record appender that flushes
every 64 records) -> `803E7A48` -> chunks of at most 1 KiB through `8053C008`,
bracketed by `8053BFF0`/`8053BFF8` (block unprotect/reprotect). It is
**persistent record storage** in the application's polling path, not a boot
requirement of the UI. The boot itself already issues `06` then `01 BC`
(`WRSR`: SRP0, TB and BP2:0 = 111, i.e. the whole array protected) at
`8053D158..D1E4`.

## ISP register layout (MIPS offset = 4 x word index from `BF001000`)

| Offset | MST786 name (bank 08) | Evidence |
|---|---|---|
| `+00` | ISP enable key (`AAAA`) | original code; earlier SPI model |
| `+04` | `SPI_COMMAND` (write starts the operation) | MST786 index `01h`; original code |
| `+08` | `ADDRESS1/2` A[15:0] | MST786 `02h`; original `8053CA84`, `CC54` |
| `+0C` | `ADDRESS3` A[23:16] | MST786 `03h`; original `8053CA8C` |
| `+10` | `WDATA` | MST786 `04h`; original |
| `+14` | `RDATA` | MST786 `05h`; original `8053C228` |
| `+18` | `SPI_CLK_DIV` | MST786 `06h`; original `0x40` |
| `+20` | `SPI_CE_CLR` | MST786 `08h` |
| `+30` / `+54` | read request / `SPI_RD_DATA_RDY` | MST786 `18h` / `15h`; original |
| `+58` / `+5C` | `SPI_WR_DATA_RDY` / `SPI_WR_CM_RDY` | MST786 `16h` / `17h`; original |
| `+A8` | `TRIGGER_MODE` (`3333`) | MST786 `2Ah` |
| **`+1C8`** | **not in MST786** | read, bit 2 cleared and later restored around every write |

`+1C8` bit 2 behaves like a write-protect pin control (the SRP0 bit written at
boot only matters with WP# low), but its polarity is **unestablished**. The
model keeps the register as plain recorded storage with INVALID reset content
(INVALID-observation grants bound to the routine's code hash,
`configs/diagnostics/opaque-spi-flash-write.tsv`), and the diagnostic
`--diagnostic-restore-by-identity-branch` handles `8053CB80`: both arms rewrite
bit 2 to the value just tested, so the representative arm is executed and the
bit is relabelled unknown with its original identity at the join `8053CBA4`.

## Confirmed vs inferred

- **Confirmed by original instructions + MST786**: command/address/data/CE-clear
  register roles, ready-flag roles, `06`/`02`/`05`/`01` opcode use.
- **Strong inference**: the ISP appends the three address bytes after `02`
  (the routine never sends them itself); WDATA is a one-entry buffer that is
  free while the opcode/address phase shifts out (the 256-byte path writes the
  first byte with no ready check).
- **Assumptions (diagnostic)**: WP# level is not modeled (SRP0 never blocks WRSR);
  busy intervals are typical Winbond-family figures on the 312 MHz virtual
  cycle domain (page program 0.7 ms, 4 KiB erase ~30 ms, WRSR ~10 ms); only
  `BP2:0 = 000` and `111` are modeled and any other protect pattern stops.
- **Unresolved**: `+1C8` meaning/polarity, the real flash marking (W25Q64BV is
  a candidate only), and the status-register upper bits (RDSR still reports only
  BUSY and WEL as known).

## Implemented

- `SpiNor` (`src/storage/spi_nor.hpp`): WEL on CS rise, WRSR (needs WEL, stores
  the protect bits, BUSY), page program (needs WEL, not busy, protection check,
  256-byte page wrap, programming is old AND new, 0->1 requests counted),
  4 KiB / 32 KiB / 64 KiB erase (aligned down, protection check), BUSY in RDSR,
  read-while-busy rejected, write-class opcodes not seen in the firmware
  (`11 31 42 44 50 60 C7`) guarded, protection state unknown until the guest
  writes the status register, `reset()`.
- `SpiIsp`: address registers and the automatic address phase for `02/20/52/D8`,
  command-mode data through `+10`, the shifter/buffer timing described above.
- `Flash` (`src/storage/flash.*`): `programAnd`, `eraseRange`, overlay and stock
  digests on the existing 4 KiB copy-on-write overlay. The stock image and the
  input file are never written (`--nvram` still aliases-checks the input).
- The machine mirrors every guest program/erase into the CPU-visible flash
  windows (`1FC00000`, `14000000`) so later guest reads agree.

## Diagnostic result (not authentic boot; options as in `docs/htotal-branch-join.md` plus
`--candidate-spi-nor-write --diagnostic-restore-by-identity-branch`, grants in
`configs/diagnostics/opaque-spi-flash-write.tsv`)

| Step | Outcome |
|---|---|
| ISP `1C8` reads granted INVALID | routine proceeds into the page loop |
| full-page first byte written without a ready check | needed the buffer model above (earlier stop: transmit FIFO overrun) |
| firmware unprotect write | `WRSR 0xB8` = SRP0, TB, BP=110: lowest 2 MiB protected; target is `0x7C0000+` |
| routine completes | **7 page programs, 1,330 bytes, 2 overlay sectors modified, 0 erases, 0 ignored writes, 0 zero-to-one violations**; `8053CB80` restore branch executed and `8053CBA4` reached |
| MIPS | **855,843,242** dispatches (+1,048,071 vs the strict stop), **1,907** new original PCs |
| next stop | `803BD910` reads `BF200E10` (byte 8, tested with `andi 0x40`) in a USB-state function `8054E3C0`; a hardware status input that has no model |

Platform polling (`8046CA74`) still has not returned, `8047602C` and `804E0DC4` are not
reached, and no GE command or pixel is produced. The overlay lives only in memory (the
run summary records `flash_overlay_sha256` and the unchanged `flash_stock_sha256`); the
input firmware file's SHA-256 is identical before and after.

## Validation

- Native checks: **1,804** (was 1,763) including immutability, copy-on-write
  visibility, NOR AND-rule, page wrap, sector isolation, WEL/busy/protection,
  guarded opcodes, reset, deterministic overlay digest and an ISP end-to-end sequence.
- Strict replay of the recorded 6,592,741-event trace with the new binary and
  write support **off**: all 289 original summary fields, coverage and PM
  history identical; 8 new diagnostic summary fields are zero/constant.
