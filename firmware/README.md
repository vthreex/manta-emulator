# Original Manta LCD4214 firmware image

`manta-lcd4214-original.bin` is the unmodified SPI flash dump of the Manta LCD4214 TV
(MStar MSD309-T2B / Titania4) that this emulator boots. It is included only so the
emulator can be run and checked (interoperability / preservation).

- Size: 8,388,624 bytes
- SHA-256: `d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9`

The firmware is **not** covered by this repository's licence. The copyright in it belongs to
its owners (the TV's maker and the vendors of the code inside it). If you are a rights holder
and want it removed, open an issue and it will be taken down.

Other firmware images (other boards, dumps, saved machine states) are deliberately not included:
supply your own and see `docs/other-board-bringup.md`.

## Sunplus SPHE8600 images (Cultraview CV9202 boards)
Used by `tools/sunplus_run.cpp` and documented in `docs/sunplus-sphe8600-architecture.md`. Same rights notice as above.

| file | what | size | SHA-256 |
|---|---|---|---|
| `sunplus-shivaki-stv-24led14.bin` | Shivaki STV-24LED14, CV9202L-T, 8 MB SPI flash, software of 2014-11 | 8,388,608 | `7827b52a8ef7eed4d5955f42f2263c9295bfee20efd7ba6155b5767be3a9fa2e` |
| `sunplus-manta-led4202-cv9202h.bin` | Manta LED4202, CV9202H-A39, 4 MB SPI flash (25Q32FV), software of 2016-03 | 4,194,304 | `add6fbbbaaba6f0881c9d9dd7deb853afbf51d3ae585ad7ecefbf950c312ad06` |
